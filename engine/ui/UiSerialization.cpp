#include "engine/ui/UiSerialization.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <cstdio>

#include <nlohmann/json.hpp>

#include "engine/ui/UiStyleSheet.hpp"

namespace engine::ui
{
namespace
{
using json = nlohmann::json;

UINodeType ParseNodeType(const std::string& typeStr)
{
    if (typeStr == "Panel")
        return UINodeType::Panel;
    if (typeStr == "Text")
        return UINodeType::Text;
    if (typeStr == "Button")
        return UINodeType::Button;
    if (typeStr == "Image")
        return UINodeType::Image;
    if (typeStr == "Shape")
        return UINodeType::Shape;
    if (typeStr == "Slider")
        return UINodeType::Slider;
    if (typeStr == "Toggle")
        return UINodeType::Toggle;
    if (typeStr == "ScrollView")
        return UINodeType::ScrollView;
    if (typeStr == "TextInput")
        return UINodeType::TextInput;
    if (typeStr == "ProgressBar")
        return UINodeType::ProgressBar;
    if (typeStr == "Spacer")
        return UINodeType::Spacer;
    return UINodeType::Container;
}

UIShapeType ParseShapeType(const std::string& shapeStr)
{
    if (shapeStr == "Circle")
    {
        return UIShapeType::Circle;
    }
    if (shapeStr == "Line")
    {
        return UIShapeType::Line;
    }
    return UIShapeType::Rectangle;
}

std::string ShapeTypeToString(UIShapeType shapeType)
{
    switch (shapeType)
    {
        case UIShapeType::Circle:
            return "Circle";
        case UIShapeType::Line:
            return "Line";
        case UIShapeType::Rectangle:
        default:
            return "Rectangle";
    }
}

Display ParseDisplay(const std::string& str)
{
    if (str == "flex")
        return Display::Flex;
    if (str == "grid")
        return Display::Grid;
    if (str == "block")
        return Display::Block;
    if (str == "none")
        return Display::None;
    return Display::Flex;
}

Position ParsePosition(const std::string& str)
{
    if (str == "absolute")
    {
        return Position::Absolute;
    }
    return Position::Relative;
}

std::string PositionToString(Position position)
{
    switch (position)
    {
        case Position::Absolute:
            return "absolute";
        case Position::Relative:
        default:
            return "relative";
    }
}

std::string DisplayToString(Display display)
{
    switch (display)
    {
        case Display::Flex:
            return "flex";
        case Display::Grid:
            return "grid";
        case Display::Block:
            return "block";
        case Display::None:
            return "none";
        default:
            return "flex";
    }
}

FlexDirection ParseFlexDirection(const std::string& str)
{
    if (str == "row")
        return FlexDirection::Row;
    if (str == "column")
        return FlexDirection::Column;
    if (str == "row-reverse")
        return FlexDirection::RowReverse;
    if (str == "column-reverse")
        return FlexDirection::ColumnReverse;
    return FlexDirection::Column;
}

std::string FlexDirectionToString(FlexDirection dir)
{
    switch (dir)
    {
        case FlexDirection::Row:
            return "row";
        case FlexDirection::Column:
            return "column";
        case FlexDirection::RowReverse:
            return "row-reverse";
        case FlexDirection::ColumnReverse:
            return "column-reverse";
        default:
            return "column";
    }
}

JustifyContent ParseJustifyContent(const std::string& str)
{
    if (str == "flex-start")
        return JustifyContent::FlexStart;
    if (str == "flex-end")
        return JustifyContent::FlexEnd;
    if (str == "center")
        return JustifyContent::Center;
    if (str == "space-between")
        return JustifyContent::SpaceBetween;
    if (str == "space-around")
        return JustifyContent::SpaceAround;
    if (str == "space-evenly")
        return JustifyContent::SpaceEvenly;
    return JustifyContent::FlexStart;
}

std::string JustifyContentToString(JustifyContent justify)
{
    switch (justify)
    {
        case JustifyContent::FlexStart:
            return "flex-start";
        case JustifyContent::FlexEnd:
            return "flex-end";
        case JustifyContent::Center:
            return "center";
        case JustifyContent::SpaceBetween:
            return "space-between";
        case JustifyContent::SpaceAround:
            return "space-around";
        case JustifyContent::SpaceEvenly:
            return "space-evenly";
        default:
            return "flex-start";
    }
}

AlignItems ParseAlignItems(const std::string& str)
{
    if (str == "flex-start")
        return AlignItems::FlexStart;
    if (str == "flex-end")
        return AlignItems::FlexEnd;
    if (str == "center")
        return AlignItems::Center;
    if (str == "stretch")
        return AlignItems::Stretch;
    if (str == "baseline")
        return AlignItems::Baseline;
    return AlignItems::Stretch;
}

std::string AlignItemsToString(AlignItems align)
{
    switch (align)
    {
        case AlignItems::FlexStart:
            return "flex-start";
        case AlignItems::FlexEnd:
            return "flex-end";
        case AlignItems::Center:
            return "center";
        case AlignItems::Stretch:
            return "stretch";
        case AlignItems::Baseline:
            return "baseline";
        default:
            return "stretch";
    }
}

Overflow ParseOverflow(const std::string& str)
{
    if (str == "hidden")
    {
        return Overflow::Hidden;
    }
    if (str == "scroll")
    {
        return Overflow::Scroll;
    }
    return Overflow::Visible;
}

std::string OverflowToString(Overflow overflow)
{
    switch (overflow)
    {
        case Overflow::Hidden:
            return "hidden";
        case Overflow::Scroll:
            return "scroll";
        case Overflow::Visible:
        default:
            return "visible";
    }
}

GridItemAlign ParseGridItemAlign(const std::string& str)
{
    if (str == "start")
        return GridItemAlign::Start;
    if (str == "end")
        return GridItemAlign::End;
    if (str == "center")
        return GridItemAlign::Center;
    return GridItemAlign::Stretch;
}

std::string GridItemAlignToString(GridItemAlign align)
{
    switch (align)
    {
        case GridItemAlign::Start:
            return "start";
        case GridItemAlign::End:
            return "end";
        case GridItemAlign::Center:
            return "center";
        case GridItemAlign::Stretch:
        default:
            return "stretch";
    }
}

std::string ToLowerCopy(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

FontProps::Weight ParseFontWeight(const std::string& value)
{
    const std::string weight = ToLowerCopy(value);
    if (weight == "100" || weight == "extra-light" || weight == "extralight" || weight == "ultra-light" || weight == "ultralight")
    {
        return FontProps::Weight::ExtraLight;
    }
    if (weight == "200" || weight == "light")
    {
        return FontProps::Weight::Light;
    }
    if (weight == "500" || weight == "medium")
    {
        return FontProps::Weight::Medium;
    }
    if (weight == "600" || weight == "semi-bold" || weight == "semibold")
    {
        return FontProps::Weight::SemiBold;
    }
    if (weight == "700" || weight == "bold")
    {
        return FontProps::Weight::Bold;
    }
    if (weight == "800" || weight == "extra-bold" || weight == "extrabold" || weight == "ultra-bold" || weight == "ultrabold")
    {
        return FontProps::Weight::ExtraBold;
    }
    return FontProps::Weight::Normal;
}

std::string FontWeightToString(FontProps::Weight weight)
{
    switch (weight)
    {
        case FontProps::Weight::ExtraLight:
            return "extra-light";
        case FontProps::Weight::Light:
            return "light";
        case FontProps::Weight::Medium:
            return "medium";
        case FontProps::Weight::SemiBold:
            return "semi-bold";
        case FontProps::Weight::Bold:
            return "bold";
        case FontProps::Weight::ExtraBold:
            return "extra-bold";
        case FontProps::Weight::Normal:
        default:
            return "normal";
    }
}

FontProps::Style ParseFontStyle(const std::string& value)
{
    const std::string style = ToLowerCopy(value);
    return style == "italic" ? FontProps::Style::Italic : FontProps::Style::Normal;
}

std::string FontStyleToString(FontProps::Style style)
{
    return style == FontProps::Style::Italic ? "italic" : "normal";
}

FontProps::Align ParseTextAlign(const std::string& value)
{
    const std::string align = ToLowerCopy(value);
    if (align == "left")
    {
        return FontProps::Align::Left;
    }
    if (align == "right")
    {
        return FontProps::Align::Right;
    }
    return FontProps::Align::Center;
}

std::string TextAlignToString(FontProps::Align align)
{
    switch (align)
    {
        case FontProps::Align::Left:
            return "left";
        case FontProps::Align::Right:
            return "right";
        case FontProps::Align::Center:
        default:
            return "center";
    }
}

void ParseTextDecoration(const std::string& value, FontProps& font)
{
    const std::string lowered = ToLowerCopy(value);
    if (lowered == "none")
    {
        font.underline = false;
        font.strikethrough = false;
        return;
    }
    font.underline = lowered.find("underline") != std::string::npos;
    font.strikethrough = lowered.find("line-through") != std::string::npos
                      || lowered.find("strikethrough") != std::string::npos;
}

std::string TextDecorationToString(const FontProps& font)
{
    if (font.underline && font.strikethrough)
    {
        return "underline line-through";
    }
    if (font.underline)
    {
        return "underline";
    }
    if (font.strikethrough)
    {
        return "line-through";
    }
    return "none";
}

void ParseVec2(const json& value, glm::vec2& out)
{
    if (value.is_array() && value.size() >= 2 && value[0].is_number() && value[1].is_number())
    {
        out.x = value[0].get<float>();
        out.y = value[1].get<float>();
    }
}

void ParseSizeValue(const json& value, SizeValue& out)
{
    if (value.is_string())
    {
        out = ParseSize(value.get<std::string>());
    }
    else if (value.is_number())
    {
        out = SizeValue::Px(value.get<float>());
    }
}

void SerializeSizeValue(json& out, const char* key, const SizeValue& value)
{
    if (value.IsAuto())
    {
        return;
    }

    switch (value.unit)
    {
        case SizeValue::Unit::Px:
            out[key] = value.value;
            break;
        case SizeValue::Unit::Percent:
            out[key] = std::to_string(static_cast<int>(value.value)) + "%";
            break;
        case SizeValue::Unit::Vw:
            out[key] = std::to_string(static_cast<int>(value.value)) + "vw";
            break;
        case SizeValue::Unit::Vh:
            out[key] = std::to_string(static_cast<int>(value.value)) + "vh";
            break;
        case SizeValue::Unit::Auto:
        default:
            break;
    }
}

void ParseLayoutProps(const json& j, LayoutProps& layout)
{
    if (!j.is_object())
        return;

    if (j.contains("display"))
        layout.display = ParseDisplay(j["display"].get<std::string>());

    if (j.contains("position"))
        layout.position = ParsePosition(j["position"].get<std::string>());

    if (j.contains("flexDirection"))
        layout.flexDirection = ParseFlexDirection(j["flexDirection"].get<std::string>());

    if (j.contains("justifyContent"))
        layout.justifyContent = ParseJustifyContent(j["justifyContent"].get<std::string>());

    if (j.contains("alignItems"))
        layout.alignItems = ParseAlignItems(j["alignItems"].get<std::string>());

    if (j.contains("gap"))
        layout.gap = j["gap"].get<float>();
    if (j.contains("gridColumns") && j["gridColumns"].is_number_integer())
        layout.gridColumns = std::max(1, j["gridColumns"].get<int>());
    if (j.contains("gridRows") && j["gridRows"].is_number_integer())
        layout.gridRows = std::max(0, j["gridRows"].get<int>());
    if (j.contains("gridColumnSize"))
        ParseSizeValue(j["gridColumnSize"], layout.gridColumnSize);
    if (j.contains("gridRowSize"))
        ParseSizeValue(j["gridRowSize"], layout.gridRowSize);
    if (j.contains("gridColumnGap") && j["gridColumnGap"].is_number())
        layout.gridColumnGap = j["gridColumnGap"].get<float>();
    if (j.contains("gridRowGap") && j["gridRowGap"].is_number())
        layout.gridRowGap = j["gridRowGap"].get<float>();
    if (j.contains("gridJustifyItems") && j["gridJustifyItems"].is_string())
        layout.gridJustifyItems = ParseGridItemAlign(j["gridJustifyItems"].get<std::string>());
    if (j.contains("gridAlignItems") && j["gridAlignItems"].is_string())
        layout.gridAlignItems = ParseGridItemAlign(j["gridAlignItems"].get<std::string>());
    if (j.contains("gridTemplateAreas"))
    {
        if (j["gridTemplateAreas"].is_string())
        {
            layout.gridTemplateAreas = j["gridTemplateAreas"].get<std::string>();
        }
        else if (j["gridTemplateAreas"].is_array())
        {
            std::string merged;
            for (const auto& line : j["gridTemplateAreas"])
            {
                if (!line.is_string())
                {
                    continue;
                }
                if (!merged.empty())
                {
                    merged.push_back('\n');
                }
                merged += line.get<std::string>();
            }
            layout.gridTemplateAreas = merged;
        }
    }
    if (j.contains("gridArea") && j["gridArea"].is_string())
        layout.gridArea = j["gridArea"].get<std::string>();
    if (j.contains("gridColumnStart") && j["gridColumnStart"].is_number_integer())
        layout.gridColumnStart = std::max(0, j["gridColumnStart"].get<int>());
    if (j.contains("gridRowStart") && j["gridRowStart"].is_number_integer())
        layout.gridRowStart = std::max(0, j["gridRowStart"].get<int>());
    if (j.contains("gridColumnSpan") && j["gridColumnSpan"].is_number_integer())
        layout.gridColumnSpan = std::max(1, j["gridColumnSpan"].get<int>());
    if (j.contains("gridRowSpan") && j["gridRowSpan"].is_number_integer())
        layout.gridRowSpan = std::max(1, j["gridRowSpan"].get<int>());

    if (j.contains("padding"))
    {
        if (j["padding"].is_array())
        {
            auto values = j["padding"].get<std::vector<float>>();
            layout.padding = ParseEdgeInsets(values);
        }
        else if (j["padding"].is_number())
        {
            layout.padding = EdgeInsets::All(j["padding"].get<float>());
        }
    }

    if (j.contains("margin"))
    {
        if (j["margin"].is_array())
        {
            auto values = j["margin"].get<std::vector<float>>();
            layout.margin = ParseEdgeInsets(values);
        }
        else if (j["margin"].is_number())
        {
            layout.margin = EdgeInsets::All(j["margin"].get<float>());
        }
    }

    if (j.contains("width"))
    {
        ParseSizeValue(j["width"], layout.width);
    }

    if (j.contains("height"))
    {
        ParseSizeValue(j["height"], layout.height);
    }

    if (j.contains("minWidth"))
    {
        ParseSizeValue(j["minWidth"], layout.minWidth);
    }

    if (j.contains("maxWidth"))
    {
        ParseSizeValue(j["maxWidth"], layout.maxWidth);
    }

    if (j.contains("minHeight"))
    {
        ParseSizeValue(j["minHeight"], layout.minHeight);
    }

    if (j.contains("maxHeight"))
    {
        ParseSizeValue(j["maxHeight"], layout.maxHeight);
    }

    if (j.contains("flexGrow"))
        layout.flexGrow = j["flexGrow"].get<float>();

    if (j.contains("flexShrink"))
        layout.flexShrink = j["flexShrink"].get<float>();

    if (j.contains("flexBasis"))
    {
        ParseSizeValue(j["flexBasis"], layout.flexBasis);
    }

    if (j.contains("anchor") && j["anchor"].is_array() && j["anchor"].size() >= 2)
    {
        glm::vec2 anchor;
        ParseVec2(j["anchor"], anchor);
        layout.anchor = anchor;
    }

    if (j.contains("offset"))
    {
        ParseVec2(j["offset"], layout.offset);
    }

    if (j.contains("pivot"))
    {
        ParseVec2(j["pivot"], layout.pivot);
    }

    if (j.contains("overflow") && j["overflow"].is_string())
    {
        layout.overflow = ParseOverflow(j["overflow"].get<std::string>());
    }

    if (j.contains("aspectRatio") && j["aspectRatio"].is_number())
    {
        layout.aspectRatio = j["aspectRatio"].get<float>();
    }
}

void SerializeLayoutProps(json& j, const LayoutProps& layout)
{
    j["display"] = DisplayToString(layout.display);
    j["position"] = PositionToString(layout.position);
    j["flexDirection"] = FlexDirectionToString(layout.flexDirection);
    j["justifyContent"] = JustifyContentToString(layout.justifyContent);
    j["alignItems"] = AlignItemsToString(layout.alignItems);
    j["overflow"] = OverflowToString(layout.overflow);

    if (layout.gap != 0.0F)
        j["gap"] = layout.gap;
    if (layout.gridColumns != 1)
        j["gridColumns"] = layout.gridColumns;
    if (layout.gridRows != 0)
        j["gridRows"] = layout.gridRows;
    SerializeSizeValue(j, "gridColumnSize", layout.gridColumnSize);
    SerializeSizeValue(j, "gridRowSize", layout.gridRowSize);
    if (layout.gridColumnGap >= 0.0F)
        j["gridColumnGap"] = layout.gridColumnGap;
    if (layout.gridRowGap >= 0.0F)
        j["gridRowGap"] = layout.gridRowGap;
    if (layout.gridJustifyItems != GridItemAlign::Stretch)
        j["gridJustifyItems"] = GridItemAlignToString(layout.gridJustifyItems);
    if (layout.gridAlignItems != GridItemAlign::Stretch)
        j["gridAlignItems"] = GridItemAlignToString(layout.gridAlignItems);
    if (!layout.gridTemplateAreas.empty())
        j["gridTemplateAreas"] = layout.gridTemplateAreas;
    if (!layout.gridArea.empty())
        j["gridArea"] = layout.gridArea;
    if (layout.gridColumnStart > 0)
        j["gridColumnStart"] = layout.gridColumnStart;
    if (layout.gridRowStart > 0)
        j["gridRowStart"] = layout.gridRowStart;
    if (layout.gridColumnSpan != 1)
        j["gridColumnSpan"] = layout.gridColumnSpan;
    if (layout.gridRowSpan != 1)
        j["gridRowSpan"] = layout.gridRowSpan;

    if (layout.padding.top != 0.0F || layout.padding.right != 0.0F || layout.padding.bottom != 0.0F || layout.padding.left != 0.0F)
    {
        if (layout.padding.top == layout.padding.right && layout.padding.right == layout.padding.bottom && layout.padding.bottom == layout.padding.left)
        {
            j["padding"] = layout.padding.top;
        }
        else
        {
            j["padding"] = {layout.padding.top, layout.padding.right, layout.padding.bottom, layout.padding.left};
        }
    }

    if (layout.margin.top != 0.0F || layout.margin.right != 0.0F || layout.margin.bottom != 0.0F || layout.margin.left != 0.0F)
    {
        if (layout.margin.top == layout.margin.right && layout.margin.right == layout.margin.bottom && layout.margin.bottom == layout.margin.left)
        {
            j["margin"] = layout.margin.top;
        }
        else
        {
            j["margin"] = {layout.margin.top, layout.margin.right, layout.margin.bottom, layout.margin.left};
        }
    }

    SerializeSizeValue(j, "width", layout.width);
    SerializeSizeValue(j, "height", layout.height);
    SerializeSizeValue(j, "minWidth", layout.minWidth);
    SerializeSizeValue(j, "maxWidth", layout.maxWidth);
    SerializeSizeValue(j, "minHeight", layout.minHeight);
    SerializeSizeValue(j, "maxHeight", layout.maxHeight);
    SerializeSizeValue(j, "flexBasis", layout.flexBasis);

    if (layout.flexGrow != 0.0F)
        j["flexGrow"] = layout.flexGrow;
    if (layout.flexShrink != 1.0F)
        j["flexShrink"] = layout.flexShrink;

    if (layout.anchor.has_value())
    {
        j["anchor"] = {layout.anchor->x, layout.anchor->y};
    }
    if (layout.offset.x != 0.0F || layout.offset.y != 0.0F)
    {
        j["offset"] = {layout.offset.x, layout.offset.y};
    }
    if (layout.pivot.x != 0.5F || layout.pivot.y != 0.5F)
    {
        j["pivot"] = {layout.pivot.x, layout.pivot.y};
    }
    if (layout.aspectRatio > 0.0F)
    {
        j["aspectRatio"] = layout.aspectRatio;
    }
}

std::unique_ptr<UINode> ParseNode(const json& j)
{
    if (!j.is_object())
        return nullptr;

    std::string id = j.value("id", "");
    std::string typeStr = j.value("type", "Container");
    UINodeType type = ParseNodeType(typeStr);

    auto node = std::make_unique<UINode>(id, type);

    node->name = j.value("name", id);

    // Parse visibility
    if (j.contains("visibility"))
    {
        std::string visStr = j["visibility"].get<std::string>();
        if (visStr == "hidden")
            node->visibility = Visibility::Hidden;
        else if (visStr == "collapsed")
            node->visibility = Visibility::Collapsed;
    }

    // Parse z-index
    if (j.contains("zIndex"))
        node->zIndex = j["zIndex"].get<int>();

    // Parse layout
    if (j.contains("layout"))
        ParseLayoutProps(j["layout"], node->layout);

    // Parse CSS classes
    if (j.contains("classes") && j["classes"].is_array())
    {
        for (const auto& cls : j["classes"])
        {
            if (cls.is_string())
                node->classes.push_back(cls.get<std::string>());
        }
    }

    // Parse inline styles
    if (j.contains("style") && j["style"].is_object())
    {
        const auto& style = j["style"];
        if (style.contains("backgroundColor"))
        {
            auto color = ParseColor(style["backgroundColor"].get<std::string>());
            if (color)
                node->backgroundColor = *color;
        }
        if (style.contains("textColor"))
        {
            auto color = ParseColor(style["textColor"].get<std::string>());
            if (color)
                node->textColor = *color;
        }
        if (style.contains("opacity"))
            node->opacity = style["opacity"].get<float>();
        if (style.contains("radius"))
            node->radius = style["radius"].get<float>();
        if (style.contains("strokeColor"))
        {
            if (style["strokeColor"].is_string())
            {
                auto color = ParseColor(style["strokeColor"].get<std::string>());
                if (color)
                    node->strokeColor = *color;
            }
        }
        if (style.contains("borderColor") && !node->strokeColor.has_value())
        {
            if (style["borderColor"].is_string())
            {
                auto color = ParseColor(style["borderColor"].get<std::string>());
                if (color)
                    node->strokeColor = *color;
            }
        }
        if (style.contains("strokeWidth"))
        {
            if (style["strokeWidth"].is_number())
            {
                node->strokeWidth = style["strokeWidth"].get<float>();
            }
        }
        if (style.contains("borderWidth") && !node->strokeWidth.has_value())
        {
            if (style["borderWidth"].is_number())
            {
                node->strokeWidth = style["borderWidth"].get<float>();
            }
        }
        if (style.contains("fontFamily") && style["fontFamily"].is_string())
        {
            if (!node->font.has_value())
            {
                node->font = FontProps{};
            }
            node->font->family = style["fontFamily"].get<std::string>();
        }
        if (style.contains("fontSize") && style["fontSize"].is_number())
        {
            if (!node->font.has_value())
            {
                node->font = FontProps{};
            }
            node->font->size = style["fontSize"].get<float>();
        }
        if (style.contains("fontWeight"))
        {
            if (!node->font.has_value())
            {
                node->font = FontProps{};
            }
            if (style["fontWeight"].is_string())
            {
                node->font->weight = ParseFontWeight(style["fontWeight"].get<std::string>());
            }
            else if (style["fontWeight"].is_number_integer())
            {
                node->font->weight = ParseFontWeight(std::to_string(style["fontWeight"].get<int>()));
            }
        }
        if (style.contains("fontStyle") && style["fontStyle"].is_string())
        {
            if (!node->font.has_value())
            {
                node->font = FontProps{};
            }
            node->font->style = ParseFontStyle(style["fontStyle"].get<std::string>());
        }
        if (style.contains("textAlign") && style["textAlign"].is_string())
        {
            if (!node->font.has_value())
            {
                node->font = FontProps{};
            }
            node->font->align = ParseTextAlign(style["textAlign"].get<std::string>());
        }
        if (style.contains("textDecoration") && style["textDecoration"].is_string())
        {
            if (!node->font.has_value())
            {
                node->font = FontProps{};
            }
            ParseTextDecoration(style["textDecoration"].get<std::string>(), *node->font);
        }
        if (style.contains("textUnderline") && style["textUnderline"].is_boolean())
        {
            if (!node->font.has_value())
            {
                node->font = FontProps{};
            }
            node->font->underline = style["textUnderline"].get<bool>();
        }
        if (style.contains("textStrikethrough") && style["textStrikethrough"].is_boolean())
        {
            if (!node->font.has_value())
            {
                node->font = FontProps{};
            }
            node->font->strikethrough = style["textStrikethrough"].get<bool>();
        }
        if (style.contains("letterSpacing") && style["letterSpacing"].is_number())
        {
            if (!node->font.has_value())
            {
                node->font = FontProps{};
            }
            node->font->letterSpacing = style["letterSpacing"].get<float>();
        }
    }

    // Parse text content
    if (j.contains("text"))
        node->text = j["text"].get<std::string>();

    // Parse image source
    if (j.contains("imageSource"))
        node->imageSource = j["imageSource"].get<std::string>();

    // Parse shape metadata
    if (j.contains("shapeType") && j["shapeType"].is_string())
    {
        node->shapeType = ParseShapeType(j["shapeType"].get<std::string>());
    }
    if (j.contains("shapeLineEnd") && j["shapeLineEnd"].is_array() && j["shapeLineEnd"].size() >= 2)
    {
        ParseVec2(j["shapeLineEnd"], node->shapeLineEnd);
    }

    // Parse render transform
    if (j.contains("transform") && j["transform"].is_object())
    {
        const auto& transform = j["transform"];
        if (transform.contains("translate"))
        {
            ParseVec2(transform["translate"], node->transformTranslate);
        }
        if (transform.contains("scale"))
        {
            ParseVec2(transform["scale"], node->transformScale);
        }
        if (transform.contains("rotation") && transform["rotation"].is_number())
        {
            node->transformRotationDeg = transform["rotation"].get<float>();
        }
    }

    // Parse built-in interactions
    if (j.contains("interaction") && j["interaction"].is_object())
    {
        const auto& interaction = j["interaction"];
        if (interaction.contains("onClickTarget") && interaction["onClickTarget"].is_string())
        {
            node->onClickTargetId = interaction["onClickTarget"].get<std::string>();
        }
        if (interaction.contains("onClickTabGroupClass") && interaction["onClickTabGroupClass"].is_string())
        {
            node->onClickTabGroupClass = interaction["onClickTabGroupClass"].get<std::string>();
        }
        if (interaction.contains("onClickButtonGroupClass") && interaction["onClickButtonGroupClass"].is_string())
        {
            node->onClickButtonGroupClass = interaction["onClickButtonGroupClass"].get<std::string>();
        }
        if (interaction.contains("onClickToggleTarget") && interaction["onClickToggleTarget"].is_boolean())
        {
            node->onClickToggleTarget = interaction["onClickToggleTarget"].get<bool>();
        }
    }

    // Parse slider range
    if (j.contains("minValue"))
        node->minValue = j["minValue"].get<float>();
    if (j.contains("maxValue"))
        node->maxValue = j["maxValue"].get<float>();

    // Parse children
    if (j.contains("children") && j["children"].is_array())
    {
        for (const auto& childJson : j["children"])
        {
            auto child = ParseNode(childJson);
            if (child)
                node->AddChild(std::move(child));
        }
    }

    return node;
}

void SerializeNode(json& j, const UINode& node)
{
    j["id"] = node.id;
    j["type"] = NodeTypeToString(node.type);

    if (!node.name.empty() && node.name != node.id)
        j["name"] = node.name;

    if (node.visibility != Visibility::Visible)
    {
        switch (node.visibility)
        {
            case Visibility::Hidden:
                j["visibility"] = "hidden";
                break;
            case Visibility::Collapsed:
                j["visibility"] = "collapsed";
                break;
            default:
                break;
        }
    }

    if (node.zIndex != 0)
        j["zIndex"] = node.zIndex;

    // Layout
    json layoutJson;
    SerializeLayoutProps(layoutJson, node.layout);
    if (!layoutJson.empty())
        j["layout"] = layoutJson;

    // Classes
    if (!node.classes.empty())
    {
        j["classes"] = node.classes;
    }

    // Inline styles
    json styleJson;
    if (node.backgroundColor.has_value())
    {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "#%02x%02x%02x%02x",
            static_cast<int>(node.backgroundColor->r * 255),
            static_cast<int>(node.backgroundColor->g * 255),
            static_cast<int>(node.backgroundColor->b * 255),
            static_cast<int>(node.backgroundColor->a * 255));
        styleJson["backgroundColor"] = buf;
    }
    if (node.textColor.has_value())
    {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "#%02x%02x%02x%02x",
            static_cast<int>(node.textColor->r * 255),
            static_cast<int>(node.textColor->g * 255),
            static_cast<int>(node.textColor->b * 255),
            static_cast<int>(node.textColor->a * 255));
        styleJson["textColor"] = buf;
    }
    if (node.opacity.has_value())
        styleJson["opacity"] = *node.opacity;
    if (node.radius.has_value())
        styleJson["radius"] = *node.radius;
    if (node.strokeColor.has_value())
    {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "#%02x%02x%02x%02x",
            static_cast<int>(node.strokeColor->r * 255),
            static_cast<int>(node.strokeColor->g * 255),
            static_cast<int>(node.strokeColor->b * 255),
            static_cast<int>(node.strokeColor->a * 255));
        styleJson["strokeColor"] = buf;
    }
    if (node.strokeWidth.has_value())
    {
        styleJson["strokeWidth"] = *node.strokeWidth;
    }
    if (node.font.has_value())
    {
        if (!node.font->family.empty())
        {
            styleJson["fontFamily"] = node.font->family;
        }
        styleJson["fontSize"] = node.font->size;
        styleJson["fontWeight"] = FontWeightToString(node.font->weight);
        styleJson["fontStyle"] = FontStyleToString(node.font->style);
        styleJson["textAlign"] = TextAlignToString(node.font->align);
        styleJson["textDecoration"] = TextDecorationToString(*node.font);
        if (std::abs(node.font->letterSpacing) > 0.001F)
        {
            styleJson["letterSpacing"] = node.font->letterSpacing;
        }
    }

    if (!styleJson.empty())
        j["style"] = styleJson;

    // Text content
    if (!node.text.empty())
        j["text"] = node.text;

    // Image source
    if (!node.imageSource.empty())
        j["imageSource"] = node.imageSource;

    // Shape metadata
    if (node.type == UINodeType::Shape)
    {
        j["shapeType"] = ShapeTypeToString(node.shapeType);
        if (node.shapeType == UIShapeType::Line)
        {
            j["shapeLineEnd"] = {node.shapeLineEnd.x, node.shapeLineEnd.y};
        }
    }

    // Render transform
    if (node.transformTranslate.x != 0.0F
        || node.transformTranslate.y != 0.0F
        || node.transformScale.x != 1.0F
        || node.transformScale.y != 1.0F
        || node.transformRotationDeg != 0.0F)
    {
        json transformJson;
        if (node.transformTranslate.x != 0.0F || node.transformTranslate.y != 0.0F)
        {
            transformJson["translate"] = {node.transformTranslate.x, node.transformTranslate.y};
        }
        if (node.transformScale.x != 1.0F || node.transformScale.y != 1.0F)
        {
            transformJson["scale"] = {node.transformScale.x, node.transformScale.y};
        }
        if (node.transformRotationDeg != 0.0F)
        {
            transformJson["rotation"] = node.transformRotationDeg;
        }
        if (!transformJson.empty())
        {
            j["transform"] = transformJson;
        }
    }

    // Slider range
    if (node.type == UINodeType::Slider)
    {
        j["minValue"] = node.minValue;
        j["maxValue"] = node.maxValue;
    }

    // Built-in interactions
    json interactionJson;
    if (!node.onClickTargetId.empty())
    {
        interactionJson["onClickTarget"] = node.onClickTargetId;
    }
    if (!node.onClickTabGroupClass.empty())
    {
        interactionJson["onClickTabGroupClass"] = node.onClickTabGroupClass;
    }
    if (!node.onClickButtonGroupClass.empty())
    {
        interactionJson["onClickButtonGroupClass"] = node.onClickButtonGroupClass;
    }
    if (node.onClickToggleTarget)
    {
        interactionJson["onClickToggleTarget"] = true;
    }
    if (!interactionJson.empty())
    {
        j["interaction"] = interactionJson;
    }

    // Children
    if (!node.children.empty())
    {
        json childrenJson = json::array();
        for (const auto& child : node.children)
        {
            if (!child)
            {
                continue;
            }
            json childJson;
            SerializeNode(childJson, *child);
            childrenJson.push_back(childJson);
        }
        j["children"] = childrenJson;
    }
}

} // namespace

std::unique_ptr<UINode> LoadScreen(const std::string& filePath)
{
    std::ifstream file(filePath);
    if (!file.is_open())
    {
        return nullptr;
    }

    std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    return ParseScreen(content);
}

std::unique_ptr<UINode> ParseScreen(const std::string& jsonContent)
{
    try
    {
        json root = json::parse(jsonContent);

        if (!root.contains("asset_version"))
        {
            return nullptr;
        }

        if (!root.contains("root"))
        {
            return nullptr;
        }

        return ParseNode(root["root"]);
    }
    catch (const std::exception&)
    {
        return nullptr;
    }
}

bool SaveScreen(const std::string& filePath, const UINode& rootNode)
{
    std::string content = SerializeScreen(rootNode);
    if (content.empty())
    {
        return false;
    }

    std::ofstream file(filePath);
    if (!file.is_open())
    {
        return false;
    }

    file << content;
    return true;
}

std::string SerializeScreen(const UINode& rootNode)
{
    try
    {
        json root;
        root["asset_version"] = 1;

        json rootJson;
        SerializeNode(rootJson, rootNode);
        root["root"] = rootJson;

        return root.dump(2);
    }
    catch (const std::exception&)
    {
        return "";
    }
}

bool LoadStyleSheet(const std::string& filePath, StyleSheet& outStyleSheet)
{
    std::ifstream file(filePath);
    if (!file.is_open())
    {
        return false;
    }

    std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    return ParseStyleSheet(content, outStyleSheet);
}

bool LoadTokens(const std::string& filePath, TokenCollection& outTokens)
{
    std::ifstream file(filePath);
    if (!file.is_open())
    {
        return false;
    }

    std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    return ParseTokens(content, outTokens);
}

bool HasFileChanged(const std::string& filePath, long long& lastModTime)
{
    try
    {
        auto ftime = std::filesystem::last_write_time(filePath);
        auto sctp = std::chrono::time_point_cast<std::chrono::seconds>(ftime);
        auto timestamp = sctp.time_since_epoch().count();

        if (timestamp > lastModTime)
        {
            lastModTime = timestamp;
            return true;
        }
        return false;
    }
    catch (...)
    {
        return false;
    }
}

std::string NodeTypeToString(UINodeType type)
{
    switch (type)
    {
        case UINodeType::Panel:
            return "Panel";
        case UINodeType::Text:
            return "Text";
        case UINodeType::Button:
            return "Button";
        case UINodeType::Image:
            return "Image";
        case UINodeType::Shape:
            return "Shape";
        case UINodeType::Slider:
            return "Slider";
        case UINodeType::Toggle:
            return "Toggle";
        case UINodeType::ScrollView:
            return "ScrollView";
        case UINodeType::TextInput:
            return "TextInput";
        case UINodeType::ProgressBar:
            return "ProgressBar";
        case UINodeType::Spacer:
            return "Spacer";
        default:
            return "Container";
    }
}

} // namespace engine::ui
