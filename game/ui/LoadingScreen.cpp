#include "LoadingScreen.hpp"

#include <cmath>
#include <algorithm>

#include "engine/ui/UiSystem.hpp"

namespace game::ui
{

LoadingScreen::LoadingScreen()
    : m_randomEngine(static_cast<unsigned>(std::chrono::steady_clock::now().time_since_epoch().count()))
{
}

LoadingScreen::~LoadingScreen()
{
    Shutdown();
}

bool LoadingScreen::Initialize(engine::ui::UiSystem* uiSystem)
{
    if (!uiSystem)
    {
        return false;
    }

    m_ui = uiSystem;
    m_state.Reset();

    // Try to load tips from config first
    if (!TryLoadLoreTipsFromConfig())
    {
        GenerateRandomLoreTips();
    }

    // Load theme if it exists
    m_ui->LoadTheme("ui/loading_theme.json");

    return true;
}

void LoadingScreen::Shutdown()
{
    m_ui = nullptr;
    m_loreTips.clear();
    m_state.Reset();
}

void LoadingScreen::BeginLoading()
{
    m_loadingComplete = false;
    m_state.Reset();
    m_animationTime = 0.0F;
    m_loreTipTimer = 0.0F;
    m_currentTipIndex = 0;
    m_previousTipIndex = -1;
    m_tipTransitionAlpha = 0.0F;

    // Pick initial random tip
    if (!m_loreTips.empty())
    {
        m_currentTipIndex = m_randomEngine() % m_loreTips.size();
    }
}

void LoadingScreen::EndLoading()
{
    m_loadingComplete = true;
}

void LoadingScreen::Update(float deltaSeconds, bool isLoading)
{
    if (!isLoading)
    {
        return;
    }

    m_animationTime += deltaSeconds;

    // Update tip timer
    if (m_showLoreTips && !m_state.hasError)
    {
        UpdateLoreTipTimer(deltaSeconds);
    }

    // Call progress callback if set
    if (m_progressCallback)
    {
        m_progressCallback(m_state);
    }
}

void LoadingScreen::Render()
{
    if (!m_ui)
    {
        return;
    }

    const int screenWidth = m_ui->ScreenWidth();
    const int screenHeight = m_ui->ScreenHeight();
    const float uiScale = m_ui->Scale();

    // Main loading panel - full screen
    engine::ui::UiRect fullScreenRect{0.0F, 0.0F, static_cast<float>(screenWidth), static_cast<float>(screenHeight)};
    m_ui->BeginPanel("loading_screen_panel", fullScreenRect, false);

    m_ui->PushLayout(engine::ui::UiSystem::LayoutAxis::Vertical, 0.0F, 0.0F);

    // Center content vertically
    float topSpacer = screenHeight * 0.3F;
    m_ui->Spacer(topSpacer);

    // Title
    {
        auto titleRect = m_ui->AllocateRect(50.0F * uiScale, -1.0F);
        engine::ui::UiRect centeredRect =
        {
            (screenWidth - titleRect.w) / 2.0F,
            titleRect.y,
            titleRect.w,
            titleRect.h
        };
        m_ui->Label(m_titleText, 2.0F * uiScale);
    }

    m_ui->Spacer(20.0F * uiScale);

    // Draw stage info (current task)
    DrawStageInfo();

    m_ui->Spacer(15.0F * uiScale);

    // Draw progress bar
    DrawProgressBar();

    // Bottom section - stats or lore
    m_ui->Spacer(40.0F * uiScale);

    if (m_state.hasError)
    {
        DrawErrorDialog();
    }
    else if (m_showLoreTips)
    {
        DrawLoreTip();
    }

    // Draw loading animation
    DrawLoadingAnimation();

    m_ui->PopLayout();
    m_ui->EndPanel();
}

void LoadingScreen::SetError(const std::string& error)
{
    m_state.hasError = true;
    m_state.errorMessage = error;
    m_state.isPaused = true;
}

void LoadingScreen::SetStage(LoadingStage stage)
{
    m_state.currentStage = stage;
    m_state.stageProgress = 0.0F;
}

void LoadingScreen::SetOverallProgress(float progress)
{
    m_state.overallProgress = std::clamp(progress, 0.0F, 1.0F);
}

void LoadingScreen::SetStageProgress(float progress)
{
    m_state.stageProgress = std::clamp(progress, 0.0F, 1.0F);
}

void LoadingScreen::SetTask(const std::string& task)
{
    m_state.currentTask = task;
}

void LoadingScreen::SetSubtask(const std::string& subtask)
{
    m_state.currentSubtask = subtask;
}

void LoadingScreen::SetAssetCounts(int loaded, int total)
{
    m_state.loadedAssets = loaded;
    m_state.totalAssets = total;
}

void LoadingScreen::LoadLoreTipsFromFile(const std::string& jsonPath)
{
    // TODO: Load from JSON file
    // For now, we add some default tips
    GenerateRandomLoreTips();
}

void LoadingScreen::AddLoreTip(const LoreTip& tip)
{
    m_loreTips.push_back(tip);
}

void LoadingScreen::CycleToNextTip()
{
    if (m_loreTips.size() <= 1)
    {
        return;
    }

    m_previousTipIndex = m_currentTipIndex;

    // Pick a different tip
    do
    {
        m_currentTipIndex = m_randomEngine() % m_loreTips.size();
    } while (m_currentTipIndex == m_previousTipIndex);

    m_tipTransitionAlpha = 0.0F;
}

std::string LoadingScreen::GetStageDisplayText(LoadingStage stage) const
{
    switch (stage)
    {
        case LoadingStage::Initializing:
            return "Initializing";
        case LoadingStage::LoadingAssets:
            return "Loading Assets";
        case LoadingStage::GeneratingMap:
            return "Generating Map";
        case LoadingStage::SpawningEntities:
            return "Spawning Entities";
        case LoadingStage::LoadingTextures:
            return "Loading Textures";
        case LoadingStage::CompilingShaders:
            return "Compiling Shaders";
        case LoadingStage::ConnectingToServer:
            return "Connecting to Server";
        case LoadingStage::Handshaking:
            return "Establishing Connection";
        case LoadingStage::Finalizing:
            return "Finalizing";
        case LoadingStage::Complete:
            return "Done";
        default:
            return "Loading...";
    }
}

void LoadingScreen::UpdateLoreTipTimer(float deltaSeconds)
{
    m_loreTipTimer += deltaSeconds;

    // Fade in effect
    if (m_tipTransitionAlpha < 1.0F)
    {
        m_tipTransitionAlpha += deltaSeconds / LORE_TIP_FADE_DURATION;
        m_tipTransitionAlpha = std::min(m_tipTransitionAlpha, 1.0F);
    }

    if (m_loreTipTimer >= LORE_TIP_DURATION)
    {
        CycleToNextTip();
        m_loreTipTimer = 0.0F;
        m_tipTransitionAlpha = 0.0F;
    }
}

void LoadingScreen::GenerateRandomLoreTips()
{
    m_loreTips.clear();

    // Dead by Daylight-style lore tips
    m_loreTips.push_back({
        "The Entity's Hunger",
        "The Entity feeds on hope. The longer survivors struggle, the stronger it becomes. Never give up."
    });

    m_loreTips.push_back({
        "Bloodwebs",
        "Each killer and survivor has their own Bloodweb. Choose your path wisely - not all perks are equal."
    });

    m_loreTips.push_back({
        "The Trapper",
        "Evan MacMillan, also known as The Trapper, was the first killer to enter the Entity's realm."
    });

    m_loreTips.push_back({
        "Flashlight Usage",
        "Flashlights can blind killers when directed at their eyes. Every killer has a specific blind duration."
    });

    m_loreTips.push_back({
        "The Hex Curse",
        "When you cleanse a dull totem, you might awaken a Hex perk. Hex perks disappear when the totem is destroyed."
    });

    m_loreTips.push_back({
        "Pallet Stuns",
        "Dropping a pallet on a killer grants immunity for a few seconds. Use this time to make distance."
    });

    m_loreTips.push_back({
        "The Entity Blocks",
        "When generators get close to completion, the Entity may spawn blocks to slow down progress."
    });

    m_loreTips.push_back({
        "Survivor Classes",
        "Each survivor has unique perks. Some are better at healing, others at escaping or repairing."
    });

    m_loreTips.push_back({
        "Red Skill Checks",
        "A Great skill check provides a small boost to generator progress and reveals your aura to teammates."
    });

    m_loreTips.push_back({
        "The Killer's Objective",
        "The killer must sacrifice survivors to hooks. Each sacrifice feeds the Entity and maintains its power."
    });
}

bool LoadingScreen::TryLoadLoreTipsFromConfig()
{
    // TODO: Implement loading from config file
    // For now, we generate default tips
    GenerateRandomLoreTips();
    return false;
}

void LoadingScreen::DrawProgressBar()
{
    if (!m_ui)
    {
        return;
    }

    const float uiScale = m_ui->Scale();
    const int screenWidth = m_ui->ScreenWidth();

    // Progress bar container
    float barWidth = 400.0F * uiScale;
    float barHeight = BAR_HEIGHT * uiScale;

    // Calculate position (centered)
    float barX = (screenWidth - barWidth) / 2.0F;

    // Background bar (dark gray)
    engine::ui::UiRect bgRect{barX, 0.0F, barWidth, barHeight};
    // Draw background
    m_ui->Label("", barWidth); // Allocate space

    // Progress fill (accent color with glow effect)
    float fillWidth = barWidth * m_state.overallProgress;

    // Glow pulse effect
    float pulse = (std::sin(m_animationTime * ANIMATION_SPEED) * 0.1F + 0.9F);

    // Progress text overlay
    std::string progressText = std::to_string(static_cast<int>(m_state.overallProgress * 100.0F)) + "%";
    m_ui->Label(progressText, 1.2F * uiScale);
}

void LoadingScreen::DrawStageInfo()
{
    if (!m_ui)
    {
        return;
    }

    const float uiScale = m_ui->Scale();

    // Current stage
    std::string stageText = GetStageDisplayText(m_state.currentStage);
    m_ui->Label(stageText, 1.5F * uiScale);

    m_ui->Spacer(8.0F * uiScale);

    // Current task
    if (!m_state.currentTask.empty())
    {
        m_ui->Label(m_state.currentTask, 1.0F * uiScale);
    }

    // Subtask (if any)
    if (!m_state.currentSubtask.empty())
    {
        m_ui->Label(m_state.currentSubtask, 0.9F * uiScale);
    }

    // Asset count (if applicable)
    if (m_state.totalAssets > 0)
    {
        std::string assetText = "Loading " + std::to_string(m_state.loadedAssets) + " / " + std::to_string(m_state.totalAssets);
        m_ui->Label(assetText, 0.85F * uiScale);
    }
}

void LoadingScreen::DrawLoreTip()
{
    if (!m_ui || m_loreTips.empty())
    {
        return;
    }

    const float uiScale = m_ui->Scale();

    m_ui->Spacer(20.0F * uiScale);

    const LoreTip& tip = m_loreTips[m_currentTipIndex];

    // Draw tip container
    float tipContainerWidth = 500.0F * uiScale;
    m_ui->Label("", tipContainerWidth); // Allocate space

    // Title
    if (!tip.title.empty())
    {
        glm::vec4 titleColor{0.21F, 0.62F, 0.92F, 1.0F}; // Accent color
        m_ui->Label(tip.title, titleColor, 1.2F * uiScale);
    }

    m_ui->Spacer(8.0F * uiScale);

    // Text (with fade effect for transition)
    glm::vec4 textColor{0.85F, 0.87F, 0.91F, m_tipTransitionAlpha};
    m_ui->Label(tip.text, textColor, 0.95F * uiScale);

    // Character attribution (if any)
    if (!tip.characterName.empty())
    {
        glm::vec4 attrColor{0.6F, 0.65F, 0.75F, m_tipTransitionAlpha};
        std::string attribution = "— " + tip.characterName;
        m_ui->Label(attribution, attrColor, 0.85F * uiScale);
    }
}

void LoadingScreen::DrawLoadingAnimation()
{
    if (!m_ui)
    {
        return;
    }

    // Simple animated dots / spinner effect
    float progress = m_animationTime * ANIMATION_SPEED;
    int dotCount = 3;
    std::string dots = "";

    for (int i = 0; i < dotCount; ++i)
    {
        float dotPos = (progress + static_cast<float>(i) * 0.7F);
        float dotSize = (std::sin(dotPos) * 0.5F + 0.5F) * 10.0F + 3.0F;
        dots += "●";
    }

    const float uiScale = m_ui->Scale();
    glm::vec4 animColor{0.3F, 0.4F, 0.5F, 0.7F};
    m_ui->Label(dots, animColor, 1.2F * uiScale);
}

void LoadingScreen::DrawErrorDialog()
{
    if (!m_ui || !m_state.hasError)
    {
        return;
    }

    const float uiScale = m_ui->Scale();

    // Error indicator
    glm::vec4 errorColor{0.84F, 0.26F, 0.25F, 1.0F};
    m_ui->Label("ERROR", errorColor, 1.5F * uiScale);

    m_ui->Spacer(10.0F * uiScale);

    // Error message
    glm::vec4 msgColor{0.92F, 0.94F, 0.98F, 1.0F};
    m_ui->Label(m_state.errorMessage, msgColor, 1.0F * uiScale);

    m_ui->Spacer(15.0F * uiScale);

    // Instructions
    glm::vec4 hintColor{0.6F, 0.65F, 0.75F, 1.0F};
    m_ui->Label("Press ESC to return to main menu", hintColor, 0.85F * uiScale);
}

} // namespace game::ui
