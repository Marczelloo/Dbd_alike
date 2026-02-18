#include "engine/ui/UiStyle.hpp"

#include <algorithm>
#include <sstream>

namespace engine::ui
{
// --- TokenCollection ---

void TokenCollection::SetToken(const std::string& tokenName, TokenValue value, const std::string& description)
{
    Token token;
    token.name = tokenName;
    token.value = std::move(value);
    token.description = description;
    tokens[tokenName] = std::move(token);
}

bool TokenCollection::HasToken(const std::string& tokenName) const
{
    return tokens.find(tokenName) != tokens.end();
}

TokenValue TokenCollection::GetToken(const std::string& tokenName) const
{
    auto it = tokens.find(tokenName);
    if (it != tokens.end())
    {
        return it->second.value;
    }
    return std::string{};
}

glm::vec4 TokenCollection::GetColorToken(const std::string& tokenName, const glm::vec4& fallback) const
{
    auto it = tokens.find(tokenName);
    if (it != tokens.end())
    {
        if (std::holds_alternative<glm::vec4>(it->second.value))
        {
            return std::get<glm::vec4>(it->second.value);
        }
    }
    return fallback;
}

float TokenCollection::GetFloatToken(const std::string& tokenName, float fallback) const
{
    auto it = tokens.find(tokenName);
    if (it != tokens.end())
    {
        if (std::holds_alternative<float>(it->second.value))
        {
            return std::get<float>(it->second.value);
        }
    }
    return fallback;
}

std::string TokenCollection::GetStringToken(const std::string& tokenName, const std::string& fallback) const
{
    auto it = tokens.find(tokenName);
    if (it != tokens.end())
    {
        if (std::holds_alternative<std::string>(it->second.value))
        {
            return std::get<std::string>(it->second.value);
        }
    }
    return fallback;
}

// --- StyleSheet ---

void StyleSheet::AddRule(const StyleRule& rule)
{
    StyleRule ruleWithSpecificity = rule;
    ruleWithSpecificity.specificity = CalculateSpecificity(rule.selector);
    rules.push_back(ruleWithSpecificity);
}

void StyleSheet::Clear()
{
    rules.clear();
}

std::vector<const StyleRule*> StyleSheet::MatchRules(const UINode& node, const TokenCollection& tokens) const
{
    (void)tokens;
    std::vector<const StyleRule*> matched;

    for (const auto& rule : rules)
    {
        if (SelectorMatches(rule.selector, node))
        {
            matched.push_back(&rule);
        }
    }

    // Sort so low specificity applies first and high specificity overrides later.
    std::stable_sort(matched.begin(), matched.end(), [](const StyleRule* a, const StyleRule* b) {
        return a->specificity < b->specificity;
    });

    return matched;
}

bool StyleSheet::SelectorMatches(const Selector& selector, const UINode& node) const
{
    if (selector.IsEmpty())
    {
        return false;
    }

    // Single part selector
    if (selector.Length() == 1)
    {
        return PartMatches(selector.parts[0], node);
    }

    // Descendant selector - walk up the tree
    const UINode* current = &node;
    for (std::size_t i = selector.Length(); i > 0; --i)
    {
        const SelectorPart& part = selector.parts[i - 1];

        // Find an ancestor that matches this part
        while (current != nullptr)
        {
            if (PartMatches(part, *current))
            {
                current = current->parent;
                break;
            }
            current = current->parent;
        }

        if (current == nullptr && i > 1)
        {
            // Didn't find a matching ancestor for this part
            return false;
        }
    }

    return true;
}

bool StyleSheet::PartMatches(const SelectorPart& part, const UINode& node) const
{
    // Check pseudo-class first
    switch (part.pseudo)
    {
        case PseudoClass::Hover:
            if (!node.state.hover)
                return false;
            break;
        case PseudoClass::Pressed:
            if (!node.state.pressed)
                return false;
            break;
        case PseudoClass::Focus:
            if (!node.state.focused)
                return false;
            break;
        case PseudoClass::Disabled:
            if (!node.state.disabled)
                return false;
            break;
        case PseudoClass::Selected:
            if (!node.state.selected)
                return false;
            break;
        case PseudoClass::Checked:
            if (!node.state.checked)
                return false;
            break;
        default:
            break;
    }

    const auto nodeTypeName = [&]() -> const char* {
        switch (node.type)
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
            case UINodeType::Container:
                return "Container";
            default:
                return "";
        }
    }();

    // Compound selector syntax support in part.value, e.g. "Button.primary#play"
    if (part.type == SelectorType::Type
        && (part.value.find('.') != std::string::npos || part.value.find('#') != std::string::npos))
    {
        std::string typeName;
        std::string idName;
        std::vector<std::string> classes;

        std::size_t i = 0;
        while (i < part.value.size())
        {
            if (part.value[i] == '.')
            {
                const std::size_t begin = ++i;
                while (i < part.value.size() && part.value[i] != '.' && part.value[i] != '#')
                {
                    ++i;
                }
                if (i > begin)
                {
                    classes.push_back(part.value.substr(begin, i - begin));
                }
                continue;
            }
            if (part.value[i] == '#')
            {
                const std::size_t begin = ++i;
                while (i < part.value.size() && part.value[i] != '.' && part.value[i] != '#')
                {
                    ++i;
                }
                if (i > begin)
                {
                    idName = part.value.substr(begin, i - begin);
                }
                continue;
            }

            const std::size_t begin = i;
            while (i < part.value.size() && part.value[i] != '.' && part.value[i] != '#')
            {
                ++i;
            }
            if (i > begin)
            {
                typeName = part.value.substr(begin, i - begin);
            }
        }

        if (!typeName.empty() && typeName != nodeTypeName)
        {
            return false;
        }
        if (!idName.empty() && node.id != idName)
        {
            return false;
        }
        for (const std::string& className : classes)
        {
            if (!node.HasClass(className))
            {
                return false;
            }
        }
        return true;
    }

    // Check selector type
    switch (part.type)
    {
        case SelectorType::Universal:
            return true;

        case SelectorType::Type:
            return part.value == nodeTypeName;

        case SelectorType::Id:
            return node.id == part.value;

        case SelectorType::Class:
            return node.HasClass(part.value);

        default:
            return false;
    }
}

int StyleSheet::CalculateSpecificity(const Selector& selector) const
{
    // CSS-like specificity:
    // - ID: 100
    // - Class: 10
    // - Type: 1
    // - Universal: 0
    // - Pseudo-class: adds 10
    int specificity = 0;

    for (const auto& part : selector.parts)
    {
        int classCountFromCompound = 0;
        int idCountFromCompound = 0;
        int typeCountFromCompound = 0;

        if (part.type == SelectorType::Type
            && (part.value.find('.') != std::string::npos || part.value.find('#') != std::string::npos))
        {
            bool sawType = false;
            for (std::size_t i = 0; i < part.value.size(); ++i)
            {
                if (part.value[i] == '.')
                {
                    ++classCountFromCompound;
                }
                else if (part.value[i] == '#')
                {
                    ++idCountFromCompound;
                }
                else if (!sawType)
                {
                    sawType = true;
                    typeCountFromCompound = 1;
                }
            }
        }

        if (classCountFromCompound > 0 || idCountFromCompound > 0 || typeCountFromCompound > 0)
        {
            specificity += idCountFromCompound * 100;
            specificity += classCountFromCompound * 10;
            specificity += typeCountFromCompound;
        }
        else
        {
            switch (part.type)
            {
                case SelectorType::Id:
                    specificity += 100;
                    break;
                case SelectorType::Class:
                    specificity += 10;
                    break;
                case SelectorType::Type:
                    specificity += 1;
                    break;
                case SelectorType::Universal:
                    specificity += 0;
                    break;
                default:
                    break;
            }
        }

        // Pseudo-classes add to specificity
        if (part.pseudo != PseudoClass::None)
        {
            specificity += 10;
        }
    }

    return specificity;
}

// --- Style application ---

void ApplyStyleToNode(const StyleRule& rule, UINode& node, const TokenCollection& tokens)
{
    // Apply properties from rule to computed style
    // Only override if the rule has the property set

    if (rule.backgroundColor.has_value())
    {
        node.computedBackgroundColor = *rule.backgroundColor;
    }
    if (rule.backgroundColorExpr.has_value())
    {
        node.computedBackgroundColor = ResolveColor(*rule.backgroundColorExpr, tokens, node.computedBackgroundColor);
    }
    if (rule.textColor.has_value())
    {
        node.computedTextColor = *rule.textColor;
    }
    if (rule.textColorExpr.has_value())
    {
        node.computedTextColor = ResolveColor(*rule.textColorExpr, tokens, node.computedTextColor);
    }
    if (rule.opacity.has_value())
    {
        node.computedOpacity = *rule.opacity;
    }
    if (rule.opacityExpr.has_value())
    {
        node.computedOpacity = ResolveFloat(*rule.opacityExpr, tokens, node.computedOpacity);
    }
    if (rule.radius.has_value())
    {
        node.computedRadius = *rule.radius;
    }
    if (rule.radiusExpr.has_value())
    {
        node.computedRadius = ResolveFloat(*rule.radiusExpr, tokens, node.computedRadius);
    }
    if (rule.strokeColor.has_value())
    {
        node.computedStrokeColor = *rule.strokeColor;
    }
    if (rule.strokeColorExpr.has_value())
    {
        node.computedStrokeColor = ResolveColor(*rule.strokeColorExpr, tokens, node.computedStrokeColor);
    }
    if (rule.strokeWidth.has_value())
    {
        node.computedStrokeWidth = *rule.strokeWidth;
    }
    if (rule.strokeWidthExpr.has_value())
    {
        node.computedStrokeWidth = ResolveFloat(*rule.strokeWidthExpr, tokens, node.computedStrokeWidth);
    }
    if (rule.shadow.has_value())
    {
        node.computedShadow = *rule.shadow;
    }
    if (rule.font.has_value())
    {
        node.computedFont = *rule.font;
    }
    if (rule.padding.has_value())
    {
        node.layout.padding = *rule.padding;
    }
    if (rule.margin.has_value())
    {
        node.layout.margin = *rule.margin;
    }
    if (rule.width.has_value())
    {
        node.layout.width = *rule.width;
    }
    if (rule.height.has_value())
    {
        node.layout.height = *rule.height;
    }
    if (!rule.transitions.empty())
    {
        node.transitions = rule.transitions;
    }
}

// --- Token resolution helpers ---

// Parse a hex color string (#RGB, #RGBA, #RRGGBB, #RRGGBBAA)
static std::optional<glm::vec4> ParseHexColor(const std::string& hex)
{
    if (hex.empty() || hex[0] != '#')
    {
        return std::nullopt;
    }

    std::string digits = hex.substr(1);
    if (digits.length() == 3)
    {
        // #RGB -> #RRGGBB
        digits = std::string(2, digits[0]) + std::string(2, digits[1]) + std::string(2, digits[2]);
    }
    else if (digits.length() == 4)
    {
        // #RGBA -> #RRGGBBAA
        digits = std::string(2, digits[0]) + std::string(2, digits[1]) + std::string(2, digits[2])
               + std::string(2, digits[3]);
    }

    if (digits.length() == 6)
    {
        // #RRGGBB
        unsigned int r = 0, g = 0, b = 0;
        std::sscanf(digits.c_str(), "%02x%02x%02x", &r, &g, &b);
        return glm::vec4(r / 255.0F, g / 255.0F, b / 255.0F, 1.0F);
    }
    else if (digits.length() == 8)
    {
        // #RRGGBBAA
        unsigned int r = 0, g = 0, b = 0, a = 0;
        std::sscanf(digits.c_str(), "%02x%02x%02x%02x", &r, &g, &b, &a);
        return glm::vec4(r / 255.0F, g / 255.0F, b / 255.0F, a / 255.0F);
    }

    return std::nullopt;
}

// Check if string is a var() reference
static bool IsVarReference(const std::string& value)
{
    return value.length() > 5 && value.substr(0, 4) == "var(" && value.back() == ')';
}

// Extract token name from var(--name)
static std::string ExtractVarName(const std::string& value)
{
    if (!IsVarReference(value))
    {
        return "";
    }
    // var(--name) -> --name
    return value.substr(4, value.length() - 5);
}

glm::vec4 ResolveColor(const std::string& value, const TokenCollection& tokens, const glm::vec4& fallback)
{
    if (value.empty())
    {
        return fallback;
    }

    // Check for var() reference
    if (IsVarReference(value))
    {
        std::string tokenName = ExtractVarName(value);
        return tokens.GetColorToken(tokenName, fallback);
    }

    // Try hex color
    if (value[0] == '#')
    {
        auto color = ParseHexColor(value);
        if (color.has_value())
        {
            return *color;
        }
    }

    // Try named colors
    if (value == "transparent")
    {
        return glm::vec4(0.0F, 0.0F, 0.0F, 0.0F);
    }
    if (value == "white")
    {
        return glm::vec4(1.0F, 1.0F, 1.0F, 1.0F);
    }
    if (value == "black")
    {
        return glm::vec4(0.0F, 0.0F, 0.0F, 1.0F);
    }
    if (value == "red")
    {
        return glm::vec4(1.0F, 0.0F, 0.0F, 1.0F);
    }
    if (value == "green")
    {
        return glm::vec4(0.0F, 1.0F, 0.0F, 1.0F);
    }
    if (value == "blue")
    {
        return glm::vec4(0.0F, 0.0F, 1.0F, 1.0F);
    }

    return fallback;
}

float ResolveFloat(const std::string& value, const TokenCollection& tokens, float fallback)
{
    if (value.empty())
    {
        return fallback;
    }

    // Check for var() reference
    if (IsVarReference(value))
    {
        std::string tokenName = ExtractVarName(value);
        return tokens.GetFloatToken(tokenName, fallback);
    }

    // Try to parse as float
    try
    {
        return std::stof(value);
    }
    catch (...)
    {
        return fallback;
    }
}

} // namespace engine::ui
