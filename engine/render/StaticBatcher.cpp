#include "engine/render/StaticBatcher.hpp"
#include "engine/core/JobSystem.hpp"

#include <algorithm>
#include <cmath>
#include <atomic>

#include <glad/glad.h>

#include <glm/common.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "engine/core/Profiler.hpp"

namespace engine::render
{
StaticBatcher::StaticBatcher() = default;

StaticBatcher::~StaticBatcher()
{
    Clear();
}

void StaticBatcher::BeginBuild()
{
    m_buildVertices.clear();
    m_chunks.clear();
    m_cachedFirsts.clear();
    m_cachedCounts.clear();
    m_built = false;
    m_vertexCount = 0;
    m_visibleCount = 0;
}

void StaticBatcher::AddBox(const glm::vec3& center, const glm::vec3& halfExtents, const glm::vec3& color)
{
    const glm::vec3 min = center - halfExtents;
    const glm::vec3 max = center + halfExtents;

    const glm::vec3 c000 = min;
    const glm::vec3 c001 = glm::vec3{min.x, min.y, max.z};
    const glm::vec3 c010 = glm::vec3{min.x, max.y, min.z};
    const glm::vec3 c011 = glm::vec3{min.x, max.y, max.z};
    const glm::vec3 c100 = glm::vec3{max.x, min.y, min.z};
    const glm::vec3 c101 = glm::vec3{max.x, min.y, max.z};
    const glm::vec3 c110 = glm::vec3{max.x, max.y, min.z};
    const glm::vec3 c111 = max;

    auto emitVertex = [&](const glm::vec3& pos, const glm::vec3& normal) {
        m_buildVertices.push_back(pos.x);
        m_buildVertices.push_back(pos.y);
        m_buildVertices.push_back(pos.z);
        m_buildVertices.push_back(normal.x);
        m_buildVertices.push_back(normal.y);
        m_buildVertices.push_back(normal.z);
        m_buildVertices.push_back(color.r);
        m_buildVertices.push_back(color.g);
        m_buildVertices.push_back(color.b);
        m_buildVertices.push_back(0.55F);
        m_buildVertices.push_back(0.0F);
        m_buildVertices.push_back(0.0F);
        m_buildVertices.push_back(0.0F);
    };

    auto emitTri = [&](const glm::vec3& a, const glm::vec3& b, const glm::vec3& c, const glm::vec3& n) {
        emitVertex(a, n);
        emitVertex(b, n);
        emitVertex(c, n);
    };

    emitTri(c000, c001, c011, glm::vec3{-1.0F, 0.0F, 0.0F});
    emitTri(c000, c011, c010, glm::vec3{-1.0F, 0.0F, 0.0F});

    emitTri(c100, c110, c111, glm::vec3{1.0F, 0.0F, 0.0F});
    emitTri(c100, c111, c101, glm::vec3{1.0F, 0.0F, 0.0F});

    emitTri(c000, c010, c110, glm::vec3{0.0F, 0.0F, -1.0F});
    emitTri(c000, c110, c100, glm::vec3{0.0F, 0.0F, -1.0F});

    emitTri(c001, c101, c111, glm::vec3{0.0F, 0.0F, 1.0F});
    emitTri(c001, c111, c011, glm::vec3{0.0F, 0.0F, 1.0F});

    emitTri(c000, c100, c101, glm::vec3{0.0F, -1.0F, 0.0F});
    emitTri(c000, c101, c001, glm::vec3{0.0F, -1.0F, 0.0F});

    emitTri(c010, c011, c111, glm::vec3{0.0F, 1.0F, 0.0F});
    emitTri(c010, c111, c110, glm::vec3{0.0F, 1.0F, 0.0F});

    BatchChunk chunk;
    chunk.firstVertex = m_chunks.empty() ? 0 : (m_chunks.back().firstVertex + m_chunks.back().vertexCount);
    chunk.vertexCount = kVerticesPerBox;
    chunk.boundsMin = min;
    chunk.boundsMax = max;
    m_chunks.push_back(chunk);
}

void StaticBatcher::EndBuild()
{
    if (m_buildVertices.empty())
    {
        m_built = true;
        return;
    }

    if (m_vao == 0)
    {
        glGenVertexArrays(1, &m_vao);
        glGenBuffers(1, &m_vbo);
    }

    m_vertexCount = m_buildVertices.size() / 13;

    glBindVertexArray(m_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferData(
        GL_ARRAY_BUFFER,
        static_cast<GLsizeiptr>(m_buildVertices.size() * sizeof(float)),
        m_buildVertices.data(),
        GL_STATIC_DRAW
    );

    constexpr GLsizei stride = 13 * sizeof(float);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(0));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(3 * sizeof(float)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(6 * sizeof(float)));
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(9 * sizeof(float)));

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);

    m_buildVertices.clear();
    m_buildVertices.shrink_to_fit();
    m_built = true;
}

void StaticBatcher::Render(
    const glm::mat4& viewProjection,
    const Frustum& frustum,
    unsigned int shaderProgram,
    int viewProjLocation,
    int modelLocation
)
{
    if (!m_built || m_vao == 0 || m_chunks.empty())
    {
        return;
    }

    m_visibleCount = 0;
    m_cachedFirsts.clear();
    m_cachedCounts.clear();
    m_cachedFirsts.reserve(m_chunks.size());
    m_cachedCounts.reserve(m_chunks.size());

    // Use parallel culling for large chunk counts
    const std::size_t chunkCount = m_chunks.size();
    auto& jobSystem = engine::core::JobSystem::Instance();
    
    if (jobSystem.IsInitialized() && jobSystem.IsEnabled() && chunkCount > 256)
    {
        // Parallel culling
        std::vector<std::int8_t> visibleFlags(chunkCount, 0);
        engine::core::JobCounter cullCounter;
        
        jobSystem.ParallelFor(chunkCount, 64, [&frustum, &visibleFlags, this](std::size_t idx) {
            const auto& chunk = m_chunks[idx];
            if (frustum.IntersectsAABB(chunk.boundsMin, chunk.boundsMax))
            {
                visibleFlags[idx] = 1;
            }
        }, engine::core::JobPriority::High, &cullCounter);
        
        jobSystem.WaitForCounter(cullCounter);
        
        // Collect visible chunks
        for (std::size_t i = 0; i < chunkCount; ++i)
        {
            if (visibleFlags[i])
            {
                m_cachedFirsts.push_back(static_cast<GLint>(m_chunks[i].firstVertex));
                m_cachedCounts.push_back(static_cast<GLsizei>(m_chunks[i].vertexCount));
                m_visibleCount += m_chunks[i].vertexCount;
            }
        }
    }
    else
    {
        // Sequential culling (fallback)
        for (const auto& chunk : m_chunks)
        {
            if (frustum.IntersectsAABB(chunk.boundsMin, chunk.boundsMax))
            {
                m_cachedFirsts.push_back(static_cast<GLint>(chunk.firstVertex));
                m_cachedCounts.push_back(static_cast<GLsizei>(chunk.vertexCount));
                m_visibleCount += chunk.vertexCount;
            }
        }
    }

    if (m_cachedFirsts.empty())
    {
        return;
    }

    glUseProgram(shaderProgram);
    glUniformMatrix4fv(viewProjLocation, 1, GL_FALSE, glm::value_ptr(viewProjection));
    if (modelLocation >= 0)
    {
        const glm::mat4 identity{1.0F};
        glUniformMatrix4fv(modelLocation, 1, GL_FALSE, glm::value_ptr(identity));
    }

    glBindVertexArray(m_vao);
    glMultiDrawArrays(
        GL_TRIANGLES,
        m_cachedFirsts.data(),
        m_cachedCounts.data(),
        static_cast<GLsizei>(m_cachedFirsts.size())
    );

    // Record stats in profiler.
    auto& profiler = engine::core::Profiler::Instance();
    profiler.RecordDrawCall(static_cast<std::uint32_t>(m_visibleCount), static_cast<std::uint32_t>(m_visibleCount / 3));
    profiler.StatsMut().staticBatchChunksVisible = static_cast<std::uint32_t>(m_cachedFirsts.size());
    profiler.StatsMut().staticBatchChunksTotal = static_cast<std::uint32_t>(m_chunks.size());
}

void StaticBatcher::Clear()
{
    if (m_vbo != 0)
    {
        glDeleteBuffers(1, &m_vbo);
        m_vbo = 0;
    }
    if (m_vao != 0)
    {
        glDeleteVertexArrays(1, &m_vao);
        m_vao = 0;
    }
    m_buildVertices.clear();
    m_chunks.clear();
    m_cachedFirsts.clear();
    m_cachedCounts.clear();
    m_vertexCount = 0;
    m_visibleCount = 0;
    m_built = false;
}
}
