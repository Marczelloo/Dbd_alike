#include "engine/ui/UiAnimation.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>

#include <glm/gtx/compatibility.hpp>

namespace engine::ui
{
// --- Easing Functions ---

namespace Easing
{
    float Linear(float t)
    {
        return t;
    }

    float EaseIn(float t)
    {
        return t * t;
    }

    float EaseOut(float t)
    {
        return t * (2.0F - t);
    }

    float EaseInOut(float t)
    {
        return t < 0.5F ? 2.0F * t * t : -1.0F + (4.0F - 2.0F * t) * t;
    }

    float EaseInQuad(float t)
    {
        return t * t;
    }

    float EaseOutQuad(float t)
    {
        return t * (2.0F - t);
    }

    float EaseInOutQuad(float t)
    {
        return t < 0.5F ? 2.0F * t * t : -1.0F + (4.0F - 2.0F * t) * t;
    }

    float EaseInCubic(float t)
    {
        return t * t * t;
    }

    float EaseOutCubic(float t)
    {
        float t1 = t - 1.0F;
        return t1 * t1 * t1 + 1.0F;
    }

    float EaseInOutCubic(float t)
    {
        return t < 0.5F ? 4.0F * t * t * t : (t - 1.0F) * (2.0F * t - 2.0F) * (2.0F * t - 2.0F) + 1.0F;
    }

    float EaseInQuart(float t)
    {
        return t * t * t * t;
    }

    float EaseOutQuart(float t)
    {
        float t1 = t - 1.0F;
        return 1.0F - t1 * t1 * t1 * t1;
    }

    float EaseInOutQuart(float t)
    {
        float t1 = t - 1.0F;
        return t < 0.5F ? 8.0F * t * t * t * t : 1.0F - 8.0F * t1 * t1 * t1 * t1;
    }

    float EaseInExpo(float t)
    {
        return t == 0.0F ? 0.0F : std::pow(1024.0F, t - 1.0F);
    }

    float EaseOutExpo(float t)
    {
        return t == 1.0F ? 1.0F : 1.0F - std::pow(2.0F, -10.0F * t);
    }

    float EaseInOutExpo(float t)
    {
        if (t == 0.0F)
            return 0.0F;
        if (t == 1.0F)
            return 1.0F;
        if (t < 0.5F)
            return std::pow(2.0F, 20.0F * t - 10.0F) * 0.5F;
        return (2.0F - std::pow(2.0F, -20.0F * t + 10.0F)) * 0.5F;
    }

    float EaseInBack(float t)
    {
        constexpr float c1 = 1.70158F;
        constexpr float c3 = c1 + 1.0F;
        return c3 * t * t * t - c1 * t * t;
    }

    float EaseOutBack(float t)
    {
        constexpr float c1 = 1.70158F;
        constexpr float c3 = c1 + 1.0F;
        float t1 = t - 1.0F;
        return 1.0F + c3 * t1 * t1 * t1 + c1 * t1 * t1;
    }

    float EaseInOutBack(float t)
    {
        constexpr float c1 = 1.70158F;
        constexpr float c2 = c1 * 1.525F;
        return t < 0.5F
            ? (std::pow(2.0F * t, 2.0F) * ((c2 + 1.0F) * 2.0F * t - c2)) * 0.5F
            : (std::pow(2.0F * t - 2.0F, 2.0F) * ((c2 + 1.0F) * (t * 2.0F - 2.0F) + c2) + 2.0F) * 0.5F;
    }

    float Bounce(float t)
    {
        // EaseOutBounce
        constexpr float n = 7.5625F;
        constexpr float d = 2.75F;

        if (t < 1.0F / d)
        {
            return n * t * t;
        }
        else if (t < 2.0F / d)
        {
            float t1 = t - 1.5F / d;
            return n * t1 * t1 + 0.75F;
        }
        else if (t < 2.5F / d)
        {
            float t1 = t - 2.25F / d;
            return n * t1 * t1 + 0.9375F;
        }
        else
        {
            float t1 = t - 2.625F / d;
            return n * t1 * t1 + 0.984375F;
        }
    }

    float Elastic(float t)
    {
        // EaseOutElastic
        constexpr float c4 = (2.0F * 3.14159265F) / 3.0F;

        if (t == 0.0F)
            return 0.0F;
        if (t == 1.0F)
            return 1.0F;

        return std::pow(2.0F, -10.0F * t) * std::sin((t * 10.0F - 0.75F) * c4) + 1.0F;
    }

    EasingFunc GetEasing(TransitionDef::Ease ease)
    {
        switch (ease)
        {
            case TransitionDef::Ease::Linear:
                return Linear;
            case TransitionDef::Ease::EaseIn:
                return EaseIn;
            case TransitionDef::Ease::EaseOut:
                return EaseOut;
            case TransitionDef::Ease::EaseInOut:
                return EaseInOut;
            case TransitionDef::Ease::EaseInQuad:
                return EaseInQuad;
            case TransitionDef::Ease::EaseOutQuad:
                return EaseOutQuad;
            case TransitionDef::Ease::EaseInOutQuad:
                return EaseInOutQuad;
            case TransitionDef::Ease::EaseInCubic:
                return EaseInCubic;
            case TransitionDef::Ease::EaseOutCubic:
                return EaseOutCubic;
            case TransitionDef::Ease::EaseInOutCubic:
                return EaseInOutCubic;
            default:
                return EaseOut;
        }
    }

    EasingFunc GetEasingByName(const std::string& name)
    {
        if (name == "linear")
            return Linear;
        if (name == "easeIn" || name == "ease-in")
            return EaseIn;
        if (name == "easeOut" || name == "ease-out")
            return EaseOut;
        if (name == "easeInOut" || name == "ease-in-out")
            return EaseInOut;
        if (name == "easeInQuad")
            return EaseInQuad;
        if (name == "easeOutQuad")
            return EaseOutQuad;
        if (name == "easeInOutQuad")
            return EaseInOutQuad;
        if (name == "easeInCubic")
            return EaseInCubic;
        if (name == "easeOutCubic")
            return EaseOutCubic;
        if (name == "easeInOutCubic")
            return EaseInOutCubic;
        if (name == "easeInQuart")
            return EaseInQuart;
        if (name == "easeOutQuart")
            return EaseOutQuart;
        if (name == "easeInOutQuart")
            return EaseInOutQuart;
        if (name == "easeInExpo")
            return EaseInExpo;
        if (name == "easeOutExpo")
            return EaseOutExpo;
        if (name == "easeInOutExpo")
            return EaseInOutExpo;
        if (name == "easeInBack")
            return EaseInBack;
        if (name == "easeOutBack")
            return EaseOutBack;
        if (name == "easeInOutBack")
            return EaseInOutBack;
        if (name == "bounce")
            return Bounce;
        if (name == "elastic")
            return Elastic;
        return EaseOut;
    }

} // namespace Easing

// --- Interpolation ---

AnimatableValue Interpolate(const AnimatableValue& start, const AnimatableValue& end, float t)
{
    if (std::holds_alternative<float>(start) && std::holds_alternative<float>(end))
    {
        float s = std::get<float>(start);
        float e = std::get<float>(end);
        return s + (e - s) * t;
    }
    else if (std::holds_alternative<glm::vec2>(start) && std::holds_alternative<glm::vec2>(end))
    {
        const glm::vec2& s = std::get<glm::vec2>(start);
        const glm::vec2& e = std::get<glm::vec2>(end);
        return glm::mix(s, e, t);
    }
    else if (std::holds_alternative<glm::vec4>(start) && std::holds_alternative<glm::vec4>(end))
    {
        const glm::vec4& s = std::get<glm::vec4>(start);
        const glm::vec4& e = std::get<glm::vec4>(end);
        return glm::mix(s, e, t);
    }

    // Types don't match - return end value at t=1, start value at t=0
    return t >= 1.0F ? end : start;
}

// --- UiAnimationSystem ---

void UiAnimationSystem::Update(float currentTime, float deltaSeconds)
{
    (void)deltaSeconds;

    // Remove completed transitions
    std::vector<std::string> toRemove;
    for (auto& [key, transition] : m_transitions)
    {
        float elapsed = currentTime - transition.startTime;
        if (elapsed >= transition.duration)
        {
            toRemove.push_back(key);
        }
    }

    for (const auto& key : toRemove)
    {
        m_transitions.erase(key);
    }
}

void UiAnimationSystem::StartTransition(
    UINode& node,
    const std::string& property,
    const AnimatableValue& endValue,
    float duration,
    EasingFunc easing
)
{
    // Get current value
    auto currentValue = GetNodePropertyValue(node, property);
    if (!currentValue.has_value())
    {
        // Can't animate if we don't have a current value
        return;
    }

    std::string key = node.id + "/" + property;

    ActiveTransition transition;
    transition.property = property;
    transition.startTime = static_cast<float>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count()) / 1000.0F;
    transition.duration = duration;
    transition.easing = std::move(easing);
    transition.startValue = *currentValue;
    transition.endValue = endValue;

    m_transitions[key] = transition;
}

void UiAnimationSystem::StartTransitionFromDef(UINode& node, const TransitionDef& def, const AnimatableValue& endValue)
{
    StartTransition(node, def.property, endValue, def.duration, Easing::GetEasing(def.ease));
}

void UiAnimationSystem::CancelTransitions(UINode& node)
{
    std::vector<std::string> toRemove;
    for (auto& [key, transition] : m_transitions)
    {
        if (key.find(node.id + "/") == 0)
        {
            toRemove.push_back(key);
        }
    }
    for (const auto& key : toRemove)
    {
        m_transitions.erase(key);
    }
}

void UiAnimationSystem::CancelTransition(UINode& node, const std::string& property)
{
    std::string key = node.id + "/" + property;
    m_transitions.erase(key);
}

bool UiAnimationSystem::HasActiveTransitions(const UINode& node) const
{
    std::string prefix = node.id + "/";
    for (const auto& [key, transition] : m_transitions)
    {
        if (key.find(prefix) == 0)
        {
            return true;
        }
    }
    return false;
}

std::optional<AnimatableValue> UiAnimationSystem::GetAnimatedValue(const UINode& node, const std::string& property) const
{
    std::string key = node.id + "/" + property;
    auto it = m_transitions.find(key);
    if (it == m_transitions.end())
    {
        return std::nullopt;
    }

    const ActiveTransition& transition = it->second;

    float currentTime = static_cast<float>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count()) / 1000.0F;
    float elapsed = currentTime - transition.startTime;
    float t = std::clamp(elapsed / transition.duration, 0.0F, 1.0F);

    float easedT = transition.easing(t);
    return Interpolate(transition.startValue, transition.endValue, easedT);
}

void UiAnimationSystem::ApplyAnimatedValues(UINode& node) const
{
    // Apply opacity
    auto opacityValue = GetAnimatedValue(node, "opacity");
    if (opacityValue && std::holds_alternative<float>(*opacityValue))
    {
        node.computedOpacity = std::get<float>(*opacityValue);
    }

    // Apply backgroundColor
    auto bgColorValue = GetAnimatedValue(node, "backgroundColor");
    if (bgColorValue && std::holds_alternative<glm::vec4>(*bgColorValue))
    {
        node.computedBackgroundColor = std::get<glm::vec4>(*bgColorValue);
    }

    // Apply textColor
    auto textColorValue = GetAnimatedValue(node, "textColor");
    if (textColorValue && std::holds_alternative<glm::vec4>(*textColorValue))
    {
        node.computedTextColor = std::get<glm::vec4>(*textColorValue);
    }

    // Apply translate (offset)
    auto translateValue = GetAnimatedValue(node, "translate");
    if (translateValue && std::holds_alternative<glm::vec2>(*translateValue))
    {
        auto offset = std::get<glm::vec2>(*translateValue);
        node.layout.offset = offset;
    }

    // Apply scale
    auto scaleValue = GetAnimatedValue(node, "scale");
    if (scaleValue && std::holds_alternative<float>(*scaleValue))
    {
        // Scale would affect rendering - store for renderer
        // For now, just note it could be applied
    }
}

std::optional<AnimatableValue> UiAnimationSystem::GetNodePropertyValue(const UINode& node, const std::string& property) const
{
    if (property == "opacity")
    {
        return node.computedOpacity;
    }
    else if (property == "backgroundColor")
    {
        return node.computedBackgroundColor;
    }
    else if (property == "textColor")
    {
        return node.computedTextColor;
    }
    else if (property == "translate")
    {
        return node.layout.offset;
    }
    else if (property == "radius")
    {
        return node.computedRadius;
    }

    return std::nullopt;
}

void UiAnimationSystem::SetNodePropertyValue(UINode& node, const std::string& property, const AnimatableValue& value) const
{
    if (property == "opacity" && std::holds_alternative<float>(value))
    {
        node.computedOpacity = std::get<float>(value);
    }
    else if (property == "backgroundColor" && std::holds_alternative<glm::vec4>(value))
    {
        node.computedBackgroundColor = std::get<glm::vec4>(value);
    }
    else if (property == "textColor" && std::holds_alternative<glm::vec4>(value))
    {
        node.computedTextColor = std::get<glm::vec4>(value);
    }
    else if (property == "translate" && std::holds_alternative<glm::vec2>(value))
    {
        node.layout.offset = std::get<glm::vec2>(value);
    }
    else if (property == "radius" && std::holds_alternative<float>(value))
    {
        node.computedRadius = std::get<float>(value);
    }
}

// --- UiAnimationPlayer ---

void UiAnimationPlayer::Play(const AnimationClip& clip, UINode& target)
{
    m_clip = &clip;
    m_target = &target;
    m_time = 0.0F;
    m_playing = true;
    m_paused = false;
}

void UiAnimationPlayer::Stop()
{
    m_playing = false;
    m_paused = false;
    m_time = 0.0F;
    m_clip = nullptr;
    m_target = nullptr;
}

void UiAnimationPlayer::Pause()
{
    m_paused = true;
}

void UiAnimationPlayer::Resume()
{
    m_paused = false;
}

void UiAnimationPlayer::Update(float deltaSeconds)
{
    if (!m_playing || m_paused || !m_clip || !m_target)
    {
        return;
    }

    m_time += deltaSeconds;

    // Check for end
    if (m_time >= m_clip->duration)
    {
        if (m_clip->looping)
        {
            m_time = std::fmod(m_time, m_clip->duration);
        }
        else
        {
            m_time = m_clip->duration;
            m_playing = false;
        }
    }

    // Apply keyframes
    for (const auto& keyframe : m_clip->keyframes)
    {
        // Find surrounding keyframes for this property
        if (keyframe.time <= m_time)
        {
            // For simplicity, just apply the value
            // A more complete implementation would interpolate between keyframes
        }
    }
}

} // namespace engine::ui
