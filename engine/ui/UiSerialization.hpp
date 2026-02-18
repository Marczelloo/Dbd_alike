#pragma once

#include <memory>
#include <string>

#include "engine/ui/UiNode.hpp"
#include "engine/ui/UiStyle.hpp"
#include "engine/ui/UiTree.hpp"

namespace engine::ui
{
// Convert node type to string (for serialization/debugging)
std::string NodeTypeToString(UINodeType type);

// Load a UI screen definition from JSON file
// Format:
// {
//   "asset_version": 1,
//   "id": "main_menu",
//   "root": {
//     "id": "root",
//     "type": "Panel",
//     "layout": { "display": "flex", "flexDirection": "column" },
//     "children": [...]
//   }
// }
std::unique_ptr<UINode> LoadScreen(const std::string& filePath);

// Load a UI screen from JSON string
std::unique_ptr<UINode> ParseScreen(const std::string& jsonContent);

// Save a UI screen to JSON file
bool SaveScreen(const std::string& filePath, const UINode& root);

// Serialize a node tree to JSON string
std::string SerializeScreen(const UINode& root);

// Load a stylesheet from file
bool LoadStyleSheet(const std::string& filePath, StyleSheet& outStyleSheet);

// Load tokens from file
bool LoadTokens(const std::string& filePath, TokenCollection& outTokens);

// Hot reload support - check file modification time
bool HasFileChanged(const std::string& filePath, long long& lastModTime);

} // namespace engine::ui
