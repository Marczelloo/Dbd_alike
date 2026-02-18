#pragma once

#include <algorithm>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <glm/vec2.hpp>
#include <glm/vec4.hpp>

namespace engine::ui
{
// Forward declarations
struct StyleProps;
struct ComputedStyle;

// Node types
enum class UINodeType
{
    Panel,
    Text,
    Button,
    Image,
    Shape,
    Slider,
    Toggle,
    ScrollView,
    TextInput,
    ProgressBar,
    Spacer,
    Container
};

enum class UIShapeType
{
    Rectangle,
    Circle,
    Line
};

// Node visibility
enum class Visibility
{
    Visible,   // Rendered and participates in layout
    Hidden,    // Not rendered but participates in layout
    Collapsed  // Not rendered and does not participate in layout
};

// Layout display mode
enum class Display
{
    Flex,
    Grid,
    Block,
    None
};

// Positioning mode
enum class Position
{
    Relative,
    Absolute
};

// Flex direction
enum class FlexDirection
{
    Row,
    Column,
    RowReverse,
    ColumnReverse
};

// Flex justify content
enum class JustifyContent
{
    FlexStart,
    FlexEnd,
    Center,
    SpaceBetween,
    SpaceAround,
    SpaceEvenly
};

// Flex align items
enum class AlignItems
{
    FlexStart,
    FlexEnd,
    Center,
    Stretch,
    Baseline
};

enum class GridItemAlign
{
    Start,
    End,
    Center,
    Stretch
};

// Overflow behavior
enum class Overflow
{
    Visible,
    Hidden,
    Scroll
};

// Size value (can be auto, pixels, percent, or viewport-based)
struct SizeValue
{
    enum class Unit
    {
        Auto,
        Px,
        Percent,
        Vw,
        Vh
    };

    float value = 0.0F;
    Unit unit = Unit::Auto;

    static SizeValue Auto()
    {
        return {0.0F, Unit::Auto};
    }
    static SizeValue Px(float v)
    {
        return {v, Unit::Px};
    }
    static SizeValue Percent(float v)
    {
        return {v, Unit::Percent};
    }
    static SizeValue Vw(float v)
    {
        return {v, Unit::Vw};
    }
    static SizeValue Vh(float v)
    {
        return {v, Unit::Vh};
    }

    [[nodiscard]] bool IsAuto() const
    {
        return unit == Unit::Auto;
    }
    [[nodiscard]] bool IsFixed() const
    {
        return unit == Unit::Px;
    }
    [[nodiscard]] bool IsRelative() const
    {
        return unit == Unit::Percent || unit == Unit::Vw || unit == Unit::Vh;
    }
};

// Edge insets (padding, margin)
struct EdgeInsets
{
    float top = 0.0F;
    float right = 0.0F;
    float bottom = 0.0F;
    float left = 0.0F;

    EdgeInsets() = default;
    explicit EdgeInsets(float all) : top(all), right(all), bottom(all), left(all)
    {
    }
    EdgeInsets(float t, float r, float b, float l) : top(t), right(r), bottom(b), left(l)
    {
    }

    static EdgeInsets All(float v)
    {
        return EdgeInsets(v, v, v, v);
    }
    static EdgeInsets Vertical(float v)
    {
        return EdgeInsets(v, 0.0F, v, 0.0F);
    }
    static EdgeInsets Horizontal(float v)
    {
        return EdgeInsets(0.0F, v, 0.0F, v);
    }
    static EdgeInsets Symmetric(float v, float h)
    {
        return EdgeInsets(v, h, v, h);
    }
};

// Shadow properties
struct ShadowProps
{
    glm::vec2 offset{0.0F, 0.0F};
    float blur = 0.0F;
    float spread = 0.0F;
    glm::vec4 color{0.0F, 0.0F, 0.0F, 0.5F};
};

// Font properties
struct FontProps
{
    std::string family;
    float size = 16.0F;
    enum class Weight
    {
        ExtraLight,
        Light,
        Normal,
        Medium,
        SemiBold,
        Bold,
        ExtraBold
    } weight = Weight::Normal;
    enum class Style
    {
        Normal,
        Italic
    } style = Style::Normal;
    enum class Align
    {
        Left,
        Center,
        Right
    } align = Align::Center;
    bool underline = false;
    bool strikethrough = false;
    float letterSpacing = 0.0F;
};

// Transition definition
struct TransitionDef
{
    std::string property;  // "opacity", "backgroundColor", "translateX", etc.
    float duration = 0.2F; // seconds
    enum class Ease
    {
        Linear,
        EaseIn,
        EaseOut,
        EaseInOut,
        EaseInQuad,
        EaseOutQuad,
        EaseInOutQuad,
        EaseInCubic,
        EaseOutCubic,
        EaseInOutCubic
    } ease = Ease::EaseOut;
};

// Layout properties for a node
struct LayoutProps
{
    // Display mode
    Display display = Display::Flex;

    // Positioning
    Position position = Position::Relative;

    // Flex container properties
    FlexDirection flexDirection = FlexDirection::Column;
    JustifyContent justifyContent = JustifyContent::FlexStart;
    AlignItems alignItems = AlignItems::Stretch;
    float gap = 0.0F;
    int gridColumns = 1;
    int gridRows = 0;
    SizeValue gridColumnSize = SizeValue::Auto();
    SizeValue gridRowSize = SizeValue::Auto();
    float gridColumnGap = -1.0F; // < 0 means fallback to gap
    float gridRowGap = -1.0F;    // < 0 means fallback to gap
    GridItemAlign gridJustifyItems = GridItemAlign::Stretch;
    GridItemAlign gridAlignItems = GridItemAlign::Stretch;
    std::string gridTemplateAreas;
    std::string gridArea;
    int gridColumnStart = 0; // 1-based line index, 0 = auto placement
    int gridRowStart = 0;    // 1-based line index, 0 = auto placement
    int gridColumnSpan = 1;
    int gridRowSpan = 1;

    // Box model
    EdgeInsets padding;
    EdgeInsets margin;

    // Sizing
    SizeValue width = SizeValue::Auto();
    SizeValue height = SizeValue::Auto();
    SizeValue minWidth;
    SizeValue maxWidth;
    SizeValue minHeight;
    SizeValue maxHeight;

    // Flex child properties
    float flexGrow = 0.0F;
    float flexShrink = 1.0F;
    SizeValue flexBasis = SizeValue::Auto();

    // Anchor positioning (for absolute/HUD elements)
    std::optional<glm::vec2> anchor;  // (0..1, 0..1) relative to parent
    glm::vec2 offset{0.0F, 0.0F};
    glm::vec2 pivot{0.5F, 0.5F};  // (0..1, 0..1) pivot point

    // Overflow
    Overflow overflow = Overflow::Visible;

    // Aspect ratio (width / height), 0 = ignore
    float aspectRatio = 0.0F;
};

// Node runtime state
struct NodeState
{
    bool hover = false;
    bool pressed = false;
    bool focused = false;
    bool disabled = false;
    bool selected = false;
    bool dragging = false;

    // Toggle/checkbox state
    bool checked = false;

    // Slider value (0-1)
    float value01 = 0.0F;

    // Text input
    std::string text;
    int cursorPos = 0;

    // ScrollView scroll position
    float scrollX = 0.0F;
    float scrollY = 0.0F;
};

// Computed rectangle after layout
struct ComputedRect
{
    float x = 0.0F;
    float y = 0.0F;
    float w = 0.0F;
    float h = 0.0F;
    float contentX = 0.0F;  // Content area (after padding)
    float contentY = 0.0F;
    float contentW = 0.0F;
    float contentH = 0.0F;

    [[nodiscard]] bool Contains(float px, float py) const
    {
        return px >= x && py >= y && px <= x + w && py <= y + h;
    }
};

// Forward declaration for UINode
class UINode;

// UINode is the retained-mode UI element
class UINode
{
public:
    // Identification
    std::string id;    // Stable unique ID
    std::string name;  // Editor-friendly name

    // Type
    UINodeType type = UINodeType::Container;

    // Tree structure
    std::vector<std::unique_ptr<UINode>> children;
    UINode* parent = nullptr;

    // Visibility
    Visibility visibility = Visibility::Visible;
    int zIndex = 0;

    // Layout properties
    LayoutProps layout;

    // CSS classes
    std::vector<std::string> classes;

    // Inline style overrides (highest priority)
    std::optional<glm::vec4> backgroundColor;
    std::optional<glm::vec4> textColor;
    std::optional<float> opacity;
    std::optional<float> radius;
    std::optional<glm::vec4> strokeColor;
    std::optional<float> strokeWidth;
    std::optional<ShadowProps> shadow;
    std::optional<FontProps> font;

    // Transitions
    std::vector<TransitionDef> transitions;

    // Text content (for Text, Button, TextInput nodes)
    std::string text;

    // Image source (for Image nodes)
    std::string imageSource;

    // Shape metadata (for Shape nodes)
    UIShapeType shapeType = UIShapeType::Rectangle;
    glm::vec2 shapeLineEnd{100.0F, 0.0F}; // Local-space end point for line shapes

    // Render transform (applied after layout)
    glm::vec2 transformTranslate{0.0F, 0.0F};
    glm::vec2 transformScale{1.0F, 1.0F};
    float transformRotationDeg = 0.0F;

    // Built-in interaction metadata (serialized with screen JSON)
    // Enables no-code tab/menu behavior such as "button shows panel X and hides other tab pages".
    std::string onClickTargetId;
    std::string onClickTabGroupClass;
    std::string onClickButtonGroupClass;
    bool onClickToggleTarget = false;

    // Slider/toggle range
    float minValue = 0.0F;
    float maxValue = 100.0F;

    // Runtime state
    NodeState state;

    // Computed values (after style resolution + layout)
    ComputedRect computedRect;
    glm::vec4 computedBackgroundColor{0.0F, 0.0F, 0.0F, 0.0F};
    glm::vec4 computedTextColor{1.0F, 1.0F, 1.0F, 1.0F};
    float computedOpacity = 1.0F;
    float computedRadius = 0.0F;
    glm::vec4 computedStrokeColor{0.0F, 0.0F, 0.0F, 0.0F};
    float computedStrokeWidth = 0.0F;
    ShadowProps computedShadow;
    FontProps computedFont;
    float measuredWidth = 0.0F;
    float measuredHeight = 0.0F;

    // Layout dirty flag
    bool layoutDirty = true;
    bool styleDirty = true;

    // User data pointer (for binding callbacks)
    void* userData = nullptr;

    // --- Methods ---

    UINode() = default;
    explicit UINode(std::string nodeId, UINodeType nodeType = UINodeType::Container)
        : id(std::move(nodeId)), type(nodeType)
    {
    }

    // Non-copyable, movable
    UINode(const UINode&) = delete;
    UINode& operator=(const UINode&) = delete;
    UINode(UINode&&) = default;
    UINode& operator=(UINode&&) = default;

    ~UINode() = default;

    // Tree manipulation
    UINode* AddChild(std::unique_ptr<UINode> child)
    {
        if (!child)
            return nullptr;
        child->parent = this;
        children.push_back(std::move(child));
        MarkLayoutDirty();
        return children.back().get();
    }

    std::unique_ptr<UINode> RemoveChild(UINode* child)
    {
        for (auto it = children.begin(); it != children.end(); ++it)
        {
            if (it->get() == child)
            {
                auto removed = std::move(*it);
                removed->parent = nullptr;
                children.erase(it);
                MarkLayoutDirty();
                return removed;
            }
        }
        return nullptr;
    }

    UINode* FindChild(std::string_view childId) const
    {
        for (const auto& child : children)
        {
            if (!child)
            {
                continue;
            }
            if (child->id == childId)
                return child.get();
        }
        return nullptr;
    }

    UINode* FindDescendant(std::string_view descendantId) const
    {
        for (const auto& child : children)
        {
            if (!child)
            {
                continue;
            }
            if (child->id == descendantId)
                return child.get();
            if (auto found = child->FindDescendant(descendantId))
                return found;
        }
        return nullptr;
    }

    void ClearChildren()
    {
        children.clear();
        MarkLayoutDirty();
    }

    // CSS class management
    void AddClass(const std::string& className)
    {
        if (!HasClass(className))
        {
            classes.push_back(className);
            MarkStyleDirty();
        }
    }

    void RemoveClass(const std::string& className)
    {
        auto it = std::find(classes.begin(), classes.end(), className);
        if (it != classes.end())
        {
            classes.erase(it);
            MarkStyleDirty();
        }
    }

    bool HasClass(const std::string& className) const
    {
        return std::find(classes.begin(), classes.end(), className) != classes.end();
    }

    void SetClass(const std::string& className, bool value)
    {
        if (value)
            AddClass(className);
        else
            RemoveClass(className);
    }

    // Dirty flag propagation
    void MarkLayoutDirty()
    {
        layoutDirty = true;
        for (const auto& child : children)
        {
            if (child)
            {
                child->MarkLayoutDirty();
            }
        }
    }

    void MarkStyleDirty()
    {
        styleDirty = true;
        for (const auto& child : children)
        {
            if (child)
            {
                child->MarkStyleDirty();
            }
        }
    }

    // Helper factory methods
    static std::unique_ptr<UINode> CreatePanel(std::string id)
    {
        return std::make_unique<UINode>(std::move(id), UINodeType::Panel);
    }

    static std::unique_ptr<UINode> CreateText(std::string id, std::string textContent)
    {
        auto node = std::make_unique<UINode>(std::move(id), UINodeType::Text);
        node->text = std::move(textContent);
        return node;
    }

    static std::unique_ptr<UINode> CreateButton(std::string id, std::string label)
    {
        auto node = std::make_unique<UINode>(std::move(id), UINodeType::Button);
        node->text = std::move(label);
        return node;
    }

    static std::unique_ptr<UINode> CreateImage(std::string id, std::string source)
    {
        auto node = std::make_unique<UINode>(std::move(id), UINodeType::Image);
        node->imageSource = std::move(source);
        return node;
    }

    static std::unique_ptr<UINode> CreateSlider(std::string id, float minVal = 0.0F, float maxVal = 100.0F)
    {
        auto node = std::make_unique<UINode>(std::move(id), UINodeType::Slider);
        node->minValue = minVal;
        node->maxValue = maxVal;
        return node;
    }

    static std::unique_ptr<UINode> CreateShape(std::string id, UIShapeType shape = UIShapeType::Rectangle)
    {
        auto node = std::make_unique<UINode>(std::move(id), UINodeType::Shape);
        node->shapeType = shape;
        return node;
    }

    static std::unique_ptr<UINode> CreateToggle(std::string id)
    {
        return std::make_unique<UINode>(std::move(id), UINodeType::Toggle);
    }

    static std::unique_ptr<UINode> CreateScrollView(std::string id)
    {
        return std::make_unique<UINode>(std::move(id), UINodeType::ScrollView);
    }

    static std::unique_ptr<UINode> CreateTextInput(std::string id, std::string placeholder = "")
    {
        auto node = std::make_unique<UINode>(std::move(id), UINodeType::TextInput);
        node->text = std::move(placeholder);
        return node;
    }

    static std::unique_ptr<UINode> CreateProgressBar(std::string id)
    {
        return std::make_unique<UINode>(std::move(id), UINodeType::ProgressBar);
    }

    static std::unique_ptr<UINode> CreateSpacer(std::string id, float size)
    {
        auto node = std::make_unique<UINode>(std::move(id), UINodeType::Spacer);
        node->layout.width = SizeValue::Px(size);
        node->layout.height = SizeValue::Px(size);
        return node;
    }

    static std::unique_ptr<UINode> CreateContainer(std::string id)
    {
        return std::make_unique<UINode>(std::move(id), UINodeType::Container);
    }
};

} // namespace engine::ui
