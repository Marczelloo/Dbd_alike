#include "engine/animation/AnimationBlender.hpp"

#include <algorithm>

namespace engine::animation
{

void AnimationBlender::CrossfadeTo(const AnimationClip* targetClip, float duration)
{
    if (targetClip == nullptr)
    {
        return;
    }

    // If we're already blending, use current blended state as new source
    if (m_blending)
    {
        // Current target becomes source
        m_sourcePlayer = m_targetPlayer;
        m_sourcePlayer.SetClip(m_targetPlayer.GetClip());
    }
    else
    {
        // Use current as source
        m_sourcePlayer = m_targetPlayer;
    }

    m_targetPlayer.SetClip(targetClip);
    m_targetPlayer.Reset();

    m_blendDuration = std::max(0.001F, duration);
    m_blendTime = 0.0F;
    m_blendWeight = 0.0F;
    m_blending = true;
}

void AnimationBlender::Update(float dt)
{
    if (m_blending)
    {
        m_blendTime += dt;
        m_blendWeight = std::clamp(m_blendTime / m_blendDuration, 0.0F, 1.0F);

        if (m_blendWeight >= 1.0F)
        {
            m_blending = false;
            // Source is now irrelevant
            m_sourcePlayer.SetClip(nullptr);
        }
    }

    // Update both players
    m_sourcePlayer.Update(dt);
    m_targetPlayer.Update(dt);
}

void AnimationBlender::ComputeBlendedTranslation(int jointIndex, glm::vec3& out) const
{
    if (!m_blending || m_blendWeight >= 1.0F)
    {
        m_targetPlayer.SampleTranslation(jointIndex, out);
        return;
    }

    if (m_blendWeight <= 0.0F)
    {
        m_sourcePlayer.SampleTranslation(jointIndex, out);
        return;
    }

    glm::vec3 sourceVal{0.0F};
    glm::vec3 targetVal{0.0F};
    m_sourcePlayer.SampleTranslation(jointIndex, sourceVal);
    m_targetPlayer.SampleTranslation(jointIndex, targetVal);

    out = sourceVal + (targetVal - sourceVal) * m_blendWeight;
}

void AnimationBlender::ComputeBlendedRotation(int jointIndex, glm::quat& out) const
{
    if (!m_blending || m_blendWeight >= 1.0F)
    {
        m_targetPlayer.SampleRotation(jointIndex, out);
        return;
    }

    if (m_blendWeight <= 0.0F)
    {
        m_sourcePlayer.SampleRotation(jointIndex, out);
        return;
    }

    glm::quat sourceVal{1.0F, 0.0F, 0.0F, 0.0F};
    glm::quat targetVal{1.0F, 0.0F, 0.0F, 0.0F};
    m_sourcePlayer.SampleRotation(jointIndex, sourceVal);
    m_targetPlayer.SampleRotation(jointIndex, targetVal);

    // Use SLERP for rotation blending
    out = glm::slerp(sourceVal, targetVal, m_blendWeight);
}

void AnimationBlender::ComputeBlendedScale(int jointIndex, glm::vec3& out) const
{
    if (!m_blending || m_blendWeight >= 1.0F)
    {
        m_targetPlayer.SampleScale(jointIndex, out);
        return;
    }

    if (m_blendWeight <= 0.0F)
    {
        m_sourcePlayer.SampleScale(jointIndex, out);
        return;
    }

    glm::vec3 sourceVal{1.0F};
    glm::vec3 targetVal{1.0F};
    m_sourcePlayer.SampleScale(jointIndex, sourceVal);
    m_targetPlayer.SampleScale(jointIndex, targetVal);

    out = sourceVal + (targetVal - sourceVal) * m_blendWeight;
}

void AnimationBlender::SetClipDirect(const AnimationClip* clip)
{
    m_targetPlayer.SetClip(clip);
    m_sourcePlayer.SetClip(nullptr);
    m_blending = false;
    m_blendWeight = 1.0F;
}

const AnimationClip* AnimationBlender::GetCurrentClip() const
{
    return m_targetPlayer.GetClip();
}

} // namespace engine::animation
