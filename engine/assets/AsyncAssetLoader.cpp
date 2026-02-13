#include "engine/assets/AsyncAssetLoader.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>

namespace engine::assets
{

bool AsyncAssetLoader::Initialize(const std::string& assetsRoot)
{
    if (m_initialized)
    {
        return true;
    }

    m_assetsRoot = assetsRoot;
    m_totalLoaded = 0;
    m_totalFailed = 0;
    m_currentlyLoading = 0;

    m_assets.clear();
    m_initialized = true;

    std::cout << "[AsyncAssetLoader] Initialized with root: " << assetsRoot << "\n";
    return true;
}

void AsyncAssetLoader::Shutdown()
{
    if (!m_initialized)
    {
        return;
    }

    WaitForAll();

    {
        std::lock_guard<std::mutex> lock(m_assetsMutex);
        m_assets.clear();
    }

    m_initialized = false;
    std::cout << "[AsyncAssetLoader] Shutdown complete\n";
}

void AsyncAssetLoader::LoadAsync(
    const std::string& assetPath,
    AssetType expectedType,
    AssetLoadCallback callback,
    core::JobPriority priority
)
{
    if (!m_initialized)
    {
        if (callback)
        {
            AssetLoadResult result;
            result.assetId = assetPath;
            result.state = AssetState::Failed;
            result.error = "AsyncAssetLoader not initialized";
            callback(result);
        }
        return;
    }

    {
        std::lock_guard<std::mutex> lock(m_assetsMutex);
        auto it = m_assets.find(assetPath);
        if (it != m_assets.end())
        {
            if (it->second.state == AssetState::Loaded)
            {
                if (callback)
                {
                    callback(it->second);
                }
                return;
            }
            if (it->second.state == AssetState::Loading)
            {
                // Asset is being loaded - add callback to pending list instead of busy-wait
                m_pendingCallbacks[assetPath].push_back(callback);
                return;
            }
        }

        AssetLoadResult& entry = m_assets[assetPath];
        entry.assetId = assetPath;
        entry.type = expectedType;
        entry.state = AssetState::Loading;
        // Store initial callback
        m_pendingCallbacks[assetPath].push_back(callback);
    }

    ++m_currentlyLoading;
    m_loadCounter.Increment();

    auto jobHandle = core::JobSystem::Instance().Schedule(
        [this, assetPath, expectedType]() {
            LoadAssetInternal(assetPath, expectedType);
        },
        priority,
        "load_asset"
    );

    if (!jobHandle)
    {
        // JobSystem scheduling failed - restore counters and run synchronously
        --m_currentlyLoading;
        m_loadCounter.Decrement();

        // Run load synchronously instead
        LoadAssetInternal(assetPath, expectedType);
    }
}

void AsyncAssetLoader::LoadBatchAsync(
    const std::vector<std::string>& assetPaths,
    AssetType expectedType,
    AssetLoadCallback callback,
    core::JobPriority priority
)
{
    for (const auto& path : assetPaths)
    {
        LoadAsync(path, expectedType, callback, priority);
    }
}

AssetState AsyncAssetLoader::GetAssetState(const std::string& assetId) const
{
    std::lock_guard<std::mutex> lock(m_assetsMutex);
    auto it = m_assets.find(assetId);
    if (it != m_assets.end())
    {
        return it->second.state;
    }
    return AssetState::Unloaded;
}

bool AsyncAssetLoader::IsAssetLoaded(const std::string& assetId) const
{
    return GetAssetState(assetId) == AssetState::Loaded;
}

bool AsyncAssetLoader::IsAssetLoading(const std::string& assetId) const
{
    return GetAssetState(assetId) == AssetState::Loading;
}

void AsyncAssetLoader::WaitForAsset(const std::string& assetId)
{
    while (IsAssetLoading(assetId))
    {
        std::this_thread::yield();
    }
}

void AsyncAssetLoader::WaitForAll()
{
    core::JobSystem::Instance().WaitForCounter(m_loadCounter);
}

std::shared_ptr<void> AsyncAssetLoader::GetAsset(const std::string& assetId) const
{
    std::lock_guard<std::mutex> lock(m_assetsMutex);
    auto it = m_assets.find(assetId);
    if (it != m_assets.end() && it->second.state == AssetState::Loaded)
    {
        return it->second.resource;
    }
    return nullptr;
}

void AsyncAssetLoader::UnloadAsset(const std::string& assetId)
{
    std::lock_guard<std::mutex> lock(m_assetsMutex);
    m_assets.erase(assetId);
}

void AsyncAssetLoader::UnloadAll()
{
    std::lock_guard<std::mutex> lock(m_assetsMutex);
    m_assets.clear();
    m_pendingCallbacks.clear();
}

AsyncAssetLoader::Stats AsyncAssetLoader::GetStats() const
{
    Stats stats;
    stats.totalLoaded = m_totalLoaded.load();
    stats.totalFailed = m_totalFailed.load();
    stats.currentlyLoading = m_currentlyLoading.load();
    
    auto jobStats = core::JobSystem::Instance().GetStats();
    stats.pendingInQueue = jobStats.pendingJobs;
    
    return stats;
}

void AsyncAssetLoader::LoadAssetInternal(
    const std::string& assetPath,
    AssetType expectedType
)
{
    AssetLoadResult result;
    result.assetId = assetPath;
    result.type = expectedType;

    std::filesystem::path fullPath = std::filesystem::path(m_assetsRoot) / assetPath;
    
    std::error_code ec;
    if (!std::filesystem::exists(fullPath, ec))
    {
        result.state = AssetState::Failed;
        result.error = "File not found: " + fullPath.string();
        
        {
            std::lock_guard<std::mutex> lock(m_assetsMutex);
            m_assets[assetPath] = result;
        }
        
        ++m_totalFailed;
        --m_currentlyLoading;
        m_loadCounter.Decrement();
        
        InvokePendingCallbacks(assetPath, result);
        return;
    }

    std::ifstream file(fullPath, std::ios::binary);
    if (!file.is_open())
    {
        result.state = AssetState::Failed;
        result.error = "Failed to open file: " + fullPath.string();
        
        {
            std::lock_guard<std::mutex> lock(m_assetsMutex);
            m_assets[assetPath] = result;
        }
        
        ++m_totalFailed;
        --m_currentlyLoading;
        m_loadCounter.Decrement();
        
        InvokePendingCallbacks(assetPath, result);
        return;
    }

    file.seekg(0, std::ios::end);
    const auto fileSizePos = file.tellg();
    if (!file || fileSizePos < 0)
    {
        result.state = AssetState::Failed;
        result.error = "Failed to determine file size: " + fullPath.string();

        {
            std::lock_guard<std::mutex> lock(m_assetsMutex);
            m_assets[assetPath] = result;
        }

        ++m_totalFailed;
        --m_currentlyLoading;
        m_loadCounter.Decrement();

        InvokePendingCallbacks(assetPath, result);
        return;
    }

    const auto fileSize = static_cast<std::size_t>(fileSizePos);
    file.seekg(0, std::ios::beg);
    if (!file)
    {
        result.state = AssetState::Failed;
        result.error = "Failed to seek in file: " + fullPath.string();

        {
            std::lock_guard<std::mutex> lock(m_assetsMutex);
            m_assets[assetPath] = result;
        }

        ++m_totalFailed;
        --m_currentlyLoading;
        m_loadCounter.Decrement();

        InvokePendingCallbacks(assetPath, result);
        return;
    }

    result.data.resize(fileSize);

    if (fileSize > 0)
    {
        file.read(reinterpret_cast<char*>(result.data.data()), static_cast<std::streamsize>(fileSize));
        if (!file || file.gcount() != static_cast<std::streamsize>(fileSize))
        {
            result.state = AssetState::Failed;
            result.error = "Failed to read entire file: " + fullPath.string();

            {
                std::lock_guard<std::mutex> lock(m_assetsMutex);
                m_assets[assetPath] = result;
            }

            ++m_totalFailed;
            --m_currentlyLoading;
            m_loadCounter.Decrement();

            InvokePendingCallbacks(assetPath, result);
            return;
        }
    }

    result.state = AssetState::Loaded;

    {
        std::lock_guard<std::mutex> lock(m_assetsMutex);
        m_assets[assetPath] = result;
    }

    ++m_totalLoaded;
    --m_currentlyLoading;
    m_loadCounter.Decrement();

    InvokePendingCallbacks(assetPath, result);
}

void AsyncAssetLoader::InvokePendingCallbacks(const std::string& assetPath, const AssetLoadResult& result)
{
    std::vector<AssetLoadCallback> callbacks;
    {
        std::lock_guard<std::mutex> lock(m_assetsMutex);
        auto it = m_pendingCallbacks.find(assetPath);
        if (it != m_pendingCallbacks.end())
        {
            callbacks = std::move(it->second);
            m_pendingCallbacks.erase(it);
        }
    }

    for (const auto& callback : callbacks)
    {
        if (callback)
        {
            callback(result);
        }
    }
}

}
