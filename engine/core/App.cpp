#define NOMINMAX
#include "engine/core/App.hpp"
#include "engine/core/Profiler.hpp"
#include "engine/core/JobSystem.hpp"
#include "engine/assets/AsyncAssetLoader.hpp"
#include "engine/render/RenderThread.hpp"

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
#include <iomanip>
#include <thread>

#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/mat4x4.hpp>
#include <nlohmann/json.hpp>

#include "game/editor/LevelAssets.hpp"
#include "engine/ui/UiSerialization.hpp"

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
constexpr std::uint8_t kPacketLobbyState = 9;
constexpr std::uint8_t kPacketLobbyPlayerJoin = 10;
constexpr std::uint8_t kPacketLobbyPlayerLeave = 11;
constexpr std::uint8_t kPacketLobbyPlayerUpdate = 12;

// Maximum players in lobby (DBD-like: 4 survivors + 1 killer)
constexpr std::size_t kMaxLobbySurvivors = 4;
constexpr std::size_t kMaxLobbyKillers = 1;
constexpr std::size_t kMaxLobbyPlayers = kMaxLobbySurvivors + kMaxLobbyKillers;

constexpr int kProtocolVersion = 1;
#ifndef BUILD_ID
#define BUILD_ID "unknown"
#endif
constexpr const char* kBuildId = BUILD_ID;

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

bool App::InitializeRuntimeUiSystem()
{
    m_runtimeUiTree.SetVirtualResolution(1920, 1080, engine::ui::VirtualResolution::ScaleMode::FitHeight);
    m_runtimeUiTree.SetScreenSize(m_window.FramebufferWidth(), m_window.FramebufferHeight());

    const std::string stylePath = "assets/ui/styles/base.ui.css.json";
    const std::string tokensPath = "assets/ui/styles/theme_default.tokens.json";

#if BUILD_WITH_IMGUI
    m_runtimeUiEditor.Initialize(&m_runtimeUiTree);
    m_runtimeUiEditor.SetMode(engine::ui::EditorMode::None);

    const bool styleLoaded = m_runtimeUiEditor.LoadStyleSheet(stylePath);
    const bool tokensLoaded = m_runtimeUiEditor.LoadTokens(tokensPath);
    const bool screenLoaded = m_runtimeUiEditor.LoadScreen(m_runtimeUiScreens[static_cast<std::size_t>(m_runtimeUiScreenIndex)]);

    if (!styleLoaded)
    {
        m_console.Print("[UI] Failed to load style: " + stylePath);
    }
    if (!tokensLoaded)
    {
        m_console.Print("[UI] Failed to load tokens: " + tokensPath);
    }
    if (!screenLoaded)
    {
        m_console.Print("[UI] Failed to load screen: " + m_runtimeUiScreens[static_cast<std::size_t>(m_runtimeUiScreenIndex)]);
    }
    return styleLoaded && tokensLoaded && screenLoaded;
#else
    const bool styleLoaded = engine::ui::LoadStyleSheet(stylePath, m_runtimeUiStyleSheet);
    const bool tokensLoaded = engine::ui::LoadTokens(tokensPath, m_runtimeUiTokens);
    if (styleLoaded)
    {
        m_runtimeUiTree.SetStyleSheet(&m_runtimeUiStyleSheet);
    }
    if (tokensLoaded)
    {
        m_runtimeUiTree.SetTokens(&m_runtimeUiTokens);
    }
    const bool screenLoaded = LoadRuntimeUiScreen(m_runtimeUiScreens[static_cast<std::size_t>(m_runtimeUiScreenIndex)]);
    return styleLoaded && tokensLoaded && screenLoaded;
#endif
}

bool App::LoadRuntimeUiScreen(const std::string& screenPath)
{
#if BUILD_WITH_IMGUI
    const bool loaded = m_runtimeUiEditor.LoadScreen(screenPath);
    if (!loaded)
    {
        return false;
    }
#else
    auto root = engine::ui::LoadScreen(screenPath);
    if (!root)
    {
        return false;
    }
    m_runtimeUiTree.SetRoot(std::move(root));
#endif

    for (std::size_t i = 0; i < m_runtimeUiScreens.size(); ++i)
    {
        if (m_runtimeUiScreens[i] == screenPath)
        {
            m_runtimeUiScreenIndex = static_cast<int>(i);
            break;
        }
    }
    return true;
}

void App::RenderRuntimeUiOverlay(float deltaSeconds)
{
    if (m_appMode != AppMode::UiEditor || !m_showRuntimeUiOverlay)
    {
        return;
    }

    m_runtimeUiTree.SetScreenSize(m_window.FramebufferWidth(), m_window.FramebufferHeight());

    const bool interactiveRuntimeUi = !m_console.IsOpen() && !m_pauseMenuOpen;
    if (interactiveRuntimeUi)
    {
        m_runtimeUiTree.ProcessInput(&m_input, deltaSeconds);
    }
    m_runtimeUiTree.ComputeLayout();

    m_runtimeUiTree.RenderToUiSystem(m_ui);
}

void App::RenderRuntimeUiEditorPanel()
{
#if BUILD_WITH_IMGUI
    if (m_appMode != AppMode::UiEditor)
    {
        m_runtimeUiEditor.SetMode(engine::ui::EditorMode::None);
        return;
    }

    m_runtimeUiEditor.Render();

    ImGui::SetNextWindowBgAlpha(0.92F);
    if (ImGui::Begin("UI Runtime Tools", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::TextUnformatted("Dedicated UI Editor Mode");
        ImGui::Checkbox("Show Runtime Preview Overlay", &m_showRuntimeUiOverlay);

        static const char* kScreenNames[] = {"Main Menu", "Settings", "In-Game HUD"};
        int selected = m_runtimeUiScreenIndex;
        if (ImGui::Combo("Screen", &selected, kScreenNames, 3))
        {
            if (!LoadRuntimeUiScreen(m_runtimeUiScreens[static_cast<std::size_t>(selected)]))
            {
                m_console.Print("[UI] Failed to switch screen");
            }
        }

        if (ImGui::Button("Save Screen"))
        {
            if (!m_runtimeUiEditor.SaveCurrentScreen())
            {
                m_console.Print("[UI] Save failed (no active screen path)");
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Reload Screen"))
        {
            const std::string path = m_runtimeUiScreens[static_cast<std::size_t>(m_runtimeUiScreenIndex)];
            if (!LoadRuntimeUiScreen(path))
            {
                m_console.Print("[UI] Reload failed: " + path);
            }
        }

        ImGui::Separator();
        if (ImGui::Button("Back To Main Menu"))
        {
            ResetToMainMenu();
        }
        ImGui::TextUnformatted("Save writes back to /assets/ui/screens/*.json");
    }
    ImGui::End();
#endif
}

bool App::Run()
{
    std::cout << "Asymmetric Horror Prototype - Build: " << kBuildId << "\n";
    OpenNetworkLogFile();
    BuildLocalIpv4List();

    (void)LoadControlsConfig();
    (void)LoadGraphicsConfig();
    (void)LoadAudioConfig();
    (void)LoadGameplayConfig();
    (void)LoadPowersConfig();
    ApplyPowersSettings(m_powersApplied, false);
    (void)LoadAnimationConfig();
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

    m_sceneFbo.Create(m_window.FramebufferWidth(), m_window.FramebufferHeight());
    if (!m_wraithCloakRenderer.Initialize())
    {
        std::cerr << "Warning: failed to initialize Wraith cloak renderer.\n";
    }
    m_wraithCloakRenderer.SetScreenSize(m_window.FramebufferWidth(), m_window.FramebufferHeight());

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

    // Initialize threading systems
    if (!JobSystem::Instance().Initialize())
    {
        std::cerr << "Warning: failed to initialize JobSystem.\n";
    }
    if (!assets::AsyncAssetLoader::Instance().Initialize("assets"))
    {
        std::cerr << "Warning: failed to initialize AsyncAssetLoader.\n";
    }
    if (!render::RenderThread::Instance().Initialize())
    {
        std::cerr << "Warning: failed to initialize RenderThread.\n";
    }

    m_renderer.SetRenderMode(m_graphicsApplied.renderMode);

    m_window.SetResizeCallback([this](int width, int height) {
        m_renderer.SetViewport(width, height);
        m_sceneFbo.Resize(width, height);
        m_wraithCloakRenderer.SetScreenSize(width, height);
    });

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
        
        // Apply character/item/power selections from lobby
        const auto& lobbyState = m_lobbyScene.GetState();
        if (!lobbyState.selectedCharacter.empty())
        {
            if (role == "survivor")
            {
                m_gameplay.SetSelectedSurvivorCharacter(lobbyState.selectedCharacter);
            }
            else
            {
                m_gameplay.SetSelectedKillerCharacter(lobbyState.selectedCharacter);
            }
        }
        
        // Apply item/power loadouts from lobby
        if (role == "survivor")
        {
            m_gameplay.SetSurvivorItemLoadout(
                lobbyState.selectedItem,
                lobbyState.selectedAddonA,
                lobbyState.selectedAddonB
            );
        }
        else
        {
            // For killer, get the power from selected character or use the selected power
            std::string powerId = lobbyState.selectedPower;
            if (!lobbyState.selectedCharacter.empty())
            {
                const auto* killerDef = m_gameplay.GetLoadoutCatalog().FindKiller(lobbyState.selectedCharacter);
                if (killerDef != nullptr && !killerDef->powerId.empty())
                {
                    powerId = killerDef->powerId;
                }
            }
            m_gameplay.SetKillerPowerLoadout(
                powerId,
                lobbyState.selectedAddonA,
                lobbyState.selectedAddonB
            );
        }
        
        m_lobbyScene.ExitLobby();
        
        if (m_multiplayerMode == MultiplayerMode::Solo)
        {
            StartSoloSession(map, role);
        }
        else
        {
            StartMatchFromLobbyMultiplayer(map, role);
        }
    });
    m_lobbyScene.SetReadyChangedCallback([this](bool ready) {
        if (m_multiplayerMode == MultiplayerMode::Host)
        {
            for (auto& player : m_lobbyState.players)
            {
                if (player.netId == m_lobbyState.localPlayerNetId)
                {
                    player.isReady = ready;
                    break;
                }
            }
            ApplyLobbyStateToUi(m_lobbyState);
            BroadcastLobbyStateToAllClients();
        }
        else if (m_multiplayerMode == MultiplayerMode::Client)
        {
            for (auto& player : m_lobbyState.players)
            {
                if (player.netId == m_lobbyState.localPlayerNetId)
                {
                    player.isReady = ready;
                    break;
                }
            }
            
            auto& lobbyState = const_cast<game::ui::LobbySceneState&>(m_lobbyScene.GetState());
            if (lobbyState.localPlayerIndex >= 0 && lobbyState.localPlayerIndex < static_cast<int>(lobbyState.players.size()))
            {
                lobbyState.players[lobbyState.localPlayerIndex].isReady = ready;
            }
            
            NetLobbyPlayer updatePlayer;
            updatePlayer.netId = m_lobbyState.localPlayerNetId;
            updatePlayer.isReady = ready;
            for (const auto& p : m_lobbyState.players)
            {
                if (p.netId == m_lobbyState.localPlayerNetId)
                {
                    updatePlayer.name = p.name;
                    updatePlayer.selectedRole = p.selectedRole;
                    updatePlayer.characterId = p.characterId;
                    updatePlayer.isHost = p.isHost;
                    updatePlayer.isConnected = p.isConnected;
                    break;
                }
            }
            
            std::vector<std::uint8_t> data;
            if (SerializeLobbyPlayerUpdate(updatePlayer, data))
            {
                data[0] = kPacketLobbyPlayerUpdate;
                m_network.SendReliable(data.data(), data.size());
                AppendNetworkLog("Sent ready state update to host: " + std::string(ready ? "true" : "false"));
            }
        }
    });
    m_lobbyScene.SetRoleChangedCallback([this](const std::string& role) {
        m_sessionRoleName = role;

        const bool isSurvivor = (role == "survivor");
        const auto& perkSystem = m_gameplay.GetPerkSystem();
        const auto availablePerks = isSurvivor
            ? perkSystem.ListPerks(game::gameplay::perks::PerkRole::Survivor)
            : perkSystem.ListPerks(game::gameplay::perks::PerkRole::Killer);
        std::vector<std::string> perkIds = availablePerks;
        std::vector<std::string> perkNames;
        perkNames.reserve(availablePerks.size());
        for (const auto& id : availablePerks)
        {
            const auto* perk = perkSystem.GetPerk(id);
            perkNames.push_back(perk ? perk->name : id);
        }
        m_lobbyScene.SetAvailablePerks(perkIds, perkNames);
        m_lobbyScene.SetLocalPlayerPerks(isSurvivor
            ? std::array<std::string, 4>{m_menuSurvivorPerks[0], m_menuSurvivorPerks[1], m_menuSurvivorPerks[2], m_menuSurvivorPerks[3]}
            : std::array<std::string, 4>{m_menuKillerPerks[0], m_menuKillerPerks[1], m_menuKillerPerks[2], m_menuKillerPerks[3]});
    });
    m_lobbyScene.SetCharacterChangedCallback([this](const std::string& characterId) {
        if (m_sessionRoleName == "killer")
        {
            m_gameplay.SetSelectedKillerCharacter(characterId);
        }
        else
        {
            m_gameplay.SetSelectedSurvivorCharacter(characterId);
        }
    });
    m_lobbyScene.SetPerksChangedCallback([this](const std::array<std::string, 4>& perks) {
        if (m_sessionRoleName == "killer")
        {
            for (std::size_t i = 0; i < perks.size(); ++i)
            {
                m_menuKillerPerks[i] = perks[i];
            }
        }
        else
        {
            for (std::size_t i = 0; i < perks.size(); ++i)
            {
                m_menuSurvivorPerks[i] = perks[i];
            }
        }
    });
    m_lobbyScene.SetItemChangedCallback([this](const std::string& itemId, const std::string& addonA, const std::string& addonB) {
        m_gameplay.SetSurvivorItemLoadout(itemId, addonA, addonB);
    });
    m_lobbyScene.SetPowerChangedCallback([this](const std::string& powerId, const std::string& addonA, const std::string& addonB) {
        m_gameplay.SetKillerPowerLoadout(powerId, addonA, addonB);
    });
    m_lobbyScene.SetLeaveLobbyCallback([this]() {
        ResetToMainMenu();
    });
    m_lobbyScene.SetCountdownStartedCallback([this](float seconds) {
        if (m_multiplayerMode == MultiplayerMode::Host)
        {
            m_lobbyState.countdownActive = true;
            m_lobbyState.countdownTimer = seconds;
            BroadcastLobbyStateToAllClients();
            AppendNetworkLog("Countdown started: " + std::to_string(seconds) + "s - broadcasting to clients");
        }
    });
    m_lobbyScene.SetCountdownCancelledCallback([this]() {
        if (m_multiplayerMode == MultiplayerMode::Host)
        {
            m_lobbyState.countdownActive = false;
            m_lobbyState.countdownTimer = -1.0F;
            BroadcastLobbyStateToAllClients();
            AppendNetworkLog("Countdown cancelled - broadcasting to clients");
        }
    });
    m_lobbyScene.SetCharacterChangedCallback([this](const std::string& characterId) {
        // Update character selection in gameplay systems
        const bool isSurvivor = (m_sessionRoleName == "survivor");
        if (isSurvivor)
        {
            m_gameplay.SetSelectedSurvivorCharacter(characterId);
        }
        else
        {
            m_gameplay.SetSelectedKillerCharacter(characterId);
        }
    });

    if (!m_console.Initialize(m_window))
    {
        CloseNetworkLogFile();
        return false;
    }
    m_console.Print("Build: " + std::string(kBuildId));
    if (!m_devToolbar.Initialize(m_window))
    {
        m_console.Shutdown();
        CloseNetworkLogFile();
        return false;
    }

    if (!InitializeRuntimeUiSystem())
    {
        m_console.Print("[UI] Runtime UI initialized with missing assets. Editor and overlay may be incomplete.");
    }
    float currentFps = 0.0F;
    double fpsAccumulator = 0.0;
    int fpsFrames = 0;

    while (!m_window.ShouldClose() && !m_gameplay.QuitRequested())
    {
        auto& profiler = engine::core::Profiler::Instance();
        profiler.BeginFrame();

        const double frameStart = glfwGetTime();

        {
            PROFILE_SCOPE("Input");
            m_window.PollEvents();
            m_input.Update(m_window.NativeHandle());
        }
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
        const bool inUiEditor = m_appMode == AppMode::UiEditor;
        const bool inLobby = m_appMode == AppMode::Lobby;
        if ((inGame || inEditor || inUiEditor || inLobby) && !m_console.IsOpen() && m_input.IsKeyPressed(GLFW_KEY_ESCAPE))
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
        const bool controlsEnabled = (inGame || inEditor)
                                  && !m_pauseMenuOpen
                                  && !m_console.IsOpen()
                                  && !m_settingsMenuOpen
                                  && !altHeld;
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
        if (m_input.IsKeyPressed(GLFW_KEY_F8))
        {
            m_wraithCloakDebugEnabled = !m_wraithCloakDebugEnabled;
            m_statusToastMessage = m_wraithCloakDebugEnabled ? "Wraith cloak debug ON (F9 to toggle)" : "Wraith cloak debug OFF";
            m_statusToastUntilSeconds = glfwGetTime() + 2.0;
        }
        if (m_input.IsKeyPressed(GLFW_KEY_F9) && m_wraithCloakDebugEnabled)
        {
            m_wraithCloakEnabled = !m_wraithCloakEnabled;
            m_statusToastMessage = m_wraithCloakEnabled ? "Cloak ON" : "Cloak OFF";
            m_statusToastUntilSeconds = glfwGetTime() + 1.5;
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

        {
            PROFILE_SCOPE("Network");
            PollNetwork();
        }
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

        std::optional<game::gameplay::HudState> frameHudState;
        if (inGame)
        {
            PROFILE_SCOPE("Update");
            const bool canLookLocally = controlsEnabled && m_multiplayerMode != MultiplayerMode::Client;
            
            m_gameplay.Update(static_cast<float>(m_time.DeltaSeconds()), m_input, canLookLocally);
            m_audio.SetListener(m_gameplay.CameraPosition(), m_gameplay.CameraForward());
            frameHudState = m_gameplay.BuildHudState();
            UpdateTerrorRadiusAudio(static_cast<float>(m_time.DeltaSeconds()), *frameHudState);
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
        else if (inUiEditor)
        {
            m_runtimeUiTree.SetScreenSize(m_window.FramebufferWidth(), m_window.FramebufferHeight());
        }
        {
            PROFILE_SCOPE("Audio");
            m_audio.Update(static_cast<float>(m_time.DeltaSeconds()));
        }

        {
            PROFILE_SCOPE("Render");
            m_renderer.BeginFrame(glm::vec3{0.06F, 0.07F, 0.08F});
        glm::mat4 viewProjection{1.0F};
        const float aspect = m_window.FramebufferHeight() > 0
                                 ? static_cast<float>(m_window.FramebufferWidth()) / static_cast<float>(m_window.FramebufferHeight())
                                 : (16.0F / 9.0F);

        if (inGame)
        {
            m_renderer.SetLightingEnabled(true);
            m_renderer.SetPointLights(m_runtimeMapPointLights);
            m_renderer.SetSpotLights(m_runtimeMapSpotLights);
            m_gameplay.Render(m_renderer, aspect);
            viewProjection = m_gameplay.BuildViewProjection(aspect);
            m_renderer.SetCameraWorldPosition(m_gameplay.CameraPosition());
        }
        else if (inEditor)
        {
            m_renderer.SetLightingEnabled(m_levelEditor.EditorLightingEnabled());
            m_renderer.SetEnvironmentSettings(m_levelEditor.CurrentEnvironmentSettings());
            m_levelEditor.Render(m_renderer);
            viewProjection = m_levelEditor.BuildViewProjection(aspect);
            m_renderer.SetCameraWorldPosition(m_levelEditor.CameraPosition());
        }
        else if (inUiEditor)
        {
            m_renderer.SetLightingEnabled(true);
            m_renderer.SetCameraWorldPosition(glm::vec3{0.0F, 2.0F, 0.0F});
        }
        else if (inLobby)
        {
            m_renderer.SetLightingEnabled(true);
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

        // Wraith cloak shader rendering
        if (inGame && m_wraithCloakRenderer.IsInitialized())
        {
            const auto hudState = m_gameplay.BuildHudState();

            // Hide cloak effect when the local player is the killer in first-person mode
            // The cloak should only be visible to other players (or when viewing killer in 3rd person)
            const bool isLocalKillerInFirstPerson =
                hudState.roleName == "Killer" &&
                hudState.cameraModeName == "1st Person";

            // Only render if killer has wraith cloak power and is cloaking/cloaked
            // AND the local player is NOT the killer in first-person (to avoid obstructing view)
            if (hudState.killerPowerId == "wraith_cloak" && hudState.wraithCloakAmount > 0.01F && !isLocalKillerInFirstPerson)
            {
                m_wraithCloakRenderer.CaptureBackbuffer();

                m_wraithCloakParams.time = static_cast<float>(glfwGetTime());
                m_wraithCloakParams.cloakAmount = hudState.wraithCloakAmount;

                const glm::mat4 model = glm::translate(glm::mat4{1.0F}, hudState.killerWorldPosition);

                m_wraithCloakRenderer.Render(
                    viewProjection,
                    model,
                    m_gameplay.CameraPosition(),
                    hudState.killerWorldPosition,
                    hudState.killerCapsuleHeight,
                    hudState.killerCapsuleRadius,
                    m_wraithCloakParams
                );
            }
        }
        
        // Debug cloak rendering (F8 toggle)
        if (m_wraithCloakDebugEnabled && m_wraithCloakRenderer.IsInitialized())
        {
            m_wraithCloakRenderer.CaptureBackbuffer();
            
            m_wraithCloakParams.time = static_cast<float>(glfwGetTime());
            
            const float target = m_wraithCloakEnabled ? 1.0F : 0.0F;
            const float speed = 3.0F;
            m_wraithCloakParams.cloakAmount += (target - m_wraithCloakParams.cloakAmount) * speed * static_cast<float>(m_time.DeltaSeconds());
            m_wraithCloakParams.cloakAmount = std::clamp(m_wraithCloakParams.cloakAmount, 0.0F, 1.0F);
            
            if (m_wraithCloakParams.cloakAmount > 0.01F)
            {
                const glm::vec3 testPos{0.0F, 1.0F, 3.0F};
                const glm::mat4 model = glm::translate(glm::mat4{1.0F}, testPos);
                
                m_wraithCloakRenderer.Render(
                    viewProjection,
                    model,
                    inGame ? m_gameplay.CameraPosition() : (inEditor ? m_levelEditor.CameraPosition() : glm::vec3{0.0F, 2.0F, 0.0F}),
                    testPos,
                    2.0F,
                    0.4F,
                    m_wraithCloakParams
                );
            }
        }
        } // end PROFILE_SCOPE("Render")

        bool shouldQuit = false;
        bool closePauseMenu = false;
        bool backToMenu = false;

#if BUILD_WITH_IMGUI
        m_runtimeUiEditor.ProcessPendingFontLoads();
#endif
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
        else if (m_appMode == AppMode::RoleSelection)
        {
            DrawRoleSelectionScreen();
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

        if (m_appMode == AppMode::InGame && frameHudState.has_value())
        {
            const game::gameplay::HudState& hudState = *frameHudState;
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

        RenderRuntimeUiOverlay(static_cast<float>(m_time.DeltaSeconds()));

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
        RenderRuntimeUiEditorPanel();

        // Build HUD state before rendering toolbar (needed for game stats display)
        game::gameplay::HudState hudState = frameHudState.has_value() ? std::move(*frameHudState) : m_gameplay.BuildHudState();
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
            toolbarContext.profilerToggle = [this]() { m_profilerOverlay.Toggle(); };
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
        context.sceneDump = [this]() {
            if (m_appMode == AppMode::Editor)
            {
                return m_levelEditor.SceneDump();
            }
            if (m_appMode == AppMode::UiEditor)
            {
                std::ostringstream oss;
                oss << "UiEditorDump\n";
                oss << " mode=ui_editor\n";
                if (m_runtimeUiScreenIndex >= 0
                    && m_runtimeUiScreenIndex < static_cast<int>(m_runtimeUiScreens.size()))
                {
                    oss << " screen=" << m_runtimeUiScreens[static_cast<std::size_t>(m_runtimeUiScreenIndex)] << "\n";
                }
                return oss.str();
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

        context.spawnTestModels = [this]() {
            m_gameplay.SpawnTestModels();
        };

        context.spawnTestModelsHere = [this]() {
            m_gameplay.SpawnTestModelsHere();
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

        // Profiler callbacks.
        context.profilerToggle = [this]() {
            m_profilerOverlay.Toggle();
        };
        context.profilerSetPinned = [this](bool pinned) {
            m_profilerOverlay.SetPinned(pinned);
        };
        context.profilerSetCompact = [this](bool compact) {
            m_profilerOverlay.SetCompactMode(compact);
        };
        context.profilerBenchmark = [](int frames) {
            engine::core::Profiler::Instance().StartBenchmark(frames);
        };
        context.profilerBenchmarkStop = []() {
            engine::core::Profiler::Instance().StopBenchmark();
        };
        context.profilerDraw = [this, &profiler]() {
            m_profilerOverlay.Draw(profiler);
        };

        // Automated perf test callbacks.
        context.perfTest = [this](const std::string& mapName, int frames) {
            // Start a solo session on the specified map, then begin benchmark.
            std::string normalizedMap = mapName;
            if (normalizedMap == "random" || normalizedMap == "random_generation" || normalizedMap == "main_map")
            {
                normalizedMap = "main";
            }
            StartSoloSession(normalizedMap, m_sessionRoleName);
            engine::core::Profiler::Instance().StartBenchmark(frames);
        };

        context.perfReport = []() -> std::string {
            const auto& result = engine::core::Profiler::Instance().LastBenchmark();
            if (result.totalFrames == 0)
            {
                return "";
            }
            std::ostringstream ss;
            ss << "=== Benchmark Results ===\n"
               << "  Frames:        " << result.totalFrames << "\n"
               << "  Duration:      " << std::fixed << std::setprecision(2) << result.durationSeconds << "s\n"
               << "  Avg FPS:       " << std::fixed << std::setprecision(1) << result.avgFps << "\n"
               << "  Min FPS:       " << std::fixed << std::setprecision(1) << result.minFps << "\n"
               << "  Max FPS:       " << std::fixed << std::setprecision(1) << result.maxFps << "\n"
               << "  1% Low FPS:    " << std::fixed << std::setprecision(1) << result.onePercentLow << "\n"
               << "  Avg Frame:     " << std::fixed << std::setprecision(3) << result.avgFrameTimeMs << "ms\n"
               << "  P99 Frame:     " << std::fixed << std::setprecision(3) << result.p99FrameTimeMs << "ms\n"
               << "=========================";
            return ss.str();
        };

        // Threading callbacks
        context.jobStats = []() -> std::string {
            auto& js = engine::core::JobSystem::Instance();
            auto stats = js.GetStats();
            std::ostringstream ss;
            ss << "=== Job System Stats ===\n"
               << "  Workers:       " << stats.totalWorkers << "\n"
               << "  Active Jobs:   " << stats.activeWorkers << "\n"
               << "  Pending Jobs:  " << stats.pendingJobs << "\n"
               << "  Completed:     " << stats.completedJobs << "\n"
               << "  High Priority: " << stats.highPriorityPending << "\n"
               << "  Normal:        " << stats.normalPriorityPending << "\n"
               << "  Low Priority:  " << stats.lowPriorityPending << "\n"
               << "=========================";
            return ss.str();
        };

        context.jobEnabled = [](bool enabled) {
            engine::core::JobSystem::Instance().SetEnabled(enabled);
        };

        context.testParallel = [](int iterations) {
            auto& js = engine::core::JobSystem::Instance();
            std::atomic<int> counter{0};
            auto start = std::chrono::high_resolution_clock::now();
            
            js.ParallelFor(static_cast<std::size_t>(iterations), 100, [&counter](std::size_t i) {
                // Simulate some work
                volatile int x = static_cast<int>(i * i);
                (void)x;
                counter.fetch_add(1);
            }, engine::core::JobPriority::Normal);
            
            js.WaitForAll();
            
            auto end = std::chrono::high_resolution_clock::now();
            auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
            
            std::cout << "[JobTest] Completed " << counter.load() << " iterations in " << ms << "ms\n";
        };

        context.assetLoaderStats = []() -> std::string {
            auto& loader = engine::assets::AsyncAssetLoader::Instance();
            auto stats = loader.GetStats();
            std::ostringstream ss;
            ss << "=== Asset Loader Stats ===\n"
               << "  Total Loaded:  " << stats.totalLoaded << "\n"
               << "  Total Failed:  " << stats.totalFailed << "\n"
               << "  Loading Now:   " << stats.currentlyLoading << "\n"
               << "  Pending Queue: " << stats.pendingInQueue << "\n"
               << "==========================";
            return ss.str();
        };

        m_console.Render(context, currentFps, hudState);

        {
            PROFILE_SCOPE("Swap");
            m_window.SwapBuffers();
        }

        profiler.EndFrame();

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

        int effectiveFpsLimit = m_fpsLimit;
        if (m_appMode == AppMode::Lobby && !m_vsyncEnabled)
        {
            effectiveFpsLimit = 60;
        }

        static bool dbgPrinted = false;
        if (!dbgPrinted)
        {
            std::cout << "[FPS DEBUG] m_vsyncEnabled=" << m_vsyncEnabled 
                      << " m_fpsLimit=" << m_fpsLimit 
                      << " effectiveFpsLimit=" << effectiveFpsLimit << std::endl;
            dbgPrinted = true;
        }

        if (!m_vsyncEnabled && effectiveFpsLimit > 0)
        {
            const double targetSeconds = 1.0 / static_cast<double>(effectiveFpsLimit);
            double elapsed = glfwGetTime() - frameStart;
            
            if (elapsed < targetSeconds)
            {
                const double sleepThreshold = 0.002;
                const double remaining = targetSeconds - elapsed;
                if (remaining > sleepThreshold)
                {
                    std::this_thread::sleep_for(std::chrono::duration<double>(remaining - sleepThreshold));
                }
                
                while ((elapsed = glfwGetTime() - frameStart) < targetSeconds)
                {
                }
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
    m_audio.Shutdown();
    m_wraithCloakRenderer.Shutdown();
    m_sceneFbo.Destroy();
    m_renderer.Shutdown();
    
    // Shutdown threading systems
    render::RenderThread::Instance().Shutdown();
    assets::AsyncAssetLoader::Instance().Shutdown();
    JobSystem::Instance().Shutdown();
    
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

    m_lobbyState.players.clear();
    m_lobbyState.localPlayerNetId = 0;
    m_roleSelectionKillerTaken = false;
    m_roleSelectionKillerName.clear();

    m_multiplayerMode = MultiplayerMode::Solo;
    m_appMode = AppMode::MainMenu;
    m_pauseMenuOpen = false;
    m_settingsMenuOpen = false;
    m_settingsOpenedFromPause = false;
    m_menuNetStatus.clear();
    m_serverGameplayValues = false;
    ApplyGameplaySettings(m_gameplayApplied, false);

    m_renderer.SetPointLights({});
    m_renderer.SetSpotLights({});
    m_runtimeMapPointLights.clear();
    m_runtimeMapSpotLights.clear();
    m_gameplay.SetMapSpotLightCount(0);
    m_sessionRoleName = "survivor";
    m_remoteRoleName = "killer";
    m_sessionMapName = "main";
    m_sessionMapType = game::gameplay::GameplaySystems::MapType::Main;
    m_sessionSeed = std::random_device{}();
    m_showRuntimeUiOverlay = false;
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


void App::StartMatchFromLobbyMultiplayer(const std::string& mapName, const std::string& roleName)
{
    m_appMode = AppMode::InGame;
    m_pauseMenuOpen = false;
    m_settingsMenuOpen = false;
    m_settingsOpenedFromPause = false;
    m_audio.StopAll();
    m_debugAudioLoops.clear();
    m_sessionAmbienceLoop = m_audio.PlayLoop("ambience_loop", audio::AudioSystem::Bus::Ambience);
    (void)LoadTerrorRadiusProfile("default_killer");

    std::string normalizedMap = mapName;
    if (normalizedMap == "main_map")
    {
        normalizedMap = "main";
    }

    if (m_multiplayerMode == MultiplayerMode::Host)
    {
        m_serverGameplayValues = false;
        StartLoading(game::ui::LoadingScenario::HostMatch);
        m_gameplay.SetNetworkAuthorityMode(true);
        ApplyGameplaySettings(m_gameplayApplied, false);
        m_gameplay.LoadMap(normalizedMap);
        if (normalizedMap == "main")
        {
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
        ApplyMapEnvironment(normalizedMap);
        InitializePlayerBindings();
        ApplyRoleMapping(m_sessionRoleName, m_remoteRoleName, "Host role selection", true, true);
        AppendNetworkLog("Match started as host");
    }
    else
    {
        m_serverGameplayValues = true;
        StartLoading(game::ui::LoadingScenario::JoinMatch);
        m_gameplay.SetNetworkAuthorityMode(false);
        m_gameplay.LoadMap(normalizedMap);
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
        ApplyRoleMapping(m_sessionRoleName, m_remoteRoleName, "Client role assignment", false, true);
        AppendNetworkLog("Match started as client");
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
    // Allow up to 5 connections for 4 survivors + 1 killer lobby
    if (!m_network.StartHost(port, kMaxLobbyPlayers))
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
                m_menuNetStatus = "Connected. Waiting for lobby state...";
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
        std::string playerName;
        if (!DeserializeHello(payload, requestedRole, requestedMap, protocolVersion, buildId, playerName))
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

        // Check role limits before accepting
        if (!CanJoinRole(requestedRole))
        {
            std::vector<std::uint8_t> reject;
            const std::string reason = "Role " + requestedRole + " is full (4 survivors max, 1 killer max)";
            if (SerializeReject(reason, reject))
            {
                m_network.SendReliable(reject.data(), reject.size());
            }
            m_lastNetworkError = reason;
            AppendNetworkLog("Rejected client: " + reason);
            return;
        }

        // Add the new player to lobby
        NetLobbyPlayer newPlayer;
        newPlayer.netId = GenerateLocalNetId();
        newPlayer.name = playerName.empty() ? ("Player_" + std::to_string(newPlayer.netId)) : playerName;
        newPlayer.selectedRole = requestedRole;
        newPlayer.isHost = false;
        newPlayer.isConnected = true;
        
        // Temporarily set localPlayerNetId for the new client in the state we'll send
        NetLobbyState stateForNewClient = m_lobbyState;
        stateForNewClient.localPlayerNetId = newPlayer.netId;
        
        // Add player to host's lobby state
        AddLobbyPlayer(newPlayer);
        
        // Update host's own UI to show new player
        ApplyLobbyStateToUi(m_lobbyState);

        RequestRoleChange(requestedRole, true);
        SendGameplayTuningToClient();
        
        // Send lobby state to the new client with THEIR localPlayerNetId
        std::vector<std::uint8_t> dataForNewClient;
        if (SerializeLobbyState(stateForNewClient, dataForNewClient))
        {
            m_network.SendReliable(dataForNewClient.data(), dataForNewClient.size());
            AppendNetworkLog("Sent lobby state to new client (netId=" + std::to_string(newPlayer.netId) + ")");
        }
        
        // Broadcast updated lobby state to all OTHER clients (with their existing netIds)
        BroadcastLobbyStateToAllClients();

        m_menuNetStatus = "Client assigned role: " + m_remoteRoleName + " (map: " + requestedMap + ") - " + std::to_string(m_lobbyState.players.size()) + " players";
        m_lanDiscovery.UpdateHostInfo(m_sessionMapName, static_cast<int>(m_lobbyState.players.size()), 
                                       static_cast<int>(kMaxLobbyPlayers), PrimaryLocalIp());
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
        
        m_showLobbyFullPopup = true;
        m_lobbyFullMessage = reason;
        
        // Return to main menu when rejected from lobby
        m_appMode = AppMode::MainMenu;
        m_multiplayerMode = MultiplayerMode::Solo;
        m_lobbyScene.ExitLobby();
        AppendNetworkLog("Rejected by host: " + reason + " - returned to main menu");
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
        return;
    }

    // Lobby state synchronization (received by clients from host)
    if (payload[0] == kPacketLobbyState && m_multiplayerMode == MultiplayerMode::Client)
    {
        NetLobbyState state;
        if (!DeserializeLobbyState(payload, state))
        {
            AppendNetworkLog("Failed to deserialize lobby state from host.");
            return;
        }
        ApplyLobbyStateToUi(state);
        AppendNetworkLog("Received lobby state: " + std::to_string(state.players.size()) + " players.");
        return;
    }

    // Player join notification (received by all clients from host)
    if (payload[0] == kPacketLobbyPlayerJoin && m_multiplayerMode == MultiplayerMode::Client)
    {
        NetLobbyPlayer player;
        if (!DeserializeLobbyPlayerJoin(payload, player))
        {
            return;
        }
        AddLobbyPlayer(player);
        ApplyLobbyStateToUi(m_lobbyState);
        return;
    }

    // Player leave notification (received by all clients from host)
    if (payload[0] == kPacketLobbyPlayerLeave && m_multiplayerMode == MultiplayerMode::Client)
    {
        std::uint32_t netId = 0;
        if (!DeserializeLobbyPlayerLeave(payload, netId))
        {
            return;
        }
        RemoveLobbyPlayer(netId);
        ApplyLobbyStateToUi(m_lobbyState);
        return;
    }

    // Player update notification (role change, ready state, etc.)
    if (payload[0] == kPacketLobbyPlayerUpdate && m_multiplayerMode == MultiplayerMode::Host)
    {
        NetLobbyPlayer player;
        if (!DeserializeLobbyPlayerUpdate(payload, player))
        {
            return;
        }
        UpdateLobbyPlayer(player);
        ApplyLobbyStateToUi(m_lobbyState);
        BroadcastLobbyStateToAllClients();
        AppendNetworkLog("Host received player update from netId=" + std::to_string(player.netId) + " ready=" + (player.isReady ? "true" : "false"));
        return;
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

    const std::uint16_t nameLen = static_cast<std::uint16_t>(std::min<std::size_t>(m_roleSelectionPlayerName.size(), 64));
    AppendValue(outBuffer, nameLen);
    outBuffer.insert(outBuffer.end(), m_roleSelectionPlayerName.begin(), m_roleSelectionPlayerName.begin() + nameLen);

    return true;
}

bool App::DeserializeHello(
    const std::vector<std::uint8_t>& buffer,
    std::string& outRequestedRole,
    std::string& outMapName,
    int& outProtocolVersion,
    std::string& outBuildId,
    std::string& outPlayerName
) const
{
    outRequestedRole.clear();
    outMapName.clear();
    outBuildId.clear();
    outPlayerName.clear();
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
    offset += mapLen;

    std::uint16_t nameLen = 0;
    if (offset + sizeof(nameLen) <= buffer.size())
    {
        if (!ReadValue(buffer, offset, nameLen))
        {
            return true;
        }
        if (offset + nameLen <= buffer.size())
        {
            outPlayerName.assign(reinterpret_cast<const char*>(buffer.data() + offset), nameLen);
        }
    }
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

// ============================================================================
// Lobby Network Serialization
// ============================================================================

bool App::SerializeLobbyState(const NetLobbyState& state, std::vector<std::uint8_t>& outBuffer) const
{
    outBuffer.clear();
    AppendValue(outBuffer, kPacketLobbyState);

    // Local player net ID
    AppendValue(outBuffer, state.localPlayerNetId);

    // Player count
    const std::uint8_t playerCount = static_cast<std::uint8_t>(
        std::min<std::size_t>(state.players.size(), kMaxLobbyPlayers));
    AppendValue(outBuffer, playerCount);

    // Each player
    for (std::size_t i = 0; i < playerCount; ++i)
    {
        const auto& player = state.players[i];
        AppendValue(outBuffer, player.netId);

        const std::uint16_t nameLen = static_cast<std::uint16_t>(
            std::min<std::size_t>(player.name.size(), 64));
        AppendValue(outBuffer, nameLen);
        outBuffer.insert(outBuffer.end(), player.name.begin(), player.name.begin() + nameLen);

        const std::uint8_t roleByte = RoleNameToByte(player.selectedRole);
        AppendValue(outBuffer, roleByte);

        const std::uint16_t charLen = static_cast<std::uint16_t>(
            std::min<std::size_t>(player.characterId.size(), 128));
        AppendValue(outBuffer, charLen);
        outBuffer.insert(outBuffer.end(), player.characterId.begin(), player.characterId.begin() + charLen);

        std::uint8_t flags = 0;
        if (player.isReady) flags |= 0x01;
        if (player.isHost) flags |= 0x02;
        if (player.isConnected) flags |= 0x04;
        AppendValue(outBuffer, flags);
    }

    AppendValue(outBuffer, state.countdownActive);
    AppendValue(outBuffer, state.countdownTimer);

    return true;
}

bool App::DeserializeLobbyState(const std::vector<std::uint8_t>& buffer, NetLobbyState& outState) const
{
    outState.players.clear();
    std::size_t offset = 0;
    std::uint8_t type = 0;

    if (!ReadValue(buffer, offset, type) || type != kPacketLobbyState)
    {
        return false;
    }

    if (!ReadValue(buffer, offset, outState.localPlayerNetId))
    {
        return false;
    }

    std::uint8_t playerCount = 0;
    if (!ReadValue(buffer, offset, playerCount))
    {
        return false;
    }

    playerCount = std::min<std::uint8_t>(playerCount, static_cast<std::uint8_t>(kMaxLobbyPlayers));

    for (std::uint8_t i = 0; i < playerCount; ++i)
    {
        NetLobbyPlayer player;

        if (!ReadValue(buffer, offset, player.netId))
        {
            return false;
        }

        std::uint16_t nameLen = 0;
        if (!ReadValue(buffer, offset, nameLen) || offset + nameLen > buffer.size())
        {
            return false;
        }
        player.name.assign(reinterpret_cast<const char*>(buffer.data() + offset), nameLen);
        offset += nameLen;

        std::uint8_t roleByte = 0;
        if (!ReadValue(buffer, offset, roleByte))
        {
            return false;
        }
        player.selectedRole = RoleByteToName(roleByte);

        std::uint16_t charLen = 0;
        if (!ReadValue(buffer, offset, charLen) || offset + charLen > buffer.size())
        {
            return false;
        }
        player.characterId.assign(reinterpret_cast<const char*>(buffer.data() + offset), charLen);
        offset += charLen;

        std::uint8_t flags = 0;
        if (!ReadValue(buffer, offset, flags))
        {
            return false;
        }
        player.isReady = (flags & 0x01) != 0;
        player.isHost = (flags & 0x02) != 0;
        player.isConnected = (flags & 0x04) != 0;

        outState.players.push_back(player);
    }

    if (!ReadValue(buffer, offset, outState.countdownActive))
    {
        return false;
    }

    if (!ReadValue(buffer, offset, outState.countdownTimer))
    {
        return false;
    }

    return true;
}

bool App::SerializeLobbyPlayerJoin(const NetLobbyPlayer& player, std::vector<std::uint8_t>& outBuffer) const
{
    outBuffer.clear();
    AppendValue(outBuffer, kPacketLobbyPlayerJoin);
    AppendValue(outBuffer, player.netId);

    const std::uint16_t nameLen = static_cast<std::uint16_t>(
        std::min<std::size_t>(player.name.size(), 64));
    AppendValue(outBuffer, nameLen);
    outBuffer.insert(outBuffer.end(), player.name.begin(), player.name.begin() + nameLen);

    const std::uint8_t roleByte = RoleNameToByte(player.selectedRole);
    AppendValue(outBuffer, roleByte);

    const std::uint16_t charLen = static_cast<std::uint16_t>(
        std::min<std::size_t>(player.characterId.size(), 128));
    AppendValue(outBuffer, charLen);
    outBuffer.insert(outBuffer.end(), player.characterId.begin(), player.characterId.begin() + charLen);

    std::uint8_t flags = 0;
    if (player.isReady) flags |= 0x01;
    if (player.isHost) flags |= 0x02;
    if (player.isConnected) flags |= 0x04;
    AppendValue(outBuffer, flags);

    return true;
}

bool App::DeserializeLobbyPlayerJoin(const std::vector<std::uint8_t>& buffer, NetLobbyPlayer& outPlayer) const
{
    std::size_t offset = 0;
    std::uint8_t type = 0;

    if (!ReadValue(buffer, offset, type) || type != kPacketLobbyPlayerJoin)
    {
        return false;
    }

    if (!ReadValue(buffer, offset, outPlayer.netId))
    {
        return false;
    }

    std::uint16_t nameLen = 0;
    if (!ReadValue(buffer, offset, nameLen) || offset + nameLen > buffer.size())
    {
        return false;
    }
    outPlayer.name.assign(reinterpret_cast<const char*>(buffer.data() + offset), nameLen);
    offset += nameLen;

    std::uint8_t roleByte = 0;
    if (!ReadValue(buffer, offset, roleByte))
    {
        return false;
    }
    outPlayer.selectedRole = RoleByteToName(roleByte);

    std::uint16_t charLen = 0;
    if (!ReadValue(buffer, offset, charLen) || offset + charLen > buffer.size())
    {
        return false;
    }
    outPlayer.characterId.assign(reinterpret_cast<const char*>(buffer.data() + offset), charLen);
    offset += charLen;

    std::uint8_t flags = 0;
    if (!ReadValue(buffer, offset, flags))
    {
        return false;
    }
    outPlayer.isReady = (flags & 0x01) != 0;
    outPlayer.isHost = (flags & 0x02) != 0;
    outPlayer.isConnected = (flags & 0x04) != 0;

    return true;
}

bool App::SerializeLobbyPlayerLeave(std::uint32_t netId, std::vector<std::uint8_t>& outBuffer) const
{
    outBuffer.clear();
    AppendValue(outBuffer, kPacketLobbyPlayerLeave);
    AppendValue(outBuffer, netId);
    return true;
}

bool App::DeserializeLobbyPlayerLeave(const std::vector<std::uint8_t>& buffer, std::uint32_t& outNetId) const
{
    std::size_t offset = 0;
    std::uint8_t type = 0;

    if (!ReadValue(buffer, offset, type) || type != kPacketLobbyPlayerLeave)
    {
        return false;
    }

    return ReadValue(buffer, offset, outNetId);
}

bool App::SerializeLobbyPlayerUpdate(const NetLobbyPlayer& player, std::vector<std::uint8_t>& outBuffer) const
{
    // Same format as PlayerJoin
    return SerializeLobbyPlayerJoin(player, outBuffer);
}

bool App::DeserializeLobbyPlayerUpdate(const std::vector<std::uint8_t>& buffer, NetLobbyPlayer& outPlayer) const
{
    // Check packet type
    if (buffer.empty() || buffer[0] != kPacketLobbyPlayerUpdate)
    {
        return false;
    }

    // Same format as PlayerJoin, just different packet type
    std::vector<std::uint8_t> tempBuffer = buffer;
    tempBuffer[0] = kPacketLobbyPlayerJoin;
    return DeserializeLobbyPlayerJoin(tempBuffer, outPlayer);
}

// ============================================================================
// Lobby Management Functions
// ============================================================================

void App::BroadcastLobbyStateToAllClients()
{
    if (m_multiplayerMode != MultiplayerMode::Host)
    {
        return;
    }

    NetLobbyState broadcastState = m_lobbyState;
    broadcastState.localPlayerNetId = 0;

    std::vector<std::uint8_t> data;
    if (!SerializeLobbyState(broadcastState, data))
    {
        return;
    }

    // Broadcast to ALL connected clients using ENet host broadcast
    m_network.BroadcastReliable(data.data(), data.size());
    AppendNetworkLog("Broadcast lobby state to " + std::to_string(m_network.ConnectedPeerCount()) + " peers");
}

void App::SendLobbyStateToClient()
{
    if (m_multiplayerMode != MultiplayerMode::Host)
    {
        return;
    }

    std::vector<std::uint8_t> data;
    if (!SerializeLobbyState(m_lobbyState, data))
    {
        return;
    }

    // Send to the most recently connected client (uses m_connectedPeer)
    m_network.SendReliable(data.data(), data.size());
}

void App::ApplyLobbyStateToUi(const NetLobbyState& state)
{
    const std::uint32_t previousLocalNetId = m_lobbyState.localPlayerNetId;
    const bool hasPreviousLocalNetId = (m_multiplayerMode == MultiplayerMode::Client && previousLocalNetId != 0);
    
    m_lobbyState = state;
    
    if (hasPreviousLocalNetId)
    {
        m_lobbyState.localPlayerNetId = previousLocalNetId;
    }

    std::vector<game::ui::LobbyPlayer> uiPlayers;
    uiPlayers.reserve(state.players.size());

    int localPlayerIndex = -1;
    for (std::size_t i = 0; i < state.players.size(); ++i)
    {
        const auto& netPlayer = state.players[i];
        game::ui::LobbyPlayer uiPlayer;
        uiPlayer.netId = netPlayer.netId;
        uiPlayer.name = netPlayer.name;
        uiPlayer.selectedRole = netPlayer.selectedRole;
        uiPlayer.characterId = netPlayer.characterId;
        uiPlayer.isReady = netPlayer.isReady;
        uiPlayer.isHost = netPlayer.isHost;
        uiPlayer.isConnected = netPlayer.isConnected;
        uiPlayers.push_back(uiPlayer);

        if (netPlayer.netId == m_lobbyState.localPlayerNetId)
        {
            localPlayerIndex = static_cast<int>(i);
        }
    }

    // Update lobby scene state
    auto& lobbyState = const_cast<game::ui::LobbySceneState&>(m_lobbyScene.GetState());
    lobbyState.players = uiPlayers;
    lobbyState.localPlayerIndex = localPlayerIndex >= 0 ? localPlayerIndex : 0;

    // Find host
    for (const auto& p : state.players)
    {
        if (p.isHost)
        {
            lobbyState.isHost = (p.netId == m_lobbyState.localPlayerNetId);
            break;
        }
    }

    bool allReady = true;
    for (const auto& p : state.players)
    {
        if (p.isConnected && !p.isReady)
        {
            allReady = false;
            break;
        }
    }
    
    if (!allReady && lobbyState.countdownActive)
    {
        m_lobbyScene.CancelCountdown();
        AppendNetworkLog("Countdown cancelled: not all players ready");
    }

    if (state.countdownActive && !lobbyState.countdownActive)
    {
        lobbyState.countdownActive = true;
        lobbyState.countdownTimer = state.countdownTimer;
        AppendNetworkLog("Countdown started by host: " + std::to_string(state.countdownTimer) + "s");
    }
    else if (state.countdownActive && lobbyState.countdownActive)
    {
        lobbyState.countdownTimer = state.countdownTimer;
    }
    else if (!state.countdownActive && lobbyState.countdownActive)
    {
        m_lobbyScene.CancelCountdown();
        AppendNetworkLog("Countdown cancelled by host");
    }

    AppendNetworkLog("Lobby state updated: " + std::to_string(state.players.size()) + " players");
}

void App::AddLobbyPlayer(const NetLobbyPlayer& player)
{
    // Check if player already exists
    for (auto& existing : m_lobbyState.players)
    {
        if (existing.netId == player.netId)
        {
            existing = player;
            return;
        }
    }

    // Check role limits before adding
    if (!CanJoinRole(player.selectedRole))
    {
        AppendNetworkLog("Rejecting player " + player.name + ": role " + player.selectedRole + " full");
        return;
    }

    m_lobbyState.players.push_back(player);
    AppendNetworkLog("Player joined lobby: " + player.name + " (" + player.selectedRole + ")");
}

void App::RemoveLobbyPlayer(std::uint32_t netId)
{
    auto it = std::remove_if(m_lobbyState.players.begin(), m_lobbyState.players.end(),
        [netId](const NetLobbyPlayer& p) { return p.netId == netId; });

    if (it != m_lobbyState.players.end())
    {
        std::string name = it->name;
        m_lobbyState.players.erase(it, m_lobbyState.players.end());
        AppendNetworkLog("Player left lobby: " + name);
    }
}

void App::UpdateLobbyPlayer(const NetLobbyPlayer& player)
{
    for (auto& existing : m_lobbyState.players)
    {
        if (existing.netId == player.netId)
        {
            existing = player;
            AppendNetworkLog("Player updated: " + player.name + " role=" + player.selectedRole);
            return;
        }
    }
}

bool App::CanJoinRole(const std::string& role) const
{
    std::size_t countInRole = 0;
    for (const auto& player : m_lobbyState.players)
    {
        if (player.selectedRole == role && player.isConnected)
        {
            ++countInRole;
        }
    }

    if (role == "killer")
    {
        return countInRole < kMaxLobbyKillers;
    }
    else // survivor
    {
        return countInRole < kMaxLobbySurvivors;
    }
}

std::uint32_t App::GenerateLocalNetId() const
{
    // Generate a unique ID based on current state
    std::uint32_t maxId = 0;
    for (const auto& player : m_lobbyState.players)
    {
        maxId = std::max(maxId, player.netId);
    }
    return maxId + 1;
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

bool App::LoadPowersConfig()
{
    m_powersApplied = PowersTuning{};
    m_powersEditing = PowersTuning{};

    std::filesystem::create_directories("config");
    const std::filesystem::path path = std::filesystem::path("config") / "powers_tuning.json";
    if (!std::filesystem::exists(path))
    {
        return SavePowersConfig();
    }

    std::ifstream stream(path);
    if (!stream.is_open())
    {
        m_powersStatus = "Failed to open powers config.";
        return false;
    }

    json root;
    try
    {
        stream >> root;
    }
    catch (const std::exception&)
    {
        m_powersStatus = "Invalid powers config. Using defaults.";
        return SavePowersConfig();
    }

    // Bear Trap section
    if (root.contains("bear_trap") && root["bear_trap"].is_object())
    {
        const auto& bt = root["bear_trap"];
        auto readBtInt = [&](const char* key, int& target) {
            if (bt.contains(key) && bt[key].is_number_integer())
            {
                target = bt[key].get<int>();
            }
        };
        auto readBtFloat = [&](const char* key, float& target) {
            if (bt.contains(key) && bt[key].is_number())
            {
                target = bt[key].get<float>();
            }
        };
        readBtInt("start_carry_traps", m_powersApplied.trapperStartCarryTraps);
        readBtInt("max_carry_traps", m_powersApplied.trapperMaxCarryTraps);
        readBtInt("ground_spawn_traps", m_powersApplied.trapperGroundSpawnTraps);
        readBtFloat("set_trap_seconds", m_powersApplied.trapperSetTrapSeconds);
        readBtFloat("disarm_seconds", m_powersApplied.trapperDisarmSeconds);
        readBtFloat("escape_base_chance", m_powersApplied.trapEscapeBaseChance);
        readBtFloat("escape_chance_step", m_powersApplied.trapEscapeChanceStep);
        readBtFloat("escape_chance_max", m_powersApplied.trapEscapeChanceMax);
        readBtFloat("killer_stun_seconds", m_powersApplied.trapKillerStunSeconds);
    }

    // Wraith Cloak section
    if (root.contains("wraith_cloak") && root["wraith_cloak"].is_object())
    {
        const auto& wc = root["wraith_cloak"];
        auto readWcFloat = [&](const char* key, float& target) {
            if (wc.contains(key) && wc[key].is_number())
            {
                target = wc[key].get<float>();
            }
        };
        readWcFloat("cloak_speed_multiplier", m_powersApplied.wraithCloakMoveSpeedMultiplier);
        readWcFloat("cloak_transition_seconds", m_powersApplied.wraithCloakTransitionSeconds);
        readWcFloat("uncloak_transition_seconds", m_powersApplied.wraithUncloakTransitionSeconds);
        readWcFloat("post_uncloak_haste_seconds", m_powersApplied.wraithPostUncloakHasteSeconds);
        readWcFloat("cloak_vault_speed_mult", m_powersApplied.wraithCloakVaultSpeedMult);
        readWcFloat("cloak_pallet_break_speed_mult", m_powersApplied.wraithCloakPalletBreakSpeedMult);
        readWcFloat("cloak_alpha", m_powersApplied.wraithCloakAlpha);
    }

    // Hatchet Throw section
    if (root.contains("hatchet_throw") && root["hatchet_throw"].is_object())
    {
        const auto& ht = root["hatchet_throw"];
        auto readHtInt = [&](const char* key, int& target) {
            if (ht.contains(key) && ht[key].is_number_integer())
            {
                target = ht[key].get<int>();
            }
        };
        auto readHtFloat = [&](const char* key, float& target) {
            if (ht.contains(key) && ht[key].is_number())
            {
                target = ht[key].get<float>();
            }
        };
        readHtInt("max_count", m_powersApplied.hatchetMaxCount);
        readHtFloat("charge_min_seconds", m_powersApplied.hatchetChargeMinSeconds);
        readHtFloat("charge_max_seconds", m_powersApplied.hatchetChargeMaxSeconds);
        readHtFloat("throw_speed_min", m_powersApplied.hatchetThrowSpeedMin);
        readHtFloat("throw_speed_max", m_powersApplied.hatchetThrowSpeedMax);
        readHtFloat("gravity_min", m_powersApplied.hatchetGravityMin);
        readHtFloat("gravity_max", m_powersApplied.hatchetGravityMax);
        readHtFloat("air_drag", m_powersApplied.hatchetAirDrag);
        readHtFloat("collision_radius", m_powersApplied.hatchetCollisionRadius);
        readHtFloat("max_range", m_powersApplied.hatchetMaxRange);
        readHtFloat("locker_replenish_time", m_powersApplied.hatchetLockerReplenishTime);
        readHtInt("locker_replenish_count", m_powersApplied.hatchetLockerReplenishCount);
    }

    // Chainsaw Sprint section
    if (root.contains("chainsaw_sprint") && root["chainsaw_sprint"].is_object())
    {
        const auto& cs = root["chainsaw_sprint"];
        auto readCsFloat = [&](const char* key, float& target) {
            if (cs.contains(key) && cs[key].is_number())
            {
                target = cs[key].get<float>();
            }
        };
        readCsFloat("charge_time", m_powersApplied.chainsawChargeTime);
        readCsFloat("sprint_speed_multiplier", m_powersApplied.chainsawSprintSpeedMultiplier);
        readCsFloat("turn_boost_window", m_powersApplied.chainsawTurnBoostWindow);
        readCsFloat("turn_boost_rate", m_powersApplied.chainsawTurnBoostRate);
        readCsFloat("turn_restricted_rate", m_powersApplied.chainsawTurnRestrictedRate);
        readCsFloat("collision_recovery_duration", m_powersApplied.chainsawCollisionRecoveryDuration);
        readCsFloat("recovery_hit_duration", m_powersApplied.chainsawRecoveryHitDuration);
        readCsFloat("recovery_cancel_duration", m_powersApplied.chainsawRecoveryCancelDuration);
        readCsFloat("overheat_per_second_charge", m_powersApplied.chainsawOverheatPerSecondCharge);
        readCsFloat("overheat_per_second_sprint", m_powersApplied.chainsawOverheatPerSecondSprint);
        readCsFloat("overheat_cooldown_rate", m_powersApplied.chainsawOverheatCooldownRate);
        readCsFloat("overheat_buff_threshold", m_powersApplied.chainsawOverheatBuffThreshold);
        readCsFloat("overheat_charge_bonus", m_powersApplied.chainsawOverheatChargeBonus);
        readCsFloat("overheat_speed_bonus", m_powersApplied.chainsawOverheatSpeedBonus);
        readCsFloat("overheat_turn_bonus", m_powersApplied.chainsawOverheatTurnBonus);
        readCsFloat("collision_raycast_distance", m_powersApplied.chainsawCollisionRaycastDistance);
        readCsFloat("survivor_hit_radius", m_powersApplied.chainsawSurvivorHitRadius);
        readCsFloat("charge_slowdown_multiplier", m_powersApplied.chainsawChargeSlowdownMultiplier);
    }

    m_powersEditing = m_powersApplied;
    return true;
}

bool App::SavePowersConfig() const
{
    std::filesystem::create_directories("config");
    const std::filesystem::path path = std::filesystem::path("config") / "powers_tuning.json";

    json root;
    root["asset_version"] = m_powersApplied.assetVersion;

    // Bear Trap section
    json bearTrap;
    bearTrap["start_carry_traps"] = m_powersApplied.trapperStartCarryTraps;
    bearTrap["max_carry_traps"] = m_powersApplied.trapperMaxCarryTraps;
    bearTrap["ground_spawn_traps"] = m_powersApplied.trapperGroundSpawnTraps;
    bearTrap["set_trap_seconds"] = m_powersApplied.trapperSetTrapSeconds;
    bearTrap["disarm_seconds"] = m_powersApplied.trapperDisarmSeconds;
    bearTrap["escape_base_chance"] = m_powersApplied.trapEscapeBaseChance;
    bearTrap["escape_chance_step"] = m_powersApplied.trapEscapeChanceStep;
    bearTrap["escape_chance_max"] = m_powersApplied.trapEscapeChanceMax;
    bearTrap["killer_stun_seconds"] = m_powersApplied.trapKillerStunSeconds;
    root["bear_trap"] = bearTrap;

    // Wraith Cloak section
    json wraithCloak;
    wraithCloak["cloak_speed_multiplier"] = m_powersApplied.wraithCloakMoveSpeedMultiplier;
    wraithCloak["cloak_transition_seconds"] = m_powersApplied.wraithCloakTransitionSeconds;
    wraithCloak["uncloak_transition_seconds"] = m_powersApplied.wraithUncloakTransitionSeconds;
    wraithCloak["post_uncloak_haste_seconds"] = m_powersApplied.wraithPostUncloakHasteSeconds;
    wraithCloak["cloak_vault_speed_mult"] = m_powersApplied.wraithCloakVaultSpeedMult;
    wraithCloak["cloak_pallet_break_speed_mult"] = m_powersApplied.wraithCloakPalletBreakSpeedMult;
    wraithCloak["cloak_alpha"] = m_powersApplied.wraithCloakAlpha;
    root["wraith_cloak"] = wraithCloak;

    // Hatchet Throw section
    json hatchetThrow;
    hatchetThrow["max_count"] = m_powersApplied.hatchetMaxCount;
    hatchetThrow["charge_min_seconds"] = m_powersApplied.hatchetChargeMinSeconds;
    hatchetThrow["charge_max_seconds"] = m_powersApplied.hatchetChargeMaxSeconds;
    hatchetThrow["throw_speed_min"] = m_powersApplied.hatchetThrowSpeedMin;
    hatchetThrow["throw_speed_max"] = m_powersApplied.hatchetThrowSpeedMax;
    hatchetThrow["gravity_min"] = m_powersApplied.hatchetGravityMin;
    hatchetThrow["gravity_max"] = m_powersApplied.hatchetGravityMax;
    hatchetThrow["air_drag"] = m_powersApplied.hatchetAirDrag;
    hatchetThrow["collision_radius"] = m_powersApplied.hatchetCollisionRadius;
    hatchetThrow["max_range"] = m_powersApplied.hatchetMaxRange;
    hatchetThrow["locker_replenish_time"] = m_powersApplied.hatchetLockerReplenishTime;
    hatchetThrow["locker_replenish_count"] = m_powersApplied.hatchetLockerReplenishCount;
    root["hatchet_throw"] = hatchetThrow;

    // Chainsaw Sprint section
    json chainsawSprint;
    chainsawSprint["charge_time"] = m_powersApplied.chainsawChargeTime;
    chainsawSprint["sprint_speed_multiplier"] = m_powersApplied.chainsawSprintSpeedMultiplier;
    chainsawSprint["turn_boost_window"] = m_powersApplied.chainsawTurnBoostWindow;
    chainsawSprint["turn_boost_rate"] = m_powersApplied.chainsawTurnBoostRate;
    chainsawSprint["turn_restricted_rate"] = m_powersApplied.chainsawTurnRestrictedRate;
    chainsawSprint["collision_recovery_duration"] = m_powersApplied.chainsawCollisionRecoveryDuration;
    chainsawSprint["recovery_hit_duration"] = m_powersApplied.chainsawRecoveryHitDuration;
    chainsawSprint["recovery_cancel_duration"] = m_powersApplied.chainsawRecoveryCancelDuration;
    chainsawSprint["overheat_per_second_charge"] = m_powersApplied.chainsawOverheatPerSecondCharge;
    chainsawSprint["overheat_per_second_sprint"] = m_powersApplied.chainsawOverheatPerSecondSprint;
    chainsawSprint["overheat_cooldown_rate"] = m_powersApplied.chainsawOverheatCooldownRate;
    chainsawSprint["overheat_buff_threshold"] = m_powersApplied.chainsawOverheatBuffThreshold;
    chainsawSprint["overheat_charge_bonus"] = m_powersApplied.chainsawOverheatChargeBonus;
    chainsawSprint["overheat_speed_bonus"] = m_powersApplied.chainsawOverheatSpeedBonus;
    chainsawSprint["overheat_turn_bonus"] = m_powersApplied.chainsawOverheatTurnBonus;
    chainsawSprint["collision_raycast_distance"] = m_powersApplied.chainsawCollisionRaycastDistance;
    chainsawSprint["survivor_hit_radius"] = m_powersApplied.chainsawSurvivorHitRadius;
    chainsawSprint["charge_slowdown_multiplier"] = m_powersApplied.chainsawChargeSlowdownMultiplier;
    root["chainsaw_sprint"] = chainsawSprint;

    std::ofstream stream(path);
    if (!stream.is_open())
    {
        return false;
    }
    stream << root.dump(2) << "\n";
    return true;
}

void App::ApplyPowersSettings(const PowersTuning& tuning, bool fromServer)
{
    // Update the powers-specific fields in gameplay tuning
    m_gameplayApplied.trapperStartCarryTraps = tuning.trapperStartCarryTraps;
    m_gameplayApplied.trapperMaxCarryTraps = tuning.trapperMaxCarryTraps;
    m_gameplayApplied.trapperGroundSpawnTraps = tuning.trapperGroundSpawnTraps;
    m_gameplayApplied.trapperSetTrapSeconds = glm::max(0.1F, tuning.trapperSetTrapSeconds);
    m_gameplayApplied.trapperDisarmSeconds = glm::max(0.1F, tuning.trapperDisarmSeconds);
    m_gameplayApplied.trapEscapeBaseChance = glm::clamp(tuning.trapEscapeBaseChance, 0.0F, 1.0F);
    m_gameplayApplied.trapEscapeChanceStep = glm::clamp(tuning.trapEscapeChanceStep, 0.0F, 1.0F);
    m_gameplayApplied.trapEscapeChanceMax = glm::clamp(tuning.trapEscapeChanceMax, 0.0F, 1.0F);
    m_gameplayApplied.trapKillerStunSeconds = glm::max(0.1F, tuning.trapKillerStunSeconds);

    m_gameplayApplied.wraithCloakMoveSpeedMultiplier = glm::max(0.1F, tuning.wraithCloakMoveSpeedMultiplier);
    m_gameplayApplied.wraithCloakTransitionSeconds = glm::max(0.1F, tuning.wraithCloakTransitionSeconds);
    m_gameplayApplied.wraithUncloakTransitionSeconds = glm::max(0.1F, tuning.wraithUncloakTransitionSeconds);
    m_gameplayApplied.wraithPostUncloakHasteSeconds = glm::max(0.0F, tuning.wraithPostUncloakHasteSeconds);
    m_gameplayApplied.wraithCloakVaultSpeedMult = glm::max(1.0F, tuning.wraithCloakVaultSpeedMult);
    m_gameplayApplied.wraithCloakPalletBreakSpeedMult = glm::max(1.0F, tuning.wraithCloakPalletBreakSpeedMult);
    m_gameplayApplied.wraithCloakAlpha = glm::clamp(tuning.wraithCloakAlpha, 0.0F, 1.0F);

    // Hatchet Throw
    m_gameplayApplied.hatchetMaxCount = glm::max(1, tuning.hatchetMaxCount);
    m_gameplayApplied.hatchetChargeMinSeconds = glm::max(0.0F, tuning.hatchetChargeMinSeconds);
    m_gameplayApplied.hatchetChargeMaxSeconds = glm::max(0.1F, tuning.hatchetChargeMaxSeconds);
    m_gameplayApplied.hatchetThrowSpeedMin = glm::max(1.0F, tuning.hatchetThrowSpeedMin);
    m_gameplayApplied.hatchetThrowSpeedMax = glm::max(1.0F, tuning.hatchetThrowSpeedMax);
    m_gameplayApplied.hatchetGravityMin = glm::max(0.1F, tuning.hatchetGravityMin);
    m_gameplayApplied.hatchetGravityMax = glm::max(0.1F, tuning.hatchetGravityMax);
    m_gameplayApplied.hatchetAirDrag = glm::clamp(tuning.hatchetAirDrag, 0.8F, 1.0F);
    m_gameplayApplied.hatchetCollisionRadius = glm::max(0.01F, tuning.hatchetCollisionRadius);
    m_gameplayApplied.hatchetMaxRange = glm::max(1.0F, tuning.hatchetMaxRange);
    m_gameplayApplied.hatchetLockerReplenishTime = glm::max(0.1F, tuning.hatchetLockerReplenishTime);
    m_gameplayApplied.hatchetLockerReplenishCount = glm::max(1, tuning.hatchetLockerReplenishCount);

    // Chainsaw Sprint - apply directly to GameplaySystems for live tuning
    m_gameplay.ApplyChainsawConfig(
        glm::max(0.1F, tuning.chainsawChargeTime),
        glm::max(1.0F, tuning.chainsawSprintSpeedMultiplier),
        glm::max(0.1F, tuning.chainsawTurnBoostWindow),
        glm::max(10.0F, tuning.chainsawTurnBoostRate),
        glm::max(10.0F, tuning.chainsawTurnRestrictedRate),
        glm::max(0.1F, tuning.chainsawCollisionRecoveryDuration),
        glm::max(0.1F, tuning.chainsawRecoveryHitDuration),
        glm::max(0.1F, tuning.chainsawRecoveryCancelDuration),
        glm::max(1.0F, tuning.chainsawOverheatPerSecondCharge),
        glm::max(1.0F, tuning.chainsawOverheatPerSecondSprint),
        glm::max(1.0F, tuning.chainsawOverheatCooldownRate),
        glm::max(50.0F, tuning.chainsawOverheatBuffThreshold),
        glm::clamp(tuning.chainsawOverheatChargeBonus, 0.0F, 1.0F),
        glm::clamp(tuning.chainsawOverheatSpeedBonus, 0.0F, 1.0F),
        glm::clamp(tuning.chainsawOverheatTurnBonus, 0.0F, 1.0F),
        glm::max(0.5F, tuning.chainsawCollisionRaycastDistance),
        glm::max(0.5F, tuning.chainsawSurvivorHitRadius),
        glm::clamp(tuning.chainsawChargeSlowdownMultiplier, 0.0F, 1.0F)
    );

    m_gameplay.ApplyGameplayTuning(m_gameplayApplied);

    m_powersApplied = tuning;
    if (!fromServer)
    {
        m_powersEditing = tuning;
        m_gameplayEditing = m_gameplayApplied;
    }
}

void App::SendPowersTuningToClient()
{
    if (m_multiplayerMode != MultiplayerMode::Host || !m_network.IsConnected())
    {
        return;
    }

    std::vector<std::uint8_t> payload;
    payload.push_back(0x50); // 'P' packet type for powers tuning

    auto appendFloat = [&payload](float v) {
        const std::uint8_t* ptr = reinterpret_cast<const std::uint8_t*>(&v);
        for (std::size_t i = 0; i < sizeof(float); ++i)
        {
            payload.push_back(ptr[i]);
        }
    };
    auto appendInt = [&payload](int v) {
        const std::uint8_t* ptr = reinterpret_cast<const std::uint8_t*>(&v);
        for (std::size_t i = 0; i < sizeof(int); ++i)
        {
            payload.push_back(ptr[i]);
        }
    };

    // Bear Trap
    appendInt(m_powersApplied.trapperStartCarryTraps);
    appendInt(m_powersApplied.trapperMaxCarryTraps);
    appendInt(m_powersApplied.trapperGroundSpawnTraps);
    appendFloat(m_powersApplied.trapperSetTrapSeconds);
    appendFloat(m_powersApplied.trapperDisarmSeconds);
    appendFloat(m_powersApplied.trapEscapeBaseChance);
    appendFloat(m_powersApplied.trapEscapeChanceStep);
    appendFloat(m_powersApplied.trapEscapeChanceMax);
    appendFloat(m_powersApplied.trapKillerStunSeconds);

    // Wraith Cloak
    appendFloat(m_powersApplied.wraithCloakMoveSpeedMultiplier);
    appendFloat(m_powersApplied.wraithCloakTransitionSeconds);
    appendFloat(m_powersApplied.wraithUncloakTransitionSeconds);
    appendFloat(m_powersApplied.wraithPostUncloakHasteSeconds);

    m_network.SendReliable(payload.data(), payload.size());
}

bool App::LoadAnimationConfig()
{
    m_animationApplied = AnimationSettings{};
    m_animationEditing = AnimationSettings{};

    std::filesystem::create_directories("config");
    const std::filesystem::path path = std::filesystem::path("config") / "animation.json";
    if (!std::filesystem::exists(path))
    {
        return SaveAnimationConfig();
    }

    std::ifstream stream(path);
    if (!stream.is_open())
    {
        m_animationStatus = "Failed to open animation config.";
        return false;
    }

    json root;
    try
    {
        stream >> root;
    }
    catch (const std::exception&)
    {
        m_animationStatus = "Invalid animation config. Using defaults.";
        return SaveAnimationConfig();
    }

    auto readFloat = [&](const char* key, float& target) {
        if (root.contains(key) && root[key].is_number())
        {
            target = root[key].get<float>();
        }
    };

    readFloat("idle_epsilon", m_animationApplied.idleEpsilon);
    readFloat("run_threshold", m_animationApplied.runThreshold);
    readFloat("blend_idle_walk", m_animationApplied.blendIdleWalk);
    readFloat("blend_walk_run", m_animationApplied.blendWalkRun);
    readFloat("blend_run_idle", m_animationApplied.blendRunIdle);
    readFloat("global_anim_scale", m_animationApplied.globalAnimScale);
    readFloat("walk_speed_ref", m_animationApplied.walkSpeedRef);
    readFloat("run_speed_ref", m_animationApplied.runSpeedRef);
    readFloat("min_walk_scale", m_animationApplied.minWalkScale);
    readFloat("max_walk_scale", m_animationApplied.maxWalkScale);
    readFloat("min_run_scale", m_animationApplied.minRunScale);
    readFloat("max_run_scale", m_animationApplied.maxRunScale);

    m_animationEditing = m_animationApplied;
    ApplyAnimationSettings(m_animationApplied);
    return true;
}

bool App::SaveAnimationConfig() const
{
    std::filesystem::create_directories("config");
    const std::filesystem::path path = std::filesystem::path("config") / "animation.json";

    json root;
    root["asset_version"] = m_animationApplied.assetVersion;
    root["idle_epsilon"] = m_animationApplied.idleEpsilon;
    root["run_threshold"] = m_animationApplied.runThreshold;
    root["blend_idle_walk"] = m_animationApplied.blendIdleWalk;
    root["blend_walk_run"] = m_animationApplied.blendWalkRun;
    root["blend_run_idle"] = m_animationApplied.blendRunIdle;
    root["global_anim_scale"] = m_animationApplied.globalAnimScale;
    root["walk_speed_ref"] = m_animationApplied.walkSpeedRef;
    root["run_speed_ref"] = m_animationApplied.runSpeedRef;
    root["min_walk_scale"] = m_animationApplied.minWalkScale;
    root["max_walk_scale"] = m_animationApplied.maxWalkScale;
    root["min_run_scale"] = m_animationApplied.minRunScale;
    root["max_run_scale"] = m_animationApplied.maxRunScale;

    std::ofstream stream(path);
    if (!stream.is_open())
    {
        return false;
    }
    stream << root.dump(2) << "\n";
    return true;
}

void App::ApplyAnimationSettings(const AnimationSettings& settings)
{
    m_animationApplied = settings;

    // Apply to animation system via GameplaySystems
    engine::animation::LocomotionProfile profile;
    profile.idleEpsilon = settings.idleEpsilon;
    profile.runThreshold = settings.runThreshold;
    profile.blendIdleWalk = settings.blendIdleWalk;
    profile.blendWalkRun = settings.blendWalkRun;
    profile.blendRunIdle = settings.blendRunIdle;
    profile.globalAnimScale = settings.globalAnimScale;
    profile.walkSpeedRef = settings.walkSpeedRef;
    profile.runSpeedRef = settings.runSpeedRef;
    profile.minWalkScale = settings.minWalkScale;
    profile.maxWalkScale = settings.maxWalkScale;
    profile.minRunScale = settings.minRunScale;
    profile.maxRunScale = settings.maxRunScale;

    m_gameplay.GetAnimationSystem().SetProfile(profile);
    m_gameplay.GetAnimationSystem().InitializeStateMachine();
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

void App::UpdateTerrorRadiusAudio(float deltaSeconds, const game::gameplay::HudState& hudState)
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
        const bool chaseActive = hudState.chaseActive;
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

    // Check if killer is undetectable - silence all TR audio
    if (hudState.killerUndetectable)
    {
        for (TerrorRadiusLayerAudio& layer : m_terrorAudioProfile.layers)
        {
            if (layer.handle != 0 && layer.currentVolume > 0.0F)
            {
                (void)m_audio.SetHandleVolume(layer.handle, 0.0F);
                layer.currentVolume = 0.0F;
            }
        }
        m_currentBand = TerrorRadiusBand::Outside;
        return;
    }

    // Calculate XZ (horizontal) distance from Survivor to Killer
    const glm::vec3 survivor = m_gameplay.RolePosition("survivor");
    const glm::vec3 killer = m_gameplay.RolePosition("killer");
    const glm::vec2 delta{survivor.x - killer.x, survivor.z - killer.z};
    const float distance = glm::length(delta);
    const float radius = std::max(1.0F, m_terrorAudioProfile.baseRadius);
    const bool chaseActive = hudState.chaseActive;

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
    const auto hudState = m_gameplay.BuildHudState();

    // Phase B1: Local role and audio routing info
    out += "Local Role: " + m_localPlayer.controlledRole + "\n";
    const bool localPlayerIsSurvivor = (m_localPlayer.controlledRole == "survivor");
    const bool localPlayerIsKiller = (m_localPlayer.controlledRole == "killer");
    out += "TR Enabled: " + std::string(localPlayerIsSurvivor ? "YES" : "NO") + "\n";
    if (localPlayerIsKiller)
    {
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
    const std::vector<std::string> mapItems{"Test", "Collision Test", "Random Generation"};
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

    auto collectLobbyAddons = [&]() {
        std::vector<std::string> lobbyAddons;
        auto appendUnique = [&lobbyAddons](const std::vector<std::string>& source) {
            for (const auto& id : source)
            {
                if (id == "none")
                {
                    continue;
                }
                if (std::find(lobbyAddons.begin(), lobbyAddons.end(), id) == lobbyAddons.end())
                {
                    lobbyAddons.push_back(id);
                }
            }
        };
        appendUnique(survivorAddonOptions);
        appendUnique(killerAddonOptions);
        return lobbyAddons;
    };

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

    // Session settings - simplified to role and map only
    m_ui.Dropdown("menu_role", "Role", &m_menuRoleIndex, roleItems);
    m_ui.Dropdown("menu_map", "Map", &m_menuMapIndex, mapItems);

    const std::string roleName = RoleNameFromIndex(m_menuRoleIndex);
    const std::string mapName = MapNameFromIndex(m_menuMapIndex);
    
    // Apply default selections for PLAY button (defaults only - lobby handles custom selections)
    auto applyMenuGameplaySelections = [&]() {
        // Keep currently selected survivor when valid; otherwise prefer Dwight if available.
        if (!survivorCharacters.empty())
        {
            std::string survivorId = m_gameplay.SelectedSurvivorCharacterId();
            if (std::find(survivorCharacters.begin(), survivorCharacters.end(), survivorId) == survivorCharacters.end())
            {
                const auto dwightIt =
                    std::find(survivorCharacters.begin(), survivorCharacters.end(), std::string{"survivor_dwight"});
                survivorId = (dwightIt != survivorCharacters.end()) ? *dwightIt : survivorCharacters.front();
            }
            m_gameplay.SetSelectedSurvivorCharacter(survivorId);
        }
        if (!killerCharacters.empty())
        {
            m_gameplay.SetSelectedKillerCharacter(killerCharacters.front());
        }
        // Clear item/power loadouts (let gameplay systems use defaults)
        m_gameplay.SetSurvivorItemLoadout("", "", "");
        m_gameplay.SetKillerPowerLoadout("", "", "");
    };

    // Configure lobby UI selections based on role (perks, characters, items, powers, addons)
    auto configureLobbyUiSelections = [&](const std::string& currentRoleName) {
        const bool isSurvivor = (currentRoleName == "survivor");

        const auto& perkSystem = m_gameplay.GetPerkSystem();
        const auto availablePerks = isSurvivor
            ? perkSystem.ListPerks(game::gameplay::perks::PerkRole::Survivor)
            : perkSystem.ListPerks(game::gameplay::perks::PerkRole::Killer);
        std::vector<std::string> perkIds = availablePerks;
        std::vector<std::string> perkNames;
        perkNames.reserve(availablePerks.size());
        for (const auto& id : availablePerks)
        {
            const auto* perk = perkSystem.GetPerk(id);
            perkNames.push_back(perk ? perk->name : id);
        }
        m_lobbyScene.SetAvailablePerks(perkIds, perkNames);

        m_lobbyScene.SetAvailableCharacters(survivorCharacters, survivorCharacters, killerCharacters, killerCharacters);
        m_lobbyScene.SetAvailableItems(survivorItems, survivorItems);
        m_lobbyScene.SetAvailablePowers(killerPowers, killerPowers);

        const auto lobbyAddonIds = collectLobbyAddons();
        m_lobbyScene.SetAvailableAddons(lobbyAddonIds, lobbyAddonIds);

        if (isSurvivor)
        {
            if (m_menuSurvivorCharacterIndex >= 0 && m_menuSurvivorCharacterIndex < static_cast<int>(survivorCharacters.size()))
            {
                m_lobbyScene.SetLocalPlayerCharacter(survivorCharacters[static_cast<std::size_t>(m_menuSurvivorCharacterIndex)]);
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
            m_lobbyScene.SetLocalPlayerItem(
                itemId,
                itemAddonA == "none" ? "" : itemAddonA,
                itemAddonB == "none" ? "" : itemAddonB
            );
        }
        else
        {
            if (m_menuKillerCharacterIndex >= 0 && m_menuKillerCharacterIndex < static_cast<int>(killerCharacters.size()))
            {
                m_lobbyScene.SetLocalPlayerCharacter(killerCharacters[static_cast<std::size_t>(m_menuKillerCharacterIndex)]);
            }

            const std::string powerId = (!killerPowers.empty() && m_menuKillerPowerIndex >= 0)
                ? killerPowers[static_cast<std::size_t>(m_menuKillerPowerIndex)]
                : std::string{};
            const std::string powerAddonA = (!killerAddonOptions.empty() && m_menuKillerAddonAIndex >= 0)
                ? killerAddonOptions[static_cast<std::size_t>(m_menuKillerAddonAIndex)]
                : std::string{"none"};
            const std::string powerAddonB = (!killerAddonOptions.empty() && m_menuKillerAddonBIndex >= 0)
                ? killerAddonOptions[static_cast<std::size_t>(m_menuKillerAddonBIndex)]
                : std::string{"none"};
            m_lobbyScene.SetLocalPlayerPower(
                powerId,
                powerAddonA == "none" ? "" : powerAddonA,
                powerAddonB == "none" ? "" : powerAddonB
            );
        }
    };

    m_ui.Spacer(12.0F * scale);
    if (m_ui.Button("play_solo", "PLAY", true, &m_ui.Theme().colorAccent))
    {
        applyMenuGameplaySelections();
        StartSoloSession(mapName, roleName);
    }
    if (m_ui.Button("enter_lobby", "LOBBY (3D)"))
    {
        m_appMode = AppMode::Lobby;
        
        // Initialize lobby state
        m_lobbyState.players.clear();
        m_lobbyState.localPlayerNetId = 1;
        
        NetLobbyPlayer localPlayer;
        localPlayer.netId = 1;
        localPlayer.name = "Player";
        localPlayer.selectedRole = roleName;
        localPlayer.isHost = true;
        localPlayer.isConnected = true;
        m_lobbyState.players.push_back(localPlayer);
        
        ApplyLobbyStateToUi(m_lobbyState);
        m_lobbyScene.SetLocalPlayerRole(roleName);
        configureLobbyUiSelections(roleName);
        m_lobbyScene.EnterLobby();
    }

    if (!savedMaps.empty())
    {
        m_ui.Spacer(8.0F * scale);
        m_ui.Dropdown("saved_maps", "Saved Map", &m_menuSavedMapIndex, savedMaps);
        if (m_ui.Button("play_saved", "PLAY SAVED"))
        {
            applyMenuGameplaySelections();
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
        m_roleSelectionIsHost = true;
        m_roleSelectionKillerTaken = false;
        m_roleSelectionKillerName.clear();
        m_appMode = AppMode::RoleSelection;
    }
    if (m_ui.Button("join_btn", "JOIN GAME"))
    {
        m_roleSelectionIsHost = false;
        m_roleSelectionKillerTaken = false;
        m_roleSelectionKillerName.clear();
        m_appMode = AppMode::RoleSelection;
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
#if BUILD_WITH_IMGUI
    if (m_ui.Button("ui_editor_mode", "UI EDITOR"))
    {
        m_lanDiscovery.Stop();
        m_network.Disconnect();
        m_gameplay.SetNetworkAuthorityMode(false);
        m_gameplay.ClearRemoteRoleCommands();
        m_multiplayerMode = MultiplayerMode::Solo;
        m_pauseMenuOpen = false;
        m_settingsMenuOpen = false;
        m_settingsOpenedFromPause = false;
        m_showRuntimeUiOverlay = true;
        m_appMode = AppMode::UiEditor;
        m_runtimeUiEditor.SetMode(engine::ui::EditorMode::Edit);

        const std::string path = m_runtimeUiScreens[static_cast<std::size_t>(m_runtimeUiScreenIndex)];
        if (!LoadRuntimeUiScreen(path))
        {
            m_console.Print("[UI] Failed to load screen for UI Editor mode: " + path);
        }

        m_menuNetStatus = "Entered UI Editor";
        TransitionNetworkState(NetworkState::Offline, "UI editor mode");
    }
#else
    m_ui.Label("UI Editor requires ImGui build", m_ui.Theme().colorTextMuted, 0.85F);
#endif

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
                applyMenuGameplaySelections();
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
    m_ui.EndScrollRegion();

    m_ui.Spacer(10.0F * scale);
    m_ui.Label("~ Console | F6 UI", m_ui.Theme().colorTextMuted, 0.8F);
    m_ui.Label("F7 Load | UI Editor from Main Menu", m_ui.Theme().colorTextMuted, 0.8F);
    m_ui.EndPanel();
    
    // Lobby Full Popup
    if (m_showLobbyFullPopup)
    {
        const auto& popupTheme = m_ui.Theme();
        const float scaleX = static_cast<float>(m_ui.ScreenWidth()) / static_cast<float>(m_window.WindowWidth());
        const float scaleY = static_cast<float>(m_ui.ScreenHeight()) / static_cast<float>(m_window.WindowHeight());
        const float popupW = 400.0F * scale;
        const float popupH = 180.0F * scale;
        const engine::ui::UiRect popupRect{
            (screenW - popupW) * 0.5F,
            (screenH - popupH) * 0.5F,
            popupW,
            popupH
        };
        
        m_ui.FillRect(popupRect, glm::vec4{0.1F, 0.1F, 0.12F, 0.98F});
        m_ui.DrawRectOutline(popupRect, 3.0F, popupTheme.colorDanger);
        
        const std::string title = "LOBBY FULL";
        m_ui.DrawTextLabel(popupRect.x + (popupRect.w - m_ui.TextWidth(title, 1.4F)) * 0.5F, popupRect.y + 20.0F * scale, title, popupTheme.colorDanger, 1.4F);
        
        const std::string& reason = m_lobbyFullMessage.empty() ? "Could not join the lobby." : m_lobbyFullMessage;
        m_ui.DrawTextLabel(popupRect.x + 20.0F * scale, popupRect.y + 60.0F * scale, reason, popupTheme.colorTextMuted, 0.9F);
        
        const engine::ui::UiRect okBtnRect{
            popupRect.x + (popupRect.w - 120.0F * scale) * 0.5F,
            popupRect.y + popupH - 50.0F * scale,
            120.0F * scale,
            36.0F * scale
        };
        
        const glm::vec2 mousePos = m_input.MousePosition();
        const bool okHovered = okBtnRect.Contains(mousePos.x * scaleX, mousePos.y * scaleY);
        glm::vec4 okBtnColor = okHovered ? popupTheme.colorButtonHover : popupTheme.colorAccent;
        okBtnColor.a = 0.9F;
        m_ui.FillRect(okBtnRect, okBtnColor);
        m_ui.DrawRectOutline(okBtnRect, 2.0F, popupTheme.colorPanelBorder);
        m_ui.DrawTextLabel(okBtnRect.x + (okBtnRect.w - m_ui.TextWidth("OK", 1.0F)) * 0.5F, okBtnRect.y + 8.0F * scale, "OK", popupTheme.colorText, 1.0F);
        
        if (okHovered && m_input.IsMousePressed(0))
        {
            m_showLobbyFullPopup = false;
            m_lobbyFullMessage.clear();
        }
    }
}

void App::DrawRoleSelectionScreen()
{
    const float scale = m_ui.Scale();
    const float screenW = static_cast<float>(m_ui.ScreenWidth());
    const float screenH = static_cast<float>(m_ui.ScreenHeight());
    const auto& theme = m_ui.Theme();
    const float scaleX = static_cast<float>(m_ui.ScreenWidth()) / static_cast<float>(m_window.WindowWidth());
    const float scaleY = static_cast<float>(m_ui.ScreenHeight()) / static_cast<float>(m_window.WindowHeight());
    
    // Check lobby state for existing killer
    m_roleSelectionKillerTaken = false;
    m_roleSelectionKillerName.clear();
    for (const auto& player : m_lobbyState.players)
    {
        if (player.selectedRole == "killer")
        {
            m_roleSelectionKillerTaken = true;
            m_roleSelectionKillerName = player.name;
            break;
        }
    }
    
    // Full screen dark overlay
    m_ui.FillRect(engine::ui::UiRect{0.0F, 0.0F, screenW, screenH}, glm::vec4{0.02F, 0.02F, 0.02F, 0.95F});
    
    // Title
    const std::string titleText = m_roleSelectionIsHost ? "CHOOSE YOUR ROLE" : "JOIN LOBBY";
    const float titleW = m_ui.TextWidth(titleText, 1.6F);
    m_ui.DrawTextLabel((screenW - titleW) * 0.5F, 30.0F * scale, titleText, theme.colorText, 1.6F);
    
    // Subtitle for client
    float subtitleY = 75.0F * scale;
    if (!m_roleSelectionIsHost)
    {
        const std::string subtitleText = "Connecting to " + m_menuJoinIp + ":" + std::to_string(m_menuPort);
        const float subtitleW = m_ui.TextWidth(subtitleText, 0.9F);
        m_ui.DrawTextLabel((screenW - subtitleW) * 0.5F, subtitleY, subtitleText, theme.colorTextMuted, 0.9F);
        subtitleY += 25.0F * scale;
    }
    
    // Nickname input field
    const float nicknameFieldWidth = 280.0F * scale;
    const float nicknameFieldHeight = 40.0F * scale;
    const float nicknameX = (screenW - nicknameFieldWidth) * 0.5F;
    const float nicknameY = subtitleY + 15.0F * scale;
    
    // Label
    const std::string nicknameLabel = "Your Name:";
    m_ui.DrawTextLabel(nicknameX, nicknameY - 18.0F * scale, nicknameLabel, theme.colorTextMuted, 0.85F);
    
    // Input field background
    const engine::ui::UiRect nicknameRect{nicknameX, nicknameY, nicknameFieldWidth, nicknameFieldHeight};
    m_ui.FillRect(nicknameRect, glm::vec4{0.12F, 0.14F, 0.18F, 0.95F});
    m_ui.DrawRectOutline(nicknameRect, 2.0F, theme.colorPanelBorder);
    
    // Display current nickname
    m_ui.DrawTextLabel(nicknameX + 12.0F * scale, nicknameY + 10.0F * scale, m_roleSelectionPlayerName, theme.colorText, 1.0F);
    
    // Handle nickname input (click to focus, type to edit)
    const glm::vec2 mousePos = m_input.MousePosition();
    const bool nicknameHovered = nicknameRect.Contains(mousePos.x * scaleX, mousePos.y * scaleY);
    
    static bool s_nicknameFocused = false;
    static std::string s_focusedId;
    
    if (nicknameHovered && m_input.IsMousePressed(0))
    {
        s_nicknameFocused = true;
        s_focusedId = "role_nickname";
    }
    else if (m_input.IsMousePressed(0) && !nicknameHovered)
    {
        s_nicknameFocused = false;
        s_focusedId.clear();
    }
    
    if (s_nicknameFocused && s_focusedId == "role_nickname")
    {
        const bool shiftHeld = m_input.IsKeyDown(GLFW_KEY_LEFT_SHIFT) || m_input.IsKeyDown(GLFW_KEY_RIGHT_SHIFT);
        
        // Letter keys (A-Z)
        for (int key = GLFW_KEY_A; key <= GLFW_KEY_Z; ++key)
        {
            if (m_input.IsKeyPressed(key))
            {
                char c = static_cast<char>(key - GLFW_KEY_A + (shiftHeld ? 'A' : 'a'));
                if (m_roleSelectionPlayerName.length() < 16)
                {
                    m_roleSelectionPlayerName += c;
                }
            }
        }
        
        // Number keys (0-9)
        for (int key = GLFW_KEY_0; key <= GLFW_KEY_9; ++key)
        {
            if (m_input.IsKeyPressed(key))
            {
                char c = static_cast<char>('0' + (key - GLFW_KEY_0));
                if (m_roleSelectionPlayerName.length() < 16)
                {
                    m_roleSelectionPlayerName += c;
                }
            }
        }
        
        // Space
        if (m_input.IsKeyPressed(GLFW_KEY_SPACE))
        {
            if (m_roleSelectionPlayerName.length() < 16)
            {
                m_roleSelectionPlayerName += ' ';
            }
        }
        
        // Underscore (shift + minus)
        if (m_input.IsKeyPressed(GLFW_KEY_MINUS) && shiftHeld)
        {
            if (m_roleSelectionPlayerName.length() < 16)
            {
                m_roleSelectionPlayerName += '_';
            }
        }
        
        // Handle backspace with repeat
        static float s_backspaceTimer = 0.0F;
        static bool s_backspaceWaiting = false;
        
        if (m_input.IsKeyDown(GLFW_KEY_BACKSPACE) && !m_roleSelectionPlayerName.empty())
        {
            if (m_input.IsKeyPressed(GLFW_KEY_BACKSPACE))
            {
                // First press - immediate delete
                m_roleSelectionPlayerName.pop_back();
                s_backspaceTimer = 0.0F;
                s_backspaceWaiting = true;
            }
            else if (s_backspaceWaiting)
            {
                // Holding - repeat after delay
                s_backspaceTimer += 0.016F; // Approximate frame time
                if (s_backspaceTimer > 0.4F) // Initial delay
                {
                    s_backspaceTimer -= 0.05F; // Repeat rate
                    m_roleSelectionPlayerName.pop_back();
                }
            }
        }
        else
        {
            s_backspaceWaiting = false;
            s_backspaceTimer = 0.0F;
        }
        
        // Cursor blink
        static float s_cursorBlink = 0.0F;
        s_cursorBlink += 0.05F;
        if (static_cast<int>(s_cursorBlink) % 2 == 0)
        {
            const float cursorX = nicknameX + 12.0F * scale + m_ui.TextWidth(m_roleSelectionPlayerName, 1.0F) + 2.0F;
            m_ui.FillRect(engine::ui::UiRect{cursorX, nicknameY + 8.0F * scale, 2.0F * scale, 24.0F * scale}, theme.colorText);
        }
    }
    
    // Role cards layout (adjusted for nickname field)
    const float cardWidth = 300.0F * scale;
    const float cardHeight = 380.0F * scale;
    const float cardSpacing = 60.0F * scale;
    const float totalWidth = cardWidth * 2.0F + cardSpacing;
    const float startX = (screenW - totalWidth) * 0.5F;
    const float cardY = (screenH - cardHeight) * 0.5F + 40.0F * scale;  // Shift down for nickname field
    
    // Survivor card (left)
    {
        const float cardX = startX;
        const engine::ui::UiRect cardRect{cardX, cardY, cardWidth, cardHeight};
        
        // Card background
        glm::vec4 survivorColor = theme.colorAccent;
        survivorColor.a = 0.15F;
        m_ui.FillRect(cardRect, survivorColor);
        m_ui.DrawRectOutline(cardRect, 3.0F, theme.colorAccent);
        
        // Icon area (placeholder - would be character icon)
        const float iconY = cardY + 30.0F * scale;
        m_ui.DrawTextLabel(cardX + cardWidth * 0.5F - m_ui.TextWidth("S", 4.0F) * 0.5F, iconY, "S", theme.colorAccent, 4.0F);
        
        // Role name
        const std::string roleText = "SURVIVOR";
        m_ui.DrawTextLabel(cardX + cardWidth * 0.5F - m_ui.TextWidth(roleText, 1.4F) * 0.5F, iconY + 80.0F * scale, roleText, theme.colorText, 1.4F);
        
        // Description
        const std::string descText = "Work together to repair\ngenerators and escape.";
        m_ui.DrawTextLabel(cardX + 20.0F * scale, iconY + 130.0F * scale, descText, theme.colorTextMuted, 0.85F);
        
        // Player info - survivors (no limit shown, just available)
        m_ui.DrawTextLabel(cardX + 20.0F * scale, cardY + cardHeight - 80.0F * scale, "Available", theme.colorTextMuted, 0.9F);
        
        // Select button
        const engine::ui::UiRect btnRect{cardX + 20.0F * scale, cardY + cardHeight - 50.0F * scale, cardWidth - 40.0F * scale, 40.0F * scale};
        const glm::vec2 mousePos = m_input.MousePosition();
        const bool hovered = btnRect.Contains(mousePos.x * scaleX, mousePos.y * scaleY);
        
        glm::vec4 btnColor = hovered ? theme.colorButtonHover : theme.colorAccent;
        btnColor.a = 0.9F;
        m_ui.FillRect(btnRect, btnColor);
        m_ui.DrawRectOutline(btnRect, 2.0F, theme.colorPanelBorder);
        
        const std::string btnText = "SELECT";
        m_ui.DrawTextLabel(btnRect.x + (btnRect.w - m_ui.TextWidth(btnText)) * 0.5F, btnRect.y + 10.0F * scale, btnText, theme.colorText, 1.0F);
        
        if (hovered && m_input.IsMousePressed(0))
        {
            // Select survivor and enter lobby
            const std::string selectedRole = "survivor";
            
            if (m_roleSelectionIsHost)
            {
                // Initialize lobby state for host
                m_lobbyState.players.clear();
                m_lobbyState.localPlayerNetId = 1;
                
                NetLobbyPlayer localPlayer;
                localPlayer.netId = 1;
                localPlayer.name = m_roleSelectionPlayerName;
                localPlayer.selectedRole = selectedRole;
                localPlayer.isHost = true;
                localPlayer.isConnected = true;
                m_lobbyState.players.push_back(localPlayer);
                
                m_multiplayerMode = MultiplayerMode::Host;
                
                // Start listening for connections
                if (!m_network.StartHost(m_menuPort, kMaxLobbyPlayers))
                {
                    m_menuNetStatus = "Failed to start lobby server.";
                    TransitionNetworkState(NetworkState::Error, m_menuNetStatus, true);
                    m_appMode = AppMode::MainMenu;
                    return;
                }
                
                m_menuNetStatus = "Lobby started. Waiting for players...";
                TransitionNetworkState(NetworkState::HostListening, "Lobby server started");
                std::string hostName = "DBD-Host";
                const char* hostNameEnv = std::getenv("COMPUTERNAME");
                if (hostNameEnv == nullptr)
                {
                    hostNameEnv = std::getenv("HOSTNAME");
                }
                if (hostNameEnv != nullptr)
                {
                    hostName = hostNameEnv;
                }
                m_lanDiscovery.StartHost(
                    m_lanDiscoveryPort,
                    m_menuPort,
                    hostName,
                    "lobby",
                    1,
                    static_cast<int>(kMaxLobbyPlayers),
                    kProtocolVersion,
                    kBuildId,
                    PrimaryLocalIp()
                );
            }
            else
            {
                // Client joining
                m_multiplayerMode = MultiplayerMode::Client;
                m_lobbyState.players.clear();
                m_lobbyState.localPlayerNetId = 0;
                
                if (!m_network.StartClient(m_menuJoinIp, m_menuPort))
                {
                    m_menuNetStatus = "Failed to connect to host.";
                    TransitionNetworkState(NetworkState::Error, m_menuNetStatus, true);
                    m_appMode = AppMode::MainMenu;
                    return;
                }
                
                m_menuNetStatus = "Connecting to " + m_menuJoinIp + ":" + std::to_string(m_menuPort) + "...";
                TransitionNetworkState(NetworkState::ClientConnecting, m_menuNetStatus);
            }
            
            m_appMode = AppMode::Lobby;
            ApplyLobbyStateToUi(m_lobbyState);
            m_lobbyScene.SetLocalPlayerRole(selectedRole);
            
            // Configure lobby UI selections
            const auto survivorCharacters = m_gameplay.ListSurvivorCharacters();
            const auto killerCharacters = m_gameplay.ListKillerCharacters();
            const auto survivorItems = m_gameplay.GetLoadoutCatalog().ListItemIds();
            const auto killerPowers = m_gameplay.GetLoadoutCatalog().ListPowerIds();
            m_lobbyScene.SetAvailableCharacters(survivorCharacters, survivorCharacters, killerCharacters, killerCharacters);
            m_lobbyScene.SetAvailableItems(survivorItems, survivorItems);
            m_lobbyScene.SetAvailablePowers(killerPowers, killerPowers);
            
            const auto& perkSystem = m_gameplay.GetPerkSystem();
            const auto availablePerks = perkSystem.ListPerks(game::gameplay::perks::PerkRole::Survivor);
            std::vector<std::string> perkNames;
            for (const auto& id : availablePerks)
            {
                const auto* perk = perkSystem.GetPerk(id);
                perkNames.push_back(perk ? perk->name : id);
            }
            m_lobbyScene.SetAvailablePerks(availablePerks, perkNames);
            
            m_lobbyScene.EnterLobby();
        }
    }
    
    // Killer card (right)
    {
        const float cardX = startX + cardWidth + cardSpacing;
        const engine::ui::UiRect cardRect{cardX, cardY, cardWidth, cardHeight};
        
        const bool killerTaken = m_roleSelectionKillerTaken;
        
        // Card background
        glm::vec4 killerColor = killerTaken ? glm::vec4{0.3F, 0.3F, 0.3F, 0.3F} : glm::vec4{theme.colorDanger.r, theme.colorDanger.g, theme.colorDanger.b, 0.15F};
        m_ui.FillRect(cardRect, killerColor);
        m_ui.DrawRectOutline(cardRect, 3.0F, killerTaken ? glm::vec4{0.4F, 0.4F, 0.4F, 1.0F} : theme.colorDanger);
        
        // Icon area
        const float iconY = cardY + 30.0F * scale;
        m_ui.DrawTextLabel(cardX + cardWidth * 0.5F - m_ui.TextWidth("K", 4.0F) * 0.5F, iconY, "K", 
                          killerTaken ? glm::vec4{0.5F, 0.5F, 0.5F, 1.0F} : theme.colorDanger, 4.0F);
        
        // Role name
        const std::string roleText = "KILLER";
        m_ui.DrawTextLabel(cardX + cardWidth * 0.5F - m_ui.TextWidth(roleText, 1.4F) * 0.5F, iconY + 80.0F * scale, roleText, 
                          killerTaken ? glm::vec4{0.5F, 0.5F, 0.5F, 1.0F} : theme.colorText, 1.4F);
        
        // Description
        const std::string descText = "Hunt and sacrifice all\nsurvivors before they escape.";
        m_ui.DrawTextLabel(cardX + 20.0F * scale, iconY + 130.0F * scale, descText, theme.colorTextMuted, 0.85F);
        
        // Player info
        if (killerTaken && !m_roleSelectionKillerName.empty())
        {
            m_ui.DrawTextLabel(cardX + 20.0F * scale, cardY + cardHeight - 100.0F * scale, "Taken by:", glm::vec4{0.6F, 0.6F, 0.6F, 1.0F}, 0.9F);
            m_ui.DrawTextLabel(cardX + 20.0F * scale, cardY + cardHeight - 75.0F * scale, m_roleSelectionKillerName, glm::vec4{0.7F, 0.4F, 0.4F, 1.0F}, 0.85F);
        }
        else
        {
            m_ui.DrawTextLabel(cardX + 20.0F * scale, cardY + cardHeight - 100.0F * scale, "Available", theme.colorTextMuted, 0.9F);
        }
        
        // Select button (disabled if taken)
        const engine::ui::UiRect btnRect{cardX + 20.0F * scale, cardY + cardHeight - 50.0F * scale, cardWidth - 40.0F * scale, 40.0F * scale};
        const glm::vec2 mousePos = m_input.MousePosition();
        const bool hovered = !killerTaken && btnRect.Contains(mousePos.x * scaleX, mousePos.y * scaleY);
        
        glm::vec4 btnColor = killerTaken ? glm::vec4{0.25F, 0.25F, 0.25F, 0.8F} : (hovered ? theme.colorButtonHover : theme.colorDanger);
        if (!killerTaken) btnColor.a = 0.9F;
        m_ui.FillRect(btnRect, btnColor);
        m_ui.DrawRectOutline(btnRect, 2.0F, killerTaken ? glm::vec4{0.3F, 0.3F, 0.3F, 1.0F} : theme.colorPanelBorder);
        
        const std::string btnText = killerTaken ? "TAKEN" : "SELECT";
        m_ui.DrawTextLabel(btnRect.x + (btnRect.w - m_ui.TextWidth(btnText)) * 0.5F, btnRect.y + 10.0F * scale, btnText, 
                          killerTaken ? glm::vec4{0.5F, 0.5F, 0.5F, 1.0F} : theme.colorText, 1.0F);
        
        if (!killerTaken && hovered && m_input.IsMousePressed(0))
        {
            // Select killer and enter lobby
            const std::string selectedRole = "killer";
            
            if (m_roleSelectionIsHost)
            {
                // Initialize lobby state for host
                m_lobbyState.players.clear();
                m_lobbyState.localPlayerNetId = 1;
                
                NetLobbyPlayer localPlayer;
                localPlayer.netId = 1;
                localPlayer.name = m_roleSelectionPlayerName;
                localPlayer.selectedRole = selectedRole;
                localPlayer.isHost = true;
                localPlayer.isConnected = true;
                m_lobbyState.players.push_back(localPlayer);
                
                m_multiplayerMode = MultiplayerMode::Host;
                
                // Start listening for connections
                if (!m_network.StartHost(m_menuPort, kMaxLobbyPlayers))
                {
                    m_menuNetStatus = "Failed to start lobby server.";
                    TransitionNetworkState(NetworkState::Error, m_menuNetStatus, true);
                    m_appMode = AppMode::MainMenu;
                    return;
                }
                
                m_menuNetStatus = "Lobby started. Waiting for players...";
                TransitionNetworkState(NetworkState::HostListening, "Lobby server started");
                std::string hostName = "DBD-Host";
                const char* hostNameEnv = std::getenv("COMPUTERNAME");
                if (hostNameEnv == nullptr)
                {
                    hostNameEnv = std::getenv("HOSTNAME");
                }
                if (hostNameEnv != nullptr)
                {
                    hostName = hostNameEnv;
                }
                m_lanDiscovery.StartHost(
                    m_lanDiscoveryPort,
                    m_menuPort,
                    hostName,
                    "lobby",
                    1,
                    static_cast<int>(kMaxLobbyPlayers),
                    kProtocolVersion,
                    kBuildId,
                    PrimaryLocalIp()
                );
            }
            else
            {
                // Client joining
                m_multiplayerMode = MultiplayerMode::Client;
                m_lobbyState.players.clear();
                m_lobbyState.localPlayerNetId = 0;
                
                if (!m_network.StartClient(m_menuJoinIp, m_menuPort))
                {
                    m_menuNetStatus = "Failed to connect to host.";
                    TransitionNetworkState(NetworkState::Error, m_menuNetStatus, true);
                    m_appMode = AppMode::MainMenu;
                    return;
                }
                
                m_menuNetStatus = "Connecting to " + m_menuJoinIp + ":" + std::to_string(m_menuPort) + "...";
                TransitionNetworkState(NetworkState::ClientConnecting, m_menuNetStatus);
            }
            
            m_appMode = AppMode::Lobby;
            ApplyLobbyStateToUi(m_lobbyState);
            m_lobbyScene.SetLocalPlayerRole(selectedRole);
            
            // Configure lobby UI selections
            const auto survivorCharacters = m_gameplay.ListSurvivorCharacters();
            const auto killerCharacters = m_gameplay.ListKillerCharacters();
            const auto survivorItems = m_gameplay.GetLoadoutCatalog().ListItemIds();
            const auto killerPowers = m_gameplay.GetLoadoutCatalog().ListPowerIds();
            m_lobbyScene.SetAvailableCharacters(survivorCharacters, survivorCharacters, killerCharacters, killerCharacters);
            m_lobbyScene.SetAvailableItems(survivorItems, survivorItems);
            m_lobbyScene.SetAvailablePowers(killerPowers, killerPowers);
            
            const auto& perkSystem = m_gameplay.GetPerkSystem();
            const auto availablePerks = perkSystem.ListPerks(game::gameplay::perks::PerkRole::Killer);
            std::vector<std::string> perkNames;
            for (const auto& id : availablePerks)
            {
                const auto* perk = perkSystem.GetPerk(id);
                perkNames.push_back(perk ? perk->name : id);
            }
            m_lobbyScene.SetAvailablePerks(availablePerks, perkNames);
            
            m_lobbyScene.EnterLobby();
        }
    }
    
    // Back button
    const float backBtnW = 120.0F * scale;
    const float backBtnH = 40.0F * scale;
    const engine::ui::UiRect backBtnRect{20.0F * scale, screenH - backBtnH - 20.0F * scale, backBtnW, backBtnH};
    const bool backHovered = backBtnRect.Contains(mousePos.x * scaleX, mousePos.y * scaleY);
    
    glm::vec4 backColor = backHovered ? theme.colorButtonHover : theme.colorButton;
    m_ui.FillRect(backBtnRect, backColor);
    m_ui.DrawRectOutline(backBtnRect, 2.0F, theme.colorPanelBorder);
    m_ui.DrawTextLabel(backBtnRect.x + (backBtnRect.w - m_ui.TextWidth("BACK")) * 0.5F, backBtnRect.y + 10.0F * scale, "BACK", theme.colorText, 0.9F);
    
    if (backHovered && m_input.IsMousePressed(0))
    {
        m_appMode = AppMode::MainMenu;
    }
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
    const float panelW = std::min(1320.0F * scale, static_cast<float>(m_ui.ScreenWidth()) - 20.0F);
    const float panelH = std::min(820.0F * scale, static_cast<float>(m_ui.ScreenHeight()) - 20.0F);
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

    constexpr int kSettingsTabCount = 7;
    m_settingsTabIndex = glm::clamp(m_settingsTabIndex, 0, kSettingsTabCount - 1);
    m_ui.PushLayout(engine::ui::UiSystem::LayoutAxis::Horizontal, 8.0F, 0.0F);
    {
        const glm::vec4 tabColor = m_ui.Theme().colorAccent;
        const float tabGap = 8.0F;
        const float availableTabsWidth = std::max(0.0F, panelW - tabGap * static_cast<float>(kSettingsTabCount - 1));
        const float tabWidth = glm::min(180.0F * scale, availableTabsWidth / static_cast<float>(kSettingsTabCount));
        if (m_ui.Button("tab_controls", "Controls", true, m_settingsTabIndex == 0 ? &tabColor : nullptr, tabWidth))
        {
            m_settingsTabIndex = 0;
        }
        if (m_ui.Button("tab_graphics", "Graphics", true, m_settingsTabIndex == 1 ? &tabColor : nullptr, tabWidth))
        {
            m_settingsTabIndex = 1;
        }
        if (m_ui.Button("tab_audio", "Audio", true, m_settingsTabIndex == 2 ? &tabColor : nullptr, tabWidth))
        {
            m_settingsTabIndex = 2;
        }
        if (m_ui.Button("tab_gameplay", "Gameplay", true, m_settingsTabIndex == 3 ? &tabColor : nullptr, tabWidth))
        {
            m_settingsTabIndex = 3;
        }
        if (m_ui.Button("tab_hitboxes", "Hitboxes", true, m_settingsTabIndex == 4 ? &tabColor : nullptr, tabWidth))
        {
            m_settingsTabIndex = 4;
        }
        if (m_ui.Button("tab_powers", "Powers", true, m_settingsTabIndex == 5 ? &tabColor : nullptr, tabWidth))
        {
            m_settingsTabIndex = 5;
        }
        if (m_ui.Button("tab_animation", "Locomotion", true, m_settingsTabIndex == 6 ? &tabColor : nullptr, tabWidth))
        {
            m_settingsTabIndex = 6;
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
    else if (m_settingsTabIndex == 3)
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
    else if (m_settingsTabIndex == 4)
    {
        const bool allowEdit = m_multiplayerMode != MultiplayerMode::Client;
        if (!allowEdit)
        {
            m_ui.Label("Read-only on clients. Host values are authoritative.", m_ui.Theme().colorDanger);
        }

        auto& t = m_gameplayEditing;
        auto& a = m_animationEditing;
        static int s_lastVisitedTab = -1;
        static std::array<std::string, 4> s_hitboxInputs;
        auto formatFloat = [](float value) {
            char buffer[32]{};
            std::snprintf(buffer, sizeof(buffer), "%.3f", value);
            return std::string{buffer};
        };
        auto tryParseClamped = [](const std::string& text, float minValue, float maxValue, float* outValue) {
            if (outValue == nullptr)
            {
                return false;
            }
            try
            {
                std::size_t parsedChars = 0;
                const float parsed = std::stof(text, &parsedChars);
                if (parsedChars != text.size())
                {
                    return false;
                }
                *outValue = glm::clamp(parsed, minValue, maxValue);
                return true;
            }
            catch (...)
            {
                return false;
            }
        };
        if (s_lastVisitedTab != m_settingsTabIndex)
        {
            s_hitboxInputs[0] = formatFloat(t.survivorCapsuleRadius);
            s_hitboxInputs[1] = formatFloat(t.survivorCapsuleHeight);
            s_hitboxInputs[2] = formatFloat(t.killerCapsuleRadius);
            s_hitboxInputs[3] = formatFloat(t.killerCapsuleHeight);
            s_lastVisitedTab = m_settingsTabIndex;
        }

        m_ui.Label("Hitbox Actions", m_ui.Theme().colorAccent);
        m_ui.PushLayout(engine::ui::UiSystem::LayoutAxis::Horizontal, 8.0F, 0.0F);
        if (m_ui.Button("apply_hitbox_btn", "Apply Hitboxes", allowEdit, &m_ui.Theme().colorSuccess, 180.0F))
        {
            (void)tryParseClamped(s_hitboxInputs[0], 0.2F, 1.2F, &t.survivorCapsuleRadius);
            (void)tryParseClamped(s_hitboxInputs[1], 0.9F, 3.0F, &t.survivorCapsuleHeight);
            (void)tryParseClamped(s_hitboxInputs[2], 0.2F, 1.2F, &t.killerCapsuleRadius);
            (void)tryParseClamped(s_hitboxInputs[3], 0.9F, 3.0F, &t.killerCapsuleHeight);
            ApplyGameplaySettings(m_gameplayEditing, false);
            if (m_multiplayerMode == MultiplayerMode::Host)
            {
                SendGameplayTuningToClient();
            }
            m_gameplayStatus = "Hitbox tuning applied.";
            s_hitboxInputs[0] = formatFloat(t.survivorCapsuleRadius);
            s_hitboxInputs[1] = formatFloat(t.survivorCapsuleHeight);
            s_hitboxInputs[2] = formatFloat(t.killerCapsuleRadius);
            s_hitboxInputs[3] = formatFloat(t.killerCapsuleHeight);
        }
        if (m_ui.Button("save_hitbox_btn", "Save Gameplay File", allowEdit, nullptr, 190.0F))
        {
            const auto previousApplied = m_gameplayApplied;
            m_gameplayApplied = m_gameplayEditing;
            const bool saved = SaveGameplayConfig();
            m_gameplayApplied = previousApplied;
            m_gameplayStatus = saved ? "Saved hitbox tuning to config/gameplay_tuning.json." : "Failed to save gameplay tuning file.";
        }
        if (m_ui.Button("reset_hitbox_defaults_btn", "Reset Hitboxes Defaults", allowEdit, &m_ui.Theme().colorDanger, 220.0F))
        {
            const game::gameplay::GameplaySystems::GameplayTuning defaults{};
            t.survivorCapsuleRadius = defaults.survivorCapsuleRadius;
            t.survivorCapsuleHeight = defaults.survivorCapsuleHeight;
            t.killerCapsuleRadius = defaults.killerCapsuleRadius;
            t.killerCapsuleHeight = defaults.killerCapsuleHeight;
            s_hitboxInputs[0] = formatFloat(t.survivorCapsuleRadius);
            s_hitboxInputs[1] = formatFloat(t.survivorCapsuleHeight);
            s_hitboxInputs[2] = formatFloat(t.killerCapsuleRadius);
            s_hitboxInputs[3] = formatFloat(t.killerCapsuleHeight);
            ApplyGameplaySettings(m_gameplayEditing, false);
            if (m_multiplayerMode == MultiplayerMode::Host)
            {
                SendGameplayTuningToClient();
            }
            m_gameplayStatus = "Hitboxes reset to defaults.";
        }
        m_ui.PopLayout();

        m_ui.Label("Capsule Hitboxes", m_ui.Theme().colorAccent);
        bool hitboxFieldChanged = false;
        if (m_ui.InputText("hb_surv_radius_input", "Survivor Radius", &s_hitboxInputs[0], 24))
        {
            if (tryParseClamped(s_hitboxInputs[0], 0.2F, 1.2F, &t.survivorCapsuleRadius))
            {
                hitboxFieldChanged = true;
            }
        }
        if (m_ui.InputText("hb_surv_height_input", "Survivor Height", &s_hitboxInputs[1], 24))
        {
            if (tryParseClamped(s_hitboxInputs[1], 0.9F, 3.0F, &t.survivorCapsuleHeight))
            {
                hitboxFieldChanged = true;
            }
        }
        if (m_ui.InputText("hb_killer_radius_input", "Killer Radius", &s_hitboxInputs[2], 24))
        {
            if (tryParseClamped(s_hitboxInputs[2], 0.2F, 1.2F, &t.killerCapsuleRadius))
            {
                hitboxFieldChanged = true;
            }
        }
        if (m_ui.InputText("hb_killer_height_input", "Killer Height", &s_hitboxInputs[3], 24))
        {
            if (tryParseClamped(s_hitboxInputs[3], 0.9F, 3.0F, &t.killerCapsuleHeight))
            {
                hitboxFieldChanged = true;
            }
        }
        if (hitboxFieldChanged && allowEdit)
        {
            ApplyGameplaySettings(m_gameplayEditing, false);
            if (m_multiplayerMode == MultiplayerMode::Host)
            {
                SendGameplayTuningToClient();
            }
            m_gameplayStatus = "Hitbox tuning applied.";
        }
        m_ui.Label(
            "Type numeric values, then click Apply Hitboxes. Values are clamped to safe gameplay ranges.",
            m_ui.Theme().colorTextMuted);

        m_ui.Label("Quick Animation", m_ui.Theme().colorAccent);
        m_ui.SliderFloat("hb_anim_global_scale", "Global Anim Scale", &a.globalAnimScale, 0.1F, 3.0F, "%.2f");
        m_ui.SliderFloat("hb_anim_idle_eps", "Idle Epsilon", &a.idleEpsilon, 0.01F, 1.0F, "%.2f");
        m_ui.SliderFloat("hb_anim_run_threshold", "Run Threshold", &a.runThreshold, 2.0F, 6.0F, "%.2f");
        if (m_ui.Button("hb_anim_apply_btn", "Apply Animation", true, &m_ui.Theme().colorSuccess, 180.0F))
        {
            ApplyAnimationSettings(m_animationEditing);
            m_animationStatus = "Animation settings applied.";
        }
        m_ui.Label("For full animation tuning, use the Locomotion tab.", m_ui.Theme().colorTextMuted);

        if (!m_gameplayStatus.empty())
        {
            m_ui.Label(m_gameplayStatus, m_ui.Theme().colorTextMuted);
        }
        if (!m_animationStatus.empty())
        {
            m_ui.Label(m_animationStatus, m_ui.Theme().colorTextMuted);
        }
    }
    else if (m_settingsTabIndex == 5)
    {
        const bool allowEdit = m_multiplayerMode != MultiplayerMode::Client;
        if (!allowEdit)
        {
            m_ui.Label("Read-only on clients. Host values are authoritative.", m_ui.Theme().colorDanger);
        }
        auto& p = m_powersEditing;

        m_ui.Label("Config Actions", m_ui.Theme().colorAccent);
        m_ui.PushLayout(engine::ui::UiSystem::LayoutAxis::Horizontal, 8.0F, 0.0F);
        if (m_ui.Button("apply_powers_btn", "Apply", allowEdit, &m_ui.Theme().colorSuccess, 165.0F))
        {
            ApplyPowersSettings(m_powersEditing, false);
            if (m_multiplayerMode == MultiplayerMode::Host)
            {
                SendPowersTuningToClient();
            }
            m_powersStatus = "Powers tuning applied.";
        }
        if (m_ui.Button("save_powers_btn", "Save To File", allowEdit, nullptr, 165.0F))
        {
            m_powersApplied = m_powersEditing;
            const bool saved = SavePowersConfig();
            m_powersStatus = saved ? "Saved to config/powers_tuning.json." : "Failed to save powers tuning file.";
        }
        if (m_ui.Button("load_powers_btn", "Load From File", true, nullptr, 165.0F))
        {
            if (LoadPowersConfig())
            {
                if (allowEdit)
                {
                    ApplyPowersSettings(m_powersEditing, false);
                    if (m_multiplayerMode == MultiplayerMode::Host)
                    {
                        SendPowersTuningToClient();
                    }
                }
                m_powersStatus = allowEdit ? "Loaded from file and applied." : "Loaded local values (client read-only).";
            }
            else
            {
                m_powersStatus = "Failed to load config/powers_tuning.json.";
            }
        }
        if (m_ui.Button("defaults_powers_btn", "Set Defaults", allowEdit, &m_ui.Theme().colorDanger, 165.0F))
        {
            m_powersEditing = PowersTuning{};
            ApplyPowersSettings(m_powersEditing, false);
            if (m_multiplayerMode == MultiplayerMode::Host)
            {
                SendPowersTuningToClient();
            }
            m_powersStatus = "Defaults applied. Use Save To File to persist.";
        }
        m_ui.PopLayout();

        m_ui.Label("Bear Trap (Trapper)", m_ui.Theme().colorAccent);
        m_ui.SliderInt("pw_trapper_start", "Start Carry Traps", &p.trapperStartCarryTraps, 0, 16);
        m_ui.SliderInt("pw_trapper_max", "Max Carry Traps", &p.trapperMaxCarryTraps, 1, 16);
        m_ui.SliderInt("pw_trapper_ground", "Ground Spawn Traps", &p.trapperGroundSpawnTraps, 0, 48);
        m_ui.SliderFloat("pw_trapper_set", "Set Trap Time (s)", &p.trapperSetTrapSeconds, 0.1F, 6.0F, "%.2f");
        m_ui.SliderFloat("pw_trapper_disarm", "Disarm Time (s)", &p.trapperDisarmSeconds, 0.1F, 8.0F, "%.2f");
        m_ui.SliderFloat("pw_trap_escape_base", "Escape Base Chance", &p.trapEscapeBaseChance, 0.01F, 0.9F, "%.2f");
        m_ui.SliderFloat("pw_trap_escape_step", "Escape Chance Step", &p.trapEscapeChanceStep, 0.01F, 0.8F, "%.2f");
        m_ui.SliderFloat("pw_trap_escape_max", "Escape Chance Max", &p.trapEscapeChanceMax, 0.05F, 0.98F, "%.2f");
        m_ui.SliderFloat("pw_trap_killer_stun", "Killer Stun (s)", &p.trapKillerStunSeconds, 0.1F, 8.0F, "%.2f");

        m_ui.Label("Wraith Cloak", m_ui.Theme().colorAccent);
        m_ui.SliderFloat("pw_wraith_cloak_speed", "Cloak Speed Mult", &p.wraithCloakMoveSpeedMultiplier, 1.0F, 3.0F, "%.2f");
        m_ui.SliderFloat("pw_wraith_cloak_trans", "Cloak Transition (s)", &p.wraithCloakTransitionSeconds, 0.1F, 4.0F, "%.2f");
        m_ui.SliderFloat("pw_wraith_uncloak_trans", "Uncloak Transition (s)", &p.wraithUncloakTransitionSeconds, 0.1F, 4.0F, "%.2f");
        m_ui.SliderFloat("pw_wraith_haste", "Post-Uncloak Haste (s)", &p.wraithPostUncloakHasteSeconds, 0.0F, 8.0F, "%.2f");
        m_ui.SliderFloat("pw_wraith_vault", "Cloak Vault Speed Mult", &p.wraithCloakVaultSpeedMult, 1.0F, 3.0F, "%.2f");
        m_ui.SliderFloat("pw_wraith_pallet", "Cloak Pallet Break Mult", &p.wraithCloakPalletBreakSpeedMult, 1.0F, 3.0F, "%.2f");
        m_ui.SliderFloat("pw_wraith_alpha", "Cloak Alpha (visibility)", &p.wraithCloakAlpha, 0.0F, 1.0F, "%.2f");

        m_ui.Label("Hatchet Throw (Huntress)", m_ui.Theme().colorAccent);
        m_ui.SliderInt("pw_hatchet_max", "Max Hatchets", &p.hatchetMaxCount, 1, 16);
        m_ui.SliderFloat("pw_hatchet_charge_min", "Min Charge Time (s)", &p.hatchetChargeMinSeconds, 0.0F, 1.0F, "%.2f");
        m_ui.SliderFloat("pw_hatchet_charge_max", "Max Charge Time (s)", &p.hatchetChargeMaxSeconds, 0.1F, 3.0F, "%.2f");
        m_ui.SliderFloat("pw_hatchet_speed_min", "Throw Speed Min", &p.hatchetThrowSpeedMin, 5.0F, 25.0F, "%.1f");
        m_ui.SliderFloat("pw_hatchet_speed_max", "Throw Speed Max", &p.hatchetThrowSpeedMax, 15.0F, 50.0F, "%.1f");
        m_ui.SliderFloat("pw_hatchet_gravity_min", "Gravity Min (heavy)", &p.hatchetGravityMin, 1.0F, 25.0F, "%.1f");
        m_ui.SliderFloat("pw_hatchet_gravity_max", "Gravity Max (light)", &p.hatchetGravityMax, 1.0F, 15.0F, "%.1f");
        m_ui.SliderFloat("pw_hatchet_drag", "Air Drag", &p.hatchetAirDrag, 0.9F, 1.0F, "%.3f");
        m_ui.SliderFloat("pw_hatchet_radius", "Collision Radius", &p.hatchetCollisionRadius, 0.05F, 0.5F, "%.2f");
        m_ui.SliderFloat("pw_hatchet_range", "Max Range", &p.hatchetMaxRange, 10.0F, 100.0F, "%.1f");
        m_ui.SliderFloat("pw_hatchet_locker_time", "Locker Replenish (s)", &p.hatchetLockerReplenishTime, 0.5F, 10.0F, "%.1f");
        m_ui.SliderInt("pw_hatchet_locker_count", "Locker Replenish Count", &p.hatchetLockerReplenishCount, 1, 16);

        m_ui.Label("Chainsaw Sprint (Hillbilly)", m_ui.Theme().colorAccent);
        m_ui.SliderFloat("pw_chainsaw_charge", "Charge Time (s)", &p.chainsawChargeTime, 0.5F, 5.0F, "%.2f");
        m_ui.SliderFloat("pw_chainsaw_speed", "Sprint Speed Mult", &p.chainsawSprintSpeedMultiplier, 1.5F, 4.0F, "%.2f");
        m_ui.SliderFloat("pw_chainsaw_turn_boost_window", "Turn Boost Window (s)", &p.chainsawTurnBoostWindow, 0.1F, 2.0F, "%.2f");
        m_ui.SliderFloat("pw_chainsaw_turn_boost_rate", "Turn Boost Rate (deg/s)", &p.chainsawTurnBoostRate, 30.0F, 300.0F, "%.0f");
        m_ui.SliderFloat("pw_chainsaw_turn_restricted", "Turn Restricted Rate (deg/s)", &p.chainsawTurnRestrictedRate, 10.0F, 90.0F, "%.0f");
        m_ui.SliderFloat("pw_chainsaw_collision_recovery", "Collision Recovery (s)", &p.chainsawCollisionRecoveryDuration, 0.5F, 5.0F, "%.2f");
        m_ui.SliderFloat("pw_chainsaw_hit_recovery", "Hit Recovery (s)", &p.chainsawRecoveryHitDuration, 0.1F, 2.0F, "%.2f");
        m_ui.SliderFloat("pw_chainsaw_cancel_recovery", "Cancel Recovery (s)", &p.chainsawRecoveryCancelDuration, 0.1F, 2.0F, "%.2f");
        m_ui.SliderFloat("pw_chainsaw_heat_charge", "Heat/Sec (Charging)", &p.chainsawOverheatPerSecondCharge, 5.0F, 50.0F, "%.1f");
        m_ui.SliderFloat("pw_chainsaw_heat_sprint", "Heat/Sec (Sprinting)", &p.chainsawOverheatPerSecondSprint, 5.0F, 50.0F, "%.1f");
        m_ui.SliderFloat("pw_chainsaw_heat_cooldown", "Heat Cooldown/Sec", &p.chainsawOverheatCooldownRate, 2.0F, 30.0F, "%.1f");
        m_ui.SliderFloat("pw_chainsaw_buff_threshold", "Buff Threshold (%)", &p.chainsawOverheatBuffThreshold, 50.0F, 150.0F, "%.0f");
        m_ui.SliderFloat("pw_chainsaw_charge_bonus", "Buff: Charge Bonus", &p.chainsawOverheatChargeBonus, 0.0F, 0.5F, "%.2f");
        m_ui.SliderFloat("pw_chainsaw_speed_bonus", "Buff: Speed Bonus", &p.chainsawOverheatSpeedBonus, 0.0F, 0.5F, "%.2f");
        m_ui.SliderFloat("pw_chainsaw_turn_bonus", "Buff: Turn Bonus", &p.chainsawOverheatTurnBonus, 0.0F, 0.5F, "%.2f");
        m_ui.SliderFloat("pw_chainsaw_raycast_dist", "Collision Raycast Dist", &p.chainsawCollisionRaycastDistance, 0.5F, 5.0F, "%.2f");
        m_ui.SliderFloat("pw_chainsaw_hit_radius", "Survivor Hit Radius", &p.chainsawSurvivorHitRadius, 0.5F, 3.0F, "%.2f");
        m_ui.SliderFloat("pw_chainsaw_charge_slowdown", "Charge Slowdown", &p.chainsawChargeSlowdownMultiplier, 0.0F, 1.0F, "%.2f");

        m_ui.Label("Tip: Apply for runtime changes, Save To File for persistence.", m_ui.Theme().colorTextMuted);
        if (!m_powersStatus.empty())
        {
            m_ui.Label(m_powersStatus, m_ui.Theme().colorTextMuted);
        }
    }
    else if (m_settingsTabIndex == 6)
    {
        auto& a = m_animationEditing;

        m_ui.Label("Config Actions", m_ui.Theme().colorAccent);
        m_ui.PushLayout(engine::ui::UiSystem::LayoutAxis::Horizontal, 8.0F, 0.0F);
        if (m_ui.Button("apply_anim_btn", "Apply", true, &m_ui.Theme().colorSuccess, 165.0F))
        {
            ApplyAnimationSettings(m_animationEditing);
            m_animationStatus = "Animation settings applied.";
        }
        if (m_ui.Button("save_anim_btn", "Save To File", true, nullptr, 165.0F))
        {
            m_animationApplied = m_animationEditing;
            const bool saved = SaveAnimationConfig();
            m_animationStatus = saved ? "Saved to config/animation.json." : "Failed to save animation file.";
        }
        if (m_ui.Button("load_anim_btn", "Load From File", true, nullptr, 165.0F))
        {
            if (LoadAnimationConfig())
            {
                m_animationEditing = m_animationApplied;
                m_animationStatus = "Loaded from file and applied.";
            }
            else
            {
                m_animationStatus = "Failed to load config/animation.json.";
            }
        }
        if (m_ui.Button("defaults_anim_btn", "Set Defaults", true, &m_ui.Theme().colorDanger, 165.0F))
        {
            m_animationEditing = AnimationSettings{};
            ApplyAnimationSettings(m_animationEditing);
            m_animationStatus = "Defaults applied. Use Save To File to persist.";
        }
        m_ui.PopLayout();

        m_ui.Label("State Thresholds", m_ui.Theme().colorAccent);
        m_ui.SliderFloat("anim_idle_epsilon", "Idle Epsilon (m/s)", &a.idleEpsilon, 0.01F, 1.0F, "%.2f");
        m_ui.SliderFloat("anim_run_threshold", "Run Threshold (m/s)", &a.runThreshold, 2.0F, 6.0F, "%.2f");

        m_ui.Label("Blend Times", m_ui.Theme().colorAccent);
        m_ui.SliderFloat("anim_blend_idle_walk", "Idle <-> Walk (s)", &a.blendIdleWalk, 0.05F, 0.5F, "%.2f");
        m_ui.SliderFloat("anim_blend_walk_run", "Walk <-> Run (s)", &a.blendWalkRun, 0.05F, 0.5F, "%.2f");
        m_ui.SliderFloat("anim_blend_run_idle", "Run <-> Idle (s)", &a.blendRunIdle, 0.05F, 0.5F, "%.2f");

        m_ui.Label("Playback Speed", m_ui.Theme().colorAccent);
        m_ui.SliderFloat("anim_global_scale", "Global Scale", &a.globalAnimScale, 0.1F, 3.0F, "%.2f");
        m_ui.SliderFloat("anim_walk_ref", "Walk Speed Ref (m/s)", &a.walkSpeedRef, 1.0F, 6.0F, "%.2f");
        m_ui.SliderFloat("anim_run_ref", "Run Speed Ref (m/s)", &a.runSpeedRef, 2.0F, 8.0F, "%.2f");
        m_ui.SliderFloat("anim_min_walk", "Min Walk Scale", &a.minWalkScale, 0.3F, 1.0F, "%.2f");
        m_ui.SliderFloat("anim_max_walk", "Max Walk Scale", &a.maxWalkScale, 1.0F, 2.0F, "%.2f");
        m_ui.SliderFloat("anim_min_run", "Min Run Scale", &a.minRunScale, 0.3F, 1.0F, "%.2f");
        m_ui.SliderFloat("anim_max_run", "Max Run Scale", &a.maxRunScale, 1.0F, 2.0F, "%.2f");

        m_ui.Label("Console Commands: anim_list, anim_play <clip>, anim_state auto|idle|walk|run, anim_scale <value>, anim_info", m_ui.Theme().colorTextMuted);
        if (!m_animationStatus.empty())
        {
            m_ui.Label(m_animationStatus, m_ui.Theme().colorTextMuted);
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
        if (hudState.roleName == "Survivor")
        {
            std::ostringstream rot;
            rot.setf(std::ios::fixed);
            rot << std::setprecision(1)
                << "RotDbg model=" << hudState.survivorVisualYawDeg
                << " target=" << hudState.survivorVisualTargetYawDeg
                << " look=" << hudState.survivorLookYawDeg
                << " cam=" << hudState.survivorCameraYawDeg;
            m_ui.Label(rot.str(), m_ui.Theme().colorTextMuted);

            std::ostringstream input;
            input.setf(std::ios::fixed);
            input << std::setprecision(2)
                  << "MoveInput x=" << hudState.survivorMoveInput.x
                  << " y=" << hudState.survivorMoveInput.y;
            m_ui.Label(input.str(), m_ui.Theme().colorTextMuted);

        }
        std::ostringstream ghost;
        ghost.setf(std::ios::fixed);
        ghost << std::setprecision(2)
              << "HitGhost " << (hudState.killerSurvivorNoCollisionActive ? "ON" : "OFF")
              << " t=" << hudState.killerSurvivorNoCollisionTimer
              << " overlap=" << (hudState.killerSurvivorOverlapping ? "YES" : "NO");
        m_ui.Label(ghost.str(), m_ui.Theme().colorTextMuted);
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

    if (hudState.roleName == "Killer" && hudState.killerPowerId == "wraith_cloak")
    {
        float panelHeight = 56.0F * scale;
        if (hudState.wraithCloakTransitionActive)
        {
            panelHeight = 88.0F * scale;
        }
        else if (hudState.wraithPostUncloakHasteSeconds > 0.0F)
        {
            panelHeight = 78.0F * scale;
        }
        const engine::ui::UiRect wraithPanel{
            18.0F * scale,
            static_cast<float>(m_ui.ScreenHeight()) - 132.0F * scale,
            280.0F * scale,
            panelHeight,
        };
        m_ui.BeginPanel("hud_wraith_corner", wraithPanel, true);
        if (hudState.wraithCloaked)
        {
            m_ui.Label("CLOAKED", glm::vec4{0.4F, 0.6F, 1.0F, 1.0F});
        }
        else if (hudState.wraithCloakTransitionActive)
        {
            m_ui.Label(hudState.wraithCloakAction, m_ui.Theme().colorAccent);
            m_ui.ProgressBar(
                "hud_wraith_cloak_progress",
                hudState.wraithCloakProgress01,
                std::to_string(static_cast<int>(hudState.wraithCloakProgress01 * 100.0F)) + "%"
            );
        }
        else
        {
            m_ui.Label("Uncloaked", m_ui.Theme().colorTextMuted);
        }
        if (hudState.wraithPostUncloakHasteSeconds > 0.0F)
        {
            m_ui.Label(
                "Haste: " + std::to_string(hudState.wraithPostUncloakHasteSeconds).substr(0, 3) + "s",
                m_ui.Theme().colorSuccess
            );
        }
        m_ui.EndPanel();
    }

    // Hatchet power HUD panel
    if (hudState.roleName == "Killer" && hudState.killerPowerId == "hatchet_throw")
    {
        float panelHeight = 56.0F * scale;
        if (hudState.hatchetCharging)
        {
            panelHeight = 88.0F * scale;
        }
        else if (hudState.lockerReplenishProgress > 0.0F)
        {
            panelHeight = 88.0F * scale;
        }
        const engine::ui::UiRect hatchetPanel{
            18.0F * scale,
            static_cast<float>(m_ui.ScreenHeight()) - 132.0F * scale,
            280.0F * scale,
            panelHeight,
        };
        m_ui.BeginPanel("hud_hatchet_corner", hatchetPanel, true);
        m_ui.Label(
            "Hatchets: " + std::to_string(hudState.hatchetCount) + "/" + std::to_string(hudState.hatchetMaxCount),
            hudState.hatchetCount > 0 ? m_ui.Theme().colorText : m_ui.Theme().colorDanger
        );

        if (hudState.hatchetCharging)
        {
            m_ui.Label("Charging...", m_ui.Theme().colorAccent);
            m_ui.ProgressBar(
                "hud_hatchet_charge",
                hudState.hatchetCharge01,
                std::to_string(static_cast<int>(hudState.hatchetCharge01 * 100.0F)) + "%"
            );
        }

        if (hudState.lockerReplenishProgress > 0.0F)
        {
            m_ui.Label("Replenishing...", m_ui.Theme().colorSuccess);
            m_ui.ProgressBar(
                "hud_hatchet_replenish",
                hudState.lockerReplenishProgress,
                std::to_string(static_cast<int>(hudState.lockerReplenishProgress * 100.0F)) + "%"
            );
        }

        m_ui.EndPanel();
    }

    // Chainsaw sprint power HUD panel
    if (hudState.roleName == "Killer" && hudState.killerPowerId == "chainsaw_sprint")
    {
        float panelHeight = 56.0F * scale;
        if (hudState.chainsawState == "Charging")
        {
            panelHeight = 120.0F * scale;
        }
        else if (hudState.chainsawState == "Sprinting")
        {
            panelHeight = 140.0F * scale;
        }
        else if (hudState.chainsawState == "Recovery")
        {
            panelHeight = 88.0F * scale;
        }

        const engine::ui::UiRect chainsawPanel{
            18.0F * scale,
            static_cast<float>(m_ui.ScreenHeight()) - 132.0F * scale - panelHeight + 56.0F * scale,
            280.0F * scale,
            panelHeight,
        };
        m_ui.BeginPanel("hud_chainsaw_corner", chainsawPanel, true);

        // State label with color coding
        glm::vec4 stateColor = m_ui.Theme().colorText;
        if (hudState.chainsawOverheatBuffed)
        {
            stateColor = glm::vec4{1.0F, 0.5F, 0.1F, 1.0F}; // Fiery orange for buffed
        }
        else if (hudState.chainsawState == "Charging")
        {
            stateColor = m_ui.Theme().colorAccent;
        }
        else if (hudState.chainsawState == "Sprinting")
        {
            stateColor = glm::vec4{0.9F, 0.2F, 0.2F, 1.0F}; // Red for sprinting
        }
        else if (hudState.chainsawState == "Recovery")
        {
            stateColor = glm::vec4{1.0F, 0.6F, 0.1F, 1.0F}; // Orange for recovery
        }

        std::string stateLabel = hudState.chainsawState;
        if (hudState.chainsawOverheatBuffed && hudState.chainsawState == "Idle")
        {
            stateLabel = "Idle (BUFFED)";
        }
        m_ui.Label(stateLabel, stateColor);

        // Charge bar during charging
        if (hudState.chainsawState == "Charging")
        {
            m_ui.Label("Charging...", m_ui.Theme().colorAccent);
            m_ui.ProgressBar(
                "hud_chainsaw_charge",
                hudState.chainsawCharge01,
                std::to_string(static_cast<int>(hudState.chainsawCharge01 * 100.0F)) + "%"
            );
            if (hudState.chainsawOverheatBuffed)
            {
                m_ui.Label("CHARGE BONUS ACTIVE", glm::vec4{1.0F, 0.6F, 0.1F, 1.0F});
            }
        }

        // Sprint info
        if (hudState.chainsawState == "Sprinting")
        {
            m_ui.Label("SPRINTING!", glm::vec4{0.9F, 0.2F, 0.2F, 1.0F});
            m_ui.Label("Speed: " + std::to_string(static_cast<int>(hudState.chainsawCurrentSpeed)) + " m/s", m_ui.Theme().colorText);

            // Turn rate indicator
            if (hudState.chainsawTurnBoostActive)
            {
                m_ui.Label("HIGH TURN RATE", glm::vec4{0.2F, 1.0F, 0.3F, 1.0F});
            }
            else
            {
                m_ui.Label("LOW TURN RATE", glm::vec4{0.8F, 0.5F, 0.2F, 1.0F});
            }
            m_ui.Label("Turn: " + std::to_string(static_cast<int>(hudState.chainsawTurnRate)) + " deg/s", m_ui.Theme().colorTextMuted);

            if (hudState.chainsawOverheatBuffed)
            {
                m_ui.Label("SPEED BONUS ACTIVE!", glm::vec4{1.0F, 0.6F, 0.1F, 1.0F});
            }
        }

        // Recovery countdown
        if (hudState.chainsawState == "Recovery")
        {
            const float remaining = hudState.chainsawRecoveryDuration - hudState.chainsawRecoveryTimer;
            m_ui.Label("Recovering: " + std::to_string(static_cast<int>(remaining * 10.0F) / 10.0F) + "s", m_ui.Theme().colorTextMuted);
            const float progress = hudState.chainsawRecoveryTimer / std::max(0.01F, hudState.chainsawRecoveryDuration);
            m_ui.ProgressBar("hud_chainsaw_recovery", progress, "");
        }

        // Overheat bar (always visible)
        glm::vec4 overheatColor = m_ui.Theme().colorTextMuted;
        if (hudState.chainsawOverheatBuffed)
        {
            overheatColor = glm::vec4{1.0F, 0.3F, 0.1F, 1.0F}; // Red-orange highlight
        }
        else if (hudState.chainsawOverheat01 > 0.7F)
        {
            overheatColor = glm::vec4{1.0F, 0.6F, 0.1F, 1.0F}; // Warning orange
        }
        m_ui.Label("Heat: " + std::to_string(static_cast<int>(hudState.chainsawOverheat01 * 100.0F)) + "%", overheatColor);
        m_ui.ProgressBar("hud_chainsaw_heat", hudState.chainsawOverheat01, "");

        m_ui.EndPanel();
    }

    // Nurse blink power HUD panel
    if (hudState.roleName == "Killer" && hudState.killerPowerId == "nurse_blink")
    {
        float panelHeight = 100.0F * scale;
        if (hudState.blinkState == "Charging")
        {
            panelHeight = 180.0F * scale;
        }
        else if (hudState.blinkState == "ChainWindow")
        {
            panelHeight = 160.0F * scale;
        }
        else if (hudState.blinkState == "Fatigue")
        {
            panelHeight = 130.0F * scale;
        }

        const engine::ui::UiRect blinkPanel{
            20.0F * scale,
            200.0F * scale,
            240.0F * scale,
            panelHeight,
        };
        m_ui.BeginPanel("hud_blink_power", blinkPanel, true);

        // Title
        glm::vec4 stateColor = m_ui.Theme().colorText;
        if (hudState.blinkState == "Charging")
        {
            stateColor = glm::vec4{0.2F, 0.8F, 1.0F, 1.0F}; // Cyan
        }
        else if (hudState.blinkState == "Traveling")
        {
            stateColor = glm::vec4{0.2F, 1.0F, 0.4F, 1.0F}; // Green
        }
        else if (hudState.blinkState == "ChainWindow")
        {
            stateColor = glm::vec4{1.0F, 0.8F, 0.2F, 1.0F}; // Yellow
        }
        else if (hudState.blinkState == "Fatigue")
        {
            stateColor = glm::vec4{0.6F, 0.3F, 0.3F, 1.0F}; // Dark red
        }

        m_ui.Label("Nurse Blink", m_ui.Theme().colorAccent);
        m_ui.Label(hudState.blinkState, stateColor);

        // Charge indicators (dots)
        std::string chargesText = "Charges: ";
        for (int i = 0; i < hudState.blinkMaxCharges; ++i)
        {
            if (i < hudState.blinkCharges)
            {
                chargesText += "●";
            }
            else
            {
                chargesText += "○";
            }
        }
        m_ui.Label(chargesText, m_ui.Theme().colorText);

        // Charge progress bar (while charging)
        if (hudState.blinkState == "Charging")
        {
            m_ui.Label("Distance: " + std::to_string(static_cast<int>(hudState.blinkDistanceMeters)) + "m", m_ui.Theme().colorText);
            m_ui.ProgressBar(
                "hud_blink_charge",
                hudState.blinkCharge01,
                std::to_string(static_cast<int>(hudState.blinkCharge01 * 100.0F)) + "%"
            );
            m_ui.Label("Release to blink!", glm::vec4{0.2F, 1.0F, 0.4F, 1.0F});
        }

        // Chain window progress
        if (hudState.blinkState == "ChainWindow")
        {
            const float remaining = (1.0F - hudState.blinkChainWindow01) * 1.5F; // chain window duration
            m_ui.Label("Chain window: " + std::to_string(static_cast<int>(remaining * 10.0F) / 10.0F) + "s", m_ui.Theme().colorText);
            m_ui.ProgressBar("hud_blink_chain", hudState.blinkChainWindow01, "");
            m_ui.Label("RMB: Chain | LMB: Attack", m_ui.Theme().colorTextMuted);
        }

        // Fatigue progress
        if (hudState.blinkState == "Fatigue")
        {
            const float remaining = hudState.blinkFatigueDuration * (1.0F - hudState.blinkFatigue01);
            m_ui.Label("Fatigue: " + std::to_string(static_cast<int>(remaining * 10.0F) / 10.0F) + "s", m_ui.Theme().colorTextMuted);
            m_ui.ProgressBar("hud_blink_fatigue", hudState.blinkFatigue01, "");
        }

        // Charge regeneration progress (when not at max and not charging)
        if (hudState.blinkState == "Idle" && hudState.blinkCharges < hudState.blinkMaxCharges)
        {
            m_ui.ProgressBar("hud_blink_regen", hudState.blinkChargeRegen01, "Regenerating...");
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

    // Status Effects Panel (right side of screen)
    {
        const auto& effects = (hudState.roleName == "Killer")
            ? hudState.killerStatusEffects
            : hudState.survivorStatusEffects;

        if (!effects.empty())
        {
            const float panelX = static_cast<float>(m_ui.ScreenWidth()) - 190.0F * scale;
            const float panelWidth = 180.0F * scale;
            const float badgeHeight = 36.0F * scale;
            float panelY = static_cast<float>(m_ui.ScreenHeight()) * 0.25F;
            const float panelHeight = badgeHeight * static_cast<float>(effects.size()) + 16.0F * scale;

            const engine::ui::UiRect statusPanel{panelX, panelY, panelWidth, panelHeight};
            m_ui.BeginPanel("hud_status_effects", statusPanel, true);

            for (const auto& effect : effects)
            {
                // Build label: "EffectName 15s" or "EffectName [inf]"
                std::string label = effect.displayName;
                if (!effect.isInfinite && effect.remainingSeconds > 0.0F)
                {
                    label += " " + std::to_string(static_cast<int>(effect.remainingSeconds)) + "s";
                }
                else if (effect.isInfinite)
                {
                    label += " [inf]";
                }

                // Color based on effect type
                glm::vec4 effectColor = m_ui.Theme().colorText;
                if (effect.typeId == "exposed") effectColor = glm::vec4{0.9F, 0.25F, 0.2F, 1.0F};
                else if (effect.typeId == "undetectable") effectColor = glm::vec4{0.25F, 0.45F, 0.75F, 1.0F};
                else if (effect.typeId == "haste") effectColor = glm::vec4{0.25F, 0.75F, 0.4F, 1.0F};
                else if (effect.typeId == "hindered") effectColor = glm::vec4{0.65F, 0.4F, 0.25F, 1.0F};
                else if (effect.typeId == "exhausted") effectColor = glm::vec4{0.75F, 0.65F, 0.25F, 1.0F};
                else if (effect.typeId == "bloodlust") effectColor = glm::vec4{0.75F, 0.25F, 0.25F, 1.0F};

                m_ui.Label(label, effectColor);

                // Progress bar for timed effects
                if (!effect.isInfinite && effect.progress01 > 0.0F)
                {
                    m_ui.ProgressBar("status_pb_" + effect.typeId, effect.progress01, "");
                }
            }

            m_ui.EndPanel();
        }
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
