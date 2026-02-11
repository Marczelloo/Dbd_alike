#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>
#include <queue>
#include <cstdint>

#include "LoadingScreen.hpp"

namespace engine::ui
{
class UiSystem;
}

namespace engine::platform
{
class Input;
}

namespace engine::assets
{
class AssetRegistry;
}

namespace engine::render
{
class Renderer;
}

namespace game::gameplay
{
class GameplaySystems;
}

namespace game::ui
{

// Represents a single load task that can be tracked
struct LoadingTask
{
    std::string id;
    std::string name;
    std::string description;

    std::function<void(LoadingState&)> loadFunc;

    float progressWeight = 1.0F;   // Weight in overall progress
    float durationEstimate = 0.0F; // Estimated duration in seconds (optional)

    bool isComplete = false;
    float currentProgress = 0.0F;
};

// Loading context - holds references to game systems for use during loading
struct LoadingContext
{
    engine::ui::UiSystem* ui = nullptr;
    engine::platform::Input* input = nullptr;
    engine::assets::AssetRegistry* assetRegistry = nullptr;
    engine::render::Renderer* renderer = nullptr;
    game::gameplay::GameplaySystems* gameplay = nullptr;

    std::string mapName;
    std::string gameMode;
    bool isMultiplayer = false;
    bool isHost = false;
    std::string serverAddress;
};

// Different loading scenarios
enum class LoadingScenario
{
    Startup,           // Initial game startup
    MainMenu,          // Going to main menu
    SoloMatch,         // Starting single player match
    HostMatch,         // Hosting a match
    JoinMatch,         // Joining a match
    EditorLevel,       // Loading level into editor
    SceneTransition,   // Between game scenes (future)
    AssetBundleLoad    // Loading additional assets
};

class LoadingManager
{
public:
    using LoadTaskCallback = std::function<void(LoadingState&, const LoadingContext&)>;

    LoadingManager();
    ~LoadingManager();

    bool Initialize(LoadingContext& context);
    void Shutdown();

    // Begin loading process for a specific scenario
    bool BeginLoading(LoadingScenario scenario, const std::string& title = "");

    // Add a task to the loading queue
    void AddTask(const LoadingTask& task);

    // Add a task using callback pattern
    void AddTask(
        const std::string& id,
        const std::string& name,
        LoadTaskCallback callback,
        float weight = 1.0F,
        float estimatedDuration = 0.0F
    );

    // Frame update - call each frame while loading
    void UpdateAndRender(float deltaSeconds);

    // Check if loading is complete
    [[nodiscard]] bool IsLoadingComplete() const { return m_isLoadingComplete; }
    void SetLoadingComplete(bool complete) { m_isLoadingComplete = complete; }

    // Error handling
    void SetError(const std::string& error);
    void ClearError();

    // Get the loading screen (for advanced usage)
    [[nodiscard]] LoadingScreen& GetLoadingScreen() { return m_loadingScreen; }

    // Get current loading scenario
    [[nodiscard]] LoadingScenario GetCurrentScenario() const { return m_currentScenario; }

    // Cancel current loading
    void CancelLoading();

    // Scenario-specific loading tasks setup
    void SetupSoloMatchTasks(const std::string& mapName);
    void SetupHostMatchTasks(const std::string& mapName, std::uint16_t port);
    void SetupJoinMatchTasks(const std::string& address, std::uint16_t port);

    // Helper functions for common loading patterns
    static void LoadGameplayAssets(LoadingState& state, LoadingContext& context);
    static void LoadMapTiles(LoadingState& state, LoadingContext& context, const std::string& mapName);
    static void GenerateMap(LoadingState& state, LoadingContext& context);
    static void SpawnEntities(LoadingState& state, LoadingContext& context);
    static void CompileShaders(LoadingState& state, LoadingContext& context);

private:
    void Reset();
    float CalculateOverallProgress();
    void ProcessNextTask();

    // Internal task execution
    void ExecuteTaskFrame(LoadingTask& task, float deltaSeconds);

    LoadingScreen m_loadingScreen;
    LoadingContext m_context;

    std::vector<LoadingTask> m_tasks;
    size_t m_currentTaskIndex = 0;

    bool m_isLoading = false;
    bool m_isLoadingComplete = false;
    bool m_isCancelled = false;

    LoadingScenario m_currentScenario = LoadingScenario::Startup;
    std::string m_currentTitle;

    float m_totalWeight = 0.0F;
    float m_progressAccumulator = 0.0F;

    // Task-specific timing
    float m_taskFrameTime = 0.0F;
    int m_taskFrameCount = 0;
};

} // namespace game::ui
