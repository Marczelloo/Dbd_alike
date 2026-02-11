#include "engine/audio/AudioSystem.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#define MINIAUDIO_IMPLEMENTATION
#include "external/miniaudio/miniaudio.h"

namespace engine::audio
{
struct AudioSystem::ActiveSound
{
    ma_sound sound{};
    bool initialized = false;
    bool looping = false;
    AudioSystem::Bus bus = AudioSystem::Bus::Sfx;
};

struct AudioSystem::EngineData
{
    ma_engine engine{};
    ma_sound_group musicGroup{};
    ma_sound_group sfxGroup{};
    ma_sound_group uiGroup{};
    ma_sound_group ambienceGroup{};
    bool musicGroupInitialized = false;
    bool sfxGroupInitialized = false;
    bool uiGroupInitialized = false;
    bool ambienceGroupInitialized = false;
    std::unordered_map<AudioSystem::SoundHandle, std::unique_ptr<AudioSystem::ActiveSound>> sounds;
};

namespace
{
float Clamped01(float value)
{
    return std::clamp(value, 0.0F, 1.0F);
}
} // namespace

bool AudioSystem::Initialize(const std::string& assetRoot)
{
    Shutdown();
    m_assetRoot = assetRoot;

    m_engine = new EngineData{};
    if (ma_engine_init(nullptr, &m_engine->engine) != MA_SUCCESS)
    {
        delete m_engine;
        m_engine = nullptr;
        return false;
    }

    if (ma_sound_group_init(&m_engine->engine, 0, nullptr, &m_engine->musicGroup) == MA_SUCCESS)
    {
        m_engine->musicGroupInitialized = true;
    }
    if (ma_sound_group_init(&m_engine->engine, 0, nullptr, &m_engine->sfxGroup) == MA_SUCCESS)
    {
        m_engine->sfxGroupInitialized = true;
    }
    if (ma_sound_group_init(&m_engine->engine, 0, nullptr, &m_engine->uiGroup) == MA_SUCCESS)
    {
        m_engine->uiGroupInitialized = true;
    }
    if (ma_sound_group_init(&m_engine->engine, 0, nullptr, &m_engine->ambienceGroup) == MA_SUCCESS)
    {
        m_engine->ambienceGroupInitialized = true;
    }

    m_initialized = true;
    for (int i = 0; i < static_cast<int>(m_busVolume.size()); ++i)
    {
        SetBusVolume(static_cast<Bus>(i), m_busVolume[static_cast<std::size_t>(i)]);
    }
    return true;
}

void AudioSystem::Shutdown()
{
    if (m_engine == nullptr)
    {
        m_active.clear();
        m_initialized = false;
        return;
    }

    StopAll();

    if (m_engine->ambienceGroupInitialized)
    {
        ma_sound_group_uninit(&m_engine->ambienceGroup);
        m_engine->ambienceGroupInitialized = false;
    }
    if (m_engine->uiGroupInitialized)
    {
        ma_sound_group_uninit(&m_engine->uiGroup);
        m_engine->uiGroupInitialized = false;
    }
    if (m_engine->sfxGroupInitialized)
    {
        ma_sound_group_uninit(&m_engine->sfxGroup);
        m_engine->sfxGroupInitialized = false;
    }
    if (m_engine->musicGroupInitialized)
    {
        ma_sound_group_uninit(&m_engine->musicGroup);
        m_engine->musicGroupInitialized = false;
    }

    ma_engine_uninit(&m_engine->engine);
    delete m_engine;
    m_engine = nullptr;
    m_active.clear();
    m_initialized = false;
}

void AudioSystem::Update(float /*deltaSeconds*/)
{
    if (!m_initialized || m_engine == nullptr)
    {
        return;
    }

    std::vector<SoundHandle> finished;
    finished.reserve(m_engine->sounds.size());
    for (const auto& [handle, active] : m_engine->sounds)
    {
        if (active == nullptr || !active->initialized || active->looping)
        {
            continue;
        }
        if (ma_sound_at_end(&active->sound) == MA_TRUE)
        {
            finished.push_back(handle);
        }
    }

    for (const SoundHandle handle : finished)
    {
        Stop(handle);
    }
}

AudioSystem::SoundHandle AudioSystem::PlayOneShot(const std::string& clipName, Bus bus)
{
    return PlayOneShot(clipName, bus, PlayOptions{});
}

AudioSystem::SoundHandle AudioSystem::PlayOneShot(const std::string& clipName, Bus bus, const PlayOptions& options)
{
    if (!m_initialized || m_engine == nullptr)
    {
        return 0;
    }

    const std::string path = ResolveClipPath(clipName);
    if (path.empty())
    {
        return 0;
    }

    auto active = std::make_unique<ActiveSound>();
    active->looping = false;
    active->bus = bus;

    const ma_uint32 flags = MA_SOUND_FLAG_ASYNC;
    if (ma_sound_init_from_file(
            &m_engine->engine,
            path.c_str(),
            flags,
            reinterpret_cast<ma_sound_group*>(GroupForBus(bus)),
            nullptr,
            &active->sound
        ) != MA_SUCCESS)
    {
        return 0;
    }

    active->initialized = true;
    ma_sound_set_looping(&active->sound, MA_FALSE);
    ma_sound_set_volume(&active->sound, std::max(0.0F, options.volume));
    ma_sound_set_pitch(&active->sound, std::max(0.01F, options.pitch));

    if (options.position.has_value())
    {
        const glm::vec3 p = *options.position;
        ma_sound_set_spatialization_enabled(&active->sound, MA_TRUE);
        ma_sound_set_position(&active->sound, p.x, p.y, p.z);
        ma_sound_set_min_distance(&active->sound, std::max(0.1F, options.minDistance));
        ma_sound_set_max_distance(&active->sound, std::max(options.minDistance + 0.1F, options.maxDistance));
    }
    else
    {
        ma_sound_set_spatialization_enabled(&active->sound, MA_FALSE);
    }

    if (ma_sound_start(&active->sound) != MA_SUCCESS)
    {
        ma_sound_uninit(&active->sound);
        return 0;
    }

    const SoundHandle handle = m_nextHandle++;
    m_active[handle] = active.get();
    m_engine->sounds.emplace(handle, std::move(active));
    return handle;
}

AudioSystem::SoundHandle AudioSystem::PlayLoop(const std::string& clipName, Bus bus)
{
    return PlayLoop(clipName, bus, PlayOptions{}, 0.0F);
}

AudioSystem::SoundHandle AudioSystem::PlayLoop(const std::string& clipName, Bus bus, const PlayOptions& options, float loopDurationSeconds)
{
    (void)loopDurationSeconds; // Reserved for future use (e.g., auto-stop after duration)
    if (!m_initialized || m_engine == nullptr)
    {
        return 0;
    }

    const std::string path = ResolveClipPath(clipName);
    if (path.empty())
    {
        return 0;
    }

    auto active = std::make_unique<ActiveSound>();
    active->looping = true;
    active->bus = bus;

    const ma_uint32 flags = MA_SOUND_FLAG_STREAM | MA_SOUND_FLAG_ASYNC;
    if (ma_sound_init_from_file(
            &m_engine->engine,
            path.c_str(),
            flags,
            reinterpret_cast<ma_sound_group*>(GroupForBus(bus)),
            nullptr,
            &active->sound
        ) != MA_SUCCESS)
    {
        return 0;
    }

    active->initialized = true;
    ma_sound_set_looping(&active->sound, MA_TRUE);
    ma_sound_set_volume(&active->sound, std::max(0.0F, options.volume));
    ma_sound_set_pitch(&active->sound, std::max(0.01F, options.pitch));

    if (options.position.has_value())
    {
        const glm::vec3 p = *options.position;
        ma_sound_set_spatialization_enabled(&active->sound, MA_TRUE);
        ma_sound_set_position(&active->sound, p.x, p.y, p.z);
        ma_sound_set_min_distance(&active->sound, std::max(0.1F, options.minDistance));
        ma_sound_set_max_distance(&active->sound, std::max(options.minDistance + 0.1F, options.maxDistance));
    }
    else
    {
        ma_sound_set_spatialization_enabled(&active->sound, MA_FALSE);
    }

    if (ma_sound_start(&active->sound) != MA_SUCCESS)
    {
        ma_sound_uninit(&active->sound);
        return 0;
    }

    const SoundHandle handle = m_nextHandle++;
    m_active[handle] = active.get();
    m_engine->sounds.emplace(handle, std::move(active));
    return handle;
}

void AudioSystem::Stop(SoundHandle handle)
{
    if (!m_initialized || m_engine == nullptr)
    {
        return;
    }

    const auto it = m_engine->sounds.find(handle);
    if (it == m_engine->sounds.end())
    {
        return;
    }

    ActiveSound* active = it->second.get();
    if (active != nullptr && active->initialized)
    {
        ma_sound_stop(&active->sound);
        ma_sound_uninit(&active->sound);
        active->initialized = false;
    }

    m_engine->sounds.erase(it);
    m_active.erase(handle);
}

void AudioSystem::StopAll()
{
    if (!m_initialized || m_engine == nullptr)
    {
        return;
    }

    std::vector<SoundHandle> handles;
    handles.reserve(m_engine->sounds.size());
    for (const auto& [handle, _] : m_engine->sounds)
    {
        handles.push_back(handle);
    }
    for (const SoundHandle handle : handles)
    {
        Stop(handle);
    }
}

bool AudioSystem::SetHandleVolume(SoundHandle handle, float volume)
{
    if (!m_initialized || m_engine == nullptr)
    {
        return false;
    }
    const auto it = m_engine->sounds.find(handle);
    if (it == m_engine->sounds.end() || it->second == nullptr || !it->second->initialized)
    {
        return false;
    }
    ma_sound_set_volume(&it->second->sound, std::max(0.0F, volume));
    return true;
}

void AudioSystem::SetBusVolume(Bus bus, float value01)
{
    const float clamped = Clamped01(value01);
    m_busVolume[static_cast<std::size_t>(bus)] = clamped;

    if (!m_initialized || m_engine == nullptr)
    {
        return;
    }

    if (bus == Bus::Master)
    {
        ma_engine_set_volume(&m_engine->engine, clamped);
        return;
    }

    ma_sound_group* group = reinterpret_cast<ma_sound_group*>(GroupForBus(bus));
    if (group != nullptr)
    {
        ma_sound_group_set_volume(group, clamped);
    }
}

float AudioSystem::GetBusVolume(Bus bus) const
{
    return m_busVolume[static_cast<std::size_t>(bus)];
}

void AudioSystem::SetListener(const glm::vec3& position, const glm::vec3& forward, const glm::vec3& up)
{
    if (!m_initialized || m_engine == nullptr)
    {
        return;
    }

    ma_engine_listener_set_position(&m_engine->engine, 0, position.x, position.y, position.z);
    ma_engine_listener_set_direction(&m_engine->engine, 0, forward.x, forward.y, forward.z);
    ma_engine_listener_set_world_up(&m_engine->engine, 0, up.x, up.y, up.z);
}

std::string AudioSystem::ResolveClipPath(const std::string& clipName) const
{
    namespace fs = std::filesystem;
    if (clipName.empty())
    {
        return {};
    }

    auto hasAudioExtension = [](const std::string& pathString) {
        const std::string ext = fs::path(pathString).extension().string();
        const std::string lower = [&ext]() {
            std::string out = ext;
            std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return out;
        }();
        return lower == ".wav" || lower == ".ogg" || lower == ".mp3" || lower == ".flac";
    };

    const fs::path direct(clipName);
    if ((IsAbsolutePathLike(clipName) || direct.has_parent_path()) && fs::exists(direct))
    {
        return direct.string();
    }

    const fs::path root(m_assetRoot);
    if (hasAudioExtension(clipName))
    {
        const fs::path candidate = root / clipName;
        if (fs::exists(candidate))
        {
            return candidate.string();
        }
        if (fs::exists(direct))
        {
            return direct.string();
        }
    }

    static const std::array<const char*, 4> kExt{".wav", ".ogg", ".mp3", ".flac"};
    for (const char* ext : kExt)
    {
        const fs::path candidate = root / (clipName + ext);
        if (fs::exists(candidate))
        {
            return candidate.string();
        }
    }

    return {};
}

bool AudioSystem::IsAbsolutePathLike(const std::string& value)
{
    if (value.size() >= 2 && std::isalpha(static_cast<unsigned char>(value[0])) != 0 && value[1] == ':')
    {
        return true;
    }
    if (!value.empty() && (value[0] == '/' || value[0] == '\\'))
    {
        return true;
    }
    return false;
}

void* AudioSystem::GroupForBus(Bus bus) const
{
    if (m_engine == nullptr)
    {
        return nullptr;
    }

    switch (bus)
    {
        case Bus::Music:
            return m_engine->musicGroupInitialized ? static_cast<void*>(&m_engine->musicGroup) : nullptr;
        case Bus::Sfx:
            return m_engine->sfxGroupInitialized ? static_cast<void*>(&m_engine->sfxGroup) : nullptr;
        case Bus::Ui:
            return m_engine->uiGroupInitialized ? static_cast<void*>(&m_engine->uiGroup) : nullptr;
        case Bus::Ambience:
            return m_engine->ambienceGroupInitialized ? static_cast<void*>(&m_engine->ambienceGroup) : nullptr;
        case Bus::Master:
        default:
            return nullptr;
    }
}
} // namespace engine::audio
