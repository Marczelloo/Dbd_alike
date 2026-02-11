#include "engine/fx/FxSystem.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>

#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <glm/trigonometric.hpp>
#include <nlohmann/json.hpp>

#include "engine/render/Renderer.hpp"

namespace engine::fx
{
namespace
{
using json = nlohmann::json;

float SafeFiniteFloat(float value, float fallback)
{
    return std::isfinite(value) ? value : fallback;
}

std::string EmitterTypeToText(FxEmitterType type)
{
    return type == FxEmitterType::Trail ? "trail" : "sprite";
}

FxEmitterType EmitterTypeFromText(const std::string& text)
{
    return text == "trail" ? FxEmitterType::Trail : FxEmitterType::Sprite;
}

std::string BlendModeToText(FxBlendMode mode)
{
    return mode == FxBlendMode::Additive ? "additive" : "alpha";
}

FxBlendMode BlendModeFromText(const std::string& text)
{
    return text == "alpha" ? FxBlendMode::Alpha : FxBlendMode::Additive;
}

std::string NetModeToText(FxNetMode mode)
{
    switch (mode)
    {
        case FxNetMode::ServerBroadcast: return "server_broadcast";
        case FxNetMode::OwnerOnly: return "owner_only";
        case FxNetMode::Local:
        default: return "local";
    }
}

FxNetMode NetModeFromText(const std::string& text)
{
    if (text == "server_broadcast")
    {
        return FxNetMode::ServerBroadcast;
    }
    if (text == "owner_only")
    {
        return FxNetMode::OwnerOnly;
    }
    return FxNetMode::Local;
}

glm::vec3 JsonToVec3(const json& value, const glm::vec3& fallback)
{
    if (!value.is_array() || value.size() < 3)
    {
        return fallback;
    }
    return glm::vec3{value[0].get<float>(), value[1].get<float>(), value[2].get<float>()};
}

glm::vec4 JsonToVec4(const json& value, const glm::vec4& fallback)
{
    if (!value.is_array() || value.size() < 4)
    {
        return fallback;
    }
    return glm::vec4{value[0].get<float>(), value[1].get<float>(), value[2].get<float>(), value[3].get<float>()};
}

json Vec3ToJson(const glm::vec3& value)
{
    return json::array({value.x, value.y, value.z});
}

json Vec4ToJson(const glm::vec4& value)
{
    return json::array({value.x, value.y, value.z, value.w});
}

json CurveToJson(const FloatCurve& curve)
{
    json out = json::array();
    for (const FloatCurveKey& key : curve.keys)
    {
        out.push_back(json{{"t", key.t}, {"v", key.value}});
    }
    return out;
}

FloatCurve CurveFromJson(const json& value, const FloatCurve& fallback)
{
    if (!value.is_array())
    {
        return fallback;
    }
    FloatCurve curve;
    curve.keys.clear();
    for (const json& key : value)
    {
        if (!key.is_object())
        {
            continue;
        }
        curve.keys.push_back(FloatCurveKey{key.value("t", 0.0F), key.value("v", 1.0F)});
    }
    if (curve.keys.empty())
    {
        return fallback;
    }
    std::sort(curve.keys.begin(), curve.keys.end(), [](const FloatCurveKey& a, const FloatCurveKey& b) { return a.t < b.t; });
    return curve;
}

json GradientToJson(const ColorGradient& gradient)
{
    json out = json::array();
    for (const ColorGradientKey& key : gradient.keys)
    {
        out.push_back(json{{"t", key.t}, {"color", Vec4ToJson(key.color)}});
    }
    return out;
}

ColorGradient GradientFromJson(const json& value, const ColorGradient& fallback)
{
    if (!value.is_array())
    {
        return fallback;
    }
    ColorGradient gradient;
    gradient.keys.clear();
    for (const json& key : value)
    {
        if (!key.is_object())
        {
            continue;
        }
        gradient.keys.push_back(ColorGradientKey{
            key.value("t", 0.0F),
            JsonToVec4(key.value("color", json::array()), glm::vec4{1.0F}),
        });
    }
    if (gradient.keys.empty())
    {
        return fallback;
    }
    std::sort(gradient.keys.begin(), gradient.keys.end(), [](const ColorGradientKey& a, const ColorGradientKey& b) { return a.t < b.t; });
    return gradient;
}
} // namespace

float FxParameterSet::GetFloat(const std::string& key, float fallback) const
{
    const auto it = values.find(key);
    if (it == values.end())
    {
        return fallback;
    }
    if (const auto value = std::get_if<float>(&it->second); value != nullptr)
    {
        return *value;
    }
    if (const auto value = std::get_if<int>(&it->second); value != nullptr)
    {
        return static_cast<float>(*value);
    }
    return fallback;
}

int FxParameterSet::GetInt(const std::string& key, int fallback) const
{
    const auto it = values.find(key);
    if (it == values.end())
    {
        return fallback;
    }
    if (const auto value = std::get_if<int>(&it->second); value != nullptr)
    {
        return *value;
    }
    if (const auto value = std::get_if<float>(&it->second); value != nullptr)
    {
        return static_cast<int>(*value);
    }
    return fallback;
}

bool FxParameterSet::GetBool(const std::string& key, bool fallback) const
{
    const auto it = values.find(key);
    if (it == values.end())
    {
        return fallback;
    }
    if (const auto value = std::get_if<bool>(&it->second); value != nullptr)
    {
        return *value;
    }
    return fallback;
}

glm::vec3 FxParameterSet::GetVec3(const std::string& key, const glm::vec3& fallback) const
{
    const auto it = values.find(key);
    if (it == values.end())
    {
        return fallback;
    }
    if (const auto value = std::get_if<glm::vec3>(&it->second); value != nullptr)
    {
        return *value;
    }
    return fallback;
}

glm::vec4 FxParameterSet::GetVec4(const std::string& key, const glm::vec4& fallback) const
{
    const auto it = values.find(key);
    if (it == values.end())
    {
        return fallback;
    }
    if (const auto value = std::get_if<glm::vec4>(&it->second); value != nullptr)
    {
        return *value;
    }
    return fallback;
}

std::string FxParameterSet::GetString(const std::string& key, const std::string& fallback) const
{
    const auto it = values.find(key);
    if (it == values.end())
    {
        return fallback;
    }
    if (const auto value = std::get_if<std::string>(&it->second); value != nullptr)
    {
        return *value;
    }
    return fallback;
}

float FloatCurve::Evaluate(float t, float fallback) const
{
    if (keys.empty())
    {
        return fallback;
    }
    if (keys.size() == 1)
    {
        return keys.front().value;
    }

    t = glm::clamp(t, 0.0F, 1.0F);
    if (t <= keys.front().t)
    {
        return keys.front().value;
    }
    if (t >= keys.back().t)
    {
        return keys.back().value;
    }

    for (std::size_t i = 1; i < keys.size(); ++i)
    {
        if (t <= keys[i].t)
        {
            const FloatCurveKey& a = keys[i - 1];
            const FloatCurveKey& b = keys[i];
            const float span = std::max(1.0e-5F, b.t - a.t);
            const float alpha = glm::clamp((t - a.t) / span, 0.0F, 1.0F);
            return glm::mix(a.value, b.value, alpha);
        }
    }
    return keys.back().value;
}

glm::vec4 ColorGradient::Evaluate(float t, const glm::vec4& fallback) const
{
    if (keys.empty())
    {
        return fallback;
    }
    if (keys.size() == 1)
    {
        return keys.front().color;
    }

    t = glm::clamp(t, 0.0F, 1.0F);
    if (t <= keys.front().t)
    {
        return keys.front().color;
    }
    if (t >= keys.back().t)
    {
        return keys.back().color;
    }

    for (std::size_t i = 1; i < keys.size(); ++i)
    {
        if (t <= keys[i].t)
        {
            const ColorGradientKey& a = keys[i - 1];
            const ColorGradientKey& b = keys[i];
            const float span = std::max(1.0e-5F, b.t - a.t);
            const float alpha = glm::clamp((t - a.t) / span, 0.0F, 1.0F);
            return glm::mix(a.color, b.color, alpha);
        }
    }
    return keys.back().color;
}

bool FxSystem::Initialize(const std::string& assetDirectory)
{
    m_assetDirectory = assetDirectory;
    std::filesystem::create_directories(m_assetDirectory);
    m_instances.assign(static_cast<std::size_t>(m_maxInstancesBudget), FxInstance{});
    EnsureDefaultAssets();
    return ReloadAssets();
}

bool FxSystem::ReloadAssets()
{
    m_assets.clear();
    std::filesystem::create_directories(m_assetDirectory);

    for (const auto& entry : std::filesystem::directory_iterator(m_assetDirectory))
    {
        if (!entry.is_regular_file() || entry.path().extension() != ".json")
        {
            continue;
        }

        FxAsset asset;
        if (!LoadAssetFromFile(entry.path().string(), &asset, nullptr))
        {
            continue;
        }
        if (asset.id.empty())
        {
            asset.id = entry.path().stem().string();
        }
        m_assets[asset.id] = std::move(asset);
    }

    return !m_assets.empty();
}

std::vector<std::string> FxSystem::ListAssetIds() const
{
    std::vector<std::string> ids;
    ids.reserve(m_assets.size());
    for (const auto& [id, _] : m_assets)
    {
        (void)_;
        ids.push_back(id);
    }
    std::sort(ids.begin(), ids.end());
    return ids;
}

std::optional<FxAsset> FxSystem::GetAsset(const std::string& id) const
{
    const auto it = m_assets.find(id);
    if (it == m_assets.end())
    {
        return std::nullopt;
    }
    return it->second;
}

bool FxSystem::SaveAsset(const FxAsset& asset, std::string* outError)
{
    if (asset.id.empty())
    {
        if (outError != nullptr)
        {
            *outError = "FX asset id is empty.";
        }
        return false;
    }

    const std::filesystem::path path = std::filesystem::path(m_assetDirectory) / (asset.id + ".json");
    if (!SaveAssetToFile(path.string(), asset, outError))
    {
        return false;
    }
    m_assets[asset.id] = asset;
    return true;
}

float FxSystem::RandomRange(float minValue, float maxValue)
{
    if (maxValue < minValue)
    {
        std::swap(maxValue, minValue);
    }
    const float u = static_cast<float>(m_rng()) / static_cast<float>(m_rng.max());
    return glm::mix(minValue, maxValue, u);
}

glm::vec3 FxSystem::RandomVec3Signed(const glm::vec3& extents)
{
    return glm::vec3{
        RandomRange(-extents.x, extents.x),
        RandomRange(-extents.y, extents.y),
        RandomRange(-extents.z, extents.z),
    };
}

FxEmitterAsset FxSystem::BuildEmitterWithParams(const FxEmitterAsset& source, const FxParameterSet& params) const
{
    FxEmitterAsset out = source;
    if (!out.rateParam.empty())
    {
        out.spawnRate = params.GetFloat(out.rateParam, out.spawnRate);
    }
    if (!out.sizeParam.empty())
    {
        const float sizeMult = params.GetFloat(out.sizeParam, 1.0F);
        out.sizeRange *= sizeMult;
    }
    if (!out.colorParam.empty())
    {
        const glm::vec4 color = params.GetVec4(out.colorParam, glm::vec4{0.0F});
        if (glm::length(glm::vec4{color}) > 0.0F && !out.colorOverLife.keys.empty())
        {
            out.colorOverLife.keys.front().color = color;
        }
    }
    out.spawnRate = SafeFiniteFloat(out.spawnRate, source.spawnRate);
    out.duration = SafeFiniteFloat(out.duration, source.duration);
    out.gravity = SafeFiniteFloat(out.gravity, source.gravity);
    out.maxDistance = SafeFiniteFloat(out.maxDistance, source.maxDistance);
    out.lodNearDistance = SafeFiniteFloat(out.lodNearDistance, source.lodNearDistance);
    out.lodFarDistance = SafeFiniteFloat(out.lodFarDistance, source.lodFarDistance);
    out.lifetimeRange.x = SafeFiniteFloat(out.lifetimeRange.x, source.lifetimeRange.x);
    out.lifetimeRange.y = SafeFiniteFloat(out.lifetimeRange.y, source.lifetimeRange.y);
    out.speedRange.x = SafeFiniteFloat(out.speedRange.x, source.speedRange.x);
    out.speedRange.y = SafeFiniteFloat(out.speedRange.y, source.speedRange.y);
    out.sizeRange.x = SafeFiniteFloat(out.sizeRange.x, source.sizeRange.x);
    out.sizeRange.y = SafeFiniteFloat(out.sizeRange.y, source.sizeRange.y);
    out.maxParticles = std::clamp(out.maxParticles, 1, std::max(1, m_maxParticlesBudget));
    out.burstCount = std::clamp(out.burstCount, 0, out.maxParticles);
    out.spawnRate = glm::clamp(out.spawnRate, 0.0F, 5000.0F);
    out.duration = glm::clamp(out.duration, 0.01F, 60.0F);
    out.maxDistance = glm::clamp(out.maxDistance, 0.1F, 5000.0F);
    out.lodNearDistance = glm::clamp(out.lodNearDistance, 0.0F, out.maxDistance);
    out.lodFarDistance = glm::clamp(out.lodFarDistance, out.lodNearDistance + 0.01F, out.maxDistance);
    out.lifetimeRange.x = glm::clamp(out.lifetimeRange.x, 0.01F, 60.0F);
    out.lifetimeRange.y = glm::clamp(out.lifetimeRange.y, out.lifetimeRange.x, 60.0F);
    out.sizeRange.x = glm::clamp(out.sizeRange.x, 0.001F, 100.0F);
    out.sizeRange.y = glm::clamp(out.sizeRange.y, out.sizeRange.x, 100.0F);
    out.trailPointStep = glm::clamp(SafeFiniteFloat(out.trailPointStep, source.trailPointStep), 0.001F, 1.0F);
    out.trailPointLifetime = glm::clamp(SafeFiniteFloat(out.trailPointLifetime, source.trailPointLifetime), 0.01F, 60.0F);
    return out;
}

FxSystem::FxInstanceId FxSystem::Spawn(
    const std::string& assetId,
    const glm::vec3& position,
    const glm::vec3& forward,
    const FxParameterSet& parameters,
    std::optional<FxNetMode> netModeOverride
)
{
    const auto assetIt = m_assets.find(assetId);
    if (assetIt == m_assets.end())
    {
        return 0;
    }
    const FxAsset& asset = assetIt->second;

    FxInstance* slot = nullptr;
    for (FxInstance& instance : m_instances)
    {
        if (!instance.active)
        {
            slot = &instance;
            break;
        }
    }
    if (slot == nullptr && !m_instances.empty())
    {
        slot = &m_instances.front();
    }
    if (slot == nullptr)
    {
        return 0;
    }

    slot->active = true;
    slot->id = m_nextInstanceId++;
    slot->asset = &asset;
    slot->parameters = parameters;
    slot->position = position;
    slot->forward = glm::length(forward) > 1.0e-5F ? glm::normalize(forward) : glm::vec3{0.0F, 0.0F, -1.0F};
    slot->age = 0.0F;
    slot->netMode = netModeOverride.value_or(asset.netMode);
    slot->emitters.clear();
    slot->emitters.reserve(asset.emitters.size());

    for (const FxEmitterAsset& emitter : asset.emitters)
    {
        EmitterRuntime runtime;
        runtime.emitter = &emitter;
        runtime.trailHead = position;
        runtime.trailVelocity = slot->forward * RandomRange(emitter.speedRange.x, emitter.speedRange.y);
        runtime.lastInstancePosition = position;
        const int reserveParticles = std::clamp(emitter.maxParticles, 8, std::max(8, m_maxParticlesBudget));
        const int reserveTrailPoints = std::clamp(reserveParticles / 2, 8, std::max(8, m_maxParticlesBudget));
        try
        {
            runtime.particles.reserve(static_cast<std::size_t>(reserveParticles));
            runtime.trailPoints.reserve(static_cast<std::size_t>(reserveTrailPoints));
            slot->emitters.push_back(std::move(runtime));
        }
        catch (const std::bad_alloc&)
        {
            slot->active = false;
            slot->emitters.clear();
            return 0;
        }
    }

    if (m_spawnCallback && slot->netMode != FxNetMode::Local)
    {
        m_spawnCallback(FxSpawnEvent{
            .assetId = asset.id,
            .position = slot->position,
            .forward = slot->forward,
            .netMode = slot->netMode,
        });
    }

    return slot->id;
}

void FxSystem::Stop(FxInstanceId instanceId)
{
    for (FxInstance& instance : m_instances)
    {
        if (instance.active && instance.id == instanceId)
        {
            instance.active = false;
            instance.emitters.clear();
            return;
        }
    }
}

void FxSystem::StopAll()
{
    for (FxInstance& instance : m_instances)
    {
        instance.active = false;
        instance.emitters.clear();
    }
}

void FxSystem::SetInstanceTransform(FxInstanceId instanceId, const glm::vec3& position, const glm::vec3& forward)
{
    for (FxInstance& instance : m_instances)
    {
        if (!instance.active || instance.id != instanceId)
        {
            continue;
        }

        instance.position = position;
        if (glm::length(forward) > 1.0e-5F)
        {
            instance.forward = glm::normalize(forward);
        }
        return;
    }
}

void FxSystem::SetGlobalBudgets(int maxInstances, int maxParticles)
{
    m_maxInstancesBudget = std::max(8, maxInstances);
    m_maxParticlesBudget = std::max(256, maxParticles);
    if (static_cast<int>(m_instances.size()) < m_maxInstancesBudget)
    {
        try
        {
            m_instances.resize(static_cast<std::size_t>(m_maxInstancesBudget));
        }
        catch (const std::bad_alloc&)
        {
            m_maxInstancesBudget = static_cast<int>(m_instances.size());
        }
    }
}

void FxSystem::SpawnParticle(FxInstance& instance, EmitterRuntime& emitterRuntime)
{
    if (emitterRuntime.emitter == nullptr || m_stats.activeParticles + m_stats.spawnedThisFrame >= m_maxParticlesBudget)
    {
        return;
    }

    const FxEmitterAsset& emitter = *emitterRuntime.emitter;
    const int maxParticles = std::clamp(emitter.maxParticles, 1, std::max(1, m_maxParticlesBudget));
    if (static_cast<int>(emitterRuntime.particles.size()) >= maxParticles)
    {
        return;
    }

    Particle particle;
    particle.position = instance.position;
    particle.velocity =
        emitter.velocityBase +
        instance.forward * RandomRange(emitter.speedRange.x, emitter.speedRange.y) +
        RandomVec3Signed(emitter.velocityRandom);
    particle.lifetime = RandomRange(emitter.lifetimeRange.x, emitter.lifetimeRange.y);
    particle.startSize = RandomRange(emitter.sizeRange.x, emitter.sizeRange.y);
    particle.tint = emitter.colorOverLife.keys.empty() ? glm::vec4{1.0F} : emitter.colorOverLife.keys.front().color;

    emitterRuntime.particles.push_back(particle);
    m_stats.spawnedThisFrame += 1;
}

void FxSystem::UpdateTrail(FxInstance& instance, EmitterRuntime& emitterRuntime, float dt)
{
    if (emitterRuntime.emitter == nullptr)
    {
        return;
    }
    const FxEmitterAsset& emitter = *emitterRuntime.emitter;
    emitterRuntime.trailVelocity.y += emitter.gravity * dt;
    emitterRuntime.trailHead += emitterRuntime.trailVelocity * dt;
    if (emitter.localSpace)
    {
        emitterRuntime.trailHead = instance.position + emitterRuntime.trailVelocity * (emitterRuntime.age * 0.25F);
    }

    emitterRuntime.trailPointAccumulator += dt;
    while (emitterRuntime.trailPointAccumulator >= emitter.trailPointStep)
    {
        emitterRuntime.trailPointAccumulator -= emitter.trailPointStep;
        emitterRuntime.trailPoints.push_back(TrailPoint{
            .position = emitterRuntime.trailHead,
            .age = 0.0F,
            .lifetime = emitter.trailPointLifetime,
            .tint = emitter.colorOverLife.keys.empty() ? glm::vec4{1.0F} : emitter.colorOverLife.keys.front().color,
        });
    }

    for (std::size_t i = 0; i < emitterRuntime.trailPoints.size();)
    {
        TrailPoint& point = emitterRuntime.trailPoints[i];
        point.age += dt;
        if (point.age >= point.lifetime)
        {
            emitterRuntime.trailPoints[i] = emitterRuntime.trailPoints.back();
            emitterRuntime.trailPoints.pop_back();
            continue;
        }
        ++i;
    }
    std::sort(emitterRuntime.trailPoints.begin(), emitterRuntime.trailPoints.end(), [](const TrailPoint& a, const TrailPoint& b) {
        return a.age < b.age;
    });
}

void FxSystem::UpdateEmitter(FxInstance& instance, EmitterRuntime& emitterRuntime, float dt, const glm::vec3& cameraPosition)
{
    if (emitterRuntime.emitter == nullptr)
    {
        return;
    }
    const FxEmitterAsset emitter = BuildEmitterWithParams(*emitterRuntime.emitter, instance.parameters);
    emitterRuntime.age += dt;

    if (emitter.localSpace)
    {
        const glm::vec3 delta = instance.position - emitterRuntime.lastInstancePosition;
        if (glm::length(delta) > 1.0e-6F)
        {
            for (Particle& particle : emitterRuntime.particles)
            {
                particle.position += delta;
            }
            for (TrailPoint& point : emitterRuntime.trailPoints)
            {
                point.position += delta;
            }
            emitterRuntime.trailHead += delta;
            emitterRuntime.lastInstancePosition = instance.position;
        }
    }
    else
    {
        emitterRuntime.lastInstancePosition = instance.position;
    }

    const float distance = glm::length(cameraPosition - instance.position);
    if (distance > emitter.maxDistance)
    {
        emitterRuntime.particles.clear();
        emitterRuntime.trailPoints.clear();
        return;
    }

    float lodFactor = 1.0F;
    if (distance > emitter.lodNearDistance)
    {
        const float span = std::max(0.01F, emitter.lodFarDistance - emitter.lodNearDistance);
        const float t = glm::clamp((distance - emitter.lodNearDistance) / span, 0.0F, 1.0F);
        lodFactor = glm::mix(1.0F, 0.2F, t);
    }

    const int maxParticles = std::clamp(emitter.maxParticles, 1, std::max(1, m_maxParticlesBudget));
    if (!emitterRuntime.burstDone && emitter.burstCount > 0)
    {
        const int count = std::clamp(
            static_cast<int>(std::round(static_cast<float>(emitter.burstCount) * lodFactor)),
            0,
            maxParticles
        );
        for (int i = 0; i < count; ++i)
        {
            SpawnParticle(instance, emitterRuntime);
        }
        emitterRuntime.burstDone = true;
    }

    const bool canEmit = emitter.looping || instance.asset->looping || emitterRuntime.age <= emitter.duration;
    if (canEmit && emitter.spawnRate > 0.0F)
    {
        emitterRuntime.spawnAccumulator += dt * emitter.spawnRate * lodFactor;
        int safetyCounter = 0;
        while (emitterRuntime.spawnAccumulator >= 1.0F)
        {
            SpawnParticle(instance, emitterRuntime);
            emitterRuntime.spawnAccumulator -= 1.0F;
            ++safetyCounter;
            if (safetyCounter >= maxParticles * 2)
            {
                emitterRuntime.spawnAccumulator = 0.0F;
                break;
            }
        }
    }

    for (std::size_t i = 0; i < emitterRuntime.particles.size();)
    {
        Particle& particle = emitterRuntime.particles[i];
        particle.age += dt;
        if (particle.age >= particle.lifetime)
        {
            emitterRuntime.particles[i] = emitterRuntime.particles.back();
            emitterRuntime.particles.pop_back();
            continue;
        }
        particle.velocity.y += emitter.gravity * dt;
        particle.position += particle.velocity * dt;
        ++i;
    }

    if (emitter.type == FxEmitterType::Trail)
    {
        UpdateTrail(instance, emitterRuntime, dt);
    }
}

bool FxSystem::InstanceFinished(const FxInstance& instance) const
{
    if (instance.asset == nullptr)
    {
        return true;
    }
    if (instance.asset->looping || instance.age < instance.asset->duration)
    {
        return false;
    }
    for (const EmitterRuntime& emitter : instance.emitters)
    {
        if (!emitter.particles.empty() || !emitter.trailPoints.empty())
        {
            return false;
        }
    }
    return true;
}

void FxSystem::Update(float deltaSeconds, const glm::vec3& cameraPosition)
{
    const auto start = std::chrono::high_resolution_clock::now();
    m_stats = FxStats{};
    m_cameraShakeOffset = glm::vec3{0.0F};
    m_postFxPulseColor = glm::vec3{0.0F};
    m_postFxPulseIntensity = 0.0F;

    for (FxInstance& instance : m_instances)
    {
        if (!instance.active || instance.asset == nullptr)
        {
            continue;
        }

        instance.age += deltaSeconds;
        for (EmitterRuntime& emitterRuntime : instance.emitters)
        {
            UpdateEmitter(instance, emitterRuntime, deltaSeconds, cameraPosition);
            m_stats.activeParticles += static_cast<int>(emitterRuntime.particles.size());
            m_stats.activeTrailPoints += static_cast<int>(emitterRuntime.trailPoints.size());
        }

        if (instance.asset->enableCameraShake && instance.asset->cameraShakeDuration > 0.0F)
        {
            const float t = glm::clamp(instance.age / std::max(0.01F, instance.asset->cameraShakeDuration), 0.0F, 1.0F);
            if (t < 1.0F)
            {
                const float envelope = 1.0F - t;
                const float phase = instance.age * instance.asset->cameraShakeFrequency;
                const float amp = envelope * instance.asset->cameraShakeAmplitude;
                m_cameraShakeOffset += glm::vec3{
                    std::sin(phase * 1.23F) * amp,
                    std::cos(phase * 1.71F) * amp,
                    std::sin(phase * 1.11F + 0.4F) * amp * 0.7F,
                };
            }
        }
        if (instance.asset->enablePostFxPulse && instance.asset->postFxDuration > 0.0F)
        {
            const float t = glm::clamp(instance.age / std::max(0.01F, instance.asset->postFxDuration), 0.0F, 1.0F);
            const float pulse = (1.0F - t) * instance.asset->postFxIntensity;
            if (pulse > m_postFxPulseIntensity)
            {
                m_postFxPulseIntensity = pulse;
                m_postFxPulseColor = instance.asset->postFxColor;
            }
        }

        if (InstanceFinished(instance))
        {
            instance.active = false;
            instance.emitters.clear();
            continue;
        }
        m_stats.activeInstances += 1;
    }

    const auto end = std::chrono::high_resolution_clock::now();
    const auto us = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
    m_stats.cpuMs = static_cast<float>(us) * 0.001F;
}

void FxSystem::Render(engine::render::Renderer& renderer, const glm::vec3& cameraPosition) const
{
    for (const FxInstance& instance : m_instances)
    {
        if (!instance.active || instance.asset == nullptr)
        {
            continue;
        }

        for (const EmitterRuntime& emitterRuntime : instance.emitters)
        {
            if (emitterRuntime.emitter == nullptr)
            {
                continue;
            }
            const FxEmitterAsset& emitter = *emitterRuntime.emitter;

            if (emitter.type == FxEmitterType::Sprite)
            {
                for (const Particle& particle : emitterRuntime.particles)
                {
                    const float lifeT = glm::clamp(particle.age / std::max(0.01F, particle.lifetime), 0.0F, 1.0F);
                    const float size = std::max(0.01F, particle.startSize * emitter.sizeOverLife.Evaluate(lifeT, 1.0F));
                    glm::vec4 color = emitter.colorOverLife.Evaluate(lifeT, particle.tint);
                    color *= emitter.alphaOverLife.Evaluate(lifeT, 1.0F);
                    if (emitter.blendMode == FxBlendMode::Additive)
                    {
                        color *= 1.35F;
                    }

                    glm::vec3 toCamera = cameraPosition - particle.position;
                    if (glm::length(toCamera) < 1.0e-5F)
                    {
                        toCamera = glm::vec3{0.0F, 0.0F, 1.0F};
                    }
                    toCamera = glm::normalize(toCamera);
                    const float yaw = std::atan2(toCamera.x, -toCamera.z);
                    const float pitch = std::asin(glm::clamp(toCamera.y, -1.0F, 1.0F));
                    renderer.DrawOrientedBox(
                        particle.position,
                        glm::vec3{size, size, 0.01F},
                        glm::vec3{glm::degrees(pitch), 180.0F - glm::degrees(yaw), 0.0F},
                        glm::vec3{color.r, color.g, color.b}
                    );
                }
            }
            else
            {
                for (std::size_t i = 1; i < emitterRuntime.trailPoints.size(); ++i)
                {
                    const float t = static_cast<float>(i) / static_cast<float>(emitterRuntime.trailPoints.size());
                    const glm::vec4 color = emitter.colorOverLife.Evaluate(t, glm::vec4{1.0F});
                    renderer.DrawOverlayLine(
                        emitterRuntime.trailPoints[i - 1].position,
                        emitterRuntime.trailPoints[i].position,
                        glm::vec3{color.r, color.g, color.b}
                    );
                }
            }
        }
    }
}

bool FxSystem::LoadAssetFromFile(const std::string& path, FxAsset* outAsset, std::string* outError)
{
    std::ifstream stream(path);
    if (!stream.is_open())
    {
        if (outError != nullptr)
        {
            *outError = "Unable to open FX asset: " + path;
        }
        return false;
    }

    json root;
    try
    {
        stream >> root;
    }
    catch (const std::exception& ex)
    {
        if (outError != nullptr)
        {
            *outError = std::string("Invalid FX JSON: ") + ex.what();
        }
        return false;
    }

    FxAsset asset;
    asset.assetVersion = root.value("asset_version", 1);
    asset.id = root.value("id", std::filesystem::path(path).stem().string());
    asset.netMode = NetModeFromText(root.value("net_mode", std::string("local")));
    asset.looping = root.value("looping", false);
    asset.duration = SafeFiniteFloat(root.value("duration", 1.0F), 1.0F);
    asset.maxInstances = root.value("max_instances", 24);
    asset.lodPriority = root.value("lod_priority", 0);
    asset.enableCameraShake = root.value("camera_shake_enabled", false);
    asset.cameraShakeAmplitude = SafeFiniteFloat(root.value("camera_shake_amplitude", 0.0F), 0.0F);
    asset.cameraShakeFrequency = SafeFiniteFloat(root.value("camera_shake_frequency", 18.0F), 18.0F);
    asset.cameraShakeDuration = SafeFiniteFloat(root.value("camera_shake_duration", 0.25F), 0.25F);
    asset.enablePostFxPulse = root.value("postfx_pulse_enabled", false);
    asset.postFxColor = JsonToVec3(root.value("postfx_color", json::array()), glm::vec3{1.0F, 0.22F, 0.15F});
    asset.postFxIntensity = SafeFiniteFloat(root.value("postfx_intensity", 0.0F), 0.0F);
    asset.postFxDuration = SafeFiniteFloat(root.value("postfx_duration", 0.2F), 0.2F);
    asset.maxInstances = std::clamp(asset.maxInstances, 1, 4096);
    asset.duration = glm::clamp(asset.duration, 0.01F, 120.0F);
    asset.cameraShakeAmplitude = glm::clamp(asset.cameraShakeAmplitude, 0.0F, 10.0F);
    asset.cameraShakeFrequency = glm::clamp(asset.cameraShakeFrequency, 0.1F, 120.0F);
    asset.cameraShakeDuration = glm::clamp(asset.cameraShakeDuration, 0.01F, 30.0F);
    asset.postFxIntensity = glm::clamp(asset.postFxIntensity, 0.0F, 10.0F);
    asset.postFxDuration = glm::clamp(asset.postFxDuration, 0.01F, 30.0F);

    const json emitterArray = root.value("emitters", json::array());
    if (emitterArray.is_array())
    {
        for (const json& emitterJson : emitterArray)
        {
            if (!emitterJson.is_object())
            {
                continue;
            }

            FxEmitterAsset emitter;
            emitter.name = emitterJson.value("name", std::string("emitter"));
            emitter.type = EmitterTypeFromText(emitterJson.value("type", std::string("sprite")));
            emitter.blendMode = BlendModeFromText(emitterJson.value("blend_mode", std::string("additive")));
            emitter.depthTest = emitterJson.value("depth_test", true);
            emitter.looping = emitterJson.value("looping", false);
            emitter.localSpace = emitterJson.value("local_space", false);
            emitter.duration = SafeFiniteFloat(emitterJson.value("duration", 0.8F), 0.8F);
            emitter.spawnRate = SafeFiniteFloat(emitterJson.value("spawn_rate", 0.0F), 0.0F);
            emitter.burstCount = emitterJson.value("burst_count", 0);
            emitter.maxParticles = emitterJson.value("max_particles", 256);
            emitter.maxDistance = SafeFiniteFloat(emitterJson.value("max_distance", 120.0F), 120.0F);
            emitter.lodNearDistance = SafeFiniteFloat(emitterJson.value("lod_near_distance", 24.0F), 24.0F);
            emitter.lodFarDistance = SafeFiniteFloat(emitterJson.value("lod_far_distance", 68.0F), 68.0F);
            emitter.lifetimeRange = glm::vec2{
                SafeFiniteFloat(emitterJson.value("lifetime_min", 0.25F), 0.25F),
                SafeFiniteFloat(emitterJson.value("lifetime_max", 0.45F), 0.45F)
            };
            emitter.speedRange = glm::vec2{
                SafeFiniteFloat(emitterJson.value("speed_min", 1.2F), 1.2F),
                SafeFiniteFloat(emitterJson.value("speed_max", 3.0F), 3.0F)
            };
            emitter.sizeRange = glm::vec2{
                SafeFiniteFloat(emitterJson.value("size_min", 0.08F), 0.08F),
                SafeFiniteFloat(emitterJson.value("size_max", 0.24F), 0.24F)
            };
            emitter.velocityBase = JsonToVec3(emitterJson.value("velocity_base", json::array()), glm::vec3{0.0F, 1.5F, 0.0F});
            emitter.velocityRandom = JsonToVec3(emitterJson.value("velocity_random", json::array()), glm::vec3{0.6F, 0.9F, 0.6F});
            emitter.gravity = SafeFiniteFloat(emitterJson.value("gravity", -2.2F), -2.2F);
            emitter.trailWidth = SafeFiniteFloat(emitterJson.value("trail_width", 0.16F), 0.16F);
            emitter.trailPointStep = SafeFiniteFloat(emitterJson.value("trail_point_step", 0.04F), 0.04F);
            emitter.trailPointLifetime = SafeFiniteFloat(emitterJson.value("trail_point_lifetime", 0.45F), 0.45F);
            emitter.rateParam = emitterJson.value("rate_param", std::string{});
            emitter.colorParam = emitterJson.value("color_param", std::string{});
            emitter.sizeParam = emitterJson.value("size_param", std::string{});
            emitter.sizeOverLife = CurveFromJson(emitterJson.value("size_over_life", json::array()), emitter.sizeOverLife);
            emitter.alphaOverLife = CurveFromJson(emitterJson.value("alpha_over_life", json::array()), emitter.alphaOverLife);
            emitter.colorOverLife = GradientFromJson(emitterJson.value("color_over_life", json::array()), emitter.colorOverLife);
            emitter.maxParticles = std::clamp(emitter.maxParticles, 1, 20000);
            emitter.burstCount = std::clamp(emitter.burstCount, 0, emitter.maxParticles);
            emitter.spawnRate = glm::clamp(emitter.spawnRate, 0.0F, 5000.0F);
            emitter.duration = glm::clamp(emitter.duration, 0.01F, 60.0F);
            emitter.maxDistance = glm::clamp(emitter.maxDistance, 0.1F, 5000.0F);
            emitter.lodNearDistance = glm::clamp(emitter.lodNearDistance, 0.0F, emitter.maxDistance);
            emitter.lodFarDistance = glm::clamp(emitter.lodFarDistance, emitter.lodNearDistance + 0.01F, emitter.maxDistance);
            emitter.lifetimeRange.x = glm::clamp(emitter.lifetimeRange.x, 0.01F, 60.0F);
            emitter.lifetimeRange.y = glm::clamp(emitter.lifetimeRange.y, emitter.lifetimeRange.x, 60.0F);
            emitter.sizeRange.x = glm::clamp(emitter.sizeRange.x, 0.001F, 100.0F);
            emitter.sizeRange.y = glm::clamp(emitter.sizeRange.y, emitter.sizeRange.x, 100.0F);
            emitter.trailWidth = glm::clamp(emitter.trailWidth, 0.001F, 50.0F);
            emitter.trailPointStep = glm::clamp(emitter.trailPointStep, 0.001F, 1.0F);
            emitter.trailPointLifetime = glm::clamp(emitter.trailPointLifetime, 0.01F, 60.0F);
            asset.emitters.push_back(std::move(emitter));
        }
    }

    if (asset.emitters.empty())
    {
        asset.emitters.push_back(FxEmitterAsset{});
    }

    *outAsset = std::move(asset);
    return true;
}

bool FxSystem::SaveAssetToFile(const std::string& path, const FxAsset& asset, std::string* outError)
{
    json root;
    root["asset_version"] = asset.assetVersion;
    root["id"] = asset.id;
    root["net_mode"] = NetModeToText(asset.netMode);
    root["looping"] = asset.looping;
    root["duration"] = asset.duration;
    root["max_instances"] = asset.maxInstances;
    root["lod_priority"] = asset.lodPriority;
    root["camera_shake_enabled"] = asset.enableCameraShake;
    root["camera_shake_amplitude"] = asset.cameraShakeAmplitude;
    root["camera_shake_frequency"] = asset.cameraShakeFrequency;
    root["camera_shake_duration"] = asset.cameraShakeDuration;
    root["postfx_pulse_enabled"] = asset.enablePostFxPulse;
    root["postfx_color"] = Vec3ToJson(asset.postFxColor);
    root["postfx_intensity"] = asset.postFxIntensity;
    root["postfx_duration"] = asset.postFxDuration;

    json emitters = json::array();
    for (const FxEmitterAsset& emitter : asset.emitters)
    {
        json out;
        out["name"] = emitter.name;
        out["type"] = EmitterTypeToText(emitter.type);
        out["blend_mode"] = BlendModeToText(emitter.blendMode);
        out["depth_test"] = emitter.depthTest;
        out["looping"] = emitter.looping;
        out["local_space"] = emitter.localSpace;
        out["duration"] = emitter.duration;
        out["spawn_rate"] = emitter.spawnRate;
        out["burst_count"] = emitter.burstCount;
        out["max_particles"] = emitter.maxParticles;
        out["max_distance"] = emitter.maxDistance;
        out["lod_near_distance"] = emitter.lodNearDistance;
        out["lod_far_distance"] = emitter.lodFarDistance;
        out["lifetime_min"] = emitter.lifetimeRange.x;
        out["lifetime_max"] = emitter.lifetimeRange.y;
        out["speed_min"] = emitter.speedRange.x;
        out["speed_max"] = emitter.speedRange.y;
        out["size_min"] = emitter.sizeRange.x;
        out["size_max"] = emitter.sizeRange.y;
        out["velocity_base"] = Vec3ToJson(emitter.velocityBase);
        out["velocity_random"] = Vec3ToJson(emitter.velocityRandom);
        out["gravity"] = emitter.gravity;
        out["trail_width"] = emitter.trailWidth;
        out["trail_point_step"] = emitter.trailPointStep;
        out["trail_point_lifetime"] = emitter.trailPointLifetime;
        out["rate_param"] = emitter.rateParam;
        out["color_param"] = emitter.colorParam;
        out["size_param"] = emitter.sizeParam;
        out["size_over_life"] = CurveToJson(emitter.sizeOverLife);
        out["alpha_over_life"] = CurveToJson(emitter.alphaOverLife);
        out["color_over_life"] = GradientToJson(emitter.colorOverLife);
        emitters.push_back(std::move(out));
    }
    root["emitters"] = std::move(emitters);

    std::ofstream stream(path);
    if (!stream.is_open())
    {
        if (outError != nullptr)
        {
            *outError = "Unable to write FX asset: " + path;
        }
        return false;
    }

    stream << root.dump(2) << "\n";
    return true;
}

void FxSystem::EnsureDefaultAssets()
{
    auto saveIfMissing = [this](const FxAsset& asset) {
        const std::filesystem::path path = std::filesystem::path(m_assetDirectory) / (asset.id + ".json");
        if (!std::filesystem::exists(path))
        {
            (void)SaveAssetToFile(path.string(), asset, nullptr);
        }
    };

    FxAsset hit;
    hit.id = "hit_spark";
    hit.netMode = FxNetMode::ServerBroadcast;
    hit.duration = 0.4F;
    hit.enableCameraShake = true;
    hit.cameraShakeAmplitude = 0.05F;
    hit.cameraShakeFrequency = 30.0F;
    hit.cameraShakeDuration = 0.16F;
    hit.enablePostFxPulse = true;
    hit.postFxIntensity = 0.25F;
    hit.postFxDuration = 0.16F;
    hit.emitters.push_back(FxEmitterAsset{});
    hit.emitters.back().name = "burst";
    hit.emitters.back().burstCount = 24;
    hit.emitters.back().lifetimeRange = glm::vec2{0.12F, 0.24F};
    hit.emitters.back().speedRange = glm::vec2{2.2F, 6.2F};
    hit.emitters.back().sizeRange = glm::vec2{0.03F, 0.09F};
    hit.emitters.back().velocityRandom = glm::vec3{1.4F, 1.1F, 1.4F};
    hit.emitters.back().gravity = -3.2F;
    saveIfMissing(hit);

    FxAsset blood;
    blood.id = "blood_spray";
    blood.netMode = FxNetMode::ServerBroadcast;
    blood.duration = 0.75F;
    blood.emitters.push_back(FxEmitterAsset{});
    blood.emitters.back().burstCount = 26;
    blood.emitters.back().blendMode = FxBlendMode::Alpha;
    blood.emitters.back().lifetimeRange = glm::vec2{0.35F, 0.7F};
    blood.emitters.back().speedRange = glm::vec2{0.8F, 3.7F};
    blood.emitters.back().sizeRange = glm::vec2{0.05F, 0.13F};
    blood.emitters.back().velocityBase = glm::vec3{0.0F, 0.8F, 0.0F};
    blood.emitters.back().velocityRandom = glm::vec3{0.9F, 1.2F, 0.9F};
    blood.emitters.back().colorOverLife.keys = {
        ColorGradientKey{0.0F, glm::vec4{0.85F, 0.12F, 0.1F, 1.0F}},
        ColorGradientKey{1.0F, glm::vec4{0.3F, 0.03F, 0.03F, 0.0F}},
    };
    saveIfMissing(blood);

    FxAsset dust;
    dust.id = "dust_puff";
    dust.netMode = FxNetMode::ServerBroadcast;
    dust.duration = 0.9F;
    dust.emitters.push_back(FxEmitterAsset{});
    dust.emitters.back().burstCount = 30;
    dust.emitters.back().blendMode = FxBlendMode::Alpha;
    dust.emitters.back().lifetimeRange = glm::vec2{0.6F, 1.0F};
    dust.emitters.back().speedRange = glm::vec2{0.3F, 1.3F};
    dust.emitters.back().sizeRange = glm::vec2{0.12F, 0.24F};
    dust.emitters.back().velocityBase = glm::vec3{0.0F, 0.2F, 0.0F};
    dust.emitters.back().velocityRandom = glm::vec3{1.0F, 0.3F, 1.0F};
    dust.emitters.back().gravity = -0.4F;
    dust.emitters.back().colorOverLife.keys = {
        ColorGradientKey{0.0F, glm::vec4{0.72F, 0.62F, 0.52F, 0.75F}},
        ColorGradientKey{1.0F, glm::vec4{0.45F, 0.39F, 0.34F, 0.0F}},
    };
    saveIfMissing(dust);

    FxAsset chase;
    chase.id = "chase_aura";
    chase.looping = true;
    chase.duration = 2.0F;
    chase.netMode = FxNetMode::Local;
    chase.emitters.push_back(FxEmitterAsset{});
    chase.emitters.back().type = FxEmitterType::Trail;
    chase.emitters.back().looping = true;
    chase.emitters.back().duration = 2.0F;
    chase.emitters.back().speedRange = glm::vec2{0.45F, 0.75F};
    chase.emitters.back().trailPointLifetime = 0.45F;
    chase.emitters.back().trailPointStep = 0.04F;
    chase.emitters.back().colorOverLife.keys = {
        ColorGradientKey{0.0F, glm::vec4{1.0F, 0.22F, 0.15F, 0.75F}},
        ColorGradientKey{1.0F, glm::vec4{0.95F, 0.05F, 0.02F, 0.0F}},
    };
    saveIfMissing(chase);

    FxAsset gen;
    gen.id = "generator_sparks_loop";
    gen.looping = true;
    gen.duration = 2.0F;
    gen.netMode = FxNetMode::Local;
    gen.emitters.push_back(FxEmitterAsset{});
    gen.emitters.back().looping = true;
    gen.emitters.back().spawnRate = 24.0F;
    gen.emitters.back().lifetimeRange = glm::vec2{0.15F, 0.3F};
    gen.emitters.back().speedRange = glm::vec2{0.9F, 2.2F};
    gen.emitters.back().sizeRange = glm::vec2{0.03F, 0.07F};
    gen.emitters.back().velocityBase = glm::vec3{0.0F, 1.1F, 0.0F};
    gen.emitters.back().velocityRandom = glm::vec3{0.75F, 0.75F, 0.75F};
    gen.emitters.back().gravity = -2.3F;
    saveIfMissing(gen);
}

} // namespace engine::fx
