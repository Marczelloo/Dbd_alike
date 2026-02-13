#pragma once

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

namespace engine::render
{
class Frustum
{
public:
    void Extract(const glm::mat4& viewProjection);

    [[nodiscard]] bool IntersectsAABB(const glm::vec3& min, const glm::vec3& max) const;
    [[nodiscard]] bool IntersectsSphere(const glm::vec3& center, float radius) const;
    [[nodiscard]] bool IntersectsPoint(const glm::vec3& point) const;

private:
    glm::vec4 m_planes[6]{};

    static constexpr int kLeft = 0;
    static constexpr int kRight = 1;
    static constexpr int kBottom = 2;
    static constexpr int kTop = 3;
    static constexpr int kNear = 4;
    static constexpr int kFar = 5;
};
}
