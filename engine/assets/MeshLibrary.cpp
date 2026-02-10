#include "engine/assets/MeshLibrary.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include <glm/common.hpp>
#include <glm/geometric.hpp>

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
        loaded = LoadGltf(absolutePath);
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
    out.geometry.indices.clear();
    out.geometry.positions.reserve(triangles.size() * 3);
    out.geometry.normals.reserve(triangles.size() * 3);
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
    out.loaded = true;
    out.error.clear();
    return out;
}

MeshData MeshLibrary::LoadGltf(const std::filesystem::path& absolutePath)
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
    out.geometry.indices.clear();

    glm::vec3 boundsMin{1.0e9F};
    glm::vec3 boundsMax{-1.0e9F};

    bool emittedAnyTriangle = false;

    for (const tinygltf::Mesh& mesh : model.meshes)
    {
        for (const tinygltf::Primitive& primitive : mesh.primitives)
        {
            const int mode = primitive.mode == -1 ? TINYGLTF_MODE_TRIANGLES : primitive.mode;
            if (mode != TINYGLTF_MODE_TRIANGLES)
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

            for (std::size_t triStart = 0; triStart + 2 < primitiveIndices.size(); triStart += 3)
            {
                const std::uint32_t ia = primitiveIndices[triStart];
                const std::uint32_t ib = primitiveIndices[triStart + 1];
                const std::uint32_t ic = primitiveIndices[triStart + 2];
                if (ia >= positions.size() || ib >= positions.size() || ic >= positions.size())
                {
                    continue;
                }

                const glm::vec3 a = positions[ia];
                const glm::vec3 b = positions[ib];
                const glm::vec3 c = positions[ic];

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
                    if (idx < normals.size())
                    {
                        glm::vec3 n = normals[idx];
                        if (glm::length(n) > 1.0e-6F)
                        {
                            return glm::normalize(n);
                        }
                    }
                    return fallbackNormal;
                };

                out.geometry.positions.push_back(a);
                out.geometry.positions.push_back(b);
                out.geometry.positions.push_back(c);
                out.geometry.normals.push_back(pickNormal(ia));
                out.geometry.normals.push_back(pickNormal(ib));
                out.geometry.normals.push_back(pickNormal(ic));
                out.geometry.indices.push_back(static_cast<std::uint32_t>(out.geometry.indices.size()));
                out.geometry.indices.push_back(static_cast<std::uint32_t>(out.geometry.indices.size()));
                out.geometry.indices.push_back(static_cast<std::uint32_t>(out.geometry.indices.size()));
                emittedAnyTriangle = true;
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
    out.boundsMin = boundsMin - center;
    out.boundsMax = boundsMax - center;
    out.loaded = true;
    out.error = warn;
    return out;
}
} // namespace engine::assets
