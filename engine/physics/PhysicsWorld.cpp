#include "engine/physics/PhysicsWorld.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <glm/vec2.hpp>

namespace engine::physics
{
namespace
{
constexpr float kResolveEpsilon = 0.0005F;
constexpr float kGroundProbeDistance = 0.08F;

float ClampFloat(float value, float minValue, float maxValue)
{
    return std::max(minValue, std::min(value, maxValue));
}

glm::vec3 ClosestPointOnAabb(const glm::vec3& point, const glm::vec3& minBounds, const glm::vec3& maxBounds)
{
    return glm::vec3{
        ClampFloat(point.x, minBounds.x, maxBounds.x),
        ClampFloat(point.y, minBounds.y, maxBounds.y),
        ClampFloat(point.z, minBounds.z, maxBounds.z),
    };
}

float HorizontalDistance(const glm::vec3& a, const glm::vec3& b)
{
    const glm::vec2 d{a.x - b.x, a.z - b.z};
    return glm::length(d);
}

int CellCoord(float value, float cellSize)
{
    return static_cast<int>(std::floor(value / std::max(0.001F, cellSize)));
}
} // namespace

void PhysicsWorld::Clear()
{
    m_solids.clear();
    m_triggers.clear();
    m_spatialCells.clear();
    m_spatialScratch.clear();
    m_spatialVisitStamp.clear();
    m_spatialCurrentStamp = 1;
    m_spatialDirty = true;
}

void PhysicsWorld::AddSolidBox(const SolidBox& box)
{
    m_solids.push_back(box);
    m_spatialDirty = true;
}

void PhysicsWorld::AddTrigger(const TriggerVolume& trigger)
{
    m_triggers.push_back(trigger);
}

MoveResult PhysicsWorld::MoveCapsule(
    const glm::vec3& currentPosition,
    float radius,
    float capsuleHeight,
    const glm::vec3& desiredDelta,
    bool collisionEnabled,
    float stepHeight
) const
{
    MoveResult result;

    if (!collisionEnabled)
    {
        result.position = currentPosition + desiredDelta;
        return result;
    }

    const glm::vec3 horizontalDelta{desiredDelta.x, 0.0F, desiredDelta.z};

    MoveResult horizontalResult = ResolveCapsulePosition(currentPosition + horizontalDelta, radius, capsuleHeight);

    const bool attemptStep = glm::length(horizontalDelta) > 1.0e-5F &&
                             horizontalResult.collided &&
                             horizontalResult.lastCollisionNormal.y < 0.25F &&
                             stepHeight > 0.0F;

    if (attemptStep)
    {
        const glm::vec3 stepUpPosition = currentPosition + glm::vec3{0.0F, stepHeight, 0.0F};
        MoveResult stepResult = ResolveCapsulePosition(stepUpPosition + horizontalDelta, radius, capsuleHeight);

        stepResult = ResolveCapsulePosition(stepResult.position + glm::vec3{0.0F, -stepHeight, 0.0F}, radius, capsuleHeight);

        const float horizontalMoveBase = HorizontalDistance(currentPosition, horizontalResult.position);
        const float horizontalMoveStep = HorizontalDistance(currentPosition, stepResult.position);

        if (horizontalMoveStep > horizontalMoveBase + 0.05F)
        {
            horizontalResult = stepResult;
            horizontalResult.steppedUp = true;
        }
    }

    MoveResult verticalResult = ResolveCapsulePosition(
        horizontalResult.position + glm::vec3{0.0F, desiredDelta.y, 0.0F},
        radius,
        capsuleHeight
    );

    result.position = verticalResult.position;
    result.collided = horizontalResult.collided || verticalResult.collided;
    result.grounded = horizontalResult.grounded || verticalResult.grounded;
    result.steppedUp = horizontalResult.steppedUp;

    result.maxPenetrationDepth = std::max(horizontalResult.maxPenetrationDepth, verticalResult.maxPenetrationDepth);
    result.lastCollisionNormal = verticalResult.collided ? verticalResult.lastCollisionNormal : horizontalResult.lastCollisionNormal;

    return result;
}

bool PhysicsWorld::HasLineOfSight(const glm::vec3& from, const glm::vec3& to, engine::scene::Entity ignoreEntity) const
{
    const glm::vec3 minBounds = glm::min(from, to);
    const glm::vec3 maxBounds = glm::max(from, to);
    AppendSolidCandidates(minBounds, maxBounds, m_spatialScratch);

    for (const std::size_t index : m_spatialScratch)
    {
        const SolidBox& box = m_solids[index];
        if (!box.blocksSight || box.entity == ignoreEntity)
        {
            continue;
        }

        const glm::vec3 solidMinBounds = box.center - box.halfExtents;
        const glm::vec3 solidMaxBounds = box.center + box.halfExtents;
        float hitT = 1.0F;
        glm::vec3 hitNormal{0.0F, 1.0F, 0.0F};
        if (SegmentIntersectsAabb3D(from, to, solidMinBounds, solidMaxBounds, &hitT, &hitNormal))
        {
            return false;
        }
    }

    return true;
}

bool PhysicsWorld::RaycastAny(const glm::vec3& from, const glm::vec3& to, engine::scene::Entity ignoreEntity) const
{
    return RaycastNearest(from, to, ignoreEntity).has_value();
}

std::optional<RaycastHit> PhysicsWorld::RaycastNearest(
    const glm::vec3& from,
    const glm::vec3& to,
    engine::scene::Entity ignoreEntity
) const
{
    std::optional<RaycastHit> best;

    const glm::vec3 queryMinBounds = glm::min(from, to);
    const glm::vec3 queryMaxBounds = glm::max(from, to);
    AppendSolidCandidates(queryMinBounds, queryMaxBounds, m_spatialScratch);

    for (const std::size_t index : m_spatialScratch)
    {
        const SolidBox& box = m_solids[index];
        if (box.entity == ignoreEntity)
        {
            continue;
        }

        const glm::vec3 minBounds = box.center - box.halfExtents;
        const glm::vec3 maxBounds = box.center + box.halfExtents;

        float hitT = 1.0F;
        glm::vec3 hitNormal{0.0F, 1.0F, 0.0F};
        if (!SegmentIntersectsAabb3D(from, to, minBounds, maxBounds, &hitT, &hitNormal))
        {
            continue;
        }

        if (!best.has_value() || hitT < best->t)
        {
            RaycastHit hit;
            hit.entity = box.entity;
            hit.t = hitT;
            hit.normal = hitNormal;
            hit.position = from + (to - from) * hitT;
            best = hit;
        }
    }

    return best;
}

std::vector<TriggerHit> PhysicsWorld::QueryCapsuleTriggers(
    const glm::vec3& position,
    float radius,
    float capsuleHeight,
    TriggerKind kind
) const
{
    std::vector<TriggerHit> result;
    QueryCapsuleTriggers(result, position, radius, capsuleHeight, kind);
    return result;
}

void PhysicsWorld::QueryCapsuleTriggers(
    std::vector<TriggerHit>& result,
    const glm::vec3& position,
    float radius,
    float capsuleHeight,
    TriggerKind kind
) const
{
    result.clear();
    const float capsuleHalfSegment = std::max(0.0F, capsuleHeight * 0.5F - radius);

    for (const TriggerVolume& trigger : m_triggers)
    {
        if (trigger.kind != kind)
        {
            continue;
        }

        const glm::vec3 minBounds = trigger.center - trigger.halfExtents - glm::vec3{0.0F, capsuleHalfSegment, 0.0F};
        const glm::vec3 maxBounds = trigger.center + trigger.halfExtents + glm::vec3{0.0F, capsuleHalfSegment, 0.0F};

        const glm::vec3 closestPoint = ClosestPointOnAabb(position, minBounds, maxBounds);
        const glm::vec3 delta = position - closestPoint;
        if (glm::dot(delta, delta) <= radius * radius)
        {
            result.push_back(TriggerHit{trigger.entity, trigger.kind});
        }
    }
}

std::vector<TriggerCastHit> PhysicsWorld::SphereCastTriggers(
    const glm::vec3& from,
    const glm::vec3& to,
    float radius
) const
{
    std::vector<TriggerCastHit> hits;

    for (const TriggerVolume& trigger : m_triggers)
    {
        const glm::vec3 minBounds = trigger.center - trigger.halfExtents - glm::vec3{radius};
        const glm::vec3 maxBounds = trigger.center + trigger.halfExtents + glm::vec3{radius};

        float hitT = 1.0F;
        glm::vec3 hitNormal{0.0F, 1.0F, 0.0F};
        if (!SegmentIntersectsAabb3D(from, to, minBounds, maxBounds, &hitT, &hitNormal))
        {
            continue;
        }

        TriggerCastHit hit;
        hit.entity = trigger.entity;
        hit.kind = trigger.kind;
        hit.t = hitT;
        hit.position = from + (to - from) * hitT;
        hits.push_back(hit);
    }

    std::sort(hits.begin(), hits.end(), [](const TriggerCastHit& lhs, const TriggerCastHit& rhs) {
        return lhs.t < rhs.t;
    });

    return hits;
}

bool PhysicsWorld::SphereIntersectsExpandedAabb(
    const glm::vec3& center,
    float radius,
    const SolidBox& box,
    float capsuleHalfSegment,
    glm::vec3* outNormal,
    float* outPenetration
)
{
    const glm::vec3 minBounds = box.center - box.halfExtents - glm::vec3{0.0F, capsuleHalfSegment, 0.0F};
    const glm::vec3 maxBounds = box.center + box.halfExtents + glm::vec3{0.0F, capsuleHalfSegment, 0.0F};

    const glm::vec3 closestPoint = ClosestPointOnAabb(center, minBounds, maxBounds);
    const glm::vec3 delta = center - closestPoint;

    const float distSq = glm::dot(delta, delta);
    const float radiusSq = radius * radius;
    if (distSq >= radiusSq)
    {
        return false;
    }

    glm::vec3 normal{0.0F, 1.0F, 0.0F};
    float penetration = 0.0F;

    if (distSq > 1.0e-8F)
    {
        const float distance = std::sqrt(distSq);
        normal = delta / distance;
        penetration = radius - distance;
    }
    else
    {
        const float distances[6] = {
            center.x - minBounds.x,
            maxBounds.x - center.x,
            center.y - minBounds.y,
            maxBounds.y - center.y,
            center.z - minBounds.z,
            maxBounds.z - center.z,
        };

        int bestIndex = 0;
        float bestDistance = distances[0];
        for (int i = 1; i < 6; ++i)
        {
            if (distances[i] < bestDistance)
            {
                bestDistance = distances[i];
                bestIndex = i;
            }
        }

        switch (bestIndex)
        {
            case 0: normal = glm::vec3{-1.0F, 0.0F, 0.0F}; break;
            case 1: normal = glm::vec3{1.0F, 0.0F, 0.0F}; break;
            case 2: normal = glm::vec3{0.0F, -1.0F, 0.0F}; break;
            case 3: normal = glm::vec3{0.0F, 1.0F, 0.0F}; break;
            case 4: normal = glm::vec3{0.0F, 0.0F, -1.0F}; break;
            case 5: normal = glm::vec3{0.0F, 0.0F, 1.0F}; break;
            default: break;
        }

        penetration = radius + bestDistance;
    }

    if (outNormal != nullptr)
    {
        *outNormal = normal;
    }
    if (outPenetration != nullptr)
    {
        *outPenetration = penetration;
    }

    return true;
}

bool PhysicsWorld::SegmentIntersectsAabb3D(
    const glm::vec3& from,
    const glm::vec3& to,
    const glm::vec3& minBounds,
    const glm::vec3& maxBounds,
    float* outT,
    glm::vec3* outNormal
)
{
    const glm::vec3 direction = to - from;

    float tMin = 0.0F;
    float tMax = 1.0F;
    glm::vec3 bestNormal{0.0F, 1.0F, 0.0F};

    for (int axis = 0; axis < 3; ++axis)
    {
        const float start = from[axis];
        const float dir = direction[axis];
        const float minAxis = minBounds[axis];
        const float maxAxis = maxBounds[axis];

        if (std::abs(dir) < 1.0e-7F)
        {
            if (start < minAxis || start > maxAxis)
            {
                return false;
            }
            continue;
        }

        const float invDir = 1.0F / dir;
        float t1 = (minAxis - start) * invDir;
        float t2 = (maxAxis - start) * invDir;

        glm::vec3 nearNormal{0.0F};
        nearNormal[axis] = (invDir >= 0.0F) ? -1.0F : 1.0F;

        if (t1 > t2)
        {
            std::swap(t1, t2);
            nearNormal[axis] = -nearNormal[axis];
        }

        if (t1 > tMin)
        {
            tMin = t1;
            bestNormal = nearNormal;
        }

        tMax = std::min(tMax, t2);
        if (tMin > tMax)
        {
            return false;
        }
    }

    if (tMin < 0.0F || tMin > 1.0F)
    {
        return false;
    }

    if (outT != nullptr)
    {
        *outT = tMin;
    }
    if (outNormal != nullptr)
    {
        *outNormal = bestNormal;
    }

    return true;
}

MoveResult PhysicsWorld::ResolveCapsulePosition(const glm::vec3& candidatePosition, float radius, float capsuleHeight) const
{
    MoveResult result;
    result.position = candidatePosition;

    const float capsuleHalfSegment = std::max(0.0F, capsuleHeight * 0.5F - radius);

    for (int iteration = 0; iteration < 8; ++iteration)
    {
        bool hadPenetration = false;

        const glm::vec3 queryHalfExtents{radius, radius + capsuleHalfSegment, radius};
        AppendSolidCandidates(result.position - queryHalfExtents, result.position + queryHalfExtents, m_spatialScratch);

        for (const std::size_t index : m_spatialScratch)
        {
            const SolidBox& box = m_solids[index];
            glm::vec3 normal{0.0F, 1.0F, 0.0F};
            float penetration = 0.0F;
            if (!SphereIntersectsExpandedAabb(result.position, radius, box, capsuleHalfSegment, &normal, &penetration))
            {
                continue;
            }

            hadPenetration = true;
            result.collided = true;
            result.position += normal * (penetration + kResolveEpsilon);
            result.maxPenetrationDepth = std::max(result.maxPenetrationDepth, penetration);
            result.lastCollisionNormal = normal;
            if (normal.y > 0.45F)
            {
                result.grounded = true;
            }
        }

        if (!hadPenetration)
        {
            break;
        }
    }

    if (!result.grounded)
    {
        const glm::vec3 probePosition = result.position + glm::vec3{0.0F, -kGroundProbeDistance, 0.0F};
        const glm::vec3 probeHalfExtents{radius, radius + capsuleHalfSegment, radius};
        AppendSolidCandidates(probePosition - probeHalfExtents, probePosition + probeHalfExtents, m_spatialScratch);
        for (const std::size_t index : m_spatialScratch)
        {
            const SolidBox& box = m_solids[index];
            glm::vec3 normal{0.0F, 1.0F, 0.0F};
            float penetration = 0.0F;
            if (!SphereIntersectsExpandedAabb(probePosition, radius, box, capsuleHalfSegment, &normal, &penetration))
            {
                continue;
            }

            if (normal.y > 0.35F)
            {
                result.grounded = true;
                break;
            }
        }
    }

    return result;
}

void PhysicsWorld::RebuildSpatialIndex() const
{
    if (!m_spatialDirty)
    {
        return;
    }

    m_spatialCells.clear();
    m_spatialVisitStamp.assign(m_solids.size(), 0U);

    if (m_solids.empty())
    {
        m_spatialDirty = false;
        return;
    }

    for (std::size_t index = 0; index < m_solids.size(); ++index)
    {
        const SolidBox& box = m_solids[index];
        const glm::vec3 minBounds = box.center - box.halfExtents;
        const glm::vec3 maxBounds = box.center + box.halfExtents;

        const int minX = CellCoord(minBounds.x, m_spatialCellSize);
        const int minY = CellCoord(minBounds.y, m_spatialCellSize);
        const int minZ = CellCoord(minBounds.z, m_spatialCellSize);
        const int maxX = CellCoord(maxBounds.x, m_spatialCellSize);
        const int maxY = CellCoord(maxBounds.y, m_spatialCellSize);
        const int maxZ = CellCoord(maxBounds.z, m_spatialCellSize);

        for (int z = minZ; z <= maxZ; ++z)
        {
            for (int y = minY; y <= maxY; ++y)
            {
                for (int x = minX; x <= maxX; ++x)
                {
                    m_spatialCells[CellKey{x, y, z}].push_back(index);
                }
            }
        }
    }

    m_spatialCurrentStamp = 1;
    m_spatialDirty = false;
}

void PhysicsWorld::AppendSolidCandidates(
    const glm::vec3& minBounds,
    const glm::vec3& maxBounds,
    std::vector<std::size_t>& outIndices
) const
{
    RebuildSpatialIndex();
    outIndices.clear();

    if (m_solids.empty())
    {
        return;
    }

    if (m_spatialVisitStamp.size() != m_solids.size())
    {
        m_spatialVisitStamp.assign(m_solids.size(), 0U);
    }

    ++m_spatialCurrentStamp;
    if (m_spatialCurrentStamp == 0)
    {
        std::fill(m_spatialVisitStamp.begin(), m_spatialVisitStamp.end(), 0U);
        m_spatialCurrentStamp = 1;
    }

    const int minX = CellCoord(minBounds.x, m_spatialCellSize);
    const int minY = CellCoord(minBounds.y, m_spatialCellSize);
    const int minZ = CellCoord(minBounds.z, m_spatialCellSize);
    const int maxX = CellCoord(maxBounds.x, m_spatialCellSize);
    const int maxY = CellCoord(maxBounds.y, m_spatialCellSize);
    const int maxZ = CellCoord(maxBounds.z, m_spatialCellSize);

    for (int z = minZ; z <= maxZ; ++z)
    {
        for (int y = minY; y <= maxY; ++y)
        {
            for (int x = minX; x <= maxX; ++x)
            {
                const auto cellIt = m_spatialCells.find(CellKey{x, y, z});
                if (cellIt == m_spatialCells.end())
                {
                    continue;
                }

                for (const std::size_t solidIndex : cellIt->second)
                {
                    if (solidIndex >= m_spatialVisitStamp.size())
                    {
                        continue;
                    }
                    if (m_spatialVisitStamp[solidIndex] == m_spatialCurrentStamp)
                    {
                        continue;
                    }
                    m_spatialVisitStamp[solidIndex] = m_spatialCurrentStamp;
                    outIndices.push_back(solidIndex);
                }
            }
        }
    }

    if (outIndices.empty())
    {
        outIndices.reserve(m_solids.size());
        for (std::size_t index = 0; index < m_solids.size(); ++index)
        {
            outIndices.push_back(index);
        }
    }
}
} // namespace engine::physics
