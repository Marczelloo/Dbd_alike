#pragma once

#include <optional>
#include <string>
#include <vector>

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include "game/maps/TileGenerator.hpp"

namespace game::editor
{
constexpr int kEditorAssetVersion = 1;
constexpr float kEditorTileSize = 16.0F;

enum class LoopElementType
{
    Wall,
    Window,
    Pallet,
    Marker
};

enum class PropType
{
    Rock,
    Tree,
    Obstacle,
    Platform,
    MeshAsset
};

enum class ColliderType
{
    None,
    Box,
    Capsule
};

enum class MaterialShaderType
{
    Lit,
    Unlit
};

enum class LightType
{
    Point,
    Spot
};

struct LoopElement
{
    LoopElementType type = LoopElementType::Wall;
    std::string name = "element";
    glm::vec3 position{0.0F, 1.0F, 0.0F};
    glm::vec3 halfExtents{1.0F, 1.0F, 0.2F};
    float pitchDegrees = 0.0F;
    float yawDegrees = 0.0F;
    float rollDegrees = 0.0F;
    bool transformLocked = false;
    std::string markerTag;
};

struct LoopAsset
{
    int assetVersion = kEditorAssetVersion;
    std::string id = "new_loop";
    std::string displayName = "New Loop";
    std::string mesh;  // Optional mesh path (e.g., "assets/meshes/loop_elements/Wall.glb")
    glm::vec3 boundsMin{-8.0F, 0.0F, -8.0F};
    glm::vec3 boundsMax{8.0F, 2.0F, 8.0F};
    int footprintWidth = 1;
    int footprintHeight = 1;
    bool manualBounds = false;
    bool manualFootprint = false;
    std::vector<LoopElement> elements;
};

struct LoopPlacement
{
    std::string loopId;
    int tileX = 0;
    int tileY = 0;
    int rotationDegrees = 0;
    bool transformLocked = false;
};

struct PropInstance
{
    std::string name = "prop";
    PropType type = PropType::Rock;
    glm::vec3 position{0.0F, 0.8F, 0.0F};
    glm::vec3 halfExtents{0.8F, 0.8F, 0.8F};
    float pitchDegrees = 0.0F;
    float yawDegrees = 0.0F;
    float rollDegrees = 0.0F;
    bool transformLocked = false;
    bool solid = true;

    std::string meshAsset;
    std::string materialAsset;
    std::string prefabSourceId;
    std::string prefabInstanceId;
    std::string animationClip;
    bool animationLoop = true;
    bool animationAutoplay = false;
    float animationSpeed = 1.0F;

    ColliderType colliderType = ColliderType::Box;
    glm::vec3 colliderOffset{0.0F};
    glm::vec3 colliderHalfExtents{0.8F, 0.8F, 0.8F};
    float colliderRadius = 0.45F;
    float colliderHeight = 1.8F;
};

struct PrefabAsset
{
    int assetVersion = kEditorAssetVersion;
    std::string id = "new_prefab";
    std::string displayName = "New Prefab";
    std::vector<PropInstance> props;
};

struct MaterialAsset
{
    int assetVersion = kEditorAssetVersion;
    std::string id = "new_material";
    std::string displayName = "New Material";
    MaterialShaderType shaderType = MaterialShaderType::Lit;
    glm::vec4 baseColor{0.8F, 0.82F, 0.88F, 1.0F};
    std::string albedoTexture;
    std::string normalTexture;
    std::string ormTexture;
    float roughness = 0.55F;
    float metallic = 0.0F;
    float emissiveStrength = 0.0F;
};

struct AnimationKeyframe
{
    float time = 0.0F;
    glm::vec3 position{0.0F};
    glm::vec3 rotationEuler{0.0F};
    glm::vec3 scale{1.0F};
};

struct AnimationClipAsset
{
    int assetVersion = kEditorAssetVersion;
    std::string id = "new_clip";
    std::string displayName = "New Clip";
    bool loop = true;
    float speed = 1.0F;
    std::vector<AnimationKeyframe> keyframes;
};

struct EnvironmentAsset
{
    int assetVersion = kEditorAssetVersion;
    std::string id = "default_environment";
    std::string displayName = "Default Environment";

    glm::vec3 skyTopColor{0.44F, 0.58F, 0.78F};
    glm::vec3 skyBottomColor{0.11F, 0.14F, 0.18F};
    bool cloudsEnabled = true;
    float cloudCoverage = 0.25F;
    float cloudDensity = 0.45F;
    float cloudSpeed = 0.25F;

    glm::vec3 directionalLightDirection{0.45F, 1.0F, 0.3F};
    glm::vec3 directionalLightColor{1.0F, 0.97F, 0.9F};
    float directionalLightIntensity = 1.0F;

    bool fogEnabled = false;
    glm::vec3 fogColor{0.55F, 0.62F, 0.70F};
    float fogDensity = 0.012F;
    float fogStart = 20.0F;
    float fogEnd = 120.0F;

    int shadowQuality = 1;
    float shadowDistance = 80.0F;
    bool toneMapping = true;
    float exposure = 1.0F;
    bool bloom = false;
};

struct LightInstance
{
    std::string name = "light";
    LightType type = LightType::Point;
    glm::vec3 position{0.0F, 2.5F, 0.0F};
    glm::vec3 rotationEuler{0.0F, 0.0F, 0.0F};
    glm::vec3 color{1.0F, 0.95F, 0.85F};
    float intensity = 1.0F;
    float range = 12.0F;
    float spotInnerAngle = 22.0F;
    float spotOuterAngle = 38.0F;
    bool enabled = true;
};

struct MapAsset
{
    int assetVersion = kEditorAssetVersion;
    std::string name = "new_map";
    int width = 64;
    int height = 64;
    float tileSize = kEditorTileSize;
    glm::vec3 survivorSpawn{-12.0F, 1.05F, -12.0F};
    glm::vec3 killerSpawn{12.0F, 1.05F, 12.0F};
    std::string environmentAssetId = "default_environment";
    std::vector<LightInstance> lights;
    std::vector<LoopPlacement> placements;
    std::vector<PropInstance> props;
};

class LevelAssetIO
{
public:
    static void EnsureAssetDirectories();

    [[nodiscard]] static std::vector<std::string> ListLoopIds();
    [[nodiscard]] static std::vector<std::string> ListMapNames();
    [[nodiscard]] static std::vector<std::string> ListMaterialIds();
    [[nodiscard]] static std::vector<std::string> ListAnimationClipIds();
    [[nodiscard]] static std::vector<std::string> ListEnvironmentIds();
    [[nodiscard]] static std::vector<std::string> ListPrefabIds();

    [[nodiscard]] static bool SaveLoop(const LoopAsset& asset, std::string* outError = nullptr);
    [[nodiscard]] static bool LoadLoop(const std::string& loopId, LoopAsset* outAsset, std::string* outError = nullptr);
    [[nodiscard]] static bool DeleteLoop(const std::string& loopId, std::string* outError = nullptr);

    [[nodiscard]] static bool SaveMap(const MapAsset& asset, std::string* outError = nullptr);
    [[nodiscard]] static bool LoadMap(const std::string& mapName, MapAsset* outAsset, std::string* outError = nullptr);
    [[nodiscard]] static bool DeleteMap(const std::string& mapName, std::string* outError = nullptr);

    [[nodiscard]] static bool SaveMaterial(const MaterialAsset& asset, std::string* outError = nullptr);
    [[nodiscard]] static bool LoadMaterial(const std::string& materialId, MaterialAsset* outAsset, std::string* outError = nullptr);
    [[nodiscard]] static bool DeleteMaterial(const std::string& materialId, std::string* outError = nullptr);

    [[nodiscard]] static bool SaveAnimationClip(const AnimationClipAsset& asset, std::string* outError = nullptr);
    [[nodiscard]] static bool LoadAnimationClip(const std::string& clipId, AnimationClipAsset* outAsset, std::string* outError = nullptr);
    [[nodiscard]] static bool DeleteAnimationClip(const std::string& clipId, std::string* outError = nullptr);

    [[nodiscard]] static bool SaveEnvironment(const EnvironmentAsset& asset, std::string* outError = nullptr);
    [[nodiscard]] static bool LoadEnvironment(const std::string& environmentId, EnvironmentAsset* outAsset, std::string* outError = nullptr);
    [[nodiscard]] static bool DeleteEnvironment(const std::string& environmentId, std::string* outError = nullptr);

    [[nodiscard]] static bool SavePrefab(const PrefabAsset& asset, std::string* outError = nullptr);
    [[nodiscard]] static bool LoadPrefab(const std::string& prefabId, PrefabAsset* outAsset, std::string* outError = nullptr);
    [[nodiscard]] static bool DeletePrefab(const std::string& prefabId, std::string* outError = nullptr);

    [[nodiscard]] static std::vector<std::string> ValidateLoop(const LoopAsset& asset);

    [[nodiscard]] static bool BuildGeneratedMapFromAsset(const MapAsset& mapAsset, maps::GeneratedMap* outMap, std::string* outError = nullptr);
    [[nodiscard]] static bool BuildGeneratedMapFromMapName(const std::string& mapName, maps::GeneratedMap* outMap, std::string* outError = nullptr);

private:
    [[nodiscard]] static std::string SanitizeName(const std::string& value);
};
} // namespace game::editor
