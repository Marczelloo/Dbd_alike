#include "LobbyScene.hpp"

#include <cmath>
#include <algorithm>
#include <random>

#include "engine/ui/UiSystem.hpp"
#include "engine/render/Renderer.hpp"
#include "engine/platform/Input.hpp"

namespace game::ui
{

LobbyScene::LobbyScene()
{
    m_fireParticles.reserve(50);
}

LobbyScene::~LobbyScene()
{
    Shutdown();
}

bool LobbyScene::Initialize(engine::ui::UiSystem* uiSystem, engine::render::Renderer* renderer, engine::platform::Input* input)
{
    if (!uiSystem || !renderer || !input)
    {
        return false;
    }
    
    m_ui = uiSystem;
    m_renderer = renderer;
    m_input = input;
    
    m_cameraAngle = 0.0F;
    m_cameraHeight = 2.5F;
    m_cameraDistance = 8.0F;
    m_fireTime = 0.0F;
    m_isInLobby = false;
    
    m_fireParticles.clear();
    
    return true;
}

void LobbyScene::Shutdown()
{
    m_ui = nullptr;
    m_renderer = nullptr;
    m_isInLobby = false;
    m_fireParticles.clear();
    m_state.players.clear();
}

void LobbyScene::Update(float deltaSeconds)
{
    if (!m_isInLobby)
    {
        return;
    }
    
    UpdateCamera(deltaSeconds);
    UpdateFireParticles(deltaSeconds);
    
    if (m_state.countdownActive && m_state.countdownTimer > 0.0F)
    {
        m_state.countdownTimer -= deltaSeconds;
        
        if (m_state.countdownTimer <= 0.0F)
        {
            m_state.countdownTimer = 0.0F;
            m_state.matchStarting = true;
            
            if (m_onStartMatch)
            {
                auto& localPlayer = m_state.players[m_state.localPlayerIndex];
                m_onStartMatch(m_state.selectedMap, localPlayer.selectedRole, m_state.selectedPerks);
            }
        }
    }
}

void LobbyScene::Render()
{
    if (!m_isInLobby || !m_ui)
    {
        return;
    }
    
    Render3DScene();
    RenderLobbyUI();
}

void LobbyScene::HandleInput()
{
    if (!m_ui || !m_input || !m_isInLobby) return;
    
    const float scale = m_ui->Scale();
    const int screenWidth = m_ui->ScreenWidth();
    const int screenHeight = m_ui->ScreenHeight();
    const auto mousePos = m_input->MousePosition();
    
    auto isMouseOver = [&](float x, float y, float w, float h) -> bool {
        return mousePos.x >= x && mousePos.x <= x + w && mousePos.y >= y && mousePos.y <= y + h;
    };
    
    const float buttonWidth = 200.0F * scale;
    const float buttonHeight = 50.0F * scale;
    const float buttonX = (screenWidth - buttonWidth) / 2.0F;
    const float buttonY = screenHeight - buttonHeight - 30.0F * scale;
    
    if (isMouseOver(buttonX, buttonY, buttonWidth, buttonHeight) && m_input->IsMousePressed(0))
    {
        const bool isCurrentlyReady = (m_state.localPlayerIndex >= 0 && 
                                       m_state.localPlayerIndex < static_cast<int>(m_state.players.size()) &&
                                       m_state.players[m_state.localPlayerIndex].isReady);
        SetLocalPlayerReady(!isCurrentlyReady);
    }
    
    if (m_state.localPlayerIndex >= 0 && m_state.localPlayerIndex < static_cast<int>(m_state.players.size()))
    {
        const float panelWidth = 350.0F * scale;
        const float panelHeight = 300.0F * scale;
        const float panelX = screenWidth - panelWidth - 20.0F * scale;
        const float panelY = screenHeight - panelHeight - 100.0F * scale;
        const float roleY = panelY + 60.0F * scale;
        
        const float roleButtonWidth = 150.0F * scale;
        const float roleButtonHeight = 35.0F * scale;
        
        if (isMouseOver(panelX + 20.0F * scale, roleY, roleButtonWidth, roleButtonHeight) && m_input->IsMousePressed(0))
        {
            SetLocalPlayerRole("survivor");
        }
        
        if (isMouseOver(panelX + 20.0F * scale + roleButtonWidth + 10.0F * scale, roleY, roleButtonWidth, roleButtonHeight) && m_input->IsMousePressed(0))
        {
            SetLocalPlayerRole("killer");
        }
    }
    
    if (m_state.isHost && !m_state.countdownActive)
    {
        bool allReady = true;
        for (const auto& player : m_state.players)
        {
            if (player.isConnected && !player.isReady)
            {
                allReady = false;
                break;
            }
        }
        
        if (allReady && !m_state.players.empty())
        {
            SetCountdown(3.0F);
        }
    }
}

void LobbyScene::EnterLobby()
{
    m_isInLobby = true;
    m_state.countdownActive = false;
    m_state.countdownTimer = -1.0F;
    m_state.matchStarting = false;
    m_cameraAngle = 0.0F;
    m_fireTime = 0.0F;
    
    UpdatePlayerPositions();
}

void LobbyScene::ExitLobby()
{
    m_isInLobby = false;
    m_state.countdownActive = false;
    m_state.countdownTimer = -1.0F;
}

void LobbyScene::SetPlayers(const std::vector<LobbyPlayer>& players)
{
    m_state.players = players;
    UpdatePlayerPositions();
}

void LobbyScene::SetLocalPlayerReady(bool ready)
{
    if (m_state.localPlayerIndex >= 0 && m_state.localPlayerIndex < static_cast<int>(m_state.players.size()))
    {
        m_state.players[m_state.localPlayerIndex].isReady = ready;
        
        if (m_onReadyChanged)
        {
            m_onReadyChanged(ready);
        }
    }
}

void LobbyScene::SetLocalPlayerRole(const std::string& role)
{
    if (m_state.localPlayerIndex >= 0 && m_state.localPlayerIndex < static_cast<int>(m_state.players.size()))
    {
        m_state.players[m_state.localPlayerIndex].selectedRole = role;
        
        if (m_onRoleChanged)
        {
            m_onRoleChanged(role);
        }
    }
}

void LobbyScene::SetLocalPlayerPerks(const std::array<std::string, 4>& perks)
{
    m_state.selectedPerks = perks;
}

void LobbyScene::SetCountdown(float seconds)
{
    m_state.countdownTimer = seconds;
    m_state.countdownActive = (seconds > 0.0F);
}

void LobbyScene::CancelCountdown()
{
    m_state.countdownActive = false;
    m_state.countdownTimer = -1.0F;
}

void LobbyScene::UpdateCamera(float deltaSeconds)
{
    m_cameraAngle += deltaSeconds * 5.0F;
    if (m_cameraAngle >= 360.0F)
    {
        m_cameraAngle -= 360.0F;
    }
    
    m_cameraTarget = glm::vec3{0.0F, 0.5F, 0.0F};
}

void LobbyScene::UpdateFireParticles(float deltaSeconds)
{
    m_fireTime += deltaSeconds;
    
    if (m_fireParticles.size() < 30 && static_cast<float>(std::rand()) / RAND_MAX < 0.3F)
    {
        FireParticle particle;
        const float angle = static_cast<float>(std::rand()) / RAND_MAX * 6.28318F;
        const float radius = static_cast<float>(std::rand()) / RAND_MAX * 0.3F;
        
        particle.position = glm::vec3{
            std::cos(angle) * radius,
            0.0F,
            std::sin(angle) * radius
        };
        particle.velocity = glm::vec3{
            (static_cast<float>(std::rand()) / RAND_MAX - 0.5F) * 0.2F,
            0.5F + static_cast<float>(std::rand()) / RAND_MAX * 0.5F,
            (static_cast<float>(std::rand()) / RAND_MAX - 0.5F) * 0.2F
        };
        particle.life = 0.0F;
        particle.maxLife = 0.8F + static_cast<float>(std::rand()) / RAND_MAX * 0.4F;
        particle.size = 0.1F + static_cast<float>(std::rand()) / RAND_MAX * 0.1F;
        
        m_fireParticles.push_back(particle);
    }
    
    for (auto& particle : m_fireParticles)
    {
        particle.life += deltaSeconds;
        particle.position += particle.velocity * deltaSeconds;
        particle.velocity.y += deltaSeconds * 0.5F;
    }
    
    m_fireParticles.erase(
        std::remove_if(m_fireParticles.begin(), m_fireParticles.end(),
            [](const FireParticle& p) { return p.life >= p.maxLife; }),
        m_fireParticles.end()
    );
}

void LobbyScene::UpdatePlayerPositions()
{
    const int playerCount = static_cast<int>(m_state.players.size());
    if (playerCount == 0) return;
    
    for (int i = 0; i < playerCount; ++i)
    {
        const float angle = (static_cast<float>(i) / playerCount) * 6.28318F - 1.5708F;
        const float radius = kFireRadius + 0.5F;
        
        m_state.players[i].worldPosition = glm::vec3{
            std::cos(angle) * radius,
            0.0F,
            std::sin(angle) * radius
        };
        m_state.players[i].rotation = angle + 3.14159F;
    }
}

void LobbyScene::Render3DScene()
{
    if (!m_renderer) return;
    
    DrawGroundPlane();
    DrawCampfire();
    
    for (const auto& particle : m_fireParticles)
    {
        float alpha = 1.0F - (particle.life / particle.maxLife);
        DrawFireParticle(particle.position, particle.size, alpha);
    }
    
    DrawPlayerModels();
}

void LobbyScene::RenderLobbyUI()
{
    if (!m_ui) return;
    
    const int screenWidth = m_ui->ScreenWidth();
    const int screenHeight = m_ui->ScreenHeight();
    const float scale = m_ui->Scale();
    const auto& theme = m_ui->Theme();
    
    RenderPlayerSlots();
    
    if (m_state.localPlayerIndex >= 0 && m_state.localPlayerIndex < static_cast<int>(m_state.players.size()))
    {
        RenderPlayerDetails(m_state.localPlayerIndex);
    }
    
    RenderReadyButton();
    RenderMatchSettings();
    
    if (m_state.countdownActive)
    {
        RenderCountdown();
    }
}

void LobbyScene::RenderPlayerSlots()
{
    if (!m_ui) return;
    
    const float scale = m_ui->Scale();
    const auto& theme = m_ui->Theme();
    const int screenWidth = m_ui->ScreenWidth();
    
    const float slotWidth = 180.0F * scale;
    const float slotHeight = 220.0F * scale;
    const float totalWidth = kMaxPlayers * slotWidth + (kMaxPlayers - 1) * 10.0F * scale;
    float startX = (screenWidth - totalWidth) / 2.0F;
    const float startY = 20.0F * scale;
    
    for (int i = 0; i < kMaxPlayers; ++i)
    {
        DrawPlayerSlot(startX + i * (slotWidth + 10.0F * scale), startY, slotWidth, slotHeight, i);
    }
}

void LobbyScene::RenderPlayerDetails(int playerIndex)
{
    if (!m_ui || playerIndex < 0) return;
    
    const float scale = m_ui->Scale();
    const int screenWidth = m_ui->ScreenWidth();
    const int screenHeight = m_ui->ScreenHeight();
    
    const float panelWidth = 350.0F * scale;
    const float panelHeight = 300.0F * scale;
    const float panelX = screenWidth - panelWidth - 20.0F * scale;
    const float panelY = screenHeight - panelHeight - 100.0F * scale;
    
    DrawUIPanel(panelX, panelY, panelWidth, panelHeight);
    
    DrawRoleSelector(panelX + 20.0F * scale, panelY + 60.0F * scale);
    DrawPerkSlots(panelX + 20.0F * scale, panelY + 150.0F * scale);
}

void LobbyScene::RenderReadyButton()
{
    if (!m_ui) return;
    
    const float scale = m_ui->Scale();
    const auto& theme = m_ui->Theme();
    const int screenWidth = m_ui->ScreenWidth();
    const int screenHeight = m_ui->ScreenHeight();
    
    const float buttonWidth = 200.0F * scale;
    const float buttonHeight = 50.0F * scale;
    const float buttonX = (screenWidth - buttonWidth) / 2.0F;
    const float buttonY = screenHeight - buttonHeight - 30.0F * scale;
    
    const bool isLocalReady = (m_state.localPlayerIndex >= 0 && 
                               m_state.localPlayerIndex < static_cast<int>(m_state.players.size()) &&
                               m_state.players[m_state.localPlayerIndex].isReady);
    
    engine::ui::UiRect buttonRect{buttonX, buttonY, buttonWidth, buttonHeight};
    glm::vec4 buttonColor = isLocalReady ? theme.colorSuccess : theme.colorAccent;
    buttonColor.a = 0.9F;
    m_ui->DrawRect(buttonRect, buttonColor);
    m_ui->DrawRectOutline(buttonRect, 2.0F, theme.colorPanelBorder);
    
    const std::string buttonText = isLocalReady ? "READY" : "READY UP";
    m_ui->DrawTextLabel(buttonX + 20.0F * scale, buttonY + 15.0F * scale, buttonText, theme.colorText, 1.2F * scale);
}

void LobbyScene::RenderCountdown()
{
    if (!m_ui) return;
    
    const float scale = m_ui->Scale();
    const auto& theme = m_ui->Theme();
    const int screenWidth = m_ui->ScreenWidth();
    const int screenHeight = m_ui->ScreenHeight();
    
    const float overlayWidth = 300.0F * scale;
    const float overlayHeight = 100.0F * scale;
    const float overlayX = (screenWidth - overlayWidth) / 2.0F;
    const float overlayY = screenHeight / 2.0F - overlayHeight / 2.0F;
    
    engine::ui::UiRect overlayRect{overlayX, overlayY, overlayWidth, overlayHeight};
    glm::vec4 overlayColor = theme.colorPanel;
    overlayColor.a = 0.95F;
    m_ui->DrawRect(overlayRect, overlayColor);
    m_ui->DrawRectOutline(overlayRect, 3.0F, theme.colorAccent);
    
    const int countdownInt = static_cast<int>(std::ceil(m_state.countdownTimer));
    const std::string countdownText = "Match starts in " + std::to_string(countdownInt);
    m_ui->DrawTextLabel(overlayX + 30.0F * scale, overlayY + 30.0F * scale, countdownText, theme.colorText, 1.1F * scale);
    m_ui->DrawTextLabel(overlayX + 30.0F * scale, overlayY + 60.0F * scale, "Get Ready!", theme.colorSuccess, 1.3F * scale);
}

void LobbyScene::RenderMatchSettings()
{
    if (!m_ui || !m_state.isHost) return;
    
    const float scale = m_ui->Scale();
    const auto& theme = m_ui->Theme();
    
    const float panelX = 20.0F * scale;
    const float panelY = 260.0F * scale;
    const float panelWidth = 200.0F * scale;
    const float panelHeight = 100.0F * scale;
    
    DrawUIPanel(panelX, panelY, panelWidth, panelHeight);
    
    m_ui->DrawTextLabel(panelX + 15.0F * scale, panelY + 15.0F * scale, "Match Settings", theme.colorText, 0.9F * scale);
    m_ui->DrawTextLabel(panelX + 15.0F * scale, panelY + 40.0F * scale, "Map: " + m_state.selectedMap, theme.colorTextMuted, 0.8F * scale);
}

void LobbyScene::DrawFireParticle(const glm::vec3& position, float size, float alpha)
{
}

void LobbyScene::DrawGroundPlane()
{
    if (!m_renderer) return;
    
    m_renderer->DrawGrid(10, 1.0F, glm::vec3{0.15F, 0.12F, 0.1F}, glm::vec3{0.08F, 0.06F, 0.05F}, glm::vec4{0.03F, 0.02F, 0.02F, 0.5F});
}

void LobbyScene::DrawCampfire()
{
    if (!m_renderer) return;
    
    m_renderer->DrawBox(
        glm::vec3{0.0F, 0.15F, 0.0F},
        glm::vec3{0.4F, 0.15F, 0.4F},
        glm::vec3{0.4F, 0.25F, 0.1F}
    );
    
    for (int i = 0; i < 5; ++i)
    {
        float angle = i * 1.2566F;
        float x = std::cos(angle) * 0.25F;
        float z = std::sin(angle) * 0.25F;
        
        m_renderer->DrawOrientedBox(
            glm::vec3{x, 0.35F, z},
            glm::vec3{0.05F, 0.3F, 0.05F},
            glm::vec3{0.0F, 45.0F + i * 20.0F, 0.0F},
            glm::vec3{0.35F, 0.2F, 0.08F}
        );
    }
    
    engine::render::MaterialParams fireMat;
    fireMat.emissive = 1.0F;
    fireMat.unlit = true;
    
    m_renderer->DrawBox(
        glm::vec3{0.0F, 0.5F, 0.0F},
        glm::vec3{0.2F, 0.2F, 0.2F},
        glm::vec3{1.0F, 0.4F, 0.1F},
        fireMat
    );
}

void LobbyScene::DrawPlayerModels()
{
    if (!m_renderer) return;
    
    for (const auto& player : m_state.players)
    {
        if (!player.isConnected) continue;
        
        glm::vec3 color = player.selectedRole == "killer" 
            ? glm::vec3{0.8F, 0.1F, 0.1F} 
            : glm::vec3{0.2F, 0.5F, 0.8F};
        
        m_renderer->DrawCapsule(
            player.worldPosition + glm::vec3{0.0F, 0.9F, 0.0F},
            1.4F,
            0.3F,
            color
        );
        
        m_renderer->DrawBox(
            player.worldPosition + glm::vec3{0.0F, 1.75F, 0.0F},
            glm::vec3{0.2F, 0.2F, 0.2F},
            color
        );
    }
}

void LobbyScene::DrawUIPanel(float x, float y, float width, float height)
{
    if (!m_ui) return;
    
    const auto& theme = m_ui->Theme();
    
    engine::ui::UiRect panelRect{x, y, width, height};
    glm::vec4 panelColor = theme.colorPanel;
    panelColor.a = 0.9F;
    m_ui->DrawRect(panelRect, panelColor);
    m_ui->DrawRectOutline(panelRect, 2.0F, theme.colorPanelBorder);
}

void LobbyScene::DrawPlayerSlot(float x, float y, float width, float height, int playerIndex)
{
    if (!m_ui) return;
    
    const auto& theme = m_ui->Theme();
    const float scale = m_ui->Scale();
    
    const bool hasPlayer = playerIndex < static_cast<int>(m_state.players.size());
    const bool isLocal = playerIndex == m_state.localPlayerIndex;
    
    engine::ui::UiRect slotRect{x, y, width, height};
    glm::vec4 slotColor = theme.colorBackground;
    slotColor.a = 0.8F;
    m_ui->DrawRect(slotRect, slotColor);
    
    glm::vec4 borderColor = isLocal ? theme.colorAccent : theme.colorPanelBorder;
    m_ui->DrawRectOutline(slotRect, isLocal ? 3.0F : 2.0F, borderColor);
    
    if (hasPlayer)
    {
        const auto& player = m_state.players[playerIndex];
        
        m_ui->DrawTextLabel(x + 10.0F * scale, y + 10.0F * scale, player.name, theme.colorText, 0.9F * scale);
        
        const std::string roleText = player.selectedRole == "survivor" ? "Survivor" : "Killer";
        m_ui->DrawTextLabel(x + 10.0F * scale, y + 35.0F * scale, roleText, theme.colorTextMuted, 0.8F * scale);
        
        if (player.isReady)
        {
            glm::vec4 readyColor = theme.colorSuccess;
            readyColor.a = 0.3F;
            engine::ui::UiRect readyRect{x + 5.0F, y + height - 35.0F * scale, width - 10.0F, 25.0F * scale};
            m_ui->DrawRect(readyRect, readyColor);
            m_ui->DrawTextLabel(x + 15.0F * scale, y + height - 30.0F * scale, "READY", theme.colorSuccess, 0.9F * scale);
        }
        
        if (player.isHost)
        {
            m_ui->DrawTextLabel(x + width - 50.0F * scale, y + 10.0F * scale, "HOST", theme.colorAccent, 0.7F * scale);
        }
    }
    else
    {
        m_ui->DrawTextLabel(x + width / 2.0F - 30.0F * scale, y + height / 2.0F, "Empty", theme.colorTextMuted, 0.9F * scale);
    }
}

void LobbyScene::DrawRoleSelector(float x, float y)
{
    if (!m_ui) return;
    
    const auto& theme = m_ui->Theme();
    const float scale = m_ui->Scale();
    
    m_ui->DrawTextLabel(x, y - 20.0F * scale, "Select Role:", theme.colorText, 0.9F * scale);
    
    const float buttonWidth = 150.0F * scale;
    const float buttonHeight = 35.0F * scale;
    
    std::string currentRole = "survivor";
    if (m_state.localPlayerIndex >= 0 && m_state.localPlayerIndex < static_cast<int>(m_state.players.size()))
    {
        currentRole = m_state.players[m_state.localPlayerIndex].selectedRole;
    }
    
    {
        engine::ui::UiRect survivorRect{x, y, buttonWidth, buttonHeight};
        glm::vec4 color = (currentRole == "survivor") ? theme.colorAccent : theme.colorButton;
        color.a = 0.8F;
        m_ui->DrawRect(survivorRect, color);
        m_ui->DrawRectOutline(survivorRect, 2.0F, theme.colorPanelBorder);
        m_ui->DrawTextLabel(x + 10.0F * scale, y + 8.0F * scale, "Survivor", theme.colorText, 0.9F * scale);
    }
    
    {
        engine::ui::UiRect killerRect{x + buttonWidth + 10.0F * scale, y, buttonWidth, buttonHeight};
        glm::vec4 color = (currentRole == "killer") ? theme.colorDanger : theme.colorButton;
        color.a = 0.8F;
        m_ui->DrawRect(killerRect, color);
        m_ui->DrawRectOutline(killerRect, 2.0F, theme.colorPanelBorder);
        m_ui->DrawTextLabel(x + buttonWidth + 20.0F * scale, y + 8.0F * scale, "Killer", theme.colorText, 0.9F * scale);
    }
}

void LobbyScene::DrawPerkSlots(float x, float y)
{
    if (!m_ui) return;
    
    const auto& theme = m_ui->Theme();
    const float scale = m_ui->Scale();
    
    m_ui->DrawTextLabel(x, y - 20.0F * scale, "Perk Loadout:", theme.colorText, 0.9F * scale);
    
    const float slotSize = 60.0F * scale;
    const float spacing = 8.0F * scale;
    
    for (int i = 0; i < 4; ++i)
    {
        const float slotX = x + i * (slotSize + spacing);
        
        engine::ui::UiRect slotRect{slotX, y, slotSize, slotSize};
        glm::vec4 slotColor = m_state.selectedPerks[i].empty() ? theme.colorBackground : theme.colorButton;
        slotColor.a = 0.8F;
        m_ui->DrawRect(slotRect, slotColor);
        m_ui->DrawRectOutline(slotRect, 2.0F, theme.colorPanelBorder);
        
        if (!m_state.selectedPerks[i].empty())
        {
            m_ui->DrawTextLabel(slotX + 5.0F * scale, y + slotSize / 2.0F, "P" + std::to_string(i + 1), theme.colorText, 0.7F * scale);
        }
        else
        {
            m_ui->DrawTextLabel(slotX + 15.0F * scale, y + slotSize / 2.0F, "+", theme.colorTextMuted, 1.2F * scale);
        }
    }
}

}
