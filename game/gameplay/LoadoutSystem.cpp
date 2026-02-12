#include "game/gameplay/LoadoutSystem.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>

#include <glm/common.hpp>
#include <nlohmann/json.hpp>

namespace game::gameplay::loadout
{
namespace
{
using json = nlohmann::json;

ModifierOp ModifierOpFromText(const std::string& value)
{
    if (value == "mul" || value == "multiply")
    {
        return ModifierOp::Multiply;
    }
    if (value == "set" || value == "override")
    {
        return ModifierOp::Override;
    }
    return ModifierOp::Add;
}

TargetKind TargetKindFromText(const std::string& value)
{
    if (value == "item")
    {
        return TargetKind::Item;
    }
    if (value == "power")
    {
        return TargetKind::Power;
    }
    return TargetKind::Any;
}

void SaveIfMissing(const std::filesystem::path& path, const json& payload)
{
    if (std::filesystem::exists(path))
    {
        return;
    }
    std::ofstream out(path);
    if (out.is_open())
    {
        out << payload.dump(2) << "\n";
    }
}
} // namespace

bool AddonDefinition::AppliesTo(TargetKind kind, const std::string& targetId) const
{
    if (appliesToKind != TargetKind::Any && appliesToKind != kind)
    {
        return false;
    }
    if (appliesToIds.empty())
    {
        return true;
    }
    return std::find(appliesToIds.begin(), appliesToIds.end(), targetId) != appliesToIds.end();
}

void AddonModifierContext::Clear()
{
    m_statModifiers.clear();
    m_hookModifiers.clear();
    m_activeAddonIds.clear();
}

void AddonModifierContext::Build(
    TargetKind targetKind,
    const std::string& targetId,
    const std::vector<std::string>& addonIds,
    const std::unordered_map<std::string, AddonDefinition>& addonDefs
)
{
    Clear();

    auto apply = [](AggregatedModifier& out, ModifierOp op, float value) {
        switch (op)
        {
            case ModifierOp::Add: out.add += value; break;
            case ModifierOp::Multiply: out.mul *= value; break;
            case ModifierOp::Override:
                out.hasOverride = true;
                out.overrideValue = value;
                break;
        }
    };

    for (const std::string& id : addonIds)
    {
        if (id.empty())
        {
            continue;
        }
        const auto it = addonDefs.find(id);
        if (it == addonDefs.end())
        {
            continue;
        }
        const AddonDefinition& addon = it->second;
        if (!addon.AppliesTo(targetKind, targetId))
        {
            continue;
        }

        m_activeAddonIds.push_back(addon.id);
        for (const StatModifier& stat : addon.statModifiers)
        {
            if (stat.stat.empty())
            {
                continue;
            }
            apply(m_statModifiers[stat.stat], stat.op, stat.value);
        }
        for (const HookModifier& hook : addon.hookModifiers)
        {
            if (hook.hook.empty() || hook.key.empty())
            {
                continue;
            }
            apply(m_hookModifiers[hook.hook + ":" + hook.key], hook.op, hook.value);
        }
    }
}

float AddonModifierContext::ApplyStat(const std::string& stat, float baseValue) const
{
    const auto it = m_statModifiers.find(stat);
    if (it == m_statModifiers.end())
    {
        return baseValue;
    }
    const AggregatedModifier& mod = it->second;
    float value = baseValue;
    if (mod.hasOverride)
    {
        value = mod.overrideValue;
    }
    value = (value + mod.add) * mod.mul;
    return value;
}

float AddonModifierContext::ApplyHook(const std::string& hook, const std::string& key, float baseValue) const
{
    const auto it = m_hookModifiers.find(hook + ":" + key);
    if (it == m_hookModifiers.end())
    {
        return baseValue;
    }
    const AggregatedModifier& mod = it->second;
    float value = baseValue;
    if (mod.hasOverride)
    {
        value = mod.overrideValue;
    }
    value = (value + mod.add) * mod.mul;
    return value;
}

bool GameplayCatalog::Initialize(const std::string& assetsRoot)
{
    m_assetsRoot = assetsRoot.empty() ? "assets" : assetsRoot;
    return Reload();
}

bool GameplayCatalog::Reload()
{
    m_items.clear();
    m_addons.clear();
    m_powers.clear();
    m_survivors.clear();
    m_killers.clear();

    if (!EnsureDefaultAssets())
    {
        return false;
    }
    return LoadItems() && LoadAddons() && LoadPowers() && LoadCharacters();
}

const ItemDefinition* GameplayCatalog::FindItem(const std::string& id) const
{
    const auto it = m_items.find(id);
    return it != m_items.end() ? &it->second : nullptr;
}

const AddonDefinition* GameplayCatalog::FindAddon(const std::string& id) const
{
    const auto it = m_addons.find(id);
    return it != m_addons.end() ? &it->second : nullptr;
}

const PowerDefinition* GameplayCatalog::FindPower(const std::string& id) const
{
    const auto it = m_powers.find(id);
    return it != m_powers.end() ? &it->second : nullptr;
}

const SurvivorCharacterDefinition* GameplayCatalog::FindSurvivor(const std::string& id) const
{
    const auto it = m_survivors.find(id);
    return it != m_survivors.end() ? &it->second : nullptr;
}

const KillerCharacterDefinition* GameplayCatalog::FindKiller(const std::string& id) const
{
    const auto it = m_killers.find(id);
    return it != m_killers.end() ? &it->second : nullptr;
}

std::vector<std::string> GameplayCatalog::ListSurvivorIds() const
{
    std::vector<std::string> out;
    out.reserve(m_survivors.size());
    for (const auto& [id, _] : m_survivors)
    {
        out.push_back(id);
    }
    std::sort(out.begin(), out.end());
    return out;
}

std::vector<std::string> GameplayCatalog::ListKillerIds() const
{
    std::vector<std::string> out;
    out.reserve(m_killers.size());
    for (const auto& [id, _] : m_killers)
    {
        out.push_back(id);
    }
    std::sort(out.begin(), out.end());
    return out;
}

std::vector<std::string> GameplayCatalog::ListItemIds() const
{
    std::vector<std::string> out;
    out.reserve(m_items.size());
    for (const auto& [id, _] : m_items)
    {
        out.push_back(id);
    }
    std::sort(out.begin(), out.end());
    return out;
}

std::vector<std::string> GameplayCatalog::ListPowerIds() const
{
    std::vector<std::string> out;
    out.reserve(m_powers.size());
    for (const auto& [id, _] : m_powers)
    {
        out.push_back(id);
    }
    std::sort(out.begin(), out.end());
    return out;
}

std::vector<std::string> GameplayCatalog::ListAddonIdsForTarget(TargetKind kind, const std::string& targetId) const
{
    std::vector<std::string> out;
    for (const auto& [id, addon] : m_addons)
    {
        if (addon.AppliesTo(kind, targetId))
        {
            out.push_back(id);
        }
    }
    std::sort(out.begin(), out.end());
    return out;
}

bool GameplayCatalog::EnsureDefaultAssets() const
{
    const std::filesystem::path root(m_assetsRoot);
    const std::filesystem::path itemsDir = root / "items";
    const std::filesystem::path addonsDir = root / "addons";
    const std::filesystem::path powersDir = root / "powers";
    const std::filesystem::path survivorsDir = root / "characters" / "survivors";
    const std::filesystem::path killersDir = root / "characters" / "killers";

    std::error_code ec;
    std::filesystem::create_directories(itemsDir, ec);
    std::filesystem::create_directories(addonsDir, ec);
    std::filesystem::create_directories(powersDir, ec);
    std::filesystem::create_directories(survivorsDir, ec);
    std::filesystem::create_directories(killersDir, ec);

    SaveIfMissing(
        itemsDir / "medkit.json",
        json{{"asset_version", 1},
             {"id", "medkit"},
             {"display_name", "Medkit"},
             {"description", "Self-heal item with charges."},
             {"max_charges", 24.0F},
             {"use_mode", "hold"},
             {"params", json{{"heal_per_second", 0.12F}, {"charge_per_second", 1.0F}}}}
    );
    SaveIfMissing(
        itemsDir / "toolbox.json",
        json{{"asset_version", 1},
             {"id", "toolbox"},
             {"display_name", "Toolbox"},
             {"description", "Repairs generators faster while charges remain."},
             {"max_charges", 24.0F},
             {"use_mode", "hold"},
             {"params", json{{"repair_speed_bonus", 0.12F}, {"charge_per_second", 1.15F}}}}
    );
    SaveIfMissing(
        itemsDir / "flashlight.json",
        json{{"asset_version", 1},
             {"id", "flashlight"},
             {"display_name", "Flashlight"},
             {"description", "Beam can blind/stun killer after exposure."},
             {"max_charges", 20.0F},
             {"use_mode", "hold"},
             {"params",
              json{{"beam_range", 9.5F}, {"beam_angle_deg", 22.0F}, {"blind_time_required", 1.2F}, {"charge_per_second", 1.0F}}}}
    );
    SaveIfMissing(
        itemsDir / "map.json",
        json{{"asset_version", 1},
             {"id", "map"},
             {"display_name", "Map"},
             {"description", "Reveals key interactables/traps around survivor."},
             {"max_charges", 10.0F},
             {"use_mode", "press"},
             {"params", json{{"reveal_radius", 16.0F}, {"charge_per_use", 1.0F}}}}
    );

    SaveIfMissing(
        powersDir / "bear_trap.json",
        json{{"asset_version", 1},
             {"id", "bear_trap"},
             {"display_name", "Bear Trap"},
             {"description", "Place trap that captures survivor and forces escape attempts."},
             {"params",
              json{{"max_active_traps", 8.0F},
                   {"trap_half_x", 0.36F},
                   {"trap_half_y", 0.08F},
                   {"trap_half_z", 0.36F},
                   {"base_escape_chance", 0.22F},
                   {"escape_chance_step", 0.14F},
                   {"max_escape_attempts", 6.0F}}}}
    );

    SaveIfMissing(
        addonsDir / "bandage_roll.json",
        json{{"asset_version", 1},
             {"id", "bandage_roll"},
             {"display_name", "Bandage Roll"},
             {"description", "Medkit add-on: more charges."},
             {"applies_to", json{{"kind", "item"}, {"ids", json::array({"medkit"})}}},
             {"modifiers", json::array({json{{"stat", "max_charges"}, {"op", "add"}, {"value", 8.0F}}})},
             {"hooks", json::array()}}
    );
    SaveIfMissing(
        addonsDir / "surgical_tape.json",
        json{{"asset_version", 1},
             {"id", "surgical_tape"},
             {"display_name", "Surgical Tape"},
             {"description", "Medkit add-on: faster heal speed."},
             {"applies_to", json{{"kind", "item"}, {"ids", json::array({"medkit"})}}},
             {"modifiers", json::array({json{{"stat", "heal_per_second"}, {"op", "mul"}, {"value", 1.2F}}})},
             {"hooks", json::array()}}
    );
    SaveIfMissing(
        addonsDir / "wire_spool.json",
        json{{"asset_version", 1},
             {"id", "wire_spool"},
             {"display_name", "Wire Spool"},
             {"description", "Toolbox add-on: repair speed bonus."},
             {"applies_to", json{{"kind", "item"}, {"ids", json::array({"toolbox"})}}},
             {"modifiers", json::array({json{{"stat", "repair_speed_bonus"}, {"op", "mul"}, {"value", 1.25F}}})},
             {"hooks", json::array()}}
    );
    SaveIfMissing(
        addonsDir / "high_capacity_cell.json",
        json{{"asset_version", 1},
             {"id", "high_capacity_cell"},
             {"display_name", "High Capacity Cell"},
             {"description", "Flashlight add-on: additional battery charges."},
             {"applies_to", json{{"kind", "item"}, {"ids", json::array({"flashlight"})}}},
             {"modifiers", json::array({json{{"stat", "max_charges"}, {"op", "add"}, {"value", 10.0F}}})},
             {"hooks", json::array()}}
    );
    SaveIfMissing(
        addonsDir / "wide_lens.json",
        json{{"asset_version", 1},
             {"id", "wide_lens"},
             {"display_name", "Wide Lens"},
             {"description", "Flashlight add-on: wider cone, slightly shorter range."},
             {"applies_to", json{{"kind", "item"}, {"ids", json::array({"flashlight"})}}},
             {"modifiers",
              json::array({json{{"stat", "beam_angle_deg"}, {"op", "add"}, {"value", 6.0F}},
                           json{{"stat", "beam_range"}, {"op", "mul"}, {"value", 0.92F}}})},
             {"hooks", json::array()}}
    );

    SaveIfMissing(
        addonsDir / "serrated_jaws.json",
        json{{"asset_version", 1},
             {"id", "serrated_jaws"},
             {"display_name", "Serrated Jaws"},
             {"description", "Bear trap add-on: escaped survivor receives stronger bleed feedback."},
             {"applies_to", json{{"kind", "power"}, {"ids", json::array({"bear_trap"})}}},
             {"modifiers", json::array()},
             {"hooks",
              json::array({json{{"hook", "trap_escape"}, {"key", "bleed_multiplier"}, {"op", "mul"}, {"value", 1.4F}}})}}
    );
    SaveIfMissing(
        addonsDir / "tighter_springs.json",
        json{{"asset_version", 1},
             {"id", "tighter_springs"},
             {"display_name", "Tighter Springs"},
             {"description", "Bear trap add-on: escape chance increases slower."},
             {"applies_to", json{{"kind", "power"}, {"ids", json::array({"bear_trap"})}}},
             {"modifiers",
              json::array({json{{"stat", "escape_chance_step"}, {"op", "mul"}, {"value", 0.78F}},
                           json{{"stat", "max_escape_attempts"}, {"op", "add"}, {"value", 2.0F}}})},
             {"hooks", json::array()}}
    );

    SaveIfMissing(
        survivorsDir / "survivor_dwight.json",
        json{{"asset_version", 1},
             {"id", "survivor_dwight"},
             {"display_name", "Dwight"},
             {"model_path", "assets/meshes/survivor_dwight.glb"},
             {"cosmetic_id", "default"}}
    );
    SaveIfMissing(
        survivorsDir / "survivor_meg.json",
        json{{"asset_version", 1},
             {"id", "survivor_meg"},
             {"display_name", "Meg"},
             {"model_path", "assets/meshes/survivor_meg.glb"},
             {"cosmetic_id", "default"}}
    );
    SaveIfMissing(
        killersDir / "killer_trapper.json",
        json{{"asset_version", 1},
             {"id", "killer_trapper"},
             {"display_name", "Trapper"},
             {"model_path", "assets/meshes/killer_trapper.glb"},
             {"cosmetic_id", "default"},
             {"power_id", "bear_trap"}}
    );
    SaveIfMissing(
        killersDir / "killer_wraith.json",
        json{{"asset_version", 1},
             {"id", "killer_wraith"},
             {"display_name", "Wraith"},
             {"model_path", "assets/meshes/killer_wraith.glb"},
             {"cosmetic_id", "default"},
             {"power_id", "bear_trap"}}
    );

    return true;
}

bool GameplayCatalog::LoadItems()
{
    const std::filesystem::path dir = std::filesystem::path(m_assetsRoot) / "items";
    for (const auto& entry : std::filesystem::directory_iterator(dir))
    {
        if (!entry.is_regular_file() || entry.path().extension() != ".json")
        {
            continue;
        }
        std::ifstream in(entry.path());
        if (!in.is_open())
        {
            continue;
        }
        json root;
        try
        {
            in >> root;
        }
        catch (const std::exception&)
        {
            continue;
        }
        ItemDefinition item;
        item.assetVersion = root.value("asset_version", 1);
        item.id = root.value("id", std::string{});
        item.displayName = root.value("display_name", item.id);
        item.description = root.value("description", std::string{});
        item.maxCharges = root.value("max_charges", 0.0F);
        item.useMode = root.value("use_mode", std::string{"hold"});
        if (root.contains("params") && root["params"].is_object())
        {
            for (auto it = root["params"].begin(); it != root["params"].end(); ++it)
            {
                if (it.value().is_number())
                {
                    item.params[it.key()] = it.value().get<float>();
                }
            }
        }
        if (!item.id.empty())
        {
            m_items[item.id] = item;
        }
    }
    return !m_items.empty();
}

bool GameplayCatalog::LoadAddons()
{
    const std::filesystem::path dir = std::filesystem::path(m_assetsRoot) / "addons";
    for (const auto& entry : std::filesystem::directory_iterator(dir))
    {
        if (!entry.is_regular_file() || entry.path().extension() != ".json")
        {
            continue;
        }
        std::ifstream in(entry.path());
        if (!in.is_open())
        {
            continue;
        }
        json root;
        try
        {
            in >> root;
        }
        catch (const std::exception&)
        {
            continue;
        }
        AddonDefinition addon;
        addon.assetVersion = root.value("asset_version", 1);
        addon.id = root.value("id", std::string{});
        addon.displayName = root.value("display_name", addon.id);
        addon.description = root.value("description", std::string{});
        if (root.contains("applies_to") && root["applies_to"].is_object())
        {
            addon.appliesToKind = TargetKindFromText(root["applies_to"].value("kind", std::string{"any"}));
            if (root["applies_to"].contains("ids") && root["applies_to"]["ids"].is_array())
            {
                for (const auto& id : root["applies_to"]["ids"])
                {
                    if (id.is_string())
                    {
                        addon.appliesToIds.push_back(id.get<std::string>());
                    }
                }
            }
        }
        if (root.contains("modifiers") && root["modifiers"].is_array())
        {
            for (const auto& m : root["modifiers"])
            {
                if (!m.is_object())
                {
                    continue;
                }
                StatModifier mod;
                mod.stat = m.value("stat", std::string{});
                mod.op = ModifierOpFromText(m.value("op", std::string{"add"}));
                mod.value = m.value("value", 0.0F);
                if (!mod.stat.empty())
                {
                    addon.statModifiers.push_back(mod);
                }
            }
        }
        if (root.contains("hooks") && root["hooks"].is_array())
        {
            for (const auto& h : root["hooks"])
            {
                if (!h.is_object())
                {
                    continue;
                }
                HookModifier hook;
                hook.hook = h.value("hook", std::string{});
                hook.key = h.value("key", std::string{});
                hook.op = ModifierOpFromText(h.value("op", std::string{"add"}));
                hook.value = h.value("value", 0.0F);
                if (!hook.hook.empty() && !hook.key.empty())
                {
                    addon.hookModifiers.push_back(hook);
                }
            }
        }
        if (!addon.id.empty())
        {
            m_addons[addon.id] = addon;
        }
    }
    return true;
}

bool GameplayCatalog::LoadPowers()
{
    const std::filesystem::path dir = std::filesystem::path(m_assetsRoot) / "powers";
    for (const auto& entry : std::filesystem::directory_iterator(dir))
    {
        if (!entry.is_regular_file() || entry.path().extension() != ".json")
        {
            continue;
        }
        std::ifstream in(entry.path());
        if (!in.is_open())
        {
            continue;
        }
        json root;
        try
        {
            in >> root;
        }
        catch (const std::exception&)
        {
            continue;
        }
        PowerDefinition power;
        power.assetVersion = root.value("asset_version", 1);
        power.id = root.value("id", std::string{});
        power.displayName = root.value("display_name", power.id);
        power.description = root.value("description", std::string{});
        if (root.contains("params") && root["params"].is_object())
        {
            for (auto it = root["params"].begin(); it != root["params"].end(); ++it)
            {
                if (it.value().is_number())
                {
                    power.params[it.key()] = it.value().get<float>();
                }
            }
        }
        if (!power.id.empty())
        {
            m_powers[power.id] = power;
        }
    }
    return !m_powers.empty();
}

bool GameplayCatalog::LoadCharacters()
{
    const std::filesystem::path survivorsDir = std::filesystem::path(m_assetsRoot) / "characters" / "survivors";
    const std::filesystem::path killersDir = std::filesystem::path(m_assetsRoot) / "characters" / "killers";

    for (const auto& entry : std::filesystem::directory_iterator(survivorsDir))
    {
        if (!entry.is_regular_file() || entry.path().extension() != ".json")
        {
            continue;
        }
        std::ifstream in(entry.path());
        if (!in.is_open())
        {
            continue;
        }
        json root;
        try
        {
            in >> root;
        }
        catch (const std::exception&)
        {
            continue;
        }
        SurvivorCharacterDefinition s;
        s.assetVersion = root.value("asset_version", 1);
        s.id = root.value("id", std::string{});
        s.displayName = root.value("display_name", s.id);
        s.modelPath = root.value("model_path", std::string{});
        s.cosmeticId = root.value("cosmetic_id", std::string{});
        if (!s.id.empty())
        {
            m_survivors[s.id] = s;
        }
    }

    for (const auto& entry : std::filesystem::directory_iterator(killersDir))
    {
        if (!entry.is_regular_file() || entry.path().extension() != ".json")
        {
            continue;
        }
        std::ifstream in(entry.path());
        if (!in.is_open())
        {
            continue;
        }
        json root;
        try
        {
            in >> root;
        }
        catch (const std::exception&)
        {
            continue;
        }
        KillerCharacterDefinition k;
        k.assetVersion = root.value("asset_version", 1);
        k.id = root.value("id", std::string{});
        k.displayName = root.value("display_name", k.id);
        k.modelPath = root.value("model_path", std::string{});
        k.cosmeticId = root.value("cosmetic_id", std::string{});
        k.powerId = root.value("power_id", std::string{});
        if (!k.id.empty())
        {
            m_killers[k.id] = k;
        }
    }

    return !m_survivors.empty() && !m_killers.empty();
}
} // namespace game::gameplay::loadout
