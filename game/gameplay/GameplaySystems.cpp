#include "game/gameplay/GameplaySystems.hpp"
#include "game/gameplay/SpawnSystem.hpp"
#include "game/gameplay/PerkSystem.hpp"
#include "engine/scene/Components.hpp"
#include "engine/core/JobSystem.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <limits>
#include <random>
#include <sstream>
#include <unordered_set>
#include <unordered_map>

#include <GLFW/glfw3.h>
#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#define TINYGLTF_NO_STB_IMAGE_WRITE
#include <tiny_gltf.h>

#include "engine/platform/Input.hpp"
#include "engine/render/Renderer.hpp"
#include "engine/assets/MeshLibrary.hpp"
#include "engine/core/Profiler.hpp"
#include "engine/physics/ColliderGen_WallBoxes.hpp"
#include "game/editor/LevelAssets.hpp"
#include "game/maps/TileGenerator.hpp"

namespace game::gameplay
{
namespace
{
constexpr float kGravity = -20.0F;
constexpr float kPi = 3.1415926535F;
constexpr float kPovLodBufferScale = 1.10F; // Dynamic actor edge buffer for low-LOD fallback

engine::scene::Entity SpawnActor(
    engine::scene::World& world,
    engine::scene::Role role,
    const glm::vec3& position,
    const glm::vec3& color
)
{
    const engine::scene::Entity entity = world.CreateEntity();

    engine::scene::Transform transform;
    transform.position = position;
    transform.rotationEuler = glm::vec3{0.0F};
    transform.scale = glm::vec3{1.0F};
    transform.forward = glm::vec3{0.0F, 0.0F, -1.0F};
    world.Transforms()[entity] = transform;

    engine::scene::ActorComponent actor;
    actor.role = role;
    if (role == engine::scene::Role::Survivor)
    {
        actor.walkSpeed = 2.85F;
        actor.sprintSpeed = 4.6F;
        actor.eyeHeight = 1.55F;
    }
    else
    {
        actor.walkSpeed = 4.6F * 1.15F;
        actor.sprintSpeed = 4.6F * 1.15F;
        actor.eyeHeight = 1.62F;
    }

    world.Actors()[entity] = actor;
    world.DebugColors()[entity] = engine::scene::DebugColorComponent{color};
    world.Names()[entity] = engine::scene::NameComponent{role == engine::scene::Role::Survivor ? "survivor" : "killer"};

    return entity;
}

glm::vec2 ReadMoveAxis(const engine::platform::Input& input, const engine::platform::ActionBindings& bindings)
{
    glm::vec2 axis{0.0F, 0.0F};

    if (bindings.IsDown(input, engine::platform::InputAction::MoveLeft))
    {
        axis.x -= 1.0F;
    }
    if (bindings.IsDown(input, engine::platform::InputAction::MoveRight))
    {
        axis.x += 1.0F;
    }
    if (bindings.IsDown(input, engine::platform::InputAction::MoveBackward))
    {
        axis.y -= 1.0F;
    }
    if (bindings.IsDown(input, engine::platform::InputAction::MoveForward))
    {
        axis.y += 1.0F;
    }

    if (glm::length(axis) > 1.0e-5F)
    {
        axis = glm::normalize(axis);
    }

    return axis;
}

std::string MapToName(GameplaySystems::MapType type)
{
    switch (type)
    {
        case GameplaySystems::MapType::Test: return "test";
        case GameplaySystems::MapType::Main: return "main";
        case GameplaySystems::MapType::CollisionTest: return "collision_test";
        case GameplaySystems::MapType::Benchmark: return "benchmark";
        default: return "unknown";
    }
}

void ItemPowerLog(const std::string& text)
{
    std::cout << "[ITEM/POWER] " << text << "\n";
}

std::filesystem::path ResolveAssetPathFromCwd(const std::string& relativeOrAbsolutePath)
{
    std::error_code ec;
    const std::filesystem::path input = relativeOrAbsolutePath;
    if (input.is_absolute())
    {
        return input;
    }

    const std::filesystem::path cwd = std::filesystem::current_path(ec);
    const std::array<std::filesystem::path, 4> candidates{
        cwd / input,
        cwd / ".." / input,
        cwd / ".." / ".." / input,
        input
    };
    for (const std::filesystem::path& candidate : candidates)
    {
        if (std::filesystem::exists(candidate, ec))
        {
            return std::filesystem::absolute(candidate, ec);
        }
    }
    return std::filesystem::absolute(input, ec);
}

float WrapAngleRadians(float angle)
{
    while (angle > glm::pi<float>())
    {
        angle -= glm::two_pi<float>();
    }
    while (angle < -glm::pi<float>())
    {
        angle += glm::two_pi<float>();
    }
    return angle;
}

glm::vec2 MoveTowardsVector(const glm::vec2& current, const glm::vec2& target, float maxDelta)
{
    if (maxDelta <= 0.0F)
    {
        return current;
    }

    const glm::vec2 delta = target - current;
    const float distance = glm::length(delta);
    if (distance <= maxDelta || distance <= 1.0e-6F)
    {
        return target;
    }

    return current + (delta / distance) * maxDelta;
}

std::string ToLowerCopy(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

bool ContainsToken(const std::string& clipName, const std::string& token)
{
    return clipName.find(token) != std::string::npos;
}

std::string PickLocomotionClip(
    const std::vector<std::string>& clipNames,
    const std::vector<std::string>& orderedTokens,
    const std::string& preferredClip
)
{
    if (!preferredClip.empty())
    {
        const auto preferredIt = std::find(clipNames.begin(), clipNames.end(), preferredClip);
        if (preferredIt != clipNames.end())
        {
            return preferredClip;
        }
    }

    for (const std::string& token : orderedTokens)
    {
        for (const std::string& candidate : clipNames)
        {
            if (ContainsToken(ToLowerCopy(candidate), token))
            {
                return candidate;
            }
        }
    }

    return preferredClip;
}

bool ReadAccessorScalarsAsIndicesTiny(
    const tinygltf::Model& model,
    const tinygltf::Accessor& accessor,
    std::vector<std::uint32_t>* outIndices
)
{
    if (outIndices == nullptr || accessor.bufferView < 0 || accessor.bufferView >= static_cast<int>(model.bufferViews.size()))
    {
        return false;
    }
    if (accessor.type != TINYGLTF_TYPE_SCALAR)
    {
        return false;
    }
    const tinygltf::BufferView& view = model.bufferViews[static_cast<std::size_t>(accessor.bufferView)];
    if (view.buffer < 0 || view.buffer >= static_cast<int>(model.buffers.size()))
    {
        return false;
    }
    const tinygltf::Buffer& buffer = model.buffers[static_cast<std::size_t>(view.buffer)];

    const int componentSize = tinygltf::GetComponentSizeInBytes(accessor.componentType);
    if (componentSize <= 0)
    {
        return false;
    }
    const std::size_t stride = accessor.ByteStride(view) > 0 ? static_cast<std::size_t>(accessor.ByteStride(view)) : static_cast<std::size_t>(componentSize);
    const std::size_t baseOffset = static_cast<std::size_t>(view.byteOffset + accessor.byteOffset);
    if (baseOffset >= buffer.data.size())
    {
        return false;
    }

    outIndices->clear();
    outIndices->reserve(accessor.count);
    for (std::size_t i = 0; i < accessor.count; ++i)
    {
        const std::size_t offset = baseOffset + i * stride;
        if (offset + static_cast<std::size_t>(componentSize) > buffer.data.size())
        {
            return false;
        }

        std::uint32_t value = 0;
        switch (accessor.componentType)
        {
            case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
            {
                std::uint8_t v = 0;
                std::memcpy(&v, buffer.data.data() + offset, sizeof(v));
                value = v;
                break;
            }
            case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT:
            {
                std::uint16_t v = 0;
                std::memcpy(&v, buffer.data.data() + offset, sizeof(v));
                value = v;
                break;
            }
            case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT:
            {
                std::uint32_t v = 0;
                std::memcpy(&v, buffer.data.data() + offset, sizeof(v));
                value = v;
                break;
            }
            default:
                return false;
        }
        outIndices->push_back(value);
    }
    return true;
}

bool ReadAccessorVec3FloatTiny(
    const tinygltf::Model& model,
    const tinygltf::Accessor& accessor,
    std::vector<glm::vec3>* outValues
)
{
    if (outValues == nullptr || accessor.bufferView < 0 || accessor.bufferView >= static_cast<int>(model.bufferViews.size()))
    {
        return false;
    }
    if (accessor.type != TINYGLTF_TYPE_VEC3 || accessor.componentType != TINYGLTF_COMPONENT_TYPE_FLOAT)
    {
        return false;
    }
    const tinygltf::BufferView& view = model.bufferViews[static_cast<std::size_t>(accessor.bufferView)];
    if (view.buffer < 0 || view.buffer >= static_cast<int>(model.buffers.size()))
    {
        return false;
    }
    const tinygltf::Buffer& buffer = model.buffers[static_cast<std::size_t>(view.buffer)];

    const std::size_t elementSize = sizeof(float) * 3U;
    const std::size_t stride = accessor.ByteStride(view) > 0 ? static_cast<std::size_t>(accessor.ByteStride(view)) : elementSize;
    const std::size_t baseOffset = static_cast<std::size_t>(view.byteOffset + accessor.byteOffset);
    if (baseOffset >= buffer.data.size())
    {
        return false;
    }

    outValues->clear();
    outValues->reserve(accessor.count);
    for (std::size_t i = 0; i < accessor.count; ++i)
    {
        const std::size_t offset = baseOffset + i * stride;
        if (offset + elementSize > buffer.data.size())
        {
            return false;
        }
        glm::vec3 value{0.0F};
        std::memcpy(&value.x, buffer.data.data() + offset, sizeof(float));
        std::memcpy(&value.y, buffer.data.data() + offset + sizeof(float), sizeof(float));
        std::memcpy(&value.z, buffer.data.data() + offset + sizeof(float) * 2U, sizeof(float));
        outValues->push_back(value);
    }
    return true;
}

bool ReadAccessorVec2FloatTiny(
    const tinygltf::Model& model,
    const tinygltf::Accessor& accessor,
    std::vector<glm::vec2>* outValues
)
{
    if (outValues == nullptr || accessor.bufferView < 0 || accessor.bufferView >= static_cast<int>(model.bufferViews.size()))
    {
        return false;
    }
    if (accessor.type != TINYGLTF_TYPE_VEC2 || accessor.componentType != TINYGLTF_COMPONENT_TYPE_FLOAT)
    {
        return false;
    }
    const tinygltf::BufferView& view = model.bufferViews[static_cast<std::size_t>(accessor.bufferView)];
    if (view.buffer < 0 || view.buffer >= static_cast<int>(model.buffers.size()))
    {
        return false;
    }
    const tinygltf::Buffer& buffer = model.buffers[static_cast<std::size_t>(view.buffer)];

    const std::size_t elementSize = sizeof(float) * 2U;
    const std::size_t stride = accessor.ByteStride(view) > 0 ? static_cast<std::size_t>(accessor.ByteStride(view)) : elementSize;
    const std::size_t baseOffset = static_cast<std::size_t>(view.byteOffset + accessor.byteOffset);
    if (baseOffset >= buffer.data.size())
    {
        return false;
    }

    outValues->clear();
    outValues->reserve(accessor.count);
    for (std::size_t i = 0; i < accessor.count; ++i)
    {
        const std::size_t offset = baseOffset + i * stride;
        if (offset + elementSize > buffer.data.size())
        {
            return false;
        }
        glm::vec2 value{0.0F};
        std::memcpy(&value.x, buffer.data.data() + offset, sizeof(float));
        std::memcpy(&value.y, buffer.data.data() + offset + sizeof(float), sizeof(float));
        outValues->push_back(value);
    }
    return true;
}

bool ReadAccessorVec4UIntTiny(
    const tinygltf::Model& model,
    const tinygltf::Accessor& accessor,
    std::vector<glm::uvec4>* outValues
)
{
    if (outValues == nullptr || accessor.bufferView < 0 || accessor.bufferView >= static_cast<int>(model.bufferViews.size()))
    {
        return false;
    }
    if (accessor.type != TINYGLTF_TYPE_VEC4)
    {
        return false;
    }

    const tinygltf::BufferView& view = model.bufferViews[static_cast<std::size_t>(accessor.bufferView)];
    if (view.buffer < 0 || view.buffer >= static_cast<int>(model.buffers.size()))
    {
        return false;
    }
    const tinygltf::Buffer& buffer = model.buffers[static_cast<std::size_t>(view.buffer)];

    const int componentSize = tinygltf::GetComponentSizeInBytes(accessor.componentType);
    if (componentSize <= 0)
    {
        return false;
    }
    const std::size_t stride = accessor.ByteStride(view) > 0 ? static_cast<std::size_t>(accessor.ByteStride(view)) : static_cast<std::size_t>(componentSize * 4);
    const std::size_t baseOffset = static_cast<std::size_t>(view.byteOffset + accessor.byteOffset);
    if (baseOffset >= buffer.data.size())
    {
        return false;
    }

    outValues->clear();
    outValues->reserve(accessor.count);
    for (std::size_t i = 0; i < accessor.count; ++i)
    {
        const std::size_t offset = baseOffset + i * stride;
        if (offset + static_cast<std::size_t>(componentSize * 4) > buffer.data.size())
        {
            return false;
        }
        glm::uvec4 value{0U};
        for (int c = 0; c < 4; ++c)
        {
            const std::size_t at = offset + static_cast<std::size_t>(c * componentSize);
            switch (accessor.componentType)
            {
                case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
                {
                    std::uint8_t v = 0;
                    std::memcpy(&v, buffer.data.data() + at, sizeof(v));
                    value[static_cast<std::size_t>(c)] = static_cast<std::uint32_t>(v);
                    break;
                }
                case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT:
                {
                    std::uint16_t v = 0;
                    std::memcpy(&v, buffer.data.data() + at, sizeof(v));
                    value[static_cast<std::size_t>(c)] = static_cast<std::uint32_t>(v);
                    break;
                }
                case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT:
                {
                    std::uint32_t v = 0;
                    std::memcpy(&v, buffer.data.data() + at, sizeof(v));
                    value[static_cast<std::size_t>(c)] = v;
                    break;
                }
                default:
                    return false;
            }
        }
        outValues->push_back(value);
    }
    return true;
}

float ReadComponentAsFloatTiny(const std::uint8_t* src, int componentType, bool normalized)
{
    switch (componentType)
    {
        case TINYGLTF_COMPONENT_TYPE_FLOAT:
        {
            float v = 0.0F;
            std::memcpy(&v, src, sizeof(v));
            return v;
        }
        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
        {
            std::uint8_t v = 0;
            std::memcpy(&v, src, sizeof(v));
            return normalized ? static_cast<float>(v) / 255.0F : static_cast<float>(v);
        }
        case TINYGLTF_COMPONENT_TYPE_BYTE:
        {
            std::int8_t v = 0;
            std::memcpy(&v, src, sizeof(v));
            if (!normalized)
            {
                return static_cast<float>(v);
            }
            return glm::max(-1.0F, static_cast<float>(v) / 127.0F);
        }
        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT:
        {
            std::uint16_t v = 0;
            std::memcpy(&v, src, sizeof(v));
            return normalized ? static_cast<float>(v) / 65535.0F : static_cast<float>(v);
        }
        case TINYGLTF_COMPONENT_TYPE_SHORT:
        {
            std::int16_t v = 0;
            std::memcpy(&v, src, sizeof(v));
            if (!normalized)
            {
                return static_cast<float>(v);
            }
            return glm::max(-1.0F, static_cast<float>(v) / 32767.0F);
        }
        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT:
        {
            std::uint32_t v = 0;
            std::memcpy(&v, src, sizeof(v));
            if (!normalized)
            {
                return static_cast<float>(v);
            }
            return static_cast<float>(v) / 4294967295.0F;
        }
        default:
            return 0.0F;
    }
}

bool ReadAccessorVec4FloatTiny(
    const tinygltf::Model& model,
    const tinygltf::Accessor& accessor,
    std::vector<glm::vec4>* outValues
)
{
    if (outValues == nullptr || accessor.bufferView < 0 || accessor.bufferView >= static_cast<int>(model.bufferViews.size()))
    {
        return false;
    }
    if (accessor.type != TINYGLTF_TYPE_VEC4)
    {
        return false;
    }
    const tinygltf::BufferView& view = model.bufferViews[static_cast<std::size_t>(accessor.bufferView)];
    if (view.buffer < 0 || view.buffer >= static_cast<int>(model.buffers.size()))
    {
        return false;
    }
    const tinygltf::Buffer& buffer = model.buffers[static_cast<std::size_t>(view.buffer)];
    const int componentSize = tinygltf::GetComponentSizeInBytes(accessor.componentType);
    if (componentSize <= 0)
    {
        return false;
    }
    const std::size_t stride = accessor.ByteStride(view) > 0 ? static_cast<std::size_t>(accessor.ByteStride(view)) : static_cast<std::size_t>(componentSize * 4);
    const std::size_t baseOffset = static_cast<std::size_t>(view.byteOffset + accessor.byteOffset);
    if (baseOffset >= buffer.data.size())
    {
        return false;
    }

    outValues->clear();
    outValues->reserve(accessor.count);
    for (std::size_t i = 0; i < accessor.count; ++i)
    {
        const std::size_t offset = baseOffset + i * stride;
        if (offset + static_cast<std::size_t>(componentSize * 4) > buffer.data.size())
        {
            return false;
        }
        glm::vec4 value{0.0F};
        for (int c = 0; c < 4; ++c)
        {
            const std::size_t at = offset + static_cast<std::size_t>(c * componentSize);
            value[static_cast<std::size_t>(c)] = ReadComponentAsFloatTiny(buffer.data.data() + at, accessor.componentType, accessor.normalized);
        }
        outValues->push_back(value);
    }
    return true;
}

bool ReadAccessorMat4FloatTiny(
    const tinygltf::Model& model,
    const tinygltf::Accessor& accessor,
    std::vector<glm::mat4>* outValues
)
{
    if (outValues == nullptr || accessor.bufferView < 0 || accessor.bufferView >= static_cast<int>(model.bufferViews.size()))
    {
        return false;
    }
    if (accessor.type != TINYGLTF_TYPE_MAT4 || accessor.componentType != TINYGLTF_COMPONENT_TYPE_FLOAT)
    {
        return false;
    }
    const tinygltf::BufferView& view = model.bufferViews[static_cast<std::size_t>(accessor.bufferView)];
    if (view.buffer < 0 || view.buffer >= static_cast<int>(model.buffers.size()))
    {
        return false;
    }
    const tinygltf::Buffer& buffer = model.buffers[static_cast<std::size_t>(view.buffer)];
    const std::size_t elementSize = sizeof(float) * 16U;
    const std::size_t stride = accessor.ByteStride(view) > 0 ? static_cast<std::size_t>(accessor.ByteStride(view)) : elementSize;
    const std::size_t baseOffset = static_cast<std::size_t>(view.byteOffset + accessor.byteOffset);
    if (baseOffset >= buffer.data.size())
    {
        return false;
    }

    outValues->clear();
    outValues->reserve(accessor.count);
    for (std::size_t i = 0; i < accessor.count; ++i)
    {
        const std::size_t offset = baseOffset + i * stride;
        if (offset + elementSize > buffer.data.size())
        {
            return false;
        }
        glm::mat4 mat{1.0F};
        for (int c = 0; c < 4; ++c)
        {
            for (int r = 0; r < 4; ++r)
            {
                float v = 0.0F;
                std::memcpy(&v, buffer.data.data() + offset + static_cast<std::size_t>((c * 4 + r) * sizeof(float)), sizeof(float));
                mat[c][r] = v;
            }
        }
        outValues->push_back(mat);
    }
    return true;
}

// High-poly mesh generation helpers for GPU stress testing
engine::render::MeshGeometry GenerateIcoSphere(int subdivisions)
{
    // Base icosahedron vertices
    const float t = (1.0F + std::sqrt(5.0F)) / 2.0F;
    std::vector<glm::vec3> vertices = {
        {-1, t, 0}, {1, t, 0}, {-1, -t, 0}, {1, -t, 0},
        {0, -1, t}, {0, 1, t}, {0, -1, -t}, {0, 1, -t},
        {t, 0, -1}, {t, 0, 1}, {-t, 0, -1}, {-t, 0, 1}
    };
    for (auto& v : vertices) v = glm::normalize(v);
    
    // Base faces (indices)
    std::vector<std::uint32_t> indices = {
        0, 11, 5, 0, 5, 1, 0, 1, 7, 0, 7, 10, 0, 10, 11,
        1, 5, 9, 5, 11, 4, 11, 10, 2, 10, 7, 6, 7, 1, 8,
        3, 9, 4, 3, 4, 2, 3, 2, 6, 3, 6, 8, 3, 8, 9,
        4, 9, 5, 2, 4, 11, 6, 2, 10, 8, 6, 7, 9, 8, 1
    };
    
    // Subdivide
    for (int sub = 0; sub < subdivisions; ++sub)
    {
        std::vector<std::uint32_t> newIndices;
        std::unordered_map<std::uint64_t, std::uint32_t> midpointCache;
        
        auto getMidpoint = [&](std::uint32_t a, std::uint32_t b) -> std::uint32_t {
            const std::uint64_t key = (static_cast<std::uint64_t>(std::min(a, b)) << 32) | std::max(a, b);
            auto it = midpointCache.find(key);
            if (it != midpointCache.end()) return it->second;
            
            glm::vec3 mid = glm::normalize((vertices[a] + vertices[b]) * 0.5F);
            const std::uint32_t idx = static_cast<std::uint32_t>(vertices.size());
            vertices.push_back(mid);
            midpointCache[key] = idx;
            return idx;
        };
        
        for (size_t i = 0; i < indices.size(); i += 3)
        {
            const std::uint32_t a = indices[i], b = indices[i+1], c = indices[i+2];
            const std::uint32_t ab = getMidpoint(a, b);
            const std::uint32_t bc = getMidpoint(b, c);
            const std::uint32_t ca = getMidpoint(c, a);
            
            newIndices.insert(newIndices.end(), {a, ab, ca, b, bc, ab, c, ca, bc, ab, bc, ca});
        }
        indices = std::move(newIndices);
    }
    
    engine::render::MeshGeometry mesh;
    mesh.positions = std::move(vertices);
    mesh.indices = std::move(indices);
    
    // Compute normals (same as positions for unit sphere)
    mesh.normals = mesh.positions;
    
    return mesh;
}

engine::render::MeshGeometry GenerateTorus(float majorRadius, float minorRadius, int majorSegments, int minorSegments)
{
    engine::render::MeshGeometry mesh;
    
    for (int i = 0; i <= majorSegments; ++i)
    {
        const float theta = static_cast<float>(i) / static_cast<float>(majorSegments) * 2.0F * kPi;
        const float cosTheta = std::cos(theta);
        const float sinTheta = std::sin(theta);
        
        for (int j = 0; j <= minorSegments; ++j)
        {
            const float phi = static_cast<float>(j) / static_cast<float>(minorSegments) * 2.0F * kPi;
            const float cosPhi = std::cos(phi);
            const float sinPhi = std::sin(phi);
            
            const float x = (majorRadius + minorRadius * cosPhi) * cosTheta;
            const float y = minorRadius * sinPhi;
            const float z = (majorRadius + minorRadius * cosPhi) * sinTheta;
            
            mesh.positions.push_back({x, y, z});
            
            // Normal
            const float nx = cosPhi * cosTheta;
            const float ny = sinPhi;
            const float nz = cosPhi * sinTheta;
            mesh.normals.push_back(glm::normalize(glm::vec3{nx, ny, nz}));
        }
    }
    
    // Generate indices
    for (int i = 0; i < majorSegments; ++i)
    {
        for (int j = 0; j < minorSegments; ++j)
        {
            const std::uint32_t a = static_cast<std::uint32_t>(i * (minorSegments + 1) + j);
            const std::uint32_t b = static_cast<std::uint32_t>(a + minorSegments + 1);
            const std::uint32_t c = static_cast<std::uint32_t>(a + 1);
            const std::uint32_t d = static_cast<std::uint32_t>(b + 1);
            
            mesh.indices.insert(mesh.indices.end(), {a, b, c, b, d, c});
        }
    }
    
    return mesh;
}

engine::render::MeshGeometry GenerateGridPlane(int xDivisions, int zDivisions)
{
    engine::render::MeshGeometry mesh;
    
    const float halfX = static_cast<float>(xDivisions) * 0.5F;
    const float halfZ = static_cast<float>(zDivisions) * 0.5F;
    const float stepX = 1.0F;
    const float stepZ = 1.0F;
    
    for (int z = 0; z <= zDivisions; ++z)
    {
        for (int x = 0; x <= xDivisions; ++x)
        {
            mesh.positions.push_back({
                static_cast<float>(x) * stepX - halfX,
                0.0F,
                static_cast<float>(z) * stepZ - halfZ
            });
            mesh.normals.push_back({0.0F, 1.0F, 0.0F});
        }
    }
    
    for (int z = 0; z < zDivisions; ++z)
    {
        for (int x = 0; x < xDivisions; ++x)
        {
            const std::uint32_t a = static_cast<std::uint32_t>(z * (xDivisions + 1) + x);
            const std::uint32_t b = static_cast<std::uint32_t>(a + xDivisions + 1);
            const std::uint32_t c = static_cast<std::uint32_t>(a + 1);
            const std::uint32_t d = static_cast<std::uint32_t>(b + 1);
            
            mesh.indices.insert(mesh.indices.end(), {a, c, b, c, d, b});
        }
    }
    
    return mesh;
}

engine::render::MeshGeometry GenerateSpiralStair(int stepCount, int segmentsPerStep)
{
    engine::render::MeshGeometry mesh;
    
    const float heightPerStep = 0.2F;
    const float radius = 1.0F;
    const float innerRadius = 0.3F;
    const float anglePerStep = 2.0F * kPi / 32.0F;
    
    for (int step = 0; step < stepCount; ++step)
    {
        const float baseAngle = static_cast<float>(step) * anglePerStep;
        const float y = static_cast<float>(step) * heightPerStep;
        
        for (int seg = 0; seg <= segmentsPerStep; ++seg)
        {
            const float t = static_cast<float>(seg) / static_cast<float>(segmentsPerStep);
            const float angle = baseAngle + t * anglePerStep;
            
            // Outer vertex
            mesh.positions.push_back({
                std::cos(angle) * radius,
                y,
                std::sin(angle) * radius
            });
            mesh.normals.push_back({std::cos(angle), 0.0F, std::sin(angle)});
            
            // Inner vertex
            mesh.positions.push_back({
                std::cos(angle) * innerRadius,
                y,
                std::sin(angle) * innerRadius
            });
            mesh.normals.push_back({-std::cos(angle), 0.0F, -std::sin(angle)});
            
            // Top edge
            mesh.positions.push_back({
                std::cos(angle) * radius,
                y + 0.05F,
                std::sin(angle) * radius
            });
            mesh.normals.push_back({0.0F, 1.0F, 0.0F});
        }
        
        // Generate indices for this step
        const std::uint32_t baseIdx = static_cast<std::uint32_t>(step * (segmentsPerStep + 1) * 3);
        for (int seg = 0; seg < segmentsPerStep; ++seg)
        {
            const std::uint32_t o0 = baseIdx + static_cast<std::uint32_t>(seg * 3);
            const std::uint32_t o1 = o0 + 3;
            const std::uint32_t i0 = o0 + 1;
            const std::uint32_t i1 = i0 + 3;
            const std::uint32_t t0 = o0 + 2;
            const std::uint32_t t1 = t0 + 3;
            
            // Tread top
            mesh.indices.insert(mesh.indices.end(), {o0, i0, o1, i0, i1, o1});
            // Riser
            mesh.indices.insert(mesh.indices.end(), {o0, o1, t1, o0, t1, t0});
        }
    }
    
    return mesh;
}

glm::vec3 ComputeMeshBounds(const engine::render::MeshGeometry& mesh)
{
    if (mesh.positions.empty()) return glm::vec3{1.0F};
    
    glm::vec3 minPos = mesh.positions[0];
    glm::vec3 maxPos = mesh.positions[0];
    
    for (const auto& p : mesh.positions)
    {
        minPos = glm::min(minPos, p);
        maxPos = glm::max(maxPos, p);
    }
    
    return (maxPos - minPos) * 0.5F;
}

} // namespace

const char* GameplaySystems::CameraModeToName(CameraMode mode)
{
    return mode == CameraMode::ThirdPerson ? "3rd Person" : "1st Person";
}

GameplaySystems::GameplaySystems()
    : m_rng(std::random_device{}())
{
}

void GameplaySystems::Initialize(engine::core::EventBus& eventBus)
{
    m_eventBus = &eventBus;
    m_fxSystem.Initialize("assets/fx");
    m_fxSystem.SetSpawnCallback([this](const engine::fx::FxSpawnEvent& event) {
        if (m_fxReplicationCallback)
        {
            m_fxReplicationCallback(event);
        }
    });

    m_eventBus->Subscribe("load_map", [this](const engine::core::Event& event) {
        if (!event.args.empty())
        {
            LoadMap(event.args[0]);
        }
    });

    m_eventBus->Subscribe("regen_loops", [this](const engine::core::Event& event) {
        if (event.args.empty())
        {
            RegenerateLoops();
            return;
        }

        try
        {
            RegenerateLoops(static_cast<unsigned int>(std::stoul(event.args[0])));
        }
        catch (...)
        {
            RegenerateLoops();
        }
    });
    m_eventBus->Subscribe("quit", [this](const engine::core::Event&) { RequestQuit(); });

    ApplyGameplayTuning(m_tuning);

    // Set default dev loadout for testing
    m_perkSystem.SetDefaultDevLoadout();

    // Initialize perk system active states
    m_perkSystem.InitializeActiveStates();
    InitializeLoadoutCatalog();
    m_animationSystem.GetStateMachineMut().SetStateChangeCallback([this](engine::animation::LocomotionState from, engine::animation::LocomotionState to) {
        if (!m_animationDebugEnabled)
        {
            return;
        }
        std::cout << "[ANIMATION] State change: "
                  << engine::animation::LocomotionStateToString(from)
                  << " -> "
                  << engine::animation::LocomotionStateToString(to)
                  << "\n";
    });
    m_animationSystem.SetClipLoadedCallback([this](const std::string& clipName) {
        if (!m_animationDebugEnabled)
        {
            return;
        }
        std::cout << "[ANIMATION] Clip registered in animation system: " << clipName << "\n";
    });

    BuildSceneFromMap(MapType::Test, m_generationSeed);
    AddRuntimeMessage("Press ~ for Console", 4.0F);
}

void GameplaySystems::CaptureInputFrame(
    const engine::platform::Input& input,
    const engine::platform::ActionBindings& bindings,
    bool controlsEnabled
)
{
    const engine::scene::Role localRole = ControlledSceneRole();
    const engine::scene::Role remoteRole = (localRole == engine::scene::Role::Survivor) ? engine::scene::Role::Killer : engine::scene::Role::Survivor;

    auto updateCommandForRole = [&](engine::scene::Role role, RoleCommand& command) {
        const engine::scene::Entity entity = (role == engine::scene::Role::Survivor) ? m_survivor : m_killer;
        const auto actorIt = m_world.Actors().find(entity);
        const bool actorExists = actorIt != m_world.Actors().end();

        bool inputLocked = !actorExists || !controlsEnabled;
        if (actorExists && IsActorInputLocked(actorIt->second))
        {
            inputLocked = true;
        }
        if (role == engine::scene::Role::Survivor &&
            (m_survivorState == SurvivorHealthState::Hooked ||
             m_survivorState == SurvivorHealthState::Trapped ||
             m_survivorState == SurvivorHealthState::Dead))
        {
            inputLocked = true;
        }

        if (inputLocked)
        {
            command.moveAxis = glm::vec2{0.0F};
            command.sprinting = false;
            command.crouchHeld = false;
            command.interactHeld = false;
            command.attackHeld = false;
            command.lungeHeld = false;
            command.useAltHeld = false;
            if (role == engine::scene::Role::Survivor &&
                (m_survivorState == SurvivorHealthState::Hooked || m_survivorState == SurvivorHealthState::Trapped) &&
                controlsEnabled)
            {
                const glm::vec2 mouseDelta = input.MouseDelta();
                command.lookDelta += glm::vec2{mouseDelta.x, m_invertLookY ? -mouseDelta.y : mouseDelta.y};
            }
            if (role == engine::scene::Role::Survivor && m_survivorState == SurvivorHealthState::Hooked && controlsEnabled)
            {
                command.interactPressed =
                    command.interactPressed || bindings.IsPressed(input, engine::platform::InputAction::Interact);
                command.jumpPressed = command.jumpPressed || input.IsKeyPressed(GLFW_KEY_SPACE);
            }
            if (role == engine::scene::Role::Survivor && m_survivorState == SurvivorHealthState::Trapped && controlsEnabled)
            {
                command.interactPressed =
                    command.interactPressed || bindings.IsPressed(input, engine::platform::InputAction::Interact);
            }
            if (role == engine::scene::Role::Survivor && m_survivorState == SurvivorHealthState::Carried && controlsEnabled)
            {
                command.wiggleLeftPressed =
                    command.wiggleLeftPressed || bindings.IsPressed(input, engine::platform::InputAction::MoveLeft);
                command.wiggleRightPressed =
                    command.wiggleRightPressed || bindings.IsPressed(input, engine::platform::InputAction::MoveRight);
            }
            return;
        }

        command.moveAxis = ReadMoveAxis(input, bindings);
        command.sprinting = role == engine::scene::Role::Survivor && bindings.IsDown(input, engine::platform::InputAction::Sprint);
        command.crouchHeld = bindings.IsDown(input, engine::platform::InputAction::Crouch);
        command.interactHeld = bindings.IsDown(input, engine::platform::InputAction::Interact);
        command.attackHeld = bindings.IsDown(input, engine::platform::InputAction::AttackShort) ||
                             bindings.IsDown(input, engine::platform::InputAction::AttackLunge);
        command.lungeHeld = bindings.IsDown(input, engine::platform::InputAction::AttackLunge);
        command.useAltHeld = input.IsMouseDown(GLFW_MOUSE_BUTTON_RIGHT);
        const glm::vec2 mouseDelta = input.MouseDelta();
        command.lookDelta += glm::vec2{mouseDelta.x, m_invertLookY ? -mouseDelta.y : mouseDelta.y};

        command.interactPressed = command.interactPressed || bindings.IsPressed(input, engine::platform::InputAction::Interact);
        command.jumpPressed = command.jumpPressed || input.IsKeyPressed(GLFW_KEY_SPACE);
        command.attackPressed = command.attackPressed || bindings.IsPressed(input, engine::platform::InputAction::AttackShort);
        command.attackReleased = command.attackReleased ||
                                 bindings.IsReleased(input, engine::platform::InputAction::AttackShort) ||
                                 bindings.IsReleased(input, engine::platform::InputAction::AttackLunge);
        command.useAltPressed = command.useAltPressed || input.IsMousePressed(GLFW_MOUSE_BUTTON_RIGHT);
        command.useAltReleased = command.useAltReleased || input.IsMouseReleased(GLFW_MOUSE_BUTTON_RIGHT);

        if (role == engine::scene::Role::Survivor)
        {
            command.dropItemPressed = command.dropItemPressed || input.IsKeyPressed(GLFW_KEY_R);
            command.pickupItemPressed = command.pickupItemPressed || input.IsMousePressed(GLFW_MOUSE_BUTTON_LEFT);
            command.wiggleLeftPressed = command.wiggleLeftPressed || bindings.IsPressed(input, engine::platform::InputAction::MoveLeft);
            command.wiggleRightPressed = command.wiggleRightPressed || bindings.IsPressed(input, engine::platform::InputAction::MoveRight);
        }
    };

    if (localRole == engine::scene::Role::Survivor)
    {
        updateCommandForRole(engine::scene::Role::Survivor, m_localSurvivorCommand);
        m_localKillerCommand = RoleCommand{};
    }
    else
    {
        updateCommandForRole(engine::scene::Role::Killer, m_localKillerCommand);
        m_localSurvivorCommand = RoleCommand{};
    }

    if (!m_networkAuthorityMode)
    {
        if (remoteRole == engine::scene::Role::Survivor)
        {
            m_remoteSurvivorCommand.reset();
        }
        else
        {
            m_remoteKillerCommand.reset();
        }
    }
}

void GameplaySystems::FixedUpdate(float fixedDt, const engine::platform::Input& input, bool controlsEnabled)
{
    (void)input;
    (void)controlsEnabled;

    // Rebuild physics only when world geometry changed (pallet drop/break, trap placement, etc.).
    // For the killer chase trigger (which moves every tick), update its position in-place.
    if (m_physicsDirty)
    {
        RebuildPhysicsWorld();
        m_physicsDirty = false;
    }
    else if (m_killer != 0)
    {
        const auto kIt = m_world.Transforms().find(m_killer);
        if (kIt != m_world.Transforms().end())
        {
            m_physics.UpdateTriggerCenter(m_killer, kIt->second.position);
        }
    }

    // Update status effects (tick timers, remove expired)
    m_statusEffectManager.Update(fixedDt);

    RoleCommand survivorCommand = m_localSurvivorCommand;
    RoleCommand killerCommand = m_localKillerCommand;

    if (m_networkAuthorityMode)
    {
        if (m_controlledRole == ControlledRole::Survivor)
        {
            if (m_remoteKillerCommand.has_value())
            {
                killerCommand = *m_remoteKillerCommand;
            }
        }
        else
        {
            if (m_remoteSurvivorCommand.has_value())
            {
                survivorCommand = *m_remoteSurvivorCommand;
            }
        }
    }
    else
    {
        if (m_controlledRole == ControlledRole::Survivor)
        {
            killerCommand = RoleCommand{};
        }
        else
        {
            survivorCommand = RoleCommand{};
        }
    }

    if (m_survivorHitHasteTimer > 0.0F)
    {
        m_survivorHitHasteTimer = std::max(0.0F, m_survivorHitHasteTimer - fixedDt);
    }
    if (m_killerSlowTimer > 0.0F)
    {
        m_killerSlowTimer = std::max(0.0F, m_killerSlowTimer - fixedDt);
        if (m_killerSlowTimer <= 0.0F)
        {
            m_killerSlowMultiplier = 1.0F;
        }
    }
    if (m_killerSurvivorNoCollisionTimer > 0.0F)
    {
        bool overlapping = false;
        bool havePair = false;
        float distanceSq = 0.0F;
        const auto killerTransformIt = m_world.Transforms().find(m_killer);
        const auto survivorTransformIt = m_world.Transforms().find(m_survivor);
        const auto killerActorIt = m_world.Actors().find(m_killer);
        const auto survivorActorIt = m_world.Actors().find(m_survivor);
        if (killerTransformIt != m_world.Transforms().end() &&
            survivorTransformIt != m_world.Transforms().end() &&
            killerActorIt != m_world.Actors().end() &&
            survivorActorIt != m_world.Actors().end())
        {
            havePair = true;
            const float combinedRadius = std::max(
                0.01F,
                killerActorIt->second.capsuleRadius + survivorActorIt->second.capsuleRadius
            );
            const glm::vec2 delta{
                survivorTransformIt->second.position.x - killerTransformIt->second.position.x,
                survivorTransformIt->second.position.z - killerTransformIt->second.position.z
            };
            distanceSq = glm::dot(delta, delta);
            overlapping = distanceSq < (combinedRadius * combinedRadius);
        }

        if (havePair &&
            distanceSq >= (m_killerSurvivorNoCollisionBreakDistance * m_killerSurvivorNoCollisionBreakDistance))
        {
            // End hit ghost immediately once actors have clearly separated.
            m_killerSurvivorNoCollisionTimer = 0.0F;
        }
        else if (!overlapping)
        {
            m_killerSurvivorNoCollisionTimer = std::max(0.0F, m_killerSurvivorNoCollisionTimer - fixedDt);
        }
        // If overlapping, freeze timer (do not reduce).
    }

    m_killerPreMovePositionValid = false;
    m_survivorPreMovePositionValid = false;
    if (const auto killerTransformIt = m_world.Transforms().find(m_killer);
        killerTransformIt != m_world.Transforms().end())
    {
        m_killerPreMovePosition = killerTransformIt->second.position;
        m_killerPreMovePositionValid = true;
    }
    if (const auto survivorTransformIt = m_world.Transforms().find(m_survivor);
        survivorTransformIt != m_world.Transforms().end())
    {
        m_survivorPreMovePosition = survivorTransformIt->second.position;
        m_survivorPreMovePositionValid = true;
    }

    for (auto& [entity, actor] : m_world.Actors())
    {
        const engine::scene::Role role = actor.role;
        const RoleCommand& command = role == engine::scene::Role::Survivor ? survivorCommand : killerCommand;

        bool inputLocked = IsActorInputLocked(actor);
        if (entity == m_survivor &&
            (m_survivorState == SurvivorHealthState::Hooked ||
             m_survivorState == SurvivorHealthState::Trapped ||
             m_survivorState == SurvivorHealthState::Dead))
        {
            inputLocked = true;
        }

        const bool allowLookWhileLocked =
            entity == m_survivor &&
            (m_survivorState == SurvivorHealthState::Hooked || m_survivorState == SurvivorHealthState::Trapped);
        if ((!inputLocked || allowLookWhileLocked) && glm::length(command.lookDelta) > 1.0e-5F)
        {
            float sensitivity = role == engine::scene::Role::Survivor ? m_survivorLookSensitivity : m_killerLookSensitivity;

            // Apply chainsaw sprint turn rate restriction when sprinting
            if (role == engine::scene::Role::Killer &&
                m_killerPowerState.chainsawState == ChainsawSprintState::Sprinting)
            {
                // Get base turn rate based on boost window
                float turnRateDegPerSec = m_killerPowerState.chainsawInTurnBoostWindow
                    ? m_chainsawConfig.turnBoostRate      // 120 deg/sec during boost
                    : m_chainsawConfig.turnRestrictedRate; // 25 deg/sec after boost

                // Apply overheat turn bonus if buffed
                const bool overheatBuffed = m_killerPowerState.chainsawOverheat >= m_chainsawConfig.overheatBuffThreshold;
                if (overheatBuffed)
                {
                    turnRateDegPerSec *= (1.0F + m_chainsawConfig.overheatTurnBonus);
                }

                // Calculate max yaw change per frame (in radians)
                const float maxYawChangeRadians = glm::radians(turnRateDegPerSec) * fixedDt;

                // Calculate requested yaw change with normal sensitivity
                const float requestedYawChange = command.lookDelta.x * m_killerLookSensitivity;

                // Clamp the yaw change to the max allowed per frame
                const float clampedYawChange = glm::clamp(requestedYawChange, -maxYawChangeRadians, maxYawChangeRadians);

                // Apply directly to transform (bypassing UpdateActorLook for yaw)
                // NOTE: Pitch is NOT modified during chainsaw sprint - vertical camera is locked
                auto transformIt = m_world.Transforms().find(entity);
                if (transformIt != m_world.Transforms().end())
                {
                    engine::scene::Transform& transform = transformIt->second;
                    transform.rotationEuler.y += clampedYawChange;
                    // Pitch (vertical look) is locked during chainsaw sprint - do not modify rotationEuler.x
                    // Recalculate forward from yaw only (pitch stays at current value)
                    transform.forward = ForwardFromYawPitch(transform.rotationEuler.y, transform.rotationEuler.x);
                }
            }
            else
            {
                UpdateActorLook(entity, command.lookDelta, sensitivity);
            }
        }

        const bool survivorActionLocked =
            role == engine::scene::Role::Survivor &&
            m_survivorItemState.actionLockTimer > 0.0F &&
            m_survivorState != SurvivorHealthState::Trapped &&
            m_survivorState != SurvivorHealthState::Hooked &&
            m_survivorState != SurvivorHealthState::Carried;

        const glm::vec2 axis = (inputLocked || survivorActionLocked) ? glm::vec2{0.0F} : command.moveAxis;
        const bool sprinting = (inputLocked || survivorActionLocked) ? false : command.sprinting;
        const bool jumpPressed = (inputLocked || survivorActionLocked) ? false : command.jumpPressed;

        UpdateActorMovement(entity, axis, sprinting, jumpPressed, survivorActionLocked ? false : command.crouchHeld, fixedDt);

        UpdateInteractBuffer(role, command, fixedDt);

        if (role == engine::scene::Role::Survivor)
        {
            if (m_survivorState == SurvivorHealthState::Carried && command.wiggleLeftPressed)
            {
                m_survivorWigglePressQueue.push_back(-1);
            }
            if (m_survivorState == SurvivorHealthState::Carried && command.wiggleRightPressed)
            {
                m_survivorWigglePressQueue.push_back(1);
            }
        }
    }

    UpdateCarriedSurvivor();
    ResolveKillerSurvivorCollision();
    UpdateCarryEscapeQte(true, fixedDt);
    UpdateHookStages(fixedDt, survivorCommand.interactPressed, survivorCommand.jumpPressed);
    const bool toolboxRepairHeld = survivorCommand.useAltHeld && m_survivorLoadout.itemId == "toolbox";
    UpdateGeneratorRepair(survivorCommand.interactHeld || toolboxRepairHeld, survivorCommand.jumpPressed, fixedDt);
    UpdateSelfHeal(survivorCommand.interactHeld, survivorCommand.jumpPressed, fixedDt);
    UpdateSurvivorItemSystem(survivorCommand, fixedDt);
    UpdateKillerPowerSystem(killerCommand, fixedDt);
    UpdateBearTrapSystem(survivorCommand, killerCommand, fixedDt);
    UpdateProjectiles(fixedDt);

    const InteractionCandidate survivorCandidate = ResolveInteractionCandidateFromView(m_survivor);
    if (survivorCandidate.type != InteractionType::None && ConsumeInteractBuffered(engine::scene::Role::Survivor))
    {
        ExecuteInteractionForRole(m_survivor, survivorCandidate);
        m_physicsDirty = true;
    }
    const InteractionCandidate killerCandidate = ResolveInteractionCandidateFromView(m_killer);
    if (killerCandidate.type != InteractionType::None && ConsumeInteractBuffered(engine::scene::Role::Killer))
    {
        ExecuteInteractionForRole(m_killer, killerCandidate);
        m_physicsDirty = true;
    }

    UpdateKillerAttack(killerCommand, fixedDt);

    UpdatePalletBreak(fixedDt);

    if (m_physicsDirty)
    {
        RebuildPhysicsWorld();
        m_physicsDirty = false;
        // Physics changed — re-resolve interaction candidate for prompt display.
        UpdateInteractionCandidate();
    }
    else
    {
        // Physics unchanged — reuse already-resolved candidate for prompt display.
        const engine::scene::Entity controlled = ControlledEntity();
        const auto actorIt = m_world.Actors().find(controlled);
        const bool inputLocked = (controlled == 0 || actorIt == m_world.Actors().end() || IsActorInputLocked(actorIt->second));
        const bool downed = (controlled == m_survivor &&
            (m_survivorState == SurvivorHealthState::Downed ||
             m_survivorState == SurvivorHealthState::Trapped ||
             m_survivorState == SurvivorHealthState::Hooked ||
             m_survivorState == SurvivorHealthState::Dead));

        if (inputLocked || downed)
        {
            m_interactionCandidate = InteractionCandidate{};
            m_interactionPromptHoldSeconds = 0.0F;
        }
        else
        {
            const InteractionCandidate& resolved = (controlled == m_survivor) ? survivorCandidate : killerCandidate;
            if (resolved.type != InteractionType::None)
            {
                m_interactionCandidate = resolved;
                m_interactionPromptHoldSeconds = 0.2F;
            }
            else if (m_interactionPromptHoldSeconds > 0.0F && !m_interactionCandidate.prompt.empty())
            {
                m_interactionPromptHoldSeconds = std::max(0.0F, m_interactionPromptHoldSeconds - (1.0F / 60.0F));
            }
            else
            {
                m_interactionCandidate = InteractionCandidate{};
                m_interactionPromptHoldSeconds = 0.0F;
            }
        }
    }
    UpdateChaseState(fixedDt);
    UpdateBloodlust(fixedDt);

    const auto survivorTransformIt = m_world.Transforms().find(m_survivor);
    if (survivorTransformIt != m_world.Transforms().end())
    {
        const glm::vec3 survivorPos = survivorTransformIt->second.position;
        const glm::vec3 survivorForward = survivorTransformIt->second.forward;
        const auto survivorActorIt = m_world.Actors().find(m_survivor);

        bool survivorSprinting = false;
        bool survivorMoving = false;

        if (survivorActorIt != m_world.Actors().end())
        {
            const RoleCommand* command = nullptr;
            if (m_controlledRole == ControlledRole::Survivor)
            {
                command = &m_localSurvivorCommand;
            }
            else if (m_remoteSurvivorCommand.has_value())
            {
                command = &m_remoteSurvivorCommand.value();
            }

            if (command != nullptr)
            {
                survivorSprinting = command->sprinting;
                survivorMoving = glm::length(command->moveAxis) > 0.1F;
            }
        }

        const bool survivorInjuredOrDowned = (m_survivorState == SurvivorHealthState::Injured ||
                                               m_survivorState == SurvivorHealthState::Downed);

        UpdateScratchMarks(fixedDt, survivorPos, survivorForward, survivorSprinting);
        UpdateBloodPools(fixedDt, survivorPos, survivorInjuredOrDowned, survivorMoving);
    }

    m_localSurvivorCommand.lookDelta = glm::vec2{0.0F};
    m_localSurvivorCommand.interactPressed = false;
    m_localSurvivorCommand.jumpPressed = false;
    m_localSurvivorCommand.attackPressed = false;
    m_localSurvivorCommand.attackReleased = false;
    m_localSurvivorCommand.useAltPressed = false;
    m_localSurvivorCommand.useAltReleased = false;
    m_localSurvivorCommand.dropItemPressed = false;
    m_localSurvivorCommand.pickupItemPressed = false;
    m_localSurvivorCommand.wiggleLeftPressed = false;
    m_localSurvivorCommand.wiggleRightPressed = false;

    m_localKillerCommand.lookDelta = glm::vec2{0.0F};
    m_localKillerCommand.interactPressed = false;
    m_localKillerCommand.jumpPressed = false;
    m_localKillerCommand.attackPressed = false;
    m_localKillerCommand.attackReleased = false;
    m_localKillerCommand.useAltPressed = false;
    m_localKillerCommand.useAltReleased = false;
    m_localKillerCommand.wiggleLeftPressed = false;
    m_localKillerCommand.wiggleRightPressed = false;

    if (m_remoteSurvivorCommand.has_value())
    {
        m_remoteSurvivorCommand->lookDelta = glm::vec2{0.0F};
        m_remoteSurvivorCommand->interactPressed = false;
        m_remoteSurvivorCommand->jumpPressed = false;
        m_remoteSurvivorCommand->attackPressed = false;
        m_remoteSurvivorCommand->attackReleased = false;
        m_remoteSurvivorCommand->useAltPressed = false;
        m_remoteSurvivorCommand->useAltReleased = false;
        m_remoteSurvivorCommand->dropItemPressed = false;
        m_remoteSurvivorCommand->pickupItemPressed = false;
        m_remoteSurvivorCommand->wiggleLeftPressed = false;
        m_remoteSurvivorCommand->wiggleRightPressed = false;
    }
    if (m_remoteKillerCommand.has_value())
    {
        m_remoteKillerCommand->lookDelta = glm::vec2{0.0F};
        m_remoteKillerCommand->interactPressed = false;
        m_remoteKillerCommand->jumpPressed = false;
        m_remoteKillerCommand->attackPressed = false;
        m_remoteKillerCommand->attackReleased = false;
        m_remoteKillerCommand->useAltPressed = false;
        m_remoteKillerCommand->useAltReleased = false;
        m_remoteKillerCommand->wiggleLeftPressed = false;
        m_remoteKillerCommand->wiggleRightPressed = false;
    }
}

void GameplaySystems::Update(float deltaSeconds, const engine::platform::Input& input, bool controlsEnabled)
{
    (void)input;
    m_elapsedSeconds += deltaSeconds;

    // Update perk system (cooldowns, active durations)
    m_perkSystem.UpdateActiveStates(deltaSeconds);

    for (auto it = m_messages.begin(); it != m_messages.end();)
    {
        it->ttl -= deltaSeconds;
        if (it->ttl <= 0.0F)
        {
            it = m_messages.erase(it);
        }
        else
        {
            ++it;
        }
    }

    m_lastSwingDebugTtl = std::max(0.0F, m_lastSwingDebugTtl - deltaSeconds);
    m_killerAttackFlashTtl = std::max(0.0F, m_killerAttackFlashTtl - deltaSeconds);
    m_trapIndicatorTimer = std::max(0.0F, m_trapIndicatorTimer - deltaSeconds);

    m_fxSystem.Update(deltaSeconds, m_cameraPosition);
    UpdateCamera(deltaSeconds);

    // Update survivor visual facing every frame from look yaw + move input.
    // This keeps model rotation responsive while holding movement keys and rotating camera.
    if (m_survivor != 0)
    {
        const auto survivorTransformIt = m_world.Transforms().find(m_survivor);
        const auto survivorActorIt = m_world.Actors().find(m_survivor);
        if (survivorTransformIt != m_world.Transforms().end() && survivorActorIt != m_world.Actors().end())
        {
            const engine::scene::Transform& survivorTransform = survivorTransformIt->second;
            const engine::scene::ActorComponent& survivorActor = survivorActorIt->second;

            glm::vec2 moveAxis{0.0F};
            if (m_controlledRole == ControlledRole::Survivor && controlsEnabled)
            {
                const bool inputLocked =
                    IsActorInputLocked(survivorActor) ||
                    m_survivorState == SurvivorHealthState::Hooked ||
                    m_survivorState == SurvivorHealthState::Trapped ||
                    m_survivorState == SurvivorHealthState::Dead ||
                    (m_survivorItemState.actionLockTimer > 0.0F &&
                     m_survivorState != SurvivorHealthState::Trapped &&
                     m_survivorState != SurvivorHealthState::Hooked &&
                     m_survivorState != SurvivorHealthState::Carried);
                if (!inputLocked)
                {
                    moveAxis = m_localSurvivorCommand.moveAxis;
                }
            }

            m_survivorVisualMoveInput = moveAxis;
            glm::vec3 desiredDirection{0.0F};
            if (glm::length(moveAxis) > 1.0e-5F && m_controlledRole == ControlledRole::Survivor)
            {
                const glm::vec3 cameraFlat{m_cameraForward.x, 0.0F, m_cameraForward.z};
                if (glm::length(cameraFlat) > 1.0e-5F)
                {
                    const glm::vec3 camForward = glm::normalize(cameraFlat);
                    const glm::vec3 camRight = glm::normalize(glm::cross(camForward, glm::vec3{0.0F, 1.0F, 0.0F}));
                    desiredDirection = glm::normalize(camRight * moveAxis.x + camForward * moveAxis.y);
                }
                else
                {
                    desiredDirection = glm::normalize(glm::vec3{survivorTransform.forward.x, 0.0F, survivorTransform.forward.z});
                }
            }
            else
            {
                const glm::vec3 velocityFlat{survivorActor.velocity.x, 0.0F, survivorActor.velocity.z};
                if (glm::length(velocityFlat) > 0.05F)
                {
                    desiredDirection = glm::normalize(velocityFlat);
                }
            }
            m_survivorVisualDesiredDirection = desiredDirection;

            if (!m_survivorVisualYawInitialized)
            {
                glm::vec3 initialFacing = desiredDirection;
                if (glm::length(initialFacing) <= 1.0e-5F)
                {
                    initialFacing = glm::vec3{survivorTransform.forward.x, 0.0F, survivorTransform.forward.z};
                }
                if (glm::length(initialFacing) <= 1.0e-5F)
                {
                    initialFacing = glm::vec3{0.0F, 0.0F, -1.0F};
                }
                else
                {
                    initialFacing = glm::normalize(initialFacing);
                }
                m_survivorVisualYawRadians = std::atan2(initialFacing.x, -initialFacing.z);
                m_survivorVisualTargetYawRadians = m_survivorVisualYawRadians;
                m_survivorVisualYawInitialized = true;
            }

            if (glm::length(desiredDirection) > 1.0e-5F)
            {
                m_survivorVisualTargetYawRadians = std::atan2(desiredDirection.x, -desiredDirection.z);
            }
            else
            {
                m_survivorVisualTargetYawRadians = m_survivorVisualYawRadians;
            }

            const float delta = WrapAngleRadians(m_survivorVisualTargetYawRadians - m_survivorVisualYawRadians);
            const float maxStep = std::max(0.1F, m_survivorVisualTurnSpeedRadiansPerSecond) * deltaSeconds;
            const float clampedDelta = glm::clamp(delta, -maxStep, maxStep);
            m_survivorVisualYawRadians = WrapAngleRadians(m_survivorVisualYawRadians + clampedDelta);
        }

        // Update animation system based on survivor speed
        if (m_animationSystem.GetStateMachine().IsAutoMode())
        {
            const auto survivorActorIt = m_world.Actors().find(m_survivor);
            if (survivorActorIt != m_world.Actors().end())
            {
                const engine::scene::ActorComponent& survivorActor = survivorActorIt->second;
                const float speed = glm::length(survivorActor.velocity);
                m_animationSystem.Update(deltaSeconds, speed);
            }
        }
        else
        {
            m_animationSystem.Update(deltaSeconds, 0.0F);
        }
    }
}

void GameplaySystems::Render(engine::render::Renderer& renderer, float aspectRatio)
{
    m_rendererPtr = &renderer;
    if (m_testModels.spawned &&
        (m_testModelMeshes.maleBody == engine::render::Renderer::kInvalidGpuMesh ||
         m_testModelMeshes.femaleBody == engine::render::Renderer::kInvalidGpuMesh))
    {
        LoadTestModelMeshes();
    }
    if (!m_selectedSurvivorCharacterId.empty())
    {
        (void)EnsureSurvivorCharacterMeshLoaded(m_selectedSurvivorCharacterId);
        RefreshAnimatedSurvivorMeshIfNeeded(m_selectedSurvivorCharacterId);
    }

    const glm::mat4 viewProjection = BuildViewProjection(aspectRatio);
    m_frustum.Extract(viewProjection);

    glm::vec3 postFxColor = m_fxSystem.PostFxPulseColor();
    float postFxIntensity = m_fxSystem.PostFxPulseIntensity();
    if (m_controlledRole == ControlledRole::Killer && m_killerPowerState.killerBlindTimer > 0.0F)
    {
        const float blind01 = glm::clamp(
            m_killerPowerState.killerBlindTimer / std::max(0.05F, m_tuning.flashlightBlindDurationSeconds),
            0.0F,
            1.0F
        );
        const bool whiteStyle = m_tuning.flashlightBlindStyle == 0;
        postFxColor = whiteStyle ? glm::vec3{1.0F, 1.0F, 1.0F} : glm::vec3{-1.0F, -1.0F, -1.0F};
        postFxIntensity = std::max(postFxIntensity, blind01 * (whiteStyle ? 1.25F : 1.0F));
    }
    renderer.SetPostFxPulse(postFxColor, postFxIntensity);

    // Dynamic spot lights (keep map lights, then append runtime lights).
    // Re-use m_runtimeSpotLights to avoid per-frame heap allocation.
    m_runtimeSpotLights.clear();
    const auto& baseSpotLights = renderer.GetSpotLights();
    const std::size_t mapCount = std::min(m_mapSpotLightCount, baseSpotLights.size());
    m_runtimeSpotLights.assign(baseSpotLights.begin(), baseSpotLights.begin() + static_cast<std::ptrdiff_t>(mapCount));
    auto& spotLights = m_runtimeSpotLights;

    // Phase B4: Killer Look Light (spot cone).
    const auto killerTransIt = m_world.Transforms().find(m_killer);
    const bool isLocalKiller = m_controlledRole == ControlledRole::Killer;
    if (m_killer != 0 && killerTransIt != m_world.Transforms().end() && m_killerLookLight.enabled && !isLocalKiller)
    {
        const glm::vec3 killerPos = killerTransIt->second.position;
        const glm::vec3 killerForward = killerTransIt->second.forward;
        const glm::vec3 flatForward = glm::length(glm::vec3{killerForward.x, 0.0F, killerForward.z}) > 0.001F
                                          ? glm::normalize(glm::vec3{killerForward.x, 0.0F, killerForward.z})
                                          : glm::vec3{0.0F, 0.0F, -1.0F};
        const glm::vec3 lightPos = killerPos + glm::vec3{0.0F, 0.8F, 0.0F} + flatForward * 0.3F;
        const float pitchRad = glm::radians(m_killerLookLight.pitchDegrees);
        const glm::vec3 lightDir = glm::normalize(flatForward * glm::cos(pitchRad) - glm::vec3{0.0F, 1.0F, 0.0F} * glm::sin(pitchRad));

        spotLights.push_back({
            lightPos,
            lightDir,
            m_killerLookLight.color,
            m_killerLookLight.intensity,
            m_killerLookLight.range,
            glm::cos(glm::radians(m_killerLookLight.innerAngleDegrees * 0.5F)),
            glm::cos(glm::radians(m_killerLookLight.outerAngleDegrees * 0.5F))
        });

        if (m_killerLookLightDebug)
        {
            const float coneLength = m_killerLookLight.range;
            const float coneRadius = coneLength * glm::tan(glm::radians(m_killerLookLight.outerAngleDegrees * 0.5F));
            const int segments = 8;
            const float angleStep = glm::two_pi<float>() / static_cast<float>(segments);

            for (int i = 0; i < segments; ++i)
            {
                const float theta1 = static_cast<float>(i) * angleStep;
                const float theta2 = static_cast<float>(i + 1) * angleStep;
                const glm::vec3 offset = glm::vec3{
                    glm::sin(theta1) * coneRadius * 0.5F,
                    0.0F,
                    glm::cos(theta1) * coneRadius * 0.5F
                } + lightPos;
                const glm::vec3 offset2 = glm::vec3{
                    glm::sin(theta2) * coneRadius * 0.5F,
                    0.0F,
                    glm::cos(theta2) * coneRadius * 0.5F
                } + lightPos;
                renderer.DrawLine(offset, offset2, m_killerLookLight.color);
            }

            const glm::vec3 tipPos = lightPos + lightDir * coneLength;
            renderer.DrawLine(lightPos, tipPos, m_killerLookLight.color * 0.5F);
        }
    }

    // Flashlight runtime light (survivor RMB item).
    const bool flashlightActive = m_survivor != 0 &&
                                  m_survivorLoadout.itemId == "flashlight" &&
                                  m_survivorItemState.active &&
                                  m_survivorItemState.charges > 0.0F;
    const auto survivorTransformIt = m_world.Transforms().find(m_survivor);
    const auto survivorActorIt = m_world.Actors().find(m_survivor);
    if (flashlightActive && survivorTransformIt != m_world.Transforms().end() && survivorActorIt != m_world.Actors().end())
    {
        const engine::scene::Transform& survivorTransform = survivorTransformIt->second;
        const engine::scene::ActorComponent& survivorActor = survivorActorIt->second;
        const float eyeOffset = survivorActor.eyeHeight - survivorActor.capsuleHeight * 0.5F;
        const glm::vec3 origin = survivorTransform.position + glm::vec3{0.0F, eyeOffset, 0.0F} + survivorTransform.forward * 0.24F;
        const glm::vec3 direction = glm::length(survivorTransform.forward) > 1.0e-5F
                                        ? glm::normalize(survivorTransform.forward)
                                        : glm::vec3{0.0F, 0.0F, -1.0F};

        float beamRange = std::max(2.0F, m_tuning.flashlightBeamRange);
        float beamAngle = std::max(5.0F, m_tuning.flashlightBeamAngleDegrees);
        float blindNeed = std::max(0.25F, m_tuning.flashlightBlindBuildSeconds);
        if (const loadout::ItemDefinition* itemDef = m_loadoutCatalog.FindItem("flashlight"))
        {
            const auto findParam = [&](const char* key, float fallback) {
                const auto it = itemDef->params.find(key);
                return it != itemDef->params.end() ? it->second : fallback;
            };
            beamRange = std::max(2.0F, m_survivorItemModifiers.ApplyStat("beam_range", findParam("beam_range", beamRange)));
            beamAngle = std::max(5.0F, m_survivorItemModifiers.ApplyStat("beam_angle_deg", findParam("beam_angle_deg", beamAngle)));
            blindNeed = std::max(0.25F, m_survivorItemModifiers.ApplyStat("blind_time_required", findParam("blind_time_required", blindNeed)));
        }
        const float blindBuild01 = glm::clamp(m_survivorItemState.flashBlindAccum / std::max(0.05F, blindNeed), 0.0F, 1.0F);
        const float innerCoreAngle = glm::mix(std::max(4.0F, beamAngle * 0.45F), std::max(2.0F, beamAngle * 0.09F), blindBuild01);
        const float outerBeamAngle = std::max(beamAngle, innerCoreAngle + 2.0F);
        spotLights.push_back({
            origin,
            direction,
            glm::vec3{1.0F, 0.94F, 0.62F},
            4.6F,
            beamRange,
            glm::cos(glm::radians(innerCoreAngle * 0.65F)),
            glm::cos(glm::radians(outerBeamAngle * 0.6F))
        });
        spotLights.push_back({
            origin,
            direction,
            glm::vec3{1.0F, 0.98F, 0.7F},
            6.0F + blindBuild01 * 2.2F,
            beamRange * 0.92F,
            glm::cos(glm::radians(innerCoreAngle * 0.5F)),
            glm::cos(glm::radians(std::max(innerCoreAngle * 0.9F, innerCoreAngle + 1.0F)))
        });

        if (m_survivorItemState.flashlightSuccessFlashTimer > 0.0F)
        {
            const float flash01 = glm::clamp(m_survivorItemState.flashlightSuccessFlashTimer / 0.18F, 0.0F, 1.0F);
            spotLights.push_back({
                origin,
                direction,
                glm::vec3{1.0F, 1.0F, 0.88F},
                12.0F * flash01,
                beamRange * 0.72F,
                glm::cos(glm::radians(1.5F)),
                glm::cos(glm::radians(6.0F))
            });
        }

        renderer.DrawLine(origin, origin + direction * std::min(beamRange, 4.0F), glm::vec3{1.0F, 0.95F, 0.45F});
    }

    renderer.SetSpotLights(std::move(m_runtimeSpotLights));

    renderer.DrawGrid(40, 1.0F, glm::vec3{0.24F, 0.24F, 0.24F}, glm::vec3{0.11F, 0.11F, 0.11F}, glm::vec4{0.09F, 0.11F, 0.13F, 1.0F});

    renderer.DrawLine(glm::vec3{0.0F}, glm::vec3{2.0F, 0.0F, 0.0F}, glm::vec3{1.0F, 0.2F, 0.2F});
    renderer.DrawLine(glm::vec3{0.0F}, glm::vec3{0.0F, 2.0F, 0.0F}, glm::vec3{0.2F, 1.0F, 0.2F});
    renderer.DrawLine(glm::vec3{0.0F}, glm::vec3{0.0F, 0.0F, 2.0F}, glm::vec3{0.2F, 0.4F, 1.0F});

    const auto& transforms = m_world.Transforms();

    // Dynamic object frustum culling counters.
    std::uint32_t dynamicDrawn = 0;
    std::uint32_t dynamicCulled = 0;

    enum class VisibilityLod
    {
        Culled,
        Full,
        EdgeLow
    };

    // Helper: test if an AABB at (center ± halfExtents) is inside frustum.
    const auto isVisible = [this](const glm::vec3& center, const glm::vec3& halfExtents) -> bool {
        return m_frustum.IntersectsAABB(center - halfExtents, center + halfExtents);
    };

    const auto classifyVisibility = [this](const glm::vec3& center, const glm::vec3& halfExtents) -> VisibilityLod {
        if (m_frustum.IntersectsAABB(center - halfExtents, center + halfExtents))
        {
            return VisibilityLod::Full;
        }

        const glm::vec3 expandedHalfExtents = halfExtents * kPovLodBufferScale;
        if (m_frustum.IntersectsAABB(center - expandedHalfExtents, center + expandedHalfExtents))
        {
            return VisibilityLod::EdgeLow;
        }

        return VisibilityLod::Culled;
    };

    if (m_staticBatcher.IsBuilt())
    {
        m_staticBatcher.Render(
            viewProjection,
            m_frustum,
            renderer.GetSolidShaderProgram(),
            renderer.GetSolidViewProjLocation(),
            renderer.GetSolidModelLocation()
        );
    }

    for (const auto& [entity, window] : m_world.Windows())
    {
        const auto transformIt = transforms.find(entity);
        if (transformIt == transforms.end())
        {
            continue;
        }

        if (!isVisible(transformIt->second.position, window.halfExtents))
        {
            ++dynamicCulled;
            continue;
        }
        ++dynamicDrawn;

        renderer.DrawBox(transformIt->second.position, window.halfExtents, glm::vec3{0.1F, 0.75F, 0.84F});
        if (m_debugDrawEnabled)
        {
            renderer.DrawLine(
                transformIt->second.position,
                transformIt->second.position + window.normal * 1.5F,
                glm::vec3{0.2F, 1.0F, 1.0F}
            );
        }
    }

    for (const auto& [entity, pallet] : m_world.Pallets())
    {
        const auto transformIt = transforms.find(entity);
        if (transformIt == transforms.end())
        {
            continue;
        }

        if (!isVisible(transformIt->second.position, pallet.halfExtents))
        {
            ++dynamicCulled;
            continue;
        }
        ++dynamicDrawn;

        glm::vec3 color{0.8F, 0.5F, 0.2F};
        if (pallet.state == engine::scene::PalletState::Dropped)
        {
            color = glm::vec3{0.95F, 0.2F, 0.2F};
        }
        else if (pallet.state == engine::scene::PalletState::Broken)
        {
            color = glm::vec3{0.35F, 0.2F, 0.1F};
        }

        renderer.DrawBox(transformIt->second.position, pallet.halfExtents, color);
    }

    for (const auto& [entity, hook] : m_world.Hooks())
    {
        const auto transformIt = transforms.find(entity);
        if (transformIt == transforms.end())
        {
            continue;
        }

        if (!isVisible(transformIt->second.position, hook.halfExtents))
        {
            ++dynamicCulled;
            continue;
        }
        ++dynamicDrawn;

        const glm::vec3 hookColor = hook.occupied ? glm::vec3{0.78F, 0.1F, 0.1F} : glm::vec3{0.9F, 0.9F, 0.12F};
        renderer.DrawBox(transformIt->second.position, hook.halfExtents, hookColor);
    }

    for (const auto& [entity, trap] : m_world.BearTraps())
    {
        const auto transformIt = transforms.find(entity);
        if (transformIt == transforms.end())
        {
            continue;
        }

        if (!isVisible(transformIt->second.position, trap.halfExtents))
        {
            ++dynamicCulled;
            continue;
        }
        ++dynamicDrawn;

        glm::vec3 color{0.72F, 0.72F, 0.75F};
        if (trap.state == engine::scene::TrapState::Triggered)
        {
            color = glm::vec3{0.95F, 0.32F, 0.25F};
        }
        else if (trap.state == engine::scene::TrapState::Disarmed)
        {
            color = glm::vec3{0.26F, 0.26F, 0.28F};
        }
        renderer.DrawBox(transformIt->second.position, trap.halfExtents, color);
        if (m_trapDebugEnabled)
        {
            renderer.DrawLine(
                transformIt->second.position,
                transformIt->second.position + glm::vec3{0.0F, 0.75F, 0.0F},
                color
            );
        }
    }

    for (const auto& [entity, groundItem] : m_world.GroundItems())
    {
        const auto transformIt = transforms.find(entity);
        if (transformIt == transforms.end())
        {
            continue;
        }

        // Small default AABB for ground items.
        const glm::vec3 itemHalf{0.3F, 0.15F, 0.3F};
        if (!isVisible(transformIt->second.position, itemHalf))
        {
            ++dynamicCulled;
            continue;
        }
        ++dynamicDrawn;

        glm::vec3 color{0.8F, 0.8F, 0.8F};
        glm::vec3 halfExtents{0.2F, 0.06F, 0.2F};
        if (groundItem.itemId == "medkit")
        {
            color = glm::vec3{0.9F, 0.2F, 0.2F};
            halfExtents = glm::vec3{0.24F, 0.08F, 0.18F};
        }
        else if (groundItem.itemId == "toolbox")
        {
            color = glm::vec3{0.2F, 0.45F, 0.95F};
            halfExtents = glm::vec3{0.25F, 0.09F, 0.18F};
        }
        else if (groundItem.itemId == "flashlight")
        {
            color = glm::vec3{0.98F, 0.88F, 0.2F};
            halfExtents = glm::vec3{0.06F, 0.06F, 0.26F};
        }
        else if (groundItem.itemId == "map")
        {
            color = glm::vec3{0.2F, 0.86F, 0.5F};
            halfExtents = glm::vec3{0.22F, 0.015F, 0.16F};
        }
        renderer.DrawBox(transformIt->second.position, halfExtents, color);
    }

    // Render imported test survivor models (spawn_test_models / spawn_test_models_here).
    if (m_testModels.spawned)
    {
        if (m_testModelMeshes.maleBody != engine::render::Renderer::kInvalidGpuMesh)
        {
            const glm::vec3 modelPos = m_testModels.malePosition + glm::vec3{0.0F, m_testModelMeshes.maleFeetOffset, 0.0F};
            const glm::mat4 modelMatrix = glm::translate(glm::mat4{1.0F}, modelPos);
            renderer.DrawGpuMesh(m_testModelMeshes.maleBody, modelMatrix);
        }
        if (m_testModelMeshes.femaleBody != engine::render::Renderer::kInvalidGpuMesh)
        {
            const glm::vec3 modelPos = m_testModels.femalePosition + glm::vec3{0.0F, m_testModelMeshes.femaleFeetOffset, 0.0F};
            const glm::mat4 modelMatrix = glm::translate(glm::mat4{1.0F}, modelPos);
            renderer.DrawGpuMesh(m_testModelMeshes.femaleBody, modelMatrix);
        }
    }

    // Render debug static boxes (test models, etc.)
    for (const auto& [entity, box] : m_world.StaticBoxes())
    {
        // Skip solid boxes (handled by physics/other systems)
        if (box.solid)
        {
            continue;
        }

        const auto transformIt = transforms.find(entity);
        const auto colorIt = m_world.DebugColors().find(entity);
        if (transformIt == transforms.end() || colorIt == m_world.DebugColors().end())
        {
            continue;
        }

        const engine::scene::Transform& transform = transformIt->second;
        const glm::vec3& color = colorIt->second.color;

        // Position box with feet at ground level (center Y = halfExtents.y)
        const glm::vec3 boxCenter = transform.position + glm::vec3{0.0F, box.halfExtents.y, 0.0F};
        renderer.DrawBox(boxCenter, box.halfExtents, color);
    }

    auto drawOverlayBox = [&](const glm::vec3& center, const glm::vec3& halfExtents, const glm::vec3& color) {
        const glm::vec3 c000 = center + glm::vec3{-halfExtents.x, -halfExtents.y, -halfExtents.z};
        const glm::vec3 c001 = center + glm::vec3{-halfExtents.x, -halfExtents.y, halfExtents.z};
        const glm::vec3 c010 = center + glm::vec3{-halfExtents.x, halfExtents.y, -halfExtents.z};
        const glm::vec3 c011 = center + glm::vec3{-halfExtents.x, halfExtents.y, halfExtents.z};
        const glm::vec3 c100 = center + glm::vec3{halfExtents.x, -halfExtents.y, -halfExtents.z};
        const glm::vec3 c101 = center + glm::vec3{halfExtents.x, -halfExtents.y, halfExtents.z};
        const glm::vec3 c110 = center + glm::vec3{halfExtents.x, halfExtents.y, -halfExtents.z};
        const glm::vec3 c111 = center + glm::vec3{halfExtents.x, halfExtents.y, halfExtents.z};
        renderer.DrawOverlayLine(c000, c001, color);
        renderer.DrawOverlayLine(c000, c010, color);
        renderer.DrawOverlayLine(c001, c011, color);
        renderer.DrawOverlayLine(c010, c011, color);
        renderer.DrawOverlayLine(c100, c101, color);
        renderer.DrawOverlayLine(c100, c110, color);
        renderer.DrawOverlayLine(c101, c111, color);
        renderer.DrawOverlayLine(c110, c111, color);
        renderer.DrawOverlayLine(c000, c100, color);
        renderer.DrawOverlayLine(c001, c101, color);
        renderer.DrawOverlayLine(c010, c110, color);
        renderer.DrawOverlayLine(c011, c111, color);
    };

    if (m_trapPreviewActive && m_killerLoadout.powerId == "bear_trap")
    {
        const glm::vec3 color = m_trapPreviewValid ? glm::vec3{0.2F, 1.0F, 0.3F} : glm::vec3{1.0F, 0.25F, 0.2F};
        drawOverlayBox(
            m_trapPreviewPosition + glm::vec3{0.0F, 0.02F, 0.0F},
            m_trapPreviewHalfExtents + glm::vec3{0.02F, 0.01F, 0.02F},
            color
        );
        renderer.DrawOverlayLine(
            m_trapPreviewPosition + glm::vec3{0.0F, 0.03F, 0.0F},
            m_trapPreviewPosition + glm::vec3{0.0F, 0.78F, 0.0F},
            color
        );
    }

    for (const auto& [entity, generator] : m_world.Generators())
    {
        const auto transformIt = transforms.find(entity);
        if (transformIt == transforms.end())
        {
            continue;
        }

        if (!isVisible(transformIt->second.position, generator.halfExtents))
        {
            ++dynamicCulled;
            continue;
        }
        ++dynamicDrawn;

        // Green color scheme for generators
        glm::vec3 generatorColor{0.2F, 0.8F, 0.2F};  // Standard green
        if (generator.completed)
        {
            generatorColor = glm::vec3{0.0F, 0.5F, 0.0F};  // Dark green
        }
        else if (entity == m_activeRepairGenerator)
        {
            generatorColor = glm::vec3{0.4F, 1.0F, 0.4F};  // Bright green
        }

        renderer.DrawBox(transformIt->second.position, generator.halfExtents, generatorColor);

        const auto revealIt = m_mapRevealGenerators.find(entity);
        if (revealIt != m_mapRevealGenerators.end() && revealIt->second > 0.0F)
        {
            const float alpha = glm::clamp(revealIt->second / std::max(0.01F, m_tuning.mapRevealDurationSeconds), 0.0F, 1.0F);
            const glm::vec3 auraColor = glm::vec3{0.35F, 0.95F, 1.0F} * (0.5F + 0.5F * alpha);
            drawOverlayBox(transformIt->second.position, generator.halfExtents + glm::vec3{0.12F}, auraColor);
        }
    }

    for (const auto& [entity, actor] : m_world.Actors())
    {
        const auto transformIt = transforms.find(entity);
        if (transformIt == transforms.end())
        {
            continue;
        }

        // Frustum cull actors using capsule bounding box.
        const glm::vec3 actorHalf{actor.capsuleRadius, actor.capsuleHeight * 0.5F, actor.capsuleRadius};
        const VisibilityLod actorVisibility = classifyVisibility(transformIt->second.position, actorHalf);
        if (actorVisibility == VisibilityLod::Culled)
        {
            ++dynamicCulled;
            continue;
        }
        ++dynamicDrawn;

        const bool hideKillerBodyInFp =
            entity == m_killer &&
            m_controlledRole == ControlledRole::Killer &&
            ResolveCameraMode() == CameraMode::FirstPerson;
        if (hideKillerBodyInFp)
        {
            continue;
        }

        // Skip rendering killer mesh when Wraith is cloaked - shader handles visibility
        const bool hideKillerCloaked =
            entity == m_killer &&
            actor.role == engine::scene::Role::Killer &&
            m_killerLoadout.powerId == "wraith_cloak" &&
            (m_killerPowerState.wraithCloaked ||
             m_killerPowerState.wraithCloakTransition ||
             m_killerPowerState.wraithUncloakTransition);
        if (hideKillerCloaked)
        {
            continue; // Don't render normal mesh - cloak shader will handle it
        }

        glm::vec3 color = glm::vec3{0.95F, 0.2F, 0.2F};
        if (actor.role == engine::scene::Role::Survivor)
        {
            switch (m_survivorState)
            {
                case SurvivorHealthState::Healthy: color = glm::vec3{0.2F, 0.95F, 0.2F}; break;
                case SurvivorHealthState::Injured: color = glm::vec3{1.0F, 0.58F, 0.15F}; break;
                case SurvivorHealthState::Downed: color = glm::vec3{0.95F, 0.15F, 0.15F}; break;
                case SurvivorHealthState::Trapped: color = glm::vec3{0.93F, 0.85F, 0.2F}; break;
                case SurvivorHealthState::Carried: color = glm::vec3{0.72F, 0.24F, 0.95F}; break;
                case SurvivorHealthState::Hooked: color = glm::vec3{0.85F, 0.1F, 0.1F}; break;
                case SurvivorHealthState::Dead: color = glm::vec3{0.2F, 0.2F, 0.2F}; break;
                default: break;
            }
        }

        const float visualHeightScale =
            actor.crawling ? 0.5F :
            (actor.crouching ? 0.72F : 1.0F);
        bool renderedSurvivorMesh = false;
        bool survivorMeshDebugDataValid = false;
        glm::vec3 survivorMeshDebugPosition{0.0F};
        float survivorMeshDebugYaw = 0.0F;
        float survivorMeshDebugScale = 1.0F;
        glm::vec3 survivorMeshDebugBoundsMin{0.0F};
        glm::vec3 survivorMeshDebugBoundsMax{0.0F};
        if (actor.role == engine::scene::Role::Survivor)
        {
            const auto meshIt = m_survivorVisualMeshes.find(m_selectedSurvivorCharacterId);
            if (meshIt != m_survivorVisualMeshes.end() &&
                meshIt->second.gpuMesh != engine::render::Renderer::kInvalidGpuMesh)
            {
                const SurvivorVisualMesh& mesh = meshIt->second;
                float visualYaw = m_survivorVisualYawRadians;
                if (!m_survivorVisualYawInitialized)
                {
                    glm::vec3 fallbackFacing = glm::vec3{transformIt->second.forward.x, 0.0F, transformIt->second.forward.z};
                    if (glm::length(fallbackFacing) <= 1.0e-5F)
                    {
                        fallbackFacing = glm::vec3{0.0F, 0.0F, -1.0F};
                    }
                    else
                    {
                        fallbackFacing = glm::normalize(fallbackFacing);
                    }
                    visualYaw = std::atan2(fallbackFacing.x, -fallbackFacing.z);
                }
                float modelYawOffsetRadians = 0.0F;
                if (const loadout::SurvivorCharacterDefinition* survivorDef =
                        m_loadoutCatalog.FindSurvivor(m_selectedSurvivorCharacterId);
                    survivorDef != nullptr)
                {
                    modelYawOffsetRadians = glm::radians(survivorDef->modelYawDegrees);
                }
                // Debug yaw uses forward=(sin(yaw), 0, -cos(yaw)); to match that convention
                // for meshes authored with -Z forward in GLM rotation space, apply negative yaw.
                const float appliedMeshYaw = WrapAngleRadians(-(visualYaw + modelYawOffsetRadians));
                const float modelHeight = std::max(0.01F, mesh.boundsMaxY - mesh.boundsMinY);
                const float modelScale = std::max(0.05F, (actor.capsuleHeight * visualHeightScale) / modelHeight);
                const float survivorFeetY = transformIt->second.position.y - actor.capsuleHeight * visualHeightScale * 0.5F;
                const glm::vec3 modelPosition{
                    transformIt->second.position.x,
                    survivorFeetY + (-mesh.boundsMinY * modelScale),
                    transformIt->second.position.z
                };

                glm::mat4 modelMatrix = glm::translate(glm::mat4{1.0F}, modelPosition);
                modelMatrix = glm::rotate(modelMatrix, appliedMeshYaw, glm::vec3{0.0F, 1.0F, 0.0F});
                modelMatrix = glm::scale(modelMatrix, glm::vec3{modelScale});
                renderer.DrawGpuMesh(mesh.gpuMesh, modelMatrix);
                renderedSurvivorMesh = true;
                survivorMeshDebugDataValid = true;
                survivorMeshDebugPosition = modelPosition;
                survivorMeshDebugYaw = appliedMeshYaw;
                survivorMeshDebugScale = modelScale;
                survivorMeshDebugBoundsMin = glm::vec3{mesh.boundsMinY, mesh.boundsMinY, mesh.boundsMinY};
                survivorMeshDebugBoundsMax = glm::vec3{mesh.boundsMaxY, mesh.boundsMaxY, mesh.boundsMaxY};
                survivorMeshDebugBoundsMin.x = -mesh.maxAbsXZ;
                survivorMeshDebugBoundsMin.z = -mesh.maxAbsXZ;
                survivorMeshDebugBoundsMax.x = mesh.maxAbsXZ;
                survivorMeshDebugBoundsMax.z = mesh.maxAbsXZ;
            }
        }

        if (!renderedSurvivorMesh && actorVisibility == VisibilityLod::EdgeLow)
        {
            const glm::vec3 lowLodHalfExtents{
                actor.capsuleRadius,
                actor.capsuleHeight * visualHeightScale * 0.5F,
                actor.capsuleRadius
            };
            renderer.DrawBox(transformIt->second.position, lowLodHalfExtents, color * 0.9F);
        }
        else if (!renderedSurvivorMesh)
        {
            renderer.DrawCapsule(
                transformIt->second.position,
                actor.capsuleHeight * visualHeightScale,
                actor.capsuleRadius,
                color
            );
        }

        if (m_debugDrawEnabled)
        {
            renderer.DrawLine(
                transformIt->second.position,
                transformIt->second.position + transformIt->second.forward * 1.4F,
                color
            );

            if (entity == m_survivor)
            {
                const glm::vec3 origin = transformIt->second.position + glm::vec3{0.0F, 0.05F, 0.0F};
                const glm::vec3 modelForward{
                    std::sin(m_survivorVisualYawRadians),
                    0.0F,
                    -std::cos(m_survivorVisualYawRadians)
                };
                renderer.DrawLine(origin, origin + modelForward * 1.8F, glm::vec3{0.2F, 0.95F, 1.0F});

                if (glm::length(m_survivorVisualDesiredDirection) > 1.0e-5F)
                {
                    renderer.DrawLine(origin, origin + glm::normalize(m_survivorVisualDesiredDirection) * 1.6F, glm::vec3{0.2F, 1.0F, 0.2F});
                }

                const glm::vec3 cameraFlat{m_cameraForward.x, 0.0F, m_cameraForward.z};
                if (glm::length(cameraFlat) > 1.0e-5F)
                {
                    renderer.DrawLine(origin, origin + glm::normalize(cameraFlat) * 1.4F, glm::vec3{1.0F, 0.9F, 0.2F});
                }

                // Draw survivor hitbox capsule wireframe (debug only).
                const float capsuleHeight = actor.capsuleHeight * visualHeightScale;
                const float radius = actor.capsuleRadius;
                const float halfSegment = std::max(0.0F, capsuleHeight * 0.5F - radius);
                const glm::vec3 capsuleCenter = transformIt->second.position;
                const glm::vec3 capTop = capsuleCenter + glm::vec3{0.0F, halfSegment, 0.0F};
                const glm::vec3 capBottom = capsuleCenter - glm::vec3{0.0F, halfSegment, 0.0F};
                const glm::vec3 capsuleColor{1.0F, 0.2F, 0.2F};
                constexpr int kCapsuleSegments = 24;
                constexpr int kHemisphereStacks = 5;
                constexpr int kMeridians = 8;
                constexpr int kArcSegments = 8;
                for (int i = 0; i < kCapsuleSegments; ++i)
                {
                    const float t0 = glm::two_pi<float>() * static_cast<float>(i) / static_cast<float>(kCapsuleSegments);
                    const float t1 = glm::two_pi<float>() * static_cast<float>(i + 1) / static_cast<float>(kCapsuleSegments);
                    const glm::vec3 r0{std::cos(t0) * radius, 0.0F, std::sin(t0) * radius};
                    const glm::vec3 r1{std::cos(t1) * radius, 0.0F, std::sin(t1) * radius};
                    renderer.DrawLine(capTop + r0, capTop + r1, capsuleColor);
                    renderer.DrawLine(capBottom + r0, capBottom + r1, capsuleColor);
                    if ((i % std::max(1, kCapsuleSegments / kMeridians)) == 0)
                    {
                        renderer.DrawLine(capBottom + r0, capTop + r0, capsuleColor);
                    }
                }
                for (int stack = 1; stack <= kHemisphereStacks; ++stack)
                {
                    const float a = glm::half_pi<float>() * static_cast<float>(stack) / static_cast<float>(kHemisphereStacks + 1);
                    const float ringRadius = std::cos(a) * radius;
                    const float yOffset = std::sin(a) * radius;
                    for (int i = 0; i < kCapsuleSegments; ++i)
                    {
                        const float t0 = glm::two_pi<float>() * static_cast<float>(i) / static_cast<float>(kCapsuleSegments);
                        const float t1 = glm::two_pi<float>() * static_cast<float>(i + 1) / static_cast<float>(kCapsuleSegments);
                        const glm::vec3 top0{std::cos(t0) * ringRadius, yOffset, std::sin(t0) * ringRadius};
                        const glm::vec3 top1{std::cos(t1) * ringRadius, yOffset, std::sin(t1) * ringRadius};
                        const glm::vec3 bottom0{std::cos(t0) * ringRadius, -yOffset, std::sin(t0) * ringRadius};
                        const glm::vec3 bottom1{std::cos(t1) * ringRadius, -yOffset, std::sin(t1) * ringRadius};
                        renderer.DrawLine(capTop + top0, capTop + top1, capsuleColor);
                        renderer.DrawLine(capBottom + bottom0, capBottom + bottom1, capsuleColor);
                    }
                }
                for (int meridian = 0; meridian < kMeridians; ++meridian)
                {
                    const float t = glm::two_pi<float>() * static_cast<float>(meridian) / static_cast<float>(kMeridians);
                    const glm::vec3 radial{std::cos(t), 0.0F, std::sin(t)};

                    glm::vec3 prevTop = capTop + radial * radius;
                    glm::vec3 prevBottom = capBottom + radial * radius;
                    for (int j = 1; j <= kArcSegments; ++j)
                    {
                        const float a = glm::half_pi<float>() * static_cast<float>(j) / static_cast<float>(kArcSegments);
                        const float ringRadius = std::cos(a) * radius;
                        const float yOffset = std::sin(a) * radius;

                        const glm::vec3 topPoint = capTop + radial * ringRadius + glm::vec3{0.0F, yOffset, 0.0F};
                        const glm::vec3 bottomPoint = capBottom + radial * ringRadius - glm::vec3{0.0F, yOffset, 0.0F};
                        renderer.DrawLine(prevTop, topPoint, capsuleColor);
                        renderer.DrawLine(prevBottom, bottomPoint, capsuleColor);
                        prevTop = topPoint;
                        prevBottom = bottomPoint;
                    }
                }

                // Draw mesh wireframe bounds box (debug only) to verify model rotation.
                if (survivorMeshDebugDataValid)
                {
                    const glm::mat4 rot = glm::rotate(glm::mat4{1.0F}, survivorMeshDebugYaw, glm::vec3{0.0F, 1.0F, 0.0F});
                    const glm::vec3 minV = survivorMeshDebugBoundsMin * survivorMeshDebugScale;
                    const glm::vec3 maxV = survivorMeshDebugBoundsMax * survivorMeshDebugScale;
                    const std::array<glm::vec3, 8> localCorners{
                        glm::vec3{minV.x, minV.y, minV.z},
                        glm::vec3{minV.x, minV.y, maxV.z},
                        glm::vec3{minV.x, maxV.y, minV.z},
                        glm::vec3{minV.x, maxV.y, maxV.z},
                        glm::vec3{maxV.x, minV.y, minV.z},
                        glm::vec3{maxV.x, minV.y, maxV.z},
                        glm::vec3{maxV.x, maxV.y, minV.z},
                        glm::vec3{maxV.x, maxV.y, maxV.z}
                    };
                    std::array<glm::vec3, 8> worldCorners{};
                    for (std::size_t i = 0; i < localCorners.size(); ++i)
                    {
                        const glm::vec4 rotated = rot * glm::vec4{localCorners[i], 1.0F};
                        worldCorners[i] = survivorMeshDebugPosition + glm::vec3{rotated.x, rotated.y, rotated.z};
                    }
                    const auto drawEdge = [&](int a, int b) {
                        renderer.DrawLine(worldCorners[static_cast<std::size_t>(a)], worldCorners[static_cast<std::size_t>(b)], glm::vec3{0.2F, 0.7F, 1.0F});
                    };
                    drawEdge(0, 1); drawEdge(0, 2); drawEdge(1, 3); drawEdge(2, 3);
                    drawEdge(4, 5); drawEdge(4, 6); drawEdge(5, 7); drawEdge(6, 7);
                    drawEdge(0, 4); drawEdge(1, 5); drawEdge(2, 6); drawEdge(3, 7);
                }
            }
        }
    }

    const bool showFpWeapon = m_controlledRole == ControlledRole::Killer && ResolveCameraMode() == CameraMode::FirstPerson;
    const auto killerTransformIt = transforms.find(m_killer);
    if (showFpWeapon && m_cameraInitialized && killerTransformIt != transforms.end())
    {
        const engine::scene::Transform& killerTransform = killerTransformIt->second;
        const float killerYaw = killerTransform.rotationEuler.y;
        const float killerPitch = killerTransform.rotationEuler.x;

        glm::vec3 forward = ForwardFromYawPitch(killerYaw, killerPitch);
        if (glm::length(forward) < 1.0e-5F)
        {
            forward = glm::vec3{0.0F, 0.0F, -1.0F};
        }
        forward = glm::normalize(forward);

        glm::vec3 right = glm::cross(forward, glm::vec3{0.0F, 1.0F, 0.0F});
        if (glm::length(right) < 1.0e-5F)
        {
            right = glm::vec3{1.0F, 0.0F, 0.0F};
        }
        right = glm::normalize(right);
        const glm::vec3 up = glm::normalize(glm::cross(right, forward));

        float attackForwardOffset = 0.0F;
        float attackUpOffset = 0.0F;
        float attackSideOffset = 0.0F;
        float attackRollDegrees = 0.0F;
        if (m_killerAttackState == KillerAttackState::ChargingLunge)
        {
            const float charge01 = glm::clamp(m_killerLungeChargeSeconds / std::max(0.01F, m_killerLungeChargeMaxSeconds), 0.0F, 1.0F);
            attackForwardOffset = -0.03F * charge01;
            attackUpOffset = -0.03F * charge01;
            attackSideOffset = -0.02F * charge01;
            attackRollDegrees = -8.0F * charge01;
        }
        else if (m_killerAttackState == KillerAttackState::Lunging)
        {
            attackForwardOffset = 0.18F;
            attackUpOffset = -0.08F;
            attackSideOffset = 0.02F;
            attackRollDegrees = 18.0F;
        }
        else if (m_killerAttackState == KillerAttackState::Recovering)
        {
            attackForwardOffset = -0.04F;
            attackUpOffset = -0.05F;
            attackSideOffset = -0.01F;
            attackRollDegrees = -10.0F;
        }

        const float sideOffset = 0.23F;
        const float forwardOffset = 0.42F;
        const float downOffset = -0.22F;
        const glm::vec3 weaponCenter =
            m_cameraPosition +
            forward * (forwardOffset + attackForwardOffset) +
            right * (sideOffset + attackSideOffset) +
            up * (downOffset + attackUpOffset);

        const glm::vec3 weaponRotationDegrees{
            glm::degrees(killerPitch) - 12.0F,
            180.0F - glm::degrees(killerYaw),
            28.0F + attackRollDegrees,
        };
        renderer.DrawOrientedBox(
            weaponCenter,
            glm::vec3{0.07F, 0.05F, 0.24F},
            weaponRotationDegrees,
            glm::vec3{0.18F, 0.18F, 0.18F}
        );
    }

    if (m_terrorRadiusVisible && m_killer != 0)
    {
        const bool killerIsUndetectable = m_statusEffectManager.IsUndetectable(m_killer);
        // Skip terror radius visualization when killer is undetectable (e.g., Wraith cloaked)
        if (!killerIsUndetectable)
        {
            const auto killerTransformIt = transforms.find(m_killer);
            if (killerTransformIt != transforms.end())
            {
                const float perkModifier = m_perkSystem.GetTerrorRadiusModifier(engine::scene::Role::Killer);
                const float baseRadius = m_chase.isChasing ? m_terrorRadiusChaseMeters : m_terrorRadiusMeters;
                const float radius = baseRadius + perkModifier;
                const glm::vec3 center = killerTransformIt->second.position + glm::vec3{0.0F, 0.06F, 0.0F};
                const glm::vec3 trColor = m_chase.isChasing ? glm::vec3{1.0F, 0.2F, 0.2F} : glm::vec3{1.0F, 0.5F, 0.15F};
                renderer.DrawCircle(center, radius, 48, trColor, true);
            }
        }
    }

    if (m_debugDrawEnabled)
    {
        if (m_killer != 0)
        {
            const auto killerTransformIt = transforms.find(m_killer);
            if (killerTransformIt != transforms.end())
            {
                const glm::vec3 origin = killerTransformIt->second.position + glm::vec3{0.0F, 0.08F, 0.0F};
                const glm::vec3 forward = glm::length(glm::vec3{killerTransformIt->second.forward.x, 0.0F, killerTransformIt->second.forward.z}) > 1.0e-5F
                                              ? glm::normalize(glm::vec3{killerTransformIt->second.forward.x, 0.0F, killerTransformIt->second.forward.z})
                                              : glm::vec3{0.0F, 0.0F, -1.0F};
                const float range = (m_killerAttackState == KillerAttackState::Lunging) ? m_killerLungeRange : m_killerShortRange;
                const float halfAngle = (m_killerAttackState == KillerAttackState::Lunging) ? m_killerLungeHalfAngleRadians : m_killerShortHalfAngleRadians;

                const glm::vec3 leftDir = glm::normalize(glm::vec3{
                    forward.x * std::cos(halfAngle) - forward.z * std::sin(halfAngle),
                    0.0F,
                    forward.x * std::sin(halfAngle) + forward.z * std::cos(halfAngle)
                });
                const glm::vec3 rightDir = glm::normalize(glm::vec3{
                    forward.x * std::cos(-halfAngle) - forward.z * std::sin(-halfAngle),
                    0.0F,
                    forward.x * std::sin(-halfAngle) + forward.z * std::cos(-halfAngle)
                });

                glm::vec3 wedgeColor = glm::vec3{0.95F, 0.95F, 0.2F};
                if (m_killerAttackState == KillerAttackState::ChargingLunge)
                {
                    wedgeColor = glm::vec3{1.0F, 0.55F, 0.15F};
                }
                else if (m_killerAttackState == KillerAttackState::Lunging)
                {
                    wedgeColor = glm::vec3{1.0F, 0.2F, 0.2F};
                }
                if (m_killerAttackFlashTtl > 0.0F)
                {
                    wedgeColor = glm::vec3{1.0F, 1.0F, 1.0F};
                }

                const glm::vec3 leftPoint = origin + leftDir * range;
                const glm::vec3 rightPoint = origin + rightDir * range;
                renderer.DrawOverlayLine(origin, leftPoint, wedgeColor);
                renderer.DrawOverlayLine(origin, rightPoint, wedgeColor);
                renderer.DrawOverlayLine(leftPoint, rightPoint, wedgeColor);
            }
        }

        for (const engine::physics::SolidBox& solid : m_physics.Solids())
        {
            renderer.DrawBox(solid.center, solid.halfExtents, glm::vec3{0.9F, 0.4F, 0.85F});
        }

        for (const engine::physics::TriggerVolume& trigger : m_physics.Triggers())
        {
            glm::vec3 triggerColor{0.2F, 0.6F, 1.0F};
            if (trigger.kind == engine::physics::TriggerKind::Interaction)
            {
                // Check if this trigger belongs to a generator
                const auto isGenerator = m_world.Generators().contains(trigger.entity);
                triggerColor = isGenerator ? glm::vec3{0.2F, 0.8F, 0.2F} : glm::vec3{1.0F, 0.8F, 0.2F};
            }
            else if (trigger.kind == engine::physics::TriggerKind::Chase)
            {
                triggerColor = glm::vec3{1.0F, 0.2F, 0.2F};
            }
            renderer.DrawBox(trigger.center, trigger.halfExtents, triggerColor);
        }

        for (const LoopDebugTile& tile : m_loopDebugTiles)
        {
            glm::vec3 color{0.3F, 0.3F, 0.3F};
            switch (tile.archetype)
            {
                case 0: color = glm::vec3{0.85F, 0.55F, 0.25F}; break; // JungleGymLong
                case 1: color = glm::vec3{0.2F, 0.7F, 0.95F}; break;   // JungleGymShort
                case 2: color = glm::vec3{0.95F, 0.3F, 0.5F}; break;   // LT Walls
                case 3: color = glm::vec3{0.35F, 0.95F, 0.35F}; break; // Shack
                case 4: color = glm::vec3{1.0F, 0.85F, 0.2F}; break;   // FourLane
                case 5: color = glm::vec3{0.55F, 0.55F, 0.55F}; break; // FillerA
                case 6: color = glm::vec3{0.5F, 0.5F, 0.5F}; break;    // FillerB
                default: break;
            }

            const glm::vec3 center = tile.center + glm::vec3{0.0F, 0.03F, 0.0F};
            renderer.DrawBox(center, tile.halfExtents, color);
            renderer.DrawLine(center, center + glm::vec3{0.0F, 0.9F, 0.0F}, color);
        }

        if (m_survivor != 0 && m_killer != 0)
        {
            const auto survivorIt = transforms.find(m_survivor);
            const auto killerIt = transforms.find(m_killer);
            if (survivorIt != transforms.end() && killerIt != transforms.end())
            {
                const glm::vec3 losColor = m_chase.hasLineOfSight ? glm::vec3{0.1F, 1.0F, 0.2F} : glm::vec3{1.0F, 0.1F, 0.1F};
                renderer.DrawLine(killerIt->second.position, survivorIt->second.position, losColor);
            }
        }

        const glm::vec3 hitColor = m_lastHitConnected ? glm::vec3{1.0F, 0.2F, 0.2F} : glm::vec3{1.0F, 1.0F, 0.2F};
        renderer.DrawLine(m_lastHitRayStart, m_lastHitRayEnd, hitColor);

        if (m_lastSwingDebugTtl > 0.0F && m_lastSwingRange > 0.01F)
        {
            const glm::vec3 dir = glm::length(m_lastSwingDirection) > 1.0e-5F
                                      ? glm::normalize(m_lastSwingDirection)
                                      : glm::vec3{0.0F, 0.0F, -1.0F};
            glm::vec3 right = glm::cross(dir, glm::vec3{0.0F, 1.0F, 0.0F});
            if (glm::length(right) < 1.0e-5F)
            {
                right = glm::cross(dir, glm::vec3{1.0F, 0.0F, 0.0F});
            }
            right = glm::normalize(right);
            const glm::vec3 up = glm::normalize(glm::cross(right, dir));

            const float radiusAtEnd = std::tan(m_lastSwingHalfAngleRadians) * m_lastSwingRange;
            const glm::vec3 endCenter = m_lastSwingOrigin + dir * m_lastSwingRange;
            renderer.DrawLine(m_lastSwingOrigin, endCenter, hitColor);

            constexpr int kSegments = 24;
            glm::vec3 firstPoint{0.0F};
            glm::vec3 previousPoint{0.0F};
            for (int i = 0; i <= kSegments; ++i)
            {
                const float theta = 2.0F * glm::pi<float>() * static_cast<float>(i) / static_cast<float>(kSegments);
                const glm::vec3 ringOffset = right * std::cos(theta) * radiusAtEnd + up * std::sin(theta) * radiusAtEnd;
                const glm::vec3 point = endCenter + ringOffset;
                if (i == 0)
                {
                    firstPoint = point;
                }
                else
                {
                    renderer.DrawLine(previousPoint, point, hitColor);
                }
                previousPoint = point;
            }
            renderer.DrawLine(previousPoint, firstPoint, hitColor);

            renderer.DrawLine(m_lastSwingOrigin, endCenter + right * radiusAtEnd, hitColor);
            renderer.DrawLine(m_lastSwingOrigin, endCenter - right * radiusAtEnd, hitColor);
            renderer.DrawLine(m_lastSwingOrigin, endCenter + up * radiusAtEnd, hitColor);
            renderer.DrawLine(m_lastSwingOrigin, endCenter - up * radiusAtEnd, hitColor);
        }
    }

    m_fxSystem.Render(renderer, m_cameraPosition);

    // Phase B2/B3: Render scratch marks and blood pools (killer-only visibility)
    const bool localIsKiller = (m_controlledRole == ControlledRole::Killer);
    RenderScratchMarks(renderer, localIsKiller);
    RenderBloodPools(renderer, localIsKiller);

    // High-poly meshes for GPU stress testing (benchmark map)
    RenderHighPolyMeshes(renderer);

    // Custom loop meshes
    RenderLoopMeshes(renderer);

    // Hatchet power debug visualization
    RenderHatchetDebug(renderer);
    RenderHatchetTrajectoryPrediction(renderer);
    RenderHatchetProjectiles(renderer);

    // Chainsaw sprint power debug visualization
    RenderChainsawDebug(renderer);

    // Nurse blink power preview (always visible when charging)
    RenderBlinkPreview(renderer);

    // Nurse blink power debug visualization (extra info when debug enabled)
    RenderBlinkDebug(renderer);

    // Report dynamic object culling stats to profiler.
    auto& profStats = engine::core::Profiler::Instance().StatsMut();
    profStats.dynamicObjectsDrawn = dynamicDrawn;
    profStats.dynamicObjectsCulled = dynamicCulled;
}

glm::mat4 GameplaySystems::BuildViewProjection(float aspectRatio) const
{
    const glm::mat4 view = glm::lookAt(m_cameraPosition, m_cameraTarget, glm::vec3{0.0F, 1.0F, 0.0F});
    float fovDeg = 60.0F;
    if (m_controlledRole == ControlledRole::Survivor &&
        m_survivorLoadout.itemId == "flashlight" &&
        m_survivorItemState.active &&
        m_survivorItemState.charges > 0.0F)
    {
        fovDeg = 48.0F;
    }
    const glm::mat4 projection = glm::perspective(glm::radians(fovDeg), aspectRatio > 0.0F ? aspectRatio : (16.0F / 9.0F), 0.05F, 400.0F);
    return projection * view;
}

HudState GameplaySystems::BuildHudState() const
{
    HudState hud;
    hud.survivorStates.reserve(1);
    hud.debugActors.reserve(2);
    hud.mapName = m_activeMapName;
    hud.roleName = m_controlledRole == ControlledRole::Survivor ? "Survivor" : "Killer";
    hud.cameraModeName = CameraModeToName(ResolveCameraMode());
    hud.renderModeName = m_renderModeName;
    hud.interactionPrompt = m_interactionCandidate.prompt;
    hud.interactionTypeName = m_interactionCandidate.typeName;
    hud.interactionTargetName = m_interactionCandidate.targetName;
    hud.interactionPriority = m_interactionCandidate.priority;
    hud.survivorStateName = SurvivorStateToText(m_survivorState);
    hud.survivorStates.push_back(std::string{"[S1] "} + SurvivorStateToText(m_survivorState));
    hud.generatorsCompleted = m_generatorsCompleted;
    hud.generatorsTotal = m_generatorsTotal;
    hud.repairingGenerator = m_activeRepairGenerator != 0;
    hud.selfHealing = m_selfHealActive;
    hud.selfHealProgress = m_selfHealProgress;
    hud.killerAttackStateName = KillerAttackStateToText(m_killerAttackState);
    hud.attackHint = "LMB attack / RMB power-item";
    hud.lungeCharge01 = glm::clamp(
        m_killerLungeChargeSeconds / std::max(0.01F, m_killerLungeDurationSeconds),
        0.0F,
        1.0F
    );
    hud.terrorRadiusVisible = m_terrorRadiusVisible;
    const float perkModifier = m_perkSystem.GetTerrorRadiusModifier(engine::scene::Role::Killer);
    const float baseRadius = m_chase.isChasing ? m_terrorRadiusChaseMeters : m_terrorRadiusMeters;
    hud.terrorRadiusMeters = baseRadius + perkModifier;
    if (m_activeRepairGenerator != 0)
    {
        const auto generatorIt = m_world.Generators().find(m_activeRepairGenerator);
        if (generatorIt != m_world.Generators().end())
        {
            hud.activeGeneratorProgress = generatorIt->second.progress;
        }
    }
    hud.skillCheckActive = m_skillCheckActive;
    hud.skillCheckNeedle = m_skillCheckNeedle;
    hud.skillCheckSuccessStart = m_skillCheckSuccessStart;
    hud.skillCheckSuccessEnd = m_skillCheckSuccessEnd;
    hud.carryEscapeProgress = m_carryEscapeProgress;
    hud.hookStage = m_hookStage;
    hud.hookEscapeAttemptsUsed = m_hookEscapeAttemptsUsed;
    hud.hookEscapeAttemptsMax = m_hookEscapeAttemptsMax;
    hud.hookEscapeChance = m_hookEscapeChance;
    hud.hookCanAttemptEscape = (m_survivorState == SurvivorHealthState::Hooked && m_hookStage == 1);
    hud.hookSkillChecksEnabled = (m_survivorState == SurvivorHealthState::Hooked && m_hookStage == 2);
    if (m_hookStage > 0)
    {
        const float stageDuration =
            (m_hookStage == 1) ? m_hookStageOneDuration :
            (m_hookStage == 2) ? m_hookStageTwoDuration :
                                 10.0F;
        hud.hookStageProgress = glm::clamp(m_hookStageTimer / stageDuration, 0.0F, 1.0F);
    }
    else
    {
        hud.hookStageProgress = 0.0F;
    }
    hud.runtimeMessage = m_messages.empty() ? std::string{} : m_messages.front().text;
    const engine::fx::FxStats fxStats = m_fxSystem.Stats();
    hud.fxActiveInstances = fxStats.activeInstances;
    hud.fxActiveParticles = fxStats.activeParticles;
    hud.fxCpuMs = fxStats.cpuMs;
    hud.survivorCharacterId = m_selectedSurvivorCharacterId;
    hud.killerCharacterId = m_selectedKillerCharacterId;
    hud.survivorItemId = m_survivorLoadout.itemId.empty() ? "none" : m_survivorLoadout.itemId;
    hud.survivorItemAddonA = m_survivorLoadout.addonAId.empty() ? "none" : m_survivorLoadout.addonAId;
    hud.survivorItemAddonB = m_survivorLoadout.addonBId.empty() ? "none" : m_survivorLoadout.addonBId;
    const loadout::ItemDefinition* hudItemDef = m_loadoutCatalog.FindItem(m_survivorLoadout.itemId);
    float hudItemMaxCharges = 0.0F;
    if (hudItemDef != nullptr)
    {
        hudItemMaxCharges = hudItemDef->maxCharges;
        if (hudItemDef->id == "toolbox")
        {
            hudItemMaxCharges = m_tuning.toolboxCharges;
        }
        else if (hudItemDef->id == "flashlight")
        {
            hudItemMaxCharges = m_tuning.flashlightMaxUseSeconds;
        }
        else if (hudItemDef->id == "map")
        {
            hudItemMaxCharges = static_cast<float>(m_tuning.mapUses);
        }
        hudItemMaxCharges = std::max(0.0F, m_survivorItemModifiers.ApplyStat("max_charges", hudItemMaxCharges));
    }
    hud.survivorItemCharges = m_survivorItemState.charges;
    hud.survivorItemMaxCharges = hudItemMaxCharges;
    hud.survivorItemCharge01 = hudItemMaxCharges > 1.0e-4F ? glm::clamp(m_survivorItemState.charges / hudItemMaxCharges, 0.0F, 1.0F) : 0.0F;
    hud.survivorItemActive = m_survivorItemState.active;
    hud.survivorFlashlightAiming = m_survivorLoadout.itemId == "flashlight" && m_survivorItemState.active;
    hud.survivorFlashlightBlindBuild01 = glm::clamp(
        m_survivorItemState.flashBlindAccum / std::max(0.1F, m_tuning.flashlightBlindBuildSeconds),
        0.0F,
        1.0F
    );
    hud.survivorItemUsesRemaining = m_survivorItemState.mapUsesRemaining;
    if (m_survivorLoadout.itemId == "map")
    {
        hud.survivorItemUseProgress01 = glm::clamp(m_survivorItemState.mapChannelSeconds / std::max(0.05F, m_tuning.mapChannelSeconds), 0.0F, 1.0F);
    }
    else if (m_survivorLoadout.itemId == "flashlight")
    {
        hud.survivorItemUseProgress01 = hud.survivorFlashlightBlindBuild01;
    }
    else if (m_survivorLoadout.itemId == "medkit")
    {
        hud.survivorItemUseProgress01 = m_selfHealProgress;
    }
    hud.killerPowerId = m_killerLoadout.powerId.empty() ? "none" : m_killerLoadout.powerId;
    hud.killerPowerAddonA = m_killerLoadout.addonAId.empty() ? "none" : m_killerLoadout.addonAId;
    hud.killerPowerAddonB = m_killerLoadout.addonBId.empty() ? "none" : m_killerLoadout.addonBId;
    hud.activeTrapCount = static_cast<int>(m_world.BearTraps().size());
    hud.carriedTrapCount = m_killerPowerState.trapperCarriedTraps;
    const loadout::PowerDefinition* hudPowerDef = m_loadoutCatalog.FindPower(m_killerLoadout.powerId);
    float trapSetDuration = m_tuning.trapperSetTrapSeconds;
    if (hudPowerDef != nullptr)
    {
        auto it = hudPowerDef->params.find("set_duration");
        if (it != hudPowerDef->params.end())
        {
            trapSetDuration = it->second;
        }
    }
    trapSetDuration = std::max(0.2F, m_killerPowerModifiers.ApplyStat("set_duration", trapSetDuration));
    hud.trapSetProgress01 = m_killerPowerState.trapperSetting
        ? glm::clamp(m_killerPowerState.trapperSetTimer / std::max(0.01F, trapSetDuration), 0.0F, 1.0F)
        : 0.0F;
    hud.wraithCloaked = m_killerPowerState.wraithCloaked;
    hud.wraithPostUncloakHasteSeconds = m_killerPowerState.wraithPostUncloakTimer;
    if (m_killerPowerState.wraithCloakTransition)
    {
        hud.wraithCloakTransitionActive = true;
        hud.wraithCloakProgress01 = glm::clamp(
            m_killerPowerState.wraithTransitionTimer / std::max(0.01F, m_tuning.wraithCloakTransitionSeconds),
            0.0F, 1.0F);
        hud.wraithCloakAction = "Cloaking...";
    }
    else if (m_killerPowerState.wraithUncloakTransition)
    {
        hud.wraithCloakTransitionActive = true;
        hud.wraithCloakProgress01 = glm::clamp(
            m_killerPowerState.wraithTransitionTimer / std::max(0.01F, m_tuning.wraithUncloakTransitionSeconds),
            0.0F, 1.0F);
        hud.wraithCloakAction = "Uncloaking...";
    }
    else
    {
        hud.wraithCloakTransitionActive = false;
        hud.wraithCloakProgress01 = 0.0F;
        hud.wraithCloakAction.clear();
    }
    
    // Calculate cloak amount for shader (0 = visible, 1 = fully cloaked)
    if (m_killerPowerState.wraithCloaked)
    {
        hud.wraithCloakAmount = 1.0F;
    }
    else if (m_killerPowerState.wraithCloakTransition)
    {
        hud.wraithCloakAmount = hud.wraithCloakProgress01;
    }
    else if (m_killerPowerState.wraithUncloakTransition)
    {
        hud.wraithCloakAmount = 1.0F - hud.wraithCloakProgress01;
    }
    else
    {
        hud.wraithCloakAmount = 0.0F;
    }
    
    // Killer position and capsule info for cloak shader
    const auto killerTransformIt = m_world.Transforms().find(m_killer);
    const auto killerActorIt = m_world.Actors().find(m_killer);
    if (killerTransformIt != m_world.Transforms().end())
    {
        hud.killerWorldPosition = killerTransformIt->second.position;
    }
    if (killerActorIt != m_world.Actors().end())
    {
        hud.killerCapsuleHeight = killerActorIt->second.capsuleHeight;
        hud.killerCapsuleRadius = killerActorIt->second.capsuleRadius;
    }
    
    hud.trapDebugEnabled = m_trapDebugEnabled;
    hud.killerBlindRemaining = m_killerPowerState.killerBlindTimer;
    hud.killerBlindWhiteStyle = m_tuning.flashlightBlindStyle == 0;
    hud.killerStunRemaining = killerActorIt != m_world.Actors().end() ? killerActorIt->second.stunTimer : 0.0F;
    hud.trapIndicatorText = m_trapIndicatorText;
    hud.trapIndicatorTtl = m_trapIndicatorTimer;
    hud.trapIndicatorDanger = m_trapIndicatorDanger;

    // Hatchet power HUD fields
    hud.hatchetCount = m_killerPowerState.hatchetCount;
    hud.hatchetMaxCount = m_killerPowerState.hatchetMaxCount;
    hud.hatchetCharging = m_killerPowerState.hatchetCharging;
    hud.hatchetCharge01 = m_killerPowerState.hatchetCharge01;
    hud.hatchetDebugEnabled = m_hatchetDebugEnabled;
    hud.activeProjectileCount = GetActiveProjectileCount();
    hud.lockerReplenishProgress = m_killerPowerState.lockerReplenishing ?
        (m_killerPowerState.lockerReplenishTimer / m_tuning.hatchetLockerReplenishTime) : 0.0F;

    // Chainsaw sprint power HUD fields
    {
        auto stateToText = [](ChainsawSprintState state) -> std::string {
            switch (state)
            {
                case ChainsawSprintState::Idle: return "Idle";
                case ChainsawSprintState::Charging: return "Charging";
                case ChainsawSprintState::Sprinting: return "Sprinting";
                case ChainsawSprintState::Recovery: return "Recovery";
                default: return "Unknown";
            }
        };
        hud.chainsawState = stateToText(m_killerPowerState.chainsawState);
        hud.chainsawCharge01 = glm::clamp(
            m_killerPowerState.chainsawChargeTimer / std::max(0.01F, m_chainsawConfig.chargeTime),
            0.0F, 1.0F
        );
        hud.chainsawOverheat01 = glm::clamp(
            m_killerPowerState.chainsawOverheat / std::max(0.01F, m_chainsawConfig.overheatMax),
            0.0F, 1.0F
        );
        hud.chainsawSprintTimer = m_killerPowerState.chainsawSprintTimer;
        hud.chainsawSprintMaxDuration = 0.0F; // No max duration - sprint until collision/release/hit
        hud.chainsawCurrentSpeed = m_killerPowerState.chainsawCurrentSpeed;
        hud.chainsawDebugEnabled = m_chainsawDebugEnabled;

        // New HUD fields
        hud.chainsawTurnBoostActive = m_killerPowerState.chainsawInTurnBoostWindow;
        hud.chainsawRecoveryTimer = m_killerPowerState.chainsawRecoveryTimer;

        // Calculate recovery duration based on cause
        if (m_killerPowerState.chainsawRecoveryWasCollision)
        {
            hud.chainsawRecoveryDuration = m_chainsawConfig.collisionRecoveryDuration;
        }
        else if (m_killerPowerState.chainsawRecoveryWasHit)
        {
            hud.chainsawRecoveryDuration = m_chainsawConfig.recoveryHitDuration;
        }
        else
        {
            hud.chainsawRecoveryDuration = m_chainsawConfig.recoveryCancelDuration;
        }

        hud.chainsawOverheatBuffed = m_killerPowerState.chainsawOverheat >= m_chainsawConfig.overheatBuffThreshold;

        // Calculate current turn rate
        if (m_killerPowerState.chainsawState == ChainsawSprintState::Sprinting)
        {
            float turnRate = m_killerPowerState.chainsawInTurnBoostWindow
                ? m_chainsawConfig.turnBoostRate
                : m_chainsawConfig.turnRestrictedRate;
            if (hud.chainsawOverheatBuffed)
            {
                turnRate *= (1.0F + m_chainsawConfig.overheatTurnBonus);
            }
            hud.chainsawTurnRate = turnRate;
        }
        else
        {
            hud.chainsawTurnRate = m_chainsawConfig.turnRateDegreesPerSec;
        }
    }

    // Nurse blink power HUD fields
    if (hudPowerDef != nullptr && hudPowerDef->id == "nurse_blink")
    {
        auto blinkStateToText = [](NurseBlinkState state) -> std::string {
            switch (state)
            {
                case NurseBlinkState::Idle: return "Idle";
                case NurseBlinkState::ChargingBlink: return "Charging";
                case NurseBlinkState::BlinkTravel: return "Traveling";
                case NurseBlinkState::ChainWindow: return "Chain Window";
                case NurseBlinkState::BlinkAttackWindup: return "Attacking";
                case NurseBlinkState::Fatigue: return "Fatigue";
                default: return "Unknown";
            }
        };
        hud.blinkState = blinkStateToText(m_killerPowerState.blinkState);
        hud.blinkCharges = m_killerPowerState.blinkCharges;
        hud.blinkMaxCharges = m_killerPowerState.blinkMaxCharges;
        hud.blinkCharge01 = m_killerPowerState.blinkCharge01;
        hud.blinksUsedThisChain = m_killerPowerState.blinksUsedThisChain;
        hud.blinkDebugEnabled = m_blinkDebugEnabled;

        // Charge regeneration progress (when not at max)
        if (m_killerPowerState.blinkCharges < m_killerPowerState.blinkMaxCharges)
        {
            hud.blinkChargeRegen01 = m_killerPowerState.blinkChargeRegenTimer / std::max(0.01F, m_blinkConfig.chargeRegenSeconds);
        }
        else
        {
            hud.blinkChargeRegen01 = 1.0F;
        }

        // Current blink distance based on charge
        hud.blinkDistanceMeters = m_blinkConfig.minBlinkDistance +
            m_killerPowerState.blinkCharge01 * (m_blinkConfig.maxBlinkDistance - m_blinkConfig.minBlinkDistance);

        // Chain window progress
        if (m_killerPowerState.blinkState == NurseBlinkState::ChainWindow)
        {
            hud.blinkChainWindow01 = m_killerPowerState.blinkChainWindowTimer / std::max(0.01F, m_blinkConfig.chainWindowSeconds);
        }
        else
        {
            hud.blinkChainWindow01 = 0.0F;
        }

        // Fatigue progress
        if (m_killerPowerState.blinkState == NurseBlinkState::Fatigue)
        {
            const float fatigueDuration = m_blinkConfig.fatigueBaseSeconds +
                (static_cast<float>(m_killerPowerState.blinksUsedThisChain) * m_blinkConfig.fatiguePerBlinkUsedSeconds);
            hud.blinkFatigue01 = m_killerPowerState.blinkFatigueTimer / std::max(0.01F, fatigueDuration);
            hud.blinkFatigueDuration = fatigueDuration;
        }
        else
        {
            hud.blinkFatigue01 = 0.0F;
            hud.blinkFatigueDuration = 0.0F;
        }
    }

    // Check if killer is near a locker
    hud.lockerInRange = false;
    if (m_killer != 0 && m_controlledRole == ControlledRole::Killer)
    {
        const auto killerTransformIt = m_world.Transforms().find(m_killer);
        if (killerTransformIt != m_world.Transforms().end())
        {
            for (const auto& [entity, locker] : m_world.Lockers())
            {
                const auto lockerTransformIt = m_world.Transforms().find(entity);
                if (lockerTransformIt != m_world.Transforms().end())
                {
                    const float distance = DistanceXZ(killerTransformIt->second.position, lockerTransformIt->second.position);
                    if (distance < 2.0F)
                    {
                        hud.lockerInRange = true;
                        break;
                    }
                }
            }
        }
    }

    if (m_survivorState == SurvivorHealthState::Trapped)
    {
        for (const auto& [entity, trap] : m_world.BearTraps())
        {
            (void)entity;
            if (trap.trappedEntity != m_survivor)
            {
                continue;
            }
            hud.trappedEscapeAttempts = trap.escapeAttempts;
            hud.trappedEscapeChance = trap.escapeChance;
            hud.interactionPrompt = "TRAPPED: Press E to attempt escape";
            hud.interactionTypeName = "TrapEscape";
            hud.interactionTargetName = "BearTrap";
            break;
        }
    }
    if (m_controlledRole == ControlledRole::Survivor && m_survivorState == SurvivorHealthState::Carried)
    {
        hud.interactionPrompt = "Wiggle: Alternate A/D to escape";
        hud.interactionTypeName = "CarryEscape";
        hud.interactionTargetName = "Self";
    }
    else if (m_controlledRole == ControlledRole::Survivor && m_survivorState == SurvivorHealthState::Hooked)
    {
        if (m_hookStage == 1)
        {
            const int attemptsLeft = std::max(0, m_hookEscapeAttemptsMax - m_hookEscapeAttemptsUsed);
            hud.interactionPrompt =
                "Press E: Attempt self-unhook (4%) | Attempts left: " + std::to_string(attemptsLeft);
            hud.interactionTypeName = "HookAttemptEscape";
            hud.interactionTargetName = "Hook";
        }
        else if (m_hookStage == 2)
        {
            hud.interactionPrompt = "Struggle: hit SPACE on skill checks";
            hud.interactionTypeName = "HookStruggle";
            hud.interactionTargetName = "Hook";
        }
    }
    if (m_controlledRole == ControlledRole::Survivor &&
        hud.interactionPrompt.empty() &&
        (m_survivorState == SurvivorHealthState::Healthy ||
         m_survivorState == SurvivorHealthState::Injured ||
         m_survivorState == SurvivorHealthState::Downed))
    {
        const auto survivorTransformIt = m_world.Transforms().find(m_survivor);
        if (survivorTransformIt != m_world.Transforms().end())
        {
            const engine::scene::Entity nearbyItem = FindNearestGroundItem(survivorTransformIt->second.position, 2.2F);
            if (nearbyItem != 0)
            {
                const auto itemIt = m_world.GroundItems().find(nearbyItem);
                if (itemIt != m_world.GroundItems().end())
                {
                    const std::string label = itemIt->second.itemId.empty() ? "item" : itemIt->second.itemId;
                    if (!m_survivorLoadout.itemId.empty())
                    {
                        hud.interactionPrompt = "Press E to swap " + m_survivorLoadout.itemId + " with " + label;
                        hud.interactionTypeName = "ItemSwap";
                    }
                    else
                    {
                        hud.interactionPrompt = "Press LMB to pick up " + label;
                        hud.interactionTypeName = "ItemPickup";
                    }
                    hud.interactionTargetName = label;
                }
            }

            if (hud.interactionPrompt.empty())
            {
                engine::scene::Entity nearbyTrap = 0;
                if (TryFindNearestTrap(survivorTransformIt->second.position, 1.9F, false, &nearbyTrap))
                {
                    const auto trapIt = m_world.BearTraps().find(nearbyTrap);
                    if (trapIt != m_world.BearTraps().end() && trapIt->second.state == engine::scene::TrapState::Armed)
                    {
                        hud.interactionPrompt = "Hold E to disarm trap";
                        hud.interactionTypeName = "TrapDisarm";
                        hud.interactionTargetName = "BearTrap";
                    }
                }
            }
        }
    }
    else if (m_controlledRole == ControlledRole::Killer && hud.interactionPrompt.empty())
    {
        if (m_killerLoadout.powerId == "bear_trap")
        {
            if (m_killerPowerState.trapperSetting)
            {
                hud.interactionPrompt = "Setting trap... hold RMB (release to cancel)";
                hud.interactionTypeName = "TrapSet";
                hud.interactionTargetName = "Ground";
            }
            else
            {
                const auto killerTransformIt = m_world.Transforms().find(m_killer);
                std::string prompt;
                if (m_killerPowerState.trapperCarriedTraps > 0)
                {
                    prompt = "Hold RMB to set trap";
                }
                if (killerTransformIt != m_world.Transforms().end())
                {
                    engine::scene::Entity nearbyDisarmedTrap = 0;
                    engine::scene::Entity nearbyTrap = 0;
                    if (TryFindNearestTrap(killerTransformIt->second.position, 2.4F, true, &nearbyDisarmedTrap) ||
                        TryFindNearestTrap(killerTransformIt->second.position, 2.4F, false, &nearbyTrap))
                    {
                        if (!prompt.empty())
                        {
                            prompt += " | ";
                        }
                        prompt += "E: pickup trap";
                        if (nearbyDisarmedTrap != 0)
                        {
                            prompt += " | RMB: re-arm";
                        }
                    }
                }
                if (!prompt.empty())
                {
                    hud.interactionPrompt = prompt;
                    hud.interactionTypeName = "TrapPower";
                    hud.interactionTargetName = "BearTrap";
                }
            }
        }
        else if (m_killerLoadout.powerId == "wraith_cloak")
        {
            hud.interactionPrompt = m_killerPowerState.wraithCloaked ? "Press RMB to uncloak" : "Press RMB to cloak";
            hud.interactionTypeName = "WraithPower";
            hud.interactionTargetName = "Self";
        }
    }

    const engine::scene::Entity controlledEntity = ControlledEntity();
    const auto controlledTransformIt = m_world.Transforms().find(controlledEntity);
    if (controlledTransformIt != m_world.Transforms().end() && !m_loopDebugTiles.empty())
    {
        float bestDistance = std::numeric_limits<float>::max();
        const LoopDebugTile* bestTile = nullptr;
        for (const LoopDebugTile& tile : m_loopDebugTiles)
        {
            const float distance = DistanceXZ(controlledTransformIt->second.position, tile.center);
            if (distance < bestDistance)
            {
                bestDistance = distance;
                bestTile = &tile;
            }
        }

        if (bestTile != nullptr)
        {
            hud.activeLoopTileId = bestTile->loopId;
            switch (bestTile->archetype)
            {
                case 0: hud.activeLoopArchetype = "JungleGymLong"; break;
                case 1: hud.activeLoopArchetype = "JungleGymShort"; break;
                case 2: hud.activeLoopArchetype = "LTWalls"; break;
                case 3: hud.activeLoopArchetype = "Shack"; break;
                case 4: hud.activeLoopArchetype = "FourLane"; break;
                case 5: hud.activeLoopArchetype = "FillerA"; break;
                case 6: hud.activeLoopArchetype = "FillerB"; break;
                case 7: hud.activeLoopArchetype = "LongWall"; break;
                case 8: hud.activeLoopArchetype = "ShortWall"; break;
                case 9: hud.activeLoopArchetype = "LWallWindow"; break;
                case 10: hud.activeLoopArchetype = "LWallPallet"; break;
                case 11: hud.activeLoopArchetype = "TWalls"; break;
                case 12: hud.activeLoopArchetype = "GymBox"; break;
                case 13: hud.activeLoopArchetype = "DebrisPile"; break;
                default: hud.activeLoopArchetype = "Unknown"; break;
            }
        }
    }

    hud.chaseActive = m_chase.isChasing;
    hud.chaseDistance = m_chase.distance;
    hud.lineOfSight = m_chase.hasLineOfSight;
    hud.inCenterFOV = m_chase.inCenterFOV;
    hud.timeInChase = m_chase.timeInChase;
    hud.timeSinceLOS = m_chase.timeSinceSeenLOS;
    hud.timeSinceCenterFOV = m_chase.timeSinceCenterFOV;

    // Get survivor sprinting state
    const auto survivorActorIt = m_world.Actors().find(m_survivor);
    hud.survivorSprinting = (survivorActorIt != m_world.Actors().end()) && survivorActorIt->second.sprinting;

    // Bloodlust state
    hud.bloodlustTier = m_bloodlust.tier;
    hud.bloodlustSpeedMultiplier = GetBloodlustSpeedMultiplier();
    hud.killerBaseSpeed = m_tuning.killerMoveSpeed;
    hud.killerCurrentSpeed = m_tuning.killerMoveSpeed * m_killerSpeedPercent * hud.bloodlustSpeedMultiplier;

    // Phase B2/B3: Scratch marks and blood pools debug info
    hud.scratchActiveCount = GetActiveScratchCount();
    hud.bloodActiveCount = GetActiveBloodPoolCount();
    hud.scratchSpawnInterval = m_scratchNextInterval;

    hud.collisionEnabled = m_collisionEnabled;
    hud.debugDrawEnabled = m_debugDrawEnabled;
    hud.physicsDebugEnabled = m_physicsDebugEnabled;
    hud.noclipEnabled = m_noClipEnabled;
    hud.killerSurvivorNoCollisionActive = m_killerSurvivorNoCollisionTimer > 0.0F;
    hud.killerSurvivorNoCollisionTimer = m_killerSurvivorNoCollisionTimer;
    hud.killerSurvivorOverlapping = false;
    if (const auto killerTransformIt = m_world.Transforms().find(m_killer);
        killerTransformIt != m_world.Transforms().end())
    {
        if (const auto survivorTransformIt = m_world.Transforms().find(m_survivor);
            survivorTransformIt != m_world.Transforms().end())
        {
            if (const auto killerActorIt = m_world.Actors().find(m_killer);
                killerActorIt != m_world.Actors().end())
            {
                if (const auto survivorActorIt = m_world.Actors().find(m_survivor);
                    survivorActorIt != m_world.Actors().end())
                {
                    const float combinedRadius = std::max(
                        0.01F,
                        killerActorIt->second.capsuleRadius + survivorActorIt->second.capsuleRadius
                    );
                    const glm::vec2 delta{
                        survivorTransformIt->second.position.x - killerTransformIt->second.position.x,
                        survivorTransformIt->second.position.z - killerTransformIt->second.position.z
                    };
                    hud.killerSurvivorOverlapping = glm::dot(delta, delta) < (combinedRadius * combinedRadius);
                }
            }
        }
    }

    const engine::scene::Entity controlled = ControlledEntity();
    const auto actorIt = m_world.Actors().find(controlled);
    if (actorIt != m_world.Actors().end())
    {
        const engine::scene::ActorComponent& actor = actorIt->second;
        hud.playerSpeed = glm::length(glm::vec2{actor.velocity.x, actor.velocity.z});
        hud.grounded = actor.grounded;
        hud.velocity = actor.velocity;
        hud.lastCollisionNormal = actor.lastCollisionNormal;
        hud.penetrationDepth = actor.lastPenetrationDepth;
        hud.vaultTypeName = actor.lastVaultType;
        hud.movementStateName = BuildMovementStateText(controlled, actor);
        if (controlled == m_survivor)
        {
            hud.survivorVisualYawDeg = glm::degrees(m_survivorVisualYawRadians);
            hud.survivorVisualTargetYawDeg = glm::degrees(m_survivorVisualTargetYawRadians);
            hud.survivorLookYawDeg = glm::degrees(m_world.Transforms().contains(m_survivor)
                                                      ? m_world.Transforms().at(m_survivor).rotationEuler.y
                                                      : 0.0F);
            const glm::vec3 cameraFlat{m_cameraForward.x, 0.0F, m_cameraForward.z};
            hud.survivorCameraYawDeg = glm::degrees(
                glm::length(cameraFlat) > 1.0e-5F ? std::atan2(cameraFlat.x, -cameraFlat.z) : 0.0F
            );
            hud.survivorMoveInput = m_survivorVisualMoveInput;
        }

        // Populate perk debug info for both roles
        const auto populatePerkDebug = [&](engine::scene::Role role, std::vector<HudState::ActivePerkDebug>& outDebug, float& outSpeedMod) {
            const auto& activePerkStates = m_perkSystem.GetActivePerks(role);
            outDebug.reserve(activePerkStates.size());
            for (const auto& state : activePerkStates)
            {
                const auto* perk = m_perkSystem.GetPerk(state.perkId);
                if (!perk) continue;

                HudState::ActivePerkDebug debug;
                debug.id = state.perkId;
                debug.name = perk->name;
                debug.isActive = state.isActive;
                debug.activeRemainingSeconds = state.activeRemainingSeconds;
                debug.cooldownRemainingSeconds = state.cooldownRemainingSeconds;
                debug.stacks = state.currentStacks;
                outDebug.push_back(debug);
            }

            // Get speed modifier for display (sample with sprint=true to show max effect)
            outSpeedMod = m_perkSystem.GetSpeedModifier(role, true, false, false);
        };

        populatePerkDebug(engine::scene::Role::Survivor, hud.activePerksSurvivor, hud.speedModifierSurvivor);
        populatePerkDebug(engine::scene::Role::Killer, hud.activePerksKiller, hud.speedModifierKiller);
    }

    auto pushDebugLabel = [&](engine::scene::Entity entity, const std::string& name, bool killer) {
        const auto transformIt = m_world.Transforms().find(entity);
        const auto actorDebugIt = m_world.Actors().find(entity);
        if (transformIt == m_world.Transforms().end() || actorDebugIt == m_world.Actors().end())
        {
            return;
        }

        HudState::DebugActorLabel label;
        label.name = name;
        label.healthState = killer ? "-" : SurvivorStateToText(m_survivorState);
        label.movementState = BuildMovementStateText(entity, actorDebugIt->second);
        label.attackState = killer ? KillerAttackStateToText(m_killerAttackState) : "-";
        label.worldPosition = transformIt->second.position + glm::vec3{0.0F, 2.2F, 0.0F};
        label.forward = transformIt->second.forward;
        label.speed = glm::length(glm::vec2{actorDebugIt->second.velocity.x, actorDebugIt->second.velocity.z});
        label.chasing = m_chase.isChasing;
        label.killer = killer;
        hud.debugActors.push_back(label);
    };
    pushDebugLabel(m_survivor, "Player1", false);
    pushDebugLabel(m_killer, "Player2", true);

    // Phase B4: Killer look light debug info
    hud.killerLightEnabled = m_killerLookLight.enabled;
    hud.killerLightRange = m_killerLookLight.range;
    hud.killerLightIntensity = m_killerLookLight.intensity;
    hud.killerLightInnerAngle = m_killerLookLight.innerAngleDegrees;
    hud.killerLightOuterAngle = m_killerLookLight.outerAngleDegrees;
    hud.killerLightPitch = m_killerLookLight.pitchDegrees;

    // Populate perk slots from actual loadouts
    const auto& survivorLoadout = m_perkSystem.GetSurvivorLoadout();
    const auto& killerLoadout = m_perkSystem.GetKillerLoadout();
    
    // Helper to get active state for a perk
    const auto getActiveState = [&](const std::string& perkId, engine::scene::Role role) -> const perks::ActivePerkState* {
        if (perkId.empty()) return nullptr;
        const auto& activePerks = m_perkSystem.GetActivePerks(role);
        for (const auto& state : activePerks)
        {
            if (state.perkId == perkId)
            {
                return &state;
            }
        }
        return nullptr;
    };

    // Populate survivor perk slots (loadout has 3 slots, HUD has 4)
    for (int i = 0; i < 4; ++i)
    {
        const std::string perkId = (i < 3) ? survivorLoadout.GetPerk(i) : "";
        if (perkId.empty())
        {
            hud.survivorPerkSlots[i] = HudState::ActivePerkDebug{}; // Empty slot
        }
        else
        {
            const auto* perk = m_perkSystem.GetPerk(perkId);
            const auto* activeState = getActiveState(perkId, engine::scene::Role::Survivor);
            hud.survivorPerkSlots[i] = HudState::ActivePerkDebug{
                perkId,
                perk ? perk->name : perkId,
                activeState ? activeState->isActive : false,
                activeState ? activeState->activeRemainingSeconds : 0.0F,
                activeState ? activeState->cooldownRemainingSeconds : 0.0F,
                activeState ? activeState->currentStacks : 0,
                1, // Default tier (TODO: implement tier system)
                perk ? perk->effects.activationCooldownSeconds : 0.0F
            };
            // std::cout << "[PERK] SURVIVOR SLOT " << i << ": " << perkId << " (active=" << hud.survivorPerkSlots[i].isActive << ")\n";
        }
    }

    // Populate killer perk slots
    for (int i = 0; i < 4; ++i)
    {
        const std::string perkId = (i < 3) ? killerLoadout.GetPerk(i) : "";
        if (perkId.empty())
        {
            hud.killerPerkSlots[i] = HudState::ActivePerkDebug{}; // Empty slot
        }
        else
        {
            const auto* perk = m_perkSystem.GetPerk(perkId);
            const auto* activeState = getActiveState(perkId, engine::scene::Role::Killer);
            hud.killerPerkSlots[i] = HudState::ActivePerkDebug{
                perkId,
                perk ? perk->name : perkId,
                activeState ? activeState->isActive : false,
                activeState ? activeState->activeRemainingSeconds : 0.0F,
                activeState ? activeState->cooldownRemainingSeconds : 0.0F,
                activeState ? activeState->currentStacks : 0,
                1, // Default tier
                perk ? perk->effects.activationCooldownSeconds : 0.0F
            };
            // std::cout << "[PERK] KILLER SLOT " << i << ": " << perkId << " (active=" << hud.killerPerkSlots[i].isActive << ")\n";
        }
    }

    // Populate status effects for HUD display
    const auto populateStatusEffects = [&](engine::scene::Entity entity, std::vector<HudState::ActiveStatusEffect>& outEffects) {
        const auto effects = m_statusEffectManager.GetActiveEffects(entity);
        outEffects.reserve(effects.size());
        for (const auto& effect : effects)
        {
            HudState::ActiveStatusEffect hudEffect;
            hudEffect.typeId = StatusEffect::TypeToId(effect.type);
            hudEffect.displayName = StatusEffect::TypeToName(effect.type);
            hudEffect.remainingSeconds = effect.remainingTime;
            hudEffect.progress01 = effect.Progress01();
            hudEffect.strength = effect.strength;
            hudEffect.stacks = effect.stacks;
            hudEffect.isInfinite = effect.infinite;
            outEffects.push_back(hudEffect);
        }
    };

    if (m_killer != 0)
    {
        populateStatusEffects(m_killer, hud.killerStatusEffects);
        hud.killerUndetectable = m_statusEffectManager.IsUndetectable(m_killer);
    }

    if (m_survivor != 0)
    {
        populateStatusEffects(m_survivor, hud.survivorStatusEffects);
        hud.survivorExposed = m_statusEffectManager.IsExposed(m_survivor);
        hud.survivorExhausted = m_statusEffectManager.IsExhausted(m_survivor);
    }

    // Animation debug info
    hud.animState = engine::animation::LocomotionStateToString(m_animationSystem.CurrentState());
    hud.animPlaybackSpeed = m_animationSystem.CurrentPlaybackSpeed();
    hud.animBlending = m_animationSystem.GetStateMachine().IsBlending();
    hud.animBlendWeight = m_animationSystem.GetStateMachine().BlendWeight();
    hud.animAutoMode = m_animationSystem.GetStateMachine().IsAutoMode();
    const auto* currentClip = m_animationSystem.GetStateMachine().GetBlender().GetCurrentClip();
    if (currentClip != nullptr)
    {
        hud.animClip = currentClip->name;
    }
    hud.animClipList = m_animationSystem.ListClips();

    return hud;
}
void GameplaySystems::LoadMap(const std::string& mapName)
{
    m_staticBatcher.Clear();

    if (mapName == "test")
    {
        BuildSceneFromMap(MapType::Test, m_generationSeed);
    }
    else if (mapName == "main" || mapName == "main_map")
    {
        BuildSceneFromMap(MapType::Main, m_generationSeed);
    }
    else if (mapName == "collision_test")
    {
        BuildSceneFromMap(MapType::CollisionTest, m_generationSeed);
    }
    else if (mapName == "benchmark")
    {
        BuildSceneFromMap(MapType::Benchmark, m_generationSeed);
    }
    else
    {
        ::game::maps::GeneratedMap generated;
        std::string error;
        if (editor::LevelAssetIO::BuildGeneratedMapFromMapName(mapName, &generated, &error))
        {
            BuildSceneFromGeneratedMap(generated, MapType::Test, m_generationSeed, mapName);
        }
        else
        {
            AddRuntimeMessage("Map load failed: " + error, 2.4F);
            BuildSceneFromMap(MapType::Test, m_generationSeed);
        }
    }
}

void GameplaySystems::RegenerateLoops()
{
    RegenerateLoops(m_generationSeed + 1U);
}

void GameplaySystems::RegenerateLoops(unsigned int seed)
{
    m_generationSeed = seed;
    if (m_currentMap == MapType::Main && m_activeMapName == "main")
    {
        BuildSceneFromMap(MapType::Main, m_generationSeed);
    }
}

void GameplaySystems::SetDbdSpawnsEnabled(bool enabled)
{
    m_dbdSpawnsEnabled = enabled;
    // Regenerate current map with new spawn settings
    if (m_currentMap == MapType::Main && m_activeMapName == "main")
    {
        BuildSceneFromMap(MapType::Main, m_generationSeed);
        AddRuntimeMessage(std::string("DBD spawns ") + (enabled ? "enabled" : "disabled"), 2.0F);
    }
    else
    {
        AddRuntimeMessage("Load main map first to use DBD spawns", 2.0F);
    }
}

void GameplaySystems::SpawnSurvivor()
{
    if (!RespawnRole("survivor"))
    {
        AddRuntimeMessage("Spawn survivor failed", 1.4F);
    }
}

void GameplaySystems::SpawnKiller()
{
    if (!RespawnRole("killer"))
    {
        AddRuntimeMessage("Spawn killer failed", 1.4F);
    }
}

void GameplaySystems::SpawnPallet()
{
    glm::vec3 spawnPosition{0.0F, 1.05F, 0.0F};
    if (m_survivor != 0)
    {
        const auto transformIt = m_world.Transforms().find(m_survivor);
        if (transformIt != m_world.Transforms().end())
        {
            const glm::vec3 forward = glm::normalize(glm::vec3{transformIt->second.forward.x, 0.0F, transformIt->second.forward.z});
            spawnPosition = transformIt->second.position + forward * 2.0F;
            spawnPosition.y = 1.05F;
        }
    }

    const engine::scene::Entity palletEntity = m_world.CreateEntity();
    m_world.Transforms()[palletEntity] = engine::scene::Transform{spawnPosition, glm::vec3{0.0F}, glm::vec3{1.0F}, glm::vec3{1.0F, 0.0F, 0.0F}};
    engine::scene::PalletComponent pallet;
    pallet.halfExtents = pallet.standingHalfExtents;
    m_world.Pallets()[palletEntity] = pallet;
}

void GameplaySystems::SpawnWindow(std::optional<float> yawDegrees)
{
    constexpr glm::vec3 kWindowHalfExtents{1.2F, 1.35F, 0.2F};
    constexpr const char* kWindowMeshPath = "assets/meshes/loop_elements/Window.glb";

    glm::vec3 spawnPosition{0.0F, kWindowHalfExtents.y, 0.0F};
    glm::vec3 placementForward{0.0F, 0.0F, 1.0F};

    engine::scene::Entity sourceEntity = ControlledEntity();
    if (sourceEntity == 0)
    {
        sourceEntity = m_survivor != 0 ? m_survivor : m_killer;
    }

    if (sourceEntity != 0)
    {
        const auto transformIt = m_world.Transforms().find(sourceEntity);
        if (transformIt != m_world.Transforms().end())
        {
            glm::vec3 forward{transformIt->second.forward.x, 0.0F, transformIt->second.forward.z};
            if (glm::length(forward) < 1.0e-4F)
            {
                forward = glm::vec3{m_cameraForward.x, 0.0F, m_cameraForward.z};
            }
            if (glm::length(forward) < 1.0e-4F)
            {
                forward = glm::vec3{0.0F, 0.0F, 1.0F};
            }
            forward = glm::normalize(forward);

            spawnPosition = transformIt->second.position + forward * 2.4F;
            placementForward = forward;
        }
    }
    else
    {
        glm::vec3 forward{m_cameraForward.x, 0.0F, m_cameraForward.z};
        if (glm::length(forward) < 1.0e-4F)
        {
            forward = glm::vec3{0.0F, 0.0F, 1.0F};
        }
        forward = glm::normalize(forward);
        spawnPosition = m_cameraPosition + forward * 2.4F;
        placementForward = forward;
    }

    glm::vec3 normal = placementForward;
    if (yawDegrees.has_value())
    {
        const float yawRad = glm::radians(*yawDegrees);
        normal = glm::normalize(glm::vec3{std::sin(yawRad), 0.0F, std::cos(yawRad)});
    }
    if (glm::length(normal) < 1.0e-4F)
    {
        normal = glm::vec3{0.0F, 0.0F, 1.0F};
    }

    // Try to read mesh bounds so we can place bottom vertices exactly on floor.
    float meshMinY = -kWindowHalfExtents.y;
    {
        static engine::assets::MeshLibrary fallbackMeshLibrary;
        engine::assets::MeshLibrary* meshLibrary = (m_meshLibrary != nullptr) ? m_meshLibrary : &fallbackMeshLibrary;

        std::error_code ec;
        std::filesystem::path meshPath = std::filesystem::current_path(ec) / kWindowMeshPath;
        if (!std::filesystem::exists(meshPath))
        {
            meshPath = std::filesystem::current_path(ec) / "assets/meshes/loop_elements/Window.glb";
        }
        std::string loadError;
        if (const engine::assets::MeshData* meshData = meshLibrary->LoadMesh(meshPath, &loadError);
            meshData != nullptr && meshData->loaded)
        {
            meshMinY = meshData->boundsMin.y;
        }
    }

    // Snap vertically so bottom of window mesh sits on top of floor.
    {
        const glm::vec3 rayStart{spawnPosition.x, spawnPosition.y + 20.0F, spawnPosition.z};
        const glm::vec3 rayEnd{spawnPosition.x, spawnPosition.y - 60.0F, spawnPosition.z};
        if (const std::optional<engine::physics::RaycastHit> hit = m_physics.RaycastNearest(rayStart, rayEnd); hit.has_value())
        {
            spawnPosition.y = hit->position.y - meshMinY;
        }
        else
        {
            spawnPosition.y = std::max(spawnPosition.y, std::max(kWindowHalfExtents.y, -meshMinY));
        }
    }

    const engine::scene::Entity windowEntity = m_world.CreateEntity();
    m_world.Transforms()[windowEntity] = engine::scene::Transform{spawnPosition, glm::vec3{0.0F}, glm::vec3{1.0F}, normal};

    engine::scene::WindowComponent window;
    // Inner vault volume matches mesh footprint in XZ. Y stays gameplay-tuned.
    window.halfExtents = kWindowHalfExtents;
    window.normal = glm::length(normal) > 0.001F ? glm::normalize(normal) : glm::vec3{0.0F, 0.0F, 1.0F};
    m_world.Windows()[windowEntity] = window;

    // Spawn visible window mesh bound to the same pose as the vault trigger.
    // Keep collision disabled here - gameplay uses WindowComponent + vault trigger.
    const float windowYawDegrees = glm::degrees(std::atan2(window.normal.x, window.normal.z));
    m_loopMeshes.push_back(LoopMeshInstance{
        kWindowMeshPath,
        engine::render::Renderer::kInvalidGpuMesh,
        spawnPosition,
        windowYawDegrees,
        glm::vec3{window.halfExtents.x, window.halfExtents.y, window.halfExtents.z},
        true
    });
    m_loopMeshesUploaded = false;

    RebuildPhysicsWorld();
    UpdateInteractionCandidate();
}

bool GameplaySystems::SpawnRoleHere(const std::string& roleName)
{
    const std::string normalizedRole = roleName == "killer" ? "killer" : "survivor";
    const SpawnPointType spawnType = SpawnPointTypeFromRole(normalizedRole);

    glm::vec3 desired = m_cameraPosition + m_cameraForward * 3.0F;
    glm::vec3 rayStart = desired + glm::vec3{0.0F, 20.0F, 0.0F};
    glm::vec3 rayEnd = desired + glm::vec3{0.0F, -40.0F, 0.0F};
    if (const std::optional<engine::physics::RaycastHit> hit = m_physics.RaycastNearest(rayStart, rayEnd); hit.has_value())
    {
        desired = hit->position;
    }

    desired.y += 1.1F;

    const float radius = normalizedRole == "survivor" ? m_tuning.survivorCapsuleRadius : m_tuning.killerCapsuleRadius;
    const float height = normalizedRole == "survivor" ? m_tuning.survivorCapsuleHeight : m_tuning.killerCapsuleHeight;
    glm::vec3 resolved = desired;
    if (!ResolveSpawnPositionValid(desired, radius, height, &resolved))
    {
        if (const std::optional<SpawnPointInfo> fallback = FindSpawnPointByType(spawnType); fallback.has_value())
        {
            resolved = fallback->position;
        }
    }

    if (normalizedRole == "survivor")
    {
        DestroyEntity(m_survivor);
    }
    else
    {
        DestroyEntity(m_killer);
    }

    const engine::scene::Entity spawned = SpawnRoleActorAt(normalizedRole, resolved);
    if (spawned == 0)
    {
        return false;
    }
    RebuildPhysicsWorld();
    return true;
}

bool GameplaySystems::SpawnRoleAt(const std::string& roleName, int spawnId)
{
    const std::string normalizedRole = roleName == "killer" ? "killer" : "survivor";
    const std::optional<SpawnPointInfo> spawn = FindSpawnPointById(spawnId);
    if (!spawn.has_value())
    {
        return false;
    }

    glm::vec3 target = spawn->position;
    const float radius = normalizedRole == "survivor" ? m_tuning.survivorCapsuleRadius : m_tuning.killerCapsuleRadius;
    const float height = normalizedRole == "survivor" ? m_tuning.survivorCapsuleHeight : m_tuning.killerCapsuleHeight;
    glm::vec3 resolved = target;
    if (!ResolveSpawnPositionValid(target, radius, height, &resolved))
    {
        resolved = target;
    }

    if (normalizedRole == "survivor")
    {
        DestroyEntity(m_survivor);
    }
    else
    {
        DestroyEntity(m_killer);
    }

    const engine::scene::Entity spawned = SpawnRoleActorAt(normalizedRole, resolved);
    if (spawned == 0)
    {
        return false;
    }
    RebuildPhysicsWorld();
    return true;
}

bool GameplaySystems::RespawnRole(const std::string& roleName)
{
    const std::string normalizedRole = roleName == "killer" ? "killer" : "survivor";
    const SpawnPointType spawnType = SpawnPointTypeFromRole(normalizedRole);
    const std::optional<SpawnPointInfo> spawn = FindSpawnPointByType(spawnType);
    if (!spawn.has_value())
    {
        return false;
    }

    return SpawnRoleAt(normalizedRole, spawn->id);
}

std::string GameplaySystems::ListSpawnPoints() const
{
    std::ostringstream oss;
    if (m_spawnPoints.empty())
    {
        oss << "No spawn points";
        return oss.str();
    }

    for (const SpawnPointInfo& spawn : m_spawnPoints)
    {
        oss << "#" << spawn.id << " [" << SpawnTypeToText(spawn.type) << "] "
            << "(" << spawn.position.x << ", " << spawn.position.y << ", " << spawn.position.z << ")\n";
    }
    return oss.str();
}

std::vector<GameplaySystems::SpawnPointInfo> GameplaySystems::GetSpawnPoints() const
{
    return m_spawnPoints;
}

engine::scene::Entity GameplaySystems::RoleEntity(const std::string& roleName) const
{
    if (roleName == "killer")
    {
        return m_killer;
    }
    return m_survivor;
}

std::string GameplaySystems::MovementStateForRole(const std::string& roleName) const
{
    const engine::scene::Entity entity = RoleEntity(roleName);
    const auto actorIt = m_world.Actors().find(entity);
    if (actorIt == m_world.Actors().end())
    {
        return "None";
    }
    return BuildMovementStateText(entity, actorIt->second);
}

glm::vec3 GameplaySystems::RolePosition(const std::string& roleName) const
{
    const engine::scene::Entity entity = RoleEntity(roleName);
    const auto transformIt = m_world.Transforms().find(entity);
    if (transformIt == m_world.Transforms().end())
    {
        return glm::vec3{0.0F};
    }
    return transformIt->second.position;
}

glm::vec3 GameplaySystems::RoleForward(const std::string& roleName) const
{
    const engine::scene::Entity entity = RoleEntity(roleName);
    const auto transformIt = m_world.Transforms().find(entity);
    if (transformIt == m_world.Transforms().end())
    {
        return glm::vec3{0.0F, 0.0F, -1.0F};
    }
    const glm::vec3 f = transformIt->second.forward;
    if (glm::length(f) < 1.0e-5F)
    {
        return glm::vec3{0.0F, 0.0F, -1.0F};
    }
    return glm::normalize(f);
}

std::string GameplaySystems::SurvivorHealthStateText() const
{
    return SurvivorStateToText(m_survivorState);
}

void GameplaySystems::TeleportSurvivor(const glm::vec3& position)
{
    if (m_survivor == 0)
    {
        SpawnSurvivor();
    }

    auto transformIt = m_world.Transforms().find(m_survivor);
    if (transformIt != m_world.Transforms().end())
    {
        transformIt->second.position = position;
    }
}

void GameplaySystems::TeleportKiller(const glm::vec3& position)
{
    if (m_killer == 0)
    {
        SpawnKiller();
    }

    auto transformIt = m_world.Transforms().find(m_killer);
    if (transformIt != m_world.Transforms().end())
    {
        transformIt->second.position = position;
    }
}

void GameplaySystems::SetSurvivorSprintSpeed(float speed)
{
    if (m_survivor == 0)
    {
        return;
    }

    auto actorIt = m_world.Actors().find(m_survivor);
    if (actorIt != m_world.Actors().end())
    {
        m_tuning.survivorSprintSpeed = std::max(0.1F, speed);
        actorIt->second.sprintSpeed = m_tuning.survivorSprintSpeed * m_survivorSpeedPercent;
        actorIt->second.walkSpeed = m_tuning.survivorWalkSpeed * m_survivorSpeedPercent;
    }
}

void GameplaySystems::SetRoleSpeedPercent(const std::string& roleName, float percent)
{
    const float clamped = glm::clamp(percent, 0.2F, 4.0F);
    if (roleName == "survivor")
    {
        m_survivorSpeedPercent = clamped;
        auto it = m_world.Actors().find(m_survivor);
        if (it != m_world.Actors().end())
        {
            it->second.sprintSpeed = m_tuning.survivorSprintSpeed * m_survivorSpeedPercent;
            it->second.walkSpeed = m_tuning.survivorWalkSpeed * m_survivorSpeedPercent;
        }
        return;
    }

    if (roleName == "killer")
    {
        m_killerSpeedPercent = clamped;
        auto it = m_world.Actors().find(m_killer);
        if (it != m_world.Actors().end())
        {
            // Apply bloodlust multiplier ON TOP of base speed
            const float bloodlustMult = GetBloodlustSpeedMultiplier();
            const float finalSpeed = m_tuning.killerMoveSpeed * m_killerSpeedPercent * bloodlustMult;
            it->second.walkSpeed = finalSpeed;
            it->second.sprintSpeed = finalSpeed;
        }
    }
}

void GameplaySystems::SetRoleCapsuleSize(const std::string& roleName, float radius, float height)
{
    const float r = glm::clamp(radius, 0.2F, 1.2F);
    const float h = glm::clamp(height, 0.9F, 3.2F);

    auto apply = [&](engine::scene::Entity entity) {
        auto it = m_world.Actors().find(entity);
        if (it == m_world.Actors().end())
        {
            return;
        }
        it->second.capsuleRadius = r;
        it->second.capsuleHeight = h;
        it->second.eyeHeight = std::max(0.8F, h * 0.88F);
    };

    if (roleName == "survivor")
    {
        apply(m_survivor);
    }
    else if (roleName == "killer")
    {
        apply(m_killer);
    }
}

void GameplaySystems::ToggleCollision(bool enabled)
{
    m_collisionEnabled = enabled;
    for (auto& [_, actor] : m_world.Actors())
    {
        actor.collisionEnabled = enabled;
    }
}

void GameplaySystems::ToggleDebugDraw(bool enabled)
{
    m_debugDrawEnabled = enabled;
}

void GameplaySystems::TogglePhysicsDebug(bool enabled)
{
    m_physicsDebugEnabled = enabled;
}

void GameplaySystems::SetNoClip(bool enabled)
{
    m_noClipEnabled = enabled;
    for (auto& [_, actor] : m_world.Actors())
    {
        actor.noclipEnabled = enabled;
    }
}

void GameplaySystems::SetForcedChase(bool enabled)
{
    m_forcedChase = enabled;
    if (!enabled)
    {
        // Reset timers when disabling forced chase
        m_chase.timeSinceSeenLOS = 0.0F;
        m_chase.timeSinceCenterFOV = 0.0F;
    }
}

void GameplaySystems::SetSurvivorPerkLoadout(const perks::PerkLoadout& loadout)
{
    m_survivorPerks = loadout;
    m_perkSystem.SetSurvivorLoadout(loadout);
    m_perkSystem.InitializeActiveStates();

    if (!m_survivorPerks.IsEmpty())
    {
        std::cout << "GameplaySystems: Set survivor perk loadout with " << m_survivorPerks.GetSlotCount() << " perks\n";
    }
}

void GameplaySystems::SetKillerPerkLoadout(const perks::PerkLoadout& loadout)
{
    m_killerPerks = loadout;
    m_perkSystem.SetKillerLoadout(loadout);
    m_perkSystem.InitializeActiveStates();

    if (!m_killerPerks.IsEmpty())
    {
        std::cout << "GameplaySystems: Set killer perk loadout with " << m_killerPerks.GetSlotCount() << " perks\n";
    }
}

void GameplaySystems::ToggleTerrorRadiusVisualization(bool enabled)
{
    m_terrorRadiusVisible = enabled;
}

void GameplaySystems::SetTerrorRadius(float meters)
{
    m_terrorRadiusMeters = std::max(1.0F, meters);
}

void GameplaySystems::SetCameraModeOverride(const std::string& modeName)
{
    if (modeName == "survivor")
    {
        m_cameraOverride = CameraOverride::SurvivorThirdPerson;
    }
    else if (modeName == "killer")
    {
        m_cameraOverride = CameraOverride::KillerFirstPerson;
    }
    else
    {
        m_cameraOverride = CameraOverride::RoleBased;
    }
}

void GameplaySystems::SetControlledRole(const std::string& roleName)
{
    if (roleName == "survivor")
    {
        m_controlledRole = ControlledRole::Survivor;
    }
    else if (roleName == "killer")
    {
        m_controlledRole = ControlledRole::Killer;
    }
}

void GameplaySystems::ToggleControlledRole()
{
    m_controlledRole = (m_controlledRole == ControlledRole::Survivor) ? ControlledRole::Killer : ControlledRole::Survivor;
}

void GameplaySystems::SetRenderModeLabel(const std::string& modeName)
{
    m_renderModeName = modeName;
}

void GameplaySystems::SetLookSettings(float survivorSensitivity, float killerSensitivity, bool invertY)
{
    m_survivorLookSensitivity = glm::clamp(survivorSensitivity, 0.0001F, 0.02F);
    m_killerLookSensitivity = glm::clamp(killerSensitivity, 0.0001F, 0.02F);
    m_invertLookY = invertY;
}

void GameplaySystems::ApplyGameplayTuning(const GameplayTuning& tuning)
{
    m_tuning = tuning;

    m_tuning.survivorWalkSpeed = glm::clamp(m_tuning.survivorWalkSpeed, 0.5F, 10.0F);
    m_tuning.survivorSprintSpeed = glm::clamp(m_tuning.survivorSprintSpeed, m_tuning.survivorWalkSpeed, 14.0F);
    m_tuning.survivorCrouchSpeed = glm::clamp(m_tuning.survivorCrouchSpeed, 0.2F, m_tuning.survivorWalkSpeed);
    m_tuning.survivorCrawlSpeed = glm::clamp(m_tuning.survivorCrawlSpeed, 0.1F, m_tuning.survivorWalkSpeed);
    m_tuning.killerMoveSpeed = glm::clamp(m_tuning.killerMoveSpeed, 0.5F, 16.0F);

    m_tuning.survivorCapsuleRadius = glm::clamp(m_tuning.survivorCapsuleRadius, 0.2F, 1.2F);
    m_tuning.survivorCapsuleHeight = glm::clamp(m_tuning.survivorCapsuleHeight, 0.9F, 3.2F);
    m_tuning.killerCapsuleRadius = glm::clamp(m_tuning.killerCapsuleRadius, 0.2F, 1.2F);
    m_tuning.killerCapsuleHeight = glm::clamp(m_tuning.killerCapsuleHeight, 0.9F, 3.2F);

    m_tuning.terrorRadiusMeters = glm::clamp(m_tuning.terrorRadiusMeters, 4.0F, 80.0F);
    m_tuning.terrorRadiusChaseMeters = glm::clamp(m_tuning.terrorRadiusChaseMeters, m_tuning.terrorRadiusMeters, 96.0F);

    m_tuning.vaultSlowTime = glm::clamp(m_tuning.vaultSlowTime, 0.2F, 2.0F);
    m_tuning.vaultMediumTime = glm::clamp(m_tuning.vaultMediumTime, 0.2F, 2.0F);
    m_tuning.vaultFastTime = glm::clamp(m_tuning.vaultFastTime, 0.15F, 1.2F);
    m_tuning.fastVaultDotThreshold = glm::clamp(m_tuning.fastVaultDotThreshold, 0.3F, 0.99F);
    m_tuning.fastVaultSpeedMultiplier = glm::clamp(m_tuning.fastVaultSpeedMultiplier, 0.3F, 1.5F);
    m_tuning.fastVaultMinRunup = glm::clamp(m_tuning.fastVaultMinRunup, 0.0F, 8.0F);

    m_tuning.shortAttackRange = glm::clamp(m_tuning.shortAttackRange, 0.5F, 8.0F);
    m_tuning.shortAttackAngleDegrees = glm::clamp(m_tuning.shortAttackAngleDegrees, 10.0F, 170.0F);
    m_tuning.lungeHoldMinSeconds = glm::clamp(m_tuning.lungeHoldMinSeconds, 0.02F, 2.0F);
    m_tuning.lungeDurationSeconds = glm::clamp(m_tuning.lungeDurationSeconds, 0.08F, 3.0F);
    m_tuning.lungeRecoverSeconds = glm::clamp(m_tuning.lungeRecoverSeconds, 0.05F, 3.0F);
    m_tuning.shortRecoverSeconds = glm::clamp(m_tuning.shortRecoverSeconds, 0.05F, 3.0F);
    m_tuning.missRecoverSeconds = glm::clamp(m_tuning.missRecoverSeconds, 0.05F, 3.0F);
    m_tuning.lungeSpeedStart = glm::clamp(m_tuning.lungeSpeedStart, 1.0F, 30.0F);
    m_tuning.lungeSpeedEnd = glm::clamp(m_tuning.lungeSpeedEnd, m_tuning.lungeSpeedStart, 35.0F);

    m_tuning.healDurationSeconds = glm::clamp(m_tuning.healDurationSeconds, 2.0F, 120.0F);
    m_tuning.skillCheckMinInterval = glm::clamp(m_tuning.skillCheckMinInterval, 0.3F, 30.0F);
    m_tuning.skillCheckMaxInterval = glm::clamp(m_tuning.skillCheckMaxInterval, m_tuning.skillCheckMinInterval, 60.0F);
    m_tuning.generatorRepairSecondsBase = glm::clamp(m_tuning.generatorRepairSecondsBase, 5.0F, 240.0F);

    m_tuning.medkitFullHealCharges = glm::clamp(m_tuning.medkitFullHealCharges, 1.0F, 128.0F);
    m_tuning.medkitHealSpeedMultiplier = glm::clamp(m_tuning.medkitHealSpeedMultiplier, 0.1F, 8.0F);
    m_tuning.toolboxCharges = glm::clamp(m_tuning.toolboxCharges, 1.0F, 256.0F);
    m_tuning.toolboxChargeDrainPerSecond = glm::clamp(m_tuning.toolboxChargeDrainPerSecond, 0.05F, 30.0F);
    m_tuning.toolboxRepairSpeedBonus = glm::clamp(m_tuning.toolboxRepairSpeedBonus, 0.0F, 5.0F);

    m_tuning.flashlightMaxUseSeconds = glm::clamp(m_tuning.flashlightMaxUseSeconds, 0.5F, 120.0F);
    m_tuning.flashlightBlindBuildSeconds = glm::clamp(m_tuning.flashlightBlindBuildSeconds, 0.05F, 10.0F);
    m_tuning.flashlightBlindDurationSeconds = glm::clamp(m_tuning.flashlightBlindDurationSeconds, 0.05F, 20.0F);
    m_tuning.flashlightBeamRange = glm::clamp(m_tuning.flashlightBeamRange, 1.0F, 100.0F);
    m_tuning.flashlightBeamAngleDegrees = glm::clamp(m_tuning.flashlightBeamAngleDegrees, 5.0F, 120.0F);
    m_tuning.flashlightBlindStyle = glm::clamp(m_tuning.flashlightBlindStyle, 0, 1);

    m_tuning.mapChannelSeconds = glm::clamp(m_tuning.mapChannelSeconds, 0.05F, 10.0F);
    m_tuning.mapUses = glm::clamp(m_tuning.mapUses, 0, 99);
    m_tuning.mapRevealRangeMeters = glm::clamp(m_tuning.mapRevealRangeMeters, 1.0F, 256.0F);
    m_tuning.mapRevealDurationSeconds = glm::clamp(m_tuning.mapRevealDurationSeconds, 0.1F, 60.0F);

    m_tuning.trapperStartCarryTraps = glm::clamp(m_tuning.trapperStartCarryTraps, 0, 32);
    m_tuning.trapperMaxCarryTraps = glm::clamp(m_tuning.trapperMaxCarryTraps, 1, 32);
    m_tuning.trapperGroundSpawnTraps = glm::clamp(m_tuning.trapperGroundSpawnTraps, 0, 128);
    m_tuning.trapperSetTrapSeconds = glm::clamp(m_tuning.trapperSetTrapSeconds, 0.1F, 20.0F);
    m_tuning.trapperDisarmSeconds = glm::clamp(m_tuning.trapperDisarmSeconds, 0.1F, 20.0F);
    m_tuning.trapEscapeBaseChance = glm::clamp(m_tuning.trapEscapeBaseChance, 0.01F, 0.99F);
    m_tuning.trapEscapeChanceStep = glm::clamp(m_tuning.trapEscapeChanceStep, 0.01F, 0.99F);
    m_tuning.trapEscapeChanceMax = glm::clamp(m_tuning.trapEscapeChanceMax, 0.05F, 0.99F);
    m_tuning.trapKillerStunSeconds = glm::clamp(m_tuning.trapKillerStunSeconds, 0.1F, 20.0F);

    m_tuning.wraithCloakMoveSpeedMultiplier = glm::clamp(m_tuning.wraithCloakMoveSpeedMultiplier, 1.0F, 4.0F);
    m_tuning.wraithCloakTransitionSeconds = glm::clamp(m_tuning.wraithCloakTransitionSeconds, 0.1F, 10.0F);
    m_tuning.wraithUncloakTransitionSeconds = glm::clamp(m_tuning.wraithUncloakTransitionSeconds, 0.1F, 10.0F);
    m_tuning.wraithPostUncloakHasteSeconds = glm::clamp(m_tuning.wraithPostUncloakHasteSeconds, 0.0F, 20.0F);

    m_tuning.weightTLWalls = std::max(0.0F, m_tuning.weightTLWalls);
    m_tuning.weightJungleGymLong = std::max(0.0F, m_tuning.weightJungleGymLong);
    m_tuning.weightJungleGymShort = std::max(0.0F, m_tuning.weightJungleGymShort);
    m_tuning.weightShack = std::max(0.0F, m_tuning.weightShack);
    m_tuning.weightFourLane = std::max(0.0F, m_tuning.weightFourLane);
    m_tuning.weightFillerA = std::max(0.0F, m_tuning.weightFillerA);
    m_tuning.weightFillerB = std::max(0.0F, m_tuning.weightFillerB);
    m_tuning.weightLongWall = std::max(0.0F, m_tuning.weightLongWall);
    m_tuning.weightShortWall = std::max(0.0F, m_tuning.weightShortWall);
    m_tuning.weightLWallWindow = std::max(0.0F, m_tuning.weightLWallWindow);
    m_tuning.weightLWallPallet = std::max(0.0F, m_tuning.weightLWallPallet);
    m_tuning.weightTWalls = std::max(0.0F, m_tuning.weightTWalls);
    m_tuning.weightGymBox = std::max(0.0F, m_tuning.weightGymBox);
    m_tuning.weightDebrisPile = std::max(0.0F, m_tuning.weightDebrisPile);
    m_tuning.maxLoopsPerMap = glm::clamp(m_tuning.maxLoopsPerMap, 0, 64);
    m_tuning.minLoopDistanceTiles = glm::clamp(m_tuning.minLoopDistanceTiles, 0.0F, 8.0F);
    m_tuning.maxSafePallets = glm::clamp(m_tuning.maxSafePallets, 0, 64);
    m_tuning.maxDeadzoneTiles = glm::clamp(m_tuning.maxDeadzoneTiles, 1, 8);

    m_tuning.serverTickRate = (m_tuning.serverTickRate <= 30) ? 30 : 60;
    m_tuning.interpolationBufferMs = glm::clamp(m_tuning.interpolationBufferMs, 50, 1000);

    // Keep survivor capsule auto-fit in sync with the latest gameplay tuning caps.
    RefreshSurvivorModelCapsuleOverride();

    m_terrorRadiusMeters = m_tuning.terrorRadiusMeters;
    m_terrorRadiusChaseMeters = m_tuning.terrorRadiusChaseMeters;
    m_killerShortRange = m_tuning.shortAttackRange;
    m_killerShortHalfAngleRadians = glm::radians(m_tuning.shortAttackAngleDegrees * 0.5F);
    m_killerLungeRange = std::max(m_tuning.shortAttackRange, m_tuning.shortAttackRange + 0.8F);
    m_killerLungeHalfAngleRadians = m_killerShortHalfAngleRadians;
    m_killerLungeChargeMinSeconds = std::min(m_tuning.lungeHoldMinSeconds, m_tuning.lungeDurationSeconds);
    m_killerLungeChargeMaxSeconds = m_tuning.lungeDurationSeconds;
    m_killerLungeDurationSeconds = m_tuning.lungeDurationSeconds;
    m_killerLungeRecoverSeconds = m_tuning.lungeRecoverSeconds;
    m_killerShortRecoverSeconds = m_tuning.shortRecoverSeconds;
    m_killerMissRecoverSeconds = m_tuning.missRecoverSeconds;
    m_killerLungeSpeedStart = m_tuning.lungeSpeedStart;
    m_killerLungeSpeedEnd = m_tuning.lungeSpeedEnd;

    m_generationSettings.weightTLWalls = m_tuning.weightTLWalls;
    m_generationSettings.weightJungleGymLong = m_tuning.weightJungleGymLong;
    m_generationSettings.weightJungleGymShort = m_tuning.weightJungleGymShort;
    m_generationSettings.weightShack = m_tuning.weightShack;
    m_generationSettings.weightFourLane = m_tuning.weightFourLane;
    m_generationSettings.weightFillerA = m_tuning.weightFillerA;
    m_generationSettings.weightFillerB = m_tuning.weightFillerB;
    m_generationSettings.weightLongWall = m_tuning.weightLongWall;
    m_generationSettings.weightShortWall = m_tuning.weightShortWall;
    m_generationSettings.weightLWallWindow = m_tuning.weightLWallWindow;
    m_generationSettings.weightLWallPallet = m_tuning.weightLWallPallet;
    m_generationSettings.weightTWalls = m_tuning.weightTWalls;
    m_generationSettings.weightGymBox = m_tuning.weightGymBox;
    m_generationSettings.weightDebrisPile = m_tuning.weightDebrisPile;
    m_generationSettings.maxLoops = m_tuning.maxLoopsPerMap;
    m_generationSettings.minLoopDistanceTiles = m_tuning.minLoopDistanceTiles;
    m_generationSettings.maxSafePallets = m_tuning.maxSafePallets;
    m_generationSettings.maxDeadzoneTiles = m_tuning.maxDeadzoneTiles;
    m_generationSettings.edgeBiasLoops = m_tuning.edgeBiasLoops;
    m_generationSettings.disableWindowsAndPallets = m_tuning.disableWindowsAndPallets;

    if (m_generationSettings.disableWindowsAndPallets)
    {
        // Zero out loop types that rely on windows/pallets
        m_generationSettings.weightJungleGymLong = 0.0F;
        m_generationSettings.weightJungleGymShort = 0.0F;
        m_generationSettings.weightLWallWindow = 0.0F;
        m_generationSettings.weightLWallPallet = 0.0F;
        m_generationSettings.weightShortWall = 0.0F;
        m_generationSettings.weightGymBox = 0.0F;

        // Boost wall-only loop types
        m_generationSettings.weightLongWall = 1.6F;
        m_generationSettings.weightTWalls = 1.4F;
        m_generationSettings.weightDebrisPile = 1.2F;
        m_generationSettings.weightTLWalls = 1.2F;
    }

    auto applyRole = [&](engine::scene::Entity entity, bool survivor) {
        auto actorIt = m_world.Actors().find(entity);
        if (actorIt == m_world.Actors().end())
        {
            return;
        }

        engine::scene::ActorComponent& actor = actorIt->second;
        if (survivor)
        {
            actor.walkSpeed = m_tuning.survivorWalkSpeed * m_survivorSpeedPercent;
            actor.sprintSpeed = m_tuning.survivorSprintSpeed * m_survivorSpeedPercent;
            const float survivorCapsuleRadius = (m_survivorCapsuleOverrideRadius > 0.0F)
                                                    ? m_survivorCapsuleOverrideRadius
                                                    : m_tuning.survivorCapsuleRadius;
            const float survivorCapsuleHeight = (m_survivorCapsuleOverrideHeight > 0.0F)
                                                    ? m_survivorCapsuleOverrideHeight
                                                    : m_tuning.survivorCapsuleHeight;
            actor.capsuleRadius = survivorCapsuleRadius;
            actor.capsuleHeight = survivorCapsuleHeight;
        }
        else
        {
            actor.walkSpeed = m_tuning.killerMoveSpeed * m_killerSpeedPercent;
            actor.sprintSpeed = m_tuning.killerMoveSpeed * m_killerSpeedPercent;
            actor.capsuleRadius = m_tuning.killerCapsuleRadius;
            actor.capsuleHeight = m_tuning.killerCapsuleHeight;
        }
        actor.eyeHeight = std::max(0.8F, actor.capsuleHeight * 0.88F);
    };

    applyRole(m_survivor, true);
    applyRole(m_killer, false);
}

GameplaySystems::GameplayTuning GameplaySystems::GetGameplayTuning() const
{
    return m_tuning;
}

void GameplaySystems::SetNetworkAuthorityMode(bool enabled)
{
    m_networkAuthorityMode = enabled;
    if (!enabled)
    {
        ClearRemoteRoleCommands();
    }
}

void GameplaySystems::SetRemoteRoleCommand(engine::scene::Role role, const RoleCommand& command)
{
    if (role == engine::scene::Role::Survivor)
    {
        m_remoteSurvivorCommand = command;
    }
    else
    {
        m_remoteKillerCommand = command;
    }
}

void GameplaySystems::ClearRemoteRoleCommands()
{
    m_remoteSurvivorCommand.reset();
    m_remoteKillerCommand.reset();
}

GameplaySystems::Snapshot GameplaySystems::BuildSnapshot() const
{
    Snapshot snapshot;
    snapshot.mapType = m_currentMap;
    snapshot.seed = m_generationSeed;
    snapshot.survivorPerkIds = m_survivorPerks.perkIds;
    snapshot.killerPerkIds = m_killerPerks.perkIds;
    snapshot.survivorCharacterId = m_selectedSurvivorCharacterId;
    snapshot.killerCharacterId = m_selectedKillerCharacterId;
    snapshot.survivorItemId = m_survivorLoadout.itemId;
    snapshot.survivorItemAddonA = m_survivorLoadout.addonAId;
    snapshot.survivorItemAddonB = m_survivorLoadout.addonBId;
    snapshot.killerPowerId = m_killerLoadout.powerId;
    snapshot.killerPowerAddonA = m_killerLoadout.addonAId;
    snapshot.killerPowerAddonB = m_killerLoadout.addonBId;
    snapshot.survivorState = static_cast<std::uint8_t>(m_survivorState);
    snapshot.killerAttackState = static_cast<std::uint8_t>(m_killerAttackState);
    snapshot.killerAttackStateTimer = m_killerAttackStateTimer;
    snapshot.killerLungeCharge = m_killerLungeChargeSeconds;
    snapshot.chaseActive = m_chase.isChasing;
    snapshot.chaseDistance = m_chase.distance;
    snapshot.chaseLos = m_chase.hasLineOfSight;
    snapshot.chaseInCenterFOV = m_chase.inCenterFOV;
    snapshot.chaseTimeSinceLOS = m_chase.timeSinceSeenLOS;
    snapshot.chaseTimeSinceCenterFOV = m_chase.timeSinceCenterFOV;
    snapshot.chaseTimeInChase = m_chase.timeInChase;
    snapshot.bloodlustTier = static_cast<std::uint8_t>(m_bloodlust.tier);
    snapshot.survivorItemCharges = m_survivorItemState.charges;
    snapshot.survivorItemActive = m_survivorItemState.active ? 1U : 0U;
    snapshot.survivorItemUsesRemaining = static_cast<std::uint8_t>(glm::clamp(m_survivorItemState.mapUsesRemaining, 0, 255));
    snapshot.wraithCloaked = m_killerPowerState.wraithCloaked ? 1U : 0U;
    snapshot.wraithTransitionTimer = m_killerPowerState.wraithTransitionTimer;
    snapshot.wraithPostUncloakTimer = m_killerPowerState.wraithPostUncloakTimer;
    snapshot.killerBlindTimer = m_killerPowerState.killerBlindTimer;
    snapshot.killerBlindStyleWhite = static_cast<std::uint8_t>(m_tuning.flashlightBlindStyle == 0 ? 1 : 0);
    snapshot.carriedTrapCount = static_cast<std::uint8_t>(glm::clamp(m_killerPowerState.trapperCarriedTraps, 0, 255));
    // Nurse blink state
    snapshot.blinkState = static_cast<std::uint8_t>(m_killerPowerState.blinkState);
    snapshot.blinkCharges = static_cast<std::uint8_t>(glm::clamp(m_killerPowerState.blinkCharges, 0, 255));
    snapshot.blinkCharge01 = m_killerPowerState.blinkCharge01;
    snapshot.blinkChargeRegenTimer = m_killerPowerState.blinkChargeRegenTimer;
    snapshot.blinkTargetPosition = m_killerPowerState.blinkTargetPosition;

    auto fillActor = [&](engine::scene::Entity entity, ActorSnapshot& outActor) {
        const auto transformIt = m_world.Transforms().find(entity);
        const auto actorIt = m_world.Actors().find(entity);
        if (transformIt == m_world.Transforms().end() || actorIt == m_world.Actors().end())
        {
            return;
        }

        outActor.position = transformIt->second.position;
        outActor.forward = transformIt->second.forward;
        outActor.velocity = actorIt->second.velocity;
        outActor.yaw = transformIt->second.rotationEuler.y;
        outActor.pitch = transformIt->second.rotationEuler.x;
    };

    fillActor(m_survivor, snapshot.survivor);
    fillActor(m_killer, snapshot.killer);

    snapshot.pallets.reserve(m_world.Pallets().size());
    for (const auto& [entity, pallet] : m_world.Pallets())
    {
        const auto transformIt = m_world.Transforms().find(entity);
        if (transformIt == m_world.Transforms().end())
        {
            continue;
        }

        PalletSnapshot palletSnapshot;
        palletSnapshot.entity = entity;
        palletSnapshot.state = static_cast<std::uint8_t>(pallet.state);
        palletSnapshot.breakTimer = pallet.breakTimer;
        palletSnapshot.position = transformIt->second.position;
        palletSnapshot.halfExtents = pallet.halfExtents;
        snapshot.pallets.push_back(palletSnapshot);
    }

    snapshot.traps.reserve(m_world.BearTraps().size());
    for (const auto& [entity, trap] : m_world.BearTraps())
    {
        const auto transformIt = m_world.Transforms().find(entity);
        if (transformIt == m_world.Transforms().end())
        {
            continue;
        }
        TrapSnapshot trapSnapshot;
        trapSnapshot.entity = entity;
        trapSnapshot.state = static_cast<std::uint8_t>(trap.state);
        trapSnapshot.trappedEntity = trap.trappedEntity;
        trapSnapshot.position = transformIt->second.position;
        trapSnapshot.halfExtents = trap.halfExtents;
        trapSnapshot.escapeChance = trap.escapeChance;
        trapSnapshot.escapeAttempts = static_cast<std::uint8_t>(glm::clamp(trap.escapeAttempts, 0, 255));
        trapSnapshot.maxEscapeAttempts = static_cast<std::uint8_t>(glm::clamp(trap.maxEscapeAttempts, 0, 255));
        snapshot.traps.push_back(trapSnapshot);
    }

    snapshot.groundItems.reserve(m_world.GroundItems().size());
    for (const auto& [entity, groundItem] : m_world.GroundItems())
    {
        const auto transformIt = m_world.Transforms().find(entity);
        if (transformIt == m_world.Transforms().end())
        {
            continue;
        }
        GroundItemSnapshot itemSnapshot;
        itemSnapshot.entity = entity;
        itemSnapshot.position = transformIt->second.position;
        itemSnapshot.charges = groundItem.charges;
        itemSnapshot.itemId = groundItem.itemId;
        itemSnapshot.addonAId = groundItem.addonAId;
        itemSnapshot.addonBId = groundItem.addonBId;
        snapshot.groundItems.push_back(itemSnapshot);
    }

    return snapshot;
}

void GameplaySystems::ApplySnapshot(const Snapshot& snapshot, float blendAlpha)
{
    // Apply perk loadouts if different
    if (snapshot.survivorPerkIds != m_survivorPerks.perkIds)
    {
        m_survivorPerks.perkIds = snapshot.survivorPerkIds;
        m_perkSystem.SetSurvivorLoadout(m_survivorPerks);
        m_perkSystem.InitializeActiveStates();
    }

    if (snapshot.killerPerkIds != m_killerPerks.perkIds)
    {
        m_killerPerks.perkIds = snapshot.killerPerkIds;
        m_perkSystem.SetKillerLoadout(m_killerPerks);
        m_perkSystem.InitializeActiveStates();
    }

    if (snapshot.mapType != m_currentMap || snapshot.seed != m_generationSeed)
    {
        BuildSceneFromMap(snapshot.mapType, snapshot.seed);
    }

    bool survivorCharacterChanged = false;
    if (!snapshot.survivorCharacterId.empty() && snapshot.survivorCharacterId != m_selectedSurvivorCharacterId)
    {
        m_selectedSurvivorCharacterId = snapshot.survivorCharacterId;
        m_animationCharacterId.clear();
        survivorCharacterChanged = true;
    }
    if (!snapshot.killerCharacterId.empty())
    {
        m_selectedKillerCharacterId = snapshot.killerCharacterId;
    }
    if (survivorCharacterChanged)
    {
        RefreshSurvivorModelCapsuleOverride();
        ApplyGameplayTuning(m_tuning);
    }

    m_survivorLoadout.itemId = snapshot.survivorItemId;
    m_survivorLoadout.addonAId = snapshot.survivorItemAddonA;
    m_survivorLoadout.addonBId = snapshot.survivorItemAddonB;
    m_killerLoadout.powerId = snapshot.killerPowerId;
    m_killerLoadout.addonAId = snapshot.killerPowerAddonA;
    m_killerLoadout.addonBId = snapshot.killerPowerAddonB;
    RefreshLoadoutModifiers();
    m_survivorItemState.charges = snapshot.survivorItemCharges;
    m_survivorItemState.active = snapshot.survivorItemActive != 0U;
    m_survivorItemState.mapUsesRemaining = static_cast<int>(snapshot.survivorItemUsesRemaining);
    m_killerPowerState.wraithCloaked = snapshot.wraithCloaked != 0U;
    m_killerPowerState.wraithTransitionTimer = snapshot.wraithTransitionTimer;
    m_killerPowerState.wraithPostUncloakTimer = snapshot.wraithPostUncloakTimer;
    m_killerPowerState.killerBlindTimer = snapshot.killerBlindTimer;
    m_tuning.flashlightBlindStyle = snapshot.killerBlindStyleWhite != 0U ? 0 : 1;
    m_killerPowerState.trapperCarriedTraps = static_cast<int>(snapshot.carriedTrapCount);
    // Nurse blink state
    m_killerPowerState.blinkState = static_cast<NurseBlinkState>(glm::clamp(static_cast<int>(snapshot.blinkState), 0, static_cast<int>(NurseBlinkState::Fatigue)));
    m_killerPowerState.blinkCharges = static_cast<int>(snapshot.blinkCharges);
    m_killerPowerState.blinkCharge01 = snapshot.blinkCharge01;
    m_killerPowerState.blinkChargeRegenTimer = snapshot.blinkChargeRegenTimer;
    m_killerPowerState.blinkTargetPosition = snapshot.blinkTargetPosition;

    m_chase.isChasing = snapshot.chaseActive;
    m_chase.distance = snapshot.chaseDistance;
    m_chase.hasLineOfSight = snapshot.chaseLos;
    m_chase.inCenterFOV = snapshot.chaseInCenterFOV;
    m_chase.timeSinceSeenLOS = snapshot.chaseTimeSinceLOS;
    m_chase.timeSinceCenterFOV = snapshot.chaseTimeSinceCenterFOV;
    m_chase.timeInChase = snapshot.chaseTimeInChase;
    m_bloodlust.tier = static_cast<int>(snapshot.bloodlustTier);

    const SurvivorHealthState nextState = static_cast<SurvivorHealthState>(
        glm::clamp(static_cast<int>(snapshot.survivorState), 0, static_cast<int>(SurvivorHealthState::Dead))
    );
    m_survivorState = nextState;
    m_killerAttackState = static_cast<KillerAttackState>(
        glm::clamp(static_cast<int>(snapshot.killerAttackState), 0, static_cast<int>(KillerAttackState::Recovering))
    );
    m_killerAttackStateTimer = snapshot.killerAttackStateTimer;
    m_killerLungeChargeSeconds = snapshot.killerLungeCharge;

    auto applyActor = [&](engine::scene::Entity entity, const ActorSnapshot& actorSnapshot) {
        auto transformIt = m_world.Transforms().find(entity);
        auto actorIt = m_world.Actors().find(entity);
        if (transformIt == m_world.Transforms().end() || actorIt == m_world.Actors().end())
        {
            return;
        }

        transformIt->second.position = glm::mix(transformIt->second.position, actorSnapshot.position, blendAlpha);
        transformIt->second.rotationEuler.y = actorSnapshot.yaw;
        transformIt->second.rotationEuler.x = actorSnapshot.pitch;
        transformIt->second.forward = glm::length(actorSnapshot.forward) > 1.0e-4F
                                          ? glm::normalize(actorSnapshot.forward)
                                          : ForwardFromYawPitch(actorSnapshot.yaw, actorSnapshot.pitch);
        actorIt->second.velocity = actorSnapshot.velocity;
        actorIt->second.carried = (entity == m_survivor && m_survivorState == SurvivorHealthState::Carried);
    };

    applyActor(m_survivor, snapshot.survivor);
    applyActor(m_killer, snapshot.killer);

    for (const PalletSnapshot& palletSnapshot : snapshot.pallets)
    {
        auto palletIt = m_world.Pallets().find(palletSnapshot.entity);
        auto transformIt = m_world.Transforms().find(palletSnapshot.entity);
        if (palletIt == m_world.Pallets().end() || transformIt == m_world.Transforms().end())
        {
            continue;
        }

        palletIt->second.state = static_cast<engine::scene::PalletState>(
            glm::clamp(static_cast<int>(palletSnapshot.state), 0, static_cast<int>(engine::scene::PalletState::Broken))
        );
        palletIt->second.breakTimer = palletSnapshot.breakTimer;
        palletIt->second.halfExtents = palletSnapshot.halfExtents;
        transformIt->second.position = glm::mix(transformIt->second.position, palletSnapshot.position, blendAlpha);
    }

    std::unordered_set<engine::scene::Entity> seenTraps;
    for (const TrapSnapshot& trapSnapshot : snapshot.traps)
    {
        seenTraps.insert(trapSnapshot.entity);

        auto transformIt = m_world.Transforms().find(trapSnapshot.entity);
        if (transformIt == m_world.Transforms().end())
        {
            m_world.Transforms()[trapSnapshot.entity] = engine::scene::Transform{
                trapSnapshot.position,
                glm::vec3{0.0F},
                glm::vec3{1.0F},
                glm::vec3{0.0F, 0.0F, 1.0F},
            };
            transformIt = m_world.Transforms().find(trapSnapshot.entity);
        }
        auto trapIt = m_world.BearTraps().find(trapSnapshot.entity);
        if (trapIt == m_world.BearTraps().end())
        {
            m_world.BearTraps()[trapSnapshot.entity] = engine::scene::BearTrapComponent{};
            trapIt = m_world.BearTraps().find(trapSnapshot.entity);
            m_world.Names()[trapSnapshot.entity] = engine::scene::NameComponent{"bear_trap"};
        }

        transformIt->second.position = glm::mix(transformIt->second.position, trapSnapshot.position, blendAlpha);
        trapIt->second.state = static_cast<engine::scene::TrapState>(
            glm::clamp(static_cast<int>(trapSnapshot.state), 0, static_cast<int>(engine::scene::TrapState::Disarmed))
        );
        trapIt->second.trappedEntity = trapSnapshot.trappedEntity;
        trapIt->second.halfExtents = trapSnapshot.halfExtents;
        trapIt->second.escapeChance = trapSnapshot.escapeChance;
        trapIt->second.escapeAttempts = static_cast<int>(trapSnapshot.escapeAttempts);
        trapIt->second.maxEscapeAttempts = static_cast<int>(trapSnapshot.maxEscapeAttempts);
    }

    std::vector<engine::scene::Entity> removeTraps;
    removeTraps.reserve(m_world.BearTraps().size());
    for (const auto& [entity, _] : m_world.BearTraps())
    {
        if (!seenTraps.contains(entity))
        {
            removeTraps.push_back(entity);
        }
    }
    for (engine::scene::Entity entity : removeTraps)
    {
        DestroyEntity(entity);
    }

    std::unordered_set<engine::scene::Entity> seenGroundItems;
    for (const GroundItemSnapshot& itemSnapshot : snapshot.groundItems)
    {
        seenGroundItems.insert(itemSnapshot.entity);

        auto transformIt = m_world.Transforms().find(itemSnapshot.entity);
        if (transformIt == m_world.Transforms().end())
        {
            m_world.Transforms()[itemSnapshot.entity] = engine::scene::Transform{
                itemSnapshot.position,
                glm::vec3{0.0F},
                glm::vec3{1.0F},
                glm::vec3{0.0F, 0.0F, 1.0F},
            };
            transformIt = m_world.Transforms().find(itemSnapshot.entity);
        }
        auto itemIt = m_world.GroundItems().find(itemSnapshot.entity);
        if (itemIt == m_world.GroundItems().end())
        {
            m_world.GroundItems()[itemSnapshot.entity] = engine::scene::GroundItemComponent{};
            itemIt = m_world.GroundItems().find(itemSnapshot.entity);
            m_world.Names()[itemSnapshot.entity] = engine::scene::NameComponent{"ground_item"};
        }

        transformIt->second.position = glm::mix(transformIt->second.position, itemSnapshot.position, blendAlpha);
        itemIt->second.itemId = itemSnapshot.itemId;
        itemIt->second.charges = itemSnapshot.charges;
        itemIt->second.addonAId = itemSnapshot.addonAId;
        itemIt->second.addonBId = itemSnapshot.addonBId;
        itemIt->second.pickupEnabled = true;
    }

    std::vector<engine::scene::Entity> removeGroundItems;
    removeGroundItems.reserve(m_world.GroundItems().size());
    for (const auto& [entity, _] : m_world.GroundItems())
    {
        if (!seenGroundItems.contains(entity))
        {
            removeGroundItems.push_back(entity);
        }
    }
    for (engine::scene::Entity entity : removeGroundItems)
    {
        DestroyEntity(entity);
    }

    RebuildPhysicsWorld();
}

void GameplaySystems::StartSkillCheckDebug()
{
    if (m_activeRepairGenerator == 0)
    {
        for (const auto& [entity, generator] : m_world.Generators())
        {
            if (!generator.completed)
            {
                m_activeRepairGenerator = entity;
                break;
            }
        }
    }

    if (m_activeRepairGenerator == 0)
    {
        AddRuntimeMessage("Skillcheck unavailable: no active generator", 1.5F);
        return;
    }

    std::uniform_real_distribution<float> startDist(0.15F, 0.78F);
    std::uniform_real_distribution<float> sizeDist(0.09F, 0.16F);
    const float zoneStart = startDist(m_rng);
    const float zoneSize = sizeDist(m_rng);
    m_skillCheckSuccessStart = zoneStart;
    m_skillCheckSuccessEnd = std::min(0.98F, zoneStart + zoneSize);
    m_skillCheckNeedle = 0.0F;
    m_skillCheckActive = true;
    AddRuntimeMessage("Skillcheck debug started", 1.5F);
}

void GameplaySystems::HealSurvivor()
{
    if (!SetSurvivorState(SurvivorHealthState::Healthy, "Heal"))
    {
        AddRuntimeMessage("Heal rejected for current survivor state", 1.6F);
    }
}

void GameplaySystems::SetSurvivorStateDebug(const std::string& stateName)
{
    SurvivorHealthState next = m_survivorState;
    if (stateName == "healthy")
    {
        next = SurvivorHealthState::Healthy;
    }
    else if (stateName == "injured")
    {
        next = SurvivorHealthState::Injured;
    }
    else if (stateName == "downed")
    {
        next = SurvivorHealthState::Downed;
    }
    else if (stateName == "trapped")
    {
        next = SurvivorHealthState::Trapped;
    }
    else if (stateName == "carried")
    {
        next = SurvivorHealthState::Carried;
    }
    else if (stateName == "hooked")
    {
        next = SurvivorHealthState::Hooked;
    }
    else if (stateName == "dead")
    {
        next = SurvivorHealthState::Dead;
    }
    else
    {
        AddRuntimeMessage("Unknown survivor state", 1.6F);
        return;
    }

    SetSurvivorState(next, "Debug force", true);
}

void GameplaySystems::SetGeneratorsCompleted(int completed)
{
    const int clamped = glm::clamp(completed, 0, m_generatorsTotal);
    int index = 0;
    for (auto& [_, generator] : m_world.Generators())
    {
        const bool done = index < clamped;
        generator.completed = done;
        generator.progress = done ? 1.0F : 0.0F;
        ++index;
    }
    RefreshGeneratorsCompleted();
}

void GameplaySystems::HookCarriedSurvivorDebug()
{
    if (m_survivorState != SurvivorHealthState::Carried)
    {
        AddRuntimeMessage("Hook debug failed: survivor is not carried", 1.6F);
        return;
    }

    engine::scene::Entity hookEntity = 0;
    if (!m_world.Hooks().empty())
    {
        hookEntity = m_world.Hooks().begin()->first;
    }
    TryHookCarriedSurvivor(hookEntity);
}

void GameplaySystems::RequestQuit()
{
    m_quitRequested = true;
}

void GameplaySystems::SpawnFxDebug(const std::string& assetId)
{
    glm::vec3 forward = m_cameraForward;
    const engine::scene::Entity controlled = ControlledEntity();
    const auto controlledTransformIt = m_world.Transforms().find(controlled);
    if (controlledTransformIt != m_world.Transforms().end() && glm::length(controlledTransformIt->second.forward) > 1.0e-5F)
    {
        forward = controlledTransformIt->second.forward;
    }
    if (glm::length(forward) <= 1.0e-5F)
    {
        forward = glm::vec3{0.0F, 0.0F, -1.0F};
    }
    const glm::vec3 origin = m_cameraInitialized ? (m_cameraPosition + m_cameraForward * 1.8F) : glm::vec3{0.0F, 1.0F, 0.0F};
    SpawnGameplayFx(assetId, origin, forward, engine::fx::FxNetMode::Local);
}

void GameplaySystems::LoadTestModelMeshes()
{
    if (m_rendererPtr == nullptr)
    {
        std::cout << "[TEST_MODELS] Cannot load meshes: renderer unavailable\n";
        return;
    }

    static engine::assets::MeshLibrary fallbackMeshLibrary;
    engine::assets::MeshLibrary& meshLibrary = (m_meshLibrary != nullptr) ? *m_meshLibrary : fallbackMeshLibrary;

    const auto resolveMeshPath = [](const std::string& fileName) {
        std::error_code ec;
        const std::filesystem::path cwd = std::filesystem::current_path(ec);
        const std::filesystem::path relative = std::filesystem::path("assets") / "meshes" / fileName;
        const std::array<std::filesystem::path, 4> candidates{
            cwd / relative,
            cwd / ".." / relative,
            cwd / ".." / ".." / relative,
            relative
        };
        for (const std::filesystem::path& candidate : candidates)
        {
            if (std::filesystem::exists(candidate, ec))
            {
                return std::filesystem::absolute(candidate, ec);
            }
        }
        return std::filesystem::absolute(relative, ec);
    };

    const auto uploadMesh = [&](const char* label,
                                const std::string& fileName,
                                const glm::vec3& color,
                                engine::render::Renderer::GpuMeshId* outGpuId,
                                float* outFeetOffset) {
        if (outGpuId == nullptr || *outGpuId != engine::render::Renderer::kInvalidGpuMesh)
        {
            return;
        }

        const std::filesystem::path meshPath = resolveMeshPath(fileName);
        std::string error;
        const engine::assets::MeshData* meshData = meshLibrary.LoadMesh(meshPath, &error);
        if (meshData == nullptr || !meshData->loaded)
        {
            std::cout << "[TEST_MODELS] Failed to load " << label << " mesh from "
                      << meshPath.string() << ": " << error << "\n";
            return;
        }

        const engine::render::MaterialParams material{};
        *outGpuId = m_rendererPtr->UploadMesh(meshData->geometry, color, material);
        if (outFeetOffset != nullptr)
        {
            *outFeetOffset = -meshData->boundsMin.y;
        }
        std::cout << "[TEST_MODELS] Loaded " << label << " mesh from " << meshPath.string() << "\n";
    };

    uploadMesh(
        "male",
        "survivor_male_blocky.glb",
        glm::vec3{0.23F, 0.51F, 0.96F},
        &m_testModelMeshes.maleBody,
        &m_testModelMeshes.maleFeetOffset
    );
    uploadMesh(
        "female",
        "survivor_female_blocky.glb",
        glm::vec3{0.93F, 0.27F, 0.60F},
        &m_testModelMeshes.femaleBody,
        &m_testModelMeshes.femaleFeetOffset
    );
}

void GameplaySystems::SpawnTestModels()
{
    std::vector<engine::scene::Entity> legacyEntities;
    legacyEntities.reserve(4);
    for (const auto& [entity, name] : m_world.Names())
    {
        if (name.name == "test_model_male_blocky" || name.name == "test_model_female_blocky")
        {
            legacyEntities.push_back(entity);
        }
    }
    for (engine::scene::Entity entity : legacyEntities)
    {
        DestroyEntity(entity);
    }

    m_testModels.spawned = true;
    m_testModels.malePosition = glm::vec3{5.0F, 0.0F, 5.0F};
    m_testModels.femalePosition = glm::vec3{7.0F, 0.0F, 5.0F};
    LoadTestModelMeshes();

    std::cout << "[TEST_MODELS] Spawned survivor meshes at (5,0,5) and (7,0,5)\n";
}

void GameplaySystems::SpawnTestModelsHere()
{
    std::vector<engine::scene::Entity> legacyEntities;
    legacyEntities.reserve(4);
    for (const auto& [entity, name] : m_world.Names())
    {
        if (name.name == "test_model_male_blocky" || name.name == "test_model_female_blocky")
        {
            legacyEntities.push_back(entity);
        }
    }
    for (engine::scene::Entity entity : legacyEntities)
    {
        DestroyEntity(entity);
    }

    glm::vec3 playerPos = m_cameraPosition;
    if (m_cameraInitialized)
    {
        const glm::vec3 rayStart = playerPos + glm::vec3{0.0F, 20.0F, 0.0F};
        const glm::vec3 rayEnd = playerPos + glm::vec3{0.0F, -40.0F, 0.0F};
        if (const auto hit = m_physics.RaycastNearest(rayStart, rayEnd); hit.has_value())
        {
            playerPos = hit->position;
        }
    }

    m_testModels.spawned = true;
    m_testModels.malePosition = playerPos + glm::vec3{-2.0F, 0.0F, 0.0F};
    m_testModels.femalePosition = playerPos + glm::vec3{2.0F, 0.0F, 0.0F};
    LoadTestModelMeshes();

    std::cout << "[TEST_MODELS] Spawned survivor meshes near player at ("
              << playerPos.x << ", " << playerPos.y << ", " << playerPos.z << ")\n";
}

void GameplaySystems::StopAllFx()
{
    m_fxSystem.StopAll();
    m_chaseAuraFxId = 0;
}

std::vector<std::string> GameplaySystems::ListFxAssets() const
{
    return m_fxSystem.ListAssetIds();
}

std::optional<engine::fx::FxAsset> GameplaySystems::GetFxAsset(const std::string& assetId) const
{
    return m_fxSystem.GetAsset(assetId);
}

bool GameplaySystems::SaveFxAsset(const engine::fx::FxAsset& asset, std::string* outError)
{
    return m_fxSystem.SaveAsset(asset, outError);
}

void GameplaySystems::SetFxReplicationCallback(std::function<void(const engine::fx::FxSpawnEvent&)> callback)
{
    m_fxReplicationCallback = std::move(callback);
}

void GameplaySystems::SpawnReplicatedFx(const engine::fx::FxSpawnEvent& event)
{
    m_fxSystem.Spawn(event.assetId, event.position, event.forward, {}, engine::fx::FxNetMode::Local);
}

void GameplaySystems::BuildSceneFromMap(MapType mapType, unsigned int seed)
{
    game::maps::TileGenerator generator;
    game::maps::GeneratedMap generated;

    if (mapType == MapType::Test)
    {
        generated = generator.GenerateTestMap();
    }
    else if (mapType == MapType::Main)
    {
        generated = generator.GenerateMainMap(seed, m_generationSettings);
        // Apply DBD-inspired spawn system if enabled
        if (m_dbdSpawnsEnabled)
        {
            generator.CalculateDbdSpawns(generated, seed);
        }
    }
    else if (mapType == MapType::Benchmark)
    {
        generated = generator.GenerateBenchmarkMap();
    }
    else
    {
        generated = generator.GenerateCollisionTestMap();
    }

    BuildSceneFromGeneratedMap(generated, mapType, seed, MapToName(mapType));
}

void GameplaySystems::BuildSceneFromGeneratedMap(
    const ::game::maps::GeneratedMap& generated,
    MapType mapType,
    unsigned int seed,
    const std::string& mapDisplayName
)
{
    m_currentMap = mapType;
    m_generationSeed = seed;
    m_activeMapName = mapDisplayName.empty() ? MapToName(mapType) : mapDisplayName;
    m_survivor = 0;
    m_killer = 0;
    m_killerBreakingPallet = 0;
    m_lastHitRayStart = glm::vec3{0.0F};
    m_lastHitRayEnd = glm::vec3{0.0F};
    m_lastHitConnected = false;
    m_lastSwingOrigin = glm::vec3{0.0F};
    m_lastSwingDirection = glm::vec3{0.0F, 0.0F, -1.0F};
    m_lastSwingRange = 0.0F;
    m_lastSwingHalfAngleRadians = 0.0F;
    m_lastSwingDebugTtl = 0.0F;
    m_fxSystem.StopAll();
    m_chaseAuraFxId = 0;
    m_chase = ChaseState{};
    m_interactionCandidate = InteractionCandidate{};
    m_cameraInitialized = false;
    m_survivorState = SurvivorHealthState::Healthy;
    m_generatorsCompleted = 0;
    m_carryEscapeProgress = 0.0F;
    m_carryLastQteDirection = 0;
    m_hookStage = 0;
    m_hookStageTimer = 0.0F;
    m_hookEscapeAttemptsUsed = 0;
    m_hookSkillCheckTimeToNext = 0.0F;
    m_activeHookEntity = 0;
    m_activeRepairGenerator = 0;
    m_selfHealActive = false;
    m_selfHealProgress = 0.0F;
    m_skillCheckActive = false;
    m_skillCheckMode = SkillCheckMode::None;
    m_skillCheckNeedle = 0.0F;
    m_skillCheckSuccessStart = 0.0F;
    m_skillCheckSuccessEnd = 0.0F;
    m_skillCheckTimeToNext = 2.0F;
    m_interactBufferRemaining = std::array<float, 2>{0.0F, 0.0F};
    m_survivorWigglePressQueue.clear();
    m_localSurvivorCommand = RoleCommand{};
    m_localKillerCommand = RoleCommand{};
    m_remoteSurvivorCommand.reset();
    m_remoteKillerCommand.reset();
    m_killerAttackState = KillerAttackState::Idle;
    m_killerAttackStateTimer = 0.0F;
    m_killerLungeChargeSeconds = 0.0F;
    m_killerAttackFlashTtl = 0.0F;
    m_killerAttackHitThisAction = false;
    m_previousAttackHeld = false;
    m_killerCurrentLungeSpeed = 0.0F;
    m_survivorHitHasteTimer = 0.0F;
    m_killerSurvivorNoCollisionTimer = 0.0F;
    m_killerPreMovePosition = glm::vec3{0.0F};
    m_survivorPreMovePosition = glm::vec3{0.0F};
    m_killerPreMovePositionValid = false;
    m_survivorPreMovePositionValid = false;
    m_killerSlowTimer = 0.0F;
    m_killerSlowMultiplier = 1.0F;
    m_carryInputGraceTimer = 0.0F;
    m_mapRevealGenerators.clear();
    m_killerPowerState = KillerPowerRuntimeState{};
    m_survivorVisualYawRadians = 0.0F;
    m_survivorVisualYawInitialized = false;
    m_survivorVisualTargetYawRadians = 0.0F;
    m_survivorVisualMoveInput = glm::vec2{0.0F};
    m_survivorVisualDesiredDirection = glm::vec3{0.0F};
    m_testModels = TestModelData{};
    m_testModels.spawned = false;

    m_world.Clear();
    m_loopDebugTiles.clear();
    m_spawnPoints.clear();
    m_nextSpawnPointId = 1;

    // Free GPU mesh resources before clearing the vector.
    if (m_rendererPtr)
    {
        for (auto& mesh : m_highPolyMeshes)
        {
            m_rendererPtr->FreeGpuMesh(mesh.gpuFullLod);
            m_rendererPtr->FreeGpuMesh(mesh.gpuMediumLod);
        }
    }
    // Swap-to-empty to actually release RAM (clear() only resets size, not capacity).
    { std::vector<HighPolyMesh> empty; m_highPolyMeshes.swap(empty); }
    m_highPolyMeshesGenerated = false;
    m_highPolyMeshesUploaded = false;

    m_loopDebugTiles.reserve(generated.tiles.size());
    for (const auto& tile : generated.tiles)
    {
        m_loopDebugTiles.push_back(LoopDebugTile{
            tile.center,
            tile.halfExtents,
            tile.loopId,
            tile.archetype,
        });
    }

    for (const auto& wall : generated.walls)
    {
        const engine::scene::Entity wallEntity = m_world.CreateEntity();
        m_world.Transforms()[wallEntity] = engine::scene::Transform{
            wall.center,
            glm::vec3{0.0F},
            glm::vec3{1.0F},
            glm::vec3{0.0F, 0.0F, 1.0F},
        };
        m_world.StaticBoxes()[wallEntity] = engine::scene::StaticBoxComponent{wall.halfExtents, true};
    }

    m_staticBatcher.BeginBuild();
    for (const auto& wall : generated.walls)
    {
        m_staticBatcher.AddBox(wall.center, wall.halfExtents, glm::vec3{0.58F, 0.62F, 0.68F});
    }
    m_staticBatcher.EndBuild();

    // Store loop mesh placements for later loading and rendering
    m_loopMeshes.clear();
    m_loopMeshesUploaded = false;
    std::cout << "[LOOP_MESH] Processing " << generated.meshPlacements.size() << " mesh placements from generated map\n";
    for (const auto& placement : generated.meshPlacements)
    {
        if (placement.meshPath.empty())
        {
            continue;
        }
        m_loopMeshes.push_back(LoopMeshInstance{
            placement.meshPath,
            engine::render::Renderer::kInvalidGpuMesh,  // Will be uploaded in RenderLoopMeshes()
            placement.position,
            placement.rotationDegrees,
            glm::vec3{2.0F, 3.0F, 2.0F},  // Default bounds, will be updated on load
        });
    }

    // For the Test map, add loop element meshes directly at fixed positions for testing
    if (mapType == MapType::Test && m_loopMeshes.empty())
    {
        std::cout << "[LOOP_MESH] Adding test loop meshes to Test map (auto-collider generation enabled)\n";

        // List of meshes to spawn - colliders will be auto-generated from mesh geometry
        const char* testMeshPaths[] = {
            "assets/meshes/loop_elements/Wall.glb",
            "assets/meshes/loop_elements/Wall_Simple.glb",
            "assets/meshes/loop_elements/Window.glb",
            "assets/meshes/loop_elements/L wall.glb",
            "assets/meshes/loop_elements/T wall.glb",
            "assets/meshes/loop_elements/Wall left end.glb",
            "assets/meshes/loop_elements/Wall right end.glb",
        };

        int col = 0;
        int row = 0;
        for (const char* meshPath : testMeshPaths)
        {
            float x = -12.0f + col * 12.0f;
            float z = -12.0f + row * 12.0f;
            float y = 1.5f;  // Half of 3m height to sit on ground

            // Add mesh instance - collision will be auto-generated when mesh is loaded
            m_loopMeshes.push_back(LoopMeshInstance{
                meshPath,
                engine::render::Renderer::kInvalidGpuMesh,  // Will be loaded in RenderLoopMeshes
                glm::vec3{x, y, z},
                0.0f,
                glm::vec3{1.0f, 1.5f, 0.5f},  // Placeholder, will be updated from mesh bounds
                false,  // collisionCreated - will be set when generated
            });

            col++;
            if (col >= 4)
            {
                col = 0;
                row++;
            }
        }
        std::cout << "[LOOP_MESH] Added " << m_loopMeshes.size() << " test meshes (colliders will be auto-generated)\n";
    }

    if (!m_loopMeshes.empty())
    {
        std::cout << "[LOOP_MESH] Queued " << m_loopMeshes.size() << " loop mesh placements for loading\n";
    }

    m_spawnPoints.push_back(SpawnPointInfo{
        m_nextSpawnPointId++,
        SpawnPointType::Survivor,
        generated.survivorSpawn,
    });
    m_spawnPoints.push_back(SpawnPointInfo{
        m_nextSpawnPointId++,
        SpawnPointType::Killer,
        generated.killerSpawn,
    });
    const glm::vec3 centerSpawn = (generated.survivorSpawn + generated.killerSpawn) * 0.5F;
    m_spawnPoints.push_back(SpawnPointInfo{
        m_nextSpawnPointId++,
        SpawnPointType::Generic,
        centerSpawn,
    });
    for (const auto& tile : generated.tiles)
    {
        m_spawnPoints.push_back(SpawnPointInfo{
            m_nextSpawnPointId++,
            SpawnPointType::Generic,
            tile.center + glm::vec3{0.0F, 1.05F, 0.0F},
        });
    }

    for (const auto& windowSpawn : generated.windows)
    {
        const engine::scene::Entity windowEntity = m_world.CreateEntity();
        m_world.Transforms()[windowEntity] = engine::scene::Transform{
            windowSpawn.center,
            glm::vec3{0.0F},
            glm::vec3{1.0F},
            windowSpawn.normal,
        };

        engine::scene::WindowComponent window;
        window.halfExtents = windowSpawn.halfExtents;
        window.normal = glm::normalize(windowSpawn.normal);
        window.survivorVaultTime = 0.6F;
        window.killerVaultMultiplier = 1.55F;
        m_world.Windows()[windowEntity] = window;
    }

    for (const auto& palletSpawn : generated.pallets)
    {
        const engine::scene::Entity palletEntity = m_world.CreateEntity();
        m_world.Transforms()[palletEntity] = engine::scene::Transform{
            palletSpawn.center,
            glm::vec3{0.0F},
            glm::vec3{1.0F},
            glm::vec3{1.0F, 0.0F, 0.0F},
        };

        engine::scene::PalletComponent pallet;
        const bool xMajor = palletSpawn.halfExtents.x >= palletSpawn.halfExtents.z;
        pallet.standingHalfExtents = xMajor
                                         ? glm::vec3{std::max(0.24F, palletSpawn.halfExtents.x), 1.08F, 0.24F}
                                         : glm::vec3{0.24F, 1.08F, std::max(0.24F, palletSpawn.halfExtents.z)};
        pallet.droppedHalfExtents = xMajor
                                        ? glm::vec3{std::max(0.9F, palletSpawn.halfExtents.x), 0.58F, 0.34F}
                                        : glm::vec3{0.34F, 0.58F, std::max(0.9F, palletSpawn.halfExtents.z)};
        pallet.halfExtents = pallet.standingHalfExtents;
        pallet.standingCenterY = std::max(1.08F, palletSpawn.center.y);
        pallet.droppedCenterY = std::max(0.58F, palletSpawn.center.y * 0.75F);
        pallet.state = engine::scene::PalletState::Standing;
        pallet.breakDuration = 1.8F;
        m_world.Pallets()[palletEntity] = pallet;
        m_world.Transforms()[palletEntity].position.y = pallet.standingCenterY;
    }

    const std::array<glm::vec3, 4> hookOffsets{
        glm::vec3{6.0F, 1.2F, 6.0F},
        glm::vec3{-6.0F, 1.2F, 6.0F},
        glm::vec3{6.0F, 1.2F, -6.0F},
        glm::vec3{-6.0F, 1.2F, -6.0F},
    };
    for (const glm::vec3& offset : hookOffsets)
    {
        const engine::scene::Entity hookEntity = m_world.CreateEntity();
        const glm::vec3 hookPos = (generated.survivorSpawn + generated.killerSpawn) * 0.5F + offset;
        m_world.Transforms()[hookEntity] = engine::scene::Transform{
            hookPos,
            glm::vec3{0.0F},
            glm::vec3{1.0F},
            glm::vec3{0.0F, 0.0F, 1.0F},
        };
        m_world.Hooks()[hookEntity] = engine::scene::HookComponent{};
        m_world.Names()[hookEntity] = engine::scene::NameComponent{"hook"};
    }

    // Spawn generators at positions from the map (attached to loops)
    for (const glm::vec3& generatorPos : generated.generatorSpawns)
    {
        const engine::scene::Entity generatorEntity = m_world.CreateEntity();
        m_world.Transforms()[generatorEntity] = engine::scene::Transform{
            generatorPos,
            glm::vec3{0.0F},
            glm::vec3{1.0F},
            glm::vec3{0.0F, 0.0F, 1.0F},
        };
        m_world.Generators()[generatorEntity] = engine::scene::GeneratorComponent{};
        m_world.Names()[generatorEntity] = engine::scene::NameComponent{"generator"};
    }

    // Generate high-poly meshes for benchmark map GPU stress test
    if (!generated.highPolyMeshes.empty())
    {
        m_highPolyMeshes.reserve(generated.highPolyMeshes.size());
        
        for (const auto& meshSpawn : generated.highPolyMeshes)
        {
            HighPolyMesh mesh;
            mesh.position = meshSpawn.position;
            mesh.rotation = meshSpawn.rotation;
            mesh.scale = meshSpawn.scale;
            mesh.color = meshSpawn.color;
            
            // Generate geometry based on type
            switch (meshSpawn.type)
            {
                case maps::HighPolyMeshSpawn::Type::IcoSphere:
                    mesh.geometry = GenerateIcoSphere(meshSpawn.detailLevel);
                    mesh.mediumLodGeometry = GenerateIcoSphere(std::max(1, meshSpawn.detailLevel - 2));
                    break;
                case maps::HighPolyMeshSpawn::Type::Torus:
                    mesh.geometry = GenerateTorus(
                        1.0F, 0.4F, 
                        16 + meshSpawn.detailLevel * 8, 
                        8 + meshSpawn.detailLevel * 4
                    );
                    mesh.mediumLodGeometry = GenerateTorus(
                        1.0F,
                        0.4F,
                        std::max(10, 10 + meshSpawn.detailLevel * 3),
                        std::max(6, 6 + meshSpawn.detailLevel * 2)
                    );
                    break;
                case maps::HighPolyMeshSpawn::Type::GridPlane:
                    mesh.geometry = GenerateGridPlane(2 << meshSpawn.detailLevel, 2 << meshSpawn.detailLevel);
                    mesh.mediumLodGeometry = GenerateGridPlane(
                        2 << std::max(2, meshSpawn.detailLevel - 2),
                        2 << std::max(2, meshSpawn.detailLevel - 2)
                    );
                    break;
                case maps::HighPolyMeshSpawn::Type::SpiralStair:
                    mesh.geometry = GenerateSpiralStair(32 + meshSpawn.detailLevel * 8, 16);
                    mesh.mediumLodGeometry = GenerateSpiralStair(
                        std::max(18, 16 + meshSpawn.detailLevel * 3),
                        12
                    );
                    break;
            }
            
            // Compute bounding box for frustum culling
            mesh.halfExtents = ComputeMeshBounds(mesh.geometry) * mesh.scale;
            
            m_highPolyMeshes.push_back(std::move(mesh));
        }
    }

    // Use DBD-inspired spawn system if enabled, otherwise use legacy spawns
    if (generated.useDbdSpawns && !generated.survivorSpawns.empty())
    {
        // Use new spawn system positions (currently single survivor for testing)
        m_survivor = SpawnActor(m_world, engine::scene::Role::Survivor, generated.survivorSpawns[0], glm::vec3{0.2F, 0.95F, 0.2F});
    }
    else
    {
        // Legacy spawn system
        m_survivor = SpawnActor(m_world, engine::scene::Role::Survivor, generated.survivorSpawn, glm::vec3{0.2F, 0.95F, 0.2F});
    }
    m_killer = SpawnActor(m_world, engine::scene::Role::Killer, generated.killerSpawn, glm::vec3{0.95F, 0.2F, 0.2F});
    ApplyGameplayTuning(m_tuning);
    SetRoleSpeedPercent("survivor", m_survivorSpeedPercent);
    SetRoleSpeedPercent("killer", m_killerSpeedPercent);
    if (const auto survivorActorIt = m_world.Actors().find(m_survivor); survivorActorIt != m_world.Actors().end())
    {
        SetRoleCapsuleSize("survivor", survivorActorIt->second.capsuleRadius, survivorActorIt->second.capsuleHeight);
    }
    if (const auto killerActorIt = m_world.Actors().find(m_killer); killerActorIt != m_world.Actors().end())
    {
        SetRoleCapsuleSize("killer", killerActorIt->second.capsuleRadius, killerActorIt->second.capsuleHeight);
    }
    SetSurvivorState(SurvivorHealthState::Healthy, "Map spawn", true);
    ResetItemAndPowerRuntimeState();
    SpawnInitialTrapperGroundTraps();
    m_generatorsTotal = static_cast<int>(m_world.Generators().size());
    RefreshGeneratorsCompleted();

    m_controlledRole = ControlledRole::Survivor;

    RebuildPhysicsWorld();
    UpdateInteractionCandidate();
}

void GameplaySystems::RebuildPhysicsWorld()
{
    m_physics.Clear();

    for (const auto& [entity, box] : m_world.StaticBoxes())
    {
        if (!box.solid)
        {
            continue;
        }

        const auto transformIt = m_world.Transforms().find(entity);
        if (transformIt == m_world.Transforms().end())
        {
            continue;
        }

        m_physics.AddSolidBox(engine::physics::SolidBox{
            .entity = entity,
            .center = transformIt->second.position,
            .halfExtents = box.halfExtents,
            .layer = engine::physics::CollisionLayer::Environment,
            .blocksSight = true,
        });
    }

    for (const auto& [entity, pallet] : m_world.Pallets())
    {
        const auto transformIt = m_world.Transforms().find(entity);
        if (transformIt == m_world.Transforms().end())
        {
            continue;
        }

        if (pallet.state == engine::scene::PalletState::Dropped)
        {
            m_physics.AddSolidBox(engine::physics::SolidBox{
                .entity = entity,
                .center = transformIt->second.position,
                .halfExtents = pallet.halfExtents,
                .layer = engine::physics::CollisionLayer::Environment,
                .blocksSight = false,
            });
        }

        if (pallet.state != engine::scene::PalletState::Broken)
        {
            m_physics.AddTrigger(engine::physics::TriggerVolume{
                .entity = entity,
                .center = transformIt->second.position,
                .halfExtents = pallet.halfExtents + glm::vec3{0.65F, 0.3F, 0.65F},
                .kind = engine::physics::TriggerKind::Interaction,
            });
        }
    }

    for (const auto& [entity, window] : m_world.Windows())
    {
        const auto transformIt = m_world.Transforms().find(entity);
        if (transformIt == m_world.Transforms().end())
        {
            continue;
        }

        glm::vec3 windowNormal{transformIt->second.forward.x, 0.0F, transformIt->second.forward.z};
        if (glm::length(windowNormal) < 1.0e-5F)
        {
            windowNormal = glm::vec3{window.normal.x, 0.0F, window.normal.z};
        }
        if (glm::length(windowNormal) < 1.0e-5F)
        {
            windowNormal = glm::vec3{0.0F, 0.0F, 1.0F};
        }
        windowNormal = glm::normalize(windowNormal);
        const float windowYawDegrees = glm::degrees(std::atan2(windowNormal.x, windowNormal.z));
        const glm::vec3 normalAxisWeight = glm::abs(glm::vec3{windowNormal.x, 0.0F, windowNormal.z});

        // Inner zone: same as window footprint (XZ) and original gameplay height (Y).
        // Outer trigger: expanded to make approach/vault from both sides easier.
        const glm::vec3 triggerHalfExtents{
            window.halfExtents.x + 0.55F + normalAxisWeight.x * 1.05F,
            window.halfExtents.y + 0.35F,
            window.halfExtents.z + 0.55F + normalAxisWeight.z * 1.05F,
        };

        m_physics.AddTrigger(engine::physics::TriggerVolume{
            .entity = entity,
            .center = transformIt->second.position,
            .halfExtents = triggerHalfExtents,
            .yawDegrees = windowYawDegrees,
            .kind = engine::physics::TriggerKind::Vault,
        });
    }

    for (const auto& [entity, hook] : m_world.Hooks())
    {
        const auto transformIt = m_world.Transforms().find(entity);
        if (transformIt == m_world.Transforms().end())
        {
            continue;
        }

        m_physics.AddTrigger(engine::physics::TriggerVolume{
            .entity = entity,
            .center = transformIt->second.position,
            .halfExtents = hook.halfExtents + glm::vec3{0.5F, 0.4F, 0.5F},
            .kind = engine::physics::TriggerKind::Interaction,
        });
    }

    for (const auto& [entity, generator] : m_world.Generators())
    {
        const auto transformIt = m_world.Transforms().find(entity);
        if (transformIt == m_world.Transforms().end() || generator.completed)
        {
            continue;
        }

        m_physics.AddTrigger(engine::physics::TriggerVolume{
            .entity = entity,
            .center = transformIt->second.position,
            .halfExtents = generator.halfExtents + glm::vec3{0.3F, 0.2F, 0.3F},  // Zmniejszone: 0.7->0.3, 0.45->0.2
            .kind = engine::physics::TriggerKind::Interaction,
        });
    }

    for (const auto& [entity, trap] : m_world.BearTraps())
    {
        const auto transformIt = m_world.Transforms().find(entity);
        if (transformIt == m_world.Transforms().end())
        {
            continue;
        }

        m_physics.AddTrigger(engine::physics::TriggerVolume{
            .entity = entity,
            .center = transformIt->second.position,
            .halfExtents = trap.halfExtents + glm::vec3{0.35F, 0.25F, 0.35F},
            .kind = engine::physics::TriggerKind::Interaction,
        });
    }

    if (m_killer != 0)
    {
        const auto killerTransformIt = m_world.Transforms().find(m_killer);
        if (killerTransformIt != m_world.Transforms().end())
        {
            m_physics.AddTrigger(engine::physics::TriggerVolume{
                .entity = m_killer,
                .center = killerTransformIt->second.position,
                .halfExtents = glm::vec3{m_chase.startDistance, 2.0F, m_chase.startDistance},
                .kind = engine::physics::TriggerKind::Chase,
            });
        }
    }
}

void GameplaySystems::DestroyEntity(engine::scene::Entity entity)
{
    if (entity == 0)
    {
        return;
    }

    m_world.Transforms().erase(entity);
    m_world.Actors().erase(entity);
    m_world.StaticBoxes().erase(entity);
    m_world.Windows().erase(entity);
    m_world.Pallets().erase(entity);
    m_world.Hooks().erase(entity);
    m_world.Generators().erase(entity);
    m_world.BearTraps().erase(entity);
    m_world.GroundItems().erase(entity);
    m_world.DebugColors().erase(entity);
    m_world.Names().erase(entity);
}

bool GameplaySystems::ResolveSpawnPositionValid(
    const glm::vec3& requestedPosition,
    float radius,
    float height,
    glm::vec3* outResolved
)
{
    RebuildPhysicsWorld();
    const std::array<glm::vec3, 12> offsets{
        glm::vec3{0.0F, 0.0F, 0.0F},
        glm::vec3{0.5F, 0.0F, 0.0F},
        glm::vec3{-0.5F, 0.0F, 0.0F},
        glm::vec3{0.0F, 0.0F, 0.5F},
        glm::vec3{0.0F, 0.0F, -0.5F},
        glm::vec3{1.0F, 0.0F, 0.0F},
        glm::vec3{-1.0F, 0.0F, 0.0F},
        glm::vec3{0.0F, 0.0F, 1.0F},
        glm::vec3{0.0F, 0.0F, -1.0F},
        glm::vec3{0.8F, 0.0F, 0.8F},
        glm::vec3{-0.8F, 0.0F, 0.8F},
        glm::vec3{0.8F, 0.0F, -0.8F},
    };

    for (const glm::vec3& offset : offsets)
    {
        glm::vec3 candidate = requestedPosition + offset;
        for (int i = 0; i < 8; ++i)
        {
            const engine::physics::MoveResult probe = m_physics.MoveCapsule(
                candidate,
                radius,
                height,
                glm::vec3{0.0F},
                true,
                0.0F
            );
            if (!probe.collided)
            {
                if (outResolved != nullptr)
                {
                    *outResolved = probe.position;
                }
                return true;
            }
            candidate.y += 0.25F;
        }
    }

    if (outResolved != nullptr)
    {
        *outResolved = requestedPosition;
    }
    return false;
}

std::optional<GameplaySystems::SpawnPointInfo> GameplaySystems::FindSpawnPointById(int spawnId) const
{
    for (const SpawnPointInfo& spawn : m_spawnPoints)
    {
        if (spawn.id == spawnId)
        {
            return spawn;
        }
    }
    return std::nullopt;
}

std::optional<GameplaySystems::SpawnPointInfo> GameplaySystems::FindSpawnPointByType(SpawnPointType type) const
{
    if (m_spawnPoints.empty())
    {
        return std::nullopt;
    }

    if (type == SpawnPointType::Survivor && m_killer != 0)
    {
        const auto killerTransformIt = m_world.Transforms().find(m_killer);
        if (killerTransformIt != m_world.Transforms().end())
        {
            const glm::vec3 killerPos = killerTransformIt->second.position;
            float bestDistance = -1.0F;
            std::optional<SpawnPointInfo> best;
            for (const SpawnPointInfo& spawn : m_spawnPoints)
            {
                if (spawn.type != SpawnPointType::Survivor && spawn.type != SpawnPointType::Generic)
                {
                    continue;
                }
                const float d = DistanceXZ(spawn.position, killerPos);
                if (d > bestDistance)
                {
                    bestDistance = d;
                    best = spawn;
                }
            }
            if (best.has_value())
            {
                return best;
            }
        }
    }

    for (const SpawnPointInfo& spawn : m_spawnPoints)
    {
        if (spawn.type == type)
        {
            return spawn;
        }
    }

    for (const SpawnPointInfo& spawn : m_spawnPoints)
    {
        if (spawn.type == SpawnPointType::Generic)
        {
            return spawn;
        }
    }

    return std::nullopt;
}

GameplaySystems::SpawnPointType GameplaySystems::SpawnPointTypeFromRole(const std::string& roleName) const
{
    return roleName == "killer" ? SpawnPointType::Killer : SpawnPointType::Survivor;
}

const char* GameplaySystems::SpawnTypeToText(SpawnPointType type) const
{
    switch (type)
    {
        case SpawnPointType::Survivor: return "Survivor";
        case SpawnPointType::Killer: return "Killer";
        case SpawnPointType::Generic: return "Generic";
        default: return "Generic";
    }
}

engine::scene::Entity GameplaySystems::SpawnRoleActorAt(const std::string& roleName, const glm::vec3& position)
{
    const bool killer = roleName == "killer";
    const engine::scene::Role role = killer ? engine::scene::Role::Killer : engine::scene::Role::Survivor;
    const engine::scene::Entity entity = SpawnActor(
        m_world,
        role,
        position,
        killer ? glm::vec3{0.95F, 0.2F, 0.2F} : glm::vec3{0.2F, 0.95F, 0.2F}
    );

    if (killer)
    {
        m_killer = entity;
    }
    else
    {
        m_survivor = entity;
        m_survivorVisualYawRadians = 0.0F;
        m_survivorVisualYawInitialized = false;
        m_survivorVisualTargetYawRadians = 0.0F;
        m_survivorVisualMoveInput = glm::vec2{0.0F};
        m_survivorVisualDesiredDirection = glm::vec3{0.0F};
    }

    ApplyGameplayTuning(m_tuning);
    return entity;
}

void GameplaySystems::UpdateActorLook(engine::scene::Entity entity, const glm::vec2& mouseDelta, float sensitivity)
{
    auto transformIt = m_world.Transforms().find(entity);
    if (transformIt == m_world.Transforms().end())
    {
        return;
    }

    engine::scene::Transform& transform = transformIt->second;

    transform.rotationEuler.y += mouseDelta.x * sensitivity;
    transform.rotationEuler.x -= mouseDelta.y * sensitivity;
    transform.rotationEuler.x = glm::clamp(transform.rotationEuler.x, -1.35F, 1.35F);

    transform.forward = ForwardFromYawPitch(transform.rotationEuler.y, transform.rotationEuler.x);
}

void GameplaySystems::UpdateActorMovement(
    engine::scene::Entity entity,
    const glm::vec2& moveAxis,
    bool sprinting,
    bool jumpPressed,
    bool crouchHeld,
    float fixedDt
)
{
    auto transformIt = m_world.Transforms().find(entity);
    auto actorIt = m_world.Actors().find(entity);
    if (transformIt == m_world.Transforms().end() || actorIt == m_world.Actors().end())
    {
        return;
    }

    engine::scene::Transform& transform = transformIt->second;
    engine::scene::ActorComponent& actor = actorIt->second;

    if (actor.stunTimer > 0.0F)
    {
        actor.stunTimer = std::max(0.0F, actor.stunTimer - fixedDt);
    }

    if (actor.vaultCooldown > 0.0F)
    {
        actor.vaultCooldown = std::max(0.0F, actor.vaultCooldown - fixedDt);
    }

    if (actor.vaulting)
    {
        actor.sprinting = false;
        actor.forwardRunupDistance = 0.0F;
        UpdateVaultState(entity, fixedDt);
        return;
    }

    if (actor.carried || actor.stunTimer > 0.0F)
    {
        actor.sprinting = false;
        actor.forwardRunupDistance = 0.0F;
        actor.velocity = glm::vec3{0.0F};
        actor.lastPenetrationDepth = 0.0F;
        actor.lastCollisionNormal = glm::vec3{0.0F, 1.0F, 0.0F};
        return;
    }

    if (entity == m_survivor &&
        (m_survivorState == SurvivorHealthState::Hooked ||
         m_survivorState == SurvivorHealthState::Trapped ||
         m_survivorState == SurvivorHealthState::Dead))
    {
        actor.sprinting = false;
        actor.forwardRunupDistance = 0.0F;
        actor.velocity = glm::vec3{0.0F};
        actor.lastPenetrationDepth = 0.0F;
        actor.lastCollisionNormal = glm::vec3{0.0F, 1.0F, 0.0F};
        return;
    }

    const float lookYaw = transform.rotationEuler.y;
    glm::vec3 movementForwardXZ = glm::normalize(glm::vec3{std::sin(lookYaw), 0.0F, -std::cos(lookYaw)});
    if (entity == ControlledEntity() && entity == m_survivor && m_cameraInitialized)
    {
        const glm::vec3 cameraFlat{m_cameraForward.x, 0.0F, m_cameraForward.z};
        if (glm::length(cameraFlat) > 1.0e-5F)
        {
            movementForwardXZ = glm::normalize(cameraFlat);
        }
    }
    const glm::vec3 movementRightXZ = glm::normalize(glm::cross(movementForwardXZ, glm::vec3{0.0F, 1.0F, 0.0F}));

    glm::vec3 moveDirection{0.0F};
    if (glm::length(moveAxis) > 1.0e-5F)
    {
        moveDirection = glm::normalize(movementRightXZ * moveAxis.x + movementForwardXZ * moveAxis.y);
    }

    float speed = actor.walkSpeed;
    actor.crawling = false;
    actor.crouching = false;
    if (actor.role == engine::scene::Role::Survivor && m_survivorState == SurvivorHealthState::Downed)
    {
        speed = m_tuning.survivorCrawlSpeed;
        sprinting = false;
        actor.crawling = true;
    }
    else if (actor.role == engine::scene::Role::Survivor && crouchHeld)
    {
        speed = m_tuning.survivorCrouchSpeed;
        sprinting = false;
        actor.crouching = true;
    }

    if (actor.role == engine::scene::Role::Survivor && sprinting)
    {
        speed = actor.sprintSpeed;
    }

    if (entity == m_survivor &&
        m_survivorHitHasteTimer > 0.0F &&
        (m_survivorState == SurvivorHealthState::Healthy || m_survivorState == SurvivorHealthState::Injured))
    {
        speed *= m_survivorHitHasteMultiplier;
    }
    if (entity == m_killer && m_killerSlowTimer > 0.0F)
    {
        speed *= m_killerSlowMultiplier;
    }
    if (entity == m_killer && m_killerLoadout.powerId == "wraith_cloak")
    {
        float baseCloakMoveSpeedMult = m_tuning.wraithCloakMoveSpeedMultiplier;
        if (const loadout::PowerDefinition* powerDef = m_loadoutCatalog.FindPower(m_killerLoadout.powerId))
        {
            const auto it = powerDef->params.find("cloak_speed_multiplier");
            if (it != powerDef->params.end())
            {
                baseCloakMoveSpeedMult = it->second;
            }
        }
        const float cloakMoveSpeedMult = std::max(
            1.0F,
            m_killerPowerModifiers.ApplyStat("cloak_speed_multiplier", baseCloakMoveSpeedMult)
        );
        if (m_killerPowerState.wraithCloaked || m_killerPowerState.wraithPostUncloakTimer > 0.0F)
        {
            speed *= cloakMoveSpeedMult;
        }
    }

    // Apply perk speed modifiers
    speed *= m_perkSystem.GetSpeedModifier(actor.role, sprinting, crouchHeld, actor.crawling);

    actor.sprinting = actor.role == engine::scene::Role::Survivor && sprinting;

    const glm::vec2 currentHorizontalVelocity{actor.velocity.x, actor.velocity.z};
    const glm::vec2 targetHorizontalVelocity{moveDirection.x * speed, moveDirection.z * speed};
    const bool hasMoveInput = glm::length(moveDirection) > 1.0e-5F;
    const float horizontalRate = hasMoveInput ? m_actorGroundAcceleration : m_actorGroundDeceleration;
    const glm::vec2 nextHorizontalVelocity = MoveTowardsVector(
        currentHorizontalVelocity,
        targetHorizontalVelocity,
        std::max(0.0F, horizontalRate * fixedDt)
    );
    actor.velocity.x = nextHorizontalVelocity.x;
    actor.velocity.z = nextHorizontalVelocity.y;

    if (entity == m_killer && m_killerAttackState == KillerAttackState::Lunging)
    {
        const glm::vec3 killerForwardXZ = glm::normalize(glm::vec3{transform.forward.x, 0.0F, transform.forward.z});
        actor.velocity.x = killerForwardXZ.x * m_killerCurrentLungeSpeed;
        actor.velocity.z = killerForwardXZ.z * m_killerCurrentLungeSpeed;
    }

    if (glm::length(moveDirection) > 1.0e-5F && glm::dot(moveDirection, movementForwardXZ) > 0.72F)
    {
        actor.forwardRunupDistance = std::min(actor.forwardRunupDistance + speed * fixedDt, 12.0F);
    }
    else
    {
        actor.forwardRunupDistance = 0.0F;
    }

    if (actor.noclipEnabled || m_noClipEnabled)
    {
        transform.position += moveDirection * speed * fixedDt;
        actor.grounded = false;
        actor.lastPenetrationDepth = 0.0F;
        actor.lastCollisionNormal = glm::vec3{0.0F, 1.0F, 0.0F};
        return;
    }

    if (actor.jumpEnabled && jumpPressed && actor.grounded)
    {
        actor.velocity.y = actor.jumpVelocity;
    }

    actor.velocity.y += kGravity * fixedDt;

    const engine::physics::MoveResult moveResult = m_physics.MoveCapsule(
        transform.position,
        actor.capsuleRadius,
        actor.capsuleHeight,
        actor.velocity * fixedDt,
        m_collisionEnabled && actor.collisionEnabled,
        actor.stepHeight
    );

    transform.position = moveResult.position;
    actor.grounded = moveResult.grounded;
    actor.lastCollisionNormal = moveResult.lastCollisionNormal;
    actor.lastPenetrationDepth = moveResult.maxPenetrationDepth;

    if (actor.grounded && actor.velocity.y < 0.0F)
    {
        actor.velocity.y = 0.0F;
    }

    if (moveResult.collided)
    {
        const float velocityIntoNormal = glm::dot(actor.velocity, moveResult.lastCollisionNormal);
        if (velocityIntoNormal < 0.0F)
        {
            actor.velocity -= moveResult.lastCollisionNormal * velocityIntoNormal;
        }
    }
}

void GameplaySystems::UpdateVaultState(engine::scene::Entity entity, float fixedDt)
{
    auto transformIt = m_world.Transforms().find(entity);
    auto actorIt = m_world.Actors().find(entity);
    if (transformIt == m_world.Transforms().end() || actorIt == m_world.Actors().end())
    {
        return;
    }

    engine::scene::Transform& transform = transformIt->second;
    engine::scene::ActorComponent& actor = actorIt->second;

    actor.vaultTimer += fixedDt;
    const float normalized = actor.vaultDuration > 0.0F ? glm::clamp(actor.vaultTimer / actor.vaultDuration, 0.0F, 1.0F) : 1.0F;

    const glm::vec3 linear = glm::mix(actor.vaultStart, actor.vaultEnd, normalized);
    const float arc = std::sin(normalized * kPi) * actor.vaultArcHeight;
    transform.position = linear + glm::vec3{0.0F, arc, 0.0F};

    if (normalized >= 1.0F)
    {
        actor.vaulting = false;
        actor.sprinting = false;
        actor.vaultTimer = 0.0F;
        actor.collisionEnabled = m_collisionEnabled;
        actor.vaultCooldown = 0.5F;
        AddRuntimeMessage("Vault ended", 1.5F);
    }
}

void GameplaySystems::UpdateInteractionCandidate()
{
    const engine::scene::Entity controlled = ControlledEntity();
    const auto actorIt = m_world.Actors().find(controlled);
    if (controlled == 0 || actorIt == m_world.Actors().end() || IsActorInputLocked(actorIt->second))
    {
        m_interactionCandidate = InteractionCandidate{};
        m_interactionPromptHoldSeconds = 0.0F;
        return;
    }
    if (controlled == m_survivor &&
        (m_survivorState == SurvivorHealthState::Downed ||
         m_survivorState == SurvivorHealthState::Trapped ||
         m_survivorState == SurvivorHealthState::Hooked ||
         m_survivorState == SurvivorHealthState::Dead))
    {
        m_interactionCandidate = InteractionCandidate{};
        m_interactionPromptHoldSeconds = 0.0F;
        return;
    }

    const InteractionCandidate resolved = ResolveInteractionCandidateFromView(controlled);
    if (resolved.type != InteractionType::None)
    {
        m_interactionCandidate = resolved;
        m_interactionPromptHoldSeconds = 0.2F;
    }
    else if (m_interactionPromptHoldSeconds > 0.0F && !m_interactionCandidate.prompt.empty())
    {
        m_interactionPromptHoldSeconds = std::max(0.0F, m_interactionPromptHoldSeconds - (1.0F / 60.0F));
    }
    else
    {
        m_interactionCandidate = InteractionCandidate{};
        m_interactionPromptHoldSeconds = 0.0F;
    }
}

void GameplaySystems::ExecuteInteractionForRole(engine::scene::Entity actorEntity, const InteractionCandidate& candidate)
{
    if (actorEntity == 0 || candidate.type == InteractionType::None)
    {
        return;
    }

    auto actorTransformIt = m_world.Transforms().find(actorEntity);
    if (actorTransformIt == m_world.Transforms().end() || !m_world.Actors().contains(actorEntity))
    {
        return;
    }

    auto snapActorToAnchor = [&](const glm::vec3& anchor, float maxSnapDistance) {
        engine::scene::Transform& actorTransform = actorTransformIt->second;
        const glm::vec3 actorAnchor = actorTransform.position;
        const float distance = DistanceXZ(actorAnchor, anchor);
        if (distance <= maxSnapDistance)
        {
            actorTransform.position.x = anchor.x;
            actorTransform.position.z = anchor.z;
        }
    };

    if (candidate.type == InteractionType::WindowVault)
    {
        const auto windowIt = m_world.Windows().find(candidate.entity);
        const auto windowTransformIt = m_world.Transforms().find(candidate.entity);
        if (windowIt != m_world.Windows().end() && windowTransformIt != m_world.Transforms().end())
        {
            const glm::vec3 normal = glm::length(windowIt->second.normal) > 1.0e-5F
                                         ? glm::normalize(windowIt->second.normal)
                                         : glm::vec3{0.0F, 0.0F, 1.0F};
            const float side = glm::dot(actorTransformIt->second.position - windowTransformIt->second.position, normal) >= 0.0F ? 1.0F : -1.0F;
            const float windowThicknessAlongNormal =
                std::abs(normal.x) * windowIt->second.halfExtents.x +
                std::abs(normal.y) * windowIt->second.halfExtents.y +
                std::abs(normal.z) * windowIt->second.halfExtents.z;
            const glm::vec3 anchor = windowTransformIt->second.position + normal * side * (windowThicknessAlongNormal + 0.55F);
            snapActorToAnchor(anchor, 0.6F);
        }
        BeginWindowVault(actorEntity, candidate.entity);
        return;
    }

    if (candidate.type == InteractionType::PalletVault)
    {
        const auto palletTransformIt = m_world.Transforms().find(candidate.entity);
        if (palletTransformIt != m_world.Transforms().end())
        {
            snapActorToAnchor(palletTransformIt->second.position, 0.6F);
        }
        BeginPalletVault(actorEntity, candidate.entity);
        return;
    }

    if (candidate.type == InteractionType::DropPallet)
    {
        auto palletIt = m_world.Pallets().find(candidate.entity);
        auto palletTransformIt = m_world.Transforms().find(candidate.entity);
        if (palletIt != m_world.Pallets().end() && palletTransformIt != m_world.Transforms().end() &&
            palletIt->second.state == engine::scene::PalletState::Standing)
        {
            snapActorToAnchor(palletTransformIt->second.position, 0.6F);
            palletIt->second.state = engine::scene::PalletState::Dropped;
            palletIt->second.breakTimer = 0.0F;
            palletIt->second.halfExtents = palletIt->second.droppedHalfExtents;
            palletTransformIt->second.position.y = palletIt->second.droppedCenterY;
            const engine::fx::FxNetMode netMode = m_networkAuthorityMode ? engine::fx::FxNetMode::ServerBroadcast
                                                                          : engine::fx::FxNetMode::Local;
            SpawnGameplayFx(
                "dust_puff",
                palletTransformIt->second.position + glm::vec3{0.0F, 0.18F, 0.0F},
                actorTransformIt->second.forward,
                netMode
            );
            AddRuntimeMessage("Pallet: standing -> dropped", 2.0F);
            TryStunKillerFromPallet(candidate.entity);
        }
        return;
    }

    if (candidate.type == InteractionType::BreakPallet)
    {
        auto palletTransformIt = m_world.Transforms().find(candidate.entity);
        if (palletTransformIt != m_world.Transforms().end())
        {
            snapActorToAnchor(palletTransformIt->second.position, 0.6F);
        }
        auto palletIt = m_world.Pallets().find(candidate.entity);
        if (palletIt != m_world.Pallets().end() && palletIt->second.state == engine::scene::PalletState::Dropped && palletIt->second.breakTimer <= 0.0F)
        {
            float breakTime = palletIt->second.breakDuration;
            if (m_killerLoadout.powerId == "wraith_cloak" && m_killerPowerState.wraithCloaked)
            {
                breakTime /= m_tuning.wraithCloakPalletBreakSpeedMult;
            }
            palletIt->second.breakTimer = breakTime;
            m_killerBreakingPallet = candidate.entity;
            const engine::fx::FxNetMode netMode = m_networkAuthorityMode ? engine::fx::FxNetMode::ServerBroadcast
                                                                          : engine::fx::FxNetMode::Local;
            SpawnGameplayFx(
                "hit_spark",
                palletTransformIt != m_world.Transforms().end() ? palletTransformIt->second.position + glm::vec3{0.0F, 0.4F, 0.0F}
                                                                 : glm::vec3{0.0F, 0.4F, 0.0F},
                actorTransformIt->second.forward,
                netMode
            );
            AddRuntimeMessage("Pallet break started", 2.0F);
        }
        return;
    }

    if (candidate.type == InteractionType::PickupSurvivor)
    {
        TryPickupDownedSurvivor();
        return;
    }

    if (candidate.type == InteractionType::DropSurvivor)
    {
        if (m_survivorState != SurvivorHealthState::Carried || m_survivor == 0 || m_killer == 0)
        {
            return;
        }

        const auto killerTransformIt = m_world.Transforms().find(m_killer);
        const auto survivorTransformIt = m_world.Transforms().find(m_survivor);
        if (killerTransformIt != m_world.Transforms().end() && survivorTransformIt != m_world.Transforms().end())
        {
            const glm::vec3 killerForward = glm::length(killerTransformIt->second.forward) > 1.0e-5F
                                                ? glm::normalize(killerTransformIt->second.forward)
                                                : glm::vec3{0.0F, 0.0F, -1.0F};
            survivorTransformIt->second.position =
                killerTransformIt->second.position - killerForward * 0.95F + glm::vec3{0.0F, 0.0F, 0.55F};
        }

        SetSurvivorState(SurvivorHealthState::Downed, "Killer manual drop");
        AddRuntimeMessage("Carry drop reason: killer manual drop", 1.5F);
        return;
    }

    if (candidate.type == InteractionType::HookSurvivor)
    {
        const auto hookTransformIt = m_world.Transforms().find(candidate.entity);
        if (hookTransformIt != m_world.Transforms().end())
        {
            snapActorToAnchor(hookTransformIt->second.position, 0.6F);
        }
        TryHookCarriedSurvivor(candidate.entity);
        return;
    }

    if (candidate.type == InteractionType::RepairGenerator)
    {
        const auto generatorTransformIt = m_world.Transforms().find(candidate.entity);
        if (generatorTransformIt != m_world.Transforms().end())
        {
            snapActorToAnchor(generatorTransformIt->second.position, 0.6F);
        }
        BeginOrContinueGeneratorRepair(candidate.entity);
        return;
    }

    if (candidate.type == InteractionType::SelfHeal)
    {
        BeginSelfHeal();
    }

    if (candidate.type == InteractionType::ReplenishHatchets)
    {
        // Start locker replenish channeling
        if (!m_killerPowerState.lockerReplenishing &&
            m_killerPowerState.hatchetCount < m_killerPowerState.hatchetMaxCount)
        {
            m_killerPowerState.lockerReplenishing = true;
            m_killerPowerState.lockerReplenishTimer = 0.0F;
            m_killerPowerState.lockerTargetEntity = candidate.entity;
            AddRuntimeMessage("Replenishing hatchets...", 1.0F);
            ItemPowerLog("Started locker replenish");
        }
    }
}

void GameplaySystems::TryKillerHit()
{
    (void)ResolveKillerAttackHit(m_killerShortRange, m_killerShortHalfAngleRadians);
}

bool GameplaySystems::ResolveKillerAttackHit(float range, float halfAngleRadians, const glm::vec3& directionOverride)
{
    if (m_killer == 0 || m_survivor == 0)
    {
        return false;
    }

    if (m_survivorState == SurvivorHealthState::Carried ||
        m_survivorState == SurvivorHealthState::Downed ||
        m_survivorState == SurvivorHealthState::Hooked ||
        m_survivorState == SurvivorHealthState::Dead)
    {
        return false;
    }

    const auto killerTransformIt = m_world.Transforms().find(m_killer);
    const auto survivorTransformIt = m_world.Transforms().find(m_survivor);
    const auto survivorActorIt = m_world.Actors().find(m_survivor);
    if (killerTransformIt == m_world.Transforms().end() ||
        survivorTransformIt == m_world.Transforms().end() ||
        survivorActorIt == m_world.Actors().end())
    {
        return false;
    }

    glm::vec3 attackOrigin = killerTransformIt->second.position + glm::vec3{0.0F, 0.9F, 0.0F};
    glm::vec3 attackForward = killerTransformIt->second.forward;
    if (glm::length(directionOverride) > 1.0e-5F)
    {
        attackForward = directionOverride;
    }
    else if (m_controlledRole == ControlledRole::Killer && ResolveCameraMode() == CameraMode::FirstPerson)
    {
        attackOrigin = m_cameraPosition;
        attackForward = m_cameraForward;
    }
    if (glm::length(attackForward) < 1.0e-5F)
    {
        attackForward = glm::vec3{0.0F, 0.0F, -1.0F};
    }
    attackForward = glm::normalize(attackForward);

    m_lastSwingOrigin = attackOrigin;
    m_lastSwingDirection = attackForward;
    m_lastSwingRange = range;
    m_lastSwingHalfAngleRadians = halfAngleRadians;
    m_lastSwingDebugTtl = 0.45F;
    m_lastHitRayStart = attackOrigin;
    m_lastHitRayEnd = attackOrigin + attackForward * range;
    m_lastHitConnected = false;

    const float cosThreshold = std::cos(halfAngleRadians);
    const glm::vec3 survivorPoint = survivorTransformIt->second.position + glm::vec3{0.0F, 0.55F, 0.0F};
    const glm::vec3 toSurvivor = survivorPoint - attackOrigin;
    const float distanceToSurvivor = glm::length(toSurvivor);
    if (distanceToSurvivor > range + survivorActorIt->second.capsuleRadius || distanceToSurvivor < 1.0e-5F)
    {
        return false;
    }

    const glm::vec3 toSurvivorDirection = toSurvivor / distanceToSurvivor;
    if (glm::dot(attackForward, toSurvivorDirection) < cosThreshold)
    {
        return false;
    }

    const std::optional<engine::physics::RaycastHit> blockHit = m_physics.RaycastNearest(attackOrigin, survivorPoint);
    if (blockHit.has_value())
    {
        return false;
    }

    m_lastHitConnected = true;
    m_killerAttackFlashTtl = 0.12F;
    const engine::fx::FxNetMode netMode = m_networkAuthorityMode ? engine::fx::FxNetMode::ServerBroadcast
                                                                  : engine::fx::FxNetMode::Local;
    SpawnGameplayFx("hit_spark", survivorPoint, attackForward, netMode);
    SpawnGameplayFx("blood_spray", survivorPoint + glm::vec3{0.0F, 0.08F, 0.0F}, attackForward, netMode);
    ApplySurvivorHit();
    AddRuntimeMessage("Killer hit confirmed", 1.3F);
    return true;
}

void GameplaySystems::UpdateKillerAttack(const RoleCommand& killerCommand, float fixedDt)
{
    // Block attacks during nurse blink fatigue (except blink attack windup handles its own attack)
    if (m_killerLoadout.powerId == "nurse_blink" &&
        m_killerPowerState.blinkState == NurseBlinkState::Fatigue)
    {
        m_previousAttackHeld = false;
        m_killerAttackState = KillerAttackState::Idle;
        m_killerAttackStateTimer = 0.0F;
        m_killerLungeChargeSeconds = 0.0F;
        return;
    }

    if (m_killerLoadout.powerId == "wraith_cloak" &&
        (m_killerPowerState.wraithCloaked || m_killerPowerState.wraithCloakTransition))
    {
        m_previousAttackHeld = false;
        m_killerAttackState = KillerAttackState::Idle;
        m_killerAttackStateTimer = 0.0F;
        m_killerLungeChargeSeconds = 0.0F;
        return;
    }

    if (m_killerHitCooldown > 0.0F)
    {
        m_killerHitCooldown = std::max(0.0F, m_killerHitCooldown - fixedDt);
    }

    if (m_killerAttackState == KillerAttackState::Recovering)
    {
        m_killerAttackStateTimer = std::max(0.0F, m_killerAttackStateTimer - fixedDt);
        if (m_killerAttackStateTimer <= 0.0F)
        {
            m_killerAttackState = KillerAttackState::Idle;
        }
        return;
    }

    if (m_killerAttackState == KillerAttackState::Lunging)
    {
        m_killerAttackStateTimer += fixedDt;
        m_killerLungeChargeSeconds = std::min(m_killerAttackStateTimer, m_killerLungeDurationSeconds);
        const float lunge01 = glm::clamp(
            m_killerLungeChargeSeconds / std::max(0.01F, m_killerLungeDurationSeconds),
            0.0F,
            1.0F
        );
        m_killerCurrentLungeSpeed = glm::mix(m_killerLungeSpeedStart, m_killerLungeSpeedEnd, lunge01);

        const bool endedByRelease = !killerCommand.attackHeld;
        const bool endedByTimeout = m_killerAttackStateTimer >= m_killerLungeDurationSeconds;
        if (endedByRelease || endedByTimeout)
        {
            const bool hit = ResolveKillerAttackHit(m_killerLungeRange, m_killerLungeHalfAngleRadians);
            ApplyKillerAttackAftermath(hit, true);
            m_killerAttackHitThisAction = hit;
            m_killerAttackState = KillerAttackState::Recovering;
            m_killerAttackStateTimer = hit ? m_killerLungeRecoverSeconds : m_killerMissRecoverSeconds;
            m_killerHitCooldown = m_killerAttackStateTimer;
            m_killerLungeChargeSeconds = 0.0F;
            m_killerCurrentLungeSpeed = 0.0F;
        }
        return;
    }

    if (m_killerAttackState != KillerAttackState::Idle || m_killerHitCooldown > 0.0F)
    {
        return;
    }

    if (!m_previousAttackHeld && killerCommand.attackPressed)
    {
        m_previousAttackHeld = true;
        m_killerLungeChargeSeconds = 0.0F;
    }

    if (!m_previousAttackHeld)
    {
        return;
    }

    if (killerCommand.attackHeld)
    {
        m_killerLungeChargeSeconds += fixedDt;
        if (m_killerLungeChargeSeconds >= m_killerLungeChargeMinSeconds)
        {
            m_previousAttackHeld = false;
            m_killerAttackState = KillerAttackState::Lunging;
            m_killerAttackStateTimer = 0.0F;
            m_killerCurrentLungeSpeed = m_killerLungeSpeedStart;
            m_killerAttackHitThisAction = false;
            AddRuntimeMessage("Killer lunge", 0.9F);
        }
        return;
    }

    if (killerCommand.attackReleased || !killerCommand.attackHeld)
    {
        const bool hit = ResolveKillerAttackHit(m_killerShortRange, m_killerShortHalfAngleRadians);
        ApplyKillerAttackAftermath(hit, false);
        m_killerAttackHitThisAction = hit;
        m_killerAttackState = KillerAttackState::Recovering;
        m_killerAttackStateTimer = hit ? m_killerShortRecoverSeconds : m_killerMissRecoverSeconds;
        m_killerHitCooldown = m_killerAttackStateTimer;
        m_killerLungeChargeSeconds = 0.0F;
        m_previousAttackHeld = false;
    }
}

void GameplaySystems::UpdatePalletBreak(float fixedDt)
{
    if (m_killerBreakingPallet == 0)
    {
        return;
    }

    auto palletIt = m_world.Pallets().find(m_killerBreakingPallet);
    if (palletIt == m_world.Pallets().end())
    {
        m_killerBreakingPallet = 0;
        return;
    }

    engine::scene::PalletComponent& pallet = palletIt->second;
    if (pallet.state != engine::scene::PalletState::Dropped)
    {
        m_killerBreakingPallet = 0;
        return;
    }

    pallet.breakTimer = std::max(0.0F, pallet.breakTimer - fixedDt);
    if (pallet.breakTimer <= 0.0F)
    {
        pallet.state = engine::scene::PalletState::Broken;
        pallet.halfExtents = glm::vec3{0.12F, 0.08F, 0.12F};
        m_physicsDirty = true;
        auto transformIt = m_world.Transforms().find(m_killerBreakingPallet);
        if (transformIt != m_world.Transforms().end())
        {
            const engine::fx::FxNetMode netMode = m_networkAuthorityMode ? engine::fx::FxNetMode::ServerBroadcast
                                                                          : engine::fx::FxNetMode::Local;
            SpawnGameplayFx("dust_puff", transformIt->second.position + glm::vec3{0.0F, 0.2F, 0.0F}, glm::vec3{0.0F, 1.0F, 0.0F}, netMode);
            transformIt->second.position.y = -20.0F;
        }

        // Reset bloodlust on pallet break (DBD-like)
        if (m_bloodlust.tier > 0)
        {
            ResetBloodlust();
        }

        AddRuntimeMessage("Pallet: dropped -> broken", 2.0F);
        m_killerBreakingPallet = 0;
    }
}

void GameplaySystems::UpdateChaseState(float fixedDt)
{
    const bool wasChasing = m_chase.isChasing;
    const auto killerTransformIt = m_world.Transforms().find(m_killer);
    const auto survivorTransformIt = m_world.Transforms().find(m_survivor);
    const auto survivorActorIt = m_world.Actors().find(m_survivor);

    if (killerTransformIt == m_world.Transforms().end() ||
        survivorTransformIt == m_world.Transforms().end() ||
        survivorActorIt == m_world.Actors().end())
    {
        m_chase.isChasing = false;
        m_chase.distance = 0.0F;
        m_chase.hasLineOfSight = false;
        m_chase.inCenterFOV = false;
        m_chase.timeSinceSeenLOS = 0.0F;
        m_chase.timeSinceCenterFOV = 0.0F;
        m_chase.timeInChase = 0.0F;
        return;
    }

    // Calculate distance and LOS
    m_chase.distance = DistanceXZ(killerTransformIt->second.position, survivorTransformIt->second.position);

    // Skip expensive LOS raycast when far outside any relevant range (chase end = 18m, buffer = 2m).
    constexpr float kLosMaxRange = 20.0F;
    if (m_chase.distance > kLosMaxRange)
    {
        m_chase.hasLineOfSight = false;
    }
    else
    {
        m_chase.hasLineOfSight = m_physics.HasLineOfSight(killerTransformIt->second.position, survivorTransformIt->second.position);
    }

    // Check if survivor is in killer's center FOV (±35°)
    m_chase.inCenterFOV = IsSurvivorInKillerCenterFOV(
        killerTransformIt->second.position,
        killerTransformIt->second.forward,
        survivorTransformIt->second.position
    ); // ±35° DBD-like center FOV

    // Track survivor running state from actor component
    bool survivorIsRunning = false;
    if (survivorActorIt != m_world.Actors().end())
    {
        survivorIsRunning = survivorActorIt->second.sprinting;
    }

    if (m_forcedChase.has_value())
    {
        m_chase.isChasing = *m_forcedChase;
    }
    else
    {
        // DBD-like chase rules:
        // - Starts only if: survivor sprinting + distance <= 12m + LOS + in center FOV (±35°)
        // - Ends if: distance >= 18m OR lost LOS > 8s OR lost center FOV > 8s
        // - Chase can last indefinitely if LOS/center-FOV keep being reacquired

        if (!m_chase.isChasing)
        {
            // Not in chase - check if we should start
            const bool canStartChase =
                survivorIsRunning &&
                m_chase.distance <= m_chase.startDistance && // <= 12m
                m_chase.hasLineOfSight &&
                m_chase.inCenterFOV;

            if (canStartChase)
            {
                m_chase.isChasing = true;
                m_chase.timeSinceSeenLOS = 0.0F;
                m_chase.timeSinceCenterFOV = 0.0F;
                m_chase.timeInChase = 0.0F;
            }
        }
        else
        {
            // Already in chase - update timers and check if we should end

            // Update time-in-chase counter
            m_chase.timeInChase += fixedDt;

            // Update timers based on current conditions
            if (m_chase.hasLineOfSight)
            {
                m_chase.timeSinceSeenLOS = 0.0F;
            }
            else
            {
                m_chase.timeSinceSeenLOS += fixedDt;
            }

            if (m_chase.inCenterFOV)
            {
                m_chase.timeSinceCenterFOV = 0.0F;
            }
            else
            {
                m_chase.timeSinceCenterFOV += fixedDt;
            }

            assert(m_chase.timeSinceSeenLOS >= 0.0F && "timeSinceSeenLOS should never be negative");
            assert(m_chase.timeSinceCenterFOV >= 0.0F && "timeSinceCenterFOV should never be negative");
            assert(m_chase.timeInChase >= 0.0F && "timeInChase should never be negative");

            const bool tooFar = m_chase.distance >= m_chase.endDistance;
            const bool lostLOSLong = m_chase.timeSinceSeenLOS > m_chase.lostSightTimeout;
            const bool lostCenterFOVLong = m_chase.timeSinceCenterFOV > m_chase.lostCenterFOVTimeout;

            if (tooFar || lostLOSLong || lostCenterFOVLong)
            {
                m_chase.isChasing = false;
                m_chase.timeSinceSeenLOS = 0.0F;
                m_chase.timeSinceCenterFOV = 0.0F;
                m_chase.timeInChase = 0.0F;
            }
        }
    }

    // Handle chase FX (aura)
    if (m_chase.isChasing)
    {
        if (killerTransformIt != m_world.Transforms().end())
        {
            const engine::fx::FxNetMode netMode = m_networkAuthorityMode ? engine::fx::FxNetMode::ServerBroadcast
                                                                          : engine::fx::FxNetMode::Local;
            if (m_chaseAuraFxId == 0)
            {
                m_chaseAuraFxId = SpawnGameplayFx(
                    "chase_aura",
                    killerTransformIt->second.position + glm::vec3{0.0F, 0.25F, 0.0F},
                    killerTransformIt->second.forward,
                    netMode
                );
            }
            else
            {
                m_fxSystem.SetInstanceTransform(
                    m_chaseAuraFxId,
                    killerTransformIt->second.position + glm::vec3{0.0F, 0.25F, 0.0F},
                    killerTransformIt->second.forward
                );
            }
        }
    }
    else if (m_chaseAuraFxId != 0)
    {
        m_fxSystem.Stop(m_chaseAuraFxId);
        m_chaseAuraFxId = 0;
    }

    if (m_chase.isChasing != wasChasing)
    {
        AddRuntimeMessage(m_chase.isChasing ? "Chase started" : "Chase ended", 1.0F);

        if (!m_chase.isChasing)
        {
            // Check for Sprint Burst: activates when chase ends
            const auto& activePerks = m_perkSystem.GetActivePerks(engine::scene::Role::Survivor);
            for (const auto& state : activePerks)
            {
                const auto* perk = m_perkSystem.GetPerk(state.perkId);
                if (perk && (perk->type == game::gameplay::perks::PerkType::Triggered) &&
                    (perk->id == "sprint_burst"))
                {
                    m_perkSystem.ActivatePerk(state.perkId, engine::scene::Role::Survivor);
                }
            }
        }
    }
}

void GameplaySystems::UpdateCamera(float deltaSeconds)
{
    const engine::scene::Entity controlled = ControlledEntity();
    const auto transformIt = m_world.Transforms().find(controlled);
    const auto actorIt = m_world.Actors().find(controlled);

    if (controlled == 0 || transformIt == m_world.Transforms().end() || actorIt == m_world.Actors().end())
    {
        return;
    }

    const engine::scene::Transform& transform = transformIt->second;
    const engine::scene::ActorComponent& actor = actorIt->second;
    const CameraMode mode = ResolveCameraMode();

    const glm::vec3 up{0.0F, 1.0F, 0.0F};
    glm::vec3 desiredPosition{0.0F};
    glm::vec3 desiredTarget{0.0F};

    if (mode == CameraMode::FirstPerson)
    {
        const float eyeScale = actor.crawling ? 0.52F : (actor.crouching ? 0.78F : 1.0F);
        const float eyeOffset = actor.eyeHeight * eyeScale - actor.capsuleHeight * 0.5F;
        desiredPosition = transform.position + glm::vec3{0.0F, eyeOffset, 0.0F};
        desiredTarget = desiredPosition + transform.forward * 8.0F;
    }
    else
    {
        const float eyeScale = actor.crawling ? 0.52F : (actor.crouching ? 0.78F : 1.0F);
        const float eyeOffset = actor.eyeHeight * eyeScale - actor.capsuleHeight * 0.45F;
        const glm::vec3 pivot = transform.position + glm::vec3{0.0F, eyeOffset, 0.0F};
        const bool flashlightAimCamera =
            (m_controlledRole == ControlledRole::Survivor) &&
            (m_survivorLoadout.itemId == "flashlight") &&
            m_survivorItemState.active &&
            m_survivorItemState.charges > 0.0F;

        const float yaw = transform.rotationEuler.y;
        const float pitch = glm::clamp(transform.rotationEuler.x * 0.65F, -0.8F, 0.8F);
        const glm::vec3 viewForward = ForwardFromYawPitch(yaw, pitch);
        glm::vec3 right = glm::cross(viewForward, up);
        if (glm::length(right) < 1.0e-5F)
        {
            right = glm::vec3{1.0F, 0.0F, 0.0F};
        }
        right = glm::normalize(right);

        const float backDistance = flashlightAimCamera ? 2.2F : 4.2F;
        const float shoulderOffset = flashlightAimCamera ? 0.22F : 0.75F;
        const float verticalOffset = flashlightAimCamera ? 0.25F : 0.55F;
        glm::vec3 desiredCamera = pivot - viewForward * backDistance + right * shoulderOffset + glm::vec3{0.0F, verticalOffset, 0.0F};

        const std::optional<engine::physics::RaycastHit> hit = m_physics.RaycastNearest(pivot, desiredCamera);
        if (hit.has_value())
        {
            const glm::vec3 dir = glm::normalize(desiredCamera - pivot);
            const float maxDistance = glm::length(desiredCamera - pivot);
            const float safeDistance = std::max(0.6F, hit->t * maxDistance - 0.2F);
            desiredCamera = pivot + dir * safeDistance;
        }

        desiredPosition = desiredCamera;
        desiredTarget = pivot + viewForward * (flashlightAimCamera ? 8.0F : 2.0F);
    }

    const glm::vec3 shakeOffset = m_fxSystem.CameraShakeOffset();
    desiredPosition += shakeOffset;
    desiredTarget += shakeOffset * 0.6F;

    if (!m_cameraInitialized)
    {
        m_cameraPosition = desiredPosition;
        m_cameraTarget = desiredTarget;
        m_cameraInitialized = true;
    }
    else if (mode == CameraMode::FirstPerson)
    {
        // In first-person keep camera fully locked to actor look to avoid weapon/camera desync.
        m_cameraPosition = desiredPosition;
        m_cameraTarget = desiredTarget;
    }
    else
    {
        const float smooth = 1.0F - std::exp(-deltaSeconds * 14.0F);
        m_cameraPosition = glm::mix(m_cameraPosition, desiredPosition, smooth);
        m_cameraTarget = glm::mix(m_cameraTarget, desiredTarget, smooth);
    }

    const glm::vec3 forward = m_cameraTarget - m_cameraPosition;
    m_cameraForward = glm::length(forward) > 1.0e-5F ? glm::normalize(forward) : glm::vec3{0.0F, 0.0F, -1.0F};
}

GameplaySystems::CameraMode GameplaySystems::ResolveCameraMode() const
{
    if (m_cameraOverride == CameraOverride::SurvivorThirdPerson)
    {
        return CameraMode::ThirdPerson;
    }
    if (m_cameraOverride == CameraOverride::KillerFirstPerson)
    {
        return CameraMode::FirstPerson;
    }

    return m_controlledRole == ControlledRole::Survivor ? CameraMode::ThirdPerson : CameraMode::FirstPerson;
}

engine::scene::Entity GameplaySystems::ControlledEntity() const
{
    return m_controlledRole == ControlledRole::Survivor ? m_survivor : m_killer;
}

engine::scene::Role GameplaySystems::ControlledSceneRole() const
{
    return m_controlledRole == ControlledRole::Survivor ? engine::scene::Role::Survivor : engine::scene::Role::Killer;
}

GameplaySystems::InteractionCandidate GameplaySystems::ResolveInteractionCandidateFromView(engine::scene::Entity actorEntity) const
{
    InteractionCandidate best;

    const auto actorTransformIt = m_world.Transforms().find(actorEntity);
    const auto actorIt = m_world.Actors().find(actorEntity);
    if (actorTransformIt == m_world.Transforms().end() || actorIt == m_world.Actors().end())
    {
        return best;
    }

    const engine::scene::Transform& actorTransform = actorTransformIt->second;
    const engine::scene::ActorComponent& actor = actorIt->second;

    const glm::vec3 eyePosition = actorTransform.position + glm::vec3{0.0F, actor.eyeHeight - actor.capsuleHeight * 0.5F, 0.0F};
    const bool useCameraRay = actorEntity == ControlledEntity() && m_cameraInitialized;
    const glm::vec3 castStart = useCameraRay ? m_cameraPosition : eyePosition;
    glm::vec3 castDirection = useCameraRay ? m_cameraForward : actorTransform.forward;
    if (glm::length(castDirection) < 1.0e-5F)
    {
        castDirection = actorTransform.forward;
    }
    castDirection = glm::normalize(castDirection);

    constexpr float kInteractionCastRange = 4.0F;
    constexpr float kInteractionCastRadius = 0.85F;
    const glm::vec3 castEnd = castStart + castDirection * kInteractionCastRange;

    m_physics.SphereCastTriggers(m_sphereCastScratch, castStart, castEnd, kInteractionCastRadius);
    std::unordered_set<engine::scene::Entity> visited;

    auto considerCandidate = [&](const InteractionCandidate& candidate) {
        if (candidate.type == InteractionType::None)
        {
            return;
        }

        if (candidate.priority > best.priority ||
            (candidate.priority == best.priority && candidate.castT < best.castT))
        {
            best = candidate;
        }
    };

    auto processTriggerEntity = [&](engine::scene::Entity entity, float castT) {
        if (m_world.Windows().contains(entity))
        {
            considerCandidate(BuildWindowVaultCandidate(actorEntity, entity, castT));
            return;
        }

        if (m_world.Hooks().contains(entity))
        {
            considerCandidate(BuildHookSurvivorCandidate(actorEntity, entity, castT));
            return;
        }

        if (m_world.Generators().contains(entity))
        {
            considerCandidate(BuildGeneratorRepairCandidate(actorEntity, entity, castT));
            return;
        }

        const auto palletIt = m_world.Pallets().find(entity);
        if (palletIt == m_world.Pallets().end())
        {
            return;
        }

        if (palletIt->second.state == engine::scene::PalletState::Standing)
        {
            considerCandidate(BuildStandingPalletCandidate(actorEntity, entity, castT));
        }
        else if (palletIt->second.state == engine::scene::PalletState::Dropped)
        {
            considerCandidate(BuildDroppedPalletCandidate(actorEntity, entity, castT));
        }
    };

    for (const engine::physics::TriggerCastHit& hit : m_sphereCastScratch)
    {
        if (!visited.insert(hit.entity).second)
        {
            continue;
        }

        processTriggerEntity(hit.entity, hit.t);
    }

    // Fallback: if camera cast misses while sprinting, still resolve entities from local trigger volumes.
    m_physics.QueryCapsuleTriggers(m_triggerHitBuf,
        actorTransform.position,
        actor.capsuleRadius,
        actor.capsuleHeight,
        engine::physics::TriggerKind::Vault
    );
    for (const engine::physics::TriggerHit& hit : m_triggerHitBuf)
    {
        if (!visited.insert(hit.entity).second)
        {
            continue;
        }
        processTriggerEntity(hit.entity, 0.12F);
    }

    m_physics.QueryCapsuleTriggers(m_triggerHitBuf,
        actorTransform.position,
        actor.capsuleRadius,
        actor.capsuleHeight,
        engine::physics::TriggerKind::Interaction
    );
    for (const engine::physics::TriggerHit& hit : m_triggerHitBuf)
    {
        if (!visited.insert(hit.entity).second)
        {
            continue;
        }
        processTriggerEntity(hit.entity, 0.18F);
    }

    considerCandidate(BuildDropSurvivorCandidate(actorEntity));
    considerCandidate(BuildPickupSurvivorCandidate(actorEntity, castStart, castDirection));
    considerCandidate(BuildSelfHealCandidate(actorEntity));

    // Locker interaction for hatchet replenishment (killer only)
    if (actor.role == engine::scene::Role::Killer &&
        m_killerLoadout.powerId == "hatchet_throw" &&
        m_killerPowerState.hatchetCount < m_killerPowerState.hatchetMaxCount)
    {
        for (const auto& [entity, locker] : m_world.Lockers())
        {
            const auto lockerTransformIt = m_world.Transforms().find(entity);
            if (lockerTransformIt == m_world.Transforms().end())
            {
                continue;
            }
            const float distance = DistanceXZ(actorTransform.position, lockerTransformIt->second.position);
            if (distance < 2.0F)
            {
                InteractionCandidate lockerCandidate;
                lockerCandidate.type = InteractionType::ReplenishHatchets;
                lockerCandidate.entity = entity;
                lockerCandidate.priority = 5; // Lower than most interactions
                lockerCandidate.castT = distance / kInteractionCastRange;
                lockerCandidate.prompt = "Hold E to replenish hatchets";
                lockerCandidate.typeName = "ReplenishHatchets";
                lockerCandidate.targetName = "Locker";
                considerCandidate(lockerCandidate);
                break; // Only consider the nearest locker
            }
        }
    }

    return best;
}

GameplaySystems::InteractionCandidate GameplaySystems::BuildWindowVaultCandidate(
    engine::scene::Entity actorEntity,
    engine::scene::Entity windowEntity,
    float castT
) const
{
    InteractionCandidate candidate;

    const auto actorTransformIt = m_world.Transforms().find(actorEntity);
    const auto actorIt = m_world.Actors().find(actorEntity);
    const auto windowIt = m_world.Windows().find(windowEntity);
    const auto windowTransformIt = m_world.Transforms().find(windowEntity);

    if (actorTransformIt == m_world.Transforms().end() || actorIt == m_world.Actors().end() ||
        windowIt == m_world.Windows().end() || windowTransformIt == m_world.Transforms().end())
    {
        return candidate;
    }

    const engine::scene::Transform& actorTransform = actorTransformIt->second;
    const engine::scene::ActorComponent& actor = actorIt->second;
    const engine::scene::WindowComponent& window = windowIt->second;

    if (actor.vaulting || actor.vaultCooldown > 0.0F)
    {
        return candidate;
    }
    if (actor.role == engine::scene::Role::Survivor &&
        (m_survivorState == SurvivorHealthState::Downed ||
         m_survivorState == SurvivorHealthState::Carried ||
         m_survivorState == SurvivorHealthState::Hooked ||
         m_survivorState == SurvivorHealthState::Dead))
    {
        return candidate;
    }
    if (actor.role == engine::scene::Role::Killer && !window.killerCanVault)
    {
        return candidate;
    }

    m_physics.QueryCapsuleTriggers(m_triggerHitBuf,
        actorTransform.position,
        actor.capsuleRadius,
        actor.capsuleHeight,
        engine::physics::TriggerKind::Vault
    );

    bool inTrigger = false;
    for (const engine::physics::TriggerHit& hit : m_triggerHitBuf)
    {
        if (hit.entity == windowEntity)
        {
            inTrigger = true;
            break;
        }
    }
    if (!inTrigger)
    {
        return candidate;
    }

    glm::vec3 windowNormal{windowTransformIt->second.forward.x, 0.0F, windowTransformIt->second.forward.z};
    if (glm::length(windowNormal) < 1.0e-5F)
    {
        windowNormal = glm::vec3{window.normal.x, 0.0F, window.normal.z};
    }
    if (glm::length(windowNormal) < 1.0e-5F)
    {
        windowNormal = glm::vec3{0.0F, 0.0F, 1.0F};
    }
    windowNormal = glm::normalize(windowNormal);
    const float side = glm::dot(actorTransform.position - windowTransformIt->second.position, windowNormal) >= 0.0F ? 1.0F : -1.0F;
    const glm::vec3 desiredForward = -windowNormal * side;

    const glm::vec3 actorForwardXZ = glm::normalize(glm::vec3{actorTransform.forward.x, 0.0F, actorTransform.forward.z});
    const glm::vec3 desiredForwardXZ = glm::normalize(glm::vec3{desiredForward.x, 0.0F, desiredForward.z});
    const float facingDot = glm::dot(actorForwardXZ, desiredForwardXZ);

    const float distanceToVaultPoint = DistanceXZ(actorTransform.position, windowTransformIt->second.position);
    if (distanceToVaultPoint > 3.0F)
    {
        return candidate;
    }

    candidate.type = InteractionType::WindowVault;
    candidate.entity = windowEntity;
    candidate.priority = 80;
    candidate.castT = castT;
    candidate.prompt = "Press E to Vault";
    if (facingDot < 0.45F)
    {
        candidate.prompt = "Press E to Vault (Face window)";
        candidate.priority = 60;
    }
    else if (distanceToVaultPoint > 2.3F)
    {
        candidate.prompt = "Press E to Vault (Move closer)";
        candidate.priority = 60;
    }
    candidate.typeName = "WindowVault";
    candidate.targetName = "Window";
    return candidate;
}

GameplaySystems::InteractionCandidate GameplaySystems::BuildStandingPalletCandidate(
    engine::scene::Entity actorEntity,
    engine::scene::Entity palletEntity,
    float castT
) const
{
    InteractionCandidate candidate;

    const auto actorTransformIt = m_world.Transforms().find(actorEntity);
    const auto actorIt = m_world.Actors().find(actorEntity);
    const auto palletIt = m_world.Pallets().find(palletEntity);
    const auto palletTransformIt = m_world.Transforms().find(palletEntity);

    if (actorTransformIt == m_world.Transforms().end() || actorIt == m_world.Actors().end() ||
        palletIt == m_world.Pallets().end() || palletTransformIt == m_world.Transforms().end())
    {
        return candidate;
    }

    if (actorIt->second.role != engine::scene::Role::Survivor || palletIt->second.state != engine::scene::PalletState::Standing)
    {
        return candidate;
    }
    if (m_survivorState != SurvivorHealthState::Healthy && m_survivorState != SurvivorHealthState::Injured)
    {
        return candidate;
    }

    m_physics.QueryCapsuleTriggers(m_triggerHitBuf,
        actorTransformIt->second.position,
        actorIt->second.capsuleRadius,
        actorIt->second.capsuleHeight,
        engine::physics::TriggerKind::Interaction
    );

    bool inTrigger = false;
    for (const engine::physics::TriggerHit& hit : m_triggerHitBuf)
    {
        if (hit.entity == palletEntity)
        {
            inTrigger = true;
            break;
        }
    }
    if (!inTrigger)
    {
        return candidate;
    }

    const glm::vec3 toPallet = palletTransformIt->second.position - actorTransformIt->second.position;
    const float distance = DistanceXZ(palletTransformIt->second.position, actorTransformIt->second.position);
    if (distance > 2.8F)
    {
        return candidate;
    }

    const glm::vec3 toPalletXZ = glm::normalize(glm::vec3{toPallet.x, 0.0F, toPallet.z});
    const glm::vec3 actorForwardXZ = glm::normalize(glm::vec3{actorTransformIt->second.forward.x, 0.0F, actorTransformIt->second.forward.z});
    const float facingDot = glm::dot(actorForwardXZ, toPalletXZ);

    candidate.type = InteractionType::DropPallet;
    candidate.entity = palletEntity;
    candidate.priority = 100;
    candidate.castT = castT;
    candidate.prompt = "Press E to Drop Pallet";
    if (facingDot < 0.1F)
    {
        candidate.prompt = "Press E to Drop Pallet (Face pallet)";
        candidate.priority = 70;
    }
    else if (distance > 2.2F)
    {
        candidate.prompt = "Press E to Drop Pallet (Move closer)";
        candidate.priority = 70;
    }
    candidate.typeName = "DropPallet";
    candidate.targetName = "Pallet";
    return candidate;
}

GameplaySystems::InteractionCandidate GameplaySystems::BuildDroppedPalletCandidate(
    engine::scene::Entity actorEntity,
    engine::scene::Entity palletEntity,
    float castT
) const
{
    InteractionCandidate candidate;

    const auto actorTransformIt = m_world.Transforms().find(actorEntity);
    const auto actorIt = m_world.Actors().find(actorEntity);
    const auto palletIt = m_world.Pallets().find(palletEntity);
    const auto palletTransformIt = m_world.Transforms().find(palletEntity);

    if (actorTransformIt == m_world.Transforms().end() || actorIt == m_world.Actors().end() ||
        palletIt == m_world.Pallets().end() || palletTransformIt == m_world.Transforms().end())
    {
        return candidate;
    }

    if (palletIt->second.state != engine::scene::PalletState::Dropped)
    {
        return candidate;
    }

    m_physics.QueryCapsuleTriggers(m_triggerHitBuf,
        actorTransformIt->second.position,
        actorIt->second.capsuleRadius,
        actorIt->second.capsuleHeight,
        engine::physics::TriggerKind::Interaction
    );

    bool inTrigger = false;
    for (const engine::physics::TriggerHit& hit : m_triggerHitBuf)
    {
        if (hit.entity == palletEntity)
        {
            inTrigger = true;
            break;
        }
    }
    if (!inTrigger)
    {
        return candidate;
    }

    const float distance = DistanceXZ(palletTransformIt->second.position, actorTransformIt->second.position);
    if (distance > 2.4F)
    {
        return candidate;
    }

    if (actorIt->second.role == engine::scene::Role::Killer)
    {
        if (palletIt->second.breakTimer > 0.0F)
        {
            return candidate;
        }

        candidate.type = InteractionType::BreakPallet;
        candidate.entity = palletEntity;
        candidate.priority = 70;
        candidate.castT = castT;
        candidate.prompt = "Press E to Break Pallet";
        if (distance > 2.0F)
        {
            candidate.prompt = "Press E to Break Pallet (Move closer)";
            candidate.priority = 55;
        }
        candidate.typeName = "BreakPallet";
        candidate.targetName = "Pallet";
        return candidate;
    }

    if (m_survivorState != SurvivorHealthState::Healthy && m_survivorState != SurvivorHealthState::Injured)
    {
        return candidate;
    }

    const glm::vec3 toPallet = palletTransformIt->second.position - actorTransformIt->second.position;
    const glm::vec3 toPalletXZ = glm::normalize(glm::vec3{toPallet.x, 0.0F, toPallet.z});
    const glm::vec3 actorForwardXZ = glm::normalize(glm::vec3{actorTransformIt->second.forward.x, 0.0F, actorTransformIt->second.forward.z});
    const float facingDot = glm::dot(actorForwardXZ, toPalletXZ);

    candidate.type = InteractionType::PalletVault;
    candidate.entity = palletEntity;
    candidate.priority = 85;
    candidate.castT = castT;
    candidate.prompt = "Press E to Vault Pallet";
    if (facingDot < 0.1F)
    {
        candidate.prompt = "Press E to Vault Pallet (Face pallet)";
        candidate.priority = 60;
    }
    candidate.typeName = "PalletVault";
    candidate.targetName = "DroppedPallet";
    return candidate;
}

GameplaySystems::InteractionCandidate GameplaySystems::BuildDropSurvivorCandidate(engine::scene::Entity actorEntity) const
{
    InteractionCandidate candidate;
    if (actorEntity != m_killer || m_survivorState != SurvivorHealthState::Carried)
    {
        return candidate;
    }

    candidate.type = InteractionType::DropSurvivor;
    candidate.entity = m_survivor;
    candidate.priority = 110;
    candidate.castT = 0.05F;
    candidate.prompt = "Press E to Drop Survivor";
    candidate.typeName = "DropSurvivor";
    candidate.targetName = "Survivor";
    return candidate;
}

GameplaySystems::InteractionCandidate GameplaySystems::BuildPickupSurvivorCandidate(
    engine::scene::Entity actorEntity,
    const glm::vec3& castStart,
    const glm::vec3& castDirection
) const
{
    InteractionCandidate candidate;

    if (actorEntity != m_killer || m_survivor == 0 ||
        (m_survivorState != SurvivorHealthState::Downed && m_survivorState != SurvivorHealthState::Trapped))
    {
        return candidate;
    }

    const auto survivorTransformIt = m_world.Transforms().find(m_survivor);
    if (survivorTransformIt == m_world.Transforms().end())
    {
        return candidate;
    }

    const glm::vec3 survivorPoint = survivorTransformIt->second.position + glm::vec3{0.0F, 0.45F, 0.0F};
    const glm::vec3 toSurvivor = survivorPoint - castStart;
    const float distance = glm::length(toSurvivor);
    if (distance > 2.4F || distance < 1.0e-5F)
    {
        return candidate;
    }

    const glm::vec3 directionToSurvivor = toSurvivor / distance;
    if (glm::dot(glm::normalize(castDirection), directionToSurvivor) < 0.55F)
    {
        return candidate;
    }

    const std::optional<engine::physics::RaycastHit> obstacleHit = m_physics.RaycastNearest(castStart, survivorPoint);
    if (obstacleHit.has_value())
    {
        return candidate;
    }

    candidate.type = InteractionType::PickupSurvivor;
    candidate.entity = m_survivor;
    candidate.priority = 95;
    candidate.castT = glm::clamp(distance / 3.0F, 0.0F, 1.0F);
    candidate.prompt = m_survivorState == SurvivorHealthState::Trapped
                           ? "Press E to Pick Up Trapped Survivor"
                           : "Press E to Pick Up Survivor";
    candidate.typeName = "PickupSurvivor";
    candidate.targetName = "Survivor";
    return candidate;
}

GameplaySystems::InteractionCandidate GameplaySystems::BuildHookSurvivorCandidate(
    engine::scene::Entity actorEntity,
    engine::scene::Entity hookEntity,
    float castT
) const
{
    InteractionCandidate candidate;

    if (actorEntity != m_killer || m_survivorState != SurvivorHealthState::Carried)
    {
        return candidate;
    }

    const auto hookIt = m_world.Hooks().find(hookEntity);
    const auto hookTransformIt = m_world.Transforms().find(hookEntity);
    const auto killerTransformIt = m_world.Transforms().find(actorEntity);
    if (hookIt == m_world.Hooks().end() || hookTransformIt == m_world.Transforms().end() ||
        killerTransformIt == m_world.Transforms().end())
    {
        return candidate;
    }

    if (hookIt->second.occupied)
    {
        return candidate;
    }

    const float distance = DistanceXZ(killerTransformIt->second.position, hookTransformIt->second.position);
    if (distance > 2.2F)
    {
        return candidate;
    }

    const glm::vec3 toHook = hookTransformIt->second.position - killerTransformIt->second.position;
    const glm::vec3 toHookXZ = glm::normalize(glm::vec3{toHook.x, 0.0F, toHook.z});
    const glm::vec3 killerForwardXZ = glm::normalize(glm::vec3{killerTransformIt->second.forward.x, 0.0F, killerTransformIt->second.forward.z});
    if (glm::dot(killerForwardXZ, toHookXZ) < 0.2F)
    {
        return candidate;
    }

    candidate.type = InteractionType::HookSurvivor;
    candidate.entity = hookEntity;
    candidate.priority = 120;
    candidate.castT = castT;
    candidate.prompt = "Press E to Hook Survivor";
    candidate.typeName = "HookSurvivor";
    candidate.targetName = "Hook";
    return candidate;
}

GameplaySystems::InteractionCandidate GameplaySystems::BuildGeneratorRepairCandidate(
    engine::scene::Entity actorEntity,
    engine::scene::Entity generatorEntity,
    float castT
) const
{
    InteractionCandidate candidate;

    const auto actorIt = m_world.Actors().find(actorEntity);
    const auto actorTransformIt = m_world.Transforms().find(actorEntity);
    const auto generatorIt = m_world.Generators().find(generatorEntity);
    const auto generatorTransformIt = m_world.Transforms().find(generatorEntity);
    if (actorIt == m_world.Actors().end() || actorTransformIt == m_world.Transforms().end() ||
        generatorIt == m_world.Generators().end() || generatorTransformIt == m_world.Transforms().end())
    {
        return candidate;
    }

    if (actorIt->second.role != engine::scene::Role::Survivor)
    {
        return candidate;
    }
    if (m_survivorState != SurvivorHealthState::Healthy && m_survivorState != SurvivorHealthState::Injured)
    {
        return candidate;
    }
    if (generatorIt->second.completed)
    {
        return candidate;
    }

    m_physics.QueryCapsuleTriggers(m_triggerHitBuf,
        actorTransformIt->second.position,
        actorIt->second.capsuleRadius,
        actorIt->second.capsuleHeight,
        engine::physics::TriggerKind::Interaction
    );

    bool inTrigger = false;
    for (const engine::physics::TriggerHit& hit : m_triggerHitBuf)
    {
        if (hit.entity == generatorEntity)
        {
            inTrigger = true;
            break;
        }
    }
    if (!inTrigger)
    {
        return candidate;
    }

    const float distance = DistanceXZ(actorTransformIt->second.position, generatorTransformIt->second.position);
    if (distance > 2.5F)
    {
        return candidate;
    }

    const glm::vec3 toGenerator = generatorTransformIt->second.position - actorTransformIt->second.position;
    const glm::vec3 toGeneratorXZ = glm::normalize(glm::vec3{toGenerator.x, 0.0F, toGenerator.z});
    const glm::vec3 actorForwardXZ = glm::normalize(glm::vec3{actorTransformIt->second.forward.x, 0.0F, actorTransformIt->second.forward.z});
    if (glm::dot(actorForwardXZ, toGeneratorXZ) < -0.2F)
    {
        return candidate;
    }

    candidate.type = InteractionType::RepairGenerator;
    candidate.entity = generatorEntity;
    candidate.priority = 55;
    candidate.castT = castT;
    if (generatorEntity == m_activeRepairGenerator && m_skillCheckActive)
    {
        candidate.prompt = "Skill Check active: press SPACE";
    }
    else if (generatorEntity == m_activeRepairGenerator)
    {
        candidate.prompt = "Hold E to Repair Generator";
    }
    else
    {
        candidate.prompt = "Press E to Repair Generator";
    }
    candidate.typeName = "RepairGenerator";
    candidate.targetName = "Generator";
    return candidate;
}

GameplaySystems::InteractionCandidate GameplaySystems::BuildSelfHealCandidate(engine::scene::Entity actorEntity) const
{
    InteractionCandidate candidate;
    if (actorEntity != m_survivor || m_survivorState != SurvivorHealthState::Injured)
    {
        return candidate;
    }

    const auto actorIt = m_world.Actors().find(actorEntity);
    if (actorIt == m_world.Actors().end() || actorIt->second.carried || actorIt->second.vaulting)
    {
        return candidate;
    }

    candidate.type = InteractionType::SelfHeal;
    candidate.entity = actorEntity;
    candidate.priority = 18;
    candidate.castT = 0.95F;
    if (m_selfHealActive && m_skillCheckActive)
    {
        candidate.prompt = "Self-heal: skill check (SPACE)";
    }
    else if (m_selfHealActive)
    {
        candidate.prompt = "Hold E to Self-heal";
    }
    else
    {
        candidate.prompt = "Press E to Self-heal";
    }
    candidate.typeName = "SelfHeal";
    candidate.targetName = "Self";
    return candidate;
}

bool GameplaySystems::IsActorInputLocked(const engine::scene::ActorComponent& actor) const
{
    return actor.vaulting || actor.stunTimer > 0.0F || actor.carried;
}

GameplaySystems::VaultType GameplaySystems::DetermineWindowVaultType(
    const engine::scene::ActorComponent& actor,
    const engine::scene::Transform& actorTransform,
    const engine::scene::Transform& windowTransform,
    const engine::scene::WindowComponent& window
) const
{
    glm::vec3 windowNormal{windowTransform.forward.x, 0.0F, windowTransform.forward.z};
    if (glm::length(windowNormal) < 1.0e-5F)
    {
        windowNormal = glm::vec3{window.normal.x, 0.0F, window.normal.z};
    }
    if (glm::length(windowNormal) < 1.0e-5F)
    {
        windowNormal = glm::vec3{0.0F, 0.0F, 1.0F};
    }
    windowNormal = glm::normalize(windowNormal);
    const float side = glm::dot(actorTransform.position - windowTransform.position, windowNormal) >= 0.0F ? 1.0F : -1.0F;
    const glm::vec3 desiredForward = -windowNormal * side;

    const glm::vec3 actorForwardXZ = glm::normalize(glm::vec3{actorTransform.forward.x, 0.0F, actorTransform.forward.z});
    const glm::vec3 desiredForwardXZ = glm::normalize(glm::vec3{desiredForward.x, 0.0F, desiredForward.z});
    const float facing = glm::dot(actorForwardXZ, desiredForwardXZ);
    const float horizontalSpeed = glm::length(glm::vec2{actor.velocity.x, actor.velocity.z});
    const float distanceToWindow = DistanceXZ(actorTransform.position, windowTransform.position);

    const bool fastBySprint = actor.sprinting;
    const bool fastBySpeed = horizontalSpeed >= actor.sprintSpeed * m_tuning.fastVaultSpeedMultiplier;
    const bool fastByFacing = facing >= m_tuning.fastVaultDotThreshold;
    const bool fastByDistance = distanceToWindow >= 0.45F && distanceToWindow <= 1.9F;
    const bool fastByRunup = actor.forwardRunupDistance >= m_tuning.fastVaultMinRunup;
    if (fastBySprint && fastBySpeed && fastByFacing && fastByDistance && fastByRunup)
    {
        return VaultType::Fast;
    }

    const bool mediumBySpeed = horizontalSpeed >= actor.walkSpeed * 0.95F;
    const bool mediumBySprint = actor.sprinting;
    const bool mediumByFacing = facing >= 0.55F;
    if ((mediumBySpeed || mediumBySprint) && mediumByFacing)
    {
        return VaultType::Medium;
    }

    return VaultType::Slow;
}

GameplaySystems::VaultType GameplaySystems::DeterminePalletVaultType(const engine::scene::ActorComponent& actor) const
{
    const float horizontalSpeed = glm::length(glm::vec2{actor.velocity.x, actor.velocity.z});
    if (actor.sprinting && horizontalSpeed >= actor.sprintSpeed * 0.84F)
    {
        return VaultType::Fast;
    }
    return VaultType::Slow;
}

const char* GameplaySystems::VaultTypeToText(VaultType type)
{
    switch (type)
    {
        case VaultType::Slow: return "Slow";
        case VaultType::Medium: return "Medium";
        case VaultType::Fast: return "Fast";
        default: return "Slow";
    }
}

void GameplaySystems::BeginWindowVault(engine::scene::Entity actorEntity, engine::scene::Entity windowEntity)
{
    auto actorIt = m_world.Actors().find(actorEntity);
    auto actorTransformIt = m_world.Transforms().find(actorEntity);
    auto windowIt = m_world.Windows().find(windowEntity);
    auto windowTransformIt = m_world.Transforms().find(windowEntity);

    if (actorIt == m_world.Actors().end() || actorTransformIt == m_world.Transforms().end() ||
        windowIt == m_world.Windows().end() || windowTransformIt == m_world.Transforms().end())
    {
        return;
    }

    engine::scene::ActorComponent& actor = actorIt->second;
    engine::scene::Transform& actorTransform = actorTransformIt->second;
    const engine::scene::WindowComponent& window = windowIt->second;

    if (actor.vaulting || actor.vaultCooldown > 0.0F)
    {
        return;
    }
    if (actor.role == engine::scene::Role::Survivor &&
        (m_survivorState != SurvivorHealthState::Healthy && m_survivorState != SurvivorHealthState::Injured))
    {
        return;
    }

    glm::vec3 normal{windowTransformIt->second.forward.x, 0.0F, windowTransformIt->second.forward.z};
    if (glm::length(normal) < 1.0e-4F)
    {
        normal = glm::vec3{window.normal.x, 0.0F, window.normal.z};
    }
    if (glm::length(normal) < 1.0e-4F)
    {
        normal = glm::vec3{0.0F, 0.0F, 1.0F};
    }
    normal = glm::normalize(normal);
    const float sideSign = glm::dot(actorTransform.position - windowTransformIt->second.position, normal) >= 0.0F ? 1.0F : -1.0F;
    const glm::vec3 vaultDirection = -normal * sideSign;

    const glm::vec3 actorForwardXZ = glm::normalize(glm::vec3{actorTransform.forward.x, 0.0F, actorTransform.forward.z});
    const glm::vec3 vaultForwardXZ = glm::normalize(glm::vec3{vaultDirection.x, 0.0F, vaultDirection.z});
    if (glm::dot(actorForwardXZ, vaultForwardXZ) < -0.2F)
    {
        AddRuntimeMessage("Vault blocked: face window", 1.2F);
        return;
    }

    const float windowThicknessAlongNormal =
        std::abs(normal.x) * window.halfExtents.x +
        std::abs(normal.y) * window.halfExtents.y +
        std::abs(normal.z) * window.halfExtents.z;

    VaultType vaultType = VaultType::Slow;
    if (actor.role == engine::scene::Role::Survivor)
    {
        vaultType = DetermineWindowVaultType(actor, actorTransform, windowTransformIt->second, window);
    }

    float duration = m_tuning.vaultSlowTime;
    if (vaultType == VaultType::Medium)
    {
        duration = m_tuning.vaultMediumTime;
    }
    else if (vaultType == VaultType::Fast)
    {
        duration = m_tuning.vaultFastTime;
    }

    actor.vaulting = true;
    actor.vaultTimer = 0.0F;
    actor.vaultStart = actorTransform.position;
    actor.vaultEnd = windowTransformIt->second.position + vaultDirection * (windowThicknessAlongNormal + actor.capsuleRadius + 0.8F);
    actor.vaultEnd.y = actorTransform.position.y;
    actor.vaultDuration = duration;
    actor.vaultArcHeight =
        vaultType == VaultType::Fast ? 0.38F :
        (vaultType == VaultType::Medium ? 0.48F : 0.55F);

    if (actor.role == engine::scene::Role::Killer)
    {
        vaultType = VaultType::Slow;
        float vaultTime = m_tuning.vaultSlowTime * window.killerVaultMultiplier;
        if (m_killerLoadout.powerId == "wraith_cloak" && m_killerPowerState.wraithCloaked)
        {
            vaultTime /= m_tuning.wraithCloakVaultSpeedMult;
        }
        actor.vaultDuration = vaultTime;
        actor.vaultArcHeight = 0.4F;
    }

    actor.velocity = glm::vec3{0.0F};
    actor.sprinting = false;
    actor.forwardRunupDistance = 0.0F;
    actor.lastVaultType = VaultTypeToText(vaultType);
    actor.collisionEnabled = false;

    const glm::vec3 fxPos = windowTransformIt->second.position + glm::vec3{0.0F, 0.8F, 0.0F};
    const engine::fx::FxNetMode netMode = m_networkAuthorityMode ? engine::fx::FxNetMode::ServerBroadcast
                                                                  : engine::fx::FxNetMode::Local;
    SpawnGameplayFx("dust_puff", fxPos, vaultDirection, netMode);
    if (vaultType == VaultType::Fast)
    {
        SpawnGameplayFx("hit_spark", fxPos, vaultDirection, netMode);
    }

    AddRuntimeMessage(std::string{"Vault: "} + actor.lastVaultType, 1.5F);
}

void GameplaySystems::BeginPalletVault(engine::scene::Entity actorEntity, engine::scene::Entity palletEntity)
{
    auto actorIt = m_world.Actors().find(actorEntity);
    auto actorTransformIt = m_world.Transforms().find(actorEntity);
    auto palletIt = m_world.Pallets().find(palletEntity);
    auto palletTransformIt = m_world.Transforms().find(palletEntity);

    if (actorIt == m_world.Actors().end() || actorTransformIt == m_world.Transforms().end() ||
        palletIt == m_world.Pallets().end() || palletTransformIt == m_world.Transforms().end())
    {
        return;
    }

    engine::scene::ActorComponent& actor = actorIt->second;
    engine::scene::Transform& actorTransform = actorTransformIt->second;
    const engine::scene::PalletComponent& pallet = palletIt->second;

    if (actor.role != engine::scene::Role::Survivor || pallet.state != engine::scene::PalletState::Dropped ||
        actor.vaulting || actor.vaultCooldown > 0.0F)
    {
        return;
    }
    if (m_survivorState != SurvivorHealthState::Healthy && m_survivorState != SurvivorHealthState::Injured)
    {
        return;
    }

    const glm::vec3 palletNormal = pallet.halfExtents.x < pallet.halfExtents.z ? glm::vec3{1.0F, 0.0F, 0.0F} : glm::vec3{0.0F, 0.0F, 1.0F};
    const float sideSign = glm::dot(actorTransform.position - palletTransformIt->second.position, palletNormal) >= 0.0F ? 1.0F : -1.0F;
    const glm::vec3 vaultDirection = -palletNormal * sideSign;
    const float thinExtent = std::abs(palletNormal.x) * pallet.halfExtents.x + std::abs(palletNormal.z) * pallet.halfExtents.z;
    const VaultType vaultType = DeterminePalletVaultType(actor);

    actor.vaulting = true;
    actor.vaultTimer = 0.0F;
    actor.vaultStart = actorTransform.position;
    actor.vaultEnd = palletTransformIt->second.position + vaultDirection * (thinExtent + actor.capsuleRadius + 0.75F);
    actor.vaultEnd.y = actorTransform.position.y;
    actor.vaultDuration = vaultType == VaultType::Fast ? 0.42F : 0.62F;
    actor.vaultArcHeight = vaultType == VaultType::Fast ? 0.4F : 0.52F;
    actor.velocity = glm::vec3{0.0F};
    actor.sprinting = false;
    actor.forwardRunupDistance = 0.0F;
    actor.lastVaultType = std::string{"Pallet-"} + VaultTypeToText(vaultType);
    actor.collisionEnabled = false;

    const engine::fx::FxNetMode netMode = m_networkAuthorityMode ? engine::fx::FxNetMode::ServerBroadcast
                                                                  : engine::fx::FxNetMode::Local;
    SpawnGameplayFx("dust_puff", palletTransformIt->second.position + glm::vec3{0.0F, 0.2F, 0.0F}, vaultDirection, netMode);

    AddRuntimeMessage("Vault started: " + actor.lastVaultType, 1.5F);
}

void GameplaySystems::TryStunKillerFromPallet(engine::scene::Entity palletEntity)
{
    if (m_killer == 0)
    {
        return;
    }

    const auto palletIt = m_world.Pallets().find(palletEntity);
    const auto palletTransformIt = m_world.Transforms().find(palletEntity);
    const auto killerIt = m_world.Actors().find(m_killer);
    const auto killerTransformIt = m_world.Transforms().find(m_killer);

    if (palletIt == m_world.Pallets().end() || palletTransformIt == m_world.Transforms().end() ||
        killerIt == m_world.Actors().end() || killerTransformIt == m_world.Transforms().end())
    {
        return;
    }

    const glm::vec3 delta = killerTransformIt->second.position - palletTransformIt->second.position;
    const glm::vec3 extent = palletIt->second.halfExtents + glm::vec3{0.55F, 0.7F, 0.55F};
    const bool inStunZone =
        std::abs(delta.x) <= extent.x &&
        std::abs(delta.y) <= extent.y &&
        std::abs(delta.z) <= extent.z;

    if (!inStunZone)
    {
        return;
    }

    // Reset bloodlust on pallet stun (DBD-like)
    if (m_bloodlust.tier > 0)
    {
        ResetBloodlust();
    }

    killerIt->second.stunTimer = std::max(killerIt->second.stunTimer, palletIt->second.stunDuration);
    killerIt->second.velocity = glm::vec3{0.0F};
    AddRuntimeMessage("Killer stunned by pallet", 1.8F);
}

void GameplaySystems::TryPickupDownedSurvivor()
{
    if (m_survivor == 0 || m_killer == 0 ||
        (m_survivorState != SurvivorHealthState::Downed && m_survivorState != SurvivorHealthState::Trapped))
    {
        return;
    }

    const auto survivorTransformIt = m_world.Transforms().find(m_survivor);
    const auto killerTransformIt = m_world.Transforms().find(m_killer);
    if (survivorTransformIt == m_world.Transforms().end() || killerTransformIt == m_world.Transforms().end())
    {
        return;
    }

    if (DistanceXZ(survivorTransformIt->second.position, killerTransformIt->second.position) > 2.5F)
    {
        return;
    }

    if (m_survivorState == SurvivorHealthState::Trapped)
    {
        ClearTrappedSurvivorBinding(m_survivor, true);
        ItemPowerLog("Carry pickup cleared trapped survivor binding");
    }

    AddRuntimeMessage("NET carry: pickup request validated", 1.2F);
    SetSurvivorState(SurvivorHealthState::Carried, "Pickup");
    AddRuntimeMessage("NET carry: state replicated Carried", 1.2F);
}

void GameplaySystems::TryHookCarriedSurvivor(engine::scene::Entity hookEntity)
{
    if (m_survivorState != SurvivorHealthState::Carried || m_killer == 0 || m_survivor == 0)
    {
        return;
    }

    auto hookIt = m_world.Hooks().end();
    if (hookEntity != 0)
    {
        hookIt = m_world.Hooks().find(hookEntity);
    }

    if (hookIt == m_world.Hooks().end())
    {
        float bestDistance = std::numeric_limits<float>::max();
        for (auto it = m_world.Hooks().begin(); it != m_world.Hooks().end(); ++it)
        {
            if (it->second.occupied)
            {
                continue;
            }

            const auto hookTransformIt = m_world.Transforms().find(it->first);
            const auto killerTransformIt = m_world.Transforms().find(m_killer);
            if (hookTransformIt == m_world.Transforms().end() || killerTransformIt == m_world.Transforms().end())
            {
                continue;
            }

            const float distance = DistanceXZ(hookTransformIt->second.position, killerTransformIt->second.position);
            if (distance < bestDistance)
            {
                bestDistance = distance;
                hookIt = it;
            }
        }
    }

    if (hookIt == m_world.Hooks().end())
    {
        return;
    }

    const auto hookTransformIt = m_world.Transforms().find(hookIt->first);
    if (hookTransformIt == m_world.Transforms().end())
    {
        return;
    }

    hookIt->second.occupied = true;
    m_activeHookEntity = hookIt->first;
    m_hookStage = 1;
    m_hookStageTimer = 0.0F;
    m_hookEscapeAttemptsUsed = 0;
    m_carryEscapeProgress = 0.0F;
    m_carryLastQteDirection = 0;
    m_skillCheckActive = false;
    m_skillCheckMode = SkillCheckMode::None;
    m_hookSkillCheckTimeToNext = 0.0F;

    auto survivorTransformIt = m_world.Transforms().find(m_survivor);
    if (survivorTransformIt != m_world.Transforms().end())
    {
        survivorTransformIt->second.position = hookTransformIt->second.position + glm::vec3{0.0F, 0.1F, 0.0F};
    }

    SetSurvivorState(SurvivorHealthState::Hooked, "Hook");
}

void GameplaySystems::UpdateCarriedSurvivor()
{
    if (m_survivorState != SurvivorHealthState::Carried || m_survivor == 0 || m_killer == 0)
    {
        return;
    }

    const auto survivorTransformIt = m_world.Transforms().find(m_survivor);
    const auto killerTransformIt = m_world.Transforms().find(m_killer);
    if (survivorTransformIt == m_world.Transforms().end() || killerTransformIt == m_world.Transforms().end())
    {
        return;
    }

    const glm::vec3 killerForward = glm::length(killerTransformIt->second.forward) > 1.0e-5F
                                        ? glm::normalize(killerTransformIt->second.forward)
                                        : glm::vec3{0.0F, 0.0F, -1.0F};

    survivorTransformIt->second.position = killerTransformIt->second.position + glm::vec3{0.0F, 0.95F, 0.0F} - killerForward * 0.35F;
    survivorTransformIt->second.forward = killerForward;
}

void GameplaySystems::UpdateCarryEscapeQte(bool survivorInputEnabled, float fixedDt)
{
    if (m_survivorState != SurvivorHealthState::Carried)
    {
        m_carryEscapeProgress = 0.0F;
        m_carryLastQteDirection = 0;
        return;
    }

    constexpr float kPassiveDecay = 0.22F;
    constexpr float kValidPressGain = 0.17F;
    constexpr float kInvalidPressPenalty = 0.08F;

    if (m_carryInputGraceTimer > 0.0F)
    {
        m_carryInputGraceTimer = std::max(0.0F, m_carryInputGraceTimer - fixedDt);
        return;
    }

    m_carryEscapeProgress = std::max(0.0F, m_carryEscapeProgress - kPassiveDecay * fixedDt);

    if (survivorInputEnabled)
    {
        bool leftPressed = false;
        bool rightPressed = false;
        ConsumeWigglePressedForSurvivor(leftPressed, rightPressed);

        int direction = 0;
        if (leftPressed)
        {
            direction = -1;
        }
        else if (rightPressed)
        {
            direction = 1;
        }

        if (direction != 0)
        {
            if (m_carryLastQteDirection == 0 || direction != m_carryLastQteDirection)
            {
                m_carryEscapeProgress = std::min(1.0F, m_carryEscapeProgress + kValidPressGain);
                m_carryLastQteDirection = direction;
            }
            else
            {
                m_carryEscapeProgress = std::max(0.0F, m_carryEscapeProgress - kInvalidPressPenalty);
            }
        }
    }

    if (m_carryEscapeProgress >= 1.0F)
    {
        auto survivorTransformIt = m_world.Transforms().find(m_survivor);
        auto killerTransformIt = m_world.Transforms().find(m_killer);
        if (survivorTransformIt != m_world.Transforms().end() && killerTransformIt != m_world.Transforms().end())
        {
            survivorTransformIt->second.position = killerTransformIt->second.position + glm::vec3{-0.9F, 0.0F, -0.9F};
        }

        m_carryEscapeProgress = 0.0F;
        m_carryLastQteDirection = 0;
        SetSurvivorState(SurvivorHealthState::Injured, "Carry escape");
        AddRuntimeMessage("Carry drop reason: wiggle success", 1.5F);
    }
}

void GameplaySystems::UpdateHookStages(float fixedDt, bool hookAttemptPressed, bool hookSkillCheckPressed)
{
    if (m_survivorState != SurvivorHealthState::Hooked)
    {
        m_hookStage = 0;
        m_hookStageTimer = 0.0F;
        m_hookEscapeAttemptsUsed = 0;
        if (m_skillCheckMode == SkillCheckMode::HookStruggle)
        {
            m_skillCheckMode = SkillCheckMode::None;
            m_skillCheckActive = false;
        }
        return;
    }

    if (m_hookStage <= 0)
    {
        m_hookStage = 1;
        m_hookStageTimer = 0.0F;
        m_hookEscapeAttemptsUsed = 0;
    }

    const float stageDuration = (m_hookStage == 1) ? m_hookStageOneDuration : m_hookStageTwoDuration;

    if (m_hookStage == 1 && hookAttemptPressed)
    {
        if (m_hookEscapeAttemptsUsed < m_hookEscapeAttemptsMax)
        {
            ++m_hookEscapeAttemptsUsed;
            std::uniform_real_distribution<float> chanceDist(0.0F, 1.0F);
            const bool success = chanceDist(m_rng) <= m_hookEscapeChance;
            if (success)
            {
                SetSurvivorState(SurvivorHealthState::Injured, "Self unhook success");
                AddRuntimeMessage("Self unhook succeeded!", 1.7F);
                return;
            }

            const int attemptsLeft = std::max(0, m_hookEscapeAttemptsMax - m_hookEscapeAttemptsUsed);
            AddRuntimeMessage("Self unhook failed. Attempts left: " + std::to_string(attemptsLeft), 1.7F);
            if (m_hookEscapeAttemptsUsed >= m_hookEscapeAttemptsMax)
            {
                m_hookStage = 2;
                m_hookStageTimer = 0.0F;
                m_hookSkillCheckTimeToNext = 1.2F;
                m_skillCheckMode = SkillCheckMode::HookStruggle;
                AddRuntimeMessage("Hook stage advanced to Stage 2 (attempt limit reached)", 1.9F);
            }
        }
    }

    if (m_hookStage == 2)
    {
        m_skillCheckMode = SkillCheckMode::HookStruggle;
        if (m_skillCheckActive && m_skillCheckMode == SkillCheckMode::HookStruggle)
        {
            m_skillCheckNeedle += m_skillCheckNeedleSpeed * fixedDt;
            if (hookSkillCheckPressed)
            {
                constexpr float kHitMargin = 0.06F;
                const float expandedStart = m_skillCheckSuccessStart - kHitMargin;
                const float expandedEnd = m_skillCheckSuccessEnd + kHitMargin;
                
                const bool success = m_skillCheckNeedle >= expandedStart && m_skillCheckNeedle <= expandedEnd;
                CompleteSkillCheck(success, false);
            }
            else if (m_skillCheckNeedle >= 1.0F)
            {
                CompleteSkillCheck(false, true);
            }
        }
        else
        {
            m_hookSkillCheckTimeToNext -= fixedDt;
            if (m_hookSkillCheckTimeToNext <= 0.0F)
            {
                std::uniform_real_distribution<float> zoneStartDist(0.16F, 0.80F);
                std::uniform_real_distribution<float> zoneSizeDist(0.07F, 0.12F);
                const float zoneStart = zoneStartDist(m_rng);
                const float zoneSize = zoneSizeDist(m_rng);
                m_skillCheckSuccessStart = zoneStart;
                m_skillCheckSuccessEnd = std::min(0.98F, zoneStart + zoneSize);
                m_skillCheckNeedle = 0.0F;
                m_skillCheckActive = true;
                m_skillCheckMode = SkillCheckMode::HookStruggle;
                AddRuntimeMessage("Hook struggle skill check: SPACE", 1.2F);
            }
        }
    }

    m_hookStageTimer += fixedDt;
    if (m_hookStageTimer < stageDuration)
    {
        return;
    }

    if (m_hookStage == 1)
    {
        m_hookStage = 2;
        m_hookStageTimer = 0.0F;
        m_hookSkillCheckTimeToNext = 1.0F;
        m_skillCheckMode = SkillCheckMode::HookStruggle;
        AddRuntimeMessage("Hook stage advanced to Stage 2", 1.8F);
        return;
    }

    m_hookStage = 3;
    AddRuntimeMessage("Hook stage advanced to Stage 3", 1.5F);
    SetSurvivorState(SurvivorHealthState::Dead, "Hook stage 3 timer");
}

void GameplaySystems::UpdateGeneratorRepair(bool holdingRepair, bool skillCheckPressed, float fixedDt)
{
    if (m_activeRepairGenerator == 0)
    {
        return;
    }

    const auto generatorIt = m_world.Generators().find(m_activeRepairGenerator);
    const auto generatorTransformIt = m_world.Transforms().find(m_activeRepairGenerator);
    const auto survivorIt = m_world.Actors().find(m_survivor);
    const auto survivorTransformIt = m_world.Transforms().find(m_survivor);
    if (generatorIt == m_world.Generators().end() || generatorTransformIt == m_world.Transforms().end() ||
        survivorIt == m_world.Actors().end() || survivorTransformIt == m_world.Transforms().end())
    {
        StopGeneratorRepair();
        return;
    }

    if (generatorIt->second.completed)
    {
        StopGeneratorRepair();
        return;
    }

    if (m_survivorState != SurvivorHealthState::Healthy && m_survivorState != SurvivorHealthState::Injured)
    {
        StopGeneratorRepair();
        return;
    }

    const float distance = DistanceXZ(survivorTransformIt->second.position, generatorTransformIt->second.position);
    if (distance > 2.6F || !holdingRepair)
    {
        StopGeneratorRepair();
        return;
    }

    const float kRepairRate = 1.0F / std::max(1.0F, m_tuning.generatorRepairSecondsBase);
    generatorIt->second.progress = glm::clamp(generatorIt->second.progress + kRepairRate * fixedDt, 0.0F, 1.0F);

    if (generatorIt->second.progress >= 1.0F)
    {
        generatorIt->second.progress = 1.0F;
        generatorIt->second.completed = true;
        RefreshGeneratorsCompleted();
        AddRuntimeMessage("Generator completed", 1.8F);
        StopGeneratorRepair();
        return;
    }

    if (m_skillCheckActive)
    {
        m_skillCheckNeedle += m_skillCheckNeedleSpeed * fixedDt;
        if (skillCheckPressed)
        {
            // Add margin for forgiveness - makes hitbox larger
            constexpr float kHitMargin = 0.06F;
            const float expandedStart = m_skillCheckSuccessStart - kHitMargin;
            const float expandedEnd = m_skillCheckSuccessEnd + kHitMargin;
            
            const bool success = m_skillCheckNeedle >= expandedStart && m_skillCheckNeedle <= expandedEnd;
            CompleteSkillCheck(success, false);
        }
        else if (m_skillCheckNeedle >= 1.0F)
        {
            CompleteSkillCheck(false, true);
        }
        return;
    }

    m_skillCheckTimeToNext -= fixedDt;
    if (m_skillCheckTimeToNext <= 0.0F)
    {
        std::uniform_real_distribution<float> zoneStartDist(0.14F, 0.82F);
        std::uniform_real_distribution<float> zoneSizeDist(0.06F, 0.11F);

        const float zoneStart = zoneStartDist(m_rng);
        const float zoneSize = zoneSizeDist(m_rng);
        m_skillCheckSuccessStart = zoneStart;
        m_skillCheckSuccessEnd = std::min(0.98F, zoneStart + zoneSize);
        m_skillCheckNeedle = 0.0F;
        m_skillCheckActive = true;
        AddRuntimeMessage("Skill Check: press SPACE in success zone", 1.6F);
    }
}

void GameplaySystems::StopGeneratorRepair()
{
    if (m_skillCheckActive && m_skillCheckMode == SkillCheckMode::Generator)
    {
        auto generatorIt = m_world.Generators().find(m_activeRepairGenerator);
        if (generatorIt != m_world.Generators().end())
        {
            generatorIt->second.progress = glm::clamp(generatorIt->second.progress - 0.1F, 0.0F, 1.0F);
        }
        
        glm::vec3 fxOrigin{0.0F, 1.0F, 0.0F};
        glm::vec3 fxForward{0.0F, 1.0F, 0.0F};
        const auto generatorTransformIt = m_world.Transforms().find(m_activeRepairGenerator);
        if (generatorTransformIt != m_world.Transforms().end())
        {
            fxOrigin = generatorTransformIt->second.position + glm::vec3{0.0F, 0.7F, 0.0F};
            fxForward = generatorTransformIt->second.forward;
        }
        
        const engine::fx::FxNetMode netMode = m_networkAuthorityMode ? engine::fx::FxNetMode::ServerBroadcast
                                                                      : engine::fx::FxNetMode::Local;
        SpawnGameplayFx("blood_spray", fxOrigin, -fxForward, netMode);
        AddRuntimeMessage("Skill Check abandoned (penalty)", 1.3F);
        
        m_skillCheckActive = false;
        m_skillCheckNeedle = 0.0F;
        m_skillCheckSuccessStart = 0.0F;
        m_skillCheckSuccessEnd = 0.0F;
        m_skillCheckMode = SkillCheckMode::None;
    }
    
    m_activeRepairGenerator = 0;
    ScheduleNextSkillCheck();
}

void GameplaySystems::BeginOrContinueGeneratorRepair(engine::scene::Entity generatorEntity)
{
    const auto generatorIt = m_world.Generators().find(generatorEntity);
    if (generatorIt == m_world.Generators().end() || generatorIt->second.completed)
    {
        return;
    }

    m_activeRepairGenerator = generatorEntity;
    m_skillCheckMode = SkillCheckMode::Generator;
    StopSelfHeal();
    if (m_skillCheckTimeToNext <= 0.0F || m_skillCheckTimeToNext > 8.0F)
    {
        ScheduleNextSkillCheck();
    }
    AddRuntimeMessage("Generator repair started (hold E)", 1.2F);
}

void GameplaySystems::BeginSelfHeal()
{
    if (m_survivorState != SurvivorHealthState::Injured)
    {
        return;
    }

    StopGeneratorRepair();
    m_selfHealActive = true;
    m_skillCheckMode = SkillCheckMode::SelfHeal;
    if (m_skillCheckTimeToNext <= 0.0F || m_skillCheckTimeToNext > 8.0F)
    {
        ScheduleNextSkillCheck();
    }
    AddRuntimeMessage("Self-heal started (hold E)", 1.0F);
}

void GameplaySystems::StopSelfHeal()
{
    if (!m_selfHealActive)
    {
        return;
    }

    m_selfHealActive = false;
    if (m_skillCheckMode == SkillCheckMode::SelfHeal)
    {
        m_skillCheckMode = SkillCheckMode::None;
    }
    if (!m_skillCheckActive)
    {
        ScheduleNextSkillCheck();
    }
}

void GameplaySystems::UpdateSelfHeal(bool holdingHeal, bool skillCheckPressed, float fixedDt)
{
    if (!m_selfHealActive)
    {
        return;
    }

    if (m_survivorState != SurvivorHealthState::Injured || !holdingHeal)
    {
        StopSelfHeal();
        return;
    }

    const float kSelfHealRate = 1.0F / std::max(0.1F, m_tuning.healDurationSeconds);
    m_selfHealProgress = glm::clamp(m_selfHealProgress + kSelfHealRate * fixedDt, 0.0F, 1.0F);

    if (m_selfHealProgress >= 1.0F)
    {
        m_selfHealProgress = 1.0F;
        SetSurvivorState(SurvivorHealthState::Healthy, "Self-heal completed");
        StopSelfHeal();
        return;
    }

    if (m_skillCheckActive && m_skillCheckMode == SkillCheckMode::SelfHeal)
    {
        m_skillCheckNeedle += m_skillCheckNeedleSpeed * fixedDt;
        if (skillCheckPressed)
        {
            constexpr float kHitMargin = 0.06F;
            const float expandedStart = m_skillCheckSuccessStart - kHitMargin;
            const float expandedEnd = m_skillCheckSuccessEnd + kHitMargin;
            
            const bool success = m_skillCheckNeedle >= expandedStart && m_skillCheckNeedle <= expandedEnd;
            CompleteSkillCheck(success, false);
        }
        else if (m_skillCheckNeedle >= 1.0F)
        {
            CompleteSkillCheck(false, true);
        }
        return;
    }

    m_skillCheckTimeToNext -= fixedDt;
    if (m_skillCheckTimeToNext <= 0.0F)
    {
        std::uniform_real_distribution<float> zoneStartDist(0.14F, 0.82F);
        std::uniform_real_distribution<float> zoneSizeDist(0.08F, 0.16F);

        const float zoneStart = zoneStartDist(m_rng);
        const float zoneSize = zoneSizeDist(m_rng);
        m_skillCheckSuccessStart = zoneStart;
        m_skillCheckSuccessEnd = std::min(0.98F, zoneStart + zoneSize);
        m_skillCheckNeedle = 0.0F;
        m_skillCheckActive = true;
        m_skillCheckMode = SkillCheckMode::SelfHeal;
        AddRuntimeMessage("Self-heal skill check", 1.2F);
    }
}

void GameplaySystems::CompleteSkillCheck(bool success, bool timeout)
{
    const bool hookSkillCheck = m_survivorState == SurvivorHealthState::Hooked && m_skillCheckMode == SkillCheckMode::HookStruggle;
    if (m_activeRepairGenerator == 0 && !hookSkillCheck)
    {
        if (!m_selfHealActive)
        {
            return;
        }
    }

    glm::vec3 fxOrigin{0.0F, 1.0F, 0.0F};
    glm::vec3 fxForward{0.0F, 1.0F, 0.0F};
    if (m_activeRepairGenerator != 0)
    {
        const auto generatorTransformIt = m_world.Transforms().find(m_activeRepairGenerator);
        if (generatorTransformIt != m_world.Transforms().end())
        {
            fxOrigin = generatorTransformIt->second.position + glm::vec3{0.0F, 0.7F, 0.0F};
            fxForward = generatorTransformIt->second.forward;
        }
    }
    else
    {
        const auto survivorTransformIt = m_world.Transforms().find(m_survivor);
        if (survivorTransformIt != m_world.Transforms().end())
        {
            fxOrigin = survivorTransformIt->second.position + glm::vec3{0.0F, 0.8F, 0.0F};
            fxForward = survivorTransformIt->second.forward;
        }
    }
    const engine::fx::FxNetMode netMode = m_networkAuthorityMode ? engine::fx::FxNetMode::ServerBroadcast
                                                                  : engine::fx::FxNetMode::Local;

    if (success)
    {
        if (hookSkillCheck)
        {
            AddRuntimeMessage("Hook skill check success", 1.1F);
        }
        else if (m_selfHealActive)
        {
            m_selfHealProgress = glm::clamp(m_selfHealProgress + 0.08F, 0.0F, 1.0F);
        }
        else
        {
            auto generatorIt = m_world.Generators().find(m_activeRepairGenerator);
            if (generatorIt != m_world.Generators().end())
            {
                generatorIt->second.progress = glm::clamp(generatorIt->second.progress + 0.05F, 0.0F, 1.0F);
            }
        }
        SpawnGameplayFx("hit_spark", fxOrigin, fxForward, netMode);
        AddRuntimeMessage("Skill Check success", 1.2F);
    }
    else
    {
        if (hookSkillCheck)
        {
            m_hookStageTimer = std::min(
                (m_hookStage == 1 ? m_hookStageOneDuration : m_hookStageTwoDuration),
                m_hookStageTimer + m_hookStageFailPenaltySeconds
            );
        }
        else if (m_selfHealActive)
        {
            m_selfHealProgress = glm::clamp(m_selfHealProgress - 0.1F, 0.0F, 1.0F);
        }
        else
        {
            auto generatorIt = m_world.Generators().find(m_activeRepairGenerator);
            if (generatorIt != m_world.Generators().end())
            {
                generatorIt->second.progress = glm::clamp(generatorIt->second.progress - 0.1F, 0.0F, 1.0F);
            }
        }
        SpawnGameplayFx("blood_spray", fxOrigin, -fxForward, netMode);
        AddRuntimeMessage(timeout ? "Skill Check missed (penalty)" : "Skill Check failed (penalty)", 1.3F);
    }

    m_skillCheckActive = false;
    m_skillCheckNeedle = 0.0F;
    m_skillCheckSuccessStart = 0.0F;
    m_skillCheckSuccessEnd = 0.0F;

    if (m_selfHealActive && m_selfHealProgress >= 1.0F)
    {
        m_selfHealProgress = 1.0F;
        SetSurvivorState(SurvivorHealthState::Healthy, "Self-heal completed");
        StopSelfHeal();
        return;
    }

    auto generatorIt = m_world.Generators().find(m_activeRepairGenerator);
    if (!hookSkillCheck && !m_selfHealActive && generatorIt != m_world.Generators().end() && generatorIt->second.progress >= 1.0F)
    {
        generatorIt->second.progress = 1.0F;
        generatorIt->second.completed = true;
        RefreshGeneratorsCompleted();
        AddRuntimeMessage("Generator completed", 1.8F);
        StopGeneratorRepair();
        return;
    }

    if (hookSkillCheck)
    {
        m_skillCheckMode = SkillCheckMode::HookStruggle;
        std::uniform_real_distribution<float> nextDist(1.4F, 3.2F);
        m_hookSkillCheckTimeToNext = nextDist(m_rng);
    }
    else
    {
        m_skillCheckMode = m_selfHealActive ? SkillCheckMode::SelfHeal : SkillCheckMode::Generator;
        ScheduleNextSkillCheck();
    }
}

void GameplaySystems::ScheduleNextSkillCheck()
{
    std::uniform_real_distribution<float> dist(m_tuning.skillCheckMinInterval, m_tuning.skillCheckMaxInterval);
    m_skillCheckTimeToNext = dist(m_rng);
}

void GameplaySystems::RefreshGeneratorsCompleted()
{
    int completed = 0;
    for (const auto& [_, generator] : m_world.Generators())
    {
        if (generator.completed || generator.progress >= 1.0F)
        {
            ++completed;
        }
    }
    m_generatorsCompleted = completed;
}

void GameplaySystems::ResolveKillerSurvivorCollision()
{
    if (!m_collisionEnabled || m_killer == 0 || m_survivor == 0)
    {
        return;
    }

    // Allow temporary overlap right after hit so killer/survivor don't snag on geometry.
    if (m_killerSurvivorNoCollisionTimer > 0.0F)
    {
        return;
    }

    // Killer can walk through downed/carried/hooked/dead survivor.
    if (m_survivorState == SurvivorHealthState::Downed ||
        m_survivorState == SurvivorHealthState::Carried ||
        m_survivorState == SurvivorHealthState::Hooked ||
        m_survivorState == SurvivorHealthState::Dead)
    {
        return;
    }

    auto killerTransformIt = m_world.Transforms().find(m_killer);
    auto survivorTransformIt = m_world.Transforms().find(m_survivor);
    auto killerActorIt = m_world.Actors().find(m_killer);
    auto survivorActorIt = m_world.Actors().find(m_survivor);
    if (killerTransformIt == m_world.Transforms().end() ||
        survivorTransformIt == m_world.Transforms().end() ||
        killerActorIt == m_world.Actors().end() ||
        survivorActorIt == m_world.Actors().end())
    {
        return;
    }

    engine::scene::Transform& killerTransform = killerTransformIt->second;
    engine::scene::Transform& survivorTransform = survivorTransformIt->second;
    engine::scene::ActorComponent& killerActor = killerActorIt->second;
    engine::scene::ActorComponent& survivorActor = survivorActorIt->second;
    if (!killerActor.collisionEnabled || !survivorActor.collisionEnabled)
    {
        return;
    }

    const float combinedRadius = std::max(0.01F, killerActor.capsuleRadius + survivorActor.capsuleRadius);
    glm::vec2 delta{survivorTransform.position.x - killerTransform.position.x,
                    survivorTransform.position.z - killerTransform.position.z};
    const float distanceSq = glm::dot(delta, delta);
    if (distanceSq >= combinedRadius * combinedRadius)
    {
        return;
    }

    float distance = std::sqrt(std::max(distanceSq, 1.0e-8F));
    glm::vec2 normal{0.0F};
    if (distance > 1.0e-5F)
    {
        normal = delta / distance;
    }
    else
    {
        normal = glm::vec2{killerTransform.forward.x, killerTransform.forward.z};
        if (glm::length(normal) <= 1.0e-5F)
        {
            normal = glm::vec2{1.0F, 0.0F};
        }
        else
        {
            normal = glm::normalize(normal);
        }
        distance = 0.0F;
    }

    const float penetration = combinedRadius - distance;

    const glm::vec2 killerMoveStep{
        killerTransform.position.x - m_killerPreMovePosition.x,
        killerTransform.position.z - m_killerPreMovePosition.z
    };
    const glm::vec2 survivorMoveStep{
        survivorTransform.position.x - m_survivorPreMovePosition.x,
        survivorTransform.position.z - m_survivorPreMovePosition.z
    };
    const bool killerMoved = m_killerPreMovePositionValid && glm::dot(killerMoveStep, killerMoveStep) > 1.0e-8F;
    const bool survivorMoved = m_survivorPreMovePositionValid && glm::dot(survivorMoveStep, survivorMoveStep) > 1.0e-8F;

    // Slide against the other actor capsule: remove only the into-normal component and keep tangent.
    if (killerMoved && m_killerPreMovePositionValid)
    {
        glm::vec2 adjustedStep = killerMoveStep;
        const float into = glm::dot(adjustedStep, normal);
        if (into > 0.0F)
        {
            adjustedStep -= normal * into;
        }
        killerTransform.position.x = m_killerPreMovePosition.x + adjustedStep.x;
        killerTransform.position.z = m_killerPreMovePosition.z + adjustedStep.y;
    }

    if (survivorMoved && m_survivorPreMovePositionValid)
    {
        glm::vec2 adjustedStep = survivorMoveStep;
        const glm::vec2 survivorNormal = -normal;
        const float into = glm::dot(adjustedStep, survivorNormal);
        if (into > 0.0F)
        {
            adjustedStep -= survivorNormal * into;
        }
        survivorTransform.position.x = m_survivorPreMovePosition.x + adjustedStep.x;
        survivorTransform.position.z = m_survivorPreMovePosition.z + adjustedStep.y;
    }

    // If capsules are still interpenetrating after slide projection, depenetrate minimally.
    glm::vec2 postDelta{
        survivorTransform.position.x - killerTransform.position.x,
        survivorTransform.position.z - killerTransform.position.z
    };
    float postDistanceSq = glm::dot(postDelta, postDelta);
    if (postDistanceSq < combinedRadius * combinedRadius)
    {
        float postDistance = std::sqrt(std::max(postDistanceSq, 1.0e-8F));
        glm::vec2 postNormal{0.0F};
        if (postDistance > 1.0e-5F)
        {
            postNormal = postDelta / postDistance;
        }
        else
        {
            postNormal = normal;
        }

        const float postPenetration = combinedRadius - postDistance;
        const glm::vec2 depenetration = postNormal * ((postPenetration + 0.002F) * 0.5F);
        killerTransform.position.x -= depenetration.x;
        killerTransform.position.z -= depenetration.y;
        survivorTransform.position.x += depenetration.x;
        survivorTransform.position.z += depenetration.y;
    }

    // Preserve tangential motion and cancel only into-normal velocity so actors can slide.
    glm::vec2 killerHorizontalVel{killerActor.velocity.x, killerActor.velocity.z};
    const float killerInto = glm::dot(killerHorizontalVel, normal);
    if (killerInto > 0.0F)
    {
        killerHorizontalVel -= normal * killerInto;
        killerActor.velocity.x = killerHorizontalVel.x;
        killerActor.velocity.z = killerHorizontalVel.y;
    }

    glm::vec2 survivorHorizontalVel{survivorActor.velocity.x, survivorActor.velocity.z};
    const glm::vec2 survivorNormal = -normal;
    const float survivorInto = glm::dot(survivorHorizontalVel, survivorNormal);
    if (survivorInto > 0.0F)
    {
        survivorHorizontalVel -= survivorNormal * survivorInto;
        survivorActor.velocity.x = survivorHorizontalVel.x;
        survivorActor.velocity.z = survivorHorizontalVel.y;
    }

    killerActor.lastCollisionNormal = glm::vec3{-normal.x, 0.0F, -normal.y};
    survivorActor.lastCollisionNormal = glm::vec3{normal.x, 0.0F, normal.y};
    killerActor.lastPenetrationDepth = std::max(killerActor.lastPenetrationDepth, penetration);
    survivorActor.lastPenetrationDepth = std::max(survivorActor.lastPenetrationDepth, penetration);
}

void GameplaySystems::ApplyKillerAttackAftermath(bool hit, bool lungeAttack)
{
    if (hit)
    {
        m_killerSurvivorNoCollisionTimer = std::max(
            m_killerSurvivorNoCollisionTimer,
            m_killerSurvivorNoCollisionAfterHitSeconds
        );
        m_survivorHitHasteTimer = std::max(m_survivorHitHasteTimer, m_survivorHitHasteSeconds);
        m_killerSlowTimer = std::max(m_killerSlowTimer, m_killerHitSlowSeconds);
        m_killerSlowMultiplier = m_killerHitSlowMultiplier;
        if (lungeAttack)
        {
            AddRuntimeMessage("Hit: survivor speed boost, killer slow", 1.1F);
        }
        return;
    }

    m_killerSlowTimer = std::max(m_killerSlowTimer, m_killerMissSlowSeconds);
    m_killerSlowMultiplier = m_killerMissSlowMultiplier;
    if (lungeAttack)
    {
        AddRuntimeMessage("Lunge missed: short killer slow", 1.0F);
    }
}

void GameplaySystems::ApplySurvivorHit()
{
    // Reset bloodlust on hit (DBD-like)
    if (m_bloodlust.tier > 0)
    {
        ResetBloodlust();
    }

    // Check for Exposed status effect - instant down from any non-downed state
    const bool survivorIsExposed = m_statusEffectManager.IsExposed(m_survivor);
    if (survivorIsExposed &&
        m_survivorState != SurvivorHealthState::Downed &&
        m_survivorState != SurvivorHealthState::Hooked &&
        m_survivorState != SurvivorHealthState::Dead)
    {
        if (SetSurvivorState(SurvivorHealthState::Downed, "Killer hit (Exposed)", true))
        {
            m_statusEffectManager.RemoveEffect(m_survivor, StatusEffectType::Exposed);
            return;
        }
    }

    if (m_survivorState == SurvivorHealthState::Healthy)
    {
        SetSurvivorState(SurvivorHealthState::Injured, "Killer hit");
        return;
    }

    if (m_survivorState == SurvivorHealthState::Injured)
    {
        SetSurvivorState(SurvivorHealthState::Downed, "Killer hit");
        return;
    }

    if (m_survivorState == SurvivorHealthState::Trapped)
    {
        SetSurvivorState(SurvivorHealthState::Downed, "Killer hit trapped survivor");
    }
}

bool GameplaySystems::SetSurvivorState(SurvivorHealthState nextState, const std::string& reason, bool force)
{
    const SurvivorHealthState previous = m_survivorState;
    if (!force && !CanTransitionSurvivorState(previous, nextState))
    {
        return false;
    }

    m_survivorState = nextState;

    if (previous == SurvivorHealthState::Hooked && nextState != SurvivorHealthState::Hooked)
    {
        auto activeHookIt = m_world.Hooks().find(m_activeHookEntity);
        if (activeHookIt != m_world.Hooks().end())
        {
            activeHookIt->second.occupied = false;
        }
        m_activeHookEntity = 0;
    }
    if (previous == SurvivorHealthState::Trapped && nextState != SurvivorHealthState::Trapped)
    {
        ClearTrappedSurvivorBinding(m_survivor, true);
    }

    if (nextState == SurvivorHealthState::Carried)
    {
        m_carryEscapeProgress = 0.0F;
        m_carryLastQteDirection = 0;
        m_carryInputGraceTimer = 0.65F;
        m_survivorWigglePressQueue.clear();
    }

    if (nextState == SurvivorHealthState::Hooked)
    {
        m_hookStage = std::max(1, m_hookStage);
        m_hookStageTimer = 0.0F;
        m_hookEscapeAttemptsUsed = 0;
        m_hookSkillCheckTimeToNext = 1.2F;
        m_skillCheckActive = false;
        m_skillCheckMode = SkillCheckMode::None;
    }
    else
    {
        m_hookStage = 0;
        m_hookStageTimer = 0.0F;
        m_hookEscapeAttemptsUsed = 0;
        if (m_skillCheckMode == SkillCheckMode::HookStruggle)
        {
            m_skillCheckMode = SkillCheckMode::None;
            m_skillCheckActive = false;
        }
    }

    if (nextState != SurvivorHealthState::Healthy && nextState != SurvivorHealthState::Injured)
    {
        StopGeneratorRepair();
        StopSelfHeal();
    }
    if (nextState == SurvivorHealthState::Healthy)
    {
        m_selfHealProgress = 0.0F;
    }
    if (nextState == SurvivorHealthState::Injured && previous != SurvivorHealthState::Injured)
    {
        m_selfHealProgress = 0.0F;
    }
    if (nextState != SurvivorHealthState::Healthy && nextState != SurvivorHealthState::Injured)
    {
        m_survivorHitHasteTimer = 0.0F;
    }

    const auto survivorActorIt = m_world.Actors().find(m_survivor);
    if (survivorActorIt != m_world.Actors().end())
    {
        engine::scene::ActorComponent& actor = survivorActorIt->second;
        actor.carried = nextState == SurvivorHealthState::Carried;
        actor.crouching = false;
        actor.crawling = false;
        actor.sprinting = false;
        actor.forwardRunupDistance = 0.0F;
        actor.velocity = glm::vec3{0.0F};
        actor.collisionEnabled = (nextState == SurvivorHealthState::Healthy ||
                                  nextState == SurvivorHealthState::Injured ||
                                  nextState == SurvivorHealthState::Downed ||
                                  nextState == SurvivorHealthState::Trapped)
                                     ? m_collisionEnabled
                                     : false;
    }

    const auto survivorTransformIt = m_world.Transforms().find(m_survivor);
    if (survivorTransformIt != m_world.Transforms().end() && nextState == SurvivorHealthState::Dead)
    {
        survivorTransformIt->second.position = glm::vec3{0.0F, -200.0F, 0.0F};
    }

    AddRuntimeMessage(
        std::string{"Survivor state: "} + SurvivorStateToText(previous) + " -> " + SurvivorStateToText(nextState) +
            " (" + reason + ")",
        2.2F
    );
    return true;
}

bool GameplaySystems::CanTransitionSurvivorState(SurvivorHealthState from, SurvivorHealthState to) const
{
    if (from == to)
    {
        return true;
    }

    switch (from)
    {
        case SurvivorHealthState::Healthy:
            return to == SurvivorHealthState::Injured || to == SurvivorHealthState::Trapped;
        case SurvivorHealthState::Injured:
            return to == SurvivorHealthState::Healthy || to == SurvivorHealthState::Downed || to == SurvivorHealthState::Trapped;
        case SurvivorHealthState::Downed:
            return to == SurvivorHealthState::Carried;
        case SurvivorHealthState::Trapped:
            return to == SurvivorHealthState::Injured ||
                   to == SurvivorHealthState::Downed ||
                   to == SurvivorHealthState::Carried;
        case SurvivorHealthState::Carried:
            return to == SurvivorHealthState::Hooked ||
                   to == SurvivorHealthState::Downed ||
                   to == SurvivorHealthState::Injured;
        case SurvivorHealthState::Hooked:
            return to == SurvivorHealthState::Dead || to == SurvivorHealthState::Injured;
        case SurvivorHealthState::Dead:
            return false;
        default:
            return false;
    }
}

const char* GameplaySystems::SurvivorStateToText(SurvivorHealthState state)
{
    switch (state)
    {
        case SurvivorHealthState::Healthy: return "Healthy";
        case SurvivorHealthState::Injured: return "Injured";
        case SurvivorHealthState::Downed: return "Downed";
        case SurvivorHealthState::Trapped: return "Trapped";
        case SurvivorHealthState::Carried: return "Carried";
        case SurvivorHealthState::Hooked: return "Hooked";
        case SurvivorHealthState::Dead: return "Dead";
        default: return "Unknown";
    }
}

const char* GameplaySystems::KillerAttackStateToText(KillerAttackState state) const
{
    switch (state)
    {
        case KillerAttackState::Idle: return "Idle";
        case KillerAttackState::ChargingLunge: return "Charging";
        case KillerAttackState::Lunging: return "Lunging";
        case KillerAttackState::Recovering: return "Recovering";
        default: return "Idle";
    }
}

std::string GameplaySystems::BuildMovementStateText(engine::scene::Entity entity, const engine::scene::ActorComponent& actor) const
{
    if (entity == m_survivor)
    {
        if (m_survivorState == SurvivorHealthState::Carried)
        {
            return "Carried";
        }
        if (m_survivorState == SurvivorHealthState::Trapped)
        {
            return "Trapped";
        }
        if (m_survivorState == SurvivorHealthState::Downed)
        {
            return "Crawling";
        }
    }
    if (actor.crouching)
    {
        return "Crouching";
    }

    const float speed = glm::length(glm::vec2{actor.velocity.x, actor.velocity.z});
    if (actor.sprinting && speed > 0.2F)
    {
        return "Running";
    }
    if (speed > 0.2F)
    {
        return "Walking";
    }
    return "Idle";
}

engine::fx::FxSystem::FxInstanceId GameplaySystems::SpawnGameplayFx(
    const std::string& assetId,
    const glm::vec3& position,
    const glm::vec3& forward,
    engine::fx::FxNetMode mode
)
{
    if (assetId.empty())
    {
        return 0;
    }
    return m_fxSystem.Spawn(assetId, position, forward, {}, mode);
}

GameplaySystems::RoleCommand GameplaySystems::BuildLocalRoleCommand(
    engine::scene::Role role,
    const engine::platform::Input& input,
    const engine::platform::ActionBindings& bindings,
    bool controlsEnabled,
    bool inputLocked
) const
{
    RoleCommand command;
    if (!controlsEnabled || inputLocked)
    {
        return command;
    }

    command.moveAxis = ReadMoveAxis(input, bindings);
    command.lookDelta = input.MouseDelta();
    if (m_invertLookY)
    {
        command.lookDelta.y = -command.lookDelta.y;
    }
    command.sprinting = role == engine::scene::Role::Survivor && bindings.IsDown(input, engine::platform::InputAction::Sprint);
    command.crouchHeld = bindings.IsDown(input, engine::platform::InputAction::Crouch);
    command.jumpPressed = input.IsKeyPressed(GLFW_KEY_SPACE);
    command.interactPressed = bindings.IsPressed(input, engine::platform::InputAction::Interact);
    command.interactHeld = bindings.IsDown(input, engine::platform::InputAction::Interact);
    command.attackPressed = bindings.IsPressed(input, engine::platform::InputAction::AttackShort);
    command.attackHeld = bindings.IsDown(input, engine::platform::InputAction::AttackShort) ||
                         bindings.IsDown(input, engine::platform::InputAction::AttackLunge);
    command.attackReleased = bindings.IsReleased(input, engine::platform::InputAction::AttackShort) ||
                             bindings.IsReleased(input, engine::platform::InputAction::AttackLunge);
    command.lungeHeld = bindings.IsDown(input, engine::platform::InputAction::AttackLunge);
    command.useAltPressed = input.IsMousePressed(GLFW_MOUSE_BUTTON_RIGHT);
    command.useAltHeld = input.IsMouseDown(GLFW_MOUSE_BUTTON_RIGHT);
    command.useAltReleased = input.IsMouseReleased(GLFW_MOUSE_BUTTON_RIGHT);
    command.dropItemPressed = role == engine::scene::Role::Survivor && input.IsKeyPressed(GLFW_KEY_R);
    command.pickupItemPressed = role == engine::scene::Role::Survivor && input.IsMousePressed(GLFW_MOUSE_BUTTON_LEFT);
    command.wiggleLeftPressed = bindings.IsPressed(input, engine::platform::InputAction::MoveLeft);
    command.wiggleRightPressed = bindings.IsPressed(input, engine::platform::InputAction::MoveRight);
    return command;
}

void GameplaySystems::UpdateInteractBuffer(engine::scene::Role role, const RoleCommand& command, float fixedDt)
{
    const std::uint8_t index = RoleToIndex(role);
    if (command.interactPressed)
    {
        m_interactBufferRemaining[index] = m_interactBufferWindowSeconds;
        return;
    }

    m_interactBufferRemaining[index] = std::max(0.0F, m_interactBufferRemaining[index] - fixedDt);
}

bool GameplaySystems::ConsumeInteractBuffered(engine::scene::Role role)
{
    const std::uint8_t index = RoleToIndex(role);
    if (m_interactBufferRemaining[index] <= 0.0F)
    {
        return false;
    }

    m_interactBufferRemaining[index] = 0.0F;
    return true;
}

void GameplaySystems::ConsumeWigglePressedForSurvivor(bool& leftPressed, bool& rightPressed)
{
    leftPressed = false;
    rightPressed = false;
    if (m_survivorWigglePressQueue.empty())
    {
        return;
    }

    const int value = m_survivorWigglePressQueue.front();
    m_survivorWigglePressQueue.erase(m_survivorWigglePressQueue.begin());
    leftPressed = value < 0;
    rightPressed = value > 0;
}

std::uint8_t GameplaySystems::RoleToIndex(engine::scene::Role role)
{
    return role == engine::scene::Role::Survivor ? 0U : 1U;
}

engine::scene::Role GameplaySystems::OppositeRole(engine::scene::Role role)
{
    return role == engine::scene::Role::Survivor ? engine::scene::Role::Killer : engine::scene::Role::Survivor;
}

void GameplaySystems::AddRuntimeMessage(const std::string& text, float ttl)
{
    std::cout << text << "\n";
    m_messages.push_back(TimedMessage{text, ttl});
    if (m_messages.size() > 6)
    {
        m_messages.erase(m_messages.begin());
    }
}

float GameplaySystems::DistanceXZ(const glm::vec3& a, const glm::vec3& b)
{
    const glm::vec2 d = glm::vec2{a.x, a.z} - glm::vec2{b.x, b.z};
    return glm::length(d);
}

float GameplaySystems::DistancePointToSegment(const glm::vec3& point, const glm::vec3& segmentA, const glm::vec3& segmentB)
{
    const glm::vec3 ab = segmentB - segmentA;
    const float denominator = glm::dot(ab, ab);
    if (denominator <= 1.0e-7F)
    {
        return glm::length(point - segmentA);
    }

    const float t = glm::clamp(glm::dot(point - segmentA, ab) / denominator, 0.0F, 1.0F);
    const glm::vec3 closest = segmentA + ab * t;
    return glm::length(point - closest);
}

glm::vec3 GameplaySystems::ForwardFromYawPitch(float yaw, float pitch)
{
    const float cosPitch = std::cos(pitch);
    return glm::normalize(glm::vec3{
        std::sin(yaw) * cosPitch,
        std::sin(pitch),
        -std::cos(yaw) * cosPitch,
    });
}

bool GameplaySystems::IsSurvivorInKillerFOV(
    const glm::vec3& killerPos, const glm::vec3& killerForward,
    const glm::vec3& survivorPos, float fovDegrees)
{
    glm::vec3 toSurvivor = survivorPos - killerPos;
    toSurvivor.y = 0.0F; // Flatten to XZ plane

    const float distance = glm::length(toSurvivor);
    if (distance < 1.0F) return true; // Too close, definitely in FOV

    const glm::vec3 dirToSurvivor = glm::normalize(toSurvivor);
    const glm::vec3 killerFlat = glm::normalize(glm::vec3(killerForward.x, 0.0F, killerForward.z));

    const float fovRad = glm::radians(fovDegrees);
    const float cosHalfFov = std::cos(fovRad * 0.5F);

    return glm::dot(killerFlat, dirToSurvivor) >= cosHalfFov;
}

bool GameplaySystems::IsSurvivorInKillerCenterFOV(
    const glm::vec3& killerPos, const glm::vec3& killerForward,
    const glm::vec3& survivorPos)
{
    // DBD-like: ±35° from killer's forward (center FOV for chase gating)
    constexpr float centerFovDegrees = 35.0F;
    return IsSurvivorInKillerFOV(killerPos, killerForward, survivorPos, centerFovDegrees * 2.0F);
}

//==============================================================================
// Bloodlust System (DBD-like)
//==============================================================================

void GameplaySystems::ResetBloodlust()
{
    const int oldTier = m_bloodlust.tier;
    m_bloodlust.tier = 0;
    m_bloodlust.timeInChase = 0.0F;
    m_bloodlust.lastTierChangeTime = 0.0F;

    // Re-apply speed to remove bloodlust bonus
    SetRoleSpeedPercent("killer", m_killerSpeedPercent);

    if (oldTier > 0)
    {
        AddRuntimeMessage("Bloodlust reset", 1.0F);
    }
}

void GameplaySystems::SetBloodlustTier(int tier)
{
    const int clampedTier = glm::clamp(tier, 0, 3);
    if (m_bloodlust.tier != clampedTier)
    {
        m_bloodlust.tier = clampedTier;
        m_bloodlust.lastTierChangeTime = m_elapsedSeconds;
        AddRuntimeMessage("Bloodlust tier " + std::to_string(clampedTier), 1.0F);
    }
}

float GameplaySystems::GetBloodlustSpeedMultiplier() const
{
    // DBD-like bloodlust tiers
    // Tier 0: 100% (no bonus)
    // Tier 1: 120% (at 15s in chase)
    // Tier 2: 125% (at 25s in chase)
    // Tier 3: 130% (at 35s in chase)
    switch (m_bloodlust.tier)
    {
        case 1: return 1.20F;
        case 2: return 1.25F;
        case 3: return 1.30F;
        default: return 1.0F;
    }
}

void GameplaySystems::UpdateBloodlust(float fixedDt)
{
    // Bloodlust only progresses during active chase
    if (!m_chase.isChasing)
    {
        // Reset immediately when chase ends
        if (m_bloodlust.tier > 0 || m_bloodlust.timeInChase > 0.0F)
        {
            ResetBloodlust();
        }
        return;
    }

    // Only server-authoritative mode should compute bloodlust
    // For now, we always compute (will be replicated in multiplayer)

    m_bloodlust.timeInChase += fixedDt;

    // DBD-like tier thresholds
    // Tier 1: 15s → 120% speed
    // Tier 2: 25s → 125% speed
    // Tier 3: 35s → 130% speed
    const int newTier = [this]() -> int {
        if (m_bloodlust.timeInChase >= 35.0F) return 3;
        if (m_bloodlust.timeInChase >= 25.0F) return 2;
        if (m_bloodlust.timeInChase >= 15.0F) return 1;
        return 0;
    }();

    if (newTier != m_bloodlust.tier)
    {
        SetBloodlustTier(newTier);
        // Apply new speed multiplier
        SetRoleSpeedPercent("killer", m_killerSpeedPercent);
    }
}

// ============================================================================
// Phase B2/B3: Scratch Marks and Blood Pools (Refactored for DBD accuracy)
// ============================================================================

float GameplaySystems::DeterministicRandom(const glm::vec3& position, int seed)
{
    unsigned int hash = static_cast<unsigned int>(seed);
    hash ^= static_cast<unsigned int>(static_cast<int>(position.x * 1000.0F));
    hash ^= static_cast<unsigned int>(static_cast<int>(position.y * 1000.0F)) << 8;
    hash ^= static_cast<unsigned int>(static_cast<int>(position.z * 1000.0F)) << 16;
    hash = (hash ^ (hash >> 16)) * 0x85ebca6b;
    hash = (hash ^ (hash >> 13)) * 0xc2b2ae35;
    hash = hash ^ (hash >> 16);
    return static_cast<float>(hash % 10000) / 10000.0F;
}

glm::vec3 GameplaySystems::ComputePerpendicular(const glm::vec3& forward)
{
    glm::vec3 up{0.0F, 1.0F, 0.0F};
    glm::vec3 perp = glm::cross(forward, up);
    if (glm::length(perp) < 0.001F)
    {
        perp = glm::vec3{1.0F, 0.0F, 0.0F};
    }
    return glm::normalize(perp);
}

bool GameplaySystems::CanSeeScratchMarks(bool localIsKiller) const
{
    if (m_scratchProfile.allowSurvivorSeeOwn)
    {
        return true;
    }
    return localIsKiller;
}

bool GameplaySystems::CanSeeBloodPools(bool localIsKiller) const
{
    if (m_bloodProfile.allowSurvivorSeeOwn)
    {
        return true;
    }
    return localIsKiller;
}

void GameplaySystems::UpdateScratchMarks(float fixedDt, const glm::vec3& survivorPos, const glm::vec3& survivorForward, bool survivorSprinting)
{
    for (auto& mark : m_scratchMarks)
    {
        if (mark.active)
        {
            mark.age += fixedDt;
            if (mark.age >= mark.lifetime)
            {
                mark.active = false;
            }
        }
    }

    if (!survivorSprinting)
    {
        return;
    }

    const float distFromLast = glm::length(glm::vec2{survivorPos.x - m_lastScratchSpawnPos.x, survivorPos.z - m_lastScratchSpawnPos.z});
    if (distFromLast < m_scratchProfile.minDistanceFromLast)
    {
        return;
    }

    m_scratchSpawnAccumulator += fixedDt;
    if (m_scratchSpawnAccumulator < m_scratchNextInterval)
    {
        return;
    }

    m_scratchSpawnAccumulator -= m_scratchNextInterval;

    const float intervalRand = DeterministicRandom(survivorPos + glm::vec3{0.0F, m_scratchSpawnAccumulator, 0.0F}, 0);
    m_scratchNextInterval = m_scratchProfile.spawnIntervalMin + intervalRand * (m_scratchProfile.spawnIntervalMax - m_scratchProfile.spawnIntervalMin);

    ScratchMark& mark = m_scratchMarks[m_scratchMarkHead];
    mark.active = true;
    mark.age = 0.0F;
    mark.lifetime = m_scratchProfile.lifetime;
    mark.direction = survivorForward;
    mark.yawDeg = glm::degrees(std::atan2(survivorForward.x, survivorForward.z));
    mark.perpOffset = ComputePerpendicular(survivorForward);

    const float jitterRand1 = DeterministicRandom(survivorPos, 1) * 2.0F - 1.0F;
    const float jitterRand2 = DeterministicRandom(survivorPos, 2) * 2.0F - 1.0F;
    const glm::vec3 jitter{jitterRand1 * m_scratchProfile.jitterRadius, 0.0F, jitterRand2 * m_scratchProfile.jitterRadius};

    constexpr float behindOffset = 1.2F;
    mark.position = survivorPos - mark.direction * behindOffset + jitter;

    const float sizeRand = DeterministicRandom(survivorPos, 3);
    mark.size = m_scratchProfile.sizeMin + sizeRand * (m_scratchProfile.sizeMax - m_scratchProfile.sizeMin);

    glm::vec3 rayStart = mark.position;
    rayStart.y += 2.0F;
    const glm::vec3 rayEnd = rayStart + glm::vec3{0.0F, -10.0F, 0.0F};

    const std::optional<engine::physics::RaycastHit> hit = m_physics.RaycastNearest(rayStart, rayEnd);
    if (hit.has_value())
    {
        mark.position.y = hit->position.y + 0.02F;
    }
    else
    {
        mark.position.y = 0.02F;
    }

    m_scratchMarkHead = (m_scratchMarkHead + 1) % kScratchMarkPoolSize;
    m_lastScratchSpawnPos = survivorPos;
}

void GameplaySystems::UpdateBloodPools(float fixedDt, const glm::vec3& survivorPos, bool survivorInjuredOrDowned, bool survivorMoving)
{
    for (auto& pool : m_bloodPools)
    {
        if (pool.active)
        {
            pool.age += fixedDt;
            if (pool.age >= pool.lifetime)
            {
                pool.active = false;
            }
        }
    }

    if (!survivorInjuredOrDowned)
    {
        return;
    }

    if (m_bloodProfile.onlyWhenMoving && !survivorMoving)
    {
        return;
    }

    const float distFromLast = glm::length(glm::vec2{survivorPos.x - m_lastBloodSpawnPos.x, survivorPos.z - m_lastBloodSpawnPos.z});
    if (distFromLast < m_bloodProfile.minDistanceFromLast)
    {
        return;
    }

    m_bloodSpawnAccumulator += fixedDt;
    if (m_bloodSpawnAccumulator < m_bloodProfile.spawnInterval)
    {
        return;
    }

    m_bloodSpawnAccumulator -= m_bloodProfile.spawnInterval;

    BloodPool& pool = m_bloodPools[m_bloodPoolHead];
    pool.active = true;
    pool.age = 0.0F;
    pool.lifetime = m_bloodProfile.lifetime;

    const float jitterRand1 = DeterministicRandom(survivorPos, 10) * 2.0F - 1.0F;
    const float jitterRand2 = DeterministicRandom(survivorPos, 11) * 2.0F - 1.0F;
    pool.position = survivorPos + glm::vec3{jitterRand1 * 0.3F, 0.0F, jitterRand2 * 0.3F};

    const float sizeRand = DeterministicRandom(survivorPos, 12);
    pool.size = m_bloodProfile.sizeMin + sizeRand * (m_bloodProfile.sizeMax - m_bloodProfile.sizeMin);

    glm::vec3 rayStart = pool.position;
    rayStart.y += 2.0F;
    const glm::vec3 rayEnd = rayStart + glm::vec3{0.0F, -10.0F, 0.0F};

    const std::optional<engine::physics::RaycastHit> hit = m_physics.RaycastNearest(rayStart, rayEnd);
    if (hit.has_value())
    {
        pool.position.y = hit->position.y + 0.01F;
    }
    else
    {
        pool.position.y = 0.01F;
    }

    m_bloodPoolHead = (m_bloodPoolHead + 1) % kBloodPoolPoolSize;
    m_lastBloodSpawnPos = survivorPos;
}

void GameplaySystems::RenderScratchMarks(engine::render::Renderer& renderer, bool localIsKiller) const
{
    const bool visible = CanSeeScratchMarks(localIsKiller) || m_scratchDebugEnabled;

    if (!visible)
    {
        return;
    }

    const glm::vec3 baseColor{0.65F, 0.15F, 0.12F};

    for (const ScratchMark& mark : m_scratchMarks)
    {
        if (!mark.active)
        {
            continue;
        }

        const float lifeT = mark.age / mark.lifetime;
        const float alpha = glm::max(0.0F, 1.0F - lifeT);

        constexpr float halfWidth = 0.04F;
        const float streakLength = mark.size * 0.8F;

        const glm::vec3 perp = mark.perpOffset * halfWidth;

        const glm::vec3 p1 = mark.position - mark.direction * streakLength * 0.5F - perp;
        const glm::vec3 p2 = mark.position - mark.direction * streakLength * 0.5F + perp;
        const glm::vec3 p3 = mark.position + mark.direction * streakLength * 0.3F + perp * 0.7F;
        const glm::vec3 p4 = mark.position + mark.direction * streakLength * 0.5F + perp * 0.4F;

        renderer.DrawLine(p1, p2, baseColor * alpha);
        renderer.DrawLine(p2, p3, baseColor * alpha);
        renderer.DrawLine(p3, p4, baseColor * alpha);

        renderer.DrawOrientedBox(
            mark.position,
            glm::vec3{halfWidth * 0.5F, 0.01F, halfWidth * 0.5F},
            glm::vec3{0.0F, mark.yawDeg, 0.0F},
            baseColor * alpha
        );
    }
}

void GameplaySystems::RenderBloodPools(engine::render::Renderer& renderer, bool localIsKiller) const
{
    const bool visible = CanSeeBloodPools(localIsKiller) || m_bloodDebugEnabled;

    if (!visible)
    {
        return;
    }

    for (const BloodPool& pool : m_bloodPools)
    {
        if (!pool.active)
        {
            continue;
        }

        const float lifeT = pool.age / pool.lifetime;
        const float alpha = glm::max(0.0F, 1.0F - lifeT * lifeT);

        const glm::vec3 color{0.55F, 0.08F, 0.08F};

        renderer.DrawBox(
            pool.position,
            glm::vec3{pool.size * 0.5F, 0.01F, pool.size * 0.5F},
            color * alpha
        );
    }
}

void GameplaySystems::RenderHighPolyMeshes(engine::render::Renderer& renderer)
{
    if (m_highPolyMeshes.empty())
    {
        return;
    }

    // ─── Lazy GPU upload: move geometry to GPU VBOs once, then free CPU-side data ───
    if (!m_highPolyMeshesUploaded)
    {
        for (auto& mesh : m_highPolyMeshes)
        {
            if (!mesh.geometry.positions.empty())
            {
                mesh.gpuFullLod = renderer.UploadMesh(mesh.geometry, mesh.color);
                // Free CPU-side geometry data after GPU upload.
                mesh.geometry = {};
            }
            if (!mesh.mediumLodGeometry.positions.empty())
            {
                mesh.gpuMediumLod = renderer.UploadMesh(mesh.mediumLodGeometry, mesh.color * 0.96F, engine::render::MaterialParams{0.65F, 0.0F, 0.0F, false});
                mesh.mediumLodGeometry = {};
            }
        }
        m_highPolyMeshesUploaded = true;
    }

    // Frustum culling helper
    auto isVisible = [this](const glm::vec3& center, const glm::vec3& halfExtents) -> bool {
        const glm::vec3 mins = center - halfExtents;
        const glm::vec3 maxs = center + halfExtents;
        return m_frustum.IntersectsAABB(mins, maxs);
    };

    // Parallel culling - determine which meshes are visible
    const std::size_t meshCount = m_highPolyMeshes.size();
    std::vector<std::size_t> visibleMeshes;
    visibleMeshes.reserve(meshCount);

    // Use JobSystem for parallel culling if available
    auto& jobSystem = engine::core::JobSystem::Instance();
    if (jobSystem.IsInitialized() && jobSystem.IsEnabled() && meshCount > 256)
    {
        // Parallel culling using workers
        // Pre-allocate results
        std::vector<std::int8_t> visibilityFlags(meshCount, 0);
        engine::core::JobCounter cullCounter;
        
        jobSystem.ParallelFor(meshCount, 64, [&](std::size_t idx) {
            const auto& mesh = m_highPolyMeshes[idx];
            if (isVisible(mesh.position, mesh.halfExtents))
            {
                visibilityFlags[idx] = 1;
            }
        }, engine::core::JobPriority::High, &cullCounter);
        
        jobSystem.WaitForCounter(cullCounter);
        
        // Collect visible mesh indices
        for (std::size_t i = 0; i < meshCount; ++i)
        {
            if (visibilityFlags[i] == 1)
            {
                visibleMeshes.push_back(i);
            }
        }
    }
    else
    {
        // Sequential culling (fallback for small mesh counts or disabled JobSystem)
        for (std::size_t i = 0; i < meshCount; ++i)
        {
            const auto& mesh = m_highPolyMeshes[i];
            if (isVisible(mesh.position, mesh.halfExtents))
            {
                visibleMeshes.push_back(i);
            }
        }
    }

    if (visibleMeshes.empty())
    {
        return;
    }

    constexpr float kHighPolyFullDetailDistance = 72.0F;
    constexpr float kHighPolyFullDetailDistanceSq = kHighPolyFullDetailDistance * kHighPolyFullDetailDistance;
    constexpr float kHighPolyMediumDetailDistance = 140.0F;
    constexpr float kHighPolyMediumDetailDistanceSq = kHighPolyMediumDetailDistance * kHighPolyMediumDetailDistance;
    constexpr std::size_t kMaxFullDetailMeshes = 8;

    std::vector<std::pair<std::size_t, float>> sortedVisible;
    sortedVisible.reserve(visibleMeshes.size());
    for (const std::size_t idx : visibleMeshes)
    {
        const auto& mesh = m_highPolyMeshes[idx];
        const glm::vec3 toCamera = mesh.position - m_cameraPosition;
        const float distanceSq = glm::dot(toCamera, toCamera);
        sortedVisible.emplace_back(idx, distanceSq);
    }
    std::sort(sortedVisible.begin(), sortedVisible.end(), [](const auto& a, const auto& b) {
        return a.second < b.second;
    });

    // Build model matrix helper
    auto buildModelMatrix = [](const glm::vec3& position, const glm::vec3& rotation, const glm::vec3& scale) -> glm::mat4 {
        glm::mat4 model{1.0F};
        model = glm::translate(model, position);
        model = glm::rotate(model, glm::radians(rotation.y), glm::vec3{0.0F, 1.0F, 0.0F});
        model = glm::rotate(model, glm::radians(rotation.x), glm::vec3{1.0F, 0.0F, 0.0F});
        model = glm::rotate(model, glm::radians(rotation.z), glm::vec3{0.0F, 0.0F, 1.0F});
        model = glm::scale(model, scale);
        return model;
    };

    // Render visible meshes using GPU-cached draw calls
    std::size_t fullDetailDraws = 0;
    for (const auto& [idx, distanceSq] : sortedVisible)
    {
        const auto& mesh = m_highPolyMeshes[idx];
        const glm::mat4 modelMatrix = buildModelMatrix(mesh.position, mesh.rotation, mesh.scale);

        if (distanceSq <= kHighPolyFullDetailDistanceSq && fullDetailDraws < kMaxFullDetailMeshes && mesh.gpuFullLod != engine::render::Renderer::kInvalidGpuMesh)
        {
            renderer.DrawGpuMesh(mesh.gpuFullLod, modelMatrix);
            ++fullDetailDraws;
        }
        else if (distanceSq <= kHighPolyMediumDetailDistanceSq && mesh.gpuMediumLod != engine::render::Renderer::kInvalidGpuMesh)
        {
            renderer.DrawGpuMesh(mesh.gpuMediumLod, modelMatrix);
        }
        else
        {
            renderer.DrawOrientedBox(
                mesh.position,
                mesh.halfExtents,
                mesh.rotation,
                mesh.color * 0.9F,
                engine::render::MaterialParams{0.85F, 0.0F, 0.0F, false}
            );
        }
    }
}

void GameplaySystems::RenderLoopMeshes(engine::render::Renderer& renderer)
{
    if (m_loopMeshes.empty())
    {
        return;
    }

    static bool loggedOnce = false;
    if (!loggedOnce)
    {
        std::cout << "[LOOP_MESH] RenderLoopMeshes called with " << m_loopMeshes.size() << " instances\n";
        loggedOnce = true;
    }

    static engine::assets::MeshLibrary loopMeshLibrary;
    engine::assets::MeshLibrary& meshLibrary = (m_meshLibrary != nullptr) ? *m_meshLibrary : loopMeshLibrary;

    // Cache of already loaded meshes by path
    static std::unordered_map<std::string, engine::render::Renderer::GpuMeshId> gpuMeshCache;
    static std::unordered_map<std::string, glm::vec3> meshBoundsCache;
    static std::unordered_map<std::string, std::vector<engine::physics::WallBoxCollider>> meshColliderCache;

    // Lazy GPU upload - load and upload meshes once
    if (!m_loopMeshesUploaded)
    {
        std::error_code ec;
        const std::filesystem::path cwd = std::filesystem::current_path(ec);
        bool createdAnyCollision = false;

        const auto rotateYaw = [](const glm::vec3& v, float yawRadians) -> glm::vec3 {
            const float c = std::cos(yawRadians);
            const float s = std::sin(yawRadians);
            return glm::vec3{
                c * v.x + s * v.z,
                v.y,
                -s * v.x + c * v.z
            };
        };

        const auto toAxisAlignedHalfExtents = [](const glm::vec3& halfExtentsLocal, float yawRadians) -> glm::vec3 {
            const float c = std::abs(std::cos(yawRadians));
            const float s = std::abs(std::sin(yawRadians));
            return glm::vec3{
                c * halfExtentsLocal.x + s * halfExtentsLocal.z,
                halfExtentsLocal.y,
                s * halfExtentsLocal.x + c * halfExtentsLocal.z
            };
        };

        const auto createFallbackCollider = [this, &createdAnyCollision, &toAxisAlignedHalfExtents](LoopMeshInstance& instance) {
            const float yawRad = glm::radians(instance.rotationDegrees);
            const glm::vec3 fallbackHalfExtents = toAxisAlignedHalfExtents(instance.halfExtents, yawRad);

            const engine::scene::Entity entity = m_world.CreateEntity();
            m_world.Transforms()[entity] = engine::scene::Transform{
                instance.position,
                glm::vec3{0.0F, 0.0F, 0.0F},
                glm::vec3{1.0F},
                glm::vec3{0.0F, 0.0F, 1.0F},
            };
            m_world.StaticBoxes()[entity] = engine::scene::StaticBoxComponent{fallbackHalfExtents, true};
            instance.collisionCreated = true;
            createdAnyCollision = true;
        };

        const auto createGeneratedColliders = [this, &createdAnyCollision, &rotateYaw, &toAxisAlignedHalfExtents](
            LoopMeshInstance& instance,
            const std::vector<engine::physics::WallBoxCollider>& boxes
        ) {
            const float yawRad = glm::radians(instance.rotationDegrees);
            for (const auto& box : boxes)
            {
                const glm::vec3 rotatedCenter = rotateYaw(box.center, yawRad);
                const glm::vec3 worldCenter = instance.position + rotatedCenter;
                const glm::vec3 worldHalfExtents = toAxisAlignedHalfExtents(box.halfExtents, yawRad);

                const engine::scene::Entity entity = m_world.CreateEntity();
                m_world.Transforms()[entity] = engine::scene::Transform{
                    worldCenter,
                    glm::vec3{0.0F, 0.0F, 0.0F},
                    glm::vec3{1.0F},
                    glm::vec3{0.0F, 0.0F, 1.0F},
                };
                m_world.StaticBoxes()[entity] = engine::scene::StaticBoxComponent{worldHalfExtents, true};
            }

            instance.collisionCreated = true;
            createdAnyCollision = true;
        };

        for (auto& instance : m_loopMeshes)
        {
            if (instance.gpuMesh != engine::render::Renderer::kInvalidGpuMesh)
            {
                continue;  // Already loaded
            }

            // Check cache first
            auto cacheIt = gpuMeshCache.find(instance.meshPath);
            if (cacheIt != gpuMeshCache.end() && cacheIt->second != engine::render::Renderer::kInvalidGpuMesh)
            {
                instance.gpuMesh = cacheIt->second;
                instance.halfExtents = meshBoundsCache[instance.meshPath];

                // Reuse cached generated colliders (or fallback if generation failed).
                if (!instance.collisionCreated)
                {
                    const auto colliderIt = meshColliderCache.find(instance.meshPath);
                    if (colliderIt != meshColliderCache.end() && !colliderIt->second.empty())
                    {
                        createGeneratedColliders(instance, colliderIt->second);
                    }
                    else
                    {
                        createFallbackCollider(instance);
                        std::cout << "[LOOP_MESH] Created fallback collision for cached mesh: " << instance.meshPath << "\n";
                    }
                }
                continue;
            }

            // Resolve mesh path
            std::filesystem::path meshPath = cwd / instance.meshPath;
            if (!std::filesystem::exists(meshPath))
            {
                meshPath = cwd / "assets" / instance.meshPath;
            }

            std::string error;
            const engine::assets::MeshData* meshData = meshLibrary.LoadMesh(meshPath, &error);
            if (meshData == nullptr || !meshData->loaded)
            {
                std::cout << "[LOOP_MESH] Failed to load mesh from " << instance.meshPath << ": " << error << "\n";
                continue;
            }

            // Upload to GPU
            const engine::render::MaterialParams material{};
            instance.gpuMesh = renderer.UploadMesh(meshData->geometry, glm::vec3{1.0F, 1.0F, 1.0F}, material);

            // Calculate half extents from actual mesh bounds for frustum culling
            instance.halfExtents = (meshData->boundsMax - meshData->boundsMin) * 0.5F;

            // Cache for reuse
            gpuMeshCache[instance.meshPath] = instance.gpuMesh;
            meshBoundsCache[instance.meshPath] = instance.halfExtents;

            // Generate mesh collider template once per unique mesh path.
            if (!meshColliderCache.contains(instance.meshPath))
            {
                using namespace engine::physics;

                WallColliderConfig config;
                config.cellSize = 0.06F;
                config.maxBoxes = 8;
                config.padXZ = 0.03F;
                config.minIslandCells = 1;
                config.cleanup = true;
                config.maxVolumeExcess = 2.5F;
                config.minCoverage = 0.70F;

                auto result = ColliderGen_WallBoxes::Generate(
                    meshData->geometry.positions,
                    meshData->geometry.indices,
                    config
                );

                if (result.valid && !result.boxes.empty())
                {
                    meshColliderCache[instance.meshPath] = result.boxes;
                    std::cout << "[LOOP_MESH] Generated " << result.boxes.size() << " colliders for "
                              << instance.meshPath << " (coverage=" << (result.coverage * 100.0f) << "%)\n";
                }
                else
                {
                    meshColliderCache[instance.meshPath] = {};
                    std::cout << "[LOOP_MESH] Fallback to single AABB for " << instance.meshPath
                              << " (reason: " << (result.error.empty() ? "unknown" : result.error) << ")\n";
                }
            }

            if (!instance.collisionCreated)
            {
                const auto colliderIt = meshColliderCache.find(instance.meshPath);
                if (colliderIt != meshColliderCache.end() && !colliderIt->second.empty())
                {
                    createGeneratedColliders(instance, colliderIt->second);
                }
                else
                {
                    createFallbackCollider(instance);
                }
            }
        }

        if (createdAnyCollision)
        {
            m_physicsDirty = true;
        }
        m_loopMeshesUploaded = true;
    }

    // Frustum culling helper
    auto isVisible = [this](const glm::vec3& center, const glm::vec3& halfExtents) -> bool {
        const glm::vec3 mins = center - halfExtents;
        const glm::vec3 maxs = center + halfExtents;
        return m_frustum.IntersectsAABB(mins, maxs);
    };

    // Build model matrix helper
    auto buildModelMatrix = [](const glm::vec3& position, float rotationDegrees) -> glm::mat4 {
        glm::mat4 model{1.0F};
        model = glm::translate(model, position);
        model = glm::rotate(model, glm::radians(rotationDegrees), glm::vec3{0.0F, 1.0F, 0.0F});
        return model;
    };

    // Render visible loop meshes
    for (const auto& instance : m_loopMeshes)
    {
        if (instance.gpuMesh == engine::render::Renderer::kInvalidGpuMesh)
        {
            continue;
        }

        // Frustum culling
        if (!isVisible(instance.position, instance.halfExtents))
        {
            continue;
        }

        const glm::mat4 modelMatrix = buildModelMatrix(instance.position, instance.rotationDegrees);
        renderer.DrawGpuMesh(instance.gpuMesh, modelMatrix);
    }
}

bool GameplaySystems::LoadSurvivorCharacterBounds(
    const std::string& characterId,
    float* outMinY,
    float* outMaxY,
    float* outMaxAbsXZ
)
{
    if (characterId.empty())
    {
        return false;
    }
    const loadout::SurvivorCharacterDefinition* survivorDef = m_loadoutCatalog.FindSurvivor(characterId);
    if (survivorDef == nullptr || survivorDef->modelPath.empty())
    {
        return false;
    }

    static engine::assets::MeshLibrary fallbackMeshLibrary;
    engine::assets::MeshLibrary& meshLibrary = (m_meshLibrary != nullptr) ? *m_meshLibrary : fallbackMeshLibrary;
    const std::filesystem::path meshPath = ResolveAssetPathFromCwd(survivorDef->modelPath);
    std::string error;
    const engine::assets::MeshData* meshData = meshLibrary.LoadMesh(meshPath, &error);
    if (meshData == nullptr || !meshData->loaded)
    {
        std::cout << "[SURVIVOR_MODEL] Failed to load bounds for " << characterId
                  << " from " << meshPath.string() << ": " << error << "\n";
        return false;
    }

    if (outMinY != nullptr)
    {
        *outMinY = meshData->boundsMin.y;
    }
    if (outMaxY != nullptr)
    {
        *outMaxY = meshData->boundsMax.y;
    }
    if (outMaxAbsXZ != nullptr)
    {
        const float absX = std::max(std::abs(meshData->boundsMin.x), std::abs(meshData->boundsMax.x));
        const float absZ = std::max(std::abs(meshData->boundsMin.z), std::abs(meshData->boundsMax.z));
        *outMaxAbsXZ = std::max(absX, absZ);
    }
    return true;
}

bool GameplaySystems::EnsureSurvivorCharacterMeshLoaded(const std::string& characterId)
{
    if (characterId.empty())
    {
        return false;
    }

    auto cacheIt = m_survivorVisualMeshes.find(characterId);
    if (cacheIt == m_survivorVisualMeshes.end())
    {
        cacheIt = m_survivorVisualMeshes.emplace(characterId, SurvivorVisualMesh{}).first;
    }
    SurvivorVisualMesh& cached = cacheIt->second;

    if (!cached.boundsLoadAttempted)
    {
        cached.boundsLoadAttempted = true;
        float minY = 0.0F;
        float maxY = 1.8F;
        float maxAbsXZ = 0.3F;
        if (!LoadSurvivorCharacterBounds(characterId, &minY, &maxY, &maxAbsXZ))
        {
            cached.boundsLoadFailed = true;
            if (characterId == m_selectedSurvivorCharacterId)
            {
                (void)TryFallbackToAvailableSurvivorModel(characterId);
            }
            return false;
        }
        cached.boundsMinY = minY;
        cached.boundsMaxY = maxY;
        cached.maxAbsXZ = maxAbsXZ;
        cached.boundsLoaded = true;
        cached.boundsLoadFailed = false;
    }
    else if (cached.boundsLoadFailed || !cached.boundsLoaded)
    {
        return false;
    }

    if (cached.gpuMesh != engine::render::Renderer::kInvalidGpuMesh)
    {
        if (characterId == m_selectedSurvivorCharacterId && m_animationCharacterId != characterId)
        {
            (void)ReloadSurvivorCharacterAnimations(characterId);
        }
        return true;
    }
    if (m_rendererPtr == nullptr)
    {
        cached.gpuUploadAttempted = false;
        return false;
    }
    if (cached.gpuUploadAttempted)
    {
        cached.gpuUploadAttempted = false;
    }
    cached.gpuUploadAttempted = true;

    const loadout::SurvivorCharacterDefinition* survivorDef = m_loadoutCatalog.FindSurvivor(characterId);
    if (survivorDef == nullptr || survivorDef->modelPath.empty())
    {
        return false;
    }

    // Use member mesh library or create a temporary one for this load.
    engine::assets::MeshLibrary* meshLibrary = m_meshLibrary;
    engine::assets::MeshLibrary tempMeshLibrary;
    if (meshLibrary == nullptr)
    {
        meshLibrary = &tempMeshLibrary;
    }

    const std::filesystem::path meshPath = ResolveAssetPathFromCwd(survivorDef->modelPath);
    std::string error;
    const engine::assets::MeshData* meshData = meshLibrary->LoadMesh(meshPath, &error);
    if (meshData == nullptr || !meshData->loaded)
    {
        cached.gpuUploadAttempted = false;
        std::cout << "[SURVIVOR_MODEL] Failed to upload mesh for " << characterId
                  << " from " << meshPath.string() << ": " << error << "\n";
        return false;
    }

    const engine::render::MaterialParams material{};
    cached.gpuMesh = m_rendererPtr->UploadMesh(meshData->geometry, glm::vec3{1.0F, 1.0F, 1.0F}, material);
    cached.boundsMinY = meshData->boundsMin.y;
    cached.boundsMaxY = meshData->boundsMax.y;
    const float absX = std::max(std::abs(meshData->boundsMin.x), std::abs(meshData->boundsMax.x));
    const float absZ = std::max(std::abs(meshData->boundsMin.z), std::abs(meshData->boundsMax.z));
    cached.maxAbsXZ = std::max(absX, absZ);
    cached.boundsLoaded = true;
    if (cached.gpuMesh == engine::render::Renderer::kInvalidGpuMesh)
    {
        cached.gpuUploadAttempted = false;
        return false;
    }

    std::cout << "[SURVIVOR_MODEL] Uploaded mesh for " << characterId
              << " from " << meshPath.string()
              << " (" << meshData->geometry.positions.size() << " verts)\n";

    if (characterId == m_selectedSurvivorCharacterId && m_animationCharacterId != characterId)
    {
        (void)ReloadSurvivorCharacterAnimations(characterId);
    }

    return true;
}

GameplaySystems::LobbyCharacterMesh GameplaySystems::GetCharacterMeshForLobby(const std::string& characterId)
{
    LobbyCharacterMesh result{};
    if (characterId.empty()) return result;

    // Try survivor first
    const auto* survivorDef = m_loadoutCatalog.FindSurvivor(characterId);
    if (survivorDef != nullptr)
    {
        if (EnsureSurvivorCharacterMeshLoaded(characterId))
        {
            auto it = m_survivorVisualMeshes.find(characterId);
            if (it != m_survivorVisualMeshes.end())
            {
                result.gpuMesh = it->second.gpuMesh;
                result.boundsMinY = it->second.boundsMinY;
                result.boundsMaxY = it->second.boundsMaxY;
                result.maxAbsXZ = it->second.maxAbsXZ;
                result.modelYawDegrees = survivorDef->modelYawDegrees;
            }
        }
        return result;
    }

    // Try killer — reuse same mesh cache mechanism
    const auto* killerDef = m_loadoutCatalog.FindKiller(characterId);
    if (killerDef != nullptr && !killerDef->modelPath.empty())
    {
        auto cacheIt = m_survivorVisualMeshes.find(characterId);
        if (cacheIt == m_survivorVisualMeshes.end())
        {
            cacheIt = m_survivorVisualMeshes.emplace(characterId, SurvivorVisualMesh{}).first;
        }
        auto& cached = cacheIt->second;
        if (cached.gpuMesh != engine::render::Renderer::kInvalidGpuMesh)
        {
            result.gpuMesh = cached.gpuMesh;
            result.boundsMinY = cached.boundsMinY;
            result.boundsMaxY = cached.boundsMaxY;
            result.maxAbsXZ = cached.maxAbsXZ;
            result.modelYawDegrees = killerDef->modelYawDegrees;
            return result;
        }
        if (m_rendererPtr == nullptr)
        {
            cached.gpuUploadAttempted = false;
            return result;
        }
        if (cached.gpuUploadAttempted)
        {
            cached.gpuUploadAttempted = false;
        }
        cached.gpuUploadAttempted = true;
        cached.boundsLoadAttempted = true;

        engine::assets::MeshLibrary* meshLibrary = m_meshLibrary;
        engine::assets::MeshLibrary tempMeshLibrary;
        if (meshLibrary == nullptr) meshLibrary = &tempMeshLibrary;

        const std::filesystem::path meshPath = ResolveAssetPathFromCwd(killerDef->modelPath);
        std::string error;
        const engine::assets::MeshData* meshData = meshLibrary->LoadMesh(meshPath, &error);
        if (meshData == nullptr || !meshData->loaded)
        {
            cached.gpuUploadAttempted = false;
            return result;
        }
        cached.gpuMesh = m_rendererPtr->UploadMesh(meshData->geometry, glm::vec3{1.0F}, engine::render::MaterialParams{});
        cached.boundsMinY = meshData->boundsMin.y;
        cached.boundsMaxY = meshData->boundsMax.y;
        cached.maxAbsXZ = std::max(std::abs(meshData->boundsMin.x), std::abs(meshData->boundsMax.x));
        cached.maxAbsXZ = std::max(cached.maxAbsXZ, std::max(std::abs(meshData->boundsMin.z), std::abs(meshData->boundsMax.z)));
        cached.boundsLoaded = true;
        if (cached.gpuMesh != engine::render::Renderer::kInvalidGpuMesh)
        {
            result.gpuMesh = cached.gpuMesh;
            result.boundsMinY = cached.boundsMinY;
            result.boundsMaxY = cached.boundsMaxY;
            result.maxAbsXZ = cached.maxAbsXZ;
            result.modelYawDegrees = killerDef->modelYawDegrees;
            std::cout << "[LOBBY_MODEL] Loaded killer mesh: " << characterId << "\n";
        }
    }
    return result;
}

void GameplaySystems::PreloadCharacterMeshes()
{
    for (const auto& id : ListSurvivorCharacters())
    {
        (void)GetCharacterMeshForLobby(id);
    }
    for (const auto& id : ListKillerCharacters())
    {
        (void)GetCharacterMeshForLobby(id);
    }
}

bool GameplaySystems::ReloadSurvivorCharacterAnimations(const std::string& characterId)
{
    if (characterId.empty())
    {
        return false;
    }

    const loadout::SurvivorCharacterDefinition* survivorDef = m_loadoutCatalog.FindSurvivor(characterId);
    if (survivorDef == nullptr || survivorDef->modelPath.empty())
    {
        std::cout << "[ANIMATION] Failed to reload: survivor '" << characterId
                  << "' has no model path\n";
        return false;
    }

    const std::filesystem::path meshPath = ResolveAssetPathFromCwd(survivorDef->modelPath);
    std::cout << "[ANIMATION] Reloading survivor clips for " << characterId
              << " from " << meshPath.string() << "\n";

    m_animationSystem.ClearClips();
    m_animationCharacterId.clear();
    m_survivorAnimationRigs.erase(characterId);

    try
    {
        std::size_t loadedClipCount = 0;
        std::vector<std::unique_ptr<engine::animation::AnimationClip>> pendingClips;
        engine::assets::MeshLibrary animationLibrary;
        animationLibrary.SetAnimationLoadedCallback([&loadedClipCount, &pendingClips, characterId](const std::string& clipName, std::unique_ptr<engine::animation::AnimationClip> clip) {
            if (clip == nullptr)
            {
                std::cout << "[ANIMATION] Warning: null clip received for '" << clipName
                          << "' (" << characterId << ")\n";
                return;
            }
            if (!clip->Valid())
            {
                std::cout << "[ANIMATION] Warning: invalid clip '" << clip->name << "' for "
                          << characterId << " (duration=" << clip->duration << ")\n";
                return;
            }

            ++loadedClipCount;
            std::cout << "[ANIMATION] Parsed clip " << clip->name
                      << " for " << characterId
                      << " (duration=" << clip->duration << "s, rot=" << clip->rotations.size()
                      << ", pos=" << clip->translations.size() << ", scale=" << clip->scales.size() << ")\n";
            pendingClips.push_back(std::move(clip));
        });

        std::string error;
        const engine::assets::MeshData* meshData = animationLibrary.LoadMesh(meshPath, &error);
        if (meshData == nullptr || !meshData->loaded)
        {
            std::cout << "[ANIMATION] Failed to parse clips for " << characterId
                      << " from " << meshPath.string() << ": " << error << "\n";
            m_animationSystem.InitializeStateMachine();
            m_animationCharacterId = characterId;
            return false;
        }

        for (auto& clip : pendingClips)
        {
            if (clip == nullptr)
            {
                continue;
            }
            m_animationSystem.AddClip(std::move(clip));
        }

        const std::vector<std::string> loadedClips = m_animationSystem.ListClips();
        auto profile = m_animationSystem.GetProfile();
        profile.idleClipName = PickLocomotionClip(loadedClips, {"idle", "stand"}, profile.idleClipName);
        profile.walkClipName = PickLocomotionClip(loadedClips, {"walk"}, profile.walkClipName);
        profile.runClipName = PickLocomotionClip(loadedClips, {"run", "sprint", "jog"}, profile.runClipName);
        m_animationSystem.SetProfile(profile);
        m_animationSystem.InitializeStateMachine();
        m_animationCharacterId = characterId;

        std::cout << "[ANIMATION] Bound locomotion clips for " << characterId
                  << " idle='" << profile.idleClipName
                  << "' walk='" << profile.walkClipName
                  << "' run='" << profile.runClipName
                  << "' total=" << loadedClipCount << "\n";

        if (loadedClips.empty())
        {
            std::cout << "[ANIMATION] Warning: no clips loaded for " << characterId << "\n";
        }
        else if (m_animationDebugEnabled)
        {
            std::cout << "[ANIMATION] Clip list for " << characterId << ":\n";
            for (const std::string& clipName : loadedClips)
            {
                std::cout << "  - " << clipName << "\n";
            }
        }

        const bool rigLoaded = LoadSurvivorAnimationRig(characterId);
        if (!rigLoaded)
        {
            std::cout << "[ANIMATION] Warning: animation rig was not loaded for " << characterId
                      << " (clips are available but mesh skinning will stay static)\n";
        }

        return loadedClipCount > 0;
    }
    catch (const std::exception& e)
    {
        std::cout << "[ANIMATION] Exception while reloading clips for " << characterId
                  << ": " << e.what() << "\n";
    }
    catch (...)
    {
        std::cout << "[ANIMATION] Unknown exception while reloading clips for "
                  << characterId << "\n";
    }

    m_animationSystem.InitializeStateMachine();
    m_animationCharacterId = characterId;
    return false;
}

bool GameplaySystems::LoadSurvivorAnimationRig(const std::string& characterId)
{
    auto existing = m_survivorAnimationRigs.find(characterId);
    if (existing != m_survivorAnimationRigs.end() && existing->second.loaded)
    {
        return true;
    }

    const loadout::SurvivorCharacterDefinition* survivorDef = m_loadoutCatalog.FindSurvivor(characterId);
    if (survivorDef == nullptr || survivorDef->modelPath.empty())
    {
        return false;
    }

    const std::filesystem::path meshPath = ResolveAssetPathFromCwd(survivorDef->modelPath);
    tinygltf::TinyGLTF loader;
    tinygltf::Model model;
    std::string warn;
    std::string err;
    const std::string extension = ToLowerCopy(meshPath.extension().string());
    const bool loaded = (extension == ".glb")
        ? loader.LoadBinaryFromFile(&model, &err, &warn, meshPath.string())
        : loader.LoadASCIIFromFile(&model, &err, &warn, meshPath.string());
    if (!loaded)
    {
        std::cout << "[ANIMATION] Failed to load rig from " << meshPath.string() << ": " << err << "\n";
        return false;
    }
    if (!warn.empty() && m_animationDebugEnabled)
    {
        std::cout << "[ANIMATION] Rig load warning for " << characterId << ": " << warn << "\n";
    }

    SurvivorAnimationRig rig;
    if (model.nodes.empty() || model.meshes.empty())
    {
        return false;
    }

    if (!model.scenes.empty())
    {
        const int sceneIndex = (model.defaultScene >= 0 && model.defaultScene < static_cast<int>(model.scenes.size()))
            ? model.defaultScene
            : 0;
        const tinygltf::Scene& scene = model.scenes[static_cast<std::size_t>(sceneIndex)];
        rig.sceneRoots = scene.nodes;
    }
    if (rig.sceneRoots.empty())
    {
        rig.sceneRoots.reserve(model.nodes.size());
        for (int i = 0; i < static_cast<int>(model.nodes.size()); ++i)
        {
            rig.sceneRoots.push_back(i);
        }
    }

    rig.nodeParents.assign(model.nodes.size(), -1);
    for (int nodeIndex = 0; nodeIndex < static_cast<int>(model.nodes.size()); ++nodeIndex)
    {
        const tinygltf::Node& node = model.nodes[static_cast<std::size_t>(nodeIndex)];
        for (const int childIndex : node.children)
        {
            if (childIndex >= 0 && childIndex < static_cast<int>(rig.nodeParents.size()))
            {
                rig.nodeParents[static_cast<std::size_t>(childIndex)] = nodeIndex;
            }
        }
    }

    int meshNodeIndex = -1;
    int skinIndex = -1;
    int meshIndex = -1;
    std::vector<int> stack = rig.sceneRoots;
    while (!stack.empty())
    {
        const int nodeIndex = stack.back();
        stack.pop_back();
        if (nodeIndex < 0 || nodeIndex >= static_cast<int>(model.nodes.size()))
        {
            continue;
        }
        const tinygltf::Node& node = model.nodes[static_cast<std::size_t>(nodeIndex)];
        if (node.mesh >= 0 && node.mesh < static_cast<int>(model.meshes.size()))
        {
            if (meshNodeIndex < 0 || (skinIndex < 0 && node.skin >= 0))
            {
                meshNodeIndex = nodeIndex;
                meshIndex = node.mesh;
                skinIndex = node.skin;
            }
        }
        for (const int child : node.children)
        {
            stack.push_back(child);
        }
    }

    if (meshNodeIndex < 0 || meshIndex < 0 || meshIndex >= static_cast<int>(model.meshes.size()))
    {
        return false;
    }

    const tinygltf::Mesh& mesh = model.meshes[static_cast<std::size_t>(meshIndex)];
    rig.basePositions.clear();
    rig.baseNormals.clear();
    rig.baseColors.clear();
    rig.baseUvs.clear();
    rig.jointIndices.clear();
    rig.jointWeights.clear();
    rig.indices.clear();

    int combinedPrimitives = 0;
    for (const tinygltf::Primitive& primitive : mesh.primitives)
    {
        const int mode = primitive.mode == -1 ? TINYGLTF_MODE_TRIANGLES : primitive.mode;
        const bool trianglesMode =
            mode == TINYGLTF_MODE_TRIANGLES ||
            mode == TINYGLTF_MODE_TRIANGLE_STRIP ||
            mode == TINYGLTF_MODE_TRIANGLE_FAN;
        if (!trianglesMode)
        {
            continue;
        }

        const auto posIt = primitive.attributes.find("POSITION");
        if (posIt == primitive.attributes.end())
        {
            continue;
        }
        const int positionAccessorIndex = posIt->second;
        if (positionAccessorIndex < 0 || positionAccessorIndex >= static_cast<int>(model.accessors.size()))
        {
            continue;
        }

        std::vector<glm::vec3> primitivePositions;
        if (!ReadAccessorVec3FloatTiny(
                model,
                model.accessors[static_cast<std::size_t>(positionAccessorIndex)],
                &primitivePositions) ||
            primitivePositions.empty())
        {
            continue;
        }

        std::vector<glm::vec3> primitiveNormals;
        if (const auto normalIt = primitive.attributes.find("NORMAL"); normalIt != primitive.attributes.end())
        {
            const int normalAccessorIndex = normalIt->second;
            if (normalAccessorIndex >= 0 && normalAccessorIndex < static_cast<int>(model.accessors.size()))
            {
                (void)ReadAccessorVec3FloatTiny(
                    model,
                    model.accessors[static_cast<std::size_t>(normalAccessorIndex)],
                    &primitiveNormals);
            }
        }

        std::vector<glm::vec2> primitiveUvs;
        if (const auto uvIt = primitive.attributes.find("TEXCOORD_0"); uvIt != primitive.attributes.end())
        {
            const int uvAccessorIndex = uvIt->second;
            if (uvAccessorIndex >= 0 && uvAccessorIndex < static_cast<int>(model.accessors.size()))
            {
                (void)ReadAccessorVec2FloatTiny(
                    model,
                    model.accessors[static_cast<std::size_t>(uvAccessorIndex)],
                    &primitiveUvs);
            }
        }

        std::vector<glm::uvec4> primitiveJointIndices;
        if (const auto jointsIt = primitive.attributes.find("JOINTS_0"); jointsIt != primitive.attributes.end())
        {
            const int jointsAccessorIndex = jointsIt->second;
            if (jointsAccessorIndex >= 0 && jointsAccessorIndex < static_cast<int>(model.accessors.size()))
            {
                (void)ReadAccessorVec4UIntTiny(
                    model,
                    model.accessors[static_cast<std::size_t>(jointsAccessorIndex)],
                    &primitiveJointIndices);
            }
        }

        std::vector<glm::vec4> primitiveJointWeights;
        if (const auto weightsIt = primitive.attributes.find("WEIGHTS_0"); weightsIt != primitive.attributes.end())
        {
            const int weightsAccessorIndex = weightsIt->second;
            if (weightsAccessorIndex >= 0 && weightsAccessorIndex < static_cast<int>(model.accessors.size()))
            {
                (void)ReadAccessorVec4FloatTiny(
                    model,
                    model.accessors[static_cast<std::size_t>(weightsAccessorIndex)],
                    &primitiveJointWeights);
            }
        }

        glm::vec3 primitiveBaseColor{1.0F, 1.0F, 1.0F};
        if (primitive.material >= 0 && primitive.material < static_cast<int>(model.materials.size()))
        {
            const tinygltf::Material& material = model.materials[static_cast<std::size_t>(primitive.material)];
            const auto& pbr = material.pbrMetallicRoughness;
            if (pbr.baseColorFactor.size() == 4U)
            {
                primitiveBaseColor = glm::clamp(
                    glm::vec3{
                        static_cast<float>(pbr.baseColorFactor[0]),
                        static_cast<float>(pbr.baseColorFactor[1]),
                        static_cast<float>(pbr.baseColorFactor[2]),
                    },
                    glm::vec3{0.0F},
                    glm::vec3{1.0F});
            }
        }

        std::vector<std::uint32_t> primitiveIndices;
        if (primitive.indices >= 0 && primitive.indices < static_cast<int>(model.accessors.size()))
        {
            if (!ReadAccessorScalarsAsIndicesTiny(
                    model,
                    model.accessors[static_cast<std::size_t>(primitive.indices)],
                    &primitiveIndices))
            {
                continue;
            }
        }
        else
        {
            primitiveIndices.reserve(primitivePositions.size());
            for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(primitivePositions.size()); ++i)
            {
                primitiveIndices.push_back(i);
            }
        }
        if (primitiveIndices.size() < 3)
        {
            continue;
        }

        const std::uint32_t baseVertex = static_cast<std::uint32_t>(rig.basePositions.size());
        rig.basePositions.insert(rig.basePositions.end(), primitivePositions.begin(), primitivePositions.end());
        for (std::size_t i = 0; i < primitivePositions.size(); ++i)
        {
            rig.baseNormals.push_back(
                (i < primitiveNormals.size()) ? primitiveNormals[i] : glm::vec3{0.0F, 1.0F, 0.0F});
            rig.baseUvs.push_back((i < primitiveUvs.size()) ? primitiveUvs[i] : glm::vec2{0.0F});
            rig.jointIndices.push_back((i < primitiveJointIndices.size()) ? primitiveJointIndices[i] : glm::uvec4{0U});
            rig.jointWeights.push_back((i < primitiveJointWeights.size()) ? primitiveJointWeights[i] : glm::vec4{0.0F});
            rig.baseColors.push_back(primitiveBaseColor);
        }

        const std::size_t indicesBefore = rig.indices.size();
        auto appendTriangle = [&](std::uint32_t ia, std::uint32_t ib, std::uint32_t ic) {
            if (ia >= primitivePositions.size() || ib >= primitivePositions.size() || ic >= primitivePositions.size())
            {
                return;
            }
            rig.indices.push_back(baseVertex + ia);
            rig.indices.push_back(baseVertex + ib);
            rig.indices.push_back(baseVertex + ic);
        };

        if (mode == TINYGLTF_MODE_TRIANGLES)
        {
            for (std::size_t triStart = 0; triStart + 2 < primitiveIndices.size(); triStart += 3)
            {
                appendTriangle(
                    primitiveIndices[triStart],
                    primitiveIndices[triStart + 1],
                    primitiveIndices[triStart + 2]);
            }
        }
        else if (mode == TINYGLTF_MODE_TRIANGLE_STRIP)
        {
            for (std::size_t i = 2; i < primitiveIndices.size(); ++i)
            {
                const bool odd = (i % 2U) == 1U;
                const std::uint32_t a = primitiveIndices[i - 2];
                const std::uint32_t b = primitiveIndices[i - 1];
                const std::uint32_t c = primitiveIndices[i];
                appendTriangle(odd ? b : a, odd ? a : b, c);
            }
        }
        else
        {
            const std::uint32_t root = primitiveIndices.front();
            for (std::size_t i = 2; i < primitiveIndices.size(); ++i)
            {
                appendTriangle(root, primitiveIndices[i - 1], primitiveIndices[i]);
            }
        }

        if (m_animationDebugEnabled)
        {
            const std::size_t emittedTris = (rig.indices.size() - indicesBefore) / 3U;
            std::cout << "[ANIMATION] Rig primitive " << (combinedPrimitives + 1)
                      << " for " << characterId
                      << " (verts=" << primitivePositions.size()
                      << ", tris=" << emittedTris
                      << ", material=" << primitive.material
                      << ", color=(" << primitiveBaseColor.r << ", " << primitiveBaseColor.g << ", " << primitiveBaseColor.b << "))\n";
        }

        ++combinedPrimitives;
    }

    if (combinedPrimitives == 0)
    {
        return false;
    }

    rig.meshNodeIndex = meshNodeIndex;
    rig.skinIndex = skinIndex;

    rig.restTranslations.assign(model.nodes.size(), glm::vec3{0.0F});
    rig.restRotations.assign(model.nodes.size(), glm::quat{1.0F, 0.0F, 0.0F, 0.0F});
    rig.restScales.assign(model.nodes.size(), glm::vec3{1.0F});
    for (std::size_t i = 0; i < model.nodes.size(); ++i)
    {
        const tinygltf::Node& node = model.nodes[i];
        glm::vec3 translation{0.0F};
        glm::quat rotation{1.0F, 0.0F, 0.0F, 0.0F};
        glm::vec3 scale{1.0F};
        if (node.translation.size() == 3U)
        {
            translation = glm::vec3{
                static_cast<float>(node.translation[0]),
                static_cast<float>(node.translation[1]),
                static_cast<float>(node.translation[2]),
            };
        }
        if (node.rotation.size() == 4U)
        {
            rotation = glm::quat{
                static_cast<float>(node.rotation[3]),
                static_cast<float>(node.rotation[0]),
                static_cast<float>(node.rotation[1]),
                static_cast<float>(node.rotation[2]),
            };
            if (glm::length(rotation) > 1.0e-6F)
            {
                rotation = glm::normalize(rotation);
            }
        }
        if (node.scale.size() == 3U)
        {
            scale = glm::vec3{
                static_cast<float>(node.scale[0]),
                static_cast<float>(node.scale[1]),
                static_cast<float>(node.scale[2]),
            };
        }
        rig.restTranslations[i] = translation;
        rig.restRotations[i] = rotation;
        rig.restScales[i] = scale;
    }

    if (skinIndex >= 0 && skinIndex < static_cast<int>(model.skins.size()))
    {
        const tinygltf::Skin& skin = model.skins[static_cast<std::size_t>(skinIndex)];
        rig.skinJoints = skin.joints;
        rig.inverseBindMatrices.assign(rig.skinJoints.size(), glm::mat4{1.0F});
        if (skin.inverseBindMatrices >= 0 && skin.inverseBindMatrices < static_cast<int>(model.accessors.size()))
        {
            std::vector<glm::mat4> ibms;
            if (ReadAccessorMat4FloatTiny(model, model.accessors[static_cast<std::size_t>(skin.inverseBindMatrices)], &ibms))
            {
                const std::size_t count = glm::min(ibms.size(), rig.inverseBindMatrices.size());
                for (std::size_t i = 0; i < count; ++i)
                {
                    rig.inverseBindMatrices[i] = ibms[i];
                }
            }
        }
    }

    rig.loaded = !rig.basePositions.empty() && !rig.indices.empty();
    if (!rig.loaded)
    {
        return false;
    }

    m_survivorAnimationRigs[characterId] = std::move(rig);
    std::cout << "[ANIMATION] Rig loaded for " << characterId
              << " (verts=" << m_survivorAnimationRigs[characterId].basePositions.size()
              << ", tris=" << (m_survivorAnimationRigs[characterId].indices.size() / 3U)
              << ", primitives=" << combinedPrimitives
              << ", joints=" << m_survivorAnimationRigs[characterId].skinJoints.size() << ")\n";
    return true;
}

bool GameplaySystems::BuildAnimatedSurvivorGeometry(
    const std::string& characterId,
    engine::render::MeshGeometry* outGeometry,
    float* outMinY,
    float* outMaxY,
    float* outMaxAbsXZ
) const
{
    if (outGeometry == nullptr || characterId.empty())
    {
        return false;
    }

    const auto rigIt = m_survivorAnimationRigs.find(characterId);
    if (rigIt == m_survivorAnimationRigs.end() || !rigIt->second.loaded)
    {
        return false;
    }
    const SurvivorAnimationRig& rig = rigIt->second;
    if (rig.basePositions.empty() || rig.indices.empty())
    {
        return false;
    }

    const auto& blender = m_animationSystem.GetStateMachine().GetBlender();
    const auto& sourcePlayer = blender.SourcePlayer();
    const auto& targetPlayer = blender.TargetPlayer();
    if (targetPlayer.GetClip() == nullptr)
    {
        return false;
    }

    const bool blending = blender.IsBlending() && sourcePlayer.GetClip() != nullptr;
    const float blendWeight = glm::clamp(blender.BlendWeight(), 0.0F, 1.0F);

    std::vector<glm::mat4> localTransforms(rig.restTranslations.size(), glm::mat4{1.0F});
    auto samplePlayerNode = [](const engine::animation::AnimationPlayer& player,
                               int nodeIndex,
                               glm::vec3* outTranslation,
                               glm::quat* outRotation,
                               glm::vec3* outScale) {
        if (player.GetClip() == nullptr)
        {
            return;
        }
        const engine::animation::AnimationClip* clip = player.GetClip();
        if (clip->HasTranslation(nodeIndex))
        {
            player.SampleTranslation(nodeIndex, *outTranslation);
        }
        if (clip->HasRotation(nodeIndex))
        {
            player.SampleRotation(nodeIndex, *outRotation);
        }
        if (clip->HasScale(nodeIndex))
        {
            player.SampleScale(nodeIndex, *outScale);
        }
    };

    for (int nodeIndex = 0; nodeIndex < static_cast<int>(rig.restTranslations.size()); ++nodeIndex)
    {
        glm::vec3 translation = rig.restTranslations[static_cast<std::size_t>(nodeIndex)];
        glm::quat rotation = rig.restRotations[static_cast<std::size_t>(nodeIndex)];
        glm::vec3 scale = rig.restScales[static_cast<std::size_t>(nodeIndex)];

        if (blending)
        {
            glm::vec3 sourceTranslation = translation;
            glm::quat sourceRotation = rotation;
            glm::vec3 sourceScale = scale;
            samplePlayerNode(sourcePlayer, nodeIndex, &sourceTranslation, &sourceRotation, &sourceScale);

            glm::vec3 targetTranslation = translation;
            glm::quat targetRotation = rotation;
            glm::vec3 targetScale = scale;
            samplePlayerNode(targetPlayer, nodeIndex, &targetTranslation, &targetRotation, &targetScale);

            translation = glm::mix(sourceTranslation, targetTranslation, blendWeight);
            if (glm::dot(sourceRotation, targetRotation) < 0.0F)
            {
                targetRotation = -targetRotation;
            }
            rotation = glm::normalize(glm::slerp(sourceRotation, targetRotation, blendWeight));
            scale = glm::mix(sourceScale, targetScale, blendWeight);
        }
        else
        {
            samplePlayerNode(targetPlayer, nodeIndex, &translation, &rotation, &scale);
        }

        localTransforms[static_cast<std::size_t>(nodeIndex)] =
            glm::translate(glm::mat4{1.0F}, translation) * glm::mat4_cast(rotation) * glm::scale(glm::mat4{1.0F}, scale);
    }

    std::vector<glm::mat4> worldTransforms(localTransforms.size(), glm::mat4{1.0F});
    std::vector<std::uint8_t> solved(localTransforms.size(), 0U);
    std::function<void(int)> computeWorld = [&](int nodeIndex) {
        if (nodeIndex < 0 || nodeIndex >= static_cast<int>(localTransforms.size()))
        {
            return;
        }
        if (solved[static_cast<std::size_t>(nodeIndex)] != 0U)
        {
            return;
        }
        const int parent = (nodeIndex < static_cast<int>(rig.nodeParents.size()))
            ? rig.nodeParents[static_cast<std::size_t>(nodeIndex)]
            : -1;
        if (parent >= 0)
        {
            computeWorld(parent);
            worldTransforms[static_cast<std::size_t>(nodeIndex)] =
                worldTransforms[static_cast<std::size_t>(parent)] * localTransforms[static_cast<std::size_t>(nodeIndex)];
        }
        else
        {
            worldTransforms[static_cast<std::size_t>(nodeIndex)] = localTransforms[static_cast<std::size_t>(nodeIndex)];
        }
        solved[static_cast<std::size_t>(nodeIndex)] = 1U;
    };
    for (int nodeIndex = 0; nodeIndex < static_cast<int>(localTransforms.size()); ++nodeIndex)
    {
        computeWorld(nodeIndex);
    }

    if (rig.meshNodeIndex < 0 || rig.meshNodeIndex >= static_cast<int>(worldTransforms.size()))
    {
        return false;
    }
    const glm::mat4 meshWorld = worldTransforms[static_cast<std::size_t>(rig.meshNodeIndex)];
    const glm::mat4 invMeshWorld = glm::inverse(meshWorld);
    const glm::mat3 normalWorld = glm::inverseTranspose(glm::mat3(meshWorld));

    std::vector<glm::mat4> skinMatrices(rig.skinJoints.size(), glm::mat4{1.0F});
    for (std::size_t i = 0; i < rig.skinJoints.size(); ++i)
    {
        const int jointNode = rig.skinJoints[i];
        if (jointNode < 0 || jointNode >= static_cast<int>(worldTransforms.size()))
        {
            continue;
        }
        const glm::mat4 ibm = i < rig.inverseBindMatrices.size() ? rig.inverseBindMatrices[i] : glm::mat4{1.0F};
        skinMatrices[i] = invMeshWorld * worldTransforms[static_cast<std::size_t>(jointNode)] * ibm;
    }

    outGeometry->positions.resize(rig.basePositions.size());
    outGeometry->normals.resize(rig.basePositions.size(), glm::vec3{0.0F, 1.0F, 0.0F});
    outGeometry->colors = rig.baseColors;
    if (outGeometry->colors.size() != rig.basePositions.size())
    {
        outGeometry->colors.assign(rig.basePositions.size(), glm::vec3{1.0F, 1.0F, 1.0F});
    }
    outGeometry->uvs = rig.baseUvs;
    if (outGeometry->uvs.size() != rig.basePositions.size())
    {
        outGeometry->uvs.assign(rig.basePositions.size(), glm::vec2{0.0F});
    }
    outGeometry->indices = rig.indices;

    glm::vec3 boundsMin{std::numeric_limits<float>::max()};
    glm::vec3 boundsMax{-std::numeric_limits<float>::max()};
    const bool canSkin = !skinMatrices.empty() &&
        rig.jointIndices.size() == rig.basePositions.size() &&
        rig.jointWeights.size() == rig.basePositions.size();

    for (std::size_t vertexIndex = 0; vertexIndex < rig.basePositions.size(); ++vertexIndex)
    {
        glm::vec3 skinnedPosition = rig.basePositions[vertexIndex];
        glm::vec3 skinnedNormal = vertexIndex < rig.baseNormals.size() ? rig.baseNormals[vertexIndex] : glm::vec3{0.0F, 1.0F, 0.0F};

        if (canSkin)
        {
            const glm::uvec4 joints = rig.jointIndices[vertexIndex];
            const glm::vec4 weights = rig.jointWeights[vertexIndex];
            glm::vec3 accumPosition{0.0F};
            glm::vec3 accumNormal{0.0F};
            float weightSum = 0.0F;
            for (int k = 0; k < 4; ++k)
            {
                const float weight = weights[static_cast<std::size_t>(k)];
                if (weight <= 1.0e-6F)
                {
                    continue;
                }
                const std::uint32_t jointIndex = joints[static_cast<std::size_t>(k)];
                if (jointIndex >= skinMatrices.size())
                {
                    continue;
                }
                const glm::mat4& jointMat = skinMatrices[static_cast<std::size_t>(jointIndex)];
                accumPosition += weight * glm::vec3(jointMat * glm::vec4(rig.basePositions[vertexIndex], 1.0F));
                accumNormal += weight * glm::mat3(jointMat) * skinnedNormal;
                weightSum += weight;
            }
            if (weightSum > 1.0e-6F)
            {
                skinnedPosition = accumPosition;
                if (glm::length(accumNormal) > 1.0e-6F)
                {
                    skinnedNormal = glm::normalize(accumNormal);
                }
            }
        }

        const glm::vec3 worldPosition = glm::vec3(meshWorld * glm::vec4(skinnedPosition, 1.0F));
        glm::vec3 worldNormal = normalWorld * skinnedNormal;
        if (glm::length(worldNormal) > 1.0e-6F)
        {
            worldNormal = glm::normalize(worldNormal);
        }
        else
        {
            worldNormal = glm::vec3{0.0F, 1.0F, 0.0F};
        }

        outGeometry->positions[vertexIndex] = worldPosition;
        outGeometry->normals[vertexIndex] = worldNormal;
        boundsMin = glm::min(boundsMin, worldPosition);
        boundsMax = glm::max(boundsMax, worldPosition);
    }

    const glm::vec3 center = (boundsMin + boundsMax) * 0.5F;
    for (glm::vec3& pos : outGeometry->positions)
    {
        pos -= center;
    }
    const glm::vec3 centeredMin = boundsMin - center;
    const glm::vec3 centeredMax = boundsMax - center;

    if (outMinY != nullptr)
    {
        *outMinY = centeredMin.y;
    }
    if (outMaxY != nullptr)
    {
        *outMaxY = centeredMax.y;
    }
    if (outMaxAbsXZ != nullptr)
    {
        const float absX = std::max(std::abs(centeredMin.x), std::abs(centeredMax.x));
        const float absZ = std::max(std::abs(centeredMin.z), std::abs(centeredMax.z));
        *outMaxAbsXZ = std::max(absX, absZ);
    }

    return true;
}

void GameplaySystems::RefreshAnimatedSurvivorMeshIfNeeded(const std::string& characterId)
{
    if (m_rendererPtr == nullptr || characterId.empty())
    {
        return;
    }
    if (characterId != m_animationCharacterId)
    {
        return;
    }
    if (!LoadSurvivorAnimationRig(characterId))
    {
        return;
    }

    auto cacheIt = m_survivorVisualMeshes.find(characterId);
    if (cacheIt == m_survivorVisualMeshes.end())
    {
        return;
    }
    SurvivorVisualMesh& cached = cacheIt->second;

    engine::render::MeshGeometry animatedGeometry;
    float minY = cached.boundsMinY;
    float maxY = cached.boundsMaxY;
    float maxAbsXZ = cached.maxAbsXZ;
    if (!BuildAnimatedSurvivorGeometry(characterId, &animatedGeometry, &minY, &maxY, &maxAbsXZ))
    {
        return;
    }

    if (cached.gpuMesh != engine::render::Renderer::kInvalidGpuMesh)
    {
        m_rendererPtr->FreeGpuMesh(cached.gpuMesh);
    }
    cached.gpuMesh = m_rendererPtr->UploadMesh(animatedGeometry, glm::vec3{1.0F, 1.0F, 1.0F});
    cached.boundsMinY = minY;
    cached.boundsMaxY = maxY;
    cached.maxAbsXZ = maxAbsXZ;
    cached.gpuUploadAttempted = cached.gpuMesh != engine::render::Renderer::kInvalidGpuMesh;

    auto rigIt = m_survivorAnimationRigs.find(characterId);
    if (rigIt != m_survivorAnimationRigs.end() &&
        !rigIt->second.runtimeUploadLogged &&
        cached.gpuMesh != engine::render::Renderer::kInvalidGpuMesh)
    {
        const auto* currentClip = m_animationSystem.GetStateMachine().GetBlender().GetCurrentClip();
        std::cout << "[ANIMATION] Runtime animated mesh upload active for " << characterId
                  << " (clip=" << (currentClip != nullptr ? currentClip->name : std::string{"none"}) << ")\n";
        rigIt->second.runtimeUploadLogged = true;
    }
}

bool GameplaySystems::TryFallbackToAvailableSurvivorModel(const std::string& failedCharacterId)
{
    if (m_selectedSurvivorCharacterId != failedCharacterId)
    {
        return false;
    }

    const auto isUsableModel = [&](const std::string& characterId) {
        const loadout::SurvivorCharacterDefinition* def = m_loadoutCatalog.FindSurvivor(characterId);
        if (def == nullptr || def->modelPath.empty())
        {
            return false;
        }
        const auto cacheIt = m_survivorVisualMeshes.find(characterId);
        if (cacheIt != m_survivorVisualMeshes.end() && cacheIt->second.boundsLoadFailed)
        {
            return false;
        }
        std::error_code ec;
        const std::filesystem::path meshPath = ResolveAssetPathFromCwd(def->modelPath);
        return std::filesystem::exists(meshPath, ec) && std::filesystem::is_regular_file(meshPath, ec);
    };

    std::vector<std::string> candidates;
    candidates.reserve(8);
    candidates.push_back("survivor_male_blocky");
    candidates.push_back("survivor_female_blocky");
    for (const std::string& id : m_loadoutCatalog.ListSurvivorIds())
    {
        if (id != failedCharacterId)
        {
            candidates.push_back(id);
        }
    }

    for (const std::string& candidate : candidates)
    {
        if (candidate == failedCharacterId)
        {
            continue;
        }
        if (!isUsableModel(candidate))
        {
            continue;
        }

        m_selectedSurvivorCharacterId = candidate;
        m_animationCharacterId.clear();
        RefreshSurvivorModelCapsuleOverride();
        ApplyGameplayTuning(m_tuning);
        AddRuntimeMessage("Survivor model fallback: " + failedCharacterId + " -> " + candidate, 3.0F);
        std::cout << "[SURVIVOR_MODEL] Fallback to " << candidate
                  << " because " << failedCharacterId << " model is unavailable\n";
        return true;
    }

    AddRuntimeMessage("Survivor model unavailable: " + failedCharacterId + " (using capsule fallback)", 3.0F);
    return false;
}

void GameplaySystems::RefreshSurvivorModelCapsuleOverride()
{
    m_survivorCapsuleOverrideRadius = -1.0F;
    m_survivorCapsuleOverrideHeight = -1.0F;

    if (m_selectedSurvivorCharacterId.empty())
    {
        return;
    }

    auto cacheIt = m_survivorVisualMeshes.find(m_selectedSurvivorCharacterId);
    if (cacheIt == m_survivorVisualMeshes.end())
    {
        cacheIt = m_survivorVisualMeshes.emplace(m_selectedSurvivorCharacterId, SurvivorVisualMesh{}).first;
    }
    SurvivorVisualMesh& cached = cacheIt->second;

    if (!cached.boundsLoadAttempted)
    {
        cached.boundsLoadAttempted = true;
        float minY = 0.0F;
        float maxY = 0.0F;
        float maxAbsXZ = 0.0F;
        if (!LoadSurvivorCharacterBounds(m_selectedSurvivorCharacterId, &minY, &maxY, &maxAbsXZ))
        {
            cached.boundsLoadFailed = true;
            (void)TryFallbackToAvailableSurvivorModel(m_selectedSurvivorCharacterId);
            return;
        }
        cached.boundsMinY = minY;
        cached.boundsMaxY = maxY;
        cached.maxAbsXZ = maxAbsXZ;
        cached.boundsLoaded = true;
        cached.boundsLoadFailed = false;
    }

    if (cached.boundsLoadFailed || !cached.boundsLoaded)
    {
        return;
    }

    const float tunedHeight = glm::clamp(m_tuning.survivorCapsuleHeight, 0.9F, 3.2F);
    const float tunedRadius = glm::clamp(m_tuning.survivorCapsuleRadius, 0.2F, 1.2F);
    const float modelHeight = std::max(0.9F, (cached.boundsMaxY - cached.boundsMinY) * 0.98F);
    const float modelRadius = std::max(0.2F, cached.maxAbsXZ * 0.70F);

    // Gameplay tuning values are authoritative for hitbox size.
    const float height = tunedHeight;
    const float radius = tunedRadius;
    m_survivorCapsuleOverrideHeight = height;
    m_survivorCapsuleOverrideRadius = radius;
    if (m_animationDebugEnabled)
    {
        std::cout << "[SURVIVOR_MODEL] Capsule override for " << m_selectedSurvivorCharacterId
                  << " radius=" << radius << " height=" << height
                  << " (modelRadius=" << modelRadius << ", modelHeight=" << modelHeight
                  << ", tunedRadius=" << tunedRadius << ", tunedHeight=" << tunedHeight << ")\n";
    }
}

void GameplaySystems::InitializeLoadoutCatalog()
{
    if (!m_loadoutCatalog.Initialize("assets"))
    {
        AddRuntimeMessage("Loadout catalog init failed", 2.0F);
        return;
    }

    const std::vector<std::string> survivorIds = m_loadoutCatalog.ListSurvivorIds();
    if (m_selectedSurvivorCharacterId.empty())
    {
        if (m_loadoutCatalog.FindSurvivor("survivor_dwight") != nullptr)
        {
            m_selectedSurvivorCharacterId = "survivor_dwight";
        }
        else if (m_loadoutCatalog.FindSurvivor("survivor_male_blocky") != nullptr)
        {
            m_selectedSurvivorCharacterId = "survivor_male_blocky";
        }
    }
    if (!survivorIds.empty() && !m_loadoutCatalog.FindSurvivor(m_selectedSurvivorCharacterId))
    {
        m_selectedSurvivorCharacterId = survivorIds.front();
    }

    const std::vector<std::string> killerIds = m_loadoutCatalog.ListKillerIds();
    if (!killerIds.empty() && !m_loadoutCatalog.FindKiller(m_selectedKillerCharacterId))
    {
        m_selectedKillerCharacterId = killerIds.front();
    }

    if (m_survivorLoadout.itemId.empty())
    {
        const std::vector<std::string> itemIds = m_loadoutCatalog.ListItemIds();
        if (!itemIds.empty())
        {
            m_survivorLoadout.itemId = itemIds.front();
        }
    }

    if (m_killerLoadout.powerId.empty())
    {
        if (const auto* killerDef = m_loadoutCatalog.FindKiller(m_selectedKillerCharacterId))
        {
            m_killerLoadout.powerId = killerDef->powerId;
        }
        if (m_killerLoadout.powerId.empty())
        {
            const std::vector<std::string> powerIds = m_loadoutCatalog.ListPowerIds();
            if (!powerIds.empty())
            {
                m_killerLoadout.powerId = powerIds.front();
            }
        }
    }

    RefreshSurvivorModelCapsuleOverride();
    RefreshLoadoutModifiers();
    ResetItemAndPowerRuntimeState();
}

void GameplaySystems::RefreshLoadoutModifiers()
{
    m_survivorItemModifiers.Build(
        loadout::TargetKind::Item,
        m_survivorLoadout.itemId,
        {m_survivorLoadout.addonAId, m_survivorLoadout.addonBId},
        m_loadoutCatalog.Addons()
    );
    m_killerPowerModifiers.Build(
        loadout::TargetKind::Power,
        m_killerLoadout.powerId,
        {m_killerLoadout.addonAId, m_killerLoadout.addonBId},
        m_loadoutCatalog.Addons()
    );
}

void GameplaySystems::ResetItemAndPowerRuntimeState()
{
    m_survivorItemState = SurvivorItemRuntimeState{};
    m_killerPowerState = KillerPowerRuntimeState{};
    m_mapRevealGenerators.clear();
    m_trapIndicatorText.clear();
    m_trapIndicatorTimer = 0.0F;
    m_trapIndicatorDanger = true;
    m_trapPreviewActive = false;
    m_trapPreviewValid = true;

    const loadout::ItemDefinition* itemDef = m_loadoutCatalog.FindItem(m_survivorLoadout.itemId);
    if (itemDef != nullptr)
    {
        auto resolveRuntimeMaxCharges = [&](const loadout::ItemDefinition& def) {
            float baseMax = def.maxCharges;
            if (def.id == "toolbox")
            {
                baseMax = m_tuning.toolboxCharges;
            }
            else if (def.id == "flashlight")
            {
                baseMax = m_tuning.flashlightMaxUseSeconds;
            }
            else if (def.id == "map")
            {
                baseMax = static_cast<float>(m_tuning.mapUses);
            }
            return std::max(0.0F, m_survivorItemModifiers.ApplyStat("max_charges", baseMax));
        };
        const float maxCharges = resolveRuntimeMaxCharges(*itemDef);
        m_survivorItemState.charges = maxCharges;
        if (itemDef->id == "flashlight")
        {
            m_survivorItemState.flashlightBatterySeconds = maxCharges > 0.0F ? maxCharges : m_tuning.flashlightMaxUseSeconds;
        }
        if (itemDef->id == "map")
        {
            m_survivorItemState.mapUsesRemaining = std::max(0, static_cast<int>(std::round(std::min(maxCharges, static_cast<float>(m_tuning.mapUses)))));
        }
    }

    const loadout::PowerDefinition* powerDef = m_loadoutCatalog.FindPower(m_killerLoadout.powerId);
    if (powerDef != nullptr && powerDef->id == "bear_trap")
    {
        m_killerPowerState.trapperMaxCarryTraps = std::max(1, m_tuning.trapperMaxCarryTraps);
        m_killerPowerState.trapperCarriedTraps = std::max(0, std::min(m_tuning.trapperStartCarryTraps, m_killerPowerState.trapperMaxCarryTraps));
    }
    if (powerDef != nullptr && powerDef->id == "hatchet_throw")
    {
        m_killerPowerState.hatchetCount = static_cast<int>(m_killerPowerModifiers.ApplyStat(
            "max_count", static_cast<float>(m_tuning.hatchetMaxCount)));
        m_killerPowerState.hatchetMaxCount = static_cast<int>(m_killerPowerModifiers.ApplyStat(
            "max_count", static_cast<float>(m_tuning.hatchetMaxCount)));
        m_killerPowerState.hatchetChargeTimer = 0.0F;
        m_killerPowerState.hatchetCharging = false;
        m_killerPowerState.hatchetCharge01 = 0.0F;
        m_killerPowerState.hatchetThrowRequiresRelease = false;
        m_killerPowerState.lockerReplenishTimer = 0.0F;
        m_killerPowerState.lockerReplenishing = false;
        m_killerPowerState.lockerTargetEntity = 0;
    }
}

bool GameplaySystems::SetSurvivorItemLoadout(const std::string& itemId, const std::string& addonAId, const std::string& addonBId)
{
    if (!itemId.empty() && m_loadoutCatalog.FindItem(itemId) == nullptr)
    {
        return false;
    }

    auto validateAddon = [&](const std::string& addonId) {
        if (addonId.empty())
        {
            return true;
        }
        const loadout::AddonDefinition* addon = m_loadoutCatalog.FindAddon(addonId);
        return addon != nullptr && addon->AppliesTo(loadout::TargetKind::Item, itemId);
    };
    if (!validateAddon(addonAId) || !validateAddon(addonBId))
    {
        return false;
    }

    m_survivorLoadout.itemId = itemId;
    m_survivorLoadout.addonAId = addonAId;
    m_survivorLoadout.addonBId = addonBId;
    RefreshLoadoutModifiers();
    ResetItemAndPowerRuntimeState();
    ItemPowerLog(
        "Set survivor loadout item=" + (itemId.empty() ? std::string{"none"} : itemId) +
        " addonA=" + (addonAId.empty() ? std::string{"none"} : addonAId) +
        " addonB=" + (addonBId.empty() ? std::string{"none"} : addonBId)
    );
    return true;
}

bool GameplaySystems::SetKillerPowerLoadout(const std::string& powerId, const std::string& addonAId, const std::string& addonBId)
{
    if (!powerId.empty() && m_loadoutCatalog.FindPower(powerId) == nullptr)
    {
        return false;
    }

    auto validateAddon = [&](const std::string& addonId) {
        if (addonId.empty())
        {
            return true;
        }
        const loadout::AddonDefinition* addon = m_loadoutCatalog.FindAddon(addonId);
        return addon != nullptr && addon->AppliesTo(loadout::TargetKind::Power, powerId);
    };
    if (!validateAddon(addonAId) || !validateAddon(addonBId))
    {
        return false;
    }

    m_killerLoadout.powerId = powerId;
    m_killerLoadout.addonAId = addonAId;
    m_killerLoadout.addonBId = addonBId;
    RefreshLoadoutModifiers();
    ResetItemAndPowerRuntimeState();
    ItemPowerLog(
        "Set killer loadout power=" + (powerId.empty() ? std::string{"none"} : powerId) +
        " addonA=" + (addonAId.empty() ? std::string{"none"} : addonAId) +
        " addonB=" + (addonBId.empty() ? std::string{"none"} : addonBId)
    );
    return true;
}

std::string GameplaySystems::ItemDump() const
{
    std::ostringstream oss;
    oss << "SurvivorItem\n";
    oss << "  character=" << m_selectedSurvivorCharacterId << "\n";
    oss << "  item=" << (m_survivorLoadout.itemId.empty() ? "none" : m_survivorLoadout.itemId) << "\n";
    oss << "  addon_a=" << (m_survivorLoadout.addonAId.empty() ? "none" : m_survivorLoadout.addonAId) << "\n";
    oss << "  addon_b=" << (m_survivorLoadout.addonBId.empty() ? "none" : m_survivorLoadout.addonBId) << "\n";
    oss << "  charges=" << m_survivorItemState.charges << "\n";
    oss << "  active=" << (m_survivorItemState.active ? "true" : "false") << "\n";
    const auto activeAddons = m_survivorItemModifiers.ActiveAddonIds();
    oss << "  active_modifiers=";
    if (activeAddons.empty())
    {
        oss << "none";
    }
    else
    {
        for (std::size_t i = 0; i < activeAddons.size(); ++i)
        {
            if (i > 0)
            {
                oss << ",";
            }
            oss << activeAddons[i];
        }
    }
    oss << "\n";
    return oss.str();
}

std::string GameplaySystems::PowerDump() const
{
    std::ostringstream oss;
    oss << "KillerPower\n";
    oss << "  character=" << m_selectedKillerCharacterId << "\n";
    oss << "  power=" << (m_killerLoadout.powerId.empty() ? "none" : m_killerLoadout.powerId) << "\n";
    oss << "  addon_a=" << (m_killerLoadout.addonAId.empty() ? "none" : m_killerLoadout.addonAId) << "\n";
    oss << "  addon_b=" << (m_killerLoadout.addonBId.empty() ? "none" : m_killerLoadout.addonBId) << "\n";
    oss << "  active_traps=" << m_world.BearTraps().size() << "\n";
    oss << "  carried_traps=" << m_killerPowerState.trapperCarriedTraps << "\n";
    oss << "  wraith_cloaked=" << (m_killerPowerState.wraithCloaked ? "true" : "false") << "\n";
    oss << "  wraith_transition=" << m_killerPowerState.wraithTransitionTimer << "\n";
    oss << "  wraith_post_uncloak=" << m_killerPowerState.wraithPostUncloakTimer << "\n";
    const auto activeAddons = m_killerPowerModifiers.ActiveAddonIds();
    oss << "  active_modifiers=";
    if (activeAddons.empty())
    {
        oss << "none";
    }
    else
    {
        for (std::size_t i = 0; i < activeAddons.size(); ++i)
        {
            if (i > 0)
            {
                oss << ",";
            }
            oss << activeAddons[i];
        }
    }
    oss << "\n";
    return oss.str();
}

bool GameplaySystems::SetSelectedSurvivorCharacter(const std::string& characterId)
{
    if (m_loadoutCatalog.FindSurvivor(characterId) == nullptr)
    {
        return false;
    }
    const std::string previousCharacterId = m_selectedSurvivorCharacterId;
    const bool changed = previousCharacterId != characterId;
    m_selectedSurvivorCharacterId = characterId;
    if (changed)
    {
        m_animationCharacterId.clear();
        if (!previousCharacterId.empty())
        {
            m_survivorAnimationRigs.erase(previousCharacterId);
        }
        m_survivorAnimationRigs.erase(characterId);
    }
    RefreshSurvivorModelCapsuleOverride();
    ApplyGameplayTuning(m_tuning);
    const bool meshLoaded = EnsureSurvivorCharacterMeshLoaded(m_selectedSurvivorCharacterId);
    if (meshLoaded)
    {
        std::cout << "[SURVIVOR_MODEL] Selected " << characterId
                  << " with " << m_animationSystem.ListClips().size() << " loaded clips\n";
    }
    else
    {
        std::cout << "[SURVIVOR_MODEL] Selection applied but mesh load pending/failed for "
                  << characterId << "\n";
    }
    return true;
}

bool GameplaySystems::ReloadSelectedSurvivorCharacter(bool reloadAnimations)
{
    if (m_selectedSurvivorCharacterId.empty())
    {
        return false;
    }
    m_survivorAnimationRigs.erase(m_selectedSurvivorCharacterId);

    auto cacheIt = m_survivorVisualMeshes.find(m_selectedSurvivorCharacterId);
    if (cacheIt != m_survivorVisualMeshes.end())
    {
        if (m_rendererPtr != nullptr && cacheIt->second.gpuMesh != engine::render::Renderer::kInvalidGpuMesh)
        {
            m_rendererPtr->FreeGpuMesh(cacheIt->second.gpuMesh);
        }
        cacheIt->second = SurvivorVisualMesh{};
    }
    else
    {
        m_survivorVisualMeshes.emplace(m_selectedSurvivorCharacterId, SurvivorVisualMesh{});
    }

    if (reloadAnimations)
    {
        (void)ReloadSurvivorCharacterAnimations(m_selectedSurvivorCharacterId);
    }
    else if (m_animationCharacterId == m_selectedSurvivorCharacterId)
    {
        m_animationCharacterId.clear();
    }

    const bool loaded = EnsureSurvivorCharacterMeshLoaded(m_selectedSurvivorCharacterId);
    std::cout << "[SURVIVOR_MODEL] Reload "
              << (loaded ? "succeeded" : "failed")
              << " for " << m_selectedSurvivorCharacterId
              << " (clips=" << m_animationSystem.ListClips().size() << ")\n";
    return loaded;
}

bool GameplaySystems::ReloadSelectedSurvivorAnimations()
{
    if (m_selectedSurvivorCharacterId.empty())
    {
        return false;
    }

    const bool loaded = ReloadSurvivorCharacterAnimations(m_selectedSurvivorCharacterId);
    std::cout << "[ANIMATION] Reload "
              << (loaded ? "succeeded" : "failed")
              << " for selected survivor " << m_selectedSurvivorCharacterId << "\n";
    return loaded;
}

bool GameplaySystems::SetSelectedKillerCharacter(const std::string& characterId)
{
    const loadout::KillerCharacterDefinition* def = m_loadoutCatalog.FindKiller(characterId);
    if (def == nullptr)
    {
        return false;
    }
    m_selectedKillerCharacterId = characterId;
    if (!def->powerId.empty())
    {
        m_killerLoadout.powerId = def->powerId;
        RefreshLoadoutModifiers();
    }
    return true;
}

std::vector<std::string> GameplaySystems::ListSurvivorCharacters() const
{
    return m_loadoutCatalog.ListSurvivorIds();
}

std::vector<std::string> GameplaySystems::ListKillerCharacters() const
{
    return m_loadoutCatalog.ListKillerIds();
}

std::vector<std::string> GameplaySystems::ListItemIds() const
{
    return m_loadoutCatalog.ListItemIds();
}

std::vector<std::string> GameplaySystems::ListPowerIds() const
{
    return m_loadoutCatalog.ListPowerIds();
}

bool GameplaySystems::SpawnGroundItemDebug(const std::string& itemId, float charges)
{
    if (itemId.empty())
    {
        return false;
    }
    const loadout::ItemDefinition* itemDef = m_loadoutCatalog.FindItem(itemId);
    if (itemDef == nullptr)
    {
        return false;
    }

    engine::scene::Entity anchor = ControlledEntity();
    if (anchor == 0)
    {
        anchor = (m_survivor != 0) ? m_survivor : m_killer;
    }
    const auto anchorTransformIt = m_world.Transforms().find(anchor);
    if (anchorTransformIt == m_world.Transforms().end())
    {
        return false;
    }

    glm::vec3 forward = anchorTransformIt->second.forward;
    forward.y = 0.0F;
    if (glm::length(forward) <= 1.0e-4F)
    {
        forward = glm::vec3{0.0F, 0.0F, -1.0F};
    }
    else
    {
        forward = glm::normalize(forward);
    }

    glm::vec3 spawnPos = anchorTransformIt->second.position + forward * 1.55F;
    const glm::vec3 rayStart = spawnPos + glm::vec3{0.0F, 2.0F, 0.0F};
    const glm::vec3 rayEnd = rayStart + glm::vec3{0.0F, -8.0F, 0.0F};
    if (const std::optional<engine::physics::RaycastHit> hit = m_physics.RaycastNearest(rayStart, rayEnd))
    {
        spawnPos.y = hit->position.y + 0.05F;
    }
    else
    {
        spawnPos.y = anchorTransformIt->second.position.y;
    }

    float baseCharges = itemDef->maxCharges;
    if (itemDef->id == "toolbox")
    {
        baseCharges = m_tuning.toolboxCharges;
    }
    else if (itemDef->id == "flashlight")
    {
        baseCharges = m_tuning.flashlightMaxUseSeconds;
    }
    else if (itemDef->id == "map")
    {
        baseCharges = static_cast<float>(m_tuning.mapUses);
    }
    const float resolvedCharges = charges >= 0.0F ? charges : baseCharges;

    const engine::scene::Entity itemEntity = SpawnGroundItemEntity(itemId, spawnPos, std::max(0.0F, resolvedCharges));
    if (itemEntity == 0)
    {
        return false;
    }
    AddRuntimeMessage("Spawned item: " + itemId, 1.0F);
    ItemPowerLog(
        "Debug spawned ground item id=" + itemId +
        " charges=" + std::to_string(resolvedCharges) +
        " entity=" + std::to_string(itemEntity)
    );
    return true;
}

engine::scene::Entity GameplaySystems::SpawnGroundItemEntity(
    const std::string& itemId,
    const glm::vec3& position,
    float charges,
    const std::string& addonAId,
    const std::string& addonBId,
    bool respawnTag
)
{
    if (itemId.empty())
    {
        return 0;
    }

    const engine::scene::Entity entity = m_world.CreateEntity();
    m_world.Transforms()[entity] = engine::scene::Transform{
        position,
        glm::vec3{0.0F},
        glm::vec3{1.0F},
        glm::vec3{0.0F, 0.0F, 1.0F},
    };

    engine::scene::GroundItemComponent groundItem;
    groundItem.itemId = itemId;
    groundItem.charges = charges;
    groundItem.addonAId = addonAId;
    groundItem.addonBId = addonBId;
    groundItem.pickupEnabled = true;
    groundItem.respawnTag = respawnTag;
    m_world.GroundItems()[entity] = groundItem;
    m_world.Names()[entity] = engine::scene::NameComponent{"ground_item_" + itemId};
    return entity;
}

engine::scene::Entity GameplaySystems::FindNearestGroundItem(const glm::vec3& fromPosition, float radiusMeters) const
{
    engine::scene::Entity bestEntity = 0;
    float bestDistance = std::max(0.01F, radiusMeters);
    for (const auto& [entity, groundItem] : m_world.GroundItems())
    {
        if (!groundItem.pickupEnabled)
        {
            continue;
        }
        const auto transformIt = m_world.Transforms().find(entity);
        if (transformIt == m_world.Transforms().end())
        {
            continue;
        }
        if (std::abs(transformIt->second.position.y - fromPosition.y) > 2.0F)
        {
            continue;
        }
        const float distance = DistanceXZ(transformIt->second.position, fromPosition);
        if (distance <= bestDistance)
        {
            bestDistance = distance;
            bestEntity = entity;
        }
    }
    return bestEntity;
}

void GameplaySystems::ApplySurvivorItemActionLock(float durationSeconds)
{
    if (m_survivor == 0 || durationSeconds <= 0.0F)
    {
        return;
    }
    m_survivorItemState.actionLockTimer = std::max(m_survivorItemState.actionLockTimer, durationSeconds);
    ItemPowerLog("Survivor action lock " + std::to_string(durationSeconds) + "s");
}

bool GameplaySystems::TryDropSurvivorItemToGround()
{
    if (m_survivor == 0 || m_survivorLoadout.itemId.empty())
    {
        return false;
    }

    const auto survivorTransformIt = m_world.Transforms().find(m_survivor);
    if (survivorTransformIt == m_world.Transforms().end())
    {
        return false;
    }

    glm::vec3 forward = survivorTransformIt->second.forward;
    forward.y = 0.0F;
    if (glm::length(forward) <= 1.0e-4F)
    {
        forward = glm::vec3{0.0F, 0.0F, -1.0F};
    }
    else
    {
        forward = glm::normalize(forward);
    }

    glm::vec3 dropPos = survivorTransformIt->second.position + forward * 0.9F;
    const glm::vec3 rayStart = dropPos + glm::vec3{0.0F, 2.0F, 0.0F};
    const glm::vec3 rayEnd = rayStart + glm::vec3{0.0F, -8.0F, 0.0F};
    if (const std::optional<engine::physics::RaycastHit> hit = m_physics.RaycastNearest(rayStart, rayEnd))
    {
        dropPos.y = hit->position.y + 0.05F;
    }
    else
    {
        dropPos.y = survivorTransformIt->second.position.y;
    }

    const float droppedCharges = m_survivorItemState.charges;
    SpawnGroundItemEntity(
        m_survivorLoadout.itemId,
        dropPos,
        droppedCharges,
        m_survivorLoadout.addonAId,
        m_survivorLoadout.addonBId,
        false
    );

    const std::string droppedItem = m_survivorLoadout.itemId;
    m_survivorLoadout.itemId.clear();
    m_survivorLoadout.addonAId.clear();
    m_survivorLoadout.addonBId.clear();
    m_survivorItemState = SurvivorItemRuntimeState{};
    ApplySurvivorItemActionLock(0.5F);
    RefreshLoadoutModifiers();
    AddRuntimeMessage("Dropped item: " + droppedItem, 1.1F);
    ItemPowerLog("Survivor dropped item id=" + droppedItem + " charges=" + std::to_string(droppedCharges));
    return true;
}

bool GameplaySystems::TryPickupSurvivorGroundItem()
{
    if (m_survivor == 0 || !m_survivorLoadout.itemId.empty())
    {
        if (m_survivor != 0 && !m_survivorLoadout.itemId.empty())
        {
            AddRuntimeMessage("Drop current item first (R)", 0.9F);
        }
        return false;
    }

    const auto survivorTransformIt = m_world.Transforms().find(m_survivor);
    if (survivorTransformIt == m_world.Transforms().end())
    {
        return false;
    }

    const engine::scene::Entity itemEntity = FindNearestGroundItem(survivorTransformIt->second.position, 2.2F);
    if (itemEntity == 0)
    {
        return false;
    }

    const auto itemIt = m_world.GroundItems().find(itemEntity);
    if (itemIt == m_world.GroundItems().end())
    {
        return false;
    }

    m_survivorLoadout.itemId = itemIt->second.itemId;
    m_survivorLoadout.addonAId = itemIt->second.addonAId;
    m_survivorLoadout.addonBId = itemIt->second.addonBId;
    RefreshLoadoutModifiers();
    m_survivorItemState = SurvivorItemRuntimeState{};
    if (const loadout::ItemDefinition* itemDef = m_loadoutCatalog.FindItem(m_survivorLoadout.itemId))
    {
        float baseMax = itemDef->maxCharges;
        if (itemDef->id == "toolbox")
        {
            baseMax = m_tuning.toolboxCharges;
        }
        else if (itemDef->id == "flashlight")
        {
            baseMax = m_tuning.flashlightMaxUseSeconds;
        }
        else if (itemDef->id == "map")
        {
            baseMax = static_cast<float>(m_tuning.mapUses);
        }
        const float maxCharges = std::max(0.0F, m_survivorItemModifiers.ApplyStat("max_charges", baseMax));
        const float requestedCharges = itemIt->second.charges > 0.0F ? itemIt->second.charges : maxCharges;
        m_survivorItemState.charges = glm::clamp(requestedCharges, 0.0F, maxCharges);
        if (itemDef->id == "flashlight")
        {
            m_survivorItemState.flashlightBatterySeconds = m_survivorItemState.charges;
        }
        if (itemDef->id == "map")
        {
            m_survivorItemState.mapUsesRemaining = std::max(
                0,
                static_cast<int>(std::round(std::min(m_survivorItemState.charges, static_cast<float>(m_tuning.mapUses))))
            );
        }
    }

    const std::string picked = m_survivorLoadout.itemId;
    DestroyEntity(itemEntity);
    ApplySurvivorItemActionLock(0.5F);
    AddRuntimeMessage("Picked up item: " + picked, 1.1F);
    ItemPowerLog("Survivor picked item id=" + picked + " charges=" + std::to_string(m_survivorItemState.charges));
    return true;
}

bool GameplaySystems::TrySwapSurvivorGroundItem()
{
    if (m_survivor == 0 || m_survivorLoadout.itemId.empty())
    {
        return false;
    }

    const auto survivorTransformIt = m_world.Transforms().find(m_survivor);
    if (survivorTransformIt == m_world.Transforms().end())
    {
        return false;
    }

    const engine::scene::Entity itemEntity = FindNearestGroundItem(survivorTransformIt->second.position, 2.2F);
    if (itemEntity == 0)
    {
        return false;
    }

    auto itemIt = m_world.GroundItems().find(itemEntity);
    if (itemIt == m_world.GroundItems().end())
    {
        return false;
    }
    if (itemIt->second.itemId.empty())
    {
        return false;
    }

    const std::string equippedItem = m_survivorLoadout.itemId;
    const std::string equippedAddonA = m_survivorLoadout.addonAId;
    const std::string equippedAddonB = m_survivorLoadout.addonBId;
    const float equippedCharges = m_survivorItemState.charges;

    const std::string groundItem = itemIt->second.itemId;
    const std::string groundAddonA = itemIt->second.addonAId;
    const std::string groundAddonB = itemIt->second.addonBId;
    const float groundCharges = itemIt->second.charges;

    m_survivorLoadout.itemId = groundItem;
    m_survivorLoadout.addonAId = groundAddonA;
    m_survivorLoadout.addonBId = groundAddonB;
    RefreshLoadoutModifiers();
    m_survivorItemState = SurvivorItemRuntimeState{};
    if (const loadout::ItemDefinition* itemDef = m_loadoutCatalog.FindItem(m_survivorLoadout.itemId))
    {
        float baseMax = itemDef->maxCharges;
        if (itemDef->id == "toolbox")
        {
            baseMax = m_tuning.toolboxCharges;
        }
        else if (itemDef->id == "flashlight")
        {
            baseMax = m_tuning.flashlightMaxUseSeconds;
        }
        else if (itemDef->id == "map")
        {
            baseMax = static_cast<float>(m_tuning.mapUses);
        }
        const float maxCharges = std::max(0.0F, m_survivorItemModifiers.ApplyStat("max_charges", baseMax));
        const float resolvedCharges = groundCharges > 0.0F ? groundCharges : maxCharges;
        m_survivorItemState.charges = glm::clamp(resolvedCharges, 0.0F, maxCharges);
        if (itemDef->id == "flashlight")
        {
            m_survivorItemState.flashlightBatterySeconds = m_survivorItemState.charges;
        }
        if (itemDef->id == "map")
        {
            m_survivorItemState.mapUsesRemaining = std::max(
                0,
                static_cast<int>(std::round(std::min(m_survivorItemState.charges, static_cast<float>(m_tuning.mapUses))))
            );
        }
    }

    itemIt->second.itemId = equippedItem;
    itemIt->second.addonAId = equippedAddonA;
    itemIt->second.addonBId = equippedAddonB;
    itemIt->second.charges = std::max(0.0F, equippedCharges);
    itemIt->second.pickupEnabled = true;

    ApplySurvivorItemActionLock(0.5F);
    m_interactBufferRemaining[RoleToIndex(engine::scene::Role::Survivor)] = 0.0F;
    AddRuntimeMessage("Swapped item: " + equippedItem + " <-> " + groundItem, 1.2F);
    ItemPowerLog(
        "Survivor swapped item equipped=" + equippedItem +
        " ground=" + groundItem +
        " newCharges=" + std::to_string(m_survivorItemState.charges)
    );
    return true;
}

bool GameplaySystems::RespawnItemsNearPlayer(float radiusMeters)
{
    const engine::scene::Entity anchorEntity = ControlledEntity() != 0 ? ControlledEntity() : m_survivor;
    const auto anchorTransformIt = m_world.Transforms().find(anchorEntity);
    if (anchorTransformIt == m_world.Transforms().end())
    {
        return false;
    }

    const glm::vec3 center = anchorTransformIt->second.position;
    const std::array<std::string, 4> itemIds{"medkit", "toolbox", "flashlight", "map"};
    int spawned = 0;
    for (std::size_t i = 0; i < itemIds.size(); ++i)
    {
        const loadout::ItemDefinition* itemDef = m_loadoutCatalog.FindItem(itemIds[i]);
        if (itemDef == nullptr)
        {
            continue;
        }
        float baseCharges = itemDef->maxCharges;
        if (itemDef->id == "toolbox")
        {
            baseCharges = m_tuning.toolboxCharges;
        }
        else if (itemDef->id == "flashlight")
        {
            baseCharges = m_tuning.flashlightMaxUseSeconds;
        }
        else if (itemDef->id == "map")
        {
            baseCharges = static_cast<float>(m_tuning.mapUses);
        }
        const float angle = (2.0F * kPi) * (static_cast<float>(i) / static_cast<float>(itemIds.size()));
        glm::vec3 pos = center + glm::vec3{std::cos(angle) * radiusMeters, 0.0F, std::sin(angle) * radiusMeters};
        const glm::vec3 rayStart = pos + glm::vec3{0.0F, 2.0F, 0.0F};
        const glm::vec3 rayEnd = rayStart + glm::vec3{0.0F, -8.0F, 0.0F};
        if (const std::optional<engine::physics::RaycastHit> hit = m_physics.RaycastNearest(rayStart, rayEnd))
        {
            pos.y = hit->position.y + 0.05F;
        }
        else
        {
            pos.y = center.y;
        }

        SpawnGroundItemEntity(itemIds[i], pos, std::max(0.0F, baseCharges), std::string{}, std::string{}, true);
        ++spawned;
    }

    if (spawned > 0)
    {
        AddRuntimeMessage("Respawned base items near player", 1.5F);
        ItemPowerLog("Respawned base items near player count=" + std::to_string(spawned));
    }
    return spawned > 0;
}

void GameplaySystems::SpawnInitialTrapperGroundTraps()
{
    if (m_killerLoadout.powerId != "bear_trap" || m_killer == 0)
    {
        return;
    }

    m_killerPowerState.trapperMaxCarryTraps = std::max(1, m_tuning.trapperMaxCarryTraps);
    m_killerPowerState.trapperCarriedTraps = std::max(
        0,
        std::min(m_tuning.trapperStartCarryTraps, m_killerPowerState.trapperMaxCarryTraps)
    );

    if (m_tuning.trapperGroundSpawnTraps <= 0)
    {
        return;
    }

    const auto killerTransformIt = m_world.Transforms().find(m_killer);
    const glm::vec3 killerPos = killerTransformIt != m_world.Transforms().end() ? killerTransformIt->second.position : glm::vec3{0.0F};
    const glm::vec3 killerForward = killerTransformIt != m_world.Transforms().end() ? killerTransformIt->second.forward : glm::vec3{0.0F, 0.0F, -1.0F};

    std::vector<glm::vec3> genericSpawnPoints;
    genericSpawnPoints.reserve(m_spawnPoints.size());
    for (const SpawnPointInfo& spawn : m_spawnPoints)
    {
        if (spawn.type == SpawnPointType::Generic)
        {
            genericSpawnPoints.push_back(spawn.position);
        }
    }
    if (genericSpawnPoints.empty())
    {
        genericSpawnPoints.push_back(killerPos);
    }

    for (int i = 0; i < m_tuning.trapperGroundSpawnTraps; ++i)
    {
        const glm::vec3 base = genericSpawnPoints[static_cast<std::size_t>(i % static_cast<int>(genericSpawnPoints.size()))];
        const float angle = static_cast<float>(i) * 0.79F;
        const glm::vec3 offset{std::cos(angle) * 1.25F, 0.0F, std::sin(angle) * 1.25F};
        const engine::scene::Entity trapEntity = SpawnBearTrap(base + offset, killerForward, false);
        auto trapIt = m_world.BearTraps().find(trapEntity);
        if (trapIt != m_world.BearTraps().end())
        {
            trapIt->second.state = engine::scene::TrapState::Disarmed;
            trapIt->second.trappedEntity = 0;
            trapIt->second.escapeAttempts = 0;
        }
    }
    ItemPowerLog(
        "Trapper initial traps spawned carry=" + std::to_string(m_killerPowerState.trapperCarriedTraps) +
        " ground=" + std::to_string(m_tuning.trapperGroundSpawnTraps)
    );
}

bool GameplaySystems::TryFindNearestTrap(
    const glm::vec3& fromPosition,
    float radiusMeters,
    bool requireDisarmed,
    engine::scene::Entity* outTrapEntity
) const
{
    if (outTrapEntity != nullptr)
    {
        *outTrapEntity = 0;
    }

    float best = radiusMeters;
    engine::scene::Entity bestEntity = 0;
    for (const auto& [entity, trap] : m_world.BearTraps())
    {
        if (requireDisarmed && trap.state != engine::scene::TrapState::Disarmed)
        {
            continue;
        }
        const auto transformIt = m_world.Transforms().find(entity);
        if (transformIt == m_world.Transforms().end())
        {
            continue;
        }
        const float distance = DistanceXZ(fromPosition, transformIt->second.position);
        if (distance <= best)
        {
            best = distance;
            bestEntity = entity;
        }
    }

    if (bestEntity == 0)
    {
        return false;
    }
    if (outTrapEntity != nullptr)
    {
        *outTrapEntity = bestEntity;
    }
    return true;
}

bool GameplaySystems::ComputeTrapPlacementPreview(glm::vec3* outPosition, glm::vec3* outHalfExtents, bool* outValid) const
{
    if (outPosition != nullptr)
    {
        *outPosition = glm::vec3{0.0F};
    }
    if (outHalfExtents != nullptr)
    {
        *outHalfExtents = glm::vec3{0.36F, 0.08F, 0.36F};
    }
    if (outValid != nullptr)
    {
        *outValid = false;
    }

    if (m_killer == 0 || m_killerLoadout.powerId != "bear_trap")
    {
        return false;
    }

    const auto killerTransformIt = m_world.Transforms().find(m_killer);
    if (killerTransformIt == m_world.Transforms().end())
    {
        return false;
    }

    const loadout::PowerDefinition* powerDef = m_loadoutCatalog.FindPower(m_killerLoadout.powerId);
    const auto readParam = [&](const std::string& key, float fallback) {
        if (powerDef == nullptr)
        {
            return fallback;
        }
        const auto it = powerDef->params.find(key);
        return it != powerDef->params.end() ? it->second : fallback;
    };

    glm::vec3 halfExtents{
        std::max(0.12F, m_killerPowerModifiers.ApplyStat("trap_half_x", readParam("trap_half_x", 0.36F))),
        std::max(0.03F, m_killerPowerModifiers.ApplyStat("trap_half_y", readParam("trap_half_y", 0.08F))),
        std::max(0.12F, m_killerPowerModifiers.ApplyStat("trap_half_z", readParam("trap_half_z", 0.36F))),
    };

    glm::vec3 forward = killerTransformIt->second.forward;
    if (glm::length(forward) > 1.0e-5F)
    {
        forward = glm::normalize(glm::vec3{forward.x, 0.0F, forward.z});
    }
    else
    {
        forward = glm::vec3{0.0F, 0.0F, -1.0F};
    }

    glm::vec3 previewPos = killerTransformIt->second.position + forward * 1.55F;
    const glm::vec3 rayStart = previewPos + glm::vec3{0.0F, 2.2F, 0.0F};
    const glm::vec3 rayEnd = rayStart + glm::vec3{0.0F, -8.0F, 0.0F};
    if (const std::optional<engine::physics::RaycastHit> hit = m_physics.RaycastNearest(rayStart, rayEnd))
    {
        previewPos.y = hit->position.y + 0.05F;
    }
    else
    {
        previewPos.y = 0.05F;
    }

    bool valid = true;
    for (const auto& [entity, trap] : m_world.BearTraps())
    {
        const auto trapTransformIt = m_world.Transforms().find(entity);
        if (trapTransformIt == m_world.Transforms().end())
        {
            continue;
        }
        const glm::vec2 deltaXZ{
            previewPos.x - trapTransformIt->second.position.x,
            previewPos.z - trapTransformIt->second.position.z
        };
        const float minDist = std::max(halfExtents.x, halfExtents.z) + std::max(trap.halfExtents.x, trap.halfExtents.z) + 0.05F;
        if (glm::length(deltaXZ) <= minDist)
        {
            valid = false;
            break;
        }
    }

    if (outPosition != nullptr)
    {
        *outPosition = previewPos;
    }
    if (outHalfExtents != nullptr)
    {
        *outHalfExtents = halfExtents;
    }
    if (outValid != nullptr)
    {
        *outValid = valid;
    }
    return true;
}

void GameplaySystems::UpdateSurvivorItemSystem(const RoleCommand& survivorCommand, float fixedDt)
{
    if (fixedDt <= 0.0F)
    {
        return;
    }

    if (m_survivorItemState.cooldown > 0.0F)
    {
        m_survivorItemState.cooldown = std::max(0.0F, m_survivorItemState.cooldown - fixedDt);
    }
    if (m_survivorItemState.mapRevealTtl > 0.0F)
    {
        m_survivorItemState.mapRevealTtl = std::max(0.0F, m_survivorItemState.mapRevealTtl - fixedDt);
    }
    if (m_survivorItemState.actionLockTimer > 0.0F)
    {
        m_survivorItemState.actionLockTimer = std::max(0.0F, m_survivorItemState.actionLockTimer - fixedDt);
    }
    if (m_survivorItemState.flashlightSuccessFlashTimer > 0.0F)
    {
        m_survivorItemState.flashlightSuccessFlashTimer = std::max(0.0F, m_survivorItemState.flashlightSuccessFlashTimer - fixedDt);
    }

    std::vector<engine::scene::Entity> revealExpired;
    revealExpired.reserve(m_mapRevealGenerators.size());
    for (auto& [generatorEntity, ttl] : m_mapRevealGenerators)
    {
        ttl = std::max(0.0F, ttl - fixedDt);
        if (ttl <= 0.0F)
        {
            revealExpired.push_back(generatorEntity);
        }
    }
    for (engine::scene::Entity generatorEntity : revealExpired)
    {
        m_mapRevealGenerators.erase(generatorEntity);
    }

    if (m_survivorItemState.actionLockTimer <= 0.0F &&
        survivorCommand.dropItemPressed &&
        (m_survivorState == SurvivorHealthState::Healthy || m_survivorState == SurvivorHealthState::Injured || m_survivorState == SurvivorHealthState::Downed))
    {
        (void)TryDropSurvivorItemToGround();
    }
    if (m_survivorItemState.actionLockTimer <= 0.0F &&
        survivorCommand.pickupItemPressed &&
        (m_survivorState == SurvivorHealthState::Healthy || m_survivorState == SurvivorHealthState::Injured || m_survivorState == SurvivorHealthState::Downed))
    {
        (void)TryPickupSurvivorGroundItem();
    }
    if (m_survivorItemState.actionLockTimer <= 0.0F &&
        survivorCommand.interactPressed &&
        !m_survivorLoadout.itemId.empty() &&
        (m_survivorState == SurvivorHealthState::Healthy || m_survivorState == SurvivorHealthState::Injured || m_survivorState == SurvivorHealthState::Downed))
    {
        (void)TrySwapSurvivorGroundItem();
    }

    const loadout::ItemDefinition* itemDef = m_loadoutCatalog.FindItem(m_survivorLoadout.itemId);
    if (itemDef == nullptr)
    {
        m_survivorItemState.active = false;
        return;
    }

    float baseMaxCharges = itemDef->maxCharges;
    if (itemDef->id == "toolbox")
    {
        baseMaxCharges = m_tuning.toolboxCharges;
    }
    else if (itemDef->id == "flashlight")
    {
        baseMaxCharges = m_tuning.flashlightMaxUseSeconds;
    }
    else if (itemDef->id == "map")
    {
        baseMaxCharges = static_cast<float>(m_tuning.mapUses);
    }
    const float maxCharges = std::max(0.0F, m_survivorItemModifiers.ApplyStat("max_charges", baseMaxCharges));
    m_survivorItemState.charges = glm::clamp(m_survivorItemState.charges, 0.0F, maxCharges);

    const bool useHeld = survivorCommand.useAltHeld;
    const auto readParam = [&](const std::string& key, float fallback) {
        const auto it = itemDef->params.find(key);
        return it != itemDef->params.end() ? it->second : fallback;
    };

    if (itemDef->id == "medkit")
    {
        m_survivorItemState.active = false;
        if (m_survivorState != SurvivorHealthState::Injured || !useHeld || m_survivorItemState.charges <= 0.0F)
        {
            return;
        }

        const float baseHealRate = 1.0F / std::max(1.0F, m_tuning.healDurationSeconds);
        const float healMultiplier = std::max(0.1F, m_survivorItemModifiers.ApplyStat(
            "heal_speed_multiplier",
            readParam("heal_speed_multiplier", m_tuning.medkitHealSpeedMultiplier)
        ));
        const float healRate = std::max(0.0F, m_survivorItemModifiers.ApplyStat("heal_per_second", baseHealRate * healMultiplier));
        const float fullHealCharges = std::max(1.0F, m_survivorItemModifiers.ApplyStat(
            "full_heal_charges",
            readParam("full_heal_charges", m_tuning.medkitFullHealCharges)
        ));
        const float chargeRate = std::max(0.05F, m_survivorItemModifiers.ApplyStat("charge_per_second", healRate * fullHealCharges));
        const float consumed = std::min(m_survivorItemState.charges, chargeRate * fixedDt);
        m_survivorItemState.active = true;
        m_survivorItemState.charges = std::max(0.0F, m_survivorItemState.charges - consumed);
        m_selfHealProgress = glm::clamp(m_selfHealProgress + consumed / fullHealCharges, 0.0F, 1.0F);
        if (m_selfHealProgress >= 1.0F)
        {
            m_selfHealProgress = 0.0F;
            SetSurvivorState(SurvivorHealthState::Healthy, "Medkit heal");
            AddRuntimeMessage("Medkit heal completed", 1.1F);
            ItemPowerLog("Medkit heal completed");
        }
        return;
    }

    if (itemDef->id == "toolbox")
    {
        m_survivorItemState.active = false;
        if (!useHeld || m_survivorItemState.charges <= 0.0F)
        {
            return;
        }

        if (m_activeRepairGenerator == 0 && m_survivor != 0)
        {
            engine::scene::Entity bestGenerator = 0;
            float bestDistance = std::numeric_limits<float>::max();
            const auto survivorTransformIt = m_world.Transforms().find(m_survivor);
            if (survivorTransformIt != m_world.Transforms().end())
            {
                for (const auto& [generatorEntity, _] : m_world.Generators())
                {
                    const auto genTransformIt = m_world.Transforms().find(generatorEntity);
                    if (genTransformIt == m_world.Transforms().end())
                    {
                        continue;
                    }
                    const float castDistance = DistanceXZ(survivorTransformIt->second.position, genTransformIt->second.position);
                    InteractionCandidate candidate = BuildGeneratorRepairCandidate(
                        m_survivor,
                        generatorEntity,
                        castDistance
                    );
                    if (candidate.type != InteractionType::RepairGenerator)
                    {
                        continue;
                    }
                    if (castDistance < bestDistance)
                    {
                        bestDistance = castDistance;
                        bestGenerator = generatorEntity;
                    }
                }
            }

            if (bestGenerator != 0)
            {
                BeginOrContinueGeneratorRepair(bestGenerator);
                ItemPowerLog("Toolbox auto-attached to generator entity=" + std::to_string(bestGenerator));
            }
        }

        if (m_activeRepairGenerator == 0)
        {
            return;
        }

        auto generatorIt = m_world.Generators().find(m_activeRepairGenerator);
        if (generatorIt == m_world.Generators().end() || generatorIt->second.completed)
        {
            return;
        }

        const float baseRate = 1.0F / std::max(1.0F, m_tuning.generatorRepairSecondsBase);
        const float repairBonus = std::max(0.0F, m_survivorItemModifiers.ApplyStat(
            "repair_speed_bonus",
            readParam("repair_speed_bonus", m_tuning.toolboxRepairSpeedBonus)
        )) * baseRate;
        const float chargeRate = std::max(0.05F, m_survivorItemModifiers.ApplyStat(
            "charge_per_second",
            readParam("charge_per_second", m_tuning.toolboxChargeDrainPerSecond)
        ));
        m_survivorItemState.active = true;
        const float consumed = std::min(m_survivorItemState.charges, chargeRate * fixedDt);
        m_survivorItemState.charges = std::max(0.0F, m_survivorItemState.charges - consumed);
        const float consumeScale = (chargeRate * fixedDt) > 1.0e-5F ? consumed / (chargeRate * fixedDt) : 0.0F;
        generatorIt->second.progress = glm::clamp(generatorIt->second.progress + repairBonus * fixedDt * consumeScale, 0.0F, 1.0F);
        if (generatorIt->second.progress >= 1.0F)
        {
            generatorIt->second.progress = 1.0F;
            generatorIt->second.completed = true;
            RefreshGeneratorsCompleted();
            AddRuntimeMessage("Generator completed with toolbox bonus", 1.2F);
            ItemPowerLog("Toolbox completed generator with bonus");
            StopGeneratorRepair();
        }
        return;
    }

    if (itemDef->id == "flashlight")
    {
        m_survivorItemState.active = false;
        if (!useHeld || m_survivorItemState.charges <= 0.0F)
        {
            m_survivorItemState.flashBlindAccum = std::max(0.0F, m_survivorItemState.flashBlindAccum - fixedDt * 0.45F);
            return;
        }

        const auto survivorTransformIt = m_world.Transforms().find(m_survivor);
        const auto killerTransformIt = m_world.Transforms().find(m_killer);
        const auto killerActorIt = m_world.Actors().find(m_killer);
        if (survivorTransformIt == m_world.Transforms().end() ||
            killerTransformIt == m_world.Transforms().end() ||
            killerActorIt == m_world.Actors().end())
        {
            return;
        }

        const float beamRange = std::max(2.0F, m_survivorItemModifiers.ApplyStat("beam_range", readParam("beam_range", m_tuning.flashlightBeamRange)));
        const float beamAngleDeg = std::max(5.0F, m_survivorItemModifiers.ApplyStat("beam_angle_deg", readParam("beam_angle_deg", m_tuning.flashlightBeamAngleDegrees)));
        const float blindNeed = std::max(0.25F, m_survivorItemModifiers.ApplyStat("blind_time_required", readParam("blind_time_required", m_tuning.flashlightBlindBuildSeconds)));
        const float blindDuration = std::max(0.2F, m_survivorItemModifiers.ApplyStat("blind_duration", readParam("blind_duration", m_tuning.flashlightBlindDurationSeconds)));
        const float chargeRate = std::max(0.05F, m_survivorItemModifiers.ApplyStat("charge_per_second", readParam("charge_per_second", 1.0F)));

        const glm::vec3 toKiller = killerTransformIt->second.position - survivorTransformIt->second.position;
        const float dist = glm::length(glm::vec2{toKiller.x, toKiller.z});
        bool inCone = false;
        bool intoFace = false;
        if (dist <= beamRange && dist > 1.0e-4F)
        {
            glm::vec3 dirToKiller{toKiller.x, 0.0F, toKiller.z};
            dirToKiller = glm::normalize(dirToKiller);
            glm::vec3 forward{survivorTransformIt->second.forward.x, 0.0F, survivorTransformIt->second.forward.z};
            if (glm::length(forward) > 1.0e-4F)
            {
                forward = glm::normalize(forward);
                const float dot = glm::dot(forward, dirToKiller);
                inCone = dot >= std::cos(glm::radians(beamAngleDeg * 0.5F));
                glm::vec3 killerFacing{killerTransformIt->second.forward.x, 0.0F, killerTransformIt->second.forward.z};
                if (glm::length(killerFacing) > 1.0e-4F)
                {
                    killerFacing = glm::normalize(killerFacing);
                    const float faceDot = glm::dot(killerFacing, -dirToKiller);
                    intoFace = faceDot >= std::cos(glm::radians(45.0F));
                }
            }
        }

        m_survivorItemState.active = true;
        const float consumed = std::min(m_survivorItemState.charges, chargeRate * fixedDt);
        m_survivorItemState.charges = std::max(0.0F, m_survivorItemState.charges - consumed);
        if (inCone && intoFace)
        {
            m_survivorItemState.flashBlindAccum += fixedDt;
        }
        else
        {
            m_survivorItemState.flashBlindAccum = std::max(0.0F, m_survivorItemState.flashBlindAccum - fixedDt * 0.6F);
        }

        if (m_survivorItemState.flashBlindAccum >= blindNeed)
        {
            killerActorIt->second.stunTimer = std::max(killerActorIt->second.stunTimer, 0.2F);
            m_killerLookLight.enabled = false;
            m_killerPowerState.killerBlindTimer = std::max(m_killerPowerState.killerBlindTimer, blindDuration);
            m_survivorItemState.flashBlindAccum = 0.0F;
            m_survivorItemState.flashlightSuccessFlashTimer = std::max(m_survivorItemState.flashlightSuccessFlashTimer, 0.18F);
            AddRuntimeMessage("Flashlight blind", 1.0F);
            ItemPowerLog("Flashlight blind applied duration=" + std::to_string(blindDuration));
        }
        return;
    }

    if (itemDef->id == "map")
    {
        m_survivorItemState.active = false;
        if (m_survivorItemState.mapUsesRemaining <= 0 && m_survivorItemState.charges > 0.0F)
        {
            m_survivorItemState.mapUsesRemaining = std::max(0, static_cast<int>(std::round(m_survivorItemState.charges)));
        }
        if (!useHeld || m_survivorItemState.charges <= 0.0F || m_survivorItemState.mapUsesRemaining <= 0)
        {
            m_survivorItemState.mapChannelSeconds = 0.0F;
            return;
        }

        const float chargePerUse = std::max(0.1F, m_survivorItemModifiers.ApplyStat("charge_per_use", readParam("charge_per_use", 1.0F)));
        const float channelSeconds = std::max(0.05F, m_survivorItemModifiers.ApplyStat("channel_seconds", readParam("channel_seconds", m_tuning.mapChannelSeconds)));
        const float revealRadius = std::max(4.0F, m_survivorItemModifiers.ApplyStat("reveal_radius", readParam("reveal_radius", m_tuning.mapRevealRangeMeters)));
        const float revealDuration = std::max(0.2F, m_survivorItemModifiers.ApplyStat("reveal_duration", readParam("reveal_duration", m_tuning.mapRevealDurationSeconds)));

        if (m_survivorItemState.charges < chargePerUse || m_survivorItemState.mapUsesRemaining <= 0)
        {
            AddRuntimeMessage("Map: not enough charges", 1.0F);
            return;
        }

        if (m_survivorItemState.cooldown > 0.0F)
        {
            return;
        }

        m_survivorItemState.active = true;
        m_survivorItemState.mapChannelSeconds += fixedDt;
        if (m_survivorItemState.mapChannelSeconds < channelSeconds)
        {
            return;
        }
        m_survivorItemState.mapChannelSeconds = 0.0F;
        m_survivorItemState.cooldown = 0.15F;
        m_survivorItemState.charges = std::max(0.0F, m_survivorItemState.charges - chargePerUse);
        m_survivorItemState.mapUsesRemaining = std::max(0, m_survivorItemState.mapUsesRemaining - 1);
        m_survivorItemState.mapRevealTtl = revealDuration;
        m_mapRevealGenerators.clear();

        int visibleGenerators = 0;
        const auto survivorTransformIt = m_world.Transforms().find(m_survivor);
        if (survivorTransformIt != m_world.Transforms().end())
        {
            for (const auto& [entity, generator] : m_world.Generators())
            {
                if (generator.completed)
                {
                    continue;
                }
                const auto generatorTransformIt = m_world.Transforms().find(entity);
                if (generatorTransformIt == m_world.Transforms().end())
                {
                    continue;
                }
                if (DistanceXZ(survivorTransformIt->second.position, generatorTransformIt->second.position) <= revealRadius)
                {
                    ++visibleGenerators;
                    m_mapRevealGenerators[entity] = revealDuration;
                }
            }
        }

        AddRuntimeMessage("Map reveal: generators " + std::to_string(visibleGenerators), 1.2F);
        ItemPowerLog("Map reveal used generators=" + std::to_string(visibleGenerators));
        return;
    }
}

void GameplaySystems::UpdateKillerPowerSystem(const RoleCommand& killerCommand, float fixedDt)
{
    if (m_killerPowerState.killerBlindTimer > 0.0F)
    {
        m_killerPowerState.killerBlindTimer = std::max(0.0F, m_killerPowerState.killerBlindTimer - fixedDt);
        if (m_killerPowerState.killerBlindTimer > 0.0F)
        {
            m_killerLookLight.enabled = false;
        }
        else
        {
            m_killerLookLight.enabled = true;
        }
    }

    const loadout::PowerDefinition* powerDef = m_loadoutCatalog.FindPower(m_killerLoadout.powerId);
    if (powerDef == nullptr || m_killer == 0)
    {
        m_trapPreviewActive = false;
        return;
    }

    if (powerDef->id == "wraith_cloak")
    {
        m_trapPreviewActive = false;
        UpdateWraithPowerSystem(killerCommand, fixedDt);
        return;
    }

    if (powerDef->id == "hatchet_throw")
    {
        m_trapPreviewActive = false;
        UpdateHatchetPowerSystem(killerCommand, fixedDt);
        return;
    }

    if (powerDef->id == "chainsaw_sprint")
    {
        m_trapPreviewActive = false;
        UpdateChainsawSprintPowerSystem(killerCommand, fixedDt);
        return;
    }

    if (powerDef->id == "nurse_blink")
    {
        m_trapPreviewActive = false;
        UpdateNurseBlinkPowerSystem(killerCommand, fixedDt);
        return;
    }

    if (powerDef->id != "bear_trap")
    {
        m_trapPreviewActive = false;
        return;
    }

    m_killerPowerState.trapperMaxCarryTraps = std::max(
        1,
        static_cast<int>(std::round(m_killerPowerModifiers.ApplyStat("max_carry", static_cast<float>(m_tuning.trapperMaxCarryTraps))))
    );
    m_killerPowerState.trapperCarriedTraps = glm::clamp(m_killerPowerState.trapperCarriedTraps, 0, m_killerPowerState.trapperMaxCarryTraps);

    const auto killerTransformIt = m_world.Transforms().find(m_killer);
    auto killerActorIt = m_world.Actors().find(m_killer);
    if (killerTransformIt == m_world.Transforms().end() || killerActorIt == m_world.Actors().end())
    {
        m_trapPreviewActive = false;
        return;
    }

    const float setDuration = std::max(0.2F, m_killerPowerModifiers.ApplyStat("set_duration", m_tuning.trapperSetTrapSeconds));
    engine::scene::Entity nearbyDisarmedTrapForRearm = 0;
    const bool canRearmNearby = TryFindNearestTrap(killerTransformIt->second.position, 2.4F, true, &nearbyDisarmedTrapForRearm);
    if (m_controlledRole == ControlledRole::Killer && m_killerPowerState.trapperCarriedTraps > 0)
    {
        m_trapPreviewActive = ComputeTrapPlacementPreview(&m_trapPreviewPosition, &m_trapPreviewHalfExtents, &m_trapPreviewValid);
    }
    else
    {
        m_trapPreviewActive = false;
    }

    if (!killerCommand.useAltHeld)
    {
        m_killerPowerState.trapperSetRequiresRelease = false;
    }

    if (m_killerPowerState.trapperSetting)
    {
        if (killerCommand.useAltReleased ||
            m_killerPowerState.trapperCarriedTraps <= 0 ||
            m_killerAttackState != KillerAttackState::Idle ||
            killerActorIt->second.stunTimer > 0.0F)
        {
            m_killerPowerState.trapperSetting = false;
            m_killerPowerState.trapperSetTimer = 0.0F;
            m_killerPowerState.trapperSetRequiresRelease = true;
            ItemPowerLog("Trapper set cancelled");
            return;
        }

        killerActorIt->second.velocity = glm::vec3{0.0F};
        m_killerPowerState.trapperSetTimer += fixedDt;
        if (m_killerPowerState.trapperSetTimer >= setDuration)
        {
            if (m_trapPreviewActive && !m_trapPreviewValid)
            {
                m_killerPowerState.trapperSetting = false;
                m_killerPowerState.trapperSetTimer = 0.0F;
                m_killerPowerState.trapperSetRequiresRelease = true;
                AddRuntimeMessage("Invalid trap placement", 0.9F);
                ItemPowerLog("Trapper placement rejected (invalid preview)");
                return;
            }
            glm::vec3 trapPlacement = m_trapPreviewActive ? m_trapPreviewPosition : killerTransformIt->second.position;
            if (!m_trapPreviewActive)
            {
                glm::vec3 killerForward = killerTransformIt->second.forward;
                killerForward.y = 0.0F;
                if (glm::length(killerForward) <= 1.0e-5F)
                {
                    killerForward = glm::vec3{0.0F, 0.0F, -1.0F};
                }
                else
                {
                    killerForward = glm::normalize(killerForward);
                }
                trapPlacement += killerForward * 1.55F;
            }

            SpawnBearTrap(trapPlacement, killerTransformIt->second.forward, true);
            m_killerPowerState.trapperCarriedTraps = std::max(0, m_killerPowerState.trapperCarriedTraps - 1);
            m_killerPowerState.trapperSetting = false;
            m_killerPowerState.trapperSetTimer = 0.0F;
            m_killerPowerState.trapperSetRequiresRelease = true;
            ItemPowerLog("Trapper placed trap, carry=" + std::to_string(m_killerPowerState.trapperCarriedTraps));
            RebuildPhysicsWorld();
        }
        return;
    }

    auto findNearestTrapByState = [&](engine::scene::TrapState state, float radiusMeters) -> engine::scene::Entity {
        float bestDistance = radiusMeters;
        engine::scene::Entity bestEntity = 0;
        for (const auto& [entity, trap] : m_world.BearTraps())
        {
            if (trap.state != state)
            {
                continue;
            }
            const auto trapTransformIt = m_world.Transforms().find(entity);
            if (trapTransformIt == m_world.Transforms().end())
            {
                continue;
            }
            const float distance = DistanceXZ(killerTransformIt->second.position, trapTransformIt->second.position);
            if (distance <= bestDistance)
            {
                bestDistance = distance;
                bestEntity = entity;
            }
        }
        return bestEntity;
    };

    if (killerCommand.useAltPressed)
    {
        const engine::scene::Entity disarmedTrap = findNearestTrapByState(engine::scene::TrapState::Disarmed, 2.4F);
        if (disarmedTrap != 0)
        {
            m_killerPowerState.trapperSetting = false;
            m_killerPowerState.trapperSetTimer = 0.0F;
            auto trapIt = m_world.BearTraps().find(disarmedTrap);
            if (trapIt != m_world.BearTraps().end())
            {
                trapIt->second.state = engine::scene::TrapState::Armed;
                trapIt->second.trappedEntity = 0;
                trapIt->second.escapeAttempts = 0;
                trapIt->second.protectedKiller = m_killer;
                trapIt->second.killerProtectionDistance = 2.0F;
                m_killerPowerState.trapperSetRequiresRelease = true;
                AddRuntimeMessage("Trap re-armed", 1.0F);
                ItemPowerLog("Trapper re-armed trap entity=" + std::to_string(disarmedTrap));
                RebuildPhysicsWorld();
                return;
            }
        }
    }

    if (killerCommand.useAltPressed &&
        !m_killerPowerState.trapperSetRequiresRelease &&
        m_killerPowerState.trapperCarriedTraps > 0 &&
        m_killerAttackState == KillerAttackState::Idle &&
        !canRearmNearby &&
        m_survivorState != SurvivorHealthState::Carried)
    {
        m_killerPowerState.trapperSetting = true;
        m_killerPowerState.trapperSetTimer = 0.0F;
        ItemPowerLog("Trapper started setting trap");
    }

    if (!killerCommand.interactPressed)
    {
        return;
    }

    engine::scene::Entity nearestTrap = findNearestTrapByState(engine::scene::TrapState::Armed, 2.4F);
    if (nearestTrap == 0)
    {
        nearestTrap = findNearestTrapByState(engine::scene::TrapState::Disarmed, 2.4F);
    }
    if (nearestTrap == 0)
    {
        return;
    }

    if (m_killerPowerState.trapperCarriedTraps >= m_killerPowerState.trapperMaxCarryTraps)
    {
        AddRuntimeMessage("Trap inventory full", 0.9F);
        ItemPowerLog("Trapper pickup blocked: inventory full");
        return;
    }

    const auto trapIt = m_world.BearTraps().find(nearestTrap);
    if (trapIt == m_world.BearTraps().end())
    {
        return;
    }
    if (trapIt->second.state != engine::scene::TrapState::Armed &&
        trapIt->second.state != engine::scene::TrapState::Disarmed)
    {
        return;
    }

    DestroyEntity(nearestTrap);
    m_killerPowerState.trapperCarriedTraps += 1;
    AddRuntimeMessage("Picked up trap", 1.0F);
    ItemPowerLog(
        "Trapper picked trap entity=" + std::to_string(nearestTrap) +
        " carry=" + std::to_string(m_killerPowerState.trapperCarriedTraps)
    );
    RebuildPhysicsWorld();
}

void GameplaySystems::UpdateWraithPowerSystem(const RoleCommand& killerCommand, float fixedDt)
{
    if (m_killer == 0)
    {
        return;
    }

    const loadout::PowerDefinition* powerDef = m_loadoutCatalog.FindPower(m_killerLoadout.powerId);
    const auto readParam = [&](const std::string& key, float fallback) {
        if (powerDef == nullptr)
        {
            return fallback;
        }
        const auto it = powerDef->params.find(key);
        return it != powerDef->params.end() ? it->second : fallback;
    };

    const float cloakDuration = std::max(
        0.1F,
        m_killerPowerModifiers.ApplyStat(
            "cloak_transition_seconds",
            readParam("cloak_transition_seconds", m_tuning.wraithCloakTransitionSeconds)
        )
    );
    const float uncloakDuration = std::max(
        0.1F,
        m_killerPowerModifiers.ApplyStat(
            "uncloak_transition_seconds",
            readParam("uncloak_transition_seconds", m_tuning.wraithUncloakTransitionSeconds)
        )
    );

    if (m_killerPowerState.wraithCloakTransition)
    {
        m_killerPowerState.wraithTransitionTimer += fixedDt;
        
        if (m_killerPowerState.wraithTransitionTimer >= cloakDuration)
        {
            m_killerPowerState.wraithCloakTransition = false;
            m_killerPowerState.wraithCloaked = true;
            m_killerPowerState.wraithTransitionTimer = 0.0F;
            AddRuntimeMessage("Wraith cloaked", 1.0F);
            ItemPowerLog("Wraith cloak completed");
        }
    }
    else if (m_killerPowerState.wraithUncloakTransition)
    {
        m_killerPowerState.wraithTransitionTimer += fixedDt;
        
        if (m_killerPowerState.wraithTransitionTimer >= uncloakDuration)
        {
            m_killerPowerState.wraithUncloakTransition = false;
            m_killerPowerState.wraithCloaked = false;
            m_killerPowerState.wraithTransitionTimer = 0.0F;
            m_killerPowerState.wraithPostUncloakTimer = std::max(
                0.0F,
                m_killerPowerModifiers.ApplyStat(
                    "post_uncloak_haste_seconds",
                    readParam("post_uncloak_haste_seconds", m_tuning.wraithPostUncloakHasteSeconds)
                )
            );
            AddRuntimeMessage("Wraith uncloaked", 1.0F);
            ItemPowerLog("Wraith uncloak completed, haste=" + std::to_string(m_killerPowerState.wraithPostUncloakTimer));
        }
    }

    if (m_killerPowerState.wraithPostUncloakTimer > 0.0F)
    {
        m_killerPowerState.wraithPostUncloakTimer = std::max(0.0F, m_killerPowerState.wraithPostUncloakTimer - fixedDt);
    }

    // Apply Undetectable status effect when fully cloaked
    // This disables terror radius and killer look light
    if (m_killerPowerState.wraithCloaked && !m_killerPowerState.wraithUncloakTransition)
    {
        StatusEffect undetectable;
        undetectable.type = StatusEffectType::Undetectable;
        undetectable.infinite = true;
        undetectable.sourceId = "wraith_cloak";
        m_statusEffectManager.ApplyEffect(m_killer, undetectable);
    }
    else
    {
        // Remove Undetectable when not fully cloaked
        m_statusEffectManager.RemoveEffectBySource(m_killer, "wraith_cloak");
    }

    if (!killerCommand.useAltPressed)
    {
        return;
    }

    if (m_killerPowerState.wraithCloakTransition || m_killerPowerState.wraithUncloakTransition)
    {
        return;
    }

    if (m_killerPowerState.wraithCloaked)
    {
        m_killerPowerState.wraithUncloakTransition = true;
        m_killerPowerState.wraithTransitionTimer = 0.0F;
        ItemPowerLog("Wraith uncloak started");
    }
    else
    {
        m_killerPowerState.wraithCloakTransition = true;
        m_killerPowerState.wraithTransitionTimer = 0.0F;
        ItemPowerLog("Wraith cloak started");
    }
}

void GameplaySystems::UpdateBearTrapSystem(const RoleCommand& survivorCommand, const RoleCommand& killerCommand, float fixedDt)
{
    (void)killerCommand;
    if (m_survivor == 0)
    {
        return;
    }

    auto survivorTransformIt = m_world.Transforms().find(m_survivor);
    if (survivorTransformIt == m_world.Transforms().end())
    {
        return;
    }

    if (m_survivorState != SurvivorHealthState::Trapped &&
        m_survivorState != SurvivorHealthState::Dead &&
        m_survivorState != SurvivorHealthState::Hooked &&
        m_survivorState != SurvivorHealthState::Carried)
    {
        TryTriggerBearTraps(m_survivor, survivorTransformIt->second.position);
    }

    // Killer stepping into trap: stun killer and reset trap.
    if (m_killer != 0 &&
        m_survivorState != SurvivorHealthState::Carried &&
        !m_killerPowerState.trapperSetting)
    {
        const auto killerTransformIt = m_world.Transforms().find(m_killer);
        const auto killerActorIt = m_world.Actors().find(m_killer);
        if (killerTransformIt != m_world.Transforms().end() && killerActorIt != m_world.Actors().end())
        {
            const float killerRadius = killerActorIt->second.capsuleRadius;
            const float killerHalfHeight = std::max(0.2F, killerActorIt->second.capsuleHeight * 0.5F);
            for (auto& [entity, trap] : m_world.BearTraps())
            {
                if (trap.state != engine::scene::TrapState::Armed)
                {
                    continue;
                }
                const auto trapTransformIt = m_world.Transforms().find(entity);
                if (trapTransformIt == m_world.Transforms().end())
                {
                    continue;
                }
                if (trap.protectedKiller == m_killer)
                {
                    const float distanceFromTrap = DistanceXZ(killerTransformIt->second.position, trapTransformIt->second.position);
                    if (distanceFromTrap < trap.killerProtectionDistance)
                    {
                        continue;
                    }
                    trap.protectedKiller = 0;
                }
                const glm::vec3 delta = killerTransformIt->second.position - trapTransformIt->second.position;
                const bool overlap =
                    std::abs(delta.x) <= (trap.halfExtents.x + killerRadius) &&
                    std::abs(delta.z) <= (trap.halfExtents.z + killerRadius) &&
                    std::abs(delta.y) <= (trap.halfExtents.y + killerHalfHeight + 0.2F);
                if (!overlap)
                {
                    continue;
                }
                trap.state = engine::scene::TrapState::Disarmed;
                trap.trappedEntity = 0;
                trap.escapeAttempts = 0;
                killerActorIt->second.stunTimer = std::max(killerActorIt->second.stunTimer, m_tuning.trapKillerStunSeconds);
                AddRuntimeMessage("Killer stepped in trap", 1.2F);
                ItemPowerLog("Killer stepped in trap entity=" + std::to_string(entity) + " stun=" + std::to_string(m_tuning.trapKillerStunSeconds));
                m_trapIndicatorText = "Killer stepped in trap (stunned)";
                m_trapIndicatorTimer = 1.6F;
                m_trapIndicatorDanger = true;
                RebuildPhysicsWorld();
                break;
            }
        }
    }

    // Survivor disarm while not trapped.
    if (m_survivorState != SurvivorHealthState::Trapped &&
        m_survivorState != SurvivorHealthState::Hooked &&
        m_survivorState != SurvivorHealthState::Carried &&
        m_survivorState != SurvivorHealthState::Dead)
    {
        engine::scene::Entity armedTrap = 0;
        if (TryFindNearestTrap(survivorTransformIt->second.position, 1.9F, false, &armedTrap))
        {
            auto trapIt = m_world.BearTraps().find(armedTrap);
            if (trapIt != m_world.BearTraps().end() && trapIt->second.state == engine::scene::TrapState::Armed && survivorCommand.interactHeld)
            {
                if (m_survivorItemState.trapDisarmTarget != armedTrap)
                {
                    m_survivorItemState.trapDisarmTarget = armedTrap;
                    m_survivorItemState.trapDisarmProgress = 0.0F;
                }
                m_survivorItemState.trapDisarmProgress += fixedDt;
                if (m_survivorItemState.trapDisarmProgress >= std::max(0.2F, m_tuning.trapperDisarmSeconds))
                {
                    trapIt->second.state = engine::scene::TrapState::Disarmed;
                    trapIt->second.trappedEntity = 0;
                    trapIt->second.escapeAttempts = 0;
                    m_survivorItemState.trapDisarmProgress = 0.0F;
                    m_survivorItemState.trapDisarmTarget = 0;
                    AddRuntimeMessage("Trap disarmed", 1.0F);
                    ItemPowerLog("Survivor disarmed trap entity=" + std::to_string(armedTrap));
                    m_trapIndicatorText = "Trap disarmed";
                    m_trapIndicatorTimer = 1.0F;
                    m_trapIndicatorDanger = false;
                    RebuildPhysicsWorld();
                }
            }
            else if (!survivorCommand.interactHeld)
            {
                m_survivorItemState.trapDisarmProgress = 0.0F;
                m_survivorItemState.trapDisarmTarget = 0;
            }
        }
        else
        {
            m_survivorItemState.trapDisarmProgress = 0.0F;
            m_survivorItemState.trapDisarmTarget = 0;
        }
    }

    if (m_survivorState != SurvivorHealthState::Trapped)
    {
        return;
    }

    auto trappedTrapIt = m_world.BearTraps().end();
    for (auto it = m_world.BearTraps().begin(); it != m_world.BearTraps().end(); ++it)
    {
        if (it->second.trappedEntity == m_survivor && it->second.state == engine::scene::TrapState::Triggered)
        {
            trappedTrapIt = it;
            break;
        }
    }
    if (trappedTrapIt == m_world.BearTraps().end())
    {
        SetSurvivorState(SurvivorHealthState::Injured, "Trap released");
        return;
    }

    engine::scene::BearTrapComponent& trap = trappedTrapIt->second;
    auto survivorActorIt = m_world.Actors().find(m_survivor);
    if (survivorActorIt != m_world.Actors().end())
    {
        survivorActorIt->second.velocity = glm::vec3{0.0F};
    }

    if (!survivorCommand.interactPressed)
    {
        return;
    }

    trap.escapeAttempts += 1;
    const float chanceStep = glm::clamp(
        m_killerPowerModifiers.ApplyStat("escape_chance_step", std::max(0.01F, m_tuning.trapEscapeChanceStep)),
        0.01F,
        0.95F
    );
    const int maxAttempts = std::max(1, static_cast<int>(std::round(m_killerPowerModifiers.ApplyStat("max_escape_attempts", static_cast<float>(trap.maxEscapeAttempts)))));
    trap.maxEscapeAttempts = maxAttempts;
    const float maxEscapeChance = glm::clamp(m_tuning.trapEscapeChanceMax, 0.05F, 0.99F);
    trap.escapeChance = glm::clamp(trap.escapeChance + chanceStep, 0.03F, maxEscapeChance);

    std::uniform_real_distribution<float> chanceRoll(0.0F, 1.0F);
    const bool success = chanceRoll(m_rng) <= trap.escapeChance || trap.escapeAttempts >= trap.maxEscapeAttempts;
    if (success)
    {
        trap.state = engine::scene::TrapState::Disarmed;
        trap.trappedEntity = 0;
        const float bleedMult = m_killerPowerModifiers.ApplyHook("trap_escape", "bleed_multiplier", 1.0F);
        SetSurvivorState(SurvivorHealthState::Injured, "Escaped trap");
        if (bleedMult > 1.01F)
        {
            AddRuntimeMessage("Escaped trap (Serrated Jaws bleed)", 1.2F);
            m_bloodSpawnAccumulator = std::max(m_bloodSpawnAccumulator, m_bloodProfile.spawnInterval * 0.65F);
            ItemPowerLog("Survivor escaped trap with bleed modifier");
        }
        else
        {
            AddRuntimeMessage("Escaped trap", 1.0F);
            ItemPowerLog("Survivor escaped trap");
        }
        m_trapIndicatorText = "Escaped trap";
        m_trapIndicatorTimer = 1.0F;
        m_trapIndicatorDanger = false;
        RebuildPhysicsWorld();
    }
    else
    {
        AddRuntimeMessage(
            "Trap escape failed (" + std::to_string(trap.escapeAttempts) + "/" + std::to_string(trap.maxEscapeAttempts) + ")",
            1.0F
        );
        ItemPowerLog(
            "Trap escape failed attempts=" + std::to_string(trap.escapeAttempts) +
            "/" + std::to_string(trap.maxEscapeAttempts)
        );
    }
}

engine::scene::Entity GameplaySystems::SpawnBearTrap(const glm::vec3& basePosition, const glm::vec3& forward, bool emitMessage)
{
    const loadout::PowerDefinition* powerDef = m_loadoutCatalog.FindPower(m_killerLoadout.powerId);
    const auto readParam = [&](const std::string& key, float fallback) {
        if (powerDef == nullptr)
        {
            return fallback;
        }
        const auto it = powerDef->params.find(key);
        return it != powerDef->params.end() ? it->second : fallback;
    };

    const glm::vec3 normalizedForward = glm::length(forward) > 1.0e-5F ? glm::normalize(forward) : glm::vec3{0.0F, 0.0F, -1.0F};
    glm::vec3 position = basePosition;

    const glm::vec3 rayStart = position + glm::vec3{0.0F, 2.2F, 0.0F};
    const glm::vec3 rayEnd = rayStart + glm::vec3{0.0F, -8.0F, 0.0F};
    if (const std::optional<engine::physics::RaycastHit> hit = m_physics.RaycastNearest(rayStart, rayEnd))
    {
        position.y = hit->position.y + 0.05F;
    }
    else
    {
        position.y = 0.05F;
    }

    const engine::scene::Entity trapEntity = m_world.CreateEntity();
    m_world.Transforms()[trapEntity] = engine::scene::Transform{
        position,
        glm::vec3{0.0F},
        glm::vec3{1.0F},
        glm::vec3{normalizedForward.x, 0.0F, normalizedForward.z},
    };

    engine::scene::BearTrapComponent trap;
    trap.state = engine::scene::TrapState::Armed;
    trap.halfExtents.x = std::max(0.12F, m_killerPowerModifiers.ApplyStat("trap_half_x", readParam("trap_half_x", 0.36F)));
    trap.halfExtents.y = std::max(0.03F, m_killerPowerModifiers.ApplyStat("trap_half_y", readParam("trap_half_y", 0.08F)));
    trap.halfExtents.z = std::max(0.12F, m_killerPowerModifiers.ApplyStat("trap_half_z", readParam("trap_half_z", 0.36F)));
    trap.escapeChance = glm::clamp(
        m_killerPowerModifiers.ApplyStat("base_escape_chance", readParam("base_escape_chance", m_tuning.trapEscapeBaseChance)),
        0.02F,
        0.95F
    );
    trap.escapeChanceStep = glm::clamp(
        m_killerPowerModifiers.ApplyStat("escape_chance_step", readParam("escape_chance_step", m_tuning.trapEscapeChanceStep)),
        0.01F,
        0.6F
    );
    trap.escapeAttempts = 0;
    trap.maxEscapeAttempts = std::max(
        1,
        static_cast<int>(std::round(m_killerPowerModifiers.ApplyStat("max_escape_attempts", readParam("max_escape_attempts", 6.0F))))
    );
    trap.protectedKiller = m_killer;
    trap.killerProtectionDistance = 2.0F;
    m_world.BearTraps()[trapEntity] = trap;
    m_world.Names()[trapEntity] = engine::scene::NameComponent{"bear_trap"};
    m_world.DebugColors()[trapEntity] = engine::scene::DebugColorComponent{glm::vec3{0.72F, 0.72F, 0.75F}};
    if (emitMessage)
    {
        AddRuntimeMessage("Bear trap placed", 1.0F);
        ItemPowerLog(
            "Trap placed entity=" + std::to_string(trapEntity) +
            " pos=(" + std::to_string(position.x) + "," + std::to_string(position.y) + "," + std::to_string(position.z) + ")"
        );
    }
    return trapEntity;
}

void GameplaySystems::ClearAllBearTraps()
{
    std::vector<engine::scene::Entity> trapEntities;
    trapEntities.reserve(m_world.BearTraps().size());
    for (const auto& [entity, _] : m_world.BearTraps())
    {
        trapEntities.push_back(entity);
    }
    for (engine::scene::Entity entity : trapEntities)
    {
        DestroyEntity(entity);
    }
    if (m_survivorState == SurvivorHealthState::Trapped)
    {
        SetSurvivorState(SurvivorHealthState::Injured, "Traps cleared");
    }
}

void GameplaySystems::ClearTrappedSurvivorBinding(engine::scene::Entity survivorEntity, bool disarmTrap)
{
    if (survivorEntity == 0)
    {
        return;
    }

    for (auto& [entity, trap] : m_world.BearTraps())
    {
        if (trap.trappedEntity != survivorEntity)
        {
            continue;
        }
        trap.trappedEntity = 0;
        trap.escapeAttempts = 0;
        trap.state = disarmTrap ? engine::scene::TrapState::Disarmed : engine::scene::TrapState::Armed;
        if (!disarmTrap)
        {
            trap.protectedKiller = m_killer;
            trap.killerProtectionDistance = 1.8F;
        }
        ItemPowerLog(
            "Cleared trap binding trap=" + std::to_string(entity) +
            " disarm=" + std::string(disarmTrap ? "true" : "false")
        );
    }
}

void GameplaySystems::TryTriggerBearTraps(engine::scene::Entity survivorEntity, const glm::vec3& survivorPos)
{
    if (survivorEntity == 0)
    {
        return;
    }

    const auto survivorActorIt = m_world.Actors().find(survivorEntity);
    if (survivorActorIt == m_world.Actors().end())
    {
        return;
    }
    const float survivorRadius = survivorActorIt->second.capsuleRadius;
    const float survivorHalfHeight = std::max(0.2F, survivorActorIt->second.capsuleHeight * 0.5F);

    for (auto& [entity, trap] : m_world.BearTraps())
    {
        if (trap.state != engine::scene::TrapState::Armed)
        {
            continue;
        }
        const auto trapTransformIt = m_world.Transforms().find(entity);
        if (trapTransformIt == m_world.Transforms().end())
        {
            continue;
        }

        const glm::vec3 delta = survivorPos - trapTransformIt->second.position;
        const bool overlap =
            std::abs(delta.x) <= (trap.halfExtents.x + survivorRadius) &&
            std::abs(delta.z) <= (trap.halfExtents.z + survivorRadius) &&
            std::abs(delta.y) <= (trap.halfExtents.y + survivorHalfHeight + 0.2F);
        if (!overlap)
        {
            continue;
        }

        trap.state = engine::scene::TrapState::Triggered;
        trap.trappedEntity = survivorEntity;
        trap.escapeAttempts = 0;
        trap.escapeChance = glm::clamp(
            m_killerPowerModifiers.ApplyStat("base_escape_chance", std::max(0.01F, m_tuning.trapEscapeBaseChance)),
            0.01F,
            0.95F
        );
        trap.escapeChanceStep = glm::clamp(
            m_killerPowerModifiers.ApplyStat("escape_chance_step", std::max(0.01F, m_tuning.trapEscapeChanceStep)),
            0.01F,
            0.95F
        );
        SetSurvivorState(SurvivorHealthState::Trapped, "Bear trap triggered");
        AddRuntimeMessage("Survivor trapped", 1.2F);
        ItemPowerLog("Survivor trapped by entity=" + std::to_string(entity));
        m_trapIndicatorText = "Survivor trapped!";
        m_trapIndicatorTimer = 1.5F;
        m_trapIndicatorDanger = true;
        RebuildPhysicsWorld();
        break;
    }
}

void GameplaySystems::TrapSpawnDebug(int count)
{
    if (m_killer == 0)
    {
        return;
    }
    const auto killerTransformIt = m_world.Transforms().find(m_killer);
    if (killerTransformIt == m_world.Transforms().end())
    {
        return;
    }
    const int spawnCount = std::max(1, count);
    for (int i = 0; i < spawnCount; ++i)
    {
        glm::vec3 spawnPos = killerTransformIt->second.position;
        glm::vec3 forward = killerTransformIt->second.forward;
        forward.y = 0.0F;
        if (glm::length(forward) > 1.0e-5F)
        {
            spawnPos += glm::normalize(forward) * 1.55F;
        }
        else
        {
            spawnPos += glm::vec3{0.0F, 0.0F, -1.55F};
        }
        SpawnBearTrap(spawnPos, killerTransformIt->second.forward, spawnCount == 1);
    }
    if (spawnCount > 1)
    {
        AddRuntimeMessage("Spawned " + std::to_string(spawnCount) + " traps", 1.0F);
    }
    ItemPowerLog("Trap debug spawn count=" + std::to_string(spawnCount));
    RebuildPhysicsWorld();
}

void GameplaySystems::TrapClearDebug()
{
    ClearAllBearTraps();
    RebuildPhysicsWorld();
}

void GameplaySystems::SetScratchDebug(bool enabled)
{
    m_scratchDebugEnabled = enabled;
}

void GameplaySystems::SetBloodDebug(bool enabled)
{
    m_bloodDebugEnabled = enabled;
}

void GameplaySystems::SetScratchProfile(const std::string& profileName)
{
    (void)profileName;
    m_scratchProfile = ScratchProfile{};
    m_scratchProfile.spawnIntervalMin = 0.08F;
    m_scratchProfile.spawnIntervalMax = 0.15F;
}

void GameplaySystems::SetBloodProfile(const std::string& profileName)
{
    (void)profileName;
    m_bloodProfile = BloodProfile{};
}

// ============================================================================
// Hatchet Power System Implementation
// ============================================================================

void GameplaySystems::UpdateHatchetPowerSystem(const RoleCommand& killerCommand, float fixedDt)
{
    if (m_killer == 0)
    {
        return;
    }

    const loadout::PowerDefinition* powerDef = m_loadoutCatalog.FindPower(m_killerLoadout.powerId);
    if (powerDef == nullptr || powerDef->id != "hatchet_throw")
    {
        return;
    }

    const auto killerTransformIt = m_world.Transforms().find(m_killer);
    const auto killerActorIt = m_world.Actors().find(m_killer);
    if (killerTransformIt == m_world.Transforms().end() || killerActorIt == m_world.Actors().end())
    {
        return;
    }

    // Read power parameters from definition or use tuning defaults
    const auto readParam = [&](const std::string& key, float fallback) {
        if (powerDef == nullptr) return fallback;
        const auto it = powerDef->params.find(key);
        return it != powerDef->params.end() ? it->second : fallback;
    };

    m_killerPowerState.hatchetMaxCount = static_cast<int>(m_killerPowerModifiers.ApplyStat(
        "max_count", static_cast<float>(m_tuning.hatchetMaxCount)));
    const float chargeMinSeconds = readParam("charge_min_seconds", m_tuning.hatchetChargeMinSeconds);
    const float chargeMaxSeconds = readParam("charge_max_seconds", m_tuning.hatchetChargeMaxSeconds);

    // Handle locker replenishing
    if (m_killerPowerState.lockerReplenishing)
    {
        const float replenishTime = readParam("locker_replenish_time", m_tuning.hatchetLockerReplenishTime);
        m_killerPowerState.lockerReplenishTimer += fixedDt;

        if (!killerCommand.interactHeld ||
            m_killerPowerState.lockerTargetEntity == 0 ||
            m_world.Lockers().find(m_killerPowerState.lockerTargetEntity) == m_world.Lockers().end())
        {
            m_killerPowerState.lockerReplenishing = false;
            m_killerPowerState.lockerReplenishTimer = 0.0F;
            m_killerPowerState.lockerTargetEntity = 0;
            ItemPowerLog("Hatchet replenish cancelled");
        }
        else if (m_killerPowerState.lockerReplenishTimer >= replenishTime)
        {
            const int replenishCount = static_cast<int>(readParam("locker_replenish_count", static_cast<float>(m_tuning.hatchetLockerReplenishCount)));
            m_killerPowerState.hatchetCount = std::min(m_killerPowerState.hatchetMaxCount, replenishCount);
            m_killerPowerState.lockerReplenishing = false;
            m_killerPowerState.lockerReplenishTimer = 0.0F;
            m_killerPowerState.lockerTargetEntity = 0;
            AddRuntimeMessage("Hatchets replenished!", 1.5F);
            ItemPowerLog("Hatchet replenish complete, count=" + std::to_string(m_killerPowerState.hatchetCount));
        }
        return;
    }

    // Reset throw requires release flag when RMB is not held
    if (!killerCommand.useAltHeld)
    {
        m_killerPowerState.hatchetThrowRequiresRelease = false;
    }

    // Don't allow charging if no hatchets or if in attack
    if (m_killerPowerState.hatchetCount <= 0 ||
        m_killerAttackState != KillerAttackState::Idle ||
        killerActorIt->second.stunTimer > 0.0F ||
        m_survivorState == SurvivorHealthState::Carried)
    {
        m_killerPowerState.hatchetCharging = false;
        m_killerPowerState.hatchetChargeTimer = 0.0F;
        m_killerPowerState.hatchetCharge01 = 0.0F;
        return;
    }

    // Start charging on RMB hold
    if (killerCommand.useAltHeld && !m_killerPowerState.hatchetThrowRequiresRelease)
    {
        if (!m_killerPowerState.hatchetCharging)
        {
            m_killerPowerState.hatchetCharging = true;
            m_killerPowerState.hatchetChargeTimer = 0.0F;
            ItemPowerLog("Hatchet charging started");
        }

        m_killerPowerState.hatchetChargeTimer += fixedDt;
        m_killerPowerState.hatchetCharge01 = glm::clamp(
            (m_killerPowerState.hatchetChargeTimer - chargeMinSeconds) / (chargeMaxSeconds - chargeMinSeconds),
            0.0F, 1.0F
        );
    }

    // Throw on RMB release
    if (killerCommand.useAltReleased && m_killerPowerState.hatchetCharging)
    {
        // Use actual camera position for spawn (center of screen)
        const glm::vec3 spawnPos = m_cameraPosition;
        glm::vec3 forward = m_cameraForward;
        if (glm::length(forward) < 1.0e-5F)
        {
            forward = glm::vec3{0.0F, 0.0F, -1.0F};
        }
        else
        {
            forward = glm::normalize(forward);
        }

        SpawnHatchetProjectile(spawnPos, forward, m_killerPowerState.hatchetCharge01);
        m_killerPowerState.hatchetCount = std::max(0, m_killerPowerState.hatchetCount - 1);
        m_killerPowerState.hatchetCharging = false;
        m_killerPowerState.hatchetChargeTimer = 0.0F;
        m_killerPowerState.hatchetCharge01 = 0.0F;
        m_killerPowerState.hatchetThrowRequiresRelease = true;
        ItemPowerLog("Hatchet thrown, remaining=" + std::to_string(m_killerPowerState.hatchetCount));
    }
}

engine::scene::Entity GameplaySystems::SpawnHatchetProjectile(
    const glm::vec3& origin,
    const glm::vec3& direction,
    float charge01)
{
    const loadout::PowerDefinition* powerDef = m_loadoutCatalog.FindPower(m_killerLoadout.powerId);
    const auto readParam = [&](const std::string& key, float fallback) {
        if (powerDef == nullptr) return fallback;
        const auto it = powerDef->params.find(key);
        return it != powerDef->params.end() ? it->second : fallback;
    };

    const float speedMin = readParam("throw_speed_min", m_tuning.hatchetThrowSpeedMin);
    const float speedMax = readParam("throw_speed_max", m_tuning.hatchetThrowSpeedMax);
    const float gravityMin = readParam("gravity_min", m_tuning.hatchetGravityMin);
    const float gravityMax = readParam("gravity_max", m_tuning.hatchetGravityMax);
    const float maxLifetime = readParam("max_lifetime", 5.0F);
    const float maxRange = readParam("max_range", m_tuning.hatchetMaxRange);

    const float speed = glm::mix(speedMin, speedMax, charge01);
    const float gravity = glm::mix(gravityMin, gravityMax, charge01);

    const engine::scene::Entity entity = m_world.CreateEntity();

    engine::scene::Transform transform;
    transform.position = origin;
    transform.forward = direction;
    transform.scale = glm::vec3{1.0F};
    m_world.Transforms()[entity] = transform;

    engine::scene::ProjectileState projectile;
    projectile.type = engine::scene::ProjectileState::Type::Hatchet;
    projectile.active = true;
    projectile.position = origin;
    projectile.velocity = direction * speed;
    projectile.forward = direction;
    projectile.age = 0.0F;
    projectile.maxLifetime = maxLifetime;
    projectile.gravity = gravity;
    projectile.ownerEntity = m_killer;
    projectile.hasHit = false;
    m_world.Projectiles()[entity] = projectile;

    engine::scene::DebugColorComponent debugColor;
    debugColor.color = glm::vec3{0.8F, 0.6F, 0.2F}; // Brown/orange for hatchets
    m_world.DebugColors()[entity] = debugColor;

    m_world.Names()[entity] = engine::scene::NameComponent{"hatchet_projectile"};

    ItemPowerLog("Spawned hatchet projectile entity=" + std::to_string(entity) +
        " speed=" + std::to_string(speed) + " gravity=" + std::to_string(gravity));

    (void)maxRange; // Used for range limiting if needed
    return entity;
}

void GameplaySystems::UpdateProjectiles(float fixedDt)
{
    const loadout::PowerDefinition* powerDef = m_loadoutCatalog.FindPower(m_killerLoadout.powerId);
    const auto readParam = [&](const std::string& key, float fallback) {
        if (powerDef == nullptr) return fallback;
        const auto it = powerDef->params.find(key);
        return it != powerDef->params.end() ? it->second : fallback;
    };
    const float collisionRadius = readParam("collision_radius", m_tuning.hatchetCollisionRadius);

    std::vector<engine::scene::Entity> toDestroy;

    for (auto& [entity, projectile] : m_world.Projectiles())
    {
        if (!projectile.active)
        {
            continue;
        }

        // Apply gravity
        projectile.velocity.y -= projectile.gravity * fixedDt;

        // Apply air drag (velocity decay) - makes hatchet arc more at distance
        projectile.velocity *= m_tuning.hatchetAirDrag;

        // Calculate next position
        const glm::vec3 nextPos = projectile.position + projectile.velocity * fixedDt;

        // Update forward direction based on velocity
        if (glm::length(projectile.velocity) > 1.0e-5F)
        {
            projectile.forward = glm::normalize(projectile.velocity);
        }

        // Update transform
        auto transformIt = m_world.Transforms().find(entity);
        if (transformIt != m_world.Transforms().end())
        {
            transformIt->second.position = nextPos;
            transformIt->second.forward = projectile.forward;
        }

        // World collision raycast
        const auto hitOpt = m_physics.RaycastNearest(projectile.position, nextPos, projectile.ownerEntity);
        if (hitOpt.has_value())
        {
            const auto& hit = hitOpt.value();
            projectile.active = false;
            projectile.hasHit = true;
            projectile.position = hit.position;

            // Spawn impact FX
            const engine::fx::FxNetMode netMode = m_networkAuthorityMode ? engine::fx::FxNetMode::ServerBroadcast : engine::fx::FxNetMode::Local;
            SpawnGameplayFx("fx_hatchet_impact", hit.position, projectile.forward, netMode);
            ItemPowerLog("Hatchet hit world at " + std::to_string(hit.position.x) + "," +
                std::to_string(hit.position.y) + "," + std::to_string(hit.position.z));
            continue;
        }

        // Survivor collision check
        if (m_survivor != 0 && projectile.ownerEntity == m_killer)
        {
            auto survivorTransformIt = m_world.Transforms().find(m_survivor);
            auto survivorActorIt = m_world.Actors().find(m_survivor);
            if (survivorTransformIt != m_world.Transforms().end() &&
                survivorActorIt != m_world.Actors().end())
            {
                const glm::vec3 survivorPos = survivorTransformIt->second.position;
                const float survivorRadius = survivorActorIt->second.capsuleRadius;
                const float survivorHeight = survivorActorIt->second.capsuleHeight;

                if (ProjectileHitsCapsule(nextPos, collisionRadius, survivorPos, survivorRadius, survivorHeight))
                {
                    projectile.active = false;
                    projectile.hasHit = true;
                    projectile.position = nextPos;

                    const engine::fx::FxNetMode netMode = m_networkAuthorityMode ? engine::fx::FxNetMode::ServerBroadcast : engine::fx::FxNetMode::Local;

                    // Apply damage to survivor
                    if (m_survivorState == SurvivorHealthState::Healthy)
                    {
                        SetSurvivorState(SurvivorHealthState::Injured, "hatchet_hit");
                        SpawnGameplayFx("fx_blood_splatter", survivorPos, projectile.forward, netMode);
                        AddRuntimeMessage("Hatchet hit survivor!", 1.5F);
                    }
                    else if (m_survivorState == SurvivorHealthState::Injured)
                    {
                        SetSurvivorState(SurvivorHealthState::Downed, "hatchet_hit");
                        SpawnGameplayFx("fx_blood_splatter_large", survivorPos, projectile.forward, netMode);
                        AddRuntimeMessage("Hatchet downed survivor!", 2.0F);
                    }

                    ItemPowerLog("Hatchet hit survivor!");
                    continue;
                }
            }
        }

        // Update position
        projectile.position = nextPos;

        // Lifetime check
        projectile.age += fixedDt;
        if (projectile.age >= projectile.maxLifetime)
        {
            projectile.active = false;
            ItemPowerLog("Hatchet expired");
        }

        // Range check (optional - could use maxRange)
        // Deactivate if too far from origin
        const float maxRange = readParam("max_range", m_tuning.hatchetMaxRange);
        if (projectile.age * glm::length(projectile.velocity) > maxRange)
        {
            projectile.active = false;
            ItemPowerLog("Hatchet exceeded max range");
        }
    }

    // Cleanup inactive projectiles (optional - could keep for debugging)
    // For now, we'll leave them but they won't be rendered
}

bool GameplaySystems::ProjectileHitsCapsule(
    const glm::vec3& projectilePos,
    float projectileRadius,
    const glm::vec3& capsulePos,
    float capsuleRadius,
    float capsuleHeight) const
{
    // Sphere vs capsule collision
    // Capsule is vertical, centered at capsulePos
    const float halfHeight = capsuleHeight * 0.5F - capsuleRadius;
    const glm::vec3 capsuleTop = capsulePos + glm::vec3{0.0F, halfHeight, 0.0F};
    const glm::vec3 capsuleBottom = capsulePos - glm::vec3{0.0F, halfHeight, 0.0F};

    // Find closest point on capsule line segment to projectile
    const glm::vec3 closestPoint = ClosestPointOnSegment(projectilePos, capsuleBottom, capsuleTop);

    // Check if within combined radii
    const float combinedRadius = projectileRadius + capsuleRadius;
    const float dist = glm::distance(projectilePos, closestPoint);
    return dist <= combinedRadius;
}

glm::vec3 GameplaySystems::ClosestPointOnSegment(const glm::vec3& point, const glm::vec3& a, const glm::vec3& b) const
{
    const glm::vec3 ab = b - a;
    const float abLenSq = glm::dot(ab, ab);
    if (abLenSq < 1.0e-10F)
    {
        return a;
    }
    float t = glm::dot(point - a, ab) / abLenSq;
    t = glm::clamp(t, 0.0F, 1.0F);
    return a + t * ab;
}

engine::scene::Entity GameplaySystems::SpawnLocker(const glm::vec3& position, const glm::vec3& forward)
{
    const engine::scene::Entity entity = m_world.CreateEntity();

    engine::scene::Transform transform;
    transform.position = position;
    transform.forward = forward;
    transform.scale = glm::vec3{1.0F};
    m_world.Transforms()[entity] = transform;

    engine::scene::LockerComponent locker;
    locker.halfExtents = glm::vec3{0.45F, 1.1F, 0.35F};
    locker.killerOnly = true;
    m_world.Lockers()[entity] = locker;

    engine::scene::DebugColorComponent debugColor;
    debugColor.color = glm::vec3{0.4F, 0.3F, 0.2F}; // Brown for lockers
    m_world.DebugColors()[entity] = debugColor;

    m_world.Names()[entity] = engine::scene::NameComponent{"locker"};

    ItemPowerLog("Spawned locker entity=" + std::to_string(entity));
    return entity;
}

void GameplaySystems::SpawnLockerAtKiller()
{
    if (m_killer == 0)
    {
        AddRuntimeMessage("No killer to spawn locker at", 1.0F);
        return;
    }

    auto killerTransformIt = m_world.Transforms().find(m_killer);
    if (killerTransformIt == m_world.Transforms().end())
    {
        return;
    }

    glm::vec3 spawnPos = killerTransformIt->second.position;
    glm::vec3 forward = killerTransformIt->second.forward;
    forward.y = 0.0F;
    if (glm::length(forward) > 1.0e-5F)
    {
        spawnPos += glm::normalize(forward) * 1.5F;
    }
    else
    {
        spawnPos += glm::vec3{0.0F, 0.0F, -1.5F};
    }

    SpawnLocker(spawnPos, killerTransformIt->second.forward);
    AddRuntimeMessage("Spawned locker", 1.0F);
    RebuildPhysicsWorld();
}

void GameplaySystems::SetHatchetCount(int count)
{
    const loadout::PowerDefinition* powerDef = m_loadoutCatalog.FindPower(m_killerLoadout.powerId);
    int maxCount = m_tuning.hatchetMaxCount;
    if (powerDef != nullptr)
    {
        const auto it = powerDef->params.find("max_count");
        if (it != powerDef->params.end())
        {
            maxCount = static_cast<int>(it->second);
        }
    }
    m_killerPowerState.hatchetCount = glm::clamp(count, 0, maxCount);
    m_killerPowerState.hatchetMaxCount = maxCount;
    ItemPowerLog("Set hatchet count to " + std::to_string(m_killerPowerState.hatchetCount));
}

void GameplaySystems::RefillHatchets()
{
    SetHatchetCount(m_killerPowerState.hatchetMaxCount);
    AddRuntimeMessage("Hatchets refilled!", 1.0F);
}

int GameplaySystems::GetActiveProjectileCount() const
{
    int count = 0;
    for (const auto& [entity, projectile] : m_world.Projectiles())
    {
        if (projectile.active)
        {
            ++count;
        }
    }
    return count;
}

void GameplaySystems::RenderHatchetDebug(engine::render::Renderer& renderer)
{
    if (!m_hatchetDebugEnabled)
    {
        return;
    }

    // Draw active projectile hitboxes (using small box instead of sphere)
    for (const auto& [entity, projectile] : m_world.Projectiles())
    {
        if (!projectile.active)
        {
            continue;
        }

        // Draw collision box (approximation for sphere)
        const float r = m_tuning.hatchetCollisionRadius;
        renderer.DrawBox(
            projectile.position,
            glm::vec3{r, r, r},
            glm::vec3{1.0F, 0.8F, 0.2F}
        );

        // Draw velocity direction
        const glm::vec3 velEnd = projectile.position + projectile.forward * 0.5F;
        renderer.DrawLine(
            projectile.position,
            velEnd,
            glm::vec3{1.0F, 1.0F, 0.0F}
        );
    }

    // Draw locker interaction ranges
    if (m_killer != 0)
    {
        auto killerTransformIt = m_world.Transforms().find(m_killer);
        if (killerTransformIt != m_world.Transforms().end())
        {
            for (const auto& [entity, locker] : m_world.Lockers())
            {
                auto lockerTransformIt = m_world.Transforms().find(entity);
                if (lockerTransformIt == m_world.Transforms().end())
                {
                    continue;
                }

                const float distance = DistanceXZ(killerTransformIt->second.position, lockerTransformIt->second.position);
                const bool inRange = distance < 2.0F;

                renderer.DrawBox(
                    lockerTransformIt->second.position,
                    locker.halfExtents,
                    inRange ? glm::vec3{0.0F, 1.0F, 0.0F} : glm::vec3{0.5F, 0.5F, 0.5F}
                );
            }
        }
    }
}

void GameplaySystems::RenderHatchetTrajectoryPrediction(engine::render::Renderer& renderer)
{
    // Trajectory prediction is always visible while charging (not debug-only)
    if (!m_killerPowerState.hatchetCharging)
    {
        return;
    }

    if (m_killer == 0)
    {
        return;
    }

    // Calculate predicted trajectory using camera position (matches spawn point)
    const float charge01 = m_killerPowerState.hatchetCharge01;
    const float speed = glm::mix(m_tuning.hatchetThrowSpeedMin, m_tuning.hatchetThrowSpeedMax, charge01);
    const float gravity = glm::mix(m_tuning.hatchetGravityMin, m_tuning.hatchetGravityMax, charge01);

    // Use camera position and forward (center of screen)
    glm::vec3 pos = m_cameraPosition;
    glm::vec3 velocity = m_cameraForward * speed;
    if (glm::length(velocity) < 1.0e-5F)
    {
        velocity = glm::vec3{0.0F, 0.0F, -speed};
    }
    else
    {
        velocity = glm::normalize(velocity) * speed;
    }

    const float dt = 0.05F;
    const int steps = 40; // More steps for longer prediction with drag
    glm::vec3 prevPos = pos;

    for (int i = 0; i < steps; ++i)
    {
        // Apply gravity
        velocity.y -= gravity * dt;
        // Apply air drag (slows down over distance, creates more arc)
        velocity *= m_tuning.hatchetAirDrag;
        pos += velocity * dt;

        // Draw trajectory line (yellow, fading based on distance)
        const float fade = 1.0F - static_cast<float>(i) / static_cast<float>(steps);
        renderer.DrawLine(prevPos, pos, glm::vec3{1.0F, 1.0F, 0.3F} * fade);
        prevPos = pos;

        // Stop if below ground
        if (pos.y < 0.0F)
        {
            break;
        }
    }
}

void GameplaySystems::RenderHatchetProjectiles(engine::render::Renderer& renderer)
{
    // Draw visible hatchet projectiles (always visible, not debug-only)
    for (const auto& [entity, projectile] : m_world.Projectiles())
    {
        if (!projectile.active || projectile.type != engine::scene::ProjectileState::Type::Hatchet)
        {
            continue;
        }

        const glm::vec3 pos = projectile.position;
        const glm::vec3 dir = projectile.forward;
        const float size = 0.15F; // Hatchet visual size

        // Hatchet color (brown/orange)
        const glm::vec3 hatchetColor{0.8F, 0.5F, 0.2F};
        const glm::vec3 highlightColor{1.0F, 0.8F, 0.3F};

        // Draw main body line in direction of travel
        renderer.DrawLine(pos, pos + dir * size * 2.5F, hatchetColor);

        // Draw cross shape for visibility (perpendicular to direction)
        glm::vec3 up{0.0F, 1.0F, 0.0F};
        glm::vec3 right = glm::normalize(glm::cross(dir, up));
        if (glm::length(right) < 0.1F)
        {
            // Dir is nearly vertical, use a different up vector
            up = glm::vec3{0.0F, 0.0F, 1.0F};
            right = glm::normalize(glm::cross(dir, up));
        }
        renderer.DrawLine(pos - right * size * 0.8F, pos + right * size * 0.8F, highlightColor);

        // Draw a small sphere indicator at the center
        const float sphereRadius = 0.06F;
        renderer.DrawBox(pos, glm::vec3{sphereRadius, sphereRadius, sphereRadius}, hatchetColor);
    }
}

// ============================================================================
// Status Effect System
// ============================================================================

void GameplaySystems::ApplyStatusEffect(
    StatusEffectType type,
    const std::string& targetRole,
    float duration,
    float strength,
    const std::string& sourceId)
{
    engine::scene::Entity targetEntity = 0;
    if (targetRole == "survivor" || targetRole == "Survivor")
    {
        targetEntity = m_survivor;
    }
    else if (targetRole == "killer" || targetRole == "Killer")
    {
        targetEntity = m_killer;
    }

    if (targetEntity == 0)
    {
        return;
    }

    // Validate effect type for role
    if (StatusEffect::IsKillerOnly(type) && targetEntity == m_survivor)
    {
        AddRuntimeMessage("Cannot apply killer-only effect to survivor", 1.5F);
        return;
    }
    if (StatusEffect::IsSurvivorOnly(type) && targetEntity == m_killer)
    {
        AddRuntimeMessage("Cannot apply survivor-only effect to killer", 1.5F);
        return;
    }

    StatusEffect effect;
    effect.type = type;
    effect.duration = duration;
    effect.remainingTime = duration;
    effect.strength = strength;
    effect.sourceId = sourceId;
    effect.infinite = (duration <= 0.0F);

    m_statusEffectManager.ApplyEffect(targetEntity, effect);

    const char* typeName = StatusEffect::TypeToName(type);
    AddRuntimeMessage("Applied " + std::string(typeName) + " to " + targetRole, 1.5F);
}

void GameplaySystems::RemoveStatusEffect(StatusEffectType type, const std::string& targetRole)
{
    engine::scene::Entity targetEntity = 0;
    if (targetRole == "survivor" || targetRole == "Survivor")
    {
        targetEntity = m_survivor;
    }
    else if (targetRole == "killer" || targetRole == "Killer")
    {
        targetEntity = m_killer;
    }

    if (targetEntity == 0)
    {
        return;
    }

    m_statusEffectManager.RemoveEffect(targetEntity, type);
}

bool GameplaySystems::IsKillerUndetectable() const
{
    if (m_killer == 0) return false;
    return m_statusEffectManager.IsUndetectable(m_killer);
}

bool GameplaySystems::IsSurvivorExposed() const
{
    if (m_survivor == 0) return false;
    return m_statusEffectManager.IsExposed(m_survivor);
}

bool GameplaySystems::IsSurvivorExhausted() const
{
    if (m_survivor == 0) return false;
    return m_statusEffectManager.IsExhausted(m_survivor);
}

std::string GameplaySystems::StatusEffectDump() const
{
    std::ostringstream oss;
    oss << "=== Status Effects ===\n";

    // Killer effects
    if (m_killer != 0)
    {
        oss << "Killer:\n";
        const auto killerEffects = m_statusEffectManager.GetActiveEffects(m_killer);
        if (killerEffects.empty())
        {
            oss << "  (none)\n";
        }
        else
        {
            for (const auto& effect : killerEffects)
            {
                oss << "  " << StatusEffect::TypeToName(effect.type);
                oss << " (source: " << effect.sourceId << ")";
                if (effect.infinite)
                {
                    oss << " [infinite]";
                }
                else
                {
                    oss << " [" << effect.remainingTime << "s remaining]";
                }
                if (effect.strength != 0.0F)
                {
                    oss << " strength=" << effect.strength;
                }
                oss << "\n";
            }
        }
    }

    // Survivor effects
    if (m_survivor != 0)
    {
        oss << "Survivor:\n";
        const auto survivorEffects = m_statusEffectManager.GetActiveEffects(m_survivor);
        if (survivorEffects.empty())
        {
            oss << "  (none)\n";
        }
        else
        {
            for (const auto& effect : survivorEffects)
            {
                oss << "  " << StatusEffect::TypeToName(effect.type);
                oss << " (source: " << effect.sourceId << ")";
                if (effect.infinite)
                {
                    oss << " [infinite]";
                }
                else
                {
                    oss << " [" << effect.remainingTime << "s remaining]";
                }
                if (effect.strength != 0.0F)
                {
                    oss << " strength=" << effect.strength;
                }
                oss << "\n";
            }
        }
    }

    return oss.str();
}

// ============================================================================
// Chainsaw Sprint Power System Implementation
// ============================================================================

void GameplaySystems::LoadChainsawSprintConfig()
{
    // Load config from power definition params (already loaded by loadout catalog)
    const loadout::PowerDefinition* powerDef = m_loadoutCatalog.FindPower(m_killerLoadout.powerId);
    if (powerDef == nullptr || powerDef->id != "chainsaw_sprint")
    {
        ItemPowerLog("ChainsawSprint: Using default config (power not equipped)");
        return;
    }

    // Helper to read float from power params
    const auto readParam = [&powerDef](const std::string& key, float fallback) -> float {
        if (powerDef == nullptr)
        {
            return fallback;
        }
        const auto it = powerDef->params.find(key);
        return it != powerDef->params.end() ? it->second : fallback;
    };

    m_chainsawConfig.chargeTime = readParam("charge_time", 2.5F);
    m_chainsawConfig.sprintSpeedMultiplier = readParam("sprint_speed_multiplier", 2.4F);
    // REMOVED: maxSprintDuration - sprint continues until collision/RMB release/hit
    m_chainsawConfig.turnRateDegreesPerSec = readParam("turn_rate_degrees_per_sec", 90.0F);
    m_chainsawConfig.recoveryDuration = readParam("recovery_duration", 1.5F);
    m_chainsawConfig.collisionRecoveryDuration = readParam("collision_recovery_duration", 2.5F);
    m_chainsawConfig.overheatMax = readParam("overheat_max", 100.0F);
    m_chainsawConfig.overheatPerSecondCharge = readParam("overheat_per_second_charge", 15.0F);
    m_chainsawConfig.overheatPerSecondSprint = readParam("overheat_per_second_sprint", 25.0F);
    m_chainsawConfig.overheatCooldownRate = readParam("overheat_cooldown_rate", 10.0F);
    m_chainsawConfig.overheatThreshold = readParam("overheat_threshold", 20.0F);
    m_chainsawConfig.fovBoost = readParam("fov_boost", 15.0F);
    m_chainsawConfig.collisionRaycastDistance = readParam("collision_raycast_distance", 2.0F);
    m_chainsawConfig.survivorHitRadius = readParam("survivor_hit_radius", 1.2F);

    // New turn rate phases
    m_chainsawConfig.turnBoostWindow = readParam("turn_boost_window", 0.5F);
    m_chainsawConfig.turnBoostRate = readParam("turn_boost_rate", 270.0F);
    m_chainsawConfig.turnRestrictedRate = readParam("turn_restricted_rate", 45.0F);

    // New recovery durations
    m_chainsawConfig.recoveryHitDuration = readParam("recovery_hit_duration", 0.5F);
    m_chainsawConfig.recoveryCancelDuration = readParam("recovery_cancel_duration", 0.5F);

    // New overheat buff system
    m_chainsawConfig.overheatBuffThreshold = readParam("overheat_buff_threshold", 100.0F);
    m_chainsawConfig.overheatChargeBonus = readParam("overheat_charge_bonus", 0.2F);
    m_chainsawConfig.overheatSpeedBonus = readParam("overheat_speed_bonus", 0.1F);
    m_chainsawConfig.overheatTurnBonus = readParam("overheat_turn_bonus", 0.3F);

    // Movement during charging
    m_chainsawConfig.chargeSlowdownMultiplier = readParam("charge_slowdown_multiplier", 0.3F);

    ItemPowerLog("ChainsawSprint: Config loaded from power definition");
}

void GameplaySystems::ApplyChainsawConfig(
    float chargeTime,
    float sprintSpeedMultiplier,
    float turnBoostWindow,
    float turnBoostRate,
    float turnRestrictedRate,
    float collisionRecoveryDuration,
    float recoveryHitDuration,
    float recoveryCancelDuration,
    float overheatPerSecondCharge,
    float overheatPerSecondSprint,
    float overheatCooldownRate,
    float overheatBuffThreshold,
    float overheatChargeBonus,
    float overheatSpeedBonus,
    float overheatTurnBonus,
    float collisionRaycastDistance,
    float survivorHitRadius,
    float chargeSlowdownMultiplier
)
{
    m_chainsawConfig.chargeTime = chargeTime;
    m_chainsawConfig.sprintSpeedMultiplier = sprintSpeedMultiplier;
    m_chainsawConfig.turnBoostWindow = turnBoostWindow;
    m_chainsawConfig.turnBoostRate = turnBoostRate;
    m_chainsawConfig.turnRestrictedRate = turnRestrictedRate;
    m_chainsawConfig.collisionRecoveryDuration = collisionRecoveryDuration;
    m_chainsawConfig.recoveryHitDuration = recoveryHitDuration;
    m_chainsawConfig.recoveryCancelDuration = recoveryCancelDuration;
    m_chainsawConfig.overheatPerSecondCharge = overheatPerSecondCharge;
    m_chainsawConfig.overheatPerSecondSprint = overheatPerSecondSprint;
    m_chainsawConfig.overheatCooldownRate = overheatCooldownRate;
    m_chainsawConfig.overheatBuffThreshold = overheatBuffThreshold;
    m_chainsawConfig.overheatChargeBonus = overheatChargeBonus;
    m_chainsawConfig.overheatSpeedBonus = overheatSpeedBonus;
    m_chainsawConfig.overheatTurnBonus = overheatTurnBonus;
    m_chainsawConfig.collisionRaycastDistance = collisionRaycastDistance;
    m_chainsawConfig.survivorHitRadius = survivorHitRadius;
    m_chainsawConfig.chargeSlowdownMultiplier = chargeSlowdownMultiplier;

    ItemPowerLog("ChainsawSprint: Config applied from settings UI");
}

void GameplaySystems::UpdateChainsawSprintPowerSystem(const RoleCommand& killerCommand, float fixedDt)
{
    if (m_killer == 0)
    {
        return;
    }

    const loadout::PowerDefinition* powerDef = m_loadoutCatalog.FindPower(m_killerLoadout.powerId);
    if (powerDef == nullptr || powerDef->id != "chainsaw_sprint")
    {
        return;
    }

    // Load config on first use
    static bool configLoaded = false;
    if (!configLoaded)
    {
        LoadChainsawSprintConfig();
        configLoaded = true;
    }

    auto killerTransformIt = m_world.Transforms().find(m_killer);
    auto killerActorIt = m_world.Actors().find(m_killer);
    if (killerTransformIt == m_world.Transforms().end() || killerActorIt == m_world.Actors().end())
    {
        return;
    }

    glm::vec3& killerPos = killerTransformIt->second.position;
    glm::vec3& killerForward = killerTransformIt->second.forward;
    glm::vec3& killerVelocity = killerActorIt->second.velocity;

    const bool canUsePower = m_killerAttackState == KillerAttackState::Idle &&
                             killerActorIt->second.stunTimer <= 0.0F &&
                             m_survivorState != SurvivorHealthState::Carried;

    // Check if overheat buff is active
    const bool overheatBuffed = m_killerPowerState.chainsawOverheat >= m_chainsawConfig.overheatBuffThreshold;

    // Helper to transition to recovery with cause tracking
    const auto transitionToRecovery = [&](bool fromCollision, bool fromHit) {
        m_killerPowerState.chainsawState = ChainsawSprintState::Recovery;
        m_killerPowerState.chainsawRecoveryTimer = 0.0F;
        m_killerPowerState.chainsawSprintTimer = 0.0F;
        m_killerPowerState.chainsawChargeTimer = 0.0F;
        m_killerPowerState.chainsawCurrentSpeed = 0.0F;
        m_killerPowerState.chainsawSprintRequiresRelease = true;
        m_killerPowerState.chainsawInTurnBoostWindow = false;
        m_killerPowerState.chainsawSprintTurnBoostTimer = 0.0F;

        // Track recovery cause
        m_killerPowerState.chainsawRecoveryWasCollision = fromCollision;
        m_killerPowerState.chainsawRecoveryWasHit = fromHit;

        if (fromCollision)
        {
            ItemPowerLog("ChainsawSprint: Collision! Entering extended recovery (1.5s)");
        }
        else if (fromHit)
        {
            ItemPowerLog("ChainsawSprint: Survivor hit! Entering recovery (0.5s)");
        }
        else
        {
            ItemPowerLog("ChainsawSprint: Cancelled! Entering recovery (0.5s)");
        }
    };

    // Reset requires release flag when RMB not held
    if (!killerCommand.useAltHeld)
    {
        m_killerPowerState.chainsawSprintRequiresRelease = false;
    }

    // State machine
    switch (m_killerPowerState.chainsawState)
    {
        case ChainsawSprintState::Idle:
        {
            // Heat decay
            m_killerPowerState.chainsawOverheat = std::max(
                0.0F,
                m_killerPowerState.chainsawOverheat - m_chainsawConfig.overheatCooldownRate * fixedDt
            );

            // Can start charging if RMB held (no overheat blocking - buff system instead)
            if (killerCommand.useAltHeld &&
                !m_killerPowerState.chainsawSprintRequiresRelease &&
                canUsePower)
            {
                m_killerPowerState.chainsawState = ChainsawSprintState::Charging;
                m_killerPowerState.chainsawChargeTimer = 0.0F;
                ItemPowerLog("ChainsawSprint: Started charging" + std::string(overheatBuffed ? " (BUFFED)" : ""));
            }
            break;
        }

        case ChainsawSprintState::Charging:
        {
            // Cancel conditions (RMB released or cannot use power)
            if (!killerCommand.useAltHeld || !canUsePower || m_killerPowerState.chainsawSprintRequiresRelease)
            {
                // Cancelled - back to idle (partial charge lost)
                m_killerPowerState.chainsawState = ChainsawSprintState::Idle;
                m_killerPowerState.chainsawChargeTimer = 0.0F;
                ItemPowerLog("ChainsawSprint: Charge cancelled");
                break;
            }

            // Apply overheat charge bonus
            float chargeRate = 1.0F;
            if (overheatBuffed)
            {
                chargeRate += m_chainsawConfig.overheatChargeBonus;
            }

            // Charge progress
            m_killerPowerState.chainsawChargeTimer += fixedDt * chargeRate;

            // Heat buildup while charging - BUT if already buffed, only decay
            if (overheatBuffed)
            {
                // When buffed, heat only decays until it reaches 0
                m_killerPowerState.chainsawOverheat = std::max(
                    0.0F,
                    m_killerPowerState.chainsawOverheat - m_chainsawConfig.overheatCooldownRate * fixedDt
                );
            }
            else
            {
                // Normal heat buildup
                m_killerPowerState.chainsawOverheat = std::min(
                    m_chainsawConfig.overheatBuffThreshold,
                    m_killerPowerState.chainsawOverheat + m_chainsawConfig.overheatPerSecondCharge * fixedDt
                );
            }

            // AUTO-SPRINT when fully charged (no release needed)
            if (m_killerPowerState.chainsawChargeTimer >= m_chainsawConfig.chargeTime)
            {
                m_killerPowerState.chainsawState = ChainsawSprintState::Sprinting;
                m_killerPowerState.chainsawSprintTimer = 0.0F;
                m_killerPowerState.chainsawHitThisSprint = false;
                m_killerPowerState.chainsawCollisionThisSprint = false;
                m_killerPowerState.chainsawSprintRequiresRelease = true;
                m_killerPowerState.chainsawSprintTurnBoostTimer = 0.0F;
                m_killerPowerState.chainsawInTurnBoostWindow = true;
                ItemPowerLog("ChainsawSprint: Sprint started!" + std::string(overheatBuffed ? " (BUFFED)" : ""));
                break;
            }

            // Reduced movement while charging (from config)
            killerVelocity *= m_chainsawConfig.chargeSlowdownMultiplier;
            break;
        }

        case ChainsawSprintState::Sprinting:
        {
            m_killerPowerState.chainsawSprintTimer += fixedDt;

            // Update turn boost window
            m_killerPowerState.chainsawSprintTurnBoostTimer += fixedDt;
            if (m_killerPowerState.chainsawSprintTurnBoostTimer >= m_chainsawConfig.turnBoostWindow)
            {
                m_killerPowerState.chainsawInTurnBoostWindow = false;
            }

            // Heat buildup while sprinting - BUT if already buffed, only decay
            if (overheatBuffed)
            {
                // When buffed, heat only decays until it reaches 0
                m_killerPowerState.chainsawOverheat = std::max(
                    0.0F,
                    m_killerPowerState.chainsawOverheat - m_chainsawConfig.overheatCooldownRate * fixedDt
                );
            }
            else
            {
                // Normal heat buildup
                m_killerPowerState.chainsawOverheat = std::min(
                    m_chainsawConfig.overheatBuffThreshold,
                    m_killerPowerState.chainsawOverheat + m_chainsawConfig.overheatPerSecondSprint * fixedDt
                );
            }

            // Calculate sprint speed with overheat bonus
            float speedMult = m_chainsawConfig.sprintSpeedMultiplier;
            if (overheatBuffed)
            {
                speedMult += m_chainsawConfig.overheatSpeedBonus;
            }

            const float baseSpeed = m_tuning.killerMoveSpeed;
            const float sprintSpeed = baseSpeed * speedMult;
            m_killerPowerState.chainsawCurrentSpeed = sprintSpeed;

            // Move forward at sprint speed - use HORIZONTAL forward only (no flying!)
            // This ensures the killer stays on the ground even if camera pitch is non-zero
            glm::vec3 forwardXZ = glm::normalize(glm::vec3(killerForward.x, 0.0F, killerForward.z));
            killerVelocity = forwardXZ * sprintSpeed;

            // === SURVIVOR HIT DETECTION (FIRST - before wall collision) ===
            // Must check survivor hit BEFORE wall collision, otherwise raycast hitting
            // a wall at 2.0m would prevent detecting a survivor at 1.5m
            if (m_survivor != 0 && !m_killerPowerState.chainsawHitThisSprint)
            {
                auto survivorTransformIt = m_world.Transforms().find(m_survivor);
                if (survivorTransformIt != m_world.Transforms().end())
                {
                    const glm::vec3 survivorPos = survivorTransformIt->second.position;

                    // Use XZ (horizontal) distance for hit detection, like DBD
                    const float distXZ = DistanceXZ(killerPos, survivorPos);

                    // Hit radius includes both capsule radii for generous detection
                    const float hitRadius = m_chainsawConfig.survivorHitRadius
                        + m_tuning.killerCapsuleRadius
                        + m_tuning.survivorCapsuleRadius;

                    if (distXZ <= hitRadius)
                    {
                        // For direction check, use horizontal direction only
                        const glm::vec3 toSurvivorXZ = glm::normalize(glm::vec3(
                            survivorPos.x - killerPos.x,
                            0.0F,
                            survivorPos.z - killerPos.z
                        ));
                        const glm::vec3 killerForwardXZ = glm::normalize(glm::vec3(
                            killerForward.x,
                            0.0F,
                            killerForward.z
                        ));
                        const float dot = glm::dot(killerForwardXZ, toSurvivorXZ);

                        if (dot > 0.5F) // ~60 degree cone in front
                        {
                            m_killerPowerState.chainsawHitThisSprint = true;

                            // Apply Downed state (instant down - force=true to bypass state checks)
                            // Chainsaw instantly downs from any state (Healthy, Injured, etc.)
                            if (m_survivorState != SurvivorHealthState::Downed &&
                                m_survivorState != SurvivorHealthState::Dead &&
                                m_survivorState != SurvivorHealthState::Hooked &&
                                m_survivorState != SurvivorHealthState::Carried)
                            {
                                SetSurvivorState(SurvivorHealthState::Downed, "chainsaw_hit", true);

                                // Blood FX
                                const engine::fx::FxNetMode netMode = m_networkAuthorityMode
                                    ? engine::fx::FxNetMode::ServerBroadcast
                                    : engine::fx::FxNetMode::Local;
                                SpawnGameplayFx("fx_blood_splatter_large", survivorPos, killerForward, netMode);
                                AddRuntimeMessage("CHAINSAW DOWN!", 2.0F);
                                ItemPowerLog("ChainsawSprint: Survivor hit and downed!");
                            }

                            transitionToRecovery(false, true);
                            break;
                        }
                    }
                }
            }

            // === COLLISION DETECTION (SECOND - after survivor check) ===
            const glm::vec3 rayOrigin = killerPos + glm::vec3(0.0F, 0.5F, 0.0F);
            const glm::vec3 rayEnd = rayOrigin + killerForward * m_chainsawConfig.collisionRaycastDistance;
            const auto hitOpt = m_physics.RaycastNearest(rayOrigin, rayEnd);

            if (hitOpt.has_value())
            {
                // Wall collision - longer recovery (1.5s)
                m_killerPowerState.chainsawCollisionThisSprint = true;
                transitionToRecovery(true, false);
                break;
            }

            // === End Conditions ===
            // Manual cancel (release RMB) - 0.5s recovery
            if (killerCommand.useAltReleased)
            {
                transitionToRecovery(false, false);
                break;
            }

            // No max duration - sprint continues until collision/RMB release/hit
            break;
        }

        case ChainsawSprintState::Recovery:
        {
            // Stunned - no movement
            killerVelocity = glm::vec3(0.0F);

            m_killerPowerState.chainsawRecoveryTimer += fixedDt;

            // Heat decay during recovery
            m_killerPowerState.chainsawOverheat = std::max(
                0.0F,
                m_killerPowerState.chainsawOverheat - m_chainsawConfig.overheatCooldownRate * fixedDt
            );

            // Variable recovery duration based on cause
            float recoveryDuration = m_chainsawConfig.recoveryCancelDuration; // 0.5s default
            if (m_killerPowerState.chainsawRecoveryWasCollision)
            {
                recoveryDuration = m_chainsawConfig.collisionRecoveryDuration; // 1.5s for collision
            }
            else if (m_killerPowerState.chainsawRecoveryWasHit)
            {
                recoveryDuration = m_chainsawConfig.recoveryHitDuration; // 0.5s for hit
            }

            if (m_killerPowerState.chainsawRecoveryTimer >= recoveryDuration)
            {
                m_killerPowerState.chainsawState = ChainsawSprintState::Idle;
                m_killerPowerState.chainsawRecoveryWasCollision = false;
                m_killerPowerState.chainsawRecoveryWasHit = false;
                ItemPowerLog("ChainsawSprint: Recovery complete");
            }
            break;
        }
    }
}

void GameplaySystems::RenderChainsawDebug(engine::render::Renderer& renderer)
{
    if (!m_chainsawDebugEnabled || m_killer == 0)
    {
        return;
    }

    const loadout::PowerDefinition* powerDef = m_loadoutCatalog.FindPower(m_killerLoadout.powerId);
    if (powerDef == nullptr || powerDef->id != "chainsaw_sprint")
    {
        return;
    }

    auto killerTransformIt = m_world.Transforms().find(m_killer);
    if (killerTransformIt == m_world.Transforms().end())
    {
        return;
    }

    const glm::vec3& killerPos = killerTransformIt->second.position;
    const glm::vec3& killerForward = killerTransformIt->second.forward;

    // Draw forward collision raycast line (red)
    const glm::vec3 rayStart = killerPos + glm::vec3(0.0F, 0.5F, 0.0F);
    const glm::vec3 rayEnd = rayStart + killerForward * m_chainsawConfig.collisionRaycastDistance;
    renderer.DrawLine(rayStart, rayEnd, glm::vec3(1.0F, 0.2F, 0.2F));

    // Draw survivor hit radius indicator (yellow circle on ground)
    const glm::vec3 hitCenter = killerPos + killerForward * 0.6F;
    const float hitRadius = m_chainsawConfig.survivorHitRadius;
    renderer.DrawCircle(hitCenter, hitRadius, 16, glm::vec3(1.0F, 1.0F, 0.2F));
}

void GameplaySystems::SetChainsawOverheat(float value)
{
    m_killerPowerState.chainsawOverheat = glm::clamp(value, 0.0F, m_chainsawConfig.overheatMax);
    ItemPowerLog("ChainsawSprint: Overheat set to " + std::to_string(m_killerPowerState.chainsawOverheat));
}

void GameplaySystems::ResetChainsawState()
{
    m_killerPowerState.chainsawState = ChainsawSprintState::Idle;
    m_killerPowerState.chainsawChargeTimer = 0.0F;
    m_killerPowerState.chainsawSprintTimer = 0.0F;
    m_killerPowerState.chainsawRecoveryTimer = 0.0F;
    m_killerPowerState.chainsawOverheat = 0.0F;
    m_killerPowerState.chainsawCurrentSpeed = 0.0F;
    m_killerPowerState.chainsawHitThisSprint = false;
    m_killerPowerState.chainsawCollisionThisSprint = false;
    m_killerPowerState.chainsawSprintRequiresRelease = false;
    m_killerPowerState.chainsawSprintTurnBoostTimer = 0.0F;
    m_killerPowerState.chainsawInTurnBoostWindow = false;
    m_killerPowerState.chainsawRecoveryWasCollision = false;
    m_killerPowerState.chainsawRecoveryWasHit = false;
    ItemPowerLog("ChainsawSprint: State reset to Idle");
}

// ============================================================================
// Nurse Blink Power System Implementation
// ============================================================================

void GameplaySystems::LoadNurseBlinkConfig()
{
    const loadout::PowerDefinition* powerDef = m_loadoutCatalog.FindPower(m_killerLoadout.powerId);
    if (powerDef == nullptr || powerDef->id != "nurse_blink")
    {
        ItemPowerLog("NurseBlink: Using default config (power not equipped)");
        return;
    }

    const auto readParam = [&powerDef](const std::string& key, float fallback) -> float {
        if (powerDef == nullptr)
        {
            return fallback;
        }
        const auto it = powerDef->params.find(key);
        return it != powerDef->params.end() ? it->second : fallback;
    };

    m_blinkConfig.maxCharges = static_cast<int>(readParam("max_charges", 2.0F));
    m_blinkConfig.chargeRegenSeconds = readParam("charge_regen_seconds", 3.0F);
    m_blinkConfig.minBlinkDistance = readParam("min_blink_distance", 2.0F);
    m_blinkConfig.maxBlinkDistance = readParam("max_blink_distance", 20.0F);
    m_blinkConfig.chargeTimeToMax = readParam("charge_time_to_max", 2.0F);
    m_blinkConfig.chargeMoveSpeedMultiplier = readParam("charge_move_speed_multiplier", 0.5F);
    m_blinkConfig.blinkTravelTime = readParam("blink_travel_time", 0.15F);
    m_blinkConfig.chainWindowSeconds = readParam("chain_window_seconds", 1.5F);
    m_blinkConfig.fatigueBaseSeconds = readParam("fatigue_base_seconds", 2.0F);
    m_blinkConfig.fatiguePerBlinkUsedSeconds = readParam("fatigue_per_blink_used_seconds", 0.5F);
    m_blinkConfig.fatigueMoveSpeedMultiplier = readParam("fatigue_move_speed_multiplier", 0.5F);
    m_blinkConfig.blinkAttackRange = readParam("blink_attack_range", 4.5F);
    m_blinkConfig.blinkAttackAngleDegrees = readParam("blink_attack_angle_degrees", 90.0F);
    m_blinkConfig.blinkAttackWindupSeconds = readParam("blink_attack_windup_seconds", 0.2F);
    m_blinkConfig.blinkAttackLungeMultiplier = readParam("blink_attack_lunge_multiplier", 2.0F);
    m_blinkConfig.endpointSlideAttempts = static_cast<int>(readParam("endpoint_slide_attempts", 8.0F));
    m_blinkConfig.endpointSlideStep = readParam("endpoint_slide_step", 0.3F);

    // Sync max charges to runtime state
    m_killerPowerState.blinkMaxCharges = m_blinkConfig.maxCharges;
    m_killerPowerState.blinkCharges = glm::min(m_killerPowerState.blinkCharges, m_blinkConfig.maxCharges);

    ItemPowerLog("NurseBlink: Config loaded from power definition");
}

bool GameplaySystems::ResolveBlinkEndpoint(const glm::vec3& start, const glm::vec3& requested, glm::vec3& out)
{
    const float radius = m_tuning.killerCapsuleRadius;
    const float height = m_tuning.killerCapsuleHeight;
    const glm::vec3 direction = glm::normalize(requested - start);
    const float requestedDistance = glm::length(requested - start);

    // Get the Y level we expect to be at (from start position)
    const float expectedGroundY = start.y;

    // Helper to check if a point is inside any solid box
    const auto isPointInSolid = [this](const glm::vec3& point) -> bool {
        for (const auto& solid : m_physics.Solids())
        {
            const glm::vec3 min = solid.center - solid.halfExtents;
            const glm::vec3 max = solid.center + solid.halfExtents;

            if (point.x >= min.x && point.x <= max.x &&
                point.y >= min.y && point.y <= max.y &&
                point.z >= min.z && point.z <= max.z)
            {
                return true;
            }
        }
        return false;
    };

    // Helper to check if capsule intersects any solid
    const auto capsuleIntersectsSolid = [this, radius, height, &isPointInSolid](const glm::vec3& groundPos) -> bool {
        // Check several points within the capsule volume
        const float halfHeight = height * 0.5F;

        // Check center and corners of capsule
        const glm::vec3 checkPoints[] = {
            groundPos + glm::vec3(0.0F, halfHeight, 0.0F),  // Center
            groundPos + glm::vec3(radius * 0.7F, halfHeight, 0.0F),
            groundPos + glm::vec3(-radius * 0.7F, halfHeight, 0.0F),
            groundPos + glm::vec3(0.0F, halfHeight, radius * 0.7F),
            groundPos + glm::vec3(0.0F, halfHeight, -radius * 0.7F),
            groundPos + glm::vec3(0.0F, 0.1F, 0.0F),  // Near feet
            groundPos + glm::vec3(0.0F, height - 0.1F, 0.0F),  // Near head
        };

        for (const auto& point : checkPoints)
        {
            if (isPointInSolid(point))
            {
                return true;
            }
        }
        return false;
    };

    // Helper to find valid ground at a position
    // Returns ground position if valid, nullopt otherwise
    const auto findValidGround = [this, &expectedGroundY, &isPointInSolid](const glm::vec3& pos) -> std::optional<glm::vec3> {
        // Raycast from above to find ground
        const glm::vec3 rayStart = pos + glm::vec3(0.0F, 5.0F, 0.0F);
        const glm::vec3 rayEnd = pos - glm::vec3(0.0F, 5.0F, 0.0F);
        const auto hit = m_physics.RaycastNearest(rayStart, rayEnd);

        if (!hit.has_value())
        {
            return std::nullopt;  // No ground found
        }

        // Check if ground normal is valid (pointing up, not the underside of something)
        const float upDot = glm::dot(hit->normal, glm::vec3(0.0F, 1.0F, 0.0F));
        if (upDot < 0.7F)  // Allow some slope but reject steep/underside surfaces
        {
            return std::nullopt;
        }

        // Check if ground level is reasonable (not too far from expected level)
        const float groundY = hit->position.y;
        if (std::abs(groundY - expectedGroundY) > 3.0F)
        {
            return std::nullopt;  // Ground too far from expected level (might be under map or on roof)
        }

        // Make sure we're not inside a solid at the ground position
        const glm::vec3 groundPos = glm::vec3(pos.x, groundY, pos.z);
        if (isPointInSolid(groundPos + glm::vec3(0.0F, 0.1F, 0.0F)))
        {
            return std::nullopt;
        }

        return groundPos;
    };

    // Helper to check if a position is fully valid
    const auto isValidPosition = [&capsuleIntersectsSolid](const glm::vec3& groundPos) -> bool {
        return !capsuleIntersectsSolid(groundPos);
    };

    // Sample positions along the blink path from far to near
    const int numSamples = 50;
    const float stepSize = requestedDistance / static_cast<float>(numSamples);

    glm::vec3 bestValidPos = start;
    float bestDistance = 0.0F;

    // Try positions along the direct path
    for (int i = numSamples; i >= 1; --i)
    {
        const float testDistance = static_cast<float>(i) * stepSize;
        const glm::vec3 testPos = start + direction * testDistance;

        const auto groundPos = findValidGround(testPos);
        if (!groundPos.has_value())
        {
            continue;
        }

        if (isValidPosition(*groundPos))
        {
            bestValidPos = *groundPos;
            bestDistance = testDistance;
            break;
        }
    }

    // If we found a valid position along the path, use it
    if (bestDistance >= m_blinkConfig.minBlinkDistance)
    {
        out = bestValidPos;
        return true;
    }

    // Try perpendicular offsets at various distances
    const glm::vec3 perpendicular = glm::vec3(-direction.z, 0.0F, direction.x);
    const float perpendicularOffsets[] = {-2.0F, -1.5F, -1.0F, -0.5F, 0.5F, 1.0F, 1.5F, 2.0F};

    for (int i = numSamples; i >= 1; --i)
    {
        const float testDistance = static_cast<float>(i) * stepSize;

        for (float perpOffset : perpendicularOffsets)
        {
            const glm::vec3 testPos = start + direction * testDistance + perpendicular * perpOffset;
            const auto groundPos = findValidGround(testPos);

            if (groundPos.has_value() && isValidPosition(*groundPos))
            {
                if (testDistance >= m_blinkConfig.minBlinkDistance)
                {
                    out = *groundPos;
                    return true;
                }
            }
        }
    }

    // Try minimum distance along path
    const glm::vec3 minDistPos = start + direction * m_blinkConfig.minBlinkDistance;
    const auto groundPos = findValidGround(minDistPos);
    if (groundPos.has_value() && isValidPosition(*groundPos))
    {
        out = *groundPos;
        return true;
    }

    // Last resort: use start position (no teleport)
    out = start;
    ItemPowerLog("NurseBlink: No valid endpoint found, staying in place");
    return false;
}

void GameplaySystems::UpdateNurseBlinkPowerSystem(const RoleCommand& killerCommand, float fixedDt)
{
    if (m_killer == 0)
    {
        return;
    }

    const loadout::PowerDefinition* powerDef = m_loadoutCatalog.FindPower(m_killerLoadout.powerId);
    if (powerDef == nullptr || powerDef->id != "nurse_blink")
    {
        return;
    }

    // Load config on first use
    static bool configLoaded = false;
    if (!configLoaded)
    {
        LoadNurseBlinkConfig();
        configLoaded = true;
    }

    auto killerTransformIt = m_world.Transforms().find(m_killer);
    auto killerActorIt = m_world.Actors().find(m_killer);
    if (killerTransformIt == m_world.Transforms().end() || killerActorIt == m_world.Actors().end())
    {
        return;
    }

    glm::vec3& killerPos = killerTransformIt->second.position;
    glm::vec3& killerForward = killerTransformIt->second.forward;
    glm::vec3& killerVelocity = killerActorIt->second.velocity;

    const bool canUsePower = m_killerAttackState == KillerAttackState::Idle &&
                             killerActorIt->second.stunTimer <= 0.0F &&
                             m_survivorState != SurvivorHealthState::Carried;

    // Reset requires release flag when RMB not held
    if (!killerCommand.useAltHeld)
    {
        m_killerPowerState.blinkRequiresRelease = false;
    }

    // Helper to enter fatigue state
    const auto enterFatigue = [this]() {
        m_killerPowerState.blinkState = NurseBlinkState::Fatigue;
        const float fatigueDuration = m_blinkConfig.fatigueBaseSeconds +
            (static_cast<float>(m_killerPowerState.blinksUsedThisChain) * m_blinkConfig.fatiguePerBlinkUsedSeconds);
        m_killerPowerState.blinkFatigueTimer = 0.0F;
        m_killerPowerState.blinkChainWindowTimer = 0.0F;
        m_killerPowerState.blinkChargeTimer = 0.0F;
        m_killerPowerState.blinkCharge01 = 0.0F;
        m_killerPowerState.blinkAttackInProgress = false;
        m_killerPowerState.blinkIsChainCharge = false;
        m_killerPowerState.blinkChainChargeRemaining = 0.0F;
        ItemPowerLog("NurseBlink: Entering fatigue (" + std::to_string(fatigueDuration) + "s) after " +
                     std::to_string(m_killerPowerState.blinksUsedThisChain) + " blink(s)");
    };

    // Charge regeneration (only when not in active blink sequence)
    if (m_killerPowerState.blinkState == NurseBlinkState::Idle ||
        m_killerPowerState.blinkState == NurseBlinkState::Fatigue)
    {
        if (m_killerPowerState.blinkCharges < m_killerPowerState.blinkMaxCharges)
        {
            m_killerPowerState.blinkChargeRegenTimer += fixedDt;
            if (m_killerPowerState.blinkChargeRegenTimer >= m_blinkConfig.chargeRegenSeconds)
            {
                m_killerPowerState.blinkChargeRegenTimer = 0.0F;
                ++m_killerPowerState.blinkCharges;
                ItemPowerLog("NurseBlink: Charge regenerated (" + std::to_string(m_killerPowerState.blinkCharges) +
                             "/" + std::to_string(m_killerPowerState.blinkMaxCharges) + ")");
            }
        }
    }

    // State machine
    switch (m_killerPowerState.blinkState)
    {
        case NurseBlinkState::Idle:
        {
            // Can start charging if RMB held, has charges, and can use power
            if (killerCommand.useAltHeld &&
                !m_killerPowerState.blinkRequiresRelease &&
                m_killerPowerState.blinkCharges > 0 &&
                canUsePower)
            {
                m_killerPowerState.blinkState = NurseBlinkState::ChargingBlink;
                m_killerPowerState.blinkChargeTimer = 0.0F;
                m_killerPowerState.blinkCharge01 = 0.0F;
                m_killerPowerState.blinkStartPosition = killerPos;
                m_killerPowerState.blinkIsChainCharge = false;  // Not a chain charge
                m_killerPowerState.blinkChainChargeRemaining = 0.0F;
                ItemPowerLog("NurseBlink: Started charging");
            }
            break;
        }

        case NurseBlinkState::ChargingBlink:
        {
            // If this is a chain charge, check if time has expired
            if (m_killerPowerState.blinkIsChainCharge)
            {
                m_killerPowerState.blinkChainChargeRemaining -= fixedDt;
                if (m_killerPowerState.blinkChainChargeRemaining <= 0.0F)
                {
                    // Chain window expired while charging - enter fatigue
                    ItemPowerLog("NurseBlink: Chain window expired while charging");
                    enterFatigue();
                    break;
                }
            }

            // Check for release FIRST - this is the primary action
            if (killerCommand.useAltReleased && canUsePower && !m_killerPowerState.blinkRequiresRelease)
            {
                // Calculate blink distance based on charge
                const float blinkDistance = m_blinkConfig.minBlinkDistance +
                    m_killerPowerState.blinkCharge01 * (m_blinkConfig.maxBlinkDistance - m_blinkConfig.minBlinkDistance);

                // Use horizontal forward only (no flying)
                const glm::vec3 forwardXZ = glm::normalize(glm::vec3(killerForward.x, 0.0F, killerForward.z));
                const glm::vec3 requestedTarget = killerPos + forwardXZ * blinkDistance;

                // Resolve endpoint (always returns valid position, even if fallback)
                glm::vec3 resolvedTarget;
                (void)ResolveBlinkEndpoint(killerPos, requestedTarget, resolvedTarget);

                // Store blink info
                m_killerPowerState.blinkStartPosition = killerPos;
                m_killerPowerState.blinkTargetPosition = resolvedTarget;
                m_killerPowerState.blinkTravelDirection = glm::normalize(resolvedTarget - killerPos);
                m_killerPowerState.blinkTravelTimer = 0.0F;

                // Consume a charge
                --m_killerPowerState.blinkCharges;
                ++m_killerPowerState.blinksUsedThisChain;

                m_killerPowerState.blinkState = NurseBlinkState::BlinkTravel;
                m_killerPowerState.blinkRequiresRelease = true;
                m_killerPowerState.blinkIsChainCharge = false;  // Clear chain charge flag

                ItemPowerLog("NurseBlink: Teleporting " + std::to_string(glm::length(resolvedTarget - killerPos)) +
                             "m, charges remaining: " + std::to_string(m_killerPowerState.blinkCharges));
                break;
            }

            // Cancel conditions (after release check, so we don't cancel on release)
            // But if this is a chain charge and player releases without blinking, they still get fatigue
            if (!canUsePower || m_killerPowerState.blinkRequiresRelease)
            {
                if (m_killerPowerState.blinkIsChainCharge)
                {
                    // Cancelling a chain charge still gives fatigue
                    ItemPowerLog("NurseBlink: Chain charge cancelled, entering fatigue");
                    enterFatigue();
                }
                else
                {
                    m_killerPowerState.blinkState = NurseBlinkState::Idle;
                    m_killerPowerState.blinkChargeTimer = 0.0F;
                    m_killerPowerState.blinkCharge01 = 0.0F;
                    m_killerPowerState.blinkIsChainCharge = false;
                    ItemPowerLog("NurseBlink: Charge cancelled");
                }
                break;
            }

            // Charge progress (only while still holding)
            if (killerCommand.useAltHeld)
            {
                m_killerPowerState.blinkChargeTimer += fixedDt;
                m_killerPowerState.blinkCharge01 = glm::clamp(
                    m_killerPowerState.blinkChargeTimer / std::max(0.01F, m_blinkConfig.chargeTimeToMax),
                    0.0F, 1.0F
                );

                // Apply movement slowdown while charging
                killerVelocity *= m_blinkConfig.chargeMoveSpeedMultiplier;
            }
            break;
        }

        case NurseBlinkState::BlinkTravel:
        {
            m_killerPowerState.blinkTravelTimer += fixedDt;
            const float travelProgress = glm::clamp(
                m_killerPowerState.blinkTravelTimer / std::max(0.01F, m_blinkConfig.blinkTravelTime),
                0.0F, 1.0F
            );

            // Interpolate position during travel
            killerPos = glm::mix(
                m_killerPowerState.blinkStartPosition,
                m_killerPowerState.blinkTargetPosition,
                travelProgress
            );

            // No velocity during travel (instant teleport feel)
            killerVelocity = glm::vec3(0.0F);

            // Travel complete
            if (travelProgress >= 1.0F)
            {
                killerPos = m_killerPowerState.blinkTargetPosition;
                m_killerPowerState.blinkChainWindowTimer = 0.0F;
                m_killerPowerState.blinkState = NurseBlinkState::ChainWindow;
                ItemPowerLog("NurseBlink: Travel complete, chain window started");
            }
            break;
        }

        case NurseBlinkState::ChainWindow:
        {
            m_killerPowerState.blinkChainWindowTimer += fixedDt;
            const float chainProgress = m_killerPowerState.blinkChainWindowTimer /
                std::max(0.01F, m_blinkConfig.chainWindowSeconds);

            // No movement during chain window (decision time)
            killerVelocity = glm::vec3(0.0F);

            // Chain blink: start charging if RMB held and has charges
            if (killerCommand.useAltHeld &&
                !m_killerPowerState.blinkRequiresRelease &&
                m_killerPowerState.blinkCharges > 0 &&
                canUsePower)
            {
                m_killerPowerState.blinkState = NurseBlinkState::ChargingBlink;
                m_killerPowerState.blinkChargeTimer = 0.0F;
                m_killerPowerState.blinkCharge01 = 0.0F;
                m_killerPowerState.blinkStartPosition = killerPos;
                // Mark this as a chain charge with remaining time
                m_killerPowerState.blinkIsChainCharge = true;
                m_killerPowerState.blinkChainChargeRemaining = m_blinkConfig.chainWindowSeconds - m_killerPowerState.blinkChainWindowTimer;
                ItemPowerLog("NurseBlink: Chain blink started (remaining time: " +
                             std::to_string(m_killerPowerState.blinkChainChargeRemaining) + "s)");
                break;
            }

            // Blink attack: if attack pressed
            if (killerCommand.attackPressed && canUsePower)
            {
                m_killerPowerState.blinkState = NurseBlinkState::BlinkAttackWindup;
                m_killerPowerState.blinkAttackWindupTimer = 0.0F;
                m_killerPowerState.blinkAttackInProgress = true;
                ItemPowerLog("NurseBlink: Blink attack initiated");
                break;
            }

            // Chain window expired
            if (chainProgress >= 1.0F)
            {
                enterFatigue();
            }
            break;
        }

        case NurseBlinkState::BlinkAttackWindup:
        {
            m_killerPowerState.blinkAttackWindupTimer += fixedDt;

            // Lunge forward during windup
            const float lungeSpeed = m_tuning.killerMoveSpeed * m_blinkConfig.blinkAttackLungeMultiplier;
            const glm::vec3 forwardXZ = glm::normalize(glm::vec3(killerForward.x, 0.0F, killerForward.z));
            killerVelocity = forwardXZ * lungeSpeed;

            // Check for survivor hit
            if (m_survivor != 0)
            {
                auto survivorTransformIt = m_world.Transforms().find(m_survivor);
                if (survivorTransformIt != m_world.Transforms().end())
                {
                    const glm::vec3 survivorPos = survivorTransformIt->second.position;
                    const float distXZ = DistanceXZ(killerPos, survivorPos);

                    if (distXZ <= m_blinkConfig.blinkAttackRange)
                    {
                        // Check angle
                        const glm::vec3 toSurvivorXZ = glm::normalize(glm::vec3(
                            survivorPos.x - killerPos.x,
                            0.0F,
                            survivorPos.z - killerPos.z
                        ));
                        const glm::vec3 killerForwardXZ = glm::normalize(glm::vec3(
                            killerForward.x, 0.0F,
                            killerForward.z
                        ));
                        const float dot = glm::dot(killerForwardXZ, toSurvivorXZ);
                        const float angleRad = glm::radians(m_blinkConfig.blinkAttackAngleDegrees * 0.5F);

                        if (dot >= glm::cos(angleRad))
                        {
                            // Hit!
                            if (m_survivorState != SurvivorHealthState::Downed &&
                                m_survivorState != SurvivorHealthState::Dead &&
                                m_survivorState != SurvivorHealthState::Hooked &&
                                m_survivorState != SurvivorHealthState::Carried)
                            {
                                SetSurvivorState(SurvivorHealthState::Injured, "blink_attack", false);

                                // Blood FX
                                const engine::fx::FxNetMode netMode = m_networkAuthorityMode
                                    ? engine::fx::FxNetMode::ServerBroadcast
                                    : engine::fx::FxNetMode::Local;
                                SpawnGameplayFx("fx_blood_splatter_large", survivorPos, killerForward, netMode);
                                AddRuntimeMessage("BLINK ATTACK!", 2.0F);
                                ItemPowerLog("NurseBlink: Blink attack hit survivor!");
                            }

                            enterFatigue();
                            break;
                        }
                    }
                }
            }

            // Windup complete
            if (m_killerPowerState.blinkAttackWindupTimer >= m_blinkConfig.blinkAttackWindupSeconds)
            {
                enterFatigue();
            }
            break;
        }

        case NurseBlinkState::Fatigue:
        {
            // Apply movement penalty during fatigue
            killerVelocity *= m_blinkConfig.fatigueMoveSpeedMultiplier;

            const float fatigueDuration = m_blinkConfig.fatigueBaseSeconds +
                (static_cast<float>(m_killerPowerState.blinksUsedThisChain) * m_blinkConfig.fatiguePerBlinkUsedSeconds);

            m_killerPowerState.blinkFatigueTimer += fixedDt;

            if (m_killerPowerState.blinkFatigueTimer >= fatigueDuration)
            {
                m_killerPowerState.blinkState = NurseBlinkState::Idle;
                m_killerPowerState.blinksUsedThisChain = 0;
                m_killerPowerState.blinkFatigueTimer = 0.0F;
                // Reset requiresRelease so player can immediately start charging again
                m_killerPowerState.blinkRequiresRelease = false;
                ItemPowerLog("NurseBlink: Fatigue ended, returning to Idle");
            }
            break;
        }
    }
}

void GameplaySystems::RenderBlinkPreview(engine::render::Renderer& renderer)
{
    // Always show blink preview when charging (not just in debug mode)
    if (m_killer == 0 || m_killerPowerState.blinkState != NurseBlinkState::ChargingBlink)
    {
        return;
    }

    const loadout::PowerDefinition* powerDef = m_loadoutCatalog.FindPower(m_killerLoadout.powerId);
    if (powerDef == nullptr || powerDef->id != "nurse_blink")
    {
        return;
    }

    auto killerTransformIt = m_world.Transforms().find(m_killer);
    if (killerTransformIt == m_world.Transforms().end())
    {
        return;
    }

    const glm::vec3& killerPos = killerTransformIt->second.position;
    const glm::vec3& killerForward = killerTransformIt->second.forward;

    // Calculate preview position
    const glm::vec3 forwardXZ = glm::normalize(glm::vec3(killerForward.x, 0.0F, killerForward.z));
    const float previewDistance = m_blinkConfig.minBlinkDistance +
        m_killerPowerState.blinkCharge01 * (m_blinkConfig.maxBlinkDistance - m_blinkConfig.minBlinkDistance);
    const glm::vec3 requestedTarget = killerPos + forwardXZ * previewDistance;

    // Resolve the actual endpoint (always returns valid position)
    glm::vec3 resolvedTarget;
    (void)ResolveBlinkEndpoint(killerPos, requestedTarget, resolvedTarget);

    // Draw direction line from killer to target (cyan, pulsing)
    const float pulseIntensity = 0.7F + 0.3F * std::sin(m_killerPowerState.blinkChargeTimer * 8.0F);
    const glm::vec3 lineColor = glm::vec3(0.2F, 0.8F * pulseIntensity, 1.0F);

    renderer.DrawLine(killerPos + glm::vec3(0.0F, 0.5F, 0.0F),
                      resolvedTarget + glm::vec3(0.0F, 0.5F, 0.0F),
                      lineColor);

    // Draw target circle on ground (cyan, pulsing)
    renderer.DrawCircle(resolvedTarget, 0.6F, 16, lineColor);

    // Draw a vertical line at target position
    renderer.DrawLine(resolvedTarget + glm::vec3(0.0F, 0.0F, 0.0F),
                      resolvedTarget + glm::vec3(0.0F, 2.0F, 0.0F),
                      lineColor);
}

void GameplaySystems::RenderBlinkDebug(engine::render::Renderer& renderer)
{
    if (!m_blinkDebugEnabled || m_killer == 0)
    {
        return;
    }

    const loadout::PowerDefinition* powerDef = m_loadoutCatalog.FindPower(m_killerLoadout.powerId);
    if (powerDef == nullptr || powerDef->id != "nurse_blink")
    {
        return;
    }

    auto killerTransformIt = m_world.Transforms().find(m_killer);
    if (killerTransformIt == m_world.Transforms().end())
    {
        return;
    }

    const glm::vec3& killerPos = killerTransformIt->second.position;
    const glm::vec3& killerForward = killerTransformIt->second.forward;

    // Debug-only: show max range circle
    const glm::vec3 forwardXZ = glm::normalize(glm::vec3(killerForward.x, 0.0F, killerForward.z));
    const glm::vec3 maxRangePos = killerPos + forwardXZ * m_blinkConfig.maxBlinkDistance;
    renderer.DrawCircle(maxRangePos, 0.3F, 8, glm::vec3(0.5F, 0.5F, 0.5F)); // Gray = max range

    // Debug: show min range circle
    const glm::vec3 minRangePos = killerPos + forwardXZ * m_blinkConfig.minBlinkDistance;
    renderer.DrawCircle(minRangePos, 0.3F, 8, glm::vec3(0.3F, 0.3F, 0.3F)); // Dark gray = min range

    // Draw blink attack range (orange circle) - during chain window
    if (m_killerPowerState.blinkState == NurseBlinkState::ChainWindow ||
        m_killerPowerState.blinkState == NurseBlinkState::BlinkAttackWindup)
    {
        renderer.DrawCircle(killerPos, m_blinkConfig.blinkAttackRange, 24, glm::vec3(1.0F, 0.6F, 0.2F));
    }

    // Draw target position if traveling
    if (m_killerPowerState.blinkState == NurseBlinkState::BlinkTravel)
    {
        renderer.DrawCircle(m_killerPowerState.blinkTargetPosition, 0.5F, 12, glm::vec3(0.2F, 1.0F, 0.2F));
    }
}

void GameplaySystems::SetBlinkCharges(int charges)
{
    m_killerPowerState.blinkCharges = glm::clamp(charges, 0, m_blinkConfig.maxCharges);
    ItemPowerLog("NurseBlink: Charges set to " + std::to_string(m_killerPowerState.blinkCharges));
}

void GameplaySystems::ResetBlinkState()
{
    m_killerPowerState.blinkState = NurseBlinkState::Idle;
    m_killerPowerState.blinkChargeTimer = 0.0F;
    m_killerPowerState.blinkCharge01 = 0.0F;
    m_killerPowerState.blinkTravelTimer = 0.0F;
    m_killerPowerState.blinkChainWindowTimer = 0.0F;
    m_killerPowerState.blinkFatigueTimer = 0.0F;
    m_killerPowerState.blinkAttackWindupTimer = 0.0F;
    m_killerPowerState.blinksUsedThisChain = 0;
    m_killerPowerState.blinkAttackInProgress = false;
    m_killerPowerState.blinkRequiresRelease = false;
    ItemPowerLog("NurseBlink: State reset to Idle");
}

std::string GameplaySystems::GetBlinkDumpInfo() const
{
    std::ostringstream oss;
    oss << "=== Nurse Blink State ===\n";

    auto stateToText = [](NurseBlinkState state) -> std::string {
        switch (state)
        {
            case NurseBlinkState::Idle: return "Idle";
            case NurseBlinkState::ChargingBlink: return "ChargingBlink";
            case NurseBlinkState::BlinkTravel: return "BlinkTravel";
            case NurseBlinkState::ChainWindow: return "ChainWindow";
            case NurseBlinkState::BlinkAttackWindup: return "BlinkAttackWindup";
            case NurseBlinkState::Fatigue: return "Fatigue";
            default: return "Unknown";
        }
    };

    oss << "State: " << stateToText(m_killerPowerState.blinkState) << "\n";
    oss << "Charges: " << m_killerPowerState.blinkCharges << "/" << m_killerPowerState.blinkMaxCharges << "\n";
    oss << "Charge01: " << m_killerPowerState.blinkCharge01 << "\n";
    oss << "ChargeRegenTimer: " << m_killerPowerState.blinkChargeRegenTimer << "\n";
    oss << "BlinksUsedThisChain: " << m_killerPowerState.blinksUsedThisChain << "\n";
    oss << "TravelTimer: " << m_killerPowerState.blinkTravelTimer << "\n";
    oss << "ChainWindowTimer: " << m_killerPowerState.blinkChainWindowTimer << "\n";
    oss << "FatigueTimer: " << m_killerPowerState.blinkFatigueTimer << "\n";
    oss << "StartPosition: " << m_killerPowerState.blinkStartPosition.x << ", "
        << m_killerPowerState.blinkStartPosition.y << ", " << m_killerPowerState.blinkStartPosition.z << "\n";
    oss << "TargetPosition: " << m_killerPowerState.blinkTargetPosition.x << ", "
        << m_killerPowerState.blinkTargetPosition.y << ", " << m_killerPowerState.blinkTargetPosition.z << "\n";
    oss << "RequiresRelease: " << (m_killerPowerState.blinkRequiresRelease ? "true" : "false") << "\n";

    return oss.str();
}

std::string GameplaySystems::GetBlinkStateString() const
{
    switch (m_killerPowerState.blinkState)
    {
        case NurseBlinkState::Idle: return "Idle";
        case NurseBlinkState::ChargingBlink: return "Charging";
        case NurseBlinkState::BlinkTravel: return "Traveling";
        case NurseBlinkState::ChainWindow: return "ChainWindow";
        case NurseBlinkState::BlinkAttackWindup: return "Attacking";
        case NurseBlinkState::Fatigue: return "Fatigue";
        default: return "Unknown";
    }
}

void GameplaySystems::ForceAnimationState(const std::string& stateName)
{
    const auto state = engine::animation::ParseLocomotionState(stateName);
    if (state.has_value())
    {
        m_animationSystem.ForceState(state.value());
    }
}

void GameplaySystems::SetAnimationAutoMode(bool autoMode)
{
    m_animationSystem.SetAutoMode(autoMode);
}

std::string GameplaySystems::GetAnimationInfo() const
{
    return m_animationSystem.GetDebugInfo();
}

std::vector<std::string> GameplaySystems::GetAnimationClipList() const
{
    return m_animationSystem.ListClips();
}

void GameplaySystems::ForcePlayAnimationClip(const std::string& clipName)
{
    const auto* clip = m_animationSystem.GetClip(clipName);
    if (clip != nullptr)
    {
        m_animationSystem.GetStateMachineMut().GetBlenderMut().CrossfadeTo(clip, 0.2F);
    }
}

void GameplaySystems::SetGlobalAnimationScale(float scale)
{
    auto profile = m_animationSystem.GetProfile();
    profile.globalAnimScale = std::max(0.1F, scale);
    m_animationSystem.SetProfile(profile);
}

void GameplaySystems::LoadAnimationConfig()
{
    std::filesystem::create_directories("config");
    const std::filesystem::path path = std::filesystem::path("config") / "animation.json";

    if (!m_animationSystem.LoadProfile(path))
    {
        // Save default profile if it didn't exist
        m_animationSystem.SaveProfile(path);
    }

    m_animationSystem.InitializeStateMachine();
}

} // namespace game::gameplay
