#pragma once

#include <array>
#include <random>
#include <vector>
#include <optional>

#include <glm/vec3.hpp>

namespace game::gameplay
{
/**
 * Dead by Daylight-inspired spawn system
 * 
 * Based on community research and official patch notes:
 * - Patch 9.0.0 introduced clustered survivor spawns (default within 12m)
 * - Killers spawn away from center on small maps
 * - Survivors must spawn at least 32m away from killer
 * - Survivor spawns are tied to generator locations
 */

// ============================================================================
// Configuration Constants (based on DBD research)
// ============================================================================

namespace SpawnConstants
{
    constexpr float MIN_SURVIVOR_KILLER_DISTANCE = 32.0F;  // BHVR confirmed
    constexpr float DEFAULT_SURVIVOR_CLUSTER_RADIUS = 12.0F; // Patch 9.0.0
    constexpr float PREFERRED_KILLER_EDGE_BIAS = 0.6F;      // Prefer edge spawns
    constexpr float MIN_INTER_SURVIVOR_DISTANCE = 3.0F;     // Prevent overlap
    constexpr float MAX_MAP_SIZE_FOR_CENTER_RULE = 40.0F;   // "Small map" threshold
    constexpr float KILLER_CENTER_RADIUS_THRESHOLD = 10.0F; // Avoid center within this
    
    // Nowe: generator heuristics (DBD community "2nd furthest gen")
    constexpr float GEN_DISTANCE_SWEET_SPOT_MIN = 20.0F;    // Minimum ideal distance from killer
    constexpr float GEN_DISTANCE_SWEET_SPOT_MAX = 45.0F;    // Maximum ideal distance from killer
    constexpr float GEN_PROXIMITY_THRESHOLD = 18.0F;        // Max distance to be "near" a generator
    
    // Nowe: floor handling for multi-floor maps
    constexpr float FLOOR_HEIGHT_TOLERANCE = 2.5F;          // Max Y difference to be considered "same floor"
}

// ============================================================================
// Spawn Point Definitions
// ============================================================================

struct SpawnPoint
{
    glm::vec3 position{0.0F};
    int tileId = -1;              // Which tile this spawn belongs to
    int floorId = 0;              // For multi-floor maps
    float quality = 1.0F;         // Weight for random selection
    bool isNearGenerator = false; // Proximity to generators
    bool isMapCenter = false;     // Marked as central spawn point
};

struct GeneratorLocation
{
    glm::vec3 position{0.0F};
    int tileId = -1;
};

// Map bounds for spawn calculations
struct MapBounds
{
    glm::vec3 center{0.0F};
    float maxDistanceFromCenter = 0.0F;
};

// ============================================================================
// Offering Types (based on DBD Shroud system)
// ============================================================================

enum class SurvivorSpawnMode : uint8_t
{
    Clustered,        // Default post-9.0.0: all within 12m
    Split,            // Shroud of Separation: maximize distances
    SemiClustered,    // Vigo's: one far, others clustered
    Spread            // Pre-9.0.0: more distributed
};

enum class KillerSpawnMode : uint8_t
{
    Standard,         // Normal weighted random with edge bias
    Central,          // Force more central spawn
    Edge              // Force edge spawn
};

struct SpawnOfferings
{
    SurvivorSpawnMode survivorMode = SurvivorSpawnMode::Clustered;
    KillerSpawnMode killerMode = KillerSpawnMode::Standard;
    bool shroudOfVanishingActive = false; // Killer counters survivor offerings
    int vigoShroudOwner = -1;             // Which survivor gets farthest spawn (-1 = none)
};

// ============================================================================
// Spawn Calculation Results
// ============================================================================

struct SpawnResult
{
    glm::vec3 killerSpawn{0.0F};
    std::array<glm::vec3, 4> survivorSpawns;
    
    // Debug/validation info
    float minSurvivorKillerDistance = 0.0F;
    float maxSurvivorClusterRadius = 0.0F;
    float averageInterSurvivorDistance = 0.0F;
    int survivorsOnSameFloor = 0;
    std::string validationMessage;
};

// ============================================================================
// Main Spawn Calculator
// ============================================================================

class SpawnCalculator
{
public:
    /**
     * Calculate spawn positions for killer and survivors
     * 
     * @param killerSpawnPoints Available killer spawn locations
     * @param survivorSpawnPoints Available survivor spawn locations
     * @param generators Generator positions on the map
     * @param offerings Active spawn offerings/modifiers
     * @param seed Random seed for reproducibility
     * @return Complete spawn configuration
     */
    SpawnResult CalculateSpawns(
        const std::vector<SpawnPoint>& killerSpawnPoints,
        const std::vector<SpawnPoint>& survivorSpawnPoints,
        const std::vector<GeneratorLocation>& generators,
        const SpawnOfferings& offerings = SpawnOfferings{},
        unsigned int seed = std::random_device{}()
    ) const;

    /**
     * Validate spawn result against design rules
     */
    bool ValidateSpawn(const SpawnResult& result, std::string& errorMessage) const;

    /**
     * Calculate map bounds for center detection
     */
    static MapBounds CalculateMapBounds(const std::vector<SpawnPoint>& spawnPoints);

private:
    // Killer spawn selection
    SpawnPoint SelectKillerSpawn(
        const std::vector<SpawnPoint>& killerSpawns,
        const std::vector<GeneratorLocation>& generators,
        const SpawnOfferings& offerings,
        std::mt19937& rng
    ) const;

    std::vector<SpawnPoint> FilterKillerSpawns(
        const std::vector<SpawnPoint>& killerSpawns,
        const MapBounds& bounds,
        const SpawnOfferings& offerings
    ) const;

    // Survivor spawn selection
    std::vector<SpawnPoint> SelectSurvivorSpawns(
        const std::vector<SpawnPoint>& survivorSpawns,
        const SpawnPoint& killerSpawn,
        const std::vector<GeneratorLocation>& generators,
        const SpawnOfferings& offerings,
        std::mt19937& rng
    ) const;

    std::vector<SpawnPoint> FilterSurvivorSpawnsByKillerDistance(
        const std::vector<SpawnPoint>& survivorSpawns,
        const SpawnPoint& killerSpawn
    ) const;

    // Clustering modes
    std::vector<SpawnPoint> SelectClusteredSpawns(
        const std::vector<SpawnPoint>& candidateSpawns,
        const SpawnPoint& killerSpawn,
        const std::vector<GeneratorLocation>& generators,
        std::mt19937& rng
    ) const;

    std::vector<SpawnPoint> SelectSplitSpawns(
        const std::vector<SpawnPoint>& candidateSpawns,
        const SpawnPoint& killerSpawn,
        std::mt19937& rng
    ) const;

    std::vector<SpawnPoint> SelectSemiClusteredSpawns(
        const std::vector<SpawnPoint>& candidateSpawns,
        const SpawnPoint& killerSpawn,
        const std::vector<GeneratorLocation>& generators,
        std::mt19937& rng
    ) const;

    std::vector<SpawnPoint> SelectSpreadSpawns(
        const std::vector<SpawnPoint>& candidateSpawns,
        const SpawnPoint& killerSpawn,
        const std::vector<GeneratorLocation>& generators,
        std::mt19937& rng
    ) const;

    // Utility functions
    static float Distance(const glm::vec3& a, const glm::vec3& b);
    static float Distance2D(const glm::vec3& a, const glm::vec3& b);
    
    SpawnPoint FindFurthestSpawn(
        const std::vector<SpawnPoint>& spawns,
        const glm::vec3& referencePoint
    ) const;

    SpawnPoint FindClosestSpawn(
        const std::vector<SpawnPoint>& spawns,
        const glm::vec3& referencePoint
    ) const;

    SpawnPoint FindClusterCenter(
        const std::vector<SpawnPoint>& candidateSpawns,
        const SpawnPoint& killerSpawn,
        const std::vector<GeneratorLocation>& generators,
        std::mt19937& rng
    ) const;

    std::vector<SpawnPoint> FindPointsWithinRadius(
        const std::vector<SpawnPoint>& candidateSpawns,
        const glm::vec3& center,
        float radius,
        int targetCount,
        bool preferSameFloor,
        std::mt19937& rng
    ) const;

    float CalculateSpawnQuality(
        const SpawnPoint& spawn,
        const SpawnPoint& killerSpawn,
        const std::vector<GeneratorLocation>& generators
    ) const;
};

// ============================================================================
// Helper for generating spawn points from tile data
// ============================================================================

class SpawnPointGenerator
{
public:
    /**
     * Generate spawn points based on tile layout
     * 
     * This would typically be called during map generation to populate
     * the spawn point lists from tile socket definitions.
     */
    static std::vector<SpawnPoint> GenerateKillerSpawns(
        const std::vector<glm::vec3>& tileCenters,
        const MapBounds& bounds
    );

    static std::vector<SpawnPoint> GenerateSurvivorSpawns(
        const std::vector<glm::vec3>& tileCenters,
        const std::vector<GeneratorLocation>& generators,
        const MapBounds& bounds
    );
};

} // namespace game::gameplay
