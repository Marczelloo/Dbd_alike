#define NOMINMAX
#include "engine/core/App.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <thread>

#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/mat4x4.hpp>
#include <nlohmann/json.hpp>

#include "game/editor/LevelAssets.hpp"

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <netdb.h>
#include <net/if.h>
#include <sys/socket.h>
#endif

#if BUILD_WITH_IMGUI
#include <imgui.h>
#endif

namespace engine::core
{
namespace
{
constexpr std::uint8_t kPacketRoleInput = 1;
constexpr std::uint8_t kPacketSnapshot = 2;
constexpr std::uint8_t kPacketAssignRole = 3;
constexpr std::uint8_t kPacketHello = 4;
constexpr std::uint8_t kPacketReject = 5;
constexpr std::uint8_t kPacketGameplayTuning = 6;
constexpr std::uint8_t kPacketRoleChangeRequest = 7;
constexpr std::uint8_t kPacketFxSpawn = 8;

constexpr int kProtocolVersion = 1;
constexpr const char* kBuildId = "dev-2026-02-09";

using json = nlohmann::json;

constexpr std::uint16_t kButtonSprint = 1 << 0;
constexpr std::uint16_t kButtonInteractPressed = 1 << 1;
constexpr std::uint16_t kButtonInteractHeld = 1 << 2;
constexpr std::uint16_t kButtonAttackPressed = 1 << 3;
constexpr std::uint16_t kButtonJumpPressed = 1 << 4;
constexpr std::uint16_t kButtonWiggleLeftPressed = 1 << 5;
constexpr std::uint16_t kButtonWiggleRightPressed = 1 << 6;
constexpr std::uint16_t kButtonAttackHeld = 1 << 7;
constexpr std::uint16_t kButtonAttackReleased = 1 << 8;
constexpr std::uint16_t kButtonCrouchHeld = 1 << 9;
constexpr std::uint16_t kButtonLungeHeld = 1 << 10;

std::string RenderModeToText(render::RenderMode mode)
{
    return mode == render::RenderMode::Wireframe ? "wireframe" : "filled";
}

render::RenderMode RenderModeFromText(const std::string& value)
{
    return value == "filled" ? render::RenderMode::Filled : render::RenderMode::Wireframe;
}

std::string DisplayModeToText(App::DisplayModeSetting mode)
{
    switch (mode)
    {
        case App::DisplayModeSetting::Fullscreen: return "fullscreen";
        case App::DisplayModeSetting::Borderless: return "borderless";
        case App::DisplayModeSetting::Windowed:
        default: return "windowed";
    }
}

App::DisplayModeSetting DisplayModeFromText(const std::string& value)
{
    if (value == "fullscreen")
    {
        return App::DisplayModeSetting::Fullscreen;
    }
    if (value == "borderless")
    {
        return App::DisplayModeSetting::Borderless;
    }
    return App::DisplayModeSetting::Windowed;
}

game::gameplay::GameplaySystems::MapType ByteToMapType(std::uint8_t value)
{
    switch (value)
    {
        case 1: return game::gameplay::GameplaySystems::MapType::Main;
        case 2: return game::gameplay::GameplaySystems::MapType::CollisionTest;
        case 0:
        default: return game::gameplay::GameplaySystems::MapType::Test;
    }
}

std::uint8_t MapTypeToByte(game::gameplay::GameplaySystems::MapType mapType)
{
    switch (mapType)
    {
        case game::gameplay::GameplaySystems::MapType::Main: return 1;
        case game::gameplay::GameplaySystems::MapType::CollisionTest: return 2;
        case game::gameplay::GameplaySystems::MapType::Test:
        default: return 0;
    }
}

std::string MapTypeToName(game::gameplay::GameplaySystems::MapType mapType)
{
    switch (mapType)
    {
        case game::gameplay::GameplaySystems::MapType::Main: return "main";
        case game::gameplay::GameplaySystems::MapType::CollisionTest: return "collision_test";
        case game::gameplay::GameplaySystems::MapType::Test:
        default: return "test";
    }
}

std::uint8_t RoleNameToByte(const std::string& roleName)
{
    return roleName == "killer" ? 1U : 0U;
}

std::string RoleByteToName(std::uint8_t roleByte)
{
    return roleByte == 1U ? "killer" : "survivor";
}

glm::mat3 RotationMatrixFromEulerDegrees(const glm::vec3& eulerDegrees)
{
    glm::mat4 transform{1.0F};
    transform = glm::rotate(transform, glm::radians(eulerDegrees.y), glm::vec3{0.0F, 1.0F, 0.0F});
    transform = glm::rotate(transform, glm::radians(eulerDegrees.x), glm::vec3{1.0F, 0.0F, 0.0F});
    transform = glm::rotate(transform, glm::radians(eulerDegrees.z), glm::vec3{0.0F, 0.0F, 1.0F});
    return glm::mat3(transform);
}

glm::vec2 ReadMoveAxis(const platform::Input& input, const platform::ActionBindings& bindings)
{
    glm::vec2 axis{0.0F};

    if (bindings.IsDown(input, platform::InputAction::MoveLeft))
    {
        axis.x -= 1.0F;
    }
    if (bindings.IsDown(input, platform::InputAction::MoveRight))
    {
        axis.x += 1.0F;
    }
    if (bindings.IsDown(input, platform::InputAction::MoveBackward))
    {
        axis.y -= 1.0F;
    }
    if (bindings.IsDown(input, platform::InputAction::MoveForward))
    {
        axis.y += 1.0F;
    }

    if (glm::length(axis) > 1.0e-5F)
    {
        axis = glm::normalize(axis);
    }

    return axis;
}

template <typename T>
void AppendValue(std::vector<std::uint8_t>& buffer, const T& value)
{
    const std::uint8_t* ptr = reinterpret_cast<const std::uint8_t*>(&value);
    buffer.insert(buffer.end(), ptr, ptr + sizeof(T));
}

template <typename T>
bool ReadValue(const std::vector<std::uint8_t>& buffer, std::size_t& offset, T& outValue)
{
    if (offset + sizeof(T) > buffer.size())
    {
        return false;
    }

    std::memcpy(&outValue, buffer.data() + offset, sizeof(T));
    offset += sizeof(T);
    return true;
}

bool SerializeFxSpawnEvent(const engine::fx::FxSpawnEvent& event, std::vector<std::uint8_t>& outBuffer)
{
    outBuffer.clear();
    outBuffer.reserve(1 + sizeof(std::uint16_t) + event.assetId.size() + sizeof(float) * 6 + sizeof(std::uint8_t));
    AppendValue(outBuffer, kPacketFxSpawn);

    const std::uint16_t length = static_cast<std::uint16_t>(std::min<std::size_t>(event.assetId.size(), 4096));
    AppendValue(outBuffer, length);
    outBuffer.insert(outBuffer.end(), event.assetId.begin(), event.assetId.begin() + length);

    AppendValue(outBuffer, event.position.x);
    AppendValue(outBuffer, event.position.y);
    AppendValue(outBuffer, event.position.z);
    AppendValue(outBuffer, event.forward.x);
    AppendValue(outBuffer, event.forward.y);
    AppendValue(outBuffer, event.forward.z);
    AppendValue(outBuffer, static_cast<std::uint8_t>(event.netMode));
    return true;
}

bool DeserializeFxSpawnEvent(const std::vector<std::uint8_t>& buffer, engine::fx::FxSpawnEvent& outEvent)
{
    std::size_t offset = 0;
    std::uint8_t type = 0;
    if (!ReadValue(buffer, offset, type) || type != kPacketFxSpawn)
    {
        return false;
    }

    std::uint16_t length = 0;
    if (!ReadValue(buffer, offset, length))
    {
        return false;
    }
    if (offset + length > buffer.size())
    {
        return false;
    }
    outEvent.assetId.assign(reinterpret_cast<const char*>(buffer.data() + offset), length);
    offset += length;

    if (!ReadValue(buffer, offset, outEvent.position.x) ||
        !ReadValue(buffer, offset, outEvent.position.y) ||
        !ReadValue(buffer, offset, outEvent.position.z) ||
        !ReadValue(buffer, offset, outEvent.forward.x) ||
        !ReadValue(buffer, offset, outEvent.forward.y) ||
        !ReadValue(buffer, offset, outEvent.forward.z))
    {
        return false;
    }

    std::uint8_t modeByte = 0;
    if (!ReadValue(buffer, offset, modeByte))
    {
        return false;
    }
    outEvent.netMode = static_cast<engine::fx::FxNetMode>(modeByte);
    return true;
}
} // namespace

bool App::Run()
{
    OpenNetworkLogFile();
    BuildLocalIpv4List();

    (void)LoadControlsConfig();
    (void)LoadGraphicsConfig();
    (void)LoadGameplayConfig();
    (void)LoadHudLayoutConfig();

    m_windowSettings.width = m_graphicsApplied.width;
    m_windowSettings.height = m_graphicsApplied.height;
    m_windowSettings.windowScale = 1.0F;
    m_windowSettings.vsync = m_graphicsApplied.vsync;
    m_windowSettings.fullscreen = m_graphicsApplied.displayMode != DisplayModeSetting::Windowed;
    m_windowSettings.fpsLimit = m_graphicsApplied.fpsLimit;
    m_windowSettings.title = "Asymmetric Horror Prototype";

    m_vsyncEnabled = m_graphicsApplied.vsync;
    m_fpsLimit = m_graphicsApplied.fpsLimit;
    m_fixedTickHz = (m_gameplayApplied.serverTickRate <= 30) ? 30 : 60;
    m_clientInterpolationBufferMs = glm::clamp(m_gameplayApplied.interpolationBufferMs, 50, 1000);
    m_time.SetFixedDeltaSeconds(1.0 / static_cast<double>(m_fixedTickHz));

    if (!m_window.Initialize(m_windowSettings))
    {
        return false;
    }
    m_window.SetFileDropCallback([this](const std::vector<std::string>& paths) {
        m_pendingDroppedFiles.insert(m_pendingDroppedFiles.end(), paths.begin(), paths.end());
    });

    if (m_graphicsApplied.displayMode == DisplayModeSetting::Borderless)
    {
        m_window.SetDisplayMode(platform::Window::DisplayMode::Borderless, m_graphicsApplied.width, m_graphicsApplied.height);
    }

    if (!gladLoadGL(reinterpret_cast<GLADloadfunc>(glfwGetProcAddress)))
    {
        std::cerr << "Failed to initialize GLAD.\n";
        return false;
    }

    const unsigned char* glVersion = glGetString(GL_VERSION);
    std::cout << "OpenGL version: " << (glVersion != nullptr ? reinterpret_cast<const char*>(glVersion) : "unknown") << "\n";

    if (!m_renderer.Initialize(m_window.FramebufferWidth(), m_window.FramebufferHeight()))
    {
        std::cerr << "Failed to initialize renderer.\n";
        return false;
    }

    if (!m_ui.Initialize())
    {
        std::cerr << "Failed to initialize custom UI.\n";
        return false;
    }
    m_ui.SetGlobalUiScale(m_hudLayout.hudScale);

    m_renderer.SetRenderMode(m_graphicsApplied.renderMode);

    m_window.SetResizeCallback([this](int width, int height) { m_renderer.SetViewport(width, height); });

    m_gameplay.Initialize(m_eventBus);
    m_gameplay.SetFxReplicationCallback([this](const engine::fx::FxSpawnEvent& event) {
        if (m_multiplayerMode != MultiplayerMode::Host || !m_network.IsConnected())
        {
            return;
        }

        std::vector<std::uint8_t> payload;
        if (!SerializeFxSpawnEvent(event, payload))
        {
            return;
        }
        m_network.SendReliable(payload.data(), payload.size());
    });
    m_gameplay.ApplyGameplayTuning(m_gameplayApplied);
    ApplyControlsSettings();
    m_gameplay.SetRenderModeLabel(RenderModeToText(m_renderer.GetRenderMode()));
    m_levelEditor.Initialize();

    // Initialize loading manager
    game::ui::LoadingContext loadingContext;
    loadingContext.ui = &m_ui;
    loadingContext.input = &m_input;
    loadingContext.renderer = &m_renderer;
    loadingContext.gameplay = &m_gameplay;
    if (!m_loadingManager.Initialize(loadingContext))
    {
        std::cerr << "Failed to initialize loading manager.\n";
    }

    if (!m_skillCheckWheel.Initialize(&m_ui, &m_renderer))
    {
        std::cerr << "Failed to initialize skill check wheel.\n";
    }

    if (!m_generatorProgressBar.Initialize(&m_ui))
    {
        std::cerr << "Failed to initialize generator progress bar.\n";
    }

    if (!m_screenEffects.Initialize(&m_ui))
    {
        std::cerr << "Failed to initialize screen effects.\n";
    }

    if (!m_perkLoadoutEditor.Initialize(&m_ui, &m_gameplay.GetPerkSystem()))
    {
        std::cerr << "Failed to initialize perk loadout editor.\n";
    }

    if (!m_lobbyScene.Initialize(&m_ui, &m_renderer, &m_input))
    {
        std::cerr << "Failed to initialize lobby scene.\n";
    }
    m_lobbyScene.SetStartMatchCallback([this](const std::string& map, const std::string& role, const std::array<std::string, 4>& perks) {
        m_sessionMapName = map;
        m_sessionRoleName = role;
        std::array<std::string, 4> perkArray = perks;
        if (role == "survivor")
        {
            for (std::size_t i = 0; i < 4; ++i)
            {
                m_menuSurvivorPerks[i] = perkArray[i];
            }
        }
        else
        {
            for (std::size_t i = 0; i < 4; ++i)
            {
                m_menuKillerPerks[i] = perkArray[i];
            }
        }
        m_lobbyScene.ExitLobby();
        StartSoloSession(map, role);
    });
    m_lobbyScene.SetReadyChangedCallback([this](bool ready) {
        (void)ready;
    });
    m_lobbyScene.SetRoleChangedCallback([this](const std::string& role) {
        m_sessionRoleName = role;
    });

    if (!m_console.Initialize(m_window))
    {
        CloseNetworkLogFile();
        return false;
    }
        if (!m_devToolbar.Initialize(m_window))
        {
            m_console.Shutdown();
            CloseNetworkLogFile();
            return false;
        }
    float currentFps = 0.0F;
    double fpsAccumulator = 0.0;
    int fpsFrames = 0;

    while (!m_window.ShouldClose() && !m_gameplay.QuitRequested())
    {
        const double frameStart = glfwGetTime();

        m_window.PollEvents();
        m_input.Update(m_window.NativeHandle());
        if (!m_pendingDroppedFiles.empty())
        {
            m_levelEditor.QueueExternalDroppedFiles(m_pendingDroppedFiles);
            m_pendingDroppedFiles.clear();
        }

        if (!m_settingsMenuOpen && m_actionBindings.IsPressed(m_input, platform::InputAction::ToggleConsole))
        {
            m_console.Toggle();
        }

        const bool inGame = m_appMode == AppMode::InGame;
        const bool inEditor = m_appMode == AppMode::Editor;
        const bool inLobby = m_appMode == AppMode::Lobby;
        if ((inGame || inEditor || inLobby) && !m_console.IsOpen() && m_input.IsKeyPressed(GLFW_KEY_ESCAPE))
        {
            if (inGame)
            {
                m_pauseMenuOpen = !m_pauseMenuOpen;
            }
            else if (inLobby)
            {
                m_lobbyScene.ExitLobby();
                ResetToMainMenu();
            }
            else
            {
                ResetToMainMenu();
            }
        }

        const bool altHeld = m_input.IsKeyDown(GLFW_KEY_LEFT_ALT) || m_input.IsKeyDown(GLFW_KEY_RIGHT_ALT);
        const bool controlsEnabled = (inGame || inEditor) && !m_pauseMenuOpen && !m_console.IsOpen() && !m_settingsMenuOpen && !altHeld;
        m_window.SetCursorCaptured(inGame && controlsEnabled);

        if (m_input.IsKeyPressed(GLFW_KEY_F11))
        {
            m_window.ToggleFullscreen();
        }

        if (inGame && !m_settingsMenuOpen && m_actionBindings.IsPressed(m_input, platform::InputAction::ToggleDebugHud))
        {
            m_showDebugOverlay = !m_showDebugOverlay;
        }

        if (inGame && m_input.IsKeyPressed(GLFW_KEY_F2))
        {
            m_gameplay.ToggleDebugDraw(!m_gameplay.DebugDrawEnabled());
        }

        if (inGame && m_input.IsKeyPressed(GLFW_KEY_F3))
        {
            m_renderer.ToggleRenderMode();
            m_gameplay.SetRenderModeLabel(RenderModeToText(m_renderer.GetRenderMode()));
        }
        if (inGame && m_input.IsKeyPressed(GLFW_KEY_F5))
        {
            m_gameplay.ToggleTerrorRadiusVisualization(!m_gameplay.TerrorRadiusVisualizationEnabled());
        }
        if (m_input.IsKeyPressed(GLFW_KEY_F4))
        {
            m_showNetworkOverlay = !m_showNetworkOverlay;
        }
        if (m_input.IsKeyPressed(GLFW_KEY_F6))
        {
            m_showUiTestPanel = !m_showUiTestPanel;
            m_statusToastMessage = m_showUiTestPanel ? "UI test panel ON" : "UI test panel OFF";
            m_statusToastUntilSeconds = glfwGetTime() + 2.0;
        }
        if (m_input.IsKeyPressed(GLFW_KEY_F7))
        {
            m_showLoadingScreenTestPanel = !m_showLoadingScreenTestPanel;
            m_statusToastMessage = m_showLoadingScreenTestPanel ? "Loading screen test panel ON" : "Loading screen test panel OFF";
            m_statusToastUntilSeconds = glfwGetTime() + 2.0;
        }

        if (inGame && m_multiplayerMode == MultiplayerMode::Solo && controlsEnabled && m_input.IsKeyPressed(GLFW_KEY_TAB))
        {
            m_gameplay.ToggleControlledRole();
        }

        if (inGame && m_multiplayerMode != MultiplayerMode::Client)
        {
            m_gameplay.CaptureInputFrame(m_input, m_actionBindings, controlsEnabled);
        }

        if (inGame && m_multiplayerMode == MultiplayerMode::Client)
        {
            SendClientInput(m_input, controlsEnabled);
        }

        PollNetwork();
        if ((m_networkState == NetworkState::ClientConnecting || m_networkState == NetworkState::ClientHandshaking) &&
            !m_network.IsConnected())
        {
            const double elapsed = glfwGetTime() - m_joinStartSeconds;
            if (elapsed > 8.0)
            {
                const std::string timeoutReason = "Connection timeout to " + m_joinTargetIp + ":" + std::to_string(m_joinTargetPort);
                m_lastNetworkError = timeoutReason;
                TransitionNetworkState(NetworkState::Error, timeoutReason, true);
                m_network.Disconnect();
                ResetToMainMenu();
                m_menuNetStatus = timeoutReason;
            }
        }
        TickLanDiscovery(glfwGetTime());

        m_time.BeginFrame(glfwGetTime());

        while (m_time.ShouldRunFixedStep())
        {
            if (inGame)
            {
                if (m_multiplayerMode != MultiplayerMode::Client)
                {
                    m_gameplay.FixedUpdate(static_cast<float>(m_time.FixedDeltaSeconds()), m_input, controlsEnabled);
                    m_eventBus.DispatchQueued();

                    if (m_multiplayerMode == MultiplayerMode::Host)
                    {
                        SendHostSnapshot();
                    }
                }
            }

            m_time.ConsumeFixedStep();
        }

        if (inGame)
        {
            const bool canLookLocally = controlsEnabled && m_multiplayerMode != MultiplayerMode::Client;
            m_gameplay.Update(static_cast<float>(m_time.DeltaSeconds()), m_input, canLookLocally);
        }
        else if (inEditor)
        {
            m_levelEditor.Update(
                static_cast<float>(m_time.DeltaSeconds()),
                m_input,
                controlsEnabled,
                m_window.FramebufferWidth(),
                m_window.FramebufferHeight()
            );
        }

        m_renderer.BeginFrame(glm::vec3{0.06F, 0.07F, 0.08F});
        glm::mat4 viewProjection{1.0F};
        if (inGame)
        {
            m_renderer.SetLightingEnabled(true);
            const float aspect = m_window.FramebufferHeight() > 0
                                     ? static_cast<float>(m_window.FramebufferWidth()) / static_cast<float>(m_window.FramebufferHeight())
                                     : (16.0F / 9.0F);
            m_gameplay.Render(m_renderer, aspect);
            viewProjection = m_gameplay.BuildViewProjection(aspect);
            m_renderer.SetCameraWorldPosition(m_gameplay.CameraPosition());
        }
        else if (inEditor)
        {
            m_renderer.SetLightingEnabled(m_levelEditor.EditorLightingEnabled());
            m_renderer.SetEnvironmentSettings(m_levelEditor.CurrentEnvironmentSettings());
            m_levelEditor.Render(m_renderer);
            const float aspect = m_window.FramebufferHeight() > 0
                                     ? static_cast<float>(m_window.FramebufferWidth()) / static_cast<float>(m_window.FramebufferHeight())
                                     : (16.0F / 9.0F);
            viewProjection = m_levelEditor.BuildViewProjection(aspect);
            m_renderer.SetCameraWorldPosition(m_levelEditor.CameraPosition());
        }
        else if (inLobby)
        {
            m_renderer.SetLightingEnabled(true);
            const float aspect = m_window.FramebufferHeight() > 0
                                     ? static_cast<float>(m_window.FramebufferWidth()) / static_cast<float>(m_window.FramebufferHeight())
                                     : (16.0F / 9.0F);
            viewProjection = m_lobbyScene.BuildViewProjection(aspect);
            m_renderer.SetCameraWorldPosition(m_lobbyScene.CameraPosition());
            m_lobbyScene.Render3D();
        }
        else
        {
            m_renderer.SetLightingEnabled(true);
            m_renderer.SetCameraWorldPosition(glm::vec3{0.0F, 2.0F, 0.0F});
        }
        m_renderer.EndFrame(viewProjection);

        bool shouldQuit = false;
        bool closePauseMenu = false;
        bool backToMenu = false;

        m_console.BeginFrame();

        if (m_appMode == AppMode::Loading && m_input.IsKeyPressed(GLFW_KEY_ESCAPE))
        {
            m_loadingTestShowFull = false;
            m_appMode = AppMode::MainMenu;
        }

        m_ui.BeginFrame(engine::ui::UiSystem::BeginFrameArgs{
            &m_input,
            m_window.FramebufferWidth(),
            m_window.FramebufferHeight(),
            m_window.WindowWidth(),
            m_window.WindowHeight(),
            static_cast<float>(m_time.DeltaSeconds()),
            true,
        });

        if (m_appMode == AppMode::Loading)
        {
            UpdateLoading(static_cast<float>(m_time.DeltaSeconds()));
            if (m_loadingManager.IsLoadingComplete())
            {
                FinishLoading();
            }
        }
        else if (m_appMode == AppMode::MainMenu && !m_settingsMenuOpen)
        {
            DrawMainMenuUiCustom(&shouldQuit);
        }
        else if (m_appMode == AppMode::Loading)
        {
            if (m_loadingTestShowFull)
            {
                DrawFullLoadingScreen(m_loadingTestProgress, m_loadingTestTips[static_cast<std::size_t>(m_loadingTestSelectedTip) % m_loadingTestTips.size()], "Loading...");
            }
        }
        else if (m_appMode == AppMode::Editor)
        {
            bool editorBackToMenu = false;
            bool editorPlaytestMap = false;
            std::string editorPlaytestMapName;
            m_levelEditor.SetCurrentRenderMode(m_renderer.GetRenderMode());
            m_levelEditor.DrawUi(&editorBackToMenu, &editorPlaytestMap, &editorPlaytestMapName);
            if (const std::optional<render::RenderMode> requestedMode = m_levelEditor.ConsumeRequestedRenderMode();
                requestedMode.has_value())
            {
                m_renderer.SetRenderMode(*requestedMode);
            }
            if (editorBackToMenu)
            {
                ResetToMainMenu();
            }
            if (editorPlaytestMap && !editorPlaytestMapName.empty())
            {
                StartSoloSession(editorPlaytestMapName, "survivor");
            }
        }
        else if (m_pauseMenuOpen && !m_settingsMenuOpen)
        {
            DrawPauseMenuUiCustom(&closePauseMenu, &backToMenu, &shouldQuit);
        }

        if (m_settingsMenuOpen)
        {
            DrawSettingsUiCustom(&m_settingsMenuOpen);
        }

        if (m_graphicsAutoConfirmPending && glfwGetTime() >= m_graphicsAutoConfirmDeadline)
        {
            ApplyGraphicsSettings(m_graphicsRollback, false);
            m_graphicsEditing = m_graphicsRollback;
            m_graphicsApplied = m_graphicsRollback;
            m_graphicsAutoConfirmPending = false;
            m_graphicsStatus = "Graphics auto-reverted after timeout.";
            (void)SaveGraphicsConfig();
        }

        if (closePauseMenu)
        {
            m_pauseMenuOpen = false;
        }
        if (backToMenu)
        {
            ResetToMainMenu();
        }
        if (shouldQuit)
        {
            m_window.SetShouldClose(true);
        }

        if (m_appMode == AppMode::InGame)
        {
            const game::gameplay::HudState hudState = m_gameplay.BuildHudState();
            DrawInGameHudCustom(hudState, currentFps, glfwGetTime());
            
            m_screenEffects.Update(static_cast<float>(m_time.DeltaSeconds()));
            game::ui::ScreenEffectsState screenState;
            screenState.terrorRadiusActive = hudState.terrorRadiusVisible;
            screenState.terrorRadiusIntensity = hudState.chaseActive ? 0.8F : 0.4F;
            screenState.chaseActive = hudState.chaseActive;
            screenState.lowHealthActive = (hudState.survivorStateName == "Injured" || hudState.survivorStateName == "Downed");
            screenState.lowHealthIntensity = hudState.survivorStateName == "Downed" ? 0.6F : 0.3F;
            m_screenEffects.Render(screenState);
            
            if (hudState.skillCheckActive)
            {
                if (!m_skillCheckWheel.IsActive())
                {
                    m_skillCheckWheel.TriggerSkillCheck(
                        hudState.skillCheckSuccessStart,
                        hudState.skillCheckSuccessEnd,
                        0.15F
                    );
                }
                // Sync needle position from game state
                m_skillCheckWheel.GetState().needleAngle = hudState.skillCheckNeedle * 360.0F;
            }
            else
            {
                if (m_skillCheckWheel.IsActive())
                {
                    // Skill check ended in game - show feedback
                    m_skillCheckWheel.GetState().active = false;
                }
            }
            m_skillCheckWheel.Update(static_cast<float>(m_time.DeltaSeconds()));
            m_skillCheckWheel.Render();
            
            game::ui::GeneratorProgressState genState;
            genState.isActive = hudState.repairingGenerator || hudState.generatorsCompleted > 0;
            genState.isRepairing = hudState.repairingGenerator;
            genState.progress = hudState.activeGeneratorProgress;
            genState.generatorsCompleted = hudState.generatorsCompleted;
            genState.generatorsTotal = hudState.generatorsTotal;
            m_generatorProgressBar.Render(genState);
        }
        else if (m_appMode == AppMode::Lobby)
        {
            m_lobbyScene.Update(static_cast<float>(m_time.DeltaSeconds()));
            m_lobbyScene.RenderUI();
            m_lobbyScene.HandleInput();
        }

        if (m_showUiTestPanel)
        {
            DrawUiTestPanel();
        }
        if (m_showLoadingScreenTestPanel && (m_appMode != AppMode::Loading || !m_loadingTestShowFull))
        {
            DrawLoadingScreenTestPanel();
        }

        // Draw connecting loading screen overlay
        if (m_connectingLoadingActive)
        {
            const double elapsed = std::max(0.0, glfwGetTime() - m_connectingLoadingStart);
            
            // Solo mode dismisses faster (2s), multiplayer has 15s timeout
            const bool isSoloMode = m_joinTargetIp.empty();
            const double timeout = isSoloMode ? 2.0 : 15.0;
            
            if (elapsed > timeout)
            {
                std::cout << "[Loading] Timeout after " << timeout << "s, dismissing loading screen\n";
                m_connectingLoadingActive = false;
            }
            else
            {
                // Fake progress: asymptotically approach 0.95 over ~8 seconds
                const float fakeProgress = std::min(0.95F, static_cast<float>(1.0 - std::exp(-elapsed * 0.35)));
                
                std::string step;
                std::string tip;
                if (isSoloMode)
                {
                    // Solo mode
                    step = "Loading solo session (" + std::to_string(static_cast<int>(elapsed)) + "s)";
                    tip = "Preparing game world...";
                }
                else
                {
                    // Multiplayer join
                    step = "Connecting to " + m_joinTargetIp + ":" + std::to_string(m_joinTargetPort) + " (" + std::to_string(static_cast<int>(elapsed)) + "s)";
                    tip = "Establishing connection to the server...";
                }
                DrawFullLoadingScreen(fakeProgress, tip, step);
            }
        }

        // Render ImGui debug windows BEFORE EndFrame
        if (m_showNetworkOverlay && (inGame || m_appMode == AppMode::MainMenu))
        {
            DrawNetworkOverlayUi(glfwGetTime());
        }
        if (inGame && m_showPlayersWindow)
        {
            DrawPlayersDebugUi(glfwGetTime());
        }

        m_ui.EndFrame();

        // Build HUD state before rendering toolbar (needed for game stats display)
        game::gameplay::HudState hudState = m_gameplay.BuildHudState();
        hudState.isInGame = (m_appMode == AppMode::InGame);

        // Render developer toolbar LAST to be on top of everything
        if (m_appMode == AppMode::InGame)
        {
            ::ui::ToolbarContext toolbarContext;
            toolbarContext.showNetworkOverlay = &m_showNetworkOverlay;
            toolbarContext.showPlayersWindow = &m_showPlayersWindow;
            toolbarContext.showDebugOverlay = &m_showDebugOverlay;
            toolbarContext.showMovementWindow = &m_showMovementWindow;
            toolbarContext.showStatsWindow = &m_showStatsWindow;
            toolbarContext.showControlsWindow = &m_showControlsWindow;
            toolbarContext.showUiTestPanel = &m_showUiTestPanel;
            toolbarContext.showLoadingScreenTestPanel = &m_showLoadingScreenTestPanel;
            toolbarContext.fps = currentFps;
            toolbarContext.tickRate = m_fixedTickHz;
            toolbarContext.renderMode = RenderModeToText(m_renderer.GetRenderMode());

            m_devToolbar.Render(toolbarContext);
        }

        ::ui::ConsoleContext context;
        context.gameplay = &m_gameplay;
        context.window = &m_window;
        context.vsync = &m_vsyncEnabled;
        context.fpsLimit = &m_fpsLimit;
        context.renderPlayerHud = false;

        bool showOverlayThisFrame = m_showDebugOverlay && m_appMode == AppMode::InGame;
        context.showDebugOverlay = &showOverlayThisFrame;
        context.showMovementWindow = &m_showMovementWindow;
        context.showStatsWindow = &m_showStatsWindow;

        context.applyVsync = [this](bool enabled) {
            m_vsyncEnabled = enabled;
            m_window.SetVSync(enabled);
            m_graphicsApplied.vsync = enabled;
            m_graphicsEditing.vsync = enabled;
        };
        context.applyFpsLimit = [this](int limit) {
            m_fpsLimit = limit;
            m_graphicsApplied.fpsLimit = limit;
            m_graphicsEditing.fpsLimit = limit;
        };
        context.applyResolution = [this](int width, int height) {
            m_window.SetResolution(width, height);
            m_graphicsApplied.width = width;
            m_graphicsApplied.height = height;
            m_graphicsEditing.width = width;
            m_graphicsEditing.height = height;
        };
        context.toggleFullscreen = [this]() {
            m_window.ToggleFullscreen();
            const bool fullscreen = m_window.IsFullscreen();
            m_graphicsApplied.displayMode = fullscreen ? DisplayModeSetting::Fullscreen : DisplayModeSetting::Windowed;
            m_graphicsEditing.displayMode = m_graphicsApplied.displayMode;
        };

        context.applyRenderMode = [this](const std::string& modeName) {
            if (modeName == "wireframe")
            {
                m_renderer.SetRenderMode(render::RenderMode::Wireframe);
            }
            else if (modeName == "filled")
            {
                m_renderer.SetRenderMode(render::RenderMode::Filled);
            }
            m_graphicsApplied.renderMode = m_renderer.GetRenderMode();
            m_graphicsEditing.renderMode = m_renderer.GetRenderMode();
            m_gameplay.SetRenderModeLabel(RenderModeToText(m_renderer.GetRenderMode()));
        };

        context.setCameraMode = [this](const std::string& modeName) {
            m_gameplay.SetCameraModeOverride(modeName);
        };

        context.setControlledRole = [this](const std::string& roleName) {
            RequestRoleChange(roleName, false);
        };

        context.requestRoleChange = [this](const std::string& roleName) {
            RequestRoleChange(roleName, false);
        };

        context.playerDump = [this]() {
            return PlayerDump();
        };

        context.spawnRoleHere = [this](const std::string& roleName) {
            const bool ok = m_gameplay.SpawnRoleHere(roleName);
            AppendNetworkLog(std::string("Console spawn_here role=") + NormalizeRoleName(roleName) + " result=" + (ok ? "ok" : "fail"));
        };

        context.spawnRoleAt = [this](const std::string& roleName, int spawnId) {
            const bool ok = m_gameplay.SpawnRoleAt(roleName, spawnId);
            std::ostringstream oss;
            oss << "Console spawn_at role=" << NormalizeRoleName(roleName) << " spawnId=" << spawnId << " result=" << (ok ? "ok" : "fail");
            AppendNetworkLog(oss.str());
        };

        context.listSpawns = [this]() {
            return m_gameplay.ListSpawnPoints();
        };

        context.setPhysicsDebug = [this](bool enabled) {
            m_gameplay.TogglePhysicsDebug(enabled);
        };

        context.setNoClip = [this](bool enabled) {
            m_gameplay.SetNoClip(enabled);
        };

        context.setTickRate = [this](int hz) {
            m_fixedTickHz = (hz <= 30) ? 30 : 60;
            m_gameplayApplied.serverTickRate = m_fixedTickHz;
            m_gameplayEditing.serverTickRate = m_fixedTickHz;
            m_time.SetFixedDeltaSeconds(1.0 / static_cast<double>(m_fixedTickHz));
        };

        context.hostSession = [this](int port) {
            StartHostSession(m_sessionMapName, m_sessionRoleName, static_cast<std::uint16_t>(std::max(1, port)));
        };

        context.joinSession = [this](const std::string& ip, int port) {
            StartJoinSession(ip, static_cast<std::uint16_t>(std::max(1, port)), m_preferredJoinRole);
        };

        context.disconnectSession = [this]() {
            if (m_multiplayerMode != MultiplayerMode::Solo)
            {
                ResetToMainMenu();
            }
        };
        context.netStatus = [this]() { return NetStatusDump(); };
        context.netDump = [this]() { return NetConfigDump(); };
        context.lanScan = [this]() { m_lanDiscovery.ForceScan(); };
        context.lanStatus = [this]() {
            std::ostringstream oss;
            oss << "LAN discovery: "
                << (m_lanDiscovery.GetMode() == net::LanDiscovery::Mode::Disabled
                        ? "OFF"
                        : (m_lanDiscovery.GetMode() == net::LanDiscovery::Mode::Host ? "HOST" : "CLIENT"))
                << " port=" << m_lanDiscovery.DiscoveryPort()
                << " servers=" << m_lanDiscovery.Servers().size()
                << " last_rx=" << m_lanDiscovery.LastResponseReceivedSeconds()
                << " last_tx=" << m_lanDiscovery.LastHostBroadcastSeconds();
            return oss.str();
        };
        context.lanDebug = [this](bool enabled) {
            m_showLanDebug = enabled;
            m_lanDiscovery.SetDebugEnabled(enabled);
        };
        context.setTerrorRadiusVisible = [this](bool enabled) {
            m_gameplay.ToggleTerrorRadiusVisualization(enabled);
        };
        context.setTerrorRadiusMeters = [this](float meters) {
            m_gameplay.SetTerrorRadius(meters);
        };

        m_console.Render(context, currentFps, hudState);

        m_window.SwapBuffers();

        const double frameEnd = glfwGetTime();
        const double frameDelta = frameEnd - frameStart;
        fpsAccumulator += frameDelta;
        ++fpsFrames;
        if (fpsAccumulator >= 0.25)
        {
            currentFps = static_cast<float>(static_cast<double>(fpsFrames) / fpsAccumulator);
            fpsAccumulator = 0.0;
            fpsFrames = 0;
        }

        if (!m_vsyncEnabled && m_fpsLimit > 0)
        {
            const double targetSeconds = 1.0 / static_cast<double>(m_fpsLimit);
            const double elapsed = glfwGetTime() - frameStart;
            if (elapsed < targetSeconds)
            {
                std::this_thread::sleep_for(std::chrono::duration<double>(targetSeconds - elapsed));
            }
        }
    }

    TransitionNetworkState(NetworkState::Disconnecting, "Application shutdown");
    m_lanDiscovery.Stop();
    m_network.Shutdown();
    m_lobbyScene.Shutdown();
    m_perkLoadoutEditor.Shutdown();
    m_screenEffects.Shutdown();
    m_generatorProgressBar.Shutdown();
    m_skillCheckWheel.Shutdown();
    m_console.Shutdown();
    m_devToolbar.Shutdown();
    m_ui.Shutdown();
    m_renderer.Shutdown();
    CloseNetworkLogFile();
    return true;
}

void App::ResetToMainMenu()
{
    TransitionNetworkState(NetworkState::Disconnecting, "Reset to main menu");
    m_lanDiscovery.Stop();
    m_network.Disconnect();
    m_gameplay.SetNetworkAuthorityMode(false);
    m_gameplay.ClearRemoteRoleCommands();

    m_multiplayerMode = MultiplayerMode::Solo;
    m_appMode = AppMode::MainMenu;
    m_pauseMenuOpen = false;
    m_settingsMenuOpen = false;
    m_settingsOpenedFromPause = false;
    m_menuNetStatus.clear();
    m_serverGameplayValues = false;
    ApplyGameplaySettings(m_gameplayApplied, false);

    m_sessionRoleName = "survivor";
    m_remoteRoleName = "killer";
    m_sessionMapName = "main";
    m_sessionMapType = game::gameplay::GameplaySystems::MapType::Main;
    m_sessionSeed = std::random_device{}();
    m_connectedEndpoint.clear();
    InitializePlayerBindings();

    m_gameplay.RegenerateLoops(m_sessionSeed);
    m_gameplay.SetControlledRole("survivor");
    m_renderer.SetEnvironmentSettings(render::EnvironmentSettings{});

    if (m_lanDiscovery.StartClient(m_lanDiscoveryPort, kProtocolVersion, kBuildId))
    {
        TransitionNetworkState(NetworkState::Offline, "Main menu (LAN scan active)");
    }
    else
    {
        TransitionNetworkState(NetworkState::Offline, "Main menu");
    }
}

void App::StartSoloSession(const std::string& mapName, const std::string& roleName)
{
    m_lanDiscovery.Stop();
    m_network.Disconnect();

    TransitionNetworkState(NetworkState::Offline, "Solo session");
    m_multiplayerMode = MultiplayerMode::Solo;
    m_sessionMapName = mapName;
    m_sessionRoleName = NormalizeRoleName(roleName);
    m_remoteRoleName = OppositeRoleName(m_sessionRoleName);

    std::string normalizedMap = mapName;
    if (normalizedMap == "main_map")
    {
        normalizedMap = "main";
    }

    // Start loading screen
    StartLoading(game::ui::LoadingScenario::SoloMatch);

    m_serverGameplayValues = false;
    m_pauseMenuOpen = false;
    m_settingsMenuOpen = false;
    m_settingsOpenedFromPause = false;

    m_menuNetStatus = "Solo session started.";

    if (normalizedMap == "main")
    {
        m_sessionMapType = game::gameplay::GameplaySystems::MapType::Main;
        m_sessionSeed = std::random_device{}();
    }
    else if (normalizedMap == "collision_test")
    {
        m_sessionMapType = game::gameplay::GameplaySystems::MapType::CollisionTest;
    }
    else
    {
        m_sessionMapType = game::gameplay::GameplaySystems::MapType::Test;
    }

    m_gameplay.LoadMap(normalizedMap);
    if (normalizedMap == "main")
    {
        m_gameplay.RegenerateLoops(m_sessionSeed);
    }

    ApplyMapEnvironment(normalizedMap);
    InitializePlayerBindings();
    ApplyRoleMapping(m_sessionRoleName, m_remoteRoleName, "Solo role selection", true, true);
}

bool App::StartHostSession(const std::string& mapName, const std::string& roleName, std::uint16_t port)
{
    TransitionNetworkState(NetworkState::HostStarting, "Starting host");
    m_lanDiscovery.Stop();
    m_network.Disconnect();
    if (!m_network.StartHost(port, 1))
    {
        m_menuNetStatus = "Failed to host multiplayer session.";
        TransitionNetworkState(NetworkState::Error, m_menuNetStatus, true);
        return false;
    }

    m_multiplayerMode = MultiplayerMode::Host;
    m_appMode = AppMode::InGame;
    m_pauseMenuOpen = false;
    m_settingsMenuOpen = false;
    m_settingsOpenedFromPause = false;
    m_serverGameplayValues = false;

    m_sessionRoleName = NormalizeRoleName(roleName);
    m_remoteRoleName = OppositeRoleName(m_sessionRoleName);
    m_sessionMapName = mapName;

    std::string normalizedMap = mapName;
    if (normalizedMap == "main_map")
    {
        normalizedMap = "main";
    }

    m_gameplay.SetNetworkAuthorityMode(true);
    ApplyGameplaySettings(m_gameplayApplied, false);
    m_gameplay.LoadMap(normalizedMap);
    if (normalizedMap == "main")
    {
        // Generate new random seed for each host session
        m_sessionSeed = std::random_device{}();
        m_gameplay.RegenerateLoops(m_sessionSeed);
        m_sessionMapType = game::gameplay::GameplaySystems::MapType::Main;
    }
    else if (normalizedMap == "collision_test")
    {
        m_sessionMapType = game::gameplay::GameplaySystems::MapType::CollisionTest;
    }
    else
    {
        m_sessionMapType = game::gameplay::GameplaySystems::MapType::Test;
    }

    // Start loading screen before setting up the actual game
    StartLoading(game::ui::LoadingScenario::HostMatch);

    ApplyMapEnvironment(normalizedMap);
    InitializePlayerBindings();
    ApplyRoleMapping(m_sessionRoleName, m_remoteRoleName, "Host role selection", true, true);
}

bool App::StartJoinSession(const std::string& ip, std::uint16_t port, const std::string& preferredRole)
{
    m_lanDiscovery.Stop();
    m_network.Disconnect();
    m_lastNetworkError.clear();
    TransitionNetworkState(NetworkState::ClientConnecting, "Connecting to " + ip + ":" + std::to_string(port));
    if (!m_network.StartClient(ip, port))
    {
        m_menuNetStatus = "Failed to join host.";
        TransitionNetworkState(NetworkState::Error, m_menuNetStatus, true);
        return false;
    }

    m_multiplayerMode = MultiplayerMode::Client;
    m_appMode = AppMode::InGame;
    m_pauseMenuOpen = false;
    m_settingsMenuOpen = false;
    m_settingsOpenedFromPause = false;
    m_serverGameplayValues = false;

    m_preferredJoinRole = NormalizeRoleName(preferredRole);
    m_sessionRoleName = m_preferredJoinRole;
    m_remoteRoleName = OppositeRoleName(m_sessionRoleName);

    m_gameplay.SetNetworkAuthorityMode(false);
    ApplyGameplaySettings(m_gameplayApplied, false);
    m_gameplay.SetControlledRole(m_preferredJoinRole);
    InitializePlayerBindings();

    m_joinTargetIp = ip;
    m_joinTargetPort = port;
    m_joinStartSeconds = glfwGetTime();
    m_connectedEndpoint.clear();
    m_menuNetStatus = "Joining " + ip + ":" + std::to_string(port) + " ...";
    m_connectingLoadingActive = m_showConnectingLoading;
    m_connectingLoadingStart = glfwGetTime();
    return true;
}

void App::PollNetwork()
{
    m_network.Poll(0);

    while (true)
    {
        std::optional<net::NetworkSession::PollEvent> event = m_network.PopEvent();
        if (!event.has_value())
        {
            break;
        }

        if (event->connected)
        {
            if (m_multiplayerMode == MultiplayerMode::Host)
            {
                m_menuNetStatus = "Client connected. Waiting for handshake...";
                TransitionNetworkState(NetworkState::HostListening, "Client connected, waiting for HELLO");
                m_remotePlayer.connected = true;
                m_remotePlayer.lastSnapshotSeconds = glfwGetTime();
                AppendNetworkLog("Peer connected: remote player slot reserved.");
            }
            else if (m_multiplayerMode == MultiplayerMode::Client)
            {
                m_menuNetStatus = "Connected. Waiting for role/map assignment...";
                TransitionNetworkState(NetworkState::ClientHandshaking, "Connected, sending HELLO");
                m_remotePlayer.connected = true;
                AppendNetworkLog("Client transport connected. Sending HELLO packet.");

                std::vector<std::uint8_t> hello;
                if (SerializeHello(m_preferredJoinRole, hello))
                {
                    m_network.SendReliable(hello.data(), hello.size());
                }
            }
        }

        if (event->disconnected)
        {
            if (m_multiplayerMode == MultiplayerMode::Client)
            {
                std::string disconnectMessage = "Disconnected from host.";
                if (!m_lastNetworkError.empty())
                {
                    disconnectMessage += " (" + m_lastNetworkError + ")";
                }
                TransitionNetworkState(
                    m_lastNetworkError.empty() ? NetworkState::Offline : NetworkState::Error,
                    disconnectMessage,
                    !m_lastNetworkError.empty()
                );
                ResetToMainMenu();
                m_menuNetStatus = disconnectMessage;
                break;
            }

            if (m_multiplayerMode == MultiplayerMode::Host)
            {
                m_menuNetStatus = "Client disconnected.";
                m_gameplay.ClearRemoteRoleCommands();
                m_lanDiscovery.UpdateHostInfo(m_sessionMapName, 1, 2, PrimaryLocalIp());
                TransitionNetworkState(NetworkState::HostListening, m_menuNetStatus);
                m_remotePlayer.connected = false;
                m_remotePlayer.controlledRole = "none";
                m_remotePlayer.selectedRole = "none";
                AppendNetworkLog("Peer disconnected: cleared remote ownership mapping.");
            }
        }

        if (!event->payload.empty())
        {
            HandleNetworkPacket(event->payload);
        }
    }
}

void App::HandleNetworkPacket(const std::vector<std::uint8_t>& payload)
{
    if (payload.empty())
    {
        return;
    }

    if (payload[0] == kPacketRoleInput && m_multiplayerMode == MultiplayerMode::Host)
    {
        NetRoleInputPacket inputPacket;
        if (!DeserializeRoleInput(payload, inputPacket))
        {
            return;
        }

        game::gameplay::GameplaySystems::RoleCommand command;
        command.moveAxis = glm::vec2{
            static_cast<float>(inputPacket.moveX) / 100.0F,
            static_cast<float>(inputPacket.moveY) / 100.0F,
        };
        command.lookDelta = glm::vec2{inputPacket.lookX, inputPacket.lookY};
        command.sprinting = (inputPacket.buttons & kButtonSprint) != 0;
        command.interactPressed = (inputPacket.buttons & kButtonInteractPressed) != 0;
        command.interactHeld = (inputPacket.buttons & kButtonInteractHeld) != 0;
        command.attackPressed = (inputPacket.buttons & kButtonAttackPressed) != 0;
        command.attackHeld = (inputPacket.buttons & kButtonAttackHeld) != 0;
        command.attackReleased = (inputPacket.buttons & kButtonAttackReleased) != 0;
        command.lungeHeld = (inputPacket.buttons & kButtonLungeHeld) != 0;
        command.jumpPressed = (inputPacket.buttons & kButtonJumpPressed) != 0;
        command.crouchHeld = (inputPacket.buttons & kButtonCrouchHeld) != 0;
        command.wiggleLeftPressed = (inputPacket.buttons & kButtonWiggleLeftPressed) != 0;
        command.wiggleRightPressed = (inputPacket.buttons & kButtonWiggleRightPressed) != 0;

        const engine::scene::Role remoteRole = m_remoteRoleName == "survivor" ? engine::scene::Role::Survivor : engine::scene::Role::Killer;
        m_gameplay.SetRemoteRoleCommand(remoteRole, command);
        m_remotePlayer.lastInputSeconds = glfwGetTime();
        return;
    }

    if (payload[0] == kPacketRoleChangeRequest && m_multiplayerMode == MultiplayerMode::Host)
    {
        NetRoleChangeRequestPacket request{};
        if (!DeserializeRoleChangeRequest(payload, request))
        {
            AppendNetworkLog("Role change request deserialize failed.");
            return;
        }
        RequestRoleChange(RoleByteToName(request.requestedRole), true);
        return;
    }

    if (payload[0] == kPacketHello && m_multiplayerMode == MultiplayerMode::Host)
    {
        std::string requestedRole;
        std::string requestedMap;
        int protocolVersion = 0;
        std::string buildId;
        if (!DeserializeHello(payload, requestedRole, requestedMap, protocolVersion, buildId))
        {
            return;
        }

        if (protocolVersion != kProtocolVersion || buildId != kBuildId)
        {
            std::vector<std::uint8_t> reject;
            const std::string reason =
                "Version mismatch: client " + std::to_string(protocolVersion) + "/" + buildId +
                ", server " + std::to_string(kProtocolVersion) + "/" + std::string(kBuildId);
            if (SerializeReject(reason, reject))
            {
                m_network.SendReliable(reject.data(), reject.size());
            }
            m_lastNetworkError = reason;
            TransitionNetworkState(NetworkState::Error, reason, true);
            return;
        }

        RequestRoleChange(requestedRole, true);
        SendGameplayTuningToClient();
        m_menuNetStatus = "Client assigned role: " + m_remoteRoleName + " (map: " + requestedMap + ")";
        m_lanDiscovery.UpdateHostInfo(m_sessionMapName, 2, 2, PrimaryLocalIp());
        TransitionNetworkState(NetworkState::Connected, "Client handshake complete");
        return;
    }

    if (payload[0] == kPacketReject && m_multiplayerMode == MultiplayerMode::Client)
    {
        std::string reason;
        if (!DeserializeReject(payload, reason))
        {
            reason = "Handshake rejected by host";
        }
        m_lastNetworkError = reason;
        m_menuNetStatus = reason;
        TransitionNetworkState(NetworkState::Error, reason, true);
        m_network.Disconnect();
        return;
    }

    if (payload[0] == kPacketSnapshot && m_multiplayerMode == MultiplayerMode::Client)
    {
        game::gameplay::GameplaySystems::Snapshot snapshot;
        if (!DeserializeSnapshot(payload, snapshot))
        {
            return;
        }

        m_sessionMapType = snapshot.mapType;
        m_sessionSeed = snapshot.seed;
        m_sessionMapName = MapTypeToName(snapshot.mapType);
        const float blendAlpha = glm::clamp(16.0F / static_cast<float>(std::max(16, m_clientInterpolationBufferMs)), 0.08F, 0.65F);
        m_gameplay.ApplySnapshot(snapshot, blendAlpha);
        m_lastSnapshotReceivedSeconds = glfwGetTime();
        m_remotePlayer.lastSnapshotSeconds = m_lastSnapshotReceivedSeconds;
        return;
    }

    if (payload[0] == kPacketAssignRole && m_multiplayerMode == MultiplayerMode::Client)
    {
        std::uint8_t roleByte = 0;
        game::gameplay::GameplaySystems::MapType mapType = game::gameplay::GameplaySystems::MapType::Main;
        unsigned int seed = 1337U;
        const game::gameplay::GameplaySystems::MapType previousMapType = m_sessionMapType;
        const unsigned int previousSeed = m_sessionSeed;

        if (!DeserializeAssignRole(payload, roleByte, mapType, seed))
        {
            return;
        }

        m_sessionRoleName = RoleByteToName(roleByte);
        m_remoteRoleName = OppositeRoleName(m_sessionRoleName);
        m_sessionMapType = mapType;
        m_sessionMapName = MapTypeToName(mapType);
        m_sessionSeed = seed;

        const bool needsMapLoad =
            (m_networkState != NetworkState::Connected) ||
            (previousMapType != mapType) ||
            (mapType == game::gameplay::GameplaySystems::MapType::Main && previousSeed != seed);
        if (needsMapLoad)
        {
            m_gameplay.LoadMap(m_sessionMapName);
            if (m_sessionMapName == "main")
            {
                m_gameplay.RegenerateLoops(seed);
            }
            ApplyMapEnvironment(m_sessionMapName);
        }
        m_gameplay.SetControlledRole(m_sessionRoleName);
        m_localPlayer.connected = true;
        m_localPlayer.selectedRole = m_sessionRoleName;
        m_localPlayer.controlledRole = m_sessionRoleName;
        m_remotePlayer.connected = true;
        m_remotePlayer.selectedRole = m_remoteRoleName;
        m_remotePlayer.controlledRole = m_remoteRoleName;
        AppendNetworkLog("Possession update from host: local=" + m_sessionRoleName + " remote=" + m_remoteRoleName);

        m_connectedEndpoint = m_joinTargetIp + ":" + std::to_string(m_joinTargetPort);
        m_menuNetStatus = "Assigned role: " + m_sessionRoleName + ".";
        TransitionNetworkState(NetworkState::Connected, "Assigned role: " + m_sessionRoleName);
        return;
    }

    if (payload[0] == kPacketFxSpawn && m_multiplayerMode == MultiplayerMode::Client)
    {
        engine::fx::FxSpawnEvent event;
        if (!DeserializeFxSpawnEvent(payload, event))
        {
            return;
        }

        m_gameplay.SpawnReplicatedFx(event);
        return;
    }

    if (payload[0] == kPacketGameplayTuning && m_multiplayerMode == MultiplayerMode::Client)
    {
        game::gameplay::GameplaySystems::GameplayTuning tuning = m_gameplayEditing;
        if (!DeserializeGameplayTuning(payload, tuning))
        {
            return;
        }
        ApplyGameplaySettings(tuning, true);
        m_serverGameplayValues = true;
        m_menuNetStatus = "Received authoritative gameplay tuning from host.";
    }
}

void App::SendClientInput(const engine::platform::Input& input, bool controlsEnabled)
{
    if (m_multiplayerMode != MultiplayerMode::Client || !m_network.IsConnected())
    {
        return;
    }

    NetRoleInputPacket packet;

    if (controlsEnabled)
    {
        const glm::vec2 moveAxis = ReadMoveAxis(input, m_actionBindings);
        packet.moveX = static_cast<std::int8_t>(std::round(glm::clamp(moveAxis.x, -1.0F, 1.0F) * 100.0F));
        packet.moveY = static_cast<std::int8_t>(std::round(glm::clamp(moveAxis.y, -1.0F, 1.0F) * 100.0F));
        packet.lookX = input.MouseDelta().x;
        packet.lookY = m_controlsSettings.invertY ? -input.MouseDelta().y : input.MouseDelta().y;

        if (m_actionBindings.IsDown(input, platform::InputAction::Sprint))
        {
            packet.buttons |= kButtonSprint;
        }
        if (m_actionBindings.IsPressed(input, platform::InputAction::Interact))
        {
            packet.buttons |= kButtonInteractPressed;
        }
        if (m_actionBindings.IsDown(input, platform::InputAction::Interact))
        {
            packet.buttons |= kButtonInteractHeld;
        }
        if (m_actionBindings.IsPressed(input, platform::InputAction::AttackShort))
        {
            packet.buttons |= kButtonAttackPressed;
        }
        if (m_actionBindings.IsDown(input, platform::InputAction::AttackShort) ||
            m_actionBindings.IsDown(input, platform::InputAction::AttackLunge))
        {
            packet.buttons |= kButtonAttackHeld;
        }
        if (m_actionBindings.IsReleased(input, platform::InputAction::AttackShort) ||
            m_actionBindings.IsReleased(input, platform::InputAction::AttackLunge))
        {
            packet.buttons |= kButtonAttackReleased;
        }
        if (m_actionBindings.IsDown(input, platform::InputAction::AttackLunge))
        {
            packet.buttons |= kButtonLungeHeld;
        }
        if (input.IsKeyPressed(GLFW_KEY_SPACE))
        {
            packet.buttons |= kButtonJumpPressed;
        }
        if (m_actionBindings.IsDown(input, platform::InputAction::Crouch))
        {
            packet.buttons |= kButtonCrouchHeld;
        }
        if (m_actionBindings.IsPressed(input, platform::InputAction::MoveLeft))
        {
            packet.buttons |= kButtonWiggleLeftPressed;
        }
        if (m_actionBindings.IsPressed(input, platform::InputAction::MoveRight))
        {
            packet.buttons |= kButtonWiggleRightPressed;
        }
    }

    std::vector<std::uint8_t> data;
    if (!SerializeRoleInput(packet, data))
    {
        return;
    }

    m_network.SendReliable(data.data(), data.size());
    m_lastInputSentSeconds = glfwGetTime();
    m_localPlayer.lastInputSeconds = m_lastInputSentSeconds;
}

void App::SendHostSnapshot()
{
    if (m_multiplayerMode != MultiplayerMode::Host || !m_network.IsConnected())
    {
        return;
    }

    const game::gameplay::GameplaySystems::Snapshot snapshot = m_gameplay.BuildSnapshot();
    m_sessionMapType = snapshot.mapType;
    m_sessionSeed = snapshot.seed;
    m_sessionMapName = MapTypeToName(snapshot.mapType);
    std::vector<std::uint8_t> data;
    if (!SerializeSnapshot(snapshot, data))
    {
        return;
    }

    m_network.SendReliable(data.data(), data.size());
    m_lastSnapshotSentSeconds = glfwGetTime();
    m_remotePlayer.lastSnapshotSeconds = m_lastSnapshotSentSeconds;
}

void App::SendGameplayTuningToClient()
{
    if (m_multiplayerMode != MultiplayerMode::Host || !m_network.IsConnected())
    {
        return;
    }

    std::vector<std::uint8_t> payload;
    if (!SerializeGameplayTuning(m_gameplayApplied, payload))
    {
        return;
    }
    m_network.SendReliable(payload.data(), payload.size());
}

bool App::SerializeRoleInput(const NetRoleInputPacket& packet, std::vector<std::uint8_t>& outBuffer)
{
    outBuffer.clear();
    outBuffer.reserve(1 + sizeof(NetRoleInputPacket));

    AppendValue(outBuffer, kPacketRoleInput);
    AppendValue(outBuffer, packet.moveX);
    AppendValue(outBuffer, packet.moveY);
    AppendValue(outBuffer, packet.lookX);
    AppendValue(outBuffer, packet.lookY);
    AppendValue(outBuffer, packet.buttons);
    return true;
}

bool App::DeserializeRoleInput(const std::vector<std::uint8_t>& buffer, NetRoleInputPacket& outPacket)
{
    std::size_t offset = 0;
    std::uint8_t type = 0;
    if (!ReadValue(buffer, offset, type) || type != kPacketRoleInput)
    {
        return false;
    }

    return ReadValue(buffer, offset, outPacket.moveX) &&
           ReadValue(buffer, offset, outPacket.moveY) &&
           ReadValue(buffer, offset, outPacket.lookX) &&
           ReadValue(buffer, offset, outPacket.lookY) &&
           ReadValue(buffer, offset, outPacket.buttons);
}

bool App::SerializeSnapshot(const game::gameplay::GameplaySystems::Snapshot& snapshot, std::vector<std::uint8_t>& outBuffer) const
{
    outBuffer.clear();

    AppendValue(outBuffer, kPacketSnapshot);
    AppendValue(outBuffer, MapTypeToByte(snapshot.mapType));
    AppendValue(outBuffer, snapshot.seed);

    auto writePerks = [&](const std::array<std::string, 3>& perkIds) {
        for (const auto& perkId : perkIds)
        {
            const std::uint16_t length = static_cast<std::uint16_t>(std::min<std::size_t>(perkId.size(), 256));
            AppendValue(outBuffer, length);
            outBuffer.insert(outBuffer.end(), perkId.begin(), perkId.begin() + length);
        }
    };

    writePerks(snapshot.survivorPerkIds);
    writePerks(snapshot.killerPerkIds);

    auto writeActor = [&](const game::gameplay::GameplaySystems::ActorSnapshot& actor) {
        AppendValue(outBuffer, actor.position.x);
        AppendValue(outBuffer, actor.position.y);
        AppendValue(outBuffer, actor.position.z);
        AppendValue(outBuffer, actor.forward.x);
        AppendValue(outBuffer, actor.forward.y);
        AppendValue(outBuffer, actor.forward.z);
        AppendValue(outBuffer, actor.velocity.x);
        AppendValue(outBuffer, actor.velocity.y);
        AppendValue(outBuffer, actor.velocity.z);
        AppendValue(outBuffer, actor.yaw);
        AppendValue(outBuffer, actor.pitch);
    };

    writeActor(snapshot.survivor);
    writeActor(snapshot.killer);

    AppendValue(outBuffer, snapshot.survivorState);
    AppendValue(outBuffer, snapshot.killerAttackState);
    AppendValue(outBuffer, snapshot.killerAttackStateTimer);
    AppendValue(outBuffer, snapshot.killerLungeCharge);
    AppendValue(outBuffer, static_cast<std::uint8_t>(snapshot.chaseActive ? 1 : 0));
    AppendValue(outBuffer, snapshot.chaseDistance);
    AppendValue(outBuffer, static_cast<std::uint8_t>(snapshot.chaseLos ? 1 : 0));

    const std::uint16_t palletCount = static_cast<std::uint16_t>(std::min<std::size_t>(snapshot.pallets.size(), 1024));
    AppendValue(outBuffer, palletCount);
    for (std::size_t i = 0; i < palletCount; ++i)
    {
        const auto& pallet = snapshot.pallets[i];
        AppendValue(outBuffer, pallet.entity);
        AppendValue(outBuffer, pallet.state);
        AppendValue(outBuffer, pallet.breakTimer);
        AppendValue(outBuffer, pallet.position.x);
        AppendValue(outBuffer, pallet.position.y);
        AppendValue(outBuffer, pallet.position.z);
        AppendValue(outBuffer, pallet.halfExtents.x);
        AppendValue(outBuffer, pallet.halfExtents.y);
        AppendValue(outBuffer, pallet.halfExtents.z);
    }

    return true;
}

bool App::DeserializeSnapshot(const std::vector<std::uint8_t>& buffer, game::gameplay::GameplaySystems::Snapshot& outSnapshot) const
{
    std::size_t offset = 0;
    std::uint8_t type = 0;
    std::uint8_t mapTypeByte = 0;

    if (!ReadValue(buffer, offset, type) || type != kPacketSnapshot)
    {
        return false;
    }

    if (!ReadValue(buffer, offset, mapTypeByte))
    {
        return false;
    }

    outSnapshot.mapType = ByteToMapType(mapTypeByte);
    if (!ReadValue(buffer, offset, outSnapshot.seed))
    {
        return false;
    }

    auto readPerks = [&](std::array<std::string, 3>& perkIds) {
        for (int i = 0; i < 3; ++i)
        {
            std::uint16_t length = 0;
            if (!ReadValue(buffer, offset, length))
            {
                return false;
            }
            if (offset + length > buffer.size())
            {
                return false;
            }
            perkIds[i].assign(reinterpret_cast<const char*>(buffer.data() + offset), length);
            offset += length;
        }
        return true;
    };

    if (!readPerks(outSnapshot.survivorPerkIds) || !readPerks(outSnapshot.killerPerkIds))
    {
        return false;
    }

    auto readActor = [&](game::gameplay::GameplaySystems::ActorSnapshot& actor) {
        return ReadValue(buffer, offset, actor.position.x) &&
               ReadValue(buffer, offset, actor.position.y) &&
               ReadValue(buffer, offset, actor.position.z) &&
               ReadValue(buffer, offset, actor.forward.x) &&
               ReadValue(buffer, offset, actor.forward.y) &&
               ReadValue(buffer, offset, actor.forward.z) &&
               ReadValue(buffer, offset, actor.velocity.x) &&
               ReadValue(buffer, offset, actor.velocity.y) &&
               ReadValue(buffer, offset, actor.velocity.z) &&
               ReadValue(buffer, offset, actor.yaw) &&
               ReadValue(buffer, offset, actor.pitch);
    };

    if (!readActor(outSnapshot.survivor) || !readActor(outSnapshot.killer))
    {
        return false;
    }

    std::uint8_t chaseActiveByte = 0;
    std::uint8_t chaseLosByte = 0;
    if (!ReadValue(buffer, offset, outSnapshot.survivorState) ||
        !ReadValue(buffer, offset, outSnapshot.killerAttackState) ||
        !ReadValue(buffer, offset, outSnapshot.killerAttackStateTimer) ||
        !ReadValue(buffer, offset, outSnapshot.killerLungeCharge) ||
        !ReadValue(buffer, offset, chaseActiveByte) ||
        !ReadValue(buffer, offset, outSnapshot.chaseDistance) ||
        !ReadValue(buffer, offset, chaseLosByte))
    {
        return false;
    }

    outSnapshot.chaseActive = chaseActiveByte != 0;
    outSnapshot.chaseLos = chaseLosByte != 0;

    std::uint16_t palletCount = 0;
    if (!ReadValue(buffer, offset, palletCount))
    {
        return false;
    }

    outSnapshot.pallets.clear();
    outSnapshot.pallets.reserve(palletCount);

    for (std::uint16_t i = 0; i < palletCount; ++i)
    {
        game::gameplay::GameplaySystems::PalletSnapshot pallet;
        if (!ReadValue(buffer, offset, pallet.entity) ||
            !ReadValue(buffer, offset, pallet.state) ||
            !ReadValue(buffer, offset, pallet.breakTimer) ||
            !ReadValue(buffer, offset, pallet.position.x) ||
            !ReadValue(buffer, offset, pallet.position.y) ||
            !ReadValue(buffer, offset, pallet.position.z) ||
            !ReadValue(buffer, offset, pallet.halfExtents.x) ||
            !ReadValue(buffer, offset, pallet.halfExtents.y) ||
            !ReadValue(buffer, offset, pallet.halfExtents.z))
        {
            return false;
        }

        outSnapshot.pallets.push_back(pallet);
    }

    return true;
}

bool App::SerializeGameplayTuning(
    const game::gameplay::GameplaySystems::GameplayTuning& tuning,
    std::vector<std::uint8_t>& outBuffer
) const
{
    outBuffer.clear();
    AppendValue(outBuffer, kPacketGameplayTuning);
    AppendValue(outBuffer, tuning.assetVersion);
    AppendValue(outBuffer, tuning.survivorWalkSpeed);
    AppendValue(outBuffer, tuning.survivorSprintSpeed);
    AppendValue(outBuffer, tuning.survivorCrouchSpeed);
    AppendValue(outBuffer, tuning.survivorCrawlSpeed);
    AppendValue(outBuffer, tuning.killerMoveSpeed);
    AppendValue(outBuffer, tuning.survivorCapsuleRadius);
    AppendValue(outBuffer, tuning.survivorCapsuleHeight);
    AppendValue(outBuffer, tuning.killerCapsuleRadius);
    AppendValue(outBuffer, tuning.killerCapsuleHeight);
    AppendValue(outBuffer, tuning.terrorRadiusMeters);
    AppendValue(outBuffer, tuning.terrorRadiusChaseMeters);
    AppendValue(outBuffer, tuning.vaultSlowTime);
    AppendValue(outBuffer, tuning.vaultMediumTime);
    AppendValue(outBuffer, tuning.vaultFastTime);
    AppendValue(outBuffer, tuning.fastVaultDotThreshold);
    AppendValue(outBuffer, tuning.fastVaultSpeedMultiplier);
    AppendValue(outBuffer, tuning.fastVaultMinRunup);
    AppendValue(outBuffer, tuning.shortAttackRange);
    AppendValue(outBuffer, tuning.shortAttackAngleDegrees);
    AppendValue(outBuffer, tuning.lungeHoldMinSeconds);
    AppendValue(outBuffer, tuning.lungeDurationSeconds);
    AppendValue(outBuffer, tuning.lungeRecoverSeconds);
    AppendValue(outBuffer, tuning.shortRecoverSeconds);
    AppendValue(outBuffer, tuning.missRecoverSeconds);
    AppendValue(outBuffer, tuning.lungeSpeedStart);
    AppendValue(outBuffer, tuning.lungeSpeedEnd);
    AppendValue(outBuffer, tuning.healDurationSeconds);
    AppendValue(outBuffer, tuning.skillCheckMinInterval);
    AppendValue(outBuffer, tuning.skillCheckMaxInterval);
    AppendValue(outBuffer, tuning.weightTLWalls);
    AppendValue(outBuffer, tuning.weightJungleGymLong);
    AppendValue(outBuffer, tuning.weightJungleGymShort);
    AppendValue(outBuffer, tuning.weightShack);
    AppendValue(outBuffer, tuning.weightFourLane);
    AppendValue(outBuffer, tuning.weightFillerA);
    AppendValue(outBuffer, tuning.weightFillerB);
    AppendValue(outBuffer, tuning.weightLongWall);
    AppendValue(outBuffer, tuning.weightShortWall);
    AppendValue(outBuffer, tuning.weightLWallWindow);
    AppendValue(outBuffer, tuning.weightLWallPallet);
    AppendValue(outBuffer, tuning.weightTWalls);
    AppendValue(outBuffer, tuning.weightGymBox);
    AppendValue(outBuffer, tuning.weightDebrisPile);
    AppendValue(outBuffer, tuning.maxLoopsPerMap);
    AppendValue(outBuffer, tuning.minLoopDistanceTiles);
    AppendValue(outBuffer, tuning.maxSafePallets);
    AppendValue(outBuffer, tuning.maxDeadzoneTiles);
    AppendValue(outBuffer, static_cast<std::uint8_t>(tuning.edgeBiasLoops ? 1 : 0));
    AppendValue(outBuffer, tuning.serverTickRate);
    AppendValue(outBuffer, tuning.interpolationBufferMs);
    return true;
}

bool App::DeserializeGameplayTuning(
    const std::vector<std::uint8_t>& buffer,
    game::gameplay::GameplaySystems::GameplayTuning& outTuning
) const
{
    std::size_t offset = 0;
    std::uint8_t type = 0;
    if (!ReadValue(buffer, offset, type) || type != kPacketGameplayTuning)
    {
        return false;
    }

    return ReadValue(buffer, offset, outTuning.assetVersion) &&
           ReadValue(buffer, offset, outTuning.survivorWalkSpeed) &&
           ReadValue(buffer, offset, outTuning.survivorSprintSpeed) &&
           ReadValue(buffer, offset, outTuning.survivorCrouchSpeed) &&
           ReadValue(buffer, offset, outTuning.survivorCrawlSpeed) &&
           ReadValue(buffer, offset, outTuning.killerMoveSpeed) &&
           ReadValue(buffer, offset, outTuning.survivorCapsuleRadius) &&
           ReadValue(buffer, offset, outTuning.survivorCapsuleHeight) &&
           ReadValue(buffer, offset, outTuning.killerCapsuleRadius) &&
           ReadValue(buffer, offset, outTuning.killerCapsuleHeight) &&
           ReadValue(buffer, offset, outTuning.terrorRadiusMeters) &&
           ReadValue(buffer, offset, outTuning.terrorRadiusChaseMeters) &&
           ReadValue(buffer, offset, outTuning.vaultSlowTime) &&
           ReadValue(buffer, offset, outTuning.vaultMediumTime) &&
           ReadValue(buffer, offset, outTuning.vaultFastTime) &&
           ReadValue(buffer, offset, outTuning.fastVaultDotThreshold) &&
           ReadValue(buffer, offset, outTuning.fastVaultSpeedMultiplier) &&
           ReadValue(buffer, offset, outTuning.fastVaultMinRunup) &&
           ReadValue(buffer, offset, outTuning.shortAttackRange) &&
           ReadValue(buffer, offset, outTuning.shortAttackAngleDegrees) &&
           ReadValue(buffer, offset, outTuning.lungeHoldMinSeconds) &&
           ReadValue(buffer, offset, outTuning.lungeDurationSeconds) &&
           ReadValue(buffer, offset, outTuning.lungeRecoverSeconds) &&
           ReadValue(buffer, offset, outTuning.shortRecoverSeconds) &&
           ReadValue(buffer, offset, outTuning.missRecoverSeconds) &&
           ReadValue(buffer, offset, outTuning.lungeSpeedStart) &&
           ReadValue(buffer, offset, outTuning.lungeSpeedEnd) &&
           ReadValue(buffer, offset, outTuning.healDurationSeconds) &&
           ReadValue(buffer, offset, outTuning.skillCheckMinInterval) &&
           ReadValue(buffer, offset, outTuning.skillCheckMaxInterval) &&
           ReadValue(buffer, offset, outTuning.weightTLWalls) &&
           ReadValue(buffer, offset, outTuning.weightJungleGymLong) &&
           ReadValue(buffer, offset, outTuning.weightJungleGymShort) &&
           ReadValue(buffer, offset, outTuning.weightShack) &&
           ReadValue(buffer, offset, outTuning.weightFourLane) &&
           ReadValue(buffer, offset, outTuning.weightFillerA) &&
           ReadValue(buffer, offset, outTuning.weightFillerB) &&
           ReadValue(buffer, offset, outTuning.weightLongWall) &&
           ReadValue(buffer, offset, outTuning.weightShortWall) &&
           ReadValue(buffer, offset, outTuning.weightLWallWindow) &&
           ReadValue(buffer, offset, outTuning.weightLWallPallet) &&
           ReadValue(buffer, offset, outTuning.weightTWalls) &&
           ReadValue(buffer, offset, outTuning.weightGymBox) &&
           ReadValue(buffer, offset, outTuning.weightDebrisPile) &&
           ReadValue(buffer, offset, outTuning.maxLoopsPerMap) &&
           ReadValue(buffer, offset, outTuning.minLoopDistanceTiles) &&
           [&]() -> bool {
               if (!ReadValue(buffer, offset, outTuning.maxSafePallets)) return false;
               if (!ReadValue(buffer, offset, outTuning.maxDeadzoneTiles)) return false;
               std::uint8_t edgeBias = 0;
               if (!ReadValue(buffer, offset, edgeBias)) return false;
               outTuning.edgeBiasLoops = (edgeBias != 0);
               return true;
           }() &&
           ReadValue(buffer, offset, outTuning.serverTickRate) &&
           ReadValue(buffer, offset, outTuning.interpolationBufferMs);
}

bool App::SerializeAssignRole(
    std::uint8_t roleByte,
    game::gameplay::GameplaySystems::MapType mapType,
    unsigned int seed,
    std::vector<std::uint8_t>& outBuffer
)
{
    outBuffer.clear();
    AppendValue(outBuffer, kPacketAssignRole);
    AppendValue(outBuffer, roleByte);
    AppendValue(outBuffer, MapTypeToByte(mapType));
    AppendValue(outBuffer, seed);
    return true;
}

bool App::DeserializeAssignRole(
    const std::vector<std::uint8_t>& buffer,
    std::uint8_t& outRole,
    game::gameplay::GameplaySystems::MapType& outMapType,
    unsigned int& outSeed
) const
{
    std::size_t offset = 0;
    std::uint8_t type = 0;
    std::uint8_t mapTypeByte = 0;

    if (!ReadValue(buffer, offset, type) || type != kPacketAssignRole)
    {
        return false;
    }

    if (!ReadValue(buffer, offset, outRole) ||
        !ReadValue(buffer, offset, mapTypeByte) ||
        !ReadValue(buffer, offset, outSeed))
    {
        return false;
    }

    outMapType = ByteToMapType(mapTypeByte);
    return true;
}

bool App::SerializeHello(const std::string& requestedRole, std::vector<std::uint8_t>& outBuffer) const
{
    outBuffer.clear();
    AppendValue(outBuffer, kPacketHello);
    AppendValue(outBuffer, static_cast<std::int32_t>(kProtocolVersion));

    const std::string build = kBuildId;
    const std::uint16_t buildLen = static_cast<std::uint16_t>(std::min<std::size_t>(build.size(), 255));
    AppendValue(outBuffer, buildLen);
    outBuffer.insert(outBuffer.end(), build.begin(), build.begin() + buildLen);

    const std::uint16_t roleLen = static_cast<std::uint16_t>(std::min<std::size_t>(requestedRole.size(), 64));
    AppendValue(outBuffer, roleLen);
    outBuffer.insert(outBuffer.end(), requestedRole.begin(), requestedRole.begin() + roleLen);

    const std::uint16_t mapLen = static_cast<std::uint16_t>(std::min<std::size_t>(m_sessionMapName.size(), 64));
    AppendValue(outBuffer, mapLen);
    outBuffer.insert(outBuffer.end(), m_sessionMapName.begin(), m_sessionMapName.begin() + mapLen);
    return true;
}

bool App::DeserializeHello(
    const std::vector<std::uint8_t>& buffer,
    std::string& outRequestedRole,
    std::string& outMapName,
    int& outProtocolVersion,
    std::string& outBuildId
) const
{
    outRequestedRole.clear();
    outMapName.clear();
    outBuildId.clear();
    outProtocolVersion = 0;

    std::size_t offset = 0;
    std::uint8_t type = 0;
    std::int32_t protocol = 0;
    std::uint16_t buildLen = 0;
    std::uint16_t roleLen = 0;
    std::uint16_t mapLen = 0;

    if (!ReadValue(buffer, offset, type) || type != kPacketHello)
    {
        return false;
    }
    if (!ReadValue(buffer, offset, protocol))
    {
        return false;
    }
    outProtocolVersion = protocol;
    if (!ReadValue(buffer, offset, buildLen))
    {
        return false;
    }
    if (offset + buildLen > buffer.size())
    {
        return false;
    }
    outBuildId.assign(reinterpret_cast<const char*>(buffer.data() + offset), buildLen);
    offset += buildLen;

    if (!ReadValue(buffer, offset, roleLen))
    {
        return false;
    }
    if (offset + roleLen > buffer.size())
    {
        return false;
    }
    outRequestedRole.assign(reinterpret_cast<const char*>(buffer.data() + offset), roleLen);
    offset += roleLen;

    if (!ReadValue(buffer, offset, mapLen))
    {
        return false;
    }
    if (offset + mapLen > buffer.size())
    {
        return false;
    }
    outMapName.assign(reinterpret_cast<const char*>(buffer.data() + offset), mapLen);
    return true;
}

bool App::SerializeReject(const std::string& reason, std::vector<std::uint8_t>& outBuffer) const
{
    outBuffer.clear();
    AppendValue(outBuffer, kPacketReject);
    const std::uint16_t reasonLen = static_cast<std::uint16_t>(std::min<std::size_t>(reason.size(), 512));
    AppendValue(outBuffer, reasonLen);
    outBuffer.insert(outBuffer.end(), reason.begin(), reason.begin() + reasonLen);
    return true;
}

bool App::DeserializeReject(const std::vector<std::uint8_t>& buffer, std::string& outReason) const
{
    outReason.clear();
    std::size_t offset = 0;
    std::uint8_t type = 0;
    std::uint16_t reasonLen = 0;
    if (!ReadValue(buffer, offset, type) || type != kPacketReject)
    {
        return false;
    }
    if (!ReadValue(buffer, offset, reasonLen))
    {
        return false;
    }
    if (offset + reasonLen > buffer.size())
    {
        return false;
    }
    outReason.assign(reinterpret_cast<const char*>(buffer.data() + offset), reasonLen);
    return true;
}

bool App::SerializeRoleChangeRequest(const NetRoleChangeRequestPacket& packet, std::vector<std::uint8_t>& outBuffer)
{
    outBuffer.clear();
    AppendValue(outBuffer, kPacketRoleChangeRequest);
    AppendValue(outBuffer, packet.requestedRole);
    return true;
}

bool App::DeserializeRoleChangeRequest(const std::vector<std::uint8_t>& buffer, NetRoleChangeRequestPacket& outPacket)
{
    std::size_t offset = 0;
    std::uint8_t type = 0;
    if (!ReadValue(buffer, offset, type) || type != kPacketRoleChangeRequest)
    {
        return false;
    }
    return ReadValue(buffer, offset, outPacket.requestedRole);
}

void App::TickLanDiscovery(double nowSeconds)
{
    if (m_multiplayerMode == MultiplayerMode::Host)
    {
        const int players = m_network.IsConnected() ? 2 : 1;
        m_lanDiscovery.UpdateHostInfo(m_sessionMapName, players, 2, PrimaryLocalIp());
        if (m_lanDiscovery.GetMode() != net::LanDiscovery::Mode::Host)
        {
            const char* hostNameEnv = std::getenv("COMPUTERNAME");
            if (hostNameEnv == nullptr)
            {
                hostNameEnv = std::getenv("HOSTNAME");
            }
            const std::string hostName = hostNameEnv != nullptr ? hostNameEnv : "DBD-Prototype";
            m_lanDiscovery.StartHost(
                m_lanDiscoveryPort,
                m_defaultGamePort,
                hostName,
                m_sessionMapName,
                players,
                2,
                kProtocolVersion,
                kBuildId,
                PrimaryLocalIp()
            );
        }
    }
    else if (m_appMode == AppMode::MainMenu)
    {
        if (m_lanDiscovery.GetMode() != net::LanDiscovery::Mode::Client)
        {
            m_lanDiscovery.StartClient(m_lanDiscoveryPort, kProtocolVersion, kBuildId);
        }
    }
    else if (m_multiplayerMode != MultiplayerMode::Client)
    {
        if (m_lanDiscovery.GetMode() == net::LanDiscovery::Mode::Client)
        {
            m_lanDiscovery.Stop();
        }
    }

    m_lanDiscovery.Tick(nowSeconds);
}

void App::TransitionNetworkState(NetworkState state, const std::string& reason, bool isError)
{
    m_networkState = state;
    m_statusToastMessage = "[NET] " + NetworkStateToText(state) + ": " + reason;
    m_statusToastUntilSeconds = glfwGetTime() + 3.0;
    if (isError)
    {
        m_lastNetworkError = reason;
    }
    // Dismiss connecting loading screen on terminal states
    if (state == NetworkState::Connected || state == NetworkState::Error || state == NetworkState::Offline)
    {
        m_connectingLoadingActive = false;
    }
    std::cout << m_statusToastMessage << "\n";
    AppendNetworkLog(m_statusToastMessage);
}

void App::AppendNetworkLog(const std::string& text)
{
    if (!m_networkLogFile.is_open())
    {
        return;
    }

    const auto now = std::chrono::system_clock::now();
    const std::time_t nowTime = std::chrono::system_clock::to_time_t(now);
    std::tm localTm{};
#ifdef _WIN32
    localtime_s(&localTm, &nowTime);
#else
    localtime_r(&nowTime, &localTm);
#endif
    char timeBuffer[64]{};
    std::strftime(timeBuffer, sizeof(timeBuffer), "%Y-%m-%d %H:%M:%S", &localTm);
    m_networkLogFile << "[" << timeBuffer << "] " << text << "\n";
    m_networkLogFile.flush();
}

void App::OpenNetworkLogFile()
{
    std::filesystem::create_directories("logs");
    m_networkLogFile.open("logs/network.log", std::ios::out | std::ios::app);
    AppendNetworkLog("=== Session start ===");
}

void App::CloseNetworkLogFile()
{
    if (m_networkLogFile.is_open())
    {
        AppendNetworkLog("=== Session end ===");
        m_networkLogFile.close();
    }
}

void App::BuildLocalIpv4List()
{
    m_localIpv4Addresses.clear();

#ifdef _WIN32
    WSADATA wsaData{};
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
    {
        return;
    }

    char hostName[256]{};
    if (gethostname(hostName, sizeof(hostName)) != 0)
    {
        WSACleanup();
        return;
    }

    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* result = nullptr;
    if (getaddrinfo(hostName, nullptr, &hints, &result) != 0)
    {
        WSACleanup();
        return;
    }

    for (addrinfo* it = result; it != nullptr; it = it->ai_next)
    {
        if (it->ai_addr == nullptr || it->ai_addr->sa_family != AF_INET)
        {
            continue;
        }
        auto* ipv4 = reinterpret_cast<sockaddr_in*>(it->ai_addr);
        char ipBuffer[INET_ADDRSTRLEN]{};
        if (inet_ntop(AF_INET, &ipv4->sin_addr, ipBuffer, sizeof(ipBuffer)) == nullptr)
        {
            continue;
        }
        const std::string ip = ipBuffer;
        if (ip.rfind("127.", 0) == 0)
        {
            continue;
        }
        if (std::find(m_localIpv4Addresses.begin(), m_localIpv4Addresses.end(), ip) == m_localIpv4Addresses.end())
        {
            m_localIpv4Addresses.push_back(ip);
        }
    }

    freeaddrinfo(result);
    WSACleanup();
#else
    ifaddrs* ifaddr = nullptr;
    if (getifaddrs(&ifaddr) != 0 || ifaddr == nullptr)
    {
        return;
    }
    for (ifaddrs* it = ifaddr; it != nullptr; it = it->ifa_next)
    {
        if (it->ifa_addr == nullptr || it->ifa_addr->sa_family != AF_INET)
        {
            continue;
        }
        if ((it->ifa_flags & IFF_LOOPBACK) != 0)
        {
            continue;
        }

        char ipBuffer[INET_ADDRSTRLEN]{};
        auto* ipv4 = reinterpret_cast<sockaddr_in*>(it->ifa_addr);
        if (inet_ntop(AF_INET, &ipv4->sin_addr, ipBuffer, sizeof(ipBuffer)) == nullptr)
        {
            continue;
        }
        const std::string ip = ipBuffer;
        if (std::find(m_localIpv4Addresses.begin(), m_localIpv4Addresses.end(), ip) == m_localIpv4Addresses.end())
        {
            m_localIpv4Addresses.push_back(ip);
        }
    }
    freeifaddrs(ifaddr);
#endif
}

std::string App::PrimaryLocalIp() const
{
    if (!m_localIpv4Addresses.empty())
    {
        return m_localIpv4Addresses.front();
    }
    return "unknown";
}

std::string App::BuildHostHelpText() const
{
    std::ostringstream oss;
    if (m_localIpv4Addresses.empty())
    {
        oss << "Local IP: unknown (check OS network settings)";
        return oss.str();
    }

    oss << "Hosting on: ";
    for (std::size_t i = 0; i < m_localIpv4Addresses.size(); ++i)
    {
        if (i > 0)
        {
            oss << ", ";
        }
        oss << m_localIpv4Addresses[i] << ":" << m_defaultGamePort;
    }
    oss << "\nLAN: use local IP above";
    oss << "\nCopy-ready: " << m_localIpv4Addresses.front() << " " << m_defaultGamePort;
    oss << "\nInternet play: requires port forwarding or VPN";
    return oss.str();
}

std::string App::NetworkStateToText(NetworkState state) const
{
    switch (state)
    {
        case NetworkState::Offline: return "OFFLINE";
        case NetworkState::HostStarting: return "HOST_STARTING";
        case NetworkState::HostListening: return "HOST_LISTENING";
        case NetworkState::ClientConnecting: return "CLIENT_CONNECTING";
        case NetworkState::ClientHandshaking: return "CLIENT_HANDSHAKING";
        case NetworkState::Connected: return "CONNECTED";
        case NetworkState::Disconnecting: return "DISCONNECTING";
        case NetworkState::Error: return "ERROR";
        default: return "UNKNOWN";
    }
}

std::string App::NetStatusDump() const
{
    std::ostringstream oss;
    oss << "State=" << NetworkStateToText(m_networkState);
    oss << " mode=" << (m_multiplayerMode == MultiplayerMode::Solo ? "solo" : (m_multiplayerMode == MultiplayerMode::Host ? "host" : "client"));
    if (!m_connectedEndpoint.empty())
    {
        oss << " endpoint=" << m_connectedEndpoint;
    }
    if (!m_lastNetworkError.empty())
    {
        oss << " error=\"" << m_lastNetworkError << "\"";
    }
    if (!m_localIpv4Addresses.empty())
    {
        oss << " ips=";
        for (std::size_t i = 0; i < m_localIpv4Addresses.size(); ++i)
        {
            if (i > 0)
            {
                oss << ",";
            }
            oss << m_localIpv4Addresses[i];
        }
    }

    oss << " local_role=" << m_localPlayer.controlledRole;
    if (m_remotePlayer.connected)
    {
        oss << " remote_role=" << m_remotePlayer.controlledRole;
    }
    else
    {
        oss << " remote_role=none";
    }

    const auto stats = m_network.GetConnectionStats();
    oss << " peers=" << stats.peerCount;
    if (stats.available)
    {
        oss << " rtt_ms=" << stats.rttMs << " loss=" << stats.packetLoss;
    }
    else
    {
        oss << " rtt_ms=n/a loss=n/a";
    }
    return oss.str();
}

std::string App::NetConfigDump() const
{
    std::ostringstream oss;
    oss << "tick_hz=" << m_fixedTickHz
        << " send_snapshot_hz=60"
        << " interpolation_buffer_ms=" << m_clientInterpolationBufferMs
        << " protocol=" << kProtocolVersion
        << " build=" << kBuildId
        << " game_port=" << m_defaultGamePort
        << " discovery_port=" << m_lanDiscoveryPort;
    return oss.str();
}

engine::scene::Role App::RoleFromString(const std::string& roleName) const
{
    return roleName == "killer" ? engine::scene::Role::Killer : engine::scene::Role::Survivor;
}

std::string App::RoleToString(engine::scene::Role role) const
{
    return role == engine::scene::Role::Killer ? "killer" : "survivor";
}

std::string App::NormalizeRoleName(const std::string& roleName) const
{
    return roleName == "killer" ? "killer" : "survivor";
}

std::string App::OppositeRoleName(const std::string& roleName) const
{
    return NormalizeRoleName(roleName) == "killer" ? "survivor" : "killer";
}

void App::InitializePlayerBindings()
{
    m_localPlayer.netId = 1;
    m_localPlayer.name = "Player1";
    m_localPlayer.isHost = (m_multiplayerMode != MultiplayerMode::Client);
    m_localPlayer.connected = true;
    m_localPlayer.selectedRole = NormalizeRoleName(m_sessionRoleName);
    m_localPlayer.controlledRole = m_localPlayer.selectedRole;
    m_localPlayer.lastInputSeconds = 0.0;
    m_localPlayer.lastSnapshotSeconds = 0.0;

    m_remotePlayer.netId = 2;
    m_remotePlayer.name = "Player2";
    m_remotePlayer.isHost = false;
    m_remotePlayer.connected = m_network.IsConnected();
    m_remotePlayer.selectedRole = m_remotePlayer.connected ? NormalizeRoleName(m_remoteRoleName) : "none";
    m_remotePlayer.controlledRole = m_remotePlayer.selectedRole;
    m_remotePlayer.lastInputSeconds = 0.0;
    m_remotePlayer.lastSnapshotSeconds = 0.0;
}

void App::ApplyRoleMapping(
    const std::string& localRole,
    const std::string& remoteRole,
    const std::string& reason,
    bool respawnLocal,
    bool respawnRemote
)
{
    const std::string normalizedLocal = NormalizeRoleName(localRole);
    const std::string normalizedRemote = NormalizeRoleName(remoteRole);

    const std::string previousLocalRole = m_sessionRoleName;
    const std::string previousRemoteRole = m_remoteRoleName;

    m_sessionRoleName = normalizedLocal;
    m_remoteRoleName = normalizedRemote;
    m_pendingRemoteRoleRequest = normalizedRemote;

    m_localPlayer.selectedRole = normalizedLocal;
    m_localPlayer.controlledRole = normalizedLocal;
    m_remotePlayer.selectedRole = m_remotePlayer.connected ? normalizedRemote : "none";
    m_remotePlayer.controlledRole = m_remotePlayer.connected ? normalizedRemote : "none";

    m_gameplay.SetControlledRole(normalizedLocal);

    bool localRespawnOk = true;
    bool remoteRespawnOk = true;
    if (respawnLocal)
    {
        localRespawnOk = m_gameplay.RespawnRole(normalizedLocal);
    }
    if (respawnRemote && (m_multiplayerMode != MultiplayerMode::Solo || m_remotePlayer.connected))
    {
        remoteRespawnOk = m_gameplay.RespawnRole(normalizedRemote);
    }

    std::ostringstream oss;
    oss << "Role mapping update (" << reason << "): local " << previousLocalRole << "->" << normalizedLocal
        << ", remote " << previousRemoteRole << "->" << normalizedRemote
        << ", respawn(local=" << (localRespawnOk ? "ok" : "fail")
        << ", remote=" << (remoteRespawnOk ? "ok" : "fail") << ")";
    AppendNetworkLog(oss.str());
}

void App::RequestRoleChange(const std::string& requestedRole, bool fromRemotePeer)
{
    const std::string normalizedRole = NormalizeRoleName(requestedRole);
    if (m_multiplayerMode == MultiplayerMode::Client)
    {
        if (fromRemotePeer)
        {
            return;
        }
        if (!SendRoleChangeRequestToHost(normalizedRole))
        {
            m_menuNetStatus = "Role change request failed.";
            TransitionNetworkState(NetworkState::Error, "Failed to send role change request", true);
            return;
        }
        m_menuNetStatus = "Role change requested: " + normalizedRole;
        AppendNetworkLog("Client requested role change to " + normalizedRole);
        return;
    }

    if (fromRemotePeer)
    {
        if (!m_network.IsConnected())
        {
            AppendNetworkLog("Ignored remote role change request: no active peer.");
            return;
        }
        const std::string remoteRole = normalizedRole;
        const std::string localRole = OppositeRoleName(remoteRole);
        ApplyRoleMapping(localRole, remoteRole, "remote request", true, true);
        SendAssignRoleToClient(remoteRole);
        m_menuNetStatus = "Remote role switched to " + remoteRole + ".";
        return;
    }

    const std::string localRole = normalizedRole;
    const std::string remoteRole = OppositeRoleName(localRole);
    ApplyRoleMapping(localRole, remoteRole, "local request", true, m_network.IsConnected());
    if (m_multiplayerMode == MultiplayerMode::Host && m_network.IsConnected())
    {
        SendAssignRoleToClient(remoteRole);
    }
    m_menuNetStatus = "Local role switched to " + localRole + ".";
}

void App::SendAssignRoleToClient(const std::string& remoteRole)
{
    if (m_multiplayerMode != MultiplayerMode::Host || !m_network.IsConnected())
    {
        return;
    }

    std::vector<std::uint8_t> assign;
    if (!SerializeAssignRole(RoleNameToByte(remoteRole), m_sessionMapType, m_sessionSeed, assign))
    {
        AppendNetworkLog("SerializeAssignRole failed while sending role update.");
        return;
    }

    m_network.SendReliable(assign.data(), assign.size());
    AppendNetworkLog("Sent possession update to client: role=" + NormalizeRoleName(remoteRole));
}

bool App::SendRoleChangeRequestToHost(const std::string& requestedRole)
{
    if (m_multiplayerMode != MultiplayerMode::Client || !m_network.IsConnected())
    {
        return false;
    }

    NetRoleChangeRequestPacket request{};
    request.requestedRole = RoleNameToByte(requestedRole);
    std::vector<std::uint8_t> payload;
    if (!SerializeRoleChangeRequest(request, payload))
    {
        return false;
    }
    m_network.SendReliable(payload.data(), payload.size());
    return true;
}

std::string App::PlayerDump() const
{
    std::ostringstream oss;
    oss << "Players -> ControlledPawn\n";
    auto dumpPlayer = [&](const PlayerBinding& player) {
        oss << "  netId=" << player.netId
            << " name=" << player.name
            << " connected=" << (player.connected ? "true" : "false")
            << " selectedRole=" << player.selectedRole
            << " controlledRole=" << player.controlledRole;
        if (player.connected && (player.controlledRole == "survivor" || player.controlledRole == "killer"))
        {
            const auto pawn = static_cast<unsigned int>(m_gameplay.RoleEntity(player.controlledRole));
            oss << " pawn=" << pawn;
        }
        oss << "\n";
    };
    dumpPlayer(m_localPlayer);
    if (m_multiplayerMode != MultiplayerMode::Solo || m_remotePlayer.connected)
    {
        dumpPlayer(m_remotePlayer);
    }

    const auto survivorPawn = static_cast<unsigned int>(m_gameplay.RoleEntity("survivor"));
    const auto killerPawn = static_cast<unsigned int>(m_gameplay.RoleEntity("killer"));
    const std::string survivorOwner =
        m_localPlayer.controlledRole == "survivor" ? ("netId=" + std::to_string(m_localPlayer.netId))
        : (m_remotePlayer.connected && m_remotePlayer.controlledRole == "survivor" ? ("netId=" + std::to_string(m_remotePlayer.netId)) : "none");
    const std::string killerOwner =
        m_localPlayer.controlledRole == "killer" ? ("netId=" + std::to_string(m_localPlayer.netId))
        : (m_remotePlayer.connected && m_remotePlayer.controlledRole == "killer" ? ("netId=" + std::to_string(m_remotePlayer.netId)) : "none");

    oss << "Pawn -> Owner\n";
    oss << "  survivor_pawn=" << survivorPawn << " owner=" << survivorOwner << "\n";
    oss << "  killer_pawn=" << killerPawn << " owner=" << killerOwner << "\n";
    return oss.str();
}

bool App::LoadControlsConfig()
{
    m_actionBindings.ResetDefaults();
    m_controlsSettings = ControlsSettings{};

    std::filesystem::create_directories("config");
    const std::filesystem::path path = std::filesystem::path("config") / "controls.json";
    if (!std::filesystem::exists(path))
    {
        return SaveControlsConfig();
    }

    std::ifstream stream(path);
    if (!stream.is_open())
    {
        m_controlsStatus = "Failed to open controls config.";
        return false;
    }

    json root;
    try
    {
        stream >> root;
    }
    catch (const std::exception&)
    {
        m_controlsStatus = "Invalid controls config JSON. Using defaults.";
        return SaveControlsConfig();
    }

    if (root.contains("survivor_sensitivity") && root["survivor_sensitivity"].is_number())
    {
        m_controlsSettings.survivorSensitivity = root["survivor_sensitivity"].get<float>();
    }
    if (root.contains("killer_sensitivity") && root["killer_sensitivity"].is_number())
    {
        m_controlsSettings.killerSensitivity = root["killer_sensitivity"].get<float>();
    }
    if (root.contains("invert_y") && root["invert_y"].is_boolean())
    {
        m_controlsSettings.invertY = root["invert_y"].get<bool>();
    }

    if (root.contains("bindings") && root["bindings"].is_object())
    {
        for (platform::InputAction action : platform::ActionBindings::AllActions())
        {
            const char* actionName = platform::ActionBindings::ActionName(action);
            if (!root["bindings"].contains(actionName))
            {
                continue;
            }
            const json& node = root["bindings"][actionName];
            if (!node.is_object())
            {
                continue;
            }
            platform::ActionBinding binding = m_actionBindings.Get(action);
            if (node.contains("primary") && node["primary"].is_number_integer())
            {
                binding.primary = node["primary"].get<int>();
            }
            if (node.contains("secondary") && node["secondary"].is_number_integer())
            {
                binding.secondary = node["secondary"].get<int>();
            }
            m_actionBindings.Set(action, binding);
        }
    }

    return true;
}

bool App::SaveControlsConfig() const
{
    std::filesystem::create_directories("config");
    const std::filesystem::path path = std::filesystem::path("config") / "controls.json";

    json root;
    root["asset_version"] = m_controlsSettings.assetVersion;
    root["survivor_sensitivity"] = m_controlsSettings.survivorSensitivity;
    root["killer_sensitivity"] = m_controlsSettings.killerSensitivity;
    root["invert_y"] = m_controlsSettings.invertY;
    root["bindings"] = json::object();
    for (platform::InputAction action : platform::ActionBindings::AllActions())
    {
        const platform::ActionBinding binding = m_actionBindings.Get(action);
        root["bindings"][platform::ActionBindings::ActionName(action)] = {
            {"primary", binding.primary},
            {"secondary", binding.secondary},
        };
    }

    std::ofstream stream(path);
    if (!stream.is_open())
    {
        return false;
    }

    stream << root.dump(2) << "\n";
    return true;
}

bool App::LoadGraphicsConfig()
{
    m_graphicsApplied = GraphicsSettings{};
    m_graphicsEditing = m_graphicsApplied;

    std::filesystem::create_directories("config");
    const std::filesystem::path path = std::filesystem::path("config") / "graphics.json";
    if (!std::filesystem::exists(path))
    {
        return SaveGraphicsConfig();
    }

    std::ifstream stream(path);
    if (!stream.is_open())
    {
        m_graphicsStatus = "Failed to open graphics config.";
        return false;
    }

    json root;
    try
    {
        stream >> root;
    }
    catch (const std::exception&)
    {
        m_graphicsStatus = "Invalid graphics JSON. Using defaults.";
        return SaveGraphicsConfig();
    }

    if (root.contains("display_mode") && root["display_mode"].is_string())
    {
        m_graphicsApplied.displayMode = DisplayModeFromText(root["display_mode"].get<std::string>());
    }
    if (root.contains("width") && root["width"].is_number_integer())
    {
        m_graphicsApplied.width = root["width"].get<int>();
    }
    if (root.contains("height") && root["height"].is_number_integer())
    {
        m_graphicsApplied.height = root["height"].get<int>();
    }
    if (root.contains("vsync") && root["vsync"].is_boolean())
    {
        m_graphicsApplied.vsync = root["vsync"].get<bool>();
    }
    if (root.contains("fps_limit") && root["fps_limit"].is_number_integer())
    {
        m_graphicsApplied.fpsLimit = root["fps_limit"].get<int>();
    }
    if (root.contains("render_mode") && root["render_mode"].is_string())
    {
        m_graphicsApplied.renderMode = RenderModeFromText(root["render_mode"].get<std::string>());
    }
    if (root.contains("shadow_quality") && root["shadow_quality"].is_number_integer())
    {
        m_graphicsApplied.shadowQuality = root["shadow_quality"].get<int>();
    }
    if (root.contains("shadow_distance") && root["shadow_distance"].is_number())
    {
        m_graphicsApplied.shadowDistance = root["shadow_distance"].get<float>();
    }
    if (root.contains("anti_aliasing") && root["anti_aliasing"].is_number_integer())
    {
        m_graphicsApplied.antiAliasing = root["anti_aliasing"].get<int>();
    }
    if (root.contains("texture_quality") && root["texture_quality"].is_number_integer())
    {
        m_graphicsApplied.textureQuality = root["texture_quality"].get<int>();
    }
    if (root.contains("fog") && root["fog"].is_boolean())
    {
        m_graphicsApplied.fogEnabled = root["fog"].get<bool>();
    }

    m_graphicsApplied.width = std::max(640, m_graphicsApplied.width);
    m_graphicsApplied.height = std::max(360, m_graphicsApplied.height);
    m_graphicsApplied.fpsLimit = std::max(0, m_graphicsApplied.fpsLimit);
    m_graphicsEditing = m_graphicsApplied;
    return true;
}

bool App::SaveGraphicsConfig() const
{
    std::filesystem::create_directories("config");
    const std::filesystem::path path = std::filesystem::path("config") / "graphics.json";

    json root;
    root["asset_version"] = m_graphicsApplied.assetVersion;
    root["display_mode"] = DisplayModeToText(m_graphicsApplied.displayMode);
    root["width"] = m_graphicsApplied.width;
    root["height"] = m_graphicsApplied.height;
    root["vsync"] = m_graphicsApplied.vsync;
    root["fps_limit"] = m_graphicsApplied.fpsLimit;
    root["render_mode"] = RenderModeToText(m_graphicsApplied.renderMode);
    root["shadow_quality"] = m_graphicsApplied.shadowQuality;
    root["shadow_distance"] = m_graphicsApplied.shadowDistance;
    root["anti_aliasing"] = m_graphicsApplied.antiAliasing;
    root["texture_quality"] = m_graphicsApplied.textureQuality;
    root["fog"] = m_graphicsApplied.fogEnabled;

    std::ofstream stream(path);
    if (!stream.is_open())
    {
        return false;
    }
    stream << root.dump(2) << "\n";
    return true;
}

bool App::LoadGameplayConfig()
{
    m_gameplayApplied = game::gameplay::GameplaySystems::GameplayTuning{};
    m_gameplayEditing = m_gameplayApplied;

    std::filesystem::create_directories("config");
    const std::filesystem::path path = std::filesystem::path("config") / "gameplay_tuning.json";
    if (!std::filesystem::exists(path))
    {
        return SaveGameplayConfig();
    }

    std::ifstream stream(path);
    if (!stream.is_open())
    {
        m_gameplayStatus = "Failed to open gameplay tuning config.";
        return false;
    }

    json root;
    try
    {
        stream >> root;
    }
    catch (const std::exception&)
    {
        m_gameplayStatus = "Invalid gameplay tuning JSON. Using defaults.";
        return SaveGameplayConfig();
    }

    auto readFloat = [&](const char* key, float& target) {
        if (root.contains(key) && root[key].is_number())
        {
            target = root[key].get<float>();
        }
    };
    auto readInt = [&](const char* key, int& target) {
        if (root.contains(key) && root[key].is_number_integer())
        {
            target = root[key].get<int>();
        }
    };

    readFloat("survivor_walk_speed", m_gameplayApplied.survivorWalkSpeed);
    readFloat("survivor_sprint_speed", m_gameplayApplied.survivorSprintSpeed);
    readFloat("survivor_crouch_speed", m_gameplayApplied.survivorCrouchSpeed);
    readFloat("survivor_crawl_speed", m_gameplayApplied.survivorCrawlSpeed);
    readFloat("killer_speed", m_gameplayApplied.killerMoveSpeed);
    readFloat("survivor_capsule_radius", m_gameplayApplied.survivorCapsuleRadius);
    readFloat("survivor_capsule_height", m_gameplayApplied.survivorCapsuleHeight);
    readFloat("killer_capsule_radius", m_gameplayApplied.killerCapsuleRadius);
    readFloat("killer_capsule_height", m_gameplayApplied.killerCapsuleHeight);
    readFloat("terror_radius", m_gameplayApplied.terrorRadiusMeters);
    readFloat("terror_radius_chase", m_gameplayApplied.terrorRadiusChaseMeters);
    readFloat("vault_slow_time", m_gameplayApplied.vaultSlowTime);
    readFloat("vault_medium_time", m_gameplayApplied.vaultMediumTime);
    readFloat("vault_fast_time", m_gameplayApplied.vaultFastTime);
    readFloat("vault_fast_dot", m_gameplayApplied.fastVaultDotThreshold);
    readFloat("vault_fast_speed_mult", m_gameplayApplied.fastVaultSpeedMultiplier);
    readFloat("vault_fast_runup", m_gameplayApplied.fastVaultMinRunup);
    readFloat("short_attack_range", m_gameplayApplied.shortAttackRange);
    readFloat("short_attack_angle_deg", m_gameplayApplied.shortAttackAngleDegrees);
    readFloat("lunge_hold_min", m_gameplayApplied.lungeHoldMinSeconds);
    readFloat("lunge_duration", m_gameplayApplied.lungeDurationSeconds);
    readFloat("lunge_recover", m_gameplayApplied.lungeRecoverSeconds);
    readFloat("short_recover", m_gameplayApplied.shortRecoverSeconds);
    readFloat("miss_recover", m_gameplayApplied.missRecoverSeconds);
    readFloat("lunge_speed_start", m_gameplayApplied.lungeSpeedStart);
    readFloat("lunge_speed_end", m_gameplayApplied.lungeSpeedEnd);
    readFloat("heal_duration", m_gameplayApplied.healDurationSeconds);
    readFloat("skillcheck_interval_min", m_gameplayApplied.skillCheckMinInterval);
    readFloat("skillcheck_interval_max", m_gameplayApplied.skillCheckMaxInterval);
    readFloat("weight_tl", m_gameplayApplied.weightTLWalls);
    readFloat("weight_jungle_long", m_gameplayApplied.weightJungleGymLong);
    readFloat("weight_jungle_short", m_gameplayApplied.weightJungleGymShort);
    readFloat("weight_shack", m_gameplayApplied.weightShack);
    readFloat("weight_fourlane", m_gameplayApplied.weightFourLane);
    readFloat("weight_filler_a", m_gameplayApplied.weightFillerA);
    readFloat("weight_filler_b", m_gameplayApplied.weightFillerB);
    readInt("max_loops", m_gameplayApplied.maxLoopsPerMap);
    readFloat("min_loop_distance_tiles", m_gameplayApplied.minLoopDistanceTiles);
    readInt("server_tick_rate", m_gameplayApplied.serverTickRate);
    readInt("interpolation_buffer_ms", m_gameplayApplied.interpolationBufferMs);

    m_gameplayEditing = m_gameplayApplied;
    return true;
}

bool App::SaveGameplayConfig() const
{
    std::filesystem::create_directories("config");
    const std::filesystem::path path = std::filesystem::path("config") / "gameplay_tuning.json";

    const auto& t = m_gameplayApplied;
    json root;
    root["asset_version"] = t.assetVersion;
    root["survivor_walk_speed"] = t.survivorWalkSpeed;
    root["survivor_sprint_speed"] = t.survivorSprintSpeed;
    root["survivor_crouch_speed"] = t.survivorCrouchSpeed;
    root["survivor_crawl_speed"] = t.survivorCrawlSpeed;
    root["killer_speed"] = t.killerMoveSpeed;
    root["survivor_capsule_radius"] = t.survivorCapsuleRadius;
    root["survivor_capsule_height"] = t.survivorCapsuleHeight;
    root["killer_capsule_radius"] = t.killerCapsuleRadius;
    root["killer_capsule_height"] = t.killerCapsuleHeight;
    root["terror_radius"] = t.terrorRadiusMeters;
    root["terror_radius_chase"] = t.terrorRadiusChaseMeters;
    root["vault_slow_time"] = t.vaultSlowTime;
    root["vault_medium_time"] = t.vaultMediumTime;
    root["vault_fast_time"] = t.vaultFastTime;
    root["vault_fast_dot"] = t.fastVaultDotThreshold;
    root["vault_fast_speed_mult"] = t.fastVaultSpeedMultiplier;
    root["vault_fast_runup"] = t.fastVaultMinRunup;
    root["short_attack_range"] = t.shortAttackRange;
    root["short_attack_angle_deg"] = t.shortAttackAngleDegrees;
    root["lunge_hold_min"] = t.lungeHoldMinSeconds;
    root["lunge_duration"] = t.lungeDurationSeconds;
    root["lunge_recover"] = t.lungeRecoverSeconds;
    root["short_recover"] = t.shortRecoverSeconds;
    root["miss_recover"] = t.missRecoverSeconds;
    root["lunge_speed_start"] = t.lungeSpeedStart;
    root["lunge_speed_end"] = t.lungeSpeedEnd;
    root["heal_duration"] = t.healDurationSeconds;
    root["skillcheck_interval_min"] = t.skillCheckMinInterval;
    root["skillcheck_interval_max"] = t.skillCheckMaxInterval;
    root["weight_tl"] = t.weightTLWalls;
    root["weight_jungle_long"] = t.weightJungleGymLong;
    root["weight_jungle_short"] = t.weightJungleGymShort;
    root["weight_shack"] = t.weightShack;
    root["weight_fourlane"] = t.weightFourLane;
    root["weight_filler_a"] = t.weightFillerA;
    root["weight_filler_b"] = t.weightFillerB;
    root["max_loops"] = t.maxLoopsPerMap;
    root["min_loop_distance_tiles"] = t.minLoopDistanceTiles;
    root["server_tick_rate"] = t.serverTickRate;
    root["interpolation_buffer_ms"] = t.interpolationBufferMs;

    std::ofstream stream(path);
    if (!stream.is_open())
    {
        return false;
    }
    stream << root.dump(2) << "\n";
    return true;
}

void App::ApplyControlsSettings()
{
    m_controlsSettings.survivorSensitivity = glm::clamp(m_controlsSettings.survivorSensitivity, 0.0001F, 0.02F);
    m_controlsSettings.killerSensitivity = glm::clamp(m_controlsSettings.killerSensitivity, 0.0001F, 0.02F);
    m_gameplay.SetLookSettings(
        m_controlsSettings.survivorSensitivity,
        m_controlsSettings.killerSensitivity,
        m_controlsSettings.invertY
    );
}

void App::ApplyGraphicsSettings(const GraphicsSettings& settings, bool startAutoConfirm)
{
    const bool modeChanged =
        m_graphicsApplied.displayMode != settings.displayMode ||
        m_graphicsApplied.width != settings.width ||
        m_graphicsApplied.height != settings.height;

    m_graphicsApplied = settings;
    m_windowSettings.width = settings.width;
    m_windowSettings.height = settings.height;
    m_windowSettings.vsync = settings.vsync;
    m_windowSettings.fpsLimit = settings.fpsLimit;

    m_vsyncEnabled = settings.vsync;
    m_window.SetVSync(m_vsyncEnabled);
    m_fpsLimit = std::max(0, settings.fpsLimit);
    m_renderer.SetRenderMode(settings.renderMode);
    m_gameplay.SetRenderModeLabel(RenderModeToText(settings.renderMode));

    platform::Window::DisplayMode windowMode = platform::Window::DisplayMode::Windowed;
    if (settings.displayMode == DisplayModeSetting::Fullscreen)
    {
        windowMode = platform::Window::DisplayMode::Fullscreen;
    }
    else if (settings.displayMode == DisplayModeSetting::Borderless)
    {
        windowMode = platform::Window::DisplayMode::Borderless;
    }
    m_window.SetDisplayMode(windowMode, settings.width, settings.height);

    if (startAutoConfirm && modeChanged)
    {
        m_graphicsAutoConfirmPending = true;
        m_graphicsAutoConfirmDeadline = glfwGetTime() + 10.0;
    }
}

void App::ApplyGameplaySettings(const game::gameplay::GameplaySystems::GameplayTuning& tuning, bool fromServer)
{
    if (!fromServer)
    {
        m_gameplayApplied = tuning;
    }
    m_gameplayEditing = tuning;
    m_gameplay.ApplyGameplayTuning(tuning);
    m_clientInterpolationBufferMs = glm::clamp(tuning.interpolationBufferMs, 50, 1000);

    const int tick = (tuning.serverTickRate <= 30) ? 30 : 60;
    m_fixedTickHz = tick;
    m_time.SetFixedDeltaSeconds(1.0 / static_cast<double>(m_fixedTickHz));
}

void App::ApplyMapEnvironment(const std::string& mapName)
{
    render::EnvironmentSettings settings{};
    std::vector<render::PointLight> pointLights;
    std::vector<render::SpotLight> spotLights;

    game::editor::MapAsset mapAsset;
    std::string error;
    if (!game::editor::LevelAssetIO::LoadMap(mapName, &mapAsset, &error))
    {
        m_renderer.SetEnvironmentSettings(settings);
        m_renderer.SetPointLights({});
        m_renderer.SetSpotLights({});
        return;
    }

    pointLights.reserve(mapAsset.lights.size());
    spotLights.reserve(mapAsset.lights.size());
    for (const game::editor::LightInstance& light : mapAsset.lights)
    {
        if (!light.enabled)
        {
            continue;
        }

        if (light.type == game::editor::LightType::Spot)
        {
            const glm::mat3 rotation = RotationMatrixFromEulerDegrees(light.rotationEuler);
            const glm::vec3 dir = glm::normalize(rotation * glm::vec3{0.0F, 0.0F, -1.0F});
            const float innerCos = std::cos(glm::radians(glm::clamp(light.spotInnerAngle, 1.0F, 89.0F)));
            const float outerCos = std::cos(glm::radians(glm::clamp(light.spotOuterAngle, light.spotInnerAngle + 0.1F, 89.5F)));
            spotLights.push_back(render::SpotLight{
                light.position,
                dir,
                glm::clamp(light.color, glm::vec3{0.0F}, glm::vec3{10.0F}),
                glm::max(0.0F, light.intensity),
                glm::max(0.1F, light.range),
                innerCos,
                outerCos,
            });
        }
        else
        {
            pointLights.push_back(render::PointLight{
                light.position,
                glm::clamp(light.color, glm::vec3{0.0F}, glm::vec3{10.0F}),
                glm::max(0.0F, light.intensity),
                glm::max(0.1F, light.range),
            });
        }
    }
    m_renderer.SetPointLights(pointLights);
    m_renderer.SetSpotLights(spotLights);

    game::editor::EnvironmentAsset envAsset;
    if (!game::editor::LevelAssetIO::LoadEnvironment(mapAsset.environmentAssetId, &envAsset, &error))
    {
        m_renderer.SetEnvironmentSettings(settings);
        return;
    }

    settings.skyEnabled = true;
    settings.skyTopColor = envAsset.skyTopColor;
    settings.skyBottomColor = envAsset.skyBottomColor;
    settings.cloudsEnabled = envAsset.cloudsEnabled;
    settings.cloudCoverage = envAsset.cloudCoverage;
    settings.cloudDensity = envAsset.cloudDensity;
    settings.cloudSpeed = envAsset.cloudSpeed;
    settings.directionalLightDirection = envAsset.directionalLightDirection;
    settings.directionalLightColor = envAsset.directionalLightColor;
    settings.directionalLightIntensity = envAsset.directionalLightIntensity;
    settings.fogEnabled = envAsset.fogEnabled;
    settings.fogColor = envAsset.fogColor;
    settings.fogDensity = envAsset.fogDensity;
    settings.fogStart = envAsset.fogStart;
    settings.fogEnd = envAsset.fogEnd;
    m_renderer.SetEnvironmentSettings(settings);
}

std::optional<int> App::CapturePressedBindCode() const
{
    for (int key = 32; key <= GLFW_KEY_LAST; ++key)
    {
        if (m_input.IsKeyPressed(key))
        {
            return key;
        }
    }

    for (int button = 0; button <= GLFW_MOUSE_BUTTON_LAST; ++button)
    {
        if (m_input.IsMousePressed(button))
        {
            return platform::ActionBindings::EncodeMouseButton(button);
        }
    }
    return std::nullopt;
}

std::vector<std::pair<int, int>> App::AvailableResolutions() const
{
    std::vector<std::pair<int, int>> modes;
    GLFWmonitor* monitor = glfwGetPrimaryMonitor();
    if (monitor == nullptr)
    {
        return modes;
    }
    int modeCount = 0;
    const GLFWvidmode* videoModes = glfwGetVideoModes(monitor, &modeCount);
    if (videoModes == nullptr || modeCount <= 0)
    {
        return modes;
    }

    for (int i = 0; i < modeCount; ++i)
    {
        const std::pair<int, int> value{videoModes[i].width, videoModes[i].height};
        if (std::find(modes.begin(), modes.end(), value) == modes.end())
        {
            modes.push_back(value);
        }
    }
    std::sort(modes.begin(), modes.end(), [](const auto& a, const auto& b) {
        const int areaA = a.first * a.second;
        const int areaB = b.first * b.second;
        if (areaA == areaB)
        {
            return a.first < b.first;
        }
        return areaA < areaB;
    });
    return modes;
}

bool App::LoadHudLayoutConfig()
{
    m_hudLayout = HudLayoutSettings{};
    std::filesystem::create_directories("ui/layouts");
    const std::filesystem::path path = std::filesystem::path("ui") / "layouts" / "hud.json";
    if (!std::filesystem::exists(path))
    {
        return false;
    }

    std::ifstream stream(path);
    if (!stream.is_open())
    {
        return false;
    }

    json root;
    try
    {
        stream >> root;
    }
    catch (const std::exception&)
    {
        return false;
    }

    m_hudLayout.assetVersion = root.value("asset_version", m_hudLayout.assetVersion);
    m_hudLayout.hudScale = root.value("hud_scale", m_hudLayout.hudScale);

    auto readVec2 = [&](const char* key, glm::vec2& target) {
        if (!root.contains(key) || !root[key].is_array() || root[key].size() != 2)
        {
            return;
        }
        target.x = root[key][0].get<float>();
        target.y = root[key][1].get<float>();
    };
    readVec2("top_left_offset", m_hudLayout.topLeftOffset);
    readVec2("top_right_offset", m_hudLayout.topRightOffset);
    readVec2("bottom_center_offset", m_hudLayout.bottomCenterOffset);
    readVec2("message_offset", m_hudLayout.messageOffset);
    m_hudLayout.hudScale = glm::clamp(m_hudLayout.hudScale, 0.5F, 3.0F);
    return true;
}

void App::DrawNetworkStatusUi(double nowSeconds)
{
#if BUILD_WITH_IMGUI
    ImGuiViewport* viewport = ImGui::GetMainViewport();

    if (!m_statusToastMessage.empty() && nowSeconds <= m_statusToastUntilSeconds)
    {
        ImGui::SetNextWindowBgAlpha(0.58F);
        ImGui::SetNextWindowPos(
            ImVec2(viewport->Pos.x + viewport->Size.x * 0.5F, viewport->Pos.y + 24.0F),
            ImGuiCond_Always,
            ImVec2(0.5F, 0.0F)
        );
        if (ImGui::Begin("NetToast", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::TextUnformatted(m_statusToastMessage.c_str());
        }
        ImGui::End();
    }

    if (m_multiplayerMode == MultiplayerMode::Host && m_appMode == AppMode::InGame)
    {
        ImGui::SetNextWindowBgAlpha(0.45F);
        ImGui::SetNextWindowPos(ImVec2(viewport->Pos.x + 10.0F, viewport->Pos.y + 220.0F), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("HostInfo", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::TextUnformatted("Hosting LAN game");
            ImGui::Text("Port: %u", m_defaultGamePort);
            if (m_localIpv4Addresses.empty())
            {
                ImGui::TextUnformatted("Local IP: unknown");
            }
            else
            {
                for (const std::string& ip : m_localIpv4Addresses)
                {
                    ImGui::Text("LAN: %s:%u", ip.c_str(), m_defaultGamePort);
                }
                ImGui::Text("Copy-ready: %s %u", m_localIpv4Addresses.front().c_str(), m_defaultGamePort);
            }
            ImGui::TextUnformatted("Friend on same network: use LAN IP");
            ImGui::TextUnformatted("Internet: requires port forwarding/VPN");
        }
        ImGui::End();
    }
#else
    (void)nowSeconds;
#endif
}

void App::DrawNetworkOverlayUi(double nowSeconds)
{
#if BUILD_WITH_IMGUI
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowBgAlpha(0.55F);
    ImGui::SetNextWindowPos(
        ImVec2(viewport->Pos.x + 10.0F, viewport->Pos.y + viewport->Size.y - 10.0F),
        ImGuiCond_FirstUseEver,
        ImVec2(0.0F, 1.0F)
    );
    if (ImGui::Begin("Network Debug (F4)", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        const net::NetworkSession::ConnectionStats stats = m_network.GetConnectionStats();
        const std::string rttText = stats.available ? (std::to_string(stats.rttMs) + " ms") : "n/a";
        const std::string lossText = stats.available ? std::to_string(stats.packetLoss) : "n/a";
        ImGui::Text("State: %s", NetworkStateToText(m_networkState).c_str());
        ImGui::Text("IsHost: %s", m_multiplayerMode == MultiplayerMode::Host ? "true" : "false");
        ImGui::Text("IsClient: %s", m_multiplayerMode == MultiplayerMode::Client ? "true" : "false");
        ImGui::Text("Server Tick: %d Hz", m_fixedTickHz);
        ImGui::Text("Client Interp Buffer: %d ms", m_clientInterpolationBufferMs);
        ImGui::Text("RTT/Ping: %s", rttText.c_str());
        ImGui::Text("Packet Loss: %s", lossText.c_str());
        ImGui::Text("Connected Peers: %u", stats.peerCount);
        ImGui::Text("Last Snapshot Rx: %.2fs ago", m_lastSnapshotReceivedSeconds > 0.0 ? nowSeconds - m_lastSnapshotReceivedSeconds : -1.0);
        ImGui::Text("Last Input Tx: %.2fs ago", m_lastInputSentSeconds > 0.0 ? nowSeconds - m_lastInputSentSeconds : -1.0);
        ImGui::Separator();
        ImGui::Text("LAN Discovery: %s",
                    m_lanDiscovery.GetMode() == net::LanDiscovery::Mode::Disabled
                        ? "OFF"
                        : (m_lanDiscovery.GetMode() == net::LanDiscovery::Mode::Host ? "HOST" : "CLIENT"));
        ImGui::Text("Discovery Port: %u", m_lanDiscovery.DiscoveryPort());
        ImGui::Text("Discovered Servers: %zu", m_lanDiscovery.Servers().size());
        ImGui::Text("Last Ping Rx: %.2fs ago",
                    m_lanDiscovery.LastResponseReceivedSeconds() > 0.0
                        ? nowSeconds - m_lanDiscovery.LastResponseReceivedSeconds()
                        : -1.0);
        ImGui::Text("Last Broadcast Tx: %.2fs ago",
                    m_lanDiscovery.LastHostBroadcastSeconds() > 0.0
                        ? nowSeconds - m_lanDiscovery.LastHostBroadcastSeconds()
                        : -1.0);
        if (m_showLanDebug)
        {
            ImGui::Separator();
            for (const auto& entry : m_lanDiscovery.Servers())
            {
                ImGui::Text("[%s] %s:%u map=%s players=%d/%d %s",
                            entry.hostName.c_str(),
                            entry.ip.c_str(),
                            entry.port,
                            entry.mapName.c_str(),
                            entry.players,
                            entry.maxPlayers,
                            entry.compatible ? "compatible" : "incompatible");
            }
        }
    }
    ImGui::End();
#else
    (void)nowSeconds;
#endif
}

void App::DrawPlayersDebugUi(double nowSeconds)
{
#if BUILD_WITH_IMGUI
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowBgAlpha(0.6F);
    ImGui::SetNextWindowPos(
        ImVec2(viewport->Pos.x + viewport->Size.x - 10.0F, viewport->Pos.y + 10.0F),
        ImGuiCond_FirstUseEver,
        ImVec2(1.0F, 0.0F)
    );

    if (ImGui::Begin("Players", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        const net::NetworkSession::ConnectionStats stats = m_network.GetConnectionStats();
        const std::vector<game::gameplay::GameplaySystems::SpawnPointInfo> spawnPoints = m_gameplay.GetSpawnPoints();

        auto drawPlayerRow = [&](PlayerBinding& player, bool localPlayer, int* spawnSelection) {
            const std::string rowName = localPlayer ? "Local Player" : "Remote Player";
            ImGui::SeparatorText(rowName.c_str());
            ImGui::Text("NetId: %u", player.netId);
            ImGui::Text("Name: %s", player.name.c_str());
            ImGui::Text("Connection: %s", player.connected ? "Connected" : "Disconnected");
            ImGui::Text("IsHost: %s", player.isHost ? "true" : "false");
            ImGui::Text("SelectedRole: %s", player.selectedRole.c_str());
            ImGui::Text("ControlledRole: %s", player.controlledRole.c_str());

            const bool hasPawn = player.controlledRole == "survivor" || player.controlledRole == "killer";
            const engine::scene::Entity pawnEntity = hasPawn ? m_gameplay.RoleEntity(player.controlledRole) : 0;
            const glm::vec3 pawnPos = hasPawn ? m_gameplay.RolePosition(player.controlledRole) : glm::vec3{0.0F};
            const std::string healthState = hasPawn && player.controlledRole == "survivor" ? m_gameplay.SurvivorHealthStateText() : "N/A";
            const std::string movementState = hasPawn ? m_gameplay.MovementStateForRole(player.controlledRole) : "None";
            const std::string rttText = stats.available ? (std::to_string(stats.rttMs) + " ms") : "n/a";

            ImGui::Text("ControlledPawn: %s", hasPawn ? player.controlledRole.c_str() : "None");
            ImGui::Text("Pawn Entity: %u", static_cast<unsigned int>(pawnEntity));
            ImGui::Text("Pawn Position: (%.2f, %.2f, %.2f)", pawnPos.x, pawnPos.y, pawnPos.z);
            ImGui::Text("HealthState: %s", healthState.c_str());
            ImGui::Text("MovementState: %s", movementState.c_str());
            ImGui::Text("Ping/RTT: %s", rttText.c_str());
            ImGui::Text(
                "Last input: %.2fs ago",
                player.lastInputSeconds > 0.0 ? std::max(0.0, nowSeconds - player.lastInputSeconds) : -1.0
            );
            ImGui::Text(
                "Last snapshot: %.2fs ago",
                player.lastSnapshotSeconds > 0.0 ? std::max(0.0, nowSeconds - player.lastSnapshotSeconds) : -1.0
            );

            if (m_multiplayerMode == MultiplayerMode::Host)
            {
                if (ImGui::Button(localPlayer ? "Set Survivor##local" : "Set Survivor##remote"))
                {
                    RequestRoleChange("survivor", !localPlayer);
                }
                ImGui::SameLine();
                if (ImGui::Button(localPlayer ? "Set Killer##local" : "Set Killer##remote"))
                {
                    RequestRoleChange("killer", !localPlayer);
                }
                if (hasPawn && ImGui::Button(localPlayer ? "Force Respawn##local" : "Force Respawn##remote"))
                {
                    const bool ok = m_gameplay.RespawnRole(player.controlledRole);
                    AppendNetworkLog(
                        std::string("Force respawn ") + (localPlayer ? "local" : "remote") + " role=" + player.controlledRole + " result=" +
                        (ok ? "ok" : "fail")
                    );
                }

                if (hasPawn && !spawnPoints.empty())
                {
                    if (*spawnSelection == 0)
                    {
                        *spawnSelection = spawnPoints.front().id;
                    }

                    const std::string preview = "Spawn #" + std::to_string(*spawnSelection);
                    if (ImGui::BeginCombo(localPlayer ? "Spawn Target##local" : "Spawn Target##remote", preview.c_str()))
                    {
                        for (const auto& spawn : spawnPoints)
                        {
                            const bool selected = (*spawnSelection == spawn.id);
                            std::ostringstream label;
                            label << "#" << spawn.id << " " << (spawn.type == game::gameplay::GameplaySystems::SpawnPointType::Survivor
                                                                      ? "Survivor"
                                                                      : (spawn.type == game::gameplay::GameplaySystems::SpawnPointType::Killer ? "Killer" : "Generic"));
                            if (ImGui::Selectable(label.str().c_str(), selected))
                            {
                                *spawnSelection = spawn.id;
                            }
                            if (selected)
                            {
                                ImGui::SetItemDefaultFocus();
                            }
                        }
                        ImGui::EndCombo();
                    }
                    if (ImGui::Button(localPlayer ? "Teleport Pawn To Spawn##local" : "Teleport Pawn To Spawn##remote"))
                    {
                        const bool ok = m_gameplay.SpawnRoleAt(player.controlledRole, *spawnSelection);
                        AppendNetworkLog(
                            std::string("Teleport via respawn ") + (localPlayer ? "local" : "remote") + " role=" + player.controlledRole +
                            " spawnId=" + std::to_string(*spawnSelection) + " result=" + (ok ? "ok" : "fail")
                        );
                    }
                }
            }
        };

        drawPlayerRow(m_localPlayer, true, &m_playersDebugSpawnSelectionLocal);
        if (m_remotePlayer.connected || m_multiplayerMode == MultiplayerMode::Client)
        {
            drawPlayerRow(m_remotePlayer, false, &m_playersDebugSpawnSelectionRemote);
        }
    }
    ImGui::End();
#else
    (void)nowSeconds;
#endif
}

void App::DrawMainMenuUiCustom(bool* shouldQuit)
{
    const std::vector<std::string> roleItems{"Survivor", "Killer"};
    const std::vector<std::string> mapItems{"Test", "Collision Test", "Random Generation"};
    const std::vector<std::string> savedMaps = game::editor::LevelAssetIO::ListMapNames();
    if (m_menuSavedMapIndex >= static_cast<int>(savedMaps.size()))
    {
        m_menuSavedMapIndex = savedMaps.empty() ? -1 : 0;
    }
    if (m_menuSavedMapIndex < 0 && !savedMaps.empty())
    {
        m_menuSavedMapIndex = 0;
    }

    const float scale = m_ui.Scale();
    const float screenW = static_cast<float>(m_ui.ScreenWidth());
    const float screenH = static_cast<float>(m_ui.ScreenHeight());
    const float gap = 12.0F * scale;
    const float marginX = 24.0F * scale;
    const float marginTop = 60.0F * scale;
    const float marginBottom = 60.0F * scale;

    // Left panel: Game Menu (centered, fixed width)
    const float leftPanelW = std::min(420.0F * scale, screenW - marginX * 2.0F - 280.0F * scale - gap);
    const float leftPanelH = screenH - marginTop - marginBottom;
    const float leftPanelX = (screenW - leftPanelW - 280.0F * scale - gap) * 0.5F;
    const engine::ui::UiRect leftPanel{
        leftPanelX,
        marginTop,
        leftPanelW,
        leftPanelH,
    };

    // Right panel: Dev Tools (fixed compact width)
    const float rightPanelW = 280.0F * scale;
    const float rightPanelH = leftPanelH;
    const engine::ui::UiRect rightPanel{
        leftPanel.x + leftPanelW + gap,
        marginTop,
        rightPanelW,
        rightPanelH,
    };

    // ==================== LEFT PANEL: Game Menu (DBD Style) ====================
    m_ui.BeginRootPanel("main_menu_game", leftPanel, true);
    m_ui.Label("THE GAME", 1.6F);
    m_ui.Spacer(4.0F * scale);
    m_ui.Label("Asymmetric Horror Prototype", m_ui.Theme().colorTextMuted);

    m_ui.Spacer(24.0F * scale);

    // Session settings
    m_ui.Dropdown("menu_role", "Role", &m_menuRoleIndex, roleItems);
    m_ui.Dropdown("menu_map", "Map", &m_menuMapIndex, mapItems);

    const std::string roleName = RoleNameFromIndex(m_menuRoleIndex);
    const std::string mapName = MapNameFromIndex(m_menuMapIndex);

    m_ui.Spacer(12.0F * scale);
    if (m_ui.Button("play_solo", "PLAY", true, &m_ui.Theme().colorAccent))
    {
        StartSoloSession(mapName, roleName);
    }
    if (m_ui.Button("enter_lobby", "LOBBY (3D)"))
    {
        m_appMode = AppMode::Lobby;
        game::ui::LobbyPlayer localPlayer;
        localPlayer.netId = 1;
        localPlayer.name = "Player";
        localPlayer.selectedRole = roleName;
        localPlayer.isHost = true;
        localPlayer.isConnected = true;
        m_lobbyScene.SetPlayers({localPlayer});
        m_lobbyScene.SetLocalPlayerRole(roleName);
        m_lobbyScene.SetLocalPlayerPerks({m_menuSurvivorPerks[0], m_menuSurvivorPerks[1], m_menuSurvivorPerks[2], m_menuSurvivorPerks[3]});
        m_lobbyScene.EnterLobby();
    }

    if (!savedMaps.empty())
    {
        m_ui.Spacer(8.0F * scale);
        m_ui.Dropdown("saved_maps", "Saved Map", &m_menuSavedMapIndex, savedMaps);
        if (m_ui.Button("play_saved", "PLAY SAVED"))
        {
            StartSoloSession(savedMaps[static_cast<std::size_t>(m_menuSavedMapIndex)], roleName);
        }
    }

    m_ui.Spacer(20.0F * scale);
    m_ui.Label("MULTIPLAYER", m_ui.Theme().colorTextMuted);

    std::string portText = std::to_string(m_menuPort);
    if (m_ui.InputText("menu_port", "Port", &portText, 6))
    {
        try
        {
            m_menuPort = std::clamp(std::stoi(portText), 1, 65535);
        }
        catch (const std::exception&)
        {
            m_menuPort = std::clamp(m_menuPort, 1, 65535);
        }
    }
    m_ui.InputText("menu_join_ip", "Join IP", &m_menuJoinIp, 63);

    m_ui.Spacer(8.0F * scale);
    if (m_ui.Button("host_btn", "HOST GAME"))
    {
        // Host goes to lobby
        m_appMode = AppMode::Lobby;
        game::ui::LobbyPlayer localPlayer;
        localPlayer.netId = 1;
        localPlayer.name = "Host";
        localPlayer.selectedRole = roleName;
        localPlayer.isHost = true;
        localPlayer.isConnected = true;
        
        // Set available perks based on role
        const bool isSurvivor = (roleName == "survivor");
        const auto& perkSystem = m_gameplay.GetPerkSystem();
        const auto availablePerks = isSurvivor 
            ? perkSystem.ListPerks(game::gameplay::perks::PerkRole::Survivor)
            : perkSystem.ListPerks(game::gameplay::perks::PerkRole::Killer);
        std::vector<std::string> perkIds = availablePerks;
        std::vector<std::string> perkNames;
        for (const auto& id : availablePerks)
        {
            const auto* perk = perkSystem.GetPerk(id);
            perkNames.push_back(perk ? perk->name : id);
        }
        m_lobbyScene.SetAvailablePerks(perkIds, perkNames);
        
        m_lobbyScene.SetPlayers({localPlayer});
        m_lobbyScene.SetLocalPlayerRole(roleName);
        m_lobbyScene.SetLocalPlayerPerks(isSurvivor 
            ? std::array<std::string, 4>{m_menuSurvivorPerks[0], m_menuSurvivorPerks[1], m_menuSurvivorPerks[2], m_menuSurvivorPerks[3]}
            : std::array<std::string, 4>{m_menuKillerPerks[0], m_menuKillerPerks[1], m_menuKillerPerks[2], m_menuKillerPerks[3]});
        m_lobbyScene.EnterLobby();
    }
    if (m_ui.Button("join_btn", "JOIN GAME"))
    {
        // Join goes to lobby (simulated for now)
        m_appMode = AppMode::Lobby;
        game::ui::LobbyPlayer hostPlayer;
        hostPlayer.netId = 1;
        hostPlayer.name = "Host";
        hostPlayer.selectedRole = (roleName == "survivor") ? "killer" : "survivor";
        hostPlayer.isHost = true;
        hostPlayer.isConnected = true;
        
        game::ui::LobbyPlayer localPlayer;
        localPlayer.netId = 2;
        localPlayer.name = "Player";
        localPlayer.selectedRole = roleName;
        localPlayer.isHost = false;
        localPlayer.isConnected = true;
        
        // Set available perks based on role
        const bool isSurvivor = (roleName == "survivor");
        const auto& perkSystem = m_gameplay.GetPerkSystem();
        const auto availablePerks = isSurvivor 
            ? perkSystem.ListPerks(game::gameplay::perks::PerkRole::Survivor)
            : perkSystem.ListPerks(game::gameplay::perks::PerkRole::Killer);
        std::vector<std::string> perkIds = availablePerks;
        std::vector<std::string> perkNames;
        for (const auto& id : availablePerks)
        {
            const auto* perk = perkSystem.GetPerk(id);
            perkNames.push_back(perk ? perk->name : id);
        }
        m_lobbyScene.SetAvailablePerks(perkIds, perkNames);
        
        m_lobbyScene.SetPlayers({hostPlayer, localPlayer});
        m_lobbyScene.SetLocalPlayerRole(roleName);
        m_lobbyScene.SetLocalPlayerPerks(isSurvivor 
            ? std::array<std::string, 4>{m_menuSurvivorPerks[0], m_menuSurvivorPerks[1], m_menuSurvivorPerks[2], m_menuSurvivorPerks[3]}
            : std::array<std::string, 4>{m_menuKillerPerks[0], m_menuKillerPerks[1], m_menuKillerPerks[2], m_menuKillerPerks[3]});
        m_lobbyScene.EnterLobby();
    }

    m_ui.Spacer(20.0F * scale);
    m_ui.Label("EDITORS", m_ui.Theme().colorTextMuted);
    if (m_ui.Button("level_editor", "LEVEL EDITOR"))
    {
        m_lanDiscovery.Stop();
        m_network.Disconnect();
        m_gameplay.SetNetworkAuthorityMode(false);
        m_gameplay.ClearRemoteRoleCommands();
        m_multiplayerMode = MultiplayerMode::Solo;
        m_pauseMenuOpen = false;
        m_appMode = AppMode::Editor;
        m_levelEditor.Enter(game::editor::LevelEditor::Mode::MapEditor);
        m_menuNetStatus = "Entered Level Editor";
        TransitionNetworkState(NetworkState::Offline, "Editor mode");
    }
    if (m_ui.Button("loop_editor", "LOOP EDITOR"))
    {
        m_lanDiscovery.Stop();
        m_network.Disconnect();
        m_gameplay.SetNetworkAuthorityMode(false);
        m_gameplay.ClearRemoteRoleCommands();
        m_multiplayerMode = MultiplayerMode::Solo;
        m_pauseMenuOpen = false;
        m_appMode = AppMode::Editor;
        m_levelEditor.Enter(game::editor::LevelEditor::Mode::LoopEditor);
        m_menuNetStatus = "Entered Loop Editor";
        TransitionNetworkState(NetworkState::Offline, "Editor mode");
    }

    m_ui.Spacer(20.0F * scale);
    if (m_ui.Button("menu_settings", "SETTINGS"))
    {
        m_settingsMenuOpen = true;
        m_settingsOpenedFromPause = false;
    }

    m_ui.Spacer(20.0F * scale);
    if (m_ui.Button("quit_game", "EXIT", true, &m_ui.Theme().colorDanger))
    {
        *shouldQuit = true;
    }

    m_ui.EndPanel();

    // ==================== RIGHT PANEL: Dev Tools (Compact) ====================
    m_ui.BeginRootPanel("main_menu_dev", rightPanel, true);
    m_ui.Label("DEV", 1.1F);

    m_ui.Spacer(8.0F * scale);
    if (m_ui.Button("toggle_ui_test", std::string("UI Test: ") + (m_showUiTestPanel ? "ON" : "OFF")))
    {
        m_showUiTestPanel = !m_showUiTestPanel;
    }
    if (m_ui.Button("toggle_loading_test", std::string("Loading: ") + (m_showLoadingScreenTestPanel ? "ON" : "OFF")))
    {
        m_showLoadingScreenTestPanel = !m_showLoadingScreenTestPanel;
    }
    m_ui.Checkbox("loading_on_join", "Loading on join", &m_showConnectingLoading);

    m_ui.Spacer(10.0F * scale);
    m_ui.Label("LAN", m_ui.Theme().colorTextMuted, 0.9F);
    if (m_ui.Button("refresh_lan", "REFRESH"))
    {
        m_lanDiscovery.ForceScan();
    }

    const auto& servers = m_lanDiscovery.Servers();
    if (servers.empty())
    {
        m_ui.Label("No games found", m_ui.Theme().colorTextMuted, 0.85F);
    }
    else
    {
        for (std::size_t i = 0; i < servers.size() && i < 3; ++i)
        {
            const auto& entry = servers[i];
            const bool canJoin = entry.compatible && entry.players < entry.maxPlayers;
            m_ui.Label(entry.hostName, canJoin ? m_ui.Theme().colorText : m_ui.Theme().colorTextMuted, 0.9F);
            m_ui.PushIdScope("lan_" + std::to_string(i));
            if (m_ui.Button("join_lan", "JOIN", canJoin))
            {
                StartJoinSession(entry.ip, entry.port, roleName);
            }
            m_ui.PopIdScope();
        }
        if (servers.size() > 3)
        {
            m_ui.Label("+" + std::to_string(servers.size() - 3) + " more...", m_ui.Theme().colorTextMuted, 0.8F);
        }
    }

    m_ui.Spacer(10.0F * scale);
    m_ui.Label(NetworkStateToText(m_networkState), m_ui.Theme().colorTextMuted, 0.85F);

    m_ui.Spacer(12.0F * scale);
    
    // Get available perks based on selected role
    const auto& perkSystem = m_gameplay.GetPerkSystem();
    const bool isSurvivor = (m_menuRoleIndex == 0);
    const auto survivorPerks = perkSystem.ListPerks(game::gameplay::perks::PerkRole::Survivor);
    const auto killerPerks = perkSystem.ListPerks(game::gameplay::perks::PerkRole::Killer);
    const auto& availablePerks = isSurvivor ? survivorPerks : killerPerks;
    auto& selectedPerks = isSurvivor ? m_menuSurvivorPerks : m_menuKillerPerks;
    
    m_ui.Label(isSurvivor ? "SURVIVOR PERKS" : "KILLER PERKS", m_ui.Theme().colorTextMuted, 0.9F);
    
    // Ensure 4 slots
    if (selectedPerks.size() < 4)
    {
        selectedPerks.resize(4, "");
    }
    
    // Show 4 perk slots (like in-game HUD)
    for (int slot = 0; slot < 4; ++slot)
    {
        const std::string slotLabel = "Slot " + std::to_string(slot + 1);
        
        // Build perk names list with "None" as first option
        std::vector<std::string> perkNames{"None"};
        for (const auto& id : availablePerks)
        {
            const auto* perk = perkSystem.GetPerk(id);
            perkNames.push_back(perk ? perk->name : id);
        }
        
        // Map selected index
        int selectedIndex = 0;
        if (slot < static_cast<int>(selectedPerks.size()) && !selectedPerks[slot].empty())
        {
            const auto& perkId = selectedPerks[slot];
            const auto* perk = perkSystem.GetPerk(perkId);
            const std::string perkName = perk ? perk->name : perkId;
            for (std::size_t i = 0; i < availablePerks.size(); ++i)
            {
                const auto* p = perkSystem.GetPerk(availablePerks[i]);
                if ((p && p->name == perkName) || availablePerks[i] == perkId)
                {
                    selectedIndex = static_cast<int>(i + 1); // +1 for "None"
                    break;
                }
            }
        }
        
        m_ui.PushIdScope("perk_slot_" + std::to_string(slot));
        if (m_ui.Dropdown("perk", slotLabel.c_str(), &selectedIndex, perkNames))
        {
            if (selectedIndex == 0)
            {
                // "None" selected
                selectedPerks[slot] = "";
            }
            else if (selectedIndex > 0 && static_cast<std::size_t>(selectedIndex - 1) < availablePerks.size())
            {
                // Perk selected
                const std::string perkId = availablePerks[static_cast<std::size_t>(selectedIndex - 1)];
                selectedPerks[slot] = perkId;
            }
            
            // Update loadout in PerkSystem (based on role)
            game::gameplay::perks::PerkLoadout loadout;
            for (std::size_t i = 0; i < selectedPerks.size() && i < 4; ++i)
            {
                if (!selectedPerks[i].empty())
                {
                    loadout.SetPerk(static_cast<int>(i), selectedPerks[i]);
                }
            }
            if (isSurvivor)
            {
                m_gameplay.SetSurvivorPerkLoadout(loadout);
            }
            else
            {
                m_gameplay.SetKillerPerkLoadout(loadout);
            }
        }
        m_ui.PopIdScope();
    }

    m_ui.Spacer(10.0F * scale);
    m_ui.Label("~ Console | F6 UI", m_ui.Theme().colorTextMuted, 0.8F);
    m_ui.Label("F7 Load", m_ui.Theme().colorTextMuted, 0.8F);

    m_ui.EndPanel();
}

void App::DrawPauseMenuUiCustom(bool* closePauseMenu, bool* backToMenu, bool* shouldQuit)
{
    const float scale = m_ui.Scale();
    const float panelW = std::min(460.0F * scale, static_cast<float>(m_ui.ScreenWidth()) - 20.0F);
    const float panelH = std::min(360.0F * scale, static_cast<float>(m_ui.ScreenHeight()) - 20.0F);
    const engine::ui::UiRect panel{
        (static_cast<float>(m_ui.ScreenWidth()) - panelW) * 0.5F,
        (static_cast<float>(m_ui.ScreenHeight()) - panelH) * 0.5F,
        panelW,
        panelH,
    };

    m_ui.BeginRootPanel("pause_menu_custom", panel, true);
    m_ui.Label("Pause Menu", 1.15F);
    if (m_ui.Button("resume_btn", "Resume", true, &m_ui.Theme().colorSuccess))
    {
        *closePauseMenu = true;
    }
    if (m_ui.Button("settings_btn", "Settings"))
    {
        m_settingsMenuOpen = true;
        m_settingsOpenedFromPause = true;
    }
    if (m_ui.Button("back_to_main_btn", "Return to Main Menu"))
    {
        *backToMenu = true;
    }
    if (m_ui.Button("quit_from_pause_btn", "Quit", true, &m_ui.Theme().colorDanger))
    {
        *shouldQuit = true;
    }
    m_ui.EndPanel();
}

void App::DrawSettingsUiCustom(bool* closeSettings)
{
    if (m_input.IsKeyPressed(GLFW_KEY_ESCAPE))
    {
        *closeSettings = false;
        return;
    }

    if (m_rebindWaiting)
    {
        if (m_input.IsKeyPressed(GLFW_KEY_ESCAPE))
        {
            m_rebindWaiting = false;
            m_controlsStatus = "Rebind cancelled.";
        }
        else if (const std::optional<int> captured = CapturePressedBindCode(); captured.has_value())
        {
            const auto conflict = m_actionBindings.FindConflict(*captured, m_rebindAction, m_rebindSlot);
            if (conflict.has_value())
            {
                m_rebindConflictAction = conflict->first;
                m_rebindConflictSlot = conflict->second;
                m_rebindCapturedCode = *captured;
                m_rebindConflictPopup = true;
            }
            else
            {
                m_actionBindings.SetCode(m_rebindAction, m_rebindSlot, *captured);
                m_rebindWaiting = false;
                m_controlsStatus = "Rebound " + std::string(platform::ActionBindings::ActionLabel(m_rebindAction));
                (void)SaveControlsConfig();
            }
        }
    }

    const float scale = m_ui.Scale();
    const float panelW = std::min(980.0F * scale, static_cast<float>(m_ui.ScreenWidth()) - 20.0F);
    const float panelH = std::min(760.0F * scale, static_cast<float>(m_ui.ScreenHeight()) - 20.0F);
    const engine::ui::UiRect panel{
        (static_cast<float>(m_ui.ScreenWidth()) - panelW) * 0.5F,
        (static_cast<float>(m_ui.ScreenHeight()) - panelH) * 0.5F,
        panelW,
        panelH,
    };
    m_ui.BeginRootPanel("settings_custom", panel, true);
    m_ui.PushLayout(engine::ui::UiSystem::LayoutAxis::Horizontal, 8.0F, 0.0F);
    m_ui.Label("Settings", 1.2F, 320.0F);
    if (m_ui.Button("settings_close_top", "Close", true, &m_ui.Theme().colorDanger, 140.0F))
    {
        *closeSettings = false;
    }
    m_ui.PopLayout();
    m_ui.Label("Tabs + scroll region. Use drag scrollbar on the right in long sections.", m_ui.Theme().colorTextMuted);

    m_settingsTabIndex = glm::clamp(m_settingsTabIndex, 0, 2);
    m_ui.PushLayout(engine::ui::UiSystem::LayoutAxis::Horizontal, 8.0F, 0.0F);
    {
        const glm::vec4 tabColor = m_ui.Theme().colorAccent;
        if (m_ui.Button("tab_controls", "Controls", true, m_settingsTabIndex == 0 ? &tabColor : nullptr, 200.0F))
        {
            m_settingsTabIndex = 0;
        }
        if (m_ui.Button("tab_graphics", "Graphics", true, m_settingsTabIndex == 1 ? &tabColor : nullptr, 200.0F))
        {
            m_settingsTabIndex = 1;
        }
        if (m_ui.Button("tab_gameplay", "Gameplay", true, m_settingsTabIndex == 2 ? &tabColor : nullptr, 200.0F))
        {
            m_settingsTabIndex = 2;
        }
    }
    m_ui.PopLayout();

    const float scrollHeight = std::max(240.0F * scale, m_ui.CurrentContentRect().h - 85.0F * scale);
    m_ui.BeginScrollRegion("settings_scroll_region", scrollHeight, &m_settingsTabScroll[static_cast<std::size_t>(m_settingsTabIndex)]);

    if (m_settingsTabIndex == 0)
    {
        m_ui.Label("Action Mappings", m_ui.Theme().colorAccent);
        if (m_rebindWaiting)
        {
            m_ui.Label("Press key/mouse to rebind. ESC cancels.", m_ui.Theme().colorAccent);
        }

        for (platform::InputAction action : platform::ActionBindings::AllActions())
        {
            const platform::ActionBinding binding = m_actionBindings.Get(action);
            m_ui.PushIdScope(platform::ActionBindings::ActionName(action));
            m_ui.PushLayout(engine::ui::UiSystem::LayoutAxis::Horizontal, 8.0F, 0.0F);
            m_ui.Label(platform::ActionBindings::ActionLabel(action), m_ui.Theme().colorText, 1.0F, 240.0F);
            if (platform::ActionBindings::IsRebindable(action))
            {
                if (m_ui.Button("rebind_primary", "Primary: " + platform::ActionBindings::CodeToLabel(binding.primary), true, nullptr, 230.0F))
                {
                    m_rebindWaiting = true;
                    m_rebindAction = action;
                    m_rebindSlot = 0;
                }
                if (m_ui.Button("rebind_secondary", "Secondary: " + platform::ActionBindings::CodeToLabel(binding.secondary), true, nullptr, 230.0F))
                {
                    m_rebindWaiting = true;
                    m_rebindAction = action;
                    m_rebindSlot = 1;
                }
            }
            else
            {
                m_ui.Label("Fixed: " + platform::ActionBindings::CodeToLabel(binding.primary), m_ui.Theme().colorTextMuted, 1.0F, 460.0F);
            }
            m_ui.PopLayout();
            m_ui.PopIdScope();
        }

        if (m_rebindConflictPopup)
        {
            m_ui.Label("Binding conflict detected.", m_ui.Theme().colorDanger);
            m_ui.PushLayout(engine::ui::UiSystem::LayoutAxis::Horizontal, 8.0F, 0.0F);
            if (m_ui.Button("conflict_override", "Override", true, &m_ui.Theme().colorDanger, 180.0F))
            {
                m_actionBindings.SetCode(m_rebindConflictAction, m_rebindConflictSlot, platform::ActionBindings::kUnbound);
                m_actionBindings.SetCode(m_rebindAction, m_rebindSlot, m_rebindCapturedCode);
                m_rebindWaiting = false;
                m_rebindConflictPopup = false;
                (void)SaveControlsConfig();
            }
            if (m_ui.Button("conflict_cancel", "Cancel", true, nullptr, 180.0F))
            {
                m_rebindConflictPopup = false;
                m_rebindWaiting = false;
            }
            m_ui.PopLayout();
        }

        bool changed = false;
        changed |= m_ui.SliderFloat("survivor_sens", "Survivor Sensitivity", &m_controlsSettings.survivorSensitivity, 0.0002F, 0.01F, "%.4f");
        changed |= m_ui.SliderFloat("killer_sens", "Killer Sensitivity", &m_controlsSettings.killerSensitivity, 0.0002F, 0.01F, "%.4f");
        changed |= m_ui.Checkbox("invert_y_toggle", "Invert Y", &m_controlsSettings.invertY);
        if (changed)
        {
            ApplyControlsSettings();
        }
        if (m_ui.Button("save_controls_btn", "Save Controls", true, &m_ui.Theme().colorSuccess))
        {
            ApplyControlsSettings();
            m_controlsStatus = SaveControlsConfig() ? "Saved controls config." : "Failed to save controls config.";
        }
        if (!m_controlsStatus.empty())
        {
            m_ui.Label(m_controlsStatus, m_ui.Theme().colorTextMuted);
        }
    }
    else if (m_settingsTabIndex == 1)
    {
        std::vector<std::string> displayModes{"Windowed", "Fullscreen", "Borderless"};
        int displayMode = static_cast<int>(m_graphicsEditing.displayMode);
        if (m_ui.Dropdown("display_mode_dd", "Display Mode", &displayMode, displayModes))
        {
            m_graphicsEditing.displayMode = static_cast<DisplayModeSetting>(glm::clamp(displayMode, 0, 2));
        }
        m_ui.Checkbox("vsync_chk", "VSync", &m_graphicsEditing.vsync);
        m_ui.SliderInt("fps_limit_slider", "FPS Limit", &m_graphicsEditing.fpsLimit, 0, 240);
        std::vector<std::string> renderModes{"Wireframe", "Filled"};
        int renderMode = m_graphicsEditing.renderMode == render::RenderMode::Wireframe ? 0 : 1;
        if (m_ui.Dropdown("render_mode_dd", "Render Mode", &renderMode, renderModes))
        {
            m_graphicsEditing.renderMode = renderMode == 0 ? render::RenderMode::Wireframe : render::RenderMode::Filled;
        }
        if (m_ui.Button("apply_graphics_btn", "Apply Graphics", true, &m_ui.Theme().colorSuccess))
        {
            m_graphicsRollback = m_graphicsApplied;
            ApplyGraphicsSettings(m_graphicsEditing, true);
            m_graphicsStatus = SaveGraphicsConfig() ? "Graphics applied and saved." : "Graphics applied, but save failed.";
        }
        if (!m_graphicsStatus.empty())
        {
            m_ui.Label(m_graphicsStatus, m_ui.Theme().colorTextMuted);
        }
    }
    else
    {
        const bool allowEdit = m_multiplayerMode != MultiplayerMode::Client;
        if (!allowEdit)
        {
            m_ui.Label("Read-only on clients. Host values are authoritative.", m_ui.Theme().colorDanger);
        }
        auto& t = m_gameplayEditing;
        m_ui.Label("Config Actions", m_ui.Theme().colorAccent);
        m_ui.PushLayout(engine::ui::UiSystem::LayoutAxis::Horizontal, 8.0F, 0.0F);
        if (m_ui.Button("apply_gameplay_btn", "Apply", allowEdit, &m_ui.Theme().colorSuccess, 165.0F))
        {
            ApplyGameplaySettings(m_gameplayEditing, false);
            if (m_multiplayerMode == MultiplayerMode::Host)
            {
                SendGameplayTuningToClient();
            }
            m_gameplayStatus = "Gameplay tuning applied.";
        }
        if (m_ui.Button("save_gameplay_btn", "Save To File", allowEdit, nullptr, 165.0F))
        {
            const auto previousApplied = m_gameplayApplied;
            m_gameplayApplied = m_gameplayEditing;
            const bool saved = SaveGameplayConfig();
            m_gameplayApplied = previousApplied;
            m_gameplayStatus = saved ? "Saved to config/gameplay_tuning.json." : "Failed to save gameplay tuning file.";
        }
        if (m_ui.Button("load_gameplay_btn", "Load From File", true, nullptr, 165.0F))
        {
            if (LoadGameplayConfig())
            {
                if (allowEdit)
                {
                    ApplyGameplaySettings(m_gameplayEditing, false);
                    if (m_multiplayerMode == MultiplayerMode::Host)
                    {
                        SendGameplayTuningToClient();
                    }
                }
                m_gameplayStatus = allowEdit ? "Loaded from file and applied." : "Loaded local values (client read-only).";
            }
            else
            {
                m_gameplayStatus = "Failed to load config/gameplay_tuning.json.";
            }
        }
        if (m_ui.Button("defaults_gameplay_btn", "Set Defaults", allowEdit, &m_ui.Theme().colorDanger, 165.0F))
        {
            m_gameplayEditing = game::gameplay::GameplaySystems::GameplayTuning{};
            ApplyGameplaySettings(m_gameplayEditing, false);
            if (m_multiplayerMode == MultiplayerMode::Host)
            {
                SendGameplayTuningToClient();
            }
            m_gameplayStatus = "Defaults applied. Use Save To File to persist.";
        }
        m_ui.PopLayout();

        m_ui.Label("Movement", m_ui.Theme().colorAccent);
        m_ui.SliderFloat("gp_surv_walk", "Survivor Walk", &t.survivorWalkSpeed, 0.5F, 8.0F, "%.2f");
        m_ui.SliderFloat("gp_surv_sprint", "Survivor Sprint", &t.survivorSprintSpeed, 0.5F, 10.0F, "%.2f");
        m_ui.SliderFloat("gp_surv_crouch", "Survivor Crouch", &t.survivorCrouchSpeed, 0.1F, 5.0F, "%.2f");
        m_ui.SliderFloat("gp_surv_crawl", "Survivor Crawl", &t.survivorCrawlSpeed, 0.1F, 3.0F, "%.2f");
        m_ui.SliderFloat("gp_killer_speed", "Killer Speed", &t.killerMoveSpeed, 0.5F, 12.0F, "%.2f");

        m_ui.Label("Capsules", m_ui.Theme().colorAccent);
        m_ui.SliderFloat("gp_surv_radius", "Survivor Radius", &t.survivorCapsuleRadius, 0.2F, 1.2F, "%.2f");
        m_ui.SliderFloat("gp_surv_height", "Survivor Height", &t.survivorCapsuleHeight, 0.9F, 3.0F, "%.2f");
        m_ui.SliderFloat("gp_killer_radius", "Killer Radius", &t.killerCapsuleRadius, 0.2F, 1.2F, "%.2f");
        m_ui.SliderFloat("gp_killer_height", "Killer Height", &t.killerCapsuleHeight, 0.9F, 3.0F, "%.2f");

        m_ui.Label("Vault + Terror Radius", m_ui.Theme().colorAccent);
        m_ui.SliderFloat("gp_terror", "Terror Radius", &t.terrorRadiusMeters, 4.0F, 80.0F, "%.1f");
        m_ui.SliderFloat("gp_terror_chase", "Terror Radius Chase", &t.terrorRadiusChaseMeters, 4.0F, 96.0F, "%.1f");
        m_ui.SliderFloat("gp_slow_vault", "Slow Vault", &t.vaultSlowTime, 0.2F, 1.6F, "%.2f");
        m_ui.SliderFloat("gp_medium_vault", "Medium Vault", &t.vaultMediumTime, 0.2F, 1.2F, "%.2f");
        m_ui.SliderFloat("gp_fast_vault", "Fast Vault", &t.vaultFastTime, 0.15F, 1.0F, "%.2f");
        m_ui.SliderFloat("gp_fast_vault_dot", "Fast Vault Dot", &t.fastVaultDotThreshold, 0.3F, 0.99F, "%.2f");
        m_ui.SliderFloat("gp_fast_vault_speed", "Fast Vault Speed Mult", &t.fastVaultSpeedMultiplier, 0.3F, 1.2F, "%.2f");
        m_ui.SliderFloat("gp_fast_vault_runup", "Fast Vault Runup", &t.fastVaultMinRunup, 0.1F, 4.0F, "%.2f");

        m_ui.Label("Combat", m_ui.Theme().colorAccent);
        m_ui.SliderFloat("gp_short_range", "Short Attack Range", &t.shortAttackRange, 0.5F, 6.0F, "%.2f");
        m_ui.SliderFloat("gp_short_angle", "Short Attack Angle", &t.shortAttackAngleDegrees, 15.0F, 170.0F, "%.0f");
        m_ui.SliderFloat("gp_lunge_hold_min", "Lunge Hold Min", &t.lungeHoldMinSeconds, 0.02F, 1.2F, "%.2f");
        m_ui.SliderFloat("gp_lunge_duration", "Lunge Duration", &t.lungeDurationSeconds, 0.08F, 2.0F, "%.2f");
        m_ui.SliderFloat("gp_lunge_recover", "Lunge Recover", &t.lungeRecoverSeconds, 0.1F, 3.0F, "%.2f");
        m_ui.SliderFloat("gp_short_recover", "Short Recover", &t.shortRecoverSeconds, 0.05F, 2.0F, "%.2f");
        m_ui.SliderFloat("gp_miss_recover", "Miss Recover", &t.missRecoverSeconds, 0.05F, 2.0F, "%.2f");
        m_ui.SliderFloat("gp_lunge_speed_start", "Lunge Speed Start", &t.lungeSpeedStart, 1.0F, 20.0F, "%.2f");
        m_ui.SliderFloat("gp_lunge_speed_end", "Lunge Speed End", &t.lungeSpeedEnd, 1.0F, 20.0F, "%.2f");

        m_ui.Label("Healing + Skill Checks", m_ui.Theme().colorAccent);
        m_ui.SliderFloat("gp_heal_duration", "Heal Duration", &t.healDurationSeconds, 2.0F, 60.0F, "%.1f");
        m_ui.SliderFloat("gp_skillcheck_min", "Skillcheck Min", &t.skillCheckMinInterval, 0.5F, 20.0F, "%.1f");
        m_ui.SliderFloat("gp_skillcheck_max", "Skillcheck Max", &t.skillCheckMaxInterval, 0.5F, 30.0F, "%.1f");

        m_ui.Label("Map Generation", m_ui.Theme().colorAccent);
        m_ui.SliderFloat("gp_weight_tl", "Weight TL", &t.weightTLWalls, 0.0F, 5.0F, "%.2f");
        m_ui.SliderFloat("gp_weight_jgl", "Weight Jungle Long", &t.weightJungleGymLong, 0.0F, 5.0F, "%.2f");
        m_ui.SliderFloat("gp_weight_jgs", "Weight Jungle Short", &t.weightJungleGymShort, 0.0F, 5.0F, "%.2f");
        m_ui.SliderFloat("gp_weight_shack", "Weight Shack", &t.weightShack, 0.0F, 5.0F, "%.2f");
        m_ui.SliderFloat("gp_weight_four", "Weight Four Lane", &t.weightFourLane, 0.0F, 5.0F, "%.2f");
        m_ui.SliderFloat("gp_weight_filla", "Weight Filler A", &t.weightFillerA, 0.0F, 5.0F, "%.2f");
        m_ui.SliderFloat("gp_weight_fillb", "Weight Filler B", &t.weightFillerB, 0.0F, 5.0F, "%.2f");
        m_ui.SliderInt("gp_max_loops", "Max Loops", &t.maxLoopsPerMap, 0, 64);
        m_ui.SliderFloat("gp_min_loop_dist", "Min Loop Distance Tiles", &t.minLoopDistanceTiles, 0.0F, 6.0F, "%.1f");

        m_ui.Label("Networking", m_ui.Theme().colorAccent);
        m_ui.SliderInt("gp_server_tick", "Server Tick Rate", &t.serverTickRate, 30, 60);
        m_ui.SliderInt("gp_interp_ms", "Interpolation Buffer (ms)", &t.interpolationBufferMs, 50, 1000);

        m_ui.Label("Tip: Apply for runtime changes, Save To File for persistence.", m_ui.Theme().colorTextMuted);
        if (!m_gameplayStatus.empty())
        {
            m_ui.Label(m_gameplayStatus, m_ui.Theme().colorTextMuted);
        }
    }

    m_ui.EndScrollRegion();

    if (m_ui.Button("settings_back_btn", "Back"))
    {
        *closeSettings = false;
    }
    m_ui.EndPanel();
}

void App::DrawInGameHudCustom(const game::gameplay::HudState& hudState, float fps, double nowSeconds)
{
    (void)nowSeconds;
    const float scale = m_ui.Scale();
    auto isActionablePrompt = [](const std::string& prompt) {
        if (prompt.empty())
        {
            return false;
        }
        if (prompt.find("Face ") != std::string::npos)
        {
            return false;
        }
        if (prompt.find("Move closer") != std::string::npos)
        {
            return false;
        }
        return true;
    };

    const bool showOverlay = m_showDebugOverlay;
    const bool showMovement = m_showMovementWindow && showOverlay;
    const bool showStats = m_showStatsWindow && showOverlay;
    const bool showControls = m_showControlsWindow && showOverlay;

    // Perk debug panel
    if (hudState.debugDrawEnabled)
    {
        const engine::ui::UiRect perkPanel{
            m_hudLayout.topLeftOffset.x * scale,
            (m_hudLayout.topLeftOffset.y + 270.0F) * scale,
            420.0F * scale,
            240.0F * scale,
        };
        m_ui.BeginPanel("hud_perks_debug", perkPanel, true);
        const std::string survMod = std::to_string(hudState.speedModifierSurvivor).substr(0, 4);
        const std::string killMod = std::to_string(hudState.speedModifierKiller).substr(0, 4);
        m_ui.Label("Perks Debug", 1.0F);
        m_ui.Label("Survivor (x" + survMod + ")", m_ui.Theme().colorTextMuted);
        if (hudState.activePerksSurvivor.empty())
        {
            m_ui.Label("  [none]", m_ui.Theme().colorTextMuted);
        }
        else
        {
            for (const auto& perk : hudState.activePerksSurvivor)
            {
                std::string line = "  " + perk.name + " [" + std::string(perk.isActive ? "ACTIVE" : "PASSIVE") + "]";
                if (perk.isActive && perk.activeRemainingSeconds > 0.01F)
                {
                    line += " (" + std::to_string(perk.activeRemainingSeconds).substr(0, 3) + "s)";
                }
                else if (!perk.isActive && perk.cooldownRemainingSeconds > 0.01F)
                {
                    line += " (CD " + std::to_string(perk.cooldownRemainingSeconds).substr(0, 3) + "s)";
                }
                m_ui.Label(line, perk.isActive ? m_ui.Theme().colorSuccess : m_ui.Theme().colorTextMuted);
            }
        }

        m_ui.Label("Killer (x" + killMod + ")", m_ui.Theme().colorTextMuted);
        if (hudState.activePerksKiller.empty())
        {
            m_ui.Label("  [none]", m_ui.Theme().colorTextMuted);
        }
        else
        {
            for (const auto& perk : hudState.activePerksKiller)
            {
                std::string line = "  " + perk.name + " [" + std::string(perk.isActive ? "ACTIVE" : "PASSIVE") + "]";
                if (perk.isActive && perk.activeRemainingSeconds > 0.01F)
                {
                    line += " (" + std::to_string(perk.activeRemainingSeconds).substr(0, 3) + "s)";
                }
                else if (!perk.isActive && perk.cooldownRemainingSeconds > 0.01F)
                {
                    line += " (CD " + std::to_string(perk.cooldownRemainingSeconds).substr(0, 3) + "s)";
                }
                m_ui.Label(line, perk.isActive ? m_ui.Theme().colorSuccess : m_ui.Theme().colorTextMuted);
            }
        }
        m_ui.EndPanel();
    }

    const engine::ui::UiRect topRight{
        static_cast<float>(m_ui.ScreenWidth()) - (360.0F * scale) - m_hudLayout.topRightOffset.x * scale,
        m_hudLayout.topRightOffset.y * scale,
        360.0F * scale,
        250.0F * scale,
    };
    m_ui.BeginPanel("hud_controls_custom", topRight, true);
    m_ui.Label("Controls", 1.03F);
    m_ui.Label("WASD: Move | Mouse: Look", m_ui.Theme().colorTextMuted);
    m_ui.Label("Shift: Sprint | Ctrl: Crouch", m_ui.Theme().colorTextMuted);
    m_ui.Label("E: Interact", m_ui.Theme().colorTextMuted);

    // Draggable/resizable HUD panels (from Ui-overhaul branch)
    const float screenW = static_cast<float>(m_ui.ScreenWidth());
    const float screenH = static_cast<float>(m_ui.ScreenHeight());
    const float windowW = static_cast<float>(std::max(1, m_window.WindowWidth()));
    const float windowH = static_cast<float>(std::max(1, m_window.WindowHeight()));
    const glm::vec2 mouseUi = m_input.MousePosition() * glm::vec2{screenW / windowW, screenH / windowH};

    const float leftX = m_hudLayout.topLeftOffset.x * scale;
    const float leftY = m_hudLayout.topLeftOffset.y * scale;
    const float defaultLeftWidth = 420.0F * scale;
    const float defaultMovementHeight = 310.0F * scale;
    const float defaultStatsHeight = 260.0F * scale;
    const float panelSpacing = 10.0F * scale;
    const float safeTop = std::max(36.0F * scale, (m_ui.Theme().baseFontSize + 12.0F) * scale);

    const float minPanelW = 200.0F * scale;
    const float minPanelH = 100.0F * scale;
    const float maxPanelW = screenW * 0.8F;
    const float maxPanelH = screenH * 0.8F;

    // Initialize default sizes on first use
    if (m_hudMovementSize.x < 0.0F) m_hudMovementSize = glm::vec2{defaultLeftWidth, defaultMovementHeight};
    if (m_hudStatsSize.x < 0.0F) m_hudStatsSize = glm::vec2{defaultLeftWidth, defaultStatsHeight};
    if (m_hudControlsSize.x < 0.0F) m_hudControlsSize = glm::vec2{360.0F * scale, 200.0F * scale};

    if (m_hudMovementPos.x < 0.0F || m_hudMovementPos.y < 0.0F)
    {
        m_hudMovementPos = glm::vec2{leftX, leftY};
    }
    if (m_hudStatsPos.x < 0.0F || m_hudStatsPos.y < 0.0F)
    {
        m_hudStatsPos = glm::vec2{leftX, leftY + m_hudMovementSize.y + panelSpacing};
    }
    if (m_hudControlsPos.x < 0.0F || m_hudControlsPos.y < 0.0F)
    {
        m_hudControlsPos = glm::vec2{
            screenW - m_hudControlsSize.x - m_hudLayout.topRightOffset.x * scale,
            m_hudLayout.topRightOffset.y * scale,
        };
    }

    auto clampPanel = [&](glm::vec2& pos, const glm::vec2& size) {
        const float maxX = std::max(0.0F, screenW - size.x);
        const float maxY = std::max(safeTop, screenH - size.y);
        pos.x = std::clamp(pos.x, 0.0F, maxX);
        pos.y = std::clamp(pos.y, safeTop, maxY);
    };

    const float headerHeight = std::max(24.0F * scale, m_ui.Theme().baseFontSize * scale + 10.0F * scale);
    const float resizeGripSize = 14.0F * scale;

    // Draw a visible drag header bar at the top of each panel
    auto drawDragHeader = [&](const glm::vec2& pos, const glm::vec2& size, const std::string& title) {
        const engine::ui::UiRect headerRect{pos.x, pos.y, size.x, headerHeight};
        const glm::vec4 headerBg{0.22F, 0.24F, 0.30F, 0.85F};
        const glm::vec4 headerBorder{0.35F, 0.38F, 0.45F, 0.9F};
        m_ui.DrawRect(headerRect, headerBg);
        m_ui.DrawRectOutline(headerRect, 1.0F, headerBorder);
        const float textX = pos.x + 8.0F * scale;
        const float textY = pos.y + 3.0F * scale;
        m_ui.DrawTextLabel(textX, textY, title, glm::vec4{0.7F, 0.75F, 0.82F, 1.0F}, 0.85F);
        // Draw grip dots to hint at draggability
        const float dotY = pos.y + headerHeight * 0.5F;
        const float dotStartX = pos.x + size.x - 28.0F * scale;
        const glm::vec4 dotColor{0.5F, 0.52F, 0.58F, 0.7F};
        for (int i = 0; i < 3; ++i)
        {
            const float dx = dotStartX + static_cast<float>(i) * 6.0F * scale;
            m_ui.DrawRect(engine::ui::UiRect{dx, dotY - 1.0F * scale, 3.0F * scale, 3.0F * scale}, dotColor);
        }
    };

    // Draw resize grip at bottom-right corner
    auto drawResizeGrip = [&](const glm::vec2& pos, const glm::vec2& size) {
        const float gx = pos.x + size.x - resizeGripSize;
        const float gy = pos.y + size.y - resizeGripSize;
        const glm::vec4 gripColor{0.45F, 0.48F, 0.55F, 0.6F};
        // Draw two diagonal lines as resize hint
        for (int i = 0; i < 3; ++i)
        {
            const float off = static_cast<float>(i) * 4.0F * scale;
            m_ui.DrawRect(engine::ui::UiRect{gx + resizeGripSize - 3.0F * scale - off, gy + resizeGripSize - 1.0F * scale, 3.0F * scale, 1.0F * scale}, gripColor);
            m_ui.DrawRect(engine::ui::UiRect{gx + resizeGripSize - 1.0F * scale, gy + resizeGripSize - 3.0F * scale - off, 1.0F * scale, 3.0F * scale}, gripColor);
        }
    };

    auto handleDrag = [&](HudDragTarget target, glm::vec2& pos, const glm::vec2& size) {
        const engine::ui::UiRect header{pos.x, pos.y, size.x, headerHeight};
        const bool hovering = header.Contains(mouseUi.x, mouseUi.y);

        if (m_hudDragTarget == HudDragTarget::None && !m_hudResizing && hovering && m_input.IsMousePressed(GLFW_MOUSE_BUTTON_LEFT))
        {
            m_hudDragTarget = target;
            m_hudDragOffset = mouseUi - pos;
        }

        if (m_hudDragTarget == target)
        {
            if (m_input.IsMouseDown(GLFW_MOUSE_BUTTON_LEFT))
            {
                pos = mouseUi - m_hudDragOffset;
            }
            else
            {
                m_hudDragTarget = HudDragTarget::None;
            }
        }

        clampPanel(pos, size);
    };

    auto handleResize = [&](HudDragTarget target, const glm::vec2& pos, glm::vec2& size) {
        const engine::ui::UiRect grip{pos.x + size.x - resizeGripSize, pos.y + size.y - resizeGripSize, resizeGripSize, resizeGripSize};
        const bool hoveringGrip = grip.Contains(mouseUi.x, mouseUi.y);

        if (!m_hudResizing && m_hudDragTarget == HudDragTarget::None && hoveringGrip && m_input.IsMousePressed(GLFW_MOUSE_BUTTON_LEFT))
        {
            m_hudResizing = true;
            m_hudResizeTarget = target;
        }

        if (m_hudResizing && m_hudResizeTarget == target)
        {
            if (m_input.IsMouseDown(GLFW_MOUSE_BUTTON_LEFT))
            {
                size.x = std::clamp(mouseUi.x - pos.x, minPanelW, maxPanelW);
                size.y = std::clamp(mouseUi.y - pos.y, minPanelH, maxPanelH);
            }
            else
            {
                m_hudResizing = false;
                m_hudResizeTarget = HudDragTarget::None;
            }
        }
    };

    if (showMovement)
    {
        handleDrag(HudDragTarget::Movement, m_hudMovementPos, m_hudMovementSize);
        handleResize(HudDragTarget::Movement, m_hudMovementPos, m_hudMovementSize);
        drawDragHeader(m_hudMovementPos, m_hudMovementSize, "Movement");
        const engine::ui::UiRect movementRect{
            m_hudMovementPos.x,
            m_hudMovementPos.y + headerHeight,
            m_hudMovementSize.x,
            m_hudMovementSize.y - headerHeight,
        };
        m_ui.BeginPanel("hud_movement_custom", movementRect, true);
        m_ui.Label("Role: " + hudState.roleName, 1.05F);
        m_ui.Label("State: " + hudState.survivorStateName + " | Move: " + hudState.movementStateName, m_ui.Theme().colorTextMuted);
        m_ui.Label("Camera: " + hudState.cameraModeName + " | Render: " + hudState.renderModeName, m_ui.Theme().colorTextMuted);
        m_ui.Label("Chase: " + std::string(hudState.chaseActive ? "ON" : "OFF"), hudState.chaseActive ? m_ui.Theme().colorDanger : m_ui.Theme().colorTextMuted);
        m_ui.Label("Attack: " + hudState.killerAttackStateName, m_ui.Theme().colorTextMuted);
        if (hudState.roleName == "Killer")
        {
            m_ui.Label(hudState.attackHint, m_ui.Theme().colorTextMuted);
        }
        if (hudState.roleName == "Killer" && hudState.lungeCharge01 > 0.0F)
        {
            m_ui.ProgressBar(
                "hud_lunge_progress_custom",
                hudState.lungeCharge01,
                std::to_string(static_cast<int>(hudState.lungeCharge01 * 100.0F)) + "%"
            );
        }
        if (hudState.selfHealing)
        {
            m_ui.ProgressBar(
                "hud_selfheal_progress_custom",
                hudState.selfHealProgress,
                std::to_string(static_cast<int>(hudState.selfHealProgress * 100.0F)) + "%"
            );
        }
        if (hudState.roleName == "Survivor" && hudState.survivorStateName == "Carried")
        {
            m_ui.Label("Wiggle: Alternate A/D to escape", m_ui.Theme().colorTextMuted);
            m_ui.ProgressBar(
                "hud_carry_escape_custom",
                hudState.carryEscapeProgress,
                std::to_string(static_cast<int>(hudState.carryEscapeProgress * 100.0F)) + "%"
            );
        }
        m_ui.Label(
            "Terror Radius: " + std::string(hudState.terrorRadiusVisible ? "ON " : "OFF ") + std::to_string(hudState.terrorRadiusMeters) + "m",
            m_ui.Theme().colorTextMuted
        );
        m_ui.EndPanel();
        drawResizeGrip(m_hudMovementPos, m_hudMovementSize);
    }

    if (showStats)
    {
        handleDrag(HudDragTarget::Stats, m_hudStatsPos, m_hudStatsSize);
        handleResize(HudDragTarget::Stats, m_hudStatsPos, m_hudStatsSize);
        drawDragHeader(m_hudStatsPos, m_hudStatsSize, "Stats");
        const engine::ui::UiRect statsRect{
            m_hudStatsPos.x,
            m_hudStatsPos.y + headerHeight,
            m_hudStatsSize.x,
            m_hudStatsSize.y - headerHeight,
        };
        m_ui.BeginPanel("hud_stats_custom", statsRect, true);
        m_ui.Label("Generators: " + std::to_string(hudState.generatorsCompleted) + "/" + std::to_string(hudState.generatorsTotal), m_ui.Theme().colorAccent);
        if (hudState.repairingGenerator)
        {
            m_ui.ProgressBar(
                "hud_gen_progress_custom",
                hudState.activeGeneratorProgress,
                std::to_string(static_cast<int>(hudState.activeGeneratorProgress * 100.0F)) + "%"
            );
        }
        m_ui.Label("Speed: " + std::to_string(hudState.playerSpeed), m_ui.Theme().colorTextMuted);
        m_ui.Label("Grounded: " + std::string(hudState.grounded ? "yes" : "no"), m_ui.Theme().colorTextMuted);
        m_ui.Label("Chase: " + std::string(hudState.chaseActive ? "ON" : "OFF"), hudState.chaseActive ? m_ui.Theme().colorDanger : m_ui.Theme().colorTextMuted);
        m_ui.Label("Distance: " + std::to_string(hudState.chaseDistance), m_ui.Theme().colorTextMuted);
        m_ui.Label("LOS: " + std::string(hudState.lineOfSight ? "true" : "false"), m_ui.Theme().colorTextMuted);
        m_ui.Label("Hook Stage: " + std::to_string(hudState.hookStage), m_ui.Theme().colorTextMuted);
        if (hudState.hookStageProgress > 0.0F)
        {
            m_ui.ProgressBar(
                "hud_hook_progress_custom",
                hudState.hookStageProgress,
                std::to_string(static_cast<int>(hudState.hookStageProgress * 100.0F)) + "%"
            );
        }
        m_ui.EndPanel();
        drawResizeGrip(m_hudStatsPos, m_hudStatsSize);
    }

    if (showControls)
    {
        handleDrag(HudDragTarget::Controls, m_hudControlsPos, m_hudControlsSize);
        handleResize(HudDragTarget::Controls, m_hudControlsPos, m_hudControlsSize);
        drawDragHeader(m_hudControlsPos, m_hudControlsSize, "Controls");
        const engine::ui::UiRect topRight{
            m_hudControlsPos.x,
            m_hudControlsPos.y + headerHeight,
            m_hudControlsSize.x,
            m_hudControlsSize.y - headerHeight,
        };
        m_ui.BeginPanel("hud_controls_custom", topRight, true);
        m_ui.Label("WASD: Move | Mouse: Look", m_ui.Theme().colorTextMuted);
        m_ui.Label("Shift: Sprint | Ctrl: Crouch", m_ui.Theme().colorTextMuted);
        m_ui.Label("E: Interact", m_ui.Theme().colorTextMuted);
        if (hudState.roleName == "Killer")
        {
            m_ui.Label("LMB click: Short | Hold LMB: Lunge", m_ui.Theme().colorTextMuted);
        }
        m_ui.Label("~ Console | F1/F2 Debug | F3 Render", m_ui.Theme().colorTextMuted);
        m_ui.Label("ALT: Release cursor for UI", m_ui.Theme().colorTextMuted);
        m_ui.EndPanel();
        drawResizeGrip(m_hudControlsPos, m_hudControlsSize);
    }

    if (isActionablePrompt(hudState.interactionPrompt))
    {
        const engine::ui::UiRect promptRect{
            (static_cast<float>(m_ui.ScreenWidth()) - 380.0F * scale) * 0.5F,
            static_cast<float>(m_ui.ScreenHeight()) * 0.60F,
            380.0F * scale,
            52.0F * scale,
        };
        m_ui.BeginPanel("hud_prompt_compact", promptRect, true);
        m_ui.Label(hudState.interactionPrompt, m_ui.Theme().colorAccent, 1.0F);
        m_ui.EndPanel();
    }

    const bool showBottomPanel = hudState.selfHealing || hudState.skillCheckActive ||
                                 hudState.carryEscapeProgress > 0.0F || hudState.hookStage > 0;
    if (!showBottomPanel)
    {
        return;
    }

    const engine::ui::UiRect bottom{
        (static_cast<float>(m_ui.ScreenWidth()) - 620.0F * scale) * 0.5F + m_hudLayout.bottomCenterOffset.x * scale,
        static_cast<float>(m_ui.ScreenHeight()) - 240.0F * scale - m_hudLayout.bottomCenterOffset.y * scale,
        620.0F * scale,
        240.0F * scale,
    };
    m_ui.BeginPanel("hud_bottom_custom", bottom, true);

    if (hudState.selfHealing)
    {
        m_ui.Label("Self Heal", m_ui.Theme().colorAccent);
        m_ui.ProgressBar("hud_heal_progress", hudState.selfHealProgress, std::to_string(static_cast<int>(hudState.selfHealProgress * 100.0F)) + "%");
    }
    if (hudState.carryEscapeProgress > 0.0F)
    {
        m_ui.Label("Wiggle Escape: Alternate A/D", m_ui.Theme().colorAccent);
        m_ui.ProgressBar("hud_wiggle_progress", hudState.carryEscapeProgress, std::to_string(static_cast<int>(hudState.carryEscapeProgress * 100.0F)) + "%");
    }
    if (hudState.hookStage > 0)
    {
        m_ui.Label("Hook Stage: " + std::to_string(hudState.hookStage), m_ui.Theme().colorDanger);
        m_ui.ProgressBar(
            "hud_hook_progress",
            hudState.hookStageProgress,
            std::to_string(static_cast<int>(hudState.hookStageProgress * 100.0F)) + "%"
        );
        if (hudState.hookStage == 1)
        {
            const int attemptsLeft = std::max(0, hudState.hookEscapeAttemptsMax - hudState.hookEscapeAttemptsUsed);
            m_ui.Label(
                "E: Attempt self-unhook (" + std::to_string(static_cast<int>(hudState.hookEscapeChance * 100.0F)) + "%), attempts left: " +
                    std::to_string(attemptsLeft),
                m_ui.Theme().colorTextMuted
            );
        }
        else if (hudState.hookStage == 2)
        {
            m_ui.Label("Struggle: hit SPACE during skill checks", m_ui.Theme().colorTextMuted);
        }
    }
    m_ui.EndPanel();
}

void App::DrawUiTestPanel()
{
    const float scale = m_ui.Scale();
    const float topY = 48.0F * scale; // clear the developer toolbar
    const engine::ui::UiRect panel{
        18.0F * scale,
        topY,
        std::min(440.0F * scale, static_cast<float>(m_ui.ScreenWidth()) - 36.0F * scale),
        std::min(760.0F * scale, static_cast<float>(m_ui.ScreenHeight()) - topY - 18.0F * scale),
    };
    m_ui.BeginPanel("ui_test_panel", panel, true);
    m_ui.Label("UI Test Panel", 1.1F);
    m_ui.Label("All core widgets should work here.", m_ui.Theme().colorTextMuted);

    if (m_ui.Button("test_button", "Button: +10% progress"))
    {
        m_uiTestProgress = std::min(1.0F, m_uiTestProgress + 0.1F);
    }
    (void)m_ui.Button("test_button_disabled", "Disabled Button", false);

    if (m_ui.Checkbox("test_checkbox", "Checkbox", &m_uiTestCheckbox))
    {
        m_statusToastMessage = std::string("Checkbox: ") + (m_uiTestCheckbox ? "ON" : "OFF");
        m_statusToastUntilSeconds = glfwGetTime() + 1.4;
    }
    m_ui.SliderFloat("test_slider_f", "Slider Float", &m_uiTestSliderF, 0.0F, 1.0F, "%.3f");
    m_ui.SliderInt("test_slider_i", "Slider Int", &m_uiTestSliderI, 0, 100);

    const std::vector<std::string> ddItems{"Option A", "Option B", "Option C", "Option D"};
    if (m_ui.Dropdown("test_dropdown", "Dropdown", &m_uiTestDropdown, ddItems))
    {
        m_statusToastMessage = "Dropdown selected: " + ddItems[static_cast<std::size_t>(glm::clamp(m_uiTestDropdown, 0, 3))];
        m_statusToastUntilSeconds = glfwGetTime() + 1.5;
    }

    if (m_ui.InputText("test_input", "InputText", &m_uiTestInput, 64))
    {
        m_statusToastMessage = "Input updated: " + m_uiTestInput;
        m_statusToastUntilSeconds = glfwGetTime() + 1.0;
    }

    m_ui.Label("Columns Example: (Label) (Input) (Input)", m_ui.Theme().colorAccent);
    m_ui.PushLayout(engine::ui::UiSystem::LayoutAxis::Horizontal, 8.0F, 0.0F);
    m_ui.Label("Pair", m_ui.Theme().colorTextMuted, 1.0F, 110.0F);
    m_ui.InputText("test_input_a", "", &m_uiTestInputA, 24, 130.0F);
    m_ui.InputText("test_input_b", "", &m_uiTestInputB, 24, 130.0F);
    m_ui.PopLayout();

    m_ui.ProgressBar("test_progress", m_uiTestProgress, std::to_string(static_cast<int>(m_uiTestProgress * 100.0F)) + "%");

    std::string captured;
    if (m_ui.KeybindCapture("test_keybind_capture", "KeybindCapture", m_uiTestCaptureMode, &captured))
    {
        if (!m_uiTestCaptureMode)
        {
            m_uiTestCaptureMode = true;
        }
        else if (!captured.empty())
        {
            m_uiTestCaptured = captured;
            m_uiTestCaptureMode = false;
        }
    }
    if (!m_uiTestCaptured.empty())
    {
        m_ui.Label("Captured: " + m_uiTestCaptured, m_ui.Theme().colorAccent);
    }
    m_ui.Label(std::string("Input Capture: ") + (m_ui.WantsInputCapture() ? "YES" : "NO"), m_ui.Theme().colorTextMuted);

    if (m_ui.Button("test_progress_reset", "Reset Test Values"))
    {
        m_uiTestCheckbox = true;
        m_uiTestSliderF = 0.35F;
        m_uiTestSliderI = 7;
        m_uiTestDropdown = 0;
        m_uiTestInput = "sample";
        m_uiTestInputA = "left";
        m_uiTestInputB = "right";
        m_uiTestProgress = 0.35F;
        m_uiTestCaptureMode = false;
        m_uiTestCaptured.clear();
    }

    m_ui.EndPanel();
}

void App::DrawLoadingScreenTestPanel()
{
    const float scale = m_ui.Scale();
    const float topY = 48.0F * scale; // clear the developer toolbar
    const engine::ui::UiRect panel{
        18.0F * scale,
        topY,
        std::min(440.0F * scale, static_cast<float>(m_ui.ScreenWidth()) - 36.0F * scale),
        std::min(680.0F * scale, static_cast<float>(m_ui.ScreenHeight()) - topY - 18.0F * scale),
    };
    m_ui.BeginPanel("loading_screen_test_panel", panel, true);
    m_ui.Label("Loading Screen Test Panel", 1.1F);
    m_ui.Label("Test loading screen UI and progress animations.", m_ui.Theme().colorTextMuted);

    m_ui.SliderFloat("loading_speed", "Loading Speed", &m_loadingTestSpeed, 0.1F, 2.0F, "%.2f");
    m_ui.SliderInt("loading_steps", "Loading Steps", &m_loadingTestSteps, 1, 10);

    m_ui.PushLayout(engine::ui::UiSystem::LayoutAxis::Horizontal, 8.0F, 0.0F);
    if (m_ui.Button("loading_start", "Start Loading"))
    {
        m_loadingTestProgress = 0.0F;
        m_loadingTestAutoAdvance = true;
        m_loadingTestCurrentStep = 0;
        m_statusToastMessage = "Loading started";
        m_statusToastUntilSeconds = glfwGetTime() + 1.0;
    }
    if (m_ui.Button("loading_pause", m_loadingTestAutoAdvance ? "Pause" : "Resume"))
    {
        m_loadingTestAutoAdvance = !m_loadingTestAutoAdvance;
    }
    if (m_ui.Button("loading_reset", "Reset"))
    {
        m_loadingTestProgress = 0.0F;
        m_loadingTestAutoAdvance = false;
        m_loadingTestCurrentStep = 0;
        m_statusToastMessage = "Loading reset";
        m_statusToastUntilSeconds = glfwGetTime() + 1.0;
    }
    m_ui.PopLayout();

    m_ui.Label("Loading Progress:", m_ui.Theme().colorAccent);
    m_ui.ProgressBar("loading_progress_bar", m_loadingTestProgress, std::to_string(static_cast<int>(m_loadingTestProgress * 100.0F)) + "%");

    m_ui.SliderFloat("loading_manual", "Manual Progress", &m_loadingTestProgress, 0.0F, 1.0F, "%.2f");

    m_ui.Label("Current Step: " + std::to_string(m_loadingTestCurrentStep + 1) + " / " + std::to_string(m_loadingTestSteps), m_ui.Theme().colorTextMuted);

    m_ui.Checkbox("loading_show_full", "Enable Full Screen Mode", &m_loadingTestShowFull);

    m_ui.Spacer(8.0F);

    if (m_ui.Button("loading_toggle_full", m_loadingTestShowFull ? "Show Full Screen" : "Show Full Screen (disabled)"))
    {
        if (m_loadingTestShowFull && m_appMode != AppMode::Loading)
        {
            m_appMode = AppMode::Loading;
        }
        else if (m_appMode == AppMode::Loading)
        {
            m_appMode = AppMode::MainMenu;
        }
    }

    // Update progress even when in full screen mode
    if (m_loadingTestAutoAdvance && m_loadingTestProgress < 1.0F)
    {
        m_loadingTestProgress += m_loadingTestSpeed * static_cast<float>(m_time.DeltaSeconds());
        m_loadingTestProgress = std::min(1.0F, m_loadingTestProgress);
        const int newStep = static_cast<int>(m_loadingTestProgress * m_loadingTestSteps);
        if (newStep != m_loadingTestCurrentStep)
        {
            m_loadingTestCurrentStep = newStep;
            m_loadingTestSelectedTip = (m_loadingTestSelectedTip + 1) % static_cast<int>(m_loadingTestTips.size());
        }
    }

    m_ui.Checkbox("loading_show_tips", "Show Tips", &m_loadingTestShowTips);
    if (m_loadingTestShowTips)
    {
        m_ui.Label("Tip:", m_ui.Theme().colorAccent);
        const std::string& tip = m_loadingTestTips[static_cast<std::size_t>(m_loadingTestSelectedTip) % m_loadingTestTips.size()];
        m_ui.Label(tip, 0.9F);
    }

    m_ui.PushLayout(engine::ui::UiSystem::LayoutAxis::Horizontal, 8.0F, 0.0F);
    if (m_ui.Button("tip_prev", "Previous Tip"))
    {
        m_loadingTestSelectedTip = (m_loadingTestSelectedTip - 1 + static_cast<int>(m_loadingTestTips.size())) % static_cast<int>(m_loadingTestTips.size());
    }
    if (m_ui.Button("tip_next", "Next Tip"))
    {
        m_loadingTestSelectedTip = (m_loadingTestSelectedTip + 1) % static_cast<int>(m_loadingTestTips.size());
    }
    m_ui.PopLayout();

    m_ui.EndPanel();
}

void App::DrawFullLoadingScreen(float progress01, const std::string& tip, const std::string& stepText)
{
    const float scale = m_ui.Scale();
    const int w = m_ui.ScreenWidth();
    const int h = m_ui.ScreenHeight();

    const engine::ui::UiRect fullScreen{0.0F, 0.0F, static_cast<float>(w), static_cast<float>(h)};
    m_ui.BeginRootPanel("loading_screen_full", fullScreen, true);

    // Use horizontal layout to center content horizontally
    m_ui.PushLayout(engine::ui::UiSystem::LayoutAxis::Horizontal, 0.0F, 0.0F);

    // Left spacer to center horizontally
    m_ui.Spacer((w - 550.0F * scale) * 0.5F);

    // Nested vertical layout for the content
    m_ui.PushLayout(engine::ui::UiSystem::LayoutAxis::Vertical, 0.0F, 0.0F);

    // Top spacer to center vertically
    m_ui.Spacer(h * 0.35F);

    m_ui.Label("LOADING", m_ui.Theme().colorAccent, 1.8F);

    m_ui.Spacer(30.0F * scale);

    const float progressBarWidth = 500.0F * scale;

    m_ui.ProgressBar("loading_full_progress", progress01, std::to_string(static_cast<int>(progress01 * 100.0F)) + "%", progressBarWidth);

    m_ui.Spacer(40.0F * scale);

    if (!tip.empty())
    {
        m_ui.Label("Tip:", m_ui.Theme().colorTextMuted, 0.9F);
        m_ui.Label(tip, 0.85F);
    }

    m_ui.Spacer(h * 0.25F);

    if (!stepText.empty())
    {
        m_ui.Label(stepText, m_ui.Theme().colorTextMuted, 0.8F);
    }

    m_ui.PopLayout(); // End vertical layout
    // Right spacer to complete centering is implicit via remaining space
    m_ui.PopLayout(); // End horizontal layout

    m_ui.EndPanel();
}

std::string App::RoleNameFromIndex(int index)
{
    return index == 1 ? "killer" : "survivor";
}

std::string App::MapNameFromIndex(int index)
{
    switch (index)
    {
        case 0: return "test";
        case 1: return "collision_test";
        case 2: return "main";
        default: return "main";
    }
}

// Loading Screen System
void App::StartLoading(game::ui::LoadingScenario scenario, const std::string& title)
{
    m_appMode = AppMode::Loading;
    m_loadingManager.BeginLoading(scenario, title);
}

void App::UpdateLoading(float deltaSeconds)
{
    m_loadingManager.UpdateAndRender(deltaSeconds);

    // Handle error and cancel
    if (m_loadingManager.GetLoadingScreen().HasError())
    {
        if (m_input.IsKeyPressed(GLFW_KEY_ESCAPE))
        {
            CancelLoading();
        }
    }
}

void App::FinishLoading()
{
    // Determine what to do after loading
    switch (m_loadingManager.GetCurrentScenario())
    {
        case game::ui::LoadingScenario::SoloMatch:
            m_appMode = AppMode::InGame;
            break;
        case game::ui::LoadingScenario::HostMatch:
        case game::ui::LoadingScenario::JoinMatch:
            m_appMode = AppMode::InGame;
            break;
        case game::ui::LoadingScenario::EditorLevel:
            m_appMode = AppMode::Editor;
            break;
        case game::ui::LoadingScenario::MainMenu:
        case game::ui::LoadingScenario::Startup:
        default:
            m_appMode = AppMode::MainMenu;
            break;
    }

    m_loadingManager.SetLoadingComplete(false);
}

void App::CancelLoading()
{
    m_loadingManager.CancelLoading();
    ResetToMainMenu();
}

bool App::IsLoading() const
{
    return m_appMode == AppMode::Loading;
}

bool App::IsLoadingComplete() const
{
    return m_loadingManager.IsLoadingComplete();
}

void App::SetLoadingStage(game::ui::LoadingStage stage)
{
    m_loadingManager.GetLoadingScreen().SetStage(stage);
}

void App::UpdateLoadingProgress(float overall, float stage)
{
    m_loadingManager.GetLoadingScreen().SetOverallProgress(overall);
    m_loadingManager.GetLoadingScreen().SetStageProgress(stage);
}

void App::SetLoadingTask(const std::string& task, const std::string& subtask)
{
    m_loadingManager.GetLoadingScreen().SetTask(task);
    if (!subtask.empty())
    {
        m_loadingManager.GetLoadingScreen().SetSubtask(subtask);
    }
}

void App::SetLoadingError(const std::string& error)
{
    m_loadingManager.SetError(error);
}

} // namespace engine::core
