#pragma once

#include "engine/animation/AnimationPlayer.hpp"

#include <vector>

namespace engine::animation
{

// Blends between two animation sources (crossfade)
class AnimationBlender
{
public:
    AnimationBlender() = default;

    // Start a crossfade to a new clip
    void CrossfadeTo(const AnimationClip* targetClip, float duration);

    // Update blending
    void Update(float dt);

    // Compute blended transforms for a joint
    void ComputeBlendedTranslation(int jointIndex, glm::vec3& out) const;
    void ComputeBlendedRotation(int jointIndex, glm::quat& out) const;
    void ComputeBlendedScale(int jointIndex, glm::vec3& out) const;

    // Get current blend weight (0 = source, 1 = target)
    [[nodiscard]] float BlendWeight() const { return m_blendWeight; }

    // Check if blending is active
    [[nodiscard]] bool IsBlending() const { return m_blending; }

    // Get source/target players (for external sampling)
    [[nodiscard]] AnimationPlayer& SourcePlayer() { return m_sourcePlayer; }
    [[nodiscard]] AnimationPlayer& TargetPlayer() { return m_targetPlayer; }
    [[nodiscard]] const AnimationPlayer& SourcePlayer() const { return m_sourcePlayer; }
    [[nodiscard]] const AnimationPlayer& TargetPlayer() const { return m_targetPlayer; }

    // Set current clip directly (no blend)
    void SetClipDirect(const AnimationClip* clip);

    // Get current dominant clip
    [[nodiscard]] const AnimationClip* GetCurrentClip() const;

private:
    AnimationPlayer m_sourcePlayer;
    AnimationPlayer m_targetPlayer;
    float m_blendDuration = 0.0F;
    float m_blendTime = 0.0F;
    float m_blendWeight = 1.0F;  // 1 = fully on target
    bool m_blending = false;
};

} // namespace engine::animation
