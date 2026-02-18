#ifdef BUILD_WITH_IMGUI

#include "ui/editor/UiEditor.hpp"

#include <algorithm>
#include <array>
#include <cfloat>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <sstream>
#include <unordered_set>

#include <glm/trigonometric.hpp>
#include <imgui.h>
#include <backends/imgui_impl_opengl3.h>

#include "engine/ui/UiSerialization.hpp"

namespace engine::ui
{
namespace
{
glm::vec2 ToVec2(const ImVec2& value)
{
    return glm::vec2(value.x, value.y);
}

SizeValue::Unit SizeUnitFromIndex(int index)
{
    switch (index)
    {
        case 1:
            return SizeValue::Unit::Px;
        case 2:
            return SizeValue::Unit::Percent;
        case 3:
            return SizeValue::Unit::Vw;
        case 4:
            return SizeValue::Unit::Vh;
        default:
            return SizeValue::Unit::Auto;
    }
}

int SizeUnitToIndex(SizeValue::Unit unit)
{
    switch (unit)
    {
        case SizeValue::Unit::Px:
            return 1;
        case SizeValue::Unit::Percent:
            return 2;
        case SizeValue::Unit::Vw:
            return 3;
        case SizeValue::Unit::Vh:
            return 4;
        case SizeValue::Unit::Auto:
        default:
            return 0;
    }
}

const char* DisplayPath(const std::string& path)
{
    if (path.empty())
    {
        return "(none)";
    }
    return path.c_str();
}

float FontWeightAlphaMultiplier(FontProps::Weight weight)
{
    switch (weight)
    {
        case FontProps::Weight::ExtraLight:
            return 0.72F;
        case FontProps::Weight::Light:
            return 0.86F;
        case FontProps::Weight::Normal:
            return 1.0F;
        case FontProps::Weight::Medium:
            return 1.03F;
        case FontProps::Weight::SemiBold:
            return 1.06F;
        case FontProps::Weight::Bold:
        case FontProps::Weight::ExtraBold:
        default:
            return 1.1F;
    }
}

int FontWeightExtraPasses(FontProps::Weight weight)
{
    switch (weight)
    {
        case FontProps::Weight::Medium:
            return 1;
        case FontProps::Weight::SemiBold:
            return 2;
        case FontProps::Weight::Bold:
            return 3;
        case FontProps::Weight::ExtraBold:
            return 4;
        default:
            return 0;
    }
}
} // namespace

UiEditor::UiEditor() = default;
UiEditor::~UiEditor() = default;

void UiEditor::Initialize(UiTree* tree)
{
    m_tree = tree;
    m_selectedNode = nullptr;
    m_draggedNode = nullptr;
    m_canvasZoom = 1.0F;
    m_canvasPan = glm::vec2(0.0F, 0.0F);
    m_availableFontsDirty = true;
    UpdateStateSnapshot();
}

void UiEditor::SetTree(UiTree* tree)
{
    m_tree = tree;
    m_selectedNode = nullptr;
    m_draggedNode = nullptr;
    m_availableFontsDirty = true;
    UpdateStateSnapshot();
}

void UiEditor::ToggleEditor()
{
    m_isOpen = !m_isOpen;
}

void UiEditor::Render()
{
    if (!m_isOpen || m_mode == EditorMode::None)
    {
        return;
    }

    ProcessPendingFontLoads();

    if (m_autoReload)
    {
        m_hotReloadPollSeconds += ImGui::GetIO().DeltaTime;
        if (m_hotReloadPollSeconds >= 1.0F)
        {
            m_hotReloadPollSeconds = 0.0F;

            if (!m_currentScreenPath.empty() && HasFileChanged(m_currentScreenPath, m_lastScreenModTime))
            {
                LoadScreen(m_currentScreenPath);
            }
            if (!m_currentStylePath.empty() && HasFileChanged(m_currentStylePath, m_lastStyleModTime))
            {
                LoadStyleSheet(m_currentStylePath);
            }
            if (!m_currentTokensPath.empty() && HasFileChanged(m_currentTokensPath, m_lastTokensModTime))
            {
                LoadTokens(m_currentTokensPath);
            }
        }
    }

    HandleKeyboardShortcuts();
    if (!m_dockLayoutInitialized)
    {
        BuildDefaultDockLayout();
        m_dockLayoutInitialized = true;
    }

    ImGuiViewport* viewport = ImGui::GetMainViewport();
    const ImVec2 workspacePos = viewport->WorkPos;
    const ImVec2 workspaceSize = viewport->WorkSize;
    const float menuHeight = 34.0F;

    // Menu bar window.
    ImGui::SetNextWindowPos(workspacePos, ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(workspaceSize.x, menuHeight), ImGuiCond_Always);
    constexpr ImGuiWindowFlags menuFlags = ImGuiWindowFlags_MenuBar
                                        | ImGuiWindowFlags_NoTitleBar
                                        | ImGuiWindowFlags_NoResize
                                        | ImGuiWindowFlags_NoMove
                                        | ImGuiWindowFlags_NoCollapse
                                        | ImGuiWindowFlags_NoSavedSettings;
    if (ImGui::Begin("UI Editor Menu##UIEditor", &m_isOpen, menuFlags))
    {
        RenderMenuBar();
    }
    ImGui::End();

    if (!m_isOpen)
    {
        return;
    }

    const ImVec2 contentPos(workspacePos.x, workspacePos.y + menuHeight);
    const ImVec2 contentSize(workspaceSize.x, std::max(120.0F, workspaceSize.y - menuHeight));

    m_leftPaneWidth = std::clamp(m_leftPaneWidth, 220.0F, contentSize.x * 0.40F);
    m_rightPaneWidth = std::clamp(m_rightPaneWidth, 240.0F, contentSize.x * 0.42F);
    m_bottomPaneHeight = std::clamp(m_bottomPaneHeight, 140.0F, contentSize.y * 0.45F);
    m_leftBottomRatio = std::clamp(m_leftBottomRatio, 0.20F, 0.80F);
    m_rightBottomRatio = std::clamp(m_rightBottomRatio, 0.20F, 0.80F);

    const float centerWidth = std::max(300.0F, contentSize.x - m_leftPaneWidth - m_rightPaneWidth);
    const float centerTopHeight = std::max(180.0F, contentSize.y - m_bottomPaneHeight);

    constexpr ImGuiWindowFlags panelFlags = ImGuiWindowFlags_NoMove
                                         | ImGuiWindowFlags_NoResize
                                         | ImGuiWindowFlags_NoCollapse
                                         | ImGuiWindowFlags_NoSavedSettings;

    // Left column.
    const float leftTopHeight = std::max(120.0F, contentSize.y * (1.0F - m_leftBottomRatio));
    const float leftBottomHeight = std::max(80.0F, contentSize.y - leftTopHeight);
    ImGui::SetNextWindowPos(contentPos, ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(m_leftPaneWidth, leftTopHeight), ImGuiCond_Always);
    if (ImGui::Begin("Layers##UIEditor", nullptr, panelFlags))
    {
        RenderHierarchyPanel();
    }
    ImGui::End();

    ImGui::SetNextWindowPos(ImVec2(contentPos.x, contentPos.y + leftTopHeight), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(m_leftPaneWidth, leftBottomHeight), ImGuiCond_Always);
    if (ImGui::Begin("Assets##UIEditor", nullptr, panelFlags))
    {
        RenderAssetsPanel();
    }
    ImGui::End();

    // Center column.
    const ImVec2 centerPos(contentPos.x + m_leftPaneWidth, contentPos.y);
    ImGui::SetNextWindowPos(centerPos, ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(centerWidth, centerTopHeight), ImGuiCond_Always);
    if (ImGui::Begin("Canvas##UIEditor", nullptr, panelFlags))
    {
        RenderCanvasPanel();
    }
    ImGui::End();

    ImGui::SetNextWindowPos(ImVec2(centerPos.x, centerPos.y + centerTopHeight), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(centerWidth, m_bottomPaneHeight), ImGuiCond_Always);
    if (ImGui::Begin("Preview##UIEditor", nullptr, panelFlags))
    {
        RenderPreviewPanel();
    }
    ImGui::End();

    // Right column.
    const ImVec2 rightPos(centerPos.x + centerWidth, contentPos.y);
    const float rightTopHeight = std::max(120.0F, contentSize.y * (1.0F - m_rightBottomRatio));
    const float rightBottomHeight = std::max(80.0F, contentSize.y - rightTopHeight);
    ImGui::SetNextWindowPos(rightPos, ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(m_rightPaneWidth, rightTopHeight), ImGuiCond_Always);
    if (ImGui::Begin("Inspector##UIEditor", nullptr, panelFlags))
    {
        RenderInspectorPanel();
    }
    ImGui::End();

    ImGui::SetNextWindowPos(ImVec2(rightPos.x, rightPos.y + rightTopHeight), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(m_rightPaneWidth, rightBottomHeight), ImGuiCond_Always);
    if (ImGui::Begin("Styles##UIEditor", nullptr, panelFlags))
    {
        RenderStylePanel();
    }
    ImGui::End();

    if (!m_notifications.empty())
    {
        ImVec2 basePos = viewport->WorkPos;
        float offsetY = 18.0F;
        for (auto it = m_notifications.begin(); it != m_notifications.end();)
        {
            it->remaining -= ImGui::GetIO().DeltaTime;
            if (it->remaining <= 0.0F)
            {
                it = m_notifications.erase(it);
                continue;
            }

            ImGui::SetNextWindowBgAlpha(0.85F);
            ImGui::SetNextWindowPos(ImVec2(basePos.x + viewport->WorkSize.x - 320.0F, basePos.y + offsetY));
            ImGui::SetNextWindowSize(ImVec2(300.0F, 0.0F), ImGuiCond_Always);
            const std::string notifId = "##ui_editor_notif_" + std::to_string(std::distance(m_notifications.begin(), it));
            constexpr ImGuiWindowFlags notifFlags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings
                                                 | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoMove;
            if (ImGui::Begin(notifId.c_str(), nullptr, notifFlags))
            {
                ImGui::TextColored(ImVec4(it->color.r, it->color.g, it->color.b, it->color.a), "%s", it->message.c_str());
            }
            ImGui::End();
            offsetY += 40.0F;
            ++it;
        }
    }
}

void UiEditor::ProcessPendingFontLoads()
{
    ImGuiIO& io = ImGui::GetIO();
    if (m_pendingEditorFontLoads.empty() || io.Fonts->Locked)
    {
        return;
    }

    std::vector<std::string> pending(m_pendingEditorFontLoads.begin(), m_pendingEditorFontLoads.end());
    m_pendingEditorFontLoads.clear();
    for (const std::string& path : pending)
    {
        EnsureEditorFontLoaded(path);
    }
}

void UiEditor::RenderMenuBar()
{
    if (ImGui::BeginMenuBar())
    {
        if (ImGui::BeginMenu("File"))
        {
            if (ImGui::MenuItem("New Screen"))
            {
                if (m_tree)
                {
                    m_tree->SetRoot(std::make_unique<UINode>("root", UINodeType::Container));
                    m_currentScreenPath.clear();
                    m_undoStack.clear();
                    m_redoStack.clear();
                    UpdateStateSnapshot();
                }
            }
            if (ImGui::BeginMenu("Open Built-In Screen"))
            {
                if (ImGui::MenuItem("Main Menu"))
                {
                    LoadScreen("assets/ui/screens/main_menu.ui.json");
                }
                if (ImGui::MenuItem("Settings"))
                {
                    LoadScreen("assets/ui/screens/settings.ui.json");
                }
                if (ImGui::MenuItem("In-Game HUD"))
                {
                    LoadScreen("assets/ui/screens/in_game_hud.ui.json");
                }
                ImGui::EndMenu();
            }
            if (ImGui::MenuItem("Save Screen", "Ctrl+S", false, !m_currentScreenPath.empty()))
            {
                SaveScreen(m_currentScreenPath);
            }
            if (ImGui::MenuItem("Save Screen As...", nullptr, false))
            {
                // File dialog would go here
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Load Default Stylesheet"))
            {
                LoadStyleSheet("assets/ui/styles/base.ui.css.json");
            }
            if (ImGui::MenuItem("Load Default Tokens"))
            {
                LoadTokens("assets/ui/styles/theme_default.tokens.json");
            }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Edit"))
        {
            if (ImGui::MenuItem("Undo", "Ctrl+Z", false, CanUndo()))
            {
                Undo();
            }
            if (ImGui::MenuItem("Redo", "Ctrl+Y", false, CanRedo()))
            {
                Redo();
            }
            ImGui::Separator();
            if (ImGui::MenuItem(
                    "Delete",
                    "Del",
                    false,
                    m_tree != nullptr && m_selectedNode != nullptr && m_selectedNode != m_tree->GetRoot()))
            {
                DeleteSelectedNode();
            }
            if (ImGui::MenuItem("Duplicate", "Ctrl+D", false, m_selectedNode != nullptr))
            {
                DuplicateSelectedNode();
            }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("View"))
        {
            ImGui::Checkbox("Snap to Grid", &m_snapToGrid);
            ImGui::Checkbox("Show Layout Bounds", &m_showLayoutBounds);
            if (m_tree)
                m_tree->SetDebugLayout(m_showLayoutBounds);
            ImGui::Checkbox("Auto Reload", &m_autoReload);
            ImGui::Separator();
            ImGui::SetNextItemWidth(180.0F);
            ImGui::SliderFloat("Left Pane", &m_leftPaneWidth, 220.0F, 520.0F, "%.0f px");
            ImGui::SetNextItemWidth(180.0F);
            ImGui::SliderFloat("Right Pane", &m_rightPaneWidth, 240.0F, 560.0F, "%.0f px");
            ImGui::SetNextItemWidth(180.0F);
            ImGui::SliderFloat("Bottom Pane", &m_bottomPaneHeight, 120.0F, 420.0F, "%.0f px");
            ImGui::SetNextItemWidth(180.0F);
            ImGui::SliderFloat("Left Split", &m_leftBottomRatio, 0.20F, 0.80F, "%.2f");
            ImGui::SetNextItemWidth(180.0F);
            ImGui::SliderFloat("Right Split", &m_rightBottomRatio, 0.20F, 0.80F, "%.2f");
            if (ImGui::MenuItem("Reset Canvas View"))
            {
                m_canvasZoom = 1.0F;
                m_canvasPan = glm::vec2(0.0F, 0.0F);
            }
            if (ImGui::MenuItem("Reset Dock Layout"))
            {
                m_dockLayoutInitialized = false;
            }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Create"))
        {
            if (ImGui::MenuItem("Panel"))
                CreateNode(UINodeType::Panel, "");
            if (ImGui::MenuItem("Text"))
                CreateNode(UINodeType::Text, "");
            if (ImGui::MenuItem("Button"))
                CreateNode(UINodeType::Button, "");
            if (ImGui::MenuItem("Image"))
                CreateNode(UINodeType::Image, "");
            if (ImGui::MenuItem("Shape"))
                CreateNode(UINodeType::Shape, "");
            if (ImGui::MenuItem("Slider"))
                CreateNode(UINodeType::Slider, "");
            if (ImGui::MenuItem("Toggle"))
                CreateNode(UINodeType::Toggle, "");
            if (ImGui::MenuItem("TextInput"))
                CreateNode(UINodeType::TextInput, "");
            if (ImGui::MenuItem("ProgressBar"))
                CreateNode(UINodeType::ProgressBar, "");
            if (ImGui::MenuItem("ScrollView"))
                CreateNode(UINodeType::ScrollView, "");
            ImGui::EndMenu();
        }

        ImGui::EndMenuBar();
    }
}

void UiEditor::RenderHierarchyPanel()
{
    ImGui::Text("Layers");
    ImGui::SameLine();
    ImGui::TextDisabled("| Figma-like tree");
    ImGui::Separator();

    const auto sameLineIfFits = [](const char* nextLabel) {
        const ImGuiStyle& style = ImGui::GetStyle();
        const float nextWidth = ImGui::CalcTextSize(nextLabel).x + style.FramePadding.x * 2.0F;
        const float needed = style.ItemSpacing.x + nextWidth;
        if (ImGui::GetContentRegionAvail().x > needed)
        {
            ImGui::SameLine();
        }
    };

    if (ImGui::Button("+ Panel"))
    {
        CreateNode(UINodeType::Panel, "");
    }
    sameLineIfFits("+ Text");
    if (ImGui::Button("+ Text"))
    {
        CreateNode(UINodeType::Text, "");
    }
    sameLineIfFits("+ Button");
    if (ImGui::Button("+ Button"))
    {
        CreateNode(UINodeType::Button, "");
    }
    sameLineIfFits("+ Shape");
    if (ImGui::Button("+ Shape"))
    {
        CreateNode(UINodeType::Shape, "");
    }

    const bool hasSelection = m_selectedNode != nullptr;
    sameLineIfFits("Duplicate");
    ImGui::BeginDisabled(!hasSelection);
    if (ImGui::Button("Duplicate"))
    {
        DuplicateSelectedNode();
    }
    ImGui::EndDisabled();

    const bool canMoveUp = m_selectedNode != nullptr
                        && m_selectedNode->parent != nullptr
                        && !m_selectedNode->parent->children.empty()
                        && m_selectedNode->parent->children.front().get() != m_selectedNode;
    sameLineIfFits("Up");
    ImGui::BeginDisabled(!canMoveUp);
    if (ImGui::Button("Up"))
    {
        QueueHierarchyAction(PendingHierarchyActionType::MoveUp, m_selectedNode);
    }
    ImGui::EndDisabled();

    const bool canMoveDown = m_selectedNode != nullptr
                          && m_selectedNode->parent != nullptr
                          && !m_selectedNode->parent->children.empty()
                          && m_selectedNode->parent->children.back().get() != m_selectedNode;
    sameLineIfFits("Down");
    ImGui::BeginDisabled(!canMoveDown);
    if (ImGui::Button("Down"))
    {
        QueueHierarchyAction(PendingHierarchyActionType::MoveDown, m_selectedNode);
    }
    ImGui::EndDisabled();
    ImGui::Separator();

    if (!m_tree || !m_tree->GetRoot())
    {
        ImGui::TextDisabled("No tree loaded");
        return;
    }

    RenderHierarchyNode(*m_tree->GetRoot());
    ApplyPendingHierarchyActions();
}

void UiEditor::RenderHierarchyNode(UINode& node, int depth)
{
    (void)depth;
    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;

    if (node.children.empty())
    {
        flags |= ImGuiTreeNodeFlags_Leaf;
    }
    if (m_selectedNode == &node)
    {
        flags |= ImGuiTreeNodeFlags_Selected;
    }

    std::string label = node.name.empty() ? node.id : node.name;
    if (label.empty())
    {
        label = "Node";
    }
    label += " (" + std::string(NodeTypeToString(node.type)) + ")";

    bool isOpen = ImGui::TreeNodeEx(&node, flags, "%s", label.c_str());

    // Selection on click
    if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
    {
        SelectNode(&node);
    }

    if (ImGui::BeginDragDropSource())
    {
        UINode* payloadNode = &node;
        ImGui::SetDragDropPayload("UI_EDITOR_NODE", &payloadNode, sizeof(payloadNode));
        ImGui::TextUnformatted(label.c_str());
        ImGui::EndDragDropSource();
    }

    if (ImGui::BeginDragDropTarget())
    {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("UI_EDITOR_NODE"))
        {
            if (payload->DataSize == sizeof(UINode*))
            {
                auto* droppedNode = *static_cast<UINode* const*>(payload->Data);
                const bool validDrop = droppedNode != nullptr
                                     && droppedNode != &node
                                     && droppedNode->parent != nullptr
                                     && droppedNode->FindDescendant(node.id) == nullptr;
                if (validDrop)
                {
                    QueueHierarchyAction(PendingHierarchyActionType::Reparent, droppedNode, &node);
                }
            }
        }
        ImGui::EndDragDropTarget();
    }

    // Context menu
    RenderNodeContextMenu(node);

    if (isOpen)
    {
        for (const auto& child : node.children)
        {
            if (child)
            {
                RenderHierarchyNode(*child, depth + 1);
            }
        }
        ImGui::TreePop();
    }
}

void UiEditor::RenderNodeContextMenu(UINode& node)
{
    if (ImGui::BeginPopupContextItem())
    {
        if (ImGui::MenuItem("Add Panel"))
        {
            QueueHierarchyAction(PendingHierarchyActionType::AddPanel, &node);
        }
        if (ImGui::MenuItem("Add Button"))
        {
            QueueHierarchyAction(PendingHierarchyActionType::AddButton, &node);
        }
        if (ImGui::MenuItem("Add Text"))
        {
            QueueHierarchyAction(PendingHierarchyActionType::AddText, &node);
        }
        if (ImGui::MenuItem("Add Shape"))
        {
            QueueHierarchyAction(PendingHierarchyActionType::AddShape, &node);
        }
        ImGui::Separator();
        const bool canMoveUp = node.parent != nullptr
                            && !node.parent->children.empty()
                            && node.parent->children.front().get() != &node;
        const bool canMoveDown = node.parent != nullptr
                              && !node.parent->children.empty()
                              && node.parent->children.back().get() != &node;
        if (ImGui::MenuItem("Move Up", nullptr, false, canMoveUp))
        {
            QueueHierarchyAction(PendingHierarchyActionType::MoveUp, &node);
        }
        if (ImGui::MenuItem("Move Down", nullptr, false, canMoveDown))
        {
            QueueHierarchyAction(PendingHierarchyActionType::MoveDown, &node);
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Delete", nullptr, false, &node != m_tree->GetRoot()))
        {
            QueueHierarchyAction(PendingHierarchyActionType::Delete, &node);
        }
        if (ImGui::MenuItem("Duplicate"))
        {
            QueueHierarchyAction(PendingHierarchyActionType::Duplicate, &node);
        }
        ImGui::EndPopup();
    }
}

void UiEditor::RenderCanvasPanel()
{
    ImGui::Text("Canvas");
    ImGui::SameLine();
    ImGui::TextDisabled("| Zoom %.0f%%", m_canvasZoom * 100.0F);
    ImGui::SameLine();
    if (ImGui::SmallButton("Fit"))
    {
        m_canvasZoom = 1.0F;
        m_canvasPan = glm::vec2(0.0F, 0.0F);
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(140.0F);
    if (ImGui::SliderFloat("##zoom", &m_canvasZoom, 0.20F, 4.00F, "%.2fx"))
    {
        m_canvasZoom = std::clamp(m_canvasZoom, 0.20F, 4.00F);
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(120.0F);
    if (ImGui::SliderFloat("##grid", &m_gridSize, 4.0F, 64.0F, "Grid %.0f"))
    {
        m_gridSize = std::clamp(m_gridSize, 4.0F, 64.0F);
    }
    ImGui::SameLine();
    ImGui::Checkbox("Snap", &m_snapToGrid);
    ImGui::Separator();

    m_canvasScreenPos = ImGui::GetCursorScreenPos();
    m_canvasScreenSize = ImGui::GetContentRegionAvail();
    if (m_canvasScreenSize.x < 10.0F || m_canvasScreenSize.y < 10.0F)
    {
        return;
    }

    ImGui::InvisibleButton("##UICanvasInput", m_canvasScreenSize, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle);
    const bool hoveredCanvas = ImGui::IsItemHovered();
    ImDrawList* drawList = ImGui::GetWindowDrawList();

    if (m_tree && m_tree->GetRoot())
    {
        m_tree->ComputeLayout();
    }

    drawList->AddRectFilled(
        m_canvasScreenPos,
        ImVec2(m_canvasScreenPos.x + m_canvasScreenSize.x, m_canvasScreenPos.y + m_canvasScreenSize.y),
        IM_COL32(24, 24, 28, 255)
    );
    drawList->PushClipRect(
        m_canvasScreenPos,
        ImVec2(m_canvasScreenPos.x + m_canvasScreenSize.x, m_canvasScreenPos.y + m_canvasScreenSize.y),
        true
    );

    if (hoveredCanvas)
    {
        const float wheel = ImGui::GetIO().MouseWheel;
        if (std::abs(wheel) > 0.001F)
        {
            const float oldZoom = m_canvasZoom;
            const float zoomFactor = 1.0F + wheel * 0.1F;
            const ImVec2 mousePos = ImGui::GetMousePos();
            const glm::vec2 localMouse = ToVec2(mousePos) - ToVec2(m_canvasScreenPos);
            const glm::vec2 beforeVirtual = localMouse / oldZoom - m_canvasPan;
            m_canvasZoom = std::clamp(m_canvasZoom * zoomFactor, 0.20F, 4.00F);
            m_canvasPan = localMouse / m_canvasZoom - beforeVirtual;
        }

        const bool panRequested = ImGui::IsMouseDragging(ImGuiMouseButton_Middle)
                               || (ImGui::IsKeyDown(ImGuiKey_Space) && ImGui::IsMouseDragging(ImGuiMouseButton_Left));
        if (panRequested)
        {
            const ImVec2 delta = ImGui::GetIO().MouseDelta;
            m_canvasPan += glm::vec2(delta.x / m_canvasZoom, delta.y / m_canvasZoom);
        }
    }

    RenderCanvasGrid();

    if (m_tree && m_tree->GetRoot())
    {
        RenderCanvasNode(*m_tree->GetRoot());
    }

    auto nodeToScreenRect = [&](const UINode& node) {
        const float x = m_canvasScreenPos.x + (node.computedRect.x + m_canvasPan.x) * m_canvasZoom;
        const float y = m_canvasScreenPos.y + (node.computedRect.y + m_canvasPan.y) * m_canvasZoom;
        const float w = node.computedRect.w * m_canvasZoom;
        const float h = node.computedRect.h * m_canvasZoom;
        return std::array<float, 4>{x, y, x + w, y + h};
    };
    auto rectContains = [](const std::array<float, 4>& rect, const ImVec2& point) {
        return point.x >= rect[0] && point.y >= rect[1] && point.x <= rect[2] && point.y <= rect[3];
    };

    if (m_tree && m_tree->GetRoot())
    {
        std::vector<UINode*> candidates;
        candidates.reserve(256);
        std::function<void(UINode&)> collect = [&](UINode& node) {
            if (!IsNodeVisible(node))
            {
                return;
            }
            candidates.push_back(&node);
            for (const auto& child : node.children)
            {
                if (child)
                {
                    collect(*child);
                }
            }
        };
        collect(*m_tree->GetRoot());

        const ImVec2 mousePos = ImGui::GetMousePos();
        UINode* hoveredNode = nullptr;
        int hoveredOrder = -1;
        for (std::size_t i = 0; i < candidates.size(); ++i)
        {
            UINode* node = candidates[i];
            const auto rect = nodeToScreenRect(*node);
            if (!rectContains(rect, mousePos))
            {
                continue;
            }
            if (hoveredNode == nullptr
                || node->zIndex > hoveredNode->zIndex
                || (node->zIndex == hoveredNode->zIndex && static_cast<int>(i) > hoveredOrder))
            {
                hoveredNode = node;
                hoveredOrder = static_cast<int>(i);
            }
        }

        static int activeResizeHandle = -1; // 0=topleft, 1=topright, 2=bottomright, 3=bottomleft
        static bool draggingSelection = false;
        static bool dragMoved = false;
        static bool resizeMoved = false;
        static glm::vec2 dragStartOffset{0.0F, 0.0F};
        static glm::vec2 resizeStartOffset{0.0F, 0.0F};
        static glm::vec2 resizeStartSize{0.0F, 0.0F};
        static glm::vec2 resizeStartLineEnd{0.0F, 0.0F};
        static ComputedRect dragStartRect{};
        const bool previewMode = (m_mode == EditorMode::Preview);

        auto toScreenX = [&](float vx) { return m_canvasScreenPos.x + (vx + m_canvasPan.x) * m_canvasZoom; };
        auto toScreenY = [&](float vy) { return m_canvasScreenPos.y + (vy + m_canvasPan.y) * m_canvasZoom; };
        auto parentControlsPlacement = [](const UINode& node) {
            if (node.parent == nullptr)
            {
                return false;
            }
            return node.parent->layout.display == Display::Flex || node.parent->layout.display == Display::Grid;
        };
        auto ensureAbsoluteEditable = [&](UINode& node) {
            if (parentControlsPlacement(node))
            {
                return false;
            }
            if (node.layout.position == Position::Absolute)
            {
                return true;
            }
            const float parentContentX = node.parent ? node.parent->computedRect.contentX : 0.0F;
            const float parentContentY = node.parent ? node.parent->computedRect.contentY : 0.0F;
            node.layout.position = Position::Absolute;
            node.layout.anchor = glm::vec2(0.0F, 0.0F);
            node.layout.pivot = glm::vec2(0.0F, 0.0F);
            node.layout.offset = glm::vec2(node.computedRect.x - parentContentX, node.computedRect.y - parentContentY);
            node.layout.width = SizeValue::Px(std::max(12.0F, node.computedRect.w));
            node.layout.height = SizeValue::Px(std::max(12.0F, node.computedRect.h));
            node.layout.flexGrow = 0.0F;
            node.layout.flexShrink = 0.0F;
            if (m_snapToGrid)
            {
                SnapValue(node.layout.offset.x);
                SnapValue(node.layout.offset.y);
                float width = node.layout.width.value;
                float height = node.layout.height.value;
                SnapValue(width);
                SnapValue(height);
                node.layout.width = SizeValue::Px(std::max(12.0F, width));
                node.layout.height = SizeValue::Px(std::max(12.0F, height));
            }
            node.MarkLayoutDirty();
            m_tree->ComputeLayout();
            return true;
        };

        struct GapGuide
        {
            float x1 = 0.0F;
            float y1 = 0.0F;
            float x2 = 0.0F;
            float y2 = 0.0F;
            float value = 0.0F;
        };
        std::vector<GapGuide> spacingGuides;
        std::vector<std::array<float, 4>> snapGuides;
        const UINode* highlightedContainer = nullptr;

        if (m_selectedNode && IsNodeVisible(*m_selectedNode))
        {
            if (m_selectedNode->parent != nullptr
                && IsNodeVisible(*m_selectedNode->parent)
                && (m_selectedNode->parent->layout.display == Display::Flex || m_selectedNode->parent->layout.display == Display::Grid))
            {
                highlightedContainer = m_selectedNode->parent;
            }

            const auto selectedRect = nodeToScreenRect(*m_selectedNode);
            drawList->AddRect(
                ImVec2(selectedRect[0], selectedRect[1]),
                ImVec2(selectedRect[2], selectedRect[3]),
                IM_COL32(86, 156, 255, 255),
                2.0F,
                0,
                2.0F
            );

            constexpr float handleRadius = 4.0F;
            if (!previewMode)
            {
                const std::array<ImVec2, 4> handles = {
                    ImVec2(selectedRect[0], selectedRect[1]),
                    ImVec2(selectedRect[2], selectedRect[1]),
                    ImVec2(selectedRect[2], selectedRect[3]),
                    ImVec2(selectedRect[0], selectedRect[3])
                };
                for (std::size_t i = 0; i < handles.size(); ++i)
                {
                    drawList->AddCircleFilled(handles[i], handleRadius, IM_COL32(20, 20, 26, 255));
                    drawList->AddCircle(handles[i], handleRadius, IM_COL32(120, 182, 255, 255), 0, 1.5F);

                    if (hoveredCanvas && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
                    {
                        const float dx = mousePos.x - handles[i].x;
                        const float dy = mousePos.y - handles[i].y;
                        const float d2 = dx * dx + dy * dy;
                        if (d2 <= (handleRadius + 3.0F) * (handleRadius + 3.0F))
                        {
                            activeResizeHandle = static_cast<int>(i);
                            resizeMoved = false;
                            resizeStartOffset = m_selectedNode->layout.offset;
                            resizeStartSize = glm::vec2(
                                std::max(12.0F, m_selectedNode->computedRect.w),
                                std::max(12.0F, m_selectedNode->computedRect.h)
                            );
                            resizeStartLineEnd = m_selectedNode->shapeLineEnd;
                        }
                    }
                }
            }

            if (!previewMode && activeResizeHandle >= 0 && m_selectedNode != nullptr)
            {
                if (ImGui::IsMouseDown(ImGuiMouseButton_Left))
                {
                    ImVec2 dragDelta = ImGui::GetMouseDragDelta(ImGuiMouseButton_Left);
                    float dx = dragDelta.x / m_canvasZoom;
                    float dy = dragDelta.y / m_canvasZoom;
                    if (std::abs(dx) > 0.001F || std::abs(dy) > 0.001F)
                    {
                        resizeMoved = true;
                    }

                    float width = resizeStartSize.x;
                    float height = resizeStartSize.y;
                    glm::vec2 offset = resizeStartOffset;

                    const bool leftEdge = (activeResizeHandle == 0 || activeResizeHandle == 3);
                    const bool rightEdge = (activeResizeHandle == 1 || activeResizeHandle == 2);
                    const bool topEdge = (activeResizeHandle == 0 || activeResizeHandle == 1);
                    const bool bottomEdge = (activeResizeHandle == 2 || activeResizeHandle == 3);

                    if (rightEdge)
                    {
                        width += dx;
                    }
                    if (leftEdge)
                    {
                        width -= dx;
                        if (m_selectedNode->layout.position == Position::Absolute)
                        {
                            offset.x += dx;
                        }
                    }
                    if (bottomEdge)
                    {
                        height += dy;
                    }
                    if (topEdge)
                    {
                        height -= dy;
                        if (m_selectedNode->layout.position == Position::Absolute)
                        {
                            offset.y += dy;
                        }
                    }

                    width = std::max(12.0F, width);
                    height = std::max(12.0F, height);
                    if (m_snapToGrid)
                    {
                        SnapValue(width);
                        SnapValue(height);
                        if (m_selectedNode->layout.position == Position::Absolute)
                        {
                            SnapValue(offset.x);
                            SnapValue(offset.y);
                        }
                    }

                    m_selectedNode->layout.width = SizeValue::Px(width);
                    m_selectedNode->layout.height = SizeValue::Px(height);
                    if (m_selectedNode->layout.position == Position::Absolute)
                    {
                        m_selectedNode->layout.offset = offset;
                    }
                    if (m_selectedNode->type == UINodeType::Shape && m_selectedNode->shapeType == UIShapeType::Line)
                    {
                        const float startW = std::max(0.001F, resizeStartSize.x);
                        const float startH = std::max(0.001F, resizeStartSize.y);
                        const float sx = width / startW;
                        const float sy = height / startH;
                        m_selectedNode->shapeLineEnd = glm::vec2(resizeStartLineEnd.x * sx, resizeStartLineEnd.y * sy);
                        if (m_snapToGrid)
                        {
                            SnapValue(m_selectedNode->shapeLineEnd.x);
                            SnapValue(m_selectedNode->shapeLineEnd.y);
                        }
                    }
                    m_selectedNode->layout.flexGrow = 0.0F;
                    m_selectedNode->MarkLayoutDirty();
                    m_tree->ComputeLayout();
                }
                else
                {
                    if (resizeMoved)
                    {
                        PushAction("Resize Node");
                    }
                    activeResizeHandle = -1;
                    resizeMoved = false;
                    ImGui::ResetMouseDragDelta(ImGuiMouseButton_Left);
                }
            }

            if (!previewMode && hoveredCanvas && activeResizeHandle < 0 && ImGui::IsMouseClicked(ImGuiMouseButton_Left)
                && rectContains(selectedRect, mousePos) && !ImGui::IsKeyDown(ImGuiKey_Space))
            {
                if (parentControlsPlacement(*m_selectedNode))
                {
                    ShowNotification("Manual move disabled: parent uses Flex/Grid layout.", 1.6F, glm::vec4(1.0F, 0.78F, 0.35F, 1.0F));
                }
                else if (ensureAbsoluteEditable(*m_selectedNode))
                {
                    draggingSelection = true;
                    dragMoved = false;
                    m_draggedNode = m_selectedNode;
                    dragStartOffset = m_draggedNode->layout.offset;
                    dragStartRect = m_draggedNode->computedRect;
                }
            }
        }

        if (hoveredCanvas && activeResizeHandle < 0 && ImGui::IsMouseClicked(ImGuiMouseButton_Left)
            && !ImGui::IsKeyDown(ImGuiKey_Space))
        {
            if (hoveredNode != nullptr)
            {
                SelectNode(hoveredNode);
                if (previewMode && m_tree && !hoveredNode->id.empty())
                {
                    m_tree->TriggerClick(hoveredNode->id);
                    m_tree->ComputeLayout();
                }
            }
            else
            {
                ClearSelection();
            }
        }

        if (!previewMode && draggingSelection && m_draggedNode != nullptr && ImGui::IsMouseDown(ImGuiMouseButton_Left))
        {
            if (m_draggedNode->parent != nullptr && IsNodeVisible(*m_draggedNode->parent))
            {
                highlightedContainer = m_draggedNode->parent;
            }
            if (m_draggedNode->layout.position == Position::Absolute)
            {
                const ImVec2 dragDelta = ImGui::GetMouseDragDelta(ImGuiMouseButton_Left);
                glm::vec2 delta = glm::vec2(dragDelta.x / m_canvasZoom, dragDelta.y / m_canvasZoom);
                if (std::abs(delta.x) > 0.001F || std::abs(delta.y) > 0.001F)
                {
                    dragMoved = true;
                }

                glm::vec2 newOffset = dragStartOffset + delta;
                if (m_snapToGrid)
                {
                    SnapValue(newOffset.x);
                    SnapValue(newOffset.y);
                }

                // Align/snap against nearby element edges and centers.
                const float threshold = 6.0F / std::max(0.2F, m_canvasZoom);
                const float startLeft = dragStartRect.x;
                const float startTop = dragStartRect.y;
                const float width = dragStartRect.w;
                const float height = dragStartRect.h;
                const float movedLeft = startLeft + (newOffset.x - dragStartOffset.x);
                const float movedTop = startTop + (newOffset.y - dragStartOffset.y);
                const std::array<float, 3> selfX = {movedLeft, movedLeft + width * 0.5F, movedLeft + width};
                const std::array<float, 3> selfY = {movedTop, movedTop + height * 0.5F, movedTop + height};

                float bestXDiff = 0.0F;
                bool hasX = false;
                float bestYDiff = 0.0F;
                bool hasY = false;
                float snappedXLine = 0.0F;
                float snappedYLine = 0.0F;

                for (UINode* candidate : candidates)
                {
                    if (candidate == nullptr || candidate == m_draggedNode || !IsNodeVisible(*candidate))
                    {
                        continue;
                    }
                    const float cLeft = candidate->computedRect.x;
                    const float cTop = candidate->computedRect.y;
                    const float cRight = candidate->computedRect.x + candidate->computedRect.w;
                    const float cBottom = candidate->computedRect.y + candidate->computedRect.h;
                    const std::array<float, 3> candX = {cLeft, cLeft + candidate->computedRect.w * 0.5F, cRight};
                    const std::array<float, 3> candY = {cTop, cTop + candidate->computedRect.h * 0.5F, cBottom};

                    for (float sx : selfX)
                    {
                        for (float cx : candX)
                        {
                            const float diff = cx - sx;
                            if (std::abs(diff) <= threshold && (!hasX || std::abs(diff) < std::abs(bestXDiff)))
                            {
                                hasX = true;
                                bestXDiff = diff;
                                snappedXLine = cx;
                            }
                        }
                    }
                    for (float sy : selfY)
                    {
                        for (float cy : candY)
                        {
                            const float diff = cy - sy;
                            if (std::abs(diff) <= threshold && (!hasY || std::abs(diff) < std::abs(bestYDiff)))
                            {
                                hasY = true;
                                bestYDiff = diff;
                                snappedYLine = cy;
                            }
                        }
                    }
                }

                if (hasX)
                {
                    newOffset.x += bestXDiff;
                    snapGuides.push_back(std::array<float, 4>{
                        toScreenX(snappedXLine),
                        m_canvasScreenPos.y,
                        toScreenX(snappedXLine),
                        m_canvasScreenPos.y + m_canvasScreenSize.y
                    });
                }
                if (hasY)
                {
                    newOffset.y += bestYDiff;
                    snapGuides.push_back(std::array<float, 4>{
                        m_canvasScreenPos.x,
                        toScreenY(snappedYLine),
                        m_canvasScreenPos.x + m_canvasScreenSize.x,
                        toScreenY(snappedYLine)
                    });
                }

                m_draggedNode->layout.offset = newOffset;
                m_draggedNode->MarkLayoutDirty();
                m_tree->ComputeLayout();
            }
        }
        if (!previewMode && draggingSelection && ImGui::IsMouseReleased(ImGuiMouseButton_Left))
        {
            if (dragMoved)
            {
                PushAction("Move Node");
            }
            draggingSelection = false;
            dragMoved = false;
            m_draggedNode = nullptr;
            ImGui::ResetMouseDragDelta(ImGuiMouseButton_Left);
        }

        // Draw spacing guides for selected node to nearest neighbors.
        if (m_selectedNode && IsNodeVisible(*m_selectedNode))
        {
            const float sx1 = m_selectedNode->computedRect.x;
            const float sy1 = m_selectedNode->computedRect.y;
            const float sx2 = m_selectedNode->computedRect.x + m_selectedNode->computedRect.w;
            const float sy2 = m_selectedNode->computedRect.y + m_selectedNode->computedRect.h;
            const float sCx = (sx1 + sx2) * 0.5F;
            const float sCy = (sy1 + sy2) * 0.5F;

            const UINode* leftNode = nullptr;
            float leftGap = 1e9F;
            const UINode* rightNode = nullptr;
            float rightGap = 1e9F;
            const UINode* topNode = nullptr;
            float topGap = 1e9F;
            const UINode* bottomNode = nullptr;
            float bottomGap = 1e9F;

            for (UINode* candidate : candidates)
            {
                if (candidate == nullptr || candidate == m_selectedNode || !IsNodeVisible(*candidate))
                {
                    continue;
                }
                const float cx1 = candidate->computedRect.x;
                const float cy1 = candidate->computedRect.y;
                const float cx2 = candidate->computedRect.x + candidate->computedRect.w;
                const float cy2 = candidate->computedRect.y + candidate->computedRect.h;
                const float overlapY = std::min(sy2, cy2) - std::max(sy1, cy1);
                const float overlapX = std::min(sx2, cx2) - std::max(sx1, cx1);

                if (overlapY > 2.0F && cx2 <= sx1)
                {
                    const float gap = sx1 - cx2;
                    if (gap < leftGap)
                    {
                        leftGap = gap;
                        leftNode = candidate;
                    }
                }
                if (overlapY > 2.0F && cx1 >= sx2)
                {
                    const float gap = cx1 - sx2;
                    if (gap < rightGap)
                    {
                        rightGap = gap;
                        rightNode = candidate;
                    }
                }
                if (overlapX > 2.0F && cy2 <= sy1)
                {
                    const float gap = sy1 - cy2;
                    if (gap < topGap)
                    {
                        topGap = gap;
                        topNode = candidate;
                    }
                }
                if (overlapX > 2.0F && cy1 >= sy2)
                {
                    const float gap = cy1 - sy2;
                    if (gap < bottomGap)
                    {
                        bottomGap = gap;
                        bottomNode = candidate;
                    }
                }
            }

            auto pushGapGuide = [&](float x1, float y1, float x2, float y2, float value) {
                if (value > 0.0F && value < 1e8F)
                {
                    spacingGuides.push_back({x1, y1, x2, y2, value});
                }
            };

            if (leftNode)
            {
                const float y = std::clamp(sCy, leftNode->computedRect.y, leftNode->computedRect.y + leftNode->computedRect.h);
                pushGapGuide(leftNode->computedRect.x + leftNode->computedRect.w, y, sx1, y, leftGap);
            }
            if (rightNode)
            {
                const float y = std::clamp(sCy, rightNode->computedRect.y, rightNode->computedRect.y + rightNode->computedRect.h);
                pushGapGuide(sx2, y, rightNode->computedRect.x, y, rightGap);
            }
            if (topNode)
            {
                const float x = std::clamp(sCx, topNode->computedRect.x, topNode->computedRect.x + topNode->computedRect.w);
                pushGapGuide(x, topNode->computedRect.y + topNode->computedRect.h, x, sy1, topGap);
            }
            if (bottomNode)
            {
                const float x = std::clamp(sCx, bottomNode->computedRect.x, bottomNode->computedRect.x + bottomNode->computedRect.w);
                pushGapGuide(x, sy2, x, bottomNode->computedRect.y, bottomGap);
            }

            if (m_selectedNode->parent != nullptr && IsNodeVisible(*m_selectedNode->parent))
            {
                const UINode& parent = *m_selectedNode->parent;
                const float px1 = parent.computedRect.contentX;
                const float py1 = parent.computedRect.contentY;
                const float px2 = parent.computedRect.contentX + parent.computedRect.contentW;
                const float py2 = parent.computedRect.contentY + parent.computedRect.contentH;

                const float guideY = std::clamp(sCy, py1, py2);
                const float guideX = std::clamp(sCx, px1, px2);
                pushGapGuide(px1, guideY, sx1, guideY, sx1 - px1);
                pushGapGuide(sx2, guideY, px2, guideY, px2 - sx2);
                pushGapGuide(guideX, py1, guideX, sy1, sy1 - py1);
                pushGapGuide(guideX, sy2, guideX, py2, py2 - sy2);
            }
        }

        if (highlightedContainer != nullptr)
        {
            const float bx1 = toScreenX(highlightedContainer->computedRect.contentX);
            const float by1 = toScreenY(highlightedContainer->computedRect.contentY);
            const float bx2 = toScreenX(highlightedContainer->computedRect.contentX + highlightedContainer->computedRect.contentW);
            const float by2 = toScreenY(highlightedContainer->computedRect.contentY + highlightedContainer->computedRect.contentH);
            drawList->AddRectFilled(ImVec2(bx1, by1), ImVec2(bx2, by2), IM_COL32(90, 168, 255, 30));
            drawList->AddRect(ImVec2(bx1, by1), ImVec2(bx2, by2), IM_COL32(96, 182, 255, 225), 2.0F, 0, 2.0F);
        }

        for (const auto& guide : snapGuides)
        {
            drawList->AddLine(ImVec2(guide[0], guide[1]), ImVec2(guide[2], guide[3]), IM_COL32(108, 196, 255, 210), 1.5F);
        }
        for (const auto& gap : spacingGuides)
        {
            const ImVec2 p1(toScreenX(gap.x1), toScreenY(gap.y1));
            const ImVec2 p2(toScreenX(gap.x2), toScreenY(gap.y2));
            drawList->AddLine(p1, p2, IM_COL32(255, 199, 78, 210), 1.3F);
            const ImVec2 mid((p1.x + p2.x) * 0.5F, (p1.y + p2.y) * 0.5F);
            const std::string valueText = std::to_string(static_cast<int>(std::round(gap.value))) + "px";
            drawList->AddText(ImVec2(mid.x + 4.0F, mid.y + 2.0F), IM_COL32(255, 224, 120, 230), valueText.c_str());
        }
    }

    drawList->AddText(
        ImVec2(m_canvasScreenPos.x + 10.0F, m_canvasScreenPos.y + 10.0F),
        m_snapToGrid ? IM_COL32(136, 230, 160, 220) : IM_COL32(255, 120, 120, 220),
        m_snapToGrid ? "Snap ON" : "Snap OFF"
    );

    drawList->PopClipRect();
}

void UiEditor::RenderCanvasGrid()
{
    if (m_gridSize <= 0.0F)
        return;

    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const ImU32 minorColor = IM_COL32(42, 42, 48, 255);
    const ImU32 majorColor = IM_COL32(64, 64, 74, 255);

    const float spacing = std::max(2.0F, m_gridSize * m_canvasZoom);
    const float offsetX = std::fmod(m_canvasPan.x * m_canvasZoom, spacing);
    const float offsetY = std::fmod(m_canvasPan.y * m_canvasZoom, spacing);

    int majorCounterX = 0;
    for (float x = offsetX; x < m_canvasScreenSize.x; x += spacing)
    {
        const ImU32 color = (majorCounterX % 8 == 0) ? majorColor : minorColor;
        drawList->AddLine(
            ImVec2(m_canvasScreenPos.x + x, m_canvasScreenPos.y),
            ImVec2(m_canvasScreenPos.x + x, m_canvasScreenPos.y + m_canvasScreenSize.y),
            color
        );
        ++majorCounterX;
    }
    int majorCounterY = 0;
    for (float y = offsetY; y < m_canvasScreenSize.y; y += spacing)
    {
        const ImU32 color = (majorCounterY % 8 == 0) ? majorColor : minorColor;
        drawList->AddLine(
            ImVec2(m_canvasScreenPos.x, m_canvasScreenPos.y + y),
            ImVec2(m_canvasScreenPos.x + m_canvasScreenSize.x, m_canvasScreenPos.y + y),
            color
        );
        ++majorCounterY;
    }

    if (m_tree && m_tree->GetRoot())
    {
        const UINode* root = m_tree->GetRoot();
        const ImVec2 min(
            m_canvasScreenPos.x + (root->computedRect.x + m_canvasPan.x) * m_canvasZoom,
            m_canvasScreenPos.y + (root->computedRect.y + m_canvasPan.y) * m_canvasZoom
        );
        const ImVec2 max(min.x + root->computedRect.w * m_canvasZoom, min.y + root->computedRect.h * m_canvasZoom);
        drawList->AddRectFilled(min, max, IM_COL32(28, 30, 36, 200));
        drawList->AddRect(min, max, IM_COL32(100, 110, 125, 255), 6.0F, 0, 1.0F);
    }
}

void UiEditor::RenderCanvasNode(UINode& node)
{
    if (!IsNodeVisible(node))
        return;

    ImDrawList* drawList = ImGui::GetWindowDrawList();
    ImVec2 canvasPos = m_canvasScreenPos;

    const float x = canvasPos.x + (node.computedRect.x + m_canvasPan.x) * m_canvasZoom;
    const float y = canvasPos.y + (node.computedRect.y + m_canvasPan.y) * m_canvasZoom;
    const float w = std::max(1.0F, node.computedRect.w * m_canvasZoom);
    const float h = std::max(1.0F, node.computedRect.h * m_canvasZoom);

    const float centerX = x + w * 0.5F + node.transformTranslate.x * m_canvasZoom;
    const float centerY = y + h * 0.5F + node.transformTranslate.y * m_canvasZoom;
    const float halfW = std::max(0.5F, w * node.transformScale.x * 0.5F);
    const float halfH = std::max(0.5F, h * node.transformScale.y * 0.5F);
    const float radians = glm::radians(node.transformRotationDeg);
    const float c = std::cos(radians);
    const float s = std::sin(radians);
    auto rotatePoint = [&](float lx, float ly) {
        return ImVec2(centerX + lx * c - ly * s, centerY + lx * s + ly * c);
    };

    const ImVec2 p0 = rotatePoint(-halfW, -halfH);
    const ImVec2 p1 = rotatePoint(halfW, -halfH);
    const ImVec2 p2 = rotatePoint(halfW, halfH);
    const ImVec2 p3 = rotatePoint(-halfW, halfH);
    const bool canDrawRoundedRect = std::abs(node.transformRotationDeg) <= 0.01F;
    const float scaledRadius = std::max(
        0.0F,
        node.computedRadius * m_canvasZoom * std::max(std::abs(node.transformScale.x), std::abs(node.transformScale.y)));
    const float roundedRadius = std::min(scaledRadius, std::max(0.0F, std::min(halfW, halfH) - 0.5F));
    const ImVec2 axisMin(centerX - halfW, centerY - halfH);
    const ImVec2 axisMax(centerX + halfW, centerY + halfH);

    const ImU32 bgColor = ImGui::ColorConvertFloat4ToU32(ImVec4(
        node.computedBackgroundColor.r,
        node.computedBackgroundColor.g,
        node.computedBackgroundColor.b,
        node.computedBackgroundColor.a * node.computedOpacity
    ));
    const bool hasBackgroundFill = node.computedBackgroundColor.a * node.computedOpacity > 0.001F;
    if (hasBackgroundFill)
    {
        if (node.type == UINodeType::Shape)
        {
            if (node.shapeType == UIShapeType::Circle)
            {
                const float radius = std::max(1.0F, std::min(halfW, halfH));
                drawList->AddCircleFilled(ImVec2(centerX, centerY), radius, bgColor, 48);
            }
            else if (node.shapeType == UIShapeType::Rectangle)
            {
                drawList->AddQuadFilled(p0, p1, p2, p3, bgColor);
            }
        }
        else
        {
            if (canDrawRoundedRect && roundedRadius > 0.5F)
            {
                drawList->AddRectFilled(axisMin, axisMax, bgColor, roundedRadius);
            }
            else
            {
                drawList->AddQuadFilled(p0, p1, p2, p3, bgColor);
            }
        }
    }

    const ImU32 strokeColor = ImGui::ColorConvertFloat4ToU32(ImVec4(
        node.computedStrokeColor.r,
        node.computedStrokeColor.g,
        node.computedStrokeColor.b,
        node.computedStrokeColor.a * node.computedOpacity
    ));
    if (node.computedStrokeWidth > 0.001F
        && node.computedStrokeColor.a > 0.001F
        && (node.type != UINodeType::Shape || node.shapeType == UIShapeType::Rectangle || node.shapeType == UIShapeType::Circle))
    {
        const float stroke = std::max(1.0F, node.computedStrokeWidth * m_canvasZoom);
        if (node.type == UINodeType::Shape && node.shapeType == UIShapeType::Circle)
        {
            const float radius = std::max(1.0F, std::min(halfW, halfH));
            drawList->AddCircle(ImVec2(centerX, centerY), radius, strokeColor, 48, stroke);
        }
        else
        {
            if (node.type != UINodeType::Shape && canDrawRoundedRect && roundedRadius > 0.5F)
            {
                drawList->AddRect(axisMin, axisMax, strokeColor, roundedRadius, 0, stroke);
            }
            else
            {
                drawList->AddQuad(p0, p1, p2, p3, strokeColor, stroke);
            }
        }
    }

    if (node.layout.display == Display::Grid && node.computedRect.contentW > 1.0F && node.computedRect.contentH > 1.0F)
    {
        int flowCount = 0;
        int explicitRowsNeeded = 0;
        for (const auto& child : node.children)
        {
            if (!child)
            {
                continue;
            }
            if (child->visibility == Visibility::Collapsed || child->layout.display == Display::None || child->layout.position == Position::Absolute)
            {
                continue;
            }
            ++flowCount;
            if (child->layout.gridRowStart > 0)
            {
                explicitRowsNeeded = std::max(explicitRowsNeeded, child->layout.gridRowStart - 1 + std::max(1, child->layout.gridRowSpan));
            }
        }

        int templateRows = 0;
        int templateColumns = 0;
        std::unordered_map<std::string, glm::ivec4> templateAreas;
        if (!node.layout.gridTemplateAreas.empty())
        {
            std::vector<std::vector<std::string>> rowsTokens;
            std::istringstream lineStream(node.layout.gridTemplateAreas);
            std::string line;
            while (std::getline(lineStream, line))
            {
                std::istringstream tokenStream(line);
                std::vector<std::string> tokens;
                std::string token;
                while (tokenStream >> token)
                {
                    tokens.push_back(token);
                }
                if (!tokens.empty())
                {
                    templateColumns = std::max(templateColumns, static_cast<int>(tokens.size()));
                    rowsTokens.push_back(tokens);
                }
            }
            templateRows = static_cast<int>(rowsTokens.size());
            struct Extent
            {
                int minCol = std::numeric_limits<int>::max();
                int minRow = std::numeric_limits<int>::max();
                int maxCol = std::numeric_limits<int>::min();
                int maxRow = std::numeric_limits<int>::min();
            };
            std::unordered_map<std::string, Extent> extents;
            for (int r = 0; r < static_cast<int>(rowsTokens.size()); ++r)
            {
                const auto& tokens = rowsTokens[static_cast<std::size_t>(r)];
                for (int c = 0; c < static_cast<int>(tokens.size()); ++c)
                {
                    const std::string& area = tokens[static_cast<std::size_t>(c)];
                    if (area.empty() || area == ".")
                    {
                        continue;
                    }
                    auto& ex = extents[area];
                    ex.minCol = std::min(ex.minCol, c);
                    ex.minRow = std::min(ex.minRow, r);
                    ex.maxCol = std::max(ex.maxCol, c);
                    ex.maxRow = std::max(ex.maxRow, r);
                }
            }
            for (const auto& [name, ex] : extents)
            {
                if (ex.minCol > ex.maxCol || ex.minRow > ex.maxRow)
                {
                    continue;
                }
                templateAreas[name] = glm::ivec4(ex.minCol, ex.minRow, ex.maxCol - ex.minCol + 1, ex.maxRow - ex.minRow + 1);
            }
        }

        for (const auto& child : node.children)
        {
            if (!child || child->layout.gridArea.empty())
            {
                continue;
            }
            auto areaIt = templateAreas.find(child->layout.gridArea);
            if (areaIt != templateAreas.end())
            {
                explicitRowsNeeded = std::max(explicitRowsNeeded, areaIt->second.y + areaIt->second.w);
            }
        }

        const int columns = std::max(1, std::max(node.layout.gridColumns, templateColumns));
        const int rows = std::max(1, node.layout.gridRows > 0
            ? node.layout.gridRows
            : std::max((flowCount + columns - 1) / columns, std::max(templateRows, explicitRowsNeeded)));
        const float colGap = node.layout.gridColumnGap >= 0.0F ? node.layout.gridColumnGap : node.layout.gap;
        const float rowGap = node.layout.gridRowGap >= 0.0F ? node.layout.gridRowGap : node.layout.gap;

        auto resolveTrack = [&](const SizeValue& value, float reference, float fallback) {
            switch (value.unit)
            {
                case SizeValue::Unit::Px:
                    return value.value;
                case SizeValue::Unit::Percent:
                    return reference * value.value / 100.0F;
                case SizeValue::Unit::Auto:
                default:
                    return fallback;
            }
        };

        const float defaultCellW = std::max(0.0F, (node.computedRect.contentW - colGap * static_cast<float>(std::max(0, columns - 1))) / static_cast<float>(columns));
        const float defaultCellH = std::max(0.0F, (node.computedRect.contentH - rowGap * static_cast<float>(std::max(0, rows - 1))) / static_cast<float>(rows));
        const float cellW = std::max(0.0F, resolveTrack(node.layout.gridColumnSize, node.computedRect.contentW, defaultCellW));
        const float cellH = std::max(0.0F, resolveTrack(node.layout.gridRowSize, node.computedRect.contentH, defaultCellH));
        const float gridW = cellW * static_cast<float>(columns) + colGap * static_cast<float>(std::max(0, columns - 1));
        const float gridH = cellH * static_cast<float>(rows) + rowGap * static_cast<float>(std::max(0, rows - 1));
        float gridOffsetX = 0.0F;
        float gridOffsetY = 0.0F;
        if (node.layout.justifyContent == JustifyContent::Center)
        {
            gridOffsetX = std::max(0.0F, (node.computedRect.contentW - gridW) * 0.5F);
        }
        else if (node.layout.justifyContent == JustifyContent::FlexEnd)
        {
            gridOffsetX = std::max(0.0F, node.computedRect.contentW - gridW);
        }
        if (node.layout.alignItems == AlignItems::Center)
        {
            gridOffsetY = std::max(0.0F, (node.computedRect.contentH - gridH) * 0.5F);
        }
        else if (node.layout.alignItems == AlignItems::FlexEnd)
        {
            gridOffsetY = std::max(0.0F, node.computedRect.contentH - gridH);
        }

        const float contentX = canvasPos.x + (node.computedRect.contentX + m_canvasPan.x + gridOffsetX) * m_canvasZoom;
        const float contentY = canvasPos.y + (node.computedRect.contentY + m_canvasPan.y + gridOffsetY) * m_canvasZoom;
        const float cellWS = cellW * m_canvasZoom;
        const float cellHS = cellH * m_canvasZoom;
        const float colGapS = colGap * m_canvasZoom;
        const float rowGapS = rowGap * m_canvasZoom;
        const ImU32 gridLineColor = IM_COL32(120, 200, 255, 110);
        const ImU32 gapColor = IM_COL32(82, 160, 220, 38);

        float currentX = contentX;
        for (int cidx = 0; cidx <= columns; ++cidx)
        {
            drawList->AddLine(ImVec2(currentX, contentY), ImVec2(currentX, contentY + gridH * m_canvasZoom), gridLineColor, 1.0F);
            if (cidx < columns)
            {
                currentX += cellWS;
                if (cidx < columns - 1 && colGapS > 0.5F)
                {
                    drawList->AddRectFilled(ImVec2(currentX, contentY), ImVec2(currentX + colGapS, contentY + gridH * m_canvasZoom), gapColor);
                    currentX += colGapS;
                }
            }
        }

        float currentY = contentY;
        for (int ridx = 0; ridx <= rows; ++ridx)
        {
            drawList->AddLine(ImVec2(contentX, currentY), ImVec2(contentX + gridW * m_canvasZoom, currentY), gridLineColor, 1.0F);
            if (ridx < rows)
            {
                currentY += cellHS;
                if (ridx < rows - 1 && rowGapS > 0.5F)
                {
                    drawList->AddRectFilled(ImVec2(contentX, currentY), ImVec2(contentX + gridW * m_canvasZoom, currentY + rowGapS), gapColor);
                    currentY += rowGapS;
                }
            }
        }
    }

    if (node.type == UINodeType::Shape)
    {
        if (node.shapeType == UIShapeType::Line)
        {
            const ImVec2 lineStart = rotatePoint(-halfW, -halfH);
            const float ex = -halfW + node.shapeLineEnd.x * node.transformScale.x * m_canvasZoom;
            const float ey = -halfH + node.shapeLineEnd.y * node.transformScale.y * m_canvasZoom;
            const ImVec2 lineEnd = rotatePoint(ex, ey);
            const float thickness = std::max(1.0F, node.computedStrokeWidth > 0.0F ? node.computedStrokeWidth * m_canvasZoom : 2.0F);
            drawList->AddLine(lineStart, lineEnd, strokeColor, thickness);
        }
    }

    if ((node.type == UINodeType::Text || node.type == UINodeType::Button || node.type == UINodeType::TextInput) && !node.text.empty())
    {
        ImVec4 textColorVec(
            node.computedTextColor.r,
            node.computedTextColor.g,
            node.computedTextColor.b,
            node.computedTextColor.a * node.computedOpacity);
        textColorVec.w = std::clamp(textColorVec.w * FontWeightAlphaMultiplier(node.computedFont.weight), 0.0F, 1.0F);
        const ImU32 textColor = ImGui::ColorConvertFloat4ToU32(textColorVec);

        const float fontPixelSize = ComputeCanvasFontSize(node);
        ImFont* font = ResolveNodeFont(node);
        if (font == nullptr)
        {
            font = ImGui::GetFont();
        }
        const float transformScale = std::max(0.25F, std::max(std::abs(node.transformScale.x), std::abs(node.transformScale.y)));
        const float letterSpacing = node.computedFont.letterSpacing * m_canvasZoom * transformScale;
        const float lineHeight = std::max(1.0F, font->CalcTextSizeA(fontPixelSize, FLT_MAX, 0.0F, "Ag").y);

        std::vector<std::string> lines;
        std::size_t lineStart = 0;
        while (lineStart <= node.text.size())
        {
            const std::size_t lineEnd = node.text.find('\n', lineStart);
            const std::size_t lineLen = (lineEnd == std::string::npos) ? (node.text.size() - lineStart) : (lineEnd - lineStart);
            lines.push_back(node.text.substr(lineStart, lineLen));
            if (lineEnd == std::string::npos)
            {
                break;
            }
            lineStart = lineEnd + 1;
        }
        if (lines.empty())
        {
            lines.emplace_back();
        }

        const auto lineWidth = [&](const std::string& line) {
            if (line.empty())
            {
                return 0.0F;
            }
            if (std::abs(letterSpacing) < 0.001F)
            {
                return font->CalcTextSizeA(fontPixelSize, FLT_MAX, 0.0F, line.c_str()).x;
            }
            float width = 0.0F;
            for (std::size_t i = 0; i < line.size(); ++i)
            {
                const char glyph[2] = {line[i], '\0'};
                width += font->CalcTextSizeA(fontPixelSize, FLT_MAX, 0.0F, glyph).x;
                if (i + 1 < line.size())
                {
                    width += letterSpacing;
                }
            }
            return width;
        };
        const auto drawLineText = [&](const std::string& line, const ImVec2& pos, ImU32 color) {
            if (line.empty())
            {
                return;
            }
            if (std::abs(letterSpacing) < 0.001F)
            {
                drawList->AddText(font, fontPixelSize, pos, color, line.c_str());
                return;
            }
            float penX = pos.x;
            for (std::size_t i = 0; i < line.size(); ++i)
            {
                const char glyph[2] = {line[i], '\0'};
                drawList->AddText(font, fontPixelSize, ImVec2(penX, pos.y), color, glyph);
                penX += font->CalcTextSizeA(fontPixelSize, FLT_MAX, 0.0F, glyph).x;
                if (i + 1 < line.size())
                {
                    penX += letterSpacing;
                }
            }
        };

        const float blockHeight = lineHeight * static_cast<float>(lines.size());
        const float contentW = node.computedRect.contentW * m_canvasZoom * std::max(0.01F, std::abs(node.transformScale.x));
        const float contentLeft = centerX - contentW * 0.5F;
        const float baseY = centerY - blockHeight * 0.5F;

        for (std::size_t i = 0; i < lines.size(); ++i)
        {
            const float currentY = baseY + static_cast<float>(i) * lineHeight;
            const float currentW = lineWidth(lines[i]);
            float lineX = contentLeft + (contentW - currentW) * 0.5F;
            switch (node.computedFont.align)
            {
                case FontProps::Align::Left:
                    lineX = contentLeft;
                    break;
                case FontProps::Align::Right:
                    lineX = contentLeft + contentW - currentW;
                    break;
                case FontProps::Align::Center:
                default:
                    break;
            }
            const ImVec2 linePos(lineX, currentY);
            drawLineText(lines[i], linePos, textColor);

            int extraPasses = FontWeightExtraPasses(node.computedFont.weight);
            if (fontPixelSize > 48.0F)
            {
                extraPasses = std::max(0, extraPasses - 1);
            }
            if (fontPixelSize > 78.0F)
            {
                extraPasses = std::max(0, extraPasses - 1);
            }
            for (int pass = 0; pass < extraPasses; ++pass)
            {
                const float passOffset = std::max(0.45F, lineHeight * (0.028F + static_cast<float>(pass) * 0.009F));
                drawLineText(lines[i], ImVec2(linePos.x + passOffset, linePos.y), textColor);
            }

            if (node.computedFont.underline || node.computedFont.strikethrough)
            {
                const float decoThickness = std::max(1.0F, m_canvasZoom * (0.85F + static_cast<float>(extraPasses) * 0.2F));
                if (node.computedFont.underline)
                {
                    const float underlineY = linePos.y + lineHeight * 0.90F;
                    drawList->AddLine(ImVec2(linePos.x, underlineY), ImVec2(linePos.x + currentW, underlineY), textColor, decoThickness);
                }
                if (node.computedFont.strikethrough)
                {
                    const float strikeY = linePos.y + lineHeight * 0.54F;
                    drawList->AddLine(ImVec2(linePos.x, strikeY), ImVec2(linePos.x + currentW, strikeY), textColor, decoThickness);
                }
            }
        }
    }
    else if (node.type == UINodeType::Image)
    {
        const std::string label = node.imageSource.empty() ? "[image]" : node.imageSource;
        const ImU32 iconColor = IM_COL32(220, 220, 235, 210);
        drawList->AddText(ImVec2(centerX - 20.0F, centerY - 8.0F), iconColor, "IMG");
        if (!label.empty())
        {
            std::string shortLabel = label;
            if (shortLabel.size() > 28)
            {
                shortLabel = "..." + shortLabel.substr(shortLabel.size() - 25);
            }
            drawList->AddText(ImVec2(centerX - halfW + 4.0F, centerY + halfH - 16.0F), IM_COL32(170, 180, 195, 220), shortLabel.c_str());
        }
    }

    if (m_selectedNode == &node)
    {
        drawList->AddQuad(p0, p1, p2, p3, IM_COL32(100, 150, 255, 255), 2.0F);
    }

    // Render children
    for (const auto& child : node.children)
    {
        if (child)
        {
            RenderCanvasNode(*child);
        }
    }
}

void UiEditor::HandleCanvasInteraction(UINode& node)
{
    (void)node;
}

void UiEditor::RenderInspectorPanel()
{
    ImGui::Text("Inspector");
    ImGui::Separator();

    if (!m_selectedNode)
    {
        ImGui::TextDisabled("No node selected");
        return;
    }

    RenderInspectorNodeProperties();
    ImGui::Separator();
    RenderInspectorLayout();
    ImGui::Separator();
    RenderInspectorStyle();
    ImGui::Separator();
    RenderInspectorInteractions();
}

void UiEditor::RenderInspectorNodeProperties()
{
    ImGui::Text("Node");

    // ID
    char idBuf[128];
    std::strncpy(idBuf, m_selectedNode->id.c_str(), sizeof(idBuf) - 1);
    idBuf[sizeof(idBuf) - 1] = '\0';
    if (ImGui::InputText("ID", idBuf, sizeof(idBuf)))
    {
        m_selectedNode->id = idBuf;
        if (m_tree)
        {
            m_tree->RebuildNodeIndex();
        }
        m_hasUnsavedChanges = true;
    }

    // Name
    char nameBuf[128];
    std::strncpy(nameBuf, m_selectedNode->name.c_str(), sizeof(nameBuf) - 1);
    nameBuf[sizeof(nameBuf) - 1] = '\0';
    if (ImGui::InputText("Name", nameBuf, sizeof(nameBuf)))
    {
        m_selectedNode->name = nameBuf;
        m_hasUnsavedChanges = true;
    }

    // Type (read-only)
    const std::string nodeTypeLabel = NodeTypeToString(m_selectedNode->type);
    ImGui::Text("Type: %s", nodeTypeLabel.c_str());

    // Visibility
    int vis = static_cast<int>(m_selectedNode->visibility);
    const char* visOptions[] = {"Visible", "Hidden", "Collapsed"};
    if (ImGui::Combo("Visibility", &vis, visOptions, 3))
    {
        m_selectedNode->visibility = static_cast<Visibility>(vis);
        m_selectedNode->MarkLayoutDirty();
        m_hasUnsavedChanges = true;
    }

    // Z-Index
    int zIndex = m_selectedNode->zIndex;
    if (ImGui::InputInt("Z-Index", &zIndex))
    {
        m_selectedNode->zIndex = zIndex;
        m_hasUnsavedChanges = true;
    }

    // Text content (for text-based nodes)
    if (m_selectedNode->type == UINodeType::Text || m_selectedNode->type == UINodeType::Button || m_selectedNode->type == UINodeType::TextInput)
    {
        char textBuf[256];
        std::strncpy(textBuf, m_selectedNode->text.c_str(), sizeof(textBuf) - 1);
        textBuf[sizeof(textBuf) - 1] = '\0';
        if (ImGui::InputText("Text", textBuf, sizeof(textBuf)))
        {
            m_selectedNode->text = textBuf;
            if (m_selectedNode->type == UINodeType::TextInput)
            {
                m_selectedNode->state.text = m_selectedNode->text;
            }
            m_selectedNode->MarkLayoutDirty();
            m_hasUnsavedChanges = true;
        }
    }

    if (m_selectedNode->type == UINodeType::Image)
    {
        char imageBuf[512];
        std::strncpy(imageBuf, m_selectedNode->imageSource.c_str(), sizeof(imageBuf) - 1);
        imageBuf[sizeof(imageBuf) - 1] = '\0';
        if (ImGui::InputText("Image Source", imageBuf, sizeof(imageBuf)))
        {
            m_selectedNode->imageSource = imageBuf;
            m_selectedNode->MarkStyleDirty();
            m_hasUnsavedChanges = true;
        }
    }

    if (m_selectedNode->type == UINodeType::Shape)
    {
        int shapeType = static_cast<int>(m_selectedNode->shapeType);
        const char* shapeOptions[] = {"Rectangle", "Circle", "Line"};
        if (ImGui::Combo("Shape Type", &shapeType, shapeOptions, 3))
        {
            m_selectedNode->shapeType = static_cast<UIShapeType>(shapeType);
            m_selectedNode->MarkStyleDirty();
            m_hasUnsavedChanges = true;
        }

        if (m_selectedNode->shapeType == UIShapeType::Line)
        {
            float lineEnd[2] = {m_selectedNode->shapeLineEnd.x, m_selectedNode->shapeLineEnd.y};
            if (ImGui::DragFloat2("Line End", lineEnd, 1.0F, -2000.0F, 2000.0F))
            {
                m_selectedNode->shapeLineEnd = glm::vec2(lineEnd[0], lineEnd[1]);
                m_selectedNode->MarkLayoutDirty();
                m_hasUnsavedChanges = true;
            }
        }
    }
}

void UiEditor::RenderInspectorLayout()
{
    ImGui::Text("Layout");

    LayoutProps& layout = m_selectedNode->layout;
    bool changed = false;
    bool widthChanged = false;
    bool heightChanged = false;
    const float beforeWidth = layout.width.IsFixed() ? layout.width.value : std::max(1.0F, m_selectedNode->computedRect.w);
    const float beforeHeight = layout.height.IsFixed() ? layout.height.value : std::max(1.0F, m_selectedNode->computedRect.h);
    const bool parentAutoLayout = m_selectedNode->parent != nullptr
        && (m_selectedNode->parent->layout.display == Display::Flex || m_selectedNode->parent->layout.display == Display::Grid);

    if (parentAutoLayout && layout.position == Position::Absolute)
    {
        layout.position = Position::Relative;
        layout.anchor.reset();
        layout.offset = glm::vec2(0.0F, 0.0F);
        layout.pivot = glm::vec2(0.5F, 0.5F);
        changed = true;
    }

    float transformTranslate[2] = {m_selectedNode->transformTranslate.x, m_selectedNode->transformTranslate.y};
    if (ImGui::DragFloat2("Translate", transformTranslate, 0.5F, -4000.0F, 4000.0F))
    {
        m_selectedNode->transformTranslate = glm::vec2(transformTranslate[0], transformTranslate[1]);
        changed = true;
    }
    float transformScale[2] = {m_selectedNode->transformScale.x, m_selectedNode->transformScale.y};
    if (ImGui::DragFloat2("Scale", transformScale, 0.01F, 0.01F, 20.0F))
    {
        m_selectedNode->transformScale = glm::vec2(
            std::max(0.01F, transformScale[0]),
            std::max(0.01F, transformScale[1]));
        changed = true;
    }
    float rotation = m_selectedNode->transformRotationDeg;
    if (ImGui::DragFloat("Rotation", &rotation, 0.5F, -360.0F, 360.0F, "%.1f deg"))
    {
        m_selectedNode->transformRotationDeg = rotation;
        changed = true;
    }

    float rectData[4] = {
        m_selectedNode->computedRect.x,
        m_selectedNode->computedRect.y,
        m_selectedNode->computedRect.w,
        m_selectedNode->computedRect.h
    };
    ImGui::InputFloat4("Computed XYWH", rectData, "%.1f", ImGuiInputTextFlags_ReadOnly);

    // Display
    int display = static_cast<int>(layout.display);
    const char* displayOptions[] = {"Flex", "Grid", "Block", "None"};
    if (ImGui::Combo("Display", &display, displayOptions, 4))
    {
        layout.display = static_cast<Display>(display);
        changed = true;
    }

    // Position
    int pos = static_cast<int>(layout.position);
    const char* posOptions[] = {"Relative", "Absolute"};
    ImGui::BeginDisabled(parentAutoLayout);
    if (ImGui::Combo("Position", &pos, posOptions, 2))
    {
        layout.position = static_cast<Position>(pos);
        changed = true;
    }
    ImGui::EndDisabled();
    if (parentAutoLayout)
    {
        ImGui::TextDisabled("Parent Flex/Grid layout controls child placement.");
    }

    if (layout.position == Position::Absolute)
    {
        float offset[2] = {layout.offset.x, layout.offset.y};
        if (ImGui::DragFloat2("Offset", offset, 1.0F, -4000.0F, 4000.0F))
        {
            layout.offset = glm::vec2(offset[0], offset[1]);
            changed = true;
        }

        glm::vec2 anchor = layout.anchor.value_or(glm::vec2(0.0F, 0.0F));
        float anchorVals[2] = {anchor.x, anchor.y};
        if (ImGui::SliderFloat2("Anchor", anchorVals, 0.0F, 1.0F, "%.2f"))
        {
            layout.anchor = glm::vec2(anchorVals[0], anchorVals[1]);
            changed = true;
        }

        float pivot[2] = {layout.pivot.x, layout.pivot.y};
        if (ImGui::SliderFloat2("Pivot", pivot, 0.0F, 1.0F, "%.2f"))
        {
            layout.pivot = glm::vec2(pivot[0], pivot[1]);
            changed = true;
        }
    }

    // Container layout
    if (layout.display == Display::Flex)
    {
        int dir = static_cast<int>(layout.flexDirection);
        const char* dirOptions[] = {"Row", "Column", "Row Reverse", "Column Reverse"};
        if (ImGui::Combo("Direction", &dir, dirOptions, 4))
        {
            layout.flexDirection = static_cast<FlexDirection>(dir);
            changed = true;
        }

        int justify = static_cast<int>(layout.justifyContent);
        const char* justifyOptions[] = {"Start", "End", "Center", "SpaceBetween", "SpaceAround", "SpaceEvenly"};
        if (ImGui::Combo("Justify", &justify, justifyOptions, 6))
        {
            layout.justifyContent = static_cast<JustifyContent>(justify);
            changed = true;
        }

        int align = static_cast<int>(layout.alignItems);
        const char* alignOptions[] = {"Start", "End", "Center", "Stretch", "Baseline"};
        if (ImGui::Combo("Align", &align, alignOptions, 5))
        {
            layout.alignItems = static_cast<AlignItems>(align);
            changed = true;
        }

        // Gap
        float gap = layout.gap;
        if (ImGui::DragFloat("Gap", &gap, 1.0F, 0.0F, 100.0F))
        {
            layout.gap = gap;
            changed = true;
        }

        // Flex Grow
        float grow = layout.flexGrow;
        if (ImGui::DragFloat("Flex Grow", &grow, 0.1F, 0.0F, 10.0F))
        {
            layout.flexGrow = grow;
            changed = true;
        }
        float shrink = layout.flexShrink;
        if (ImGui::DragFloat("Flex Shrink", &shrink, 0.1F, 0.0F, 10.0F))
        {
            layout.flexShrink = shrink;
            changed = true;
        }
    }
    else if (layout.display == Display::Grid)
    {
        int columns = std::max(1, layout.gridColumns);
        if (ImGui::DragInt("Grid Columns", &columns, 1.0F, 1, 24))
        {
            layout.gridColumns = std::max(1, columns);
            changed = true;
        }
        int rows = std::max(0, layout.gridRows);
        if (ImGui::DragInt("Grid Rows (0=Auto)", &rows, 1.0F, 0, 24))
        {
            layout.gridRows = std::max(0, rows);
            changed = true;
        }

        auto editGridTrackSize = [&](const char* label, SizeValue& value) {
            bool localChanged = false;
            float current = value.value;
            int unit = SizeUnitToIndex(value.unit);
            ImGui::PushID(label);
            ImGui::SetNextItemWidth(110.0F);
            if (ImGui::DragFloat("##value", &current, 1.0F, -4000.0F, 4000.0F, "%.2f"))
            {
                value.value = current;
                localChanged = true;
            }
            ImGui::SameLine();
            ImGui::SetNextItemWidth(120.0F);
            const char* unitNames[] = {"Auto", "Px", "%", "Vw", "Vh"};
            if (ImGui::Combo("##unit", &unit, unitNames, 5))
            {
                value.unit = SizeUnitFromIndex(unit);
                localChanged = true;
            }
            ImGui::SameLine();
            ImGui::TextUnformatted(label);
            ImGui::PopID();
            return localChanged;
        };
        if (editGridTrackSize("Column Size", layout.gridColumnSize))
        {
            changed = true;
        }
        if (editGridTrackSize("Row Size", layout.gridRowSize))
        {
            changed = true;
        }

        float baseGap = layout.gap;
        if (ImGui::DragFloat("Grid Gap (Fallback)", &baseGap, 1.0F, 0.0F, 200.0F))
        {
            layout.gap = baseGap;
            changed = true;
        }
        float columnGap = layout.gridColumnGap;
        if (ImGui::DragFloat("Column Gap (-1 auto)", &columnGap, 1.0F, -1.0F, 200.0F))
        {
            layout.gridColumnGap = columnGap < 0.0F ? -1.0F : columnGap;
            changed = true;
        }
        float rowGap = layout.gridRowGap;
        if (ImGui::DragFloat("Row Gap (-1 auto)", &rowGap, 1.0F, -1.0F, 200.0F))
        {
            layout.gridRowGap = rowGap < 0.0F ? -1.0F : rowGap;
            changed = true;
        }

        int justifyItems = static_cast<int>(layout.gridJustifyItems);
        const char* gridAlignOptions[] = {"Start", "End", "Center", "Stretch"};
        if (ImGui::Combo("Grid Item X", &justifyItems, gridAlignOptions, 4))
        {
            layout.gridJustifyItems = static_cast<GridItemAlign>(justifyItems);
            changed = true;
        }

        int alignItemsGrid = static_cast<int>(layout.gridAlignItems);
        if (ImGui::Combo("Grid Item Y", &alignItemsGrid, gridAlignOptions, 4))
        {
            layout.gridAlignItems = static_cast<GridItemAlign>(alignItemsGrid);
            changed = true;
        }

        char templateAreasBuf[2048];
        std::strncpy(templateAreasBuf, layout.gridTemplateAreas.c_str(), sizeof(templateAreasBuf) - 1);
        templateAreasBuf[sizeof(templateAreasBuf) - 1] = '\0';
        if (ImGui::InputTextMultiline(
                "Template Areas",
                templateAreasBuf,
                sizeof(templateAreasBuf),
                ImVec2(-FLT_MIN, ImGui::GetTextLineHeight() * 4.5F)))
        {
            layout.gridTemplateAreas = templateAreasBuf;
            changed = true;
        }
        ImGui::TextDisabled("Use names per cell, e.g.: header header\\nsidebar content");
    }

    const bool parentGridLayout = m_selectedNode->parent != nullptr && m_selectedNode->parent->layout.display == Display::Grid;
    if (parentGridLayout)
    {
        ImGui::SeparatorText("Grid Item");
        char gridAreaBuf[128];
        std::strncpy(gridAreaBuf, layout.gridArea.c_str(), sizeof(gridAreaBuf) - 1);
        gridAreaBuf[sizeof(gridAreaBuf) - 1] = '\0';
        if (ImGui::InputText("Area Name", gridAreaBuf, sizeof(gridAreaBuf)))
        {
            layout.gridArea = gridAreaBuf;
            changed = true;
        }
        int colStart = std::max(0, layout.gridColumnStart);
        if (ImGui::DragInt("Column Start (0 auto)", &colStart, 1.0F, 0, 64))
        {
            layout.gridColumnStart = std::max(0, colStart);
            changed = true;
        }
        int rowStart = std::max(0, layout.gridRowStart);
        if (ImGui::DragInt("Row Start (0 auto)", &rowStart, 1.0F, 0, 64))
        {
            layout.gridRowStart = std::max(0, rowStart);
            changed = true;
        }
        int colSpan = std::max(1, layout.gridColumnSpan);
        if (ImGui::DragInt("Column Span", &colSpan, 1.0F, 1, 24))
        {
            layout.gridColumnSpan = std::max(1, colSpan);
            changed = true;
        }
        int rowSpan = std::max(1, layout.gridRowSpan);
        if (ImGui::DragInt("Row Span", &rowSpan, 1.0F, 1, 24))
        {
            layout.gridRowSpan = std::max(1, rowSpan);
            changed = true;
        }
    }

    auto editSizeValue = [&](const char* label, SizeValue& value) {
        bool localChanged = false;
        float current = value.value;
        int unit = SizeUnitToIndex(value.unit);
        ImGui::PushID(label);
        ImGui::SetNextItemWidth(110.0F);
        if (ImGui::DragFloat("##value", &current, 1.0F, -4000.0F, 4000.0F, "%.2f"))
        {
            value.value = current;
            localChanged = true;
        }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(120.0F);
        const char* unitNames[] = {"Auto", "Px", "%", "Vw", "Vh"};
        if (ImGui::Combo("##unit", &unit, unitNames, 5))
        {
            value.unit = SizeUnitFromIndex(unit);
            localChanged = true;
        }
        ImGui::SameLine();
        ImGui::TextUnformatted(label);
        ImGui::PopID();
        return localChanged;
    };

    if (editSizeValue("Width", layout.width))
    {
        changed = true;
        widthChanged = true;
    }
    if (editSizeValue("Height", layout.height))
    {
        changed = true;
        heightChanged = true;
    }
    if (editSizeValue("Min Width", layout.minWidth))
    {
        changed = true;
    }
    if (editSizeValue("Min Height", layout.minHeight))
    {
        changed = true;
    }
    if (editSizeValue("Max Width", layout.maxWidth))
    {
        changed = true;
    }
    if (editSizeValue("Max Height", layout.maxHeight))
    {
        changed = true;
    }
    if (editSizeValue("Flex Basis", layout.flexBasis))
    {
        changed = true;
    }

    int overflow = static_cast<int>(layout.overflow);
    const char* overflowOptions[] = {"Visible", "Hidden", "Scroll"};
    if (ImGui::Combo("Overflow", &overflow, overflowOptions, 3))
    {
        layout.overflow = static_cast<Overflow>(overflow);
        changed = true;
    }
    float aspect = layout.aspectRatio;
    if (ImGui::DragFloat("Aspect Ratio", &aspect, 0.05F, 0.0F, 10.0F, "%.2f"))
    {
        layout.aspectRatio = aspect;
        changed = true;
    }

    // Padding
    float padding[4] = {layout.padding.top, layout.padding.right, layout.padding.bottom, layout.padding.left};
    if (ImGui::DragFloat4("Padding TRBL", padding, 1.0F, 0.0F, 200.0F))
    {
        layout.padding.top = padding[0];
        layout.padding.right = padding[1];
        layout.padding.bottom = padding[2];
        layout.padding.left = padding[3];
        changed = true;
    }

    // Margin
    float margin[4] = {layout.margin.top, layout.margin.right, layout.margin.bottom, layout.margin.left};
    if (ImGui::DragFloat4("Margin TRBL", margin, 1.0F, -200.0F, 200.0F))
    {
        layout.margin.top = margin[0];
        layout.margin.right = margin[1];
        layout.margin.bottom = margin[2];
        layout.margin.left = margin[3];
        changed = true;
    }

    if (changed)
    {
        if (m_selectedNode->type == UINodeType::Shape
            && m_selectedNode->shapeType == UIShapeType::Line
            && (widthChanged || heightChanged))
        {
            const float afterWidth = layout.width.IsFixed() ? layout.width.value : std::max(1.0F, m_selectedNode->computedRect.w);
            const float afterHeight = layout.height.IsFixed() ? layout.height.value : std::max(1.0F, m_selectedNode->computedRect.h);
            const float sx = afterWidth / std::max(0.001F, beforeWidth);
            const float sy = afterHeight / std::max(0.001F, beforeHeight);
            m_selectedNode->shapeLineEnd = glm::vec2(
                m_selectedNode->shapeLineEnd.x * sx,
                m_selectedNode->shapeLineEnd.y * sy);
            if (m_snapToGrid)
            {
                SnapValue(m_selectedNode->shapeLineEnd.x);
                SnapValue(m_selectedNode->shapeLineEnd.y);
            }
        }
        if (m_snapToGrid && layout.position == Position::Absolute)
        {
            SnapValue(layout.offset.x);
            SnapValue(layout.offset.y);
        }
        m_selectedNode->MarkLayoutDirty();
        m_selectedNode->MarkStyleDirty();
        m_hasUnsavedChanges = true;
    }
}

void UiEditor::RenderInspectorStyle()
{
    ImGui::Text("Style");

    // Background color
    float bgColor[4] = {
        m_selectedNode->computedBackgroundColor.r,
        m_selectedNode->computedBackgroundColor.g,
        m_selectedNode->computedBackgroundColor.b,
        m_selectedNode->computedBackgroundColor.a
    };
    if (ImGui::ColorEdit4("Background", bgColor))
    {
        m_selectedNode->backgroundColor = glm::vec4(bgColor[0], bgColor[1], bgColor[2], bgColor[3]);
        m_selectedNode->computedBackgroundColor = *m_selectedNode->backgroundColor;
        m_selectedNode->MarkStyleDirty();
        m_hasUnsavedChanges = true;
    }

    // Text color
    float textColor[4] = {
        m_selectedNode->computedTextColor.r,
        m_selectedNode->computedTextColor.g,
        m_selectedNode->computedTextColor.b,
        m_selectedNode->computedTextColor.a
    };
    if (ImGui::ColorEdit4("Text Color", textColor))
    {
        m_selectedNode->textColor = glm::vec4(textColor[0], textColor[1], textColor[2], textColor[3]);
        m_selectedNode->computedTextColor = *m_selectedNode->textColor;
        m_selectedNode->MarkStyleDirty();
        m_hasUnsavedChanges = true;
    }

    const bool supportsTextFont = m_selectedNode->type == UINodeType::Text
                               || m_selectedNode->type == UINodeType::Button
                               || m_selectedNode->type == UINodeType::TextInput;
    if (supportsTextFont)
    {
        bool hasCustomFont = m_selectedNode->font.has_value();
        if (ImGui::Checkbox("Custom Font", &hasCustomFont))
        {
            if (hasCustomFont)
            {
                FontProps props = m_selectedNode->computedFont;
                if (props.size <= 0.0F)
                {
                    props.size = 16.0F;
                }
                m_selectedNode->font = props;
                m_selectedNode->computedFont = props;
            }
            else
            {
                m_selectedNode->font.reset();
            }
            m_selectedNode->MarkStyleDirty();
            m_hasUnsavedChanges = true;
        }

        if (m_selectedNode->font.has_value())
        {
            FontProps& font = *m_selectedNode->font;
            if (m_availableFontsDirty)
            {
                RefreshAvailableFonts();
            }

            int selectedFont = 0;
            for (std::size_t i = 1; i < m_availableFontPaths.size(); ++i)
            {
                if (m_availableFontPaths[i] == font.family)
                {
                    selectedFont = static_cast<int>(i);
                    break;
                }
            }
            const char* previewLabel = selectedFont >= 0 && static_cast<std::size_t>(selectedFont) < m_availableFontLabels.size()
                ? m_availableFontLabels[static_cast<std::size_t>(selectedFont)].c_str()
                : "(Select Font)";
            if (ImGui::BeginCombo("System Font", previewLabel))
            {
                for (std::size_t i = 0; i < m_availableFontPaths.size(); ++i)
                {
                    const bool isSelected = static_cast<int>(i) == selectedFont;
                    if (ImGui::Selectable(m_availableFontLabels[i].c_str(), isSelected))
                    {
                        const std::string previousFamily = font.family;
                        font.family = m_availableFontPaths[i];
                        if (!font.family.empty())
                        {
                            ImGuiIO& io = ImGui::GetIO();
                            const bool atlasLocked = io.Fonts->Locked;
                            if (EnsureEditorFontLoaded(font.family) == nullptr)
                            {
                                if (atlasLocked)
                                {
                                    ShowNotification("Font queued: it will apply when editor font atlas is available.", 2.2F, glm::vec4(0.7F, 0.9F, 1.0F, 1.0F));
                                }
                                else
                                {
                                    font.family = previousFamily;
                                    ShowNotification("Could not load this font file.", 2.0F, glm::vec4(1.0F, 0.62F, 0.35F, 1.0F));
                                }
                            }
                        }
                        m_selectedNode->computedFont = font;
                        m_selectedNode->MarkStyleDirty();
                        m_selectedNode->MarkLayoutDirty();
                        m_hasUnsavedChanges = true;
                    }
                    if (isSelected)
                    {
                        ImGui::SetItemDefaultFocus();
                    }
                }
                ImGui::EndCombo();
            }

            if (ImGui::InputText("Custom Font Path", m_customFontPathInput, sizeof(m_customFontPathInput)))
            {
                // edited path buffer
            }
            ImGui::SameLine();
            if (ImGui::Button("Add Font"))
            {
                std::string path = m_customFontPathInput;
                if (!path.empty() && std::filesystem::exists(path))
                {
                    const std::string normalized = std::filesystem::path(path).generic_string();
                    if (std::find(m_customFontPaths.begin(), m_customFontPaths.end(), normalized) == m_customFontPaths.end())
                    {
                        m_customFontPaths.push_back(normalized);
                    }
                    std::strncpy(m_customFontPathInput, "", sizeof(m_customFontPathInput));
                    m_availableFontsDirty = true;
                    RefreshAvailableFonts();
                    const std::string previousFamily = font.family;
                    font.family = normalized;
                    ImGuiIO& io = ImGui::GetIO();
                    const bool atlasLocked = io.Fonts->Locked;
                    if (EnsureEditorFontLoaded(font.family) == nullptr)
                    {
                        if (atlasLocked)
                        {
                            ShowNotification("Font queued: it will apply when editor font atlas is available.", 2.2F, glm::vec4(0.7F, 0.9F, 1.0F, 1.0F));
                        }
                        else
                        {
                            font.family = previousFamily;
                            ShowNotification("Could not load this font file.", 2.0F, glm::vec4(1.0F, 0.62F, 0.35F, 1.0F));
                        }
                    }
                    m_selectedNode->computedFont = font;
                    m_selectedNode->MarkStyleDirty();
                    m_selectedNode->MarkLayoutDirty();
                    m_hasUnsavedChanges = true;
                }
                else
                {
                    ShowNotification("Custom font path does not exist.", 1.8F, glm::vec4(1.0F, 0.62F, 0.35F, 1.0F));
                }
            }

            char familyBuf[256];
            std::strncpy(familyBuf, font.family.c_str(), sizeof(familyBuf) - 1);
            familyBuf[sizeof(familyBuf) - 1] = '\0';
            if (ImGui::InputText("Font Family Raw", familyBuf, sizeof(familyBuf)))
            {
                const std::string previousFamily = font.family;
                font.family = familyBuf;
                if (!font.family.empty())
                {
                    ImGuiIO& io = ImGui::GetIO();
                    const bool atlasLocked = io.Fonts->Locked;
                    if (EnsureEditorFontLoaded(font.family) == nullptr)
                    {
                        if (atlasLocked)
                        {
                            ShowNotification("Font queued: it will apply when editor font atlas is available.", 2.2F, glm::vec4(0.7F, 0.9F, 1.0F, 1.0F));
                        }
                        else
                        {
                            font.family = previousFamily;
                            ShowNotification("Could not load this font file.", 2.0F, glm::vec4(1.0F, 0.62F, 0.35F, 1.0F));
                        }
                    }
                }
                m_selectedNode->computedFont = font;
                m_selectedNode->MarkStyleDirty();
                m_selectedNode->MarkLayoutDirty();
                m_hasUnsavedChanges = true;
            }

            float fontSize = font.size > 0.0F ? font.size : 16.0F;
            if (ImGui::DragFloat("Font Size", &fontSize, 0.5F, 6.0F, 200.0F, "%.1f"))
            {
                font.size = std::max(6.0F, fontSize);
                m_selectedNode->computedFont = font;
                m_selectedNode->MarkStyleDirty();
                m_selectedNode->MarkLayoutDirty();
                m_hasUnsavedChanges = true;
            }

            int weight = 2;
            switch (font.weight)
            {
                case FontProps::Weight::ExtraLight:
                    weight = 0;
                    break;
                case FontProps::Weight::Light:
                    weight = 1;
                    break;
                case FontProps::Weight::Medium:
                    weight = 3;
                    break;
                case FontProps::Weight::SemiBold:
                    weight = 4;
                    break;
                case FontProps::Weight::Bold:
                    weight = 5;
                    break;
                case FontProps::Weight::ExtraBold:
                    weight = 6;
                    break;
                case FontProps::Weight::Normal:
                default:
                    weight = 2;
                    break;
            }
            const char* weightOptions[] = {"Extra Light", "Light", "Normal", "Medium", "Semi Bold", "Bold", "Extra Bold"};
            if (ImGui::Combo("Font Weight", &weight, weightOptions, 7))
            {
                switch (weight)
                {
                    case 0:
                        font.weight = FontProps::Weight::ExtraLight;
                        break;
                    case 1:
                        font.weight = FontProps::Weight::Light;
                        break;
                    case 3:
                        font.weight = FontProps::Weight::Medium;
                        break;
                    case 4:
                        font.weight = FontProps::Weight::SemiBold;
                        break;
                    case 5:
                        font.weight = FontProps::Weight::Bold;
                        break;
                    case 6:
                        font.weight = FontProps::Weight::ExtraBold;
                        break;
                    case 2:
                    default:
                        font.weight = FontProps::Weight::Normal;
                        break;
                }
                m_selectedNode->computedFont = font;
                m_selectedNode->MarkStyleDirty();
                m_hasUnsavedChanges = true;
            }

            int style = font.style == FontProps::Style::Italic ? 1 : 0;
            const char* styleOptions[] = {"Normal", "Italic"};
            if (ImGui::Combo("Font Style", &style, styleOptions, 2))
            {
                font.style = style == 1 ? FontProps::Style::Italic : FontProps::Style::Normal;
                m_selectedNode->computedFont = font;
                m_selectedNode->MarkStyleDirty();
                m_hasUnsavedChanges = true;
            }

            int align = 1;
            switch (font.align)
            {
                case FontProps::Align::Left:
                    align = 0;
                    break;
                case FontProps::Align::Right:
                    align = 2;
                    break;
                case FontProps::Align::Center:
                default:
                    align = 1;
                    break;
            }
            const char* alignOptions[] = {"Left", "Center", "Right"};
            if (ImGui::Combo("Text Align", &align, alignOptions, 3))
            {
                switch (align)
                {
                    case 0:
                        font.align = FontProps::Align::Left;
                        break;
                    case 2:
                        font.align = FontProps::Align::Right;
                        break;
                    case 1:
                    default:
                        font.align = FontProps::Align::Center;
                        break;
                }
                m_selectedNode->computedFont = font;
                m_selectedNode->MarkStyleDirty();
                m_hasUnsavedChanges = true;
            }

            bool underline = font.underline;
            if (ImGui::Checkbox("Underline", &underline))
            {
                font.underline = underline;
                m_selectedNode->computedFont = font;
                m_selectedNode->MarkStyleDirty();
                m_hasUnsavedChanges = true;
            }

            bool strikethrough = font.strikethrough;
            if (ImGui::Checkbox("Strikethrough", &strikethrough))
            {
                font.strikethrough = strikethrough;
                m_selectedNode->computedFont = font;
                m_selectedNode->MarkStyleDirty();
                m_hasUnsavedChanges = true;
            }

            float letterSpacing = font.letterSpacing;
            if (ImGui::DragFloat("Letter Spacing", &letterSpacing, 0.1F, -8.0F, 32.0F, "%.1f px"))
            {
                font.letterSpacing = letterSpacing;
                m_selectedNode->computedFont = font;
                m_selectedNode->MarkStyleDirty();
                m_selectedNode->MarkLayoutDirty();
                m_hasUnsavedChanges = true;
            }
        }
    }

    // Opacity
    float opacity = m_selectedNode->computedOpacity;
    if (ImGui::SliderFloat("Opacity", &opacity, 0.0F, 1.0F))
    {
        m_selectedNode->opacity = opacity;
        m_selectedNode->computedOpacity = opacity;
        m_selectedNode->MarkStyleDirty();
        m_hasUnsavedChanges = true;
    }

    // Radius
    float radius = m_selectedNode->computedRadius;
    if (ImGui::DragFloat("Radius", &radius, 1.0F, 0.0F, 100.0F))
    {
        m_selectedNode->radius = radius;
        m_selectedNode->computedRadius = radius;
        m_selectedNode->MarkStyleDirty();
        m_hasUnsavedChanges = true;
    }

    float borderColor[4] = {
        m_selectedNode->computedStrokeColor.r,
        m_selectedNode->computedStrokeColor.g,
        m_selectedNode->computedStrokeColor.b,
        m_selectedNode->computedStrokeColor.a
    };
    if (ImGui::ColorEdit4("Border Color", borderColor))
    {
        m_selectedNode->strokeColor = glm::vec4(borderColor[0], borderColor[1], borderColor[2], borderColor[3]);
        m_selectedNode->computedStrokeColor = *m_selectedNode->strokeColor;
        m_selectedNode->MarkStyleDirty();
        m_hasUnsavedChanges = true;
    }

    float borderWidth = m_selectedNode->strokeWidth.value_or(m_selectedNode->computedStrokeWidth);
    if (ImGui::DragFloat("Border Width", &borderWidth, 0.25F, 0.0F, 32.0F))
    {
        m_selectedNode->strokeWidth = std::max(0.0F, borderWidth);
        m_selectedNode->computedStrokeWidth = *m_selectedNode->strokeWidth;
        m_selectedNode->MarkStyleDirty();
        m_hasUnsavedChanges = true;
    }

    // Classes
    ImGui::Text("Classes:");
    for (std::size_t i = 0; i < m_selectedNode->classes.size(); ++i)
    {
        ImGui::BulletText("%s", m_selectedNode->classes[i].c_str());
        ImGui::SameLine();
        if (ImGui::SmallButton(("X##" + std::to_string(i)).c_str()))
        {
            m_selectedNode->RemoveClass(m_selectedNode->classes[i]);
            m_selectedNode->MarkStyleDirty();
            m_hasUnsavedChanges = true;
            break;
        }
    }

    static char newClassBuf[64] = "";
    if (ImGui::InputText("##newClass", newClassBuf, sizeof(newClassBuf), ImGuiInputTextFlags_EnterReturnsTrue))
    {
        if (newClassBuf[0] != '\0')
        {
            m_selectedNode->AddClass(newClassBuf);
            m_selectedNode->MarkStyleDirty();
            newClassBuf[0] = '\0';
            m_hasUnsavedChanges = true;
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("+"))
    {
        if (newClassBuf[0] != '\0')
        {
            m_selectedNode->AddClass(newClassBuf);
            m_selectedNode->MarkStyleDirty();
            newClassBuf[0] = '\0';
            m_hasUnsavedChanges = true;
        }
    }
}

void UiEditor::RenderInspectorInteractions()
{
    ImGui::Text("Interactions");

    if (m_selectedNode->type != UINodeType::Button)
    {
        ImGui::TextDisabled("Select a Button node to edit click actions.");
        return;
    }

    char targetBuf[128];
    std::strncpy(targetBuf, m_selectedNode->onClickTargetId.c_str(), sizeof(targetBuf) - 1);
    targetBuf[sizeof(targetBuf) - 1] = '\0';
    if (ImGui::InputText("OnClick Target ID", targetBuf, sizeof(targetBuf)))
    {
        m_selectedNode->onClickTargetId = targetBuf;
        m_hasUnsavedChanges = true;
    }

    bool toggleTarget = m_selectedNode->onClickToggleTarget;
    if (ImGui::Checkbox("Toggle Target Visibility", &toggleTarget))
    {
        m_selectedNode->onClickToggleTarget = toggleTarget;
        m_hasUnsavedChanges = true;
    }

    char tabGroupBuf[128];
    std::strncpy(tabGroupBuf, m_selectedNode->onClickTabGroupClass.c_str(), sizeof(tabGroupBuf) - 1);
    tabGroupBuf[sizeof(tabGroupBuf) - 1] = '\0';
    if (ImGui::InputText("Tab Content Class", tabGroupBuf, sizeof(tabGroupBuf)))
    {
        m_selectedNode->onClickTabGroupClass = tabGroupBuf;
        m_hasUnsavedChanges = true;
    }
    ImGui::TextDisabled("All nodes with this class are hidden, target is shown.");

    char buttonGroupBuf[128];
    std::strncpy(buttonGroupBuf, m_selectedNode->onClickButtonGroupClass.c_str(), sizeof(buttonGroupBuf) - 1);
    buttonGroupBuf[sizeof(buttonGroupBuf) - 1] = '\0';
    if (ImGui::InputText("Tab Button Class", buttonGroupBuf, sizeof(buttonGroupBuf)))
    {
        m_selectedNode->onClickButtonGroupClass = buttonGroupBuf;
        m_hasUnsavedChanges = true;
    }
    ImGui::TextDisabled("Buttons in this class get :selected style switching.");

    if (ImGui::Button("Preview Click"))
    {
        if (m_tree && !m_selectedNode->id.empty())
        {
            m_tree->TriggerClick(m_selectedNode->id);
            m_tree->ComputeLayout();
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Clear Interaction"))
    {
        m_selectedNode->onClickTargetId.clear();
        m_selectedNode->onClickTabGroupClass.clear();
        m_selectedNode->onClickButtonGroupClass.clear();
        m_selectedNode->onClickToggleTarget = false;
        m_hasUnsavedChanges = true;
    }
}

void UiEditor::RenderStylePanel()
{
    ImGui::Text("Stylesheet");
    ImGui::Separator();

    if (!m_tree || !m_tree->GetStyleSheet())
    {
        ImGui::TextDisabled("No stylesheet loaded");
        return;
    }

    StyleSheet* ss = m_tree->GetStyleSheet();
    ImGui::Text("%zu rules loaded", ss->rules.size());

    // List rules
    for (std::size_t i = 0; i < ss->rules.size(); ++i)
    {
        const StyleRule& rule = ss->rules[i];
        std::string selectorStr;
        for (const auto& part : rule.selector.parts)
        {
            switch (part.type)
            {
                case SelectorType::Id:
                    selectorStr += "#";
                    break;
                case SelectorType::Class:
                    selectorStr += ".";
                    break;
                default:
                    break;
            }
            selectorStr += part.value;
            if (part.pseudo != PseudoClass::None)
            {
                selectorStr += ":";
                switch (part.pseudo)
                {
                    case PseudoClass::Hover:
                        selectorStr += "hover";
                        break;
                    case PseudoClass::Pressed:
                        selectorStr += "pressed";
                        break;
                    case PseudoClass::Focus:
                        selectorStr += "focus";
                        break;
                    case PseudoClass::Disabled:
                        selectorStr += "disabled";
                        break;
                    default:
                        break;
                }
            }
            selectorStr += " ";
        }

        if (ImGui::TreeNode(("Rule " + std::to_string(i)).c_str(), "%s", selectorStr.c_str()))
        {
            if (rule.backgroundColor)
                ImGui::ColorButton("BG", ImVec4(rule.backgroundColor->r, rule.backgroundColor->g, rule.backgroundColor->b, rule.backgroundColor->a)), ImGui::SameLine(), ImGui::Text("backgroundColor");
            if (rule.textColor)
                ImGui::ColorButton("TX", ImVec4(rule.textColor->r, rule.textColor->g, rule.textColor->b, rule.textColor->a)), ImGui::SameLine(), ImGui::Text("textColor");
            if (rule.radius)
                ImGui::Text("radius: %.1f", *rule.radius);
            if (rule.opacity)
                ImGui::Text("opacity: %.2f", *rule.opacity);

            ImGui::TreePop();
        }
    }
}

void UiEditor::RenderAssetsPanel()
{
    ImGui::Text("Assets");
    ImGui::Separator();

    if (ImGui::CollapsingHeader("Current Files", ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::Text("Screen: %s", DisplayPath(m_currentScreenPath));
        ImGui::Text("Styles: %s", DisplayPath(m_currentStylePath));
        ImGui::Text("Tokens: %s", DisplayPath(m_currentTokensPath));
    }

    if (ImGui::CollapsingHeader("Screens", ImGuiTreeNodeFlags_DefaultOpen))
    {
        if (ImGui::Button("New Empty Screen"))
        {
            if (m_tree)
            {
                auto root = std::make_unique<UINode>("root", UINodeType::Container);
                root->layout.width = SizeValue::Percent(100.0F);
                root->layout.height = SizeValue::Percent(100.0F);
                m_tree->SetRoot(std::move(root));
                m_tree->RebuildNodeIndex();
                m_currentScreenPath.clear();
                m_hasUnsavedChanges = true;
                UpdateStateSnapshot();
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Reload") && !m_currentScreenPath.empty())
        {
            ReloadCurrentScreen();
        }

        const std::array<const char*, 3> defaults = {
            "assets/ui/screens/main_menu.ui.json",
            "assets/ui/screens/settings.ui.json",
            "assets/ui/screens/in_game_hud.ui.json"
        };
        namespace fs = std::filesystem;
        std::vector<std::string> screenPaths;
        screenPaths.reserve(32);
        std::unordered_set<std::string> seenPaths;
        auto pushUniquePath = [&](const std::string& path) {
            const std::string normalized = fs::path(path).generic_string();
            if (seenPaths.insert(normalized).second)
            {
                screenPaths.push_back(normalized);
            }
        };
        for (const char* path : defaults)
        {
            pushUniquePath(path);
        }

        const fs::path screensDir("assets/ui/screens");
        if (fs::exists(screensDir))
        {
            for (const auto& entry : fs::directory_iterator(screensDir))
            {
                if (!entry.is_regular_file())
                {
                    continue;
                }
                const fs::path ext = entry.path().extension();
                if (ext != ".json")
                {
                    continue;
                }
                pushUniquePath(entry.path().generic_string());
            }
        }

        for (std::size_t i = 0; i < screenPaths.size(); ++i)
        {
            const std::string& path = screenPaths[i];
            const bool selected = (m_currentScreenPath == path);
            ImGui::PushID(static_cast<int>(i));
            if (ImGui::Selectable(path.c_str(), selected))
            {
                if (LoadScreen(path))
                {
                    ShowNotification(std::string("Loaded ") + path, 2.0F, glm::vec4(0.7F, 0.9F, 1.0F, 1.0F));
                }
            }
            ImGui::PopID();
        }
    }

    if (ImGui::CollapsingHeader("Styles", ImGuiTreeNodeFlags_DefaultOpen))
    {
        if (ImGui::Selectable("assets/ui/styles/base.ui.css.json", m_currentStylePath == "assets/ui/styles/base.ui.css.json"))
        {
            LoadStyleSheet("assets/ui/styles/base.ui.css.json");
        }
        if (ImGui::Selectable("assets/ui/styles/theme_default.tokens.json", m_currentTokensPath == "assets/ui/styles/theme_default.tokens.json"))
        {
            LoadTokens("assets/ui/styles/theme_default.tokens.json");
        }
    }

    if (ImGui::CollapsingHeader("Quick Create", ImGuiTreeNodeFlags_DefaultOpen))
    {
        if (ImGui::Button("Panel"))
        {
            CreateNode(UINodeType::Panel, "");
        }
        ImGui::SameLine();
        if (ImGui::Button("Text"))
        {
            CreateNode(UINodeType::Text, "");
        }
        ImGui::SameLine();
        if (ImGui::Button("Button"))
        {
            CreateNode(UINodeType::Button, "");
        }
        if (ImGui::Button("Slider"))
        {
            CreateNode(UINodeType::Slider, "");
        }
        ImGui::SameLine();
        if (ImGui::Button("Toggle"))
        {
            CreateNode(UINodeType::Toggle, "");
        }
        ImGui::SameLine();
        if (ImGui::Button("Image"))
        {
            CreateNode(UINodeType::Image, "");
        }
        ImGui::SameLine();
        if (ImGui::Button("Shape"))
        {
            CreateNode(UINodeType::Shape, "");
        }
    }
}

void UiEditor::RenderPreviewPanel()
{
    ImGui::Text("Live Preview");
    ImGui::Separator();

    int mode = static_cast<int>(m_mode);
    const char* modeLabels[] = {"None", "Edit", "Preview", "Create"};
    if (ImGui::Combo("Mode", &mode, modeLabels, 4))
    {
        m_mode = static_cast<EditorMode>(mode);
    }

    if (m_tree)
    {
        if (ImGui::Button("1280x720"))
        {
            m_tree->SetScreenSize(1280, 720);
            m_tree->ComputeLayout();
        }
        ImGui::SameLine();
        if (ImGui::Button("1920x1080"))
        {
            m_tree->SetScreenSize(1920, 1080);
            m_tree->ComputeLayout();
        }
        ImGui::SameLine();
        if (ImGui::Button("2560x1440"))
        {
            m_tree->SetScreenSize(2560, 1440);
            m_tree->ComputeLayout();
        }
    }

    if (m_selectedNode != nullptr)
    {
        ImGui::SeparatorText("State Preview");
        if (ImGui::Checkbox("Hover", &m_selectedNode->state.hover))
        {
            m_selectedNode->MarkStyleDirty();
        }
        ImGui::SameLine();
        if (ImGui::Checkbox("Pressed", &m_selectedNode->state.pressed))
        {
            m_selectedNode->MarkStyleDirty();
        }
        ImGui::SameLine();
        if (ImGui::Checkbox("Focused", &m_selectedNode->state.focused))
        {
            m_selectedNode->MarkStyleDirty();
        }
        if (ImGui::Checkbox("Disabled", &m_selectedNode->state.disabled))
        {
            m_selectedNode->MarkStyleDirty();
        }
        ImGui::SameLine();
        if (ImGui::Checkbox("Selected", &m_selectedNode->state.selected))
        {
            m_selectedNode->MarkStyleDirty();
        }
    }

    ImGui::SeparatorText("Workflow");
    ImGui::BulletText("Middle mouse or Space+Drag to pan");
    ImGui::BulletText("Mouse wheel to zoom around cursor");
    ImGui::BulletText("Drag corner handles to resize");
    ImGui::BulletText("Manual drag works only when parent is not Flex/Grid");
    ImGui::BulletText("Switch Mode to Preview to test tab/button click actions");
}

void UiEditor::BuildDefaultDockLayout()
{
    m_leftPaneWidth = 300.0F;
    m_rightPaneWidth = 360.0F;
    m_bottomPaneHeight = 220.0F;
    m_leftBottomRatio = 0.45F;
    m_rightBottomRatio = 0.50F;
}

void UiEditor::SelectNode(UINode* node)
{
    m_selectedNode = node;
}

void UiEditor::ClearSelection()
{
    m_selectedNode = nullptr;
}

UINode* UiEditor::CreateNode(UINodeType type, const std::string& id, UINode* parent)
{
    if (!parent && !m_selectedNode && m_tree)
    {
        m_selectedNode = m_tree->GetRoot();
    }

    UINode* targetParent = parent != nullptr ? parent : m_selectedNode;
    if (!targetParent)
        return nullptr;

    std::string nodeId = id.empty() ? GenerateNodeId(type) : id;
    std::unique_ptr<UINode> newNode;

    switch (type)
    {
        case UINodeType::Panel:
            newNode = UINode::CreatePanel(nodeId);
            break;
        case UINodeType::Text:
            newNode = UINode::CreateText(nodeId, "Text");
            break;
        case UINodeType::Button:
            newNode = UINode::CreateButton(nodeId, "Button");
            break;
        case UINodeType::Image:
            newNode = UINode::CreateImage(nodeId, "");
            break;
        case UINodeType::Shape:
            newNode = UINode::CreateShape(nodeId, UIShapeType::Rectangle);
            break;
        case UINodeType::Slider:
            newNode = UINode::CreateSlider(nodeId);
            break;
        case UINodeType::Toggle:
            newNode = UINode::CreateToggle(nodeId);
            break;
        case UINodeType::TextInput:
            newNode = UINode::CreateTextInput(nodeId);
            break;
        case UINodeType::ProgressBar:
            newNode = UINode::CreateProgressBar(nodeId);
            break;
        case UINodeType::ScrollView:
            newNode = UINode::CreateScrollView(nodeId);
            break;
        default:
            newNode = std::make_unique<UINode>(nodeId, type);
            break;
    }

    if (!newNode)
        return nullptr;

    // Set default size
    newNode->layout.width = SizeValue::Px(100);
    newNode->layout.height = SizeValue::Px(50);
    newNode->computedBackgroundColor = glm::vec4(0.15F, 0.15F, 0.18F, 1.0F);
    if (type == UINodeType::Shape)
    {
        newNode->layout.width = SizeValue::Px(120.0F);
        newNode->layout.height = SizeValue::Px(120.0F);
        newNode->strokeColor = glm::vec4(0.88F, 0.90F, 0.96F, 1.0F);
        newNode->strokeWidth = 2.0F;
        newNode->shapeType = UIShapeType::Rectangle;
    }

    UINode* ptr = newNode.get();
    targetParent->AddChild(std::move(newNode));
    if (m_tree)
    {
        m_tree->RebuildNodeIndex();
    }

    PushAction("Create " + std::string(NodeTypeToString(type)));
    SelectNode(ptr);

    return ptr;
}

void UiEditor::DeleteSelectedNode()
{
    if (!m_selectedNode || !m_tree || m_selectedNode == m_tree->GetRoot())
        return;

    UINode* parent = m_selectedNode->parent;
    if (!parent)
        return;

    parent->RemoveChild(m_selectedNode);
    m_selectedNode = nullptr;
    if (m_tree)
    {
        m_tree->RebuildNodeIndex();
    }
    PushAction("Delete Node");
}

void UiEditor::DuplicateSelectedNode()
{
    if (!m_selectedNode || !m_selectedNode->parent)
        return;

    // Serialize and deserialize to duplicate
    std::string state = SerializeScreen(*m_selectedNode);

    auto newNode = ParseScreen(state);
    if (!newNode)
        return;

    newNode->id = m_selectedNode->id + "_copy";
    newNode->name = m_selectedNode->name + " (Copy)";

    m_selectedNode->parent->AddChild(std::move(newNode));
    if (m_tree)
    {
        m_tree->RebuildNodeIndex();
    }
    PushAction("Duplicate Node");
}

bool UiEditor::LoadScreen(const std::string& filePath)
{
    if (!m_tree)
        return false;

    auto root = engine::ui::LoadScreen(filePath);
    if (!root)
        return false;

    m_tree->SetRoot(std::move(root));
    m_currentScreenPath = filePath;
    HasFileChanged(m_currentScreenPath, m_lastScreenModTime);
    m_selectedNode = nullptr;
    m_draggedNode = nullptr;
    m_undoStack.clear();
    m_redoStack.clear();
    UpdateStateSnapshot();
    return true;
}

bool UiEditor::SaveScreen(const std::string& filePath)
{
    if (!m_tree || !m_tree->GetRoot())
        return false;

    bool result = engine::ui::SaveScreen(filePath, *m_tree->GetRoot());
    if (result)
    {
        m_currentScreenPath = filePath;
        HasFileChanged(m_currentScreenPath, m_lastScreenModTime);
        m_hasUnsavedChanges = false;
        UpdateStateSnapshot();
    }
    return result;
}

bool UiEditor::SaveCurrentScreen()
{
    if (m_currentScreenPath.empty())
    {
        return false;
    }
    return SaveScreen(m_currentScreenPath);
}

bool UiEditor::LoadStyleSheet(const std::string& filePath)
{
    if (!m_tree)
        return false;

    static StyleSheet ss;
    if (!engine::ui::LoadStyleSheet(filePath, ss))
        return false;

    m_tree->SetStyleSheet(&ss);
    m_currentStylePath = filePath;
    HasFileChanged(m_currentStylePath, m_lastStyleModTime);
    return true;
}

bool UiEditor::LoadTokens(const std::string& filePath)
{
    if (!m_tree)
        return false;

    static TokenCollection tokens;
    if (!engine::ui::LoadTokens(filePath, tokens))
        return false;

    m_tree->SetTokens(&tokens);
    m_currentTokensPath = filePath;
    HasFileChanged(m_currentTokensPath, m_lastTokensModTime);
    return true;
}

bool UiEditor::CanUndo() const
{
    return !m_undoStack.empty();
}

bool UiEditor::CanRedo() const
{
    return !m_redoStack.empty();
}

void UiEditor::Undo()
{
    if (!CanUndo())
        return;

    const EditorAction action = m_undoStack.back();
    m_undoStack.pop_back();
    m_redoStack.push_back(action);

    RestoreState(action.beforeState);
    m_stateSnapshot = action.beforeState;
    m_hasUnsavedChanges = true;
}

void UiEditor::Redo()
{
    if (!CanRedo())
        return;

    const EditorAction action = m_redoStack.back();
    m_redoStack.pop_back();
    m_undoStack.push_back(action);

    RestoreState(action.afterState);
    m_stateSnapshot = action.afterState;
    m_hasUnsavedChanges = true;
}

void UiEditor::PushAction(const std::string& description)
{
    if (!m_tree || !m_tree->GetRoot())
    {
        return;
    }

    EditorAction action;
    action.description = description;
    action.beforeState = m_stateSnapshot;
    SaveState(action.afterState);
    if (action.beforeState == action.afterState)
    {
        return;
    }
    m_undoStack.push_back(action);
    m_redoStack.clear();
    m_stateSnapshot = action.afterState;
    m_hasUnsavedChanges = true;
}

void UiEditor::SaveState(std::string& outState)
{
    if (m_tree && m_tree->GetRoot())
    {
        outState = SerializeScreen(*m_tree->GetRoot());
        return;
    }
    outState.clear();
}

void UiEditor::RestoreState(const std::string& state)
{
    if (!m_tree)
        return;

    auto root = ParseScreen(state);
    if (root)
    {
        m_tree->SetRoot(std::move(root));
        m_selectedNode = nullptr;
        m_draggedNode = nullptr;
        UpdateStateSnapshot();
    }
}

void UiEditor::ReloadCurrentScreen()
{
    if (!m_currentScreenPath.empty())
    {
        LoadScreen(m_currentScreenPath);
    }
}

void UiEditor::CopySelectedNode()
{
    if (!m_selectedNode)
    {
        return;
    }
    m_clipboard = SerializeScreen(*m_selectedNode);
}

void UiEditor::PasteNode()
{
    if (!m_tree || m_clipboard.empty())
    {
        return;
    }

    UINode* targetParent = m_selectedNode != nullptr ? m_selectedNode : m_tree->GetRoot();
    if (!targetParent)
    {
        return;
    }

    auto pasted = ParseScreen(m_clipboard);
    if (!pasted)
    {
        return;
    }

    pasted->id = pasted->id + "_copy";
    UINode* pastedPtr = targetParent->AddChild(std::move(pasted));
    if (m_tree)
    {
        m_tree->RebuildNodeIndex();
    }
    SelectNode(pastedPtr);
    PushAction("Paste Node");
}

void UiEditor::AddTemplate(const NodeTemplate& templ)
{
    m_templates.push_back(templ);
}

UINode* UiEditor::CreateFromTemplate(const std::string& templateName, float x, float y)
{
    auto it = std::find_if(m_templates.begin(), m_templates.end(), [&](const NodeTemplate& templ) {
        return templ.name == templateName;
    });
    if (it == m_templates.end())
    {
        return nullptr;
    }

    UINode* node = CreateNode(it->type, "", nullptr);
    if (!node)
    {
        return nullptr;
    }

    if (it->setupCallback)
    {
        it->setupCallback(*node);
    }
    for (const std::string& className : it->defaultClasses)
    {
        node->AddClass(className);
    }
    node->layout.position = Position::Absolute;
    node->layout.offset = glm::vec2(x, y);
    node->MarkLayoutDirty();
    return node;
}

void UiEditor::ShowNotification(const std::string& message, float duration, const glm::vec4& color)
{
    EditorNotification notification;
    notification.message = message;
    notification.duration = duration;
    notification.remaining = duration;
    notification.color = color;
    m_notifications.push_back(notification);
}

void UiEditor::HandleKeyboardShortcuts()
{
    const ImGuiIO& io = ImGui::GetIO();
    if (io.WantTextInput)
    {
        return;
    }

    const bool ctrl = io.KeyCtrl;
    const bool shift = io.KeyShift;

    if (ctrl && ImGui::IsKeyPressed(ImGuiKey_Z) && shift)
    {
        Redo();
        return;
    }
    if (ctrl && ImGui::IsKeyPressed(ImGuiKey_Z))
    {
        Undo();
        return;
    }
    if (ctrl && ImGui::IsKeyPressed(ImGuiKey_Y))
    {
        Redo();
        return;
    }
    if (ctrl && ImGui::IsKeyPressed(ImGuiKey_D))
    {
        DuplicateSelectedNode();
        return;
    }
    if (ctrl && ImGui::IsKeyPressed(ImGuiKey_C))
    {
        CopySelectedNode();
        return;
    }
    if (ctrl && ImGui::IsKeyPressed(ImGuiKey_X))
    {
        CopySelectedNode();
        DeleteSelectedNode();
        return;
    }
    if (ctrl && ImGui::IsKeyPressed(ImGuiKey_V))
    {
        PasteNode();
        return;
    }
    if (ctrl && ImGui::IsKeyPressed(ImGuiKey_S))
    {
        SaveCurrentScreen();
        return;
    }

    if (ImGui::IsKeyPressed(ImGuiKey_Delete) || ImGui::IsKeyPressed(ImGuiKey_Backspace))
    {
        if (m_tree && m_selectedNode && m_selectedNode != m_tree->GetRoot())
        {
            DeleteSelectedNode();
        }
    }
}

void UiEditor::RefreshAvailableFonts()
{
    namespace fs = std::filesystem;
    m_availableFontPaths.clear();
    m_availableFontLabels.clear();
    m_availableFontPaths.emplace_back("");
    m_availableFontLabels.emplace_back("(Theme Default)");

    std::unordered_set<std::string> seen;
    auto addFontPath = [&](const std::string& rawPath) {
        if (rawPath.empty())
        {
            return;
        }

        fs::path path(rawPath);
        std::error_code ec;
        if (!fs::exists(path, ec) || !fs::is_regular_file(path, ec))
        {
            return;
        }

        std::string ext = path.extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (ext != ".ttf" && ext != ".otf")
        {
            return;
        }

        std::string normalized = fs::weakly_canonical(path, ec).generic_string();
        if (ec || normalized.empty())
        {
            normalized = path.generic_string();
        }
        if (!seen.insert(normalized).second)
        {
            return;
        }

        m_availableFontPaths.push_back(normalized);
        std::string label = path.stem().string();
        if (label.empty())
        {
            label = normalized;
        }
        m_availableFontLabels.push_back(label);
    };

    for (const std::string& customPath : m_customFontPaths)
    {
        addFontPath(customPath);
    }

    std::vector<fs::path> systemDirs;
#if defined(_WIN32)
    systemDirs.emplace_back("C:/Windows/Fonts");
    if (const char* localAppData = std::getenv("LOCALAPPDATA"))
    {
        systemDirs.emplace_back(std::string(localAppData) + "/Microsoft/Windows/Fonts");
    }
#else
    systemDirs.emplace_back("/usr/share/fonts");
    systemDirs.emplace_back("/usr/local/share/fonts");
    if (const char* home = std::getenv("HOME"))
    {
        systemDirs.emplace_back(std::string(home) + "/.fonts");
        systemDirs.emplace_back(std::string(home) + "/.local/share/fonts");
    }
#endif

    for (const fs::path& dir : systemDirs)
    {
        std::error_code ec;
        if (!fs::exists(dir, ec) || !fs::is_directory(dir, ec))
        {
            continue;
        }
        for (const auto& entry : fs::recursive_directory_iterator(dir, ec))
        {
            if (ec)
            {
                break;
            }
            if (!entry.is_regular_file())
            {
                continue;
            }
            addFontPath(entry.path().generic_string());
        }
    }

    m_availableFontsDirty = false;
}

ImFont* UiEditor::EnsureEditorFontLoaded(const std::string& path)
{
    if (path.empty())
    {
        return nullptr;
    }

    namespace fs = std::filesystem;
    std::error_code ec;
    std::string normalizedPath = fs::weakly_canonical(fs::path(path), ec).generic_string();
    if (ec || normalizedPath.empty())
    {
        normalizedPath = fs::path(path).generic_string();
    }

    auto it = m_editorFontCache.find(normalizedPath);
    if (it != m_editorFontCache.end())
    {
        return it->second;
    }

    ImGuiIO& io = ImGui::GetIO();
    if (io.Fonts->Locked)
    {
        m_pendingEditorFontLoads.insert(normalizedPath);
        return nullptr;
    }
    io.Fonts->TexDesiredWidth = std::max(io.Fonts->TexDesiredWidth, 4096);
    io.Fonts->Flags |= ImFontAtlasFlags_NoPowerOfTwoHeight;

    ImFontConfig config;
    config.OversampleH = 3;
    config.OversampleV = 2;
    config.PixelSnapH = false;
    config.RasterizerMultiply = 1.08F;
    ImFont* font = io.Fonts->AddFontFromFileTTF(normalizedPath.c_str(), 72.0F, &config);
    if (font == nullptr)
    {
        // Atlas might be full; rebuild with a minimal set and retry the requested font.
        io.Fonts->Clear();
        m_editorFontCache.clear();
        io.Fonts->AddFontDefault();
        font = io.Fonts->AddFontFromFileTTF(normalizedPath.c_str(), 72.0F, &config);
        if (font == nullptr)
        {
            return nullptr;
        }
    }

    io.Fonts->Build();
    ImGui_ImplOpenGL3_DestroyFontsTexture();
    ImGui_ImplOpenGL3_CreateFontsTexture();
    m_editorFontCache[normalizedPath] = font;
    m_pendingEditorFontLoads.erase(normalizedPath);
    return font;
}

ImFont* UiEditor::ResolveNodeFont(const UINode& node)
{
    if (!node.computedFont.family.empty())
    {
        return EnsureEditorFontLoaded(node.computedFont.family);
    }

    if (m_availableFontsDirty)
    {
        RefreshAvailableFonts();
    }

    const auto pickFallbackByName = [&](const char* token) -> ImFont* {
        for (std::size_t i = 1; i < m_availableFontPaths.size(); ++i)
        {
            std::string lower = m_availableFontPaths[i];
            std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) {
                return static_cast<char>(std::tolower(c));
            });
            if (lower.find(token) != std::string::npos)
            {
                if (ImFont* font = EnsureEditorFontLoaded(m_availableFontPaths[i]))
                {
                    return font;
                }
            }
        }
        return nullptr;
    };

#if defined(_WIN32)
    if (ImFont* font = pickFallbackByName("segoeui.ttf"))
    {
        return font;
    }
    if (ImFont* font = pickFallbackByName("arial.ttf"))
    {
        return font;
    }
#else
    if (ImFont* font = pickFallbackByName("dejavusans"))
    {
        return font;
    }
#endif
    return ImGui::GetFont();
}

float UiEditor::ComputeCanvasFontSize(const UINode& node) const
{
    const float fontSize = node.computedFont.size > 0.0F ? node.computedFont.size : 16.0F;
    const float transformScale = std::max(0.25F, std::max(std::abs(node.transformScale.x), std::abs(node.transformScale.y)));
    return std::max(8.0F, fontSize * m_canvasZoom * transformScale);
}

std::string UiEditor::GenerateNodeId(UINodeType type) const
{
    static int counter = 0;
    return std::string(NodeTypeToString(type)) + "_" + std::to_string(++counter);
}

void UiEditor::SnapValue(float& value) const
{
    if (m_gridSize > 0.0F)
    {
        value = std::round(value / m_gridSize) * m_gridSize;
    }
}

void UiEditor::SnapPosition(float& x, float& y) const
{
    SnapValue(x);
    SnapValue(y);
}

bool UiEditor::IsNodeVisible(const UINode& node) const
{
    return node.visibility != Visibility::Collapsed && node.layout.display != Display::None;
}

void UiEditor::UpdateStateSnapshot()
{
    SaveState(m_stateSnapshot);
}

void UiEditor::QueueHierarchyAction(PendingHierarchyActionType type, UINode* node, UINode* aux)
{
    PendingHierarchyAction action;
    action.type = type;
    action.node = node;
    action.aux = aux;
    m_pendingHierarchyActions.push_back(action);
}

bool UiEditor::IsNodeInTree(const UINode* node) const
{
    if (!m_tree || !m_tree->GetRoot() || node == nullptr)
    {
        return false;
    }

    bool found = false;
    std::function<void(const UINode&)> visit = [&](const UINode& current) {
        if (&current == node)
        {
            found = true;
            return;
        }
        for (const auto& child : current.children)
        {
            if (found || !child)
            {
                continue;
            }
            visit(*child);
        }
    };
    visit(*m_tree->GetRoot());
    return found;
}

void UiEditor::ApplyPendingHierarchyActions()
{
    if (!m_tree || m_pendingHierarchyActions.empty())
    {
        m_pendingHierarchyActions.clear();
        return;
    }

    const bool hadActions = !m_pendingHierarchyActions.empty();
    for (const PendingHierarchyAction& action : m_pendingHierarchyActions)
    {
        if (action.node == nullptr || !IsNodeInTree(action.node))
        {
            continue;
        }

        switch (action.type)
        {
            case PendingHierarchyActionType::AddPanel:
            {
                auto newNode = UINode::CreatePanel(GenerateNodeId(UINodeType::Panel));
                action.node->AddChild(std::move(newNode));
                PushAction("Add Panel");
                break;
            }
            case PendingHierarchyActionType::AddButton:
            {
                auto newNode = UINode::CreateButton(GenerateNodeId(UINodeType::Button), "Button");
                action.node->AddChild(std::move(newNode));
                PushAction("Add Button");
                break;
            }
            case PendingHierarchyActionType::AddText:
            {
                auto newNode = UINode::CreateText(GenerateNodeId(UINodeType::Text), "Text");
                action.node->AddChild(std::move(newNode));
                PushAction("Add Text");
                break;
            }
            case PendingHierarchyActionType::AddShape:
            {
                auto newNode = UINode::CreateShape(GenerateNodeId(UINodeType::Shape), UIShapeType::Rectangle);
                action.node->AddChild(std::move(newNode));
                PushAction("Add Shape");
                break;
            }
            case PendingHierarchyActionType::Delete:
            {
                SelectNode(action.node);
                DeleteSelectedNode();
                break;
            }
            case PendingHierarchyActionType::Duplicate:
            {
                SelectNode(action.node);
                DuplicateSelectedNode();
                break;
            }
            case PendingHierarchyActionType::Reparent:
            {
                UINode* droppedNode = action.node;
                UINode* newParent = action.aux;
                if (droppedNode == nullptr
                    || newParent == nullptr
                    || !IsNodeInTree(droppedNode)
                    || !IsNodeInTree(newParent)
                    || droppedNode == newParent
                    || droppedNode->parent == nullptr
                    || droppedNode->FindDescendant(newParent->id) != nullptr)
                {
                    break;
                }
                std::unique_ptr<UINode> moved = droppedNode->parent->RemoveChild(droppedNode);
                if (moved)
                {
                    newParent->AddChild(std::move(moved));
                    PushAction("Reparent Node");
                }
                break;
            }
            case PendingHierarchyActionType::MoveUp:
            {
                UINode* node = action.node;
                if (node == nullptr || node->parent == nullptr)
                {
                    break;
                }
                auto& siblings = node->parent->children;
                for (std::size_t i = 1; i < siblings.size(); ++i)
                {
                    if (siblings[i].get() == node)
                    {
                        std::swap(siblings[i], siblings[i - 1]);
                        node->parent->MarkLayoutDirty();
                        PushAction("Move Node Up");
                        break;
                    }
                }
                break;
            }
            case PendingHierarchyActionType::MoveDown:
            {
                UINode* node = action.node;
                if (node == nullptr || node->parent == nullptr)
                {
                    break;
                }
                auto& siblings = node->parent->children;
                for (std::size_t i = 0; i + 1 < siblings.size(); ++i)
                {
                    if (siblings[i].get() == node)
                    {
                        std::swap(siblings[i], siblings[i + 1]);
                        node->parent->MarkLayoutDirty();
                        PushAction("Move Node Down");
                        break;
                    }
                }
                break;
            }
            default:
                break;
        }
    }

    if (hadActions && m_tree)
    {
        m_tree->RebuildNodeIndex();
    }
    m_pendingHierarchyActions.clear();
}

} // namespace engine::ui

#endif // BUILD_WITH_IMGUI
