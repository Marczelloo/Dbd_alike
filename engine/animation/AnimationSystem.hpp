#pragma once

#include "engine/animation/AnimationClip.hpp"
#include "engine/animation/AnimationStateMachine.hpp"

#include <filesystem>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace engine::animation
{

// Manages all animation clips and state machines
class AnimationSystem
{
public:
    AnimationSystem() = default;

    // Load animation profile from JSON
    bool LoadProfile(const std::filesystem::path& path);
    bool SaveProfile(const std::filesystem::path& path) const;

    // Get/Set profile directly
    [[nodiscard]] const LocomotionProfile& GetProfile() const { return m_profile; }
    void SetProfile(const LocomotionProfile& profile) { m_profile = profile; m_stateMachine.SetProfile(profile); }

    // Add an animation clip (takes ownership)
    void AddClip(std::unique_ptr<AnimationClip> clip);

    // Get clip by name
    [[nodiscard]] const AnimationClip* GetClip(const std::string& name) const;

    // List all clip names
    [[nodiscard]] std::vector<std::string> ListClips() const;

    // Clear all clips
    void ClearClips();

    // Initialize state machine with default clips
    void InitializeStateMachine();

    // Update state machine with current speed
    void Update(float dt, float currentSpeed);

    // Get state machine
    [[nodiscard]] const AnimationStateMachine& GetStateMachine() const { return m_stateMachine; }
    [[nodiscard]] AnimationStateMachine& GetStateMachineMut() { return m_stateMachine; }

    // Current state info
    [[nodiscard]] LocomotionState CurrentState() const { return m_stateMachine.CurrentState(); }
    [[nodiscard]] float CurrentPlaybackSpeed() const { return m_stateMachine.CurrentPlaybackSpeed(); }

    // Force state (for debugging)
    void ForceState(LocomotionState state);
    void SetAutoMode(bool autoMode);

    // Debug info
    [[nodiscard]] std::string GetDebugInfo() const;

    // Callback for clip loading from GLTF
    using ClipLoadedCallback = std::function<void(const std::string& clipName)>;
    void SetClipLoadedCallback(ClipLoadedCallback callback) { m_clipLoadedCallback = std::move(callback); }

private:
    std::unordered_map<std::string, std::unique_ptr<AnimationClip>> m_clips;
    LocomotionProfile m_profile;
    AnimationStateMachine m_stateMachine;
    ClipLoadedCallback m_clipLoadedCallback;
};

} // namespace engine::animation
