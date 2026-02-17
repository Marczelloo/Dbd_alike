#pragma once

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <string>
#include <vector>
#include <filesystem>
#include <optional>

namespace engine::physics
{

// Configuration for wall collider generation
struct WallColliderConfig
{
    float cellSize = 0.10f;        // Grid cell size in meters (0.05-0.15)
    int maxBoxes = 4;              // Maximum boxes to generate
    float padXZ = 0.02f;           // Padding on X/Z axes for gameplay
    int minIslandCells = 4;        // Minimum cells for a valid island
    bool cleanup = true;           // Apply morphological cleanup
    float maxVolumeExcess = 0.5f;  // Max allowed volume excess (50%)
    float minCoverage = 0.95f;     // Minimum cell coverage (95%)
};

// A single box collider in local space
struct WallBoxCollider
{
    glm::vec3 center{0.0f};
    glm::vec3 halfExtents{0.0f};
};

// Result of collider generation
struct WallColliderResult
{
    std::vector<WallBoxCollider> boxes;
    float coverage = 0.0f;         // How much of footprint is covered
    float volumeRatio = 0.0f;      // Collider volume / mesh AABB volume
    bool valid = false;
    std::string error;
};

// Cached collider data for persistence
struct WallColliderCache
{
    std::string meshHash;          // Hash of mesh geometry
    WallColliderConfig config;     // Config used for generation
    std::vector<WallBoxCollider> boxes;
    int version = 1;
};

class ColliderGen_WallBoxes
{
public:
    // Main entry point: generate colliders from mesh geometry
    // @param positions: vertex positions (3 floats per vertex)
    // @param indices: triangle indices (3 uints per triangle)
    // @param config: generation configuration
    // @return generated colliders
    static WallColliderResult Generate(
        const std::vector<glm::vec3>& positions,
        const std::vector<std::uint32_t>& indices,
        const WallColliderConfig& config = WallColliderConfig{}
    );

    // Load cached colliders from JSON file
    static std::optional<WallColliderCache> LoadCache(const std::filesystem::path& cachePath);

    // Save colliders to JSON cache file
    static bool SaveCache(
        const std::filesystem::path& cachePath,
        const WallColliderCache& cache
    );

    // Compute hash of mesh geometry for cache validation
    static std::string ComputeMeshHash(
        const std::vector<glm::vec3>& positions,
        const std::vector<std::uint32_t>& indices
    );

    // Get default cache path for a mesh file
    static std::filesystem::path GetCachePath(const std::filesystem::path& meshPath);

private:
    // Internal: build occupancy grid from mesh triangles
    static void BuildOccupancyGrid(
        const std::vector<glm::vec3>& positions,
        const std::vector<std::uint32_t>& indices,
        std::vector<bool>& grid,
        int gridW, int gridH,
        const glm::vec2& gridMin,
        float cellSize
    );

    // Internal: morphological cleanup (close small gaps)
    static void CleanupGrid(std::vector<bool>& grid, int gridW, int gridH);

    // Internal: remove small isolated islands
    static void RemoveSmallIslands(
        std::vector<bool>& grid,
        int gridW, int gridH,
        int minCells
    );

    // Internal: greedy rectangle decomposition
    static std::vector<glm::ivec4> DecomposeRectangles(
        std::vector<bool>& grid,
        int gridW, int gridH,
        int maxRects
    );

    // Internal: find largest rectangle in current grid
    static glm::ivec4 FindLargestRectangle(
        const std::vector<bool>& grid,
        int gridW, int gridH
    );

    // Internal: check if point is inside 2D triangle
    static bool PointInTriangle2D(
        const glm::vec2& p,
        const glm::vec2& a,
        const glm::vec2& b,
        const glm::vec2& c
    );

    // Internal: calculate coverage percentage
    static float CalculateCoverage(
        const std::vector<bool>& originalGrid,
        const std::vector<glm::ivec4>& rectangles,
        int gridW, int gridH
    );
};

} // namespace engine::physics
