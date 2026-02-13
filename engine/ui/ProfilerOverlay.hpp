#pragma once

#include "engine/core/Profiler.hpp"

namespace engine::core { 
    class Profiler; 
}

namespace engine::ui
{

/// Renders a performance profiler overlay using ImGui.
/// Can be shown as a dockable panel inside the game window or as a separate floating window.
class ProfilerOverlay
{
public:
    /// Draw the profiler window. Call between ImGui::NewFrame() and ImGui::Render().
    void Draw(engine::core::Profiler& profiler);

    /// Toggle visibility.
    void Toggle() { m_visible = !m_visible; }
    void SetVisible(bool visible) { m_visible = visible; }
    [[nodiscard]] bool IsVisible() const { return m_visible; }

    /// Toggle between pinned (in-game) and floating (separate) mode.
    void SetPinned(bool pinned) { m_pinned = pinned; }
    [[nodiscard]] bool IsPinned() const { return m_pinned; }

    /// Compact overlay (just FPS/frame time bar at top).
    void SetCompactMode(bool compact) { m_compactMode = compact; }
    [[nodiscard]] bool IsCompactMode() const { return m_compactMode; }

private:
    void DrawFrameTimeGraph(engine::core::Profiler& profiler);
    void DrawSectionTable(engine::core::Profiler& profiler);
    void DrawRenderStats(engine::core::Profiler& profiler);
    void DrawBenchmarkPanel(engine::core::Profiler& profiler);
    void DrawCompactOverlay(engine::core::Profiler& profiler);
    void DrawSystemTimings(engine::core::Profiler& profiler);
    void DrawFrameTimeHistogram(engine::core::Profiler& profiler);

    bool m_visible = false;
    bool m_pinned = false;
    bool m_compactMode = false;

    // Graph state.
    float m_graphMax = 16.67F; // initial scale = 60 FPS
    bool m_autoScale = true;

    // Update rate control.
    float m_updateInterval = 0.0F;    // seconds (0 = every frame)
    float m_timeSinceUpdate = 0.0F;
    bool m_paused = false;
    bool m_pauseOnAlt = true;         // auto-pause when Alt held

    // Cached stats for display when paused/slow update.
    engine::core::FrameStats m_cachedStats{};
    std::vector<engine::core::ProfileSection> m_cachedSections;
    bool m_hasCachedData = false;
};

} // namespace engine::ui
