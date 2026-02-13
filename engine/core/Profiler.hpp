#pragma once

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace engine::core
{

/// Fixed-size ring buffer for timing history.
template <std::size_t N>
class TimingRing
{
public:
    void Push(float valueMs)
    {
        m_buffer[m_writeIndex] = valueMs;
        m_writeIndex = (m_writeIndex + 1) % N;
        if (m_count < N)
        {
            ++m_count;
        }
    }

    [[nodiscard]] float Average() const
    {
        if (m_count == 0)
        {
            return 0.0F;
        }
        float sum = 0.0F;
        for (std::size_t i = 0; i < m_count; ++i)
        {
            sum += m_buffer[i];
        }
        return sum / static_cast<float>(m_count);
    }

    [[nodiscard]] float Peak() const
    {
        float peak = 0.0F;
        for (std::size_t i = 0; i < m_count; ++i)
        {
            peak = std::max(peak, m_buffer[i]);
        }
        return peak;
    }

    [[nodiscard]] float Latest() const
    {
        if (m_count == 0)
        {
            return 0.0F;
        }
        return m_buffer[(m_writeIndex + N - 1) % N];
    }

    [[nodiscard]] std::size_t Count() const { return m_count; }

    /// Read the ring contents from oldest to newest into |out| (up to N entries).
    void CopyHistory(float* out, std::size_t maxEntries) const
    {
        const std::size_t toCopy = std::min(maxEntries, m_count);
        std::size_t readIdx = (m_count < N) ? 0 : m_writeIndex;
        for (std::size_t i = 0; i < toCopy; ++i)
        {
            out[i] = m_buffer[readIdx];
            readIdx = (readIdx + 1) % N;
        }
    }

private:
    std::array<float, N> m_buffer{};
    std::size_t m_writeIndex = 0;
    std::size_t m_count = 0;
};

/// Per-section timing data.
struct ProfileSection
{
    std::string name;
    TimingRing<256> history;
    float currentMs = 0.0F;
    std::uint32_t callCount = 0; // calls this frame
};

/// GPU query timing pair.
struct GpuTimerQuery
{
    std::uint32_t queryBegin = 0;
    std::uint32_t queryEnd = 0;
    bool pending = false;
};

/// Frame-level statistics snapshot.
struct FrameStats
{
    float totalFrameMs = 0.0F;
    float fps = 0.0F;
    float avgFps = 0.0F;
    float onePercentLowFps = 0.0F;

    // Per-system CPU timings (ms).
    float updateMs = 0.0F;
    float physicsMs = 0.0F;
    float renderSubmitMs = 0.0F;     // CPU-side render submission
    float renderGpuMs = 0.0F;       // GPU time (if available)
    float uiMs = 0.0F;
    float fxMs = 0.0F;
    float audioMs = 0.0F;
    float swapMs = 0.0F;

    // Draw call / vertex stats.
    std::uint32_t drawCalls = 0;
    std::uint32_t verticesSubmitted = 0;
    std::uint32_t trianglesSubmitted = 0;
    std::uint32_t staticBatchChunksVisible = 0;
    std::uint32_t staticBatchChunksTotal = 0;
    std::uint32_t dynamicObjectsCulled = 0;
    std::uint32_t dynamicObjectsDrawn = 0;
    std::uint32_t uiBatches = 0;
    std::uint32_t uiVertices = 0;

    // Memory.
    std::size_t solidVboBytes = 0;
    std::size_t texturedVboBytes = 0;
    std::size_t lineVboBytes = 0;
    std::size_t systemRamBytes = 0;    // Process working set (RAM)

    // Frame time percentiles (computed from recent history).
    float frameTimeP50 = 0.0F;
    float frameTimeP90 = 0.0F;
    float frameTimeP95 = 0.0F;
    float frameTimeP99 = 0.0F;

    // System timings breakdown (ms).
    float appTotalMs = 0.0F;
    float inputMs = 0.0F;
    float networkMs = 0.0F;

    // Threading stats.
    std::size_t jobWorkersTotal = 0;
    std::size_t jobWorkersActive = 0;
    std::size_t jobPending = 0;
    std::size_t jobCompleted = 0;
    float jobWaitTimeMs = 0.0F;
};

/// Lightweight CPU profiler with optional GPU timer queries.
/// Designed to be non-intrusive (<0.01 ms overhead).
class Profiler
{
public:
    static Profiler& Instance()
    {
        static Profiler s_instance;
        return s_instance;
    }

    /// Call at the start of each frame.
    void BeginFrame();

    /// Call at the end of each frame.
    void EndFrame();

    /// Begin a named CPU section. Returns a section index.
    std::size_t BeginSection(std::string_view name);

    /// End the most recently begun section.
    void EndSection(std::size_t sectionIndex);

    /// Record a draw call.
    void RecordDrawCall(std::uint32_t vertices, std::uint32_t triangles = 0);

    /// Record stat directly.
    void SetStat(std::string_view key, float value);
    void SetStatU32(std::string_view key, std::uint32_t value);

    /// Access current frame stats.
    [[nodiscard]] const FrameStats& Stats() const { return m_stats; }
    [[nodiscard]] FrameStats& StatsMut() { return m_stats; }

    /// Access named section data.
    [[nodiscard]] const std::vector<ProfileSection>& Sections() const { return m_sections; }

    /// FPS history ring.
    [[nodiscard]] const TimingRing<256>& FpsHistory() const { return m_fpsHistory; }
    [[nodiscard]] const TimingRing<256>& FrameTimeHistory() const { return m_frameTimeHistory; }

    /// Enabled/disabled toggle.
    void SetEnabled(bool enabled) { m_enabled = enabled; }
    [[nodiscard]] bool IsEnabled() const { return m_enabled; }

    /// Automated benchmark.
    void StartBenchmark(int durationFrames = 600);
    void StopBenchmark();
    [[nodiscard]] bool IsBenchmarkRunning() const { return m_benchmarkRunning; }

    struct BenchmarkResult
    {
        float avgFps = 0.0F;
        float minFps = 0.0F;
        float maxFps = 0.0F;
        float onePercentLow = 0.0F;
        float avgFrameTimeMs = 0.0F;
        float p99FrameTimeMs = 0.0F;
        int totalFrames = 0;
        float durationSeconds = 0.0F;
        std::vector<float> frameTimes; // all captured frame times
    };

    [[nodiscard]] const BenchmarkResult& LastBenchmark() const { return m_benchmarkResult; }

private:
    Profiler() = default;

    bool m_enabled = true;

    // Frame timing.
    std::chrono::high_resolution_clock::time_point m_frameStart{};

    // Section tracking.
    std::vector<ProfileSection> m_sections;
    std::unordered_map<std::string, std::size_t> m_sectionNameToIndex;
    std::vector<std::chrono::high_resolution_clock::time_point> m_sectionStartTimes;

    // Frame stats.
    FrameStats m_stats{};
    TimingRing<256> m_fpsHistory;
    TimingRing<256> m_frameTimeHistory;

    // 1% low FPS tracking.
    TimingRing<256> m_frameTimes1PercentLow;
    std::vector<float> m_recentFrameTimes;

    // RAM update counter (sample every N frames).
    int m_ramUpdateCounter = 0;

    // Benchmark.
    bool m_benchmarkRunning = false;
    int m_benchmarkTargetFrames = 0;
    int m_benchmarkFrameCount = 0;
    std::chrono::high_resolution_clock::time_point m_benchmarkStart{};
    std::vector<float> m_benchmarkFrameTimes;
    BenchmarkResult m_benchmarkResult{};
};

/// RAII helper for profiling a scope.
class ProfileScope
{
public:
    explicit ProfileScope(std::string_view name)
        : m_index(Profiler::Instance().BeginSection(name))
    {
    }
    ~ProfileScope()
    {
        Profiler::Instance().EndSection(m_index);
    }

    ProfileScope(const ProfileScope&) = delete;
    ProfileScope& operator=(const ProfileScope&) = delete;

private:
    std::size_t m_index;
};

#define PROFILE_SCOPE(name) ::engine::core::ProfileScope _profileScope_##__LINE__(name)

} // namespace engine::core
