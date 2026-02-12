#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>
#include <array>

#include <glm/vec3.hpp>
#include <glm/mat4x4.hpp>

namespace engine::ui
{
class UiSystem;
}

namespace engine::render
{
class Renderer;
}

namespace engine::platform
{
class Input;
}

namespace engine::scene
{
class World;
}

namespace game::ui
{

struct LobbyPlayer
{
    std::uint32_t netId = 0;
    std::string name = "Player";
    std::string selectedRole = "survivor";
    std::string characterId;
    bool isReady = false;
    bool isHost = false;
    bool isConnected = false;
    glm::vec3 worldPosition{0.0F, 0.0F, 0.0F};
    float rotation = 0.0F;
};

struct LobbySceneState
{
    std::vector<LobbyPlayer> players;
    int localPlayerIndex = 0;
    bool isHost = false;
    std::string selectedMap = "main";
    std::array<std::string, 4> selectedPerks = {"", "", "", ""};
    float countdownTimer = -1.0F;
    bool countdownActive = false;
    bool matchStarting = false;
};

class LobbyScene
{
public:
    using StartMatchCallback = std::function<void(const std::string& map, const std::string& role, const std::array<std::string, 4>& perks)>;
    using ReadyChangedCallback = std::function<void(bool ready)>;
    using RoleChangedCallback = std::function<void(const std::string& role)>;
    using CharacterChangedCallback = std::function<void(const std::string& characterId)>;

    LobbyScene();
    ~LobbyScene();

    bool Initialize(engine::ui::UiSystem* uiSystem, engine::render::Renderer* renderer, engine::platform::Input* input);
    void Shutdown();

    void Update(float deltaSeconds);
    void Render();
    void HandleInput();
    
    void EnterLobby();
    void ExitLobby();
    
    void SetPlayers(const std::vector<LobbyPlayer>& players);
    void SetLocalPlayerReady(bool ready);
    void SetLocalPlayerRole(const std::string& role);
    void SetLocalPlayerPerks(const std::array<std::string, 4>& perks);
    void SetCountdown(float seconds);
    void CancelCountdown();
    
    void SetStartMatchCallback(StartMatchCallback callback) { m_onStartMatch = std::move(callback); }
    void SetReadyChangedCallback(ReadyChangedCallback callback) { m_onReadyChanged = std::move(callback); }
    void SetRoleChangedCallback(RoleChangedCallback callback) { m_onRoleChanged = std::move(callback); }
    void SetCharacterChangedCallback(CharacterChangedCallback callback) { m_onCharacterChanged = std::move(callback); }

    [[nodiscard]] const LobbySceneState& GetState() const { return m_state; }
    [[nodiscard]] bool IsInLobby() const { return m_isInLobby; }

private:
    void UpdateCamera(float deltaSeconds);
    void UpdateFireParticles(float deltaSeconds);
    void UpdatePlayerPositions();
    
    void Render3DScene();
    void RenderLobbyUI();
    void RenderPlayerSlots();
    void RenderPlayerDetails(int playerIndex);
    void RenderReadyButton();
    void RenderCountdown();
    void RenderMatchSettings();
    
    void DrawFireParticle(const glm::vec3& position, float size, float alpha);
    void DrawGroundPlane();
    void DrawCampfire();
    void DrawPlayerModels();
    
    void DrawUIPanel(float x, float y, float width, float height);
    void DrawPlayerSlot(float x, float y, float width, float height, int playerIndex);
    void DrawRoleSelector(float x, float y);
    void DrawPerkSlots(float x, float y);

    engine::ui::UiSystem* m_ui = nullptr;
    engine::render::Renderer* m_renderer = nullptr;
    engine::platform::Input* m_input = nullptr;
    LobbySceneState m_state;
    bool m_isInLobby = false;
    
    float m_cameraAngle = 0.0F;
    float m_cameraHeight = 2.5F;
    float m_cameraDistance = 8.0F;
    float m_cameraTargetHeight = 1.0F;
    glm::vec3 m_cameraTarget{0.0F, 0.0F, 0.0F};
    glm::mat4 m_viewMatrix{1.0F};
    glm::mat4 m_projectionMatrix{1.0F};
    
    float m_fireTime = 0.0F;
    struct FireParticle
    {
        glm::vec3 position{0.0F, 0.0F, 0.0F};
        glm::vec3 velocity{0.0F, 0.0F, 0.0F};
        float life = 0.0F;
        float maxLife = 1.0F;
        float size = 0.1F;
    };
    std::vector<FireParticle> m_fireParticles;
    
    StartMatchCallback m_onStartMatch;
    ReadyChangedCallback m_onReadyChanged;
    RoleChangedCallback m_onRoleChanged;
    CharacterChangedCallback m_onCharacterChanged;
    
    static constexpr int kMaxPlayers = 4;
    static constexpr float kFireRadius = 3.0F;
};

}
