#include "engine/ui/UiSystem.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>

#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <nlohmann/json.hpp>

#include "engine/platform/Input.hpp"

#define STB_TRUETYPE_IMPLEMENTATION
#include "external/stb/stb_truetype.h"

namespace engine::ui
{
namespace
{
using json = nlohmann::json;

constexpr const char* kUiVertexShader = R"(
#version 450 core
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aUv;
layout(location = 2) in vec4 aColor;
layout(location = 3) in float aTextured;
uniform vec2 uScreenSize;
out vec2 vUv;
out vec4 vColor;
flat out float vTextured;
void main() {
    vec2 ndc = vec2((aPos.x / uScreenSize.x) * 2.0 - 1.0, 1.0 - (aPos.y / uScreenSize.y) * 2.0);
    gl_Position = vec4(ndc, 0.0, 1.0);
    vUv = aUv;
    vColor = aColor;
    vTextured = aTextured;
}
)";

constexpr const char* kUiFragmentShader = R"(
#version 450 core
in vec2 vUv;
in vec4 vColor;
flat in float vTextured;
uniform sampler2D uFontTexture;
out vec4 FragColor;
void main() {
    if (vTextured > 0.5) {
        float alpha = texture(uFontTexture, vUv).r;
        FragColor = vec4(vColor.rgb, vColor.a * alpha);
    } else if (vTextured < -0.5) {
        vec2 center = vec2(0.5, 0.5);
        float dist = length(vUv - center);
        float vignette = smoothstep(0.35, 1.0, dist);
        vignette = pow(clamp(vignette, 0.0, 1.0), 1.2);
        FragColor = vec4(vColor.rgb, vColor.a * vignette);
    } else {
        FragColor = vColor;
    }
}
)";

unsigned int Compile(unsigned int type, const char* source)
{
    const unsigned int shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);
    int ok = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (ok == GL_TRUE)
    {
        return shader;
    }
    char log[1024]{};
    glGetShaderInfoLog(shader, static_cast<int>(sizeof(log)), nullptr, log);
    std::fprintf(stderr, "UI shader compile failed: %s\n", log);
    glDeleteShader(shader);
    return 0;
}

unsigned int CreateProgram(const char* vsSource, const char* fsSource)
{
    const unsigned int vs = Compile(GL_VERTEX_SHADER, vsSource);
    const unsigned int fs = Compile(GL_FRAGMENT_SHADER, fsSource);
    if (vs == 0 || fs == 0)
    {
        if (vs != 0)
        {
            glDeleteShader(vs);
        }
        if (fs != 0)
        {
            glDeleteShader(fs);
        }
        return 0;
    }
    const unsigned int program = glCreateProgram();
    glAttachShader(program, vs);
    glAttachShader(program, fs);
    glLinkProgram(program);
    glDeleteShader(vs);
    glDeleteShader(fs);
    int ok = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &ok);
    if (ok == GL_TRUE)
    {
        return program;
    }
    char log[1024]{};
    glGetProgramInfoLog(program, static_cast<int>(sizeof(log)), nullptr, log);
    std::fprintf(stderr, "UI program link failed: %s\n", log);
    glDeleteProgram(program);
    return 0;
}

glm::vec4 JsonColor(const json& root, const char* key, const glm::vec4& fallback)
{
    if (!root.contains(key) || !root[key].is_array() || root[key].size() != 4)
    {
        return fallback;
    }
    return glm::vec4{
        root[key][0].get<float>(),
        root[key][1].get<float>(),
        root[key][2].get<float>(),
        root[key][3].get<float>(),
    };
}
} // namespace

bool UiSystem::Initialize()
{
    if (!LoadTheme(m_themePath))
    {
        m_theme = UiTheme{};
    }
    return InitializeRenderer() && InitializeFontAtlas();
}

void UiSystem::Shutdown()
{
    ShutdownRenderer();
}

bool UiSystem::InitializeRenderer()
{
    m_program = CreateProgram(kUiVertexShader, kUiFragmentShader);
    if (m_program == 0)
    {
        return false;
    }
    glGenVertexArrays(1, &m_vao);
    glGenBuffers(1, &m_vbo);
    glBindVertexArray(m_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferData(GL_ARRAY_BUFFER, 6 * 1024 * 1024, nullptr, GL_DYNAMIC_DRAW);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(QuadVertex), reinterpret_cast<void*>(offsetof(QuadVertex, x)));
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(QuadVertex), reinterpret_cast<void*>(offsetof(QuadVertex, u)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(QuadVertex), reinterpret_cast<void*>(offsetof(QuadVertex, r)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, sizeof(QuadVertex), reinterpret_cast<void*>(offsetof(QuadVertex, textured)));
    glEnableVertexAttribArray(3);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);
    m_uniformScreenSize = glGetUniformLocation(m_program, "uScreenSize");
    m_uniformFontTexture = glGetUniformLocation(m_program, "uFontTexture");
    return true;
}

void UiSystem::ShutdownRenderer()
{
    if (m_fontTexture != 0)
    {
        glDeleteTextures(1, &m_fontTexture);
        m_fontTexture = 0;
    }
    if (m_vbo != 0)
    {
        glDeleteBuffers(1, &m_vbo);
        m_vbo = 0;
    }
    if (m_vao != 0)
    {
        glDeleteVertexArrays(1, &m_vao);
        m_vao = 0;
    }
    if (m_program != 0)
    {
        glDeleteProgram(m_program);
        m_program = 0;
    }
}

std::vector<std::string> UiSystem::CandidateFontPaths(const std::string& requested) const
{
    std::vector<std::string> paths;
    if (!requested.empty())
    {
        paths.push_back(requested);
    }
#ifdef _WIN32
    paths.emplace_back("C:/Windows/Fonts/segoeui.ttf");
    paths.emplace_back("C:/Windows/Fonts/arial.ttf");
#else
    paths.emplace_back("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf");
    paths.emplace_back("/usr/share/fonts/dejavu/DejaVuSans.ttf");
    paths.emplace_back("/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf");
#endif
    return paths;
}

bool UiSystem::LoadFontFromPath(const std::string& path)
{
    std::ifstream stream(path, std::ios::binary);
    if (!stream.is_open())
    {
        return false;
    }
    m_fontFileData.assign(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
    if (m_fontFileData.empty())
    {
        return false;
    }

    std::array<unsigned char, 512 * 512> bitmap{};
    std::array<stbtt_bakedchar, 96> baked{};
    const int result = stbtt_BakeFontBitmap(
        m_fontFileData.data(),
        0,
        m_fontPixelHeight,
        bitmap.data(),
        m_fontAtlasWidth,
        m_fontAtlasHeight,
        32,
        96,
        baked.data()
    );
    if (result <= 0)
    {
        return false;
    }

    m_glyphs.clear();
    m_glyphs.reserve(96);
    float minYoff = 0.0F;
    float maxY = 0.0F;
    bool firstGlyph = true;
    for (int i = 0; i < 96; ++i)
    {
        const stbtt_bakedchar& src = baked[static_cast<std::size_t>(i)];
        if (firstGlyph)
        {
            minYoff = src.yoff;
            maxY = src.yoff + static_cast<float>(src.y1 - src.y0);
            firstGlyph = false;
        }
        else
        {
            minYoff = std::min(minYoff, src.yoff);
            maxY = std::max(maxY, src.yoff + static_cast<float>(src.y1 - src.y0));
        }
        m_glyphs.push_back(BakedGlyph{
            32 + i,
            src.x0,
            src.y0,
            src.x1,
            src.y1,
            src.xoff,
            src.yoff,
            src.xadvance,
        });
    }
    m_fontBaselinePx = -minYoff;
    m_fontLineHeightPx = std::max(1.0F, maxY - minYoff);

    if (m_fontTexture == 0)
    {
        glGenTextures(1, &m_fontTexture);
    }
    glBindTexture(GL_TEXTURE_2D, m_fontTexture);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(
        GL_TEXTURE_2D,
        0,
        GL_R8,
        m_fontAtlasWidth,
        m_fontAtlasHeight,
        0,
        GL_RED,
        GL_UNSIGNED_BYTE,
        bitmap.data()
    );
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);
    return true;
}

bool UiSystem::InitializeFontAtlas()
{
    const std::vector<std::string> candidates = CandidateFontPaths(m_theme.fontPath);
    for (const std::string& path : candidates)
    {
        if (LoadFontFromPath(path))
        {
            return true;
        }
    }
    return false;
}

bool UiSystem::LoadTheme(const std::string& themePath)
{
    m_themePath = themePath;
    UiTheme loaded = UiTheme{};

    std::ifstream stream(themePath);
    if (stream.is_open())
    {
        try
        {
            json root;
            stream >> root;
            loaded.fontPath = root.value("font_path", loaded.fontPath);
            loaded.baseFontSize = root.value("base_font_size", loaded.baseFontSize);
            loaded.radius = root.value("radius", loaded.radius);
            loaded.padding = root.value("padding", loaded.padding);
            loaded.spacing = root.value("spacing", loaded.spacing);
            if (root.contains("colors") && root["colors"].is_object())
            {
                const json& colors = root["colors"];
                loaded.colorBackground = JsonColor(colors, "background", loaded.colorBackground);
                loaded.colorPanel = JsonColor(colors, "panel", loaded.colorPanel);
                loaded.colorPanelBorder = JsonColor(colors, "panel_border", loaded.colorPanelBorder);
                loaded.colorText = JsonColor(colors, "text", loaded.colorText);
                loaded.colorTextMuted = JsonColor(colors, "text_muted", loaded.colorTextMuted);
                loaded.colorAccent = JsonColor(colors, "accent", loaded.colorAccent);
                loaded.colorButton = JsonColor(colors, "button", loaded.colorButton);
                loaded.colorButtonHover = JsonColor(colors, "button_hover", loaded.colorButtonHover);
                loaded.colorButtonPressed = JsonColor(colors, "button_pressed", loaded.colorButtonPressed);
                loaded.colorDanger = JsonColor(colors, "danger", loaded.colorDanger);
                loaded.colorSuccess = JsonColor(colors, "success", loaded.colorSuccess);
            }
        }
        catch (const std::exception&)
        {
            return false;
        }
    }
    m_theme = loaded;
    return true;
}

bool UiSystem::ReloadTheme()
{
    if (!LoadTheme(m_themePath))
    {
        return false;
    }
    return InitializeFontAtlas();
}

void UiSystem::SetGlobalUiScale(float value)
{
    m_userScale = std::max(0.5F, std::min(value, 3.0F));
}

bool UiSystem::HasKeyPressed(int key) const
{
    return m_input != nullptr && m_input->IsKeyPressed(key);
}

bool UiSystem::HasMousePressed(int button) const
{
    return m_input != nullptr && !m_mousePressConsumed && m_input->IsMousePressed(button);
}

bool UiSystem::HasMouseDown(int button) const
{
    return m_input != nullptr && m_input->IsMouseDown(button);
}

bool UiSystem::HasMouseReleased(int button) const
{
    return m_input != nullptr && !m_mouseReleaseConsumed && m_input->IsMouseReleased(button);
}

void UiSystem::ConsumeMousePress()
{
    m_mousePressConsumed = true;
}

void UiSystem::ConsumeMouseRelease()
{
    m_mouseReleaseConsumed = true;
}

bool UiSystem::IsShiftDown() const
{
    if (m_input == nullptr)
    {
        return false;
    }
    return m_input->IsKeyDown(GLFW_KEY_LEFT_SHIFT) || m_input->IsKeyDown(GLFW_KEY_RIGHT_SHIFT);
}

std::string UiSystem::CollectTypedCharacters() const
{
    std::string result;
    result.reserve(16);
    if (m_input == nullptr)
    {
        return result;
    }

    const bool shift = IsShiftDown();
    auto appendIf = [&](int key, char normal, char shifted) {
        if (m_input->IsKeyPressed(key))
        {
            result.push_back(shift ? shifted : normal);
        }
    };

    for (int key = GLFW_KEY_A; key <= GLFW_KEY_Z; ++key)
    {
        if (m_input->IsKeyPressed(key))
        {
            const char c = static_cast<char>('a' + (key - GLFW_KEY_A));
            result.push_back(shift ? static_cast<char>(std::toupper(static_cast<unsigned char>(c))) : c);
        }
    }
    appendIf(GLFW_KEY_0, '0', ')');
    appendIf(GLFW_KEY_1, '1', '!');
    appendIf(GLFW_KEY_2, '2', '@');
    appendIf(GLFW_KEY_3, '3', '#');
    appendIf(GLFW_KEY_4, '4', '$');
    appendIf(GLFW_KEY_5, '5', '%');
    appendIf(GLFW_KEY_6, '6', '^');
    appendIf(GLFW_KEY_7, '7', '&');
    appendIf(GLFW_KEY_8, '8', '*');
    appendIf(GLFW_KEY_9, '9', '(');
    appendIf(GLFW_KEY_SPACE, ' ', ' ');
    appendIf(GLFW_KEY_PERIOD, '.', '>');
    appendIf(GLFW_KEY_COMMA, ',', '<');
    appendIf(GLFW_KEY_MINUS, '-', '_');
    appendIf(GLFW_KEY_EQUAL, '=', '+');
    appendIf(GLFW_KEY_SLASH, '/', '?');
    appendIf(GLFW_KEY_SEMICOLON, ';', ':');
    appendIf(GLFW_KEY_APOSTROPHE, '\'', '"');
    appendIf(GLFW_KEY_LEFT_BRACKET, '[', '{');
    appendIf(GLFW_KEY_RIGHT_BRACKET, ']', '}');
    appendIf(GLFW_KEY_BACKSLASH, '\\', '|');
    return result;
}

void UiSystem::BeginFrame(const BeginFrameArgs& args)
{
    m_input = args.input;
    m_screenWidth = std::max(args.framebufferWidth, 1);
    m_screenHeight = std::max(args.framebufferHeight, 1);
    const int windowW = std::max(args.windowWidth, 1);
    const int windowH = std::max(args.windowHeight, 1);
    m_mouseToUiScale = glm::vec2{
        static_cast<float>(m_screenWidth) / static_cast<float>(windowW),
        static_cast<float>(m_screenHeight) / static_cast<float>(windowH),
    };
    m_deltaSeconds = args.deltaSeconds;
    m_interactive = args.interactive;
    m_scale = std::max(0.65F, static_cast<float>(m_screenHeight) / 1080.0F) * m_userScale;

    m_layoutStack.clear();
    m_clipStack.clear();
    m_batches.clear();
    m_batches.reserve(64);
    m_focusOrder.clear();
    m_hoveredId.clear();
    m_mouseCaptured = false;
    m_keyboardCaptured = false;
    m_mousePressConsumed = false;
    m_mouseReleaseConsumed = false;

    if (!HasMouseDown(GLFW_MOUSE_BUTTON_LEFT) && !HasMouseReleased(GLFW_MOUSE_BUTTON_LEFT))
    {
        m_activeId.clear();
    }

    if (!m_lastFrameFocusOrder.empty() && HasKeyPressed(GLFW_KEY_TAB))
    {
        int idx = -1;
        for (int i = 0; i < static_cast<int>(m_lastFrameFocusOrder.size()); ++i)
        {
            if (m_lastFrameFocusOrder[static_cast<std::size_t>(i)] == m_keyboardFocusId)
            {
                idx = i;
                break;
            }
        }
        if (idx < 0)
        {
            idx = 0;
        }
        if (IsShiftDown())
        {
            idx = (idx - 1 + static_cast<int>(m_lastFrameFocusOrder.size())) % static_cast<int>(m_lastFrameFocusOrder.size());
        }
        else
        {
            idx = (idx + 1) % static_cast<int>(m_lastFrameFocusOrder.size());
        }
        m_keyboardFocusId = m_lastFrameFocusOrder[static_cast<std::size_t>(idx)];
        m_keyboardCaptured = true;
    }
}

void UiSystem::EndFrame()
{
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_DEPTH_TEST);

    glUseProgram(m_program);
    glUniform2f(m_uniformScreenSize, static_cast<float>(m_screenWidth), static_cast<float>(m_screenHeight));
    glUniform1i(m_uniformFontTexture, 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_fontTexture);
    glBindVertexArray(m_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);

    // Consolidate all batch vertices into one upload to reduce driver overhead.
    std::size_t totalVertices = 0;
    for (const DrawBatch& batch : m_batches)
    {
        totalVertices += batch.vertices.size();
    }
    if (totalVertices > 0)
    {
        // Upload all vertices in one glBufferData call.
        glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(totalVertices * sizeof(QuadVertex)), nullptr, GL_DYNAMIC_DRAW);
        GLintptr offset = 0;
        for (const DrawBatch& batch : m_batches)
        {
            if (!batch.vertices.empty())
            {
                glBufferSubData(GL_ARRAY_BUFFER, offset, static_cast<GLsizeiptr>(batch.vertices.size() * sizeof(QuadVertex)), batch.vertices.data());
                offset += static_cast<GLintptr>(batch.vertices.size() * sizeof(QuadVertex));
            }
        }

        // Draw each batch as a sub-range with its scissor rect.
        GLint vertexOffset = 0;
        for (const DrawBatch& batch : m_batches)
        {
            if (batch.vertices.empty())
            {
                continue;
            }
            const int sx = batch.clip.x;
            const int syTop = batch.clip.y;
            const int sw = batch.clip.w;
            const int sh = batch.clip.h;
            const int sy = m_screenHeight - (syTop + sh);
            glEnable(GL_SCISSOR_TEST);
            glScissor(sx, std::max(0, sy), sw, sh);
            glDrawArrays(GL_TRIANGLES, vertexOffset, static_cast<GLsizei>(batch.vertices.size()));
            vertexOffset += static_cast<GLint>(batch.vertices.size());
        }
    }

    glDisable(GL_SCISSOR_TEST);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);
    glBindTexture(GL_TEXTURE_2D, 0);
    glUseProgram(0);
    glEnable(GL_DEPTH_TEST);

    m_lastFrameFocusOrder = m_focusOrder;
}

bool UiSystem::WantsInputCapture() const
{
    return m_mouseCaptured || m_keyboardCaptured || !m_activeId.empty() || !m_keyboardFocusId.empty();
}

UiRect UiSystem::CurrentContentRect() const
{
    if (m_layoutStack.empty())
    {
        return UiRect{};
    }
    return m_layoutStack.back().contentRect;
}

void UiSystem::BeginRootPanel(const std::string& id, const UiRect& rect, bool drawBackground)
{
    BeginPanel(id, rect, drawBackground);
}

void UiSystem::BeginPanel(const std::string& id, const UiRect& rect, bool drawBackground)
{
    (void)id;
    if (drawBackground)
    {
        DrawRect(rect, m_theme.colorPanel);
        DrawRectOutline(rect, 1.0F, m_theme.colorPanelBorder);
    }
    PushClipRect(rect);
    LayoutState layout;
    layout.panelRect = rect;
    const float pad = m_theme.padding * m_scale;
    layout.contentRect = UiRect{rect.x + pad, rect.y + pad, std::max(1.0F, rect.w - pad * 2.0F), std::max(1.0F, rect.h - pad * 2.0F)};
    layout.axis = LayoutAxis::Vertical;
    layout.padding = pad;
    layout.spacing = m_theme.spacing * m_scale;
    layout.cursorMain = 0.0F;
    layout.cursorCross = 0.0F;
    layout.usedMain = 0.0F;
    layout.usedCross = 0.0F;
    layout.parentStartMain = 0.0F;
    m_layoutStack.push_back(layout);
}

void UiSystem::EndPanel()
{
    if (!m_layoutStack.empty())
    {
        m_layoutStack.pop_back();
    }
    PopClipRect();
}

void UiSystem::BeginScrollRegion(const std::string& id, float height, float* scrollY)
{
    const UiRect region = AllocateRect(height);
    DrawRect(region, m_theme.colorBackground);
    DrawRectOutline(region, 1.0F, m_theme.colorPanelBorder);
    PushClipRect(region);

    const float pad = std::max(6.0F, m_theme.padding * 0.6F) * m_scale;
    LayoutState layout;
    layout.panelRect = region;
    layout.contentRect = UiRect{
        region.x + pad,
        region.y + pad - (scrollY != nullptr ? std::max(0.0F, *scrollY) : 0.0F),
        std::max(1.0F, region.w - pad * 2.0F - 12.0F * m_scale),
        std::max(1.0F, region.h - pad * 2.0F),
    };
    layout.axis = LayoutAxis::Vertical;
    layout.padding = pad;
    layout.spacing = m_theme.spacing * m_scale;
    layout.cursorMain = 0.0F;
    layout.cursorCross = 0.0F;
    layout.usedMain = 0.0F;
    layout.usedCross = 0.0F;
    layout.parentStartMain = 0.0F;
    m_layoutStack.push_back(layout);

    m_scrollStack.push_back(ScrollState{
        id,
        region,
        UiRect{region.x + pad, region.y + pad, std::max(1.0F, region.w - pad * 2.0F - 12.0F * m_scale), std::max(1.0F, region.h - pad * 2.0F)},
        scrollY,
    });
}

void UiSystem::EndScrollRegion()
{
    if (m_layoutStack.empty() || m_scrollStack.empty())
    {
        return;
    }

    const LayoutState child = m_layoutStack.back();
    const ScrollState scroll = m_scrollStack.back();
    m_layoutStack.pop_back();
    m_scrollStack.pop_back();
    PopClipRect();

    const float contentHeight = std::max(child.usedCross, child.usedMain);
    const float visibleHeight = scroll.contentRectNoScroll.h;
    const float maxScroll = std::max(0.0F, contentHeight - visibleHeight);
    if (scroll.scrollY != nullptr)
    {
        *scroll.scrollY = std::clamp(*scroll.scrollY, 0.0F, maxScroll);
    }

    if (maxScroll <= 1.0e-4F || scroll.scrollY == nullptr)
    {
        return;
    }

    const std::string barId = BuildId(scroll.id + "/scrollbar");
    const float trackW = 8.0F * m_scale;
    const UiRect track{
        scroll.viewportRect.x + scroll.viewportRect.w - trackW - 4.0F * m_scale,
        scroll.contentRectNoScroll.y,
        trackW,
        scroll.contentRectNoScroll.h,
    };
    DrawRect(track, glm::vec4{m_theme.colorPanelBorder.r, m_theme.colorPanelBorder.g, m_theme.colorPanelBorder.b, 0.35F});

    const float ratio = std::clamp(visibleHeight / std::max(1.0F, contentHeight), 0.05F, 1.0F);
    const float thumbH = std::max(20.0F * m_scale, track.h * ratio);
    const float scrollT = std::clamp(*scroll.scrollY / maxScroll, 0.0F, 1.0F);
    const float thumbY = track.y + (track.h - thumbH) * scrollT;
    UiRect thumb{track.x, thumbY, track.w, thumbH};

    const bool trackHovered = IsMouseOver(track);
    const bool thumbHovered = IsMouseOver(thumb);
    if (trackHovered)
    {
        m_mouseCaptured = true;
    }
    if (trackHovered && HasMousePressed(GLFW_MOUSE_BUTTON_LEFT))
    {
        m_activeId = barId;
    }
    if (m_activeId == barId && HasMouseDown(GLFW_MOUSE_BUTTON_LEFT))
    {
        const float mouseY = MousePositionUi().y;
        const float t = std::clamp((mouseY - track.y - thumbH * 0.5F) / std::max(1.0F, (track.h - thumbH)), 0.0F, 1.0F);
        *scroll.scrollY = t * maxScroll;
    }
    if (m_activeId == barId && HasMouseReleased(GLFW_MOUSE_BUTTON_LEFT))
    {
        m_activeId.clear();
    }

    if (trackHovered && m_input != nullptr)
    {
        if (m_input->IsKeyDown(GLFW_KEY_DOWN))
        {
            *scroll.scrollY = std::min(maxScroll, *scroll.scrollY + 220.0F * m_deltaSeconds);
        }
        if (m_input->IsKeyDown(GLFW_KEY_UP))
        {
            *scroll.scrollY = std::max(0.0F, *scroll.scrollY - 220.0F * m_deltaSeconds);
        }
        if (m_input->IsKeyDown(GLFW_KEY_PAGE_DOWN))
        {
            *scroll.scrollY = std::min(maxScroll, *scroll.scrollY + visibleHeight * 0.75F * m_deltaSeconds * 8.0F);
        }
        if (m_input->IsKeyDown(GLFW_KEY_PAGE_UP))
        {
            *scroll.scrollY = std::max(0.0F, *scroll.scrollY - visibleHeight * 0.75F * m_deltaSeconds * 8.0F);
        }
    }

    DrawRect(thumb, thumbHovered || m_activeId == barId ? m_theme.colorAccent : m_theme.colorButtonHover);
    DrawRectOutline(thumb, 1.0F, m_theme.colorPanelBorder);
}

void UiSystem::PushLayout(LayoutAxis axis, float spacing, float padding)
{
    if (m_layoutStack.empty())
    {
        return;
    }
    const LayoutState& parent = m_layoutStack.back();
    LayoutState child = parent;
    child.axis = axis;
    child.spacing = spacing * m_scale;
    child.padding = padding * m_scale;
    child.parentStartMain = parent.cursorMain;
    child.usedMain = 0.0F;
    child.usedCross = 0.0F;
    child.cursorMain = 0.0F;
    child.cursorCross = 0.0F;

    child.contentRect = parent.contentRect;
    if (parent.axis == LayoutAxis::Vertical)
    {
        child.contentRect.y += parent.cursorMain;
        child.contentRect.h = std::max(1.0F, parent.contentRect.h - parent.cursorMain);
    }
    else
    {
        child.contentRect.x += parent.cursorMain;
        child.contentRect.w = std::max(1.0F, parent.contentRect.w - parent.cursorMain);
    }

    m_layoutStack.push_back(child);
}

void UiSystem::PopLayout()
{
    if (m_layoutStack.size() > 1)
    {
        const LayoutState child = m_layoutStack.back();
        m_layoutStack.pop_back();
        LayoutState& parent = m_layoutStack.back();
        const float childFootprintHeight = (child.axis == LayoutAxis::Vertical) ? child.usedMain : child.usedCross;
        const float childFootprintWidth = (child.axis == LayoutAxis::Vertical) ? child.usedCross : child.usedMain;

        float footprintAlongParentMain = childFootprintHeight;
        float footprintAlongParentCross = childFootprintWidth;
        if (parent.axis == LayoutAxis::Horizontal)
        {
            footprintAlongParentMain = childFootprintWidth;
            footprintAlongParentCross = childFootprintHeight;
        }

        const float consumed = child.parentStartMain + footprintAlongParentMain + parent.spacing;
        parent.cursorMain = std::max(parent.cursorMain, consumed);
        parent.usedMain = std::max(parent.usedMain, parent.cursorMain);
        parent.usedCross = std::max(parent.usedCross, footprintAlongParentCross);
    }
}

void UiSystem::Spacer(float pixels)
{
    if (!m_layoutStack.empty())
    {
        m_layoutStack.back().cursorMain += pixels * m_scale;
    }
}

UiRect UiSystem::AllocateRect(float height, float width)
{
    if (m_layoutStack.empty())
    {
        return UiRect{};
    }
    LayoutState& l = m_layoutStack.back();
    const float h = std::max(1.0F, height * m_scale);
    const float w = width > 0.0F ? width * m_scale : l.contentRect.w;
    UiRect rect{};
    if (l.axis == LayoutAxis::Vertical)
    {
        rect = UiRect{l.contentRect.x + l.cursorCross, l.contentRect.y + l.cursorMain, std::max(1.0F, w - l.cursorCross), h};
        l.cursorMain += h + l.spacing;
        l.usedMain = std::max(l.usedMain, l.cursorMain);
        l.usedCross = std::max(l.usedCross, rect.w);
    }
    else
    {
        rect = UiRect{l.contentRect.x + l.cursorMain, l.contentRect.y + l.cursorCross, w, h};
        l.cursorMain += w + l.spacing;
        l.usedMain = std::max(l.usedMain, l.cursorMain);
        l.usedCross = std::max(l.usedCross, rect.h);
    }
    return rect;
}

bool UiSystem::IsMouseOver(const UiRect& rect) const
{
    if (m_input == nullptr)
    {
        return false;
    }
    const glm::vec2 mouse = MousePositionUi();
    return rect.Contains(mouse.x, mouse.y);
}

glm::vec2 UiSystem::MousePositionUi() const
{
    if (m_input == nullptr)
    {
        return glm::vec2{0.0F, 0.0F};
    }
    return m_input->MousePosition() * m_mouseToUiScale;
}

void UiSystem::Label(const std::string& text, const glm::vec4& color, float fontScale, float width)
{
    const UiRect rect = AllocateRect(std::max(20.0F, LineHeight(fontScale) + 4.0F), width);
    DrawText(rect.x, rect.y + 2.0F, text, color, fontScale);
}

void UiSystem::Label(const std::string& text, float fontScale, float width)
{
    Label(text, m_theme.colorText, fontScale, width);
}

void UiSystem::FillRect(const UiRect& rect, const glm::vec4& color)
{
    DrawRect(rect, color);
}

bool UiSystem::Button(const std::string& id, const std::string& label, bool enabled, const glm::vec4* overrideColor, float width)
{
    const UiRect rect = AllocateRect(36.0F, width);
    const std::string fullId = BuildId(id);
    m_focusOrder.push_back(fullId);

    const bool hovered = enabled && m_interactive && IsMouseOver(rect);
    if (hovered)
    {
        m_hoveredId = fullId;
        m_mouseCaptured = true;
    }
    if (hovered && HasMousePressed(GLFW_MOUSE_BUTTON_LEFT))
    {
        m_activeId = fullId;
        m_keyboardFocusId = fullId;
        m_keyboardCaptured = true;
    }
    const bool pressed = (m_activeId == fullId) && HasMouseDown(GLFW_MOUSE_BUTTON_LEFT);
    const bool clicked = enabled && hovered && (m_activeId == fullId) && HasMouseReleased(GLFW_MOUSE_BUTTON_LEFT);

    glm::vec4 fill = overrideColor != nullptr ? *overrideColor : m_theme.colorButton;
    if (!enabled)
    {
        fill *= 0.55F;
    }
    else if (pressed)
    {
        fill = m_theme.colorButtonPressed;
    }
    else if (hovered)
    {
        fill = m_theme.colorButtonHover;
    }
    DrawRect(rect, fill);
    DrawRectOutline(rect, 1.0F, m_theme.colorPanelBorder);

    const float textW = TextWidth(label);
    DrawText(rect.x + (rect.w - textW) * 0.5F, rect.y + (rect.h - LineHeight()) * 0.5F + 1.0F, label, enabled ? m_theme.colorText : m_theme.colorTextMuted);
    return clicked;
}

bool UiSystem::Checkbox(const std::string& id, const std::string& label, bool* value, float width)
{
    const UiRect row = AllocateRect(32.0F, width);
    const UiRect box{row.x, row.y + 6.0F * m_scale, 20.0F * m_scale, 20.0F * m_scale};
    const std::string fullId = BuildId(id);
    m_focusOrder.push_back(fullId);

    const bool hovered = m_interactive && IsMouseOver(row);
    if (hovered)
    {
        m_mouseCaptured = true;
    }
    if (hovered && HasMousePressed(GLFW_MOUSE_BUTTON_LEFT))
    {
        m_activeId = fullId;
        m_keyboardFocusId = fullId;
    }
    const bool clicked = hovered && (m_activeId == fullId) && HasMouseReleased(GLFW_MOUSE_BUTTON_LEFT);
    if (clicked && value != nullptr)
    {
        *value = !(*value);
    }
    if (m_keyboardFocusId == fullId && (HasKeyPressed(GLFW_KEY_SPACE) || HasKeyPressed(GLFW_KEY_ENTER)))
    {
        if (value != nullptr)
        {
            *value = !(*value);
        }
        m_keyboardCaptured = true;
        return true;
    }

    DrawRect(box, m_theme.colorButton);
    DrawRectOutline(box, 1.0F, m_theme.colorPanelBorder);
    if (value != nullptr && *value)
    {
        DrawRect(UiRect{box.x + 4.0F * m_scale, box.y + 4.0F * m_scale, box.w - 8.0F * m_scale, box.h - 8.0F * m_scale}, m_theme.colorAccent);
    }
    DrawText(box.x + box.w + 8.0F * m_scale, row.y + 6.0F * m_scale, label, m_theme.colorText);
    return clicked;
}

bool UiSystem::SliderFloat(const std::string& id, const std::string& label, float* value, float minValue, float maxValue, const char* format, float width)
{
    if (!label.empty())
    {
        Label(label, m_theme.colorTextMuted);
    }
    const UiRect rect = AllocateRect(28.0F, width);
    const std::string fullId = BuildId(id);
    m_focusOrder.push_back(fullId);

    float t = 0.0F;
    if (value != nullptr && maxValue > minValue)
    {
        t = (*value - minValue) / (maxValue - minValue);
    }
    t = std::clamp(t, 0.0F, 1.0F);

    const bool hovered = m_interactive && IsMouseOver(rect);
    if (hovered)
    {
        m_mouseCaptured = true;
    }
    if (hovered && HasMousePressed(GLFW_MOUSE_BUTTON_LEFT))
    {
        m_activeId = fullId;
        m_keyboardFocusId = fullId;
    }
    bool changed = false;
    if ((m_activeId == fullId) && HasMouseDown(GLFW_MOUSE_BUTTON_LEFT) && value != nullptr)
    {
        const float mouseX = MousePositionUi().x;
        const float nt = std::clamp((mouseX - rect.x) / rect.w, 0.0F, 1.0F);
        *value = minValue + (maxValue - minValue) * nt;
        changed = true;
    }
    if (m_keyboardFocusId == fullId && value != nullptr)
    {
        if (HasKeyPressed(GLFW_KEY_LEFT))
        {
            *value = std::max(minValue, *value - (maxValue - minValue) * 0.01F);
            changed = true;
            m_keyboardCaptured = true;
        }
        if (HasKeyPressed(GLFW_KEY_RIGHT))
        {
            *value = std::min(maxValue, *value + (maxValue - minValue) * 0.01F);
            changed = true;
            m_keyboardCaptured = true;
        }
    }

    DrawRect(rect, m_theme.colorButton);
    DrawRectOutline(rect, 1.0F, m_theme.colorPanelBorder);
    DrawRect(UiRect{rect.x, rect.y, rect.w * t, rect.h}, m_theme.colorAccent);
    DrawRect(UiRect{rect.x + rect.w * t - 3.0F * m_scale, rect.y, 6.0F * m_scale, rect.h}, m_theme.colorText);

    char valueBuffer[64]{};
    if (value != nullptr)
    {
        std::snprintf(valueBuffer, sizeof(valueBuffer), format != nullptr ? format : "%.2f", *value);
    }
    DrawText(rect.x + rect.w - TextWidth(valueBuffer) - 6.0F * m_scale, rect.y + 4.0F * m_scale, valueBuffer, m_theme.colorText);
    return changed;
}

bool UiSystem::SliderInt(const std::string& id, const std::string& label, int* value, int minValue, int maxValue, float width)
{
    if (value == nullptr)
    {
        return false;
    }
    float asFloat = static_cast<float>(*value);
    const bool changed = SliderFloat(id, label, &asFloat, static_cast<float>(minValue), static_cast<float>(maxValue), "%.0f", width);
    if (changed)
    {
        *value = static_cast<int>(std::round(asFloat));
    }
    return changed;
}

bool UiSystem::Dropdown(const std::string& id, const std::string& label, int* selectedIndex, const std::vector<std::string>& items, float width)
{
    if (!label.empty())
    {
        Label(label, m_theme.colorTextMuted);
    }
    const UiRect rect = AllocateRect(34.0F, width);
    const std::string fullId = BuildId(id);
    m_focusOrder.push_back(fullId);
    WidgetState& state = m_widgetState[fullId];

    const bool hovered = m_interactive && IsMouseOver(rect);
    if (hovered)
    {
        m_mouseCaptured = true;
    }
    if (hovered && HasMousePressed(GLFW_MOUSE_BUTTON_LEFT))
    {
        m_activeId = fullId;
        m_keyboardFocusId = fullId;
    }
    if (hovered && (m_activeId == fullId) && HasMouseReleased(GLFW_MOUSE_BUTTON_LEFT))
    {
        state.open = !state.open;
    }

    DrawRect(rect, m_theme.colorButton);
    DrawRectOutline(rect, 1.0F, m_theme.colorPanelBorder);
    std::string preview = "None";
    if (selectedIndex != nullptr && *selectedIndex >= 0 && *selectedIndex < static_cast<int>(items.size()))
    {
        preview = items[static_cast<std::size_t>(*selectedIndex)];
    }
    DrawText(rect.x + 8.0F * m_scale, rect.y + 7.0F * m_scale, preview, m_theme.colorText);
    DrawText(rect.x + rect.w - 14.0F * m_scale, rect.y + 7.0F * m_scale, state.open ? "^" : "v", m_theme.colorTextMuted);

    bool changed = false;
    if (state.open)
    {
        const float itemH = 30.0F * m_scale;
        const float popupH = itemH * static_cast<float>(std::max(1, static_cast<int>(items.size())));
        float popupY = rect.y + rect.h + 2.0F * m_scale;
        if (!m_layoutStack.empty())
        {
            const UiRect content = m_layoutStack.back().contentRect;
            const float contentBottom = content.y + content.h;
            if (popupY + popupH > contentBottom)
            {
                popupY = std::max(content.y, rect.y - popupH - 2.0F * m_scale);
            }
            if (popupY >= rect.y)
            {
                m_layoutStack.back().cursorMain += popupH + 2.0F * m_scale;
            }
        }

        UiRect popup{rect.x, popupY, rect.w, popupH};
        DrawRect(popup, m_theme.colorButtonHover);
        DrawRectOutline(popup, 1.0F, m_theme.colorPanelBorder);
        const bool hoveredAny = hovered || IsMouseOver(popup);
        if (!hoveredAny && HasMousePressed(GLFW_MOUSE_BUTTON_LEFT))
        {
            state.open = false;
        }
        for (int i = 0; i < static_cast<int>(items.size()); ++i)
        {
            UiRect itemRect{popup.x, popup.y + itemH * static_cast<float>(i), popup.w, itemH};
            const bool itemHovered = m_interactive && IsMouseOver(itemRect);
            if (itemHovered)
            {
                m_mouseCaptured = true;
                DrawRect(itemRect, m_theme.colorButtonHover);
                if (HasMousePressed(GLFW_MOUSE_BUTTON_LEFT))
                {
                    m_activeId = fullId + "/item";
                    ConsumeMousePress();
                }
                if ((m_activeId == fullId + "/item") && HasMouseReleased(GLFW_MOUSE_BUTTON_LEFT))
                {
                    if (selectedIndex != nullptr)
                    {
                        *selectedIndex = i;
                    }
                    state.open = false;
                    changed = true;
                    ConsumeMouseRelease();
                }
            }
            DrawText(itemRect.x + 8.0F * m_scale, itemRect.y + 6.0F * m_scale, items[static_cast<std::size_t>(i)], m_theme.colorText);
        }
    }
    return changed;
}

bool UiSystem::InputText(const std::string& id, const std::string& label, std::string* value, std::size_t maxChars, float width)
{
    if (!label.empty())
    {
        Label(label, m_theme.colorTextMuted);
    }
    const UiRect rect = AllocateRect(34.0F, width);
    const std::string fullId = BuildId(id);
    m_focusOrder.push_back(fullId);

    const bool hovered = m_interactive && IsMouseOver(rect);
    if (hovered)
    {
        m_mouseCaptured = true;
    }
    if (hovered && HasMousePressed(GLFW_MOUSE_BUTTON_LEFT))
    {
        m_keyboardFocusId = fullId;
        m_activeId = fullId;
    }

    bool changed = false;
    if (m_keyboardFocusId == fullId && value != nullptr)
    {
        m_keyboardCaptured = true;
        if (HasKeyPressed(GLFW_KEY_ESCAPE))
        {
            m_keyboardFocusId.clear();
        }
        if (HasKeyPressed(GLFW_KEY_BACKSPACE) && !value->empty())
        {
            value->pop_back();
            changed = true;
        }
        const std::string chars = CollectTypedCharacters();
        for (char c : chars)
        {
            if (value->size() >= maxChars)
            {
                break;
            }
            value->push_back(c);
            changed = true;
        }
    }

    DrawRect(rect, m_theme.colorButton);
    DrawRectOutline(rect, 1.0F, m_keyboardFocusId == fullId ? m_theme.colorAccent : m_theme.colorPanelBorder);
    std::string text = value != nullptr ? *value : std::string{};
    if (m_keyboardFocusId == fullId && static_cast<int>(std::fmod(glfwGetTime() * 2.0, 2.0)) == 0)
    {
        text += "|";
    }
    DrawText(rect.x + 8.0F * m_scale, rect.y + 7.0F * m_scale, text, m_theme.colorText);
    return changed;
}

void UiSystem::ProgressBar(const std::string& id, float value01, const std::string& overlay, float width)
{
    const UiRect rect = AllocateRect(24.0F, width);
    (void)id;
    const float t = std::clamp(value01, 0.0F, 1.0F);
    DrawRect(rect, m_theme.colorButton);
    DrawRectOutline(rect, 1.0F, m_theme.colorPanelBorder);
    DrawRect(UiRect{rect.x, rect.y, rect.w * t, rect.h}, m_theme.colorAccent);
    if (!overlay.empty())
    {
        const float tw = TextWidth(overlay);
        DrawText(rect.x + (rect.w - tw) * 0.5F, rect.y + 3.0F * m_scale, overlay, m_theme.colorText);
    }
}

void UiSystem::SkillCheckBar(const std::string& id, float needle01, float successStart01, float successEnd01, float width)
{
    const UiRect rect = AllocateRect(28.0F, width);
    (void)id;

    const float n = std::clamp(needle01, 0.0F, 1.0F);
    const float s0 = std::clamp(successStart01, 0.0F, 1.0F);
    const float s1 = std::clamp(successEnd01, 0.0F, 1.0F);

    DrawRect(rect, m_theme.colorButton);
    DrawRectOutline(rect, 1.0F, m_theme.colorPanelBorder);

    const float zoneX = rect.x + rect.w * std::min(s0, s1);
    const float zoneW = rect.w * std::max(0.0F, std::abs(s1 - s0));
    DrawRect(UiRect{zoneX, rect.y, std::max(2.0F * m_scale, zoneW), rect.h}, m_theme.colorSuccess * glm::vec4{1.0F, 1.0F, 1.0F, 0.55F});

    const float needleX = rect.x + rect.w * n;
    DrawRect(UiRect{needleX - 2.0F * m_scale, rect.y - 2.0F * m_scale, 4.0F * m_scale, rect.h + 4.0F * m_scale}, m_theme.colorDanger);
}

bool UiSystem::KeybindCapture(const std::string& id, const std::string& label, bool capturing, std::string* outCapturedLabel, float width)
{
    const bool clicked = Button(id, capturing ? "Press key..." : label, true, nullptr, width);
    if (capturing && outCapturedLabel != nullptr)
    {
        const std::string chars = CollectTypedCharacters();
        if (!chars.empty())
        {
            *outCapturedLabel = chars;
            return true;
        }
    }
    return clicked;
}

std::string UiSystem::BuildId(const std::string& localId) const
{
    if (m_idScopeStack.empty())
    {
        return localId;
    }
    std::ostringstream oss;
    for (const std::string& scope : m_idScopeStack)
    {
        oss << scope << "/";
    }
    oss << localId;
    return oss.str();
}

void UiSystem::PushIdScope(const std::string& scopeId)
{
    m_idScopeStack.push_back(scopeId);
}

void UiSystem::PopIdScope()
{
    if (!m_idScopeStack.empty())
    {
        m_idScopeStack.pop_back();
    }
}

void UiSystem::DrawRect(const UiRect& rect, const glm::vec4& color)
{
    EmitQuad(rect, color, 0.0F, 0.0F, 0.0F, 0.0F, 0.0F);
}

void UiSystem::DrawFullscreenVignette(const glm::vec4& color)
{
    if (m_screenWidth <= 0 || m_screenHeight <= 0)
    {
        return;
    }

    const UiRect fullScreen{0.0F, 0.0F, static_cast<float>(m_screenWidth), static_cast<float>(m_screenHeight)};
    EmitQuad(fullScreen, color, 0.0F, 0.0F, 1.0F, 1.0F, -1.0F);
}

void UiSystem::DrawRectOutline(const UiRect& rect, float thickness, const glm::vec4& color)
{
    const float t = std::max(1.0F, thickness * m_scale);
    DrawRect(UiRect{rect.x, rect.y, rect.w, t}, color);
    DrawRect(UiRect{rect.x, rect.y + rect.h - t, rect.w, t}, color);
    DrawRect(UiRect{rect.x, rect.y, t, rect.h}, color);
    DrawRect(UiRect{rect.x + rect.w - t, rect.y, t, rect.h}, color);
}

float UiSystem::LineHeight(float fontScale) const
{
    const float s = (m_theme.baseFontSize * m_scale * std::max(0.5F, fontScale)) / m_fontPixelHeight;
    return std::max(1.0F, m_fontLineHeightPx * s);
}

float UiSystem::TextWidth(std::string_view text, float fontScale) const
{
    const float s = (m_theme.baseFontSize * m_scale * fontScale) / m_fontPixelHeight;
    float width = 0.0F;
    for (char ch : text)
    {
        if (ch < 32 || ch > 126)
        {
            width += 6.0F * s;
            continue;
        }
        width += m_glyphs[static_cast<std::size_t>(ch - 32)].xadvance * s;
    }
    return width;
}

void UiSystem::DrawText(float x, float y, std::string_view text, const glm::vec4& color, float fontScale)
{
    if (m_glyphs.empty())
    {
        return;
    }
    const float s = (m_theme.baseFontSize * m_scale * fontScale) / m_fontPixelHeight;
    float penX = x;
    float penY = y + m_fontBaselinePx * s;
    for (char ch : text)
    {
        if (ch == '\n')
        {
            penX = x;
            penY += LineHeight(fontScale);
            continue;
        }
        if (ch < 32 || ch > 126)
        {
            penX += 6.0F * s;
            continue;
        }
        const BakedGlyph& g = m_glyphs[static_cast<std::size_t>(ch - 32)];
        const float gx0 = penX + g.xoff * s;
        const float gy0 = penY + g.yoff * s;
        const float gx1 = gx0 + static_cast<float>(g.x1 - g.x0) * s;
        const float gy1 = gy0 + static_cast<float>(g.y1 - g.y0) * s;
        const float u0 = static_cast<float>(g.x0) / static_cast<float>(m_fontAtlasWidth);
        const float v0 = static_cast<float>(g.y0) / static_cast<float>(m_fontAtlasHeight);
        const float u1 = static_cast<float>(g.x1) / static_cast<float>(m_fontAtlasWidth);
        const float v1 = static_cast<float>(g.y1) / static_cast<float>(m_fontAtlasHeight);
        EmitTexturedQuad(gx0, gy0, gx1, gy1, u0, v0, u1, v1, color);
        penX += g.xadvance * s;
    }
}

void UiSystem::DrawTextLabel(float x, float y, std::string_view text, const glm::vec4& color, float fontScale)
{
    DrawText(x, y, text, color, fontScale);
}

void UiSystem::PushClipRect(const UiRect& rect)
{
    ClipRect clip{
        static_cast<int>(std::floor(rect.x)),
        static_cast<int>(std::floor(rect.y)),
        static_cast<int>(std::ceil(rect.w)),
        static_cast<int>(std::ceil(rect.h)),
    };
    clip.x = std::max(0, clip.x);
    clip.y = std::max(0, clip.y);
    clip.w = std::max(0, std::min(clip.w, m_screenWidth - clip.x));
    clip.h = std::max(0, std::min(clip.h, m_screenHeight - clip.y));
    if (!m_clipStack.empty())
    {
        const ClipRect parent = m_clipStack.back();
        const int nx0 = std::max(parent.x, clip.x);
        const int ny0 = std::max(parent.y, clip.y);
        const int nx1 = std::min(parent.x + parent.w, clip.x + clip.w);
        const int ny1 = std::min(parent.y + parent.h, clip.y + clip.h);
        clip.x = nx0;
        clip.y = ny0;
        clip.w = std::max(0, nx1 - nx0);
        clip.h = std::max(0, ny1 - ny0);
    }
    m_clipStack.push_back(clip);
}

void UiSystem::PopClipRect()
{
    if (!m_clipStack.empty())
    {
        m_clipStack.pop_back();
    }
}

UiSystem::ClipRect UiSystem::CurrentClipRect() const
{
    if (m_clipStack.empty())
    {
        return ClipRect{0, 0, m_screenWidth, m_screenHeight};
    }
    return m_clipStack.back();
}

UiSystem::DrawBatch& UiSystem::ActiveBatch()
{
    const ClipRect clip = CurrentClipRect();
    if (m_batches.empty())
    {
        m_batches.push_back(DrawBatch{clip, {}});
        m_batches.back().vertices.reserve(1024);
        return m_batches.back();
    }
    DrawBatch& last = m_batches.back();
    if (last.clip.x != clip.x || last.clip.y != clip.y || last.clip.w != clip.w || last.clip.h != clip.h)
    {
        m_batches.push_back(DrawBatch{clip, {}});
        m_batches.back().vertices.reserve(1024);
        return m_batches.back();
    }
    return last;
}

void UiSystem::EmitQuad(const UiRect& rect, const glm::vec4& color, float u0, float v0, float u1, float v1, float mode)
{
    DrawBatch& batch = ActiveBatch();
    auto push = [&](float x, float y, float u, float v) {
        batch.vertices.push_back(QuadVertex{x, y, u, v, color.r, color.g, color.b, color.a, mode});
    };
    const float x0 = rect.x;
    const float y0 = rect.y;
    const float x1 = rect.x + rect.w;
    const float y1 = rect.y + rect.h;
    push(x0, y0, u0, v0);
    push(x1, y0, u1, v0);
    push(x1, y1, u1, v1);
    push(x0, y0, u0, v0);
    push(x1, y1, u1, v1);
    push(x0, y1, u0, v1);
}

void UiSystem::EmitTexturedQuad(float x0, float y0, float x1, float y1, float u0, float v0, float u1, float v1, const glm::vec4& color)
{
    EmitQuad(UiRect{x0, y0, x1 - x0, y1 - y0}, color, u0, v0, u1, v1, 1.0F);
}

std::uint32_t UiSystem::HashString(const std::string& value) const
{
    return static_cast<std::uint32_t>(std::hash<std::string>{}(value));
}
} // namespace engine::ui
