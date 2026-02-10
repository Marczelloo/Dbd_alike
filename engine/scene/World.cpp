#include "engine/scene/World.hpp"

#include <unordered_set>

namespace engine::scene
{
Entity World::CreateEntity()
{
    return m_nextEntity++;
}

void World::Clear()
{
    m_nextEntity = 1;
    m_transforms.clear();
    m_actors.clear();
    m_staticBoxes.clear();
    m_windows.clear();
    m_pallets.clear();
    m_hooks.clear();
    m_generators.clear();
    m_debugColors.clear();
    m_names.clear();
}

bool World::HasEntity(Entity entity) const
{
    return m_transforms.contains(entity) || m_actors.contains(entity) || m_staticBoxes.contains(entity) ||
           m_windows.contains(entity) || m_pallets.contains(entity) || m_hooks.contains(entity) ||
           m_generators.contains(entity) ||
           m_debugColors.contains(entity) ||
           m_names.contains(entity);
}

std::vector<Entity> World::Entities() const
{
    std::unordered_set<Entity> dedup;
    dedup.reserve(
        m_transforms.size() + m_actors.size() + m_staticBoxes.size() + m_windows.size() + m_pallets.size() + m_hooks.size() + m_generators.size()
    );

    auto collect = [&dedup](const auto& map) {
        for (const auto& [entity, _] : map)
        {
            dedup.insert(entity);
        }
    };

    collect(m_transforms);
    collect(m_actors);
    collect(m_staticBoxes);
    collect(m_windows);
    collect(m_pallets);
    collect(m_hooks);
    collect(m_generators);
    collect(m_debugColors);
    collect(m_names);

    return {dedup.begin(), dedup.end()};
}
} // namespace engine::scene
