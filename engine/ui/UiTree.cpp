#include "engine/ui/UiTree.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <sstream>
#include <unordered_map>

#include <GLFW/glfw3.h>
#include <glm/trigonometric.hpp>

#include "engine/platform/Input.hpp"
#include "engine/ui/UiSystem.hpp"

namespace engine::ui
{
namespace
{
void ResetComputedStyle(UINode& node)
{
    node.computedBackgroundColor = glm::vec4(0.0F, 0.0F, 0.0F, 0.0F);
    node.computedTextColor = glm::vec4(1.0F, 1.0F, 1.0F, 1.0F);
    node.computedOpacity = 1.0F;
    node.computedRadius = 0.0F;
    node.computedStrokeColor = glm::vec4(0.0F, 0.0F, 0.0F, 0.0F);
    node.computedStrokeWidth = 0.0F;
    node.computedShadow = ShadowProps{};
    node.computedFont = FontProps{};
}

bool HasRenderTransform(const UINode& node)
{
    return std::abs(node.transformRotationDeg) > 0.001F
        || std::abs(node.transformTranslate.x) > 0.001F
        || std::abs(node.transformTranslate.y) > 0.001F
        || std::abs(node.transformScale.x - 1.0F) > 0.001F
        || std::abs(node.transformScale.y - 1.0F) > 0.001F;
}

float EstimateTextContentWidth(const UINode& node)
{
    if (node.text.empty())
    {
        return 0.0F;
    }
    const float fontSize = std::max(6.0F, node.computedFont.size);
    const float charWidth = fontSize * 0.6F;
    const float spacing = node.computedFont.letterSpacing;
    float maxWidth = 0.0F;
    std::size_t lineStart = 0;
    while (lineStart <= node.text.size())
    {
        const std::size_t lineEnd = node.text.find('\n', lineStart);
        const std::size_t glyphCount = (lineEnd == std::string::npos) ? (node.text.size() - lineStart) : (lineEnd - lineStart);
        const float lineWidth = static_cast<float>(glyphCount) * charWidth
            + std::max(0.0F, static_cast<float>(glyphCount > 0 ? glyphCount - 1 : 0)) * spacing;
        maxWidth = std::max(maxWidth, lineWidth);
        if (lineEnd == std::string::npos)
        {
            break;
        }
        lineStart = lineEnd + 1;
    }
    return std::max(1.0F, maxWidth);
}

float EstimateTextContentHeight(const UINode& node)
{
    const int lineCount = 1 + static_cast<int>(std::count(node.text.begin(), node.text.end(), '\n'));
    return std::max(1.0F, std::max(6.0F, node.computedFont.size) * 1.4F * static_cast<float>(lineCount));
}

float WeightAlphaMultiplier(FontProps::Weight weight)
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

int WeightExtraPasses(FontProps::Weight weight)
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

UiTree::UiTree()
{
    m_root = std::make_unique<UINode>("root", UINodeType::Container);
    m_root->layout.width = SizeValue::Percent(100.0F);
    m_root->layout.height = SizeValue::Percent(100.0F);
}

UiTree::~UiTree() = default;

void UiTree::SetVirtualResolution(int width, int height, VirtualResolution::ScaleMode mode)
{
    m_virtualRes.width = width;
    m_virtualRes.height = height;
    m_virtualRes.scaleMode = mode;

    // Recalculate scale
    if (m_screenWidth > 0 && m_screenHeight > 0)
    {
        SetScreenSize(m_screenWidth, m_screenHeight);
    }
}

void UiTree::SetScreenSize(int width, int height)
{
    m_screenWidth = width;
    m_screenHeight = height;

    if (m_virtualRes.width <= 0 || m_virtualRes.height <= 0 || width <= 0 || height <= 0)
    {
        m_scale = 1.0F;
        m_offset = glm::vec2(0.0F, 0.0F);
        return;
    }

    const float scaleX = static_cast<float>(width) / static_cast<float>(m_virtualRes.width);
    const float scaleY = static_cast<float>(height) / static_cast<float>(m_virtualRes.height);

    switch (m_virtualRes.scaleMode)
    {
        case VirtualResolution::ScaleMode::FitHeight:
            m_scale = scaleY;
            break;
        case VirtualResolution::ScaleMode::FitWidth:
            m_scale = scaleX;
            break;
        case VirtualResolution::ScaleMode::FitMin:
            m_scale = std::min(scaleX, scaleY);
            break;
        case VirtualResolution::ScaleMode::FitMax:
            m_scale = std::max(scaleX, scaleY);
            break;
        case VirtualResolution::ScaleMode::Stretch:
            m_scale = 1.0F;  // No uniform scaling
            break;
    }

    // Center the virtual canvas
    const float scaledWidth = static_cast<float>(m_virtualRes.width) * m_scale;
    const float scaledHeight = static_cast<float>(m_virtualRes.height) * m_scale;
    m_offset.x = (static_cast<float>(width) - scaledWidth) * 0.5F;
    m_offset.y = (static_cast<float>(height) - scaledHeight) * 0.5F;

    // Mark layout dirty
    if (m_root)
    {
        m_root->MarkLayoutDirty();
    }
}

void UiTree::SetRoot(std::unique_ptr<UINode> root)
{
    // Root replacement invalidates all cached node pointers (hover/focus/pressed).
    m_hoveredNode = nullptr;
    m_pressedNode = nullptr;
    m_focusedNode = nullptr;
    m_focusableNodes.clear();
    m_mouseCaptured = false;
    m_lastNavAxis = glm::vec2(0.0F, 0.0F);

    m_root = std::move(root);
    if (m_root)
    {
        m_root->MarkLayoutDirty();
    }
    RebuildNodeIndex();
}

void UiTree::RebuildNodeIndex()
{
    m_nodeIndex.clear();
    if (!m_root)
    {
        return;
    }

    std::function<void(UINode*)> indexNode = [&](UINode* node) {
        if (!node->id.empty())
        {
            m_nodeIndex[node->id] = node;
        }
        for (const auto& child : node->children)
        {
            if (child)
            {
                indexNode(child.get());
            }
        }
    };
    indexNode(m_root.get());
}

UINode* UiTree::FindNode(const std::string& id)
{
    auto it = m_nodeIndex.find(id);
    if (it != m_nodeIndex.end())
    {
        return it->second;
    }
    return m_root ? m_root->FindDescendant(id) : nullptr;
}

const UINode* UiTree::FindNode(const std::string& id) const
{
    auto it = m_nodeIndex.find(id);
    if (it != m_nodeIndex.end())
    {
        return it->second;
    }
    return m_root ? m_root->FindDescendant(id) : nullptr;
}

void UiTree::SetStyleSheet(StyleSheet* styleSheet)
{
    m_styleSheet = styleSheet;
    if (m_root)
    {
        m_root->MarkStyleDirty();
    }
}

void UiTree::SetTokens(TokenCollection* tokens)
{
    m_tokens = tokens;
    if (m_root)
    {
        m_root->MarkStyleDirty();
    }
}

void UiTree::BindOnClick(const std::string& nodeId, OnClickCallback callback)
{
    m_callbacks[nodeId].onClick = std::move(callback);
}

void UiTree::BindOnClick(const std::string& nodeId, OnClickSimpleCallback callback)
{
    m_callbacks[nodeId].onClick = [cb = std::move(callback)](UINode&) {
        cb();
    };
}

void UiTree::BindOnValueChanged(const std::string& nodeId, OnValueChangedCallback callback)
{
    m_callbacks[nodeId].onValueChanged = std::move(callback);
}

void UiTree::BindOnTextChanged(const std::string& nodeId, OnTextChangedCallback callback)
{
    m_callbacks[nodeId].onTextChanged = std::move(callback);
}

void UiTree::BindOnFocus(const std::string& nodeId, OnFocusCallback callback)
{
    m_callbacks[nodeId].onFocus = std::move(callback);
}

void UiTree::BindOnBlur(const std::string& nodeId, OnBlurCallback callback)
{
    m_callbacks[nodeId].onBlur = std::move(callback);
}

void UiTree::ClearBindings(const std::string& nodeId)
{
    m_callbacks.erase(nodeId);
    m_sliderBindings.erase(nodeId);
    m_toggleBindings.erase(nodeId);
    m_textBindings.erase(nodeId);
    m_textProviders.erase(nodeId);
}

void UiTree::BindSliderValue(const std::string& nodeId, float* valuePtr)
{
    m_sliderBindings[nodeId] = valuePtr;
}

void UiTree::BindSlider(const std::string& nodeId, float* valuePtr, float minValue, float maxValue)
{
    m_sliderBindings[nodeId] = valuePtr;
    if (UINode* node = FindNode(nodeId))
    {
        node->minValue = minValue;
        node->maxValue = maxValue;
        if (valuePtr && maxValue > minValue)
        {
            node->state.value01 = std::clamp((*valuePtr - minValue) / (maxValue - minValue), 0.0F, 1.0F);
        }
    }
}

void UiTree::BindToggleValue(const std::string& nodeId, bool* valuePtr)
{
    m_toggleBindings[nodeId] = valuePtr;
}

void UiTree::BindToggle(const std::string& nodeId, bool* valuePtr)
{
    m_toggleBindings[nodeId] = valuePtr;
    if (UINode* node = FindNode(nodeId); node && valuePtr)
    {
        node->state.checked = *valuePtr;
    }
}

void UiTree::BindTextValue(const std::string& nodeId, std::string* valuePtr)
{
    m_textBindings[nodeId] = valuePtr;
}

void UiTree::BindText(const std::string& nodeId, TextProviderCallback callback)
{
    m_textProviders[nodeId] = std::move(callback);
}

bool UiTree::SetNodeVisibility(const std::string& nodeId, Visibility visibility)
{
    UINode* node = FindNode(nodeId);
    if (node == nullptr)
    {
        return false;
    }
    if (node->visibility == visibility)
    {
        return true;
    }
    node->visibility = visibility;
    node->MarkLayoutDirty();
    node->MarkStyleDirty();
    return true;
}

bool UiTree::ToggleNodeVisibility(const std::string& nodeId, Visibility hiddenMode)
{
    UINode* node = FindNode(nodeId);
    if (node == nullptr)
    {
        return false;
    }
    const Visibility nextVisibility = (node->visibility == Visibility::Visible) ? hiddenMode : Visibility::Visible;
    node->visibility = nextVisibility;
    node->MarkLayoutDirty();
    node->MarkStyleDirty();
    return true;
}

bool UiTree::TriggerClick(const std::string& nodeId)
{
    UINode* node = FindNode(nodeId);
    if (node == nullptr)
    {
        return false;
    }
    ProcessClick(*node);
    return true;
}

bool UiTree::IsFocusable(const UINode& node) const
{
    if (node.visibility != Visibility::Visible || node.layout.display == Display::None || node.state.disabled)
    {
        return false;
    }
    switch (node.type)
    {
        case UINodeType::Button:
        case UINodeType::Slider:
        case UINodeType::Toggle:
        case UINodeType::TextInput:
            return true;
        default:
            return false;
    }
}

void UiTree::GatherFocusableNodes(UINode& node, std::vector<UINode*>& outNodes)
{
    if (IsFocusable(node))
    {
        outNodes.push_back(&node);
    }
    for (auto& child : node.children)
    {
        if (child)
        {
            GatherFocusableNodes(*child, outNodes);
        }
    }
}

void UiTree::SetFocusedNode(UINode* node)
{
    if (m_focusedNode == node)
    {
        return;
    }

    if (m_focusedNode)
    {
        m_focusedNode->state.focused = false;
        m_focusedNode->MarkStyleDirty();
        auto prevIt = m_callbacks.find(m_focusedNode->id);
        if (prevIt != m_callbacks.end() && prevIt->second.onBlur)
        {
            prevIt->second.onBlur(*m_focusedNode);
        }
    }

    m_focusedNode = node;
    if (m_focusedNode)
    {
        m_focusedNode->state.focused = true;
        m_focusedNode->MarkStyleDirty();
        auto nextIt = m_callbacks.find(m_focusedNode->id);
        if (nextIt != m_callbacks.end() && nextIt->second.onFocus)
        {
            nextIt->second.onFocus(*m_focusedNode);
        }
    }
}

void UiTree::StepFocus(int step)
{
    if (m_focusableNodes.empty() || step == 0)
    {
        return;
    }

    auto it = std::find(m_focusableNodes.begin(), m_focusableNodes.end(), m_focusedNode);
    int index = 0;
    if (it != m_focusableNodes.end())
    {
        index = static_cast<int>(std::distance(m_focusableNodes.begin(), it));
    }
    index = (index + step) % static_cast<int>(m_focusableNodes.size());
    if (index < 0)
    {
        index += static_cast<int>(m_focusableNodes.size());
    }
    SetFocusedNode(m_focusableNodes[static_cast<std::size_t>(index)]);
}

UINode* UiTree::FindNearestFocusable(UINode* from, int dirX, int dirY) const
{
    if (!from || (dirX == 0 && dirY == 0))
    {
        return nullptr;
    }

    const glm::vec2 fromCenter{
        from->computedRect.x + from->computedRect.w * 0.5F,
        from->computedRect.y + from->computedRect.h * 0.5F
    };

    UINode* best = nullptr;
    float bestScore = std::numeric_limits<float>::max();

    for (UINode* candidate : m_focusableNodes)
    {
        if (!candidate || candidate == from)
        {
            continue;
        }

        const glm::vec2 candidateCenter{
            candidate->computedRect.x + candidate->computedRect.w * 0.5F,
            candidate->computedRect.y + candidate->computedRect.h * 0.5F
        };
        const glm::vec2 delta = candidateCenter - fromCenter;

        if (dirX != 0)
        {
            if ((dirX > 0 && delta.x <= 0.0F) || (dirX < 0 && delta.x >= 0.0F))
            {
                continue;
            }
        }
        if (dirY != 0)
        {
            if ((dirY > 0 && delta.y <= 0.0F) || (dirY < 0 && delta.y >= 0.0F))
            {
                continue;
            }
        }

        // Prefer candidates in requested direction, penalize orthogonal distance.
        const float primary = dirX != 0 ? std::abs(delta.x) : std::abs(delta.y);
        const float secondary = dirX != 0 ? std::abs(delta.y) : std::abs(delta.x);
        const float score = primary + secondary * 0.6F;

        if (score < bestScore)
        {
            bestScore = score;
            best = candidate;
        }
    }

    return best;
}

void UiTree::MoveFocus(int dirX, int dirY)
{
    if (m_focusableNodes.empty())
    {
        SetFocusedNode(nullptr);
        return;
    }
    if (!m_focusedNode || !IsFocusable(*m_focusedNode))
    {
        SetFocusedNode(m_focusableNodes.front());
        return;
    }

    if (UINode* nearest = FindNearestFocusable(m_focusedNode, dirX, dirY))
    {
        SetFocusedNode(nearest);
        return;
    }

    // Fallback to simple cycling if no directional candidate found.
    if (dirX > 0 || dirY > 0)
    {
        StepFocus(1);
    }
    else if (dirX < 0 || dirY < 0)
    {
        StepFocus(-1);
    }
}

glm::vec2 UiTree::VirtualToScreen(float vx, float vy) const
{
    return glm::vec2(vx * m_scale + m_offset.x, vy * m_scale + m_offset.y);
}

glm::vec2 UiTree::ScreenToVirtual(float sx, float sy) const
{
    return glm::vec2((sx - m_offset.x) / m_scale, (sy - m_offset.y) / m_scale);
}

glm::vec2 UiTree::GetVirtualMousePos(float screenMouseX, float screenMouseY) const
{
    return ScreenToVirtual(screenMouseX, screenMouseY);
}

void UiTree::ProcessInput(const platform::Input* input, float deltaSeconds)
{
    (void)deltaSeconds;
    if (!m_root || !input)
    {
        return;
    }

    m_focusableNodes.clear();
    GatherFocusableNodes(*m_root, m_focusableNodes);
    if (m_focusedNode && !IsFocusable(*m_focusedNode))
    {
        SetFocusedNode(nullptr);
    }
    if (!m_focusedNode && !m_focusableNodes.empty())
    {
        SetFocusedNode(m_focusableNodes.front());
    }

    // Pull external bindings into node state before processing interactions.
    for (auto& [id, ptr] : m_sliderBindings)
    {
        UINode* node = FindNode(id);
        if (node && ptr && node->maxValue > node->minValue)
        {
            node->state.value01 = std::clamp((*ptr - node->minValue) / (node->maxValue - node->minValue), 0.0F, 1.0F);
        }
    }
    for (auto& [id, ptr] : m_toggleBindings)
    {
        UINode* node = FindNode(id);
        if (node && ptr)
        {
            node->state.checked = *ptr;
        }
    }
    for (auto& [id, ptr] : m_textBindings)
    {
        UINode* node = FindNode(id);
        if (node && ptr)
        {
            node->state.text = *ptr;
        }
    }
    for (auto& [id, provider] : m_textProviders)
    {
        if (!provider)
        {
            continue;
        }
        UINode* node = FindNode(id);
        if (!node)
        {
            continue;
        }
        const std::string nextText = provider();
        if (node->text != nextText)
        {
            node->text = nextText;
            node->MarkLayoutDirty();
        }
    }

    // Get mouse position in virtual coordinates
    float mouseX = 0.0F;
    float mouseY = 0.0F;
    {
        glm::vec2 mouse = input->MousePosition();
        glm::vec2 virtualMouse = ScreenToVirtual(mouse.x, mouse.y);
        mouseX = virtualMouse.x;
        mouseY = virtualMouse.y;
    }

    // Clear hover state from all nodes
    std::function<void(UINode*)> clearHover = [&](UINode* node) {
        node->state.hover = false;
        for (const auto& child : node->children)
        {
            if (child)
            {
                clearHover(child.get());
            }
        }
    };
    clearHover(m_root.get());

    // Hit test from root
    UINode* newHovered = HitTest(*m_root, mouseX, mouseY);

    // Update hover state
    if (newHovered != m_hoveredNode)
    {
        if (m_hoveredNode)
        {
            m_hoveredNode->state.hover = false;
            m_hoveredNode->MarkStyleDirty();
        }
        m_hoveredNode = newHovered;
        if (m_hoveredNode)
        {
            m_hoveredNode->state.hover = true;
            m_hoveredNode->MarkStyleDirty();
        }
    }

    // Handle mouse press
    if (input->IsMousePressed(GLFW_MOUSE_BUTTON_LEFT))
    {
        if (m_hoveredNode)
        {
            m_pressedNode = m_hoveredNode;
            m_pressedNode->state.pressed = true;
            m_pressedNode->MarkStyleDirty();
            m_mouseCaptured = true;

            // Focus handling
            if (m_focusedNode != m_pressedNode)
            {
                SetFocusedNode(m_pressedNode);
            }
        }
        else
        {
            // Clicked on empty space - blur
            SetFocusedNode(nullptr);
        }
    }

    // Handle mouse release
    if (input->IsMouseReleased(GLFW_MOUSE_BUTTON_LEFT))
    {
        if (m_pressedNode)
        {
            m_pressedNode->state.pressed = false;
            m_pressedNode->MarkStyleDirty();

            // Check if released on same node (click)
            if (m_pressedNode == m_hoveredNode)
            {
                ProcessClick(*m_pressedNode);
            }

            m_pressedNode = nullptr;
        }
        m_mouseCaptured = false;
    }

    // Keyboard + gamepad directional focus navigation.
    const bool navLeft = input->IsKeyPressed(GLFW_KEY_LEFT)
                      || input->IsGamepadButtonPressed(GLFW_GAMEPAD_BUTTON_DPAD_LEFT);
    const bool navRight = input->IsKeyPressed(GLFW_KEY_RIGHT)
                       || input->IsGamepadButtonPressed(GLFW_GAMEPAD_BUTTON_DPAD_RIGHT);
    const bool navUp = input->IsKeyPressed(GLFW_KEY_UP)
                    || input->IsGamepadButtonPressed(GLFW_GAMEPAD_BUTTON_DPAD_UP);
    const bool navDown = input->IsKeyPressed(GLFW_KEY_DOWN)
                      || input->IsGamepadButtonPressed(GLFW_GAMEPAD_BUTTON_DPAD_DOWN);
    const bool navNext = input->IsKeyPressed(GLFW_KEY_TAB)
                      || input->IsGamepadButtonPressed(GLFW_GAMEPAD_BUTTON_RIGHT_BUMPER);
    const bool navPrev = (input->IsKeyPressed(GLFW_KEY_TAB)
                       && (input->IsKeyDown(GLFW_KEY_LEFT_SHIFT) || input->IsKeyDown(GLFW_KEY_RIGHT_SHIFT)))
                      || input->IsGamepadButtonPressed(GLFW_GAMEPAD_BUTTON_LEFT_BUMPER);

    const float stickX = input->GamepadAxis(GLFW_GAMEPAD_AXIS_LEFT_X, 0.55F);
    const float stickY = input->GamepadAxis(GLFW_GAMEPAD_AXIS_LEFT_Y, 0.55F);

    if (navLeft)
        MoveFocus(-1, 0);
    if (navRight)
        MoveFocus(1, 0);
    if (navUp)
        MoveFocus(0, -1);
    if (navDown)
        MoveFocus(0, 1);
    if (navNext && !navPrev)
        StepFocus(1);
    if (navPrev)
        StepFocus(-1);

    if (stickX <= -0.55F && m_lastNavAxis.x > -0.55F)
        MoveFocus(-1, 0);
    else if (stickX >= 0.55F && m_lastNavAxis.x < 0.55F)
        MoveFocus(1, 0);
    if (stickY <= -0.55F && m_lastNavAxis.y > -0.55F)
        MoveFocus(0, -1);
    else if (stickY >= 0.55F && m_lastNavAxis.y < 0.55F)
        MoveFocus(0, 1);
    m_lastNavAxis = glm::vec2(stickX, stickY);

    const bool activateFocused = input->IsKeyPressed(GLFW_KEY_ENTER)
                              || input->IsKeyPressed(GLFW_KEY_SPACE)
                              || input->IsGamepadButtonPressed(GLFW_GAMEPAD_BUTTON_A);
    if (activateFocused && m_focusedNode)
    {
        ProcessClick(*m_focusedNode);
    }
    const bool cancelFocus = input->IsKeyPressed(GLFW_KEY_ESCAPE)
                          || input->IsGamepadButtonPressed(GLFW_GAMEPAD_BUTTON_B);
    if (cancelFocus && !m_hoveredNode)
    {
        SetFocusedNode(nullptr);
    }

    // Handle slider dragging
    if (m_pressedNode && m_pressedNode->type == UINodeType::Slider && input->IsMouseDown(GLFW_MOUSE_BUTTON_LEFT))
    {
        ProcessSliderDrag(*m_pressedNode, mouseX);
    }

    // Sync bound values
    for (auto& [id, ptr] : m_sliderBindings)
    {
        UINode* node = FindNode(id);
        if (node && ptr)
        {
            *ptr = node->minValue + node->state.value01 * (node->maxValue - node->minValue);
        }
    }
    for (auto& [id, ptr] : m_toggleBindings)
    {
        UINode* node = FindNode(id);
        if (node && ptr)
        {
            *ptr = node->state.checked;
        }
    }
    for (auto& [id, ptr] : m_textBindings)
    {
        UINode* node = FindNode(id);
        if (node && ptr)
        {
            *ptr = node->state.text;
        }
    }
}

UINode* UiTree::HitTest(UINode& node, float vx, float vy)
{
    // Skip invisible nodes
    if (node.visibility == Visibility::Hidden || node.visibility == Visibility::Collapsed)
    {
        return nullptr;
    }

    // Skip if display is none
    if (node.layout.display == Display::None)
    {
        return nullptr;
    }

    // Check children first (reverse order for z-index)
    for (auto it = node.children.rbegin(); it != node.children.rend(); ++it)
    {
        if (!(*it))
        {
            continue;
        }
        UINode* hit = HitTest(**it, vx, vy);
        if (hit)
        {
            return hit;
        }
    }

    // Check self
    if (node.computedRect.Contains(vx, vy))
    {
        return &node;
    }

    return nullptr;
}

void UiTree::ProcessClick(UINode& node)
{
    auto forEachNode = [&](const std::function<void(UINode&)>& fn) {
        if (!m_root)
        {
            return;
        }
        std::function<void(UINode&)> visit = [&](UINode& current) {
            fn(current);
            for (auto& child : current.children)
            {
                if (child)
                {
                    visit(*child);
                }
            }
        };
        visit(*m_root);
    };

    // Built-in data-driven behavior (tabs/visibility) executed before external callbacks.
    if (!node.onClickButtonGroupClass.empty())
    {
        forEachNode([&](UINode& n) {
            if (n.HasClass(node.onClickButtonGroupClass))
            {
                n.state.selected = false;
                n.MarkStyleDirty();
            }
        });
        node.state.selected = true;
        node.MarkStyleDirty();
    }

    if (!node.onClickTargetId.empty())
    {
        UINode* target = FindNode(node.onClickTargetId);
        if (target != nullptr)
        {
            if (!node.onClickTabGroupClass.empty())
            {
                forEachNode([&](UINode& n) {
                    if (n.HasClass(node.onClickTabGroupClass))
                    {
                        n.visibility = Visibility::Collapsed;
                        n.MarkLayoutDirty();
                        n.MarkStyleDirty();
                    }
                });
                target->visibility = Visibility::Visible;
                target->MarkLayoutDirty();
                target->MarkStyleDirty();
            }
            else if (node.onClickToggleTarget)
            {
                const Visibility nextVisibility = (target->visibility == Visibility::Visible)
                    ? Visibility::Collapsed
                    : Visibility::Visible;
                target->visibility = nextVisibility;
                target->MarkLayoutDirty();
                target->MarkStyleDirty();
            }
            else
            {
                target->visibility = Visibility::Visible;
                target->MarkLayoutDirty();
                target->MarkStyleDirty();
            }
        }
    }

    // Handle different node types
    switch (node.type)
    {
        case UINodeType::Button:
        {
            // Invoke callback
            auto it = m_callbacks.find(node.id);
            if (it != m_callbacks.end() && it->second.onClick)
            {
                it->second.onClick(node);
            }
            break;
        }

        case UINodeType::Toggle:
        {
            node.state.checked = !node.state.checked;
            node.MarkStyleDirty();
            // Invoke callback
            auto it = m_callbacks.find(node.id);
            if (it != m_callbacks.end() && it->second.onValueChanged)
            {
                it->second.onValueChanged(node, node.state.checked ? 1.0F : 0.0F);
            }
            break;
        }

        default:
            break;
    }
}

void UiTree::ProcessSliderDrag(UINode& node, float mouseX)
{
    const float x = node.computedRect.contentX;
    const float w = node.computedRect.contentW;

    if (w > 0.0F)
    {
        float t = (mouseX - x) / w;
        t = std::clamp(t, 0.0F, 1.0F);
        node.state.value01 = t;
        node.MarkStyleDirty();

        // Invoke callback
        auto it = m_callbacks.find(node.id);
        if (it != m_callbacks.end() && it->second.onValueChanged)
        {
            float value = node.minValue + t * (node.maxValue - node.minValue);
            it->second.onValueChanged(node, value);
        }
    }
}

void UiTree::ComputeLayout()
{
    if (!m_root)
    {
        return;
    }

    // Apply styles first
    ApplyStyleToTree(*m_root);

    // Measure pass
    MeasureNode(*m_root);

    // Arrange pass
    ArrangeNode(*m_root, 0.0F, 0.0F, static_cast<float>(m_virtualRes.width), static_cast<float>(m_virtualRes.height));
}

void UiTree::ApplyStyleToTree(UINode& node)
{
    if (!node.styleDirty && !node.layoutDirty)
    {
        // Just recurse
        for (const auto& child : node.children)
        {
            if (child)
            {
                ApplyStyleToTree(*child);
            }
        }
        return;
    }

    // Rebuild computed style from a clean baseline so state transitions don't leave stale values.
    ResetComputedStyle(node);

    // Apply stylesheet rules if available
    if (m_styleSheet && m_tokens)
    {
        auto rules = m_styleSheet->MatchRules(node, *m_tokens);
        for (const StyleRule* rule : rules)
        {
            ApplyStyleToNode(*rule, node, *m_tokens);
        }
    }

    // Apply inline overrides (highest priority)
    if (node.backgroundColor.has_value())
        node.computedBackgroundColor = *node.backgroundColor;
    if (node.textColor.has_value())
        node.computedTextColor = *node.textColor;
    if (node.opacity.has_value())
        node.computedOpacity = *node.opacity;
    if (node.radius.has_value())
        node.computedRadius = *node.radius;
    if (node.strokeColor.has_value())
        node.computedStrokeColor = *node.strokeColor;
    if (node.strokeWidth.has_value())
        node.computedStrokeWidth = *node.strokeWidth;
    if (node.shadow.has_value())
        node.computedShadow = *node.shadow;
    if (node.font.has_value())
        node.computedFont = *node.font;

    node.styleDirty = false;

    // Recurse
    for (const auto& child : node.children)
    {
        if (child)
        {
            ApplyStyleToTree(*child);
        }
    }
}

void UiTree::MeasureNode(UINode& node)
{
    if (node.visibility == Visibility::Collapsed || node.layout.display == Display::None)
    {
        return;
    }

    // Measure children first
    for (const auto& child : node.children)
    {
        if (child)
        {
            MeasureNode(*child);
        }
    }

    const auto measuredChildWidth = [](const UINode& child) {
        if (child.layout.width.IsFixed())
        {
            return child.layout.width.value;
        }
        return child.measuredWidth;
    };
    const auto measuredChildHeight = [](const UINode& child) {
        if (child.layout.height.IsFixed())
        {
            return child.layout.height.value;
        }
        return child.measuredHeight;
    };

    // Calculate intrinsic content size (without this node's padding)
    float intrinsicContentWidth = 0.0F;
    float intrinsicContentHeight = 0.0F;

    switch (node.type)
    {
        case UINodeType::Text:
        case UINodeType::Button:
        case UINodeType::TextInput:
        {
            intrinsicContentWidth = EstimateTextContentWidth(node);
            intrinsicContentHeight = EstimateTextContentHeight(node);
            break;
        }

        case UINodeType::Spacer:
        {
            break;
        }

        case UINodeType::Panel:
        case UINodeType::Container:
        case UINodeType::ScrollView:
        default:
        {
            std::vector<const UINode*> flowChildren;
            flowChildren.reserve(node.children.size());
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
                flowChildren.push_back(child.get());
            }

            if (node.layout.display == Display::Grid)
            {
                float maxItemW = 0.0F;
                float maxItemH = 0.0F;
                int explicitRowsNeeded = 0;
                int templateColumns = 0;
                int templateRows = 0;
                if (!node.layout.gridTemplateAreas.empty())
                {
                    std::istringstream lineStream(node.layout.gridTemplateAreas);
                    std::string line;
                    while (std::getline(lineStream, line))
                    {
                        std::istringstream tokenStream(line);
                        int tokenCount = 0;
                        std::string token;
                        while (tokenStream >> token)
                        {
                            ++tokenCount;
                        }
                        if (tokenCount > 0)
                        {
                            templateColumns = std::max(templateColumns, tokenCount);
                            ++templateRows;
                        }
                    }
                }
                for (const UINode* child : flowChildren)
                {
                    const float childW = measuredChildWidth(*child) + child->layout.margin.left + child->layout.margin.right;
                    const float childH = measuredChildHeight(*child) + child->layout.margin.top + child->layout.margin.bottom;
                    maxItemW = std::max(maxItemW, childW / static_cast<float>(std::max(1, child->layout.gridColumnSpan)));
                    maxItemH = std::max(maxItemH, childH / static_cast<float>(std::max(1, child->layout.gridRowSpan)));
                    if (child->layout.gridRowStart > 0)
                    {
                        explicitRowsNeeded = std::max(explicitRowsNeeded, child->layout.gridRowStart - 1 + std::max(1, child->layout.gridRowSpan));
                    }
                }
                const int columns = std::max({1, node.layout.gridColumns, templateColumns});
                const int rows = std::max(1, node.layout.gridRows > 0
                    ? node.layout.gridRows
                    : std::max(
                        static_cast<int>((flowChildren.size() + static_cast<std::size_t>(columns) - 1) / static_cast<std::size_t>(columns)),
                        std::max(templateRows, explicitRowsNeeded)));
                const float colGap = node.layout.gridColumnGap >= 0.0F ? node.layout.gridColumnGap : node.layout.gap;
                const float rowGap = node.layout.gridRowGap >= 0.0F ? node.layout.gridRowGap : node.layout.gap;
                intrinsicContentWidth = maxItemW * static_cast<float>(columns) + std::max(0, columns - 1) * std::max(0.0F, colGap);
                intrinsicContentHeight = maxItemH * static_cast<float>(rows) + std::max(0, rows - 1) * std::max(0.0F, rowGap);
            }
            else
            {
                const bool rowLayout = node.layout.display == Display::Flex
                    && (node.layout.flexDirection == FlexDirection::Row || node.layout.flexDirection == FlexDirection::RowReverse);
                const float gap = std::max(0.0F, node.layout.gap);
                if (rowLayout)
                {
                    for (const UINode* child : flowChildren)
                    {
                        const float childW = measuredChildWidth(*child) + child->layout.margin.left + child->layout.margin.right;
                        const float childH = measuredChildHeight(*child) + child->layout.margin.top + child->layout.margin.bottom;
                        intrinsicContentWidth += childW;
                        intrinsicContentHeight = std::max(intrinsicContentHeight, childH);
                    }
                    intrinsicContentWidth += gap * static_cast<float>(std::max(0, static_cast<int>(flowChildren.size()) - 1));
                }
                else
                {
                    for (const UINode* child : flowChildren)
                    {
                        const float childW = measuredChildWidth(*child) + child->layout.margin.left + child->layout.margin.right;
                        const float childH = measuredChildHeight(*child) + child->layout.margin.top + child->layout.margin.bottom;
                        intrinsicContentWidth = std::max(intrinsicContentWidth, childW);
                        intrinsicContentHeight += childH;
                    }
                    intrinsicContentHeight += gap * static_cast<float>(std::max(0, static_cast<int>(flowChildren.size()) - 1));
                }
            }
            break;
        }
    }

    float intrinsicWidth = intrinsicContentWidth + std::max(0.0F, node.layout.padding.left + node.layout.padding.right);
    float intrinsicHeight = intrinsicContentHeight + std::max(0.0F, node.layout.padding.top + node.layout.padding.bottom);

    if (node.layout.minWidth.IsFixed())
    {
        intrinsicWidth = std::max(intrinsicWidth, node.layout.minWidth.value);
    }
    if (node.layout.maxWidth.IsFixed())
    {
        intrinsicWidth = std::min(intrinsicWidth, node.layout.maxWidth.value);
    }
    if (node.layout.minHeight.IsFixed())
    {
        intrinsicHeight = std::max(intrinsicHeight, node.layout.minHeight.value);
    }
    if (node.layout.maxHeight.IsFixed())
    {
        intrinsicHeight = std::min(intrinsicHeight, node.layout.maxHeight.value);
    }

    node.measuredWidth = std::max(0.0F, intrinsicWidth);
    node.measuredHeight = std::max(0.0F, intrinsicHeight);
    node.layoutDirty = false;
}

void UiTree::ArrangeNode(UINode& node, float x, float y, float availableWidth, float availableHeight)
{
    if (node.visibility == Visibility::Collapsed || node.layout.display == Display::None)
    {
        node.computedRect = ComputedRect{};
        return;
    }

    const bool textLikeNode = node.type == UINodeType::Text || node.type == UINodeType::Button || node.type == UINodeType::TextInput;
    const auto shouldAutoFillAxis = [&](bool horizontal) {
        if (node.parent == nullptr)
        {
            return true;
        }
        if (node.layout.position == Position::Absolute)
        {
            return false;
        }
        const UINode& parent = *node.parent;
        if (parent.layout.display == Display::Flex)
        {
            const bool parentRow = parent.layout.flexDirection == FlexDirection::Row || parent.layout.flexDirection == FlexDirection::RowReverse;
            const bool mainAxis = horizontal ? parentRow : !parentRow;
            if (textLikeNode)
            {
                // Keep text-like elements content-sized unless explicit width/height/flex-basis says otherwise.
                const SizeValue& axisValue = horizontal ? node.layout.width : node.layout.height;
                if (axisValue.unit == SizeValue::Unit::Auto)
                {
                    if (mainAxis)
                    {
                        return node.layout.flexGrow > 0.001F && node.layout.flexBasis.unit != SizeValue::Unit::Auto;
                    }
                    return false;
                }
            }
            if (mainAxis)
            {
                return true;
            }
            return parent.layout.alignItems == AlignItems::Stretch;
        }
        if (parent.layout.display == Display::Grid)
        {
            if (textLikeNode)
            {
                const SizeValue& axisValue = horizontal ? node.layout.width : node.layout.height;
                if (axisValue.unit == SizeValue::Unit::Auto)
                {
                    return false;
                }
            }
            return horizontal
                ? parent.layout.gridJustifyItems == GridItemAlign::Stretch
                : parent.layout.gridAlignItems == GridItemAlign::Stretch;
        }
        return false;
    };
    const float fallbackMeasuredWidth = textLikeNode
        ? (EstimateTextContentWidth(node) + std::max(0.0F, node.layout.padding.left + node.layout.padding.right))
        : std::max(1.0F, availableWidth);
    const float fallbackMeasuredHeight = textLikeNode
        ? (EstimateTextContentHeight(node) + std::max(0.0F, node.layout.padding.top + node.layout.padding.bottom))
        : std::max(1.0F, availableHeight);
    const float autoMeasuredWidth = node.measuredWidth > 0.0F ? node.measuredWidth : fallbackMeasuredWidth;
    const float autoMeasuredHeight = node.measuredHeight > 0.0F ? node.measuredHeight : fallbackMeasuredHeight;

    // Calculate node size
    float width = availableWidth;
    float height = availableHeight;

    // Apply width
    switch (node.layout.width.unit)
    {
        case SizeValue::Unit::Px:
            width = node.layout.width.value;
            break;
        case SizeValue::Unit::Percent:
            width = availableWidth * node.layout.width.value / 100.0F;
            break;
        case SizeValue::Unit::Vw:
            width = static_cast<float>(m_virtualRes.width) * node.layout.width.value / 100.0F;
            break;
        case SizeValue::Unit::Vh:
            width = static_cast<float>(m_virtualRes.height) * node.layout.width.value / 100.0F;
            break;
        case SizeValue::Unit::Auto:
            width = shouldAutoFillAxis(true) ? availableWidth : autoMeasuredWidth;
            break;
        default:
            break;
    }

    // Apply height
    switch (node.layout.height.unit)
    {
        case SizeValue::Unit::Px:
            height = node.layout.height.value;
            break;
        case SizeValue::Unit::Percent:
            height = availableHeight * node.layout.height.value / 100.0F;
            break;
        case SizeValue::Unit::Vw:
            height = static_cast<float>(m_virtualRes.width) * node.layout.height.value / 100.0F;
            break;
        case SizeValue::Unit::Vh:
            height = static_cast<float>(m_virtualRes.height) * node.layout.height.value / 100.0F;
            break;
        case SizeValue::Unit::Auto:
            height = shouldAutoFillAxis(false) ? availableHeight : autoMeasuredHeight;
            break;
        default:
            break;
    }

    // Clamp to min/max
    if (node.layout.minWidth.IsFixed())
        width = std::max(width, node.layout.minWidth.value);
    if (node.layout.maxWidth.IsFixed())
        width = std::min(width, node.layout.maxWidth.value);
    if (node.layout.minHeight.IsFixed())
        height = std::max(height, node.layout.minHeight.value);
    if (node.layout.maxHeight.IsFixed())
        height = std::min(height, node.layout.maxHeight.value);

    if (textLikeNode)
    {
        // Prevent text content from overflowing its own bounds when font size/family changes.
        width = std::max(width, autoMeasuredWidth);
        height = std::max(height, autoMeasuredHeight);
    }

    // Apply margins
    x += node.layout.margin.left;
    y += node.layout.margin.top;
    width -= node.layout.margin.left + node.layout.margin.right;
    height -= node.layout.margin.top + node.layout.margin.bottom;

    // Set computed rect
    node.computedRect.x = x;
    node.computedRect.y = y;
    node.computedRect.w = std::max(0.0F, width);
    node.computedRect.h = std::max(0.0F, height);

    // Calculate content rect (after padding)
    node.computedRect.contentX = x + node.layout.padding.left;
    node.computedRect.contentY = y + node.layout.padding.top;
    node.computedRect.contentW = std::max(0.0F, width - node.layout.padding.left - node.layout.padding.right);
    node.computedRect.contentH = std::max(0.0F, height - node.layout.padding.top - node.layout.padding.bottom);

    const auto estimatedWidth = [&](const UINode& child) {
        if (child.layout.width.IsFixed())
        {
            return std::max(0.0F, child.layout.width.value);
        }
        if (child.measuredWidth > 0.0F)
        {
            return child.measuredWidth;
        }
        const float fallback = EstimateTextContentWidth(child) + std::max(0.0F, child.layout.padding.left + child.layout.padding.right);
        return std::max(1.0F, fallback);
    };
    const auto estimatedHeight = [&](const UINode& child) {
        if (child.layout.height.IsFixed())
        {
            return std::max(0.0F, child.layout.height.value);
        }
        if (child.measuredHeight > 0.0F)
        {
            return child.measuredHeight;
        }
        const float fallback = EstimateTextContentHeight(child) + std::max(0.0F, child.layout.padding.top + child.layout.padding.bottom);
        return std::max(1.0F, fallback);
    };
    const auto resolveSize = [&](const SizeValue& value, float reference, float autoFallback) {
        switch (value.unit)
        {
            case SizeValue::Unit::Px:
                return value.value;
            case SizeValue::Unit::Percent:
                return reference * value.value / 100.0F;
            case SizeValue::Unit::Vw:
                return static_cast<float>(m_virtualRes.width) * value.value / 100.0F;
            case SizeValue::Unit::Vh:
                return static_cast<float>(m_virtualRes.height) * value.value / 100.0F;
            case SizeValue::Unit::Auto:
            default:
                return autoFallback;
        }
    };

    auto arrangeAbsoluteChildren = [&]() {
        for (std::size_t i = 0; i < node.children.size(); ++i)
        {
            if (!node.children[i])
            {
                continue;
            }
            UINode& child = *node.children[i];
            if (child.visibility == Visibility::Collapsed || child.layout.display == Display::None || child.layout.position != Position::Absolute)
            {
                continue;
            }

            float childW = child.layout.width.IsFixed() ? child.layout.width.value : estimatedWidth(child);
            float childH = child.layout.height.IsFixed() ? child.layout.height.value : estimatedHeight(child);

            if (child.layout.width.unit == SizeValue::Unit::Percent)
            {
                childW = node.computedRect.contentW * child.layout.width.value / 100.0F;
            }
            if (child.layout.height.unit == SizeValue::Unit::Percent)
            {
                childH = node.computedRect.contentH * child.layout.height.value / 100.0F;
            }

            float childX = node.computedRect.contentX + child.layout.offset.x;
            float childY = node.computedRect.contentY + child.layout.offset.y;

            if (child.layout.anchor.has_value())
            {
                const glm::vec2 anchor = *child.layout.anchor;
                const float anchorX = node.computedRect.contentX + node.computedRect.contentW * anchor.x + child.layout.offset.x;
                const float anchorY = node.computedRect.contentY + node.computedRect.contentH * anchor.y + child.layout.offset.y;
                childX = anchorX - childW * child.layout.pivot.x;
                childY = anchorY - childH * child.layout.pivot.y;
            }

            ArrangeNode(child, childX, childY, childW, childH);
        }
    };

    if (!node.children.empty())
    {
        if (node.layout.display == Display::Flex)
        {
            const bool isRow = (node.layout.flexDirection == FlexDirection::Row || node.layout.flexDirection == FlexDirection::RowReverse);
            const bool reverseMain = (node.layout.flexDirection == FlexDirection::RowReverse || node.layout.flexDirection == FlexDirection::ColumnReverse);
            const float mainSize = isRow ? node.computedRect.contentW : node.computedRect.contentH;
            const float crossSize = isRow ? node.computedRect.contentH : node.computedRect.contentW;

            std::vector<std::size_t> flowChildren;
            flowChildren.reserve(node.children.size());
            std::vector<float> baseMain(node.children.size(), 0.0F);
            std::vector<float> finalMain(node.children.size(), 0.0F);

            float totalBaseMain = 0.0F;
            float totalFlexGrow = 0.0F;
            float totalFlexShrinkFactor = 0.0F;

            for (std::size_t i = 0; i < node.children.size(); ++i)
            {
                if (!node.children[i])
                {
                    continue;
                }
                UINode& child = *node.children[i];
                if (child.visibility == Visibility::Collapsed || child.layout.display == Display::None || child.layout.position == Position::Absolute)
                {
                    continue;
                }

                flowChildren.push_back(i);

                const SizeValue& mainValue = isRow ? child.layout.width : child.layout.height;
                const float autoMain = isRow ? estimatedWidth(child) : estimatedHeight(child);
                float basis = child.layout.flexBasis.IsAuto()
                    ? resolveSize(mainValue, mainSize, autoMain)
                    : resolveSize(child.layout.flexBasis, mainSize, autoMain);
                const bool childTextLike = child.type == UINodeType::Text || child.type == UINodeType::Button || child.type == UINodeType::TextInput;
                if (childTextLike)
                {
                    basis = std::max(basis, autoMain);
                }
                const float mainBase = std::max(0.0F, basis);

                baseMain[i] = mainBase;
                finalMain[i] = mainBase;
                totalBaseMain += mainBase;
                totalFlexGrow += std::max(0.0F, child.layout.flexGrow);
                totalFlexShrinkFactor += std::max(0.0F, child.layout.flexShrink) * mainBase;
            }

            const float baseGap = std::max(0.0F, node.layout.gap);
            const float gapTotal = baseGap * static_cast<float>(std::max(0, static_cast<int>(flowChildren.size()) - 1));
            const float remainingMain = mainSize - totalBaseMain - gapTotal;

            if (remainingMain > 0.0F && totalFlexGrow > 0.0F)
            {
                for (std::size_t index : flowChildren)
                {
                    UINode& child = *node.children[index];
                    const float grow = std::max(0.0F, child.layout.flexGrow);
                    finalMain[index] += remainingMain * (grow / totalFlexGrow);
                }
            }
            else if (remainingMain < 0.0F && totalFlexShrinkFactor > 0.0F)
            {
                const float deficit = -remainingMain;
                for (std::size_t index : flowChildren)
                {
                    UINode& child = *node.children[index];
                    const float shrinkFactor = std::max(0.0F, child.layout.flexShrink) * baseMain[index];
                    const float shrinkAmount = deficit * (shrinkFactor / totalFlexShrinkFactor);
                    finalMain[index] = std::max(0.0F, finalMain[index] - shrinkAmount);
                }
            }

            float sumFinalMain = 0.0F;
            for (std::size_t index : flowChildren)
            {
                sumFinalMain += finalMain[index];
            }

            const float occupiedMain = sumFinalMain + gapTotal;
            const float freeMain = mainSize - occupiedMain;
            float justifyOffset = 0.0F;
            float gapSpacing = baseGap;
            switch (node.layout.justifyContent)
            {
                case JustifyContent::FlexEnd:
                    justifyOffset = std::max(0.0F, freeMain);
                    break;
                case JustifyContent::Center:
                    justifyOffset = std::max(0.0F, freeMain * 0.5F);
                    break;
                case JustifyContent::SpaceBetween:
                    if (flowChildren.size() > 1)
                    {
                        gapSpacing += std::max(0.0F, freeMain / static_cast<float>(flowChildren.size() - 1));
                    }
                    break;
                case JustifyContent::SpaceAround:
                    if (!flowChildren.empty())
                    {
                        gapSpacing += std::max(0.0F, freeMain / static_cast<float>(flowChildren.size()));
                        justifyOffset = gapSpacing * 0.5F;
                    }
                    break;
                case JustifyContent::SpaceEvenly:
                    if (!flowChildren.empty())
                    {
                        gapSpacing += std::max(0.0F, freeMain / static_cast<float>(flowChildren.size() + 1));
                        justifyOffset = gapSpacing;
                    }
                    break;
                default:
                    break;
            }

            float cursorMain = reverseMain ? (mainSize - justifyOffset) : justifyOffset;
            const auto placeFlowChild = [&](std::size_t index, float& cursor) {
                UINode& child = *node.children[index];
                const SizeValue& mainValue = isRow ? child.layout.width : child.layout.height;
                const SizeValue& crossValue = isRow ? child.layout.height : child.layout.width;
                const float desiredMain = std::max(0.0F, finalMain[index]);
                const float autoCross = isRow ? estimatedHeight(child) : estimatedWidth(child);
                float desiredCross = resolveSize(crossValue, crossSize, autoCross);
                const bool childTextLike = child.type == UINodeType::Text || child.type == UINodeType::Button || child.type == UINodeType::TextInput;
                if (crossValue.unit == SizeValue::Unit::Auto
                    && node.layout.alignItems == AlignItems::Stretch
                    && !childTextLike)
                {
                    desiredCross = crossSize;
                }
                desiredCross = std::max(0.0F, desiredCross);

                float mainAvail = desiredMain;
                if (mainValue.unit == SizeValue::Unit::Percent)
                {
                    mainAvail = (std::abs(mainValue.value) > 0.001F)
                        ? (desiredMain * 100.0F / mainValue.value)
                        : mainSize;
                }

                float crossAvail = desiredCross;
                if (crossValue.unit == SizeValue::Unit::Percent)
                {
                    crossAvail = (std::abs(crossValue.value) > 0.001F)
                        ? (desiredCross * 100.0F / crossValue.value)
                        : crossSize;
                }

                float mainPos = cursor;
                if (reverseMain)
                {
                    cursor -= desiredMain;
                    mainPos = cursor;
                }

                float crossOffset = 0.0F;
                switch (node.layout.alignItems)
                {
                    case AlignItems::Center:
                        crossOffset = (crossSize - desiredCross) * 0.5F;
                        break;
                    case AlignItems::FlexEnd:
                        crossOffset = crossSize - desiredCross;
                        break;
                    default:
                        break;
                }

                const float childX = isRow ? (node.computedRect.contentX + mainPos) : (node.computedRect.contentX + crossOffset);
                const float childY = isRow ? (node.computedRect.contentY + crossOffset) : (node.computedRect.contentY + mainPos);
                ArrangeNode(child, childX, childY, isRow ? mainAvail : crossAvail, isRow ? crossAvail : mainAvail);

                if (reverseMain)
                {
                    cursor -= gapSpacing;
                }
                else
                {
                    cursor += desiredMain + gapSpacing;
                }
            };

            for (std::size_t index : flowChildren)
            {
                placeFlowChild(index, cursorMain);
            }
        }
        else if (node.layout.display == Display::Grid)
        {
            std::vector<UINode*> flowChildren;
            flowChildren.reserve(node.children.size());
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
                flowChildren.push_back(child.get());
            }

            int templateColumns = 0;
            int templateRows = 0;
            std::unordered_map<std::string, glm::ivec4> templateAreas; // name -> {col,row,spanCol,spanRow}
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
                    templateAreas[name] = glm::ivec4(
                        ex.minCol,
                        ex.minRow,
                        ex.maxCol - ex.minCol + 1,
                        ex.maxRow - ex.minRow + 1);
                }
            }

            const int columns = std::max({1, node.layout.gridColumns, templateColumns});
            const float colGap = node.layout.gridColumnGap >= 0.0F ? node.layout.gridColumnGap : node.layout.gap;
            const float rowGap = node.layout.gridRowGap >= 0.0F ? node.layout.gridRowGap : node.layout.gap;

            struct Placement
            {
                int row = 0;
                int col = 0;
                int spanR = 1;
                int spanC = 1;
            };
            std::unordered_map<const UINode*, Placement> placements;
            std::vector<std::vector<bool>> occupied;

            auto ensureRows = [&](int rowCount) {
                if (rowCount <= 0)
                {
                    return;
                }
                if (static_cast<int>(occupied.size()) < rowCount)
                {
                    occupied.resize(static_cast<std::size_t>(rowCount), std::vector<bool>(static_cast<std::size_t>(columns), false));
                }
            };
            auto canPlace = [&](int row, int col, int spanR, int spanC) {
                if (row < 0 || col < 0 || spanR <= 0 || spanC <= 0 || col + spanC > columns)
                {
                    return false;
                }
                ensureRows(row + spanR);
                for (int r = row; r < row + spanR; ++r)
                {
                    for (int c = col; c < col + spanC; ++c)
                    {
                        if (occupied[static_cast<std::size_t>(r)][static_cast<std::size_t>(c)])
                        {
                            return false;
                        }
                    }
                }
                return true;
            };
            auto occupy = [&](int row, int col, int spanR, int spanC) {
                ensureRows(row + spanR);
                for (int r = row; r < row + spanR; ++r)
                {
                    for (int c = col; c < col + spanC; ++c)
                    {
                        occupied[static_cast<std::size_t>(r)][static_cast<std::size_t>(c)] = true;
                    }
                }
            };

            int usedRows = 0;
            for (UINode* child : flowChildren)
            {
                Placement placement;
                placement.spanC = std::clamp(std::max(1, child->layout.gridColumnSpan), 1, columns);
                placement.spanR = std::max(1, child->layout.gridRowSpan);

                bool hasExplicitPlacement = false;
                if (!child->layout.gridArea.empty())
                {
                    auto areaIt = templateAreas.find(child->layout.gridArea);
                    if (areaIt != templateAreas.end())
                    {
                        placement.col = std::clamp(areaIt->second.x, 0, columns - 1);
                        placement.row = std::max(0, areaIt->second.y);
                        placement.spanC = std::clamp(areaIt->second.z, 1, columns - placement.col);
                        placement.spanR = std::max(1, areaIt->second.w);
                        hasExplicitPlacement = true;
                    }
                }
                if (!hasExplicitPlacement && (child->layout.gridColumnStart > 0 || child->layout.gridRowStart > 0))
                {
                    placement.col = std::clamp(std::max(0, child->layout.gridColumnStart - 1), 0, columns - 1);
                    placement.row = std::max(0, child->layout.gridRowStart - 1);
                    placement.spanC = std::clamp(placement.spanC, 1, columns - placement.col);
                    hasExplicitPlacement = true;
                }

                bool placed = false;
                if (hasExplicitPlacement && canPlace(placement.row, placement.col, placement.spanR, placement.spanC))
                {
                    placed = true;
                }
                if (!placed)
                {
                    const int maxRowsToScan = node.layout.gridRows > 0
                        ? std::max(node.layout.gridRows, 1)
                        : std::max(static_cast<int>(flowChildren.size()) * 2 + templateRows + 1, 8);
                    for (int r = 0; r < maxRowsToScan && !placed; ++r)
                    {
                        for (int c = 0; c < columns && !placed; ++c)
                        {
                            if (canPlace(r, c, placement.spanR, placement.spanC))
                            {
                                placement.row = r;
                                placement.col = c;
                                placed = true;
                            }
                        }
                    }
                }
                if (!placed)
                {
                    placement.row = std::max(0, node.layout.gridRows - 1);
                    placement.col = 0;
                    placement.spanC = std::clamp(placement.spanC, 1, columns);
                    placement.spanR = std::max(1, placement.spanR);
                }

                occupy(placement.row, placement.col, placement.spanR, placement.spanC);
                placements[child] = placement;
                usedRows = std::max(usedRows, placement.row + placement.spanR);
            }

            const int rows = std::max({1, templateRows, node.layout.gridRows > 0 ? node.layout.gridRows : usedRows});
            const float defaultColSize = columns > 0
                ? std::max(0.0F, (node.computedRect.contentW - colGap * static_cast<float>(std::max(0, columns - 1))) / static_cast<float>(columns))
                : node.computedRect.contentW;
            const float defaultRowSize = rows > 0
                ? std::max(0.0F, (node.computedRect.contentH - rowGap * static_cast<float>(std::max(0, rows - 1))) / static_cast<float>(rows))
                : node.computedRect.contentH;
            const float cellW = std::max(0.0F, resolveSize(node.layout.gridColumnSize, node.computedRect.contentW, defaultColSize));
            const float cellH = std::max(0.0F, resolveSize(node.layout.gridRowSize, node.computedRect.contentH, defaultRowSize));

            const float gridW = cellW * static_cast<float>(columns) + colGap * static_cast<float>(std::max(0, columns - 1));
            const float gridH = cellH * static_cast<float>(rows) + rowGap * static_cast<float>(std::max(0, rows - 1));
            float gridOffsetX = 0.0F;
            float gridOffsetY = 0.0F;

            switch (node.layout.justifyContent)
            {
                case JustifyContent::Center:
                    gridOffsetX = std::max(0.0F, (node.computedRect.contentW - gridW) * 0.5F);
                    break;
                case JustifyContent::FlexEnd:
                    gridOffsetX = std::max(0.0F, node.computedRect.contentW - gridW);
                    break;
                default:
                    break;
            }
            switch (node.layout.alignItems)
            {
                case AlignItems::Center:
                    gridOffsetY = std::max(0.0F, (node.computedRect.contentH - gridH) * 0.5F);
                    break;
                case AlignItems::FlexEnd:
                    gridOffsetY = std::max(0.0F, node.computedRect.contentH - gridH);
                    break;
                default:
                    break;
            }

            for (UINode* child : flowChildren)
            {
                auto placementIt = placements.find(child);
                if (placementIt == placements.end())
                {
                    continue;
                }
                const Placement placement = placementIt->second;
                if (placement.row >= rows)
                {
                    break;
                }

                const float slotX = node.computedRect.contentX + gridOffsetX + static_cast<float>(placement.col) * (cellW + colGap);
                const float slotY = node.computedRect.contentY + gridOffsetY + static_cast<float>(placement.row) * (cellH + rowGap);
                const float slotW = std::max(0.0F, cellW * static_cast<float>(placement.spanC)
                    + colGap * static_cast<float>(std::max(0, placement.spanC - 1)));
                const float slotH = std::max(0.0F, cellH * static_cast<float>(placement.spanR)
                    + rowGap * static_cast<float>(std::max(0, placement.spanR - 1)));

                float targetW = resolveSize(child->layout.width, slotW, estimatedWidth(*child));
                float targetH = resolveSize(child->layout.height, slotH, estimatedHeight(*child));
                const bool childTextLike = child->type == UINodeType::Text || child->type == UINodeType::Button || child->type == UINodeType::TextInput;
                if (child->layout.width.unit == SizeValue::Unit::Auto
                    && node.layout.gridJustifyItems == GridItemAlign::Stretch
                    && !childTextLike)
                {
                    targetW = slotW;
                }
                if (child->layout.height.unit == SizeValue::Unit::Auto
                    && node.layout.gridAlignItems == GridItemAlign::Stretch
                    && !childTextLike)
                {
                    targetH = slotH;
                }
                if (childTextLike)
                {
                    targetW = std::max(targetW, estimatedWidth(*child));
                    targetH = std::max(targetH, estimatedHeight(*child));
                }
                targetW = std::max(0.0F, targetW);
                targetH = std::max(0.0F, targetH);

                float childX = slotX;
                float childY = slotY;
                switch (node.layout.gridJustifyItems)
                {
                    case GridItemAlign::Center:
                        childX += (slotW - targetW) * 0.5F;
                        break;
                    case GridItemAlign::End:
                        childX += (slotW - targetW);
                        break;
                    default:
                        break;
                }
                switch (node.layout.gridAlignItems)
                {
                    case GridItemAlign::Center:
                        childY += (slotH - targetH) * 0.5F;
                        break;
                    case GridItemAlign::End:
                        childY += (slotH - targetH);
                        break;
                    default:
                        break;
                }

                float availW = targetW;
                float availH = targetH;
                if (child->layout.width.unit == SizeValue::Unit::Percent)
                {
                    availW = (std::abs(child->layout.width.value) > 0.001F)
                        ? (targetW * 100.0F / child->layout.width.value)
                        : slotW;
                }
                if (child->layout.height.unit == SizeValue::Unit::Percent)
                {
                    availH = (std::abs(child->layout.height.value) > 0.001F)
                        ? (targetH * 100.0F / child->layout.height.value)
                        : slotH;
                }
                ArrangeNode(*child, childX, childY, availW, availH);
            }
        }
        else
        {
            float cursorY = 0.0F;
            const float gap = std::max(0.0F, node.layout.gap);
            for (const auto& childPtr : node.children)
            {
                if (!childPtr)
                {
                    continue;
                }
                UINode& child = *childPtr;
                if (child.visibility == Visibility::Collapsed || child.layout.display == Display::None || child.layout.position == Position::Absolute)
                {
                    continue;
                }

                float childAvailH = resolveSize(child.layout.height, node.computedRect.contentH, estimatedHeight(child));
                if (child.layout.height.unit == SizeValue::Unit::Percent)
                {
                    childAvailH = node.computedRect.contentH;
                }
                ArrangeNode(child, node.computedRect.contentX, node.computedRect.contentY + cursorY, node.computedRect.contentW, childAvailH);
                cursorY += child.computedRect.h + gap;
            }
        }

        arrangeAbsoluteChildren();
    }
}

void UiTree::RenderToUiSystem(UiSystem& uiSystem) const
{
    if (!m_root)
    {
        return;
    }

    // Render tree
    RenderNode(*m_root, uiSystem);

    // Debug overlay
    if (m_debugLayout)
    {
        RenderDebugLayout(*m_root, uiSystem);
    }
}

void UiTree::RenderNode(const UINode& node, UiSystem& uiSystem) const
{
    if (node.visibility != Visibility::Visible)
    {
        return;
    }

    // Render shadow first (behind everything)
    RenderNodeShadow(node, uiSystem);

    // Render background
    RenderNodeBackground(node, uiSystem);

    // Render border
    RenderNodeBorder(node, uiSystem);

    // Render content
    RenderNodeContent(node, uiSystem);

    // Render children
    for (const auto& child : node.children)
    {
        if (child)
        {
            RenderNode(*child, uiSystem);
        }
    }
}

void UiTree::RenderNodeBackground(const UINode& node, UiSystem& uiSystem) const
{
    if (node.type == UINodeType::Shape)
    {
        return;
    }
    if (node.computedBackgroundColor.a <= 0.001F)
    {
        return;
    }

    glm::vec2 screenPos = VirtualToScreen(node.computedRect.x, node.computedRect.y);
    glm::vec2 screenSize = glm::vec2(node.computedRect.w * m_scale, node.computedRect.h * m_scale);

    UiRect rect{screenPos.x, screenPos.y, screenSize.x, screenSize.y};

    glm::vec4 color = node.computedBackgroundColor;
    color.a *= node.computedOpacity;
    const float radius = std::max(0.0F, node.computedRadius * m_scale);
    if (HasRenderTransform(node))
    {
        uiSystem.DrawRectTransformed(rect, color, node.transformRotationDeg, node.transformScale, node.transformTranslate, node.layout.pivot);
    }
    else
    {
        if (radius > 0.5F)
        {
            uiSystem.DrawRoundedRect(rect, radius, color);
        }
        else
        {
            uiSystem.DrawRect(rect, color);
        }
    }
}

void UiTree::RenderNodeBorder(const UINode& node, UiSystem& uiSystem) const
{
    if (node.type == UINodeType::Shape)
    {
        return;
    }
    if (node.computedStrokeWidth <= 0.001F || node.computedStrokeColor.a <= 0.001F)
    {
        return;
    }

    glm::vec2 screenPos = VirtualToScreen(node.computedRect.x, node.computedRect.y);
    glm::vec2 screenSize = glm::vec2(node.computedRect.w * m_scale, node.computedRect.h * m_scale);

    UiRect rect{screenPos.x, screenPos.y, screenSize.x, screenSize.y};

    glm::vec4 color = node.computedStrokeColor;
    color.a *= node.computedOpacity;
    if (HasRenderTransform(node))
    {
        uiSystem.DrawRectOutlineTransformed(
            rect,
            node.computedStrokeWidth * m_scale,
            color,
            node.transformRotationDeg,
            node.transformScale,
            node.transformTranslate,
            node.layout.pivot);
    }
    else
    {
        uiSystem.DrawRectOutline(rect, node.computedStrokeWidth * m_scale, color);
    }
}

void UiTree::RenderNodeShadow(const UINode& node, UiSystem& uiSystem) const
{
    if (node.computedShadow.blur <= 0.001F && node.computedShadow.spread <= 0.001F)
    {
        return;
    }

    // Simple shadow approximation - draw offset rect with low opacity
    glm::vec2 screenPos = VirtualToScreen(
        node.computedRect.x + node.computedShadow.offset.x,
        node.computedRect.y + node.computedShadow.offset.y
    );
    glm::vec2 screenSize = glm::vec2(
        (node.computedRect.w + node.computedShadow.spread * 2.0F) * m_scale,
        (node.computedRect.h + node.computedShadow.spread * 2.0F) * m_scale
    );

    UiRect rect{screenPos.x, screenPos.y, screenSize.x, screenSize.y};

    glm::vec4 color = node.computedShadow.color;
    color.a *= node.computedOpacity * 0.5F;

    if (HasRenderTransform(node))
    {
        uiSystem.DrawRectTransformed(
            rect,
            color,
            node.transformRotationDeg,
            node.transformScale,
            node.transformTranslate,
            node.layout.pivot);
    }
    else
    {
        uiSystem.DrawRect(rect, color);
    }
}

void UiTree::RenderNodeContent(const UINode& node, UiSystem& uiSystem) const
{
    const float tx = node.transformTranslate.x * m_scale;
    const float ty = node.transformTranslate.y * m_scale;
    const float sx = std::max(0.01F, node.transformScale.x);
    const float sy = std::max(0.01F, node.transformScale.y);

    switch (node.type)
    {
        case UINodeType::Text:
        case UINodeType::Button:
        case UINodeType::TextInput:
        {
            if (!node.text.empty())
            {
                glm::vec2 screenPos = VirtualToScreen(node.computedRect.contentX, node.computedRect.contentY);
                screenPos.x += tx;
                screenPos.y += ty;

                glm::vec4 textColor = node.computedTextColor;
                textColor.a *= node.computedOpacity;
                textColor.a = std::clamp(textColor.a * WeightAlphaMultiplier(node.computedFont.weight), 0.0F, 1.0F);

                // Center text in content area
                const float fontScale = (node.computedFont.size / 16.0F) * std::max(sx, sy);  // Relative to base font size
                const float italicSkew = node.computedFont.style == FontProps::Style::Italic ? 0.22F : 0.0F;
                const float letterSpacing = node.computedFont.letterSpacing * m_scale * std::max(sx, sy);
                const float lineHeight = uiSystem.LineHeight(fontScale);
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

                const float contentW = node.computedRect.contentW * m_scale * sx;
                const float contentH = node.computedRect.contentH * m_scale * sy;
                const float textHeight = lineHeight * static_cast<float>(lines.size());
                const float baseY = screenPos.y + (contentH - textHeight) * 0.5F;

                int extraPasses = WeightExtraPasses(node.computedFont.weight);
                if (fontScale > 2.0F)
                {
                    extraPasses = std::max(0, extraPasses - 1);
                }
                if (fontScale > 3.2F)
                {
                    extraPasses = std::max(0, extraPasses - 1);
                }

                for (std::size_t i = 0; i < lines.size(); ++i)
                {
                    const float lineWidth = uiSystem.TextWidth(lines[i], fontScale, letterSpacing);
                    float lineX = screenPos.x + (contentW - lineWidth) * 0.5F;
                    switch (node.computedFont.align)
                    {
                        case FontProps::Align::Left:
                            lineX = screenPos.x;
                            break;
                        case FontProps::Align::Right:
                            lineX = screenPos.x + contentW - lineWidth;
                            break;
                        case FontProps::Align::Center:
                        default:
                            break;
                    }
                    const float lineTop = baseY + lineHeight * static_cast<float>(i);
                    uiSystem.DrawTextLabel(lineX, lineTop, lines[i], textColor, fontScale, italicSkew, letterSpacing);
                    for (int pass = 0; pass < extraPasses; ++pass)
                    {
                        const float offset = std::max(0.4F, lineHeight * (0.028F + static_cast<float>(pass) * 0.009F));
                        uiSystem.DrawTextLabel(lineX + offset, lineTop, lines[i], textColor, fontScale, italicSkew, letterSpacing);
                    }

                    if (node.computedFont.underline || node.computedFont.strikethrough)
                    {
                        const float lineThickness = std::max(1.0F, m_scale * (0.85F + static_cast<float>(extraPasses) * 0.25F));
                        if (node.computedFont.underline)
                        {
                            const float underlineY = lineTop + lineHeight * 0.90F;
                            uiSystem.DrawLine(lineX, underlineY, lineX + lineWidth, underlineY, lineThickness, textColor);
                        }
                        if (node.computedFont.strikethrough)
                        {
                            const float strikeY = lineTop + lineHeight * 0.54F;
                            uiSystem.DrawLine(lineX, strikeY, lineX + lineWidth, strikeY, lineThickness, textColor);
                        }
                    }
                }
            }
            break;
        }

        case UINodeType::Image:
        {
            if (!node.imageSource.empty())
            {
                glm::vec2 screenPos = VirtualToScreen(node.computedRect.contentX, node.computedRect.contentY);
                UiRect rect{
                    screenPos.x,
                    screenPos.y,
                    node.computedRect.contentW * m_scale,
                    node.computedRect.contentH * m_scale
                };
                glm::vec4 tint = node.computedTextColor;
                tint.a *= node.computedOpacity;
                uiSystem.DrawImage(rect, node.imageSource, tint, node.transformRotationDeg, node.transformScale, node.transformTranslate, node.layout.pivot);
            }
            break;
        }

        case UINodeType::Shape:
        {
            glm::vec2 screenPos = VirtualToScreen(node.computedRect.contentX, node.computedRect.contentY);
            UiRect rect{
                screenPos.x,
                screenPos.y,
                node.computedRect.contentW * m_scale,
                node.computedRect.contentH * m_scale
            };
            glm::vec4 fillColor = node.computedBackgroundColor;
            fillColor.a *= node.computedOpacity;
            glm::vec4 strokeColor = node.computedStrokeColor;
            strokeColor.a *= node.computedOpacity;
            const float strokeWidth = std::max(1.0F, node.computedStrokeWidth * m_scale);

            switch (node.shapeType)
            {
                case UIShapeType::Rectangle:
                {
                    if (fillColor.a > 0.001F)
                    {
                        uiSystem.DrawRectTransformed(rect, fillColor, node.transformRotationDeg, node.transformScale, node.transformTranslate, node.layout.pivot);
                    }
                    if (strokeColor.a > 0.001F && node.computedStrokeWidth > 0.001F)
                    {
                        uiSystem.DrawRectOutlineTransformed(
                            rect,
                            strokeWidth,
                            strokeColor,
                            node.transformRotationDeg,
                            node.transformScale,
                            node.transformTranslate,
                            node.layout.pivot);
                    }
                    break;
                }
                case UIShapeType::Circle:
                {
                    const float cx = rect.x + rect.w * 0.5F + tx;
                    const float cy = rect.y + rect.h * 0.5F + ty;
                    const float radius = std::max(1.0F, std::min(rect.w * sx, rect.h * sy) * 0.5F);
                    if (fillColor.a > 0.001F)
                    {
                        uiSystem.DrawCircle(cx, cy, radius, fillColor);
                    }
                    if (strokeColor.a > 0.001F && node.computedStrokeWidth > 0.001F)
                    {
                        uiSystem.DrawCircleOutline(cx, cy, radius, strokeWidth, strokeColor);
                    }
                    break;
                }
                case UIShapeType::Line:
                {
                    const float startX = rect.x + tx;
                    const float startY = rect.y + ty;
                    float endX = startX + node.shapeLineEnd.x * sx * m_scale;
                    float endY = startY + node.shapeLineEnd.y * sy * m_scale;
                    if (std::abs(node.transformRotationDeg) > 0.001F)
                    {
                        const float radians = glm::radians(node.transformRotationDeg);
                        const float c = std::cos(radians);
                        const float s = std::sin(radians);
                        const float dx = endX - startX;
                        const float dy = endY - startY;
                        endX = startX + dx * c - dy * s;
                        endY = startY + dx * s + dy * c;
                    }
                    uiSystem.DrawLine(startX, startY, endX, endY, strokeWidth, strokeColor.a > 0.001F ? strokeColor : fillColor);
                    break;
                }
                default:
                    break;
            }
            break;
        }

        case UINodeType::Slider:
        {
            glm::vec2 screenPos = VirtualToScreen(node.computedRect.contentX, node.computedRect.contentY);
            screenPos.x += tx;
            screenPos.y += ty;
            float trackW = node.computedRect.contentW * m_scale * sx;
            float trackH = node.computedRect.contentH * m_scale * sy;

            // Track
            UiRect track{screenPos.x, screenPos.y, trackW, trackH};
            uiSystem.DrawRect(track, glm::vec4(0.2F, 0.2F, 0.2F, node.computedOpacity));

            // Fill
            float fillW = trackW * node.state.value01;
            UiRect fill{screenPos.x, screenPos.y, fillW, trackH};
            uiSystem.DrawRect(fill, glm::vec4(0.3F, 0.6F, 0.9F, node.computedOpacity));

            // Thumb
            float thumbX = screenPos.x + fillW - 6.0F;
            UiRect thumb{thumbX, screenPos.y - 2.0F, 12.0F, trackH + 4.0F};
            uiSystem.DrawRect(thumb, glm::vec4(1.0F, 1.0F, 1.0F, node.computedOpacity));
            break;
        }

        case UINodeType::Toggle:
        {
            glm::vec2 screenPos = VirtualToScreen(node.computedRect.contentX, node.computedRect.contentY);
            screenPos.x += tx;
            screenPos.y += ty;
            float size = std::min(node.computedRect.contentW * sx, node.computedRect.contentH * sy) * m_scale;

            // Box
            UiRect box{screenPos.x, screenPos.y, size, size};
            uiSystem.DrawRect(box, glm::vec4(0.2F, 0.2F, 0.2F, node.computedOpacity));
            uiSystem.DrawRectOutline(box, 1.0F, glm::vec4(0.5F, 0.5F, 0.5F, node.computedOpacity));

            // Checkmark
            if (node.state.checked)
            {
                float pad = size * 0.2F;
                UiRect check{screenPos.x + pad, screenPos.y + pad, size - pad * 2.0F, size - pad * 2.0F};
                uiSystem.DrawRect(check, glm::vec4(0.3F, 0.7F, 0.4F, node.computedOpacity));
            }

            // Label
            if (!node.text.empty())
            {
                float labelX = screenPos.x + size + 8.0F * m_scale;
                float labelY = screenPos.y + (size - node.computedFont.size * m_scale) * 0.5F;
                uiSystem.DrawTextLabel(labelX, labelY, node.text, node.computedTextColor, 1.0F);
            }
            break;
        }

        case UINodeType::ProgressBar:
        {
            glm::vec2 screenPos = VirtualToScreen(node.computedRect.contentX, node.computedRect.contentY);
            screenPos.x += tx;
            screenPos.y += ty;
            float barW = node.computedRect.contentW * m_scale * sx;
            float barH = node.computedRect.contentH * m_scale * sy;

            // Background
            UiRect bg{screenPos.x, screenPos.y, barW, barH};
            uiSystem.DrawRect(bg, glm::vec4(0.2F, 0.2F, 0.2F, node.computedOpacity));

            // Fill
            float fillW = barW * std::clamp(node.state.value01, 0.0F, 1.0F);
            UiRect fill{screenPos.x, screenPos.y, fillW, barH};
            uiSystem.DrawRect(fill, glm::vec4(0.3F, 0.7F, 0.4F, node.computedOpacity));
            break;
        }

        default:
            break;
    }
}

void UiTree::RenderDebugLayout(const UINode& node, UiSystem& uiSystem) const
{
    if (node.visibility != Visibility::Visible)
    {
        return;
    }

    // Draw layout bounds
    glm::vec2 screenPos = VirtualToScreen(node.computedRect.x, node.computedRect.y);
    glm::vec2 screenSize = glm::vec2(node.computedRect.w * m_scale, node.computedRect.h * m_scale);

    UiRect rect{screenPos.x, screenPos.y, screenSize.x, screenSize.y};

    // Green outline for bounds
    uiSystem.DrawRectOutline(rect, 1.0F, glm::vec4(0.0F, 1.0F, 0.0F, 0.5F));

    // Cyan for content area (if different from bounds)
    if (node.layout.padding.left > 0 || node.layout.padding.top > 0 || node.layout.padding.right > 0 || node.layout.padding.bottom > 0)
    {
        glm::vec2 contentPos = VirtualToScreen(node.computedRect.contentX, node.computedRect.contentY);
        glm::vec2 contentSize = glm::vec2(node.computedRect.contentW * m_scale, node.computedRect.contentH * m_scale);
        UiRect contentRect{contentPos.x, contentPos.y, contentSize.x, contentSize.y};
        uiSystem.DrawRectOutline(contentRect, 1.0F, glm::vec4(0.0F, 1.0F, 1.0F, 0.5F));
    }

    // Recurse
    for (const auto& child : node.children)
    {
        if (child)
        {
            RenderDebugLayout(*child, uiSystem);
        }
    }
}

} // namespace engine::ui
