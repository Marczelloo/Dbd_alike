#pragma once

#include <array>

#include <glm/vec2.hpp>

struct GLFWwindow;

namespace engine::platform
{
class Input
{
public:
    void Update(GLFWwindow* window);

    [[nodiscard]] bool IsKeyDown(int key) const;
    [[nodiscard]] bool IsKeyPressed(int key) const;
    [[nodiscard]] bool IsKeyReleased(int key) const;
    [[nodiscard]] bool IsMouseDown(int button) const;
    [[nodiscard]] bool IsMousePressed(int button) const;
    [[nodiscard]] bool IsMouseReleased(int button) const;
    [[nodiscard]] glm::vec2 MousePosition() const { return m_mousePosition; }
    [[nodiscard]] glm::vec2 MouseDelta() const { return m_mouseDelta; }

private:
    static constexpr int kMaxKeys = 512;
    static constexpr int kMaxMouseButtons = 8;

    std::array<unsigned char, kMaxKeys> m_currentKeys{};
    std::array<unsigned char, kMaxKeys> m_previousKeys{};

    std::array<unsigned char, kMaxMouseButtons> m_currentMouse{};
    std::array<unsigned char, kMaxMouseButtons> m_previousMouse{};

    glm::vec2 m_mousePosition{0.0F, 0.0F};
    glm::vec2 m_mouseDelta{0.0F, 0.0F};
    bool m_firstMouseSample = true;
};
} // namespace engine::platform
