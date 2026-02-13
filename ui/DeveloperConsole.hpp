#pragma once

#include <functional>
#include <string>
#include <glm/glm.hpp>

namespace engine::platform
{
class Window;
}

namespace game::gameplay
{
class GameplaySystems;
struct HudState;
}

namespace game::gameplay::perks
{
class PerkSystem;
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
    bool* showMovementWindow = nullptr;
    bool* showStatsWindow = nullptr;
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
    std::function<void(bool)> setTerrorAudioDebug;
    std::function<std::string()> terrorRadiusDump;
    std::function<void(const std::string&)> requestRoleChange;
    std::function<void(const std::string&, const std::string&, bool)> audioPlay;
    std::function<void()> audioStopAll;
    std::function<std::string()> playerDump;
    std::function<std::string()> sceneDump;
    std::function<void(const std::string&)> spawnRoleHere;
    std::function<void(const std::string&, int)> spawnRoleAt;
    std::function<std::string()> listSpawns;
    std::function<void(float)> setKillerLightIntensity;
    std::function<void(float)> setKillerLookLightAngle;
    std::function<void(float)> setKillerLookLightPitch;
    std::function<void(bool)> setKillerLookLightDebug;

    // Profiler callbacks.
    std::function<void()> profilerToggle;
    std::function<void(bool)> profilerSetPinned;
    std::function<void(bool)> profilerSetCompact;
    std::function<void(int)> profilerBenchmark;
    std::function<void()> profilerBenchmarkStop;
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

    // Color palette for console messages
    struct ConsoleColors
    {
        static constexpr glm::vec4 Command{0.0F, 0.75F, 1.0F, 1.0F};      // Cyan
        static constexpr glm::vec4 Success{0.0F, 0.9F, 0.3F, 1.0F};       // Green
        static constexpr glm::vec4 Error{1.0F, 0.3F, 0.3F, 1.0F};         // Red
        static constexpr glm::vec4 Warning{1.0F, 0.75F, 0.0F, 1.0F};      // Orange/Yellow
        static constexpr glm::vec4 Info{0.7F, 0.7F, 0.85F, 1.0F};        // Light gray-ish blue
        static constexpr glm::vec4 Category{0.6F, 0.9F, 0.95F, 1.0F};    // Light cyan
        static constexpr glm::vec4 Value{0.9F, 0.85F, 0.7F, 1.0F};       // Light yellow
        static constexpr glm::vec4 Default{0.9F, 0.9F, 0.9F, 1.0F};      // White-ish
    };
};
} // namespace ui
