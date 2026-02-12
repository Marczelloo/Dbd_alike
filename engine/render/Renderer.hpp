#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include "engine/render/Frustum.hpp"

namespace engine::render
{
enum class RenderMode
{
    Wireframe,
    Filled
};

struct EnvironmentSettings
{
    bool skyEnabled = true;
    glm::vec3 skyTopColor{0.44F, 0.58F, 0.78F};
    glm::vec3 skyBottomColor{0.11F, 0.14F, 0.18F};

    bool cloudsEnabled = true;
    float cloudCoverage = 0.25F;
    float cloudDensity = 0.45F;
    float cloudSpeed = 0.25F;

    glm::vec3 directionalLightDirection{0.45F, 1.0F, 0.3F};
    glm::vec3 directionalLightColor{1.0F, 1.0F, 1.0F};
    float directionalLightIntensity = 1.0F;

    bool fogEnabled = false;
    glm::vec3 fogColor{0.55F, 0.62F, 0.70F};
    float fogDensity = 0.012F;
    float fogStart = 20.0F;
    float fogEnd = 120.0F;
};

struct PointLight
{
    glm::vec3 position{0.0F, 2.0F, 0.0F};
    glm::vec3 color{1.0F, 1.0F, 1.0F};
    float intensity = 1.0F;
    float range = 10.0F;
};

struct SpotLight
{
    glm::vec3 position{0.0F, 3.0F, 0.0F};
    glm::vec3 direction{0.0F, -1.0F, 0.0F};
    glm::vec3 color{1.0F, 1.0F, 1.0F};
    float intensity = 1.0F;
    float range = 12.0F;
    float innerCos = 0.93F;
    float outerCos = 0.83F;
};

struct MeshGeometry
{
    std::vector<glm::vec3> positions;
    std::vector<glm::vec3> normals;
    std::vector<glm::vec3> colors;
    std::vector<glm::vec2> uvs;
    std::vector<std::uint32_t> indices;
};

struct MaterialParams
{
    float roughness = 0.55F;
    float metallic = 0.0F;
    float emissive = 0.0F;
    bool unlit = false;
};

class Renderer
{
public:
    bool Initialize(int framebufferWidth, int framebufferHeight);
    void Shutdown();

    void SetViewport(int framebufferWidth, int framebufferHeight) const;

    void SetRenderMode(RenderMode mode);
    void ToggleRenderMode();
    [[nodiscard]] RenderMode GetRenderMode() const { return m_renderMode; }
    void SetEnvironmentSettings(const EnvironmentSettings& settings);
    [[nodiscard]] const EnvironmentSettings& GetEnvironmentSettings() const { return m_environment; }
    void SetPointLights(const std::vector<PointLight>& lights);
    void SetSpotLights(const std::vector<SpotLight>& lights);
    void SetPostFxPulse(const glm::vec3& color, float intensity);
    void SetLightingEnabled(bool enabled);
    [[nodiscard]] bool LightingEnabled() const { return m_lightingEnabled; }
    [[nodiscard]] const Frustum& GetFrustum() const { return m_frustum; }
    [[nodiscard]] unsigned int GetSolidShaderProgram() const { return m_solidProgram; }
    [[nodiscard]] int GetSolidViewProjLocation() const { return m_solidViewProjLocation; }
    void SetCameraWorldPosition(const glm::vec3& position);

    void BeginFrame(const glm::vec3& clearColor);
    void EndFrame(const glm::mat4& viewProjection);

    void DrawLine(const glm::vec3& from, const glm::vec3& to, const glm::vec3& color);
    void DrawOverlayLine(const glm::vec3& from, const glm::vec3& to, const glm::vec3& color);
    void DrawBox(
        const glm::vec3& center,
        const glm::vec3& halfExtents,
        const glm::vec3& color,
        const MaterialParams& material = {}
    );
    void DrawOrientedBox(
        const glm::vec3& center,
        const glm::vec3& halfExtents,
        const glm::vec3& rotationEulerDegrees,
        const glm::vec3& color,
        const MaterialParams& material = {}
    );
    void DrawCapsule(
        const glm::vec3& center,
        float height,
        float radius,
        const glm::vec3& color,
        const MaterialParams& material = {}
    );
    void DrawMesh(
        const MeshGeometry& mesh,
        const glm::vec3& position,
        const glm::vec3& rotationEulerDegrees,
        const glm::vec3& scale,
        const glm::vec3& color,
        const MaterialParams& material = {}
    );
    void DrawTexturedMesh(
        const MeshGeometry& mesh,
        const glm::vec3& position,
        const glm::vec3& rotationEulerDegrees,
        const glm::vec3& scale,
        const glm::vec3& color,
        const MaterialParams& material,
        unsigned int textureId
    );
    void DrawGrid(int halfSize, float step, const glm::vec3& majorColor, const glm::vec3& minorColor);
    void DrawCircle(
        const glm::vec3& center,
        float radius,
        int segments,
        const glm::vec3& color,
        bool overlay = false
    );

    struct BillboardData
    {
        glm::vec3 position{0.0F};
        float size = 0.1F;
        glm::vec4 color{1.0F};
    };

    void DrawBillboards(
        const BillboardData* billboards,
        std::size_t count,
        const glm::vec3& cameraPosition
    );

private:
    struct LineVertex
    {
        glm::vec3 position{0.0F};
        glm::vec3 color{1.0F};
    };

    struct SolidVertex
    {
        glm::vec3 position{0.0F};
        glm::vec3 normal{0.0F, 1.0F, 0.0F};
        glm::vec3 color{1.0F};
        glm::vec4 material{0.55F, 0.0F, 0.0F, 0.0F};
    };
    struct TexturedVertex
    {
        glm::vec3 position{0.0F};
        glm::vec3 normal{0.0F, 1.0F, 0.0F};
        glm::vec3 color{1.0F};
        glm::vec2 uv{0.0F};
        glm::vec4 material{0.55F, 0.0F, 0.0F, 0.0F};
    };
    struct TexturedBatch
    {
        unsigned int textureId = 0;
        std::size_t firstVertex = 0;
        std::size_t vertexCount = 0;
    };

    static unsigned int CompileShader(unsigned int type, const char* source);
    static unsigned int CreateProgram(const char* vertexSource, const char* fragmentSource);

    void AddWireBox(const glm::vec3& center, const glm::vec3& halfExtents, const glm::vec3& color);
    void AddSolidBox(const glm::vec3& center, const glm::vec3& halfExtents, const glm::vec3& color, const MaterialParams& material);
    void AddWireOrientedBox(const glm::vec3& center, const glm::vec3& halfExtents, const glm::vec3& rotationEulerDegrees, const glm::vec3& color);
    void AddSolidOrientedBox(
        const glm::vec3& center,
        const glm::vec3& halfExtents,
        const glm::vec3& rotationEulerDegrees,
        const glm::vec3& color,
        const MaterialParams& material
    );
    void AddWireCapsule(const glm::vec3& center, float height, float radius, const glm::vec3& color);
    void AddSolidCapsule(
        const glm::vec3& center,
        float height,
        float radius,
        const glm::vec3& color,
        const MaterialParams& material
    );
    void AddSolidTriangle(
        const glm::vec3& a,
        const glm::vec3& b,
        const glm::vec3& c,
        const glm::vec3& color,
        const MaterialParams& material
    );

    RenderMode m_renderMode = RenderMode::Wireframe;

    unsigned int m_lineProgram = 0;
    unsigned int m_solidProgram = 0;

    unsigned int m_lineVao = 0;
    unsigned int m_lineVbo = 0;
    unsigned int m_solidVao = 0;
    unsigned int m_solidVbo = 0;
    unsigned int m_texturedProgram = 0;
    unsigned int m_texturedVao = 0;
    unsigned int m_texturedVbo = 0;
    std::size_t m_lineVboCapacityBytes = 0;
    std::size_t m_solidVboCapacityBytes = 0;
    std::size_t m_texturedVboCapacityBytes = 0;

    int m_lineViewProjLocation = -1;
    int m_solidViewProjLocation = -1;
    int m_solidCameraPosLocation = -1;
    int m_solidLightingEnabledLocation = -1;
    int m_solidLightDirLocation = -1;
    int m_solidLightColorLocation = -1;
    int m_solidLightIntensityLocation = -1;
    int m_solidFogEnabledLocation = -1;
    int m_solidFogColorLocation = -1;
    int m_solidFogDensityLocation = -1;
    int m_solidFogStartLocation = -1;
    int m_solidFogEndLocation = -1;
    int m_solidPointLightCountLocation = -1;
    int m_solidPointLightPosRangeLocation = -1;
    int m_solidPointLightColorIntensityLocation = -1;
    int m_solidSpotLightCountLocation = -1;
    int m_solidSpotLightPosRangeLocation = -1;
    int m_solidSpotLightDirInnerCosLocation = -1;
    int m_solidSpotLightColorIntensityLocation = -1;
    int m_solidSpotLightOuterCosLocation = -1;
    int m_texturedViewProjLocation = -1;
    int m_texturedCameraPosLocation = -1;
    int m_texturedLightingEnabledLocation = -1;
    int m_texturedLightDirLocation = -1;
    int m_texturedLightColorLocation = -1;
    int m_texturedLightIntensityLocation = -1;
    int m_texturedFogEnabledLocation = -1;
    int m_texturedFogColorLocation = -1;
    int m_texturedFogDensityLocation = -1;
    int m_texturedFogStartLocation = -1;
    int m_texturedFogEndLocation = -1;
    int m_texturedPointLightCountLocation = -1;
    int m_texturedPointLightPosRangeLocation = -1;
    int m_texturedPointLightColorIntensityLocation = -1;
    int m_texturedSpotLightCountLocation = -1;
    int m_texturedSpotLightPosRangeLocation = -1;
    int m_texturedSpotLightDirInnerCosLocation = -1;
    int m_texturedSpotLightColorIntensityLocation = -1;
    int m_texturedSpotLightOuterCosLocation = -1;
    int m_texturedAlbedoSamplerLocation = -1;

    std::vector<LineVertex> m_lineVertices;
    std::vector<LineVertex> m_overlayLineVertices;
    std::vector<SolidVertex> m_solidVertices;
    std::vector<TexturedVertex> m_texturedVertices;
    std::vector<TexturedBatch> m_texturedBatches;
    bool m_lightingEnabled = true;
    glm::vec3 m_cameraWorldPosition{0.0F, 2.0F, 0.0F};
    EnvironmentSettings m_environment{};
    std::vector<PointLight> m_pointLights;
    std::vector<SpotLight> m_spotLights;
    float m_cloudPhase = 0.0F;
    glm::vec3 m_postFxPulseColor{1.0F, 0.45F, 0.35F};
    float m_postFxPulseIntensity = 0.0F;
    Frustum m_frustum{};
};
} // namespace engine::render
