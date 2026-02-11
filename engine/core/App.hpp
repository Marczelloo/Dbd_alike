#pragma once

#include <cstdint>
#include <array>
#include <fstream>
#include <random>
#include <string>
#include <vector>

#include <glm/vec2.hpp>

#include "engine/core/EventBus.hpp"
#include "engine/net/LanDiscovery.hpp"
#include "engine/net/NetworkSession.hpp"
#include "engine/core/Time.hpp"
#include "engine/platform/ActionBindings.hpp"
#include "engine/platform/Input.hpp"
#include "engine/platform/Window.hpp"
#include "engine/render/Renderer.hpp"
#include "engine/ui/UiSystem.hpp"
#include "game/editor/LevelEditor.hpp"
#include "game/gameplay/GameplaySystems.hpp"
#include "ui/DeveloperConsole.hpp"
#include "ui/DeveloperToolbar.hpp"

namespace engine::core
{
class App
{
public:
    bool Run();

public:
    enum class DisplayModeSetting
    {
        Windowed = 0,
        Fullscreen = 1,
        Borderless = 2
    };

    struct ControlsSettings
    {
        int assetVersion = 1;
        float survivorSensitivity = 0.0022F;
        float killerSensitivity = 0.0022F;
        bool invertY = false;
    };

    struct GraphicsSettings
    {
        int assetVersion = 1;
        DisplayModeSetting displayMode = DisplayModeSetting::Windowed;
        int width = 1600;
        int height = 900;
        bool vsync = true;
        int fpsLimit = 144;
        render::RenderMode renderMode = render::RenderMode::Wireframe;
        int shadowQuality = 0;
        float shadowDistance = 40.0F;
        int antiAliasing = 0;
        int textureQuality = 0;
        bool fogEnabled = false;
    };

private:
    enum class AppMode
    {
        MainMenu,
        Editor,
        InGame,
        Loading
    };

    enum class MultiplayerMode
    {
        Solo,
        Host,
        Client
    };

    enum class NetworkState
    {
        Offline,
        HostStarting,
        HostListening,
        ClientConnecting,
        ClientHandshaking,
        Connected,
        Disconnecting,
        Error
    };

    enum class HudDragTarget
    {
        None,
        Movement,
        Stats,
        Controls
    };

    struct NetRoleInputPacket
    {
        std::int8_t moveX = 0;
        std::int8_t moveY = 0;
        float lookX = 0.0F;
        float lookY = 0.0F;
        std::uint16_t buttons = 0;
    };

    struct NetRoleChangeRequestPacket
    {
        std::uint8_t requestedRole = 0;
    };

    struct PlayerBinding
    {
        std::uint32_t netId = 0;
        std::string name = "Player";
        bool isHost = false;
        bool connected = false;
        std::string selectedRole = "survivor";
        std::string controlledRole = "survivor";
        double lastInputSeconds = 0.0;
        double lastSnapshotSeconds = 0.0;
    };

    void ResetToMainMenu();
    void StartSoloSession(const std::string& mapName, const std::string& roleName);
    bool StartHostSession(const std::string& mapName, const std::string& roleName, std::uint16_t port);
    bool StartJoinSession(const std::string& ip, std::uint16_t port, const std::string& preferredRole);

    void PollNetwork();
    void HandleNetworkPacket(const std::vector<std::uint8_t>& payload);
    void SendClientInput(const engine::platform::Input& input, bool controlsEnabled);
    void SendHostSnapshot();

    static bool SerializeRoleInput(const NetRoleInputPacket& packet, std::vector<std::uint8_t>& outBuffer);
    static bool DeserializeRoleInput(const std::vector<std::uint8_t>& buffer, NetRoleInputPacket& outPacket);
    bool SerializeSnapshot(const game::gameplay::GameplaySystems::Snapshot& snapshot, std::vector<std::uint8_t>& outBuffer) const;
    bool DeserializeSnapshot(const std::vector<std::uint8_t>& buffer, game::gameplay::GameplaySystems::Snapshot& outSnapshot) const;
    bool SerializeGameplayTuning(const game::gameplay::GameplaySystems::GameplayTuning& tuning, std::vector<std::uint8_t>& outBuffer) const;
    bool DeserializeGameplayTuning(const std::vector<std::uint8_t>& buffer, game::gameplay::GameplaySystems::GameplayTuning& outTuning) const;
    static bool SerializeAssignRole(std::uint8_t roleByte, game::gameplay::GameplaySystems::MapType mapType, unsigned int seed, std::vector<std::uint8_t>& outBuffer);
    bool DeserializeAssignRole(const std::vector<std::uint8_t>& buffer, std::uint8_t& outRole, game::gameplay::GameplaySystems::MapType& outMapType, unsigned int& outSeed) const;
    bool SerializeHello(const std::string& requestedRole, std::vector<std::uint8_t>& outBuffer) const;
    bool DeserializeHello(
        const std::vector<std::uint8_t>& buffer,
        std::string& outRequestedRole,
        std::string& outMapName,
        int& outProtocolVersion,
        std::string& outBuildId
    ) const;
    bool SerializeReject(const std::string& reason, std::vector<std::uint8_t>& outBuffer) const;
    bool DeserializeReject(const std::vector<std::uint8_t>& buffer, std::string& outReason) const;
    static bool SerializeRoleChangeRequest(const NetRoleChangeRequestPacket& packet, std::vector<std::uint8_t>& outBuffer);
    static bool DeserializeRoleChangeRequest(const std::vector<std::uint8_t>& buffer, NetRoleChangeRequestPacket& outPacket);

    void TickLanDiscovery(double nowSeconds);
    void TransitionNetworkState(NetworkState state, const std::string& reason, bool isError = false);
    void AppendNetworkLog(const std::string& text);
    void OpenNetworkLogFile();
    void CloseNetworkLogFile();
    void BuildLocalIpv4List();
    [[nodiscard]] std::string PrimaryLocalIp() const;
    [[nodiscard]] std::string BuildHostHelpText() const;
    [[nodiscard]] std::string NetworkStateToText(NetworkState state) const;
    [[nodiscard]] std::string NetStatusDump() const;
    [[nodiscard]] std::string NetConfigDump() const;
    [[nodiscard]] std::string PlayerDump() const;
    [[nodiscard]] engine::scene::Role RoleFromString(const std::string& roleName) const;
    [[nodiscard]] std::string RoleToString(engine::scene::Role role) const;
    [[nodiscard]] std::string NormalizeRoleName(const std::string& roleName) const;
    [[nodiscard]] std::string OppositeRoleName(const std::string& roleName) const;
    void InitializePlayerBindings();
    void ApplyRoleMapping(const std::string& localRole, const std::string& remoteRole, const std::string& reason, bool respawnLocal, bool respawnRemote);
    void RequestRoleChange(const std::string& requestedRole, bool fromRemotePeer);
    void SendAssignRoleToClient(const std::string& remoteRole);
    bool SendRoleChangeRequestToHost(const std::string& requestedRole);

    void DrawMainMenuUi(bool* shouldQuit);
    void DrawPauseMenuUi(bool* closePauseMenu, bool* backToMenu, bool* shouldQuit);
    void DrawSettingsUi(bool* closeSettings);
    void DrawMainMenuUiCustom(bool* shouldQuit);
    void DrawPauseMenuUiCustom(bool* closePauseMenu, bool* backToMenu, bool* shouldQuit);
    void DrawSettingsUiCustom(bool* closeSettings);
    void DrawInGameHudCustom(const game::gameplay::HudState& hudState, float fps, double nowSeconds);
    void DrawUiTestPanel();
    void DrawLoadingScreenTestPanel();
    void DrawFullLoadingScreen(float progress01, const std::string& tip, const std::string& stepText);
    void DrawNetworkStatusUi(double nowSeconds);
    void DrawNetworkOverlayUi(double nowSeconds);
    void DrawPlayersDebugUi(double nowSeconds);
    static std::string RoleNameFromIndex(int index);
    static std::string MapNameFromIndex(int index);
    [[nodiscard]] bool LoadControlsConfig();
    [[nodiscard]] bool SaveControlsConfig() const;
    [[nodiscard]] bool LoadGraphicsConfig();
    [[nodiscard]] bool SaveGraphicsConfig() const;
    [[nodiscard]] bool LoadGameplayConfig();
    [[nodiscard]] bool SaveGameplayConfig() const;
    void ApplyControlsSettings();
    void ApplyGraphicsSettings(const GraphicsSettings& settings, bool startAutoConfirm);
    void ApplyGameplaySettings(const game::gameplay::GameplaySystems::GameplayTuning& tuning, bool fromServer);
    [[nodiscard]] std::optional<int> CapturePressedBindCode() const;
    [[nodiscard]] std::vector<std::pair<int, int>> AvailableResolutions() const;
    [[nodiscard]] bool LoadHudLayoutConfig();
    void SendGameplayTuningToClient();
    void ApplyMapEnvironment(const std::string& mapName);

    platform::WindowSettings m_windowSettings{};

    platform::Window m_window;
    platform::Input m_input;
    platform::ActionBindings m_actionBindings;
    render::Renderer m_renderer;
    ui::UiSystem m_ui;

    EventBus m_eventBus;
    Time m_time{1.0 / 60.0};

    game::gameplay::GameplaySystems m_gameplay;
    game::editor::LevelEditor m_levelEditor;
    ::ui::DeveloperConsole m_console;
    ::ui::DeveloperToolbar m_devToolbar;
    net::NetworkSession m_network;
    net::LanDiscovery m_lanDiscovery;

    bool m_vsyncEnabled = true;
    int m_fpsLimit = 144;
    int m_fixedTickHz = 60;
    bool m_showDebugOverlay = true;
    bool m_showNetworkOverlay = false;
    bool m_showPlayersWindow = false;
    bool m_showMovementWindow = false;
    bool m_showStatsWindow = false;
    bool m_showControlsWindow = true;
    bool m_showLanDebug = false;
    std::uint16_t m_defaultGamePort = 7777;
    std::uint16_t m_lanDiscoveryPort = 7778;
    int m_clientInterpolationBufferMs = 350;

    AppMode m_appMode = AppMode::MainMenu;
    MultiplayerMode m_multiplayerMode = MultiplayerMode::Solo;
    NetworkState m_networkState = NetworkState::Offline;
    bool m_pauseMenuOpen = false;
    bool m_settingsMenuOpen = false;
    bool m_settingsOpenedFromPause = false;
    int m_settingsTabIndex = 0;
    std::array<float, 3> m_settingsTabScroll{0.0F, 0.0F, 0.0F};
    bool m_useLegacyImGuiMenus = false;
    bool m_showUiTestPanel = false;
    bool m_showLoadingScreenTestPanel = false;

    struct HudLayoutSettings
    {
        int assetVersion = 1;
        float hudScale = 1.0F;
        glm::vec2 topLeftOffset{18.0F, 18.0F};
        glm::vec2 topRightOffset{18.0F, 18.0F};
        glm::vec2 bottomCenterOffset{0.0F, 110.0F};
        glm::vec2 messageOffset{0.0F, 72.0F};
    } m_hudLayout{};

    HudDragTarget m_hudDragTarget = HudDragTarget::None;
    glm::vec2 m_hudDragOffset{0.0F, 0.0F};
    glm::vec2 m_hudMovementPos{-1.0F, -1.0F};
    glm::vec2 m_hudStatsPos{-1.0F, -1.0F};
    glm::vec2 m_hudControlsPos{-1.0F, -1.0F};
    glm::vec2 m_hudMovementSize{-1.0F, -1.0F};
    glm::vec2 m_hudStatsSize{-1.0F, -1.0F};
    glm::vec2 m_hudControlsSize{-1.0F, -1.0F};
    bool m_hudResizing = false;
    HudDragTarget m_hudResizeTarget = HudDragTarget::None;

    bool m_connectingLoadingActive = false;
    bool m_showConnectingLoading = true;
    double m_connectingLoadingStart = 0.0;

    ControlsSettings m_controlsSettings{};
    GraphicsSettings m_graphicsApplied{};
    GraphicsSettings m_graphicsEditing{};
    game::gameplay::GameplaySystems::GameplayTuning m_gameplayApplied{};
    game::gameplay::GameplaySystems::GameplayTuning m_gameplayEditing{};
    bool m_serverGameplayValues = false;
    bool m_graphicsAutoConfirmPending = false;
    double m_graphicsAutoConfirmDeadline = 0.0;
    GraphicsSettings m_graphicsRollback{};

    bool m_rebindWaiting = false;
    engine::platform::InputAction m_rebindAction = engine::platform::InputAction::MoveForward;
    int m_rebindSlot = 0;
    bool m_rebindConflictPopup = false;
    engine::platform::InputAction m_rebindConflictAction = engine::platform::InputAction::MoveForward;
    int m_rebindConflictSlot = 0;
    int m_rebindCapturedCode = platform::ActionBindings::kUnbound;
    std::string m_controlsStatus;
    std::string m_graphicsStatus;
    std::string m_gameplayStatus;

    int m_menuRoleIndex = 0;
    int m_menuMapIndex = 0;
    int m_menuSavedMapIndex = -1;
    int m_menuPort = 7777;
    std::string m_menuJoinIp = "127.0.0.1";
    std::string m_menuNetStatus;
    std::string m_lastNetworkError;
    std::string m_connectedEndpoint;
    std::string m_preferredJoinRole = "survivor";
    std::string m_joinTargetIp = "127.0.0.1";
    std::uint16_t m_joinTargetPort = 7777;
    double m_joinStartSeconds = 0.0;
    double m_statusToastUntilSeconds = 0.0;
    std::string m_statusToastMessage;

    std::string m_sessionRoleName = "survivor";
    std::string m_sessionMapName = "main";
    game::gameplay::GameplaySystems::MapType m_sessionMapType = game::gameplay::GameplaySystems::MapType::Main;
    unsigned int m_sessionSeed = std::random_device{}();

    std::string m_remoteRoleName = "killer";
    std::string m_pendingRemoteRoleRequest = "survivor";
    PlayerBinding m_localPlayer{};
    PlayerBinding m_remotePlayer{};
    int m_playersDebugSpawnSelectionLocal = 0;
    int m_playersDebugSpawnSelectionRemote = 0;

    bool m_uiTestCheckbox = true;
    float m_uiTestSliderF = 0.35F;
    int m_uiTestSliderI = 7;
    int m_uiTestDropdown = 0;
    std::string m_uiTestInput = "sample";
    std::string m_uiTestInputA = "left";
    std::string m_uiTestInputB = "right";
    float m_uiTestProgress = 0.35F;
    bool m_uiTestCaptureMode = false;
    std::string m_uiTestCaptured;

    float m_loadingTestProgress = 0.0F;
    bool m_loadingTestAutoAdvance = false;
    float m_loadingTestSpeed = 0.5F;
    int m_loadingTestSteps = 5;
    int m_loadingTestCurrentStep = 0;
    bool m_loadingTestShowTips = true;
    int m_loadingTestSelectedTip = 0;
    bool m_loadingTestShowFull = false;
    std::vector<std::string> m_loadingTestTips{
        "Survivors: Work together to repair 5 generators and escape.",
        "Killer: Hunt down and sacrifice all survivors before they escape.",
        "Pallets: Drop pallets to block the killer's path and create distance.",
        "Windows: Fast vault through windows to break line of sight.",
        "Generators: Stay near generators to earn repair progress bonus.",
        "Skill Checks: Press SPACE when the needle is in the green zone.",
        "Terror Radius: The heartbeat indicates the killer is nearby.",
        "Chase: Run in circles around loops to waste the killer's time."
    };

    std::vector<std::string> m_localIpv4Addresses;
    double m_lastSnapshotReceivedSeconds = 0.0;
    double m_lastInputSentSeconds = 0.0;
    double m_lastSnapshotSentSeconds = 0.0;
    std::vector<std::string> m_pendingDroppedFiles;

    std::ofstream m_networkLogFile;
};
} // namespace engine::core
