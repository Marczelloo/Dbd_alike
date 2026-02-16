#pragma once

#include "engine/animation/AnimationClip.hpp"

#include <memory>

namespace engine::animation
{

// Plays a single animation clip
class AnimationPlayer
{
public:
    AnimationPlayer() = default;

    // Set the clip to play
    void SetClip(const AnimationClip* clip);

    // Update playback time
    void Update(float dt);

    // Get current time in clip
    [[nodiscard]] float Time() const { return m_time; }

    // Get clip duration
    [[nodiscard]] float Duration() const { return m_clip ? m_clip->duration : 0.0F; }

    // Get normalized progress (0-1)
    [[nodiscard]] float Progress() const;

    // Playback controls
    void SetLooping(bool loop) { m_looping = loop; }
    [[nodiscard]] bool IsLooping() const { return m_looping; }

    void SetPlaybackSpeed(float speed) { m_playbackSpeed = std::max(0.0F, speed); }
    [[nodiscard]] float PlaybackSpeed() const { return m_playbackSpeed; }

    void SetTime(float time);
    void Reset();

    // Get current clip
    [[nodiscard]] const AnimationClip* GetClip() const { return m_clip; }

    // Check if playing
    [[nodiscard]] bool IsPlaying() const { return m_clip != nullptr; }

    // Sample current frame for a joint
    void SampleTranslation(int jointIndex, glm::vec3& out) const;
    void SampleRotation(int jointIndex, glm::quat& out) const;
    void SampleScale(int jointIndex, glm::vec3& out) const;

private:
    const AnimationClip* m_clip = nullptr;
    float m_time = 0.0F;
    float m_playbackSpeed = 1.0F;
    bool m_looping = true;
};

} // namespace engine::animation
