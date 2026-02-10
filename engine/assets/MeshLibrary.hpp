#pragma once

#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

#include <glm/vec3.hpp>

#include "engine/render/Renderer.hpp"

namespace engine::assets
{
struct MeshData
{
    render::MeshGeometry geometry;
    glm::vec3 boundsMin{0.0F};
    glm::vec3 boundsMax{0.0F};
    bool loaded = false;
    std::string error;
};

class MeshLibrary
{
public:
    const MeshData* LoadMesh(const std::filesystem::path& absolutePath, std::string* outError = nullptr);
    void Clear();

private:
    static MeshData LoadObj(const std::filesystem::path& absolutePath);
    static MeshData LoadGltf(const std::filesystem::path& absolutePath);

    std::unordered_map<std::string, MeshData> m_cache;
};
} // namespace engine::assets
