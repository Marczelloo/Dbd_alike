#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace engine::render
{

struct RenderCommand
{
    enum class Type : std::uint8_t
    {
        Clear,
        SetViewport,
        SetViewProjection,
        DrawLines,
        DrawSolid,
        DrawTextured,
        SetLighting,
        SetPointLights,
        SetSpotLights,
        SetEnvironment,
        SetCameraPosition,
        Custom
    };

    Type type = Type::Custom;
    std::function<void()> customExecutor;
};

struct LineCommand
{
    std::vector<glm::vec3> positions;
    std::vector<glm::vec3> colors;
    bool overlay = false;
};

struct SolidCommand
{
    std::vector<float> vertices;
    std::size_t vertexStride = 0;
    std::size_t vertexCount = 0;
};

struct TexturedCommand
{
    std::vector<float> vertices;
    std::size_t vertexStride = 0;
    std::size_t firstVertex = 0;
    std::size_t vertexCount = 0;
    unsigned int textureId = 0;
};

struct PointLightData
{
    glm::vec3 position{0.0F};
    float range = 10.0F;
    glm::vec3 color{1.0F};
    float intensity = 1.0F;
};

struct SpotLightData
{
    glm::vec3 position{0.0F};
    float range = 10.0F;
    glm::vec3 direction{0.0F, -1.0F, 0.0F};
    float innerCos = 0.9F;
    glm::vec3 color{1.0F};
    float intensity = 1.0F;
    float outerCos = 0.7F;
};

struct EnvironmentData
{
    glm::vec3 directionalLightDirection{0.3F, -0.8F, 0.5F};
    glm::vec3 directionalLightColor{1.0F};
    float directionalLightIntensity = 1.0F;
    glm::vec3 fogColor{0.5F};
    float fogDensity = 0.0F;
    glm::vec3 skyTopColor{0.4F, 0.6F, 0.9F};
    float fogStart = 50.0F;
    glm::vec3 skyBottomColor{0.7F, 0.8F, 0.95F};
    float fogEnd = 200.0F;
    bool lightingEnabled = true;
    bool fogEnabled = false;
    bool skyEnabled = true;
    bool cloudsEnabled = false;
    float cloudCoverage = 0.5F;
    float cloudDensity = 0.5F;
    float cloudSpeed = 1.0F;
};

struct RenderFrameData
{
    glm::mat4 viewProjection{1.0F};
    glm::vec3 cameraPosition{0.0F};
    glm::vec3 clearColor{0.06F, 0.07F, 0.08F};
    int framebufferWidth = 1920;
    int framebufferHeight = 1080;

    std::vector<LineCommand> lines;
    std::vector<LineCommand> overlayLines;
    std::vector<SolidCommand> solids;
    std::vector<TexturedCommand> textured;

    std::vector<PointLightData> pointLights;
    std::vector<SpotLightData> spotLights;
    EnvironmentData environment;
    bool lightingEnabled = true;

    void Clear()
    {
        lines.clear();
        overlayLines.clear();
        solids.clear();
        textured.clear();
        pointLights.clear();
        spotLights.clear();
    }
};

class RenderThread
{
public:
    static RenderThread& Instance()
    {
        static RenderThread s_instance;
        return s_instance;
    }

    bool Initialize();
    void Shutdown();

    [[nodiscard]] bool IsInitialized() const { return m_initialized; }

    void BeginFrame();
    void SubmitFrameData(const RenderFrameData& data);
    void EndFrame();

    void WaitForSubmit();

    [[nodiscard]] std::size_t PendingFrames() const;

    void SetEnabled(bool enabled) { m_enabled = enabled; }
    [[nodiscard]] bool IsEnabled() const { return m_enabled; }

    struct Stats
    {
        std::size_t framesSubmitted = 0;
        std::size_t framesDropped = 0;
        std::size_t pendingFrames = 0;
        float avgSubmitTimeMs = 0.0F;
    };
    [[nodiscard]] Stats GetStats() const;

private:
    RenderThread() = default;
    ~RenderThread() { Shutdown(); }

    RenderThread(const RenderThread&) = delete;
    RenderThread& operator=(const RenderThread&) = delete;

    std::atomic<bool> m_initialized{false};
    std::atomic<bool> m_enabled{true};
    std::atomic<bool> m_shutdown{false};

    mutable std::mutex m_queueMutex;
    std::queue<RenderFrameData> m_frameQueue;
    std::condition_variable m_submitCondition;
    std::condition_variable m_completeCondition;

    static constexpr std::size_t kMaxPendingFrames = 2;
    std::atomic<std::size_t> m_framesSubmitted{0};
    std::atomic<std::size_t> m_framesDropped{0};
};

}
