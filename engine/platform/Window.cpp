#include "engine/platform/Window.hpp"

#include <algorithm>
#include <iostream>

#include <GLFW/glfw3.h>

namespace engine::platform
{
Window::~Window()
{
    Shutdown();
}

bool Window::Initialize(const WindowSettings& settings)
{
    if (glfwInit() != GLFW_TRUE)
    {
        std::cerr << "Failed to initialize GLFW.\n";
        return false;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 5);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#if defined(__APPLE__)
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
#endif

    m_windowWidth = static_cast<int>(static_cast<float>(settings.width) * settings.windowScale);
    m_windowHeight = static_cast<int>(static_cast<float>(settings.height) * settings.windowScale);
    m_fullscreen = settings.fullscreen;
    m_displayMode = settings.fullscreen ? DisplayMode::Fullscreen : DisplayMode::Windowed;

    GLFWmonitor* monitor = settings.fullscreen ? glfwGetPrimaryMonitor() : nullptr;
    m_window = glfwCreateWindow(m_windowWidth, m_windowHeight, settings.title.c_str(), monitor, nullptr);

    if (m_window == nullptr)
    {
        std::cerr << "Failed to create GLFW window.\n";
        glfwTerminate();
        return false;
    }

    glfwMakeContextCurrent(m_window);
    glfwSetWindowUserPointer(m_window, this);
    glfwSetFramebufferSizeCallback(m_window, FramebufferResizeCallback);
    glfwSetDropCallback(m_window, FileDropCallback);
    glfwGetFramebufferSize(m_window, &m_fbWidth, &m_fbHeight);

    SetVSync(settings.vsync);
    return true;
}

void Window::Shutdown()
{
    if (m_window != nullptr)
    {
        glfwDestroyWindow(m_window);
        m_window = nullptr;
        glfwTerminate();
    }
}

void Window::PollEvents() const
{
    glfwPollEvents();
}

void Window::SwapBuffers() const
{
    if (m_window != nullptr)
    {
        glfwSwapBuffers(m_window);
    }
}

bool Window::ShouldClose() const
{
    return m_window == nullptr || glfwWindowShouldClose(m_window) == GLFW_TRUE;
}

void Window::SetShouldClose(bool shouldClose) const
{
    if (m_window != nullptr)
    {
        glfwSetWindowShouldClose(m_window, shouldClose ? GLFW_TRUE : GLFW_FALSE);
    }
}

void Window::SetVSync(bool enabled) const
{
    glfwSwapInterval(enabled ? 1 : 0);
}

void Window::SetResolution(int width, int height)
{
    m_windowWidth = width;
    m_windowHeight = height;
    if (!m_fullscreen)
    {
        m_windowedWidth = width;
        m_windowedHeight = height;
    }
    if (m_window != nullptr)
    {
        if (m_displayMode == DisplayMode::Windowed)
        {
            glfwSetWindowSize(m_window, width, height);
        }
        else
        {
            GLFWmonitor* primaryMonitor = glfwGetPrimaryMonitor();
            const GLFWvidmode* mode = glfwGetVideoMode(primaryMonitor);
            const int refreshRate = mode != nullptr ? mode->refreshRate : GLFW_DONT_CARE;
            glfwSetWindowMonitor(m_window, primaryMonitor, 0, 0, width, height, refreshRate);
        }
    }
}

void Window::SetDisplayMode(DisplayMode mode, int width, int height)
{
    if (m_window == nullptr)
    {
        return;
    }

    width = std::max(320, width);
    height = std::max(200, height);

    GLFWmonitor* primaryMonitor = glfwGetPrimaryMonitor();
    const GLFWvidmode* primaryMode = glfwGetVideoMode(primaryMonitor);

    if (mode == DisplayMode::Windowed)
    {
        if (m_displayMode != DisplayMode::Windowed)
        {
            glfwSetWindowMonitor(m_window, nullptr, m_windowedX, m_windowedY, width, height, 0);
        }
        else
        {
            glfwSetWindowSize(m_window, width, height);
        }
        m_windowedWidth = width;
        m_windowedHeight = height;
        m_windowWidth = width;
        m_windowHeight = height;
        m_displayMode = DisplayMode::Windowed;
        m_fullscreen = false;
        return;
    }

    glfwGetWindowPos(m_window, &m_windowedX, &m_windowedY);
    glfwGetWindowSize(m_window, &m_windowedWidth, &m_windowedHeight);

    int targetWidth = width;
    int targetHeight = height;
    if (mode == DisplayMode::Borderless && primaryMode != nullptr)
    {
        targetWidth = primaryMode->width;
        targetHeight = primaryMode->height;
    }

    const int refreshRate = primaryMode != nullptr ? primaryMode->refreshRate : GLFW_DONT_CARE;
    glfwSetWindowMonitor(m_window, primaryMonitor, 0, 0, targetWidth, targetHeight, refreshRate);
    m_windowWidth = targetWidth;
    m_windowHeight = targetHeight;
    m_displayMode = mode;
    m_fullscreen = true;
}

void Window::ToggleFullscreen()
{
    if (m_window == nullptr)
    {
        return;
    }

    if (m_displayMode == DisplayMode::Windowed)
    {
        SetDisplayMode(DisplayMode::Fullscreen, m_windowWidth, m_windowHeight);
    }
    else
    {
        SetDisplayMode(DisplayMode::Windowed, m_windowedWidth, m_windowedHeight);
    }
}

void Window::SetCursorCaptured(bool captured) const
{
    if (m_window == nullptr)
    {
        return;
    }

    glfwSetInputMode(m_window, GLFW_CURSOR, captured ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
}

void Window::SetResizeCallback(std::function<void(int, int)> callback)
{
    m_resizeCallback = std::move(callback);
}

void Window::SetFileDropCallback(std::function<void(const std::vector<std::string>&)> callback)
{
    m_fileDropCallback = std::move(callback);
}

void Window::FramebufferResizeCallback(GLFWwindow* window, int width, int height)
{
    Window* self = static_cast<Window*>(glfwGetWindowUserPointer(window));
    if (self == nullptr)
    {
        return;
    }

    self->m_fbWidth = width;
    self->m_fbHeight = height;
    if (self->m_resizeCallback)
    {
        self->m_resizeCallback(width, height);
    }
}

void Window::FileDropCallback(GLFWwindow* window, int pathCount, const char** paths)
{
    Window* self = static_cast<Window*>(glfwGetWindowUserPointer(window));
    if (self == nullptr || pathCount <= 0 || paths == nullptr || !self->m_fileDropCallback)
    {
        return;
    }

    std::vector<std::string> dropped;
    dropped.reserve(static_cast<std::size_t>(pathCount));
    for (int i = 0; i < pathCount; ++i)
    {
        if (paths[i] != nullptr && paths[i][0] != '\0')
        {
            dropped.emplace_back(paths[i]);
        }
    }
    if (!dropped.empty())
    {
        self->m_fileDropCallback(dropped);
    }
}
} // namespace engine::platform
