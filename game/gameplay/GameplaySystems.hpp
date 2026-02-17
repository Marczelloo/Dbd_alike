#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <random>
#include <string>
#include <unordered_map>
#include <vector>

#include <glm/mat4x4.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include "engine/animation/AnimationSystem.hpp"
#include "engine/core/EventBus.hpp"
#include "engine/fx/FxSystem.hpp"
#include "engine/platform/ActionBindings.hpp"
#include "engine/physics/PhysicsWorld.hpp"
#include "engine/render/Frustum.hpp"
#include "engine/render/Renderer.hpp"
#include "engine/render/StaticBatcher.hpp"
#include "engine/scene/World.hpp"
#include "game/gameplay/LoadoutSystem.hpp"
#include "game/gameplay/PerkSystem.hpp"
#include "game/gameplay/StatusEffectManager.hpp"
#include "game/maps/TileGenerator.hpp"

namespace engine::scene
{
enum class Role;
}

namespace engine::platform
{
class Input;
}

namespace engine::render
{
class Renderer;
}

namespace engine::assets
{
class MeshLibrary;
}

namespace game::gameplay::perks
{
struct PerkLoadout;
class PerkSystem;
struct PerkEffect;
}

// Forward declarations
struct RoleCommand;

namespace game::gameplay
{
// Phase B4: Killer Look Light configuration
struct KillerLookLight
{
    bool enabled = true;
    float intensity = 1.1F;
    float range = 14.0F;
    float innerAngleDegrees = 16.0F;
    float outerAngleDegrees = 28.0F;
    float pitchDegrees = 35.0F;
    glm::vec3 color{1.0F, 0.15F, 0.1F};
};

// HUD State - defined outside class for access from DeveloperConsole
struct HudState
{
    struct DebugActorLabel
    {
        std::string name = "Player";
        std::string healthState = "Healthy";
        std::string movementState = "Walking";
        std::string attackState = "Idle";
        glm::vec3 worldPosition{0.0F};
        glm::vec3 forward{0.0F, 0.0F, -1.0F};
        float speed = 0.0F;
        bool chasing = false;
        bool killer = false;
    };

    std::string mapName = "test";
    std::string roleName = "Survivor";
    bool isInGame = false;
    std::string cameraModeName = "3rd Person";
    std::string renderModeName = "wireframe";
    std::string interactionPrompt;
    std::string interactionTypeName = "None";
    std::string interactionTargetName = "None";
    int interactionPriority = 0;
    std::string vaultTypeName = "None";
    std::string survivorStateName = "Healthy";
    std::vector<std::string> survivorStates;
    int generatorsCompleted = 0;
    int generatorsTotal = 5;
    bool repairingGenerator = false;
    float activeGeneratorProgress = 0.0F;
    bool selfHealing = false;
    float selfHealProgress = 0.0F;
    bool skillCheckActive = false;
    float skillCheckNeedle = 0.0F;
    float skillCheckSuccessStart = 0.0F;
    float skillCheckSuccessEnd = 0.0F;
    float carryEscapeProgress = 0.0F;
    int hookStage = 0;
    float hookStageProgress = 0.0F;
    int hookEscapeAttemptsUsed = 0;
    int hookEscapeAttemptsMax = 3;
    float hookEscapeChance = 0.04F;
    bool hookCanAttemptEscape = false;
    bool hookSkillChecksEnabled = false;
    std::string runtimeMessage;
    std::string movementStateName = "Walking";
    float survivorVisualYawDeg = 0.0F;
    float survivorVisualTargetYawDeg = 0.0F;
    float survivorLookYawDeg = 0.0F;
    float survivorCameraYawDeg = 0.0F;
    glm::vec2 survivorMoveInput{0.0F};
    std::string killerAttackStateName = "Idle";
    float lungeCharge01 = 0.0F;
    std::string attackHint = "LMB click short / hold LMB lunge";
    bool terrorRadiusVisible = true;
    float terrorRadiusMeters = 24.0F;
    int activeLoopTileId = -1;
    std::string activeLoopArchetype = "N/A";

    bool chaseActive = false;
    float chaseDistance = 0.0F;
    bool lineOfSight = false;
    bool inCenterFOV = false;      // Survivor in killer's center FOV
    bool survivorSprinting = false; // Survivor is sprinting
    float timeInChase = 0.0F;     // Total time chase has been active
    float timeSinceLOS = 0.0F;     // Time since killer had LOS
    float timeSinceCenterFOV = 0.0F; // Time since survivor was in center FOV

    // Bloodlust state
    int bloodlustTier = 0;          // Current bloodlust tier (0-3)
    float bloodlustSpeedMultiplier = 1.0F; // Speed multiplier from bloodlust
    float killerBaseSpeed = 0.0F;    // Killer's base movement speed
    float killerCurrentSpeed = 0.0F;  // Killer's current speed after all multipliers

    // Phase B2/B3: Scratch marks and blood pools debug info
    int scratchActiveCount = 0;
    int bloodActiveCount = 0;
    float scratchSpawnInterval = 0.0F;

    // Phase B4: Killer look light debug info
    bool killerLightEnabled = true;
    float killerLightRange = 14.0F;
    float killerLightIntensity = 1.1F;
    float killerLightInnerAngle = 16.0F;
    float killerLightOuterAngle = 28.0F;
    float killerLightPitch = 10.0F;  // Pitch downward (positive = down)

    bool collisionEnabled = true;
    bool debugDrawEnabled = true;
    bool physicsDebugEnabled = false;
    bool noclipEnabled = false;
    bool killerSurvivorNoCollisionActive = false;
    float killerSurvivorNoCollisionTimer = 0.0F;
    bool killerSurvivorOverlapping = false;

    float playerSpeed = 0.0F;
    bool grounded = false;
    glm::vec3 velocity{0.0F};
    glm::vec3 lastCollisionNormal{0.0F, 1.0F, 0.0F};
    float penetrationDepth = 0.0F;
    std::vector<DebugActorLabel> debugActors;
    int fxActiveInstances = 0;
    int fxActiveParticles = 0;
    float fxCpuMs = 0.0F;

    std::string survivorCharacterId = "survivor_dwight";
    std::string killerCharacterId = "killer_trapper";
    std::string survivorItemId = "none";
    std::string survivorItemAddonA = "none";
    std::string survivorItemAddonB = "none";
    float survivorItemCharges = 0.0F;
    float survivorItemMaxCharges = 0.0F;
    float survivorItemCharge01 = 0.0F;
    float survivorItemUseProgress01 = 0.0F;
    bool survivorItemActive = false;
    bool survivorFlashlightAiming = false;
    float survivorFlashlightBlindBuild01 = 0.0F;
    int survivorItemUsesRemaining = 0;
    std::string killerPowerId = "none";
    std::string killerPowerAddonA = "none";
    std::string killerPowerAddonB = "none";
    int activeTrapCount = 0;
    int carriedTrapCount = 0;
    float trapSetProgress01 = 0.0F;
    bool wraithCloaked = false;
    bool wraithCloakTransitionActive = false;
    float wraithCloakProgress01 = 0.0F;
    float wraithCloakAmount = 0.0F;
    std::string wraithCloakAction;
    float wraithPostUncloakHasteSeconds = 0.0F;
    
    // Killer render info for cloak shader
    glm::vec3 killerWorldPosition{0.0F};
    float killerCapsuleHeight = 2.0F;
    float killerCapsuleRadius = 0.4F;
    
    bool trapDebugEnabled = false;
    int trappedEscapeAttempts = 0;
    float trappedEscapeChance = 0.0F;
    float killerBlindRemaining = 0.0F;
    bool killerBlindWhiteStyle = true;
    float killerStunRemaining = 0.0F;
    std::string trapIndicatorText;
    float trapIndicatorTtl = 0.0F;
    bool trapIndicatorDanger = true;

    // Hatchet power HUD fields
    int hatchetCount = 7;
    int hatchetMaxCount = 7;
    bool hatchetCharging = false;
    float hatchetCharge01 = 0.0F;
    bool hatchetDebugEnabled = false;
    int activeProjectileCount = 0;
    float lockerReplenishProgress = 0.0F;
    bool lockerInRange = false;

    // Chainsaw sprint power HUD fields
    std::string chainsawState = "Idle";
    float chainsawCharge01 = 0.0F;
    float chainsawOverheat01 = 0.0F;
    float chainsawSprintTimer = 0.0F;
    float chainsawSprintMaxDuration = 5.0F;
    float chainsawCurrentSpeed = 0.0F;
    bool chainsawDebugEnabled = false;

    // New chainsaw HUD fields
    bool chainsawTurnBoostActive = false;     // True during first 0.5s
    float chainsawRecoveryTimer = 0.0F;       // Time remaining in recovery
    float chainsawRecoveryDuration = 0.0F;    // Total recovery duration
    bool chainsawOverheatBuffed = false;      // True when heat >= 100%
    float chainsawTurnRate = 0.0F;            // Current turn rate (deg/sec)

    // Nurse blink power HUD fields
    std::string blinkState = "Idle";
    int blinkCharges = 2;
    int blinkMaxCharges = 2;
    float blinkCharge01 = 0.0F;
    float blinkChainWindow01 = 0.0F;
    float blinkFatigue01 = 0.0F;
    int blinksUsedThisChain = 0;
    bool blinkDebugEnabled = false;
    float blinkChargeRegen01 = 0.0F;
    float blinkDistanceMeters = 0.0F;
    float blinkFatigueDuration = 0.0F;

    // Perks debug info
    struct ActivePerkDebug
    {
        std::string id;
        std::string name;
        bool isActive = false;
        float activeRemainingSeconds = 0.0F;
        float cooldownRemainingSeconds = 0.0F;
        int stacks = 0;
        int tier = 1;
        float maxCooldownSeconds = 0.0F;
    };
    std::vector<ActivePerkDebug> activePerksSurvivor;
    std::vector<ActivePerkDebug> activePerksKiller;
    float speedModifierSurvivor = 1.0F;
    float speedModifierKiller = 1.0F;

    // Perk HUD slots (for graphical display)
    static constexpr std::size_t kPerkSlotCount = 4;
    std::array<ActivePerkDebug, kPerkSlotCount> survivorPerkSlots{};
    std::array<ActivePerkDebug, kPerkSlotCount> killerPerkSlots{};

    // Status Effects HUD
    struct ActiveStatusEffect
    {
        std::string typeId;              // "haste", "exposed", etc.
        std::string displayName;         // "Haste", "Exposed"
        float remainingSeconds = 0.0F;
        float progress01 = 0.0F;         // For progress bar
        float strength = 0.0F;
        int stacks = 1;
        bool isInfinite = false;
    };
    std::vector<ActiveStatusEffect> killerStatusEffects;
    std::vector<ActiveStatusEffect> survivorStatusEffects;

    // Quick access flags for common status effect queries
    bool killerUndetectable = false;
    bool survivorExposed = false;
    bool survivorExhausted = false;

    // Animation/Locomotion debug info
    std::string animState = "Idle";
    std::string animClip = "";
    float animPlaybackSpeed = 1.0F;
    float animBlendWeight = 1.0F;
    bool animBlending = false;
    bool animAutoMode = true;
    std::vector<std::string> animClipList;
};

class GameplaySystems
{
public:
    enum class SpawnPointType
    {
        Survivor,
        Killer,
        Generic
    };

    struct SpawnPointInfo
    {
        int id = -1;
        SpawnPointType type = SpawnPointType::Generic;
        glm::vec3 position{0.0F};
    };

    struct GameplayTuning
    {
        int assetVersion = 1;

        float survivorWalkSpeed = 2.85F;
        float survivorSprintSpeed = 4.6F;
        float survivorCrouchSpeed = 1.2F;
        float survivorCrawlSpeed = 0.82F;
        float killerMoveSpeed = 4.6F * 1.15F;

        float survivorCapsuleRadius = 0.45F;
        float survivorCapsuleHeight = 1.8F;
        float killerCapsuleRadius = 0.45F;
        float killerCapsuleHeight = 1.8F;

        float terrorRadiusMeters = 24.0F;
        float terrorRadiusChaseMeters = 32.0F;

        float vaultSlowTime = 0.9F;
        float vaultMediumTime = 0.6F;
        float vaultFastTime = 0.4F;
        float fastVaultDotThreshold = 0.85F;
        float fastVaultSpeedMultiplier = 0.88F;
        float fastVaultMinRunup = 1.1F;

        float shortAttackRange = 2.6F;
        float shortAttackAngleDegrees = 60.0F;
        float lungeHoldMinSeconds = 0.08F;
        float lungeDurationSeconds = 0.62F;
        float lungeRecoverSeconds = 0.58F;

        // Chase FOV configuration (degrees)
        float chaseFovDegrees = 87.0F;      // DBD-like: total FOV for chase detection
        float chaseCenterFovDegrees = 35.0F; // DBD-like: center FOV for chase gating (±35° from forward)
        float shortRecoverSeconds = 0.52F;
        float missRecoverSeconds = 0.45F;
        float lungeSpeedStart = 7.0F;
        float lungeSpeedEnd = 11.5F;

        float healDurationSeconds = 12.5F;
        float skillCheckMinInterval = 2.5F;
        float skillCheckMaxInterval = 6.5F;
        float generatorRepairSecondsBase = 90.0F;

        float medkitFullHealCharges = 16.0F;
        float medkitHealSpeedMultiplier = 1.5F;
        float toolboxCharges = 15.0F;
        float toolboxChargeDrainPerSecond = 1.0F;
        float toolboxRepairSpeedBonus = 0.9F;

        float flashlightMaxUseSeconds = 8.0F;
        float flashlightBlindBuildSeconds = 1.0F;
        float flashlightBlindDurationSeconds = 5.0F;
        float flashlightBeamRange = 9.5F;
        float flashlightBeamAngleDegrees = 22.0F;
        int flashlightBlindStyle = 0; // 0=white, 1=dark

        float mapChannelSeconds = 0.75F;
        int mapUses = 5;
        float mapRevealRangeMeters = 48.0F;
        float mapRevealDurationSeconds = 3.0F;

        int trapperStartCarryTraps = 5;
        int trapperMaxCarryTraps = 5;
        int trapperGroundSpawnTraps = 8;
        float trapperSetTrapSeconds = 1.5F;
        float trapperDisarmSeconds = 2.0F;
        float trapEscapeBaseChance = 0.2F;
        float trapEscapeChanceStep = 0.2F;
        float trapEscapeChanceMax = 0.85F;
        float trapKillerStunSeconds = 2.0F;

        float wraithCloakMoveSpeedMultiplier = 1.3F;
        float wraithCloakTransitionSeconds = 1.0F;
        float wraithUncloakTransitionSeconds = 1.0F;
        float wraithPostUncloakHasteSeconds = 2.0F;
        float wraithCloakVaultSpeedMult = 1.25F;
        float wraithCloakPalletBreakSpeedMult = 1.35F;
        float wraithCloakAlpha = 0.15F;

        // Hatchet power (Huntress-like)
        int hatchetMaxCount = 7;
        float hatchetChargeMinSeconds = 0.1F;
        float hatchetChargeMaxSeconds = 1.0F;
        float hatchetThrowSpeedMin = 12.0F;   // Partial charge
        float hatchetThrowSpeedMax = 28.0F;   // Full charge
        float hatchetGravityMin = 12.0F;      // Heavy drop for partial
        float hatchetGravityMax = 4.0F;       // Light drop for full charge
        float hatchetAirDrag = 0.985F;        // Velocity multiplier per frame
        float hatchetCollisionRadius = 0.12F;
        float hatchetMaxRange = 40.0F;
        float hatchetLockerReplenishTime = 2.0F;
        int hatchetLockerReplenishCount = 7;

        float weightTLWalls = 1.0F;
        float weightJungleGymLong = 1.0F;
        float weightJungleGymShort = 1.0F;
        float weightShack = 1.0F;
        float weightFourLane = 1.0F;
        float weightFillerA = 1.0F;
        float weightFillerB = 1.0F;
        // v2 loop types
        float weightLongWall = 1.0F;
        float weightShortWall = 0.8F;
        float weightLWallWindow = 1.2F;
        float weightLWallPallet = 1.0F;
        float weightTWalls = 0.9F;
        float weightGymBox = 1.1F;
        float weightDebrisPile = 0.6F;
        int maxLoopsPerMap = 110;  // More loops for 12x12 map
        float minLoopDistanceTiles = 1.5F;
        int maxSafePallets = 0;   // No pallets
        int maxDeadzoneTiles = 3;
        bool edgeBiasLoops = true;
        bool disableWindowsAndPallets = false;

        int serverTickRate = 60;
        int interpolationBufferMs = 350;
    };

    enum class MapType
    {
        Test,
        Main,
        CollisionTest,
        Benchmark
    };

    enum class CameraMode
    {
        ThirdPerson,
        FirstPerson
    };

    struct RoleCommand
    {
        glm::vec2 moveAxis{0.0F};
        glm::vec2 lookDelta{0.0F};
        bool sprinting = false;
        bool crouchHeld = false;
        bool jumpPressed = false;
        bool interactPressed = false;
        bool interactHeld = false;
        bool attackPressed = false;
        bool attackHeld = false;
        bool attackReleased = false;
        bool lungeHeld = false;
        bool useAltPressed = false;
        bool useAltHeld = false;
        bool useAltReleased = false;
        bool dropItemPressed = false;
        bool pickupItemPressed = false;
        bool wiggleLeftPressed = false;
        bool wiggleRightPressed = false;
    };

    struct ActorSnapshot
    {
        glm::vec3 position{0.0F};
        glm::vec3 forward{0.0F, 0.0F, -1.0F};
        glm::vec3 velocity{0.0F};
        float yaw = 0.0F;
        float pitch = 0.0F;
    };

    struct PalletSnapshot
    {
        engine::scene::Entity entity = 0;
        std::uint8_t state = 0;
        float breakTimer = 0.0F;
        glm::vec3 position{0.0F};
        glm::vec3 halfExtents{0.5F};
    };

    struct TrapSnapshot
    {
        engine::scene::Entity entity = 0;
        std::uint8_t state = 0;
        engine::scene::Entity trappedEntity = 0;
        glm::vec3 position{0.0F};
        glm::vec3 halfExtents{0.36F, 0.08F, 0.36F};
        float escapeChance = 0.22F;
        std::uint8_t escapeAttempts = 0;
        std::uint8_t maxEscapeAttempts = 6;
    };

    struct GroundItemSnapshot
    {
        engine::scene::Entity entity = 0;
        glm::vec3 position{0.0F};
        float charges = 0.0F;
        std::string itemId;
        std::string addonAId;
        std::string addonBId;
    };

    struct Snapshot
    {
        MapType mapType = MapType::Test;
        unsigned int seed = 1337U;
        std::array<std::string, 3> survivorPerkIds = {"", "", ""};
        std::array<std::string, 3> killerPerkIds = {"", "", ""};
        std::string survivorCharacterId = "survivor_dwight";
        std::string killerCharacterId = "killer_trapper";
        std::string survivorItemId;
        std::string survivorItemAddonA;
        std::string survivorItemAddonB;
        std::string killerPowerId;
        std::string killerPowerAddonA;
        std::string killerPowerAddonB;
        ActorSnapshot survivor;
        ActorSnapshot killer;
        std::uint8_t survivorState = 0;
        std::uint8_t killerAttackState = 0;
        float killerAttackStateTimer = 0.0F;
        float killerLungeCharge = 0.0F;
        bool chaseActive = false;
        float chaseDistance = 0.0F;
        bool chaseLos = false;
        bool chaseInCenterFOV = false;
        float chaseTimeSinceLOS = 0.0F;
        float chaseTimeSinceCenterFOV = 0.0F;
        float chaseTimeInChase = 0.0F;
        std::uint8_t bloodlustTier = 0;
        float survivorItemCharges = 0.0F;
        std::uint8_t survivorItemActive = 0;
        std::uint8_t survivorItemUsesRemaining = 0;
        std::uint8_t wraithCloaked = 0;
        float wraithTransitionTimer = 0.0F;
        float wraithPostUncloakTimer = 0.0F;
        float killerBlindTimer = 0.0F;
        std::uint8_t killerBlindStyleWhite = 1;
        std::uint8_t carriedTrapCount = 0;
        // Nurse blink power state
        std::uint8_t blinkState = 0;
        std::uint8_t blinkCharges = 2;
        float blinkCharge01 = 0.0F;
        float blinkChargeRegenTimer = 0.0F;
        glm::vec3 blinkTargetPosition{0.0F};
        std::vector<PalletSnapshot> pallets;
        std::vector<TrapSnapshot> traps;
        std::vector<GroundItemSnapshot> groundItems;
    };

    GameplaySystems();

    void Initialize(engine::core::EventBus& eventBus);
    void CaptureInputFrame(const engine::platform::Input& input, const engine::platform::ActionBindings& bindings, bool controlsEnabled);
    void FixedUpdate(float fixedDt, const engine::platform::Input& input, bool controlsEnabled);
    void Update(float deltaSeconds, const engine::platform::Input& input, bool controlsEnabled);

    void Render(engine::render::Renderer& renderer, float aspectRatio);
    [[nodiscard]] glm::mat4 BuildViewProjection(float aspectRatio) const;
    [[nodiscard]] glm::vec3 CameraPosition() const { return m_cameraPosition; }
    [[nodiscard]] glm::vec3 CameraForward() const { return m_cameraForward; }
    [[nodiscard]] HudState BuildHudState() const;

    void LoadMap(const std::string& mapName);
    void RegenerateLoops();
    void RegenerateLoops(unsigned int seed);
    void SetDbdSpawnsEnabled(bool enabled);

    void SpawnSurvivor();
    void SpawnKiller();
    void SpawnPallet();
    void SpawnWindow(std::optional<float> yawDegrees = std::nullopt);
    bool SpawnRoleHere(const std::string& roleName);
    bool SpawnRoleAt(const std::string& roleName, int spawnId);
    bool RespawnRole(const std::string& roleName);
    [[nodiscard]] std::string ListSpawnPoints() const;
    [[nodiscard]] std::vector<SpawnPointInfo> GetSpawnPoints() const;
    [[nodiscard]] engine::scene::Entity RoleEntity(const std::string& roleName) const;
    [[nodiscard]] std::string MovementStateForRole(const std::string& roleName) const;
    [[nodiscard]] glm::vec3 RolePosition(const std::string& roleName) const;
    [[nodiscard]] glm::vec3 RoleForward(const std::string& roleName) const;
    [[nodiscard]] std::string SurvivorHealthStateText() const;

    void TeleportSurvivor(const glm::vec3& position);
    void TeleportKiller(const glm::vec3& position);

    void SetSurvivorSprintSpeed(float speed);
    void SetRoleSpeedPercent(const std::string& roleName, float percent);
    void SetRoleCapsuleSize(const std::string& roleName, float radius, float height);
    void ToggleCollision(bool enabled);
    void ToggleDebugDraw(bool enabled);
    void TogglePhysicsDebug(bool enabled);
    void SetNoClip(bool enabled);
    void SetForcedChase(bool enabled);

    void SetCameraModeOverride(const std::string& modeName);
    void SetControlledRole(const std::string& roleName);
    void ToggleControlledRole();

    void SetRenderModeLabel(const std::string& modeName);
    void SetLookSettings(float survivorSensitivity, float killerSensitivity, bool invertY);
    void ApplyGameplayTuning(const GameplayTuning& tuning);
    [[nodiscard]] GameplayTuning GetGameplayTuning() const;
    void StartSkillCheckDebug();
    void HealSurvivor();
    void SetSurvivorStateDebug(const std::string& stateName);
    void SetGeneratorsCompleted(int completed);
    void HookCarriedSurvivorDebug();
    void ToggleTerrorRadiusVisualization(bool enabled);
    void SetTerrorRadius(float meters);
    [[nodiscard]] bool TerrorRadiusVisualizationEnabled() const { return m_terrorRadiusVisible; }

    // Bloodlust system
    void ResetBloodlust();
    void SetBloodlustTier(int tier);
    [[nodiscard]] int GetBloodlustTier() const { return m_bloodlust.tier; }
    [[nodiscard]] float GetBloodlustSpeedMultiplier() const;

    [[nodiscard]] bool DebugDrawEnabled() const { return m_debugDrawEnabled; }

    void SetNetworkAuthorityMode(bool enabled);
    void SetRemoteRoleCommand(engine::scene::Role role, const RoleCommand& command);
    void ClearRemoteRoleCommands();
    [[nodiscard]] Snapshot BuildSnapshot() const;
    void ApplySnapshot(const Snapshot& snapshot, float blendAlpha);

    void RequestQuit();
    [[nodiscard]] bool QuitRequested() const { return m_quitRequested; }

    void SpawnFxDebug(const std::string& assetId);
    void SpawnTestModels(); // Spawn imported survivor mesh test models
    void SpawnTestModelsHere(); // Spawn imported survivor mesh test models at player's position
    void LoadTestModelMeshes(); // Load glTF meshes for test model rendering
    void SetMeshLibrary(engine::assets::MeshLibrary* lib) { m_meshLibrary = lib; }
    void StopAllFx();
    [[nodiscard]] std::vector<std::string> ListFxAssets() const;
    [[nodiscard]] std::optional<engine::fx::FxAsset> GetFxAsset(const std::string& assetId) const;
    bool SaveFxAsset(const engine::fx::FxAsset& asset, std::string* outError = nullptr);
    void SetFxReplicationCallback(std::function<void(const engine::fx::FxSpawnEvent&)> callback);
    void SpawnReplicatedFx(const engine::fx::FxSpawnEvent& event);

    // Perk system methods
    void SetSurvivorPerkLoadout(const perks::PerkLoadout& loadout);
    void SetKillerPerkLoadout(const perks::PerkLoadout& loadout);
    [[nodiscard]] perks::PerkSystem& GetPerkSystem() { return m_perkSystem; }
    [[nodiscard]] const perks::PerkSystem& GetPerkSystem() const { return m_perkSystem; }

    bool SetSurvivorItemLoadout(const std::string& itemId, const std::string& addonAId, const std::string& addonBId);
    bool SetKillerPowerLoadout(const std::string& powerId, const std::string& addonAId, const std::string& addonBId);
    [[nodiscard]] std::string ItemDump() const;
    [[nodiscard]] std::string PowerDump() const;
    [[nodiscard]] std::vector<std::string> ListItemIds() const;
    [[nodiscard]] std::vector<std::string> ListPowerIds() const;
    [[nodiscard]] const loadout::GameplayCatalog& GetLoadoutCatalog() const { return m_loadoutCatalog; }
    bool RespawnItemsNearPlayer(float radiusMeters = 3.0F);
    bool SpawnGroundItemDebug(const std::string& itemId, float charges = -1.0F);

    bool SetSelectedSurvivorCharacter(const std::string& characterId);
    bool SetSelectedKillerCharacter(const std::string& characterId);
    bool ReloadSelectedSurvivorCharacter(bool reloadAnimations = true);
    bool ReloadSelectedSurvivorAnimations();
    [[nodiscard]] const std::string& SelectedSurvivorCharacterId() const { return m_selectedSurvivorCharacterId; }
    [[nodiscard]] std::vector<std::string> ListSurvivorCharacters() const;
    [[nodiscard]] std::vector<std::string> ListKillerCharacters() const;

    void TrapSpawnDebug(int count = 1);
    void TrapClearDebug();
    void SetTrapDebug(bool enabled) { m_trapDebugEnabled = enabled; }
    [[nodiscard]] bool TrapDebugEnabled() const { return m_trapDebugEnabled; }

    // Hatchet power debug and control
    void SetHatchetDebug(bool enabled) { m_hatchetDebugEnabled = enabled; }
    [[nodiscard]] bool HatchetDebugEnabled() const { return m_hatchetDebugEnabled; }
    void SetHatchetCount(int count);
    void RefillHatchets();
    void SpawnLockerAtKiller();
    [[nodiscard]] int GetHatchetCount() const { return m_killerPowerState.hatchetCount; }
    [[nodiscard]] int GetActiveProjectileCount() const;

    // Chainsaw sprint power debug and control
    void SetChainsawDebug(bool enabled) { m_chainsawDebugEnabled = enabled; }
    [[nodiscard]] bool ChainsawDebugEnabled() const { return m_chainsawDebugEnabled; }
    void SetChainsawOverheat(float value);
    void ResetChainsawState();
    [[nodiscard]] float GetChainsawOverheat() const { return m_killerPowerState.chainsawOverheat; }

    // Apply chainsaw config from external settings (for live tuning)
    void ApplyChainsawConfig(
        float chargeTime,
        float sprintSpeedMultiplier,
        float turnBoostWindow,
        float turnBoostRate,
        float turnRestrictedRate,
        float collisionRecoveryDuration,
        float recoveryHitDuration,
        float recoveryCancelDuration,
        float overheatPerSecondCharge,
        float overheatPerSecondSprint,
        float overheatCooldownRate,
        float overheatBuffThreshold,
        float overheatChargeBonus,
        float overheatSpeedBonus,
        float overheatTurnBonus,
        float collisionRaycastDistance,
        float survivorHitRadius,
        float chargeSlowdownMultiplier
    );

    // Nurse blink power debug and control
    void SetBlinkDebug(bool enabled) { m_blinkDebugEnabled = enabled; }
    [[nodiscard]] bool BlinkDebugEnabled() const { return m_blinkDebugEnabled; }
    void SetBlinkCharges(int charges);
    void ResetBlinkState();
    [[nodiscard]] int GetBlinkChargesCount() const { return m_killerPowerState.blinkCharges; }
    [[nodiscard]] std::string GetBlinkStateString() const;
    std::string GetBlinkDumpInfo() const;

    // Phase B2/B3: Scratch Marks and Blood Pools debug control
    void SetScratchDebug(bool enabled);
    void SetBloodDebug(bool enabled);
    void SetScratchProfile(const std::string& profileName);
    void SetBloodProfile(const std::string& profileName);
    [[nodiscard]] int GetActiveScratchCount() const
    {
        int count = 0;
        for (const auto& mark : m_scratchMarks)
        {
            if (mark.active) ++count;
        }
        return count;
    }
    [[nodiscard]] int GetActiveBloodPoolCount() const
    {
        int count = 0;
        for (const auto& pool : m_bloodPools)
        {
            if (pool.active) ++count;
        }
        return count;
    }
    [[nodiscard]] bool ScratchDebugEnabled() const { return m_scratchDebugEnabled; }
    [[nodiscard]] bool BloodDebugEnabled() const { return m_bloodDebugEnabled; }

    // Phase B4: Killer Look Light control
    void SetKillerLookLightEnabled(bool enabled) { m_killerLookLight.enabled = enabled; }
    void SetKillerLookLightRange(float range) { m_killerLookLight.range = range; }
    void SetKillerLookLightIntensity(float intensity) { m_killerLookLight.intensity = intensity; }
    void SetKillerLookLightAngle(float angleDegrees) { m_killerLookLight.innerAngleDegrees = angleDegrees; }
    void SetKillerLookLightOuterAngle(float angleDegrees) { m_killerLookLight.outerAngleDegrees = angleDegrees; }
    void SetKillerLookLightPitch(float pitchDegrees) { m_killerLookLight.pitchDegrees = pitchDegrees; }
    void SetKillerLookLightDebug(bool enabled) { m_killerLookLightDebug = enabled; }
    [[nodiscard]] bool KillerLookLightEnabled() const { return m_killerLookLight.enabled; }
    [[nodiscard]] bool KillerLookLightDebug() const { return m_killerLookLightDebug; }
    [[nodiscard]] float KillerLightIntensity() const { return m_killerLookLight.intensity; }
    [[nodiscard]] float KillerLightRange() const { return m_killerLookLight.range; }
    [[nodiscard]] float KillerLightInnerAngle() const { return m_killerLookLight.innerAngleDegrees; }
    [[nodiscard]] float KillerLightOuterAngle() const { return m_killerLookLight.outerAngleDegrees; }
    [[nodiscard]] float KillerLightPitch() const { return m_killerLookLight.pitchDegrees; }

    void SetMapSpotLightCount(std::size_t count) { m_mapSpotLightCount = count; }

    // Status Effect System
    void ApplyStatusEffect(StatusEffectType type, const std::string& targetRole, float duration, float strength, const std::string& sourceId);
    void RemoveStatusEffect(StatusEffectType type, const std::string& targetRole);
    [[nodiscard]] StatusEffectManager& GetStatusEffectManager() { return m_statusEffectManager; }
    [[nodiscard]] const StatusEffectManager& GetStatusEffectManager() const { return m_statusEffectManager; }
    [[nodiscard]] bool IsKillerUndetectable() const;
    [[nodiscard]] bool IsSurvivorExposed() const;
    [[nodiscard]] bool IsSurvivorExhausted() const;
    [[nodiscard]] std::string StatusEffectDump() const;

    // Animation System
    [[nodiscard]] engine::animation::AnimationSystem& GetAnimationSystem() { return m_animationSystem; }
    [[nodiscard]] const engine::animation::AnimationSystem& GetAnimationSystem() const { return m_animationSystem; }
    void SetAnimationDebug(bool enabled) { m_animationDebugEnabled = enabled; }
    [[nodiscard]] bool AnimationDebugEnabled() const { return m_animationDebugEnabled; }
    void ForceAnimationState(const std::string& stateName);
    void SetAnimationAutoMode(bool autoMode);
    [[nodiscard]] std::string GetAnimationInfo() const;
    [[nodiscard]] std::vector<std::string> GetAnimationClipList() const;
    void ForcePlayAnimationClip(const std::string& clipName);
    void SetGlobalAnimationScale(float scale);
    void LoadAnimationConfig();

private:
    enum class InteractionType
    {
        None,
        WindowVault,
        PalletVault,
        DropPallet,
        BreakPallet,
        PickupSurvivor,
        DropSurvivor,
        HookSurvivor,
        RepairGenerator,
        SelfHeal,
        ReplenishHatchets
    };

    enum class VaultType
    {
        Slow,
        Medium,
        Fast
    };

    enum class SurvivorHealthState
    {
        Healthy,
        Injured,
        Downed,
        Trapped,
        Carried,
        Hooked,
        Dead
    };

    enum class KillerAttackState
    {
        Idle,
        ChargingLunge,
        Lunging,
        Recovering
    };

    enum class ChainsawSprintState
    {
        Idle,
        Charging,
        Sprinting,
        Recovery
        // REMOVED: Overheated - now overheat grants buffs instead of locking
    };

    enum class NurseBlinkState
    {
        Idle,               // Normal state
        ChargingBlink,      // Holding power, charging distance
        BlinkTravel,        // Teleporting (phasing through walls)
        ChainWindow,        // Post-blink decision window
        BlinkAttackWindup,  // Initiating blink attack
        Fatigue             // Recovery with movement penalty
    };

    enum class SkillCheckMode
    {
        None,
        Generator,
        SelfHeal,
        HookStruggle
    };

    enum class ControlledRole
    {
        Survivor,
        Killer
    };

    enum class CameraOverride
    {
        RoleBased,
        SurvivorThirdPerson,
        KillerFirstPerson
    };

    struct InteractionCandidate
    {
        InteractionType type = InteractionType::None;
        engine::scene::Entity entity = 0;
        int priority = 0;
        float castT = 1.0F;
        std::string prompt;
        std::string typeName = "None";
        std::string targetName = "None";
    };

    struct ChaseState
    {
        bool isChasing = false;
        bool hasLineOfSight = false;
        bool inCenterFOV = false;          // Survivor in killer's center FOV (±35°)
        float distance = 0.0F;
        float timeSinceSeenLOS = 0.0F;     // Time since killer had LOS to survivor
        float timeSinceCenterFOV = 0.0F;   // Time since survivor was in center FOV
        float timeInChase = 0.0F;          // Total time chase has been active
        float startDistance = 12.0F;        // Chase start distance (DBD-like)
        float endDistance = 18.0F;          // Chase end distance (DBD-like)
        float lostSightTimeout = 8.0F;      // DBD-like: 8s lost LOS timeout
        float lostCenterFOVTimeout = 8.0F;  // DBD-like: 8s lost center FOV timeout
    };

    struct BloodlustState
    {
        int tier = 0;                          // 0-3 (0 = none, 1/2/3 = active tiers)
        float timeInChase = 0.0F;              // Time spent in current chase (for tier thresholds)
        float lastTierChangeTime = 0.0F;        // When the last tier change occurred
    };

    struct TimedMessage
    {
        std::string text;
        float ttl = 0.0F;
    };

    void BuildSceneFromMap(MapType mapType, unsigned int seed);
    void BuildSceneFromGeneratedMap(
        const ::game::maps::GeneratedMap& generated,
        MapType mapType,
        unsigned int seed,
        const std::string& mapDisplayName
    );
    void RebuildPhysicsWorld();
    void DestroyEntity(engine::scene::Entity entity);
    [[nodiscard]] bool ResolveSpawnPositionValid(
        const glm::vec3& requestedPosition,
        float radius,
        float height,
        glm::vec3* outResolved
    );
    [[nodiscard]] std::optional<SpawnPointInfo> FindSpawnPointById(int spawnId) const;
    [[nodiscard]] std::optional<SpawnPointInfo> FindSpawnPointByType(SpawnPointType type) const;
    [[nodiscard]] SpawnPointType SpawnPointTypeFromRole(const std::string& roleName) const;
    [[nodiscard]] const char* SpawnTypeToText(SpawnPointType type) const;
    [[nodiscard]] engine::scene::Entity SpawnRoleActorAt(const std::string& roleName, const glm::vec3& position);

    void UpdateActorLook(engine::scene::Entity entity, const glm::vec2& mouseDelta, float sensitivity);
    void UpdateActorMovement(
        engine::scene::Entity entity,
        const glm::vec2& moveAxis,
        bool sprinting,
        bool jumpPressed,
        bool crouchHeld,
        float fixedDt
    );
    void UpdateVaultState(engine::scene::Entity entity, float fixedDt);

    void UpdateInteractionCandidate();
    void ExecuteInteractionForRole(engine::scene::Entity actorEntity, const InteractionCandidate& candidate);
    [[nodiscard]] InteractionCandidate ResolveInteractionCandidateFromView(engine::scene::Entity actorEntity) const;
    [[nodiscard]] InteractionCandidate BuildWindowVaultCandidate(engine::scene::Entity actorEntity, engine::scene::Entity windowEntity, float castT) const;
    [[nodiscard]] InteractionCandidate BuildStandingPalletCandidate(engine::scene::Entity actorEntity, engine::scene::Entity palletEntity, float castT) const;
    [[nodiscard]] InteractionCandidate BuildDroppedPalletCandidate(engine::scene::Entity actorEntity, engine::scene::Entity palletEntity, float castT) const;
    [[nodiscard]] InteractionCandidate BuildDropSurvivorCandidate(engine::scene::Entity actorEntity) const;
    [[nodiscard]] InteractionCandidate BuildPickupSurvivorCandidate(
        engine::scene::Entity actorEntity,
        const glm::vec3& castStart,
        const glm::vec3& castDirection
    ) const;
    [[nodiscard]] InteractionCandidate BuildHookSurvivorCandidate(engine::scene::Entity actorEntity, engine::scene::Entity hookEntity, float castT) const;
    [[nodiscard]] InteractionCandidate BuildGeneratorRepairCandidate(engine::scene::Entity actorEntity, engine::scene::Entity generatorEntity, float castT) const;
    [[nodiscard]] InteractionCandidate BuildSelfHealCandidate(engine::scene::Entity actorEntity) const;
    [[nodiscard]] bool IsActorInputLocked(const engine::scene::ActorComponent& actor) const;
    [[nodiscard]] VaultType DetermineWindowVaultType(
        const engine::scene::ActorComponent& actor,
        const engine::scene::Transform& actorTransform,
        const engine::scene::Transform& windowTransform,
        const engine::scene::WindowComponent& window
    ) const;
    [[nodiscard]] VaultType DeterminePalletVaultType(const engine::scene::ActorComponent& actor) const;
    static const char* VaultTypeToText(VaultType type);

    void TryKillerHit();
    bool ResolveKillerAttackHit(float range, float halfAngleRadians, const glm::vec3& directionOverride = glm::vec3{0.0F});
    void UpdateKillerAttack(const RoleCommand& killerCommand, float fixedDt);
    void UpdatePalletBreak(float fixedDt);
    void UpdateChaseState(float fixedDt);
    void UpdateBloodlust(float fixedDt);
    void UpdateCamera(float deltaSeconds);

    [[nodiscard]] CameraMode ResolveCameraMode() const;
    [[nodiscard]] engine::scene::Entity ControlledEntity() const;
    [[nodiscard]] engine::scene::Role ControlledSceneRole() const;

    void BeginWindowVault(engine::scene::Entity actorEntity, engine::scene::Entity windowEntity);
    void BeginPalletVault(engine::scene::Entity actorEntity, engine::scene::Entity palletEntity);
    void TryStunKillerFromPallet(engine::scene::Entity palletEntity);
    void TryPickupDownedSurvivor();
    void TryHookCarriedSurvivor(engine::scene::Entity hookEntity);
    void UpdateCarriedSurvivor();
    void UpdateCarryEscapeQte(bool survivorInputEnabled, float fixedDt);
    void UpdateHookStages(float fixedDt, bool hookAttemptPressed, bool hookSkillCheckPressed);
    void UpdateGeneratorRepair(bool holdingRepair, bool skillCheckPressed, float fixedDt);
    void StopGeneratorRepair();
    void BeginOrContinueGeneratorRepair(engine::scene::Entity generatorEntity);
    void BeginSelfHeal();
    void StopSelfHeal();
    void UpdateSelfHeal(bool holdingHeal, bool skillCheckPressed, float fixedDt);
    void CompleteSkillCheck(bool success, bool timeout = false);
    void ScheduleNextSkillCheck();
    void RefreshGeneratorsCompleted();
    void ResolveKillerSurvivorCollision();
    void ApplyKillerAttackAftermath(bool hit, bool lungeAttack);
    void ApplySurvivorHit();
    bool SetSurvivorState(SurvivorHealthState nextState, const std::string& reason, bool force = false);
    [[nodiscard]] bool CanTransitionSurvivorState(SurvivorHealthState from, SurvivorHealthState to) const;
    static const char* SurvivorStateToText(SurvivorHealthState state);
    [[nodiscard]] const char* KillerAttackStateToText(KillerAttackState state) const;
    [[nodiscard]] std::string BuildMovementStateText(engine::scene::Entity entity, const engine::scene::ActorComponent& actor) const;
    engine::fx::FxSystem::FxInstanceId SpawnGameplayFx(
        const std::string& assetId,
        const glm::vec3& position,
        const glm::vec3& forward,
        engine::fx::FxNetMode mode
    );

    void AddRuntimeMessage(const std::string& text, float ttl = 2.0F);
    [[nodiscard]] RoleCommand BuildLocalRoleCommand(
        engine::scene::Role role,
        const engine::platform::Input& input,
        const engine::platform::ActionBindings& bindings,
        bool controlsEnabled,
        bool inputLocked
    ) const;
    void UpdateInteractBuffer(engine::scene::Role role, const RoleCommand& command, float fixedDt);
    [[nodiscard]] bool ConsumeInteractBuffered(engine::scene::Role role);
    void ConsumeWigglePressedForSurvivor(bool& leftPressed, bool& rightPressed);
    static std::uint8_t RoleToIndex(engine::scene::Role role);
    [[nodiscard]] static float DistanceXZ(const glm::vec3& a, const glm::vec3& b);
    [[nodiscard]] static float DistancePointToSegment(const glm::vec3& point, const glm::vec3& segmentA, const glm::vec3& segmentB);
    [[nodiscard]] static glm::vec3 ForwardFromYawPitch(float yaw, float pitch);
    void InitializeLoadoutCatalog();
    void RefreshLoadoutModifiers();
    void RefreshSurvivorModelCapsuleOverride();
    bool TryFallbackToAvailableSurvivorModel(const std::string& failedCharacterId);
    [[nodiscard]] bool LoadSurvivorCharacterBounds(
        const std::string& characterId,
        float* outMinY,
        float* outMaxY,
        float* outMaxAbsXZ
    );
    [[nodiscard]] bool EnsureSurvivorCharacterMeshLoaded(const std::string& characterId);
    [[nodiscard]] bool ReloadSurvivorCharacterAnimations(const std::string& characterId);
    [[nodiscard]] bool LoadSurvivorAnimationRig(const std::string& characterId);
    [[nodiscard]] bool BuildAnimatedSurvivorGeometry(
        const std::string& characterId,
        engine::render::MeshGeometry* outGeometry,
        float* outMinY,
        float* outMaxY,
        float* outMaxAbsXZ
    ) const;
    void RefreshAnimatedSurvivorMeshIfNeeded(const std::string& characterId);
    void ResetItemAndPowerRuntimeState();
    void UpdateSurvivorItemSystem(const RoleCommand& survivorCommand, float fixedDt);
    void UpdateKillerPowerSystem(const RoleCommand& killerCommand, float fixedDt);
    void UpdateBearTrapSystem(const RoleCommand& survivorCommand, const RoleCommand& killerCommand, float fixedDt);
    void UpdateWraithPowerSystem(const RoleCommand& killerCommand, float fixedDt);
    void UpdateHatchetPowerSystem(const RoleCommand& killerCommand, float fixedDt);
    void UpdateChainsawSprintPowerSystem(const RoleCommand& killerCommand, float fixedDt);
    void LoadChainsawSprintConfig();
    void RenderChainsawDebug(engine::render::Renderer& renderer);
    void UpdateNurseBlinkPowerSystem(const RoleCommand& killerCommand, float fixedDt);
    void LoadNurseBlinkConfig();
    void RenderBlinkPreview(engine::render::Renderer& renderer);
    void RenderBlinkDebug(engine::render::Renderer& renderer);
    [[nodiscard]] bool ResolveBlinkEndpoint(const glm::vec3& start, const glm::vec3& requested, glm::vec3& out);
    void UpdateProjectiles(float fixedDt);
    engine::scene::Entity SpawnHatchetProjectile(const glm::vec3& origin, const glm::vec3& direction, float charge01);
    engine::scene::Entity SpawnLocker(const glm::vec3& position, const glm::vec3& forward);
    void RenderHatchetDebug(engine::render::Renderer& renderer);
    void RenderHatchetTrajectoryPrediction(engine::render::Renderer& renderer);
    void RenderHatchetProjectiles(engine::render::Renderer& renderer);
    [[nodiscard]] bool ProjectileHitsCapsule(
        const glm::vec3& projectilePos,
        float projectileRadius,
        const glm::vec3& capsulePos,
        float capsuleRadius,
        float capsuleHeight
    ) const;
    [[nodiscard]] glm::vec3 ClosestPointOnSegment(const glm::vec3& point, const glm::vec3& a, const glm::vec3& b) const;
    void ApplySurvivorItemActionLock(float durationSeconds);
    bool TryDropSurvivorItemToGround();
    bool TryPickupSurvivorGroundItem();
    bool TrySwapSurvivorGroundItem();
    [[nodiscard]] engine::scene::Entity FindNearestGroundItem(const glm::vec3& fromPosition, float radiusMeters) const;
    engine::scene::Entity SpawnGroundItemEntity(
        const std::string& itemId,
        const glm::vec3& position,
        float charges,
        const std::string& addonAId = std::string{},
        const std::string& addonBId = std::string{},
        bool respawnTag = false
    );
    void SpawnInitialTrapperGroundTraps();
    bool TryFindNearestTrap(
        const glm::vec3& fromPosition,
        float radiusMeters,
        bool requireDisarmed,
        engine::scene::Entity* outTrapEntity
    ) const;
    bool ComputeTrapPlacementPreview(glm::vec3* outPosition, glm::vec3* outHalfExtents, bool* outValid = nullptr) const;
    engine::scene::Entity SpawnBearTrap(const glm::vec3& basePosition, const glm::vec3& forward, bool emitMessage = true);
    void ClearAllBearTraps();
    void TryTriggerBearTraps(engine::scene::Entity survivorEntity, const glm::vec3& survivorPos);
    void ClearTrappedSurvivorBinding(engine::scene::Entity survivorEntity, bool disarmTrap);
    static const char* CameraModeToName(CameraMode mode);
    [[nodiscard]] const char* MapTypeToName(MapType type) const;
    static std::uint8_t MapTypeToByte(MapType type);
    static MapType ByteToMapType(std::uint8_t byte);
    static const char* MapNameFromIndex(int index);
    [[nodiscard]] const char* RoleNameFromIndex(int index) const;
    static const char* RoleNameFromEnum(engine::scene::Role role);
    static bool IsEnemyRole(engine::scene::Role myRole, engine::scene::Role otherRole);
    static engine::scene::Role ParseRoleEnum(const std::string& roleName);
    [[nodiscard]] static bool IsSurvivorInKillerFOV(
        const glm::vec3& killerPos, const glm::vec3& killerForward,
        const glm::vec3& survivorPos, float fovDegrees = 87.0F);
    [[nodiscard]] static bool IsSurvivorInKillerCenterFOV(
        const glm::vec3& killerPos, const glm::vec3& killerForward,
        const glm::vec3& survivorPos); // Uses ±35° center FOV (DBD-like)

    engine::core::EventBus* m_eventBus = nullptr;
    engine::render::Renderer* m_rendererPtr = nullptr;  // Set on first Render call, used for GPU resource cleanup.

    engine::scene::World m_world;
    engine::physics::PhysicsWorld m_physics;
    engine::fx::FxSystem m_fxSystem;

    MapType m_currentMap = MapType::Test;
    std::string m_activeMapName = "test";
    unsigned int m_generationSeed = std::random_device{}();

    engine::scene::Entity m_survivor = 0;
    engine::scene::Entity m_killer = 0;
    engine::scene::Entity m_killerBreakingPallet = 0;
    SurvivorHealthState m_survivorState = SurvivorHealthState::Healthy;
    int m_generatorsCompleted = 0;
    int m_generatorsTotal = 5;

    ControlledRole m_controlledRole = ControlledRole::Survivor;
    CameraOverride m_cameraOverride = CameraOverride::RoleBased;

    ChaseState m_chase;
    std::optional<bool> m_forcedChase;
    BloodlustState m_bloodlust{};

    InteractionCandidate m_interactionCandidate{};
    float m_interactionPromptHoldSeconds = 0.0F;
    mutable std::vector<engine::physics::TriggerCastHit> m_sphereCastScratch;

    bool m_collisionEnabled = true;
    bool m_debugDrawEnabled = true;
    bool m_physicsDebugEnabled = false;
    bool m_noClipEnabled = false;
    bool m_quitRequested = false;
    bool m_networkAuthorityMode = false;

    // Test model mesh loading and rendering
    engine::assets::MeshLibrary* m_meshLibrary = nullptr;
    struct TestModelGpuMeshes
    {
        engine::render::Renderer::GpuMeshId maleBody = engine::render::Renderer::kInvalidGpuMesh;
        engine::render::Renderer::GpuMeshId femaleBody = engine::render::Renderer::kInvalidGpuMesh;
        float maleFeetOffset = 0.0F;
        float femaleFeetOffset = 0.0F;
    };
    TestModelGpuMeshes m_testModelMeshes;

    struct SurvivorVisualMesh
    {
        engine::render::Renderer::GpuMeshId gpuMesh = engine::render::Renderer::kInvalidGpuMesh;
        float boundsMinY = 0.0F;
        float boundsMaxY = 1.8F;
        float maxAbsXZ = 0.3F;
        bool boundsLoadAttempted = false;
        bool boundsLoaded = false;
        bool boundsLoadFailed = false;
        bool gpuUploadAttempted = false;
    };

    struct SurvivorAnimationRig
    {
        bool loaded = false;
        bool runtimeUploadLogged = false;
        int meshNodeIndex = -1;
        int skinIndex = -1;
        std::vector<int> sceneRoots;
        std::vector<int> nodeParents;
        std::vector<glm::vec3> restTranslations;
        std::vector<glm::quat> restRotations;
        std::vector<glm::vec3> restScales;
        std::vector<int> skinJoints;
        std::vector<glm::mat4> inverseBindMatrices;
        std::vector<glm::vec3> basePositions;
        std::vector<glm::vec3> baseNormals;
        std::vector<glm::vec3> baseColors;
        std::vector<glm::vec2> baseUvs;
        std::vector<glm::uvec4> jointIndices;
        std::vector<glm::vec4> jointWeights;
        std::vector<std::uint32_t> indices;
    };

    std::unordered_map<std::string, SurvivorVisualMesh> m_survivorVisualMeshes;
    std::unordered_map<std::string, SurvivorAnimationRig> m_survivorAnimationRigs;
    float m_survivorCapsuleOverrideRadius = -1.0F;
    float m_survivorCapsuleOverrideHeight = -1.0F;
    float m_survivorVisualYawRadians = 0.0F;
    bool m_survivorVisualYawInitialized = false;
    float m_survivorVisualTurnSpeedRadiansPerSecond = 8.0F;
    float m_survivorVisualTargetYawRadians = 0.0F;
    glm::vec2 m_survivorVisualMoveInput{0.0F};
    glm::vec3 m_survivorVisualDesiredDirection{0.0F};

    struct TestModelData
    {
        glm::vec3 malePosition{5.0F, 0.0F, 5.0F};
        glm::vec3 femalePosition{7.0F, 0.0F, 5.0F};
        bool spawned = false;
    };
    TestModelData m_testModels;
    bool m_dbdSpawnsEnabled = false;

    RoleCommand m_localSurvivorCommand{};
    RoleCommand m_localKillerCommand{};
    std::optional<RoleCommand> m_remoteSurvivorCommand;
    std::optional<RoleCommand> m_remoteKillerCommand;

    float m_interactBufferWindowSeconds = 0.18F;
    std::array<float, 2> m_interactBufferRemaining{0.0F, 0.0F};
    std::vector<int> m_survivorWigglePressQueue;

    float m_killerHitCooldown = 0.0F;
    bool m_terrorRadiusVisible = true;
    float m_terrorRadiusMeters = 24.0F;
    float m_terrorRadiusChaseMeters = 32.0F;

    KillerAttackState m_killerAttackState = KillerAttackState::Idle;
    float m_killerAttackStateTimer = 0.0F;
    float m_killerLungeChargeSeconds = 0.0F;
    float m_killerAttackFlashTtl = 0.0F;
    bool m_killerAttackHitThisAction = false;
    bool m_previousAttackHeld = false;
    float m_killerShortRange = 2.6F;
    float m_killerShortHalfAngleRadians = 0.5235988F;
    float m_killerLungeRange = 3.4F;
    float m_killerLungeHalfAngleRadians = 0.5235988F;
    float m_killerLungeChargeMinSeconds = 0.08F;
    float m_killerLungeChargeMaxSeconds = 0.62F;
    float m_killerLungeDurationSeconds = 0.62F;
    float m_killerLungeRecoverSeconds = 0.58F;
    float m_killerShortRecoverSeconds = 0.52F;
    float m_killerMissRecoverSeconds = 0.32F;
    float m_killerLungeSpeedStart = 7.0F;
    float m_killerLungeSpeedEnd = 11.5F;
    float m_killerCurrentLungeSpeed = 0.0F;
    float m_survivorHitHasteTimer = 0.0F;
    float m_survivorHitHasteSeconds = 1.45F;
    float m_survivorHitHasteMultiplier = 1.18F;
    float m_actorGroundAcceleration = 36.0F;
    float m_actorGroundDeceleration = 22.0F;
    float m_killerSurvivorNoCollisionTimer = 0.0F;
    float m_killerSurvivorNoCollisionAfterHitSeconds = 1.8F;
    float m_killerSurvivorNoCollisionBreakDistance = 4.0F;
    glm::vec3 m_killerPreMovePosition{0.0F};
    glm::vec3 m_survivorPreMovePosition{0.0F};
    bool m_killerPreMovePositionValid = false;
    bool m_survivorPreMovePositionValid = false;
    float m_killerSlowTimer = 0.0F;
    float m_killerSlowMultiplier = 1.0F;
    float m_killerHitSlowSeconds = 1.05F;
    float m_killerHitSlowMultiplier = 0.6F;
    float m_killerMissSlowSeconds = 0.45F;
    float m_killerMissSlowMultiplier = 0.78F;

    glm::vec3 m_lastHitRayStart{0.0F};
    glm::vec3 m_lastHitRayEnd{0.0F};
    bool m_lastHitConnected = false;
    glm::vec3 m_lastSwingOrigin{0.0F};
    glm::vec3 m_lastSwingDirection{0.0F, 0.0F, -1.0F};
    float m_lastSwingRange = 0.0F;
    float m_lastSwingHalfAngleRadians = 0.0F;
    float m_lastSwingDebugTtl = 0.0F;

    float m_elapsedSeconds = 0.0F;
    float m_carryEscapeProgress = 0.0F;
    int m_carryLastQteDirection = 0;
    float m_carryInputGraceTimer = 0.0F;
    float m_hookStageTimer = 0.0F;
    int m_hookStage = 0;
    int m_hookEscapeAttemptsUsed = 0;
    int m_hookEscapeAttemptsMax = 3;
    float m_hookEscapeChance = 0.04F;
    float m_hookStageOneDuration = 20.0F;
    float m_hookStageTwoDuration = 20.0F;
    float m_hookStageFailPenaltySeconds = 2.8F;
    float m_hookSkillCheckTimeToNext = 0.0F;
    engine::scene::Entity m_activeHookEntity = 0;
    engine::scene::Entity m_activeRepairGenerator = 0;
    bool m_selfHealActive = false;
    float m_selfHealProgress = 0.0F;
    bool m_skillCheckActive = false;
    SkillCheckMode m_skillCheckMode = SkillCheckMode::None;
    float m_skillCheckNeedle = 0.0F;
    float m_skillCheckNeedleSpeed = 0.9F;
    float m_skillCheckSuccessStart = 0.0F;
    float m_skillCheckSuccessEnd = 0.0F;
    float m_skillCheckTimeToNext = 2.0F;
    float m_survivorSpeedPercent = 1.0F;
    float m_killerSpeedPercent = 1.15F;
    mutable std::mt19937 m_rng;

    float m_survivorLookSensitivity = 0.0022F;
    float m_killerLookSensitivity = 0.0022F;
    bool m_invertLookY = false;

    GameplayTuning m_tuning{};
    maps::TileGenerator::GenerationSettings m_generationSettings{};

    glm::vec3 m_cameraPosition{0.0F, 4.0F, 6.0F};
    glm::vec3 m_cameraTarget{0.0F, 1.0F, 0.0F};
    glm::vec3 m_cameraForward{0.0F, 0.0F, -1.0F};
    bool m_cameraInitialized = false;

    std::vector<TimedMessage> m_messages;
    std::string m_renderModeName = "wireframe";

    struct LoopDebugTile
    {
        glm::vec3 center{0.0F};
        glm::vec3 halfExtents{5.0F, 0.05F, 5.0F};
        int loopId = 0;
        int archetype = 0;
    };

    std::vector<LoopDebugTile> m_loopDebugTiles;
    std::vector<SpawnPointInfo> m_spawnPoints;
    int m_nextSpawnPointId = 1;
    std::function<void(const engine::fx::FxSpawnEvent&)> m_fxReplicationCallback;
    engine::fx::FxSystem::FxInstanceId m_chaseAuraFxId = 0;

    // Perks system
    perks::PerkSystem m_perkSystem;
    perks::PerkLoadout m_survivorPerks;
    perks::PerkLoadout m_killerPerks;

    // Status Effect System
    StatusEffectManager m_statusEffectManager;

    loadout::GameplayCatalog m_loadoutCatalog;
    loadout::LoadoutSurvivor m_survivorLoadout;
    loadout::LoadoutKiller m_killerLoadout;
    loadout::AddonModifierContext m_survivorItemModifiers;
    loadout::AddonModifierContext m_killerPowerModifiers;
    std::string m_selectedSurvivorCharacterId = "survivor_dwight";
    std::string m_selectedKillerCharacterId = "killer_trapper";
    bool m_trapDebugEnabled = false;
    bool m_hatchetDebugEnabled = false;

    struct SurvivorItemRuntimeState
    {
        float charges = 0.0F;
        bool active = false;
        float cooldown = 0.0F;
        float flashBlindAccum = 0.0F;
        float flashlightBatterySeconds = 0.0F;
        float flashlightSuccessFlashTimer = 0.0F;
        float actionLockTimer = 0.0F;
        float mapRevealTtl = 0.0F;
        float mapChannelSeconds = 0.0F;
        int mapUsesRemaining = 0;
        engine::scene::Entity mapRevealLastGenerator = 0;
        float trapDisarmProgress = 0.0F;
        engine::scene::Entity trapDisarmTarget = 0;
    };

    struct KillerPowerRuntimeState
    {
        int trapperCarriedTraps = 0;
        int trapperMaxCarryTraps = 5;
        float trapperSetTimer = 0.0F;
        bool trapperSetting = false;
        bool trapperSetRequiresRelease = false;
        engine::scene::Entity trapperFocusTrap = 0;

        bool wraithCloaked = false;
        bool wraithCloakTransition = false;
        bool wraithUncloakTransition = false;
        float wraithTransitionTimer = 0.0F;
        float wraithPostUncloakTimer = 0.0F;

        float killerBlindTimer = 0.0F;

        // Hatchet power state
        int hatchetCount = 7;
        int hatchetMaxCount = 7;
        float hatchetChargeTimer = 0.0F;
        bool hatchetCharging = false;
        float hatchetCharge01 = 0.0F;
        bool hatchetThrowRequiresRelease = false;
        float lockerReplenishTimer = 0.0F;
        bool lockerReplenishing = false;
        engine::scene::Entity lockerTargetEntity = 0;

        // Chainsaw sprint power state
        ChainsawSprintState chainsawState = ChainsawSprintState::Idle;
        float chainsawChargeTimer = 0.0F;
        float chainsawSprintTimer = 0.0F;
        float chainsawRecoveryTimer = 0.0F;
        float chainsawOverheat = 0.0F;
        float chainsawCurrentSpeed = 0.0F;
        bool chainsawHitThisSprint = false;
        bool chainsawCollisionThisSprint = false;
        bool chainsawSprintRequiresRelease = false;

        // New turn rate tracking
        float chainsawSprintTurnBoostTimer = 0.0F;  // Timer for turn boost (0-0.5s)
        bool chainsawInTurnBoostWindow = false;     // True during first 0.5s

        // New recovery tracking
        bool chainsawRecoveryWasCollision = false;  // Track recovery type
        bool chainsawRecoveryWasHit = false;        // Track if recovery from hit

        // Nurse blink power state
        NurseBlinkState blinkState = NurseBlinkState::Idle;
        int blinkCharges = 2;
        int blinkMaxCharges = 2;
        float blinkChargeRegenTimer = 0.0F;
        float blinkChargeTimer = 0.0F;
        float blinkCharge01 = 0.0F;
        int blinksUsedThisChain = 0;
        float blinkTravelTimer = 0.0F;
        float blinkChainWindowTimer = 0.0F;
        float blinkChainChargeRemaining = 0.0F;  // Time left before fatigue when charging during chain window
        float blinkFatigueTimer = 0.0F;
        float blinkAttackWindupTimer = 0.0F;
        glm::vec3 blinkStartPosition{0.0F};
        glm::vec3 blinkTargetPosition{0.0F};
        glm::vec3 blinkTravelDirection{0.0F};
        bool blinkAttackInProgress = false;
        bool blinkRequiresRelease = false;
        bool blinkIsChainCharge = false;  // True if this charge started during chain window
    };

    SurvivorItemRuntimeState m_survivorItemState{};
    KillerPowerRuntimeState m_killerPowerState{};
    std::unordered_map<engine::scene::Entity, float> m_mapRevealGenerators;
    std::string m_trapIndicatorText;
    float m_trapIndicatorTimer = 0.0F;
    bool m_trapIndicatorDanger = true;
    glm::vec3 m_trapPreviewPosition{0.0F};
    glm::vec3 m_trapPreviewHalfExtents{0.36F, 0.08F, 0.36F};
    bool m_trapPreviewActive = false;
    bool m_trapPreviewValid = true;
    engine::render::Frustum m_frustum{};
    bool m_physicsDirty = false; // Set when interactions change collision geometry; triggers deferred RebuildPhysicsWorld.
    mutable std::vector<engine::physics::TriggerHit> m_triggerHitBuf; // Reusable buffer for trigger queries (avoids per-call heap alloc).
    engine::render::StaticBatcher m_staticBatcher{};

    [[nodiscard]] static engine::scene::Role OppositeRole(engine::scene::Role role);

    // Phase B2/B3: Scratch Marks and Blood Pools (Refactored for determinism)
    static constexpr int kScratchMarkPoolSize = 64;
    static constexpr int kBloodPoolPoolSize = 32;

    struct ScratchMark
    {
        glm::vec3 position{0.0F};
        glm::vec3 direction{0.0F, 0.0F, -1.0F};
        glm::vec3 perpOffset{0.0F};
        float age = 0.0F;
        float lifetime = 30.0F;
        float size = 0.35F;
        float yawDeg = 0.0F; // cached atan2(direction.x, direction.z) in degrees
        bool active = false;
    };

    struct BloodPool
    {
        glm::vec3 position{0.0F};
        float age = 0.0F;
        float lifetime = 120.0F;
        float size = 0.5F;
        bool active = false;
    };

    struct ScratchProfile
    {
        float spawnIntervalMin = 0.15F;
        float spawnIntervalMax = 0.25F;
        float lifetime = 30.0F;
        float sizeMin = 0.3F;
        float sizeMax = 0.5F;
        float jitterRadius = 0.5F;
        float minDistanceFromLast = 0.3F;
        bool allowSurvivorSeeOwn = false;
    };

    struct BloodProfile
    {
        float spawnInterval = 2.0F;
        float lifetime = 120.0F;
        float sizeMin = 0.4F;
        float sizeMax = 0.7F;
        float minDistanceFromLast = 0.5F;
        bool onlyWhenMoving = true;
        bool allowSurvivorSeeOwn = false;
    };

    struct ChainsawSprintConfig
    {
        float chargeTime = 2.5F;
        float sprintSpeedMultiplier = 2.4F;
        // REMOVED: maxSprintDuration - sprint until collision/RMB release/hit
        float turnRateDegreesPerSec = 90.0F;
        float recoveryDuration = 1.5F;           // Default recovery (for RMB release/hit)
        float collisionRecoveryDuration = 2.5F;  // Wall collision recovery (longer)
        float overheatMax = 100.0F;
        float overheatPerSecondCharge = 15.0F;
        float overheatPerSecondSprint = 25.0F;
        float overheatCooldownRate = 10.0F;
        float overheatThreshold = 20.0F;
        float fovBoost = 15.0F;
        float collisionRaycastDistance = 2.0F;
        float survivorHitRadius = 1.5F;

        // New turn rate phases
        float turnBoostWindow = 0.5F;            // First 0.5s = high turn rate
        float turnBoostRate = 360.0F;            // Deg/sec during boost
        float turnRestrictedRate = 90.0F;        // Deg/sec after boost window

        // New recovery durations
        float recoveryHitDuration = 0.5F;        // Recovery after hitting survivor
        float recoveryCancelDuration = 0.5F;     // Recovery after RMB release

        // New overheat buff system (instead of locked state)
        float overheatBuffThreshold = 100.0F;    // When buffs activate
        float overheatChargeBonus = 0.2F;        // +20% charge rate when buffed
        float overheatSpeedBonus = 0.1F;         // +10% sprint speed when buffed
        float overheatTurnBonus = 0.15F;         // +15% turn rate when buffed

        // Movement during charging
        float chargeSlowdownMultiplier = 0.3F;   // Movement speed while charging (0.3 = 30% speed)
    };

    struct NurseBlinkConfig
    {
        int maxCharges = 2;
        float chargeRegenSeconds = 3.0F;
        float minBlinkDistance = 2.0F;
        float maxBlinkDistance = 20.0F;
        float chargeTimeToMax = 2.0F;
        float chargeMoveSpeedMultiplier = 0.5F;
        float blinkTravelTime = 0.15F;
        float chainWindowSeconds = 1.5F;
        float fatigueBaseSeconds = 2.0F;
        float fatiguePerBlinkUsedSeconds = 0.5F;
        float fatigueMoveSpeedMultiplier = 0.5F;
        float blinkAttackRange = 4.5F;
        float blinkAttackAngleDegrees = 90.0F;
        float blinkAttackWindupSeconds = 0.2F;
        float blinkAttackLungeMultiplier = 2.0F;
        int endpointSlideAttempts = 8;
        float endpointSlideStep = 0.3F;
    };

    void UpdateScratchMarks(float fixedDt, const glm::vec3& survivorPos, const glm::vec3& survivorForward, bool survivorSprinting);
    void UpdateBloodPools(float fixedDt, const glm::vec3& survivorPos, bool survivorInjuredOrDowned, bool survivorMoving);
    void RenderScratchMarks(engine::render::Renderer& renderer, bool localIsKiller) const;
    void RenderBloodPools(engine::render::Renderer& renderer, bool localIsKiller) const;
    void RenderHighPolyMeshes(engine::render::Renderer& renderer);
    [[nodiscard]] bool CanSeeScratchMarks(bool localIsKiller) const;
    [[nodiscard]] bool CanSeeBloodPools(bool localIsKiller) const;

    [[nodiscard]] static float DeterministicRandom(const glm::vec3& position, int seed);
    [[nodiscard]] static glm::vec3 ComputePerpendicular(const glm::vec3& forward);

    std::array<ScratchMark, kScratchMarkPoolSize> m_scratchMarks{};
    std::array<BloodPool, kBloodPoolPoolSize> m_bloodPools{};
    int m_scratchMarkHead = 0;
    int m_bloodPoolHead = 0;
    float m_scratchSpawnAccumulator = 0.0F;
    float m_scratchNextInterval = 0.2F;
    float m_bloodSpawnAccumulator = 0.0F;
    glm::vec3 m_lastScratchSpawnPos{-10000.0F, 0.0F, -10000.0F};
    glm::vec3 m_lastBloodSpawnPos{-10000.0F, 0.0F, -10000.0F};
    ScratchProfile m_scratchProfile{};
    BloodProfile m_bloodProfile{};
    bool m_scratchDebugEnabled = false;
    bool m_bloodDebugEnabled = false;

    ChainsawSprintConfig m_chainsawConfig{};
    bool m_chainsawDebugEnabled = false;

    NurseBlinkConfig m_blinkConfig{};
    bool m_blinkDebugEnabled = false;

    KillerLookLight m_killerLookLight{};
    bool m_killerLookLightDebug = false;
    std::size_t m_mapSpotLightCount = 0;

    // Cached vector to avoid per-frame heap allocation in Render().
    std::vector<engine::render::SpotLight> m_runtimeSpotLights;

    // High-poly meshes for GPU stress testing (benchmark map)
    struct HighPolyMesh
    {
        engine::render::MeshGeometry geometry;
        engine::render::MeshGeometry mediumLodGeometry;
        engine::render::Renderer::GpuMeshId gpuFullLod = engine::render::Renderer::kInvalidGpuMesh;
        engine::render::Renderer::GpuMeshId gpuMediumLod = engine::render::Renderer::kInvalidGpuMesh;
        glm::vec3 position{0.0F};
        glm::vec3 rotation{0.0F};
        glm::vec3 scale{1.0F};
        glm::vec3 color{1.0F};
        glm::vec3 halfExtents{1.0F};  // For frustum culling
    };
    std::vector<HighPolyMesh> m_highPolyMeshes;
    bool m_highPolyMeshesGenerated = false;
    bool m_highPolyMeshesUploaded = false;

    // Loop mesh rendering (custom meshes for loop elements)
    struct LoopMeshInstance
    {
        std::string meshPath;  // Path to mesh file
        engine::render::Renderer::GpuMeshId gpuMesh = engine::render::Renderer::kInvalidGpuMesh;
        glm::vec3 position{0.0F};
        float rotationDegrees = 0.0F;
        glm::vec3 halfExtents{1.0F};  // For frustum culling
        bool collisionCreated = false;  // Whether collision boxes were created
    };
    std::vector<LoopMeshInstance> m_loopMeshes;
    bool m_loopMeshesUploaded = false;
    void RenderLoopMeshes(engine::render::Renderer& renderer);

    // Animation system for locomotion (survivor)
    engine::animation::AnimationSystem m_animationSystem;
    bool m_animationDebugEnabled = false;
    std::string m_animationCharacterId;
};

} // namespace game::gameplay
