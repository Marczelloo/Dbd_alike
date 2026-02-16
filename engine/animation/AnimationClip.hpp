#pragma once

#include <optional>
#include <string>
#include <vector>

#include <glm/gtc/quaternion.hpp>
#include <glm/vec3.hpp>

namespace engine::animation
{

// Animation channel for a single joint property (translation, rotation, or scale)
template <typename T>
struct AnimationChannel
{
    int jointIndex = -1;           // Index into skeleton joints
    std::vector<float> times;       // Keyframe timestamps
    std::vector<T> values;          // Keyframe values

    [[nodiscard]] bool Empty() const { return times.empty() || values.empty(); }
    [[nodiscard]] std::size_t KeyCount() const { return times.size(); }
};

// Translation channel (vec3)
using TranslationChannel = AnimationChannel<glm::vec3>;

// Rotation channel (quat)
using RotationChannel = AnimationChannel<glm::quat>;

// Scale channel (vec3)
using ScaleChannel = AnimationChannel<glm::vec3>;

// A complete animation clip loaded from glTF
struct AnimationClip
{
    std::string name;
    float duration = 0.0F;
    float ticksPerSecond = 24.0F;   // glTF default

    std::vector<TranslationChannel> translations;
    std::vector<RotationChannel> rotations;
    std::vector<ScaleChannel> scales;

    [[nodiscard]] bool Valid() const { return !name.empty() && duration > 0.0F; }
    [[nodiscard]] bool HasTranslation(int jointIndex) const;
    [[nodiscard]] bool HasRotation(int jointIndex) const;
    [[nodiscard]] bool HasScale(int jointIndex) const;

    // Sample at a specific time (returns default values if no channel)
    void SampleTranslation(int jointIndex, float time, glm::vec3& out) const;
    void SampleRotation(int jointIndex, float time, glm::quat& out) const;
    void SampleScale(int jointIndex, float time, glm::vec3& out) const;

private:
    // Find channel for joint
    [[nodiscard]] const TranslationChannel* FindTranslation(int jointIndex) const;
    [[nodiscard]] const RotationChannel* FindRotation(int jointIndex) const;
    [[nodiscard]] const ScaleChannel* FindScale(int jointIndex) const;

    // Sample a channel with linear/SLERP interpolation
    template <typename T, typename InterpFunc>
    static void SampleChannel(const AnimationChannel<T>& channel, float time, T& out, InterpFunc interp);
};

// Locomotion state for state machine
enum class LocomotionState
{
    Idle,
    Walk,
    Run
};

// Convert state to string
[[nodiscard]] const char* LocomotionStateToString(LocomotionState state);

// Try to parse state from string (returns nullopt if invalid)
[[nodiscard]] std::optional<LocomotionState> ParseLocomotionState(const std::string& str);

// Animation profile for mapping locomotion states to clips
struct LocomotionProfile
{
    std::string idleClipName = "surv_idle";
    std::string walkClipName = "surv_walk";
    std::string runClipName = "surv_run";

    // Speed thresholds for state transitions
    float idleEpsilon = 0.1F;       // Below this = idle
    float runThreshold = 3.5F;      // Above this = run (between = walk)

    // Crossfade blend times
    float blendIdleWalk = 0.12F;
    float blendWalkRun = 0.10F;
    float blendRunIdle = 0.15F;

    // Speed scaling for moonwalk prevention
    float walkSpeedRef = 3.43F;     // Reference speed for walk animation
    float runSpeedRef = 4.6F;       // Reference speed for run animation
    float minWalkScale = 0.8F;      // Minimum playback scale for walk
    float maxWalkScale = 1.2F;      // Maximum playback scale for walk
    float minRunScale = 0.8F;       // Minimum playback scale for run
    float maxRunScale = 1.2F;       // Maximum playback scale for run

    // Global animation scale
    float globalAnimScale = 1.0F;
};

} // namespace engine::animation
