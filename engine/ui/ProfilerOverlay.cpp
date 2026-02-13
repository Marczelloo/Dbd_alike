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

    // Check for Alt-held pause.
    const bool altHeld = ImGui::GetIO().KeyAlt;
    const bool shouldPause = m_pauseOnAlt && altHeld;
    m_paused = shouldPause;

    // Update cached data if not paused and interval elapsed.
    const float dt = ImGui::GetIO().DeltaTime;
    if (!m_paused)
    {
        m_timeSinceUpdate += dt;
        if (m_timeSinceUpdate >= m_updateInterval)
        {
            m_timeSinceUpdate = 0.0F;
            m_cachedStats = profiler.Stats();
            m_cachedSections = profiler.Sections();
            m_hasCachedData = true;
        }
    }

    // Use cached data if available, otherwise live data.
    const auto& stats = m_hasCachedData ? m_cachedStats : profiler.Stats();
    const auto& sections = m_hasCachedData ? m_cachedSections : profiler.Sections();

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse;
    if (m_pinned)
    {
        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x + viewport->WorkSize.x - 420.0F, viewport->WorkPos.y + 5.0F), ImGuiCond_Once);
        ImGui::SetNextWindowSize(ImVec2(410.0F, 560.0F), ImGuiCond_Once);
    }
    else
    {
        ImGui::SetNextWindowSize(ImVec2(450.0F, 620.0F), ImGuiCond_FirstUseEver);
    }

    bool open = m_visible;
    if (ImGui::Begin("Performance Profiler", &open, flags))
    {
        // Header with pause indicator.
        if (m_paused)
        {
            ImGui::TextColored(ImVec4(1.0F, 0.5F, 0.0F, 1.0F), "[PAUSED - Alt held]");
            ImGui::SameLine();
        }
        
        bool overBudget = stats.totalFrameMs > 16.67F;
        ImVec4 msColor = overBudget ? ImVec4(1.0F, 0.5F, 0.3F, 1.0F) : ImVec4(0.4F, 1.0F, 0.4F, 1.0F);
        
        ImGui::TextColored(ImVec4(0.4F, 1.0F, 0.4F, 1.0F), "FPS: %.0f", stats.fps);
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1.0F, 1.0F, 0.5F, 1.0F), " Avg: %.0f", stats.avgFps);
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1.0F, 0.6F, 0.3F, 1.0F), " 1%%Low: %.0f", stats.onePercentLowFps);
        ImGui::SameLine();
        ImGui::TextColored(msColor, " %.2f ms", stats.totalFrameMs);

        float budgetPct = (stats.totalFrameMs / 16.67F) * 100.0F;
        ImGui::Text("Frame Budget: ");
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_PlotHistogram, overBudget ? ImVec4(1.0F, 0.3F, 0.3F, 1.0F) : ImVec4(0.3F, 0.8F, 0.3F, 1.0F));
        ImGui::ProgressBar(std::min(budgetPct / 100.0F, 1.0F), ImVec2(150.0F, 0.0F));
        ImGui::PopStyleColor();
        ImGui::SameLine();
        ImGui::TextColored(msColor, "%.1f%%", budgetPct);

        // Update rate controls.
        ImGui::Separator();
        ImGui::PushItemWidth(100.0F);
        ImGui::SliderFloat("Update interval (s)", &m_updateInterval, 0.0F, 2.0F, "%.2f");
        ImGui::SameLine();
        ImGui::Checkbox("Pause on Alt", &m_pauseOnAlt);
        ImGui::SameLine();
        if (ImGui::Button(m_paused ? "Resume" : "Pause"))
        {
            m_paused = !m_paused;
            if (!m_paused)
            {
                m_timeSinceUpdate = m_updateInterval; // Force immediate update
            }
        }
        ImGui::PopItemWidth();

        ImGui::Separator();

        if (ImGui::BeginTabBar("ProfilerTabs"))
        {
            if (ImGui::BeginTabItem("Overview"))
            {
                DrawFrameTimeGraph(profiler);
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Systems"))
            {
                DrawSystemTimings(profiler);
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
            if (ImGui::BeginTabItem("Distribution"))
            {
                DrawFrameTimeHistogram(profiler);
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
    const auto& sections = m_hasCachedData ? m_cachedSections : profiler.Sections();

    if (sections.empty())
    {
        ImGui::Text("No profiling sections recorded.");
        return;
    }

    if (ImGui::BeginTable("Sections", 5, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Sortable | ImGuiTableFlags_SizingStretchProp))
    {
        ImGui::TableSetupColumn("Section", ImGuiTableColumnFlags_WidthStretch, 3.0F);
        ImGui::TableSetupColumn("Time (ms)", ImGuiTableColumnFlags_WidthStretch, 1.5F);
        ImGui::TableSetupColumn("Avg (ms)", ImGuiTableColumnFlags_WidthStretch, 1.5F);
        ImGui::TableSetupColumn("Peak (ms)", ImGuiTableColumnFlags_WidthStretch, 1.5F);
        ImGui::TableSetupColumn("Calls", ImGuiTableColumnFlags_WidthStretch, 1.0F);
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
    const auto& stats = m_hasCachedData ? m_cachedStats : profiler.Stats();

    ImGui::TextColored(ImVec4(0.8F, 0.8F, 1.0F, 1.0F), "Draw Statistics");
    ImGui::Separator();
    
    if (ImGui::BeginTable("DrawStats", 2, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp))
    {
        ImGui::TableSetupColumn("Metric", ImGuiTableColumnFlags_WidthStretch, 2.0F);
        ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch, 1.0F);
        ImGui::TableHeadersRow();

        ImGui::TableNextRow();
        ImGui::TableNextColumn(); ImGui::Text("Draw Calls");
        ImGui::TableNextColumn(); ImGui::Text("%u", stats.drawCalls);

        ImGui::TableNextRow();
        ImGui::TableNextColumn(); ImGui::Text("Vertices");
        ImGui::TableNextColumn(); ImGui::Text("%u", stats.verticesSubmitted);

        ImGui::TableNextRow();
        ImGui::TableNextColumn(); ImGui::Text("Triangles");
        ImGui::TableNextColumn(); ImGui::Text("%u", stats.trianglesSubmitted);

        ImGui::TableNextRow();
        ImGui::TableNextColumn(); ImGui::Text("UI Batches");
        ImGui::TableNextColumn(); ImGui::Text("%u", stats.uiBatches);

        ImGui::TableNextRow();
        ImGui::TableNextColumn(); ImGui::Text("UI Vertices");
        ImGui::TableNextColumn(); ImGui::Text("%u", stats.uiVertices);

        ImGui::EndTable();
    }

    ImGui::Separator();
    ImGui::TextColored(ImVec4(0.8F, 0.8F, 1.0F, 1.0F), "Culling & Batching");
    ImGui::Separator();
    
    ImGui::Text("Static Batch Chunks: %u / %u visible", stats.staticBatchChunksVisible, stats.staticBatchChunksTotal);
    if (stats.staticBatchChunksTotal > 0)
    {
        float visPct = (static_cast<float>(stats.staticBatchChunksVisible) / static_cast<float>(stats.staticBatchChunksTotal)) * 100.0F;
        ImGui::ProgressBar(visPct / 100.0F, ImVec2(100.0F, 0.0F));
        ImGui::SameLine();
        ImGui::Text("%.1f%%", visPct);
    }
    
    const std::uint32_t totalDyn = stats.dynamicObjectsDrawn + stats.dynamicObjectsCulled;
    ImGui::Text("Dynamic Objects: %u drawn, %u culled", stats.dynamicObjectsDrawn, stats.dynamicObjectsCulled);
    if (totalDyn > 0)
    {
        float drawPct = (static_cast<float>(stats.dynamicObjectsDrawn) / static_cast<float>(totalDyn)) * 100.0F;
        ImGui::ProgressBar(drawPct / 100.0F, ImVec2(100.0F, 0.0F));
        ImGui::SameLine();
        ImGui::Text("%.1f%% visible", drawPct);
    }

    ImGui::Separator();
    ImGui::TextColored(ImVec4(0.8F, 0.8F, 1.0F, 1.0F), "GPU Memory (VBO)");
    ImGui::Separator();
    
    const std::size_t totalMem = stats.solidVboBytes + stats.texturedVboBytes + stats.lineVboBytes;
    ImGui::Text("Total: %.2f MB", static_cast<float>(totalMem) / (1024.0F * 1024.0F));
    ImGui::Indent();
    const float totalMemF = static_cast<float>(totalMem > 0 ? totalMem : 1);
    ImGui::Text("Solid:    %zu KB (%.1f%%)", stats.solidVboBytes / 1024,
                100.0F * static_cast<float>(stats.solidVboBytes) / totalMemF);
    ImGui::Text("Textured: %zu KB (%.1f%%)", stats.texturedVboBytes / 1024,
                100.0F * static_cast<float>(stats.texturedVboBytes) / totalMemF);
    ImGui::Text("Lines:    %zu KB (%.1f%%)", stats.lineVboBytes / 1024,
                100.0F * static_cast<float>(stats.lineVboBytes) / totalMemF);
    ImGui::Unindent();
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
    // Check for Alt-held pause.
    const bool altHeld = ImGui::GetIO().KeyAlt;
    const bool shouldPause = m_pauseOnAlt && altHeld;
    m_paused = shouldPause;

    // Update cached data if not paused and interval elapsed.
    const float dt = ImGui::GetIO().DeltaTime;
    if (!m_paused)
    {
        m_timeSinceUpdate += dt;
        if (m_timeSinceUpdate >= m_updateInterval)
        {
            m_timeSinceUpdate = 0.0F;
            m_cachedStats = profiler.Stats();
            m_hasCachedData = true;
        }
    }

    const auto& stats = m_hasCachedData ? m_cachedStats : profiler.Stats();
    const ImGuiViewport* viewport = ImGui::GetMainViewport();

    ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x + viewport->WorkSize.x - 2.0F, 
                                   viewport->WorkPos.y + viewport->WorkSize.y - 2.0F),
                            ImGuiCond_Always, ImVec2(1.0F, 1.0F));
    ImGui::SetNextWindowSize(ImVec2(0.0F, 0.0F));

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                             ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
                             ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoFocusOnAppearing |
                             ImGuiWindowFlags_NoNav;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6, 3));
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 0.85F);

    if (ImGui::Begin("##CompactProfiler", nullptr, flags))
    {
        if (m_paused)
        {
            ImGui::TextColored(ImVec4(1.0F, 0.5F, 0.0F, 1.0F), "||");
            ImGui::SameLine();
        }

        ImVec4 fpsColor = (stats.fps >= 120.0F) ? ImVec4(0.3F, 1.0F, 0.3F, 1.0F) :
                          (stats.fps >= 60.0F)  ? ImVec4(0.5F, 1.0F, 0.5F, 1.0F) :
                          (stats.fps >= 30.0F)  ? ImVec4(1.0F, 1.0F, 0.3F, 1.0F) :
                                                  ImVec4(1.0F, 0.3F, 0.3F, 1.0F);

        ImGui::TextColored(fpsColor, "%.0f FPS", stats.fps);
        ImGui::SameLine();
        
        bool overBudget = stats.totalFrameMs > 16.67F;
        ImVec4 msColor = overBudget ? ImVec4(1.0F, 0.4F, 0.4F, 1.0F) : ImVec4(0.6F, 0.8F, 0.6F, 1.0F);
        ImGui::TextColored(msColor, "%.2fms", stats.totalFrameMs);
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.6F, 0.6F, 0.6F, 1.0F), "DC:%u", stats.drawCalls);
        ImGui::SameLine();
        
        std::size_t totalGpuMem = stats.solidVboBytes + stats.texturedVboBytes + stats.lineVboBytes;
        ImGui::TextColored(ImVec4(0.7F, 0.7F, 1.0F, 1.0F), "GPU:%.0fM", static_cast<float>(totalGpuMem) / (1024.0F * 1024.0F));
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.9F, 0.7F, 0.5F, 1.0F), "RAM:%.0fM", static_cast<float>(stats.systemRamBytes) / (1024.0F * 1024.0F));
        
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.5F, 0.2F, 0.2F, 0.6F));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.8F, 0.3F, 0.3F, 0.8F));
        if (ImGui::SmallButton("X"))
        {
            m_visible = false;
        }
        ImGui::PopStyleColor(2);
        
        if (ImGui::IsItemHovered() || ImGui::IsWindowHovered())
        {
            if (ImGui::IsMouseClicked(0) && !ImGui::IsItemHovered())
            {
                m_compactMode = false;
            }
        }
        
        if (ImGui::IsWindowHovered())
        {
            ImGui::SetTooltip("Click to expand | X to close\nOr use: perf_compact off");
        }
    }
    ImGui::End();

    ImGui::PopStyleVar(2);
#endif
}

void ProfilerOverlay::DrawSystemTimings([[maybe_unused]] engine::core::Profiler& profiler)
{
#ifdef IMGUI_ENABLED
    const auto& stats = m_hasCachedData ? m_cachedStats : profiler.Stats();
    const auto& sections = m_hasCachedData ? m_cachedSections : profiler.Sections();

    float frameMs = (stats.totalFrameMs > 0.001F) ? stats.totalFrameMs : 0.001F;
    ImGui::Text("Frame Budget: %.2f / 16.67 ms", frameMs);
    float budgetPct = (frameMs / 16.67F) * 100.0F;
    ImVec4 budgetColor = (budgetPct <= 80.0F) ? ImVec4(0.3F, 1.0F, 0.3F, 1.0F) :
                         (budgetPct <= 100.0F) ? ImVec4(1.0F, 1.0F, 0.3F, 1.0F) :
                                                  ImVec4(1.0F, 0.3F, 0.3F, 1.0F);
    
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_PlotHistogram, budgetColor);
    ImGui::ProgressBar(std::min(budgetPct / 100.0F, 1.0F), ImVec2(120.0F, 0.0F), "");
    ImGui::PopStyleColor();
    
    ImGui::Separator();
    
    if (ImGui::BeginTable("SystemTimings", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp))
    {
        ImGui::TableSetupColumn("System", ImGuiTableColumnFlags_WidthStretch, 2.0F);
        ImGui::TableSetupColumn("Time (ms)", ImGuiTableColumnFlags_WidthStretch, 1.0F);
        ImGui::TableSetupColumn("% of Frame", ImGuiTableColumnFlags_WidthStretch, 1.0F);
        ImGui::TableHeadersRow();

        auto row = [frameMs](const char* name, float ms) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::Text("%s", name);
            ImGui::TableNextColumn();
            
            ImVec4 color = (ms < 1.0F) ? ImVec4(0.4F, 1.0F, 0.4F, 1.0F) :
                           (ms < 4.0F) ? ImVec4(1.0F, 1.0F, 0.4F, 1.0F) :
                                         ImVec4(1.0F, 0.4F, 0.4F, 1.0F);
            ImGui::TextColored(color, "%.3f", ms);
            ImGui::TableNextColumn();
            
            float pct = (ms / frameMs) * 100.0F;
            ImGui::Text("%.1f%%", pct);
        };

        row("Update", stats.updateMs);
        row("Physics", stats.physicsMs);
        row("Render Submit", stats.renderSubmitMs);
        row("Render GPU", stats.renderGpuMs);
        row("UI", stats.uiMs);
        row("FX", stats.fxMs);
        row("Audio", stats.audioMs);
        row("Swap", stats.swapMs);
        row("Input", stats.inputMs);
        row("Network", stats.networkMs);

        ImGui::EndTable();
    }

    ImGui::Separator();
    ImGui::TextColored(ImVec4(0.6F, 0.8F, 1.0F, 1.0F), "Profiled Sections (%zu):", sections.size());
    
    float totalProfiled = 0.0F;
    for (const auto& sec : sections)
    {
        totalProfiled += sec.currentMs;
    }
    ImGui::Text("  Total in sections: %.3f ms", totalProfiled);
    ImGui::Text("  Untracked: %.3f ms", std::max(0.0F, frameMs - totalProfiled));

    ImGui::Separator();
    ImGui::TextColored(ImVec4(0.6F, 0.8F, 1.0F, 1.0F), "Memory:");
    const std::size_t gpuMem = stats.solidVboBytes + stats.texturedVboBytes + stats.lineVboBytes;
    ImGui::Text("  GPU VBO: %.2f MB", static_cast<float>(gpuMem) / (1024.0F * 1024.0F));
    ImGui::Text("  System RAM: %.1f MB", static_cast<float>(stats.systemRamBytes) / (1024.0F * 1024.0F));
#endif
}

void ProfilerOverlay::DrawFrameTimeHistogram([[maybe_unused]] engine::core::Profiler& profiler)
{
#ifdef IMGUI_ENABLED
    const auto& stats = m_hasCachedData ? m_cachedStats : profiler.Stats();
    const auto& history = profiler.FrameTimeHistory();

    constexpr std::size_t kMaxSamples = 256;
    float samples[kMaxSamples];
    history.CopyHistory(samples, kMaxSamples);
    const int count = static_cast<int>(std::min(kMaxSamples, history.Count()));

    if (count == 0)
    {
        ImGui::Text("No data yet.");
        return;
    }

    ImGui::TextColored(ImVec4(0.8F, 0.8F, 0.8F, 1.0F), "Frame Time Percentiles (last %d frames):", count);
    ImGui::Separator();

    auto pctRow = [](const char* label, float ms, float targetFps) {
        ImGui::Text("%s:", label);
        ImGui::SameLine(80);
        ImVec4 color = (ms <= 1000.0F / targetFps) ? ImVec4(0.4F, 1.0F, 0.4F, 1.0F) :
                                                     ImVec4(1.0F, 0.5F, 0.3F, 1.0F);
        ImGui::TextColored(color, "%.2f ms", ms);
        ImGui::SameLine(160);
        float fps = (ms > 0.001F) ? (1000.0F / ms) : 0.0F;
        ImGui::Text("(%.0f fps)", fps);
    };

    pctRow("Median (P50)", stats.frameTimeP50, 60.0F);
    pctRow("P90", stats.frameTimeP90, 60.0F);
    pctRow("P95", stats.frameTimeP95, 60.0F);
    pctRow("P99", stats.frameTimeP99, 60.0F);
    pctRow("1% Low", 1000.0F / std::max(stats.onePercentLowFps, 1.0F), 60.0F);

    ImGui::Separator();
    ImGui::TextColored(ImVec4(0.8F, 0.8F, 0.8F, 1.0F), "Distribution:");

    float maxMs = 0.0F;
    for (int i = 0; i < count; ++i)
    {
        maxMs = std::max(maxMs, samples[i]);
    }
    maxMs = std::min(maxMs, 50.0F);

    ImGui::PlotHistogram("##FrameTimeHist", samples, count, 0, 
                         "Frame time distribution", 0.0F, maxMs * 1.1F, ImVec2(0, 100));

    ImGui::TextColored(ImVec4(0.5F, 0.5F, 0.5F, 1.0F), "Max displayed: %.1f ms", maxMs);

    ImGui::Separator();
    ImGui::TextColored(ImVec4(0.8F, 0.8F, 0.8F, 1.0F), "Reference Targets:");
    ImGui::BulletText("16.67 ms = 60 fps");
    ImGui::BulletText("11.11 ms = 90 fps");
    ImGui::BulletText(" 8.33 ms = 120 fps");
    ImGui::BulletText(" 6.94 ms = 144 fps");
    ImGui::BulletText(" 4.17 ms = 240 fps");
#endif
}

} // namespace engine::ui
