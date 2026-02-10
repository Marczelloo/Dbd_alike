#include "engine/platform/Input.hpp"

#include <GLFW/glfw3.h>

namespace engine::platform
{
void Input::Update(GLFWwindow* window)
{
    m_previousKeys = m_currentKeys;
    m_previousMouse = m_currentMouse;

    for (int key = 0; key < kMaxKeys; ++key)
    {
        m_currentKeys[static_cast<size_t>(key)] = static_cast<unsigned char>(glfwGetKey(window, key) == GLFW_PRESS);
    }

    for (int button = 0; button < kMaxMouseButtons; ++button)
    {
        m_currentMouse[static_cast<size_t>(button)] = static_cast<unsigned char>(glfwGetMouseButton(window, button) == GLFW_PRESS);
    }

    double mouseX = 0.0;
    double mouseY = 0.0;
    glfwGetCursorPos(window, &mouseX, &mouseY);

    const glm::vec2 newPosition{static_cast<float>(mouseX), static_cast<float>(mouseY)};
    if (m_firstMouseSample)
    {
        m_mousePosition = newPosition;
        m_mouseDelta = glm::vec2{0.0F};
        m_firstMouseSample = false;
    }
    else
    {
        m_mouseDelta = newPosition - m_mousePosition;
        m_mousePosition = newPosition;
    }
}

bool Input::IsKeyDown(int key) const
{
    if (key < 0 || key >= kMaxKeys)
    {
        return false;
    }
    return m_currentKeys[static_cast<size_t>(key)] != 0;
}

bool Input::IsKeyPressed(int key) const
{
    if (key < 0 || key >= kMaxKeys)
    {
        return false;
    }
    const size_t index = static_cast<size_t>(key);
    return m_currentKeys[index] != 0 && m_previousKeys[index] == 0;
}

bool Input::IsKeyReleased(int key) const
{
    if (key < 0 || key >= kMaxKeys)
    {
        return false;
    }
    const size_t index = static_cast<size_t>(key);
    return m_currentKeys[index] == 0 && m_previousKeys[index] != 0;
}

bool Input::IsMouseDown(int button) const
{
    if (button < 0 || button >= kMaxMouseButtons)
    {
        return false;
    }
    return m_currentMouse[static_cast<size_t>(button)] != 0;
}

bool Input::IsMousePressed(int button) const
{
    if (button < 0 || button >= kMaxMouseButtons)
    {
        return false;
    }
    const size_t index = static_cast<size_t>(button);
    return m_currentMouse[index] != 0 && m_previousMouse[index] == 0;
}

bool Input::IsMouseReleased(int button) const
{
    if (button < 0 || button >= kMaxMouseButtons)
    {
        return false;
    }
    const size_t index = static_cast<size_t>(button);
    return m_currentMouse[index] == 0 && m_previousMouse[index] != 0;
}
} // namespace engine::platform
