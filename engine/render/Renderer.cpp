#include "engine/render/Renderer.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>

#include <glad/glad.h>

#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "engine/core/Profiler.hpp"

namespace engine::render
{
namespace
{
constexpr int kMaxPointLights = 8;
constexpr int kMaxSpotLights = 8;

constexpr const char* kLineVertexShader = R"(
#version 450 core
layout (location = 0) in vec3 aPosition;
layout (location = 1) in vec3 aColor;

uniform mat4 uViewProjection;

out vec3 vColor;

void main()
{
    vColor = aColor;
    gl_Position = uViewProjection * vec4(aPosition, 1.0);
}
)";

constexpr const char* kLineFragmentShader = R"(
#version 450 core
in vec3 vColor;
out vec4 FragColor;

void main()
{
    FragColor = vec4(vColor, 1.0);
}
)";

constexpr const char* kSolidVertexShader = R"(
#version 450 core
layout (location = 0) in vec3 aPosition;
layout (location = 1) in vec3 aNormal;
layout (location = 2) in vec3 aColor;
layout (location = 3) in vec4 aMaterial;

uniform mat4 uViewProjection;
uniform mat4 uModel;

out vec3 vNormal;
out vec3 vColor;
out vec3 vWorldPos;
out vec4 vMaterial;

void main()
{
    vec4 worldPos = uModel * vec4(aPosition, 1.0);
    vWorldPos = worldPos.xyz;
    vNormal = mat3(uModel) * aNormal;
    vColor = aColor;
    vMaterial = aMaterial;
    gl_Position = uViewProjection * worldPos;
}
)";

constexpr const char* kSolidFragmentShader = R"(
#version 450 core
in vec3 vNormal;
in vec3 vColor;
in vec3 vWorldPos;
in vec4 vMaterial;
out vec4 FragColor;

uniform vec3 uCameraPos;
uniform int uLightingEnabled;
uniform vec3 uLightDir;
uniform vec3 uLightColor;
uniform float uLightIntensity;
uniform int uFogEnabled;
uniform vec3 uFogColor;
uniform float uFogDensity;
uniform float uFogStart;
uniform float uFogEnd;
uniform int uPointLightCount;
uniform vec4 uPointLightPosRange[8];
uniform vec4 uPointLightColorIntensity[8];
uniform int uSpotLightCount;
uniform vec4 uSpotLightPosRange[8];
uniform vec4 uSpotLightDirInnerCos[8];
uniform vec4 uSpotLightColorIntensity[8];
uniform float uSpotLightOuterCos[8];

void main()
{
    float roughness = clamp(vMaterial.x, 0.0, 1.0);
    float metallic = clamp(vMaterial.y, 0.0, 1.0);
    float emissive = max(vMaterial.z, 0.0);
    bool unlit = vMaterial.w > 0.5;
    vec3 baseColor = max(vColor, vec3(0.0));
    vec3 emissiveColor = baseColor * emissive;
    if (unlit || uLightingEnabled == 0)
    {
        vec3 color = baseColor + emissiveColor;
        color = pow(max(color, vec3(0.0)), vec3(1.0 / 2.2));
        FragColor = vec4(color, 1.0);
        return;
    }

    vec3 n = normalize(vNormal);
    vec3 viewDir = normalize(uCameraPos - vWorldPos);
    vec3 lightDir = normalize(uLightDir);
    float lambert = max(dot(n, lightDir), 0.0);
    vec3 diffuseColor = baseColor * (1.0 - metallic * 0.6);
    vec3 specColor = mix(vec3(0.04), baseColor, metallic);
    float shininess = mix(6.0, 140.0, 1.0 - roughness);
    vec3 halfVec = normalize(lightDir + viewDir);
    float spec = pow(max(dot(n, halfVec), 0.0), shininess);

    vec3 lit = diffuseColor * (0.10 + (0.90 * lambert * uLightIntensity));
    lit *= mix(vec3(1.0), uLightColor, 0.65);
    lit += specColor * spec * (0.35 + uLightIntensity * 0.5);

    for (int i = 0; i < uPointLightCount; ++i)
    {
        vec3 toLight = uPointLightPosRange[i].xyz - vWorldPos;
        float distSq = dot(toLight, toLight);
        float range = uPointLightPosRange[i].w;
        if (distSq < range * range)
        {
            float invDist = inversesqrt(distSq);
            float dist = distSq * invDist;
            vec3 l = toLight * invDist;
            float ndotl = max(dot(n, l), 0.0);
            float attenuation = 1.0 - (dist / range);
            attenuation *= attenuation;
            vec3 lightColor = uPointLightColorIntensity[i].rgb;
            float intensity = uPointLightColorIntensity[i].a;
            vec3 pointHalf = normalize(l + viewDir);
            float pointSpec = pow(max(dot(n, pointHalf), 0.0), shininess);
            lit += diffuseColor * lightColor * (ndotl * attenuation * intensity * 0.85);
            lit += specColor * lightColor * (pointSpec * attenuation * intensity * 0.45);
        }
    }

    for (int i = 0; i < uSpotLightCount; ++i)
    {
        vec3 toLight = uSpotLightPosRange[i].xyz - vWorldPos;
        float distSq = dot(toLight, toLight);
        float range = uSpotLightPosRange[i].w;
        if (distSq < range * range)
        {
            float invDist = inversesqrt(distSq);
            float dist = distSq * invDist;
            vec3 l = toLight * invDist;
            float cosTheta = dot(-l, uSpotLightDirInnerCos[i].xyz);
            float innerCos = uSpotLightDirInnerCos[i].w;
            float outerCos = uSpotLightOuterCos[i];
            float cone = smoothstep(outerCos, innerCos, cosTheta);
            float ndotl = max(dot(n, l), 0.0);
            float attenuation = 1.0 - (dist / range);
            attenuation *= attenuation;
            vec3 lightColor = uSpotLightColorIntensity[i].rgb;
            float intensity = uSpotLightColorIntensity[i].a;
            vec3 spotHalf = normalize(l + viewDir);
            float spotSpec = pow(max(dot(n, spotHalf), 0.0), shininess);
            lit += diffuseColor * lightColor * (ndotl * attenuation * cone * intensity * 0.95);
            lit += specColor * lightColor * (spotSpec * attenuation * cone * intensity * 0.5);
        }
    }

    lit = max(lit, baseColor * 0.06);
    lit += emissiveColor;

    if (uFogEnabled != 0)
    {
        float viewDist = length(vWorldPos - uCameraPos);
        float linear = clamp((viewDist - uFogStart) / max(0.001, uFogEnd - uFogStart), 0.0, 1.0);
        float expFog = 1.0 - exp(-uFogDensity * viewDist);
        float fogAmount = clamp(max(linear, expFog), 0.0, 1.0);
        lit = mix(lit, uFogColor, fogAmount);
    }

    lit = pow(max(lit, vec3(0.0)), vec3(1.0 / 2.2));
    FragColor = vec4(lit, 1.0);
}
)";

constexpr const char* kTexturedVertexShader = R"(
#version 450 core
layout (location = 0) in vec3 aPosition;
layout (location = 1) in vec3 aNormal;
layout (location = 2) in vec3 aColor;
layout (location = 3) in vec2 aUv;
layout (location = 4) in vec4 aMaterial;

uniform mat4 uViewProjection;

out vec3 vNormal;
out vec3 vColor;
out vec3 vWorldPos;
out vec2 vUv;
out vec4 vMaterial;

void main()
{
    vNormal = aNormal;
    vColor = aColor;
    vWorldPos = aPosition;
    vUv = aUv;
    vMaterial = aMaterial;
    gl_Position = uViewProjection * vec4(aPosition, 1.0);
}
)";

constexpr const char* kTexturedFragmentShader = R"(
#version 450 core
in vec3 vNormal;
in vec3 vColor;
in vec3 vWorldPos;
in vec2 vUv;
in vec4 vMaterial;
out vec4 FragColor;

uniform sampler2D uAlbedoTex;
uniform vec3 uCameraPos;
uniform int uLightingEnabled;
uniform vec3 uLightDir;
uniform vec3 uLightColor;
uniform float uLightIntensity;
uniform int uFogEnabled;
uniform vec3 uFogColor;
uniform float uFogDensity;
uniform float uFogStart;
uniform float uFogEnd;
uniform int uPointLightCount;
uniform vec4 uPointLightPosRange[8];
uniform vec4 uPointLightColorIntensity[8];
uniform int uSpotLightCount;
uniform vec4 uSpotLightPosRange[8];
uniform vec4 uSpotLightDirInnerCos[8];
uniform vec4 uSpotLightColorIntensity[8];
uniform float uSpotLightOuterCos[8];

void main()
{
    vec4 texel = texture(uAlbedoTex, vUv);
    vec3 baseColor = max(vColor * texel.rgb, vec3(0.0));
    float roughness = clamp(vMaterial.x, 0.0, 1.0);
    float metallic = clamp(vMaterial.y, 0.0, 1.0);
    float emissive = max(vMaterial.z, 0.0);
    bool unlit = vMaterial.w > 0.5;
    vec3 emissiveColor = baseColor * emissive;
    if (unlit || uLightingEnabled == 0)
    {
        vec3 color = baseColor + emissiveColor;
        color = pow(max(color, vec3(0.0)), vec3(1.0 / 2.2));
        FragColor = vec4(color, texel.a);
        return;
    }

    vec3 n = normalize(vNormal);
    vec3 viewDir = normalize(uCameraPos - vWorldPos);
    vec3 lightDir = normalize(uLightDir);
    float lambert = max(dot(n, lightDir), 0.0);
    vec3 diffuseColor = baseColor * (1.0 - metallic * 0.6);
    vec3 specColor = mix(vec3(0.04), baseColor, metallic);
    float shininess = mix(6.0, 140.0, 1.0 - roughness);
    vec3 halfVec = normalize(lightDir + viewDir);
    float spec = pow(max(dot(n, halfVec), 0.0), shininess);

    vec3 lit = diffuseColor * (0.10 + (0.90 * lambert * uLightIntensity));
    lit *= mix(vec3(1.0), uLightColor, 0.65);
    lit += specColor * spec * (0.35 + uLightIntensity * 0.5);

    for (int i = 0; i < uPointLightCount; ++i)
    {
        vec3 toLight = uPointLightPosRange[i].xyz - vWorldPos;
        float distSq = dot(toLight, toLight);
        float range = uPointLightPosRange[i].w;
        if (distSq < range * range)
        {
            float invDist = inversesqrt(distSq);
            float dist = distSq * invDist;
            vec3 l = toLight * invDist;
            float ndotl = max(dot(n, l), 0.0);
            float attenuation = 1.0 - (dist / range);
            attenuation *= attenuation;
            vec3 lightColor = uPointLightColorIntensity[i].rgb;
            float intensity = uPointLightColorIntensity[i].a;
            vec3 pointHalf = normalize(l + viewDir);
            float pointSpec = pow(max(dot(n, pointHalf), 0.0), shininess);
            lit += diffuseColor * lightColor * (ndotl * attenuation * intensity * 0.85);
            lit += specColor * lightColor * (pointSpec * attenuation * intensity * 0.45);
        }
    }

    for (int i = 0; i < uSpotLightCount; ++i)
    {
        vec3 toLight = uSpotLightPosRange[i].xyz - vWorldPos;
        float distSq = dot(toLight, toLight);
        float range = uSpotLightPosRange[i].w;
        if (distSq < range * range)
        {
            float invDist = inversesqrt(distSq);
            float dist = distSq * invDist;
            vec3 l = toLight * invDist;
            float cosTheta = dot(-l, uSpotLightDirInnerCos[i].xyz);
            float innerCos = uSpotLightDirInnerCos[i].w;
            float outerCos = uSpotLightOuterCos[i];
            float cone = smoothstep(outerCos, innerCos, cosTheta);
            float ndotl = max(dot(n, l), 0.0);
            float attenuation = 1.0 - (dist / range);
            attenuation *= attenuation;
            vec3 lightColor = uSpotLightColorIntensity[i].rgb;
            float intensity = uSpotLightColorIntensity[i].a;
            vec3 spotHalf = normalize(l + viewDir);
            float spotSpec = pow(max(dot(n, spotHalf), 0.0), shininess);
            lit += diffuseColor * lightColor * (ndotl * attenuation * cone * intensity * 0.95);
            lit += specColor * lightColor * (spotSpec * attenuation * cone * intensity * 0.5);
        }
    }

    lit = max(lit, baseColor * 0.06);
    lit += emissiveColor;

    if (uFogEnabled != 0)
    {
        float viewDist = length(vWorldPos - uCameraPos);
        float linear = clamp((viewDist - uFogStart) / max(0.001, uFogEnd - uFogStart), 0.0, 1.0);
        float expFog = 1.0 - exp(-uFogDensity * viewDist);
        float fogAmount = clamp(max(linear, expFog), 0.0, 1.0);
        lit = mix(lit, uFogColor, fogAmount);
    }

    lit = pow(max(lit, vec3(0.0)), vec3(1.0 / 2.2));
    FragColor = vec4(lit, texel.a);
}
)";

glm::mat3 RotationMatrixFromEulerDegrees(const glm::vec3& eulerDegrees)
{
    glm::mat4 transform{1.0F};
    transform = glm::rotate(transform, glm::radians(eulerDegrees.y), glm::vec3{0.0F, 1.0F, 0.0F});
    transform = glm::rotate(transform, glm::radians(eulerDegrees.x), glm::vec3{1.0F, 0.0F, 0.0F});
    transform = glm::rotate(transform, glm::radians(eulerDegrees.z), glm::vec3{0.0F, 0.0F, 1.0F});
    return glm::mat3(transform);
}
} // namespace

bool Renderer::Initialize(int framebufferWidth, int framebufferHeight)
{
    glEnable(GL_DEPTH_TEST);
    // NOTE: GL_CULL_FACE intentionally NOT enabled — the geometry pipeline
    // (boxes, capsules, UI quads) has mixed CW/CCW winding order.
    // A full winding audit would be needed before culling can safely be enabled.

    // Reserve transient CPU-side buffers once to reduce per-frame growth churn.
    m_lineVertices.reserve(8192);
    m_overlayLineVertices.reserve(4096);
    m_solidVertices.reserve(32768);
    m_texturedVertices.reserve(16384);
    m_texturedBatches.reserve(512);

    m_lineProgram = CreateProgram(kLineVertexShader, kLineFragmentShader);
    m_solidProgram = CreateProgram(kSolidVertexShader, kSolidFragmentShader);
    m_texturedProgram = CreateProgram(kTexturedVertexShader, kTexturedFragmentShader);
    if (m_lineProgram == 0 || m_solidProgram == 0 || m_texturedProgram == 0)
    {
        return false;
    }

    glGenVertexArrays(1, &m_lineVao);
    glGenBuffers(1, &m_lineVbo);

    glBindVertexArray(m_lineVao);
    glBindBuffer(GL_ARRAY_BUFFER, m_lineVbo);
    m_lineVboCapacityBytes = 2U * 1024U * 1024U;
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(m_lineVboCapacityBytes), nullptr, GL_STREAM_DRAW);

    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(LineVertex), reinterpret_cast<void*>(offsetof(LineVertex, position)));
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(LineVertex), reinterpret_cast<void*>(offsetof(LineVertex, color)));
    glEnableVertexAttribArray(1);

    glGenVertexArrays(1, &m_solidVao);
    glGenBuffers(1, &m_solidVbo);

    glBindVertexArray(m_solidVao);
    glBindBuffer(GL_ARRAY_BUFFER, m_solidVbo);
    m_solidVboCapacityBytes = 4U * 1024U * 1024U;
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(m_solidVboCapacityBytes), nullptr, GL_STREAM_DRAW);

    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(SolidVertex), reinterpret_cast<void*>(offsetof(SolidVertex, position)));
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(SolidVertex), reinterpret_cast<void*>(offsetof(SolidVertex, normal)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, sizeof(SolidVertex), reinterpret_cast<void*>(offsetof(SolidVertex, color)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, sizeof(SolidVertex), reinterpret_cast<void*>(offsetof(SolidVertex, material)));
    glEnableVertexAttribArray(3);

    glGenVertexArrays(1, &m_texturedVao);
    glGenBuffers(1, &m_texturedVbo);

    glBindVertexArray(m_texturedVao);
    glBindBuffer(GL_ARRAY_BUFFER, m_texturedVbo);
    m_texturedVboCapacityBytes = 4U * 1024U * 1024U;
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(m_solidVboCapacityBytes), nullptr, GL_STREAM_DRAW);

    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(TexturedVertex), reinterpret_cast<void*>(offsetof(TexturedVertex, position)));
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(TexturedVertex), reinterpret_cast<void*>(offsetof(TexturedVertex, normal)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, sizeof(TexturedVertex), reinterpret_cast<void*>(offsetof(TexturedVertex, color)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(3, 2, GL_FLOAT, GL_FALSE, sizeof(TexturedVertex), reinterpret_cast<void*>(offsetof(TexturedVertex, uv)));
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(4, 4, GL_FLOAT, GL_FALSE, sizeof(TexturedVertex), reinterpret_cast<void*>(offsetof(TexturedVertex, material)));
    glEnableVertexAttribArray(4);

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);

    m_lineViewProjLocation = glGetUniformLocation(m_lineProgram, "uViewProjection");
    m_solidViewProjLocation = glGetUniformLocation(m_solidProgram, "uViewProjection");
    m_solidModelLocation = glGetUniformLocation(m_solidProgram, "uModel");
    m_solidCameraPosLocation = glGetUniformLocation(m_solidProgram, "uCameraPos");
    m_solidLightingEnabledLocation = glGetUniformLocation(m_solidProgram, "uLightingEnabled");
    m_solidLightDirLocation = glGetUniformLocation(m_solidProgram, "uLightDir");
    m_solidLightColorLocation = glGetUniformLocation(m_solidProgram, "uLightColor");
    m_solidLightIntensityLocation = glGetUniformLocation(m_solidProgram, "uLightIntensity");
    m_solidFogEnabledLocation = glGetUniformLocation(m_solidProgram, "uFogEnabled");
    m_solidFogColorLocation = glGetUniformLocation(m_solidProgram, "uFogColor");
    m_solidFogDensityLocation = glGetUniformLocation(m_solidProgram, "uFogDensity");
    m_solidFogStartLocation = glGetUniformLocation(m_solidProgram, "uFogStart");
    m_solidFogEndLocation = glGetUniformLocation(m_solidProgram, "uFogEnd");
    m_solidPointLightCountLocation = glGetUniformLocation(m_solidProgram, "uPointLightCount");
    m_solidPointLightPosRangeLocation = glGetUniformLocation(m_solidProgram, "uPointLightPosRange");
    m_solidPointLightColorIntensityLocation = glGetUniformLocation(m_solidProgram, "uPointLightColorIntensity");
    m_solidSpotLightCountLocation = glGetUniformLocation(m_solidProgram, "uSpotLightCount");
    m_solidSpotLightPosRangeLocation = glGetUniformLocation(m_solidProgram, "uSpotLightPosRange");
    m_solidSpotLightDirInnerCosLocation = glGetUniformLocation(m_solidProgram, "uSpotLightDirInnerCos");
    m_solidSpotLightColorIntensityLocation = glGetUniformLocation(m_solidProgram, "uSpotLightColorIntensity");
    m_solidSpotLightOuterCosLocation = glGetUniformLocation(m_solidProgram, "uSpotLightOuterCos");
    m_texturedViewProjLocation = glGetUniformLocation(m_texturedProgram, "uViewProjection");
    m_texturedCameraPosLocation = glGetUniformLocation(m_texturedProgram, "uCameraPos");
    m_texturedLightingEnabledLocation = glGetUniformLocation(m_texturedProgram, "uLightingEnabled");
    m_texturedLightDirLocation = glGetUniformLocation(m_texturedProgram, "uLightDir");
    m_texturedLightColorLocation = glGetUniformLocation(m_texturedProgram, "uLightColor");
    m_texturedLightIntensityLocation = glGetUniformLocation(m_texturedProgram, "uLightIntensity");
    m_texturedFogEnabledLocation = glGetUniformLocation(m_texturedProgram, "uFogEnabled");
    m_texturedFogColorLocation = glGetUniformLocation(m_texturedProgram, "uFogColor");
    m_texturedFogDensityLocation = glGetUniformLocation(m_texturedProgram, "uFogDensity");
    m_texturedFogStartLocation = glGetUniformLocation(m_texturedProgram, "uFogStart");
    m_texturedFogEndLocation = glGetUniformLocation(m_texturedProgram, "uFogEnd");
    m_texturedPointLightCountLocation = glGetUniformLocation(m_texturedProgram, "uPointLightCount");
    m_texturedPointLightPosRangeLocation = glGetUniformLocation(m_texturedProgram, "uPointLightPosRange");
    m_texturedPointLightColorIntensityLocation = glGetUniformLocation(m_texturedProgram, "uPointLightColorIntensity");
    m_texturedSpotLightCountLocation = glGetUniformLocation(m_texturedProgram, "uSpotLightCount");
    m_texturedSpotLightPosRangeLocation = glGetUniformLocation(m_texturedProgram, "uSpotLightPosRange");
    m_texturedSpotLightDirInnerCosLocation = glGetUniformLocation(m_texturedProgram, "uSpotLightDirInnerCos");
    m_texturedSpotLightColorIntensityLocation = glGetUniformLocation(m_texturedProgram, "uSpotLightColorIntensity");
    m_texturedSpotLightOuterCosLocation = glGetUniformLocation(m_texturedProgram, "uSpotLightOuterCos");
    m_texturedAlbedoSamplerLocation = glGetUniformLocation(m_texturedProgram, "uAlbedoTex");

    SetViewport(framebufferWidth, framebufferHeight);
    return true;
}

void Renderer::Shutdown()
{
    FreeAllGpuMeshes();

    if (m_lineVbo != 0)
    {
        glDeleteBuffers(1, &m_lineVbo);
        m_lineVbo = 0;
    }
    if (m_solidVbo != 0)
    {
        glDeleteBuffers(1, &m_solidVbo);
        m_solidVbo = 0;
    }
    if (m_texturedVbo != 0)
    {
        glDeleteBuffers(1, &m_texturedVbo);
        m_texturedVbo = 0;
    }

    if (m_lineVao != 0)
    {
        glDeleteVertexArrays(1, &m_lineVao);
        m_lineVao = 0;
    }
    if (m_solidVao != 0)
    {
        glDeleteVertexArrays(1, &m_solidVao);
        m_solidVao = 0;
    }
    if (m_texturedVao != 0)
    {
        glDeleteVertexArrays(1, &m_texturedVao);
        m_texturedVao = 0;
    }

    if (m_lineProgram != 0)
    {
        glDeleteProgram(m_lineProgram);
        m_lineProgram = 0;
    }
    if (m_solidProgram != 0)
    {
        glDeleteProgram(m_solidProgram);
        m_solidProgram = 0;
    }
    if (m_texturedProgram != 0)
    {
        glDeleteProgram(m_texturedProgram);
        m_texturedProgram = 0;
    }
}

void Renderer::SetViewport(int framebufferWidth, int framebufferHeight) const
{
    glViewport(0, 0, framebufferWidth, framebufferHeight);
}

void Renderer::SetRenderMode(RenderMode mode)
{
    m_renderMode = mode;
}

void Renderer::ToggleRenderMode()
{
    m_renderMode = m_renderMode == RenderMode::Wireframe ? RenderMode::Filled : RenderMode::Wireframe;
}

void Renderer::SetEnvironmentSettings(const EnvironmentSettings& settings)
{
    m_environment = settings;
    m_environment.cloudCoverage = glm::clamp(m_environment.cloudCoverage, 0.0F, 1.0F);
    m_environment.cloudDensity = glm::clamp(m_environment.cloudDensity, 0.0F, 1.0F);
    m_environment.cloudSpeed = glm::clamp(m_environment.cloudSpeed, 0.0F, 10.0F);
    m_environment.fogDensity = glm::clamp(m_environment.fogDensity, 0.0F, 1.0F);
    m_environment.fogStart = glm::max(0.0F, m_environment.fogStart);
    m_environment.fogEnd = glm::max(m_environment.fogStart + 0.01F, m_environment.fogEnd);
}

void Renderer::SetPointLights(const std::vector<PointLight>& lights)
{
    m_pointLights = lights;
    if (m_pointLights.size() > static_cast<std::size_t>(kMaxPointLights))
    {
        m_pointLights.resize(static_cast<std::size_t>(kMaxPointLights));
    }
}

void Renderer::SetPointLights(std::vector<PointLight>&& lights)
{
    m_pointLights = std::move(lights);
    if (m_pointLights.size() > static_cast<std::size_t>(kMaxPointLights))
    {
        m_pointLights.resize(static_cast<std::size_t>(kMaxPointLights));
    }
}

void Renderer::SetSpotLights(const std::vector<SpotLight>& lights)
{
    m_spotLights = lights;
    if (m_spotLights.size() > static_cast<std::size_t>(kMaxSpotLights))
    {
        m_spotLights.resize(static_cast<std::size_t>(kMaxSpotLights));
    }
}

void Renderer::SetSpotLights(std::vector<SpotLight>&& lights)
{
    m_spotLights = std::move(lights);
    if (m_spotLights.size() > static_cast<std::size_t>(kMaxSpotLights))
    {
        m_spotLights.resize(static_cast<std::size_t>(kMaxSpotLights));
    }
}

void Renderer::SetPostFxPulse(const glm::vec3& color, float intensity)
{
    m_postFxPulseColor = glm::clamp(color, glm::vec3{-2.0F}, glm::vec3{2.0F});
    m_postFxPulseIntensity = glm::clamp(intensity, 0.0F, 2.0F);
}

void Renderer::SetLightingEnabled(bool enabled)
{
    m_lightingEnabled = enabled;
}

void Renderer::SetCameraWorldPosition(const glm::vec3& position)
{
    m_cameraWorldPosition = position;
}

void Renderer::BeginFrame(const glm::vec3& clearColor)
{
    m_lineVertices.clear();
    m_overlayLineVertices.clear();
    m_solidVertices.clear();
    m_texturedVertices.clear();
    m_texturedBatches.clear();
    m_gpuMeshDraws.clear();

    // Periodically reclaim excess transient buffer capacity (e.g. after leaving benchmark).
    // If capacity is >4× the last frame's usage or >256K, shrink to save RAM.
    constexpr std::size_t kShrinkThreshold = 256U * 1024U;
    if (m_solidVertices.capacity() > kShrinkThreshold && m_solidVertices.capacity() > m_lastFrameSolidCount * 4)
    {
        m_solidVertices.shrink_to_fit();
        m_solidVertices.reserve(std::max<std::size_t>(32768, m_lastFrameSolidCount * 2));
    }
    if (m_lineVertices.capacity() > kShrinkThreshold)
    {
        m_lineVertices.shrink_to_fit();
        m_lineVertices.reserve(8192);
    }

    m_cloudPhase += 0.016F;
    glm::vec3 finalClear = clearColor;
    if (m_environment.skyEnabled)
    {
        finalClear = glm::mix(m_environment.skyBottomColor, m_environment.skyTopColor, 0.72F);
        if (m_environment.cloudsEnabled)
        {
            const float cloudWave = 0.5F + 0.5F * std::sin(m_cloudPhase * glm::max(0.05F, m_environment.cloudSpeed));
            const float cloudBoost = m_environment.cloudCoverage * m_environment.cloudDensity * cloudWave * 0.14F;
            finalClear += glm::vec3{cloudBoost};
        }
        finalClear = glm::clamp(finalClear, glm::vec3{0.0F}, glm::vec3{1.0F});
    }

    if (m_postFxPulseIntensity > 0.0F)
    {
        finalClear += m_postFxPulseColor * (0.16F * m_postFxPulseIntensity);
        finalClear = glm::clamp(finalClear, glm::vec3{0.0F}, glm::vec3{1.0F});
    }

    glClearColor(finalClear.r, finalClear.g, finalClear.b, 1.0F);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

void Renderer::EndFrame(const glm::mat4& viewProjection)
{
    PROFILE_SCOPE("Renderer::EndFrame");
    m_frustum.Extract(viewProjection);

    auto& profiler = engine::core::Profiler::Instance();

    const auto ensureBufferCapacity = [](std::size_t* ioCapacityBytes, std::size_t requiredBytes) {
        if (ioCapacityBytes == nullptr || requiredBytes == 0U)
        {
            return;
        }
        if (requiredBytes <= *ioCapacityBytes)
        {
            // Orphan the existing buffer to avoid GPU sync stalls.
            glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(*ioCapacityBytes), nullptr, GL_STREAM_DRAW);
            return;
        }

        std::size_t newCapacity = std::max<std::size_t>(*ioCapacityBytes, 1024U * 1024U);
        while (newCapacity < requiredBytes)
        {
            newCapacity *= 2U;
        }
        glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(newCapacity), nullptr, GL_STREAM_DRAW);
        *ioCapacityBytes = newCapacity;
    };

    // ─── Build light uniform arrays ONCE (shared by solid + textured) ───
    const glm::vec3 normalizedLightDir = glm::normalize(m_environment.directionalLightDirection);

    std::array<glm::vec4, static_cast<std::size_t>(kMaxPointLights)> pointPosRange{};
    std::array<glm::vec4, static_cast<std::size_t>(kMaxPointLights)> pointColorIntensity{};
    const int pointCount = std::min(static_cast<int>(m_pointLights.size()), kMaxPointLights);
    for (int i = 0; i < pointCount; ++i)
    {
        const PointLight& light = m_pointLights[static_cast<std::size_t>(i)];
        pointPosRange[static_cast<std::size_t>(i)] =
            glm::vec4{light.position.x, light.position.y, light.position.z, glm::max(0.001F, light.range)};
        pointColorIntensity[static_cast<std::size_t>(i)] =
            glm::vec4{light.color.x, light.color.y, light.color.z, glm::max(0.0F, light.intensity)};
    }

    std::array<glm::vec4, static_cast<std::size_t>(kMaxSpotLights)> spotPosRange{};
    std::array<glm::vec4, static_cast<std::size_t>(kMaxSpotLights)> spotDirInnerCos{};
    std::array<glm::vec4, static_cast<std::size_t>(kMaxSpotLights)> spotColorIntensity{};
    std::array<float, static_cast<std::size_t>(kMaxSpotLights)> spotOuterCos{};
    const int spotCount = std::min(static_cast<int>(m_spotLights.size()), kMaxSpotLights);
    for (int i = 0; i < spotCount; ++i)
    {
        const SpotLight& light = m_spotLights[static_cast<std::size_t>(i)];
        const glm::vec3 dir = glm::length(light.direction) > 1.0e-6F ? glm::normalize(light.direction) : glm::vec3{0.0F, -1.0F, 0.0F};
        spotPosRange[static_cast<std::size_t>(i)] =
            glm::vec4{light.position.x, light.position.y, light.position.z, glm::max(0.001F, light.range)};
        spotDirInnerCos[static_cast<std::size_t>(i)] =
            glm::vec4{dir.x, dir.y, dir.z, glm::clamp(light.innerCos, -1.0F, 1.0F)};
        spotColorIntensity[static_cast<std::size_t>(i)] =
            glm::vec4{light.color.x, light.color.y, light.color.z, glm::max(0.0F, light.intensity)};
        spotOuterCos[static_cast<std::size_t>(i)] = glm::clamp(light.outerCos, -1.0F, 1.0F);
    }

    // Lambda to upload shared light uniforms to any program.
    const auto uploadLightUniforms = [&](
        int lightDirLoc, int lightColorLoc, int lightIntensityLoc,
        int cameraPosLoc, int lightingEnabledLoc,
        int fogEnabledLoc, int fogColorLoc, int fogDensityLoc, int fogStartLoc, int fogEndLoc,
        int pointCountLoc, int pointPosRangeLoc, int pointColorIntensityLoc,
        int spotCountLoc, int spotPosRangeLoc, int spotDirInnerCosLoc, int spotColorIntensityLoc, int spotOuterCosLoc)
    {
        glUniform3fv(cameraPosLoc, 1, glm::value_ptr(m_cameraWorldPosition));
        glUniform1i(lightingEnabledLoc, m_lightingEnabled ? 1 : 0);
        glUniform3fv(lightDirLoc, 1, glm::value_ptr(normalizedLightDir));
        glUniform3fv(lightColorLoc, 1, glm::value_ptr(m_environment.directionalLightColor));
        glUniform1f(lightIntensityLoc, m_environment.directionalLightIntensity);
        glUniform1i(fogEnabledLoc, m_environment.fogEnabled ? 1 : 0);
        glUniform3fv(fogColorLoc, 1, glm::value_ptr(m_environment.fogColor));
        glUniform1f(fogDensityLoc, m_environment.fogDensity);
        glUniform1f(fogStartLoc, m_environment.fogStart);
        glUniform1f(fogEndLoc, m_environment.fogEnd);
        glUniform1i(pointCountLoc, pointCount);
        glUniform4fv(pointPosRangeLoc, kMaxPointLights, glm::value_ptr(pointPosRange[0]));
        glUniform4fv(pointColorIntensityLoc, kMaxPointLights, glm::value_ptr(pointColorIntensity[0]));
        glUniform1i(spotCountLoc, spotCount);
        glUniform4fv(spotPosRangeLoc, kMaxSpotLights, glm::value_ptr(spotPosRange[0]));
        glUniform4fv(spotDirInnerCosLoc, kMaxSpotLights, glm::value_ptr(spotDirInnerCos[0]));
        glUniform4fv(spotColorIntensityLoc, kMaxSpotLights, glm::value_ptr(spotColorIntensity[0]));
        glUniform1fv(spotOuterCosLoc, kMaxSpotLights, spotOuterCos.data());
    };

    // ─── Solid pass ───
    if (!m_solidVertices.empty())
    {
        glUseProgram(m_solidProgram);
        glUniformMatrix4fv(m_solidViewProjLocation, 1, GL_FALSE, glm::value_ptr(viewProjection));
        const glm::mat4 identity{1.0F};
        glUniformMatrix4fv(m_solidModelLocation, 1, GL_FALSE, glm::value_ptr(identity));
        uploadLightUniforms(
            m_solidLightDirLocation, m_solidLightColorLocation, m_solidLightIntensityLocation,
            m_solidCameraPosLocation, m_solidLightingEnabledLocation,
            m_solidFogEnabledLocation, m_solidFogColorLocation, m_solidFogDensityLocation,
            m_solidFogStartLocation, m_solidFogEndLocation,
            m_solidPointLightCountLocation, m_solidPointLightPosRangeLocation, m_solidPointLightColorIntensityLocation,
            m_solidSpotLightCountLocation, m_solidSpotLightPosRangeLocation, m_solidSpotLightDirInnerCosLocation,
            m_solidSpotLightColorIntensityLocation, m_solidSpotLightOuterCosLocation
        );

        glBindVertexArray(m_solidVao);
        glBindBuffer(GL_ARRAY_BUFFER, m_solidVbo);
        const std::size_t solidBytes = m_solidVertices.size() * sizeof(SolidVertex);
        ensureBufferCapacity(&m_solidVboCapacityBytes, solidBytes);
        glBufferSubData(GL_ARRAY_BUFFER, 0, static_cast<GLsizeiptr>(solidBytes), m_solidVertices.data());

        glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(m_solidVertices.size()));
        profiler.RecordDrawCall(static_cast<std::uint32_t>(m_solidVertices.size()), static_cast<std::uint32_t>(m_solidVertices.size() / 3));
        profiler.StatsMut().solidVboBytes = solidBytes;
    }

    // ─── GPU-cached mesh pass (uses same solid shader with per-draw model matrix) ───
    if (!m_gpuMeshDraws.empty())
    {
        // Ensure solid shader is bound with correct shared uniforms.
        const bool solidWasActive = !m_solidVertices.empty(); // shader already set up
        if (!solidWasActive)
        {
            glUseProgram(m_solidProgram);
            glUniformMatrix4fv(m_solidViewProjLocation, 1, GL_FALSE, glm::value_ptr(viewProjection));
            uploadLightUniforms(
                m_solidLightDirLocation, m_solidLightColorLocation, m_solidLightIntensityLocation,
                m_solidCameraPosLocation, m_solidLightingEnabledLocation,
                m_solidFogEnabledLocation, m_solidFogColorLocation, m_solidFogDensityLocation,
                m_solidFogStartLocation, m_solidFogEndLocation,
                m_solidPointLightCountLocation, m_solidPointLightPosRangeLocation, m_solidPointLightColorIntensityLocation,
                m_solidSpotLightCountLocation, m_solidSpotLightPosRangeLocation, m_solidSpotLightDirInnerCosLocation,
                m_solidSpotLightColorIntensityLocation, m_solidSpotLightOuterCosLocation
            );
        }

        for (const GpuMeshDraw& draw : m_gpuMeshDraws)
        {
            const auto it = m_gpuMeshes.find(draw.meshId);
            if (it == m_gpuMeshes.end() || it->second.vertexCount == 0)
            {
                continue;
            }
            glUniformMatrix4fv(m_solidModelLocation, 1, GL_FALSE, glm::value_ptr(draw.modelMatrix));
            glBindVertexArray(it->second.vao);
            glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(it->second.vertexCount));
            profiler.RecordDrawCall(it->second.vertexCount, it->second.vertexCount / 3);
        }

        // Reset model matrix back to identity for subsequent passes.
        const glm::mat4 identity{1.0F};
        glUniformMatrix4fv(m_solidModelLocation, 1, GL_FALSE, glm::value_ptr(identity));
    }

    // ─── Textured pass ───
    if (!m_texturedVertices.empty() && !m_texturedBatches.empty())
    {
        glUseProgram(m_texturedProgram);
        glUniformMatrix4fv(m_texturedViewProjLocation, 1, GL_FALSE, glm::value_ptr(viewProjection));
        uploadLightUniforms(
            m_texturedLightDirLocation, m_texturedLightColorLocation, m_texturedLightIntensityLocation,
            m_texturedCameraPosLocation, m_texturedLightingEnabledLocation,
            m_texturedFogEnabledLocation, m_texturedFogColorLocation, m_texturedFogDensityLocation,
            m_texturedFogStartLocation, m_texturedFogEndLocation,
            m_texturedPointLightCountLocation, m_texturedPointLightPosRangeLocation, m_texturedPointLightColorIntensityLocation,
            m_texturedSpotLightCountLocation, m_texturedSpotLightPosRangeLocation, m_texturedSpotLightDirInnerCosLocation,
            m_texturedSpotLightColorIntensityLocation, m_texturedSpotLightOuterCosLocation
        );

        glBindVertexArray(m_texturedVao);
        glBindBuffer(GL_ARRAY_BUFFER, m_texturedVbo);
        const std::size_t texturedBytes = m_texturedVertices.size() * sizeof(TexturedVertex);
        ensureBufferCapacity(&m_texturedVboCapacityBytes, texturedBytes);
        glBufferSubData(GL_ARRAY_BUFFER, 0, static_cast<GLsizeiptr>(texturedBytes), m_texturedVertices.data());

        glActiveTexture(GL_TEXTURE0);
        glUniform1i(m_texturedAlbedoSamplerLocation, 0);
        for (const TexturedBatch& batch : m_texturedBatches)
        {
            if (batch.textureId == 0 || batch.vertexCount == 0)
            {
                continue;
            }
            glBindTexture(GL_TEXTURE_2D, batch.textureId);
            glDrawArrays(GL_TRIANGLES, static_cast<GLint>(batch.firstVertex), static_cast<GLsizei>(batch.vertexCount));
            profiler.RecordDrawCall(static_cast<std::uint32_t>(batch.vertexCount), static_cast<std::uint32_t>(batch.vertexCount / 3));
        }
        glBindTexture(GL_TEXTURE_2D, 0);
        profiler.StatsMut().texturedVboBytes = texturedBytes;
    }

    // ─── Line pass (combined: lines + overlay lines in single buffer) ───
    const bool hasLines = !m_lineVertices.empty();
    const bool hasOverlay = !m_overlayLineVertices.empty();
    if (hasLines || hasOverlay)
    {
        glUseProgram(m_lineProgram);
        glUniformMatrix4fv(m_lineViewProjLocation, 1, GL_FALSE, glm::value_ptr(viewProjection));
        glBindVertexArray(m_lineVao);
        glBindBuffer(GL_ARRAY_BUFFER, m_lineVbo);

        const std::size_t lineBytes = m_lineVertices.size() * sizeof(LineVertex);
        const std::size_t overlayBytes = m_overlayLineVertices.size() * sizeof(LineVertex);
        const std::size_t totalBytes = lineBytes + overlayBytes;

        // Single orphan + upload for both line arrays.
        ensureBufferCapacity(&m_lineVboCapacityBytes, totalBytes);
        if (hasLines)
        {
            glBufferSubData(GL_ARRAY_BUFFER, 0, static_cast<GLsizeiptr>(lineBytes), m_lineVertices.data());
        }
        if (hasOverlay)
        {
            glBufferSubData(GL_ARRAY_BUFFER, static_cast<GLintptr>(lineBytes), static_cast<GLsizeiptr>(overlayBytes), m_overlayLineVertices.data());
        }

        if (hasLines)
        {
            glDrawArrays(GL_LINES, 0, static_cast<GLsizei>(m_lineVertices.size()));
            profiler.RecordDrawCall(static_cast<std::uint32_t>(m_lineVertices.size()), 0);
            profiler.StatsMut().lineVboBytes = lineBytes;
        }
        if (hasOverlay)
        {
            glDisable(GL_DEPTH_TEST);
            glDrawArrays(GL_LINES, static_cast<GLint>(m_lineVertices.size()), static_cast<GLsizei>(m_overlayLineVertices.size()));
            profiler.RecordDrawCall(static_cast<std::uint32_t>(m_overlayLineVertices.size()), 0);
            glEnable(GL_DEPTH_TEST);
        }
    }

    // No glBindBuffer(0)/glBindVertexArray(0)/glUseProgram(0) cleanup needed —
    // next frame's BeginFrame/EndFrame will set fresh state.

    m_lastFrameSolidCount = m_solidVertices.size();
}

void Renderer::DrawLine(const glm::vec3& from, const glm::vec3& to, const glm::vec3& color)
{
    m_lineVertices.push_back(LineVertex{from, color});
    m_lineVertices.push_back(LineVertex{to, color});
}

void Renderer::DrawOverlayLine(const glm::vec3& from, const glm::vec3& to, const glm::vec3& color)
{
    m_overlayLineVertices.push_back(LineVertex{from, color});
    m_overlayLineVertices.push_back(LineVertex{to, color});
}

void Renderer::DrawBox(
    const glm::vec3& center,
    const glm::vec3& halfExtents,
    const glm::vec3& color,
    const MaterialParams& material
)
{
    if (m_renderMode == RenderMode::Wireframe)
    {
        AddWireBox(center, halfExtents, color);
    }
    else
    {
        AddSolidBox(center, halfExtents, color, material);
    }
}

void Renderer::DrawOrientedBox(
    const glm::vec3& center,
    const glm::vec3& halfExtents,
    const glm::vec3& rotationEulerDegrees,
    const glm::vec3& color,
    const MaterialParams& material
)
{
    if (m_renderMode == RenderMode::Wireframe)
    {
        AddWireOrientedBox(center, halfExtents, rotationEulerDegrees, color);
    }
    else
    {
        AddSolidOrientedBox(center, halfExtents, rotationEulerDegrees, color, material);
    }
}

void Renderer::DrawCapsule(
    const glm::vec3& center,
    float height,
    float radius,
    const glm::vec3& color,
    const MaterialParams& material
)
{
    if (m_renderMode == RenderMode::Wireframe)
    {
        AddWireCapsule(center, height, radius, color);
    }
    else
    {
        // Distance-based LOD for capsule tessellation.
        const float distSq = glm::dot(center - m_cameraWorldPosition, center - m_cameraWorldPosition);
        int segments = 16;
        int hemiRings = 6;
        if (distSq > 900.0F)       // > 30m
        {
            segments = 6;
            hemiRings = 2;
        }
        else if (distSq > 225.0F)  // > 15m
        {
            segments = 8;
            hemiRings = 3;
        }
        else if (distSq > 64.0F)   // > 8m
        {
            segments = 12;
            hemiRings = 4;
        }
        AddSolidCapsule(center, height, radius, color, material, segments, hemiRings);
    }
}

void Renderer::DrawMesh(
    const MeshGeometry& mesh,
    const glm::vec3& position,
    const glm::vec3& rotationEulerDegrees,
    const glm::vec3& scale,
    const glm::vec3& color,
    const MaterialParams& material
)
{
    if (mesh.positions.empty())
    {
        return;
    }

    const glm::mat3 rotation = RotationMatrixFromEulerDegrees(rotationEulerDegrees);

    auto transformPos = [&](const glm::vec3& p) {
        return position + rotation * (p * scale);
    };
    auto transformNormal = [&](const glm::vec3& n) {
        glm::vec3 out = rotation * n;
        if (glm::length(out) > 1.0e-6F)
        {
            out = glm::normalize(out);
        }
        return out;
    };

    auto emitTri = [&](std::uint32_t ia, std::uint32_t ib, std::uint32_t ic) {
        if (ia >= mesh.positions.size() || ib >= mesh.positions.size() || ic >= mesh.positions.size())
        {
            return;
        }

        const glm::vec3 a = transformPos(mesh.positions[ia]);
        const glm::vec3 b = transformPos(mesh.positions[ib]);
        const glm::vec3 c = transformPos(mesh.positions[ic]);

        glm::vec3 fallbackNormal = glm::cross(b - a, c - a);
        if (glm::length(fallbackNormal) > 1.0e-6F)
        {
            fallbackNormal = glm::normalize(fallbackNormal);
        }
        else
        {
            fallbackNormal = glm::vec3{0.0F, 1.0F, 0.0F};
        }
        const glm::vec3 nA = ia < mesh.normals.size() ? transformNormal(mesh.normals[ia]) : fallbackNormal;
        const glm::vec3 nB = ib < mesh.normals.size() ? transformNormal(mesh.normals[ib]) : fallbackNormal;
        const glm::vec3 nC = ic < mesh.normals.size() ? transformNormal(mesh.normals[ic]) : fallbackNormal;
        const glm::vec3 cA = glm::clamp(ia < mesh.colors.size() ? color * mesh.colors[ia] : color, glm::vec3{0.0F}, glm::vec3{1.0F});
        const glm::vec3 cB = glm::clamp(ib < mesh.colors.size() ? color * mesh.colors[ib] : color, glm::vec3{0.0F}, glm::vec3{1.0F});
        const glm::vec3 cC = glm::clamp(ic < mesh.colors.size() ? color * mesh.colors[ic] : color, glm::vec3{0.0F}, glm::vec3{1.0F});

        if (m_renderMode == RenderMode::Wireframe)
        {
            const glm::vec3 wireColor = glm::clamp((cA + cB + cC) * (1.0F / 3.0F), glm::vec3{0.0F}, glm::vec3{1.0F});
            DrawLine(a, b, wireColor);
            DrawLine(b, c, wireColor);
            DrawLine(c, a, wireColor);
        }
        else
        {
            const glm::vec4 packedMaterial{
                glm::clamp(material.roughness, 0.0F, 1.0F),
                glm::clamp(material.metallic, 0.0F, 1.0F),
                glm::max(0.0F, material.emissive),
                material.unlit ? 1.0F : 0.0F,
            };
            m_solidVertices.push_back(SolidVertex{a, nA, cA, packedMaterial});
            m_solidVertices.push_back(SolidVertex{b, nB, cB, packedMaterial});
            m_solidVertices.push_back(SolidVertex{c, nC, cC, packedMaterial});
        }
    };

    if (!mesh.indices.empty())
    {
        for (std::size_t i = 0; i + 2 < mesh.indices.size(); i += 3)
        {
            emitTri(mesh.indices[i], mesh.indices[i + 1], mesh.indices[i + 2]);
        }
    }
    else
    {
        for (std::uint32_t i = 0; i + 2 < static_cast<std::uint32_t>(mesh.positions.size()); i += 3)
        {
            emitTri(i, i + 1, i + 2);
        }
    }
}

void Renderer::DrawTexturedMesh(
    const MeshGeometry& mesh,
    const glm::vec3& position,
    const glm::vec3& rotationEulerDegrees,
    const glm::vec3& scale,
    const glm::vec3& color,
    const MaterialParams& material,
    unsigned int textureId
)
{
    if (textureId == 0 || mesh.positions.empty())
    {
        DrawMesh(mesh, position, rotationEulerDegrees, scale, color, material);
        return;
    }

    const glm::mat3 rotation = RotationMatrixFromEulerDegrees(rotationEulerDegrees);

    auto transformPos = [&](const glm::vec3& p) {
        return position + rotation * (p * scale);
    };
    auto transformNormal = [&](const glm::vec3& n) {
        glm::vec3 out = rotation * n;
        if (glm::length(out) > 1.0e-6F)
        {
            out = glm::normalize(out);
        }
        return out;
    };

    const std::size_t firstVertex = m_texturedVertices.size();
    auto emitTri = [&](std::uint32_t ia, std::uint32_t ib, std::uint32_t ic) {
        if (ia >= mesh.positions.size() || ib >= mesh.positions.size() || ic >= mesh.positions.size())
        {
            return;
        }

        const glm::vec3 a = transformPos(mesh.positions[ia]);
        const glm::vec3 b = transformPos(mesh.positions[ib]);
        const glm::vec3 c = transformPos(mesh.positions[ic]);

        glm::vec3 fallbackNormal = glm::cross(b - a, c - a);
        if (glm::length(fallbackNormal) > 1.0e-6F)
        {
            fallbackNormal = glm::normalize(fallbackNormal);
        }
        else
        {
            fallbackNormal = glm::vec3{0.0F, 1.0F, 0.0F};
        }
        const glm::vec3 nA = ia < mesh.normals.size() ? transformNormal(mesh.normals[ia]) : fallbackNormal;
        const glm::vec3 nB = ib < mesh.normals.size() ? transformNormal(mesh.normals[ib]) : fallbackNormal;
        const glm::vec3 nC = ic < mesh.normals.size() ? transformNormal(mesh.normals[ic]) : fallbackNormal;

        const glm::vec3 cA = glm::clamp(ia < mesh.colors.size() ? color * mesh.colors[ia] : color, glm::vec3{0.0F}, glm::vec3{1.0F});
        const glm::vec3 cB = glm::clamp(ib < mesh.colors.size() ? color * mesh.colors[ib] : color, glm::vec3{0.0F}, glm::vec3{1.0F});
        const glm::vec3 cC = glm::clamp(ic < mesh.colors.size() ? color * mesh.colors[ic] : color, glm::vec3{0.0F}, glm::vec3{1.0F});

        const glm::vec2 uvA = ia < mesh.uvs.size() ? mesh.uvs[ia] : glm::vec2{0.0F};
        const glm::vec2 uvB = ib < mesh.uvs.size() ? mesh.uvs[ib] : glm::vec2{0.0F};
        const glm::vec2 uvC = ic < mesh.uvs.size() ? mesh.uvs[ic] : glm::vec2{0.0F};

        if (m_renderMode == RenderMode::Wireframe)
        {
            const glm::vec3 wireColor = glm::clamp((cA + cB + cC) * (1.0F / 3.0F), glm::vec3{0.0F}, glm::vec3{1.0F});
            DrawLine(a, b, wireColor);
            DrawLine(b, c, wireColor);
            DrawLine(c, a, wireColor);
            return;
        }

        const glm::vec4 packedMaterial{
            glm::clamp(material.roughness, 0.0F, 1.0F),
            glm::clamp(material.metallic, 0.0F, 1.0F),
            glm::max(0.0F, material.emissive),
            material.unlit ? 1.0F : 0.0F,
        };
        m_texturedVertices.push_back(TexturedVertex{a, nA, cA, uvA, packedMaterial});
        m_texturedVertices.push_back(TexturedVertex{b, nB, cB, uvB, packedMaterial});
        m_texturedVertices.push_back(TexturedVertex{c, nC, cC, uvC, packedMaterial});
    };

    if (!mesh.indices.empty())
    {
        for (std::size_t i = 0; i + 2 < mesh.indices.size(); i += 3)
        {
            emitTri(mesh.indices[i], mesh.indices[i + 1], mesh.indices[i + 2]);
        }
    }
    else
    {
        for (std::uint32_t i = 0; i + 2 < static_cast<std::uint32_t>(mesh.positions.size()); i += 3)
        {
            emitTri(i, i + 1, i + 2);
        }
    }

    const std::size_t endVertex = m_texturedVertices.size();
    if (endVertex > firstVertex)
    {
        const std::size_t addedVertices = endVertex - firstVertex;
        if (!m_texturedBatches.empty())
        {
            TexturedBatch& lastBatch = m_texturedBatches.back();
            const bool contiguous = (lastBatch.firstVertex + lastBatch.vertexCount) == firstVertex;
            if (lastBatch.textureId == textureId && contiguous)
            {
                lastBatch.vertexCount += addedVertices;
                return;
            }
        }

        m_texturedBatches.push_back(TexturedBatch{textureId, firstVertex, addedVertices});
    }
}

void Renderer::DrawGrid(int halfSize, float step, const glm::vec3& majorColor, const glm::vec3& minorColor, const glm::vec4& filledColor)
{
    const float range = static_cast<float>(halfSize) * step;
    
    if (m_renderMode == RenderMode::Filled && filledColor.a > 0.0F)
    {
        AddSolidBox(
            glm::vec3{0.0F, -0.01F, 0.0F},
            glm::vec3{range, 0.005F, range},
            glm::vec3{filledColor.r, filledColor.g, filledColor.b},
            MaterialParams{}
        );
    }
    
    // Always draw grid lines
    for (int i = -halfSize; i <= halfSize; ++i)
    {
        const float value = static_cast<float>(i) * step;
        const bool major = (i % 5) == 0;
        const glm::vec3 color = major ? majorColor : minorColor;

        DrawLine(glm::vec3{-range, 0.0F, value}, glm::vec3{range, 0.0F, value}, color);
        DrawLine(glm::vec3{value, 0.0F, -range}, glm::vec3{value, 0.0F, range}, color);
    }
}

void Renderer::DrawCircle(
    const glm::vec3& center,
    float radius,
    int segments,
    const glm::vec3& color,
    bool overlay
)
{
    if (segments < 3 || radius <= 0.0F)
    {
        return;
    }

    auto addLine = [&](const glm::vec3& a, const glm::vec3& b) {
        if (overlay)
        {
            m_overlayLineVertices.push_back(LineVertex{a, color});
            m_overlayLineVertices.push_back(LineVertex{b, color});
        }
        else
        {
            m_lineVertices.push_back(LineVertex{a, color});
            m_lineVertices.push_back(LineVertex{b, color});
        }
    };

    constexpr float kTwoPi = 6.28318530718F;
    const float step = kTwoPi / static_cast<float>(segments);
    
    glm::vec3 prev = center + glm::vec3{radius, 0.0F, 0.0F};
    for (int i = 1; i <= segments; ++i)
    {
        const float angle = step * static_cast<float>(i);
        const glm::vec3 curr = center + glm::vec3{std::cos(angle) * radius, 0.0F, std::sin(angle) * radius};
        addLine(prev, curr);
        prev = curr;
    }
}

void Renderer::DrawBillboards(
    const BillboardData* billboards,
    std::size_t count,
    const glm::vec3& cameraPosition
)
{
    if (billboards == nullptr || count == 0)
    {
        return;
    }

    for (std::size_t i = 0; i < count; ++i)
    {
        const BillboardData& b = billboards[i];
        const glm::vec3 camDir = cameraPosition - b.position;
        const float dist = glm::length(camDir);
        if (dist < 0.01F)
        {
            continue;
        }

        const glm::vec3 forward = camDir / dist;
        glm::vec3 localRight = glm::normalize(glm::cross(glm::vec3{0.0F, 1.0F, 0.0F}, forward));
        if (glm::length(localRight) < 0.1F)
        {
            localRight = glm::vec3{1.0F, 0.0F, 0.0F};
        }
        const glm::vec3 localUp = glm::normalize(glm::cross(forward, localRight));

        const float halfSize = b.size * 0.5F;
        const glm::vec3 cornerLU = b.position - localRight * halfSize + localUp * halfSize;
        const glm::vec3 cornerRU = b.position + localRight * halfSize + localUp * halfSize;
        const glm::vec3 cornerLD = b.position - localRight * halfSize - localUp * halfSize;
        const glm::vec3 cornerRD = b.position + localRight * halfSize - localUp * halfSize;

        const glm::vec3 normal = forward;
        const glm::vec3 color = glm::vec3{b.color.r, b.color.g, b.color.b};
        const glm::vec4 material{0.55F, 0.0F, 0.0F, 0.0F};

        m_solidVertices.push_back(SolidVertex{cornerLD, normal, color, material});
        m_solidVertices.push_back(SolidVertex{cornerRU, normal, color, material});
        m_solidVertices.push_back(SolidVertex{cornerLU, normal, color, material});
        m_solidVertices.push_back(SolidVertex{cornerLD, normal, color, material});
        m_solidVertices.push_back(SolidVertex{cornerRD, normal, color, material});
        m_solidVertices.push_back(SolidVertex{cornerRU, normal, color, material});
    }
}

// ─── GPU Mesh Cache ─────────────────────────────────────────────────────────

Renderer::GpuMeshId Renderer::UploadMesh(const MeshGeometry& mesh, const glm::vec3& color, const MaterialParams& material)
{
    if (mesh.positions.empty())
    {
        return kInvalidGpuMesh;
    }

    // Build vertex data in object space (no transform applied).
    const glm::vec4 packedMaterial{
        glm::clamp(material.roughness, 0.0F, 1.0F),
        glm::clamp(material.metallic, 0.0F, 1.0F),
        glm::max(0.0F, material.emissive),
        material.unlit ? 1.0F : 0.0F,
    };

    std::vector<SolidVertex> vertices;

    auto emitTri = [&](std::uint32_t ia, std::uint32_t ib, std::uint32_t ic) {
        if (ia >= mesh.positions.size() || ib >= mesh.positions.size() || ic >= mesh.positions.size())
        {
            return;
        }
        const glm::vec3& a = mesh.positions[ia];
        const glm::vec3& b = mesh.positions[ib];
        const glm::vec3& c = mesh.positions[ic];

        glm::vec3 fallbackNormal = glm::cross(b - a, c - a);
        if (glm::length(fallbackNormal) > 1.0e-6F)
        {
            fallbackNormal = glm::normalize(fallbackNormal);
        }
        else
        {
            fallbackNormal = glm::vec3{0.0F, 1.0F, 0.0F};
        }
        const glm::vec3 nA = ia < mesh.normals.size() ? mesh.normals[ia] : fallbackNormal;
        const glm::vec3 nB = ib < mesh.normals.size() ? mesh.normals[ib] : fallbackNormal;
        const glm::vec3 nC = ic < mesh.normals.size() ? mesh.normals[ic] : fallbackNormal;
        const glm::vec3 cA = glm::clamp(ia < mesh.colors.size() ? color * mesh.colors[ia] : color, glm::vec3{0.0F}, glm::vec3{1.0F});
        const glm::vec3 cB = glm::clamp(ib < mesh.colors.size() ? color * mesh.colors[ib] : color, glm::vec3{0.0F}, glm::vec3{1.0F});
        const glm::vec3 cC = glm::clamp(ic < mesh.colors.size() ? color * mesh.colors[ic] : color, glm::vec3{0.0F}, glm::vec3{1.0F});

        vertices.push_back(SolidVertex{a, nA, cA, packedMaterial});
        vertices.push_back(SolidVertex{b, nB, cB, packedMaterial});
        vertices.push_back(SolidVertex{c, nC, cC, packedMaterial});
    };

    if (!mesh.indices.empty())
    {
        vertices.reserve(mesh.indices.size());
        for (std::size_t i = 0; i + 2 < mesh.indices.size(); i += 3)
        {
            emitTri(mesh.indices[i], mesh.indices[i + 1], mesh.indices[i + 2]);
        }
    }
    else
    {
        vertices.reserve(mesh.positions.size());
        for (std::uint32_t i = 0; i + 2 < static_cast<std::uint32_t>(mesh.positions.size()); i += 3)
        {
            emitTri(i, i + 1, i + 2);
        }
    }

    if (vertices.empty())
    {
        return kInvalidGpuMesh;
    }

    unsigned int vao = 0;
    unsigned int vbo = 0;
    glGenVertexArrays(1, &vao);
    glGenBuffers(1, &vbo);

    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(vertices.size() * sizeof(SolidVertex)), vertices.data(), GL_STATIC_DRAW);

    constexpr GLsizei stride = sizeof(SolidVertex);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(offsetof(SolidVertex, position)));
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(offsetof(SolidVertex, normal)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(offsetof(SolidVertex, color)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(offsetof(SolidVertex, material)));
    glEnableVertexAttribArray(3);

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);

    const GpuMeshId id = m_nextGpuMeshId++;
    m_gpuMeshes[id] = GpuMeshInfo{vao, vbo, static_cast<std::uint32_t>(vertices.size())};
    return id;
}

void Renderer::DrawGpuMesh(GpuMeshId id, const glm::mat4& modelMatrix)
{
    if (id == kInvalidGpuMesh)
    {
        return;
    }
    m_gpuMeshDraws.push_back(GpuMeshDraw{id, modelMatrix});
}

void Renderer::FreeGpuMesh(GpuMeshId id)
{
    if (id == kInvalidGpuMesh)
    {
        return;
    }
    const auto it = m_gpuMeshes.find(id);
    if (it == m_gpuMeshes.end())
    {
        return;
    }
    if (it->second.vbo != 0)
    {
        glDeleteBuffers(1, &it->second.vbo);
    }
    if (it->second.vao != 0)
    {
        glDeleteVertexArrays(1, &it->second.vao);
    }
    m_gpuMeshes.erase(it);
}

void Renderer::FreeAllGpuMeshes()
{
    for (auto& [id, info] : m_gpuMeshes)
    {
        if (info.vbo != 0)
        {
            glDeleteBuffers(1, &info.vbo);
        }
        if (info.vao != 0)
        {
            glDeleteVertexArrays(1, &info.vao);
        }
    }
    m_gpuMeshes.clear();
}

unsigned int Renderer::CompileShader(unsigned int type, const char* source)
{
    const unsigned int shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);

    int success = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (success == GL_FALSE)
    {
        int logLength = 0;
        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &logLength);
        std::string log(static_cast<size_t>(logLength), '\0');
        glGetShaderInfoLog(shader, logLength, nullptr, log.data());
        std::cerr << "Shader compile error: " << log << "\n";
        glDeleteShader(shader);
        return 0;
    }

    return shader;
}

unsigned int Renderer::CreateProgram(const char* vertexSource, const char* fragmentSource)
{
    const unsigned int vertexShader = CompileShader(GL_VERTEX_SHADER, vertexSource);
    const unsigned int fragmentShader = CompileShader(GL_FRAGMENT_SHADER, fragmentSource);
    if (vertexShader == 0 || fragmentShader == 0)
    {
        if (vertexShader != 0)
        {
            glDeleteShader(vertexShader);
        }
        if (fragmentShader != 0)
        {
            glDeleteShader(fragmentShader);
        }
        return 0;
    }

    const unsigned int program = glCreateProgram();
    glAttachShader(program, vertexShader);
    glAttachShader(program, fragmentShader);
    glLinkProgram(program);

    int success = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &success);
    if (success == GL_FALSE)
    {
        int logLength = 0;
        glGetProgramiv(program, GL_INFO_LOG_LENGTH, &logLength);
        std::string log(static_cast<size_t>(logLength), '\0');
        glGetProgramInfoLog(program, logLength, nullptr, log.data());
        std::cerr << "Program link error: " << log << "\n";

        glDeleteProgram(program);
        glDeleteShader(vertexShader);
        glDeleteShader(fragmentShader);
        return 0;
    }

    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);
    return program;
}

void Renderer::AddWireBox(const glm::vec3& center, const glm::vec3& halfExtents, const glm::vec3& color)
{
    const std::array<glm::vec3, 8> corners = {
        center + glm::vec3{-halfExtents.x, -halfExtents.y, -halfExtents.z},
        center + glm::vec3{+halfExtents.x, -halfExtents.y, -halfExtents.z},
        center + glm::vec3{+halfExtents.x, -halfExtents.y, +halfExtents.z},
        center + glm::vec3{-halfExtents.x, -halfExtents.y, +halfExtents.z},
        center + glm::vec3{-halfExtents.x, +halfExtents.y, -halfExtents.z},
        center + glm::vec3{+halfExtents.x, +halfExtents.y, -halfExtents.z},
        center + glm::vec3{+halfExtents.x, +halfExtents.y, +halfExtents.z},
        center + glm::vec3{-halfExtents.x, +halfExtents.y, +halfExtents.z},
    };

    auto edge = [&](int a, int b) { DrawLine(corners[static_cast<size_t>(a)], corners[static_cast<size_t>(b)], color); };

    edge(0, 1); edge(1, 2); edge(2, 3); edge(3, 0);
    edge(4, 5); edge(5, 6); edge(6, 7); edge(7, 4);
    edge(0, 4); edge(1, 5); edge(2, 6); edge(3, 7);
}

void Renderer::AddSolidBox(
    const glm::vec3& center,
    const glm::vec3& halfExtents,
    const glm::vec3& color,
    const MaterialParams& material
)
{
    const glm::vec3 c0 = center + glm::vec3{-halfExtents.x, -halfExtents.y, -halfExtents.z};
    const glm::vec3 c1 = center + glm::vec3{+halfExtents.x, -halfExtents.y, -halfExtents.z};
    const glm::vec3 c2 = center + glm::vec3{+halfExtents.x, -halfExtents.y, +halfExtents.z};
    const glm::vec3 c3 = center + glm::vec3{-halfExtents.x, -halfExtents.y, +halfExtents.z};
    const glm::vec3 c4 = center + glm::vec3{-halfExtents.x, +halfExtents.y, -halfExtents.z};
    const glm::vec3 c5 = center + glm::vec3{+halfExtents.x, +halfExtents.y, -halfExtents.z};
    const glm::vec3 c6 = center + glm::vec3{+halfExtents.x, +halfExtents.y, +halfExtents.z};
    const glm::vec3 c7 = center + glm::vec3{-halfExtents.x, +halfExtents.y, +halfExtents.z};

    const glm::vec4 mat{
        glm::clamp(material.roughness, 0.0F, 1.0F),
        glm::clamp(material.metallic, 0.0F, 1.0F),
        glm::max(0.0F, material.emissive),
        material.unlit ? 1.0F : 0.0F,
    };

    // Pre-size to avoid internal reallocations.
    auto& v = m_solidVertices;

    // Bottom  (normal: 0,-1,0)
    const glm::vec3 nBot{0,-1,0};
    v.push_back({c0,nBot,color,mat}); v.push_back({c2,nBot,color,mat}); v.push_back({c1,nBot,color,mat});
    v.push_back({c0,nBot,color,mat}); v.push_back({c3,nBot,color,mat}); v.push_back({c2,nBot,color,mat});
    // Top     (normal: 0,+1,0)
    const glm::vec3 nTop{0,1,0};
    v.push_back({c4,nTop,color,mat}); v.push_back({c5,nTop,color,mat}); v.push_back({c6,nTop,color,mat});
    v.push_back({c4,nTop,color,mat}); v.push_back({c6,nTop,color,mat}); v.push_back({c7,nTop,color,mat});
    // Front   (normal: 0,0,-1)
    const glm::vec3 nFront{0,0,-1};
    v.push_back({c0,nFront,color,mat}); v.push_back({c1,nFront,color,mat}); v.push_back({c5,nFront,color,mat});
    v.push_back({c0,nFront,color,mat}); v.push_back({c5,nFront,color,mat}); v.push_back({c4,nFront,color,mat});
    // Back    (normal: 0,0,+1)
    const glm::vec3 nBack{0,0,1};
    v.push_back({c3,nBack,color,mat}); v.push_back({c7,nBack,color,mat}); v.push_back({c6,nBack,color,mat});
    v.push_back({c3,nBack,color,mat}); v.push_back({c6,nBack,color,mat}); v.push_back({c2,nBack,color,mat});
    // Left    (normal: -1,0,0)
    const glm::vec3 nLeft{-1,0,0};
    v.push_back({c0,nLeft,color,mat}); v.push_back({c4,nLeft,color,mat}); v.push_back({c7,nLeft,color,mat});
    v.push_back({c0,nLeft,color,mat}); v.push_back({c7,nLeft,color,mat}); v.push_back({c3,nLeft,color,mat});
    // Right   (normal: +1,0,0)
    const glm::vec3 nRight{1,0,0};
    v.push_back({c1,nRight,color,mat}); v.push_back({c2,nRight,color,mat}); v.push_back({c6,nRight,color,mat});
    v.push_back({c1,nRight,color,mat}); v.push_back({c6,nRight,color,mat}); v.push_back({c5,nRight,color,mat});
}

void Renderer::AddWireOrientedBox(
    const glm::vec3& center,
    const glm::vec3& halfExtents,
    const glm::vec3& rotationEulerDegrees,
    const glm::vec3& color
)
{
    const glm::mat3 rotation = RotationMatrixFromEulerDegrees(rotationEulerDegrees);
    const std::array<glm::vec3, 8> localCorners = {
        glm::vec3{-halfExtents.x, -halfExtents.y, -halfExtents.z},
        glm::vec3{+halfExtents.x, -halfExtents.y, -halfExtents.z},
        glm::vec3{+halfExtents.x, -halfExtents.y, +halfExtents.z},
        glm::vec3{-halfExtents.x, -halfExtents.y, +halfExtents.z},
        glm::vec3{-halfExtents.x, +halfExtents.y, -halfExtents.z},
        glm::vec3{+halfExtents.x, +halfExtents.y, -halfExtents.z},
        glm::vec3{+halfExtents.x, +halfExtents.y, +halfExtents.z},
        glm::vec3{-halfExtents.x, +halfExtents.y, +halfExtents.z},
    };

    std::array<glm::vec3, 8> corners{};
    for (std::size_t i = 0; i < localCorners.size(); ++i)
    {
        corners[i] = center + rotation * localCorners[i];
    }

    auto edge = [&](int a, int b) { DrawLine(corners[static_cast<std::size_t>(a)], corners[static_cast<std::size_t>(b)], color); };
    edge(0, 1); edge(1, 2); edge(2, 3); edge(3, 0);
    edge(4, 5); edge(5, 6); edge(6, 7); edge(7, 4);
    edge(0, 4); edge(1, 5); edge(2, 6); edge(3, 7);
}

void Renderer::AddSolidOrientedBox(
    const glm::vec3& center,
    const glm::vec3& halfExtents,
    const glm::vec3& rotationEulerDegrees,
    const glm::vec3& color,
    const MaterialParams& material
)
{
    const glm::mat3 rotation = RotationMatrixFromEulerDegrees(rotationEulerDegrees);
    const glm::vec3 c0 = center + rotation * glm::vec3{-halfExtents.x, -halfExtents.y, -halfExtents.z};
    const glm::vec3 c1 = center + rotation * glm::vec3{+halfExtents.x, -halfExtents.y, -halfExtents.z};
    const glm::vec3 c2 = center + rotation * glm::vec3{+halfExtents.x, -halfExtents.y, +halfExtents.z};
    const glm::vec3 c3 = center + rotation * glm::vec3{-halfExtents.x, -halfExtents.y, +halfExtents.z};
    const glm::vec3 c4 = center + rotation * glm::vec3{-halfExtents.x, +halfExtents.y, -halfExtents.z};
    const glm::vec3 c5 = center + rotation * glm::vec3{+halfExtents.x, +halfExtents.y, -halfExtents.z};
    const glm::vec3 c6 = center + rotation * glm::vec3{+halfExtents.x, +halfExtents.y, +halfExtents.z};
    const glm::vec3 c7 = center + rotation * glm::vec3{-halfExtents.x, +halfExtents.y, +halfExtents.z};

    const glm::vec4 mat{
        glm::clamp(material.roughness, 0.0F, 1.0F),
        glm::clamp(material.metallic, 0.0F, 1.0F),
        glm::max(0.0F, material.emissive),
        material.unlit ? 1.0F : 0.0F,
    };

    // Rotated face normals (cheaper than per-tri cross product).
    const glm::vec3 nBot   = rotation * glm::vec3{0,-1, 0};
    const glm::vec3 nTop   = rotation * glm::vec3{0, 1, 0};
    const glm::vec3 nFront = rotation * glm::vec3{0, 0,-1};
    const glm::vec3 nBack  = rotation * glm::vec3{0, 0, 1};
    const glm::vec3 nLeft  = rotation * glm::vec3{-1,0, 0};
    const glm::vec3 nRight = rotation * glm::vec3{1, 0, 0};

    auto& v = m_solidVertices;
    v.push_back({c0,nBot,color,mat});   v.push_back({c2,nBot,color,mat});   v.push_back({c1,nBot,color,mat});
    v.push_back({c0,nBot,color,mat});   v.push_back({c3,nBot,color,mat});   v.push_back({c2,nBot,color,mat});
    v.push_back({c4,nTop,color,mat});   v.push_back({c5,nTop,color,mat});   v.push_back({c6,nTop,color,mat});
    v.push_back({c4,nTop,color,mat});   v.push_back({c6,nTop,color,mat});   v.push_back({c7,nTop,color,mat});
    v.push_back({c0,nFront,color,mat}); v.push_back({c1,nFront,color,mat}); v.push_back({c5,nFront,color,mat});
    v.push_back({c0,nFront,color,mat}); v.push_back({c5,nFront,color,mat}); v.push_back({c4,nFront,color,mat});
    v.push_back({c3,nBack,color,mat});  v.push_back({c7,nBack,color,mat});  v.push_back({c6,nBack,color,mat});
    v.push_back({c3,nBack,color,mat});  v.push_back({c6,nBack,color,mat});  v.push_back({c2,nBack,color,mat});
    v.push_back({c0,nLeft,color,mat});  v.push_back({c4,nLeft,color,mat});  v.push_back({c7,nLeft,color,mat});
    v.push_back({c0,nLeft,color,mat});  v.push_back({c7,nLeft,color,mat});  v.push_back({c3,nLeft,color,mat});
    v.push_back({c1,nRight,color,mat}); v.push_back({c2,nRight,color,mat}); v.push_back({c6,nRight,color,mat});
    v.push_back({c1,nRight,color,mat}); v.push_back({c6,nRight,color,mat}); v.push_back({c5,nRight,color,mat});
}

void Renderer::AddWireCapsule(const glm::vec3& center, float height, float radius, const glm::vec3& color)
{
    constexpr int segments = 16;
    const float halfCylinder = std::max(0.0F, height * 0.5F - radius);
    const glm::vec3 topCenter = center + glm::vec3{0.0F, halfCylinder, 0.0F};
    const glm::vec3 bottomCenter = center - glm::vec3{0.0F, halfCylinder, 0.0F};

    for (int i = 0; i < segments; ++i)
    {
        const float t0 = static_cast<float>(i) / static_cast<float>(segments) * 6.2831853F;
        const float t1 = static_cast<float>(i + 1) / static_cast<float>(segments) * 6.2831853F;

        const glm::vec3 ring0{std::cos(t0) * radius, 0.0F, std::sin(t0) * radius};
        const glm::vec3 ring1{std::cos(t1) * radius, 0.0F, std::sin(t1) * radius};

        DrawLine(topCenter + ring0, topCenter + ring1, color);
        DrawLine(bottomCenter + ring0, bottomCenter + ring1, color);
        DrawLine(topCenter + ring0, bottomCenter + ring0, color);
    }
}

void Renderer::AddSolidCapsule(
    const glm::vec3& center,
    float height,
    float radius,
    const glm::vec3& color,
    const MaterialParams& material,
    int segments,
    int hemiRings
)
{
    const float halfCylinder = std::max(0.0F, height * 0.5F - radius);
    const glm::vec3 topCenter = center + glm::vec3{0.0F, halfCylinder, 0.0F};
    const glm::vec3 bottomCenter = center - glm::vec3{0.0F, halfCylinder, 0.0F};

    const glm::vec4 mat{
        glm::clamp(material.roughness, 0.0F, 1.0F),
        glm::clamp(material.metallic, 0.0F, 1.0F),
        glm::max(0.0F, material.emissive),
        material.unlit ? 1.0F : 0.0F,
    };

    auto& v = m_solidVertices;
    const float twoPi = 6.2831853F;
    const float halfPi = 1.5707963F;

    // Pre-compute sin/cos per segment.
    const int maxSegs = std::min(segments, 64);
    float cosTable[65], sinTable[65]; // +1 for wrap
    for (int i = 0; i <= maxSegs; ++i)
    {
        const float t = static_cast<float>(i) / static_cast<float>(maxSegs) * twoPi;
        cosTable[i] = std::cos(t);
        sinTable[i] = std::sin(t);
    }

    // Cylinder body — normals point radially outward (no Y component).
    for (int i = 0; i < maxSegs; ++i)
    {
        const glm::vec3 ring0{cosTable[i] * radius, 0.0F, sinTable[i] * radius};
        const glm::vec3 ring1{cosTable[i + 1] * radius, 0.0F, sinTable[i + 1] * radius};
        const glm::vec3 n0 = glm::vec3{cosTable[i], 0.0F, sinTable[i]};
        const glm::vec3 n1 = glm::vec3{cosTable[i + 1], 0.0F, sinTable[i + 1]};

        const glm::vec3 b0 = bottomCenter + ring0;
        const glm::vec3 b1 = bottomCenter + ring1;
        const glm::vec3 t0 = topCenter + ring0;
        const glm::vec3 t1 = topCenter + ring1;

        v.push_back({b0, n0, color, mat}); v.push_back({t0, n0, color, mat}); v.push_back({t1, n1, color, mat});
        v.push_back({b0, n0, color, mat}); v.push_back({t1, n1, color, mat}); v.push_back({b1, n1, color, mat});
    }

    // Hemisphere with analytically-computed normals.
    auto addHemisphere = [&](const glm::vec3& hemiCenter, float ySign) {
        for (int ring = 0; ring < hemiRings; ++ring)
        {
            const float phi0 = static_cast<float>(ring) / static_cast<float>(hemiRings) * halfPi;
            const float phi1 = static_cast<float>(ring + 1) / static_cast<float>(hemiRings) * halfPi;
            const float r0 = std::cos(phi0) * radius;
            const float r1 = std::cos(phi1) * radius;
            const float y0 = std::sin(phi0) * radius * ySign;
            const float y1 = std::sin(phi1) * radius * ySign;
            const float ny0 = std::sin(phi0) * ySign;
            const float ny1 = std::sin(phi1) * ySign;
            const float nr0 = std::cos(phi0);
            const float nr1 = std::cos(phi1);

            for (int i = 0; i < maxSegs; ++i)
            {
                const glm::vec3 v00 = hemiCenter + glm::vec3{cosTable[i] * r0, y0, sinTable[i] * r0};
                const glm::vec3 v01 = hemiCenter + glm::vec3{cosTable[i + 1] * r0, y0, sinTable[i + 1] * r0};
                const glm::vec3 v10 = hemiCenter + glm::vec3{cosTable[i] * r1, y1, sinTable[i] * r1};
                const glm::vec3 v11 = hemiCenter + glm::vec3{cosTable[i + 1] * r1, y1, sinTable[i + 1] * r1};

                const glm::vec3 n00{cosTable[i] * nr0, ny0, sinTable[i] * nr0};
                const glm::vec3 n01{cosTable[i + 1] * nr0, ny0, sinTable[i + 1] * nr0};
                const glm::vec3 n10{cosTable[i] * nr1, ny1, sinTable[i] * nr1};
                const glm::vec3 n11{cosTable[i + 1] * nr1, ny1, sinTable[i + 1] * nr1};

                if (ySign > 0.0F)
                {
                    v.push_back({v00, n00, color, mat}); v.push_back({v10, n10, color, mat}); v.push_back({v11, n11, color, mat});
                    v.push_back({v00, n00, color, mat}); v.push_back({v11, n11, color, mat}); v.push_back({v01, n01, color, mat});
                }
                else
                {
                    v.push_back({v00, n00, color, mat}); v.push_back({v11, n11, color, mat}); v.push_back({v10, n10, color, mat});
                    v.push_back({v00, n00, color, mat}); v.push_back({v01, n01, color, mat}); v.push_back({v11, n11, color, mat});
                }
            }
        }
    };

    addHemisphere(topCenter, 1.0F);
    addHemisphere(bottomCenter, -1.0F);
}

void Renderer::AddSolidTriangle(
    const glm::vec3& a,
    const glm::vec3& b,
    const glm::vec3& c,
    const glm::vec3& color,
    const MaterialParams& material
)
{
    glm::vec3 normal = glm::cross(b - a, c - a);
    if (glm::length(normal) <= 1.0e-8F)
    {
        normal = glm::vec3{0.0F, 1.0F, 0.0F};
    }
    else
    {
        normal = -glm::normalize(normal);
    }

    const glm::vec4 packedMaterial{
        glm::clamp(material.roughness, 0.0F, 1.0F),
        glm::clamp(material.metallic, 0.0F, 1.0F),
        glm::max(0.0F, material.emissive),
        material.unlit ? 1.0F : 0.0F,
    };

    m_solidVertices.push_back(SolidVertex{a, normal, color, packedMaterial});
    m_solidVertices.push_back(SolidVertex{b, normal, color, packedMaterial});
    m_solidVertices.push_back(SolidVertex{c, normal, color, packedMaterial});
}
} // namespace engine::render
