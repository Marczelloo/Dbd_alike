#include "engine/core/Profiler.hpp"

#include <algorithm>
#include <numeric>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <psapi.h>
#elif defined(__linux__)
#include <fstream>
#include <sstream>
#endif

namespace engine::core
{

namespace
{
std::size_t GetProcessRamBytes()
{
#if defined(_WIN32)
    PROCESS_MEMORY_COUNTERS_EX pmc;
    if (GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmc), sizeof(pmc)))
    {
        return pmc.WorkingSetSize;
    }
#elif defined(__linux__)
    std::ifstream file("/proc/self/status");
    std::string line;
    while (std::getline(file, line))
    {
        if (line.rfind("VmRSS:", 0) == 0)
        {
            std::istringstream iss(line.substr(6));
            std::size_t kb;
            iss >> kb;
            return kb * 1024;
        }
    }
#endif
    return 0;
}
}

void Profiler::BeginFrame()
{
    m_frameStart = std::chrono::high_resolution_clock::now();

    // Reset per-frame counters.
    m_stats.drawCalls = 0;
    m_stats.verticesSubmitted = 0;
    m_stats.trianglesSubmitted = 0;
    m_stats.dynamicObjectsCulled = 0;
    m_stats.dynamicObjectsDrawn = 0;

    for (auto& section : m_sections)
    {
        section.callCount = 0;
        section.currentMs = 0.0F;
    }
}

void Profiler::EndFrame()
{
    const auto now = std::chrono::high_resolution_clock::now();
    const float frameMs = std::chrono::duration<float, std::milli>(now - m_frameStart).count();

    m_stats.totalFrameMs = frameMs;
    m_stats.fps = (frameMs > 0.001F) ? (1000.0F / frameMs) : 0.0F;

    m_frameTimeHistory.Push(frameMs);
    m_fpsHistory.Push(m_stats.fps);

    m_stats.avgFps = m_fpsHistory.Average();

    // Compute frame time percentiles from recent history.
    m_recentFrameTimes.clear();
    constexpr std::size_t kRecentCount = 128;
    float recentBuf[kRecentCount];
    m_frameTimeHistory.CopyHistory(recentBuf, kRecentCount);
    const std::size_t count = std::min(kRecentCount, m_frameTimeHistory.Count());
    if (count >= 10)
    {
        m_recentFrameTimes.assign(recentBuf, recentBuf + count);
        std::sort(m_recentFrameTimes.begin(), m_recentFrameTimes.end());

        const auto percentile = [count, this](float p) -> float {
            const std::size_t idx = static_cast<std::size_t>(static_cast<float>(count - 1) * p);
            return m_recentFrameTimes[std::min(idx, count - 1)];
        };

        m_stats.frameTimeP50 = percentile(0.50F);
        m_stats.frameTimeP90 = percentile(0.90F);
        m_stats.frameTimeP95 = percentile(0.95F);
        m_stats.frameTimeP99 = percentile(0.99F);

        // Compute 1% low FPS (worst 1% of frames).
        std::sort(m_recentFrameTimes.begin(), m_recentFrameTimes.end(), std::greater<float>());
        const std::size_t onePercent = std::max<std::size_t>(1, count / 100);
        float worstSum = 0.0F;
        for (std::size_t i = 0; i < onePercent; ++i)
        {
            worstSum += m_recentFrameTimes[i];
        }
        const float worstAvgMs = worstSum / static_cast<float>(onePercent);
        m_stats.onePercentLowFps = (worstAvgMs > 0.001F) ? (1000.0F / worstAvgMs) : 0.0F;
    }

    // Push section history + map to system stats.
    for (auto& section : m_sections)
    {
        if (section.callCount > 0)
        {
            section.history.Push(section.currentMs);
        }

        // Map named sections to system timings.
        if (section.name == "Update")
        {
            m_stats.updateMs = section.currentMs;
        }
        else if (section.name == "Physics")
        {
            m_stats.physicsMs = section.currentMs;
        }
        else if (section.name == "Render")
        {
            m_stats.renderSubmitMs = section.currentMs;
        }
        else if (section.name == "RenderGPU" || section.name == "Render GPU")
        {
            m_stats.renderGpuMs = section.currentMs;
        }
        else if (section.name == "UI")
        {
            m_stats.uiMs = section.currentMs;
        }
        else if (section.name == "FX" || section.name == "VFX")
        {
            m_stats.fxMs = section.currentMs;
        }
        else if (section.name == "Audio")
        {
            m_stats.audioMs = section.currentMs;
        }
        else if (section.name == "Swap")
        {
            m_stats.swapMs = section.currentMs;
        }
        else if (section.name == "Input")
        {
            m_stats.inputMs = section.currentMs;
        }
        else if (section.name == "Network")
        {
            m_stats.networkMs = section.currentMs;
        }
    }

    // Update RAM usage (sampling every few frames to reduce overhead).
    ++m_ramUpdateCounter;
    if (m_ramUpdateCounter >= 30)
    {
        m_ramUpdateCounter = 0;
        m_stats.systemRamBytes = GetProcessRamBytes();
    }

    // Benchmark tracking.
    if (m_benchmarkRunning)
    {
        m_benchmarkFrameTimes.push_back(frameMs);
        ++m_benchmarkFrameCount;
        if (m_benchmarkFrameCount >= m_benchmarkTargetFrames)
        {
            StopBenchmark();
        }
    }
}

std::size_t Profiler::BeginSection(std::string_view name)
{
    if (!m_enabled)
    {
        return 0;
    }

    const std::string nameStr(name);
    auto it = m_sectionNameToIndex.find(nameStr);
    std::size_t index;
    if (it != m_sectionNameToIndex.end())
    {
        index = it->second;
    }
    else
    {
        index = m_sections.size();
        m_sections.push_back(ProfileSection{nameStr, {}, 0.0F, 0});
        m_sectionNameToIndex[nameStr] = index;
    }

    // Ensure start time storage.
    if (index >= m_sectionStartTimes.size())
    {
        m_sectionStartTimes.resize(index + 1);
    }
    m_sectionStartTimes[index] = std::chrono::high_resolution_clock::now();

    return index;
}

void Profiler::EndSection(std::size_t sectionIndex)
{
    if (!m_enabled || sectionIndex >= m_sectionStartTimes.size())
    {
        return;
    }

    const auto now = std::chrono::high_resolution_clock::now();
    const float elapsed = std::chrono::duration<float, std::milli>(now - m_sectionStartTimes[sectionIndex]).count();

    if (sectionIndex < m_sections.size())
    {
        m_sections[sectionIndex].currentMs += elapsed;
        m_sections[sectionIndex].callCount++;
    }
}

void Profiler::RecordDrawCall(std::uint32_t vertices, std::uint32_t triangles)
{
    m_stats.drawCalls++;
    m_stats.verticesSubmitted += vertices;
    m_stats.trianglesSubmitted += triangles;
}

void Profiler::SetStat(std::string_view /*key*/, float /*value*/)
{
    // For future ad-hoc stats.
}

void Profiler::SetStatU32(std::string_view /*key*/, std::uint32_t /*value*/)
{
    // For future ad-hoc stats.
}

void Profiler::StartBenchmark(int durationFrames)
{
    m_benchmarkRunning = true;
    m_benchmarkTargetFrames = durationFrames;
    m_benchmarkFrameCount = 0;
    m_benchmarkFrameTimes.clear();
    m_benchmarkFrameTimes.reserve(static_cast<std::size_t>(durationFrames));
    m_benchmarkStart = std::chrono::high_resolution_clock::now();
    m_benchmarkResult = {};
}

void Profiler::StopBenchmark()
{
    if (!m_benchmarkRunning)
    {
        return;
    }

    m_benchmarkRunning = false;
    const auto now = std::chrono::high_resolution_clock::now();
    const float totalSec = std::chrono::duration<float>(now - m_benchmarkStart).count();

    auto& r = m_benchmarkResult;
    r.totalFrames = static_cast<int>(m_benchmarkFrameTimes.size());
    r.durationSeconds = totalSec;
    r.frameTimes = m_benchmarkFrameTimes;

    if (r.totalFrames > 0)
    {
        float sum = 0.0F;
        float minT = 1e9F;
        float maxT = 0.0F;
        for (float t : m_benchmarkFrameTimes)
        {
            sum += t;
            minT = std::min(minT, t);
            maxT = std::max(maxT, t);
        }
        r.avgFrameTimeMs = sum / static_cast<float>(r.totalFrames);
        r.avgFps = (r.avgFrameTimeMs > 0.001F) ? (1000.0F / r.avgFrameTimeMs) : 0.0F;
        r.minFps = (maxT > 0.001F) ? (1000.0F / maxT) : 0.0F;
        r.maxFps = (minT > 0.001F) ? (1000.0F / minT) : 0.0F;

        // P99 frame time.
        std::vector<float> sorted = m_benchmarkFrameTimes;
        std::sort(sorted.begin(), sorted.end());
        const std::size_t p99Index = static_cast<std::size_t>(static_cast<float>(sorted.size()) * 0.99F);
        r.p99FrameTimeMs = sorted[std::min(p99Index, sorted.size() - 1)];

        // 1% low.
        std::sort(sorted.begin(), sorted.end(), std::greater<float>());
        const std::size_t onePercent = std::max<std::size_t>(1, sorted.size() / 100);
        float worstSum = 0.0F;
        for (std::size_t i = 0; i < onePercent; ++i)
        {
            worstSum += sorted[i];
        }
        r.onePercentLow = (worstSum > 0.001F) ? (1000.0F / (worstSum / static_cast<float>(onePercent))) : 0.0F;
    }
}

} // namespace engine::core
