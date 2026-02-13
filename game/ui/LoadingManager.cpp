#include "LoadingManager.hpp"

#include <algorithm>

#include "engine/ui/UiSystem.hpp"
#include "engine/assets/AssetRegistry.hpp"
#include "engine/render/Renderer.hpp"
#include "game/gameplay/GameplaySystems.hpp"

namespace game::ui
{

LoadingManager::LoadingManager()
{
}

LoadingManager::~LoadingManager()
{
    Shutdown();
}

bool LoadingManager::Initialize(LoadingContext& context)
{
    m_context = context;

    if (!m_loadingScreen.Initialize(m_context.ui))
    {
        return false;
    }

    return true;
}

void LoadingManager::Shutdown()
{
    m_loadingScreen.Shutdown();
    Reset();
}

bool LoadingManager::BeginLoading(LoadingScenario scenario, const std::string& title)
{
    Reset();

    m_currentScenario = scenario;
    m_currentTitle = title;

    // Set title based on scenario if not provided
    if (m_currentTitle.empty())
    {
        switch (scenario)
        {
            case LoadingScenario::Startup:
                m_currentTitle = "Loading";
                break;
            case LoadingScenario::MainMenu:
                m_currentTitle = "Entering Menu";
                break;
            case LoadingScenario::SoloMatch:
            case LoadingScenario::HostMatch:
            case LoadingScenario::JoinMatch:
                m_currentTitle = "Entering The Fog";
                break;
            case LoadingScenario::EditorLevel:
                m_currentTitle = "Loading Editor";
                break;
            case LoadingScenario::SceneTransition:
                m_currentTitle = "Transferring";
                break;
            case LoadingScenario::AssetBundleLoad:
                m_currentTitle = "Downloading Assets";
                break;
        }
    }

    m_loadingScreen.SetTitleText(m_currentTitle);
    m_loadingScreen.BeginLoading();
    m_isLoading = true;
    m_isLoadingComplete = false;
    m_isCancelled = false;

    // Setup tasks based on scenario
    switch (scenario)
    {
        case LoadingScenario::SoloMatch:
            SetupSoloMatchTasks(m_context.mapName);
            break;
        case LoadingScenario::HostMatch:
            SetupHostMatchTasks(m_context.mapName, 0); // Port will be set by App
            break;
        case LoadingScenario::JoinMatch:
            SetupJoinMatchTasks(m_context.serverAddress, 0);
            break;
        case LoadingScenario::Startup:
        case LoadingScenario::MainMenu:
        case LoadingScenario::EditorLevel:
        case LoadingScenario::SceneTransition:
        case LoadingScenario::AssetBundleLoad:
            // Add basic loading tasks
            AddTask("init", "Initializing", [](LoadingState&, const LoadingContext&) {
                // Basic initialization
            }, 0.1F, 0.5F);
            break;
    }

    return true;
}

void LoadingManager::AddTask(const LoadingTask& task)
{
    m_tasks.push_back(task);
    m_totalWeight += task.progressWeight;
}

void LoadingManager::AddTask(
    const std::string& id,
    const std::string& name,
    LoadTaskCallback callback,
    float weight,
    float estimatedDuration
)
{
    LoadingTask task;
    task.id = id;
    task.name = name;
    task.loadFunc = [callback, ctx = m_context](LoadingState& state) {
        callback(state, ctx);
    };
    task.progressWeight = weight;
    task.durationEstimate = estimatedDuration;
    task.isComplete = false;
    task.currentProgress = 0.0F;

    AddTask(task);
}

void LoadingManager::UpdateAndRender(float deltaSeconds)
{
    if (!m_isLoading || m_isCancelled || m_loadingScreen.HasError())
    {
        return;
    }

    // Process next task if current one is done
    if (m_currentTaskIndex < m_tasks.size())
    {
        LoadingTask& task = m_tasks[m_currentTaskIndex];

        if (!task.isComplete)
        {
            // Update task progress
            if (task.loadFunc)
            {
                LoadingState& state = m_loadingScreen.GetState();

                // Set current task info
                m_loadingScreen.SetTask(task.name);
                m_loadingScreen.SetStageProgress(task.currentProgress);

                ExecuteTaskFrame(task, deltaSeconds);
            }
        }
        else
        {
            // Move to next task
            m_progressAccumulator += task.progressWeight;
            m_loadingScreen.SetStageProgress(1.0F);
            m_currentTaskIndex++;
        }
    }
    else
    {
        // All tasks complete
        m_loadingScreen.SetOverallProgress(1.0F);
        m_loadingScreen.SetStageProgress(1.0F);
        m_loadingScreen.SetLoadingComplete(true);
        m_isLoadingComplete = true;
        m_isLoading = false;
    }

    // Update overall progress
    float overallProgress = CalculateOverallProgress();
    m_loadingScreen.SetOverallProgress(overallProgress);

    // Update and render loading screen
    m_loadingScreen.Update(deltaSeconds, !m_isLoadingComplete);
    m_loadingScreen.Render();
}

void LoadingManager::SetError(const std::string& error)
{
    m_loadingScreen.SetError(error);
}

void LoadingManager::ClearError()
{
    // Reset error state, continue loading from where we left off
    m_loadingScreen.BeginLoading();
}

void LoadingManager::CancelLoading()
{
    m_isCancelled = true;
    m_isLoadingComplete = true;
    m_isLoading = false;
}

void LoadingManager::SetupSoloMatchTasks(const std::string& mapName)
{
    m_context.mapName = mapName;
    m_context.isMultiplayer = false;

    // 1. Compile shaders
    AddTask(
        "compile_shaders",
        "Compiling Shaders",
        [](LoadingState& state, const LoadingContext& ctx) {
            LoadGameplayAssets(state, const_cast<LoadingContext&>(ctx));
        },
        0.15F,
        1.0F
    );

    // 2. Load gameplay assets
    AddTask(
        "load_assets",
        "Loading Assets",
        [](LoadingState& state, const LoadingContext& ctx) {
            LoadGameplayAssets(state, const_cast<LoadingContext&>(ctx));
        },
        0.20F,
        2.0F
    );

    // 3. Load map tiles
    AddTask(
        "load_map_tiles",
        "Loading Map Data",
        [mapName](LoadingState& state, const LoadingContext& ctx) {
            LoadMapTiles(state, const_cast<LoadingContext&>(ctx), mapName);
        },
        0.15F,
        1.5F
    );

    // 4. Generate map
    AddTask(
        "generate_map",
        "Generating Map Tiles",
        [](LoadingState& state, const LoadingContext& ctx) {
            GenerateMap(state, const_cast<LoadingContext&>(ctx));
        },
        0.30F,
        3.0F
    );

    // 5. Spawn entities
    AddTask(
        "spawn_entities",
        "Spawning Entities",
        [](LoadingState& state, const LoadingContext& ctx) {
            SpawnEntities(state, const_cast<LoadingContext&>(ctx));
        },
        0.20F,
        2.0F
    );
}

void LoadingManager::SetupHostMatchTasks(const std::string& mapName, std::uint16_t port)
{
    m_context.mapName = mapName;
    m_context.isMultiplayer = true;
    m_context.isHost = true;

    // 1. Compile shaders
    AddTask(
        "compile_shaders",
        "Compiling Shaders",
        [](LoadingState& state, const LoadingContext& ctx) {
            CompileShaders(state, const_cast<LoadingContext&>(ctx));
        },
        0.10F,
        1.0F
    );

    // 2. Load gameplay assets
    AddTask(
        "load_assets",
        "Loading Assets",
        [](LoadingState& state, const LoadingContext& ctx) {
            LoadGameplayAssets(state, const_cast<LoadingContext&>(ctx));
        },
        0.15F,
        1.5F
    );

    // 3. Load map tiles
    AddTask(
        "load_map_tiles",
        "Loading Map Data",
        [mapName](LoadingState& state, const LoadingContext& ctx) {
            LoadMapTiles(state, const_cast<LoadingContext&>(ctx), mapName);
        },
        0.10F,
        1.0F
    );

    // 4. Start server
    AddTask(
        "start_server",
        "Starting Server",
        [](LoadingState& state, const LoadingContext& ctx) {
            // Server setup - state updates handled by network system
        },
        0.05F,
        0.5F
    );

    // 5. Generate map
    AddTask(
        "generate_map",
        "Generating Map Tiles",
        [](LoadingState& state, const LoadingContext& ctx) {
            GenerateMap(state, const_cast<LoadingContext&>(ctx));
        },
        0.25F,
        2.5F
    );

    // 6. Spawn entities
    AddTask(
        "spawn_entities",
        "Spawning Entities",
        [](LoadingState& state, const LoadingContext& ctx) {
            SpawnEntities(state, const_cast<LoadingContext&>(ctx));
        },
        0.20F,
        2.0F
    );

    // 7. Wait for players
    AddTask(
        "wait_players",
        "Waiting for Players",
        [](LoadingState& state, const LoadingContext& ctx) {
            // Waiting for players - state updated externally
        },
        0.15F,
        5.0F
    );
}

void LoadingManager::SetupJoinMatchTasks(const std::string& address, std::uint16_t port)
{
    m_context.serverAddress = address;
    m_context.isMultiplayer = true;
    m_context.isHost = false;

    // 1. Compile shaders
    AddTask(
        "compile_shaders",
        "Compiling Shaders",
        [](LoadingState& state, const LoadingContext& ctx) {
            CompileShaders(state, const_cast<LoadingContext&>(ctx));
        },
        0.10F,
        0.5F
    );

    // 2. Load gameplay assets
    AddTask(
        "load_assets",
        "Loading Assets",
        [](LoadingState& state, const LoadingContext& ctx) {
            LoadGameplayAssets(state, const_cast<LoadingContext&>(ctx));
        },
        0.15F,
        1.0F
    );

    // 3. Connect to server
    AddTask(
        "connect_server",
        "Connecting to Server",
        [](LoadingState& state, const LoadingContext& ctx) {
            // Server connection - state updated by network system
        },
        0.20F,
        3.0F
    );

    // 4. Handshake
    AddTask(
        "handshake",
        "Establishing Connection",
        [](LoadingState& state, const LoadingContext& ctx) {
            // Handshake - state updated by network system
        },
        0.10F,
        2.0F
    );

    // 5. Download map info
    AddTask(
        "download_map",
        "Receiving Map Data",
        [](LoadingState& state, const LoadingContext& ctx) {
            // Map data download - state updated by network system
        },
        0.15F,
        3.0F
    );

    // 6. Load received tiles
    AddTask(
        "load_received_tiles",
        "Loading Map Tiles",
        [](LoadingState& state, const LoadingContext& ctx) {
            LoadMapTiles(state, const_cast<LoadingContext&>(ctx), "");
        },
        0.15F,
        1.5F
    );

    // 7. Prepare entities
    AddTask(
        "prepare_entities",
        "Preparing Match",
        [](LoadingState& state, const LoadingContext& ctx) {
            SpawnEntities(state, const_cast<LoadingContext&>(ctx));
        },
        0.15F,
        1.5F
    );
}

void LoadingManager::Reset()
{
    m_tasks.clear();
    m_currentTaskIndex = 0;
    m_isLoading = false;
    m_isLoadingComplete = false;
    m_isCancelled = false;
    m_totalWeight = 0.0F;
    m_progressAccumulator = 0.0F;
    m_currentScenario = LoadingScenario::Startup;
    m_currentTitle.clear();
}

float LoadingManager::CalculateOverallProgress()
{
    if (m_totalWeight <= 0.0F)
    {
        return 0.0F;
    }

    float progress = m_progressAccumulator;

    // Add progress from current incomplete task
    if (m_currentTaskIndex < m_tasks.size())
    {
        const LoadingTask& task = m_tasks[m_currentTaskIndex];
        progress += task.progressWeight * task.currentProgress;
    }

    return progress / m_totalWeight;
}

void LoadingManager::ProcessNextTask()
{
    // This is handled in UpdateAndRender
}

void LoadingManager::ExecuteTaskFrame(LoadingTask& task, float deltaSeconds)
{
    // Simulate progress if no callback provided
    if (!task.loadFunc)
    {
        if (task.durationEstimate > 0.0F)
        {
            task.currentProgress += deltaSeconds / task.durationEstimate;
        }
        else
        {
            task.currentProgress = 1.0F; // Complete immediately
        }

        if (task.currentProgress >= 1.0F)
        {
            task.currentProgress = 1.0F;
            task.isComplete = true;
        }
        return;
    }

    // Execute the task's callback
    // In a real implementation, tasks would be multi-frame and track their own progress
    // For now, we'll assume tasks complete in one frame
    task.loadFunc(m_loadingScreen.GetState());
    task.currentProgress = 1.0F;
    task.isComplete = true;
}

// Static helper functions

void LoadingManager::LoadGameplayAssets(LoadingState& state, LoadingContext& context)
{
    // Placeholder: Load gameplay-specific assets
    state.currentTask = "Loading Assets";
    state.currentSubtask = "Player models, weapon skins, and effects";

    if (context.assetRegistry)
    {
        // context.assetRegistry->LoadAssetBundle("gameplay");
        state.loadedAssets = 42;
        state.totalAssets = 42;
    }

    state.stageProgress = 1.0F;
}

void LoadingManager::LoadMapTiles(LoadingState& state, LoadingContext& context, const std::string& mapName)
{
    // Placeholder: Load map tile data
    state.currentTask = "Loading Map Data";
    state.currentSubtask = mapName;

    if (context.assetRegistry)
    {
        // context.assetRegistry->LoadAssetBundle("maps/" + mapName);
        state.loadedAssets = 15;
        state.totalAssets = 15;
    }

    state.stageProgress = 1.0F;
}

void LoadingManager::GenerateMap(LoadingState& state, LoadingContext& context)
{
    // Placeholder: Generate map from tiles
    state.currentTask = "Generating Map";
    state.currentSubtask = "Placing tiles and connecting rooms";

    if (context.gameplay)
    {
        // context.gameplay->GenerateMap(context.mapName);
        state.currentSubtask = "Pathfinding and collision calculations";
    }

    state.stageProgress = 1.0F;
}

void LoadingManager::SpawnEntities(LoadingState& state, LoadingContext& context)
{
    // Placeholder: Spawn gameplay entities
    state.currentTask = "Spawning Entities";
    state.currentSubtask = "Survivors, generators, pallets, and interactables";

    if (context.gameplay)
    {
        // context.gameplay->SpawnEntities();
        state.currentSubtask = "Setting initial positions and states";
    }

    state.loadedAssets = 128;
    state.totalAssets = 128;
    state.stageProgress = 1.0F;
}

void LoadingManager::CompileShaders(LoadingState& state, LoadingContext& context)
{
    // Placeholder: Compile or validate shaders
    state.currentTask = "Compiling Shaders";
    state.currentSubtask = "Rendering pipeline initialization";

    if (context.renderer)
    {
        // context.renderer->RecompileShaders();
    }

    state.stageProgress = 1.0F;
}

} // namespace game::ui
