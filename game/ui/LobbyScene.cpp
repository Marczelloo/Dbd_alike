#include "LobbyScene.hpp"

#include <cmath>
#include <algorithm>
#include <random>

#include <glm/gtc/matrix_transform.hpp>

#include "engine/ui/UiSystem.hpp"
#include "engine/render/Renderer.hpp"
#include "engine/platform/Input.hpp"
#include "game/gameplay/GameplaySystems.hpp"

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
    const bool mousePressed = m_input->IsMousePressed(0);
    
    const float buttonHeight = 50.0F * scale;
    const float buttonY = screenHeight - buttonHeight - 30.0F * scale;
    
    // Calculate button positions (same as render)
    const float btnWidth = 180.0F * scale;
    const float btnHeight = 50.0F * scale;
    const float forceBtnWidth = 140.0F * scale;
    const float totalBtnWidth = btnWidth + 10.0F * scale + forceBtnWidth;
    const float btnStartX = (screenWidth - totalBtnWidth) / 2.0F;
    
    if (isMouseOver(btnStartX, buttonY, btnWidth, btnHeight) && mousePressed)
    {
        const bool isCurrentlyReady = (m_state.localPlayerIndex >= 0 && 
                                       m_state.localPlayerIndex < static_cast<int>(m_state.players.size()) &&
                                       m_state.players[m_state.localPlayerIndex].isReady);
        // Cancel countdown when a player unreadies
        if (isCurrentlyReady && m_state.countdownActive)
        {
            CancelCountdown();
        }
        SetLocalPlayerReady(!isCurrentlyReady);
    }
    
    // Force Start button (available to everyone)
    if (!m_state.countdownActive)
    {
        const float forceBtnX = btnStartX + btnWidth + 10.0F * scale;
        
        if (isMouseOver(forceBtnX, buttonY, forceBtnWidth, btnHeight) && mousePressed)
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

    // Leave Lobby button (bottom-left corner)
    const float leaveBtnWidth = 140.0F * scale;
    const float leaveBtnHeight = 40.0F * scale;
    const float leaveBtnX = 20.0F * scale;
    const float leaveBtnY = screenHeight - leaveBtnHeight - 30.0F * scale;
    
    if (mousePressed && isMouseOver(leaveBtnX, leaveBtnY, leaveBtnWidth, leaveBtnHeight))
    {
        ExitLobby();
        if (m_onLeaveLobby)
        {
            m_onLeaveLobby();
        }
        return;
    }

    // Loadout menu toggle (open/close)
    const float menuBtnWidth = 220.0F * scale;
    const float menuBtnHeight = 42.0F * scale;
    const float menuBtnX = screenWidth - menuBtnWidth - 20.0F * scale;
    const float menuBtnY = 20.0F * scale;
    if (mousePressed && isMouseOver(menuBtnX, menuBtnY, menuBtnWidth, menuBtnHeight))
    {
        m_loadoutMenuOpen = !m_loadoutMenuOpen;
        if (!m_loadoutMenuOpen)
        {
            CloseAllDropdownsExcept("");
        }
    }
    
    if (m_loadoutMenuOpen && m_state.localPlayerIndex >= 0 && m_state.localPlayerIndex < static_cast<int>(m_state.players.size()))
    {
        const float panelWidth = 560.0F * scale;
        const float panelHeight = std::min(640.0F * scale, static_cast<float>(screenHeight) - 140.0F * scale);
        const float panelX = screenWidth - panelWidth - 20.0F * scale;
        const float panelY = 80.0F * scale;
        
        const bool isSurvivor = m_state.players[m_state.localPlayerIndex].selectedRole == "survivor";
        
        // Character selector dropdown (paginated)
        const float charY = panelY + 105.0F * scale;
        const float btnWidth = 260.0F * scale;
        const float btnHeight = 35.0F * scale;
        
        if (mousePressed)
        {
            if (isMouseOver(panelX + 20.0F * scale, charY, btnWidth, btnHeight))
            {
                m_characterDropdownOpen = !m_characterDropdownOpen;
                CloseAllDropdownsExcept("character");
                m_characterDropdownStart = 0;
            }
            else if (m_characterDropdownOpen)
            {
                const auto& ids = isSurvivor ? m_survivorIds : m_killerIds;
                const float dropdownY = charY + btnHeight + 2.0F * scale;
                const float optionHeight = 28.0F * scale;
                const int totalOptions = static_cast<int>(ids.size());
                const int visibleCount = std::min(kDropdownPageSize, totalOptions);
                m_characterDropdownStart = std::clamp(m_characterDropdownStart, 0, std::max(0, totalOptions - visibleCount));
                const float listY = dropdownY + 5.0F * scale;
                const float navY = listY + static_cast<float>(visibleCount) * optionHeight + 4.0F * scale;
                const float navW = (btnWidth - 10.0F * scale) * 0.5F;

                if (isMouseOver(panelX + 20.0F * scale + 3.0F * scale, navY, navW - 3.0F * scale, 24.0F * scale))
                {
                    m_characterDropdownStart = std::max(0, m_characterDropdownStart - 1);
                }
                else if (isMouseOver(panelX + 20.0F * scale + navW + 2.0F * scale, navY, navW - 3.0F * scale, 24.0F * scale))
                {
                    m_characterDropdownStart = std::min(std::max(0, totalOptions - visibleCount), m_characterDropdownStart + 1);
                }
                else
                {
                    for (int i = 0; i < visibleCount; ++i)
                    {
                        const int optionIndex = m_characterDropdownStart + i;
                        const float optY = listY + static_cast<float>(i) * optionHeight;
                        if (isMouseOver(panelX + 20.0F * scale + 3.0F * scale, optY, btnWidth - 6.0F * scale, optionHeight - 2.0F * scale))
                        {
                            m_selectedCharacterIndex = optionIndex;
                            SetLocalPlayerCharacter(ids[static_cast<std::size_t>(optionIndex)]);
                            m_characterDropdownOpen = false;
                            break;
                        }
                    }
                }
            }
        }
        
        // Item/Power selector (paginated)
        const float itemY = panelY + 165.0F * scale;
        if (mousePressed)
        {
            if (isSurvivor)
            {
                // Item dropdown
                if (isMouseOver(panelX + 20.0F * scale, itemY, btnWidth, btnHeight))
                {
                    m_itemDropdownOpen = !m_itemDropdownOpen;
                    CloseAllDropdownsExcept("item");
                    m_itemDropdownStart = 0;
                }
                else if (m_itemDropdownOpen)
                {
                    const float dropdownY = itemY + btnHeight + 2.0F * scale;
                    const float optionHeight = 28.0F * scale;
                    const int totalOptions = static_cast<int>(m_itemIds.size()) + 1; // +None
                    const int visibleCount = std::min(kDropdownPageSize, totalOptions);
                    m_itemDropdownStart = std::clamp(m_itemDropdownStart, 0, std::max(0, totalOptions - visibleCount));
                    const float listY = dropdownY + 5.0F * scale;
                    const float navY = listY + static_cast<float>(visibleCount) * optionHeight + 4.0F * scale;
                    const float navW = (btnWidth - 10.0F * scale) * 0.5F;

                    if (isMouseOver(panelX + 20.0F * scale + 3.0F * scale, navY, navW - 3.0F * scale, 24.0F * scale))
                    {
                        m_itemDropdownStart = std::max(0, m_itemDropdownStart - 1);
                    }
                    else if (isMouseOver(panelX + 20.0F * scale + navW + 2.0F * scale, navY, navW - 3.0F * scale, 24.0F * scale))
                    {
                        m_itemDropdownStart = std::min(std::max(0, totalOptions - visibleCount), m_itemDropdownStart + 1);
                    }
                    else
                    {
                        for (int i = 0; i < visibleCount; ++i)
                        {
                            const int optionIndex = m_itemDropdownStart + i;
                            const float optY = listY + static_cast<float>(i) * optionHeight;
                            if (isMouseOver(panelX + 20.0F * scale + 3.0F * scale, optY, btnWidth - 6.0F * scale, optionHeight - 2.0F * scale))
                            {
                                m_selectedItemIndex = optionIndex;
                                if (optionIndex == 0)
                                {
                                    SetLocalPlayerItem("", "", "");
                                }
                                else
                                {
                                    SetLocalPlayerItem(m_itemIds[static_cast<std::size_t>(optionIndex - 1)], m_state.selectedAddonA, m_state.selectedAddonB);
                                }
                                m_itemDropdownOpen = false;
                                break;
                            }
                        }
                    }
                }
            }
            else
            {
                // Power dropdown
                if (isMouseOver(panelX + 20.0F * scale, itemY, btnWidth, btnHeight))
                {
                    m_powerDropdownOpen = !m_powerDropdownOpen;
                    CloseAllDropdownsExcept("power");
                    m_powerDropdownStart = 0;
                }
                else if (m_powerDropdownOpen)
                {
                    const float dropdownY = itemY + btnHeight + 2.0F * scale;
                    const float optionHeight = 28.0F * scale;
                    const int totalOptions = static_cast<int>(m_powerIds.size());
                    const int visibleCount = std::min(kDropdownPageSize, totalOptions);
                    m_powerDropdownStart = std::clamp(m_powerDropdownStart, 0, std::max(0, totalOptions - visibleCount));
                    const float listY = dropdownY + 5.0F * scale;
                    const float navY = listY + static_cast<float>(visibleCount) * optionHeight + 4.0F * scale;
                    const float navW = (btnWidth - 10.0F * scale) * 0.5F;

                    if (isMouseOver(panelX + 20.0F * scale + 3.0F * scale, navY, navW - 3.0F * scale, 24.0F * scale))
                    {
                        m_powerDropdownStart = std::max(0, m_powerDropdownStart - 1);
                    }
                    else if (isMouseOver(panelX + 20.0F * scale + navW + 2.0F * scale, navY, navW - 3.0F * scale, 24.0F * scale))
                    {
                        m_powerDropdownStart = std::min(std::max(0, totalOptions - visibleCount), m_powerDropdownStart + 1);
                    }
                    else
                    {
                        for (int i = 0; i < visibleCount; ++i)
                        {
                            const int optionIndex = m_powerDropdownStart + i;
                            const float optY = listY + static_cast<float>(i) * optionHeight;
                            if (isMouseOver(panelX + 20.0F * scale + 3.0F * scale, optY, btnWidth - 6.0F * scale, optionHeight - 2.0F * scale))
                            {
                                m_selectedPowerIndex = optionIndex;
                                SetLocalPlayerPower(m_powerIds[static_cast<std::size_t>(optionIndex)], m_state.selectedAddonA, m_state.selectedAddonB);
                                m_powerDropdownOpen = false;
                                break;
                            }
                        }
                    }
                }
            }
        }
        
        // Addon selectors
        const float addonY = panelY + 225.0F * scale;
        const float addonBtnWidth = 130.0F * scale;
        const float addonBtnHeight = 30.0F * scale;
        
        if (mousePressed)
        {
            // Addon A
            if (isMouseOver(panelX + 20.0F * scale, addonY, addonBtnWidth, addonBtnHeight))
            {
                m_addonADropdownOpen = !m_addonADropdownOpen;
                CloseAllDropdownsExcept("addonA");
                m_addonADropdownStart = 0;
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
                m_addonBDropdownStart = 0;
            }
            else if (m_addonBDropdownOpen)
            {
                HandleAddonDropdownClick(panelX + 170.0F * scale, addonY, false);
            }
        }
        
        // Perk slot positions (updated position)
        const float perkY = panelY + 295.0F * scale;
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
            const int totalOptions = static_cast<int>(m_availablePerkIds.size()) + 1;
            const int visibleCount = std::min(kDropdownPageSize, totalOptions);
            m_perkDropdownStart = std::clamp(m_perkDropdownStart, 0, std::max(0, totalOptions - visibleCount));
        const float dropdownHeight = static_cast<float>(visibleCount) * optionHeight + 12.0F * scale;
            
            if (mousePressed)
            {
                // Check if clicked in dropdown area
                if (isMouseOver(dropdownX, dropdownY, dropdownWidth, dropdownHeight))
                {
                    const float listY = dropdownY + 5.0F * scale;
                    const float navY = listY + static_cast<float>(visibleCount) * optionHeight + 4.0F * scale;
                    const float navW = (dropdownWidth - 10.0F * scale) * 0.5F;

                    if (isMouseOver(dropdownX + 3.0F * scale, navY, navW - 3.0F * scale, 24.0F * scale))
                    {
                        m_perkDropdownStart = std::max(0, m_perkDropdownStart - 1);
                    }
                    else if (isMouseOver(dropdownX + navW + 2.0F * scale, navY, navW - 3.0F * scale, 24.0F * scale))
                    {
                        m_perkDropdownStart = std::min(std::max(0, totalOptions - visibleCount), m_perkDropdownStart + 1);
                    }
                    else
                    {
                        for (int i = 0; i < visibleCount; ++i)
                        {
                            const int optionIndex = m_perkDropdownStart + i;
                            const float optY = listY + static_cast<float>(i) * optionHeight;
                            if (isMouseOver(dropdownX + 3.0F * scale, optY, dropdownWidth - 6.0F * scale, optionHeight))
                            {
                                if (optionIndex == 0)
                                {
                                    m_state.selectedPerks[m_selectedPerkSlot] = "";
                                }
                                else
                                {
                                    m_state.selectedPerks[m_selectedPerkSlot] = m_availablePerkIds[static_cast<std::size_t>(optionIndex - 1)];
                                }
                                if (m_onPerksChanged) m_onPerksChanged(m_state.selectedPerks);
                                m_selectedPerkSlot = -1;
                                break;
                            }
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
                if (isMouseOver(slotX, perkY, slotSize, slotSize) && mousePressed)
                {
                    m_selectedPerkSlot = i;
                    m_perkDropdownStart = 0;
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
    m_lobbyMeshCache.clear();
    m_showLobbyFull = false;
    m_lobbyFullMessage.clear();
    m_loadoutMenuOpen = false;
    m_characterDropdownStart = 0;
    m_itemDropdownStart = 0;
    m_powerDropdownStart = 0;
    m_addonADropdownStart = 0;
    m_addonBDropdownStart = 0;
    m_perkDropdownStart = 0;
    
    if (m_gameplay)
    {
        m_gameplay->PreloadCharacterMeshes();
    }
    
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
    const bool wasActive = m_state.countdownActive;
    m_state.countdownTimer = seconds;
    m_state.countdownActive = (seconds > 0.0F);
    
    if (m_state.countdownActive && !wasActive && m_onCountdownStarted)
    {
        m_onCountdownStarted(seconds);
    }
}

void LobbyScene::CancelCountdown()
{
    const bool wasActive = m_state.countdownActive;
    m_state.countdownActive = false;
    m_state.countdownTimer = -1.0F;
    
    if (wasActive && m_onCountdownCancelled)
    {
        m_onCountdownCancelled();
    }
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
    const float scale = m_ui->Scale();
    const auto& theme = m_ui->Theme();
    const int screenWidth = m_ui->ScreenWidth();
    
    RenderPlayerSlots();

    const float menuBtnWidth = 220.0F * scale;
    const float menuBtnHeight = 42.0F * scale;
    const float menuBtnX = static_cast<float>(screenWidth) - menuBtnWidth - 20.0F * scale;
    const float menuBtnY = 20.0F * scale;
    engine::ui::UiRect menuBtnRect{menuBtnX, menuBtnY, menuBtnWidth, menuBtnHeight};
    glm::vec4 menuColor = m_loadoutMenuOpen ? theme.colorAccent : theme.colorButton;
    menuColor.a = 0.9F;
    m_ui->DrawRect(menuBtnRect, menuColor);
    m_ui->DrawRectOutline(menuBtnRect, 2.0F, theme.colorPanelBorder);
    m_ui->DrawTextLabel(menuBtnX + 14.0F * scale, menuBtnY + 11.0F * scale,
                        m_loadoutMenuOpen ? "CLOSE LOADOUT" : "OPEN LOADOUT", theme.colorText, 0.95F * scale);
    
    if (m_loadoutMenuOpen && m_state.localPlayerIndex >= 0 && m_state.localPlayerIndex < static_cast<int>(m_state.players.size()))
    {
        RenderPlayerDetails(m_state.localPlayerIndex);
    }
    
    RenderReadyButton();
    RenderLeaveButton();
    RenderMatchSettings();
    
    if (m_state.countdownActive)
    {
        RenderCountdown();
    }
    
    if (m_showLobbyFull)
    {
        RenderLobbyFullOverlay();
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
    const float gap = 10.0F * scale;
    const float labelHeight = 22.0F * scale;
    const float startY = 20.0F * scale + labelHeight;
    
    // --- KILLER section (1 slot, left side) ---
    const float killerSectionWidth = slotWidth;
    const float survivorSectionWidth = kMaxSurvivors * slotWidth + (kMaxSurvivors - 1) * gap;
    const float separatorGap = 30.0F * scale;
    const float totalWidth = killerSectionWidth + separatorGap + survivorSectionWidth;
    const float sectionStartX = (screenWidth - totalWidth) / 2.0F;
    
    // "KILLER" label
    m_ui->DrawTextLabel(sectionStartX + slotWidth / 2.0F - 25.0F * scale, startY - labelHeight,
                        "KILLER", glm::vec4{0.9F, 0.2F, 0.2F, 1.0F}, 0.9F * scale);
    
    // Find killer player index
    int killerIndex = -1;
    for (int i = 0; i < static_cast<int>(m_state.players.size()); ++i)
    {
        if (m_state.players[i].selectedRole == "killer")
        {
            killerIndex = i;
            break;
        }
    }
    
    DrawPlayerSlot(sectionStartX, startY, slotWidth, slotHeight, killerIndex, true);
    
    // --- SURVIVORS section (kMaxSurvivors slots, right side) ---
    const float survivorStartX = sectionStartX + killerSectionWidth + separatorGap;
    
    // "SURVIVORS" label
    m_ui->DrawTextLabel(survivorStartX + survivorSectionWidth / 2.0F - 45.0F * scale, startY - labelHeight,
                        "SURVIVORS", glm::vec4{0.3F, 0.6F, 1.0F, 1.0F}, 0.9F * scale);
    
    // Gather survivor indices
    std::vector<int> survivorIndices;
    for (int i = 0; i < static_cast<int>(m_state.players.size()); ++i)
    {
        if (m_state.players[i].selectedRole != "killer")
        {
            survivorIndices.push_back(i);
        }
    }
    
    for (int s = 0; s < kMaxSurvivors; ++s)
    {
        const float sx = survivorStartX + s * (slotWidth + gap);
        const int playerIdx = s < static_cast<int>(survivorIndices.size()) ? survivorIndices[s] : -1;
        DrawPlayerSlot(sx, startY, slotWidth, slotHeight, playerIdx, false);
    }
}

void LobbyScene::RenderPlayerDetails(int playerIndex)
{
    if (!m_ui || playerIndex < 0) return;
    
    const float scale = m_ui->Scale();
    const int screenWidth = m_ui->ScreenWidth();
    const int screenHeight = m_ui->ScreenHeight();
    
    const float panelWidth = 560.0F * scale;
    const float panelHeight = std::min(640.0F * scale, static_cast<float>(screenHeight) - 140.0F * scale);
    const float panelX = screenWidth - panelWidth - 20.0F * scale;
    const float panelY = 80.0F * scale;
    
    DrawUIPanel(panelX, panelY, panelWidth, panelHeight);
    
    const bool isSurvivor = (m_state.localPlayerIndex >= 0 && 
                             m_state.localPlayerIndex < static_cast<int>(m_state.players.size()) &&
                             m_state.players[m_state.localPlayerIndex].selectedRole == "survivor");
    
    const auto& localPlayer = m_state.players[m_state.localPlayerIndex];
    m_ui->DrawTextLabel(panelX + 20.0F * scale, panelY + 20.0F * scale, "LOADOUT", m_ui->Theme().colorText, 1.2F * scale);
    m_ui->DrawTextLabel(panelX + 20.0F * scale, panelY + 48.0F * scale,
                        "Role: " + localPlayer.selectedRole + " (set before lobby)",
                        m_ui->Theme().colorTextMuted, 0.8F * scale);
    
    // First pass: draw all static elements (buttons, slots)
    DrawCharacterSelector(panelX + 20.0F * scale, panelY + 105.0F * scale, false);
    
    if (isSurvivor)
    {
        DrawItemSelector(panelX + 20.0F * scale, panelY + 165.0F * scale, false);
        DrawAddonSelector(panelX + 20.0F * scale, panelY + 225.0F * scale, true, false);
        DrawAddonSelector(panelX + 170.0F * scale, panelY + 225.0F * scale, false, false);
    }
    else
    {
        DrawPowerSelector(panelX + 20.0F * scale, panelY + 165.0F * scale, false);
        DrawAddonSelector(panelX + 20.0F * scale, panelY + 225.0F * scale, true, false);
        DrawAddonSelector(panelX + 170.0F * scale, panelY + 225.0F * scale, false, false);
    }
    
    DrawPerkSlots(panelX + 20.0F * scale, panelY + 295.0F * scale, false);
    
    // Second pass: draw all dropdowns on top (higher z-index)
    DrawCharacterSelector(panelX + 20.0F * scale, panelY + 105.0F * scale, true);
    
    if (isSurvivor)
    {
        DrawItemSelector(panelX + 20.0F * scale, panelY + 165.0F * scale, true);
        DrawAddonSelector(panelX + 20.0F * scale, panelY + 225.0F * scale, true, true);
        DrawAddonSelector(panelX + 170.0F * scale, panelY + 225.0F * scale, false, true);
    }
    else
    {
        DrawPowerSelector(panelX + 20.0F * scale, panelY + 165.0F * scale, true);
        DrawAddonSelector(panelX + 20.0F * scale, panelY + 225.0F * scale, true, true);
        DrawAddonSelector(panelX + 170.0F * scale, panelY + 225.0F * scale, false, true);
    }
    
    DrawPerkSlots(panelX + 20.0F * scale, panelY + 295.0F * scale, true);
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

void LobbyScene::RenderLeaveButton()
{
    if (!m_ui) return;
    
    const float scale = m_ui->Scale();
    const auto& theme = m_ui->Theme();
    const int screenHeight = m_ui->ScreenHeight();
    
    const float buttonWidth = 140.0F * scale;
    const float buttonHeight = 40.0F * scale;
    const float buttonX = 20.0F * scale;
    const float buttonY = screenHeight - buttonHeight - 30.0F * scale;
    
    engine::ui::UiRect buttonRect{buttonX, buttonY, buttonWidth, buttonHeight};
    glm::vec4 buttonColor = theme.colorButton;
    buttonColor.a = 0.9F;
    m_ui->DrawRect(buttonRect, buttonColor);
    m_ui->DrawRectOutline(buttonRect, 2.0F, theme.colorPanelBorder);
    
    m_ui->DrawTextLabel(buttonX + 12.0F * scale, buttonY + 11.0F * scale, "LEAVE LOBBY", theme.colorText, 0.9F * scale);
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
    DrawFences();
    DrawGraves();
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
        
        // Try GLB model first, fallback to procedural body
        DrawPlayerGlbModel(player);
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

void LobbyScene::DrawPlayerSlot(float x, float y, float width, float height, int playerIndex, bool isKillerSlot)
{
    if (!m_ui) return;
    
    const auto& theme = m_ui->Theme();
    const float scale = m_ui->Scale();
    
    const bool hasPlayer = playerIndex >= 0 && playerIndex < static_cast<int>(m_state.players.size());
    const bool isLocal = hasPlayer && playerIndex == m_state.localPlayerIndex;
    
    engine::ui::UiRect slotRect{x, y, width, height};
    
    // Killer slot gets a dark red background; survivor gets default
    glm::vec4 slotColor = isKillerSlot
        ? glm::vec4{0.5F, 0.1F, 0.1F, 0.8F}
        : glm::vec4{theme.colorBackground.r, theme.colorBackground.g, theme.colorBackground.b, 0.8F};
    m_ui->DrawRect(slotRect, slotColor);
    
    // Border: killer=red, local=accent, default=panel border
    glm::vec4 borderColor = isKillerSlot
        ? glm::vec4{0.8F, 0.2F, 0.2F, 0.7F}
        : (isLocal ? theme.colorAccent : theme.colorPanelBorder);
    const float borderWidth = (isLocal || isKillerSlot) ? 3.0F : 2.0F;
    m_ui->DrawRectOutline(slotRect, borderWidth, borderColor);
    
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
        // Empty slot: killer shows "No Killer", survivor shows "Empty"
        const std::string emptyText = isKillerSlot ? "No Killer" : "Empty";
        const glm::vec4 emptyColor = isKillerSlot
            ? glm::vec4{0.8F, 0.3F, 0.3F, 0.7F}
            : theme.colorTextMuted;
        m_ui->DrawTextLabel(x + width / 2.0F - 35.0F * scale, y + height / 2.0F, emptyText, emptyColor, 0.9F * scale);
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
        const int totalOptions = static_cast<int>(m_availablePerkIds.size()) + 1;
        const int visibleCount = std::min(kDropdownPageSize, totalOptions);
        m_perkDropdownStart = std::clamp(m_perkDropdownStart, 0, std::max(0, totalOptions - visibleCount));
        const float dropdownHeight = static_cast<float>(visibleCount) * optionHeight + 36.0F * scale;
        
        // Draw dropdown panel
        engine::ui::UiRect dropdownRect{dropdownX, dropdownY, dropdownWidth, dropdownHeight};
        glm::vec4 bgColor = theme.colorPanel;
        bgColor.a = 0.98F;
        m_ui->DrawRect(dropdownRect, bgColor);
        m_ui->DrawRectOutline(dropdownRect, 2.0F, theme.colorAccent);
        
        const float listY = dropdownY + 5.0F * scale;
        for (int i = 0; i < visibleCount; ++i)
        {
            const int optionIndex = m_perkDropdownStart + i;
            const float optY = listY + static_cast<float>(i) * optionHeight;
            engine::ui::UiRect optRect{dropdownX + 3.0F * scale, optY, dropdownWidth - 6.0F * scale, optionHeight};
            const bool selected = (optionIndex == 0 && m_state.selectedPerks[m_selectedPerkSlot].empty()) ||
                                  (optionIndex > 0 && m_state.selectedPerks[m_selectedPerkSlot] == m_availablePerkIds[static_cast<std::size_t>(optionIndex - 1)]);
            glm::vec4 optColor = selected ? theme.colorAccent : theme.colorBackground;
            optColor.a = 0.7F;
            m_ui->DrawRect(optRect, optColor);
            
            std::string perkName = "- None -";
            if (optionIndex > 0)
            {
                const std::size_t perkIx = static_cast<std::size_t>(optionIndex - 1);
                perkName = perkIx < m_availablePerkNames.size() ? m_availablePerkNames[perkIx] : m_availablePerkIds[perkIx];
            }
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
    if (keepOpen != "character")
    {
        m_characterDropdownOpen = false;
        m_characterDropdownStart = 0;
    }
    if (keepOpen != "item")
    {
        m_itemDropdownOpen = false;
        m_itemDropdownStart = 0;
    }
    if (keepOpen != "power")
    {
        m_powerDropdownOpen = false;
        m_powerDropdownStart = 0;
    }
    if (keepOpen != "addonA")
    {
        m_addonADropdownOpen = false;
        m_addonADropdownStart = 0;
    }
    if (keepOpen != "addonB")
    {
        m_addonBDropdownOpen = false;
        m_addonBDropdownStart = 0;
    }
    if (keepOpen != "perk")
    {
        m_selectedPerkSlot = -1;
        m_perkDropdownStart = 0;
    }
}

void LobbyScene::HandleAddonDropdownClick(float x, float y, bool isAddonA)
{
    const float scale = m_ui->Scale();
    const float btnWidth = 130.0F * scale;
    const float btnHeight = 30.0F * scale;
    int& selectedIndex = isAddonA ? m_selectedAddonAIndex : m_selectedAddonBIndex;
    bool& dropdownOpen = isAddonA ? m_addonADropdownOpen : m_addonBDropdownOpen;
    int& dropdownStart = isAddonA ? m_addonADropdownStart : m_addonBDropdownStart;
    
    const float dropdownY = y + btnHeight + 2.0F * scale;
    const float optionHeight = 24.0F * scale;
    const int totalOptions = static_cast<int>(m_addonIds.size()) + 1;
    const int visibleCount = std::min(kDropdownPageSize, totalOptions);
    dropdownStart = std::clamp(dropdownStart, 0, std::max(0, totalOptions - visibleCount));
    const float listY = dropdownY + 4.0F * scale;
    const float navY = listY + static_cast<float>(visibleCount) * optionHeight + 3.0F * scale;
    const float navW = (btnWidth - 8.0F * scale) * 0.5F;

    if (isMouseOver(x + 2.0F * scale, navY, navW - 2.0F * scale, 22.0F * scale))
    {
        dropdownStart = std::max(0, dropdownStart - 1);
        return;
    }

    if (isMouseOver(x + navW + 1.0F * scale, navY, navW - 2.0F * scale, 22.0F * scale))
    {
        dropdownStart = std::min(std::max(0, totalOptions - visibleCount), dropdownStart + 1);
        return;
    }
    
    for (int i = 0; i < visibleCount; ++i)
    {
        const int optionIndex = dropdownStart + i;
        const float optY = listY + static_cast<float>(i) * optionHeight;
        if (!isMouseOver(x + 2.0F * scale, optY, btnWidth - 4.0F * scale, optionHeight - 2.0F * scale))
        {
            continue;
        }

        selectedIndex = optionIndex;
        if (optionIndex == 0)
        {
            if (isAddonA)
            {
                m_state.selectedAddonA = "";
            }
            else
            {
                m_state.selectedAddonB = "";
            }
        }
        else
        {
            const std::string& selectedAddonId = m_addonIds[static_cast<std::size_t>(optionIndex - 1)];
            if (isAddonA)
            {
                m_state.selectedAddonA = selectedAddonId;
            }
            else
            {
                m_state.selectedAddonB = selectedAddonId;
            }
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
            engine::ui::UiRect btnRect{x, y, 260.0F * scale, 35.0F * scale};
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
    
    const float btnWidth = 260.0F * scale;
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
        const int totalOptions = static_cast<int>(ids.size());
        const int visibleCount = std::min(kDropdownPageSize, totalOptions);
        m_characterDropdownStart = std::clamp(m_characterDropdownStart, 0, std::max(0, totalOptions - visibleCount));
        const float dropdownHeight = static_cast<float>(visibleCount) * optionHeight + 12.0F * scale;
        
        engine::ui::UiRect dropdownRect{x, dropdownY, btnWidth, dropdownHeight};
        glm::vec4 bgColor = theme.colorPanel;
        bgColor.a = 0.98F;
        m_ui->DrawRect(dropdownRect, bgColor);
        m_ui->DrawRectOutline(dropdownRect, 2.0F, theme.colorAccent);
        
        const float listY = dropdownY + 5.0F * scale;
        for (int i = 0; i < visibleCount; ++i)
        {
            const int optionIndex = m_characterDropdownStart + i;
            const float optY = listY + static_cast<float>(i) * optionHeight;
            engine::ui::UiRect optRect{x + 3.0F * scale, optY, btnWidth - 6.0F * scale, optionHeight - 2.0F * scale};
            glm::vec4 optColor = optionIndex == m_selectedCharacterIndex ? theme.colorAccent : theme.colorBackground;
            optColor.a = 0.7F;
            m_ui->DrawRect(optRect, optColor);
            
            std::string charName = static_cast<std::size_t>(optionIndex) < names.size()
                ? names[static_cast<std::size_t>(optionIndex)]
                : ids[static_cast<std::size_t>(optionIndex)];
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
            engine::ui::UiRect btnRect{x, y, 260.0F * scale, 35.0F * scale};
            m_ui->DrawRect(btnRect, theme.colorBackground);
            m_ui->DrawRectOutline(btnRect, 2.0F, theme.colorPanelBorder);
            m_ui->DrawTextLabel(x + 10.0F * scale, y + 8.0F * scale, "No items", theme.colorTextMuted, 0.85F * scale);
        }
        return;
    }
    
    const float btnWidth = 260.0F * scale;
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
        const int totalOptions = static_cast<int>(m_itemIds.size()) + 1;
        const int visibleCount = std::min(kDropdownPageSize, totalOptions);
        m_itemDropdownStart = std::clamp(m_itemDropdownStart, 0, std::max(0, totalOptions - visibleCount));
        const float dropdownHeight = static_cast<float>(visibleCount) * optionHeight + 12.0F * scale;
        
        engine::ui::UiRect dropdownRect{x, dropdownY, btnWidth, dropdownHeight};
        glm::vec4 bgColor = theme.colorPanel;
        bgColor.a = 0.98F;
        m_ui->DrawRect(dropdownRect, bgColor);
        m_ui->DrawRectOutline(dropdownRect, 2.0F, theme.colorAccent);
        
        const float listY = dropdownY + 5.0F * scale;
        for (int i = 0; i < visibleCount; ++i)
        {
            const int optionIndex = m_itemDropdownStart + i;
            const float optY = listY + static_cast<float>(i) * optionHeight;
            engine::ui::UiRect optRect{x + 3.0F * scale, optY, btnWidth - 6.0F * scale, optionHeight - 2.0F * scale};
            glm::vec4 optColor = optionIndex == m_selectedItemIndex ? theme.colorAccent : theme.colorBackground;
            optColor.a = 0.7F;
            m_ui->DrawRect(optRect, optColor);
            std::string itemName = "- None -";
            if (optionIndex > 0)
            {
                const std::size_t itemIx = static_cast<std::size_t>(optionIndex - 1);
                itemName = itemIx < m_itemNames.size() ? m_itemNames[itemIx] : m_itemIds[itemIx];
            }
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
    
    const float btnWidth = 260.0F * scale;
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
        const int totalOptions = static_cast<int>(m_powerIds.size());
        const int visibleCount = std::min(kDropdownPageSize, totalOptions);
        m_powerDropdownStart = std::clamp(m_powerDropdownStart, 0, std::max(0, totalOptions - visibleCount));
        const float dropdownHeight = static_cast<float>(visibleCount) * optionHeight + 12.0F * scale;
        
        engine::ui::UiRect dropdownRect{x, dropdownY, btnWidth, dropdownHeight};
        glm::vec4 bgColor = theme.colorPanel;
        bgColor.a = 0.98F;
        m_ui->DrawRect(dropdownRect, bgColor);
        m_ui->DrawRectOutline(dropdownRect, 2.0F, theme.colorDanger);
        
        const float listY = dropdownY + 5.0F * scale;
        for (int i = 0; i < visibleCount; ++i)
        {
            const int optionIndex = m_powerDropdownStart + i;
            const float optY = listY + static_cast<float>(i) * optionHeight;
            engine::ui::UiRect optRect{x + 3.0F * scale, optY, btnWidth - 6.0F * scale, optionHeight - 2.0F * scale};
            glm::vec4 optColor = optionIndex == m_selectedPowerIndex ? theme.colorDanger : theme.colorBackground;
            optColor.a = 0.7F;
            m_ui->DrawRect(optRect, optColor);
            
            std::string powerName = static_cast<std::size_t>(optionIndex) < m_powerNames.size()
                ? m_powerNames[static_cast<std::size_t>(optionIndex)]
                : m_powerIds[static_cast<std::size_t>(optionIndex)];
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
        const int totalOptions = static_cast<int>(m_addonIds.size()) + 1;
        const int visibleCount = std::min(kDropdownPageSize, totalOptions);
        int& dropdownStart = isAddonA ? m_addonADropdownStart : m_addonBDropdownStart;
        dropdownStart = std::clamp(dropdownStart, 0, std::max(0, totalOptions - visibleCount));
        const float dropdownHeight = static_cast<float>(visibleCount) * optionHeight + 10.0F * scale;
        
        engine::ui::UiRect dropdownRect{x, dropdownY, btnWidth, dropdownHeight};
        glm::vec4 bgColor = theme.colorPanel;
        bgColor.a = 0.98F;
        m_ui->DrawRect(dropdownRect, bgColor);
        m_ui->DrawRectOutline(dropdownRect, 2.0F, theme.colorAccent);
        
        const float listY = dropdownY + 4.0F * scale;
        for (int i = 0; i < visibleCount; ++i)
        {
            const int optionIndex = dropdownStart + i;
            const float optY = listY + static_cast<float>(i) * optionHeight;
            engine::ui::UiRect optRect{x + 2.0F * scale, optY, btnWidth - 4.0F * scale, optionHeight - 2.0F * scale};
            glm::vec4 optColor = selectedIndex == optionIndex ? theme.colorAccent : theme.colorBackground;
            optColor.a = 0.7F;
            m_ui->DrawRect(optRect, optColor);
            std::string addonName = "- None -";
            if (optionIndex > 0)
            {
                const std::size_t addonIx = static_cast<std::size_t>(optionIndex - 1);
                addonName = addonIx < m_addonNames.size() ? m_addonNames[addonIx] : m_addonIds[addonIx];
            }
            if (addonName.length() > 14) addonName = addonName.substr(0, 13) + ".";
            m_ui->DrawTextLabel(x + 8.0F * scale, optY + 4.0F * scale, addonName, theme.colorText, 0.75F * scale);
        }
    }
}

// ---- GLB model rendering ----

LobbyScene::LobbyMeshEntry LobbyScene::GetOrLoadLobbyMesh(const std::string& characterId)
{
    auto it = m_lobbyMeshCache.find(characterId);
    if (it != m_lobbyMeshCache.end())
    {
        return it->second;
    }
    
    LobbyMeshEntry entry{};
    entry.attempted = true;
    
    if (m_gameplay)
    {
        auto mesh = m_gameplay->GetCharacterMeshForLobby(characterId);
        entry.gpuMesh = mesh.gpuMesh;
        entry.boundsMinY = mesh.boundsMinY;
        entry.boundsMaxY = mesh.boundsMaxY;
        entry.maxAbsXZ = mesh.maxAbsXZ;
        entry.modelYawDegrees = mesh.modelYawDegrees;
    }
    
    m_lobbyMeshCache[characterId] = entry;
    return entry;
}

void LobbyScene::DrawPlayerGlbModel(const LobbyPlayer& player)
{
    if (!m_renderer) return;
    
    // Try loading GLB mesh for this character
    if (!player.characterId.empty())
    {
        auto meshEntry = GetOrLoadLobbyMesh(player.characterId);
        if (meshEntry.gpuMesh != engine::render::Renderer::kInvalidGpuMesh)
        {
            // Scale model to fit nicely
            const float modelHeight = meshEntry.boundsMaxY - meshEntry.boundsMinY;
            const float targetHeight = 1.8F;  // Standard character height
            const float scaleFactor = (modelHeight > 0.01F) ? targetHeight / modelHeight : 1.0F;
            
            glm::mat4 transform = glm::mat4(1.0F);
            transform = glm::translate(transform, player.worldPosition);
            transform = glm::rotate(transform, player.rotation + glm::radians(meshEntry.modelYawDegrees), glm::vec3{0.0F, 1.0F, 0.0F});
            transform = glm::scale(transform, glm::vec3{scaleFactor});
            transform = glm::translate(transform, glm::vec3{0.0F, -meshEntry.boundsMinY, 0.0F});
            
            m_renderer->DrawGpuMesh(meshEntry.gpuMesh, transform);
            return;  // Successfully drew GLB model
        }
    }
    
    // Fallback to procedural body
    DrawPlayerBody(player);
}

// ---- Enhanced environment ----

void LobbyScene::DrawFences()
{
    if (!m_renderer) return;
    
    engine::render::MaterialParams fenceMat;
    fenceMat.roughness = 0.92F;
    
    const glm::vec3 fenceColor{0.22F, 0.15F, 0.08F};
    const glm::vec3 postColor{0.2F, 0.12F, 0.06F};
    
    // Scattered fence sections (broken, horror style)
    const auto drawFenceSection = [&](float x, float z, float rotation, int posts, bool broken) {
        for (int i = 0; i < posts; ++i)
        {
            const float offset = static_cast<float>(i) * 0.6F;
            const float postHeight = broken && (i % 2 == 1) ? 0.5F : 0.9F;
            const float tilt = broken && (i % 3 == 0) ? 8.0F : 0.0F;
            
            const float rad = glm::radians(rotation);
            const float px = x + std::cos(rad) * offset;
            const float pz = z + std::sin(rad) * offset;
            
            m_renderer->DrawOrientedBox(
                glm::vec3{px, postHeight * 0.5F, pz},
                glm::vec3{0.04F, postHeight * 0.5F, 0.04F},
                glm::vec3{tilt, rotation, 0.0F},
                postColor, fenceMat
            );
        }
        // Cross planks
        for (int i = 0; i < posts - 1; ++i)
        {
            const float offset = static_cast<float>(i) * 0.6F + 0.3F;
            const float rad = glm::radians(rotation);
            const float px = x + std::cos(rad) * offset;
            const float pz = z + std::sin(rad) * offset;
            const float plankY = broken && (i % 2 == 0) ? 0.35F : 0.55F;
            
            m_renderer->DrawOrientedBox(
                glm::vec3{px, plankY, pz},
                glm::vec3{0.32F, 0.03F, 0.02F},
                glm::vec3{0.0F, rotation, broken ? 5.0F : 0.0F},
                fenceColor, fenceMat
            );
        }
    };
    
    drawFenceSection(-8.0F, -2.0F, 10.0F, 4, true);
    drawFenceSection(7.5F, 3.0F, -25.0F, 3, false);
    drawFenceSection(-3.0F, -7.0F, 80.0F, 5, true);
}

void LobbyScene::DrawGraves()
{
    if (!m_renderer) return;
    
    engine::render::MaterialParams stoneMat;
    stoneMat.roughness = 0.88F;
    
    const auto drawGravestone = [&](float x, float z, float rotation, float height) {
        // Stone slab
        m_renderer->DrawOrientedBox(
            glm::vec3{x, height * 0.5F, z},
            glm::vec3{0.2F, height * 0.5F, 0.06F},
            glm::vec3{0.0F, rotation, 3.0F},
            glm::vec3{0.4F, 0.38F, 0.35F},
            stoneMat
        );
        // Top arch
        m_renderer->DrawOrientedBox(
            glm::vec3{x, height + 0.05F, z},
            glm::vec3{0.15F, 0.08F, 0.06F},
            glm::vec3{0.0F, rotation, 3.0F},
            glm::vec3{0.38F, 0.36F, 0.33F},
            stoneMat
        );
        // Dirt mound
        engine::render::MaterialParams dirtMat;
        dirtMat.roughness = 0.95F;
        m_renderer->DrawOrientedBox(
            glm::vec3{x, 0.04F, z + 0.3F},
            glm::vec3{0.25F, 0.06F, 0.4F},
            glm::vec3{0.0F, rotation, 0.0F},
            glm::vec3{0.2F, 0.15F, 0.1F},
            dirtMat
        );
    };
    
    drawGravestone(6.5F, -5.0F, 5.0F, 0.7F);
    drawGravestone(7.2F, -4.2F, -8.0F, 0.55F);
    drawGravestone(-7.0F, -3.5F, 12.0F, 0.65F);
}

void LobbyScene::DrawFog()
{
    if (!m_renderer) return;
    
    // Low-lying fog patches using semi-transparent flat boxes
    engine::render::MaterialParams fogMat;
    fogMat.roughness = 1.0F;
    fogMat.unlit = true;
    
    const float fogAlpha = 0.12F + std::sin(m_fireTime * 0.4F) * 0.03F;
    const glm::vec3 fogColor{0.5F, 0.5F, 0.55F};
    
    for (int i = 0; i < 8; ++i)
    {
        const float angle = static_cast<float>(i) * 0.785F + m_fireTime * 0.02F;
        const float radius = 5.0F + static_cast<float>(i % 3) * 2.0F;
        const float size = 1.5F + static_cast<float>(i % 2) * 1.0F;
        const float height = 0.08F + static_cast<float>(i % 3) * 0.05F;
        
        m_renderer->DrawBox(
            glm::vec3{std::cos(angle) * radius, height, std::sin(angle) * radius},
            glm::vec3{size, 0.03F, size},
            fogColor,
            fogMat
        );
    }
}

void LobbyScene::RenderLobbyFullOverlay()
{
    if (!m_ui) return;
    
    const float scale = m_ui->Scale();
    const auto& theme = m_ui->Theme();
    const int screenWidth = m_ui->ScreenWidth();
    const int screenHeight = m_ui->ScreenHeight();
    
    // Dim background
    engine::ui::UiRect bgRect{0.0F, 0.0F, static_cast<float>(screenWidth), static_cast<float>(screenHeight)};
    m_ui->DrawRect(bgRect, glm::vec4{0.0F, 0.0F, 0.0F, 0.6F});
    
    // Center panel
    const float panelWidth = 400.0F * scale;
    const float panelHeight = 180.0F * scale;
    const float panelX = (screenWidth - panelWidth) / 2.0F;
    const float panelY = (screenHeight - panelHeight) / 2.0F;
    
    engine::ui::UiRect panelRect{panelX, panelY, panelWidth, panelHeight};
    glm::vec4 panelColor = theme.colorPanel;
    panelColor.a = 0.95F;
    m_ui->DrawRect(panelRect, panelColor);
    m_ui->DrawRectOutline(panelRect, 3.0F, theme.colorDanger);
    
    m_ui->DrawTextLabel(panelX + panelWidth / 2.0F - 55.0F * scale, panelY + 25.0F * scale,
                        "LOBBY FULL", theme.colorDanger, 1.4F * scale);
    
    m_ui->DrawTextLabel(panelX + 30.0F * scale, panelY + 70.0F * scale,
                        m_lobbyFullMessage, theme.colorText, 0.9F * scale);
    
    // "OK" button to dismiss
    const float btnWidth = 100.0F * scale;
    const float btnHeight = 40.0F * scale;
    const float btnX = panelX + (panelWidth - btnWidth) / 2.0F;
    const float btnY = panelY + panelHeight - btnHeight - 20.0F * scale;
    
    engine::ui::UiRect btnRect{btnX, btnY, btnWidth, btnHeight};
    m_ui->DrawRect(btnRect, theme.colorButton);
    m_ui->DrawRectOutline(btnRect, 2.0F, theme.colorPanelBorder);
    m_ui->DrawTextLabel(btnX + btnWidth / 2.0F - 10.0F * scale, btnY + 10.0F * scale,
                        "OK", theme.colorText, 1.1F * scale);
    
    // Handle click on OK
    if (m_input && m_input->IsMousePressed(0))
    {
        if (isMouseOver(btnX, btnY, btnWidth, btnHeight))
        {
            ClearLobbyFullMessage();
        }
    }
}

}
