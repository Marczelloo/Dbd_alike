#pragma once

#include <array>
#include <functional>
#include <string>
#include <vector>

namespace engine::ui
{
class UiSystem;
}

namespace game::gameplay::perks
{
struct PerkAsset;
class PerkSystem;
}

namespace game::ui
{

struct PerkSlotState
{
    std::string perkId;
    std::string perkName;
    bool isLocked = false;
    bool isEmpty = true;
};

struct PerkLoadoutEditorState
{
    std::array<PerkSlotState, 4> slots;
    int selectedSlotIndex = -1;
    int hoveredPerkIndex = -1;
    std::string filterText;
    bool isSurvivorMode = true;
};

class PerkLoadoutEditor
{
public:
    using LoadoutChangedCallback = std::function<void(const std::array<std::string, 4>&)>;

    PerkLoadoutEditor() = default;
    ~PerkLoadoutEditor() = default;

    bool Initialize(engine::ui::UiSystem* uiSystem, game::gameplay::perks::PerkSystem* perkSystem);
    void Shutdown();

    void Update(float deltaSeconds);
    void Render();
    
    void SetLoadout(const std::array<std::string, 4>& perkIds);
    [[nodiscard]] std::array<std::string, 4> GetLoadout() const;
    
    void SetSurvivorMode(bool isSurvivor) { m_state.isSurvivorMode = isSurvivor; }
    void SetLoadoutChangedCallback(LoadoutChangedCallback callback) { m_onLoadoutChanged = std::move(callback); }
    
    void ClearLoadout();
    void RandomizeLoadout();

private:
    void DrawPerkSlots(float x, float y);
    void DrawAvailablePerks(float x, float y, float width, float height);
    void DrawPerkTooltip(const game::gameplay::perks::PerkAsset& perk, float x, float y);
    void HandlePerkSelection(const std::string& perkId);
    void HandleSlotClick(int slotIndex);
    void RemovePerkFromSlot(int slotIndex);

    [[nodiscard]] std::vector<game::gameplay::perks::PerkAsset> GetFilteredPerks() const;
    [[nodiscard]] bool IsPerkInLoadout(const std::string& perkId) const;
    [[nodiscard]] int FindEmptySlot() const;

    engine::ui::UiSystem* m_ui = nullptr;
    game::gameplay::perks::PerkSystem* m_perkSystem = nullptr;
    
    PerkLoadoutEditorState m_state;
    LoadoutChangedCallback m_onLoadoutChanged;
    
    float m_slotSize = 80.0F;
    float m_slotSpacing = 12.0F;
    float m_tooltipDelay = 0.5F;
    float m_tooltipTimer = 0.0F;
};

}
