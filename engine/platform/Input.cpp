#include "engine/platform/Input.hpp"

#include <cmath>

#include <GLFW/glfw3.h>

namespace engine::platform
{
void Input::Update(GLFWwindow* window)
{
    m_previousKeys = m_currentKeys;
    m_previousMouse = m_currentMouse;
    m_previousGamepadButtons = m_currentGamepadButtons;
    m_previousGamepadAxes = m_currentGamepadAxes;

    for (int key = 0; key < kMaxKeys; ++key)
    {
        m_currentKeys[static_cast<size_t>(key)] = static_cast<unsigned char>(glfwGetKey(window, key) == GLFW_PRESS);
    }

    for (int button = 0; button < kMaxMouseButtons; ++button)
    {
        m_currentMouse[static_cast<size_t>(button)] = static_cast<unsigned char>(glfwGetMouseButton(window, button) == GLFW_PRESS);
    }

    if (m_activeGamepadId >= GLFW_JOYSTICK_1 && m_activeGamepadId <= GLFW_JOYSTICK_LAST
        && (!glfwJoystickPresent(m_activeGamepadId) || !glfwJoystickIsGamepad(m_activeGamepadId)))
    {
        m_activeGamepadId = -1;
    }
    if (m_activeGamepadId == -1)
    {
        for (int jid = GLFW_JOYSTICK_1; jid <= GLFW_JOYSTICK_LAST; ++jid)
        {
            if (glfwJoystickPresent(jid) && glfwJoystickIsGamepad(jid))
            {
                m_activeGamepadId = jid;
                break;
            }
        }
    }

    m_currentGamepadButtons.fill(0);
    m_currentGamepadAxes.fill(0.0F);
    if (m_activeGamepadId != -1)
    {
        GLFWgamepadstate gamepadState{};
        if (glfwGetGamepadState(m_activeGamepadId, &gamepadState) == GLFW_TRUE)
        {
            for (int button = 0; button <= GLFW_GAMEPAD_BUTTON_LAST && button < kMaxGamepadButtons; ++button)
            {
                m_currentGamepadButtons[static_cast<size_t>(button)] = static_cast<unsigned char>(gamepadState.buttons[button] == GLFW_PRESS);
            }
            for (int axis = 0; axis <= GLFW_GAMEPAD_AXIS_LAST && axis < kMaxGamepadAxes; ++axis)
            {
                m_currentGamepadAxes[static_cast<size_t>(axis)] = gamepadState.axes[axis];
            }
        }
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

bool Input::IsGamepadConnected() const
{
    return m_activeGamepadId != -1;
}

bool Input::IsGamepadButtonDown(int button) const
{
    if (button < 0 || button >= kMaxGamepadButtons)
    {
        return false;
    }
    return m_currentGamepadButtons[static_cast<size_t>(button)] != 0;
}

bool Input::IsGamepadButtonPressed(int button) const
{
    if (button < 0 || button >= kMaxGamepadButtons)
    {
        return false;
    }
    const size_t index = static_cast<size_t>(button);
    return m_currentGamepadButtons[index] != 0 && m_previousGamepadButtons[index] == 0;
}

bool Input::IsGamepadButtonReleased(int button) const
{
    if (button < 0 || button >= kMaxGamepadButtons)
    {
        return false;
    }
    const size_t index = static_cast<size_t>(button);
    return m_currentGamepadButtons[index] == 0 && m_previousGamepadButtons[index] != 0;
}

float Input::GamepadAxis(int axis, float deadzone) const
{
    if (axis < 0 || axis >= kMaxGamepadAxes)
    {
        return 0.0F;
    }
    const float value = m_currentGamepadAxes[static_cast<size_t>(axis)];
    return std::abs(value) < deadzone ? 0.0F : value;
}
} // namespace engine::platform
