#pragma once

#include <cstdint>
#include <string>

namespace engine::platform
{
class Window;
}

namespace game::gameplay
{
struct HudState;
}

namespace ui
{
struct ToolbarContext
{
    bool* showNetworkOverlay = nullptr;
    bool* showPlayersWindow = nullptr;
    bool* showDebugOverlay = nullptr;
    bool* showMovementWindow = nullptr;
    bool* showStatsWindow = nullptr;
    bool* showControlsWindow = nullptr;
    bool* showUiTestPanel = nullptr;
    bool* showLoadingScreenTestPanel = nullptr;

    float fps = 0.0f;
    int tickRate = 60;
    std::string renderMode;
};

class DeveloperToolbar
{
public:
    bool Initialize(engine::platform::Window& window);
    void Shutdown();

    void Render(const ToolbarContext& context);

private:
#if BUILD_WITH_IMGUI
    struct Impl;
    Impl* m_impl = nullptr;
#endif
};
} // namespace ui
