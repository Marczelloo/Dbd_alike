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
constexpr std::uint16_t kButtonUseAltPressed = 1 << 11;
constexpr std::uint16_t kButtonUseAltHeld = 1 << 12;
constexpr std::uint16_t kButtonUseAltReleased = 1 << 13;
constexpr std::uint16_t kButtonDropItemPressed = 1 << 14;
constexpr std::uint16_t kButtonPickupItemPressed = 1 << 15;

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

audio::AudioSystem::Bus AudioBusFromName(const std::string& value)
{
    if (value == "music")
    {
        return audio::AudioSystem::Bus::Music;
    }
    if (value == "ui")
    {
        return audio::AudioSystem::Bus::Ui;
    }
    if (value == "ambience" || value == "ambient")
    {
        return audio::AudioSystem::Bus::Ambience;
    }
    if (value == "master")
    {
        return audio::AudioSystem::Bus::Master;
    }
    return audio::AudioSystem::Bus::Sfx;
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
    (void)LoadAudioConfig();
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

    if (!m_audio.Initialize("assets/audio"))
    {
        std::cerr << "Warning: failed to initialize audio backend.\n";
    }
    ApplyAudioSettings();
    (void)LoadTerrorRadiusProfile("default_killer");

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

    if (!m_console.Initialize(m_window))
    {
        CloseNetworkLogFile();
        return false;
    }

    ResetToMainMenu();

    double fpsAccumulator = 0.0;
    int fpsFrames = 0;
    float currentFps = 0.0F;

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
        if ((inGame || inEditor) && !m_console.IsOpen() && m_input.IsKeyPressed(GLFW_KEY_ESCAPE))
        {
            if (inGame)
            {
                m_pauseMenuOpen = !m_pauseMenuOpen;
            }
            else
            {
                ResetToMainMenu();
            }
        }

        const bool controlsEnabled = (inGame || inEditor) && !m_pauseMenuOpen && !m_console.IsOpen() && !m_settingsMenuOpen;
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
        if (m_input.IsKeyPressed(GLFW_KEY_F10))
        {
            m_useLegacyImGuiMenus = !m_useLegacyImGuiMenus;
            m_statusToastMessage = m_useLegacyImGuiMenus ? "Legacy ImGui menus ON" : "Custom UI menus ON";
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
            m_audio.SetListener(m_gameplay.CameraPosition(), m_gameplay.CameraForward());
            UpdateTerrorRadiusAudio(static_cast<float>(m_time.DeltaSeconds()));
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
        m_audio.Update(static_cast<float>(m_time.DeltaSeconds()));

        m_renderer.BeginFrame(glm::vec3{0.06F, 0.07F, 0.08F});
        glm::mat4 viewProjection{1.0F};
        if (inGame)
        {
            m_renderer.SetLightingEnabled(true);
            m_gameplay.Render(m_renderer);

            m_renderer.SetPointLights(m_runtimeMapPointLights);

            const float aspect = m_window.FramebufferHeight() > 0
                                     ? static_cast<float>(m_window.FramebufferWidth()) / static_cast<float>(m_window.FramebufferHeight())
                                     : (16.0F / 9.0F);
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
            if (m_useLegacyImGuiMenus)
            {
                DrawMainMenuUi(&shouldQuit);
            }
            else
            {
                DrawMainMenuUiCustom(&shouldQuit);
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
            if (m_useLegacyImGuiMenus)
            {
                DrawPauseMenuUi(&closePauseMenu, &backToMenu, &shouldQuit);
            }
            else
            {
                DrawPauseMenuUiCustom(&closePauseMenu, &backToMenu, &shouldQuit);
            }
        }

        if (m_settingsMenuOpen)
        {
            if (m_useLegacyImGuiMenus)
            {
                DrawSettingsUi(&m_settingsMenuOpen);
            }
            else
            {
                DrawSettingsUiCustom(&m_settingsMenuOpen);
            }
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
            if (!m_useLegacyImGuiMenus)
            {
                DrawInGameHudCustom(m_gameplay.BuildHudState(), currentFps, glfwGetTime());
            }
        }

        if (m_showUiTestPanel)
        {
            DrawUiTestPanel();
        }

        m_ui.EndFrame();

        if (m_showNetworkOverlay && (inGame || m_appMode == AppMode::MainMenu))
        {
            DrawNetworkOverlayUi(glfwGetTime());
        }
        if (inGame && m_showDebugOverlay)
        {
            DrawPlayersDebugUi(glfwGetTime());
        }

        const game::gameplay::HudState hudState = m_gameplay.BuildHudState();
        ::ui::ConsoleContext context;
        context.gameplay = &m_gameplay;
        context.window = &m_window;
        context.vsync = &m_vsyncEnabled;
        context.fpsLimit = &m_fpsLimit;
        context.renderPlayerHud = m_useLegacyImGuiMenus;

        bool showOverlayThisFrame = m_showDebugOverlay && m_appMode == AppMode::InGame;
        context.showDebugOverlay = &showOverlayThisFrame;

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
        context.sceneDump = [this]() {
            if (m_appMode == AppMode::Editor)
            {
                return m_levelEditor.SceneDump();
            }
            std::ostringstream oss;
            oss << "GameplaySceneDump\n";
            oss << " mode=in_game";
            return oss.str();
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
        context.setTerrorAudioDebug = [this](bool enabled) {
            m_terrorAudioDebug = enabled;
            if (enabled)
            {
                m_statusToastMessage = "Terror audio debug ON";
            }
            else
            {
                m_statusToastMessage = "Terror audio debug OFF";
            }
            m_statusToastUntilSeconds = glfwGetTime() + 2.0;
        };
        context.terrorRadiusDump = [this]() -> std::string {
            return DumpTerrorRadiusState();
        };
        context.audioPlay = [this](const std::string& clip, const std::string& busName, bool loop) {
            const audio::AudioSystem::Bus bus = AudioBusFromName(busName);
            const audio::AudioSystem::PlayOptions options{};
            audio::AudioSystem::SoundHandle handle = 0;
            if (loop)
            {
                handle = m_audio.PlayLoop(clip, bus, options);
                if (handle != 0)
                {
                    m_debugAudioLoops.push_back(handle);
                }
            }
            else
            {
                handle = m_audio.PlayOneShot(clip, bus, options);
            }
            if (handle == 0)
            {
                AppendNetworkLog(std::string("AUDIO play failed: clip=") + clip + " bus=" + busName);
            }
        };
        context.audioStopAll = [this]() {
            for (const audio::AudioSystem::SoundHandle handle : m_debugAudioLoops)
            {
                m_audio.Stop(handle);
            }
            m_debugAudioLoops.clear();
            m_audio.StopAll();
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
    m_console.Shutdown();
    m_ui.Shutdown();
    m_audio.Shutdown();
    m_renderer.Shutdown();
    CloseNetworkLogFile();
    return true;
}

void App::ResetToMainMenu()
{
    StopTerrorRadiusAudio();
    m_audio.StopAll();
    m_debugAudioLoops.clear();
    m_sessionAmbienceLoop = 0;

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

    m_gameplay.LoadMap("main");
    m_gameplay.SetControlledRole("survivor");
    m_renderer.SetEnvironmentSettings(render::EnvironmentSettings{});
    m_renderer.SetPointLights({});
    m_renderer.SetSpotLights({});
    m_runtimeMapPointLights.clear();
    m_runtimeMapSpotLights.clear();
    m_gameplay.SetMapSpotLightCount(0);

    m_sessionRoleName = "survivor";
    m_remoteRoleName = "killer";
    m_sessionMapName = "main";
    m_sessionMapType = game::gameplay::GameplaySystems::MapType::Main;
    m_connectedEndpoint.clear();
    InitializePlayerBindings();

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
    m_appMode = AppMode::InGame;
    m_pauseMenuOpen = false;
    m_settingsMenuOpen = false;
    m_settingsOpenedFromPause = false;
    m_menuNetStatus = "Solo session started.";
    m_serverGameplayValues = false;
    m_audio.StopAll();
    m_debugAudioLoops.clear();
    m_sessionAmbienceLoop = m_audio.PlayLoop("ambience_loop", audio::AudioSystem::Bus::Ambience);
    (void)LoadTerrorRadiusProfile("default_killer");

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
    }
    else if (normalizedMap == "collision_test")
    {
        m_sessionMapType = game::gameplay::GameplaySystems::MapType::CollisionTest;
    }
    else
    {
        m_sessionMapType = game::gameplay::GameplaySystems::MapType::Test;
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
    m_audio.StopAll();
    m_debugAudioLoops.clear();
    m_sessionAmbienceLoop = m_audio.PlayLoop("ambience_loop", audio::AudioSystem::Bus::Ambience);
    (void)LoadTerrorRadiusProfile("default_killer");

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
    m_audio.StopAll();
    m_debugAudioLoops.clear();
    m_sessionAmbienceLoop = m_audio.PlayLoop("ambience_loop", audio::AudioSystem::Bus::Ambience);
    (void)LoadTerrorRadiusProfile("default_killer");

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
        command.useAltPressed = (inputPacket.buttons & kButtonUseAltPressed) != 0;
        command.useAltHeld = (inputPacket.buttons & kButtonUseAltHeld) != 0;
        command.useAltReleased = (inputPacket.buttons & kButtonUseAltReleased) != 0;
        command.dropItemPressed = (inputPacket.buttons & kButtonDropItemPressed) != 0;
        command.pickupItemPressed = (inputPacket.buttons & kButtonPickupItemPressed) != 0;
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
        if (input.IsMousePressed(GLFW_MOUSE_BUTTON_RIGHT))
        {
            packet.buttons |= kButtonUseAltPressed;
        }
        if (input.IsMouseDown(GLFW_MOUSE_BUTTON_RIGHT))
        {
            packet.buttons |= kButtonUseAltHeld;
        }
        if (input.IsMouseReleased(GLFW_MOUSE_BUTTON_RIGHT))
        {
            packet.buttons |= kButtonUseAltReleased;
        }
        if (input.IsKeyPressed(GLFW_KEY_R))
        {
            packet.buttons |= kButtonDropItemPressed;
        }
        if (input.IsMousePressed(GLFW_MOUSE_BUTTON_LEFT))
        {
            packet.buttons |= kButtonPickupItemPressed;
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

    auto writeString = [&](const std::string& value, std::uint16_t maxLen) {
        const std::uint16_t length = static_cast<std::uint16_t>(std::min<std::size_t>(value.size(), maxLen));
        AppendValue(outBuffer, length);
        outBuffer.insert(outBuffer.end(), value.begin(), value.begin() + length);
    };

    writeString(snapshot.survivorCharacterId, 128);
    writeString(snapshot.killerCharacterId, 128);
    writeString(snapshot.survivorItemId, 128);
    writeString(snapshot.survivorItemAddonA, 128);
    writeString(snapshot.survivorItemAddonB, 128);
    writeString(snapshot.killerPowerId, 128);
    writeString(snapshot.killerPowerAddonA, 128);
    writeString(snapshot.killerPowerAddonB, 128);

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
    AppendValue(outBuffer, static_cast<std::uint8_t>(snapshot.chaseInCenterFOV ? 1 : 0));
    AppendValue(outBuffer, snapshot.chaseTimeSinceLOS);
    AppendValue(outBuffer, snapshot.chaseTimeSinceCenterFOV);
    AppendValue(outBuffer, snapshot.chaseTimeInChase);
    AppendValue(outBuffer, snapshot.bloodlustTier);
    AppendValue(outBuffer, snapshot.survivorItemCharges);
    AppendValue(outBuffer, snapshot.survivorItemActive);
    AppendValue(outBuffer, snapshot.survivorItemUsesRemaining);
    AppendValue(outBuffer, snapshot.wraithCloaked);
    AppendValue(outBuffer, snapshot.wraithTransitionTimer);
    AppendValue(outBuffer, snapshot.wraithPostUncloakTimer);
    AppendValue(outBuffer, snapshot.killerBlindTimer);
    AppendValue(outBuffer, snapshot.killerBlindStyleWhite);
    AppendValue(outBuffer, snapshot.carriedTrapCount);

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

    const std::uint16_t trapCount = static_cast<std::uint16_t>(std::min<std::size_t>(snapshot.traps.size(), 1024));
    AppendValue(outBuffer, trapCount);
    for (std::size_t i = 0; i < trapCount; ++i)
    {
        const auto& trap = snapshot.traps[i];
        AppendValue(outBuffer, trap.entity);
        AppendValue(outBuffer, trap.state);
        AppendValue(outBuffer, trap.trappedEntity);
        AppendValue(outBuffer, trap.position.x);
        AppendValue(outBuffer, trap.position.y);
        AppendValue(outBuffer, trap.position.z);
        AppendValue(outBuffer, trap.halfExtents.x);
        AppendValue(outBuffer, trap.halfExtents.y);
        AppendValue(outBuffer, trap.halfExtents.z);
        AppendValue(outBuffer, trap.escapeChance);
        AppendValue(outBuffer, trap.escapeAttempts);
        AppendValue(outBuffer, trap.maxEscapeAttempts);
    }

    const std::uint16_t groundItemCount = static_cast<std::uint16_t>(std::min<std::size_t>(snapshot.groundItems.size(), 1024));
    AppendValue(outBuffer, groundItemCount);
    for (std::size_t i = 0; i < groundItemCount; ++i)
    {
        const auto& groundItem = snapshot.groundItems[i];
        AppendValue(outBuffer, groundItem.entity);
        AppendValue(outBuffer, groundItem.position.x);
        AppendValue(outBuffer, groundItem.position.y);
        AppendValue(outBuffer, groundItem.position.z);
        AppendValue(outBuffer, groundItem.charges);
        writeString(groundItem.itemId, 128);
        writeString(groundItem.addonAId, 128);
        writeString(groundItem.addonBId, 128);
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

    auto readString = [&](std::string& outValue) {
        std::uint16_t length = 0;
        if (!ReadValue(buffer, offset, length))
        {
            return false;
        }
        if (offset + length > buffer.size())
        {
            return false;
        }
        outValue.assign(reinterpret_cast<const char*>(buffer.data() + offset), length);
        offset += length;
        return true;
    };

    if (!readString(outSnapshot.survivorCharacterId) ||
        !readString(outSnapshot.killerCharacterId) ||
        !readString(outSnapshot.survivorItemId) ||
        !readString(outSnapshot.survivorItemAddonA) ||
        !readString(outSnapshot.survivorItemAddonB) ||
        !readString(outSnapshot.killerPowerId) ||
        !readString(outSnapshot.killerPowerAddonA) ||
        !readString(outSnapshot.killerPowerAddonB))
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
    std::uint8_t chaseInCenterFOVByte = 0;
    if (!ReadValue(buffer, offset, outSnapshot.survivorState) ||
        !ReadValue(buffer, offset, outSnapshot.killerAttackState) ||
        !ReadValue(buffer, offset, outSnapshot.killerAttackStateTimer) ||
        !ReadValue(buffer, offset, outSnapshot.killerLungeCharge) ||
        !ReadValue(buffer, offset, chaseActiveByte) ||
        !ReadValue(buffer, offset, outSnapshot.chaseDistance) ||
        !ReadValue(buffer, offset, chaseLosByte) ||
        !ReadValue(buffer, offset, chaseInCenterFOVByte) ||
        !ReadValue(buffer, offset, outSnapshot.chaseTimeSinceLOS) ||
        !ReadValue(buffer, offset, outSnapshot.chaseTimeSinceCenterFOV) ||
        !ReadValue(buffer, offset, outSnapshot.chaseTimeInChase) ||
        !ReadValue(buffer, offset, outSnapshot.bloodlustTier) ||
        !ReadValue(buffer, offset, outSnapshot.survivorItemCharges) ||
        !ReadValue(buffer, offset, outSnapshot.survivorItemActive) ||
        !ReadValue(buffer, offset, outSnapshot.survivorItemUsesRemaining) ||
        !ReadValue(buffer, offset, outSnapshot.wraithCloaked) ||
        !ReadValue(buffer, offset, outSnapshot.wraithTransitionTimer) ||
        !ReadValue(buffer, offset, outSnapshot.wraithPostUncloakTimer) ||
        !ReadValue(buffer, offset, outSnapshot.killerBlindTimer) ||
        !ReadValue(buffer, offset, outSnapshot.killerBlindStyleWhite) ||
        !ReadValue(buffer, offset, outSnapshot.carriedTrapCount))
    {
        return false;
    }

    outSnapshot.chaseActive = chaseActiveByte != 0;
    outSnapshot.chaseLos = chaseLosByte != 0;
    outSnapshot.chaseInCenterFOV = chaseInCenterFOVByte != 0;

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

    std::uint16_t trapCount = 0;
    if (!ReadValue(buffer, offset, trapCount))
    {
        return false;
    }
    outSnapshot.traps.clear();
    outSnapshot.traps.reserve(trapCount);
    for (std::uint16_t i = 0; i < trapCount; ++i)
    {
        game::gameplay::GameplaySystems::TrapSnapshot trap;
        if (!ReadValue(buffer, offset, trap.entity) ||
            !ReadValue(buffer, offset, trap.state) ||
            !ReadValue(buffer, offset, trap.trappedEntity) ||
            !ReadValue(buffer, offset, trap.position.x) ||
            !ReadValue(buffer, offset, trap.position.y) ||
            !ReadValue(buffer, offset, trap.position.z) ||
            !ReadValue(buffer, offset, trap.halfExtents.x) ||
            !ReadValue(buffer, offset, trap.halfExtents.y) ||
            !ReadValue(buffer, offset, trap.halfExtents.z) ||
            !ReadValue(buffer, offset, trap.escapeChance) ||
            !ReadValue(buffer, offset, trap.escapeAttempts) ||
            !ReadValue(buffer, offset, trap.maxEscapeAttempts))
        {
            return false;
        }
        outSnapshot.traps.push_back(trap);
    }

    std::uint16_t groundItemCount = 0;
    if (!ReadValue(buffer, offset, groundItemCount))
    {
        return false;
    }
    outSnapshot.groundItems.clear();
    outSnapshot.groundItems.reserve(groundItemCount);
    for (std::uint16_t i = 0; i < groundItemCount; ++i)
    {
        game::gameplay::GameplaySystems::GroundItemSnapshot groundItem;
        if (!ReadValue(buffer, offset, groundItem.entity) ||
            !ReadValue(buffer, offset, groundItem.position.x) ||
            !ReadValue(buffer, offset, groundItem.position.y) ||
            !ReadValue(buffer, offset, groundItem.position.z) ||
            !ReadValue(buffer, offset, groundItem.charges))
        {
            return false;
        }
        if (!readString(groundItem.itemId) ||
            !readString(groundItem.addonAId) ||
            !readString(groundItem.addonBId))
        {
            return false;
        }
        outSnapshot.groundItems.push_back(groundItem);
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
    AppendValue(outBuffer, tuning.generatorRepairSecondsBase);
    AppendValue(outBuffer, tuning.medkitFullHealCharges);
    AppendValue(outBuffer, tuning.medkitHealSpeedMultiplier);
    AppendValue(outBuffer, tuning.toolboxCharges);
    AppendValue(outBuffer, tuning.toolboxChargeDrainPerSecond);
    AppendValue(outBuffer, tuning.toolboxRepairSpeedBonus);
    AppendValue(outBuffer, tuning.flashlightMaxUseSeconds);
    AppendValue(outBuffer, tuning.flashlightBlindBuildSeconds);
    AppendValue(outBuffer, tuning.flashlightBlindDurationSeconds);
    AppendValue(outBuffer, tuning.flashlightBeamRange);
    AppendValue(outBuffer, tuning.flashlightBeamAngleDegrees);
    AppendValue(outBuffer, tuning.flashlightBlindStyle);
    AppendValue(outBuffer, tuning.mapChannelSeconds);
    AppendValue(outBuffer, tuning.mapUses);
    AppendValue(outBuffer, tuning.mapRevealRangeMeters);
    AppendValue(outBuffer, tuning.mapRevealDurationSeconds);
    AppendValue(outBuffer, tuning.trapperStartCarryTraps);
    AppendValue(outBuffer, tuning.trapperMaxCarryTraps);
    AppendValue(outBuffer, tuning.trapperGroundSpawnTraps);
    AppendValue(outBuffer, tuning.trapperSetTrapSeconds);
    AppendValue(outBuffer, tuning.trapperDisarmSeconds);
    AppendValue(outBuffer, tuning.trapEscapeBaseChance);
    AppendValue(outBuffer, tuning.trapEscapeChanceStep);
    AppendValue(outBuffer, tuning.trapEscapeChanceMax);
    AppendValue(outBuffer, tuning.trapKillerStunSeconds);
    AppendValue(outBuffer, tuning.wraithCloakMoveSpeedMultiplier);
    AppendValue(outBuffer, tuning.wraithCloakTransitionSeconds);
    AppendValue(outBuffer, tuning.wraithUncloakTransitionSeconds);
    AppendValue(outBuffer, tuning.wraithPostUncloakHasteSeconds);
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
           ReadValue(buffer, offset, outTuning.generatorRepairSecondsBase) &&
           ReadValue(buffer, offset, outTuning.medkitFullHealCharges) &&
           ReadValue(buffer, offset, outTuning.medkitHealSpeedMultiplier) &&
           ReadValue(buffer, offset, outTuning.toolboxCharges) &&
           ReadValue(buffer, offset, outTuning.toolboxChargeDrainPerSecond) &&
           ReadValue(buffer, offset, outTuning.toolboxRepairSpeedBonus) &&
           ReadValue(buffer, offset, outTuning.flashlightMaxUseSeconds) &&
           ReadValue(buffer, offset, outTuning.flashlightBlindBuildSeconds) &&
           ReadValue(buffer, offset, outTuning.flashlightBlindDurationSeconds) &&
           ReadValue(buffer, offset, outTuning.flashlightBeamRange) &&
           ReadValue(buffer, offset, outTuning.flashlightBeamAngleDegrees) &&
           ReadValue(buffer, offset, outTuning.flashlightBlindStyle) &&
           ReadValue(buffer, offset, outTuning.mapChannelSeconds) &&
           ReadValue(buffer, offset, outTuning.mapUses) &&
           ReadValue(buffer, offset, outTuning.mapRevealRangeMeters) &&
           ReadValue(buffer, offset, outTuning.mapRevealDurationSeconds) &&
           ReadValue(buffer, offset, outTuning.trapperStartCarryTraps) &&
           ReadValue(buffer, offset, outTuning.trapperMaxCarryTraps) &&
           ReadValue(buffer, offset, outTuning.trapperGroundSpawnTraps) &&
           ReadValue(buffer, offset, outTuning.trapperSetTrapSeconds) &&
           ReadValue(buffer, offset, outTuning.trapperDisarmSeconds) &&
           ReadValue(buffer, offset, outTuning.trapEscapeBaseChance) &&
           ReadValue(buffer, offset, outTuning.trapEscapeChanceStep) &&
           ReadValue(buffer, offset, outTuning.trapEscapeChanceMax) &&
           ReadValue(buffer, offset, outTuning.trapKillerStunSeconds) &&
           ReadValue(buffer, offset, outTuning.wraithCloakMoveSpeedMultiplier) &&
           ReadValue(buffer, offset, outTuning.wraithCloakTransitionSeconds) &&
           ReadValue(buffer, offset, outTuning.wraithUncloakTransitionSeconds) &&
           ReadValue(buffer, offset, outTuning.wraithPostUncloakHasteSeconds) &&
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

bool App::LoadAudioConfig()
{
    m_audioSettings = AudioSettings{};

    std::filesystem::create_directories("config");
    const std::filesystem::path path = std::filesystem::path("config") / "audio.json";
    if (!std::filesystem::exists(path))
    {
        return SaveAudioConfig();
    }

    std::ifstream stream(path);
    if (!stream.is_open())
    {
        m_audioStatus = "Failed to open audio config.";
        return false;
    }

    json root;
    try
    {
        stream >> root;
    }
    catch (const std::exception&)
    {
        m_audioStatus = "Invalid audio config. Using defaults.";
        return SaveAudioConfig();
    }

    auto readFloat = [&](const char* key, float& target) {
        if (root.contains(key) && root[key].is_number())
        {
            target = root[key].get<float>();
        }
    };
    auto readBool = [&](const char* key, bool& target) {
        if (root.contains(key) && root[key].is_boolean())
        {
            target = root[key].get<bool>();
        }
    };

    readFloat("master", m_audioSettings.master);
    readFloat("music", m_audioSettings.music);
    readFloat("sfx", m_audioSettings.sfx);
    readFloat("ui", m_audioSettings.ui);
    readFloat("ambience", m_audioSettings.ambience);
    readBool("muted", m_audioSettings.muted);
    readFloat("killer_light_red", m_audioSettings.killerLightRed);
    readFloat("killer_light_green", m_audioSettings.killerLightGreen);
    readFloat("killer_light_blue", m_audioSettings.killerLightBlue);

    m_audioSettings.master = glm::clamp(m_audioSettings.master, 0.0F, 1.0F);
    m_audioSettings.music = glm::clamp(m_audioSettings.music, 0.0F, 1.0F);
    m_audioSettings.sfx = glm::clamp(m_audioSettings.sfx, 0.0F, 1.0F);
    m_audioSettings.ui = glm::clamp(m_audioSettings.ui, 0.0F, 1.0F);
    m_audioSettings.ambience = glm::clamp(m_audioSettings.ambience, 0.0F, 1.0F);
    return true;
}

bool App::SaveAudioConfig() const
{
    std::filesystem::create_directories("config");
    const std::filesystem::path path = std::filesystem::path("config") / "audio.json";

    json root;
    root["asset_version"] = m_audioSettings.assetVersion;
    root["master"] = m_audioSettings.master;
    root["music"] = m_audioSettings.music;
    root["sfx"] = m_audioSettings.sfx;
    root["ui"] = m_audioSettings.ui;
    root["ambience"] = m_audioSettings.ambience;
    root["muted"] = m_audioSettings.muted;
    root["killer_light_red"] = m_audioSettings.killerLightRed;
    root["killer_light_green"] = m_audioSettings.killerLightGreen;
    root["killer_light_blue"] = m_audioSettings.killerLightBlue;

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
    readFloat("generator_repair_seconds_base", m_gameplayApplied.generatorRepairSecondsBase);
    readFloat("medkit_full_heal_charges", m_gameplayApplied.medkitFullHealCharges);
    readFloat("medkit_heal_speed_multiplier", m_gameplayApplied.medkitHealSpeedMultiplier);
    readFloat("toolbox_charges", m_gameplayApplied.toolboxCharges);
    readFloat("toolbox_charge_drain_per_second", m_gameplayApplied.toolboxChargeDrainPerSecond);
    readFloat("toolbox_repair_speed_bonus", m_gameplayApplied.toolboxRepairSpeedBonus);
    readFloat("flashlight_max_use_seconds", m_gameplayApplied.flashlightMaxUseSeconds);
    readFloat("flashlight_blind_build_seconds", m_gameplayApplied.flashlightBlindBuildSeconds);
    readFloat("flashlight_blind_duration_seconds", m_gameplayApplied.flashlightBlindDurationSeconds);
    readFloat("flashlight_beam_range", m_gameplayApplied.flashlightBeamRange);
    readFloat("flashlight_beam_angle_degrees", m_gameplayApplied.flashlightBeamAngleDegrees);
    readInt("flashlight_blind_style", m_gameplayApplied.flashlightBlindStyle);
    readFloat("map_channel_seconds", m_gameplayApplied.mapChannelSeconds);
    readInt("map_uses", m_gameplayApplied.mapUses);
    readFloat("map_reveal_range_meters", m_gameplayApplied.mapRevealRangeMeters);
    readFloat("map_reveal_duration_seconds", m_gameplayApplied.mapRevealDurationSeconds);
    readInt("trapper_start_carry_traps", m_gameplayApplied.trapperStartCarryTraps);
    readInt("trapper_max_carry_traps", m_gameplayApplied.trapperMaxCarryTraps);
    readInt("trapper_ground_spawn_traps", m_gameplayApplied.trapperGroundSpawnTraps);
    readFloat("trapper_set_trap_seconds", m_gameplayApplied.trapperSetTrapSeconds);
    readFloat("trapper_disarm_seconds", m_gameplayApplied.trapperDisarmSeconds);
    readFloat("trap_escape_base_chance", m_gameplayApplied.trapEscapeBaseChance);
    readFloat("trap_escape_chance_step", m_gameplayApplied.trapEscapeChanceStep);
    readFloat("trap_escape_chance_max", m_gameplayApplied.trapEscapeChanceMax);
    readFloat("trap_killer_stun_seconds", m_gameplayApplied.trapKillerStunSeconds);
    readFloat("wraith_cloak_move_speed_multiplier", m_gameplayApplied.wraithCloakMoveSpeedMultiplier);
    readFloat("wraith_cloak_transition_seconds", m_gameplayApplied.wraithCloakTransitionSeconds);
    readFloat("wraith_uncloak_transition_seconds", m_gameplayApplied.wraithUncloakTransitionSeconds);
    readFloat("wraith_post_uncloak_haste_seconds", m_gameplayApplied.wraithPostUncloakHasteSeconds);
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
    root["generator_repair_seconds_base"] = t.generatorRepairSecondsBase;
    root["medkit_full_heal_charges"] = t.medkitFullHealCharges;
    root["medkit_heal_speed_multiplier"] = t.medkitHealSpeedMultiplier;
    root["toolbox_charges"] = t.toolboxCharges;
    root["toolbox_charge_drain_per_second"] = t.toolboxChargeDrainPerSecond;
    root["toolbox_repair_speed_bonus"] = t.toolboxRepairSpeedBonus;
    root["flashlight_max_use_seconds"] = t.flashlightMaxUseSeconds;
    root["flashlight_blind_build_seconds"] = t.flashlightBlindBuildSeconds;
    root["flashlight_blind_duration_seconds"] = t.flashlightBlindDurationSeconds;
    root["flashlight_beam_range"] = t.flashlightBeamRange;
    root["flashlight_beam_angle_degrees"] = t.flashlightBeamAngleDegrees;
    root["flashlight_blind_style"] = t.flashlightBlindStyle;
    root["map_channel_seconds"] = t.mapChannelSeconds;
    root["map_uses"] = t.mapUses;
    root["map_reveal_range_meters"] = t.mapRevealRangeMeters;
    root["map_reveal_duration_seconds"] = t.mapRevealDurationSeconds;
    root["trapper_start_carry_traps"] = t.trapperStartCarryTraps;
    root["trapper_max_carry_traps"] = t.trapperMaxCarryTraps;
    root["trapper_ground_spawn_traps"] = t.trapperGroundSpawnTraps;
    root["trapper_set_trap_seconds"] = t.trapperSetTrapSeconds;
    root["trapper_disarm_seconds"] = t.trapperDisarmSeconds;
    root["trap_escape_base_chance"] = t.trapEscapeBaseChance;
    root["trap_escape_chance_step"] = t.trapEscapeChanceStep;
    root["trap_escape_chance_max"] = t.trapEscapeChanceMax;
    root["trap_killer_stun_seconds"] = t.trapKillerStunSeconds;
    root["wraith_cloak_move_speed_multiplier"] = t.wraithCloakMoveSpeedMultiplier;
    root["wraith_cloak_transition_seconds"] = t.wraithCloakTransitionSeconds;
    root["wraith_uncloak_transition_seconds"] = t.wraithUncloakTransitionSeconds;
    root["wraith_post_uncloak_haste_seconds"] = t.wraithPostUncloakHasteSeconds;
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

void App::ApplyAudioSettings()
{
    m_audioSettings.master = glm::clamp(m_audioSettings.master, 0.0F, 1.0F);
    m_audioSettings.music = glm::clamp(m_audioSettings.music, 0.0F, 1.0F);
    m_audioSettings.sfx = glm::clamp(m_audioSettings.sfx, 0.0F, 1.0F);
    m_audioSettings.ui = glm::clamp(m_audioSettings.ui, 0.0F, 1.0F);
    m_audioSettings.ambience = glm::clamp(m_audioSettings.ambience, 0.0F, 1.0F);

    const float muteMul = m_audioSettings.muted ? 0.0F : 1.0F;
    m_audio.SetBusVolume(audio::AudioSystem::Bus::Master, m_audioSettings.master * muteMul);
    m_audio.SetBusVolume(audio::AudioSystem::Bus::Music, m_audioSettings.music);
    m_audio.SetBusVolume(audio::AudioSystem::Bus::Sfx, m_audioSettings.sfx);
    m_audio.SetBusVolume(audio::AudioSystem::Bus::Ui, m_audioSettings.ui);
    m_audio.SetBusVolume(audio::AudioSystem::Bus::Ambience, m_audioSettings.ambience);
}

bool App::LoadTerrorRadiusProfile(const std::string& killerId)
{
    StopTerrorRadiusAudio();
    m_terrorAudioProfile = TerrorRadiusProfileAudio{};
    m_terrorAudioProfile.killerId = killerId.empty() ? "default_killer" : killerId;

    const std::filesystem::path dir = std::filesystem::path("assets") / "terror_radius";
    std::filesystem::create_directories(dir);
    const std::filesystem::path path = dir / (m_terrorAudioProfile.killerId + ".json");

    if (!std::filesystem::exists(path))
    {
        json defaults;
        defaults["asset_version"] = 1;
        defaults["killer_id"] = m_terrorAudioProfile.killerId;
        defaults["base_radius"] = 32.0F;  // DBD-like: 32m default TR radius
        defaults["smoothing_time"] = 0.25F; // Crossfade duration 0.15-0.35s
        defaults["layers"] = json::array({
            json{{"clip", "tr_far"}, {"fade_in_start", 0.0F}, {"fade_in_end", 0.45F}, {"gain", 0.15F}},
            json{{"clip", "tr_mid"}, {"fade_in_start", 0.25F}, {"fade_in_end", 0.75F}, {"gain", 0.2F}},
            json{{"clip", "tr_close"}, {"fade_in_start", 0.55F}, {"fade_in_end", 1.0F}, {"gain", 0.25F}},
            json{{"clip", "tr_chase"}, {"fade_in_start", 0.0F}, {"fade_in_end", 1.0F}, {"gain", 0.25F}, {"chase_only", true}},
        });
        std::ofstream out(path);
        if (out.is_open())
        {
            out << defaults.dump(2) << "\n";
        }
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

    if (root.contains("base_radius") && root["base_radius"].is_number())
    {
        m_terrorAudioProfile.baseRadius = glm::clamp(root["base_radius"].get<float>(), 4.0F, 120.0F);
    }
    if (root.contains("smoothing_time") && root["smoothing_time"].is_number())
    {
        m_terrorAudioProfile.smoothingTime = glm::clamp(root["smoothing_time"].get<float>(), 0.15F, 0.35F);
    }
    if (root.contains("layers") && root["layers"].is_array())
    {
        for (const auto& layerJson : root["layers"])
        {
            if (!layerJson.is_object())
            {
                continue;
            }
            TerrorRadiusLayerAudio layer{};
            if (layerJson.contains("clip") && layerJson["clip"].is_string())
            {
                layer.clip = layerJson["clip"].get<std::string>();
            }
            if (layer.clip.empty())
            {
                continue;
            }
            if (layerJson.contains("fade_in_start") && layerJson["fade_in_start"].is_number())
            {
                layer.fadeInStart = glm::clamp(layerJson["fade_in_start"].get<float>(), 0.0F, 1.0F);
            }
            if (layerJson.contains("fade_in_end") && layerJson["fade_in_end"].is_number())
            {
                layer.fadeInEnd = glm::clamp(layerJson["fade_in_end"].get<float>(), 0.0F, 1.0F);
            }
            if (layer.fadeInEnd < layer.fadeInStart)
            {
                std::swap(layer.fadeInEnd, layer.fadeInStart);
            }
            if (layerJson.contains("gain") && layerJson["gain"].is_number())
            {
                layer.gain = glm::clamp(layerJson["gain"].get<float>(), 0.0F, 1.0F);
            }
            if (layerJson.contains("chase_only") && layerJson["chase_only"].is_boolean())
            {
                layer.chaseOnly = layerJson["chase_only"].get<bool>();
            }
            m_terrorAudioProfile.layers.push_back(layer);
        }
    }

    // First pass: Start all TR layers at 0 volume
    for (TerrorRadiusLayerAudio& layer : m_terrorAudioProfile.layers)
    {
        const audio::AudioSystem::PlayOptions options{};
        layer.handle = m_audio.PlayLoop(layer.clip, audio::AudioSystem::Bus::Music, options);
        layer.currentVolume = 0.0F;
        std::cout << "[TR Load] clip=" << layer.clip << " handle=" << layer.handle << "\n";
        if (layer.handle != 0)
        {
            (void)m_audio.SetHandleVolume(layer.handle, 0.0F);
        }
        else
        {
            std::cerr << "[TR Load] Failed to load layer: " << layer.clip << "\n";
        }
    }

    // Second pass: Sync all layers to the same playback cursor
    // This prevents phase jumps when switching to chase music
    audio::AudioSystem::SoundHandle firstHandle = 0;
    std::uint64_t referenceCursor = 0;

    for (const TerrorRadiusLayerAudio& layer : m_terrorAudioProfile.layers)
    {
        if (layer.handle != 0)
        {
            if (firstHandle == 0)
            {
                // Use first successfully loaded layer as reference
                firstHandle = layer.handle;
                referenceCursor = m_audio.GetSoundCursorInPcmFrames(layer.handle);
                std::cout << "[TR Load] Reference handle=" << layer.handle << " cursor=" << referenceCursor << "\n";
            }
            else
            {
                // Sync all other layers to the reference cursor
                (void)m_audio.SeekSoundToPcmFrame(layer.handle, referenceCursor);
                std::cout << "[TR Load] Synced " << layer.clip << " to cursor=" << referenceCursor << "\n";
            }
        }
    }

    m_terrorAudioProfile.loaded = !m_terrorAudioProfile.layers.empty();
    std::cout << "[TR Load] Loaded " << m_terrorAudioProfile.layers.size() << " layers, loaded=" << m_terrorAudioProfile.loaded << "\n";
    return m_terrorAudioProfile.loaded;
}

void App::StopTerrorRadiusAudio()
{
    for (TerrorRadiusLayerAudio& layer : m_terrorAudioProfile.layers)
    {
        if (layer.handle != 0)
        {
            m_audio.Stop(layer.handle);
            layer.handle = 0;
        }
        layer.currentVolume = 0.0F;
    }
    m_terrorAudioProfile.layers.clear();
    m_terrorAudioProfile.loaded = false;
}

void App::UpdateTerrorRadiusAudio(float deltaSeconds)
{
    if (!m_terrorAudioProfile.loaded || m_appMode != AppMode::InGame)
    {
        return;
    }

    // Phase B1: Audio routing based on local player role
    // Survivor hears: TR bands (far/mid/close) + chase override
    // Killer hears: ONLY chase music when in chase
    const bool localPlayerIsSurvivor = (m_localPlayer.controlledRole == "survivor");
    const bool localPlayerIsKiller = (m_localPlayer.controlledRole == "killer");
    (void)localPlayerIsSurvivor;  // Used for early-exit logic clarity

    const bool hasSurvivor = m_gameplay.RoleEntity("survivor") != 0;
    const bool hasKiller = m_gameplay.RoleEntity("killer") != 0;

    if (!hasSurvivor || !hasKiller)
    {
        // Fade out all layers if one entity is missing
        for (TerrorRadiusLayerAudio& layer : m_terrorAudioProfile.layers)
        {
            layer.currentVolume = 0.0F;
            if (layer.handle != 0)
            {
                (void)m_audio.SetHandleVolume(layer.handle, 0.0F);
            }
        }
        m_currentBand = TerrorRadiusBand::Outside;
        m_chaseWasActive = false;
        return;
    }

    // Early exit for Killer: only chase layer matters
    if (localPlayerIsKiller)
    {
        const bool chaseActive = m_gameplay.BuildHudState().chaseActive;
        const float smooth = glm::clamp(deltaSeconds / m_terrorAudioProfile.smoothingTime, 0.0F, 1.0F);

        for (TerrorRadiusLayerAudio& layer : m_terrorAudioProfile.layers)
        {
            float targetVolume = 0.0F;
            // Killer only hears chase music when actively chasing
            if (layer.chaseOnly && chaseActive)
            {
                targetVolume = layer.gain;
            }
            // All TR distance-based bands are silent for killer
            layer.currentVolume = glm::mix(layer.currentVolume, targetVolume, smooth);
            if (layer.handle != 0)
            {
                (void)m_audio.SetHandleVolume(layer.handle, layer.currentVolume);
            }
        }
        // Don't update band state for killer (not relevant)
        return;
    }

    // Calculate XZ (horizontal) distance from Survivor to Killer
    const glm::vec3 survivor = m_gameplay.RolePosition("survivor");
    const glm::vec3 killer = m_gameplay.RolePosition("killer");
    const glm::vec2 delta{survivor.x - killer.x, survivor.z - killer.z};
    const float distance = glm::length(delta);
    const float radius = std::max(1.0F, m_terrorAudioProfile.baseRadius);
    const bool chaseActive = m_gameplay.BuildHudState().chaseActive;

    // Track chase state transitions for anti-leak guard
    const bool justEnteredChase = chaseActive && !m_chaseWasActive;
    m_chaseWasActive = chaseActive;

    // DBD-like stepped bands (no gradient!)
    // FAR:  0.66R < dist <= R       (outer edge, weakest)
    // MID:  0.33R < dist <= 0.66R   (middle)
    // CLOSE: 0 <= dist <= 0.33R       (closest, strongest)
    TerrorRadiusBand newBand = TerrorRadiusBand::Outside;
    if (distance <= radius * 0.333333F)
    {
        newBand = TerrorRadiusBand::Close;
    }
    else if (distance <= radius * 0.666667F)
    {
        newBand = TerrorRadiusBand::Mid;
    }
    else if (distance <= radius)
    {
        newBand = TerrorRadiusBand::Far;
    }
    else
    {
        newBand = TerrorRadiusBand::Outside;
    }

    m_currentBand = newBand;

    // Normal smoothing factor (0.15-0.35s)
    const float smooth = glm::clamp(deltaSeconds / m_terrorAudioProfile.smoothingTime, 0.0F, 1.0F);

    // Anti-leak rapid fade-out for entering chase (0.05s instead of normal smoothing)
    const float rapidSmooth = glm::clamp(deltaSeconds / 0.05F, 0.0F, 1.0F);

    // Update each layer based on stepped band logic and chase override
    for (TerrorRadiusLayerAudio& layer : m_terrorAudioProfile.layers)
    {
        float targetVolume = 0.0F;

        // ============================================================
        // MUTUALLY EXCLUSIVE: Chase suppression logic BEFORE band logic
        // ============================================================
        if (layer.chaseOnly)
        {
            // Chase layer (tr_chase): ON during chase, OFF otherwise
            targetVolume = chaseActive ? layer.gain : 0.0F;
        }
        else
        {
            // Distance-based layers (tr_far, tr_mid, tr_close)

            // Stepped band logic - each layer is fully ON or OFF based on its designated band
            // Layer names must contain "far", "mid", "close" to identify their band
            const std::string lowerClip = [&layer]() {
                std::string lower = layer.clip;
                std::transform(lower.begin(), lower.end(), lower.begin(),
                    [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                return lower;
            }();

            if (lowerClip.find("far") != std::string::npos)
            {
                // FAR layer: ON only in FAR band (continues during chase for ambience)
                targetVolume = (newBand == TerrorRadiusBand::Far) ? layer.gain : 0.0F;
            }
            else if (lowerClip.find("mid") != std::string::npos)
            {
                // MID layer: ON only in MID band (continues during chase)
                targetVolume = (newBand == TerrorRadiusBand::Mid) ? layer.gain : 0.0F;
            }
            else if (lowerClip.find("close") != std::string::npos)
            {
                // CLOSE layer: MUST BE SUPPRESSED during chase (replaced by chase music)
                // This suppression is based ONLY on chaseActive, NOT on LOS/FOV timers
                if (chaseActive)
                {
                    targetVolume = 0.0F; // FORCE SUPPRESS CLOSE during chase
                }
                else
                {
                    targetVolume = (newBand == TerrorRadiusBand::Close) ? layer.gain : 0.0F;
                }
            }
            else
            {
                // Unknown layer - use old gradient behavior as fallback
                const float intensity = glm::clamp(1.0F - distance / radius, 0.0F, 1.0F);
                if (layer.fadeInEnd <= layer.fadeInStart + 1.0e-4F)
                {
                    targetVolume = intensity >= layer.fadeInStart ? layer.gain : 0.0F;
                }
                else
                {
                    targetVolume = glm::clamp((intensity - layer.fadeInStart) / (layer.fadeInEnd - layer.fadeInStart), 0.0F, 1.0F) * layer.gain;
                }
            }
        }

        // ============================================================
        // Apply smoothing with anti-leak guard for chase transitions
        // ============================================================
        float actualSmooth = smooth;

        // Anti-leak: When entering chase, fade out non-chase layers rapidly
        if (justEnteredChase && !layer.chaseOnly)
        {
            actualSmooth = rapidSmooth;
        }

        // Apply smoothing (AFTER suppression logic)
        layer.currentVolume = glm::mix(layer.currentVolume, targetVolume, actualSmooth);
        if (layer.handle != 0)
        {
            (void)m_audio.SetHandleVolume(layer.handle, layer.currentVolume);
        }
    }
}

std::string App::DumpTerrorRadiusState() const
{
    std::string out = "=== Terror Radius State ===\n";

    // Phase B1: Local role and audio routing info
    out += "Local Role: " + m_localPlayer.controlledRole + "\n";
    const bool localPlayerIsSurvivor = (m_localPlayer.controlledRole == "survivor");
    const bool localPlayerIsKiller = (m_localPlayer.controlledRole == "killer");
    out += "TR Enabled: " + std::string(localPlayerIsSurvivor ? "YES" : "NO") + "\n";
    if (localPlayerIsKiller)
    {
        const auto hudState = m_gameplay.BuildHudState();
        out += "Chase Enabled for Killer: " + std::string(hudState.chaseActive ? "YES" : "NO") + "\n";
    }

    // Band name
    const char* bandName = "OUTSIDE";
    switch (m_currentBand)
    {
        case TerrorRadiusBand::Outside: bandName = "OUTSIDE"; break;
        case TerrorRadiusBand::Far: bandName = "FAR"; break;
        case TerrorRadiusBand::Mid: bandName = "MID"; break;
        case TerrorRadiusBand::Close: bandName = "CLOSE"; break;
    }
    out += "Band: ";
    out += bandName;
    out += "\n";

    // Radius
    out += "Base Radius: " + std::to_string(m_terrorAudioProfile.baseRadius) + " m\n";
    out += "Smoothing Time: " + std::to_string(m_terrorAudioProfile.smoothingTime) + " s\n";

    // Distance info
    const bool hasSurvivor = m_gameplay.RoleEntity("survivor") != 0;
    const bool hasKiller = m_gameplay.RoleEntity("killer") != 0;
    if (hasSurvivor && hasKiller)
    {
        const glm::vec3 survivor = m_gameplay.RolePosition("survivor");
        const glm::vec3 killer = m_gameplay.RolePosition("killer");
        const glm::vec2 delta{survivor.x - killer.x, survivor.z - killer.z};
        const float distance = glm::length(delta);
        out += "Distance: " + std::to_string(distance) + " m\n";
    }

    // Chase state
    const auto hudState = m_gameplay.BuildHudState();
    out += std::string("Chase Active: ") + (hudState.chaseActive ? "YES" : "NO") + "\n";

    // Bus volume
    const float musicBusVol = m_audio.GetBusVolume(audio::AudioSystem::Bus::Music);
    out += "Music Bus Volume: " + std::to_string(musicBusVol) + "\n";

    // Per-layer volumes with detailed breakdown
    out += "Layer Volumes:\n";
    for (const TerrorRadiusLayerAudio& layer : m_terrorAudioProfile.layers)
    {
        const float finalApplied = layer.currentVolume * layer.gain * musicBusVol;
        out += "  [" + layer.clip + "]";
        if (layer.chaseOnly)
        {
            out += " (chase_only)";
        }
        out += "\n";

        // Check if this is the close layer and if it's suppressed by chase
        bool isCloseLayer = (layer.clip.find("close") != std::string::npos ||
                           layer.clip.find("Close") != std::string::npos ||
                           layer.clip.find("CLOSE") != std::string::npos);
        if (isCloseLayer && hudState.chaseActive)
        {
            out += "    SUPPRESSED_BY_CHASE\n";
        }

        out += "    profileGain=" + std::to_string(layer.gain) + "\n";
        out += "    currentVolume=" + std::to_string(layer.currentVolume) + "\n";
        out += "    busVolume=" + std::to_string(musicBusVol) + "\n";
        out += "    finalApplied=" + std::to_string(finalApplied) + "\n";
    }

    return out;
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
        m_gameplay.SetMapSpotLightCount(0);
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
    m_runtimeMapPointLights = pointLights;
    m_runtimeMapSpotLights = spotLights;
    m_renderer.SetPointLights(m_runtimeMapPointLights);
    m_renderer.SetSpotLights(m_runtimeMapSpotLights);
    m_gameplay.SetMapSpotLightCount(m_runtimeMapSpotLights.size());

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
    const std::vector<std::string> mapItems{"main_map", "collision_test", "test"};
    const std::vector<std::string> savedMaps = game::editor::LevelAssetIO::ListMapNames();
    const auto survivorCharacters = m_gameplay.ListSurvivorCharacters();
    const auto killerCharacters = m_gameplay.ListKillerCharacters();
    const auto survivorItems = m_gameplay.GetLoadoutCatalog().ListItemIds();
    const auto killerPowers = m_gameplay.GetLoadoutCatalog().ListPowerIds();

    auto makeWithNone = [](const std::vector<std::string>& source) {
        std::vector<std::string> out;
        out.reserve(source.size() + 1);
        out.push_back("none");
        out.insert(out.end(), source.begin(), source.end());
        return out;
    };

    const std::string selectedItemId =
        (!survivorItems.empty() && m_menuSurvivorItemIndex >= 0 && m_menuSurvivorItemIndex < static_cast<int>(survivorItems.size()))
            ? survivorItems[static_cast<std::size_t>(m_menuSurvivorItemIndex)]
            : (survivorItems.empty() ? std::string{} : survivorItems.front());
    const std::string selectedPowerId =
        (!killerPowers.empty() && m_menuKillerPowerIndex >= 0 && m_menuKillerPowerIndex < static_cast<int>(killerPowers.size()))
            ? killerPowers[static_cast<std::size_t>(m_menuKillerPowerIndex)]
            : (killerPowers.empty() ? std::string{} : killerPowers.front());
    const std::vector<std::string> survivorAddonOptions = makeWithNone(
        m_gameplay.GetLoadoutCatalog().ListAddonIdsForTarget(game::gameplay::loadout::TargetKind::Item, selectedItemId)
    );
    const std::vector<std::string> killerAddonOptions = makeWithNone(
        m_gameplay.GetLoadoutCatalog().ListAddonIdsForTarget(game::gameplay::loadout::TargetKind::Power, selectedPowerId)
    );

    auto clampDropdownIndex = [](int& index, std::size_t count) {
        if (count == 0)
        {
            index = -1;
            return;
        }
        if (index < 0 || index >= static_cast<int>(count))
        {
            index = 0;
        }
    };
    clampDropdownIndex(m_menuSurvivorCharacterIndex, survivorCharacters.size());
    clampDropdownIndex(m_menuKillerCharacterIndex, killerCharacters.size());
    clampDropdownIndex(m_menuSurvivorItemIndex, survivorItems.size());
    clampDropdownIndex(m_menuKillerPowerIndex, killerPowers.size());
    clampDropdownIndex(m_menuSurvivorAddonAIndex, survivorAddonOptions.size());
    clampDropdownIndex(m_menuSurvivorAddonBIndex, survivorAddonOptions.size());
    clampDropdownIndex(m_menuKillerAddonAIndex, killerAddonOptions.size());
    clampDropdownIndex(m_menuKillerAddonBIndex, killerAddonOptions.size());

    if (m_menuSavedMapIndex >= static_cast<int>(savedMaps.size()))
    {
        m_menuSavedMapIndex = savedMaps.empty() ? -1 : 0;
    }
    if (m_menuSavedMapIndex < 0 && !savedMaps.empty())
    {
        m_menuSavedMapIndex = 0;
    }

    const float scale = m_ui.Scale();
    const float panelW = std::min(700.0F * scale, static_cast<float>(m_ui.ScreenWidth()) - 20.0F);
    const float panelH = std::min(900.0F * scale, static_cast<float>(m_ui.ScreenHeight()) - 20.0F);
    const engine::ui::UiRect panel{
        (static_cast<float>(m_ui.ScreenWidth()) - panelW) * 0.5F,
        (static_cast<float>(m_ui.ScreenHeight()) - panelH) * 0.5F,
        panelW,
        panelH,
    };

    m_ui.BeginRootPanel("main_menu_custom", panel, true);
    const float scrollHeight = std::max(200.0F * scale, panel.h - 24.0F * scale);
    m_ui.BeginScrollRegion("main_menu_scroll_region", scrollHeight, &m_mainMenuScrollY);
    m_ui.Label("Asymmetric Horror Prototype", 1.2F);
    m_ui.Label("Press ~ for Console | F6 UI test | F10 Legacy UI toggle", m_ui.Theme().colorTextMuted);
    if (m_ui.Button("toggle_legacy_ui", std::string("Legacy ImGui menus: ") + (m_useLegacyImGuiMenus ? "ON" : "OFF")))
    {
        m_useLegacyImGuiMenus = !m_useLegacyImGuiMenus;
    }
    if (m_ui.Button("toggle_ui_test", std::string("UI test panel: ") + (m_showUiTestPanel ? "ON" : "OFF")))
    {
        m_showUiTestPanel = !m_showUiTestPanel;
    }

    if (m_ui.Button("menu_settings", "Settings"))
    {
        m_settingsMenuOpen = true;
        m_settingsOpenedFromPause = false;
    }

    m_ui.Label("Session", m_ui.Theme().colorAccent);
    m_ui.Dropdown("menu_role", "Role", &m_menuRoleIndex, roleItems);
    m_ui.Dropdown("menu_map", "Map", &m_menuMapIndex, mapItems);
    if (!survivorCharacters.empty())
    {
        m_ui.Dropdown("survivor_character", "Survivor Character", &m_menuSurvivorCharacterIndex, survivorCharacters);
    }
    if (!killerCharacters.empty())
    {
        if (m_ui.Dropdown("killer_character", "Killer Character", &m_menuKillerCharacterIndex, killerCharacters))
        {
            const std::string& killerId = killerCharacters[static_cast<std::size_t>(m_menuKillerCharacterIndex)];
            m_gameplay.SetSelectedKillerCharacter(killerId);
            const auto* killerDef = m_gameplay.GetLoadoutCatalog().FindKiller(killerId);
            if (killerDef != nullptr && !killerDef->powerId.empty() && !killerPowers.empty())
            {
                const auto it = std::find(killerPowers.begin(), killerPowers.end(), killerDef->powerId);
                if (it != killerPowers.end())
                {
                    m_menuKillerPowerIndex = static_cast<int>(std::distance(killerPowers.begin(), it));
                }
            }
        }
    }
    if (!survivorItems.empty())
    {
        m_ui.Dropdown("survivor_item", "Survivor Item", &m_menuSurvivorItemIndex, survivorItems);
    }
    if (!survivorAddonOptions.empty())
    {
        m_ui.Dropdown("survivor_item_addon_a", "Survivor Addon A", &m_menuSurvivorAddonAIndex, survivorAddonOptions);
        m_ui.Dropdown("survivor_item_addon_b", "Survivor Addon B", &m_menuSurvivorAddonBIndex, survivorAddonOptions);
    }
    if (!killerPowers.empty())
    {
        m_ui.Dropdown("killer_power", "Killer Power", &m_menuKillerPowerIndex, killerPowers);
    }
    if (!killerAddonOptions.empty())
    {
        m_ui.Dropdown("killer_power_addon_a", "Killer Addon A", &m_menuKillerAddonAIndex, killerAddonOptions);
        m_ui.Dropdown("killer_power_addon_b", "Killer Addon B", &m_menuKillerAddonBIndex, killerAddonOptions);
    }

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

    const std::string roleName = RoleNameFromIndex(m_menuRoleIndex);
    const std::string mapName = MapNameFromIndex(m_menuMapIndex);
    auto applyMenuGameplaySelections = [&]() {
        if (!survivorCharacters.empty())
        {
            m_gameplay.SetSelectedSurvivorCharacter(survivorCharacters[static_cast<std::size_t>(m_menuSurvivorCharacterIndex)]);
        }
        if (!killerCharacters.empty())
        {
            m_gameplay.SetSelectedKillerCharacter(killerCharacters[static_cast<std::size_t>(m_menuKillerCharacterIndex)]);
        }
        const std::string itemId = (!survivorItems.empty() && m_menuSurvivorItemIndex >= 0)
                                       ? survivorItems[static_cast<std::size_t>(m_menuSurvivorItemIndex)]
                                       : std::string{};
        const std::string itemAddonA = (!survivorAddonOptions.empty() && m_menuSurvivorAddonAIndex >= 0)
                                           ? survivorAddonOptions[static_cast<std::size_t>(m_menuSurvivorAddonAIndex)]
                                           : std::string{"none"};
        const std::string itemAddonB = (!survivorAddonOptions.empty() && m_menuSurvivorAddonBIndex >= 0)
                                           ? survivorAddonOptions[static_cast<std::size_t>(m_menuSurvivorAddonBIndex)]
                                           : std::string{"none"};
        m_gameplay.SetSurvivorItemLoadout(
            itemId,
            itemAddonA == "none" ? "" : itemAddonA,
            itemAddonB == "none" ? "" : itemAddonB
        );

        std::string powerId = (!killerPowers.empty() && m_menuKillerPowerIndex >= 0)
                                  ? killerPowers[static_cast<std::size_t>(m_menuKillerPowerIndex)]
                                  : std::string{};
        bool powerForcedByCharacter = false;
        if (!killerCharacters.empty() && m_menuKillerCharacterIndex >= 0 &&
            m_menuKillerCharacterIndex < static_cast<int>(killerCharacters.size()))
        {
            const auto* killerDef = m_gameplay.GetLoadoutCatalog().FindKiller(
                killerCharacters[static_cast<std::size_t>(m_menuKillerCharacterIndex)]
            );
            if (killerDef != nullptr && !killerDef->powerId.empty())
            {
                powerId = killerDef->powerId;
                powerForcedByCharacter = true;
                if (!killerPowers.empty())
                {
                    const auto it = std::find(killerPowers.begin(), killerPowers.end(), powerId);
                    if (it != killerPowers.end())
                    {
                        m_menuKillerPowerIndex = static_cast<int>(std::distance(killerPowers.begin(), it));
                    }
                }
            }
        }
        const std::string powerAddonA = (!killerAddonOptions.empty() && m_menuKillerAddonAIndex >= 0)
                                            ? killerAddonOptions[static_cast<std::size_t>(m_menuKillerAddonAIndex)]
                                            : std::string{"none"};
        const std::string powerAddonB = (!killerAddonOptions.empty() && m_menuKillerAddonBIndex >= 0)
                                            ? killerAddonOptions[static_cast<std::size_t>(m_menuKillerAddonBIndex)]
                                            : std::string{"none"};
        const std::string resolvedPowerAddonA = powerForcedByCharacter ? std::string{"none"} : powerAddonA;
        const std::string resolvedPowerAddonB = powerForcedByCharacter ? std::string{"none"} : powerAddonB;
        if (powerForcedByCharacter)
        {
            m_menuKillerAddonAIndex = 0;
            m_menuKillerAddonBIndex = 0;
        }
        m_gameplay.SetKillerPowerLoadout(
            powerId,
            resolvedPowerAddonA == "none" ? "" : resolvedPowerAddonA,
            resolvedPowerAddonB == "none" ? "" : resolvedPowerAddonB
        );
    };
    if (m_ui.Button("play_solo", "Play Solo", true, &m_ui.Theme().colorSuccess))
    {
        applyMenuGameplaySelections();
        StartSoloSession(mapName, roleName);
    }

    if (!savedMaps.empty())
    {
        m_ui.Dropdown("saved_maps", "Play Saved Map", &m_menuSavedMapIndex, savedMaps);
        if (m_ui.Button("play_saved", "Play Map"))
        {
            applyMenuGameplaySelections();
            StartSoloSession(savedMaps[static_cast<std::size_t>(m_menuSavedMapIndex)], roleName);
        }
    }

    m_ui.Label("Editor", m_ui.Theme().colorAccent);
    if (m_ui.Button("level_editor", "Level Editor"))
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
    if (m_ui.Button("loop_editor", "Loop Editor"))
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

    m_ui.Label("Multiplayer", m_ui.Theme().colorAccent);
    if (m_ui.Button("host_btn", "Host Multiplayer"))
    {
        applyMenuGameplaySelections();
        StartHostSession(mapName, roleName, static_cast<std::uint16_t>(std::clamp(m_menuPort, 1, 65535)));
    }
    if (m_ui.Button("join_btn", "Join Multiplayer"))
    {
        applyMenuGameplaySelections();
        StartJoinSession(m_menuJoinIp, static_cast<std::uint16_t>(std::clamp(m_menuPort, 1, 65535)), roleName);
    }

    if (m_ui.Button("refresh_lan", "Refresh LAN"))
    {
        m_lanDiscovery.ForceScan();
    }
    const auto& servers = m_lanDiscovery.Servers();
    if (servers.empty())
    {
        m_ui.Label("No LAN games found.", m_ui.Theme().colorTextMuted);
    }
    else
    {
        for (std::size_t i = 0; i < servers.size(); ++i)
        {
            const auto& entry = servers[i];
            const bool canJoin = entry.compatible && entry.players < entry.maxPlayers;
            std::string line = "[" + entry.hostName + "] " + entry.ip + ":" + std::to_string(entry.port) + " | Map: " + entry.mapName + " | Players: " +
                               std::to_string(entry.players) + "/" + std::to_string(entry.maxPlayers);
            m_ui.Label(line, canJoin ? m_ui.Theme().colorText : m_ui.Theme().colorTextMuted);
            m_ui.PushIdScope("lan_" + std::to_string(i));
            if (m_ui.Button("join_lan", "Join", canJoin))
            {
                StartJoinSession(entry.ip, entry.port, roleName);
            }
            m_ui.PopIdScope();
        }
    }

    m_ui.Label("Network State: " + NetworkStateToText(m_networkState), m_ui.Theme().colorTextMuted);
    if (!m_menuNetStatus.empty())
    {
        m_ui.Label(m_menuNetStatus, m_ui.Theme().colorTextMuted);
    }
    if (!m_lastNetworkError.empty())
    {
        m_ui.Label("Last Error: " + m_lastNetworkError, m_ui.Theme().colorDanger);
    }
    if (m_ui.Button("quit_game", "Quit", true, &m_ui.Theme().colorDanger))
    {
        *shouldQuit = true;
    }
    m_ui.EndScrollRegion();
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

    m_settingsTabIndex = glm::clamp(m_settingsTabIndex, 0, 3);
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
        if (m_ui.Button("tab_audio", "Audio", true, m_settingsTabIndex == 2 ? &tabColor : nullptr, 200.0F))
        {
            m_settingsTabIndex = 2;
        }
        if (m_ui.Button("tab_gameplay", "Gameplay", true, m_settingsTabIndex == 3 ? &tabColor : nullptr, 200.0F))
        {
            m_settingsTabIndex = 3;
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
    else if (m_settingsTabIndex == 2)
    {
        bool changed = false;
        changed |= m_ui.SliderFloat("audio_master", "Master", &m_audioSettings.master, 0.0F, 1.0F, "%.2f");
        changed |= m_ui.SliderFloat("audio_music", "Music", &m_audioSettings.music, 0.0F, 1.0F, "%.2f");
        changed |= m_ui.SliderFloat("audio_sfx", "SFX", &m_audioSettings.sfx, 0.0F, 1.0F, "%.2f");
        changed |= m_ui.SliderFloat("audio_ui", "UI", &m_audioSettings.ui, 0.0F, 1.0F, "%.2f");
        changed |= m_ui.SliderFloat("audio_amb", "Ambience", &m_audioSettings.ambience, 0.0F, 1.0F, "%.2f");
        changed |= m_ui.Checkbox("audio_mute", "Mute All", &m_audioSettings.muted);
        if (changed)
        {
            ApplyAudioSettings();
        }

        m_ui.PushLayout(engine::ui::UiSystem::LayoutAxis::Horizontal, 8.0F, 0.0F);
        if (m_ui.Button("audio_apply_btn", "Apply", true, &m_ui.Theme().colorSuccess, 170.0F))
        {
            ApplyAudioSettings();
            m_audioStatus = "Applied audio volumes.";
        }
        if (m_ui.Button("audio_save_btn", "Save To File", true, nullptr, 170.0F))
        {
            ApplyAudioSettings();
            m_audioStatus = SaveAudioConfig() ? "Saved to config/audio.json." : "Failed to save audio config.";
        }
        if (m_ui.Button("audio_load_btn", "Load From File", true, nullptr, 170.0F))
        {
            if (LoadAudioConfig())
            {
                ApplyAudioSettings();
                m_audioStatus = "Loaded audio config.";
            }
            else
            {
                m_audioStatus = "Failed to load audio config.";
            }
        }
        if (m_ui.Button("audio_defaults_btn", "Defaults", true, &m_ui.Theme().colorDanger, 170.0F))
        {
            m_audioSettings = AudioSettings{};
            ApplyAudioSettings();
            m_audioStatus = "Audio defaults applied.";
        }
        m_ui.PopLayout();

        m_ui.Label("Clips are resolved from assets/audio by name or explicit file path.", m_ui.Theme().colorTextMuted);
        if (!m_audioStatus.empty())
        {
            m_ui.Label(m_audioStatus, m_ui.Theme().colorTextMuted);
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

        m_ui.Label("Killer Light", m_ui.Theme().colorAccent);
        {
            bool killerLightEnabled = m_gameplay.KillerLookLightEnabled();
            if (m_ui.Checkbox("gp_killer_light_enabled", "Enabled", &killerLightEnabled))
            {
                m_gameplay.SetKillerLookLightEnabled(killerLightEnabled);
            }
        }
        {
            float intensity = m_gameplay.KillerLightIntensity();
            if (m_ui.SliderFloat("gp_killer_light_intensity", "Intensity", &intensity, 0.0F, 5.0F, "%.2f"))
            {
                m_gameplay.SetKillerLookLightIntensity(intensity);
            }
        }
        {
            float range = m_gameplay.KillerLightRange();
            if (m_ui.SliderFloat("gp_killer_light_range", "Range (m)", &range, 1.0F, 50.0F, "%.1f"))
            {
                m_gameplay.SetKillerLookLightRange(range);
            }
        }
        {
            float innerAngle = m_gameplay.KillerLightInnerAngle();
            if (m_ui.SliderFloat("gp_killer_light_inner", "Inner Angle (deg)", &innerAngle, 2.0F, 60.0F, "%.0f"))
            {
                m_gameplay.SetKillerLookLightAngle(innerAngle);
            }
        }
        {
            float outerAngle = m_gameplay.KillerLightOuterAngle();
            if (m_ui.SliderFloat("gp_killer_light_outer", "Outer Angle (deg)", &outerAngle, 5.0F, 90.0F, "%.0f"))
            {
                m_gameplay.SetKillerLookLightOuterAngle(outerAngle);
            }
        }
        {
            float pitch = m_gameplay.KillerLightPitch();
            if (m_ui.SliderFloat("gp_killer_light_pitch", "Pitch (deg, 0=horiz, 90=down)", &pitch, 0.0F, 90.0F, "%.0f"))
            {
                m_gameplay.SetKillerLookLightPitch(pitch);
            }
        }
        {
            bool debug = m_gameplay.KillerLookLightDebug();
            if (m_ui.Checkbox("gp_killer_light_debug", "Debug Overlay", &debug))
            {
                m_gameplay.SetKillerLookLightDebug(debug);
            }
        }

        m_ui.Label("Repair + Healing", m_ui.Theme().colorAccent);
        m_ui.SliderFloat("gp_gen_base_seconds", "Generator Base Seconds", &t.generatorRepairSecondsBase, 20.0F, 180.0F, "%.1f");
        m_ui.SliderFloat("gp_heal_duration", "Heal Duration", &t.healDurationSeconds, 2.0F, 60.0F, "%.1f");
        m_ui.SliderFloat("gp_skillcheck_min", "Skillcheck Min", &t.skillCheckMinInterval, 0.5F, 20.0F, "%.1f");
        m_ui.SliderFloat("gp_skillcheck_max", "Skillcheck Max", &t.skillCheckMaxInterval, 0.5F, 30.0F, "%.1f");

        m_ui.Label("Items: Medkit + Toolbox", m_ui.Theme().colorAccent);
        m_ui.SliderFloat("gp_medkit_full_heal_charges", "Medkit Full Heal Charges", &t.medkitFullHealCharges, 4.0F, 64.0F, "%.1f");
        m_ui.SliderFloat("gp_medkit_heal_mult", "Medkit Heal Speed Mult", &t.medkitHealSpeedMultiplier, 0.5F, 4.0F, "%.2f");
        m_ui.SliderFloat("gp_toolbox_charges", "Toolbox Charges", &t.toolboxCharges, 1.0F, 120.0F, "%.1f");
        m_ui.SliderFloat("gp_toolbox_drain", "Toolbox Drain / sec", &t.toolboxChargeDrainPerSecond, 0.05F, 8.0F, "%.2f");
        m_ui.SliderFloat("gp_toolbox_bonus", "Toolbox Repair Bonus", &t.toolboxRepairSpeedBonus, 0.0F, 3.0F, "%.2f");

        m_ui.Label("Items: Flashlight + Map", m_ui.Theme().colorAccent);
        m_ui.SliderFloat("gp_flashlight_max_use", "Flashlight Max Use (s)", &t.flashlightMaxUseSeconds, 1.0F, 30.0F, "%.2f");
        m_ui.SliderFloat("gp_flashlight_blind_build", "Flashlight Blind Build (s)", &t.flashlightBlindBuildSeconds, 0.1F, 6.0F, "%.2f");
        m_ui.SliderFloat("gp_flashlight_blind_dur", "Flashlight Blind Duration (s)", &t.flashlightBlindDurationSeconds, 0.1F, 8.0F, "%.2f");
        m_ui.SliderFloat("gp_flashlight_range", "Flashlight Range", &t.flashlightBeamRange, 2.0F, 25.0F, "%.1f");
        m_ui.SliderFloat("gp_flashlight_angle", "Flashlight Angle", &t.flashlightBeamAngleDegrees, 5.0F, 80.0F, "%.1f");
        {
            int blindStyle = glm::clamp(t.flashlightBlindStyle, 0, 1);
            std::vector<std::string> blindStyles{"White", "Dark"};
            if (m_ui.Dropdown("gp_flashlight_blind_style", "Flashlight Blind Style", &blindStyle, blindStyles))
            {
                t.flashlightBlindStyle = glm::clamp(blindStyle, 0, 1);
            }
        }
        m_ui.SliderFloat("gp_map_channel", "Map Channel (s)", &t.mapChannelSeconds, 0.05F, 4.0F, "%.2f");
        m_ui.SliderInt("gp_map_uses", "Map Uses", &t.mapUses, 0, 20);
        m_ui.SliderFloat("gp_map_reveal_range", "Map Reveal Range (m)", &t.mapRevealRangeMeters, 4.0F, 120.0F, "%.1f");
        m_ui.SliderFloat("gp_map_reveal_duration", "Map Reveal Duration (s)", &t.mapRevealDurationSeconds, 0.2F, 12.0F, "%.2f");

        m_ui.Label("Killer Powers: Trapper + Wraith", m_ui.Theme().colorAccent);
        m_ui.SliderInt("gp_trapper_start", "Trapper Start Carry", &t.trapperStartCarryTraps, 0, 16);
        m_ui.SliderInt("gp_trapper_max", "Trapper Max Carry", &t.trapperMaxCarryTraps, 1, 16);
        m_ui.SliderInt("gp_trapper_ground", "Trapper Ground Spawn", &t.trapperGroundSpawnTraps, 0, 48);
        m_ui.SliderFloat("gp_trapper_set", "Trapper Set Time (s)", &t.trapperSetTrapSeconds, 0.1F, 6.0F, "%.2f");
        m_ui.SliderFloat("gp_trapper_disarm", "Trapper Disarm Time (s)", &t.trapperDisarmSeconds, 0.1F, 8.0F, "%.2f");
        m_ui.SliderFloat("gp_trap_escape_base", "Trap Escape Base Chance", &t.trapEscapeBaseChance, 0.01F, 0.9F, "%.2f");
        m_ui.SliderFloat("gp_trap_escape_step", "Trap Escape Step", &t.trapEscapeChanceStep, 0.01F, 0.8F, "%.2f");
        m_ui.SliderFloat("gp_trap_escape_max", "Trap Escape Max", &t.trapEscapeChanceMax, 0.05F, 0.98F, "%.2f");
        m_ui.SliderFloat("gp_trap_killer_stun", "Trap Killer Stun (s)", &t.trapKillerStunSeconds, 0.1F, 8.0F, "%.2f");
        m_ui.SliderFloat("gp_wraith_cloak_speed", "Wraith Cloak Speed Mult", &t.wraithCloakMoveSpeedMultiplier, 1.0F, 3.0F, "%.2f");
        m_ui.SliderFloat("gp_wraith_cloak_trans", "Wraith Cloak Transition (s)", &t.wraithCloakTransitionSeconds, 0.1F, 4.0F, "%.2f");
        m_ui.SliderFloat("gp_wraith_uncloak_trans", "Wraith Uncloak Transition (s)", &t.wraithUncloakTransitionSeconds, 0.1F, 4.0F, "%.2f");
        m_ui.SliderFloat("gp_wraith_haste", "Wraith Post-Uncloak Haste (s)", &t.wraithPostUncloakHasteSeconds, 0.0F, 8.0F, "%.2f");

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

    const engine::ui::UiRect topLeft{
        m_hudLayout.topLeftOffset.x * scale,
        m_hudLayout.topLeftOffset.y * scale,
        420.0F * scale,
        260.0F * scale,
    };
    m_ui.BeginPanel("hud_top_left_custom", topLeft, true);
    m_ui.Label(hudState.roleName + " | " + hudState.cameraModeName, 1.05F);
    m_ui.Label("State: " + hudState.survivorStateName + " | Move: " + hudState.movementStateName, m_ui.Theme().colorTextMuted);
    m_ui.Label("Render: " + hudState.renderModeName + " | FPS: " + std::to_string(static_cast<int>(fps)), m_ui.Theme().colorTextMuted);
    m_ui.Label("Speed: " + std::to_string(hudState.playerSpeed) + " | Grounded: " + (hudState.grounded ? "yes" : "no"), m_ui.Theme().colorTextMuted);
    m_ui.Label("Chase: " + std::string(hudState.chaseActive ? "ON" : "OFF"), hudState.chaseActive ? m_ui.Theme().colorDanger : m_ui.Theme().colorTextMuted);
    m_ui.Label(
        "Generators: " + std::to_string(hudState.generatorsCompleted) + "/" + std::to_string(hudState.generatorsTotal),
        m_ui.Theme().colorAccent
    );
    m_ui.Label(
        "Survivor: " + hudState.survivorCharacterId + " | Killer: " + hudState.killerCharacterId,
        m_ui.Theme().colorTextMuted
    );
    m_ui.Label(
        "Item: " + hudState.survivorItemId +
            " [" + hudState.survivorItemAddonA + ", " + hudState.survivorItemAddonB + "]"
            " charges=" + std::to_string(hudState.survivorItemCharges) +
            " uses=" + std::to_string(hudState.survivorItemUsesRemaining),
        hudState.survivorItemActive ? m_ui.Theme().colorAccent : m_ui.Theme().colorTextMuted
    );
    m_ui.Label(
        "Power: " + hudState.killerPowerId +
            " [" + hudState.killerPowerAddonA + ", " + hudState.killerPowerAddonB + "]"
            " traps=" + std::to_string(hudState.activeTrapCount) +
            " carry=" + std::to_string(hudState.carriedTrapCount),
        m_ui.Theme().colorTextMuted
    );
    if (hudState.killerPowerId == "wraith_cloak")
    {
        m_ui.Label(
            std::string("Wraith: ") + (hudState.wraithCloaked ? "CLOAKED" : "VISIBLE") +
                " haste=" + std::to_string(hudState.wraithPostUncloakHasteSeconds),
            hudState.wraithCloaked ? m_ui.Theme().colorAccent : m_ui.Theme().colorTextMuted
        );
    }
    if (hudState.survivorStateName == "Trapped")
    {
        m_ui.Label(
            "TRAPPED attempts: " + std::to_string(hudState.trappedEscapeAttempts) +
                " chance: " + std::to_string(static_cast<int>(hudState.trappedEscapeChance * 100.0F)) + "%",
            m_ui.Theme().colorDanger
        );
    }
    m_ui.EndPanel();

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
    if (hudState.roleName == "Survivor")
    {
        m_ui.Label("RMB: Use Item | R: Drop Item | LMB: Pickup Item | E: Swap Item", m_ui.Theme().colorTextMuted);
    }
    if (hudState.roleName == "Killer")
    {
        m_ui.Label("LMB click: Short | Hold LMB: Lunge", m_ui.Theme().colorTextMuted);
        m_ui.Label("RMB: Power | E: Secondary Power Action", m_ui.Theme().colorTextMuted);
    }
    m_ui.Label("~ Console | F1/F2 Debug | F3 Render", m_ui.Theme().colorTextMuted);
    m_ui.EndPanel();

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

    if (hudState.roleName == "Survivor" && hudState.survivorItemId != "none")
    {
        const engine::ui::UiRect itemPanel{
            18.0F * scale,
            static_cast<float>(m_ui.ScreenHeight()) - 156.0F * scale,
            300.0F * scale,
            138.0F * scale,
        };
        m_ui.BeginPanel("hud_item_corner", itemPanel, true);
        m_ui.Label("Item: " + hudState.survivorItemId, m_ui.Theme().colorAccent);
        const std::string chargeText =
            std::to_string(static_cast<int>(std::round(hudState.survivorItemCharges))) + " / " +
            std::to_string(static_cast<int>(std::round(hudState.survivorItemMaxCharges)));
        m_ui.ProgressBar("hud_item_charges", hudState.survivorItemCharge01, chargeText);
        if (hudState.survivorItemUseProgress01 > 0.0F)
        {
            m_ui.ProgressBar(
                "hud_item_use_progress",
                hudState.survivorItemUseProgress01,
                std::to_string(static_cast<int>(hudState.survivorItemUseProgress01 * 100.0F)) + "%"
            );
        }
        if (hudState.survivorFlashlightAiming)
        {
            m_ui.Label("Flashlight aiming", m_ui.Theme().colorSuccess, 0.95F);
        }
        m_ui.EndPanel();
    }

    if (hudState.roleName == "Killer" && hudState.killerPowerId == "bear_trap")
    {
        const engine::ui::UiRect trapPanel{
            18.0F * scale,
            static_cast<float>(m_ui.ScreenHeight()) - 132.0F * scale,
            300.0F * scale,
            112.0F * scale,
        };
        m_ui.BeginPanel("hud_trap_corner", trapPanel, true);
        m_ui.Label(
            "Traps: carried " + std::to_string(hudState.carriedTrapCount) + " | active " + std::to_string(hudState.activeTrapCount),
            m_ui.Theme().colorAccent
        );
        if (hudState.trapSetProgress01 > 0.0F)
        {
            m_ui.ProgressBar(
                "hud_trap_set_progress",
                hudState.trapSetProgress01,
                "Setting " + std::to_string(static_cast<int>(hudState.trapSetProgress01 * 100.0F)) + "%"
            );
        }
        if (hudState.killerStunRemaining > 0.01F)
        {
            m_ui.Label(
                "STUNNED: " + std::to_string(hudState.killerStunRemaining).substr(0, 4) + "s",
                m_ui.Theme().colorDanger
            );
        }
        m_ui.EndPanel();
    }

    if (hudState.trapIndicatorTtl > 0.0F && !hudState.trapIndicatorText.empty())
    {
        const engine::ui::UiRect trapIndicator{
            (static_cast<float>(m_ui.ScreenWidth()) - 460.0F * scale) * 0.5F,
            90.0F * scale,
            460.0F * scale,
            52.0F * scale,
        };
        m_ui.BeginPanel("hud_trap_indicator", trapIndicator, true);
        m_ui.Label(
            hudState.trapIndicatorText,
            hudState.trapIndicatorDanger ? m_ui.Theme().colorDanger : m_ui.Theme().colorSuccess,
            1.02F
        );
        m_ui.EndPanel();
    }

    if (hudState.roleName == "Survivor" && hudState.survivorFlashlightAiming)
    {
        const float cx = static_cast<float>(m_ui.ScreenWidth()) * 0.5F;
        const float cy = static_cast<float>(m_ui.ScreenHeight()) * 0.5F;
        const glm::vec4 color{1.0F, 0.95F, 0.55F, 0.92F};
        m_ui.FillRect(engine::ui::UiRect{cx - 1.0F * scale, cy - 15.0F * scale, 2.0F * scale, 30.0F * scale}, color);
        m_ui.FillRect(engine::ui::UiRect{cx - 15.0F * scale, cy - 1.0F * scale, 30.0F * scale, 2.0F * scale}, color);
    }

    const bool showBottomPanel = hudState.repairingGenerator || hudState.selfHealing || hudState.skillCheckActive ||
                                 hudState.carryEscapeProgress > 0.0F || hudState.hookStage > 0;
    if (showBottomPanel)
    {
        const engine::ui::UiRect bottom{
            (static_cast<float>(m_ui.ScreenWidth()) - 620.0F * scale) * 0.5F + m_hudLayout.bottomCenterOffset.x * scale,
            static_cast<float>(m_ui.ScreenHeight()) - 240.0F * scale - m_hudLayout.bottomCenterOffset.y * scale,
            620.0F * scale,
            240.0F * scale,
        };
        m_ui.BeginPanel("hud_bottom_custom", bottom, true);

        if (hudState.repairingGenerator)
        {
            m_ui.Label("Generator Repair", m_ui.Theme().colorAccent);
            m_ui.ProgressBar("hud_gen_progress", hudState.activeGeneratorProgress, std::to_string(static_cast<int>(hudState.activeGeneratorProgress * 100.0F)) + "%");
        }
        if (hudState.selfHealing)
        {
            m_ui.Label("Self Heal", m_ui.Theme().colorAccent);
            m_ui.ProgressBar("hud_heal_progress", hudState.selfHealProgress, std::to_string(static_cast<int>(hudState.selfHealProgress * 100.0F)) + "%");
        }
        if (hudState.skillCheckActive)
        {
            m_ui.Label("Skill Check active: SPACE", m_ui.Theme().colorDanger);
            m_ui.SkillCheckBar(
                "hud_skillcheck_progress",
                hudState.skillCheckNeedle,
                hudState.skillCheckSuccessStart,
                hudState.skillCheckSuccessEnd
            );
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

    if (hudState.roleName == "Killer" && hudState.killerBlindRemaining > 0.0F)
    {
        const float blind01 = glm::clamp(hudState.killerBlindRemaining / 2.0F, 0.0F, 1.0F);
        const glm::vec4 overlayColor = hudState.killerBlindWhiteStyle
            ? glm::vec4{1.0F, 1.0F, 1.0F, 0.82F * blind01}
            : glm::vec4{0.0F, 0.0F, 0.0F, 0.78F * blind01};
        m_ui.FillRect(
            engine::ui::UiRect{
                0.0F,
                0.0F,
                static_cast<float>(m_ui.ScreenWidth()),
                static_cast<float>(m_ui.ScreenHeight()),
            },
            overlayColor
        );
    }

    // Draw TR debug overlay if enabled
    if (m_terrorAudioDebug && m_terrorAudioProfile.loaded)
    {
        const float scale = m_ui.Scale();
        const engine::ui::UiRect trDebugPanel{
            (static_cast<float>(m_ui.ScreenWidth()) - 420.0F * scale) * 0.5F,
            200.0F * scale,
            420.0F * scale,
            320.0F * scale,
        };
        m_ui.BeginPanel("tr_debug_overlay", trDebugPanel, true);
        m_ui.Label("Terror Radius Audio Debug", m_ui.Theme().colorAccent, 1.05F);

        // Phase B1: Audio routing info
        const bool localPlayerIsSurvivor = (m_localPlayer.controlledRole == "survivor");
        const bool localPlayerIsKiller = (m_localPlayer.controlledRole == "killer");
        const bool trEnabled = localPlayerIsSurvivor; // TR only for survivor
        const bool chaseEnabledForKiller = localPlayerIsKiller && hudState.chaseActive; // Chase only for killer in chase

        m_ui.Label("Local Role: " + m_localPlayer.controlledRole, localPlayerIsSurvivor ? m_ui.Theme().colorSuccess : m_ui.Theme().colorDanger);
        m_ui.Label("TR Enabled: " + std::string(trEnabled ? "YES" : "NO"), trEnabled ? m_ui.Theme().colorSuccess : m_ui.Theme().colorTextMuted);
        if (localPlayerIsKiller)
        {
            m_ui.Label("Chase Enabled for Killer: " + std::string(chaseEnabledForKiller ? "YES" : "NO"), chaseEnabledForKiller ? m_ui.Theme().colorSuccess : m_ui.Theme().colorTextMuted);
        }

        // Band name
        const char* bandName = "OUTSIDE";
        switch (m_currentBand)
        {
            case TerrorRadiusBand::Outside: bandName = "OUTSIDE"; break;
            case TerrorRadiusBand::Far: bandName = "FAR"; break;
            case TerrorRadiusBand::Mid: bandName = "MID"; break;
            case TerrorRadiusBand::Close: bandName = "CLOSE"; break;
        }
        m_ui.Label("Band: " + std::string(bandName));

        // Distance and radius
        const glm::vec3 survivorPos = m_gameplay.RolePosition("survivor");
        const glm::vec3 killerPos = m_gameplay.RolePosition("killer");
        const glm::vec2 delta{survivorPos.x - killerPos.x, survivorPos.z - killerPos.z};
        const float distance = glm::length(delta);
        m_ui.Label("Distance: " + std::to_string(distance) + " m (Radius: " + std::to_string(m_terrorAudioProfile.baseRadius) + " m)");

        // Chase state
        m_ui.Label("Chase Active: " + std::string(hudState.chaseActive ? "YES" : "NO"), hudState.chaseActive ? m_ui.Theme().colorDanger : m_ui.Theme().colorTextMuted);

        // Bus volumes
        const float musicBusVol = m_audio.GetBusVolume(audio::AudioSystem::Bus::Music);
        m_ui.Label("Music Bus: " + std::to_string(musicBusVol), m_ui.Theme().colorTextMuted);

        // Per-layer volumes
        m_ui.Label("Layer Volumes:", m_ui.Theme().colorAccent);
        for (const TerrorRadiusLayerAudio& layer : m_terrorAudioProfile.layers)
        {
            const float busVol = musicBusVol;
            const float finalApplied = layer.currentVolume * layer.gain * busVol;

            std::string layerInfo = layer.clip;
            if (layer.chaseOnly)
            {
                layerInfo += " [chase]";
            }
            layerInfo += ": " + std::to_string(finalApplied);

            // Color code: red if suppressed (near 0), green if audible
            if (finalApplied < 0.01F)
            {
                m_ui.Label(layerInfo, m_ui.Theme().colorTextMuted);
            }
            else
            {
                m_ui.Label(layerInfo, m_ui.Theme().colorSuccess);
            }

            // Detailed breakdown (smaller)
            m_ui.Label(
                "  gain=" + std::to_string(layer.gain) +
                " cur=" + std::to_string(layer.currentVolume) +
                " bus=" + std::to_string(busVol) +
                " final=" + std::to_string(finalApplied),
                m_ui.Theme().colorTextMuted, 0.85F
            );
        }

        m_ui.EndPanel();
    }

    // Phase B2/B3: Scratch marks and blood pools debug overlays
    if (m_gameplay.ScratchDebugEnabled() || m_gameplay.BloodDebugEnabled())
    {
        const float scale = m_ui.Scale();
        const engine::ui::UiRect debugPanel{
            (static_cast<float>(m_ui.ScreenWidth()) - 300.0F * scale) * 0.5F,
            540.0F * scale,  // Below TR debug panel
            300.0F * scale,
            180.0F * scale,
        };
        m_ui.BeginPanel("scratch_blood_debug", debugPanel, true);
        m_ui.Label("VFX Debug", m_ui.Theme().colorAccent, 1.05F);

        if (m_gameplay.ScratchDebugEnabled())
        {
            m_ui.Label("=== Scratch Marks ===", m_ui.Theme().colorAccent);
            m_ui.Label("Active Count: " + std::to_string(hudState.scratchActiveCount));
            m_ui.Label("Spawn Interval: " + std::to_string(hudState.scratchSpawnInterval) + " s");
            m_ui.Label("(Visible only to Killer)", m_ui.Theme().colorTextMuted, 0.9F);
        }

        if (m_gameplay.BloodDebugEnabled())
        {
            m_ui.Label("=== Blood Pools ===", m_ui.Theme().colorAccent);
            m_ui.Label("Active Count: " + std::to_string(hudState.bloodActiveCount));
            m_ui.Label("(Visible only to Killer)", m_ui.Theme().colorTextMuted, 0.9F);
        }

        // Phase B4: Killer Look Light debug
        if (m_gameplay.KillerLookLightDebug())
        {
            m_ui.Label("=== Killer Light ===", m_ui.Theme().colorAccent);
            m_ui.Label("Enabled: " + std::string(hudState.killerLightEnabled ? "YES" : "NO"), hudState.killerLightEnabled ? m_ui.Theme().colorSuccess : m_ui.Theme().colorTextMuted);
            m_ui.Label("Range: " + std::to_string(hudState.killerLightRange) + " m");
            m_ui.Label("Intensity: " + std::to_string(hudState.killerLightIntensity));
            m_ui.Label("Inner Angle: " + std::to_string(hudState.killerLightInnerAngle) + "°");
            m_ui.Label("Outer Angle: " + std::to_string(hudState.killerLightOuterAngle) + "°");
        }

        m_ui.EndPanel();
    }
}

void App::DrawUiTestPanel()
{
    const float scale = m_ui.Scale();
    const engine::ui::UiRect panel{
        18.0F * scale,
        18.0F * scale,
        std::min(440.0F * scale, static_cast<float>(m_ui.ScreenWidth()) - 36.0F * scale),
        std::min(760.0F * scale, static_cast<float>(m_ui.ScreenHeight()) - 36.0F * scale),
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

void App::DrawMainMenuUi(bool* shouldQuit)
{
#if BUILD_WITH_IMGUI
    constexpr const char* kRoleItems[] = {"Survivor", "Killer"};
    constexpr const char* kMapItems[] = {"main_map", "collision_test", "test"};
    const std::vector<std::string> savedMaps = game::editor::LevelAssetIO::ListMapNames();
    if (m_menuSavedMapIndex >= static_cast<int>(savedMaps.size()))
    {
        m_menuSavedMapIndex = savedMaps.empty() ? -1 : 0;
    }
    if (m_menuSavedMapIndex < 0 && !savedMaps.empty())
    {
        m_menuSavedMapIndex = 0;
    }

    ImGui::SetNextWindowSize(ImVec2(620.0F, 720.0F), ImGuiCond_Always);
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Always, ImVec2(0.5F, 0.5F));

    if (ImGui::Begin("Main Menu", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse))
    {
        ImGui::TextUnformatted("Asymmetric Horror Prototype");
        if (ImGui::Button(m_useLegacyImGuiMenus ? "Use Custom UI Menus (F10)" : "Use Legacy ImGui Menus (F10)", ImVec2(-1.0F, 0.0F)))
        {
            m_useLegacyImGuiMenus = !m_useLegacyImGuiMenus;
        }
        if (ImGui::Button(m_showUiTestPanel ? "Hide UI Test Panel (F6)" : "Show UI Test Panel (F6)", ImVec2(-1.0F, 0.0F)))
        {
            m_showUiTestPanel = !m_showUiTestPanel;
        }
        if (ImGui::Button("Settings", ImVec2(-1.0F, 0.0F)))
        {
            m_settingsMenuOpen = true;
            m_settingsOpenedFromPause = false;
        }
        ImGui::Separator();

        ImGui::Combo("Role", &m_menuRoleIndex, kRoleItems, IM_ARRAYSIZE(kRoleItems));
        ImGui::Combo("Map", &m_menuMapIndex, kMapItems, IM_ARRAYSIZE(kMapItems));

        m_menuPort = std::clamp(m_menuPort, 1, 65535);
        ImGui::InputInt("Port", &m_menuPort);

        char ipBuffer[64]{};
        std::snprintf(ipBuffer, sizeof(ipBuffer), "%s", m_menuJoinIp.c_str());
        if (ImGui::InputText("Join IP", ipBuffer, sizeof(ipBuffer)))
        {
            m_menuJoinIp = ipBuffer;
        }

        const std::string roleName = RoleNameFromIndex(m_menuRoleIndex);
        const std::string mapName = MapNameFromIndex(m_menuMapIndex);

        if (ImGui::Button("Play Solo", ImVec2(-1.0F, 0.0F)))
        {
            StartSoloSession(mapName, roleName);
        }

        if (!savedMaps.empty())
        {
            ImGui::Separator();
            ImGui::TextUnformatted("Play Saved Map");
            if (ImGui::BeginListBox("##saved_maps", ImVec2(-1.0F, 110.0F)))
            {
                for (int i = 0; i < static_cast<int>(savedMaps.size()); ++i)
                {
                    const bool selected = m_menuSavedMapIndex == i;
                    if (ImGui::Selectable(savedMaps[static_cast<std::size_t>(i)].c_str(), selected))
                    {
                        m_menuSavedMapIndex = i;
                    }
                }
                ImGui::EndListBox();
            }
            if (ImGui::Button("Play Map", ImVec2(-1.0F, 0.0F)))
            {
                if (m_menuSavedMapIndex >= 0 && m_menuSavedMapIndex < static_cast<int>(savedMaps.size()))
                {
                    StartSoloSession(savedMaps[static_cast<std::size_t>(m_menuSavedMapIndex)], roleName);
                }
            }
        }

        ImGui::Separator();
        ImGui::TextUnformatted("Editor");
        if (ImGui::Button("Level Editor", ImVec2(-1.0F, 0.0F)))
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
        if (ImGui::Button("Loop Editor", ImVec2(-1.0F, 0.0F)))
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

        ImGui::Separator();
        ImGui::TextUnformatted("Multiplayer");
        if (ImGui::Button("Host Multiplayer", ImVec2(-1.0F, 0.0F)))
        {
            StartHostSession(mapName, roleName, static_cast<std::uint16_t>(std::clamp(m_menuPort, 1, 65535)));
        }

        if (ImGui::Button("Join Multiplayer", ImVec2(-1.0F, 0.0F)))
        {
            StartJoinSession(m_menuJoinIp, static_cast<std::uint16_t>(std::clamp(m_menuPort, 1, 65535)), roleName);
        }

        ImGui::Separator();
        ImGui::TextUnformatted("LAN Games (Local Network Only)");
        if (ImGui::Button("Refresh LAN", ImVec2(-1.0F, 0.0F)))
        {
            m_lanDiscovery.ForceScan();
        }

        const auto& servers = m_lanDiscovery.Servers();
        if (servers.empty())
        {
            ImGui::TextWrapped("No LAN games found. Make sure host is running and on same network.");
        }
        else
        {
            for (std::size_t i = 0; i < servers.size(); ++i)
            {
                const auto& entry = servers[i];
                const bool full = entry.players >= entry.maxPlayers;
                const bool canJoin = entry.compatible && !full;

                ImGui::PushID(static_cast<int>(i));
                ImGui::Text("[%s] %s:%u | Map: %s | Players: %d/%d",
                            entry.hostName.c_str(),
                            entry.ip.c_str(),
                            entry.port,
                            entry.mapName.c_str(),
                            entry.players,
                            entry.maxPlayers);
                if (!entry.compatible)
                {
                    ImGui::SameLine();
                    ImGui::TextUnformatted("(Incompatible Version)");
                }
                else if (full)
                {
                    ImGui::SameLine();
                    ImGui::TextUnformatted("(Full)");
                }

                if (!canJoin)
                {
                    ImGui::BeginDisabled();
                }
                if (ImGui::Button("Join", ImVec2(100.0F, 0.0F)))
                {
                    StartJoinSession(entry.ip, entry.port, roleName);
                }
                if (!canJoin)
                {
                    ImGui::EndDisabled();
                }
                ImGui::PopID();
            }
        }

        if (ImGui::Button("Quit", ImVec2(-1.0F, 0.0F)))
        {
            *shouldQuit = true;
        }

        if (!m_menuNetStatus.empty())
        {
            ImGui::Separator();
            ImGui::TextWrapped("%s", m_menuNetStatus.c_str());
        }

        ImGui::Separator();
        ImGui::Text("Network State: %s", NetworkStateToText(m_networkState).c_str());
        if (m_networkState == NetworkState::ClientConnecting || m_networkState == NetworkState::ClientHandshaking)
        {
            const double elapsed = std::max(0.0, glfwGetTime() - m_joinStartSeconds);
            ImGui::Text("Connecting to %s:%u (%.1fs)", m_joinTargetIp.c_str(), m_joinTargetPort, elapsed);
        }
        if (!m_lastNetworkError.empty())
        {
            ImGui::TextWrapped("Last Error: %s", m_lastNetworkError.c_str());
        }
        ImGui::TextUnformatted("Press F4 for network diagnostics");
    }
    ImGui::End();
#else
    (void)shouldQuit;
#endif
}

void App::DrawPauseMenuUi(bool* closePauseMenu, bool* backToMenu, bool* shouldQuit)
{
#if BUILD_WITH_IMGUI
    ImGui::SetNextWindowSize(ImVec2(360.0F, 240.0F), ImGuiCond_Always);
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Always, ImVec2(0.5F, 0.5F));

    if (ImGui::Begin("Pause Menu", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse))
    {
        if (ImGui::Button("Resume", ImVec2(-1.0F, 0.0F)))
        {
            *closePauseMenu = true;
        }

        if (ImGui::Button("Return to Main Menu", ImVec2(-1.0F, 0.0F)))
        {
            *backToMenu = true;
        }

        if (ImGui::Button("Settings", ImVec2(-1.0F, 0.0F)))
        {
            m_settingsMenuOpen = true;
            m_settingsOpenedFromPause = true;
        }

        if (ImGui::Button("Quit", ImVec2(-1.0F, 0.0F)))
        {
            *shouldQuit = true;
        }
    }
    ImGui::End();
#else
    (void)closePauseMenu;
    (void)backToMenu;
    (void)shouldQuit;
#endif
}

void App::DrawSettingsUi(bool* closeSettings)
{
#if BUILD_WITH_IMGUI
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
                m_controlsStatus =
                    std::string{"Bound "} + platform::ActionBindings::ActionLabel(m_rebindAction) + " to " +
                    platform::ActionBindings::CodeToLabel(*captured);
                (void)SaveControlsConfig();
            }
        }
    }

    ImGui::SetNextWindowSize(ImVec2(920.0F, 680.0F), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5F, 0.5F));
    if (ImGui::Begin("Settings", closeSettings, ImGuiWindowFlags_NoCollapse))
    {
        ImGui::TextUnformatted("Persistent settings (config/*.json)");
        if (m_multiplayerMode == MultiplayerMode::Client)
        {
            ImGui::SameLine();
            ImGui::TextUnformatted("| Server Values active");
        }
        ImGui::Separator();

        if (ImGui::BeginTabBar("settings_tabs"))
        {
            if (ImGui::BeginTabItem("Controls"))
            {
                ImGui::TextWrapped("Action bindings with primary/secondary slots. Click Rebind and press a key or mouse button.");
                if (m_rebindWaiting)
                {
                    ImGui::Text("Waiting input for: %s (%s)",
                                platform::ActionBindings::ActionLabel(m_rebindAction),
                                m_rebindSlot == 0 ? "Primary" : "Secondary");
                    ImGui::TextUnformatted("Press ESC to cancel.");
                }

                ImGui::Separator();
                ImGui::BeginChild("controls_bindings", ImVec2(0.0F, 350.0F), true);
                for (platform::InputAction action : platform::ActionBindings::AllActions())
                {
                    const platform::ActionBinding binding = m_actionBindings.Get(action);
                    ImGui::PushID(static_cast<int>(action));
                    ImGui::TextUnformatted(platform::ActionBindings::ActionLabel(action));
                    ImGui::SameLine(240.0F);

                    const bool rebindable = platform::ActionBindings::IsRebindable(action);
                    if (!rebindable)
                    {
                        ImGui::Text("%s", platform::ActionBindings::CodeToLabel(binding.primary).c_str());
                    }
                    else
                    {
                        if (ImGui::Button((platform::ActionBindings::CodeToLabel(binding.primary) + "##p").c_str(), ImVec2(120.0F, 0.0F)))
                        {
                            m_rebindWaiting = true;
                            m_rebindAction = action;
                            m_rebindSlot = 0;
                        }
                        ImGui::SameLine();
                        if (ImGui::Button((platform::ActionBindings::CodeToLabel(binding.secondary) + "##s").c_str(), ImVec2(120.0F, 0.0F)))
                        {
                            m_rebindWaiting = true;
                            m_rebindAction = action;
                            m_rebindSlot = 1;
                        }
                        ImGui::SameLine();
                        if (ImGui::SmallButton("Unbind"))
                        {
                            m_actionBindings.SetCode(action, 0, platform::ActionBindings::kUnbound);
                            m_actionBindings.SetCode(action, 1, platform::ActionBindings::kUnbound);
                            (void)SaveControlsConfig();
                        }
                    }
                    ImGui::PopID();
                }
                ImGui::EndChild();

                if (m_rebindConflictPopup)
                {
                    ImGui::OpenPopup("Rebind Conflict");
                    m_rebindConflictPopup = false;
                }
                if (ImGui::BeginPopupModal("Rebind Conflict", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
                {
                    ImGui::Text("Key already used by %s (%s).",
                                platform::ActionBindings::ActionLabel(m_rebindConflictAction),
                                m_rebindConflictSlot == 0 ? "Primary" : "Secondary");
                    ImGui::Text("Override with %s?",
                                platform::ActionBindings::CodeToLabel(m_rebindCapturedCode).c_str());
                    if (ImGui::Button("Override"))
                    {
                        m_actionBindings.SetCode(m_rebindConflictAction, m_rebindConflictSlot, platform::ActionBindings::kUnbound);
                        m_actionBindings.SetCode(m_rebindAction, m_rebindSlot, m_rebindCapturedCode);
                        m_rebindWaiting = false;
                        (void)SaveControlsConfig();
                        ImGui::CloseCurrentPopup();
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Cancel"))
                    {
                        m_rebindWaiting = false;
                        ImGui::CloseCurrentPopup();
                    }
                    ImGui::EndPopup();
                }

                bool sensitivityChanged = false;
                sensitivityChanged |= ImGui::SliderFloat("Survivor Look Sensitivity", &m_controlsSettings.survivorSensitivity, 0.0002F, 0.01F, "%.4f");
                sensitivityChanged |= ImGui::SliderFloat("Killer Look Sensitivity", &m_controlsSettings.killerSensitivity, 0.0002F, 0.01F, "%.4f");
                sensitivityChanged |= ImGui::Checkbox("Invert Y", &m_controlsSettings.invertY);
                if (sensitivityChanged)
                {
                    ApplyControlsSettings();
                }

                if (ImGui::Button("Save Controls"))
                {
                    ApplyControlsSettings();
                    if (SaveControlsConfig())
                    {
                        m_controlsStatus = "Controls saved to config/controls.json";
                    }
                    else
                    {
                        m_controlsStatus = "Failed to save controls config.";
                    }
                }
                ImGui::SameLine();
                if (ImGui::Button("Reset Defaults"))
                {
                    m_actionBindings.ResetDefaults();
                    m_controlsSettings = ControlsSettings{};
                    ApplyControlsSettings();
                    (void)SaveControlsConfig();
                    m_controlsStatus = "Controls reset to defaults.";
                }
                if (!m_controlsStatus.empty())
                {
                    ImGui::TextWrapped("%s", m_controlsStatus.c_str());
                }
                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("Graphics"))
            {
                const char* displayModes[] = {"Windowed", "Fullscreen", "Borderless"};
                int displayModeIndex = static_cast<int>(m_graphicsEditing.displayMode);
                if (ImGui::Combo("Display Mode", &displayModeIndex, displayModes, IM_ARRAYSIZE(displayModes)))
                {
                    m_graphicsEditing.displayMode = static_cast<DisplayModeSetting>(glm::clamp(displayModeIndex, 0, 2));
                }

                const std::vector<std::pair<int, int>> resolutions = AvailableResolutions();
                int resolutionIndex = -1;
                std::vector<std::string> labels;
                labels.reserve(resolutions.size());
                for (std::size_t i = 0; i < resolutions.size(); ++i)
                {
                    const auto [w, h] = resolutions[i];
                    labels.push_back(std::to_string(w) + "x" + std::to_string(h));
                    if (w == m_graphicsEditing.width && h == m_graphicsEditing.height)
                    {
                        resolutionIndex = static_cast<int>(i);
                    }
                }
                if (resolutionIndex < 0 && !resolutions.empty())
                {
                    resolutionIndex = 0;
                }

                if (!labels.empty())
                {
                    const char* preview = labels[static_cast<std::size_t>(resolutionIndex)].c_str();
                    if (ImGui::BeginCombo("Resolution", preview))
                    {
                        for (int i = 0; i < static_cast<int>(labels.size()); ++i)
                        {
                            const bool selected = i == resolutionIndex;
                            if (ImGui::Selectable(labels[static_cast<std::size_t>(i)].c_str(), selected))
                            {
                                resolutionIndex = i;
                                m_graphicsEditing.width = resolutions[static_cast<std::size_t>(i)].first;
                                m_graphicsEditing.height = resolutions[static_cast<std::size_t>(i)].second;
                            }
                            if (selected)
                            {
                                ImGui::SetItemDefaultFocus();
                            }
                        }
                        ImGui::EndCombo();
                    }
                }

                ImGui::Checkbox("VSync", &m_graphicsEditing.vsync);
                ImGui::SliderInt("FPS Limit (0 = Unlimited)", &m_graphicsEditing.fpsLimit, 0, 240);

                int renderMode = m_graphicsEditing.renderMode == render::RenderMode::Wireframe ? 0 : 1;
                const char* renderItems[] = {"Wireframe", "Filled"};
                if (ImGui::Combo("Render Mode", &renderMode, renderItems, IM_ARRAYSIZE(renderItems)))
                {
                    m_graphicsEditing.renderMode = renderMode == 0 ? render::RenderMode::Wireframe : render::RenderMode::Filled;
                }

                const char* shadowItems[] = {"Off", "Low", "Med", "High"};
                ImGui::Combo("Shadow Quality", &m_graphicsEditing.shadowQuality, shadowItems, IM_ARRAYSIZE(shadowItems));
                ImGui::SliderFloat("Shadow Distance", &m_graphicsEditing.shadowDistance, 8.0F, 200.0F, "%.0f");
                const char* aaItems[] = {"None", "FXAA"};
                ImGui::Combo("Anti-Aliasing", &m_graphicsEditing.antiAliasing, aaItems, IM_ARRAYSIZE(aaItems));
                const char* texItems[] = {"Low", "Medium", "High"};
                ImGui::Combo("Texture Quality", &m_graphicsEditing.textureQuality, texItems, IM_ARRAYSIZE(texItems));
                ImGui::Checkbox("Fog", &m_graphicsEditing.fogEnabled);

                if (ImGui::Button("Apply Graphics"))
                {
                    m_graphicsRollback = m_graphicsApplied;
                    ApplyGraphicsSettings(m_graphicsEditing, true);
                    if (SaveGraphicsConfig())
                    {
                        m_graphicsStatus = "Graphics applied and saved.";
                    }
                    else
                    {
                        m_graphicsStatus = "Graphics applied, but failed to save config.";
                    }
                }
                ImGui::SameLine();
                if (ImGui::Button("Cancel"))
                {
                    m_graphicsEditing = m_graphicsApplied;
                    m_graphicsStatus = "Pending graphics changes reverted.";
                }

                if (m_graphicsAutoConfirmPending)
                {
                    const double secondsLeft = std::max(0.0, m_graphicsAutoConfirmDeadline - glfwGetTime());
                    ImGui::Separator();
                    ImGui::Text("Confirm display settings: %.1fs left", secondsLeft);
                    if (ImGui::Button("Keep"))
                    {
                        m_graphicsAutoConfirmPending = false;
                        m_graphicsRollback = m_graphicsApplied;
                        (void)SaveGraphicsConfig();
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Revert"))
                    {
                        ApplyGraphicsSettings(m_graphicsRollback, false);
                        m_graphicsEditing = m_graphicsRollback;
                        m_graphicsApplied = m_graphicsRollback;
                        m_graphicsAutoConfirmPending = false;
                        (void)SaveGraphicsConfig();
                    }
                }

                if (!m_graphicsStatus.empty())
                {
                    ImGui::TextWrapped("%s", m_graphicsStatus.c_str());
                }

                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("Gameplay"))
            {
                const bool allowEdit = m_multiplayerMode != MultiplayerMode::Client;
                if (!allowEdit)
                {
                    ImGui::TextColored(ImVec4(1.0F, 0.8F, 0.2F, 1.0F), "Server Values are authoritative in multiplayer client mode.");
                    ImGui::BeginDisabled();
                }

                auto& t = m_gameplayEditing;
                ImGui::TextUnformatted("Movement");
                ImGui::SliderFloat("Survivor Walk", &t.survivorWalkSpeed, 0.5F, 8.0F, "%.2f");
                ImGui::SliderFloat("Survivor Sprint", &t.survivorSprintSpeed, 0.5F, 10.0F, "%.2f");
                ImGui::SliderFloat("Survivor Crouch", &t.survivorCrouchSpeed, 0.1F, 5.0F, "%.2f");
                ImGui::SliderFloat("Survivor Crawl", &t.survivorCrawlSpeed, 0.1F, 3.0F, "%.2f");
                ImGui::SliderFloat("Killer Speed", &t.killerMoveSpeed, 0.5F, 12.0F, "%.2f");
                ImGui::Separator();
                ImGui::TextUnformatted("Capsules");
                ImGui::SliderFloat("Survivor Radius", &t.survivorCapsuleRadius, 0.2F, 1.2F, "%.2f");
                ImGui::SliderFloat("Survivor Height", &t.survivorCapsuleHeight, 0.9F, 3.0F, "%.2f");
                ImGui::SliderFloat("Killer Radius", &t.killerCapsuleRadius, 0.2F, 1.2F, "%.2f");
                ImGui::SliderFloat("Killer Height", &t.killerCapsuleHeight, 0.9F, 3.0F, "%.2f");
                ImGui::Separator();
                ImGui::TextUnformatted("Vault / Combat / Heal");
                ImGui::SliderFloat("Terror Radius", &t.terrorRadiusMeters, 4.0F, 80.0F, "%.1f");
                ImGui::SliderFloat("Slow Vault Time", &t.vaultSlowTime, 0.2F, 1.6F, "%.2f");
                ImGui::SliderFloat("Medium Vault Time", &t.vaultMediumTime, 0.2F, 1.2F, "%.2f");
                ImGui::SliderFloat("Fast Vault Time", &t.vaultFastTime, 0.15F, 1.0F, "%.2f");
                ImGui::SliderFloat("Fast Vault Dot", &t.fastVaultDotThreshold, 0.3F, 0.99F, "%.2f");
                ImGui::SliderFloat("Fast Vault Speed Mult", &t.fastVaultSpeedMultiplier, 0.3F, 1.2F, "%.2f");
                ImGui::SliderFloat("Short Attack Range", &t.shortAttackRange, 0.5F, 6.0F, "%.2f");
                ImGui::SliderFloat("Short Attack Angle", &t.shortAttackAngleDegrees, 15.0F, 170.0F, "%.0f");
                ImGui::SliderFloat("Lunge Hold Min", &t.lungeHoldMinSeconds, 0.02F, 1.2F, "%.2f");
                ImGui::SliderFloat("Lunge Duration", &t.lungeDurationSeconds, 0.08F, 2.0F, "%.2f");
                ImGui::SliderFloat("Lunge Boost End Speed", &t.lungeSpeedEnd, 1.0F, 20.0F, "%.2f");
                ImGui::SliderFloat("Heal Duration", &t.healDurationSeconds, 2.0F, 60.0F, "%.1f");
                ImGui::SliderFloat("Skillcheck Min", &t.skillCheckMinInterval, 0.5F, 20.0F, "%.1f");
                ImGui::SliderFloat("Skillcheck Max", &t.skillCheckMaxInterval, 0.5F, 30.0F, "%.1f");
                ImGui::SliderFloat("Generator Base Seconds", &t.generatorRepairSecondsBase, 20.0F, 180.0F, "%.1f");
                ImGui::Separator();
                ImGui::TextUnformatted("Items");
                ImGui::SliderFloat("Medkit Full Heal Charges", &t.medkitFullHealCharges, 4.0F, 64.0F, "%.1f");
                ImGui::SliderFloat("Medkit Heal Speed Mult", &t.medkitHealSpeedMultiplier, 0.5F, 4.0F, "%.2f");
                ImGui::SliderFloat("Toolbox Charges", &t.toolboxCharges, 1.0F, 120.0F, "%.1f");
                ImGui::SliderFloat("Toolbox Drain / sec", &t.toolboxChargeDrainPerSecond, 0.05F, 8.0F, "%.2f");
                ImGui::SliderFloat("Toolbox Repair Bonus", &t.toolboxRepairSpeedBonus, 0.0F, 3.0F, "%.2f");
                ImGui::SliderFloat("Flashlight Max Use (s)", &t.flashlightMaxUseSeconds, 1.0F, 30.0F, "%.2f");
                ImGui::SliderFloat("Flashlight Blind Build (s)", &t.flashlightBlindBuildSeconds, 0.1F, 6.0F, "%.2f");
                ImGui::SliderFloat("Flashlight Blind Duration (s)", &t.flashlightBlindDurationSeconds, 0.1F, 8.0F, "%.2f");
                ImGui::SliderFloat("Flashlight Beam Range", &t.flashlightBeamRange, 2.0F, 25.0F, "%.1f");
                ImGui::SliderFloat("Flashlight Beam Angle", &t.flashlightBeamAngleDegrees, 5.0F, 80.0F, "%.1f");
                const char* blindStyleItems[] = {"White", "Dark"};
                ImGui::Combo("Flashlight Blind Style", &t.flashlightBlindStyle, blindStyleItems, 2);
                ImGui::SliderFloat("Map Channel (s)", &t.mapChannelSeconds, 0.05F, 4.0F, "%.2f");
                ImGui::SliderInt("Map Uses", &t.mapUses, 0, 20);
                ImGui::SliderFloat("Map Reveal Range (m)", &t.mapRevealRangeMeters, 4.0F, 120.0F, "%.1f");
                ImGui::SliderFloat("Map Reveal Duration (s)", &t.mapRevealDurationSeconds, 0.2F, 12.0F, "%.2f");
                ImGui::Separator();
                ImGui::TextUnformatted("Powers");
                ImGui::SliderInt("Trapper Start Carry", &t.trapperStartCarryTraps, 0, 16);
                ImGui::SliderInt("Trapper Max Carry", &t.trapperMaxCarryTraps, 1, 16);
                ImGui::SliderInt("Trapper Ground Spawn", &t.trapperGroundSpawnTraps, 0, 48);
                ImGui::SliderFloat("Trapper Set Time (s)", &t.trapperSetTrapSeconds, 0.1F, 6.0F, "%.2f");
                ImGui::SliderFloat("Trapper Disarm Time (s)", &t.trapperDisarmSeconds, 0.1F, 8.0F, "%.2f");
                ImGui::SliderFloat("Trap Escape Base", &t.trapEscapeBaseChance, 0.01F, 0.9F, "%.2f");
                ImGui::SliderFloat("Trap Escape Step", &t.trapEscapeChanceStep, 0.01F, 0.8F, "%.2f");
                ImGui::SliderFloat("Trap Escape Max", &t.trapEscapeChanceMax, 0.05F, 0.98F, "%.2f");
                ImGui::SliderFloat("Trap Killer Stun (s)", &t.trapKillerStunSeconds, 0.1F, 8.0F, "%.2f");
                ImGui::SliderFloat("Wraith Cloak Speed Mult", &t.wraithCloakMoveSpeedMultiplier, 1.0F, 3.0F, "%.2f");
                ImGui::SliderFloat("Wraith Cloak Transition (s)", &t.wraithCloakTransitionSeconds, 0.1F, 4.0F, "%.2f");
                ImGui::SliderFloat("Wraith Uncloak Transition (s)", &t.wraithUncloakTransitionSeconds, 0.1F, 4.0F, "%.2f");
                ImGui::SliderFloat("Wraith Post-Uncloak Haste (s)", &t.wraithPostUncloakHasteSeconds, 0.0F, 8.0F, "%.2f");
                ImGui::Separator();
                ImGui::TextUnformatted("Map Generation Weights");
                ImGui::TextUnformatted("  Classic Loops:");
                ImGui::SliderFloat("Weight TL", &t.weightTLWalls, 0.0F, 5.0F, "%.2f");
                ImGui::SliderFloat("Weight Jungle Long", &t.weightJungleGymLong, 0.0F, 5.0F, "%.2f");
                ImGui::SliderFloat("Weight Jungle Short", &t.weightJungleGymShort, 0.0F, 5.0F, "%.2f");
                ImGui::SliderFloat("Weight Shack", &t.weightShack, 0.0F, 5.0F, "%.2f");
                ImGui::SliderFloat("Weight Four Lane", &t.weightFourLane, 0.0F, 5.0F, "%.2f");
                ImGui::SliderFloat("Weight Filler A", &t.weightFillerA, 0.0F, 5.0F, "%.2f");
                ImGui::SliderFloat("Weight Filler B", &t.weightFillerB, 0.0F, 5.0F, "%.2f");
                ImGui::Separator();
                ImGui::TextUnformatted("  v2 Loop Types:");
                ImGui::SliderFloat("Weight Long Wall", &t.weightLongWall, 0.0F, 5.0F, "%.2f");
                ImGui::SliderFloat("Weight Short Wall", &t.weightShortWall, 0.0F, 5.0F, "%.2f");
                ImGui::SliderFloat("Weight L-Wall Window", &t.weightLWallWindow, 0.0F, 5.0F, "%.2f");
                ImGui::SliderFloat("Weight L-Wall Pallet", &t.weightLWallPallet, 0.0F, 5.0F, "%.2f");
                ImGui::SliderFloat("Weight T-Walls", &t.weightTWalls, 0.0F, 5.0F, "%.2f");
                ImGui::SliderFloat("Weight Gym Box", &t.weightGymBox, 0.0F, 5.0F, "%.2f");
                ImGui::SliderFloat("Weight Debris Pile", &t.weightDebrisPile, 0.0F, 5.0F, "%.2f");
                ImGui::Separator();
                ImGui::TextUnformatted("  Constraints:");
                ImGui::SliderInt("Max Loops", &t.maxLoopsPerMap, 0, 64);
                ImGui::SliderFloat("Min Loop Distance Tiles", &t.minLoopDistanceTiles, 0.0F, 6.0F, "%.1f");
                ImGui::SliderInt("Max Safe Pallets", &t.maxSafePallets, 0, 64);
                ImGui::SliderInt("Max Deadzone Tiles", &t.maxDeadzoneTiles, 1, 8);
                ImGui::Checkbox("Edge Bias Loops", &t.edgeBiasLoops);
                ImGui::Separator();
                ImGui::TextUnformatted("Networking");
                ImGui::SliderInt("Server Tick Rate", &t.serverTickRate, 30, 60);
                ImGui::SliderInt("Interpolation Buffer (ms)", &t.interpolationBufferMs, 50, 1000);

                if (!allowEdit)
                {
                    ImGui::EndDisabled();
                }

                if (allowEdit && ImGui::Button("Apply Gameplay Tuning"))
                {
                    ApplyGameplaySettings(m_gameplayEditing, false);
                    if (m_multiplayerMode == MultiplayerMode::Host)
                    {
                        SendGameplayTuningToClient();
                    }
                    if (SaveGameplayConfig())
                    {
                        m_gameplayStatus = "Gameplay tuning applied and saved.";
                    }
                    else
                    {
                        m_gameplayStatus = "Gameplay tuning applied, but config save failed.";
                    }
                }
                if (allowEdit)
                {
                    ImGui::SameLine();
                }
                if (ImGui::Button("Reset Gameplay Defaults"))
                {
                    m_gameplayEditing = game::gameplay::GameplaySystems::GameplayTuning{};
                    if (allowEdit)
                    {
                        ApplyGameplaySettings(m_gameplayEditing, false);
                        (void)SaveGameplayConfig();
                    }
                }
                ImGui::SameLine();
                if (ImGui::Button("Cancel Gameplay Changes"))
                {
                    m_gameplayEditing = m_gameplayApplied;
                }

                ImGui::TextWrapped("Map generation weights and some collider values are safest to verify after reloading map.");
                if (!m_gameplayStatus.empty())
                {
                    ImGui::TextWrapped("%s", m_gameplayStatus.c_str());
                }
                ImGui::EndTabItem();
            }

            ImGui::EndTabBar();
        }

        ImGui::Separator();
        if (ImGui::Button("Back"))
        {
            *closeSettings = false;
        }
    }
    ImGui::End();
#else
    (void)closeSettings;
#endif
}

std::string App::RoleNameFromIndex(int index)
{
    return index == 1 ? "killer" : "survivor";
}

std::string App::MapNameFromIndex(int index)
{
    switch (index)
    {
        case 1: return "collision_test";
        case 2: return "test";
        case 0:
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
