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
                core::JobSystem::Instance().Schedule([this, assetPath, callback]() {
                    WaitForAsset(assetPath);
                    std::lock_guard<std::mutex> lock(m_assetsMutex);
                    auto it = m_assets.find(assetPath);
                    if (it != m_assets.end() && callback)
                    {
                        callback(it->second);
                    }
                }, core::JobPriority::Low, "wait_for_asset");
                return;
            }
        }

        AssetLoadResult& entry = m_assets[assetPath];
        entry.assetId = assetPath;
        entry.type = expectedType;
        entry.state = AssetState::Loading;
    }

    ++m_currentlyLoading;
    m_loadCounter.Increment();

    core::JobSystem::Instance().Schedule(
        [this, assetPath, expectedType, callback]() {
            LoadAssetInternal(assetPath, expectedType, callback);
        },
        priority,
        "load_asset"
    );
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
    AssetType expectedType,
    AssetLoadCallback callback
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
        
        if (callback)
        {
            callback(result);
        }
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
        
        if (callback)
        {
            callback(result);
        }
        return;
    }

    file.seekg(0, std::ios::end);
    const auto fileSize = file.tellg();
    file.seekg(0, std::ios::beg);

    result.data.resize(static_cast<std::size_t>(fileSize));
    file.read(reinterpret_cast<char*>(result.data.data()), fileSize);

    result.state = AssetState::Loaded;

    {
        std::lock_guard<std::mutex> lock(m_assetsMutex);
        m_assets[assetPath] = result;
    }

    ++m_totalLoaded;
    --m_currentlyLoading;
    m_loadCounter.Decrement();

    if (callback)
    {
        callback(result);
    }
}

}
