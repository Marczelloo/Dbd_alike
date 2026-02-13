#pragma once

#include "engine/core/JobSystem.hpp"

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace engine::assets
{

enum class AssetState : std::uint8_t
{
    Unloaded,
    Loading,
    Loaded,
    Failed
};

enum class AssetType : std::uint8_t
{
    Unknown,
    Mesh,
    Texture,
    Audio,
    Material,
    Animation,
    Config
};

struct AssetLoadResult
{
    std::string assetId;
    AssetType type = AssetType::Unknown;
    AssetState state = AssetState::Unloaded;
    std::string error;
    std::vector<std::uint8_t> data;
    std::shared_ptr<void> resource;
};

using AssetLoadCallback = std::function<void(const AssetLoadResult&)>;

class AsyncAssetLoader
{
public:
    static AsyncAssetLoader& Instance()
    {
        static AsyncAssetLoader s_instance;
        return s_instance;
    }

    bool Initialize(const std::string& assetsRoot);
    void Shutdown();

    [[nodiscard]] bool IsInitialized() const { return m_initialized; }

    void LoadAsync(
        const std::string& assetPath,
        AssetType expectedType,
        AssetLoadCallback callback,
        core::JobPriority priority = core::JobPriority::Low
    );

    void LoadBatchAsync(
        const std::vector<std::string>& assetPaths,
        AssetType expectedType,
        AssetLoadCallback callback,
        core::JobPriority priority = core::JobPriority::Low
    );

    [[nodiscard]] AssetState GetAssetState(const std::string& assetId) const;
    [[nodiscard]] bool IsAssetLoaded(const std::string& assetId) const;
    [[nodiscard]] bool IsAssetLoading(const std::string& assetId) const;

    void WaitForAsset(const std::string& assetId);
    void WaitForAll();

    std::shared_ptr<void> GetAsset(const std::string& assetId) const;

    template <typename T>
    std::shared_ptr<T> GetAssetAs(const std::string& assetId) const
    {
        return std::static_pointer_cast<T>(GetAsset(assetId));
    }

    void UnloadAsset(const std::string& assetId);
    void UnloadAll();

    struct Stats
    {
        std::size_t totalLoaded = 0;
        std::size_t totalFailed = 0;
        std::size_t currentlyLoading = 0;
        std::size_t pendingInQueue = 0;
    };
    [[nodiscard]] Stats GetStats() const;

private:
    AsyncAssetLoader() = default;
    ~AsyncAssetLoader() { Shutdown(); }

    AsyncAssetLoader(const AsyncAssetLoader&) = delete;
    AsyncAssetLoader& operator=(const AsyncAssetLoader&) = delete;

    void LoadAssetInternal(
        const std::string& assetPath,
        AssetType expectedType,
        AssetLoadCallback callback
    );

    std::string m_assetsRoot;
    std::atomic<bool> m_initialized{false};

    mutable std::mutex m_assetsMutex;
    std::unordered_map<std::string, AssetLoadResult> m_assets;

    core::JobCounter m_loadCounter;
    std::atomic<std::size_t> m_totalLoaded{0};
    std::atomic<std::size_t> m_totalFailed{0};
    std::atomic<std::size_t> m_currentlyLoading{0};
};

}
