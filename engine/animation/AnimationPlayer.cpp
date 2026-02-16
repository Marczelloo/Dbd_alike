#include "engine/animation/AnimationPlayer.hpp"

#include <algorithm>

namespace engine::animation
{

void AnimationPlayer::SetClip(const AnimationClip* clip)
{
    m_clip = clip;
    m_time = 0.0F;
}

void AnimationPlayer::Update(float dt)
{
    if (m_clip == nullptr || m_clip->duration <= 0.0F)
    {
        return;
    }

    m_time += dt * m_playbackSpeed;

    if (m_looping)
    {
        // Wrap around
        while (m_time >= m_clip->duration)
        {
            m_time -= m_clip->duration;
        }
        while (m_time < 0.0F)
        {
            m_time += m_clip->duration;
        }
    }
    else
    {
        // Clamp to end
        m_time = std::clamp(m_time, 0.0F, m_clip->duration);
    }
}

float AnimationPlayer::Progress() const
{
    if (m_clip == nullptr || m_clip->duration <= 0.0F)
    {
        return 0.0F;
    }
    return m_time / m_clip->duration;
}

void AnimationPlayer::SetTime(float time)
{
    m_time = time;
    if (m_clip != nullptr && m_looping)
    {
        while (m_time >= m_clip->duration)
        {
            m_time -= m_clip->duration;
        }
        while (m_time < 0.0F)
        {
            m_time += m_clip->duration;
        }
    }
}

void AnimationPlayer::Reset()
{
    m_time = 0.0F;
}

void AnimationPlayer::SampleTranslation(int jointIndex, glm::vec3& out) const
{
    if (m_clip != nullptr)
    {
        m_clip->SampleTranslation(jointIndex, m_time, out);
    }
}

void AnimationPlayer::SampleRotation(int jointIndex, glm::quat& out) const
{
    if (m_clip != nullptr)
    {
        m_clip->SampleRotation(jointIndex, m_time, out);
    }
}

void AnimationPlayer::SampleScale(int jointIndex, glm::vec3& out) const
{
    if (m_clip != nullptr)
    {
        m_clip->SampleScale(jointIndex, m_time, out);
    }
    else
    {
        out = glm::vec3{1.0F};
    }
}

} // namespace engine::animation
