#include "engine/physics/ColliderGen_WallBoxes.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <limits>
#include <queue>
#include <sstream>
#include <unordered_map>

#include <glm/geometric.hpp>
#include <nlohmann/json.hpp>

namespace engine::physics
{

namespace
{
using json = nlohmann::json;

// 2D point/hash for flood fill
struct GridCoord
{
    int x, z;
    bool operator==(const GridCoord& o) const { return x == o.x && z == o.z; }
};

struct GridCoordHash
{
    std::size_t operator()(const GridCoord& c) const
    {
        return static_cast<std::size_t>(c.x) * 73856093 ^ static_cast<std::size_t>(c.z) * 19349663;
    }
};

constexpr float kGeomEpsilon = 1.0e-6f;

bool PointInAabb2D(const glm::vec2& p, const glm::vec2& minBounds, const glm::vec2& maxBounds)
{
    return p.x >= minBounds.x - kGeomEpsilon && p.x <= maxBounds.x + kGeomEpsilon &&
           p.y >= minBounds.y - kGeomEpsilon && p.y <= maxBounds.y + kGeomEpsilon;
}

float Cross2D(const glm::vec2& a, const glm::vec2& b, const glm::vec2& c)
{
    const glm::vec2 ab = b - a;
    const glm::vec2 ac = c - a;
    return ab.x * ac.y - ab.y * ac.x;
}

bool OnSegment2D(const glm::vec2& a, const glm::vec2& b, const glm::vec2& p)
{
    if (std::abs(Cross2D(a, b, p)) > 1.0e-5f)
    {
        return false;
    }
    return p.x >= std::min(a.x, b.x) - kGeomEpsilon && p.x <= std::max(a.x, b.x) + kGeomEpsilon &&
           p.y >= std::min(a.y, b.y) - kGeomEpsilon && p.y <= std::max(a.y, b.y) + kGeomEpsilon;
}

bool SegmentsIntersect2D(const glm::vec2& a0, const glm::vec2& a1, const glm::vec2& b0, const glm::vec2& b1)
{
    const float c1 = Cross2D(a0, a1, b0);
    const float c2 = Cross2D(a0, a1, b1);
    const float c3 = Cross2D(b0, b1, a0);
    const float c4 = Cross2D(b0, b1, a1);

    const bool properHit = ((c1 > 0.0f && c2 < 0.0f) || (c1 < 0.0f && c2 > 0.0f)) &&
                           ((c3 > 0.0f && c4 < 0.0f) || (c3 < 0.0f && c4 > 0.0f));
    if (properHit)
    {
        return true;
    }

    if (std::abs(c1) <= 1.0e-5f && OnSegment2D(a0, a1, b0))
    {
        return true;
    }
    if (std::abs(c2) <= 1.0e-5f && OnSegment2D(a0, a1, b1))
    {
        return true;
    }
    if (std::abs(c3) <= 1.0e-5f && OnSegment2D(b0, b1, a0))
    {
        return true;
    }
    if (std::abs(c4) <= 1.0e-5f && OnSegment2D(b0, b1, a1))
    {
        return true;
    }
    return false;
}

} // namespace

WallColliderResult ColliderGen_WallBoxes::Generate(
    const std::vector<glm::vec3>& positions,
    const std::vector<std::uint32_t>& indices,
    const WallColliderConfig& config)
{
    WallColliderResult result;

    if (positions.empty() || indices.empty() || indices.size() % 3 != 0)
    {
        result.error = "Invalid mesh data";
        return result;
    }

    // 1. Compute mesh bounds in XZ plane and Y
    glm::vec2 minxz(std::numeric_limits<float>::max());
    glm::vec2 maxxz(std::numeric_limits<float>::lowest());
    float minY = std::numeric_limits<float>::max();
    float maxY = std::numeric_limits<float>::lowest();

    for (const auto& p : positions)
    {
        minxz.x = std::min(minxz.x, p.x);
        minxz.y = std::min(minxz.y, p.z);
        maxxz.x = std::max(maxxz.x, p.x);
        maxxz.y = std::max(maxxz.y, p.z);
        minY = std::min(minY, p.y);
        maxY = std::max(maxY, p.y);
    }

    const float cellSize = std::max(0.01f, config.cellSize);
    const int gridW = std::max(1, static_cast<int>(std::ceil((maxxz.x - minxz.x) / cellSize)));
    const int gridH = std::max(1, static_cast<int>(std::ceil((maxxz.y - minxz.y) / cellSize)));

    // 2. Build occupancy grid
    std::vector<bool> grid(static_cast<std::size_t>(gridW) * gridH, false);
    BuildOccupancyGrid(positions, indices, grid, gridW, gridH, minxz, cellSize);

    const int initialFilled = static_cast<int>(std::count(grid.begin(), grid.end(), true));
    if (initialFilled == 0)
    {
        result.error = "Empty occupancy grid";
        return result;
    }

    // 3. Cleanup
    if (config.cleanup)
    {
        CleanupGrid(grid, gridW, gridH);
    }
    RemoveSmallIslands(grid, gridW, gridH, config.minIslandCells);
    const std::vector<bool> cleanedGrid = grid;

    // 4. Rectangle decomposition
    auto rectangles = DecomposeRectangles(grid, gridW, gridH, config.maxBoxes);
    if (rectangles.empty())
    {
        result.error = "Rectangle decomposition failed";
        return result;
    }

    // 5. Convert to 3D colliders
    const float yCenter = (minY + maxY) * 0.5f;
    const float yHalf = std::max((maxY - minY) * 0.5f, 0.05f);

    float totalColliderVolume = 0.0f;
    for (const auto& rect : rectangles)
    {
        int x0 = rect.x, z0 = rect.y, x1 = rect.z, z1 = rect.w;

        float cminX = minxz.x + x0 * cellSize;
        float cmaxX = minxz.x + (x1 + 1) * cellSize;
        float cminZ = minxz.y + z0 * cellSize;
        float cmaxZ = minxz.y + (z1 + 1) * cellSize;

        WallBoxCollider box;
        box.center.x = (cminX + cmaxX) * 0.5f;
        box.center.y = yCenter;
        box.center.z = (cminZ + cmaxZ) * 0.5f;

        box.halfExtents.x = (cmaxX - cminX) * 0.5f + config.padXZ;
        box.halfExtents.y = yHalf;
        box.halfExtents.z = (cmaxZ - cminZ) * 0.5f + config.padXZ;

        result.boxes.push_back(box);
        totalColliderVolume +=
            (2.0f * box.halfExtents.x) *
            (2.0f * box.halfExtents.y) *
            (2.0f * box.halfExtents.z);
    }

    // 6. Calculate coverage
    result.coverage = CalculateCoverage(cleanedGrid, rectangles, gridW, gridH);

    // 7. Validate
    const float meshAABBVolume =
        (maxxz.x - minxz.x) *
        (2.0f * yHalf) *
        (maxxz.y - minxz.y);

    result.volumeRatio = meshAABBVolume > 0.0f ?
        totalColliderVolume / meshAABBVolume : 0.0f;

    // Check if result is valid
    if (result.boxes.empty())
    {
        result.error = "No colliders generated";
        return result;
    }

    if (result.coverage < config.minCoverage)
    {
        result.error = "Coverage too low";
        return result;
    }

    if (result.volumeRatio > 1.0f + config.maxVolumeExcess)
    {
        result.error = "Collider volume too large";
        return result;
    }

    result.valid = true;
    return result;
}

void ColliderGen_WallBoxes::BuildOccupancyGrid(
    const std::vector<glm::vec3>& positions,
    const std::vector<std::uint32_t>& indices,
    std::vector<bool>& grid,
    int gridW, int gridH,
    const glm::vec2& gridMin,
    float cellSize)
{
    const int triCount = static_cast<int>(indices.size()) / 3;

    for (int t = 0; t < triCount; ++t)
    {
        const std::uint32_t i0 = indices[t * 3 + 0];
        const std::uint32_t i1 = indices[t * 3 + 1];
        const std::uint32_t i2 = indices[t * 3 + 2];
        if (i0 >= positions.size() || i1 >= positions.size() || i2 >= positions.size())
        {
            continue;
        }

        const glm::vec3& v0 = positions[i0];
        const glm::vec3& v1 = positions[i1];
        const glm::vec3& v2 = positions[i2];

        // Project to XZ plane
        glm::vec2 a(v0.x, v0.z);
        glm::vec2 b(v1.x, v1.z);
        glm::vec2 c(v2.x, v2.z);

        // Find triangle AABB in grid coords
        int minX = static_cast<int>(std::floor((std::min({a.x, b.x, c.x}) - gridMin.x) / cellSize));
        int maxX = static_cast<int>(std::floor((std::max({a.x, b.x, c.x}) - gridMin.x) / cellSize));
        int minZ = static_cast<int>(std::floor((std::min({a.y, b.y, c.y}) - gridMin.y) / cellSize));
        int maxZ = static_cast<int>(std::floor((std::max({a.y, b.y, c.y}) - gridMin.y) / cellSize));

        // Clamp to grid bounds
        minX = std::max(0, minX);
        maxX = std::min(gridW - 1, maxX);
        minZ = std::max(0, minZ);
        maxZ = std::min(gridH - 1, maxZ);

        // Mark cells that overlap triangle
        for (int gz = minZ; gz <= maxZ; ++gz)
        {
            for (int gx = minX; gx <= maxX; ++gx)
            {
                const float cellMinX = gridMin.x + static_cast<float>(gx) * cellSize;
                const float cellMinZ = gridMin.y + static_cast<float>(gz) * cellSize;
                const glm::vec2 cellMin{cellMinX, cellMinZ};
                const glm::vec2 cellMax{cellMinX + cellSize, cellMinZ + cellSize};

                const glm::vec2 triMin{
                    std::min({a.x, b.x, c.x}),
                    std::min({a.y, b.y, c.y}),
                };
                const glm::vec2 triMax{
                    std::max({a.x, b.x, c.x}),
                    std::max({a.y, b.y, c.y}),
                };

                const bool overlapAabb = !(triMax.x < cellMin.x || triMin.x > cellMax.x ||
                                           triMax.y < cellMin.y || triMin.y > cellMax.y);
                if (!overlapAabb)
                {
                    continue;
                }

                bool hit = false;
                if (PointInAabb2D(a, cellMin, cellMax) || PointInAabb2D(b, cellMin, cellMax) || PointInAabb2D(c, cellMin, cellMax))
                {
                    hit = true;
                }

                if (!hit)
                {
                    const glm::vec2 cellCorners[4] = {
                        cellMin,
                        glm::vec2{cellMax.x, cellMin.y},
                        cellMax,
                        glm::vec2{cellMin.x, cellMax.y},
                    };
                    for (const glm::vec2& corner : cellCorners)
                    {
                        if (PointInTriangle2D(corner, a, b, c))
                        {
                            hit = true;
                            break;
                        }
                    }
                }

                if (!hit)
                {
                    const glm::vec2 triEdges[3][2] = {
                        {a, b},
                        {b, c},
                        {c, a},
                    };
                    const glm::vec2 cellEdges[4][2] = {
                        {cellMin, glm::vec2{cellMax.x, cellMin.y}},
                        {glm::vec2{cellMax.x, cellMin.y}, cellMax},
                        {cellMax, glm::vec2{cellMin.x, cellMax.y}},
                        {glm::vec2{cellMin.x, cellMax.y}, cellMin},
                    };
                    for (const auto& triEdge : triEdges)
                    {
                        for (const auto& cellEdge : cellEdges)
                        {
                            if (SegmentsIntersect2D(triEdge[0], triEdge[1], cellEdge[0], cellEdge[1]))
                            {
                                hit = true;
                                break;
                            }
                        }
                        if (hit)
                        {
                            break;
                        }
                    }
                }

                if (hit)
                {
                    grid[static_cast<std::size_t>(gz * gridW + gx)] = true;
                }
            }
        }
    }
}

void ColliderGen_WallBoxes::CleanupGrid(std::vector<bool>& grid, int gridW, int gridH)
{
    if (grid.empty()) return;

    // Single smoothing pass: fill tiny holes, remove isolated noise.
    auto countFilledNeighbors = [gridW, gridH](const std::vector<bool>& src, int x, int z) {
        int count = 0;
        for (int dz = -1; dz <= 1; ++dz)
        {
            for (int dx = -1; dx <= 1; ++dx)
            {
                if (dx == 0 && dz == 0)
                {
                    continue;
                }
                const int nx = x + dx;
                const int nz = z + dz;
                if (nx < 0 || nx >= gridW || nz < 0 || nz >= gridH)
                {
                    continue;
                }
                if (src[nz * gridW + nx])
                {
                    ++count;
                }
            }
        }
        return count;
    };

    std::vector<bool> filtered = grid;
    for (int z = 0; z < gridH; ++z)
    {
        for (int x = 0; x < gridW; ++x)
        {
            const int idx = z * gridW + x;
            const int neighborCount = countFilledNeighbors(grid, x, z);
            if (!grid[idx] && neighborCount >= 5)
            {
                filtered[idx] = true;
            }
            else if (grid[idx] && neighborCount <= 1)
            {
                filtered[idx] = false;
            }
        }
    }

    grid.swap(filtered);
}

void ColliderGen_WallBoxes::RemoveSmallIslands(
    std::vector<bool>& grid,
    int gridW,
    int gridH,
    int minCells)
{
    if (grid.empty()) return;

    std::vector<bool> visited(grid.size(), false);
    std::vector<std::vector<GridCoord>> islands;

    // Flood fill to find all islands
    for (int z = 0; z < gridH; ++z)
    {
        for (int x = 0; x < gridW; ++x)
        {
            const int idx = z * gridW + x;
            if (grid[idx] && !visited[idx])
            {
                // Found new island - flood fill
                std::vector<GridCoord> island;
                std::queue<GridCoord> queue;
                queue.push({x, z});
                visited[idx] = true;

                while (!queue.empty())
                {
                    auto coord = queue.front();
                    queue.pop();
                    island.push_back(coord);

                    // Check 4 neighbors
                    const int dx[] = {0, 0, -1, 1};
                    const int dz[] = {-1, 1, 0, 0};
                    for (int i = 0; i < 4; ++i)
                    {
                        int nx = coord.x + dx[i];
                        int nz = coord.z + dz[i];
                        if (nx >= 0 && nx < gridW && nz >= 0 && nz < gridH)
                        {
                            int nidx = nz * gridW + nx;
                            if (grid[nidx] && !visited[nidx])
                            {
                                visited[nidx] = true;
                                queue.push({nx, nz});
                            }
                        }
                    }
                }

                islands.push_back(std::move(island));
            }
        }
    }

    // Remove cells from small islands
    std::size_t largestIslandIndex = 0;
    std::size_t largestIslandSize = 0;
    for (std::size_t i = 0; i < islands.size(); ++i)
    {
        if (islands[i].size() > largestIslandSize)
        {
            largestIslandSize = islands[i].size();
            largestIslandIndex = i;
        }
    }

    for (std::size_t i = 0; i < islands.size(); ++i)
    {
        const auto& island = islands[i];
        if (i != largestIslandIndex && static_cast<int>(island.size()) < minCells)
        {
            for (const auto& coord : island)
            {
                grid[coord.z * gridW + coord.x] = false;
            }
        }
    }
}

std::vector<glm::ivec4> ColliderGen_WallBoxes::DecomposeRectangles(
    std::vector<bool>& grid,
    int gridW,
    int gridH,
    int maxRects)
{
    std::vector<glm::ivec4> rectangles;

    // Count remaining filled cells
    auto countFilled = [&]() {
        int count = 0;
        for (bool v : grid) if (v) ++count;
        return count;
    };

    while (rectangles.size() < static_cast<std::size_t>(maxRects))
    {
        int filled = countFilled();
        if (filled == 0) break;

        auto rect = FindLargestRectangle(grid, gridW, gridH);
        if (rect.z < rect.x || rect.w < rect.y)
        {
            // Fallback for degenerate/very thin residual occupancy.
            bool foundCell = false;
            for (int z = 0; z < gridH && !foundCell; ++z)
            {
                for (int x = 0; x < gridW; ++x)
                {
                    if (grid[z * gridW + x])
                    {
                        rect = glm::ivec4{x, z, x, z};
                        foundCell = true;
                        break;
                    }
                }
            }
            if (!foundCell)
            {
                break;
            }
        }

        rectangles.push_back(rect);

        // Clear cells covered by this rectangle
        for (int z = rect.y; z <= rect.w; ++z)
        {
            for (int x = rect.x; x <= rect.z; ++x)
            {
                grid[z * gridW + x] = false;
            }
        }
    }

    return rectangles;
}

glm::ivec4 ColliderGen_WallBoxes::FindLargestRectangle(
    const std::vector<bool>& grid,
    int gridW,
    int gridH)
{
    // Use histogram method: for each row, compute height of consecutive filled cells
    // Then find max rectangle in histogram

    std::vector<int> heights(gridW, 0);
    int bestArea = 0;
    glm::ivec4 best(0, 0, -1, -1);  // x0, z0, x1, z1

    for (int z = 0; z < gridH; ++z)
    {
        // Update heights for this row
        for (int x = 0; x < gridW; ++x)
        {
            if (grid[z * gridW + x])
            {
                heights[x]++;
            }
            else
            {
                heights[x] = 0;
            }
        }

        // Find max rectangle in current histogram
        // Use stack-based algorithm
        std::vector<int> stack;
        for (int x = 0; x <= gridW; ++x)
        {
            int h = (x == gridW) ? 0 : heights[x];
            while (!stack.empty() && heights[stack.back()] > h)
            {
                int height = heights[stack.back()];
                stack.pop_back();
                int width = stack.empty() ? x : x - stack.back() - 1;
                int area = height * width;

                if (area > bestArea)
                {
                    bestArea = area;
                    int startX = stack.empty() ? 0 : stack.back() + 1;
                    best.x = startX;
                    best.z = x - 1;
                    best.y = z - height + 1;
                    best.w = z;
                }
            }
            stack.push_back(x);
        }
    }

    return best;
}

bool ColliderGen_WallBoxes::PointInTriangle2D(
    const glm::vec2& p,
    const glm::vec2& a,
    const glm::vec2& b,
    const glm::vec2& c)
{
    // Barycentric coordinates method
    glm::vec2 v0 = c - a;
    glm::vec2 v1 = b - a;
    glm::vec2 v2 = p - a;

    float dot00 = glm::dot(v0, v0);
    float dot01 = glm::dot(v0, v1);
    float dot02 = glm::dot(v0, v2);
    float dot11 = glm::dot(v1, v1);
    float dot12 = glm::dot(v1, v2);

    float denom = dot00 * dot11 - dot01 * dot01;
    if (std::abs(denom) < 1e-8f) return false;

    float u = (dot11 * dot02 - dot01 * dot12) / denom;
    float v = (dot00 * dot12 - dot01 * dot02) / denom;

    return (u >= -kGeomEpsilon) && (v >= -kGeomEpsilon) && (u + v <= 1.0f + kGeomEpsilon);
}

float ColliderGen_WallBoxes::CalculateCoverage(
    const std::vector<bool>& originalGrid,
    const std::vector<glm::ivec4>& rectangles,
    int gridW,
    int gridH)
{
    int totalFilled = 0;
    int covered = 0;

    std::vector<bool> coverage(originalGrid.size(), false);

    // Mark cells covered by rectangles
    for (const auto& rect : rectangles)
    {
        for (int z = rect.y; z <= rect.w; ++z)
        {
            for (int x = rect.x; x <= rect.z; ++x)
            {
                if (z >= 0 && z < gridH && x >= 0 && x < gridW)
                {
                    coverage[z * gridW + x] = true;
                }
            }
        }
    }

    // Count coverage
    for (int i = 0; i < static_cast<int>(originalGrid.size()); ++i)
    {
        if (originalGrid[i])
        {
            ++totalFilled;
            if (coverage[i]) ++covered;
        }
    }

    return totalFilled > 0 ? static_cast<float>(covered) / static_cast<float>(totalFilled) : 0.0f;
}

std::string ColliderGen_WallBoxes::ComputeMeshHash(
    const std::vector<glm::vec3>& positions,
    const std::vector<std::uint32_t>& indices)
{
    // Simple hash combining position and index data
    std::size_t hash = 0;

    // Hash positions
    for (const auto& p : positions)
    {
        hash ^= std::hash<float>{}(p.x) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
        hash ^= std::hash<float>{}(p.y) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
        hash ^= std::hash<float>{}(p.z) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
    }

    // Hash indices
    for (const auto& idx : indices)
    {
        hash ^= std::hash<std::uint32_t>{}(idx) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
    }

    std::ostringstream oss;
    oss << std::hex << hash;
    return oss.str();
}

std::filesystem::path ColliderGen_WallBoxes::GetCachePath(const std::filesystem::path& meshPath)
{
    return meshPath.string() + ".colliders.json";
}

std::optional<WallColliderCache> ColliderGen_WallBoxes::LoadCache(const std::filesystem::path& cachePath)
{
    std::ifstream file(cachePath);
    if (!file.is_open()) return std::nullopt;

    try
    {
        json j;
        file >> j;

        WallColliderCache cache;
        cache.version = j.value("version", 1);
        cache.meshHash = j.value("meshHash", "");

        // Load config
        if (j.contains("config"))
        {
            auto& cfg = j["config"];
            cache.config.cellSize = cfg.value("cellSize", 0.10f);
            cache.config.maxBoxes = cfg.value("maxBoxes", 4);
            cache.config.padXZ = cfg.value("padXZ", 0.02f);
            cache.config.minIslandCells = cfg.value("minIslandCells", 4);
            cache.config.cleanup = cfg.value("cleanup", true);
        }

        // Load boxes
        if (j.contains("boxes"))
        {
            for (const auto& boxJson : j["boxes"])
            {
                WallBoxCollider box;
                box.center.x = boxJson["center"][0].get<float>();
                box.center.y = boxJson["center"][1].get<float>();
                box.center.z = boxJson["center"][2].get<float>();
                box.halfExtents.x = boxJson["halfExtents"][0].get<float>();
                box.halfExtents.y = boxJson["halfExtents"][1].get<float>();
                box.halfExtents.z = boxJson["halfExtents"][2].get<float>();
                cache.boxes.push_back(box);
            }
        }

        return cache;
    }
    catch (...)
    {
        return std::nullopt;
    }
}

bool ColliderGen_WallBoxes::SaveCache(
    const std::filesystem::path& cachePath,
    const WallColliderCache& cache)
{
    std::ofstream file(cachePath);
    if (!file.is_open()) return false;

    try
    {
        json j;
        j["version"] = cache.version;
        j["meshHash"] = cache.meshHash;

        // Save config
        j["config"] = {
            {"cellSize", cache.config.cellSize},
            {"maxBoxes", cache.config.maxBoxes},
            {"padXZ", cache.config.padXZ},
            {"minIslandCells", cache.config.minIslandCells},
            {"cleanup", cache.config.cleanup}
        };

        // Save boxes
        j["boxes"] = json::array();
        for (const auto& box : cache.boxes)
        {
            j["boxes"].push_back({
                {"center", {box.center.x, box.center.y, box.center.z}},
                {"halfExtents", {box.halfExtents.x, box.halfExtents.y, box.halfExtents.z}}
            });
        }

        file << j.dump(2);
        return true;
    }
    catch (...)
    {
        return false;
    }
}

} // namespace engine::physics
