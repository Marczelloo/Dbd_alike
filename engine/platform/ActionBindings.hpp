#pragma once

#include <array>
#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace engine::platform
{
class Input;

enum class InputAction : std::size_t
{
    MoveForward = 0,
    MoveBackward,
    MoveLeft,
    MoveRight,
    LookX,
    LookY,
    Sprint,
    Crouch,
    Interact,
    AttackShort,
    AttackLunge,
    ToggleConsole,
    ToggleDebugHud,
    Count
};

struct ActionBinding
{
    int primary = -1;
    int secondary = -1;
};

class ActionBindings
{
public:
    static constexpr int kUnbound = -1;
    static constexpr int kMouseOffset = 10000;
    static constexpr int kMouseAxisX = -1001;
    static constexpr int kMouseAxisY = -1002;

    ActionBindings();

    void ResetDefaults();

    [[nodiscard]] const ActionBinding& Get(InputAction action) const;
    void Set(InputAction action, const ActionBinding& binding);
    void SetCode(InputAction action, int slot, int code);
    [[nodiscard]] int GetCode(InputAction action, int slot) const;

    [[nodiscard]] bool IsDown(const Input& input, InputAction action) const;
    [[nodiscard]] bool IsPressed(const Input& input, InputAction action) const;
    [[nodiscard]] bool IsReleased(const Input& input, InputAction action) const;

    [[nodiscard]] std::optional<std::pair<InputAction, int>> FindConflict(int code, InputAction ignoredAction, int ignoredSlot) const;

    [[nodiscard]] bool LoadFromJsonFile(const std::string& path, std::string* outError = nullptr);
    [[nodiscard]] bool SaveToJsonFile(const std::string& path, std::string* outError = nullptr) const;

    [[nodiscard]] static std::vector<InputAction> AllActions();
    [[nodiscard]] static const char* ActionName(InputAction action);
    [[nodiscard]] static const char* ActionLabel(InputAction action);
    [[nodiscard]] static bool IsRebindable(InputAction action);
    [[nodiscard]] static std::string CodeToLabel(int code);

    [[nodiscard]] static int EncodeMouseButton(int button) { return kMouseOffset + button; }
    [[nodiscard]] static bool IsMouseCode(int code) { return code >= kMouseOffset; }
    [[nodiscard]] static int DecodeMouseButton(int code) { return code - kMouseOffset; }

private:
    [[nodiscard]] bool Matches(const Input& input, int code, bool pressed, bool released) const;

    std::array<ActionBinding, static_cast<std::size_t>(InputAction::Count)> m_bindings{};
};
} // namespace engine::platform
