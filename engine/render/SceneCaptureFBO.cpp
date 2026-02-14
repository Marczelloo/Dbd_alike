#include "SceneCaptureFBO.hpp"
#include <cstdio>

namespace engine::render
{

void SceneCaptureFBO::Create(int w, int h)
{
    if (valid)
    {
        Destroy();
    }

    width = w;
    height = h;

    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);

    CreateTextures(w, h);
    AttachTextures();

    static const GLenum drawBuffers[] = {GL_COLOR_ATTACHMENT0};
    glDrawBuffers(1, drawBuffers);

    const GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE)
    {
        std::printf("SceneCaptureFBO: Framebuffer incomplete! Status: 0x%X\n", status);
        valid = false;
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        return;
    }

    valid = true;
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    std::printf("SceneCaptureFBO: Created %dx%d (FBO=%u, Color=%u, Depth=%u)\n", w, h, fbo, colorTex, depthTex);
}

void SceneCaptureFBO::CreateTextures(int w, int h)
{
    glGenTextures(1, &colorTex);
    glBindTexture(GL_TEXTURE_2D, colorTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, w, h, 0, GL_RGBA, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glGenTextures(1, &depthTex);
    glBindTexture(GL_TEXTURE_2D, depthTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, w, h, 0, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}

void SceneCaptureFBO::AttachTextures()
{
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, colorTex, 0);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, depthTex, 0);
}

void SceneCaptureFBO::Destroy()
{
    if (colorTex != 0)
    {
        glDeleteTextures(1, &colorTex);
        colorTex = 0;
    }
    if (depthTex != 0)
    {
        glDeleteTextures(1, &depthTex);
        depthTex = 0;
    }
    if (fbo != 0)
    {
        glDeleteFramebuffers(1, &fbo);
        fbo = 0;
    }
    valid = false;
    width = 0;
    height = 0;
}

void SceneCaptureFBO::Resize(int w, int h)
{
    if (w == width && h == height && valid)
    {
        return;
    }

    if (fbo != 0)
    {
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        
        if (colorTex != 0)
        {
            glDeleteTextures(1, &colorTex);
            colorTex = 0;
        }
        if (depthTex != 0)
        {
            glDeleteTextures(1, &depthTex);
            depthTex = 0;
        }

        CreateTextures(w, h);
        AttachTextures();

        width = w;
        height = h;

        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }
    else
    {
        Create(w, h);
    }
}

void SceneCaptureFBO::Bind() const
{
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glViewport(0, 0, width, height);
}

void SceneCaptureFBO::Unbind() const
{
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void SceneCaptureFBO::BlitToScreen(int screenW, int screenH) const
{
    glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    glBlitFramebuffer(0, 0, width, height, 0, 0, screenW, screenH, GL_COLOR_BUFFER_BIT, GL_LINEAR);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
}

}
