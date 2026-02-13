#include "LobbyScene.hpp"

#include <cmath>
#include <algorithm>
#include <random>

#include <glm/gtc/matrix_transform.hpp>

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
                const bool hasValidLocalPlayer =
                    m_state.localPlayerIndex >= 0 &&
                    m_state.localPlayerIndex < static_cast<int>(m_state.players.size());
                if (hasValidLocalPlayer)
                {
                    auto& localPlayer = m_state.players[m_state.localPlayerIndex];
                    m_onStartMatch(m_state.selectedMap, localPlayer.selectedRole, m_state.selectedPerks);
                }
            }
        }
    }
}

void LobbyScene::HandleInput()
{
    if (!m_ui || !m_input || !m_isInLobby) return;
    
    const float scale = m_ui->Scale();
    const int screenWidth = m_ui->ScreenWidth();
    const int screenHeight = m_ui->ScreenHeight();
    const auto mousePos = m_input->MousePosition();
    
    const float buttonWidth = 200.0F * scale;
    const float buttonHeight = 50.0F * scale;
    const float buttonX = (screenWidth - buttonWidth) / 2.0F;
    const float buttonY = screenHeight - buttonHeight - 30.0F * scale;
    
    // Calculate button positions (same as render)
    const float btnWidth = 180.0F * scale;
    const float btnHeight = 50.0F * scale;
    const float forceBtnWidth = 140.0F * scale;
    const float totalBtnWidth = btnWidth + 10.0F * scale + forceBtnWidth;
    const float btnStartX = (screenWidth - totalBtnWidth) / 2.0F;
    
    if (isMouseOver(btnStartX, buttonY, btnWidth, btnHeight) && m_input->IsMousePressed(0))
    {
        const bool isCurrentlyReady = (m_state.localPlayerIndex >= 0 && 
                                       m_state.localPlayerIndex < static_cast<int>(m_state.players.size()) &&
                                       m_state.players[m_state.localPlayerIndex].isReady);
        SetLocalPlayerReady(!isCurrentlyReady);
    }
    
    // Force Start button (available to everyone)
    if (!m_state.countdownActive)
    {
        const float forceBtnX = btnStartX + btnWidth + 10.0F * scale;
        
        if (isMouseOver(forceBtnX, buttonY, forceBtnWidth, btnHeight) && m_input->IsMousePressed(0))
        {
            // Force start match immediately
            if (m_onStartMatch)
            {
                const bool hasValidLocalPlayer =
                    m_state.localPlayerIndex >= 0 &&
                    m_state.localPlayerIndex < static_cast<int>(m_state.players.size());
                if (hasValidLocalPlayer)
                {
                    auto& localPlayer = m_state.players[m_state.localPlayerIndex];
                    m_onStartMatch(m_state.selectedMap, localPlayer.selectedRole, m_state.selectedPerks);
                }
            }
        }
    }
    
    if (m_state.localPlayerIndex >= 0 && m_state.localPlayerIndex < static_cast<int>(m_state.players.size()))
    {
        const float panelWidth = 420.0F * scale;
        const float panelHeight = 480.0F * scale;
        const float panelX = screenWidth - panelWidth - 20.0F * scale;
        const float panelY = screenHeight - panelHeight - 60.0F * scale;
        const float roleY = panelY + 60.0F * scale;
        
        const float roleButtonWidth = 150.0F * scale;
        const float roleButtonHeight = 35.0F * scale;
        
        // Role selector buttons
        if (isMouseOver(panelX + 20.0F * scale, roleY, roleButtonWidth, roleButtonHeight) && m_input->IsMousePressed(0))
        {
            SetLocalPlayerRole("survivor");
            m_selectedCharacterIndex = 0; // Reset character selection
        }
        
        if (isMouseOver(panelX + 20.0F * scale + roleButtonWidth + 10.0F * scale, roleY, roleButtonWidth, roleButtonHeight) && m_input->IsMousePressed(0))
        {
            SetLocalPlayerRole("killer");
            m_selectedCharacterIndex = 0; // Reset character selection
        }
        
        const bool isSurvivor = m_state.players[m_state.localPlayerIndex].selectedRole == "survivor";
        
        // Character selector dropdown
        const float charY = panelY + 115.0F * scale;
        const float btnWidth = 180.0F * scale;
        const float btnHeight = 35.0F * scale;
        
        if (m_input->IsMousePressed(0))
        {
            if (isMouseOver(panelX + 20.0F * scale, charY, btnWidth, btnHeight))
            {
                m_characterDropdownOpen = !m_characterDropdownOpen;
                CloseAllDropdownsExcept("character");
            }
            else if (m_characterDropdownOpen)
            {
                const auto& ids = isSurvivor ? m_survivorIds : m_killerIds;
                const float dropdownY = charY + btnHeight + 2.0F * scale;
                const float optionHeight = 28.0F * scale;
                
                for (std::size_t i = 0; i < ids.size() && i < 6; ++i)
                {
                    const float optY = dropdownY + 5.0F * scale + static_cast<float>(i) * optionHeight;
                    if (isMouseOver(panelX + 20.0F * scale + 3.0F * scale, optY, btnWidth - 6.0F * scale, optionHeight - 2.0F * scale))
                    {
                        m_selectedCharacterIndex = static_cast<int>(i);
                        SetLocalPlayerCharacter(ids[i]);
                        m_characterDropdownOpen = false;
                        break;
                    }
                }
                // Click outside closes dropdown
                m_characterDropdownOpen = false;
            }
        }
        
        // Item/Power selector (row below character)
        const float itemY = panelY + 175.0F * scale;
        if (m_input->IsMousePressed(0))
        {
            if (isSurvivor)
            {
                // Item dropdown
                if (isMouseOver(panelX + 20.0F * scale, itemY, btnWidth, btnHeight))
                {
                    m_itemDropdownOpen = !m_itemDropdownOpen;
                    CloseAllDropdownsExcept("item");
                }
                else if (m_itemDropdownOpen)
                {
                    const float dropdownY = itemY + btnHeight + 2.0F * scale;
                    const float optionHeight = 28.0F * scale;
                    
                    // Check "None" option
                    if (isMouseOver(panelX + 20.0F * scale + 3.0F * scale, dropdownY + 5.0F * scale, btnWidth - 6.0F * scale, optionHeight - 2.0F * scale))
                    {
                        m_selectedItemIndex = 0;
                        SetLocalPlayerItem("", "", "");
                        m_itemDropdownOpen = false;
                    }
                    else
                    {
                        for (std::size_t i = 0; i < m_itemIds.size() && i < 6; ++i)
                        {
                            const float optY = dropdownY + 5.0F * scale + static_cast<float>(i + 1) * optionHeight;
                            if (isMouseOver(panelX + 20.0F * scale + 3.0F * scale, optY, btnWidth - 6.0F * scale, optionHeight - 2.0F * scale))
                            {
                                m_selectedItemIndex = static_cast<int>(i + 1);
                                SetLocalPlayerItem(m_itemIds[i], m_state.selectedAddonA, m_state.selectedAddonB);
                                m_itemDropdownOpen = false;
                                break;
                            }
                        }
                    }
                    m_itemDropdownOpen = false;
                }
            }
            else
            {
                // Power dropdown
                if (isMouseOver(panelX + 20.0F * scale, itemY, btnWidth, btnHeight))
                {
                    m_powerDropdownOpen = !m_powerDropdownOpen;
                    CloseAllDropdownsExcept("power");
                }
                else if (m_powerDropdownOpen)
                {
                    const float dropdownY = itemY + btnHeight + 2.0F * scale;
                    const float optionHeight = 28.0F * scale;
                    
                    for (std::size_t i = 0; i < m_powerIds.size() && i < 5; ++i)
                    {
                        const float optY = dropdownY + 5.0F * scale + static_cast<float>(i) * optionHeight;
                        if (isMouseOver(panelX + 20.0F * scale + 3.0F * scale, optY, btnWidth - 6.0F * scale, optionHeight - 2.0F * scale))
                        {
                            m_selectedPowerIndex = static_cast<int>(i);
                            SetLocalPlayerPower(m_powerIds[i], m_state.selectedAddonA, m_state.selectedAddonB);
                            m_powerDropdownOpen = false;
                            break;
                        }
                    }
                    m_powerDropdownOpen = false;
                }
            }
        }
        
        // Addon selectors
        const float addonY = panelY + 235.0F * scale;
        const float addonBtnWidth = 130.0F * scale;
        const float addonBtnHeight = 30.0F * scale;
        
        if (m_input->IsMousePressed(0))
        {
            // Addon A
            if (isMouseOver(panelX + 20.0F * scale, addonY, addonBtnWidth, addonBtnHeight))
            {
                m_addonADropdownOpen = !m_addonADropdownOpen;
                CloseAllDropdownsExcept("addonA");
            }
            else if (m_addonADropdownOpen)
            {
                HandleAddonDropdownClick(panelX + 20.0F * scale, addonY, true);
            }
            
            // Addon B - position updated to match wider layout
            if (isMouseOver(panelX + 170.0F * scale, addonY, addonBtnWidth, addonBtnHeight))
            {
                m_addonBDropdownOpen = !m_addonBDropdownOpen;
                CloseAllDropdownsExcept("addonB");
            }
            else if (m_addonBDropdownOpen)
            {
                HandleAddonDropdownClick(panelX + 170.0F * scale, addonY, false);
            }
        }
        
        // Perk slot positions (updated position)
        const float perkY = panelY + 305.0F * scale;
        const float perkStartX = panelX + 20.0F * scale;
        const float slotSize = 60.0F * scale;
        const float spacing = 8.0F * scale;
        const float dropdownWidth = 160.0F * scale;
        const float optionHeight = 22.0F * scale;
        
        if (m_selectedPerkSlot >= 0 && m_selectedPerkSlot < 4 && !m_availablePerkIds.empty())
        {
            const float dropdownY = perkY + slotSize + 5.0F * scale;
            const float slotXSelected = perkStartX + m_selectedPerkSlot * (slotSize + spacing);
            const float dropdownX = std::min(slotXSelected, static_cast<float>(screenWidth) - dropdownWidth - 10.0F * scale);
            const std::size_t numOptions = std::min(m_availablePerkIds.size() + 1, static_cast<std::size_t>(10));
            const float dropdownHeight = static_cast<float>(numOptions) * optionHeight + 10.0F * scale;
            
            if (m_input->IsMousePressed(0))
            {
                // Check if clicked in dropdown area
                if (isMouseOver(dropdownX, dropdownY, dropdownWidth, dropdownHeight))
                {
                    // "None" option
                    if (isMouseOver(dropdownX + 3.0F * scale, dropdownY + 5.0F * scale, dropdownWidth - 6.0F * scale, optionHeight))
                    {
                        m_state.selectedPerks[m_selectedPerkSlot] = "";
                        if (m_onPerksChanged) m_onPerksChanged(m_state.selectedPerks);
                        m_selectedPerkSlot = -1;
                    }
                    else
                    {
                        // Perk options - check which one was clicked
                        const float clickY = mousePos.y - (dropdownY + 5.0F * scale + optionHeight);
                        const int clickedIndex = static_cast<int>(clickY / optionHeight);
                        
                        if (clickY >= 0.0F && clickedIndex >= 0 && static_cast<std::size_t>(clickedIndex) < m_availablePerkIds.size())
                        {
                            m_state.selectedPerks[m_selectedPerkSlot] = m_availablePerkIds[clickedIndex];
                            if (m_onPerksChanged) m_onPerksChanged(m_state.selectedPerks);
                            m_selectedPerkSlot = -1;
                        }
                    }
                }
                else
                {
                    // Click outside dropdown - check if clicked on another slot
                    bool clickedSlot = false;
                    for (int i = 0; i < 4; ++i)
                    {
                        const float sx = perkStartX + i * (slotSize + spacing);
                        if (isMouseOver(sx, perkY, slotSize, slotSize))
                        {
                            m_selectedPerkSlot = i;
                            clickedSlot = true;
                            break;
                        }
                    }
                    if (!clickedSlot)
                    {
                        m_selectedPerkSlot = -1;
                    }
                }
            }
        }
        else
        {
            // No dropdown - check for slot clicks
            for (int i = 0; i < 4; ++i)
            {
                const float slotX = perkStartX + i * (slotSize + spacing);
                if (isMouseOver(slotX, perkY, slotSize, slotSize) && m_input->IsMousePressed(0))
                {
                    m_selectedPerkSlot = i;
                    break;
                }
            }
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

void LobbyScene::SetLocalPlayerCharacter(const std::string& characterId)
{
    m_state.selectedCharacter = characterId;
    if (m_state.localPlayerIndex >= 0 && m_state.localPlayerIndex < static_cast<int>(m_state.players.size()))
    {
        m_state.players[m_state.localPlayerIndex].characterId = characterId;
    }
    
    if (m_onCharacterChanged)
    {
        m_onCharacterChanged(characterId);
    }
}

void LobbyScene::SetLocalPlayerItem(const std::string& itemId, const std::string& addonA, const std::string& addonB)
{
    m_state.selectedItem = itemId;
    m_state.selectedAddonA = addonA;
    m_state.selectedAddonB = addonB;
    
    if (m_onItemChanged)
    {
        m_onItemChanged(itemId, addonA, addonB);
    }
}

void LobbyScene::SetLocalPlayerPower(const std::string& powerId, const std::string& addonA, const std::string& addonB)
{
    m_state.selectedPower = powerId;
    m_state.selectedAddonA = addonA;
    m_state.selectedAddonB = addonB;
    
    if (m_onPowerChanged)
    {
        m_onPowerChanged(powerId, addonA, addonB);
    }
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

glm::vec3 LobbyScene::CameraPosition() const
{
    const float angleRad = glm::radians(m_cameraAngle);
    return glm::vec3{
        std::cos(angleRad) * m_cameraDistance,
        m_cameraHeight,
        std::sin(angleRad) * m_cameraDistance
    };
}

glm::mat4 LobbyScene::BuildViewProjection(float aspectRatio) const
{
    const glm::vec3 cameraPos = CameraPosition();
    const glm::vec3 up{0.0F, 1.0F, 0.0F};
    
    m_viewMatrix = glm::lookAt(cameraPos, m_cameraTarget, up);
    m_projectionMatrix = glm::perspective(glm::radians(45.0F), aspectRatio, 0.1F, 100.0F);
    
    return m_projectionMatrix * m_viewMatrix;
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
    DrawEnvironment();
    DrawCampfire();
    
    for (const auto& particle : m_fireParticles)
    {
        float alpha = 1.0F - (particle.life / particle.maxLife);
        DrawFireParticle(particle.position, particle.size, alpha);
    }
    
    DrawPlayerModels();
}

void LobbyScene::Render3D()
{
    if (!m_isInLobby) return;
    Render3DScene();
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

void LobbyScene::RenderUI()
{
    if (!m_isInLobby) return;
    RenderLobbyUI();
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
    
    const float panelWidth = 420.0F * scale;
    const float panelHeight = 480.0F * scale;
    const float panelX = screenWidth - panelWidth - 20.0F * scale;
    const float panelY = screenHeight - panelHeight - 60.0F * scale;
    
    DrawUIPanel(panelX, panelY, panelWidth, panelHeight);
    
    const bool isSurvivor = (m_state.localPlayerIndex >= 0 && 
                             m_state.localPlayerIndex < static_cast<int>(m_state.players.size()) &&
                             m_state.players[m_state.localPlayerIndex].selectedRole == "survivor");
    
    // Layout with proper spacing:
    // Role selector: 60px from top, buttons are 35px tall, ends at ~95px
    // Character selector: 115px from top, dropdown is 35px, ends at ~150px
    // Item/Power selector: 175px from top, dropdown is 35px, ends at ~210px
    // Addon selectors: 235px from top, dropdowns are 30px, ends at ~265px
    // Perk slots: 305px from top, slots are 60px, ends at ~365px
    
    // First pass: draw all static elements (buttons, slots)
    DrawRoleSelector(panelX + 20.0F * scale, panelY + 60.0F * scale, false);
    DrawCharacterSelector(panelX + 20.0F * scale, panelY + 115.0F * scale, false);
    
    if (isSurvivor)
    {
        DrawItemSelector(panelX + 20.0F * scale, panelY + 175.0F * scale, false);
        DrawAddonSelector(panelX + 20.0F * scale, panelY + 235.0F * scale, true, false);
        DrawAddonSelector(panelX + 170.0F * scale, panelY + 235.0F * scale, false, false);
    }
    else
    {
        DrawPowerSelector(panelX + 20.0F * scale, panelY + 175.0F * scale, false);
        DrawAddonSelector(panelX + 20.0F * scale, panelY + 235.0F * scale, true, false);
        DrawAddonSelector(panelX + 170.0F * scale, panelY + 235.0F * scale, false, false);
    }
    
    DrawPerkSlots(panelX + 20.0F * scale, panelY + 305.0F * scale, false);
    
    // Second pass: draw all dropdowns on top (higher z-index)
    DrawRoleSelector(panelX + 20.0F * scale, panelY + 60.0F * scale, true);
    DrawCharacterSelector(panelX + 20.0F * scale, panelY + 115.0F * scale, true);
    
    if (isSurvivor)
    {
        DrawItemSelector(panelX + 20.0F * scale, panelY + 175.0F * scale, true);
        DrawAddonSelector(panelX + 20.0F * scale, panelY + 235.0F * scale, true, true);
        DrawAddonSelector(panelX + 170.0F * scale, panelY + 235.0F * scale, false, true);
    }
    else
    {
        DrawPowerSelector(panelX + 20.0F * scale, panelY + 175.0F * scale, true);
        DrawAddonSelector(panelX + 20.0F * scale, panelY + 235.0F * scale, true, true);
        DrawAddonSelector(panelX + 170.0F * scale, panelY + 235.0F * scale, false, true);
    }
    
    DrawPerkSlots(panelX + 20.0F * scale, panelY + 305.0F * scale, true);
}

void LobbyScene::RenderReadyButton()
{
    if (!m_ui) return;
    
    const float scale = m_ui->Scale();
    const auto& theme = m_ui->Theme();
    const int screenWidth = m_ui->ScreenWidth();
    const int screenHeight = m_ui->ScreenHeight();
    
    const float buttonWidth = 180.0F * scale;
    const float buttonHeight = 50.0F * scale;
    const float forceButtonWidth = 140.0F * scale;
    const float totalButtonWidth = buttonWidth + 10.0F * scale + forceButtonWidth;
    const float buttonStartX = (screenWidth - totalButtonWidth) / 2.0F;
    const float buttonY = screenHeight - buttonHeight - 30.0F * scale;
    
    const bool isLocalReady = (m_state.localPlayerIndex >= 0 && 
                               m_state.localPlayerIndex < static_cast<int>(m_state.players.size()) &&
                               m_state.players[m_state.localPlayerIndex].isReady);
    
    // READY button
    engine::ui::UiRect buttonRect{buttonStartX, buttonY, buttonWidth, buttonHeight};
    glm::vec4 buttonColor = isLocalReady ? theme.colorSuccess : theme.colorAccent;
    buttonColor.a = 0.9F;
    m_ui->DrawRect(buttonRect, buttonColor);
    m_ui->DrawRectOutline(buttonRect, 2.0F, theme.colorPanelBorder);
    
    const std::string buttonText = isLocalReady ? "READY" : "READY UP";
    m_ui->DrawTextLabel(buttonStartX + 15.0F * scale, buttonY + 15.0F * scale, buttonText, theme.colorText, 1.2F * scale);
    
    // Force Start button (available to everyone when countdown not active)
    if (!m_state.countdownActive)
    {
        const float forceButtonX = buttonStartX + buttonWidth + 10.0F * scale;
        
        engine::ui::UiRect forceButtonRect{forceButtonX, buttonY, forceButtonWidth, buttonHeight};
        glm::vec4 forceButtonColor = theme.colorDanger;
        forceButtonColor.a = 0.9F;
        m_ui->DrawRect(forceButtonRect, forceButtonColor);
        m_ui->DrawRectOutline(forceButtonRect, 2.0F, theme.colorPanelBorder);
        m_ui->DrawTextLabel(forceButtonX + 8.0F * scale, buttonY + 15.0F * scale, "FORCE START", theme.colorText, 1.0F * scale);
    }
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
    if (!m_renderer) return;
    
    engine::render::MaterialParams fireMat;
    fireMat.emissive = 1.0F;
    fireMat.unlit = true;
    
    float intensity = alpha * 0.8F;
    m_renderer->DrawBox(
        position,
        glm::vec3{size, size, size},
        glm::vec3{1.0F, 0.4F + intensity * 0.4F, 0.1F},
        fireMat
    );
}

void LobbyScene::DrawGroundPlane()
{
    if (!m_renderer) return;
    
    // Main ground
    m_renderer->DrawGrid(20, 1.0F, glm::vec3{0.12F, 0.1F, 0.08F}, glm::vec3{0.06F, 0.05F, 0.04F}, glm::vec4{0.02F, 0.015F, 0.01F, 0.5F});
    
    // Dirt circle around campfire
    engine::render::MaterialParams dirtMat;
    dirtMat.roughness = 0.95F;
    for (int i = 0; i < 12; ++i)
    {
        const float angle = i * 0.5236F;
        const float radius = 1.5F + (i % 3) * 0.2F;
        const float size = 0.35F + (i % 2) * 0.15F;
        m_renderer->DrawBox(
            glm::vec3{std::cos(angle) * radius, 0.02F, std::sin(angle) * radius},
            glm::vec3{size, 0.02F, size},
            glm::vec3{0.25F, 0.18F, 0.12F},
            dirtMat
        );
    }
    
    // Small stones scattered around
    engine::render::MaterialParams stoneMat;
    stoneMat.roughness = 0.8F;
    for (int i = 0; i < 8; ++i)
    {
        const float angle = i * 0.785F + 0.3F;
        const float radius = 2.5F + static_cast<float>(i % 3) * 0.4F;
        m_renderer->DrawBox(
            glm::vec3{std::cos(angle) * radius, 0.04F, std::sin(angle) * radius},
            glm::vec3{0.08F, 0.05F, 0.1F},
            glm::vec3{0.35F, 0.32F, 0.3F},
            stoneMat
        );
    }
}

void LobbyScene::DrawCampfire()
{
    if (!m_renderer) return;
    
    engine::render::MaterialParams woodMat;
    woodMat.roughness = 0.9F;
    
    // Fire pit base (stone ring)
    engine::render::MaterialParams stoneMat;
    stoneMat.roughness = 0.85F;
    
    for (int i = 0; i < 12; ++i)
    {
        const float angle = i * 0.5236F;
        const float x = std::cos(angle) * 0.5F;
        const float z = std::sin(angle) * 0.5F;
        
        m_renderer->DrawOrientedBox(
            glm::vec3{x, 0.08F, z},
            glm::vec3{0.12F, 0.12F, 0.08F},
            glm::vec3{0.0F, glm::degrees(angle), 0.0F},
            glm::vec3{0.4F, 0.38F, 0.35F},
            stoneMat
        );
    }
    
    // Inner ash/gravel
    m_renderer->DrawBox(
        glm::vec3{0.0F, 0.02F, 0.0F},
        glm::vec3{0.35F, 0.02F, 0.35F},
        glm::vec3{0.2F, 0.18F, 0.15F}
    );
    
    // Logs in teepee formation
    for (int i = 0; i < 5; ++i)
    {
        float angle = i * 1.2566F;
        float x = std::cos(angle) * 0.22F;
        float z = std::sin(angle) * 0.22F;
        
        m_renderer->DrawOrientedBox(
            glm::vec3{x, 0.28F, z},
            glm::vec3{0.06F, 0.4F, 0.06F},
            glm::vec3{15.0F, 45.0F + i * 72.0F, i * 5.0F},
            glm::vec3{0.35F, 0.22F, 0.1F},
            woodMat
        );
    }
    
    // Cross logs at base
    for (int i = 0; i < 3; ++i)
    {
        const float angle = i * 2.094F;
        m_renderer->DrawOrientedBox(
            glm::vec3{std::cos(angle) * 0.15F, 0.08F, std::sin(angle) * 0.15F},
            glm::vec3{0.3F, 0.05F, 0.05F},
            glm::vec3{0.0F, glm::degrees(angle), 0.0F},
            glm::vec3{0.32F, 0.2F, 0.08F},
            woodMat
        );
    }
    
    // Fire core (bright orange/yellow glow)
    engine::render::MaterialParams fireMat;
    fireMat.emissive = 1.0F;
    fireMat.unlit = true;
    
    // Flickering effect based on time
    const float flicker = 0.9F + std::sin(m_fireTime * 12.0F) * 0.1F + std::sin(m_fireTime * 7.3F) * 0.05F;
    
    m_renderer->DrawBox(
        glm::vec3{0.0F, 0.3F, 0.0F},
        glm::vec3{0.15F * flicker, 0.2F, 0.15F * flicker},
        glm::vec3{1.0F, 0.5F, 0.1F},
        fireMat
    );
    
    // Brighter inner core
    m_renderer->DrawBox(
        glm::vec3{0.0F, 0.35F, 0.0F},
        glm::vec3{0.08F * flicker, 0.15F, 0.08F * flicker},
        glm::vec3{1.0F, 0.85F, 0.4F},
        fireMat
    );
}

void LobbyScene::DrawEnvironment()
{
    if (!m_renderer) return;
    
    DrawTrees();
    DrawRocks();
    DrawLogs();
}

void LobbyScene::DrawTrees()
{
    if (!m_renderer) return;
    
    engine::render::MaterialParams barkMat;
    barkMat.roughness = 0.95F;
    
    engine::render::MaterialParams foliageMat;
    foliageMat.roughness = 0.8F;
    
    // Dead/dark trees around the perimeter
    const auto drawTree = [&](float x, float z, float height, float rotation) {
        // Trunk
        m_renderer->DrawOrientedBox(
            glm::vec3{x, height * 0.5F, z},
            glm::vec3{0.15F, height * 0.5F, 0.15F},
            glm::vec3{0.0F, rotation, 3.0F},
            glm::vec3{0.25F, 0.2F, 0.15F},
            barkMat
        );
        
        // Bare branches
        for (int i = 0; i < 4; ++i)
        {
            const float branchAngle = rotation + i * 90.0F;
            const float branchY = height * 0.7F + i * 0.3F;
            m_renderer->DrawOrientedBox(
                glm::vec3{x, branchY, z},
                glm::vec3{0.6F, 0.03F, 0.03F},
                glm::vec3{35.0F, branchAngle, 0.0F},
                glm::vec3{0.2F, 0.18F, 0.12F},
                barkMat
            );
        }
    };
    
    // Trees around the perimeter
    drawTree(-6.0F, -4.0F, 4.5F, 15.0F);
    drawTree(6.0F, -3.5F, 5.0F, 85.0F);
    drawTree(-5.5F, 5.0F, 4.0F, 200.0F);
    drawTree(5.0F, 5.5F, 4.8F, 270.0F);
    drawTree(-7.0F, 1.0F, 3.8F, 45.0F);
    drawTree(7.5F, 0.5F, 4.2F, 160.0F);
}

void LobbyScene::DrawRocks()
{
    if (!m_renderer) return;
    
    engine::render::MaterialParams rockMat;
    rockMat.roughness = 0.85F;
    
    const auto drawRock = [&](float x, float z, float scale, float rotation) {
        // Main rock body
        m_renderer->DrawOrientedBox(
            glm::vec3{x, scale * 0.3F, z},
            glm::vec3{scale * 0.5F, scale * 0.35F, scale * 0.4F},
            glm::vec3{rotation, rotation * 0.5F, rotation * 0.3F},
            glm::vec3{0.3F + scale * 0.02F, 0.28F, 0.25F},
            rockMat
        );
        // Smaller protrusion
        m_renderer->DrawOrientedBox(
            glm::vec3{x + scale * 0.2F, z, scale * 0.4F},
            glm::vec3{scale * 0.25F, scale * 0.2F, scale * 0.2F},
            glm::vec3{rotation * 0.7F, -rotation * 0.3F, rotation * 0.5F},
            glm::vec3{0.32F, 0.3F, 0.27F},
            rockMat
        );
    };
    
    // Scattered rocks
    drawRock(-4.0F, 3.0F, 1.0F, 25.0F);
    drawRock(4.5F, -2.5F, 0.8F, 70.0F);
    drawRock(-3.0F, -3.5F, 1.2F, 140.0F);
    drawRock(3.5F, 4.0F, 0.7F, 210.0F);
    drawRock(-5.0F, -1.0F, 0.9F, 300.0F);
}

void LobbyScene::DrawLogs()
{
    if (!m_renderer) return;
    
    engine::render::MaterialParams logMat;
    logMat.roughness = 0.9F;
    
    // Fallen logs (seating for players concept)
    const auto drawFallenLog = [&](float x, float z, float length, float rotation, float tilt) {
        m_renderer->DrawOrientedBox(
            glm::vec3{x, 0.18F, z},
            glm::vec3{0.12F, length * 0.5F, 0.12F},
            glm::vec3{tilt, rotation, 0.0F},
            glm::vec3{0.32F, 0.22F, 0.1F},
            logMat
        );
    };
    
    drawFallenLog(-2.8F, 2.0F, 1.5F, 35.0F, 5.0F);
    drawFallenLog(3.0F, 1.8F, 1.8F, -30.0F, -3.0F);
}

void LobbyScene::DrawPlayerModels()
{
    if (!m_renderer) return;
    
    for (const auto& player : m_state.players)
    {
        if (!player.isConnected) continue;
        
        DrawPlayerBody(player);
    }
}

void LobbyScene::DrawPlayerBody(const LobbyPlayer& player)
{
    if (!m_renderer) return;
    
    const bool isKiller = player.selectedRole == "killer";
    const float bobOffset = std::sin(m_fireTime * 1.5F + player.worldPosition.x) * 0.02F;
    
    // Colors based on role
    glm::vec3 bodyColor = isKiller 
        ? glm::vec3{0.25F, 0.08F, 0.08F}   // Dark red for killer
        : glm::vec3{0.15F, 0.25F, 0.4F};   // Blue-gray for survivor
    
    glm::vec3 clothColor = isKiller
        ? glm::vec3{0.15F, 0.05F, 0.05F}
        : glm::vec3{0.12F, 0.2F, 0.3F};
    
    glm::vec3 skinColor{0.7F, 0.55F, 0.45F};
    
    engine::render::MaterialParams clothMat;
    clothMat.roughness = 0.85F;
    
    engine::render::MaterialParams skinMat;
    skinMat.roughness = 0.6F;
    
    const glm::vec3 basePos = player.worldPosition + glm::vec3{0.0F, bobOffset, 0.0F};
    const float rotation = player.rotation;
    
    // Legs
    const float legOffsetX = 0.1F;
    const glm::vec3 legPosL = basePos + glm::vec3{std::cos(rotation + 1.57F) * legOffsetX, 0.25F, std::sin(rotation + 1.57F) * legOffsetX};
    const glm::vec3 legPosR = basePos + glm::vec3{std::cos(rotation - 1.57F) * legOffsetX, 0.25F, std::sin(rotation - 1.57F) * legOffsetX};
    
    m_renderer->DrawOrientedBox(legPosL, glm::vec3{0.08F, 0.25F, 0.1F}, glm::vec3{0.0F, glm::degrees(rotation), 0.0F}, clothColor, clothMat);
    m_renderer->DrawOrientedBox(legPosR, glm::vec3{0.08F, 0.25F, 0.1F}, glm::vec3{0.0F, glm::degrees(rotation), 0.0F}, clothColor, clothMat);
    
    // Feet
    m_renderer->DrawBox(legPosL + glm::vec3{0.0F, -0.25F, 0.05F}, glm::vec3{0.08F, 0.04F, 0.12F}, glm::vec3{0.15F, 0.1F, 0.08F});
    m_renderer->DrawBox(legPosR + glm::vec3{0.0F, -0.25F, 0.05F}, glm::vec3{0.08F, 0.04F, 0.12F}, glm::vec3{0.15F, 0.1F, 0.08F});
    
    // Torso
    const glm::vec3 torsoPos = basePos + glm::vec3{0.0F, 0.65F, 0.0F};
    m_renderer->DrawOrientedBox(torsoPos, glm::vec3{0.18F, 0.25F, 0.12F}, glm::vec3{0.0F, glm::degrees(rotation), 0.0F}, bodyColor, clothMat);
    
    // Chest detail
    m_renderer->DrawOrientedBox(torsoPos + glm::vec3{0.0F, 0.05F, 0.0F}, glm::vec3{0.14F, 0.15F, 0.13F}, glm::vec3{0.0F, glm::degrees(rotation), 0.0F}, clothColor, clothMat);
    
    // Arms
    const float armOffsetX = 0.2F;
    const glm::vec3 armPosL = torsoPos + glm::vec3{std::cos(rotation + 1.57F) * armOffsetX, 0.0F, std::sin(rotation + 1.57F) * armOffsetX};
    const glm::vec3 armPosR = torsoPos + glm::vec3{std::cos(rotation - 1.57F) * armOffsetX, 0.0F, std::sin(rotation - 1.57F) * armOffsetX};
    
    m_renderer->DrawOrientedBox(armPosL, glm::vec3{0.06F, 0.22F, 0.06F}, glm::vec3{0.0F, glm::degrees(rotation), 0.0F}, clothColor, clothMat);
    m_renderer->DrawOrientedBox(armPosR, glm::vec3{0.06F, 0.22F, 0.06F}, glm::vec3{0.0F, glm::degrees(rotation), 0.0F}, clothColor, clothMat);
    
    // Hands
    m_renderer->DrawBox(armPosL + glm::vec3{0.0F, -0.15F, 0.0F}, glm::vec3{0.05F, 0.06F, 0.04F}, skinColor, skinMat);
    m_renderer->DrawBox(armPosR + glm::vec3{0.0F, -0.15F, 0.0F}, glm::vec3{0.05F, 0.06F, 0.04F}, skinColor, skinMat);
    
    // Neck
    const glm::vec3 neckPos = torsoPos + glm::vec3{0.0F, 0.3F, 0.0F};
    m_renderer->DrawOrientedBox(neckPos, glm::vec3{0.06F, 0.08F, 0.05F}, glm::vec3{0.0F, glm::degrees(rotation), 0.0F}, skinColor, skinMat);
    
    // Head
    const glm::vec3 headPos = neckPos + glm::vec3{0.0F, 0.15F, 0.0F};
    m_renderer->DrawOrientedBox(headPos, glm::vec3{0.1F, 0.12F, 0.1F}, glm::vec3{0.0F, glm::degrees(rotation), 0.0F}, skinColor, skinMat);
    
    // Hair
    const glm::vec3 hairColor = isKiller ? glm::vec3{0.05F, 0.02F, 0.02F} : glm::vec3{0.1F, 0.08F, 0.06F};
    m_renderer->DrawOrientedBox(headPos + glm::vec3{0.0F, 0.08F, -0.02F}, glm::vec3{0.11F, 0.08F, 0.1F}, glm::vec3{0.0F, glm::degrees(rotation), 0.0F}, hairColor, clothMat);
    
    // Killer-specific: add menacing hood/mask detail
    if (isKiller)
    {
        // Dark hood over shoulders
        m_renderer->DrawOrientedBox(torsoPos + glm::vec3{0.0F, 0.15F, -0.05F}, glm::vec3{0.22F, 0.12F, 0.1F}, glm::vec3{0.0F, glm::degrees(rotation), 0.0F}, glm::vec3{0.1F, 0.05F, 0.05F}, clothMat);
        
        // Mask covering face
        m_renderer->DrawOrientedBox(headPos + glm::vec3{0.0F, -0.02F, 0.05F}, glm::vec3{0.08F, 0.08F, 0.03F}, glm::vec3{0.0F, glm::degrees(rotation), 0.0F}, glm::vec3{0.12F, 0.08F, 0.06F}, clothMat);
    }
    
    // Survivor-specific: add backpack
    if (!isKiller)
    {
        m_renderer->DrawOrientedBox(torsoPos + glm::vec3{0.0F, 0.0F, -0.14F}, glm::vec3{0.15F, 0.2F, 0.06F}, glm::vec3{0.0F, glm::degrees(rotation), 0.0F}, glm::vec3{0.2F, 0.18F, 0.12F}, clothMat);
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

void LobbyScene::DrawRoleSelector(float x, float y, bool dropdownOnly)
{
    if (!m_ui) return;
    
    const auto& theme = m_ui->Theme();
    const float scale = m_ui->Scale();
    
    if (!dropdownOnly)
    {
        m_ui->DrawTextLabel(x, y - 20.0F * scale, "Select Role:", theme.colorText, 0.9F * scale);
    }
    
    const float buttonWidth = 150.0F * scale;
    const float buttonHeight = 35.0F * scale;
    
    std::string currentRole = "survivor";
    if (m_state.localPlayerIndex >= 0 && m_state.localPlayerIndex < static_cast<int>(m_state.players.size()))
    {
        currentRole = m_state.players[m_state.localPlayerIndex].selectedRole;
    }
    
    if (!dropdownOnly)
    {
        engine::ui::UiRect survivorRect{x, y, buttonWidth, buttonHeight};
        glm::vec4 color = (currentRole == "survivor") ? theme.colorAccent : theme.colorButton;
        color.a = 0.8F;
        m_ui->DrawRect(survivorRect, color);
        m_ui->DrawRectOutline(survivorRect, 2.0F, theme.colorPanelBorder);
        m_ui->DrawTextLabel(x + 10.0F * scale, y + 8.0F * scale, "Survivor", theme.colorText, 0.9F * scale);
    }
    
    if (!dropdownOnly)
    {
        engine::ui::UiRect killerRect{x + buttonWidth + 10.0F * scale, y, buttonWidth, buttonHeight};
        glm::vec4 color = (currentRole == "killer") ? theme.colorDanger : theme.colorButton;
        color.a = 0.8F;
        m_ui->DrawRect(killerRect, color);
        m_ui->DrawRectOutline(killerRect, 2.0F, theme.colorPanelBorder);
        m_ui->DrawTextLabel(x + buttonWidth + 20.0F * scale, y + 8.0F * scale, "Killer", theme.colorText, 0.9F * scale);
    }
}

void LobbyScene::DrawPerkSlots(float x, float y, bool dropdownOnly)
{
    if (!m_ui) return;
    
    const auto& theme = m_ui->Theme();
    const float scale = m_ui->Scale();
    
    if (!dropdownOnly)
    {
        m_ui->DrawTextLabel(x, y - 20.0F * scale, "Perk Loadout:", theme.colorText, 0.9F * scale);
    
        const float slotSize = 60.0F * scale;
        const float spacing = 8.0F * scale;
        
        for (int i = 0; i < 4; ++i)
        {
            const float slotX = x + i * (slotSize + spacing);
            
            const bool isSelected = m_selectedPerkSlot == i;
            const bool hasPerk = !m_state.selectedPerks[i].empty();
            
            engine::ui::UiRect slotRect{slotX, y, slotSize, slotSize};
            glm::vec4 slotColor = hasPerk ? theme.colorButton : theme.colorBackground;
            slotColor.a = isSelected ? 1.0F : 0.8F;
            m_ui->DrawRect(slotRect, slotColor);
            
            // Highlight selected slot
            glm::vec4 borderColor = isSelected ? theme.colorAccent : theme.colorPanelBorder;
            m_ui->DrawRectOutline(slotRect, isSelected ? 3.0F : 2.0F, borderColor);
            
            if (hasPerk)
            {
                // Find perk name
                std::string perkName = m_state.selectedPerks[i];
                for (std::size_t j = 0; j < m_availablePerkIds.size(); ++j)
                {
                    if (m_availablePerkIds[j] == m_state.selectedPerks[i] && j < m_availablePerkNames.size())
                    {
                        perkName = m_availablePerkNames[j];
                        break;
                    }
                }
                // Truncate name if too long
                if (perkName.length() > 8)
                {
                    perkName = perkName.substr(0, 7) + ".";
                }
                m_ui->DrawTextLabel(slotX + 5.0F * scale, y + slotSize / 2.0F - 5.0F * scale, perkName, theme.colorText, 0.6F * scale);
            }
            else
            {
                m_ui->DrawTextLabel(slotX + slotSize / 2.0F - 8.0F * scale, y + slotSize / 2.0F - 5.0F * scale, "+", theme.colorTextMuted, 1.4F * scale);
            }
        }
    }
    
    // If a slot is selected, show dropdown below (always render dropdown in dropdownOnly mode)
    if (m_selectedPerkSlot >= 0 && m_selectedPerkSlot < 4 && !m_availablePerkIds.empty())
    {
        const float slotSize = 60.0F * scale;
        const float spacing = 8.0F * scale;
        const float dropdownY = y + slotSize + 5.0F * scale;
        const float dropdownWidth = 160.0F * scale;
        const float slotXSelected = x + m_selectedPerkSlot * (slotSize + spacing);
        const float dropdownX = std::min(slotXSelected, m_ui->ScreenWidth() - dropdownWidth - 10.0F * scale);
        
        const float optionHeight = 22.0F * scale;
        const float numOptions = static_cast<float>(std::min(m_availablePerkIds.size() + 1, static_cast<std::size_t>(10)));
        const float dropdownHeight = numOptions * optionHeight + 10.0F * scale;
        
        // Draw dropdown panel
        engine::ui::UiRect dropdownRect{dropdownX, dropdownY, dropdownWidth, dropdownHeight};
        glm::vec4 bgColor = theme.colorPanel;
        bgColor.a = 0.98F;
        m_ui->DrawRect(dropdownRect, bgColor);
        m_ui->DrawRectOutline(dropdownRect, 2.0F, theme.colorAccent);
        
        // Scrollable content area
        const std::size_t startIndex = 0;
        const std::size_t endIndex = std::min(m_availablePerkIds.size() + 1, static_cast<std::size_t>(10));
        
        // "None" option
        {
            const float optY = dropdownY + 5.0F * scale;
            engine::ui::UiRect noneRect{dropdownX + 3.0F * scale, optY, dropdownWidth - 6.0F * scale, optionHeight};
            glm::vec4 noneColor = m_state.selectedPerks[m_selectedPerkSlot].empty() ? theme.colorAccent : theme.colorBackground;
            noneColor.a = 0.7F;
            m_ui->DrawRect(noneRect, noneColor);
            m_ui->DrawTextLabel(dropdownX + 10.0F * scale, optY + 4.0F * scale, "- None -", theme.colorText, 0.8F * scale);
        }
        
        // Perk options
        for (std::size_t j = startIndex; j < endIndex - 1 && j < m_availablePerkIds.size(); ++j)
        {
            const float optY = dropdownY + 5.0F * scale + static_cast<float>(j + 1) * optionHeight;
            engine::ui::UiRect optRect{dropdownX + 3.0F * scale, optY, dropdownWidth - 6.0F * scale, optionHeight};
            glm::vec4 optColor = m_state.selectedPerks[m_selectedPerkSlot] == m_availablePerkIds[j] ? theme.colorAccent : theme.colorBackground;
            optColor.a = 0.7F;
            m_ui->DrawRect(optRect, optColor);
            
            std::string perkName = j < m_availablePerkNames.size() ? m_availablePerkNames[j] : m_availablePerkIds[j];
            if (perkName.length() > 16)
            {
                perkName = perkName.substr(0, 15) + ".";
            }
            m_ui->DrawTextLabel(dropdownX + 10.0F * scale, optY + 4.0F * scale, perkName, theme.colorText, 0.8F * scale);
        }
    }
}

void LobbyScene::CloseAllDropdownsExcept(const std::string& keepOpen)
{
    if (keepOpen != "character") m_characterDropdownOpen = false;
    if (keepOpen != "item") m_itemDropdownOpen = false;
    if (keepOpen != "power") m_powerDropdownOpen = false;
    if (keepOpen != "addonA") m_addonADropdownOpen = false;
    if (keepOpen != "addonB") m_addonBDropdownOpen = false;
    if (keepOpen != "perk") m_selectedPerkSlot = -1;
}

void LobbyScene::HandleAddonDropdownClick(float x, float y, bool isAddonA)
{
    const float scale = m_ui->Scale();
    const float btnWidth = 130.0F * scale;
    const float btnHeight = 30.0F * scale;
    int& selectedIndex = isAddonA ? m_selectedAddonAIndex : m_selectedAddonBIndex;
    bool& dropdownOpen = isAddonA ? m_addonADropdownOpen : m_addonBDropdownOpen;
    
    const float dropdownY = y + btnHeight + 2.0F * scale;
    const float optionHeight = 24.0F * scale;
    
    // Check "None" option
    if (isMouseOver(x + 2.0F * scale, dropdownY + 4.0F * scale, btnWidth - 4.0F * scale, optionHeight - 2.0F * scale))
    {
        selectedIndex = 0;
        if (isAddonA)
        {
            m_state.selectedAddonA = "";
        }
        else
        {
            m_state.selectedAddonB = "";
        }

        const bool isSurvivor =
            m_state.localPlayerIndex >= 0 &&
            m_state.localPlayerIndex < static_cast<int>(m_state.players.size()) &&
            m_state.players[m_state.localPlayerIndex].selectedRole == "survivor";
        if (isSurvivor)
        {
            if (m_onItemChanged)
            {
                m_onItemChanged(m_state.selectedItem, m_state.selectedAddonA, m_state.selectedAddonB);
            }
        }
        else
        {
            if (m_onPowerChanged)
            {
                m_onPowerChanged(m_state.selectedPower, m_state.selectedAddonA, m_state.selectedAddonB);
            }
        }

        dropdownOpen = false;
        return;
    }
    
    // Check addon options
    for (std::size_t i = 0; i < m_addonIds.size() && i < 5; ++i)
    {
        const float optY = dropdownY + 4.0F * scale + static_cast<float>(i + 1) * optionHeight;
        if (isMouseOver(x + 2.0F * scale, optY, btnWidth - 4.0F * scale, optionHeight - 2.0F * scale))
        {
            selectedIndex = static_cast<int>(i + 1);
            if (isAddonA)
            {
                m_state.selectedAddonA = m_addonIds[i];
            }
            else
            {
                m_state.selectedAddonB = m_addonIds[i];
            }

            const bool isSurvivor =
                m_state.localPlayerIndex >= 0 &&
                m_state.localPlayerIndex < static_cast<int>(m_state.players.size()) &&
                m_state.players[m_state.localPlayerIndex].selectedRole == "survivor";
            if (isSurvivor)
            {
                if (m_onItemChanged)
                {
                    m_onItemChanged(m_state.selectedItem, m_state.selectedAddonA, m_state.selectedAddonB);
                }
            }
            else
            {
                if (m_onPowerChanged)
                {
                    m_onPowerChanged(m_state.selectedPower, m_state.selectedAddonA, m_state.selectedAddonB);
                }
            }

            dropdownOpen = false;
            return;
        }
    }
    
    dropdownOpen = false;
}

bool LobbyScene::isMouseOver(float x, float y, float w, float h) const
{
    const auto mousePos = m_input->MousePosition();
    return mousePos.x >= x && mousePos.x <= x + w && mousePos.y >= y && mousePos.y <= y + h;
}

void LobbyScene::DrawCharacterSelector(float x, float y, bool dropdownOnly)
{
    if (!m_ui) return;
    
    const auto& theme = m_ui->Theme();
    const float scale = m_ui->Scale();
    
    if (!dropdownOnly)
    {
        m_ui->DrawTextLabel(x, y - 20.0F * scale, "Character:", theme.colorText, 0.9F * scale);
    }
    
    // Determine which list to use based on role
    const bool isSurvivor = (m_state.localPlayerIndex >= 0 && 
                             m_state.localPlayerIndex < static_cast<int>(m_state.players.size()) &&
                             m_state.players[m_state.localPlayerIndex].selectedRole == "survivor");
    
    const auto& ids = isSurvivor ? m_survivorIds : m_killerIds;
    const auto& names = isSurvivor ? m_survivorNames : m_killerNames;
    
    if (ids.empty())
    {
        if (!dropdownOnly)
        {
            engine::ui::UiRect btnRect{x, y, 180.0F * scale, 35.0F * scale};
            m_ui->DrawRect(btnRect, theme.colorBackground);
            m_ui->DrawRectOutline(btnRect, 2.0F, theme.colorPanelBorder);
            m_ui->DrawTextLabel(x + 10.0F * scale, y + 8.0F * scale, "No characters", theme.colorTextMuted, 0.85F * scale);
        }
        return;
    }
    
    // Clamp index
    if (m_selectedCharacterIndex < 0 || m_selectedCharacterIndex >= static_cast<int>(ids.size()))
    {
        m_selectedCharacterIndex = 0;
    }
    
    const float btnWidth = 180.0F * scale;
    const float btnHeight = 35.0F * scale;
    
    if (!dropdownOnly)
    {
        // Dropdown button
        engine::ui::UiRect btnRect{x, y, btnWidth, btnHeight};
        glm::vec4 btnColor = m_characterDropdownOpen ? theme.colorAccent : theme.colorButton;
        btnColor.a = 0.9F;
        m_ui->DrawRect(btnRect, btnColor);
        m_ui->DrawRectOutline(btnRect, 2.0F, theme.colorPanelBorder);
        
        std::string displayName = static_cast<std::size_t>(m_selectedCharacterIndex) < names.size()
            ? names[static_cast<std::size_t>(m_selectedCharacterIndex)]
            : ids[static_cast<std::size_t>(m_selectedCharacterIndex)];
        if (displayName.length() > 18) displayName = displayName.substr(0, 17) + ".";
        m_ui->DrawTextLabel(x + 10.0F * scale, y + 8.0F * scale, displayName, theme.colorText, 0.85F * scale);
        m_ui->DrawTextLabel(x + btnWidth - 20.0F * scale, y + 8.0F * scale, "▼", theme.colorTextMuted, 0.7F * scale);
    }
    
    // Dropdown content (always render in dropdownOnly mode if open)
    if (m_characterDropdownOpen)
    {
        const float dropdownY = y + btnHeight + 2.0F * scale;
        const float optionHeight = 28.0F * scale;
        const float dropdownHeight = static_cast<float>(std::min(ids.size(), static_cast<std::size_t>(6))) * optionHeight + 10.0F * scale;
        
        engine::ui::UiRect dropdownRect{x, dropdownY, btnWidth, dropdownHeight};
        glm::vec4 bgColor = theme.colorPanel;
        bgColor.a = 0.98F;
        m_ui->DrawRect(dropdownRect, bgColor);
        m_ui->DrawRectOutline(dropdownRect, 2.0F, theme.colorAccent);
        
        for (std::size_t i = 0; i < ids.size() && i < 6; ++i)
        {
            const float optY = dropdownY + 5.0F * scale + static_cast<float>(i) * optionHeight;
            engine::ui::UiRect optRect{x + 3.0F * scale, optY, btnWidth - 6.0F * scale, optionHeight - 2.0F * scale};
            glm::vec4 optColor = static_cast<int>(i) == m_selectedCharacterIndex ? theme.colorAccent : theme.colorBackground;
            optColor.a = 0.7F;
            m_ui->DrawRect(optRect, optColor);
            
            std::string charName = i < names.size() ? names[i] : ids[i];
            if (charName.length() > 16) charName = charName.substr(0, 15) + ".";
            m_ui->DrawTextLabel(x + 10.0F * scale, optY + 5.0F * scale, charName, theme.colorText, 0.8F * scale);
        }
    }
}

void LobbyScene::DrawItemSelector(float x, float y, bool dropdownOnly)
{
    if (!m_ui) return;
    
    const auto& theme = m_ui->Theme();
    const float scale = m_ui->Scale();
    
    if (!dropdownOnly)
    {
        m_ui->DrawTextLabel(x, y - 20.0F * scale, "Bring Item:", theme.colorText, 0.9F * scale);
    }
    
    if (m_itemIds.empty())
    {
        if (!dropdownOnly)
        {
            engine::ui::UiRect btnRect{x, y, 180.0F * scale, 35.0F * scale};
            m_ui->DrawRect(btnRect, theme.colorBackground);
            m_ui->DrawRectOutline(btnRect, 2.0F, theme.colorPanelBorder);
            m_ui->DrawTextLabel(x + 10.0F * scale, y + 8.0F * scale, "No items", theme.colorTextMuted, 0.85F * scale);
        }
        return;
    }
    
    const float btnWidth = 180.0F * scale;
    const float btnHeight = 35.0F * scale;
    
    // Clamp index
    if (m_selectedItemIndex < 0 || m_selectedItemIndex > static_cast<int>(m_itemIds.size()))
    {
        m_selectedItemIndex = 0; // 0 = "None"
    }
    
    if (!dropdownOnly)
    {
        // Dropdown button
        engine::ui::UiRect btnRect{x, y, btnWidth, btnHeight};
        glm::vec4 btnColor = m_itemDropdownOpen ? theme.colorAccent : theme.colorButton;
        btnColor.a = 0.9F;
        m_ui->DrawRect(btnRect, btnColor);
        m_ui->DrawRectOutline(btnRect, 2.0F, theme.colorPanelBorder);
        
        std::string displayName = "None";
        if (m_selectedItemIndex > 0 && static_cast<std::size_t>(m_selectedItemIndex - 1) < m_itemNames.size())
        {
            displayName = m_itemNames[static_cast<std::size_t>(m_selectedItemIndex - 1)];
        }
        if (displayName.length() > 18) displayName = displayName.substr(0, 17) + ".";
        m_ui->DrawTextLabel(x + 10.0F * scale, y + 8.0F * scale, displayName, theme.colorText, 0.85F * scale);
        m_ui->DrawTextLabel(x + btnWidth - 20.0F * scale, y + 8.0F * scale, "▼", theme.colorTextMuted, 0.7F * scale);
    }
    
    if (m_itemDropdownOpen)
    {
        const float dropdownY = y + btnHeight + 2.0F * scale;
        const float optionHeight = 28.0F * scale;
        const std::size_t numOptions = std::min(m_itemIds.size() + 1, static_cast<std::size_t>(7));
        const float dropdownHeight = static_cast<float>(numOptions) * optionHeight + 10.0F * scale;
        
        engine::ui::UiRect dropdownRect{x, dropdownY, btnWidth, dropdownHeight};
        glm::vec4 bgColor = theme.colorPanel;
        bgColor.a = 0.98F;
        m_ui->DrawRect(dropdownRect, bgColor);
        m_ui->DrawRectOutline(dropdownRect, 2.0F, theme.colorAccent);
        
        // "None" option
        if (dropdownOnly)
        {
            const float optY = dropdownY + 5.0F * scale;
            engine::ui::UiRect optRect{x + 3.0F * scale, optY, btnWidth - 6.0F * scale, optionHeight - 2.0F * scale};
            glm::vec4 optColor = m_selectedItemIndex == 0 ? theme.colorAccent : theme.colorBackground;
            optColor.a = 0.7F;
            m_ui->DrawRect(optRect, optColor);
            m_ui->DrawTextLabel(x + 10.0F * scale, optY + 5.0F * scale, "- None -", theme.colorText, 0.8F * scale);
        }
        
        for (std::size_t i = 0; i < m_itemIds.size() && i < 6; ++i)
        {
            const float optY = dropdownY + 5.0F * scale + static_cast<float>(i + 1) * optionHeight;
            engine::ui::UiRect optRect{x + 3.0F * scale, optY, btnWidth - 6.0F * scale, optionHeight - 2.0F * scale};
            glm::vec4 optColor = static_cast<int>(i + 1) == m_selectedItemIndex ? theme.colorAccent : theme.colorBackground;
            optColor.a = 0.7F;
            m_ui->DrawRect(optRect, optColor);
            
            std::string itemName = i < m_itemNames.size() ? m_itemNames[i] : m_itemIds[i];
            if (itemName.length() > 16) itemName = itemName.substr(0, 15) + ".";
            m_ui->DrawTextLabel(x + 10.0F * scale, optY + 5.0F * scale, itemName, theme.colorText, 0.8F * scale);
        }
    }
}

void LobbyScene::DrawPowerSelector(float x, float y, bool dropdownOnly)
{
    if (!m_ui) return;
    
    const auto& theme = m_ui->Theme();
    const float scale = m_ui->Scale();
    
    if (!dropdownOnly)
    {
        m_ui->DrawTextLabel(x, y - 20.0F * scale, "Killer Power:", theme.colorText, 0.9F * scale);
    }
    
    if (m_powerIds.empty())
    {
        if (!dropdownOnly)
        {
            engine::ui::UiRect btnRect{x, y, 180.0F * scale, 35.0F * scale};
            m_ui->DrawRect(btnRect, theme.colorBackground);
            m_ui->DrawRectOutline(btnRect, 2.0F, theme.colorPanelBorder);
            m_ui->DrawTextLabel(x + 10.0F * scale, y + 8.0F * scale, "No powers", theme.colorTextMuted, 0.85F * scale);
        }
        return;
    }
    
    const float btnWidth = 180.0F * scale;
    const float btnHeight = 35.0F * scale;
    
    // Clamp index
    if (m_selectedPowerIndex < 0 || m_selectedPowerIndex >= static_cast<int>(m_powerIds.size()))
    {
        m_selectedPowerIndex = 0;
    }
    
    if (!dropdownOnly)
    {
        // Dropdown button
        engine::ui::UiRect btnRect{x, y, btnWidth, btnHeight};
        glm::vec4 btnColor = m_powerDropdownOpen ? theme.colorDanger : theme.colorButton;
        btnColor.a = 0.9F;
        m_ui->DrawRect(btnRect, btnColor);
        m_ui->DrawRectOutline(btnRect, 2.0F, theme.colorPanelBorder);
        
        std::string displayName = static_cast<std::size_t>(m_selectedPowerIndex) < m_powerNames.size()
            ? m_powerNames[static_cast<std::size_t>(m_selectedPowerIndex)]
            : m_powerIds[static_cast<std::size_t>(m_selectedPowerIndex)];
        if (displayName.length() > 18) displayName = displayName.substr(0, 17) + ".";
        m_ui->DrawTextLabel(x + 10.0F * scale, y + 8.0F * scale, displayName, theme.colorText, 0.85F * scale);
        m_ui->DrawTextLabel(x + btnWidth - 20.0F * scale, y + 8.0F * scale, "▼", theme.colorTextMuted, 0.7F * scale);
    }
    
    if (m_powerDropdownOpen)
    {
        const float dropdownY = y + btnHeight + 2.0F * scale;
        const float optionHeight = 28.0F * scale;
        const float dropdownHeight = static_cast<float>(std::min(m_powerIds.size(), static_cast<std::size_t>(5))) * optionHeight + 10.0F * scale;
        
        engine::ui::UiRect dropdownRect{x, dropdownY, btnWidth, dropdownHeight};
        glm::vec4 bgColor = theme.colorPanel;
        bgColor.a = 0.98F;
        m_ui->DrawRect(dropdownRect, bgColor);
        m_ui->DrawRectOutline(dropdownRect, 2.0F, theme.colorDanger);
        
        for (std::size_t i = 0; i < m_powerIds.size() && i < 5; ++i)
        {
            const float optY = dropdownY + 5.0F * scale + static_cast<float>(i) * optionHeight;
            engine::ui::UiRect optRect{x + 3.0F * scale, optY, btnWidth - 6.0F * scale, optionHeight - 2.0F * scale};
            glm::vec4 optColor = static_cast<int>(i) == m_selectedPowerIndex ? theme.colorDanger : theme.colorBackground;
            optColor.a = 0.7F;
            m_ui->DrawRect(optRect, optColor);
            
            std::string powerName = i < m_powerNames.size() ? m_powerNames[i] : m_powerIds[i];
            if (powerName.length() > 16) powerName = powerName.substr(0, 15) + ".";
            m_ui->DrawTextLabel(x + 10.0F * scale, optY + 5.0F * scale, powerName, theme.colorText, 0.8F * scale);
        }
    }
}

void LobbyScene::DrawAddonSelector(float x, float y, bool isAddonA, bool dropdownOnly)
{
    if (!m_ui) return;
    
    const auto& theme = m_ui->Theme();
    const float scale = m_ui->Scale();
    
    if (!dropdownOnly)
    {
        std::string label = isAddonA ? "Addon A:" : "Addon B:";
        m_ui->DrawTextLabel(x, y - 20.0F * scale, label, theme.colorText, 0.9F * scale);
    }
    
    if (m_addonIds.empty())
    {
        if (!dropdownOnly)
        {
            engine::ui::UiRect btnRect{x, y, 130.0F * scale, 30.0F * scale};
            m_ui->DrawRect(btnRect, theme.colorBackground);
            m_ui->DrawRectOutline(btnRect, 2.0F, theme.colorPanelBorder);
            m_ui->DrawTextLabel(x + 10.0F * scale, y + 6.0F * scale, "No addons", theme.colorTextMuted, 0.8F * scale);
        }
        return;
    }
    
    const float btnWidth = 130.0F * scale;
    const float btnHeight = 30.0F * scale;
    
    int& selectedIndex = isAddonA ? m_selectedAddonAIndex : m_selectedAddonBIndex;
    bool& dropdownOpen = isAddonA ? m_addonADropdownOpen : m_addonBDropdownOpen;
    
    // Clamp index
    if (selectedIndex < 0 || selectedIndex > static_cast<int>(m_addonIds.size()))
    {
        selectedIndex = 0;
    }
    
    if (!dropdownOnly)
    {
        // Dropdown button
        engine::ui::UiRect btnRect{x, y, btnWidth, btnHeight};
        glm::vec4 btnColor = dropdownOpen ? theme.colorAccent : theme.colorButton;
        btnColor.a = 0.9F;
        m_ui->DrawRect(btnRect, btnColor);
        m_ui->DrawRectOutline(btnRect, 2.0F, theme.colorPanelBorder);
        
        std::string displayName = "None";
        if (selectedIndex > 0 && static_cast<std::size_t>(selectedIndex - 1) < m_addonNames.size())
        {
            displayName = m_addonNames[static_cast<std::size_t>(selectedIndex - 1)];
        }
        if (displayName.length() > 14) displayName = displayName.substr(0, 13) + ".";
        m_ui->DrawTextLabel(x + 8.0F * scale, y + 6.0F * scale, displayName, theme.colorText, 0.8F * scale);
        m_ui->DrawTextLabel(x + btnWidth - 16.0F * scale, y + 6.0F * scale, "▼", theme.colorTextMuted, 0.6F * scale);
    }
    
    if (dropdownOpen)
    {
        const float dropdownY = y + btnHeight + 2.0F * scale;
        const float optionHeight = 24.0F * scale;
        const std::size_t numOptions = std::min(m_addonIds.size() + 1, static_cast<std::size_t>(6));
        const float dropdownHeight = static_cast<float>(numOptions) * optionHeight + 8.0F * scale;
        
        engine::ui::UiRect dropdownRect{x, dropdownY, btnWidth, dropdownHeight};
        glm::vec4 bgColor = theme.colorPanel;
        bgColor.a = 0.98F;
        m_ui->DrawRect(dropdownRect, bgColor);
        m_ui->DrawRectOutline(dropdownRect, 2.0F, theme.colorAccent);
        
        // "None" option
        {
            const float optY = dropdownY + 4.0F * scale;
            engine::ui::UiRect optRect{x + 2.0F * scale, optY, btnWidth - 4.0F * scale, optionHeight - 2.0F * scale};
            glm::vec4 optColor = selectedIndex == 0 ? theme.colorAccent : theme.colorBackground;
            optColor.a = 0.7F;
            m_ui->DrawRect(optRect, optColor);
            m_ui->DrawTextLabel(x + 8.0F * scale, optY + 4.0F * scale, "- None -", theme.colorText, 0.75F * scale);
        }
        
        for (std::size_t i = 0; i < m_addonIds.size() && i < 5; ++i)
        {
            const float optY = dropdownY + 4.0F * scale + static_cast<float>(i + 1) * optionHeight;
            engine::ui::UiRect optRect{x + 2.0F * scale, optY, btnWidth - 4.0F * scale, optionHeight - 2.0F * scale};
            glm::vec4 optColor = static_cast<int>(i + 1) == selectedIndex ? theme.colorAccent : theme.colorBackground;
            optColor.a = 0.7F;
            m_ui->DrawRect(optRect, optColor);
            
            std::string addonName = i < m_addonNames.size() ? m_addonNames[i] : m_addonIds[i];
            if (addonName.length() > 14) addonName = addonName.substr(0, 13) + ".";
            m_ui->DrawTextLabel(x + 8.0F * scale, optY + 4.0F * scale, addonName, theme.colorText, 0.75F * scale);
        }
    }
}

}
