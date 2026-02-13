#include "engine/ui/ProfilerOverlay.hpp"
#include "engine/core/Profiler.hpp"

// Support both macro names (HAS_IMGUI is legacy, BUILD_WITH_IMGUI is from CMake)
#if defined(HAS_IMGUI) || defined(BUILD_WITH_IMGUI)
#define IMGUI_ENABLED 1
#include <imgui.h>
#endif

#include <algorithm>
#include <cstdio>

namespace engine::ui
{

void ProfilerOverlay::Draw([[maybe_unused]] engine::core::Profiler& profiler)
{
#ifdef IMGUI_ENABLED
    if (!m_visible)
    {
        return;
    }

    if (m_compactMode)
    {
        DrawCompactOverlay(profiler);
        return;
    }

    const auto& stats = profiler.Stats();

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse;
    if (m_pinned)
    {
        // When pinned, position at top-right of viewport.
        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x + viewport->WorkSize.x - 420.0F, viewport->WorkPos.y + 5.0F), ImGuiCond_Once);
        ImGui::SetNextWindowSize(ImVec2(410.0F, 520.0F), ImGuiCond_Once);
    }
    else
    {
        ImGui::SetNextWindowSize(ImVec2(450.0F, 580.0F), ImGuiCond_FirstUseEver);
    }

    bool open = m_visible;
    if (ImGui::Begin("Performance Profiler", &open, flags))
    {
        // Header: FPS + frame time.
        ImGui::TextColored(ImVec4(0.4F, 1.0F, 0.4F, 1.0F), "FPS: %.0f", stats.fps);
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1.0F, 1.0F, 0.5F, 1.0F), " Avg: %.0f", stats.avgFps);
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1.0F, 0.6F, 0.3F, 1.0F), " 1%%Low: %.0f", stats.onePercentLowFps);
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.7F, 0.7F, 0.7F, 1.0F), " %.2f ms", stats.totalFrameMs);

        ImGui::Separator();

        if (ImGui::BeginTabBar("ProfilerTabs"))
        {
            if (ImGui::BeginTabItem("Frame Graph"))
            {
                DrawFrameTimeGraph(profiler);
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Sections"))
            {
                DrawSectionTable(profiler);
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Render"))
            {
                DrawRenderStats(profiler);
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Benchmark"))
            {
                DrawBenchmarkPanel(profiler);
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }

        // Controls.
        ImGui::Separator();
        bool pinned = m_pinned;
        if (ImGui::Checkbox("Pin to game window", &pinned))
        {
            m_pinned = pinned;
        }
        ImGui::SameLine();
        bool compact = m_compactMode;
        if (ImGui::Checkbox("Compact", &compact))
        {
            m_compactMode = compact;
        }
    }
    ImGui::End();

    if (!open)
    {
        m_visible = false;
    }
#endif
}

void ProfilerOverlay::DrawFrameTimeGraph([[maybe_unused]] engine::core::Profiler& profiler)
{
#ifdef IMGUI_ENABLED
    const auto& history = profiler.FrameTimeHistory();

    // Copy history into a flat array for ImGui plot.
    constexpr std::size_t kMaxSamples = 256;
    float samples[kMaxSamples];
    history.CopyHistory(samples, kMaxSamples);
    const int count = static_cast<int>(std::min(kMaxSamples, history.Count()));

    if (count == 0)
    {
        ImGui::Text("No data yet.");
        return;
    }

    // Auto-scale.
    if (m_autoScale && count > 10)
    {
        float maxVal = 0.0F;
        for (int i = 0; i < count; ++i)
        {
            maxVal = std::max(maxVal, samples[i]);
        }
        m_graphMax = std::max(1.0F, maxVal * 1.2F);
    }

    char overlay[64];
    std::snprintf(overlay, sizeof(overlay), "Frame Time (ms) - %.1f fps", profiler.Stats().fps);

    ImGui::PlotLines("##FrameTime", samples, count, 0, overlay, 0.0F, m_graphMax, ImVec2(0, 120));

    // Reference lines info.
    ImGui::TextColored(ImVec4(0.5F, 0.8F, 0.5F, 1.0F), "16.67ms=60fps");
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(0.8F, 0.8F, 0.5F, 1.0F), " 8.33ms=120fps");
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(0.5F, 0.5F, 0.8F, 1.0F), " 6.94ms=144fps");

    ImGui::Checkbox("Auto-scale", &m_autoScale);
    if (!m_autoScale)
    {
        ImGui::SameLine();
        ImGui::SliderFloat("Max ms", &m_graphMax, 1.0F, 100.0F);
    }

    // FPS histogram.
    float fpsSamples[kMaxSamples];
    profiler.FpsHistory().CopyHistory(fpsSamples, kMaxSamples);
    const int fpsCount = static_cast<int>(std::min(kMaxSamples, profiler.FpsHistory().Count()));

    if (fpsCount > 0)
    {
        char fpsOverlay[64];
        std::snprintf(fpsOverlay, sizeof(fpsOverlay), "FPS - Avg: %.0f", profiler.Stats().avgFps);
        float fpsMax = 0.0F;
        for (int i = 0; i < fpsCount; ++i)
        {
            fpsMax = std::max(fpsMax, fpsSamples[i]);
        }
        ImGui::PlotHistogram("##FPS", fpsSamples, fpsCount, 0, fpsOverlay, 0.0F, fpsMax * 1.2F, ImVec2(0, 80));
    }
#endif
}

void ProfilerOverlay::DrawSectionTable([[maybe_unused]] engine::core::Profiler& profiler)
{
#ifdef IMGUI_ENABLED
    const auto& sections = profiler.Sections();

    if (sections.empty())
    {
        ImGui::Text("No profiling sections recorded.");
        return;
    }

    if (ImGui::BeginTable("Sections", 5, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Sortable | ImGuiTableFlags_SizingStretchProp))
    {
        ImGui::TableSetupColumn("Section", ImGuiTableColumnFlags_None, 3.0F);
        ImGui::TableSetupColumn("Time (ms)", ImGuiTableColumnFlags_None, 1.5F);
        ImGui::TableSetupColumn("Avg (ms)", ImGuiTableColumnFlags_None, 1.5F);
        ImGui::TableSetupColumn("Peak (ms)", ImGuiTableColumnFlags_None, 1.5F);
        ImGui::TableSetupColumn("Calls", ImGuiTableColumnFlags_None, 1.0F);
        ImGui::TableHeadersRow();

        for (const auto& section : sections)
        {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::Text("%s", section.name.c_str());
            ImGui::TableNextColumn();

            // Color-code: green < 1ms, yellow < 4ms, red >= 4ms.
            const float ms = section.currentMs;
            ImVec4 color = (ms < 1.0F) ? ImVec4(0.4F, 1.0F, 0.4F, 1.0F) :
                           (ms < 4.0F) ? ImVec4(1.0F, 1.0F, 0.4F, 1.0F) :
                                         ImVec4(1.0F, 0.4F, 0.4F, 1.0F);
            ImGui::TextColored(color, "%.3f", ms);
            ImGui::TableNextColumn();
            ImGui::Text("%.3f", section.history.Average());
            ImGui::TableNextColumn();
            ImGui::Text("%.3f", section.history.Peak());
            ImGui::TableNextColumn();
            ImGui::Text("%u", section.callCount);
        }

        ImGui::EndTable();
    }
#endif
}

void ProfilerOverlay::DrawRenderStats([[maybe_unused]] engine::core::Profiler& profiler)
{
#ifdef IMGUI_ENABLED
    const auto& stats = profiler.Stats();

    ImGui::Text("Draw Calls: %u", stats.drawCalls);
    ImGui::Text("Vertices Submitted: %u", stats.verticesSubmitted);
    ImGui::Text("Triangles Submitted: %u", stats.trianglesSubmitted);
    ImGui::Separator();
    ImGui::Text("Static Batch Chunks: %u / %u visible", stats.staticBatchChunksVisible, stats.staticBatchChunksTotal);
    ImGui::Text("Dynamic Objects: %u drawn, %u culled", stats.dynamicObjectsDrawn, stats.dynamicObjectsCulled);
    ImGui::Separator();
    ImGui::Text("UI Batches: %u", stats.uiBatches);
    ImGui::Text("UI Vertices: %u", stats.uiVertices);
    ImGui::Separator();
    ImGui::Text("VBO Usage:");
    ImGui::Text("  Solid:    %zu KB", stats.solidVboBytes / 1024);
    ImGui::Text("  Textured: %zu KB", stats.texturedVboBytes / 1024);
    ImGui::Text("  Lines:    %zu KB", stats.lineVboBytes / 1024);
#endif
}

void ProfilerOverlay::DrawBenchmarkPanel([[maybe_unused]] engine::core::Profiler& profiler)
{
#ifdef IMGUI_ENABLED
    if (profiler.IsBenchmarkRunning())
    {
        ImGui::TextColored(ImVec4(1.0F, 0.5F, 0.0F, 1.0F), "BENCHMARK RUNNING...");
        ImGui::Text("Frames: %d", static_cast<int>(profiler.LastBenchmark().totalFrames));
        if (ImGui::Button("Stop Benchmark"))
        {
            profiler.StopBenchmark();
        }
    }
    else
    {
        static int benchFrames = 600;
        ImGui::SliderInt("Duration (frames)", &benchFrames, 60, 3600);
        if (ImGui::Button("Start Benchmark"))
        {
            profiler.StartBenchmark(benchFrames);
        }

        const auto& result = profiler.LastBenchmark();
        if (result.totalFrames > 0)
        {
            ImGui::Separator();
            ImGui::TextColored(ImVec4(0.4F, 1.0F, 0.4F, 1.0F), "Last Benchmark Results:");
            ImGui::Text("Frames: %d over %.1f sec", result.totalFrames, result.durationSeconds);
            ImGui::Text("Avg FPS:    %.1f", result.avgFps);
            ImGui::Text("Min FPS:    %.1f", result.minFps);
            ImGui::Text("Max FPS:    %.1f", result.maxFps);
            ImGui::Text("1%% Low:     %.1f", result.onePercentLow);
            ImGui::Text("Avg Frame:  %.3f ms", result.avgFrameTimeMs);
            ImGui::Text("P99 Frame:  %.3f ms", result.p99FrameTimeMs);

            // Mini histogram of benchmark frame times.
            if (!result.frameTimes.empty())
            {
                float maxT = 0.0F;
                for (float t : result.frameTimes)
                {
                    maxT = std::max(maxT, t);
                }
                ImGui::PlotHistogram("##BenchHist", result.frameTimes.data(),
                                     static_cast<int>(result.frameTimes.size()),
                                     0, "Frame Times (ms)", 0.0F, maxT * 1.1F, ImVec2(0, 100));
            }
        }
    }
#endif
}

void ProfilerOverlay::DrawCompactOverlay([[maybe_unused]] engine::core::Profiler& profiler)
{
#ifdef IMGUI_ENABLED
    const auto& stats = profiler.Stats();
    const ImGuiViewport* viewport = ImGui::GetMainViewport();

    // Position at bottom-right corner
    ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x + viewport->WorkSize.x - 2.0F, 
                                   viewport->WorkPos.y + viewport->WorkSize.y - 2.0F),
                            ImGuiCond_Always, ImVec2(1.0F, 1.0F));
    ImGui::SetNextWindowSize(ImVec2(0.0F, 0.0F));

    // Remove NoInputs to allow interaction for close button
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                             ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
                             ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoFocusOnAppearing |
                             ImGuiWindowFlags_NoNav;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6, 3));
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 0.85F);

    if (ImGui::Begin("##CompactProfiler", nullptr, flags))
    {
        // Color-code FPS.
        ImVec4 fpsColor = (stats.fps >= 120.0F) ? ImVec4(0.3F, 1.0F, 0.3F, 1.0F) :
                          (stats.fps >= 60.0F)  ? ImVec4(0.5F, 1.0F, 0.5F, 1.0F) :
                          (stats.fps >= 30.0F)  ? ImVec4(1.0F, 1.0F, 0.3F, 1.0F) :
                                                  ImVec4(1.0F, 0.3F, 0.3F, 1.0F);

        ImGui::TextColored(fpsColor, "%.0f FPS", stats.fps);
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.6F, 0.6F, 0.6F, 1.0F), "%.2fms", stats.totalFrameMs);
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.6F, 0.6F, 0.6F, 1.0F), "DC:%u V:%u", stats.drawCalls, stats.verticesSubmitted);
        
        // Click anywhere to expand, or click X to close
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.5F, 0.2F, 0.2F, 0.6F));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.8F, 0.3F, 0.3F, 0.8F));
        if (ImGui::SmallButton("X"))
        {
            m_visible = false;
        }
        ImGui::PopStyleColor(2);
        
        // Make entire window clickable to expand to full mode
        if (ImGui::IsItemHovered() || ImGui::IsWindowHovered())
        {
            if (ImGui::IsMouseClicked(0) && !ImGui::IsItemHovered())
            {
                // Left click on window (not on X button) -> expand to full mode
                m_compactMode = false;
            }
        }
        
        // Tooltip
        if (ImGui::IsWindowHovered())
        {
            ImGui::SetTooltip("Click to expand | X to close\nOr use: perf_compact off");
        }
    }
    ImGui::End();

    ImGui::PopStyleVar(2);
#endif
}

} // namespace engine::ui
