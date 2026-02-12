#include "engine/render/Frustum.hpp"

#include <cmath>

#include <glm/geometric.hpp>
#include <glm/gtc/type_ptr.hpp>

namespace engine::render
{
void Frustum::Extract(const glm::mat4& viewProjection)
{
    const float* m = glm::value_ptr(viewProjection);

    m_planes[kLeft] = glm::vec4{
        m[3] + m[0],
        m[7] + m[4],
        m[11] + m[8],
        m[15] + m[12]
    };

    m_planes[kRight] = glm::vec4{
        m[3] - m[0],
        m[7] - m[4],
        m[11] - m[8],
        m[15] - m[12]
    };

    m_planes[kBottom] = glm::vec4{
        m[3] + m[1],
        m[7] + m[5],
        m[11] + m[9],
        m[15] + m[13]
    };

    m_planes[kTop] = glm::vec4{
        m[3] - m[1],
        m[7] - m[5],
        m[11] - m[9],
        m[15] - m[13]
    };

    m_planes[kNear] = glm::vec4{
        m[3] + m[2],
        m[7] + m[6],
        m[11] + m[10],
        m[15] + m[14]
    };

    m_planes[kFar] = glm::vec4{
        m[3] - m[2],
        m[7] - m[6],
        m[11] - m[10],
        m[15] - m[14]
    };

    for (auto& plane : m_planes)
    {
        const float len = std::sqrt(plane.x * plane.x + plane.y * plane.y + plane.z * plane.z);
        if (len > 1.0e-6F)
        {
            plane /= len;
        }
    }
}

bool Frustum::IntersectsAABB(const glm::vec3& min, const glm::vec3& max) const
{
    for (const auto& plane : m_planes)
    {
        glm::vec3 positive = min;
        if (plane.x >= 0.0F) positive.x = max.x;
        if (plane.y >= 0.0F) positive.y = max.y;
        if (plane.z >= 0.0F) positive.z = max.z;

        const float distance = plane.x * positive.x + plane.y * positive.y + plane.z * positive.z + plane.w;
        if (distance < 0.0F)
        {
            return false;
        }
    }
    return true;
}

bool Frustum::IntersectsSphere(const glm::vec3& center, float radius) const
{
    for (const auto& plane : m_planes)
    {
        const float distance = plane.x * center.x + plane.y * center.y + plane.z * center.z + plane.w;
        if (distance < -radius)
        {
            return false;
        }
    }
    return true;
}

bool Frustum::IntersectsPoint(const glm::vec3& point) const
{
    for (const auto& plane : m_planes)
    {
        const float distance = plane.x * point.x + plane.y * point.y + plane.z * point.z + plane.w;
        if (distance < 0.0F)
        {
            return false;
        }
    }
    return true;
}
}
