#pragma once

#include <cmath>
#include <functional>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

#include <glm/vec2.hpp>
#include <glm/vec4.hpp>

#include "engine/ui/UiNode.hpp"

namespace engine::ui
{
// Easing function type
using EasingFunc = std::function<float(float)>;

// Property value that can be animated
using AnimatableValue = std::variant<float, glm::vec2, glm::vec4>;

// Active transition state
struct ActiveTransition
{
    std::string property;
    float startTime = 0.0F;
    float duration = 0.2F;
    EasingFunc easing;
    AnimatableValue startValue;
    AnimatableValue endValue;
};

// Easing functions
namespace Easing
{
    float Linear(float t);
    float EaseIn(float t);
    float EaseOut(float t);
    float EaseInOut(float t);
    float EaseInQuad(float t);
    float EaseOutQuad(float t);
    float EaseInOutQuad(float t);
    float EaseInCubic(float t);
    float EaseOutCubic(float t);
    float EaseInOutCubic(float t);
    float EaseInQuart(float t);
    float EaseOutQuart(float t);
    float EaseInOutQuart(float t);
    float EaseInExpo(float t);
    float EaseOutExpo(float t);
    float EaseInOutExpo(float t);
    float EaseInBack(float t);
    float EaseOutBack(float t);
    float EaseInOutBack(float t);
    float Bounce(float t);
    float Elastic(float t);

    // Get easing function by name
    EasingFunc GetEasing(TransitionDef::Ease ease);
    EasingFunc GetEasingByName(const std::string& name);

} // namespace Easing

// Interpolate between two values
AnimatableValue Interpolate(const AnimatableValue& start, const AnimatableValue& end, float t);

// Animation system for UI nodes
class UiAnimationSystem
{
public:
    UiAnimationSystem() = default;

    // Update all active transitions
    void Update(float currentTime, float deltaSeconds);

    // Start a transition on a node property
    void StartTransition(
        UINode& node,
        const std::string& property,
        const AnimatableValue& endValue,
        float duration = 0.2F,
        EasingFunc easing = Easing::EaseOut
    );

    // Start a transition using a transition definition
    void StartTransitionFromDef(UINode& node, const TransitionDef& def, const AnimatableValue& endValue);

    // Cancel all transitions for a node
    void CancelTransitions(UINode& node);

    // Cancel a specific transition
    void CancelTransition(UINode& node, const std::string& property);

    // Check if a node has any active transitions
    [[nodiscard]] bool HasActiveTransitions(const UINode& node) const;

    // Get the current animated value for a property (or nullopt if not animating)
    [[nodiscard]] std::optional<AnimatableValue> GetAnimatedValue(const UINode& node, const std::string& property) const;

    // Apply animated values to node's computed properties
    void ApplyAnimatedValues(UINode& node) const;

private:
    // Get a property value from a node
    [[nodiscard]] std::optional<AnimatableValue> GetNodePropertyValue(const UINode& node, const std::string& property) const;

    // Set a property value on a node
    void SetNodePropertyValue(UINode& node, const std::string& property, const AnimatableValue& value) const;

    // Active transitions keyed by node ID + property
    std::unordered_map<std::string, ActiveTransition> m_transitions;
};

// Animation clip for complex multi-property animations
struct AnimationClip
{
    std::string name;
    float duration = 1.0F;
    bool looping = false;

    struct Keyframe
    {
        float time = 0.0F;
        std::string property;
        AnimatableValue value;
        EasingFunc easing;
    };

    std::vector<Keyframe> keyframes;
};

// Animation player for playing animation clips
class UiAnimationPlayer
{
public:
    void Play(const AnimationClip& clip, UINode& target);
    void Stop();
    void Pause();
    void Resume();

    void Update(float deltaSeconds);

    [[nodiscard]] bool IsPlaying() const
    {
        return m_playing;
    }
    [[nodiscard]] bool IsPaused() const
    {
        return m_paused;
    }
    [[nodiscard]] float GetTime() const
    {
        return m_time;
    }
    [[nodiscard]] float GetDuration() const
    {
        return m_clip ? m_clip->duration : 0.0F;
    }
    [[nodiscard]] float GetProgress() const
    {
        return m_clip && m_clip->duration > 0.0F ? m_time / m_clip->duration : 0.0F;
    }

private:
    const AnimationClip* m_clip = nullptr;
    UINode* m_target = nullptr;
    float m_time = 0.0F;
    bool m_playing = false;
    bool m_paused = false;
};

} // namespace engine::ui
