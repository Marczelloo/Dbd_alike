#include "engine/animation/AnimationClip.hpp"

#include <algorithm>
#include <cmath>

namespace engine::animation
{

namespace
{
// Find keyframe index for time (returns lower bound)
template <typename T>
std::size_t FindKeyframeIndex(const std::vector<float>& times, float time)
{
    if (times.size() < 2)
    {
        return 0;
    }

    // Binary search for upper bound
    auto it = std::upper_bound(times.begin(), times.end(), time);
    if (it == times.begin())
    {
        return 0;
    }
    if (it == times.end())
    {
        return times.size() - 2;  // Return second-to-last for clamping
    }
    return static_cast<std::size_t>(std::distance(times.begin(), it) - 1);
}

// Linear interpolation between two values
template <typename T>
T Lerp(const T& a, const T& b, float t)
{
    return a + (b - a) * t;
}

// SLERP for quaternions
glm::quat Slerp(const glm::quat& a, const glm::quat& b, float t)
{
    // Ensure short path
    glm::quat result = glm::dot(a, b) < 0.0F ? -b : b;
    return glm::slerp(a, result, t);
}

// Compute alpha between two keyframes
float ComputeAlpha(float time, float t0, float t1)
{
    if (t1 <= t0)
    {
        return 0.0F;
    }
    return std::clamp((time - t0) / (t1 - t0), 0.0F, 1.0F);
}
} // namespace

const char* LocomotionStateToString(LocomotionState state)
{
    switch (state)
    {
        case LocomotionState::Idle:
            return "Idle";
        case LocomotionState::Walk:
            return "Walk";
        case LocomotionState::Run:
            return "Run";
    }
    return "Unknown";
}

std::optional<LocomotionState> ParseLocomotionState(const std::string& str)
{
    if (str == "idle" || str == "Idle")
    {
        return LocomotionState::Idle;
    }
    if (str == "walk" || str == "Walk")
    {
        return LocomotionState::Walk;
    }
    if (str == "run" || str == "Run")
    {
        return LocomotionState::Run;
    }
    return std::nullopt;
}

bool AnimationClip::HasTranslation(int jointIndex) const
{
    return FindTranslation(jointIndex) != nullptr;
}

bool AnimationClip::HasRotation(int jointIndex) const
{
    return FindRotation(jointIndex) != nullptr;
}

bool AnimationClip::HasScale(int jointIndex) const
{
    return FindScale(jointIndex) != nullptr;
}

const TranslationChannel* AnimationClip::FindTranslation(int jointIndex) const
{
    for (const auto& ch : translations)
    {
        if (ch.jointIndex == jointIndex && !ch.Empty())
        {
            return &ch;
        }
    }
    return nullptr;
}

const RotationChannel* AnimationClip::FindRotation(int jointIndex) const
{
    for (const auto& ch : rotations)
    {
        if (ch.jointIndex == jointIndex && !ch.Empty())
        {
            return &ch;
        }
    }
    return nullptr;
}

const ScaleChannel* AnimationClip::FindScale(int jointIndex) const
{
    for (const auto& ch : scales)
    {
        if (ch.jointIndex == jointIndex && !ch.Empty())
        {
            return &ch;
        }
    }
    return nullptr;
}

template <typename T, typename InterpFunc>
void AnimationClip::SampleChannel(const AnimationChannel<T>& channel, float time, T& out, InterpFunc interp)
{
    if (channel.Empty())
    {
        return;
    }

    const std::size_t idx = FindKeyframeIndex<T>(channel.times, time);

    if (idx >= channel.times.size() - 1)
    {
        // At or past end - use last value
        out = channel.values.back();
        return;
    }

    const float t0 = channel.times[idx];
    const float t1 = channel.times[idx + 1];
    const float alpha = ComputeAlpha(time, t0, t1);

    out = interp(channel.values[idx], channel.values[idx + 1], alpha);
}

void AnimationClip::SampleTranslation(int jointIndex, float time, glm::vec3& out) const
{
    const TranslationChannel* ch = FindTranslation(jointIndex);
    if (ch != nullptr)
    {
        SampleChannel(*ch, time, out, Lerp<glm::vec3>);
    }
    // else: keep out unchanged (default)
}

void AnimationClip::SampleRotation(int jointIndex, float time, glm::quat& out) const
{
    const RotationChannel* ch = FindRotation(jointIndex);
    if (ch != nullptr)
    {
        SampleChannel(*ch, time, out, Slerp);
    }
}

void AnimationClip::SampleScale(int jointIndex, float time, glm::vec3& out) const
{
    const ScaleChannel* ch = FindScale(jointIndex);
    if (ch != nullptr)
    {
        SampleChannel(*ch, time, out, Lerp<glm::vec3>);
    }
    else
    {
        out = glm::vec3{1.0F};  // Default scale
    }
}

} // namespace engine::animation
