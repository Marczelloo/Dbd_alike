#pragma once

#include <cstdint>
#include <vector>

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

#include <glad/glad.h>

#include "engine/render/Frustum.hpp"

namespace engine::render
{
struct SolidVertex;

class StaticBatcher
{
public:
    StaticBatcher();
    ~StaticBatcher();

    void BeginBuild();
    void AddBox(const glm::vec3& center, const glm::vec3& halfExtents, const glm::vec3& color);
    void EndBuild();

    void Render(const glm::mat4& viewProjection, const Frustum& frustum, unsigned int shaderProgram, int viewProjLocation);
    void Clear();

    [[nodiscard]] bool IsBuilt() const { return m_built; }
    [[nodiscard]] std::size_t VertexCount() const { return m_vertexCount; }
    [[nodiscard]] std::size_t VisibleCount() const { return m_visibleCount; }

private:
    struct BatchChunk
    {
        std::size_t firstVertex = 0;
        std::size_t vertexCount = 0;
        glm::vec3 boundsMin{};
        glm::vec3 boundsMax{};
    };

    unsigned int m_vao = 0;
    unsigned int m_vbo = 0;
    std::vector<float> m_buildVertices;
    std::vector<BatchChunk> m_chunks;
    std::vector<GLint> m_cachedFirsts;
    std::vector<GLsizei> m_cachedCounts;
    std::size_t m_vertexCount = 0;
    std::size_t m_visibleCount = 0;
    bool m_built = false;

    static constexpr std::size_t kVerticesPerBox = 36;
};
}
