#pragma once

#include <string>
#include <vector>

#include <glm/vec3.hpp>

// Forward declaration for spawn system integration
namespace game::gameplay
{
class SpawnCalculator;
struct SpawnResult;
}

namespace game::maps
{
struct BoxSpawn
{
    glm::vec3 center{0.0F};
    glm::vec3 halfExtents{0.5F};
};

struct WindowSpawn
{
    glm::vec3 center{0.0F};
    glm::vec3 halfExtents{0.6F, 1.0F, 0.1F};
    glm::vec3 normal{0.0F, 0.0F, 1.0F};
};

struct PalletSpawn
{
    glm::vec3 center{0.0F};
    glm::vec3 halfExtents{0.9F, 0.6F, 0.18F};
};

// High-poly mesh placement for GPU stress testing
struct HighPolyMeshSpawn
{
    enum class Type
    {
        IcoSphere,      // Geodesic sphere with subdivision
        Torus,          // Torus with many rings/segments
        GridPlane,      // High-res terrain-like grid
        SpiralStair     // Spiral staircase with many steps
    };
    
    glm::vec3 position{0.0F};
    glm::vec3 rotation{0.0F};   // Euler degrees
    glm::vec3 scale{1.0F};
    glm::vec3 color{0.6F, 0.5F, 0.4F};
    Type type = Type::IcoSphere;
    int detailLevel = 4;        // Subdivision level (higher = more polys)
    bool castShadows = true;
};

struct GeneratedMap
{
    struct TileDebug
    {
        glm::vec3 center{0.0F};
        glm::vec3 halfExtents{5.0F, 0.05F, 5.0F};
        int loopId = 0;
        int archetype = 0;
    };

    // Mesh placement for loops with custom meshes
    struct MeshPlacement
    {
        std::string meshPath;       // Path to .glb/.gltf file
        glm::vec3 position{0.0F};   // World position
        float rotationDegrees = 0.0F; // Y-axis rotation
    };

    std::vector<BoxSpawn> walls;
    std::vector<WindowSpawn> windows;
    std::vector<PalletSpawn> pallets;
    std::vector<glm::vec3> generatorSpawns; // Positions for generators (always 5)
    std::vector<TileDebug> tiles;
    std::vector<HighPolyMeshSpawn> highPolyMeshes; // GPU stress test meshes
    std::vector<MeshPlacement> meshPlacements; // Custom mesh placements for loops
    
    // Legacy single spawn points (kept for backward compatibility)
    glm::vec3 survivorSpawn{0.0F};
    glm::vec3 killerSpawn{0.0F};
    
    // DBD-inspired spawn system support
    std::vector<glm::vec3> survivorSpawns; // 4 survivor spawn positions
    bool useDbdSpawns = false; // Enable/disable new spawn system
};

class TileGenerator
{
public:
    struct GenerationSettings
    {
        // --- Archetype weights (0 = disabled) ---
        float weightTLWalls = 1.0F;
        float weightJungleGymLong = 1.0F;
        float weightJungleGymShort = 1.0F;
        float weightShack = 1.0F;
        float weightFourLane = 1.0F;
        float weightFillerA = 1.0F;
        float weightFillerB = 1.0F;

        // --- New v2 loop types ---
        float weightLongWall = 1.0F;       // Single long wall with window
        float weightShortWall = 0.8F;      // Single short wall with pallet (unsafe)
        float weightLWallWindow = 1.2F;    // L-shaped walls, window on long side
        float weightLWallPallet = 1.0F;    // L-shaped walls, pallet on short side
        float weightTWalls = 0.9F;         // T-shaped intersecting walls
        float weightGymBox = 1.1F;         // Rectangular gym enclosure (window + pallet)
        float weightDebrisPile = 0.6F;     // Cluster of small solids with line-of-sight breaks

        // --- Constraints ---
        int maxLoops = 40;
        float minLoopDistanceTiles = 2.0F;
        int maxSafePallets = 12;        // Limit strong pallets for balance
        int maxDeadzoneTiles = 3;       // Max consecutive tiles without a loop before forcing one
        bool edgeBiasLoops = true;      // Prefer loops near map edges to reduce deadzones
        bool disableWindowsAndPallets = false; // Remove windows/pallets after layout
    };

    GeneratedMap GenerateTestMap() const;
    GeneratedMap GenerateMainMap(unsigned int seed) const;
    GeneratedMap GenerateMainMap(unsigned int seed, const GenerationSettings& settings) const;
    GeneratedMap GenerateCollisionTestMap() const;
    GeneratedMap GenerateBenchmarkMap() const; // Comprehensive benchmark + collision stress test

    // DBD-inspired spawn calculation
    void CalculateDbdSpawns(GeneratedMap& map, unsigned int seed) const;
};
} // namespace game::maps
