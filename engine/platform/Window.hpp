#pragma once

#include <functional>
#include <string>

struct GLFWwindow;

namespace engine::platform
{
struct WindowSettings
{
    int width = 1600;
    int height = 900;
    float windowScale = 1.0F;
    bool fullscreen = false;
    bool vsync = true;
    int fpsLimit = 120;
    std::string title = "Asymmetric Horror Prototype";
};

class Window
{
public:
    enum class DisplayMode
    {
        Windowed,
        Fullscreen,
        Borderless
    };

    Window() = default;
    ~Window();

    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    bool Initialize(const WindowSettings& settings);
    void Shutdown();

    void PollEvents() const;
    void SwapBuffers() const;

    [[nodiscard]] bool ShouldClose() const;
    void SetShouldClose(bool shouldClose) const;

    [[nodiscard]] GLFWwindow* NativeHandle() const { return m_window; }

    void SetVSync(bool enabled) const;
    void SetResolution(int width, int height);
    void SetDisplayMode(DisplayMode mode, int width, int height);
    void ToggleFullscreen();
    void SetCursorCaptured(bool captured) const;

    [[nodiscard]] int FramebufferWidth() const { return m_fbWidth; }
    [[nodiscard]] int FramebufferHeight() const { return m_fbHeight; }
    [[nodiscard]] int WindowWidth() const { return m_windowWidth; }
    [[nodiscard]] int WindowHeight() const { return m_windowHeight; }
    [[nodiscard]] bool IsFullscreen() const { return m_fullscreen; }
    [[nodiscard]] DisplayMode GetDisplayMode() const { return m_displayMode; }

    void SetResizeCallback(std::function<void(int, int)> callback);

private:
    static void FramebufferResizeCallback(GLFWwindow* window, int width, int height);

    GLFWwindow* m_window = nullptr;
    std::function<void(int, int)> m_resizeCallback;

    int m_windowedX = 100;
    int m_windowedY = 100;
    int m_windowedWidth = 1600;
    int m_windowedHeight = 900;

    int m_windowWidth = 1600;
    int m_windowHeight = 900;
    int m_fbWidth = 1600;
    int m_fbHeight = 900;

    bool m_fullscreen = false;
    DisplayMode m_displayMode = DisplayMode::Windowed;
};
} // namespace engine::platform
