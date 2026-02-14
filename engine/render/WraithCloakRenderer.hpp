#ifndef ENGINE_RENDER_WRAITHCLOAKRENDERER_HPP
#define ENGINE_RENDER_WRAITHCLOAKRENDERER_HPP

#include "SceneCaptureFBO.hpp"
#include <glm/glm.hpp>
#include <glad/glad.h>
#include <vector>

namespace engine::render
{

struct WraithCloakParams
{
    float cloakAmount = 0.0F;
    float rimStrength = 0.28F;
    float rimPower = 4.5F;
    float distortStrength = 0.010F;
    float noiseScale = 3.0F;
    float noiseSpeed = 0.25F;
    float baseCloakOpacity = 0.02F;
    float transitionWidth = 0.20F;
    float time = 0.0F;
};

class WraithCloakRenderer
{
public:
    WraithCloakRenderer() = default;
    ~WraithCloakRenderer() { Shutdown(); }

    bool Initialize();
    void Shutdown();
    void CaptureBackbuffer();
    void Render(const glm::mat4& viewProj, const glm::mat4& model, const glm::vec3& cameraPos, const glm::vec3& wraithPos, float capsuleHeight, float capsuleRadius, const WraithCloakParams& params);

    void SetScreenSize(int w, int h);

    [[nodiscard]] GLuint NoiseTexture() const { return m_noiseTex; }
    [[nodiscard]] GLuint DistortTexture() const { return m_distortTex; }
    [[nodiscard]] GLuint SceneTexture() const { return m_sceneTex; }
    [[nodiscard]] bool IsInitialized() const { return m_initialized; }

private:
    bool CreateShader();
    bool CreateTextures();
    bool CreateSceneTexture();
    GLuint CompileShader(GLenum type, const char* source) const;
    GLuint LinkProgram(GLuint vs, GLuint fs) const;
    void GenerateNoiseTexture();
    void GenerateDistortTexture();

    GLuint m_program = 0;
    GLuint m_vao = 0;
    GLuint m_vbo = 0;
    GLuint m_ebo = 0;
    GLuint m_noiseTex = 0;
    GLuint m_distortTex = 0;
    GLuint m_sceneTex = 0;
    bool m_initialized = false;
    int m_screenW = 0;
    int m_screenH = 0;
    int m_indexCount = 0;

    GLint m_locViewProj = -1;
    GLint m_locModel = -1;
    GLint m_locCameraPos = -1;
    GLint m_locWraithPos = -1;
    GLint m_locCapsuleHeight = -1;
    GLint m_locCapsuleRadius = -1;
    GLint m_locScreenSize = -1;
    GLint m_locTime = -1;
    GLint m_locCloakAmount = -1;
    GLint m_locRimStrength = -1;
    GLint m_locRimPower = -1;
    GLint m_locDistortStrength = -1;
    GLint m_locNoiseScale = -1;
    GLint m_locNoiseSpeed = -1;
    GLint m_locBaseCloakOpacity = -1;
    GLint m_locTransitionWidth = -1;
    GLint m_locSceneColor = -1;
    GLint m_locNoiseTex = -1;
    GLint m_locDistortTex = -1;
};

}

#endif
