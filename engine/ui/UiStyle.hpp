#pragma once

#include <optional>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

#include <glm/vec2.hpp>
#include <glm/vec4.hpp>

#include "engine/ui/UiNode.hpp"

namespace engine::ui
{
// CSS variable value (can be color, number, or string)
using TokenValue = std::variant<glm::vec4, float, std::string>;

// Token definition
struct Token
{
    std::string name;     // e.g. "--bg", "--spacing-md"
    TokenValue value;
    std::string description;
};

// Token collection (CSS variables)
class TokenCollection
{
public:
    std::string name;
    std::unordered_map<std::string, Token> tokens;

    void SetToken(const std::string& name, TokenValue value, const std::string& description = "");
    [[nodiscard]] bool HasToken(const std::string& name) const;
    [[nodiscard]] TokenValue GetToken(const std::string& name) const;
    [[nodiscard]] glm::vec4 GetColorToken(const std::string& name, const glm::vec4& fallback = glm::vec4(1.0F)) const;
    [[nodiscard]] float GetFloatToken(const std::string& name, float fallback = 0.0F) const;
    [[nodiscard]] std::string GetStringToken(const std::string& name, const std::string& fallback = "") const;
};

// Selector types
enum class SelectorType
{
    Type,        // Button, Text, Panel
    Id,          // #btn_play
    Class,       // .primary, .danger
    Universal,   // *
    Descendant   // .sidebar Button (parent then child)
};

// Pseudo-classes
enum class PseudoClass
{
    None,
    Hover,
    Pressed,
    Focus,
    Disabled,
    Selected,
    Checked
};

// Single selector component
struct SelectorPart
{
    SelectorType type = SelectorType::Type;
    std::string value;           // Type name, ID (without #), or class name (without .)
    PseudoClass pseudo = PseudoClass::None;
};

// Full selector (may be a chain for descendant selectors)
struct Selector
{
    std::vector<SelectorPart> parts;  // For ".sidebar Button": [Class("sidebar"), Type("Button")]

    [[nodiscard]] bool IsEmpty() const
    {
        return parts.empty();
    }
    [[nodiscard]] std::size_t Length() const
    {
        return parts.size();
    }
};

// Style rule: selector + properties
struct StyleRule
{
    Selector selector;
    std::optional<glm::vec4> backgroundColor;
    std::optional<std::string> backgroundColorExpr;
    std::optional<glm::vec4> textColor;
    std::optional<std::string> textColorExpr;
    std::optional<float> opacity;
    std::optional<std::string> opacityExpr;
    std::optional<float> radius;
    std::optional<std::string> radiusExpr;
    std::optional<glm::vec4> strokeColor;
    std::optional<std::string> strokeColorExpr;
    std::optional<float> strokeWidth;
    std::optional<std::string> strokeWidthExpr;
    std::optional<ShadowProps> shadow;
    std::optional<FontProps> font;
    std::optional<EdgeInsets> padding;
    std::optional<EdgeInsets> margin;
    std::optional<SizeValue> width;
    std::optional<SizeValue> height;
    std::vector<TransitionDef> transitions;
    int specificity = 0;  // Calculated specificity for priority
};

// StyleSheet: collection of rules
class StyleSheet
{
public:
    std::string name;
    std::vector<StyleRule> rules;

    void AddRule(const StyleRule& rule);
    void Clear();

    // Match rules for a given node (returns rules sorted by specificity)
    [[nodiscard]] std::vector<const StyleRule*> MatchRules(
        const UINode& node,
        const TokenCollection& tokens
    ) const;

private:
    [[nodiscard]] bool SelectorMatches(const Selector& selector, const UINode& node) const;
    [[nodiscard]] bool PartMatches(const SelectorPart& part, const UINode& node) const;
    [[nodiscard]] int CalculateSpecificity(const Selector& selector) const;
};

// Apply style properties to a node's computed style
void ApplyStyleToNode(
    const StyleRule& rule,
    UINode& node,
    const TokenCollection& tokens
);

// Resolve a token reference (var(--name)) to actual value
glm::vec4 ResolveColor(
    const std::string& value,
    const TokenCollection& tokens,
    const glm::vec4& fallback = glm::vec4(1.0F)
);

float ResolveFloat(
    const std::string& value,
    const TokenCollection& tokens,
    float fallback = 0.0F
);

} // namespace engine::ui
