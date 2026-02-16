#pragma once

#include "engine/animation/AnimationBlender.hpp"
#include "engine/animation/AnimationClip.hpp"

#include <functional>
#include <optional>

namespace engine::animation
{

// Locomotion state machine for Idle/Walk/Run
class AnimationStateMachine
{
public:
    AnimationStateMachine() = default;

    // Set profile (thresholds, blend times)
    void SetProfile(const LocomotionProfile& profile) { m_profile = profile; }
    [[nodiscard]] const LocomotionProfile& GetProfile() const { return m_profile; }
    [[nodiscard]] LocomotionProfile& GetProfileMut() { return m_profile; }

    // Set clips for each state
    void SetClips(const AnimationClip* idle, const AnimationClip* walk, const AnimationClip* run);

    // Update state machine based on current speed
    void Update(float dt, float currentSpeed);

    // Get current state
    [[nodiscard]] LocomotionState CurrentState() const { return m_currentState; }

    // Force a specific state (for debugging)
    void ForceState(LocomotionState state);

    // Set auto mode (speed-based) or manual mode
    void SetAutoMode(bool autoMode) { m_autoMode = autoMode; }
    [[nodiscard]] bool IsAutoMode() const { return m_autoMode; }

    // Get blender for sampling
    [[nodiscard]] const AnimationBlender& GetBlender() const { return m_blender; }
    [[nodiscard]] AnimationBlender& GetBlenderMut() { return m_blender; }

    // Get current playback speed (adjusted for moonwalk prevention)
    [[nodiscard]] float CurrentPlaybackSpeed() const { return m_currentPlaybackSpeed; }

    // Get blend weight for debug display
    [[nodiscard]] float BlendWeight() const { return m_blender.BlendWeight(); }

    // Check if blender is actively blending
    [[nodiscard]] bool IsBlending() const { return m_blender.IsBlending(); }

    // State change callback
    using StateChangeCallback = std::function<void(LocomotionState, LocomotionState)>;
    void SetStateChangeCallback(StateChangeCallback callback) { m_stateChangeCallback = std::move(callback); }

private:
    LocomotionState DetermineState(float speed) const;
    float ComputePlaybackSpeed(LocomotionState state, float currentSpeed) const;
    float GetBlendTime(LocomotionState from, LocomotionState to) const;

    LocomotionProfile m_profile{};
    AnimationBlender m_blender;

    const AnimationClip* m_idleClip = nullptr;
    const AnimationClip* m_walkClip = nullptr;
    const AnimationClip* m_runClip = nullptr;

    LocomotionState m_currentState = LocomotionState::Idle;
    float m_currentPlaybackSpeed = 1.0F;
    bool m_autoMode = true;

    StateChangeCallback m_stateChangeCallback;
};

} // namespace engine::animation
