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

        // Spread selected generators: first pick the most spread out subset
        if (static_cast<int>(selected.size()) > targetCount)
        {
            // Simple greedy spread: start with first, then pick furthest each time
            std::vector<glm::vec3> spread;
            spread.push_back(selected[0]);

            while (spread.size() < static_cast<std::size_t>(targetCount))
            {
                float bestDist = -1.0F;
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

                    // Find min distance to already selected
                    float minDist = std::numeric_limits<float>::max();
                    for (const glm::vec3& used : spread)
                    {
                        const float d = glm::length(candidate - used);
                        minDist = std::min(minDist, d);
                    }

                    if (minDist > bestDist)
                    {
                        bestDist = minDist;
                        bestIdx = i;
                    }
                }

                spread.push_back(selected[bestIdx]);
            }

            selected = spread;
        }

        // Add generator spawns (offset slightly from loop center to avoid clipping)
        for (const glm::vec3& loopPos : selected)
        {
            map.generatorSpawns.push_back(loopPos + glm::vec3{0.0F, 1.0F, 0.0F});
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
