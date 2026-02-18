#include "engine/ui/UiStyleSheet.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>

#include <nlohmann/json.hpp>

namespace engine::ui
{
namespace
{
using json = nlohmann::json;

// Trim whitespace from string
std::string Trim(std::string str)
{
    // Left trim
    str.erase(str.begin(), std::find_if(str.begin(), str.end(), [](unsigned char ch) {
        return !std::isspace(ch);
    }));
    // Right trim
    str.erase(std::find_if(str.rbegin(), str.rend(), [](unsigned char ch) {
        return !std::isspace(ch);
    }).base(), str.end());
    return str;
}

// Split string by delimiter
std::vector<std::string> Split(const std::string& str, char delimiter)
{
    std::vector<std::string> parts;
    std::stringstream ss(str);
    std::string part;
    while (std::getline(ss, part, delimiter))
    {
        part = Trim(part);
        if (!part.empty())
        {
            parts.push_back(part);
        }
    }
    return parts;
}

std::optional<std::string> ReadStringOrNumber(const json& value)
{
    if (value.is_string())
    {
        return value.get<std::string>();
    }
    if (value.is_number_float() || value.is_number_integer() || value.is_number_unsigned())
    {
        return std::to_string(value.get<float>());
    }
    return std::nullopt;
}

std::optional<float> ReadFloat(const json& value)
{
    if (value.is_number_float() || value.is_number_integer() || value.is_number_unsigned())
    {
        return value.get<float>();
    }
    if (value.is_string())
    {
        try
        {
            return std::stof(value.get<std::string>());
        }
        catch (...)
        {
            return std::nullopt;
        }
    }
    return std::nullopt;
}

// Parse pseudo-class from string
PseudoClass ParsePseudoClass(const std::string& pseudo)
{
    if (pseudo == "hover")
        return PseudoClass::Hover;
    if (pseudo == "pressed" || pseudo == "active")
        return PseudoClass::Pressed;
    if (pseudo == "focus")
        return PseudoClass::Focus;
    if (pseudo == "disabled")
        return PseudoClass::Disabled;
    if (pseudo == "selected")
        return PseudoClass::Selected;
    if (pseudo == "checked")
        return PseudoClass::Checked;
    return PseudoClass::None;
}

// Parse hex color
std::optional<glm::vec4> ParseHexColor(const std::string& hex)
{
    if (hex.empty() || hex[0] != '#')
    {
        return std::nullopt;
    }

    std::string digits = hex.substr(1);
    if (digits.length() == 3)
    {
        digits = std::string(2, digits[0]) + std::string(2, digits[1]) + std::string(2, digits[2]);
    }
    else if (digits.length() == 4)
    {
        digits = std::string(2, digits[0]) + std::string(2, digits[1]) + std::string(2, digits[2])
               + std::string(2, digits[3]);
    }

    if (digits.length() == 6)
    {
        unsigned int r = 0, g = 0, b = 0;
        std::sscanf(digits.c_str(), "%02x%02x%02x", &r, &g, &b);
        return glm::vec4(r / 255.0F, g / 255.0F, b / 255.0F, 1.0F);
    }
    else if (digits.length() == 8)
    {
        unsigned int r = 0, g = 0, b = 0, a = 0;
        std::sscanf(digits.c_str(), "%02x%02x%02x%02x", &r, &g, &b, &a);
        return glm::vec4(r / 255.0F, g / 255.0F, b / 255.0F, a / 255.0F);
    }

    return std::nullopt;
}

// Parse rgba() color
std::optional<glm::vec4> ParseRgbaColor(const std::string& rgba)
{
    // rgba(r, g, b, a) or rgb(r, g, b)
    if (rgba.substr(0, 4) == "rgba")
    {
        float r = 0.0F, g = 0.0F, b = 0.0F, a = 1.0F;
        if (std::sscanf(rgba.c_str(), "rgba(%f,%f,%f,%f)", &r, &g, &b, &a) == 4)
        {
            return glm::vec4(r / 255.0F, g / 255.0F, b / 255.0F, a);
        }
    }
    else if (rgba.substr(0, 3) == "rgb")
    {
        float r = 0.0F, g = 0.0F, b = 0.0F;
        if (std::sscanf(rgba.c_str(), "rgb(%f,%f,%f)", &r, &g, &b) == 3)
        {
            return glm::vec4(r / 255.0F, g / 255.0F, b / 255.0F, 1.0F);
        }
    }
    return std::nullopt;
}

// Named colors
std::optional<glm::vec4> ParseNamedColor(const std::string& name)
{
    static const std::unordered_map<std::string, glm::vec4> namedColors = {
        {"transparent", glm::vec4(0.0F, 0.0F, 0.0F, 0.0F)},
        {"white", glm::vec4(1.0F, 1.0F, 1.0F, 1.0F)},
        {"black", glm::vec4(0.0F, 0.0F, 0.0F, 1.0F)},
        {"red", glm::vec4(1.0F, 0.0F, 0.0F, 1.0F)},
        {"green", glm::vec4(0.0F, 1.0F, 0.0F, 1.0F)},
        {"blue", glm::vec4(0.0F, 0.0F, 1.0F, 1.0F)},
        {"yellow", glm::vec4(1.0F, 1.0F, 0.0F, 1.0F)},
        {"cyan", glm::vec4(0.0F, 1.0F, 1.0F, 1.0F)},
        {"magenta", glm::vec4(1.0F, 0.0F, 1.0F, 1.0F)},
        {"gray", glm::vec4(0.5F, 0.5F, 0.5F, 1.0F)},
        {"grey", glm::vec4(0.5F, 0.5F, 0.5F, 1.0F)},
        {"orange", glm::vec4(1.0F, 0.65F, 0.0F, 1.0F)},
        {"purple", glm::vec4(0.5F, 0.0F, 0.5F, 1.0F)},
        {"pink", glm::vec4(1.0F, 0.75F, 0.8F, 1.0F)},
    };

    auto it = namedColors.find(name);
    if (it != namedColors.end())
    {
        return it->second;
    }
    return std::nullopt;
}

} // namespace

Selector ParseSelector(const std::string& selectorStr)
{
    Selector selector;

    // Split by whitespace for descendant selectors
    std::vector<std::string> parts = Split(selectorStr, ' ');

    for (const std::string& part : parts)
    {
        if (part.empty())
            continue;

        SelectorPart selPart;
        selPart.type = SelectorType::Type;

        std::string remaining = part;
        std::string pseudo;

        // Extract pseudo-class
        std::size_t colonPos = remaining.find(':');
        if (colonPos != std::string::npos)
        {
            pseudo = remaining.substr(colonPos + 1);
            remaining = remaining.substr(0, colonPos);
            selPart.pseudo = ParsePseudoClass(pseudo);
        }

        // Determine selector type
        if (remaining.empty())
        {
            selPart.type = SelectorType::Universal;
        }
        else if (remaining.find('.') != std::string::npos
                 || (remaining.find('#') != std::string::npos && remaining[0] != '#')
                 || (remaining[0] == '#' && remaining.find('.', 1) != std::string::npos))
        {
            // Compound selector segment on the same node, e.g. "Button.primary" or "#id.primary".
            selPart.type = SelectorType::Type;
            selPart.value = remaining;
        }
        else if (remaining[0] == '#')
        {
            selPart.type = SelectorType::Id;
            selPart.value = remaining.substr(1);
        }
        else if (remaining[0] == '.')
        {
            selPart.type = SelectorType::Class;
            selPart.value = remaining.substr(1);
        }
        else if (remaining == "*")
        {
            selPart.type = SelectorType::Universal;
        }
        else
        {
            selPart.type = SelectorType::Type;
            selPart.value = remaining;
        }

        selector.parts.push_back(selPart);
    }

    return selector;
}

std::optional<glm::vec4> ParseColor(const std::string& value, const TokenCollection* tokens)
{
    std::string v = Trim(value);

    if (v.empty())
    {
        return std::nullopt;
    }

    // var() reference
    if (v.length() > 5 && v.substr(0, 4) == "var(" && v.back() == ')')
    {
        std::string tokenName = v.substr(4, v.length() - 5);
        if (tokens)
        {
            return tokens->GetColorToken(tokenName, glm::vec4(1.0F));
        }
        return std::nullopt;
    }

    // Hex color
    if (v[0] == '#')
    {
        return ParseHexColor(v);
    }

    // rgb/rgba
    if (v.substr(0, 3) == "rgb")
    {
        return ParseRgbaColor(v);
    }

    // Named color
    return ParseNamedColor(v);
}

SizeValue ParseSize(const std::string& value)
{
    std::string v = Trim(value);

    if (v.empty() || v == "auto")
    {
        return SizeValue::Auto();
    }

    // Check for units
    if (v.length() > 2 && v.substr(v.length() - 2) == "px")
    {
        float num = std::stof(v.substr(0, v.length() - 2));
        return SizeValue::Px(num);
    }
    if (v.length() > 1 && v.back() == '%')
    {
        float num = std::stof(v.substr(0, v.length() - 1));
        return SizeValue::Percent(num);
    }
    if (v.length() > 2 && v.substr(v.length() - 2) == "vw")
    {
        float num = std::stof(v.substr(0, v.length() - 2));
        return SizeValue::Vw(num);
    }
    if (v.length() > 2 && v.substr(v.length() - 2) == "vh")
    {
        float num = std::stof(v.substr(0, v.length() - 2));
        return SizeValue::Vh(num);
    }

    // Assume pixels if no unit
    try
    {
        float num = std::stof(v);
        return SizeValue::Px(num);
    }
    catch (...)
    {
        return SizeValue::Auto();
    }
}

EdgeInsets ParseEdgeInsets(const std::vector<float>& values)
{
    EdgeInsets insets;

    switch (values.size())
    {
        case 1:
            insets = EdgeInsets::All(values[0]);
            break;
        case 2:
            insets = EdgeInsets::Symmetric(values[0], values[1]);  // top/bottom, left/right
            break;
        case 3:
            insets.top = values[0];
            insets.right = values[1];
            insets.bottom = values[2];
            insets.left = values[1];  // left = right
            break;
        case 4:
            insets.top = values[0];
            insets.right = values[1];
            insets.bottom = values[2];
            insets.left = values[3];
            break;
        default:
            break;
    }

    return insets;
}

bool ParseStyleSheet(const std::string& jsonContent, StyleSheet& outStyleSheet)
{
    try
    {
        json root = json::parse(jsonContent);

        // Check version
        if (!root.contains("asset_version"))
        {
            return false;
        }

        if (!root.contains("rules") || !root["rules"].is_array())
        {
            return false;
        }

        outStyleSheet.rules.clear();

        for (const auto& ruleJson : root["rules"])
        {
            if (!ruleJson.contains("selector"))
            {
                continue;
            }

            StyleRule rule;
            rule.selector = ParseSelector(ruleJson["selector"].get<std::string>());

            if (rule.selector.IsEmpty())
            {
                continue;
            }

            if (ruleJson.contains("properties") && ruleJson["properties"].is_object())
            {
                const auto& props = ruleJson["properties"];

                if (props.contains("backgroundColor"))
                {
                    if (auto expr = ReadStringOrNumber(props["backgroundColor"]))
                    {
                        rule.backgroundColorExpr = *expr;
                        auto color = ParseColor(*expr);
                        if (color.has_value())
                        {
                            rule.backgroundColor = *color;
                        }
                    }
                }

                if (props.contains("textColor"))
                {
                    if (auto expr = ReadStringOrNumber(props["textColor"]))
                    {
                        rule.textColorExpr = *expr;
                        auto color = ParseColor(*expr);
                        if (color.has_value())
                        {
                            rule.textColor = *color;
                        }
                    }
                }

                if (props.contains("color") && !rule.textColor.has_value())
                {
                    if (auto expr = ReadStringOrNumber(props["color"]))
                    {
                        rule.textColorExpr = *expr;
                        auto color = ParseColor(*expr);
                        if (color.has_value())
                        {
                            rule.textColor = *color;
                        }
                    }
                }

                if (props.contains("opacity"))
                {
                    if (auto numeric = ReadFloat(props["opacity"]))
                    {
                        rule.opacity = *numeric;
                    }
                    if (auto expr = ReadStringOrNumber(props["opacity"]))
                    {
                        rule.opacityExpr = *expr;
                    }
                }

                if (props.contains("radius"))
                {
                    if (auto numeric = ReadFloat(props["radius"]))
                    {
                        rule.radius = *numeric;
                    }
                    if (auto expr = ReadStringOrNumber(props["radius"]))
                    {
                        rule.radiusExpr = *expr;
                    }
                }

                if (props.contains("borderRadius") && !rule.radius.has_value())
                {
                    if (auto numeric = ReadFloat(props["borderRadius"]))
                    {
                        rule.radius = *numeric;
                    }
                    if (auto expr = ReadStringOrNumber(props["borderRadius"]))
                    {
                        rule.radiusExpr = *expr;
                    }
                }

                if (props.contains("strokeColor"))
                {
                    if (auto expr = ReadStringOrNumber(props["strokeColor"]))
                    {
                        rule.strokeColorExpr = *expr;
                        auto color = ParseColor(*expr);
                        if (color.has_value())
                        {
                            rule.strokeColor = *color;
                        }
                    }
                }

                if (props.contains("borderColor") && !rule.strokeColor.has_value())
                {
                    if (auto expr = ReadStringOrNumber(props["borderColor"]))
                    {
                        rule.strokeColorExpr = *expr;
                        auto color = ParseColor(*expr);
                        if (color.has_value())
                        {
                            rule.strokeColor = *color;
                        }
                    }
                }

                if (props.contains("strokeWidth"))
                {
                    if (auto numeric = ReadFloat(props["strokeWidth"]))
                    {
                        rule.strokeWidth = *numeric;
                    }
                    if (auto expr = ReadStringOrNumber(props["strokeWidth"]))
                    {
                        rule.strokeWidthExpr = *expr;
                    }
                }

                if (props.contains("borderWidth") && !rule.strokeWidth.has_value())
                {
                    if (auto numeric = ReadFloat(props["borderWidth"]))
                    {
                        rule.strokeWidth = *numeric;
                    }
                    if (auto expr = ReadStringOrNumber(props["borderWidth"]))
                    {
                        rule.strokeWidthExpr = *expr;
                    }
                }

                if (props.contains("padding"))
                {
                    if (props["padding"].is_array())
                    {
                        std::vector<float> values = props["padding"].get<std::vector<float>>();
                        rule.padding = ParseEdgeInsets(values);
                    }
                    else if (props["padding"].is_number())
                    {
                        rule.padding = EdgeInsets::All(props["padding"].get<float>());
                    }
                }

                if (props.contains("margin"))
                {
                    if (props["margin"].is_array())
                    {
                        std::vector<float> values = props["margin"].get<std::vector<float>>();
                        rule.margin = ParseEdgeInsets(values);
                    }
                    else if (props["margin"].is_number())
                    {
                        rule.margin = EdgeInsets::All(props["margin"].get<float>());
                    }
                }

                if (props.contains("width"))
                {
                    if (props["width"].is_string())
                    {
                        rule.width = ParseSize(props["width"].get<std::string>());
                    }
                    else if (props["width"].is_number())
                    {
                        rule.width = SizeValue::Px(props["width"].get<float>());
                    }
                }

                if (props.contains("height"))
                {
                    if (props["height"].is_string())
                    {
                        rule.height = ParseSize(props["height"].get<std::string>());
                    }
                    else if (props["height"].is_number())
                    {
                        rule.height = SizeValue::Px(props["height"].get<float>());
                    }
                }
            }

            outStyleSheet.AddRule(rule);
        }

        return true;
    }
    catch (const std::exception&)
    {
        return false;
    }
}

bool ParseTokens(const std::string& jsonContent, TokenCollection& outTokens)
{
    try
    {
        json root = json::parse(jsonContent);

        // Check version
        if (!root.contains("asset_version"))
        {
            return false;
        }

        if (!root.contains("tokens") || !root["tokens"].is_object())
        {
            return false;
        }

        outTokens.tokens.clear();

        for (auto it = root["tokens"].begin(); it != root["tokens"].end(); ++it)
        {
            std::string name = it.key();
            TokenValue value;

            if (it->is_string())
            {
                std::string strVal = it->get<std::string>();
                // Try to parse as color
                auto color = ParseColor(strVal);
                if (color)
                {
                    value = *color;
                }
                else
                {
                    value = strVal;
                }
            }
            else if (it->is_number())
            {
                value = it->get<float>();
            }
            else
            {
                continue;
            }

            outTokens.SetToken(name, value);
        }

        return true;
    }
    catch (const std::exception&)
    {
        return false;
    }
}

} // namespace engine::ui
