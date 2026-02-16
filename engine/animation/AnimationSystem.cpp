#include "engine/animation/AnimationSystem.hpp"

#include <fstream>

#include <nlohmann/json.hpp>

namespace engine::animation
{

namespace
{
// Default profile values
LocomotionProfile MakeDefaultProfile()
{
    return LocomotionProfile{};
}
} // namespace

bool AnimationSystem::LoadProfile(const std::filesystem::path& path)
{
    std::ifstream stream(path);
    if (!stream.is_open())
    {
        // Create default profile
        m_profile = MakeDefaultProfile();
        m_stateMachine.SetProfile(m_profile);
        return false;
    }

    try
    {
        nlohmann::json root;
        stream >> root;

        if (root.contains("idle_epsilon") && root["idle_epsilon"].is_number())
        {
            m_profile.idleEpsilon = root["idle_epsilon"].get<float>();
        }
        if (root.contains("run_threshold") && root["run_threshold"].is_number())
        {
            m_profile.runThreshold = root["run_threshold"].get<float>();
        }
        if (root.contains("blend_idle_walk") && root["blend_idle_walk"].is_number())
        {
            m_profile.blendIdleWalk = root["blend_idle_walk"].get<float>();
        }
        if (root.contains("blend_walk_run") && root["blend_walk_run"].is_number())
        {
            m_profile.blendWalkRun = root["blend_walk_run"].get<float>();
        }
        if (root.contains("blend_run_idle") && root["blend_run_idle"].is_number())
        {
            m_profile.blendRunIdle = root["blend_run_idle"].get<float>();
        }
        if (root.contains("global_anim_scale") && root["global_anim_scale"].is_number())
        {
            m_profile.globalAnimScale = root["global_anim_scale"].get<float>();
        }
        if (root.contains("walk_speed_ref") && root["walk_speed_ref"].is_number())
        {
            m_profile.walkSpeedRef = root["walk_speed_ref"].get<float>();
        }
        if (root.contains("run_speed_ref") && root["run_speed_ref"].is_number())
        {
            m_profile.runSpeedRef = root["run_speed_ref"].get<float>();
        }
        if (root.contains("min_walk_scale") && root["min_walk_scale"].is_number())
        {
            m_profile.minWalkScale = root["min_walk_scale"].get<float>();
        }
        if (root.contains("max_walk_scale") && root["max_walk_scale"].is_number())
        {
            m_profile.maxWalkScale = root["max_walk_scale"].get<float>();
        }
        if (root.contains("min_run_scale") && root["min_run_scale"].is_number())
        {
            m_profile.minRunScale = root["min_run_scale"].get<float>();
        }
        if (root.contains("max_run_scale") && root["max_run_scale"].is_number())
        {
            m_profile.maxRunScale = root["max_run_scale"].get<float>();
        }
        if (root.contains("idle_clip_name") && root["idle_clip_name"].is_string())
        {
            m_profile.idleClipName = root["idle_clip_name"].get<std::string>();
        }
        if (root.contains("walk_clip_name") && root["walk_clip_name"].is_string())
        {
            m_profile.walkClipName = root["walk_clip_name"].get<std::string>();
        }
        if (root.contains("run_clip_name") && root["run_clip_name"].is_string())
        {
            m_profile.runClipName = root["run_clip_name"].get<std::string>();
        }

        m_stateMachine.SetProfile(m_profile);
        return true;
    }
    catch (const std::exception&)
    {
        m_profile = MakeDefaultProfile();
        m_stateMachine.SetProfile(m_profile);
        return false;
    }
}

bool AnimationSystem::SaveProfile(const std::filesystem::path& path) const
{
    std::ofstream stream(path);
    if (!stream.is_open())
    {
        return false;
    }

    nlohmann::json root;
    root["asset_version"] = 1;
    root["idle_epsilon"] = m_profile.idleEpsilon;
    root["run_threshold"] = m_profile.runThreshold;
    root["blend_idle_walk"] = m_profile.blendIdleWalk;
    root["blend_walk_run"] = m_profile.blendWalkRun;
    root["blend_run_idle"] = m_profile.blendRunIdle;
    root["global_anim_scale"] = m_profile.globalAnimScale;
    root["walk_speed_ref"] = m_profile.walkSpeedRef;
    root["run_speed_ref"] = m_profile.runSpeedRef;
    root["min_walk_scale"] = m_profile.minWalkScale;
    root["max_walk_scale"] = m_profile.maxWalkScale;
    root["min_run_scale"] = m_profile.minRunScale;
    root["max_run_scale"] = m_profile.maxRunScale;
    root["idle_clip_name"] = m_profile.idleClipName;
    root["walk_clip_name"] = m_profile.walkClipName;
    root["run_clip_name"] = m_profile.runClipName;

    stream << root.dump(2) << "\n";
    return true;
}

void AnimationSystem::AddClip(std::unique_ptr<AnimationClip> clip)
{
    if (clip == nullptr || clip->name.empty())
    {
        return;
    }

    const std::string name = clip->name;
    m_clips[name] = std::move(clip);

    if (m_clipLoadedCallback)
    {
        m_clipLoadedCallback(name);
    }
}

const AnimationClip* AnimationSystem::GetClip(const std::string& name) const
{
    const auto it = m_clips.find(name);
    return it != m_clips.end() ? it->second.get() : nullptr;
}

std::vector<std::string> AnimationSystem::ListClips() const
{
    std::vector<std::string> names;
    names.reserve(m_clips.size());
    for (const auto& [name, clip] : m_clips)
    {
        names.push_back(name);
    }
    std::sort(names.begin(), names.end());
    return names;
}

void AnimationSystem::ClearClips()
{
    // Clear state-machine clip bindings first so no player keeps pointers
    // to clip storage that is about to be freed.
    m_stateMachine.SetClips(nullptr, nullptr, nullptr);
    m_clips.clear();
}

void AnimationSystem::InitializeStateMachine()
{
    const AnimationClip* idle = GetClip(m_profile.idleClipName);
    const AnimationClip* walk = GetClip(m_profile.walkClipName);
    const AnimationClip* run = GetClip(m_profile.runClipName);

    m_stateMachine.SetProfile(m_profile);
    m_stateMachine.SetClips(idle, walk, run);
}

void AnimationSystem::Update(float dt, float currentSpeed)
{
    m_stateMachine.Update(dt, currentSpeed);
}

void AnimationSystem::ForceState(LocomotionState state)
{
    m_stateMachine.SetAutoMode(false);
    m_stateMachine.ForceState(state);
}

void AnimationSystem::SetAutoMode(bool autoMode)
{
    m_stateMachine.SetAutoMode(autoMode);
}

std::string AnimationSystem::GetDebugInfo() const
{
    std::string info;
    info += "State: " + std::string(LocomotionStateToString(m_stateMachine.CurrentState()));
    info += " | Speed: " + std::to_string(m_stateMachine.CurrentPlaybackSpeed()).substr(0, 5);
    info += " | Blending: " + std::string(m_stateMachine.IsBlending() ? "yes" : "no");
    if (m_stateMachine.IsBlending())
    {
        info += " (" + std::to_string(static_cast<int>(m_stateMachine.BlendWeight() * 100.0F)) + "%)";
    }
    info += " | Auto: " + std::string(m_stateMachine.IsAutoMode() ? "yes" : "no");

    const auto* clip = m_stateMachine.GetBlender().GetCurrentClip();
    if (clip != nullptr)
    {
        info += " | Clip: " + clip->name;
        const float progress = m_stateMachine.GetBlender().TargetPlayer().Progress();
        info += " [" + std::to_string(static_cast<int>(progress * 100.0F)) + "%]";
    }

    return info;
}

} // namespace engine::animation
