#pragma once

#include <cstdint>
#include <optional>
#include <unordered_map>
#include <vector>

#include <glm/vec3.hpp>

#include "engine/scene/Components.hpp"

namespace engine::physics
{
enum class CollisionLayer
{
    Player,
    Environment,
    Interactable
};

enum class TriggerKind
{
    Vault,
    Interaction,
    Chase
};

struct SolidBox
{
    engine::scene::Entity entity = 0;
    glm::vec3 center{0.0F};
    glm::vec3 halfExtents{0.5F};
    CollisionLayer layer = CollisionLayer::Environment;
    bool blocksSight = true;
};

struct TriggerVolume
{
    engine::scene::Entity entity = 0;
    glm::vec3 center{0.0F};
    glm::vec3 halfExtents{0.5F};
    TriggerKind kind = TriggerKind::Interaction;
};

struct TriggerHit
{
    engine::scene::Entity entity = 0;
    TriggerKind kind = TriggerKind::Interaction;
};

struct TriggerCastHit
{
    engine::scene::Entity entity = 0;
    TriggerKind kind = TriggerKind::Interaction;
    float t = 1.0F;
    glm::vec3 position{0.0F};
};

struct RaycastHit
{
    engine::scene::Entity entity = 0;
    float t = 1.0F;
    glm::vec3 position{0.0F};
    glm::vec3 normal{0.0F, 1.0F, 0.0F};
};

struct MoveResult
{
    glm::vec3 position{0.0F};
    bool collided = false;
    bool grounded = false;
    bool steppedUp = false;
    glm::vec3 lastCollisionNormal{0.0F, 1.0F, 0.0F};
    float maxPenetrationDepth = 0.0F;
};

class PhysicsWorld
{
public:
    void Clear();

    void AddSolidBox(const SolidBox& box);
    void AddTrigger(const TriggerVolume& trigger);

    /// Update center of an existing trigger by entity. Returns true if found.
    bool UpdateTriggerCenter(engine::scene::Entity entity, const glm::vec3& newCenter);

    [[nodiscard]] const std::vector<SolidBox>& Solids() const { return m_solids; }
    [[nodiscard]] const std::vector<TriggerVolume>& Triggers() const { return m_triggers; }

    [[nodiscard]] MoveResult MoveCapsule(
        const glm::vec3& currentPosition,
        float radius,
        float capsuleHeight,
        const glm::vec3& desiredDelta,
        bool collisionEnabled,
        float stepHeight
    ) const;

    [[nodiscard]] bool HasLineOfSight(const glm::vec3& from, const glm::vec3& to, engine::scene::Entity ignoreEntity = 0) const;
    [[nodiscard]] bool RaycastAny(const glm::vec3& from, const glm::vec3& to, engine::scene::Entity ignoreEntity = 0) const;
    [[nodiscard]] std::optional<RaycastHit> RaycastNearest(
        const glm::vec3& from,
        const glm::vec3& to,
        engine::scene::Entity ignoreEntity = 0
    ) const;

    [[nodiscard]] std::vector<TriggerHit> QueryCapsuleTriggers(
        const glm::vec3& position,
        float radius,
        float capsuleHeight,
        TriggerKind kind
    ) const;

    /// Output-parameter version that reuses caller's buffer (avoids heap allocation).
    void QueryCapsuleTriggers(
        std::vector<TriggerHit>& out,
        const glm::vec3& position,
        float radius,
        float capsuleHeight,
        TriggerKind kind
    ) const;

    [[nodiscard]] std::vector<TriggerCastHit> SphereCastTriggers(
        const glm::vec3& from,
        const glm::vec3& to,
        float radius
    ) const;

    /// Output-parameter version that reuses caller's buffer (avoids heap allocation).
    void SphereCastTriggers(
        std::vector<TriggerCastHit>& out,
        const glm::vec3& from,
        const glm::vec3& to,
        float radius
    ) const;

private:
    struct CellKey
    {
        int x = 0;
        int y = 0;
        int z = 0;

        [[nodiscard]] bool operator==(const CellKey& other) const
        {
            return x == other.x && y == other.y && z == other.z;
        }
    };

    struct CellKeyHash
    {
        [[nodiscard]] std::size_t operator()(const CellKey& key) const
        {
            const std::size_t hx = static_cast<std::size_t>(key.x) * 73856093U;
            const std::size_t hy = static_cast<std::size_t>(key.y) * 19349663U;
            const std::size_t hz = static_cast<std::size_t>(key.z) * 83492791U;
            return hx ^ hy ^ hz;
        }
    };

    void RebuildSpatialIndex() const;
    void AppendSolidCandidates(const glm::vec3& minBounds, const glm::vec3& maxBounds, std::vector<std::size_t>& outIndices) const;

    static bool SphereIntersectsExpandedAabb(
        const glm::vec3& center,
        float radius,
        const SolidBox& box,
        float capsuleHalfSegment,
        glm::vec3* outNormal,
        float* outPenetration
    );

    static bool SegmentIntersectsAabb3D(
        const glm::vec3& from,
        const glm::vec3& to,
        const glm::vec3& minBounds,
        const glm::vec3& maxBounds,
        float* outT,
        glm::vec3* outNormal
    );

    MoveResult ResolveCapsulePosition(
        const glm::vec3& candidatePosition,
        float radius,
        float capsuleHeight
    ) const;

    std::vector<SolidBox> m_solids;
    std::vector<TriggerVolume> m_triggers;

    mutable std::unordered_map<CellKey, std::vector<std::size_t>, CellKeyHash> m_spatialCells;
    mutable std::vector<std::size_t> m_spatialScratch;
    mutable std::vector<std::uint32_t> m_spatialVisitStamp;
    mutable std::uint32_t m_spatialCurrentStamp = 1;
    mutable bool m_spatialDirty = true;
    float m_spatialCellSize = 8.0F;
};
} // namespace engine::physics
