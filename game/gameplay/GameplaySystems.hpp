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
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include "engine/core/EventBus.hpp"
#include "engine/fx/FxSystem.hpp"
#include "engine/platform/ActionBindings.hpp"
#include "engine/physics/PhysicsWorld.hpp"
#include "engine/scene/World.hpp"
#include "game/gameplay/PerkSystem.hpp"
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
    float pitchDegrees = 10.0F;
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

    float playerSpeed = 0.0F;
    bool grounded = false;
    glm::vec3 velocity{0.0F};
    glm::vec3 lastCollisionNormal{0.0F, 1.0F, 0.0F};
    float penetrationDepth = 0.0F;
    std::vector<DebugActorLabel> debugActors;
    int fxActiveInstances = 0;
    int fxActiveParticles = 0;
    float fxCpuMs = 0.0F;

    // Perks debug info
    struct ActivePerkDebug
    {
        std::string id;
        std::string name;
        bool isActive = false;
        float activeRemainingSeconds = 0.0F;
        float cooldownRemainingSeconds = 0.0F;
        int stacks = 0;
    };
    std::vector<ActivePerkDebug> activePerksSurvivor;
    std::vector<ActivePerkDebug> activePerksKiller;
    float speedModifierSurvivor = 1.0F;
    float speedModifierKiller = 1.0F;
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
        CollisionTest
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

    struct Snapshot
    {
        MapType mapType = MapType::Test;
        unsigned int seed = 1337U;
        std::array<std::string, 3> survivorPerkIds = {"", "", ""};
        std::array<std::string, 3> killerPerkIds = {"", "", ""};
        ActorSnapshot survivor;
        ActorSnapshot killer;
        std::uint8_t survivorState = 0;
        std::uint8_t killerAttackState = 0;
        float killerAttackStateTimer = 0.0F;
        float killerLungeCharge = 0.0F;
        bool chaseActive = false;
        float chaseDistance = 0.0F;
        bool chaseLos = false;
        std::vector<PalletSnapshot> pallets;
    };

    GameplaySystems();

    void Initialize(engine::core::EventBus& eventBus);
    void CaptureInputFrame(const engine::platform::Input& input, const engine::platform::ActionBindings& bindings, bool controlsEnabled);
    void FixedUpdate(float fixedDt, const engine::platform::Input& input, bool controlsEnabled);
    void Update(float deltaSeconds, const engine::platform::Input& input, bool controlsEnabled);

    void Render(engine::render::Renderer& renderer) const;
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
    void SpawnWindow();
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

    // Phase B2/B3: Scratch Marks and Blood Pools debug control
    void SetScratchDebug(bool enabled);
    void SetBloodDebug(bool enabled);
    void SetScratchProfile(const std::string& profileName);
    void SetBloodProfile(const std::string& profileName);
    [[nodiscard]] int GetActiveScratchCount() const { return static_cast<int>(m_scratchMarks.size()); }
    [[nodiscard]] int GetActiveBloodPoolCount() const { return static_cast<int>(m_bloodPools.size()); }
    [[nodiscard]] bool ScratchDebugEnabled() const { return m_scratchDebugEnabled; }
    [[nodiscard]] bool BloodDebugEnabled() const { return m_bloodDebugEnabled; }

    // Phase B4: Killer Look Light control
    void SetKillerLookLightEnabled(bool enabled) { m_killerLookLight.enabled = enabled; }
    void SetKillerLookLightRange(float range) { m_killerLookLight.range = range; }
    void SetKillerLookLightIntensity(float intensity) { m_killerLookLight.intensity = intensity; }
    void SetKillerLookLightAngle(float angleDegrees) { m_killerLookLight.innerAngleDegrees = angleDegrees; }
    void SetKillerLookLightPitch(float pitchDegrees) { m_killerLookLight.pitchDegrees = pitchDegrees; }
    void SetKillerLookLightDebug(bool enabled) { m_killerLookLightDebug = enabled; }
    [[nodiscard]] bool KillerLookLightEnabled() const { return m_killerLookLight.enabled; }
    [[nodiscard]] bool KillerLookLightDebug() const { return m_killerLookLightDebug; }

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
        SelfHeal
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

    bool m_collisionEnabled = true;
    bool m_debugDrawEnabled = true;
    bool m_physicsDebugEnabled = false;
    bool m_noClipEnabled = false;
    bool m_quitRequested = false;
    bool m_networkAuthorityMode = false;
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

    [[nodiscard]] static engine::scene::Role OppositeRole(engine::scene::Role role);

    // Phase B2/B3: Scratch Marks and Blood Pools
    struct ScratchMark
    {
        glm::vec3 position{0.0F};
        glm::vec3 direction{0.0F, 0.0F, -1.0F};
        float age = 0.0F;
        float lifetime = 30.0F;
        float size = 0.35F;
    };

    struct BloodPool
    {
        glm::vec3 position{0.0F};
        float age = 0.0F;
        float lifetime = 120.0F;
        float size = 0.5F;
    };

    struct ScratchProfile
    {
        float spawnIntervalMin = 0.15F;
        float spawnIntervalMax = 0.25F;
        float lifetime = 30.0F;
        float sizeMin = 0.3F;
        float sizeMax = 0.5F;
        float jitterRadius = 0.8F;
        int maxActive = 64;
        bool allowSurvivorSeeOwn = false;
    };

    struct BloodProfile
    {
        float spawnInterval = 2.0F;
        float lifetime = 120.0F;
        float sizeMin = 0.4F;
        float sizeMax = 0.7F;
        int maxActive = 32;
        bool onlyWhenMoving = true;
        bool allowSurvivorSeeOwn = false;
    };

    void UpdateScratchMarks(float deltaSeconds, const glm::vec3& survivorPos, bool survivorSprinting);
    void UpdateBloodPools(float deltaSeconds, const glm::vec3& survivorPos, bool survivorMoving);
    void RenderScratchMarks(engine::render::Renderer& renderer, bool localIsKiller) const;
    void RenderBloodPools(engine::render::Renderer& renderer, bool localIsKiller) const;
    [[nodiscard]] bool CanSeeScratchMarks(bool localIsKiller) const;
    [[nodiscard]] bool CanSeeBloodPools(bool localIsKiller) const;

    std::vector<ScratchMark> m_scratchMarks;
    std::vector<BloodPool> m_bloodPools;
    float m_scratchSpawnAccumulator = 0.0F;
    float m_scratchNextInterval = 0.2F;
    float m_bloodSpawnAccumulator = 0.0F;
    ScratchProfile m_scratchProfile{};
    BloodProfile m_bloodProfile{};
    bool m_scratchDebugEnabled = false;
    bool m_bloodDebugEnabled = false;

    KillerLookLight m_killerLookLight{};
    bool m_killerLookLightDebug = false;
};

} // namespace game::gameplay
