#pragma once

#include <vector>
#include <unordered_map>

#include "game/gameplay/StatusEffect.hpp"
#include "engine/scene/World.hpp"

namespace game::gameplay
{

/// Manages status effects for all entities in the game.
/// Supports adding, removing, updating, and querying effects.
class StatusEffectManager
{
public:
    StatusEffectManager() = default;

    /// Apply a new effect or refresh an existing one of the same type + source.
    /// @param entity The entity to apply the effect to.
    /// @param effect The effect to apply.
    void ApplyEffect(engine::scene::Entity entity, const StatusEffect& effect);

    /// Remove a specific effect type from an entity.
    /// @param entity The entity to remove the effect from.
    /// @param type The type of effect to remove.
    void RemoveEffect(engine::scene::Entity entity, StatusEffectType type);

    /// Remove all effects from an entity that were caused by a specific source.
    /// @param entity The entity to remove effects from.
    /// @param sourceId The source ID to match.
    void RemoveEffectBySource(engine::scene::Entity entity, const std::string& sourceId);

    /// Remove all effects from an entity.
    /// @param entity The entity to clear.
    void ClearEffects(engine::scene::Entity entity);

    /// Get all active effects for an entity.
    /// @param entity The entity to query.
    /// @return Vector of active effects (may be empty).
    [[nodiscard]] std::vector<StatusEffect> GetActiveEffects(engine::scene::Entity entity) const;

    /// Check if an entity has a specific effect type active.
    /// @param entity The entity to check.
    /// @param type The effect type to look for.
    /// @return True if the entity has this effect type active.
    [[nodiscard]] bool HasEffect(engine::scene::Entity entity, StatusEffectType type) const;

    /// Get a specific effect from an entity.
    /// @param entity The entity to query.
    /// @param type The effect type to get.
    /// @return Pointer to the effect if found, nullptr otherwise.
    [[nodiscard]] const StatusEffect* GetEffect(engine::scene::Entity entity, StatusEffectType type) const;

    /// Get the total speed modifier from Haste/Hindered effects.
    /// @param entity The entity to query.
    /// @return Multiplier (1.0 = no change, >1.0 = haste, <1.0 = hindered).
    [[nodiscard]] float GetTotalSpeedModifier(engine::scene::Entity entity) const;

    /// Check if an entity is Undetectable (killer-only).
    /// @param entity The entity to check.
    /// @return True if undetectable.
    [[nodiscard]] bool IsUndetectable(engine::scene::Entity entity) const;

    /// Check if an entity is Exposed (survivor-only).
    /// @param entity The entity to check.
    /// @return True if exposed.
    [[nodiscard]] bool IsExposed(engine::scene::Entity entity) const;

    /// Check if an entity is Exhausted (survivor-only).
    /// @param entity The entity to check.
    /// @return True if exhausted.
    [[nodiscard]] bool IsExhausted(engine::scene::Entity entity) const;

    /// Update all effects (tick timers, remove expired).
    /// @param deltaSeconds Time elapsed since last update.
    void Update(float deltaSeconds);

    /// Get the number of active effects across all entities.
    /// @return Total count of active effects.
    [[nodiscard]] std::size_t GetTotalActiveEffectCount() const;

    /// Clear all effects for all entities.
    void ClearAll();

private:
    /// Per-entity effect storage.
    /// Using a map of entity -> vector of effects for O(1) entity lookup.
    std::unordered_map<engine::scene::Entity, std::vector<StatusEffect>> m_entityEffects;

    /// Find an effect iterator by type for an entity.
    /// @return Iterator to the effect, or end() if not found.
    std::vector<StatusEffect>::iterator FindEffect(engine::scene::Entity entity, StatusEffectType type);
    std::vector<StatusEffect>::const_iterator FindEffect(engine::scene::Entity entity, StatusEffectType type) const;

    /// Find an effect iterator by source for an entity.
    /// @return Iterator to the effect, or end() if not found.
    std::vector<StatusEffect>::iterator FindEffectBySource(engine::scene::Entity entity, const std::string& sourceId);
};

}  // namespace game::gameplay
