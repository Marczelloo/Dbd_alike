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
} // namespace

void PhysicsWorld::Clear()
{
    m_solids.clear();
    m_triggers.clear();
}

void PhysicsWorld::AddSolidBox(const SolidBox& box)
{
    m_solids.push_back(box);
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
    for (const SolidBox& box : m_solids)
    {
        if (!box.blocksSight || box.entity == ignoreEntity)
        {
            continue;
        }

        const glm::vec3 minBounds = box.center - box.halfExtents;
        const glm::vec3 maxBounds = box.center + box.halfExtents;
        float hitT = 1.0F;
        glm::vec3 hitNormal{0.0F, 1.0F, 0.0F};
        if (SegmentIntersectsAabb3D(from, to, minBounds, maxBounds, &hitT, &hitNormal))
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

    for (const SolidBox& box : m_solids)
    {
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

    return result;
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

        for (const SolidBox& box : m_solids)
        {
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
        for (const SolidBox& box : m_solids)
        {
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
} // namespace engine::physics
