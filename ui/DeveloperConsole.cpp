#include "ui/DeveloperConsole.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <map>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <glm/geometric.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec4.hpp>

#include "engine/platform/Window.hpp"
#include "game/gameplay/GameplaySystems.hpp"

#if BUILD_WITH_IMGUI
#include <imgui.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_opengl3.h>
#endif

namespace ui
{
#if BUILD_WITH_IMGUI
namespace
{
std::vector<std::string> Tokenize(const std::string& text)
{
    std::istringstream stream(text);
    std::vector<std::string> tokens;
    std::string token;
    while (stream >> token)
    {
        tokens.push_back(token);
    }
    return tokens;
}

bool ParseBoolToken(const std::string& token, bool& outValue)
{
    if (token == "on" || token == "true" || token == "1")
    {
        outValue = true;
        return true;
    }
    if (token == "off" || token == "false" || token == "0")
    {
        outValue = false;
        return true;
    }
    return false;
}

float ParseFloatOr(float fallback, const std::string& token)
{
    try
    {
        return std::stof(token);
    }
    catch (...)
    {
        return fallback;
    }
}

int ParseIntOr(int fallback, const std::string& token)
{
    try
    {
        return std::stoi(token);
    }
    catch (...)
    {
        return fallback;
    }
}

std::string CommandCategoryForUsage(const std::string& usage)
{
    const std::vector<std::string> tokens = Tokenize(usage);
    if (tokens.empty())
    {
        return "General";
    }

    const std::string& command = tokens.front();
    if (command == "host" || command == "join" || command == "disconnect" || command == "net_status" ||
        command == "net_dump" || command == "lan_scan" || command == "lan_status" || command == "lan_debug")
    {
        return "Network";
    }
    if (command == "set_vsync" || command == "set_fps" || command == "set_tick" || command == "set_resolution" ||
        command == "toggle_fullscreen" || command == "render_mode" || command == "audio_play" ||
        command == "audio_loop" || command == "audio_stop_all")
    {
        return "System";
    }
    if (command == "toggle_collision" || command == "toggle_debug_draw" || command == "physics_debug" ||
        command == "noclip" || command == "tr_vis" || command == "tr_set" || command == "set_chase" ||
        command == "cam_mode" || command == "control_role" || command == "set_role" ||
        command == "fx_spawn" || command == "fx_stop_all" || command == "fx_list" ||
        command == "player_dump" || command == "scene_dump")
    {
        return "Debug";
    }
    if (command == "help" || command == "quit")
    {
        return "General";
    }
    return "Gameplay";
}
} // namespace

struct DeveloperConsole::Impl
{
    struct CommandInfo
    {
        std::string usage;
        std::string description;
        std::string category;
    };

    using CommandHandler = std::function<void(const std::vector<std::string>&, const ConsoleContext&)>;

    bool open = false;
    bool firstOpenAnnouncementDone = false;
    bool scrollToBottom = false;
    bool reclaimFocus = false;

    std::array<char, 512> inputBuffer{};

    std::vector<std::string> items;
    std::vector<std::string> history;
    int historyPos = -1;

    std::unordered_map<std::string, CommandHandler> commandRegistry;
    std::vector<CommandInfo> commandInfos;

    void AddLog(const std::string& text)
    {
        items.push_back(text);
        scrollToBottom = true;
    }

    void PrintHelp()
    {
        AddLog("Available commands by category:");
        std::map<std::string, std::vector<CommandInfo>> grouped;
        for (const CommandInfo& info : commandInfos)
        {
            grouped[info.category].push_back(info);
        }

        for (auto& [category, commands] : grouped)
        {
            std::sort(commands.begin(), commands.end(), [](const CommandInfo& a, const CommandInfo& b) {
                return a.usage < b.usage;
            });
            AddLog("[" + category + "]");
            for (const CommandInfo& info : commands)
            {
                AddLog("  " + info.usage + " - " + info.description);
            }
        }
    }

    void RegisterCommand(const std::string& usage, const std::string& description, CommandHandler handler)
    {
        const std::vector<std::string> tokens = Tokenize(usage);
        if (tokens.empty())
        {
            return;
        }

        commandInfos.push_back(CommandInfo{usage, description, CommandCategoryForUsage(usage)});
        commandRegistry[tokens.front()] = std::move(handler);
    }

    std::vector<CommandInfo> BuildHints(const std::string& inputText) const
    {
        std::vector<CommandInfo> hints;
        const std::vector<std::string> inputTokens = Tokenize(inputText);
        const std::string prefix = inputTokens.empty() ? std::string{} : inputTokens.front();

        for (const CommandInfo& info : commandInfos)
        {
            if (prefix.empty() || info.usage.rfind(prefix, 0) == 0)
            {
                hints.push_back(info);
            }
        }
        std::sort(hints.begin(), hints.end(), [](const CommandInfo& a, const CommandInfo& b) {
            if (a.category == b.category)
            {
                return a.usage < b.usage;
            }
            return a.category < b.category;
        });
        return hints;
    }

    void RegisterDefaultCommands()
    {
        RegisterCommand("help", "List all commands", [this](const std::vector<std::string>&, const ConsoleContext&) {
            PrintHelp();
        });

        RegisterCommand("fx_spawn <assetId>", "Spawn an FX asset at camera forward", [this](const std::vector<std::string>& tokens, const ConsoleContext& context) {
            if (context.gameplay == nullptr || tokens.size() != 2)
            {
                AddLog("Usage: fx_spawn <assetId>");
                return;
            }

            context.gameplay->SpawnFxDebug(tokens[1]);
            AddLog("FX spawn requested: " + tokens[1]);
        });

        RegisterCommand("fx_stop_all", "Stop all active FX instances", [this](const std::vector<std::string>&, const ConsoleContext& context) {
            if (context.gameplay == nullptr)
            {
                return;
            }
            context.gameplay->StopAllFx();
            AddLog("All FX stopped.");
        });

        RegisterCommand("fx_list", "List available FX assets", [this](const std::vector<std::string>&, const ConsoleContext& context) {
            if (context.gameplay == nullptr)
            {
                return;
            }
            const std::vector<std::string> assets = context.gameplay->ListFxAssets();
            if (assets.empty())
            {
                AddLog("No FX assets found.");
                return;
            }
            AddLog("FX assets:");
            for (const std::string& assetId : assets)
            {
                AddLog("  " + assetId);
            }
        });

        RegisterCommand("audio_play <clip> [bus]", "Play one-shot audio clip (bus: music|sfx|ui|ambience)", [this](const std::vector<std::string>& tokens, const ConsoleContext& context) {
            if (tokens.size() < 2 || context.audioPlay == nullptr)
            {
                AddLog("Usage: audio_play <clip> [bus]");
                return;
            }
            const std::string bus = tokens.size() >= 3 ? tokens[2] : "sfx";
            context.audioPlay(tokens[1], bus, false);
            AddLog("Audio one-shot requested: " + tokens[1] + " (" + bus + ")");
        });

        RegisterCommand("audio_loop <clip> [bus]", "Play looping audio clip (bus: music|sfx|ui|ambience)", [this](const std::vector<std::string>& tokens, const ConsoleContext& context) {
            if (tokens.size() < 2 || context.audioPlay == nullptr)
            {
                AddLog("Usage: audio_loop <clip> [bus]");
                return;
            }
            const std::string bus = tokens.size() >= 3 ? tokens[2] : "music";
            context.audioPlay(tokens[1], bus, true);
            AddLog("Audio loop requested: " + tokens[1] + " (" + bus + ")");
        });

        RegisterCommand("audio_stop_all", "Stop all active audio loops/sounds", [this](const std::vector<std::string>&, const ConsoleContext& context) {
            if (context.audioStopAll)
            {
                context.audioStopAll();
                AddLog("All audio stopped.");
            }
        });

        RegisterCommand("spawn survivor|killer|pallet|window", "Spawn gameplay entities", [this](const std::vector<std::string>& tokens, const ConsoleContext& context) {
            if (context.gameplay == nullptr || tokens.size() < 2)
            {
                AddLog("Usage: spawn survivor|killer|pallet|window");
                return;
            }

            if (tokens[1] == "survivor")
            {
                context.gameplay->SpawnSurvivor();
                AddLog("Spawned survivor.");
            }
            else if (tokens[1] == "killer")
            {
                context.gameplay->SpawnKiller();
                AddLog("Spawned killer.");
            }
            else if (tokens[1] == "pallet")
            {
                context.gameplay->SpawnPallet();
                AddLog("Spawned pallet.");
            }
            else if (tokens[1] == "window")
            {
                context.gameplay->SpawnWindow();
                AddLog("Spawned window.");
            }
            else
            {
                AddLog("Unknown spawn target.");
            }
        });

        RegisterCommand("spawn_survivor_here", "Spawn/respawn survivor at camera projected ground", [this](const std::vector<std::string>&, const ConsoleContext& context) {
            if (context.spawnRoleHere)
            {
                context.spawnRoleHere("survivor");
                AddLog("spawn_survivor_here requested.");
            }
        });

        RegisterCommand("spawn_killer_here", "Spawn/respawn killer at camera projected ground", [this](const std::vector<std::string>&, const ConsoleContext& context) {
            if (context.spawnRoleHere)
            {
                context.spawnRoleHere("killer");
                AddLog("spawn_killer_here requested.");
            }
        });

        RegisterCommand("spawn_survivor_at <spawnId>", "Spawn/respawn survivor at spawn point ID", [this](const std::vector<std::string>& tokens, const ConsoleContext& context) {
            if (tokens.size() != 2 || context.spawnRoleAt == nullptr)
            {
                AddLog("Usage: spawn_survivor_at <spawnId>");
                return;
            }
            context.spawnRoleAt("survivor", ParseIntOr(-1, tokens[1]));
            AddLog("spawn_survivor_at requested.");
        });

        RegisterCommand("spawn_killer_at <spawnId>", "Spawn/respawn killer at spawn point ID", [this](const std::vector<std::string>& tokens, const ConsoleContext& context) {
            if (tokens.size() != 2 || context.spawnRoleAt == nullptr)
            {
                AddLog("Usage: spawn_killer_at <spawnId>");
                return;
            }
            context.spawnRoleAt("killer", ParseIntOr(-1, tokens[1]));
            AddLog("spawn_killer_at requested.");
        });

        RegisterCommand("list_spawns", "List spawn points with IDs", [this](const std::vector<std::string>&, const ConsoleContext& context) {
            if (context.listSpawns)
            {
                AddLog(context.listSpawns());
            }
        });

        RegisterCommand("teleport survivor|killer x y z", "Teleport survivor or killer", [this](const std::vector<std::string>& tokens, const ConsoleContext& context) {
            if (context.gameplay == nullptr || tokens.size() != 5)
            {
                AddLog("Usage: teleport survivor|killer x y z");
                return;
            }

            const glm::vec3 position{
                ParseFloatOr(0.0F, tokens[2]),
                ParseFloatOr(1.0F, tokens[3]),
                ParseFloatOr(0.0F, tokens[4]),
            };

            if (tokens[1] == "survivor")
            {
                context.gameplay->TeleportSurvivor(position);
                AddLog("Teleported survivor.");
            }
            else if (tokens[1] == "killer")
            {
                context.gameplay->TeleportKiller(position);
                AddLog("Teleported killer.");
            }
            else
            {
                AddLog("Unknown teleport target.");
            }
        });

        RegisterCommand("give_speed survivor 6.0", "Set survivor sprint speed", [this](const std::vector<std::string>& tokens, const ConsoleContext& context) {
            if (context.gameplay == nullptr || tokens.size() != 3 || tokens[1] != "survivor")
            {
                AddLog("Usage: give_speed survivor 6.0");
                return;
            }

            context.gameplay->SetSurvivorSprintSpeed(ParseFloatOr(6.0F, tokens[2]));
            AddLog("Updated survivor sprint speed.");
        });

        RegisterCommand("set_speed survivor|killer <percent>", "Set role movement speed percent (100 = default)", [this](const std::vector<std::string>& tokens, const ConsoleContext& context) {
            if (context.gameplay == nullptr || tokens.size() != 3)
            {
                AddLog("Usage: set_speed survivor|killer <percent>");
                return;
            }
            if (tokens[1] != "survivor" && tokens[1] != "killer")
            {
                AddLog("Role must be survivor or killer.");
                return;
            }

            float value = ParseFloatOr(100.0F, tokens[2]);
            if (value <= 0.0F)
            {
                AddLog("Percent must be > 0.");
                return;
            }

            if (value > 10.0F)
            {
                value *= 0.01F;
            }
            context.gameplay->SetRoleSpeedPercent(tokens[1], value);
            AddLog("Updated " + tokens[1] + " speed multiplier to " + std::to_string(value));
        });

        RegisterCommand("set_size survivor|killer <radius> <height>", "Set role capsule size", [this](const std::vector<std::string>& tokens, const ConsoleContext& context) {
            if (context.gameplay == nullptr || tokens.size() != 4)
            {
                AddLog("Usage: set_size survivor|killer <radius> <height>");
                return;
            }
            if (tokens[1] != "survivor" && tokens[1] != "killer")
            {
                AddLog("Role must be survivor or killer.");
                return;
            }

            const float radius = ParseFloatOr(0.35F, tokens[2]);
            const float height = ParseFloatOr(1.8F, tokens[3]);
            context.gameplay->SetRoleCapsuleSize(tokens[1], radius, height);
            AddLog("Updated " + tokens[1] + " capsule size.");
        });

        RegisterCommand("heal survivor", "Heal survivor (Injured -> Healthy)", [this](const std::vector<std::string>& tokens, const ConsoleContext& context) {
            if (context.gameplay == nullptr || tokens.size() != 2 || tokens[1] != "survivor")
            {
                AddLog("Usage: heal survivor");
                return;
            }

            context.gameplay->HealSurvivor();
            AddLog("Heal requested for survivor.");
        });

        RegisterCommand("survivor_state healthy|injured|downed|carried|hooked|dead", "Force survivor FSM state (debug)", [this](const std::vector<std::string>& tokens, const ConsoleContext& context) {
            if (context.gameplay == nullptr || tokens.size() != 2)
            {
                AddLog("Usage: survivor_state healthy|injured|downed|carried|hooked|dead");
                return;
            }

            context.gameplay->SetSurvivorStateDebug(tokens[1]);
            AddLog("Survivor state debug command sent.");
        });

        RegisterCommand("set_generators_done 0..5", "Set generator completion count", [this](const std::vector<std::string>& tokens, const ConsoleContext& context) {
            if (context.gameplay == nullptr || tokens.size() != 2)
            {
                AddLog("Usage: set_generators_done <count>");
                return;
            }

            context.gameplay->SetGeneratorsCompleted(ParseIntOr(0, tokens[1]));
            AddLog("Generator progress updated.");
        });

        RegisterCommand("hook_survivor", "Hook carried survivor on nearest hook", [this](const std::vector<std::string>&, const ConsoleContext& context) {
            if (context.gameplay == nullptr)
            {
                return;
            }

            context.gameplay->HookCarriedSurvivorDebug();
            AddLog("Hook command requested.");
        });

        RegisterCommand("skillcheck start", "Start skillcheck widget (debug)", [this](const std::vector<std::string>& tokens, const ConsoleContext& context) {
            if (context.gameplay == nullptr || tokens.size() != 2 || tokens[1] != "start")
            {
                AddLog("Usage: skillcheck start");
                return;
            }

            context.gameplay->StartSkillCheckDebug();
            AddLog("Skillcheck start requested.");
        });

        RegisterCommand("toggle_collision on|off", "Enable/disable collision", [this](const std::vector<std::string>& tokens, const ConsoleContext& context) {
            if (context.gameplay == nullptr || tokens.size() != 2)
            {
                AddLog("Usage: toggle_collision on|off");
                return;
            }

            bool enabled = true;
            if (!ParseBoolToken(tokens[1], enabled))
            {
                AddLog("Expected on|off.");
                return;
            }

            context.gameplay->ToggleCollision(enabled);
            AddLog(std::string("Collision ") + (enabled ? "enabled." : "disabled."));
        });

        RegisterCommand("toggle_debug_draw on|off", "Enable/disable collider and trigger debug draw", [this](const std::vector<std::string>& tokens, const ConsoleContext& context) {
            if (context.gameplay == nullptr || tokens.size() != 2)
            {
                AddLog("Usage: toggle_debug_draw on|off");
                return;
            }

            bool enabled = true;
            if (!ParseBoolToken(tokens[1], enabled))
            {
                AddLog("Expected on|off.");
                return;
            }

            context.gameplay->ToggleDebugDraw(enabled);
            AddLog(std::string("Debug draw ") + (enabled ? "enabled." : "disabled."));
        });

        RegisterCommand("physics_debug on|off", "Toggle physics debug readout", [this](const std::vector<std::string>& tokens, const ConsoleContext& context) {
            if (context.gameplay == nullptr || tokens.size() != 2)
            {
                AddLog("Usage: physics_debug on|off");
                return;
            }

            bool enabled = true;
            if (!ParseBoolToken(tokens[1], enabled))
            {
                AddLog("Expected on|off.");
                return;
            }

            context.gameplay->TogglePhysicsDebug(enabled);
            if (context.setPhysicsDebug)
            {
                context.setPhysicsDebug(enabled);
            }
            AddLog(std::string("Physics debug ") + (enabled ? "enabled." : "disabled."));
        });

        RegisterCommand("noclip on|off", "Toggle noclip for players", [this](const std::vector<std::string>& tokens, const ConsoleContext& context) {
            if (context.gameplay == nullptr || tokens.size() != 2)
            {
                AddLog("Usage: noclip on|off");
                return;
            }

            bool enabled = false;
            if (!ParseBoolToken(tokens[1], enabled))
            {
                AddLog("Expected on|off.");
                return;
            }

            context.gameplay->SetNoClip(enabled);
            if (context.setNoClip)
            {
                context.setNoClip(enabled);
            }
            AddLog(std::string("Noclip ") + (enabled ? "enabled." : "disabled."));
        });

        RegisterCommand("load map test|main|main_map|collision_test", "Load gameplay scene", [this](const std::vector<std::string>& tokens, const ConsoleContext& context) {
            if (context.gameplay == nullptr || tokens.size() != 3 || tokens[1] != "map")
            {
                AddLog("Usage: load map test|main|main_map|collision_test");
                return;
            }

            std::string mapName = tokens[2];
            if (mapName == "main_map")
            {
                mapName = "main";
            }
            context.gameplay->LoadMap(mapName);
            AddLog("Map loaded: " + mapName);
        });

        RegisterCommand("host [port]", "Host listen server (default 7777)", [this](const std::vector<std::string>& tokens, const ConsoleContext& context) {
            if (context.hostSession == nullptr || tokens.size() > 2)
            {
                AddLog("Usage: host [port]");
                return;
            }

            const int port = std::clamp(ParseIntOr(7777, tokens.size() == 2 ? tokens[1] : "7777"), 1, 65535);
            context.hostSession(port);
            AddLog("Host requested on port " + std::to_string(port));
        });

        RegisterCommand("join <ip> <port>", "Join listen server", [this](const std::vector<std::string>& tokens, const ConsoleContext& context) {
            if (context.joinSession == nullptr || tokens.size() != 3)
            {
                AddLog("Usage: join <ip> <port>");
                return;
            }

            const int port = std::clamp(ParseIntOr(7777, tokens[2]), 1, 65535);
            context.joinSession(tokens[1], port);
            AddLog("Join requested.");
        });

        RegisterCommand("disconnect", "Disconnect and return to menu", [this](const std::vector<std::string>&, const ConsoleContext& context) {
            if (context.disconnectSession)
            {
                context.disconnectSession();
                AddLog("Disconnect requested.");
            }
        });

        RegisterCommand("net_status", "Print network state and diagnostics", [this](const std::vector<std::string>&, const ConsoleContext& context) {
            if (context.netStatus)
            {
                AddLog(context.netStatus());
            }
        });

        RegisterCommand("net_dump", "Print network config/tick/interpolation", [this](const std::vector<std::string>&, const ConsoleContext& context) {
            if (context.netDump)
            {
                AddLog(context.netDump());
            }
        });

        RegisterCommand("lan_scan", "Force LAN discovery scan", [this](const std::vector<std::string>&, const ConsoleContext& context) {
            if (context.lanScan)
            {
                context.lanScan();
                AddLog("LAN scan requested.");
            }
        });

        RegisterCommand("lan_status", "Print LAN discovery status", [this](const std::vector<std::string>&, const ConsoleContext& context) {
            if (context.lanStatus)
            {
                AddLog(context.lanStatus());
            }
        });

        RegisterCommand("lan_debug on|off", "Toggle LAN discovery debug", [this](const std::vector<std::string>& tokens, const ConsoleContext& context) {
            if (tokens.size() != 2 || context.lanDebug == nullptr)
            {
                AddLog("Usage: lan_debug on|off");
                return;
            }

            bool enabled = false;
            if (!ParseBoolToken(tokens[1], enabled))
            {
                AddLog("Expected on|off.");
                return;
            }

            context.lanDebug(enabled);
            AddLog(std::string("LAN debug ") + (enabled ? "enabled." : "disabled."));
        });

        RegisterCommand("tr_vis on|off", "Toggle terror radius visualization", [this](const std::vector<std::string>& tokens, const ConsoleContext& context) {
            if (tokens.size() != 2)
            {
                AddLog("Usage: tr_vis on|off");
                return;
            }

            bool enabled = true;
            if (!ParseBoolToken(tokens[1], enabled))
            {
                AddLog("Expected on|off.");
                return;
            }

            if (context.gameplay != nullptr)
            {
                context.gameplay->ToggleTerrorRadiusVisualization(enabled);
            }
            if (context.setTerrorRadiusVisible)
            {
                context.setTerrorRadiusVisible(enabled);
            }
            AddLog(std::string("Terror radius ") + (enabled ? "enabled." : "disabled."));
        });

        RegisterCommand("tr_set <meters>", "Set terror radius meters", [this](const std::vector<std::string>& tokens, const ConsoleContext& context) {
            if (tokens.size() != 2)
            {
                AddLog("Usage: tr_set <meters>");
                return;
            }

            const float meters = std::max(1.0F, ParseFloatOr(24.0F, tokens[1]));
            if (context.gameplay != nullptr)
            {
                context.gameplay->SetTerrorRadius(meters);
            }
            if (context.setTerrorRadiusMeters)
            {
                context.setTerrorRadiusMeters(meters);
            }
            AddLog("Terror radius set to " + std::to_string(meters));
        });

        RegisterCommand("tr_debug on|off", "Toggle terror radius audio debug mode", [this](const std::vector<std::string>& tokens, const ConsoleContext& context) {
            if (tokens.size() != 2 || context.setTerrorAudioDebug == nullptr)
            {
                AddLog("Usage: tr_debug on|off");
                return;
            }
            bool enabled = false;
            if (!ParseBoolToken(tokens[1], enabled))
            {
                AddLog("Expected on|off.");
                return;
            }
            context.setTerrorAudioDebug(enabled);
            AddLog(std::string("Terror radius audio debug ") + (enabled ? "enabled." : "disabled."));
        });

        RegisterCommand("regen_loops [seed]", "Regenerate loop layout on main map (optional deterministic seed)", [this](const std::vector<std::string>& tokens, const ConsoleContext& context) {
            if (context.gameplay == nullptr)
            {
                return;
            }

            if (tokens.size() == 2)
            {
                const int parsedSeed = ParseIntOr(1337, tokens[1]);
                context.gameplay->RegenerateLoops(static_cast<unsigned int>(std::max(1, parsedSeed)));
                AddLog("Regenerated loops with explicit seed.");
            }
            else
            {
                context.gameplay->RegenerateLoops();
                AddLog("Regenerated loops with incremented seed.");
            }
        });

        RegisterCommand("dbd_spawns on|off", "Enable/disable DBD-inspired spawn system", [this](const std::vector<std::string>& tokens, const ConsoleContext& context) {
            if (context.gameplay == nullptr || tokens.size() != 2)
            {
                AddLog("Usage: dbd_spawns on|off");
                return;
            }

            bool enabled = false;
            if (!ParseBoolToken(tokens[1], enabled))
            {
                AddLog("Expected on|off.");
                return;
            }

            context.gameplay->SetDbdSpawnsEnabled(enabled);
            AddLog(std::string("DBD spawns ") + (enabled ? "enabled." : "disabled."));
        });

        RegisterCommand("perks <list|equip|clear>", "Manage perks (list/equip/clear)", [this](const std::vector<std::string>& tokens, const ConsoleContext& context) {
            if (context.gameplay == nullptr)
            {
                return;
            }

            if (tokens.size() < 2)
            {
                AddLog("Usage: perks <list|equip|clear>");
                AddLog("  perks list");
                AddLog("  perks equip <role> <slot> <id>");
                AddLog("  perks clear <role>");
                return;
            }

            const std::string& subcommand = tokens[1];
            if (subcommand == "list")
            {
                const auto& perkSystem = context.gameplay->GetPerkSystem();
                std::vector<std::string> survivorPerks = perkSystem.ListPerks(game::gameplay::perks::PerkRole::Survivor);
                std::vector<std::string> killerPerks = perkSystem.ListPerks(game::gameplay::perks::PerkRole::Killer);
                std::vector<std::string> bothPerks = perkSystem.ListPerks(game::gameplay::perks::PerkRole::Both);

                AddLog("=== SURVIVOR PERKS ===");
                for (const auto& id : survivorPerks)
                {
                    const auto* perk = perkSystem.GetPerk(id);
                    if (perk)
                    {
                        AddLog(id + " - " + perk->name);
                    }
                }

                AddLog("=== KILLER PERKS ===");
                for (const auto& id : killerPerks)
                {
                    const auto* perk = perkSystem.GetPerk(id);
                    if (perk)
                    {
                        AddLog(id + " - " + perk->name);
                    }
                }

                if (!bothPerks.empty())
                {
                    AddLog("=== BOTH ROLES ===");
                    for (const auto& id : bothPerks)
                    {
                        const auto* perk = perkSystem.GetPerk(id);
                        if (perk)
                        {
                            AddLog(id + " - " + perk->name);
                        }
                    }
                }

                AddLog("Total: " + std::to_string(survivorPerks.size() + killerPerks.size() + bothPerks.size()) + " perks");
                return;
            }

            if (subcommand == "equip")
            {
                if (tokens.size() != 5)
                {
                    AddLog("Usage: perks equip <role> <slot> <id>");
                    AddLog("  role: survivor | killer");
                    AddLog("  slot: 0 | 1 | 2");
                    AddLog("  id: perk_id (use 'perks list' to see available)");
                    return;
                }

                const std::string& roleName = tokens[2];
                if (roleName != "survivor" && roleName != "killer")
                {
                    AddLog("Role must be 'survivor' or 'killer'");
                    return;
                }

                const int slot = ParseIntOr(-1, tokens[3]);
                if (slot < 0 || slot > 2)
                {
                    AddLog("Invalid slot (must be 0, 1, or 2)");
                    return;
                }

                const std::string& perkId = tokens[4];
                const auto& perkSystem = context.gameplay->GetPerkSystem();
                const auto* perk = perkSystem.GetPerk(perkId);
                if (!perk)
                {
                    AddLog("Perk not found: " + perkId + " (use 'perks list' to see available)");
                    return;
                }

                const auto role = (roleName == "survivor") ? game::gameplay::perks::PerkRole::Survivor : game::gameplay::perks::PerkRole::Killer;
                if (perk->role != game::gameplay::perks::PerkRole::Both && perk->role != role)
                {
                    AddLog("Perk '" + perk->name + "' is not for " + roleName);
                    return;
                }

                game::gameplay::perks::PerkLoadout loadout;
                if (roleName == "survivor")
                {
                    loadout = perkSystem.GetSurvivorLoadout();
                }
                else
                {
                    loadout = perkSystem.GetKillerLoadout();
                }

                loadout.SetPerk(slot, perkId);

                if (roleName == "survivor")
                {
                    context.gameplay->SetSurvivorPerkLoadout(loadout);
                }
                else
                {
                    context.gameplay->SetKillerPerkLoadout(loadout);
                }

                AddLog("Equipped '" + perk->name + "' for " + roleName + " in slot " + std::to_string(slot));
                return;
            }

            if (subcommand == "clear")
            {
                if (tokens.size() != 3)
                {
                    AddLog("Usage: perks clear <role>");
                    AddLog("  role: survivor | killer");
                    return;
                }

                const std::string& roleName = tokens[2];
                if (roleName != "survivor" && roleName != "killer")
                {
                    AddLog("Role must be 'survivor' or 'killer'");
                    return;
                }

                game::gameplay::perks::PerkLoadout loadout;
                loadout.Clear();

                if (roleName == "survivor")
                {
                    context.gameplay->SetSurvivorPerkLoadout(loadout);
                }
                else
                {
                    context.gameplay->SetKillerPerkLoadout(loadout);
                }

                AddLog("Cleared all perks for " + roleName);
                return;
            }

            AddLog("Unknown perks subcommand. Use: perks list | perks equip | perks clear");
        });

        RegisterCommand("set_chase on|off", "Force chase state", [this](const std::vector<std::string>& tokens, const ConsoleContext& context) {
            if (context.gameplay == nullptr || tokens.size() != 2)
            {
                AddLog("Usage: set_chase on|off");
                return;
            }

            bool enabled = true;
            if (!ParseBoolToken(tokens[1], enabled))
            {
                AddLog("Expected on|off.");
                return;
            }

            context.gameplay->SetForcedChase(enabled);
            AddLog(std::string("Forced chase ") + (enabled ? "enabled." : "disabled."));
        });

        RegisterCommand("cam_mode survivor|killer|role", "Force camera mode (3rd/1st/role-based)", [this](const std::vector<std::string>& tokens, const ConsoleContext& context) {
            if (context.gameplay == nullptr || tokens.size() != 2)
            {
                AddLog("Usage: cam_mode survivor|killer|role");
                return;
            }

            context.gameplay->SetCameraModeOverride(tokens[1]);
            if (context.setCameraMode)
            {
                context.setCameraMode(tokens[1]);
            }
            AddLog("Camera mode updated.");
        });

        RegisterCommand("control_role survivor|killer", "Switch controlled role", [this](const std::vector<std::string>& tokens, const ConsoleContext& context) {
            if (context.gameplay == nullptr || tokens.size() != 2)
            {
                AddLog("Usage: control_role survivor|killer");
                return;
            }

            if (context.requestRoleChange)
            {
                context.requestRoleChange(tokens[1]);
            }
            else
            {
                context.gameplay->SetControlledRole(tokens[1]);
                if (context.setControlledRole)
                {
                    context.setControlledRole(tokens[1]);
                }
            }
            AddLog("Controlled role changed.");
        });

        RegisterCommand("set_role survivor|killer", "Alias for control_role", [this](const std::vector<std::string>& tokens, const ConsoleContext& context) {
            if (context.gameplay == nullptr || tokens.size() != 2)
            {
                AddLog("Usage: set_role survivor|killer");
                return;
            }

            if (context.requestRoleChange)
            {
                context.requestRoleChange(tokens[1]);
            }
            else
            {
                context.gameplay->SetControlledRole(tokens[1]);
                if (context.setControlledRole)
                {
                    context.setControlledRole(tokens[1]);
                }
            }
            AddLog("Role set.");
        });

        RegisterCommand("player_dump", "Print player->pawn ownership mapping", [this](const std::vector<std::string>&, const ConsoleContext& context) {
            if (context.playerDump)
            {
                AddLog(context.playerDump());
            }
        });

        RegisterCommand("scene_dump", "Print current scene entities summary", [this](const std::vector<std::string>&, const ConsoleContext& context) {
            if (context.sceneDump)
            {
                AddLog(context.sceneDump());
            }
        });

        RegisterCommand("render_mode wireframe|filled", "Set render mode", [this](const std::vector<std::string>& tokens, const ConsoleContext& context) {
            if (tokens.size() != 2)
            {
                AddLog("Usage: render_mode wireframe|filled");
                return;
            }

            if (context.applyRenderMode)
            {
                context.applyRenderMode(tokens[1]);
                AddLog("Render mode set to " + tokens[1]);
            }
        });

        RegisterCommand("quit", "Quit application", [this](const std::vector<std::string>&, const ConsoleContext& context) {
            if (context.gameplay != nullptr)
            {
                context.gameplay->RequestQuit();
            }
            AddLog("Quit requested.");
        });

        RegisterCommand("set_vsync on|off", "Toggle VSync", [this](const std::vector<std::string>& tokens, const ConsoleContext& context) {
            if (tokens.size() != 2)
            {
                AddLog("Usage: set_vsync on|off");
                return;
            }

            bool enabled = true;
            if (!ParseBoolToken(tokens[1], enabled))
            {
                AddLog("Expected on|off.");
                return;
            }

            if (context.vsync != nullptr)
            {
                *context.vsync = enabled;
            }
            if (context.applyVsync)
            {
                context.applyVsync(enabled);
            }
            AddLog(std::string("VSync ") + (enabled ? "enabled." : "disabled."));
        });

        RegisterCommand("set_fps 120", "Set FPS limit", [this](const std::vector<std::string>& tokens, const ConsoleContext& context) {
            if (tokens.size() != 2)
            {
                AddLog("Usage: set_fps <limit>");
                return;
            }

            const int fps = std::max(30, ParseIntOr(120, tokens[1]));
            if (context.fpsLimit != nullptr)
            {
                *context.fpsLimit = fps;
            }
            if (context.applyFpsLimit)
            {
                context.applyFpsLimit(fps);
            }
            AddLog("FPS limit set to " + std::to_string(fps));
        });

        RegisterCommand("set_tick 30|60", "Set fixed simulation tick rate", [this](const std::vector<std::string>& tokens, const ConsoleContext& context) {
            if (tokens.size() != 2 || context.setTickRate == nullptr)
            {
                AddLog("Usage: set_tick 30|60");
                return;
            }

            const int requested = ParseIntOr(60, tokens[1]);
            const int hz = requested <= 30 ? 30 : 60;
            context.setTickRate(hz);
            AddLog("Fixed tick set to " + std::to_string(hz) + " Hz.");
        });

        RegisterCommand("set_resolution 1600 900", "Set window resolution", [this](const std::vector<std::string>& tokens, const ConsoleContext& context) {
            if (tokens.size() != 3)
            {
                AddLog("Usage: set_resolution <width> <height>");
                return;
            }

            const int width = std::max(640, ParseIntOr(1600, tokens[1]));
            const int height = std::max(360, ParseIntOr(900, tokens[2]));
            if (context.applyResolution)
            {
                context.applyResolution(width, height);
            }
            AddLog("Resolution set.");
        });

        RegisterCommand("toggle_fullscreen", "Toggle fullscreen", [this](const std::vector<std::string>&, const ConsoleContext& context) {
            if (context.toggleFullscreen)
            {
                context.toggleFullscreen();
                AddLog("Toggled fullscreen.");
            }
        });
    }

    void ExecuteCommand(const std::string& commandLine, const ConsoleContext& context)
    {
        AddLog("# " + commandLine);

        std::vector<std::string> tokens = Tokenize(commandLine);
        if (tokens.empty())
        {
            return;
        }

        history.erase(std::remove(history.begin(), history.end(), commandLine), history.end());
        history.push_back(commandLine);
        historyPos = -1;

        const auto it = commandRegistry.find(tokens[0]);
        if (it == commandRegistry.end())
        {
            AddLog("Unknown command. Type `help`.");
            return;
        }

        it->second(tokens, context);
    }

    static int TextEditCallbackStub(ImGuiInputTextCallbackData* data)
    {
        Impl* impl = static_cast<Impl*>(data->UserData);
        return impl->TextEditCallback(data);
    }

    int TextEditCallback(ImGuiInputTextCallbackData* data)
    {
        switch (data->EventFlag)
        {
            case ImGuiInputTextFlags_CallbackCompletion:
            {
                const char* wordEnd = data->Buf + data->CursorPos;
                const char* wordStart = wordEnd;
                while (wordStart > data->Buf && wordStart[-1] != ' ' && wordStart[-1] != '\t')
                {
                    --wordStart;
                }

                std::vector<std::string> candidates;
                for (const CommandInfo& info : commandInfos)
                {
                    if (info.usage.rfind(wordStart, 0) == 0)
                    {
                        candidates.push_back(info.usage);
                    }
                }

                if (candidates.size() == 1)
                {
                    data->DeleteChars(static_cast<int>(wordStart - data->Buf), static_cast<int>(wordEnd - wordStart));
                    data->InsertChars(data->CursorPos, candidates[0].c_str());
                }
                else if (candidates.size() > 1)
                {
                    AddLog("Possible matches:");
                    for (const std::string& candidate : candidates)
                    {
                        AddLog("  " + candidate);
                    }
                }
                break;
            }
            case ImGuiInputTextFlags_CallbackHistory:
            {
                const int previousHistoryPos = historyPos;
                if (data->EventKey == ImGuiKey_UpArrow)
                {
                    if (historyPos == -1)
                    {
                        historyPos = static_cast<int>(history.size()) - 1;
                    }
                    else if (historyPos > 0)
                    {
                        --historyPos;
                    }
                }
                else if (data->EventKey == ImGuiKey_DownArrow)
                {
                    if (historyPos != -1)
                    {
                        if (++historyPos >= static_cast<int>(history.size()))
                        {
                            historyPos = -1;
                        }
                    }
                }

                if (previousHistoryPos != historyPos)
                {
                    const char* historyText = (historyPos >= 0) ? history[static_cast<size_t>(historyPos)].c_str() : "";
                    data->DeleteChars(0, data->BufTextLen);
                    data->InsertChars(0, historyText);
                }
                break;
            }
            default:
                break;
        }

        return 0;
    }
};
#endif

bool DeveloperConsole::Initialize(engine::platform::Window& window)
{
#if BUILD_WITH_IMGUI
    m_impl = new Impl();
    m_impl->RegisterDefaultCommands();
    m_impl->AddLog("Developer console ready. Press ~ to toggle.");

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    ImGui::StyleColorsDark();

    ImGui_ImplGlfw_InitForOpenGL(window.NativeHandle(), true);
    ImGui_ImplOpenGL3_Init("#version 450");
#else
    (void)window;
#endif
    return true;
}

void DeveloperConsole::Shutdown()
{
#if BUILD_WITH_IMGUI
    if (m_impl != nullptr)
    {
        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();

        delete m_impl;
        m_impl = nullptr;
    }
#endif
}

void DeveloperConsole::BeginFrame()
{
#if BUILD_WITH_IMGUI
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
#endif
}

void DeveloperConsole::Render(const ConsoleContext& context, float fps, const game::gameplay::HudState& hudState)
{
#if BUILD_WITH_IMGUI
    if (m_impl == nullptr)
    {
        return;
    }

    const bool showOverlay = context.showDebugOverlay == nullptr || *context.showDebugOverlay;

    if (context.renderPlayerHud)
    {
        // Legacy ImGui gameplay HUD (disabled when custom HUD is active).
        ImGui::SetNextWindowBgAlpha(0.46F);
        ImGui::SetNextWindowPos(ImVec2(10.0F, 10.0F), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Player State", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
        ImGui::Text("Role: %s", hudState.roleName.c_str());
        ImGui::Text("State: %s", hudState.survivorStateName.c_str());
        ImGui::Text("Move: %s", hudState.movementStateName.c_str());
        ImGui::Text("Camera: %s", hudState.cameraModeName.c_str());
        ImGui::Text("Chase: %s", hudState.chaseActive ? "ON" : "OFF");
        ImGui::Text("Render: %s", hudState.renderModeName.c_str());
        ImGui::Text("Attack: %s", hudState.killerAttackStateName.c_str());
        if (hudState.roleName == "Killer")
        {
            ImGui::TextUnformatted(hudState.attackHint.c_str());
        }
        if (hudState.roleName == "Killer" && hudState.lungeCharge01 > 0.0F)
        {
            ImGui::ProgressBar(hudState.lungeCharge01, ImVec2(220.0F, 0.0F), "Lunge momentum");
        }
        if (hudState.selfHealing)
        {
            ImGui::ProgressBar(hudState.selfHealProgress, ImVec2(220.0F, 0.0F), "Self-heal");
        }
        if (hudState.roleName == "Survivor" && hudState.survivorStateName == "Carried")
        {
            ImGui::TextUnformatted("Wiggle: Alternate A/D to escape");
            ImGui::ProgressBar(hudState.carryEscapeProgress, ImVec2(220.0F, 0.0F), "Carry escape");
        }
        ImGui::Text("Terror Radius: %s %.1fm", hudState.terrorRadiusVisible ? "ON" : "OFF", hudState.terrorRadiusMeters);
        ImGui::TextUnformatted("Press ~ for Console");
        }
        ImGui::End();

        ImGui::SetNextWindowBgAlpha(0.46F);
        ImGui::SetNextWindowPos(ImVec2(10.0F, 165.0F), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Generator Progress", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
        ImGui::Text("Generators: %d/%d", hudState.generatorsCompleted, hudState.generatorsTotal);
        if (hudState.repairingGenerator)
        {
            ImGui::ProgressBar(hudState.activeGeneratorProgress, ImVec2(220.0F, 0.0F));
        }
        }
        ImGui::End();

        if (!hudState.interactionPrompt.empty())
        {
        const ImVec2 center = ImGui::GetMainViewport()->GetCenter();
        ImGui::SetNextWindowBgAlpha(0.62F);
        ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5F, 0.5F));
        if (ImGui::Begin("Interaction Prompt", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar))
        {
            ImGui::TextUnformatted(hudState.interactionPrompt.c_str());
        }
        ImGui::End();
        }

        if (hudState.skillCheckActive)
        {
        const ImVec2 center = ImGui::GetMainViewport()->GetCenter();
        const ImVec2 size{220.0F, 180.0F};
        ImGui::SetNextWindowBgAlpha(0.70F);
        ImGui::SetNextWindowPos(ImVec2(center.x - size.x * 0.5F, center.y + 90.0F), ImGuiCond_Always);
        ImGui::SetNextWindowSize(size, ImGuiCond_Always);
        if (ImGui::Begin("Skill Check Widget", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize))
        {
            ImGui::TextUnformatted("SKILL CHECK");
            ImGui::TextUnformatted("Press SPACE in green zone");

            ImDrawList* drawList = ImGui::GetWindowDrawList();
            const ImVec2 winPos = ImGui::GetWindowPos();
            const ImVec2 localCenter = ImVec2(winPos.x + size.x * 0.5F, winPos.y + 108.0F);
            constexpr float radius = 56.0F;

            drawList->AddCircle(localCenter, radius, IM_COL32(190, 190, 190, 255), 64, 2.0F);

            constexpr float kPi = 3.1415926535F;
            const float startAngle = -kPi * 0.5F;
            const float successStart = startAngle + hudState.skillCheckSuccessStart * 2.0F * kPi;
            const float successEnd = startAngle + hudState.skillCheckSuccessEnd * 2.0F * kPi;
            drawList->PathArcTo(localCenter, radius + 1.0F, successStart, successEnd, 28);
            drawList->PathStroke(IM_COL32(80, 220, 110, 255), false, 6.0F);

            const float needleAngle = startAngle + hudState.skillCheckNeedle * 2.0F * kPi;
            const ImVec2 needleEnd{
                localCenter.x + std::cos(needleAngle) * (radius - 5.0F),
                localCenter.y + std::sin(needleAngle) * (radius - 5.0F),
            };
            drawList->AddLine(localCenter, needleEnd, IM_COL32(240, 80, 80, 255), 3.0F);
            drawList->AddCircleFilled(localCenter, 4.0F, IM_COL32(240, 240, 240, 255));
        }
        ImGui::End();
        }

        if (showOverlay)
        {
        ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowBgAlpha(0.56F);
        ImGui::SetNextWindowPos(
            ImVec2(viewport->Pos.x + viewport->Size.x - 12.0F, viewport->Pos.y + 12.0F),
            ImGuiCond_FirstUseEver,
            ImVec2(1.0F, 0.0F)
        );
        if (ImGui::Begin("HUD", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::Text("Role: %s", hudState.roleName.c_str());
            ImGui::Text("Camera: %s", hudState.cameraModeName.c_str());
            ImGui::Text("Render: %s", hudState.renderModeName.c_str());
            ImGui::Separator();
            ImGui::Text("FPS: %.1f", fps);
            ImGui::Text("Speed: %.2f", hudState.playerSpeed);
            ImGui::Text("Grounded: %s", hudState.grounded ? "true" : "false");
            ImGui::Text("Chase: %s", hudState.chaseActive ? "ON" : "OFF");
            ImGui::Text("Distance: %.2f", hudState.chaseDistance);
            ImGui::Text("LOS: %s", hudState.lineOfSight ? "true" : "false");
            ImGui::Text("MoveState: %s", hudState.movementStateName.c_str());
            ImGui::Text("KillerAttack: %s", hudState.killerAttackStateName.c_str());
            ImGui::Text("LungeCharge: %.0f%%", hudState.lungeCharge01 * 100.0F);
            ImGui::Text("Map: %s", hudState.mapName.c_str());
            ImGui::Text("Loop Tile: %d", hudState.activeLoopTileId);
            ImGui::Text("Loop Archetype: %s", hudState.activeLoopArchetype.c_str());
            ImGui::Text("Generators: %d/%d", hudState.generatorsCompleted, hudState.generatorsTotal);
            ImGui::Text("Survivor FSM: %s", hudState.survivorStateName.c_str());
            ImGui::Text("Carry Escape: %.0f%%", hudState.carryEscapeProgress * 100.0F);
            if (hudState.carryEscapeProgress > 0.0F)
            {
                ImGui::TextUnformatted("Wiggle: Alternate A/D");
            }
            ImGui::Text("Hook Stage: %d", hudState.hookStage);
            ImGui::Text("Hook Progress: %.0f%%", hudState.hookStageProgress * 100.0F);
            ImGui::Text("Repairing: %s", hudState.repairingGenerator ? "yes" : "no");
            ImGui::Text("Generator Progress: %.0f%%", hudState.activeGeneratorProgress * 100.0F);
            ImGui::TextUnformatted("Survivors:");
            for (const std::string& survivor : hudState.survivorStates)
            {
                ImGui::Text("  %s", survivor.c_str());
            }
            ImGui::Text("VaultType: %s", hudState.vaultTypeName.c_str());
            ImGui::Text("Interaction: %s", hudState.interactionTypeName.c_str());
            ImGui::Text("Target: %s", hudState.interactionTargetName.c_str());
            ImGui::Text("Priority: %d", hudState.interactionPriority);
            ImGui::Separator();
            ImGui::Text("FX Instances: %d", hudState.fxActiveInstances);
            ImGui::Text("FX Particles: %d", hudState.fxActiveParticles);
            ImGui::Text("FX CPU: %.3f ms", hudState.fxCpuMs);
            ImGui::Separator();
            ImGui::Text("WASD: Move");
            ImGui::Text("Mouse: Look");
            ImGui::Text("Shift: Sprint (Survivor)");
            ImGui::Text("Ctrl: Crouch (Survivor)");
            ImGui::Text("E: Interact");
            ImGui::Text("Space: Jump (N/A)");
            ImGui::Text("LMB click: Short swing (Killer)");
            ImGui::Text("Hold LMB: Lunge (Killer)");
            ImGui::Text("Space: Skill Check (Repair)");
            ImGui::Text("~: Console");
            ImGui::Text("F1/F2/F3/F4/F5: HUD/DebugDraw/RenderMode/NetDebug/TerrorRadius");
            ImGui::Text("Press ~ for Console");

            if (hudState.physicsDebugEnabled)
            {
                ImGui::Separator();
                ImGui::Text("Velocity: (%.2f, %.2f, %.2f)", hudState.velocity.x, hudState.velocity.y, hudState.velocity.z);
                ImGui::Text("Last Normal: (%.2f, %.2f, %.2f)", hudState.lastCollisionNormal.x, hudState.lastCollisionNormal.y, hudState.lastCollisionNormal.z);
                ImGui::Text("Penetration: %.4f", hudState.penetrationDepth);
            }
        }
        ImGui::End();

        if (!hudState.runtimeMessage.empty())
        {
            ImGui::SetNextWindowBgAlpha(0.45F);
            ImGui::SetNextWindowPos(ImVec2(0.0F, 48.0F), ImGuiCond_Always, ImVec2(0.5F, 0.0F));
            if (ImGui::Begin("RuntimeMsg", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar))
            {
                ImGui::TextUnformatted(hudState.runtimeMessage.c_str());
            }
            ImGui::End();
        }

        // Debug overlay: always draw perks at top-left when F2 is active
        if (context.gameplay != nullptr && !hudState.debugActors.empty())
        {
            const ImGuiViewport* viewport = ImGui::GetMainViewport();
            const float aspect = viewport->Size.y > 1.0F ? viewport->Size.x / viewport->Size.y : (16.0F / 9.0F);
            const glm::mat4 viewProjection = context.gameplay->BuildViewProjection(aspect);
            ImDrawList* drawList = ImGui::GetForegroundDrawList();

            auto projectWorld = [&](const glm::vec3& world, ImVec2& outScreen) {
                const glm::vec4 clip = viewProjection * glm::vec4(world, 1.0F);
                if (clip.w <= 0.01F)
                {
                    return false;
                }
                const glm::vec3 ndc = glm::vec3(clip) / clip.w;
                if (ndc.x < -1.2F || ndc.x > 1.2F || ndc.y < -1.2F || ndc.y > 1.2F)
                {
                    return false;
                }
                outScreen.x = viewport->Pos.x + (ndc.x * 0.5F + 0.5F) * viewport->Size.x;
                outScreen.y = viewport->Pos.y + (1.0F - (ndc.y * 0.5F + 0.5F)) * viewport->Size.y;
                return true;
            };

            for (const auto& actor : hudState.debugActors)
            {
                ImVec2 screen{};
                if (!projectWorld(actor.worldPosition, screen))
                {
                    continue;
                }

                ImU32 labelColor = actor.killer ? IM_COL32(255, 120, 120, 255) : IM_COL32(120, 255, 120, 255);
                const std::string line1 = actor.name + (actor.chasing ? " [CHASE]" : "");
                const std::string line2 = "HP:" + actor.healthState + " MOV:" + actor.movementState + " SPD:" + std::to_string(actor.speed);
                const std::string line3 = actor.killer ? ("ATK:" + actor.attackState) : "";
                drawList->AddText(ImVec2(screen.x - 84.0F, screen.y - 30.0F), labelColor, line1.c_str());
                drawList->AddText(ImVec2(screen.x - 84.0F, screen.y - 16.0F), IM_COL32(235, 235, 235, 255), line2.c_str());
                if (!line3.empty())
                {
                    drawList->AddText(ImVec2(screen.x - 84.0F, screen.y - 2.0F), IM_COL32(255, 210, 120, 255), line3.c_str());
                }

                glm::vec3 forward = actor.forward;
                if (glm::length(forward) < 1.0e-5F)
                {
                    forward = glm::vec3{0.0F, 0.0F, -1.0F};
                }
                forward = glm::normalize(forward);
                ImVec2 endScreen{};
                if (projectWorld(actor.worldPosition + glm::vec3{forward.x, 0.0F, forward.z} * 1.3F, endScreen))
                {
                    drawList->AddLine(screen, endScreen, IM_COL32(80, 180, 255, 230), 2.0F);
                }
            }
        }

        // Render perk debug info (top-left corner below runtime message) - always when debug mode is on
        if (context.gameplay != nullptr && !hudState.debugActors.empty())
        {
            const ImGuiViewport* viewport = ImGui::GetMainViewport();
            ImDrawList* drawList = ImGui::GetForegroundDrawList();

            // Render perk debug info (top-left corner below runtime message)
            const ImVec2 perkOrigin(viewport->Pos.x + 16.0F, viewport->Pos.y + 88.0F);
            const float lineHeight = 18.0F;
            float perkY = perkOrigin.y;
            
            const auto drawPerkSection = [&](const std::string& label, const std::vector<game::gameplay::HudState::ActivePerkDebug>& perks, float mod) {
                drawList->AddText(perkOrigin, IM_COL32(200, 200, 200, 255), (label + " (x" + std::to_string(mod).substr(0, 4) + ")").c_str());
                perkY += lineHeight;
                
                if (perks.empty())
                {
                    drawList->AddText(ImVec2(perkOrigin.x, perkY), IM_COL32(120, 120, 120, 255), "  [none]");
                    perkY += lineHeight;
                }
                else
                {
                    for (const auto& perk : perks)
                    {
                        const ImU32 color = perk.isActive ? IM_COL32(120, 255, 120, 255) : IM_COL32(180, 180, 180, 255);
                        const std::string status = perk.isActive ? "ACTIVE" : "PASSIVE";
                        std::string extra;
                        if (perk.isActive && perk.activeRemainingSeconds > 0.01F)
                        {
                            extra = " (" + std::to_string(perk.activeRemainingSeconds).substr(0, 3) + "s)";
                        }
                        if (!perk.isActive && perk.cooldownRemainingSeconds > 0.01F)
                        {
                            extra = " (CD " + std::to_string(perk.cooldownRemainingSeconds).substr(0, 3) + "s)";
                        }
                        drawList->AddText(ImVec2(perkOrigin.x, perkY), color, ("  " + perk.name + " [" + status + "]" + extra).c_str());
                        perkY += lineHeight;
                    }
                }
                perkY += 8.0F; // spacing between sections
            };
            
            drawPerkSection("SURVIVOR PERKS", hudState.activePerksSurvivor, hudState.speedModifierSurvivor);
            drawPerkSection("KILLER PERKS", hudState.activePerksKiller, hudState.speedModifierKiller);
        }

        }
    }

    if (m_impl->open)
    {
        if (!m_impl->firstOpenAnnouncementDone)
        {
            m_impl->AddLog("Type `help` to list commands.");
            m_impl->PrintHelp();
            m_impl->firstOpenAnnouncementDone = true;
        }

        ImGui::SetNextWindowSize(ImVec2(840.0F, 390.0F), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Developer Console", &m_impl->open))
        {
            if (ImGui::Button("Clear"))
            {
                m_impl->items.clear();
            }
            ImGui::SameLine();
            ImGui::Text("Examples: host 7777 | join 127.0.0.1 7777 | render_mode filled");

            ImGui::Separator();
            ImGui::BeginChild("ScrollingRegion", ImVec2(0, -ImGui::GetFrameHeightWithSpacing()), false, ImGuiWindowFlags_HorizontalScrollbar);
            for (const std::string& item : m_impl->items)
            {
                ImGui::TextUnformatted(item.c_str());
            }
            if (m_impl->scrollToBottom)
            {
                ImGui::SetScrollHereY(1.0F);
                m_impl->scrollToBottom = false;
            }
            ImGui::EndChild();

            if (m_impl->reclaimFocus)
            {
                ImGui::SetKeyboardFocusHere(-1);
                m_impl->reclaimFocus = false;
            }

            ImGuiInputTextFlags inputFlags = ImGuiInputTextFlags_EnterReturnsTrue |
                                             ImGuiInputTextFlags_CallbackCompletion |
                                             ImGuiInputTextFlags_CallbackHistory;
            if (ImGui::InputText(
                    "Input",
                    m_impl->inputBuffer.data(),
                    m_impl->inputBuffer.size(),
                    inputFlags,
                    &Impl::TextEditCallbackStub,
                    m_impl))
            {
                const std::string command = m_impl->inputBuffer.data();
                if (!command.empty())
                {
                    m_impl->ExecuteCommand(command, context);
                }
                m_impl->inputBuffer.fill('\0');
                m_impl->reclaimFocus = true;
            }

            const std::string currentInput = m_impl->inputBuffer.data();
            if (currentInput.empty())
            {
                ImGui::TextUnformatted("Hint: type a command, press TAB to autocomplete, ENTER to execute.");
            }
            else
            {
                const std::vector<Impl::CommandInfo> hints = m_impl->BuildHints(currentInput);
                if (!hints.empty())
                {
                    ImGui::Separator();
                    ImGui::TextUnformatted("Suggestions:");
                    const int maxHints = std::min<int>(8, static_cast<int>(hints.size()));
                    for (int i = 0; i < maxHints; ++i)
                    {
                        const auto& hint = hints[static_cast<std::size_t>(i)];
                        ImGui::Text("  [%s] %s - %s", hint.category.c_str(), hint.usage.c_str(), hint.description.c_str());
                    }
                }
            }

            if (m_impl->reclaimFocus)
            {
                ImGui::SetKeyboardFocusHere(-1);
                m_impl->reclaimFocus = false;
            }
        }
        ImGui::End();
    }

    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
#else
    (void)context;
    (void)fps;
    (void)hudState;
#endif
}

void DeveloperConsole::Toggle()
{
#if BUILD_WITH_IMGUI
    if (m_impl != nullptr)
    {
        const bool wasOpen = m_impl->open;
        m_impl->open = !m_impl->open;
        // Set focus only when opening, not when closing
        if (!wasOpen && m_impl->open)
        {
            m_impl->reclaimFocus = true;
        }
    }
#endif
}

bool DeveloperConsole::IsOpen() const
{
#if BUILD_WITH_IMGUI
    return m_impl != nullptr && m_impl->open;
#else
    return false;
#endif
}

bool DeveloperConsole::WantsKeyboardCapture() const
{
#if BUILD_WITH_IMGUI
    if (m_impl == nullptr)
    {
        return false;
    }
    return m_impl->open && ImGui::GetIO().WantCaptureKeyboard;
#else
    return false;
#endif
}
} // namespace ui
