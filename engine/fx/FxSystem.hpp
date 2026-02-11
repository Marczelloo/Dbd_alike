#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <random>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

namespace engine::render
{
class Renderer;
}

namespace engine::fx
{
enum class FxEmitterType
{
    Sprite,
    Trail
};

enum class FxBlendMode
{
    Alpha,
    Additive
};

enum class FxNetMode
{
    Local = 0,
    ServerBroadcast = 1,
    OwnerOnly = 2
};

using FxParameterValue = std::variant<std::monostate, float, int, bool, glm::vec3, glm::vec4, std::string>;

struct FxParameterSet
{
    std::unordered_map<std::string, FxParameterValue> values;

    [[nodiscard]] float GetFloat(const std::string& key, float fallback) const;
    [[nodiscard]] int GetInt(const std::string& key, int fallback) const;
    [[nodiscard]] bool GetBool(const std::string& key, bool fallback) const;
    [[nodiscard]] glm::vec3 GetVec3(const std::string& key, const glm::vec3& fallback) const;
    [[nodiscard]] glm::vec4 GetVec4(const std::string& key, const glm::vec4& fallback) const;
    [[nodiscard]] std::string GetString(const std::string& key, const std::string& fallback) const;
};

struct FloatCurveKey
{
    float t = 0.0F;
    float value = 1.0F;
};

struct FloatCurve
{
    std::vector<FloatCurveKey> keys;

    [[nodiscard]] float Evaluate(float t, float fallback) const;
};

struct ColorGradientKey
{
    float t = 0.0F;
    glm::vec4 color{1.0F};
};

struct ColorGradient
{
    std::vector<ColorGradientKey> keys;

    [[nodiscard]] glm::vec4 Evaluate(float t, const glm::vec4& fallback) const;
};

struct FxEmitterAsset
{
    std::string name = "emitter";
    FxEmitterType type = FxEmitterType::Sprite;
    FxBlendMode blendMode = FxBlendMode::Additive;
    bool depthTest = true;
    bool looping = false;
    bool localSpace = false;

    float duration = 0.8F;
    float spawnRate = 0.0F;
    int burstCount = 0;
    int maxParticles = 256;
    float maxDistance = 120.0F;
    float lodNearDistance = 24.0F;
    float lodFarDistance = 68.0F;

    glm::vec2 lifetimeRange{0.25F, 0.45F};
    glm::vec2 speedRange{1.2F, 3.0F};
    glm::vec2 sizeRange{0.08F, 0.24F};
    glm::vec3 velocityBase{0.0F, 1.5F, 0.0F};
    glm::vec3 velocityRandom{0.6F, 0.9F, 0.6F};
    float gravity = -2.2F;

    float trailWidth = 0.16F;
    float trailPointStep = 0.04F;
    float trailPointLifetime = 0.45F;

    std::string rateParam;
    std::string colorParam;
    std::string sizeParam;

    FloatCurve sizeOverLife{{FloatCurveKey{0.0F, 1.0F}, FloatCurveKey{1.0F, 0.0F}}};
    FloatCurve alphaOverLife{{FloatCurveKey{0.0F, 1.0F}, FloatCurveKey{1.0F, 0.0F}}};
    ColorGradient colorOverLife{
        {ColorGradientKey{0.0F, glm::vec4{1.0F, 0.95F, 0.7F, 1.0F}}, ColorGradientKey{1.0F, glm::vec4{1.0F, 0.2F, 0.1F, 0.0F}}}
    };
};

struct FxAsset
{
    int assetVersion = 1;
    std::string id = "new_fx";
    FxNetMode netMode = FxNetMode::Local;
    bool looping = false;
    float duration = 1.0F;
    int maxInstances = 24;
    int lodPriority = 0;

    bool enableCameraShake = false;
    float cameraShakeAmplitude = 0.0F;
    float cameraShakeFrequency = 18.0F;
    float cameraShakeDuration = 0.25F;

    bool enablePostFxPulse = false;
    glm::vec3 postFxColor{1.0F, 0.25F, 0.18F};
    float postFxIntensity = 0.0F;
    float postFxDuration = 0.22F;

    std::vector<FxEmitterAsset> emitters;
};

struct FxStats
{
    int activeInstances = 0;
    int activeParticles = 0;
    int activeTrailPoints = 0;
    int spawnedThisFrame = 0;
    float cpuMs = 0.0F;
};

struct FxSpawnEvent
{
    std::string assetId;
    glm::vec3 position{0.0F};
    glm::vec3 forward{0.0F, 0.0F, -1.0F};
    FxNetMode netMode = FxNetMode::Local;
};

class FxSystem
{
public:
    using FxInstanceId = std::uint64_t;
    using SpawnCallback = std::function<void(const FxSpawnEvent&)>;

    bool Initialize(const std::string& assetDirectory = "assets/fx");
    bool ReloadAssets();

    [[nodiscard]] std::vector<std::string> ListAssetIds() const;
    [[nodiscard]] std::optional<FxAsset> GetAsset(const std::string& id) const;
    bool SaveAsset(const FxAsset& asset, std::string* outError = nullptr);

    FxInstanceId Spawn(
        const std::string& assetId,
        const glm::vec3& position,
        const glm::vec3& forward,
        const FxParameterSet& parameters = {},
        std::optional<FxNetMode> netModeOverride = std::nullopt
    );
    void Stop(FxInstanceId instanceId);
    void StopAll();
    void SetInstanceTransform(FxInstanceId instanceId, const glm::vec3& position, const glm::vec3& forward);

    void Update(float deltaSeconds, const glm::vec3& cameraPosition);
    void Render(engine::render::Renderer& renderer, const glm::vec3& cameraPosition) const;

    [[nodiscard]] FxStats Stats() const { return m_stats; }
    [[nodiscard]] glm::vec3 CameraShakeOffset() const { return m_cameraShakeOffset; }
    [[nodiscard]] glm::vec3 PostFxPulseColor() const { return m_postFxPulseColor; }
    [[nodiscard]] float PostFxPulseIntensity() const { return m_postFxPulseIntensity; }

    void SetSpawnCallback(SpawnCallback callback) { m_spawnCallback = std::move(callback); }
    void SetGlobalBudgets(int maxInstances, int maxParticles);

private:
    struct Particle
    {
        glm::vec3 position{0.0F};
        glm::vec3 velocity{0.0F};
        float age = 0.0F;
        float lifetime = 0.5F;
        float startSize = 0.12F;
        glm::vec4 tint{1.0F};
    };

    struct TrailPoint
    {
        glm::vec3 position{0.0F};
        float age = 0.0F;
        float lifetime = 0.35F;
        glm::vec4 tint{1.0F};
    };

    struct EmitterRuntime
    {
        const FxEmitterAsset* emitter = nullptr;
        float age = 0.0F;
        float spawnAccumulator = 0.0F;
        bool burstDone = false;
        std::vector<Particle> particles;
        std::vector<TrailPoint> trailPoints;
        float trailPointAccumulator = 0.0F;
        glm::vec3 trailHead{0.0F};
        glm::vec3 trailVelocity{0.0F};
        glm::vec3 lastInstancePosition{0.0F};
    };

    struct FxInstance
    {
        bool active = false;
        FxInstanceId id = 0;
        const FxAsset* asset = nullptr;
        FxParameterSet parameters;
        glm::vec3 position{0.0F};
        glm::vec3 forward{0.0F, 0.0F, -1.0F};
        float age = 0.0F;
        FxNetMode netMode = FxNetMode::Local;
        std::vector<EmitterRuntime> emitters;
    };

    [[nodiscard]] FxEmitterAsset BuildEmitterWithParams(const FxEmitterAsset& source, const FxParameterSet& params) const;
    [[nodiscard]] float RandomRange(float minValue, float maxValue);
    [[nodiscard]] glm::vec3 RandomVec3Signed(const glm::vec3& extents);

    void EnsureDefaultAssets();
    bool LoadAssetFromFile(const std::string& path, FxAsset* outAsset, std::string* outError = nullptr);
    bool SaveAssetToFile(const std::string& path, const FxAsset& asset, std::string* outError = nullptr);

    void UpdateEmitter(FxInstance& instance, EmitterRuntime& emitterRuntime, float dt, const glm::vec3& cameraPosition);
    void SpawnParticle(FxInstance& instance, EmitterRuntime& emitterRuntime);
    void UpdateTrail(FxInstance& instance, EmitterRuntime& emitterRuntime, float dt);
    [[nodiscard]] bool InstanceFinished(const FxInstance& instance) const;

    std::string m_assetDirectory = "assets/fx";
    std::unordered_map<std::string, FxAsset> m_assets;
    std::vector<FxInstance> m_instances;
    FxInstanceId m_nextInstanceId = 1;
    FxStats m_stats{};
    int m_maxInstancesBudget = 256;
    int m_maxParticlesBudget = 32000;
    glm::vec3 m_cameraShakeOffset{0.0F};
    glm::vec3 m_postFxPulseColor{0.0F};
    float m_postFxPulseIntensity = 0.0F;
    mutable std::minstd_rand m_rng{1337};
    SpawnCallback m_spawnCallback;
};
} // namespace engine::fx
