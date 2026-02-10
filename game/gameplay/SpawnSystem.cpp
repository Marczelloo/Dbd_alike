#include "SpawnSystem.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <string>
#include <unordered_map>

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtx/norm.hpp>

namespace game::gameplay
{
// ============================================================================
// Utility Functions
// ============================================================================

float SpawnCalculator::Distance(const glm::vec3& a, const glm::vec3& b)
{
    glm::vec3 diff = a - b;
    return std::sqrt(diff.x * diff.x + diff.y * diff.y + diff.z * diff.z);
}

float SpawnCalculator::Distance2D(const glm::vec3& a, const glm::vec3& b)
{
    glm::vec2 diff = glm::vec2(a.x, a.z) - glm::vec2(b.x, b.z);
    return std::sqrt(diff.x * diff.x + diff.y * diff.y);
}

// ============================================================================
// SpawnCalculator Implementation
// ============================================================================

SpawnResult SpawnCalculator::CalculateSpawns(
    const std::vector<SpawnPoint>& killerSpawnPoints,
    const std::vector<SpawnPoint>& survivorSpawnPoints,
    const std::vector<GeneratorLocation>& generators,
    const SpawnOfferings& offerings,
    unsigned int seed
) const
{
    SpawnResult result;
    std::mt19937 rng(seed);

    // Validate inputs
    if (killerSpawnPoints.empty())
    {
        result.validationMessage = "ERROR: No killer spawn points available";
        return result;
    }
    if (survivorSpawnPoints.size() < 4)
    {
        result.validationMessage = "ERROR: Insufficient survivor spawn points (need 4+)";
        return result;
    }

    // Step 1: Select killer spawn
    SpawnPoint killerSpawn = SelectKillerSpawn(killerSpawnPoints, generators, offerings, rng);
    result.killerSpawn = killerSpawn.position;

    // Step 2: Select survivor spawns based on mode
    std::vector<SpawnPoint> survivorSpawns = SelectSurvivorSpawns(
        survivorSpawnPoints,
        killerSpawn,
        generators,
        offerings,
        rng
    );

    // Fill result (only use first 4 if we got more)
    for (size_t i = 0; i < 4 && i < survivorSpawns.size(); ++i)
    {
        result.survivorSpawns[i] = survivorSpawns[i].position;
    }

    // Step 3: Calculate validation metrics
    float minSurvivorKillerDist = std::numeric_limits<float>::max();
    float maxSurvivorClusterRadius = 0.0F;
    std::vector<float> interSurvivorDistances;

    for (size_t i = 0; i < 4; ++i)
    {
        const glm::vec3& survPos = result.survivorSpawns[i];
        
        // Distance to killer
        float distToKiller = Distance(survPos, result.killerSpawn);
        minSurvivorKillerDist = std::min(minSurvivorKillerDist, distToKiller);

        // Inter-survivor distances
        for (size_t j = i + 1; j < 4; ++j)
        {
            float dist = Distance(survPos, result.survivorSpawns[j]);
            interSurvivorDistances.push_back(dist);
        }

        // Floor counting
        if (i == 0 || survivorSpawns[i].floorId == survivorSpawns[0].floorId)
        {
            result.survivorsOnSameFloor++;
        }
    }

    // Cluster radius: max distance from cluster centroid
    glm::vec3 centroid(0.0F);
    for (const auto& pos : result.survivorSpawns)
    {
        centroid += pos;
    }
    centroid /= 4.0F;
    
    for (const auto& pos : result.survivorSpawns)
    {
        maxSurvivorClusterRadius = std::max(maxSurvivorClusterRadius, Distance(pos, centroid));
    }

    // Average inter-survivor distance
    if (!interSurvivorDistances.empty())
    {
        float sum = std::accumulate(interSurvivorDistances.begin(), interSurvivorDistances.end(), 0.0F);
        result.averageInterSurvivorDistance = sum / static_cast<float>(interSurvivorDistances.size());
    }

    result.minSurvivorKillerDistance = minSurvivorKillerDist;
    result.maxSurvivorClusterRadius = maxSurvivorClusterRadius;

    // Final validation
    std::string errorMsg;
    if (ValidateSpawn(result, errorMsg))
    {
        result.validationMessage = "OK";
    }
    else
    {
        result.validationMessage = "WARNING: " + errorMsg;
    }

    return result;
}

bool SpawnCalculator::ValidateSpawn(const SpawnResult& result, std::string& errorMessage) const
{
    // Check minimum distance to killer
    if (result.minSurvivorKillerDistance < SpawnConstants::MIN_SURVIVOR_KILLER_DISTANCE)
    {
        errorMessage = "Survivor spawned too close to killer (" +
            std::to_string(result.minSurvivorKillerDistance) + "m < " +
            std::to_string(SpawnConstants::MIN_SURVIVOR_KILLER_DISTANCE) + "m)";
        return false;
    }

    // Check for overlapping survivors
    for (size_t i = 0; i < 4; ++i)
    {
        for (size_t j = i + 1; j < 4; ++j)
        {
            float dist = Distance(result.survivorSpawns[i], result.survivorSpawns[j]);
            if (dist < SpawnConstants::MIN_INTER_SURVIVOR_DISTANCE)
            {
                errorMessage = "Survivors spawned too close to each other (" +
                    std::to_string(dist) + "m)";
                return false;
            }
        }
    }

    return true;
}

MapBounds SpawnCalculator::CalculateMapBounds(
    const std::vector<SpawnPoint>& spawnPoints
)
{
    if (spawnPoints.empty())
    {
        return MapBounds{};
    }

    glm::vec3 minPos = spawnPoints[0].position;
    glm::vec3 maxPos = spawnPoints[0].position;

    for (const auto& spawn : spawnPoints)
    {
        minPos.x = std::min(minPos.x, spawn.position.x);
        minPos.z = std::min(minPos.z, spawn.position.z);
        maxPos.x = std::max(maxPos.x, spawn.position.x);
        maxPos.z = std::max(maxPos.z, spawn.position.z);
    }

    MapBounds bounds;
    bounds.center = (minPos + maxPos) * 0.5F;
    
    float maxDist = 0.0F;
    for (const auto& spawn : spawnPoints)
    {
        float dist = Distance2D(spawn.position, bounds.center);
        maxDist = std::max(maxDist, dist);
    }
    bounds.maxDistanceFromCenter = maxDist;

    return bounds;
}

// ============================================================================
// Killer Spawn Selection
// ============================================================================

SpawnPoint SpawnCalculator::SelectKillerSpawn(
    const std::vector<SpawnPoint>& killerSpawns,
    const std::vector<GeneratorLocation>& generators,
    const SpawnOfferings& offerings,
    std::mt19937& rng
) const
{
    // Calculate map bounds
    MapBounds bounds = CalculateMapBounds(killerSpawns);
    
    // Filter spawns based on mode and constraints
    std::vector<SpawnPoint> candidateSpawns = FilterKillerSpawns(killerSpawns, bounds, offerings);

    if (candidateSpawns.empty())
    {
        // Fallback: use all spawns
        candidateSpawns = killerSpawns;
    }

    // Weight selection by quality and mode
    std::vector<float> weights;
    weights.reserve(candidateSpawns.size());

    for (const auto& spawn : candidateSpawns)
    {
        float weight = spawn.quality;
        
        // Apply mode-based modifiers
        switch (offerings.killerMode)
        {
            case KillerSpawnMode::Edge:
                // Prefer edge spawns
                if (!spawn.isMapCenter)
                {
                    weight *= 2.0F;
                }
                break;
            
            case KillerSpawnMode::Central:
                // Prefer central spawns
                if (spawn.isMapCenter)
                {
                    weight *= 2.0F;
                }
                break;
            
            case KillerSpawnMode::Standard:
            default:
                // Natural bias away from center on small maps (Patch 9.0.0)
                if (bounds.maxDistanceFromCenter < SpawnConstants::MAX_MAP_SIZE_FOR_CENTER_RULE)
                {
                    float distFromCenter = Distance2D(spawn.position, bounds.center);
                    if (distFromCenter > SpawnConstants::KILLER_CENTER_RADIUS_THRESHOLD)
                    {
                        weight *= (1.0F + SpawnConstants::PREFERRED_KILLER_EDGE_BIAS);
                    }
                    else if (spawn.isMapCenter)
                    {
                        weight *= 0.1F; // Strongly discourage center on small maps
                    }
                }
                break;
        }

        // Consider generator proximity (for early game pressure)
        if (!generators.empty())
        {
            float minGenDist = std::numeric_limits<float>::max();
            for (const auto& gen : generators)
            {
                float dist = Distance(spawn.position, gen.position);
                minGenDist = std::min(minGenDist, dist);
            }
            // Prefer spawns with reasonable generator access (not too far, not too close)
            // Sweet spot around 15-30m
            if (minGenDist >= 15.0F && minGenDist <= 30.0F)
            {
                weight *= 1.2F;
            }
        }

        weights.push_back(weight);
    }

    // Weighted random selection
    std::discrete_distribution<size_t> dist(weights.begin(), weights.end());
    size_t selectedIndex = dist(rng);
    
    return candidateSpawns[selectedIndex];
}

std::vector<SpawnPoint> SpawnCalculator::FilterKillerSpawns(
    const std::vector<SpawnPoint>& killerSpawns,
    const MapBounds& bounds,
    const SpawnOfferings& offerings
) const
{
    std::vector<SpawnPoint> filtered;

    // For small maps with standard mode, filter out center spawns (Patch 9.0.0)
    bool isSmallMap = bounds.maxDistanceFromCenter < SpawnConstants::MAX_MAP_SIZE_FOR_CENTER_RULE;
    bool applyCenterRule = isSmallMap && offerings.killerMode == KillerSpawnMode::Standard;

    for (const auto& spawn : killerSpawns)
    {
        if (applyCenterRule && spawn.isMapCenter)
        {
            continue; // Skip center spawns on small maps
        }
        filtered.push_back(spawn);
    }

    return filtered;
}

// ============================================================================
// Survivor Spawn Selection
// ============================================================================

std::vector<SpawnPoint> SpawnCalculator::SelectSurvivorSpawns(
    const std::vector<SpawnPoint>& survivorSpawns,
    const SpawnPoint& killerSpawn,
    const std::vector<GeneratorLocation>& generators,
    const SpawnOfferings& offerings,
    std::mt19937& rng
) const
{
    // Apply shroud of vanishing (killer counters survivor offerings)
    SpawnOfferings effectiveOfferings = offerings;
    if (offerings.shroudOfVanishingActive)
    {
        effectiveOfferings.survivorMode = SurvivorSpawnMode::Clustered;
        effectiveOfferings.vigoShroudOwner = -1;
    }

    // Filter spawns by minimum distance to killer (32m rule - BHVR confirmed)
    std::vector<SpawnPoint> candidateSpawns = FilterSurvivorSpawnsByKillerDistance(
        survivorSpawns,
        killerSpawn
    );

    if (candidateSpawns.size() < 4)
    {
        // Not enough valid spawns - use all candidates as fallback
        candidateSpawns = survivorSpawns;
    }

    // Select based on spawn mode
    std::vector<SpawnPoint> selectedSpawns;

    switch (effectiveOfferings.survivorMode)
    {
        case SurvivorSpawnMode::Clustered:
            selectedSpawns = SelectClusteredSpawns(candidateSpawns, killerSpawn, generators, rng);
            break;
        
        case SurvivorSpawnMode::Split:
            selectedSpawns = SelectSplitSpawns(candidateSpawns, killerSpawn, rng);
            break;
        
        case SurvivorSpawnMode::SemiClustered:
            selectedSpawns = SelectSemiClusteredSpawns(candidateSpawns, killerSpawn, generators, rng);
            break;
        
        case SurvivorSpawnMode::Spread:
            selectedSpawns = SelectSpreadSpawns(candidateSpawns, killerSpawn, generators, rng);
            break;
    }

    // Ensure we have exactly 4 spawns
    if (selectedSpawns.size() > 4)
    {
        selectedSpawns.resize(4);
    }
    else if (selectedSpawns.size() < 4)
    {
        // Fill remaining with random candidates
        while (selectedSpawns.size() < 4 && !candidateSpawns.empty())
        {
            std::uniform_int_distribution<size_t> dist(0, candidateSpawns.size() - 1);
            selectedSpawns.push_back(candidateSpawns[dist(rng)]);
        }
    }

    return selectedSpawns;
}

std::vector<SpawnPoint> SpawnCalculator::FilterSurvivorSpawnsByKillerDistance(
    const std::vector<SpawnPoint>& survivorSpawns,
    const SpawnPoint& killerSpawn
) const
{
    std::vector<SpawnPoint> filtered;

    for (const auto& spawn : survivorSpawns)
    {
        float dist = Distance(spawn.position, killerSpawn.position);
        if (dist >= SpawnConstants::MIN_SURVIVOR_KILLER_DISTANCE)
        {
            filtered.push_back(spawn);
        }
    }

    return filtered;
}

// ============================================================================
// Clustering Mode Implementations
// ============================================================================

std::vector<SpawnPoint> SpawnCalculator::SelectClusteredSpawns(
    const std::vector<SpawnPoint>& candidateSpawns,
    const SpawnPoint& killerSpawn,
    const std::vector<GeneratorLocation>& generators,
    std::mt19937& rng
) const
{
    // Default post-9.0.0 behavior: all survivors within 12m cluster
    std::vector<SpawnPoint> result;

    // Choose cluster center based on generators and killer position
    SpawnPoint clusterCenter = FindClusterCenter(candidateSpawns, killerSpawn, generators, rng);

    // Find 4 points within cluster radius
    std::vector<SpawnPoint> clustered = FindPointsWithinRadius(
        candidateSpawns,
        clusterCenter.position,
        SpawnConstants::DEFAULT_SURVIVOR_CLUSTER_RADIUS,
        4,
        true, // Prefer same floor
        rng
    );

    // If we couldn't find enough in radius, expand search
    if (clustered.size() < 4)
    {
        clustered = FindPointsWithinRadius(
            candidateSpawns,
            clusterCenter.position,
            SpawnConstants::DEFAULT_SURVIVOR_CLUSTER_RADIUS * 2.0F,
            4,
            true,
            rng
        );
    }

    return clustered;
}

std::vector<SpawnPoint> SpawnCalculator::SelectSplitSpawns(
    const std::vector<SpawnPoint>& candidateSpawns,
    const SpawnPoint& killerSpawn,
    std::mt19937& rng
) const
{
    // Shroud of Separation: maximize distances between survivors
    std::vector<SpawnPoint> result;
    std::vector<SpawnPoint> remaining = candidateSpawns;

    // Greedy algorithm: pick furthest from previously selected
    for (int i = 0; i < 4 && !remaining.empty(); ++i)
    {
        SpawnPoint bestSpawn = remaining[0];
        float bestScore = 0.0F;

        for (const auto& candidate : remaining)
        {
            float score = 0.0F;

            // Distance to killer (prefer far)
            float distToKiller = Distance(candidate.position, killerSpawn.position);
            score += distToKiller * 0.3F;

            // Distance from already selected survivors (maximize)
            for (const auto& selected : result)
            {
                float distFromSelected = Distance(candidate.position, selected.position);
                score += distFromSelected * 2.0F;
            }

            if (score > bestScore)
            {
                bestScore = score;
                bestSpawn = candidate;
            }
        }

        result.push_back(bestSpawn);
        
        // Remove selected from remaining
        auto it = std::remove_if(remaining.begin(), remaining.end(),
            [&bestSpawn](const SpawnPoint& s) {
                return s.position == bestSpawn.position;
            });
        remaining.erase(it, remaining.end());
    }

    return result;
}

std::vector<SpawnPoint> SpawnCalculator::SelectSemiClusteredSpawns(
    const std::vector<SpawnPoint>& candidateSpawns,
    const SpawnPoint& killerSpawn,
    const std::vector<GeneratorLocation>& generators,
    std::mt19937& rng
) const
{
    // Vigo's Shroud: one survivor far from killer, others clustered
    std::vector<SpawnPoint> result;

    // Find furthest spawn from killer
    SpawnPoint furthest = FindFurthestSpawn(candidateSpawns, killerSpawn.position);
    result.push_back(furthest);

    // Remove furthest from candidates
    std::vector<SpawnPoint> remaining;
    for (const auto& spawn : candidateSpawns)
    {
        if (spawn.position != furthest.position)
        {
            remaining.push_back(spawn);
        }
    }

    // Cluster the remaining 3 near each other
    SpawnPoint clusterCenter = FindClusterCenter(remaining, killerSpawn, generators, rng);
    std::vector<SpawnPoint> clustered = FindPointsWithinRadius(
        remaining,
        clusterCenter.position,
        SpawnConstants::DEFAULT_SURVIVOR_CLUSTER_RADIUS,
        3,
        true,
        rng
    );

    // Add clustered spawns (pad with random if needed)
    for (size_t i = 0; i < 3 && i < clustered.size(); ++i)
    {
        result.push_back(clustered[i]);
    }

    while (result.size() < 4 && !remaining.empty())
    {
        std::uniform_int_distribution<size_t> dist(0, remaining.size() - 1);
        result.push_back(remaining[dist(rng)]);
    }

    return result;
}

std::vector<SpawnPoint> SpawnCalculator::SelectSpreadSpawns(
    const std::vector<SpawnPoint>& candidateSpawns,
    const SpawnPoint& killerSpawn,
    const std::vector<GeneratorLocation>& generators,
    std::mt19937& rng
) const
{
    // Pre-9.0.0 behavior: more distributed but still generator-linked
    // Survivors spread across different generator regions
    
    std::vector<SpawnPoint> result;
    std::vector<SpawnPoint> remaining = candidateSpawns;

    // Prioritize spawns near different generators
    std::vector<bool> genUsed(generators.size(), false);

    for (int i = 0; i < 4 && !remaining.empty(); ++i)
    {
        SpawnPoint bestSpawn = remaining[0];
        float bestScore = -1.0F;
        int bestGenIndex = -1;

        for (size_t j = 0; j < remaining.size(); ++j)
        {
            const auto& candidate = remaining[j];
            
            // Find closest unused generator
            float minGenDist = std::numeric_limits<float>::max();
            int closestGenIndex = -1;
            
            for (size_t g = 0; g < generators.size(); ++g)
            {
                if (genUsed[g]) continue;

                float dist = Distance(candidate.position, generators[g].position);
                if (dist < minGenDist)
                {
                    minGenDist = dist;
                    closestGenIndex = static_cast<int>(g);
                }
            }

            // Score: prefer close to unused generators, far from killer
            float distToKiller = Distance(candidate.position, killerSpawn.position);
            float score = distToKiller * 0.5F;

            if (closestGenIndex >= 0)
            {
                score += (100.0F - minGenDist) * 2.0F; // Prefer near unused generators
            }
            else
            {
                // All gens used, just pick far from killer
                score = distToKiller;
            }

            if (score > bestScore)
            {
                bestScore = score;
                bestSpawn = candidate;
                bestGenIndex = closestGenIndex;
            }
        }

        result.push_back(bestSpawn);
        
        if (bestGenIndex >= 0)
        {
            genUsed[bestGenIndex] = true;
        }

        // Remove selected from remaining
        auto it = std::remove_if(remaining.begin(), remaining.end(),
            [&bestSpawn](const SpawnPoint& s) {
                return s.position == bestSpawn.position;
            });
        remaining.erase(it, remaining.end());
    }

    return result;
}

// ============================================================================
// Utility Functions
// ============================================================================

SpawnPoint SpawnCalculator::FindClusterCenter(
    const std::vector<SpawnPoint>& candidateSpawns,
    const SpawnPoint& killerSpawn,
    const std::vector<GeneratorLocation>& generators,
    std::mt19937& rng
) const
{
    if (candidateSpawns.empty())
    {
        return SpawnPoint{};
    }

    // DBD heuristic: survivors spawn near "medium-far" generators
    // Not the closest, not the furthest, but in the sweet spot
    
    std::vector<SpawnPoint> preferred;
    std::vector<float> weights;

    for (const auto& spawn : candidateSpawns)
    {
        float distToKiller = Distance(spawn.position, killerSpawn.position);
        
        // Find closest generator
        float minGenDist = std::numeric_limits<float>::max();
        for (const auto& gen : generators)
        {
            float dist = Distance(spawn.position, gen.position);
            minGenDist = std::min(minGenDist, dist);
        }

        // Prefer spawns with moderate distance to killer (20-40m)
        // and close to generators
        float weight = spawn.quality;

        if (distToKiller >= 20.0F && distToKiller <= 40.0F && minGenDist < 15.0F)
        {
            weight *= 2.0F; // Sweet spot
        }
        else if (distToKiller < 20.0F || minGenDist > 25.0F)
        {
            weight *= 0.3F; // Less ideal
        }

        preferred.push_back(spawn);
        weights.push_back(weight);
    }

    // Weighted random selection
    std::discrete_distribution<size_t> dist(weights.begin(), weights.end());
    return preferred[dist(rng)];
}

std::vector<SpawnPoint> SpawnCalculator::FindPointsWithinRadius(
    const std::vector<SpawnPoint>& candidateSpawns,
    const glm::vec3& center,
    float radius,
    int targetCount,
    bool preferSameFloor,
    std::mt19937& rng
) const
{
    struct SpawnScore
    {
        SpawnPoint spawn;
        float score;
    };

    std::vector<SpawnScore> scored;

    for (const auto& spawn : candidateSpawns)
    {
        float dist = Distance(spawn.position, center);
        
        if (dist <= radius)
        {
            float score = spawn.quality;
            
            // Prefer same floor
            if (preferSameFloor)
            {
                float yDiff = std::abs(spawn.position.y - center.y);
                if (yDiff < 1.0F)
                {
                    score *= 2.0F;
                }
            }

            scored.push_back({spawn, score});
        }
    }

    // Sort by score
    std::sort(scored.begin(), scored.end(),
        [](const SpawnScore& a, const SpawnScore& b) {
            return a.score > b.score;
        });

    // Take top targetCount
    std::vector<SpawnPoint> result;
    int count = std::min(targetCount, static_cast<int>(scored.size()));
    
    for (int i = 0; i < count; ++i)
    {
        result.push_back(scored[i].spawn);
    }

    // Add some randomness within top tier
    if (result.size() > 1)
    {
        std::shuffle(result.begin(), result.end(), rng);
    }

    return result;
}

float SpawnCalculator::CalculateSpawnQuality(
    const SpawnPoint& spawn,
    const SpawnPoint& killerSpawn,
    const std::vector<GeneratorLocation>& generators
) const
{
    // Base quality from spawn point definition
    float quality = spawn.quality;

    // Check for obstacles, line of sight, etc. would go here
    // For now, just use base quality

    return quality;
}

SpawnPoint SpawnCalculator::FindFurthestSpawn(
    const std::vector<SpawnPoint>& spawns,
    const glm::vec3& referencePoint
) const
{
    if (spawns.empty())
    {
        return SpawnPoint{};
    }

    SpawnPoint furthest = spawns[0];
    float maxDist = Distance(spawns[0].position, referencePoint);

    for (size_t i = 1; i < spawns.size(); ++i)
    {
        float dist = Distance(spawns[i].position, referencePoint);
        if (dist > maxDist)
        {
            maxDist = dist;
            furthest = spawns[i];
        }
    }

    return furthest;
}

SpawnPoint SpawnCalculator::FindClosestSpawn(
    const std::vector<SpawnPoint>& spawns,
    const glm::vec3& referencePoint
) const
{
    if (spawns.empty())
    {
        return SpawnPoint{};
    }

    SpawnPoint closest = spawns[0];
    float minDist = Distance(spawns[0].position, referencePoint);

    for (size_t i = 1; i < spawns.size(); ++i)
    {
        float dist = Distance(spawns[i].position, referencePoint);
        if (dist < minDist)
        {
            minDist = dist;
            closest = spawns[i];
        }
    }

    return closest;
}

// ============================================================================
// SpawnPointGenerator Implementation
// ============================================================================

std::vector<SpawnPoint> SpawnPointGenerator::GenerateKillerSpawns(
    const std::vector<glm::vec3>& tileCenters,
    const MapBounds& bounds
)
{
    std::vector<SpawnPoint> spawns;

    // Generate spawns at tile centers, marking central ones
    for (size_t i = 0; i < tileCenters.size(); ++i)
    {
        SpawnPoint spawn;
        spawn.position = tileCenters[i];
        spawn.tileId = static_cast<int>(i);
        spawn.floorId = 0;
        spawn.quality = 1.0F;
        
        // Mark as center if within threshold radius
        float distFromCenter = std::sqrt(
            std::pow(tileCenters[i].x - bounds.center.x, 2) +
            std::pow(tileCenters[i].z - bounds.center.z, 2)
        );
        spawn.isMapCenter = (distFromCenter < bounds.maxDistanceFromCenter * 0.3F);

        spawns.push_back(spawn);
    }

    // Ensure we have some edge spawns
    if (spawns.size() > 4)
    {
        // Keep 2-3 center spawns and mark rest as potential edge spawns
        int centerCount = 0;
        for (auto& spawn : spawns)
        {
            if (spawn.isMapCenter)
            {
                centerCount++;
                if (centerCount > 2)
                {
                    spawn.isMapCenter = false;
                }
            }
        }
    }

    return spawns;
}

std::vector<SpawnPoint> SpawnPointGenerator::GenerateSurvivorSpawns(
    const std::vector<glm::vec3>& tileCenters,
    const std::vector<GeneratorLocation>& generators,
    const MapBounds& bounds
)
{
    std::vector<SpawnPoint> spawns;

    // Generate spawns near generators
    for (size_t i = 0; i < tileCenters.size(); ++i)
    {
        SpawnPoint spawn;
        spawn.position = tileCenters[i];
        spawn.tileId = static_cast<int>(i);
        spawn.floorId = 0;
        spawn.quality = 1.0F;

        // Check if near generator
        for (const auto& gen : generators)
        {
            float dist = std::sqrt(
                std::pow(tileCenters[i].x - gen.position.x, 2) +
                std::pow(tileCenters[i].y - gen.position.y, 2) +
                std::pow(tileCenters[i].z - gen.position.z, 2)
            );
            if (dist < 10.0F)
            {
                spawn.isNearGenerator = true;
                spawn.quality = 1.5F; // Prefer spawns near generators
                break;
            }
        }

        spawns.push_back(spawn);
    }

    return spawns;
}

} // namespace game::gameplay
