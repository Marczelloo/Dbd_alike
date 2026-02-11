#pragma once

#include <functional>
#include <string>
#include <vector>
#include <cstdint>
#include <random>
#include <chrono>

namespace engine::ui
{
class UiSystem;
}

namespace game::ui
{

enum class LoadingStage
{
    Initializing,
    LoadingAssets,
    GeneratingMap,
    SpawningEntities,
    LoadingTextures,
    CompilingShaders,
    ConnectingToServer,
    Handshaking,
    Finalizing,
    Complete
};

struct LoadingState
{
    LoadingStage currentStage = LoadingStage::Initializing;
    float overallProgress = 0.0F;        // 0.0 - 1.0
    float stageProgress = 0.0F;          // 0.0 - 1.0
    std::string currentTask;
    std::string currentSubtask;
    int loadedAssets = 0;
    int totalAssets = 0;
    bool isPaused = false;
    bool hasError = false;
    std::string errorMessage;

    void Reset()
    {
        currentStage = LoadingStage::Initializing;
        overallProgress = 0.0F;
        stageProgress = 0.0F;
        currentTask.clear();
        currentSubtask.clear();
        loadedAssets = 0;
        totalAssets = 0;
        isPaused = false;
        hasError = false;
        errorMessage.clear();
    }
};

struct LoreTip
{
    std::string title;
    std::string text;
    std::string characterName;  // Optional: which character is speaking
};

class LoadingScreen
{
public:
    using ProgressCallback = std::function<void(LoadingState& state)>;

    LoadingScreen();
    ~LoadingScreen();

    bool Initialize(engine::ui::UiSystem* uiSystem);
    void Shutdown();

    void BeginLoading();
    void EndLoading();

    void Update(float deltaSeconds, bool isLoading);
    void Render();

    void SetProgressCallback(ProgressCallback callback) { m_progressCallback = callback; }

    [[nodiscard]] bool IsLoadingComplete() const { return m_loadingComplete; }
    void SetLoadingComplete(bool complete) { m_loadingComplete = complete; }

    void SetError(const std::string& error);
    [[nodiscard]] bool HasError() const { return m_state.hasError; }

    // Progress control - called by game systems
    void SetStage(LoadingStage stage);
    void SetOverallProgress(float progress);
    void SetStageProgress(float progress);
    void SetTask(const std::string& task);
    void SetSubtask(const std::string& subtask);
    void SetAssetCounts(int loaded, int total);

    // Lore/tips system
    void SetLoreTipsEnabled(bool enabled) { m_showLoreTips = enabled; }
    void LoadLoreTipsFromFile(const std::string& jsonPath);
    void AddLoreTip(const LoreTip& tip);
    void CycleToNextTip();

    // Visual customization
    void SetTitleText(const std::string& title) { m_titleText = title; }
    void SetBackgroundStyle(const std::string& styleId) { m_backgroundStyle = styleId; }

    // Get current state
    [[nodiscard]] LoadingState& GetState() { return m_state; }
    [[nodiscard]] const LoadingState& GetState() const { return m_state; }

private:
    std::string GetStageDisplayText(LoadingStage stage) const;
    void UpdateLoreTipTimer(float deltaSeconds);
    void GenerateRandomLoreTips();
    bool TryLoadLoreTipsFromConfig();

    void DrawProgressBar();
    void DrawStageInfo();
    void DrawLoreTip();
    void DrawLoadingAnimation();
    void DrawErrorDialog();

    engine::ui::UiSystem* m_ui = nullptr;
    LoadingState m_state;

    // Visual state
    bool m_loadingComplete = false;
    bool m_showLoreTips = true;
    float m_animationTime = 0.0F;
    float m_loreTipTimer = 0.0F;
    int m_currentTipIndex = 0;
    int m_previousTipIndex = -1;
    float m_tipTransitionAlpha = 0.0F;

    // Customization
    std::string m_titleText;
    std::string m_backgroundStyle;

    // Lore / tips
    std::vector<LoreTip> m_loreTips;
    std::mt19937 m_randomEngine;

    // External progress callback
    ProgressCallback m_progressCallback;

    // Constants
    static constexpr float LORE_TIP_DURATION = 8.0F;      // Duration each tip is shown
    static constexpr float LORE_TIP_FADE_DURATION = 0.5F; // Fade in/out duration
    static constexpr float BAR_HEIGHT = 6.0F;
    static constexpr float ANIMATION_SPEED = 2.0F;
};

} // namespace game::ui
