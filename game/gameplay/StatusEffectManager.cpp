#include "game/gameplay/StatusEffectManager.hpp"

#include <algorithm>

namespace game::gameplay
{

void StatusEffectManager::ApplyEffect(engine::scene::Entity entity, const StatusEffect& effect)
{
    auto& effects = m_entityEffects[entity];

    // Check if we already have this effect type from this source
    auto it = std::find_if(effects.begin(), effects.end(), [&](const StatusEffect& e) {
        return e.type == effect.type && e.sourceId == effect.sourceId;
    });

    if (it != effects.end())
    {
        // Refresh existing effect
        *it = effect;
    }
    else
    {
        // Add new effect
        effects.push_back(effect);
    }
}

void StatusEffectManager::RemoveEffect(engine::scene::Entity entity, StatusEffectType type)
{
    auto entityIt = m_entityEffects.find(entity);
    if (entityIt == m_entityEffects.end())
    {
        return;
    }

    auto& effects = entityIt->second;
    effects.erase(
        std::remove_if(effects.begin(), effects.end(),
            [type](const StatusEffect& e) { return e.type == type; }),
        effects.end()
    );
}

void StatusEffectManager::RemoveEffectBySource(engine::scene::Entity entity, const std::string& sourceId)
{
    auto entityIt = m_entityEffects.find(entity);
    if (entityIt == m_entityEffects.end())
    {
        return;
    }

    auto& effects = entityIt->second;
    effects.erase(
        std::remove_if(effects.begin(), effects.end(),
            [&sourceId](const StatusEffect& e) { return e.sourceId == sourceId; }),
        effects.end()
    );
}

void StatusEffectManager::ClearEffects(engine::scene::Entity entity)
{
    m_entityEffects.erase(entity);
}

std::vector<StatusEffect> StatusEffectManager::GetActiveEffects(engine::scene::Entity entity) const
{
    auto entityIt = m_entityEffects.find(entity);
    if (entityIt == m_entityEffects.end())
    {
        return {};
    }
    return entityIt->second;
}

bool StatusEffectManager::HasEffect(engine::scene::Entity entity, StatusEffectType type) const
{
    auto entityIt = m_entityEffects.find(entity);
    if (entityIt == m_entityEffects.end())
    {
        return false;
    }

    const auto& effects = entityIt->second;
    return std::any_of(effects.begin(), effects.end(),
        [type](const StatusEffect& e) { return e.type == type; });
}

const StatusEffect* StatusEffectManager::GetEffect(engine::scene::Entity entity, StatusEffectType type) const
{
    auto entityIt = m_entityEffects.find(entity);
    if (entityIt == m_entityEffects.end())
    {
        return nullptr;
    }

    const auto& effects = entityIt->second;
    auto it = std::find_if(effects.begin(), effects.end(),
        [type](const StatusEffect& e) { return e.type == type; });

    return it != effects.end() ? &(*it) : nullptr;
}

float StatusEffectManager::GetTotalSpeedModifier(engine::scene::Entity entity) const
{
    auto entityIt = m_entityEffects.find(entity);
    if (entityIt == m_entityEffects.end())
    {
        return 1.0F;
    }

    float totalModifier = 1.0F;

    for (const auto& effect : entityIt->second)
    {
        if (effect.type == StatusEffectType::Haste)
        {
            // Haste is multiplicative (e.g., +15% = 1.15)
            totalModifier *= (1.0F + effect.strength);
        }
        else if (effect.type == StatusEffectType::Hindered)
        {
            // Hindered is multiplicative (e.g., -15% = 0.85)
            // Strength is stored as negative for hindered
            totalModifier *= (1.0F + effect.strength);
        }
    }

    return totalModifier;
}

bool StatusEffectManager::IsUndetectable(engine::scene::Entity entity) const
{
    return HasEffect(entity, StatusEffectType::Undetectable);
}

bool StatusEffectManager::IsExposed(engine::scene::Entity entity) const
{
    return HasEffect(entity, StatusEffectType::Exposed);
}

bool StatusEffectManager::IsExhausted(engine::scene::Entity entity) const
{
    return HasEffect(entity, StatusEffectType::Exhausted);
}

void StatusEffectManager::Update(float deltaSeconds)
{
    for (auto& [entity, effects] : m_entityEffects)
    {
        // Update timers and remove expired effects
        effects.erase(
            std::remove_if(effects.begin(), effects.end(),
                [deltaSeconds](StatusEffect& e) {
                    if (!e.infinite)
                    {
                        e.remainingTime -= deltaSeconds;
                        return e.IsExpired();
                    }
                    return false;
                }),
            effects.end()
        );
    }
}

std::size_t StatusEffectManager::GetTotalActiveEffectCount() const
{
    std::size_t count = 0;
    for (const auto& [entity, effects] : m_entityEffects)
    {
        count += effects.size();
    }
    return count;
}

void StatusEffectManager::ClearAll()
{
    m_entityEffects.clear();
}

std::vector<StatusEffect>::iterator StatusEffectManager::FindEffect(
    engine::scene::Entity entity,
    StatusEffectType type)
{
    auto entityIt = m_entityEffects.find(entity);
    if (entityIt == m_entityEffects.end())
    {
        // Return end iterator from a dummy vector - this method is not used directly
        static std::vector<StatusEffect> dummy;
        return dummy.end();
    }

    auto& effects = entityIt->second;
    return std::find_if(effects.begin(), effects.end(),
        [type](const StatusEffect& e) { return e.type == type; });
}

std::vector<StatusEffect>::const_iterator StatusEffectManager::FindEffect(
    engine::scene::Entity entity,
    StatusEffectType type) const
{
    auto entityIt = m_entityEffects.find(entity);
    if (entityIt == m_entityEffects.end())
    {
        // Return end iterator from a dummy vector
        static const std::vector<StatusEffect> empty;
        return empty.end();
    }

    const auto& effects = entityIt->second;
    return std::find_if(effects.begin(), effects.end(),
        [type](const StatusEffect& e) { return e.type == type; });
}

std::vector<StatusEffect>::iterator StatusEffectManager::FindEffectBySource(
    engine::scene::Entity entity,
    const std::string& sourceId)
{
    auto entityIt = m_entityEffects.find(entity);
    if (entityIt == m_entityEffects.end())
    {
        static std::vector<StatusEffect> dummy;
        return dummy.end();
    }

    auto& effects = entityIt->second;
    return std::find_if(effects.begin(), effects.end(),
        [&sourceId](const StatusEffect& e) { return e.sourceId == sourceId; });
}

}  // namespace game::gameplay
