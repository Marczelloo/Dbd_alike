#include "engine/assets/MeshLibrary.hpp"
#include "engine/animation/AnimationClip.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/vec2.hpp>
#include <glm/vec4.hpp>

#define TINYGLTF_IMPLEMENTATION
#define TINYGLTF_NO_STB_IMAGE_WRITE
#define STB_IMAGE_IMPLEMENTATION
#include <tiny_gltf.h>

namespace engine::assets
{
namespace
{
std::string ToLower(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

int ParseIndex(const std::string& token, int count)
{
    if (token.empty())
    {
        return -1;
    }
    int index = -1;
    try
    {
        index = std::stoi(token);
    }
    catch (...)
    {
        return -1;
    }

    if (index > 0)
    {
        index -= 1;
    }
    else if (index < 0)
    {
        index = count + index;
    }
    return index;
}

struct FaceVertex
{
    int position = -1;
    int normal = -1;
};

FaceVertex ParseFaceVertex(const std::string& token, int positionCount, int normalCount)
{
    FaceVertex out;
    std::stringstream ss(token);
    std::string a;
    std::string b;
    std::string c;
    std::getline(ss, a, '/');
    std::getline(ss, b, '/');
    std::getline(ss, c, '/');

    out.position = ParseIndex(a, positionCount);
    out.normal = ParseIndex(c, normalCount);
    return out;
}

bool ReadAccessorScalarsAsIndices(
    const tinygltf::Model& model,
    const tinygltf::Accessor& accessor,
    std::vector<std::uint32_t>* outIndices,
    std::string* outError
)
{
    if (outIndices == nullptr)
    {
        if (outError != nullptr)
        {
            *outError = "Index output buffer is null.";
        }
        return false;
    }
    if (accessor.bufferView < 0 || accessor.bufferView >= static_cast<int>(model.bufferViews.size()))
    {
        if (outError != nullptr)
        {
            *outError = "Accessor has invalid buffer view.";
        }
        return false;
    }
    if (accessor.type != TINYGLTF_TYPE_SCALAR)
    {
        if (outError != nullptr)
        {
            *outError = "Index accessor is not scalar.";
        }
        return false;
    }

    const tinygltf::BufferView& view = model.bufferViews[static_cast<std::size_t>(accessor.bufferView)];
    if (view.buffer < 0 || view.buffer >= static_cast<int>(model.buffers.size()))
    {
        if (outError != nullptr)
        {
            *outError = "Buffer view references invalid buffer.";
        }
        return false;
    }
    const tinygltf::Buffer& buffer = model.buffers[static_cast<std::size_t>(view.buffer)];

    const int componentSize = tinygltf::GetComponentSizeInBytes(accessor.componentType);
    if (componentSize <= 0)
    {
        if (outError != nullptr)
        {
            *outError = "Unsupported index component type.";
        }
        return false;
    }
    const std::size_t stride = accessor.ByteStride(view) > 0 ? static_cast<std::size_t>(accessor.ByteStride(view)) : static_cast<std::size_t>(componentSize);
    const std::size_t baseOffset = static_cast<std::size_t>(view.byteOffset + accessor.byteOffset);

    if (baseOffset >= buffer.data.size())
    {
        if (outError != nullptr)
        {
            *outError = "Index accessor offset out of range.";
        }
        return false;
    }

    outIndices->clear();
    outIndices->reserve(accessor.count);

    for (std::size_t i = 0; i < accessor.count; ++i)
    {
        const std::size_t offset = baseOffset + i * stride;
        if (offset + static_cast<std::size_t>(componentSize) > buffer.data.size())
        {
            if (outError != nullptr)
            {
                *outError = "Index accessor data out of range.";
            }
            return false;
        }

        std::uint32_t value = 0;
        switch (accessor.componentType)
        {
            case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
            {
                std::uint8_t tmp = 0;
                std::memcpy(&tmp, buffer.data.data() + offset, sizeof(tmp));
                value = tmp;
                break;
            }
            case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT:
            {
                std::uint16_t tmp = 0;
                std::memcpy(&tmp, buffer.data.data() + offset, sizeof(tmp));
                value = tmp;
                break;
            }
            case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT:
            {
                std::uint32_t tmp = 0;
                std::memcpy(&tmp, buffer.data.data() + offset, sizeof(tmp));
                value = tmp;
                break;
            }
            default:
            {
                if (outError != nullptr)
                {
                    *outError = "Unsupported index component type in accessor.";
                }
                return false;
            }
        }
        outIndices->push_back(value);
    }

    return true;
}

bool ReadAccessorVec3Float(
    const tinygltf::Model& model,
    const tinygltf::Accessor& accessor,
    std::vector<glm::vec3>* outValues,
    std::string* outError
)
{
    if (outValues == nullptr)
    {
        if (outError != nullptr)
        {
            *outError = "Vec3 output buffer is null.";
        }
        return false;
    }
    if (accessor.bufferView < 0 || accessor.bufferView >= static_cast<int>(model.bufferViews.size()))
    {
        if (outError != nullptr)
        {
            *outError = "Accessor has invalid buffer view.";
        }
        return false;
    }
    if (accessor.type != TINYGLTF_TYPE_VEC3)
    {
        if (outError != nullptr)
        {
            *outError = "Accessor is not vec3.";
        }
        return false;
    }
    if (accessor.componentType != TINYGLTF_COMPONENT_TYPE_FLOAT)
    {
        if (outError != nullptr)
        {
            *outError = "Only float vec3 accessor is supported.";
        }
        return false;
    }

    const tinygltf::BufferView& view = model.bufferViews[static_cast<std::size_t>(accessor.bufferView)];
    if (view.buffer < 0 || view.buffer >= static_cast<int>(model.buffers.size()))
    {
        if (outError != nullptr)
        {
            *outError = "Buffer view references invalid buffer.";
        }
        return false;
    }
    const tinygltf::Buffer& buffer = model.buffers[static_cast<std::size_t>(view.buffer)];

    const std::size_t floatBytes = sizeof(float);
    const std::size_t elementSize = 3 * floatBytes;
    const std::size_t stride = accessor.ByteStride(view) > 0 ? static_cast<std::size_t>(accessor.ByteStride(view)) : elementSize;
    const std::size_t baseOffset = static_cast<std::size_t>(view.byteOffset + accessor.byteOffset);
    if (baseOffset >= buffer.data.size())
    {
        if (outError != nullptr)
        {
            *outError = "Vec3 accessor offset out of range.";
        }
        return false;
    }

    outValues->clear();
    outValues->reserve(accessor.count);

    for (std::size_t i = 0; i < accessor.count; ++i)
    {
        const std::size_t offset = baseOffset + i * stride;
        if (offset + elementSize > buffer.data.size())
        {
            if (outError != nullptr)
            {
                *outError = "Vec3 accessor data out of range.";
            }
            return false;
        }

        glm::vec3 value{0.0F};
        std::memcpy(&value.x, buffer.data.data() + offset, floatBytes);
        std::memcpy(&value.y, buffer.data.data() + offset + floatBytes, floatBytes);
        std::memcpy(&value.z, buffer.data.data() + offset + 2 * floatBytes, floatBytes);
        outValues->push_back(value);
    }

    return true;
}

bool ReadAccessorVec2Float(
    const tinygltf::Model& model,
    const tinygltf::Accessor& accessor,
    std::vector<glm::vec2>* outValues,
    std::string* outError
)
{
    if (outValues == nullptr)
    {
        if (outError != nullptr)
        {
            *outError = "Vec2 output buffer is null.";
        }
        return false;
    }
    if (accessor.bufferView < 0 || accessor.bufferView >= static_cast<int>(model.bufferViews.size()))
    {
        if (outError != nullptr)
        {
            *outError = "Accessor has invalid buffer view.";
        }
        return false;
    }
    if (accessor.type != TINYGLTF_TYPE_VEC2)
    {
        if (outError != nullptr)
        {
            *outError = "Accessor is not vec2.";
        }
        return false;
    }
    if (accessor.componentType != TINYGLTF_COMPONENT_TYPE_FLOAT)
    {
        if (outError != nullptr)
        {
            *outError = "Only float vec2 accessor is supported.";
        }
        return false;
    }

    const tinygltf::BufferView& view = model.bufferViews[static_cast<std::size_t>(accessor.bufferView)];
    if (view.buffer < 0 || view.buffer >= static_cast<int>(model.buffers.size()))
    {
        if (outError != nullptr)
        {
            *outError = "Buffer view references invalid buffer.";
        }
        return false;
    }
    const tinygltf::Buffer& buffer = model.buffers[static_cast<std::size_t>(view.buffer)];

    const std::size_t floatBytes = sizeof(float);
    const std::size_t elementSize = 2 * floatBytes;
    const std::size_t stride = accessor.ByteStride(view) > 0 ? static_cast<std::size_t>(accessor.ByteStride(view)) : elementSize;
    const std::size_t baseOffset = static_cast<std::size_t>(view.byteOffset + accessor.byteOffset);
    if (baseOffset >= buffer.data.size())
    {
        if (outError != nullptr)
        {
            *outError = "Vec2 accessor offset out of range.";
        }
        return false;
    }

    outValues->clear();
    outValues->reserve(accessor.count);

    for (std::size_t i = 0; i < accessor.count; ++i)
    {
        const std::size_t offset = baseOffset + i * stride;
        if (offset + elementSize > buffer.data.size())
        {
            if (outError != nullptr)
            {
                *outError = "Vec2 accessor data out of range.";
            }
            return false;
        }

        glm::vec2 value{0.0F};
        std::memcpy(&value.x, buffer.data.data() + offset, floatBytes);
        std::memcpy(&value.y, buffer.data.data() + offset + floatBytes, floatBytes);
        outValues->push_back(value);
    }

    return true;
}

bool ReadAccessorVec4UInt(
    const tinygltf::Model& model,
    const tinygltf::Accessor& accessor,
    std::vector<glm::uvec4>* outValues,
    std::string* outError
)
{
    if (outValues == nullptr)
    {
        if (outError != nullptr)
        {
            *outError = "Vec4 uint output buffer is null.";
        }
        return false;
    }
    if (accessor.bufferView < 0 || accessor.bufferView >= static_cast<int>(model.bufferViews.size()))
    {
        if (outError != nullptr)
        {
            *outError = "Accessor has invalid buffer view.";
        }
        return false;
    }
    if (accessor.type != TINYGLTF_TYPE_VEC4)
    {
        if (outError != nullptr)
        {
            *outError = "Accessor is not vec4.";
        }
        return false;
    }
    const tinygltf::BufferView& view = model.bufferViews[static_cast<std::size_t>(accessor.bufferView)];
    if (view.buffer < 0 || view.buffer >= static_cast<int>(model.buffers.size()))
    {
        if (outError != nullptr)
        {
            *outError = "Buffer view references invalid buffer.";
        }
        return false;
    }
    const tinygltf::Buffer& buffer = model.buffers[static_cast<std::size_t>(view.buffer)];
    const int componentSize = tinygltf::GetComponentSizeInBytes(accessor.componentType);
    if (componentSize <= 0)
    {
        if (outError != nullptr)
        {
            *outError = "Unsupported component type for vec4 uint.";
        }
        return false;
    }

    const std::size_t stride = accessor.ByteStride(view) > 0 ? static_cast<std::size_t>(accessor.ByteStride(view)) : static_cast<std::size_t>(componentSize * 4);
    const std::size_t baseOffset = static_cast<std::size_t>(view.byteOffset + accessor.byteOffset);
    if (baseOffset >= buffer.data.size())
    {
        if (outError != nullptr)
        {
            *outError = "Vec4 uint accessor offset out of range.";
        }
        return false;
    }

    outValues->clear();
    outValues->reserve(accessor.count);
    for (std::size_t i = 0; i < accessor.count; ++i)
    {
        const std::size_t offset = baseOffset + i * stride;
        if (offset + static_cast<std::size_t>(componentSize * 4) > buffer.data.size())
        {
            if (outError != nullptr)
            {
                *outError = "Vec4 uint accessor data out of range.";
            }
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
                {
                    if (outError != nullptr)
                    {
                        *outError = "Unsupported vec4 uint component type.";
                    }
                    return false;
                }
            }
        }
        outValues->push_back(value);
    }
    return true;
}

float ReadComponentAsFloat(const std::uint8_t* src, int componentType, bool normalized)
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

bool ReadAccessorVec4Float(
    const tinygltf::Model& model,
    const tinygltf::Accessor& accessor,
    std::vector<glm::vec4>* outValues,
    std::string* outError
)
{
    if (outValues == nullptr)
    {
        if (outError != nullptr)
        {
            *outError = "Vec4 float output buffer is null.";
        }
        return false;
    }
    if (accessor.bufferView < 0 || accessor.bufferView >= static_cast<int>(model.bufferViews.size()))
    {
        if (outError != nullptr)
        {
            *outError = "Accessor has invalid buffer view.";
        }
        return false;
    }
    if (accessor.type != TINYGLTF_TYPE_VEC4)
    {
        if (outError != nullptr)
        {
            *outError = "Accessor is not vec4.";
        }
        return false;
    }
    const tinygltf::BufferView& view = model.bufferViews[static_cast<std::size_t>(accessor.bufferView)];
    if (view.buffer < 0 || view.buffer >= static_cast<int>(model.buffers.size()))
    {
        if (outError != nullptr)
        {
            *outError = "Buffer view references invalid buffer.";
        }
        return false;
    }
    const tinygltf::Buffer& buffer = model.buffers[static_cast<std::size_t>(view.buffer)];
    const int componentSize = tinygltf::GetComponentSizeInBytes(accessor.componentType);
    if (componentSize <= 0)
    {
        if (outError != nullptr)
        {
            *outError = "Unsupported component type for vec4 float.";
        }
        return false;
    }

    const std::size_t stride = accessor.ByteStride(view) > 0 ? static_cast<std::size_t>(accessor.ByteStride(view)) : static_cast<std::size_t>(componentSize * 4);
    const std::size_t baseOffset = static_cast<std::size_t>(view.byteOffset + accessor.byteOffset);
    if (baseOffset >= buffer.data.size())
    {
        if (outError != nullptr)
        {
            *outError = "Vec4 float accessor offset out of range.";
        }
        return false;
    }

    outValues->clear();
    outValues->reserve(accessor.count);
    for (std::size_t i = 0; i < accessor.count; ++i)
    {
        const std::size_t offset = baseOffset + i * stride;
        if (offset + static_cast<std::size_t>(componentSize * 4) > buffer.data.size())
        {
            if (outError != nullptr)
            {
                *outError = "Vec4 float accessor data out of range.";
            }
            return false;
        }

        glm::vec4 value{0.0F};
        for (int c = 0; c < 4; ++c)
        {
            const std::size_t at = offset + static_cast<std::size_t>(c * componentSize);
            value[static_cast<std::size_t>(c)] = ReadComponentAsFloat(buffer.data.data() + at, accessor.componentType, accessor.normalized);
        }
        outValues->push_back(value);
    }
    return true;
}

bool ReadAccessorMat4Float(
    const tinygltf::Model& model,
    const tinygltf::Accessor& accessor,
    std::vector<glm::mat4>* outValues,
    std::string* outError
)
{
    if (outValues == nullptr)
    {
        if (outError != nullptr)
        {
            *outError = "Mat4 output buffer is null.";
        }
        return false;
    }
    if (accessor.bufferView < 0 || accessor.bufferView >= static_cast<int>(model.bufferViews.size()))
    {
        if (outError != nullptr)
        {
            *outError = "Accessor has invalid buffer view.";
        }
        return false;
    }
    if (accessor.type != TINYGLTF_TYPE_MAT4 || accessor.componentType != TINYGLTF_COMPONENT_TYPE_FLOAT)
    {
        if (outError != nullptr)
        {
            *outError = "Only float mat4 accessor is supported.";
        }
        return false;
    }
    const tinygltf::BufferView& view = model.bufferViews[static_cast<std::size_t>(accessor.bufferView)];
    if (view.buffer < 0 || view.buffer >= static_cast<int>(model.buffers.size()))
    {
        if (outError != nullptr)
        {
            *outError = "Buffer view references invalid buffer.";
        }
        return false;
    }
    const tinygltf::Buffer& buffer = model.buffers[static_cast<std::size_t>(view.buffer)];

    const std::size_t elementSize = sizeof(float) * 16U;
    const std::size_t stride = accessor.ByteStride(view) > 0 ? static_cast<std::size_t>(accessor.ByteStride(view)) : elementSize;
    const std::size_t baseOffset = static_cast<std::size_t>(view.byteOffset + accessor.byteOffset);
    if (baseOffset >= buffer.data.size())
    {
        if (outError != nullptr)
        {
            *outError = "Mat4 accessor offset out of range.";
        }
        return false;
    }

    outValues->clear();
    outValues->reserve(accessor.count);
    for (std::size_t i = 0; i < accessor.count; ++i)
    {
        const std::size_t offset = baseOffset + i * stride;
        if (offset + elementSize > buffer.data.size())
        {
            if (outError != nullptr)
            {
                *outError = "Mat4 accessor data out of range.";
            }
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

glm::mat4 NodeLocalTransform(const tinygltf::Node& node)
{
    if (node.matrix.size() == 16U)
    {
        glm::mat4 m{1.0F};
        for (int r = 0; r < 4; ++r)
        {
            for (int c = 0; c < 4; ++c)
            {
                // glTF stores matrices in column-major order.
                m[c][r] = static_cast<float>(node.matrix[static_cast<std::size_t>(c * 4 + r)]);
            }
        }
        return m;
    }

    glm::vec3 translation{0.0F};
    if (node.translation.size() == 3U)
    {
        translation = glm::vec3{
            static_cast<float>(node.translation[0]),
            static_cast<float>(node.translation[1]),
            static_cast<float>(node.translation[2]),
        };
    }

    glm::quat rotation = glm::quat{1.0F, 0.0F, 0.0F, 0.0F};
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

    glm::vec3 scale{1.0F};
    if (node.scale.size() == 3U)
    {
        scale = glm::vec3{
            static_cast<float>(node.scale[0]),
            static_cast<float>(node.scale[1]),
            static_cast<float>(node.scale[2]),
        };
    }

    const glm::mat4 identity{1.0F};
    return glm::translate(identity, translation) * glm::mat4_cast(rotation) * glm::scale(identity, scale);
}

struct MeshInstanceData
{
    glm::mat4 world{1.0F};
    int skinIndex = -1;
};

void CollectNodeInstances(
    const tinygltf::Model& model,
    int nodeIndex,
    const glm::mat4& parentWorld,
    std::vector<glm::mat4>* outNodeWorlds,
    std::vector<std::vector<MeshInstanceData>>* outMeshInstances
)
{
    if (outNodeWorlds == nullptr || outMeshInstances == nullptr || nodeIndex < 0 || nodeIndex >= static_cast<int>(model.nodes.size()))
    {
        return;
    }

    const tinygltf::Node& node = model.nodes[static_cast<std::size_t>(nodeIndex)];
    const glm::mat4 world = parentWorld * NodeLocalTransform(node);
    (*outNodeWorlds)[static_cast<std::size_t>(nodeIndex)] = world;
    if (node.mesh >= 0 && node.mesh < static_cast<int>(outMeshInstances->size()))
    {
        (*outMeshInstances)[static_cast<std::size_t>(node.mesh)].push_back(MeshInstanceData{
            world,
            node.skin,
        });
    }

    for (int child : node.children)
    {
        CollectNodeInstances(model, child, world, outNodeWorlds, outMeshInstances);
    }
}
} // namespace

const MeshData* MeshLibrary::LoadMesh(const std::filesystem::path& absolutePath, std::string* outError)
{
    const std::string key = absolutePath.lexically_normal().generic_string();
    const auto existing = m_cache.find(key);
    if (existing != m_cache.end())
    {
        if (outError != nullptr)
        {
            *outError = existing->second.error;
        }
        return &existing->second;
    }

    MeshData loaded;
    const std::string ext = ToLower(absolutePath.extension().string());
    if (ext == ".obj")
    {
        loaded = LoadObj(absolutePath);
    }
    else if (ext == ".gltf" || ext == ".glb")
    {
        loaded = LoadGltf(absolutePath, m_animationCallback ? &m_animationCallback : nullptr);
    }
    else
    {
        loaded.loaded = false;
        loaded.error = "Mesh format not supported yet (supported: .obj, .gltf, .glb)";
    }

    const auto [it, inserted] = m_cache.emplace(key, std::move(loaded));
    (void)inserted;
    if (outError != nullptr)
    {
        *outError = it->second.error;
    }
    return &it->second;
}

void MeshLibrary::Clear()
{
    m_cache.clear();
}

MeshData MeshLibrary::LoadObj(const std::filesystem::path& absolutePath)
{
    MeshData out;

    std::ifstream stream(absolutePath);
    if (!stream.is_open())
    {
        out.error = "Unable to open OBJ: " + absolutePath.generic_string();
        return out;
    }

    std::vector<glm::vec3> positions;
    std::vector<glm::vec3> normals;
    struct Triangle
    {
        std::array<FaceVertex, 3> v;
    };
    std::vector<Triangle> triangles;

    std::string line;
    while (std::getline(stream, line))
    {
        if (line.empty() || line[0] == '#')
        {
            continue;
        }

        std::stringstream ss(line);
        std::string type;
        ss >> type;
        if (type == "v")
        {
            glm::vec3 p{0.0F};
            ss >> p.x >> p.y >> p.z;
            positions.push_back(p);
        }
        else if (type == "vn")
        {
            glm::vec3 n{0.0F, 1.0F, 0.0F};
            ss >> n.x >> n.y >> n.z;
            if (glm::length(n) > 1.0e-6F)
            {
                n = glm::normalize(n);
            }
            normals.push_back(n);
        }
        else if (type == "f")
        {
            std::vector<FaceVertex> polygon;
            std::string token;
            while (ss >> token)
            {
                polygon.push_back(ParseFaceVertex(token, static_cast<int>(positions.size()), static_cast<int>(normals.size())));
            }

            if (polygon.size() < 3)
            {
                continue;
            }

            for (std::size_t i = 1; i + 1 < polygon.size(); ++i)
            {
                triangles.push_back(Triangle{
                    {
                        polygon[0],
                        polygon[i],
                        polygon[i + 1],
                    }});
            }
        }
    }

    if (positions.empty() || triangles.empty())
    {
        out.error = "OBJ has no renderable triangles: " + absolutePath.generic_string();
        return out;
    }

    out.geometry.positions.clear();
    out.geometry.normals.clear();
    out.geometry.colors.clear();
    out.geometry.uvs.clear();
    out.geometry.indices.clear();
    out.surfaces.clear();
    out.geometry.positions.reserve(triangles.size() * 3);
    out.geometry.normals.reserve(triangles.size() * 3);
    out.geometry.colors.reserve(triangles.size() * 3);
    out.geometry.uvs.reserve(triangles.size() * 3);
    out.geometry.indices.reserve(triangles.size() * 3);

    glm::vec3 boundsMin{1.0e9F};
    glm::vec3 boundsMax{-1.0e9F};

    for (const Triangle& tri : triangles)
    {
        std::array<glm::vec3, 3> triPos{};
        for (int i = 0; i < 3; ++i)
        {
            const int posIdx = tri.v[static_cast<std::size_t>(i)].position;
            if (posIdx < 0 || posIdx >= static_cast<int>(positions.size()))
            {
                out.error = "OBJ face references invalid position index.";
                return out;
            }
            triPos[static_cast<std::size_t>(i)] = positions[static_cast<std::size_t>(posIdx)];
            boundsMin = glm::min(boundsMin, triPos[static_cast<std::size_t>(i)]);
            boundsMax = glm::max(boundsMax, triPos[static_cast<std::size_t>(i)]);
        }

        glm::vec3 fallbackNormal = glm::cross(triPos[1] - triPos[0], triPos[2] - triPos[0]);
        if (glm::length(fallbackNormal) > 1.0e-6F)
        {
            fallbackNormal = glm::normalize(fallbackNormal);
        }
        else
        {
            fallbackNormal = glm::vec3{0.0F, 1.0F, 0.0F};
        }

        for (int i = 0; i < 3; ++i)
        {
            glm::vec3 n = fallbackNormal;
            const int normalIdx = tri.v[static_cast<std::size_t>(i)].normal;
            if (normalIdx >= 0 && normalIdx < static_cast<int>(normals.size()))
            {
                n = normals[static_cast<std::size_t>(normalIdx)];
            }
            out.geometry.positions.push_back(triPos[static_cast<std::size_t>(i)]);
            out.geometry.normals.push_back(n);
            out.geometry.colors.push_back(glm::vec3{1.0F, 1.0F, 1.0F});
            out.geometry.uvs.push_back(glm::vec2{0.0F, 0.0F});
            out.geometry.indices.push_back(static_cast<std::uint32_t>(out.geometry.indices.size()));
        }
    }

    const glm::vec3 center = (boundsMin + boundsMax) * 0.5F;
    for (glm::vec3& p : out.geometry.positions)
    {
        p -= center;
    }
    out.boundsMin = boundsMin - center;
    out.boundsMax = boundsMax - center;
    MeshSurfaceData surface{};
    surface.geometry = out.geometry;
    out.surfaces.push_back(std::move(surface));
    out.loaded = true;
    out.error.clear();
    return out;
}

MeshData MeshLibrary::LoadGltf(const std::filesystem::path& absolutePath, AnimationLoadedCallback* animationCallback)
{
    MeshData out;

    tinygltf::TinyGLTF loader;
    tinygltf::Model model;
    std::string warn;
    std::string err;

    const std::string ext = ToLower(absolutePath.extension().string());
    bool loaded = false;
    if (ext == ".glb")
    {
        loaded = loader.LoadBinaryFromFile(&model, &err, &warn, absolutePath.string());
    }
    else
    {
        loaded = loader.LoadASCIIFromFile(&model, &err, &warn, absolutePath.string());
    }

    if (!loaded)
    {
        out.error = "Failed to load glTF: " + absolutePath.generic_string();
        if (!err.empty())
        {
            out.error += " | " + err;
        }
        return out;
    }

    out.geometry.positions.clear();
    out.geometry.normals.clear();
    out.geometry.colors.clear();
    out.geometry.uvs.clear();
    out.geometry.indices.clear();

    glm::vec3 boundsMin{1.0e9F};
    glm::vec3 boundsMax{-1.0e9F};

    std::vector<glm::mat4> nodeWorlds(model.nodes.size(), glm::mat4{1.0F});
    std::vector<std::vector<MeshInstanceData>> meshInstances(model.meshes.size());
    if (!model.scenes.empty())
    {
        const int sceneIndex = (model.defaultScene >= 0 && model.defaultScene < static_cast<int>(model.scenes.size()))
                                   ? model.defaultScene
                                   : 0;
        const tinygltf::Scene& scene = model.scenes[static_cast<std::size_t>(sceneIndex)];
        for (int rootNode : scene.nodes)
        {
            CollectNodeInstances(model, rootNode, glm::mat4{1.0F}, &nodeWorlds, &meshInstances);
        }
    }
    for (std::size_t mi = 0; mi < meshInstances.size(); ++mi)
    {
        if (meshInstances[mi].empty())
        {
            meshInstances[mi].push_back(MeshInstanceData{glm::mat4{1.0F}, -1});
        }
    }

    struct SkinCache
    {
        std::vector<int> joints;
        std::vector<glm::mat4> inverseBindMatrices;
    };
    std::vector<SkinCache> skinCaches(model.skins.size());
    for (std::size_t skinIndex = 0; skinIndex < model.skins.size(); ++skinIndex)
    {
        const tinygltf::Skin& skin = model.skins[skinIndex];
        SkinCache cache;
        cache.joints = skin.joints;
        cache.inverseBindMatrices.assign(cache.joints.size(), glm::mat4{1.0F});
        if (skin.inverseBindMatrices >= 0 && skin.inverseBindMatrices < static_cast<int>(model.accessors.size()))
        {
            const tinygltf::Accessor& accessor = model.accessors[static_cast<std::size_t>(skin.inverseBindMatrices)];
            std::vector<glm::mat4> ibms;
            if (ReadAccessorMat4Float(model, accessor, &ibms, nullptr))
            {
                const std::size_t count = glm::min(cache.joints.size(), ibms.size());
                for (std::size_t i = 0; i < count; ++i)
                {
                    cache.inverseBindMatrices[i] = ibms[i];
                }
            }
        }
        skinCaches[skinIndex] = std::move(cache);
    }

    bool emittedAnyTriangle = false;

    for (std::size_t meshIndex = 0; meshIndex < model.meshes.size(); ++meshIndex)
    {
        const tinygltf::Mesh& mesh = model.meshes[meshIndex];
        const std::vector<MeshInstanceData>& instances = meshInstances[meshIndex];
        for (const tinygltf::Primitive& primitive : mesh.primitives)
        {
            const int mode = primitive.mode == -1 ? TINYGLTF_MODE_TRIANGLES : primitive.mode;
            const bool trianglesMode =
                (mode == TINYGLTF_MODE_TRIANGLES) ||
                (mode == TINYGLTF_MODE_TRIANGLE_STRIP) ||
                (mode == TINYGLTF_MODE_TRIANGLE_FAN);
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
            const tinygltf::Accessor& positionAccessor = model.accessors[static_cast<std::size_t>(positionAccessorIndex)];
            std::vector<glm::vec3> positions;
            std::string readError;
            if (!ReadAccessorVec3Float(model, positionAccessor, &positions, &readError))
            {
                out.error = "Failed to read POSITION accessor: " + readError;
                return out;
            }
            if (positions.empty())
            {
                continue;
            }

            std::vector<glm::vec3> normals;
            const auto normalIt = primitive.attributes.find("NORMAL");
            if (normalIt != primitive.attributes.end())
            {
                const int normalAccessorIndex = normalIt->second;
                if (normalAccessorIndex >= 0 && normalAccessorIndex < static_cast<int>(model.accessors.size()))
                {
                    const tinygltf::Accessor& normalAccessor = model.accessors[static_cast<std::size_t>(normalAccessorIndex)];
                    (void)ReadAccessorVec3Float(model, normalAccessor, &normals, nullptr);
                }
            }

            glm::vec4 baseColorFactor{1.0F, 1.0F, 1.0F, 1.0F};
            const tinygltf::Image* baseColorImage = nullptr;
            int baseColorTexcoordSet = 0;
            if (primitive.material >= 0 && primitive.material < static_cast<int>(model.materials.size()))
            {
                const tinygltf::Material& material = model.materials[static_cast<std::size_t>(primitive.material)];
                const auto& pbr = material.pbrMetallicRoughness;
                if (pbr.baseColorFactor.size() == 4U)
                {
                    baseColorFactor = glm::vec4{
                        static_cast<float>(pbr.baseColorFactor[0]),
                        static_cast<float>(pbr.baseColorFactor[1]),
                        static_cast<float>(pbr.baseColorFactor[2]),
                        static_cast<float>(pbr.baseColorFactor[3]),
                    };
                }
                const int baseColorTextureIndex = pbr.baseColorTexture.index;
                if (baseColorTextureIndex >= 0 && baseColorTextureIndex < static_cast<int>(model.textures.size()))
                {
                    baseColorTexcoordSet = pbr.baseColorTexture.texCoord;
                    const tinygltf::Texture& texture = model.textures[static_cast<std::size_t>(baseColorTextureIndex)];
                    if (texture.source >= 0 && texture.source < static_cast<int>(model.images.size()))
                    {
                        baseColorImage = &model.images[static_cast<std::size_t>(texture.source)];
                    }
                }
            }
            std::vector<glm::vec2> texcoords;
            const std::string texcoordSemantic = "TEXCOORD_" + std::to_string(glm::max(0, baseColorTexcoordSet));
            const auto texcoordIt = primitive.attributes.find(texcoordSemantic);
            if (texcoordIt != primitive.attributes.end())
            {
                const int texcoordAccessorIndex = texcoordIt->second;
                if (texcoordAccessorIndex >= 0 && texcoordAccessorIndex < static_cast<int>(model.accessors.size()))
                {
                    const tinygltf::Accessor& texcoordAccessor = model.accessors[static_cast<std::size_t>(texcoordAccessorIndex)];
                    (void)ReadAccessorVec2Float(model, texcoordAccessor, &texcoords, nullptr);
                }
            }
            else
            {
                const auto texcoord0 = primitive.attributes.find("TEXCOORD_0");
                if (texcoord0 != primitive.attributes.end())
                {
                    const int texcoordAccessorIndex = texcoord0->second;
                    if (texcoordAccessorIndex >= 0 && texcoordAccessorIndex < static_cast<int>(model.accessors.size()))
                    {
                        const tinygltf::Accessor& texcoordAccessor = model.accessors[static_cast<std::size_t>(texcoordAccessorIndex)];
                        (void)ReadAccessorVec2Float(model, texcoordAccessor, &texcoords, nullptr);
                    }
                }
            }

            std::vector<glm::uvec4> jointIndices;
            const auto jointsIt = primitive.attributes.find("JOINTS_0");
            if (jointsIt != primitive.attributes.end())
            {
                const int jointsAccessorIndex = jointsIt->second;
                if (jointsAccessorIndex >= 0 && jointsAccessorIndex < static_cast<int>(model.accessors.size()))
                {
                    const tinygltf::Accessor& jointsAccessor = model.accessors[static_cast<std::size_t>(jointsAccessorIndex)];
                    (void)ReadAccessorVec4UInt(model, jointsAccessor, &jointIndices, nullptr);
                }
            }
            std::vector<glm::vec4> jointWeights;
            const auto weightsIt = primitive.attributes.find("WEIGHTS_0");
            if (weightsIt != primitive.attributes.end())
            {
                const int weightsAccessorIndex = weightsIt->second;
                if (weightsAccessorIndex >= 0 && weightsAccessorIndex < static_cast<int>(model.accessors.size()))
                {
                    const tinygltf::Accessor& weightsAccessor = model.accessors[static_cast<std::size_t>(weightsAccessorIndex)];
                    (void)ReadAccessorVec4Float(model, weightsAccessor, &jointWeights, nullptr);
                }
            }

            MeshSurfaceData primitiveSurface{};
            if (baseColorImage != nullptr &&
                baseColorImage->width > 0 &&
                baseColorImage->height > 0 &&
                baseColorImage->bits == 8 &&
                !baseColorImage->image.empty())
            {
                primitiveSurface.albedoPixels = baseColorImage->image;
                primitiveSurface.albedoWidth = baseColorImage->width;
                primitiveSurface.albedoHeight = baseColorImage->height;
                primitiveSurface.albedoChannels = glm::clamp(baseColorImage->component, 1, 4);
            }

            std::vector<std::uint32_t> primitiveIndices;
            if (primitive.indices >= 0)
            {
                if (primitive.indices >= static_cast<int>(model.accessors.size()))
                {
                    continue;
                }
                const tinygltf::Accessor& indexAccessor = model.accessors[static_cast<std::size_t>(primitive.indices)];
                if (!ReadAccessorScalarsAsIndices(model, indexAccessor, &primitiveIndices, &readError))
                {
                    out.error = "Failed to read index accessor: " + readError;
                    return out;
                }
            }
            else
            {
                primitiveIndices.reserve(positions.size());
                for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(positions.size()); ++i)
                {
                    primitiveIndices.push_back(i);
                }
            }

            if (primitiveIndices.size() < 3)
            {
                continue;
            }

            std::vector<std::array<std::uint32_t, 3>> triangles;
            triangles.reserve(primitiveIndices.size() / 3U + 2U);
            if (mode == TINYGLTF_MODE_TRIANGLES)
            {
                for (std::size_t triStart = 0; triStart + 2 < primitiveIndices.size(); triStart += 3)
                {
                    triangles.push_back(std::array<std::uint32_t, 3>{
                        primitiveIndices[triStart],
                        primitiveIndices[triStart + 1],
                        primitiveIndices[triStart + 2],
                    });
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
                    triangles.push_back(odd ? std::array<std::uint32_t, 3>{b, a, c}
                                            : std::array<std::uint32_t, 3>{a, b, c});
                }
            }
            else // TRIANGLE_FAN
            {
                const std::uint32_t root = primitiveIndices[0];
                for (std::size_t i = 2; i < primitiveIndices.size(); ++i)
                {
                    triangles.push_back(std::array<std::uint32_t, 3>{
                        root,
                        primitiveIndices[i - 1],
                        primitiveIndices[i],
                    });
                }
            }

            for (const MeshInstanceData& instance : instances)
            {
                const glm::mat4& worldTransform = instance.world;
                const glm::mat3 normalTransform = glm::inverseTranspose(glm::mat3(worldTransform));
                const bool flipWinding = glm::determinant(glm::mat3(worldTransform)) < 0.0F;

                bool canSkin = false;
                std::vector<glm::mat4> skinJointMatrices;
                if (instance.skinIndex >= 0 &&
                    instance.skinIndex < static_cast<int>(skinCaches.size()) &&
                    !jointIndices.empty() &&
                    !jointWeights.empty())
                {
                    const SkinCache& skin = skinCaches[static_cast<std::size_t>(instance.skinIndex)];
                    if (!skin.joints.empty())
                    {
                        skinJointMatrices.assign(skin.joints.size(), glm::mat4{1.0F});
                        const glm::mat4 invMeshWorld = glm::inverse(worldTransform);
                        for (std::size_t ji = 0; ji < skin.joints.size(); ++ji)
                        {
                            const int jointNode = skin.joints[ji];
                            if (jointNode < 0 || jointNode >= static_cast<int>(nodeWorlds.size()))
                            {
                                continue;
                            }
                            skinJointMatrices[ji] = invMeshWorld * nodeWorlds[static_cast<std::size_t>(jointNode)] * skin.inverseBindMatrices[ji];
                        }
                        canSkin = true;
                    }
                }

                const auto skinVertex = [&](std::uint32_t idx, const glm::vec3& localPos, const glm::vec3& localNormal) {
                    if (!canSkin || idx >= jointIndices.size() || idx >= jointWeights.size())
                    {
                        return std::pair<glm::vec3, glm::vec3>{localPos, localNormal};
                    }

                    const glm::uvec4 joints = jointIndices[idx];
                    const glm::vec4 weights = jointWeights[idx];
                    glm::vec3 skinnedPos{0.0F};
                    glm::vec3 skinnedNormal{0.0F};
                    float weightSum = 0.0F;

                    for (int k = 0; k < 4; ++k)
                    {
                        const float w = weights[static_cast<std::size_t>(k)];
                        if (w <= 1.0e-6F)
                        {
                            continue;
                        }
                        const std::uint32_t jointIndex = joints[static_cast<std::size_t>(k)];
                        if (jointIndex >= skinJointMatrices.size())
                        {
                            continue;
                        }
                        const glm::mat4& jointMat = skinJointMatrices[static_cast<std::size_t>(jointIndex)];
                        skinnedPos += w * glm::vec3(jointMat * glm::vec4(localPos, 1.0F));
                        skinnedNormal += w * glm::mat3(jointMat) * localNormal;
                        weightSum += w;
                    }

                    if (weightSum <= 1.0e-6F)
                    {
                        return std::pair<glm::vec3, glm::vec3>{localPos, localNormal};
                    }

                    glm::vec3 n = skinnedNormal;
                    if (glm::length(n) > 1.0e-6F)
                    {
                        n = glm::normalize(n);
                    }
                    else
                    {
                        n = localNormal;
                    }
                    return std::pair<glm::vec3, glm::vec3>{skinnedPos, n};
                };

                for (const auto& tri : triangles)
                {
                    const std::uint32_t ia = tri[0];
                    const std::uint32_t ib = flipWinding ? tri[2] : tri[1];
                    const std::uint32_t ic = flipWinding ? tri[1] : tri[2];
                    if (ia >= positions.size() || ib >= positions.size() || ic >= positions.size())
                    {
                        continue;
                    }

                    const glm::vec3 localNormalA = ia < normals.size() ? normals[ia] : glm::vec3{0.0F, 1.0F, 0.0F};
                    const glm::vec3 localNormalB = ib < normals.size() ? normals[ib] : glm::vec3{0.0F, 1.0F, 0.0F};
                    const glm::vec3 localNormalC = ic < normals.size() ? normals[ic] : glm::vec3{0.0F, 1.0F, 0.0F};
                    const auto [skinnedA, skinnedNormalA] = skinVertex(ia, positions[ia], localNormalA);
                    const auto [skinnedB, skinnedNormalB] = skinVertex(ib, positions[ib], localNormalB);
                    const auto [skinnedC, skinnedNormalC] = skinVertex(ic, positions[ic], localNormalC);

                    const glm::vec3 a = glm::vec3(worldTransform * glm::vec4(skinnedA, 1.0F));
                    const glm::vec3 b = glm::vec3(worldTransform * glm::vec4(skinnedB, 1.0F));
                    const glm::vec3 c = glm::vec3(worldTransform * glm::vec4(skinnedC, 1.0F));

                    boundsMin = glm::min(boundsMin, a);
                    boundsMin = glm::min(boundsMin, b);
                    boundsMin = glm::min(boundsMin, c);
                    boundsMax = glm::max(boundsMax, a);
                    boundsMax = glm::max(boundsMax, b);
                    boundsMax = glm::max(boundsMax, c);

                    glm::vec3 fallbackNormal = glm::cross(b - a, c - a);
                    if (glm::length(fallbackNormal) > 1.0e-6F)
                    {
                        fallbackNormal = glm::normalize(fallbackNormal);
                    }
                    else
                    {
                        fallbackNormal = glm::vec3{0.0F, 1.0F, 0.0F};
                    }

                    const auto pickNormal = [&](std::uint32_t idx) {
                        if (idx == ia)
                        {
                            glm::vec3 n = glm::vec3(normalTransform * skinnedNormalA);
                            if (glm::length(n) > 1.0e-6F)
                            {
                                return glm::normalize(n);
                            }
                        }
                        else if (idx == ib)
                        {
                            glm::vec3 n = glm::vec3(normalTransform * skinnedNormalB);
                            if (glm::length(n) > 1.0e-6F)
                            {
                                return glm::normalize(n);
                            }
                        }
                        else if (idx == ic)
                        {
                            glm::vec3 n = glm::vec3(normalTransform * skinnedNormalC);
                            if (glm::length(n) > 1.0e-6F)
                            {
                                return glm::normalize(n);
                            }
                        }
                        if (idx < normals.size() && !canSkin)
                        {
                            glm::vec3 n = glm::vec3(normalTransform * normals[idx]);
                            if (glm::length(n) > 1.0e-6F)
                            {
                                return glm::normalize(n);
                            }
                        }
                        return fallbackNormal;
                    };
                    const auto pickColor = [&](std::uint32_t idx) {
                        (void)idx;
                        const glm::vec3 factor = glm::vec3{baseColorFactor.r, baseColorFactor.g, baseColorFactor.b};
                        return glm::clamp(factor, glm::vec3{0.0F}, glm::vec3{1.0F});
                    };
                    const auto pickUv = [&](std::uint32_t idx) {
                        if (idx < texcoords.size())
                        {
                            return texcoords[idx];
                        }
                        return glm::vec2{0.0F, 0.0F};
                    };

                    out.geometry.positions.push_back(a);
                    out.geometry.positions.push_back(b);
                    out.geometry.positions.push_back(c);
                    out.geometry.normals.push_back(pickNormal(ia));
                    out.geometry.normals.push_back(pickNormal(ib));
                    out.geometry.normals.push_back(pickNormal(ic));
                    out.geometry.colors.push_back(pickColor(ia));
                    out.geometry.colors.push_back(pickColor(ib));
                    out.geometry.colors.push_back(pickColor(ic));
                    out.geometry.uvs.push_back(pickUv(ia));
                    out.geometry.uvs.push_back(pickUv(ib));
                    out.geometry.uvs.push_back(pickUv(ic));
                    out.geometry.indices.push_back(static_cast<std::uint32_t>(out.geometry.indices.size()));
                    out.geometry.indices.push_back(static_cast<std::uint32_t>(out.geometry.indices.size()));
                    out.geometry.indices.push_back(static_cast<std::uint32_t>(out.geometry.indices.size()));

                    primitiveSurface.geometry.positions.push_back(a);
                    primitiveSurface.geometry.positions.push_back(b);
                    primitiveSurface.geometry.positions.push_back(c);
                    primitiveSurface.geometry.normals.push_back(pickNormal(ia));
                    primitiveSurface.geometry.normals.push_back(pickNormal(ib));
                    primitiveSurface.geometry.normals.push_back(pickNormal(ic));
                    primitiveSurface.geometry.colors.push_back(pickColor(ia));
                    primitiveSurface.geometry.colors.push_back(pickColor(ib));
                    primitiveSurface.geometry.colors.push_back(pickColor(ic));
                    primitiveSurface.geometry.uvs.push_back(pickUv(ia));
                    primitiveSurface.geometry.uvs.push_back(pickUv(ib));
                    primitiveSurface.geometry.uvs.push_back(pickUv(ic));
                    primitiveSurface.geometry.indices.push_back(static_cast<std::uint32_t>(primitiveSurface.geometry.indices.size()));
                    primitiveSurface.geometry.indices.push_back(static_cast<std::uint32_t>(primitiveSurface.geometry.indices.size()));
                    primitiveSurface.geometry.indices.push_back(static_cast<std::uint32_t>(primitiveSurface.geometry.indices.size()));
                    emittedAnyTriangle = true;
                }
            }
            if (!primitiveSurface.geometry.positions.empty())
            {
                out.surfaces.push_back(std::move(primitiveSurface));
            }
        }
    }

    if (!emittedAnyTriangle || out.geometry.positions.empty())
    {
        out.error = "glTF has no renderable TRIANGLES primitives: " + absolutePath.generic_string();
        return out;
    }

    const glm::vec3 center = (boundsMin + boundsMax) * 0.5F;
    for (glm::vec3& p : out.geometry.positions)
    {
        p -= center;
    }
    for (MeshSurfaceData& surface : out.surfaces)
    {
        for (glm::vec3& p : surface.geometry.positions)
        {
            p -= center;
        }
    }
    out.boundsMin = boundsMin - center;
    out.boundsMax = boundsMax - center;
    out.loaded = true;
    out.error = warn;

    // Extract animations if callback is set
    if (animationCallback != nullptr && *animationCallback != nullptr && !model.animations.empty())
    {
        for (const tinygltf::Animation& anim : model.animations)
        {
            auto clip = std::make_unique<engine::animation::AnimationClip>();
            clip->name = anim.name.empty() ? "animation_" + std::to_string(&anim - &model.animations[0]) : anim.name;

            float maxTime = 0.0F;

            for (const tinygltf::AnimationChannel& channel : anim.channels)
            {
                if (channel.target_node < 0 || channel.target_node >= static_cast<int>(model.nodes.size()))
                {
                    continue;
                }
                if (channel.sampler < 0 || channel.sampler >= static_cast<int>(anim.samplers.size()))
                {
                    continue;
                }

                const tinygltf::AnimationSampler& sampler = anim.samplers[static_cast<std::size_t>(channel.sampler)];
                if (sampler.input < 0 || sampler.input >= static_cast<int>(model.accessors.size()))
                {
                    continue;
                }
                if (sampler.output < 0 || sampler.output >= static_cast<int>(model.accessors.size()))
                {
                    continue;
                }

                const tinygltf::Accessor& inputAccessor = model.accessors[static_cast<std::size_t>(sampler.input)];
                const tinygltf::Accessor& outputAccessor = model.accessors[static_cast<std::size_t>(sampler.output)];

                // Read keyframe times
                std::vector<float> times;
                if (inputAccessor.componentType == TINYGLTF_COMPONENT_TYPE_FLOAT && inputAccessor.type == TINYGLTF_TYPE_SCALAR)
                {
                    const tinygltf::BufferView* view = (inputAccessor.bufferView >= 0 && inputAccessor.bufferView < static_cast<int>(model.bufferViews.size()))
                        ? &model.bufferViews[static_cast<std::size_t>(inputAccessor.bufferView)] : nullptr;
                    if (view != nullptr && view->buffer >= 0 && view->buffer < static_cast<int>(model.buffers.size()))
                    {
                        const tinygltf::Buffer& buffer = model.buffers[static_cast<std::size_t>(view->buffer)];
                        const std::size_t stride = static_cast<std::size_t>(inputAccessor.ByteStride(*view) > 0 ? inputAccessor.ByteStride(*view) : sizeof(float));
                        const std::size_t baseOffset = static_cast<std::size_t>(view->byteOffset + inputAccessor.byteOffset);

                        times.reserve(inputAccessor.count);
                        for (std::size_t i = 0; i < inputAccessor.count; ++i)
                        {
                            const std::size_t offset = baseOffset + i * stride;
                            if (offset + sizeof(float) <= buffer.data.size())
                            {
                                float t = 0.0F;
                                std::memcpy(&t, buffer.data.data() + offset, sizeof(float));
                                times.push_back(t);
                                maxTime = std::max(maxTime, t);
                            }
                        }
                    }
                }

                if (times.empty())
                {
                    continue;
                }

                const int jointIndex = channel.target_node;

                // Read keyframe values based on path
                if (channel.target_path == "translation")
                {
                    engine::animation::TranslationChannel ch;
                    ch.jointIndex = jointIndex;
                    ch.times = times;

                    std::vector<glm::vec3> values;
                    if (ReadAccessorVec3Float(model, outputAccessor, &values, nullptr))
                    {
                        // Validate times and values arrays match
                        if (values.size() != times.size())
                        {
                            std::cout << "[ANIMATION] Warning: translation channel times/values count mismatch ("
                                      << times.size() << " vs " << values.size() << ") for joint " << jointIndex << "\n";
                            continue;
                        }
                        ch.values = std::move(values);
                        clip->translations.push_back(std::move(ch));
                    }
                }
                else if (channel.target_path == "rotation")
                {
                    engine::animation::RotationChannel ch;
                    ch.jointIndex = jointIndex;
                    ch.times = times;

                    // Read vec4 (quaternion)
                    if (outputAccessor.type == TINYGLTF_TYPE_VEC4 && outputAccessor.componentType == TINYGLTF_COMPONENT_TYPE_FLOAT)
                    {
                        const tinygltf::BufferView* view = (outputAccessor.bufferView >= 0 && outputAccessor.bufferView < static_cast<int>(model.bufferViews.size()))
                            ? &model.bufferViews[static_cast<std::size_t>(outputAccessor.bufferView)] : nullptr;
                        if (view != nullptr && view->buffer >= 0 && view->buffer < static_cast<int>(model.buffers.size()))
                        {
                            const tinygltf::Buffer& buffer = model.buffers[static_cast<std::size_t>(view->buffer)];
                            const std::size_t stride = static_cast<std::size_t>(outputAccessor.ByteStride(*view) > 0 ? outputAccessor.ByteStride(*view) : sizeof(float) * 4);
                            const std::size_t baseOffset = static_cast<std::size_t>(view->byteOffset + outputAccessor.byteOffset);

                            ch.values.reserve(outputAccessor.count);
                            for (std::size_t i = 0; i < outputAccessor.count; ++i)
                            {
                                const std::size_t offset = baseOffset + i * stride;
                                const std::size_t requiredSize = offset + sizeof(float) * 4;
                                if (requiredSize > buffer.data.size())
                                {
                                    // Bounds check failed - skip this keyframe
                                    std::cout << "[ANIMATION] Warning: buffer overflow prevented at offset " << offset
                                              << " (buffer size: " << buffer.data.size() << ", required: " << requiredSize << ")\n";
                                    continue;
                                }
                                float x = 0.0F, y = 0.0F, z = 0.0F, w = 0.0F;
                                std::memcpy(&x, buffer.data.data() + offset, sizeof(float));
                                std::memcpy(&y, buffer.data.data() + offset + sizeof(float), sizeof(float));
                                std::memcpy(&z, buffer.data.data() + offset + sizeof(float) * 2, sizeof(float));
                                std::memcpy(&w, buffer.data.data() + offset + sizeof(float) * 3, sizeof(float));
                                // glTF uses (x, y, z, w), glm uses (w, x, y, z)
                                glm::quat q{w, x, y, z};
                                if (glm::length(q) > 1.0e-6F)
                                {
                                    q = glm::normalize(q);
                                }
                                ch.values.push_back(q);
                            }
                        }
                    }
                    if (!ch.values.empty())
                    {
                        // Validate times and values arrays match
                        if (ch.values.size() != times.size())
                        {
                            std::cout << "[ANIMATION] Warning: rotation channel times/values count mismatch ("
                                      << times.size() << " vs " << ch.values.size() << ") for joint " << jointIndex << "\n";
                            ch.values.clear();
                        }
                        else
                        {
                            clip->rotations.push_back(std::move(ch));
                        }
                    }
                }
                else if (channel.target_path == "scale")
                {
                    engine::animation::ScaleChannel ch;
                    ch.jointIndex = jointIndex;
                    ch.times = times;

                    std::vector<glm::vec3> values;
                    if (ReadAccessorVec3Float(model, outputAccessor, &values, nullptr))
                    {
                        // Validate times and values arrays match
                        if (values.size() != times.size())
                        {
                            std::cout << "[ANIMATION] Warning: scale channel times/values count mismatch ("
                                      << times.size() << " vs " << values.size() << ") for joint " << jointIndex << "\n";
                            continue;
                        }
                        ch.values = std::move(values);
                        clip->scales.push_back(std::move(ch));
                    }
                }
            }

            clip->duration = maxTime;

            if (clip->Valid())
            {
                out.animationNames.push_back(clip->name);
                (*animationCallback)(clip->name, std::move(clip));
            }
            else
            {
                std::cout << "[ANIMATION] Warning: clip '" << clip->name << "' is invalid (duration=" << clip->duration << ")\n";
            }
        }
    }

    // Log discovered animation names
    if (!out.animationNames.empty())
    {
        std::cout << "[GLTF] Animations found in " << absolutePath.filename().string() << ":\n";
        for (const auto& name : out.animationNames)
        {
            std::cout << "  - " << name << "\n";
        }
    }

    return out;
}
} // namespace engine::assets
