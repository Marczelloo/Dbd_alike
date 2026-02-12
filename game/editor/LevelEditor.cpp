#include "game/editor/LevelEditor.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <limits>
#include <iostream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <glm/common.hpp>
#include <glm/ext/scalar_constants.hpp>
#include <glm/geometric.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/trigonometric.hpp>
#include <nlohmann/json.hpp>

#if BUILD_WITH_IMGUI
#include <glad/glad.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <stb_image.h>
#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <commdlg.h>
#endif
#endif

namespace game::editor
{
using json = nlohmann::json;
namespace
{
float ClampPitch(float value)
{
    return glm::clamp(value, -1.5F, 1.5F);
}

glm::vec3 RotateY(const glm::vec3& value, float degrees)
{
    const float radians = glm::radians(degrees);
    const float c = std::cos(radians);
    const float s = std::sin(radians);
    return glm::vec3{
        value.x * c - value.z * s,
        value.y,
        value.x * s + value.z * c,
    };
}

glm::mat3 RotationMatrixFromEulerDegrees(const glm::vec3& eulerDegrees)
{
    glm::mat4 transform{1.0F};
    transform = glm::rotate(transform, glm::radians(eulerDegrees.y), glm::vec3{0.0F, 1.0F, 0.0F});
    transform = glm::rotate(transform, glm::radians(eulerDegrees.x), glm::vec3{1.0F, 0.0F, 0.0F});
    transform = glm::rotate(transform, glm::radians(eulerDegrees.z), glm::vec3{0.0F, 0.0F, 1.0F});
    return glm::mat3(transform);
}

glm::vec3 RotateExtentsXYZ(const glm::vec3& halfExtents, const glm::vec3& eulerDegrees)
{
    const glm::mat3 rotation = RotationMatrixFromEulerDegrees(eulerDegrees);
    const glm::mat3 absRotation{
        glm::abs(rotation[0]),
        glm::abs(rotation[1]),
        glm::abs(rotation[2]),
    };
    return absRotation * halfExtents;
}

glm::ivec2 RotatedFootprintFor(const LoopAsset& loop, int rotationDegrees)
{
    const int rot = ((rotationDegrees % 360) + 360) % 360;
    const bool swap = rot == 90 || rot == 270;
    return glm::ivec2{
        swap ? std::max(1, loop.footprintHeight) : std::max(1, loop.footprintWidth),
        swap ? std::max(1, loop.footprintWidth) : std::max(1, loop.footprintHeight),
    };
}

bool SegmentIntersectsAabb(
    const glm::vec3& origin,
    const glm::vec3& direction,
    const glm::vec3& minBounds,
    const glm::vec3& maxBounds,
    float* outT
)
{
    float tMin = 0.0F;
    float tMax = 10000.0F;

    for (int axis = 0; axis < 3; ++axis)
    {
        if (std::abs(direction[axis]) < 1.0e-7F)
        {
            if (origin[axis] < minBounds[axis] || origin[axis] > maxBounds[axis])
            {
                return false;
            }
            continue;
        }

        const float invDir = 1.0F / direction[axis];
        float t1 = (minBounds[axis] - origin[axis]) * invDir;
        float t2 = (maxBounds[axis] - origin[axis]) * invDir;
        if (t1 > t2)
        {
            std::swap(t1, t2);
        }
        tMin = std::max(tMin, t1);
        tMax = std::min(tMax, t2);
        if (tMin > tMax)
        {
            return false;
        }
    }

    if (outT != nullptr)
    {
        *outT = tMin;
    }
    return true;
}

bool RayIntersectsTriangle(
    const glm::vec3& rayOrigin,
    const glm::vec3& rayDirection,
    const glm::vec3& a,
    const glm::vec3& b,
    const glm::vec3& c,
    float* outT
)
{
    constexpr float epsilon = 1.0e-6F;
    const glm::vec3 edge1 = b - a;
    const glm::vec3 edge2 = c - a;
    const glm::vec3 pvec = glm::cross(rayDirection, edge2);
    const float det = glm::dot(edge1, pvec);
    if (std::abs(det) < epsilon)
    {
        return false;
    }
    const float invDet = 1.0F / det;
    const glm::vec3 tvec = rayOrigin - a;
    const float u = glm::dot(tvec, pvec) * invDet;
    if (u < 0.0F || u > 1.0F)
    {
        return false;
    }
    const glm::vec3 qvec = glm::cross(tvec, edge1);
    const float v = glm::dot(rayDirection, qvec) * invDet;
    if (v < 0.0F || (u + v) > 1.0F)
    {
        return false;
    }
    const float t = glm::dot(edge2, qvec) * invDet;
    if (t < 0.0F)
    {
        return false;
    }
    if (outT != nullptr)
    {
        *outT = t;
    }
    return true;
}

float DistanceRayToSegment(
    const glm::vec3& rayOrigin,
    const glm::vec3& rayDirection,
    const glm::vec3& segA,
    const glm::vec3& segB,
    float* outRayT,
    float* outSegT
)
{
    const glm::vec3 u = glm::normalize(rayDirection);
    const glm::vec3 v = segB - segA;
    const glm::vec3 w0 = rayOrigin - segA;

    const float a = glm::dot(u, u);
    const float b = glm::dot(u, v);
    const float c = glm::dot(v, v);
    const float d = glm::dot(u, w0);
    const float e = glm::dot(v, w0);
    const float denom = a * c - b * b;

    float sc = 0.0F;
    float tc = 0.0F;
    if (denom < 1.0e-8F)
    {
        sc = 0.0F;
        tc = c > 1.0e-8F ? glm::clamp(e / c, 0.0F, 1.0F) : 0.0F;
    }
    else
    {
        sc = (b * e - c * d) / denom;
        tc = (a * e - b * d) / denom;
        if (sc < 0.0F)
        {
            sc = 0.0F;
            tc = c > 1.0e-8F ? glm::clamp(e / c, 0.0F, 1.0F) : 0.0F;
        }
        else
        {
            tc = glm::clamp(tc, 0.0F, 1.0F);
        }
    }

    const glm::vec3 pRay = rayOrigin + u * sc;
    const glm::vec3 pSeg = segA + v * tc;
    if (outRayT != nullptr)
    {
        *outRayT = sc;
    }
    if (outSegT != nullptr)
    {
        *outSegT = tc;
    }
    return glm::length(pRay - pSeg);
}

const char* ModeToText(LevelEditor::Mode mode)
{
    return mode == LevelEditor::Mode::LoopEditor ? "Loop Editor" : "Map Editor";
}

const char* GizmoToText(LevelEditor::GizmoMode mode)
{
    switch (mode)
    {
        case LevelEditor::GizmoMode::Translate: return "Translate";
        case LevelEditor::GizmoMode::Rotate: return "Rotate";
        case LevelEditor::GizmoMode::Scale: return "Scale";
        default: return "Translate";
    }
}

const char* PropToText(PropType type)
{
    switch (type)
    {
        case PropType::Rock: return "Rock";
        case PropType::Tree: return "Tree";
        case PropType::Obstacle: return "Obstacle";
        case PropType::Platform: return "Platform";
        case PropType::MeshAsset: return "MeshAsset";
        default: return "Rock";
    }
}

const char* LightTypeToText(LightType type)
{
    switch (type)
    {
        case LightType::Spot: return "Spot";
        case LightType::Point:
        default: return "Point";
    }
}

const char* LoopElementTypeToText(LoopElementType type)
{
    switch (type)
    {
        case LoopElementType::Wall: return "Wall";
        case LoopElementType::Window: return "Window";
        case LoopElementType::Pallet: return "Pallet";
        case LoopElementType::Marker: return "Marker";
        default: return "Wall";
    }
}

std::string QuickLoopAssetId(LoopElementType type)
{
    switch (type)
    {
        case LoopElementType::Wall: return "__quick_loop_wall";
        case LoopElementType::Window: return "__quick_loop_window";
        case LoopElementType::Pallet: return "__quick_loop_pallet";
        case LoopElementType::Marker: return "__quick_loop_marker";
        default: return "__quick_loop_wall";
    }
}

glm::vec3 QuickLoopDefaultHalfExtents(LoopElementType type)
{
    switch (type)
    {
        case LoopElementType::Wall: return glm::vec3{2.5F, 1.1F, 0.25F};
        case LoopElementType::Window: return glm::vec3{1.1F, 1.0F, 0.20F};
        case LoopElementType::Pallet: return glm::vec3{1.25F, 0.85F, 0.25F};
        case LoopElementType::Marker: return glm::vec3{0.25F, 0.25F, 0.25F};
        default: return glm::vec3{1.0F, 1.0F, 0.2F};
    }
}

glm::vec3 ElementRotation(const LoopElement& element)
{
    return glm::vec3{element.pitchDegrees, element.yawDegrees, element.rollDegrees};
}

glm::vec3 PropRotation(const PropInstance& prop)
{
    return glm::vec3{prop.pitchDegrees, prop.yawDegrees, prop.rollDegrees};
}

const char* RenderModeToText(engine::render::RenderMode mode)
{
    return mode == engine::render::RenderMode::Wireframe ? "Wireframe" : "Filled";
}

const char* MaterialLabViewModeToText(LevelEditor::MaterialLabViewMode mode)
{
    switch (mode)
    {
        case LevelEditor::MaterialLabViewMode::Overlay: return "Overlay";
        case LevelEditor::MaterialLabViewMode::Dedicated: return "Dedicated";
        case LevelEditor::MaterialLabViewMode::Off:
        default: return "Off";
    }
}

engine::render::MeshGeometry BuildUvSphereGeometry(int latSegments, int lonSegments)
{
    engine::render::MeshGeometry mesh{};
    latSegments = std::max(6, latSegments);
    lonSegments = std::max(8, lonSegments);

    mesh.positions.reserve(static_cast<std::size_t>((latSegments + 1) * (lonSegments + 1)));
    mesh.normals.reserve(static_cast<std::size_t>((latSegments + 1) * (lonSegments + 1)));
    mesh.indices.reserve(static_cast<std::size_t>(latSegments * lonSegments * 6));

    for (int y = 0; y <= latSegments; ++y)
    {
        const float v = static_cast<float>(y) / static_cast<float>(latSegments);
        const float theta = v * glm::pi<float>();
        const float sinTheta = std::sin(theta);
        const float cosTheta = std::cos(theta);

        for (int x = 0; x <= lonSegments; ++x)
        {
            const float u = static_cast<float>(x) / static_cast<float>(lonSegments);
            const float phi = u * glm::two_pi<float>();
            const float sinPhi = std::sin(phi);
            const float cosPhi = std::cos(phi);

            glm::vec3 normal{
                cosPhi * sinTheta,
                cosTheta,
                sinPhi * sinTheta,
            };
            if (glm::length(normal) < 1.0e-6F)
            {
                normal = glm::vec3{0.0F, 1.0F, 0.0F};
            }
            else
            {
                normal = glm::normalize(normal);
            }
            mesh.normals.push_back(normal);
            mesh.positions.push_back(normal);
        }
    }

    const int stride = lonSegments + 1;
    for (int y = 0; y < latSegments; ++y)
    {
        for (int x = 0; x < lonSegments; ++x)
        {
            const std::uint32_t i0 = static_cast<std::uint32_t>(y * stride + x);
            const std::uint32_t i1 = static_cast<std::uint32_t>((y + 1) * stride + x);
            const std::uint32_t i2 = static_cast<std::uint32_t>(y * stride + x + 1);
            const std::uint32_t i3 = static_cast<std::uint32_t>((y + 1) * stride + x + 1);

            mesh.indices.push_back(i0);
            mesh.indices.push_back(i1);
            mesh.indices.push_back(i2);

            mesh.indices.push_back(i2);
            mesh.indices.push_back(i1);
            mesh.indices.push_back(i3);
        }
    }
    return mesh;
}

const char* AssetKindToText(engine::assets::AssetKind kind)
{
    switch (kind)
    {
        case engine::assets::AssetKind::Mesh: return "Mesh";
        case engine::assets::AssetKind::Texture: return "Texture";
        case engine::assets::AssetKind::Material: return "Material";
        case engine::assets::AssetKind::Animation: return "Animation";
        case engine::assets::AssetKind::Environment: return "Environment";
        case engine::assets::AssetKind::Prefab: return "Prefab";
        case engine::assets::AssetKind::Loop: return "Loop";
        case engine::assets::AssetKind::Map: return "Map";
        default: return "Unknown";
    }
}

bool PointInPolygon2D(const std::vector<glm::vec2>& polygon, const glm::vec2& point)
{
    if (polygon.size() < 3U)
    {
        return false;
    }
    bool inside = false;
    std::size_t j = polygon.size() - 1U;
    for (std::size_t i = 0; i < polygon.size(); ++i)
    {
        const glm::vec2& pi = polygon[i];
        const glm::vec2& pj = polygon[j];
        const bool intersect =
            ((pi.y > point.y) != (pj.y > point.y)) &&
            (point.x < (pj.x - pi.x) * (point.y - pi.y) / glm::max(1.0e-6F, (pj.y - pi.y)) + pi.x);
        if (intersect)
        {
            inside = !inside;
        }
        j = i;
    }
    return inside;
}

#if BUILD_WITH_IMGUI
GLuint CreateTextureRgba8(const std::vector<unsigned char>& rgba, int width, int height)
{
    if (rgba.empty() || width <= 0 || height <= 0)
    {
        return 0;
    }
    GLuint texture = 0;
    glGenTextures(1, &texture);
    if (texture == 0)
    {
        return 0;
    }
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
    glGenerateMipmap(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, 0);
    return texture;
}

void PutPixel(std::vector<unsigned char>* pixels, int width, int height, int x, int y, const std::array<std::uint8_t, 4>& color)
{
    if (pixels == nullptr || x < 0 || y < 0 || x >= width || y >= height)
    {
        return;
    }
    const std::size_t idx = static_cast<std::size_t>((y * width + x) * 4);
    (*pixels)[idx + 0] = color[0];
    (*pixels)[idx + 1] = color[1];
    (*pixels)[idx + 2] = color[2];
    (*pixels)[idx + 3] = color[3];
}

void DrawLineRgba(
    std::vector<unsigned char>* pixels,
    int width,
    int height,
    int x0,
    int y0,
    int x1,
    int y1,
    const std::array<std::uint8_t, 4>& color
)
{
    const int dx = std::abs(x1 - x0);
    const int sx = x0 < x1 ? 1 : -1;
    const int dy = -std::abs(y1 - y0);
    const int sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    int x = x0;
    int y = y0;
    while (true)
    {
        PutPixel(pixels, width, height, x, y, color);
        if (x == x1 && y == y1)
        {
            break;
        }
        const int e2 = 2 * err;
        if (e2 >= dy)
        {
            err += dy;
            x += sx;
        }
        if (e2 <= dx)
        {
            err += dx;
            y += sy;
        }
    }
}

std::vector<unsigned char> BuildMaterialSphereThumbnailRgba(const MaterialAsset& material, int width, int height)
{
    std::vector<unsigned char> pixels(static_cast<std::size_t>(width * height * 4), 255U);
    const glm::vec3 base = glm::clamp(glm::vec3{material.baseColor}, glm::vec3{0.0F}, glm::vec3{1.0F});
    const glm::vec3 lightDir = glm::normalize(glm::vec3{0.45F, 0.75F, 0.35F});
    const glm::vec3 viewDir = glm::normalize(glm::vec3{0.0F, 0.0F, 1.0F});
    const float roughness = glm::clamp(material.roughness, 0.02F, 1.0F);
    const float metallic = glm::clamp(material.metallic, 0.0F, 1.0F);
    const float shininess = glm::mix(96.0F, 8.0F, roughness);

    for (int y = 0; y < height; ++y)
    {
        const float v = (static_cast<float>(y) + 0.5F) / static_cast<float>(height);
        const float ny = 1.0F - 2.0F * v;
        for (int x = 0; x < width; ++x)
        {
            const float u = (static_cast<float>(x) + 0.5F) / static_cast<float>(width);
            const float nx = 2.0F * u - 1.0F;
            const float r2 = nx * nx + ny * ny;
            glm::vec3 color = glm::mix(glm::vec3{0.06F, 0.07F, 0.1F}, glm::vec3{0.12F, 0.15F, 0.2F}, v);
            if (r2 <= 1.0F)
            {
                const float nz = std::sqrt(glm::max(0.0F, 1.0F - r2));
                const glm::vec3 n = glm::normalize(glm::vec3{nx, ny, nz});
                const float ndotl = glm::max(0.0F, glm::dot(n, lightDir));
                const glm::vec3 diffuse = base * (0.18F + 0.82F * ndotl);
                const glm::vec3 halfVec = glm::normalize(lightDir + viewDir);
                const float specTerm = std::pow(glm::max(0.0F, glm::dot(n, halfVec)), shininess);
                const glm::vec3 f0 = glm::mix(glm::vec3{0.04F}, base, metallic);
                const glm::vec3 spec = f0 * specTerm * (1.0F - roughness * 0.55F);
                color = glm::clamp(diffuse + spec + base * material.emissiveStrength * 0.06F, glm::vec3{0.0F}, glm::vec3{1.0F});
            }
            const std::array<std::uint8_t, 4> out{
                static_cast<std::uint8_t>(glm::clamp(color.r, 0.0F, 1.0F) * 255.0F),
                static_cast<std::uint8_t>(glm::clamp(color.g, 0.0F, 1.0F) * 255.0F),
                static_cast<std::uint8_t>(glm::clamp(color.b, 0.0F, 1.0F) * 255.0F),
                255U,
            };
            PutPixel(&pixels, width, height, x, y, out);
        }
    }
    return pixels;
}

std::vector<unsigned char> BuildMeshThumbnailRgba(const engine::assets::MeshData& mesh, int width, int height)
{
    std::vector<unsigned char> pixels(static_cast<std::size_t>(width * height * 4), 255U);
    for (int y = 0; y < height; ++y)
    {
        const float t = static_cast<float>(y) / static_cast<float>(glm::max(1, height - 1));
        const glm::vec3 bg = glm::mix(glm::vec3{0.08F, 0.1F, 0.14F}, glm::vec3{0.12F, 0.15F, 0.2F}, t);
        for (int x = 0; x < width; ++x)
        {
            PutPixel(
                &pixels,
                width,
                height,
                x,
                y,
                std::array<std::uint8_t, 4>{
                    static_cast<std::uint8_t>(bg.r * 255.0F),
                    static_cast<std::uint8_t>(bg.g * 255.0F),
                    static_cast<std::uint8_t>(bg.b * 255.0F),
                    255U,
                });
        }
    }

    if (!mesh.loaded || mesh.geometry.positions.empty() || mesh.geometry.indices.empty())
    {
        return pixels;
    }

    const engine::render::MeshGeometry* previewGeometry = &mesh.geometry;
    const engine::assets::MeshSurfaceData* previewSurface = nullptr;
    for (const auto& surface : mesh.surfaces)
    {
        if (!surface.geometry.positions.empty())
        {
            previewGeometry = &surface.geometry;
            if (!surface.albedoPixels.empty() && surface.albedoWidth > 0 && surface.albedoHeight > 0 && surface.albedoChannels > 0)
            {
                previewSurface = &surface;
                break;
            }
        }
    }
    if (previewGeometry->positions.empty() || previewGeometry->indices.empty())
    {
        return pixels;
    }

    glm::vec3 boundsMin{1.0e9F};
    glm::vec3 boundsMax{-1.0e9F};
    for (const glm::vec3& p : previewGeometry->positions)
    {
        boundsMin = glm::min(boundsMin, p);
        boundsMax = glm::max(boundsMax, p);
    }
    const glm::vec3 center = (boundsMin + boundsMax) * 0.5F;
    const glm::vec3 ext = glm::max(glm::vec3{0.001F}, boundsMax - boundsMin);
    const float scale = 1.8F / glm::max(ext.x, glm::max(ext.y, ext.z));
    const float yaw = glm::radians(35.0F);
    const float pitch = glm::radians(-25.0F);
    const glm::mat4 rot = glm::rotate(glm::mat4{1.0F}, yaw, glm::vec3{0.0F, 1.0F, 0.0F}) *
                          glm::rotate(glm::mat4{1.0F}, pitch, glm::vec3{1.0F, 0.0F, 0.0F});
    std::vector<float> depthBuffer(static_cast<std::size_t>(width * height), std::numeric_limits<float>::max());
    const glm::vec3 lightDir = glm::normalize(glm::vec3{0.45F, 0.8F, 0.35F});
    const glm::vec3 viewDir = glm::normalize(glm::vec3{0.0F, 0.0F, 1.0F});
    const glm::vec3 baseColor{0.72F, 0.84F, 0.98F};
    const std::size_t totalTriangles = previewGeometry->indices.size() / 3U;
    const std::size_t maxTriangles = glm::min<std::size_t>(totalTriangles, 8192U);
    auto edge = [](const glm::vec2& a, const glm::vec2& b, const glm::vec2& p) {
        return (p.x - a.x) * (b.y - a.y) - (p.y - a.y) * (b.x - a.x);
    };

    struct Vertex2D
    {
        glm::vec2 screen{0.0F};
        float depth = 0.0F;
        glm::vec3 normal{0.0F, 1.0F, 0.0F};
    };
    auto project = [&](const glm::vec3& p, const glm::vec3& n) -> Vertex2D {
        const glm::vec3 q = glm::vec3(rot * glm::vec4((p - center) * scale, 1.0F));
        const glm::vec3 nn = glm::normalize(glm::mat3(rot) * n);
        Vertex2D outV;
        outV.screen.x = (q.x * 0.5F + 0.5F) * static_cast<float>(width - 1);
        outV.screen.y = (0.5F - q.y * 0.5F) * static_cast<float>(height - 1);
        outV.depth = -q.z;
        outV.normal = nn;
        return outV;
    };

    for (std::size_t triSample = 0; triSample < maxTriangles; ++triSample)
    {
        const std::size_t tri = (totalTriangles <= maxTriangles)
                                    ? triSample
                                    : glm::min<std::size_t>(
                                          totalTriangles - 1U,
                                          static_cast<std::size_t>(
                                              (static_cast<double>(triSample) / static_cast<double>(maxTriangles)) *
                                              static_cast<double>(totalTriangles)));
        const std::size_t i0 = tri * 3U;
        const std::uint32_t ia = previewGeometry->indices[i0 + 0];
        const std::uint32_t ib = previewGeometry->indices[i0 + 1];
        const std::uint32_t ic = previewGeometry->indices[i0 + 2];
        if (ia >= previewGeometry->positions.size() || ib >= previewGeometry->positions.size() || ic >= previewGeometry->positions.size())
        {
            continue;
        }

        glm::vec3 na{0.0F, 1.0F, 0.0F};
        glm::vec3 nb{0.0F, 1.0F, 0.0F};
        glm::vec3 nc{0.0F, 1.0F, 0.0F};
        if (ia < previewGeometry->normals.size()) na = previewGeometry->normals[ia];
        if (ib < previewGeometry->normals.size()) nb = previewGeometry->normals[ib];
        if (ic < previewGeometry->normals.size()) nc = previewGeometry->normals[ic];

        const Vertex2D a = project(previewGeometry->positions[ia], na);
        const Vertex2D b = project(previewGeometry->positions[ib], nb);
        const Vertex2D c = project(previewGeometry->positions[ic], nc);
        const glm::vec2 uvA = ia < previewGeometry->uvs.size() ? previewGeometry->uvs[ia] : glm::vec2{0.0F};
        const glm::vec2 uvB = ib < previewGeometry->uvs.size() ? previewGeometry->uvs[ib] : glm::vec2{0.0F};
        const glm::vec2 uvC = ic < previewGeometry->uvs.size() ? previewGeometry->uvs[ic] : glm::vec2{0.0F};
        const float area = edge(a.screen, b.screen, c.screen);
        if (std::abs(area) < 1.0e-6F)
        {
            continue;
        }

        const float invArea = 1.0F / area;
        const int minX = glm::clamp(static_cast<int>(std::floor(glm::min(a.screen.x, glm::min(b.screen.x, c.screen.x)))), 0, width - 1);
        const int maxX = glm::clamp(static_cast<int>(std::ceil(glm::max(a.screen.x, glm::max(b.screen.x, c.screen.x)))), 0, width - 1);
        const int minY = glm::clamp(static_cast<int>(std::floor(glm::min(a.screen.y, glm::min(b.screen.y, c.screen.y)))), 0, height - 1);
        const int maxY = glm::clamp(static_cast<int>(std::ceil(glm::max(a.screen.y, glm::max(b.screen.y, c.screen.y)))), 0, height - 1);

        for (int y = minY; y <= maxY; ++y)
        {
            for (int x = minX; x <= maxX; ++x)
            {
                const glm::vec2 p{static_cast<float>(x) + 0.5F, static_cast<float>(y) + 0.5F};
                const float w0 = edge(b.screen, c.screen, p) * invArea;
                const float w1 = edge(c.screen, a.screen, p) * invArea;
                const float w2 = edge(a.screen, b.screen, p) * invArea;
                if (w0 < 0.0F || w1 < 0.0F || w2 < 0.0F)
                {
                    continue;
                }

                const float depth = w0 * a.depth + w1 * b.depth + w2 * c.depth;
                const std::size_t di = static_cast<std::size_t>(y * width + x);
                if (depth >= depthBuffer[di])
                {
                    continue;
                }
                depthBuffer[di] = depth;

                glm::vec3 n = glm::normalize(w0 * a.normal + w1 * b.normal + w2 * c.normal);
                if (glm::length(n) < 1.0e-6F)
                {
                    n = glm::vec3{0.0F, 1.0F, 0.0F};
                }
                glm::vec3 surfaceColor = baseColor;
                if (previewSurface != nullptr)
                {
                    const glm::vec2 uv = w0 * uvA + w1 * uvB + w2 * uvC;
                    const float u = uv.x - std::floor(uv.x);
                    const float v = uv.y - std::floor(uv.y);
                    const int tw = previewSurface->albedoWidth;
                    const int th = previewSurface->albedoHeight;
                    const int tc = glm::clamp(previewSurface->albedoChannels, 1, 4);
                    const int tx = glm::clamp(static_cast<int>(u * static_cast<float>(tw - 1) + 0.5F), 0, tw - 1);
                    const int ty = glm::clamp(static_cast<int>(v * static_cast<float>(th - 1) + 0.5F), 0, th - 1);
                    const std::size_t ti = static_cast<std::size_t>((ty * tw + tx) * tc);
                    if (ti < previewSurface->albedoPixels.size())
                    {
                        const float tr = static_cast<float>(previewSurface->albedoPixels[ti]) / 255.0F;
                        const float tg = (tc > 1 && ti + 1 < previewSurface->albedoPixels.size()) ? static_cast<float>(previewSurface->albedoPixels[ti + 1]) / 255.0F : tr;
                        const float tb = (tc > 2 && ti + 2 < previewSurface->albedoPixels.size()) ? static_cast<float>(previewSurface->albedoPixels[ti + 2]) / 255.0F : tr;
                        surfaceColor = glm::vec3{tr, tg, tb};
                    }
                }
                const float ndotl = glm::max(0.0F, glm::dot(n, lightDir));
                const glm::vec3 halfVec = glm::normalize(lightDir + viewDir);
                const float spec = std::pow(glm::max(0.0F, glm::dot(n, halfVec)), 28.0F);
                const glm::vec3 shaded = glm::clamp(surfaceColor * (0.22F + 0.78F * ndotl) + glm::vec3{0.35F} * spec, glm::vec3{0.0F}, glm::vec3{1.0F});
                PutPixel(
                    &pixels,
                    width,
                    height,
                    x,
                    y,
                    std::array<std::uint8_t, 4>{
                        static_cast<std::uint8_t>(shaded.r * 255.0F),
                        static_cast<std::uint8_t>(shaded.g * 255.0F),
                        static_cast<std::uint8_t>(shaded.b * 255.0F),
                        255U,
                    });
            }
        }
    }

    return pixels;
}

std::vector<std::string> OpenMultipleFileDialog()
{
    std::vector<std::string> files;
#if defined(_WIN32)
    constexpr DWORD kBufferChars = 64 * 1024;
    std::vector<char> buffer(static_cast<std::size_t>(kBufferChars), '\0');
    OPENFILENAMEA ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.lpstrFilter =
        "Supported Assets\0*.obj;*.gltf;*.glb;*.fbx;*.png;*.jpg;*.jpeg;*.bmp;*.tga;*.json\0"
        "All Files\0*.*\0";
    ofn.lpstrFile = buffer.data();
    ofn.nMaxFile = kBufferChars;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_ALLOWMULTISELECT | OFN_EXPLORER;
    ofn.lpstrTitle = "Import Asset(s)";
    if (GetOpenFileNameA(&ofn) == TRUE)
    {
        const char* p = buffer.data();
        std::string first = p;
        p += first.size() + 1U;
        if (*p == '\0')
        {
            files.push_back(first);
        }
        else
        {
            const std::filesystem::path dir = first;
            while (*p != '\0')
            {
                files.push_back((dir / p).string());
                p += std::strlen(p) + 1U;
            }
        }
    }
#endif
    return files;
}
#endif

const char* FxNetModeToText(engine::fx::FxNetMode mode)
{
    switch (mode)
    {
        case engine::fx::FxNetMode::ServerBroadcast: return "ServerBroadcast";
        case engine::fx::FxNetMode::OwnerOnly: return "OwnerOnly";
        case engine::fx::FxNetMode::Local:
        default: return "Local";
    }
}

int FxNetModeToIndex(engine::fx::FxNetMode mode)
{
    switch (mode)
    {
        case engine::fx::FxNetMode::ServerBroadcast: return 1;
        case engine::fx::FxNetMode::OwnerOnly: return 2;
        case engine::fx::FxNetMode::Local:
        default: return 0;
    }
}

engine::fx::FxNetMode FxNetModeFromIndex(int index)
{
    switch (index)
    {
        case 1: return engine::fx::FxNetMode::ServerBroadcast;
        case 2: return engine::fx::FxNetMode::OwnerOnly;
        case 0:
        default: return engine::fx::FxNetMode::Local;
    }
}

const char* FxEmitterTypeToText(engine::fx::FxEmitterType type)
{
    return type == engine::fx::FxEmitterType::Trail ? "Trail" : "Sprite";
}

int FxEmitterTypeToIndex(engine::fx::FxEmitterType type)
{
    return type == engine::fx::FxEmitterType::Trail ? 1 : 0;
}

engine::fx::FxEmitterType FxEmitterTypeFromIndex(int index)
{
    return index == 1 ? engine::fx::FxEmitterType::Trail : engine::fx::FxEmitterType::Sprite;
}

const char* FxBlendModeToText(engine::fx::FxBlendMode mode)
{
    return mode == engine::fx::FxBlendMode::Alpha ? "Alpha" : "Additive";
}

int FxBlendModeToIndex(engine::fx::FxBlendMode mode)
{
    return mode == engine::fx::FxBlendMode::Alpha ? 1 : 0;
}

engine::fx::FxBlendMode FxBlendModeFromIndex(int index)
{
    return index == 1 ? engine::fx::FxBlendMode::Alpha : engine::fx::FxBlendMode::Additive;
}

engine::render::EnvironmentSettings ToRenderEnvironment(const EnvironmentAsset& env)
{
    engine::render::EnvironmentSettings settings;
    settings.skyEnabled = true;
    settings.skyTopColor = env.skyTopColor;
    settings.skyBottomColor = env.skyBottomColor;
    settings.cloudsEnabled = env.cloudsEnabled;
    settings.cloudCoverage = env.cloudCoverage;
    settings.cloudDensity = env.cloudDensity;
    settings.cloudSpeed = env.cloudSpeed;
    settings.directionalLightDirection = env.directionalLightDirection;
    settings.directionalLightColor = env.directionalLightColor;
    settings.directionalLightIntensity = env.directionalLightIntensity;
    settings.fogEnabled = env.fogEnabled;
    settings.fogColor = env.fogColor;
    settings.fogDensity = env.fogDensity;
    settings.fogStart = env.fogStart;
    settings.fogEnd = env.fogEnd;
    return settings;
}

bool SampleAnimation(const AnimationClipAsset& clip, float time, glm::vec3* outPos, glm::vec3* outRot, glm::vec3* outScale)
{
    if (clip.keyframes.empty())
    {
        return false;
    }

    if (clip.keyframes.size() == 1)
    {
        if (outPos != nullptr)
        {
            *outPos = clip.keyframes.front().position;
        }
        if (outRot != nullptr)
        {
            *outRot = clip.keyframes.front().rotationEuler;
        }
        if (outScale != nullptr)
        {
            *outScale = clip.keyframes.front().scale;
        }
        return true;
    }

    const float endTime = std::max(clip.keyframes.back().time, 0.001F);
    float sampleTime = time;
    if (clip.loop)
    {
        sampleTime = std::fmod(sampleTime, endTime);
        if (sampleTime < 0.0F)
        {
            sampleTime += endTime;
        }
    }
    else
    {
        sampleTime = glm::clamp(sampleTime, 0.0F, endTime);
    }

    std::size_t nextIdx = 0;
    for (std::size_t i = 0; i < clip.keyframes.size(); ++i)
    {
        if (clip.keyframes[i].time >= sampleTime)
        {
            nextIdx = i;
            break;
        }
        nextIdx = i;
    }

    if (nextIdx == 0)
    {
        if (outPos != nullptr)
        {
            *outPos = clip.keyframes.front().position;
        }
        if (outRot != nullptr)
        {
            *outRot = clip.keyframes.front().rotationEuler;
        }
        if (outScale != nullptr)
        {
            *outScale = clip.keyframes.front().scale;
        }
        return true;
    }

    const std::size_t prevIdx = nextIdx - 1;
    const AnimationKeyframe& a = clip.keyframes[prevIdx];
    const AnimationKeyframe& b = clip.keyframes[nextIdx];
    const float denom = std::max(0.0001F, b.time - a.time);
    const float t = glm::clamp((sampleTime - a.time) / denom, 0.0F, 1.0F);

    if (outPos != nullptr)
    {
        *outPos = glm::mix(a.position, b.position, t);
    }
    if (outRot != nullptr)
    {
        *outRot = glm::mix(a.rotationEuler, b.rotationEuler, t);
    }
    if (outScale != nullptr)
    {
        *outScale = glm::mix(a.scale, b.scale, t);
    }
    return true;
}

bool ContainsCaseInsensitive(const std::string& text, const std::string& needle)
{
    if (needle.empty())
    {
        return true;
    }

    auto lower = [](const std::string& value) {
        std::string out = value;
        std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return out;
    };

    return lower(text).find(lower(needle)) != std::string::npos;
}

std::string StripNumericSuffix(const std::string& value)
{
    if (value.empty())
    {
        return "element";
    }

    const std::size_t underscore = value.find_last_of('_');
    if (underscore == std::string::npos || underscore + 1 >= value.size())
    {
        return value;
    }

    bool digitsOnly = true;
    for (std::size_t i = underscore + 1; i < value.size(); ++i)
    {
        if (!std::isdigit(static_cast<unsigned char>(value[i])))
        {
            digitsOnly = false;
            break;
        }
    }
    if (!digitsOnly)
    {
        return value;
    }

    const std::string base = value.substr(0, underscore);
    return base.empty() ? "element" : base;
}

engine::render::MaterialParams ToRenderMaterialParams(const MaterialAsset* material)
{
    engine::render::MaterialParams params{};
    if (material == nullptr)
    {
        return params;
    }
    params.roughness = glm::clamp(material->roughness, 0.0F, 1.0F);
    params.metallic = glm::clamp(material->metallic, 0.0F, 1.0F);
    params.emissive = glm::max(0.0F, material->emissiveStrength);
    params.unlit = material->shaderType == MaterialShaderType::Unlit;
    return params;
}
} // namespace

const char* LevelEditor::DockPanelTitle(DockPanel panel) const
{
    switch (panel)
    {
        case DockPanel::ToolSettings: return "Tool Settings";
        case DockPanel::LoopLibrary: return "Loop Library";
        case DockPanel::LoopEditor: return "Loop Editor";
        case DockPanel::MapLibrary: return "Map Library";
        case DockPanel::LoopPalette: return "Loop Palette";
        case DockPanel::Prefabs: return "Prefabs";
        case DockPanel::MapEditor: return "Map Editor";
        case DockPanel::Inspector: return "Inspector";
        case DockPanel::ContentBrowser: return "Content Browser";
        case DockPanel::MaterialsEnvironment: return "Materials & Environment";
        case DockPanel::FxEditor: return "FX Editor";
        case DockPanel::Outliner: return "Outliner";
        case DockPanel::Count: break;
    }
    return "Panel";
}

std::filesystem::path LevelEditor::EditorLayoutPath() const
{
    return std::filesystem::path("config") / "editor_layout.json";
}

void LevelEditor::ResetEditorLayout()
{
    m_layoutTopHeightRatio = 0.13F;
    m_layoutBottomHeightRatio = 0.24F;
    m_layoutLeftWidthRatio = 0.23F;
    m_layoutRightWidthRatio = 0.26F;
    m_layoutBottomSplitRatio = 0.52F;

    m_panelRegion.fill(LayoutRegion::Right);
    m_panelRegion[static_cast<std::size_t>(DockPanel::ToolSettings)] = LayoutRegion::Right;
    m_panelRegion[static_cast<std::size_t>(DockPanel::LoopLibrary)] = LayoutRegion::Left;
    m_panelRegion[static_cast<std::size_t>(DockPanel::LoopEditor)] = LayoutRegion::Right;
    m_panelRegion[static_cast<std::size_t>(DockPanel::MapLibrary)] = LayoutRegion::Left;
    m_panelRegion[static_cast<std::size_t>(DockPanel::LoopPalette)] = LayoutRegion::Left;
    m_panelRegion[static_cast<std::size_t>(DockPanel::Prefabs)] = LayoutRegion::Left;
    m_panelRegion[static_cast<std::size_t>(DockPanel::MapEditor)] = LayoutRegion::Right;
    m_panelRegion[static_cast<std::size_t>(DockPanel::Inspector)] = LayoutRegion::Right;
    m_panelRegion[static_cast<std::size_t>(DockPanel::ContentBrowser)] = LayoutRegion::Left;
    m_panelRegion[static_cast<std::size_t>(DockPanel::MaterialsEnvironment)] = LayoutRegion::Right;
    m_panelRegion[static_cast<std::size_t>(DockPanel::FxEditor)] = LayoutRegion::Right;
    m_panelRegion[static_cast<std::size_t>(DockPanel::Outliner)] = LayoutRegion::BottomLeft;
    m_panelVisible.fill(true);
    m_showToolbar = true;
    m_showStatusBar = true;
}

void LevelEditor::LoadEditorLayout()
{
    ResetEditorLayout();
    std::ifstream file(EditorLayoutPath());
    if (!file.is_open())
    {
        return;
    }
    try
    {
        json data;
        file >> data;
        m_layoutTopHeightRatio = glm::clamp(data.value("top_height_ratio", m_layoutTopHeightRatio), 0.06F, 0.2F);
        m_layoutBottomHeightRatio = glm::clamp(data.value("bottom_height_ratio", m_layoutBottomHeightRatio), 0.12F, 0.45F);
        m_layoutLeftWidthRatio = glm::clamp(data.value("left_width_ratio", m_layoutLeftWidthRatio), 0.12F, 0.45F);
        m_layoutRightWidthRatio = glm::clamp(data.value("right_width_ratio", m_layoutRightWidthRatio), 0.14F, 0.5F);
        m_layoutBottomSplitRatio = glm::clamp(data.value("bottom_split_ratio", m_layoutBottomSplitRatio), 0.2F, 0.8F);

        if (data.contains("panel_region") && data["panel_region"].is_object())
        {
            const json& regions = data["panel_region"];
            for (int panel = 0; panel < static_cast<int>(DockPanel::Count); ++panel)
            {
                const DockPanel panelId = static_cast<DockPanel>(panel);
                const std::string key = DockPanelTitle(panelId);
                if (!regions.contains(key) || !regions[key].is_string())
                {
                    continue;
                }
                const std::string value = regions[key].get<std::string>();
                LayoutRegion region = LayoutRegion::Right;
                if (value == "left")
                {
                    region = LayoutRegion::Left;
                }
                else if (value == "bottom_left")
                {
                    region = LayoutRegion::BottomLeft;
                }
                m_panelRegion[static_cast<std::size_t>(panelId)] = region;
            }
        }
        if (data.contains("panel_visible") && data["panel_visible"].is_object())
        {
            const json& visible = data["panel_visible"];
            for (int panel = 0; panel < static_cast<int>(DockPanel::Count); ++panel)
            {
                const DockPanel panelId = static_cast<DockPanel>(panel);
                const std::string key = DockPanelTitle(panelId);
                if (visible.contains(key) && visible[key].is_boolean())
                {
                    m_panelVisible[static_cast<std::size_t>(panelId)] = visible[key].get<bool>();
                }
            }
        }
        m_showToolbar = data.value("show_toolbar", m_showToolbar);
        m_showStatusBar = data.value("show_status_bar", m_showStatusBar);
    }
    catch (...)
    {
        ResetEditorLayout();
    }
}

void LevelEditor::SaveEditorLayout() const
{
    std::error_code ec;
    std::filesystem::create_directories(EditorLayoutPath().parent_path(), ec);
    json data;
    data["top_height_ratio"] = m_layoutTopHeightRatio;
    data["bottom_height_ratio"] = m_layoutBottomHeightRatio;
    data["left_width_ratio"] = m_layoutLeftWidthRatio;
    data["right_width_ratio"] = m_layoutRightWidthRatio;
    data["bottom_split_ratio"] = m_layoutBottomSplitRatio;
    json regions = json::object();
    json visible = json::object();
    for (int panel = 0; panel < static_cast<int>(DockPanel::Count); ++panel)
    {
        const DockPanel panelId = static_cast<DockPanel>(panel);
        const LayoutRegion region = m_panelRegion[static_cast<std::size_t>(panelId)];
        const char* regionName = region == LayoutRegion::Left ? "left" : (region == LayoutRegion::BottomLeft ? "bottom_left" : "right");
        regions[DockPanelTitle(panelId)] = regionName;
        visible[DockPanelTitle(panelId)] = m_panelVisible[static_cast<std::size_t>(panelId)];
    }
    data["panel_region"] = regions;
    data["panel_visible"] = visible;
    data["show_toolbar"] = m_showToolbar;
    data["show_status_bar"] = m_showStatusBar;
    std::ofstream file(EditorLayoutPath());
    if (file.is_open())
    {
        file << data.dump(2);
    }
}

void LevelEditor::Initialize()
{
    LevelAssetIO::EnsureAssetDirectories();
    m_assetRegistry.EnsureAssetDirectories();
    RefreshLibraries();
    RefreshContentBrowser();
    CreateNewLoop();
    CreateNewMap();
    if (LevelAssetIO::ListEnvironmentIds().empty())
    {
        EnvironmentAsset defaultEnv;
        defaultEnv.id = "default_environment";
        defaultEnv.displayName = "Default Environment";
        std::string error;
        (void)LevelAssetIO::SaveEnvironment(defaultEnv, &error);
    }
    std::string envError;
    if (!LevelAssetIO::LoadEnvironment("default_environment", &m_environmentEditing, &envError))
    {
        m_environmentEditing = EnvironmentAsset{};
    }
    m_materialEditing = MaterialAsset{};
    m_materialEditing.id = "new_material";
    m_materialEditing.displayName = "New Material";
    m_animationEditing = AnimationClipAsset{};
    m_animationEditing.id = "new_clip";
    m_animationEditing.displayName = "New Clip";
    m_materialCache.clear();
    m_animationCache.clear();
    m_fxPreviewSystem.Initialize("assets/fx");
    m_fxLibrary = m_fxPreviewSystem.ListAssetIds();
    m_selectedFxIndex = m_fxLibrary.empty() ? -1 : 0;
    m_fxEditing = engine::fx::FxAsset{};
    if (m_selectedFxIndex >= 0 && m_selectedFxIndex < static_cast<int>(m_fxLibrary.size()))
    {
        const auto loaded = m_fxPreviewSystem.GetAsset(m_fxLibrary[static_cast<std::size_t>(m_selectedFxIndex)]);
        if (loaded.has_value())
        {
            m_fxEditing = *loaded;
        }
    }
    if (m_fxEditing.emitters.empty())
    {
        m_fxEditing.emitters.push_back(engine::fx::FxEmitterAsset{});
    }
    m_selectedFxEmitterIndex =
        m_fxEditing.emitters.empty() ? -1 : glm::clamp(m_selectedFxEmitterIndex, 0, static_cast<int>(m_fxEditing.emitters.size()) - 1);
    m_fxDirty = false;
    ClearContentPreviewCache();
    ClearMeshAlbedoTextureCache();
    LoadEditorLayout();
    ResetMeshModelerToCube();
    m_undoStack.clear();
    m_redoStack.clear();
}

void LevelEditor::Enter(Mode mode)
{
    m_mode = mode;
    ClearSelections();
    m_propPlacementMode = false;
    m_pendingPlacementRotation = 0;
    m_axisDragActive = false;
    m_axisDragAxis = GizmoAxis::None;
    m_axisDragMode = GizmoMode::Translate;
    m_gizmoEditing = false;

    if (mode == Mode::LoopEditor)
    {
        // Keep loop editing deterministic: always start focused on a single 16x16 tile area.
        m_topDownView = false;
        m_cameraPosition = glm::vec3{0.0F, 11.0F, 18.0F};
        m_cameraYaw = 0.0F;
        m_cameraPitch = -0.52F;
        m_cameraSpeed = 16.0F;
        m_debugView = true;
    }
    m_cameraOrbitTarget = CameraFocusPivot();
    m_cameraOrbitTargetValid = true;

    m_contentNeedsRefresh = true;
    m_statusLine = std::string{"Entered "} + ModeToText(mode);
}

void LevelEditor::SetMeshObjectEditMode(bool editMode, const char* reason)
{
    if (editMode && m_mode != Mode::LoopEditor)
    {
        m_mode = Mode::LoopEditor;
    }
    m_meshObjectMode = editMode ? MeshObjectMode::Edit : MeshObjectMode::Object;
    m_meshModelSceneEditEnabled = editMode;
    if (editMode)
    {
        TryActivateMeshFromCurrentSelection();
        // Mesh edit mode should operate on active mesh object only.
        m_selection = Selection{};
        m_selectedLoopElements.clear();
        m_selectedMapPlacements.clear();
        m_selectedProps.clear();
        m_selectedLightIndex = -1;
        FocusCameraOnSelection();
    }
    if (reason != nullptr && reason[0] != '\0')
    {
        m_statusLine = std::string(editMode ? "EDIT MODE" : "OBJECT MODE") + " (" + reason + ")";
    }
    else
    {
        m_statusLine = editMode ? "EDIT MODE" : "OBJECT MODE";
    }
    std::cout << "[EDITOR] mode_switch object_mode=" << (editMode ? "edit" : "object")
              << " reason=" << (reason != nullptr ? reason : "n/a") << "\n";
}

void LevelEditor::ToggleMeshObjectEditMode(const char* reason)
{
    SetMeshObjectEditMode(!m_meshModelSceneEditEnabled, reason);
}

const char* LevelEditor::MeshPrimitiveToText(int primitiveTypeRaw) const
{
    const MeshPrimitiveType primitive = static_cast<MeshPrimitiveType>(std::clamp(primitiveTypeRaw, 0, 6));
    switch (primitive)
    {
        case MeshPrimitiveType::Cube: return "Cube";
        case MeshPrimitiveType::Sphere: return "Sphere";
        case MeshPrimitiveType::Plane: return "Plane";
        case MeshPrimitiveType::Cylinder: return "Cylinder";
        case MeshPrimitiveType::Cone: return "Cone";
        case MeshPrimitiveType::Capsule: return "Capsule";
        default: break;
    }
    return "None";
}

const char* LevelEditor::MeshSpawnModeToText(int spawnModeRaw) const
{
    const MeshSpawnMode mode = static_cast<MeshSpawnMode>(std::clamp(spawnModeRaw, 0, 2));
    switch (mode)
    {
        case MeshSpawnMode::Cursor: return "Cursor";
        case MeshSpawnMode::CameraRaycast: return "Camera Raycast";
        case MeshSpawnMode::ClickPlace: return "Click Place";
        default: break;
    }
    return "Cursor";
}

void LevelEditor::QueueMeshPrimitiveSpawn(int primitiveTypeRaw)
{
    const MeshPrimitiveType primitive = static_cast<MeshPrimitiveType>(std::clamp(primitiveTypeRaw, 0, 6));
    if (primitive == MeshPrimitiveType::None)
    {
        return;
    }

    if (m_meshSpawnMode == MeshSpawnMode::ClickPlace)
    {
        m_clickPlacePrimitive = primitive;
        m_meshClickPlacementPending = true;
        m_statusLine = std::string("Click-to-place: ") + MeshPrimitiveToText(static_cast<int>(primitive)) + " (LMB confirm, Esc cancel)";
        return;
    }

    m_pendingMeshPrimitive = primitive;
}

glm::vec3 LevelEditor::ComputeMeshSpawnPosition(int spawnModeRaw, const engine::platform::Input& input) const
{
    const MeshSpawnMode mode = static_cast<MeshSpawnMode>(std::clamp(spawnModeRaw, 0, 2));
    if (mode == MeshSpawnMode::Cursor)
    {
        return m_meshCursorPosition;
    }

    glm::vec3 rayOrigin = m_cameraPosition;
    glm::vec3 rayDirection = CameraForward();
    if (mode == MeshSpawnMode::ClickPlace)
    {
        glm::vec3 mouseOrigin{0.0F};
        glm::vec3 mouseDirection{0.0F};
        if (BuildMouseRay(input, m_lastFramebufferWidth, m_lastFramebufferHeight, &mouseOrigin, &mouseDirection))
        {
            rayOrigin = mouseOrigin;
            rayDirection = mouseDirection;
        }
    }

    glm::vec3 hit{0.0F};
    if (RayIntersectGround(rayOrigin, rayDirection, 0.0F, &hit))
    {
        return hit + glm::vec3{0.0F, 0.8F, 0.0F};
    }
    return m_cameraPosition + CameraForward() * 8.0F + glm::vec3{0.0F, 0.8F, 0.0F};
}

void LevelEditor::SpawnMeshPrimitiveNow(int primitiveTypeRaw, const glm::vec3& position, const char* sourceLabel)
{
    const MeshPrimitiveType primitive = static_cast<MeshPrimitiveType>(std::clamp(primitiveTypeRaw, 0, 6));
    if (primitive == MeshPrimitiveType::None)
    {
        return;
    }

    PushHistorySnapshot();

    switch (primitive)
    {
        case MeshPrimitiveType::Cube:
            ResetMeshModelerToCube();
            break;
        case MeshPrimitiveType::Sphere:
            ResetMeshModelerToSphere(m_meshPrimitiveSphereLatSegments, m_meshPrimitiveSphereLonSegments, m_meshPrimitiveRadius);
            break;
        case MeshPrimitiveType::Plane:
            ResetMeshModelerToSquare();
            break;
        case MeshPrimitiveType::Cylinder:
            // MVP cylinder: capsule generator with near-flat caps.
            ResetMeshModelerToCapsule(m_meshPrimitiveCapsuleSegments, 3, 1, m_meshPrimitiveRadius, m_meshPrimitiveHeight);
            break;
        case MeshPrimitiveType::Cone:
            ResetMeshModelerToCircle(std::max(6, m_meshPrimitiveCircleSegments), m_meshPrimitiveRadius);
            if (m_meshModelSelectedFace >= 0)
            {
                MeshModelerExtrudeFace(m_meshModelSelectedFace, std::max(0.2F, m_meshPrimitiveHeight * 0.8F));
            }
            break;
        case MeshPrimitiveType::Capsule:
            ResetMeshModelerToCapsule(
                m_meshPrimitiveCapsuleSegments,
                m_meshPrimitiveCapsuleHemiRings,
                m_meshPrimitiveCapsuleCylinderRings,
                m_meshPrimitiveRadius,
                m_meshPrimitiveHeight
            );
            break;
        default:
            break;
    }

    m_meshModelPosition = position;
    m_meshActiveObjectName = "mesh_" + std::to_string(m_meshObjectCounter++);
    SetMeshObjectEditMode(false, sourceLabel != nullptr ? sourceLabel : "spawn");
    FocusCameraOnSelection();
    std::ostringstream oss;
    oss << "Spawned " << MeshPrimitiveToText(static_cast<int>(primitive))
        << " at (" << position.x << ", " << position.y << ", " << position.z << ")";
    m_statusLine = oss.str();
    std::cout << "[EDITOR] mesh_spawn primitive=" << MeshPrimitiveToText(static_cast<int>(primitive))
              << " source=" << (sourceLabel != nullptr ? sourceLabel : "n/a")
              << " pos=" << position.x << "," << position.y << "," << position.z << "\n";
}

void LevelEditor::SpawnQueuedMeshPrimitive(const engine::platform::Input& input)
{
    if (m_pendingMeshPrimitive == MeshPrimitiveType::None)
    {
        return;
    }

    const glm::vec3 spawn = ComputeMeshSpawnPosition(static_cast<int>(m_meshSpawnMode), input);
    SpawnMeshPrimitiveNow(static_cast<int>(m_pendingMeshPrimitive), spawn, "quick add");
    m_pendingMeshPrimitive = MeshPrimitiveType::None;
}

void LevelEditor::UpdateMeshCursorFromMouse(
    const engine::platform::Input& input,
    int framebufferWidth,
    int framebufferHeight
)
{
    glm::vec3 rayOrigin{0.0F};
    glm::vec3 rayDirection{0.0F};
    if (!BuildMouseRay(input, framebufferWidth, framebufferHeight, &rayOrigin, &rayDirection))
    {
        return;
    }

    glm::vec3 hit{0.0F};
    if (RayIntersectGround(rayOrigin, rayDirection, 0.0F, &hit))
    {
        m_meshCursorPosition = hit + glm::vec3{0.0F, 0.8F, 0.0F};
        std::ostringstream oss;
        oss << "3D Cursor set to (" << m_meshCursorPosition.x << ", " << m_meshCursorPosition.y << ", "
            << m_meshCursorPosition.z << ")";
        m_statusLine = oss.str();
    }
}

void LevelEditor::BuildMeshModelFromProp(const PropInstance& prop)
{
    // Build editable mesh proxy from selected scene prop.
    switch (prop.type)
    {
        case PropType::Rock:
            ResetMeshModelerToSphere(
                std::max(10, m_meshPrimitiveSphereLatSegments),
                std::max(14, m_meshPrimitiveSphereLonSegments),
                std::max(0.15F, std::max(prop.halfExtents.x, std::max(prop.halfExtents.y, prop.halfExtents.z)))
            );
            break;
        case PropType::Platform:
            ResetMeshModelerToSquare();
            break;
        case PropType::Tree:
            ResetMeshModelerToCapsule(
                std::max(8, m_meshPrimitiveCapsuleSegments),
                std::max(4, m_meshPrimitiveCapsuleHemiRings),
                std::max(2, m_meshPrimitiveCapsuleCylinderRings),
                std::max(0.1F, prop.halfExtents.x),
                std::max(0.4F, prop.halfExtents.y * 2.0F)
            );
            break;
        case PropType::Obstacle:
        case PropType::MeshAsset:
        default:
            ResetMeshModelerToCube();
            break;
    }

    m_meshModelScale = glm::max(glm::vec3{0.25F}, prop.halfExtents * 2.0F);
    if (m_uiWorkspace == UiWorkspace::Mesh)
    {
        // Dedicated modeling workspace should isolate active object at editor origin.
        m_meshModelPosition = glm::vec3{0.0F, std::max(0.8F, m_meshModelScale.y * 0.5F), 0.0F};
    }
    else
    {
        m_meshModelPosition = prop.position;
    }
    m_meshActiveObjectName = prop.name.empty() ? "mesh_object" : prop.name;
}

void LevelEditor::TryActivateMeshFromCurrentSelection()
{
    if (m_selection.kind == SelectionKind::Prop &&
        m_selection.index >= 0 &&
        m_selection.index < static_cast<int>(m_map.props.size()))
    {
        BuildMeshModelFromProp(m_map.props[static_cast<std::size_t>(m_selection.index)]);
        return;
    }

    // Fallback to default editable mesh if topology got cleared.
    if (m_meshModelVertices.empty() || m_meshModelFaces.empty())
    {
        ResetMeshModelerToCube();
        m_meshActiveObjectName = "mesh_object";
    }
}

void LevelEditor::QueueExternalDroppedFiles(const std::vector<std::string>& absolutePaths)
{
    for (const std::string& path : absolutePaths)
    {
        if (path.empty())
        {
            continue;
        }
        std::error_code ec;
        if (!std::filesystem::is_regular_file(path, ec))
        {
            continue;
        }
        m_pendingExternalDrops.push_back(path);
    }
    if (!m_pendingExternalDrops.empty())
    {
        m_statusLine = "Dropped files queued: " + std::to_string(m_pendingExternalDrops.size()) +
                       " (hover Content Browser/folder to import)";
    }
}

std::optional<engine::render::RenderMode> LevelEditor::ConsumeRequestedRenderMode()
{
    const std::optional<engine::render::RenderMode> mode = m_pendingRenderMode;
    m_pendingRenderMode.reset();
    return mode;
}

glm::vec3 LevelEditor::CameraForward() const
{
    if (m_topDownView)
    {
        return glm::vec3{0.0F, -1.0F, 0.0F};
    }

    const float cosPitch = std::cos(m_cameraPitch);
    return glm::normalize(glm::vec3{
        std::sin(m_cameraYaw) * cosPitch,
        std::sin(m_cameraPitch),
        -std::cos(m_cameraYaw) * cosPitch,
    });
}

glm::vec3 LevelEditor::CameraUp() const
{
    if (m_topDownView)
    {
        // Forward=(0,-1,0) cannot use world up=(0,1,0) in lookAt; pick a stable horizontal up axis.
        return glm::vec3{0.0F, 0.0F, -1.0F};
    }
    return glm::vec3{0.0F, 1.0F, 0.0F};
}

glm::vec3 LevelEditor::CameraRight() const
{
    const glm::vec3 forward = CameraForward();
    glm::vec3 right = glm::cross(forward, CameraUp());
    if (glm::length(right) < 1.0e-5F)
    {
        right = glm::vec3{1.0F, 0.0F, 0.0F};
    }
    return glm::normalize(right);
}

glm::vec3 LevelEditor::CameraFocusPivot() const
{
    if (m_mode == Mode::LoopEditor && m_meshModelSceneEditEnabled)
    {
        return MeshModelSelectionPivot();
    }

    const glm::vec3 selectionPivot = SelectionPivot();
    if (m_selection.kind != SelectionKind::None)
    {
        return selectionPivot;
    }

    if (m_mode == Mode::MapEditor)
    {
        return glm::vec3{0.0F, 0.0F, 0.0F};
    }
    return m_meshModelPosition;
}

void LevelEditor::FocusCameraOnSelection()
{
    const glm::vec3 pivot = CameraFocusPivot();
    const float currentDistance = glm::length(m_cameraPosition - pivot);
    const float desiredDistance = glm::clamp(currentDistance > 1.0e-3F ? currentDistance : 16.0F, 2.0F, 180.0F);
    const glm::vec3 forward = CameraForward();
    m_cameraPosition = pivot - forward * desiredDistance;
    m_cameraOrbitTarget = pivot;
    m_cameraOrbitTargetValid = true;
    m_statusLine = "Framed selection";
}

void LevelEditor::RefreshLibraries()
{
    m_loopLibrary = LevelAssetIO::ListLoopIds();
    m_mapLibrary = LevelAssetIO::ListMapNames();
    m_prefabLibrary = LevelAssetIO::ListPrefabIds();
    m_materialLibrary = LevelAssetIO::ListMaterialIds();
    m_animationLibrary = LevelAssetIO::ListAnimationClipIds();
    m_fxLibrary = m_fxPreviewSystem.ListAssetIds();
    if (m_paletteLoopIndex >= static_cast<int>(m_loopLibrary.size()))
    {
        m_paletteLoopIndex = m_loopLibrary.empty() ? -1 : 0;
    }
    if (m_selectedPrefabIndex >= static_cast<int>(m_prefabLibrary.size()))
    {
        m_selectedPrefabIndex = m_prefabLibrary.empty() ? -1 : 0;
    }
    if (m_selectedMaterialIndex >= static_cast<int>(m_materialLibrary.size()))
    {
        m_selectedMaterialIndex = m_materialLibrary.empty() ? -1 : 0;
    }
    if (m_selectedAnimationIndex >= static_cast<int>(m_animationLibrary.size()))
    {
        m_selectedAnimationIndex = m_animationLibrary.empty() ? -1 : 0;
    }
    if (m_selectedFxIndex >= static_cast<int>(m_fxLibrary.size()))
    {
        m_selectedFxIndex = m_fxLibrary.empty() ? -1 : 0;
    }

    if (m_selectedMaterialIndex >= 0 && m_selectedMaterialIndex < static_cast<int>(m_materialLibrary.size()))
    {
        m_selectedMaterialId = m_materialLibrary[static_cast<std::size_t>(m_selectedMaterialIndex)];
    }
    else
    {
        m_selectedMaterialId.clear();
    }

    if (m_selectedAnimationIndex >= 0 && m_selectedAnimationIndex < static_cast<int>(m_animationLibrary.size()))
    {
        m_animationPreviewClip = m_animationLibrary[static_cast<std::size_t>(m_selectedAnimationIndex)];
    }
    else
    {
        m_animationPreviewClip.clear();
    }
}

void LevelEditor::CreateNewLoop(const std::string& suggestedName)
{
    m_loop = LoopAsset{};
    m_loop.id = suggestedName;
    m_loop.displayName = suggestedName;
    m_loop.elements.clear();
    ClearSelections();
}

void LevelEditor::CreateNewMap(const std::string& suggestedName)
{
    m_map = MapAsset{};
    m_map.name = suggestedName;
    m_map.environmentAssetId = "default_environment";
    m_map.placements.clear();
    m_map.props.clear();
    m_selectedLightIndex = -1;
    (void)LevelAssetIO::LoadEnvironment(m_map.environmentAssetId, &m_environmentEditing, nullptr);
    ClearSelections();
}

LevelEditor::HistoryState LevelEditor::CaptureHistoryState() const
{
    HistoryState state;
    state.mode = m_mode;
    state.loop = m_loop;
    state.map = m_map;
    state.selection = m_selection;
    state.selectedLoopElements = m_selectedLoopElements;
    state.selectedMapPlacements = m_selectedMapPlacements;
    state.selectedProps = m_selectedProps;
    state.propPlacementMode = m_propPlacementMode;
    state.pendingPlacementRotation = m_pendingPlacementRotation;
    state.paletteLoopIndex = m_paletteLoopIndex;
    state.selectedPropType = m_selectedPropType;
    state.meshModelVertices = m_meshModelVertices;
    state.meshModelFaces = m_meshModelFaces;
    state.meshModelSelectedFace = m_meshModelSelectedFace;
    state.meshModelSelectedVertex = m_meshModelSelectedVertex;
    state.meshModelSelectedEdge = m_meshModelSelectedEdge;
    state.meshModelFaceSelection = m_meshModelFaceSelection;
    state.meshModelVertexSelection = m_meshModelVertexSelection;
    state.meshModelLoopSelectionEdges = m_meshModelLoopSelectionEdges;
    state.meshModelRingSelectionEdges = m_meshModelRingSelectionEdges;
    state.meshModelPosition = m_meshModelPosition;
    state.meshModelScale = m_meshModelScale;
    state.meshActiveObjectName = m_meshActiveObjectName;
    state.meshModelSceneEditEnabled = m_meshModelSceneEditEnabled;
    state.meshEditMode = static_cast<int>(m_meshEditMode);
    state.meshObjectMode = static_cast<int>(m_meshObjectMode);
    return state;
}

void LevelEditor::RestoreHistoryState(const HistoryState& state)
{
    m_historyApplying = true;
    m_mode = state.mode;
    m_loop = state.loop;
    m_map = state.map;
    m_selection = state.selection;
    m_selectedLoopElements = state.selectedLoopElements;
    m_selectedMapPlacements = state.selectedMapPlacements;
    m_selectedProps = state.selectedProps;
    m_propPlacementMode = state.propPlacementMode;
    m_pendingPlacementRotation = state.pendingPlacementRotation;
    m_paletteLoopIndex = state.paletteLoopIndex;
    m_selectedPropType = state.selectedPropType;
    m_meshModelVertices = state.meshModelVertices;
    m_meshModelFaces = state.meshModelFaces;
    m_meshModelSelectedFace = state.meshModelSelectedFace;
    m_meshModelSelectedVertex = state.meshModelSelectedVertex;
    m_meshModelSelectedEdge = state.meshModelSelectedEdge;
    m_meshModelFaceSelection = state.meshModelFaceSelection;
    m_meshModelVertexSelection = state.meshModelVertexSelection;
    m_meshModelLoopSelectionEdges = state.meshModelLoopSelectionEdges;
    m_meshModelRingSelectionEdges = state.meshModelRingSelectionEdges;
    m_meshModelPosition = state.meshModelPosition;
    m_meshModelScale = state.meshModelScale;
    m_meshActiveObjectName = state.meshActiveObjectName;
    m_meshModelSceneEditEnabled = state.meshModelSceneEditEnabled;
    m_meshEditMode = static_cast<MeshEditMode>(std::clamp(state.meshEditMode, 0, 2));
    m_meshObjectMode = static_cast<MeshObjectMode>(std::clamp(state.meshObjectMode, 0, 1));
    m_historyApplying = false;
}

void LevelEditor::PushHistorySnapshot()
{
    if (m_historyApplying)
    {
        return;
    }

    m_undoStack.push_back(CaptureHistoryState());
    if (m_undoStack.size() > m_historyMaxEntries)
    {
        m_undoStack.erase(m_undoStack.begin());
    }
    m_redoStack.clear();
}

void LevelEditor::Undo()
{
    if (m_undoStack.empty())
    {
        m_statusLine = "Undo: no history";
        return;
    }

    m_redoStack.push_back(CaptureHistoryState());
    const HistoryState previous = m_undoStack.back();
    m_undoStack.pop_back();
    RestoreHistoryState(previous);
    m_statusLine = "Undo";
}

void LevelEditor::Redo()
{
    if (m_redoStack.empty())
    {
        m_statusLine = "Redo: no history";
        return;
    }

    m_undoStack.push_back(CaptureHistoryState());
    const HistoryState next = m_redoStack.back();
    m_redoStack.pop_back();
    RestoreHistoryState(next);
    m_statusLine = "Redo";
}

void LevelEditor::ClearSelections()
{
    m_selection = Selection{};
    m_selectedLoopElements.clear();
    m_selectedMapPlacements.clear();
    m_selectedProps.clear();
}

void LevelEditor::SelectSingle(const Selection& selection)
{
    ClearSelections();
    if (selection.kind == SelectionKind::None || selection.index < 0)
    {
        return;
    }

    m_selection = selection;
    if (selection.kind == SelectionKind::LoopElement)
    {
        m_selectedLoopElements.push_back(selection.index);
    }
    else if (selection.kind == SelectionKind::MapPlacement)
    {
        m_selectedMapPlacements.push_back(selection.index);
    }
    else if (selection.kind == SelectionKind::Prop)
    {
        m_selectedProps.push_back(selection.index);
    }
}

void LevelEditor::ToggleSelection(const Selection& selection)
{
    if (selection.kind == SelectionKind::None || selection.index < 0)
    {
        return;
    }

    std::vector<int>* list = nullptr;
    if (selection.kind == SelectionKind::LoopElement)
    {
        list = &m_selectedLoopElements;
    }
    else if (selection.kind == SelectionKind::MapPlacement)
    {
        list = &m_selectedMapPlacements;
    }
    else if (selection.kind == SelectionKind::Prop)
    {
        list = &m_selectedProps;
    }
    if (list == nullptr)
    {
        return;
    }

    if (m_selection.kind != selection.kind)
    {
        SelectSingle(selection);
        return;
    }

    const auto it = std::find(list->begin(), list->end(), selection.index);
    if (it != list->end())
    {
        list->erase(it);
        if (m_selection.kind == selection.kind && m_selection.index == selection.index)
        {
            if (list->empty())
            {
                m_selection = Selection{};
            }
            else
            {
                m_selection.index = list->front();
            }
        }
    }
    else
    {
        list->push_back(selection.index);
        m_selection = selection;
    }
}

bool LevelEditor::IsSelected(SelectionKind kind, int index) const
{
    if (index < 0)
    {
        return false;
    }

    if (kind == SelectionKind::LoopElement)
    {
        return std::find(m_selectedLoopElements.begin(), m_selectedLoopElements.end(), index) != m_selectedLoopElements.end();
    }
    if (kind == SelectionKind::MapPlacement)
    {
        return std::find(m_selectedMapPlacements.begin(), m_selectedMapPlacements.end(), index) != m_selectedMapPlacements.end();
    }
    if (kind == SelectionKind::Prop)
    {
        return std::find(m_selectedProps.begin(), m_selectedProps.end(), index) != m_selectedProps.end();
    }
    return false;
}

std::vector<int> LevelEditor::SortedUniqueValidSelection(SelectionKind kind) const
{
    std::vector<int> indices;
    if (kind == SelectionKind::LoopElement)
    {
        indices = m_selectedLoopElements;
        if (indices.empty() && m_selection.kind == kind)
        {
            indices.push_back(m_selection.index);
        }
        const int maxIndex = static_cast<int>(m_loop.elements.size());
        indices.erase(
            std::remove_if(indices.begin(), indices.end(), [maxIndex](int idx) { return idx < 0 || idx >= maxIndex; }),
            indices.end()
        );
    }
    else if (kind == SelectionKind::MapPlacement)
    {
        indices = m_selectedMapPlacements;
        if (indices.empty() && m_selection.kind == kind)
        {
            indices.push_back(m_selection.index);
        }
        const int maxIndex = static_cast<int>(m_map.placements.size());
        indices.erase(
            std::remove_if(indices.begin(), indices.end(), [maxIndex](int idx) { return idx < 0 || idx >= maxIndex; }),
            indices.end()
        );
    }
    else if (kind == SelectionKind::Prop)
    {
        indices = m_selectedProps;
        if (indices.empty() && m_selection.kind == kind)
        {
            indices.push_back(m_selection.index);
        }
        const int maxIndex = static_cast<int>(m_map.props.size());
        indices.erase(
            std::remove_if(indices.begin(), indices.end(), [maxIndex](int idx) { return idx < 0 || idx >= maxIndex; }),
            indices.end()
        );
    }

    std::sort(indices.begin(), indices.end());
    indices.erase(std::unique(indices.begin(), indices.end()), indices.end());
    return indices;
}

void LevelEditor::HandleCamera(float deltaSeconds, const engine::platform::Input& input, bool controlsEnabled)
{
    if (!controlsEnabled)
    {
        return;
    }

    const glm::vec2 mousePos = input.MousePosition();
    const bool mouseInSceneRect =
        mousePos.x >= m_sceneViewportRectMin.x && mousePos.x <= m_sceneViewportRectMax.x &&
        mousePos.y >= m_sceneViewportRectMin.y && mousePos.y <= m_sceneViewportRectMax.y;
    const bool sceneMouseActive = mouseInSceneRect && (m_sceneViewportHovered || m_sceneViewportFocused);
    const bool sceneKeyboardActive = sceneMouseActive || m_sceneViewportFocused;

#if BUILD_WITH_IMGUI
    if ((ImGui::GetIO().WantCaptureKeyboard || ImGui::GetIO().WantCaptureMouse) && !sceneKeyboardActive)
    {
        return;
    }
#endif

    const bool shiftDown = input.IsKeyDown(GLFW_KEY_LEFT_SHIFT) || input.IsKeyDown(GLFW_KEY_RIGHT_SHIFT);
    const bool ctrlDown = input.IsKeyDown(GLFW_KEY_LEFT_CONTROL) || input.IsKeyDown(GLFW_KEY_RIGHT_CONTROL);
    const bool hasTransformSelection = HasSceneSelection() || HasMeshSelection();
    const bool shiftRmbCursorAction = (m_mode == Mode::LoopEditor) && shiftDown;
    const bool lookActive =
        input.IsMouseDown(GLFW_MOUSE_BUTTON_RIGHT) && !ctrlDown && !m_lassoSelection.active && !shiftRmbCursorAction;
    const bool orbitActive = input.IsMouseDown(GLFW_MOUSE_BUTTON_MIDDLE);
    const glm::vec2 mouseDelta = input.MouseDelta();
    if (orbitActive)
    {
        if (!m_cameraOrbitTargetValid)
        {
            m_cameraOrbitTarget = CameraFocusPivot();
            m_cameraOrbitTargetValid = true;
        }

        if (shiftDown)
        {
            const glm::vec3 right = CameraRight();
            const glm::vec3 up = CameraUp();
            const float distance = glm::max(1.0F, glm::length(m_cameraPosition - m_cameraOrbitTarget));
            const float panScale = glm::max(0.015F, distance * 0.0018F);
            const glm::vec3 panOffset = (-right * mouseDelta.x + up * mouseDelta.y) * panScale;
            m_cameraPosition += panOffset;
            m_cameraOrbitTarget += panOffset;
        }
        else
        {
            const float distance = glm::max(1.0F, glm::length(m_cameraPosition - m_cameraOrbitTarget));
            m_cameraYaw += mouseDelta.x * 0.0038F;
            m_cameraPitch = ClampPitch(m_cameraPitch - mouseDelta.y * 0.0038F);
            const glm::vec3 forward = CameraForward();
            m_cameraPosition = m_cameraOrbitTarget - forward * distance;
        }
    }
    else if (lookActive)
    {
        m_cameraYaw += mouseDelta.x * 0.0025F;
        m_cameraPitch = ClampPitch(m_cameraPitch - mouseDelta.y * 0.0025F);
    }

    glm::vec3 movement{0.0F};
    const bool cameraMoveEnabled = sceneMouseActive && (lookActive || orbitActive);
    if (cameraMoveEnabled)
    {
        const glm::vec3 forward =
            m_topDownView ? glm::vec3{0.0F, 0.0F, -1.0F} : glm::normalize(glm::vec3{CameraForward().x, 0.0F, CameraForward().z});
        const glm::vec3 right = m_topDownView ? glm::vec3{1.0F, 0.0F, 0.0F} : CameraRight();

        if (input.IsKeyDown(GLFW_KEY_W))
        {
            movement += forward;
        }
        if (input.IsKeyDown(GLFW_KEY_S) && !(hasTransformSelection && input.IsKeyPressed(GLFW_KEY_S)))
        {
            movement -= forward;
        }
        if (input.IsKeyDown(GLFW_KEY_D))
        {
            movement += right;
        }
        if (input.IsKeyDown(GLFW_KEY_A))
        {
            movement -= right;
        }
        if (input.IsKeyDown(GLFW_KEY_E))
        {
            movement += glm::vec3{0.0F, 1.0F, 0.0F};
        }
        if (input.IsKeyDown(GLFW_KEY_Q))
        {
            movement -= glm::vec3{0.0F, 1.0F, 0.0F};
        }
    }

    if (glm::length(movement) > 1.0e-5F)
    {
        movement = glm::normalize(movement);
    }

    float speed = m_cameraSpeed;
    if (shiftDown)
    {
        speed *= 2.2F;
    }
    const glm::vec3 moveDelta = movement * speed * deltaSeconds;
    m_cameraPosition += moveDelta;
    if (glm::length(moveDelta) > 1.0e-6F && m_cameraOrbitTargetValid)
    {
        m_cameraOrbitTarget += moveDelta;
    }

#if BUILD_WITH_IMGUI
    float wheel = 0.0F;
    if ((!ImGui::GetIO().WantCaptureMouse) || sceneMouseActive)
    {
        wheel = ImGui::GetIO().MouseWheel;
    }
    if (std::abs(wheel) > 1.0e-5F)
    {
        if (!m_cameraOrbitTargetValid)
        {
            m_cameraOrbitTarget = CameraFocusPivot();
            m_cameraOrbitTargetValid = true;
        }
        glm::vec3 offset = m_cameraPosition - m_cameraOrbitTarget;
        float distance = glm::length(offset);
        if (distance < 1.0e-4F)
        {
            offset = -CameraForward();
            distance = 1.0F;
        }
        const float zoomStep = glm::max(0.2F, distance * 0.12F);
        const float nextDistance = glm::clamp(distance - wheel * zoomStep, 0.4F, 800.0F);
        m_cameraPosition = m_cameraOrbitTarget + glm::normalize(offset) * nextDistance;
    }
#endif
}

void LevelEditor::HandleEditorHotkeys(const engine::platform::Input& input, bool controlsEnabled)
{
    if (!controlsEnabled)
    {
        return;
    }

    const glm::vec2 mousePos = input.MousePosition();
    const bool mouseInSceneRect =
        mousePos.x >= m_sceneViewportRectMin.x && mousePos.x <= m_sceneViewportRectMax.x &&
        mousePos.y >= m_sceneViewportRectMin.y && mousePos.y <= m_sceneViewportRectMax.y;
    const bool sceneMouseActive = mouseInSceneRect && (m_sceneViewportHovered || m_sceneViewportFocused);
    const bool sceneKeyboardActive = sceneMouseActive || m_sceneViewportFocused;
    const bool ctrlDown = input.IsKeyDown(GLFW_KEY_LEFT_CONTROL) || input.IsKeyDown(GLFW_KEY_RIGHT_CONTROL);
    const bool shiftDown = input.IsKeyDown(GLFW_KEY_LEFT_SHIFT) || input.IsKeyDown(GLFW_KEY_RIGHT_SHIFT);

#if BUILD_WITH_IMGUI
    const bool textInputActive = ImGui::GetIO().WantTextInput;
#else
    const bool textInputActive = false;
#endif

    if (!textInputActive && ctrlDown && input.IsKeyPressed(GLFW_KEY_Z))
    {
        if (shiftDown)
        {
            Redo();
        }
        else
        {
            Undo();
        }
        return;
    }
    if (!textInputActive && ctrlDown && input.IsKeyPressed(GLFW_KEY_Y))
    {
        Redo();
        return;
    }

    if (!textInputActive && input.IsKeyPressed(GLFW_KEY_TAB))
    {
        if (shiftDown)
        {
            m_gridSnap = !m_gridSnap;
            m_statusLine = std::string{"Snap "} + (m_gridSnap ? "ON" : "OFF");
        }
        else
        {
            if (m_mode != Mode::LoopEditor)
            {
                m_mode = Mode::LoopEditor;
            }
            ToggleMeshObjectEditMode("Tab");
        }
    }

#if BUILD_WITH_IMGUI
    if ((ImGui::GetIO().WantCaptureKeyboard || ImGui::GetIO().WantCaptureMouse) &&
        !sceneKeyboardActive)
    {
        return;
    }
#endif

    const bool editMode = (m_mode == Mode::LoopEditor) && m_meshModelSceneEditEnabled;
    if (input.IsKeyPressed(GLFW_KEY_PERIOD))
    {
        FocusCameraOnSelection();
    }

    if (editMode)
    {
        if (input.IsKeyPressed(GLFW_KEY_1) || input.IsKeyPressed(GLFW_KEY_KP_1))
        {
            m_meshEditMode = MeshEditMode::Vertex;
        }
        if (input.IsKeyPressed(GLFW_KEY_2) || input.IsKeyPressed(GLFW_KEY_KP_2))
        {
            m_meshEditMode = MeshEditMode::Edge;
        }
        if (input.IsKeyPressed(GLFW_KEY_3) || input.IsKeyPressed(GLFW_KEY_KP_3))
        {
            m_meshEditMode = MeshEditMode::Face;
        }
    }
    else
    {
        if (input.IsKeyPressed(GLFW_KEY_1) || input.IsKeyPressed(GLFW_KEY_KP_1))
        {
            m_gizmoMode = GizmoMode::Translate;
        }
        if (input.IsKeyPressed(GLFW_KEY_2) || input.IsKeyPressed(GLFW_KEY_KP_2))
        {
            m_gizmoMode = GizmoMode::Rotate;
        }
        if (input.IsKeyPressed(GLFW_KEY_3) || input.IsKeyPressed(GLFW_KEY_KP_3))
        {
            m_gizmoMode = GizmoMode::Scale;
        }
    }
    if (input.IsKeyPressed(GLFW_KEY_G))
    {
        if (HasSceneSelection() || HasMeshSelection())
        {
            BeginModalTransform(GizmoMode::Translate, input);
        }
        else
        {
            m_gizmoMode = GizmoMode::Translate;
        }
    }
    if (input.IsKeyPressed(GLFW_KEY_R))
    {
        const bool hasSelection = HasSceneSelection() || HasMeshSelection();
        if (hasSelection)
        {
            BeginModalTransform(GizmoMode::Rotate, input);
        }
        else if (m_mode == Mode::MapEditor && !editMode)
        {
            m_pendingPlacementRotation = (m_pendingPlacementRotation + 90) % 360;
        }
        else
        {
            m_gizmoMode = GizmoMode::Rotate;
        }
    }
    if (input.IsKeyPressed(GLFW_KEY_S))
    {
        if (HasSceneSelection() || HasMeshSelection())
        {
            BeginModalTransform(GizmoMode::Scale, input);
        }
        else
        {
            m_gizmoMode = GizmoMode::Scale;
        }
    }
    if (!ctrlDown && input.IsKeyPressed(GLFW_KEY_B) && sceneMouseActive)
    {
        m_boxSelection.active = true;
        m_boxSelection.dragging = false;
        m_boxSelection.start = input.MousePosition();
        m_boxSelection.end = m_boxSelection.start;
        m_statusLine = "Box select: hold LMB and drag";
    }

    if (m_mode == Mode::LoopEditor)
    {
        if (input.IsKeyPressed(GLFW_KEY_4))
        {
            m_meshEditMode = MeshEditMode::Face;
        }
        if (input.IsKeyPressed(GLFW_KEY_5))
        {
            m_meshEditMode = MeshEditMode::Edge;
        }
        if (input.IsKeyPressed(GLFW_KEY_6))
        {
            m_meshEditMode = MeshEditMode::Vertex;
        }
        if (input.IsKeyPressed(GLFW_KEY_M))
        {
            ToggleMeshObjectEditMode("M");
        }
        if (m_meshModelSceneEditEnabled && m_meshEditMode == MeshEditMode::Face && input.IsKeyPressed(GLFW_KEY_E))
        {
            int faceToExtrude = m_meshModelSelectedFace;
            if ((faceToExtrude < 0 || faceToExtrude >= static_cast<int>(m_meshModelFaces.size())) &&
                !m_meshModelFaceSelection.empty())
            {
                faceToExtrude = m_meshModelFaceSelection.back();
            }
            if (faceToExtrude >= 0 && faceToExtrude < static_cast<int>(m_meshModelFaces.size()))
            {
                PushHistorySnapshot();
                const float startDistance = glm::max(0.02F, m_meshModelExtrudeDistance * 0.08F);
                MeshModelerExtrudeFace(faceToExtrude, startDistance);
                BeginModalTransform(GizmoMode::Translate, input, false);
                m_statusLine = "Extrude modal active (mouse move, Enter/LMB confirm, Esc/RMB cancel)";
                return;
            }
        }
        if (m_meshEditMode == MeshEditMode::Edge && input.IsKeyPressed(GLFW_KEY_J))
        {
            MeshModelerExtrudeActiveEdges(m_meshModelExtrudeDistance);
        }
        if (m_meshEditMode == MeshEditMode::Edge && ctrlDown && input.IsKeyPressed(GLFW_KEY_B))
        {
            MeshModelerBevelActiveEdges(m_meshModelBevelDistance, m_meshModelBevelSegments);
        }
        if (m_meshEditMode == MeshEditMode::Edge && input.IsKeyPressed(GLFW_KEY_ENTER))
        {
            if (m_meshModelBatchOperation == MeshBatchEdgeOperation::Extrude)
            {
                MeshModelerExtrudeActiveEdges(m_meshModelBatchPreviewDistance);
            }
            else
            {
                MeshModelerBevelActiveEdges(m_meshModelBatchPreviewDistance, m_meshModelBevelSegments);
            }
        }
        if (m_meshEditMode == MeshEditMode::Edge && input.IsKeyPressed(GLFW_KEY_L))
        {
            MeshModelerLoopCutEdge(m_meshModelSelectedEdge, m_meshModelLoopCutRatio);
        }
        if (m_meshEditMode == MeshEditMode::Edge && input.IsKeyPressed(GLFW_KEY_U))
        {
            MeshModelerSelectEdgeLoop(m_meshModelSelectedEdge);
        }
        if (m_meshEditMode == MeshEditMode::Edge && input.IsKeyPressed(GLFW_KEY_I))
        {
            MeshModelerSelectEdgeRing(m_meshModelSelectedEdge);
        }
        if (input.IsKeyPressed(GLFW_KEY_K))
        {
            m_meshModelKnifeEnabled = !m_meshModelKnifeEnabled;
            m_meshModelKnifeHasFirstPoint = false;
            m_meshModelKnifeFaceIndex = -1;
            m_meshModelKnifeFirstPointLocal = glm::vec3{0.0F};
            m_meshModelKnifeFirstPointWorld = glm::vec3{0.0F};
            m_meshModelKnifePreviewValid = false;
            m_meshModelKnifePreviewWorld = glm::vec3{0.0F};
            m_meshModelKnifePreviewSegments.clear();
        }
        if (input.IsKeyPressed(GLFW_KEY_O))
        {
            m_meshModelLoopCutToolEnabled = !m_meshModelLoopCutToolEnabled;
        }
    }
    if (input.IsKeyPressed(GLFW_KEY_T))
    {
        m_topDownView = !m_topDownView;
    }
    if (input.IsKeyPressed(GLFW_KEY_F2))
    {
        m_debugView = !m_debugView;
    }
    if (input.IsKeyPressed(GLFW_KEY_F3))
    {
        m_pendingRenderMode =
            (m_currentRenderMode == engine::render::RenderMode::Wireframe)
                ? engine::render::RenderMode::Filled
                : engine::render::RenderMode::Wireframe;
    }
    if (input.IsKeyPressed(GLFW_KEY_P) && m_mode == Mode::MapEditor)
    {
        m_propPlacementMode = !m_propPlacementMode;
        if (m_propPlacementMode)
        {
            m_lightPlacementMode = false;
        }
    }
    if (input.IsKeyPressed(GLFW_KEY_L) && m_mode == Mode::MapEditor)
    {
        m_lightPlacementMode = !m_lightPlacementMode;
        if (m_lightPlacementMode)
        {
            m_propPlacementMode = false;
        }
    }

    if (ctrlDown && input.IsKeyPressed(GLFW_KEY_C))
    {
        CopyCurrentSelection();
        return;
    }
    if (ctrlDown && input.IsKeyPressed(GLFW_KEY_V))
    {
        PasteClipboard();
        return;
    }

    if (input.IsKeyPressed(GLFW_KEY_DELETE))
    {
        DeleteCurrentSelection();
    }

    if (ctrlDown && input.IsKeyPressed(GLFW_KEY_D))
    {
        DuplicateCurrentSelection();
    }
}

void LevelEditor::UpdateHoveredTile(const engine::platform::Input& input, int framebufferWidth, int framebufferHeight)
{
    m_hoveredTileValid = false;
    glm::vec3 rayOrigin{0.0F};
    glm::vec3 rayDirection{0.0F};
    if (!BuildMouseRay(input, framebufferWidth, framebufferHeight, &rayOrigin, &rayDirection))
    {
        m_meshModelKnifePreviewValid = false;
        m_meshModelKnifePreviewSegments.clear();
        return;
    }

    if (m_mode == Mode::LoopEditor && m_meshModelKnifeEnabled && m_meshModelKnifeHasFirstPoint)
    {
        int previewFace = -1;
        glm::vec3 previewPoint{0.0F};
        m_meshModelKnifePreviewValid = RaycastMeshModel(rayOrigin, rayDirection, &previewFace, &previewPoint);
        if (m_meshModelKnifePreviewValid)
        {
            m_meshModelKnifePreviewWorld = previewPoint;
            if (!BuildKnifePreviewSegments(
                    m_meshModelKnifeFirstPointWorld,
                    m_meshModelKnifePreviewWorld,
                    &m_meshModelKnifePreviewSegments))
            {
                m_meshModelKnifePreviewSegments.clear();
                m_meshModelKnifePreviewSegments.push_back(
                    std::pair<glm::vec3, glm::vec3>{m_meshModelKnifeFirstPointWorld, m_meshModelKnifePreviewWorld});
            }
        }
        else
        {
            m_meshModelKnifePreviewSegments.clear();
        }
    }
    if (m_mode == Mode::MapEditor)
    {
        m_meshModelKnifePreviewValid = false;
        m_meshModelKnifePreviewSegments.clear();
    }

    glm::vec3 hit{0.0F};
    if (!RayIntersectGround(rayOrigin, rayDirection, 0.0F, &hit))
    {
        return;
    }
    m_hoveredWorld = hit;

    const float halfWidth = static_cast<float>(m_map.width) * m_map.tileSize * 0.5F;
    const float halfHeight = static_cast<float>(m_map.height) * m_map.tileSize * 0.5F;
    const int tileX = static_cast<int>(std::floor((hit.x + halfWidth) / m_map.tileSize));
    const int tileY = static_cast<int>(std::floor((hit.z + halfHeight) / m_map.tileSize));

    if (tileX < 0 || tileY < 0 || tileX >= m_map.width || tileY >= m_map.height)
    {
        return;
    }

    m_hoveredTile = glm::ivec2{tileX, tileY};
    m_hoveredTileValid = true;
}

bool LevelEditor::BuildMouseRay(
    const engine::platform::Input& input,
    int framebufferWidth,
    int framebufferHeight,
    glm::vec3* outOrigin,
    glm::vec3* outDirection
) const
{
    if (framebufferWidth <= 0 || framebufferHeight <= 0 || outOrigin == nullptr || outDirection == nullptr)
    {
        return false;
    }

    const glm::vec2 mouse = input.MousePosition();
    const float x = (2.0F * mouse.x) / static_cast<float>(framebufferWidth) - 1.0F;
    const float y = 1.0F - (2.0F * mouse.y) / static_cast<float>(framebufferHeight);

    const float aspect = static_cast<float>(framebufferWidth) / static_cast<float>(framebufferHeight);
    const glm::mat4 projection = glm::perspective(glm::radians(60.0F), aspect, 0.05F, 900.0F);
    const glm::vec3 forward = CameraForward();
    const glm::mat4 view = glm::lookAt(m_cameraPosition, m_cameraPosition + forward, CameraUp());
    const glm::mat4 inv = glm::inverse(projection * view);

    const glm::vec4 nearClip = inv * glm::vec4{x, y, -1.0F, 1.0F};
    const glm::vec4 farClip = inv * glm::vec4{x, y, 1.0F, 1.0F};
    if (std::abs(nearClip.w) < 1.0e-6F || std::abs(farClip.w) < 1.0e-6F)
    {
        return false;
    }

    const glm::vec3 nearWorld = glm::vec3(nearClip) / nearClip.w;
    const glm::vec3 farWorld = glm::vec3(farClip) / farClip.w;
    const glm::vec3 direction = farWorld - nearWorld;
    if (glm::length(direction) < 1.0e-6F)
    {
        return false;
    }

    *outOrigin = nearWorld;
    *outDirection = glm::normalize(direction);
    return true;
}

bool LevelEditor::RayIntersectGround(
    const glm::vec3& rayOrigin,
    const glm::vec3& rayDirection,
    float groundY,
    glm::vec3* outHit
) const
{
    if (std::abs(rayDirection.y) < 1.0e-6F)
    {
        return false;
    }

    const float t = (groundY - rayOrigin.y) / rayDirection.y;
    if (t < 0.0F)
    {
        return false;
    }

    if (outHit != nullptr)
    {
        *outHit = rayOrigin + rayDirection * t;
    }
    return true;
}

LevelEditor::Selection LevelEditor::PickSelection(const glm::vec3& rayOrigin, const glm::vec3& rayDirection) const
{
    float bestT = 1.0e9F;
    Selection best{};

    if (m_mode == Mode::LoopEditor)
    {
        for (int i = 0; i < static_cast<int>(m_loop.elements.size()); ++i)
        {
            const LoopElement& element = m_loop.elements[static_cast<std::size_t>(i)];
            const glm::vec3 pickExtents = RotateExtentsXYZ(element.halfExtents, ElementRotation(element));
            const glm::vec3 minBounds = element.position - pickExtents;
            const glm::vec3 maxBounds = element.position + pickExtents;
            float t = 0.0F;
            if (!SegmentIntersectsAabb(rayOrigin, rayDirection, minBounds, maxBounds, &t))
            {
                continue;
            }
            if (t < bestT)
            {
                bestT = t;
                best.kind = SelectionKind::LoopElement;
                best.index = i;
            }
        }
        return best;
    }

    for (int i = 0; i < static_cast<int>(m_map.props.size()); ++i)
    {
        const PropInstance& prop = m_map.props[static_cast<std::size_t>(i)];
        const glm::vec3 extents = RotateExtentsXYZ(prop.halfExtents, PropRotation(prop));
        const glm::vec3 minBounds = prop.position - extents;
        const glm::vec3 maxBounds = prop.position + extents;
        float t = 0.0F;
        if (!SegmentIntersectsAabb(rayOrigin, rayDirection, minBounds, maxBounds, &t))
        {
            continue;
        }
        if (t < bestT)
        {
            bestT = t;
            best.kind = SelectionKind::Prop;
            best.index = i;
        }
    }

    for (int i = 0; i < static_cast<int>(m_map.placements.size()); ++i)
    {
        const LoopPlacement& placement = m_map.placements[static_cast<std::size_t>(i)];
        LoopAsset loop;
        std::string error;
        if (!LevelAssetIO::LoadLoop(placement.loopId, &loop, &error))
        {
            continue;
        }
        const glm::ivec2 footprint = RotatedFootprintFor(loop, placement.rotationDegrees);
        const glm::vec3 center = TileCenter(placement.tileX, placement.tileY) +
                                 glm::vec3{
                                     (static_cast<float>(footprint.x) - 1.0F) * m_map.tileSize * 0.5F,
                                     1.0F,
                                     (static_cast<float>(footprint.y) - 1.0F) * m_map.tileSize * 0.5F,
                                 };
        const glm::vec3 extents{
            static_cast<float>(footprint.x) * m_map.tileSize * 0.5F,
            2.0F,
            static_cast<float>(footprint.y) * m_map.tileSize * 0.5F,
        };

        float t = 0.0F;
        if (!SegmentIntersectsAabb(rayOrigin, rayDirection, center - extents, center + extents, &t))
        {
            continue;
        }
        if (t < bestT)
        {
            bestT = t;
            best.kind = SelectionKind::MapPlacement;
            best.index = i;
        }
    }

    return best;
}

glm::vec3 LevelEditor::SelectionPivot() const
{
    if (m_selection.kind == SelectionKind::LoopElement)
    {
        const std::vector<int> indices = SortedUniqueValidSelection(SelectionKind::LoopElement);
        if (indices.empty())
        {
            return glm::vec3{0.0F};
        }

        glm::vec3 pivot{0.0F};
        for (int idx : indices)
        {
            pivot += m_loop.elements[static_cast<std::size_t>(idx)].position;
        }
        return pivot / static_cast<float>(indices.size());
    }

    if (m_selection.kind == SelectionKind::MapPlacement)
    {
        const std::vector<int> indices = SortedUniqueValidSelection(SelectionKind::MapPlacement);
        if (indices.empty())
        {
            return glm::vec3{0.0F};
        }

        glm::vec3 pivot{0.0F};
        int validCount = 0;
        for (int idx : indices)
        {
            if (idx < 0 || idx >= static_cast<int>(m_map.placements.size()))
            {
                continue;
            }
            const LoopPlacement& placement = m_map.placements[static_cast<std::size_t>(idx)];
            LoopAsset loop;
            std::string error;
            if (!LevelAssetIO::LoadLoop(placement.loopId, &loop, &error))
            {
                continue;
            }
            const glm::ivec2 footprint = RotatedFootprint(loop, placement.rotationDegrees);
            pivot += TileCenter(placement.tileX, placement.tileY) +
                     glm::vec3{
                         (static_cast<float>(footprint.x) - 1.0F) * m_map.tileSize * 0.5F,
                         0.0F,
                         (static_cast<float>(footprint.y) - 1.0F) * m_map.tileSize * 0.5F,
                     };
            ++validCount;
        }
        if (validCount == 0)
        {
            return glm::vec3{0.0F};
        }
        return pivot / static_cast<float>(validCount);
    }

    if (m_selection.kind == SelectionKind::Prop)
    {
        const std::vector<int> indices = SortedUniqueValidSelection(SelectionKind::Prop);
        if (indices.empty())
        {
            return glm::vec3{0.0F};
        }

        glm::vec3 pivot{0.0F};
        for (int idx : indices)
        {
            pivot += m_map.props[static_cast<std::size_t>(idx)].position;
        }
        return pivot / static_cast<float>(indices.size());
    }

    return glm::vec3{0.0F};
}

bool LevelEditor::RayIntersectPlane(
    const glm::vec3& rayOrigin,
    const glm::vec3& rayDirection,
    const glm::vec3& planePoint,
    const glm::vec3& planeNormal,
    glm::vec3* outHit
) const
{
    const float denom = glm::dot(rayDirection, planeNormal);
    if (std::abs(denom) < 1.0e-6F)
    {
        return false;
    }

    const float t = glm::dot(planePoint - rayOrigin, planeNormal) / denom;
    if (t < 0.0F)
    {
        return false;
    }

    if (outHit != nullptr)
    {
        *outHit = rayOrigin + rayDirection * t;
    }
    return true;
}

bool LevelEditor::StartAxisDrag(const glm::vec3& rayOrigin, const glm::vec3& rayDirection)
{
    if (m_selection.kind == SelectionKind::None)
    {
        return false;
    }
    if (m_selection.kind == SelectionKind::MapPlacement && m_gizmoMode != GizmoMode::Translate)
    {
        return false;
    }

    auto hasUnlocked = [&]() {
        if (m_selection.kind == SelectionKind::LoopElement)
        {
            const std::vector<int> indices = SortedUniqueValidSelection(SelectionKind::LoopElement);
            for (int idx : indices)
            {
                if (!m_loop.elements[static_cast<std::size_t>(idx)].transformLocked)
                {
                    return true;
                }
            }
            return false;
        }
        if (m_selection.kind == SelectionKind::MapPlacement)
        {
            const std::vector<int> indices = SortedUniqueValidSelection(SelectionKind::MapPlacement);
            for (int idx : indices)
            {
                if (!m_map.placements[static_cast<std::size_t>(idx)].transformLocked)
                {
                    return true;
                }
            }
            return false;
        }
        if (m_selection.kind == SelectionKind::Prop)
        {
            const std::vector<int> indices = SortedUniqueValidSelection(SelectionKind::Prop);
            for (int idx : indices)
            {
                if (!m_map.props[static_cast<std::size_t>(idx)].transformLocked)
                {
                    return true;
                }
            }
            return false;
        }
        return false;
    };

    if (!hasUnlocked())
    {
        m_statusLine = "Selection is transform-locked";
        return false;
    }

    const glm::vec3 pivot = SelectionPivot();
    const float cameraDistance = glm::length(m_cameraPosition - pivot);
    const float axisLength = glm::clamp(cameraDistance * 0.18F, 1.8F, 10.0F);
    const float handleHalf = glm::max(0.38F, axisLength * 0.17F);
    const glm::vec3 axisDirections[3] = {
        glm::vec3{1.0F, 0.0F, 0.0F},
        glm::vec3{0.0F, 1.0F, 0.0F},
        glm::vec3{0.0F, 0.0F, 1.0F},
    };

    float bestT = 1.0e9F;
    GizmoAxis bestAxis = GizmoAxis::None;
    glm::vec3 bestDirection{0.0F};
    for (int axisIndex = 0; axisIndex < 3; ++axisIndex)
    {
        if (m_selection.kind == SelectionKind::MapPlacement && axisIndex == 1)
        {
            continue;
        }
        const glm::vec3 direction = axisDirections[axisIndex];
        const glm::vec3 tip = pivot + direction * axisLength;
        const glm::vec3 minBounds = tip - glm::vec3{handleHalf};
        const glm::vec3 maxBounds = tip + glm::vec3{handleHalf};
        float t = 0.0F;
        if (!SegmentIntersectsAabb(rayOrigin, rayDirection, minBounds, maxBounds, &t))
        {
            continue;
        }
        if (t < bestT)
        {
            bestT = t;
            bestDirection = direction;
            bestAxis = axisIndex == 0 ? GizmoAxis::X : (axisIndex == 1 ? GizmoAxis::Y : GizmoAxis::Z);
        }
    }

    if (bestAxis == GizmoAxis::None)
    {
        return false;
    }

    glm::vec3 planeNormal{0.0F};
    if (m_gizmoMode == GizmoMode::Rotate)
    {
        planeNormal = bestDirection;
    }
    else
    {
        const glm::vec3 forward = CameraForward();
        planeNormal = glm::cross(bestDirection, forward);
        if (glm::length(planeNormal) < 1.0e-4F)
        {
            planeNormal = glm::cross(bestDirection, glm::vec3{0.0F, 1.0F, 0.0F});
        }
        if (glm::length(planeNormal) < 1.0e-4F)
        {
            planeNormal = glm::cross(bestDirection, glm::vec3{1.0F, 0.0F, 0.0F});
        }
        if (glm::length(planeNormal) < 1.0e-4F)
        {
            return false;
        }
        planeNormal = glm::normalize(planeNormal);
    }

    glm::vec3 hit{0.0F};
    if (!RayIntersectPlane(rayOrigin, rayDirection, pivot, planeNormal, &hit))
    {
        return false;
    }

    m_axisDragActive = true;
    m_axisDragAxis = bestAxis;
    m_axisDragPivot = pivot;
    m_axisDragDirection = bestDirection;
    m_axisDragPlaneNormal = planeNormal;
    m_axisDragMode = m_gizmoMode;
    if (m_axisDragMode == GizmoMode::Rotate)
    {
        glm::vec3 startVector = hit - pivot;
        startVector -= bestDirection * glm::dot(startVector, bestDirection);
        if (glm::length(startVector) < 1.0e-4F)
        {
            return false;
        }
        startVector = glm::normalize(startVector);
        m_axisDragLastVector = startVector;
        m_axisDragStartScalar = 0.0F;
        m_axisDragLastScalar = 0.0F;
    }
    else
    {
        m_axisDragStartScalar = glm::dot(hit - pivot, bestDirection);
        m_axisDragLastScalar = m_axisDragStartScalar;
        m_axisDragLastVector = glm::vec3{1.0F, 0.0F, 0.0F};
    }
    PushHistorySnapshot();
    m_gizmoEditing = true;
    const char* axisText = bestAxis == GizmoAxis::X ? "X" : (bestAxis == GizmoAxis::Y ? "Y" : "Z");
    m_statusLine = std::string{"Gizmo drag: "} + GizmoToText(m_gizmoMode) + " axis " + axisText;
    return true;
}

void LevelEditor::UpdateAxisDrag(const glm::vec3& rayOrigin, const glm::vec3& rayDirection)
{
    if (!m_axisDragActive || m_axisDragAxis == GizmoAxis::None)
    {
        return;
    }

    glm::vec3 hit{0.0F};
    if (!RayIntersectPlane(rayOrigin, rayDirection, m_axisDragPivot, m_axisDragPlaneNormal, &hit))
    {
        return;
    }

    const float scalar = glm::dot(hit - m_axisDragPivot, m_axisDragDirection);
    const float previousScalar = m_axisDragLastScalar;

    float delta = 0.0F;
    if (m_axisDragMode == GizmoMode::Rotate)
    {
        glm::vec3 currentVector = hit - m_axisDragPivot;
        currentVector -= m_axisDragDirection * glm::dot(currentVector, m_axisDragDirection);
        if (glm::length(currentVector) < 1.0e-4F)
        {
            return;
        }
        currentVector = glm::normalize(currentVector);
        const glm::vec3 previousVector = m_axisDragLastVector;
        const float sinTerm = glm::dot(m_axisDragDirection, glm::cross(previousVector, currentVector));
        const float cosTerm = glm::dot(previousVector, currentVector);
        const float deltaDegreesRaw = glm::degrees(std::atan2(sinTerm, cosTerm));
        float appliedDegrees = deltaDegreesRaw;
        if (m_angleSnap)
        {
            const float stepDegrees = std::max(1.0F, m_angleStepDegrees);
            const float accumulatedNow = m_axisDragLastScalar + deltaDegreesRaw;
            const float snappedNow = std::round(accumulatedNow / stepDegrees) * stepDegrees;
            const float snappedBefore = std::round(m_axisDragLastScalar / stepDegrees) * stepDegrees;
            appliedDegrees = snappedNow - snappedBefore;
            m_axisDragLastScalar = accumulatedNow;
        }
        else
        {
            m_axisDragLastScalar += deltaDegreesRaw;
        }
        m_axisDragLastVector = currentVector;
        if (std::abs(appliedDegrees) < 1.0e-6F)
        {
            return;
        }

        auto applyRotationDelta = [&](LoopElement& element) {
            if (element.transformLocked)
            {
                return;
            }
            if (m_axisDragAxis == GizmoAxis::X)
            {
                element.pitchDegrees += appliedDegrees;
            }
            else if (m_axisDragAxis == GizmoAxis::Y)
            {
                element.yawDegrees += appliedDegrees;
            }
            else if (m_axisDragAxis == GizmoAxis::Z)
            {
                element.rollDegrees += appliedDegrees;
            }
        };
        auto applyRotationDeltaProp = [&](PropInstance& prop) {
            if (prop.transformLocked)
            {
                return;
            }
            if (m_axisDragAxis == GizmoAxis::X)
            {
                prop.pitchDegrees += appliedDegrees;
            }
            else if (m_axisDragAxis == GizmoAxis::Y)
            {
                prop.yawDegrees += appliedDegrees;
            }
            else if (m_axisDragAxis == GizmoAxis::Z)
            {
                prop.rollDegrees += appliedDegrees;
            }
        };

        if (m_selection.kind == SelectionKind::LoopElement)
        {
            const std::vector<int> indices = SortedUniqueValidSelection(SelectionKind::LoopElement);
            for (int idx : indices)
            {
                applyRotationDelta(m_loop.elements[static_cast<std::size_t>(idx)]);
            }
            AutoComputeLoopBoundsAndFootprint();
        }
        else if (m_selection.kind == SelectionKind::Prop)
        {
            const std::vector<int> indices = SortedUniqueValidSelection(SelectionKind::Prop);
            for (int idx : indices)
            {
                applyRotationDeltaProp(m_map.props[static_cast<std::size_t>(idx)]);
            }
        }
        return;
    }

    if (m_axisDragMode == GizmoMode::Translate)
    {
        if (m_selection.kind == SelectionKind::MapPlacement)
        {
            const int tileDelta = static_cast<int>(std::round((scalar - previousScalar) / m_map.tileSize));
            if (tileDelta == 0)
            {
                m_axisDragLastScalar = scalar;
                return;
            }
            const std::vector<int> indices = SortedUniqueValidSelection(SelectionKind::MapPlacement);
            for (int idx : indices)
            {
                LoopPlacement& placement = m_map.placements[static_cast<std::size_t>(idx)];
                if (placement.transformLocked)
                {
                    continue;
                }
                const int nextX = placement.tileX + (m_axisDragAxis == GizmoAxis::X ? tileDelta : 0);
                const int nextY = placement.tileY + (m_axisDragAxis == GizmoAxis::Z ? tileDelta : 0);
                if (CanPlaceLoopAt(nextX, nextY, placement.rotationDegrees, idx))
                {
                    placement.tileX = nextX;
                    placement.tileY = nextY;
                }
            }
            m_axisDragLastScalar = scalar;
            return;
        }

        delta = scalar - previousScalar;
        if (m_gridSnap)
        {
            const float step = std::max(0.1F, m_gridStep);
            const float snappedNow = std::round((scalar - m_axisDragStartScalar) / step) * step;
            const float snappedBefore = std::round((previousScalar - m_axisDragStartScalar) / step) * step;
            delta = snappedNow - snappedBefore;
        }
        if (std::abs(delta) < 1.0e-6F)
        {
            m_axisDragLastScalar = scalar;
            return;
        }

        const glm::vec3 move = m_axisDragDirection * delta;
        if (m_selection.kind == SelectionKind::LoopElement)
        {
            const std::vector<int> indices = SortedUniqueValidSelection(SelectionKind::LoopElement);
            for (int idx : indices)
            {
                LoopElement& element = m_loop.elements[static_cast<std::size_t>(idx)];
                if (!element.transformLocked)
                {
                    element.position += move;
                }
            }
            AutoComputeLoopBoundsAndFootprint();
        }
        else if (m_selection.kind == SelectionKind::Prop)
        {
            const std::vector<int> indices = SortedUniqueValidSelection(SelectionKind::Prop);
            for (int idx : indices)
            {
                PropInstance& prop = m_map.props[static_cast<std::size_t>(idx)];
                if (!prop.transformLocked)
                {
                    prop.position += move;
                }
            }
        }
        m_axisDragLastScalar = scalar;
        return;
    }

    if (m_axisDragMode == GizmoMode::Scale)
    {
        const int axisComponent = m_axisDragAxis == GizmoAxis::X ? 0 : (m_axisDragAxis == GizmoAxis::Y ? 1 : 2);
        delta = scalar - previousScalar;

        if (m_gridSnap)
        {
            const float step = std::max(0.1F, m_gridStep);
            const float snappedNow = std::round((scalar - m_axisDragStartScalar) / step) * step;
            const float snappedBefore = std::round((previousScalar - m_axisDragStartScalar) / step) * step;
            delta = snappedNow - snappedBefore;
        }
        if (std::abs(delta) < 1.0e-6F)
        {
            m_axisDragLastScalar = scalar;
            return;
        }

        const float scaleDelta = delta * 0.35F;
        if (m_selection.kind == SelectionKind::LoopElement)
        {
            const std::vector<int> indices = SortedUniqueValidSelection(SelectionKind::LoopElement);
            for (int idx : indices)
            {
                LoopElement& element = m_loop.elements[static_cast<std::size_t>(idx)];
                if (!element.transformLocked)
                {
                    element.halfExtents[axisComponent] = std::max(0.05F, element.halfExtents[axisComponent] + scaleDelta);
                }
            }
            AutoComputeLoopBoundsAndFootprint();
        }
        else if (m_selection.kind == SelectionKind::Prop)
        {
            const std::vector<int> indices = SortedUniqueValidSelection(SelectionKind::Prop);
            for (int idx : indices)
            {
                PropInstance& prop = m_map.props[static_cast<std::size_t>(idx)];
                if (!prop.transformLocked)
                {
                    prop.halfExtents[axisComponent] = std::max(0.05F, prop.halfExtents[axisComponent] + scaleDelta);
                }
            }
        }
        m_axisDragLastScalar = scalar;
    }
}

void LevelEditor::StopAxisDrag()
{
    m_axisDragActive = false;
    m_axisDragAxis = GizmoAxis::None;
    m_axisDragMode = GizmoMode::Translate;
    m_axisDragDirection = glm::vec3{1.0F, 0.0F, 0.0F};
    m_axisDragPlaneNormal = glm::vec3{0.0F, 1.0F, 0.0F};
    m_axisDragLastVector = glm::vec3{1.0F, 0.0F, 0.0F};
    m_gizmoEditing = false;
}

void LevelEditor::ApplyGizmoInput(const engine::platform::Input& input, float deltaSeconds)
{
    if (m_selection.kind == SelectionKind::None)
    {
        m_gizmoEditing = false;
        return;
    }

#if BUILD_WITH_IMGUI
    if (ImGui::GetIO().WantCaptureKeyboard)
    {
        m_gizmoEditing = false;
        return;
    }
#endif

    const float moveStep = m_gridSnap ? std::max(0.1F, m_gridStep) : std::max(0.05F, 4.0F * deltaSeconds);
    const float angleStep = m_angleSnap ? std::max(1.0F, m_angleStepDegrees) : (75.0F * deltaSeconds);

    if (m_selection.kind == SelectionKind::LoopElement)
    {
        const std::vector<int> indices = SortedUniqueValidSelection(SelectionKind::LoopElement);
        if (indices.empty())
        {
            m_gizmoEditing = false;
            return;
        }

        const bool translateHeld = input.IsKeyDown(GLFW_KEY_LEFT) ||
                                   input.IsKeyDown(GLFW_KEY_RIGHT) ||
                                   input.IsKeyDown(GLFW_KEY_UP) ||
                                   input.IsKeyDown(GLFW_KEY_DOWN) ||
                                   input.IsKeyDown(GLFW_KEY_PAGE_UP) ||
                                   input.IsKeyDown(GLFW_KEY_PAGE_DOWN);
        const bool rotateHeld = input.IsKeyDown(GLFW_KEY_LEFT_BRACKET) ||
                                input.IsKeyDown(GLFW_KEY_RIGHT_BRACKET);
        const bool scaleHeld = input.IsKeyDown(GLFW_KEY_EQUAL) ||
                               input.IsKeyDown(GLFW_KEY_MINUS);
        const bool activeEdit =
            (m_gizmoMode == GizmoMode::Translate && translateHeld) ||
            (m_gizmoMode == GizmoMode::Rotate && rotateHeld) ||
            (m_gizmoMode == GizmoMode::Scale && scaleHeld);

        if (!activeEdit)
        {
            m_gizmoEditing = false;
            return;
        }
        if (!m_gizmoEditing)
        {
            PushHistorySnapshot();
            m_gizmoEditing = true;
        }

        for (int idx : indices)
        {
            LoopElement& element = m_loop.elements[static_cast<std::size_t>(idx)];
            if (element.transformLocked)
            {
                continue;
            }
            if (m_gizmoMode == GizmoMode::Translate)
            {
                if (input.IsKeyDown(GLFW_KEY_LEFT))
                {
                    element.position.x -= moveStep;
                }
                if (input.IsKeyDown(GLFW_KEY_RIGHT))
                {
                    element.position.x += moveStep;
                }
                if (input.IsKeyDown(GLFW_KEY_UP))
                {
                    element.position.z -= moveStep;
                }
                if (input.IsKeyDown(GLFW_KEY_DOWN))
                {
                    element.position.z += moveStep;
                }
                if (input.IsKeyDown(GLFW_KEY_PAGE_UP))
                {
                    element.position.y += moveStep;
                }
                if (input.IsKeyDown(GLFW_KEY_PAGE_DOWN))
                {
                    element.position.y -= moveStep;
                }
            }
            else if (m_gizmoMode == GizmoMode::Rotate)
            {
                if (input.IsKeyDown(GLFW_KEY_LEFT_BRACKET))
                {
                    element.yawDegrees -= angleStep;
                }
                if (input.IsKeyDown(GLFW_KEY_RIGHT_BRACKET))
                {
                    element.yawDegrees += angleStep;
                }
            }
            else if (m_gizmoMode == GizmoMode::Scale)
            {
                if (input.IsKeyDown(GLFW_KEY_EQUAL))
                {
                    element.halfExtents += glm::vec3{moveStep * 0.5F};
                }
                if (input.IsKeyDown(GLFW_KEY_MINUS))
                {
                    element.halfExtents -= glm::vec3{moveStep * 0.5F};
                    element.halfExtents = glm::max(element.halfExtents, glm::vec3{0.05F});
                }
            }
        }
        AutoComputeLoopBoundsAndFootprint();
        return;
    }

    if (m_selection.kind == SelectionKind::MapPlacement)
    {
        m_gizmoEditing = false;
        const std::vector<int> indices = SortedUniqueValidSelection(SelectionKind::MapPlacement);
        if (indices.empty())
        {
            return;
        }

        if (m_gizmoMode == GizmoMode::Translate)
        {
            int dx = 0;
            int dy = 0;
            if (input.IsKeyPressed(GLFW_KEY_LEFT))
            {
                dx -= 1;
            }
            if (input.IsKeyPressed(GLFW_KEY_RIGHT))
            {
                dx += 1;
            }
            if (input.IsKeyPressed(GLFW_KEY_UP))
            {
                dy -= 1;
            }
            if (input.IsKeyPressed(GLFW_KEY_DOWN))
            {
                dy += 1;
            }

            if (dx != 0 || dy != 0)
            {
                PushHistorySnapshot();
                for (int idx : indices)
                {
                    LoopPlacement& placement = m_map.placements[static_cast<std::size_t>(idx)];
                    const int newX = placement.tileX + dx;
                    const int newY = placement.tileY + dy;
                if (CanPlaceLoopAt(newX, newY, placement.rotationDegrees, idx))
                {
                    if (!placement.transformLocked)
                    {
                        placement.tileX = newX;
                        placement.tileY = newY;
                    }
                }
            }
            }
        }
        else if (m_gizmoMode == GizmoMode::Rotate)
        {
            int rotationDelta = 0;
            if (input.IsKeyPressed(GLFW_KEY_LEFT_BRACKET))
            {
                rotationDelta -= 90;
            }
            if (input.IsKeyPressed(GLFW_KEY_RIGHT_BRACKET))
            {
                rotationDelta += 90;
            }
            if (rotationDelta != 0)
            {
                PushHistorySnapshot();
                for (int idx : indices)
                {
                    LoopPlacement& placement = m_map.placements[static_cast<std::size_t>(idx)];
                    int nextRot = ((placement.rotationDegrees + rotationDelta) % 360 + 360) % 360;
                    if (!placement.transformLocked && CanPlaceLoopAt(placement.tileX, placement.tileY, nextRot, idx))
                    {
                        placement.rotationDegrees = nextRot;
                    }
                }
            }
        }
        return;
    }

    if (m_selection.kind == SelectionKind::Prop)
    {
        const std::vector<int> indices = SortedUniqueValidSelection(SelectionKind::Prop);
        if (indices.empty())
        {
            m_gizmoEditing = false;
            return;
        }

        const bool translateHeld = input.IsKeyDown(GLFW_KEY_LEFT) ||
                                   input.IsKeyDown(GLFW_KEY_RIGHT) ||
                                   input.IsKeyDown(GLFW_KEY_UP) ||
                                   input.IsKeyDown(GLFW_KEY_DOWN) ||
                                   input.IsKeyDown(GLFW_KEY_PAGE_UP) ||
                                   input.IsKeyDown(GLFW_KEY_PAGE_DOWN);
        const bool rotateHeld = input.IsKeyDown(GLFW_KEY_LEFT_BRACKET) ||
                                input.IsKeyDown(GLFW_KEY_RIGHT_BRACKET);
        const bool scaleHeld = input.IsKeyDown(GLFW_KEY_EQUAL) ||
                               input.IsKeyDown(GLFW_KEY_MINUS);
        const bool activeEdit =
            (m_gizmoMode == GizmoMode::Translate && translateHeld) ||
            (m_gizmoMode == GizmoMode::Rotate && rotateHeld) ||
            (m_gizmoMode == GizmoMode::Scale && scaleHeld);

        if (!activeEdit)
        {
            m_gizmoEditing = false;
            return;
        }
        if (!m_gizmoEditing)
        {
            PushHistorySnapshot();
            m_gizmoEditing = true;
        }

        for (int idx : indices)
        {
            PropInstance& prop = m_map.props[static_cast<std::size_t>(idx)];
            if (prop.transformLocked)
            {
                continue;
            }
            if (m_gizmoMode == GizmoMode::Translate)
            {
                if (input.IsKeyDown(GLFW_KEY_LEFT))
                {
                    prop.position.x -= moveStep;
                }
                if (input.IsKeyDown(GLFW_KEY_RIGHT))
                {
                    prop.position.x += moveStep;
                }
                if (input.IsKeyDown(GLFW_KEY_UP))
                {
                    prop.position.z -= moveStep;
                }
                if (input.IsKeyDown(GLFW_KEY_DOWN))
                {
                    prop.position.z += moveStep;
                }
                if (input.IsKeyDown(GLFW_KEY_PAGE_UP))
                {
                    prop.position.y += moveStep;
                }
                if (input.IsKeyDown(GLFW_KEY_PAGE_DOWN))
                {
                    prop.position.y -= moveStep;
                }
            }
            else if (m_gizmoMode == GizmoMode::Rotate)
            {
                if (input.IsKeyDown(GLFW_KEY_LEFT_BRACKET))
                {
                    prop.yawDegrees -= angleStep;
                }
                if (input.IsKeyDown(GLFW_KEY_RIGHT_BRACKET))
                {
                    prop.yawDegrees += angleStep;
                }
            }
            else if (m_gizmoMode == GizmoMode::Scale)
            {
                if (input.IsKeyDown(GLFW_KEY_EQUAL))
                {
                    prop.halfExtents += glm::vec3{moveStep * 0.35F};
                }
                if (input.IsKeyDown(GLFW_KEY_MINUS))
                {
                    prop.halfExtents -= glm::vec3{moveStep * 0.35F};
                    prop.halfExtents = glm::max(prop.halfExtents, glm::vec3{0.05F});
                }
            }
        }
    }
}

glm::ivec2 LevelEditor::RotatedFootprint(const LoopAsset& loop, int rotationDegrees) const
{
    return RotatedFootprintFor(loop, rotationDegrees);
}

bool LevelEditor::CanPlaceLoopAt(int tileX, int tileY, int rotationDegrees, int ignoredPlacement) const
{
    if (m_paletteLoopIndex < 0 || m_paletteLoopIndex >= static_cast<int>(m_loopLibrary.size()))
    {
        return false;
    }

    LoopAsset selectedLoop;
    std::string error;
    if (!LevelAssetIO::LoadLoop(m_loopLibrary[static_cast<std::size_t>(m_paletteLoopIndex)], &selectedLoop, &error))
    {
        return false;
    }
    const glm::ivec2 newFootprint = RotatedFootprint(selectedLoop, rotationDegrees);
    if (tileX < 0 || tileY < 0 || tileX + newFootprint.x > m_map.width || tileY + newFootprint.y > m_map.height)
    {
        return false;
    }

    auto overlapRect = [](int ax, int ay, int aw, int ah, int bx, int by, int bw, int bh) {
        return ax < bx + bw && ax + aw > bx && ay < by + bh && ay + ah > by;
    };

    for (int i = 0; i < static_cast<int>(m_map.placements.size()); ++i)
    {
        if (i == ignoredPlacement)
        {
            continue;
        }

        const LoopPlacement& existing = m_map.placements[static_cast<std::size_t>(i)];
        LoopAsset existingLoop;
        if (!LevelAssetIO::LoadLoop(existing.loopId, &existingLoop, &error))
        {
            continue;
        }
        const glm::ivec2 existingFootprint = RotatedFootprint(existingLoop, existing.rotationDegrees);
        if (overlapRect(
                tileX,
                tileY,
                newFootprint.x,
                newFootprint.y,
                existing.tileX,
                existing.tileY,
                existingFootprint.x,
                existingFootprint.y))
        {
            return false;
        }
    }
    return true;
}

glm::vec3 LevelEditor::TileCenter(int tileX, int tileY) const
{
    const float halfWidth = static_cast<float>(m_map.width) * m_map.tileSize * 0.5F;
    const float halfHeight = static_cast<float>(m_map.height) * m_map.tileSize * 0.5F;
    return glm::vec3{
        -halfWidth + m_map.tileSize * 0.5F + static_cast<float>(tileX) * m_map.tileSize,
        0.0F,
        -halfHeight + m_map.tileSize * 0.5F + static_cast<float>(tileY) * m_map.tileSize,
    };
}

void LevelEditor::PlaceLoopAtHoveredTile()
{
    if (!m_hoveredTileValid || m_paletteLoopIndex < 0 || m_paletteLoopIndex >= static_cast<int>(m_loopLibrary.size()))
    {
        return;
    }

    if (!CanPlaceLoopAt(m_hoveredTile.x, m_hoveredTile.y, m_pendingPlacementRotation, -1))
    {
        m_statusLine = "Placement invalid (overlap or out of bounds)";
        return;
    }

    LoopPlacement placement;
    placement.loopId = m_loopLibrary[static_cast<std::size_t>(m_paletteLoopIndex)];
    placement.tileX = m_hoveredTile.x;
    placement.tileY = m_hoveredTile.y;
    placement.rotationDegrees = m_pendingPlacementRotation;
    PushHistorySnapshot();
    m_map.placements.push_back(placement);
    SelectSingle(Selection{SelectionKind::MapPlacement, static_cast<int>(m_map.placements.size()) - 1});
    m_statusLine = "Placed loop " + placement.loopId;
}

bool LevelEditor::EnsureQuickLoopAsset(LoopElementType type, std::string* outLoopId)
{
    LoopAsset quickLoop;
    quickLoop.id = QuickLoopAssetId(type);
    quickLoop.displayName = std::string{"Quick "} + LoopElementTypeToText(type);
    quickLoop.manualBounds = true;
    quickLoop.manualFootprint = true;
    quickLoop.footprintWidth = 1;
    quickLoop.footprintHeight = 1;
    quickLoop.boundsMin = glm::vec3{-8.0F, 0.0F, -8.0F};
    quickLoop.boundsMax = glm::vec3{8.0F, 2.5F, 8.0F};

    LoopElement element;
    element.type = type;
    element.name = std::string{LoopElementTypeToText(type)} + "_1";
    element.position = glm::vec3{0.0F, type == LoopElementType::Marker ? 0.35F : 1.0F, 0.0F};
    element.halfExtents = QuickLoopDefaultHalfExtents(type);
    if (type == LoopElementType::Pallet)
    {
        element.position.y = 0.85F;
    }
    if (type == LoopElementType::Marker)
    {
        element.markerTag = "generic_marker";
    }
    quickLoop.elements.push_back(element);

    std::string error;
    if (!LevelAssetIO::SaveLoop(quickLoop, &error))
    {
        m_statusLine = "Quick loop save failed: " + error;
        return false;
    }

    RefreshLibraries();
    if (outLoopId != nullptr)
    {
        *outLoopId = quickLoop.id;
    }
    return true;
}

void LevelEditor::PlaceQuickLoopObjectAtHovered(LoopElementType type)
{
    if (m_mode != Mode::MapEditor || !m_hoveredTileValid)
    {
        m_statusLine = "Quick loop placement requires Map Editor + hovered tile";
        return;
    }

    std::string loopId;
    if (!EnsureQuickLoopAsset(type, &loopId))
    {
        return;
    }

    const auto it = std::find(m_loopLibrary.begin(), m_loopLibrary.end(), loopId);
    if (it == m_loopLibrary.end())
    {
        m_statusLine = "Quick loop asset not found in library";
        return;
    }

    m_paletteLoopIndex = static_cast<int>(std::distance(m_loopLibrary.begin(), it));
    PlaceLoopAtHoveredTile();
    if (m_statusLine.rfind("Placed loop ", 0) == 0)
    {
        m_statusLine = std::string{"Placed quick "} + LoopElementTypeToText(type);
    }
}

void LevelEditor::RemovePlacementAtHoveredTile()
{
    if (!m_hoveredTileValid)
    {
        return;
    }

    for (int i = static_cast<int>(m_map.placements.size()) - 1; i >= 0; --i)
    {
        const LoopPlacement& placement = m_map.placements[static_cast<std::size_t>(i)];
        LoopAsset loop;
        std::string error;
        if (!LevelAssetIO::LoadLoop(placement.loopId, &loop, &error))
        {
            continue;
        }

        const glm::ivec2 footprint = RotatedFootprint(loop, placement.rotationDegrees);
        if (m_hoveredTile.x >= placement.tileX &&
            m_hoveredTile.x < placement.tileX + footprint.x &&
            m_hoveredTile.y >= placement.tileY &&
            m_hoveredTile.y < placement.tileY + footprint.y)
        {
            PushHistorySnapshot();
            m_map.placements.erase(m_map.placements.begin() + i);
            m_statusLine = "Removed loop placement";
            ClearSelections();
            return;
        }
    }
}

void LevelEditor::AddPropAtHoveredTile()
{
    if (!m_hoveredTileValid)
    {
        return;
    }

    PushHistorySnapshot();
    PropInstance prop;
    prop.name = BuildUniquePropName("prop");
    prop.type = m_selectedPropType;
    prop.position = glm::vec3{m_hoveredWorld.x, 0.85F, m_hoveredWorld.z};
    switch (prop.type)
    {
        case PropType::Rock: prop.halfExtents = glm::vec3{0.9F, 0.9F, 0.9F}; break;
        case PropType::Tree: prop.halfExtents = glm::vec3{0.6F, 1.6F, 0.6F}; break;
        case PropType::Obstacle: prop.halfExtents = glm::vec3{1.2F, 1.0F, 0.7F}; break;
        case PropType::Platform:
            prop.halfExtents = glm::vec3{2.2F, 0.25F, 2.2F};
            prop.position.y = 0.55F;
            break;
        case PropType::MeshAsset: prop.halfExtents = glm::vec3{0.8F, 0.8F, 0.8F}; break;
        default: break;
    }
    prop.colliderHalfExtents = prop.halfExtents;
    prop.colliderType = ColliderType::Box;
    m_map.props.push_back(prop);
    SelectSingle(Selection{SelectionKind::Prop, static_cast<int>(m_map.props.size()) - 1});
    m_statusLine = std::string{"Added prop "} + PropToText(prop.type);
}

void LevelEditor::AddLightAtHovered(LightType type)
{
    if (m_mode != Mode::MapEditor || !m_hoveredTileValid)
    {
        m_statusLine = "Hover valid tile to place light";
        return;
    }

    PushHistorySnapshot();
    LightInstance light;
    light.type = type;
    light.name = std::string(type == LightType::Spot ? "spot_light_" : "point_light_") + std::to_string(static_cast<int>(m_map.lights.size()) + 1);
    light.position = TileCenter(m_hoveredTile.x, m_hoveredTile.y) + glm::vec3{0.0F, type == LightType::Spot ? 3.0F : 2.5F, 0.0F};
    if (type == LightType::Spot)
    {
        light.rotationEuler = glm::vec3{-45.0F, glm::degrees(m_cameraYaw), 0.0F};
        light.spotInnerAngle = 22.0F;
        light.spotOuterAngle = 36.0F;
    }

    m_map.lights.push_back(light);
    m_selectedLightIndex = static_cast<int>(m_map.lights.size()) - 1;
    m_statusLine = std::string{"Added "} + (type == LightType::Spot ? "spot" : "point") + " light";
}

void LevelEditor::DeleteCurrentSelection()
{
    if (m_selection.kind == SelectionKind::None)
    {
        return;
    }

    const std::vector<int> indices = SortedUniqueValidSelection(m_selection.kind);
    if (indices.empty())
    {
        return;
    }

    PushHistorySnapshot();
    if (m_selection.kind == SelectionKind::LoopElement)
    {
        for (auto it = indices.rbegin(); it != indices.rend(); ++it)
        {
            m_loop.elements.erase(m_loop.elements.begin() + *it);
        }
        AutoComputeLoopBoundsAndFootprint();
        m_statusLine = "Deleted loop element(s)";
    }
    else if (m_selection.kind == SelectionKind::MapPlacement)
    {
        for (auto it = indices.rbegin(); it != indices.rend(); ++it)
        {
            m_map.placements.erase(m_map.placements.begin() + *it);
        }
        m_statusLine = "Deleted placement(s)";
    }
    else if (m_selection.kind == SelectionKind::Prop)
    {
        for (auto it = indices.rbegin(); it != indices.rend(); ++it)
        {
            m_map.props.erase(m_map.props.begin() + *it);
        }
        m_statusLine = "Deleted prop(s)";
    }
    ClearSelections();
}

void LevelEditor::DuplicateCurrentSelection()
{
    if (m_selection.kind == SelectionKind::None)
    {
        return;
    }

    const std::vector<int> indices = SortedUniqueValidSelection(m_selection.kind);
    if (indices.empty())
    {
        return;
    }

    PushHistorySnapshot();

    if (m_selection.kind == SelectionKind::LoopElement)
    {
        std::vector<int> newIndices;
        for (int idx : indices)
        {
            LoopElement clone = m_loop.elements[static_cast<std::size_t>(idx)];
            clone.name = BuildUniqueLoopElementName(clone.name);
            clone.position += glm::vec3{m_gridSnap ? m_gridStep : 0.5F, 0.0F, m_gridSnap ? m_gridStep : 0.5F};
            m_loop.elements.push_back(clone);
            newIndices.push_back(static_cast<int>(m_loop.elements.size()) - 1);
        }
        AutoComputeLoopBoundsAndFootprint();
        ClearSelections();
        m_selectedLoopElements = newIndices;
        if (!newIndices.empty())
        {
            m_selection = Selection{SelectionKind::LoopElement, newIndices.back()};
        }
        m_statusLine = "Duplicated loop element(s)";
        return;
    }

    if (m_selection.kind == SelectionKind::MapPlacement)
    {
        std::vector<int> newIndices;
        for (int idx : indices)
        {
            LoopPlacement clone = m_map.placements[static_cast<std::size_t>(idx)];
            clone.tileX += 1;
            if (CanPlaceLoopAt(clone.tileX, clone.tileY, clone.rotationDegrees, -1))
            {
                m_map.placements.push_back(clone);
                newIndices.push_back(static_cast<int>(m_map.placements.size()) - 1);
            }
        }
        ClearSelections();
        m_selectedMapPlacements = newIndices;
        if (!newIndices.empty())
        {
            m_selection = Selection{SelectionKind::MapPlacement, newIndices.back()};
            m_statusLine = "Duplicated placement(s)";
        }
        else
        {
            m_statusLine = "Duplicate failed: no free space";
        }
        return;
    }

    if (m_selection.kind == SelectionKind::Prop)
    {
        std::vector<int> newIndices;
        for (int idx : indices)
        {
            PropInstance clone = m_map.props[static_cast<std::size_t>(idx)];
            clone.position += glm::vec3{m_gridSnap ? m_gridStep : 0.5F, 0.0F, m_gridSnap ? m_gridStep : 0.5F};
            m_map.props.push_back(clone);
            newIndices.push_back(static_cast<int>(m_map.props.size()) - 1);
        }
        ClearSelections();
        m_selectedProps = newIndices;
        if (!newIndices.empty())
        {
            m_selection = Selection{SelectionKind::Prop, newIndices.back()};
        }
        m_statusLine = "Duplicated prop(s)";
    }
}

void LevelEditor::CopyCurrentSelection()
{
    if (m_selection.kind == SelectionKind::None)
    {
        m_statusLine = "Copy: nothing selected";
        return;
    }

    const std::vector<int> indices = SortedUniqueValidSelection(m_selection.kind);
    if (indices.empty())
    {
        m_statusLine = "Copy: invalid selection";
        return;
    }

    m_clipboard = ClipboardState{};
    m_clipboard.kind = m_selection.kind;

    if (m_selection.kind == SelectionKind::LoopElement)
    {
        for (int idx : indices)
        {
            m_clipboard.loopElements.push_back(m_loop.elements[static_cast<std::size_t>(idx)]);
        }
        m_clipboard.hasData = !m_clipboard.loopElements.empty();
        m_statusLine = "Copied loop element(s): " + std::to_string(m_clipboard.loopElements.size());
    }
    else if (m_selection.kind == SelectionKind::MapPlacement)
    {
        for (int idx : indices)
        {
            m_clipboard.mapPlacements.push_back(m_map.placements[static_cast<std::size_t>(idx)]);
        }
        m_clipboard.hasData = !m_clipboard.mapPlacements.empty();
        m_statusLine = "Copied placement(s): " + std::to_string(m_clipboard.mapPlacements.size());
    }
    else if (m_selection.kind == SelectionKind::Prop)
    {
        for (int idx : indices)
        {
            m_clipboard.props.push_back(m_map.props[static_cast<std::size_t>(idx)]);
        }
        m_clipboard.hasData = !m_clipboard.props.empty();
        m_statusLine = "Copied prop(s): " + std::to_string(m_clipboard.props.size());
    }

    if (!m_clipboard.hasData)
    {
        m_statusLine = "Copy: unsupported selection";
        return;
    }
    m_clipboard.pasteCount = 0;
}

void LevelEditor::PasteClipboard()
{
    if (!m_clipboard.hasData || m_clipboard.kind == SelectionKind::None)
    {
        m_statusLine = "Paste: clipboard is empty";
        return;
    }

    if (m_clipboard.kind == SelectionKind::LoopElement && m_mode != Mode::LoopEditor)
    {
        m_statusLine = "Paste: loop elements only in Loop Editor";
        return;
    }
    if ((m_clipboard.kind == SelectionKind::MapPlacement || m_clipboard.kind == SelectionKind::Prop) &&
        m_mode != Mode::MapEditor)
    {
        m_statusLine = "Paste: map objects only in Map Editor";
        return;
    }

    const int pasteIndex = m_clipboard.pasteCount + 1;
    const float worldOffset = (m_gridSnap ? m_gridStep : 0.5F) * static_cast<float>(pasteIndex);
    const int tileOffset = pasteIndex;

    bool snapshotPushed = false;
    auto pushSnapshotOnce = [&]() {
        if (!snapshotPushed)
        {
            PushHistorySnapshot();
            snapshotPushed = true;
        }
    };

    if (m_clipboard.kind == SelectionKind::LoopElement)
    {
        std::vector<int> newIndices;
        for (const LoopElement& source : m_clipboard.loopElements)
        {
            pushSnapshotOnce();
            LoopElement clone = source;
            clone.name = BuildUniqueLoopElementName(source.name);
            clone.position += glm::vec3{worldOffset, 0.0F, worldOffset};
            m_loop.elements.push_back(clone);
            newIndices.push_back(static_cast<int>(m_loop.elements.size()) - 1);
        }

        if (newIndices.empty())
        {
            m_statusLine = "Paste failed";
            return;
        }
        AutoComputeLoopBoundsAndFootprint();
        ClearSelections();
        m_selectedLoopElements = newIndices;
        m_selection = Selection{SelectionKind::LoopElement, newIndices.back()};
        m_clipboard.pasteCount += 1;
        m_statusLine = "Pasted loop element(s): " + std::to_string(newIndices.size());
        return;
    }

    if (m_clipboard.kind == SelectionKind::MapPlacement)
    {
        std::vector<int> newIndices;
        for (const LoopPlacement& source : m_clipboard.mapPlacements)
        {
            LoopPlacement clone = source;
            clone.tileX += tileOffset;
            clone.tileY += tileOffset;
            if (!CanPlaceLoopAt(clone.tileX, clone.tileY, clone.rotationDegrees, -1))
            {
                continue;
            }
            pushSnapshotOnce();
            m_map.placements.push_back(clone);
            newIndices.push_back(static_cast<int>(m_map.placements.size()) - 1);
        }

        if (newIndices.empty())
        {
            m_statusLine = "Paste failed: no free map space";
            return;
        }
        ClearSelections();
        m_selectedMapPlacements = newIndices;
        m_selection = Selection{SelectionKind::MapPlacement, newIndices.back()};
        m_clipboard.pasteCount += 1;
        m_statusLine = "Pasted placement(s): " + std::to_string(newIndices.size());
        return;
    }

    if (m_clipboard.kind == SelectionKind::Prop)
    {
        std::vector<int> newIndices;
        for (const PropInstance& source : m_clipboard.props)
        {
            pushSnapshotOnce();
            PropInstance clone = source;
            clone.position += glm::vec3{worldOffset, 0.0F, worldOffset};
            m_map.props.push_back(clone);
            newIndices.push_back(static_cast<int>(m_map.props.size()) - 1);
        }

        if (newIndices.empty())
        {
            m_statusLine = "Paste failed";
            return;
        }
        ClearSelections();
        m_selectedProps = newIndices;
        m_selection = Selection{SelectionKind::Prop, newIndices.back()};
        m_clipboard.pasteCount += 1;
        m_statusLine = "Pasted prop(s): " + std::to_string(newIndices.size());
    }
}

void LevelEditor::AutoComputeLoopBoundsAndFootprint()
{
    if (m_loop.elements.empty())
    {
        return;
    }

    glm::vec3 minValue{1.0e9F};
    glm::vec3 maxValue{-1.0e9F};
    for (const LoopElement& element : m_loop.elements)
    {
        minValue = glm::min(minValue, element.position - element.halfExtents);
        maxValue = glm::max(maxValue, element.position + element.halfExtents);
    }

    if (!m_loop.manualBounds)
    {
        m_loop.boundsMin = minValue;
        m_loop.boundsMax = maxValue;
    }

    if (!m_loop.manualFootprint)
    {
        const glm::vec3 size = maxValue - minValue;
        m_loop.footprintWidth = std::max(1, static_cast<int>(std::ceil(size.x / kEditorTileSize)));
        m_loop.footprintHeight = std::max(1, static_cast<int>(std::ceil(size.z / kEditorTileSize)));
    }
}

std::vector<std::string> LevelEditor::ValidateLoopForUi() const
{
    return LevelAssetIO::ValidateLoop(m_loop);
}

std::string LevelEditor::BuildUniqueLoopElementName(const std::string& preferredBaseName) const
{
    const std::string base = StripNumericSuffix(preferredBaseName.empty() ? "element" : preferredBaseName);
    int suffix = 1;
    while (true)
    {
        const std::string candidate = base + "_" + std::to_string(suffix);
        const bool exists = std::any_of(
            m_loop.elements.begin(),
            m_loop.elements.end(),
            [&](const LoopElement& element) { return element.name == candidate; });
        if (!exists)
        {
            return candidate;
        }
        ++suffix;
    }
}

std::string LevelEditor::BuildUniquePropName(const std::string& preferredBaseName) const
{
    const std::string base = StripNumericSuffix(preferredBaseName.empty() ? "prop" : preferredBaseName);
    int suffix = 1;
    while (true)
    {
        const std::string candidate = base + "_" + std::to_string(suffix);
        const bool exists = std::any_of(
            m_map.props.begin(),
            m_map.props.end(),
            [&](const PropInstance& prop) { return prop.name == candidate; });
        if (!exists)
        {
            return candidate;
        }
        ++suffix;
    }
}

void LevelEditor::RefreshContentBrowser()
{
    ClearContentPreviewCache();
    ClearMeshAlbedoTextureCache();
    m_contentEntries = m_assetRegistry.ListDirectory(m_contentDirectory);
    if (m_selectedContentEntry >= static_cast<int>(m_contentEntries.size()))
    {
        m_selectedContentEntry = -1;
        m_selectedContentPath.clear();
    }
    m_contentNeedsRefresh = false;
}

void LevelEditor::ClearContentPreviewCache()
{
#if BUILD_WITH_IMGUI
    for (auto& [_, preview] : m_contentPreviews)
    {
        if (preview.textureId != 0)
        {
            GLuint tex = static_cast<GLuint>(preview.textureId);
            glDeleteTextures(1, &tex);
        }
    }
#endif
    m_contentPreviews.clear();
    m_contentPreviewLru.clear();
}

unsigned int LevelEditor::GetOrCreateMeshSurfaceAlbedoTexture(
    const std::string& meshPath,
    std::size_t surfaceIndex,
    const engine::assets::MeshSurfaceData& surface
) const
{
#if BUILD_WITH_IMGUI
    if (surface.albedoPixels.empty() || surface.albedoWidth <= 0 || surface.albedoHeight <= 0 || surface.albedoChannels <= 0)
    {
        return 0;
    }

    const std::string key = meshPath + "#" + std::to_string(surfaceIndex);
    const auto existing = m_meshAlbedoTextures.find(key);
    if (existing != m_meshAlbedoTextures.end())
    {
        return existing->second;
    }

    const int width = surface.albedoWidth;
    const int height = surface.albedoHeight;
    const int channels = glm::clamp(surface.albedoChannels, 1, 4);
    std::vector<unsigned char> rgba(static_cast<std::size_t>(width * height * 4), 255U);
    for (int i = 0; i < width * height; ++i)
    {
        const std::size_t src = static_cast<std::size_t>(i * channels);
        const std::size_t dst = static_cast<std::size_t>(i * 4);
        const unsigned char r = src < surface.albedoPixels.size() ? surface.albedoPixels[src] : 255U;
        const unsigned char g = (channels > 1 && src + 1 < surface.albedoPixels.size()) ? surface.albedoPixels[src + 1] : r;
        const unsigned char b = (channels > 2 && src + 2 < surface.albedoPixels.size()) ? surface.albedoPixels[src + 2] : r;
        const unsigned char a = (channels > 3 && src + 3 < surface.albedoPixels.size()) ? surface.albedoPixels[src + 3] : 255U;
        rgba[dst + 0] = r;
        rgba[dst + 1] = g;
        rgba[dst + 2] = b;
        rgba[dst + 3] = a;
    }

    GLuint texture = 0;
    glGenTextures(1, &texture);
    if (texture == 0)
    {
        return 0;
    }
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
#ifdef GL_TEXTURE_MAX_ANISOTROPY_EXT
    GLfloat maxAniso = 1.0F;
    glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &maxAniso);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, glm::max(1.0F, maxAniso));
#endif
    glTexImage2D(
        GL_TEXTURE_2D,
        0,
        GL_SRGB8_ALPHA8,
        width,
        height,
        0,
        GL_RGBA,
        GL_UNSIGNED_BYTE,
        rgba.data());
    glGenerateMipmap(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, 0);
    m_meshAlbedoTextures.emplace(key, texture);
    return texture;
#else
    (void)meshPath;
    (void)surfaceIndex;
    (void)surface;
    return 0;
#endif
}

void LevelEditor::ClearMeshAlbedoTextureCache() const
{
#if BUILD_WITH_IMGUI
    for (auto& [_, texture] : m_meshAlbedoTextures)
    {
        if (texture != 0)
        {
            GLuint glTex = static_cast<GLuint>(texture);
            glDeleteTextures(1, &glTex);
        }
    }
#endif
    m_meshAlbedoTextures.clear();
}

void LevelEditor::TouchContentPreviewLru(const std::string& key)
{
    if (key.empty())
    {
        return;
    }
    auto it = std::find(m_contentPreviewLru.begin(), m_contentPreviewLru.end(), key);
    if (it != m_contentPreviewLru.end())
    {
        m_contentPreviewLru.erase(it);
    }
    m_contentPreviewLru.insert(m_contentPreviewLru.begin(), key);
    EnforceContentPreviewLru();
}

void LevelEditor::EnforceContentPreviewLru()
{
    if (m_contentPreviewLruCapacity == 0)
    {
        m_contentPreviewLruCapacity = 64;
    }
    while (m_contentPreviewLru.size() > m_contentPreviewLruCapacity)
    {
        const std::string evictKey = m_contentPreviewLru.back();
        m_contentPreviewLru.pop_back();
        const auto previewIt = m_contentPreviews.find(evictKey);
        if (previewIt == m_contentPreviews.end())
        {
            continue;
        }
#if BUILD_WITH_IMGUI
        if (previewIt->second.textureId != 0)
        {
            GLuint tex = static_cast<GLuint>(previewIt->second.textureId);
            glDeleteTextures(1, &tex);
        }
#endif
        m_contentPreviews.erase(previewIt);
    }
}

bool LevelEditor::HasSceneSelection() const
{
    if (m_selection.kind == SelectionKind::None)
    {
        return false;
    }
    if (m_selection.kind == SelectionKind::LoopElement)
    {
        return !SortedUniqueValidSelection(SelectionKind::LoopElement).empty();
    }
    if (m_selection.kind == SelectionKind::MapPlacement)
    {
        return !SortedUniqueValidSelection(SelectionKind::MapPlacement).empty();
    }
    if (m_selection.kind == SelectionKind::Prop)
    {
        return !SortedUniqueValidSelection(SelectionKind::Prop).empty();
    }
    return false;
}

bool LevelEditor::HasMeshSelection() const
{
    if (m_mode != Mode::LoopEditor || !m_meshModelSceneEditEnabled)
    {
        return false;
    }
    if (m_meshEditMode == MeshEditMode::Vertex)
    {
        return !m_meshModelVertexSelection.empty() ||
               (m_meshModelSelectedVertex >= 0 && m_meshModelSelectedVertex < static_cast<int>(m_meshModelVertices.size()));
    }
    if (m_meshEditMode == MeshEditMode::Edge)
    {
        return !CollectModalMeshVertexSelection().empty();
    }
    return !m_meshModelFaceSelection.empty() ||
           (m_meshModelSelectedFace >= 0 && m_meshModelSelectedFace < static_cast<int>(m_meshModelFaces.size()));
}

std::vector<int> LevelEditor::CollectModalMeshVertexSelection() const
{
    std::unordered_set<int> vertices;
    if (m_meshEditMode == MeshEditMode::Vertex)
    {
        for (const int selected : m_meshModelVertexSelection)
        {
            vertices.insert(selected);
        }
        if (m_meshModelSelectedVertex >= 0)
        {
            vertices.insert(m_meshModelSelectedVertex);
        }
    }
    else if (m_meshEditMode == MeshEditMode::Edge)
    {
        const std::vector<std::array<int, 2>> edges = BuildMeshModelEdges();
        std::vector<int> edgeIndices = CollectMeshModelActiveEdges();
        if (edgeIndices.empty() && m_meshModelSelectedEdge >= 0)
        {
            edgeIndices.push_back(m_meshModelSelectedEdge);
        }
        for (const int edgeIndex : edgeIndices)
        {
            if (edgeIndex < 0 || edgeIndex >= static_cast<int>(edges.size()))
            {
                continue;
            }
            vertices.insert(edges[static_cast<std::size_t>(edgeIndex)][0]);
            vertices.insert(edges[static_cast<std::size_t>(edgeIndex)][1]);
        }
    }
    else
    {
        std::vector<int> faces = m_meshModelFaceSelection;
        if (faces.empty() && m_meshModelSelectedFace >= 0 && m_meshModelSelectedFace < static_cast<int>(m_meshModelFaces.size()))
        {
            faces.push_back(m_meshModelSelectedFace);
        }
        for (const int faceIndex : faces)
        {
            if (faceIndex < 0 || faceIndex >= static_cast<int>(m_meshModelFaces.size()))
            {
                continue;
            }
            const MeshModelFace& face = m_meshModelFaces[static_cast<std::size_t>(faceIndex)];
            if (face.deleted)
            {
                continue;
            }
            const int count = std::clamp(face.vertexCount, 3, 4);
            for (int i = 0; i < count; ++i)
            {
                vertices.insert(face.indices[static_cast<std::size_t>(i)]);
            }
        }
    }

    std::vector<int> out(vertices.begin(), vertices.end());
    std::sort(out.begin(), out.end());
    out.erase(std::remove_if(out.begin(), out.end(), [this](int idx) {
                  return idx < 0 || idx >= static_cast<int>(m_meshModelVertices.size()) ||
                         m_meshModelVertices[static_cast<std::size_t>(idx)].deleted;
              }),
              out.end());
    return out;
}

void LevelEditor::BeginModalTransform(GizmoMode tool, const engine::platform::Input& input, bool pushHistory)
{
    if (m_modalTransform.active)
    {
        return;
    }

    const bool meshSelection = HasMeshSelection();
    const bool sceneSelection = HasSceneSelection();
    if (!meshSelection && !sceneSelection)
    {
        return;
    }

    if (pushHistory)
    {
        PushHistorySnapshot();
    }

    m_modalTransform = ModalTransformState{};
    m_modalTransform.active = true;
    m_modalTransform.meshSelection = meshSelection;
    m_modalTransform.tool = tool;
    m_modalTransform.orientation = m_transformOrientationUi;
    m_modalTransform.startMouse = input.MousePosition();
    m_modalTransform.lastMouse = m_modalTransform.startMouse;
    m_modalTransform.pivotWorld = meshSelection ? MeshModelSelectionPivot() : SelectionPivot();
    m_modalTransform.viewRight = CameraRight();
    m_modalTransform.viewUp = CameraUp();
    m_modalTransform.numericMode = NumericInputMode::Absolute;
    m_modalTransform.previewTranslateMagnitude = 0.0F;
    m_modalTransform.previewRotateDegrees = 0.0F;
    m_modalTransform.previewScaleFactor = 1.0F;
    m_modalTransform.previewWorldDelta = glm::vec3{0.0F};

    if (meshSelection)
    {
        for (const int idx : CollectModalMeshVertexSelection())
        {
            m_modalTransform.meshVertices.push_back(MeshVertexTransform{
                idx,
                m_meshModelVertices[static_cast<std::size_t>(idx)].position,
            });
        }
    }
    else
    {
        for (const int idx : SortedUniqueValidSelection(SelectionKind::LoopElement))
        {
            const LoopElement& e = m_loop.elements[static_cast<std::size_t>(idx)];
            m_modalTransform.loopElements.push_back(SceneLoopElementTransform{
                idx,
                e.position,
                e.halfExtents,
                glm::vec3{e.pitchDegrees, e.yawDegrees, e.rollDegrees},
            });
        }
        for (const int idx : SortedUniqueValidSelection(SelectionKind::MapPlacement))
        {
            const LoopPlacement& p = m_map.placements[static_cast<std::size_t>(idx)];
            m_modalTransform.placements.push_back(ScenePlacementTransform{idx, p.tileX, p.tileY, p.rotationDegrees});
        }
        for (const int idx : SortedUniqueValidSelection(SelectionKind::Prop))
        {
            const PropInstance& p = m_map.props[static_cast<std::size_t>(idx)];
            m_modalTransform.props.push_back(ScenePropTransform{
                idx,
                p.position,
                p.halfExtents,
                glm::vec3{p.pitchDegrees, p.yawDegrees, p.rollDegrees},
            });
        }
    }

    m_gizmoMode = tool;
    m_statusLine = std::string{"Modal transform: "} + GizmoToText(tool) +
                   " (X/Y/Z axis, Shift+axis plane, double axis key toggles global/local, numeric: 1|-2.5|*2|/2)";
}

void LevelEditor::UpdateModalConstraint(const engine::platform::Input& input)
{
    const bool shiftDown = input.IsKeyDown(GLFW_KEY_LEFT_SHIFT) || input.IsKeyDown(GLFW_KEY_RIGHT_SHIFT);
    const auto handleAxisKey = [&](int key, GizmoAxis axis) {
        if (!input.IsKeyPressed(key))
        {
            return;
        }
        if (m_modalTransform.constraintAxis == axis && !shiftDown &&
            m_modalTransform.constraintMode == ModalConstraintMode::Axis)
        {
            m_modalTransform.orientation = (m_modalTransform.orientation == TransformOrientation::Global)
                                               ? TransformOrientation::Local
                                               : TransformOrientation::Global;
            m_statusLine = std::string{"Constraint orientation: "} +
                           (m_modalTransform.orientation == TransformOrientation::Local ? "Local" : "Global");
            return;
        }
        m_modalTransform.constraintAxis = axis;
        m_modalTransform.constraintMode = shiftDown ? ModalConstraintMode::Plane : ModalConstraintMode::Axis;
    };

    handleAxisKey(GLFW_KEY_X, GizmoAxis::X);
    handleAxisKey(GLFW_KEY_Y, GizmoAxis::Y);
    handleAxisKey(GLFW_KEY_Z, GizmoAxis::Z);
}

glm::vec3 LevelEditor::ModalConstraintAxisDirection() const
{
    if (!m_modalTransform.active || m_modalTransform.constraintAxis == GizmoAxis::None)
    {
        return glm::vec3{0.0F, 1.0F, 0.0F};
    }
    glm::vec3 axis =
        m_modalTransform.constraintAxis == GizmoAxis::X ? glm::vec3{1.0F, 0.0F, 0.0F}
        : (m_modalTransform.constraintAxis == GizmoAxis::Y ? glm::vec3{0.0F, 1.0F, 0.0F}
                                                           : glm::vec3{0.0F, 0.0F, 1.0F});
    if (m_modalTransform.orientation == TransformOrientation::Global)
    {
        return axis;
    }

    if (m_modalTransform.meshSelection)
    {
        glm::mat3 basis{1.0F};
        if (BuildActiveMeshLocalBasis(&basis))
        {
            if (m_modalTransform.constraintAxis == GizmoAxis::X)
            {
                axis = basis[0];
            }
            else if (m_modalTransform.constraintAxis == GizmoAxis::Y)
            {
                axis = basis[1];
            }
            else
            {
                axis = basis[2];
            }
            if (glm::length(axis) > 1.0e-6F)
            {
                return glm::normalize(axis);
            }
        }
        return axis;
    }

    glm::vec3 euler{0.0F};
    if (!m_modalTransform.loopElements.empty())
    {
        euler = m_modalTransform.loopElements.front().rotationEuler;
    }
    else if (!m_modalTransform.props.empty())
    {
        euler = m_modalTransform.props.front().rotationEuler;
    }
    const glm::mat3 localRot = RotationMatrixFromEulerDegrees(euler);
    return glm::normalize(localRot * axis);
}

float LevelEditor::ApplyModalNumericMode(float baseValue, const std::optional<float>& numericOverride) const
{
    if (!numericOverride.has_value())
    {
        return baseValue;
    }
    const float numericValue = *numericOverride;
    if (m_modalTransform.numericMode == NumericInputMode::Multiply)
    {
        return baseValue * numericValue;
    }
    if (m_modalTransform.numericMode == NumericInputMode::Divide)
    {
        const float denom = std::abs(numericValue) < 1.0e-4F ? 1.0F : numericValue;
        return baseValue / denom;
    }
    return numericValue;
}

std::optional<float> LevelEditor::ConsumeModalNumericInput(const engine::platform::Input& input)
{
    const bool shiftDown = input.IsKeyDown(GLFW_KEY_LEFT_SHIFT) || input.IsKeyDown(GLFW_KEY_RIGHT_SHIFT);
    if (input.IsKeyPressed(GLFW_KEY_KP_MULTIPLY) || (shiftDown && input.IsKeyPressed(GLFW_KEY_8)))
    {
        m_modalTransform.numericMode = NumericInputMode::Multiply;
        m_modalTransform.numericInput.clear();
        m_modalTransform.numericNegative = false;
        return std::nullopt;
    }
    if (input.IsKeyPressed(GLFW_KEY_SLASH) || input.IsKeyPressed(GLFW_KEY_KP_DIVIDE))
    {
        m_modalTransform.numericMode = NumericInputMode::Divide;
        m_modalTransform.numericInput.clear();
        m_modalTransform.numericNegative = false;
        return std::nullopt;
    }

    auto appendDigit = [&](int key, char digit) {
        if (key == GLFW_KEY_8 && shiftDown)
        {
            return;
        }
        if (input.IsKeyPressed(key) || input.IsKeyPressed(GLFW_KEY_KP_0 + (key - GLFW_KEY_0)))
        {
            m_modalTransform.numericInput.push_back(digit);
        }
    };
    appendDigit(GLFW_KEY_0, '0');
    appendDigit(GLFW_KEY_1, '1');
    appendDigit(GLFW_KEY_2, '2');
    appendDigit(GLFW_KEY_3, '3');
    appendDigit(GLFW_KEY_4, '4');
    appendDigit(GLFW_KEY_5, '5');
    appendDigit(GLFW_KEY_6, '6');
    appendDigit(GLFW_KEY_7, '7');
    appendDigit(GLFW_KEY_8, '8');
    appendDigit(GLFW_KEY_9, '9');
    if (input.IsKeyPressed(GLFW_KEY_PERIOD) || input.IsKeyPressed(GLFW_KEY_KP_DECIMAL))
    {
        if (m_modalTransform.numericInput.find('.') == std::string::npos)
        {
            m_modalTransform.numericInput.push_back('.');
        }
    }
    if (input.IsKeyPressed(GLFW_KEY_MINUS) || input.IsKeyPressed(GLFW_KEY_KP_SUBTRACT))
    {
        m_modalTransform.numericNegative = !m_modalTransform.numericNegative;
    }
    if (input.IsKeyPressed(GLFW_KEY_BACKSPACE) && !m_modalTransform.numericInput.empty())
    {
        m_modalTransform.numericInput.pop_back();
    }
    if (m_modalTransform.numericInput.empty())
    {
        return std::nullopt;
    }

    try
    {
        float value = std::stof(m_modalTransform.numericInput);
        if (m_modalTransform.numericNegative)
        {
            value = -value;
        }
        return value;
    }
    catch (...)
    {
        return std::nullopt;
    }
}

glm::vec3 LevelEditor::ApplyModalAxisConstraint(const glm::vec3& worldDelta) const
{
    if (!m_modalTransform.active || m_modalTransform.constraintMode == ModalConstraintMode::None)
    {
        return worldDelta;
    }
    const glm::vec3 axisDirection = glm::normalize(ModalConstraintAxisDirection());
    if (m_modalTransform.constraintMode == ModalConstraintMode::Axis)
    {
        return axisDirection * glm::dot(worldDelta, axisDirection);
    }

    return worldDelta - axisDirection * glm::dot(worldDelta, axisDirection);
}

glm::vec3 LevelEditor::ModalRotationAxis() const
{
    if (!m_modalTransform.active || m_modalTransform.constraintMode == ModalConstraintMode::None ||
        m_modalTransform.constraintAxis == GizmoAxis::None)
    {
        return glm::vec3{0.0F, 1.0F, 0.0F};
    }
    return glm::normalize(ModalConstraintAxisDirection());
}

void LevelEditor::UpdateModalTransform(const engine::platform::Input& input, float /*deltaSeconds*/)
{
    if (!m_modalTransform.active)
    {
        return;
    }

    UpdateModalConstraint(input);

    const glm::vec2 mouseNow = input.MousePosition();
    const glm::vec2 totalDelta = mouseNow - m_modalTransform.startMouse;
    const float cameraDistance = glm::max(1.0F, glm::length(m_cameraPosition - m_modalTransform.pivotWorld));
    const std::optional<float> numericOverride = ConsumeModalNumericInput(input);

    if (m_modalTransform.tool == GizmoMode::Translate)
    {
        const float moveScale = cameraDistance * 0.0018F;
        glm::vec3 worldDelta = m_modalTransform.viewRight * totalDelta.x - m_modalTransform.viewUp * totalDelta.y;
        worldDelta *= moveScale;
        float baseMagnitude = glm::length(worldDelta);
        if (numericOverride.has_value())
        {
            glm::vec3 direction = worldDelta;
            if (glm::length(direction) < 1.0e-6F)
            {
                direction = m_modalTransform.viewRight;
            }
            if (m_modalTransform.constraintMode != ModalConstraintMode::None)
            {
                direction = ModalConstraintAxisDirection();
            }
            direction = glm::normalize(direction);
            const float targetMagnitude = ApplyModalNumericMode(baseMagnitude, numericOverride);
            worldDelta = direction * targetMagnitude;
        }
        worldDelta = ApplyModalAxisConstraint(worldDelta);
        m_modalTransform.previewWorldDelta = worldDelta;
        m_modalTransform.previewTranslateMagnitude = glm::length(worldDelta);
        m_modalTransform.previewRotateDegrees = 0.0F;
        m_modalTransform.previewScaleFactor = 1.0F;

        if (m_gridSnap && !numericOverride.has_value())
        {
            const float step = std::max(0.05F, m_gridStep);
            worldDelta.x = std::round(worldDelta.x / step) * step;
            worldDelta.y = std::round(worldDelta.y / step) * step;
            worldDelta.z = std::round(worldDelta.z / step) * step;
        }

        if (m_modalTransform.meshSelection)
        {
            const glm::vec3 localDelta{
                std::abs(m_meshModelScale.x) > 1.0e-6F ? worldDelta.x / m_meshModelScale.x : 0.0F,
                std::abs(m_meshModelScale.y) > 1.0e-6F ? worldDelta.y / m_meshModelScale.y : 0.0F,
                std::abs(m_meshModelScale.z) > 1.0e-6F ? worldDelta.z / m_meshModelScale.z : 0.0F,
            };
            for (const MeshVertexTransform& v : m_modalTransform.meshVertices)
            {
                if (v.index < 0 || v.index >= static_cast<int>(m_meshModelVertices.size()))
                {
                    continue;
                }
                m_meshModelVertices[static_cast<std::size_t>(v.index)].position = v.position + localDelta;
            }
        }
        else
        {
            for (const SceneLoopElementTransform& e : m_modalTransform.loopElements)
            {
                if (e.index < 0 || e.index >= static_cast<int>(m_loop.elements.size()))
                {
                    continue;
                }
                m_loop.elements[static_cast<std::size_t>(e.index)].position = e.position + worldDelta;
            }
            for (const ScenePropTransform& p : m_modalTransform.props)
            {
                if (p.index < 0 || p.index >= static_cast<int>(m_map.props.size()))
                {
                    continue;
                }
                m_map.props[static_cast<std::size_t>(p.index)].position = p.position + worldDelta;
            }
            for (const ScenePlacementTransform& p : m_modalTransform.placements)
            {
                if (p.index < 0 || p.index >= static_cast<int>(m_map.placements.size()))
                {
                    continue;
                }
                const int dx = static_cast<int>(std::round(worldDelta.x / glm::max(1.0F, m_map.tileSize)));
                const int dy = static_cast<int>(std::round(worldDelta.z / glm::max(1.0F, m_map.tileSize)));
                m_map.placements[static_cast<std::size_t>(p.index)].tileX = p.tileX + dx;
                m_map.placements[static_cast<std::size_t>(p.index)].tileY = p.tileY + dy;
            }
        }
        m_modalTransform.lastMouse = mouseNow;
        return;
    }

    if (m_modalTransform.tool == GizmoMode::Rotate)
    {
        const float baseDegrees = totalDelta.x * 0.35F;
        float degrees = ApplyModalNumericMode(baseDegrees, numericOverride);
        if (m_angleSnap && !numericOverride.has_value())
        {
            const float step = std::max(1.0F, m_angleStepDegrees);
            degrees = std::round(degrees / step) * step;
        }
        m_modalTransform.previewRotateDegrees = degrees;
        m_modalTransform.previewTranslateMagnitude = 0.0F;
        m_modalTransform.previewScaleFactor = 1.0F;
        m_modalTransform.previewWorldDelta = glm::vec3{0.0F};
        const glm::vec3 axis = glm::normalize(ModalRotationAxis());
        if (m_modalTransform.meshSelection)
        {
            const glm::mat4 rotationMat = glm::rotate(glm::mat4{1.0F}, glm::radians(degrees), axis);
            for (const MeshVertexTransform& v : m_modalTransform.meshVertices)
            {
                if (v.index < 0 || v.index >= static_cast<int>(m_meshModelVertices.size()))
                {
                    continue;
                }
                const glm::vec3 worldBase = m_meshModelPosition + v.position * m_meshModelScale;
                const glm::vec3 rotatedWorld =
                    m_modalTransform.pivotWorld +
                    glm::vec3(rotationMat * glm::vec4(worldBase - m_modalTransform.pivotWorld, 0.0F));
                const glm::vec3 localRotated{
                    std::abs(m_meshModelScale.x) > 1.0e-6F ? (rotatedWorld.x - m_meshModelPosition.x) / m_meshModelScale.x : 0.0F,
                    std::abs(m_meshModelScale.y) > 1.0e-6F ? (rotatedWorld.y - m_meshModelPosition.y) / m_meshModelScale.y : 0.0F,
                    std::abs(m_meshModelScale.z) > 1.0e-6F ? (rotatedWorld.z - m_meshModelPosition.z) / m_meshModelScale.z : 0.0F,
                };
                m_meshModelVertices[static_cast<std::size_t>(v.index)].position = localRotated;
            }
        }
        else
        {
            for (const SceneLoopElementTransform& e : m_modalTransform.loopElements)
            {
                if (e.index < 0 || e.index >= static_cast<int>(m_loop.elements.size()))
                {
                    continue;
                }
                LoopElement& target = m_loop.elements[static_cast<std::size_t>(e.index)];
                target.pitchDegrees = e.rotationEuler.x + (axis.x > 0.5F ? degrees : 0.0F);
                target.yawDegrees = e.rotationEuler.y + (axis.y > 0.5F ? degrees : 0.0F);
                target.rollDegrees = e.rotationEuler.z + (axis.z > 0.5F ? degrees : 0.0F);
            }
            for (const ScenePropTransform& p : m_modalTransform.props)
            {
                if (p.index < 0 || p.index >= static_cast<int>(m_map.props.size()))
                {
                    continue;
                }
                PropInstance& target = m_map.props[static_cast<std::size_t>(p.index)];
                target.pitchDegrees = p.rotationEuler.x + (axis.x > 0.5F ? degrees : 0.0F);
                target.yawDegrees = p.rotationEuler.y + (axis.y > 0.5F ? degrees : 0.0F);
                target.rollDegrees = p.rotationEuler.z + (axis.z > 0.5F ? degrees : 0.0F);
            }
            for (const ScenePlacementTransform& p : m_modalTransform.placements)
            {
                if (p.index < 0 || p.index >= static_cast<int>(m_map.placements.size()))
                {
                    continue;
                }
                const int snapped = static_cast<int>(std::round(degrees / 90.0F)) * 90;
                m_map.placements[static_cast<std::size_t>(p.index)].rotationDegrees =
                    ((p.rotationDegrees + snapped) % 360 + 360) % 360;
            }
        }
        return;
    }

    const float baseFactor = 1.0F + (totalDelta.x - totalDelta.y) * 0.005F;
    float factor = ApplyModalNumericMode(baseFactor, numericOverride);
    factor = glm::clamp(factor, 0.05F, 8.0F);
    m_modalTransform.previewScaleFactor = factor;
    m_modalTransform.previewTranslateMagnitude = 0.0F;
    m_modalTransform.previewRotateDegrees = 0.0F;
    m_modalTransform.previewWorldDelta = glm::vec3{0.0F};
    if (m_gridSnap && !numericOverride.has_value())
    {
        const float step = 0.05F;
        factor = std::round(factor / step) * step;
        factor = glm::clamp(factor, 0.05F, 8.0F);
    }

    glm::vec3 axisScale{factor, factor, factor};
    if (m_modalTransform.constraintMode == ModalConstraintMode::Axis)
    {
        axisScale = glm::vec3{1.0F};
        if (m_modalTransform.constraintAxis == GizmoAxis::X)
        {
            axisScale.x = factor;
        }
        else if (m_modalTransform.constraintAxis == GizmoAxis::Y)
        {
            axisScale.y = factor;
        }
        else if (m_modalTransform.constraintAxis == GizmoAxis::Z)
        {
            axisScale.z = factor;
        }
    }
    else if (m_modalTransform.constraintMode == ModalConstraintMode::Plane)
    {
        if (m_modalTransform.constraintAxis == GizmoAxis::X)
        {
            axisScale.x = 1.0F;
        }
        else if (m_modalTransform.constraintAxis == GizmoAxis::Y)
        {
            axisScale.y = 1.0F;
        }
        else if (m_modalTransform.constraintAxis == GizmoAxis::Z)
        {
            axisScale.z = 1.0F;
        }
    }

    if (m_modalTransform.meshSelection)
    {
        for (const MeshVertexTransform& v : m_modalTransform.meshVertices)
        {
            if (v.index < 0 || v.index >= static_cast<int>(m_meshModelVertices.size()))
            {
                continue;
            }
            const glm::vec3 worldBase = m_meshModelPosition + v.position * m_meshModelScale;
            const glm::vec3 fromPivot = worldBase - m_modalTransform.pivotWorld;
            const glm::vec3 scaledWorld = m_modalTransform.pivotWorld + fromPivot * axisScale;
            const glm::vec3 localScaled{
                std::abs(m_meshModelScale.x) > 1.0e-6F ? (scaledWorld.x - m_meshModelPosition.x) / m_meshModelScale.x : 0.0F,
                std::abs(m_meshModelScale.y) > 1.0e-6F ? (scaledWorld.y - m_meshModelPosition.y) / m_meshModelScale.y : 0.0F,
                std::abs(m_meshModelScale.z) > 1.0e-6F ? (scaledWorld.z - m_meshModelPosition.z) / m_meshModelScale.z : 0.0F,
            };
            m_meshModelVertices[static_cast<std::size_t>(v.index)].position = localScaled;
        }
    }
    else
    {
        for (const SceneLoopElementTransform& e : m_modalTransform.loopElements)
        {
            if (e.index < 0 || e.index >= static_cast<int>(m_loop.elements.size()))
            {
                continue;
            }
            m_loop.elements[static_cast<std::size_t>(e.index)].halfExtents = glm::max(e.halfExtents * axisScale, glm::vec3{0.05F});
        }
        for (const ScenePropTransform& p : m_modalTransform.props)
        {
            if (p.index < 0 || p.index >= static_cast<int>(m_map.props.size()))
            {
                continue;
            }
            m_map.props[static_cast<std::size_t>(p.index)].halfExtents = glm::max(p.halfExtents * axisScale, glm::vec3{0.05F});
        }
    }
}

void LevelEditor::ConfirmModalTransform()
{
    if (!m_modalTransform.active)
    {
        return;
    }
    AutoComputeLoopBoundsAndFootprint();
    m_modalTransform = ModalTransformState{};
    m_statusLine = "Transform confirmed";
}

void LevelEditor::CancelModalTransform()
{
    if (!m_modalTransform.active)
    {
        return;
    }
    m_modalTransform = ModalTransformState{};
    Undo();
    m_statusLine = "Transform canceled";
}

bool LevelEditor::WorldToScreenPoint(const glm::vec3& world, glm::vec2* outScreen) const
{
    if (outScreen == nullptr || m_lastFramebufferWidth <= 0 || m_lastFramebufferHeight <= 0)
    {
        return false;
    }
    const float aspect = static_cast<float>(m_lastFramebufferWidth) / static_cast<float>(m_lastFramebufferHeight);
    const glm::mat4 projection = glm::perspective(glm::radians(60.0F), aspect, 0.05F, 900.0F);
    const glm::mat4 view = glm::lookAt(m_cameraPosition, m_cameraPosition + CameraForward(), CameraUp());
    const glm::vec4 clip = projection * view * glm::vec4(world, 1.0F);
    if (clip.w <= 1.0e-6F)
    {
        return false;
    }
    const glm::vec3 ndc = glm::vec3(clip) / clip.w;
    if (ndc.z < -1.0F || ndc.z > 1.0F)
    {
        return false;
    }
    outScreen->x = (ndc.x * 0.5F + 0.5F) * static_cast<float>(m_lastFramebufferWidth);
    outScreen->y = (1.0F - (ndc.y * 0.5F + 0.5F)) * static_cast<float>(m_lastFramebufferHeight);
    return true;
}

void LevelEditor::ApplyBoxSelection(SelectionCombineMode mode)
{
    const glm::vec2 minCorner{
        std::min(m_boxSelection.start.x, m_boxSelection.end.x),
        std::min(m_boxSelection.start.y, m_boxSelection.end.y),
    };
    const glm::vec2 maxCorner{
        std::max(m_boxSelection.start.x, m_boxSelection.end.x),
        std::max(m_boxSelection.start.y, m_boxSelection.end.y),
    };
    if (glm::length(maxCorner - minCorner) < 3.0F)
    {
        return;
    }

    auto inRect = [&](const glm::vec2& p) {
        return p.x >= minCorner.x && p.x <= maxCorner.x && p.y >= minCorner.y && p.y <= maxCorner.y;
    };

    if (m_mode == Mode::LoopEditor && m_meshModelSceneEditEnabled)
    {
        if (m_meshEditMode == MeshEditMode::Vertex)
        {
            std::vector<int> hits;
            for (int i = 0; i < static_cast<int>(m_meshModelVertices.size()); ++i)
            {
                if (m_meshModelVertices[static_cast<std::size_t>(i)].deleted)
                {
                    continue;
                }
                glm::vec2 screen{0.0F};
                const glm::vec3 world = m_meshModelPosition + m_meshModelVertices[static_cast<std::size_t>(i)].position * m_meshModelScale;
                if (!WorldToScreenPoint(world, &screen) || !inRect(screen))
                {
                    continue;
                }
                hits.push_back(i);
            }
            auto current = m_meshModelVertexSelection;
            if (current.empty() && m_meshModelSelectedVertex >= 0)
            {
                current.push_back(m_meshModelSelectedVertex);
            }
            if (mode == SelectionCombineMode::Replace)
            {
                current = hits;
            }
            else if (mode == SelectionCombineMode::Add)
            {
                current.insert(current.end(), hits.begin(), hits.end());
            }
            else if (mode == SelectionCombineMode::Subtract)
            {
                current.erase(
                    std::remove_if(current.begin(), current.end(), [&hits](int idx) {
                        return std::find(hits.begin(), hits.end(), idx) != hits.end();
                    }),
                    current.end());
            }
            else
            {
                current.erase(
                    std::remove_if(current.begin(), current.end(), [&hits](int idx) {
                        return std::find(hits.begin(), hits.end(), idx) == hits.end();
                    }),
                    current.end());
            }
            std::sort(current.begin(), current.end());
            current.erase(std::unique(current.begin(), current.end()), current.end());
            m_meshModelVertexSelection = current;
            m_meshModelSelectedVertex = current.empty() ? -1 : current.back();
            return;
        }
        if (m_meshEditMode == MeshEditMode::Edge)
        {
            const std::vector<std::array<int, 2>> edges = BuildMeshModelEdges();
            std::vector<int> hits;
            for (int i = 0; i < static_cast<int>(edges.size()); ++i)
            {
                const auto edge = edges[static_cast<std::size_t>(i)];
                const glm::vec3 a = m_meshModelPosition + m_meshModelVertices[static_cast<std::size_t>(edge[0])].position * m_meshModelScale;
                const glm::vec3 b = m_meshModelPosition + m_meshModelVertices[static_cast<std::size_t>(edge[1])].position * m_meshModelScale;
                glm::vec2 screen{0.0F};
                if (!WorldToScreenPoint((a + b) * 0.5F, &screen) || !inRect(screen))
                {
                    continue;
                }
                hits.push_back(i);
            }
            std::vector<int> current = m_meshModelLoopSelectionEdges;
            if (current.empty() && m_meshModelSelectedEdge >= 0)
            {
                current.push_back(m_meshModelSelectedEdge);
            }
            if (mode == SelectionCombineMode::Replace)
            {
                current = hits;
            }
            else if (mode == SelectionCombineMode::Add)
            {
                current.insert(current.end(), hits.begin(), hits.end());
            }
            else if (mode == SelectionCombineMode::Subtract)
            {
                current.erase(
                    std::remove_if(current.begin(), current.end(), [&hits](int idx) {
                        return std::find(hits.begin(), hits.end(), idx) != hits.end();
                    }),
                    current.end());
            }
            else
            {
                current.erase(
                    std::remove_if(current.begin(), current.end(), [&hits](int idx) {
                        return std::find(hits.begin(), hits.end(), idx) == hits.end();
                    }),
                    current.end());
            }
            std::sort(current.begin(), current.end());
            current.erase(std::unique(current.begin(), current.end()), current.end());
            m_meshModelLoopSelectionEdges = current;
            m_meshModelRingSelectionEdges.clear();
            m_meshModelSelectedEdge = current.empty() ? -1 : current.back();
            return;
        }

        std::vector<int> hits;
        for (int i = 0; i < static_cast<int>(m_meshModelFaces.size()); ++i)
        {
            const MeshModelFace& face = m_meshModelFaces[static_cast<std::size_t>(i)];
            if (face.deleted)
            {
                continue;
            }
            glm::vec3 center{0.0F};
            int count = 0;
            const int vc = std::clamp(face.vertexCount, 3, 4);
            for (int k = 0; k < vc; ++k)
            {
                const int vi = face.indices[static_cast<std::size_t>(k)];
                if (vi < 0 || vi >= static_cast<int>(m_meshModelVertices.size()))
                {
                    continue;
                }
                center += m_meshModelPosition + m_meshModelVertices[static_cast<std::size_t>(vi)].position * m_meshModelScale;
                ++count;
            }
            if (count <= 0)
            {
                continue;
            }
            glm::vec2 screen{0.0F};
            if (WorldToScreenPoint(center / static_cast<float>(count), &screen) && inRect(screen))
            {
                hits.push_back(i);
            }
        }
        auto current = m_meshModelFaceSelection;
        if (current.empty() && m_meshModelSelectedFace >= 0)
        {
            current.push_back(m_meshModelSelectedFace);
        }
        if (mode == SelectionCombineMode::Replace)
        {
            current = hits;
        }
        else if (mode == SelectionCombineMode::Add)
        {
            current.insert(current.end(), hits.begin(), hits.end());
        }
        else if (mode == SelectionCombineMode::Subtract)
        {
            current.erase(
                std::remove_if(current.begin(), current.end(), [&hits](int idx) {
                    return std::find(hits.begin(), hits.end(), idx) != hits.end();
                }),
                current.end());
        }
        else
        {
            current.erase(
                std::remove_if(current.begin(), current.end(), [&hits](int idx) {
                    return std::find(hits.begin(), hits.end(), idx) == hits.end();
                }),
                current.end());
        }
        std::sort(current.begin(), current.end());
        current.erase(std::unique(current.begin(), current.end()), current.end());
        m_meshModelFaceSelection = current;
        m_meshModelSelectedFace = current.empty() ? -1 : current.back();
        return;
    }

    std::vector<int> hitLoop;
    std::vector<int> hitPlacement;
    std::vector<int> hitProps;

    if (m_mode == Mode::LoopEditor)
    {
        for (int i = 0; i < static_cast<int>(m_loop.elements.size()); ++i)
        {
            glm::vec2 screen{0.0F};
            if (WorldToScreenPoint(m_loop.elements[static_cast<std::size_t>(i)].position, &screen) && inRect(screen))
            {
                hitLoop.push_back(i);
            }
        }
    }
    else
    {
        for (int i = 0; i < static_cast<int>(m_map.props.size()); ++i)
        {
            glm::vec2 screen{0.0F};
            if (WorldToScreenPoint(m_map.props[static_cast<std::size_t>(i)].position, &screen) && inRect(screen))
            {
                hitProps.push_back(i);
            }
        }
        for (int i = 0; i < static_cast<int>(m_map.placements.size()); ++i)
        {
            const LoopPlacement& p = m_map.placements[static_cast<std::size_t>(i)];
            glm::vec2 screen{0.0F};
            if (WorldToScreenPoint(TileCenter(p.tileX, p.tileY), &screen) && inRect(screen))
            {
                hitPlacement.push_back(i);
            }
        }
    }

    auto applyToList = [&](std::vector<int>* list, const std::vector<int>& hits) {
        if (list == nullptr)
        {
            return;
        }
        if (mode == SelectionCombineMode::Replace)
        {
            *list = hits;
        }
        else if (mode == SelectionCombineMode::Add)
        {
            list->insert(list->end(), hits.begin(), hits.end());
        }
        else if (mode == SelectionCombineMode::Subtract)
        {
            list->erase(
                std::remove_if(list->begin(), list->end(), [&hits](int idx) {
                    return std::find(hits.begin(), hits.end(), idx) != hits.end();
                }),
                list->end());
        }
        else
        {
            list->erase(
                std::remove_if(list->begin(), list->end(), [&hits](int idx) {
                    return std::find(hits.begin(), hits.end(), idx) == hits.end();
                }),
                list->end());
        }
        std::sort(list->begin(), list->end());
        list->erase(std::unique(list->begin(), list->end()), list->end());
    };

    applyToList(&m_selectedLoopElements, hitLoop);
    applyToList(&m_selectedMapPlacements, hitPlacement);
    applyToList(&m_selectedProps, hitProps);
    if (!m_selectedLoopElements.empty())
    {
        m_selection = Selection{SelectionKind::LoopElement, m_selectedLoopElements.back()};
    }
    else if (!m_selectedMapPlacements.empty())
    {
        m_selection = Selection{SelectionKind::MapPlacement, m_selectedMapPlacements.back()};
    }
    else if (!m_selectedProps.empty())
    {
        m_selection = Selection{SelectionKind::Prop, m_selectedProps.back()};
    }
    else
    {
        m_selection = Selection{};
    }
}

void LevelEditor::ApplyLassoSelection(SelectionCombineMode mode)
{
    if (m_lassoSelection.points.size() < 3U)
    {
        return;
    }
    auto inLasso = [this](const glm::vec2& p) { return PointInPolygon2D(m_lassoSelection.points, p); };

    if (m_mode == Mode::LoopEditor && m_meshModelSceneEditEnabled)
    {
        if (m_meshEditMode == MeshEditMode::Vertex)
        {
            std::vector<int> hits;
            for (int i = 0; i < static_cast<int>(m_meshModelVertices.size()); ++i)
            {
                if (m_meshModelVertices[static_cast<std::size_t>(i)].deleted)
                {
                    continue;
                }
                glm::vec2 s{0.0F};
                const glm::vec3 world = m_meshModelPosition + m_meshModelVertices[static_cast<std::size_t>(i)].position * m_meshModelScale;
                if (WorldToScreenPoint(world, &s) && inLasso(s))
                {
                    hits.push_back(i);
                }
            }
            auto current = m_meshModelVertexSelection;
            if (current.empty() && m_meshModelSelectedVertex >= 0)
            {
                current.push_back(m_meshModelSelectedVertex);
            }
            if (mode == SelectionCombineMode::Replace)
            {
                current = hits;
            }
            else if (mode == SelectionCombineMode::Add)
            {
                current.insert(current.end(), hits.begin(), hits.end());
            }
            else if (mode == SelectionCombineMode::Subtract)
            {
                current.erase(
                    std::remove_if(current.begin(), current.end(), [&hits](int idx) {
                        return std::find(hits.begin(), hits.end(), idx) != hits.end();
                    }),
                    current.end());
            }
            else
            {
                current.erase(
                    std::remove_if(current.begin(), current.end(), [&hits](int idx) {
                        return std::find(hits.begin(), hits.end(), idx) == hits.end();
                    }),
                    current.end());
            }
            std::sort(current.begin(), current.end());
            current.erase(std::unique(current.begin(), current.end()), current.end());
            m_meshModelVertexSelection = current;
            m_meshModelSelectedVertex = current.empty() ? -1 : current.back();
            return;
        }
        if (m_meshEditMode == MeshEditMode::Edge)
        {
            const std::vector<std::array<int, 2>> edges = BuildMeshModelEdges();
            std::vector<int> hits;
            for (int i = 0; i < static_cast<int>(edges.size()); ++i)
            {
                const auto edge = edges[static_cast<std::size_t>(i)];
                const glm::vec3 a = m_meshModelPosition + m_meshModelVertices[static_cast<std::size_t>(edge[0])].position * m_meshModelScale;
                const glm::vec3 b = m_meshModelPosition + m_meshModelVertices[static_cast<std::size_t>(edge[1])].position * m_meshModelScale;
                glm::vec2 s{0.0F};
                if (WorldToScreenPoint((a + b) * 0.5F, &s) && inLasso(s))
                {
                    hits.push_back(i);
                }
            }
            auto current = m_meshModelLoopSelectionEdges;
            if (current.empty() && m_meshModelSelectedEdge >= 0)
            {
                current.push_back(m_meshModelSelectedEdge);
            }
            if (mode == SelectionCombineMode::Replace)
            {
                current = hits;
            }
            else if (mode == SelectionCombineMode::Add)
            {
                current.insert(current.end(), hits.begin(), hits.end());
            }
            else if (mode == SelectionCombineMode::Subtract)
            {
                current.erase(
                    std::remove_if(current.begin(), current.end(), [&hits](int idx) {
                        return std::find(hits.begin(), hits.end(), idx) != hits.end();
                    }),
                    current.end());
            }
            else
            {
                current.erase(
                    std::remove_if(current.begin(), current.end(), [&hits](int idx) {
                        return std::find(hits.begin(), hits.end(), idx) == hits.end();
                    }),
                    current.end());
            }
            std::sort(current.begin(), current.end());
            current.erase(std::unique(current.begin(), current.end()), current.end());
            m_meshModelLoopSelectionEdges = current;
            m_meshModelRingSelectionEdges.clear();
            m_meshModelSelectedEdge = current.empty() ? -1 : current.back();
            return;
        }
        if (m_meshEditMode == MeshEditMode::Face)
        {
            std::vector<int> hits;
            for (int i = 0; i < static_cast<int>(m_meshModelFaces.size()); ++i)
            {
                const MeshModelFace& face = m_meshModelFaces[static_cast<std::size_t>(i)];
                if (face.deleted)
                {
                    continue;
                }
                glm::vec3 center{0.0F};
                int count = 0;
                const int vc = std::clamp(face.vertexCount, 3, 4);
                for (int k = 0; k < vc; ++k)
                {
                    const int vi = face.indices[static_cast<std::size_t>(k)];
                    if (vi < 0 || vi >= static_cast<int>(m_meshModelVertices.size()))
                    {
                        continue;
                    }
                    center += m_meshModelPosition + m_meshModelVertices[static_cast<std::size_t>(vi)].position * m_meshModelScale;
                    ++count;
                }
                if (count <= 0)
                {
                    continue;
                }
                glm::vec2 s{0.0F};
                if (WorldToScreenPoint(center / static_cast<float>(count), &s) && inLasso(s))
                {
                    hits.push_back(i);
                }
            }
            auto current = m_meshModelFaceSelection;
            if (current.empty() && m_meshModelSelectedFace >= 0)
            {
                current.push_back(m_meshModelSelectedFace);
            }
            if (mode == SelectionCombineMode::Replace)
            {
                current = hits;
            }
            else if (mode == SelectionCombineMode::Add)
            {
                current.insert(current.end(), hits.begin(), hits.end());
            }
            else if (mode == SelectionCombineMode::Subtract)
            {
                current.erase(
                    std::remove_if(current.begin(), current.end(), [&hits](int idx) {
                        return std::find(hits.begin(), hits.end(), idx) != hits.end();
                    }),
                    current.end());
            }
            else
            {
                current.erase(
                    std::remove_if(current.begin(), current.end(), [&hits](int idx) {
                        return std::find(hits.begin(), hits.end(), idx) == hits.end();
                    }),
                    current.end());
            }
            std::sort(current.begin(), current.end());
            current.erase(std::unique(current.begin(), current.end()), current.end());
            m_meshModelFaceSelection = current;
            m_meshModelSelectedFace = current.empty() ? -1 : current.back();
            return;
        }
    }

    std::vector<int> hitLoop;
    std::vector<int> hitPlacement;
    std::vector<int> hitProps;

    if (m_mode == Mode::LoopEditor)
    {
        for (int i = 0; i < static_cast<int>(m_loop.elements.size()); ++i)
        {
            glm::vec2 s{0.0F};
            if (WorldToScreenPoint(m_loop.elements[static_cast<std::size_t>(i)].position, &s) && inLasso(s))
            {
                hitLoop.push_back(i);
            }
        }
    }
    else
    {
        for (int i = 0; i < static_cast<int>(m_map.props.size()); ++i)
        {
            glm::vec2 s{0.0F};
            if (WorldToScreenPoint(m_map.props[static_cast<std::size_t>(i)].position, &s) && inLasso(s))
            {
                hitProps.push_back(i);
            }
        }
        for (int i = 0; i < static_cast<int>(m_map.placements.size()); ++i)
        {
            const LoopPlacement& p = m_map.placements[static_cast<std::size_t>(i)];
            glm::vec2 s{0.0F};
            if (WorldToScreenPoint(TileCenter(p.tileX, p.tileY), &s) && inLasso(s))
            {
                hitPlacement.push_back(i);
            }
        }
    }

    auto applyToList = [&](std::vector<int>* list, const std::vector<int>& hits) {
        if (list == nullptr)
        {
            return;
        }
        if (mode == SelectionCombineMode::Replace)
        {
            *list = hits;
        }
        else if (mode == SelectionCombineMode::Add)
        {
            list->insert(list->end(), hits.begin(), hits.end());
        }
        else if (mode == SelectionCombineMode::Subtract)
        {
            list->erase(
                std::remove_if(list->begin(), list->end(), [&hits](int idx) {
                    return std::find(hits.begin(), hits.end(), idx) != hits.end();
                }),
                list->end());
        }
        else
        {
            list->erase(
                std::remove_if(list->begin(), list->end(), [&hits](int idx) {
                    return std::find(hits.begin(), hits.end(), idx) == hits.end();
                }),
                list->end());
        }
        std::sort(list->begin(), list->end());
        list->erase(std::unique(list->begin(), list->end()), list->end());
    };

    applyToList(&m_selectedLoopElements, hitLoop);
    applyToList(&m_selectedMapPlacements, hitPlacement);
    applyToList(&m_selectedProps, hitProps);
    if (!m_selectedLoopElements.empty())
    {
        m_selection = Selection{SelectionKind::LoopElement, m_selectedLoopElements.back()};
    }
    else if (!m_selectedMapPlacements.empty())
    {
        m_selection = Selection{SelectionKind::MapPlacement, m_selectedMapPlacements.back()};
    }
    else if (!m_selectedProps.empty())
    {
        m_selection = Selection{SelectionKind::Prop, m_selectedProps.back()};
    }
    else
    {
        m_selection = Selection{};
    }
}

void LevelEditor::PlaceImportedAssetAtHovered(const std::string& relativeAssetPath)
{
    if (m_mode != Mode::MapEditor || !m_hoveredTileValid)
    {
        m_statusLine = "Asset placement requires Map Editor + hovered tile";
        return;
    }

    const engine::assets::AssetKind kind = engine::assets::AssetRegistry::KindFromPath(std::filesystem::path(relativeAssetPath));
    if (kind != engine::assets::AssetKind::Mesh)
    {
        m_statusLine = "Only mesh assets can be placed in scene (" + relativeAssetPath + ")";
        return;
    }
    {
        const std::string ext = std::filesystem::path(relativeAssetPath).extension().string();
        std::string extLower = ext;
        std::transform(extLower.begin(), extLower.end(), extLower.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
        if (extLower == ".fbx")
        {
            m_statusLine = "FBX runtime mesh loading is not supported yet. Use .obj/.gltf/.glb";
            return;
        }
    }

    PushHistorySnapshot();

    PropInstance prop;
    prop.name = BuildUniquePropName(std::filesystem::path(relativeAssetPath).stem().string());
    prop.type = PropType::MeshAsset;
    prop.meshAsset = relativeAssetPath;
    prop.materialAsset.clear();
    prop.position = m_hoveredWorld + glm::vec3{0.0F, 0.8F, 0.0F};
    prop.halfExtents = glm::vec3{0.8F, 0.8F, 0.8F};
    std::string meshLoadError;
    if (const engine::assets::MeshData* meshData =
            m_meshLibrary.LoadMesh(m_assetRegistry.AbsolutePath(relativeAssetPath), &meshLoadError);
        meshData != nullptr && meshData->loaded)
    {
        const glm::vec3 boundsSize = glm::max(glm::vec3{0.1F}, meshData->boundsMax - meshData->boundsMin);
        prop.halfExtents = glm::clamp(boundsSize * 0.5F, glm::vec3{0.2F}, glm::vec3{4.0F});
        prop.position.y = m_hoveredWorld.y + prop.halfExtents.y;
    }
    else if (!meshLoadError.empty())
    {
        m_statusLine = "Mesh load failed: " + meshLoadError;
    }
    prop.colliderHalfExtents = prop.halfExtents;
    prop.colliderType = ColliderType::Box;
    if (m_gridSnap)
    {
        prop.position.x = std::round(prop.position.x / m_gridStep) * m_gridStep;
        prop.position.z = std::round(prop.position.z / m_gridStep) * m_gridStep;
    }

    m_map.props.push_back(prop);
    SelectSingle(Selection{SelectionKind::Prop, static_cast<int>(m_map.props.size()) - 1});
    m_statusLine = "Placed asset " + relativeAssetPath;
}

void LevelEditor::InstantiatePrefabAtHovered(const std::string& prefabId)
{
    if (m_mode != Mode::MapEditor || !m_hoveredTileValid)
    {
        m_statusLine = "Prefab instantiate requires Map Editor + hovered tile";
        return;
    }

    PrefabAsset prefab;
    std::string error;
    if (!LevelAssetIO::LoadPrefab(prefabId, &prefab, &error))
    {
        m_statusLine = "Load prefab failed: " + error;
        return;
    }
    if (prefab.props.empty())
    {
        m_statusLine = "Prefab is empty.";
        return;
    }

    PushHistorySnapshot();
    const std::string instanceId = prefab.id + "_inst_" + std::to_string(m_nextPrefabInstanceId++);
    std::vector<int> newIndices;
    newIndices.reserve(prefab.props.size());
    for (const PropInstance& src : prefab.props)
    {
        PropInstance prop = src;
        prop.name = BuildUniquePropName(src.name.empty() ? "prop" : src.name);
        prop.position += m_hoveredWorld;
        prop.prefabSourceId = prefab.id;
        prop.prefabInstanceId = instanceId;
        m_map.props.push_back(prop);
        newIndices.push_back(static_cast<int>(m_map.props.size()) - 1);
    }

    m_selectedProps = newIndices;
    m_selection = Selection{SelectionKind::Prop, newIndices.back()};
    m_statusLine = "Instantiated prefab " + prefab.id;
}

void LevelEditor::SaveSelectedPropsAsPrefab(const std::string& prefabId)
{
    if (m_mode != Mode::MapEditor)
    {
        m_statusLine = "Save prefab available only in Map Editor";
        return;
    }

    const std::vector<int> indices = SortedUniqueValidSelection(SelectionKind::Prop);
    if (indices.empty())
    {
        m_statusLine = "Select at least one prop to create prefab";
        return;
    }

    glm::vec3 pivot{0.0F};
    for (int idx : indices)
    {
        pivot += m_map.props[static_cast<std::size_t>(idx)].position;
    }
    pivot /= static_cast<float>(indices.size());

    PrefabAsset prefab;
    prefab.id = prefabId;
    prefab.displayName = prefabId;
    for (int idx : indices)
    {
        PropInstance copy = m_map.props[static_cast<std::size_t>(idx)];
        copy.position -= pivot;
        copy.prefabSourceId.clear();
        copy.prefabInstanceId.clear();
        prefab.props.push_back(copy);
    }

    std::string error;
    if (LevelAssetIO::SavePrefab(prefab, &error))
    {
        RefreshLibraries();
        m_statusLine = "Saved prefab " + prefab.id;
        return;
    }
    m_statusLine = "Save prefab failed: " + error;
}

void LevelEditor::ReapplySelectedPrefabInstance()
{
    if (m_mode != Mode::MapEditor)
    {
        m_statusLine = "Reapply prefab available only in Map Editor";
        return;
    }

    const std::vector<int> selected = SortedUniqueValidSelection(SelectionKind::Prop);
    if (selected.empty())
    {
        m_statusLine = "Select prefab instance props first";
        return;
    }

    const PropInstance& seed = m_map.props[static_cast<std::size_t>(selected.front())];
    if (seed.prefabSourceId.empty() || seed.prefabInstanceId.empty())
    {
        m_statusLine = "Selected prop is not a prefab instance";
        return;
    }

    PrefabAsset prefab;
    std::string error;
    if (!LevelAssetIO::LoadPrefab(seed.prefabSourceId, &prefab, &error))
    {
        m_statusLine = "Load prefab failed: " + error;
        return;
    }

    std::vector<int> instanceIndices;
    glm::vec3 anchor{0.0F};
    for (int i = 0; i < static_cast<int>(m_map.props.size()); ++i)
    {
        const PropInstance& prop = m_map.props[static_cast<std::size_t>(i)];
        if (prop.prefabInstanceId == seed.prefabInstanceId)
        {
            instanceIndices.push_back(i);
            anchor += prop.position;
        }
    }
    if (instanceIndices.empty())
    {
        m_statusLine = "Prefab instance not found in map";
        return;
    }
    anchor /= static_cast<float>(instanceIndices.size());

    PushHistorySnapshot();
    for (auto it = instanceIndices.rbegin(); it != instanceIndices.rend(); ++it)
    {
        m_map.props.erase(m_map.props.begin() + *it);
    }

    std::vector<int> newIndices;
    for (const PropInstance& src : prefab.props)
    {
        PropInstance prop = src;
        prop.position += anchor;
        prop.prefabSourceId = prefab.id;
        prop.prefabInstanceId = seed.prefabInstanceId;
        prop.name = BuildUniquePropName(src.name.empty() ? "prop" : src.name);
        m_map.props.push_back(prop);
        newIndices.push_back(static_cast<int>(m_map.props.size()) - 1);
    }

    m_selectedProps = newIndices;
    m_selection = Selection{SelectionKind::Prop, newIndices.empty() ? -1 : newIndices.back()};
    m_statusLine = "Reapplied prefab instance " + seed.prefabInstanceId;
}

const MaterialAsset* LevelEditor::GetMaterialCached(const std::string& materialId) const
{
    if (materialId.empty())
    {
        return nullptr;
    }

    const auto cached = m_materialCache.find(materialId);
    if (cached != m_materialCache.end())
    {
        return &cached->second;
    }

    MaterialAsset loaded;
    if (!LevelAssetIO::LoadMaterial(materialId, &loaded, nullptr))
    {
        return nullptr;
    }

    const auto [insertedIt, _] = m_materialCache.emplace(materialId, std::move(loaded));
    return &insertedIt->second;
}

const AnimationClipAsset* LevelEditor::GetAnimationClipCached(const std::string& clipId) const
{
    if (clipId.empty())
    {
        return nullptr;
    }

    const auto cached = m_animationCache.find(clipId);
    if (cached != m_animationCache.end())
    {
        return &cached->second;
    }

    AnimationClipAsset loaded;
    if (!LevelAssetIO::LoadAnimationClip(clipId, &loaded, nullptr))
    {
        return nullptr;
    }

    const auto [insertedIt, _] = m_animationCache.emplace(clipId, std::move(loaded));
    return &insertedIt->second;
}

void LevelEditor::ResetMeshModelerToCube()
{
    m_meshModelVertices.clear();
    m_meshModelFaces.clear();

    const std::array<glm::vec3, 8> cubeVerts{
        glm::vec3{-0.5F, -0.5F, -0.5F},
        glm::vec3{0.5F, -0.5F, -0.5F},
        glm::vec3{0.5F, 0.5F, -0.5F},
        glm::vec3{-0.5F, 0.5F, -0.5F},
        glm::vec3{-0.5F, -0.5F, 0.5F},
        glm::vec3{0.5F, -0.5F, 0.5F},
        glm::vec3{0.5F, 0.5F, 0.5F},
        glm::vec3{-0.5F, 0.5F, 0.5F},
    };
    for (const glm::vec3& v : cubeVerts)
    {
        m_meshModelVertices.push_back(MeshModelVertex{v, false});
    }

    const std::array<std::array<int, 4>, 6> faces{
        std::array<int, 4>{0, 1, 2, 3},
        std::array<int, 4>{4, 5, 6, 7},
        std::array<int, 4>{0, 4, 5, 1},
        std::array<int, 4>{3, 2, 6, 7},
        std::array<int, 4>{0, 3, 7, 4},
        std::array<int, 4>{1, 5, 6, 2},
    };
    for (const auto& f : faces)
    {
        m_meshModelFaces.push_back(MeshModelFace{f, false});
    }

    ResetMeshModelerCommonState();
}

void LevelEditor::ResetMeshModelerToSquare()
{
    m_meshModelVertices.clear();
    m_meshModelFaces.clear();

    const float r = glm::clamp(m_meshPrimitiveRadius, 0.05F, 6.0F);
    const float y = 0.0F;
    m_meshModelVertices.push_back(MeshModelVertex{glm::vec3{-r, y, -r}, false});
    m_meshModelVertices.push_back(MeshModelVertex{glm::vec3{r, y, -r}, false});
    m_meshModelVertices.push_back(MeshModelVertex{glm::vec3{r, y, r}, false});
    m_meshModelVertices.push_back(MeshModelVertex{glm::vec3{-r, y, r}, false});
    m_meshModelFaces.push_back(MeshModelFace{{0, 1, 2, 3}, false, 4});

    ResetMeshModelerCommonState();
}

void LevelEditor::ResetMeshModelerToCircle(int segments, float radius)
{
    m_meshModelVertices.clear();
    m_meshModelFaces.clear();

    segments = std::clamp(segments, 6, 128);
    radius = glm::clamp(radius, 0.05F, 8.0F);
    const float y = 0.0F;

    m_meshModelVertices.push_back(MeshModelVertex{glm::vec3{0.0F, y, 0.0F}, false});
    for (int i = 0; i < segments; ++i)
    {
        const float t = static_cast<float>(i) / static_cast<float>(segments) * glm::two_pi<float>();
        m_meshModelVertices.push_back(MeshModelVertex{
            glm::vec3{std::cos(t) * radius, y, std::sin(t) * radius},
            false,
        });
    }

    for (int i = 0; i < segments; ++i)
    {
        const int v0 = 1 + i;
        const int v1 = 1 + ((i + 1) % segments);
        m_meshModelFaces.push_back(MeshModelFace{{0, v1, v0, v0}, false, 3});
    }

    ResetMeshModelerCommonState();
}

void LevelEditor::ResetMeshModelerToSphere(int latSegments, int lonSegments, float radius)
{
    m_meshModelVertices.clear();
    m_meshModelFaces.clear();

    latSegments = std::clamp(latSegments, 6, 96);
    lonSegments = std::clamp(lonSegments, 8, 192);
    radius = glm::clamp(radius, 0.05F, 8.0F);

    const engine::render::MeshGeometry sphereMesh = BuildUvSphereGeometry(latSegments, lonSegments);
    m_meshModelVertices.reserve(sphereMesh.positions.size());
    for (const glm::vec3& p : sphereMesh.positions)
    {
        m_meshModelVertices.push_back(MeshModelVertex{p * radius, false});
    }
    for (std::size_t i = 0; i + 2 < sphereMesh.indices.size(); i += 3)
    {
        const int a = static_cast<int>(sphereMesh.indices[i]);
        const int b = static_cast<int>(sphereMesh.indices[i + 1]);
        const int c = static_cast<int>(sphereMesh.indices[i + 2]);
        m_meshModelFaces.push_back(MeshModelFace{{a, b, c, c}, false, 3});
    }

    ResetMeshModelerCommonState();
}

void LevelEditor::ResetMeshModelerToCapsule(
    int segments,
    int hemiRings,
    int cylinderRings,
    float radius,
    float height
)
{
    m_meshModelVertices.clear();
    m_meshModelFaces.clear();

    segments = std::clamp(segments, 8, 128);
    hemiRings = std::clamp(hemiRings, 3, 24);
    cylinderRings = std::clamp(cylinderRings, 0, 24);
    radius = glm::clamp(radius, 0.05F, 6.0F);
    height = glm::clamp(height, radius * 2.0F + 0.05F, 18.0F);

    const float halfCylinder = std::max(0.0F, height * 0.5F - radius);

    struct RingDesc
    {
        float y = 0.0F;
        float r = 0.0F;
    };
    std::vector<RingDesc> rings;
    rings.reserve(static_cast<std::size_t>(hemiRings * 2 + cylinderRings + 4));

    rings.push_back(RingDesc{halfCylinder + radius, 0.0F});
    for (int i = 1; i < hemiRings; ++i)
    {
        const float a = static_cast<float>(i) / static_cast<float>(hemiRings) * glm::half_pi<float>();
        rings.push_back(RingDesc{
            halfCylinder + std::cos(a) * radius,
            std::sin(a) * radius,
        });
    }
    rings.push_back(RingDesc{halfCylinder, radius});
    if (halfCylinder > 1.0e-5F)
    {
        for (int i = 1; i <= cylinderRings; ++i)
        {
            const float t = static_cast<float>(i) / static_cast<float>(cylinderRings + 1);
            rings.push_back(RingDesc{halfCylinder - t * (2.0F * halfCylinder), radius});
        }
    }
    rings.push_back(RingDesc{-halfCylinder, radius});
    for (int i = hemiRings - 1; i >= 1; --i)
    {
        const float a = static_cast<float>(i) / static_cast<float>(hemiRings) * glm::half_pi<float>();
        rings.push_back(RingDesc{
            -halfCylinder - std::cos(a) * radius,
            std::sin(a) * radius,
        });
    }
    rings.push_back(RingDesc{-(halfCylinder + radius), 0.0F});

    std::vector<std::vector<int>> ringVertexIndices;
    ringVertexIndices.reserve(rings.size());
    for (const RingDesc& ring : rings)
    {
        std::vector<int> ringIndices;
        if (ring.r <= 1.0e-5F)
        {
            m_meshModelVertices.push_back(MeshModelVertex{glm::vec3{0.0F, ring.y, 0.0F}, false});
            ringIndices.push_back(static_cast<int>(m_meshModelVertices.size()) - 1);
        }
        else
        {
            ringIndices.reserve(static_cast<std::size_t>(segments));
            for (int s = 0; s < segments; ++s)
            {
                const float t = static_cast<float>(s) / static_cast<float>(segments) * glm::two_pi<float>();
                m_meshModelVertices.push_back(MeshModelVertex{
                    glm::vec3{std::cos(t) * ring.r, ring.y, std::sin(t) * ring.r},
                    false,
                });
                ringIndices.push_back(static_cast<int>(m_meshModelVertices.size()) - 1);
            }
        }
        ringVertexIndices.push_back(std::move(ringIndices));
    }

    for (std::size_t ring = 0; ring + 1 < ringVertexIndices.size(); ++ring)
    {
        const std::vector<int>& a = ringVertexIndices[ring];
        const std::vector<int>& b = ringVertexIndices[ring + 1];
        if (a.empty() || b.empty())
        {
            continue;
        }
        if (a.size() == 1U && b.size() > 1U)
        {
            const int pole = a.front();
            for (std::size_t s = 0; s < b.size(); ++s)
            {
                const int v0 = b[s];
                const int v1 = b[(s + 1U) % b.size()];
                m_meshModelFaces.push_back(MeshModelFace{{pole, v1, v0, v0}, false, 3});
            }
            continue;
        }
        if (a.size() > 1U && b.size() == 1U)
        {
            const int pole = b.front();
            for (std::size_t s = 0; s < a.size(); ++s)
            {
                const int v0 = a[s];
                const int v1 = a[(s + 1U) % a.size()];
                m_meshModelFaces.push_back(MeshModelFace{{pole, v0, v1, v1}, false, 3});
            }
            continue;
        }
        const std::size_t shared = std::min(a.size(), b.size());
        for (std::size_t s = 0; s < shared; ++s)
        {
            const int a0 = a[s];
            const int a1 = a[(s + 1U) % a.size()];
            const int b1 = b[(s + 1U) % b.size()];
            const int b0 = b[s];
            m_meshModelFaces.push_back(MeshModelFace{{a0, a1, b1, b0}, false, 4});
        }
    }

    ResetMeshModelerCommonState();
}

void LevelEditor::ResetMeshModelerCommonState()
{
    if (!m_meshModelVertices.empty())
    {
        CleanupMeshModelTopology();
    }

    m_meshModelSelectedFace = m_meshModelFaces.empty() ? -1 : 0;
    m_meshModelSelectedVertex = m_meshModelVertices.empty() ? -1 : 0;
    m_meshModelFaceSelection.clear();
    m_meshModelVertexSelection.clear();
    if (m_meshModelSelectedFace >= 0)
    {
        m_meshModelFaceSelection.push_back(m_meshModelSelectedFace);
    }
    if (m_meshModelSelectedVertex >= 0)
    {
        m_meshModelVertexSelection.push_back(m_meshModelSelectedVertex);
    }
    m_meshModelHoveredFace = -1;
    m_meshModelHoveredVertex = -1;
    const std::vector<std::array<int, 2>> edges = BuildMeshModelEdges();
    m_meshModelSelectedEdge = edges.empty() ? -1 : 0;
    m_meshModelHoveredEdge = -1;
    m_meshModelLoopSelectionEdges.clear();
    m_meshModelRingSelectionEdges.clear();
    m_meshModelPosition = glm::vec3{0.0F, 1.1F, 0.0F};
    m_meshModelScale = glm::vec3{1.0F, 1.0F, 1.0F};
    m_meshModelExtrudeDistance = 0.6F;
    m_meshModelVertexDelta = glm::vec3{0.0F};
    m_meshModelBevelDistance = 0.15F;
    m_meshModelBevelSegments = 2;
    m_meshModelBevelProfile = 1.0F;
    m_meshModelBevelUseMiter = true;
    m_meshModelLoopCutRatio = 0.5F;
    m_meshModelBridgeEdgeA = -1;
    m_meshModelBridgeEdgeB = -1;
    m_meshModelMergeKeepVertex = -1;
    m_meshModelMergeRemoveVertex = -1;
    m_meshModelKnifeEnabled = false;
    m_meshModelLoopCutToolEnabled = false;
    m_meshModelKnifeHasFirstPoint = false;
    m_meshModelKnifeFaceIndex = -1;
    m_meshModelKnifeFirstPointLocal = glm::vec3{0.0F};
    m_meshModelKnifeFirstPointWorld = glm::vec3{0.0F};
    m_meshModelKnifePreviewValid = false;
    m_meshModelKnifePreviewWorld = glm::vec3{0.0F};
    m_meshModelKnifePreviewSegments.clear();
    m_meshModelBatchOperation = MeshBatchEdgeOperation::Bevel;
    m_meshModelBatchGizmoEnabled = true;
    m_meshModelBatchDragActive = false;
    m_meshModelBatchPreviewDistance = m_meshModelBevelDistance;
    m_meshModelBatchDragPivot = glm::vec3{0.0F};
    m_meshModelBatchDragDirection = glm::vec3{0.0F, 1.0F, 0.0F};
    m_meshModelBatchDragPlaneNormal = glm::vec3{1.0F, 0.0F, 0.0F};
    m_meshModelBatchDragStartScalar = 0.0F;
}

void LevelEditor::MeshModelerSubdivideFace(int faceIndex)
{
    if (faceIndex < 0 || faceIndex >= static_cast<int>(m_meshModelFaces.size()))
    {
        return;
    }
    MeshModelFace& face = m_meshModelFaces[static_cast<std::size_t>(faceIndex)];
    if (face.deleted || std::clamp(face.vertexCount, 3, 4) != 4)
    {
        return;
    }

    const int i0 = face.indices[0];
    const int i1 = face.indices[1];
    const int i2 = face.indices[2];
    const int i3 = face.indices[3];
    if (i0 < 0 || i1 < 0 || i2 < 0 || i3 < 0 ||
        i0 >= static_cast<int>(m_meshModelVertices.size()) ||
        i1 >= static_cast<int>(m_meshModelVertices.size()) ||
        i2 >= static_cast<int>(m_meshModelVertices.size()) ||
        i3 >= static_cast<int>(m_meshModelVertices.size()))
    {
        return;
    }

    const glm::vec3 v0 = m_meshModelVertices[static_cast<std::size_t>(i0)].position;
    const glm::vec3 v1 = m_meshModelVertices[static_cast<std::size_t>(i1)].position;
    const glm::vec3 v2 = m_meshModelVertices[static_cast<std::size_t>(i2)].position;
    const glm::vec3 v3 = m_meshModelVertices[static_cast<std::size_t>(i3)].position;

    const auto addVertex = [this](const glm::vec3& p) {
        m_meshModelVertices.push_back(MeshModelVertex{p, false});
        return static_cast<int>(m_meshModelVertices.size()) - 1;
    };

    const int m01 = addVertex((v0 + v1) * 0.5F);
    const int m12 = addVertex((v1 + v2) * 0.5F);
    const int m23 = addVertex((v2 + v3) * 0.5F);
    const int m30 = addVertex((v3 + v0) * 0.5F);
    const int center = addVertex((v0 + v1 + v2 + v3) * 0.25F);

    face.deleted = true;
    m_meshModelFaces.push_back(MeshModelFace{{i0, m01, center, m30}, false});
    m_meshModelFaces.push_back(MeshModelFace{{m01, i1, m12, center}, false});
    m_meshModelFaces.push_back(MeshModelFace{{center, m12, i2, m23}, false});
    m_meshModelFaces.push_back(MeshModelFace{{m30, center, m23, i3}, false});
    m_meshModelSelectedFace = static_cast<int>(m_meshModelFaces.size()) - 1;
}

void LevelEditor::MeshModelerCutFace(int faceIndex, bool verticalCut)
{
    if (faceIndex < 0 || faceIndex >= static_cast<int>(m_meshModelFaces.size()))
    {
        return;
    }
    MeshModelFace& face = m_meshModelFaces[static_cast<std::size_t>(faceIndex)];
    if (face.deleted || std::clamp(face.vertexCount, 3, 4) != 4)
    {
        return;
    }

    const int i0 = face.indices[0];
    const int i1 = face.indices[1];
    const int i2 = face.indices[2];
    const int i3 = face.indices[3];
    if (i0 < 0 || i1 < 0 || i2 < 0 || i3 < 0 ||
        i0 >= static_cast<int>(m_meshModelVertices.size()) ||
        i1 >= static_cast<int>(m_meshModelVertices.size()) ||
        i2 >= static_cast<int>(m_meshModelVertices.size()) ||
        i3 >= static_cast<int>(m_meshModelVertices.size()))
    {
        return;
    }

    const glm::vec3 v0 = m_meshModelVertices[static_cast<std::size_t>(i0)].position;
    const glm::vec3 v1 = m_meshModelVertices[static_cast<std::size_t>(i1)].position;
    const glm::vec3 v2 = m_meshModelVertices[static_cast<std::size_t>(i2)].position;
    const glm::vec3 v3 = m_meshModelVertices[static_cast<std::size_t>(i3)].position;
    const auto addVertex = [this](const glm::vec3& p) {
        m_meshModelVertices.push_back(MeshModelVertex{p, false});
        return static_cast<int>(m_meshModelVertices.size()) - 1;
    };

    face.deleted = true;
    if (verticalCut)
    {
        const int m01 = addVertex((v0 + v1) * 0.5F);
        const int m32 = addVertex((v3 + v2) * 0.5F);
        m_meshModelFaces.push_back(MeshModelFace{{i0, m01, m32, i3}, false});
        m_meshModelFaces.push_back(MeshModelFace{{m01, i1, i2, m32}, false});
    }
    else
    {
        const int m03 = addVertex((v0 + v3) * 0.5F);
        const int m12 = addVertex((v1 + v2) * 0.5F);
        m_meshModelFaces.push_back(MeshModelFace{{i0, i1, m12, m03}, false});
        m_meshModelFaces.push_back(MeshModelFace{{m03, m12, i2, i3}, false});
    }
    m_meshModelSelectedFace = static_cast<int>(m_meshModelFaces.size()) - 1;
}

void LevelEditor::MeshModelerExtrudeFace(int faceIndex, float distance)
{
    if (faceIndex < 0 || faceIndex >= static_cast<int>(m_meshModelFaces.size()))
    {
        return;
    }
    MeshModelFace& face = m_meshModelFaces[static_cast<std::size_t>(faceIndex)];
    if (face.deleted || std::clamp(face.vertexCount, 3, 4) != 4)
    {
        return;
    }

    const int i0 = face.indices[0];
    const int i1 = face.indices[1];
    const int i2 = face.indices[2];
    const int i3 = face.indices[3];
    if (i0 < 0 || i1 < 0 || i2 < 0 || i3 < 0 ||
        i0 >= static_cast<int>(m_meshModelVertices.size()) ||
        i1 >= static_cast<int>(m_meshModelVertices.size()) ||
        i2 >= static_cast<int>(m_meshModelVertices.size()) ||
        i3 >= static_cast<int>(m_meshModelVertices.size()))
    {
        return;
    }

    const glm::vec3 v0 = m_meshModelVertices[static_cast<std::size_t>(i0)].position;
    const glm::vec3 v1 = m_meshModelVertices[static_cast<std::size_t>(i1)].position;
    const glm::vec3 v2 = m_meshModelVertices[static_cast<std::size_t>(i2)].position;
    const glm::vec3 normal = glm::normalize(glm::cross(v1 - v0, v2 - v0));
    if (!std::isfinite(normal.x) || !std::isfinite(normal.y) || !std::isfinite(normal.z))
    {
        return;
    }

    const auto addVertex = [this](const glm::vec3& p) {
        m_meshModelVertices.push_back(MeshModelVertex{p, false});
        return static_cast<int>(m_meshModelVertices.size()) - 1;
    };

    const glm::vec3 offset = normal * distance;
    const int e0 = addVertex(m_meshModelVertices[static_cast<std::size_t>(i0)].position + offset);
    const int e1 = addVertex(m_meshModelVertices[static_cast<std::size_t>(i1)].position + offset);
    const int e2 = addVertex(m_meshModelVertices[static_cast<std::size_t>(i2)].position + offset);
    const int e3 = addVertex(m_meshModelVertices[static_cast<std::size_t>(i3)].position + offset);

    m_meshModelFaces.push_back(MeshModelFace{{e0, e1, e2, e3}, false});
    m_meshModelFaces.push_back(MeshModelFace{{i0, i1, e1, e0}, false});
    m_meshModelFaces.push_back(MeshModelFace{{i1, i2, e2, e1}, false});
    m_meshModelFaces.push_back(MeshModelFace{{i2, i3, e3, e2}, false});
    m_meshModelFaces.push_back(MeshModelFace{{i3, i0, e0, e3}, false});
    m_meshModelSelectedFace = static_cast<int>(m_meshModelFaces.size()) - 5;
    m_meshModelFaceSelection.clear();
    m_meshModelFaceSelection.push_back(m_meshModelSelectedFace);
}

std::vector<std::array<int, 2>> LevelEditor::BuildMeshModelEdges() const
{
    std::vector<std::array<int, 2>> edges;
    std::unordered_set<std::uint64_t> seen;
    edges.reserve(m_meshModelFaces.size() * 2U);

    auto edgeKey = [](int a, int b) {
        const std::uint32_t ua = static_cast<std::uint32_t>(std::min(a, b));
        const std::uint32_t ub = static_cast<std::uint32_t>(std::max(a, b));
        return (static_cast<std::uint64_t>(ua) << 32U) | static_cast<std::uint64_t>(ub);
    };

    for (const MeshModelFace& face : m_meshModelFaces)
    {
        if (face.deleted)
        {
            continue;
        }
        const int count = std::clamp(face.vertexCount, 3, 4);
        for (int i = 0; i < count; ++i)
        {
            const int a = face.indices[static_cast<std::size_t>(i)];
            const int b = face.indices[static_cast<std::size_t>((i + 1) % count)];
            if (a < 0 || b < 0 || a == b ||
                a >= static_cast<int>(m_meshModelVertices.size()) ||
                b >= static_cast<int>(m_meshModelVertices.size()))
            {
                continue;
            }
            if (m_meshModelVertices[static_cast<std::size_t>(a)].deleted ||
                m_meshModelVertices[static_cast<std::size_t>(b)].deleted)
            {
                continue;
            }
            const std::uint64_t key = edgeKey(a, b);
            if (seen.insert(key).second)
            {
                edges.push_back(std::array<int, 2>{a, b});
            }
        }
    }
    return edges;
}

std::vector<int> LevelEditor::CollectMeshModelActiveEdges() const
{
    const std::vector<std::array<int, 2>> edges = BuildMeshModelEdges();
    const int edgeCount = static_cast<int>(edges.size());
    std::vector<int> activeEdges;
    if (!m_meshModelLoopSelectionEdges.empty())
    {
        activeEdges = m_meshModelLoopSelectionEdges;
    }
    else if (!m_meshModelRingSelectionEdges.empty())
    {
        activeEdges = m_meshModelRingSelectionEdges;
    }
    else if (m_meshModelSelectedEdge >= 0)
    {
        activeEdges.push_back(m_meshModelSelectedEdge);
    }

    std::sort(activeEdges.begin(), activeEdges.end());
    activeEdges.erase(std::unique(activeEdges.begin(), activeEdges.end()), activeEdges.end());
    activeEdges.erase(
        std::remove_if(
            activeEdges.begin(),
            activeEdges.end(),
            [edgeCount](int edgeIndex) { return edgeIndex < 0 || edgeIndex >= edgeCount; }),
        activeEdges.end());
    return activeEdges;
}

void LevelEditor::MeshModelerExtrudeActiveEdges(float distance)
{
    const std::vector<int> activeEdgeIndices = CollectMeshModelActiveEdges();
    if (activeEdgeIndices.empty())
    {
        return;
    }

    const std::vector<std::array<int, 2>> initialEdges = BuildMeshModelEdges();
    std::vector<std::array<int, 2>> activePairs;
    activePairs.reserve(activeEdgeIndices.size());
    std::unordered_set<std::uint64_t> seenPairs;
    auto pairKey = [](int a, int b) {
        const std::uint32_t ua = static_cast<std::uint32_t>(std::min(a, b));
        const std::uint32_t ub = static_cast<std::uint32_t>(std::max(a, b));
        return (static_cast<std::uint64_t>(ua) << 32U) | static_cast<std::uint64_t>(ub);
    };

    for (const int edgeIndex : activeEdgeIndices)
    {
        const auto edge = initialEdges[static_cast<std::size_t>(edgeIndex)];
        const std::uint64_t key = pairKey(edge[0], edge[1]);
        if (!seenPairs.insert(key).second)
        {
            continue;
        }
        activePairs.push_back(std::array<int, 2>{std::min(edge[0], edge[1]), std::max(edge[0], edge[1])});
    }
    if (activePairs.empty())
    {
        return;
    }

    const std::size_t undoBefore = m_undoStack.size();
    PushHistorySnapshot();

    int appliedCount = 0;
    for (const auto& targetPair : activePairs)
    {
        const std::vector<std::array<int, 2>> edgesNow = BuildMeshModelEdges();
        int edgeToApply = -1;
        for (int edgeIdx = 0; edgeIdx < static_cast<int>(edgesNow.size()); ++edgeIdx)
        {
            const auto edgeNow = edgesNow[static_cast<std::size_t>(edgeIdx)];
            const int minNow = std::min(edgeNow[0], edgeNow[1]);
            const int maxNow = std::max(edgeNow[0], edgeNow[1]);
            if (minNow == targetPair[0] && maxNow == targetPair[1])
            {
                edgeToApply = edgeIdx;
                break;
            }
        }
        if (edgeToApply < 0)
        {
            continue;
        }
        MeshModelerExtrudeEdge(edgeToApply, distance);
        ++appliedCount;
    }

    if (appliedCount <= 0)
    {
        if (m_undoStack.size() > undoBefore)
        {
            m_undoStack.pop_back();
        }
        return;
    }
    m_statusLine = "Extruded edges: " + std::to_string(appliedCount);
}

void LevelEditor::MeshModelerBevelActiveEdges(float distance, int segments)
{
    const std::vector<int> activeEdgeIndices = CollectMeshModelActiveEdges();
    if (activeEdgeIndices.empty())
    {
        return;
    }

    const std::vector<std::array<int, 2>> initialEdges = BuildMeshModelEdges();
    std::vector<std::array<int, 2>> activePairs;
    activePairs.reserve(activeEdgeIndices.size());
    std::unordered_set<std::uint64_t> seenPairs;
    auto pairKey = [](int a, int b) {
        const std::uint32_t ua = static_cast<std::uint32_t>(std::min(a, b));
        const std::uint32_t ub = static_cast<std::uint32_t>(std::max(a, b));
        return (static_cast<std::uint64_t>(ua) << 32U) | static_cast<std::uint64_t>(ub);
    };

    for (const int edgeIndex : activeEdgeIndices)
    {
        const auto edge = initialEdges[static_cast<std::size_t>(edgeIndex)];
        const std::uint64_t key = pairKey(edge[0], edge[1]);
        if (!seenPairs.insert(key).second)
        {
            continue;
        }
        activePairs.push_back(std::array<int, 2>{std::min(edge[0], edge[1]), std::max(edge[0], edge[1])});
    }
    if (activePairs.empty())
    {
        return;
    }

    const std::size_t undoBefore = m_undoStack.size();
    PushHistorySnapshot();

    int appliedCount = 0;
    for (const auto& targetPair : activePairs)
    {
        const std::vector<std::array<int, 2>> edgesNow = BuildMeshModelEdges();
        int edgeToApply = -1;
        for (int edgeIdx = 0; edgeIdx < static_cast<int>(edgesNow.size()); ++edgeIdx)
        {
            const auto edgeNow = edgesNow[static_cast<std::size_t>(edgeIdx)];
            const int minNow = std::min(edgeNow[0], edgeNow[1]);
            const int maxNow = std::max(edgeNow[0], edgeNow[1]);
            if (minNow == targetPair[0] && maxNow == targetPair[1])
            {
                edgeToApply = edgeIdx;
                break;
            }
        }
        if (edgeToApply < 0)
        {
            continue;
        }
        MeshModelerBevelEdge(edgeToApply, distance, segments);
        ++appliedCount;
    }

    if (appliedCount <= 0)
    {
        if (m_undoStack.size() > undoBefore)
        {
            m_undoStack.pop_back();
        }
        return;
    }
    m_statusLine = "Beveled edges: " + std::to_string(appliedCount);
}

void LevelEditor::MeshModelerExtrudeEdge(int edgeIndex, float distance)
{
    const std::vector<std::array<int, 2>> edges = BuildMeshModelEdges();
    if (edgeIndex < 0 || edgeIndex >= static_cast<int>(edges.size()))
    {
        return;
    }

    const int i0 = edges[static_cast<std::size_t>(edgeIndex)][0];
    const int i1 = edges[static_cast<std::size_t>(edgeIndex)][1];
    if (i0 < 0 || i1 < 0 || i0 >= static_cast<int>(m_meshModelVertices.size()) || i1 >= static_cast<int>(m_meshModelVertices.size()))
    {
        return;
    }

    glm::vec3 averageNormal{0.0F};
    int adjacentFaces = 0;
    for (const MeshModelFace& face : m_meshModelFaces)
    {
        if (face.deleted)
        {
            continue;
        }
        bool hasI0 = false;
        bool hasI1 = false;
        const int count = std::clamp(face.vertexCount, 3, 4);
        for (int i = 0; i < count; ++i)
        {
            const int idx = face.indices[static_cast<std::size_t>(i)];
            hasI0 = hasI0 || idx == i0;
            hasI1 = hasI1 || idx == i1;
        }
        if (!hasI0 || !hasI1)
        {
            continue;
        }

        const glm::vec3 p0 = m_meshModelVertices[static_cast<std::size_t>(face.indices[0])].position;
        const glm::vec3 p1 = m_meshModelVertices[static_cast<std::size_t>(face.indices[1])].position;
        const glm::vec3 p2 = m_meshModelVertices[static_cast<std::size_t>(face.indices[2])].position;
        glm::vec3 n = glm::cross(p1 - p0, p2 - p0);
        if (glm::length(n) > 1.0e-6F)
        {
            averageNormal += glm::normalize(n);
            ++adjacentFaces;
        }
    }

    if (adjacentFaces == 0 || glm::length(averageNormal) < 1.0e-6F)
    {
        averageNormal = glm::vec3{0.0F, 1.0F, 0.0F};
    }
    else
    {
        averageNormal = glm::normalize(averageNormal);
    }

    const auto addVertex = [this](const glm::vec3& p) {
        m_meshModelVertices.push_back(MeshModelVertex{p, false});
        return static_cast<int>(m_meshModelVertices.size()) - 1;
    };

    const glm::vec3 offset = averageNormal * distance;
    const int e0 = addVertex(m_meshModelVertices[static_cast<std::size_t>(i0)].position + offset);
    const int e1 = addVertex(m_meshModelVertices[static_cast<std::size_t>(i1)].position + offset);
    m_meshModelFaces.push_back(MeshModelFace{{i0, i1, e1, e0}, false});

    const std::vector<std::array<int, 2>> afterEdges = BuildMeshModelEdges();
    m_meshModelSelectedEdge = -1;
    for (int i = 0; i < static_cast<int>(afterEdges.size()); ++i)
    {
        const auto& edge = afterEdges[static_cast<std::size_t>(i)];
        if ((edge[0] == e0 && edge[1] == e1) || (edge[0] == e1 && edge[1] == e0))
        {
            m_meshModelSelectedEdge = i;
            break;
        }
    }
}

void LevelEditor::MeshModelerBevelEdge(int edgeIndex, float distance, int segments)
{
    const std::vector<std::array<int, 2>> edges = BuildMeshModelEdges();
    if (edgeIndex < 0 || edgeIndex >= static_cast<int>(edges.size()))
    {
        return;
    }
    const int i0 = edges[static_cast<std::size_t>(edgeIndex)][0];
    const int i1 = edges[static_cast<std::size_t>(edgeIndex)][1];
    if (i0 < 0 || i1 < 0 ||
        i0 >= static_cast<int>(m_meshModelVertices.size()) ||
        i1 >= static_cast<int>(m_meshModelVertices.size()) ||
        m_meshModelVertices[static_cast<std::size_t>(i0)].deleted ||
        m_meshModelVertices[static_cast<std::size_t>(i1)].deleted)
    {
        return;
    }

    if (segments < 2)
    {
        segments = 2;
    }
    if (segments <= 0)
    {
        segments = 1;
    }
    const float totalDistance = std::max(0.001F, distance);
    const float profile = glm::clamp(m_meshModelBevelProfile, 0.2F, 4.0F);
    const glm::vec3 p0 = m_meshModelVertices[static_cast<std::size_t>(i0)].position;
    const glm::vec3 p1 = m_meshModelVertices[static_cast<std::size_t>(i1)].position;
    glm::vec3 edgeDir = p1 - p0;
    if (glm::length(edgeDir) < 1.0e-6F)
    {
        return;
    }
    edgeDir = glm::normalize(edgeDir);

    struct AdjacentFaceInfo
    {
        int faceIndex = -1;
        int localEdge = -1;
        int count = 4;
        int a = -1;
        int b = -1;
        int i0Neighbor = -1;
        int i1Neighbor = -1;
        glm::vec3 i0Dir{0.0F};
        glm::vec3 i1Dir{0.0F};
        glm::vec3 faceNormal{0.0F, 1.0F, 0.0F};
    };

    std::vector<AdjacentFaceInfo> adjacentFaces;
    adjacentFaces.reserve(4U);
    auto safeNormalize = [](const glm::vec3& value, const glm::vec3& fallback) {
        const float len = glm::length(value);
        if (len < 1.0e-6F)
        {
            const float fallbackLen = glm::length(fallback);
            if (fallbackLen < 1.0e-6F)
            {
                return glm::vec3{0.0F, 1.0F, 0.0F};
            }
            return fallback / fallbackLen;
        }
        return value / len;
    };

    for (int faceIndex = 0; faceIndex < static_cast<int>(m_meshModelFaces.size()); ++faceIndex)
    {
        const MeshModelFace& face = m_meshModelFaces[static_cast<std::size_t>(faceIndex)];
        if (face.deleted)
        {
            continue;
        }
        const int count = std::clamp(face.vertexCount, 3, 4);
        for (int local = 0; local < count; ++local)
        {
            const int a = face.indices[static_cast<std::size_t>(local)];
            const int b = face.indices[static_cast<std::size_t>((local + 1) % count)];
            if (!((a == i0 && b == i1) || (a == i1 && b == i0)))
            {
                continue;
            }
            const int prev = face.indices[static_cast<std::size_t>((local - 1 + count) % count)];
            const int next = face.indices[static_cast<std::size_t>((local + 2) % count)];
            const int i0Neighbor = (a == i0) ? prev : next;
            const int i1Neighbor = (a == i0) ? next : prev;
            if (i0Neighbor < 0 || i1Neighbor < 0 ||
                i0Neighbor >= static_cast<int>(m_meshModelVertices.size()) ||
                i1Neighbor >= static_cast<int>(m_meshModelVertices.size()))
            {
                continue;
            }

            const glm::vec3 faceP0 = m_meshModelVertices[static_cast<std::size_t>(face.indices[0])].position;
            const glm::vec3 faceP1 = m_meshModelVertices[static_cast<std::size_t>(face.indices[1])].position;
            const glm::vec3 faceP2 = m_meshModelVertices[static_cast<std::size_t>(face.indices[2])].position;
            glm::vec3 faceNormal = glm::cross(faceP1 - faceP0, faceP2 - faceP0);
            if (glm::length(faceNormal) < 1.0e-6F)
            {
                faceNormal = glm::vec3{0.0F, 1.0F, 0.0F};
            }
            else
            {
                faceNormal = glm::normalize(faceNormal);
            }

            const glm::vec3 posI0 = m_meshModelVertices[static_cast<std::size_t>(i0)].position;
            const glm::vec3 posI1 = m_meshModelVertices[static_cast<std::size_t>(i1)].position;
            const glm::vec3 posN0 = m_meshModelVertices[static_cast<std::size_t>(i0Neighbor)].position;
            const glm::vec3 posN1 = m_meshModelVertices[static_cast<std::size_t>(i1Neighbor)].position;

            glm::vec3 dir0 = safeNormalize(posN0 - posI0, glm::cross(faceNormal, edgeDir));
            glm::vec3 dir1 = safeNormalize(posN1 - posI1, glm::cross(faceNormal, edgeDir));
            if (glm::abs(glm::dot(dir0, edgeDir)) > 0.97F)
            {
                dir0 = safeNormalize(glm::cross(faceNormal, edgeDir), -edgeDir);
            }
            if (glm::abs(glm::dot(dir1, edgeDir)) > 0.97F)
            {
                dir1 = safeNormalize(glm::cross(faceNormal, edgeDir), edgeDir);
            }

            adjacentFaces.push_back(AdjacentFaceInfo{
                faceIndex,
                local,
                count,
                a,
                b,
                i0Neighbor,
                i1Neighbor,
                dir0,
                dir1,
                faceNormal
            });
            break;
        }
    }
    if (adjacentFaces.empty())
    {
        return;
    }
    if (adjacentFaces.size() > 2U)
    {
        float bestDot = 1.0F;
        std::size_t bestA = 0;
        std::size_t bestB = 1;
        for (std::size_t a = 0; a + 1U < adjacentFaces.size(); ++a)
        {
            for (std::size_t b = a + 1U; b < adjacentFaces.size(); ++b)
            {
                const float d = glm::dot(adjacentFaces[a].faceNormal, adjacentFaces[b].faceNormal);
                if (d < bestDot)
                {
                    bestDot = d;
                    bestA = a;
                    bestB = b;
                }
            }
        }
        std::vector<AdjacentFaceInfo> reduced;
        reduced.reserve(2U);
        reduced.push_back(adjacentFaces[bestA]);
        reduced.push_back(adjacentFaces[bestB]);
        adjacentFaces = std::move(reduced);
    }
    if (adjacentFaces.size() < 2U)
    {
        // Boundary/non-manifold fallback: edge extrude is more predictable than broken bevel topology.
        MeshModelerExtrudeEdge(edgeIndex, totalDistance);
        return;
    }

    const auto addVertex = [this](const glm::vec3& position) {
        m_meshModelVertices.push_back(MeshModelVertex{position, false});
        return static_cast<int>(m_meshModelVertices.size()) - 1;
    };

    const AdjacentFaceInfo& sideA = adjacentFaces[0];
    const AdjacentFaceInfo& sideB = adjacentFaces[1];
    const float miterScale = m_meshModelBevelUseMiter ? glm::clamp(1.0F / glm::sin(glm::radians(45.0F)), 1.0F, 2.0F) : 1.0F;
    std::vector<std::array<int, 2>> rings;
    rings.reserve(static_cast<std::size_t>(segments) + 1U);
    for (int step = 0; step <= segments; ++step)
    {
        const float t = static_cast<float>(step) / static_cast<float>(segments);
        const float shapedT = glm::clamp(std::pow(t, profile), 0.0F, 1.0F);
        glm::vec3 d0 = glm::mix(sideA.i0Dir, sideB.i0Dir, shapedT);
        glm::vec3 d1 = glm::mix(sideA.i1Dir, sideB.i1Dir, shapedT);
        d0 = safeNormalize(d0, sideA.i0Dir);
        d1 = safeNormalize(d1, sideA.i1Dir);
        const int ring0 = addVertex(p0 + d0 * totalDistance * miterScale);
        const int ring1 = addVertex(p1 + d1 * totalDistance * miterScale);
        rings.push_back(std::array<int, 2>{ring0, ring1});
    }

    // Replace original faces that used the beveled edge with trimmed versions that use first/last bevel rings.
    const auto emitTrimmedFace = [this, i0, i1](const AdjacentFaceInfo& adjacent, const std::array<int, 2>& ring) {
        MeshModelFace oldFace = m_meshModelFaces[static_cast<std::size_t>(adjacent.faceIndex)];
        m_meshModelFaces[static_cast<std::size_t>(adjacent.faceIndex)].deleted = true;

        const int replacementA = (adjacent.a == i0) ? ring[0] : ring[1];
        const int replacementB = (adjacent.b == i0) ? ring[0] : ring[1];
        oldFace.indices[static_cast<std::size_t>(adjacent.localEdge)] = replacementA;
        oldFace.indices[static_cast<std::size_t>((adjacent.localEdge + 1) % adjacent.count)] = replacementB;
        oldFace.deleted = false;
        oldFace.vertexCount = adjacent.count;
        m_meshModelFaces.push_back(oldFace);
    };
    emitTrimmedFace(sideA, rings.front());
    emitTrimmedFace(sideB, rings.back());

    for (std::size_t ringIndex = 0; ringIndex + 1U < rings.size(); ++ringIndex)
    {
        const auto& a = rings[ringIndex];
        const auto& b = rings[ringIndex + 1U];
        if (a[0] < 0 || a[1] < 0 || b[0] < 0 || b[1] < 0)
        {
            continue;
        }
        m_meshModelFaces.push_back(MeshModelFace{{a[0], a[1], b[1], b[0]}, false, 4});
    }

    CleanupMeshModelTopology();
    const std::vector<std::array<int, 2>> postEdges = BuildMeshModelEdges();
    m_meshModelSelectedEdge = -1;
    const std::array<int, 2> highlightPair = rings.size() > 1U ? rings[1U] : rings.back();
    for (int edgeIdx = 0; edgeIdx < static_cast<int>(postEdges.size()); ++edgeIdx)
    {
        const auto& edge = postEdges[static_cast<std::size_t>(edgeIdx)];
        if ((edge[0] == highlightPair[0] && edge[1] == highlightPair[1]) ||
            (edge[0] == highlightPair[1] && edge[1] == highlightPair[0]))
        {
            m_meshModelSelectedEdge = edgeIdx;
            break;
        }
    }
    m_meshModelFaceSelection.clear();
    if (!m_meshModelFaces.empty())
    {
        m_meshModelSelectedFace = static_cast<int>(m_meshModelFaces.size()) - 1;
        m_meshModelFaceSelection.push_back(m_meshModelSelectedFace);
    }
}

void LevelEditor::MeshModelerLoopCutEdge(int edgeIndex, float ratio)
{
    const std::vector<std::array<int, 2>> edges = BuildMeshModelEdges();
    if (edgeIndex < 0 || edgeIndex >= static_cast<int>(edges.size()))
    {
        return;
    }
    const int selectedA = edges[static_cast<std::size_t>(edgeIndex)][0];
    const int selectedB = edges[static_cast<std::size_t>(edgeIndex)][1];
    const float cutRatio = glm::clamp(ratio, 0.05F, 0.95F);

    auto edgeKey = [](int a, int b) {
        const std::uint32_t ua = static_cast<std::uint32_t>(std::min(a, b));
        const std::uint32_t ub = static_cast<std::uint32_t>(std::max(a, b));
        return (static_cast<std::uint64_t>(ua) << 32U) | static_cast<std::uint64_t>(ub);
    };
    auto findFaceEdge = [&](const MeshModelFace& face, int a, int b) -> int {
        const int count = std::clamp(face.vertexCount, 3, 4);
        if (face.deleted || count != 4)
        {
            return -1;
        }
        for (int i = 0; i < count; ++i)
        {
            const int e0 = face.indices[static_cast<std::size_t>(i)];
            const int e1 = face.indices[static_cast<std::size_t>((i + 1) % count)];
            if ((e0 == a && e1 == b) || (e0 == b && e1 == a))
            {
                return i;
            }
        }
        return -1;
    };

    std::unordered_map<std::uint64_t, std::vector<int>> edgeToFaces;
    edgeToFaces.reserve(m_meshModelFaces.size() * 2U);
    for (int faceIndex = 0; faceIndex < static_cast<int>(m_meshModelFaces.size()); ++faceIndex)
    {
        const MeshModelFace& face = m_meshModelFaces[static_cast<std::size_t>(faceIndex)];
        const int count = std::clamp(face.vertexCount, 3, 4);
        if (face.deleted || count != 4)
        {
            continue;
        }
        for (int i = 0; i < count; ++i)
        {
            const int a = face.indices[static_cast<std::size_t>(i)];
            const int b = face.indices[static_cast<std::size_t>((i + 1) % count)];
            if (a < 0 || b < 0)
            {
                continue;
            }
            edgeToFaces[edgeKey(a, b)].push_back(faceIndex);
        }
    }

    struct LoopCutFaceOp
    {
        int faceIndex = -1;
        int splitEdgeIndex = -1;
    };
    std::vector<LoopCutFaceOp> ops;
    std::unordered_set<int> visitedFaces;

    auto walkLoopDirection = [&](int startFace, int startA, int startB) {
        int currentFace = startFace;
        int currentA = startA;
        int currentB = startB;
        for (int guard = 0; guard < static_cast<int>(m_meshModelFaces.size()) + 8; ++guard)
        {
            if (currentFace < 0 || currentFace >= static_cast<int>(m_meshModelFaces.size()))
            {
                break;
            }
            const MeshModelFace& face = m_meshModelFaces[static_cast<std::size_t>(currentFace)];
            const int edgeInFace = findFaceEdge(face, currentA, currentB);
            if (edgeInFace < 0)
            {
                break;
            }
            if (!visitedFaces.insert(currentFace).second)
            {
                break;
            }
            ops.push_back(LoopCutFaceOp{currentFace, edgeInFace});

            const int oppositeA = face.indices[static_cast<std::size_t>((edgeInFace + 2) % 4)];
            const int oppositeB = face.indices[static_cast<std::size_t>((edgeInFace + 3) % 4)];
            const auto adjacentIt = edgeToFaces.find(edgeKey(oppositeA, oppositeB));
            if (adjacentIt == edgeToFaces.end())
            {
                break;
            }

            int nextFace = -1;
            for (const int candidate : adjacentIt->second)
            {
                if (candidate != currentFace && !visitedFaces.contains(candidate))
                {
                    nextFace = candidate;
                    break;
                }
            }
            if (nextFace < 0)
            {
                break;
            }
            currentFace = nextFace;
            currentA = oppositeA;
            currentB = oppositeB;
        }
    };

    for (int faceIndex = 0; faceIndex < static_cast<int>(m_meshModelFaces.size()); ++faceIndex)
    {
        const MeshModelFace& face = m_meshModelFaces[static_cast<std::size_t>(faceIndex)];
        if (findFaceEdge(face, selectedA, selectedB) >= 0)
        {
            walkLoopDirection(faceIndex, selectedA, selectedB);
        }
    }
    if (ops.empty())
    {
        return;
    }

    auto addVertex = [this](const glm::vec3& p) {
        m_meshModelVertices.push_back(MeshModelVertex{p, false});
        return static_cast<int>(m_meshModelVertices.size()) - 1;
    };
    auto vertexOnEdge = [&](int edgeStartIndex, int edgeEndIndex, float t) {
        if (t <= 1.0e-4F)
        {
            return edgeStartIndex;
        }
        if (t >= 1.0F - 1.0e-4F)
        {
            return edgeEndIndex;
        }
        const glm::vec3 p0 = m_meshModelVertices[static_cast<std::size_t>(edgeStartIndex)].position;
        const glm::vec3 p1 = m_meshModelVertices[static_cast<std::size_t>(edgeEndIndex)].position;
        return addVertex(glm::mix(p0, p1, t));
    };

    for (const LoopCutFaceOp& op : ops)
    {
        if (op.faceIndex < 0 || op.faceIndex >= static_cast<int>(m_meshModelFaces.size()) ||
            op.splitEdgeIndex < 0 || op.splitEdgeIndex >= 4)
        {
            continue;
        }
        MeshModelFace& face = m_meshModelFaces[static_cast<std::size_t>(op.faceIndex)];
        if (face.deleted)
        {
            continue;
        }

        const int k = op.splitEdgeIndex;
        const int i0 = face.indices[static_cast<std::size_t>(k)];
        const int i1 = face.indices[static_cast<std::size_t>((k + 1) % 4)];
        const int i2 = face.indices[static_cast<std::size_t>((k + 2) % 4)];
        const int i3 = face.indices[static_cast<std::size_t>((k + 3) % 4)];
        if (i0 < 0 || i1 < 0 || i2 < 0 || i3 < 0 ||
            i0 >= static_cast<int>(m_meshModelVertices.size()) ||
            i1 >= static_cast<int>(m_meshModelVertices.size()) ||
            i2 >= static_cast<int>(m_meshModelVertices.size()) ||
            i3 >= static_cast<int>(m_meshModelVertices.size()))
        {
            continue;
        }

        const int cutA = vertexOnEdge(i0, i1, cutRatio);
        const int cutB = vertexOnEdge(i2, i3, cutRatio);
        face.deleted = true;
        m_meshModelFaces.push_back(MeshModelFace{{i0, cutA, cutB, i3}, false});
        m_meshModelFaces.push_back(MeshModelFace{{cutA, i1, i2, cutB}, false});
    }

    CleanupMeshModelTopology();
    m_meshModelSelectedFace = static_cast<int>(m_meshModelFaces.size()) - 1;
}

void LevelEditor::MeshModelerSelectEdgeLoop(int edgeIndex)
{
    m_meshModelLoopSelectionEdges.clear();
    m_meshModelRingSelectionEdges.clear();

    const std::vector<std::array<int, 2>> edges = BuildMeshModelEdges();
    if (edgeIndex < 0 || edgeIndex >= static_cast<int>(edges.size()))
    {
        return;
    }

    auto edgeKey = [](int a, int b) {
        const std::uint32_t ua = static_cast<std::uint32_t>(std::min(a, b));
        const std::uint32_t ub = static_cast<std::uint32_t>(std::max(a, b));
        return (static_cast<std::uint64_t>(ua) << 32U) | static_cast<std::uint64_t>(ub);
    };

    std::unordered_map<std::uint64_t, int> edgeKeyToIndex;
    edgeKeyToIndex.reserve(edges.size());
    for (int i = 0; i < static_cast<int>(edges.size()); ++i)
    {
        edgeKeyToIndex[edgeKey(edges[static_cast<std::size_t>(i)][0], edges[static_cast<std::size_t>(i)][1])] = i;
    }

    struct EdgeOccurrence
    {
        int faceIndex = -1;
        int localEdge = -1;
    };
    std::unordered_map<std::uint64_t, std::vector<EdgeOccurrence>> occurrences;
    occurrences.reserve(m_meshModelFaces.size() * 2U);
    for (int faceIndex = 0; faceIndex < static_cast<int>(m_meshModelFaces.size()); ++faceIndex)
    {
        const MeshModelFace& face = m_meshModelFaces[static_cast<std::size_t>(faceIndex)];
        if (face.deleted || std::clamp(face.vertexCount, 3, 4) != 4)
        {
            continue;
        }
        for (int local = 0; local < 4; ++local)
        {
            const int a = face.indices[static_cast<std::size_t>(local)];
            const int b = face.indices[static_cast<std::size_t>((local + 1) % 4)];
            occurrences[edgeKey(a, b)].push_back(EdgeOccurrence{faceIndex, local});
        }
    }

    std::unordered_set<std::uint64_t> visited;
    std::vector<std::uint64_t> queue;
    const std::uint64_t startKey =
        edgeKey(edges[static_cast<std::size_t>(edgeIndex)][0], edges[static_cast<std::size_t>(edgeIndex)][1]);
    queue.push_back(startKey);
    visited.insert(startKey);

    std::size_t qi = 0;
    while (qi < queue.size())
    {
        const std::uint64_t currentKey = queue[qi++];
        const auto it = edgeKeyToIndex.find(currentKey);
        if (it != edgeKeyToIndex.end())
        {
            m_meshModelLoopSelectionEdges.push_back(it->second);
        }

        const auto occIt = occurrences.find(currentKey);
        if (occIt == occurrences.end())
        {
            continue;
        }
        for (const EdgeOccurrence& occ : occIt->second)
        {
            if (occ.faceIndex < 0 || occ.faceIndex >= static_cast<int>(m_meshModelFaces.size()))
            {
                continue;
            }
            const MeshModelFace& face = m_meshModelFaces[static_cast<std::size_t>(occ.faceIndex)];
            const int oppositeLocal = (occ.localEdge + 2) % 4;
            const int oa = face.indices[static_cast<std::size_t>(oppositeLocal)];
            const int ob = face.indices[static_cast<std::size_t>((oppositeLocal + 1) % 4)];
            const std::uint64_t oppositeKey = edgeKey(oa, ob);
            if (visited.insert(oppositeKey).second)
            {
                queue.push_back(oppositeKey);
            }
        }
    }
}

void LevelEditor::MeshModelerSelectEdgeRing(int edgeIndex)
{
    m_meshModelRingSelectionEdges.clear();
    m_meshModelLoopSelectionEdges.clear();

    const std::vector<std::array<int, 2>> edges = BuildMeshModelEdges();
    if (edgeIndex < 0 || edgeIndex >= static_cast<int>(edges.size()))
    {
        return;
    }

    auto edgeKey = [](int a, int b) {
        const std::uint32_t ua = static_cast<std::uint32_t>(std::min(a, b));
        const std::uint32_t ub = static_cast<std::uint32_t>(std::max(a, b));
        return (static_cast<std::uint64_t>(ua) << 32U) | static_cast<std::uint64_t>(ub);
    };
    auto stateKey = [](int faceIndex, int localEdge) {
        return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(faceIndex)) << 8U) |
               static_cast<std::uint64_t>(static_cast<std::uint8_t>(localEdge & 0xFF));
    };

    std::unordered_map<std::uint64_t, int> edgeKeyToIndex;
    edgeKeyToIndex.reserve(edges.size());
    for (int i = 0; i < static_cast<int>(edges.size()); ++i)
    {
        edgeKeyToIndex[edgeKey(edges[static_cast<std::size_t>(i)][0], edges[static_cast<std::size_t>(i)][1])] = i;
    }

    struct EdgeOccurrence
    {
        int faceIndex = -1;
        int localEdge = -1;
    };
    std::unordered_map<std::uint64_t, std::vector<EdgeOccurrence>> occurrences;
    occurrences.reserve(m_meshModelFaces.size() * 2U);
    for (int faceIndex = 0; faceIndex < static_cast<int>(m_meshModelFaces.size()); ++faceIndex)
    {
        const MeshModelFace& face = m_meshModelFaces[static_cast<std::size_t>(faceIndex)];
        if (face.deleted || std::clamp(face.vertexCount, 3, 4) != 4)
        {
            continue;
        }
        for (int local = 0; local < 4; ++local)
        {
            const int a = face.indices[static_cast<std::size_t>(local)];
            const int b = face.indices[static_cast<std::size_t>((local + 1) % 4)];
            occurrences[edgeKey(a, b)].push_back(EdgeOccurrence{faceIndex, local});
        }
    }

    const std::uint64_t startKey =
        edgeKey(edges[static_cast<std::size_t>(edgeIndex)][0], edges[static_cast<std::size_t>(edgeIndex)][1]);
    const auto startOccIt = occurrences.find(startKey);
    if (startOccIt == occurrences.end())
    {
        return;
    }

    std::unordered_set<std::uint64_t> visitedStates;
    std::unordered_set<std::uint64_t> ringEdgeKeys;
    std::vector<EdgeOccurrence> queue = startOccIt->second;
    for (const EdgeOccurrence& occ : queue)
    {
        visitedStates.insert(stateKey(occ.faceIndex, occ.localEdge));
    }

    std::size_t qi = 0;
    while (qi < queue.size())
    {
        const EdgeOccurrence current = queue[qi++];
        if (current.faceIndex < 0 || current.faceIndex >= static_cast<int>(m_meshModelFaces.size()))
        {
            continue;
        }
        const MeshModelFace& face = m_meshModelFaces[static_cast<std::size_t>(current.faceIndex)];
        if (face.deleted || std::clamp(face.vertexCount, 3, 4) != 4)
        {
            continue;
        }

        const int a = face.indices[static_cast<std::size_t>(current.localEdge)];
        const int b = face.indices[static_cast<std::size_t>((current.localEdge + 1) % 4)];
        const std::uint64_t currentEdgeKey = edgeKey(a, b);
        ringEdgeKeys.insert(currentEdgeKey);

        const int sideEdges[2] = {(current.localEdge + 1) % 4, (current.localEdge + 3) % 4};
        for (const int sideLocal : sideEdges)
        {
            const int sa = face.indices[static_cast<std::size_t>(sideLocal)];
            const int sb = face.indices[static_cast<std::size_t>((sideLocal + 1) % 4)];
            const std::uint64_t sideKey = edgeKey(sa, sb);
            const auto occIt = occurrences.find(sideKey);
            if (occIt == occurrences.end())
            {
                continue;
            }
            for (const EdgeOccurrence& nextOcc : occIt->second)
            {
                if (nextOcc.faceIndex == current.faceIndex)
                {
                    continue;
                }
                if (nextOcc.faceIndex < 0 || nextOcc.faceIndex >= static_cast<int>(m_meshModelFaces.size()))
                {
                    continue;
                }
                const MeshModelFace& nextFace = m_meshModelFaces[static_cast<std::size_t>(nextOcc.faceIndex)];
                if (nextFace.deleted || std::clamp(nextFace.vertexCount, 3, 4) != 4)
                {
                    continue;
                }
                const int nextLocal = (nextOcc.localEdge + 2) % 4;
                const std::uint64_t state = stateKey(nextOcc.faceIndex, nextLocal);
                if (visitedStates.insert(state).second)
                {
                    queue.push_back(EdgeOccurrence{nextOcc.faceIndex, nextLocal});
                }
            }
        }
    }

    for (const std::uint64_t key : ringEdgeKeys)
    {
        const auto it = edgeKeyToIndex.find(key);
        if (it != edgeKeyToIndex.end())
        {
            m_meshModelRingSelectionEdges.push_back(it->second);
        }
    }
}

void LevelEditor::MeshModelerMergeVertices(int keepVertexIndex, int removeVertexIndex)
{
    if (keepVertexIndex < 0 || removeVertexIndex < 0 ||
        keepVertexIndex >= static_cast<int>(m_meshModelVertices.size()) ||
        removeVertexIndex >= static_cast<int>(m_meshModelVertices.size()) ||
        keepVertexIndex == removeVertexIndex)
    {
        return;
    }
    if (m_meshModelVertices[static_cast<std::size_t>(keepVertexIndex)].deleted ||
        m_meshModelVertices[static_cast<std::size_t>(removeVertexIndex)].deleted)
    {
        return;
    }

    for (MeshModelFace& face : m_meshModelFaces)
    {
        if (face.deleted)
        {
            continue;
        }
        const int count = std::clamp(face.vertexCount, 3, 4);
        for (int i = 0; i < count; ++i)
        {
            int& idx = face.indices[static_cast<std::size_t>(i)];
            if (idx == removeVertexIndex)
            {
                idx = keepVertexIndex;
            }
        }
    }

    m_meshModelVertices[static_cast<std::size_t>(removeVertexIndex)].deleted = true;
    if (m_meshModelSelectedVertex == removeVertexIndex)
    {
        m_meshModelSelectedVertex = keepVertexIndex;
    }
    CleanupMeshModelTopology();
}

void LevelEditor::MeshModelerSplitSelectedVertex()
{
    if (m_meshModelSelectedVertex < 0 || m_meshModelSelectedVertex >= static_cast<int>(m_meshModelVertices.size()))
    {
        return;
    }
    if (m_meshModelSelectedFace < 0 || m_meshModelSelectedFace >= static_cast<int>(m_meshModelFaces.size()))
    {
        return;
    }
    MeshModelFace& face = m_meshModelFaces[static_cast<std::size_t>(m_meshModelSelectedFace)];
    if (face.deleted)
    {
        return;
    }

    int faceSlot = -1;
    const int count = std::clamp(face.vertexCount, 3, 4);
    for (int i = 0; i < count; ++i)
    {
        if (face.indices[static_cast<std::size_t>(i)] == m_meshModelSelectedVertex)
        {
            faceSlot = i;
            break;
        }
    }
    if (faceSlot < 0)
    {
        return;
    }

    const glm::vec3 position = m_meshModelVertices[static_cast<std::size_t>(m_meshModelSelectedVertex)].position;
    m_meshModelVertices.push_back(MeshModelVertex{position, false});
    const int duplicateIndex = static_cast<int>(m_meshModelVertices.size()) - 1;
    face.indices[static_cast<std::size_t>(faceSlot)] = duplicateIndex;
    m_meshModelSelectedVertex = duplicateIndex;
    CleanupMeshModelTopology();
}

void LevelEditor::MeshModelerDissolveSelectedEdge()
{
    const std::vector<std::array<int, 2>> edges = BuildMeshModelEdges();
    if (m_meshModelSelectedEdge < 0 || m_meshModelSelectedEdge >= static_cast<int>(edges.size()))
    {
        return;
    }
    const int i0 = edges[static_cast<std::size_t>(m_meshModelSelectedEdge)][0];
    const int i1 = edges[static_cast<std::size_t>(m_meshModelSelectedEdge)][1];
    if (i0 < 0 || i1 < 0 ||
        i0 >= static_cast<int>(m_meshModelVertices.size()) ||
        i1 >= static_cast<int>(m_meshModelVertices.size()))
    {
        return;
    }

    std::vector<int> adjacentFaces;
    adjacentFaces.reserve(4);
    for (int faceIndex = 0; faceIndex < static_cast<int>(m_meshModelFaces.size()); ++faceIndex)
    {
        const MeshModelFace& face = m_meshModelFaces[static_cast<std::size_t>(faceIndex)];
        if (face.deleted)
        {
            continue;
        }
        const int count = std::clamp(face.vertexCount, 3, 4);
        bool has0 = false;
        bool has1 = false;
        for (int i = 0; i < count; ++i)
        {
            const int idx = face.indices[static_cast<std::size_t>(i)];
            has0 = has0 || idx == i0;
            has1 = has1 || idx == i1;
        }
        if (has0 && has1)
        {
            adjacentFaces.push_back(faceIndex);
        }
    }
    if (adjacentFaces.size() < 2U)
    {
        m_statusLine = "Dissolve edge canceled: need 2 adjacent faces";
        return;
    }

    auto edgeKey = [](int a, int b) {
        const std::uint32_t ua = static_cast<std::uint32_t>(std::min(a, b));
        const std::uint32_t ub = static_cast<std::uint32_t>(std::max(a, b));
        return (static_cast<std::uint64_t>(ua) << 32U) | static_cast<std::uint64_t>(ub);
    };

    struct BoundaryEdge
    {
        int a = -1;
        int b = -1;
    };
    std::unordered_map<std::uint64_t, std::vector<BoundaryEdge>> edgeCounts;
    edgeCounts.reserve(16);
    for (std::size_t f = 0; f < 2U; ++f)
    {
        const MeshModelFace& face = m_meshModelFaces[static_cast<std::size_t>(adjacentFaces[f])];
        const int count = std::clamp(face.vertexCount, 3, 4);
        for (int i = 0; i < count; ++i)
        {
            const int a = face.indices[static_cast<std::size_t>(i)];
            const int b = face.indices[static_cast<std::size_t>((i + 1) % count)];
            edgeCounts[edgeKey(a, b)].push_back(BoundaryEdge{a, b});
        }
    }

    std::vector<BoundaryEdge> boundary;
    boundary.reserve(8);
    for (const auto& [_, list] : edgeCounts)
    {
        if (list.size() == 1U)
        {
            boundary.push_back(list.front());
        }
    }
    if (boundary.size() < 3U)
    {
        m_statusLine = "Dissolve edge canceled: invalid boundary";
        return;
    }

    std::unordered_map<int, std::vector<int>> graph;
    graph.reserve(boundary.size() * 2U);
    for (const BoundaryEdge& edge : boundary)
    {
        graph[edge.a].push_back(edge.b);
        graph[edge.b].push_back(edge.a);
    }

    int start = boundary.front().a;
    int previous = -1;
    int current = start;
    std::vector<int> loop;
    loop.reserve(boundary.size() + 1U);
    for (std::size_t guard = 0; guard < boundary.size() + 2U; ++guard)
    {
        loop.push_back(current);
        const auto it = graph.find(current);
        if (it == graph.end() || it->second.empty())
        {
            break;
        }
        int next = -1;
        for (const int candidate : it->second)
        {
            if (candidate != previous)
            {
                next = candidate;
                break;
            }
        }
        if (next < 0)
        {
            break;
        }
        previous = current;
        current = next;
        if (current == start)
        {
            break;
        }
    }
    if (loop.size() >= 2U && loop.back() == start)
    {
        loop.pop_back();
    }
    std::unordered_set<int> uniqueLoop(loop.begin(), loop.end());
    if (loop.size() < 3U || uniqueLoop.size() < 3U)
    {
        m_statusLine = "Dissolve edge canceled: failed loop reconstruction";
        return;
    }

    PushHistorySnapshot();
    m_meshModelFaces[static_cast<std::size_t>(adjacentFaces[0])].deleted = true;
    m_meshModelFaces[static_cast<std::size_t>(adjacentFaces[1])].deleted = true;
    for (std::size_t i = 1; i + 1 < loop.size(); ++i)
    {
        m_meshModelFaces.push_back(MeshModelFace{
            std::array<int, 4>{loop[0], loop[i], loop[i + 1], loop[i + 1]},
            false,
            3,
        });
    }
    CleanupMeshModelTopology();
    m_statusLine = "Dissolve edge reconstructed " + std::to_string(loop.size()) + "-gon";
}

void LevelEditor::MeshModelerBridgeEdges(int edgeIndexA, int edgeIndexB)
{
    const std::vector<std::array<int, 2>> edges = BuildMeshModelEdges();
    if (edgeIndexA < 0 || edgeIndexB < 0 ||
        edgeIndexA >= static_cast<int>(edges.size()) ||
        edgeIndexB >= static_cast<int>(edges.size()) ||
        edgeIndexA == edgeIndexB)
    {
        return;
    }

    int a0 = edges[static_cast<std::size_t>(edgeIndexA)][0];
    int a1 = edges[static_cast<std::size_t>(edgeIndexA)][1];
    int b0 = edges[static_cast<std::size_t>(edgeIndexB)][0];
    int b1 = edges[static_cast<std::size_t>(edgeIndexB)][1];
    if (a0 < 0 || a1 < 0 || b0 < 0 || b1 < 0 ||
        a0 >= static_cast<int>(m_meshModelVertices.size()) ||
        a1 >= static_cast<int>(m_meshModelVertices.size()) ||
        b0 >= static_cast<int>(m_meshModelVertices.size()) ||
        b1 >= static_cast<int>(m_meshModelVertices.size()))
    {
        return;
    }
    if (a0 == b0 || a0 == b1 || a1 == b0 || a1 == b1)
    {
        return;
    }

    const glm::vec3 pa0 = m_meshModelVertices[static_cast<std::size_t>(a0)].position;
    const glm::vec3 pa1 = m_meshModelVertices[static_cast<std::size_t>(a1)].position;
    const glm::vec3 pb0 = m_meshModelVertices[static_cast<std::size_t>(b0)].position;
    const glm::vec3 pb1 = m_meshModelVertices[static_cast<std::size_t>(b1)].position;

    const float sameOrder = glm::length(pa0 - pb0) + glm::length(pa1 - pb1);
    const float flippedOrder = glm::length(pa0 - pb1) + glm::length(pa1 - pb0);
    if (flippedOrder < sameOrder)
    {
        std::swap(b0, b1);
    }

    m_meshModelFaces.push_back(MeshModelFace{{a0, a1, b1, b0}, false});
    CleanupMeshModelTopology();
    m_meshModelSelectedFace = static_cast<int>(m_meshModelFaces.size()) - 1;
}

void LevelEditor::CleanupMeshModelTopology()
{
    const int oldSelectedFace = m_meshModelSelectedFace;
    const int oldSelectedVertex = m_meshModelSelectedVertex;
    const std::vector<int> oldSelectedFaces = m_meshModelFaceSelection;
    const std::vector<int> oldSelectedVertices = m_meshModelVertexSelection;
    const int oldMergeKeep = m_meshModelMergeKeepVertex;
    const int oldMergeRemove = m_meshModelMergeRemoveVertex;

    for (MeshModelFace& face : m_meshModelFaces)
    {
        if (face.deleted)
        {
            continue;
        }

        const int count = std::clamp(face.vertexCount, 3, 4);
        std::unordered_set<int> used;
        bool invalidFace = false;
        for (int i = 0; i < count; ++i)
        {
            const int idx = face.indices[static_cast<std::size_t>(i)];
            if (idx < 0 || idx >= static_cast<int>(m_meshModelVertices.size()) ||
                m_meshModelVertices[static_cast<std::size_t>(idx)].deleted)
            {
                invalidFace = true;
                break;
            }
            if (!used.insert(idx).second)
            {
                invalidFace = true;
                break;
            }
        }
        if (invalidFace)
        {
            face.deleted = true;
        }
    }

    std::unordered_set<int> usedVertices;
    usedVertices.reserve(m_meshModelFaces.size() * 4U);
    for (const MeshModelFace& face : m_meshModelFaces)
    {
        if (face.deleted)
        {
            continue;
        }
        const int count = std::clamp(face.vertexCount, 3, 4);
        for (int i = 0; i < count; ++i)
        {
            const int idx = face.indices[static_cast<std::size_t>(i)];
            if (idx >= 0 && idx < static_cast<int>(m_meshModelVertices.size()) &&
                !m_meshModelVertices[static_cast<std::size_t>(idx)].deleted)
            {
                usedVertices.insert(idx);
            }
        }
    }

    std::vector<int> vertexRemap(m_meshModelVertices.size(), -1);
    std::vector<MeshModelVertex> compactVertices;
    compactVertices.reserve(usedVertices.size());
    for (int oldIndex = 0; oldIndex < static_cast<int>(m_meshModelVertices.size()); ++oldIndex)
    {
        if (!usedVertices.contains(oldIndex))
        {
            continue;
        }
        const MeshModelVertex& vertex = m_meshModelVertices[static_cast<std::size_t>(oldIndex)];
        if (vertex.deleted)
        {
            continue;
        }
        vertexRemap[static_cast<std::size_t>(oldIndex)] = static_cast<int>(compactVertices.size());
        compactVertices.push_back(MeshModelVertex{vertex.position, false});
    }

    std::vector<int> faceRemap(m_meshModelFaces.size(), -1);
    std::vector<MeshModelFace> compactFaces;
    compactFaces.reserve(m_meshModelFaces.size());
    for (int oldFaceIndex = 0; oldFaceIndex < static_cast<int>(m_meshModelFaces.size()); ++oldFaceIndex)
    {
        const MeshModelFace& face = m_meshModelFaces[static_cast<std::size_t>(oldFaceIndex)];
        if (face.deleted)
        {
            continue;
        }

        MeshModelFace remappedFace = face;
        const int count = std::clamp(remappedFace.vertexCount, 3, 4);
        bool invalid = false;
        std::unordered_set<int> unique;
        for (int i = 0; i < count; ++i)
        {
            int& idx = remappedFace.indices[static_cast<std::size_t>(i)];
            if (idx < 0 || idx >= static_cast<int>(vertexRemap.size()))
            {
                invalid = true;
                break;
            }
            const int mapped = vertexRemap[static_cast<std::size_t>(idx)];
            if (mapped < 0)
            {
                invalid = true;
                break;
            }
            idx = mapped;
            unique.insert(mapped);
        }
        if (invalid || unique.size() < 3U)
        {
            continue;
        }
        remappedFace.vertexCount = count;
        remappedFace.deleted = false;
        faceRemap[static_cast<std::size_t>(oldFaceIndex)] = static_cast<int>(compactFaces.size());
        compactFaces.push_back(remappedFace);
    }

    m_meshModelVertices = std::move(compactVertices);
    m_meshModelFaces = std::move(compactFaces);

    auto remapVertexSelection = [&](int oldIndex) -> int {
        if (oldIndex < 0 || oldIndex >= static_cast<int>(vertexRemap.size()))
        {
            return -1;
        }
        return vertexRemap[static_cast<std::size_t>(oldIndex)];
    };

    m_meshModelSelectedVertex = remapVertexSelection(oldSelectedVertex);
    m_meshModelMergeKeepVertex = remapVertexSelection(oldMergeKeep);
    m_meshModelMergeRemoveVertex = remapVertexSelection(oldMergeRemove);
    m_meshModelVertexSelection.clear();
    for (const int oldIndex : oldSelectedVertices)
    {
        const int mapped = remapVertexSelection(oldIndex);
        if (mapped >= 0)
        {
            m_meshModelVertexSelection.push_back(mapped);
        }
    }
    std::sort(m_meshModelVertexSelection.begin(), m_meshModelVertexSelection.end());
    m_meshModelVertexSelection.erase(
        std::unique(m_meshModelVertexSelection.begin(), m_meshModelVertexSelection.end()),
        m_meshModelVertexSelection.end());

    if (oldSelectedFace >= 0 && oldSelectedFace < static_cast<int>(faceRemap.size()))
    {
        m_meshModelSelectedFace = faceRemap[static_cast<std::size_t>(oldSelectedFace)];
    }
    else
    {
        m_meshModelSelectedFace = -1;
    }
    m_meshModelFaceSelection.clear();
    for (const int oldFace : oldSelectedFaces)
    {
        if (oldFace < 0 || oldFace >= static_cast<int>(faceRemap.size()))
        {
            continue;
        }
        const int mapped = faceRemap[static_cast<std::size_t>(oldFace)];
        if (mapped >= 0)
        {
            m_meshModelFaceSelection.push_back(mapped);
        }
    }
    std::sort(m_meshModelFaceSelection.begin(), m_meshModelFaceSelection.end());
    m_meshModelFaceSelection.erase(
        std::unique(m_meshModelFaceSelection.begin(), m_meshModelFaceSelection.end()),
        m_meshModelFaceSelection.end());

    if (m_meshModelSelectedFace < 0 && !m_meshModelFaces.empty())
    {
        m_meshModelSelectedFace = 0;
    }
    if (m_meshModelSelectedVertex < 0 && !m_meshModelVertices.empty())
    {
        m_meshModelSelectedVertex = 0;
    }
    if (m_meshModelSelectedFace >= 0 &&
        std::find(m_meshModelFaceSelection.begin(), m_meshModelFaceSelection.end(), m_meshModelSelectedFace) ==
            m_meshModelFaceSelection.end())
    {
        m_meshModelFaceSelection.push_back(m_meshModelSelectedFace);
    }
    if (m_meshModelSelectedVertex >= 0 &&
        std::find(m_meshModelVertexSelection.begin(), m_meshModelVertexSelection.end(), m_meshModelSelectedVertex) ==
            m_meshModelVertexSelection.end())
    {
        m_meshModelVertexSelection.push_back(m_meshModelSelectedVertex);
    }

    m_meshModelSelectedEdge = -1;
    m_meshModelHoveredEdge = -1;
    m_meshModelBridgeEdgeA = -1;
    m_meshModelBridgeEdgeB = -1;
    m_meshModelLoopSelectionEdges.clear();
    m_meshModelRingSelectionEdges.clear();
    m_meshModelHoveredFace = -1;
    m_meshModelHoveredVertex = -1;
    m_meshModelKnifePreviewValid = false;
    m_meshModelKnifePreviewSegments.clear();
    m_meshModelBatchDragActive = false;
}

void LevelEditor::MeshModelerDeleteFace(int faceIndex)
{
    if (faceIndex < 0 || faceIndex >= static_cast<int>(m_meshModelFaces.size()))
    {
        return;
    }
    m_meshModelFaces[static_cast<std::size_t>(faceIndex)].deleted = true;
    CleanupMeshModelTopology();
}

void LevelEditor::MeshModelerDissolveFace(int faceIndex)
{
    if (faceIndex < 0 || faceIndex >= static_cast<int>(m_meshModelFaces.size()))
    {
        return;
    }
    const MeshModelFace& targetFaceRef = m_meshModelFaces[static_cast<std::size_t>(faceIndex)];
    if (targetFaceRef.deleted)
    {
        return;
    }

    auto faceNormal = [&](const MeshModelFace& face) -> glm::vec3 {
        const int count = std::clamp(face.vertexCount, 3, 4);
        if (count < 3)
        {
            return glm::vec3{0.0F, 1.0F, 0.0F};
        }
        const glm::vec3 a = m_meshModelVertices[static_cast<std::size_t>(face.indices[0])].position;
        const glm::vec3 b = m_meshModelVertices[static_cast<std::size_t>(face.indices[1])].position;
        const glm::vec3 c = m_meshModelVertices[static_cast<std::size_t>(face.indices[2])].position;
        glm::vec3 n = glm::cross(b - a, c - a);
        if (glm::length(n) < 1.0e-6F)
        {
            return glm::vec3{0.0F, 1.0F, 0.0F};
        }
        return glm::normalize(n);
    };
    auto edgeKey = [](int a, int b) {
        const std::uint32_t ua = static_cast<std::uint32_t>(std::min(a, b));
        const std::uint32_t ub = static_cast<std::uint32_t>(std::max(a, b));
        return (static_cast<std::uint64_t>(ua) << 32U) | static_cast<std::uint64_t>(ub);
    };

    const glm::vec3 targetNormal = faceNormal(targetFaceRef);
    int bestNeighbor = -1;
    float bestScore = -2.0F;
    for (int neighborIndex = 0; neighborIndex < static_cast<int>(m_meshModelFaces.size()); ++neighborIndex)
    {
        if (neighborIndex == faceIndex)
        {
            continue;
        }
        const MeshModelFace& neighborFace = m_meshModelFaces[static_cast<std::size_t>(neighborIndex)];
        if (neighborFace.deleted)
        {
            continue;
        }

        std::unordered_set<int> targetSet;
        std::unordered_set<int> neighborSet;
        const int targetCount = std::clamp(targetFaceRef.vertexCount, 3, 4);
        const int neighborCount = std::clamp(neighborFace.vertexCount, 3, 4);
        for (int i = 0; i < targetCount; ++i)
        {
            targetSet.insert(targetFaceRef.indices[static_cast<std::size_t>(i)]);
        }
        for (int i = 0; i < neighborCount; ++i)
        {
            neighborSet.insert(neighborFace.indices[static_cast<std::size_t>(i)]);
        }

        int sharedCount = 0;
        for (const int idx : targetSet)
        {
            if (neighborSet.contains(idx))
            {
                ++sharedCount;
            }
        }
        if (sharedCount != 2)
        {
            continue;
        }

        const glm::vec3 neighborNormal = faceNormal(neighborFace);
        const float coplanarScore = glm::dot(targetNormal, neighborNormal);
        if (coplanarScore < 0.75F)
        {
            continue;
        }
        if (coplanarScore > bestScore)
        {
            bestScore = coplanarScore;
            bestNeighbor = neighborIndex;
        }
    }

    if (bestNeighbor < 0)
    {
        m_statusLine = "Dissolve face canceled: no compatible neighbor";
        return;
    }

    struct BoundaryEdge
    {
        int a = -1;
        int b = -1;
    };
    std::unordered_map<std::uint64_t, std::vector<BoundaryEdge>> edgeCounts;
    edgeCounts.reserve(16);
    const std::array<int, 2> mergeFaces{faceIndex, bestNeighbor};
    for (const int fIndex : mergeFaces)
    {
        const MeshModelFace& face = m_meshModelFaces[static_cast<std::size_t>(fIndex)];
        const int count = std::clamp(face.vertexCount, 3, 4);
        for (int i = 0; i < count; ++i)
        {
            const int a = face.indices[static_cast<std::size_t>(i)];
            const int b = face.indices[static_cast<std::size_t>((i + 1) % count)];
            edgeCounts[edgeKey(a, b)].push_back(BoundaryEdge{a, b});
        }
    }

    std::vector<BoundaryEdge> boundary;
    boundary.reserve(8);
    for (const auto& [_, list] : edgeCounts)
    {
        if (list.size() == 1U)
        {
            boundary.push_back(list.front());
        }
    }
    if (boundary.size() < 3U)
    {
        m_statusLine = "Dissolve face canceled: invalid merge boundary";
        return;
    }

    std::unordered_map<int, std::vector<int>> graph;
    graph.reserve(boundary.size() * 2U);
    for (const BoundaryEdge& edge : boundary)
    {
        graph[edge.a].push_back(edge.b);
        graph[edge.b].push_back(edge.a);
    }

    int start = boundary.front().a;
    int previous = -1;
    int current = start;
    std::vector<int> loop;
    loop.reserve(boundary.size() + 1U);
    for (std::size_t guard = 0; guard < boundary.size() + 2U; ++guard)
    {
        loop.push_back(current);
        const auto it = graph.find(current);
        if (it == graph.end() || it->second.empty())
        {
            break;
        }
        int next = -1;
        for (const int candidate : it->second)
        {
            if (candidate != previous)
            {
                next = candidate;
                break;
            }
        }
        if (next < 0)
        {
            break;
        }
        previous = current;
        current = next;
        if (current == start)
        {
            break;
        }
    }
    if (loop.size() >= 2U && loop.back() == start)
    {
        loop.pop_back();
    }
    std::unordered_set<int> uniqueLoop(loop.begin(), loop.end());
    if (loop.size() < 3U || uniqueLoop.size() < 3U)
    {
        m_statusLine = "Dissolve face canceled: failed boundary ordering";
        return;
    }

    PushHistorySnapshot();
    m_meshModelFaces[static_cast<std::size_t>(bestNeighbor)].deleted = true;
    m_meshModelFaces[static_cast<std::size_t>(faceIndex)].deleted = true;
    for (std::size_t i = 1; i + 1 < loop.size(); ++i)
    {
        m_meshModelFaces.push_back(MeshModelFace{
            std::array<int, 4>{loop[0], loop[i], loop[i + 1], loop[i + 1]},
            false,
            3,
        });
    }
    CleanupMeshModelTopology();
    m_statusLine = "Dissolve face reconstructed " + std::to_string(loop.size()) + "-gon";
}

void LevelEditor::MeshModelerMoveVertex(int vertexIndex, const glm::vec3& delta)
{
    if (vertexIndex < 0 || vertexIndex >= static_cast<int>(m_meshModelVertices.size()))
    {
        return;
    }
    if (m_meshModelVertices[static_cast<std::size_t>(vertexIndex)].deleted)
    {
        return;
    }
    m_meshModelVertices[static_cast<std::size_t>(vertexIndex)].position += delta;
}

glm::vec3 LevelEditor::MeshModelSelectionPivot() const
{
    auto toWorld = [this](int vertexIndex) {
        return m_meshModelPosition + m_meshModelVertices[static_cast<std::size_t>(vertexIndex)].position * m_meshModelScale;
    };

    if (m_meshEditMode == MeshEditMode::Vertex)
    {
        std::vector<int> selected = m_meshModelVertexSelection;
        if (selected.empty() && m_meshModelSelectedVertex >= 0)
        {
            selected.push_back(m_meshModelSelectedVertex);
        }
        glm::vec3 center{0.0F};
        int count = 0;
        for (const int idx : selected)
        {
            if (idx < 0 || idx >= static_cast<int>(m_meshModelVertices.size()))
            {
                continue;
            }
            if (m_meshModelVertices[static_cast<std::size_t>(idx)].deleted)
            {
                continue;
            }
            center += toWorld(idx);
            ++count;
        }
        if (count > 0)
        {
            return center / static_cast<float>(count);
        }
    }
    else if (m_meshEditMode == MeshEditMode::Edge)
    {
        const std::vector<std::array<int, 2>> edges = BuildMeshModelEdges();
        if (m_meshModelSelectedEdge >= 0 && m_meshModelSelectedEdge < static_cast<int>(edges.size()))
        {
            const auto& edge = edges[static_cast<std::size_t>(m_meshModelSelectedEdge)];
            return (toWorld(edge[0]) + toWorld(edge[1])) * 0.5F;
        }
    }
    else
    {
        std::vector<int> faces = m_meshModelFaceSelection;
        if (faces.empty() && m_meshModelSelectedFace >= 0 && m_meshModelSelectedFace < static_cast<int>(m_meshModelFaces.size()))
        {
            faces.push_back(m_meshModelSelectedFace);
        }
        glm::vec3 center{0.0F};
        int count = 0;
        for (const int faceIndex : faces)
        {
            if (faceIndex < 0 || faceIndex >= static_cast<int>(m_meshModelFaces.size()))
            {
                continue;
            }
            const MeshModelFace& face = m_meshModelFaces[static_cast<std::size_t>(faceIndex)];
            if (face.deleted)
            {
                continue;
            }
            const int faceCount = std::clamp(face.vertexCount, 3, 4);
            for (int i = 0; i < faceCount; ++i)
            {
                const int idx = face.indices[static_cast<std::size_t>(i)];
                if (idx >= 0 && idx < static_cast<int>(m_meshModelVertices.size()) &&
                    !m_meshModelVertices[static_cast<std::size_t>(idx)].deleted)
                {
                    center += toWorld(idx);
                    ++count;
                }
            }
        }
        if (count > 0)
        {
            return center / static_cast<float>(count);
        }
    }
    return m_meshModelPosition;
}

bool LevelEditor::BuildActiveMeshLocalBasis(glm::mat3* outBasis) const
{
    if (outBasis == nullptr)
    {
        return false;
    }

    auto toWorld = [this](int vertexIndex) {
        return m_meshModelPosition + m_meshModelVertices[static_cast<std::size_t>(vertexIndex)].position * m_meshModelScale;
    };

    glm::vec3 basisX{1.0F, 0.0F, 0.0F};
    glm::vec3 basisY{0.0F, 1.0F, 0.0F};
    glm::vec3 basisZ{0.0F, 0.0F, 1.0F};

    auto safeNormalize = [](const glm::vec3& v, const glm::vec3& fallback) {
        const float len = glm::length(v);
        if (len < 1.0e-6F || !std::isfinite(len))
        {
            return fallback;
        }
        return v / len;
    };

    if (m_meshEditMode == MeshEditMode::Face && m_meshModelSelectedFace >= 0 &&
        m_meshModelSelectedFace < static_cast<int>(m_meshModelFaces.size()))
    {
        const MeshModelFace& face = m_meshModelFaces[static_cast<std::size_t>(m_meshModelSelectedFace)];
        if (!face.deleted)
        {
            const int i0 = face.indices[0];
            const int i1 = face.indices[1];
            const int i2 = face.indices[2];
            if (i0 >= 0 && i1 >= 0 && i2 >= 0 &&
                i0 < static_cast<int>(m_meshModelVertices.size()) &&
                i1 < static_cast<int>(m_meshModelVertices.size()) &&
                i2 < static_cast<int>(m_meshModelVertices.size()))
            {
                const glm::vec3 p0 = toWorld(i0);
                const glm::vec3 p1 = toWorld(i1);
                const glm::vec3 p2 = toWorld(i2);
                basisX = safeNormalize(p1 - p0, basisX);
                basisZ = safeNormalize(glm::cross(p1 - p0, p2 - p0), basisZ);
                basisY = safeNormalize(glm::cross(basisZ, basisX), basisY);
            }
        }
    }
    else if (m_meshEditMode == MeshEditMode::Edge)
    {
        const std::vector<std::array<int, 2>> edges = BuildMeshModelEdges();
        const std::vector<int> activeEdges = CollectMeshModelActiveEdges();
        if (!activeEdges.empty())
        {
            const int edgeIndex = activeEdges.front();
            if (edgeIndex >= 0 && edgeIndex < static_cast<int>(edges.size()))
            {
                const int a = edges[static_cast<std::size_t>(edgeIndex)][0];
                const int b = edges[static_cast<std::size_t>(edgeIndex)][1];
                if (a >= 0 && b >= 0 && a < static_cast<int>(m_meshModelVertices.size()) &&
                    b < static_cast<int>(m_meshModelVertices.size()))
                {
                    const glm::vec3 pa = toWorld(a);
                    const glm::vec3 pb = toWorld(b);
                    basisX = safeNormalize(pb - pa, basisX);

                    glm::vec3 avgNormal{0.0F};
                    int normalCount = 0;
                    for (const MeshModelFace& face : m_meshModelFaces)
                    {
                        if (face.deleted)
                        {
                            continue;
                        }
                        bool hasA = false;
                        bool hasB = false;
                        const int count = std::clamp(face.vertexCount, 3, 4);
                        for (int i = 0; i < count; ++i)
                        {
                            const int idx = face.indices[static_cast<std::size_t>(i)];
                            hasA = hasA || idx == a;
                            hasB = hasB || idx == b;
                        }
                        if (!hasA || !hasB)
                        {
                            continue;
                        }
                        const glm::vec3 p0 = toWorld(face.indices[0]);
                        const glm::vec3 p1 = toWorld(face.indices[1]);
                        const glm::vec3 p2 = toWorld(face.indices[2]);
                        const glm::vec3 n = glm::cross(p1 - p0, p2 - p0);
                        if (glm::length(n) > 1.0e-6F)
                        {
                            avgNormal += glm::normalize(n);
                            ++normalCount;
                        }
                    }
                    if (normalCount > 0)
                    {
                        basisZ = safeNormalize(avgNormal, basisZ);
                    }
                    basisY = safeNormalize(glm::cross(basisZ, basisX), basisY);
                    basisZ = safeNormalize(glm::cross(basisX, basisY), basisZ);
                }
            }
        }
    }
    else if (m_meshEditMode == MeshEditMode::Vertex && m_meshModelSelectedVertex >= 0 &&
             m_meshModelSelectedVertex < static_cast<int>(m_meshModelVertices.size()))
    {
        const int vertexIndex = m_meshModelSelectedVertex;
        glm::vec3 avgNormal{0.0F};
        int normalCount = 0;
        int firstNeighbor = -1;
        for (const MeshModelFace& face : m_meshModelFaces)
        {
            if (face.deleted)
            {
                continue;
            }
            const int count = std::clamp(face.vertexCount, 3, 4);
            bool hasVertex = false;
            for (int i = 0; i < count; ++i)
            {
                const int idx = face.indices[static_cast<std::size_t>(i)];
                if (idx == vertexIndex)
                {
                    hasVertex = true;
                }
                else if (firstNeighbor < 0)
                {
                    firstNeighbor = idx;
                }
            }
            if (!hasVertex)
            {
                continue;
            }
            const glm::vec3 p0 = toWorld(face.indices[0]);
            const glm::vec3 p1 = toWorld(face.indices[1]);
            const glm::vec3 p2 = toWorld(face.indices[2]);
            const glm::vec3 n = glm::cross(p1 - p0, p2 - p0);
            if (glm::length(n) > 1.0e-6F)
            {
                avgNormal += glm::normalize(n);
                ++normalCount;
            }
        }
        if (firstNeighbor >= 0 && firstNeighbor < static_cast<int>(m_meshModelVertices.size()))
        {
            basisX = safeNormalize(toWorld(firstNeighbor) - toWorld(vertexIndex), basisX);
        }
        if (normalCount > 0)
        {
            basisZ = safeNormalize(avgNormal, basisZ);
        }
        basisY = safeNormalize(glm::cross(basisZ, basisX), basisY);
        basisX = safeNormalize(glm::cross(basisY, basisZ), basisX);
    }

    *outBasis = glm::mat3{basisX, basisY, basisZ};
    return true;
}

void LevelEditor::MoveMeshSelection(const glm::vec3& delta)
{
    const glm::vec3 localDelta{
        std::abs(m_meshModelScale.x) > 1.0e-6F ? delta.x / m_meshModelScale.x : 0.0F,
        std::abs(m_meshModelScale.y) > 1.0e-6F ? delta.y / m_meshModelScale.y : 0.0F,
        std::abs(m_meshModelScale.z) > 1.0e-6F ? delta.z / m_meshModelScale.z : 0.0F,
    };

    std::unordered_set<int> moveVertices;
    if (m_meshEditMode == MeshEditMode::Vertex)
    {
        for (const int idx : m_meshModelVertexSelection)
        {
            moveVertices.insert(idx);
        }
        if (m_meshModelSelectedVertex >= 0)
        {
            moveVertices.insert(m_meshModelSelectedVertex);
        }
    }
    else if (m_meshEditMode == MeshEditMode::Edge)
    {
        const std::vector<std::array<int, 2>> edges = BuildMeshModelEdges();
        std::vector<int> edgeIndices = CollectMeshModelActiveEdges();
        if (edgeIndices.empty() && m_meshModelSelectedEdge >= 0)
        {
            edgeIndices.push_back(m_meshModelSelectedEdge);
        }
        for (const int edgeIndex : edgeIndices)
        {
            if (edgeIndex < 0 || edgeIndex >= static_cast<int>(edges.size()))
            {
                continue;
            }
            const auto& edge = edges[static_cast<std::size_t>(edgeIndex)];
            moveVertices.insert(edge[0]);
            moveVertices.insert(edge[1]);
        }
    }
    else
    {
        std::vector<int> selectedFaces = m_meshModelFaceSelection;
        if (selectedFaces.empty() && m_meshModelSelectedFace >= 0)
        {
            selectedFaces.push_back(m_meshModelSelectedFace);
        }
        for (const int faceIndex : selectedFaces)
        {
            if (faceIndex < 0 || faceIndex >= static_cast<int>(m_meshModelFaces.size()))
            {
                continue;
            }
            const MeshModelFace& face = m_meshModelFaces[static_cast<std::size_t>(faceIndex)];
            if (face.deleted)
            {
                continue;
            }
            const int faceCount = std::clamp(face.vertexCount, 3, 4);
            for (int i = 0; i < faceCount; ++i)
            {
                const int idx = face.indices[static_cast<std::size_t>(i)];
                moveVertices.insert(idx);
            }
        }
    }

    for (int idx : moveVertices)
    {
        if (idx < 0 || idx >= static_cast<int>(m_meshModelVertices.size()))
        {
            continue;
        }
        if (m_meshModelVertices[static_cast<std::size_t>(idx)].deleted)
        {
            continue;
        }
        m_meshModelVertices[static_cast<std::size_t>(idx)].position += localDelta;
    }
}

bool LevelEditor::RaycastMeshModel(const glm::vec3& rayOrigin, const glm::vec3& rayDirection, int* outFaceIndex, glm::vec3* outHitPoint) const
{
    auto toWorld = [this](int vertexIndex) {
        return m_meshModelPosition + m_meshModelVertices[static_cast<std::size_t>(vertexIndex)].position * m_meshModelScale;
    };

    float bestT = std::numeric_limits<float>::max();
    int bestFace = -1;
    glm::vec3 bestPoint{0.0F};

    for (int i = 0; i < static_cast<int>(m_meshModelFaces.size()); ++i)
    {
        const MeshModelFace& face = m_meshModelFaces[static_cast<std::size_t>(i)];
        if (face.deleted)
        {
            continue;
        }
        const int count = std::clamp(face.vertexCount, 3, 4);
        const int i0 = face.indices[0];
        const int i1 = face.indices[1];
        const int i2 = face.indices[2];
        const int i3 = face.indices[3];
        if (i0 < 0 || i1 < 0 || i2 < 0 ||
            i0 >= static_cast<int>(m_meshModelVertices.size()) ||
            i1 >= static_cast<int>(m_meshModelVertices.size()) ||
            i2 >= static_cast<int>(m_meshModelVertices.size()))
        {
            continue;
        }
        if (m_meshModelVertices[static_cast<std::size_t>(i0)].deleted ||
            m_meshModelVertices[static_cast<std::size_t>(i1)].deleted ||
            m_meshModelVertices[static_cast<std::size_t>(i2)].deleted)
        {
            continue;
        }
        if (count == 4 &&
            (i3 < 0 || i3 >= static_cast<int>(m_meshModelVertices.size()) ||
             m_meshModelVertices[static_cast<std::size_t>(i3)].deleted))
        {
            continue;
        }

        const glm::vec3 p0 = toWorld(i0);
        const glm::vec3 p1 = toWorld(i1);
        const glm::vec3 p2 = toWorld(i2);
        const glm::vec3 p3 = count == 4 ? toWorld(i3) : p2;

        float t0 = 0.0F;
        float t1 = 0.0F;
        float t = std::numeric_limits<float>::max();
        if (RayIntersectsTriangle(rayOrigin, rayDirection, p0, p1, p2, &t0))
        {
            t = std::min(t, t0);
        }
        if (count == 4 && RayIntersectsTriangle(rayOrigin, rayDirection, p0, p2, p3, &t1))
        {
            t = std::min(t, t1);
        }
        if (t < bestT)
        {
            bestT = t;
            bestFace = i;
            bestPoint = rayOrigin + rayDirection * t;
        }
    }

    if (bestFace < 0)
    {
        return false;
    }
    if (outFaceIndex != nullptr)
    {
        *outFaceIndex = bestFace;
    }
    if (outHitPoint != nullptr)
    {
        *outHitPoint = bestPoint;
    }
    return true;
}

void LevelEditor::UpdateMeshHover(const glm::vec3& rayOrigin, const glm::vec3& rayDirection)
{
    m_meshModelHoveredFace = -1;
    m_meshModelHoveredEdge = -1;
    m_meshModelHoveredVertex = -1;

    const auto toWorld = [this](int vertexIndex) {
        return m_meshModelPosition + m_meshModelVertices[static_cast<std::size_t>(vertexIndex)].position * m_meshModelScale;
    };

    if (m_meshEditMode == MeshEditMode::Vertex)
    {
        float bestMetric = std::numeric_limits<float>::max();
        for (int i = 0; i < static_cast<int>(m_meshModelVertices.size()); ++i)
        {
            const MeshModelVertex& vertex = m_meshModelVertices[static_cast<std::size_t>(i)];
            if (vertex.deleted)
            {
                continue;
            }
            const glm::vec3 p = toWorld(i);
            const float t = glm::dot(p - rayOrigin, rayDirection);
            if (t < 0.0F)
            {
                continue;
            }
            const glm::vec3 closest = rayOrigin + rayDirection * t;
            const float dist = glm::length(p - closest);
            if (dist > 0.5F)
            {
                continue;
            }
            const float metric = t + dist * 3.0F;
            if (metric < bestMetric)
            {
                bestMetric = metric;
                m_meshModelHoveredVertex = i;
            }
        }
        return;
    }

    if (m_meshEditMode == MeshEditMode::Edge)
    {
        const std::vector<std::array<int, 2>> edges = BuildMeshModelEdges();
        float bestMetric = std::numeric_limits<float>::max();
        for (int i = 0; i < static_cast<int>(edges.size()); ++i)
        {
            const auto& edge = edges[static_cast<std::size_t>(i)];
            float rayT = 0.0F;
            float segT = 0.0F;
            const float dist = DistanceRayToSegment(rayOrigin, rayDirection, toWorld(edge[0]), toWorld(edge[1]), &rayT, &segT);
            (void)segT;
            if (rayT < 0.0F || dist > 0.35F)
            {
                continue;
            }
            const float metric = rayT + dist * 5.0F;
            if (metric < bestMetric)
            {
                bestMetric = metric;
                m_meshModelHoveredEdge = i;
            }
        }
        return;
    }

    const bool hitFace = RaycastMeshModel(rayOrigin, rayDirection, &m_meshModelHoveredFace, nullptr);
    (void)hitFace;
}

bool LevelEditor::PickMeshModelInScene(const glm::vec3& rayOrigin, const glm::vec3& rayDirection)
{
    UpdateMeshHover(rayOrigin, rayDirection);
    if (m_meshEditMode == MeshEditMode::Vertex && m_meshModelHoveredVertex >= 0)
    {
        m_meshModelSelectedVertex = m_meshModelHoveredVertex;
        return true;
    }
    if (m_meshEditMode == MeshEditMode::Edge && m_meshModelHoveredEdge >= 0)
    {
        m_meshModelSelectedEdge = m_meshModelHoveredEdge;
        return true;
    }
    if (m_meshEditMode == MeshEditMode::Face && m_meshModelHoveredFace >= 0)
    {
        m_meshModelSelectedFace = m_meshModelHoveredFace;
        return true;
    }
    return false;
}

bool LevelEditor::StartMeshAxisDrag(const glm::vec3& rayOrigin, const glm::vec3& rayDirection)
{
    if (!m_meshModelSceneEditEnabled || !m_meshModelShowGizmo)
    {
        return false;
    }
    const std::vector<std::array<int, 2>> edges = BuildMeshModelEdges();
    const bool hasSelection =
        (m_meshEditMode == MeshEditMode::Vertex &&
         m_meshModelSelectedVertex >= 0 && m_meshModelSelectedVertex < static_cast<int>(m_meshModelVertices.size())) ||
        (m_meshEditMode == MeshEditMode::Edge &&
         m_meshModelSelectedEdge >= 0 && m_meshModelSelectedEdge < static_cast<int>(edges.size())) ||
        (m_meshEditMode == MeshEditMode::Face &&
         m_meshModelSelectedFace >= 0 && m_meshModelSelectedFace < static_cast<int>(m_meshModelFaces.size()));
    if (!hasSelection)
    {
        return false;
    }

    const glm::vec3 pivot = MeshModelSelectionPivot();
    const float cameraDistance = glm::length(m_cameraPosition - pivot);
    const float axisLength = glm::clamp(cameraDistance * 0.16F, 1.2F, 6.0F);
    const float handleHalf = glm::max(0.22F, axisLength * 0.16F);
    const glm::vec3 axisDirections[3] = {
        glm::vec3{1.0F, 0.0F, 0.0F},
        glm::vec3{0.0F, 1.0F, 0.0F},
        glm::vec3{0.0F, 0.0F, 1.0F},
    };

    float bestT = std::numeric_limits<float>::max();
    GizmoAxis bestAxis = GizmoAxis::None;
    glm::vec3 bestDirection{1.0F, 0.0F, 0.0F};
    for (int axisIndex = 0; axisIndex < 3; ++axisIndex)
    {
        const glm::vec3 direction = axisDirections[axisIndex];
        const glm::vec3 tip = pivot + direction * axisLength;
        float t = 0.0F;
        if (!SegmentIntersectsAabb(rayOrigin, rayDirection, tip - glm::vec3{handleHalf}, tip + glm::vec3{handleHalf}, &t))
        {
            continue;
        }
        if (t < bestT)
        {
            bestT = t;
            bestDirection = direction;
            bestAxis = axisIndex == 0 ? GizmoAxis::X : (axisIndex == 1 ? GizmoAxis::Y : GizmoAxis::Z);
        }
    }
    if (bestAxis == GizmoAxis::None)
    {
        return false;
    }

    glm::vec3 planeNormal = glm::cross(bestDirection, CameraForward());
    if (glm::length(planeNormal) < 1.0e-4F)
    {
        planeNormal = glm::cross(bestDirection, glm::vec3{0.0F, 1.0F, 0.0F});
    }
    if (glm::length(planeNormal) < 1.0e-4F)
    {
        planeNormal = glm::cross(bestDirection, glm::vec3{1.0F, 0.0F, 0.0F});
    }
    if (glm::length(planeNormal) < 1.0e-4F)
    {
        return false;
    }
    planeNormal = glm::normalize(planeNormal);

    glm::vec3 hit{0.0F};
    if (!RayIntersectPlane(rayOrigin, rayDirection, pivot, planeNormal, &hit))
    {
        return false;
    }

    m_meshModelAxisDragActive = true;
    m_meshModelAxisDragAxis = bestAxis;
    m_meshModelAxisDragPivot = pivot;
    m_meshModelAxisDragDirection = bestDirection;
    m_meshModelAxisDragPlaneNormal = planeNormal;
    m_meshModelAxisDragStartScalar = glm::dot(hit - pivot, bestDirection);
    m_meshModelAxisDragLastScalar = m_meshModelAxisDragStartScalar;
    return true;
}

void LevelEditor::UpdateMeshAxisDrag(const glm::vec3& rayOrigin, const glm::vec3& rayDirection)
{
    if (!m_meshModelAxisDragActive)
    {
        return;
    }

    glm::vec3 hit{0.0F};
    if (!RayIntersectPlane(rayOrigin, rayDirection, m_meshModelAxisDragPivot, m_meshModelAxisDragPlaneNormal, &hit))
    {
        return;
    }

    const float scalar = glm::dot(hit - m_meshModelAxisDragPivot, m_meshModelAxisDragDirection);
    const float previous = m_meshModelAxisDragLastScalar;
    float delta = scalar - previous;
    if (m_gridSnap)
    {
        const float step = std::max(0.02F, m_gridStep * 0.1F);
        const float snappedNow = std::round((scalar - m_meshModelAxisDragStartScalar) / step) * step;
        const float snappedBefore = std::round((previous - m_meshModelAxisDragStartScalar) / step) * step;
        delta = snappedNow - snappedBefore;
    }
    if (std::abs(delta) < 1.0e-6F)
    {
        m_meshModelAxisDragLastScalar = scalar;
        return;
    }
    MoveMeshSelection(m_meshModelAxisDragDirection * delta);
    m_meshModelAxisDragLastScalar = scalar;
}

void LevelEditor::StopMeshAxisDrag()
{
    m_meshModelAxisDragActive = false;
    m_meshModelAxisDragAxis = GizmoAxis::None;
    m_meshModelAxisDragDirection = glm::vec3{1.0F, 0.0F, 0.0F};
    m_meshModelAxisDragPlaneNormal = glm::vec3{0.0F, 1.0F, 0.0F};
    m_meshModelAxisDragStartScalar = 0.0F;
    m_meshModelAxisDragLastScalar = 0.0F;
}

bool LevelEditor::ComputeMeshBatchEdgeGizmo(
    glm::vec3* outPivot,
    glm::vec3* outDirection,
    glm::vec3* outPlaneNormal,
    float* outAxisLength
) const
{
    if (outPivot == nullptr || outDirection == nullptr || outPlaneNormal == nullptr || outAxisLength == nullptr)
    {
        return false;
    }
    if (!m_meshModelSceneEditEnabled || !m_meshModelShowGizmo || !m_meshModelBatchGizmoEnabled ||
        m_meshEditMode != MeshEditMode::Edge)
    {
        return false;
    }

    const std::vector<std::array<int, 2>> edges = BuildMeshModelEdges();
    const std::vector<int> activeEdges = CollectMeshModelActiveEdges();
    if (activeEdges.empty())
    {
        return false;
    }

    auto toWorld = [this](int vertexIndex) {
        return m_meshModelPosition + m_meshModelVertices[static_cast<std::size_t>(vertexIndex)].position * m_meshModelScale;
    };
    auto computeEdgeDirection = [&](int edgeIndex) {
        glm::vec3 averageNormal{0.0F};
        int adjacentFaces = 0;
        if (edgeIndex < 0 || edgeIndex >= static_cast<int>(edges.size()))
        {
            return glm::vec3{0.0F};
        }
        const int i0 = edges[static_cast<std::size_t>(edgeIndex)][0];
        const int i1 = edges[static_cast<std::size_t>(edgeIndex)][1];
        for (const MeshModelFace& face : m_meshModelFaces)
        {
            if (face.deleted)
            {
                continue;
            }
            bool hasI0 = false;
            bool hasI1 = false;
            const int count = std::clamp(face.vertexCount, 3, 4);
            for (int i = 0; i < count; ++i)
            {
                const int idx = face.indices[static_cast<std::size_t>(i)];
                hasI0 = hasI0 || idx == i0;
                hasI1 = hasI1 || idx == i1;
            }
            if (!hasI0 || !hasI1)
            {
                continue;
            }
            const glm::vec3 p0 = m_meshModelVertices[static_cast<std::size_t>(face.indices[0])].position;
            const glm::vec3 p1 = m_meshModelVertices[static_cast<std::size_t>(face.indices[1])].position;
            const glm::vec3 p2 = m_meshModelVertices[static_cast<std::size_t>(face.indices[2])].position;
            glm::vec3 n = glm::cross(p1 - p0, p2 - p0);
            if (glm::length(n) > 1.0e-6F)
            {
                averageNormal += glm::normalize(n);
                ++adjacentFaces;
            }
        }
        if (adjacentFaces == 0 || glm::length(averageNormal) < 1.0e-6F)
        {
            return glm::vec3{0.0F, 1.0F, 0.0F};
        }
        return glm::normalize(averageNormal);
    };

    glm::vec3 pivot{0.0F};
    glm::vec3 directionSum{0.0F};
    int usedEdges = 0;
    for (const int edgeIndex : activeEdges)
    {
        if (edgeIndex < 0 || edgeIndex >= static_cast<int>(edges.size()))
        {
            continue;
        }
        const auto edge = edges[static_cast<std::size_t>(edgeIndex)];
        const glm::vec3 p0 = toWorld(edge[0]);
        const glm::vec3 p1 = toWorld(edge[1]);
        pivot += (p0 + p1) * 0.5F;

        glm::vec3 edgeDir = computeEdgeDirection(edgeIndex);
        if (glm::length(directionSum) > 1.0e-6F && glm::dot(directionSum, edgeDir) < 0.0F)
        {
            edgeDir = -edgeDir;
        }
        directionSum += edgeDir;
        ++usedEdges;
    }
    if (usedEdges <= 0)
    {
        return false;
    }
    pivot /= static_cast<float>(usedEdges);

    glm::vec3 direction = directionSum;
    if (glm::length(direction) < 1.0e-6F)
    {
        direction = glm::vec3{0.0F, 1.0F, 0.0F};
    }
    direction = glm::normalize(direction);

    glm::vec3 planeNormal = glm::cross(direction, CameraForward());
    if (glm::length(planeNormal) < 1.0e-4F)
    {
        planeNormal = glm::cross(direction, CameraUp());
    }
    if (glm::length(planeNormal) < 1.0e-4F)
    {
        planeNormal = glm::cross(direction, glm::vec3{1.0F, 0.0F, 0.0F});
    }
    if (glm::length(planeNormal) < 1.0e-4F)
    {
        return false;
    }
    planeNormal = glm::normalize(planeNormal);

    const float cameraDistance = glm::length(m_cameraPosition - pivot);
    const float axisLength = glm::clamp(cameraDistance * 0.14F, 0.9F, 4.5F);

    *outPivot = pivot;
    *outDirection = direction;
    *outPlaneNormal = planeNormal;
    *outAxisLength = axisLength;
    return true;
}

bool LevelEditor::StartMeshBatchEdgeDrag(const glm::vec3& rayOrigin, const glm::vec3& rayDirection)
{
    glm::vec3 pivot{0.0F};
    glm::vec3 direction{0.0F, 1.0F, 0.0F};
    glm::vec3 planeNormal{1.0F, 0.0F, 0.0F};
    float axisLength = 0.0F;
    if (!ComputeMeshBatchEdgeGizmo(&pivot, &direction, &planeNormal, &axisLength))
    {
        return false;
    }

    const float handleHalf = glm::max(0.14F, axisLength * 0.1F);
    const glm::vec3 tip = pivot + direction * axisLength;
    float hitT = 0.0F;
    if (!SegmentIntersectsAabb(rayOrigin, rayDirection, tip - glm::vec3{handleHalf}, tip + glm::vec3{handleHalf}, &hitT))
    {
        return false;
    }

    glm::vec3 hit{0.0F};
    if (!RayIntersectPlane(rayOrigin, rayDirection, pivot, planeNormal, &hit))
    {
        return false;
    }

    m_meshModelBatchDragActive = true;
    m_meshModelBatchDragPivot = pivot;
    m_meshModelBatchDragDirection = direction;
    m_meshModelBatchDragPlaneNormal = planeNormal;
    m_meshModelBatchDragStartScalar = glm::dot(hit - pivot, direction) - m_meshModelBatchPreviewDistance;
    return true;
}

void LevelEditor::UpdateMeshBatchEdgeDrag(const glm::vec3& rayOrigin, const glm::vec3& rayDirection)
{
    if (!m_meshModelBatchDragActive)
    {
        return;
    }
    glm::vec3 hit{0.0F};
    if (!RayIntersectPlane(rayOrigin, rayDirection, m_meshModelBatchDragPivot, m_meshModelBatchDragPlaneNormal, &hit))
    {
        return;
    }

    const float scalar = glm::dot(hit - m_meshModelBatchDragPivot, m_meshModelBatchDragDirection);
    float distance = scalar - m_meshModelBatchDragStartScalar;
    distance = glm::max(0.0F, distance);
    if (m_gridSnap)
    {
        const float snap = std::max(0.01F, m_gridStep * 0.05F);
        distance = std::round(distance / snap) * snap;
    }
    m_meshModelBatchPreviewDistance = glm::clamp(distance, 0.0F, 6.0F);
    m_meshModelExtrudeDistance = glm::max(0.01F, m_meshModelBatchPreviewDistance);
    m_meshModelBevelDistance = glm::max(0.01F, m_meshModelBatchPreviewDistance);
}

void LevelEditor::StopMeshBatchEdgeDrag()
{
    m_meshModelBatchDragActive = false;
    m_meshModelBatchDragPivot = glm::vec3{0.0F};
    m_meshModelBatchDragDirection = glm::vec3{0.0F, 1.0F, 0.0F};
    m_meshModelBatchDragPlaneNormal = glm::vec3{1.0F, 0.0F, 0.0F};
    m_meshModelBatchDragStartScalar = 0.0F;
}

bool LevelEditor::BuildKnifePreviewSegments(
    const glm::vec3& lineStartWorld,
    const glm::vec3& lineEndWorld,
    std::vector<std::pair<glm::vec3, glm::vec3>>* outSegments
) const
{
    if (outSegments == nullptr)
    {
        return false;
    }
    outSegments->clear();
    if (glm::length(lineEndWorld - lineStartWorld) < 1.0e-5F)
    {
        return false;
    }

    auto toWorld = [this](int vertexIndex) {
        return m_meshModelPosition + m_meshModelVertices[static_cast<std::size_t>(vertexIndex)].position * m_meshModelScale;
    };
    auto cross2 = [](const glm::vec2& a, const glm::vec2& b) {
        return a.x * b.y - a.y * b.x;
    };
    auto segmentIntersect2d = [&](const glm::vec2& a0, const glm::vec2& a1,
                                  const glm::vec2& b0, const glm::vec2& b1,
                                  float* outA, float* outB) {
        const glm::vec2 r = a1 - a0;
        const glm::vec2 s = b1 - b0;
        const float denom = cross2(r, s);
        if (std::abs(denom) < 1.0e-6F)
        {
            return false;
        }
        const glm::vec2 delta = b0 - a0;
        const float t = cross2(delta, s) / denom;
        const float u = cross2(delta, r) / denom;
        if (t < -1.0e-4F || t > 1.0F + 1.0e-4F || u < -1.0e-4F || u > 1.0F + 1.0e-4F)
        {
            return false;
        }
        if (outA != nullptr)
        {
            *outA = glm::clamp(t, 0.0F, 1.0F);
        }
        if (outB != nullptr)
        {
            *outB = glm::clamp(u, 0.0F, 1.0F);
        }
        return true;
    };

    struct Hit
    {
        int edge = -1;
        float edgeT = 0.0F;
        float lineT = 0.0F;
    };

    for (int faceIndex = 0; faceIndex < static_cast<int>(m_meshModelFaces.size()); ++faceIndex)
    {
        const MeshModelFace& face = m_meshModelFaces[static_cast<std::size_t>(faceIndex)];
        if (face.deleted)
        {
            continue;
        }
        const int count = std::clamp(face.vertexCount, 3, 4);
        bool invalid = false;
        std::vector<glm::vec3> worldVerts(static_cast<std::size_t>(count));
        for (int i = 0; i < count; ++i)
        {
            const int idx = face.indices[static_cast<std::size_t>(i)];
            if (idx < 0 || idx >= static_cast<int>(m_meshModelVertices.size()) ||
                m_meshModelVertices[static_cast<std::size_t>(idx)].deleted)
            {
                invalid = true;
                break;
            }
            worldVerts[static_cast<std::size_t>(i)] = toWorld(idx);
        }
        if (invalid)
        {
            continue;
        }

        const glm::vec3 axisXRaw = worldVerts[1] - worldVerts[0];
        glm::vec3 faceNormal = glm::cross(axisXRaw, worldVerts[2] - worldVerts[0]);
        if (glm::length(axisXRaw) < 1.0e-6F || glm::length(faceNormal) < 1.0e-6F)
        {
            continue;
        }
        const glm::vec3 axisX = glm::normalize(axisXRaw);
        faceNormal = glm::normalize(faceNormal);
        glm::vec3 axisY = glm::cross(faceNormal, axisX);
        if (glm::length(axisY) < 1.0e-6F)
        {
            continue;
        }
        axisY = glm::normalize(axisY);

        auto project2 = [&](const glm::vec3& point) {
            const glm::vec3 rel = point - worldVerts[0];
            return glm::vec2{glm::dot(rel, axisX), glm::dot(rel, axisY)};
        };

        const glm::vec2 lineA = project2(lineStartWorld);
        const glm::vec2 lineB = project2(lineEndWorld);
        std::vector<glm::vec2> face2d(static_cast<std::size_t>(count));
        for (int i = 0; i < count; ++i)
        {
            face2d[static_cast<std::size_t>(i)] = project2(worldVerts[static_cast<std::size_t>(i)]);
        }

        std::vector<Hit> hits;
        hits.reserve(static_cast<std::size_t>(count));
        for (int edge = 0; edge < count; ++edge)
        {
            float lineT = 0.0F;
            float edgeT = 0.0F;
            if (!segmentIntersect2d(
                    lineA,
                    lineB,
                    face2d[static_cast<std::size_t>(edge)],
                    face2d[static_cast<std::size_t>((edge + 1) % count)],
                    &lineT,
                    &edgeT))
            {
                continue;
            }
            hits.push_back(Hit{edge, edgeT, lineT});
        }
        if (hits.size() < 2U)
        {
            continue;
        }

        std::sort(hits.begin(), hits.end(), [](const Hit& a, const Hit& b) { return a.lineT < b.lineT; });
        const Hit firstHit = hits.front();
        const Hit secondHit = hits.back();
        if (firstHit.edge == secondHit.edge)
        {
            continue;
        }

        const int edgeA0 = firstHit.edge;
        const int edgeA1 = (firstHit.edge + 1) % count;
        const int edgeB0 = secondHit.edge;
        const int edgeB1 = (secondHit.edge + 1) % count;
        const glm::vec3 cutA = glm::mix(
            worldVerts[static_cast<std::size_t>(edgeA0)],
            worldVerts[static_cast<std::size_t>(edgeA1)],
            firstHit.edgeT);
        const glm::vec3 cutB = glm::mix(
            worldVerts[static_cast<std::size_t>(edgeB0)],
            worldVerts[static_cast<std::size_t>(edgeB1)],
            secondHit.edgeT);
        outSegments->push_back(std::pair<glm::vec3, glm::vec3>{cutA, cutB});
    }
    return !outSegments->empty();
}

bool LevelEditor::HandleMeshKnifeClick(const glm::vec3& rayOrigin, const glm::vec3& rayDirection)
{
    if (!m_meshModelKnifeEnabled)
    {
        return false;
    }

    int bestFace = -1;
    glm::vec3 bestHitPoint{0.0F};
    auto toWorld = [this](int vertexIndex) {
        return m_meshModelPosition + m_meshModelVertices[static_cast<std::size_t>(vertexIndex)].position * m_meshModelScale;
    };

    if (!RaycastMeshModel(rayOrigin, rayDirection, &bestFace, &bestHitPoint))
    {
        return false;
    }

    auto toLocal = [this](const glm::vec3& worldPoint) {
        return glm::vec3{
            std::abs(m_meshModelScale.x) > 1.0e-6F ? (worldPoint.x - m_meshModelPosition.x) / m_meshModelScale.x : 0.0F,
            std::abs(m_meshModelScale.y) > 1.0e-6F ? (worldPoint.y - m_meshModelPosition.y) / m_meshModelScale.y : 0.0F,
            std::abs(m_meshModelScale.z) > 1.0e-6F ? (worldPoint.z - m_meshModelPosition.z) / m_meshModelScale.z : 0.0F,
        };
    };
    const glm::vec3 hitLocal = toLocal(bestHitPoint);
    m_meshModelSelectedFace = bestFace;

    if (!m_meshModelKnifeHasFirstPoint)
    {
        m_meshModelKnifeHasFirstPoint = true;
        m_meshModelKnifeFaceIndex = bestFace;
        m_meshModelKnifeFirstPointLocal = hitLocal;
        m_meshModelKnifeFirstPointWorld = bestHitPoint;
        m_meshModelKnifePreviewSegments.clear();
        m_statusLine = "Knife start point set (click end point, can cross many faces)";
        return true;
    }

    const glm::vec3 lineStartWorld = m_meshModelKnifeFirstPointWorld;
    const glm::vec3 lineEndWorld = bestHitPoint;
    m_meshModelKnifeHasFirstPoint = false;
    const int startFace = m_meshModelKnifeFaceIndex;
    const int endFace = bestFace;
    m_meshModelKnifeFaceIndex = -1;
    m_meshModelKnifePreviewSegments.clear();

    if (glm::length(lineEndWorld - lineStartWorld) < 1.0e-4F)
    {
        m_statusLine = "Knife canceled: points too close";
        return true;
    }

    struct KnifeCutCandidate
    {
        int faceIndex = -1;
        int edgeA = -1;
        int edgeB = -1;
        float edgeAT = 0.0F;
        float edgeBT = 0.0F;
    };

    auto cross2 = [](const glm::vec2& a, const glm::vec2& b) {
        return a.x * b.y - a.y * b.x;
    };
    auto segmentIntersect2d = [&](const glm::vec2& a0, const glm::vec2& a1,
                                  const glm::vec2& b0, const glm::vec2& b1,
                                  float* outA, float* outB) {
        const glm::vec2 r = a1 - a0;
        const glm::vec2 s = b1 - b0;
        const float denom = cross2(r, s);
        if (std::abs(denom) < 1.0e-6F)
        {
            return false;
        }
        const glm::vec2 delta = b0 - a0;
        const float t = cross2(delta, s) / denom;
        const float u = cross2(delta, r) / denom;
        if (t < -1.0e-4F || t > 1.0F + 1.0e-4F || u < -1.0e-4F || u > 1.0F + 1.0e-4F)
        {
            return false;
        }
        if (outA != nullptr)
        {
            *outA = glm::clamp(t, 0.0F, 1.0F);
        }
        if (outB != nullptr)
        {
            *outB = glm::clamp(u, 0.0F, 1.0F);
        }
        return true;
    };

    std::unordered_map<int, KnifeCutCandidate> candidatesByFace;
    candidatesByFace.reserve(m_meshModelFaces.size());

    for (int faceIndex = 0; faceIndex < static_cast<int>(m_meshModelFaces.size()); ++faceIndex)
    {
        const MeshModelFace& face = m_meshModelFaces[static_cast<std::size_t>(faceIndex)];
        if (face.deleted)
        {
            continue;
        }
        const int count = std::clamp(face.vertexCount, 3, 4);
        std::array<int, 4> indices = face.indices;
        bool invalid = false;
        std::vector<glm::vec3> worldVerts(static_cast<std::size_t>(count));
        for (int i = 0; i < count; ++i)
        {
            const int idx = indices[static_cast<std::size_t>(i)];
            if (idx < 0 || idx >= static_cast<int>(m_meshModelVertices.size()) ||
                m_meshModelVertices[static_cast<std::size_t>(idx)].deleted)
            {
                invalid = true;
                break;
            }
            worldVerts[static_cast<std::size_t>(i)] = toWorld(idx);
        }
        if (invalid)
        {
            continue;
        }

        const glm::vec3 axisXRaw = worldVerts[1] - worldVerts[0];
        glm::vec3 faceNormal = glm::cross(axisXRaw, worldVerts[2] - worldVerts[0]);
        if (glm::length(axisXRaw) < 1.0e-6F || glm::length(faceNormal) < 1.0e-6F)
        {
            continue;
        }
        const glm::vec3 axisX = glm::normalize(axisXRaw);
        faceNormal = glm::normalize(faceNormal);
        glm::vec3 axisY = glm::cross(faceNormal, axisX);
        if (glm::length(axisY) < 1.0e-6F)
        {
            continue;
        }
        axisY = glm::normalize(axisY);

        auto project2 = [&](const glm::vec3& point) {
            const glm::vec3 rel = point - worldVerts[0];
            return glm::vec2{glm::dot(rel, axisX), glm::dot(rel, axisY)};
        };

        const glm::vec2 lineA = project2(lineStartWorld);
        const glm::vec2 lineB = project2(lineEndWorld);
        std::vector<glm::vec2> face2d(static_cast<std::size_t>(count));
        for (int i = 0; i < count; ++i)
        {
            face2d[static_cast<std::size_t>(i)] = project2(worldVerts[static_cast<std::size_t>(i)]);
        }

        struct Hit
        {
            int edge = -1;
            float edgeT = 0.0F;
            float lineT = 0.0F;
        };
        std::vector<Hit> hits;
        hits.reserve(static_cast<std::size_t>(count));
        for (int edge = 0; edge < count; ++edge)
        {
            float lineT = 0.0F;
            float edgeT = 0.0F;
            if (!segmentIntersect2d(lineA, lineB, face2d[static_cast<std::size_t>(edge)],
                                    face2d[static_cast<std::size_t>((edge + 1) % count)], &lineT, &edgeT))
            {
                continue;
            }
            hits.push_back(Hit{edge, edgeT, lineT});
        }
        if (hits.size() < 2U)
        {
            continue;
        }
        std::sort(hits.begin(), hits.end(), [](const Hit& a, const Hit& b) { return a.lineT < b.lineT; });
        const Hit firstHit = hits.front();
        const Hit secondHit = hits.back();
        if (firstHit.edge == secondHit.edge)
        {
            continue;
        }

        candidatesByFace.emplace(faceIndex, KnifeCutCandidate{
                                             faceIndex,
                                             firstHit.edge,
                                             secondHit.edge,
                                             firstHit.edgeT,
                                             secondHit.edgeT,
                                         });
    }

    if (candidatesByFace.empty())
    {
        m_statusLine = "Knife canceled: no faces crossed by cut";
        return true;
    }

    auto edgeKey = [](int a, int b) {
        const std::uint32_t ua = static_cast<std::uint32_t>(std::min(a, b));
        const std::uint32_t ub = static_cast<std::uint32_t>(std::max(a, b));
        return (static_cast<std::uint64_t>(ua) << 32U) | static_cast<std::uint64_t>(ub);
    };

    std::unordered_map<std::uint64_t, std::vector<int>> faceEdges;
    for (const auto& [faceIndex, _] : candidatesByFace)
    {
        const MeshModelFace& face = m_meshModelFaces[static_cast<std::size_t>(faceIndex)];
        const int count = std::clamp(face.vertexCount, 3, 4);
        for (int i = 0; i < count; ++i)
        {
            const int a = face.indices[static_cast<std::size_t>(i)];
            const int b = face.indices[static_cast<std::size_t>((i + 1) % count)];
            if (a >= 0 && b >= 0)
            {
                faceEdges[edgeKey(a, b)].push_back(faceIndex);
            }
        }
    }

    std::unordered_map<int, std::vector<int>> adjacency;
    adjacency.reserve(candidatesByFace.size());
    for (const auto& [_, faces] : faceEdges)
    {
        for (std::size_t i = 0; i < faces.size(); ++i)
        {
            for (std::size_t j = i + 1; j < faces.size(); ++j)
            {
                adjacency[faces[i]].push_back(faces[j]);
                adjacency[faces[j]].push_back(faces[i]);
            }
        }
    }

    std::vector<int> facesToCut;
    const bool hasStart = candidatesByFace.contains(startFace);
    const bool hasEnd = candidatesByFace.contains(endFace);
    if (hasStart && hasEnd && startFace != endFace)
    {
        std::unordered_map<int, int> parent;
        std::unordered_set<int> visited;
        std::vector<int> queue;
        queue.push_back(startFace);
        visited.insert(startFace);

        std::size_t queueIndex = 0;
        bool found = false;
        while (queueIndex < queue.size())
        {
            const int current = queue[queueIndex++];
            if (current == endFace)
            {
                found = true;
                break;
            }
            const auto it = adjacency.find(current);
            if (it == adjacency.end())
            {
                continue;
            }
            for (const int next : it->second)
            {
                if (visited.insert(next).second)
                {
                    parent[next] = current;
                    queue.push_back(next);
                }
            }
        }
        if (found)
        {
            int current = endFace;
            facesToCut.push_back(current);
            while (current != startFace)
            {
                const auto parentIt = parent.find(current);
                if (parentIt == parent.end())
                {
                    break;
                }
                current = parentIt->second;
                facesToCut.push_back(current);
            }
            std::reverse(facesToCut.begin(), facesToCut.end());
        }
    }

    if (facesToCut.empty())
    {
        int seedFace = hasStart ? startFace : (hasEnd ? endFace : candidatesByFace.begin()->first);
        std::unordered_set<int> visited;
        std::vector<int> queue;
        queue.push_back(seedFace);
        visited.insert(seedFace);
        std::size_t queueIndex = 0;
        while (queueIndex < queue.size())
        {
            const int current = queue[queueIndex++];
            facesToCut.push_back(current);
            const auto it = adjacency.find(current);
            if (it == adjacency.end())
            {
                continue;
            }
            for (const int next : it->second)
            {
                if (visited.insert(next).second)
                {
                    queue.push_back(next);
                }
            }
        }
    }

    if (facesToCut.empty())
    {
        m_statusLine = "Knife canceled: no valid connected cut path";
        return true;
    }

    PushHistorySnapshot();

    const auto addVertex = [this](const glm::vec3& p) {
        m_meshModelVertices.push_back(MeshModelVertex{p, false});
        return static_cast<int>(m_meshModelVertices.size()) - 1;
    };

    int cutsApplied = 0;
    int lastNewFace = -1;
    for (const int faceIndex : facesToCut)
    {
        const auto candidateIt = candidatesByFace.find(faceIndex);
        if (candidateIt == candidatesByFace.end())
        {
            continue;
        }
        MeshModelFace& face = m_meshModelFaces[static_cast<std::size_t>(faceIndex)];
        if (face.deleted)
        {
            continue;
        }
        const KnifeCutCandidate& candidate = candidateIt->second;
        const int eA = candidate.edgeA;
        const int eB = candidate.edgeB;
        const int count = std::clamp(face.vertexCount, 3, 4);
        if (eA < 0 || eB < 0 || eA >= count || eB >= count || eA == eB)
        {
            continue;
        }

        bool invalidFace = false;
        for (int i = 0; i < count; ++i)
        {
            const int idx = face.indices[static_cast<std::size_t>(i)];
            if (idx < 0 || idx >= static_cast<int>(m_meshModelVertices.size()) ||
                m_meshModelVertices[static_cast<std::size_t>(idx)].deleted)
            {
                invalidFace = true;
                break;
            }
        }
        if (invalidFace)
        {
            continue;
        }

        const auto edgeVertex = [&](int edgeIndex, float t) {
            const int a = face.indices[static_cast<std::size_t>(edgeIndex)];
            const int b = face.indices[static_cast<std::size_t>((edgeIndex + 1) % count)];
            if (t <= 1.0e-4F)
            {
                return a;
            }
            if (t >= 1.0F - 1.0e-4F)
            {
                return b;
            }
            const glm::vec3 pa = m_meshModelVertices[static_cast<std::size_t>(a)].position;
            const glm::vec3 pb = m_meshModelVertices[static_cast<std::size_t>(b)].position;
            return addVertex(glm::mix(pa, pb, t));
        };

        const int cutA = edgeVertex(eA, candidate.edgeAT);
        const int cutB = edgeVertex(eB, candidate.edgeBT);

        auto buildPathVertices = [&](int startEdge, int endEdge, int startCut, int endCut) {
            std::vector<int> path;
            path.reserve(static_cast<std::size_t>(count + 2));
            path.push_back(startCut);
            int v = (startEdge + 1) % count;
            const int endVertex = (endEdge + 1) % count;
            int guard = 0;
            while (v != endVertex && guard < count + 2)
            {
                path.push_back(face.indices[static_cast<std::size_t>(v)]);
                v = (v + 1) % count;
                ++guard;
            }
            path.push_back(endCut);
            return path;
        };
        auto emitTriFan = [&](const std::vector<int>& polygon) {
            std::vector<int> cleaned;
            cleaned.reserve(polygon.size());
            for (const int idx : polygon)
            {
                if (cleaned.empty() || cleaned.back() != idx)
                {
                    cleaned.push_back(idx);
                }
            }
            if (cleaned.size() > 1U && cleaned.front() == cleaned.back())
            {
                cleaned.pop_back();
            }
            std::unordered_set<int> unique(cleaned.begin(), cleaned.end());
            if (cleaned.size() < 3U || unique.size() < 3U)
            {
                return;
            }
            for (std::size_t i = 1; i + 1 < cleaned.size(); ++i)
            {
                m_meshModelFaces.push_back(MeshModelFace{
                    std::array<int, 4>{cleaned[0], cleaned[i], cleaned[i + 1], cleaned[i + 1]},
                    false,
                    3,
                });
                lastNewFace = static_cast<int>(m_meshModelFaces.size()) - 1;
                ++cutsApplied;
            }
        };

        const std::vector<int> polygonA = buildPathVertices(eA, eB, cutA, cutB);
        const std::vector<int> polygonB = buildPathVertices(eB, eA, cutB, cutA);
        face.deleted = true;
        emitTriFan(polygonA);
        emitTriFan(polygonB);
    }

    if (cutsApplied <= 0)
    {
        m_statusLine = "Knife canceled: no faces could be split";
        return true;
    }

    CleanupMeshModelTopology();
    if (lastNewFace >= 0)
    {
        m_meshModelSelectedFace = glm::clamp(lastNewFace, 0, static_cast<int>(m_meshModelFaces.size()) - 1);
    }
    m_statusLine = "Knife cut applied on " + std::to_string(cutsApplied) + " face(s)";
    return true;
}

bool LevelEditor::ExportMeshModelerObj(const std::string& assetName, std::string* outRelativePath, std::string* outError) const
{
    std::string sanitized;
    sanitized.reserve(assetName.size());
    for (const char c : assetName)
    {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-')
        {
            sanitized.push_back(c);
        }
        else if (c == ' ' || c == '.')
        {
            sanitized.push_back('_');
        }
    }
    if (sanitized.empty())
    {
        sanitized = "generated_mesh";
    }

    const std::filesystem::path relPath = std::filesystem::path("assets") / "meshes" / (sanitized + ".obj");
    std::error_code ec;
    std::filesystem::create_directories(relPath.parent_path(), ec);
    if (ec)
    {
        if (outError != nullptr)
        {
            *outError = "Failed to create mesh directory: " + ec.message();
        }
        return false;
    }

    std::unordered_set<int> usedVertices;
    for (const MeshModelFace& face : m_meshModelFaces)
    {
        if (face.deleted)
        {
            continue;
        }
        const int count = std::clamp(face.vertexCount, 3, 4);
        for (int i = 0; i < count; ++i)
        {
            const int idx = face.indices[static_cast<std::size_t>(i)];
            if (idx >= 0 && idx < static_cast<int>(m_meshModelVertices.size()) &&
                !m_meshModelVertices[static_cast<std::size_t>(idx)].deleted)
            {
                usedVertices.insert(idx);
            }
        }
    }
    if (usedVertices.empty())
    {
        if (outError != nullptr)
        {
            *outError = "Mesh export failed: no valid faces.";
        }
        return false;
    }

    std::vector<int> sortedVertices(usedVertices.begin(), usedVertices.end());
    std::sort(sortedVertices.begin(), sortedVertices.end());
    std::unordered_map<int, int> remap;
    remap.reserve(sortedVertices.size());
    for (int i = 0; i < static_cast<int>(sortedVertices.size()); ++i)
    {
        remap[sortedVertices[static_cast<std::size_t>(i)]] = i + 1;
    }

    std::ofstream file(relPath);
    if (!file.is_open())
    {
        if (outError != nullptr)
        {
            *outError = "Failed to open mesh file for writing: " + relPath.string();
        }
        return false;
    }

    file << "# Generated by Asym Horror LevelEditor\n";
    for (int idx : sortedVertices)
    {
        const glm::vec3 p = m_meshModelVertices[static_cast<std::size_t>(idx)].position;
        const glm::vec3 world = m_meshModelPosition + p * m_meshModelScale;
        file << "v " << world.x << " " << world.y << " " << world.z << "\n";
    }

    for (const MeshModelFace& face : m_meshModelFaces)
    {
        if (face.deleted)
        {
            continue;
        }
        const int count = std::clamp(face.vertexCount, 3, 4);
        const int i0 = face.indices[0];
        const int i1 = face.indices[1];
        const int i2 = face.indices[2];
        const int i3 = face.indices[3];
        if (!remap.contains(i0) || !remap.contains(i1) || !remap.contains(i2))
        {
            continue;
        }
        file << "f " << remap[i0] << " " << remap[i1] << " " << remap[i2] << "\n";
        if (count == 4 && remap.contains(i3))
        {
            file << "f " << remap[i0] << " " << remap[i2] << " " << remap[i3] << "\n";
        }
    }

    if (outRelativePath != nullptr)
    {
        *outRelativePath = relPath.generic_string();
    }
    return true;
}

void LevelEditor::RenderMeshModeler(engine::render::Renderer& renderer) const
{
    if (m_meshModelFaces.empty() || m_meshModelVertices.empty())
    {
        return;
    }

    engine::render::MeshGeometry geometry;
    geometry.positions.reserve(m_meshModelFaces.size() * 6U);
    geometry.normals.reserve(m_meshModelFaces.size() * 6U);
    geometry.indices.reserve(m_meshModelFaces.size() * 6U);

    std::uint32_t idx = 0;
    for (std::size_t faceIndex = 0; faceIndex < m_meshModelFaces.size(); ++faceIndex)
    {
        const MeshModelFace& face = m_meshModelFaces[faceIndex];
        if (face.deleted)
        {
            continue;
        }
        const int count = std::clamp(face.vertexCount, 3, 4);

        const int i0 = face.indices[0];
        const int i1 = face.indices[1];
        const int i2 = face.indices[2];
        const int i3 = face.indices[3];
        if (i0 < 0 || i1 < 0 || i2 < 0 ||
            i0 >= static_cast<int>(m_meshModelVertices.size()) ||
            i1 >= static_cast<int>(m_meshModelVertices.size()) ||
            i2 >= static_cast<int>(m_meshModelVertices.size()))
        {
            continue;
        }
        if (m_meshModelVertices[static_cast<std::size_t>(i0)].deleted ||
            m_meshModelVertices[static_cast<std::size_t>(i1)].deleted ||
            m_meshModelVertices[static_cast<std::size_t>(i2)].deleted)
        {
            continue;
        }
        if (count == 4 &&
            (i3 < 0 || i3 >= static_cast<int>(m_meshModelVertices.size()) ||
             m_meshModelVertices[static_cast<std::size_t>(i3)].deleted))
        {
            continue;
        }

        const glm::vec3 p0 = m_meshModelPosition + m_meshModelVertices[static_cast<std::size_t>(i0)].position * m_meshModelScale;
        const glm::vec3 p1 = m_meshModelPosition + m_meshModelVertices[static_cast<std::size_t>(i1)].position * m_meshModelScale;
        const glm::vec3 p2 = m_meshModelPosition + m_meshModelVertices[static_cast<std::size_t>(i2)].position * m_meshModelScale;
        const glm::vec3 p3 = count == 4
                                 ? (m_meshModelPosition + m_meshModelVertices[static_cast<std::size_t>(i3)].position * m_meshModelScale)
                                 : p2;
        glm::vec3 normal = glm::cross(p1 - p0, p2 - p0);
        if (glm::length(normal) <= 1.0e-7F)
        {
            normal = glm::vec3{0.0F, 1.0F, 0.0F};
        }
        else
        {
            normal = glm::normalize(normal);
        }

        geometry.positions.push_back(p0);
        geometry.positions.push_back(p1);
        geometry.positions.push_back(p2);
        for (int i = 0; i < 3; ++i)
        {
            geometry.normals.push_back(normal);
            geometry.indices.push_back(idx++);
        }
        if (count == 4)
        {
            geometry.positions.push_back(p0);
            geometry.positions.push_back(p2);
            geometry.positions.push_back(p3);
            for (int i = 0; i < 3; ++i)
            {
                geometry.normals.push_back(normal);
                geometry.indices.push_back(idx++);
            }
        }
    }

    if (!geometry.positions.empty())
    {
        renderer.DrawMesh(
            geometry,
            glm::vec3{0.0F},
            glm::vec3{0.0F},
            glm::vec3{1.0F},
            glm::vec3{0.42F, 0.62F, 0.9F}
        );
    }

    const std::vector<std::array<int, 2>> edges = BuildMeshModelEdges();
    const std::unordered_set<int> loopEdges(
        m_meshModelLoopSelectionEdges.begin(),
        m_meshModelLoopSelectionEdges.end()
    );
    const std::unordered_set<int> ringEdges(
        m_meshModelRingSelectionEdges.begin(),
        m_meshModelRingSelectionEdges.end()
    );
    const std::vector<int> activeEditableEdges = CollectMeshModelActiveEdges();
    const std::unordered_set<int> activeEditableEdgeSet(activeEditableEdges.begin(), activeEditableEdges.end());
    for (int i = 0; i < static_cast<int>(edges.size()); ++i)
    {
        const auto& edge = edges[static_cast<std::size_t>(i)];
        if (edge[0] < 0 || edge[1] < 0 ||
            edge[0] >= static_cast<int>(m_meshModelVertices.size()) ||
            edge[1] >= static_cast<int>(m_meshModelVertices.size()))
        {
            continue;
        }
        const glm::vec3 p0 = m_meshModelPosition + m_meshModelVertices[static_cast<std::size_t>(edge[0])].position * m_meshModelScale;
        const glm::vec3 p1 = m_meshModelPosition + m_meshModelVertices[static_cast<std::size_t>(edge[1])].position * m_meshModelScale;
        const bool selected = m_meshEditMode == MeshEditMode::Edge && i == m_meshModelSelectedEdge;
        const bool hovered = m_meshEditMode == MeshEditMode::Edge && i == m_meshModelHoveredEdge;
        const bool inLoopSelection = loopEdges.contains(i);
        const bool inRingSelection = ringEdges.contains(i);
        const bool bridgeA = i == m_meshModelBridgeEdgeA;
        const bool bridgeB = i == m_meshModelBridgeEdgeB;
        glm::vec3 color = selected ? glm::vec3{1.0F, 0.52F, 0.15F} : glm::vec3{0.3F, 0.85F, 1.0F};
        if (hovered && !selected)
        {
            color = glm::vec3{1.0F, 0.9F, 0.35F};
        }
        if (inLoopSelection)
        {
            color = glm::vec3{0.85F, 0.35F, 1.0F};
        }
        if (inRingSelection)
        {
            color = glm::vec3{0.3F, 1.0F, 0.65F};
        }
        if (bridgeA)
        {
            color = glm::vec3{1.0F, 0.15F, 0.85F};
        }
        if (bridgeB)
        {
            color = glm::vec3{0.25F, 1.0F, 0.8F};
        }
        renderer.DrawOverlayLine(p0, p1, color);
        if (selected || hovered)
        {
            renderer.DrawOverlayLine(p0 + glm::vec3{0.0F, 0.005F, 0.0F}, p1 + glm::vec3{0.0F, 0.005F, 0.0F}, color);
        }
        if (activeEditableEdgeSet.contains(i))
        {
            const glm::vec3 hi = glm::vec3{1.0F, 0.95F, 0.25F};
            renderer.DrawOverlayLine(
                p0 + glm::vec3{0.0F, 0.012F, 0.0F},
                p1 + glm::vec3{0.0F, 0.012F, 0.0F},
                hi
            );
            const glm::vec3 mid = (p0 + p1) * 0.5F;
            renderer.DrawBox(mid, glm::vec3{0.025F}, hi);
        }
    }

    if (!activeEditableEdges.empty() && m_meshEditMode == MeshEditMode::Edge)
    {
        auto computeEdgePreviewDirection = [&](int edgeIndex) {
            glm::vec3 averageNormal{0.0F};
            int adjacentFaces = 0;
            if (edgeIndex < 0 || edgeIndex >= static_cast<int>(edges.size()))
            {
                return glm::vec3{0.0F};
            }
            const int i0 = edges[static_cast<std::size_t>(edgeIndex)][0];
            const int i1 = edges[static_cast<std::size_t>(edgeIndex)][1];
            for (const MeshModelFace& face : m_meshModelFaces)
            {
                if (face.deleted)
                {
                    continue;
                }
                bool hasI0 = false;
                bool hasI1 = false;
                const int count = std::clamp(face.vertexCount, 3, 4);
                for (int i = 0; i < count; ++i)
                {
                    const int idx = face.indices[static_cast<std::size_t>(i)];
                    hasI0 = hasI0 || idx == i0;
                    hasI1 = hasI1 || idx == i1;
                }
                if (!hasI0 || !hasI1)
                {
                    continue;
                }
                const glm::vec3 p0 = m_meshModelVertices[static_cast<std::size_t>(face.indices[0])].position;
                const glm::vec3 p1 = m_meshModelVertices[static_cast<std::size_t>(face.indices[1])].position;
                const glm::vec3 p2 = m_meshModelVertices[static_cast<std::size_t>(face.indices[2])].position;
                glm::vec3 n = glm::cross(p1 - p0, p2 - p0);
                if (glm::length(n) > 1.0e-6F)
                {
                    averageNormal += glm::normalize(n);
                    ++adjacentFaces;
                }
            }
            if (adjacentFaces == 0 || glm::length(averageNormal) < 1.0e-6F)
            {
                return glm::vec3{0.0F, 1.0F, 0.0F};
            }
            return glm::normalize(averageNormal);
        };

        const float previewDistance = glm::max(0.0F, m_meshModelBatchPreviewDistance);
        const glm::vec3 previewColor =
            m_meshModelBatchOperation == MeshBatchEdgeOperation::Extrude
                ? glm::vec3{1.0F, 0.6F, 0.2F}
                : glm::vec3{0.2F, 0.85F, 1.0F};

        if (previewDistance > 1.0e-4F)
        {
            for (const int edgeIndex : activeEditableEdges)
            {
                if (edgeIndex < 0 || edgeIndex >= static_cast<int>(edges.size()))
                {
                    continue;
                }
                const auto edge = edges[static_cast<std::size_t>(edgeIndex)];
                const glm::vec3 p0 = m_meshModelPosition +
                                     m_meshModelVertices[static_cast<std::size_t>(edge[0])].position * m_meshModelScale;
                const glm::vec3 p1 = m_meshModelPosition +
                                     m_meshModelVertices[static_cast<std::size_t>(edge[1])].position * m_meshModelScale;
                const glm::vec3 direction = computeEdgePreviewDirection(edgeIndex);
                const glm::vec3 offset = direction * previewDistance;
                renderer.DrawOverlayLine(p0 + offset, p1 + offset, previewColor);
                if (m_meshModelBatchOperation == MeshBatchEdgeOperation::Bevel)
                {
                    renderer.DrawOverlayLine(p0 - offset, p1 - offset, previewColor * 0.8F);
                }
            }
        }
    }

    const int faceToOutline =
        (m_meshEditMode == MeshEditMode::Face && m_meshModelSelectedFace >= 0)
            ? m_meshModelSelectedFace
            : ((m_meshEditMode == MeshEditMode::Face && m_meshModelHoveredFace >= 0) ? m_meshModelHoveredFace : -1);
    if (faceToOutline >= 0 && faceToOutline < static_cast<int>(m_meshModelFaces.size()))
    {
        const MeshModelFace& face = m_meshModelFaces[static_cast<std::size_t>(faceToOutline)];
        if (!face.deleted)
        {
            const int count = std::clamp(face.vertexCount, 3, 4);
            const int i0 = face.indices[0];
            const int i1 = face.indices[1];
            const int i2 = face.indices[2];
            const int i3 = face.indices[3];
            if (i0 >= 0 && i1 >= 0 && i2 >= 0 &&
                i0 < static_cast<int>(m_meshModelVertices.size()) &&
                i1 < static_cast<int>(m_meshModelVertices.size()) &&
                i2 < static_cast<int>(m_meshModelVertices.size()) &&
                (count == 3 || (i3 >= 0 && i3 < static_cast<int>(m_meshModelVertices.size()))))
            {
                const glm::vec3 p0 = m_meshModelPosition + m_meshModelVertices[static_cast<std::size_t>(i0)].position * m_meshModelScale;
                const glm::vec3 p1 = m_meshModelPosition + m_meshModelVertices[static_cast<std::size_t>(i1)].position * m_meshModelScale;
                const glm::vec3 p2 = m_meshModelPosition + m_meshModelVertices[static_cast<std::size_t>(i2)].position * m_meshModelScale;
                const glm::vec3 p3 = count == 4
                                         ? (m_meshModelPosition + m_meshModelVertices[static_cast<std::size_t>(i3)].position * m_meshModelScale)
                                         : p0;
                const bool isSelectedFace =
                    faceToOutline == m_meshModelSelectedFace ||
                    std::find(m_meshModelFaceSelection.begin(), m_meshModelFaceSelection.end(), faceToOutline) !=
                        m_meshModelFaceSelection.end();
                const glm::vec3 c = isSelectedFace ? glm::vec3{1.0F, 0.45F, 0.2F} : glm::vec3{0.95F, 0.9F, 0.35F};
                renderer.DrawOverlayLine(p0, p1, c);
                renderer.DrawOverlayLine(p1, p2, c);
                if (count == 4)
                {
                    renderer.DrawOverlayLine(p2, p3, c);
                    renderer.DrawOverlayLine(p3, p0, c);
                }
                else
                {
                    renderer.DrawOverlayLine(p2, p0, c);
                }
            }
        }
    }

    for (std::size_t i = 0; i < m_meshModelVertices.size(); ++i)
    {
        const MeshModelVertex& vertex = m_meshModelVertices[i];
        if (vertex.deleted)
        {
            continue;
        }
        const glm::vec3 pos = m_meshModelPosition + vertex.position * m_meshModelScale;
        const bool selected = static_cast<int>(i) == m_meshModelSelectedVertex ||
                              std::find(m_meshModelVertexSelection.begin(), m_meshModelVertexSelection.end(), static_cast<int>(i)) !=
                                  m_meshModelVertexSelection.end();
        const bool hovered = m_meshEditMode == MeshEditMode::Vertex && static_cast<int>(i) == m_meshModelHoveredVertex;
        const glm::vec3 color = selected
                                    ? glm::vec3{1.0F, 0.82F, 0.2F}
                                    : (hovered ? glm::vec3{0.95F, 0.9F, 0.35F} : glm::vec3{0.2F, 1.0F, 0.3F});
        const float size = selected ? 0.095F : (hovered ? 0.08F : 0.06F);
        renderer.DrawBox(pos, glm::vec3{size}, color);
    }

    if (m_meshModelKnifeHasFirstPoint)
    {
        renderer.DrawBox(m_meshModelKnifeFirstPointWorld, glm::vec3{0.09F}, glm::vec3{1.0F, 0.25F, 0.25F});
        if (m_meshModelKnifePreviewValid)
        {
            if (!m_meshModelKnifePreviewSegments.empty())
            {
                for (const auto& segment : m_meshModelKnifePreviewSegments)
                {
                    renderer.DrawOverlayLine(segment.first, segment.second, glm::vec3{1.0F, 0.35F, 0.35F});
                }
            }
            else
            {
                renderer.DrawOverlayLine(
                    m_meshModelKnifeFirstPointWorld,
                    m_meshModelKnifePreviewWorld,
                    glm::vec3{1.0F, 0.35F, 0.35F}
                );
            }
        }
    }

    auto hasMeshSelection = [&]() {
        if (m_meshEditMode == MeshEditMode::Vertex)
        {
            return m_meshModelSelectedVertex >= 0 && m_meshModelSelectedVertex < static_cast<int>(m_meshModelVertices.size());
        }
        if (m_meshEditMode == MeshEditMode::Edge)
        {
            return m_meshModelSelectedEdge >= 0 && m_meshModelSelectedEdge < static_cast<int>(edges.size());
        }
        return m_meshModelSelectedFace >= 0 && m_meshModelSelectedFace < static_cast<int>(m_meshModelFaces.size());
    };

    if (m_meshModelShowGizmo && hasMeshSelection())
    {
        const glm::vec3 pivot = MeshModelSelectionPivot();
        const float cameraDistance = glm::length(m_cameraPosition - pivot);
        const float axisLength = glm::clamp(cameraDistance * 0.16F, 1.2F, 6.0F);
        const float boxHalf = glm::max(0.08F, axisLength * 0.06F);

        auto axisColor = [&](GizmoAxis axis) {
            const bool active = m_meshModelAxisDragActive && m_meshModelAxisDragAxis == axis;
            if (axis == GizmoAxis::X)
            {
                return active ? glm::vec3{1.0F, 1.0F, 0.2F} : glm::vec3{1.0F, 0.3F, 0.3F};
            }
            if (axis == GizmoAxis::Y)
            {
                return active ? glm::vec3{1.0F, 1.0F, 0.2F} : glm::vec3{0.3F, 1.0F, 0.3F};
            }
            return active ? glm::vec3{1.0F, 1.0F, 0.2F} : glm::vec3{0.3F, 0.6F, 1.0F};
        };

        const glm::vec3 xTip = pivot + glm::vec3{axisLength, 0.0F, 0.0F};
        const glm::vec3 yTip = pivot + glm::vec3{0.0F, axisLength, 0.0F};
        const glm::vec3 zTip = pivot + glm::vec3{0.0F, 0.0F, axisLength};

        renderer.DrawOverlayLine(pivot, xTip, axisColor(GizmoAxis::X));
        renderer.DrawOverlayLine(pivot, yTip, axisColor(GizmoAxis::Y));
        renderer.DrawOverlayLine(pivot, zTip, axisColor(GizmoAxis::Z));
        renderer.DrawBox(xTip, glm::vec3{boxHalf}, axisColor(GizmoAxis::X));
        renderer.DrawBox(yTip, glm::vec3{boxHalf}, axisColor(GizmoAxis::Y));
        renderer.DrawBox(zTip, glm::vec3{boxHalf}, axisColor(GizmoAxis::Z));
    }

    if (m_meshEditMode == MeshEditMode::Edge && m_meshModelBatchGizmoEnabled)
    {
        glm::vec3 batchPivot{0.0F};
        glm::vec3 batchDirection{0.0F, 1.0F, 0.0F};
        glm::vec3 batchPlaneNormal{1.0F, 0.0F, 0.0F};
        float batchAxisLength = 0.0F;
        if (ComputeMeshBatchEdgeGizmo(&batchPivot, &batchDirection, &batchPlaneNormal, &batchAxisLength))
        {
            (void)batchPlaneNormal;
            const glm::vec3 tip = batchPivot + batchDirection * batchAxisLength;
            const glm::vec3 previewTip = batchPivot + batchDirection * glm::max(0.01F, m_meshModelBatchPreviewDistance);
            const glm::vec3 baseColor = m_meshModelBatchDragActive
                                            ? glm::vec3{1.0F, 0.9F, 0.2F}
                                            : glm::vec3{1.0F, 0.75F, 0.25F};
            renderer.DrawOverlayLine(batchPivot, tip, baseColor);
            renderer.DrawBox(tip, glm::vec3{glm::max(0.08F, batchAxisLength * 0.08F)}, baseColor);
            renderer.DrawOverlayLine(batchPivot, previewTip, glm::vec3{0.9F, 0.95F, 0.35F});
            renderer.DrawBox(previewTip, glm::vec3{0.04F}, glm::vec3{0.9F, 0.95F, 0.35F});
        }
    }
}

std::string LevelEditor::SelectedLabel() const
{
    switch (m_selection.kind)
    {
        case SelectionKind::None: return "None";
        case SelectionKind::LoopElement:
            if (m_selectedLoopElements.size() > 1)
            {
                return "Loop elements (" + std::to_string(m_selectedLoopElements.size()) + ")";
            }
            return "Loop element #" + std::to_string(m_selection.index);
        case SelectionKind::MapPlacement:
            if (m_selectedMapPlacements.size() > 1)
            {
                return "Placements (" + std::to_string(m_selectedMapPlacements.size()) + ")";
            }
            return "Placement #" + std::to_string(m_selection.index);
        case SelectionKind::Prop:
            if (m_selectedProps.size() > 1)
            {
                return "Props (" + std::to_string(m_selectedProps.size()) + ")";
            }
            return "Prop #" + std::to_string(m_selection.index);
        default: return "None";
    }
}

std::string LevelEditor::SceneDump() const
{
    std::ostringstream oss;
    oss << "EditorSceneDump\n";
    oss << " mode=" << ModeToText(m_mode)
        << " object_mode=" << (m_meshModelSceneEditEnabled ? "edit" : "object")
        << " workspace=" << (m_uiWorkspace == UiWorkspace::All
                                  ? "all"
                                  : (m_uiWorkspace == UiWorkspace::Mesh
                                         ? "mesh"
                                         : (m_uiWorkspace == UiWorkspace::Map
                                                ? "map"
                                                : (m_uiWorkspace == UiWorkspace::Lighting ? "lighting" : "fx"))))
        << "\n";
    oss << " active_mesh=" << m_meshActiveObjectName
        << " verts=" << m_meshModelVertices.size()
        << " faces=" << m_meshModelFaces.size()
        << " pos=(" << m_meshModelPosition.x << "," << m_meshModelPosition.y << "," << m_meshModelPosition.z << ")\n";
    oss << " map placements=" << m_map.placements.size()
        << " props=" << m_map.props.size()
        << " lights=" << m_map.lights.size() << "\n";
    return oss.str();
}

void LevelEditor::Update(
    float deltaSeconds,
    const engine::platform::Input& input,
    bool controlsEnabled,
    int framebufferWidth,
    int framebufferHeight
)
{
    m_lastFramebufferWidth = std::max(1, framebufferWidth);
    m_lastFramebufferHeight = std::max(1, framebufferHeight);
    m_meshObjectMode = m_meshModelSceneEditEnabled ? MeshObjectMode::Edit : MeshObjectMode::Object;
    m_materialLabElapsed += glm::max(0.0F, deltaSeconds);

    if (m_contentNeedsRefresh)
    {
        RefreshContentBrowser();
    }

    if (m_animationPreviewPlaying)
    {
        float speed = 1.0F;
        if (m_selection.kind == SelectionKind::Prop &&
            m_selection.index >= 0 &&
            m_selection.index < static_cast<int>(m_map.props.size()))
        {
            speed = std::max(0.01F, m_map.props[static_cast<std::size_t>(m_selection.index)].animationSpeed);
        }
        m_animationPreviewTime += deltaSeconds * speed;
    }

    HandleCamera(deltaSeconds, input, controlsEnabled);
    HandleEditorHotkeys(input, controlsEnabled);
    if (m_modalTransform.active)
    {
        UpdateModalTransform(input, deltaSeconds);
        if (input.IsKeyPressed(GLFW_KEY_ESCAPE) || input.IsMousePressed(GLFW_MOUSE_BUTTON_RIGHT))
        {
            CancelModalTransform();
            return;
        }
        if (input.IsKeyPressed(GLFW_KEY_ENTER) || input.IsMousePressed(GLFW_MOUSE_BUTTON_LEFT))
        {
            ConfirmModalTransform();
            return;
        }
    }
    else
    {
        ApplyGizmoInput(input, deltaSeconds);
    }
    UpdateHoveredTile(input, framebufferWidth, framebufferHeight);
    m_fxPreviewSystem.Update(deltaSeconds, m_cameraPosition);
    if (m_axisDragActive && input.IsMouseReleased(GLFW_MOUSE_BUTTON_LEFT))
    {
        StopAxisDrag();
    }
    if (m_meshModelAxisDragActive && input.IsMouseReleased(GLFW_MOUSE_BUTTON_LEFT))
    {
        StopMeshAxisDrag();
    }
    if (m_meshModelBatchDragActive && input.IsMouseReleased(GLFW_MOUSE_BUTTON_LEFT))
    {
        StopMeshBatchEdgeDrag();
    }

    if (!controlsEnabled)
    {
        return;
    }

    const glm::vec2 mousePos = input.MousePosition();
    const bool mouseInSceneRect =
        mousePos.x >= m_sceneViewportRectMin.x && mousePos.x <= m_sceneViewportRectMax.x &&
        mousePos.y >= m_sceneViewportRectMin.y && mousePos.y <= m_sceneViewportRectMax.y;
    const bool sceneMouseActive = mouseInSceneRect && (m_sceneViewportHovered || m_sceneViewportFocused);

    if (m_mode == Mode::LoopEditor)
    {
        SpawnQueuedMeshPrimitive(input);
    }

#if BUILD_WITH_IMGUI
    if (ImGui::GetIO().WantCaptureMouse && !sceneMouseActive)
    {
        return;
    }
#endif

    const bool ctrlDown = input.IsKeyDown(GLFW_KEY_LEFT_CONTROL) || input.IsKeyDown(GLFW_KEY_RIGHT_CONTROL);
    const bool shiftDown = input.IsKeyDown(GLFW_KEY_LEFT_SHIFT) || input.IsKeyDown(GLFW_KEY_RIGHT_SHIFT);
    const bool altDown = input.IsKeyDown(GLFW_KEY_LEFT_ALT) || input.IsKeyDown(GLFW_KEY_RIGHT_ALT);

    if (m_mode == Mode::LoopEditor)
    {
        if (sceneMouseActive && shiftDown && input.IsMousePressed(GLFW_MOUSE_BUTTON_RIGHT) && !ctrlDown)
        {
            UpdateMeshCursorFromMouse(input, framebufferWidth, framebufferHeight);
        }

        if (m_meshClickPlacementPending)
        {
            m_meshClickPreviewPosition = ComputeMeshSpawnPosition(static_cast<int>(MeshSpawnMode::ClickPlace), input);
            if (input.IsKeyPressed(GLFW_KEY_ESCAPE))
            {
                m_meshClickPlacementPending = false;
                m_clickPlacePrimitive = MeshPrimitiveType::None;
                m_statusLine = "Click placement canceled";
                return;
            }
            if (sceneMouseActive && input.IsMousePressed(GLFW_MOUSE_BUTTON_LEFT))
            {
                SpawnMeshPrimitiveNow(
                    static_cast<int>(m_clickPlacePrimitive),
                    m_meshClickPreviewPosition,
                    "click place"
                );
                m_meshClickPlacementPending = false;
                m_clickPlacePrimitive = MeshPrimitiveType::None;
                return;
            }
        }
    }

    SelectionCombineMode runtimeSelectionMode = m_selectionCombineMode;
    if (shiftDown && altDown)
    {
        runtimeSelectionMode = SelectionCombineMode::Intersect;
    }
    else if (altDown)
    {
        runtimeSelectionMode = SelectionCombineMode::Subtract;
    }
    else if (shiftDown)
    {
        runtimeSelectionMode = SelectionCombineMode::Add;
    }

    if (sceneMouseActive && ctrlDown && input.IsMousePressed(GLFW_MOUSE_BUTTON_RIGHT) &&
        !m_lassoSelection.active)
    {
        m_lassoSelection.active = true;
        m_lassoSelection.points.clear();
        m_lassoSelection.points.push_back(input.MousePosition());
        m_statusLine = "Lasso select active";
    }
    if (m_lassoSelection.active)
    {
        if (input.IsMouseDown(GLFW_MOUSE_BUTTON_RIGHT))
        {
            const glm::vec2 p = input.MousePosition();
            if (m_lassoSelection.points.empty() || glm::length(p - m_lassoSelection.points.back()) > 2.0F)
            {
                m_lassoSelection.points.push_back(p);
            }
        }
        else
        {
            ApplyLassoSelection(runtimeSelectionMode);
            m_lassoSelection.active = false;
            m_lassoSelection.points.clear();
            m_statusLine = "Lasso selection applied";
        }
        return;
    }
    if (m_boxSelection.active)
    {
        if (input.IsKeyPressed(GLFW_KEY_ESCAPE))
        {
            m_boxSelection = BoxSelectionState{};
            m_statusLine = "Box select canceled";
            return;
        }
        if (input.IsMouseDown(GLFW_MOUSE_BUTTON_LEFT))
        {
            m_boxSelection.dragging = true;
            m_boxSelection.end = input.MousePosition();
        }
        if (input.IsMouseReleased(GLFW_MOUSE_BUTTON_LEFT))
        {
            if (m_boxSelection.dragging)
            {
                m_boxSelection.end = input.MousePosition();
                ApplyBoxSelection(runtimeSelectionMode);
                m_statusLine = "Box selection applied";
            }
            m_boxSelection = BoxSelectionState{};
        }
        return;
    }

    if (!sceneMouseActive)
    {
        m_meshModelHoveredFace = -1;
        m_meshModelHoveredEdge = -1;
        m_meshModelHoveredVertex = -1;
        return;
    }

    glm::vec3 rayOrigin{0.0F};
    glm::vec3 rayDirection{0.0F};
    if (!BuildMouseRay(input, framebufferWidth, framebufferHeight, &rayOrigin, &rayDirection))
    {
        m_meshModelHoveredFace = -1;
        m_meshModelHoveredEdge = -1;
        m_meshModelHoveredVertex = -1;
        return;
    }

    if (m_mode == Mode::LoopEditor && m_meshModelSceneEditEnabled && sceneMouseActive)
    {
        UpdateMeshHover(rayOrigin, rayDirection);
    }
    else
    {
        m_meshModelHoveredFace = -1;
        m_meshModelHoveredEdge = -1;
        m_meshModelHoveredVertex = -1;
    }

    if (m_axisDragActive)
    {
        if (input.IsKeyPressed(GLFW_KEY_ESCAPE) || input.IsMousePressed(GLFW_MOUSE_BUTTON_RIGHT))
        {
            StopAxisDrag();
            Undo();
            m_statusLine = "Transform canceled";
            return;
        }
        if (input.IsMouseDown(GLFW_MOUSE_BUTTON_LEFT))
        {
            UpdateAxisDrag(rayOrigin, rayDirection);
        }
        return;
    }
    if (m_meshModelAxisDragActive)
    {
        if (input.IsKeyPressed(GLFW_KEY_ESCAPE) || input.IsMousePressed(GLFW_MOUSE_BUTTON_RIGHT))
        {
            StopMeshAxisDrag();
            Undo();
            m_statusLine = "Mesh transform canceled";
            return;
        }
        if (input.IsMouseDown(GLFW_MOUSE_BUTTON_LEFT))
        {
            UpdateMeshAxisDrag(rayOrigin, rayDirection);
        }
        return;
    }
    if (m_meshModelBatchDragActive)
    {
        if (input.IsKeyPressed(GLFW_KEY_ESCAPE) || input.IsMousePressed(GLFW_MOUSE_BUTTON_RIGHT))
        {
            StopMeshBatchEdgeDrag();
            m_statusLine = "Batch drag canceled";
            return;
        }
        if (input.IsMouseDown(GLFW_MOUSE_BUTTON_LEFT))
        {
            UpdateMeshBatchEdgeDrag(rayOrigin, rayDirection);
        }
        return;
    }

    if (input.IsMousePressed(GLFW_MOUSE_BUTTON_LEFT))
    {
        const bool shiftSelect = input.IsKeyDown(GLFW_KEY_LEFT_SHIFT) || input.IsKeyDown(GLFW_KEY_RIGHT_SHIFT);
        const bool altSelect = input.IsKeyDown(GLFW_KEY_LEFT_ALT) || input.IsKeyDown(GLFW_KEY_RIGHT_ALT);
#if BUILD_WITH_IMGUI
        const bool doubleClickSelect = ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
#else
        const bool doubleClickSelect = false;
#endif
        if (m_mode == Mode::LoopEditor && m_meshModelSceneEditEnabled)
        {
            if (m_meshModelLoopCutToolEnabled)
            {
                const MeshEditMode previousMode = m_meshEditMode;
                m_meshEditMode = MeshEditMode::Edge;
                const bool pickedEdge = PickMeshModelInScene(rayOrigin, rayDirection);
                m_meshEditMode = previousMode;
                if (pickedEdge && m_meshModelSelectedEdge >= 0)
                {
                    PushHistorySnapshot();
                    MeshModelerLoopCutEdge(m_meshModelSelectedEdge, m_meshModelLoopCutRatio);
                    m_statusLine = "Loop cut applied from scene pick";
                    return;
                }
            }
            if (m_meshModelKnifeEnabled)
            {
                if (HandleMeshKnifeClick(rayOrigin, rayDirection))
                {
                    return;
                }
            }
            if (StartMeshBatchEdgeDrag(rayOrigin, rayDirection))
            {
                m_statusLine = "Batch edge gizmo drag started";
                return;
            }
            if (StartMeshAxisDrag(rayOrigin, rayDirection))
            {
                PushHistorySnapshot();
                m_statusLine = "Mesh gizmo drag started";
                return;
            }
            if ((altSelect || doubleClickSelect) && m_meshEditMode == MeshEditMode::Edge && m_meshModelHoveredEdge >= 0)
            {
                if (shiftSelect)
                {
                    MeshModelerSelectEdgeRing(m_meshModelHoveredEdge);
                    m_statusLine = "Edge ring selected";
                }
                else
                {
                    MeshModelerSelectEdgeLoop(m_meshModelHoveredEdge);
                    m_statusLine = "Edge loop selected";
                }
                return;
            }
            if (PickMeshModelInScene(rayOrigin, rayDirection))
            {
                if (m_meshEditMode == MeshEditMode::Vertex)
                {
                    auto current = m_meshModelVertexSelection;
                    if (current.empty() && m_meshModelSelectedVertex >= 0)
                    {
                        current.push_back(m_meshModelSelectedVertex);
                    }
                    if (shiftSelect && m_meshModelHoveredVertex >= 0)
                    {
                        const auto it = std::find(current.begin(), current.end(), m_meshModelHoveredVertex);
                        if (it != current.end())
                        {
                            current.erase(it);
                        }
                        else
                        {
                            current.push_back(m_meshModelHoveredVertex);
                        }
                    }
                    else
                    {
                        current.clear();
                        if (m_meshModelSelectedVertex >= 0)
                        {
                            current.push_back(m_meshModelSelectedVertex);
                        }
                    }
                    std::sort(current.begin(), current.end());
                    current.erase(std::unique(current.begin(), current.end()), current.end());
                    m_meshModelVertexSelection = current;
                    m_meshModelSelectedVertex = current.empty() ? -1 : current.back();
                }
                else if (m_meshEditMode == MeshEditMode::Edge)
                {
                    if (shiftSelect && m_meshModelHoveredEdge >= 0)
                    {
                        auto toggleEdge = [this](int edgeIndex) {
                            auto it = std::find(m_meshModelLoopSelectionEdges.begin(), m_meshModelLoopSelectionEdges.end(), edgeIndex);
                            if (it != m_meshModelLoopSelectionEdges.end())
                            {
                                m_meshModelLoopSelectionEdges.erase(it);
                            }
                            else
                            {
                                m_meshModelLoopSelectionEdges.push_back(edgeIndex);
                            }
                        };
                        toggleEdge(m_meshModelHoveredEdge);
                    }
                    else
                    {
                        m_meshModelLoopSelectionEdges.clear();
                        m_meshModelRingSelectionEdges.clear();
                    }
                }
                else
                {
                    auto current = m_meshModelFaceSelection;
                    if (current.empty() && m_meshModelSelectedFace >= 0)
                    {
                        current.push_back(m_meshModelSelectedFace);
                    }
                    if (shiftSelect && m_meshModelHoveredFace >= 0)
                    {
                        const auto it = std::find(current.begin(), current.end(), m_meshModelHoveredFace);
                        if (it != current.end())
                        {
                            current.erase(it);
                        }
                        else
                        {
                            current.push_back(m_meshModelHoveredFace);
                        }
                    }
                    else
                    {
                        current.clear();
                        if (m_meshModelSelectedFace >= 0)
                        {
                            current.push_back(m_meshModelSelectedFace);
                        }
                    }
                    std::sort(current.begin(), current.end());
                    current.erase(std::unique(current.begin(), current.end()), current.end());
                    m_meshModelFaceSelection = current;
                    m_meshModelSelectedFace = current.empty() ? -1 : current.back();
                    if (!shiftSelect)
                    {
                        m_meshModelLoopSelectionEdges.clear();
                        m_meshModelRingSelectionEdges.clear();
                    }
                }
                if (m_meshEditMode == MeshEditMode::Vertex)
                {
                    m_statusLine = "Mesh vertex selected: " + std::to_string(m_meshModelSelectedVertex);
                }
                else if (m_meshEditMode == MeshEditMode::Edge)
                {
                    m_statusLine = "Mesh edge selected: " + std::to_string(m_meshModelSelectedEdge);
                }
                else
                {
                    m_statusLine = "Mesh face selected: " + std::to_string(m_meshModelSelectedFace);
                }
                return;
            }
        }

        if (m_mode == Mode::MapEditor && m_lightPlacementMode)
        {
            AddLightAtHovered(m_lightPlacementType);
            return;
        }

        if (StartAxisDrag(rayOrigin, rayDirection))
        {
            return;
        }

        const bool ctrlDown = input.IsKeyDown(GLFW_KEY_LEFT_CONTROL) || input.IsKeyDown(GLFW_KEY_RIGHT_CONTROL);
        const bool additiveSelect = ctrlDown || shiftSelect;
        int pickedLightIndex = -1;
        float pickedLightT = 1.0e9F;
        if (m_mode == Mode::MapEditor)
        {
            for (int i = 0; i < static_cast<int>(m_map.lights.size()); ++i)
            {
                const LightInstance& light = m_map.lights[static_cast<std::size_t>(i)];
                const glm::vec3 extents = light.type == LightType::Spot ? glm::vec3{0.28F, 0.28F, 0.28F} : glm::vec3{0.24F, 0.24F, 0.24F};
                float t = 0.0F;
                if (!SegmentIntersectsAabb(rayOrigin, rayDirection, light.position - extents, light.position + extents, &t))
                {
                    continue;
                }
                if (t < pickedLightT)
                {
                    pickedLightT = t;
                    pickedLightIndex = i;
                }
            }
        }
        if (pickedLightIndex >= 0)
        {
            if (!additiveSelect)
            {
                ClearSelections();
            }
            m_selectedLightIndex = pickedLightIndex;
            m_statusLine = "Selected light " + m_map.lights[static_cast<std::size_t>(pickedLightIndex)].name;
            return;
        }

        const Selection selection = PickSelection(rayOrigin, rayDirection);
        if (selection.kind != SelectionKind::None)
        {
            m_selectedLightIndex = -1;
            if (additiveSelect)
            {
                ToggleSelection(selection);
            }
            else
            {
                SelectSingle(selection);
            }
            return;
        }

        if (!additiveSelect)
        {
            ClearSelections();
            m_selectedLightIndex = -1;
        }

        if (m_mode == Mode::MapEditor)
        {
            if (m_propPlacementMode)
            {
                AddPropAtHoveredTile();
            }
            else
            {
                PlaceLoopAtHoveredTile();
            }
        }
    }

    if (input.IsMousePressed(GLFW_MOUSE_BUTTON_RIGHT) && m_mode == Mode::MapEditor)
    {
        RemovePlacementAtHoveredTile();
    }
}

void LevelEditor::Render(engine::render::Renderer& renderer) const
{
    const bool materialLabVisible = m_materialLabViewMode != MaterialLabViewMode::Off;
    const bool materialLabOverlay = m_materialLabViewMode == MaterialLabViewMode::Overlay;
    const bool materialLabDedicated = m_materialLabViewMode == MaterialLabViewMode::Dedicated;
    const bool loopMode = m_mode == Mode::LoopEditor && !materialLabDedicated;
    const glm::vec3 previewForward = CameraForward();
    const float previewYawRadians =
        m_materialLabAutoRotate ? glm::radians(m_materialLabElapsed * m_materialLabAutoRotateSpeed) : glm::radians(m_materialLabManualYaw);
    const float previewYawDegrees =
        m_materialLabAutoRotate ? (m_materialLabElapsed * m_materialLabAutoRotateSpeed) : m_materialLabManualYaw;
    const glm::vec3 orbitOffset = materialLabDedicated
                                      ? glm::vec3{0.0F, 0.0F, 0.0F}
                                      : glm::vec3{
                                            std::cos(previewYawRadians) * m_materialLabDistance,
                                            m_materialLabHeight,
                                            std::sin(previewYawRadians) * m_materialLabDistance,
                                        };
    const glm::vec3 previewCenter = materialLabDedicated
                                        ? glm::vec3{0.0F, m_materialLabSphereRadius + 0.8F, 0.0F}
                                        : (m_cameraPosition + previewForward * 5.6F + orbitOffset);
    const glm::vec3 previewFloorCenter = previewCenter + glm::vec3{0.0F, -m_materialLabSphereRadius - 0.28F, 0.0F};

    engine::render::EnvironmentSettings environment = m_environmentEditing.id.empty() ? CurrentEnvironmentSettings() : ToRenderEnvironment(m_environmentEditing);
    if (materialLabVisible && !m_materialLabDirectionalLightEnabled)
    {
        environment.directionalLightIntensity = 0.0F;
    }
    else if (materialLabVisible)
    {
        environment.directionalLightIntensity = glm::max(0.0F, m_materialLabDirectionalIntensity);
    }
    renderer.SetEnvironmentSettings(environment);

    auto applyMaterialLabPointLights = [&]() {
        std::vector<engine::render::PointLight> pointLights;
        std::vector<engine::render::SpotLight> spotLights;
        if (m_materialLabLightingEnabled && m_materialLabPointLightsEnabled)
        {
            pointLights.push_back(engine::render::PointLight{
                previewCenter + glm::vec3{1.8F, 1.3F, 0.9F},
                glm::vec3{1.0F, 0.95F, 0.9F},
                glm::max(0.0F, m_materialLabPointIntensity),
                glm::max(0.1F, m_materialLabPointRange),
            });
            pointLights.push_back(engine::render::PointLight{
                previewCenter + glm::vec3{-1.6F, 0.9F, -1.1F},
                glm::vec3{0.45F, 0.56F, 1.0F},
                glm::max(0.0F, m_materialLabPointIntensity * 0.6F),
                glm::max(0.1F, m_materialLabPointRange),
            });
        }
        renderer.SetPointLights(pointLights);
        renderer.SetSpotLights(spotLights);
    };

    if (materialLabDedicated)
    {
        applyMaterialLabPointLights();

        renderer.DrawGrid(18, 1.0F, glm::vec3{0.26F, 0.26F, 0.29F}, glm::vec3{0.12F, 0.12F, 0.14F});
        if (m_materialLabBackdropEnabled)
        {
            renderer.DrawOrientedBox(
                previewFloorCenter,
                glm::vec3{2.1F, 0.2F, 2.1F},
                glm::vec3{0.0F, 0.0F, 0.0F},
                glm::vec3{0.2F, 0.2F, 0.22F}
            );
            renderer.DrawOrientedBox(
                previewCenter + glm::vec3{0.0F, 0.95F, -2.25F},
                glm::vec3{2.2F, 1.2F, 0.12F},
                glm::vec3{0.0F, 0.0F, 0.0F},
                glm::vec3{0.16F, 0.16F, 0.19F}
            );
        }

        static const engine::render::MeshGeometry kMaterialLabSphere = BuildUvSphereGeometry(36, 64);
        const glm::vec3 previewColor = glm::vec3{
            m_materialEditing.baseColor.r,
            m_materialEditing.baseColor.g,
            m_materialEditing.baseColor.b,
        };
        const engine::render::MaterialParams previewMaterial = ToRenderMaterialParams(&m_materialEditing);
        renderer.DrawMesh(
            kMaterialLabSphere,
            previewCenter,
            glm::vec3{0.0F, previewYawDegrees, 0.0F},
            glm::vec3{m_materialLabSphereRadius},
            glm::clamp(previewColor, glm::vec3{0.0F}, glm::vec3{1.0F}),
            previewMaterial
        );
        if (m_debugView)
        {
            renderer.DrawOverlayLine(
                previewCenter,
                previewCenter + glm::vec3{0.0F, m_materialLabSphereRadius + 0.8F, 0.0F},
                glm::vec3{0.95F, 0.8F, 0.25F}
            );
        }
        return;
    }

    if (loopMode)
    {
        std::vector<engine::render::PointLight> pointLights;
        if (materialLabOverlay && m_materialLabLightingEnabled && m_materialLabPointLightsEnabled)
        {
            pointLights.push_back(engine::render::PointLight{
                previewCenter + glm::vec3{1.8F, 1.3F, 0.9F},
                glm::vec3{1.0F, 0.95F, 0.9F},
                glm::max(0.0F, m_materialLabPointIntensity),
                glm::max(0.1F, m_materialLabPointRange),
            });
            pointLights.push_back(engine::render::PointLight{
                previewCenter + glm::vec3{-1.6F, 0.9F, -1.1F},
                glm::vec3{0.45F, 0.56F, 1.0F},
                glm::max(0.0F, m_materialLabPointIntensity * 0.6F),
                glm::max(0.1F, m_materialLabPointRange),
            });
        }
        renderer.SetPointLights(pointLights);
        renderer.SetSpotLights({});
    }
    else
    {
        std::vector<engine::render::PointLight> pointLights;
        std::vector<engine::render::SpotLight> spotLights;
        pointLights.reserve(m_map.lights.size());
        spotLights.reserve(m_map.lights.size());

        for (const LightInstance& light : m_map.lights)
        {
            if (!light.enabled)
            {
                continue;
            }

            if (light.type == LightType::Spot)
            {
                const glm::mat3 rotation = RotationMatrixFromEulerDegrees(light.rotationEuler);
                const glm::vec3 dir = glm::normalize(rotation * glm::vec3{0.0F, 0.0F, -1.0F});
                const float inner = std::cos(glm::radians(glm::clamp(light.spotInnerAngle, 1.0F, 89.0F)));
                const float outer = std::cos(glm::radians(glm::clamp(light.spotOuterAngle, light.spotInnerAngle + 0.1F, 89.5F)));
                spotLights.push_back(engine::render::SpotLight{
                    light.position,
                    dir,
                    glm::clamp(light.color, glm::vec3{0.0F}, glm::vec3{10.0F}),
                    glm::max(0.0F, light.intensity),
                    glm::max(0.1F, light.range),
                    inner,
                    outer,
                });
            }
            else
            {
                pointLights.push_back(engine::render::PointLight{
                    light.position,
                    glm::clamp(light.color, glm::vec3{0.0F}, glm::vec3{10.0F}),
                    glm::max(0.0F, light.intensity),
                    glm::max(0.1F, light.range),
                });
            }
        }

        if (materialLabOverlay && m_materialLabLightingEnabled && m_materialLabPointLightsEnabled)
        {
            pointLights.push_back(engine::render::PointLight{
                previewCenter + glm::vec3{1.8F, 1.3F, 0.9F},
                glm::vec3{1.0F, 0.95F, 0.9F},
                glm::max(0.0F, m_materialLabPointIntensity),
                glm::max(0.1F, m_materialLabPointRange),
            });
            pointLights.push_back(engine::render::PointLight{
                previewCenter + glm::vec3{-1.6F, 0.9F, -1.1F},
                glm::vec3{0.45F, 0.56F, 1.0F},
                glm::max(0.0F, m_materialLabPointIntensity * 0.6F),
                glm::max(0.1F, m_materialLabPointRange),
            });
        }

        renderer.SetPointLights(pointLights);
        renderer.SetSpotLights(spotLights);
    }

    const int gridHalf = loopMode ? static_cast<int>(kEditorTileSize * 0.5F) : std::max(8, std::max(m_map.width, m_map.height));
    const float step = loopMode ? 1.0F : m_map.tileSize;
    const glm::vec3 majorColor = m_debugView ? glm::vec3{0.35F, 0.35F, 0.35F} : glm::vec3{0.18F, 0.18F, 0.18F};
    const glm::vec3 minorColor = m_debugView ? glm::vec3{0.18F, 0.18F, 0.18F} : glm::vec3{0.1F, 0.1F, 0.1F};
    renderer.DrawGrid(gridHalf, step, majorColor, minorColor);

    if (!loopMode)
    {
        const float boardHalfX = std::max(6.0F, static_cast<float>(m_map.width) * m_map.tileSize * 0.5F);
        const float boardHalfZ = std::max(6.0F, static_cast<float>(m_map.height) * m_map.tileSize * 0.5F);
        renderer.DrawBox(
            glm::vec3{0.0F, -0.02F, 0.0F},
            glm::vec3{boardHalfX, 0.02F, boardHalfZ},
            glm::vec3{0.12F, 0.14F, 0.17F}
        );
    }

    if (loopMode)
    {
        const float halfTile = kEditorTileSize * 0.5F;
        renderer.DrawBox(glm::vec3{0.0F, 0.005F, 0.0F}, glm::vec3{halfTile, 0.005F, halfTile}, glm::vec3{0.12F, 0.14F, 0.17F});
        const glm::vec3 edgeColor{1.0F, 0.95F, 0.35F};
        renderer.DrawOverlayLine(glm::vec3{-halfTile, 0.02F, -halfTile}, glm::vec3{halfTile, 0.02F, -halfTile}, edgeColor);
        renderer.DrawOverlayLine(glm::vec3{halfTile, 0.02F, -halfTile}, glm::vec3{halfTile, 0.02F, halfTile}, edgeColor);
        renderer.DrawOverlayLine(glm::vec3{halfTile, 0.02F, halfTile}, glm::vec3{-halfTile, 0.02F, halfTile}, edgeColor);
        renderer.DrawOverlayLine(glm::vec3{-halfTile, 0.02F, halfTile}, glm::vec3{-halfTile, 0.02F, -halfTile}, edgeColor);
    }

    if (m_debugView)
    {
        renderer.DrawOverlayLine(glm::vec3{0.0F, 0.01F, 0.0F}, glm::vec3{4.0F, 0.01F, 0.0F}, glm::vec3{1.0F, 0.25F, 0.25F});
        renderer.DrawOverlayLine(glm::vec3{0.0F, 0.01F, 0.0F}, glm::vec3{0.0F, 4.0F, 0.0F}, glm::vec3{0.25F, 1.0F, 0.25F});
        renderer.DrawOverlayLine(glm::vec3{0.0F, 0.01F, 0.0F}, glm::vec3{0.0F, 0.01F, 4.0F}, glm::vec3{0.25F, 0.55F, 1.0F});
    }

    auto drawGizmo = [&]() {
        if (m_selection.kind == SelectionKind::None)
        {
            return;
        }
        if (m_gizmoMode == GizmoMode::Scale && m_selection.kind == SelectionKind::MapPlacement)
        {
            return;
        }
        if (m_gizmoMode == GizmoMode::Rotate && m_selection.kind == SelectionKind::MapPlacement)
        {
            return;
        }

        const glm::vec3 pivot = SelectionPivot();
        const float cameraDistance = glm::length(m_cameraPosition - pivot);
        const float axisLength = glm::clamp(cameraDistance * 0.18F, 1.8F, 10.0F);
        const float headSize = glm::max(0.12F, axisLength * 0.08F);
        const float arrowHeadLength = glm::max(0.25F, axisLength * 0.2F);
        const float arrowHeadWidth = glm::max(0.1F, arrowHeadLength * 0.35F);

        auto axisColor = [&](GizmoAxis axis) {
            const bool active = m_axisDragActive && m_axisDragAxis == axis;
            if (axis == GizmoAxis::X)
            {
                return active ? glm::vec3{1.0F, 1.0F, 0.2F} : glm::vec3{1.0F, 0.25F, 0.25F};
            }
            if (axis == GizmoAxis::Y)
            {
                return active ? glm::vec3{1.0F, 1.0F, 0.2F} : glm::vec3{0.25F, 1.0F, 0.25F};
            }
            return active ? glm::vec3{1.0F, 1.0F, 0.2F} : glm::vec3{0.25F, 0.55F, 1.0F};
        };

        auto drawAxisLine = [&](const glm::vec3& direction, GizmoAxis axis, bool drawArrow, bool drawCube) {
            if (m_selection.kind == SelectionKind::MapPlacement && axis == GizmoAxis::Y)
            {
                return;
            }
            const glm::vec3 color = axisColor(axis);
            const glm::vec3 dir = glm::normalize(direction);
            const glm::vec3 tip = pivot + dir * axisLength;
            renderer.DrawOverlayLine(pivot, tip, color);

            if (drawArrow)
            {
                glm::vec3 side = glm::cross(dir, glm::vec3{0.0F, 1.0F, 0.0F});
                if (glm::length(side) < 1.0e-4F)
                {
                    side = glm::cross(dir, glm::vec3{1.0F, 0.0F, 0.0F});
                }
                side = glm::normalize(side);
                const glm::vec3 up = glm::normalize(glm::cross(side, dir));
                const glm::vec3 base = tip - dir * arrowHeadLength;
                renderer.DrawOverlayLine(tip, base + side * arrowHeadWidth, color);
                renderer.DrawOverlayLine(tip, base - side * arrowHeadWidth, color);
                renderer.DrawOverlayLine(tip, base + up * arrowHeadWidth, color);
                renderer.DrawOverlayLine(tip, base - up * arrowHeadWidth, color);
            }
            if (drawCube)
            {
                renderer.DrawBox(tip, glm::vec3{headSize}, color);
            }
            else
            {
                renderer.DrawBox(tip, glm::vec3{headSize * 0.75F}, color);
            }
        };

        if (m_gizmoMode == GizmoMode::Rotate)
        {
            drawAxisLine(glm::vec3{1.0F, 0.0F, 0.0F}, GizmoAxis::X, false, true);
            drawAxisLine(glm::vec3{0.0F, 1.0F, 0.0F}, GizmoAxis::Y, false, true);
            drawAxisLine(glm::vec3{0.0F, 0.0F, 1.0F}, GizmoAxis::Z, false, true);
            return;
        }

        const bool arrow = m_gizmoMode == GizmoMode::Translate;
        const bool cube = m_gizmoMode == GizmoMode::Scale;
        drawAxisLine(glm::vec3{1.0F, 0.0F, 0.0F}, GizmoAxis::X, arrow, cube);
        drawAxisLine(glm::vec3{0.0F, 1.0F, 0.0F}, GizmoAxis::Y, arrow, cube);
        drawAxisLine(glm::vec3{0.0F, 0.0F, 1.0F}, GizmoAxis::Z, arrow, cube);
    };

    if (loopMode)
    {
        // In dedicated Modeling workspace we keep scene uncluttered: active mesh + work plane.
        const bool modelingWorkspace = m_uiWorkspace == UiWorkspace::Mesh;
        if (!modelingWorkspace)
        {
            for (int i = 0; i < static_cast<int>(m_loop.elements.size()); ++i)
            {
                const LoopElement& element = m_loop.elements[static_cast<std::size_t>(i)];
                glm::vec3 color{0.8F, 0.8F, 0.8F};
                if (element.type == LoopElementType::Window)
                {
                    color = glm::vec3{0.2F, 0.8F, 1.0F};
                }
                else if (element.type == LoopElementType::Pallet)
                {
                    color = glm::vec3{1.0F, 0.85F, 0.2F};
                }
                else if (element.type == LoopElementType::Marker)
                {
                    color = glm::vec3{0.9F, 0.4F, 1.0F};
                }
                if (IsSelected(SelectionKind::LoopElement, i))
                {
                    color = glm::vec3{1.0F, 0.2F, 0.2F};
                }
                if (element.transformLocked && !IsSelected(SelectionKind::LoopElement, i))
                {
                    color *= 0.65F;
                }
                renderer.DrawOrientedBox(element.position, element.halfExtents, ElementRotation(element), color);
            }

            if (m_debugView)
            {
                const glm::vec3 loopCenter = (m_loop.boundsMin + m_loop.boundsMax) * 0.5F;
                const glm::vec3 loopHalf = glm::max(glm::vec3{0.05F}, (m_loop.boundsMax - m_loop.boundsMin) * 0.5F);
                renderer.DrawBox(loopCenter + glm::vec3{0.0F, 0.01F, 0.0F}, loopHalf, glm::vec3{0.35F, 0.65F, 0.35F});
            }
        }

        const float planeSize = modelingWorkspace ? 12.0F : 8.0F;
        const glm::vec3 planeCenter = modelingWorkspace
                                          ? glm::vec3{m_meshModelPosition.x, -0.06F, m_meshModelPosition.z}
                                          : glm::vec3{0.0F, -0.06F, 0.0F};
        renderer.DrawOrientedBox(
            planeCenter,
            glm::vec3{planeSize, 0.05F, planeSize},
            glm::vec3{0.0F, 0.0F, 0.0F},
            glm::vec3{0.20F, 0.23F, 0.28F}
        );
    }

    if (!loopMode)
    {
    for (int i = 0; i < static_cast<int>(m_map.placements.size()); ++i)
    {
        const LoopPlacement& placement = m_map.placements[static_cast<std::size_t>(i)];
        LoopAsset loop;
        std::string error;
        if (!LevelAssetIO::LoadLoop(placement.loopId, &loop, &error))
        {
            continue;
        }

        const glm::ivec2 footprint = RotatedFootprint(loop, placement.rotationDegrees);
        const glm::vec3 pivot = TileCenter(placement.tileX, placement.tileY) +
                                glm::vec3{
                                    (static_cast<float>(footprint.x) - 1.0F) * m_map.tileSize * 0.5F,
                                    0.0F,
                                    (static_cast<float>(footprint.y) - 1.0F) * m_map.tileSize * 0.5F,
                                };

        for (const LoopElement& element : loop.elements)
        {
            const glm::vec3 worldCenter = pivot + RotateY(element.position, static_cast<float>(placement.rotationDegrees));
            const glm::vec3 worldRotation{
                element.pitchDegrees,
                static_cast<float>(placement.rotationDegrees) + element.yawDegrees,
                element.rollDegrees,
            };
            glm::vec3 color{0.55F, 0.55F, 0.58F};
            if (element.type == LoopElementType::Window)
            {
                color = glm::vec3{0.2F, 0.8F, 1.0F};
            }
            else if (element.type == LoopElementType::Pallet)
            {
                color = glm::vec3{1.0F, 0.85F, 0.2F};
            }
            if (IsSelected(SelectionKind::MapPlacement, i))
            {
                color = glm::vec3{1.0F, 0.3F, 0.3F};
            }
            renderer.DrawOrientedBox(worldCenter, element.halfExtents, worldRotation, color);
        }

        if (m_debugView)
        {
            renderer.DrawBox(
                pivot + glm::vec3{0.0F, 0.02F, 0.0F},
                glm::vec3{
                    static_cast<float>(footprint.x) * m_map.tileSize * 0.5F,
                    0.02F,
                    static_cast<float>(footprint.y) * m_map.tileSize * 0.5F,
                },
                glm::vec3{0.4F, 0.4F, 0.4F}
            );
        }
    }

    for (int i = 0; i < static_cast<int>(m_map.props.size()); ++i)
    {
        const PropInstance& prop = m_map.props[static_cast<std::size_t>(i)];
        glm::vec3 color{0.3F, 0.6F, 0.28F};
        if (prop.type == PropType::Rock)
        {
            color = glm::vec3{0.5F, 0.5F, 0.55F};
        }
        else if (prop.type == PropType::Obstacle)
        {
            color = glm::vec3{0.75F, 0.38F, 0.28F};
        }
        else if (prop.type == PropType::Platform)
        {
            color = glm::vec3{0.62F, 0.62F, 0.70F};
        }
        else if (prop.type == PropType::MeshAsset)
        {
            color = glm::vec3{1.0F, 1.0F, 1.0F};
        }
        engine::render::MaterialParams materialParams{};
        if (const MaterialAsset* material = prop.materialAsset.empty() ? nullptr : GetMaterialCached(prop.materialAsset);
            material != nullptr)
        {
            color = glm::vec3{material->baseColor.r, material->baseColor.g, material->baseColor.b};
            color = glm::clamp(color, glm::vec3{0.0F}, glm::vec3{1.0F});
            materialParams = ToRenderMaterialParams(material);
        }
        if (IsSelected(SelectionKind::Prop, i))
        {
            color = prop.type == PropType::MeshAsset ? glm::vec3{1.0F, 1.0F, 1.0F} : glm::vec3{1.0F, 0.3F, 0.3F};
        }
        if (prop.transformLocked && !IsSelected(SelectionKind::Prop, i))
        {
            color *= 0.65F;
        }

        glm::vec3 drawPosition = prop.position;
        glm::vec3 drawRotation = PropRotation(prop);
        glm::vec3 drawScale = glm::vec3{1.0F};
        const bool isSelectedProp = IsSelected(SelectionKind::Prop, i);
        if (!prop.animationClip.empty() && ((isSelectedProp && m_animationPreviewPlaying) || prop.animationAutoplay))
        {
            if (const AnimationClipAsset* cachedClip = GetAnimationClipCached(prop.animationClip); cachedClip != nullptr)
            {
                AnimationClipAsset clip = *cachedClip;
                clip.loop = clip.loop && prop.animationLoop;
                clip.speed *= std::max(0.01F, prop.animationSpeed);
                glm::vec3 posOffset{0.0F};
                glm::vec3 rotOffset{0.0F};
                if (SampleAnimation(clip, m_animationPreviewTime * std::max(0.01F, clip.speed), &posOffset, &rotOffset, &drawScale))
                {
                    drawPosition += posOffset;
                    drawRotation += rotOffset;
                }
            }
        }

        const bool isMeshAsset = prop.type == PropType::MeshAsset;
        const bool drawProxyBox =
            !isMeshAsset || renderer.GetRenderMode() == engine::render::RenderMode::Wireframe || m_debugView;
        if (drawProxyBox)
        {
            renderer.DrawOrientedBox(drawPosition, prop.halfExtents * drawScale, drawRotation, color, materialParams);
        }
        if (prop.type == PropType::MeshAsset && !prop.meshAsset.empty())
        {
            std::string loadError;
            const std::filesystem::path absolute = m_assetRegistry.AbsolutePath(prop.meshAsset);
            const engine::assets::MeshData* meshData = m_meshLibrary.LoadMesh(absolute, &loadError);
            if (meshData != nullptr && meshData->loaded)
            {
                const glm::vec3 meshSize = glm::max(glm::vec3{0.0001F}, meshData->boundsMax - meshData->boundsMin);
                const glm::vec3 targetSize = glm::max(glm::vec3{0.05F}, prop.halfExtents * 2.0F);
                const float uniformScale = glm::max(
                    0.0001F,
                    glm::min(
                        targetSize.x / meshSize.x,
                        glm::min(targetSize.y / meshSize.y, targetSize.z / meshSize.z)));
                if (!meshData->surfaces.empty())
                {
                    for (std::size_t surfaceIndex = 0; surfaceIndex < meshData->surfaces.size(); ++surfaceIndex)
                    {
                        const auto& surface = meshData->surfaces[surfaceIndex];
                        const unsigned int albedoTexture = GetOrCreateMeshSurfaceAlbedoTexture(prop.meshAsset, surfaceIndex, surface);
                        if (albedoTexture != 0)
                        {
                            renderer.DrawTexturedMesh(
                                surface.geometry,
                                drawPosition,
                                drawRotation,
                                glm::vec3{uniformScale} * drawScale,
                                color,
                                materialParams,
                                albedoTexture);
                        }
                        else
                        {
                            renderer.DrawMesh(
                                surface.geometry,
                                drawPosition,
                                drawRotation,
                                glm::vec3{uniformScale} * drawScale,
                                color,
                                materialParams);
                        }
                    }
                }
                else
                {
                    renderer.DrawMesh(
                        meshData->geometry,
                        drawPosition,
                        drawRotation,
                        glm::vec3{uniformScale} * drawScale,
                        color,
                        materialParams);
                }
            }
            else if (m_debugView && !loadError.empty())
            {
                renderer.DrawOverlayLine(
                    drawPosition,
                    drawPosition + glm::vec3{0.0F, 2.4F, 0.0F},
                    glm::vec3{1.0F, 0.2F, 0.2F}
                );
            }
        }

        if (m_debugView && prop.type == PropType::MeshAsset)
        {
            renderer.DrawOverlayLine(drawPosition, drawPosition + glm::vec3{0.0F, 1.8F, 0.0F}, glm::vec3{0.35F, 0.9F, 1.0F});
        }
    }

    if (materialLabOverlay)
    {
        if (m_materialLabBackdropEnabled)
        {
            renderer.DrawOrientedBox(
                previewFloorCenter,
                glm::vec3{1.8F, 0.2F, 1.8F},
                glm::vec3{0.0F, 0.0F, 0.0F},
                glm::vec3{0.2F, 0.2F, 0.22F}
            );
            renderer.DrawOrientedBox(
                previewCenter + glm::vec3{0.0F, 0.95F, -1.95F},
                glm::vec3{1.9F, 1.2F, 0.12F},
                glm::vec3{0.0F, 0.0F, 0.0F},
                glm::vec3{0.16F, 0.16F, 0.19F}
            );
        }

        const glm::vec3 previewColor = glm::vec3{
            m_materialEditing.baseColor.r,
            m_materialEditing.baseColor.g,
            m_materialEditing.baseColor.b,
        };
        const engine::render::MaterialParams previewMaterial = ToRenderMaterialParams(&m_materialEditing);
        static const engine::render::MeshGeometry kMaterialLabSphere = BuildUvSphereGeometry(36, 64);
        renderer.DrawMesh(
            kMaterialLabSphere,
            previewCenter,
            glm::vec3{0.0F, previewYawDegrees, 0.0F},
            glm::vec3{m_materialLabSphereRadius},
            glm::clamp(previewColor, glm::vec3{0.0F}, glm::vec3{1.0F}),
            previewMaterial
        );
        if (m_debugView)
        {
            renderer.DrawOverlayLine(previewCenter, previewCenter + glm::vec3{0.0F, m_materialLabSphereRadius + 0.8F, 0.0F}, glm::vec3{0.95F, 0.8F, 0.25F});
        }
    }

    for (int i = 0; i < static_cast<int>(m_map.lights.size()); ++i)
    {
        const LightInstance& light = m_map.lights[static_cast<std::size_t>(i)];
        const bool selected = m_selectedLightIndex == i;
        glm::vec3 color = glm::clamp(light.color, glm::vec3{0.05F}, glm::vec3{1.0F});
        if (!light.enabled)
        {
            color *= 0.35F;
        }
        if (selected)
        {
            color = glm::vec3{1.0F, 0.35F, 0.2F};
        }

        const float markerRadius = light.type == LightType::Spot ? 0.22F : 0.18F;
        renderer.DrawCapsule(light.position, markerRadius * 2.0F, markerRadius, color);
        if (light.type == LightType::Spot || m_debugView)
        {
            const glm::mat3 rotation = RotationMatrixFromEulerDegrees(light.rotationEuler);
            const glm::vec3 dir = glm::normalize(rotation * glm::vec3{0.0F, 0.0F, -1.0F});
            const float lineLength = glm::max(1.0F, light.range * (light.type == LightType::Spot ? 0.25F : 0.12F));
            renderer.DrawOverlayLine(light.position, light.position + dir * lineLength, color);
        }
    }

    if (m_hoveredTileValid && m_debugView)
    {
        const glm::vec3 center = TileCenter(m_hoveredTile.x, m_hoveredTile.y);
        const glm::vec3 color = m_propPlacementMode
                                    ? glm::vec3{0.7F, 0.3F, 1.0F}
                                    : (CanPlaceLoopAt(m_hoveredTile.x, m_hoveredTile.y, m_pendingPlacementRotation, -1)
                                           ? glm::vec3{0.25F, 1.0F, 0.25F}
                                           : glm::vec3{1.0F, 0.25F, 0.25F});
        renderer.DrawBox(center + glm::vec3{0.0F, 0.02F, 0.0F}, glm::vec3{m_map.tileSize * 0.5F, 0.02F, m_map.tileSize * 0.5F}, color);
    }

    if (m_mode == Mode::MapEditor && m_lightPlacementMode && m_hoveredTileValid)
    {
        const glm::vec3 center = TileCenter(m_hoveredTile.x, m_hoveredTile.y);
        const bool spot = m_lightPlacementType == LightType::Spot;
        const glm::vec3 color = spot ? glm::vec3{1.0F, 0.65F, 0.2F} : glm::vec3{1.0F, 1.0F, 0.4F};
        const glm::vec3 pos = center + glm::vec3{0.0F, spot ? 3.0F : 2.5F, 0.0F};
        renderer.DrawOverlayLine(center + glm::vec3{0.0F, 0.05F, 0.0F}, pos, color);
        renderer.DrawCapsule(pos, 0.42F, 0.18F, color);
        if (spot)
        {
            const glm::vec3 dir = glm::normalize(RotationMatrixFromEulerDegrees(glm::vec3{-45.0F, glm::degrees(m_cameraYaw), 0.0F}) * glm::vec3{0.0F, 0.0F, -1.0F});
            renderer.DrawOverlayLine(pos, pos + dir * 2.4F, color);
        }
    }
    }

    if (m_mode == Mode::LoopEditor)
    {
        const glm::vec3 cursorColor{0.2F, 0.95F, 0.95F};
        const float c = 0.32F;
        renderer.DrawOverlayLine(
            m_meshCursorPosition + glm::vec3{-c, 0.0F, 0.0F},
            m_meshCursorPosition + glm::vec3{c, 0.0F, 0.0F},
            cursorColor
        );
        renderer.DrawOverlayLine(
            m_meshCursorPosition + glm::vec3{0.0F, -c, 0.0F},
            m_meshCursorPosition + glm::vec3{0.0F, c, 0.0F},
            cursorColor
        );
        renderer.DrawOverlayLine(
            m_meshCursorPosition + glm::vec3{0.0F, 0.0F, -c},
            m_meshCursorPosition + glm::vec3{0.0F, 0.0F, c},
            cursorColor
        );
        if (m_meshClickPlacementPending)
        {
            renderer.DrawCapsule(
                m_meshClickPreviewPosition,
                0.65F,
                0.18F,
                glm::vec3{0.95F, 0.82F, 0.25F}
            );
        }
    }

    m_fxPreviewSystem.Render(renderer, m_cameraPosition);
    RenderMeshModeler(renderer);
    drawGizmo();
}

glm::mat4 LevelEditor::BuildViewProjection(float aspectRatio) const
{
    const glm::vec3 forward = CameraForward();
    const glm::mat4 view = glm::lookAt(m_cameraPosition, m_cameraPosition + forward, CameraUp());
    const glm::mat4 projection = glm::perspective(glm::radians(60.0F), aspectRatio > 0.0F ? aspectRatio : (16.0F / 9.0F), 0.05F, 900.0F);
    return projection * view;
}

engine::render::EnvironmentSettings LevelEditor::CurrentEnvironmentSettings() const
{
    engine::render::EnvironmentSettings settings = ToRenderEnvironment(m_environmentEditing);
    if (m_materialLabViewMode != MaterialLabViewMode::Off)
    {
        settings.directionalLightIntensity =
            m_materialLabDirectionalLightEnabled ? glm::max(0.0F, m_materialLabDirectionalIntensity) : 0.0F;
    }
    return settings;
}

void LevelEditor::DrawUi(
    bool* outBackToMenu,
    bool* outPlaytestMap,
    std::string* outPlaytestMapName
)
{
#if BUILD_WITH_IMGUI
    if (outBackToMenu != nullptr)
    {
        *outBackToMenu = false;
    }
    if (outPlaytestMap != nullptr)
    {
        *outPlaytestMap = false;
    }
    if (outPlaytestMapName != nullptr)
    {
        outPlaytestMapName->clear();
    }
    m_sceneViewportHovered = false;
    m_sceneViewportFocused = false;
    m_sceneViewportRectMin = glm::vec2{0.0F};
    m_sceneViewportRectMax = glm::vec2{0.0F};
    m_contentBrowserHovered = false;

    auto saveCurrentLoop = [this]() {
        std::string error;
        if (LevelAssetIO::SaveLoop(m_loop, &error))
        {
            m_statusLine = "Saved loop " + m_loop.id;
            RefreshLibraries();
        }
        else
        {
            m_statusLine = "Save failed: " + error;
        }
    };

    auto saveCurrentMap = [this]() {
        std::string error;
        if (m_map.environmentAssetId.empty())
        {
            m_map.environmentAssetId = "default_environment";
        }
        if (!LevelAssetIO::SaveEnvironment(m_environmentEditing, &error))
        {
            m_statusLine = "Save environment failed: " + error;
            return;
        }
        m_map.environmentAssetId = m_environmentEditing.id;
        if (LevelAssetIO::SaveMap(m_map, &error))
        {
            m_statusLine = "Saved map " + m_map.name;
            RefreshLibraries();
        }
        else
        {
            m_statusLine = "Save map failed: " + error;
        }
    };

    ImGuiViewport* viewport = ImGui::GetMainViewport();
    const ImVec2 vpPos = viewport != nullptr ? viewport->Pos : ImVec2{0.0F, 0.0F};
    const ImVec2 vpSize = viewport != nullptr ? viewport->Size : ImVec2{1920.0F, 1080.0F};
    const float pad = 2.0F;
    const float workspaceX = vpPos.x + pad;
    const float workspaceY = vpPos.y + pad;
    const float workspaceW = std::max(900.0F, vpSize.x - pad * 2.0F);
    const float workspaceH = std::max(620.0F, vpSize.y - pad * 2.0F);
    float topH = m_blenderLayoutPreset
                     ? 92.0F
                     : glm::clamp(workspaceH * m_layoutTopHeightRatio, 62.0F, 120.0F);
    float bottomH = glm::clamp(workspaceH * m_layoutBottomHeightRatio, 95.0F, workspaceH * 0.45F);
    float middleH = workspaceH - topH - bottomH;
    if (middleH < 210.0F)
    {
        const float deficit = 210.0F - middleH;
        bottomH = std::max(95.0F, bottomH - deficit * 0.5F);
        topH = std::max(70.0F, topH - deficit * 0.5F);
        middleH = workspaceH - topH - bottomH;
    }

    float leftW = glm::clamp(workspaceW * m_layoutLeftWidthRatio, 150.0F, workspaceW * 0.45F);
    float rightW = glm::clamp(workspaceW * m_layoutRightWidthRatio, 220.0F, workspaceW * 0.48F);
    float centerW = workspaceW - leftW - rightW;
    if (centerW < 360.0F)
    {
        const float deficit = 360.0F - centerW;
        leftW = std::max(150.0F, leftW - deficit * 0.5F);
        rightW = std::max(220.0F, rightW - deficit * 0.5F);
        centerW = workspaceW - leftW - rightW;
    }

    const glm::vec4 topRect{workspaceX, workspaceY, workspaceW, topH};
    const glm::vec4 leftRect{workspaceX, workspaceY + topH, leftW, middleH};
    const glm::vec4 centerRect{leftRect.x + leftRect.z, leftRect.y, centerW, middleH};
    const glm::vec4 rightRect{centerRect.x + centerRect.z, leftRect.y, rightW, middleH};
    const glm::vec4 bottomRect{workspaceX, leftRect.y + leftRect.w, workspaceW, bottomH};
    float bottomSplitX = bottomRect.x + glm::clamp(m_layoutBottomSplitRatio, 0.2F, 0.8F) * bottomRect.z;
    bottomSplitX = glm::clamp(bottomSplitX, bottomRect.x + 180.0F, bottomRect.x + bottomRect.z - 220.0F);
    const glm::vec4 bottomLeftRect{bottomRect.x, bottomRect.y, bottomSplitX - bottomRect.x, bottomRect.w};
    const glm::vec4 bottomRightRect{bottomSplitX, bottomRect.y, bottomRect.x + bottomRect.z - bottomSplitX, bottomRect.w};
    const float toolbarW = m_blenderLayoutPreset ? 86.0F : 0.0F;
    const float contentAreaX = leftRect.x + toolbarW;
    const float contentAreaW = std::max(180.0F, leftRect.z - toolbarW);
    const float midY = leftRect.y;
    const float midH = leftRect.w;

    auto setPanelRect = [](float x, float y, float w, float h) {
        ImGui::SetNextWindowPos(ImVec2{x, y}, ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2{std::max(140.0F, w), std::max(90.0F, h)}, ImGuiCond_Always);
    };

    const ImGuiWindowFlags dockPanelFlags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoMove |
                                            ImGuiWindowFlags_NoResize;
    const ImGuiWindowFlags sceneWindowFlags = dockPanelFlags | ImGuiWindowFlags_NoScrollbar |
                                              ImGuiWindowFlags_NoScrollWithMouse;

    const ImGuiWindowFlags rootFlags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                                       ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus |
                                       ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoInputs;
    ImGui::SetNextWindowPos(ImVec2{workspaceX, workspaceY}, ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2{workspaceW, workspaceH}, ImGuiCond_Always);
    if (ImGui::Begin("##EditorLayoutRoot", nullptr, rootFlags))
    {
        ImDrawList* draw = ImGui::GetWindowDrawList();
        const ImU32 sepColor = IM_COL32(64, 79, 103, 180);
        draw->AddLine(ImVec2{leftRect.x, leftRect.y}, ImVec2{leftRect.x + leftRect.z + rightRect.z + centerRect.z, leftRect.y}, sepColor, 1.0F);
        draw->AddLine(ImVec2{leftRect.x, bottomRect.y}, ImVec2{leftRect.x + leftRect.z + rightRect.z + centerRect.z, bottomRect.y}, sepColor, 1.0F);
        draw->AddLine(ImVec2{leftRect.x + leftRect.z, leftRect.y}, ImVec2{leftRect.x + leftRect.z, leftRect.y + leftRect.w}, sepColor, 1.0F);
        draw->AddLine(ImVec2{rightRect.x, rightRect.y}, ImVec2{rightRect.x, rightRect.y + rightRect.w}, sepColor, 1.0F);
        draw->AddLine(ImVec2{bottomSplitX, bottomRect.y}, ImVec2{bottomSplitX, bottomRect.y + bottomRect.w}, sepColor, 1.0F);

        // NOTE: layout root is visual-only to avoid invisible hitboxes blocking panel input.
    }
    ImGui::End();

    const bool workspaceAll = m_uiWorkspace == UiWorkspace::All;
    const bool workspaceMesh = m_uiWorkspace == UiWorkspace::Mesh;
    const bool workspaceMap = m_uiWorkspace == UiWorkspace::Map;
    const bool workspaceLighting = m_uiWorkspace == UiWorkspace::Lighting;
    const bool workspaceFxEnv = m_uiWorkspace == UiWorkspace::FxEnv;

    if (workspaceMesh && m_mode != Mode::LoopEditor)
    {
        m_mode = Mode::LoopEditor;
    }
    if ((workspaceMap || workspaceLighting || workspaceFxEnv) && m_mode != Mode::MapEditor)
    {
        m_mode = Mode::MapEditor;
    }

    const bool showLoopPanels = (m_mode == Mode::LoopEditor) && (workspaceAll || workspaceMesh);
    const bool showMapPanels = (m_mode == Mode::MapEditor) && (workspaceAll || workspaceMap);
    const bool showContentWindow = workspaceAll || workspaceMap || workspaceLighting || workspaceFxEnv;
    const bool showMaterialWindow = workspaceAll || workspaceLighting || workspaceFxEnv;
    const bool showFxWindow = workspaceAll || workspaceFxEnv || workspaceMesh;

    auto handleSceneDropPayload = [this]() {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_ASSET_PATH"))
        {
            const char* path = static_cast<const char*>(payload->Data);
            if (path != nullptr)
            {
                PlaceImportedAssetAtHovered(path);
                return true;
            }
        }
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("LOOP_ASSET_ID"))
        {
            const char* loopId = static_cast<const char*>(payload->Data);
            if (loopId != nullptr)
            {
                const auto it = std::find(m_loopLibrary.begin(), m_loopLibrary.end(), loopId);
                if (it != m_loopLibrary.end())
                {
                    m_paletteLoopIndex = static_cast<int>(std::distance(m_loopLibrary.begin(), it));
                }
                if (m_mode == Mode::MapEditor)
                {
                    PlaceLoopAtHoveredTile();
                }
                else
                {
                    LoopAsset loaded;
                    std::string error;
                    if (LevelAssetIO::LoadLoop(loopId, &loaded, &error))
                    {
                        m_loop = loaded;
                        m_statusLine = "Loaded loop by drag&drop: " + std::string(loopId);
                    }
                    else
                    {
                        m_statusLine = "Loop drop failed: " + error;
                    }
                }
                return true;
            }
        }
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("PREFAB_ASSET_ID"))
        {
            const char* prefabId = static_cast<const char*>(payload->Data);
            if (prefabId != nullptr)
            {
                InstantiatePrefabAtHovered(prefabId);
                return true;
            }
        }
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("FX_ASSET_ID"))
        {
            const char* fxId = static_cast<const char*>(payload->Data);
            if (fxId != nullptr)
            {
                const glm::vec3 spawnPos = m_hoveredTileValid
                                               ? TileCenter(m_hoveredTile.x, m_hoveredTile.y) + glm::vec3{0.0F, 0.2F, 0.0F}
                                               : (m_cameraPosition + CameraForward() * 4.0F + glm::vec3{0.0F, 0.2F, 0.0F});
                m_fxPreviewSystem.Spawn(fxId, spawnPos, CameraForward(), {});
                m_statusLine = "Spawned FX by drag&drop: " + std::string(fxId);
                return true;
            }
        }
        return false;
    };

    float leftDockCursorY = midY + 6.0F;
    float rightDockCursorY = midY + 6.0F;
    float bottomLeftDockCursorY = bottomLeftRect.y + 6.0F;
    const float panelSpacing = 4.0F;
    const float regionPadding = 6.0F;

    auto drawPanelDragHandle = [&](DockPanel panel) {
        ImGui::SameLine();
        const std::string buttonId = std::string("Dock##") + DockPanelTitle(panel);
        ImGui::SmallButton(buttonId.c_str());
        if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceNoPreviewTooltip))
        {
            const int panelId = static_cast<int>(panel);
            ImGui::SetDragDropPayload("EDITOR_DOCK_PANEL", &panelId, sizeof(panelId));
            ImGui::Text("Dock %s", DockPanelTitle(panel));
            ImGui::EndDragDropSource();
        }
    };

    auto beginDockedPanel = [&](DockPanel panel, float preferredHeight, const char* titleOverride) {
        if (!m_panelVisible[static_cast<std::size_t>(panel)])
        {
            return false;
        }
        const LayoutRegion region = m_panelRegion[static_cast<std::size_t>(panel)];
        float x = rightRect.x + regionPadding;
        float y = rightDockCursorY;
        float w = rightRect.z - regionPadding * 2.0F;
        float h = preferredHeight;
        float* cursorY = &rightDockCursorY;
        float regionBottom = rightRect.y + rightRect.w - regionPadding;

        if (region == LayoutRegion::Left)
        {
            x = contentAreaX + regionPadding;
            y = leftDockCursorY;
            w = contentAreaW - regionPadding * 2.0F;
            cursorY = &leftDockCursorY;
            regionBottom = leftRect.y + leftRect.w - regionPadding;
        }
        else if (region == LayoutRegion::BottomLeft)
        {
            x = bottomLeftRect.x + regionPadding;
            y = bottomLeftDockCursorY;
            w = bottomLeftRect.z - regionPadding * 2.0F;
            cursorY = &bottomLeftDockCursorY;
            regionBottom = bottomLeftRect.y + bottomLeftRect.w - regionPadding;
        }

        const float remaining = regionBottom - y;
        if (remaining < 80.0F)
        {
            return false;
        }
        h = glm::clamp(h, 80.0F, remaining);
        setPanelRect(x, y, w, h);
        const char* title = titleOverride != nullptr ? titleOverride : DockPanelTitle(panel);
        const bool open = ImGui::Begin(title, nullptr, dockPanelFlags);
        *cursorY += h + panelSpacing;
        if (!open)
        {
            ImGui::End();
            return false;
        }
        drawPanelDragHandle(panel);
        return true;
    };

    setPanelRect(topRect.x, topRect.y, topRect.z, topRect.w);
    ImGui::SetNextWindowBgAlpha(0.88F);
    if (ImGui::Begin("Editor Top Bar", nullptr, dockPanelFlags | ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse))
    {
        auto setWorkspace = [this](UiWorkspace ws) {
            m_uiWorkspace = ws;
            if (ws == UiWorkspace::Mesh)
            {
                m_mode = Mode::LoopEditor;
                SetMeshObjectEditMode(true, "workspace");
            }
            else if (ws == UiWorkspace::Map || ws == UiWorkspace::Lighting || ws == UiWorkspace::FxEnv)
            {
                m_mode = Mode::MapEditor;
                SetMeshObjectEditMode(false, "workspace");
            }
        };
        auto tooltip = [](const char* text) {
            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip("%s", text);
            }
        };
        auto playtestCurrentMap = [&]() {
            std::string error;
            if (LevelAssetIO::SaveEnvironment(m_environmentEditing, &error))
            {
                m_map.environmentAssetId = m_environmentEditing.id;
            }
            if (error.empty() && LevelAssetIO::SaveMap(m_map, &error))
            {
                if (outPlaytestMap != nullptr)
                {
                    *outPlaytestMap = true;
                }
                if (outPlaytestMapName != nullptr)
                {
                    *outPlaytestMapName = m_map.name;
                }
            }
            else
            {
                m_statusLine = "Playtest failed: " + error;
            }
        };

        if (ImGui::BeginMenuBar())
        {
            if (ImGui::BeginMenu("File"))
            {
                if (ImGui::MenuItem("New Map"))
                {
                    PushHistorySnapshot();
                    CreateNewMap("new_map");
                    m_mode = Mode::MapEditor;
                }
                if (ImGui::MenuItem("New Loop"))
                {
                    PushHistorySnapshot();
                    CreateNewLoop("new_loop");
                    m_mode = Mode::LoopEditor;
                }
                if (ImGui::MenuItem("Save Map", "Ctrl+S", false, m_mode == Mode::MapEditor))
                {
                    saveCurrentMap();
                }
                if (ImGui::MenuItem("Save Loop", "Ctrl+Shift+S", false, m_mode == Mode::LoopEditor))
                {
                    saveCurrentLoop();
                }
                if (ImGui::MenuItem("Import Asset..."))
                {
                    const std::vector<std::string> pickedFiles = OpenMultipleFileDialog();
                    if (!pickedFiles.empty())
                    {
                        int importedCount = 0;
                        for (const std::string& picked : pickedFiles)
                        {
                            const engine::assets::ImportResult imported =
                                m_assetRegistry.ImportExternalFileToDirectory(picked, m_contentDirectory == "." ? std::string{} : m_contentDirectory);
                            if (imported.success)
                            {
                                ++importedCount;
                            }
                        }
                        std::ostringstream oss;
                        oss << "Imported " << importedCount << "/" << pickedFiles.size() << " file(s)";
                        m_statusLine = oss.str();
                        RefreshLibraries();
                        RefreshContentBrowser();
                    }
                }
                if (ImGui::MenuItem("Playtest Current Map", "F5", false, m_mode == Mode::MapEditor))
                {
                    playtestCurrentMap();
                }
                if (ImGui::MenuItem("Exit To Main Menu", "Esc"))
                {
                    if (outBackToMenu != nullptr)
                    {
                        *outBackToMenu = true;
                    }
                }
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Edit"))
            {
                if (ImGui::MenuItem("Undo", "Ctrl+Z"))
                {
                    Undo();
                }
                if (ImGui::MenuItem("Redo", "Ctrl+Y"))
                {
                    Redo();
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Reset Layout"))
                {
                    ResetEditorLayout();
                    m_layoutNeedsSave = true;
                }
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("View"))
            {
                ImGui::MenuItem("Toolbar", nullptr, &m_showToolbar);
                ImGui::MenuItem("Status Bar", nullptr, &m_showStatusBar);
                for (int panel = 0; panel < static_cast<int>(DockPanel::Count); ++panel)
                {
                    DockPanel panelId = static_cast<DockPanel>(panel);
                    ImGui::MenuItem(DockPanelTitle(panelId), nullptr, &m_panelVisible[static_cast<std::size_t>(panelId)]);
                }
                ImGui::Separator();
                ImGui::MenuItem("Debug View", nullptr, &m_debugView);
                ImGui::MenuItem("Top-down Camera", nullptr, &m_topDownView);
                if (ImGui::MenuItem("Wireframe"))
                {
                    m_pendingRenderMode = engine::render::RenderMode::Wireframe;
                    m_currentRenderMode = *m_pendingRenderMode;
                }
                if (ImGui::MenuItem("Filled"))
                {
                    m_pendingRenderMode = engine::render::RenderMode::Filled;
                    m_currentRenderMode = *m_pendingRenderMode;
                }
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Add"))
            {
                if (ImGui::MenuItem("Loop Placement", "LMB", false, m_mode == Mode::MapEditor))
                {
                    m_propPlacementMode = false;
                }
                if (ImGui::MenuItem("Prop Placement", "P", false, m_mode == Mode::MapEditor))
                {
                    m_propPlacementMode = true;
                }
                if (ImGui::MenuItem("Point Light", nullptr, false, m_mode == Mode::MapEditor))
                {
                    AddLightAtHovered(LightType::Point);
                }
                if (ImGui::MenuItem("Spot Light", nullptr, false, m_mode == Mode::MapEditor))
                {
                    AddLightAtHovered(LightType::Spot);
                }
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Meshes"))
            {
                int spawnModeIndex = static_cast<int>(m_meshSpawnMode);
                const char* spawnModeItems[] = {"Cursor", "Raycast", "Click Place"};
                ImGui::SetNextItemWidth(180.0F);
                if (ImGui::Combo("Spawn Mode", &spawnModeIndex, spawnModeItems, IM_ARRAYSIZE(spawnModeItems)))
                {
                    spawnModeIndex = std::clamp(spawnModeIndex, 0, 2);
                    m_meshSpawnMode = static_cast<MeshSpawnMode>(spawnModeIndex);
                }
                ImGui::Text("3D Cursor: %.2f %.2f %.2f", m_meshCursorPosition.x, m_meshCursorPosition.y, m_meshCursorPosition.z);
                ImGui::TextDisabled("Shift+RMB in viewport = set cursor");
                ImGui::Separator();

                auto addPrimitive = [&](MeshPrimitiveType primitive) {
                    if (m_mode == Mode::LoopEditor)
                    {
                        QueueMeshPrimitiveSpawn(static_cast<int>(primitive));
                        return;
                    }

                    PushHistorySnapshot();
                    PropInstance prop;
                    prop.name = BuildUniquePropName(std::string("mesh_") + std::string(MeshPrimitiveToText(static_cast<int>(primitive))));
                    prop.type = PropType::Obstacle;
                    glm::vec3 spawnPos = m_hoveredTileValid
                                             ? glm::vec3{m_hoveredWorld.x, 0.85F, m_hoveredWorld.z}
                                             : (m_cameraPosition + CameraForward() * 7.0F + glm::vec3{0.0F, 0.85F, 0.0F});
                    glm::vec3 hit{0.0F};
                    if (RayIntersectGround(m_cameraPosition, CameraForward(), 0.0F, &hit))
                    {
                        spawnPos = hit + glm::vec3{0.0F, 0.85F, 0.0F};
                    }
                    prop.position = spawnPos;
                    switch (primitive)
                    {
                        case MeshPrimitiveType::Cube:
                            prop.halfExtents = glm::vec3{0.6F, 0.6F, 0.6F};
                            break;
                        case MeshPrimitiveType::Sphere:
                            prop.type = PropType::Rock;
                            prop.halfExtents = glm::vec3{0.65F, 0.65F, 0.65F};
                            break;
                        case MeshPrimitiveType::Plane:
                            prop.type = PropType::Platform;
                            prop.halfExtents = glm::vec3{1.2F, 0.08F, 1.2F};
                            prop.position.y = 0.18F;
                            break;
                        case MeshPrimitiveType::Cylinder:
                            prop.halfExtents = glm::vec3{0.5F, 0.95F, 0.5F};
                            break;
                        case MeshPrimitiveType::Cone:
                            prop.halfExtents = glm::vec3{0.55F, 0.85F, 0.55F};
                            break;
                        case MeshPrimitiveType::Capsule:
                            prop.halfExtents = glm::vec3{0.5F, 1.1F, 0.5F};
                            break;
                        default:
                            prop.halfExtents = glm::vec3{0.6F, 0.6F, 0.6F};
                            break;
                    }
                    prop.colliderType = ColliderType::Box;
                    prop.colliderHalfExtents = prop.halfExtents;
                    m_map.props.push_back(prop);
                    SelectSingle(Selection{SelectionKind::Prop, static_cast<int>(m_map.props.size()) - 1});
                    m_statusLine = std::string("Added map primitive: ") + MeshPrimitiveToText(static_cast<int>(primitive));
                };

                if (ImGui::MenuItem("Add Cube")) { addPrimitive(MeshPrimitiveType::Cube); }
                if (ImGui::MenuItem("Add Sphere")) { addPrimitive(MeshPrimitiveType::Sphere); }
                if (ImGui::MenuItem("Add Plane")) { addPrimitive(MeshPrimitiveType::Plane); }
                if (ImGui::MenuItem("Add Cylinder")) { addPrimitive(MeshPrimitiveType::Cylinder); }
                if (ImGui::MenuItem("Add Cone")) { addPrimitive(MeshPrimitiveType::Cone); }
                if (ImGui::MenuItem("Add Capsule")) { addPrimitive(MeshPrimitiveType::Capsule); }
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Select"))
            {
                if (ImGui::MenuItem("Select None", "A"))
                {
                    ClearSelections();
                    m_selectedLightIndex = -1;
                }
                if (ImGui::MenuItem("Select All", "Ctrl+A"))
                {
                    if (m_mode == Mode::LoopEditor)
                    {
                        m_selectedLoopElements.clear();
                        for (int i = 0; i < static_cast<int>(m_loop.elements.size()); ++i)
                        {
                            m_selectedLoopElements.push_back(i);
                        }
                        if (!m_selectedLoopElements.empty())
                        {
                            m_selection = Selection{SelectionKind::LoopElement, m_selectedLoopElements.front()};
                        }
                    }
                    else
                    {
                        m_selectedMapPlacements.clear();
                        for (int i = 0; i < static_cast<int>(m_map.placements.size()); ++i)
                        {
                            m_selectedMapPlacements.push_back(i);
                        }
                        if (!m_selectedMapPlacements.empty())
                        {
                            m_selection = Selection{SelectionKind::MapPlacement, m_selectedMapPlacements.front()};
                        }
                    }
                }
                ImGui::Separator();
                ImGui::MenuItem("Box Select", "B");
                ImGui::MenuItem("Lasso Select", "Ctrl+RMB");
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Help"))
            {
                ImGui::MenuItem("Shortcut Guide", nullptr, &m_showShortcutGuide);
                ImGui::MenuItem("Editor Manual", nullptr, &m_showEditorManual);
                ImGui::EndMenu();
            }
            ImGui::EndMenuBar();
        }

        auto tabButton = [&](const char* label, UiWorkspace ws) {
            const bool active = m_uiWorkspace == ws;
            if (active)
            {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18F, 0.36F, 0.62F, 0.95F));
            }
            if (ImGui::Button(label))
            {
                setWorkspace(ws);
            }
            if (active)
            {
                ImGui::PopStyleColor();
            }
        };

        tabButton("Layout", UiWorkspace::All);
        ImGui::SameLine();
        tabButton("Modeling", UiWorkspace::Mesh);
        ImGui::SameLine();
        tabButton("Map", UiWorkspace::Map);
        ImGui::SameLine();
        tabButton("Lighting", UiWorkspace::Lighting);
        ImGui::SameLine();
        tabButton("FX", UiWorkspace::FxEnv);

        ImGui::Separator();
        ImGui::TextUnformatted("Mode");
        ImGui::SameLine();
        if (ImGui::Button("Object"))
        {
            if (m_mode != Mode::LoopEditor)
            {
                m_mode = Mode::LoopEditor;
            }
            SetMeshObjectEditMode(false, "toolbar");
        }
        tooltip("Object Mode (Tab)");
        ImGui::SameLine();
        if (ImGui::Button("Edit"))
        {
            if (m_mode != Mode::LoopEditor)
            {
                m_mode = Mode::LoopEditor;
            }
            SetMeshObjectEditMode(true, "toolbar");
        }
        tooltip("Edit Mode (Tab)");
        if (m_mode == Mode::LoopEditor && m_meshModelSceneEditEnabled)
        {
            ImGui::SameLine();
            if (ImGui::Button("Vertex"))
            {
                m_meshEditMode = MeshEditMode::Vertex;
            }
            tooltip("Vertex Select (1)");
            ImGui::SameLine();
            if (ImGui::Button("Edge"))
            {
                m_meshEditMode = MeshEditMode::Edge;
            }
            tooltip("Edge Select (2)");
            ImGui::SameLine();
            if (ImGui::Button("Face"))
            {
                m_meshEditMode = MeshEditMode::Face;
            }
            tooltip("Face Select (3)");
        }

        if (m_uiWorkspace == UiWorkspace::Mesh)
        {
            ImGui::Separator();
            ImGui::TextUnformatted("Meshes");
            ImGui::SameLine();
            int spawnModeIndex = static_cast<int>(m_meshSpawnMode);
            const char* spawnModeItems[] = {"Cursor", "Raycast", "Click"};
            ImGui::SetNextItemWidth(110.0F);
            if (ImGui::Combo("Spawn", &spawnModeIndex, spawnModeItems, IM_ARRAYSIZE(spawnModeItems)))
            {
                spawnModeIndex = std::clamp(spawnModeIndex, 0, 2);
                m_meshSpawnMode = static_cast<MeshSpawnMode>(spawnModeIndex);
            }
            ImGui::SameLine();
            ImGui::Text("Cursor %.1f %.1f %.1f", m_meshCursorPosition.x, m_meshCursorPosition.y, m_meshCursorPosition.z);
            tooltip("Shift+RMB in viewport sets 3D cursor");

            if (ImGui::Button("Add Cube"))
            {
                QueueMeshPrimitiveSpawn(static_cast<int>(MeshPrimitiveType::Cube));
            }
            tooltip("Create Cube");
            ImGui::SameLine();
            if (ImGui::Button("Add Sphere"))
            {
                QueueMeshPrimitiveSpawn(static_cast<int>(MeshPrimitiveType::Sphere));
            }
            tooltip("Create Sphere");
            ImGui::SameLine();
            if (ImGui::Button("Add Plane"))
            {
                QueueMeshPrimitiveSpawn(static_cast<int>(MeshPrimitiveType::Plane));
            }
            tooltip("Create Plane");
            ImGui::SameLine();
            if (ImGui::Button("Add Cylinder"))
            {
                QueueMeshPrimitiveSpawn(static_cast<int>(MeshPrimitiveType::Cylinder));
            }
            tooltip("Create Cylinder");
            ImGui::SameLine();
            if (ImGui::Button("Add Cone"))
            {
                QueueMeshPrimitiveSpawn(static_cast<int>(MeshPrimitiveType::Cone));
            }
            tooltip("Create Cone");
            ImGui::SameLine();
            if (ImGui::Button("Add Capsule"))
            {
                QueueMeshPrimitiveSpawn(static_cast<int>(MeshPrimitiveType::Capsule));
            }
            tooltip("Create Capsule");
            if (m_meshClickPlacementPending)
            {
                ImGui::SameLine();
                if (ImGui::Button("Cancel Place"))
                {
                    m_meshClickPlacementPending = false;
                    m_clickPlacePrimitive = MeshPrimitiveType::None;
                    m_statusLine = "Click placement canceled";
                }
            }
        }

        ImGui::SameLine();
        ImGui::Checkbox("Snap", &m_gridSnap);
        tooltip("Toggle Snap (Shift+Tab)");
        ImGui::SameLine();
        int snapTypeIndex = static_cast<int>(m_snapType);
        const char* snapItems[] = {"Increment", "Vertex", "Edge", "Face"};
        if (ImGui::Combo("##snap_type", &snapTypeIndex, snapItems, IM_ARRAYSIZE(snapItems)))
        {
            snapTypeIndex = std::clamp(snapTypeIndex, 0, 3);
            m_snapType = static_cast<SnapType>(snapTypeIndex);
        }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(80.0F);
        ImGui::DragFloat("Grid", &m_gridStep, 0.05F, 0.1F, 8.0F, "%.2f");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(80.0F);
        ImGui::DragFloat("Angle", &m_angleStepDegrees, 1.0F, 1.0F, 90.0F, "%.1f");
        ImGui::SameLine();
        int orientationIndex = m_transformOrientationUi == TransformOrientation::Local ? 1 : 0;
        const char* orientationItems[] = {"Global", "Local"};
        if (ImGui::Combo("Orientation", &orientationIndex, orientationItems, IM_ARRAYSIZE(orientationItems)))
        {
            m_transformOrientationUi = orientationIndex == 1 ? TransformOrientation::Local : TransformOrientation::Global;
            if (m_modalTransform.active)
            {
                m_modalTransform.orientation = m_transformOrientationUi;
            }
        }

        ImGui::SameLine();
        if (ImGui::Button("Back"))
        {
            if (outBackToMenu != nullptr)
            {
                *outBackToMenu = true;
            }
        }
        tooltip("Return to Main Menu");
        if (m_mode == Mode::MapEditor)
        {
            ImGui::SameLine();
            if (ImGui::Button("Playtest"))
            {
                playtestCurrentMap();
            }
            tooltip("Save and play current map");
        }
    }
    ImGui::End();

    if (m_blenderLayoutPreset && m_showToolbar)
    {
        ImGui::SetNextWindowPos(ImVec2{leftRect.x, leftRect.y}, ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2{toolbarW, leftRect.w}, ImGuiCond_Always);
        if (ImGui::Begin("Toolbar", nullptr, dockPanelFlags))
        {
            if (ImGui::Button("Select"))
            {
                m_statusLine = "Select tool active";
            }
            if (ImGui::Button("Move (G)"))
            {
                m_gizmoMode = GizmoMode::Translate;
            }
            if (ImGui::Button("Rotate (R)"))
            {
                m_gizmoMode = GizmoMode::Rotate;
            }
            if (ImGui::Button("Scale (S)"))
            {
                m_gizmoMode = GizmoMode::Scale;
            }
            ImGui::Separator();
            if (ImGui::Button("Object"))
            {
                SetMeshObjectEditMode(false, "toolbar");
            }
            if (ImGui::Button("Edit"))
            {
                if (m_mode == Mode::LoopEditor)
                {
                    SetMeshObjectEditMode(true, "toolbar");
                }
            }
            if (m_mode == Mode::LoopEditor && m_meshModelSceneEditEnabled)
            {
                ImGui::Separator();
                if (ImGui::Button("Vtx (1)"))
                {
                    m_meshEditMode = MeshEditMode::Vertex;
                }
                if (ImGui::Button("Edge (2)"))
                {
                    m_meshEditMode = MeshEditMode::Edge;
                }
                if (ImGui::Button("Face (3)"))
                {
                    m_meshEditMode = MeshEditMode::Face;
                }
            }
            ImGui::Separator();
            if (ImGui::Button(m_gridSnap ? "Snap ON" : "Snap OFF"))
            {
                m_gridSnap = !m_gridSnap;
            }
            if (ImGui::Button("Frame (.)"))
            {
                FocusCameraOnSelection();
            }
        }
        ImGui::End();

    }

    const bool toolSettingsOpen =
        beginDockedPanel(DockPanel::ToolSettings, std::min(260.0F, std::max(170.0F, midH * 0.32F)), "Tool Settings");
    if (toolSettingsOpen)
    {
        ImGui::Text("Active Tool: %s", GizmoToText(m_gizmoMode));
        ImGui::Text("Modal Transform: %s", m_modalTransform.active ? "ON" : "OFF");
        if (m_modalTransform.active)
        {
            int orientationIndex =
                m_modalTransform.orientation == TransformOrientation::Local ? 1 : 0;
            const char* orientationItems[] = {"Global", "Local"};
            if (ImGui::Combo("Transform Orientation", &orientationIndex, orientationItems, IM_ARRAYSIZE(orientationItems)))
            {
                m_modalTransform.orientation = orientationIndex == 1 ? TransformOrientation::Local : TransformOrientation::Global;
            }

            const char* constraintLabel = m_modalTransform.constraintMode == ModalConstraintMode::None
                                              ? "None"
                                              : (m_modalTransform.constraintMode == ModalConstraintMode::Axis ? "Axis" : "Plane");
            ImGui::Text("Constraint: %s", constraintLabel);
            ImGui::Text("Axis: %s",
                        m_modalTransform.constraintAxis == GizmoAxis::X ? "X"
                        : (m_modalTransform.constraintAxis == GizmoAxis::Y ? "Y"
                                                                           : (m_modalTransform.constraintAxis == GizmoAxis::Z ? "Z" : "None")));

            char numericBuffer[64]{};
            std::snprintf(numericBuffer, sizeof(numericBuffer), "%s",
                          m_modalTransform.numericInput.empty() ? "" : m_modalTransform.numericInput.c_str());
            if (ImGui::InputText("Numeric", numericBuffer, sizeof(numericBuffer)))
            {
                m_modalTransform.numericInput = numericBuffer;
            }
            ImGui::SameLine();
            ImGui::Checkbox("Neg", &m_modalTransform.numericNegative);
            ImGui::SameLine();
            if (ImGui::Button("Abs"))
            {
                m_modalTransform.numericMode = NumericInputMode::Absolute;
            }
            ImGui::SameLine();
            if (ImGui::Button("*"))
            {
                m_modalTransform.numericMode = NumericInputMode::Multiply;
            }
            ImGui::SameLine();
            if (ImGui::Button("/"))
            {
                m_modalTransform.numericMode = NumericInputMode::Divide;
            }
            const char* numericModeText =
                m_modalTransform.numericMode == NumericInputMode::Absolute
                    ? "Absolute"
                    : (m_modalTransform.numericMode == NumericInputMode::Multiply ? "Multiply" : "Divide");
            ImGui::Text("Numeric Mode: %s", numericModeText);
            ImGui::TextWrapped("Numeric: 1, -2.5, *2, /2. Enter confirms, Esc cancels.");
        }
        else
        {
            ImGui::TextWrapped("Press G/R/S with selection to start modal transform.");
            ImGui::TextWrapped("Use X/Y/Z to lock axis, Shift+X/Y/Z to lock plane.");
        }

        ImGui::Separator();
        int combineIndex = static_cast<int>(m_selectionCombineMode);
        const char* combineItems[] = {"Replace", "Add", "Subtract", "Intersect"};
        if (ImGui::Combo("Box/Lasso Combine", &combineIndex, combineItems, IM_ARRAYSIZE(combineItems)))
        {
            combineIndex = std::clamp(combineIndex, 0, 3);
            m_selectionCombineMode = static_cast<SelectionCombineMode>(combineIndex);
        }

        if (m_mode == Mode::LoopEditor)
        {
            ImGui::Separator();
            ImGui::TextUnformatted("Mesh Quick Ops");
            if (ImGui::Button("Extrude (E) Face") && m_meshEditMode == MeshEditMode::Face && m_meshModelSelectedFace >= 0)
            {
                MeshModelerExtrudeFace(m_meshModelSelectedFace, m_meshModelExtrudeDistance);
            }
            ImGui::SameLine();
            if (ImGui::Button("Delete (X) Face") && m_meshEditMode == MeshEditMode::Face && m_meshModelSelectedFace >= 0)
            {
                MeshModelerDeleteFace(m_meshModelSelectedFace);
            }
            if (ImGui::Button("Ctrl+B Bevel Edge(s)") && m_meshEditMode == MeshEditMode::Edge)
            {
                MeshModelerBevelActiveEdges(m_meshModelBevelDistance, m_meshModelBevelSegments);
            }
        }
    }
    if (toolSettingsOpen)
    {
        ImGui::End();
    }

    setPanelRect(centerRect.x, centerRect.y, centerRect.z, centerRect.w);
    ImGui::SetNextWindowBgAlpha(0.08F);
    if (ImGui::Begin("Scene Viewport", nullptr, sceneWindowFlags))
    {
        const bool editModeActive = (m_mode == Mode::LoopEditor) && m_meshModelSceneEditEnabled;
        const char* editSelectionType =
            m_meshEditMode == MeshEditMode::Vertex ? "VERTEX" :
            (m_meshEditMode == MeshEditMode::Edge ? "EDGE" : "FACE");
        const std::string modeDetails = editModeActive ? (" (" + std::string(editSelectionType) + ")") : "";
        const char* modeLabel = editModeActive ? "EDIT MODE" : "OBJECT MODE";
        const char* toolLabel = m_gizmoMode == GizmoMode::Translate ? "MOVE (G)" :
                                (m_gizmoMode == GizmoMode::Rotate ? "ROTATE (R)" : "SCALE (S)");
        int selectedVertexCount = (m_meshModelSelectedVertex >= 0) ? 1 : 0;
        int selectedFaceCount = (m_meshModelSelectedFace >= 0) ? 1 : 0;
        int selectedEdgeCount = 0;
        if (!m_meshModelLoopSelectionEdges.empty())
        {
            selectedEdgeCount = static_cast<int>(m_meshModelLoopSelectionEdges.size());
        }
        else if (!m_meshModelRingSelectionEdges.empty())
        {
            selectedEdgeCount = static_cast<int>(m_meshModelRingSelectionEdges.size());
        }
        else if (m_meshModelSelectedEdge >= 0)
        {
            selectedEdgeCount = 1;
        }

        ImGui::Text("Mode: %s%s", modeLabel, modeDetails.c_str());
        ImGui::Text("Tool: %s | Snap: %s", toolLabel, m_gridSnap ? "ON" : "OFF");
        if (m_mode == Mode::LoopEditor)
        {
            ImGui::Text("ActiveObject: %s (Mesh)", m_meshActiveObjectName.c_str());
        }
        ImGui::Text("Selection Count  V:%d  E:%d  F:%d", selectedVertexCount, selectedEdgeCount, selectedFaceCount);
        ImGui::TextUnformatted("Main scene area (drag assets/loops/prefabs/fx here).");
        ImGui::TextWrapped("Mouse now: LMB select/drag gizmo | MMB orbit | Shift+MMB pan | Wheel zoom");
        ImGui::TextWrapped("Transforms: G/R/S tool, X/Y/Z constraint via gizmo axis pick, Enter/LMB confirm, Esc/RMB cancel");
        if (workspaceMesh)
        {
            ImGui::TextWrapped(
                "Mesh workspace active: Tab switches Object/Edit. In Edit mode use 1/2/3 (Vertex/Edge/Face), Shift+LMB add/remove, Alt+LMB loop select.");
            ImGui::Text("Hover Face/Edge/Vertex: %d / %d / %d", m_meshModelHoveredFace, m_meshModelHoveredEdge, m_meshModelHoveredVertex);
        }
        const ImVec2 dropSize = ImGui::GetContentRegionAvail();
        ImGui::InvisibleButton("##scene_drop_target", dropSize, ImGuiButtonFlags_MouseButtonLeft);
        const ImVec2 dropRectMin = ImGui::GetItemRectMin();
        const ImVec2 dropRectMax = ImGui::GetItemRectMax();
        m_sceneViewportRectMin = glm::vec2{dropRectMin.x, dropRectMin.y};
        m_sceneViewportRectMax = glm::vec2{dropRectMax.x, dropRectMax.y};
        m_sceneViewportHovered = ImGui::IsItemHovered(ImGuiHoveredFlags_RectOnly);
        m_sceneViewportFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
        ImDrawList* fg = ImGui::GetForegroundDrawList();
        if (m_boxSelection.active)
        {
            const ImVec2 a{m_boxSelection.start.x, m_boxSelection.start.y};
            const ImVec2 b{m_boxSelection.end.x, m_boxSelection.end.y};
            fg->AddRectFilled(a, b, IM_COL32(90, 165, 255, 28));
            fg->AddRect(a, b, IM_COL32(130, 195, 255, 220), 0.0F, 0, 2.0F);
        }
        if (m_lassoSelection.active && m_lassoSelection.points.size() >= 2U)
        {
            for (std::size_t i = 1; i < m_lassoSelection.points.size(); ++i)
            {
                const glm::vec2& p0 = m_lassoSelection.points[i - 1U];
                const glm::vec2& p1 = m_lassoSelection.points[i];
                fg->AddLine(ImVec2{p0.x, p0.y}, ImVec2{p1.x, p1.y}, IM_COL32(255, 200, 90, 230), 2.0F);
            }
        }
        if (m_modalTransform.active)
        {
            std::ostringstream modalLabel;
            if (m_modalTransform.tool == GizmoMode::Translate)
            {
                modalLabel << "Move: " << m_modalTransform.previewTranslateMagnitude;
                if (m_modalTransform.constraintMode != ModalConstraintMode::None)
                {
                    modalLabel << " (" << (m_modalTransform.orientation == TransformOrientation::Local ? "Local" : "Global")
                               << ")";
                }
                glm::vec2 pivotScreen{0.0F};
                glm::vec2 endScreen{0.0F};
                if (WorldToScreenPoint(m_modalTransform.pivotWorld, &pivotScreen) &&
                    WorldToScreenPoint(m_modalTransform.pivotWorld + m_modalTransform.previewWorldDelta, &endScreen))
                {
                    fg->AddLine(
                        ImVec2{pivotScreen.x, pivotScreen.y},
                        ImVec2{endScreen.x, endScreen.y},
                        IM_COL32(255, 220, 90, 235),
                        2.5F);
                    fg->AddCircleFilled(ImVec2{endScreen.x, endScreen.y}, 4.0F, IM_COL32(255, 220, 90, 235));
                }
            }
            else if (m_modalTransform.tool == GizmoMode::Rotate)
            {
                modalLabel << "Rotate: " << m_modalTransform.previewRotateDegrees << " deg";
            }
            else
            {
                modalLabel << "Scale: " << m_modalTransform.previewScaleFactor << "x";
            }

            const ImVec2 mouseNow = ImGui::GetMousePos();
            const ImVec2 textPos{mouseNow.x + 18.0F, mouseNow.y + 18.0F};
            const std::string text = modalLabel.str();
            const ImVec2 textSize = ImGui::CalcTextSize(text.c_str());
            const ImVec2 bgMin{textPos.x - 6.0F, textPos.y - 4.0F};
            const ImVec2 bgMax{textPos.x + textSize.x + 6.0F, textPos.y + textSize.y + 4.0F};
            fg->AddRectFilled(bgMin, bgMax, IM_COL32(15, 20, 30, 220), 4.0F);
            fg->AddRect(bgMin, bgMax, IM_COL32(100, 155, 235, 220), 4.0F, 0, 1.0F);
            fg->AddText(textPos, IM_COL32(230, 236, 255, 255), text.c_str());
        }
        if (ImGui::BeginDragDropTarget())
        {
            (void)handleSceneDropPayload();
            ImGui::EndDragDropTarget();
        }
        if (!m_hoveredTileValid)
        {
            ImGui::TextColored(ImVec4(1.0F, 0.75F, 0.3F, 1.0F), "Hover tile not valid currently.");
        }
    }
    ImGui::End();

    const bool outlinerOpen =
        m_blenderLayoutPreset &&
        beginDockedPanel(DockPanel::Outliner, std::max(170.0F, bottomLeftRect.w * 0.52F), "Outliner");
    if (outlinerOpen)
    {
        if (m_mode == Mode::LoopEditor)
        {
            ImGui::TextUnformatted("Mesh Objects");
            const bool selected = true;
            if (ImGui::Selectable(m_meshActiveObjectName.c_str(), selected))
            {
                ClearSelections();
            }
            if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0))
            {
                FocusCameraOnSelection();
            }
            ImGui::Separator();
            ImGui::Text("Active: %s", m_meshActiveObjectName.c_str());
            ImGui::Text("Verts: %d", static_cast<int>(m_meshModelVertices.size()));
            ImGui::Text("Faces: %d", static_cast<int>(m_meshModelFaces.size()));
            ImGui::Text("Mode: %s", m_meshModelSceneEditEnabled ? "Edit" : "Object");
            if (ImGui::Button("Frame Active (.)"))
            {
                FocusCameraOnSelection();
            }
            ImGui::SameLine();
            if (ImGui::Button(m_meshModelSceneEditEnabled ? "Switch Object" : "Switch Edit"))
            {
                ToggleMeshObjectEditMode("outliner");
            }
            if (workspaceAll)
            {
                ImGui::Separator();
                ImGui::TextUnformatted("Loop Elements");
                for (int i = 0; i < static_cast<int>(m_loop.elements.size()); ++i)
                {
                    const LoopElement& element = m_loop.elements[static_cast<std::size_t>(i)];
                    std::string label = "[" + std::to_string(i) + "] " + element.name;
                    if (ImGui::Selectable(label.c_str(), IsSelected(SelectionKind::LoopElement, i)))
                    {
                        SelectSingle(Selection{SelectionKind::LoopElement, i});
                    }
                }
            }
        }
        else
        {
            ImGui::TextUnformatted("Placements");
            for (int i = 0; i < static_cast<int>(m_map.placements.size()); ++i)
            {
                const LoopPlacement& p = m_map.placements[static_cast<std::size_t>(i)];
                std::string label = "[" + std::to_string(i) + "] " + p.loopId;
                if (ImGui::Selectable(label.c_str(), IsSelected(SelectionKind::MapPlacement, i)))
                {
                    SelectSingle(Selection{SelectionKind::MapPlacement, i});
                }
            }
            ImGui::Separator();
            ImGui::TextUnformatted("Props");
            for (int i = 0; i < static_cast<int>(m_map.props.size()); ++i)
            {
                const PropInstance& p = m_map.props[static_cast<std::size_t>(i)];
                std::string label = "[" + std::to_string(i) + "] " + p.name;
                if (ImGui::Selectable(label.c_str(), IsSelected(SelectionKind::Prop, i)))
                {
                    SelectSingle(Selection{SelectionKind::Prop, i});
                }
            }
            ImGui::Separator();
            ImGui::TextUnformatted("Lights");
            for (int i = 0; i < static_cast<int>(m_map.lights.size()); ++i)
            {
                const LightInstance& l = m_map.lights[static_cast<std::size_t>(i)];
                std::string label = "[" + std::to_string(i) + "] " + l.name;
                if (ImGui::Selectable(label.c_str(), m_selectedLightIndex == i))
                {
                    m_selectedLightIndex = i;
                    m_statusLine = "Selected light " + l.name;
                }
            }
        }
    }
    if (outlinerOpen)
    {
        ImGui::End();
    }

    if (showLoopPanels)
    {
        const float loopLibraryH = std::max(180.0F, midH * 0.44F);
        const bool loopLibraryOpen = beginDockedPanel(DockPanel::LoopLibrary, loopLibraryH, "Loop Library");
        if (loopLibraryOpen)
        {
            char searchBuffer[128]{};
            std::snprintf(searchBuffer, sizeof(searchBuffer), "%s", m_loopSearch.c_str());
            if (ImGui::InputText("Search", searchBuffer, sizeof(searchBuffer)))
            {
                m_loopSearch = searchBuffer;
            }

            if (ImGui::Button("Refresh"))
            {
                RefreshLibraries();
            }
            ImGui::SameLine();
            if (ImGui::Button("New"))
            {
                PushHistorySnapshot();
                CreateNewLoop("new_loop");
            }
            ImGui::SameLine();
            if (ImGui::Button("Save Current"))
            {
                saveCurrentLoop();
            }
            if (ImGui::Button("Load Selected") && m_selectedLibraryLoop >= 0 &&
                m_selectedLibraryLoop < static_cast<int>(m_loopLibrary.size()))
            {
                LoopAsset loaded;
                std::string error;
                const std::string& id = m_loopLibrary[static_cast<std::size_t>(m_selectedLibraryLoop)];
                if (LevelAssetIO::LoadLoop(id, &loaded, &error))
                {
                    PushHistorySnapshot();
                    m_loop = loaded;
                    ClearSelections();
                    m_statusLine = "Loaded loop " + id;
                }
                else
                {
                    m_statusLine = "Load failed: " + error;
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Duplicate Selected") && m_selectedLibraryLoop >= 0 &&
                m_selectedLibraryLoop < static_cast<int>(m_loopLibrary.size()))
            {
                LoopAsset loaded;
                std::string error;
                const std::string sourceId = m_loopLibrary[static_cast<std::size_t>(m_selectedLibraryLoop)];
                if (LevelAssetIO::LoadLoop(sourceId, &loaded, &error))
                {
                    loaded.id = sourceId + "_copy";
                    loaded.displayName = loaded.displayName + " Copy";
                    if (LevelAssetIO::SaveLoop(loaded, &error))
                    {
                        RefreshLibraries();
                        m_statusLine = "Duplicated loop " + sourceId;
                    }
                    else
                    {
                        m_statusLine = "Duplicate failed: " + error;
                    }
                }
            }
            if (ImGui::Button("Delete Selected") && m_selectedLibraryLoop >= 0 &&
                m_selectedLibraryLoop < static_cast<int>(m_loopLibrary.size()))
            {
                std::string error;
                const std::string id = m_loopLibrary[static_cast<std::size_t>(m_selectedLibraryLoop)];
                if (LevelAssetIO::DeleteLoop(id, &error))
                {
                    RefreshLibraries();
                    m_selectedLibraryLoop = -1;
                    m_statusLine = "Deleted loop " + id;
                }
                else
                {
                    m_statusLine = "Delete failed: " + error;
                }
            }

            char renameBuffer[128]{};
            std::snprintf(renameBuffer, sizeof(renameBuffer), "%s", m_loopRenameTarget.c_str());
            if (ImGui::InputText("Rename To", renameBuffer, sizeof(renameBuffer)))
            {
                m_loopRenameTarget = renameBuffer;
            }
            if (ImGui::Button("Rename Selected") &&
                m_selectedLibraryLoop >= 0 &&
                m_selectedLibraryLoop < static_cast<int>(m_loopLibrary.size()) &&
                !m_loopRenameTarget.empty())
            {
                std::string error;
                const std::string oldId = m_loopLibrary[static_cast<std::size_t>(m_selectedLibraryLoop)];
                LoopAsset loaded;
                if (LevelAssetIO::LoadLoop(oldId, &loaded, &error))
                {
                    loaded.id = m_loopRenameTarget;
                    loaded.displayName = m_loopRenameTarget;
                    if (LevelAssetIO::SaveLoop(loaded, &error))
                    {
                        const bool deletedOld = LevelAssetIO::DeleteLoop(oldId, nullptr);
                        (void)deletedOld;
                        RefreshLibraries();
                        m_statusLine = "Renamed loop " + oldId + " -> " + m_loopRenameTarget;
                        if (m_loop.id == oldId)
                        {
                            PushHistorySnapshot();
                            m_loop = loaded;
                        }
                    }
                    else
                    {
                        m_statusLine = "Rename failed: " + error;
                    }
                }
            }

            ImGui::Separator();
            for (int i = 0; i < static_cast<int>(m_loopLibrary.size()); ++i)
            {
                const std::string& id = m_loopLibrary[static_cast<std::size_t>(i)];
                if (!ContainsCaseInsensitive(id, m_loopSearch))
                {
                    continue;
                }
                const bool selected = m_selectedLibraryLoop == i;
                if (ImGui::Selectable(id.c_str(), selected))
                {
                    m_selectedLibraryLoop = i;
                }
                if (ImGui::BeginDragDropSource())
                {
                    ImGui::SetDragDropPayload("LOOP_ASSET_ID", id.c_str(), id.size() + 1U);
                    ImGui::Text("Drop loop: %s", id.c_str());
                    ImGui::EndDragDropSource();
                }
            }
        }
        if (loopLibraryOpen)
        {
            ImGui::End();
        }

        const bool loopEditorOpen =
            beginDockedPanel(DockPanel::LoopEditor, std::max(220.0F, midH * 0.52F), "Loop Editor");
        if (loopEditorOpen)
        {
            ImGui::TextWrapped("Quick Guide (Loop Editor):");
            ImGui::BulletText("Add Wall/Window/Pallet/Marker.");
            ImGui::BulletText("Select object with LMB (Ctrl+LMB for multiselect).");
            ImGui::BulletText("Use gizmo mode 1/2/3 then drag axis handle.");
            ImGui::BulletText("1 tile area (16x16) is always visible with strong border.");
            ImGui::BulletText("Save Current to reusable loop asset.");
            ImGui::Separator();
            char idBuffer[128]{};
            std::snprintf(idBuffer, sizeof(idBuffer), "%s", m_loop.id.c_str());
            if (ImGui::InputText("Loop ID", idBuffer, sizeof(idBuffer)))
            {
                m_loop.id = idBuffer;
            }
            char displayNameBuffer[128]{};
            std::snprintf(displayNameBuffer, sizeof(displayNameBuffer), "%s", m_loop.displayName.c_str());
            if (ImGui::InputText("Display Name", displayNameBuffer, sizeof(displayNameBuffer)))
            {
                m_loop.displayName = displayNameBuffer;
            }

            ImGui::Checkbox("Manual Bounds", &m_loop.manualBounds);
            if (m_loop.manualBounds)
            {
                ImGui::DragFloat3("Bounds Min", &m_loop.boundsMin.x, 0.1F);
                ImGui::DragFloat3("Bounds Max", &m_loop.boundsMax.x, 0.1F);
            }
            ImGui::Checkbox("Manual Footprint", &m_loop.manualFootprint);
            if (m_loop.manualFootprint)
            {
                ImGui::InputInt("Footprint Width", &m_loop.footprintWidth);
                ImGui::InputInt("Footprint Height", &m_loop.footprintHeight);
                m_loop.footprintWidth = std::max(1, m_loop.footprintWidth);
                m_loop.footprintHeight = std::max(1, m_loop.footprintHeight);
            }
            else
            {
                ImGui::Text("Footprint: %d x %d", m_loop.footprintWidth, m_loop.footprintHeight);
            }

            if (ImGui::Button("Auto Compute Bounds/Footprint"))
            {
                AutoComputeLoopBoundsAndFootprint();
            }

            ImGui::Separator();
            if (ImGui::Button("Add Wall"))
            {
                PushHistorySnapshot();
                LoopElement element;
                element.type = LoopElementType::Wall;
                element.name = BuildUniqueLoopElementName("wall");
                element.position = glm::vec3{0.0F, 1.0F, 0.0F};
                element.halfExtents = glm::vec3{1.0F, 1.0F, 0.2F};
                m_loop.elements.push_back(element);
                SelectSingle(Selection{SelectionKind::LoopElement, static_cast<int>(m_loop.elements.size()) - 1});
                AutoComputeLoopBoundsAndFootprint();
            }
            ImGui::SameLine();
            if (ImGui::Button("Add Window"))
            {
                PushHistorySnapshot();
                LoopElement element;
                element.type = LoopElementType::Window;
                element.name = BuildUniqueLoopElementName("window");
                element.position = glm::vec3{0.0F, 1.0F, 0.0F};
                element.halfExtents = glm::vec3{0.8F, 0.9F, 0.2F};
                m_loop.elements.push_back(element);
                SelectSingle(Selection{SelectionKind::LoopElement, static_cast<int>(m_loop.elements.size()) - 1});
                AutoComputeLoopBoundsAndFootprint();
            }
            ImGui::SameLine();
            if (ImGui::Button("Add Pallet"))
            {
                PushHistorySnapshot();
                LoopElement element;
                element.type = LoopElementType::Pallet;
                element.name = BuildUniqueLoopElementName("pallet");
                element.position = glm::vec3{0.0F, 0.8F, 0.0F};
                element.halfExtents = glm::vec3{0.8F, 0.8F, 0.25F};
                m_loop.elements.push_back(element);
                SelectSingle(Selection{SelectionKind::LoopElement, static_cast<int>(m_loop.elements.size()) - 1});
                AutoComputeLoopBoundsAndFootprint();
            }
            ImGui::SameLine();
            if (ImGui::Button("Add Marker"))
            {
                PushHistorySnapshot();
                LoopElement element;
                element.type = LoopElementType::Marker;
                element.name = BuildUniqueLoopElementName("marker");
                element.position = glm::vec3{0.0F, 0.5F, 0.0F};
                element.halfExtents = glm::vec3{0.2F, 0.2F, 0.2F};
                element.markerTag = "survivor_spawn";
                m_loop.elements.push_back(element);
                SelectSingle(Selection{SelectionKind::LoopElement, static_cast<int>(m_loop.elements.size()) - 1});
                AutoComputeLoopBoundsAndFootprint();
            }

            ImGui::Separator();
            ImGui::Text("Elements: %d", static_cast<int>(m_loop.elements.size()));
            if (ImGui::BeginListBox("##loop_elements", ImVec2(-1.0F, 170.0F)))
            {
                for (int i = 0; i < static_cast<int>(m_loop.elements.size()); ++i)
                {
                    const LoopElement& element = m_loop.elements[static_cast<std::size_t>(i)];
                    const std::string label =
                        element.name + " [" + LoopElementTypeToText(element.type) + "]" + (element.transformLocked ? " [LOCK]" : "");
                    const bool selected = IsSelected(SelectionKind::LoopElement, i);
                    if (ImGui::Selectable(label.c_str(), selected))
                    {
                        if (ImGui::GetIO().KeyCtrl)
                        {
                            ToggleSelection(Selection{SelectionKind::LoopElement, i});
                        }
                        else
                        {
                            SelectSingle(Selection{SelectionKind::LoopElement, i});
                        }
                    }
                }
                ImGui::EndListBox();
            }

            ImGui::Separator();
            const std::vector<std::string> issues = ValidateLoopForUi();
            if (issues.empty())
            {
                ImGui::TextColored(ImVec4(0.35F, 1.0F, 0.35F, 1.0F), "Validation: OK");
            }
            else
            {
                ImGui::TextColored(ImVec4(1.0F, 0.45F, 0.2F, 1.0F), "Validation:");
                for (const std::string& issue : issues)
                {
                    ImGui::BulletText("%s", issue.c_str());
                }
            }
        }
        if (loopEditorOpen)
        {
            ImGui::End();
        }

        const bool loopInspectorOpen = beginDockedPanel(DockPanel::Inspector, std::max(260.0F, midH * 0.5F), "Inspector");
        if (loopInspectorOpen)
        {
            if (m_selection.kind == SelectionKind::LoopElement &&
                m_selection.index >= 0 && m_selection.index < static_cast<int>(m_loop.elements.size()))
            {
                LoopElement& element = m_loop.elements[static_cast<std::size_t>(m_selection.index)];
                ImGui::Text("Element #%d", m_selection.index);

                char nameBuffer[128]{};
                std::snprintf(nameBuffer, sizeof(nameBuffer), "%s", element.name.c_str());
                if (ImGui::InputText("Name", nameBuffer, sizeof(nameBuffer)))
                {
                    element.name = nameBuffer;
                }

                int typeIndex = static_cast<int>(element.type);
                const char* typeItems[] = {"Wall", "Window", "Pallet", "Marker"};
                if (ImGui::Combo("Type", &typeIndex, typeItems, IM_ARRAYSIZE(typeItems)))
                {
                    typeIndex = std::clamp(typeIndex, 0, 3);
                    element.type = static_cast<LoopElementType>(typeIndex);
                }
                ImGui::DragFloat3("Position", &element.position.x, 0.05F);
                ImGui::DragFloat3("Half Extents", &element.halfExtents.x, 0.05F, 0.05F, 64.0F);
                ImGui::DragFloat3("Rotation (Pitch/Yaw/Roll)", &element.pitchDegrees, 1.0F, -720.0F, 720.0F);
                ImGui::Checkbox("Lock Transform", &element.transformLocked);
                if (element.type == LoopElementType::Marker || element.type == LoopElementType::Window)
                {
                    char tagBuffer[128]{};
                    std::snprintf(tagBuffer, sizeof(tagBuffer), "%s", element.markerTag.c_str());
                    if (ImGui::InputText("Marker/Meta", tagBuffer, sizeof(tagBuffer)))
                    {
                        element.markerTag = tagBuffer;
                    }
                }
                if (ImGui::Button("Delete Element"))
                {
                    DeleteCurrentSelection();
                }
                AutoComputeLoopBoundsAndFootprint();
            }
            else
            {
                ImGui::TextUnformatted("Select a loop element.");
            }
        }
        if (loopInspectorOpen)
        {
            ImGui::End();
        }
    }
    if (showMapPanels)
    {
        const float mapLibraryH = std::max(190.0F, midH * 0.42F);
        const float paletteH = std::max(160.0F, midH * 0.30F);
        const float prefabsH = std::max(120.0F, midH - mapLibraryH - paletteH - pad * 2.0F);

        const bool mapLibraryOpen = beginDockedPanel(DockPanel::MapLibrary, mapLibraryH, "Map Library");
        if (mapLibraryOpen)
        {
            char searchBuffer[128]{};
            std::snprintf(searchBuffer, sizeof(searchBuffer), "%s", m_mapSearch.c_str());
            if (ImGui::InputText("Search", searchBuffer, sizeof(searchBuffer)))
            {
                m_mapSearch = searchBuffer;
            }
            if (ImGui::Button("Refresh"))
            {
                RefreshLibraries();
            }
            ImGui::SameLine();
            if (ImGui::Button("New Map"))
            {
                PushHistorySnapshot();
                CreateNewMap("new_map");
            }
            ImGui::SameLine();
            if (ImGui::Button("Save Current"))
            {
                saveCurrentMap();
            }
            if (ImGui::Button("Load Selected") && m_selectedLibraryMap >= 0 &&
                m_selectedLibraryMap < static_cast<int>(m_mapLibrary.size()))
            {
                MapAsset loaded;
                std::string error;
                const std::string& name = m_mapLibrary[static_cast<std::size_t>(m_selectedLibraryMap)];
                if (LevelAssetIO::LoadMap(name, &loaded, &error))
                {
                    PushHistorySnapshot();
                    m_map = loaded;
                    m_selectedLightIndex = m_map.lights.empty() ? -1 : 0;
                    (void)LevelAssetIO::LoadEnvironment(m_map.environmentAssetId, &m_environmentEditing, nullptr);
                    ClearSelections();
                    m_statusLine = "Loaded map " + name;
                }
                else
                {
                    m_statusLine = "Load map failed: " + error;
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Duplicate Selected") && m_selectedLibraryMap >= 0 &&
                m_selectedLibraryMap < static_cast<int>(m_mapLibrary.size()))
            {
                MapAsset loaded;
                std::string error;
                const std::string sourceName = m_mapLibrary[static_cast<std::size_t>(m_selectedLibraryMap)];
                if (LevelAssetIO::LoadMap(sourceName, &loaded, &error))
                {
                    loaded.name = sourceName + "_copy";
                    if (LevelAssetIO::SaveMap(loaded, &error))
                    {
                        RefreshLibraries();
                        m_statusLine = "Duplicated map " + sourceName;
                    }
                }
            }
            if (ImGui::Button("Delete Selected") && m_selectedLibraryMap >= 0 &&
                m_selectedLibraryMap < static_cast<int>(m_mapLibrary.size()))
            {
                std::string error;
                const std::string name = m_mapLibrary[static_cast<std::size_t>(m_selectedLibraryMap)];
                if (LevelAssetIO::DeleteMap(name, &error))
                {
                    RefreshLibraries();
                    m_selectedLibraryMap = -1;
                    m_statusLine = "Deleted map " + name;
                }
                else
                {
                    m_statusLine = "Delete map failed: " + error;
                }
            }

            char renameBuffer[128]{};
            std::snprintf(renameBuffer, sizeof(renameBuffer), "%s", m_mapRenameTarget.c_str());
            if (ImGui::InputText("Rename To", renameBuffer, sizeof(renameBuffer)))
            {
                m_mapRenameTarget = renameBuffer;
            }
            if (ImGui::Button("Rename Selected") &&
                m_selectedLibraryMap >= 0 &&
                m_selectedLibraryMap < static_cast<int>(m_mapLibrary.size()) &&
                !m_mapRenameTarget.empty())
            {
                std::string error;
                const std::string oldName = m_mapLibrary[static_cast<std::size_t>(m_selectedLibraryMap)];
                MapAsset loaded;
                if (LevelAssetIO::LoadMap(oldName, &loaded, &error))
                {
                    loaded.name = m_mapRenameTarget;
                    if (LevelAssetIO::SaveMap(loaded, &error))
                    {
                        const bool deletedOld = LevelAssetIO::DeleteMap(oldName, nullptr);
                        (void)deletedOld;
                        RefreshLibraries();
                        m_statusLine = "Renamed map " + oldName + " -> " + m_mapRenameTarget;
                        if (m_map.name == oldName)
                        {
                            PushHistorySnapshot();
                            m_map = loaded;
                            (void)LevelAssetIO::LoadEnvironment(m_map.environmentAssetId, &m_environmentEditing, nullptr);
                        }
                    }
                }
            }
            ImGui::Separator();
            for (int i = 0; i < static_cast<int>(m_mapLibrary.size()); ++i)
            {
                const std::string& name = m_mapLibrary[static_cast<std::size_t>(i)];
                if (!ContainsCaseInsensitive(name, m_mapSearch))
                {
                    continue;
                }
                const bool selected = m_selectedLibraryMap == i;
                if (ImGui::Selectable(name.c_str(), selected))
                {
                    m_selectedLibraryMap = i;
                }
            }
        }
        if (mapLibraryOpen)
        {
            ImGui::End();
        }

        const bool loopPaletteOpen = beginDockedPanel(DockPanel::LoopPalette, paletteH, "Loop Palette");
        if (loopPaletteOpen)
        {
            if (ImGui::Button("Refresh Loops"))
            {
                RefreshLibraries();
            }
            ImGui::Text("Selected Loop: %s",
                        (m_paletteLoopIndex >= 0 && m_paletteLoopIndex < static_cast<int>(m_loopLibrary.size()))
                            ? m_loopLibrary[static_cast<std::size_t>(m_paletteLoopIndex)].c_str()
                            : "none");
            if (ImGui::BeginListBox("##loop_palette", ImVec2(-1.0F, 360.0F)))
            {
                for (int i = 0; i < static_cast<int>(m_loopLibrary.size()); ++i)
                {
                    const bool selected = m_paletteLoopIndex == i;
                    const std::string& loopId = m_loopLibrary[static_cast<std::size_t>(i)];
                    if (ImGui::Selectable(loopId.c_str(), selected))
                    {
                        m_paletteLoopIndex = i;
                    }
                    if (ImGui::BeginDragDropSource())
                    {
                        ImGui::SetDragDropPayload("LOOP_ASSET_ID", loopId.c_str(), loopId.size() + 1U);
                        ImGui::Text("Drop loop: %s", loopId.c_str());
                        ImGui::EndDragDropSource();
                    }
                }
                ImGui::EndListBox();
            }
            ImGui::Text("Pending Rotation: %d (R key)", m_pendingPlacementRotation);
            if (ImGui::Button("Place At Hovered"))
            {
                PlaceLoopAtHoveredTile();
            }
            ImGui::SameLine();
            if (ImGui::Button("Remove At Hovered"))
            {
                RemovePlacementAtHoveredTile();
            }
        }
        if (loopPaletteOpen)
        {
            ImGui::End();
        }

        const bool prefabsOpen = beginDockedPanel(DockPanel::Prefabs, prefabsH, "Prefabs");
        if (prefabsOpen)
        {
            char prefabIdBuffer[128]{};
            std::snprintf(prefabIdBuffer, sizeof(prefabIdBuffer), "%s", m_prefabNewId.c_str());
            if (ImGui::InputText("Prefab Id", prefabIdBuffer, sizeof(prefabIdBuffer)))
            {
                m_prefabNewId = prefabIdBuffer;
            }

            if (ImGui::Button("Save Selected Props As Prefab"))
            {
                SaveSelectedPropsAsPrefab(m_prefabNewId);
            }
            ImGui::SameLine();
            if (ImGui::Button("Refresh Prefabs"))
            {
                RefreshLibraries();
            }

            if (ImGui::BeginListBox("##prefab_library", ImVec2(-1.0F, 130.0F)))
            {
                for (int i = 0; i < static_cast<int>(m_prefabLibrary.size()); ++i)
                {
                    const bool selected = m_selectedPrefabIndex == i;
                    const std::string& prefabId = m_prefabLibrary[static_cast<std::size_t>(i)];
                    if (ImGui::Selectable(prefabId.c_str(), selected))
                    {
                        m_selectedPrefabIndex = i;
                    }
                    if (ImGui::BeginDragDropSource())
                    {
                        ImGui::SetDragDropPayload("PREFAB_ASSET_ID", prefabId.c_str(), prefabId.size() + 1U);
                        ImGui::Text("Drop prefab: %s", prefabId.c_str());
                        ImGui::EndDragDropSource();
                    }
                }
                ImGui::EndListBox();
            }

            if (m_selectedPrefabIndex >= 0 && m_selectedPrefabIndex < static_cast<int>(m_prefabLibrary.size()))
            {
                const std::string& selectedPrefab = m_prefabLibrary[static_cast<std::size_t>(m_selectedPrefabIndex)];
                ImGui::Text("Selected: %s", selectedPrefab.c_str());
                if (ImGui::Button("Instantiate At Hovered"))
                {
                    InstantiatePrefabAtHovered(selectedPrefab);
                }
                ImGui::SameLine();
                if (ImGui::Button("Delete Prefab"))
                {
                    std::string error;
                    if (LevelAssetIO::DeletePrefab(selectedPrefab, &error))
                    {
                        m_statusLine = "Deleted prefab " + selectedPrefab;
                        RefreshLibraries();
                    }
                    else
                    {
                        m_statusLine = "Delete prefab failed: " + error;
                    }
                }
            }

            if (ImGui::Button("Reapply Selected Prefab Instance"))
            {
                ReapplySelectedPrefabInstance();
            }
        }
        if (prefabsOpen)
        {
            ImGui::End();
        }

        const bool mapEditorOpen = beginDockedPanel(DockPanel::MapEditor, std::max(220.0F, midH * 0.52F), "Map Editor");
        if (mapEditorOpen)
        {
            ImGui::TextWrapped("Quick Guide (Level/Map Editor):");
            ImGui::BulletText("Select loop from Loop Palette and place on hovered tile.");
            ImGui::BulletText("R rotates pending loop, P toggles prop placement, L toggles light placement.");
            ImGui::BulletText("Props/placements can be selected and transformed.");
            ImGui::BulletText("Quick Loop Objects places Wall/Window/Pallet/Marker as ready 1x1 loop prefabs.");
            ImGui::BulletText("Add Point/Spot lights in Lights section and tune them in Inspector.");
            ImGui::BulletText("Debug View shows extra overlays and placement validation.");
            ImGui::TextWrapped("Place mode: %s", m_propPlacementMode ? "PROP" : "LOOP");
            ImGui::Separator();
            char mapNameBuffer[128]{};
            std::snprintf(mapNameBuffer, sizeof(mapNameBuffer), "%s", m_map.name.c_str());
            if (ImGui::InputText("Map Name", mapNameBuffer, sizeof(mapNameBuffer)))
            {
                m_map.name = mapNameBuffer;
            }
            ImGui::InputInt("Width", &m_map.width);
            ImGui::InputInt("Height", &m_map.height);
            m_map.width = std::max(4, m_map.width);
            m_map.height = std::max(4, m_map.height);
            ImGui::DragFloat("Tile Size", &m_map.tileSize, 0.5F, 4.0F, 64.0F);
            m_map.tileSize = std::max(4.0F, m_map.tileSize);
            ImGui::DragFloat3("Survivor Spawn", &m_map.survivorSpawn.x, 0.1F);
            ImGui::DragFloat3("Killer Spawn", &m_map.killerSpawn.x, 0.1F);
            char environmentBuffer[128]{};
            std::snprintf(environmentBuffer, sizeof(environmentBuffer), "%s", m_map.environmentAssetId.c_str());
            if (ImGui::InputText("Environment Asset", environmentBuffer, sizeof(environmentBuffer)))
            {
                m_map.environmentAssetId = environmentBuffer;
            }
            if (ImGui::Button("Load Environment Asset"))
            {
                if (LevelAssetIO::LoadEnvironment(m_map.environmentAssetId, &m_environmentEditing, nullptr))
                {
                    m_statusLine = "Loaded environment " + m_map.environmentAssetId;
                }
                else
                {
                    m_statusLine = "Failed to load environment " + m_map.environmentAssetId;
                }
            }
            ImGui::Separator();
            ImGui::Text(
                "Placements: %d | Props: %d | Lights: %d",
                static_cast<int>(m_map.placements.size()),
                static_cast<int>(m_map.props.size()),
                static_cast<int>(m_map.lights.size())
            );
            ImGui::Text("Hovered Tile: %s", m_hoveredTileValid ? "valid" : "none");
            if (m_hoveredTileValid)
            {
                ImGui::Text("Tile: (%d, %d)", m_hoveredTile.x, m_hoveredTile.y);
                const glm::vec3 base = TileCenter(m_hoveredTile.x, m_hoveredTile.y);
                ImGui::Text("Hovered World: %.2f %.2f %.2f", base.x, base.y, base.z);
            }
            ImGui::Separator();
            ImGui::TextUnformatted("Lights");
            const bool lightPlacementBefore = m_lightPlacementMode;
            ImGui::Checkbox("Light Placement Mode (LMB)", &m_lightPlacementMode);
            if (!lightPlacementBefore && m_lightPlacementMode)
            {
                m_propPlacementMode = false;
            }
            ImGui::SameLine();
            int lightPlacementType = m_lightPlacementType == LightType::Spot ? 1 : 0;
            const char* lightTypeItems[] = {"Point", "Spot"};
            if (ImGui::Combo("Light Type", &lightPlacementType, lightTypeItems, IM_ARRAYSIZE(lightTypeItems)))
            {
                m_lightPlacementType = lightPlacementType == 1 ? LightType::Spot : LightType::Point;
            }
            if (m_lightPlacementMode)
            {
                ImGui::TextWrapped("LMB places %s light at hovered tile center.", m_lightPlacementType == LightType::Spot ? "Spot" : "Point");
                if (m_hoveredTileValid)
                {
                    const glm::vec3 pos = TileCenter(m_hoveredTile.x, m_hoveredTile.y) +
                                          glm::vec3{0.0F, m_lightPlacementType == LightType::Spot ? 3.0F : 2.5F, 0.0F};
                    ImGui::Text("Preview Pos: %.2f %.2f %.2f", pos.x, pos.y, pos.z);
                }
                else
                {
                    ImGui::TextUnformatted("Move cursor over map tile to place light");
                }
            }
            if (ImGui::Button("Add Point Light"))
            {
                AddLightAtHovered(LightType::Point);
            }
            ImGui::SameLine();
            if (ImGui::Button("Add Spot Light"))
            {
                AddLightAtHovered(LightType::Spot);
            }
            if (ImGui::BeginListBox("##map_lights", ImVec2(-1.0F, 90.0F)))
            {
                for (int i = 0; i < static_cast<int>(m_map.lights.size()); ++i)
                {
                    const LightInstance& light = m_map.lights[static_cast<std::size_t>(i)];
                    const bool selected = m_selectedLightIndex == i;
                    std::string label = light.name + " [" + LightTypeToText(light.type) + "]";
                    if (!light.enabled)
                    {
                        label += " (off)";
                    }
                    if (ImGui::Selectable(label.c_str(), selected))
                    {
                        m_selectedLightIndex = i;
                    }
                }
                ImGui::EndListBox();
            }
            if (m_selectedLightIndex >= 0 && m_selectedLightIndex < static_cast<int>(m_map.lights.size()))
            {
                if (ImGui::Button("Delete Selected Light"))
                {
                    PushHistorySnapshot();
                    m_map.lights.erase(m_map.lights.begin() + m_selectedLightIndex);
                    if (m_map.lights.empty())
                    {
                        m_selectedLightIndex = -1;
                    }
                    else
                    {
                        m_selectedLightIndex = std::min(m_selectedLightIndex, static_cast<int>(m_map.lights.size()) - 1);
                    }
                    m_statusLine = "Deleted light";
                }
            }
            ImGui::Separator();
            ImGui::TextUnformatted("Quick Loop Objects (1x1)");
            int quickLoopType = static_cast<int>(m_quickLoopType);
            const char* quickLoopItems[] = {"Wall", "Window", "Pallet", "Marker"};
            if (ImGui::Combo("Loop Object", &quickLoopType, quickLoopItems, IM_ARRAYSIZE(quickLoopItems)))
            {
                quickLoopType = std::clamp(quickLoopType, 0, 3);
                m_quickLoopType = static_cast<LoopElementType>(quickLoopType);
            }
            ImGui::TextWrapped("Use this to quickly place a single loop object on hovered tile without switching editor mode.");
            if (ImGui::Button("Place Loop Object At Hovered"))
            {
                PlaceQuickLoopObjectAtHovered(m_quickLoopType);
            }
            const bool propPlacementBefore = m_propPlacementMode;
            ImGui::Checkbox("Prop Placement Mode (P)", &m_propPlacementMode);
            if (!propPlacementBefore && m_propPlacementMode)
            {
                m_lightPlacementMode = false;
            }
            if (m_propPlacementMode)
            {
                int propIndex = static_cast<int>(m_selectedPropType);
                const char* propItems[] = {"Rock", "Tree", "Obstacle", "Platform", "MeshAsset"};
                if (ImGui::Combo("Prop Type", &propIndex, propItems, IM_ARRAYSIZE(propItems)))
                {
                    propIndex = std::clamp(propIndex, 0, 4);
                    m_selectedPropType = static_cast<PropType>(propIndex);
                }
                if (ImGui::Button("Add Prop At Hovered"))
                {
                    AddPropAtHoveredTile();
                }
            }
            if (ImGui::Button("Add Small Platform At Hovered"))
            {
                m_selectedPropType = PropType::Platform;
                AddPropAtHoveredTile();
            }
            ImGui::Separator();
            ImGui::TextWrapped("Use central Scene Viewport as drag&drop target.");
        }
        if (mapEditorOpen)
        {
            ImGui::End();
        }

        const bool mapInspectorOpen = beginDockedPanel(DockPanel::Inspector, std::max(220.0F, midH * 0.48F), "Inspector");
        if (mapInspectorOpen)
        {
            if (m_selection.kind == SelectionKind::MapPlacement &&
                m_selection.index >= 0 && m_selection.index < static_cast<int>(m_map.placements.size()))
            {
                LoopPlacement& placement = m_map.placements[static_cast<std::size_t>(m_selection.index)];
                ImGui::Text("Placement #%d", m_selection.index);
                ImGui::Text("Loop: %s", placement.loopId.c_str());
                ImGui::InputInt("Tile X", &placement.tileX);
                ImGui::InputInt("Tile Y", &placement.tileY);
                ImGui::SliderInt("Rotation", &placement.rotationDegrees, 0, 270, "%d deg");
                placement.rotationDegrees = ((placement.rotationDegrees + 45) / 90) * 90;
                placement.rotationDegrees = ((placement.rotationDegrees % 360) + 360) % 360;
                ImGui::Checkbox("Lock Transform", &placement.transformLocked);
                if (ImGui::Button("Delete Placement"))
                {
                    DeleteCurrentSelection();
                }
            }
            else if (m_selection.kind == SelectionKind::Prop &&
                     m_selection.index >= 0 && m_selection.index < static_cast<int>(m_map.props.size()))
            {
                PropInstance& prop = m_map.props[static_cast<std::size_t>(m_selection.index)];
                ImGui::Text("Prop #%d", m_selection.index);
                int propIndex = static_cast<int>(prop.type);
                const char* propItems[] = {"Rock", "Tree", "Obstacle", "Platform", "MeshAsset"};
                if (ImGui::Combo("Type", &propIndex, propItems, IM_ARRAYSIZE(propItems)))
                {
                    propIndex = std::clamp(propIndex, 0, 4);
                    prop.type = static_cast<PropType>(propIndex);
                }
                char propNameBuffer[128]{};
                std::snprintf(propNameBuffer, sizeof(propNameBuffer), "%s", prop.name.c_str());
                if (ImGui::InputText("Name", propNameBuffer, sizeof(propNameBuffer)))
                {
                    prop.name = propNameBuffer;
                }
                ImGui::DragFloat3("Position", &prop.position.x, 0.05F);
                ImGui::DragFloat3("Half Extents", &prop.halfExtents.x, 0.05F, 0.05F, 64.0F);
                ImGui::DragFloat3("Rotation (Pitch/Yaw/Roll)", &prop.pitchDegrees, 1.0F, -720.0F, 720.0F);
                char meshBuffer[256]{};
                std::snprintf(meshBuffer, sizeof(meshBuffer), "%s", prop.meshAsset.c_str());
                if (ImGui::InputText("Mesh Asset", meshBuffer, sizeof(meshBuffer)))
                {
                    prop.meshAsset = meshBuffer;
                }
                char materialBuffer[256]{};
                std::snprintf(materialBuffer, sizeof(materialBuffer), "%s", prop.materialAsset.c_str());
                if (ImGui::InputText("Material Asset", materialBuffer, sizeof(materialBuffer)))
                {
                    prop.materialAsset = materialBuffer;
                }
                char animBuffer[256]{};
                std::snprintf(animBuffer, sizeof(animBuffer), "%s", prop.animationClip.c_str());
                if (ImGui::InputText("Animation Clip", animBuffer, sizeof(animBuffer)))
                {
                    prop.animationClip = animBuffer;
                }
                ImGui::Checkbox("Anim Loop", &prop.animationLoop);
                ImGui::Checkbox("Anim AutoPlay", &prop.animationAutoplay);
                ImGui::DragFloat("Anim Speed", &prop.animationSpeed, 0.05F, 0.01F, 8.0F);
                int colliderType = static_cast<int>(prop.colliderType);
                const char* colliderItems[] = {"None", "Box", "Capsule"};
                if (ImGui::Combo("Collider Type", &colliderType, colliderItems, IM_ARRAYSIZE(colliderItems)))
                {
                    colliderType = std::clamp(colliderType, 0, 2);
                    prop.colliderType = static_cast<ColliderType>(colliderType);
                }
                ImGui::DragFloat3("Collider Offset", &prop.colliderOffset.x, 0.05F);
                ImGui::DragFloat3("Collider HalfExt", &prop.colliderHalfExtents.x, 0.05F, 0.05F, 64.0F);
                ImGui::DragFloat("Collider Radius", &prop.colliderRadius, 0.01F, 0.05F, 8.0F);
                ImGui::DragFloat("Collider Height", &prop.colliderHeight, 0.01F, 0.1F, 16.0F);
                ImGui::Checkbox("Lock Transform", &prop.transformLocked);
                ImGui::Checkbox("Solid", &prop.solid);
                if (ImGui::Button(m_animationPreviewPlaying ? "Stop Preview Animation" : "Play Preview Animation"))
                {
                    m_animationPreviewPlaying = !m_animationPreviewPlaying;
                    if (!m_animationPreviewPlaying)
                    {
                        m_animationPreviewTime = 0.0F;
                    }
                }
                if (ImGui::Button("Delete Prop"))
                {
                    DeleteCurrentSelection();
                }
            }
            else if (m_selectedLightIndex >= 0 && m_selectedLightIndex < static_cast<int>(m_map.lights.size()))
            {
                LightInstance& light = m_map.lights[static_cast<std::size_t>(m_selectedLightIndex)];
                ImGui::Text("Light #%d", m_selectedLightIndex);
                char lightNameBuffer[128]{};
                std::snprintf(lightNameBuffer, sizeof(lightNameBuffer), "%s", light.name.c_str());
                if (ImGui::InputText("Light Name", lightNameBuffer, sizeof(lightNameBuffer)))
                {
                    light.name = lightNameBuffer;
                }
                int typeIndex = light.type == LightType::Spot ? 1 : 0;
                const char* typeItems[] = {"Point", "Spot"};
                if (ImGui::Combo("Light Type", &typeIndex, typeItems, IM_ARRAYSIZE(typeItems)))
                {
                    light.type = typeIndex == 1 ? LightType::Spot : LightType::Point;
                }
                ImGui::Checkbox("Enabled", &light.enabled);
                ImGui::ColorEdit3("Light Color", &light.color.x);
                ImGui::DragFloat3("Light Position", &light.position.x, 0.05F);
                ImGui::DragFloat("Intensity", &light.intensity, 0.05F, 0.0F, 64.0F);
                ImGui::DragFloat("Range", &light.range, 0.1F, 0.1F, 256.0F);
                if (light.type == LightType::Spot)
                {
                    ImGui::DragFloat3("Rotation (Pitch/Yaw/Roll)", &light.rotationEuler.x, 1.0F, -720.0F, 720.0F);
                    ImGui::DragFloat("Inner Angle", &light.spotInnerAngle, 0.2F, 1.0F, 89.0F);
                    ImGui::DragFloat("Outer Angle", &light.spotOuterAngle, 0.2F, 1.5F, 89.5F);
                    light.spotOuterAngle = std::max(light.spotOuterAngle, light.spotInnerAngle + 0.1F);
                }
            }
            else
            {
                ImGui::TextUnformatted("Select map placement, prop or light.");
            }
        }
        if (mapInspectorOpen)
        {
            ImGui::End();
        }
    }

    const bool contentBrowserOpen =
        showContentWindow && beginDockedPanel(DockPanel::ContentBrowser, std::max(220.0F, bottomLeftRect.w * 0.85F), "Content Browser");
    if (contentBrowserOpen)
    {
        auto importIntoDirectory = [this](const std::string& sourcePath, const std::string& targetRelativeDir) -> bool {
            const std::string normalizedTarget = (targetRelativeDir == "." ? std::string{} : targetRelativeDir);
            const engine::assets::ImportResult imported =
                m_assetRegistry.ImportExternalFileToDirectory(sourcePath, normalizedTarget);
            m_statusLine = imported.success ? imported.message : ("Import failed: " + imported.message);
            if (imported.success)
            {
                RefreshLibraries();
                RefreshContentBrowser();
            }
            return imported.success;
        };

#if BUILD_WITH_IMGUI
        auto getFolderPreviewTexture = [this]() -> ImTextureID {
            constexpr const char* kFolderPreviewKey = "__folder_preview__";
            const auto existing = m_contentPreviews.find(kFolderPreviewKey);
            if (existing != m_contentPreviews.end())
            {
                TouchContentPreviewLru(kFolderPreviewKey);
                return existing->second.textureId != 0 ? static_cast<ImTextureID>(existing->second.textureId) : 0;
            }

            ContentPreviewTexture preview{};
            const int width = 96;
            const int height = 96;
            std::vector<unsigned char> pixels(static_cast<std::size_t>(width * height * 4), 255U);
            for (int y = 0; y < height; ++y)
            {
                const float t = static_cast<float>(y) / static_cast<float>(height - 1);
                const glm::vec3 bg = glm::mix(glm::vec3{0.09F, 0.1F, 0.14F}, glm::vec3{0.14F, 0.16F, 0.22F}, t);
                for (int x = 0; x < width; ++x)
                {
                    PutPixel(
                        &pixels,
                        width,
                        height,
                        x,
                        y,
                        std::array<std::uint8_t, 4>{
                            static_cast<std::uint8_t>(bg.r * 255.0F),
                            static_cast<std::uint8_t>(bg.g * 255.0F),
                            static_cast<std::uint8_t>(bg.b * 255.0F),
                            255U,
                        });
                }
            }
            const std::array<std::uint8_t, 4> body{228U, 185U, 82U, 255U};
            const std::array<std::uint8_t, 4> flap{245U, 206U, 109U, 255U};
            for (int y = 28; y < 78; ++y)
            {
                for (int x = 14; x < 84; ++x)
                {
                    PutPixel(&pixels, width, height, x, y, body);
                }
            }
            for (int y = 20; y < 36; ++y)
            {
                for (int x = 22; x < 58; ++x)
                {
                    PutPixel(&pixels, width, height, x, y, flap);
                }
            }
            DrawLineRgba(&pixels, width, height, 14, 28, 84, 28, std::array<std::uint8_t, 4>{255U, 231U, 154U, 255U});
            DrawLineRgba(&pixels, width, height, 14, 78, 84, 78, std::array<std::uint8_t, 4>{168U, 130U, 40U, 255U});
            preview.textureId = CreateTextureRgba8(pixels, width, height);
            preview.width = width;
            preview.height = height;
            preview.failed = preview.textureId == 0;
            m_contentPreviews.emplace(kFolderPreviewKey, preview);
            TouchContentPreviewLru(kFolderPreviewKey);
            return preview.textureId != 0 ? static_cast<ImTextureID>(preview.textureId) : 0;
        };

        auto getContentPreviewTexture = [this](const engine::assets::AssetEntry& entry) -> ImTextureID {
            if (entry.directory)
            {
                return 0;
            }
            const auto existing = m_contentPreviews.find(entry.relativePath);
            if (existing != m_contentPreviews.end())
            {
                TouchContentPreviewLru(entry.relativePath);
                return existing->second.textureId != 0 ? static_cast<ImTextureID>(existing->second.textureId) : 0;
            }

            ContentPreviewTexture preview{};
            std::vector<unsigned char> pixels;
            int width = 0;
            int height = 0;

            const std::filesystem::path absolute = m_assetRegistry.AbsolutePath(entry.relativePath);
            if (entry.kind == engine::assets::AssetKind::Texture)
            {
                int infoWidth = 0;
                int infoHeight = 0;
                int infoChannels = 0;
                constexpr std::int64_t kMaxPreviewPixels = 16LL * 1024LL * 1024LL;
                if (stbi_info(absolute.string().c_str(), &infoWidth, &infoHeight, &infoChannels) != 0 &&
                    infoWidth > 0 && infoHeight > 0 &&
                    static_cast<std::int64_t>(infoWidth) * static_cast<std::int64_t>(infoHeight) <= kMaxPreviewPixels)
                {
                    int channels = 0;
                    unsigned char* data = stbi_load(absolute.string().c_str(), &width, &height, &channels, 4);
                    if (data != nullptr && width > 0 && height > 0)
                    {
                        constexpr int kThumbSize = 96;
                        std::vector<unsigned char> source(data, data + static_cast<std::size_t>(width * height * 4));
                        pixels.assign(static_cast<std::size_t>(kThumbSize * kThumbSize * 4), 0U);
                        for (int y = 0; y < kThumbSize; ++y)
                        {
                            const int srcY = glm::clamp((y * height) / kThumbSize, 0, height - 1);
                            for (int x = 0; x < kThumbSize; ++x)
                            {
                                const int srcX = glm::clamp((x * width) / kThumbSize, 0, width - 1);
                                const std::size_t srcIdx = static_cast<std::size_t>((srcY * width + srcX) * 4);
                                const std::size_t dstIdx = static_cast<std::size_t>((y * kThumbSize + x) * 4);
                                pixels[dstIdx + 0] = source[srcIdx + 0];
                                pixels[dstIdx + 1] = source[srcIdx + 1];
                                pixels[dstIdx + 2] = source[srcIdx + 2];
                                pixels[dstIdx + 3] = source[srcIdx + 3];
                            }
                        }
                        width = kThumbSize;
                        height = kThumbSize;
                    }
                    stbi_image_free(data);
                }
            }
            else if (entry.kind == engine::assets::AssetKind::Material)
            {
                MaterialAsset material;
                const std::string materialId = std::filesystem::path(entry.relativePath).stem().string();
                if (LevelAssetIO::LoadMaterial(materialId, &material, nullptr))
                {
                    width = 96;
                    height = 96;
                    pixels = BuildMaterialSphereThumbnailRgba(material, width, height);
                }
            }
            else if (entry.kind == engine::assets::AssetKind::Mesh)
            {
                std::string meshError;
                if (const engine::assets::MeshData* meshData = m_meshLibrary.LoadMesh(absolute, &meshError);
                    meshData != nullptr && meshData->loaded)
                {
                    width = 96;
                    height = 96;
                    pixels = BuildMeshThumbnailRgba(*meshData, width, height);
                }
            }

            if (!pixels.empty() && width > 0 && height > 0)
            {
                preview.textureId = CreateTextureRgba8(pixels, width, height);
                preview.width = width;
                preview.height = height;
                preview.failed = preview.textureId == 0;
            }
            else
            {
                preview.failed = true;
            }
            m_contentPreviews.emplace(entry.relativePath, preview);
            TouchContentPreviewLru(entry.relativePath);
            if (preview.textureId != 0)
            {
                return static_cast<ImTextureID>(preview.textureId);
            }
            return 0;
        };
#endif

        if (m_contentNeedsRefresh)
        {
            RefreshContentBrowser();
        }
        m_contentBrowserHovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows);

        ImGui::Text("Directory: %s", m_contentDirectory.c_str());
        if (ImGui::Button("Up"))
        {
            if (m_contentDirectory != "." && !m_contentDirectory.empty())
            {
                std::filesystem::path p = m_contentDirectory;
                const std::filesystem::path parent = p.parent_path();
                m_contentDirectory = parent.empty() ? "." : parent.generic_string();
                m_contentNeedsRefresh = true;
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Refresh Assets"))
        {
            RefreshLibraries();
            RefreshContentBrowser();
        }
        ImGui::SameLine();
        if (ImGui::Button("Import (Browse...)"))
        {
            const std::vector<std::string> pickedFiles = OpenMultipleFileDialog();
            if (!pickedFiles.empty())
            {
                int importedCount = 0;
                for (const std::string& picked : pickedFiles)
                {
                    m_contentImportPath = picked;
                    if (importIntoDirectory(m_contentImportPath, m_contentDirectory))
                    {
                        ++importedCount;
                    }
                }
                std::ostringstream oss;
                oss << "Imported " << importedCount << "/" << pickedFiles.size() << " file(s) into " << m_contentDirectory;
                m_statusLine = oss.str();
            }
            else
            {
#if !defined(_WIN32)
                m_statusLine = "System file dialog is currently implemented for Windows. Use Import Path on this platform.";
#endif
            }
        }

        char importBuffer[260]{};
        std::snprintf(importBuffer, sizeof(importBuffer), "%s", m_contentImportPath.c_str());
        if (ImGui::InputText("Import Path", importBuffer, sizeof(importBuffer)))
        {
            m_contentImportPath = importBuffer;
        }
        if (ImGui::Button("Import File To Current Folder"))
        {
            (void)importIntoDirectory(m_contentImportPath, m_contentDirectory);
        }

        char folderBuffer[128]{};
        std::snprintf(folderBuffer, sizeof(folderBuffer), "%s", m_contentNewFolderName.c_str());
        if (ImGui::InputText("New Folder", folderBuffer, sizeof(folderBuffer)))
        {
            m_contentNewFolderName = folderBuffer;
        }
        if (ImGui::Button("Create Folder"))
        {
            const std::string relative = (m_contentDirectory == "." ? "" : (m_contentDirectory + "/")) + m_contentNewFolderName;
            std::string error;
            if (m_assetRegistry.CreateFolder(relative, &error))
            {
                m_statusLine = "Created folder " + relative;
                RefreshContentBrowser();
            }
            else
            {
                m_statusLine = "Create folder failed: " + error;
            }
        }

        char renameBuffer[256]{};
        std::snprintf(renameBuffer, sizeof(renameBuffer), "%s", m_contentRenameTarget.c_str());
        if (ImGui::InputText("Rename Selected To", renameBuffer, sizeof(renameBuffer)))
        {
            m_contentRenameTarget = renameBuffer;
        }
        if (ImGui::Button("Rename Selected") && m_selectedContentEntry >= 0 && m_selectedContentEntry < static_cast<int>(m_contentEntries.size()))
        {
            const std::string from = m_contentEntries[static_cast<std::size_t>(m_selectedContentEntry)].relativePath;
            std::filesystem::path targetPath = std::filesystem::path(from).parent_path() / m_contentRenameTarget;
            std::string error;
            if (m_assetRegistry.RenamePath(from, targetPath.generic_string(), &error))
            {
                m_statusLine = "Renamed asset";
                RefreshContentBrowser();
            }
            else
            {
                m_statusLine = "Rename failed: " + error;
            }
        }
        if (ImGui::Button("Delete Selected") && m_selectedContentEntry >= 0 && m_selectedContentEntry < static_cast<int>(m_contentEntries.size()))
        {
            std::string error;
            if (m_assetRegistry.DeletePath(m_contentEntries[static_cast<std::size_t>(m_selectedContentEntry)].relativePath, &error))
            {
                m_statusLine = "Deleted asset";
                m_selectedContentEntry = -1;
                m_selectedContentPath.clear();
                RefreshContentBrowser();
            }
            else
            {
                m_statusLine = "Delete failed: " + error;
            }
        }

        ImGui::Separator();
        std::string hoveredFolderTarget;
        const float thumbSize = 82.0F;
        const float cellWidth = 120.0F;
        const int columns = glm::max(1, static_cast<int>(std::floor(ImGui::GetContentRegionAvail().x / cellWidth)));
        ImGui::BeginChild("##content_grid", ImVec2(-1.0F, 280.0F), true);
        for (int i = 0; i < static_cast<int>(m_contentEntries.size()); ++i)
        {
                const auto& entry = m_contentEntries[static_cast<std::size_t>(i)];
                ImGui::PushID(i);
                ImGui::BeginGroup();
                const bool selected = m_selectedContentEntry == i;
                if (selected)
                {
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.22F, 0.42F, 0.68F, 0.9F));
                }
                if (entry.directory)
                {
#if BUILD_WITH_IMGUI
                    const ImTextureID folderTexture = getFolderPreviewTexture();
                    const bool clickedFolderThumb = folderTexture != 0
                                                        ? ImGui::ImageButton("##folder_thumb", folderTexture, ImVec2(thumbSize, thumbSize))
                                                        : ImGui::Button("[Folder]", ImVec2(thumbSize, thumbSize));
#else
                    const bool clickedFolderThumb = ImGui::Button("[Folder]", ImVec2(thumbSize, thumbSize));
#endif
                    if (clickedFolderThumb)
                    {
                        m_selectedContentEntry = i;
                        m_selectedContentPath = entry.relativePath;
                    }
                }
                else
                {
#if BUILD_WITH_IMGUI
                    ImTextureID textureId = getContentPreviewTexture(entry);
#else
                    ImTextureID textureId = 0;
#endif
                    if (textureId != 0)
                    {
                        if (ImGui::ImageButton("##asset_thumb", textureId, ImVec2(thumbSize, thumbSize)))
                        {
                            m_selectedContentEntry = i;
                            m_selectedContentPath = entry.relativePath;
                        }
                    }
                    else
                    {
                        const std::string fallback = std::string("[") + AssetKindToText(entry.kind) + "]";
                        if (ImGui::Button(fallback.c_str(), ImVec2(thumbSize, thumbSize)))
                        {
                            m_selectedContentEntry = i;
                            m_selectedContentPath = entry.relativePath;
                        }
                    }
                }
                if (selected)
                {
                    ImGui::PopStyleColor();
                }

                if (ImGui::IsItemHovered())
                {
                    if (entry.directory)
                    {
                        hoveredFolderTarget = entry.relativePath;
                    }
                    if (ImGui::IsMouseDoubleClicked(0) && entry.directory)
                    {
                        m_contentDirectory = entry.relativePath;
                        m_contentNeedsRefresh = true;
                    }
                }
                if (!entry.directory && ImGui::BeginDragDropSource())
                {
                    const std::string payloadPath = entry.relativePath;
                    ImGui::SetDragDropPayload("CONTENT_ASSET_PATH", payloadPath.c_str(), payloadPath.size() + 1);
                    ImGui::Text("Drop: %s", payloadPath.c_str());
                    ImGui::EndDragDropSource();
                }

                ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + cellWidth - 8.0F);
                ImGui::TextUnformatted(entry.name.c_str());
                ImGui::PopTextWrapPos();
                ImGui::TextDisabled("%s", entry.directory ? "Folder" : AssetKindToText(entry.kind));
                ImGui::EndGroup();
                ImGui::PopID();
                if (((i + 1) % columns) != 0)
                {
                    ImGui::SameLine();
                }
        }
        ImGui::EndChild();

        if (m_contentBrowserHovered && !m_pendingExternalDrops.empty())
        {
            const std::string targetDir = hoveredFolderTarget.empty() ? m_contentDirectory : hoveredFolderTarget;
            int importedCount = 0;
            int failedCount = 0;
            std::string lastError;
            for (const std::string& sourcePath : m_pendingExternalDrops)
            {
                const engine::assets::ImportResult imported =
                    m_assetRegistry.ImportExternalFileToDirectory(sourcePath, targetDir == "." ? std::string{} : targetDir);
                if (imported.success)
                {
                    ++importedCount;
                }
                else
                {
                    ++failedCount;
                    lastError = imported.message;
                }
            }
            m_pendingExternalDrops.clear();
            RefreshLibraries();
            RefreshContentBrowser();
            std::ostringstream oss;
            oss << "Dropped import -> " << targetDir << ": " << importedCount << " ok";
            if (failedCount > 0)
            {
                oss << ", " << failedCount << " failed";
                if (!lastError.empty())
                {
                    oss << " (" << lastError << ")";
                }
            }
            m_statusLine = oss.str();
        }
        else if (!m_pendingExternalDrops.empty())
        {
            ImGui::TextColored(
                ImVec4(1.0F, 0.85F, 0.25F, 1.0F),
                "Dropped files pending: %d (hover Content Browser/folder to import here)",
                static_cast<int>(m_pendingExternalDrops.size()));
        }

        ImGui::TextWrapped("Selected: %s", m_selectedContentPath.empty() ? "none" : m_selectedContentPath.c_str());
        if (!m_selectedContentPath.empty())
        {
            const engine::assets::AssetKind kind = engine::assets::AssetRegistry::KindFromPath(std::filesystem::path(m_selectedContentPath));
            ImGui::Text("Kind: %s", AssetKindToText(kind));
            if (kind == engine::assets::AssetKind::Mesh)
            {
                std::string err;
                const engine::assets::MeshData* md = m_meshLibrary.LoadMesh(m_assetRegistry.AbsolutePath(m_selectedContentPath), &err);
                if (md != nullptr && md->loaded)
                {
                    const glm::vec3 size = md->boundsMax - md->boundsMin;
                    ImGui::Text("Mesh verts: %d tris: %d",
                                static_cast<int>(md->geometry.positions.size()),
                                static_cast<int>(md->geometry.indices.size() / 3));
                    ImGui::Text("Bounds size: %.2f %.2f %.2f", size.x, size.y, size.z);
                }
                else if (!err.empty())
                {
                    ImGui::TextWrapped("Mesh load: %s", err.c_str());
                }
            }
        }
        if (m_mode == Mode::MapEditor && !m_selectedContentPath.empty() && ImGui::Button("Place Selected Asset At Hovered"))
        {
            PlaceImportedAssetAtHovered(m_selectedContentPath);
        }
    }
    if (contentBrowserOpen)
    {
        ImGui::End();
    }

    const bool materialWindowOpen =
        showMaterialWindow &&
        beginDockedPanel(DockPanel::MaterialsEnvironment, std::max(260.0F, rightRect.w * 0.55F), "Materials & Environment");
    if (materialWindowOpen)
    {
        if (ImGui::CollapsingHeader("Material Editor", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::TextUnformatted("Material Library");
            if (ImGui::BeginListBox("##material_library", ImVec2(-1.0F, 90.0F)))
            {
                for (int i = 0; i < static_cast<int>(m_materialLibrary.size()); ++i)
                {
                    const bool selected = m_selectedMaterialIndex == i;
                    if (ImGui::Selectable(m_materialLibrary[static_cast<std::size_t>(i)].c_str(), selected))
                    {
                        m_selectedMaterialIndex = i;
                        m_selectedMaterialId = m_materialLibrary[static_cast<std::size_t>(i)];
                    }
                }
                ImGui::EndListBox();
            }
            if (ImGui::Button("New Material"))
            {
                m_materialEditing = MaterialAsset{};
                m_materialEditing.id = "new_material";
                m_materialEditing.displayName = "New Material";
                m_materialDirty = true;
            }
            ImGui::SameLine();
            if (ImGui::Button("Load Selected Material") && !m_selectedMaterialId.empty())
            {
                if (LevelAssetIO::LoadMaterial(m_selectedMaterialId, &m_materialEditing, nullptr))
                {
                    m_materialDirty = false;
                    m_statusLine = "Loaded material " + m_selectedMaterialId;
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Delete Selected Material") && !m_selectedMaterialId.empty())
            {
                std::string error;
                if (LevelAssetIO::DeleteMaterial(m_selectedMaterialId, &error))
                {
                    m_statusLine = "Deleted material " + m_selectedMaterialId;
                    if (m_materialEditing.id == m_selectedMaterialId)
                    {
                        m_materialEditing = MaterialAsset{};
                        m_materialEditing.id = "new_material";
                        m_materialEditing.displayName = "New Material";
                        m_materialDirty = false;
                    }
                    RefreshLibraries();
                    m_contentNeedsRefresh = true;
                    m_materialCache.clear();
                }
                else
                {
                    m_statusLine = "Delete material failed: " + error;
                }
            }
            if (ImGui::Button("Assign Material To Selected Props") && !m_selectedMaterialId.empty())
            {
                const std::vector<int> selectedProps = SortedUniqueValidSelection(SelectionKind::Prop);
                if (selectedProps.empty())
                {
                    m_statusLine = "Select prop(s) first";
                }
                else
                {
                    PushHistorySnapshot();
                    for (int idx : selectedProps)
                    {
                        if (idx >= 0 && idx < static_cast<int>(m_map.props.size()))
                        {
                            m_map.props[static_cast<std::size_t>(idx)].materialAsset = m_selectedMaterialId;
                        }
                    }
                    std::ostringstream oss;
                    oss << "Assigned material " << m_selectedMaterialId << " to " << selectedProps.size() << " prop(s)";
                    m_statusLine = oss.str();
                }
            }

            ImGui::Separator();
            char materialIdBuffer[128]{};
            std::snprintf(materialIdBuffer, sizeof(materialIdBuffer), "%s", m_materialEditing.id.c_str());
            if (ImGui::InputText("Material Id", materialIdBuffer, sizeof(materialIdBuffer)))
            {
                m_materialEditing.id = materialIdBuffer;
                m_materialDirty = true;
            }
            char materialNameBuffer[128]{};
            std::snprintf(materialNameBuffer, sizeof(materialNameBuffer), "%s", m_materialEditing.displayName.c_str());
            if (ImGui::InputText("Material Name", materialNameBuffer, sizeof(materialNameBuffer)))
            {
                m_materialEditing.displayName = materialNameBuffer;
                m_materialDirty = true;
            }
            int shaderType = static_cast<int>(m_materialEditing.shaderType);
            const char* shaderTypes[] = {"Lit", "Unlit"};
            if (ImGui::Combo("Shader Type", &shaderType, shaderTypes, IM_ARRAYSIZE(shaderTypes)))
            {
                m_materialEditing.shaderType = static_cast<MaterialShaderType>(std::clamp(shaderType, 0, 1));
                m_materialDirty = true;
            }
            if (ImGui::ColorEdit4("Base Color", &m_materialEditing.baseColor.x))
            {
                m_materialDirty = true;
            }
            if (ImGui::DragFloat("Roughness", &m_materialEditing.roughness, 0.01F, 0.0F, 1.0F))
            {
                m_materialDirty = true;
            }
            if (ImGui::DragFloat("Metallic", &m_materialEditing.metallic, 0.01F, 0.0F, 1.0F))
            {
                m_materialDirty = true;
            }
            if (ImGui::DragFloat("Emissive", &m_materialEditing.emissiveStrength, 0.01F, 0.0F, 8.0F))
            {
                m_materialDirty = true;
            }

            char albedoBuffer[256]{};
            std::snprintf(albedoBuffer, sizeof(albedoBuffer), "%s", m_materialEditing.albedoTexture.c_str());
            if (ImGui::InputText("Albedo Texture", albedoBuffer, sizeof(albedoBuffer)))
            {
                m_materialEditing.albedoTexture = albedoBuffer;
                m_materialDirty = true;
            }
            if (ImGui::Button("Save Material"))
            {
                std::string error;
                if (LevelAssetIO::SaveMaterial(m_materialEditing, &error))
                {
                    m_statusLine = "Saved material " + m_materialEditing.id;
                    m_selectedMaterialId = m_materialEditing.id;
                    m_materialCache.clear();
                    m_materialDirty = false;
                    m_contentNeedsRefresh = true;
                    RefreshLibraries();
                }
                else
                {
                    m_statusLine = "Save material failed: " + error;
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Load Material") && !m_selectedMaterialId.empty())
            {
                (void)LevelAssetIO::LoadMaterial(m_selectedMaterialId, &m_materialEditing, nullptr);
                m_materialDirty = false;
            }
            if (m_materialDirty)
            {
                ImGui::TextUnformatted("* Material has unsaved changes");
            }

            ImGui::Separator();
            if (ImGui::CollapsingHeader("Material Lab Controls", ImGuiTreeNodeFlags_DefaultOpen))
            {
                ImGui::TextUnformatted("Dedicated preview scene for current material (sphere + controllable lights).");
                int labViewMode = static_cast<int>(m_materialLabViewMode);
                const char* labViewItems[] = {"Off", "Overlay (on world)", "Dedicated Scene"};
                if (ImGui::Combo("Material Lab View", &labViewMode, labViewItems, IM_ARRAYSIZE(labViewItems)))
                {
                    labViewMode = std::clamp(labViewMode, 0, 2);
                    m_materialLabViewMode = static_cast<MaterialLabViewMode>(labViewMode);
                    if (m_materialLabViewMode == MaterialLabViewMode::Dedicated)
                    {
                        m_cameraPosition = glm::vec3{0.0F, m_materialLabSphereRadius + 1.3F, 5.0F};
                        m_cameraYaw = glm::radians(180.0F);
                        m_cameraPitch = -0.1F;
                        m_statusLine = "Material Lab: switched to dedicated scene.";
                    }
                }
                ImGui::Checkbox("Lighting Enabled", &m_materialLabLightingEnabled);
                ImGui::Checkbox("Directional Light", &m_materialLabDirectionalLightEnabled);
                ImGui::Checkbox("Point Lights", &m_materialLabPointLightsEnabled);
                ImGui::Checkbox("Backdrop (floor+wall)", &m_materialLabBackdropEnabled);
                ImGui::Checkbox("Auto Rotate Sphere", &m_materialLabAutoRotate);
                if (!m_materialLabAutoRotate)
                {
                    ImGui::SliderFloat("Manual Yaw", &m_materialLabManualYaw, -180.0F, 180.0F, "%.1f deg");
                }
                else
                {
                    ImGui::SliderFloat("Auto Rotate Speed", &m_materialLabAutoRotateSpeed, 0.0F, 180.0F, "%.1f deg/s");
                }

                ImGui::SliderFloat("Preview Distance", &m_materialLabDistance, 1.5F, 12.0F, "%.2f");
                ImGui::SliderFloat("Preview Height Offset", &m_materialLabHeight, -4.0F, 4.0F, "%.2f");
                ImGui::SliderFloat("Sphere Radius", &m_materialLabSphereRadius, 0.2F, 2.0F, "%.2f");
                ImGui::SliderFloat("Directional Intensity", &m_materialLabDirectionalIntensity, 0.0F, 5.0F, "%.2f");
                ImGui::SliderFloat("Point Intensity", &m_materialLabPointIntensity, 0.0F, 16.0F, "%.2f");
                ImGui::SliderFloat("Point Range", &m_materialLabPointRange, 1.0F, 30.0F, "%.2f");

                int labRenderMode = (m_currentRenderMode == engine::render::RenderMode::Wireframe) ? 0 : 1;
                const char* renderItems[] = {"Wireframe", "Filled"};
                if (ImGui::Combo("Preview Render Mode", &labRenderMode, renderItems, IM_ARRAYSIZE(renderItems)))
                {
                    m_pendingRenderMode =
                        (labRenderMode == 0) ? engine::render::RenderMode::Wireframe : engine::render::RenderMode::Filled;
                    m_currentRenderMode = *m_pendingRenderMode;
                }
                ImGui::Checkbox("Force Filled For Material Lab", &m_materialLabForceFilled);
                if (m_materialLabForceFilled && m_materialLabViewMode != MaterialLabViewMode::Off &&
                    m_currentRenderMode != engine::render::RenderMode::Filled)
                {
                    m_pendingRenderMode = engine::render::RenderMode::Filled;
                    m_currentRenderMode = engine::render::RenderMode::Filled;
                }
                ImGui::Text("View Mode: %s", MaterialLabViewModeToText(m_materialLabViewMode));
                ImGui::Text("Current Material: %s", m_materialEditing.id.c_str());
                ImGui::Text("R/M/E = %.2f / %.2f / %.2f",
                            m_materialEditing.roughness,
                            m_materialEditing.metallic,
                            m_materialEditing.emissiveStrength);
                if (ImGui::Button("Align Camera To Lab"))
                {
                    if (m_materialLabViewMode == MaterialLabViewMode::Dedicated)
                    {
                        m_cameraPosition = glm::vec3{0.0F, m_materialLabSphereRadius + 1.3F, 5.0F};
                        m_cameraYaw = glm::radians(180.0F);
                        m_cameraPitch = -0.1F;
                    }
                    else
                    {
                        m_cameraPitch = -0.18F;
                        m_cameraYaw = 0.0F;
                    }
                    m_statusLine = "Material Lab camera aligned.";
                }
                ImGui::SameLine();
                if (ImGui::Button("Reset Lab Defaults"))
                {
                    m_materialLabViewMode = MaterialLabViewMode::Dedicated;
                    m_materialLabLightingEnabled = true;
                    m_materialLabDirectionalLightEnabled = true;
                    m_materialLabPointLightsEnabled = true;
                    m_materialLabAutoRotate = true;
                    m_materialLabForceFilled = true;
                    m_materialLabBackdropEnabled = true;
                    m_materialLabDistance = 4.6F;
                    m_materialLabHeight = -0.5F;
                    m_materialLabSphereRadius = 0.75F;
                    m_materialLabAutoRotateSpeed = 26.0F;
                    m_materialLabManualYaw = 0.0F;
                    m_materialLabDirectionalIntensity = 1.2F;
                    m_materialLabPointIntensity = 5.5F;
                    m_materialLabPointRange = 12.0F;
                    m_statusLine = "Material Lab defaults restored.";
                }
            }
        }

        if (ImGui::CollapsingHeader("Animation Clip Editor", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::TextUnformatted("Animation Clips");
            if (ImGui::BeginListBox("##animation_library", ImVec2(-1.0F, 90.0F)))
            {
                for (int i = 0; i < static_cast<int>(m_animationLibrary.size()); ++i)
                {
                    const bool selected = m_selectedAnimationIndex == i;
                    if (ImGui::Selectable(m_animationLibrary[static_cast<std::size_t>(i)].c_str(), selected))
                    {
                        m_selectedAnimationIndex = i;
                        m_animationPreviewClip = m_animationLibrary[static_cast<std::size_t>(i)];
                    }
                }
                ImGui::EndListBox();
            }
            if (ImGui::Button("New Clip"))
            {
                m_animationEditing = AnimationClipAsset{};
                m_animationEditing.id = "new_clip";
                m_animationEditing.displayName = "New Clip";
                m_animationEditing.keyframes = {AnimationKeyframe{}};
                m_animationDirty = true;
            }
            ImGui::SameLine();
            if (ImGui::Button("Load Selected Clip") && m_selectedAnimationIndex >= 0 &&
                m_selectedAnimationIndex < static_cast<int>(m_animationLibrary.size()))
            {
                const std::string& clipId = m_animationLibrary[static_cast<std::size_t>(m_selectedAnimationIndex)];
                if (LevelAssetIO::LoadAnimationClip(clipId, &m_animationEditing, nullptr))
                {
                    m_animationPreviewClip = clipId;
                    m_animationDirty = false;
                    m_statusLine = "Loaded animation clip " + clipId;
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Delete Selected Clip") && m_selectedAnimationIndex >= 0 &&
                m_selectedAnimationIndex < static_cast<int>(m_animationLibrary.size()))
            {
                const std::string clipId = m_animationLibrary[static_cast<std::size_t>(m_selectedAnimationIndex)];
                std::string error;
                if (LevelAssetIO::DeleteAnimationClip(clipId, &error))
                {
                    m_statusLine = "Deleted animation clip " + clipId;
                    if (m_animationEditing.id == clipId)
                    {
                        m_animationEditing = AnimationClipAsset{};
                        m_animationEditing.id = "new_clip";
                        m_animationEditing.displayName = "New Clip";
                    }
                    RefreshLibraries();
                    m_contentNeedsRefresh = true;
                    m_animationCache.clear();
                }
                else
                {
                    m_statusLine = "Delete animation failed: " + error;
                }
            }
            if (ImGui::Button("Assign Clip To Selected Props") && !m_animationPreviewClip.empty())
            {
                const std::vector<int> selectedProps = SortedUniqueValidSelection(SelectionKind::Prop);
                if (selectedProps.empty())
                {
                    m_statusLine = "Select prop(s) first";
                }
                else
                {
                    PushHistorySnapshot();
                    for (int idx : selectedProps)
                    {
                        if (idx >= 0 && idx < static_cast<int>(m_map.props.size()))
                        {
                            m_map.props[static_cast<std::size_t>(idx)].animationClip = m_animationPreviewClip;
                        }
                    }
                    std::ostringstream oss;
                    oss << "Assigned animation " << m_animationPreviewClip << " to " << selectedProps.size() << " prop(s)";
                    m_statusLine = oss.str();
                }
            }

            ImGui::Separator();
            char clipIdBuffer[128]{};
            std::snprintf(clipIdBuffer, sizeof(clipIdBuffer), "%s", m_animationEditing.id.c_str());
            if (ImGui::InputText("Clip Id", clipIdBuffer, sizeof(clipIdBuffer)))
            {
                m_animationEditing.id = clipIdBuffer;
                m_animationDirty = true;
            }
            char clipNameBuffer[128]{};
            std::snprintf(clipNameBuffer, sizeof(clipNameBuffer), "%s", m_animationEditing.displayName.c_str());
            if (ImGui::InputText("Clip Name", clipNameBuffer, sizeof(clipNameBuffer)))
            {
                m_animationEditing.displayName = clipNameBuffer;
                m_animationDirty = true;
            }
            if (ImGui::Checkbox("Clip Loop", &m_animationEditing.loop))
            {
                m_animationDirty = true;
            }
            if (ImGui::DragFloat("Clip Speed", &m_animationEditing.speed, 0.02F, 0.01F, 8.0F))
            {
                m_animationDirty = true;
            }

            if (m_animationEditing.keyframes.empty())
            {
                m_animationEditing.keyframes.push_back(AnimationKeyframe{});
                m_animationDirty = true;
            }

            int removeKeyframe = -1;
            ImGui::BeginChild("clip_keyframes", ImVec2(0.0F, 160.0F), true);
            for (int i = 0; i < static_cast<int>(m_animationEditing.keyframes.size()); ++i)
            {
                AnimationKeyframe& key = m_animationEditing.keyframes[static_cast<std::size_t>(i)];
                ImGui::PushID(i);
                ImGui::Text("Keyframe %d", i);
                ImGui::SameLine();
                if (ImGui::SmallButton("Remove"))
                {
                    removeKeyframe = i;
                }
                if (ImGui::DragFloat("Time", &key.time, 0.01F, 0.0F, 999.0F, "%.2f"))
                {
                    m_animationDirty = true;
                }
                if (ImGui::DragFloat3("Position", &key.position.x, 0.02F))
                {
                    m_animationDirty = true;
                }
                if (ImGui::DragFloat3("Rotation", &key.rotationEuler.x, 0.5F))
                {
                    m_animationDirty = true;
                }
                if (ImGui::DragFloat3("Scale", &key.scale.x, 0.02F, 0.01F, 10.0F))
                {
                    m_animationDirty = true;
                }
                ImGui::Separator();
                ImGui::PopID();
            }
            ImGui::EndChild();

            if (removeKeyframe >= 0 && static_cast<int>(m_animationEditing.keyframes.size()) > 1)
            {
                m_animationEditing.keyframes.erase(m_animationEditing.keyframes.begin() + removeKeyframe);
                m_animationDirty = true;
            }

            if (ImGui::Button("Add Keyframe"))
            {
                AnimationKeyframe next;
                if (!m_animationEditing.keyframes.empty())
                {
                    next = m_animationEditing.keyframes.back();
                    next.time += 0.5F;
                }
                m_animationEditing.keyframes.push_back(next);
                m_animationDirty = true;
            }
            ImGui::SameLine();
            if (ImGui::Button("Sort by Time"))
            {
                std::sort(
                    m_animationEditing.keyframes.begin(),
                    m_animationEditing.keyframes.end(),
                    [](const AnimationKeyframe& a, const AnimationKeyframe& b) { return a.time < b.time; }
                );
                m_animationDirty = true;
            }
            ImGui::SameLine();
            if (ImGui::Button("Save Clip"))
            {
                std::sort(
                    m_animationEditing.keyframes.begin(),
                    m_animationEditing.keyframes.end(),
                    [](const AnimationKeyframe& a, const AnimationKeyframe& b) { return a.time < b.time; }
                );

                std::string error;
                if (LevelAssetIO::SaveAnimationClip(m_animationEditing, &error))
                {
                    m_animationDirty = false;
                    m_animationPreviewClip = m_animationEditing.id;
                    m_statusLine = "Saved animation clip " + m_animationEditing.id;
                    m_contentNeedsRefresh = true;
                    m_animationCache.clear();
                    RefreshLibraries();
                }
                else
                {
                    m_statusLine = "Save animation clip failed: " + error;
                }
            }
            if (m_animationDirty)
            {
                ImGui::TextUnformatted("* Animation clip has unsaved changes");
            }
        }

        if (ImGui::CollapsingHeader("Environment", ImGuiTreeNodeFlags_DefaultOpen))
        {
            char envIdBuffer[128]{};
            std::snprintf(envIdBuffer, sizeof(envIdBuffer), "%s", m_environmentEditing.id.c_str());
            if (ImGui::InputText("Environment Id", envIdBuffer, sizeof(envIdBuffer)))
            {
                m_environmentEditing.id = envIdBuffer;
                m_environmentDirty = true;
            }
            if (ImGui::ColorEdit3("Sky Top", &m_environmentEditing.skyTopColor.x)) m_environmentDirty = true;
            if (ImGui::ColorEdit3("Sky Bottom", &m_environmentEditing.skyBottomColor.x)) m_environmentDirty = true;
            ImGui::Checkbox("Clouds Enabled", &m_environmentEditing.cloudsEnabled);
            ImGui::DragFloat("Cloud Coverage", &m_environmentEditing.cloudCoverage, 0.01F, 0.0F, 1.0F);
            ImGui::DragFloat("Cloud Density", &m_environmentEditing.cloudDensity, 0.01F, 0.0F, 1.0F);
            ImGui::DragFloat("Cloud Speed", &m_environmentEditing.cloudSpeed, 0.01F, 0.0F, 8.0F);
            ImGui::DragFloat3("Directional Dir", &m_environmentEditing.directionalLightDirection.x, 0.01F, -1.0F, 1.0F);
            if (ImGui::ColorEdit3("Directional Color", &m_environmentEditing.directionalLightColor.x)) m_environmentDirty = true;
            ImGui::DragFloat("Directional Intensity", &m_environmentEditing.directionalLightIntensity, 0.01F, 0.0F, 8.0F);
            ImGui::Checkbox("Fog Enabled", &m_environmentEditing.fogEnabled);
            if (ImGui::ColorEdit3("Fog Color", &m_environmentEditing.fogColor.x)) m_environmentDirty = true;
            ImGui::DragFloat("Fog Density", &m_environmentEditing.fogDensity, 0.0005F, 0.0F, 0.2F, "%.4f");
            ImGui::DragFloat("Fog Start", &m_environmentEditing.fogStart, 0.1F, 0.0F, 2000.0F);
            ImGui::DragFloat("Fog End", &m_environmentEditing.fogEnd, 0.1F, 0.1F, 3000.0F);
            ImGui::DragInt("Shadow Quality", &m_environmentEditing.shadowQuality, 1.0F, 0, 3);
            ImGui::DragFloat("Shadow Distance", &m_environmentEditing.shadowDistance, 0.5F, 1.0F, 1000.0F);
            ImGui::Checkbox("Tone Mapping", &m_environmentEditing.toneMapping);
            ImGui::DragFloat("Exposure", &m_environmentEditing.exposure, 0.01F, 0.1F, 8.0F);
            ImGui::Checkbox("Bloom", &m_environmentEditing.bloom);

            if (ImGui::Button("Save Environment"))
            {
                std::string error;
                if (LevelAssetIO::SaveEnvironment(m_environmentEditing, &error))
                {
                    m_map.environmentAssetId = m_environmentEditing.id;
                    m_statusLine = "Saved environment " + m_environmentEditing.id;
                    m_environmentDirty = false;
                    m_contentNeedsRefresh = true;
                }
                else
                {
                    m_statusLine = "Save environment failed: " + error;
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Load Environment"))
            {
                if (LevelAssetIO::LoadEnvironment(m_map.environmentAssetId, &m_environmentEditing, nullptr))
                {
                    m_statusLine = "Loaded environment " + m_map.environmentAssetId;
                }
            }
        }
    }
    if (materialWindowOpen)
    {
        ImGui::End();
    }

    const bool fxWindowOpen =
        showFxWindow &&
        beginDockedPanel(DockPanel::FxEditor, std::max(240.0F, rightRect.w * 0.5F), workspaceMesh ? "Mesh Modeler" : "FX Editor");
    if (fxWindowOpen)
    {
        const bool showFxCore = !workspaceMesh;
        const bool showMeshTools = !workspaceFxEnv;
        if (showFxCore)
        {
        auto normalizeCurves = [this]() {
            for (engine::fx::FxEmitterAsset& emitter : m_fxEditing.emitters)
            {
                if (emitter.sizeOverLife.keys.empty())
                {
                    emitter.sizeOverLife.keys = {
                        engine::fx::FloatCurveKey{0.0F, 1.0F},
                        engine::fx::FloatCurveKey{1.0F, 0.0F},
                    };
                }
                if (emitter.sizeOverLife.keys.size() == 1U)
                {
                    emitter.sizeOverLife.keys.push_back(engine::fx::FloatCurveKey{1.0F, emitter.sizeOverLife.keys.front().value});
                }
                if (emitter.alphaOverLife.keys.empty())
                {
                    emitter.alphaOverLife.keys = {
                        engine::fx::FloatCurveKey{0.0F, 1.0F},
                        engine::fx::FloatCurveKey{1.0F, 0.0F},
                    };
                }
                if (emitter.alphaOverLife.keys.size() == 1U)
                {
                    emitter.alphaOverLife.keys.push_back(engine::fx::FloatCurveKey{1.0F, emitter.alphaOverLife.keys.front().value});
                }
                if (emitter.colorOverLife.keys.empty())
                {
                    emitter.colorOverLife.keys = {
                        engine::fx::ColorGradientKey{0.0F, glm::vec4{1.0F}},
                        engine::fx::ColorGradientKey{1.0F, glm::vec4{1.0F, 1.0F, 1.0F, 0.0F}},
                    };
                }
                if (emitter.colorOverLife.keys.size() == 1U)
                {
                    emitter.colorOverLife.keys.push_back(engine::fx::ColorGradientKey{1.0F, emitter.colorOverLife.keys.front().color});
                }
                std::sort(
                    emitter.sizeOverLife.keys.begin(),
                    emitter.sizeOverLife.keys.end(),
                    [](const engine::fx::FloatCurveKey& a, const engine::fx::FloatCurveKey& b) { return a.t < b.t; }
                );
                std::sort(
                    emitter.alphaOverLife.keys.begin(),
                    emitter.alphaOverLife.keys.end(),
                    [](const engine::fx::FloatCurveKey& a, const engine::fx::FloatCurveKey& b) { return a.t < b.t; }
                );
                std::sort(
                    emitter.colorOverLife.keys.begin(),
                    emitter.colorOverLife.keys.end(),
                    [](const engine::fx::ColorGradientKey& a, const engine::fx::ColorGradientKey& b) { return a.t < b.t; }
                );
            }
        };

        auto reloadFxLibrary = [this]() {
            m_fxPreviewSystem.ReloadAssets();
            m_fxLibrary = m_fxPreviewSystem.ListAssetIds();
            if (m_selectedFxIndex >= static_cast<int>(m_fxLibrary.size()))
            {
                m_selectedFxIndex = m_fxLibrary.empty() ? -1 : 0;
            }
        };

        auto loadSelectedAssetToEditing = [this]() {
            if (m_selectedFxIndex < 0 || m_selectedFxIndex >= static_cast<int>(m_fxLibrary.size()))
            {
                return false;
            }
            const std::string assetId = m_fxLibrary[static_cast<std::size_t>(m_selectedFxIndex)];
            const auto loaded = m_fxPreviewSystem.GetAsset(assetId);
            if (!loaded.has_value())
            {
                return false;
            }
            m_fxEditing = *loaded;
            if (m_fxEditing.emitters.empty())
            {
                m_fxEditing.emitters.push_back(engine::fx::FxEmitterAsset{});
            }
            m_selectedFxEmitterIndex = glm::clamp(m_selectedFxEmitterIndex, 0, static_cast<int>(m_fxEditing.emitters.size()) - 1);
            m_fxDirty = false;
            return true;
        };

        auto saveEditingAsset = [this, &normalizeCurves, &reloadFxLibrary]() {
            normalizeCurves();
            if (m_fxEditing.id.empty())
            {
                m_statusLine = "FX save failed: empty id";
                return false;
            }
            std::string error;
            if (!m_fxPreviewSystem.SaveAsset(m_fxEditing, &error))
            {
                m_statusLine = "FX save failed: " + error;
                return false;
            }
            m_fxDirty = false;
            reloadFxLibrary();
            for (int i = 0; i < static_cast<int>(m_fxLibrary.size()); ++i)
            {
                if (m_fxLibrary[static_cast<std::size_t>(i)] == m_fxEditing.id)
                {
                    m_selectedFxIndex = i;
                    break;
                }
            }
            m_statusLine = "Saved FX asset " + m_fxEditing.id;
            return true;
        };

        if (ImGui::Button("Reload FX"))
        {
            reloadFxLibrary();
            m_statusLine = "FX assets reloaded";
        }
        ImGui::SameLine();
        if (ImGui::Button("New FX"))
        {
            m_fxEditing = engine::fx::FxAsset{};
            m_fxEditing.id = "new_fx";
            m_fxEditing.emitters = {engine::fx::FxEmitterAsset{}};
            m_selectedFxEmitterIndex = 0;
            m_fxDirty = true;
            m_statusLine = "Created new FX editing asset";
        }
        ImGui::SameLine();
        if (ImGui::Button("Load Selected FX"))
        {
            if (loadSelectedAssetToEditing())
            {
                m_statusLine = "Loaded FX " + m_fxEditing.id;
            }
            else
            {
                m_statusLine = "Load FX failed";
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Save FX"))
        {
            (void)saveEditingAsset();
        }
        ImGui::SameLine();
        if (ImGui::Button("Delete Selected FX"))
        {
            if (m_selectedFxIndex >= 0 && m_selectedFxIndex < static_cast<int>(m_fxLibrary.size()))
            {
                const std::string assetId = m_fxLibrary[static_cast<std::size_t>(m_selectedFxIndex)];
                const std::filesystem::path path = std::filesystem::path("assets") / "fx" / (assetId + ".json");
                std::error_code ec;
                const bool removed = std::filesystem::remove(path, ec);
                if (removed && !ec)
                {
                    reloadFxLibrary();
                    m_statusLine = "Deleted FX " + assetId;
                    if (m_fxEditing.id == assetId)
                    {
                        m_fxEditing = engine::fx::FxAsset{};
                        m_fxEditing.emitters = {engine::fx::FxEmitterAsset{}};
                        m_selectedFxEmitterIndex = 0;
                        m_fxDirty = false;
                    }
                }
                else
                {
                    m_statusLine = "Delete FX failed: " + ec.message();
                }
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Stop Preview"))
        {
            m_fxPreviewSystem.StopAll();
            m_statusLine = "FX preview stopped";
        }

        ImGui::Separator();
        ImGui::TextUnformatted("FX Library");
        if (ImGui::BeginListBox("##fx_library", ImVec2(280.0F, 140.0F)))
        {
            for (int i = 0; i < static_cast<int>(m_fxLibrary.size()); ++i)
            {
                const std::string& fxId = m_fxLibrary[static_cast<std::size_t>(i)];
                const bool selected = m_selectedFxIndex == i;
                if (ImGui::Selectable(fxId.c_str(), selected))
                {
                    m_selectedFxIndex = i;
                }
                if (ImGui::BeginDragDropSource())
                {
                    ImGui::SetDragDropPayload("FX_ASSET_ID", fxId.c_str(), fxId.size() + 1U);
                    ImGui::Text("Drop FX: %s", fxId.c_str());
                    ImGui::EndDragDropSource();
                }
            }
            ImGui::EndListBox();
        }

        ImGui::Separator();
        char fxIdBuffer[128]{};
        std::snprintf(fxIdBuffer, sizeof(fxIdBuffer), "%s", m_fxEditing.id.c_str());
        if (ImGui::InputText("FX Id", fxIdBuffer, sizeof(fxIdBuffer)))
        {
            m_fxEditing.id = fxIdBuffer;
            m_fxDirty = true;
        }

        int netModeIndex = FxNetModeToIndex(m_fxEditing.netMode);
        const char* netModeItems[] = {"Local", "ServerBroadcast", "OwnerOnly"};
        if (ImGui::Combo("Net Mode", &netModeIndex, netModeItems, IM_ARRAYSIZE(netModeItems)))
        {
            m_fxEditing.netMode = FxNetModeFromIndex(netModeIndex);
            m_fxDirty = true;
        }
        if (ImGui::Checkbox("Looping Asset", &m_fxEditing.looping)) m_fxDirty = true;
        if (ImGui::DragFloat("Asset Duration", &m_fxEditing.duration, 0.01F, 0.01F, 30.0F)) m_fxDirty = true;
        if (ImGui::DragInt("Max Instances", &m_fxEditing.maxInstances, 1.0F, 1, 4096)) m_fxDirty = true;
        if (ImGui::DragInt("LOD Priority", &m_fxEditing.lodPriority, 1.0F, -8, 8)) m_fxDirty = true;

        if (ImGui::CollapsingHeader("Camera Shake", ImGuiTreeNodeFlags_DefaultOpen))
        {
            if (ImGui::Checkbox("Enable Shake", &m_fxEditing.enableCameraShake)) m_fxDirty = true;
            if (ImGui::DragFloat("Shake Amplitude", &m_fxEditing.cameraShakeAmplitude, 0.01F, 0.0F, 5.0F)) m_fxDirty = true;
            if (ImGui::DragFloat("Shake Frequency", &m_fxEditing.cameraShakeFrequency, 0.1F, 0.1F, 80.0F)) m_fxDirty = true;
            if (ImGui::DragFloat("Shake Duration", &m_fxEditing.cameraShakeDuration, 0.01F, 0.01F, 10.0F)) m_fxDirty = true;
        }

        if (ImGui::CollapsingHeader("PostFX Pulse", ImGuiTreeNodeFlags_DefaultOpen))
        {
            if (ImGui::Checkbox("Enable Pulse", &m_fxEditing.enablePostFxPulse)) m_fxDirty = true;
            if (ImGui::ColorEdit3("Pulse Color", &m_fxEditing.postFxColor.x)) m_fxDirty = true;
            if (ImGui::DragFloat("Pulse Intensity", &m_fxEditing.postFxIntensity, 0.01F, 0.0F, 3.0F)) m_fxDirty = true;
            if (ImGui::DragFloat("Pulse Duration", &m_fxEditing.postFxDuration, 0.01F, 0.01F, 10.0F)) m_fxDirty = true;
        }

        ImGui::Separator();
        ImGui::TextUnformatted("Emitters");
        if (ImGui::BeginListBox("##fx_emitters", ImVec2(280.0F, 110.0F)))
        {
            for (int i = 0; i < static_cast<int>(m_fxEditing.emitters.size()); ++i)
            {
                const engine::fx::FxEmitterAsset& emitter = m_fxEditing.emitters[static_cast<std::size_t>(i)];
                std::ostringstream label;
                label << i << ": " << (emitter.name.empty() ? "emitter" : emitter.name);
                const bool selected = m_selectedFxEmitterIndex == i;
                if (ImGui::Selectable(label.str().c_str(), selected))
                {
                    m_selectedFxEmitterIndex = i;
                }
            }
            ImGui::EndListBox();
        }
        if (ImGui::Button("Add Emitter"))
        {
            engine::fx::FxEmitterAsset emitter{};
            emitter.name = "emitter_" + std::to_string(m_fxEditing.emitters.size() + 1U);
            m_fxEditing.emitters.push_back(emitter);
            m_selectedFxEmitterIndex = static_cast<int>(m_fxEditing.emitters.size()) - 1;
            m_fxDirty = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("Remove Emitter") && !m_fxEditing.emitters.empty() && m_selectedFxEmitterIndex >= 0)
        {
            m_fxEditing.emitters.erase(m_fxEditing.emitters.begin() + m_selectedFxEmitterIndex);
            if (m_fxEditing.emitters.empty())
            {
                m_fxEditing.emitters.push_back(engine::fx::FxEmitterAsset{});
            }
            m_selectedFxEmitterIndex = glm::clamp(m_selectedFxEmitterIndex, 0, static_cast<int>(m_fxEditing.emitters.size()) - 1);
            m_fxDirty = true;
        }

        if (!m_fxEditing.emitters.empty())
        {
            m_selectedFxEmitterIndex = glm::clamp(m_selectedFxEmitterIndex, 0, static_cast<int>(m_fxEditing.emitters.size()) - 1);
            engine::fx::FxEmitterAsset& emitter =
                m_fxEditing.emitters[static_cast<std::size_t>(m_selectedFxEmitterIndex)];

            ImGui::Separator();
            ImGui::Text("Emitter #%d (%s)", m_selectedFxEmitterIndex, emitter.name.c_str());
            char emitterNameBuffer[128]{};
            std::snprintf(emitterNameBuffer, sizeof(emitterNameBuffer), "%s", emitter.name.c_str());
            if (ImGui::InputText("Emitter Name", emitterNameBuffer, sizeof(emitterNameBuffer)))
            {
                emitter.name = emitterNameBuffer;
                m_fxDirty = true;
            }

            int emitterTypeIndex = FxEmitterTypeToIndex(emitter.type);
            const char* emitterTypeItems[] = {"Sprite", "Trail"};
            if (ImGui::Combo("Emitter Type", &emitterTypeIndex, emitterTypeItems, IM_ARRAYSIZE(emitterTypeItems)))
            {
                emitter.type = FxEmitterTypeFromIndex(emitterTypeIndex);
                m_fxDirty = true;
            }

            int blendModeIndex = FxBlendModeToIndex(emitter.blendMode);
            const char* blendItems[] = {"Additive", "Alpha"};
            if (ImGui::Combo("Blend Mode", &blendModeIndex, blendItems, IM_ARRAYSIZE(blendItems)))
            {
                emitter.blendMode = FxBlendModeFromIndex(blendModeIndex);
                m_fxDirty = true;
            }

            if (ImGui::Checkbox("Depth Test", &emitter.depthTest)) m_fxDirty = true;
            ImGui::SameLine();
            if (ImGui::Checkbox("Looping Emitter", &emitter.looping)) m_fxDirty = true;
            ImGui::SameLine();
            if (ImGui::Checkbox("Local Space", &emitter.localSpace)) m_fxDirty = true;

            if (ImGui::DragFloat("Emitter Duration", &emitter.duration, 0.01F, 0.01F, 20.0F)) m_fxDirty = true;
            if (ImGui::DragFloat("Spawn Rate", &emitter.spawnRate, 0.1F, 0.0F, 1000.0F)) m_fxDirty = true;
            if (ImGui::DragInt("Burst Count", &emitter.burstCount, 1.0F, 0, 20000)) m_fxDirty = true;
            if (ImGui::DragInt("Max Particles", &emitter.maxParticles, 1.0F, 1, 20000)) m_fxDirty = true;
            if (ImGui::DragFloat("Max Distance", &emitter.maxDistance, 0.5F, 0.1F, 2000.0F)) m_fxDirty = true;
            if (ImGui::DragFloat("LOD Near", &emitter.lodNearDistance, 0.1F, 0.0F, 500.0F)) m_fxDirty = true;
            if (ImGui::DragFloat("LOD Far", &emitter.lodFarDistance, 0.1F, 0.0F, 2000.0F)) m_fxDirty = true;
            if (ImGui::DragFloat2("Lifetime Range", &emitter.lifetimeRange.x, 0.01F, 0.01F, 40.0F)) m_fxDirty = true;
            if (ImGui::DragFloat2("Speed Range", &emitter.speedRange.x, 0.05F, -200.0F, 200.0F)) m_fxDirty = true;
            if (ImGui::DragFloat2("Size Range", &emitter.sizeRange.x, 0.005F, 0.001F, 20.0F)) m_fxDirty = true;
            if (ImGui::DragFloat3("Velocity Base", &emitter.velocityBase.x, 0.05F, -100.0F, 100.0F)) m_fxDirty = true;
            if (ImGui::DragFloat3("Velocity Random", &emitter.velocityRandom.x, 0.05F, 0.0F, 100.0F)) m_fxDirty = true;
            if (ImGui::DragFloat("Gravity", &emitter.gravity, 0.05F, -100.0F, 100.0F)) m_fxDirty = true;
            if (ImGui::DragFloat("Trail Width", &emitter.trailWidth, 0.005F, 0.001F, 20.0F)) m_fxDirty = true;
            if (ImGui::DragFloat("Trail Point Step", &emitter.trailPointStep, 0.001F, 0.001F, 2.0F)) m_fxDirty = true;
            if (ImGui::DragFloat("Trail Point Lifetime", &emitter.trailPointLifetime, 0.01F, 0.01F, 20.0F)) m_fxDirty = true;

            char rateParamBuffer[128]{};
            std::snprintf(rateParamBuffer, sizeof(rateParamBuffer), "%s", emitter.rateParam.c_str());
            if (ImGui::InputText("Rate Param", rateParamBuffer, sizeof(rateParamBuffer)))
            {
                emitter.rateParam = rateParamBuffer;
                m_fxDirty = true;
            }
            char colorParamBuffer[128]{};
            std::snprintf(colorParamBuffer, sizeof(colorParamBuffer), "%s", emitter.colorParam.c_str());
            if (ImGui::InputText("Color Param", colorParamBuffer, sizeof(colorParamBuffer)))
            {
                emitter.colorParam = colorParamBuffer;
                m_fxDirty = true;
            }
            char sizeParamBuffer[128]{};
            std::snprintf(sizeParamBuffer, sizeof(sizeParamBuffer), "%s", emitter.sizeParam.c_str());
            if (ImGui::InputText("Size Param", sizeParamBuffer, sizeof(sizeParamBuffer)))
            {
                emitter.sizeParam = sizeParamBuffer;
                m_fxDirty = true;
            }

            normalizeCurves();
            engine::fx::FloatCurveKey& sizeStart = emitter.sizeOverLife.keys.front();
            engine::fx::FloatCurveKey& sizeEnd = emitter.sizeOverLife.keys.back();
            if (ImGui::DragFloat("Size Start T", &sizeStart.t, 0.01F, 0.0F, 1.0F)) m_fxDirty = true;
            if (ImGui::DragFloat("Size Start V", &sizeStart.value, 0.01F, 0.0F, 10.0F)) m_fxDirty = true;
            if (ImGui::DragFloat("Size End T", &sizeEnd.t, 0.01F, 0.0F, 1.0F)) m_fxDirty = true;
            if (ImGui::DragFloat("Size End V", &sizeEnd.value, 0.01F, 0.0F, 10.0F)) m_fxDirty = true;

            engine::fx::FloatCurveKey& alphaStart = emitter.alphaOverLife.keys.front();
            engine::fx::FloatCurveKey& alphaEnd = emitter.alphaOverLife.keys.back();
            if (ImGui::DragFloat("Alpha Start T", &alphaStart.t, 0.01F, 0.0F, 1.0F)) m_fxDirty = true;
            if (ImGui::DragFloat("Alpha Start V", &alphaStart.value, 0.01F, 0.0F, 1.0F)) m_fxDirty = true;
            if (ImGui::DragFloat("Alpha End T", &alphaEnd.t, 0.01F, 0.0F, 1.0F)) m_fxDirty = true;
            if (ImGui::DragFloat("Alpha End V", &alphaEnd.value, 0.01F, 0.0F, 1.0F)) m_fxDirty = true;

            engine::fx::ColorGradientKey& colorStart = emitter.colorOverLife.keys.front();
            engine::fx::ColorGradientKey& colorEnd = emitter.colorOverLife.keys.back();
            if (ImGui::DragFloat("Color Start T", &colorStart.t, 0.01F, 0.0F, 1.0F)) m_fxDirty = true;
            if (ImGui::ColorEdit4("Color Start", &colorStart.color.x)) m_fxDirty = true;
            if (ImGui::DragFloat("Color End T", &colorEnd.t, 0.01F, 0.0F, 1.0F)) m_fxDirty = true;
            if (ImGui::ColorEdit4("Color End", &colorEnd.color.x)) m_fxDirty = true;
        }

        ImGui::Separator();
        if (ImGui::Button("Spawn Editing FX At Camera"))
        {
            if (saveEditingAsset())
            {
                const glm::vec3 spawnPos = m_cameraPosition + CameraForward() * 4.0F + glm::vec3{0.0F, 0.2F, 0.0F};
                m_fxPreviewSystem.Spawn(m_fxEditing.id, spawnPos, CameraForward(), {});
            }
        }
        if (m_hoveredTileValid)
        {
            ImGui::SameLine();
            if (ImGui::Button("Spawn Editing FX At Hovered"))
            {
                if (saveEditingAsset())
                {
                    const glm::vec3 spawnPos = TileCenter(m_hoveredTile.x, m_hoveredTile.y) + glm::vec3{0.0F, 0.2F, 0.0F};
                    m_fxPreviewSystem.Spawn(m_fxEditing.id, spawnPos, CameraForward(), {});
                }
            }
        }
        if (m_fxDirty)
        {
            ImGui::TextUnformatted("* FX asset has unsaved changes");
        }
        ImGui::Text("Net Mode: %s", FxNetModeToText(m_fxEditing.netMode));
        if (!m_fxEditing.emitters.empty() && m_selectedFxEmitterIndex >= 0 && m_selectedFxEmitterIndex < static_cast<int>(m_fxEditing.emitters.size()))
        {
            const engine::fx::FxEmitterAsset& selectedEmitter =
                m_fxEditing.emitters[static_cast<std::size_t>(m_selectedFxEmitterIndex)];
            ImGui::Text("Emitter Type: %s", FxEmitterTypeToText(selectedEmitter.type));
            ImGui::Text("Emitter Blend: %s", FxBlendModeToText(selectedEmitter.blendMode));
        }

        const engine::fx::FxStats fxStats = m_fxPreviewSystem.Stats();
        ImGui::Separator();
        ImGui::Text("Active Instances: %d", fxStats.activeInstances);
        ImGui::Text("Active Particles: %d", fxStats.activeParticles);
        ImGui::Text("Trail Points: %d", fxStats.activeTrailPoints);
        ImGui::Text("FX CPU: %.3f ms", fxStats.cpuMs);
        }

        if (showMeshTools && ImGui::CollapsingHeader("Mesh Modeler", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::TextUnformatted("Advanced mesh editing: face/edge/vertex + scene picking + gizmo drag.");
            ImGui::Checkbox("Scene Edit (click in Scene Viewport)", &m_meshModelSceneEditEnabled);
            ImGui::SameLine();
            ImGui::Checkbox("Show Mesh Gizmo", &m_meshModelShowGizmo);
            ImGui::TextUnformatted("Hotkeys: Tab=Object/Edit, 1=Vertex 2=Edge 3=Face, Alt+LMB=LoopSelect, Shift+Alt+LMB=RingSelect");
            ImGui::TextUnformatted("Extra: 4/5/6 legacy mode switch, J batch extrude, Ctrl+B batch bevel, Enter apply preview, K knife, O scene loop cut");
            int editModeIndex = static_cast<int>(m_meshEditMode);
            const char* editModes[] = {"Face", "Edge", "Vertex"};
            if (ImGui::Combo("Edit Mode", &editModeIndex, editModes, IM_ARRAYSIZE(editModes)))
            {
                editModeIndex = std::clamp(editModeIndex, 0, 2);
                m_meshEditMode = static_cast<MeshEditMode>(editModeIndex);
            }
            ImGui::Checkbox("Knife Tool (2 clicks, multi-face path)", &m_meshModelKnifeEnabled);
            ImGui::SameLine();
            ImGui::Checkbox("Scene Loop Cut Tool", &m_meshModelLoopCutToolEnabled);
            if (!m_meshModelKnifeEnabled)
            {
                m_meshModelKnifeHasFirstPoint = false;
                m_meshModelKnifeFaceIndex = -1;
                m_meshModelKnifeFirstPointLocal = glm::vec3{0.0F};
                m_meshModelKnifeFirstPointWorld = glm::vec3{0.0F};
                m_meshModelKnifePreviewValid = false;
                m_meshModelKnifePreviewWorld = glm::vec3{0.0F};
                m_meshModelKnifePreviewSegments.clear();
            }
            ImGui::SameLine();
            if (ImGui::Button("Reset Knife Points"))
            {
                m_meshModelKnifeHasFirstPoint = false;
                m_meshModelKnifeFaceIndex = -1;
                m_meshModelKnifeFirstPointLocal = glm::vec3{0.0F};
                m_meshModelKnifeFirstPointWorld = glm::vec3{0.0F};
                m_meshModelKnifePreviewValid = false;
                m_meshModelKnifePreviewWorld = glm::vec3{0.0F};
                m_meshModelKnifePreviewSegments.clear();
            }
            if (m_meshModelKnifeHasFirstPoint)
            {
                ImGui::Text("Knife: first point set on face %d", m_meshModelKnifeFaceIndex);
            }
            ImGui::DragFloat3("Model Position", &m_meshModelPosition.x, 0.05F);
            ImGui::DragFloat3("Model Scale", &m_meshModelScale.x, 0.02F, 0.05F, 30.0F);
            char meshNameBuffer[128]{};
            std::snprintf(meshNameBuffer, sizeof(meshNameBuffer), "%s", m_meshModelAssetName.c_str());
            if (ImGui::InputText("Asset Name", meshNameBuffer, sizeof(meshNameBuffer)))
            {
                m_meshModelAssetName = meshNameBuffer;
            }
            ImGui::Separator();
            ImGui::TextUnformatted("Primitives");
            ImGui::DragFloat("Prim Radius", &m_meshPrimitiveRadius, 0.01F, 0.05F, 8.0F);
            ImGui::DragFloat("Prim Height", &m_meshPrimitiveHeight, 0.02F, 0.2F, 18.0F);
            ImGui::SliderInt("Circle Segments", &m_meshPrimitiveCircleSegments, 6, 128);
            ImGui::SliderInt("Sphere Lat Segments", &m_meshPrimitiveSphereLatSegments, 6, 96);
            ImGui::SliderInt("Sphere Lon Segments", &m_meshPrimitiveSphereLonSegments, 8, 192);
            ImGui::SliderInt("Capsule Segments", &m_meshPrimitiveCapsuleSegments, 8, 128);
            ImGui::SliderInt("Capsule Hemi Rings", &m_meshPrimitiveCapsuleHemiRings, 3, 24);
            ImGui::SliderInt("Capsule Cyl Rings", &m_meshPrimitiveCapsuleCylinderRings, 0, 24);
            if (ImGui::Button("New Cube"))
            {
                PushHistorySnapshot();
                ResetMeshModelerToCube();
                m_statusLine = "Primitive created: cube";
            }
            ImGui::SameLine();
            if (ImGui::Button("New Square"))
            {
                PushHistorySnapshot();
                ResetMeshModelerToSquare();
                m_statusLine = "Primitive created: square";
            }
            ImGui::SameLine();
            if (ImGui::Button("New Circle"))
            {
                PushHistorySnapshot();
                ResetMeshModelerToCircle(m_meshPrimitiveCircleSegments, m_meshPrimitiveRadius);
                m_statusLine = "Primitive created: circle";
            }
            if (ImGui::Button("New Sphere"))
            {
                PushHistorySnapshot();
                ResetMeshModelerToSphere(
                    m_meshPrimitiveSphereLatSegments,
                    m_meshPrimitiveSphereLonSegments,
                    m_meshPrimitiveRadius
                );
                m_statusLine = "Primitive created: sphere";
            }
            ImGui::SameLine();
            if (ImGui::Button("New Fasolka (Capsule)"))
            {
                PushHistorySnapshot();
                ResetMeshModelerToCapsule(
                    m_meshPrimitiveCapsuleSegments,
                    m_meshPrimitiveCapsuleHemiRings,
                    m_meshPrimitiveCapsuleCylinderRings,
                    m_meshPrimitiveRadius,
                    m_meshPrimitiveHeight
                );
                m_statusLine = "Primitive created: fasolka/capsule";
            }
            ImGui::SameLine();
            if (ImGui::Button("Subdivide Face") && m_meshModelSelectedFace >= 0)
            {
                MeshModelerSubdivideFace(m_meshModelSelectedFace);
            }
            ImGui::SameLine();
            if (ImGui::Button("Delete Face") && m_meshModelSelectedFace >= 0)
            {
                MeshModelerDeleteFace(m_meshModelSelectedFace);
            }
            ImGui::SameLine();
            if (ImGui::Button("Dissolve Face") && m_meshModelSelectedFace >= 0)
            {
                MeshModelerDissolveFace(m_meshModelSelectedFace);
            }
            if (ImGui::Button("Cut Face X") && m_meshModelSelectedFace >= 0)
            {
                MeshModelerCutFace(m_meshModelSelectedFace, true);
            }
            ImGui::SameLine();
            if (ImGui::Button("Cut Face Z") && m_meshModelSelectedFace >= 0)
            {
                MeshModelerCutFace(m_meshModelSelectedFace, false);
            }
            ImGui::DragFloat("Extrude Distance", &m_meshModelExtrudeDistance, 0.02F, 0.01F, 8.0F);
            if (ImGui::Button("Extrude Face") && m_meshModelSelectedFace >= 0)
            {
                MeshModelerExtrudeFace(m_meshModelSelectedFace, m_meshModelExtrudeDistance);
            }

            const std::vector<std::array<int, 2>> meshEdges = BuildMeshModelEdges();
            if (m_meshModelSelectedEdge >= static_cast<int>(meshEdges.size()))
            {
                m_meshModelSelectedEdge = meshEdges.empty() ? -1 : 0;
            }
            ImGui::DragFloat("Bevel Width", &m_meshModelBevelDistance, 0.01F, 0.01F, 3.0F);
            ImGui::SliderInt("Bevel Segments", &m_meshModelBevelSegments, 1, 8);
            ImGui::SliderFloat("Bevel Profile", &m_meshModelBevelProfile, 0.2F, 3.5F, "%.2f");
            ImGui::Checkbox("Bevel Corner Miter", &m_meshModelBevelUseMiter);
            ImGui::SliderFloat("Loop Cut Ratio", &m_meshModelLoopCutRatio, 0.05F, 0.95F, "%.2f");
            ImGui::Checkbox("Batch Edge Gizmo", &m_meshModelBatchGizmoEnabled);
            int batchOperationIndex = m_meshModelBatchOperation == MeshBatchEdgeOperation::Extrude ? 0 : 1;
            const char* batchOperationItems[] = {"Extrude", "Bevel"};
            if (ImGui::Combo("Batch Operation", &batchOperationIndex, batchOperationItems, IM_ARRAYSIZE(batchOperationItems)))
            {
                m_meshModelBatchOperation =
                    batchOperationIndex == 0 ? MeshBatchEdgeOperation::Extrude : MeshBatchEdgeOperation::Bevel;
            }
            ImGui::DragFloat("Batch Preview Width", &m_meshModelBatchPreviewDistance, 0.01F, 0.0F, 6.0F);
            m_meshModelBatchPreviewDistance = glm::clamp(m_meshModelBatchPreviewDistance, 0.0F, 6.0F);
            m_meshModelExtrudeDistance = glm::max(0.01F, m_meshModelBatchPreviewDistance);
            m_meshModelBevelDistance = glm::max(0.01F, m_meshModelBatchPreviewDistance);
            if (ImGui::Button("Extrude Active Edge(s)"))
            {
                MeshModelerExtrudeActiveEdges(m_meshModelExtrudeDistance);
            }
            ImGui::SameLine();
            if (ImGui::Button("Bevel Active Edge(s)"))
            {
                MeshModelerBevelActiveEdges(m_meshModelBevelDistance, m_meshModelBevelSegments);
            }
            ImGui::SameLine();
            if (ImGui::Button("Loop Cut Edge") && m_meshModelSelectedEdge >= 0)
            {
                MeshModelerLoopCutEdge(m_meshModelSelectedEdge, m_meshModelLoopCutRatio);
            }
            const std::vector<int> activeEdges = CollectMeshModelActiveEdges();
            const char* activeSource =
                !m_meshModelLoopSelectionEdges.empty() ? "Loop" :
                (!m_meshModelRingSelectionEdges.empty() ? "Ring" :
                                                        (m_meshModelSelectedEdge >= 0 ? "Single" : "None"));
            ImGui::Text("Editable edge set: %s (%d edge%s)",
                        activeSource,
                        static_cast<int>(activeEdges.size()),
                        activeEdges.size() == 1U ? "" : "s");
            ImGui::SameLine();
            if (ImGui::Button("Apply Preview Operation"))
            {
                if (m_meshModelBatchOperation == MeshBatchEdgeOperation::Extrude)
                {
                    MeshModelerExtrudeActiveEdges(m_meshModelBatchPreviewDistance);
                }
                else
                {
                    MeshModelerBevelActiveEdges(m_meshModelBatchPreviewDistance, m_meshModelBevelSegments);
                }
            }

            ImGui::Separator();
            ImGui::Text("Faces");
            if (ImGui::BeginListBox("##mesh_faces", ImVec2(-1.0F, 100.0F)))
            {
                for (int i = 0; i < static_cast<int>(m_meshModelFaces.size()); ++i)
                {
                    const MeshModelFace& face = m_meshModelFaces[static_cast<std::size_t>(i)];
                    std::ostringstream label;
                    label << "Face " << i;
                    if (face.deleted)
                    {
                        label << " [deleted]";
                    }
                    if (ImGui::Selectable(label.str().c_str(), m_meshModelSelectedFace == i))
                    {
                        m_meshModelSelectedFace = i;
                        m_meshModelFaceSelection.clear();
                        m_meshModelFaceSelection.push_back(i);
                    }
                }
                ImGui::EndListBox();
            }

            ImGui::Text("Edges");
            if (ImGui::BeginListBox("##mesh_edges", ImVec2(-1.0F, 90.0F)))
            {
                for (int i = 0; i < static_cast<int>(meshEdges.size()); ++i)
                {
                    const auto& edge = meshEdges[static_cast<std::size_t>(i)];
                    std::ostringstream label;
                    label << "E" << i << " (V" << edge[0] << " - V" << edge[1] << ")";
                if (ImGui::Selectable(label.str().c_str(), m_meshModelSelectedEdge == i))
                {
                    m_meshModelSelectedEdge = i;
                }
            }
            ImGui::EndListBox();
            }
            if (ImGui::Button("Select Edge Loop") && m_meshModelSelectedEdge >= 0)
            {
                MeshModelerSelectEdgeLoop(m_meshModelSelectedEdge);
            }
            ImGui::SameLine();
            if (ImGui::Button("Select Edge Ring") && m_meshModelSelectedEdge >= 0)
            {
                MeshModelerSelectEdgeRing(m_meshModelSelectedEdge);
            }
            ImGui::SameLine();
            if (ImGui::Button("Clear Loop/Ring Selection"))
            {
                m_meshModelLoopSelectionEdges.clear();
                m_meshModelRingSelectionEdges.clear();
            }
            ImGui::Text("Loop edges: %d | Ring edges: %d",
                        static_cast<int>(m_meshModelLoopSelectionEdges.size()),
                        static_cast<int>(m_meshModelRingSelectionEdges.size()));

            ImGui::Text("Vertices");
            if (ImGui::BeginListBox("##mesh_vertices", ImVec2(-1.0F, 100.0F)))
            {
                for (int i = 0; i < static_cast<int>(m_meshModelVertices.size()); ++i)
                {
                    const MeshModelVertex& vertex = m_meshModelVertices[static_cast<std::size_t>(i)];
                    std::ostringstream label;
                    label << "V" << i << " (" << vertex.position.x << ", " << vertex.position.y << ", " << vertex.position.z << ")";
                    if (vertex.deleted)
                    {
                        label << " [deleted]";
                    }
                    if (ImGui::Selectable(label.str().c_str(), m_meshModelSelectedVertex == i))
                    {
                        m_meshModelSelectedVertex = i;
                        m_meshModelVertexSelection.clear();
                        m_meshModelVertexSelection.push_back(i);
                    }
                }
                ImGui::EndListBox();
            }
            if (m_meshModelSelectedEdge >= 0)
            {
                if (ImGui::Button("Set Bridge Edge A"))
                {
                    m_meshModelBridgeEdgeA = m_meshModelSelectedEdge;
                }
                ImGui::SameLine();
                if (ImGui::Button("Set Bridge Edge B"))
                {
                    m_meshModelBridgeEdgeB = m_meshModelSelectedEdge;
                }
                ImGui::SameLine();
                if (ImGui::Button("Bridge A-B") &&
                    m_meshModelBridgeEdgeA >= 0 &&
                    m_meshModelBridgeEdgeB >= 0 &&
                    m_meshModelBridgeEdgeA != m_meshModelBridgeEdgeB)
                {
                    MeshModelerBridgeEdges(m_meshModelBridgeEdgeA, m_meshModelBridgeEdgeB);
                }
                ImGui::Text("Bridge edges: A=%d B=%d", m_meshModelBridgeEdgeA, m_meshModelBridgeEdgeB);
            }
            if (ImGui::Button("Dissolve Selected Edge") && m_meshModelSelectedEdge >= 0)
            {
                MeshModelerDissolveSelectedEdge();
            }
            ImGui::SameLine();
            if (ImGui::Button("Split Selected Vertex"))
            {
                MeshModelerSplitSelectedVertex();
            }

            ImGui::InputInt("Merge Keep V", &m_meshModelMergeKeepVertex);
            ImGui::InputInt("Merge Remove V", &m_meshModelMergeRemoveVertex);
            if (ImGui::Button("Merge Vertices"))
            {
                MeshModelerMergeVertices(m_meshModelMergeKeepVertex, m_meshModelMergeRemoveVertex);
            }

            ImGui::DragFloat3("Vertex Delta", &m_meshModelVertexDelta.x, 0.02F, -8.0F, 8.0F);
            if (ImGui::Button("Move Selected Vertex") && m_meshModelSelectedVertex >= 0)
            {
                MeshModelerMoveVertex(m_meshModelSelectedVertex, m_meshModelVertexDelta);
            }
            if (ImGui::Button("Move Current Selection (Delta)"))
            {
                MoveMeshSelection(m_meshModelVertexDelta);
            }

            std::string exportedRelativePath;
            std::string exportError;
            if (ImGui::Button("Export OBJ"))
            {
                if (ExportMeshModelerObj(m_meshModelAssetName, &exportedRelativePath, &exportError))
                {
                    m_statusLine = "Mesh exported: " + exportedRelativePath;
                    m_contentNeedsRefresh = true;
                }
                else
                {
                    m_statusLine = "Mesh export failed: " + exportError;
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Export + Place At Hovered"))
            {
                if (ExportMeshModelerObj(m_meshModelAssetName, &exportedRelativePath, &exportError))
                {
                    m_contentNeedsRefresh = true;
                    PlaceImportedAssetAtHovered(exportedRelativePath);
                    m_statusLine = "Mesh exported and placed: " + exportedRelativePath;
                }
                else
                {
                    m_statusLine = "Mesh export/place failed: " + exportError;
                }
            }
        }
    }

    if (m_showStatusBar)
    {
        ImGui::SetNextWindowPos(ImVec2{bottomRightRect.x, bottomRightRect.y}, ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2{bottomRightRect.z, bottomRightRect.w}, ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(0.92F);
        if (ImGui::Begin(
                "Editor Status",
                nullptr,
                ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
                    ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse))
        {
            const bool editModeActive = (m_mode == Mode::LoopEditor) && m_meshModelSceneEditEnabled;
            const char* selectionType =
                m_meshEditMode == MeshEditMode::Vertex ? "VERTEX" :
                (m_meshEditMode == MeshEditMode::Edge ? "EDGE" : "FACE");
            const std::string editSuffix = editModeActive ? (" (" + std::string(selectionType) + ")") : "";
            ImGui::Text(
                "Mode: %s%s | Tool: %s | Snap: %s | Hint: G Move | R Rotate | S Scale | Tab Mode | Shift+Tab Snap | . Frame",
                editModeActive ? "EDIT" : "OBJECT",
                editSuffix.c_str(),
                GizmoToText(m_gizmoMode),
                m_gridSnap ? "ON" : "OFF");
            if (m_modalTransform.active)
            {
                const char* constraintText =
                    m_modalTransform.constraintMode == ModalConstraintMode::Axis
                        ? (m_modalTransform.constraintAxis == GizmoAxis::X ? "Axis X"
                           : (m_modalTransform.constraintAxis == GizmoAxis::Y ? "Axis Y" : "Axis Z"))
                    : (m_modalTransform.constraintMode == ModalConstraintMode::Plane
                           ? (m_modalTransform.constraintAxis == GizmoAxis::X ? "Plane YZ"
                              : (m_modalTransform.constraintAxis == GizmoAxis::Y ? "Plane XZ" : "Plane XY"))
                           : "Free");
                ImGui::SameLine();
                const char* orientText = m_modalTransform.orientation == TransformOrientation::Local ? "Local" : "Global";
                const char* numericModeText =
                    m_modalTransform.numericMode == NumericInputMode::Absolute
                        ? "Abs"
                        : (m_modalTransform.numericMode == NumericInputMode::Multiply ? "*" : "/");
                ImGui::Text("| Modal: %s | %s | %s | Num[%s]:%s%s",
                            GizmoToText(m_modalTransform.tool),
                            constraintText,
                            orientText,
                            numericModeText,
                            m_modalTransform.numericNegative ? "-" : "",
                            m_modalTransform.numericInput.empty() ? "_" : m_modalTransform.numericInput.c_str());
            }
            else if (m_boxSelection.active)
            {
                ImGui::SameLine();
                ImGui::Text("| Box Select");
            }
            else if (m_lassoSelection.active)
            {
                ImGui::SameLine();
                ImGui::Text("| Lasso Select");
            }
        }
        ImGui::End();
    }

    if (m_showShortcutGuide)
    {
        ImGui::SetNextWindowSize(ImVec2{720.0F, 520.0F}, ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Shortcut Guide", &m_showShortcutGuide))
        {
            ImGui::TextUnformatted("Navigation");
            ImGui::BulletText("MMB orbit");
            ImGui::BulletText("Shift+MMB pan");
            ImGui::BulletText("Wheel zoom");
            ImGui::BulletText("RMB + WASD camera look/move");
            ImGui::BulletText(". frame selection");
            ImGui::Separator();
            ImGui::TextUnformatted("Selection");
            ImGui::BulletText("LMB select");
            ImGui::BulletText("Shift+LMB add/remove");
            ImGui::BulletText("B box select");
            ImGui::BulletText("Ctrl+RMB lasso");
            ImGui::Separator();
            ImGui::TextUnformatted("Transform");
            ImGui::BulletText("G move | R rotate | S scale");
            ImGui::BulletText("X/Y/Z axis constraint");
            ImGui::BulletText("Shift+X/Y/Z plane constraint");
            ImGui::BulletText("Enter/LMB confirm | Esc/RMB cancel");
            ImGui::Separator();
            ImGui::TextUnformatted("Modes");
            ImGui::BulletText("Tab object/edit");
            ImGui::BulletText("1 vertex | 2 edge | 3 face");
            ImGui::BulletText("Shift+Tab snap toggle");
            ImGui::Separator();
            ImGui::TextUnformatted("Editor");
            ImGui::BulletText("R rotate loop placement");
            ImGui::BulletText("P prop placement mode");
            ImGui::BulletText("Delete remove selected | Ctrl+D duplicate");
            ImGui::BulletText("Ctrl+Z undo | Ctrl+Y redo");
        }
        ImGui::End();
    }

    if (m_showEditorManual)
    {
        ImGui::SetNextWindowSize(ImVec2{760.0F, 560.0F}, ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Editor Manual", &m_showEditorManual))
        {
            ImGui::TextWrapped("Top Menu: File/Edit/View/Add/Select/Help. Tool Header below controls workspace, mode, selection type and snapping.");
            ImGui::Separator();
            ImGui::TextUnformatted("Basic Workflow");
            ImGui::BulletText("Choose workspace: Layout/Modeling/Map/Lighting/FX.");
            ImGui::BulletText("In Loop Editor use Tab for Object/Edit and 1/2/3 for V/E/F.");
            ImGui::BulletText("Use G/R/S and constraints X/Y/Z for transforms.");
            ImGui::BulletText("Place loops/props/lights in Map Editor and test with Playtest.");
            ImGui::Separator();
            ImGui::TextUnformatted("Panel Management");
            ImGui::BulletText("Use View menu to show/hide Toolbar, Status Bar and docked panels.");
            ImGui::BulletText("Use Reset Layout in Edit menu if workspace gets messy.");
            ImGui::Separator();
            ImGui::TextWrapped("Status bar at bottom shows mode/tool/snap and active modal transform state.");
        }
        ImGui::End();
    }

    if (fxWindowOpen)
    {
        ImGui::End();
    }
    if (m_layoutNeedsSave)
    {
        SaveEditorLayout();
        m_layoutNeedsSave = false;
    }
#else
    (void)outBackToMenu;
    (void)outPlaytestMap;
    (void)outPlaytestMapName;
#endif
}

} // namespace game::editor
