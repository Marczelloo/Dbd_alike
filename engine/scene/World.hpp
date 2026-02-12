#pragma once

#include <unordered_map>
#include <vector>

#include "engine/scene/Components.hpp"

namespace engine::scene
{
class World
{
public:
    Entity CreateEntity();
    void Clear();

    [[nodiscard]] bool HasEntity(Entity entity) const;

    std::unordered_map<Entity, Transform>& Transforms() { return m_transforms; }
    std::unordered_map<Entity, ActorComponent>& Actors() { return m_actors; }
    std::unordered_map<Entity, StaticBoxComponent>& StaticBoxes() { return m_staticBoxes; }
    std::unordered_map<Entity, WindowComponent>& Windows() { return m_windows; }
    std::unordered_map<Entity, PalletComponent>& Pallets() { return m_pallets; }
    std::unordered_map<Entity, HookComponent>& Hooks() { return m_hooks; }
    std::unordered_map<Entity, GeneratorComponent>& Generators() { return m_generators; }
    std::unordered_map<Entity, BearTrapComponent>& BearTraps() { return m_bearTraps; }
    std::unordered_map<Entity, DebugColorComponent>& DebugColors() { return m_debugColors; }
    std::unordered_map<Entity, NameComponent>& Names() { return m_names; }

    [[nodiscard]] const std::unordered_map<Entity, Transform>& Transforms() const { return m_transforms; }
    [[nodiscard]] const std::unordered_map<Entity, ActorComponent>& Actors() const { return m_actors; }
    [[nodiscard]] const std::unordered_map<Entity, StaticBoxComponent>& StaticBoxes() const { return m_staticBoxes; }
    [[nodiscard]] const std::unordered_map<Entity, WindowComponent>& Windows() const { return m_windows; }
    [[nodiscard]] const std::unordered_map<Entity, PalletComponent>& Pallets() const { return m_pallets; }
    [[nodiscard]] const std::unordered_map<Entity, HookComponent>& Hooks() const { return m_hooks; }
    [[nodiscard]] const std::unordered_map<Entity, GeneratorComponent>& Generators() const { return m_generators; }
    [[nodiscard]] const std::unordered_map<Entity, BearTrapComponent>& BearTraps() const { return m_bearTraps; }
    [[nodiscard]] const std::unordered_map<Entity, DebugColorComponent>& DebugColors() const { return m_debugColors; }
    [[nodiscard]] const std::unordered_map<Entity, NameComponent>& Names() const { return m_names; }

    [[nodiscard]] std::vector<Entity> Entities() const;

private:
    Entity m_nextEntity = 1;
    std::unordered_map<Entity, Transform> m_transforms;
    std::unordered_map<Entity, ActorComponent> m_actors;
    std::unordered_map<Entity, StaticBoxComponent> m_staticBoxes;
    std::unordered_map<Entity, WindowComponent> m_windows;
    std::unordered_map<Entity, PalletComponent> m_pallets;
    std::unordered_map<Entity, HookComponent> m_hooks;
    std::unordered_map<Entity, GeneratorComponent> m_generators;
    std::unordered_map<Entity, BearTrapComponent> m_bearTraps;
    std::unordered_map<Entity, DebugColorComponent> m_debugColors;
    std::unordered_map<Entity, NameComponent> m_names;
};
} // namespace engine::scene
