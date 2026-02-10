#include "engine/render/Renderer.hpp"

#include <array>
#include <cmath>
#include <iostream>

#include <glad/glad.h>

#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

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

uniform mat4 uViewProjection;

out vec3 vNormal;
out vec3 vColor;
out vec3 vWorldPos;

void main()
{
    vNormal = aNormal;
    vColor = aColor;
    vWorldPos = aPosition;
    gl_Position = uViewProjection * vec4(aPosition, 1.0);
}
)";

constexpr const char* kSolidFragmentShader = R"(
#version 450 core
in vec3 vNormal;
in vec3 vColor;
in vec3 vWorldPos;
out vec4 FragColor;

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
    vec3 n = normalize(vNormal);
    vec3 lightDir = normalize(uLightDir);
    float lambert = max(dot(n, lightDir), 0.0);
    vec3 lit = vColor * (0.15 + (0.85 * lambert * uLightIntensity));
    lit *= mix(vec3(1.0), uLightColor, 0.65);

    for (int i = 0; i < uPointLightCount; ++i)
    {
        vec3 toLight = uPointLightPosRange[i].xyz - vWorldPos;
        float dist = length(toLight);
        float range = max(0.001, uPointLightPosRange[i].w);
        if (dist < range)
        {
            vec3 l = normalize(toLight);
            float ndotl = max(dot(n, l), 0.0);
            float attenuation = 1.0 - (dist / range);
            attenuation *= attenuation;
            vec3 lightColor = uPointLightColorIntensity[i].rgb;
            float intensity = uPointLightColorIntensity[i].a;
            lit += vColor * lightColor * (ndotl * attenuation * intensity * 0.85);
        }
    }

    for (int i = 0; i < uSpotLightCount; ++i)
    {
        vec3 toLight = uSpotLightPosRange[i].xyz - vWorldPos;
        float dist = length(toLight);
        float range = max(0.001, uSpotLightPosRange[i].w);
        if (dist < range)
        {
            vec3 l = normalize(toLight);
            vec3 fromLightToFrag = normalize(-toLight);
            vec3 spotDir = normalize(uSpotLightDirInnerCos[i].xyz);
            float cosTheta = dot(fromLightToFrag, spotDir);
            float innerCos = uSpotLightDirInnerCos[i].w;
            float outerCos = uSpotLightOuterCos[i];
            float cone = smoothstep(outerCos, innerCos, cosTheta);
            float ndotl = max(dot(n, l), 0.0);
            float attenuation = 1.0 - (dist / range);
            attenuation *= attenuation;
            vec3 lightColor = uSpotLightColorIntensity[i].rgb;
            float intensity = uSpotLightColorIntensity[i].a;
            lit += vColor * lightColor * (ndotl * attenuation * cone * intensity * 0.95);
        }
    }

    lit = max(lit, vColor * 0.08);

    if (uFogEnabled != 0)
    {
        float dist = length(vWorldPos.xz);
        float linear = clamp((dist - uFogStart) / max(0.001, uFogEnd - uFogStart), 0.0, 1.0);
        float expFog = 1.0 - exp(-uFogDensity * dist);
        float fogAmount = clamp(max(linear, expFog), 0.0, 1.0);
        lit = mix(lit, uFogColor, fogAmount);
    }

    FragColor = vec4(lit, 1.0);
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

    m_lineProgram = CreateProgram(kLineVertexShader, kLineFragmentShader);
    m_solidProgram = CreateProgram(kSolidVertexShader, kSolidFragmentShader);
    if (m_lineProgram == 0 || m_solidProgram == 0)
    {
        return false;
    }

    glGenVertexArrays(1, &m_lineVao);
    glGenBuffers(1, &m_lineVbo);

    glBindVertexArray(m_lineVao);
    glBindBuffer(GL_ARRAY_BUFFER, m_lineVbo);
    glBufferData(GL_ARRAY_BUFFER, 2 * 1024 * 1024, nullptr, GL_DYNAMIC_DRAW);

    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(LineVertex), reinterpret_cast<void*>(offsetof(LineVertex, position)));
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(LineVertex), reinterpret_cast<void*>(offsetof(LineVertex, color)));
    glEnableVertexAttribArray(1);

    glGenVertexArrays(1, &m_solidVao);
    glGenBuffers(1, &m_solidVbo);

    glBindVertexArray(m_solidVao);
    glBindBuffer(GL_ARRAY_BUFFER, m_solidVbo);
    glBufferData(GL_ARRAY_BUFFER, 4 * 1024 * 1024, nullptr, GL_DYNAMIC_DRAW);

    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(SolidVertex), reinterpret_cast<void*>(offsetof(SolidVertex, position)));
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(SolidVertex), reinterpret_cast<void*>(offsetof(SolidVertex, normal)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, sizeof(SolidVertex), reinterpret_cast<void*>(offsetof(SolidVertex, color)));
    glEnableVertexAttribArray(2);

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);

    m_lineViewProjLocation = glGetUniformLocation(m_lineProgram, "uViewProjection");
    m_solidViewProjLocation = glGetUniformLocation(m_solidProgram, "uViewProjection");
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

    SetViewport(framebufferWidth, framebufferHeight);
    return true;
}

void Renderer::Shutdown()
{
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

void Renderer::SetSpotLights(const std::vector<SpotLight>& lights)
{
    m_spotLights = lights;
    if (m_spotLights.size() > static_cast<std::size_t>(kMaxSpotLights))
    {
        m_spotLights.resize(static_cast<std::size_t>(kMaxSpotLights));
    }
}

void Renderer::BeginFrame(const glm::vec3& clearColor)
{
    m_lineVertices.clear();
    m_overlayLineVertices.clear();
    m_solidVertices.clear();

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

    glClearColor(finalClear.r, finalClear.g, finalClear.b, 1.0F);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

void Renderer::EndFrame(const glm::mat4& viewProjection)
{
    if (!m_solidVertices.empty())
    {
        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

        glUseProgram(m_solidProgram);
        glUniformMatrix4fv(m_solidViewProjLocation, 1, GL_FALSE, glm::value_ptr(viewProjection));
        glUniform3fv(m_solidLightDirLocation, 1, glm::value_ptr(glm::normalize(m_environment.directionalLightDirection)));
        glUniform3fv(m_solidLightColorLocation, 1, glm::value_ptr(m_environment.directionalLightColor));
        glUniform1f(m_solidLightIntensityLocation, m_environment.directionalLightIntensity);
        glUniform1i(m_solidFogEnabledLocation, m_environment.fogEnabled ? 1 : 0);
        glUniform3fv(m_solidFogColorLocation, 1, glm::value_ptr(m_environment.fogColor));
        glUniform1f(m_solidFogDensityLocation, m_environment.fogDensity);
        glUniform1f(m_solidFogStartLocation, m_environment.fogStart);
        glUniform1f(m_solidFogEndLocation, m_environment.fogEnd);

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
        glUniform1i(m_solidPointLightCountLocation, pointCount);
        glUniform4fv(m_solidPointLightPosRangeLocation, kMaxPointLights, glm::value_ptr(pointPosRange[0]));
        glUniform4fv(m_solidPointLightColorIntensityLocation, kMaxPointLights, glm::value_ptr(pointColorIntensity[0]));

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
                glm::vec4{
                    light.color.x,
                    light.color.y,
                    light.color.z,
                    glm::max(0.0F, light.intensity),
                };
            spotOuterCos[static_cast<std::size_t>(i)] = glm::clamp(light.outerCos, -1.0F, 1.0F);
        }
        glUniform1i(m_solidSpotLightCountLocation, spotCount);
        glUniform4fv(m_solidSpotLightPosRangeLocation, kMaxSpotLights, glm::value_ptr(spotPosRange[0]));
        glUniform4fv(m_solidSpotLightDirInnerCosLocation, kMaxSpotLights, glm::value_ptr(spotDirInnerCos[0]));
        glUniform4fv(m_solidSpotLightColorIntensityLocation, kMaxSpotLights, glm::value_ptr(spotColorIntensity[0]));
        glUniform1fv(m_solidSpotLightOuterCosLocation, kMaxSpotLights, spotOuterCos.data());

        glBindVertexArray(m_solidVao);
        glBindBuffer(GL_ARRAY_BUFFER, m_solidVbo);
        glBufferSubData(GL_ARRAY_BUFFER, 0, static_cast<GLsizeiptr>(m_solidVertices.size() * sizeof(SolidVertex)), m_solidVertices.data());

        glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(m_solidVertices.size()));
    }

    if (!m_lineVertices.empty())
    {
        glUseProgram(m_lineProgram);
        glUniformMatrix4fv(m_lineViewProjLocation, 1, GL_FALSE, glm::value_ptr(viewProjection));

        glBindVertexArray(m_lineVao);
        glBindBuffer(GL_ARRAY_BUFFER, m_lineVbo);
        glBufferSubData(GL_ARRAY_BUFFER, 0, static_cast<GLsizeiptr>(m_lineVertices.size() * sizeof(LineVertex)), m_lineVertices.data());

        glDrawArrays(GL_LINES, 0, static_cast<GLsizei>(m_lineVertices.size()));
    }

    if (!m_overlayLineVertices.empty())
    {
        glDisable(GL_DEPTH_TEST);
        glUseProgram(m_lineProgram);
        glUniformMatrix4fv(m_lineViewProjLocation, 1, GL_FALSE, glm::value_ptr(viewProjection));

        glBindVertexArray(m_lineVao);
        glBindBuffer(GL_ARRAY_BUFFER, m_lineVbo);
        glBufferSubData(GL_ARRAY_BUFFER, 0, static_cast<GLsizeiptr>(m_overlayLineVertices.size() * sizeof(LineVertex)), m_overlayLineVertices.data());

        glDrawArrays(GL_LINES, 0, static_cast<GLsizei>(m_overlayLineVertices.size()));
        glEnable(GL_DEPTH_TEST);
    }

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);
    glUseProgram(0);
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

void Renderer::DrawBox(const glm::vec3& center, const glm::vec3& halfExtents, const glm::vec3& color)
{
    if (m_renderMode == RenderMode::Wireframe)
    {
        AddWireBox(center, halfExtents, color);
    }
    else
    {
        AddSolidBox(center, halfExtents, color);
    }
}

void Renderer::DrawOrientedBox(
    const glm::vec3& center,
    const glm::vec3& halfExtents,
    const glm::vec3& rotationEulerDegrees,
    const glm::vec3& color
)
{
    if (m_renderMode == RenderMode::Wireframe)
    {
        AddWireOrientedBox(center, halfExtents, rotationEulerDegrees, color);
    }
    else
    {
        AddSolidOrientedBox(center, halfExtents, rotationEulerDegrees, color);
    }
}

void Renderer::DrawCapsule(const glm::vec3& center, float height, float radius, const glm::vec3& color)
{
    if (m_renderMode == RenderMode::Wireframe)
    {
        AddWireCapsule(center, height, radius, color);
    }
    else
    {
        AddSolidCapsule(center, height, radius, color);
    }
}

void Renderer::DrawMesh(
    const MeshGeometry& mesh,
    const glm::vec3& position,
    const glm::vec3& rotationEulerDegrees,
    const glm::vec3& scale,
    const glm::vec3& color
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

        glm::vec3 n{0.0F, 1.0F, 0.0F};
        if (ia < mesh.normals.size())
        {
            n = transformNormal(mesh.normals[ia]);
        }
        else
        {
            glm::vec3 fallback = glm::cross(b - a, c - a);
            if (glm::length(fallback) > 1.0e-6F)
            {
                n = glm::normalize(fallback);
            }
        }

        if (m_renderMode == RenderMode::Wireframe)
        {
            DrawLine(a, b, color);
            DrawLine(b, c, color);
            DrawLine(c, a, color);
        }
        else
        {
            m_solidVertices.push_back(SolidVertex{a, n, color});
            m_solidVertices.push_back(SolidVertex{b, n, color});
            m_solidVertices.push_back(SolidVertex{c, n, color});
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

void Renderer::DrawGrid(int halfSize, float step, const glm::vec3& majorColor, const glm::vec3& minorColor)
{
    const float range = static_cast<float>(halfSize) * step;
    for (int i = -halfSize; i <= halfSize; ++i)
    {
        const float value = static_cast<float>(i) * step;
        const bool major = (i % 5) == 0;
        const glm::vec3 color = major ? majorColor : minorColor;

        DrawLine(glm::vec3{-range, 0.0F, value}, glm::vec3{range, 0.0F, value}, color);
        DrawLine(glm::vec3{value, 0.0F, -range}, glm::vec3{value, 0.0F, range}, color);
    }
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

void Renderer::AddSolidBox(const glm::vec3& center, const glm::vec3& halfExtents, const glm::vec3& color)
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

    const auto tri = [&](int a, int b, int c) {
        AddSolidTriangle(corners[static_cast<size_t>(a)], corners[static_cast<size_t>(b)], corners[static_cast<size_t>(c)], color);
    };

    tri(0, 2, 1); tri(0, 3, 2);
    tri(4, 5, 6); tri(4, 6, 7);
    tri(0, 1, 5); tri(0, 5, 4);
    tri(3, 7, 6); tri(3, 6, 2);
    tri(0, 4, 7); tri(0, 7, 3);
    tri(1, 2, 6); tri(1, 6, 5);
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

    const auto tri = [&](int a, int b, int c) {
        AddSolidTriangle(corners[static_cast<std::size_t>(a)], corners[static_cast<std::size_t>(b)], corners[static_cast<std::size_t>(c)], color);
    };

    tri(0, 2, 1); tri(0, 3, 2);
    tri(4, 5, 6); tri(4, 6, 7);
    tri(0, 1, 5); tri(0, 5, 4);
    tri(3, 7, 6); tri(3, 6, 2);
    tri(0, 4, 7); tri(0, 7, 3);
    tri(1, 2, 6); tri(1, 6, 5);
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

void Renderer::AddSolidCapsule(const glm::vec3& center, float height, float radius, const glm::vec3& color)
{
    constexpr int segments = 16;
    constexpr int hemiRings = 6;

    const float halfCylinder = std::max(0.0F, height * 0.5F - radius);
    const glm::vec3 topCenter = center + glm::vec3{0.0F, halfCylinder, 0.0F};
    const glm::vec3 bottomCenter = center - glm::vec3{0.0F, halfCylinder, 0.0F};

    for (int i = 0; i < segments; ++i)
    {
        const float t0 = static_cast<float>(i) / static_cast<float>(segments) * 6.2831853F;
        const float t1 = static_cast<float>(i + 1) / static_cast<float>(segments) * 6.2831853F;

        const glm::vec3 ring0{std::cos(t0) * radius, 0.0F, std::sin(t0) * radius};
        const glm::vec3 ring1{std::cos(t1) * radius, 0.0F, std::sin(t1) * radius};

        const glm::vec3 b0 = bottomCenter + ring0;
        const glm::vec3 b1 = bottomCenter + ring1;
        const glm::vec3 t00 = topCenter + ring0;
        const glm::vec3 t11 = topCenter + ring1;

        AddSolidTriangle(b0, t00, t11, color);
        AddSolidTriangle(b0, t11, b1, color);
    }

    auto addHemisphere = [&](const glm::vec3& hemiCenter, float ySign) {
        for (int ring = 0; ring < hemiRings; ++ring)
        {
            const float v0 = static_cast<float>(ring) / static_cast<float>(hemiRings);
            const float v1 = static_cast<float>(ring + 1) / static_cast<float>(hemiRings);

            const float phi0 = v0 * 1.5707963F;
            const float phi1 = v1 * 1.5707963F;

            const float r0 = std::cos(phi0) * radius;
            const float r1 = std::cos(phi1) * radius;
            const float y0 = std::sin(phi0) * radius * ySign;
            const float y1 = std::sin(phi1) * radius * ySign;

            for (int i = 0; i < segments; ++i)
            {
                const float t0 = static_cast<float>(i) / static_cast<float>(segments) * 6.2831853F;
                const float t1 = static_cast<float>(i + 1) / static_cast<float>(segments) * 6.2831853F;

                const glm::vec3 v00 = hemiCenter + glm::vec3{std::cos(t0) * r0, y0, std::sin(t0) * r0};
                const glm::vec3 v01 = hemiCenter + glm::vec3{std::cos(t1) * r0, y0, std::sin(t1) * r0};
                const glm::vec3 v10 = hemiCenter + glm::vec3{std::cos(t0) * r1, y1, std::sin(t0) * r1};
                const glm::vec3 v11 = hemiCenter + glm::vec3{std::cos(t1) * r1, y1, std::sin(t1) * r1};

                if (ySign > 0.0F)
                {
                    AddSolidTriangle(v00, v10, v11, color);
                    AddSolidTriangle(v00, v11, v01, color);
                }
                else
                {
                    AddSolidTriangle(v00, v11, v10, color);
                    AddSolidTriangle(v00, v01, v11, color);
                }
            }
        }
    };

    addHemisphere(topCenter, 1.0F);
    addHemisphere(bottomCenter, -1.0F);
}

void Renderer::AddSolidTriangle(const glm::vec3& a, const glm::vec3& b, const glm::vec3& c, const glm::vec3& color)
{
    glm::vec3 normal = glm::cross(b - a, c - a);
    if (glm::length(normal) <= 1.0e-8F)
    {
        normal = glm::vec3{0.0F, 1.0F, 0.0F};
    }
    else
    {
        normal = glm::normalize(normal);
    }

    m_solidVertices.push_back(SolidVertex{a, normal, color});
    m_solidVertices.push_back(SolidVertex{b, normal, color});
    m_solidVertices.push_back(SolidVertex{c, normal, color});
}
} // namespace engine::render
