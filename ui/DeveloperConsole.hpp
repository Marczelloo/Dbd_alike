#pragma once

#include <functional>
#include <string>

namespace engine::platform
{
class Window;
}

namespace game::gameplay
{
class GameplaySystems;
struct HudState;
}

namespace ui
{
struct ConsoleContext
{
    game::gameplay::GameplaySystems* gameplay = nullptr;
    engine::platform::Window* window = nullptr;

    bool* vsync = nullptr;
    int* fpsLimit = nullptr;
    bool* showDebugOverlay = nullptr;
    bool renderPlayerHud = true;

    std::function<void(bool)> applyVsync;
    std::function<void(int)> applyFpsLimit;
    std::function<void(int, int)> applyResolution;
    std::function<void()> toggleFullscreen;

    std::function<void(const std::string&)> applyRenderMode;
    std::function<void(const std::string&)> setCameraMode;
    std::function<void(const std::string&)> setControlledRole;
    std::function<void(bool)> setPhysicsDebug;
    std::function<void(bool)> setNoClip;
    std::function<void(int)> setTickRate;
    std::function<void(int)> hostSession;
    std::function<void(const std::string&, int)> joinSession;
    std::function<void()> disconnectSession;
    std::function<std::string()> netStatus;
    std::function<std::string()> netDump;
    std::function<void()> lanScan;
    std::function<std::string()> lanStatus;
    std::function<void(bool)> lanDebug;
    std::function<void(bool)> setTerrorRadiusVisible;
    std::function<void(float)> setTerrorRadiusMeters;
    std::function<void(const std::string&)> requestRoleChange;
    std::function<std::string()> playerDump;
    std::function<void(const std::string&)> spawnRoleHere;
    std::function<void(const std::string&, int)> spawnRoleAt;
    std::function<std::string()> listSpawns;
};

class DeveloperConsole
{
public:
    bool Initialize(engine::platform::Window& window);
    void Shutdown();

    void BeginFrame();
    void Render(const ConsoleContext& context, float fps, const game::gameplay::HudState& hudState);

    void Toggle();
    [[nodiscard]] bool IsOpen() const;
    [[nodiscard]] bool WantsKeyboardCapture() const;

private:
#if BUILD_WITH_IMGUI
    struct Impl;
    Impl* m_impl = nullptr;
#endif
};
} // namespace ui
