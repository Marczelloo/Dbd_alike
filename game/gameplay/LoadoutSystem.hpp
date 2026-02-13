#pragma once

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace game::gameplay::loadout
{
enum class ModifierOp
{
    Add,
    Multiply,
    Override
};

enum class TargetKind
{
    Any,
    Item,
    Power
};

struct StatModifier
{
    std::string stat;
    ModifierOp op = ModifierOp::Add;
    float value = 0.0F;
};

struct HookModifier
{
    std::string hook;
    std::string key;
    ModifierOp op = ModifierOp::Add;
    float value = 0.0F;
};

struct AddonDefinition
{
    int assetVersion = 1;
    std::string id;
    std::string displayName;
    std::string description;
    TargetKind appliesToKind = TargetKind::Any;
    std::vector<std::string> appliesToIds;
    std::vector<StatModifier> statModifiers;
    std::vector<HookModifier> hookModifiers;

    [[nodiscard]] bool AppliesTo(TargetKind kind, const std::string& targetId) const;
};

struct ItemDefinition
{
    int assetVersion = 1;
    std::string id;
    std::string displayName;
    std::string description;
    std::string meshPath;
    float maxCharges = 0.0F;
    std::string useMode = "hold";
    std::unordered_map<std::string, float> params;
};

struct PowerDefinition
{
    int assetVersion = 1;
    std::string id;
    std::string displayName;
    std::string description;
    std::unordered_map<std::string, float> params;
};

struct SurvivorCharacterDefinition
{
    int assetVersion = 1;
    std::string id;
    std::string displayName;
    std::string modelPath;
    std::string cosmeticId;
};

struct KillerCharacterDefinition
{
    int assetVersion = 1;
    std::string id;
    std::string displayName;
    std::string modelPath;
    std::string cosmeticId;
    std::string powerId;
};

struct LoadoutSurvivor
{
    std::string itemId;
    std::string addonAId;
    std::string addonBId;
};

struct LoadoutKiller
{
    std::string powerId;
    std::string addonAId;
    std::string addonBId;
};

struct ItemRuntimeState
{
    float charges = 0.0F;
    bool active = false;
    float cooldown = 0.0F;
};

struct PowerRuntimeState
{
    bool active = false;
    float cooldown = 0.0F;
};

struct BehaviourContext
{
    float fixedDeltaSeconds = 0.0F;
    bool serverAuthoritative = true;
};

class IAddonModifier
{
public:
    virtual ~IAddonModifier() = default;
    virtual void ApplyStatModifiers(std::unordered_map<std::string, float>& ioStats) const = 0;
    virtual void ApplyHookModifiers(std::unordered_map<std::string, float>& ioHooks) const = 0;
};

class IItemBehaviour
{
public:
    virtual ~IItemBehaviour() = default;
    [[nodiscard]] virtual std::string Id() const = 0;
    virtual void OnUseStart(ItemRuntimeState& state, const BehaviourContext& context) = 0;
    virtual void OnUseTick(ItemRuntimeState& state, const BehaviourContext& context) = 0;
    virtual void OnUseStop(ItemRuntimeState& state, const BehaviourContext& context) = 0;
};

class IPowerBehaviour
{
public:
    virtual ~IPowerBehaviour() = default;
    [[nodiscard]] virtual std::string Id() const = 0;
    virtual void OnPowerStart(PowerRuntimeState& state, const BehaviourContext& context) = 0;
    virtual void OnPowerTick(PowerRuntimeState& state, const BehaviourContext& context) = 0;
    virtual void OnPowerStop(PowerRuntimeState& state, const BehaviourContext& context) = 0;
};

class AddonModifierContext
{
public:
    void Clear();
    void Build(
        TargetKind targetKind,
        const std::string& targetId,
        const std::vector<std::string>& addonIds,
        const std::unordered_map<std::string, AddonDefinition>& addonDefs
    );

    [[nodiscard]] float ApplyStat(const std::string& stat, float baseValue) const;
    [[nodiscard]] float ApplyHook(const std::string& hook, const std::string& key, float baseValue) const;
    [[nodiscard]] std::vector<std::string> ActiveAddonIds() const { return m_activeAddonIds; }

private:
    struct AggregatedModifier
    {
        float add = 0.0F;
        float mul = 1.0F;
        bool hasOverride = false;
        float overrideValue = 0.0F;
    };

    std::unordered_map<std::string, AggregatedModifier> m_statModifiers;
    std::unordered_map<std::string, AggregatedModifier> m_hookModifiers;
    std::vector<std::string> m_activeAddonIds;
};

class GameplayCatalog
{
public:
    bool Initialize(const std::string& assetsRoot = "assets");
    bool Reload();

    [[nodiscard]] const std::unordered_map<std::string, ItemDefinition>& Items() const { return m_items; }
    [[nodiscard]] const std::unordered_map<std::string, AddonDefinition>& Addons() const { return m_addons; }
    [[nodiscard]] const std::unordered_map<std::string, PowerDefinition>& Powers() const { return m_powers; }
    [[nodiscard]] const std::unordered_map<std::string, SurvivorCharacterDefinition>& Survivors() const { return m_survivors; }
    [[nodiscard]] const std::unordered_map<std::string, KillerCharacterDefinition>& Killers() const { return m_killers; }

    [[nodiscard]] const ItemDefinition* FindItem(const std::string& id) const;
    [[nodiscard]] const AddonDefinition* FindAddon(const std::string& id) const;
    [[nodiscard]] const PowerDefinition* FindPower(const std::string& id) const;
    [[nodiscard]] const SurvivorCharacterDefinition* FindSurvivor(const std::string& id) const;
    [[nodiscard]] const KillerCharacterDefinition* FindKiller(const std::string& id) const;

    [[nodiscard]] std::vector<std::string> ListSurvivorIds() const;
    [[nodiscard]] std::vector<std::string> ListKillerIds() const;
    [[nodiscard]] std::vector<std::string> ListItemIds() const;
    [[nodiscard]] std::vector<std::string> ListPowerIds() const;
    [[nodiscard]] std::vector<std::string> ListAddonIdsForTarget(TargetKind kind, const std::string& targetId) const;

private:
    bool EnsureDefaultAssets() const;
    bool LoadItems();
    bool LoadAddons();
    bool LoadPowers();
    bool LoadCharacters();

    std::string m_assetsRoot = "assets";
    std::unordered_map<std::string, ItemDefinition> m_items;
    std::unordered_map<std::string, AddonDefinition> m_addons;
    std::unordered_map<std::string, PowerDefinition> m_powers;
    std::unordered_map<std::string, SurvivorCharacterDefinition> m_survivors;
    std::unordered_map<std::string, KillerCharacterDefinition> m_killers;
};
} // namespace game::gameplay::loadout
