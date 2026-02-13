#include "ui/DeveloperToolbar.hpp"

#include <string>

#include "engine/platform/Window.hpp"

#if BUILD_WITH_IMGUI
#include <imgui.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_opengl3.h>
#endif

namespace ui
{
#if BUILD_WITH_IMGUI
struct DeveloperToolbar::Impl
{
    ImFont* font = nullptr;
};
#endif

bool DeveloperToolbar::Initialize(engine::platform::Window& window)
{
#if BUILD_WITH_IMGUI
    (void)window; // Window not currently used but kept for API consistency
    m_impl = new Impl();
#else
    (void)window;
#endif
    return true;
}

void DeveloperToolbar::Shutdown()
{
#if BUILD_WITH_IMGUI
    if (m_impl != nullptr)
    {
        delete m_impl;
        m_impl = nullptr;
    }
#endif
}

void DeveloperToolbar::Render(const ToolbarContext& context)
{
#if BUILD_WITH_IMGUI
    if (m_impl == nullptr)
    {
        return;
    }

    // Get main viewport size
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    const ImVec2 viewportSize = viewport->Size;

    // Set window to be at the top, full width, with no decorations
    ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
    ImGui::SetNextWindowSize(ImVec2(viewportSize.x, 0.0f));
    ImGui::SetNextWindowBgAlpha(0.9f);

    const ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoTitleBar |
                                         ImGuiWindowFlags_NoResize |
                                         ImGuiWindowFlags_NoMove |
                                         ImGuiWindowFlags_NoScrollbar |
                                         ImGuiWindowFlags_NoCollapse |
                                         ImGuiWindowFlags_NoNavFocus;

    if (ImGui::Begin("DeveloperToolbar", nullptr, windowFlags))
    {
        // Left side: Window toggle buttons
        ImGui::PushStyleColor(ImGuiCol_Button, (ImVec4)ImColor::HSV(0.6f, 0.6f, 0.6f, 0.8f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, (ImVec4)ImColor::HSV(0.6f, 0.7f, 0.7f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, (ImVec4)ImColor::HSV(0.6f, 0.8f, 0.8f, 1.0f));

        // Window toggle buttons
        if (context.showNetworkOverlay != nullptr)
        {
            const bool networkActive = *context.showNetworkOverlay;
            if (networkActive)
            {
                ImGui::PushStyleColor(ImGuiCol_Button, (ImVec4)ImColor::HSV(0.3f, 0.6f, 0.6f, 0.8f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, (ImVec4)ImColor::HSV(0.3f, 0.7f, 0.7f, 1.0f));
            }

            if (ImGui::Button("🌐 Network"))
            {
                *context.showNetworkOverlay = !*context.showNetworkOverlay;
            }
            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip("Toggle Network Debug window (F4)");
            }

            if (networkActive)
            {
                ImGui::PopStyleColor(2);
            }
            ImGui::SameLine();
        }

        if (context.showPlayersWindow != nullptr)
        {
            const bool playersActive = *context.showPlayersWindow;
            if (playersActive)
            {
                ImGui::PushStyleColor(ImGuiCol_Button, (ImVec4)ImColor::HSV(0.3f, 0.6f, 0.6f, 0.8f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, (ImVec4)ImColor::HSV(0.3f, 0.7f, 0.7f, 1.0f));
            }

            if (ImGui::Button("👥 Players"))
            {
                *context.showPlayersWindow = !*context.showPlayersWindow;
            }
            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip("Toggle Players window");
            }

            if (playersActive)
            {
                ImGui::PopStyleColor(2);
            }
            ImGui::SameLine();
        }

        if (context.showDebugOverlay != nullptr)
        {
            const bool debugActive = *context.showDebugOverlay;
            if (debugActive)
            {
                ImGui::PushStyleColor(ImGuiCol_Button, (ImVec4)ImColor::HSV(0.3f, 0.6f, 0.6f, 0.8f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, (ImVec4)ImColor::HSV(0.3f, 0.7f, 0.7f, 1.0f));
            }

            if (ImGui::Button("🎮 HUD"))
            {
                *context.showDebugOverlay = !*context.showDebugOverlay;
            }
            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip("Toggle HUD debug overlay");
            }

            if (debugActive)
            {
                ImGui::PopStyleColor(2);
            }
        }

        ImGui::PopStyleColor(3);

        // Separator
        ImGui::SameLine();
        ImGui::Separator();

        // Movement and Stats window toggle buttons
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Button, (ImVec4)ImColor::HSV(0.6f, 0.6f, 0.6f, 0.8f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, (ImVec4)ImColor::HSV(0.6f, 0.7f, 0.7f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, (ImVec4)ImColor::HSV(0.6f, 0.8f, 0.8f, 1.0f));

        if (context.showMovementWindow != nullptr)
        {
            const bool movementActive = *context.showMovementWindow;
            if (movementActive)
            {
                ImGui::PushStyleColor(ImGuiCol_Button, (ImVec4)ImColor::HSV(0.3f, 0.6f, 0.6f, 0.8f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, (ImVec4)ImColor::HSV(0.3f, 0.7f, 0.7f, 1.0f));
            }

            if (ImGui::Button("Movement"))
            {
                *context.showMovementWindow = !*context.showMovementWindow;
            }
            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip("Toggle Movement window");
            }

            if (movementActive)
            {
                ImGui::PopStyleColor(2);
            }
            ImGui::SameLine();
        }

        if (context.showStatsWindow != nullptr)
        {
            const bool statsActive = *context.showStatsWindow;
            if (statsActive)
            {
                ImGui::PushStyleColor(ImGuiCol_Button, (ImVec4)ImColor::HSV(0.3f, 0.6f, 0.6f, 0.8f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, (ImVec4)ImColor::HSV(0.3f, 0.7f, 0.7f, 1.0f));
            }

            if (ImGui::Button("Stats"))
            {
                *context.showStatsWindow = !*context.showStatsWindow;
            }
            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip("Toggle Game Stats window");
            }

            if (statsActive)
            {
                ImGui::PopStyleColor(2);
            }
            ImGui::SameLine();
        }

        if (context.showControlsWindow != nullptr)
        {
            const bool controlsActive = *context.showControlsWindow;
            if (controlsActive)
            {
                ImGui::PushStyleColor(ImGuiCol_Button, (ImVec4)ImColor::HSV(0.3f, 0.6f, 0.6f, 0.8f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, (ImVec4)ImColor::HSV(0.3f, 0.7f, 0.7f, 1.0f));
            }

            if (ImGui::Button("Controls"))
            {
                *context.showControlsWindow = !*context.showControlsWindow;
            }
            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip("Toggle Controls window");
            }

            if (controlsActive)
            {
                ImGui::PopStyleColor(2);
            }
        }

        if (context.profilerToggle)
        {
            if (ImGui::Button("Profiler"))
            {
                context.profilerToggle();
            }
            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip("Toggle Performance Profiler (prof command)");
            }
        }

        ImGui::PopStyleColor(3);

        // Separator before F6/F7 tools
        ImGui::SameLine();
        ImGui::Separator();
        ImGui::SameLine();

        ImGui::PushStyleColor(ImGuiCol_Button, (ImVec4)ImColor::HSV(0.08f, 0.5f, 0.5f, 0.8f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, (ImVec4)ImColor::HSV(0.08f, 0.6f, 0.6f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, (ImVec4)ImColor::HSV(0.08f, 0.7f, 0.7f, 1.0f));

        if (context.showUiTestPanel != nullptr)
        {
            const bool uiTestActive = *context.showUiTestPanel;
            if (uiTestActive)
            {
                ImGui::PushStyleColor(ImGuiCol_Button, (ImVec4)ImColor::HSV(0.12f, 0.6f, 0.6f, 0.8f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, (ImVec4)ImColor::HSV(0.12f, 0.7f, 0.7f, 1.0f));
            }

            if (ImGui::Button("UI Test (F6)"))
            {
                *context.showUiTestPanel = !*context.showUiTestPanel;
            }
            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip("Toggle UI Test Panel (F6)");
            }

            if (uiTestActive)
            {
                ImGui::PopStyleColor(2);
            }
            ImGui::SameLine();
        }

        if (context.showLoadingScreenTestPanel != nullptr)
        {
            const bool loadingTestActive = *context.showLoadingScreenTestPanel;
            if (loadingTestActive)
            {
                ImGui::PushStyleColor(ImGuiCol_Button, (ImVec4)ImColor::HSV(0.12f, 0.6f, 0.6f, 0.8f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, (ImVec4)ImColor::HSV(0.12f, 0.7f, 0.7f, 1.0f));
            }

            if (ImGui::Button("Loading (F7)"))
            {
                *context.showLoadingScreenTestPanel = !*context.showLoadingScreenTestPanel;
            }
            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip("Toggle Loading Screen Test Panel (F7)");
            }

            if (loadingTestActive)
            {
                ImGui::PopStyleColor(2);
            }
        }

        ImGui::PopStyleColor(3);

        // Separator and right side: FPS/Tick/Render
        ImGui::SameLine();
        ImGui::Separator();
        ImGui::SameLine();
        ImGui::SetCursorPosX(viewportSize.x - 400.0f);

        ImGui::Text("FPS: %.1f", context.fps);
        ImGui::SameLine();
        ImGui::Text("|");
        ImGui::SameLine();
        ImGui::Text("Tick: %d", context.tickRate);
        ImGui::SameLine();
        ImGui::Text("|");
        ImGui::SameLine();
        ImGui::Text("%s", context.renderMode.c_str());
    }

    ImGui::End();
#endif
}
} // namespace ui
