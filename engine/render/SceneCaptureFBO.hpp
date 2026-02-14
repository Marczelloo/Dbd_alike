#ifndef ENGINE_RENDER_SCENECAPTUREFBO_HPP
#define ENGINE_RENDER_SCENECAPTUREFBO_HPP

#include <glad/glad.h>

namespace engine::render
{

struct SceneCaptureFBO
{
    GLuint fbo = 0;
    GLuint colorTex = 0;
    GLuint depthTex = 0;
    int width = 0;
    int height = 0;
    bool valid = false;

    void Create(int w, int h);
    void Destroy();
    void Resize(int w, int h);
    void Bind() const;
    void Unbind() const;
    void BlitToScreen(int screenW, int screenH) const;

    [[nodiscard]] bool IsValid() const { return valid; }
    [[nodiscard]] int Width() const { return width; }
    [[nodiscard]] int Height() const { return height; }
    [[nodiscard]] GLuint ColorTexture() const { return colorTex; }
    [[nodiscard]] GLuint DepthTexture() const { return depthTex; }

private:
    void CreateTextures(int w, int h);
    void AttachTextures();
};

}

#endif
