#pragma once

#include <cstdint>
#include <string>

namespace game::gameplay
{

/// Status effect types for both killers and survivors.
/// Each effect has specific gameplay implications.
enum class StatusEffectType : std::uint8_t
{
    Undetectable = 0,  ///< Killer: No terror radius, no killer light
    Haste,             ///< Both: +speed%
    Hindered,          ///< Both: -speed%
    Bloodlust,         ///< Killer: Chase speed bonus (migrated from existing system)
    Exhausted,         ///< Survivor: Cannot use exhaustion perks
    Exposed,           ///< Survivor: One-hit down from any health state
    Count              ///< Total count of effect types
};

/// Single active status effect instance.
struct StatusEffect
{
    StatusEffectType type = StatusEffectType::Haste;
    std::string sourceId;           ///< What caused this (perk, power, etc.)
    float duration = 0.0F;          ///< Total duration (0 = indefinite)
    float remainingTime = 0.0F;     ///< Time remaining
    float strength = 0.0F;          ///< Effect magnitude (e.g., 0.15 = 15% haste)
    int stacks = 1;                 ///< Number of stacks (for stackable effects)
    bool infinite = false;          ///< Doesn't tick down

    /// Check if this effect has expired.
    [[nodiscard]] bool IsExpired() const
    {
        if (infinite)
        {
            return false;
        }
        return remainingTime <= 0.0F;
    }

    /// Get progress as 0-1 for UI display.
    [[nodiscard]] float Progress01() const
    {
        if (infinite || duration <= 0.0F)
        {
            return 1.0F;  // Full for infinite effects
        }
        return remainingTime / duration;
    }

    /// Get display name for this effect type.
    [[nodiscard]] static const char* TypeToName(StatusEffectType type)
    {
        switch (type)
        {
            case StatusEffectType::Undetectable: return "Undetectable";
            case StatusEffectType::Haste: return "Haste";
            case StatusEffectType::Hindered: return "Hindered";
            case StatusEffectType::Bloodlust: return "Bloodlust";
            case StatusEffectType::Exhausted: return "Exhausted";
            case StatusEffectType::Exposed: return "Exposed";
            default: return "Unknown";
        }
    }

    /// Get short type ID string for serialization/UI.
    [[nodiscard]] static const char* TypeToId(StatusEffectType type)
    {
        switch (type)
        {
            case StatusEffectType::Undetectable: return "undetectable";
            case StatusEffectType::Haste: return "haste";
            case StatusEffectType::Hindered: return "hindered";
            case StatusEffectType::Bloodlust: return "bloodlust";
            case StatusEffectType::Exhausted: return "exhausted";
            case StatusEffectType::Exposed: return "exposed";
            default: return "unknown";
        }
    }

    /// Parse type from string.
    [[nodiscard]] static StatusEffectType ParseType(const std::string& str)
    {
        if (str == "undetectable") return StatusEffectType::Undetectable;
        if (str == "haste") return StatusEffectType::Haste;
        if (str == "hindered") return StatusEffectType::Hindered;
        if (str == "bloodlust") return StatusEffectType::Bloodlust;
        if (str == "exhausted") return StatusEffectType::Exhausted;
        if (str == "exposed") return StatusEffectType::Exposed;
        return StatusEffectType::Haste;  // Default fallback
    }

    /// Check if this effect type is killer-only.
    [[nodiscard]] static bool IsKillerOnly(StatusEffectType type)
    {
        return type == StatusEffectType::Undetectable || type == StatusEffectType::Bloodlust;
    }

    /// Check if this effect type is survivor-only.
    [[nodiscard]] static bool IsSurvivorOnly(StatusEffectType type)
    {
        return type == StatusEffectType::Exhausted || type == StatusEffectType::Exposed;
    }
};

/// Lightweight snapshot of a status effect for network replication.
struct StatusEffectSnapshot
{
    std::uint8_t type = 0;
    float remainingTime = 0.0F;
    float strength = 0.0F;
    std::uint8_t stacks = 1;
    std::uint8_t isInfinite = 0;
};

}  // namespace game::gameplay
