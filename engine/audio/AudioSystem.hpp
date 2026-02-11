#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>

#include <glm/vec3.hpp>

namespace engine::audio
{
class AudioSystem
{
public:
    enum class Bus
    {
        Master = 0,
        Music = 1,
        Sfx = 2,
        Ui = 3,
        Ambience = 4
    };

    struct PlayOptions
    {
        std::optional<glm::vec3> position;
        float volume = 1.0F;
        float pitch = 1.0F;
        float minDistance = 1.0F;
        float maxDistance = 64.0F;
    };

    using SoundHandle = std::uint64_t;

    bool Initialize(const std::string& assetRoot = "assets/audio");
    void Shutdown();

    [[nodiscard]] bool IsInitialized() const { return m_initialized; }

    void Update(float deltaSeconds);

    SoundHandle PlayOneShot(const std::string& clipName, Bus bus = Bus::Sfx);
    SoundHandle PlayOneShot(const std::string& clipName, Bus bus, const PlayOptions& options);
    SoundHandle PlayLoop(const std::string& clipName, Bus bus = Bus::Music);
    SoundHandle PlayLoop(const std::string& clipName, Bus bus, const PlayOptions& options, float loopDurationSeconds = 0.0F);
    SoundHandle PlayLoop(const std::string& clipName, Bus bus, const PlayOptions& options);
    void Stop(SoundHandle handle);
    void StopAll();
    [[nodiscard]] bool SetHandleVolume(SoundHandle handle, float volume);

    void SetBusVolume(Bus bus, float value01);
    [[nodiscard]] float GetBusVolume(Bus bus) const;

    void SetListener(const glm::vec3& position, const glm::vec3& forward, const glm::vec3& up = glm::vec3{0.0F, 1.0F, 0.0F});

private:
    struct ActiveSound;

    [[nodiscard]] std::string ResolveClipPath(const std::string& clipName) const;
    [[nodiscard]] static bool IsAbsolutePathLike(const std::string& value);
    [[nodiscard]] void* GroupForBus(Bus bus) const;

    std::string m_assetRoot = "assets/audio";
    bool m_initialized = false;
    SoundHandle m_nextHandle = 1;

    struct EngineData;
    EngineData* m_engine = nullptr;
    std::unordered_map<SoundHandle, ActiveSound*> m_active;

    std::array<float, 5> m_busVolume{
        1.0F, // Master
        1.0F, // Music
        1.0F, // Sfx
        1.0F, // Ui
        1.0F, // Ambience
    };
};
} // namespace engine::audio
