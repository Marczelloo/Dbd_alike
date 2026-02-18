#pragma once

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include <glm/vec2.hpp>

#include "engine/ui/UiNode.hpp"
#include "engine/ui/UiStyle.hpp"

namespace engine::platform
{
class Input;
}

namespace engine::ui
{
class UiSystem;

// Virtual resolution settings
struct VirtualResolution
{
    int width = 1920;
    int height = 1080;

    enum class ScaleMode
    {
        FitHeight,
        FitWidth,
        FitMin,
        FitMax,
        Stretch
    } scaleMode = ScaleMode::FitHeight;
};

// Callback types for node interactions
using OnClickCallback = std::function<void(UINode&)>;
using OnClickSimpleCallback = std::function<void()>;
using OnValueChangedCallback = std::function<void(UINode&, float)>;
using OnTextChangedCallback = std::function<void(UINode&, const std::string&)>;
using TextProviderCallback = std::function<std::string()>;
using OnFocusCallback = std::function<void(UINode&)>;
using OnBlurCallback = std::function<void(UINode&)>;

// Node callbacks container
struct NodeCallbacks
{
    OnClickCallback onClick;
    OnValueChangedCallback onValueChanged;
    OnTextChangedCallback onTextChanged;
    OnFocusCallback onFocus;
    OnBlurCallback onBlur;
};

// UI Tree - manages retained UI node hierarchy
class UiTree
{
public:
    UiTree();
    ~UiTree();

    // Non-copyable
    UiTree(const UiTree&) = delete;
    UiTree& operator=(const UiTree&) = delete;

    // Initialization
    void SetVirtualResolution(int width, int height, VirtualResolution::ScaleMode mode = VirtualResolution::ScaleMode::FitHeight);
    void SetScreenSize(int width, int height);

    // Root node access
    [[nodiscard]] UINode* GetRoot()
    {
        return m_root.get();
    }
    [[nodiscard]] const UINode* GetRoot() const
    {
        return m_root.get();
    }
    void SetRoot(std::unique_ptr<UINode> root);
    void RebuildNodeIndex();

    // Node lookup by ID
    [[nodiscard]] UINode* FindNode(const std::string& id);
    [[nodiscard]] const UINode* FindNode(const std::string& id) const;

    // Style and tokens
    void SetStyleSheet(StyleSheet* styleSheet);
    void SetTokens(TokenCollection* tokens);
    [[nodiscard]] TokenCollection* GetTokens()
    {
        return m_tokens;
    }
    [[nodiscard]] StyleSheet* GetStyleSheet()
    {
        return m_styleSheet;
    }

    // Callbacks binding
    void BindOnClick(const std::string& nodeId, OnClickCallback callback);
    void BindOnClick(const std::string& nodeId, OnClickSimpleCallback callback);
    void BindOnValueChanged(const std::string& nodeId, OnValueChangedCallback callback);
    void BindOnTextChanged(const std::string& nodeId, OnTextChangedCallback callback);
    void BindOnFocus(const std::string& nodeId, OnFocusCallback callback);
    void BindOnBlur(const std::string& nodeId, OnBlurCallback callback);
    void ClearBindings(const std::string& nodeId);

    // Bind value directly to a slider/toggle
    void BindSlider(const std::string& nodeId, float* valuePtr, float minValue, float maxValue);
    void BindToggle(const std::string& nodeId, bool* valuePtr);
    void BindText(const std::string& nodeId, TextProviderCallback callback);
    void BindSliderValue(const std::string& nodeId, float* valuePtr);
    void BindToggleValue(const std::string& nodeId, bool* valuePtr);
    void BindTextValue(const std::string& nodeId, std::string* valuePtr);
    bool SetNodeVisibility(const std::string& nodeId, Visibility visibility);
    bool ToggleNodeVisibility(const std::string& nodeId, Visibility hiddenMode = Visibility::Collapsed);
    bool TriggerClick(const std::string& nodeId);

    // Input handling
    void ProcessInput(const platform::Input* input, float deltaSeconds);

    // Layout pass - computes positions and sizes
    void ComputeLayout();

    // Rendering - draws to existing UiSystem
    void RenderToUiSystem(UiSystem& uiSystem) const;

    // Get mouse position in virtual coordinates
    [[nodiscard]] glm::vec2 GetVirtualMousePos(float screenMouseX, float screenMouseY) const;

    // Scale helpers
    [[nodiscard]] float GetVirtualToScreenScale() const
    {
        return m_scale;
    }
    [[nodiscard]] glm::vec2 VirtualToScreen(float vx, float vy) const;
    [[nodiscard]] glm::vec2 ScreenToVirtual(float sx, float sy) const;

    // Debug/Editor support
    void SetDebugLayout(bool enabled)
    {
        m_debugLayout = enabled;
    }
    [[nodiscard]] bool IsDebugLayout() const
    {
        return m_debugLayout;
    }

private:
    // Layout computation
    void MeasureNode(UINode& node);
    void ArrangeNode(UINode& node, float x, float y, float availableWidth, float availableHeight);
    void ApplyStyleToTree(UINode& node);

    // Input handling helpers
    UINode* HitTest(UINode& node, float vx, float vy);
    void UpdateNodeInput(UINode& node, const platform::Input* input, float deltaSeconds);
    void ProcessClick(UINode& node);
    void ProcessSliderDrag(UINode& node, float mouseX);
    [[nodiscard]] bool IsFocusable(const UINode& node) const;
    [[nodiscard]] UINode* FindNearestFocusable(UINode* from, int dirX, int dirY) const;
    void GatherFocusableNodes(UINode& node, std::vector<UINode*>& outNodes);
    void SetFocusedNode(UINode* node);
    void StepFocus(int step);
    void MoveFocus(int dirX, int dirY);

    // Rendering helpers
    void RenderNode(const UINode& node, UiSystem& uiSystem) const;
    void RenderNodeBackground(const UINode& node, UiSystem& uiSystem) const;
    void RenderNodeBorder(const UINode& node, UiSystem& uiSystem) const;
    void RenderNodeShadow(const UINode& node, UiSystem& uiSystem) const;
    void RenderNodeContent(const UINode& node, UiSystem& uiSystem) const;
    void RenderDebugLayout(const UINode& node, UiSystem& uiSystem) const;

    // Member variables
    std::unique_ptr<UINode> m_root;
    std::unordered_map<std::string, UINode*> m_nodeIndex;
    std::unordered_map<std::string, NodeCallbacks> m_callbacks;
    std::unordered_map<std::string, float*> m_sliderBindings;
    std::unordered_map<std::string, bool*> m_toggleBindings;
    std::unordered_map<std::string, std::string*> m_textBindings;
    std::unordered_map<std::string, TextProviderCallback> m_textProviders;

    StyleSheet* m_styleSheet = nullptr;
    TokenCollection* m_tokens = nullptr;

    VirtualResolution m_virtualRes;
    int m_screenWidth = 0;
    int m_screenHeight = 0;
    float m_scale = 1.0F;
    glm::vec2 m_offset{0.0F, 0.0F};

    // Input state
    UINode* m_hoveredNode = nullptr;
    UINode* m_pressedNode = nullptr;
    UINode* m_focusedNode = nullptr;
    std::vector<UINode*> m_focusableNodes;
    bool m_mouseCaptured = false;
    float m_navRepeatTimer = 0.0F;
    glm::vec2 m_lastNavAxis{0.0F, 0.0F};

    // Debug
    bool m_debugLayout = false;
};

} // namespace engine::ui
