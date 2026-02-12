#include "PerkSystem.hpp"

#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>

#include "engine/scene/Components.hpp"

namespace
{
[[nodiscard]] constexpr float kEpsilon = 1.0e-6F;
}

namespace game::gameplay::perks
{
PerkSystem::PerkSystem()
{
    InitializeDefaultPerks();
}

void PerkSystem::InitializeDefaultPerks()
{
    // === SURVIVOR PERKS ===

    // Sprint Burst: Burst of speed when leaving chase
    PerkAsset sprintBurst;
    sprintBurst.id = "sprint_burst";
    sprintBurst.name = "Sprint Burst";
    sprintBurst.description = "Gain a burst of speed when leaving chase";
    sprintBurst.role = PerkRole::Survivor;
    sprintBurst.type = PerkType::Triggered;
    sprintBurst.effects.sprintSpeedPercent = 0.15F;
    sprintBurst.effects.effectDurationSeconds = 3.0F;
    RegisterPerk(sprintBurst);

    // Resilience: Faster vaulting
    PerkAsset resilience;
    resilience.id = "resilience";
    resilience.name = "Resilience";
    resilience.description = "Perform actions faster while injured";
    resilience.role = PerkRole::Survivor;
    resilience.type = PerkType::Conditional;
    resilience.effects.vaultSpeedPercent = 0.10F;
    resilience.effects.selfHealSpeedPercent = 0.10F;
    resilience.effects.altruisticHealSpeedPercent = -0.08F; // Faster healing = negative percent
    RegisterPerk(resilience);

    // Adrenaline: Heal and speed boost when exit gates powered
    PerkAsset adrenaline;
    adrenaline.id = "adrenaline";
    adrenaline.name = "Adrenaline";
    adrenaline.description = "Heal one health state and gain speed when generators are complete";
    adrenaline.role = PerkRole::Survivor;
    adrenaline.type = PerkType::Triggered;
    adrenaline.effects.sprintSpeedPercent = 0.20F;
    adrenaline.effects.effectDurationSeconds = 5.0F;
    RegisterPerk(adrenaline);

    // === KILLER PERKS ===

    // Brutal Strength: faster pallet breaking
    PerkAsset brutalStrength;
    brutalStrength.id = "brutal_strength";
    brutalStrength.name = "Brutal Strength";
    brutalStrength.description = "Break obstacles and damage generators faster";
    brutalStrength.role = PerkRole::Killer;
    brutalStrength.type = PerkType::Passive;
    brutalStrength.effects.palletBreakTimePercent = -0.15F;
    brutalStrength.effects.walkSpeedPercent = 0.02F;
    RegisterPerk(brutalStrength);

    // Terrifying Presence: larger terror radius
    PerkAsset terrifyingPresence;
    terrifyingPresence.id = "terrifying_presence";
    terrifyingPresence.name = "Terrifying Presence";
    terrifyingPresence.description = "Your presence looms over the survivors";
    terrifyingPresence.role = PerkRole::Killer;
    terrifyingPresence.type = PerkType::Passive;
    terrifyingPresence.effects.terrorRadiusMeters = 4.0F;
    RegisterPerk(terrifyingPresence);

    // Sloppy Butcher: slows healing
    PerkAsset sloppyButcher;
    sloppyButcher.id = "sloppy_butcher";
    sloppyButcher.name = "Sloppy Butcher";
    sloppyButcher.description = "Survivors suffer from blood loss and require more healing";
    sloppyButcher.role = PerkRole::Killer;
    sloppyButcher.type = PerkType::Passive;
    sloppyButcher.effects.selfHealSpeedPercent = 0.20F;
    sloppyButcher.effects.altruisticHealSpeedPercent = 0.20F;
    RegisterPerk(sloppyButcher);

    std::cout << "PerkSystem: Registered " << m_perkRegistry.size() << " default perks\n";
}

void PerkSystem::RegisterPerk(const PerkAsset& perk)
{
    if (m_perkRegistry.contains(perk.id))
    {
        std::cout << "PerkSystem: WARNING - Perk with id '" << perk.id << "' already registered, overwriting\n";
    }
    m_perkRegistry[perk.id] = perk;
}

const PerkAsset* PerkSystem::GetPerk(const std::string& id) const
{
    auto it = m_perkRegistry.find(id);
    if (it != m_perkRegistry.end())
    {
        return &it->second;
    }
    return nullptr;
}

std::vector<std::string> PerkSystem::ListPerks(PerkRole role) const
{
    std::vector<std::string> result;

    for (const auto& [id, perk] : m_perkRegistry)
    {
        if (role == PerkRole::Both || perk.role == role || perk.role == PerkRole::Both)
        {
            result.push_back(id);
        }
    }

    return result;
}

bool PerkSystem::HasPerk(const std::string& id) const
{
    return m_perkRegistry.contains(id);
}

void PerkSystem::SetDefaultDevLoadout()
{
    // Survivor: sprint_burst, self_care, iron_will
    {
        PerkLoadout survivorLoadout;
        survivorLoadout.SetPerk(0, "sprint_burst");
        survivorLoadout.SetPerk(1, "self_care");
        survivorLoadout.SetPerk(2, "iron_will");
        m_survivorLoadout = survivorLoadout;
        std::cout << "[PERKS] Set default survivor dev loadout (3 perks)\n";
    }
    
    // Killer: brutal_strength, terrifying_presence, sloppy_butcher
    {
        PerkLoadout killerLoadout;
        killerLoadout.SetPerk(0, "brutal_strength");
        killerLoadout.SetPerk(1, "terrifying_presence");
        killerLoadout.SetPerk(2, "sloppy_butcher");
        m_killerLoadout = killerLoadout;
        std::cout << "[PERKS] Set default killer dev loadout (3 perks)\n";
    }
    
    InitializeActiveStates();
}

void PerkSystem::InitializeActiveStates()
{
    m_activeSurvivorPerks.clear();
    m_activeKillerPerks.clear();

    // Initialize survivor active states
    for (const auto& perkId : m_survivorLoadout.perkIds)
    {
        if (!perkId.empty())
        {
            ActivePerkState state;
            state.perkId = perkId;
            state.currentStacks = 1;
            state.cooldownRemainingSeconds = 0.0F;
            state.activeRemainingSeconds = 0.0F;
            state.isActive = false;

            // Passive perks are always active
            const auto* perk = GetPerk(perkId);
            if (perk && perk->type == PerkType::Passive)
            {
                state.isActive = true;
            }

            m_activeSurvivorPerks.push_back(state);
        }
    }

    // Initialize killer active states
    for (const auto& perkId : m_killerLoadout.perkIds)
    {
        if (!perkId.empty())
        {
            ActivePerkState state;
            state.perkId = perkId;
            state.currentStacks = 1;
            state.cooldownRemainingSeconds = 0.0F;
            state.activeRemainingSeconds = 0.0F;
            state.isActive = false;

            // Passive perks are always active
            const auto* perk = GetPerk(perkId);
            if (perk && perk->type == PerkType::Passive)
            {
                state.isActive = true;
            }

            m_activeKillerPerks.push_back(state);
        }
    }

    std::cout << "PerkSystem: Initialized " << m_activeSurvivorPerks.size()
              << " survivor and " << m_activeKillerPerks.size()
              << " killer active perk states\n";
}

void PerkSystem::UpdateActiveStates(float fixedDt)
{
    auto updateState = [&](std::vector<ActivePerkState>& states) {
        for (auto& state : states)
        {
            // Update cooldown
            if (state.cooldownRemainingSeconds > 0.0F)
            {
                state.cooldownRemainingSeconds = std::max(0.0F, state.cooldownRemainingSeconds - fixedDt);
            }

            // Update active duration
            if (state.activeRemainingSeconds > 0.0F)
            {
                state.activeRemainingSeconds = std::max(0.0F, state.activeRemainingSeconds - fixedDt);
                if (state.activeRemainingSeconds <= kEpsilon)
                {
                    state.isActive = false;

                    const auto* perk = GetPerk(state.perkId);
                    if (perk)
                    {
                        state.cooldownRemainingSeconds = perk->effects.activationCooldownSeconds;
                    }
                }
            }
        }
    };

    updateState(m_activeSurvivorPerks);
    updateState(m_activeKillerPerks);
}

void PerkSystem::ActivatePerk(const std::string& perkId, engine::scene::Role role)
{
    auto& states = (role == engine::scene::Role::Survivor) ? m_activeSurvivorPerks : m_activeKillerPerks;

    for (auto& state : states)
    {
        if (state.perkId == perkId)
        {
            if (state.cooldownRemainingSeconds > kEpsilon)
            {
                std::cout << "PerkSystem: WARNING - Cannot activate perk '" << perkId << "' - cooldown active (" << state.cooldownRemainingSeconds << "s)\n";
                return;
            }

            const auto* perk = GetPerk(perkId);
            if (!perk)
            {
                std::cout << "PerkSystem: ERROR - Cannot activate perk '" << perkId << "' - not found in registry\n";
                return;
            }

            if (perk->type == PerkType::Passive)
            {
                std::cout << "PerkSystem: WARNING - Cannot activate passive perk '" << perkId << "'\n";
                return;
            }

            state.isActive = true;
            state.activeRemainingSeconds = perk->effects.effectDurationSeconds > kEpsilon ? perk->effects.effectDurationSeconds : 0.0F;

            if (state.currentStacks <= 0)
            {
                state.currentStacks = 1;
            }

            std::cout << "PerkSystem: Activated perk '" << perkId << "' for role " << static_cast<int>(role) << "\n";
            return;
        }
    }

    std::cout << "PerkSystem: WARNING - Cannot activate perk '" << perkId << "' - not in loadout\n";
}

void PerkSystem::DeactivatePerk(const std::string& perkId, engine::scene::Role role)
{
    auto& states = (role == engine::scene::Role::Survivor) ? m_activeSurvivorPerks : m_activeKillerPerks;

    for (auto& state : states)
    {
        if (state.perkId == perkId)
        {
            state.isActive = false;
            state.activeRemainingSeconds = 0.0F;
            std::cout << "PerkSystem: Deactivated perk '" << perkId << "' for role " << static_cast<int>(role) << "\n";
            return;
        }
    }

    std::cout << "PerkSystem: WARNING - Cannot deactivate perk '" << perkId << "' - not in loadout\n";
}

bool PerkSystem::IsPerkActive(const std::string& perkId, engine::scene::Role role) const
{
    const auto& states = (role == engine::scene::Role::Survivor) ? m_activeSurvivorPerks : m_activeKillerPerks;

    for (const auto& state : states)
    {
        if (state.perkId == perkId)
        {
            return state.isActive;
        }
    }

    return false;
}

PerkEffect PerkSystem::GetTotalEffects(engine::scene::Role role) const
{
    PerkEffect total;

    const auto& states = (role == engine::scene::Role::Survivor) ? m_activeSurvivorPerks : m_activeKillerPerks;

    for (const auto& state : states)
    {
        if (!state.isActive)
        {
            continue;
        }

        const auto* perk = GetPerk(state.perkId);
        if (!perk)
        {
            continue;
        }

        // Apply stacking based on perk's max stacks
        const float stackMultiplier = static_cast<float>(state.currentStacks) / static_cast<float>(perk->maxStacks);

        total.walkSpeedPercent += perk->effects.walkSpeedPercent * stackMultiplier;
        total.sprintSpeedPercent += perk->effects.sprintSpeedPercent * stackMultiplier;
        total.crouchSpeedPercent += perk->effects.crouchSpeedPercent * stackMultiplier;
        total.crawlSpeedPercent += perk->effects.crawlSpeedPercent * stackMultiplier;
        total.vaultSpeedPercent += perk->effects.vaultSpeedPercent * stackMultiplier;
        total.terrorRadiusMeters += perk->effects.terrorRadiusMeters * stackMultiplier;
        total.palletBreakTimePercent += perk->effects.palletBreakTimePercent * stackMultiplier;
        total.palletDropTimePercent += perk->effects.palletDropTimePercent * stackMultiplier;
        total.selfHealSpeedPercent += perk->effects.selfHealSpeedPercent * stackMultiplier;
        total.altruisticHealSpeedPercent += perk->effects.altruisticHealSpeedPercent * stackMultiplier;
        total.repairSpeedPercent += perk->effects.repairSpeedPercent * stackMultiplier;
        total.skillCheckZonePercent += perk->effects.skillCheckZonePercent * stackMultiplier;
    }

    return total;
}

float PerkSystem::GetSpeedModifier(engine::scene::Role role, bool sprinting, bool crouching, bool crawling) const
{
    const PerkEffect effects = GetTotalEffects(role);

    float modifier = 1.0F;

    if (sprinting)
    {
        modifier += effects.sprintSpeedPercent;
    }
    else if (crawling)
    {
        modifier += effects.crawlSpeedPercent;
    }
    else if (crouching)
    {
        modifier += effects.crouchSpeedPercent;
    }
    else
    {
        modifier += effects.walkSpeedPercent;
    }

    return std::max(0.1F, modifier); // Ensure at least 10% speed
}

float PerkSystem::GetTerrorRadiusModifier(engine::scene::Role role) const
{
    const PerkEffect effects = GetTotalEffects(role);
    return effects.terrorRadiusMeters;
}

float PerkSystem::GetVaultSpeedModifier(engine::scene::Role role) const
{
    const PerkEffect effects = GetTotalEffects(role);
    return 1.0F / (1.0F - effects.vaultSpeedPercent); // Inverse: +10% speed = 1/0.9 ≈ 1.11x faster
}

float PerkSystem::GetPalletBreakModifier(engine::scene::Role role) const
{
    const PerkEffect effects = GetTotalEffects(role);
    return 1.0F / (1.0F - effects.palletBreakTimePercent); // Inverse for faster breaking
}

float PerkSystem::GetHealSpeedModifier(engine::scene::Role role, bool selfHeal) const
{
    const PerkEffect effects = GetTotalEffects(role);

    // Negative percent = faster healing, so we invert it
    const float healModifier = selfHeal ? effects.selfHealSpeedPercent : effects.altruisticHealSpeedPercent;
    return 1.0F / (1.0F - healModifier);
}

const std::vector<ActivePerkState>& PerkSystem::GetActivePerks(engine::scene::Role role) const
{
    const auto& states = (role == engine::scene::Role::Survivor) ? m_activeSurvivorPerks : m_activeKillerPerks;
    return states;
}

float PerkSystem::GetRepairSpeedModifier(engine::scene::Role role) const
{
    const PerkEffect effects = GetTotalEffects(role);
    return 1.0F + effects.repairSpeedPercent;
}

bool PerkSystem::LoadPerksFromJson(const std::string& jsonPath)
{
    std::ifstream file(jsonPath);
    if (!file.is_open())
    {
        std::cout << "PerkSystem: WARNING - Could not open perks.json at '" << jsonPath << "'\n";
        return false;
    }

    try
    {
        nlohmann::json root;
        file >> root;

        const int assetVersion = root.value("asset_version", 0);
        if (assetVersion != 1)
        {
            std::cout << "PerkSystem: WARNING - Unexpected asset version " << assetVersion << ", expected 1\n";
        }

        if (!root.contains("perks"))
        {
            std::cout << "PerkSystem: WARNING - No 'perks' array found in JSON\n";
            return false;
        }

        int loaded = 0;
        for (const auto& perkJson : root["perks"])
        {
            PerkAsset perk;
            perk.id = perkJson.value("id", "");
            perk.name = perkJson.value("name", "");
            perk.description = perkJson.value("description", "");

            const std::string roleStr = perkJson.value("role", "both");
            if (roleStr == "survivor")
                perk.role = PerkRole::Survivor;
            else if (roleStr == "killer")
                perk.role = PerkRole::Killer;
            else
                perk.role = PerkRole::Both;

            const std::string typeStr = perkJson.value("type", "passive");
            if (typeStr == "passive")
                perk.type = PerkType::Passive;
            else if (typeStr == "triggered")
                perk.type = PerkType::Triggered;
            else
                perk.type = PerkType::Conditional;

            // Load effects if present
            if (perkJson.contains("effects"))
            {
                const auto& effectsJson = perkJson["effects"];
                auto& e = perk.effects;
                e.walkSpeedPercent = effectsJson.value("walk_speed_percent", 0.0F);
                e.sprintSpeedPercent = effectsJson.value("sprint_speed_percent", 0.0F);
                e.crouchSpeedPercent = effectsJson.value("crouch_speed_percent", 0.0F);
                e.crawlSpeedPercent = effectsJson.value("crawl_speed_percent", 0.0F);
                e.vaultSpeedPercent = effectsJson.value("vault_speed_percent", 0.0F);
                e.terrorRadiusMeters = effectsJson.value("terror_radius_meters", 0.0F);
                e.palletBreakTimePercent = effectsJson.value("pallet_break_time_percent", 0.0F);
                e.palletDropTimePercent = effectsJson.value("pallet_drop_time_percent", 0.0F);
                e.selfHealSpeedPercent = effectsJson.value("self_heal_speed_percent", 0.0F);
                e.altruisticHealSpeedPercent = effectsJson.value("altruistic_heal_speed_percent", 0.0F);
                e.repairSpeedPercent = effectsJson.value("repair_speed_percent", 0.0F);
                e.skillCheckZonePercent = effectsJson.value("skill_check_zone_percent", 0.0F);
                e.activationCooldownSeconds = effectsJson.value("activation_cooldown_seconds", 0.0F);
                e.effectDurationSeconds = effectsJson.value("effect_duration_seconds", 0.0F);
            }

            perk.maxStacks = perkJson.value("max_stacks", 1);

            if (!perk.id.empty())
            {
                RegisterPerk(perk);
                ++loaded;
            }
        }

        std::cout << "PerkSystem: Loaded " << loaded << " perks from " << jsonPath << "\n";
        return true;
    }
    catch (const std::exception& e)
    {
        std::cout << "PerkSystem: ERROR - Failed to load perks.json: " << e.what() << "\n";
        return false;
    }
}

bool PerkSystem::SavePerksToJson(const std::string& jsonPath) const
{
    try
    {
        nlohmann::json root;
        root["asset_version"] = 1;

        nlohmann::json perksArray = nlohmann::json::array();
        for (const auto& [id, perk] : m_perkRegistry)
        {
            nlohmann::json perkJson;
            perkJson["id"] = perk.id;
            perkJson["name"] = perk.name;
            perkJson["description"] = perk.description;

            perkJson["role"] = PerkRoleToText(perk.role);
            perkJson["type"] = PerkTypeToText(perk.type);
            perkJson["max_stacks"] = perk.maxStacks;

            nlohmann::json effectsJson;
            auto& e = perk.effects;
            effectsJson["walk_speed_percent"] = e.walkSpeedPercent;
            effectsJson["sprint_speed_percent"] = e.sprintSpeedPercent;
            effectsJson["crouch_speed_percent"] = e.crouchSpeedPercent;
            effectsJson["crawl_speed_percent"] = e.crawlSpeedPercent;
            effectsJson["vault_speed_percent"] = e.vaultSpeedPercent;
            effectsJson["terror_radius_meters"] = e.terrorRadiusMeters;
            effectsJson["pallet_break_time_percent"] = e.palletBreakTimePercent;
            effectsJson["pallet_drop_time_percent"] = e.palletDropTimePercent;
            effectsJson["self_heal_speed_percent"] = e.selfHealSpeedPercent;
            effectsJson["altruistic_heal_speed_percent"] = e.altruisticHealSpeedPercent;
            effectsJson["repair_speed_percent"] = e.repairSpeedPercent;
            effectsJson["skill_check_zone_percent"] = e.skillCheckZonePercent;
            effectsJson["activation_cooldown_seconds"] = e.activationCooldownSeconds;
            effectsJson["effect_duration_seconds"] = e.effectDurationSeconds;

            perkJson["effects"] = effectsJson;
            perksArray.push_back(perkJson);
        }

        root["perks"] = perksArray;

        std::ofstream file(jsonPath);
        if (!file.is_open())
        {
            std::cout << "PerkSystem: ERROR - Could not open '" << jsonPath << "' for writing\n";
            return false;
        }

        file << root.dump(2);
        std::cout << "PerkSystem: Saved " << m_perkRegistry.size() << " perks to " << jsonPath << "\n";
        return true;
    }
    catch (const std::exception& e)
    {
        std::cout << "PerkSystem: ERROR - Failed to save perks.json: " << e.what() << "\n";
        return false;
    }
}

const char* PerkTypeToText(PerkType type)
{
    switch (type)
    {
        case PerkType::Passive: return "passive";
        case PerkType::Triggered: return "triggered";
        case PerkType::Conditional: return "conditional";
        default: return "unknown";
    }
}

const char* PerkRoleToText(PerkRole role)
{
    switch (role)
    {
        case PerkRole::Survivor: return "survivor";
        case PerkRole::Killer: return "killer";
        case PerkRole::Both: return "both";
        default: return "unknown";
    }
}
} // namespace game::gameplay::perks
