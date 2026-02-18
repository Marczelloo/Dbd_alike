#pragma once

#ifdef BUILD_WITH_IMGUI

#include <functional>
#include <unordered_map>
#include <unordered_set>
#include <string>
#include <vector>

#include <glm/vec2.hpp>
#include <glm/vec4.hpp>
#include <imgui.h>

#include "engine/ui/UiTree.hpp"

namespace engine::ui
{
enum class EditorMode
{
    None,
    Edit,
    Preview,
    Create
};

struct NodeTemplate
{
    std::string name;
    UINodeType type = UINodeType::Container;
    std::vector<std::string> defaultClasses;
    std::function<void(UINode&)> setupCallback;
};

struct EditorNotification
{
    std::string message;
    float duration = 3.0F;
    float remaining = 3.0F;
    glm::vec4 color{1.0F, 1.0F, 1.0F, 1.0F};
};

class UiEditor
{
public:
    UiEditor();
    ~UiEditor();

    void Initialize(UiTree* tree);
    void SetTree(UiTree* tree);
    [[nodiscard]] UiTree* GetTree() const { return m_tree; }

    void Render();
    void ProcessPendingFontLoads();
    void ToggleEditor();
    void SetMode(EditorMode mode) { m_mode = mode; }
    [[nodiscard]] EditorMode GetMode() const { return m_mode; }

    void SelectNode(UINode* node);
    void ClearSelection();
    [[nodiscard]] UINode* GetSelectedNode() const { return m_selectedNode; }

    bool LoadScreen(const std::string& filePath);
    bool SaveScreen(const std::string& filePath);
    bool SaveCurrentScreen();
    bool LoadStyleSheet(const std::string& filePath);
    bool LoadTokens(const std::string& filePath);
    void ReloadCurrentScreen();

    UINode* CreateNode(UINodeType type, const std::string& id, UINode* parent = nullptr);
    void DeleteSelectedNode();
    void DuplicateSelectedNode();
    void CopySelectedNode();
    void PasteNode();
    void AddTemplate(const NodeTemplate& templ);
    UINode* CreateFromTemplate(const std::string& templateName, float x, float y);

    bool CanUndo() const;
    bool CanRedo() const;
    void Undo();
    void Redo();

    void ShowNotification(const std::string& message, float duration = 3.0F, const glm::vec4& color = glm::vec4(1.0F));
    void HandleKeyboardShortcuts();

private:
    enum class PendingHierarchyActionType
    {
        AddPanel,
        AddButton,
        AddText,
        AddShape,
        Delete,
        Duplicate,
        Reparent,
        MoveUp,
        MoveDown
    };

    struct PendingHierarchyAction
    {
        PendingHierarchyActionType type = PendingHierarchyActionType::AddPanel;
        UINode* node = nullptr;
        UINode* aux = nullptr;
    };

    void RenderMenuBar();
    void RenderHierarchyPanel();
    void RenderCanvasPanel();
    void RenderInspectorPanel();
    void RenderStylePanel();
    void RenderAssetsPanel();
    void RenderPreviewPanel();
    void BuildDefaultDockLayout();

    void RenderHierarchyNode(UINode& node, int depth = 0);
    void RenderNodeContextMenu(UINode& node);
    void RenderCanvasGrid();
    void RenderCanvasNode(UINode& node);
    void HandleCanvasInteraction(UINode& node);
    void RenderInspectorNodeProperties();
    void RenderInspectorLayout();
    void RenderInspectorStyle();
    void RenderInspectorInteractions();
    void RefreshAvailableFonts();
    ImFont* EnsureEditorFontLoaded(const std::string& path);
    [[nodiscard]] ImFont* ResolveNodeFont(const UINode& node);
    [[nodiscard]] float ComputeCanvasFontSize(const UINode& node) const;

    [[nodiscard]] std::string GenerateNodeId(UINodeType type) const;
    void SnapValue(float& value) const;
    void SnapPosition(float& x, float& y) const;
    [[nodiscard]] bool IsNodeVisible(const UINode& node) const;

    struct EditorAction
    {
        std::string description;
        std::string beforeState;
        std::string afterState;
    };

    void PushAction(const std::string& description);
    void SaveState(std::string& outState);
    void RestoreState(const std::string& state);
    void UpdateStateSnapshot();
    void QueueHierarchyAction(PendingHierarchyActionType type, UINode* node, UINode* aux = nullptr);
    void ApplyPendingHierarchyActions();
    [[nodiscard]] bool IsNodeInTree(const UINode* node) const;

    UiTree* m_tree = nullptr;
    UINode* m_selectedNode = nullptr;
    UINode* m_draggedNode = nullptr;
    EditorMode m_mode = EditorMode::Edit;
    bool m_isOpen = true;
    bool m_dockLayoutInitialized = false;
    unsigned int m_dockspaceId = 0;
    float m_leftPaneWidth = 300.0F;
    float m_rightPaneWidth = 360.0F;
    float m_bottomPaneHeight = 220.0F;
    float m_leftBottomRatio = 0.45F;
    float m_rightBottomRatio = 0.50F;

    float m_canvasZoom = 1.0F;
    glm::vec2 m_canvasPan{0.0F, 0.0F};
    ImVec2 m_canvasScreenPos{0.0F, 0.0F};
    ImVec2 m_canvasScreenSize{0.0F, 0.0F};

    float m_gridSize = 8.0F;
    bool m_snapToGrid = true;
    bool m_showLayoutBounds = false;
    bool m_autoReload = true;

    std::vector<NodeTemplate> m_templates;
    std::string m_currentScreenPath;
    std::string m_currentStylePath;
    std::string m_currentTokensPath;
    bool m_hasUnsavedChanges = false;

    std::vector<EditorAction> m_undoStack;
    std::vector<EditorAction> m_redoStack;
    std::string m_stateSnapshot;
    std::string m_clipboard;
    std::vector<EditorNotification> m_notifications;
    std::vector<PendingHierarchyAction> m_pendingHierarchyActions;
    std::vector<std::string> m_availableFontPaths;
    std::vector<std::string> m_availableFontLabels;
    std::vector<std::string> m_customFontPaths;
    std::unordered_map<std::string, ImFont*> m_editorFontCache;
    std::unordered_set<std::string> m_pendingEditorFontLoads;
    char m_customFontPathInput[512]{};
    bool m_availableFontsDirty = true;

    float m_hotReloadPollSeconds = 0.0F;
    long long m_lastScreenModTime = 0;
    long long m_lastStyleModTime = 0;
    long long m_lastTokensModTime = 0;
};

} // namespace engine::ui

#endif // BUILD_WITH_IMGUI
