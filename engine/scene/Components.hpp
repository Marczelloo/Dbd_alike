#pragma once

#include <cstdint>
#include <string>

#include <glm/vec3.hpp>

namespace engine::scene
{
using Entity = std::uint32_t;

enum class Role
{
    Survivor,
    Killer
};

enum class PalletState
{
    Standing,
    Dropped,
    Broken
};

enum class TrapState
{
    Armed,
    Triggered,
    Disarmed
};

struct Transform
{
    glm::vec3 position{0.0F, 0.0F, 0.0F};
    glm::vec3 rotationEuler{0.0F, 0.0F, 0.0F};
    glm::vec3 scale{1.0F, 1.0F, 1.0F};
    glm::vec3 forward{0.0F, 0.0F, -1.0F};
};

struct ActorComponent
{
    Role role = Role::Survivor;
    float walkSpeed = 4.0F;
    float sprintSpeed = 6.0F;
    float capsuleRadius = 0.45F;
    float capsuleHeight = 1.8F;
    float stepHeight = 0.45F;
    float eyeHeight = 1.55F;
    bool sprinting = false;
    float forwardRunupDistance = 0.0F;
    bool jumpEnabled = false;
    float jumpVelocity = 6.0F;
    bool collisionEnabled = true;
    bool noclipEnabled = false;
    bool grounded = false;
    glm::vec3 velocity{0.0F, 0.0F, 0.0F};
    glm::vec3 lastCollisionNormal{0.0F, 1.0F, 0.0F};
    float lastPenetrationDepth = 0.0F;
    bool vaulting = false;
    float vaultTimer = 0.0F;
    float vaultDuration = 0.35F;
    float vaultCooldown = 0.0F;
    glm::vec3 vaultStart{0.0F};
    glm::vec3 vaultEnd{0.0F};
    float vaultArcHeight = 0.55F;
    std::string lastVaultType = "None";
    float stunTimer = 0.0F;
    bool carried = false;
    bool crouching = false;
    bool crawling = false;
};

struct StaticBoxComponent
{
    glm::vec3 halfExtents{0.5F};
    bool solid = true;
};

struct WindowComponent
{
    glm::vec3 halfExtents{0.7F, 1.0F, 0.1F};
    glm::vec3 normal{0.0F, 0.0F, 1.0F};
    float survivorVaultTime = 0.35F;
    float killerVaultMultiplier = 1.6F;
    bool killerCanVault = true;
};

struct PalletComponent
{
    glm::vec3 halfExtents{0.9F, 0.6F, 0.18F};
    glm::vec3 standingHalfExtents{0.24F, 1.08F, 1.1F};
    glm::vec3 droppedHalfExtents{1.1F, 0.58F, 0.34F};
    float standingCenterY = 1.08F;
    float droppedCenterY = 0.58F;
    PalletState state = PalletState::Standing;
    float breakTimer = 0.0F;
    float breakDuration = 1.6F;
    float stunDuration = 1.6F;
};

struct HookComponent
{
    glm::vec3 halfExtents{0.3F, 1.1F, 0.3F};
    bool occupied = false;
};

struct GeneratorComponent
{
    glm::vec3 halfExtents{0.35F, 0.6F, 0.35F};  // Zmniejszone: 0.7->0.35 (XZ), 0.7->0.6 (Y)
    float progress = 0.0F;
    bool completed = false;
};

struct BearTrapComponent
{
    TrapState state = TrapState::Armed;
    glm::vec3 halfExtents{0.36F, 0.08F, 0.36F};
    Entity trappedEntity = 0;
    float escapeChance = 0.22F;
    float escapeChanceStep = 0.14F;
    int escapeAttempts = 0;
    int maxEscapeAttempts = 6;
    Entity protectedKiller = 0;
    float killerProtectionDistance = 2.0F;
};

struct GroundItemComponent
{
    std::string itemId;
    float charges = 0.0F;
    std::string addonAId;
    std::string addonBId;
    std::uint32_t ownerNetId = 0;
    bool pickupEnabled = true;
    bool respawnTag = false;
};

struct DebugColorComponent
{
    glm::vec3 color{1.0F, 1.0F, 1.0F};
};

struct NameComponent
{
    std::string name;
};

struct ProjectileState
{
    enum class Type
    {
        Hatchet
    };
    Type type = Type::Hatchet;
    bool active = false;
    glm::vec3 velocity{0.0F};
    glm::vec3 position{0.0F};
    glm::vec3 forward{0.0F, 0.0F, -1.0F};
    float age = 0.0F;
    float maxLifetime = 5.0F;
    float gravity = 9.81F;
    engine::scene::Entity ownerEntity = 0;
    bool hasHit = false;
};

struct LockerComponent
{
    glm::vec3 halfExtents{0.45F, 1.1F, 0.35F};
    bool killerOnly = true;
};
} // namespace engine::scene
