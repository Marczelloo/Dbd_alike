#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <glm/vec2.hpp>
#include <glm/vec4.hpp>

namespace engine::platform
{
class Input;
}

namespace engine::ui
{
struct UiRect
{
    float x = 0.0F;
    float y = 0.0F;
    float w = 0.0F;
    float h = 0.0F;

    [[nodiscard]] bool Contains(float px, float py) const
    {
        return px >= x && py >= y && px <= x + w && py <= y + h;
    }
};

struct UiTheme
{
    std::string fontPath;
    float baseFontSize = 18.0F;
    float radius = 8.0F;
    float padding = 12.0F;
    float spacing = 8.0F;

    glm::vec4 colorBackground{0.06F, 0.07F, 0.09F, 0.95F};
    glm::vec4 colorPanel{0.10F, 0.12F, 0.16F, 0.94F};
    glm::vec4 colorPanelBorder{0.30F, 0.36F, 0.45F, 1.0F};
    glm::vec4 colorText{0.92F, 0.94F, 0.98F, 1.0F};
    glm::vec4 colorTextMuted{0.70F, 0.75F, 0.82F, 1.0F};
    glm::vec4 colorAccent{0.21F, 0.62F, 0.92F, 1.0F};
    glm::vec4 colorButton{0.18F, 0.22F, 0.30F, 1.0F};
    glm::vec4 colorButtonHover{0.25F, 0.31F, 0.42F, 1.0F};
    glm::vec4 colorButtonPressed{0.33F, 0.42F, 0.58F, 1.0F};
    glm::vec4 colorDanger{0.84F, 0.26F, 0.25F, 1.0F};
    glm::vec4 colorSuccess{0.22F, 0.70F, 0.38F, 1.0F};
};

class UiSystem
{
public:
    enum class LayoutAxis
    {
        Vertical,
        Horizontal
    };

    struct BeginFrameArgs
    {
        const platform::Input* input = nullptr;
        int framebufferWidth = 0;
        int framebufferHeight = 0;
        int windowWidth = 0;
        int windowHeight = 0;
        float deltaSeconds = 0.0F;
        bool interactive = true;
    };

    bool Initialize();
    void Shutdown();

    void BeginFrame(const BeginFrameArgs& args);
    void EndFrame();

    [[nodiscard]] bool WantsInputCapture() const;

    [[nodiscard]] float Scale() const { return m_scale; }
    [[nodiscard]] int ScreenWidth() const { return m_screenWidth; }
    [[nodiscard]] int ScreenHeight() const { return m_screenHeight; }

    [[nodiscard]] const UiTheme& Theme() const { return m_theme; }
    bool LoadTheme(const std::string& themePath);
    bool ReloadTheme();

    void BeginRootPanel(const std::string& id, const UiRect& rect, bool drawBackground = true);
    void BeginPanel(const std::string& id, const UiRect& rect, bool drawBackground = true);
    void EndPanel();
    void BeginScrollRegion(const std::string& id, float height, float* scrollY);
    void EndScrollRegion();

    void PushLayout(LayoutAxis axis, float spacing, float padding);
    void PopLayout();

    void Spacer(float pixels);
    [[nodiscard]] UiRect AllocateRect(float height, float width = -1.0F);
    [[nodiscard]] UiRect CurrentContentRect() const;

    void Label(const std::string& text, const glm::vec4& color, float fontScale = 1.0F, float width = -1.0F);
    void Label(const std::string& text, float fontScale = 1.0F, float width = -1.0F);
    void FillRect(const UiRect& rect, const glm::vec4& color);

    bool Button(const std::string& id, const std::string& label, bool enabled = true, const glm::vec4* overrideColor = nullptr, float width = -1.0F);
    bool Checkbox(const std::string& id, const std::string& label, bool* value, float width = -1.0F);
    bool SliderFloat(const std::string& id, const std::string& label, float* value, float minValue, float maxValue, const char* format = "%.2f", float width = -1.0F);
    bool SliderInt(const std::string& id, const std::string& label, int* value, int minValue, int maxValue, float width = -1.0F);
    bool Dropdown(const std::string& id, const std::string& label, int* selectedIndex, const std::vector<std::string>& items, float width = -1.0F);
    bool InputText(const std::string& id, const std::string& label, std::string* value, std::size_t maxChars = 128, float width = -1.0F);
    void ProgressBar(const std::string& id, float value01, const std::string& overlay = "", float width = -1.0F);
    void SkillCheckBar(const std::string& id, float needle01, float successStart01, float successEnd01, float width = -1.0F);
    bool KeybindCapture(const std::string& id, const std::string& label, bool capturing, std::string* outCapturedLabel, float width = -1.0F);

    void SetGlobalUiScale(float value);
    [[nodiscard]] float GlobalUiScale() const { return m_userScale; }

    [[nodiscard]] std::string BuildId(const std::string& localId) const;
    void PushIdScope(const std::string& scopeId);
    void PopIdScope();

    // Low-level drawing (public for custom HUD panels with drag headers)
    void DrawRect(const UiRect& rect, const glm::vec4& color);
    void DrawRectOutline(const UiRect& rect, float thickness, const glm::vec4& color);
    void DrawTextLabel(float x, float y, std::string_view text, const glm::vec4& color, float fontScale = 1.0F);
    void DrawFullscreenVignette(const glm::vec4& color);

private:
    struct QuadVertex
    {
        float x = 0.0F;
        float y = 0.0F;
        float u = 0.0F;
        float v = 0.0F;
        float r = 1.0F;
        float g = 1.0F;
        float b = 1.0F;
        float a = 1.0F;
        float textured = 0.0F;
    };

    struct LayoutState
    {
        UiRect panelRect{};
        UiRect contentRect{};
        float cursorMain = 0.0F;
        float cursorCross = 0.0F;
        LayoutAxis axis = LayoutAxis::Vertical;
        float spacing = 8.0F;
        float padding = 10.0F;
        float usedMain = 0.0F;
        float usedCross = 0.0F;
        float parentStartMain = 0.0F;
    };

    struct ScrollState
    {
        std::string id;
        UiRect viewportRect{};
        UiRect contentRectNoScroll{};
        float* scrollY = nullptr;
    };

    struct WidgetState
    {
        bool open = false;
        float value01 = 0.0F;
        int intValue = 0;
        std::string text;
    };

    struct ClipRect
    {
        int x = 0;
        int y = 0;
        int w = 0;
        int h = 0;
    };

    struct DrawBatch
    {
        ClipRect clip{};
        std::vector<QuadVertex> vertices;
    };

    struct BakedGlyph
    {
        int codepoint = 0;
        int x0 = 0;
        int y0 = 0;
        int x1 = 0;
        int y1 = 0;
        float xoff = 0.0F;
        float yoff = 0.0F;
        float xadvance = 0.0F;
    };

    bool InitializeRenderer();
    void ShutdownRenderer();
    bool InitializeFontAtlas();
    bool LoadFontFromPath(const std::string& path);
    [[nodiscard]] std::vector<std::string> CandidateFontPaths(const std::string& requested) const;

    void ProcessInput();
    [[nodiscard]] std::string CollectTypedCharacters() const;
    [[nodiscard]] bool IsShiftDown() const;
    [[nodiscard]] bool IsMouseOver(const UiRect& rect) const;
    [[nodiscard]] glm::vec2 MousePositionUi() const;
    [[nodiscard]] bool IsFocusableWidget(const std::string& id) const;

    void DrawText(float x, float y, std::string_view text, const glm::vec4& color, float fontScale = 1.0F);
    [[nodiscard]] float TextWidth(std::string_view text, float fontScale = 1.0F) const;
    [[nodiscard]] float LineHeight(float fontScale = 1.0F) const;

    void PushClipRect(const UiRect& rect);
    void PopClipRect();
    [[nodiscard]] ClipRect CurrentClipRect() const;
    DrawBatch& ActiveBatch();
    void EmitQuad(
        const UiRect& rect,
        const glm::vec4& color,
        float u0,
        float v0,
        float u1,
        float v1,
        float mode
    );
    void EmitTexturedQuad(
        float x0,
        float y0,
        float x1,
        float y1,
        float u0,
        float v0,
        float u1,
        float v1,
        const glm::vec4& color
    );

    [[nodiscard]] std::uint32_t HashString(const std::string& value) const;
    [[nodiscard]] bool HasKeyPressed(int key) const;
    [[nodiscard]] bool HasMousePressed(int button) const;
    [[nodiscard]] bool HasMouseDown(int button) const;
    [[nodiscard]] bool HasMouseReleased(int button) const;
    void ConsumeMousePress();
    void ConsumeMouseRelease();

    const platform::Input* m_input = nullptr;
    int m_screenWidth = 0;
    int m_screenHeight = 0;
    float m_deltaSeconds = 0.0F;
    bool m_interactive = true;
    glm::vec2 m_mouseToUiScale{1.0F, 1.0F};

    float m_scale = 1.0F;
    float m_userScale = 1.0F;
    UiTheme m_theme{};
    std::string m_themePath = "ui/theme.json";

    std::vector<LayoutState> m_layoutStack;
    std::vector<ScrollState> m_scrollStack;
    std::vector<std::string> m_idScopeStack;
    std::vector<ClipRect> m_clipStack;

    std::unordered_map<std::string, WidgetState> m_widgetState;
    std::vector<std::string> m_focusOrder;
    std::vector<std::string> m_lastFrameFocusOrder;
    std::string m_hoveredId;
    std::string m_activeId;
    std::string m_keyboardFocusId;
    bool m_mouseCaptured = false;
    bool m_keyboardCaptured = false;
    bool m_mousePressConsumed = false;
    bool m_mouseReleaseConsumed = false;

    std::vector<DrawBatch> m_batches;

    unsigned int m_program = 0;
    unsigned int m_vbo = 0;
    unsigned int m_vao = 0;
    int m_uniformScreenSize = -1;
    int m_uniformFontTexture = -1;
    unsigned int m_fontTexture = 0;

    std::vector<unsigned char> m_fontFileData;
    std::vector<BakedGlyph> m_glyphs;
    int m_fontAtlasWidth = 512;
    int m_fontAtlasHeight = 512;
    float m_fontPixelHeight = 42.0F;
    float m_fontBaselinePx = 0.0F;
    float m_fontLineHeightPx = 42.0F;
};
} // namespace engine::ui
