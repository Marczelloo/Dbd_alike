#include "engine/animation/AnimationStateMachine.hpp"

#include <algorithm>

namespace engine::animation
{

void AnimationStateMachine::SetClips(const AnimationClip* idle, const AnimationClip* walk, const AnimationClip* run)
{
    m_idleClip = idle;
    m_walkClip = walk;
    m_runClip = run;

    // Always reset blender target when clip bindings change to avoid stale clip pointers.
    if (idle != nullptr)
    {
        m_blender.SetClipDirect(idle);
        m_currentState = LocomotionState::Idle;
        return;
    }

    if (walk != nullptr)
    {
        m_blender.SetClipDirect(walk);
        m_currentState = LocomotionState::Walk;
        return;
    }

    if (run != nullptr)
    {
        m_blender.SetClipDirect(run);
        m_currentState = LocomotionState::Run;
        return;
    }

    m_blender.SetClipDirect(nullptr);
    m_currentState = LocomotionState::Idle;
    m_currentPlaybackSpeed = 1.0F;
}

void AnimationStateMachine::Update(float dt, float currentSpeed)
{
    // Determine target state
    LocomotionState targetState = m_autoMode ? DetermineState(currentSpeed) : m_currentState;

    // Handle state transition
    if (targetState != m_currentState)
    {
        const AnimationClip* targetClip = nullptr;
        switch (targetState)
        {
            case LocomotionState::Idle:
                targetClip = m_idleClip;
                break;
            case LocomotionState::Walk:
                targetClip = m_walkClip;
                break;
            case LocomotionState::Run:
                targetClip = m_runClip;
                break;
        }

        if (targetClip != nullptr)
        {
            const float blendTime = GetBlendTime(m_currentState, targetState);
            m_blender.CrossfadeTo(targetClip, blendTime);
        }

        if (m_stateChangeCallback)
        {
            m_stateChangeCallback(m_currentState, targetState);
        }
        m_currentState = targetState;
    }

    // Compute playback speed for moonwalk prevention
    m_currentPlaybackSpeed = ComputePlaybackSpeed(m_currentState, currentSpeed);

    // Apply playback speed to blender
    m_blender.TargetPlayer().SetPlaybackSpeed(m_currentPlaybackSpeed * m_profile.globalAnimScale);
    m_blender.SourcePlayer().SetPlaybackSpeed(m_profile.globalAnimScale);

    // Update blender
    m_blender.Update(dt);
}

void AnimationStateMachine::ForceState(LocomotionState state)
{
    if (state == m_currentState)
    {
        return;
    }

    const AnimationClip* targetClip = nullptr;
    switch (state)
    {
        case LocomotionState::Idle:
            targetClip = m_idleClip;
            break;
        case LocomotionState::Walk:
            targetClip = m_walkClip;
            break;
        case LocomotionState::Run:
            targetClip = m_runClip;
            break;
    }

    if (targetClip != nullptr)
    {
        const float blendTime = GetBlendTime(m_currentState, state);
        m_blender.CrossfadeTo(targetClip, blendTime);
    }

    if (m_stateChangeCallback)
    {
        m_stateChangeCallback(m_currentState, state);
    }
    m_currentState = state;
}

LocomotionState AnimationStateMachine::DetermineState(float speed) const
{
    if (speed < m_profile.idleEpsilon)
    {
        return LocomotionState::Idle;
    }
    if (speed >= m_profile.runThreshold)
    {
        return LocomotionState::Run;
    }
    return LocomotionState::Walk;
}

float AnimationStateMachine::ComputePlaybackSpeed(LocomotionState state, float currentSpeed) const
{
    if (currentSpeed < 0.001F)
    {
        return 1.0F;  // Default speed when not moving
    }

    switch (state)
    {
        case LocomotionState::Idle:
            return 1.0F;

        case LocomotionState::Walk:
        {
            // Match animation playback to movement speed
            const float refSpeed = m_profile.walkSpeedRef;
            if (refSpeed <= 0.0F)
            {
                return 1.0F;
            }
            const float scale = currentSpeed / refSpeed;
            return std::clamp(scale, m_profile.minWalkScale, m_profile.maxWalkScale);
        }

        case LocomotionState::Run:
        {
            const float refSpeed = m_profile.runSpeedRef;
            if (refSpeed <= 0.0F)
            {
                return 1.0F;
            }
            const float scale = currentSpeed / refSpeed;
            return std::clamp(scale, m_profile.minRunScale, m_profile.maxRunScale);
        }
    }
    return 1.0F;
}

float AnimationStateMachine::GetBlendTime(LocomotionState from, LocomotionState to) const
{
    // Get blend time for transition
    if (from == to)
    {
        return 0.0F;
    }

    switch (from)
    {
        case LocomotionState::Idle:
            return m_profile.blendIdleWalk;

        case LocomotionState::Walk:
            if (to == LocomotionState::Run)
            {
                return m_profile.blendWalkRun;
            }
            return m_profile.blendIdleWalk;

        case LocomotionState::Run:
            return m_profile.blendRunIdle;
    }
    return 0.15F;
}

} // namespace engine::animation
