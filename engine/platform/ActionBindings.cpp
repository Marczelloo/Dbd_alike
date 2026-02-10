#include "engine/platform/ActionBindings.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <unordered_map>

#include <GLFW/glfw3.h>
#include <nlohmann/json.hpp>

#include "engine/platform/Input.hpp"

namespace engine::platform
{
namespace
{
using json = nlohmann::json;

const std::unordered_map<int, std::string> kCodeToText{
    {GLFW_KEY_W, "W"},
    {GLFW_KEY_A, "A"},
    {GLFW_KEY_S, "S"},
    {GLFW_KEY_D, "D"},
    {GLFW_KEY_E, "E"},
    {GLFW_KEY_SPACE, "Space"},
    {GLFW_KEY_LEFT_SHIFT, "LShift"},
    {GLFW_KEY_LEFT_CONTROL, "LCtrl"},
    {GLFW_KEY_RIGHT_CONTROL, "RCtrl"},
    {GLFW_KEY_GRAVE_ACCENT, "Tilde"},
    {GLFW_KEY_F1, "F1"},
    {GLFW_KEY_F2, "F2"},
    {GLFW_KEY_F3, "F3"},
    {GLFW_KEY_F4, "F4"},
    {GLFW_KEY_F5, "F5"},
    {GLFW_KEY_ESCAPE, "Esc"},
    {ActionBindings::EncodeMouseButton(GLFW_MOUSE_BUTTON_LEFT), "MouseLeft"},
    {ActionBindings::EncodeMouseButton(GLFW_MOUSE_BUTTON_RIGHT), "MouseRight"},
    {ActionBindings::EncodeMouseButton(GLFW_MOUSE_BUTTON_MIDDLE), "MouseMiddle"},
};
} // namespace

ActionBindings::ActionBindings()
{
    ResetDefaults();
}

void ActionBindings::ResetDefaults()
{
    for (ActionBinding& binding : m_bindings)
    {
        binding = ActionBinding{};
    }

    SetCode(InputAction::MoveForward, 0, GLFW_KEY_W);
    SetCode(InputAction::MoveBackward, 0, GLFW_KEY_S);
    SetCode(InputAction::MoveLeft, 0, GLFW_KEY_A);
    SetCode(InputAction::MoveRight, 0, GLFW_KEY_D);
    SetCode(InputAction::LookX, 0, kMouseAxisX);
    SetCode(InputAction::LookY, 0, kMouseAxisY);
    SetCode(InputAction::Sprint, 0, GLFW_KEY_LEFT_SHIFT);
    SetCode(InputAction::Crouch, 0, GLFW_KEY_LEFT_CONTROL);
    SetCode(InputAction::Crouch, 1, GLFW_KEY_RIGHT_CONTROL);
    SetCode(InputAction::Interact, 0, GLFW_KEY_E);
    SetCode(InputAction::AttackShort, 0, EncodeMouseButton(GLFW_MOUSE_BUTTON_LEFT));
    SetCode(InputAction::AttackLunge, 0, EncodeMouseButton(GLFW_MOUSE_BUTTON_LEFT));
    SetCode(InputAction::ToggleConsole, 0, GLFW_KEY_GRAVE_ACCENT);
    SetCode(InputAction::ToggleDebugHud, 0, GLFW_KEY_F1);
}

const ActionBinding& ActionBindings::Get(InputAction action) const
{
    return m_bindings[static_cast<std::size_t>(action)];
}

void ActionBindings::Set(InputAction action, const ActionBinding& binding)
{
    m_bindings[static_cast<std::size_t>(action)] = binding;
}

void ActionBindings::SetCode(InputAction action, int slot, int code)
{
    ActionBinding& binding = m_bindings[static_cast<std::size_t>(action)];
    if (slot <= 0)
    {
        binding.primary = code;
    }
    else
    {
        binding.secondary = code;
    }
}

int ActionBindings::GetCode(InputAction action, int slot) const
{
    const ActionBinding& binding = m_bindings[static_cast<std::size_t>(action)];
    return slot <= 0 ? binding.primary : binding.secondary;
}

bool ActionBindings::Matches(const Input& input, int code, bool pressed, bool released) const
{
    if (code == kUnbound || code == kMouseAxisX || code == kMouseAxisY)
    {
        return false;
    }

    if (IsMouseCode(code))
    {
        const int button = DecodeMouseButton(code);
        if (released)
        {
            return input.IsMouseReleased(button);
        }
        if (pressed)
        {
            return input.IsMousePressed(button);
        }
        return input.IsMouseDown(button);
    }

    if (released)
    {
        return input.IsKeyReleased(code);
    }
    if (pressed)
    {
        return input.IsKeyPressed(code);
    }
    return input.IsKeyDown(code);
}

bool ActionBindings::IsDown(const Input& input, InputAction action) const
{
    const ActionBinding& binding = Get(action);
    return Matches(input, binding.primary, false, false) || Matches(input, binding.secondary, false, false);
}

bool ActionBindings::IsPressed(const Input& input, InputAction action) const
{
    const ActionBinding& binding = Get(action);
    return Matches(input, binding.primary, true, false) || Matches(input, binding.secondary, true, false);
}

bool ActionBindings::IsReleased(const Input& input, InputAction action) const
{
    const ActionBinding& binding = Get(action);
    return Matches(input, binding.primary, false, true) || Matches(input, binding.secondary, false, true);
}

std::optional<std::pair<InputAction, int>> ActionBindings::FindConflict(int code, InputAction ignoredAction, int ignoredSlot) const
{
    if (code == kUnbound || code == kMouseAxisX || code == kMouseAxisY)
    {
        return std::nullopt;
    }

    for (InputAction action : AllActions())
    {
        if (!IsRebindable(action))
        {
            continue;
        }

        const ActionBinding& binding = Get(action);
        if (!(action == ignoredAction && ignoredSlot == 0) && binding.primary == code)
        {
            return std::pair<InputAction, int>{action, 0};
        }
        if (!(action == ignoredAction && ignoredSlot == 1) && binding.secondary == code)
        {
            return std::pair<InputAction, int>{action, 1};
        }
    }

    return std::nullopt;
}

bool ActionBindings::LoadFromJsonFile(const std::string& path, std::string* outError)
{
    std::ifstream stream(path);
    if (!stream.is_open())
    {
        if (outError != nullptr)
        {
            *outError = "Cannot open controls file: " + path;
        }
        return false;
    }

    json root;
    try
    {
        stream >> root;
    }
    catch (const std::exception& ex)
    {
        if (outError != nullptr)
        {
            *outError = std::string{"Invalid controls JSON: "} + ex.what();
        }
        return false;
    }

    if (!root.contains("bindings") || !root["bindings"].is_object())
    {
        if (outError != nullptr)
        {
            *outError = "Missing controls.bindings object";
        }
        return false;
    }

    for (InputAction action : AllActions())
    {
        const char* actionName = ActionName(action);
        if (!root["bindings"].contains(actionName))
        {
            continue;
        }
        const json& node = root["bindings"][actionName];
        if (!node.is_object())
        {
            continue;
        }
        ActionBinding binding = Get(action);
        if (node.contains("primary") && node["primary"].is_number_integer())
        {
            binding.primary = node["primary"].get<int>();
        }
        if (node.contains("secondary") && node["secondary"].is_number_integer())
        {
            binding.secondary = node["secondary"].get<int>();
        }
        Set(action, binding);
    }

    return true;
}

bool ActionBindings::SaveToJsonFile(const std::string& path, std::string* outError) const
{
    std::filesystem::path filePath(path);
    std::filesystem::create_directories(filePath.parent_path());

    json root;
    root["asset_version"] = 1;
    json bindings = json::object();
    for (InputAction action : AllActions())
    {
        const ActionBinding& binding = Get(action);
        bindings[ActionName(action)] = {
            {"primary", binding.primary},
            {"secondary", binding.secondary},
        };
    }
    root["bindings"] = std::move(bindings);

    std::ofstream stream(path);
    if (!stream.is_open())
    {
        if (outError != nullptr)
        {
            *outError = "Cannot write controls file: " + path;
        }
        return false;
    }

    stream << root.dump(2) << "\n";
    return true;
}

std::vector<InputAction> ActionBindings::AllActions()
{
    return {
        InputAction::MoveForward,
        InputAction::MoveBackward,
        InputAction::MoveLeft,
        InputAction::MoveRight,
        InputAction::LookX,
        InputAction::LookY,
        InputAction::Sprint,
        InputAction::Crouch,
        InputAction::Interact,
        InputAction::AttackShort,
        InputAction::AttackLunge,
        InputAction::ToggleConsole,
        InputAction::ToggleDebugHud,
    };
}

const char* ActionBindings::ActionName(InputAction action)
{
    switch (action)
    {
        case InputAction::MoveForward: return "MoveForward";
        case InputAction::MoveBackward: return "MoveBackward";
        case InputAction::MoveLeft: return "MoveLeft";
        case InputAction::MoveRight: return "MoveRight";
        case InputAction::LookX: return "LookX";
        case InputAction::LookY: return "LookY";
        case InputAction::Sprint: return "Sprint";
        case InputAction::Crouch: return "Crouch";
        case InputAction::Interact: return "Interact";
        case InputAction::AttackShort: return "AttackShort";
        case InputAction::AttackLunge: return "AttackLunge";
        case InputAction::ToggleConsole: return "ToggleConsole";
        case InputAction::ToggleDebugHud: return "ToggleDebugHUD";
        default: return "Unknown";
    }
}

const char* ActionBindings::ActionLabel(InputAction action)
{
    switch (action)
    {
        case InputAction::MoveForward: return "Move Forward";
        case InputAction::MoveBackward: return "Move Backward";
        case InputAction::MoveLeft: return "Move Left";
        case InputAction::MoveRight: return "Move Right";
        case InputAction::LookX: return "Look X";
        case InputAction::LookY: return "Look Y";
        case InputAction::Sprint: return "Sprint";
        case InputAction::Crouch: return "Crouch";
        case InputAction::Interact: return "Interact";
        case InputAction::AttackShort: return "Attack Short";
        case InputAction::AttackLunge: return "Attack Lunge";
        case InputAction::ToggleConsole: return "Toggle Console";
        case InputAction::ToggleDebugHud: return "Toggle Debug HUD";
        default: return "Unknown";
    }
}

bool ActionBindings::IsRebindable(InputAction action)
{
    return action != InputAction::LookX && action != InputAction::LookY;
}

std::string ActionBindings::CodeToLabel(int code)
{
    if (code == kUnbound)
    {
        return "Unbound";
    }
    if (code == kMouseAxisX)
    {
        return "Mouse X";
    }
    if (code == kMouseAxisY)
    {
        return "Mouse Y";
    }

    if (const auto it = kCodeToText.find(code); it != kCodeToText.end())
    {
        return it->second;
    }

    if (IsMouseCode(code))
    {
        return "Mouse" + std::to_string(DecodeMouseButton(code));
    }

    return "Key(" + std::to_string(code) + ")";
}
} // namespace engine::platform
