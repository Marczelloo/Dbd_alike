#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

#include <glm/vec3.hpp>

namespace engine::scene
{
struct ActorComponent;
enum class Role;
}

namespace game::gameplay::perks
{
enum class PerkType
{
    Passive,    // Always active modifiers
    Triggered,  // Activated on specific events
    Conditional // Active under certain conditions (chase, near hooks, etc.)
};

enum class PerkRole
{
    Survivor,
    Killer,
    Both
};

struct PerkEffect
{
    // Speed modifiers (percentage, 0.0 = none, 0.15 = +15%)
    float walkSpeedPercent = 0.0F;
    float sprintSpeedPercent = 0.0F;
    float crouchSpeedPercent = 0.0F;
    float crawlSpeedPercent = 0.0F;
    float vaultSpeedPercent = 0.0F;

    // Terror radius modifier (meters, positive = increase)
    float terrorRadiusMeters = 0.0F;

    // Pallet interaction modifiers
    float palletBreakTimePercent = 0.0F;
    float palletDropTimePercent = 0.0F;

    // Healing modifiers (percentage, 0.0 = none, -0.2 = 20% faster)
    float selfHealSpeedPercent = 0.0F;
    float altruisticHealSpeedPercent = 0.0F;

    // Generator modifiers
    float repairSpeedPercent = 0.0F;
    float skillCheckZonePercent = 0.0F;

    // Cooldown and duration (seconds)
    float activationCooldownSeconds = 0.0F;
    float effectDurationSeconds = 0.0F;
};

struct PerkAsset
{
    std::string id;
    std::string name;
    std::string description;
    PerkRole role = PerkRole::Both;
    PerkType type = PerkType::Passive;
    PerkEffect effects;
    int maxStacks = 1;
};

struct ActivePerkState
{
    std::string perkId;
    int currentStacks = 0;
    float cooldownRemainingSeconds = 0.0F;
    float activeRemainingSeconds = 0.0F;
    bool isActive = false;
};

struct PerkLoadout
{
    static constexpr int kMaxSlots = 3;
    std::array<std::string, kMaxSlots> perkIds = {"", "", ""};

    [[nodiscard]] bool IsEmpty() const
    {
        for (const auto& id : perkIds)
        {
            if (!id.empty())
            {
                return false;
            }
        }
        return true;
    }

    void Clear()
    {
        perkIds = {"", "", ""};
    }

    void SetPerk(int slot, const std::string& id)
    {
        if (slot >= 0 && slot < kMaxSlots)
        {
            perkIds[slot] = id;
        }
    }

    [[nodiscard]] std::string GetPerk(int slot) const
    {
        if (slot >= 0 && slot < kMaxSlots)
        {
            return perkIds[slot];
        }
        return "";
    }

    [[nodiscard]] int GetSlotCount() const
    {
        int count = 0;
        for (const auto& id : perkIds)
        {
            if (!id.empty())
            {
                ++count;
            }
        }
        return count;
    }
};

class PerkSystem
{
public:
    PerkSystem();
    ~PerkSystem() = default;

    void InitializeDefaultPerks();

    [[nodiscard]] const PerkAsset* GetPerk(const std::string& id) const;
    [[nodiscard]] std::vector<std::string> ListPerks(PerkRole role) const;
    [[nodiscard]] bool HasPerk(const std::string& id) const;

    // Loadout management
    void SetSurvivorLoadout(const PerkLoadout& loadout) { m_survivorLoadout = loadout; }
    void SetKillerLoadout(const PerkLoadout& loadout) { m_killerLoadout = loadout; }
    [[nodiscard]] const PerkLoadout& GetSurvivorLoadout() const { return m_survivorLoadout; }
    [[nodiscard]] const PerkLoadout& GetKillerLoadout() const { return m_killerLoadout; }
    
    // Set default dev loadouts (for testing)
    void SetDefaultDevLoadout();

    // Active state management
    void InitializeActiveStates();
    void UpdateActiveStates(float fixedDt);
    void ActivatePerk(const std::string& perkId, engine::scene::Role role);
    void DeactivatePerk(const std::string& perkId, engine::scene::Role role);
    [[nodiscard]] bool IsPerkActive(const std::string& perkId, engine::scene::Role role) const;

    // Active state access (for gameplay integration)
    [[nodiscard]] const std::vector<ActivePerkState>& GetActivePerks(engine::scene::Role role) const;

    // Effect application
    [[nodiscard]] PerkEffect GetTotalEffects(engine::scene::Role role) const;
    [[nodiscard]] float GetSpeedModifier(engine::scene::Role role, bool sprinting, bool crouching, bool crawling) const;
    [[nodiscard]] float GetTerrorRadiusModifier(engine::scene::Role role) const;
    [[nodiscard]] float GetVaultSpeedModifier(engine::scene::Role role) const;
    [[nodiscard]] float GetPalletBreakModifier(engine::scene::Role role) const;
    [[nodiscard]] float GetHealSpeedModifier(engine::scene::Role role, bool selfHeal) const;
    [[nodiscard]] float GetRepairSpeedModifier(engine::scene::Role role) const;

    // Config
    bool LoadPerksFromJson(const std::string& jsonPath);
    bool SavePerksToJson(const std::string& jsonPath) const;

private:
    void RegisterPerk(const PerkAsset& perk);

    std::unordered_map<std::string, PerkAsset> m_perkRegistry;
    PerkLoadout m_survivorLoadout;
    PerkLoadout m_killerLoadout;
    std::vector<ActivePerkState> m_activeSurvivorPerks;
    std::vector<ActivePerkState> m_activeKillerPerks;
};

// Helper functions
[[nodiscard]] const char* PerkTypeToText(PerkType type);
[[nodiscard]] const char* PerkRoleToText(PerkRole role);
} // namespace game::gameplay::perks
