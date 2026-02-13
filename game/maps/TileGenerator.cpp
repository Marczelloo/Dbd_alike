#include "game/maps/TileGenerator.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <numeric>
#include <random>
#include <string>
#include <unordered_map>
#include <vector>

#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/trigonometric.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include "game/gameplay/SpawnSystem.hpp"

namespace game::maps
{
namespace
{
constexpr int kGridSize = 12;  // Reduced from 16 (map was too large)
constexpr float kTileSize = 16.0F;
constexpr float kTileLocalMin = 0.0F;
constexpr float kTileLocalMax = 15.0F;
constexpr float kTileCenter = 7.5F;
constexpr float kTileHalfExtent = kTileSize * 0.5F;
constexpr float kWallHalfHeight = 1.0F;
constexpr float kWallThickness = 0.28F;
constexpr float kWorldSnapStep = 0.05F;

enum class TileArchetype
{
    JungleGymLong = 0,
    JungleGymShort = 1,
    LTWalls = 2,
    Shack = 3,
    FourLane = 4,
    FillerA = 5,
    FillerB = 6,
    // --- New v2 loop types ---
    LongWall = 7,       // Single long wall with window
    ShortWall = 8,      // Single short wall with unsafe pallet
    LWallWindow = 9,    // L-shaped walls, window on long side (safe loop)
    LWallPallet = 10,   // L-shaped walls, pallet on short side
    TWalls = 11,        // T-shaped intersecting walls
    GymBox = 12,        // Rectangular gym (window + pallet)
    DebrisPile = 13,    // Cluster of small solids for LOS breaks
};

struct LocalPoint
{
    float x = 0.0F;
    float y = 0.0F;
};

struct LineSegment
{
    LocalPoint a{};
    LocalPoint b{};
};

struct Point
{
    LocalPoint value{};
};

struct SolidRect
{
    LocalPoint min{};
    LocalPoint max{};
};

class StructureLayout
{
public:
    std::vector<LineSegment> walls;
    std::vector<SolidRect> solids;
    std::vector<Point> windows;
    std::vector<Point> pallets;
    glm::vec2 entranceDirection{0.0F, -1.0F}; // for directional rotation selection

    [[nodiscard]] StructureLayout ApplyRotation(int degrees) const;
    void DebugPrint(const std::string& name) const;
};

[[nodiscard]] float ClampLocal(float value)
{
    return glm::clamp(value, kTileLocalMin, kTileLocalMax);
}

[[nodiscard]] LocalPoint RotatePointByMatrix(const LocalPoint& point, float radians)
{
    // Rotate around tile center so transformed coordinates stay in [0..15].
    const float localX = point.x - kTileCenter;
    const float localY = point.y - kTileCenter;
    const float cosTheta = std::cos(radians);
    const float sinTheta = std::sin(radians);

    const float rotatedX = localX * cosTheta - localY * sinTheta;
    const float rotatedY = localX * sinTheta + localY * cosTheta;

    return LocalPoint{
        ClampLocal(rotatedX + kTileCenter),
        ClampLocal(rotatedY + kTileCenter),
    };
}

[[nodiscard]] glm::vec2 RotateDirectionByMatrix(const glm::vec2& direction, float radians)
{
    const float cosTheta = std::cos(radians);
    const float sinTheta = std::sin(radians);
    return glm::vec2{
        direction.x * cosTheta - direction.y * sinTheta,
        direction.x * sinTheta + direction.y * cosTheta,
    };
}

[[nodiscard]] StructureLayout StructureLayout::ApplyRotation(int degrees) const
{
    StructureLayout rotated = *this;
    const float radians = glm::radians(static_cast<float>(degrees));

    for (LineSegment& wall : rotated.walls)
    {
        wall.a = RotatePointByMatrix(wall.a, radians);
        wall.b = RotatePointByMatrix(wall.b, radians);
    }

    for (SolidRect& rect : rotated.solids)
    {
        const LocalPoint corners[4]{
            rect.min,
            LocalPoint{rect.max.x, rect.min.y},
            rect.max,
            LocalPoint{rect.min.x, rect.max.y},
        };

        float minX = std::numeric_limits<float>::max();
        float minY = std::numeric_limits<float>::max();
        float maxX = -std::numeric_limits<float>::max();
        float maxY = -std::numeric_limits<float>::max();

        for (const LocalPoint& corner : corners)
        {
            const LocalPoint rotatedCorner = RotatePointByMatrix(corner, radians);
            minX = std::min(minX, rotatedCorner.x);
            minY = std::min(minY, rotatedCorner.y);
            maxX = std::max(maxX, rotatedCorner.x);
            maxY = std::max(maxY, rotatedCorner.y);
        }

        rect.min = LocalPoint{ClampLocal(minX), ClampLocal(minY)};
        rect.max = LocalPoint{ClampLocal(maxX), ClampLocal(maxY)};
    }

    for (Point& window : rotated.windows)
    {
        window.value = RotatePointByMatrix(window.value, radians);
    }
    for (Point& pallet : rotated.pallets)
    {
        pallet.value = RotatePointByMatrix(pallet.value, radians);
    }

    rotated.entranceDirection = RotateDirectionByMatrix(rotated.entranceDirection, radians);
    if (glm::length(rotated.entranceDirection) > 1.0e-5F)
    {
        rotated.entranceDirection = glm::normalize(rotated.entranceDirection);
    }
    return rotated;
}

void RasterizeLine(char (&grid)[16][16], const LineSegment& segment)
{
    const int x0 = glm::clamp(static_cast<int>(std::round(segment.a.x)), 0, 15);
    const int y0 = glm::clamp(static_cast<int>(std::round(segment.a.y)), 0, 15);
    const int x1 = glm::clamp(static_cast<int>(std::round(segment.b.x)), 0, 15);
    const int y1 = glm::clamp(static_cast<int>(std::round(segment.b.y)), 0, 15);

    if (x0 == x1)
    {
        const int minY = std::min(y0, y1);
        const int maxY = std::max(y0, y1);
        for (int y = minY; y <= maxY; ++y)
        {
            grid[y][x0] = '#';
        }
        return;
    }

    if (y0 == y1)
    {
        const int minX = std::min(x0, x1);
        const int maxX = std::max(x0, x1);
        for (int x = minX; x <= maxX; ++x)
        {
            grid[y0][x] = '#';
        }
        return;
    }
}

void StructureLayout::DebugPrint(const std::string& name) const
{
    char grid[16][16];
    for (int y = 0; y < 16; ++y)
    {
        for (int x = 0; x < 16; ++x)
        {
            grid[y][x] = '.';
        }
    }

    for (const LineSegment& wall : walls)
    {
        RasterizeLine(grid, wall);
    }

    for (const SolidRect& solid : solids)
    {
        const int minX = glm::clamp(static_cast<int>(std::round(std::min(solid.min.x, solid.max.x))), 0, 15);
        const int maxX = glm::clamp(static_cast<int>(std::round(std::max(solid.min.x, solid.max.x))), 0, 15);
        const int minY = glm::clamp(static_cast<int>(std::round(std::min(solid.min.y, solid.max.y))), 0, 15);
        const int maxY = glm::clamp(static_cast<int>(std::round(std::max(solid.min.y, solid.max.y))), 0, 15);
        for (int y = minY; y <= maxY; ++y)
        {
            for (int x = minX; x <= maxX; ++x)
            {
                grid[y][x] = '#';
            }
        }
    }

    for (const Point& window : windows)
    {
        const int x = glm::clamp(static_cast<int>(std::round(window.value.x)), 0, 15);
        const int y = glm::clamp(static_cast<int>(std::round(window.value.y)), 0, 15);
        grid[y][x] = 'W';
    }
    for (const Point& pallet : pallets)
    {
        const int x = glm::clamp(static_cast<int>(std::round(pallet.value.x)), 0, 15);
        const int y = glm::clamp(static_cast<int>(std::round(pallet.value.y)), 0, 15);
        grid[y][x] = 'P';
    }

    std::cout << "[LAYOUT] " << name << "\n";
    for (int y = 15; y >= 0; --y)
    {
        for (int x = 0; x < 16; ++x)
        {
            std::cout << grid[y][x];
        }
        std::cout << "\n";
    }
}

[[nodiscard]] glm::vec3 LocalToWorld(const glm::vec3& tileCenter, const LocalPoint& point, float y)
{
    return glm::vec3{
        tileCenter.x + (point.x - kTileCenter),
        y,
        tileCenter.z + (point.y - kTileCenter),
    };
}

[[nodiscard]] float SnapValue(float value)
{
    return std::round(value / kWorldSnapStep) * kWorldSnapStep;
}

[[nodiscard]] glm::vec3 SnapVec(const glm::vec3& value)
{
    return glm::vec3{
        SnapValue(value.x),
        SnapValue(value.y),
        SnapValue(value.z),
    };
}

void AddWall(StructureLayout& layout, LocalPoint a, LocalPoint b)
{
    layout.walls.push_back(LineSegment{a, b});
}

void AddWindow(StructureLayout& layout, LocalPoint point)
{
    layout.windows.push_back(Point{point});
}

void AddPallet(StructureLayout& layout, LocalPoint point)
{
    layout.pallets.push_back(Point{point});
}

void AddSolid(StructureLayout& layout, LocalPoint min, LocalPoint max)
{
    layout.solids.push_back(SolidRect{min, max});
}

[[nodiscard]] StructureLayout BuildKillerShackLayout()
{
    StructureLayout layout;
    AddWall(layout, LocalPoint{4.0F, 0.0F}, LocalPoint{12.0F, 0.0F});
    AddWall(layout, LocalPoint{4.0F, 0.0F}, LocalPoint{4.0F, 5.0F});
    AddWall(layout, LocalPoint{4.0F, 7.0F}, LocalPoint{4.0F, 12.0F});
    AddWall(layout, LocalPoint{4.0F, 12.0F}, LocalPoint{7.0F, 12.0F});
    AddWall(layout, LocalPoint{9.0F, 12.0F}, LocalPoint{12.0F, 12.0F});
    AddWall(layout, LocalPoint{12.0F, 0.0F}, LocalPoint{12.0F, 3.0F});
    AddWall(layout, LocalPoint{12.0F, 5.0F}, LocalPoint{12.0F, 12.0F});
    AddWindow(layout, LocalPoint{8.0F, 12.0F});
    AddPallet(layout, LocalPoint{12.0F, 4.0F});
    return layout;
}

[[nodiscard]] StructureLayout BuildLTWallsLayout()
{
    StructureLayout layout;
    AddWall(layout, LocalPoint{2.0F, 2.0F}, LocalPoint{2.0F, 10.0F});
    AddWall(layout, LocalPoint{2.0F, 10.0F}, LocalPoint{6.0F, 10.0F});
    AddWall(layout, LocalPoint{10.0F, 2.0F}, LocalPoint{14.0F, 2.0F});
    AddWall(layout, LocalPoint{12.0F, 2.0F}, LocalPoint{12.0F, 10.0F});
    AddWindow(layout, LocalPoint{2.0F, 6.0F});
    AddWindow(layout, LocalPoint{12.0F, 6.0F});
    return layout;
}

[[nodiscard]] StructureLayout BuildJungleGymLongLayout()
{
    StructureLayout layout;
    AddWall(layout, LocalPoint{2.0F, 2.0F}, LocalPoint{2.0F, 14.0F});
    AddWall(layout, LocalPoint{2.0F, 14.0F}, LocalPoint{8.0F, 14.0F});
    // Split wall for pallet gap: two segments with gap at Y=6
    AddWall(layout, LocalPoint{10.0F, 2.0F}, LocalPoint{10.0F, 5.0F});
    AddWall(layout, LocalPoint{10.0F, 7.0F}, LocalPoint{10.0F, 10.0F});
    AddWindow(layout, LocalPoint{2.0F, 8.0F});
    AddPallet(layout, LocalPoint{10.0F, 6.0F});
    return layout;
}

[[nodiscard]] StructureLayout BuildJungleGymShortLayout()
{
    StructureLayout layout;
    AddWall(layout, LocalPoint{6.0F, 4.0F}, LocalPoint{10.0F, 4.0F});
    // Split horizontal wall for pallet gap at X=8
    AddWall(layout, LocalPoint{4.0F, 8.0F}, LocalPoint{7.0F, 8.0F});
    AddWall(layout, LocalPoint{9.0F, 8.0F}, LocalPoint{12.0F, 8.0F});
    AddWall(layout, LocalPoint{4.0F, 8.0F}, LocalPoint{4.0F, 12.0F});
    AddWall(layout, LocalPoint{12.0F, 8.0F}, LocalPoint{12.0F, 12.0F});
    AddWindow(layout, LocalPoint{8.0F, 4.0F});
    AddPallet(layout, LocalPoint{8.0F, 8.0F});
    layout.entranceDirection = glm::vec2{0.0F, -1.0F};
    return layout;
}

[[nodiscard]] StructureLayout BuildFourLaneLayout()
{
    StructureLayout layout;
    AddWall(layout, LocalPoint{2.0F, 2.0F}, LocalPoint{2.0F, 14.0F});
    AddWall(layout, LocalPoint{6.0F, 2.0F}, LocalPoint{6.0F, 14.0F});
    // Split third lane wall for pallet gap at Y=8
    AddWall(layout, LocalPoint{10.0F, 2.0F}, LocalPoint{10.0F, 7.0F});
    AddWall(layout, LocalPoint{10.0F, 9.0F}, LocalPoint{10.0F, 14.0F});
    AddWall(layout, LocalPoint{14.0F, 2.0F}, LocalPoint{14.0F, 14.0F});
    AddWindow(layout, LocalPoint{6.0F, 8.0F});
    AddPallet(layout, LocalPoint{10.0F, 8.0F});
    return layout;
}

[[nodiscard]] StructureLayout BuildFillerLayoutA()
{
    StructureLayout layout;
    AddSolid(layout, LocalPoint{4.0F, 4.0F}, LocalPoint{6.0F, 10.0F});
    AddSolid(layout, LocalPoint{10.0F, 4.0F}, LocalPoint{12.0F, 10.0F});
    AddPallet(layout, LocalPoint{8.0F, 7.0F});
    return layout;
}

[[nodiscard]] StructureLayout BuildFillerLayoutB()
{
    StructureLayout layout;
    // Wall with gap at right end for pallet
    AddWall(layout, LocalPoint{4.0F, 8.0F}, LocalPoint{11.0F, 8.0F});
    AddWall(layout, LocalPoint{13.0F, 8.0F}, LocalPoint{14.5F, 8.0F});
    AddPallet(layout, LocalPoint{12.0F, 8.0F});
    return layout;
}

// ============================================================
// New v2 loop layouts with variation support
// ============================================================

// --- LongWall: single long wall with window, survivor runs around ends ---
[[nodiscard]] StructureLayout BuildLongWallLayoutA()
{
    StructureLayout layout;
    // Long horizontal wall spanning most of the tile
    AddWall(layout, LocalPoint{2.0F, 8.0F}, LocalPoint{14.0F, 8.0F});
    AddWindow(layout, LocalPoint{8.0F, 8.0F}); // Window at center
    layout.entranceDirection = glm::vec2{0.0F, -1.0F};
    return layout;
}

[[nodiscard]] StructureLayout BuildLongWallLayoutB()
{
    StructureLayout layout;
    // Slightly offset long wall with window near one end
    AddWall(layout, LocalPoint{1.5F, 7.0F}, LocalPoint{13.5F, 7.0F});
    AddWindow(layout, LocalPoint{4.0F, 7.0F}); // Window near left end
    // Small debris at other end for visual interest
    AddSolid(layout, LocalPoint{12.0F, 10.0F}, LocalPoint{13.5F, 11.5F});
    layout.entranceDirection = glm::vec2{0.0F, -1.0F};
    return layout;
}

[[nodiscard]] StructureLayout PickLongWallLayout(std::mt19937& rng)
{
    std::uniform_int_distribution<int> dist(0, 1);
    return dist(rng) == 0 ? BuildLongWallLayoutA() : BuildLongWallLayoutB();
}

// --- ShortWall: short wall with unsafe pallet in gap, weak loop ---
[[nodiscard]] StructureLayout BuildShortWallLayoutA()
{
    StructureLayout layout;
    // Split wall for pallet gap at X=8
    AddWall(layout, LocalPoint{5.0F, 8.0F}, LocalPoint{7.0F, 8.0F});
    AddWall(layout, LocalPoint{9.0F, 8.0F}, LocalPoint{11.0F, 8.0F});
    AddPallet(layout, LocalPoint{8.0F, 8.0F});
    layout.entranceDirection = glm::vec2{0.0F, -1.0F};
    return layout;
}

[[nodiscard]] StructureLayout BuildShortWallLayoutB()
{
    StructureLayout layout;
    // Wall with pallet gap near the right end
    AddWall(layout, LocalPoint{4.5F, 7.0F}, LocalPoint{9.5F, 7.0F});
    AddWall(layout, LocalPoint{11.5F, 7.0F}, LocalPoint{13.0F, 7.0F});
    AddPallet(layout, LocalPoint{10.5F, 7.0F});
    layout.entranceDirection = glm::vec2{0.0F, -1.0F};
    return layout;
}

[[nodiscard]] StructureLayout PickShortWallLayout(std::mt19937& rng)
{
    std::uniform_int_distribution<int> dist(0, 1);
    return dist(rng) == 0 ? BuildShortWallLayoutA() : BuildShortWallLayoutB();
}

// --- LWallWindow: L-shaped walls with window on long side (strong/safe loop) ---
[[nodiscard]] StructureLayout BuildLWallWindowLayoutA()
{
    StructureLayout layout;
    // Vertical wall (long side)
    AddWall(layout, LocalPoint{4.0F, 3.0F}, LocalPoint{4.0F, 13.0F});
    // Horizontal wall (short side, forming the L)
    AddWall(layout, LocalPoint{4.0F, 13.0F}, LocalPoint{10.0F, 13.0F});
    // Window on the long vertical wall
    AddWindow(layout, LocalPoint{4.0F, 8.0F});
    layout.entranceDirection = glm::vec2{1.0F, 0.0F};
    return layout;
}

[[nodiscard]] StructureLayout BuildLWallWindowLayoutB()
{
    StructureLayout layout;
    // Mirror: vertical wall on right
    AddWall(layout, LocalPoint{12.0F, 3.0F}, LocalPoint{12.0F, 13.0F});
    // Horizontal wall going left
    AddWall(layout, LocalPoint{6.0F, 3.0F}, LocalPoint{12.0F, 3.0F});
    AddWindow(layout, LocalPoint{12.0F, 8.0F});
    layout.entranceDirection = glm::vec2{-1.0F, 0.0F};
    return layout;
}

[[nodiscard]] StructureLayout BuildLWallWindowLayoutC()
{
    StructureLayout layout;
    // Wider L with window and debris
    AddWall(layout, LocalPoint{3.0F, 2.0F}, LocalPoint{3.0F, 12.0F});
    AddWall(layout, LocalPoint{3.0F, 12.0F}, LocalPoint{9.0F, 12.0F});
    AddWindow(layout, LocalPoint{3.0F, 7.0F});
    // Small debris piece for additional LOS break
    AddSolid(layout, LocalPoint{10.0F, 5.0F}, LocalPoint{11.5F, 7.0F});
    layout.entranceDirection = glm::vec2{1.0F, 0.0F};
    return layout;
}

[[nodiscard]] StructureLayout PickLWallWindowLayout(std::mt19937& rng)
{
    std::uniform_int_distribution<int> dist(0, 2);
    switch (dist(rng))
    {
        case 0: return BuildLWallWindowLayoutA();
        case 1: return BuildLWallWindowLayoutB();
        default: return BuildLWallWindowLayoutC();
    }
}

// --- LWallPallet: L-shaped walls with pallet gap on short side ---
[[nodiscard]] StructureLayout BuildLWallPalletLayoutA()
{
    StructureLayout layout;
    AddWall(layout, LocalPoint{4.0F, 3.0F}, LocalPoint{4.0F, 12.0F});
    // Split horizontal L-arm for pallet gap at X=7
    AddWall(layout, LocalPoint{4.0F, 12.0F}, LocalPoint{6.0F, 12.0F});
    AddWall(layout, LocalPoint{8.0F, 12.0F}, LocalPoint{10.0F, 12.0F});
    AddPallet(layout, LocalPoint{7.0F, 12.0F});
    layout.entranceDirection = glm::vec2{1.0F, 0.0F};
    return layout;
}

[[nodiscard]] StructureLayout BuildLWallPalletLayoutB()
{
    StructureLayout layout;
    AddWall(layout, LocalPoint{12.0F, 4.0F}, LocalPoint{12.0F, 13.0F});
    // Split horizontal arm for pallet gap at X=9
    AddWall(layout, LocalPoint{6.0F, 4.0F}, LocalPoint{8.0F, 4.0F});
    AddWall(layout, LocalPoint{10.0F, 4.0F}, LocalPoint{12.0F, 4.0F});
    AddPallet(layout, LocalPoint{9.0F, 4.0F});
    layout.entranceDirection = glm::vec2{-1.0F, 0.0F};
    return layout;
}

[[nodiscard]] StructureLayout PickLWallPalletLayout(std::mt19937& rng)
{
    std::uniform_int_distribution<int> dist(0, 1);
    return dist(rng) == 0 ? BuildLWallPalletLayoutA() : BuildLWallPalletLayoutB();
}

// --- TWalls: T-shaped structure with multiple pathing options ---
[[nodiscard]] StructureLayout BuildTWallsLayoutA()
{
    StructureLayout layout;
    // Horizontal wall split for pallet gap at X=11
    AddWall(layout, LocalPoint{2.0F, 8.0F}, LocalPoint{10.0F, 8.0F});
    AddWall(layout, LocalPoint{12.0F, 8.0F}, LocalPoint{14.0F, 8.0F});
    // Vertical stem going up
    AddWall(layout, LocalPoint{8.0F, 8.0F}, LocalPoint{8.0F, 14.0F});
    AddWindow(layout, LocalPoint{5.0F, 8.0F});
    AddPallet(layout, LocalPoint{11.0F, 8.0F});
    layout.entranceDirection = glm::vec2{0.0F, -1.0F};
    return layout;
}

[[nodiscard]] StructureLayout BuildTWallsLayoutB()
{
    StructureLayout layout;
    // Vertical wall split for pallet gap at Y=11
    AddWall(layout, LocalPoint{8.0F, 2.0F}, LocalPoint{8.0F, 10.0F});
    AddWall(layout, LocalPoint{8.0F, 12.0F}, LocalPoint{8.0F, 14.0F});
    // Horizontal stem going right
    AddWall(layout, LocalPoint{8.0F, 8.0F}, LocalPoint{14.0F, 8.0F});
    AddWindow(layout, LocalPoint{8.0F, 5.0F});
    AddPallet(layout, LocalPoint{8.0F, 11.0F});
    layout.entranceDirection = glm::vec2{-1.0F, 0.0F};
    return layout;
}

[[nodiscard]] StructureLayout BuildTWallsLayoutC()
{
    StructureLayout layout;
    // Horizontal wall split for pallet gap at X=10.5
    AddWall(layout, LocalPoint{3.0F, 6.0F}, LocalPoint{9.5F, 6.0F});
    AddWall(layout, LocalPoint{11.5F, 6.0F}, LocalPoint{13.0F, 6.0F});
    // Stem going down
    AddWall(layout, LocalPoint{8.0F, 2.0F}, LocalPoint{8.0F, 6.0F});
    AddWindow(layout, LocalPoint{5.5F, 6.0F});
    AddPallet(layout, LocalPoint{10.5F, 6.0F});
    AddSolid(layout, LocalPoint{2.0F, 10.0F}, LocalPoint{3.5F, 12.0F});
    layout.entranceDirection = glm::vec2{0.0F, 1.0F};
    return layout;
}

[[nodiscard]] StructureLayout PickTWallsLayout(std::mt19937& rng)
{
    std::uniform_int_distribution<int> dist(0, 2);
    switch (dist(rng))
    {
        case 0: return BuildTWallsLayoutA();
        case 1: return BuildTWallsLayoutB();
        default: return BuildTWallsLayoutC();
    }
}

// --- GymBox: rectangular enclosure with window + pallet (strong loop) ---
[[nodiscard]] StructureLayout BuildGymBoxLayoutA()
{
    StructureLayout layout;
    // Left wall
    AddWall(layout, LocalPoint{4.0F, 4.0F}, LocalPoint{4.0F, 12.0F});
    // Right wall split for pallet gap at Y=8
    AddWall(layout, LocalPoint{12.0F, 4.0F}, LocalPoint{12.0F, 7.0F});
    AddWall(layout, LocalPoint{12.0F, 9.0F}, LocalPoint{12.0F, 12.0F});
    // Top wall with entrance gap
    AddWall(layout, LocalPoint{4.0F, 12.0F}, LocalPoint{8.0F, 12.0F});
    AddWall(layout, LocalPoint{10.0F, 12.0F}, LocalPoint{12.0F, 12.0F});
    // Bottom wall
    AddWall(layout, LocalPoint{4.0F, 4.0F}, LocalPoint{12.0F, 4.0F});
    AddWindow(layout, LocalPoint{4.0F, 8.0F});
    AddPallet(layout, LocalPoint{12.0F, 8.0F});
    layout.entranceDirection = glm::vec2{0.0F, 1.0F};
    return layout;
}

[[nodiscard]] StructureLayout BuildGymBoxLayoutB()
{
    StructureLayout layout;
    // Slightly smaller gym with different opening
    AddWall(layout, LocalPoint{5.0F, 5.0F}, LocalPoint{5.0F, 11.0F});
    AddWall(layout, LocalPoint{11.0F, 5.0F}, LocalPoint{11.0F, 11.0F});
    AddWall(layout, LocalPoint{5.0F, 11.0F}, LocalPoint{11.0F, 11.0F});
    AddWall(layout, LocalPoint{5.0F, 5.0F}, LocalPoint{7.0F, 5.0F});  // Bottom left
    AddWall(layout, LocalPoint{9.0F, 5.0F}, LocalPoint{11.0F, 5.0F}); // Bottom right (gap)
    AddWindow(layout, LocalPoint{11.0F, 8.0F});
    AddPallet(layout, LocalPoint{8.0F, 5.0F}); // Pallet at bottom entrance
    layout.entranceDirection = glm::vec2{0.0F, -1.0F};
    return layout;
}

[[nodiscard]] StructureLayout PickGymBoxLayout(std::mt19937& rng)
{
    std::uniform_int_distribution<int> dist(0, 1);
    return dist(rng) == 0 ? BuildGymBoxLayoutA() : BuildGymBoxLayoutB();
}

// --- DebrisPile: cluster of obstacles for line-of-sight breaks (weak filler loop) ---
[[nodiscard]] StructureLayout BuildDebrisPileLayoutA()
{
    StructureLayout layout;
    AddSolid(layout, LocalPoint{3.0F, 6.0F}, LocalPoint{5.0F, 10.0F});
    AddSolid(layout, LocalPoint{7.0F, 4.0F}, LocalPoint{9.0F, 7.0F});
    AddSolid(layout, LocalPoint{10.0F, 9.0F}, LocalPoint{13.0F, 11.0F});
    return layout;
}

[[nodiscard]] StructureLayout BuildDebrisPileLayoutB()
{
    StructureLayout layout;
    AddSolid(layout, LocalPoint{4.0F, 3.0F}, LocalPoint{6.5F, 5.5F});
    // Two solids forming a narrow corridor for the pallet
    AddSolid(layout, LocalPoint{5.5F, 7.0F}, LocalPoint{6.5F, 11.0F});
    AddSolid(layout, LocalPoint{8.5F, 7.0F}, LocalPoint{10.0F, 11.0F});
    AddSolid(layout, LocalPoint{11.0F, 3.0F}, LocalPoint{13.0F, 5.0F});
    AddPallet(layout, LocalPoint{7.5F, 9.0F});
    return layout;
}

[[nodiscard]] StructureLayout PickDebrisPileLayout(std::mt19937& rng)
{
    std::uniform_int_distribution<int> dist(0, 1);
    return dist(rng) == 0 ? BuildDebrisPileLayoutA() : BuildDebrisPileLayoutB();
}

// Also add variations to existing layouts
[[nodiscard]] StructureLayout BuildLTWallsLayoutV2()
{
    StructureLayout layout;
    // Variant: mirrored L and T with different spacing
    AddWall(layout, LocalPoint{3.0F, 2.0F}, LocalPoint{3.0F, 9.0F});
    AddWall(layout, LocalPoint{3.0F, 9.0F}, LocalPoint{7.0F, 9.0F});
    AddWall(layout, LocalPoint{9.0F, 2.0F}, LocalPoint{13.0F, 2.0F});
    AddWall(layout, LocalPoint{13.0F, 2.0F}, LocalPoint{13.0F, 11.0F});
    AddWindow(layout, LocalPoint{3.0F, 5.5F});
    AddWindow(layout, LocalPoint{13.0F, 6.5F});
    return layout;
}

[[nodiscard]] StructureLayout BuildJungleGymLongV2()
{
    StructureLayout layout;
    AddWall(layout, LocalPoint{3.0F, 2.0F}, LocalPoint{3.0F, 13.0F});
    AddWall(layout, LocalPoint{3.0F, 13.0F}, LocalPoint{9.0F, 13.0F});
    // Split wall for pallet gap at Y=7
    AddWall(layout, LocalPoint{11.0F, 3.0F}, LocalPoint{11.0F, 6.0F});
    AddWall(layout, LocalPoint{11.0F, 8.0F}, LocalPoint{11.0F, 11.0F});
    AddWindow(layout, LocalPoint{3.0F, 7.5F});
    AddPallet(layout, LocalPoint{11.0F, 7.0F});
    return layout;
}

[[nodiscard]] StructureLayout PickLTWallsLayout(std::mt19937& rng, const StructureLayout& original)
{
    std::uniform_int_distribution<int> dist(0, 1);
    return dist(rng) == 0 ? original : BuildLTWallsLayoutV2();
}

[[nodiscard]] StructureLayout PickJungleLongLayout(std::mt19937& rng, const StructureLayout& original)
{
    std::uniform_int_distribution<int> dist(0, 1);
    return dist(rng) == 0 ? original : BuildJungleGymLongV2();
}

[[nodiscard]] bool IsMazeArchetype(TileArchetype archetype)
{
    return archetype == TileArchetype::JungleGymLong ||
           archetype == TileArchetype::JungleGymShort ||
           archetype == TileArchetype::FourLane ||
           archetype == TileArchetype::LTWalls ||
           archetype == TileArchetype::Shack ||
           archetype == TileArchetype::LongWall ||
           archetype == TileArchetype::ShortWall ||
           archetype == TileArchetype::LWallWindow ||
           archetype == TileArchetype::LWallPallet ||
           archetype == TileArchetype::TWalls ||
           archetype == TileArchetype::GymBox;
}

// Returns true if the archetype has a "safe" pallet (requires killer to go around a long wall).
[[nodiscard]] bool HasSafePallet(TileArchetype archetype)
{
    return archetype == TileArchetype::LWallPallet ||
           archetype == TileArchetype::GymBox ||
           archetype == TileArchetype::JungleGymLong;
}

// Returns true if the archetype is a filler / non-loopable / debris type.
[[nodiscard]] bool IsFillerArchetype(TileArchetype archetype)
{
    return archetype == TileArchetype::FillerA ||
           archetype == TileArchetype::FillerB ||
           archetype == TileArchetype::DebrisPile;
}

[[nodiscard]] float DistancePointToSegment(const glm::vec2& p, const glm::vec2& a, const glm::vec2& b)
{
    const glm::vec2 ab = b - a;
    const float denom = glm::dot(ab, ab);
    if (denom < 1.0e-6F)
    {
        return glm::length(p - a);
    }
    const float t = glm::clamp(glm::dot(p - a, ab) / denom, 0.0F, 1.0F);
    return glm::length(p - (a + ab * t));
}

[[nodiscard]] bool NearestWallVertical(const StructureLayout& layout, const LocalPoint& point)
{
    float bestDistance = std::numeric_limits<float>::max();
    bool vertical = false;
    const glm::vec2 p{point.x, point.y};
    for (const LineSegment& wall : layout.walls)
    {
        const glm::vec2 a{wall.a.x, wall.a.y};
        const glm::vec2 b{wall.b.x, wall.b.y};
        const float distance = DistancePointToSegment(p, a, b);
        if (distance < bestDistance)
        {
            bestDistance = distance;
            vertical = std::abs(wall.a.x - wall.b.x) < std::abs(wall.a.y - wall.b.y);
        }
    }
    return vertical;
}

void EmitLayout(GeneratedMap& map, const StructureLayout& layout, const glm::vec3& tileCenter)
{
    for (const LineSegment& wall : layout.walls)
    {
        const glm::vec3 a = LocalToWorld(tileCenter, wall.a, 0.0F);
        const glm::vec3 b = LocalToWorld(tileCenter, wall.b, 0.0F);
        const glm::vec3 delta = b - a;
        const bool xMajor = std::abs(delta.x) >= std::abs(delta.z);
        const float length = std::max(std::abs(delta.x), std::abs(delta.z));
        if (length < 0.05F)
        {
            continue;
        }

        map.walls.push_back(BoxSpawn{
            SnapVec((a + b) * 0.5F + glm::vec3{0.0F, kWallHalfHeight, 0.0F}),
            SnapVec(xMajor
                        ? glm::vec3{length * 0.5F, kWallHalfHeight, kWallThickness}
                        : glm::vec3{kWallThickness, kWallHalfHeight, length * 0.5F}),
        });
    }

    for (const SolidRect& solid : layout.solids)
    {
        const float minX = std::min(solid.min.x, solid.max.x);
        const float maxX = std::max(solid.min.x, solid.max.x);
        const float minY = std::min(solid.min.y, solid.max.y);
        const float maxY = std::max(solid.min.y, solid.max.y);
        const LocalPoint center{
            (minX + maxX) * 0.5F,
            (minY + maxY) * 0.5F,
        };
        const glm::vec3 worldCenter = LocalToWorld(tileCenter, center, kWallHalfHeight);
        const glm::vec3 halfExtents{
            std::max(0.25F, (maxX - minX) * 0.5F),
            kWallHalfHeight,
            std::max(0.25F, (maxY - minY) * 0.5F),
        };
        map.walls.push_back(BoxSpawn{
            SnapVec(worldCenter),
            SnapVec(halfExtents),
        });
    }

    for (const Point& window : layout.windows)
    {
        const bool verticalWall = NearestWallVertical(layout, window.value);
        const glm::vec3 normal = verticalWall ? glm::vec3{1.0F, 0.0F, 0.0F} : glm::vec3{0.0F, 0.0F, 1.0F};
        map.windows.push_back(WindowSpawn{
            SnapVec(LocalToWorld(tileCenter, window.value, 1.0F)),
            verticalWall ? glm::vec3{0.18F, 1.0F, 0.95F} : glm::vec3{0.95F, 1.0F, 0.18F},
            normal,
        });
    }

    for (const Point& pallet : layout.pallets)
    {
        const bool verticalWall = NearestWallVertical(layout, pallet.value);
        map.pallets.push_back(PalletSpawn{
            SnapVec(LocalToWorld(tileCenter, pallet.value, 0.6F)),
            verticalWall ? glm::vec3{0.2F, 0.6F, 0.95F} : glm::vec3{0.95F, 0.6F, 0.2F},
        });
    }
}

[[nodiscard]] float GetDistanceToNearestMaze(const glm::ivec2& currentTile, const std::vector<glm::ivec2>& mazeTiles)
{
    float best = std::numeric_limits<float>::max();
    for (const glm::ivec2& tile : mazeTiles)
    {
        const glm::vec2 delta = glm::vec2{
            static_cast<float>(currentTile.x - tile.x),
            static_cast<float>(currentTile.y - tile.y),
        };
        best = std::min(best, glm::length(delta));
    }
    return best;
}

[[nodiscard]] int PickRandomRotation(std::mt19937& rng)
{
    std::uniform_int_distribution<int> dist(0, 3);
    return dist(rng) * 90;
}

[[nodiscard]] int PickShortLayoutRotationFacingCenter(const StructureLayout& base, const glm::vec3& tileCenter, std::mt19937& rng)
{
    glm::vec2 toCenter = glm::vec2{-tileCenter.x, -tileCenter.z};
    if (glm::length(toCenter) < 1.0e-4F)
    {
        return PickRandomRotation(rng);
    }
    toCenter = glm::normalize(toCenter);

    float bestDot = -std::numeric_limits<float>::max();
    int bestRotation = 0;
    for (int step = 0; step < 4; ++step)
    {
        const int degrees = step * 90;
        const StructureLayout rotated = base.ApplyRotation(degrees);
        glm::vec2 entrance = rotated.entranceDirection;
        if (glm::length(entrance) < 1.0e-5F)
        {
            entrance = glm::vec2{0.0F, -1.0F};
        }
        entrance = glm::normalize(entrance);
        const float dot = glm::dot(entrance, toCenter);
        if (dot > bestDot)
        {
            bestDot = dot;
            bestRotation = degrees;
        }
    }
    return bestRotation;
}

[[nodiscard]] TileArchetype PickWeightedArchetype(
    std::mt19937& rng,
    const std::vector<std::pair<TileArchetype, float>>& weightedCandidates,
    TileArchetype fallback
)
{
    std::vector<double> weights;
    weights.reserve(weightedCandidates.size());
    double total = 0.0;
    for (const auto& [_, weight] : weightedCandidates)
    {
        const double safe = std::max(0.0F, weight);
        weights.push_back(safe);
        total += safe;
    }

    if (weightedCandidates.empty() || total <= 1.0e-9)
    {
        return fallback;
    }

    std::discrete_distribution<int> dist(weights.begin(), weights.end());
    const int index = dist(rng);
    if (index < 0 || index >= static_cast<int>(weightedCandidates.size()))
    {
        return fallback;
    }
    return weightedCandidates[static_cast<std::size_t>(index)].first;
}

[[nodiscard]] TileArchetype PickFillerArchetype(std::mt19937& rng, const TileGenerator::GenerationSettings& settings)
{
    const std::vector<std::pair<TileArchetype, float>> weighted{
        {TileArchetype::FillerA, settings.weightFillerA},
        {TileArchetype::FillerB, settings.weightFillerB},
        {TileArchetype::DebrisPile, settings.weightDebrisPile},
    };
    return PickWeightedArchetype(rng, weighted, TileArchetype::FillerA);
}

// For archetype types that use variation, we generate a fresh layout.
// For legacy types we return a reference to the pre-built layout.
// This function always returns a copy (by value) so the caller can rotate freely.
[[nodiscard]] StructureLayout PickLayoutForArchetype(
    TileArchetype archetype,
    std::mt19937& rng,
    const StructureLayout& jungleLong,
    const StructureLayout& jungleShort,
    const StructureLayout& ltwalls,
    const StructureLayout& shack,
    const StructureLayout& fourLane,
    const StructureLayout& fillerA,
    const StructureLayout& fillerB
)
{
    switch (archetype)
    {
        case TileArchetype::JungleGymLong: return PickJungleLongLayout(rng, jungleLong);
        case TileArchetype::JungleGymShort: return jungleShort;
        case TileArchetype::LTWalls: return PickLTWallsLayout(rng, ltwalls);
        case TileArchetype::Shack: return shack;
        case TileArchetype::FourLane: return fourLane;
        case TileArchetype::FillerA: return fillerA;
        case TileArchetype::FillerB: return fillerB;
        case TileArchetype::LongWall: return PickLongWallLayout(rng);
        case TileArchetype::ShortWall: return PickShortWallLayout(rng);
        case TileArchetype::LWallWindow: return PickLWallWindowLayout(rng);
        case TileArchetype::LWallPallet: return PickLWallPalletLayout(rng);
        case TileArchetype::TWalls: return PickTWallsLayout(rng);
        case TileArchetype::GymBox: return PickGymBoxLayout(rng);
        case TileArchetype::DebrisPile: return PickDebrisPileLayout(rng);
        default: return fillerA;
    }
}

void MaybeDebugPrintLayouts(
    const StructureLayout& jungleLong,
    const StructureLayout& jungleShort,
    const StructureLayout& ltwalls,
    const StructureLayout& shack,
    const StructureLayout& fourLane,
    const StructureLayout& fillerA,
    const StructureLayout& fillerB
)
{
    static bool printed = false;
    if (printed)
    {
        return;
    }
    printed = true;

    const char* env = std::getenv("DBD_LAYOUT_DEBUG");
    if (env == nullptr || std::string(env) == "0")
    {
        return;
    }

    jungleLong.DebugPrint("JungleGymLong");
    jungleShort.DebugPrint("JungleGymShort");
    ltwalls.DebugPrint("LTWalls");
    shack.DebugPrint("Shack");
    fourLane.DebugPrint("FourLane");
    fillerA.DebugPrint("FillerA");
    fillerB.DebugPrint("FillerB");

    // Also print samples of new v2 layouts
    std::mt19937 sampleRng(42);
    PickLongWallLayout(sampleRng).DebugPrint("LongWall (sample)");
    PickShortWallLayout(sampleRng).DebugPrint("ShortWall (sample)");
    PickLWallWindowLayout(sampleRng).DebugPrint("LWallWindow (sample)");
    PickLWallPalletLayout(sampleRng).DebugPrint("LWallPallet (sample)");
    PickTWallsLayout(sampleRng).DebugPrint("TWalls (sample)");
    PickGymBoxLayout(sampleRng).DebugPrint("GymBox (sample)");
    PickDebrisPileLayout(sampleRng).DebugPrint("DebrisPile (sample)");
}
} // namespace

GeneratedMap TileGenerator::GenerateTestMap() const
{
    GeneratedMap map;

    map.survivorSpawn = glm::vec3{-5.0F, 1.05F, 0.0F};
    map.killerSpawn = glm::vec3{5.0F, 1.05F, 0.0F};

    map.walls.push_back(BoxSpawn{glm::vec3{0.0F, -0.5F, 0.0F}, glm::vec3{24.0F, 0.5F, 24.0F}});
    map.walls.push_back(BoxSpawn{glm::vec3{0.0F, 1.0F, -12.0F}, glm::vec3{24.0F, 1.0F, 0.6F}});
    map.walls.push_back(BoxSpawn{glm::vec3{0.0F, 1.0F, 12.0F}, glm::vec3{24.0F, 1.0F, 0.6F}});
    map.walls.push_back(BoxSpawn{glm::vec3{-24.0F, 1.0F, 0.0F}, glm::vec3{0.6F, 1.0F, 12.0F}});
    map.walls.push_back(BoxSpawn{glm::vec3{24.0F, 1.0F, 0.0F}, glm::vec3{0.6F, 1.0F, 12.0F}});

    map.walls.push_back(BoxSpawn{glm::vec3{0.0F, 1.0F, 0.0F}, glm::vec3{3.0F, 1.0F, 0.6F}});
    map.windows.push_back(WindowSpawn{glm::vec3{0.0F, 1.0F, 0.7F}, glm::vec3{1.0F, 1.0F, 0.18F}, glm::vec3{0.0F, 0.0F, 1.0F}});
    map.pallets.push_back(PalletSpawn{glm::vec3{3.4F, 0.6F, 2.2F}, glm::vec3{0.95F, 0.6F, 0.2F}});
    map.generatorSpawns.push_back(glm::vec3{0.0F, 1.0F, -4.0F});
    map.tiles.push_back(GeneratedMap::TileDebug{glm::vec3{0.0F}, glm::vec3{24.0F, 0.05F, 24.0F}, 0, 0});
    return map;
}

GeneratedMap TileGenerator::GenerateMainMap(unsigned int seed) const
{
    return GenerateMainMap(seed, GenerationSettings{});
}

GeneratedMap TileGenerator::GenerateMainMap(unsigned int seed, const GenerationSettings& settings) const
{
    GeneratedMap map;
    const StructureLayout jungleLong = BuildJungleGymLongLayout();
    const StructureLayout jungleShort = BuildJungleGymShortLayout();
    const StructureLayout ltwalls = BuildLTWallsLayout();
    const StructureLayout shack = BuildKillerShackLayout();
    const StructureLayout fourLane = BuildFourLaneLayout();
    const StructureLayout fillerA = BuildFillerLayoutA();
    const StructureLayout fillerB = BuildFillerLayoutB();

    MaybeDebugPrintLayouts(jungleLong, jungleShort, ltwalls, shack, fourLane, fillerA, fillerB);

    std::mt19937 rng(seed);

    const float mapHalf = kGridSize * kTileSize * 0.5F;
    const float firstTileCenter = -mapHalf + kTileSize * 0.5F;

    map.walls.push_back(BoxSpawn{glm::vec3{0.0F, -0.5F, 0.0F}, glm::vec3{mapHalf + 6.0F, 0.5F, mapHalf + 6.0F}});
    map.walls.push_back(BoxSpawn{glm::vec3{0.0F, 1.0F, -(mapHalf + 0.6F)}, glm::vec3{mapHalf + 4.0F, 1.0F, 0.6F}});
    map.walls.push_back(BoxSpawn{glm::vec3{0.0F, 1.0F, (mapHalf + 0.6F)}, glm::vec3{mapHalf + 4.0F, 1.0F, 0.6F}});
    map.walls.push_back(BoxSpawn{glm::vec3{-(mapHalf + 0.6F), 1.0F, 0.0F}, glm::vec3{0.6F, 1.0F, mapHalf + 4.0F}});
    map.walls.push_back(BoxSpawn{glm::vec3{(mapHalf + 0.6F), 1.0F, 0.0F}, glm::vec3{0.6F, 1.0F, mapHalf + 4.0F}});

    map.survivorSpawn = glm::vec3{firstTileCenter - 6.5F, 1.05F, firstTileCenter - 6.5F};
    map.killerSpawn = glm::vec3{firstTileCenter + (kGridSize - 1) * kTileSize + 6.5F, 1.05F, firstTileCenter + (kGridSize - 1) * kTileSize + 6.5F};

    std::unordered_map<int, TileArchetype> forced;
    // Position shack and LTWalls for 12x12 grid (spread apart)
    forced.emplace(3 * kGridSize + 3, TileArchetype::Shack);
    forced.emplace(8 * kGridSize + 8, TileArchetype::LTWalls);

    // Track loop positions for generator placement (always exactly 5)
    std::vector<glm::vec3> loopCenters;
    std::vector<int> loopPriorities; // Higher = better for generator

    // Build full weighted candidate list including v2 types.
    const std::vector<std::pair<TileArchetype, float>> allWeights{
        {TileArchetype::LTWalls, settings.weightTLWalls},
        {TileArchetype::JungleGymLong, settings.weightJungleGymLong},
        {TileArchetype::JungleGymShort, settings.weightJungleGymShort},
        {TileArchetype::Shack, settings.weightShack},
        {TileArchetype::FourLane, settings.weightFourLane},
        {TileArchetype::FillerA, settings.weightFillerA},
        {TileArchetype::FillerB, settings.weightFillerB},
        // v2 types:
        {TileArchetype::LongWall, settings.weightLongWall},
        {TileArchetype::ShortWall, settings.weightShortWall},
        {TileArchetype::LWallWindow, settings.weightLWallWindow},
        {TileArchetype::LWallPallet, settings.weightLWallPallet},
        {TileArchetype::TWalls, settings.weightTWalls},
        {TileArchetype::GymBox, settings.weightGymBox},
        {TileArchetype::DebrisPile, settings.weightDebrisPile},
    };

    std::vector<glm::ivec2> mazeTiles;
    int loopsPlaced = 0;
    int safePalletsPlaced = 0;
    const int maxLoops = std::max(0, settings.maxLoops);
    const int maxSafePallets = std::max(0, settings.maxSafePallets);
    const float minLoopDistanceTiles = std::max(0.0F, settings.minLoopDistanceTiles);
    const int maxDeadzone = std::max(1, settings.maxDeadzoneTiles);

    // Track consecutive filler tiles (for deadzone prevention)
    int consecutiveFillerInRow = 0;

    // Edge bias: tiles on the outer ring get a loop bonus.
    auto isEdgeTile = [](int x, int z) {
        return x == 0 || x == kGridSize - 1 || z == 0 || z == kGridSize - 1;
    };

    for (int z = 0; z < kGridSize; ++z)
    {
        consecutiveFillerInRow = 0;
        for (int x = 0; x < kGridSize; ++x)
        {
            const int key = z * kGridSize + x;
            const glm::ivec2 tileCoord{x, z};
            const glm::vec3 tileCenter{
                firstTileCenter + static_cast<float>(x) * kTileSize,
                0.0F,
                firstTileCenter + static_cast<float>(z) * kTileSize,
            };

            bool forcedTile = false;
            TileArchetype archetype = TileArchetype::FillerA;
            if (const auto it = forced.find(key); it != forced.end())
            {
                archetype = it->second;
                forcedTile = true;
            }
            else
            {
                archetype = PickWeightedArchetype(rng, allWeights, TileArchetype::FillerA);
            }

            if (!forcedTile && IsMazeArchetype(archetype))
            {
                // --- Constraint: max loops ---
                if (loopsPlaced >= maxLoops)
                {
                    archetype = PickFillerArchetype(rng, settings);
                }

                // --- Constraint: min distance between loops ---
                const float nearestMaze = GetDistanceToNearestMaze(tileCoord, mazeTiles);
                if (nearestMaze < minLoopDistanceTiles)
                {
                    archetype = PickFillerArchetype(rng, settings);
                }

                // --- Constraint: safe pallet budget ---
                if (IsMazeArchetype(archetype) && HasSafePallet(archetype) && safePalletsPlaced >= maxSafePallets)
                {
                    // Try to substitute with a non-safe-pallet loop type
                    const std::vector<std::pair<TileArchetype, float>> unsafeOnlyWeights{
                        {TileArchetype::LTWalls, settings.weightTLWalls},
                        {TileArchetype::JungleGymShort, settings.weightJungleGymShort},
                        {TileArchetype::FourLane, settings.weightFourLane},
                        {TileArchetype::LongWall, settings.weightLongWall},
                        {TileArchetype::ShortWall, settings.weightShortWall},
                        {TileArchetype::LWallWindow, settings.weightLWallWindow},
                        {TileArchetype::TWalls, settings.weightTWalls},
                    };
                    archetype = PickWeightedArchetype(rng, unsafeOnlyWeights, TileArchetype::LongWall);
                }
            }

            // --- Constraint: deadzone prevention ---
            if (!forcedTile && IsFillerArchetype(archetype))
            {
                ++consecutiveFillerInRow;
                if (consecutiveFillerInRow >= maxDeadzone && loopsPlaced < maxLoops)
                {
                    // Force a loop tile to break the deadzone
                    const float nearestMaze = GetDistanceToNearestMaze(tileCoord, mazeTiles);
                    if (nearestMaze >= 1.0F) // Allow tighter packing to prevent deadzones
                    {
                        // Pick a simpler loop type for forced placement
                        const std::vector<std::pair<TileArchetype, float>> simpleLoops{
                            {TileArchetype::LongWall, 2.0F},
                            {TileArchetype::ShortWall, 2.0F},
                            {TileArchetype::LWallWindow, 1.5F},
                            {TileArchetype::FillerA, 1.0F},
                        };
                        archetype = PickWeightedArchetype(rng, simpleLoops, TileArchetype::LongWall);
                    }
                }
            }
            else
            {
                consecutiveFillerInRow = 0;
            }

            // --- Edge bias: boost loop probability near edges ---
            if (!forcedTile && settings.edgeBiasLoops && isEdgeTile(x, z) &&
                IsFillerArchetype(archetype) && loopsPlaced < maxLoops)
            {
                // 40% chance to upgrade edge filler to a loop
                std::uniform_real_distribution<float> edgeDist(0.0F, 1.0F);
                if (edgeDist(rng) < 0.4F)
                {
                    const float nearestMaze = GetDistanceToNearestMaze(tileCoord, mazeTiles);
                    if (nearestMaze >= minLoopDistanceTiles)
                    {
                        const std::vector<std::pair<TileArchetype, float>> edgeLoops{
                            {TileArchetype::LongWall, 2.0F},
                            {TileArchetype::LWallWindow, 1.5F},
                            {TileArchetype::ShortWall, 1.0F},
                        };
                        archetype = PickWeightedArchetype(rng, edgeLoops, TileArchetype::LongWall);
                    }
                }
            }

            // --- Rotation selection ---
            int rotation = PickRandomRotation(rng);
            if (archetype == TileArchetype::JungleGymShort || archetype == TileArchetype::GymBox)
            {
                rotation = PickShortLayoutRotationFacingCenter(jungleShort, tileCenter, rng);
            }

            // Pick layout with archetype-specific variation
            const StructureLayout base = PickLayoutForArchetype(
                archetype, rng, jungleLong, jungleShort, ltwalls, shack, fourLane, fillerA, fillerB);
            const StructureLayout rotated = base.ApplyRotation(rotation);
            EmitLayout(map, rotated, tileCenter);

            if (IsMazeArchetype(archetype))
            {
                mazeTiles.push_back(tileCoord);
                ++loopsPlaced;
                if (HasSafePallet(archetype))
                {
                    ++safePalletsPlaced;
                }

                // Track loop for generator placement (higher priority for complex loops)
                loopCenters.push_back(tileCenter);
                int priority = 1;
                if (archetype == TileArchetype::JungleGymLong || archetype == TileArchetype::GymBox ||
                    archetype == TileArchetype::LTWalls || archetype == TileArchetype::FourLane)
                {
                    priority = 3; // Strong loops
                }
                else if (archetype == TileArchetype::LWallWindow || archetype == TileArchetype::LWallPallet ||
                         archetype == TileArchetype::TWalls)
                {
                    priority = 2; // Medium loops
                }
                loopPriorities.push_back(priority);
            }

            map.tiles.push_back(GeneratedMap::TileDebug{
                tileCenter,
                glm::vec3{kTileHalfExtent, 0.05F, kTileHalfExtent},
                key,
                static_cast<int>(archetype),
            });
        }
    }

    // Log generation stats
    std::cout << "[TileGen] Seed=" << seed
              << " loops=" << loopsPlaced
              << " safePallets=" << safePalletsPlaced
              << " tiles=" << kGridSize * kGridSize
              << "\n";

    // --- Place exactly 5 generators at loop positions ---
    // Strategy: pick highest priority loops, then spread them across the map
    if (loopCenters.empty())
    {
        // Fallback: place at map center if no loops
        map.generatorSpawns.push_back(glm::vec3{0.0F, 1.0F, 0.0F});
    }
    else
    {
        // Sort loops by priority (highest first)
        std::vector<std::size_t> indices(loopCenters.size());
        std::iota(indices.begin(), indices.end(), 0);
        std::stable_sort(indices.begin(), indices.end(),
            [&loopPriorities](std::size_t a, std::size_t b) {
                return loopPriorities[a] > loopPriorities[b];
            });

        // Pick top 5 (or all if fewer)
        const int targetCount = 5;
        const int pickCount = std::min(targetCount, static_cast<int>(loopCenters.size()));

        // First add highest priority loops
        std::vector<glm::vec3> selected;
        for (int i = 0; i < pickCount; ++i)
        {
            selected.push_back(loopCenters[indices[i]]);
        }

        // If we need more, add remaining loops (shouldn't happen with proper weights)
        for (std::size_t i = pickCount; i < indices.size() && static_cast<int>(selected.size()) < targetCount; ++i)
        {
            selected.push_back(loopCenters[indices[i]]);
        }

        // Spread selected generators: pick most spread out positions, avoiding windows/pallets
        if (static_cast<int>(selected.size()) > targetCount)
        {
            // Advanced greedy spread: maximize distance between generators AND avoid vaults
            const float minDistanceFromVault = 4.0F; // Minimalna odległość od okna/palety
            
            auto getScoreForCandidate = [&](const glm::vec3& candidate, const std::vector<glm::vec3>& placed) -> float {
                float score = 0.0F;
                
                // 1. Minimal distance to already placed generators (HIGHER is better)
                float minGenDist = std::numeric_limits<float>::max();
                for (const glm::vec3& placedGen : placed)
                {
                    float d = glm::length(candidate - placedGen);
                    minGenDist = std::min(minGenDist, d);
                }
                score += minGenDist * 2.0F; // Waga 2x dla rozrzucenia
                
                // 2. Minimal distance to windows/pallets (LOWER is better, subtract from score)
                float minVaultDist = std::numeric_limits<float>::max();
                
                for (const WindowSpawn& win : map.windows)
                {
                    float d = glm::length(glm::vec2(candidate.x, candidate.z) - glm::vec2(win.center.x, win.center.z));
                    minVaultDist = std::min(minVaultDist, d);
                }
                for (const PalletSpawn& pal : map.pallets)
                {
                    float d = glm::length(glm::vec2(candidate.x, candidate.z) - glm::vec2(pal.center.x, pal.center.z));
                    minVaultDist = std::min(minVaultDist, d);
                }
                
                // Kara za bycie blisko okna/palety
                if (minVaultDist < minDistanceFromVault)
                {
                    score -= (minDistanceFromVault - minVaultDist) * 10.0F; // Silna kara
                }
                
                return score;
            };
            
            std::vector<glm::vec3> spread;
            spread.push_back(selected[0]); // Start with highest priority

            while (spread.size() < static_cast<std::size_t>(targetCount))
            {
                float bestScore = -std::numeric_limits<float>::max();
                std::size_t bestIdx = 0;

                for (std::size_t i = 0; i < selected.size(); ++i)
                {
                    const glm::vec3& candidate = selected[i];
                    
                    // Skip if already selected
                    bool alreadyUsed = false;
                    for (const glm::vec3& used : spread)
                    {
                        if (glm::length(candidate - used) < 0.1F)
                        {
                            alreadyUsed = true;
                            break;
                        }
                    }
                    if (alreadyUsed) continue;

                    float score = getScoreForCandidate(candidate, spread);
                    if (score > bestScore)
                    {
                        bestScore = score;
                        bestIdx = i;
                    }
                }

                spread.push_back(selected[bestIdx]);
            }

            selected = spread;
        }

        // Add generator spawns with collision avoidance
        // Generator half extents: {0.35, 0.6, 0.35}
        const glm::vec3 genHalfExtents{0.35F, 0.6F, 0.35F};
        const float clearance = 0.1F; // Dodatkowy odstęp od ścian
        const glm::vec3 effectiveHalfExtents = genHalfExtents + glm::vec3{clearance};

        for (const glm::vec3& loopPos : selected)
        {
            glm::vec3 genPos = loopPos + glm::vec3{0.0F, 1.0F, 0.0F};
            
            // Sprawdź kolizję z istniejącymi ścianami w tym tile
            bool collisionFound = true;
            int attempts = 0;
            const int maxAttempts = 8;
            
            // Próbuj przesunąć generator w różnych kierunkach
            const std::vector<glm::vec2> offsetDirs = {
                {0.0F, 0.0F},    // Centrum
                {1.5F, 0.0F},    // Prawo
                {-1.5F, 0.0F},   // Lewo
                {0.0F, 1.5F},    // Przód
                {0.0F, -1.5F},   // Tył
                {1.0F, 1.0F},
                {-1.0F, 1.0F},
                {1.0F, -1.0F},
                {-1.0F, -1.0F}
            };
            
            for (int dirIdx = 0; dirIdx < static_cast<int>(offsetDirs.size()) && collisionFound; ++dirIdx)
            {
                const glm::vec3 testPos = glm::vec3{
                    loopPos.x + offsetDirs[dirIdx].x,
                    1.0F,
                    loopPos.z + offsetDirs[dirIdx].y
                };
                
                collisionFound = false;
                
                // Sprawdź kolizję z każdą ścianą
                for (const BoxSpawn& wall : map.walls)
                {
                    // AABB collision test
                    const glm::vec3 minA = testPos - effectiveHalfExtents;
                    const glm::vec3 maxA = testPos + effectiveHalfExtents;
                    const glm::vec3 minB = wall.center - wall.halfExtents;
                    const glm::vec3 maxB = wall.center + wall.halfExtents;
                    
                    if (minA.x < maxB.x && maxA.x > minB.x &&
                        minA.y < maxB.y && maxA.y > minB.y &&
                        minA.z < maxB.z && maxA.z > minB.z)
                    {
                        collisionFound = true;
                        break;
                    }
                }
                
                if (!collisionFound)
                {
                    genPos = testPos;
                }
                
                ++attempts;
            }
            
            map.generatorSpawns.push_back(genPos);
        }
    }

    // Always ensure exactly 5 generators (fill with center positions if needed)
    while (static_cast<int>(map.generatorSpawns.size()) < 5)
    {
        const float mapHalf = kGridSize * kTileSize * 0.5F;
        const glm::vec3 center{0.0F, 1.0F, 0.0F};
        // Spread around center
        const int idx = static_cast<int>(map.generatorSpawns.size());
        const float offset = 8.0F * static_cast<float>(idx);
        const glm::vec3 pos = center + glm::vec3{
            (idx % 2 == 0 ? offset : -offset),
            0.0F,
            (idx % 3 == 0 ? offset : -offset)
        };
        map.generatorSpawns.push_back(pos);
    }

    if (settings.disableWindowsAndPallets)
    {
        map.windows.clear();
        map.pallets.clear();
    }

    return map;
}

GeneratedMap TileGenerator::GenerateCollisionTestMap() const
{
    GeneratedMap map;

    map.survivorSpawn = glm::vec3{-10.0F, 1.05F, 0.0F};
    map.killerSpawn = glm::vec3{-6.0F, 1.05F, 0.0F};

    map.walls.push_back(BoxSpawn{glm::vec3{0.0F, -0.5F, 0.0F}, glm::vec3{22.0F, 0.5F, 22.0F}});
    map.walls.push_back(BoxSpawn{glm::vec3{-8.0F, 1.0F, -3.2F}, glm::vec3{8.0F, 1.0F, 0.5F}});
    map.walls.push_back(BoxSpawn{glm::vec3{-8.0F, 1.0F, 3.2F}, glm::vec3{8.0F, 1.0F, 0.5F}});
    map.walls.push_back(BoxSpawn{glm::vec3{2.0F, 1.0F, 0.0F}, glm::vec3{0.5F, 1.0F, 3.0F}});
    map.windows.push_back(WindowSpawn{glm::vec3{-1.0F, 1.0F, 8.6F}, glm::vec3{0.9F, 1.0F, 0.18F}, glm::vec3{0.0F, 0.0F, 1.0F}});
    map.pallets.push_back(PalletSpawn{glm::vec3{3.0F, 0.6F, 7.2F}, glm::vec3{0.95F, 0.6F, 0.2F}});
    map.tiles.push_back(GeneratedMap::TileDebug{glm::vec3{0.0F}, glm::vec3{22.0F, 0.05F, 22.0F}, 0, 0});
    return map;
}

GeneratedMap TileGenerator::GenerateBenchmarkMap() const
{
    // ============================================================
    // COMPREHENSIVE BENCHMARK MAP
    // Tests: collision edge cases, rendering stress, AI scenarios
    // ============================================================
    GeneratedMap map;
    
    // --- Global layout: 100m x 100m arena ---
    constexpr float kMapHalf = 50.0F;
    
    // Spawn points at corners
    map.survivorSpawn = glm::vec3{-35.0F, 1.05F, -35.0F};
    map.killerSpawn = glm::vec3{35.0F, 1.05F, 35.0F};
    map.survivorSpawns = {
        glm::vec3{-35.0F, 1.05F, -35.0F},
        glm::vec3{-35.0F, 1.05F, 35.0F},
        glm::vec3{35.0F, 1.05F, -35.0F},
        glm::vec3{35.0F, 1.05F, 35.0F}
    };
    map.useDbdSpawns = true;
    
    // --- Ground plane ---
    map.walls.push_back(BoxSpawn{glm::vec3{0.0F, -0.5F, 0.0F}, glm::vec3{kMapHalf, 0.5F, kMapHalf}});
    
    // ============================================================
    // ZONE 1: CORNER CORRIDORS (collision precision test)
    // Tight corners test capsule-slide and step-up mechanics
    // ============================================================
    
    // Tight L-corridor at NW corner
    constexpr float corridorWidth = 1.2F;
    constexpr float wallThickness = 0.28F;
    
    // Outer walls of L-corridor
    map.walls.push_back(BoxSpawn{glm::vec3{-40.0F, 1.0F, -45.0F}, glm::vec3{10.0F, 1.0F, wallThickness / 2.0F}}); // Top
    map.walls.push_back(BoxSpawn{glm::vec3{-45.0F, 1.0F, -40.0F}, glm::vec3{wallThickness / 2.0F, 1.0F, 10.0F}}); // Left
    // Inner corner piece
    map.walls.push_back(BoxSpawn{glm::vec3{-40.0F + corridorWidth, 1.0F, -40.0F + corridorWidth}, glm::vec3{8.0F, 1.0F, wallThickness / 2.0F}});
    map.walls.push_back(BoxSpawn{glm::vec3{-40.0F + corridorWidth, 1.0F, -40.0F + corridorWidth}, glm::vec3{wallThickness / 2.0F, 1.0F, 8.0F}});
    
    // Window in L-corridor
    map.windows.push_back(WindowSpawn{
        glm::vec3{-42.0F, 1.0F, -42.0F},
        glm::vec3{0.9F, 1.0F, 0.18F},
        glm::vec3{-0.707F, 0.0F, 0.707F}
    });
    
    // Duplicate tight L-corridor at NE corner (mirrored)
    map.walls.push_back(BoxSpawn{glm::vec3{40.0F, 1.0F, -45.0F}, glm::vec3{10.0F, 1.0F, wallThickness / 2.0F}});
    map.walls.push_back(BoxSpawn{glm::vec3{45.0F, 1.0F, -40.0F}, glm::vec3{wallThickness / 2.0F, 1.0F, 10.0F}});
    map.walls.push_back(BoxSpawn{glm::vec3{40.0F - corridorWidth, 1.0F, -40.0F + corridorWidth}, glm::vec3{8.0F, 1.0F, wallThickness / 2.0F}});
    map.walls.push_back(BoxSpawn{glm::vec3{40.0F - corridorWidth, 1.0F, -40.0F + corridorWidth}, glm::vec3{wallThickness / 2.0F, 1.0F, 8.0F}});
    
    map.windows.push_back(WindowSpawn{
        glm::vec3{42.0F, 1.0F, -42.0F},
        glm::vec3{0.9F, 1.0F, 0.18F},
        glm::vec3{0.707F, 0.0F, 0.707F}
    });

    // Tight L-corridor at SW corner
    map.walls.push_back(BoxSpawn{glm::vec3{-40.0F, 1.0F, 45.0F}, glm::vec3{10.0F, 1.0F, wallThickness / 2.0F}});
    map.walls.push_back(BoxSpawn{glm::vec3{-45.0F, 1.0F, 40.0F}, glm::vec3{wallThickness / 2.0F, 1.0F, 10.0F}});
    map.walls.push_back(BoxSpawn{glm::vec3{-40.0F + corridorWidth, 1.0F, 40.0F - corridorWidth}, glm::vec3{8.0F, 1.0F, wallThickness / 2.0F}});
    map.walls.push_back(BoxSpawn{glm::vec3{-40.0F + corridorWidth, 1.0F, 40.0F - corridorWidth}, glm::vec3{wallThickness / 2.0F, 1.0F, 8.0F}});
    
    map.windows.push_back(WindowSpawn{
        glm::vec3{-42.0F, 1.0F, 42.0F},
        glm::vec3{0.9F, 1.0F, 0.18F},
        glm::vec3{-0.707F, 0.0F, -0.707F}
    });

    // Tight L-corridor at SE corner
    map.walls.push_back(BoxSpawn{glm::vec3{40.0F, 1.0F, 45.0F}, glm::vec3{10.0F, 1.0F, wallThickness / 2.0F}});
    map.walls.push_back(BoxSpawn{glm::vec3{45.0F, 1.0F, 40.0F}, glm::vec3{wallThickness / 2.0F, 1.0F, 10.0F}});
    map.walls.push_back(BoxSpawn{glm::vec3{40.0F - corridorWidth, 1.0F, 40.0F - corridorWidth}, glm::vec3{8.0F, 1.0F, wallThickness / 2.0F}});
    map.walls.push_back(BoxSpawn{glm::vec3{40.0F - corridorWidth, 1.0F, 40.0F - corridorWidth}, glm::vec3{wallThickness / 2.0F, 1.0F, 8.0F}});
    
    map.windows.push_back(WindowSpawn{
        glm::vec3{42.0F, 1.0F, 42.0F},
        glm::vec3{0.9F, 1.0F, 0.18F},
        glm::vec3{0.707F, 0.0F, -0.707F}
    });
    
    // ============================================================
    // ZONE 2: SPIRAL MAZE (collision stress test)
    // Continuous collision checks against many walls
    // ============================================================
    
    // Outer ring of spiral (center-left area)
    const glm::vec3 spiralCenter{-20.0F, 0.0F, 0.0F};
    constexpr float spiralOuterRadius = 12.0F;
    constexpr float spiralWallThickness = 0.3F;
    constexpr float spiralGap = 2.4F;
    
    // Circular-ish spiral made of segments
    for (int ring = 0; ring < 4; ++ring)
    {
        const float radius = spiralOuterRadius - ring * (spiralGap + spiralWallThickness);
        const int segments = static_cast<int>(radius * 2.5F) + 8; // More segments for outer rings
        const float arcPerSegment = glm::radians(360.0F / static_cast<float>(segments));
        
        for (int i = 0; i < segments; ++i)
        {
            // Skip a gap segment for entry/exit
            if (i == segments / (4 - ring)) // Different gap position per ring
                continue;
            
            const float angle1 = static_cast<float>(i) * arcPerSegment;
            const float angle2 = (static_cast<float>(i) + 0.9F) * arcPerSegment; // Slight gap between wall segments
            
            const glm::vec3 p1 = spiralCenter + glm::vec3{
                glm::cos(angle1) * radius,
                1.0F,
                glm::sin(angle1) * radius
            };
            const glm::vec3 p2 = spiralCenter + glm::vec3{
                glm::cos(angle2) * radius,
                1.0F,
                glm::sin(angle2) * radius
            };
            
            // Approximate wall segment with box
            const glm::vec3 wallCenter = (p1 + p2) * 0.5F;
            const float wallLength = glm::length(p2 - p1) * 0.5F + 0.1F;
            const float tangentAngle = std::atan2(p2.z - p1.z, p2.x - p1.x);
            
            // Simplified: use thin box along segment
            map.walls.push_back(BoxSpawn{
                wallCenter,
                glm::vec3{wallLength, 1.0F, spiralWallThickness * 0.5F}
            });
        }
    }
    
    // Add pallet inside spiral center
    map.pallets.push_back(PalletSpawn{
        spiralCenter + glm::vec3{0.0F, 0.6F, 0.0F},
        glm::vec3{0.95F, 0.6F, 0.2F}
    });
    
    // ============================================================
    // ZONE 3: STAIRCASE PYRAMID (step-up / gravity test)
    // Tiered platforms testing capsule step-up mechanics
    // ============================================================
    
    const glm::vec3 pyramidCenter{20.0F, 0.0F, 0.0F};
    constexpr float pyramidBaseSize = 16.0F;
    constexpr int pyramidTiers = 6;
    
    for (int tier = 0; tier < pyramidTiers; ++tier)
    {
        const float tierSize = pyramidBaseSize - tier * 2.5F;
        const float tierHeight = static_cast<float>(tier) * 0.5F;
        
        // Solid tier (as a thin floor piece)
        map.walls.push_back(BoxSpawn{
            pyramidCenter + glm::vec3{0.0F, tierHeight, 0.0F},
            glm::vec3{tierSize * 0.5F, 0.25F, tierSize * 0.5F}
        });
        
        // Small walls on edges for visual interest
        if (tier < pyramidTiers - 1)
        {
            // Corner pillars
            const float cornerOffset = tierSize * 0.45F;
            for (int corner = 0; corner < 4; ++corner)
            {
                const float cx = cornerOffset * ((corner % 2 == 0) ? 1.0F : -1.0F);
                const float cz = cornerOffset * ((corner < 2) ? 1.0F : -1.0F);
                map.walls.push_back(BoxSpawn{
                    pyramidCenter + glm::vec3{cx, tierHeight + 0.5F, cz},
                    glm::vec3{0.4F, 0.5F, 0.4F}
                });
            }
        }
    }
    
    // Window at top of pyramid for vault test
    map.windows.push_back(WindowSpawn{
        pyramidCenter + glm::vec3{0.0F, static_cast<float>(pyramidTiers) * 0.5F, 0.0F},
        glm::vec3{0.9F, 1.0F, 0.18F},
        glm::vec3{0.0F, 0.0F, 1.0F}
    });
    
    // ============================================================
    // ZONE 4: TIGHT PILLAR FOREST (rendering + collision pressure)
    // Many small obstacles to test broadphase and rendering
    // ============================================================
    
    const glm::vec3 forestCenter{0.0F, 0.0F, 25.0F};
    constexpr float forestRadius = 18.0F;
    constexpr int pillarRings = 4;
    constexpr int pillarsPerRing = 16;
    
    for (int ring = 1; ring <= pillarRings; ++ring)
    {
        const float ringRadius = forestRadius * static_cast<float>(ring) / static_cast<float>(pillarRings);
        const int pillarsThisRing = pillarsPerRing * ring;
        
        for (int i = 0; i < pillarsThisRing; ++i)
        {
            const float angle = static_cast<float>(i) * glm::radians(360.0F / static_cast<float>(pillarsThisRing));
            // Offset alternating rings for honeycomb pattern
            const float angleOffset = (ring % 2 == 0) ? glm::radians(360.0F / static_cast<float>(pillarsThisRing) * 0.5F) : 0.0F;
            
            const glm::vec3 pillarPos = forestCenter + glm::vec3{
                glm::cos(angle + angleOffset) * ringRadius,
                0.75F,
                glm::sin(angle + angleOffset) * ringRadius
            };
            
            // Small pillar (varied sizes for visual interest)
            const float pillarHalfWidth = 0.25F + 0.1F * static_cast<float>(ring % 3);
            map.walls.push_back(BoxSpawn{
                pillarPos,
                glm::vec3{pillarHalfWidth, 0.75F, pillarHalfWidth}
            });
        }
    }
    
    // Pallet in center of pillar forest
    map.pallets.push_back(PalletSpawn{
        forestCenter + glm::vec3{0.0F, 0.6F, 0.0F},
        glm::vec3{0.95F, 0.6F, 0.2F}
    });
    
    // ============================================================
    // ZONE 5: NARROW SLALOM (capsule slide test)
    // Zigzag walls forcing constant direction changes
    // ============================================================
    
    const glm::vec3 slalomStart{-20.0F, 0.0F, 30.0F};
    constexpr float slalomLength = 30.0F;
    constexpr int slalomGates = 10;
    constexpr float slalomGateWidth = 1.8F;
    constexpr float slalomGateThickness = 0.25F;
    
    for (int gate = 0; gate < slalomGates; ++gate)
    {
        const float zPos = slalomStart.z - static_cast<float>(gate) * (slalomLength / slalomGates);
        const float xOffset = (gate % 2 == 0) ? 2.0F : -2.0F;
        
        // Left wall of gate
        map.walls.push_back(BoxSpawn{
            slalomStart + glm::vec3{xOffset - slalomGateWidth, 1.0F, zPos},
            glm::vec3{slalomGateThickness, 1.0F, 1.5F}
        });
        
        // Right wall of gate
        map.walls.push_back(BoxSpawn{
            slalomStart + glm::vec3{xOffset + slalomGateWidth, 1.0F, zPos},
            glm::vec3{slalomGateThickness, 1.0F, 1.5F}
        });
        
        // Small connecting wall between gates for visual continuity
        if (gate > 0)
        {
            const float prevXOffset = ((gate - 1) % 2 == 0) ? 2.0F : -2.0F;
            map.walls.push_back(BoxSpawn{
                slalomStart + glm::vec3{(xOffset + prevXOffset) * 0.5F, 0.5F, zPos + (slalomLength / slalomGates) * 0.5F},
                glm::vec3{glm::abs(xOffset - prevXOffset) * 0.5F + 0.3F, 0.5F, slalomGateThickness}
            });
        }
    }
    
    // ============================================================
    // ZONE 6: DENSITY GRID (worst-case broadphase test)
    // Dense grid of small obstacles
    // ============================================================
    
    const glm::vec3 gridStart{25.0F, 0.0F, -25.0F};
    constexpr int gridSize = 12;
    constexpr float gridSpacing = 3.0F;
    constexpr float gridObstacleSize = 0.4F;
    
    for (int x = 0; x < gridSize; ++x)
    {
        for (int z = 0; z < gridSize; ++z)
        {
            // Skip some cells for paths
            if ((x + z) % 3 == 0)
                continue;
            
            const glm::vec3 obstaclePos = gridStart + glm::vec3{
                static_cast<float>(x) * gridSpacing,
                gridObstacleSize + 0.1F,
                static_cast<float>(z) * gridSpacing
            };
            
            // Varied heights
            const float heightVar = 0.3F + 0.4F * static_cast<float>((x * gridSize + z) % 4);
            
            map.walls.push_back(BoxSpawn{
                obstaclePos,
                glm::vec3{gridObstacleSize, heightVar, gridObstacleSize}
            });
        }
    }
    
    // ============================================================
    // ZONE 7: COMPLEX INTERSECTION (multi-vault scenario)
    // Central hub with multiple windows/pallets in close proximity
    // ============================================================
    
    const glm::vec3 hubCenter{0.0F, 0.0F, -20.0F};
    
    // Central platform
    map.walls.push_back(BoxSpawn{
        hubCenter + glm::vec3{0.0F, 0.25F, 0.0F},
        glm::vec3{6.0F, 0.25F, 6.0F}
    });
    
    // Four corner walls with vaults
    for (int corner = 0; corner < 4; ++corner)
    {
        const float angle = static_cast<float>(corner) * glm::radians(90.0F);
        const glm::vec3 wallDir = glm::vec3{glm::cos(angle), 0.0F, glm::sin(angle)};
        
        // Wall perpendicular to direction
        const glm::vec3 wallCenter = hubCenter + wallDir * 5.0F;
        const glm::vec3 wallTangent = glm::vec3{-wallDir.z, 0.0F, wallDir.x};
        
        map.walls.push_back(BoxSpawn{
            wallCenter - wallTangent * 2.0F,
            glm::vec3{1.8F, 1.0F, 0.28F}
        });
        map.walls.push_back(BoxSpawn{
            wallCenter + wallTangent * 2.0F,
            glm::vec3{1.8F, 1.0F, 0.28F}
        });
        
        // Window between wall segments
        map.windows.push_back(WindowSpawn{
            wallCenter,
            glm::vec3{0.9F, 1.0F, 0.18F},
            wallDir
        });
    }
    
    // Pallets around central hub
    for (int pallet = 0; pallet < 4; ++pallet)
    {
        const float angle = static_cast<float>(pallet) * glm::radians(90.0F) + glm::radians(45.0F);
        const glm::vec3 palletPos = hubCenter + glm::vec3{
            glm::cos(angle) * 8.0F,
            0.6F,
            glm::sin(angle) * 8.0F
        };
        
        map.pallets.push_back(PalletSpawn{
            palletPos,
            glm::vec3{0.95F, 0.6F, 0.2F}
        });
    }
    
    // ============================================================
    // ZONE 8: EDGE CASE CORNERS (V-shaped, acute angles)
    // Tests collision response at acute angles
    // ============================================================
    
    const glm::vec3 acuteCenter{35.0F, 0.0F, -35.0F};
    
    // V-shaped wall (acute 30-degree angle)
    for (int side = 0; side < 2; ++side)
    {
        const float baseAngle = glm::radians(side == 0 ? -60.0F : -120.0F);
        const glm::vec3 wallDir = glm::vec3{glm::cos(baseAngle), 0.0F, glm::sin(baseAngle)};
        
        for (int segment = 0; segment < 5; ++segment)
        {
            const glm::vec3 segCenter = acuteCenter + wallDir * (2.0F + static_cast<float>(segment) * 1.5F);
            map.walls.push_back(BoxSpawn{
                segCenter,
                glm::vec3{0.8F, 1.0F, 0.28F}
            });
        }
    }
    
    // Pallet at V-point
    map.pallets.push_back(PalletSpawn{
        acuteCenter + glm::vec3{0.0F, 0.6F, 2.0F},
        glm::vec3{0.95F, 0.6F, 0.2F}
    });
    
    // ============================================================
    // ZONE 9: MULTI-TIER PLATFORMS (elevation changes)
    // Tests falling/step-down and jumping mechanics
    // ============================================================
    
    const glm::vec3 tierCenter{-35.0F, 0.0F, 25.0F};
    constexpr int tierLevels = 4;
    
    for (int tier = 0; tier < tierLevels; ++tier)
    {
        const float tierHeight = static_cast<float>(tier) * 1.2F;
        const float tierSize = 8.0F - static_cast<float>(tier) * 1.5F;
        const float tierOffset = static_cast<float>(tier) * 3.0F;
        
        // Platform
        map.walls.push_back(BoxSpawn{
            tierCenter + glm::vec3{tierOffset, tierHeight + 0.2F, 0.0F},
            glm::vec3{tierSize, 0.2F, tierSize}
        });
        
        // Ramp connector (simplified as angled wall - just cosmetic)
        if (tier > 0)
        {
            map.walls.push_back(BoxSpawn{
                tierCenter + glm::vec3{tierOffset - 1.5F, tierHeight - 0.3F, 0.0F},
                glm::vec3{0.8F, 0.8F, 1.5F}
            });
        }
    }
    
    // ============================================================
    // ZONE 10: CHAOS SCATTER (random debris field)
    // Randomly placed obstacles for unpredictable navigation
    // ============================================================
    
    const glm::vec3 chaosCenter{0.0F, 0.0F, -40.0F};
    constexpr int chaosCount = 40;
    constexpr float chaosRadius = 15.0F;
    
    // Use deterministic "random" for reproducibility
    for (int i = 0; i < chaosCount; ++i)
    {
        // Simple pseudo-random from index
        const float angle = static_cast<float>(i * 137) * 0.0174533F; // ~prime multiple for distribution
        const float radius = chaosRadius * (0.3F + 0.7F * (static_cast<float>((i * 73) % 100) / 100.0F));
        
        const glm::vec3 debrisPos = chaosCenter + glm::vec3{
            glm::cos(angle) * radius,
            0.3F + 0.3F * static_cast<float>(i % 4),
            glm::sin(angle) * radius
        };
        
        const float debrisSize = 0.3F + 0.2F * static_cast<float>(i % 5);
        
        map.walls.push_back(BoxSpawn{
            debrisPos,
            glm::vec3{debrisSize, debrisSize * 0.8F, debrisSize}
        });
    }
    
    // ============================================================
    // ZONE 11: TUNNEL GALLERY (long corridor with side passages)
    // Tests LOS calculations and tight space movement
    // ============================================================
    
    const glm::vec3 tunnelStart{-45.0F, 0.0F, 0.0F};
    constexpr float tunnelLength = 30.0F;
    constexpr float tunnelWidth = 3.0F;
    
    // Ceiling for tunnel (simulated with overhead boxes)
    for (int section = 0; section < 10; ++section)
    {
        const float zPos = tunnelStart.z + static_cast<float>(section) * (tunnelLength / 10.0F);
        
        // Top wall
        map.walls.push_back(BoxSpawn{
            tunnelStart + glm::vec3{0.0F, 2.2F, zPos},
            glm::vec3{tunnelWidth, 0.3F, 1.4F}
        });
        
        // Side walls with gaps
        if (section % 2 == 0)
        {
            map.walls.push_back(BoxSpawn{
                tunnelStart + glm::vec3{-tunnelWidth, 1.0F, zPos},
                glm::vec3{0.3F, 1.0F, 1.4F}
            });
            map.walls.push_back(BoxSpawn{
                tunnelStart + glm::vec3{tunnelWidth, 1.0F, zPos},
                glm::vec3{0.3F, 1.0F, 1.4F}
            });
        }
        
        // Side passages every other section
        if (section % 3 == 0)
        {
            // Small alcove on each side
            map.walls.push_back(BoxSpawn{
                tunnelStart + glm::vec3{-tunnelWidth - 2.0F, 1.0F, zPos},
                glm::vec3{2.0F, 1.0F, 0.4F}
            });
            map.walls.push_back(BoxSpawn{
                tunnelStart + glm::vec3{tunnelWidth + 2.0F, 1.0F, zPos},
                glm::vec3{2.0F, 1.0F, 0.4F}
            });
        }
    }
    
    // Window at tunnel end
    map.windows.push_back(WindowSpawn{
        tunnelStart + glm::vec3{0.0F, 1.0F, tunnelStart.z + tunnelLength},
        glm::vec3{0.9F, 1.0F, 0.18F},
        glm::vec3{0.0F, 0.0F, 1.0F}
    });
    
    // ============================================================
    // ZONE 12: CONCENTRIC RINGS (radial LOS test)
    // Tests visibility calculation around curved obstacles
    // ============================================================
    
    const glm::vec3 ringsCenter{35.0F, 0.0F, 0.0F};
    constexpr float ringsOuterRadius = 15.0F;
    constexpr int ringCount = 3;
    
    for (int ring = 0; ring < ringCount; ++ring)
    {
        const float radius = ringsOuterRadius - static_cast<float>(ring) * 4.0F;
        const int arcSegments = static_cast<int>(radius * 2.0F);
        
        for (int seg = 0; seg < arcSegments; ++seg)
        {
            // Leave 4 gaps per ring at cardinal directions
            const float segAngle = static_cast<float>(seg) * glm::radians(360.0F / static_cast<float>(arcSegments));
            const float gapAngle = glm::radians(15.0F);
            bool nearGap = false;
            
            for (int gap = 0; gap < 4; ++gap)
            {
                const float gapCenter = static_cast<float>(gap) * glm::radians(90.0F);
                if (std::abs(segAngle - gapCenter) < gapAngle || std::abs(segAngle - gapCenter - 2.0F * glm::pi<float>()) < gapAngle)
                {
                    nearGap = true;
                    break;
                }
            }
            
            if (nearGap)
                continue;
            
            const glm::vec3 segCenter = ringsCenter + glm::vec3{
                glm::cos(segAngle) * radius,
                1.0F,
                glm::sin(segAngle) * radius
            };
            
            map.walls.push_back(BoxSpawn{
                segCenter,
                glm::vec3{0.5F, 1.0F, 0.5F}
            });
        }
    }
    
    // Pallet in center of rings
    map.pallets.push_back(PalletSpawn{
        ringsCenter + glm::vec3{0.0F, 0.6F, 0.0F},
        glm::vec3{0.95F, 0.6F, 0.2F}
    });
    
    // ============================================================
    // ZONE 13: BIASED STEPS (slanted surfaces test)
    // Series of angled walls testing projectile/climb mechanics
    // ============================================================
    
    const glm::vec3 stepsCenter{-15.0F, 0.0F, -35.0F};
    constexpr int stepsCount = 8;
    
    for (int step = 0; step < stepsCount; ++step)
    {
        const float stepAngle = glm::radians(15.0F * static_cast<float>(step) - 60.0F);
        const float stepHeight = 0.3F * static_cast<float>(step);
        const float stepOffset = static_cast<float>(step) * 2.0F;
        
        map.walls.push_back(BoxSpawn{
            stepsCenter + glm::vec3{stepOffset, stepHeight + 0.3F, 0.0F},
            glm::vec3{1.0F, 0.3F, 2.5F}
        });
    }
    
    // ============================================================
    // ZONE 14: BRIDGE CROSSING (gap traversal)
    // Narrow bridge over "pit" testing precision movement
    // ============================================================
    
    const glm::vec3 bridgeStart{10.0F, 0.0F, 40.0F};
    constexpr float bridgeLength = 20.0F;
    constexpr float bridgeWidth = 1.5F;
    
    // Bridge platform
    map.walls.push_back(BoxSpawn{
        bridgeStart + glm::vec3{0.0F, 0.2F, 0.0F},
        glm::vec3{bridgeWidth, 0.2F, bridgeLength}
    });
    
    // Railings (thin walls on sides)
    for (int rail = 0; rail < 2; ++rail)
    {
        const float xOffset = (rail == 0) ? bridgeWidth : -bridgeWidth;
        for (int seg = 0; seg < 10; ++seg)
        {
            // Leave gaps in railing
            if (seg % 3 == 1)
                continue;
            
            const float zPos = -static_cast<float>(seg) * (bridgeLength / 10.0F);
            map.walls.push_back(BoxSpawn{
                bridgeStart + glm::vec3{xOffset, 0.8F, zPos},
                glm::vec3{0.15F, 0.6F, bridgeLength / 10.0F * 0.8F}
            });
        }
    }
    
    // Platforms at bridge ends
    map.walls.push_back(BoxSpawn{
        bridgeStart + glm::vec3{0.0F, 0.2F, -bridgeLength - 2.0F},
        glm::vec3{4.0F, 0.2F, 3.0F}
    });
    map.walls.push_back(BoxSpawn{
        bridgeStart + glm::vec3{0.0F, 0.2F, 3.0F},
        glm::vec3{4.0F, 0.2F, 3.0F}
    });
    
    // ============================================================
    // ZONE 15: PALLET GALLERY (rapid pallet cycling test)
    // Many pallets in close proximity for interaction stress
    // ============================================================
    
    const glm::vec3 palletCenter{20.0F, 0.0F, 30.0F};
    constexpr int palletGrid = 3;
    constexpr float palletSpacing = 5.0F;
    
    for (int px = 0; px < palletGrid; ++px)
    {
        for (int pz = 0; pz < palletGrid; ++pz)
        {
            const glm::vec3 pos = palletCenter + glm::vec3{
                static_cast<float>(px - 1) * palletSpacing,
                0.6F,
                static_cast<float>(pz - 1) * palletSpacing
            };
            
            map.pallets.push_back(PalletSpawn{
                pos,
                glm::vec3{0.95F, 0.6F, 0.2F}
            });
            
            // Small wall behind each pallet
            map.walls.push_back(BoxSpawn{
                pos + glm::vec3{0.0F, 1.0F, 1.5F},
                glm::vec3{1.2F, 1.0F, 0.28F}
            });
        }
    }
    
    // ============================================================
    // ZONE 16: HIGH-POLY GARDEN (GPU stress test)
    // High-polygon meshes to tank FPS and test GPU limits
    // ============================================================
    
    // High-poly icosphere cluster (center of map, highly visible)
    // Icosphere detail levels: 4=~2.5k tris, 5=~10k tris, 6=~40k tris
    const glm::vec3 highPolyCenter{0.0F, 0.0F, 0.0F};
    
    // Central massive icosphere (detail 6 = ~40k triangles)
    map.highPolyMeshes.push_back(HighPolyMeshSpawn{
        highPolyCenter + glm::vec3{0.0F, 3.0F, 0.0F},
        glm::vec3{0.0F, 0.0F, 0.0F},
        glm::vec3{2.5F, 2.5F, 2.5F},
        glm::vec3{0.7F, 0.4F, 0.3F},
        HighPolyMeshSpawn::Type::IcoSphere,
        6,  // ~40,960 triangles
        true
    });
    
    // Surrounding ring of high-poly toruses (detail 5 = ~10k tris each)
    for (int ring = 0; ring < 8; ++ring)
    {
        const float angle = static_cast<float>(ring) * glm::radians(45.0F);
        const glm::vec3 torusPos = highPolyCenter + glm::vec3{
            glm::cos(angle) * 8.0F,
            1.5F + static_cast<float>(ring % 2) * 0.5F,
            glm::sin(angle) * 8.0F
        };
        
        map.highPolyMeshes.push_back(HighPolyMeshSpawn{
            torusPos,
            glm::vec3{0.0F, glm::degrees(angle), 0.0F},
            glm::vec3{0.8F, 0.8F, 0.8F},
            glm::vec3{0.4F, 0.5F + static_cast<float>(ring) * 0.05F, 0.6F},
            HighPolyMeshSpawn::Type::Torus,
            5,  // ~10k triangles each
            true
        });
    }
    
    // Grid planes removed — they looked like grass and added ~64k triangles
    // with no meaningful collision or gameplay purpose.
    
    // Spiral staircases (many small steps = many triangles)
    for (int stair = 0; stair < 2; ++stair)
    {
        const float stairAngle = static_cast<float>(stair) * glm::radians(180.0F) + glm::radians(45.0F);
        const glm::vec3 stairPos = highPolyCenter + glm::vec3{
            glm::cos(stairAngle) * 12.0F,
            0.0F,
            glm::sin(stairAngle) * 12.0F
        };
        
        map.highPolyMeshes.push_back(HighPolyMeshSpawn{
            stairPos,
            glm::vec3{0.0F, glm::degrees(stairAngle), 0.0F},
            glm::vec3{1.2F, 1.2F, 1.2F},
            glm::vec3{0.5F, 0.4F, 0.3F},
            HighPolyMeshSpawn::Type::SpiralStair,
            5,  // 64 steps = many triangles
            true
        });
    }
    
    // Additional stress test: small high-poly spheres scattered
    for (int scatter = 0; scatter < 16; ++scatter)
    {
        const float scatterAngle = static_cast<float>(scatter) * glm::radians(22.5F);
        const float scatterRadius = 6.0F + static_cast<float>(scatter % 4) * 2.0F;
        const glm::vec3 scatterPos = highPolyCenter + glm::vec3{
            glm::cos(scatterAngle) * scatterRadius,
            0.8F + static_cast<float>(scatter % 3) * 0.4F,
            glm::sin(scatterAngle) * scatterRadius
        };
        
        map.highPolyMeshes.push_back(HighPolyMeshSpawn{
            scatterPos,
            glm::vec3{0.0F, 0.0F, 0.0F},
            glm::vec3{0.4F, 0.4F, 0.4F},
            glm::vec3{0.6F, 0.7F, 0.9F},
            HighPolyMeshSpawn::Type::IcoSphere,
            5,  // ~10k triangles each
            true
        });
    }
    
    // Collision pedestal for high-poly garden (so player can walk around)
    // Raised above ground to avoid z-fighting with main floor
    map.walls.push_back(BoxSpawn{
        highPolyCenter + glm::vec3{0.0F, 0.15F, 0.0F},
        glm::vec3{20.0F, 0.15F, 20.0F}
    });
    
    // Small collision boxes around each torus (cosmetic collision)
    for (int ring = 0; ring < 8; ++ring)
    {
        const float angle = static_cast<float>(ring) * glm::radians(45.0F);
        const glm::vec3 torusPos = highPolyCenter + glm::vec3{
            glm::cos(angle) * 8.0F,
            1.5F,
            glm::sin(angle) * 8.0F
        };
        
        map.walls.push_back(BoxSpawn{
            torusPos,
            glm::vec3{1.2F, 1.2F, 1.2F}
        });
    }
    
    // ============================================================
    // GENERATOR PLACEMENTS (5 generators scattered)
    // ============================================================
    
    map.generatorSpawns = {
        glm::vec3{-30.0F, 1.0F, -10.0F},
        glm::vec3{30.0F, 1.0F, -15.0F},
        glm::vec3{0.0F, 1.0F, 10.0F},
        glm::vec3{-25.0F, 1.0F, 35.0F},
        glm::vec3{25.0F, 1.0F, 25.0F}
    };
    
    // ============================================================
    // TILE DEBUG INFO (for visualization)
    // ============================================================
    
    // Add tile markers for key zones
    map.tiles.push_back(GeneratedMap::TileDebug{glm::vec3{-20.0F, 0.05F, 0.0F}, glm::vec3{14.0F, 0.05F, 14.0F}, 1, 0});   // Spiral
    map.tiles.push_back(GeneratedMap::TileDebug{glm::vec3{20.0F, 0.05F, 0.0F}, glm::vec3{9.0F, 0.05F, 9.0F}, 2, 0});      // Pyramid
    map.tiles.push_back(GeneratedMap::TileDebug{glm::vec3{0.0F, 0.05F, 25.0F}, glm::vec3{20.0F, 0.05F, 20.0F}, 3, 0});    // Pillars
    map.tiles.push_back(GeneratedMap::TileDebug{glm::vec3{-20.0F, 0.05F, 30.0F}, glm::vec3{18.0F, 0.05F, 18.0F}, 4, 0});  // Slalom
    map.tiles.push_back(GeneratedMap::TileDebug{glm::vec3{25.0F, 0.05F, -25.0F}, glm::vec3{20.0F, 0.05F, 20.0F}, 5, 0});  // Grid
    map.tiles.push_back(GeneratedMap::TileDebug{glm::vec3{0.0F, 0.05F, -20.0F}, glm::vec3{10.0F, 0.05F, 10.0F}, 6, 0});   // Hub
    map.tiles.push_back(GeneratedMap::TileDebug{glm::vec3{35.0F, 0.05F, -35.0F}, glm::vec3{8.0F, 0.05F, 8.0F}, 7, 0});    // Acute
    map.tiles.push_back(GeneratedMap::TileDebug{glm::vec3{-35.0F, 0.05F, 25.0F}, glm::vec3{12.0F, 0.05F, 12.0F}, 8, 0});  // Tiers
    map.tiles.push_back(GeneratedMap::TileDebug{glm::vec3{0.0F, 0.05F, -40.0F}, glm::vec3{15.0F, 0.05F, 8.0F}, 9, 0});    // Chaos
    map.tiles.push_back(GeneratedMap::TileDebug{glm::vec3{35.0F, 0.05F, 0.0F}, glm::vec3{16.0F, 0.05F, 16.0F}, 10, 0});   // Rings
    map.tiles.push_back(GeneratedMap::TileDebug{glm::vec3{0.0F, 0.05F, 0.0F}, glm::vec3{20.0F, 0.05F, 20.0F}, 11, 0});    // High-Poly Garden
    
    return map;
}

void TileGenerator::CalculateDbdSpawns(GeneratedMap& map, unsigned int seed) const
{
    using namespace game::gameplay;
    
    // Build spawn points from tile centers
    std::vector<SpawnPoint> killerSpawnPoints;
    std::vector<SpawnPoint> survivorSpawnPoints;
    
    // Extract tile centers from debug tiles
    std::vector<glm::vec3> tileCenters;
    for (const auto& tile : map.tiles)
    {
        tileCenters.push_back(tile.center);
    }
    
    // Calculate map bounds
    game::gameplay::MapBounds bounds;
    if (!tileCenters.empty())
    {
        glm::vec3 minPos = tileCenters[0];
        glm::vec3 maxPos = tileCenters[0];
        
        for (const auto& center : tileCenters)
        {
            minPos.x = std::min(minPos.x, center.x);
            minPos.z = std::min(minPos.z, center.z);
            maxPos.x = std::max(maxPos.x, center.x);
            maxPos.z = std::max(maxPos.z, center.z);
        }
        
        bounds.center = (minPos + maxPos) * 0.5F;
        bounds.maxDistanceFromCenter = 0.0F;
        for (const auto& center : tileCenters)
        {
            glm::vec2 diff = glm::vec2(center.x, center.z) - glm::vec2(bounds.center.x, bounds.center.z);
            float dist = glm::length(diff);
            bounds.maxDistanceFromCenter = std::max(bounds.maxDistanceFromCenter, dist);
        }
    }
    
    // Generate killer spawn points from tile centers
    killerSpawnPoints = SpawnPointGenerator::GenerateKillerSpawns(tileCenters, bounds);
    
    // Build generator locations
    std::vector<GeneratorLocation> generators;
    for (size_t i = 0; i < map.generatorSpawns.size(); ++i)
    {
        generators.push_back(GeneratorLocation{map.generatorSpawns[i], static_cast<int>(i)});
    }
    
    // Generate survivor spawn points
    survivorSpawnPoints = SpawnPointGenerator::GenerateSurvivorSpawns(tileCenters, generators, bounds);
    
    // Calculate spawns using DBD-inspired system
    SpawnCalculator calculator;
    SpawnOfferings offerings; // Use default (clustered) spawn mode
    SpawnResult result = calculator.CalculateSpawns(killerSpawnPoints, survivorSpawnPoints, generators, offerings, seed);
    
    // Apply results to map
    map.killerSpawn = result.killerSpawn;
    map.survivorSpawns.clear();
    for (const auto& spawn : result.survivorSpawns)
    {
        map.survivorSpawns.push_back(spawn);
    }
    
    // For backward compatibility, set single survivor spawn to first position
    if (!map.survivorSpawns.empty())
    {
        map.survivorSpawn = map.survivorSpawns[0];
    }
    
    // Enable DBD spawn system
    map.useDbdSpawns = true;
}

} // namespace game::maps
