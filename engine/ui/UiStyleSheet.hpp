#pragma once

#include <string>
#include <vector>

#include "engine/ui/UiStyle.hpp"

namespace engine::ui
{
// Parse a selector string into a Selector object
// Supports: Type, #id, .class, :pseudo, descendant (.parent .child)
Selector ParseSelector(const std::string& selectorStr);

// Parse a stylesheet from JSON format
// Format:
// {
//   "asset_version": 1,
//   "rules": [
//     {
//       "selector": "Button",
//       "properties": { "backgroundColor": "#1a1d24", "textColor": "#ebeff7" }
//     }
//   ]
// }
bool ParseStyleSheet(const std::string& jsonContent, StyleSheet& outStyleSheet);

// Parse a token collection from JSON format
// Format:
// {
//   "asset_version": 1,
//   "tokens": {
//     "--bg": "#0F1115",
//     "--spacing-md": 16
//   }
// }
bool ParseTokens(const std::string& jsonContent, TokenCollection& outTokens);

// Helper to parse a color value (hex, named, or var() reference)
std::optional<glm::vec4> ParseColor(const std::string& value, const TokenCollection* tokens = nullptr);

// Helper to parse a size value (number, px, %, vw, vh)
SizeValue ParseSize(const std::string& value);

// Helper to parse edge insets
EdgeInsets ParseEdgeInsets(const std::vector<float>& values);

} // namespace engine::ui
