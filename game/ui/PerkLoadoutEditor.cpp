#include "PerkLoadoutEditor.hpp"

#include <algorithm>

#include "engine/ui/UiSystem.hpp"
#include "game/gameplay/PerkSystem.hpp"

namespace game::ui
{

bool PerkLoadoutEditor::Initialize(engine::ui::UiSystem* uiSystem, game::gameplay::perks::PerkSystem* perkSystem)
{
    if (!uiSystem || !perkSystem)
    {
        return false;
    }
    
    m_ui = uiSystem;
    m_perkSystem = perkSystem;
    
    for (auto& slot : m_state.slots)
    {
        slot.perkId.clear();
        slot.perkName.clear();
        slot.isEmpty = true;
        slot.isLocked = false;
    }
    
    return true;
}

void PerkLoadoutEditor::Shutdown()
{
    m_ui = nullptr;
    m_perkSystem = nullptr;
}

void PerkLoadoutEditor::Update(float deltaSeconds)
{
    if (m_state.hoveredPerkIndex >= 0)
    {
        m_tooltipTimer += deltaSeconds;
    }
    else
    {
        m_tooltipTimer = 0.0F;
    }
}

void PerkLoadoutEditor::Render()
{
    if (!m_ui)
    {
        return;
    }
    
    const int screenWidth = m_ui->ScreenWidth();
    const int screenHeight = m_ui->ScreenHeight();
    const float scale = m_ui->Scale();
    const auto& theme = m_ui->Theme();
    
    const float panelWidth = 500.0F * scale;
    const float panelHeight = 400.0F * scale;
    const float panelX = (screenWidth - panelWidth) / 2.0F;
    const float panelY = (screenHeight - panelHeight) / 2.0F;
    
    engine::ui::UiRect panelRect{panelX, panelY, panelWidth, panelHeight};
    glm::vec4 panelColor = theme.colorPanel;
    panelColor.a = 0.95F;
    m_ui->DrawRect(panelRect, panelColor);
    m_ui->DrawRectOutline(panelRect, 2.0F, theme.colorPanelBorder);
    
    const float titleY = panelY + 15.0F * scale;
    const std::string title = m_state.isSurvivorMode ? "Survivor Perks" : "Killer Perks";
    m_ui->DrawTextLabel(panelX + 20.0F * scale, titleY, title, theme.colorText, 1.2F * scale);
    
    DrawPerkSlots(panelX + 20.0F * scale, panelY + 60.0F * scale);
    
    DrawAvailablePerks(panelX + 20.0F * scale, panelY + 180.0F * scale, panelWidth - 40.0F * scale, panelHeight - 200.0F * scale);
}

void PerkLoadoutEditor::DrawPerkSlots(float x, float y)
{
    const auto& theme = m_ui->Theme();
    const float scale = m_ui->Scale();
    const float slotSize = m_slotSize * scale;
    const float spacing = m_slotSpacing * scale;
    
    m_ui->DrawTextLabel(x, y - 20.0F * scale, "Loadout:", theme.colorTextMuted, 0.8F * scale);
    
    for (int i = 0; i < 4; ++i)
    {
        const float slotX = x + i * (slotSize + spacing);
        const auto& slot = m_state.slots[i];
        
        engine::ui::UiRect slotRect{slotX, y, slotSize, slotSize};
        
        glm::vec4 slotColor = theme.colorBackground;
        if (i == m_state.selectedSlotIndex)
        {
            slotColor = theme.colorAccent;
            slotColor.a = 0.3F;
        }
        else if (!slot.isEmpty)
        {
            slotColor = theme.colorButton;
            slotColor.a = 0.8F;
        }
        
        m_ui->DrawRect(slotRect, slotColor);
        m_ui->DrawRectOutline(slotRect, 2.0F, theme.colorPanelBorder);
        
        if (!slot.isEmpty)
        {
            const float iconPadding = 8.0F * scale;
            engine::ui::UiRect iconRect{slotX + iconPadding, y + iconPadding, slotSize - iconPadding * 2, slotSize - iconPadding * 2};
            glm::vec4 iconColor = theme.colorSuccess;
            iconColor.a = 0.7F;
            m_ui->DrawRect(iconRect, iconColor);
            
            const float textY = y + slotSize + 5.0F * scale;
            m_ui->DrawTextLabel(slotX, textY, slot.perkName, theme.colorText, 0.7F * scale);
        }
        else
        {
            const float textY = y + slotSize / 2.0F;
            m_ui->DrawTextLabel(slotX + 5.0F * scale, textY, "Empty", theme.colorTextMuted, 0.7F * scale);
        }
    }
}

void PerkLoadoutEditor::DrawAvailablePerks(float x, float y, float width, float height)
{
    const auto& theme = m_ui->Theme();
    const float scale = m_ui->Scale();
    
    m_ui->DrawTextLabel(x, y - 20.0F * scale, "Available Perks:", theme.colorTextMuted, 0.8F * scale);
    
    engine::ui::UiRect listRect{x, y, width, height};
    glm::vec4 listBgColor = theme.colorBackground;
    listBgColor.a = 0.5F;
    m_ui->DrawRect(listRect, listBgColor);
    m_ui->DrawRectOutline(listRect, 1.0F, theme.colorPanelBorder);
    
    auto filteredPerks = GetFilteredPerks();
    
    const float perkItemHeight = 40.0F * scale;
    const float padding = 10.0F * scale;
    float currentY = y + padding;
    
    int perkIndex = 0;
    for (const auto& perk : filteredPerks)
    {
        if (currentY + perkItemHeight > y + height)
        {
            break;
        }
        
        const bool isInLoadout = IsPerkInLoadout(perk.id);
        const bool isHovered = (perkIndex == m_state.hoveredPerkIndex);
        
        engine::ui::UiRect perkRect{x + padding, currentY, width - padding * 2, perkItemHeight};
        
        glm::vec4 perkColor = isInLoadout ? theme.colorAccent : theme.colorButton;
        perkColor.a = isHovered ? 0.9F : 0.7F;
        m_ui->DrawRect(perkRect, perkColor);
        
        m_ui->DrawTextLabel(x + padding + 10.0F * scale, currentY + 8.0F * scale, perk.name, theme.colorText, 0.9F * scale);
        
        std::string roleText = (perk.role == game::gameplay::perks::PerkRole::Survivor) ? "Survivor" :
                               (perk.role == game::gameplay::perks::PerkRole::Killer) ? "Killer" : "Both";
        m_ui->DrawTextLabel(x + padding + 10.0F * scale, currentY + 24.0F * scale, roleText, theme.colorTextMuted, 0.7F * scale);
        
        if (isInLoadout)
        {
            m_ui->DrawTextLabel(x + width - 60.0F * scale, currentY + 12.0F * scale, "Equipped", theme.colorSuccess, 0.7F * scale);
        }
        
        currentY += perkItemHeight + 4.0F * scale;
        ++perkIndex;
    }
    
    if (m_tooltipTimer >= m_tooltipDelay && m_state.hoveredPerkIndex >= 0 && 
        m_state.hoveredPerkIndex < static_cast<int>(filteredPerks.size()))
    {
        const auto& hoveredPerk = filteredPerks[m_state.hoveredPerkIndex];
        DrawPerkTooltip(hoveredPerk, x + width + 10.0F * scale, y);
    }
}

void PerkLoadoutEditor::DrawPerkTooltip(const game::gameplay::perks::PerkAsset& perk, float x, float y)
{
    const auto& theme = m_ui->Theme();
    const float scale = m_ui->Scale();
    
    const float tooltipWidth = 250.0F * scale;
    const float tooltipHeight = 120.0F * scale;
    
    engine::ui::UiRect tooltipRect{x, y, tooltipWidth, tooltipHeight};
    glm::vec4 tooltipBg = theme.colorPanel;
    tooltipBg.a = 0.98F;
    m_ui->DrawRect(tooltipRect, tooltipBg);
    m_ui->DrawRectOutline(tooltipRect, 2.0F, theme.colorAccent);
    
    m_ui->DrawTextLabel(x + 10.0F * scale, y + 10.0F * scale, perk.name, theme.colorText, 1.0F * scale);
    m_ui->DrawTextLabel(x + 10.0F * scale, y + 35.0F * scale, perk.description, theme.colorTextMuted, 0.75F * scale);
}

void PerkLoadoutEditor::HandlePerkSelection(const std::string& perkId)
{
    if (IsPerkInLoadout(perkId))
    {
        return;
    }
    
    int targetSlot = m_state.selectedSlotIndex;
    if (targetSlot < 0)
    {
        targetSlot = FindEmptySlot();
    }
    
    if (targetSlot < 0 || targetSlot >= 4)
    {
        return;
    }
    
    m_state.slots[targetSlot].perkId = perkId;
    m_state.slots[targetSlot].isEmpty = false;
    
    if (m_perkSystem)
    {
        auto perk = m_perkSystem->GetPerk(perkId);
        if (perk)
        {
            m_state.slots[targetSlot].perkName = perk->name;
        }
    }
    
    if (m_onLoadoutChanged)
    {
        m_onLoadoutChanged(GetLoadout());
    }
}

void PerkLoadoutEditor::HandleSlotClick(int slotIndex)
{
    m_state.selectedSlotIndex = slotIndex;
}

void PerkLoadoutEditor::RemovePerkFromSlot(int slotIndex)
{
    if (slotIndex < 0 || slotIndex >= 4)
    {
        return;
    }
    
    m_state.slots[slotIndex].perkId.clear();
    m_state.slots[slotIndex].perkName.clear();
    m_state.slots[slotIndex].isEmpty = true;
    
    if (m_onLoadoutChanged)
    {
        m_onLoadoutChanged(GetLoadout());
    }
}

void PerkLoadoutEditor::SetLoadout(const std::array<std::string, 4>& perkIds)
{
    for (int i = 0; i < 4; ++i)
    {
        m_state.slots[i].perkId = perkIds[i];
        m_state.slots[i].isEmpty = perkIds[i].empty();
        
        if (!perkIds[i].empty() && m_perkSystem)
        {
            auto perk = m_perkSystem->GetPerk(perkIds[i]);
            if (perk)
            {
                m_state.slots[i].perkName = perk->name;
            }
        }
    }
}

std::array<std::string, 4> PerkLoadoutEditor::GetLoadout() const
{
    std::array<std::string, 4> result;
    for (int i = 0; i < 4; ++i)
    {
        result[i] = m_state.slots[i].perkId;
    }
    return result;
}

void PerkLoadoutEditor::ClearLoadout()
{
    for (auto& slot : m_state.slots)
    {
        slot.perkId.clear();
        slot.perkName.clear();
        slot.isEmpty = true;
    }
    
    if (m_onLoadoutChanged)
    {
        m_onLoadoutChanged(GetLoadout());
    }
}

void PerkLoadoutEditor::RandomizeLoadout()
{
    if (!m_perkSystem) return;
    
    auto filteredPerks = GetFilteredPerks();
    if (filteredPerks.empty()) return;
    
    ClearLoadout();
    
    std::vector<int> usedIndices;
    int slotsFilled = 0;
    
    while (slotsFilled < 4 && usedIndices.size() < filteredPerks.size())
    {
        int randomIndex = std::rand() % filteredPerks.size();
        
        bool alreadyUsed = false;
        for (int idx : usedIndices)
        {
            if (idx == randomIndex)
            {
                alreadyUsed = true;
                break;
            }
        }
        
        if (!alreadyUsed)
        {
            m_state.slots[slotsFilled].perkId = filteredPerks[randomIndex].id;
            m_state.slots[slotsFilled].perkName = filteredPerks[randomIndex].name;
            m_state.slots[slotsFilled].isEmpty = false;
            usedIndices.push_back(randomIndex);
            ++slotsFilled;
        }
    }
    
    if (m_onLoadoutChanged)
    {
        m_onLoadoutChanged(GetLoadout());
    }
}

std::vector<game::gameplay::perks::PerkAsset> PerkLoadoutEditor::GetFilteredPerks() const
{
    if (!m_perkSystem) return {};
    
    auto role = m_state.isSurvivorMode ? game::gameplay::perks::PerkRole::Survivor : game::gameplay::perks::PerkRole::Killer;
    auto perkIds = m_perkSystem->ListPerks(role);
    std::vector<game::gameplay::perks::PerkAsset> filtered;
    
    for (const auto& perkId : perkIds)
    {
        const auto* perk = m_perkSystem->GetPerk(perkId);
        if (!perk) continue;
        
        bool roleMatch = (perk->role == game::gameplay::perks::PerkRole::Both) ||
                         (m_state.isSurvivorMode && perk->role == game::gameplay::perks::PerkRole::Survivor) ||
                         (!m_state.isSurvivorMode && perk->role == game::gameplay::perks::PerkRole::Killer);
        
        if (!roleMatch) continue;
        
        if (!m_state.filterText.empty())
        {
            if (perk->name.find(m_state.filterText) == std::string::npos &&
                perk->id.find(m_state.filterText) == std::string::npos)
            {
                continue;
            }
        }
        
        filtered.push_back(*perk);
    }
    
    return filtered;
}

bool PerkLoadoutEditor::IsPerkInLoadout(const std::string& perkId) const
{
    for (const auto& slot : m_state.slots)
    {
        if (slot.perkId == perkId)
        {
            return true;
        }
    }
    return false;
}

int PerkLoadoutEditor::FindEmptySlot() const
{
    for (int i = 0; i < 4; ++i)
    {
        if (m_state.slots[i].isEmpty)
        {
            return i;
        }
    }
    return -1;
}

}
