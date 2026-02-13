#include "game/gameplay/GameplaySystems.hpp"
#include "game/gameplay/SpawnSystem.hpp"
#include "game/gameplay/PerkSystem.hpp"
#include "engine/scene/Components.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <iostream>
#include <limits>
#include <random>
#include <sstream>
#include <unordered_set>
#include <unordered_map>

#include <GLFW/glfw3.h>
#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "engine/platform/Input.hpp"
#include "engine/render/Renderer.hpp"
#include "engine/core/Profiler.hpp"
#include "game/editor/LevelAssets.hpp"
#include "game/maps/TileGenerator.hpp"

namespace game::gameplay
{
namespace
{
constexpr float kGravity = -20.0F;
constexpr float kPi = 3.1415926535F;

engine::scene::Entity SpawnActor(
    engine::scene::World& world,
    engine::scene::Role role,
    const glm::vec3& position,
    const glm::vec3& color
)
{
    const engine::scene::Entity entity = world.CreateEntity();

    engine::scene::Transform transform;
    transform.position = position;
    transform.rotationEuler = glm::vec3{0.0F};
    transform.scale = glm::vec3{1.0F};
    transform.forward = glm::vec3{0.0F, 0.0F, -1.0F};
    world.Transforms()[entity] = transform;

    engine::scene::ActorComponent actor;
    actor.role = role;
    if (role == engine::scene::Role::Survivor)
    {
        actor.walkSpeed = 2.85F;
        actor.sprintSpeed = 4.6F;
        actor.eyeHeight = 1.55F;
    }
    else
    {
        actor.walkSpeed = 4.6F * 1.15F;
        actor.sprintSpeed = 4.6F * 1.15F;
        actor.eyeHeight = 1.62F;
    }

    world.Actors()[entity] = actor;
    world.DebugColors()[entity] = engine::scene::DebugColorComponent{color};
    world.Names()[entity] = engine::scene::NameComponent{role == engine::scene::Role::Survivor ? "survivor" : "killer"};

    return entity;
}

glm::vec2 ReadMoveAxis(const engine::platform::Input& input, const engine::platform::ActionBindings& bindings)
{
    glm::vec2 axis{0.0F, 0.0F};

    if (bindings.IsDown(input, engine::platform::InputAction::MoveLeft))
    {
        axis.x -= 1.0F;
    }
    if (bindings.IsDown(input, engine::platform::InputAction::MoveRight))
    {
        axis.x += 1.0F;
    }
    if (bindings.IsDown(input, engine::platform::InputAction::MoveBackward))
    {
        axis.y -= 1.0F;
    }
    if (bindings.IsDown(input, engine::platform::InputAction::MoveForward))
    {
        axis.y += 1.0F;
    }

    if (glm::length(axis) > 1.0e-5F)
    {
        axis = glm::normalize(axis);
    }

    return axis;
}

std::string MapToName(GameplaySystems::MapType type)
{
    switch (type)
    {
        case GameplaySystems::MapType::Test: return "test";
        case GameplaySystems::MapType::Main: return "main";
        case GameplaySystems::MapType::CollisionTest: return "collision_test";
        default: return "unknown";
    }
}

void ItemPowerLog(const std::string& text)
{
    std::cout << "[ITEM/POWER] " << text << "\n";
}

} // namespace

const char* GameplaySystems::CameraModeToName(CameraMode mode)
{
    return mode == CameraMode::ThirdPerson ? "3rd Person" : "1st Person";
}

GameplaySystems::GameplaySystems()
    : m_rng(std::random_device{}())
{
}

void GameplaySystems::Initialize(engine::core::EventBus& eventBus)
{
    m_eventBus = &eventBus;
    m_fxSystem.Initialize("assets/fx");
    m_fxSystem.SetSpawnCallback([this](const engine::fx::FxSpawnEvent& event) {
        if (m_fxReplicationCallback)
        {
            m_fxReplicationCallback(event);
        }
    });

    m_eventBus->Subscribe("load_map", [this](const engine::core::Event& event) {
        if (!event.args.empty())
        {
            LoadMap(event.args[0]);
        }
    });

    m_eventBus->Subscribe("regen_loops", [this](const engine::core::Event& event) {
        if (event.args.empty())
        {
            RegenerateLoops();
            return;
        }

        try
        {
            RegenerateLoops(static_cast<unsigned int>(std::stoul(event.args[0])));
        }
        catch (...)
        {
            RegenerateLoops();
        }
    });
    m_eventBus->Subscribe("quit", [this](const engine::core::Event&) { RequestQuit(); });

    ApplyGameplayTuning(m_tuning);

    // Set default dev loadout for testing
    m_perkSystem.SetDefaultDevLoadout();

    // Initialize perk system active states
    m_perkSystem.InitializeActiveStates();
    InitializeLoadoutCatalog();

    BuildSceneFromMap(MapType::Test, m_generationSeed);
    AddRuntimeMessage("Press ~ for Console", 4.0F);
}

void GameplaySystems::CaptureInputFrame(
    const engine::platform::Input& input,
    const engine::platform::ActionBindings& bindings,
    bool controlsEnabled
)
{
    const engine::scene::Role localRole = ControlledSceneRole();
    const engine::scene::Role remoteRole = (localRole == engine::scene::Role::Survivor) ? engine::scene::Role::Killer : engine::scene::Role::Survivor;

    auto updateCommandForRole = [&](engine::scene::Role role, RoleCommand& command) {
        const engine::scene::Entity entity = (role == engine::scene::Role::Survivor) ? m_survivor : m_killer;
        const auto actorIt = m_world.Actors().find(entity);
        const bool actorExists = actorIt != m_world.Actors().end();

        bool inputLocked = !actorExists || !controlsEnabled;
        if (actorExists && IsActorInputLocked(actorIt->second))
        {
            inputLocked = true;
        }
        if (role == engine::scene::Role::Survivor &&
            (m_survivorState == SurvivorHealthState::Hooked ||
             m_survivorState == SurvivorHealthState::Trapped ||
             m_survivorState == SurvivorHealthState::Dead))
        {
            inputLocked = true;
        }

        if (inputLocked)
        {
            command.moveAxis = glm::vec2{0.0F};
            command.sprinting = false;
            command.crouchHeld = false;
            command.interactHeld = false;
            command.attackHeld = false;
            command.lungeHeld = false;
            command.useAltHeld = false;
            if (role == engine::scene::Role::Survivor &&
                (m_survivorState == SurvivorHealthState::Hooked || m_survivorState == SurvivorHealthState::Trapped) &&
                controlsEnabled)
            {
                const glm::vec2 mouseDelta = input.MouseDelta();
                command.lookDelta += glm::vec2{mouseDelta.x, m_invertLookY ? -mouseDelta.y : mouseDelta.y};
            }
            if (role == engine::scene::Role::Survivor && m_survivorState == SurvivorHealthState::Hooked && controlsEnabled)
            {
                command.interactPressed =
                    command.interactPressed || bindings.IsPressed(input, engine::platform::InputAction::Interact);
                command.jumpPressed = command.jumpPressed || input.IsKeyPressed(GLFW_KEY_SPACE);
            }
            if (role == engine::scene::Role::Survivor && m_survivorState == SurvivorHealthState::Trapped && controlsEnabled)
            {
                command.interactPressed =
                    command.interactPressed || bindings.IsPressed(input, engine::platform::InputAction::Interact);
            }
            if (role == engine::scene::Role::Survivor && m_survivorState == SurvivorHealthState::Carried && controlsEnabled)
            {
                command.wiggleLeftPressed =
                    command.wiggleLeftPressed || bindings.IsPressed(input, engine::platform::InputAction::MoveLeft);
                command.wiggleRightPressed =
                    command.wiggleRightPressed || bindings.IsPressed(input, engine::platform::InputAction::MoveRight);
            }
            return;
        }

        command.moveAxis = ReadMoveAxis(input, bindings);
        command.sprinting = role == engine::scene::Role::Survivor && bindings.IsDown(input, engine::platform::InputAction::Sprint);
        command.crouchHeld = bindings.IsDown(input, engine::platform::InputAction::Crouch);
        command.interactHeld = bindings.IsDown(input, engine::platform::InputAction::Interact);
        command.attackHeld = bindings.IsDown(input, engine::platform::InputAction::AttackShort) ||
                             bindings.IsDown(input, engine::platform::InputAction::AttackLunge);
        command.lungeHeld = bindings.IsDown(input, engine::platform::InputAction::AttackLunge);
        command.useAltHeld = input.IsMouseDown(GLFW_MOUSE_BUTTON_RIGHT);
        const glm::vec2 mouseDelta = input.MouseDelta();
        command.lookDelta += glm::vec2{mouseDelta.x, m_invertLookY ? -mouseDelta.y : mouseDelta.y};

        command.interactPressed = command.interactPressed || bindings.IsPressed(input, engine::platform::InputAction::Interact);
        command.jumpPressed = command.jumpPressed || input.IsKeyPressed(GLFW_KEY_SPACE);
        command.attackPressed = command.attackPressed || bindings.IsPressed(input, engine::platform::InputAction::AttackShort);
        command.attackReleased = command.attackReleased ||
                                 bindings.IsReleased(input, engine::platform::InputAction::AttackShort) ||
                                 bindings.IsReleased(input, engine::platform::InputAction::AttackLunge);
        command.useAltPressed = command.useAltPressed || input.IsMousePressed(GLFW_MOUSE_BUTTON_RIGHT);
        command.useAltReleased = command.useAltReleased || input.IsMouseReleased(GLFW_MOUSE_BUTTON_RIGHT);

        if (role == engine::scene::Role::Survivor)
        {
            command.dropItemPressed = command.dropItemPressed || input.IsKeyPressed(GLFW_KEY_R);
            command.pickupItemPressed = command.pickupItemPressed || input.IsMousePressed(GLFW_MOUSE_BUTTON_LEFT);
            command.wiggleLeftPressed = command.wiggleLeftPressed || bindings.IsPressed(input, engine::platform::InputAction::MoveLeft);
            command.wiggleRightPressed = command.wiggleRightPressed || bindings.IsPressed(input, engine::platform::InputAction::MoveRight);
        }
    };

    if (localRole == engine::scene::Role::Survivor)
    {
        updateCommandForRole(engine::scene::Role::Survivor, m_localSurvivorCommand);
        m_localKillerCommand = RoleCommand{};
    }
    else
    {
        updateCommandForRole(engine::scene::Role::Killer, m_localKillerCommand);
        m_localSurvivorCommand = RoleCommand{};
    }

    if (!m_networkAuthorityMode)
    {
        if (remoteRole == engine::scene::Role::Survivor)
        {
            m_remoteSurvivorCommand.reset();
        }
        else
        {
            m_remoteKillerCommand.reset();
        }
    }
}

void GameplaySystems::FixedUpdate(float fixedDt, const engine::platform::Input& input, bool controlsEnabled)
{
    (void)input;
    (void)controlsEnabled;

    RebuildPhysicsWorld();

    RoleCommand survivorCommand = m_localSurvivorCommand;
    RoleCommand killerCommand = m_localKillerCommand;

    if (m_networkAuthorityMode)
    {
        if (m_controlledRole == ControlledRole::Survivor)
        {
            if (m_remoteKillerCommand.has_value())
            {
                killerCommand = *m_remoteKillerCommand;
            }
        }
        else
        {
            if (m_remoteSurvivorCommand.has_value())
            {
                survivorCommand = *m_remoteSurvivorCommand;
            }
        }
    }
    else
    {
        if (m_controlledRole == ControlledRole::Survivor)
        {
            killerCommand = RoleCommand{};
        }
        else
        {
            survivorCommand = RoleCommand{};
        }
    }

    if (m_survivorHitHasteTimer > 0.0F)
    {
        m_survivorHitHasteTimer = std::max(0.0F, m_survivorHitHasteTimer - fixedDt);
    }
    if (m_killerSlowTimer > 0.0F)
    {
        m_killerSlowTimer = std::max(0.0F, m_killerSlowTimer - fixedDt);
        if (m_killerSlowTimer <= 0.0F)
        {
            m_killerSlowMultiplier = 1.0F;
        }
    }

    for (auto& [entity, actor] : m_world.Actors())
    {
        const engine::scene::Role role = actor.role;
        const RoleCommand& command = role == engine::scene::Role::Survivor ? survivorCommand : killerCommand;

        bool inputLocked = IsActorInputLocked(actor);
        if (entity == m_survivor &&
            (m_survivorState == SurvivorHealthState::Hooked ||
             m_survivorState == SurvivorHealthState::Trapped ||
             m_survivorState == SurvivorHealthState::Dead))
        {
            inputLocked = true;
        }

        const bool allowLookWhileLocked =
            entity == m_survivor &&
            (m_survivorState == SurvivorHealthState::Hooked || m_survivorState == SurvivorHealthState::Trapped);
        if ((!inputLocked || allowLookWhileLocked) && glm::length(command.lookDelta) > 1.0e-5F)
        {
            const float sensitivity = role == engine::scene::Role::Survivor ? m_survivorLookSensitivity : m_killerLookSensitivity;
            UpdateActorLook(entity, command.lookDelta, sensitivity);
        }

        const bool survivorActionLocked =
            role == engine::scene::Role::Survivor &&
            m_survivorItemState.actionLockTimer > 0.0F &&
            m_survivorState != SurvivorHealthState::Trapped &&
            m_survivorState != SurvivorHealthState::Hooked &&
            m_survivorState != SurvivorHealthState::Carried;

        const glm::vec2 axis = (inputLocked || survivorActionLocked) ? glm::vec2{0.0F} : command.moveAxis;
        const bool sprinting = (inputLocked || survivorActionLocked) ? false : command.sprinting;
        const bool jumpPressed = (inputLocked || survivorActionLocked) ? false : command.jumpPressed;

        UpdateActorMovement(entity, axis, sprinting, jumpPressed, survivorActionLocked ? false : command.crouchHeld, fixedDt);

        UpdateInteractBuffer(role, command, fixedDt);

        if (role == engine::scene::Role::Survivor)
        {
            if (m_survivorState == SurvivorHealthState::Carried && command.wiggleLeftPressed)
            {
                m_survivorWigglePressQueue.push_back(-1);
            }
            if (m_survivorState == SurvivorHealthState::Carried && command.wiggleRightPressed)
            {
                m_survivorWigglePressQueue.push_back(1);
            }
        }
    }

    UpdateCarriedSurvivor();
    UpdateCarryEscapeQte(true, fixedDt);
    UpdateHookStages(fixedDt, survivorCommand.interactPressed, survivorCommand.jumpPressed);
    const bool toolboxRepairHeld = survivorCommand.useAltHeld && m_survivorLoadout.itemId == "toolbox";
    UpdateGeneratorRepair(survivorCommand.interactHeld || toolboxRepairHeld, survivorCommand.jumpPressed, fixedDt);
    UpdateSelfHeal(survivorCommand.interactHeld, survivorCommand.jumpPressed, fixedDt);
    UpdateSurvivorItemSystem(survivorCommand, fixedDt);
    UpdateKillerPowerSystem(killerCommand, fixedDt);
    UpdateBearTrapSystem(survivorCommand, killerCommand, fixedDt);

    const InteractionCandidate survivorCandidate = ResolveInteractionCandidateFromView(m_survivor);
    if (survivorCandidate.type != InteractionType::None && ConsumeInteractBuffered(engine::scene::Role::Survivor))
    {
        ExecuteInteractionForRole(m_survivor, survivorCandidate);
        m_physicsDirty = true;
    }
    const InteractionCandidate killerCandidate = ResolveInteractionCandidateFromView(m_killer);
    if (killerCandidate.type != InteractionType::None && ConsumeInteractBuffered(engine::scene::Role::Killer))
    {
        ExecuteInteractionForRole(m_killer, killerCandidate);
        m_physicsDirty = true;
    }

    UpdateKillerAttack(killerCommand, fixedDt);

    UpdatePalletBreak(fixedDt);

    if (m_physicsDirty)
    {
        RebuildPhysicsWorld();
        m_physicsDirty = false;
        // Physics changed — re-resolve interaction candidate for prompt display.
        UpdateInteractionCandidate();
    }
    else
    {
        // Physics unchanged — reuse already-resolved candidate for prompt display.
        const engine::scene::Entity controlled = ControlledEntity();
        const auto actorIt = m_world.Actors().find(controlled);
        const bool inputLocked = (controlled == 0 || actorIt == m_world.Actors().end() || IsActorInputLocked(actorIt->second));
        const bool downed = (controlled == m_survivor &&
            (m_survivorState == SurvivorHealthState::Downed ||
             m_survivorState == SurvivorHealthState::Trapped ||
             m_survivorState == SurvivorHealthState::Hooked ||
             m_survivorState == SurvivorHealthState::Dead));

        if (inputLocked || downed)
        {
            m_interactionCandidate = InteractionCandidate{};
            m_interactionPromptHoldSeconds = 0.0F;
        }
        else
        {
            const InteractionCandidate& resolved = (controlled == m_survivor) ? survivorCandidate : killerCandidate;
            if (resolved.type != InteractionType::None)
            {
                m_interactionCandidate = resolved;
                m_interactionPromptHoldSeconds = 0.2F;
            }
            else if (m_interactionPromptHoldSeconds > 0.0F && !m_interactionCandidate.prompt.empty())
            {
                m_interactionPromptHoldSeconds = std::max(0.0F, m_interactionPromptHoldSeconds - (1.0F / 60.0F));
            }
            else
            {
                m_interactionCandidate = InteractionCandidate{};
                m_interactionPromptHoldSeconds = 0.0F;
            }
        }
    }
    UpdateChaseState(fixedDt);
    UpdateBloodlust(fixedDt);

    const auto survivorTransformIt = m_world.Transforms().find(m_survivor);
    if (survivorTransformIt != m_world.Transforms().end())
    {
        const glm::vec3 survivorPos = survivorTransformIt->second.position;
        const glm::vec3 survivorForward = survivorTransformIt->second.forward;
        const auto survivorActorIt = m_world.Actors().find(m_survivor);

        bool survivorSprinting = false;
        bool survivorMoving = false;

        if (survivorActorIt != m_world.Actors().end())
        {
            const RoleCommand* command = nullptr;
            if (m_controlledRole == ControlledRole::Survivor)
            {
                command = &m_localSurvivorCommand;
            }
            else if (m_remoteSurvivorCommand.has_value())
            {
                command = &m_remoteSurvivorCommand.value();
            }

            if (command != nullptr)
            {
                survivorSprinting = command->sprinting;
                survivorMoving = glm::length(command->moveAxis) > 0.1F;
            }
        }

        const bool survivorInjuredOrDowned = (m_survivorState == SurvivorHealthState::Injured ||
                                               m_survivorState == SurvivorHealthState::Downed);

        UpdateScratchMarks(fixedDt, survivorPos, survivorForward, survivorSprinting);
        UpdateBloodPools(fixedDt, survivorPos, survivorInjuredOrDowned, survivorMoving);
    }

    m_localSurvivorCommand.lookDelta = glm::vec2{0.0F};
    m_localSurvivorCommand.interactPressed = false;
    m_localSurvivorCommand.jumpPressed = false;
    m_localSurvivorCommand.attackPressed = false;
    m_localSurvivorCommand.attackReleased = false;
    m_localSurvivorCommand.useAltPressed = false;
    m_localSurvivorCommand.useAltReleased = false;
    m_localSurvivorCommand.dropItemPressed = false;
    m_localSurvivorCommand.pickupItemPressed = false;
    m_localSurvivorCommand.wiggleLeftPressed = false;
    m_localSurvivorCommand.wiggleRightPressed = false;

    m_localKillerCommand.lookDelta = glm::vec2{0.0F};
    m_localKillerCommand.interactPressed = false;
    m_localKillerCommand.jumpPressed = false;
    m_localKillerCommand.attackPressed = false;
    m_localKillerCommand.attackReleased = false;
    m_localKillerCommand.useAltPressed = false;
    m_localKillerCommand.useAltReleased = false;
    m_localKillerCommand.wiggleLeftPressed = false;
    m_localKillerCommand.wiggleRightPressed = false;

    if (m_remoteSurvivorCommand.has_value())
    {
        m_remoteSurvivorCommand->lookDelta = glm::vec2{0.0F};
        m_remoteSurvivorCommand->interactPressed = false;
        m_remoteSurvivorCommand->jumpPressed = false;
        m_remoteSurvivorCommand->attackPressed = false;
        m_remoteSurvivorCommand->attackReleased = false;
        m_remoteSurvivorCommand->useAltPressed = false;
        m_remoteSurvivorCommand->useAltReleased = false;
        m_remoteSurvivorCommand->dropItemPressed = false;
        m_remoteSurvivorCommand->pickupItemPressed = false;
        m_remoteSurvivorCommand->wiggleLeftPressed = false;
        m_remoteSurvivorCommand->wiggleRightPressed = false;
    }
    if (m_remoteKillerCommand.has_value())
    {
        m_remoteKillerCommand->lookDelta = glm::vec2{0.0F};
        m_remoteKillerCommand->interactPressed = false;
        m_remoteKillerCommand->jumpPressed = false;
        m_remoteKillerCommand->attackPressed = false;
        m_remoteKillerCommand->attackReleased = false;
        m_remoteKillerCommand->useAltPressed = false;
        m_remoteKillerCommand->useAltReleased = false;
        m_remoteKillerCommand->wiggleLeftPressed = false;
        m_remoteKillerCommand->wiggleRightPressed = false;
    }
}

void GameplaySystems::Update(float deltaSeconds, const engine::platform::Input& input, bool controlsEnabled)
{
    (void)input;
    (void)controlsEnabled;
    m_elapsedSeconds += deltaSeconds;

    // Update perk system (cooldowns, active durations)
    m_perkSystem.UpdateActiveStates(deltaSeconds);

    for (auto it = m_messages.begin(); it != m_messages.end();)
    {
        it->ttl -= deltaSeconds;
        if (it->ttl <= 0.0F)
        {
            it = m_messages.erase(it);
        }
        else
        {
            ++it;
        }
    }

    m_lastSwingDebugTtl = std::max(0.0F, m_lastSwingDebugTtl - deltaSeconds);
    m_killerAttackFlashTtl = std::max(0.0F, m_killerAttackFlashTtl - deltaSeconds);
    m_trapIndicatorTimer = std::max(0.0F, m_trapIndicatorTimer - deltaSeconds);

    m_fxSystem.Update(deltaSeconds, m_cameraPosition);
    UpdateCamera(deltaSeconds);
}

void GameplaySystems::Render(engine::render::Renderer& renderer, float aspectRatio)
{
    const glm::mat4 viewProjection = BuildViewProjection(aspectRatio);
    m_frustum.Extract(viewProjection);

    glm::vec3 postFxColor = m_fxSystem.PostFxPulseColor();
    float postFxIntensity = m_fxSystem.PostFxPulseIntensity();
    if (m_controlledRole == ControlledRole::Killer && m_killerPowerState.killerBlindTimer > 0.0F)
    {
        const float blind01 = glm::clamp(
            m_killerPowerState.killerBlindTimer / std::max(0.05F, m_tuning.flashlightBlindDurationSeconds),
            0.0F,
            1.0F
        );
        const bool whiteStyle = m_tuning.flashlightBlindStyle == 0;
        postFxColor = whiteStyle ? glm::vec3{1.0F, 1.0F, 1.0F} : glm::vec3{-1.0F, -1.0F, -1.0F};
        postFxIntensity = std::max(postFxIntensity, blind01 * (whiteStyle ? 1.25F : 1.0F));
    }
    renderer.SetPostFxPulse(postFxColor, postFxIntensity);

    // Dynamic spot lights (keep map lights, then append runtime lights).
    // Re-use m_runtimeSpotLights to avoid per-frame heap allocation.
    m_runtimeSpotLights.clear();
    const auto& baseSpotLights = renderer.GetSpotLights();
    const std::size_t mapCount = std::min(m_mapSpotLightCount, baseSpotLights.size());
    m_runtimeSpotLights.assign(baseSpotLights.begin(), baseSpotLights.begin() + static_cast<std::ptrdiff_t>(mapCount));
    auto& spotLights = m_runtimeSpotLights;

    // Phase B4: Killer Look Light (spot cone).
    const auto killerTransIt = m_world.Transforms().find(m_killer);
    const bool isLocalKiller = m_controlledRole == ControlledRole::Killer;
    if (m_killer != 0 && killerTransIt != m_world.Transforms().end() && m_killerLookLight.enabled && !isLocalKiller)
    {
        const glm::vec3 killerPos = killerTransIt->second.position;
        const glm::vec3 killerForward = killerTransIt->second.forward;
        const glm::vec3 flatForward = glm::length(glm::vec3{killerForward.x, 0.0F, killerForward.z}) > 0.001F
                                          ? glm::normalize(glm::vec3{killerForward.x, 0.0F, killerForward.z})
                                          : glm::vec3{0.0F, 0.0F, -1.0F};
        const glm::vec3 lightPos = killerPos + glm::vec3{0.0F, 0.8F, 0.0F} + flatForward * 0.3F;
        const float pitchRad = glm::radians(m_killerLookLight.pitchDegrees);
        const glm::vec3 lightDir = glm::normalize(flatForward * glm::cos(pitchRad) - glm::vec3{0.0F, 1.0F, 0.0F} * glm::sin(pitchRad));

        spotLights.push_back({
            lightPos,
            lightDir,
            m_killerLookLight.color,
            m_killerLookLight.intensity,
            m_killerLookLight.range,
            glm::cos(glm::radians(m_killerLookLight.innerAngleDegrees * 0.5F)),
            glm::cos(glm::radians(m_killerLookLight.outerAngleDegrees * 0.5F))
        });

        if (m_killerLookLightDebug)
        {
            const float coneLength = m_killerLookLight.range;
            const float coneRadius = coneLength * glm::tan(glm::radians(m_killerLookLight.outerAngleDegrees * 0.5F));
            const int segments = 8;
            const float angleStep = glm::two_pi<float>() / static_cast<float>(segments);

            for (int i = 0; i < segments; ++i)
            {
                const float theta1 = static_cast<float>(i) * angleStep;
                const float theta2 = static_cast<float>(i + 1) * angleStep;
                const glm::vec3 offset = glm::vec3{
                    glm::sin(theta1) * coneRadius * 0.5F,
                    0.0F,
                    glm::cos(theta1) * coneRadius * 0.5F
                } + lightPos;
                const glm::vec3 offset2 = glm::vec3{
                    glm::sin(theta2) * coneRadius * 0.5F,
                    0.0F,
                    glm::cos(theta2) * coneRadius * 0.5F
                } + lightPos;
                renderer.DrawLine(offset, offset2, m_killerLookLight.color);
            }

            const glm::vec3 tipPos = lightPos + lightDir * coneLength;
            renderer.DrawLine(lightPos, tipPos, m_killerLookLight.color * 0.5F);
        }
    }

    // Flashlight runtime light (survivor RMB item).
    const bool flashlightActive = m_survivor != 0 &&
                                  m_survivorLoadout.itemId == "flashlight" &&
                                  m_survivorItemState.active &&
                                  m_survivorItemState.charges > 0.0F;
    const auto survivorTransformIt = m_world.Transforms().find(m_survivor);
    const auto survivorActorIt = m_world.Actors().find(m_survivor);
    if (flashlightActive && survivorTransformIt != m_world.Transforms().end() && survivorActorIt != m_world.Actors().end())
    {
        const engine::scene::Transform& survivorTransform = survivorTransformIt->second;
        const engine::scene::ActorComponent& survivorActor = survivorActorIt->second;
        const float eyeOffset = survivorActor.eyeHeight - survivorActor.capsuleHeight * 0.5F;
        const glm::vec3 origin = survivorTransform.position + glm::vec3{0.0F, eyeOffset, 0.0F} + survivorTransform.forward * 0.24F;
        const glm::vec3 direction = glm::length(survivorTransform.forward) > 1.0e-5F
                                        ? glm::normalize(survivorTransform.forward)
                                        : glm::vec3{0.0F, 0.0F, -1.0F};

        float beamRange = std::max(2.0F, m_tuning.flashlightBeamRange);
        float beamAngle = std::max(5.0F, m_tuning.flashlightBeamAngleDegrees);
        float blindNeed = std::max(0.25F, m_tuning.flashlightBlindBuildSeconds);
        if (const loadout::ItemDefinition* itemDef = m_loadoutCatalog.FindItem("flashlight"))
        {
            const auto findParam = [&](const char* key, float fallback) {
                const auto it = itemDef->params.find(key);
                return it != itemDef->params.end() ? it->second : fallback;
            };
            beamRange = std::max(2.0F, m_survivorItemModifiers.ApplyStat("beam_range", findParam("beam_range", beamRange)));
            beamAngle = std::max(5.0F, m_survivorItemModifiers.ApplyStat("beam_angle_deg", findParam("beam_angle_deg", beamAngle)));
            blindNeed = std::max(0.25F, m_survivorItemModifiers.ApplyStat("blind_time_required", findParam("blind_time_required", blindNeed)));
        }
        const float blindBuild01 = glm::clamp(m_survivorItemState.flashBlindAccum / std::max(0.05F, blindNeed), 0.0F, 1.0F);
        const float innerCoreAngle = glm::mix(std::max(4.0F, beamAngle * 0.45F), std::max(2.0F, beamAngle * 0.09F), blindBuild01);
        const float outerBeamAngle = std::max(beamAngle, innerCoreAngle + 2.0F);
        spotLights.push_back({
            origin,
            direction,
            glm::vec3{1.0F, 0.94F, 0.62F},
            4.6F,
            beamRange,
            glm::cos(glm::radians(innerCoreAngle * 0.65F)),
            glm::cos(glm::radians(outerBeamAngle * 0.6F))
        });
        spotLights.push_back({
            origin,
            direction,
            glm::vec3{1.0F, 0.98F, 0.7F},
            6.0F + blindBuild01 * 2.2F,
            beamRange * 0.92F,
            glm::cos(glm::radians(innerCoreAngle * 0.5F)),
            glm::cos(glm::radians(std::max(innerCoreAngle * 0.9F, innerCoreAngle + 1.0F)))
        });

        if (m_survivorItemState.flashlightSuccessFlashTimer > 0.0F)
        {
            const float flash01 = glm::clamp(m_survivorItemState.flashlightSuccessFlashTimer / 0.18F, 0.0F, 1.0F);
            spotLights.push_back({
                origin,
                direction,
                glm::vec3{1.0F, 1.0F, 0.88F},
                12.0F * flash01,
                beamRange * 0.72F,
                glm::cos(glm::radians(1.5F)),
                glm::cos(glm::radians(6.0F))
            });
        }

        renderer.DrawLine(origin, origin + direction * std::min(beamRange, 4.0F), glm::vec3{1.0F, 0.95F, 0.45F});
    }

    renderer.SetSpotLights(std::move(m_runtimeSpotLights));

    renderer.DrawGrid(60, 1.0F, glm::vec3{0.24F, 0.24F, 0.24F}, glm::vec3{0.11F, 0.11F, 0.11F}, glm::vec4{0.09F, 0.11F, 0.13F, 1.0F});

    renderer.DrawLine(glm::vec3{0.0F}, glm::vec3{2.0F, 0.0F, 0.0F}, glm::vec3{1.0F, 0.2F, 0.2F});
    renderer.DrawLine(glm::vec3{0.0F}, glm::vec3{0.0F, 2.0F, 0.0F}, glm::vec3{0.2F, 1.0F, 0.2F});
    renderer.DrawLine(glm::vec3{0.0F}, glm::vec3{0.0F, 0.0F, 2.0F}, glm::vec3{0.2F, 0.4F, 1.0F});

    const auto& transforms = m_world.Transforms();

    // Dynamic object frustum culling counters.
    std::uint32_t dynamicDrawn = 0;
    std::uint32_t dynamicCulled = 0;

    // Helper: test if an AABB at (center ± halfExtents) is inside frustum.
    const auto isVisible = [this](const glm::vec3& center, const glm::vec3& halfExtents) -> bool {
        return m_frustum.IntersectsAABB(center - halfExtents, center + halfExtents);
    };

    if (m_staticBatcher.IsBuilt())
    {
        m_staticBatcher.Render(
            viewProjection,
            m_frustum,
            renderer.GetSolidShaderProgram(),
            renderer.GetSolidViewProjLocation()
        );
    }

    for (const auto& [entity, window] : m_world.Windows())
    {
        const auto transformIt = transforms.find(entity);
        if (transformIt == transforms.end())
        {
            continue;
        }

        if (!isVisible(transformIt->second.position, window.halfExtents))
        {
            ++dynamicCulled;
            continue;
        }
        ++dynamicDrawn;

        renderer.DrawBox(transformIt->second.position, window.halfExtents, glm::vec3{0.1F, 0.75F, 0.84F});
        if (m_debugDrawEnabled)
        {
            renderer.DrawLine(
                transformIt->second.position,
                transformIt->second.position + window.normal * 1.5F,
                glm::vec3{0.2F, 1.0F, 1.0F}
            );
        }
    }

    for (const auto& [entity, pallet] : m_world.Pallets())
    {
        const auto transformIt = transforms.find(entity);
        if (transformIt == transforms.end())
        {
            continue;
        }

        if (!isVisible(transformIt->second.position, pallet.halfExtents))
        {
            ++dynamicCulled;
            continue;
        }
        ++dynamicDrawn;

        glm::vec3 color{0.8F, 0.5F, 0.2F};
        if (pallet.state == engine::scene::PalletState::Dropped)
        {
            color = glm::vec3{0.95F, 0.2F, 0.2F};
        }
        else if (pallet.state == engine::scene::PalletState::Broken)
        {
            color = glm::vec3{0.35F, 0.2F, 0.1F};
        }

        renderer.DrawBox(transformIt->second.position, pallet.halfExtents, color);
    }

    for (const auto& [entity, hook] : m_world.Hooks())
    {
        const auto transformIt = transforms.find(entity);
        if (transformIt == transforms.end())
        {
            continue;
        }

        if (!isVisible(transformIt->second.position, hook.halfExtents))
        {
            ++dynamicCulled;
            continue;
        }
        ++dynamicDrawn;

        const glm::vec3 hookColor = hook.occupied ? glm::vec3{0.78F, 0.1F, 0.1F} : glm::vec3{0.9F, 0.9F, 0.12F};
        renderer.DrawBox(transformIt->second.position, hook.halfExtents, hookColor);
    }

    for (const auto& [entity, trap] : m_world.BearTraps())
    {
        const auto transformIt = transforms.find(entity);
        if (transformIt == transforms.end())
        {
            continue;
        }

        if (!isVisible(transformIt->second.position, trap.halfExtents))
        {
            ++dynamicCulled;
            continue;
        }
        ++dynamicDrawn;

        glm::vec3 color{0.72F, 0.72F, 0.75F};
        if (trap.state == engine::scene::TrapState::Triggered)
        {
            color = glm::vec3{0.95F, 0.32F, 0.25F};
        }
        else if (trap.state == engine::scene::TrapState::Disarmed)
        {
            color = glm::vec3{0.26F, 0.26F, 0.28F};
        }
        renderer.DrawBox(transformIt->second.position, trap.halfExtents, color);
        if (m_trapDebugEnabled)
        {
            renderer.DrawLine(
                transformIt->second.position,
                transformIt->second.position + glm::vec3{0.0F, 0.75F, 0.0F},
                color
            );
        }
    }

    for (const auto& [entity, groundItem] : m_world.GroundItems())
    {
        const auto transformIt = transforms.find(entity);
        if (transformIt == transforms.end())
        {
            continue;
        }

        // Small default AABB for ground items.
        const glm::vec3 itemHalf{0.3F, 0.15F, 0.3F};
        if (!isVisible(transformIt->second.position, itemHalf))
        {
            ++dynamicCulled;
            continue;
        }
        ++dynamicDrawn;

        glm::vec3 color{0.8F, 0.8F, 0.8F};
        glm::vec3 halfExtents{0.2F, 0.06F, 0.2F};
        if (groundItem.itemId == "medkit")
        {
            color = glm::vec3{0.9F, 0.2F, 0.2F};
            halfExtents = glm::vec3{0.24F, 0.08F, 0.18F};
        }
        else if (groundItem.itemId == "toolbox")
        {
            color = glm::vec3{0.2F, 0.45F, 0.95F};
            halfExtents = glm::vec3{0.25F, 0.09F, 0.18F};
        }
        else if (groundItem.itemId == "flashlight")
        {
            color = glm::vec3{0.98F, 0.88F, 0.2F};
            halfExtents = glm::vec3{0.06F, 0.06F, 0.26F};
        }
        else if (groundItem.itemId == "map")
        {
            color = glm::vec3{0.2F, 0.86F, 0.5F};
            halfExtents = glm::vec3{0.22F, 0.015F, 0.16F};
        }
        renderer.DrawBox(transformIt->second.position, halfExtents, color);
    }

    auto drawOverlayBox = [&](const glm::vec3& center, const glm::vec3& halfExtents, const glm::vec3& color) {
        const glm::vec3 c000 = center + glm::vec3{-halfExtents.x, -halfExtents.y, -halfExtents.z};
        const glm::vec3 c001 = center + glm::vec3{-halfExtents.x, -halfExtents.y, halfExtents.z};
        const glm::vec3 c010 = center + glm::vec3{-halfExtents.x, halfExtents.y, -halfExtents.z};
        const glm::vec3 c011 = center + glm::vec3{-halfExtents.x, halfExtents.y, halfExtents.z};
        const glm::vec3 c100 = center + glm::vec3{halfExtents.x, -halfExtents.y, -halfExtents.z};
        const glm::vec3 c101 = center + glm::vec3{halfExtents.x, -halfExtents.y, halfExtents.z};
        const glm::vec3 c110 = center + glm::vec3{halfExtents.x, halfExtents.y, -halfExtents.z};
        const glm::vec3 c111 = center + glm::vec3{halfExtents.x, halfExtents.y, halfExtents.z};
        renderer.DrawOverlayLine(c000, c001, color);
        renderer.DrawOverlayLine(c000, c010, color);
        renderer.DrawOverlayLine(c001, c011, color);
        renderer.DrawOverlayLine(c010, c011, color);
        renderer.DrawOverlayLine(c100, c101, color);
        renderer.DrawOverlayLine(c100, c110, color);
        renderer.DrawOverlayLine(c101, c111, color);
        renderer.DrawOverlayLine(c110, c111, color);
        renderer.DrawOverlayLine(c000, c100, color);
        renderer.DrawOverlayLine(c001, c101, color);
        renderer.DrawOverlayLine(c010, c110, color);
        renderer.DrawOverlayLine(c011, c111, color);
    };

    if (m_trapPreviewActive && m_killerLoadout.powerId == "bear_trap")
    {
        const glm::vec3 color = m_trapPreviewValid ? glm::vec3{0.2F, 1.0F, 0.3F} : glm::vec3{1.0F, 0.25F, 0.2F};
        drawOverlayBox(
            m_trapPreviewPosition + glm::vec3{0.0F, 0.02F, 0.0F},
            m_trapPreviewHalfExtents + glm::vec3{0.02F, 0.01F, 0.02F},
            color
        );
        renderer.DrawOverlayLine(
            m_trapPreviewPosition + glm::vec3{0.0F, 0.03F, 0.0F},
            m_trapPreviewPosition + glm::vec3{0.0F, 0.78F, 0.0F},
            color
        );
    }

    for (const auto& [entity, generator] : m_world.Generators())
    {
        const auto transformIt = transforms.find(entity);
        if (transformIt == transforms.end())
        {
            continue;
        }

        if (!isVisible(transformIt->second.position, generator.halfExtents))
        {
            ++dynamicCulled;
            continue;
        }
        ++dynamicDrawn;

        // Green color scheme for generators
        glm::vec3 generatorColor{0.2F, 0.8F, 0.2F};  // Standard green
        if (generator.completed)
        {
            generatorColor = glm::vec3{0.0F, 0.5F, 0.0F};  // Dark green
        }
        else if (entity == m_activeRepairGenerator)
        {
            generatorColor = glm::vec3{0.4F, 1.0F, 0.4F};  // Bright green
        }

        renderer.DrawBox(transformIt->second.position, generator.halfExtents, generatorColor);

        const auto revealIt = m_mapRevealGenerators.find(entity);
        if (revealIt != m_mapRevealGenerators.end() && revealIt->second > 0.0F)
        {
            const float alpha = glm::clamp(revealIt->second / std::max(0.01F, m_tuning.mapRevealDurationSeconds), 0.0F, 1.0F);
            const glm::vec3 auraColor = glm::vec3{0.35F, 0.95F, 1.0F} * (0.5F + 0.5F * alpha);
            drawOverlayBox(transformIt->second.position, generator.halfExtents + glm::vec3{0.12F}, auraColor);
        }
    }

    for (const auto& [entity, actor] : m_world.Actors())
    {
        const auto transformIt = transforms.find(entity);
        if (transformIt == transforms.end())
        {
            continue;
        }

        // Frustum cull actors using capsule bounding box.
        const glm::vec3 actorHalf{actor.capsuleRadius, actor.capsuleHeight * 0.5F, actor.capsuleRadius};
        if (!isVisible(transformIt->second.position, actorHalf))
        {
            ++dynamicCulled;
            continue;
        }
        ++dynamicDrawn;

        const bool hideKillerBodyInFp =
            entity == m_killer &&
            m_controlledRole == ControlledRole::Killer &&
            ResolveCameraMode() == CameraMode::FirstPerson;
        if (hideKillerBodyInFp)
        {
            continue;
        }

        glm::vec3 color = glm::vec3{0.95F, 0.2F, 0.2F};
        if (actor.role == engine::scene::Role::Survivor)
        {
            switch (m_survivorState)
            {
                case SurvivorHealthState::Healthy: color = glm::vec3{0.2F, 0.95F, 0.2F}; break;
                case SurvivorHealthState::Injured: color = glm::vec3{1.0F, 0.58F, 0.15F}; break;
                case SurvivorHealthState::Downed: color = glm::vec3{0.95F, 0.15F, 0.15F}; break;
                case SurvivorHealthState::Trapped: color = glm::vec3{0.93F, 0.85F, 0.2F}; break;
                case SurvivorHealthState::Carried: color = glm::vec3{0.72F, 0.24F, 0.95F}; break;
                case SurvivorHealthState::Hooked: color = glm::vec3{0.85F, 0.1F, 0.1F}; break;
                case SurvivorHealthState::Dead: color = glm::vec3{0.2F, 0.2F, 0.2F}; break;
                default: break;
            }
        }
        else if (actor.role == engine::scene::Role::Killer &&
                 m_killerLoadout.powerId == "wraith_cloak" &&
                 (m_killerPowerState.wraithCloaked || m_killerPowerState.wraithCloakTransition))
        {
            // Nearly invisible silhouette while cloaked (no alpha path in capsule renderer yet).
            color = glm::vec3{0.13F, 0.15F, 0.17F};
        }

        const float visualHeightScale =
            actor.crawling ? 0.5F :
            (actor.crouching ? 0.72F : 1.0F);
        renderer.DrawCapsule(
            transformIt->second.position,
            actor.capsuleHeight * visualHeightScale,
            actor.capsuleRadius,
            color
        );

        if (m_debugDrawEnabled)
        {
            renderer.DrawLine(
                transformIt->second.position,
                transformIt->second.position + transformIt->second.forward * 1.4F,
                color
            );
        }
    }

    const bool showFpWeapon = m_controlledRole == ControlledRole::Killer && ResolveCameraMode() == CameraMode::FirstPerson;
    const auto killerTransformIt = transforms.find(m_killer);
    if (showFpWeapon && m_cameraInitialized && killerTransformIt != transforms.end())
    {
        const engine::scene::Transform& killerTransform = killerTransformIt->second;
        const float killerYaw = killerTransform.rotationEuler.y;
        const float killerPitch = killerTransform.rotationEuler.x;

        glm::vec3 forward = ForwardFromYawPitch(killerYaw, killerPitch);
        if (glm::length(forward) < 1.0e-5F)
        {
            forward = glm::vec3{0.0F, 0.0F, -1.0F};
        }
        forward = glm::normalize(forward);

        glm::vec3 right = glm::cross(forward, glm::vec3{0.0F, 1.0F, 0.0F});
        if (glm::length(right) < 1.0e-5F)
        {
            right = glm::vec3{1.0F, 0.0F, 0.0F};
        }
        right = glm::normalize(right);
        const glm::vec3 up = glm::normalize(glm::cross(right, forward));

        float attackForwardOffset = 0.0F;
        float attackUpOffset = 0.0F;
        float attackSideOffset = 0.0F;
        float attackRollDegrees = 0.0F;
        if (m_killerAttackState == KillerAttackState::ChargingLunge)
        {
            const float charge01 = glm::clamp(m_killerLungeChargeSeconds / std::max(0.01F, m_killerLungeChargeMaxSeconds), 0.0F, 1.0F);
            attackForwardOffset = -0.03F * charge01;
            attackUpOffset = -0.03F * charge01;
            attackSideOffset = -0.02F * charge01;
            attackRollDegrees = -8.0F * charge01;
        }
        else if (m_killerAttackState == KillerAttackState::Lunging)
        {
            attackForwardOffset = 0.18F;
            attackUpOffset = -0.08F;
            attackSideOffset = 0.02F;
            attackRollDegrees = 18.0F;
        }
        else if (m_killerAttackState == KillerAttackState::Recovering)
        {
            attackForwardOffset = -0.04F;
            attackUpOffset = -0.05F;
            attackSideOffset = -0.01F;
            attackRollDegrees = -10.0F;
        }

        const float sideOffset = 0.23F;
        const float forwardOffset = 0.42F;
        const float downOffset = -0.22F;
        const glm::vec3 weaponCenter =
            m_cameraPosition +
            forward * (forwardOffset + attackForwardOffset) +
            right * (sideOffset + attackSideOffset) +
            up * (downOffset + attackUpOffset);

        const glm::vec3 weaponRotationDegrees{
            glm::degrees(killerPitch) - 12.0F,
            180.0F - glm::degrees(killerYaw),
            28.0F + attackRollDegrees,
        };
        renderer.DrawOrientedBox(
            weaponCenter,
            glm::vec3{0.07F, 0.05F, 0.24F},
            weaponRotationDegrees,
            glm::vec3{0.18F, 0.18F, 0.18F}
        );
    }

    if (m_terrorRadiusVisible && m_killer != 0)
    {
        const auto killerTransformIt = transforms.find(m_killer);
        if (killerTransformIt != transforms.end())
        {
            const float perkModifier = m_perkSystem.GetTerrorRadiusModifier(engine::scene::Role::Killer);
            const float baseRadius = m_chase.isChasing ? m_terrorRadiusChaseMeters : m_terrorRadiusMeters;
            const float radius = baseRadius + perkModifier;
            const glm::vec3 center = killerTransformIt->second.position + glm::vec3{0.0F, 0.06F, 0.0F};
            const glm::vec3 trColor = m_chase.isChasing ? glm::vec3{1.0F, 0.2F, 0.2F} : glm::vec3{1.0F, 0.5F, 0.15F};
            renderer.DrawCircle(center, radius, 48, trColor, true);
        }
    }

    if (m_debugDrawEnabled)
    {
        if (m_killer != 0)
        {
            const auto killerTransformIt = transforms.find(m_killer);
            if (killerTransformIt != transforms.end())
            {
                const glm::vec3 origin = killerTransformIt->second.position + glm::vec3{0.0F, 0.08F, 0.0F};
                const glm::vec3 forward = glm::length(glm::vec3{killerTransformIt->second.forward.x, 0.0F, killerTransformIt->second.forward.z}) > 1.0e-5F
                                              ? glm::normalize(glm::vec3{killerTransformIt->second.forward.x, 0.0F, killerTransformIt->second.forward.z})
                                              : glm::vec3{0.0F, 0.0F, -1.0F};
                const float range = (m_killerAttackState == KillerAttackState::Lunging) ? m_killerLungeRange : m_killerShortRange;
                const float halfAngle = (m_killerAttackState == KillerAttackState::Lunging) ? m_killerLungeHalfAngleRadians : m_killerShortHalfAngleRadians;

                const glm::vec3 leftDir = glm::normalize(glm::vec3{
                    forward.x * std::cos(halfAngle) - forward.z * std::sin(halfAngle),
                    0.0F,
                    forward.x * std::sin(halfAngle) + forward.z * std::cos(halfAngle)
                });
                const glm::vec3 rightDir = glm::normalize(glm::vec3{
                    forward.x * std::cos(-halfAngle) - forward.z * std::sin(-halfAngle),
                    0.0F,
                    forward.x * std::sin(-halfAngle) + forward.z * std::cos(-halfAngle)
                });

                glm::vec3 wedgeColor = glm::vec3{0.95F, 0.95F, 0.2F};
                if (m_killerAttackState == KillerAttackState::ChargingLunge)
                {
                    wedgeColor = glm::vec3{1.0F, 0.55F, 0.15F};
                }
                else if (m_killerAttackState == KillerAttackState::Lunging)
                {
                    wedgeColor = glm::vec3{1.0F, 0.2F, 0.2F};
                }
                if (m_killerAttackFlashTtl > 0.0F)
                {
                    wedgeColor = glm::vec3{1.0F, 1.0F, 1.0F};
                }

                const glm::vec3 leftPoint = origin + leftDir * range;
                const glm::vec3 rightPoint = origin + rightDir * range;
                renderer.DrawOverlayLine(origin, leftPoint, wedgeColor);
                renderer.DrawOverlayLine(origin, rightPoint, wedgeColor);
                renderer.DrawOverlayLine(leftPoint, rightPoint, wedgeColor);
            }
        }

        for (const engine::physics::SolidBox& solid : m_physics.Solids())
        {
            renderer.DrawBox(solid.center, solid.halfExtents, glm::vec3{0.9F, 0.4F, 0.85F});
        }

        for (const engine::physics::TriggerVolume& trigger : m_physics.Triggers())
        {
            glm::vec3 triggerColor{0.2F, 0.6F, 1.0F};
            if (trigger.kind == engine::physics::TriggerKind::Interaction)
            {
                // Check if this trigger belongs to a generator
                const auto isGenerator = m_world.Generators().contains(trigger.entity);
                triggerColor = isGenerator ? glm::vec3{0.2F, 0.8F, 0.2F} : glm::vec3{1.0F, 0.8F, 0.2F};
            }
            else if (trigger.kind == engine::physics::TriggerKind::Chase)
            {
                triggerColor = glm::vec3{1.0F, 0.2F, 0.2F};
            }
            renderer.DrawBox(trigger.center, trigger.halfExtents, triggerColor);
        }

        for (const LoopDebugTile& tile : m_loopDebugTiles)
        {
            glm::vec3 color{0.3F, 0.3F, 0.3F};
            switch (tile.archetype)
            {
                case 0: color = glm::vec3{0.85F, 0.55F, 0.25F}; break; // JungleGymLong
                case 1: color = glm::vec3{0.2F, 0.7F, 0.95F}; break;   // JungleGymShort
                case 2: color = glm::vec3{0.95F, 0.3F, 0.5F}; break;   // LT Walls
                case 3: color = glm::vec3{0.35F, 0.95F, 0.35F}; break; // Shack
                case 4: color = glm::vec3{1.0F, 0.85F, 0.2F}; break;   // FourLane
                case 5: color = glm::vec3{0.55F, 0.55F, 0.55F}; break; // FillerA
                case 6: color = glm::vec3{0.5F, 0.5F, 0.5F}; break;    // FillerB
                default: break;
            }

            const glm::vec3 center = tile.center + glm::vec3{0.0F, 0.03F, 0.0F};
            renderer.DrawBox(center, tile.halfExtents, color);
            renderer.DrawLine(center, center + glm::vec3{0.0F, 0.9F, 0.0F}, color);
        }

        if (m_survivor != 0 && m_killer != 0)
        {
            const auto survivorIt = transforms.find(m_survivor);
            const auto killerIt = transforms.find(m_killer);
            if (survivorIt != transforms.end() && killerIt != transforms.end())
            {
                const glm::vec3 losColor = m_chase.hasLineOfSight ? glm::vec3{0.1F, 1.0F, 0.2F} : glm::vec3{1.0F, 0.1F, 0.1F};
                renderer.DrawLine(killerIt->second.position, survivorIt->second.position, losColor);
            }
        }

        const glm::vec3 hitColor = m_lastHitConnected ? glm::vec3{1.0F, 0.2F, 0.2F} : glm::vec3{1.0F, 1.0F, 0.2F};
        renderer.DrawLine(m_lastHitRayStart, m_lastHitRayEnd, hitColor);

        if (m_lastSwingDebugTtl > 0.0F && m_lastSwingRange > 0.01F)
        {
            const glm::vec3 dir = glm::length(m_lastSwingDirection) > 1.0e-5F
                                      ? glm::normalize(m_lastSwingDirection)
                                      : glm::vec3{0.0F, 0.0F, -1.0F};
            glm::vec3 right = glm::cross(dir, glm::vec3{0.0F, 1.0F, 0.0F});
            if (glm::length(right) < 1.0e-5F)
            {
                right = glm::cross(dir, glm::vec3{1.0F, 0.0F, 0.0F});
            }
            right = glm::normalize(right);
            const glm::vec3 up = glm::normalize(glm::cross(right, dir));

            const float radiusAtEnd = std::tan(m_lastSwingHalfAngleRadians) * m_lastSwingRange;
            const glm::vec3 endCenter = m_lastSwingOrigin + dir * m_lastSwingRange;
            renderer.DrawLine(m_lastSwingOrigin, endCenter, hitColor);

            constexpr int kSegments = 24;
            glm::vec3 firstPoint{0.0F};
            glm::vec3 previousPoint{0.0F};
            for (int i = 0; i <= kSegments; ++i)
            {
                const float theta = 2.0F * glm::pi<float>() * static_cast<float>(i) / static_cast<float>(kSegments);
                const glm::vec3 ringOffset = right * std::cos(theta) * radiusAtEnd + up * std::sin(theta) * radiusAtEnd;
                const glm::vec3 point = endCenter + ringOffset;
                if (i == 0)
                {
                    firstPoint = point;
                }
                else
                {
                    renderer.DrawLine(previousPoint, point, hitColor);
                }
                previousPoint = point;
            }
            renderer.DrawLine(previousPoint, firstPoint, hitColor);

            renderer.DrawLine(m_lastSwingOrigin, endCenter + right * radiusAtEnd, hitColor);
            renderer.DrawLine(m_lastSwingOrigin, endCenter - right * radiusAtEnd, hitColor);
            renderer.DrawLine(m_lastSwingOrigin, endCenter + up * radiusAtEnd, hitColor);
            renderer.DrawLine(m_lastSwingOrigin, endCenter - up * radiusAtEnd, hitColor);
        }
    }

    m_fxSystem.Render(renderer, m_cameraPosition);

    // Phase B2/B3: Render scratch marks and blood pools (killer-only visibility)
    const bool localIsKiller = (m_controlledRole == ControlledRole::Killer);
    RenderScratchMarks(renderer, localIsKiller);
    RenderBloodPools(renderer, localIsKiller);

    // Report dynamic object culling stats to profiler.
    auto& profStats = engine::core::Profiler::Instance().StatsMut();
    profStats.dynamicObjectsDrawn = dynamicDrawn;
    profStats.dynamicObjectsCulled = dynamicCulled;
}

glm::mat4 GameplaySystems::BuildViewProjection(float aspectRatio) const
{
    const glm::mat4 view = glm::lookAt(m_cameraPosition, m_cameraTarget, glm::vec3{0.0F, 1.0F, 0.0F});
    float fovDeg = 60.0F;
    if (m_controlledRole == ControlledRole::Survivor &&
        m_survivorLoadout.itemId == "flashlight" &&
        m_survivorItemState.active &&
        m_survivorItemState.charges > 0.0F)
    {
        fovDeg = 48.0F;
    }
    const glm::mat4 projection = glm::perspective(glm::radians(fovDeg), aspectRatio > 0.0F ? aspectRatio : (16.0F / 9.0F), 0.05F, 400.0F);
    return projection * view;
}

HudState GameplaySystems::BuildHudState() const
{
    HudState hud;
    hud.survivorStates.reserve(1);
    hud.debugActors.reserve(2);
    hud.mapName = m_activeMapName;
    hud.roleName = m_controlledRole == ControlledRole::Survivor ? "Survivor" : "Killer";
    hud.cameraModeName = CameraModeToName(ResolveCameraMode());
    hud.renderModeName = m_renderModeName;
    hud.interactionPrompt = m_interactionCandidate.prompt;
    hud.interactionTypeName = m_interactionCandidate.typeName;
    hud.interactionTargetName = m_interactionCandidate.targetName;
    hud.interactionPriority = m_interactionCandidate.priority;
    hud.survivorStateName = SurvivorStateToText(m_survivorState);
    hud.survivorStates.push_back(std::string{"[S1] "} + SurvivorStateToText(m_survivorState));
    hud.generatorsCompleted = m_generatorsCompleted;
    hud.generatorsTotal = m_generatorsTotal;
    hud.repairingGenerator = m_activeRepairGenerator != 0;
    hud.selfHealing = m_selfHealActive;
    hud.selfHealProgress = m_selfHealProgress;
    hud.killerAttackStateName = KillerAttackStateToText(m_killerAttackState);
    hud.attackHint = "LMB attack / RMB power-item";
    hud.lungeCharge01 = glm::clamp(
        m_killerLungeChargeSeconds / std::max(0.01F, m_killerLungeDurationSeconds),
        0.0F,
        1.0F
    );
    hud.terrorRadiusVisible = m_terrorRadiusVisible;
    const float perkModifier = m_perkSystem.GetTerrorRadiusModifier(engine::scene::Role::Killer);
    const float baseRadius = m_chase.isChasing ? m_terrorRadiusChaseMeters : m_terrorRadiusMeters;
    hud.terrorRadiusMeters = baseRadius + perkModifier;
    if (m_activeRepairGenerator != 0)
    {
        const auto generatorIt = m_world.Generators().find(m_activeRepairGenerator);
        if (generatorIt != m_world.Generators().end())
        {
            hud.activeGeneratorProgress = generatorIt->second.progress;
        }
    }
    hud.skillCheckActive = m_skillCheckActive;
    hud.skillCheckNeedle = m_skillCheckNeedle;
    hud.skillCheckSuccessStart = m_skillCheckSuccessStart;
    hud.skillCheckSuccessEnd = m_skillCheckSuccessEnd;
    hud.carryEscapeProgress = m_carryEscapeProgress;
    hud.hookStage = m_hookStage;
    hud.hookEscapeAttemptsUsed = m_hookEscapeAttemptsUsed;
    hud.hookEscapeAttemptsMax = m_hookEscapeAttemptsMax;
    hud.hookEscapeChance = m_hookEscapeChance;
    hud.hookCanAttemptEscape = (m_survivorState == SurvivorHealthState::Hooked && m_hookStage == 1);
    hud.hookSkillChecksEnabled = (m_survivorState == SurvivorHealthState::Hooked && m_hookStage == 2);
    if (m_hookStage > 0)
    {
        const float stageDuration =
            (m_hookStage == 1) ? m_hookStageOneDuration :
            (m_hookStage == 2) ? m_hookStageTwoDuration :
                                 10.0F;
        hud.hookStageProgress = glm::clamp(m_hookStageTimer / stageDuration, 0.0F, 1.0F);
    }
    else
    {
        hud.hookStageProgress = 0.0F;
    }
    hud.runtimeMessage = m_messages.empty() ? std::string{} : m_messages.front().text;
    const engine::fx::FxStats fxStats = m_fxSystem.Stats();
    hud.fxActiveInstances = fxStats.activeInstances;
    hud.fxActiveParticles = fxStats.activeParticles;
    hud.fxCpuMs = fxStats.cpuMs;
    hud.survivorCharacterId = m_selectedSurvivorCharacterId;
    hud.killerCharacterId = m_selectedKillerCharacterId;
    hud.survivorItemId = m_survivorLoadout.itemId.empty() ? "none" : m_survivorLoadout.itemId;
    hud.survivorItemAddonA = m_survivorLoadout.addonAId.empty() ? "none" : m_survivorLoadout.addonAId;
    hud.survivorItemAddonB = m_survivorLoadout.addonBId.empty() ? "none" : m_survivorLoadout.addonBId;
    const loadout::ItemDefinition* hudItemDef = m_loadoutCatalog.FindItem(m_survivorLoadout.itemId);
    float hudItemMaxCharges = 0.0F;
    if (hudItemDef != nullptr)
    {
        hudItemMaxCharges = hudItemDef->maxCharges;
        if (hudItemDef->id == "toolbox")
        {
            hudItemMaxCharges = m_tuning.toolboxCharges;
        }
        else if (hudItemDef->id == "flashlight")
        {
            hudItemMaxCharges = m_tuning.flashlightMaxUseSeconds;
        }
        else if (hudItemDef->id == "map")
        {
            hudItemMaxCharges = static_cast<float>(m_tuning.mapUses);
        }
        hudItemMaxCharges = std::max(0.0F, m_survivorItemModifiers.ApplyStat("max_charges", hudItemMaxCharges));
    }
    hud.survivorItemCharges = m_survivorItemState.charges;
    hud.survivorItemMaxCharges = hudItemMaxCharges;
    hud.survivorItemCharge01 = hudItemMaxCharges > 1.0e-4F ? glm::clamp(m_survivorItemState.charges / hudItemMaxCharges, 0.0F, 1.0F) : 0.0F;
    hud.survivorItemActive = m_survivorItemState.active;
    hud.survivorFlashlightAiming = m_survivorLoadout.itemId == "flashlight" && m_survivorItemState.active;
    hud.survivorFlashlightBlindBuild01 = glm::clamp(
        m_survivorItemState.flashBlindAccum / std::max(0.1F, m_tuning.flashlightBlindBuildSeconds),
        0.0F,
        1.0F
    );
    hud.survivorItemUsesRemaining = m_survivorItemState.mapUsesRemaining;
    if (m_survivorLoadout.itemId == "map")
    {
        hud.survivorItemUseProgress01 = glm::clamp(m_survivorItemState.mapChannelSeconds / std::max(0.05F, m_tuning.mapChannelSeconds), 0.0F, 1.0F);
    }
    else if (m_survivorLoadout.itemId == "flashlight")
    {
        hud.survivorItemUseProgress01 = hud.survivorFlashlightBlindBuild01;
    }
    else if (m_survivorLoadout.itemId == "medkit")
    {
        hud.survivorItemUseProgress01 = m_selfHealProgress;
    }
    hud.killerPowerId = m_killerLoadout.powerId.empty() ? "none" : m_killerLoadout.powerId;
    hud.killerPowerAddonA = m_killerLoadout.addonAId.empty() ? "none" : m_killerLoadout.addonAId;
    hud.killerPowerAddonB = m_killerLoadout.addonBId.empty() ? "none" : m_killerLoadout.addonBId;
    hud.activeTrapCount = static_cast<int>(m_world.BearTraps().size());
    hud.carriedTrapCount = m_killerPowerState.trapperCarriedTraps;
    const loadout::PowerDefinition* hudPowerDef = m_loadoutCatalog.FindPower(m_killerLoadout.powerId);
    float trapSetDuration = m_tuning.trapperSetTrapSeconds;
    if (hudPowerDef != nullptr)
    {
        auto it = hudPowerDef->params.find("set_duration");
        if (it != hudPowerDef->params.end())
        {
            trapSetDuration = it->second;
        }
    }
    trapSetDuration = std::max(0.2F, m_killerPowerModifiers.ApplyStat("set_duration", trapSetDuration));
    hud.trapSetProgress01 = m_killerPowerState.trapperSetting
        ? glm::clamp(m_killerPowerState.trapperSetTimer / std::max(0.01F, trapSetDuration), 0.0F, 1.0F)
        : 0.0F;
    hud.wraithCloaked = m_killerPowerState.wraithCloaked;
    hud.wraithPostUncloakHasteSeconds = m_killerPowerState.wraithPostUncloakTimer;
    hud.trapDebugEnabled = m_trapDebugEnabled;
    hud.killerBlindRemaining = m_killerPowerState.killerBlindTimer;
    hud.killerBlindWhiteStyle = m_tuning.flashlightBlindStyle == 0;
    const auto killerActorIt = m_world.Actors().find(m_killer);
    hud.killerStunRemaining = killerActorIt != m_world.Actors().end() ? killerActorIt->second.stunTimer : 0.0F;
    hud.trapIndicatorText = m_trapIndicatorText;
    hud.trapIndicatorTtl = m_trapIndicatorTimer;
    hud.trapIndicatorDanger = m_trapIndicatorDanger;
    if (m_survivorState == SurvivorHealthState::Trapped)
    {
        for (const auto& [entity, trap] : m_world.BearTraps())
        {
            (void)entity;
            if (trap.trappedEntity != m_survivor)
            {
                continue;
            }
            hud.trappedEscapeAttempts = trap.escapeAttempts;
            hud.trappedEscapeChance = trap.escapeChance;
            hud.interactionPrompt = "TRAPPED: Press E to attempt escape";
            hud.interactionTypeName = "TrapEscape";
            hud.interactionTargetName = "BearTrap";
            break;
        }
    }
    if (m_controlledRole == ControlledRole::Survivor && m_survivorState == SurvivorHealthState::Carried)
    {
        hud.interactionPrompt = "Wiggle: Alternate A/D to escape";
        hud.interactionTypeName = "CarryEscape";
        hud.interactionTargetName = "Self";
    }
    else if (m_controlledRole == ControlledRole::Survivor && m_survivorState == SurvivorHealthState::Hooked)
    {
        if (m_hookStage == 1)
        {
            const int attemptsLeft = std::max(0, m_hookEscapeAttemptsMax - m_hookEscapeAttemptsUsed);
            hud.interactionPrompt =
                "Press E: Attempt self-unhook (4%) | Attempts left: " + std::to_string(attemptsLeft);
            hud.interactionTypeName = "HookAttemptEscape";
            hud.interactionTargetName = "Hook";
        }
        else if (m_hookStage == 2)
        {
            hud.interactionPrompt = "Struggle: hit SPACE on skill checks";
            hud.interactionTypeName = "HookStruggle";
            hud.interactionTargetName = "Hook";
        }
    }
    if (m_controlledRole == ControlledRole::Survivor &&
        hud.interactionPrompt.empty() &&
        (m_survivorState == SurvivorHealthState::Healthy ||
         m_survivorState == SurvivorHealthState::Injured ||
         m_survivorState == SurvivorHealthState::Downed))
    {
        const auto survivorTransformIt = m_world.Transforms().find(m_survivor);
        if (survivorTransformIt != m_world.Transforms().end())
        {
            const engine::scene::Entity nearbyItem = FindNearestGroundItem(survivorTransformIt->second.position, 2.2F);
            if (nearbyItem != 0)
            {
                const auto itemIt = m_world.GroundItems().find(nearbyItem);
                if (itemIt != m_world.GroundItems().end())
                {
                    const std::string label = itemIt->second.itemId.empty() ? "item" : itemIt->second.itemId;
                    if (!m_survivorLoadout.itemId.empty())
                    {
                        hud.interactionPrompt = "Press E to swap " + m_survivorLoadout.itemId + " with " + label;
                        hud.interactionTypeName = "ItemSwap";
                    }
                    else
                    {
                        hud.interactionPrompt = "Press LMB to pick up " + label;
                        hud.interactionTypeName = "ItemPickup";
                    }
                    hud.interactionTargetName = label;
                }
            }

            if (hud.interactionPrompt.empty())
            {
                engine::scene::Entity nearbyTrap = 0;
                if (TryFindNearestTrap(survivorTransformIt->second.position, 1.9F, false, &nearbyTrap))
                {
                    const auto trapIt = m_world.BearTraps().find(nearbyTrap);
                    if (trapIt != m_world.BearTraps().end() && trapIt->second.state == engine::scene::TrapState::Armed)
                    {
                        hud.interactionPrompt = "Hold E to disarm trap";
                        hud.interactionTypeName = "TrapDisarm";
                        hud.interactionTargetName = "BearTrap";
                    }
                }
            }
        }
    }
    else if (m_controlledRole == ControlledRole::Killer && hud.interactionPrompt.empty())
    {
        if (m_killerLoadout.powerId == "bear_trap")
        {
            if (m_killerPowerState.trapperSetting)
            {
                hud.interactionPrompt = "Setting trap... hold RMB (release to cancel)";
                hud.interactionTypeName = "TrapSet";
                hud.interactionTargetName = "Ground";
            }
            else
            {
                const auto killerTransformIt = m_world.Transforms().find(m_killer);
                std::string prompt;
                if (m_killerPowerState.trapperCarriedTraps > 0)
                {
                    prompt = "Hold RMB to set trap";
                }
                if (killerTransformIt != m_world.Transforms().end())
                {
                    engine::scene::Entity nearbyDisarmedTrap = 0;
                    engine::scene::Entity nearbyTrap = 0;
                    if (TryFindNearestTrap(killerTransformIt->second.position, 2.4F, true, &nearbyDisarmedTrap) ||
                        TryFindNearestTrap(killerTransformIt->second.position, 2.4F, false, &nearbyTrap))
                    {
                        if (!prompt.empty())
                        {
                            prompt += " | ";
                        }
                        prompt += "E: pickup trap";
                        if (nearbyDisarmedTrap != 0)
                        {
                            prompt += " | RMB: re-arm";
                        }
                    }
                }
                if (!prompt.empty())
                {
                    hud.interactionPrompt = prompt;
                    hud.interactionTypeName = "TrapPower";
                    hud.interactionTargetName = "BearTrap";
                }
            }
        }
        else if (m_killerLoadout.powerId == "wraith_cloak")
        {
            hud.interactionPrompt = m_killerPowerState.wraithCloaked ? "Press RMB to uncloak" : "Press RMB to cloak";
            hud.interactionTypeName = "WraithPower";
            hud.interactionTargetName = "Self";
        }
    }

    const engine::scene::Entity controlledEntity = ControlledEntity();
    const auto controlledTransformIt = m_world.Transforms().find(controlledEntity);
    if (controlledTransformIt != m_world.Transforms().end() && !m_loopDebugTiles.empty())
    {
        float bestDistance = std::numeric_limits<float>::max();
        const LoopDebugTile* bestTile = nullptr;
        for (const LoopDebugTile& tile : m_loopDebugTiles)
        {
            const float distance = DistanceXZ(controlledTransformIt->second.position, tile.center);
            if (distance < bestDistance)
            {
                bestDistance = distance;
                bestTile = &tile;
            }
        }

        if (bestTile != nullptr)
        {
            hud.activeLoopTileId = bestTile->loopId;
            switch (bestTile->archetype)
            {
                case 0: hud.activeLoopArchetype = "JungleGymLong"; break;
                case 1: hud.activeLoopArchetype = "JungleGymShort"; break;
                case 2: hud.activeLoopArchetype = "LTWalls"; break;
                case 3: hud.activeLoopArchetype = "Shack"; break;
                case 4: hud.activeLoopArchetype = "FourLane"; break;
                case 5: hud.activeLoopArchetype = "FillerA"; break;
                case 6: hud.activeLoopArchetype = "FillerB"; break;
                case 7: hud.activeLoopArchetype = "LongWall"; break;
                case 8: hud.activeLoopArchetype = "ShortWall"; break;
                case 9: hud.activeLoopArchetype = "LWallWindow"; break;
                case 10: hud.activeLoopArchetype = "LWallPallet"; break;
                case 11: hud.activeLoopArchetype = "TWalls"; break;
                case 12: hud.activeLoopArchetype = "GymBox"; break;
                case 13: hud.activeLoopArchetype = "DebrisPile"; break;
                default: hud.activeLoopArchetype = "Unknown"; break;
            }
        }
    }

    hud.chaseActive = m_chase.isChasing;
    hud.chaseDistance = m_chase.distance;
    hud.lineOfSight = m_chase.hasLineOfSight;
    hud.inCenterFOV = m_chase.inCenterFOV;
    hud.timeInChase = m_chase.timeInChase;
    hud.timeSinceLOS = m_chase.timeSinceSeenLOS;
    hud.timeSinceCenterFOV = m_chase.timeSinceCenterFOV;

    // Get survivor sprinting state
    const auto survivorActorIt = m_world.Actors().find(m_survivor);
    hud.survivorSprinting = (survivorActorIt != m_world.Actors().end()) && survivorActorIt->second.sprinting;

    // Bloodlust state
    hud.bloodlustTier = m_bloodlust.tier;
    hud.bloodlustSpeedMultiplier = GetBloodlustSpeedMultiplier();
    hud.killerBaseSpeed = m_tuning.killerMoveSpeed;
    hud.killerCurrentSpeed = m_tuning.killerMoveSpeed * m_killerSpeedPercent * hud.bloodlustSpeedMultiplier;

    // Phase B2/B3: Scratch marks and blood pools debug info
    hud.scratchActiveCount = GetActiveScratchCount();
    hud.bloodActiveCount = GetActiveBloodPoolCount();
    hud.scratchSpawnInterval = m_scratchNextInterval;

    hud.collisionEnabled = m_collisionEnabled;
    hud.debugDrawEnabled = m_debugDrawEnabled;
    hud.physicsDebugEnabled = m_physicsDebugEnabled;
    hud.noclipEnabled = m_noClipEnabled;

    const engine::scene::Entity controlled = ControlledEntity();
    const auto actorIt = m_world.Actors().find(controlled);
    if (actorIt != m_world.Actors().end())
    {
        const engine::scene::ActorComponent& actor = actorIt->second;
        hud.playerSpeed = glm::length(glm::vec2{actor.velocity.x, actor.velocity.z});
        hud.grounded = actor.grounded;
        hud.velocity = actor.velocity;
        hud.lastCollisionNormal = actor.lastCollisionNormal;
        hud.penetrationDepth = actor.lastPenetrationDepth;
        hud.vaultTypeName = actor.lastVaultType;
        hud.movementStateName = BuildMovementStateText(controlled, actor);

        // Populate perk debug info for both roles
        const auto populatePerkDebug = [&](engine::scene::Role role, std::vector<HudState::ActivePerkDebug>& outDebug, float& outSpeedMod) {
            const auto& activePerkStates = m_perkSystem.GetActivePerks(role);
            outDebug.reserve(activePerkStates.size());
            for (const auto& state : activePerkStates)
            {
                const auto* perk = m_perkSystem.GetPerk(state.perkId);
                if (!perk) continue;

                HudState::ActivePerkDebug debug;
                debug.id = state.perkId;
                debug.name = perk->name;
                debug.isActive = state.isActive;
                debug.activeRemainingSeconds = state.activeRemainingSeconds;
                debug.cooldownRemainingSeconds = state.cooldownRemainingSeconds;
                debug.stacks = state.currentStacks;
                outDebug.push_back(debug);
            }

            // Get speed modifier for display (sample with sprint=true to show max effect)
            outSpeedMod = m_perkSystem.GetSpeedModifier(role, true, false, false);
        };

        populatePerkDebug(engine::scene::Role::Survivor, hud.activePerksSurvivor, hud.speedModifierSurvivor);
        populatePerkDebug(engine::scene::Role::Killer, hud.activePerksKiller, hud.speedModifierKiller);
    }

    auto pushDebugLabel = [&](engine::scene::Entity entity, const std::string& name, bool killer) {
        const auto transformIt = m_world.Transforms().find(entity);
        const auto actorDebugIt = m_world.Actors().find(entity);
        if (transformIt == m_world.Transforms().end() || actorDebugIt == m_world.Actors().end())
        {
            return;
        }

        HudState::DebugActorLabel label;
        label.name = name;
        label.healthState = killer ? "-" : SurvivorStateToText(m_survivorState);
        label.movementState = BuildMovementStateText(entity, actorDebugIt->second);
        label.attackState = killer ? KillerAttackStateToText(m_killerAttackState) : "-";
        label.worldPosition = transformIt->second.position + glm::vec3{0.0F, 2.2F, 0.0F};
        label.forward = transformIt->second.forward;
        label.speed = glm::length(glm::vec2{actorDebugIt->second.velocity.x, actorDebugIt->second.velocity.z});
        label.chasing = m_chase.isChasing;
        label.killer = killer;
        hud.debugActors.push_back(label);
    };
    pushDebugLabel(m_survivor, "Player1", false);
    pushDebugLabel(m_killer, "Player2", true);

    // Phase B4: Killer look light debug info
    hud.killerLightEnabled = m_killerLookLight.enabled;
    hud.killerLightRange = m_killerLookLight.range;
    hud.killerLightIntensity = m_killerLookLight.intensity;
    hud.killerLightInnerAngle = m_killerLookLight.innerAngleDegrees;
    hud.killerLightOuterAngle = m_killerLookLight.outerAngleDegrees;
    hud.killerLightPitch = m_killerLookLight.pitchDegrees;

    // Populate perk slots from actual loadouts
    const auto& survivorLoadout = m_perkSystem.GetSurvivorLoadout();
    const auto& killerLoadout = m_perkSystem.GetKillerLoadout();
    
    // Helper to get active state for a perk
    const auto getActiveState = [&](const std::string& perkId, engine::scene::Role role) -> const perks::ActivePerkState* {
        if (perkId.empty()) return nullptr;
        const auto& activePerks = m_perkSystem.GetActivePerks(role);
        for (const auto& state : activePerks)
        {
            if (state.perkId == perkId)
            {
                return &state;
            }
        }
        return nullptr;
    };

    // Populate survivor perk slots (loadout has 3 slots, HUD has 4)
    for (int i = 0; i < 4; ++i)
    {
        const std::string perkId = (i < 3) ? survivorLoadout.GetPerk(i) : "";
        if (perkId.empty())
        {
            hud.survivorPerkSlots[i] = HudState::ActivePerkDebug{}; // Empty slot
        }
        else
        {
            const auto* perk = m_perkSystem.GetPerk(perkId);
            const auto* activeState = getActiveState(perkId, engine::scene::Role::Survivor);
            hud.survivorPerkSlots[i] = HudState::ActivePerkDebug{
                perkId,
                perk ? perk->name : perkId,
                activeState ? activeState->isActive : false,
                activeState ? activeState->activeRemainingSeconds : 0.0F,
                activeState ? activeState->cooldownRemainingSeconds : 0.0F,
                activeState ? activeState->currentStacks : 0,
                1, // Default tier (TODO: implement tier system)
                perk ? perk->effects.activationCooldownSeconds : 0.0F
            };
            // std::cout << "[PERK] SURVIVOR SLOT " << i << ": " << perkId << " (active=" << hud.survivorPerkSlots[i].isActive << ")\n";
        }
    }

    // Populate killer perk slots
    for (int i = 0; i < 4; ++i)
    {
        const std::string perkId = (i < 3) ? killerLoadout.GetPerk(i) : "";
        if (perkId.empty())
        {
            hud.killerPerkSlots[i] = HudState::ActivePerkDebug{}; // Empty slot
        }
        else
        {
            const auto* perk = m_perkSystem.GetPerk(perkId);
            const auto* activeState = getActiveState(perkId, engine::scene::Role::Killer);
            hud.killerPerkSlots[i] = HudState::ActivePerkDebug{
                perkId,
                perk ? perk->name : perkId,
                activeState ? activeState->isActive : false,
                activeState ? activeState->activeRemainingSeconds : 0.0F,
                activeState ? activeState->cooldownRemainingSeconds : 0.0F,
                activeState ? activeState->currentStacks : 0,
                1, // Default tier
                perk ? perk->effects.activationCooldownSeconds : 0.0F
            };
            // std::cout << "[PERK] KILLER SLOT " << i << ": " << perkId << " (active=" << hud.killerPerkSlots[i].isActive << ")\n";
        }
    }

    return hud;
}
void GameplaySystems::LoadMap(const std::string& mapName)
{
    m_staticBatcher.Clear();

    if (mapName == "test")
    {
        BuildSceneFromMap(MapType::Test, m_generationSeed);
    }
    else if (mapName == "main" || mapName == "main_map")
    {
        BuildSceneFromMap(MapType::Main, m_generationSeed);
    }
    else if (mapName == "collision_test")
    {
        BuildSceneFromMap(MapType::CollisionTest, m_generationSeed);
    }
    else
    {
        ::game::maps::GeneratedMap generated;
        std::string error;
        if (editor::LevelAssetIO::BuildGeneratedMapFromMapName(mapName, &generated, &error))
        {
            BuildSceneFromGeneratedMap(generated, MapType::Test, m_generationSeed, mapName);
        }
        else
        {
            AddRuntimeMessage("Map load failed: " + error, 2.4F);
            BuildSceneFromMap(MapType::Test, m_generationSeed);
        }
    }
}

void GameplaySystems::RegenerateLoops()
{
    RegenerateLoops(m_generationSeed + 1U);
}

void GameplaySystems::RegenerateLoops(unsigned int seed)
{
    m_generationSeed = seed;
    if (m_currentMap == MapType::Main && m_activeMapName == "main")
    {
        BuildSceneFromMap(MapType::Main, m_generationSeed);
    }
}

void GameplaySystems::SetDbdSpawnsEnabled(bool enabled)
{
    m_dbdSpawnsEnabled = enabled;
    // Regenerate current map with new spawn settings
    if (m_currentMap == MapType::Main && m_activeMapName == "main")
    {
        BuildSceneFromMap(MapType::Main, m_generationSeed);
        AddRuntimeMessage(std::string("DBD spawns ") + (enabled ? "enabled" : "disabled"), 2.0F);
    }
    else
    {
        AddRuntimeMessage("Load main map first to use DBD spawns", 2.0F);
    }
}

void GameplaySystems::SpawnSurvivor()
{
    if (!RespawnRole("survivor"))
    {
        AddRuntimeMessage("Spawn survivor failed", 1.4F);
    }
}

void GameplaySystems::SpawnKiller()
{
    if (!RespawnRole("killer"))
    {
        AddRuntimeMessage("Spawn killer failed", 1.4F);
    }
}

void GameplaySystems::SpawnPallet()
{
    glm::vec3 spawnPosition{0.0F, 1.05F, 0.0F};
    if (m_survivor != 0)
    {
        const auto transformIt = m_world.Transforms().find(m_survivor);
        if (transformIt != m_world.Transforms().end())
        {
            const glm::vec3 forward = glm::normalize(glm::vec3{transformIt->second.forward.x, 0.0F, transformIt->second.forward.z});
            spawnPosition = transformIt->second.position + forward * 2.0F;
            spawnPosition.y = 1.05F;
        }
    }

    const engine::scene::Entity palletEntity = m_world.CreateEntity();
    m_world.Transforms()[palletEntity] = engine::scene::Transform{spawnPosition, glm::vec3{0.0F}, glm::vec3{1.0F}, glm::vec3{1.0F, 0.0F, 0.0F}};
    engine::scene::PalletComponent pallet;
    pallet.halfExtents = pallet.standingHalfExtents;
    m_world.Pallets()[palletEntity] = pallet;
}

void GameplaySystems::SpawnWindow()
{
    glm::vec3 spawnPosition{0.0F, 1.0F, 0.0F};
    glm::vec3 normal{0.0F, 0.0F, 1.0F};
    if (m_survivor != 0)
    {
        const auto transformIt = m_world.Transforms().find(m_survivor);
        if (transformIt != m_world.Transforms().end())
        {
            const glm::vec3 forward = glm::normalize(glm::vec3{transformIt->second.forward.x, 0.0F, transformIt->second.forward.z});
            spawnPosition = transformIt->second.position + forward * 2.4F;
            spawnPosition.y = 1.0F;
            normal = forward;
        }
    }

    const engine::scene::Entity windowEntity = m_world.CreateEntity();
    m_world.Transforms()[windowEntity] = engine::scene::Transform{spawnPosition, glm::vec3{0.0F}, glm::vec3{1.0F}, normal};

    engine::scene::WindowComponent window;
    window.normal = glm::length(normal) > 0.001F ? glm::normalize(normal) : glm::vec3{0.0F, 0.0F, 1.0F};
    m_world.Windows()[windowEntity] = window;
}

bool GameplaySystems::SpawnRoleHere(const std::string& roleName)
{
    const std::string normalizedRole = roleName == "killer" ? "killer" : "survivor";
    const SpawnPointType spawnType = SpawnPointTypeFromRole(normalizedRole);

    glm::vec3 desired = m_cameraPosition + m_cameraForward * 3.0F;
    glm::vec3 rayStart = desired + glm::vec3{0.0F, 20.0F, 0.0F};
    glm::vec3 rayEnd = desired + glm::vec3{0.0F, -40.0F, 0.0F};
    if (const std::optional<engine::physics::RaycastHit> hit = m_physics.RaycastNearest(rayStart, rayEnd); hit.has_value())
    {
        desired = hit->position;
    }

    desired.y += 1.1F;

    const float radius = normalizedRole == "survivor" ? m_tuning.survivorCapsuleRadius : m_tuning.killerCapsuleRadius;
    const float height = normalizedRole == "survivor" ? m_tuning.survivorCapsuleHeight : m_tuning.killerCapsuleHeight;
    glm::vec3 resolved = desired;
    if (!ResolveSpawnPositionValid(desired, radius, height, &resolved))
    {
        if (const std::optional<SpawnPointInfo> fallback = FindSpawnPointByType(spawnType); fallback.has_value())
        {
            resolved = fallback->position;
        }
    }

    if (normalizedRole == "survivor")
    {
        DestroyEntity(m_survivor);
    }
    else
    {
        DestroyEntity(m_killer);
    }

    const engine::scene::Entity spawned = SpawnRoleActorAt(normalizedRole, resolved);
    if (spawned == 0)
    {
        return false;
    }
    RebuildPhysicsWorld();
    return true;
}

bool GameplaySystems::SpawnRoleAt(const std::string& roleName, int spawnId)
{
    const std::string normalizedRole = roleName == "killer" ? "killer" : "survivor";
    const std::optional<SpawnPointInfo> spawn = FindSpawnPointById(spawnId);
    if (!spawn.has_value())
    {
        return false;
    }

    glm::vec3 target = spawn->position;
    const float radius = normalizedRole == "survivor" ? m_tuning.survivorCapsuleRadius : m_tuning.killerCapsuleRadius;
    const float height = normalizedRole == "survivor" ? m_tuning.survivorCapsuleHeight : m_tuning.killerCapsuleHeight;
    glm::vec3 resolved = target;
    if (!ResolveSpawnPositionValid(target, radius, height, &resolved))
    {
        resolved = target;
    }

    if (normalizedRole == "survivor")
    {
        DestroyEntity(m_survivor);
    }
    else
    {
        DestroyEntity(m_killer);
    }

    const engine::scene::Entity spawned = SpawnRoleActorAt(normalizedRole, resolved);
    if (spawned == 0)
    {
        return false;
    }
    RebuildPhysicsWorld();
    return true;
}

bool GameplaySystems::RespawnRole(const std::string& roleName)
{
    const std::string normalizedRole = roleName == "killer" ? "killer" : "survivor";
    const SpawnPointType spawnType = SpawnPointTypeFromRole(normalizedRole);
    const std::optional<SpawnPointInfo> spawn = FindSpawnPointByType(spawnType);
    if (!spawn.has_value())
    {
        return false;
    }

    return SpawnRoleAt(normalizedRole, spawn->id);
}

std::string GameplaySystems::ListSpawnPoints() const
{
    std::ostringstream oss;
    if (m_spawnPoints.empty())
    {
        oss << "No spawn points";
        return oss.str();
    }

    for (const SpawnPointInfo& spawn : m_spawnPoints)
    {
        oss << "#" << spawn.id << " [" << SpawnTypeToText(spawn.type) << "] "
            << "(" << spawn.position.x << ", " << spawn.position.y << ", " << spawn.position.z << ")\n";
    }
    return oss.str();
}

std::vector<GameplaySystems::SpawnPointInfo> GameplaySystems::GetSpawnPoints() const
{
    return m_spawnPoints;
}

engine::scene::Entity GameplaySystems::RoleEntity(const std::string& roleName) const
{
    if (roleName == "killer")
    {
        return m_killer;
    }
    return m_survivor;
}

std::string GameplaySystems::MovementStateForRole(const std::string& roleName) const
{
    const engine::scene::Entity entity = RoleEntity(roleName);
    const auto actorIt = m_world.Actors().find(entity);
    if (actorIt == m_world.Actors().end())
    {
        return "None";
    }
    return BuildMovementStateText(entity, actorIt->second);
}

glm::vec3 GameplaySystems::RolePosition(const std::string& roleName) const
{
    const engine::scene::Entity entity = RoleEntity(roleName);
    const auto transformIt = m_world.Transforms().find(entity);
    if (transformIt == m_world.Transforms().end())
    {
        return glm::vec3{0.0F};
    }
    return transformIt->second.position;
}

glm::vec3 GameplaySystems::RoleForward(const std::string& roleName) const
{
    const engine::scene::Entity entity = RoleEntity(roleName);
    const auto transformIt = m_world.Transforms().find(entity);
    if (transformIt == m_world.Transforms().end())
    {
        return glm::vec3{0.0F, 0.0F, -1.0F};
    }
    const glm::vec3 f = transformIt->second.forward;
    if (glm::length(f) < 1.0e-5F)
    {
        return glm::vec3{0.0F, 0.0F, -1.0F};
    }
    return glm::normalize(f);
}

std::string GameplaySystems::SurvivorHealthStateText() const
{
    return SurvivorStateToText(m_survivorState);
}

void GameplaySystems::TeleportSurvivor(const glm::vec3& position)
{
    if (m_survivor == 0)
    {
        SpawnSurvivor();
    }

    auto transformIt = m_world.Transforms().find(m_survivor);
    if (transformIt != m_world.Transforms().end())
    {
        transformIt->second.position = position;
    }
}

void GameplaySystems::TeleportKiller(const glm::vec3& position)
{
    if (m_killer == 0)
    {
        SpawnKiller();
    }

    auto transformIt = m_world.Transforms().find(m_killer);
    if (transformIt != m_world.Transforms().end())
    {
        transformIt->second.position = position;
    }
}

void GameplaySystems::SetSurvivorSprintSpeed(float speed)
{
    if (m_survivor == 0)
    {
        return;
    }

    auto actorIt = m_world.Actors().find(m_survivor);
    if (actorIt != m_world.Actors().end())
    {
        m_tuning.survivorSprintSpeed = std::max(0.1F, speed);
        actorIt->second.sprintSpeed = m_tuning.survivorSprintSpeed * m_survivorSpeedPercent;
        actorIt->second.walkSpeed = m_tuning.survivorWalkSpeed * m_survivorSpeedPercent;
    }
}

void GameplaySystems::SetRoleSpeedPercent(const std::string& roleName, float percent)
{
    const float clamped = glm::clamp(percent, 0.2F, 4.0F);
    if (roleName == "survivor")
    {
        m_survivorSpeedPercent = clamped;
        auto it = m_world.Actors().find(m_survivor);
        if (it != m_world.Actors().end())
        {
            it->second.sprintSpeed = m_tuning.survivorSprintSpeed * m_survivorSpeedPercent;
            it->second.walkSpeed = m_tuning.survivorWalkSpeed * m_survivorSpeedPercent;
        }
        return;
    }

    if (roleName == "killer")
    {
        m_killerSpeedPercent = clamped;
        auto it = m_world.Actors().find(m_killer);
        if (it != m_world.Actors().end())
        {
            // Apply bloodlust multiplier ON TOP of base speed
            const float bloodlustMult = GetBloodlustSpeedMultiplier();
            const float finalSpeed = m_tuning.killerMoveSpeed * m_killerSpeedPercent * bloodlustMult;
            it->second.walkSpeed = finalSpeed;
            it->second.sprintSpeed = finalSpeed;
        }
    }
}

void GameplaySystems::SetRoleCapsuleSize(const std::string& roleName, float radius, float height)
{
    const float r = glm::clamp(radius, 0.2F, 1.2F);
    const float h = glm::clamp(height, 0.9F, 3.2F);

    auto apply = [&](engine::scene::Entity entity) {
        auto it = m_world.Actors().find(entity);
        if (it == m_world.Actors().end())
        {
            return;
        }
        it->second.capsuleRadius = r;
        it->second.capsuleHeight = h;
        it->second.eyeHeight = std::max(0.8F, h * 0.88F);
    };

    if (roleName == "survivor")
    {
        apply(m_survivor);
    }
    else if (roleName == "killer")
    {
        apply(m_killer);
    }
}

void GameplaySystems::ToggleCollision(bool enabled)
{
    m_collisionEnabled = enabled;
    for (auto& [_, actor] : m_world.Actors())
    {
        actor.collisionEnabled = enabled;
    }
}

void GameplaySystems::ToggleDebugDraw(bool enabled)
{
    m_debugDrawEnabled = enabled;
}

void GameplaySystems::TogglePhysicsDebug(bool enabled)
{
    m_physicsDebugEnabled = enabled;
}

void GameplaySystems::SetNoClip(bool enabled)
{
    m_noClipEnabled = enabled;
    for (auto& [_, actor] : m_world.Actors())
    {
        actor.noclipEnabled = enabled;
    }
}

void GameplaySystems::SetForcedChase(bool enabled)
{
    m_forcedChase = enabled;
    if (!enabled)
    {
        // Reset timers when disabling forced chase
        m_chase.timeSinceSeenLOS = 0.0F;
        m_chase.timeSinceCenterFOV = 0.0F;
    }
}

void GameplaySystems::SetSurvivorPerkLoadout(const perks::PerkLoadout& loadout)
{
    m_survivorPerks = loadout;
    m_perkSystem.SetSurvivorLoadout(loadout);
    m_perkSystem.InitializeActiveStates();

    if (!m_survivorPerks.IsEmpty())
    {
        std::cout << "GameplaySystems: Set survivor perk loadout with " << m_survivorPerks.GetSlotCount() << " perks\n";
    }
}

void GameplaySystems::SetKillerPerkLoadout(const perks::PerkLoadout& loadout)
{
    m_killerPerks = loadout;
    m_perkSystem.SetKillerLoadout(loadout);
    m_perkSystem.InitializeActiveStates();

    if (!m_killerPerks.IsEmpty())
    {
        std::cout << "GameplaySystems: Set killer perk loadout with " << m_killerPerks.GetSlotCount() << " perks\n";
    }
}

void GameplaySystems::ToggleTerrorRadiusVisualization(bool enabled)
{
    m_terrorRadiusVisible = enabled;
}

void GameplaySystems::SetTerrorRadius(float meters)
{
    m_terrorRadiusMeters = std::max(1.0F, meters);
}

void GameplaySystems::SetCameraModeOverride(const std::string& modeName)
{
    if (modeName == "survivor")
    {
        m_cameraOverride = CameraOverride::SurvivorThirdPerson;
    }
    else if (modeName == "killer")
    {
        m_cameraOverride = CameraOverride::KillerFirstPerson;
    }
    else
    {
        m_cameraOverride = CameraOverride::RoleBased;
    }
}

void GameplaySystems::SetControlledRole(const std::string& roleName)
{
    if (roleName == "survivor")
    {
        m_controlledRole = ControlledRole::Survivor;
    }
    else if (roleName == "killer")
    {
        m_controlledRole = ControlledRole::Killer;
    }
}

void GameplaySystems::ToggleControlledRole()
{
    m_controlledRole = (m_controlledRole == ControlledRole::Survivor) ? ControlledRole::Killer : ControlledRole::Survivor;
}

void GameplaySystems::SetRenderModeLabel(const std::string& modeName)
{
    m_renderModeName = modeName;
}

void GameplaySystems::SetLookSettings(float survivorSensitivity, float killerSensitivity, bool invertY)
{
    m_survivorLookSensitivity = glm::clamp(survivorSensitivity, 0.0001F, 0.02F);
    m_killerLookSensitivity = glm::clamp(killerSensitivity, 0.0001F, 0.02F);
    m_invertLookY = invertY;
}

void GameplaySystems::ApplyGameplayTuning(const GameplayTuning& tuning)
{
    m_tuning = tuning;

    m_tuning.survivorWalkSpeed = glm::clamp(m_tuning.survivorWalkSpeed, 0.5F, 10.0F);
    m_tuning.survivorSprintSpeed = glm::clamp(m_tuning.survivorSprintSpeed, m_tuning.survivorWalkSpeed, 14.0F);
    m_tuning.survivorCrouchSpeed = glm::clamp(m_tuning.survivorCrouchSpeed, 0.2F, m_tuning.survivorWalkSpeed);
    m_tuning.survivorCrawlSpeed = glm::clamp(m_tuning.survivorCrawlSpeed, 0.1F, m_tuning.survivorWalkSpeed);
    m_tuning.killerMoveSpeed = glm::clamp(m_tuning.killerMoveSpeed, 0.5F, 16.0F);

    m_tuning.survivorCapsuleRadius = glm::clamp(m_tuning.survivorCapsuleRadius, 0.2F, 1.2F);
    m_tuning.survivorCapsuleHeight = glm::clamp(m_tuning.survivorCapsuleHeight, 0.9F, 3.2F);
    m_tuning.killerCapsuleRadius = glm::clamp(m_tuning.killerCapsuleRadius, 0.2F, 1.2F);
    m_tuning.killerCapsuleHeight = glm::clamp(m_tuning.killerCapsuleHeight, 0.9F, 3.2F);

    m_tuning.terrorRadiusMeters = glm::clamp(m_tuning.terrorRadiusMeters, 4.0F, 80.0F);
    m_tuning.terrorRadiusChaseMeters = glm::clamp(m_tuning.terrorRadiusChaseMeters, m_tuning.terrorRadiusMeters, 96.0F);

    m_tuning.vaultSlowTime = glm::clamp(m_tuning.vaultSlowTime, 0.2F, 2.0F);
    m_tuning.vaultMediumTime = glm::clamp(m_tuning.vaultMediumTime, 0.2F, 2.0F);
    m_tuning.vaultFastTime = glm::clamp(m_tuning.vaultFastTime, 0.15F, 1.2F);
    m_tuning.fastVaultDotThreshold = glm::clamp(m_tuning.fastVaultDotThreshold, 0.3F, 0.99F);
    m_tuning.fastVaultSpeedMultiplier = glm::clamp(m_tuning.fastVaultSpeedMultiplier, 0.3F, 1.5F);
    m_tuning.fastVaultMinRunup = glm::clamp(m_tuning.fastVaultMinRunup, 0.0F, 8.0F);

    m_tuning.shortAttackRange = glm::clamp(m_tuning.shortAttackRange, 0.5F, 8.0F);
    m_tuning.shortAttackAngleDegrees = glm::clamp(m_tuning.shortAttackAngleDegrees, 10.0F, 170.0F);
    m_tuning.lungeHoldMinSeconds = glm::clamp(m_tuning.lungeHoldMinSeconds, 0.02F, 2.0F);
    m_tuning.lungeDurationSeconds = glm::clamp(m_tuning.lungeDurationSeconds, 0.08F, 3.0F);
    m_tuning.lungeRecoverSeconds = glm::clamp(m_tuning.lungeRecoverSeconds, 0.05F, 3.0F);
    m_tuning.shortRecoverSeconds = glm::clamp(m_tuning.shortRecoverSeconds, 0.05F, 3.0F);
    m_tuning.missRecoverSeconds = glm::clamp(m_tuning.missRecoverSeconds, 0.05F, 3.0F);
    m_tuning.lungeSpeedStart = glm::clamp(m_tuning.lungeSpeedStart, 1.0F, 30.0F);
    m_tuning.lungeSpeedEnd = glm::clamp(m_tuning.lungeSpeedEnd, m_tuning.lungeSpeedStart, 35.0F);

    m_tuning.healDurationSeconds = glm::clamp(m_tuning.healDurationSeconds, 2.0F, 120.0F);
    m_tuning.skillCheckMinInterval = glm::clamp(m_tuning.skillCheckMinInterval, 0.3F, 30.0F);
    m_tuning.skillCheckMaxInterval = glm::clamp(m_tuning.skillCheckMaxInterval, m_tuning.skillCheckMinInterval, 60.0F);
    m_tuning.generatorRepairSecondsBase = glm::clamp(m_tuning.generatorRepairSecondsBase, 5.0F, 240.0F);

    m_tuning.medkitFullHealCharges = glm::clamp(m_tuning.medkitFullHealCharges, 1.0F, 128.0F);
    m_tuning.medkitHealSpeedMultiplier = glm::clamp(m_tuning.medkitHealSpeedMultiplier, 0.1F, 8.0F);
    m_tuning.toolboxCharges = glm::clamp(m_tuning.toolboxCharges, 1.0F, 256.0F);
    m_tuning.toolboxChargeDrainPerSecond = glm::clamp(m_tuning.toolboxChargeDrainPerSecond, 0.05F, 30.0F);
    m_tuning.toolboxRepairSpeedBonus = glm::clamp(m_tuning.toolboxRepairSpeedBonus, 0.0F, 5.0F);

    m_tuning.flashlightMaxUseSeconds = glm::clamp(m_tuning.flashlightMaxUseSeconds, 0.5F, 120.0F);
    m_tuning.flashlightBlindBuildSeconds = glm::clamp(m_tuning.flashlightBlindBuildSeconds, 0.05F, 10.0F);
    m_tuning.flashlightBlindDurationSeconds = glm::clamp(m_tuning.flashlightBlindDurationSeconds, 0.05F, 20.0F);
    m_tuning.flashlightBeamRange = glm::clamp(m_tuning.flashlightBeamRange, 1.0F, 100.0F);
    m_tuning.flashlightBeamAngleDegrees = glm::clamp(m_tuning.flashlightBeamAngleDegrees, 5.0F, 120.0F);
    m_tuning.flashlightBlindStyle = glm::clamp(m_tuning.flashlightBlindStyle, 0, 1);

    m_tuning.mapChannelSeconds = glm::clamp(m_tuning.mapChannelSeconds, 0.05F, 10.0F);
    m_tuning.mapUses = glm::clamp(m_tuning.mapUses, 0, 99);
    m_tuning.mapRevealRangeMeters = glm::clamp(m_tuning.mapRevealRangeMeters, 1.0F, 256.0F);
    m_tuning.mapRevealDurationSeconds = glm::clamp(m_tuning.mapRevealDurationSeconds, 0.1F, 60.0F);

    m_tuning.trapperStartCarryTraps = glm::clamp(m_tuning.trapperStartCarryTraps, 0, 32);
    m_tuning.trapperMaxCarryTraps = glm::clamp(m_tuning.trapperMaxCarryTraps, 1, 32);
    m_tuning.trapperGroundSpawnTraps = glm::clamp(m_tuning.trapperGroundSpawnTraps, 0, 128);
    m_tuning.trapperSetTrapSeconds = glm::clamp(m_tuning.trapperSetTrapSeconds, 0.1F, 20.0F);
    m_tuning.trapperDisarmSeconds = glm::clamp(m_tuning.trapperDisarmSeconds, 0.1F, 20.0F);
    m_tuning.trapEscapeBaseChance = glm::clamp(m_tuning.trapEscapeBaseChance, 0.01F, 0.99F);
    m_tuning.trapEscapeChanceStep = glm::clamp(m_tuning.trapEscapeChanceStep, 0.01F, 0.99F);
    m_tuning.trapEscapeChanceMax = glm::clamp(m_tuning.trapEscapeChanceMax, 0.05F, 0.99F);
    m_tuning.trapKillerStunSeconds = glm::clamp(m_tuning.trapKillerStunSeconds, 0.1F, 20.0F);

    m_tuning.wraithCloakMoveSpeedMultiplier = glm::clamp(m_tuning.wraithCloakMoveSpeedMultiplier, 1.0F, 4.0F);
    m_tuning.wraithCloakTransitionSeconds = glm::clamp(m_tuning.wraithCloakTransitionSeconds, 0.1F, 10.0F);
    m_tuning.wraithUncloakTransitionSeconds = glm::clamp(m_tuning.wraithUncloakTransitionSeconds, 0.1F, 10.0F);
    m_tuning.wraithPostUncloakHasteSeconds = glm::clamp(m_tuning.wraithPostUncloakHasteSeconds, 0.0F, 20.0F);

    m_tuning.weightTLWalls = std::max(0.0F, m_tuning.weightTLWalls);
    m_tuning.weightJungleGymLong = std::max(0.0F, m_tuning.weightJungleGymLong);
    m_tuning.weightJungleGymShort = std::max(0.0F, m_tuning.weightJungleGymShort);
    m_tuning.weightShack = std::max(0.0F, m_tuning.weightShack);
    m_tuning.weightFourLane = std::max(0.0F, m_tuning.weightFourLane);
    m_tuning.weightFillerA = std::max(0.0F, m_tuning.weightFillerA);
    m_tuning.weightFillerB = std::max(0.0F, m_tuning.weightFillerB);
    m_tuning.weightLongWall = std::max(0.0F, m_tuning.weightLongWall);
    m_tuning.weightShortWall = std::max(0.0F, m_tuning.weightShortWall);
    m_tuning.weightLWallWindow = std::max(0.0F, m_tuning.weightLWallWindow);
    m_tuning.weightLWallPallet = std::max(0.0F, m_tuning.weightLWallPallet);
    m_tuning.weightTWalls = std::max(0.0F, m_tuning.weightTWalls);
    m_tuning.weightGymBox = std::max(0.0F, m_tuning.weightGymBox);
    m_tuning.weightDebrisPile = std::max(0.0F, m_tuning.weightDebrisPile);
    m_tuning.maxLoopsPerMap = glm::clamp(m_tuning.maxLoopsPerMap, 0, 64);
    m_tuning.minLoopDistanceTiles = glm::clamp(m_tuning.minLoopDistanceTiles, 0.0F, 8.0F);
    m_tuning.maxSafePallets = glm::clamp(m_tuning.maxSafePallets, 0, 64);
    m_tuning.maxDeadzoneTiles = glm::clamp(m_tuning.maxDeadzoneTiles, 1, 8);

    m_tuning.serverTickRate = (m_tuning.serverTickRate <= 30) ? 30 : 60;
    m_tuning.interpolationBufferMs = glm::clamp(m_tuning.interpolationBufferMs, 50, 1000);

    m_terrorRadiusMeters = m_tuning.terrorRadiusMeters;
    m_terrorRadiusChaseMeters = m_tuning.terrorRadiusChaseMeters;
    m_killerShortRange = m_tuning.shortAttackRange;
    m_killerShortHalfAngleRadians = glm::radians(m_tuning.shortAttackAngleDegrees * 0.5F);
    m_killerLungeRange = std::max(m_tuning.shortAttackRange, m_tuning.shortAttackRange + 0.8F);
    m_killerLungeHalfAngleRadians = m_killerShortHalfAngleRadians;
    m_killerLungeChargeMinSeconds = std::min(m_tuning.lungeHoldMinSeconds, m_tuning.lungeDurationSeconds);
    m_killerLungeChargeMaxSeconds = m_tuning.lungeDurationSeconds;
    m_killerLungeDurationSeconds = m_tuning.lungeDurationSeconds;
    m_killerLungeRecoverSeconds = m_tuning.lungeRecoverSeconds;
    m_killerShortRecoverSeconds = m_tuning.shortRecoverSeconds;
    m_killerMissRecoverSeconds = m_tuning.missRecoverSeconds;
    m_killerLungeSpeedStart = m_tuning.lungeSpeedStart;
    m_killerLungeSpeedEnd = m_tuning.lungeSpeedEnd;

    m_generationSettings.weightTLWalls = m_tuning.weightTLWalls;
    m_generationSettings.weightJungleGymLong = m_tuning.weightJungleGymLong;
    m_generationSettings.weightJungleGymShort = m_tuning.weightJungleGymShort;
    m_generationSettings.weightShack = m_tuning.weightShack;
    m_generationSettings.weightFourLane = m_tuning.weightFourLane;
    m_generationSettings.weightFillerA = m_tuning.weightFillerA;
    m_generationSettings.weightFillerB = m_tuning.weightFillerB;
    m_generationSettings.weightLongWall = m_tuning.weightLongWall;
    m_generationSettings.weightShortWall = m_tuning.weightShortWall;
    m_generationSettings.weightLWallWindow = m_tuning.weightLWallWindow;
    m_generationSettings.weightLWallPallet = m_tuning.weightLWallPallet;
    m_generationSettings.weightTWalls = m_tuning.weightTWalls;
    m_generationSettings.weightGymBox = m_tuning.weightGymBox;
    m_generationSettings.weightDebrisPile = m_tuning.weightDebrisPile;
    m_generationSettings.maxLoops = m_tuning.maxLoopsPerMap;
    m_generationSettings.minLoopDistanceTiles = m_tuning.minLoopDistanceTiles;
    m_generationSettings.maxSafePallets = m_tuning.maxSafePallets;
    m_generationSettings.maxDeadzoneTiles = m_tuning.maxDeadzoneTiles;
    m_generationSettings.edgeBiasLoops = m_tuning.edgeBiasLoops;
    m_generationSettings.disableWindowsAndPallets = m_tuning.disableWindowsAndPallets;

    if (m_generationSettings.disableWindowsAndPallets)
    {
        // Zero out loop types that rely on windows/pallets
        m_generationSettings.weightJungleGymLong = 0.0F;
        m_generationSettings.weightJungleGymShort = 0.0F;
        m_generationSettings.weightLWallWindow = 0.0F;
        m_generationSettings.weightLWallPallet = 0.0F;
        m_generationSettings.weightShortWall = 0.0F;
        m_generationSettings.weightGymBox = 0.0F;

        // Boost wall-only loop types
        m_generationSettings.weightLongWall = 1.6F;
        m_generationSettings.weightTWalls = 1.4F;
        m_generationSettings.weightDebrisPile = 1.2F;
        m_generationSettings.weightTLWalls = 1.2F;
    }

    auto applyRole = [&](engine::scene::Entity entity, bool survivor) {
        auto actorIt = m_world.Actors().find(entity);
        if (actorIt == m_world.Actors().end())
        {
            return;
        }

        engine::scene::ActorComponent& actor = actorIt->second;
        if (survivor)
        {
            actor.walkSpeed = m_tuning.survivorWalkSpeed * m_survivorSpeedPercent;
            actor.sprintSpeed = m_tuning.survivorSprintSpeed * m_survivorSpeedPercent;
            actor.capsuleRadius = m_tuning.survivorCapsuleRadius;
            actor.capsuleHeight = m_tuning.survivorCapsuleHeight;
        }
        else
        {
            actor.walkSpeed = m_tuning.killerMoveSpeed * m_killerSpeedPercent;
            actor.sprintSpeed = m_tuning.killerMoveSpeed * m_killerSpeedPercent;
            actor.capsuleRadius = m_tuning.killerCapsuleRadius;
            actor.capsuleHeight = m_tuning.killerCapsuleHeight;
        }
        actor.eyeHeight = std::max(0.8F, actor.capsuleHeight * 0.88F);
    };

    applyRole(m_survivor, true);
    applyRole(m_killer, false);
}

GameplaySystems::GameplayTuning GameplaySystems::GetGameplayTuning() const
{
    return m_tuning;
}

void GameplaySystems::SetNetworkAuthorityMode(bool enabled)
{
    m_networkAuthorityMode = enabled;
    if (!enabled)
    {
        ClearRemoteRoleCommands();
    }
}

void GameplaySystems::SetRemoteRoleCommand(engine::scene::Role role, const RoleCommand& command)
{
    if (role == engine::scene::Role::Survivor)
    {
        m_remoteSurvivorCommand = command;
    }
    else
    {
        m_remoteKillerCommand = command;
    }
}

void GameplaySystems::ClearRemoteRoleCommands()
{
    m_remoteSurvivorCommand.reset();
    m_remoteKillerCommand.reset();
}

GameplaySystems::Snapshot GameplaySystems::BuildSnapshot() const
{
    Snapshot snapshot;
    snapshot.mapType = m_currentMap;
    snapshot.seed = m_generationSeed;
    snapshot.survivorPerkIds = m_survivorPerks.perkIds;
    snapshot.killerPerkIds = m_killerPerks.perkIds;
    snapshot.survivorCharacterId = m_selectedSurvivorCharacterId;
    snapshot.killerCharacterId = m_selectedKillerCharacterId;
    snapshot.survivorItemId = m_survivorLoadout.itemId;
    snapshot.survivorItemAddonA = m_survivorLoadout.addonAId;
    snapshot.survivorItemAddonB = m_survivorLoadout.addonBId;
    snapshot.killerPowerId = m_killerLoadout.powerId;
    snapshot.killerPowerAddonA = m_killerLoadout.addonAId;
    snapshot.killerPowerAddonB = m_killerLoadout.addonBId;
    snapshot.survivorState = static_cast<std::uint8_t>(m_survivorState);
    snapshot.killerAttackState = static_cast<std::uint8_t>(m_killerAttackState);
    snapshot.killerAttackStateTimer = m_killerAttackStateTimer;
    snapshot.killerLungeCharge = m_killerLungeChargeSeconds;
    snapshot.chaseActive = m_chase.isChasing;
    snapshot.chaseDistance = m_chase.distance;
    snapshot.chaseLos = m_chase.hasLineOfSight;
    snapshot.chaseInCenterFOV = m_chase.inCenterFOV;
    snapshot.chaseTimeSinceLOS = m_chase.timeSinceSeenLOS;
    snapshot.chaseTimeSinceCenterFOV = m_chase.timeSinceCenterFOV;
    snapshot.chaseTimeInChase = m_chase.timeInChase;
    snapshot.bloodlustTier = static_cast<std::uint8_t>(m_bloodlust.tier);
    snapshot.survivorItemCharges = m_survivorItemState.charges;
    snapshot.survivorItemActive = m_survivorItemState.active ? 1U : 0U;
    snapshot.survivorItemUsesRemaining = static_cast<std::uint8_t>(glm::clamp(m_survivorItemState.mapUsesRemaining, 0, 255));
    snapshot.wraithCloaked = m_killerPowerState.wraithCloaked ? 1U : 0U;
    snapshot.wraithTransitionTimer = m_killerPowerState.wraithTransitionTimer;
    snapshot.wraithPostUncloakTimer = m_killerPowerState.wraithPostUncloakTimer;
    snapshot.killerBlindTimer = m_killerPowerState.killerBlindTimer;
    snapshot.killerBlindStyleWhite = static_cast<std::uint8_t>(m_tuning.flashlightBlindStyle == 0 ? 1 : 0);
    snapshot.carriedTrapCount = static_cast<std::uint8_t>(glm::clamp(m_killerPowerState.trapperCarriedTraps, 0, 255));

    auto fillActor = [&](engine::scene::Entity entity, ActorSnapshot& outActor) {
        const auto transformIt = m_world.Transforms().find(entity);
        const auto actorIt = m_world.Actors().find(entity);
        if (transformIt == m_world.Transforms().end() || actorIt == m_world.Actors().end())
        {
            return;
        }

        outActor.position = transformIt->second.position;
        outActor.forward = transformIt->second.forward;
        outActor.velocity = actorIt->second.velocity;
        outActor.yaw = transformIt->second.rotationEuler.y;
        outActor.pitch = transformIt->second.rotationEuler.x;
    };

    fillActor(m_survivor, snapshot.survivor);
    fillActor(m_killer, snapshot.killer);

    snapshot.pallets.reserve(m_world.Pallets().size());
    for (const auto& [entity, pallet] : m_world.Pallets())
    {
        const auto transformIt = m_world.Transforms().find(entity);
        if (transformIt == m_world.Transforms().end())
        {
            continue;
        }

        PalletSnapshot palletSnapshot;
        palletSnapshot.entity = entity;
        palletSnapshot.state = static_cast<std::uint8_t>(pallet.state);
        palletSnapshot.breakTimer = pallet.breakTimer;
        palletSnapshot.position = transformIt->second.position;
        palletSnapshot.halfExtents = pallet.halfExtents;
        snapshot.pallets.push_back(palletSnapshot);
    }

    snapshot.traps.reserve(m_world.BearTraps().size());
    for (const auto& [entity, trap] : m_world.BearTraps())
    {
        const auto transformIt = m_world.Transforms().find(entity);
        if (transformIt == m_world.Transforms().end())
        {
            continue;
        }
        TrapSnapshot trapSnapshot;
        trapSnapshot.entity = entity;
        trapSnapshot.state = static_cast<std::uint8_t>(trap.state);
        trapSnapshot.trappedEntity = trap.trappedEntity;
        trapSnapshot.position = transformIt->second.position;
        trapSnapshot.halfExtents = trap.halfExtents;
        trapSnapshot.escapeChance = trap.escapeChance;
        trapSnapshot.escapeAttempts = static_cast<std::uint8_t>(glm::clamp(trap.escapeAttempts, 0, 255));
        trapSnapshot.maxEscapeAttempts = static_cast<std::uint8_t>(glm::clamp(trap.maxEscapeAttempts, 0, 255));
        snapshot.traps.push_back(trapSnapshot);
    }

    snapshot.groundItems.reserve(m_world.GroundItems().size());
    for (const auto& [entity, groundItem] : m_world.GroundItems())
    {
        const auto transformIt = m_world.Transforms().find(entity);
        if (transformIt == m_world.Transforms().end())
        {
            continue;
        }
        GroundItemSnapshot itemSnapshot;
        itemSnapshot.entity = entity;
        itemSnapshot.position = transformIt->second.position;
        itemSnapshot.charges = groundItem.charges;
        itemSnapshot.itemId = groundItem.itemId;
        itemSnapshot.addonAId = groundItem.addonAId;
        itemSnapshot.addonBId = groundItem.addonBId;
        snapshot.groundItems.push_back(itemSnapshot);
    }

    return snapshot;
}

void GameplaySystems::ApplySnapshot(const Snapshot& snapshot, float blendAlpha)
{
    // Apply perk loadouts if different
    if (snapshot.survivorPerkIds != m_survivorPerks.perkIds)
    {
        m_survivorPerks.perkIds = snapshot.survivorPerkIds;
        m_perkSystem.SetSurvivorLoadout(m_survivorPerks);
        m_perkSystem.InitializeActiveStates();
    }

    if (snapshot.killerPerkIds != m_killerPerks.perkIds)
    {
        m_killerPerks.perkIds = snapshot.killerPerkIds;
        m_perkSystem.SetKillerLoadout(m_killerPerks);
        m_perkSystem.InitializeActiveStates();
    }

    if (snapshot.mapType != m_currentMap || snapshot.seed != m_generationSeed)
    {
        BuildSceneFromMap(snapshot.mapType, snapshot.seed);
    }

    if (!snapshot.survivorCharacterId.empty())
    {
        m_selectedSurvivorCharacterId = snapshot.survivorCharacterId;
    }
    if (!snapshot.killerCharacterId.empty())
    {
        m_selectedKillerCharacterId = snapshot.killerCharacterId;
    }

    m_survivorLoadout.itemId = snapshot.survivorItemId;
    m_survivorLoadout.addonAId = snapshot.survivorItemAddonA;
    m_survivorLoadout.addonBId = snapshot.survivorItemAddonB;
    m_killerLoadout.powerId = snapshot.killerPowerId;
    m_killerLoadout.addonAId = snapshot.killerPowerAddonA;
    m_killerLoadout.addonBId = snapshot.killerPowerAddonB;
    RefreshLoadoutModifiers();
    m_survivorItemState.charges = snapshot.survivorItemCharges;
    m_survivorItemState.active = snapshot.survivorItemActive != 0U;
    m_survivorItemState.mapUsesRemaining = static_cast<int>(snapshot.survivorItemUsesRemaining);
    m_killerPowerState.wraithCloaked = snapshot.wraithCloaked != 0U;
    m_killerPowerState.wraithTransitionTimer = snapshot.wraithTransitionTimer;
    m_killerPowerState.wraithPostUncloakTimer = snapshot.wraithPostUncloakTimer;
    m_killerPowerState.killerBlindTimer = snapshot.killerBlindTimer;
    m_tuning.flashlightBlindStyle = snapshot.killerBlindStyleWhite != 0U ? 0 : 1;
    m_killerPowerState.trapperCarriedTraps = static_cast<int>(snapshot.carriedTrapCount);

    m_chase.isChasing = snapshot.chaseActive;
    m_chase.distance = snapshot.chaseDistance;
    m_chase.hasLineOfSight = snapshot.chaseLos;
    m_chase.inCenterFOV = snapshot.chaseInCenterFOV;
    m_chase.timeSinceSeenLOS = snapshot.chaseTimeSinceLOS;
    m_chase.timeSinceCenterFOV = snapshot.chaseTimeSinceCenterFOV;
    m_chase.timeInChase = snapshot.chaseTimeInChase;
    m_bloodlust.tier = static_cast<int>(snapshot.bloodlustTier);

    const SurvivorHealthState nextState = static_cast<SurvivorHealthState>(
        glm::clamp(static_cast<int>(snapshot.survivorState), 0, static_cast<int>(SurvivorHealthState::Dead))
    );
    m_survivorState = nextState;
    m_killerAttackState = static_cast<KillerAttackState>(
        glm::clamp(static_cast<int>(snapshot.killerAttackState), 0, static_cast<int>(KillerAttackState::Recovering))
    );
    m_killerAttackStateTimer = snapshot.killerAttackStateTimer;
    m_killerLungeChargeSeconds = snapshot.killerLungeCharge;

    auto applyActor = [&](engine::scene::Entity entity, const ActorSnapshot& actorSnapshot) {
        auto transformIt = m_world.Transforms().find(entity);
        auto actorIt = m_world.Actors().find(entity);
        if (transformIt == m_world.Transforms().end() || actorIt == m_world.Actors().end())
        {
            return;
        }

        transformIt->second.position = glm::mix(transformIt->second.position, actorSnapshot.position, blendAlpha);
        transformIt->second.rotationEuler.y = actorSnapshot.yaw;
        transformIt->second.rotationEuler.x = actorSnapshot.pitch;
        transformIt->second.forward = glm::length(actorSnapshot.forward) > 1.0e-4F
                                          ? glm::normalize(actorSnapshot.forward)
                                          : ForwardFromYawPitch(actorSnapshot.yaw, actorSnapshot.pitch);
        actorIt->second.velocity = actorSnapshot.velocity;
        actorIt->second.carried = (entity == m_survivor && m_survivorState == SurvivorHealthState::Carried);
    };

    applyActor(m_survivor, snapshot.survivor);
    applyActor(m_killer, snapshot.killer);

    for (const PalletSnapshot& palletSnapshot : snapshot.pallets)
    {
        auto palletIt = m_world.Pallets().find(palletSnapshot.entity);
        auto transformIt = m_world.Transforms().find(palletSnapshot.entity);
        if (palletIt == m_world.Pallets().end() || transformIt == m_world.Transforms().end())
        {
            continue;
        }

        palletIt->second.state = static_cast<engine::scene::PalletState>(
            glm::clamp(static_cast<int>(palletSnapshot.state), 0, static_cast<int>(engine::scene::PalletState::Broken))
        );
        palletIt->second.breakTimer = palletSnapshot.breakTimer;
        palletIt->second.halfExtents = palletSnapshot.halfExtents;
        transformIt->second.position = glm::mix(transformIt->second.position, palletSnapshot.position, blendAlpha);
    }

    std::unordered_set<engine::scene::Entity> seenTraps;
    for (const TrapSnapshot& trapSnapshot : snapshot.traps)
    {
        seenTraps.insert(trapSnapshot.entity);

        auto transformIt = m_world.Transforms().find(trapSnapshot.entity);
        if (transformIt == m_world.Transforms().end())
        {
            m_world.Transforms()[trapSnapshot.entity] = engine::scene::Transform{
                trapSnapshot.position,
                glm::vec3{0.0F},
                glm::vec3{1.0F},
                glm::vec3{0.0F, 0.0F, 1.0F},
            };
            transformIt = m_world.Transforms().find(trapSnapshot.entity);
        }
        auto trapIt = m_world.BearTraps().find(trapSnapshot.entity);
        if (trapIt == m_world.BearTraps().end())
        {
            m_world.BearTraps()[trapSnapshot.entity] = engine::scene::BearTrapComponent{};
            trapIt = m_world.BearTraps().find(trapSnapshot.entity);
            m_world.Names()[trapSnapshot.entity] = engine::scene::NameComponent{"bear_trap"};
        }

        transformIt->second.position = glm::mix(transformIt->second.position, trapSnapshot.position, blendAlpha);
        trapIt->second.state = static_cast<engine::scene::TrapState>(
            glm::clamp(static_cast<int>(trapSnapshot.state), 0, static_cast<int>(engine::scene::TrapState::Disarmed))
        );
        trapIt->second.trappedEntity = trapSnapshot.trappedEntity;
        trapIt->second.halfExtents = trapSnapshot.halfExtents;
        trapIt->second.escapeChance = trapSnapshot.escapeChance;
        trapIt->second.escapeAttempts = static_cast<int>(trapSnapshot.escapeAttempts);
        trapIt->second.maxEscapeAttempts = static_cast<int>(trapSnapshot.maxEscapeAttempts);
    }

    std::vector<engine::scene::Entity> removeTraps;
    removeTraps.reserve(m_world.BearTraps().size());
    for (const auto& [entity, _] : m_world.BearTraps())
    {
        if (!seenTraps.contains(entity))
        {
            removeTraps.push_back(entity);
        }
    }
    for (engine::scene::Entity entity : removeTraps)
    {
        DestroyEntity(entity);
    }

    std::unordered_set<engine::scene::Entity> seenGroundItems;
    for (const GroundItemSnapshot& itemSnapshot : snapshot.groundItems)
    {
        seenGroundItems.insert(itemSnapshot.entity);

        auto transformIt = m_world.Transforms().find(itemSnapshot.entity);
        if (transformIt == m_world.Transforms().end())
        {
            m_world.Transforms()[itemSnapshot.entity] = engine::scene::Transform{
                itemSnapshot.position,
                glm::vec3{0.0F},
                glm::vec3{1.0F},
                glm::vec3{0.0F, 0.0F, 1.0F},
            };
            transformIt = m_world.Transforms().find(itemSnapshot.entity);
        }
        auto itemIt = m_world.GroundItems().find(itemSnapshot.entity);
        if (itemIt == m_world.GroundItems().end())
        {
            m_world.GroundItems()[itemSnapshot.entity] = engine::scene::GroundItemComponent{};
            itemIt = m_world.GroundItems().find(itemSnapshot.entity);
            m_world.Names()[itemSnapshot.entity] = engine::scene::NameComponent{"ground_item"};
        }

        transformIt->second.position = glm::mix(transformIt->second.position, itemSnapshot.position, blendAlpha);
        itemIt->second.itemId = itemSnapshot.itemId;
        itemIt->second.charges = itemSnapshot.charges;
        itemIt->second.addonAId = itemSnapshot.addonAId;
        itemIt->second.addonBId = itemSnapshot.addonBId;
        itemIt->second.pickupEnabled = true;
    }

    std::vector<engine::scene::Entity> removeGroundItems;
    removeGroundItems.reserve(m_world.GroundItems().size());
    for (const auto& [entity, _] : m_world.GroundItems())
    {
        if (!seenGroundItems.contains(entity))
        {
            removeGroundItems.push_back(entity);
        }
    }
    for (engine::scene::Entity entity : removeGroundItems)
    {
        DestroyEntity(entity);
    }

    RebuildPhysicsWorld();
}

void GameplaySystems::StartSkillCheckDebug()
{
    if (m_activeRepairGenerator == 0)
    {
        for (const auto& [entity, generator] : m_world.Generators())
        {
            if (!generator.completed)
            {
                m_activeRepairGenerator = entity;
                break;
            }
        }
    }

    if (m_activeRepairGenerator == 0)
    {
        AddRuntimeMessage("Skillcheck unavailable: no active generator", 1.5F);
        return;
    }

    std::uniform_real_distribution<float> startDist(0.15F, 0.78F);
    std::uniform_real_distribution<float> sizeDist(0.09F, 0.16F);
    const float zoneStart = startDist(m_rng);
    const float zoneSize = sizeDist(m_rng);
    m_skillCheckSuccessStart = zoneStart;
    m_skillCheckSuccessEnd = std::min(0.98F, zoneStart + zoneSize);
    m_skillCheckNeedle = 0.0F;
    m_skillCheckActive = true;
    AddRuntimeMessage("Skillcheck debug started", 1.5F);
}

void GameplaySystems::HealSurvivor()
{
    if (!SetSurvivorState(SurvivorHealthState::Healthy, "Heal"))
    {
        AddRuntimeMessage("Heal rejected for current survivor state", 1.6F);
    }
}

void GameplaySystems::SetSurvivorStateDebug(const std::string& stateName)
{
    SurvivorHealthState next = m_survivorState;
    if (stateName == "healthy")
    {
        next = SurvivorHealthState::Healthy;
    }
    else if (stateName == "injured")
    {
        next = SurvivorHealthState::Injured;
    }
    else if (stateName == "downed")
    {
        next = SurvivorHealthState::Downed;
    }
    else if (stateName == "trapped")
    {
        next = SurvivorHealthState::Trapped;
    }
    else if (stateName == "carried")
    {
        next = SurvivorHealthState::Carried;
    }
    else if (stateName == "hooked")
    {
        next = SurvivorHealthState::Hooked;
    }
    else if (stateName == "dead")
    {
        next = SurvivorHealthState::Dead;
    }
    else
    {
        AddRuntimeMessage("Unknown survivor state", 1.6F);
        return;
    }

    SetSurvivorState(next, "Debug force", true);
}

void GameplaySystems::SetGeneratorsCompleted(int completed)
{
    const int clamped = glm::clamp(completed, 0, m_generatorsTotal);
    int index = 0;
    for (auto& [_, generator] : m_world.Generators())
    {
        const bool done = index < clamped;
        generator.completed = done;
        generator.progress = done ? 1.0F : 0.0F;
        ++index;
    }
    RefreshGeneratorsCompleted();
}

void GameplaySystems::HookCarriedSurvivorDebug()
{
    if (m_survivorState != SurvivorHealthState::Carried)
    {
        AddRuntimeMessage("Hook debug failed: survivor is not carried", 1.6F);
        return;
    }

    engine::scene::Entity hookEntity = 0;
    if (!m_world.Hooks().empty())
    {
        hookEntity = m_world.Hooks().begin()->first;
    }
    TryHookCarriedSurvivor(hookEntity);
}

void GameplaySystems::RequestQuit()
{
    m_quitRequested = true;
}

void GameplaySystems::SpawnFxDebug(const std::string& assetId)
{
    glm::vec3 forward = m_cameraForward;
    const engine::scene::Entity controlled = ControlledEntity();
    const auto controlledTransformIt = m_world.Transforms().find(controlled);
    if (controlledTransformIt != m_world.Transforms().end() && glm::length(controlledTransformIt->second.forward) > 1.0e-5F)
    {
        forward = controlledTransformIt->second.forward;
    }
    if (glm::length(forward) <= 1.0e-5F)
    {
        forward = glm::vec3{0.0F, 0.0F, -1.0F};
    }
    const glm::vec3 origin = m_cameraInitialized ? (m_cameraPosition + m_cameraForward * 1.8F) : glm::vec3{0.0F, 1.0F, 0.0F};
    SpawnGameplayFx(assetId, origin, forward, engine::fx::FxNetMode::Local);
}

void GameplaySystems::StopAllFx()
{
    m_fxSystem.StopAll();
    m_chaseAuraFxId = 0;
}

std::vector<std::string> GameplaySystems::ListFxAssets() const
{
    return m_fxSystem.ListAssetIds();
}

std::optional<engine::fx::FxAsset> GameplaySystems::GetFxAsset(const std::string& assetId) const
{
    return m_fxSystem.GetAsset(assetId);
}

bool GameplaySystems::SaveFxAsset(const engine::fx::FxAsset& asset, std::string* outError)
{
    return m_fxSystem.SaveAsset(asset, outError);
}

void GameplaySystems::SetFxReplicationCallback(std::function<void(const engine::fx::FxSpawnEvent&)> callback)
{
    m_fxReplicationCallback = std::move(callback);
}

void GameplaySystems::SpawnReplicatedFx(const engine::fx::FxSpawnEvent& event)
{
    m_fxSystem.Spawn(event.assetId, event.position, event.forward, {}, engine::fx::FxNetMode::Local);
}

void GameplaySystems::BuildSceneFromMap(MapType mapType, unsigned int seed)
{
    game::maps::TileGenerator generator;
    game::maps::GeneratedMap generated;

    if (mapType == MapType::Test)
    {
        generated = generator.GenerateTestMap();
    }
    else if (mapType == MapType::Main)
    {
        generated = generator.GenerateMainMap(seed, m_generationSettings);
        // Apply DBD-inspired spawn system if enabled
        if (m_dbdSpawnsEnabled)
        {
            generator.CalculateDbdSpawns(generated, seed);
        }
    }
    else
    {
        generated = generator.GenerateCollisionTestMap();
    }

    BuildSceneFromGeneratedMap(generated, mapType, seed, MapToName(mapType));
}

void GameplaySystems::BuildSceneFromGeneratedMap(
    const ::game::maps::GeneratedMap& generated,
    MapType mapType,
    unsigned int seed,
    const std::string& mapDisplayName
)
{
    m_currentMap = mapType;
    m_generationSeed = seed;
    m_activeMapName = mapDisplayName.empty() ? MapToName(mapType) : mapDisplayName;
    m_survivor = 0;
    m_killer = 0;
    m_killerBreakingPallet = 0;
    m_lastHitRayStart = glm::vec3{0.0F};
    m_lastHitRayEnd = glm::vec3{0.0F};
    m_lastHitConnected = false;
    m_lastSwingOrigin = glm::vec3{0.0F};
    m_lastSwingDirection = glm::vec3{0.0F, 0.0F, -1.0F};
    m_lastSwingRange = 0.0F;
    m_lastSwingHalfAngleRadians = 0.0F;
    m_lastSwingDebugTtl = 0.0F;
    m_fxSystem.StopAll();
    m_chaseAuraFxId = 0;
    m_chase = ChaseState{};
    m_interactionCandidate = InteractionCandidate{};
    m_cameraInitialized = false;
    m_survivorState = SurvivorHealthState::Healthy;
    m_generatorsCompleted = 0;
    m_carryEscapeProgress = 0.0F;
    m_carryLastQteDirection = 0;
    m_hookStage = 0;
    m_hookStageTimer = 0.0F;
    m_hookEscapeAttemptsUsed = 0;
    m_hookSkillCheckTimeToNext = 0.0F;
    m_activeHookEntity = 0;
    m_activeRepairGenerator = 0;
    m_selfHealActive = false;
    m_selfHealProgress = 0.0F;
    m_skillCheckActive = false;
    m_skillCheckMode = SkillCheckMode::None;
    m_skillCheckNeedle = 0.0F;
    m_skillCheckSuccessStart = 0.0F;
    m_skillCheckSuccessEnd = 0.0F;
    m_skillCheckTimeToNext = 2.0F;
    m_interactBufferRemaining = std::array<float, 2>{0.0F, 0.0F};
    m_survivorWigglePressQueue.clear();
    m_localSurvivorCommand = RoleCommand{};
    m_localKillerCommand = RoleCommand{};
    m_remoteSurvivorCommand.reset();
    m_remoteKillerCommand.reset();
    m_killerAttackState = KillerAttackState::Idle;
    m_killerAttackStateTimer = 0.0F;
    m_killerLungeChargeSeconds = 0.0F;
    m_killerAttackFlashTtl = 0.0F;
    m_killerAttackHitThisAction = false;
    m_previousAttackHeld = false;
    m_killerCurrentLungeSpeed = 0.0F;
    m_survivorHitHasteTimer = 0.0F;
    m_killerSlowTimer = 0.0F;
    m_killerSlowMultiplier = 1.0F;
    m_carryInputGraceTimer = 0.0F;
    m_mapRevealGenerators.clear();
    m_killerPowerState = KillerPowerRuntimeState{};

    m_world.Clear();
    m_loopDebugTiles.clear();
    m_spawnPoints.clear();
    m_nextSpawnPointId = 1;

    m_loopDebugTiles.reserve(generated.tiles.size());
    for (const auto& tile : generated.tiles)
    {
        m_loopDebugTiles.push_back(LoopDebugTile{
            tile.center,
            tile.halfExtents,
            tile.loopId,
            tile.archetype,
        });
    }

    for (const auto& wall : generated.walls)
    {
        const engine::scene::Entity wallEntity = m_world.CreateEntity();
        m_world.Transforms()[wallEntity] = engine::scene::Transform{
            wall.center,
            glm::vec3{0.0F},
            glm::vec3{1.0F},
            glm::vec3{0.0F, 0.0F, 1.0F},
        };
        m_world.StaticBoxes()[wallEntity] = engine::scene::StaticBoxComponent{wall.halfExtents, true};
    }

    m_staticBatcher.BeginBuild();
    for (const auto& wall : generated.walls)
    {
        m_staticBatcher.AddBox(wall.center, wall.halfExtents, glm::vec3{0.58F, 0.62F, 0.68F});
    }
    m_staticBatcher.EndBuild();

    m_spawnPoints.push_back(SpawnPointInfo{
        m_nextSpawnPointId++,
        SpawnPointType::Survivor,
        generated.survivorSpawn,
    });
    m_spawnPoints.push_back(SpawnPointInfo{
        m_nextSpawnPointId++,
        SpawnPointType::Killer,
        generated.killerSpawn,
    });
    const glm::vec3 centerSpawn = (generated.survivorSpawn + generated.killerSpawn) * 0.5F;
    m_spawnPoints.push_back(SpawnPointInfo{
        m_nextSpawnPointId++,
        SpawnPointType::Generic,
        centerSpawn,
    });
    for (const auto& tile : generated.tiles)
    {
        m_spawnPoints.push_back(SpawnPointInfo{
            m_nextSpawnPointId++,
            SpawnPointType::Generic,
            tile.center + glm::vec3{0.0F, 1.05F, 0.0F},
        });
    }

    for (const auto& windowSpawn : generated.windows)
    {
        const engine::scene::Entity windowEntity = m_world.CreateEntity();
        m_world.Transforms()[windowEntity] = engine::scene::Transform{
            windowSpawn.center,
            glm::vec3{0.0F},
            glm::vec3{1.0F},
            windowSpawn.normal,
        };

        engine::scene::WindowComponent window;
        window.halfExtents = windowSpawn.halfExtents;
        window.normal = glm::normalize(windowSpawn.normal);
        window.survivorVaultTime = 0.6F;
        window.killerVaultMultiplier = 1.55F;
        m_world.Windows()[windowEntity] = window;
    }

    for (const auto& palletSpawn : generated.pallets)
    {
        const engine::scene::Entity palletEntity = m_world.CreateEntity();
        m_world.Transforms()[palletEntity] = engine::scene::Transform{
            palletSpawn.center,
            glm::vec3{0.0F},
            glm::vec3{1.0F},
            glm::vec3{1.0F, 0.0F, 0.0F},
        };

        engine::scene::PalletComponent pallet;
        const bool xMajor = palletSpawn.halfExtents.x >= palletSpawn.halfExtents.z;
        pallet.standingHalfExtents = xMajor
                                         ? glm::vec3{std::max(0.24F, palletSpawn.halfExtents.x), 1.08F, 0.24F}
                                         : glm::vec3{0.24F, 1.08F, std::max(0.24F, palletSpawn.halfExtents.z)};
        pallet.droppedHalfExtents = xMajor
                                        ? glm::vec3{std::max(0.9F, palletSpawn.halfExtents.x), 0.58F, 0.34F}
                                        : glm::vec3{0.34F, 0.58F, std::max(0.9F, palletSpawn.halfExtents.z)};
        pallet.halfExtents = pallet.standingHalfExtents;
        pallet.standingCenterY = std::max(1.08F, palletSpawn.center.y);
        pallet.droppedCenterY = std::max(0.58F, palletSpawn.center.y * 0.75F);
        pallet.state = engine::scene::PalletState::Standing;
        pallet.breakDuration = 1.8F;
        m_world.Pallets()[palletEntity] = pallet;
        m_world.Transforms()[palletEntity].position.y = pallet.standingCenterY;
    }

    const std::array<glm::vec3, 4> hookOffsets{
        glm::vec3{6.0F, 1.2F, 6.0F},
        glm::vec3{-6.0F, 1.2F, 6.0F},
        glm::vec3{6.0F, 1.2F, -6.0F},
        glm::vec3{-6.0F, 1.2F, -6.0F},
    };
    for (const glm::vec3& offset : hookOffsets)
    {
        const engine::scene::Entity hookEntity = m_world.CreateEntity();
        const glm::vec3 hookPos = (generated.survivorSpawn + generated.killerSpawn) * 0.5F + offset;
        m_world.Transforms()[hookEntity] = engine::scene::Transform{
            hookPos,
            glm::vec3{0.0F},
            glm::vec3{1.0F},
            glm::vec3{0.0F, 0.0F, 1.0F},
        };
        m_world.Hooks()[hookEntity] = engine::scene::HookComponent{};
        m_world.Names()[hookEntity] = engine::scene::NameComponent{"hook"};
    }

    // Spawn generators at positions from the map (attached to loops)
    for (const glm::vec3& generatorPos : generated.generatorSpawns)
    {
        const engine::scene::Entity generatorEntity = m_world.CreateEntity();
        m_world.Transforms()[generatorEntity] = engine::scene::Transform{
            generatorPos,
            glm::vec3{0.0F},
            glm::vec3{1.0F},
            glm::vec3{0.0F, 0.0F, 1.0F},
        };
        m_world.Generators()[generatorEntity] = engine::scene::GeneratorComponent{};
        m_world.Names()[generatorEntity] = engine::scene::NameComponent{"generator"};
    }

    // Use DBD-inspired spawn system if enabled, otherwise use legacy spawns
    if (generated.useDbdSpawns && !generated.survivorSpawns.empty())
    {
        // Use new spawn system positions (currently single survivor for testing)
        m_survivor = SpawnActor(m_world, engine::scene::Role::Survivor, generated.survivorSpawns[0], glm::vec3{0.2F, 0.95F, 0.2F});
    }
    else
    {
        // Legacy spawn system
        m_survivor = SpawnActor(m_world, engine::scene::Role::Survivor, generated.survivorSpawn, glm::vec3{0.2F, 0.95F, 0.2F});
    }
    m_killer = SpawnActor(m_world, engine::scene::Role::Killer, generated.killerSpawn, glm::vec3{0.95F, 0.2F, 0.2F});
    ApplyGameplayTuning(m_tuning);
    SetRoleSpeedPercent("survivor", m_survivorSpeedPercent);
    SetRoleSpeedPercent("killer", m_killerSpeedPercent);
    SetRoleCapsuleSize("survivor", m_tuning.survivorCapsuleRadius, m_tuning.survivorCapsuleHeight);
    SetRoleCapsuleSize("killer", m_tuning.killerCapsuleRadius, m_tuning.killerCapsuleHeight);
    SetSurvivorState(SurvivorHealthState::Healthy, "Map spawn", true);
    ResetItemAndPowerRuntimeState();
    SpawnInitialTrapperGroundTraps();
    m_generatorsTotal = static_cast<int>(m_world.Generators().size());
    RefreshGeneratorsCompleted();

    m_controlledRole = ControlledRole::Survivor;

    RebuildPhysicsWorld();
    UpdateInteractionCandidate();
}

void GameplaySystems::RebuildPhysicsWorld()
{
    m_physics.Clear();

    for (const auto& [entity, box] : m_world.StaticBoxes())
    {
        if (!box.solid)
        {
            continue;
        }

        const auto transformIt = m_world.Transforms().find(entity);
        if (transformIt == m_world.Transforms().end())
        {
            continue;
        }

        m_physics.AddSolidBox(engine::physics::SolidBox{
            .entity = entity,
            .center = transformIt->second.position,
            .halfExtents = box.halfExtents,
            .layer = engine::physics::CollisionLayer::Environment,
            .blocksSight = true,
        });
    }

    for (const auto& [entity, pallet] : m_world.Pallets())
    {
        const auto transformIt = m_world.Transforms().find(entity);
        if (transformIt == m_world.Transforms().end())
        {
            continue;
        }

        if (pallet.state == engine::scene::PalletState::Dropped)
        {
            m_physics.AddSolidBox(engine::physics::SolidBox{
                .entity = entity,
                .center = transformIt->second.position,
                .halfExtents = pallet.halfExtents,
                .layer = engine::physics::CollisionLayer::Environment,
                .blocksSight = false,
            });
        }

        if (pallet.state != engine::scene::PalletState::Broken)
        {
            m_physics.AddTrigger(engine::physics::TriggerVolume{
                .entity = entity,
                .center = transformIt->second.position,
                .halfExtents = pallet.halfExtents + glm::vec3{0.65F, 0.3F, 0.65F},
                .kind = engine::physics::TriggerKind::Interaction,
            });
        }
    }

    for (const auto& [entity, window] : m_world.Windows())
    {
        const auto transformIt = m_world.Transforms().find(entity);
        if (transformIt == m_world.Transforms().end())
        {
            continue;
        }

        m_physics.AddTrigger(engine::physics::TriggerVolume{
            .entity = entity,
            .center = transformIt->second.position,
            .halfExtents = window.halfExtents + glm::vec3{0.8F, 0.35F, 0.8F},
            .kind = engine::physics::TriggerKind::Vault,
        });
    }

    for (const auto& [entity, hook] : m_world.Hooks())
    {
        const auto transformIt = m_world.Transforms().find(entity);
        if (transformIt == m_world.Transforms().end())
        {
            continue;
        }

        m_physics.AddTrigger(engine::physics::TriggerVolume{
            .entity = entity,
            .center = transformIt->second.position,
            .halfExtents = hook.halfExtents + glm::vec3{0.5F, 0.4F, 0.5F},
            .kind = engine::physics::TriggerKind::Interaction,
        });
    }

    for (const auto& [entity, generator] : m_world.Generators())
    {
        const auto transformIt = m_world.Transforms().find(entity);
        if (transformIt == m_world.Transforms().end() || generator.completed)
        {
            continue;
        }

        m_physics.AddTrigger(engine::physics::TriggerVolume{
            .entity = entity,
            .center = transformIt->second.position,
            .halfExtents = generator.halfExtents + glm::vec3{0.3F, 0.2F, 0.3F},  // Zmniejszone: 0.7->0.3, 0.45->0.2
            .kind = engine::physics::TriggerKind::Interaction,
        });
    }

    for (const auto& [entity, trap] : m_world.BearTraps())
    {
        const auto transformIt = m_world.Transforms().find(entity);
        if (transformIt == m_world.Transforms().end())
        {
            continue;
        }

        m_physics.AddTrigger(engine::physics::TriggerVolume{
            .entity = entity,
            .center = transformIt->second.position,
            .halfExtents = trap.halfExtents + glm::vec3{0.35F, 0.25F, 0.35F},
            .kind = engine::physics::TriggerKind::Interaction,
        });
    }

    if (m_killer != 0)
    {
        const auto killerTransformIt = m_world.Transforms().find(m_killer);
        if (killerTransformIt != m_world.Transforms().end())
        {
            m_physics.AddTrigger(engine::physics::TriggerVolume{
                .entity = m_killer,
                .center = killerTransformIt->second.position,
                .halfExtents = glm::vec3{m_chase.startDistance, 2.0F, m_chase.startDistance},
                .kind = engine::physics::TriggerKind::Chase,
            });
        }
    }
}

void GameplaySystems::DestroyEntity(engine::scene::Entity entity)
{
    if (entity == 0)
    {
        return;
    }

    m_world.Transforms().erase(entity);
    m_world.Actors().erase(entity);
    m_world.StaticBoxes().erase(entity);
    m_world.Windows().erase(entity);
    m_world.Pallets().erase(entity);
    m_world.Hooks().erase(entity);
    m_world.Generators().erase(entity);
    m_world.BearTraps().erase(entity);
    m_world.GroundItems().erase(entity);
    m_world.DebugColors().erase(entity);
    m_world.Names().erase(entity);
}

bool GameplaySystems::ResolveSpawnPositionValid(
    const glm::vec3& requestedPosition,
    float radius,
    float height,
    glm::vec3* outResolved
)
{
    RebuildPhysicsWorld();
    const std::array<glm::vec3, 12> offsets{
        glm::vec3{0.0F, 0.0F, 0.0F},
        glm::vec3{0.5F, 0.0F, 0.0F},
        glm::vec3{-0.5F, 0.0F, 0.0F},
        glm::vec3{0.0F, 0.0F, 0.5F},
        glm::vec3{0.0F, 0.0F, -0.5F},
        glm::vec3{1.0F, 0.0F, 0.0F},
        glm::vec3{-1.0F, 0.0F, 0.0F},
        glm::vec3{0.0F, 0.0F, 1.0F},
        glm::vec3{0.0F, 0.0F, -1.0F},
        glm::vec3{0.8F, 0.0F, 0.8F},
        glm::vec3{-0.8F, 0.0F, 0.8F},
        glm::vec3{0.8F, 0.0F, -0.8F},
    };

    for (const glm::vec3& offset : offsets)
    {
        glm::vec3 candidate = requestedPosition + offset;
        for (int i = 0; i < 8; ++i)
        {
            const engine::physics::MoveResult probe = m_physics.MoveCapsule(
                candidate,
                radius,
                height,
                glm::vec3{0.0F},
                true,
                0.0F
            );
            if (!probe.collided)
            {
                if (outResolved != nullptr)
                {
                    *outResolved = probe.position;
                }
                return true;
            }
            candidate.y += 0.25F;
        }
    }

    if (outResolved != nullptr)
    {
        *outResolved = requestedPosition;
    }
    return false;
}

std::optional<GameplaySystems::SpawnPointInfo> GameplaySystems::FindSpawnPointById(int spawnId) const
{
    for (const SpawnPointInfo& spawn : m_spawnPoints)
    {
        if (spawn.id == spawnId)
        {
            return spawn;
        }
    }
    return std::nullopt;
}

std::optional<GameplaySystems::SpawnPointInfo> GameplaySystems::FindSpawnPointByType(SpawnPointType type) const
{
    if (m_spawnPoints.empty())
    {
        return std::nullopt;
    }

    if (type == SpawnPointType::Survivor && m_killer != 0)
    {
        const auto killerTransformIt = m_world.Transforms().find(m_killer);
        if (killerTransformIt != m_world.Transforms().end())
        {
            const glm::vec3 killerPos = killerTransformIt->second.position;
            float bestDistance = -1.0F;
            std::optional<SpawnPointInfo> best;
            for (const SpawnPointInfo& spawn : m_spawnPoints)
            {
                if (spawn.type != SpawnPointType::Survivor && spawn.type != SpawnPointType::Generic)
                {
                    continue;
                }
                const float d = DistanceXZ(spawn.position, killerPos);
                if (d > bestDistance)
                {
                    bestDistance = d;
                    best = spawn;
                }
            }
            if (best.has_value())
            {
                return best;
            }
        }
    }

    for (const SpawnPointInfo& spawn : m_spawnPoints)
    {
        if (spawn.type == type)
        {
            return spawn;
        }
    }

    for (const SpawnPointInfo& spawn : m_spawnPoints)
    {
        if (spawn.type == SpawnPointType::Generic)
        {
            return spawn;
        }
    }

    return std::nullopt;
}

GameplaySystems::SpawnPointType GameplaySystems::SpawnPointTypeFromRole(const std::string& roleName) const
{
    return roleName == "killer" ? SpawnPointType::Killer : SpawnPointType::Survivor;
}

const char* GameplaySystems::SpawnTypeToText(SpawnPointType type) const
{
    switch (type)
    {
        case SpawnPointType::Survivor: return "Survivor";
        case SpawnPointType::Killer: return "Killer";
        case SpawnPointType::Generic: return "Generic";
        default: return "Generic";
    }
}

engine::scene::Entity GameplaySystems::SpawnRoleActorAt(const std::string& roleName, const glm::vec3& position)
{
    const bool killer = roleName == "killer";
    const engine::scene::Role role = killer ? engine::scene::Role::Killer : engine::scene::Role::Survivor;
    const engine::scene::Entity entity = SpawnActor(
        m_world,
        role,
        position,
        killer ? glm::vec3{0.95F, 0.2F, 0.2F} : glm::vec3{0.2F, 0.95F, 0.2F}
    );

    if (killer)
    {
        m_killer = entity;
    }
    else
    {
        m_survivor = entity;
    }

    ApplyGameplayTuning(m_tuning);
    return entity;
}

void GameplaySystems::UpdateActorLook(engine::scene::Entity entity, const glm::vec2& mouseDelta, float sensitivity)
{
    auto transformIt = m_world.Transforms().find(entity);
    if (transformIt == m_world.Transforms().end())
    {
        return;
    }

    engine::scene::Transform& transform = transformIt->second;

    transform.rotationEuler.y += mouseDelta.x * sensitivity;
    transform.rotationEuler.x -= mouseDelta.y * sensitivity;
    transform.rotationEuler.x = glm::clamp(transform.rotationEuler.x, -1.35F, 1.35F);

    transform.forward = ForwardFromYawPitch(transform.rotationEuler.y, transform.rotationEuler.x);
}

void GameplaySystems::UpdateActorMovement(
    engine::scene::Entity entity,
    const glm::vec2& moveAxis,
    bool sprinting,
    bool jumpPressed,
    bool crouchHeld,
    float fixedDt
)
{
    auto transformIt = m_world.Transforms().find(entity);
    auto actorIt = m_world.Actors().find(entity);
    if (transformIt == m_world.Transforms().end() || actorIt == m_world.Actors().end())
    {
        return;
    }

    engine::scene::Transform& transform = transformIt->second;
    engine::scene::ActorComponent& actor = actorIt->second;

    if (actor.stunTimer > 0.0F)
    {
        actor.stunTimer = std::max(0.0F, actor.stunTimer - fixedDt);
    }

    if (actor.vaultCooldown > 0.0F)
    {
        actor.vaultCooldown = std::max(0.0F, actor.vaultCooldown - fixedDt);
    }

    if (actor.vaulting)
    {
        actor.sprinting = false;
        actor.forwardRunupDistance = 0.0F;
        UpdateVaultState(entity, fixedDt);
        return;
    }

    if (actor.carried || actor.stunTimer > 0.0F)
    {
        actor.sprinting = false;
        actor.forwardRunupDistance = 0.0F;
        actor.velocity = glm::vec3{0.0F};
        actor.lastPenetrationDepth = 0.0F;
        actor.lastCollisionNormal = glm::vec3{0.0F, 1.0F, 0.0F};
        return;
    }

    if (entity == m_survivor &&
        (m_survivorState == SurvivorHealthState::Hooked ||
         m_survivorState == SurvivorHealthState::Trapped ||
         m_survivorState == SurvivorHealthState::Dead))
    {
        actor.sprinting = false;
        actor.forwardRunupDistance = 0.0F;
        actor.velocity = glm::vec3{0.0F};
        actor.lastPenetrationDepth = 0.0F;
        actor.lastCollisionNormal = glm::vec3{0.0F, 1.0F, 0.0F};
        return;
    }

    glm::vec3 forwardXZ{0.0F, 0.0F, -1.0F};
    if (entity == ControlledEntity() && m_cameraInitialized)
    {
        const glm::vec3 cameraFlat{m_cameraForward.x, 0.0F, m_cameraForward.z};
        if (glm::length(cameraFlat) > 1.0e-5F)
        {
            forwardXZ = glm::normalize(cameraFlat);
        }
    }
    else
    {
        const float yaw = transform.rotationEuler.y;
        forwardXZ = glm::normalize(glm::vec3{std::sin(yaw), 0.0F, -std::cos(yaw)});
    }
    const glm::vec3 rightXZ = glm::normalize(glm::cross(forwardXZ, glm::vec3{0.0F, 1.0F, 0.0F}));

    glm::vec3 moveDirection{0.0F};
    if (glm::length(moveAxis) > 1.0e-5F)
    {
        moveDirection = glm::normalize(rightXZ * moveAxis.x + forwardXZ * moveAxis.y);
    }

    float speed = actor.walkSpeed;
    actor.crawling = false;
    actor.crouching = false;
    if (actor.role == engine::scene::Role::Survivor && m_survivorState == SurvivorHealthState::Downed)
    {
        speed = m_tuning.survivorCrawlSpeed;
        sprinting = false;
        actor.crawling = true;
    }
    else if (actor.role == engine::scene::Role::Survivor && crouchHeld)
    {
        speed = m_tuning.survivorCrouchSpeed;
        sprinting = false;
        actor.crouching = true;
    }

    if (actor.role == engine::scene::Role::Survivor && sprinting)
    {
        speed = actor.sprintSpeed;
    }

    if (entity == m_survivor &&
        m_survivorHitHasteTimer > 0.0F &&
        (m_survivorState == SurvivorHealthState::Healthy || m_survivorState == SurvivorHealthState::Injured))
    {
        speed *= m_survivorHitHasteMultiplier;
    }
    if (entity == m_killer && m_killerSlowTimer > 0.0F)
    {
        speed *= m_killerSlowMultiplier;
    }
    if (entity == m_killer && m_killerLoadout.powerId == "wraith_cloak")
    {
        float baseCloakMoveSpeedMult = m_tuning.wraithCloakMoveSpeedMultiplier;
        if (const loadout::PowerDefinition* powerDef = m_loadoutCatalog.FindPower(m_killerLoadout.powerId))
        {
            const auto it = powerDef->params.find("cloak_speed_multiplier");
            if (it != powerDef->params.end())
            {
                baseCloakMoveSpeedMult = it->second;
            }
        }
        const float cloakMoveSpeedMult = std::max(
            1.0F,
            m_killerPowerModifiers.ApplyStat("cloak_speed_multiplier", baseCloakMoveSpeedMult)
        );
        if (m_killerPowerState.wraithCloaked || m_killerPowerState.wraithPostUncloakTimer > 0.0F)
        {
            speed *= cloakMoveSpeedMult;
        }
    }

    // Apply perk speed modifiers
    speed *= m_perkSystem.GetSpeedModifier(actor.role, sprinting, crouchHeld, actor.crawling);

    actor.sprinting = actor.role == engine::scene::Role::Survivor && sprinting;

    actor.velocity.x = moveDirection.x * speed;
    actor.velocity.z = moveDirection.z * speed;

    if (entity == m_killer && m_killerAttackState == KillerAttackState::Lunging)
    {
        const glm::vec3 killerForwardXZ = glm::normalize(glm::vec3{transform.forward.x, 0.0F, transform.forward.z});
        actor.velocity.x = killerForwardXZ.x * m_killerCurrentLungeSpeed;
        actor.velocity.z = killerForwardXZ.z * m_killerCurrentLungeSpeed;
    }

    if (glm::length(moveDirection) > 1.0e-5F && glm::dot(moveDirection, forwardXZ) > 0.72F)
    {
        actor.forwardRunupDistance = std::min(actor.forwardRunupDistance + speed * fixedDt, 12.0F);
    }
    else
    {
        actor.forwardRunupDistance = 0.0F;
    }

    if (actor.noclipEnabled || m_noClipEnabled)
    {
        transform.position += moveDirection * speed * fixedDt;
        actor.grounded = false;
        actor.lastPenetrationDepth = 0.0F;
        actor.lastCollisionNormal = glm::vec3{0.0F, 1.0F, 0.0F};
        return;
    }

    if (actor.jumpEnabled && jumpPressed && actor.grounded)
    {
        actor.velocity.y = actor.jumpVelocity;
    }

    actor.velocity.y += kGravity * fixedDt;

    const engine::physics::MoveResult moveResult = m_physics.MoveCapsule(
        transform.position,
        actor.capsuleRadius,
        actor.capsuleHeight,
        actor.velocity * fixedDt,
        m_collisionEnabled && actor.collisionEnabled,
        actor.stepHeight
    );

    transform.position = moveResult.position;
    actor.grounded = moveResult.grounded;
    actor.lastCollisionNormal = moveResult.lastCollisionNormal;
    actor.lastPenetrationDepth = moveResult.maxPenetrationDepth;

    if (actor.grounded && actor.velocity.y < 0.0F)
    {
        actor.velocity.y = 0.0F;
    }

    if (moveResult.collided)
    {
        const float velocityIntoNormal = glm::dot(actor.velocity, moveResult.lastCollisionNormal);
        if (velocityIntoNormal < 0.0F)
        {
            actor.velocity -= moveResult.lastCollisionNormal * velocityIntoNormal;
        }
    }
}

void GameplaySystems::UpdateVaultState(engine::scene::Entity entity, float fixedDt)
{
    auto transformIt = m_world.Transforms().find(entity);
    auto actorIt = m_world.Actors().find(entity);
    if (transformIt == m_world.Transforms().end() || actorIt == m_world.Actors().end())
    {
        return;
    }

    engine::scene::Transform& transform = transformIt->second;
    engine::scene::ActorComponent& actor = actorIt->second;

    actor.vaultTimer += fixedDt;
    const float normalized = actor.vaultDuration > 0.0F ? glm::clamp(actor.vaultTimer / actor.vaultDuration, 0.0F, 1.0F) : 1.0F;

    const glm::vec3 linear = glm::mix(actor.vaultStart, actor.vaultEnd, normalized);
    const float arc = std::sin(normalized * kPi) * actor.vaultArcHeight;
    transform.position = linear + glm::vec3{0.0F, arc, 0.0F};

    if (normalized >= 1.0F)
    {
        actor.vaulting = false;
        actor.sprinting = false;
        actor.vaultTimer = 0.0F;
        actor.collisionEnabled = m_collisionEnabled;
        actor.vaultCooldown = 0.5F;
        AddRuntimeMessage("Vault ended", 1.5F);
    }
}

void GameplaySystems::UpdateInteractionCandidate()
{
    const engine::scene::Entity controlled = ControlledEntity();
    const auto actorIt = m_world.Actors().find(controlled);
    if (controlled == 0 || actorIt == m_world.Actors().end() || IsActorInputLocked(actorIt->second))
    {
        m_interactionCandidate = InteractionCandidate{};
        m_interactionPromptHoldSeconds = 0.0F;
        return;
    }
    if (controlled == m_survivor &&
        (m_survivorState == SurvivorHealthState::Downed ||
         m_survivorState == SurvivorHealthState::Trapped ||
         m_survivorState == SurvivorHealthState::Hooked ||
         m_survivorState == SurvivorHealthState::Dead))
    {
        m_interactionCandidate = InteractionCandidate{};
        m_interactionPromptHoldSeconds = 0.0F;
        return;
    }

    const InteractionCandidate resolved = ResolveInteractionCandidateFromView(controlled);
    if (resolved.type != InteractionType::None)
    {
        m_interactionCandidate = resolved;
        m_interactionPromptHoldSeconds = 0.2F;
    }
    else if (m_interactionPromptHoldSeconds > 0.0F && !m_interactionCandidate.prompt.empty())
    {
        m_interactionPromptHoldSeconds = std::max(0.0F, m_interactionPromptHoldSeconds - (1.0F / 60.0F));
    }
    else
    {
        m_interactionCandidate = InteractionCandidate{};
        m_interactionPromptHoldSeconds = 0.0F;
    }
}

void GameplaySystems::ExecuteInteractionForRole(engine::scene::Entity actorEntity, const InteractionCandidate& candidate)
{
    if (actorEntity == 0 || candidate.type == InteractionType::None)
    {
        return;
    }

    auto actorTransformIt = m_world.Transforms().find(actorEntity);
    if (actorTransformIt == m_world.Transforms().end() || !m_world.Actors().contains(actorEntity))
    {
        return;
    }

    auto snapActorToAnchor = [&](const glm::vec3& anchor, float maxSnapDistance) {
        engine::scene::Transform& actorTransform = actorTransformIt->second;
        const glm::vec3 actorAnchor = actorTransform.position;
        const float distance = DistanceXZ(actorAnchor, anchor);
        if (distance <= maxSnapDistance)
        {
            actorTransform.position.x = anchor.x;
            actorTransform.position.z = anchor.z;
        }
    };

    if (candidate.type == InteractionType::WindowVault)
    {
        const auto windowIt = m_world.Windows().find(candidate.entity);
        const auto windowTransformIt = m_world.Transforms().find(candidate.entity);
        if (windowIt != m_world.Windows().end() && windowTransformIt != m_world.Transforms().end())
        {
            const glm::vec3 normal = glm::length(windowIt->second.normal) > 1.0e-5F
                                         ? glm::normalize(windowIt->second.normal)
                                         : glm::vec3{0.0F, 0.0F, 1.0F};
            const float side = glm::dot(actorTransformIt->second.position - windowTransformIt->second.position, normal) >= 0.0F ? 1.0F : -1.0F;
            const float windowThicknessAlongNormal =
                std::abs(normal.x) * windowIt->second.halfExtents.x +
                std::abs(normal.y) * windowIt->second.halfExtents.y +
                std::abs(normal.z) * windowIt->second.halfExtents.z;
            const glm::vec3 anchor = windowTransformIt->second.position + normal * side * (windowThicknessAlongNormal + 0.55F);
            snapActorToAnchor(anchor, 0.6F);
        }
        BeginWindowVault(actorEntity, candidate.entity);
        return;
    }

    if (candidate.type == InteractionType::PalletVault)
    {
        const auto palletTransformIt = m_world.Transforms().find(candidate.entity);
        if (palletTransformIt != m_world.Transforms().end())
        {
            snapActorToAnchor(palletTransformIt->second.position, 0.6F);
        }
        BeginPalletVault(actorEntity, candidate.entity);
        return;
    }

    if (candidate.type == InteractionType::DropPallet)
    {
        auto palletIt = m_world.Pallets().find(candidate.entity);
        auto palletTransformIt = m_world.Transforms().find(candidate.entity);
        if (palletIt != m_world.Pallets().end() && palletTransformIt != m_world.Transforms().end() &&
            palletIt->second.state == engine::scene::PalletState::Standing)
        {
            snapActorToAnchor(palletTransformIt->second.position, 0.6F);
            palletIt->second.state = engine::scene::PalletState::Dropped;
            palletIt->second.breakTimer = 0.0F;
            palletIt->second.halfExtents = palletIt->second.droppedHalfExtents;
            palletTransformIt->second.position.y = palletIt->second.droppedCenterY;
            const engine::fx::FxNetMode netMode = m_networkAuthorityMode ? engine::fx::FxNetMode::ServerBroadcast
                                                                          : engine::fx::FxNetMode::Local;
            SpawnGameplayFx(
                "dust_puff",
                palletTransformIt->second.position + glm::vec3{0.0F, 0.18F, 0.0F},
                actorTransformIt->second.forward,
                netMode
            );
            AddRuntimeMessage("Pallet: standing -> dropped", 2.0F);
            TryStunKillerFromPallet(candidate.entity);
        }
        return;
    }

    if (candidate.type == InteractionType::BreakPallet)
    {
        auto palletTransformIt = m_world.Transforms().find(candidate.entity);
        if (palletTransformIt != m_world.Transforms().end())
        {
            snapActorToAnchor(palletTransformIt->second.position, 0.6F);
        }
        auto palletIt = m_world.Pallets().find(candidate.entity);
        if (palletIt != m_world.Pallets().end() && palletIt->second.state == engine::scene::PalletState::Dropped && palletIt->second.breakTimer <= 0.0F)
        {
            palletIt->second.breakTimer = palletIt->second.breakDuration;
            m_killerBreakingPallet = candidate.entity;
            const engine::fx::FxNetMode netMode = m_networkAuthorityMode ? engine::fx::FxNetMode::ServerBroadcast
                                                                          : engine::fx::FxNetMode::Local;
            SpawnGameplayFx(
                "hit_spark",
                palletTransformIt != m_world.Transforms().end() ? palletTransformIt->second.position + glm::vec3{0.0F, 0.4F, 0.0F}
                                                                 : glm::vec3{0.0F, 0.4F, 0.0F},
                actorTransformIt->second.forward,
                netMode
            );
            AddRuntimeMessage("Pallet break started", 2.0F);
        }
        return;
    }

    if (candidate.type == InteractionType::PickupSurvivor)
    {
        TryPickupDownedSurvivor();
        return;
    }

    if (candidate.type == InteractionType::DropSurvivor)
    {
        if (m_survivorState != SurvivorHealthState::Carried || m_survivor == 0 || m_killer == 0)
        {
            return;
        }

        const auto killerTransformIt = m_world.Transforms().find(m_killer);
        const auto survivorTransformIt = m_world.Transforms().find(m_survivor);
        if (killerTransformIt != m_world.Transforms().end() && survivorTransformIt != m_world.Transforms().end())
        {
            const glm::vec3 killerForward = glm::length(killerTransformIt->second.forward) > 1.0e-5F
                                                ? glm::normalize(killerTransformIt->second.forward)
                                                : glm::vec3{0.0F, 0.0F, -1.0F};
            survivorTransformIt->second.position =
                killerTransformIt->second.position - killerForward * 0.95F + glm::vec3{0.0F, 0.0F, 0.55F};
        }

        SetSurvivorState(SurvivorHealthState::Downed, "Killer manual drop");
        AddRuntimeMessage("Carry drop reason: killer manual drop", 1.5F);
        return;
    }

    if (candidate.type == InteractionType::HookSurvivor)
    {
        const auto hookTransformIt = m_world.Transforms().find(candidate.entity);
        if (hookTransformIt != m_world.Transforms().end())
        {
            snapActorToAnchor(hookTransformIt->second.position, 0.6F);
        }
        TryHookCarriedSurvivor(candidate.entity);
        return;
    }

    if (candidate.type == InteractionType::RepairGenerator)
    {
        const auto generatorTransformIt = m_world.Transforms().find(candidate.entity);
        if (generatorTransformIt != m_world.Transforms().end())
        {
            snapActorToAnchor(generatorTransformIt->second.position, 0.6F);
        }
        BeginOrContinueGeneratorRepair(candidate.entity);
        return;
    }

    if (candidate.type == InteractionType::SelfHeal)
    {
        BeginSelfHeal();
    }
}

void GameplaySystems::TryKillerHit()
{
    (void)ResolveKillerAttackHit(m_killerShortRange, m_killerShortHalfAngleRadians);
}

bool GameplaySystems::ResolveKillerAttackHit(float range, float halfAngleRadians, const glm::vec3& directionOverride)
{
    if (m_killer == 0 || m_survivor == 0)
    {
        return false;
    }

    if (m_survivorState == SurvivorHealthState::Carried ||
        m_survivorState == SurvivorHealthState::Hooked ||
        m_survivorState == SurvivorHealthState::Dead)
    {
        return false;
    }

    const auto killerTransformIt = m_world.Transforms().find(m_killer);
    const auto survivorTransformIt = m_world.Transforms().find(m_survivor);
    const auto survivorActorIt = m_world.Actors().find(m_survivor);
    if (killerTransformIt == m_world.Transforms().end() ||
        survivorTransformIt == m_world.Transforms().end() ||
        survivorActorIt == m_world.Actors().end())
    {
        return false;
    }

    glm::vec3 attackOrigin = killerTransformIt->second.position + glm::vec3{0.0F, 0.9F, 0.0F};
    glm::vec3 attackForward = killerTransformIt->second.forward;
    if (glm::length(directionOverride) > 1.0e-5F)
    {
        attackForward = directionOverride;
    }
    else if (m_controlledRole == ControlledRole::Killer && ResolveCameraMode() == CameraMode::FirstPerson)
    {
        attackOrigin = m_cameraPosition;
        attackForward = m_cameraForward;
    }
    if (glm::length(attackForward) < 1.0e-5F)
    {
        attackForward = glm::vec3{0.0F, 0.0F, -1.0F};
    }
    attackForward = glm::normalize(attackForward);

    m_lastSwingOrigin = attackOrigin;
    m_lastSwingDirection = attackForward;
    m_lastSwingRange = range;
    m_lastSwingHalfAngleRadians = halfAngleRadians;
    m_lastSwingDebugTtl = 0.45F;
    m_lastHitRayStart = attackOrigin;
    m_lastHitRayEnd = attackOrigin + attackForward * range;
    m_lastHitConnected = false;

    const float cosThreshold = std::cos(halfAngleRadians);
    const glm::vec3 survivorPoint = survivorTransformIt->second.position + glm::vec3{0.0F, 0.55F, 0.0F};
    const glm::vec3 toSurvivor = survivorPoint - attackOrigin;
    const float distanceToSurvivor = glm::length(toSurvivor);
    if (distanceToSurvivor > range + survivorActorIt->second.capsuleRadius || distanceToSurvivor < 1.0e-5F)
    {
        return false;
    }

    const glm::vec3 toSurvivorDirection = toSurvivor / distanceToSurvivor;
    if (glm::dot(attackForward, toSurvivorDirection) < cosThreshold)
    {
        return false;
    }

    const std::optional<engine::physics::RaycastHit> blockHit = m_physics.RaycastNearest(attackOrigin, survivorPoint);
    if (blockHit.has_value())
    {
        return false;
    }

    const glm::vec3 knockbackDirection = glm::normalize(glm::vec3{attackForward.x, 0.0F, attackForward.z});
    survivorTransformIt->second.position += knockbackDirection * 1.4F;
    m_lastHitConnected = true;
    m_killerAttackFlashTtl = 0.12F;
    const engine::fx::FxNetMode netMode = m_networkAuthorityMode ? engine::fx::FxNetMode::ServerBroadcast
                                                                  : engine::fx::FxNetMode::Local;
    SpawnGameplayFx("hit_spark", survivorPoint, attackForward, netMode);
    SpawnGameplayFx("blood_spray", survivorPoint + glm::vec3{0.0F, 0.08F, 0.0F}, attackForward, netMode);
    ApplySurvivorHit();
    AddRuntimeMessage("Killer hit confirmed", 1.3F);
    return true;
}

void GameplaySystems::UpdateKillerAttack(const RoleCommand& killerCommand, float fixedDt)
{
    if (m_killerLoadout.powerId == "wraith_cloak" &&
        (m_killerPowerState.wraithCloaked || m_killerPowerState.wraithCloakTransition))
    {
        m_previousAttackHeld = false;
        m_killerAttackState = KillerAttackState::Idle;
        m_killerAttackStateTimer = 0.0F;
        m_killerLungeChargeSeconds = 0.0F;
        return;
    }

    if (m_killerHitCooldown > 0.0F)
    {
        m_killerHitCooldown = std::max(0.0F, m_killerHitCooldown - fixedDt);
    }

    if (m_killerAttackState == KillerAttackState::Recovering)
    {
        m_killerAttackStateTimer = std::max(0.0F, m_killerAttackStateTimer - fixedDt);
        if (m_killerAttackStateTimer <= 0.0F)
        {
            m_killerAttackState = KillerAttackState::Idle;
        }
        return;
    }

    if (m_killerAttackState == KillerAttackState::Lunging)
    {
        m_killerAttackStateTimer += fixedDt;
        m_killerLungeChargeSeconds = std::min(m_killerAttackStateTimer, m_killerLungeDurationSeconds);
        const float lunge01 = glm::clamp(
            m_killerLungeChargeSeconds / std::max(0.01F, m_killerLungeDurationSeconds),
            0.0F,
            1.0F
        );
        m_killerCurrentLungeSpeed = glm::mix(m_killerLungeSpeedStart, m_killerLungeSpeedEnd, lunge01);

        const bool endedByRelease = !killerCommand.attackHeld;
        const bool endedByTimeout = m_killerAttackStateTimer >= m_killerLungeDurationSeconds;
        if (endedByRelease || endedByTimeout)
        {
            const bool hit = ResolveKillerAttackHit(m_killerLungeRange, m_killerLungeHalfAngleRadians);
            ApplyKillerAttackAftermath(hit, true);
            m_killerAttackHitThisAction = hit;
            m_killerAttackState = KillerAttackState::Recovering;
            m_killerAttackStateTimer = hit ? m_killerLungeRecoverSeconds : m_killerMissRecoverSeconds;
            m_killerHitCooldown = m_killerAttackStateTimer;
            m_killerLungeChargeSeconds = 0.0F;
            m_killerCurrentLungeSpeed = 0.0F;
        }
        return;
    }

    if (m_killerAttackState != KillerAttackState::Idle || m_killerHitCooldown > 0.0F)
    {
        return;
    }

    if (!m_previousAttackHeld && killerCommand.attackPressed)
    {
        m_previousAttackHeld = true;
        m_killerLungeChargeSeconds = 0.0F;
    }

    if (!m_previousAttackHeld)
    {
        return;
    }

    if (killerCommand.attackHeld)
    {
        m_killerLungeChargeSeconds += fixedDt;
        if (m_killerLungeChargeSeconds >= m_killerLungeChargeMinSeconds)
        {
            m_previousAttackHeld = false;
            m_killerAttackState = KillerAttackState::Lunging;
            m_killerAttackStateTimer = 0.0F;
            m_killerCurrentLungeSpeed = m_killerLungeSpeedStart;
            m_killerAttackHitThisAction = false;
            AddRuntimeMessage("Killer lunge", 0.9F);
        }
        return;
    }

    if (killerCommand.attackReleased || !killerCommand.attackHeld)
    {
        const bool hit = ResolveKillerAttackHit(m_killerShortRange, m_killerShortHalfAngleRadians);
        ApplyKillerAttackAftermath(hit, false);
        m_killerAttackHitThisAction = hit;
        m_killerAttackState = KillerAttackState::Recovering;
        m_killerAttackStateTimer = hit ? m_killerShortRecoverSeconds : m_killerMissRecoverSeconds;
        m_killerHitCooldown = m_killerAttackStateTimer;
        m_killerLungeChargeSeconds = 0.0F;
        m_previousAttackHeld = false;
    }
}

void GameplaySystems::UpdatePalletBreak(float fixedDt)
{
    if (m_killerBreakingPallet == 0)
    {
        return;
    }

    auto palletIt = m_world.Pallets().find(m_killerBreakingPallet);
    if (palletIt == m_world.Pallets().end())
    {
        m_killerBreakingPallet = 0;
        return;
    }

    engine::scene::PalletComponent& pallet = palletIt->second;
    if (pallet.state != engine::scene::PalletState::Dropped)
    {
        m_killerBreakingPallet = 0;
        return;
    }

    pallet.breakTimer = std::max(0.0F, pallet.breakTimer - fixedDt);
    if (pallet.breakTimer <= 0.0F)
    {
        pallet.state = engine::scene::PalletState::Broken;
        pallet.halfExtents = glm::vec3{0.12F, 0.08F, 0.12F};
        m_physicsDirty = true;
        auto transformIt = m_world.Transforms().find(m_killerBreakingPallet);
        if (transformIt != m_world.Transforms().end())
        {
            const engine::fx::FxNetMode netMode = m_networkAuthorityMode ? engine::fx::FxNetMode::ServerBroadcast
                                                                          : engine::fx::FxNetMode::Local;
            SpawnGameplayFx("dust_puff", transformIt->second.position + glm::vec3{0.0F, 0.2F, 0.0F}, glm::vec3{0.0F, 1.0F, 0.0F}, netMode);
            transformIt->second.position.y = -20.0F;
        }

        // Reset bloodlust on pallet break (DBD-like)
        if (m_bloodlust.tier > 0)
        {
            ResetBloodlust();
        }

        AddRuntimeMessage("Pallet: dropped -> broken", 2.0F);
        m_killerBreakingPallet = 0;
    }
}

void GameplaySystems::UpdateChaseState(float fixedDt)
{
    const bool wasChasing = m_chase.isChasing;
    const auto killerTransformIt = m_world.Transforms().find(m_killer);
    const auto survivorTransformIt = m_world.Transforms().find(m_survivor);
    const auto survivorActorIt = m_world.Actors().find(m_survivor);

    if (killerTransformIt == m_world.Transforms().end() ||
        survivorTransformIt == m_world.Transforms().end() ||
        survivorActorIt == m_world.Actors().end())
    {
        m_chase.isChasing = false;
        m_chase.distance = 0.0F;
        m_chase.hasLineOfSight = false;
        m_chase.inCenterFOV = false;
        m_chase.timeSinceSeenLOS = 0.0F;
        m_chase.timeSinceCenterFOV = 0.0F;
        m_chase.timeInChase = 0.0F;
        return;
    }

    // Calculate distance and LOS
    m_chase.distance = DistanceXZ(killerTransformIt->second.position, survivorTransformIt->second.position);

    // Skip expensive LOS raycast when far outside any relevant range (chase end = 18m, buffer = 2m).
    constexpr float kLosMaxRange = 20.0F;
    if (m_chase.distance > kLosMaxRange)
    {
        m_chase.hasLineOfSight = false;
    }
    else
    {
        m_chase.hasLineOfSight = m_physics.HasLineOfSight(killerTransformIt->second.position, survivorTransformIt->second.position);
    }

    // Check if survivor is in killer's center FOV (±35°)
    m_chase.inCenterFOV = IsSurvivorInKillerCenterFOV(
        killerTransformIt->second.position,
        killerTransformIt->second.forward,
        survivorTransformIt->second.position
    ); // ±35° DBD-like center FOV

    // Track survivor running state from actor component
    bool survivorIsRunning = false;
    if (survivorActorIt != m_world.Actors().end())
    {
        survivorIsRunning = survivorActorIt->second.sprinting;
    }

    if (m_forcedChase.has_value())
    {
        m_chase.isChasing = *m_forcedChase;
    }
    else
    {
        // DBD-like chase rules:
        // - Starts only if: survivor sprinting + distance <= 12m + LOS + in center FOV (±35°)
        // - Ends if: distance >= 18m OR lost LOS > 8s OR lost center FOV > 8s
        // - Chase can last indefinitely if LOS/center-FOV keep being reacquired

        if (!m_chase.isChasing)
        {
            // Not in chase - check if we should start
            const bool canStartChase =
                survivorIsRunning &&
                m_chase.distance <= m_chase.startDistance && // <= 12m
                m_chase.hasLineOfSight &&
                m_chase.inCenterFOV;

            if (canStartChase)
            {
                m_chase.isChasing = true;
                m_chase.timeSinceSeenLOS = 0.0F;
                m_chase.timeSinceCenterFOV = 0.0F;
                m_chase.timeInChase = 0.0F;
            }
        }
        else
        {
            // Already in chase - update timers and check if we should end

            // Update time-in-chase counter
            m_chase.timeInChase += fixedDt;

            // Update timers based on current conditions
            if (m_chase.hasLineOfSight)
            {
                m_chase.timeSinceSeenLOS = 0.0F;
            }
            else
            {
                m_chase.timeSinceSeenLOS += fixedDt;
            }

            if (m_chase.inCenterFOV)
            {
                m_chase.timeSinceCenterFOV = 0.0F;
            }
            else
            {
                m_chase.timeSinceCenterFOV += fixedDt;
            }

            assert(m_chase.timeSinceSeenLOS >= 0.0F && "timeSinceSeenLOS should never be negative");
            assert(m_chase.timeSinceCenterFOV >= 0.0F && "timeSinceCenterFOV should never be negative");
            assert(m_chase.timeInChase >= 0.0F && "timeInChase should never be negative");

            const bool tooFar = m_chase.distance >= m_chase.endDistance;
            const bool lostLOSLong = m_chase.timeSinceSeenLOS > m_chase.lostSightTimeout;
            const bool lostCenterFOVLong = m_chase.timeSinceCenterFOV > m_chase.lostCenterFOVTimeout;

            if (tooFar || lostLOSLong || lostCenterFOVLong)
            {
                m_chase.isChasing = false;
                m_chase.timeSinceSeenLOS = 0.0F;
                m_chase.timeSinceCenterFOV = 0.0F;
                m_chase.timeInChase = 0.0F;
            }
        }
    }

    // Handle chase FX (aura)
    if (m_chase.isChasing)
    {
        if (killerTransformIt != m_world.Transforms().end())
        {
            const engine::fx::FxNetMode netMode = m_networkAuthorityMode ? engine::fx::FxNetMode::ServerBroadcast
                                                                          : engine::fx::FxNetMode::Local;
            if (m_chaseAuraFxId == 0)
            {
                m_chaseAuraFxId = SpawnGameplayFx(
                    "chase_aura",
                    killerTransformIt->second.position + glm::vec3{0.0F, 0.25F, 0.0F},
                    killerTransformIt->second.forward,
                    netMode
                );
            }
            else
            {
                m_fxSystem.SetInstanceTransform(
                    m_chaseAuraFxId,
                    killerTransformIt->second.position + glm::vec3{0.0F, 0.25F, 0.0F},
                    killerTransformIt->second.forward
                );
            }
        }
    }
    else if (m_chaseAuraFxId != 0)
    {
        m_fxSystem.Stop(m_chaseAuraFxId);
        m_chaseAuraFxId = 0;
    }

    if (m_chase.isChasing != wasChasing)
    {
        AddRuntimeMessage(m_chase.isChasing ? "Chase started" : "Chase ended", 1.0F);

        if (!m_chase.isChasing)
        {
            // Check for Sprint Burst: activates when chase ends
            const auto& activePerks = m_perkSystem.GetActivePerks(engine::scene::Role::Survivor);
            for (const auto& state : activePerks)
            {
                const auto* perk = m_perkSystem.GetPerk(state.perkId);
                if (perk && (perk->type == game::gameplay::perks::PerkType::Triggered) &&
                    (perk->id == "sprint_burst"))
                {
                    m_perkSystem.ActivatePerk(state.perkId, engine::scene::Role::Survivor);
                }
            }
        }
    }
}

void GameplaySystems::UpdateCamera(float deltaSeconds)
{
    const engine::scene::Entity controlled = ControlledEntity();
    const auto transformIt = m_world.Transforms().find(controlled);
    const auto actorIt = m_world.Actors().find(controlled);

    if (controlled == 0 || transformIt == m_world.Transforms().end() || actorIt == m_world.Actors().end())
    {
        return;
    }

    const engine::scene::Transform& transform = transformIt->second;
    const engine::scene::ActorComponent& actor = actorIt->second;
    const CameraMode mode = ResolveCameraMode();

    const glm::vec3 up{0.0F, 1.0F, 0.0F};
    glm::vec3 desiredPosition{0.0F};
    glm::vec3 desiredTarget{0.0F};

    if (mode == CameraMode::FirstPerson)
    {
        const float eyeScale = actor.crawling ? 0.52F : (actor.crouching ? 0.78F : 1.0F);
        const float eyeOffset = actor.eyeHeight * eyeScale - actor.capsuleHeight * 0.5F;
        desiredPosition = transform.position + glm::vec3{0.0F, eyeOffset, 0.0F};
        desiredTarget = desiredPosition + transform.forward * 8.0F;
    }
    else
    {
        const float eyeScale = actor.crawling ? 0.52F : (actor.crouching ? 0.78F : 1.0F);
        const float eyeOffset = actor.eyeHeight * eyeScale - actor.capsuleHeight * 0.45F;
        const glm::vec3 pivot = transform.position + glm::vec3{0.0F, eyeOffset, 0.0F};
        const bool flashlightAimCamera =
            (m_controlledRole == ControlledRole::Survivor) &&
            (m_survivorLoadout.itemId == "flashlight") &&
            m_survivorItemState.active &&
            m_survivorItemState.charges > 0.0F;

        const float yaw = transform.rotationEuler.y;
        const float pitch = glm::clamp(transform.rotationEuler.x * 0.65F, -0.8F, 0.8F);
        const glm::vec3 viewForward = ForwardFromYawPitch(yaw, pitch);
        glm::vec3 right = glm::cross(viewForward, up);
        if (glm::length(right) < 1.0e-5F)
        {
            right = glm::vec3{1.0F, 0.0F, 0.0F};
        }
        right = glm::normalize(right);

        const float backDistance = flashlightAimCamera ? 2.2F : 4.2F;
        const float shoulderOffset = flashlightAimCamera ? 0.22F : 0.75F;
        const float verticalOffset = flashlightAimCamera ? 0.25F : 0.55F;
        glm::vec3 desiredCamera = pivot - viewForward * backDistance + right * shoulderOffset + glm::vec3{0.0F, verticalOffset, 0.0F};

        const std::optional<engine::physics::RaycastHit> hit = m_physics.RaycastNearest(pivot, desiredCamera);
        if (hit.has_value())
        {
            const glm::vec3 dir = glm::normalize(desiredCamera - pivot);
            const float maxDistance = glm::length(desiredCamera - pivot);
            const float safeDistance = std::max(0.6F, hit->t * maxDistance - 0.2F);
            desiredCamera = pivot + dir * safeDistance;
        }

        desiredPosition = desiredCamera;
        desiredTarget = pivot + viewForward * (flashlightAimCamera ? 8.0F : 2.0F);
    }

    const glm::vec3 shakeOffset = m_fxSystem.CameraShakeOffset();
    desiredPosition += shakeOffset;
    desiredTarget += shakeOffset * 0.6F;

    if (!m_cameraInitialized)
    {
        m_cameraPosition = desiredPosition;
        m_cameraTarget = desiredTarget;
        m_cameraInitialized = true;
    }
    else if (mode == CameraMode::FirstPerson)
    {
        // In first-person keep camera fully locked to actor look to avoid weapon/camera desync.
        m_cameraPosition = desiredPosition;
        m_cameraTarget = desiredTarget;
    }
    else
    {
        const float smooth = 1.0F - std::exp(-deltaSeconds * 14.0F);
        m_cameraPosition = glm::mix(m_cameraPosition, desiredPosition, smooth);
        m_cameraTarget = glm::mix(m_cameraTarget, desiredTarget, smooth);
    }

    const glm::vec3 forward = m_cameraTarget - m_cameraPosition;
    m_cameraForward = glm::length(forward) > 1.0e-5F ? glm::normalize(forward) : glm::vec3{0.0F, 0.0F, -1.0F};
}

GameplaySystems::CameraMode GameplaySystems::ResolveCameraMode() const
{
    if (m_cameraOverride == CameraOverride::SurvivorThirdPerson)
    {
        return CameraMode::ThirdPerson;
    }
    if (m_cameraOverride == CameraOverride::KillerFirstPerson)
    {
        return CameraMode::FirstPerson;
    }

    return m_controlledRole == ControlledRole::Survivor ? CameraMode::ThirdPerson : CameraMode::FirstPerson;
}

engine::scene::Entity GameplaySystems::ControlledEntity() const
{
    return m_controlledRole == ControlledRole::Survivor ? m_survivor : m_killer;
}

engine::scene::Role GameplaySystems::ControlledSceneRole() const
{
    return m_controlledRole == ControlledRole::Survivor ? engine::scene::Role::Survivor : engine::scene::Role::Killer;
}

GameplaySystems::InteractionCandidate GameplaySystems::ResolveInteractionCandidateFromView(engine::scene::Entity actorEntity) const
{
    InteractionCandidate best;

    const auto actorTransformIt = m_world.Transforms().find(actorEntity);
    const auto actorIt = m_world.Actors().find(actorEntity);
    if (actorTransformIt == m_world.Transforms().end() || actorIt == m_world.Actors().end())
    {
        return best;
    }

    const engine::scene::Transform& actorTransform = actorTransformIt->second;
    const engine::scene::ActorComponent& actor = actorIt->second;

    const glm::vec3 eyePosition = actorTransform.position + glm::vec3{0.0F, actor.eyeHeight - actor.capsuleHeight * 0.5F, 0.0F};
    const bool useCameraRay = actorEntity == ControlledEntity() && m_cameraInitialized;
    const glm::vec3 castStart = useCameraRay ? m_cameraPosition : eyePosition;
    glm::vec3 castDirection = useCameraRay ? m_cameraForward : actorTransform.forward;
    if (glm::length(castDirection) < 1.0e-5F)
    {
        castDirection = actorTransform.forward;
    }
    castDirection = glm::normalize(castDirection);

    constexpr float kInteractionCastRange = 4.0F;
    constexpr float kInteractionCastRadius = 0.85F;
    const glm::vec3 castEnd = castStart + castDirection * kInteractionCastRange;

    m_physics.SphereCastTriggers(m_sphereCastScratch, castStart, castEnd, kInteractionCastRadius);
    std::unordered_set<engine::scene::Entity> visited;

    auto considerCandidate = [&](const InteractionCandidate& candidate) {
        if (candidate.type == InteractionType::None)
        {
            return;
        }

        if (candidate.priority > best.priority ||
            (candidate.priority == best.priority && candidate.castT < best.castT))
        {
            best = candidate;
        }
    };

    auto processTriggerEntity = [&](engine::scene::Entity entity, float castT) {
        if (m_world.Windows().contains(entity))
        {
            considerCandidate(BuildWindowVaultCandidate(actorEntity, entity, castT));
            return;
        }

        if (m_world.Hooks().contains(entity))
        {
            considerCandidate(BuildHookSurvivorCandidate(actorEntity, entity, castT));
            return;
        }

        if (m_world.Generators().contains(entity))
        {
            considerCandidate(BuildGeneratorRepairCandidate(actorEntity, entity, castT));
            return;
        }

        const auto palletIt = m_world.Pallets().find(entity);
        if (palletIt == m_world.Pallets().end())
        {
            return;
        }

        if (palletIt->second.state == engine::scene::PalletState::Standing)
        {
            considerCandidate(BuildStandingPalletCandidate(actorEntity, entity, castT));
        }
        else if (palletIt->second.state == engine::scene::PalletState::Dropped)
        {
            considerCandidate(BuildDroppedPalletCandidate(actorEntity, entity, castT));
        }
    };

    for (const engine::physics::TriggerCastHit& hit : m_sphereCastScratch)
    {
        if (!visited.insert(hit.entity).second)
        {
            continue;
        }

        processTriggerEntity(hit.entity, hit.t);
    }

    // Fallback: if camera cast misses while sprinting, still resolve entities from local trigger volumes.
    m_physics.QueryCapsuleTriggers(m_triggerHitBuf,
        actorTransform.position,
        actor.capsuleRadius,
        actor.capsuleHeight,
        engine::physics::TriggerKind::Vault
    );
    for (const engine::physics::TriggerHit& hit : m_triggerHitBuf)
    {
        if (!visited.insert(hit.entity).second)
        {
            continue;
        }
        processTriggerEntity(hit.entity, 0.12F);
    }

    m_physics.QueryCapsuleTriggers(m_triggerHitBuf,
        actorTransform.position,
        actor.capsuleRadius,
        actor.capsuleHeight,
        engine::physics::TriggerKind::Interaction
    );
    for (const engine::physics::TriggerHit& hit : m_triggerHitBuf)
    {
        if (!visited.insert(hit.entity).second)
        {
            continue;
        }
        processTriggerEntity(hit.entity, 0.18F);
    }

    considerCandidate(BuildDropSurvivorCandidate(actorEntity));
    considerCandidate(BuildPickupSurvivorCandidate(actorEntity, castStart, castDirection));
    considerCandidate(BuildSelfHealCandidate(actorEntity));

    return best;
}

GameplaySystems::InteractionCandidate GameplaySystems::BuildWindowVaultCandidate(
    engine::scene::Entity actorEntity,
    engine::scene::Entity windowEntity,
    float castT
) const
{
    InteractionCandidate candidate;

    const auto actorTransformIt = m_world.Transforms().find(actorEntity);
    const auto actorIt = m_world.Actors().find(actorEntity);
    const auto windowIt = m_world.Windows().find(windowEntity);
    const auto windowTransformIt = m_world.Transforms().find(windowEntity);

    if (actorTransformIt == m_world.Transforms().end() || actorIt == m_world.Actors().end() ||
        windowIt == m_world.Windows().end() || windowTransformIt == m_world.Transforms().end())
    {
        return candidate;
    }

    const engine::scene::Transform& actorTransform = actorTransformIt->second;
    const engine::scene::ActorComponent& actor = actorIt->second;
    const engine::scene::WindowComponent& window = windowIt->second;

    if (actor.vaulting || actor.vaultCooldown > 0.0F)
    {
        return candidate;
    }
    if (actor.role == engine::scene::Role::Survivor &&
        (m_survivorState == SurvivorHealthState::Downed ||
         m_survivorState == SurvivorHealthState::Carried ||
         m_survivorState == SurvivorHealthState::Hooked ||
         m_survivorState == SurvivorHealthState::Dead))
    {
        return candidate;
    }
    if (actor.role == engine::scene::Role::Killer && !window.killerCanVault)
    {
        return candidate;
    }

    m_physics.QueryCapsuleTriggers(m_triggerHitBuf,
        actorTransform.position,
        actor.capsuleRadius,
        actor.capsuleHeight,
        engine::physics::TriggerKind::Vault
    );

    bool inTrigger = false;
    for (const engine::physics::TriggerHit& hit : m_triggerHitBuf)
    {
        if (hit.entity == windowEntity)
        {
            inTrigger = true;
            break;
        }
    }
    if (!inTrigger)
    {
        return candidate;
    }

    const glm::vec3 windowNormal = glm::normalize(window.normal);
    const float side = glm::dot(actorTransform.position - windowTransformIt->second.position, windowNormal) >= 0.0F ? 1.0F : -1.0F;
    const glm::vec3 desiredForward = -windowNormal * side;

    const glm::vec3 actorForwardXZ = glm::normalize(glm::vec3{actorTransform.forward.x, 0.0F, actorTransform.forward.z});
    const glm::vec3 desiredForwardXZ = glm::normalize(glm::vec3{desiredForward.x, 0.0F, desiredForward.z});
    const float facingDot = glm::dot(actorForwardXZ, desiredForwardXZ);

    const float distanceToVaultPoint = DistanceXZ(actorTransform.position, windowTransformIt->second.position);
    if (distanceToVaultPoint > 3.0F)
    {
        return candidate;
    }

    candidate.type = InteractionType::WindowVault;
    candidate.entity = windowEntity;
    candidate.priority = 80;
    candidate.castT = castT;
    candidate.prompt = "Press E to Vault";
    if (facingDot < 0.45F)
    {
        candidate.prompt = "Press E to Vault (Face window)";
        candidate.priority = 60;
    }
    else if (distanceToVaultPoint > 2.3F)
    {
        candidate.prompt = "Press E to Vault (Move closer)";
        candidate.priority = 60;
    }
    candidate.typeName = "WindowVault";
    candidate.targetName = "Window";
    return candidate;
}

GameplaySystems::InteractionCandidate GameplaySystems::BuildStandingPalletCandidate(
    engine::scene::Entity actorEntity,
    engine::scene::Entity palletEntity,
    float castT
) const
{
    InteractionCandidate candidate;

    const auto actorTransformIt = m_world.Transforms().find(actorEntity);
    const auto actorIt = m_world.Actors().find(actorEntity);
    const auto palletIt = m_world.Pallets().find(palletEntity);
    const auto palletTransformIt = m_world.Transforms().find(palletEntity);

    if (actorTransformIt == m_world.Transforms().end() || actorIt == m_world.Actors().end() ||
        palletIt == m_world.Pallets().end() || palletTransformIt == m_world.Transforms().end())
    {
        return candidate;
    }

    if (actorIt->second.role != engine::scene::Role::Survivor || palletIt->second.state != engine::scene::PalletState::Standing)
    {
        return candidate;
    }
    if (m_survivorState != SurvivorHealthState::Healthy && m_survivorState != SurvivorHealthState::Injured)
    {
        return candidate;
    }

    m_physics.QueryCapsuleTriggers(m_triggerHitBuf,
        actorTransformIt->second.position,
        actorIt->second.capsuleRadius,
        actorIt->second.capsuleHeight,
        engine::physics::TriggerKind::Interaction
    );

    bool inTrigger = false;
    for (const engine::physics::TriggerHit& hit : m_triggerHitBuf)
    {
        if (hit.entity == palletEntity)
        {
            inTrigger = true;
            break;
        }
    }
    if (!inTrigger)
    {
        return candidate;
    }

    const glm::vec3 toPallet = palletTransformIt->second.position - actorTransformIt->second.position;
    const float distance = DistanceXZ(palletTransformIt->second.position, actorTransformIt->second.position);
    if (distance > 2.8F)
    {
        return candidate;
    }

    const glm::vec3 toPalletXZ = glm::normalize(glm::vec3{toPallet.x, 0.0F, toPallet.z});
    const glm::vec3 actorForwardXZ = glm::normalize(glm::vec3{actorTransformIt->second.forward.x, 0.0F, actorTransformIt->second.forward.z});
    const float facingDot = glm::dot(actorForwardXZ, toPalletXZ);

    candidate.type = InteractionType::DropPallet;
    candidate.entity = palletEntity;
    candidate.priority = 100;
    candidate.castT = castT;
    candidate.prompt = "Press E to Drop Pallet";
    if (facingDot < 0.1F)
    {
        candidate.prompt = "Press E to Drop Pallet (Face pallet)";
        candidate.priority = 70;
    }
    else if (distance > 2.2F)
    {
        candidate.prompt = "Press E to Drop Pallet (Move closer)";
        candidate.priority = 70;
    }
    candidate.typeName = "DropPallet";
    candidate.targetName = "Pallet";
    return candidate;
}

GameplaySystems::InteractionCandidate GameplaySystems::BuildDroppedPalletCandidate(
    engine::scene::Entity actorEntity,
    engine::scene::Entity palletEntity,
    float castT
) const
{
    InteractionCandidate candidate;

    const auto actorTransformIt = m_world.Transforms().find(actorEntity);
    const auto actorIt = m_world.Actors().find(actorEntity);
    const auto palletIt = m_world.Pallets().find(palletEntity);
    const auto palletTransformIt = m_world.Transforms().find(palletEntity);

    if (actorTransformIt == m_world.Transforms().end() || actorIt == m_world.Actors().end() ||
        palletIt == m_world.Pallets().end() || palletTransformIt == m_world.Transforms().end())
    {
        return candidate;
    }

    if (palletIt->second.state != engine::scene::PalletState::Dropped)
    {
        return candidate;
    }

    m_physics.QueryCapsuleTriggers(m_triggerHitBuf,
        actorTransformIt->second.position,
        actorIt->second.capsuleRadius,
        actorIt->second.capsuleHeight,
        engine::physics::TriggerKind::Interaction
    );

    bool inTrigger = false;
    for (const engine::physics::TriggerHit& hit : m_triggerHitBuf)
    {
        if (hit.entity == palletEntity)
        {
            inTrigger = true;
            break;
        }
    }
    if (!inTrigger)
    {
        return candidate;
    }

    const float distance = DistanceXZ(palletTransformIt->second.position, actorTransformIt->second.position);
    if (distance > 2.4F)
    {
        return candidate;
    }

    if (actorIt->second.role == engine::scene::Role::Killer)
    {
        if (palletIt->second.breakTimer > 0.0F)
        {
            return candidate;
        }

        candidate.type = InteractionType::BreakPallet;
        candidate.entity = palletEntity;
        candidate.priority = 70;
        candidate.castT = castT;
        candidate.prompt = "Press E to Break Pallet";
        if (distance > 2.0F)
        {
            candidate.prompt = "Press E to Break Pallet (Move closer)";
            candidate.priority = 55;
        }
        candidate.typeName = "BreakPallet";
        candidate.targetName = "Pallet";
        return candidate;
    }

    if (m_survivorState != SurvivorHealthState::Healthy && m_survivorState != SurvivorHealthState::Injured)
    {
        return candidate;
    }

    const glm::vec3 toPallet = palletTransformIt->second.position - actorTransformIt->second.position;
    const glm::vec3 toPalletXZ = glm::normalize(glm::vec3{toPallet.x, 0.0F, toPallet.z});
    const glm::vec3 actorForwardXZ = glm::normalize(glm::vec3{actorTransformIt->second.forward.x, 0.0F, actorTransformIt->second.forward.z});
    const float facingDot = glm::dot(actorForwardXZ, toPalletXZ);

    candidate.type = InteractionType::PalletVault;
    candidate.entity = palletEntity;
    candidate.priority = 85;
    candidate.castT = castT;
    candidate.prompt = "Press E to Vault Pallet";
    if (facingDot < 0.1F)
    {
        candidate.prompt = "Press E to Vault Pallet (Face pallet)";
        candidate.priority = 60;
    }
    candidate.typeName = "PalletVault";
    candidate.targetName = "DroppedPallet";
    return candidate;
}

GameplaySystems::InteractionCandidate GameplaySystems::BuildDropSurvivorCandidate(engine::scene::Entity actorEntity) const
{
    InteractionCandidate candidate;
    if (actorEntity != m_killer || m_survivorState != SurvivorHealthState::Carried)
    {
        return candidate;
    }

    candidate.type = InteractionType::DropSurvivor;
    candidate.entity = m_survivor;
    candidate.priority = 110;
    candidate.castT = 0.05F;
    candidate.prompt = "Press E to Drop Survivor";
    candidate.typeName = "DropSurvivor";
    candidate.targetName = "Survivor";
    return candidate;
}

GameplaySystems::InteractionCandidate GameplaySystems::BuildPickupSurvivorCandidate(
    engine::scene::Entity actorEntity,
    const glm::vec3& castStart,
    const glm::vec3& castDirection
) const
{
    InteractionCandidate candidate;

    if (actorEntity != m_killer || m_survivor == 0 ||
        (m_survivorState != SurvivorHealthState::Downed && m_survivorState != SurvivorHealthState::Trapped))
    {
        return candidate;
    }

    const auto survivorTransformIt = m_world.Transforms().find(m_survivor);
    if (survivorTransformIt == m_world.Transforms().end())
    {
        return candidate;
    }

    const glm::vec3 survivorPoint = survivorTransformIt->second.position + glm::vec3{0.0F, 0.45F, 0.0F};
    const glm::vec3 toSurvivor = survivorPoint - castStart;
    const float distance = glm::length(toSurvivor);
    if (distance > 2.4F || distance < 1.0e-5F)
    {
        return candidate;
    }

    const glm::vec3 directionToSurvivor = toSurvivor / distance;
    if (glm::dot(glm::normalize(castDirection), directionToSurvivor) < 0.55F)
    {
        return candidate;
    }

    const std::optional<engine::physics::RaycastHit> obstacleHit = m_physics.RaycastNearest(castStart, survivorPoint);
    if (obstacleHit.has_value())
    {
        return candidate;
    }

    candidate.type = InteractionType::PickupSurvivor;
    candidate.entity = m_survivor;
    candidate.priority = 95;
    candidate.castT = glm::clamp(distance / 3.0F, 0.0F, 1.0F);
    candidate.prompt = m_survivorState == SurvivorHealthState::Trapped
                           ? "Press E to Pick Up Trapped Survivor"
                           : "Press E to Pick Up Survivor";
    candidate.typeName = "PickupSurvivor";
    candidate.targetName = "Survivor";
    return candidate;
}

GameplaySystems::InteractionCandidate GameplaySystems::BuildHookSurvivorCandidate(
    engine::scene::Entity actorEntity,
    engine::scene::Entity hookEntity,
    float castT
) const
{
    InteractionCandidate candidate;

    if (actorEntity != m_killer || m_survivorState != SurvivorHealthState::Carried)
    {
        return candidate;
    }

    const auto hookIt = m_world.Hooks().find(hookEntity);
    const auto hookTransformIt = m_world.Transforms().find(hookEntity);
    const auto killerTransformIt = m_world.Transforms().find(actorEntity);
    if (hookIt == m_world.Hooks().end() || hookTransformIt == m_world.Transforms().end() ||
        killerTransformIt == m_world.Transforms().end())
    {
        return candidate;
    }

    if (hookIt->second.occupied)
    {
        return candidate;
    }

    const float distance = DistanceXZ(killerTransformIt->second.position, hookTransformIt->second.position);
    if (distance > 2.2F)
    {
        return candidate;
    }

    const glm::vec3 toHook = hookTransformIt->second.position - killerTransformIt->second.position;
    const glm::vec3 toHookXZ = glm::normalize(glm::vec3{toHook.x, 0.0F, toHook.z});
    const glm::vec3 killerForwardXZ = glm::normalize(glm::vec3{killerTransformIt->second.forward.x, 0.0F, killerTransformIt->second.forward.z});
    if (glm::dot(killerForwardXZ, toHookXZ) < 0.2F)
    {
        return candidate;
    }

    candidate.type = InteractionType::HookSurvivor;
    candidate.entity = hookEntity;
    candidate.priority = 120;
    candidate.castT = castT;
    candidate.prompt = "Press E to Hook Survivor";
    candidate.typeName = "HookSurvivor";
    candidate.targetName = "Hook";
    return candidate;
}

GameplaySystems::InteractionCandidate GameplaySystems::BuildGeneratorRepairCandidate(
    engine::scene::Entity actorEntity,
    engine::scene::Entity generatorEntity,
    float castT
) const
{
    InteractionCandidate candidate;

    const auto actorIt = m_world.Actors().find(actorEntity);
    const auto actorTransformIt = m_world.Transforms().find(actorEntity);
    const auto generatorIt = m_world.Generators().find(generatorEntity);
    const auto generatorTransformIt = m_world.Transforms().find(generatorEntity);
    if (actorIt == m_world.Actors().end() || actorTransformIt == m_world.Transforms().end() ||
        generatorIt == m_world.Generators().end() || generatorTransformIt == m_world.Transforms().end())
    {
        return candidate;
    }

    if (actorIt->second.role != engine::scene::Role::Survivor)
    {
        return candidate;
    }
    if (m_survivorState != SurvivorHealthState::Healthy && m_survivorState != SurvivorHealthState::Injured)
    {
        return candidate;
    }
    if (generatorIt->second.completed)
    {
        return candidate;
    }

    m_physics.QueryCapsuleTriggers(m_triggerHitBuf,
        actorTransformIt->second.position,
        actorIt->second.capsuleRadius,
        actorIt->second.capsuleHeight,
        engine::physics::TriggerKind::Interaction
    );

    bool inTrigger = false;
    for (const engine::physics::TriggerHit& hit : m_triggerHitBuf)
    {
        if (hit.entity == generatorEntity)
        {
            inTrigger = true;
            break;
        }
    }
    if (!inTrigger)
    {
        return candidate;
    }

    const float distance = DistanceXZ(actorTransformIt->second.position, generatorTransformIt->second.position);
    if (distance > 2.5F)
    {
        return candidate;
    }

    const glm::vec3 toGenerator = generatorTransformIt->second.position - actorTransformIt->second.position;
    const glm::vec3 toGeneratorXZ = glm::normalize(glm::vec3{toGenerator.x, 0.0F, toGenerator.z});
    const glm::vec3 actorForwardXZ = glm::normalize(glm::vec3{actorTransformIt->second.forward.x, 0.0F, actorTransformIt->second.forward.z});
    if (glm::dot(actorForwardXZ, toGeneratorXZ) < -0.2F)
    {
        return candidate;
    }

    candidate.type = InteractionType::RepairGenerator;
    candidate.entity = generatorEntity;
    candidate.priority = 55;
    candidate.castT = castT;
    if (generatorEntity == m_activeRepairGenerator && m_skillCheckActive)
    {
        candidate.prompt = "Skill Check active: press SPACE";
    }
    else if (generatorEntity == m_activeRepairGenerator)
    {
        candidate.prompt = "Hold E to Repair Generator";
    }
    else
    {
        candidate.prompt = "Press E to Repair Generator";
    }
    candidate.typeName = "RepairGenerator";
    candidate.targetName = "Generator";
    return candidate;
}

GameplaySystems::InteractionCandidate GameplaySystems::BuildSelfHealCandidate(engine::scene::Entity actorEntity) const
{
    InteractionCandidate candidate;
    if (actorEntity != m_survivor || m_survivorState != SurvivorHealthState::Injured)
    {
        return candidate;
    }

    const auto actorIt = m_world.Actors().find(actorEntity);
    if (actorIt == m_world.Actors().end() || actorIt->second.carried || actorIt->second.vaulting)
    {
        return candidate;
    }

    candidate.type = InteractionType::SelfHeal;
    candidate.entity = actorEntity;
    candidate.priority = 18;
    candidate.castT = 0.95F;
    if (m_selfHealActive && m_skillCheckActive)
    {
        candidate.prompt = "Self-heal: skill check (SPACE)";
    }
    else if (m_selfHealActive)
    {
        candidate.prompt = "Hold E to Self-heal";
    }
    else
    {
        candidate.prompt = "Press E to Self-heal";
    }
    candidate.typeName = "SelfHeal";
    candidate.targetName = "Self";
    return candidate;
}

bool GameplaySystems::IsActorInputLocked(const engine::scene::ActorComponent& actor) const
{
    return actor.vaulting || actor.stunTimer > 0.0F || actor.carried;
}

GameplaySystems::VaultType GameplaySystems::DetermineWindowVaultType(
    const engine::scene::ActorComponent& actor,
    const engine::scene::Transform& actorTransform,
    const engine::scene::Transform& windowTransform,
    const engine::scene::WindowComponent& window
) const
{
    const glm::vec3 windowNormal = glm::normalize(window.normal);
    const float side = glm::dot(actorTransform.position - windowTransform.position, windowNormal) >= 0.0F ? 1.0F : -1.0F;
    const glm::vec3 desiredForward = -windowNormal * side;

    const glm::vec3 actorForwardXZ = glm::normalize(glm::vec3{actorTransform.forward.x, 0.0F, actorTransform.forward.z});
    const glm::vec3 desiredForwardXZ = glm::normalize(glm::vec3{desiredForward.x, 0.0F, desiredForward.z});
    const float facing = glm::dot(actorForwardXZ, desiredForwardXZ);
    const float horizontalSpeed = glm::length(glm::vec2{actor.velocity.x, actor.velocity.z});
    const float distanceToWindow = DistanceXZ(actorTransform.position, windowTransform.position);

    const bool fastBySprint = actor.sprinting;
    const bool fastBySpeed = horizontalSpeed >= actor.sprintSpeed * m_tuning.fastVaultSpeedMultiplier;
    const bool fastByFacing = facing >= m_tuning.fastVaultDotThreshold;
    const bool fastByDistance = distanceToWindow >= 0.45F && distanceToWindow <= 1.9F;
    const bool fastByRunup = actor.forwardRunupDistance >= m_tuning.fastVaultMinRunup;
    if (fastBySprint && fastBySpeed && fastByFacing && fastByDistance && fastByRunup)
    {
        return VaultType::Fast;
    }

    const bool mediumBySpeed = horizontalSpeed >= actor.walkSpeed * 0.95F;
    const bool mediumBySprint = actor.sprinting;
    const bool mediumByFacing = facing >= 0.55F;
    if ((mediumBySpeed || mediumBySprint) && mediumByFacing)
    {
        return VaultType::Medium;
    }

    return VaultType::Slow;
}

GameplaySystems::VaultType GameplaySystems::DeterminePalletVaultType(const engine::scene::ActorComponent& actor) const
{
    const float horizontalSpeed = glm::length(glm::vec2{actor.velocity.x, actor.velocity.z});
    if (actor.sprinting && horizontalSpeed >= actor.sprintSpeed * 0.84F)
    {
        return VaultType::Fast;
    }
    return VaultType::Slow;
}

const char* GameplaySystems::VaultTypeToText(VaultType type)
{
    switch (type)
    {
        case VaultType::Slow: return "Slow";
        case VaultType::Medium: return "Medium";
        case VaultType::Fast: return "Fast";
        default: return "Slow";
    }
}

void GameplaySystems::BeginWindowVault(engine::scene::Entity actorEntity, engine::scene::Entity windowEntity)
{
    auto actorIt = m_world.Actors().find(actorEntity);
    auto actorTransformIt = m_world.Transforms().find(actorEntity);
    auto windowIt = m_world.Windows().find(windowEntity);
    auto windowTransformIt = m_world.Transforms().find(windowEntity);

    if (actorIt == m_world.Actors().end() || actorTransformIt == m_world.Transforms().end() ||
        windowIt == m_world.Windows().end() || windowTransformIt == m_world.Transforms().end())
    {
        return;
    }

    engine::scene::ActorComponent& actor = actorIt->second;
    engine::scene::Transform& actorTransform = actorTransformIt->second;
    const engine::scene::WindowComponent& window = windowIt->second;

    if (actor.vaulting || actor.vaultCooldown > 0.0F)
    {
        return;
    }
    if (actor.role == engine::scene::Role::Survivor &&
        (m_survivorState != SurvivorHealthState::Healthy && m_survivorState != SurvivorHealthState::Injured))
    {
        return;
    }

    const glm::vec3 normal = glm::length(window.normal) > 1.0e-4F ? glm::normalize(window.normal) : glm::vec3{0.0F, 0.0F, 1.0F};
    const float sideSign = glm::dot(actorTransform.position - windowTransformIt->second.position, normal) >= 0.0F ? 1.0F : -1.0F;
    const glm::vec3 vaultDirection = -normal * sideSign;

    const glm::vec3 actorForwardXZ = glm::normalize(glm::vec3{actorTransform.forward.x, 0.0F, actorTransform.forward.z});
    const glm::vec3 vaultForwardXZ = glm::normalize(glm::vec3{vaultDirection.x, 0.0F, vaultDirection.z});
    if (glm::dot(actorForwardXZ, vaultForwardXZ) < -0.2F)
    {
        AddRuntimeMessage("Vault blocked: face window", 1.2F);
        return;
    }

    const float windowThicknessAlongNormal =
        std::abs(normal.x) * window.halfExtents.x +
        std::abs(normal.y) * window.halfExtents.y +
        std::abs(normal.z) * window.halfExtents.z;

    VaultType vaultType = VaultType::Slow;
    if (actor.role == engine::scene::Role::Survivor)
    {
        vaultType = DetermineWindowVaultType(actor, actorTransform, windowTransformIt->second, window);
    }

    float duration = m_tuning.vaultSlowTime;
    if (vaultType == VaultType::Medium)
    {
        duration = m_tuning.vaultMediumTime;
    }
    else if (vaultType == VaultType::Fast)
    {
        duration = m_tuning.vaultFastTime;
    }

    actor.vaulting = true;
    actor.vaultTimer = 0.0F;
    actor.vaultStart = actorTransform.position;
    actor.vaultEnd = windowTransformIt->second.position + vaultDirection * (windowThicknessAlongNormal + actor.capsuleRadius + 0.8F);
    actor.vaultEnd.y = actorTransform.position.y;
    actor.vaultDuration = duration;
    actor.vaultArcHeight =
        vaultType == VaultType::Fast ? 0.38F :
        (vaultType == VaultType::Medium ? 0.48F : 0.55F);

    if (actor.role == engine::scene::Role::Killer)
    {
        vaultType = VaultType::Slow;
        actor.vaultDuration = m_tuning.vaultSlowTime * window.killerVaultMultiplier;
        actor.vaultArcHeight = 0.4F;
    }

    actor.velocity = glm::vec3{0.0F};
    actor.sprinting = false;
    actor.forwardRunupDistance = 0.0F;
    actor.lastVaultType = VaultTypeToText(vaultType);
    actor.collisionEnabled = false;

    const glm::vec3 fxPos = windowTransformIt->second.position + glm::vec3{0.0F, 0.8F, 0.0F};
    const engine::fx::FxNetMode netMode = m_networkAuthorityMode ? engine::fx::FxNetMode::ServerBroadcast
                                                                  : engine::fx::FxNetMode::Local;
    SpawnGameplayFx("dust_puff", fxPos, vaultDirection, netMode);
    if (vaultType == VaultType::Fast)
    {
        SpawnGameplayFx("hit_spark", fxPos, vaultDirection, netMode);
    }

    AddRuntimeMessage(std::string{"Vault: "} + actor.lastVaultType, 1.5F);
}

void GameplaySystems::BeginPalletVault(engine::scene::Entity actorEntity, engine::scene::Entity palletEntity)
{
    auto actorIt = m_world.Actors().find(actorEntity);
    auto actorTransformIt = m_world.Transforms().find(actorEntity);
    auto palletIt = m_world.Pallets().find(palletEntity);
    auto palletTransformIt = m_world.Transforms().find(palletEntity);

    if (actorIt == m_world.Actors().end() || actorTransformIt == m_world.Transforms().end() ||
        palletIt == m_world.Pallets().end() || palletTransformIt == m_world.Transforms().end())
    {
        return;
    }

    engine::scene::ActorComponent& actor = actorIt->second;
    engine::scene::Transform& actorTransform = actorTransformIt->second;
    const engine::scene::PalletComponent& pallet = palletIt->second;

    if (actor.role != engine::scene::Role::Survivor || pallet.state != engine::scene::PalletState::Dropped ||
        actor.vaulting || actor.vaultCooldown > 0.0F)
    {
        return;
    }
    if (m_survivorState != SurvivorHealthState::Healthy && m_survivorState != SurvivorHealthState::Injured)
    {
        return;
    }

    const glm::vec3 palletNormal = pallet.halfExtents.x < pallet.halfExtents.z ? glm::vec3{1.0F, 0.0F, 0.0F} : glm::vec3{0.0F, 0.0F, 1.0F};
    const float sideSign = glm::dot(actorTransform.position - palletTransformIt->second.position, palletNormal) >= 0.0F ? 1.0F : -1.0F;
    const glm::vec3 vaultDirection = -palletNormal * sideSign;
    const float thinExtent = std::abs(palletNormal.x) * pallet.halfExtents.x + std::abs(palletNormal.z) * pallet.halfExtents.z;
    const VaultType vaultType = DeterminePalletVaultType(actor);

    actor.vaulting = true;
    actor.vaultTimer = 0.0F;
    actor.vaultStart = actorTransform.position;
    actor.vaultEnd = palletTransformIt->second.position + vaultDirection * (thinExtent + actor.capsuleRadius + 0.75F);
    actor.vaultEnd.y = actorTransform.position.y;
    actor.vaultDuration = vaultType == VaultType::Fast ? 0.42F : 0.62F;
    actor.vaultArcHeight = vaultType == VaultType::Fast ? 0.4F : 0.52F;
    actor.velocity = glm::vec3{0.0F};
    actor.sprinting = false;
    actor.forwardRunupDistance = 0.0F;
    actor.lastVaultType = std::string{"Pallet-"} + VaultTypeToText(vaultType);
    actor.collisionEnabled = false;

    const engine::fx::FxNetMode netMode = m_networkAuthorityMode ? engine::fx::FxNetMode::ServerBroadcast
                                                                  : engine::fx::FxNetMode::Local;
    SpawnGameplayFx("dust_puff", palletTransformIt->second.position + glm::vec3{0.0F, 0.2F, 0.0F}, vaultDirection, netMode);

    AddRuntimeMessage("Vault started: " + actor.lastVaultType, 1.5F);
}

void GameplaySystems::TryStunKillerFromPallet(engine::scene::Entity palletEntity)
{
    if (m_killer == 0)
    {
        return;
    }

    const auto palletIt = m_world.Pallets().find(palletEntity);
    const auto palletTransformIt = m_world.Transforms().find(palletEntity);
    const auto killerIt = m_world.Actors().find(m_killer);
    const auto killerTransformIt = m_world.Transforms().find(m_killer);

    if (palletIt == m_world.Pallets().end() || palletTransformIt == m_world.Transforms().end() ||
        killerIt == m_world.Actors().end() || killerTransformIt == m_world.Transforms().end())
    {
        return;
    }

    const glm::vec3 delta = killerTransformIt->second.position - palletTransformIt->second.position;
    const glm::vec3 extent = palletIt->second.halfExtents + glm::vec3{0.55F, 0.7F, 0.55F};
    const bool inStunZone =
        std::abs(delta.x) <= extent.x &&
        std::abs(delta.y) <= extent.y &&
        std::abs(delta.z) <= extent.z;

    if (!inStunZone)
    {
        return;
    }

    // Reset bloodlust on pallet stun (DBD-like)
    if (m_bloodlust.tier > 0)
    {
        ResetBloodlust();
    }

    killerIt->second.stunTimer = std::max(killerIt->second.stunTimer, palletIt->second.stunDuration);
    killerIt->second.velocity = glm::vec3{0.0F};
    AddRuntimeMessage("Killer stunned by pallet", 1.8F);
}

void GameplaySystems::TryPickupDownedSurvivor()
{
    if (m_survivor == 0 || m_killer == 0 ||
        (m_survivorState != SurvivorHealthState::Downed && m_survivorState != SurvivorHealthState::Trapped))
    {
        return;
    }

    const auto survivorTransformIt = m_world.Transforms().find(m_survivor);
    const auto killerTransformIt = m_world.Transforms().find(m_killer);
    if (survivorTransformIt == m_world.Transforms().end() || killerTransformIt == m_world.Transforms().end())
    {
        return;
    }

    if (DistanceXZ(survivorTransformIt->second.position, killerTransformIt->second.position) > 2.5F)
    {
        return;
    }

    if (m_survivorState == SurvivorHealthState::Trapped)
    {
        ClearTrappedSurvivorBinding(m_survivor, true);
        ItemPowerLog("Carry pickup cleared trapped survivor binding");
    }

    AddRuntimeMessage("NET carry: pickup request validated", 1.2F);
    SetSurvivorState(SurvivorHealthState::Carried, "Pickup");
    AddRuntimeMessage("NET carry: state replicated Carried", 1.2F);
}

void GameplaySystems::TryHookCarriedSurvivor(engine::scene::Entity hookEntity)
{
    if (m_survivorState != SurvivorHealthState::Carried || m_killer == 0 || m_survivor == 0)
    {
        return;
    }

    auto hookIt = m_world.Hooks().end();
    if (hookEntity != 0)
    {
        hookIt = m_world.Hooks().find(hookEntity);
    }

    if (hookIt == m_world.Hooks().end())
    {
        float bestDistance = std::numeric_limits<float>::max();
        for (auto it = m_world.Hooks().begin(); it != m_world.Hooks().end(); ++it)
        {
            if (it->second.occupied)
            {
                continue;
            }

            const auto hookTransformIt = m_world.Transforms().find(it->first);
            const auto killerTransformIt = m_world.Transforms().find(m_killer);
            if (hookTransformIt == m_world.Transforms().end() || killerTransformIt == m_world.Transforms().end())
            {
                continue;
            }

            const float distance = DistanceXZ(hookTransformIt->second.position, killerTransformIt->second.position);
            if (distance < bestDistance)
            {
                bestDistance = distance;
                hookIt = it;
            }
        }
    }

    if (hookIt == m_world.Hooks().end())
    {
        return;
    }

    const auto hookTransformIt = m_world.Transforms().find(hookIt->first);
    if (hookTransformIt == m_world.Transforms().end())
    {
        return;
    }

    hookIt->second.occupied = true;
    m_activeHookEntity = hookIt->first;
    m_hookStage = 1;
    m_hookStageTimer = 0.0F;
    m_hookEscapeAttemptsUsed = 0;
    m_carryEscapeProgress = 0.0F;
    m_carryLastQteDirection = 0;
    m_skillCheckActive = false;
    m_skillCheckMode = SkillCheckMode::None;
    m_hookSkillCheckTimeToNext = 0.0F;

    auto survivorTransformIt = m_world.Transforms().find(m_survivor);
    if (survivorTransformIt != m_world.Transforms().end())
    {
        survivorTransformIt->second.position = hookTransformIt->second.position + glm::vec3{0.0F, 0.1F, 0.0F};
    }

    SetSurvivorState(SurvivorHealthState::Hooked, "Hook");
}

void GameplaySystems::UpdateCarriedSurvivor()
{
    if (m_survivorState != SurvivorHealthState::Carried || m_survivor == 0 || m_killer == 0)
    {
        return;
    }

    const auto survivorTransformIt = m_world.Transforms().find(m_survivor);
    const auto killerTransformIt = m_world.Transforms().find(m_killer);
    if (survivorTransformIt == m_world.Transforms().end() || killerTransformIt == m_world.Transforms().end())
    {
        return;
    }

    const glm::vec3 killerForward = glm::length(killerTransformIt->second.forward) > 1.0e-5F
                                        ? glm::normalize(killerTransformIt->second.forward)
                                        : glm::vec3{0.0F, 0.0F, -1.0F};

    survivorTransformIt->second.position = killerTransformIt->second.position + glm::vec3{0.0F, 0.95F, 0.0F} - killerForward * 0.35F;
    survivorTransformIt->second.forward = killerForward;
}

void GameplaySystems::UpdateCarryEscapeQte(bool survivorInputEnabled, float fixedDt)
{
    if (m_survivorState != SurvivorHealthState::Carried)
    {
        m_carryEscapeProgress = 0.0F;
        m_carryLastQteDirection = 0;
        return;
    }

    constexpr float kPassiveDecay = 0.22F;
    constexpr float kValidPressGain = 0.17F;
    constexpr float kInvalidPressPenalty = 0.08F;

    if (m_carryInputGraceTimer > 0.0F)
    {
        m_carryInputGraceTimer = std::max(0.0F, m_carryInputGraceTimer - fixedDt);
        return;
    }

    m_carryEscapeProgress = std::max(0.0F, m_carryEscapeProgress - kPassiveDecay * fixedDt);

    if (survivorInputEnabled)
    {
        bool leftPressed = false;
        bool rightPressed = false;
        ConsumeWigglePressedForSurvivor(leftPressed, rightPressed);

        int direction = 0;
        if (leftPressed)
        {
            direction = -1;
        }
        else if (rightPressed)
        {
            direction = 1;
        }

        if (direction != 0)
        {
            if (m_carryLastQteDirection == 0 || direction != m_carryLastQteDirection)
            {
                m_carryEscapeProgress = std::min(1.0F, m_carryEscapeProgress + kValidPressGain);
                m_carryLastQteDirection = direction;
            }
            else
            {
                m_carryEscapeProgress = std::max(0.0F, m_carryEscapeProgress - kInvalidPressPenalty);
            }
        }
    }

    if (m_carryEscapeProgress >= 1.0F)
    {
        auto survivorTransformIt = m_world.Transforms().find(m_survivor);
        auto killerTransformIt = m_world.Transforms().find(m_killer);
        if (survivorTransformIt != m_world.Transforms().end() && killerTransformIt != m_world.Transforms().end())
        {
            survivorTransformIt->second.position = killerTransformIt->second.position + glm::vec3{-0.9F, 0.0F, -0.9F};
        }

        m_carryEscapeProgress = 0.0F;
        m_carryLastQteDirection = 0;
        SetSurvivorState(SurvivorHealthState::Injured, "Carry escape");
        AddRuntimeMessage("Carry drop reason: wiggle success", 1.5F);
    }
}

void GameplaySystems::UpdateHookStages(float fixedDt, bool hookAttemptPressed, bool hookSkillCheckPressed)
{
    if (m_survivorState != SurvivorHealthState::Hooked)
    {
        m_hookStage = 0;
        m_hookStageTimer = 0.0F;
        m_hookEscapeAttemptsUsed = 0;
        if (m_skillCheckMode == SkillCheckMode::HookStruggle)
        {
            m_skillCheckMode = SkillCheckMode::None;
            m_skillCheckActive = false;
        }
        return;
    }

    if (m_hookStage <= 0)
    {
        m_hookStage = 1;
        m_hookStageTimer = 0.0F;
        m_hookEscapeAttemptsUsed = 0;
    }

    const float stageDuration = (m_hookStage == 1) ? m_hookStageOneDuration : m_hookStageTwoDuration;

    if (m_hookStage == 1 && hookAttemptPressed)
    {
        if (m_hookEscapeAttemptsUsed < m_hookEscapeAttemptsMax)
        {
            ++m_hookEscapeAttemptsUsed;
            std::uniform_real_distribution<float> chanceDist(0.0F, 1.0F);
            const bool success = chanceDist(m_rng) <= m_hookEscapeChance;
            if (success)
            {
                SetSurvivorState(SurvivorHealthState::Injured, "Self unhook success");
                AddRuntimeMessage("Self unhook succeeded!", 1.7F);
                return;
            }

            const int attemptsLeft = std::max(0, m_hookEscapeAttemptsMax - m_hookEscapeAttemptsUsed);
            AddRuntimeMessage("Self unhook failed. Attempts left: " + std::to_string(attemptsLeft), 1.7F);
            if (m_hookEscapeAttemptsUsed >= m_hookEscapeAttemptsMax)
            {
                m_hookStage = 2;
                m_hookStageTimer = 0.0F;
                m_hookSkillCheckTimeToNext = 1.2F;
                m_skillCheckMode = SkillCheckMode::HookStruggle;
                AddRuntimeMessage("Hook stage advanced to Stage 2 (attempt limit reached)", 1.9F);
            }
        }
    }

    if (m_hookStage == 2)
    {
        m_skillCheckMode = SkillCheckMode::HookStruggle;
        if (m_skillCheckActive && m_skillCheckMode == SkillCheckMode::HookStruggle)
        {
            m_skillCheckNeedle += m_skillCheckNeedleSpeed * fixedDt;
            if (hookSkillCheckPressed)
            {
                constexpr float kHitMargin = 0.06F;
                const float expandedStart = m_skillCheckSuccessStart - kHitMargin;
                const float expandedEnd = m_skillCheckSuccessEnd + kHitMargin;
                
                const bool success = m_skillCheckNeedle >= expandedStart && m_skillCheckNeedle <= expandedEnd;
                CompleteSkillCheck(success, false);
            }
            else if (m_skillCheckNeedle >= 1.0F)
            {
                CompleteSkillCheck(false, true);
            }
        }
        else
        {
            m_hookSkillCheckTimeToNext -= fixedDt;
            if (m_hookSkillCheckTimeToNext <= 0.0F)
            {
                std::uniform_real_distribution<float> zoneStartDist(0.16F, 0.80F);
                std::uniform_real_distribution<float> zoneSizeDist(0.07F, 0.12F);
                const float zoneStart = zoneStartDist(m_rng);
                const float zoneSize = zoneSizeDist(m_rng);
                m_skillCheckSuccessStart = zoneStart;
                m_skillCheckSuccessEnd = std::min(0.98F, zoneStart + zoneSize);
                m_skillCheckNeedle = 0.0F;
                m_skillCheckActive = true;
                m_skillCheckMode = SkillCheckMode::HookStruggle;
                AddRuntimeMessage("Hook struggle skill check: SPACE", 1.2F);
            }
        }
    }

    m_hookStageTimer += fixedDt;
    if (m_hookStageTimer < stageDuration)
    {
        return;
    }

    if (m_hookStage == 1)
    {
        m_hookStage = 2;
        m_hookStageTimer = 0.0F;
        m_hookSkillCheckTimeToNext = 1.0F;
        m_skillCheckMode = SkillCheckMode::HookStruggle;
        AddRuntimeMessage("Hook stage advanced to Stage 2", 1.8F);
        return;
    }

    m_hookStage = 3;
    AddRuntimeMessage("Hook stage advanced to Stage 3", 1.5F);
    SetSurvivorState(SurvivorHealthState::Dead, "Hook stage 3 timer");
}

void GameplaySystems::UpdateGeneratorRepair(bool holdingRepair, bool skillCheckPressed, float fixedDt)
{
    if (m_activeRepairGenerator == 0)
    {
        return;
    }

    const auto generatorIt = m_world.Generators().find(m_activeRepairGenerator);
    const auto generatorTransformIt = m_world.Transforms().find(m_activeRepairGenerator);
    const auto survivorIt = m_world.Actors().find(m_survivor);
    const auto survivorTransformIt = m_world.Transforms().find(m_survivor);
    if (generatorIt == m_world.Generators().end() || generatorTransformIt == m_world.Transforms().end() ||
        survivorIt == m_world.Actors().end() || survivorTransformIt == m_world.Transforms().end())
    {
        StopGeneratorRepair();
        return;
    }

    if (generatorIt->second.completed)
    {
        StopGeneratorRepair();
        return;
    }

    if (m_survivorState != SurvivorHealthState::Healthy && m_survivorState != SurvivorHealthState::Injured)
    {
        StopGeneratorRepair();
        return;
    }

    const float distance = DistanceXZ(survivorTransformIt->second.position, generatorTransformIt->second.position);
    if (distance > 2.6F || !holdingRepair)
    {
        StopGeneratorRepair();
        return;
    }

    const float kRepairRate = 1.0F / std::max(1.0F, m_tuning.generatorRepairSecondsBase);
    generatorIt->second.progress = glm::clamp(generatorIt->second.progress + kRepairRate * fixedDt, 0.0F, 1.0F);

    if (generatorIt->second.progress >= 1.0F)
    {
        generatorIt->second.progress = 1.0F;
        generatorIt->second.completed = true;
        RefreshGeneratorsCompleted();
        AddRuntimeMessage("Generator completed", 1.8F);
        StopGeneratorRepair();
        return;
    }

    if (m_skillCheckActive)
    {
        m_skillCheckNeedle += m_skillCheckNeedleSpeed * fixedDt;
        if (skillCheckPressed)
        {
            // Add margin for forgiveness - makes hitbox larger
            constexpr float kHitMargin = 0.06F;
            const float expandedStart = m_skillCheckSuccessStart - kHitMargin;
            const float expandedEnd = m_skillCheckSuccessEnd + kHitMargin;
            
            const bool success = m_skillCheckNeedle >= expandedStart && m_skillCheckNeedle <= expandedEnd;
            CompleteSkillCheck(success, false);
        }
        else if (m_skillCheckNeedle >= 1.0F)
        {
            CompleteSkillCheck(false, true);
        }
        return;
    }

    m_skillCheckTimeToNext -= fixedDt;
    if (m_skillCheckTimeToNext <= 0.0F)
    {
        std::uniform_real_distribution<float> zoneStartDist(0.14F, 0.82F);
        std::uniform_real_distribution<float> zoneSizeDist(0.06F, 0.11F);

        const float zoneStart = zoneStartDist(m_rng);
        const float zoneSize = zoneSizeDist(m_rng);
        m_skillCheckSuccessStart = zoneStart;
        m_skillCheckSuccessEnd = std::min(0.98F, zoneStart + zoneSize);
        m_skillCheckNeedle = 0.0F;
        m_skillCheckActive = true;
        AddRuntimeMessage("Skill Check: press SPACE in success zone", 1.6F);
    }
}

void GameplaySystems::StopGeneratorRepair()
{
    if (m_skillCheckActive && m_skillCheckMode == SkillCheckMode::Generator)
    {
        auto generatorIt = m_world.Generators().find(m_activeRepairGenerator);
        if (generatorIt != m_world.Generators().end())
        {
            generatorIt->second.progress = glm::clamp(generatorIt->second.progress - 0.1F, 0.0F, 1.0F);
        }
        
        glm::vec3 fxOrigin{0.0F, 1.0F, 0.0F};
        glm::vec3 fxForward{0.0F, 1.0F, 0.0F};
        const auto generatorTransformIt = m_world.Transforms().find(m_activeRepairGenerator);
        if (generatorTransformIt != m_world.Transforms().end())
        {
            fxOrigin = generatorTransformIt->second.position + glm::vec3{0.0F, 0.7F, 0.0F};
            fxForward = generatorTransformIt->second.forward;
        }
        
        const engine::fx::FxNetMode netMode = m_networkAuthorityMode ? engine::fx::FxNetMode::ServerBroadcast
                                                                      : engine::fx::FxNetMode::Local;
        SpawnGameplayFx("blood_spray", fxOrigin, -fxForward, netMode);
        AddRuntimeMessage("Skill Check abandoned (penalty)", 1.3F);
        
        m_skillCheckActive = false;
        m_skillCheckNeedle = 0.0F;
        m_skillCheckSuccessStart = 0.0F;
        m_skillCheckSuccessEnd = 0.0F;
        m_skillCheckMode = SkillCheckMode::None;
    }
    
    m_activeRepairGenerator = 0;
    ScheduleNextSkillCheck();
}

void GameplaySystems::BeginOrContinueGeneratorRepair(engine::scene::Entity generatorEntity)
{
    const auto generatorIt = m_world.Generators().find(generatorEntity);
    if (generatorIt == m_world.Generators().end() || generatorIt->second.completed)
    {
        return;
    }

    m_activeRepairGenerator = generatorEntity;
    m_skillCheckMode = SkillCheckMode::Generator;
    StopSelfHeal();
    if (m_skillCheckTimeToNext <= 0.0F || m_skillCheckTimeToNext > 8.0F)
    {
        ScheduleNextSkillCheck();
    }
    AddRuntimeMessage("Generator repair started (hold E)", 1.2F);
}

void GameplaySystems::BeginSelfHeal()
{
    if (m_survivorState != SurvivorHealthState::Injured)
    {
        return;
    }

    StopGeneratorRepair();
    m_selfHealActive = true;
    m_skillCheckMode = SkillCheckMode::SelfHeal;
    if (m_skillCheckTimeToNext <= 0.0F || m_skillCheckTimeToNext > 8.0F)
    {
        ScheduleNextSkillCheck();
    }
    AddRuntimeMessage("Self-heal started (hold E)", 1.0F);
}

void GameplaySystems::StopSelfHeal()
{
    if (!m_selfHealActive)
    {
        return;
    }

    m_selfHealActive = false;
    if (m_skillCheckMode == SkillCheckMode::SelfHeal)
    {
        m_skillCheckMode = SkillCheckMode::None;
    }
    if (!m_skillCheckActive)
    {
        ScheduleNextSkillCheck();
    }
}

void GameplaySystems::UpdateSelfHeal(bool holdingHeal, bool skillCheckPressed, float fixedDt)
{
    if (!m_selfHealActive)
    {
        return;
    }

    if (m_survivorState != SurvivorHealthState::Injured || !holdingHeal)
    {
        StopSelfHeal();
        return;
    }

    const float kSelfHealRate = 1.0F / std::max(0.1F, m_tuning.healDurationSeconds);
    m_selfHealProgress = glm::clamp(m_selfHealProgress + kSelfHealRate * fixedDt, 0.0F, 1.0F);

    if (m_selfHealProgress >= 1.0F)
    {
        m_selfHealProgress = 1.0F;
        SetSurvivorState(SurvivorHealthState::Healthy, "Self-heal completed");
        StopSelfHeal();
        return;
    }

    if (m_skillCheckActive && m_skillCheckMode == SkillCheckMode::SelfHeal)
    {
        m_skillCheckNeedle += m_skillCheckNeedleSpeed * fixedDt;
        if (skillCheckPressed)
        {
            constexpr float kHitMargin = 0.06F;
            const float expandedStart = m_skillCheckSuccessStart - kHitMargin;
            const float expandedEnd = m_skillCheckSuccessEnd + kHitMargin;
            
            const bool success = m_skillCheckNeedle >= expandedStart && m_skillCheckNeedle <= expandedEnd;
            CompleteSkillCheck(success, false);
        }
        else if (m_skillCheckNeedle >= 1.0F)
        {
            CompleteSkillCheck(false, true);
        }
        return;
    }

    m_skillCheckTimeToNext -= fixedDt;
    if (m_skillCheckTimeToNext <= 0.0F)
    {
        std::uniform_real_distribution<float> zoneStartDist(0.14F, 0.82F);
        std::uniform_real_distribution<float> zoneSizeDist(0.08F, 0.16F);

        const float zoneStart = zoneStartDist(m_rng);
        const float zoneSize = zoneSizeDist(m_rng);
        m_skillCheckSuccessStart = zoneStart;
        m_skillCheckSuccessEnd = std::min(0.98F, zoneStart + zoneSize);
        m_skillCheckNeedle = 0.0F;
        m_skillCheckActive = true;
        m_skillCheckMode = SkillCheckMode::SelfHeal;
        AddRuntimeMessage("Self-heal skill check", 1.2F);
    }
}

void GameplaySystems::CompleteSkillCheck(bool success, bool timeout)
{
    const bool hookSkillCheck = m_survivorState == SurvivorHealthState::Hooked && m_skillCheckMode == SkillCheckMode::HookStruggle;
    if (m_activeRepairGenerator == 0 && !hookSkillCheck)
    {
        if (!m_selfHealActive)
        {
            return;
        }
    }

    glm::vec3 fxOrigin{0.0F, 1.0F, 0.0F};
    glm::vec3 fxForward{0.0F, 1.0F, 0.0F};
    if (m_activeRepairGenerator != 0)
    {
        const auto generatorTransformIt = m_world.Transforms().find(m_activeRepairGenerator);
        if (generatorTransformIt != m_world.Transforms().end())
        {
            fxOrigin = generatorTransformIt->second.position + glm::vec3{0.0F, 0.7F, 0.0F};
            fxForward = generatorTransformIt->second.forward;
        }
    }
    else
    {
        const auto survivorTransformIt = m_world.Transforms().find(m_survivor);
        if (survivorTransformIt != m_world.Transforms().end())
        {
            fxOrigin = survivorTransformIt->second.position + glm::vec3{0.0F, 0.8F, 0.0F};
            fxForward = survivorTransformIt->second.forward;
        }
    }
    const engine::fx::FxNetMode netMode = m_networkAuthorityMode ? engine::fx::FxNetMode::ServerBroadcast
                                                                  : engine::fx::FxNetMode::Local;

    if (success)
    {
        if (hookSkillCheck)
        {
            AddRuntimeMessage("Hook skill check success", 1.1F);
        }
        else if (m_selfHealActive)
        {
            m_selfHealProgress = glm::clamp(m_selfHealProgress + 0.08F, 0.0F, 1.0F);
        }
        else
        {
            auto generatorIt = m_world.Generators().find(m_activeRepairGenerator);
            if (generatorIt != m_world.Generators().end())
            {
                generatorIt->second.progress = glm::clamp(generatorIt->second.progress + 0.05F, 0.0F, 1.0F);
            }
        }
        SpawnGameplayFx("hit_spark", fxOrigin, fxForward, netMode);
        AddRuntimeMessage("Skill Check success", 1.2F);
    }
    else
    {
        if (hookSkillCheck)
        {
            m_hookStageTimer = std::min(
                (m_hookStage == 1 ? m_hookStageOneDuration : m_hookStageTwoDuration),
                m_hookStageTimer + m_hookStageFailPenaltySeconds
            );
        }
        else if (m_selfHealActive)
        {
            m_selfHealProgress = glm::clamp(m_selfHealProgress - 0.1F, 0.0F, 1.0F);
        }
        else
        {
            auto generatorIt = m_world.Generators().find(m_activeRepairGenerator);
            if (generatorIt != m_world.Generators().end())
            {
                generatorIt->second.progress = glm::clamp(generatorIt->second.progress - 0.1F, 0.0F, 1.0F);
            }
        }
        SpawnGameplayFx("blood_spray", fxOrigin, -fxForward, netMode);
        AddRuntimeMessage(timeout ? "Skill Check missed (penalty)" : "Skill Check failed (penalty)", 1.3F);
    }

    m_skillCheckActive = false;
    m_skillCheckNeedle = 0.0F;
    m_skillCheckSuccessStart = 0.0F;
    m_skillCheckSuccessEnd = 0.0F;

    if (m_selfHealActive && m_selfHealProgress >= 1.0F)
    {
        m_selfHealProgress = 1.0F;
        SetSurvivorState(SurvivorHealthState::Healthy, "Self-heal completed");
        StopSelfHeal();
        return;
    }

    auto generatorIt = m_world.Generators().find(m_activeRepairGenerator);
    if (!hookSkillCheck && !m_selfHealActive && generatorIt != m_world.Generators().end() && generatorIt->second.progress >= 1.0F)
    {
        generatorIt->second.progress = 1.0F;
        generatorIt->second.completed = true;
        RefreshGeneratorsCompleted();
        AddRuntimeMessage("Generator completed", 1.8F);
        StopGeneratorRepair();
        return;
    }

    if (hookSkillCheck)
    {
        m_skillCheckMode = SkillCheckMode::HookStruggle;
        std::uniform_real_distribution<float> nextDist(1.4F, 3.2F);
        m_hookSkillCheckTimeToNext = nextDist(m_rng);
    }
    else
    {
        m_skillCheckMode = m_selfHealActive ? SkillCheckMode::SelfHeal : SkillCheckMode::Generator;
        ScheduleNextSkillCheck();
    }
}

void GameplaySystems::ScheduleNextSkillCheck()
{
    std::uniform_real_distribution<float> dist(m_tuning.skillCheckMinInterval, m_tuning.skillCheckMaxInterval);
    m_skillCheckTimeToNext = dist(m_rng);
}

void GameplaySystems::RefreshGeneratorsCompleted()
{
    int completed = 0;
    for (const auto& [_, generator] : m_world.Generators())
    {
        if (generator.completed || generator.progress >= 1.0F)
        {
            ++completed;
        }
    }
    m_generatorsCompleted = completed;
}

void GameplaySystems::ApplyKillerAttackAftermath(bool hit, bool lungeAttack)
{
    if (hit)
    {
        m_survivorHitHasteTimer = std::max(m_survivorHitHasteTimer, m_survivorHitHasteSeconds);
        m_killerSlowTimer = std::max(m_killerSlowTimer, m_killerHitSlowSeconds);
        m_killerSlowMultiplier = m_killerHitSlowMultiplier;
        if (lungeAttack)
        {
            AddRuntimeMessage("Hit: survivor speed boost, killer slow", 1.1F);
        }
        return;
    }

    m_killerSlowTimer = std::max(m_killerSlowTimer, m_killerMissSlowSeconds);
    m_killerSlowMultiplier = m_killerMissSlowMultiplier;
    if (lungeAttack)
    {
        AddRuntimeMessage("Lunge missed: short killer slow", 1.0F);
    }
}

void GameplaySystems::ApplySurvivorHit()
{
    // Reset bloodlust on hit (DBD-like)
    if (m_bloodlust.tier > 0)
    {
        ResetBloodlust();
    }

    if (m_survivorState == SurvivorHealthState::Healthy)
    {
        SetSurvivorState(SurvivorHealthState::Injured, "Killer hit");
        return;
    }

    if (m_survivorState == SurvivorHealthState::Injured)
    {
        SetSurvivorState(SurvivorHealthState::Downed, "Killer hit");
        return;
    }

    if (m_survivorState == SurvivorHealthState::Trapped)
    {
        SetSurvivorState(SurvivorHealthState::Downed, "Killer hit trapped survivor");
    }
}

bool GameplaySystems::SetSurvivorState(SurvivorHealthState nextState, const std::string& reason, bool force)
{
    const SurvivorHealthState previous = m_survivorState;
    if (!force && !CanTransitionSurvivorState(previous, nextState))
    {
        return false;
    }

    m_survivorState = nextState;

    if (previous == SurvivorHealthState::Hooked && nextState != SurvivorHealthState::Hooked)
    {
        auto activeHookIt = m_world.Hooks().find(m_activeHookEntity);
        if (activeHookIt != m_world.Hooks().end())
        {
            activeHookIt->second.occupied = false;
        }
        m_activeHookEntity = 0;
    }
    if (previous == SurvivorHealthState::Trapped && nextState != SurvivorHealthState::Trapped)
    {
        ClearTrappedSurvivorBinding(m_survivor, true);
    }

    if (nextState == SurvivorHealthState::Carried)
    {
        m_carryEscapeProgress = 0.0F;
        m_carryLastQteDirection = 0;
        m_carryInputGraceTimer = 0.65F;
        m_survivorWigglePressQueue.clear();
    }

    if (nextState == SurvivorHealthState::Hooked)
    {
        m_hookStage = std::max(1, m_hookStage);
        m_hookStageTimer = 0.0F;
        m_hookEscapeAttemptsUsed = 0;
        m_hookSkillCheckTimeToNext = 1.2F;
        m_skillCheckActive = false;
        m_skillCheckMode = SkillCheckMode::None;
    }
    else
    {
        m_hookStage = 0;
        m_hookStageTimer = 0.0F;
        m_hookEscapeAttemptsUsed = 0;
        if (m_skillCheckMode == SkillCheckMode::HookStruggle)
        {
            m_skillCheckMode = SkillCheckMode::None;
            m_skillCheckActive = false;
        }
    }

    if (nextState != SurvivorHealthState::Healthy && nextState != SurvivorHealthState::Injured)
    {
        StopGeneratorRepair();
        StopSelfHeal();
    }
    if (nextState == SurvivorHealthState::Healthy)
    {
        m_selfHealProgress = 0.0F;
    }
    if (nextState == SurvivorHealthState::Injured && previous != SurvivorHealthState::Injured)
    {
        m_selfHealProgress = 0.0F;
    }
    if (nextState != SurvivorHealthState::Healthy && nextState != SurvivorHealthState::Injured)
    {
        m_survivorHitHasteTimer = 0.0F;
    }

    const auto survivorActorIt = m_world.Actors().find(m_survivor);
    if (survivorActorIt != m_world.Actors().end())
    {
        engine::scene::ActorComponent& actor = survivorActorIt->second;
        actor.carried = nextState == SurvivorHealthState::Carried;
        actor.crouching = false;
        actor.crawling = false;
        actor.sprinting = false;
        actor.forwardRunupDistance = 0.0F;
        actor.velocity = glm::vec3{0.0F};
        actor.collisionEnabled = (nextState == SurvivorHealthState::Healthy ||
                                  nextState == SurvivorHealthState::Injured ||
                                  nextState == SurvivorHealthState::Downed ||
                                  nextState == SurvivorHealthState::Trapped)
                                     ? m_collisionEnabled
                                     : false;
    }

    const auto survivorTransformIt = m_world.Transforms().find(m_survivor);
    if (survivorTransformIt != m_world.Transforms().end() && nextState == SurvivorHealthState::Dead)
    {
        survivorTransformIt->second.position = glm::vec3{0.0F, -200.0F, 0.0F};
    }

    AddRuntimeMessage(
        std::string{"Survivor state: "} + SurvivorStateToText(previous) + " -> " + SurvivorStateToText(nextState) +
            " (" + reason + ")",
        2.2F
    );
    return true;
}

bool GameplaySystems::CanTransitionSurvivorState(SurvivorHealthState from, SurvivorHealthState to) const
{
    if (from == to)
    {
        return true;
    }

    switch (from)
    {
        case SurvivorHealthState::Healthy:
            return to == SurvivorHealthState::Injured || to == SurvivorHealthState::Trapped;
        case SurvivorHealthState::Injured:
            return to == SurvivorHealthState::Healthy || to == SurvivorHealthState::Downed || to == SurvivorHealthState::Trapped;
        case SurvivorHealthState::Downed:
            return to == SurvivorHealthState::Carried;
        case SurvivorHealthState::Trapped:
            return to == SurvivorHealthState::Injured ||
                   to == SurvivorHealthState::Downed ||
                   to == SurvivorHealthState::Carried;
        case SurvivorHealthState::Carried:
            return to == SurvivorHealthState::Hooked ||
                   to == SurvivorHealthState::Downed ||
                   to == SurvivorHealthState::Injured;
        case SurvivorHealthState::Hooked:
            return to == SurvivorHealthState::Dead || to == SurvivorHealthState::Injured;
        case SurvivorHealthState::Dead:
            return false;
        default:
            return false;
    }
}

const char* GameplaySystems::SurvivorStateToText(SurvivorHealthState state)
{
    switch (state)
    {
        case SurvivorHealthState::Healthy: return "Healthy";
        case SurvivorHealthState::Injured: return "Injured";
        case SurvivorHealthState::Downed: return "Downed";
        case SurvivorHealthState::Trapped: return "Trapped";
        case SurvivorHealthState::Carried: return "Carried";
        case SurvivorHealthState::Hooked: return "Hooked";
        case SurvivorHealthState::Dead: return "Dead";
        default: return "Unknown";
    }
}

const char* GameplaySystems::KillerAttackStateToText(KillerAttackState state) const
{
    switch (state)
    {
        case KillerAttackState::Idle: return "Idle";
        case KillerAttackState::ChargingLunge: return "Charging";
        case KillerAttackState::Lunging: return "Lunging";
        case KillerAttackState::Recovering: return "Recovering";
        default: return "Idle";
    }
}

std::string GameplaySystems::BuildMovementStateText(engine::scene::Entity entity, const engine::scene::ActorComponent& actor) const
{
    if (entity == m_survivor)
    {
        if (m_survivorState == SurvivorHealthState::Carried)
        {
            return "Carried";
        }
        if (m_survivorState == SurvivorHealthState::Trapped)
        {
            return "Trapped";
        }
        if (m_survivorState == SurvivorHealthState::Downed)
        {
            return "Crawling";
        }
    }
    if (actor.crouching)
    {
        return "Crouching";
    }

    const float speed = glm::length(glm::vec2{actor.velocity.x, actor.velocity.z});
    if (actor.sprinting && speed > 0.2F)
    {
        return "Running";
    }
    if (speed > 0.2F)
    {
        return "Walking";
    }
    return "Idle";
}

engine::fx::FxSystem::FxInstanceId GameplaySystems::SpawnGameplayFx(
    const std::string& assetId,
    const glm::vec3& position,
    const glm::vec3& forward,
    engine::fx::FxNetMode mode
)
{
    if (assetId.empty())
    {
        return 0;
    }
    return m_fxSystem.Spawn(assetId, position, forward, {}, mode);
}

GameplaySystems::RoleCommand GameplaySystems::BuildLocalRoleCommand(
    engine::scene::Role role,
    const engine::platform::Input& input,
    const engine::platform::ActionBindings& bindings,
    bool controlsEnabled,
    bool inputLocked
) const
{
    RoleCommand command;
    if (!controlsEnabled || inputLocked)
    {
        return command;
    }

    command.moveAxis = ReadMoveAxis(input, bindings);
    command.lookDelta = input.MouseDelta();
    if (m_invertLookY)
    {
        command.lookDelta.y = -command.lookDelta.y;
    }
    command.sprinting = role == engine::scene::Role::Survivor && bindings.IsDown(input, engine::platform::InputAction::Sprint);
    command.crouchHeld = bindings.IsDown(input, engine::platform::InputAction::Crouch);
    command.jumpPressed = input.IsKeyPressed(GLFW_KEY_SPACE);
    command.interactPressed = bindings.IsPressed(input, engine::platform::InputAction::Interact);
    command.interactHeld = bindings.IsDown(input, engine::platform::InputAction::Interact);
    command.attackPressed = bindings.IsPressed(input, engine::platform::InputAction::AttackShort);
    command.attackHeld = bindings.IsDown(input, engine::platform::InputAction::AttackShort) ||
                         bindings.IsDown(input, engine::platform::InputAction::AttackLunge);
    command.attackReleased = bindings.IsReleased(input, engine::platform::InputAction::AttackShort) ||
                             bindings.IsReleased(input, engine::platform::InputAction::AttackLunge);
    command.lungeHeld = bindings.IsDown(input, engine::platform::InputAction::AttackLunge);
    command.useAltPressed = input.IsMousePressed(GLFW_MOUSE_BUTTON_RIGHT);
    command.useAltHeld = input.IsMouseDown(GLFW_MOUSE_BUTTON_RIGHT);
    command.useAltReleased = input.IsMouseReleased(GLFW_MOUSE_BUTTON_RIGHT);
    command.dropItemPressed = role == engine::scene::Role::Survivor && input.IsKeyPressed(GLFW_KEY_R);
    command.pickupItemPressed = role == engine::scene::Role::Survivor && input.IsMousePressed(GLFW_MOUSE_BUTTON_LEFT);
    command.wiggleLeftPressed = bindings.IsPressed(input, engine::platform::InputAction::MoveLeft);
    command.wiggleRightPressed = bindings.IsPressed(input, engine::platform::InputAction::MoveRight);
    return command;
}

void GameplaySystems::UpdateInteractBuffer(engine::scene::Role role, const RoleCommand& command, float fixedDt)
{
    const std::uint8_t index = RoleToIndex(role);
    if (command.interactPressed)
    {
        m_interactBufferRemaining[index] = m_interactBufferWindowSeconds;
        return;
    }

    m_interactBufferRemaining[index] = std::max(0.0F, m_interactBufferRemaining[index] - fixedDt);
}

bool GameplaySystems::ConsumeInteractBuffered(engine::scene::Role role)
{
    const std::uint8_t index = RoleToIndex(role);
    if (m_interactBufferRemaining[index] <= 0.0F)
    {
        return false;
    }

    m_interactBufferRemaining[index] = 0.0F;
    return true;
}

void GameplaySystems::ConsumeWigglePressedForSurvivor(bool& leftPressed, bool& rightPressed)
{
    leftPressed = false;
    rightPressed = false;
    if (m_survivorWigglePressQueue.empty())
    {
        return;
    }

    const int value = m_survivorWigglePressQueue.front();
    m_survivorWigglePressQueue.erase(m_survivorWigglePressQueue.begin());
    leftPressed = value < 0;
    rightPressed = value > 0;
}

std::uint8_t GameplaySystems::RoleToIndex(engine::scene::Role role)
{
    return role == engine::scene::Role::Survivor ? 0U : 1U;
}

engine::scene::Role GameplaySystems::OppositeRole(engine::scene::Role role)
{
    return role == engine::scene::Role::Survivor ? engine::scene::Role::Killer : engine::scene::Role::Survivor;
}

void GameplaySystems::AddRuntimeMessage(const std::string& text, float ttl)
{
    std::cout << text << "\n";
    m_messages.push_back(TimedMessage{text, ttl});
    if (m_messages.size() > 6)
    {
        m_messages.erase(m_messages.begin());
    }
}

float GameplaySystems::DistanceXZ(const glm::vec3& a, const glm::vec3& b)
{
    const glm::vec2 d = glm::vec2{a.x, a.z} - glm::vec2{b.x, b.z};
    return glm::length(d);
}

float GameplaySystems::DistancePointToSegment(const glm::vec3& point, const glm::vec3& segmentA, const glm::vec3& segmentB)
{
    const glm::vec3 ab = segmentB - segmentA;
    const float denominator = glm::dot(ab, ab);
    if (denominator <= 1.0e-7F)
    {
        return glm::length(point - segmentA);
    }

    const float t = glm::clamp(glm::dot(point - segmentA, ab) / denominator, 0.0F, 1.0F);
    const glm::vec3 closest = segmentA + ab * t;
    return glm::length(point - closest);
}

glm::vec3 GameplaySystems::ForwardFromYawPitch(float yaw, float pitch)
{
    const float cosPitch = std::cos(pitch);
    return glm::normalize(glm::vec3{
        std::sin(yaw) * cosPitch,
        std::sin(pitch),
        -std::cos(yaw) * cosPitch,
    });
}

bool GameplaySystems::IsSurvivorInKillerFOV(
    const glm::vec3& killerPos, const glm::vec3& killerForward,
    const glm::vec3& survivorPos, float fovDegrees)
{
    glm::vec3 toSurvivor = survivorPos - killerPos;
    toSurvivor.y = 0.0F; // Flatten to XZ plane

    const float distance = glm::length(toSurvivor);
    if (distance < 1.0F) return true; // Too close, definitely in FOV

    const glm::vec3 dirToSurvivor = glm::normalize(toSurvivor);
    const glm::vec3 killerFlat = glm::normalize(glm::vec3(killerForward.x, 0.0F, killerForward.z));

    const float fovRad = glm::radians(fovDegrees);
    const float cosHalfFov = std::cos(fovRad * 0.5F);

    return glm::dot(killerFlat, dirToSurvivor) >= cosHalfFov;
}

bool GameplaySystems::IsSurvivorInKillerCenterFOV(
    const glm::vec3& killerPos, const glm::vec3& killerForward,
    const glm::vec3& survivorPos)
{
    // DBD-like: ±35° from killer's forward (center FOV for chase gating)
    constexpr float centerFovDegrees = 35.0F;
    return IsSurvivorInKillerFOV(killerPos, killerForward, survivorPos, centerFovDegrees * 2.0F);
}

//==============================================================================
// Bloodlust System (DBD-like)
//==============================================================================

void GameplaySystems::ResetBloodlust()
{
    const int oldTier = m_bloodlust.tier;
    m_bloodlust.tier = 0;
    m_bloodlust.timeInChase = 0.0F;
    m_bloodlust.lastTierChangeTime = 0.0F;

    // Re-apply speed to remove bloodlust bonus
    SetRoleSpeedPercent("killer", m_killerSpeedPercent);

    if (oldTier > 0)
    {
        AddRuntimeMessage("Bloodlust reset", 1.0F);
    }
}

void GameplaySystems::SetBloodlustTier(int tier)
{
    const int clampedTier = glm::clamp(tier, 0, 3);
    if (m_bloodlust.tier != clampedTier)
    {
        m_bloodlust.tier = clampedTier;
        m_bloodlust.lastTierChangeTime = m_elapsedSeconds;
        AddRuntimeMessage("Bloodlust tier " + std::to_string(clampedTier), 1.0F);
    }
}

float GameplaySystems::GetBloodlustSpeedMultiplier() const
{
    // DBD-like bloodlust tiers
    // Tier 0: 100% (no bonus)
    // Tier 1: 120% (at 15s in chase)
    // Tier 2: 125% (at 25s in chase)
    // Tier 3: 130% (at 35s in chase)
    switch (m_bloodlust.tier)
    {
        case 1: return 1.20F;
        case 2: return 1.25F;
        case 3: return 1.30F;
        default: return 1.0F;
    }
}

void GameplaySystems::UpdateBloodlust(float fixedDt)
{
    // Bloodlust only progresses during active chase
    if (!m_chase.isChasing)
    {
        // Reset immediately when chase ends
        if (m_bloodlust.tier > 0 || m_bloodlust.timeInChase > 0.0F)
        {
            ResetBloodlust();
        }
        return;
    }

    // Only server-authoritative mode should compute bloodlust
    // For now, we always compute (will be replicated in multiplayer)

    m_bloodlust.timeInChase += fixedDt;

    // DBD-like tier thresholds
    // Tier 1: 15s → 120% speed
    // Tier 2: 25s → 125% speed
    // Tier 3: 35s → 130% speed
    const int newTier = [this]() -> int {
        if (m_bloodlust.timeInChase >= 35.0F) return 3;
        if (m_bloodlust.timeInChase >= 25.0F) return 2;
        if (m_bloodlust.timeInChase >= 15.0F) return 1;
        return 0;
    }();

    if (newTier != m_bloodlust.tier)
    {
        SetBloodlustTier(newTier);
        // Apply new speed multiplier
        SetRoleSpeedPercent("killer", m_killerSpeedPercent);
    }
}

// ============================================================================
// Phase B2/B3: Scratch Marks and Blood Pools (Refactored for DBD accuracy)
// ============================================================================

float GameplaySystems::DeterministicRandom(const glm::vec3& position, int seed)
{
    unsigned int hash = static_cast<unsigned int>(seed);
    hash ^= static_cast<unsigned int>(static_cast<int>(position.x * 1000.0F));
    hash ^= static_cast<unsigned int>(static_cast<int>(position.y * 1000.0F)) << 8;
    hash ^= static_cast<unsigned int>(static_cast<int>(position.z * 1000.0F)) << 16;
    hash = (hash ^ (hash >> 16)) * 0x85ebca6b;
    hash = (hash ^ (hash >> 13)) * 0xc2b2ae35;
    hash = hash ^ (hash >> 16);
    return static_cast<float>(hash % 10000) / 10000.0F;
}

glm::vec3 GameplaySystems::ComputePerpendicular(const glm::vec3& forward)
{
    glm::vec3 up{0.0F, 1.0F, 0.0F};
    glm::vec3 perp = glm::cross(forward, up);
    if (glm::length(perp) < 0.001F)
    {
        perp = glm::vec3{1.0F, 0.0F, 0.0F};
    }
    return glm::normalize(perp);
}

bool GameplaySystems::CanSeeScratchMarks(bool localIsKiller) const
{
    if (m_scratchProfile.allowSurvivorSeeOwn)
    {
        return true;
    }
    return localIsKiller;
}

bool GameplaySystems::CanSeeBloodPools(bool localIsKiller) const
{
    if (m_bloodProfile.allowSurvivorSeeOwn)
    {
        return true;
    }
    return localIsKiller;
}

void GameplaySystems::UpdateScratchMarks(float fixedDt, const glm::vec3& survivorPos, const glm::vec3& survivorForward, bool survivorSprinting)
{
    for (auto& mark : m_scratchMarks)
    {
        if (mark.active)
        {
            mark.age += fixedDt;
            if (mark.age >= mark.lifetime)
            {
                mark.active = false;
            }
        }
    }

    if (!survivorSprinting)
    {
        return;
    }

    const float distFromLast = glm::length(glm::vec2{survivorPos.x - m_lastScratchSpawnPos.x, survivorPos.z - m_lastScratchSpawnPos.z});
    if (distFromLast < m_scratchProfile.minDistanceFromLast)
    {
        return;
    }

    m_scratchSpawnAccumulator += fixedDt;
    if (m_scratchSpawnAccumulator < m_scratchNextInterval)
    {
        return;
    }

    m_scratchSpawnAccumulator -= m_scratchNextInterval;

    const float intervalRand = DeterministicRandom(survivorPos + glm::vec3{0.0F, m_scratchSpawnAccumulator, 0.0F}, 0);
    m_scratchNextInterval = m_scratchProfile.spawnIntervalMin + intervalRand * (m_scratchProfile.spawnIntervalMax - m_scratchProfile.spawnIntervalMin);

    ScratchMark& mark = m_scratchMarks[m_scratchMarkHead];
    mark.active = true;
    mark.age = 0.0F;
    mark.lifetime = m_scratchProfile.lifetime;
    mark.direction = survivorForward;
    mark.yawDeg = glm::degrees(std::atan2(survivorForward.x, survivorForward.z));
    mark.perpOffset = ComputePerpendicular(survivorForward);

    const float jitterRand1 = DeterministicRandom(survivorPos, 1) * 2.0F - 1.0F;
    const float jitterRand2 = DeterministicRandom(survivorPos, 2) * 2.0F - 1.0F;
    const glm::vec3 jitter{jitterRand1 * m_scratchProfile.jitterRadius, 0.0F, jitterRand2 * m_scratchProfile.jitterRadius};

    constexpr float behindOffset = 1.2F;
    mark.position = survivorPos - mark.direction * behindOffset + jitter;

    const float sizeRand = DeterministicRandom(survivorPos, 3);
    mark.size = m_scratchProfile.sizeMin + sizeRand * (m_scratchProfile.sizeMax - m_scratchProfile.sizeMin);

    glm::vec3 rayStart = mark.position;
    rayStart.y += 2.0F;
    const glm::vec3 rayEnd = rayStart + glm::vec3{0.0F, -10.0F, 0.0F};

    const std::optional<engine::physics::RaycastHit> hit = m_physics.RaycastNearest(rayStart, rayEnd);
    if (hit.has_value())
    {
        mark.position.y = hit->position.y + 0.02F;
    }
    else
    {
        mark.position.y = 0.02F;
    }

    m_scratchMarkHead = (m_scratchMarkHead + 1) % kScratchMarkPoolSize;
    m_lastScratchSpawnPos = survivorPos;
}

void GameplaySystems::UpdateBloodPools(float fixedDt, const glm::vec3& survivorPos, bool survivorInjuredOrDowned, bool survivorMoving)
{
    for (auto& pool : m_bloodPools)
    {
        if (pool.active)
        {
            pool.age += fixedDt;
            if (pool.age >= pool.lifetime)
            {
                pool.active = false;
            }
        }
    }

    if (!survivorInjuredOrDowned)
    {
        return;
    }

    if (m_bloodProfile.onlyWhenMoving && !survivorMoving)
    {
        return;
    }

    const float distFromLast = glm::length(glm::vec2{survivorPos.x - m_lastBloodSpawnPos.x, survivorPos.z - m_lastBloodSpawnPos.z});
    if (distFromLast < m_bloodProfile.minDistanceFromLast)
    {
        return;
    }

    m_bloodSpawnAccumulator += fixedDt;
    if (m_bloodSpawnAccumulator < m_bloodProfile.spawnInterval)
    {
        return;
    }

    m_bloodSpawnAccumulator -= m_bloodProfile.spawnInterval;

    BloodPool& pool = m_bloodPools[m_bloodPoolHead];
    pool.active = true;
    pool.age = 0.0F;
    pool.lifetime = m_bloodProfile.lifetime;

    const float jitterRand1 = DeterministicRandom(survivorPos, 10) * 2.0F - 1.0F;
    const float jitterRand2 = DeterministicRandom(survivorPos, 11) * 2.0F - 1.0F;
    pool.position = survivorPos + glm::vec3{jitterRand1 * 0.3F, 0.0F, jitterRand2 * 0.3F};

    const float sizeRand = DeterministicRandom(survivorPos, 12);
    pool.size = m_bloodProfile.sizeMin + sizeRand * (m_bloodProfile.sizeMax - m_bloodProfile.sizeMin);

    glm::vec3 rayStart = pool.position;
    rayStart.y += 2.0F;
    const glm::vec3 rayEnd = rayStart + glm::vec3{0.0F, -10.0F, 0.0F};

    const std::optional<engine::physics::RaycastHit> hit = m_physics.RaycastNearest(rayStart, rayEnd);
    if (hit.has_value())
    {
        pool.position.y = hit->position.y + 0.01F;
    }
    else
    {
        pool.position.y = 0.01F;
    }

    m_bloodPoolHead = (m_bloodPoolHead + 1) % kBloodPoolPoolSize;
    m_lastBloodSpawnPos = survivorPos;
}

void GameplaySystems::RenderScratchMarks(engine::render::Renderer& renderer, bool localIsKiller) const
{
    const bool visible = CanSeeScratchMarks(localIsKiller) || m_scratchDebugEnabled;

    if (!visible)
    {
        return;
    }

    const glm::vec3 baseColor{0.65F, 0.15F, 0.12F};

    for (const ScratchMark& mark : m_scratchMarks)
    {
        if (!mark.active)
        {
            continue;
        }

        const float lifeT = mark.age / mark.lifetime;
        const float alpha = glm::max(0.0F, 1.0F - lifeT);

        constexpr float halfWidth = 0.04F;
        const float streakLength = mark.size * 0.8F;

        const glm::vec3 perp = mark.perpOffset * halfWidth;

        const glm::vec3 p1 = mark.position - mark.direction * streakLength * 0.5F - perp;
        const glm::vec3 p2 = mark.position - mark.direction * streakLength * 0.5F + perp;
        const glm::vec3 p3 = mark.position + mark.direction * streakLength * 0.3F + perp * 0.7F;
        const glm::vec3 p4 = mark.position + mark.direction * streakLength * 0.5F + perp * 0.4F;

        renderer.DrawLine(p1, p2, baseColor * alpha);
        renderer.DrawLine(p2, p3, baseColor * alpha);
        renderer.DrawLine(p3, p4, baseColor * alpha);

        renderer.DrawOrientedBox(
            mark.position,
            glm::vec3{halfWidth * 0.5F, 0.01F, halfWidth * 0.5F},
            glm::vec3{0.0F, mark.yawDeg, 0.0F},
            baseColor * alpha
        );
    }
}

void GameplaySystems::RenderBloodPools(engine::render::Renderer& renderer, bool localIsKiller) const
{
    const bool visible = CanSeeBloodPools(localIsKiller) || m_bloodDebugEnabled;

    if (!visible)
    {
        return;
    }

    for (const BloodPool& pool : m_bloodPools)
    {
        if (!pool.active)
        {
            continue;
        }

        const float lifeT = pool.age / pool.lifetime;
        const float alpha = glm::max(0.0F, 1.0F - lifeT * lifeT);

        const glm::vec3 color{0.55F, 0.08F, 0.08F};

        renderer.DrawBox(
            pool.position,
            glm::vec3{pool.size * 0.5F, 0.01F, pool.size * 0.5F},
            color * alpha
        );
    }
}

void GameplaySystems::InitializeLoadoutCatalog()
{
    if (!m_loadoutCatalog.Initialize("assets"))
    {
        AddRuntimeMessage("Loadout catalog init failed", 2.0F);
        return;
    }

    const std::vector<std::string> survivorIds = m_loadoutCatalog.ListSurvivorIds();
    if (!survivorIds.empty() && !m_loadoutCatalog.FindSurvivor(m_selectedSurvivorCharacterId))
    {
        m_selectedSurvivorCharacterId = survivorIds.front();
    }

    const std::vector<std::string> killerIds = m_loadoutCatalog.ListKillerIds();
    if (!killerIds.empty() && !m_loadoutCatalog.FindKiller(m_selectedKillerCharacterId))
    {
        m_selectedKillerCharacterId = killerIds.front();
    }

    if (m_survivorLoadout.itemId.empty())
    {
        const std::vector<std::string> itemIds = m_loadoutCatalog.ListItemIds();
        if (!itemIds.empty())
        {
            m_survivorLoadout.itemId = itemIds.front();
        }
    }

    if (m_killerLoadout.powerId.empty())
    {
        if (const auto* killerDef = m_loadoutCatalog.FindKiller(m_selectedKillerCharacterId))
        {
            m_killerLoadout.powerId = killerDef->powerId;
        }
        if (m_killerLoadout.powerId.empty())
        {
            const std::vector<std::string> powerIds = m_loadoutCatalog.ListPowerIds();
            if (!powerIds.empty())
            {
                m_killerLoadout.powerId = powerIds.front();
            }
        }
    }

    RefreshLoadoutModifiers();
    ResetItemAndPowerRuntimeState();
}

void GameplaySystems::RefreshLoadoutModifiers()
{
    m_survivorItemModifiers.Build(
        loadout::TargetKind::Item,
        m_survivorLoadout.itemId,
        {m_survivorLoadout.addonAId, m_survivorLoadout.addonBId},
        m_loadoutCatalog.Addons()
    );
    m_killerPowerModifiers.Build(
        loadout::TargetKind::Power,
        m_killerLoadout.powerId,
        {m_killerLoadout.addonAId, m_killerLoadout.addonBId},
        m_loadoutCatalog.Addons()
    );
}

void GameplaySystems::ResetItemAndPowerRuntimeState()
{
    m_survivorItemState = SurvivorItemRuntimeState{};
    m_killerPowerState = KillerPowerRuntimeState{};
    m_mapRevealGenerators.clear();
    m_trapIndicatorText.clear();
    m_trapIndicatorTimer = 0.0F;
    m_trapIndicatorDanger = true;
    m_trapPreviewActive = false;
    m_trapPreviewValid = true;

    const loadout::ItemDefinition* itemDef = m_loadoutCatalog.FindItem(m_survivorLoadout.itemId);
    if (itemDef != nullptr)
    {
        auto resolveRuntimeMaxCharges = [&](const loadout::ItemDefinition& def) {
            float baseMax = def.maxCharges;
            if (def.id == "toolbox")
            {
                baseMax = m_tuning.toolboxCharges;
            }
            else if (def.id == "flashlight")
            {
                baseMax = m_tuning.flashlightMaxUseSeconds;
            }
            else if (def.id == "map")
            {
                baseMax = static_cast<float>(m_tuning.mapUses);
            }
            return std::max(0.0F, m_survivorItemModifiers.ApplyStat("max_charges", baseMax));
        };
        const float maxCharges = resolveRuntimeMaxCharges(*itemDef);
        m_survivorItemState.charges = maxCharges;
        if (itemDef->id == "flashlight")
        {
            m_survivorItemState.flashlightBatterySeconds = maxCharges > 0.0F ? maxCharges : m_tuning.flashlightMaxUseSeconds;
        }
        if (itemDef->id == "map")
        {
            m_survivorItemState.mapUsesRemaining = std::max(0, static_cast<int>(std::round(std::min(maxCharges, static_cast<float>(m_tuning.mapUses)))));
        }
    }

    const loadout::PowerDefinition* powerDef = m_loadoutCatalog.FindPower(m_killerLoadout.powerId);
    if (powerDef != nullptr && powerDef->id == "bear_trap")
    {
        m_killerPowerState.trapperMaxCarryTraps = std::max(1, m_tuning.trapperMaxCarryTraps);
        m_killerPowerState.trapperCarriedTraps = std::max(0, std::min(m_tuning.trapperStartCarryTraps, m_killerPowerState.trapperMaxCarryTraps));
    }
}

bool GameplaySystems::SetSurvivorItemLoadout(const std::string& itemId, const std::string& addonAId, const std::string& addonBId)
{
    if (!itemId.empty() && m_loadoutCatalog.FindItem(itemId) == nullptr)
    {
        return false;
    }

    auto validateAddon = [&](const std::string& addonId) {
        if (addonId.empty())
        {
            return true;
        }
        const loadout::AddonDefinition* addon = m_loadoutCatalog.FindAddon(addonId);
        return addon != nullptr && addon->AppliesTo(loadout::TargetKind::Item, itemId);
    };
    if (!validateAddon(addonAId) || !validateAddon(addonBId))
    {
        return false;
    }

    m_survivorLoadout.itemId = itemId;
    m_survivorLoadout.addonAId = addonAId;
    m_survivorLoadout.addonBId = addonBId;
    RefreshLoadoutModifiers();
    ResetItemAndPowerRuntimeState();
    ItemPowerLog(
        "Set survivor loadout item=" + (itemId.empty() ? std::string{"none"} : itemId) +
        " addonA=" + (addonAId.empty() ? std::string{"none"} : addonAId) +
        " addonB=" + (addonBId.empty() ? std::string{"none"} : addonBId)
    );
    return true;
}

bool GameplaySystems::SetKillerPowerLoadout(const std::string& powerId, const std::string& addonAId, const std::string& addonBId)
{
    if (!powerId.empty() && m_loadoutCatalog.FindPower(powerId) == nullptr)
    {
        return false;
    }

    auto validateAddon = [&](const std::string& addonId) {
        if (addonId.empty())
        {
            return true;
        }
        const loadout::AddonDefinition* addon = m_loadoutCatalog.FindAddon(addonId);
        return addon != nullptr && addon->AppliesTo(loadout::TargetKind::Power, powerId);
    };
    if (!validateAddon(addonAId) || !validateAddon(addonBId))
    {
        return false;
    }

    m_killerLoadout.powerId = powerId;
    m_killerLoadout.addonAId = addonAId;
    m_killerLoadout.addonBId = addonBId;
    RefreshLoadoutModifiers();
    ResetItemAndPowerRuntimeState();
    ItemPowerLog(
        "Set killer loadout power=" + (powerId.empty() ? std::string{"none"} : powerId) +
        " addonA=" + (addonAId.empty() ? std::string{"none"} : addonAId) +
        " addonB=" + (addonBId.empty() ? std::string{"none"} : addonBId)
    );
    return true;
}

std::string GameplaySystems::ItemDump() const
{
    std::ostringstream oss;
    oss << "SurvivorItem\n";
    oss << "  character=" << m_selectedSurvivorCharacterId << "\n";
    oss << "  item=" << (m_survivorLoadout.itemId.empty() ? "none" : m_survivorLoadout.itemId) << "\n";
    oss << "  addon_a=" << (m_survivorLoadout.addonAId.empty() ? "none" : m_survivorLoadout.addonAId) << "\n";
    oss << "  addon_b=" << (m_survivorLoadout.addonBId.empty() ? "none" : m_survivorLoadout.addonBId) << "\n";
    oss << "  charges=" << m_survivorItemState.charges << "\n";
    oss << "  active=" << (m_survivorItemState.active ? "true" : "false") << "\n";
    const auto activeAddons = m_survivorItemModifiers.ActiveAddonIds();
    oss << "  active_modifiers=";
    if (activeAddons.empty())
    {
        oss << "none";
    }
    else
    {
        for (std::size_t i = 0; i < activeAddons.size(); ++i)
        {
            if (i > 0)
            {
                oss << ",";
            }
            oss << activeAddons[i];
        }
    }
    oss << "\n";
    return oss.str();
}

std::string GameplaySystems::PowerDump() const
{
    std::ostringstream oss;
    oss << "KillerPower\n";
    oss << "  character=" << m_selectedKillerCharacterId << "\n";
    oss << "  power=" << (m_killerLoadout.powerId.empty() ? "none" : m_killerLoadout.powerId) << "\n";
    oss << "  addon_a=" << (m_killerLoadout.addonAId.empty() ? "none" : m_killerLoadout.addonAId) << "\n";
    oss << "  addon_b=" << (m_killerLoadout.addonBId.empty() ? "none" : m_killerLoadout.addonBId) << "\n";
    oss << "  active_traps=" << m_world.BearTraps().size() << "\n";
    oss << "  carried_traps=" << m_killerPowerState.trapperCarriedTraps << "\n";
    oss << "  wraith_cloaked=" << (m_killerPowerState.wraithCloaked ? "true" : "false") << "\n";
    oss << "  wraith_transition=" << m_killerPowerState.wraithTransitionTimer << "\n";
    oss << "  wraith_post_uncloak=" << m_killerPowerState.wraithPostUncloakTimer << "\n";
    const auto activeAddons = m_killerPowerModifiers.ActiveAddonIds();
    oss << "  active_modifiers=";
    if (activeAddons.empty())
    {
        oss << "none";
    }
    else
    {
        for (std::size_t i = 0; i < activeAddons.size(); ++i)
        {
            if (i > 0)
            {
                oss << ",";
            }
            oss << activeAddons[i];
        }
    }
    oss << "\n";
    return oss.str();
}

bool GameplaySystems::SetSelectedSurvivorCharacter(const std::string& characterId)
{
    if (m_loadoutCatalog.FindSurvivor(characterId) == nullptr)
    {
        return false;
    }
    m_selectedSurvivorCharacterId = characterId;
    return true;
}

bool GameplaySystems::SetSelectedKillerCharacter(const std::string& characterId)
{
    const loadout::KillerCharacterDefinition* def = m_loadoutCatalog.FindKiller(characterId);
    if (def == nullptr)
    {
        return false;
    }
    m_selectedKillerCharacterId = characterId;
    if (!def->powerId.empty())
    {
        m_killerLoadout.powerId = def->powerId;
        RefreshLoadoutModifiers();
    }
    return true;
}

std::vector<std::string> GameplaySystems::ListSurvivorCharacters() const
{
    return m_loadoutCatalog.ListSurvivorIds();
}

std::vector<std::string> GameplaySystems::ListKillerCharacters() const
{
    return m_loadoutCatalog.ListKillerIds();
}

std::vector<std::string> GameplaySystems::ListItemIds() const
{
    return m_loadoutCatalog.ListItemIds();
}

std::vector<std::string> GameplaySystems::ListPowerIds() const
{
    return m_loadoutCatalog.ListPowerIds();
}

bool GameplaySystems::SpawnGroundItemDebug(const std::string& itemId, float charges)
{
    if (itemId.empty())
    {
        return false;
    }
    const loadout::ItemDefinition* itemDef = m_loadoutCatalog.FindItem(itemId);
    if (itemDef == nullptr)
    {
        return false;
    }

    engine::scene::Entity anchor = ControlledEntity();
    if (anchor == 0)
    {
        anchor = (m_survivor != 0) ? m_survivor : m_killer;
    }
    const auto anchorTransformIt = m_world.Transforms().find(anchor);
    if (anchorTransformIt == m_world.Transforms().end())
    {
        return false;
    }

    glm::vec3 forward = anchorTransformIt->second.forward;
    forward.y = 0.0F;
    if (glm::length(forward) <= 1.0e-4F)
    {
        forward = glm::vec3{0.0F, 0.0F, -1.0F};
    }
    else
    {
        forward = glm::normalize(forward);
    }

    glm::vec3 spawnPos = anchorTransformIt->second.position + forward * 1.55F;
    const glm::vec3 rayStart = spawnPos + glm::vec3{0.0F, 2.0F, 0.0F};
    const glm::vec3 rayEnd = rayStart + glm::vec3{0.0F, -8.0F, 0.0F};
    if (const std::optional<engine::physics::RaycastHit> hit = m_physics.RaycastNearest(rayStart, rayEnd))
    {
        spawnPos.y = hit->position.y + 0.05F;
    }
    else
    {
        spawnPos.y = anchorTransformIt->second.position.y;
    }

    float baseCharges = itemDef->maxCharges;
    if (itemDef->id == "toolbox")
    {
        baseCharges = m_tuning.toolboxCharges;
    }
    else if (itemDef->id == "flashlight")
    {
        baseCharges = m_tuning.flashlightMaxUseSeconds;
    }
    else if (itemDef->id == "map")
    {
        baseCharges = static_cast<float>(m_tuning.mapUses);
    }
    const float resolvedCharges = charges >= 0.0F ? charges : baseCharges;

    const engine::scene::Entity itemEntity = SpawnGroundItemEntity(itemId, spawnPos, std::max(0.0F, resolvedCharges));
    if (itemEntity == 0)
    {
        return false;
    }
    AddRuntimeMessage("Spawned item: " + itemId, 1.0F);
    ItemPowerLog(
        "Debug spawned ground item id=" + itemId +
        " charges=" + std::to_string(resolvedCharges) +
        " entity=" + std::to_string(itemEntity)
    );
    return true;
}

engine::scene::Entity GameplaySystems::SpawnGroundItemEntity(
    const std::string& itemId,
    const glm::vec3& position,
    float charges,
    const std::string& addonAId,
    const std::string& addonBId,
    bool respawnTag
)
{
    if (itemId.empty())
    {
        return 0;
    }

    const engine::scene::Entity entity = m_world.CreateEntity();
    m_world.Transforms()[entity] = engine::scene::Transform{
        position,
        glm::vec3{0.0F},
        glm::vec3{1.0F},
        glm::vec3{0.0F, 0.0F, 1.0F},
    };

    engine::scene::GroundItemComponent groundItem;
    groundItem.itemId = itemId;
    groundItem.charges = charges;
    groundItem.addonAId = addonAId;
    groundItem.addonBId = addonBId;
    groundItem.pickupEnabled = true;
    groundItem.respawnTag = respawnTag;
    m_world.GroundItems()[entity] = groundItem;
    m_world.Names()[entity] = engine::scene::NameComponent{"ground_item_" + itemId};
    return entity;
}

engine::scene::Entity GameplaySystems::FindNearestGroundItem(const glm::vec3& fromPosition, float radiusMeters) const
{
    engine::scene::Entity bestEntity = 0;
    float bestDistance = std::max(0.01F, radiusMeters);
    for (const auto& [entity, groundItem] : m_world.GroundItems())
    {
        if (!groundItem.pickupEnabled)
        {
            continue;
        }
        const auto transformIt = m_world.Transforms().find(entity);
        if (transformIt == m_world.Transforms().end())
        {
            continue;
        }
        if (std::abs(transformIt->second.position.y - fromPosition.y) > 2.0F)
        {
            continue;
        }
        const float distance = DistanceXZ(transformIt->second.position, fromPosition);
        if (distance <= bestDistance)
        {
            bestDistance = distance;
            bestEntity = entity;
        }
    }
    return bestEntity;
}

void GameplaySystems::ApplySurvivorItemActionLock(float durationSeconds)
{
    if (m_survivor == 0 || durationSeconds <= 0.0F)
    {
        return;
    }
    m_survivorItemState.actionLockTimer = std::max(m_survivorItemState.actionLockTimer, durationSeconds);
    ItemPowerLog("Survivor action lock " + std::to_string(durationSeconds) + "s");
}

bool GameplaySystems::TryDropSurvivorItemToGround()
{
    if (m_survivor == 0 || m_survivorLoadout.itemId.empty())
    {
        return false;
    }

    const auto survivorTransformIt = m_world.Transforms().find(m_survivor);
    if (survivorTransformIt == m_world.Transforms().end())
    {
        return false;
    }

    glm::vec3 forward = survivorTransformIt->second.forward;
    forward.y = 0.0F;
    if (glm::length(forward) <= 1.0e-4F)
    {
        forward = glm::vec3{0.0F, 0.0F, -1.0F};
    }
    else
    {
        forward = glm::normalize(forward);
    }

    glm::vec3 dropPos = survivorTransformIt->second.position + forward * 0.9F;
    const glm::vec3 rayStart = dropPos + glm::vec3{0.0F, 2.0F, 0.0F};
    const glm::vec3 rayEnd = rayStart + glm::vec3{0.0F, -8.0F, 0.0F};
    if (const std::optional<engine::physics::RaycastHit> hit = m_physics.RaycastNearest(rayStart, rayEnd))
    {
        dropPos.y = hit->position.y + 0.05F;
    }
    else
    {
        dropPos.y = survivorTransformIt->second.position.y;
    }

    const float droppedCharges = m_survivorItemState.charges;
    SpawnGroundItemEntity(
        m_survivorLoadout.itemId,
        dropPos,
        droppedCharges,
        m_survivorLoadout.addonAId,
        m_survivorLoadout.addonBId,
        false
    );

    const std::string droppedItem = m_survivorLoadout.itemId;
    m_survivorLoadout.itemId.clear();
    m_survivorLoadout.addonAId.clear();
    m_survivorLoadout.addonBId.clear();
    m_survivorItemState = SurvivorItemRuntimeState{};
    ApplySurvivorItemActionLock(0.5F);
    RefreshLoadoutModifiers();
    AddRuntimeMessage("Dropped item: " + droppedItem, 1.1F);
    ItemPowerLog("Survivor dropped item id=" + droppedItem + " charges=" + std::to_string(droppedCharges));
    return true;
}

bool GameplaySystems::TryPickupSurvivorGroundItem()
{
    if (m_survivor == 0 || !m_survivorLoadout.itemId.empty())
    {
        if (m_survivor != 0 && !m_survivorLoadout.itemId.empty())
        {
            AddRuntimeMessage("Drop current item first (R)", 0.9F);
        }
        return false;
    }

    const auto survivorTransformIt = m_world.Transforms().find(m_survivor);
    if (survivorTransformIt == m_world.Transforms().end())
    {
        return false;
    }

    const engine::scene::Entity itemEntity = FindNearestGroundItem(survivorTransformIt->second.position, 2.2F);
    if (itemEntity == 0)
    {
        return false;
    }

    const auto itemIt = m_world.GroundItems().find(itemEntity);
    if (itemIt == m_world.GroundItems().end())
    {
        return false;
    }

    m_survivorLoadout.itemId = itemIt->second.itemId;
    m_survivorLoadout.addonAId = itemIt->second.addonAId;
    m_survivorLoadout.addonBId = itemIt->second.addonBId;
    RefreshLoadoutModifiers();
    m_survivorItemState = SurvivorItemRuntimeState{};
    if (const loadout::ItemDefinition* itemDef = m_loadoutCatalog.FindItem(m_survivorLoadout.itemId))
    {
        float baseMax = itemDef->maxCharges;
        if (itemDef->id == "toolbox")
        {
            baseMax = m_tuning.toolboxCharges;
        }
        else if (itemDef->id == "flashlight")
        {
            baseMax = m_tuning.flashlightMaxUseSeconds;
        }
        else if (itemDef->id == "map")
        {
            baseMax = static_cast<float>(m_tuning.mapUses);
        }
        const float maxCharges = std::max(0.0F, m_survivorItemModifiers.ApplyStat("max_charges", baseMax));
        const float requestedCharges = itemIt->second.charges > 0.0F ? itemIt->second.charges : maxCharges;
        m_survivorItemState.charges = glm::clamp(requestedCharges, 0.0F, maxCharges);
        if (itemDef->id == "flashlight")
        {
            m_survivorItemState.flashlightBatterySeconds = m_survivorItemState.charges;
        }
        if (itemDef->id == "map")
        {
            m_survivorItemState.mapUsesRemaining = std::max(
                0,
                static_cast<int>(std::round(std::min(m_survivorItemState.charges, static_cast<float>(m_tuning.mapUses))))
            );
        }
    }

    const std::string picked = m_survivorLoadout.itemId;
    DestroyEntity(itemEntity);
    ApplySurvivorItemActionLock(0.5F);
    AddRuntimeMessage("Picked up item: " + picked, 1.1F);
    ItemPowerLog("Survivor picked item id=" + picked + " charges=" + std::to_string(m_survivorItemState.charges));
    return true;
}

bool GameplaySystems::TrySwapSurvivorGroundItem()
{
    if (m_survivor == 0 || m_survivorLoadout.itemId.empty())
    {
        return false;
    }

    const auto survivorTransformIt = m_world.Transforms().find(m_survivor);
    if (survivorTransformIt == m_world.Transforms().end())
    {
        return false;
    }

    const engine::scene::Entity itemEntity = FindNearestGroundItem(survivorTransformIt->second.position, 2.2F);
    if (itemEntity == 0)
    {
        return false;
    }

    auto itemIt = m_world.GroundItems().find(itemEntity);
    if (itemIt == m_world.GroundItems().end())
    {
        return false;
    }
    if (itemIt->second.itemId.empty())
    {
        return false;
    }

    const std::string equippedItem = m_survivorLoadout.itemId;
    const std::string equippedAddonA = m_survivorLoadout.addonAId;
    const std::string equippedAddonB = m_survivorLoadout.addonBId;
    const float equippedCharges = m_survivorItemState.charges;

    const std::string groundItem = itemIt->second.itemId;
    const std::string groundAddonA = itemIt->second.addonAId;
    const std::string groundAddonB = itemIt->second.addonBId;
    const float groundCharges = itemIt->second.charges;

    m_survivorLoadout.itemId = groundItem;
    m_survivorLoadout.addonAId = groundAddonA;
    m_survivorLoadout.addonBId = groundAddonB;
    RefreshLoadoutModifiers();
    m_survivorItemState = SurvivorItemRuntimeState{};
    if (const loadout::ItemDefinition* itemDef = m_loadoutCatalog.FindItem(m_survivorLoadout.itemId))
    {
        float baseMax = itemDef->maxCharges;
        if (itemDef->id == "toolbox")
        {
            baseMax = m_tuning.toolboxCharges;
        }
        else if (itemDef->id == "flashlight")
        {
            baseMax = m_tuning.flashlightMaxUseSeconds;
        }
        else if (itemDef->id == "map")
        {
            baseMax = static_cast<float>(m_tuning.mapUses);
        }
        const float maxCharges = std::max(0.0F, m_survivorItemModifiers.ApplyStat("max_charges", baseMax));
        const float resolvedCharges = groundCharges > 0.0F ? groundCharges : maxCharges;
        m_survivorItemState.charges = glm::clamp(resolvedCharges, 0.0F, maxCharges);
        if (itemDef->id == "flashlight")
        {
            m_survivorItemState.flashlightBatterySeconds = m_survivorItemState.charges;
        }
        if (itemDef->id == "map")
        {
            m_survivorItemState.mapUsesRemaining = std::max(
                0,
                static_cast<int>(std::round(std::min(m_survivorItemState.charges, static_cast<float>(m_tuning.mapUses))))
            );
        }
    }

    itemIt->second.itemId = equippedItem;
    itemIt->second.addonAId = equippedAddonA;
    itemIt->second.addonBId = equippedAddonB;
    itemIt->second.charges = std::max(0.0F, equippedCharges);
    itemIt->second.pickupEnabled = true;

    ApplySurvivorItemActionLock(0.5F);
    m_interactBufferRemaining[RoleToIndex(engine::scene::Role::Survivor)] = 0.0F;
    AddRuntimeMessage("Swapped item: " + equippedItem + " <-> " + groundItem, 1.2F);
    ItemPowerLog(
        "Survivor swapped item equipped=" + equippedItem +
        " ground=" + groundItem +
        " newCharges=" + std::to_string(m_survivorItemState.charges)
    );
    return true;
}

bool GameplaySystems::RespawnItemsNearPlayer(float radiusMeters)
{
    const engine::scene::Entity anchorEntity = ControlledEntity() != 0 ? ControlledEntity() : m_survivor;
    const auto anchorTransformIt = m_world.Transforms().find(anchorEntity);
    if (anchorTransformIt == m_world.Transforms().end())
    {
        return false;
    }

    const glm::vec3 center = anchorTransformIt->second.position;
    const std::array<std::string, 4> itemIds{"medkit", "toolbox", "flashlight", "map"};
    int spawned = 0;
    for (std::size_t i = 0; i < itemIds.size(); ++i)
    {
        const loadout::ItemDefinition* itemDef = m_loadoutCatalog.FindItem(itemIds[i]);
        if (itemDef == nullptr)
        {
            continue;
        }
        float baseCharges = itemDef->maxCharges;
        if (itemDef->id == "toolbox")
        {
            baseCharges = m_tuning.toolboxCharges;
        }
        else if (itemDef->id == "flashlight")
        {
            baseCharges = m_tuning.flashlightMaxUseSeconds;
        }
        else if (itemDef->id == "map")
        {
            baseCharges = static_cast<float>(m_tuning.mapUses);
        }
        const float angle = (2.0F * kPi) * (static_cast<float>(i) / static_cast<float>(itemIds.size()));
        glm::vec3 pos = center + glm::vec3{std::cos(angle) * radiusMeters, 0.0F, std::sin(angle) * radiusMeters};
        const glm::vec3 rayStart = pos + glm::vec3{0.0F, 2.0F, 0.0F};
        const glm::vec3 rayEnd = rayStart + glm::vec3{0.0F, -8.0F, 0.0F};
        if (const std::optional<engine::physics::RaycastHit> hit = m_physics.RaycastNearest(rayStart, rayEnd))
        {
            pos.y = hit->position.y + 0.05F;
        }
        else
        {
            pos.y = center.y;
        }

        SpawnGroundItemEntity(itemIds[i], pos, std::max(0.0F, baseCharges), std::string{}, std::string{}, true);
        ++spawned;
    }

    if (spawned > 0)
    {
        AddRuntimeMessage("Respawned base items near player", 1.5F);
        ItemPowerLog("Respawned base items near player count=" + std::to_string(spawned));
    }
    return spawned > 0;
}

void GameplaySystems::SpawnInitialTrapperGroundTraps()
{
    if (m_killerLoadout.powerId != "bear_trap" || m_killer == 0)
    {
        return;
    }

    m_killerPowerState.trapperMaxCarryTraps = std::max(1, m_tuning.trapperMaxCarryTraps);
    m_killerPowerState.trapperCarriedTraps = std::max(
        0,
        std::min(m_tuning.trapperStartCarryTraps, m_killerPowerState.trapperMaxCarryTraps)
    );

    if (m_tuning.trapperGroundSpawnTraps <= 0)
    {
        return;
    }

    const auto killerTransformIt = m_world.Transforms().find(m_killer);
    const glm::vec3 killerPos = killerTransformIt != m_world.Transforms().end() ? killerTransformIt->second.position : glm::vec3{0.0F};
    const glm::vec3 killerForward = killerTransformIt != m_world.Transforms().end() ? killerTransformIt->second.forward : glm::vec3{0.0F, 0.0F, -1.0F};

    std::vector<glm::vec3> genericSpawnPoints;
    genericSpawnPoints.reserve(m_spawnPoints.size());
    for (const SpawnPointInfo& spawn : m_spawnPoints)
    {
        if (spawn.type == SpawnPointType::Generic)
        {
            genericSpawnPoints.push_back(spawn.position);
        }
    }
    if (genericSpawnPoints.empty())
    {
        genericSpawnPoints.push_back(killerPos);
    }

    for (int i = 0; i < m_tuning.trapperGroundSpawnTraps; ++i)
    {
        const glm::vec3 base = genericSpawnPoints[static_cast<std::size_t>(i % static_cast<int>(genericSpawnPoints.size()))];
        const float angle = static_cast<float>(i) * 0.79F;
        const glm::vec3 offset{std::cos(angle) * 1.25F, 0.0F, std::sin(angle) * 1.25F};
        const engine::scene::Entity trapEntity = SpawnBearTrap(base + offset, killerForward, false);
        auto trapIt = m_world.BearTraps().find(trapEntity);
        if (trapIt != m_world.BearTraps().end())
        {
            trapIt->second.state = engine::scene::TrapState::Disarmed;
            trapIt->second.trappedEntity = 0;
            trapIt->second.escapeAttempts = 0;
        }
    }
    ItemPowerLog(
        "Trapper initial traps spawned carry=" + std::to_string(m_killerPowerState.trapperCarriedTraps) +
        " ground=" + std::to_string(m_tuning.trapperGroundSpawnTraps)
    );
}

bool GameplaySystems::TryFindNearestTrap(
    const glm::vec3& fromPosition,
    float radiusMeters,
    bool requireDisarmed,
    engine::scene::Entity* outTrapEntity
) const
{
    if (outTrapEntity != nullptr)
    {
        *outTrapEntity = 0;
    }

    float best = radiusMeters;
    engine::scene::Entity bestEntity = 0;
    for (const auto& [entity, trap] : m_world.BearTraps())
    {
        if (requireDisarmed && trap.state != engine::scene::TrapState::Disarmed)
        {
            continue;
        }
        const auto transformIt = m_world.Transforms().find(entity);
        if (transformIt == m_world.Transforms().end())
        {
            continue;
        }
        const float distance = DistanceXZ(fromPosition, transformIt->second.position);
        if (distance <= best)
        {
            best = distance;
            bestEntity = entity;
        }
    }

    if (bestEntity == 0)
    {
        return false;
    }
    if (outTrapEntity != nullptr)
    {
        *outTrapEntity = bestEntity;
    }
    return true;
}

bool GameplaySystems::ComputeTrapPlacementPreview(glm::vec3* outPosition, glm::vec3* outHalfExtents, bool* outValid) const
{
    if (outPosition != nullptr)
    {
        *outPosition = glm::vec3{0.0F};
    }
    if (outHalfExtents != nullptr)
    {
        *outHalfExtents = glm::vec3{0.36F, 0.08F, 0.36F};
    }
    if (outValid != nullptr)
    {
        *outValid = false;
    }

    if (m_killer == 0 || m_killerLoadout.powerId != "bear_trap")
    {
        return false;
    }

    const auto killerTransformIt = m_world.Transforms().find(m_killer);
    if (killerTransformIt == m_world.Transforms().end())
    {
        return false;
    }

    const loadout::PowerDefinition* powerDef = m_loadoutCatalog.FindPower(m_killerLoadout.powerId);
    const auto readParam = [&](const std::string& key, float fallback) {
        if (powerDef == nullptr)
        {
            return fallback;
        }
        const auto it = powerDef->params.find(key);
        return it != powerDef->params.end() ? it->second : fallback;
    };

    glm::vec3 halfExtents{
        std::max(0.12F, m_killerPowerModifiers.ApplyStat("trap_half_x", readParam("trap_half_x", 0.36F))),
        std::max(0.03F, m_killerPowerModifiers.ApplyStat("trap_half_y", readParam("trap_half_y", 0.08F))),
        std::max(0.12F, m_killerPowerModifiers.ApplyStat("trap_half_z", readParam("trap_half_z", 0.36F))),
    };

    glm::vec3 forward = killerTransformIt->second.forward;
    if (glm::length(forward) > 1.0e-5F)
    {
        forward = glm::normalize(glm::vec3{forward.x, 0.0F, forward.z});
    }
    else
    {
        forward = glm::vec3{0.0F, 0.0F, -1.0F};
    }

    glm::vec3 previewPos = killerTransformIt->second.position + forward * 1.55F;
    const glm::vec3 rayStart = previewPos + glm::vec3{0.0F, 2.2F, 0.0F};
    const glm::vec3 rayEnd = rayStart + glm::vec3{0.0F, -8.0F, 0.0F};
    if (const std::optional<engine::physics::RaycastHit> hit = m_physics.RaycastNearest(rayStart, rayEnd))
    {
        previewPos.y = hit->position.y + 0.05F;
    }
    else
    {
        previewPos.y = 0.05F;
    }

    bool valid = true;
    for (const auto& [entity, trap] : m_world.BearTraps())
    {
        const auto trapTransformIt = m_world.Transforms().find(entity);
        if (trapTransformIt == m_world.Transforms().end())
        {
            continue;
        }
        const glm::vec2 deltaXZ{
            previewPos.x - trapTransformIt->second.position.x,
            previewPos.z - trapTransformIt->second.position.z
        };
        const float minDist = std::max(halfExtents.x, halfExtents.z) + std::max(trap.halfExtents.x, trap.halfExtents.z) + 0.05F;
        if (glm::length(deltaXZ) <= minDist)
        {
            valid = false;
            break;
        }
    }

    if (outPosition != nullptr)
    {
        *outPosition = previewPos;
    }
    if (outHalfExtents != nullptr)
    {
        *outHalfExtents = halfExtents;
    }
    if (outValid != nullptr)
    {
        *outValid = valid;
    }
    return true;
}

void GameplaySystems::UpdateSurvivorItemSystem(const RoleCommand& survivorCommand, float fixedDt)
{
    if (fixedDt <= 0.0F)
    {
        return;
    }

    if (m_survivorItemState.cooldown > 0.0F)
    {
        m_survivorItemState.cooldown = std::max(0.0F, m_survivorItemState.cooldown - fixedDt);
    }
    if (m_survivorItemState.mapRevealTtl > 0.0F)
    {
        m_survivorItemState.mapRevealTtl = std::max(0.0F, m_survivorItemState.mapRevealTtl - fixedDt);
    }
    if (m_survivorItemState.actionLockTimer > 0.0F)
    {
        m_survivorItemState.actionLockTimer = std::max(0.0F, m_survivorItemState.actionLockTimer - fixedDt);
    }
    if (m_survivorItemState.flashlightSuccessFlashTimer > 0.0F)
    {
        m_survivorItemState.flashlightSuccessFlashTimer = std::max(0.0F, m_survivorItemState.flashlightSuccessFlashTimer - fixedDt);
    }

    std::vector<engine::scene::Entity> revealExpired;
    revealExpired.reserve(m_mapRevealGenerators.size());
    for (auto& [generatorEntity, ttl] : m_mapRevealGenerators)
    {
        ttl = std::max(0.0F, ttl - fixedDt);
        if (ttl <= 0.0F)
        {
            revealExpired.push_back(generatorEntity);
        }
    }
    for (engine::scene::Entity generatorEntity : revealExpired)
    {
        m_mapRevealGenerators.erase(generatorEntity);
    }

    if (m_survivorItemState.actionLockTimer <= 0.0F &&
        survivorCommand.dropItemPressed &&
        (m_survivorState == SurvivorHealthState::Healthy || m_survivorState == SurvivorHealthState::Injured || m_survivorState == SurvivorHealthState::Downed))
    {
        (void)TryDropSurvivorItemToGround();
    }
    if (m_survivorItemState.actionLockTimer <= 0.0F &&
        survivorCommand.pickupItemPressed &&
        (m_survivorState == SurvivorHealthState::Healthy || m_survivorState == SurvivorHealthState::Injured || m_survivorState == SurvivorHealthState::Downed))
    {
        (void)TryPickupSurvivorGroundItem();
    }
    if (m_survivorItemState.actionLockTimer <= 0.0F &&
        survivorCommand.interactPressed &&
        !m_survivorLoadout.itemId.empty() &&
        (m_survivorState == SurvivorHealthState::Healthy || m_survivorState == SurvivorHealthState::Injured || m_survivorState == SurvivorHealthState::Downed))
    {
        (void)TrySwapSurvivorGroundItem();
    }

    const loadout::ItemDefinition* itemDef = m_loadoutCatalog.FindItem(m_survivorLoadout.itemId);
    if (itemDef == nullptr)
    {
        m_survivorItemState.active = false;
        return;
    }

    float baseMaxCharges = itemDef->maxCharges;
    if (itemDef->id == "toolbox")
    {
        baseMaxCharges = m_tuning.toolboxCharges;
    }
    else if (itemDef->id == "flashlight")
    {
        baseMaxCharges = m_tuning.flashlightMaxUseSeconds;
    }
    else if (itemDef->id == "map")
    {
        baseMaxCharges = static_cast<float>(m_tuning.mapUses);
    }
    const float maxCharges = std::max(0.0F, m_survivorItemModifiers.ApplyStat("max_charges", baseMaxCharges));
    m_survivorItemState.charges = glm::clamp(m_survivorItemState.charges, 0.0F, maxCharges);

    const bool useHeld = survivorCommand.useAltHeld;
    const auto readParam = [&](const std::string& key, float fallback) {
        const auto it = itemDef->params.find(key);
        return it != itemDef->params.end() ? it->second : fallback;
    };

    if (itemDef->id == "medkit")
    {
        m_survivorItemState.active = false;
        if (m_survivorState != SurvivorHealthState::Injured || !useHeld || m_survivorItemState.charges <= 0.0F)
        {
            return;
        }

        const float baseHealRate = 1.0F / std::max(1.0F, m_tuning.healDurationSeconds);
        const float healMultiplier = std::max(0.1F, m_survivorItemModifiers.ApplyStat(
            "heal_speed_multiplier",
            readParam("heal_speed_multiplier", m_tuning.medkitHealSpeedMultiplier)
        ));
        const float healRate = std::max(0.0F, m_survivorItemModifiers.ApplyStat("heal_per_second", baseHealRate * healMultiplier));
        const float fullHealCharges = std::max(1.0F, m_survivorItemModifiers.ApplyStat(
            "full_heal_charges",
            readParam("full_heal_charges", m_tuning.medkitFullHealCharges)
        ));
        const float chargeRate = std::max(0.05F, m_survivorItemModifiers.ApplyStat("charge_per_second", healRate * fullHealCharges));
        const float consumed = std::min(m_survivorItemState.charges, chargeRate * fixedDt);
        m_survivorItemState.active = true;
        m_survivorItemState.charges = std::max(0.0F, m_survivorItemState.charges - consumed);
        m_selfHealProgress = glm::clamp(m_selfHealProgress + consumed / fullHealCharges, 0.0F, 1.0F);
        if (m_selfHealProgress >= 1.0F)
        {
            m_selfHealProgress = 0.0F;
            SetSurvivorState(SurvivorHealthState::Healthy, "Medkit heal");
            AddRuntimeMessage("Medkit heal completed", 1.1F);
            ItemPowerLog("Medkit heal completed");
        }
        return;
    }

    if (itemDef->id == "toolbox")
    {
        m_survivorItemState.active = false;
        if (!useHeld || m_survivorItemState.charges <= 0.0F)
        {
            return;
        }

        if (m_activeRepairGenerator == 0 && m_survivor != 0)
        {
            engine::scene::Entity bestGenerator = 0;
            float bestDistance = std::numeric_limits<float>::max();
            const auto survivorTransformIt = m_world.Transforms().find(m_survivor);
            if (survivorTransformIt != m_world.Transforms().end())
            {
                for (const auto& [generatorEntity, _] : m_world.Generators())
                {
                    const auto genTransformIt = m_world.Transforms().find(generatorEntity);
                    if (genTransformIt == m_world.Transforms().end())
                    {
                        continue;
                    }
                    const float castDistance = DistanceXZ(survivorTransformIt->second.position, genTransformIt->second.position);
                    InteractionCandidate candidate = BuildGeneratorRepairCandidate(
                        m_survivor,
                        generatorEntity,
                        castDistance
                    );
                    if (candidate.type != InteractionType::RepairGenerator)
                    {
                        continue;
                    }
                    if (castDistance < bestDistance)
                    {
                        bestDistance = castDistance;
                        bestGenerator = generatorEntity;
                    }
                }
            }

            if (bestGenerator != 0)
            {
                BeginOrContinueGeneratorRepair(bestGenerator);
                ItemPowerLog("Toolbox auto-attached to generator entity=" + std::to_string(bestGenerator));
            }
        }

        if (m_activeRepairGenerator == 0)
        {
            return;
        }

        auto generatorIt = m_world.Generators().find(m_activeRepairGenerator);
        if (generatorIt == m_world.Generators().end() || generatorIt->second.completed)
        {
            return;
        }

        const float baseRate = 1.0F / std::max(1.0F, m_tuning.generatorRepairSecondsBase);
        const float repairBonus = std::max(0.0F, m_survivorItemModifiers.ApplyStat(
            "repair_speed_bonus",
            readParam("repair_speed_bonus", m_tuning.toolboxRepairSpeedBonus)
        )) * baseRate;
        const float chargeRate = std::max(0.05F, m_survivorItemModifiers.ApplyStat(
            "charge_per_second",
            readParam("charge_per_second", m_tuning.toolboxChargeDrainPerSecond)
        ));
        m_survivorItemState.active = true;
        const float consumed = std::min(m_survivorItemState.charges, chargeRate * fixedDt);
        m_survivorItemState.charges = std::max(0.0F, m_survivorItemState.charges - consumed);
        const float consumeScale = (chargeRate * fixedDt) > 1.0e-5F ? consumed / (chargeRate * fixedDt) : 0.0F;
        generatorIt->second.progress = glm::clamp(generatorIt->second.progress + repairBonus * fixedDt * consumeScale, 0.0F, 1.0F);
        if (generatorIt->second.progress >= 1.0F)
        {
            generatorIt->second.progress = 1.0F;
            generatorIt->second.completed = true;
            RefreshGeneratorsCompleted();
            AddRuntimeMessage("Generator completed with toolbox bonus", 1.2F);
            ItemPowerLog("Toolbox completed generator with bonus");
            StopGeneratorRepair();
        }
        return;
    }

    if (itemDef->id == "flashlight")
    {
        m_survivorItemState.active = false;
        if (!useHeld || m_survivorItemState.charges <= 0.0F)
        {
            m_survivorItemState.flashBlindAccum = std::max(0.0F, m_survivorItemState.flashBlindAccum - fixedDt * 0.45F);
            return;
        }

        const auto survivorTransformIt = m_world.Transforms().find(m_survivor);
        const auto killerTransformIt = m_world.Transforms().find(m_killer);
        const auto killerActorIt = m_world.Actors().find(m_killer);
        if (survivorTransformIt == m_world.Transforms().end() ||
            killerTransformIt == m_world.Transforms().end() ||
            killerActorIt == m_world.Actors().end())
        {
            return;
        }

        const float beamRange = std::max(2.0F, m_survivorItemModifiers.ApplyStat("beam_range", readParam("beam_range", m_tuning.flashlightBeamRange)));
        const float beamAngleDeg = std::max(5.0F, m_survivorItemModifiers.ApplyStat("beam_angle_deg", readParam("beam_angle_deg", m_tuning.flashlightBeamAngleDegrees)));
        const float blindNeed = std::max(0.25F, m_survivorItemModifiers.ApplyStat("blind_time_required", readParam("blind_time_required", m_tuning.flashlightBlindBuildSeconds)));
        const float blindDuration = std::max(0.2F, m_survivorItemModifiers.ApplyStat("blind_duration", readParam("blind_duration", m_tuning.flashlightBlindDurationSeconds)));
        const float chargeRate = std::max(0.05F, m_survivorItemModifiers.ApplyStat("charge_per_second", readParam("charge_per_second", 1.0F)));

        const glm::vec3 toKiller = killerTransformIt->second.position - survivorTransformIt->second.position;
        const float dist = glm::length(glm::vec2{toKiller.x, toKiller.z});
        bool inCone = false;
        bool intoFace = false;
        if (dist <= beamRange && dist > 1.0e-4F)
        {
            glm::vec3 dirToKiller{toKiller.x, 0.0F, toKiller.z};
            dirToKiller = glm::normalize(dirToKiller);
            glm::vec3 forward{survivorTransformIt->second.forward.x, 0.0F, survivorTransformIt->second.forward.z};
            if (glm::length(forward) > 1.0e-4F)
            {
                forward = glm::normalize(forward);
                const float dot = glm::dot(forward, dirToKiller);
                inCone = dot >= std::cos(glm::radians(beamAngleDeg * 0.5F));
                glm::vec3 killerFacing{killerTransformIt->second.forward.x, 0.0F, killerTransformIt->second.forward.z};
                if (glm::length(killerFacing) > 1.0e-4F)
                {
                    killerFacing = glm::normalize(killerFacing);
                    const float faceDot = glm::dot(killerFacing, -dirToKiller);
                    intoFace = faceDot >= std::cos(glm::radians(45.0F));
                }
            }
        }

        m_survivorItemState.active = true;
        const float consumed = std::min(m_survivorItemState.charges, chargeRate * fixedDt);
        m_survivorItemState.charges = std::max(0.0F, m_survivorItemState.charges - consumed);
        if (inCone && intoFace)
        {
            m_survivorItemState.flashBlindAccum += fixedDt;
        }
        else
        {
            m_survivorItemState.flashBlindAccum = std::max(0.0F, m_survivorItemState.flashBlindAccum - fixedDt * 0.6F);
        }

        if (m_survivorItemState.flashBlindAccum >= blindNeed)
        {
            killerActorIt->second.stunTimer = std::max(killerActorIt->second.stunTimer, 0.2F);
            m_killerLookLight.enabled = false;
            m_killerPowerState.killerBlindTimer = std::max(m_killerPowerState.killerBlindTimer, blindDuration);
            m_survivorItemState.flashBlindAccum = 0.0F;
            m_survivorItemState.flashlightSuccessFlashTimer = std::max(m_survivorItemState.flashlightSuccessFlashTimer, 0.18F);
            AddRuntimeMessage("Flashlight blind", 1.0F);
            ItemPowerLog("Flashlight blind applied duration=" + std::to_string(blindDuration));
        }
        return;
    }

    if (itemDef->id == "map")
    {
        m_survivorItemState.active = false;
        if (m_survivorItemState.mapUsesRemaining <= 0 && m_survivorItemState.charges > 0.0F)
        {
            m_survivorItemState.mapUsesRemaining = std::max(0, static_cast<int>(std::round(m_survivorItemState.charges)));
        }
        if (!useHeld || m_survivorItemState.charges <= 0.0F || m_survivorItemState.mapUsesRemaining <= 0)
        {
            m_survivorItemState.mapChannelSeconds = 0.0F;
            return;
        }

        const float chargePerUse = std::max(0.1F, m_survivorItemModifiers.ApplyStat("charge_per_use", readParam("charge_per_use", 1.0F)));
        const float channelSeconds = std::max(0.05F, m_survivorItemModifiers.ApplyStat("channel_seconds", readParam("channel_seconds", m_tuning.mapChannelSeconds)));
        const float revealRadius = std::max(4.0F, m_survivorItemModifiers.ApplyStat("reveal_radius", readParam("reveal_radius", m_tuning.mapRevealRangeMeters)));
        const float revealDuration = std::max(0.2F, m_survivorItemModifiers.ApplyStat("reveal_duration", readParam("reveal_duration", m_tuning.mapRevealDurationSeconds)));

        if (m_survivorItemState.charges < chargePerUse || m_survivorItemState.mapUsesRemaining <= 0)
        {
            AddRuntimeMessage("Map: not enough charges", 1.0F);
            return;
        }

        if (m_survivorItemState.cooldown > 0.0F)
        {
            return;
        }

        m_survivorItemState.active = true;
        m_survivorItemState.mapChannelSeconds += fixedDt;
        if (m_survivorItemState.mapChannelSeconds < channelSeconds)
        {
            return;
        }
        m_survivorItemState.mapChannelSeconds = 0.0F;
        m_survivorItemState.cooldown = 0.15F;
        m_survivorItemState.charges = std::max(0.0F, m_survivorItemState.charges - chargePerUse);
        m_survivorItemState.mapUsesRemaining = std::max(0, m_survivorItemState.mapUsesRemaining - 1);
        m_survivorItemState.mapRevealTtl = revealDuration;
        m_mapRevealGenerators.clear();

        int visibleGenerators = 0;
        const auto survivorTransformIt = m_world.Transforms().find(m_survivor);
        if (survivorTransformIt != m_world.Transforms().end())
        {
            for (const auto& [entity, generator] : m_world.Generators())
            {
                if (generator.completed)
                {
                    continue;
                }
                const auto generatorTransformIt = m_world.Transforms().find(entity);
                if (generatorTransformIt == m_world.Transforms().end())
                {
                    continue;
                }
                if (DistanceXZ(survivorTransformIt->second.position, generatorTransformIt->second.position) <= revealRadius)
                {
                    ++visibleGenerators;
                    m_mapRevealGenerators[entity] = revealDuration;
                }
            }
        }

        AddRuntimeMessage("Map reveal: generators " + std::to_string(visibleGenerators), 1.2F);
        ItemPowerLog("Map reveal used generators=" + std::to_string(visibleGenerators));
        return;
    }
}

void GameplaySystems::UpdateKillerPowerSystem(const RoleCommand& killerCommand, float fixedDt)
{
    if (m_killerPowerState.killerBlindTimer > 0.0F)
    {
        m_killerPowerState.killerBlindTimer = std::max(0.0F, m_killerPowerState.killerBlindTimer - fixedDt);
        if (m_killerPowerState.killerBlindTimer > 0.0F)
        {
            m_killerLookLight.enabled = false;
        }
        else
        {
            m_killerLookLight.enabled = true;
        }
    }

    const loadout::PowerDefinition* powerDef = m_loadoutCatalog.FindPower(m_killerLoadout.powerId);
    if (powerDef == nullptr || m_killer == 0)
    {
        m_trapPreviewActive = false;
        return;
    }

    if (powerDef->id == "wraith_cloak")
    {
        m_trapPreviewActive = false;
        UpdateWraithPowerSystem(killerCommand, fixedDt);
        return;
    }

    if (powerDef->id != "bear_trap")
    {
        m_trapPreviewActive = false;
        return;
    }

    m_killerPowerState.trapperMaxCarryTraps = std::max(
        1,
        static_cast<int>(std::round(m_killerPowerModifiers.ApplyStat("max_carry", static_cast<float>(m_tuning.trapperMaxCarryTraps))))
    );
    m_killerPowerState.trapperCarriedTraps = glm::clamp(m_killerPowerState.trapperCarriedTraps, 0, m_killerPowerState.trapperMaxCarryTraps);

    const auto killerTransformIt = m_world.Transforms().find(m_killer);
    auto killerActorIt = m_world.Actors().find(m_killer);
    if (killerTransformIt == m_world.Transforms().end() || killerActorIt == m_world.Actors().end())
    {
        m_trapPreviewActive = false;
        return;
    }

    const float setDuration = std::max(0.2F, m_killerPowerModifiers.ApplyStat("set_duration", m_tuning.trapperSetTrapSeconds));
    engine::scene::Entity nearbyDisarmedTrapForRearm = 0;
    const bool canRearmNearby = TryFindNearestTrap(killerTransformIt->second.position, 2.4F, true, &nearbyDisarmedTrapForRearm);
    if (m_controlledRole == ControlledRole::Killer && m_killerPowerState.trapperCarriedTraps > 0)
    {
        m_trapPreviewActive = ComputeTrapPlacementPreview(&m_trapPreviewPosition, &m_trapPreviewHalfExtents, &m_trapPreviewValid);
    }
    else
    {
        m_trapPreviewActive = false;
    }

    if (!killerCommand.useAltHeld)
    {
        m_killerPowerState.trapperSetRequiresRelease = false;
    }

    if (m_killerPowerState.trapperSetting)
    {
        if (killerCommand.useAltReleased ||
            m_killerPowerState.trapperCarriedTraps <= 0 ||
            m_killerAttackState != KillerAttackState::Idle ||
            killerActorIt->second.stunTimer > 0.0F)
        {
            m_killerPowerState.trapperSetting = false;
            m_killerPowerState.trapperSetTimer = 0.0F;
            m_killerPowerState.trapperSetRequiresRelease = true;
            ItemPowerLog("Trapper set cancelled");
            return;
        }

        killerActorIt->second.velocity = glm::vec3{0.0F};
        m_killerPowerState.trapperSetTimer += fixedDt;
        if (m_killerPowerState.trapperSetTimer >= setDuration)
        {
            if (m_trapPreviewActive && !m_trapPreviewValid)
            {
                m_killerPowerState.trapperSetting = false;
                m_killerPowerState.trapperSetTimer = 0.0F;
                m_killerPowerState.trapperSetRequiresRelease = true;
                AddRuntimeMessage("Invalid trap placement", 0.9F);
                ItemPowerLog("Trapper placement rejected (invalid preview)");
                return;
            }
            glm::vec3 trapPlacement = m_trapPreviewActive ? m_trapPreviewPosition : killerTransformIt->second.position;
            if (!m_trapPreviewActive)
            {
                glm::vec3 killerForward = killerTransformIt->second.forward;
                killerForward.y = 0.0F;
                if (glm::length(killerForward) <= 1.0e-5F)
                {
                    killerForward = glm::vec3{0.0F, 0.0F, -1.0F};
                }
                else
                {
                    killerForward = glm::normalize(killerForward);
                }
                trapPlacement += killerForward * 1.55F;
            }

            SpawnBearTrap(trapPlacement, killerTransformIt->second.forward, true);
            m_killerPowerState.trapperCarriedTraps = std::max(0, m_killerPowerState.trapperCarriedTraps - 1);
            m_killerPowerState.trapperSetting = false;
            m_killerPowerState.trapperSetTimer = 0.0F;
            m_killerPowerState.trapperSetRequiresRelease = true;
            ItemPowerLog("Trapper placed trap, carry=" + std::to_string(m_killerPowerState.trapperCarriedTraps));
            RebuildPhysicsWorld();
        }
        return;
    }

    auto findNearestTrapByState = [&](engine::scene::TrapState state, float radiusMeters) -> engine::scene::Entity {
        float bestDistance = radiusMeters;
        engine::scene::Entity bestEntity = 0;
        for (const auto& [entity, trap] : m_world.BearTraps())
        {
            if (trap.state != state)
            {
                continue;
            }
            const auto trapTransformIt = m_world.Transforms().find(entity);
            if (trapTransformIt == m_world.Transforms().end())
            {
                continue;
            }
            const float distance = DistanceXZ(killerTransformIt->second.position, trapTransformIt->second.position);
            if (distance <= bestDistance)
            {
                bestDistance = distance;
                bestEntity = entity;
            }
        }
        return bestEntity;
    };

    if (killerCommand.useAltPressed)
    {
        const engine::scene::Entity disarmedTrap = findNearestTrapByState(engine::scene::TrapState::Disarmed, 2.4F);
        if (disarmedTrap != 0)
        {
            m_killerPowerState.trapperSetting = false;
            m_killerPowerState.trapperSetTimer = 0.0F;
            auto trapIt = m_world.BearTraps().find(disarmedTrap);
            if (trapIt != m_world.BearTraps().end())
            {
                trapIt->second.state = engine::scene::TrapState::Armed;
                trapIt->second.trappedEntity = 0;
                trapIt->second.escapeAttempts = 0;
                trapIt->second.protectedKiller = m_killer;
                trapIt->second.killerProtectionDistance = 2.0F;
                m_killerPowerState.trapperSetRequiresRelease = true;
                AddRuntimeMessage("Trap re-armed", 1.0F);
                ItemPowerLog("Trapper re-armed trap entity=" + std::to_string(disarmedTrap));
                RebuildPhysicsWorld();
                return;
            }
        }
    }

    if (killerCommand.useAltPressed &&
        !m_killerPowerState.trapperSetRequiresRelease &&
        m_killerPowerState.trapperCarriedTraps > 0 &&
        m_killerAttackState == KillerAttackState::Idle &&
        !canRearmNearby &&
        m_survivorState != SurvivorHealthState::Carried)
    {
        m_killerPowerState.trapperSetting = true;
        m_killerPowerState.trapperSetTimer = 0.0F;
        ItemPowerLog("Trapper started setting trap");
    }

    if (!killerCommand.interactPressed)
    {
        return;
    }

    engine::scene::Entity nearestTrap = findNearestTrapByState(engine::scene::TrapState::Armed, 2.4F);
    if (nearestTrap == 0)
    {
        nearestTrap = findNearestTrapByState(engine::scene::TrapState::Disarmed, 2.4F);
    }
    if (nearestTrap == 0)
    {
        return;
    }

    if (m_killerPowerState.trapperCarriedTraps >= m_killerPowerState.trapperMaxCarryTraps)
    {
        AddRuntimeMessage("Trap inventory full", 0.9F);
        ItemPowerLog("Trapper pickup blocked: inventory full");
        return;
    }

    const auto trapIt = m_world.BearTraps().find(nearestTrap);
    if (trapIt == m_world.BearTraps().end())
    {
        return;
    }
    if (trapIt->second.state != engine::scene::TrapState::Armed &&
        trapIt->second.state != engine::scene::TrapState::Disarmed)
    {
        return;
    }

    DestroyEntity(nearestTrap);
    m_killerPowerState.trapperCarriedTraps += 1;
    AddRuntimeMessage("Picked up trap", 1.0F);
    ItemPowerLog(
        "Trapper picked trap entity=" + std::to_string(nearestTrap) +
        " carry=" + std::to_string(m_killerPowerState.trapperCarriedTraps)
    );
    RebuildPhysicsWorld();
}

void GameplaySystems::UpdateWraithPowerSystem(const RoleCommand& killerCommand, float fixedDt)
{
    if (m_killer == 0)
    {
        return;
    }

    const loadout::PowerDefinition* powerDef = m_loadoutCatalog.FindPower(m_killerLoadout.powerId);
    const auto readParam = [&](const std::string& key, float fallback) {
        if (powerDef == nullptr)
        {
            return fallback;
        }
        const auto it = powerDef->params.find(key);
        return it != powerDef->params.end() ? it->second : fallback;
    };

    const float cloakDuration = std::max(
        0.1F,
        m_killerPowerModifiers.ApplyStat(
            "cloak_transition_seconds",
            readParam("cloak_transition_seconds", m_tuning.wraithCloakTransitionSeconds)
        )
    );
    const float uncloakDuration = std::max(
        0.1F,
        m_killerPowerModifiers.ApplyStat(
            "uncloak_transition_seconds",
            readParam("uncloak_transition_seconds", m_tuning.wraithUncloakTransitionSeconds)
        )
    );

    if (m_killerPowerState.wraithCloakTransition)
    {
        m_killerPowerState.wraithTransitionTimer += fixedDt;
        if (m_killerPowerState.wraithTransitionTimer >= cloakDuration)
        {
            m_killerPowerState.wraithCloakTransition = false;
            m_killerPowerState.wraithCloaked = true;
            m_killerPowerState.wraithTransitionTimer = 0.0F;
            AddRuntimeMessage("Wraith cloaked", 1.0F);
            ItemPowerLog("Wraith cloak completed");
        }
    }
    else if (m_killerPowerState.wraithUncloakTransition)
    {
        m_killerPowerState.wraithTransitionTimer += fixedDt;
        if (m_killerPowerState.wraithTransitionTimer >= uncloakDuration)
        {
            m_killerPowerState.wraithUncloakTransition = false;
            m_killerPowerState.wraithCloaked = false;
            m_killerPowerState.wraithTransitionTimer = 0.0F;
            m_killerPowerState.wraithPostUncloakTimer = std::max(
                0.0F,
                m_killerPowerModifiers.ApplyStat(
                    "post_uncloak_haste_seconds",
                    readParam("post_uncloak_haste_seconds", m_tuning.wraithPostUncloakHasteSeconds)
                )
            );
            AddRuntimeMessage("Wraith uncloaked", 1.0F);
            ItemPowerLog("Wraith uncloak completed, haste=" + std::to_string(m_killerPowerState.wraithPostUncloakTimer));
        }
    }

    if (m_killerPowerState.wraithPostUncloakTimer > 0.0F)
    {
        m_killerPowerState.wraithPostUncloakTimer = std::max(0.0F, m_killerPowerState.wraithPostUncloakTimer - fixedDt);
    }

    if (!killerCommand.useAltPressed)
    {
        return;
    }

    if (m_killerPowerState.wraithCloakTransition || m_killerPowerState.wraithUncloakTransition)
    {
        return;
    }

    if (m_killerPowerState.wraithCloaked)
    {
        m_killerPowerState.wraithUncloakTransition = true;
        m_killerPowerState.wraithTransitionTimer = 0.0F;
        ItemPowerLog("Wraith uncloak started");
    }
    else
    {
        m_killerPowerState.wraithCloakTransition = true;
        m_killerPowerState.wraithTransitionTimer = 0.0F;
        ItemPowerLog("Wraith cloak started");
    }
}

void GameplaySystems::UpdateBearTrapSystem(const RoleCommand& survivorCommand, const RoleCommand& killerCommand, float fixedDt)
{
    (void)killerCommand;
    if (m_survivor == 0)
    {
        return;
    }

    auto survivorTransformIt = m_world.Transforms().find(m_survivor);
    if (survivorTransformIt == m_world.Transforms().end())
    {
        return;
    }

    if (m_survivorState != SurvivorHealthState::Trapped &&
        m_survivorState != SurvivorHealthState::Dead &&
        m_survivorState != SurvivorHealthState::Hooked &&
        m_survivorState != SurvivorHealthState::Carried)
    {
        TryTriggerBearTraps(m_survivor, survivorTransformIt->second.position);
    }

    // Killer stepping into trap: stun killer and reset trap.
    if (m_killer != 0 &&
        m_survivorState != SurvivorHealthState::Carried &&
        !m_killerPowerState.trapperSetting)
    {
        const auto killerTransformIt = m_world.Transforms().find(m_killer);
        const auto killerActorIt = m_world.Actors().find(m_killer);
        if (killerTransformIt != m_world.Transforms().end() && killerActorIt != m_world.Actors().end())
        {
            const float killerRadius = killerActorIt->second.capsuleRadius;
            const float killerHalfHeight = std::max(0.2F, killerActorIt->second.capsuleHeight * 0.5F);
            for (auto& [entity, trap] : m_world.BearTraps())
            {
                if (trap.state != engine::scene::TrapState::Armed)
                {
                    continue;
                }
                const auto trapTransformIt = m_world.Transforms().find(entity);
                if (trapTransformIt == m_world.Transforms().end())
                {
                    continue;
                }
                if (trap.protectedKiller == m_killer)
                {
                    const float distanceFromTrap = DistanceXZ(killerTransformIt->second.position, trapTransformIt->second.position);
                    if (distanceFromTrap < trap.killerProtectionDistance)
                    {
                        continue;
                    }
                    trap.protectedKiller = 0;
                }
                const glm::vec3 delta = killerTransformIt->second.position - trapTransformIt->second.position;
                const bool overlap =
                    std::abs(delta.x) <= (trap.halfExtents.x + killerRadius) &&
                    std::abs(delta.z) <= (trap.halfExtents.z + killerRadius) &&
                    std::abs(delta.y) <= (trap.halfExtents.y + killerHalfHeight + 0.2F);
                if (!overlap)
                {
                    continue;
                }
                trap.state = engine::scene::TrapState::Disarmed;
                trap.trappedEntity = 0;
                trap.escapeAttempts = 0;
                killerActorIt->second.stunTimer = std::max(killerActorIt->second.stunTimer, m_tuning.trapKillerStunSeconds);
                AddRuntimeMessage("Killer stepped in trap", 1.2F);
                ItemPowerLog("Killer stepped in trap entity=" + std::to_string(entity) + " stun=" + std::to_string(m_tuning.trapKillerStunSeconds));
                m_trapIndicatorText = "Killer stepped in trap (stunned)";
                m_trapIndicatorTimer = 1.6F;
                m_trapIndicatorDanger = true;
                RebuildPhysicsWorld();
                break;
            }
        }
    }

    // Survivor disarm while not trapped.
    if (m_survivorState != SurvivorHealthState::Trapped &&
        m_survivorState != SurvivorHealthState::Hooked &&
        m_survivorState != SurvivorHealthState::Carried &&
        m_survivorState != SurvivorHealthState::Dead)
    {
        engine::scene::Entity armedTrap = 0;
        if (TryFindNearestTrap(survivorTransformIt->second.position, 1.9F, false, &armedTrap))
        {
            auto trapIt = m_world.BearTraps().find(armedTrap);
            if (trapIt != m_world.BearTraps().end() && trapIt->second.state == engine::scene::TrapState::Armed && survivorCommand.interactHeld)
            {
                if (m_survivorItemState.trapDisarmTarget != armedTrap)
                {
                    m_survivorItemState.trapDisarmTarget = armedTrap;
                    m_survivorItemState.trapDisarmProgress = 0.0F;
                }
                m_survivorItemState.trapDisarmProgress += fixedDt;
                if (m_survivorItemState.trapDisarmProgress >= std::max(0.2F, m_tuning.trapperDisarmSeconds))
                {
                    trapIt->second.state = engine::scene::TrapState::Disarmed;
                    trapIt->second.trappedEntity = 0;
                    trapIt->second.escapeAttempts = 0;
                    m_survivorItemState.trapDisarmProgress = 0.0F;
                    m_survivorItemState.trapDisarmTarget = 0;
                    AddRuntimeMessage("Trap disarmed", 1.0F);
                    ItemPowerLog("Survivor disarmed trap entity=" + std::to_string(armedTrap));
                    m_trapIndicatorText = "Trap disarmed";
                    m_trapIndicatorTimer = 1.0F;
                    m_trapIndicatorDanger = false;
                    RebuildPhysicsWorld();
                }
            }
            else if (!survivorCommand.interactHeld)
            {
                m_survivorItemState.trapDisarmProgress = 0.0F;
                m_survivorItemState.trapDisarmTarget = 0;
            }
        }
        else
        {
            m_survivorItemState.trapDisarmProgress = 0.0F;
            m_survivorItemState.trapDisarmTarget = 0;
        }
    }

    if (m_survivorState != SurvivorHealthState::Trapped)
    {
        return;
    }

    auto trappedTrapIt = m_world.BearTraps().end();
    for (auto it = m_world.BearTraps().begin(); it != m_world.BearTraps().end(); ++it)
    {
        if (it->second.trappedEntity == m_survivor && it->second.state == engine::scene::TrapState::Triggered)
        {
            trappedTrapIt = it;
            break;
        }
    }
    if (trappedTrapIt == m_world.BearTraps().end())
    {
        SetSurvivorState(SurvivorHealthState::Injured, "Trap released");
        return;
    }

    engine::scene::BearTrapComponent& trap = trappedTrapIt->second;
    auto survivorActorIt = m_world.Actors().find(m_survivor);
    if (survivorActorIt != m_world.Actors().end())
    {
        survivorActorIt->second.velocity = glm::vec3{0.0F};
    }

    if (!survivorCommand.interactPressed)
    {
        return;
    }

    trap.escapeAttempts += 1;
    const float chanceStep = glm::clamp(
        m_killerPowerModifiers.ApplyStat("escape_chance_step", std::max(0.01F, m_tuning.trapEscapeChanceStep)),
        0.01F,
        0.95F
    );
    const int maxAttempts = std::max(1, static_cast<int>(std::round(m_killerPowerModifiers.ApplyStat("max_escape_attempts", static_cast<float>(trap.maxEscapeAttempts)))));
    trap.maxEscapeAttempts = maxAttempts;
    const float maxEscapeChance = glm::clamp(m_tuning.trapEscapeChanceMax, 0.05F, 0.99F);
    trap.escapeChance = glm::clamp(trap.escapeChance + chanceStep, 0.03F, maxEscapeChance);

    std::uniform_real_distribution<float> chanceRoll(0.0F, 1.0F);
    const bool success = chanceRoll(m_rng) <= trap.escapeChance || trap.escapeAttempts >= trap.maxEscapeAttempts;
    if (success)
    {
        trap.state = engine::scene::TrapState::Disarmed;
        trap.trappedEntity = 0;
        const float bleedMult = m_killerPowerModifiers.ApplyHook("trap_escape", "bleed_multiplier", 1.0F);
        SetSurvivorState(SurvivorHealthState::Injured, "Escaped trap");
        if (bleedMult > 1.01F)
        {
            AddRuntimeMessage("Escaped trap (Serrated Jaws bleed)", 1.2F);
            m_bloodSpawnAccumulator = std::max(m_bloodSpawnAccumulator, m_bloodProfile.spawnInterval * 0.65F);
            ItemPowerLog("Survivor escaped trap with bleed modifier");
        }
        else
        {
            AddRuntimeMessage("Escaped trap", 1.0F);
            ItemPowerLog("Survivor escaped trap");
        }
        m_trapIndicatorText = "Escaped trap";
        m_trapIndicatorTimer = 1.0F;
        m_trapIndicatorDanger = false;
        RebuildPhysicsWorld();
    }
    else
    {
        AddRuntimeMessage(
            "Trap escape failed (" + std::to_string(trap.escapeAttempts) + "/" + std::to_string(trap.maxEscapeAttempts) + ")",
            1.0F
        );
        ItemPowerLog(
            "Trap escape failed attempts=" + std::to_string(trap.escapeAttempts) +
            "/" + std::to_string(trap.maxEscapeAttempts)
        );
    }
}

engine::scene::Entity GameplaySystems::SpawnBearTrap(const glm::vec3& basePosition, const glm::vec3& forward, bool emitMessage)
{
    const loadout::PowerDefinition* powerDef = m_loadoutCatalog.FindPower(m_killerLoadout.powerId);
    const auto readParam = [&](const std::string& key, float fallback) {
        if (powerDef == nullptr)
        {
            return fallback;
        }
        const auto it = powerDef->params.find(key);
        return it != powerDef->params.end() ? it->second : fallback;
    };

    const glm::vec3 normalizedForward = glm::length(forward) > 1.0e-5F ? glm::normalize(forward) : glm::vec3{0.0F, 0.0F, -1.0F};
    glm::vec3 position = basePosition;

    const glm::vec3 rayStart = position + glm::vec3{0.0F, 2.2F, 0.0F};
    const glm::vec3 rayEnd = rayStart + glm::vec3{0.0F, -8.0F, 0.0F};
    if (const std::optional<engine::physics::RaycastHit> hit = m_physics.RaycastNearest(rayStart, rayEnd))
    {
        position.y = hit->position.y + 0.05F;
    }
    else
    {
        position.y = 0.05F;
    }

    const engine::scene::Entity trapEntity = m_world.CreateEntity();
    m_world.Transforms()[trapEntity] = engine::scene::Transform{
        position,
        glm::vec3{0.0F},
        glm::vec3{1.0F},
        glm::vec3{normalizedForward.x, 0.0F, normalizedForward.z},
    };

    engine::scene::BearTrapComponent trap;
    trap.state = engine::scene::TrapState::Armed;
    trap.halfExtents.x = std::max(0.12F, m_killerPowerModifiers.ApplyStat("trap_half_x", readParam("trap_half_x", 0.36F)));
    trap.halfExtents.y = std::max(0.03F, m_killerPowerModifiers.ApplyStat("trap_half_y", readParam("trap_half_y", 0.08F)));
    trap.halfExtents.z = std::max(0.12F, m_killerPowerModifiers.ApplyStat("trap_half_z", readParam("trap_half_z", 0.36F)));
    trap.escapeChance = glm::clamp(
        m_killerPowerModifiers.ApplyStat("base_escape_chance", readParam("base_escape_chance", m_tuning.trapEscapeBaseChance)),
        0.02F,
        0.95F
    );
    trap.escapeChanceStep = glm::clamp(
        m_killerPowerModifiers.ApplyStat("escape_chance_step", readParam("escape_chance_step", m_tuning.trapEscapeChanceStep)),
        0.01F,
        0.6F
    );
    trap.escapeAttempts = 0;
    trap.maxEscapeAttempts = std::max(
        1,
        static_cast<int>(std::round(m_killerPowerModifiers.ApplyStat("max_escape_attempts", readParam("max_escape_attempts", 6.0F))))
    );
    trap.protectedKiller = m_killer;
    trap.killerProtectionDistance = 2.0F;
    m_world.BearTraps()[trapEntity] = trap;
    m_world.Names()[trapEntity] = engine::scene::NameComponent{"bear_trap"};
    m_world.DebugColors()[trapEntity] = engine::scene::DebugColorComponent{glm::vec3{0.72F, 0.72F, 0.75F}};
    if (emitMessage)
    {
        AddRuntimeMessage("Bear trap placed", 1.0F);
        ItemPowerLog(
            "Trap placed entity=" + std::to_string(trapEntity) +
            " pos=(" + std::to_string(position.x) + "," + std::to_string(position.y) + "," + std::to_string(position.z) + ")"
        );
    }
    return trapEntity;
}

void GameplaySystems::ClearAllBearTraps()
{
    std::vector<engine::scene::Entity> trapEntities;
    trapEntities.reserve(m_world.BearTraps().size());
    for (const auto& [entity, _] : m_world.BearTraps())
    {
        trapEntities.push_back(entity);
    }
    for (engine::scene::Entity entity : trapEntities)
    {
        DestroyEntity(entity);
    }
    if (m_survivorState == SurvivorHealthState::Trapped)
    {
        SetSurvivorState(SurvivorHealthState::Injured, "Traps cleared");
    }
}

void GameplaySystems::ClearTrappedSurvivorBinding(engine::scene::Entity survivorEntity, bool disarmTrap)
{
    if (survivorEntity == 0)
    {
        return;
    }

    for (auto& [entity, trap] : m_world.BearTraps())
    {
        if (trap.trappedEntity != survivorEntity)
        {
            continue;
        }
        trap.trappedEntity = 0;
        trap.escapeAttempts = 0;
        trap.state = disarmTrap ? engine::scene::TrapState::Disarmed : engine::scene::TrapState::Armed;
        if (!disarmTrap)
        {
            trap.protectedKiller = m_killer;
            trap.killerProtectionDistance = 1.8F;
        }
        ItemPowerLog(
            "Cleared trap binding trap=" + std::to_string(entity) +
            " disarm=" + std::string(disarmTrap ? "true" : "false")
        );
    }
}

void GameplaySystems::TryTriggerBearTraps(engine::scene::Entity survivorEntity, const glm::vec3& survivorPos)
{
    if (survivorEntity == 0)
    {
        return;
    }

    const auto survivorActorIt = m_world.Actors().find(survivorEntity);
    if (survivorActorIt == m_world.Actors().end())
    {
        return;
    }
    const float survivorRadius = survivorActorIt->second.capsuleRadius;
    const float survivorHalfHeight = std::max(0.2F, survivorActorIt->second.capsuleHeight * 0.5F);

    for (auto& [entity, trap] : m_world.BearTraps())
    {
        if (trap.state != engine::scene::TrapState::Armed)
        {
            continue;
        }
        const auto trapTransformIt = m_world.Transforms().find(entity);
        if (trapTransformIt == m_world.Transforms().end())
        {
            continue;
        }

        const glm::vec3 delta = survivorPos - trapTransformIt->second.position;
        const bool overlap =
            std::abs(delta.x) <= (trap.halfExtents.x + survivorRadius) &&
            std::abs(delta.z) <= (trap.halfExtents.z + survivorRadius) &&
            std::abs(delta.y) <= (trap.halfExtents.y + survivorHalfHeight + 0.2F);
        if (!overlap)
        {
            continue;
        }

        trap.state = engine::scene::TrapState::Triggered;
        trap.trappedEntity = survivorEntity;
        trap.escapeAttempts = 0;
        trap.escapeChance = glm::clamp(
            m_killerPowerModifiers.ApplyStat("base_escape_chance", std::max(0.01F, m_tuning.trapEscapeBaseChance)),
            0.01F,
            0.95F
        );
        trap.escapeChanceStep = glm::clamp(
            m_killerPowerModifiers.ApplyStat("escape_chance_step", std::max(0.01F, m_tuning.trapEscapeChanceStep)),
            0.01F,
            0.95F
        );
        SetSurvivorState(SurvivorHealthState::Trapped, "Bear trap triggered");
        AddRuntimeMessage("Survivor trapped", 1.2F);
        ItemPowerLog("Survivor trapped by entity=" + std::to_string(entity));
        m_trapIndicatorText = "Survivor trapped!";
        m_trapIndicatorTimer = 1.5F;
        m_trapIndicatorDanger = true;
        RebuildPhysicsWorld();
        break;
    }
}

void GameplaySystems::TrapSpawnDebug(int count)
{
    if (m_killer == 0)
    {
        return;
    }
    const auto killerTransformIt = m_world.Transforms().find(m_killer);
    if (killerTransformIt == m_world.Transforms().end())
    {
        return;
    }
    const int spawnCount = std::max(1, count);
    for (int i = 0; i < spawnCount; ++i)
    {
        glm::vec3 spawnPos = killerTransformIt->second.position;
        glm::vec3 forward = killerTransformIt->second.forward;
        forward.y = 0.0F;
        if (glm::length(forward) > 1.0e-5F)
        {
            spawnPos += glm::normalize(forward) * 1.55F;
        }
        else
        {
            spawnPos += glm::vec3{0.0F, 0.0F, -1.55F};
        }
        SpawnBearTrap(spawnPos, killerTransformIt->second.forward, spawnCount == 1);
    }
    if (spawnCount > 1)
    {
        AddRuntimeMessage("Spawned " + std::to_string(spawnCount) + " traps", 1.0F);
    }
    ItemPowerLog("Trap debug spawn count=" + std::to_string(spawnCount));
    RebuildPhysicsWorld();
}

void GameplaySystems::TrapClearDebug()
{
    ClearAllBearTraps();
    RebuildPhysicsWorld();
}

void GameplaySystems::SetScratchDebug(bool enabled)
{
    m_scratchDebugEnabled = enabled;
}

void GameplaySystems::SetBloodDebug(bool enabled)
{
    m_bloodDebugEnabled = enabled;
}

void GameplaySystems::SetScratchProfile(const std::string& profileName)
{
    (void)profileName;
    m_scratchProfile = ScratchProfile{};
    m_scratchProfile.spawnIntervalMin = 0.08F;
    m_scratchProfile.spawnIntervalMax = 0.15F;
}

void GameplaySystems::SetBloodProfile(const std::string& profileName)
{
    (void)profileName;
    m_bloodProfile = BloodProfile{};
}

} // namespace game::gameplay
