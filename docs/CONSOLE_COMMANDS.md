# Console Commands — Development Guidelines

This document describes how to create consistent, user-friendly console commands for the developer console.

---

## Quick Reference

| Log Type | Method | Use Case | Prefix |
|----------|--------|----------|--------|
| Error | `LogError(msg)` | Invalid usage, missing params, failures | `✗` |
| Success | `LogSuccess(msg)` | Command executed successfully | `✓` |
| Warning | `LogWarning(msg)` | Non-fatal issues, deprecations | `⚠` |
| Info | `LogInfo(msg)` / `AddLog(msg, color)` | General output, dumps | — |
| Command | `LogCommand(msg)` | Echo user input | `»` |

---

## Registration Pattern

```cpp
RegisterCommand("command_name <required> [optional]", 
    "Short description of what the command does",
    [this](const std::vector<std::string>& tokens, const ConsoleContext& context) {
        // 1. Validate prerequisites
        if (context.gameplay == nullptr)
        {
            return; // Silent return if system unavailable
        }
        
        // 2. Validate argument count
        if (tokens.size() != 2)
        {
            LogError("Usage: command_name <param>");
            return;
        }
        
        // 3. Execute and report
        context.gameplay->DoSomething(tokens[1]);
        LogSuccess("Something done: " + tokens[1]);
    });
```

---

## Naming Conventions

### Command Names

| Pattern | Example | Notes |
|---------|---------|-------|
| `<noun>_<verb>` | `tr_debug`, `trap_clear` | Preferred for subsystem commands |
| `<verb>_<noun>` | `spawn survivor`, `set_speed` | Acceptable for general actions |
| `<subsystem>_<action>` | `killer_light_range`, `audio_play` | Clear grouping |

### Parameter Syntax in Usage

| Notation | Meaning | Example |
|----------|---------|---------|
| `<param>` | Required parameter | `<id>` |
| `[param]` | Optional parameter | `[count]` |
| `a\|b\|c` | Enum choice | `on\|off`, `survivor\|killer` |

---

## Logging Rules

### Usage Errors

Always use `LogError` for usage/argument errors:

```cpp
if (tokens.size() != 2)
{
    LogError("Usage: my_command <id>");
    return;
}

if (tokens[1] != "on" && tokens[1] != "off")
{
    LogError("Expected on|off");
    return;
}
```

### Success Messages

Use `LogSuccess` for successful execution:

```cpp
LogSuccess("Feature enabled");
LogSuccess("Value set to " + tokens[1]);
LogSuccess(std::string("Mode: ") + (enabled ? "enabled" : "disabled"));
```

### Data Output

Use `AddLog` (with optional color) for data dumps and info:

```cpp
AddLog("=== Section Title ===");
AddLog("  Key: " + value, ConsoleColors::Info);
AddLog("  • Item 1");
AddLog("  • Item 2");
```

### System Unavailable

Silent return (no message) if the required system is not available:

```cpp
if (context.gameplay == nullptr)
{
    return;
}
```

Only log if the user explicitly requested data from that system:

```cpp
RegisterCommand("tr_dump", "Print terror radius state", [...](...) {
    if (context.terrorRadiusDump == nullptr)
    {
        LogError("Terror radius system not available");
        return;
    }
    AddLog(context.terrorRadiusDump());
});
```

---

## Message Formatting

### No Trailing Dots

```diff
- LogSuccess("Feature enabled.");
+ LogSuccess("Feature enabled");

- LogError("Expected on|off.");
+ LogError("Expected on|off");
```

### Consistent Phrasing

| Type | Format | Example |
|------|--------|---------|
| State toggle | `<Feature> <state>` | `"Debug mode enabled"` |
| Value set | `<Feature> set to <value>` | `"Speed set to 6.0"` |
| Action done | `<Action> <result>` | `"Survivor spawned"` |
| List header | `=== <TITLE> ===` | `"=== CHASE STATE ==="` |

### Boolean Display

```cpp
std::string("Feature ") + (enabled ? "enabled" : "disabled")
std::string("Active: ") + (isActive ? "YES" : "NO")
```

---

## Boolean Parsing

Use the standard helper:

```cpp
bool enabled = false;
if (!ParseBoolToken(tokens[1], enabled))
{
    LogError("Expected on|off");
    return;
}
```

Accepts: `on`, `off`, `1`, `0`, `true`, `false` (case-insensitive).

---

## Aliases

Create aliases for discoverability, but keep implementation DRY:

```cpp
const auto doListItems = [this](const ConsoleContext& context) {
    // Implementation
};

RegisterCommand("item_ids", "List all item IDs", doListItems);
RegisterCommand("items", "Alias for item_ids", doListItems);
RegisterCommand("list_items", "Alias for item_ids", doListItems);
```

---

## Categories

Commands are auto-categorized by their usage string:

| Prefix | Category |
|--------|----------|
| `tr_`, `chase_`, `bloodlust_` | Gameplay |
| `audio_`, `scratch_`, `blood_` | Gameplay |
| `spawn`, `teleport`, `heal` | Gameplay |
| `fx_` | General |
| `host`, `join`, `net_`, `lan_` | Network |
| `set_`, `toggle_` | General |

---

## Examples

### Simple Toggle

```cpp
RegisterCommand("debug_mode on|off", "Toggle debug overlay",
    [this](const std::vector<std::string>& tokens, const ConsoleContext& context) {
        if (tokens.size() != 2)
        {
            LogError("Usage: debug_mode on|off");
            return;
        }
        
        bool enabled = false;
        if (!ParseBoolToken(tokens[1], enabled))
        {
            LogError("Expected on|off");
            return;
        }
        
        context.gameplay->SetDebugMode(enabled);
        LogSuccess(std::string("Debug mode ") + (enabled ? "enabled" : "disabled"));
    });
```

### Value Setter

```cpp
RegisterCommand("set_speed <role> <percent>", "Set movement speed percent",
    [this](const std::vector<std::string>& tokens, const ConsoleContext& context) {
        if (tokens.size() != 3)
        {
            LogError("Usage: set_speed <role> <percent>");
            return;
        }
        
        if (tokens[1] != "survivor" && tokens[1] != "killer")
        {
            LogError("Role must be survivor or killer");
            return;
        }
        
        float value = ParseFloatOr(100.0F, tokens[2]);
        if (value <= 0.0F)
        {
            LogError("Percent must be > 0");
            return;
        }
        
        context.gameplay->SetRoleSpeedPercent(tokens[1], value);
        LogSuccess(tokens[1] + " speed set to " + std::to_string(value));
    });
```

### Data Dump

```cpp
RegisterCommand("state_dump", "Print current state",
    [this](const std::vector<std::string>&, const ConsoleContext& context) {
        if (context.gameplay == nullptr)
        {
            return;
        }
        
        const auto state = context.gameplay->GetState();
        AddLog("=== State ===", ConsoleColors::Category);
        AddLog("  Position: " + FormatVec3(state.position));
        AddLog("  Health: " + std::to_string(state.health));
        AddLog("  Active: " + std::string(state.isActive ? "YES" : "NO"));
    });
```

---

## Checklist for New Commands

- [ ] Usage string includes all parameters with correct notation (`<req>` / `[opt]` / `a|b`)
- [ ] Description is concise (one line, no period)
- [ ] Uses `LogError` for usage/argument errors
- [ ] Uses `LogSuccess` for success confirmation
- [ ] Uses `AddLog` for data output
- [ ] No trailing dots in messages
- [ ] Silent return when system unavailable (unless explicit request)
- [ ] Boolean parsing uses `ParseBoolToken`
- [ ] Consistent phrasing with existing commands
