#include "WraithCloakRenderer.hpp"
#include <glm/gtc/type_ptr.hpp>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <ctime>

namespace engine::render
{

constexpr const char* kWraithCloakVertexShader = R"(#version 330 core
layout(location = 0) in vec3 aPosition;

uniform mat4 uViewProj;
uniform mat4 uModel;
uniform float uCapsuleHeight;
uniform float uCapsuleRadius;

out vec3 vWorldPos;
out vec3 vLocalPos;
out vec3 vNormal;

void main()
{
    vec3 pos = aPosition;
    
    // Capsule geometry: scale by radius and height
    float scaleY = uCapsuleHeight * 0.5;
    pos.x *= uCapsuleRadius;
    pos.y *= scaleY;
    pos.z *= uCapsuleRadius;
    
    vec4 worldPos = uModel * vec4(pos, 1.0);
    vWorldPos = worldPos.xyz;
    vLocalPos = pos;
    
    // Approximate normal from local position (for capsule)
    vec3 n = normalize(pos);
    if (abs(n.y) > 0.9 && length(pos.xz) < 0.1) {
        n = vec3(0.0, sign(pos.y), 0.0);
    }
    vNormal = mat3(uModel) * n;
    
    gl_Position = uViewProj * worldPos;
}
)";

constexpr const char* kWraithCloakFragmentShader = R"(#version 330 core
in vec3 vWorldPos;
in vec3 vLocalPos;
in vec3 vNormal;

uniform sampler2D uSceneColor;
uniform sampler2D uNoiseTex;
uniform sampler2D uDistortTex;
uniform vec2 uScreenSize;
uniform vec3 uCameraPos;
uniform vec3 uWraithPos;
uniform float uTime;
uniform float uCloakAmount;
uniform float uRimStrength;
uniform float uRimPower;
uniform float uDistortStrength;
uniform float uNoiseScale;
uniform float uNoiseSpeed;
uniform float uBaseCloakOpacity;
uniform float uTransitionWidth;

out vec4 FragColor;

// 4x4 Bayer matrix for dithered transparency
float bayer4x4(vec2 screenPos)
{
    int x = int(mod(screenPos.x, 4.0));
    int y = int(mod(screenPos.y, 4.0));
    int index = x + y * 4;
    
    float threshold[16] = float[16](
        0.0625, 0.5625, 0.1875, 0.6875,
        0.8125, 0.3125, 0.9375, 0.4375,
        0.25,   0.75,   0.125,  0.625,
        1.0,    0.5,    0.875,  0.375
    );
    return threshold[index];
}

void main()
{
    vec3 N = normalize(vNormal);
    vec3 V = normalize(uCameraPos - vWorldPos);
    vec3 P = vWorldPos;
    
    // Fresnel rim lighting
    float fresnel = pow(1.0 - max(dot(N, V), 0.0), uRimPower);
    fresnel *= uRimStrength;
    
    // Screen-space UV
    vec4 clipPos = gl_FragCoord;
    vec2 screenUV = clipPos.xy / uScreenSize;
    
    // Animated noise for distortion and transition
    vec2 noiseUV = (P.xz + P.y) * uNoiseScale * 0.1 + vec2(uTime * uNoiseSpeed);
    float noise = texture(uNoiseTex, noiseUV).r;
    
    // Distortion from normal map
    vec2 distortSample = texture(uDistortTex, noiseUV * 2.0).rg * 2.0 - 1.0;
    vec2 distortedUV = screenUV + distortSample * uDistortStrength * uCloakAmount;
    
    // Sample refracted background
    vec3 sceneColor = texture(uSceneColor, distortedUV).rgb;
    
    // Transition mask with noise breakup
    float transitionNoise = noise * 2.0 - 0.5;
    float t = uCloakAmount;
    float mask = smoothstep(t - uTransitionWidth, t + uTransitionWidth, transitionNoise + 0.5);
    
    // Base albedo (faint bright silhouette)
    vec3 baseAlbedo = vec3(0.08, 0.10, 0.12);
    
    // Rim tint (subtle foggy blue, brighter)
    vec3 rimTint = vec3(0.35, 0.45, 0.60) * fresnel * 0.4;
    
    // Refracted scene with rim enhancement
    vec3 refractedColor = sceneColor + rimTint * 0.5;
    
    // Mix based on transition mask - favor scene heavily for translucency
    vec3 finalColor = mix(baseAlbedo, refractedColor, mask * 0.7);
    
    // Alpha calculation for dithering - very low for translucency
    float cloakAlpha = mix(1.0, uBaseCloakOpacity + fresnel * 0.08, mask);
    
    // Dithered transparency (Option A)
    float threshold = bayer4x4(gl_FragCoord.xy);
    if (cloakAlpha < threshold)
    {
        discard;
    }
    
    // Add subtle watery shimmer
    float shimmer = sin(uTime * 3.0 + noise * 10.0) * 0.03 * uCloakAmount;
    finalColor += shimmer * vec3(0.3, 0.4, 0.5);
    
    // Blend heavily with scene for spaced translucent feel
    finalColor = mix(finalColor, sceneColor, 0.75 + fresnel * 0.15);
    
    FragColor = vec4(finalColor, 1.0);
}
)";

bool WraithCloakRenderer::Initialize()
{
    if (m_initialized)
    {
        return true;
    }

    if (!CreateShader())
    {
        return false;
    }

    if (!CreateTextures())
    {
        return false;
    }

    if (!CreateSceneTexture())
    {
        return false;
    }

    // Create capsule geometry VAO/VBO/EBO (unit capsule, scaled in shader)
    glGenVertexArrays(1, &m_vao);
    glGenBuffers(1, &m_vbo);
    glGenBuffers(1, &m_ebo);
    glBindVertexArray(m_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);

    // Create a simple capsule mesh (sphere approximation using UV sphere)
    std::vector<float> vertices;
    std::vector<unsigned int> indices;
    const int segments = 24;
    const int rings = 12;
    
    for (int ring = 0; ring <= rings; ++ring)
    {
        const float phi = 3.14159f * float(ring) / float(rings);
        const float y = std::cos(phi);
        const float ringRadius = std::sin(phi);
        
        for (int seg = 0; seg <= segments; ++seg)
        {
            const float theta = 2.0f * 3.14159f * float(seg) / float(segments);
            const float x = ringRadius * std::cos(theta);
            const float z = ringRadius * std::sin(theta);
            
            vertices.push_back(x);
            vertices.push_back(y);
            vertices.push_back(z);
        }
    }

    // Generate triangle indices
    for (int ring = 0; ring < rings; ++ring)
    {
        for (int seg = 0; seg < segments; ++seg)
        {
            const int current = ring * (segments + 1) + seg;
            const int next = current + segments + 1;
            
            indices.push_back(current);
            indices.push_back(next);
            indices.push_back(current + 1);
            
            indices.push_back(current + 1);
            indices.push_back(next);
            indices.push_back(next + 1);
        }
    }

    glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(float), vertices.data(), GL_STATIC_DRAW);
    
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(unsigned int), indices.data(), GL_STATIC_DRAW);
    
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
    glBindVertexArray(0);
    
    m_indexCount = static_cast<int>(indices.size());

    m_initialized = true;
    std::printf("WraithCloakRenderer: Initialized successfully\n");
    return true;
}

void WraithCloakRenderer::Shutdown()
{
    if (m_program != 0)
    {
        glDeleteProgram(m_program);
        m_program = 0;
    }
    if (m_vao != 0)
    {
        glDeleteVertexArrays(1, &m_vao);
        m_vao = 0;
    }
    if (m_vbo != 0)
    {
        glDeleteBuffers(1, &m_vbo);
        m_vbo = 0;
    }
    if (m_ebo != 0)
    {
        glDeleteBuffers(1, &m_ebo);
        m_ebo = 0;
    }
    if (m_noiseTex != 0)
    {
        glDeleteTextures(1, &m_noiseTex);
        m_noiseTex = 0;
    }
    if (m_distortTex != 0)
    {
        glDeleteTextures(1, &m_distortTex);
        m_distortTex = 0;
    }
    if (m_sceneTex != 0)
    {
        glDeleteTextures(1, &m_sceneTex);
        m_sceneTex = 0;
    }
    m_initialized = false;
    m_indexCount = 0;
}

bool WraithCloakRenderer::CreateShader()
{
    const GLuint vs = CompileShader(GL_VERTEX_SHADER, kWraithCloakVertexShader);
    if (vs == 0)
    {
        return false;
    }

    const GLuint fs = CompileShader(GL_FRAGMENT_SHADER, kWraithCloakFragmentShader);
    if (fs == 0)
    {
        glDeleteShader(vs);
        return false;
    }

    m_program = LinkProgram(vs, fs);
    glDeleteShader(vs);
    glDeleteShader(fs);

    if (m_program == 0)
    {
        return false;
    }

    // Cache uniform locations
    m_locViewProj = glGetUniformLocation(m_program, "uViewProj");
    m_locModel = glGetUniformLocation(m_program, "uModel");
    m_locCameraPos = glGetUniformLocation(m_program, "uCameraPos");
    m_locWraithPos = glGetUniformLocation(m_program, "uWraithPos");
    m_locCapsuleHeight = glGetUniformLocation(m_program, "uCapsuleHeight");
    m_locCapsuleRadius = glGetUniformLocation(m_program, "uCapsuleRadius");
    m_locScreenSize = glGetUniformLocation(m_program, "uScreenSize");
    m_locTime = glGetUniformLocation(m_program, "uTime");
    m_locCloakAmount = glGetUniformLocation(m_program, "uCloakAmount");
    m_locRimStrength = glGetUniformLocation(m_program, "uRimStrength");
    m_locRimPower = glGetUniformLocation(m_program, "uRimPower");
    m_locDistortStrength = glGetUniformLocation(m_program, "uDistortStrength");
    m_locNoiseScale = glGetUniformLocation(m_program, "uNoiseScale");
    m_locNoiseSpeed = glGetUniformLocation(m_program, "uNoiseSpeed");
    m_locBaseCloakOpacity = glGetUniformLocation(m_program, "uBaseCloakOpacity");
    m_locTransitionWidth = glGetUniformLocation(m_program, "uTransitionWidth");
    m_locSceneColor = glGetUniformLocation(m_program, "uSceneColor");
    m_locNoiseTex = glGetUniformLocation(m_program, "uNoiseTex");
    m_locDistortTex = glGetUniformLocation(m_program, "uDistortTex");

    return true;
}

GLuint WraithCloakRenderer::CompileShader(GLenum type, const char* source) const
{
    const GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);

    GLint success = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success)
    {
        char log[512];
        glGetShaderInfoLog(shader, 512, nullptr, log);
        std::printf("WraithCloakRenderer: Shader compile error: %s\n", log);
        glDeleteShader(shader);
        return 0;
    }

    return shader;
}

GLuint WraithCloakRenderer::LinkProgram(GLuint vs, GLuint fs) const
{
    const GLuint program = glCreateProgram();
    glAttachShader(program, vs);
    glAttachShader(program, fs);
    glLinkProgram(program);

    GLint success = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &success);
    if (!success)
    {
        char log[512];
        glGetProgramInfoLog(program, 512, nullptr, log);
        std::printf("WraithCloakRenderer: Program link error: %s\n", log);
        glDeleteProgram(program);
        return 0;
    }

    return program;
}

bool WraithCloakRenderer::CreateTextures()
{
    GenerateNoiseTexture();
    GenerateDistortTexture();
    return m_noiseTex != 0 && m_distortTex != 0;
}

void WraithCloakRenderer::GenerateNoiseTexture()
{
    const int size = 256;
    std::vector<unsigned char> data(size * size);

    std::srand(static_cast<unsigned int>(std::time(nullptr)));
    for (int i = 0; i < size * size; ++i)
    {
        data[i] = static_cast<unsigned char>(std::rand() % 256);
    }

    glGenTextures(1, &m_noiseTex);
    glBindTexture(GL_TEXTURE_2D, m_noiseTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, size, size, 0, GL_RED, GL_UNSIGNED_BYTE, data.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
}

void WraithCloakRenderer::GenerateDistortTexture()
{
    const int size = 256;
    std::vector<unsigned char> data(size * size * 2);

    std::srand(static_cast<unsigned int>(std::time(nullptr) + 12345));
    for (int y = 0; y < size; ++y)
    {
        for (int x = 0; x < size; ++x)
        {
            const int idx = (y * size + x) * 2;
            // Generate smooth noise-like distortion vectors
            const float fx = static_cast<float>(x) / size;
            const float fy = static_cast<float>(y) / size;
            const float angle = std::sin(fx * 10.0f) * std::cos(fy * 10.0f) * 3.14159f;
            data[idx] = static_cast<unsigned char>(128 + 127 * std::cos(angle));
            data[idx + 1] = static_cast<unsigned char>(128 + 127 * std::sin(angle));
        }
    }

    glGenTextures(1, &m_distortTex);
    glBindTexture(GL_TEXTURE_2D, m_distortTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RG8, size, size, 0, GL_RG, GL_UNSIGNED_BYTE, data.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
}

bool WraithCloakRenderer::CreateSceneTexture()
{
    if (m_screenW <= 0 || m_screenH <= 0)
    {
        m_screenW = 1920;
        m_screenH = 1080;
    }

    glGenTextures(1, &m_sceneTex);
    glBindTexture(GL_TEXTURE_2D, m_sceneTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, m_screenW, m_screenH, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);

    return m_sceneTex != 0;
}

void WraithCloakRenderer::CaptureBackbuffer()
{
    if (m_sceneTex == 0 || m_screenW <= 0 || m_screenH <= 0)
    {
        return;
    }

    glBindTexture(GL_TEXTURE_2D, m_sceneTex);
    glCopyTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 0, 0, m_screenW, m_screenH, 0);
    glBindTexture(GL_TEXTURE_2D, 0);
}

void WraithCloakRenderer::SetScreenSize(int w, int h)
{
    m_screenW = w;
    m_screenH = h;

    // Recreate scene texture if size changed
    if (m_sceneTex != 0)
    {
        glDeleteTextures(1, &m_sceneTex);
        m_sceneTex = 0;
    }
    CreateSceneTexture();
}

void WraithCloakRenderer::Render(const glm::mat4& viewProj, const glm::mat4& model, const glm::vec3& cameraPos, const glm::vec3& wraithPos, float capsuleHeight, float capsuleRadius, const WraithCloakParams& params)
{
    if (!m_initialized || params.cloakAmount <= 0.001F)
    {
        return;
    }

    glUseProgram(m_program);

    // Matrices
    glUniformMatrix4fv(m_locViewProj, 1, GL_FALSE, glm::value_ptr(viewProj));
    glUniformMatrix4fv(m_locModel, 1, GL_FALSE, glm::value_ptr(model));

    // Vectors
    glUniform3fv(m_locCameraPos, 1, glm::value_ptr(cameraPos));
    glUniform3fv(m_locWraithPos, 1, glm::value_ptr(wraithPos));

    // Capsule dimensions
    glUniform1f(m_locCapsuleHeight, capsuleHeight);
    glUniform1f(m_locCapsuleRadius, capsuleRadius);

    // Screen size
    glUniform2f(m_locScreenSize, static_cast<float>(m_screenW), static_cast<float>(m_screenH));

    // Time and cloak parameters
    glUniform1f(m_locTime, params.time);
    glUniform1f(m_locCloakAmount, params.cloakAmount);
    glUniform1f(m_locRimStrength, params.rimStrength);
    glUniform1f(m_locRimPower, params.rimPower);
    glUniform1f(m_locDistortStrength, params.distortStrength);
    glUniform1f(m_locNoiseScale, params.noiseScale);
    glUniform1f(m_locNoiseSpeed, params.noiseSpeed);
    glUniform1f(m_locBaseCloakOpacity, params.baseCloakOpacity);
    glUniform1f(m_locTransitionWidth, params.transitionWidth);

    // Bind textures
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_sceneTex);
    glUniform1i(m_locSceneColor, 0);

    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, m_noiseTex);
    glUniform1i(m_locNoiseTex, 1);

    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, m_distortTex);
    glUniform1i(m_locDistortTex, 2);

    // Render states for dithered alpha (Option A)
    glDisable(GL_BLEND);
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);

    // Draw capsule with triangles
    glBindVertexArray(m_vao);
    glDrawElements(GL_TRIANGLES, m_indexCount, GL_UNSIGNED_INT, 0);

    glBindVertexArray(0);
    glUseProgram(0);
}

}
