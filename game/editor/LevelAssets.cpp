#include "game/editor/LevelAssets.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/trigonometric.hpp>

#include <nlohmann/json.hpp>

namespace game::editor
{
namespace
{
using json = nlohmann::json;

constexpr float kPi = 3.1415926535F;

const std::filesystem::path kAssetsRoot = "assets";
const std::filesystem::path kLoopDir = kAssetsRoot / "loops";
const std::filesystem::path kMapDir = kAssetsRoot / "maps";
const std::filesystem::path kMaterialDir = kAssetsRoot / "materials";
const std::filesystem::path kAnimationDir = kAssetsRoot / "animations";
const std::filesystem::path kEnvironmentDir = kAssetsRoot / "environments";
const std::filesystem::path kPrefabDir = kAssetsRoot / "prefabs";

std::string LoopTypeToString(LoopElementType type)
{
    switch (type)
    {
        case LoopElementType::Wall: return "wall";
        case LoopElementType::Window: return "window";
        case LoopElementType::Pallet: return "pallet";
        case LoopElementType::Marker: return "marker";
        default: return "wall";
    }
}

LoopElementType LoopTypeFromString(const std::string& value)
{
    if (value == "window")
    {
        return LoopElementType::Window;
    }
    if (value == "pallet")
    {
        return LoopElementType::Pallet;
    }
    if (value == "marker")
    {
        return LoopElementType::Marker;
    }
    return LoopElementType::Wall;
}

std::string PropTypeToString(PropType type)
{
    switch (type)
    {
        case PropType::Rock: return "rock";
        case PropType::Tree: return "tree";
        case PropType::Obstacle: return "obstacle";
        case PropType::Platform: return "platform";
        case PropType::MeshAsset: return "mesh_asset";
        default: return "rock";
    }
}

PropType PropTypeFromString(const std::string& value)
{
    if (value == "mesh_asset")
    {
        return PropType::MeshAsset;
    }
    if (value == "tree")
    {
        return PropType::Tree;
    }
    if (value == "obstacle")
    {
        return PropType::Obstacle;
    }
    if (value == "platform")
    {
        return PropType::Platform;
    }
    return PropType::Rock;
}

std::string ColliderTypeToString(ColliderType type)
{
    switch (type)
    {
        case ColliderType::None: return "none";
        case ColliderType::Capsule: return "capsule";
        case ColliderType::Box:
        default: return "box";
    }
}

ColliderType ColliderTypeFromString(const std::string& value)
{
    if (value == "none")
    {
        return ColliderType::None;
    }
    if (value == "capsule")
    {
        return ColliderType::Capsule;
    }
    return ColliderType::Box;
}

std::string MaterialShaderTypeToString(MaterialShaderType type)
{
    return type == MaterialShaderType::Unlit ? "unlit" : "lit";
}

MaterialShaderType MaterialShaderTypeFromString(const std::string& value)
{
    return value == "unlit" ? MaterialShaderType::Unlit : MaterialShaderType::Lit;
}

std::string LightTypeToString(LightType type)
{
    return type == LightType::Spot ? "spot" : "point";
}

LightType LightTypeFromString(const std::string& value)
{
    return value == "spot" ? LightType::Spot : LightType::Point;
}

json Vec3ToJson(const glm::vec3& value)
{
    return json::array({value.x, value.y, value.z});
}

json Vec4ToJson(const glm::vec4& value)
{
    return json::array({value.x, value.y, value.z, value.w});
}

glm::vec3 Vec3FromJson(const json& value, const glm::vec3& fallback)
{
    if (!value.is_array() || value.size() != 3)
    {
        return fallback;
    }
    return glm::vec3{
        value.at(0).get<float>(),
        value.at(1).get<float>(),
        value.at(2).get<float>(),
    };
}

glm::vec4 Vec4FromJson(const json& value, const glm::vec4& fallback)
{
    if (!value.is_array() || value.size() != 4)
    {
        return fallback;
    }
    return glm::vec4{
        value.at(0).get<float>(),
        value.at(1).get<float>(),
        value.at(2).get<float>(),
        value.at(3).get<float>(),
    };
}

bool WriteJsonFile(const std::filesystem::path& path, const json& value, std::string* outError)
{
    std::ofstream stream(path);
    if (!stream.is_open())
    {
        if (outError != nullptr)
        {
            *outError = "Unable to open file for writing: " + path.string();
        }
        return false;
    }

    stream << std::setw(2) << value;
    stream << "\n";
    return true;
}

bool ReadJsonFile(const std::filesystem::path& path, json* outValue, std::string* outError)
{
    std::ifstream stream(path);
    if (!stream.is_open())
    {
        if (outError != nullptr)
        {
            *outError = "Unable to open file: " + path.string();
        }
        return false;
    }

    try
    {
        stream >> *outValue;
    }
    catch (const std::exception& ex)
    {
        if (outError != nullptr)
        {
            *outError = "Invalid JSON in " + path.string() + ": " + ex.what();
        }
        return false;
    }
    return true;
}

std::filesystem::path LoopPathFromId(const std::string& loopId)
{
    return kLoopDir / (loopId + ".json");
}

std::filesystem::path MapPathFromName(const std::string& mapName)
{
    return kMapDir / (mapName + ".json");
}

std::filesystem::path MaterialPathFromId(const std::string& materialId)
{
    return kMaterialDir / (materialId + ".json");
}

std::filesystem::path AnimationPathFromId(const std::string& clipId)
{
    return kAnimationDir / (clipId + ".json");
}

std::filesystem::path EnvironmentPathFromId(const std::string& environmentId)
{
    return kEnvironmentDir / (environmentId + ".json");
}

std::filesystem::path PrefabPathFromId(const std::string& prefabId)
{
    return kPrefabDir / (prefabId + ".json");
}

std::string StemName(const std::filesystem::directory_entry& entry)
{
    return entry.path().stem().string();
}

std::vector<std::string> ListJsonAssetNames(const std::filesystem::path& root)
{
    std::vector<std::string> result;
    if (!std::filesystem::exists(root))
    {
        return result;
    }

    for (const auto& entry : std::filesystem::directory_iterator(root))
    {
        if (!entry.is_regular_file())
        {
            continue;
        }
        if (entry.path().extension() != ".json")
        {
            continue;
        }
        result.push_back(StemName(entry));
    }
    std::sort(result.begin(), result.end());
    return result;
}

glm::vec3 RotateY(const glm::vec3& value, float degrees)
{
    const float radians = glm::radians(degrees);
    const float c = std::cos(radians);
    const float s = std::sin(radians);
    return glm::vec3{
        value.x * c - value.z * s,
        value.y,
        value.x * s + value.z * c,
    };
}

glm::mat3 RotationMatrixFromEulerDegrees(const glm::vec3& eulerDegrees)
{
    glm::mat4 transform{1.0F};
    transform = glm::rotate(transform, glm::radians(eulerDegrees.y), glm::vec3{0.0F, 1.0F, 0.0F});
    transform = glm::rotate(transform, glm::radians(eulerDegrees.x), glm::vec3{1.0F, 0.0F, 0.0F});
    transform = glm::rotate(transform, glm::radians(eulerDegrees.z), glm::vec3{0.0F, 0.0F, 1.0F});
    return glm::mat3(transform);
}

glm::vec3 RotateExtentsXYZ(const glm::vec3& halfExtents, const glm::vec3& eulerDegrees)
{
    const glm::mat3 rotation = RotationMatrixFromEulerDegrees(eulerDegrees);
    const glm::mat3 absRotation{
        glm::abs(rotation[0]),
        glm::abs(rotation[1]),
        glm::abs(rotation[2]),
    };
    return absRotation * halfExtents;
}

glm::vec3 AabbMin(const glm::vec3& center, const glm::vec3& extents)
{
    return center - extents;
}

glm::vec3 AabbMax(const glm::vec3& center, const glm::vec3& extents)
{
    return center + extents;
}

bool OverlapAabb(const glm::vec3& minA, const glm::vec3& maxA, const glm::vec3& minB, const glm::vec3& maxB)
{
    return minA.x <= maxB.x && maxA.x >= minB.x &&
           minA.y <= maxB.y && maxA.y >= minB.y &&
           minA.z <= maxB.z && maxA.z >= minB.z;
}

int NormalizeRightAngle(int degrees)
{
    const int snapped = static_cast<int>(std::round(static_cast<float>(degrees) / 90.0F)) * 90;
    const int wrapped = snapped % 360;
    return wrapped < 0 ? wrapped + 360 : wrapped;
}

bool IsSnapped(float value, float step)
{
    if (step <= 0.0F)
    {
        return true;
    }
    const float scaled = value / step;
    return std::abs(scaled - std::round(scaled)) < 1.0e-3F;
}
} // namespace

std::string LevelAssetIO::SanitizeName(const std::string& value)
{
    std::string out;
    out.reserve(value.size());
    for (const char ch : value)
    {
        if ((ch >= 'a' && ch <= 'z') ||
            (ch >= 'A' && ch <= 'Z') ||
            (ch >= '0' && ch <= '9') ||
            ch == '_' || ch == '-')
        {
            out.push_back(ch);
        }
        else if (ch == ' ' || ch == '.')
        {
            out.push_back('_');
        }
    }
    if (out.empty())
    {
        out = "asset";
    }
    return out;
}

void LevelAssetIO::EnsureAssetDirectories()
{
    std::error_code ec;
    std::filesystem::create_directories(kLoopDir, ec);
    std::filesystem::create_directories(kMapDir, ec);
    std::filesystem::create_directories(kMaterialDir, ec);
    std::filesystem::create_directories(kAnimationDir, ec);
    std::filesystem::create_directories(kEnvironmentDir, ec);
    std::filesystem::create_directories(kPrefabDir, ec);
}

std::vector<std::string> LevelAssetIO::ListLoopIds()
{
    EnsureAssetDirectories();
    return ListJsonAssetNames(kLoopDir);
}

std::vector<std::string> LevelAssetIO::ListMapNames()
{
    EnsureAssetDirectories();
    return ListJsonAssetNames(kMapDir);
}

std::vector<std::string> LevelAssetIO::ListMaterialIds()
{
    EnsureAssetDirectories();
    return ListJsonAssetNames(kMaterialDir);
}

std::vector<std::string> LevelAssetIO::ListAnimationClipIds()
{
    EnsureAssetDirectories();
    return ListJsonAssetNames(kAnimationDir);
}

std::vector<std::string> LevelAssetIO::ListEnvironmentIds()
{
    EnsureAssetDirectories();
    return ListJsonAssetNames(kEnvironmentDir);
}

std::vector<std::string> LevelAssetIO::ListPrefabIds()
{
    EnsureAssetDirectories();
    return ListJsonAssetNames(kPrefabDir);
}

bool LevelAssetIO::SaveLoop(const LoopAsset& asset, std::string* outError)
{
    EnsureAssetDirectories();

    LoopAsset copy = asset;
    copy.assetVersion = kEditorAssetVersion;
    copy.id = SanitizeName(copy.id.empty() ? copy.displayName : copy.id);
    if (copy.displayName.empty())
    {
        copy.displayName = copy.id;
    }

    json root;
    root["asset_version"] = copy.assetVersion;
    root["id"] = copy.id;
    root["display_name"] = copy.displayName;
    root["bounds"] = {
        {"min", Vec3ToJson(copy.boundsMin)},
        {"max", Vec3ToJson(copy.boundsMax)},
    };
    root["footprint"] = {
        {"width", std::max(1, copy.footprintWidth)},
        {"height", std::max(1, copy.footprintHeight)},
    };
    root["manual_bounds"] = copy.manualBounds;
    root["manual_footprint"] = copy.manualFootprint;

    root["elements"] = json::array();
    for (const LoopElement& element : copy.elements)
    {
        root["elements"].push_back({
            {"type", LoopTypeToString(element.type)},
            {"name", element.name},
            {"position", Vec3ToJson(element.position)},
            {"half_extents", Vec3ToJson(element.halfExtents)},
            {"pitch_degrees", element.pitchDegrees},
            {"yaw_degrees", element.yawDegrees},
            {"roll_degrees", element.rollDegrees},
            {"transform_locked", element.transformLocked},
            {"marker_tag", element.markerTag},
        });
    }

    return WriteJsonFile(LoopPathFromId(copy.id), root, outError);
}

bool LevelAssetIO::LoadLoop(const std::string& loopId, LoopAsset* outAsset, std::string* outError)
{
    if (outAsset == nullptr)
    {
        if (outError != nullptr)
        {
            *outError = "LoadLoop called with null outAsset.";
        }
        return false;
    }

    EnsureAssetDirectories();

    const std::string sanitized = SanitizeName(loopId);
    json root;
    if (!ReadJsonFile(LoopPathFromId(sanitized), &root, outError))
    {
        return false;
    }

    const int version = root.value("asset_version", -1);
    if (version != kEditorAssetVersion)
    {
        if (outError != nullptr)
        {
            std::ostringstream oss;
            oss << "Unsupported loop asset version. Expected " << kEditorAssetVersion << ", got " << version;
            *outError = oss.str();
        }
        return false;
    }

    LoopAsset result;
    result.assetVersion = version;
    result.id = SanitizeName(root.value("id", sanitized));
    result.displayName = root.value("display_name", result.id);
    result.manualBounds = root.value("manual_bounds", false);
    result.manualFootprint = root.value("manual_footprint", false);

    const json bounds = root.value("bounds", json::object());
    result.boundsMin = Vec3FromJson(bounds.value("min", json::array()), glm::vec3{-8.0F, 0.0F, -8.0F});
    result.boundsMax = Vec3FromJson(bounds.value("max", json::array()), glm::vec3{8.0F, 2.0F, 8.0F});

    const json footprint = root.value("footprint", json::object());
    result.footprintWidth = std::max(1, footprint.value("width", 1));
    result.footprintHeight = std::max(1, footprint.value("height", 1));

    const json elements = root.value("elements", json::array());
    if (elements.is_array())
    {
        for (const json& item : elements)
        {
            LoopElement element;
            element.type = LoopTypeFromString(item.value("type", "wall"));
            element.name = item.value("name", LoopTypeToString(element.type));
            element.position = Vec3FromJson(item.value("position", json::array()), glm::vec3{0.0F, 1.0F, 0.0F});
            element.halfExtents = Vec3FromJson(item.value("half_extents", json::array()), glm::vec3{1.0F, 1.0F, 0.2F});
            element.pitchDegrees = item.value("pitch_degrees", 0.0F);
            element.yawDegrees = item.value("yaw_degrees", 0.0F);
            element.rollDegrees = item.value("roll_degrees", 0.0F);
            element.transformLocked = item.value("transform_locked", false);
            element.markerTag = item.value("marker_tag", std::string{});
            result.elements.push_back(element);
        }
    }

    *outAsset = result;
    return true;
}

bool LevelAssetIO::DeleteLoop(const std::string& loopId, std::string* outError)
{
    EnsureAssetDirectories();
    std::error_code ec;
    const bool removed = std::filesystem::remove(LoopPathFromId(SanitizeName(loopId)), ec);
    if (ec && outError != nullptr)
    {
        *outError = "Failed to delete loop: " + ec.message();
    }
    return removed && !ec;
}

bool LevelAssetIO::SaveMap(const MapAsset& asset, std::string* outError)
{
    EnsureAssetDirectories();

    MapAsset copy = asset;
    copy.assetVersion = kEditorAssetVersion;
    copy.name = SanitizeName(copy.name);
    copy.width = std::max(1, copy.width);
    copy.height = std::max(1, copy.height);
    copy.tileSize = std::max(1.0F, copy.tileSize);

    json root;
    root["asset_version"] = copy.assetVersion;
    root["name"] = copy.name;
    root["grid"] = {
        {"width", copy.width},
        {"height", copy.height},
        {"tile_size", copy.tileSize},
    };
    root["spawns"] = {
        {"survivor", Vec3ToJson(copy.survivorSpawn)},
        {"killer", Vec3ToJson(copy.killerSpawn)},
    };
    root["environment_asset"] = SanitizeName(copy.environmentAssetId.empty() ? "default_environment" : copy.environmentAssetId);
    root["lights"] = json::array();
    for (const LightInstance& light : copy.lights)
    {
        root["lights"].push_back({
            {"name", light.name},
            {"type", LightTypeToString(light.type)},
            {"position", Vec3ToJson(light.position)},
            {"rotation_euler", Vec3ToJson(light.rotationEuler)},
            {"color", Vec3ToJson(light.color)},
            {"intensity", light.intensity},
            {"range", light.range},
            {"spot_inner_angle", light.spotInnerAngle},
            {"spot_outer_angle", light.spotOuterAngle},
            {"enabled", light.enabled},
        });
    }

    root["placements"] = json::array();
    for (const LoopPlacement& placement : copy.placements)
    {
        root["placements"].push_back({
            {"loop_id", SanitizeName(placement.loopId)},
            {"tile", json::array({placement.tileX, placement.tileY})},
            {"rotation_degrees", NormalizeRightAngle(placement.rotationDegrees)},
            {"transform_locked", placement.transformLocked},
        });
    }

    root["props"] = json::array();
    for (const PropInstance& prop : copy.props)
    {
        root["props"].push_back({
            {"name", prop.name},
            {"type", PropTypeToString(prop.type)},
            {"position", Vec3ToJson(prop.position)},
            {"half_extents", Vec3ToJson(prop.halfExtents)},
            {"pitch_degrees", prop.pitchDegrees},
            {"yaw_degrees", prop.yawDegrees},
            {"roll_degrees", prop.rollDegrees},
            {"transform_locked", prop.transformLocked},
            {"solid", prop.solid},
            {"mesh_asset", prop.meshAsset},
            {"material_asset", prop.materialAsset},
            {"prefab_source", prop.prefabSourceId},
            {"prefab_instance", prop.prefabInstanceId},
            {"animation_clip", prop.animationClip},
            {"animation_loop", prop.animationLoop},
            {"animation_autoplay", prop.animationAutoplay},
            {"animation_speed", prop.animationSpeed},
            {"collider", {
                {"type", ColliderTypeToString(prop.colliderType)},
                {"offset", Vec3ToJson(prop.colliderOffset)},
                {"half_extents", Vec3ToJson(prop.colliderHalfExtents)},
                {"radius", prop.colliderRadius},
                {"height", prop.colliderHeight},
            }},
        });
    }

    return WriteJsonFile(MapPathFromName(copy.name), root, outError);
}

bool LevelAssetIO::LoadMap(const std::string& mapName, MapAsset* outAsset, std::string* outError)
{
    if (outAsset == nullptr)
    {
        if (outError != nullptr)
        {
            *outError = "LoadMap called with null outAsset.";
        }
        return false;
    }

    EnsureAssetDirectories();

    std::filesystem::path path = mapName;
    if (!path.has_extension())
    {
        path = MapPathFromName(SanitizeName(mapName));
    }

    json root;
    if (!ReadJsonFile(path, &root, outError))
    {
        return false;
    }

    const int version = root.value("asset_version", -1);
    if (version != kEditorAssetVersion)
    {
        if (outError != nullptr)
        {
            std::ostringstream oss;
            oss << "Unsupported map asset version. Expected " << kEditorAssetVersion << ", got " << version;
            *outError = oss.str();
        }
        return false;
    }

    MapAsset map;
    map.assetVersion = version;
    map.name = SanitizeName(root.value("name", path.stem().string()));
    const json grid = root.value("grid", json::object());
    map.width = std::max(1, grid.value("width", 64));
    map.height = std::max(1, grid.value("height", 64));
    map.tileSize = std::max(1.0F, grid.value("tile_size", kEditorTileSize));

    const json spawns = root.value("spawns", json::object());
    map.survivorSpawn = Vec3FromJson(spawns.value("survivor", json::array()), glm::vec3{-12.0F, 1.05F, -12.0F});
    map.killerSpawn = Vec3FromJson(spawns.value("killer", json::array()), glm::vec3{12.0F, 1.05F, 12.0F});
    map.environmentAssetId = SanitizeName(root.value("environment_asset", std::string{"default_environment"}));

    const json lights = root.value("lights", json::array());
    if (lights.is_array())
    {
        for (const json& item : lights)
        {
            LightInstance light;
            light.name = item.value("name", std::string{"light"});
            light.type = LightTypeFromString(item.value("type", std::string{"point"}));
            light.position = Vec3FromJson(item.value("position", json::array()), glm::vec3{0.0F, 2.5F, 0.0F});
            light.rotationEuler = Vec3FromJson(item.value("rotation_euler", json::array()), glm::vec3{0.0F});
            light.color = Vec3FromJson(item.value("color", json::array()), glm::vec3{1.0F, 0.95F, 0.85F});
            light.intensity = item.value("intensity", 1.0F);
            light.range = item.value("range", 12.0F);
            light.spotInnerAngle = item.value("spot_inner_angle", 22.0F);
            light.spotOuterAngle = item.value("spot_outer_angle", 38.0F);
            light.enabled = item.value("enabled", true);
            map.lights.push_back(light);
        }
    }

    const json placements = root.value("placements", json::array());
    if (placements.is_array())
    {
        for (const json& item : placements)
        {
            LoopPlacement placement;
            placement.loopId = SanitizeName(item.value("loop_id", std::string{}));
            const json tile = item.value("tile", json::array());
            if (tile.is_array() && tile.size() == 2)
            {
                placement.tileX = tile.at(0).get<int>();
                placement.tileY = tile.at(1).get<int>();
            }
            placement.rotationDegrees = NormalizeRightAngle(item.value("rotation_degrees", 0));
            placement.transformLocked = item.value("transform_locked", false);
            map.placements.push_back(placement);
        }
    }

    const json props = root.value("props", json::array());
    if (props.is_array())
    {
        for (const json& item : props)
        {
            PropInstance prop;
            prop.name = item.value("name", std::string{"prop"});
            prop.type = PropTypeFromString(item.value("type", "rock"));
            prop.position = Vec3FromJson(item.value("position", json::array()), glm::vec3{0.0F, 0.8F, 0.0F});
            prop.halfExtents = Vec3FromJson(item.value("half_extents", json::array()), glm::vec3{0.8F, 0.8F, 0.8F});
            prop.pitchDegrees = item.value("pitch_degrees", 0.0F);
            prop.yawDegrees = item.value("yaw_degrees", 0.0F);
            prop.rollDegrees = item.value("roll_degrees", 0.0F);
            prop.transformLocked = item.value("transform_locked", false);
            prop.solid = item.value("solid", true);
            prop.meshAsset = item.value("mesh_asset", std::string{});
            prop.materialAsset = item.value("material_asset", std::string{});
            prop.prefabSourceId = item.value("prefab_source", std::string{});
            prop.prefabInstanceId = item.value("prefab_instance", std::string{});
            prop.animationClip = item.value("animation_clip", std::string{});
            prop.animationLoop = item.value("animation_loop", true);
            prop.animationAutoplay = item.value("animation_autoplay", false);
            prop.animationSpeed = item.value("animation_speed", 1.0F);

            const json collider = item.value("collider", json::object());
            prop.colliderType = ColliderTypeFromString(collider.value("type", "box"));
            prop.colliderOffset = Vec3FromJson(collider.value("offset", json::array()), glm::vec3{0.0F});
            prop.colliderHalfExtents = Vec3FromJson(collider.value("half_extents", json::array()), prop.halfExtents);
            prop.colliderRadius = collider.value("radius", 0.45F);
            prop.colliderHeight = collider.value("height", 1.8F);
            map.props.push_back(prop);
        }
    }

    *outAsset = map;
    return true;
}

bool LevelAssetIO::DeleteMap(const std::string& mapName, std::string* outError)
{
    EnsureAssetDirectories();
    std::error_code ec;
    const bool removed = std::filesystem::remove(MapPathFromName(SanitizeName(mapName)), ec);
    if (ec && outError != nullptr)
    {
        *outError = "Failed to delete map: " + ec.message();
    }
    return removed && !ec;
}

bool LevelAssetIO::SaveMaterial(const MaterialAsset& asset, std::string* outError)
{
    EnsureAssetDirectories();

    MaterialAsset copy = asset;
    copy.assetVersion = kEditorAssetVersion;
    copy.id = SanitizeName(copy.id.empty() ? copy.displayName : copy.id);
    if (copy.displayName.empty())
    {
        copy.displayName = copy.id;
    }

    json root;
    root["asset_version"] = copy.assetVersion;
    root["id"] = copy.id;
    root["display_name"] = copy.displayName;
    root["shader_type"] = MaterialShaderTypeToString(copy.shaderType);
    root["base_color"] = Vec4ToJson(copy.baseColor);
    root["textures"] = {
        {"albedo", copy.albedoTexture},
        {"normal", copy.normalTexture},
        {"orm", copy.ormTexture},
    };
    root["params"] = {
        {"roughness", copy.roughness},
        {"metallic", copy.metallic},
        {"emissive_strength", copy.emissiveStrength},
    };

    return WriteJsonFile(MaterialPathFromId(copy.id), root, outError);
}

bool LevelAssetIO::LoadMaterial(const std::string& materialId, MaterialAsset* outAsset, std::string* outError)
{
    if (outAsset == nullptr)
    {
        if (outError != nullptr)
        {
            *outError = "LoadMaterial called with null outAsset.";
        }
        return false;
    }

    EnsureAssetDirectories();
    const std::string sanitized = SanitizeName(materialId);
    json root;
    if (!ReadJsonFile(MaterialPathFromId(sanitized), &root, outError))
    {
        return false;
    }

    const int version = root.value("asset_version", -1);
    if (version != kEditorAssetVersion)
    {
        if (outError != nullptr)
        {
            *outError = "Unsupported material version.";
        }
        return false;
    }

    MaterialAsset asset;
    asset.assetVersion = version;
    asset.id = SanitizeName(root.value("id", sanitized));
    asset.displayName = root.value("display_name", asset.id);
    asset.shaderType = MaterialShaderTypeFromString(root.value("shader_type", "lit"));
    asset.baseColor = Vec4FromJson(root.value("base_color", json::array()), glm::vec4{0.8F, 0.82F, 0.88F, 1.0F});
    const json textures = root.value("textures", json::object());
    asset.albedoTexture = textures.value("albedo", std::string{});
    asset.normalTexture = textures.value("normal", std::string{});
    asset.ormTexture = textures.value("orm", std::string{});
    const json params = root.value("params", json::object());
    asset.roughness = params.value("roughness", 0.55F);
    asset.metallic = params.value("metallic", 0.0F);
    asset.emissiveStrength = params.value("emissive_strength", 0.0F);

    *outAsset = asset;
    return true;
}

bool LevelAssetIO::DeleteMaterial(const std::string& materialId, std::string* outError)
{
    EnsureAssetDirectories();
    std::error_code ec;
    const bool removed = std::filesystem::remove(MaterialPathFromId(SanitizeName(materialId)), ec);
    if (ec && outError != nullptr)
    {
        *outError = "Failed to delete material: " + ec.message();
    }
    return removed && !ec;
}

bool LevelAssetIO::SaveAnimationClip(const AnimationClipAsset& asset, std::string* outError)
{
    EnsureAssetDirectories();

    AnimationClipAsset copy = asset;
    copy.assetVersion = kEditorAssetVersion;
    copy.id = SanitizeName(copy.id.empty() ? copy.displayName : copy.id);
    if (copy.displayName.empty())
    {
        copy.displayName = copy.id;
    }

    json root;
    root["asset_version"] = copy.assetVersion;
    root["id"] = copy.id;
    root["display_name"] = copy.displayName;
    root["loop"] = copy.loop;
    root["speed"] = copy.speed;
    root["keyframes"] = json::array();
    for (const AnimationKeyframe& key : copy.keyframes)
    {
        root["keyframes"].push_back({
            {"time", key.time},
            {"position", Vec3ToJson(key.position)},
            {"rotation", Vec3ToJson(key.rotationEuler)},
            {"scale", Vec3ToJson(key.scale)},
        });
    }
    return WriteJsonFile(AnimationPathFromId(copy.id), root, outError);
}

bool LevelAssetIO::LoadAnimationClip(const std::string& clipId, AnimationClipAsset* outAsset, std::string* outError)
{
    if (outAsset == nullptr)
    {
        if (outError != nullptr)
        {
            *outError = "LoadAnimationClip called with null outAsset.";
        }
        return false;
    }

    EnsureAssetDirectories();
    const std::string sanitized = SanitizeName(clipId);
    json root;
    if (!ReadJsonFile(AnimationPathFromId(sanitized), &root, outError))
    {
        return false;
    }

    const int version = root.value("asset_version", -1);
    if (version != kEditorAssetVersion)
    {
        if (outError != nullptr)
        {
            *outError = "Unsupported animation clip version.";
        }
        return false;
    }

    AnimationClipAsset clip;
    clip.assetVersion = version;
    clip.id = SanitizeName(root.value("id", sanitized));
    clip.displayName = root.value("display_name", clip.id);
    clip.loop = root.value("loop", true);
    clip.speed = root.value("speed", 1.0F);

    const json keyframes = root.value("keyframes", json::array());
    if (keyframes.is_array())
    {
        for (const json& key : keyframes)
        {
            AnimationKeyframe frame;
            frame.time = key.value("time", 0.0F);
            frame.position = Vec3FromJson(key.value("position", json::array()), glm::vec3{0.0F});
            frame.rotationEuler = Vec3FromJson(key.value("rotation", json::array()), glm::vec3{0.0F});
            frame.scale = Vec3FromJson(key.value("scale", json::array()), glm::vec3{1.0F});
            clip.keyframes.push_back(frame);
        }
    }

    if (clip.keyframes.empty())
    {
        clip.keyframes.push_back(AnimationKeyframe{0.0F, glm::vec3{0.0F}, glm::vec3{0.0F}, glm::vec3{1.0F}});
    }

    std::sort(clip.keyframes.begin(), clip.keyframes.end(), [](const AnimationKeyframe& a, const AnimationKeyframe& b) {
        return a.time < b.time;
    });

    *outAsset = clip;
    return true;
}

bool LevelAssetIO::DeleteAnimationClip(const std::string& clipId, std::string* outError)
{
    EnsureAssetDirectories();
    std::error_code ec;
    const bool removed = std::filesystem::remove(AnimationPathFromId(SanitizeName(clipId)), ec);
    if (ec && outError != nullptr)
    {
        *outError = "Failed to delete animation clip: " + ec.message();
    }
    return removed && !ec;
}

bool LevelAssetIO::SaveEnvironment(const EnvironmentAsset& asset, std::string* outError)
{
    EnsureAssetDirectories();

    EnvironmentAsset copy = asset;
    copy.assetVersion = kEditorAssetVersion;
    copy.id = SanitizeName(copy.id.empty() ? copy.displayName : copy.id);
    if (copy.displayName.empty())
    {
        copy.displayName = copy.id;
    }

    json root;
    root["asset_version"] = copy.assetVersion;
    root["id"] = copy.id;
    root["display_name"] = copy.displayName;
    root["sky"] = {
        {"top_color", Vec3ToJson(copy.skyTopColor)},
        {"bottom_color", Vec3ToJson(copy.skyBottomColor)},
    };
    root["clouds"] = {
        {"enabled", copy.cloudsEnabled},
        {"coverage", copy.cloudCoverage},
        {"density", copy.cloudDensity},
        {"speed", copy.cloudSpeed},
    };
    root["directional_light"] = {
        {"direction", Vec3ToJson(copy.directionalLightDirection)},
        {"color", Vec3ToJson(copy.directionalLightColor)},
        {"intensity", copy.directionalLightIntensity},
    };
    root["fog"] = {
        {"enabled", copy.fogEnabled},
        {"color", Vec3ToJson(copy.fogColor)},
        {"density", copy.fogDensity},
        {"start", copy.fogStart},
        {"end", copy.fogEnd},
    };
    root["graphics"] = {
        {"shadow_quality", copy.shadowQuality},
        {"shadow_distance", copy.shadowDistance},
        {"tone_mapping", copy.toneMapping},
        {"exposure", copy.exposure},
        {"bloom", copy.bloom},
    };

    return WriteJsonFile(EnvironmentPathFromId(copy.id), root, outError);
}

bool LevelAssetIO::LoadEnvironment(const std::string& environmentId, EnvironmentAsset* outAsset, std::string* outError)
{
    if (outAsset == nullptr)
    {
        if (outError != nullptr)
        {
            *outError = "LoadEnvironment called with null outAsset.";
        }
        return false;
    }

    EnsureAssetDirectories();
    const std::string sanitized = SanitizeName(environmentId);
    json root;
    if (!ReadJsonFile(EnvironmentPathFromId(sanitized), &root, outError))
    {
        return false;
    }

    const int version = root.value("asset_version", -1);
    if (version != kEditorAssetVersion)
    {
        if (outError != nullptr)
        {
            *outError = "Unsupported environment asset version.";
        }
        return false;
    }

    EnvironmentAsset env;
    env.assetVersion = version;
    env.id = SanitizeName(root.value("id", sanitized));
    env.displayName = root.value("display_name", env.id);

    const json sky = root.value("sky", json::object());
    env.skyTopColor = Vec3FromJson(sky.value("top_color", json::array()), env.skyTopColor);
    env.skyBottomColor = Vec3FromJson(sky.value("bottom_color", json::array()), env.skyBottomColor);

    const json clouds = root.value("clouds", json::object());
    env.cloudsEnabled = clouds.value("enabled", true);
    env.cloudCoverage = clouds.value("coverage", env.cloudCoverage);
    env.cloudDensity = clouds.value("density", env.cloudDensity);
    env.cloudSpeed = clouds.value("speed", env.cloudSpeed);

    const json light = root.value("directional_light", json::object());
    env.directionalLightDirection = Vec3FromJson(light.value("direction", json::array()), env.directionalLightDirection);
    env.directionalLightColor = Vec3FromJson(light.value("color", json::array()), env.directionalLightColor);
    env.directionalLightIntensity = light.value("intensity", env.directionalLightIntensity);

    const json fog = root.value("fog", json::object());
    env.fogEnabled = fog.value("enabled", env.fogEnabled);
    env.fogColor = Vec3FromJson(fog.value("color", json::array()), env.fogColor);
    env.fogDensity = fog.value("density", env.fogDensity);
    env.fogStart = fog.value("start", env.fogStart);
    env.fogEnd = fog.value("end", env.fogEnd);

    const json graphics = root.value("graphics", json::object());
    env.shadowQuality = graphics.value("shadow_quality", env.shadowQuality);
    env.shadowDistance = graphics.value("shadow_distance", env.shadowDistance);
    env.toneMapping = graphics.value("tone_mapping", env.toneMapping);
    env.exposure = graphics.value("exposure", env.exposure);
    env.bloom = graphics.value("bloom", env.bloom);

    *outAsset = env;
    return true;
}

bool LevelAssetIO::DeleteEnvironment(const std::string& environmentId, std::string* outError)
{
    EnsureAssetDirectories();
    std::error_code ec;
    const bool removed = std::filesystem::remove(EnvironmentPathFromId(SanitizeName(environmentId)), ec);
    if (ec && outError != nullptr)
    {
        *outError = "Failed to delete environment: " + ec.message();
    }
    return removed && !ec;
}

bool LevelAssetIO::SavePrefab(const PrefabAsset& asset, std::string* outError)
{
    EnsureAssetDirectories();

    PrefabAsset copy = asset;
    copy.assetVersion = kEditorAssetVersion;
    copy.id = SanitizeName(copy.id.empty() ? copy.displayName : copy.id);
    if (copy.displayName.empty())
    {
        copy.displayName = copy.id;
    }

    json root;
    root["asset_version"] = copy.assetVersion;
    root["id"] = copy.id;
    root["display_name"] = copy.displayName;
    root["props"] = json::array();
    for (const PropInstance& prop : copy.props)
    {
        root["props"].push_back({
            {"name", prop.name},
            {"type", PropTypeToString(prop.type)},
            {"position", Vec3ToJson(prop.position)},
            {"half_extents", Vec3ToJson(prop.halfExtents)},
            {"pitch_degrees", prop.pitchDegrees},
            {"yaw_degrees", prop.yawDegrees},
            {"roll_degrees", prop.rollDegrees},
            {"mesh_asset", prop.meshAsset},
            {"material_asset", prop.materialAsset},
            {"animation_clip", prop.animationClip},
            {"animation_loop", prop.animationLoop},
            {"animation_autoplay", prop.animationAutoplay},
            {"animation_speed", prop.animationSpeed},
            {"collider", {
                {"type", ColliderTypeToString(prop.colliderType)},
                {"offset", Vec3ToJson(prop.colliderOffset)},
                {"half_extents", Vec3ToJson(prop.colliderHalfExtents)},
                {"radius", prop.colliderRadius},
                {"height", prop.colliderHeight},
            }},
            {"solid", prop.solid},
        });
    }

    return WriteJsonFile(PrefabPathFromId(copy.id), root, outError);
}

bool LevelAssetIO::LoadPrefab(const std::string& prefabId, PrefabAsset* outAsset, std::string* outError)
{
    if (outAsset == nullptr)
    {
        if (outError != nullptr)
        {
            *outError = "LoadPrefab called with null outAsset.";
        }
        return false;
    }

    EnsureAssetDirectories();
    const std::string sanitized = SanitizeName(prefabId);
    json root;
    if (!ReadJsonFile(PrefabPathFromId(sanitized), &root, outError))
    {
        return false;
    }

    const int version = root.value("asset_version", -1);
    if (version != kEditorAssetVersion)
    {
        if (outError != nullptr)
        {
            *outError = "Unsupported prefab asset version.";
        }
        return false;
    }

    PrefabAsset prefab;
    prefab.assetVersion = version;
    prefab.id = SanitizeName(root.value("id", sanitized));
    prefab.displayName = root.value("display_name", prefab.id);

    const json props = root.value("props", json::array());
    if (props.is_array())
    {
        for (const json& item : props)
        {
            PropInstance prop;
            prop.name = item.value("name", std::string{"prop"});
            prop.type = PropTypeFromString(item.value("type", "rock"));
            prop.position = Vec3FromJson(item.value("position", json::array()), glm::vec3{0.0F});
            prop.halfExtents = Vec3FromJson(item.value("half_extents", json::array()), glm::vec3{0.8F, 0.8F, 0.8F});
            prop.pitchDegrees = item.value("pitch_degrees", 0.0F);
            prop.yawDegrees = item.value("yaw_degrees", 0.0F);
            prop.rollDegrees = item.value("roll_degrees", 0.0F);
            prop.meshAsset = item.value("mesh_asset", std::string{});
            prop.materialAsset = item.value("material_asset", std::string{});
            prop.animationClip = item.value("animation_clip", std::string{});
            prop.animationLoop = item.value("animation_loop", true);
            prop.animationAutoplay = item.value("animation_autoplay", false);
            prop.animationSpeed = item.value("animation_speed", 1.0F);
            prop.solid = item.value("solid", true);
            const json collider = item.value("collider", json::object());
            prop.colliderType = ColliderTypeFromString(collider.value("type", "box"));
            prop.colliderOffset = Vec3FromJson(collider.value("offset", json::array()), glm::vec3{0.0F});
            prop.colliderHalfExtents = Vec3FromJson(collider.value("half_extents", json::array()), prop.halfExtents);
            prop.colliderRadius = collider.value("radius", 0.45F);
            prop.colliderHeight = collider.value("height", 1.8F);
            prefab.props.push_back(prop);
        }
    }

    *outAsset = prefab;
    return true;
}

bool LevelAssetIO::DeletePrefab(const std::string& prefabId, std::string* outError)
{
    EnsureAssetDirectories();
    std::error_code ec;
    const bool removed = std::filesystem::remove(PrefabPathFromId(SanitizeName(prefabId)), ec);
    if (ec && outError != nullptr)
    {
        *outError = "Failed to delete prefab: " + ec.message();
    }
    return removed && !ec;
}

std::vector<std::string> LevelAssetIO::ValidateLoop(const LoopAsset& asset)
{
    std::vector<std::string> issues;

    if (asset.id.empty())
    {
        issues.push_back("Loop id is empty.");
    }
    if (asset.elements.empty())
    {
        issues.push_back("Loop has no elements.");
    }
    if (asset.footprintWidth <= 0 || asset.footprintHeight <= 0)
    {
        issues.push_back("Loop footprint must be positive.");
    }

    for (const LoopElement& element : asset.elements)
    {
        if ((element.type == LoopElementType::Wall ||
             element.type == LoopElementType::Window ||
             element.type == LoopElementType::Pallet) &&
            (element.halfExtents.x <= 0.01F || element.halfExtents.y <= 0.01F || element.halfExtents.z <= 0.01F))
        {
            issues.push_back("Element " + element.name + " has invalid size.");
        }

        if (!IsSnapped(element.position.x, 0.5F) || !IsSnapped(element.position.z, 0.5F))
        {
            issues.push_back("Walls not snapped to grid: " + element.name);
        }

        if (element.type == LoopElementType::Window &&
            (!std::isfinite(element.pitchDegrees) || !std::isfinite(element.yawDegrees) || !std::isfinite(element.rollDegrees)))
        {
            issues.push_back("Window missing vault direction: " + element.name);
        }
    }

    for (std::size_t i = 0; i < asset.elements.size(); ++i)
    {
        const LoopElement& a = asset.elements[i];
        if (a.type != LoopElementType::Pallet)
        {
            continue;
        }
        const glm::vec3 aMin = AabbMin(a.position, a.halfExtents);
        const glm::vec3 aMax = AabbMax(a.position, a.halfExtents);
        for (std::size_t j = 0; j < asset.elements.size(); ++j)
        {
            if (i == j)
            {
                continue;
            }
            const LoopElement& b = asset.elements[j];
            if (b.type != LoopElementType::Wall)
            {
                continue;
            }
            const glm::vec3 bMin = AabbMin(b.position, b.halfExtents);
            const glm::vec3 bMax = AabbMax(b.position, b.halfExtents);
            if (OverlapAabb(aMin, aMax, bMin, bMax))
            {
                issues.push_back("Pallet overlap with wall: " + a.name + " vs " + b.name);
            }
        }
    }

    if ((asset.boundsMax.x - asset.boundsMin.x) < 1.0F || (asset.boundsMax.z - asset.boundsMin.z) < 1.0F)
    {
        issues.push_back("Loop bounds too small.");
    }

    return issues;
}

bool LevelAssetIO::BuildGeneratedMapFromAsset(const MapAsset& mapAsset, maps::GeneratedMap* outMap, std::string* outError)
{
    if (outMap == nullptr)
    {
        if (outError != nullptr)
        {
            *outError = "BuildGeneratedMapFromAsset called with null outMap.";
        }
        return false;
    }

    const int width = std::max(1, mapAsset.width);
    const int height = std::max(1, mapAsset.height);
    const float tileSize = std::max(1.0F, mapAsset.tileSize);
    const float halfWidth = static_cast<float>(width) * tileSize * 0.5F;
    const float halfHeight = static_cast<float>(height) * tileSize * 0.5F;

    maps::GeneratedMap generated;
    generated.survivorSpawn = mapAsset.survivorSpawn;
    generated.killerSpawn = mapAsset.killerSpawn;

    generated.walls.push_back(maps::BoxSpawn{glm::vec3{0.0F, -0.5F, 0.0F}, glm::vec3{halfWidth + 6.0F, 0.5F, halfHeight + 6.0F}});
    generated.walls.push_back(maps::BoxSpawn{glm::vec3{0.0F, 1.0F, -(halfHeight + 0.6F)}, glm::vec3{halfWidth + 4.0F, 1.0F, 0.6F}});
    generated.walls.push_back(maps::BoxSpawn{glm::vec3{0.0F, 1.0F, (halfHeight + 0.6F)}, glm::vec3{halfWidth + 4.0F, 1.0F, 0.6F}});
    generated.walls.push_back(maps::BoxSpawn{glm::vec3{-(halfWidth + 0.6F), 1.0F, 0.0F}, glm::vec3{0.6F, 1.0F, halfHeight + 4.0F}});
    generated.walls.push_back(maps::BoxSpawn{glm::vec3{(halfWidth + 0.6F), 1.0F, 0.0F}, glm::vec3{0.6F, 1.0F, halfHeight + 4.0F}});

    std::unordered_map<std::string, LoopAsset> loadedLoops;
    std::vector<int> occupancy(static_cast<std::size_t>(width * height), -1);

    auto cellIndex = [width](int x, int y) {
        return y * width + x;
    };

    for (std::size_t placementIndex = 0; placementIndex < mapAsset.placements.size(); ++placementIndex)
    {
        const LoopPlacement& placement = mapAsset.placements[placementIndex];
        if (placement.loopId.empty())
        {
            continue;
        }

        if (!loadedLoops.contains(placement.loopId))
        {
            LoopAsset loaded;
            if (!LoadLoop(placement.loopId, &loaded, outError))
            {
                if (outError != nullptr && outError->empty())
                {
                    *outError = "Missing loop asset: " + placement.loopId;
                }
                return false;
            }
            loadedLoops.emplace(placement.loopId, loaded);
        }
        const LoopAsset& loop = loadedLoops.at(placement.loopId);

        const int rotation = NormalizeRightAngle(placement.rotationDegrees);
        const bool swapFootprint = (rotation == 90 || rotation == 270);
        const int footprintW = swapFootprint ? std::max(1, loop.footprintHeight) : std::max(1, loop.footprintWidth);
        const int footprintH = swapFootprint ? std::max(1, loop.footprintWidth) : std::max(1, loop.footprintHeight);

        if (placement.tileX < 0 || placement.tileY < 0 ||
            placement.tileX + footprintW > width || placement.tileY + footprintH > height)
        {
            if (outError != nullptr)
            {
                std::ostringstream oss;
                oss << "Placement out of bounds for loop '" << placement.loopId << "' at tile (" << placement.tileX << "," << placement.tileY << ")";
                *outError = oss.str();
            }
            return false;
        }

        for (int y = 0; y < footprintH; ++y)
        {
            for (int x = 0; x < footprintW; ++x)
            {
                const int idx = cellIndex(placement.tileX + x, placement.tileY + y);
                if (occupancy[static_cast<std::size_t>(idx)] >= 0)
                {
                    if (outError != nullptr)
                    {
                        std::ostringstream oss;
                        oss << "Loop overlap at tile (" << (placement.tileX + x) << "," << (placement.tileY + y) << ")";
                        *outError = oss.str();
                    }
                    return false;
                }
                occupancy[static_cast<std::size_t>(idx)] = static_cast<int>(placementIndex);
            }
        }

        const float minCenterX = -halfWidth + tileSize * 0.5F + static_cast<float>(placement.tileX) * tileSize;
        const float minCenterZ = -halfHeight + tileSize * 0.5F + static_cast<float>(placement.tileY) * tileSize;
        const glm::vec3 pivot{
            minCenterX + static_cast<float>(footprintW - 1) * tileSize * 0.5F,
            0.0F,
            minCenterZ + static_cast<float>(footprintH - 1) * tileSize * 0.5F,
        };

        const int loopHash = static_cast<int>(std::hash<std::string>{}(placement.loopId) % 997U);
        generated.tiles.push_back(maps::GeneratedMap::TileDebug{
            pivot,
            glm::vec3{static_cast<float>(footprintW) * tileSize * 0.5F, 0.05F, static_cast<float>(footprintH) * tileSize * 0.5F},
            static_cast<int>(placementIndex),
            100 + loopHash,
        });

        for (const LoopElement& element : loop.elements)
        {
            const glm::vec3 worldCenter = pivot + RotateY(element.position, static_cast<float>(rotation));
            const float totalYaw = static_cast<float>(rotation) + element.yawDegrees;
            const glm::vec3 totalRotation{
                element.pitchDegrees,
                totalYaw,
                element.rollDegrees,
            };
            const glm::vec3 extents = RotateExtentsXYZ(element.halfExtents, totalRotation);

            if (element.type == LoopElementType::Wall)
            {
                generated.walls.push_back(maps::BoxSpawn{worldCenter, extents});
            }
            else if (element.type == LoopElementType::Window)
            {
                const glm::vec3 normal = glm::normalize(RotationMatrixFromEulerDegrees(totalRotation) * glm::vec3{0.0F, 0.0F, 1.0F});
                generated.windows.push_back(maps::WindowSpawn{
                    worldCenter,
                    extents,
                    normal,
                });
            }
            else if (element.type == LoopElementType::Pallet)
            {
                generated.pallets.push_back(maps::PalletSpawn{
                    glm::vec3{worldCenter.x, std::max(0.6F, worldCenter.y), worldCenter.z},
                    extents,
                });
            }
            else if (element.type == LoopElementType::Marker)
            {
                if (element.markerTag == "survivor_spawn")
                {
                    generated.survivorSpawn = worldCenter;
                }
                else if (element.markerTag == "killer_spawn")
                {
                    generated.killerSpawn = worldCenter;
                }
            }
        }
    }

    for (const PropInstance& prop : mapAsset.props)
    {
        if (!prop.solid)
        {
            continue;
        }

        if (prop.colliderType == ColliderType::None)
        {
            continue;
        }

        const glm::vec3 sourceExtents = prop.colliderType == ColliderType::Box ? prop.colliderHalfExtents : glm::vec3{prop.colliderRadius, prop.colliderHeight * 0.5F, prop.colliderRadius};
        const glm::vec3 extents = RotateExtentsXYZ(sourceExtents, glm::vec3{prop.pitchDegrees, prop.yawDegrees, prop.rollDegrees});
        generated.walls.push_back(maps::BoxSpawn{
            prop.position + prop.colliderOffset,
            extents,
        });
    }

    *outMap = generated;
    return true;
}

bool LevelAssetIO::BuildGeneratedMapFromMapName(const std::string& mapName, maps::GeneratedMap* outMap, std::string* outError)
{
    MapAsset asset;
    if (!LoadMap(mapName, &asset, outError))
    {
        return false;
    }
    return BuildGeneratedMapFromAsset(asset, outMap, outError);
}
} // namespace game::editor
