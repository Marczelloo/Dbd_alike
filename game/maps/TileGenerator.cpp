#include "game/maps/TileGenerator.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <random>
#include <string>
#include <unordered_map>
#include <vector>

#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <glm/trigonometric.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

namespace game::maps
{
namespace
{
constexpr int kGridSize = 8;
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
    AddWall(layout, LocalPoint{10.0F, 2.0F}, LocalPoint{10.0F, 10.0F});
    AddWindow(layout, LocalPoint{2.0F, 8.0F});
    AddPallet(layout, LocalPoint{10.0F, 2.0F});
    return layout;
}

[[nodiscard]] StructureLayout BuildJungleGymShortLayout()
{
    StructureLayout layout;
    AddWall(layout, LocalPoint{6.0F, 4.0F}, LocalPoint{10.0F, 4.0F});
    AddWall(layout, LocalPoint{4.0F, 8.0F}, LocalPoint{12.0F, 8.0F});
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
    AddWall(layout, LocalPoint{10.0F, 2.0F}, LocalPoint{10.0F, 14.0F});
    AddWall(layout, LocalPoint{14.0F, 2.0F}, LocalPoint{14.0F, 14.0F});
    AddWindow(layout, LocalPoint{6.0F, 8.0F});
    AddPallet(layout, LocalPoint{10.0F, 4.0F});
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
    AddWall(layout, LocalPoint{4.0F, 8.0F}, LocalPoint{12.0F, 8.0F});
    AddPallet(layout, LocalPoint{13.0F, 8.0F});
    return layout;
}

[[nodiscard]] bool IsMazeArchetype(TileArchetype archetype)
{
    return archetype == TileArchetype::JungleGymLong ||
           archetype == TileArchetype::JungleGymShort ||
           archetype == TileArchetype::FourLane ||
           archetype == TileArchetype::LTWalls ||
           archetype == TileArchetype::Shack;
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
    };
    return PickWeightedArchetype(rng, weighted, TileArchetype::FillerA);
}

[[nodiscard]] const StructureLayout& LayoutForArchetype(
    TileArchetype archetype,
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
        case TileArchetype::JungleGymLong: return jungleLong;
        case TileArchetype::JungleGymShort: return jungleShort;
        case TileArchetype::LTWalls: return ltwalls;
        case TileArchetype::Shack: return shack;
        case TileArchetype::FourLane: return fourLane;
        case TileArchetype::FillerA: return fillerA;
        case TileArchetype::FillerB: return fillerB;
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
    forced.emplace(1 * kGridSize + 1, TileArchetype::Shack);
    forced.emplace(6 * kGridSize + 6, TileArchetype::LTWalls);

    const std::vector<std::pair<TileArchetype, float>> allWeights{
        {TileArchetype::LTWalls, settings.weightTLWalls},
        {TileArchetype::JungleGymLong, settings.weightJungleGymLong},
        {TileArchetype::JungleGymShort, settings.weightJungleGymShort},
        {TileArchetype::Shack, settings.weightShack},
        {TileArchetype::FourLane, settings.weightFourLane},
        {TileArchetype::FillerA, settings.weightFillerA},
        {TileArchetype::FillerB, settings.weightFillerB},
    };

    std::vector<glm::ivec2> mazeTiles;
    int loopsPlaced = 0;
    const int maxLoops = std::max(0, settings.maxLoops);
    const float minLoopDistanceTiles = std::max(0.0F, settings.minLoopDistanceTiles);
    for (int z = 0; z < kGridSize; ++z)
    {
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
                if (loopsPlaced >= maxLoops)
                {
                    archetype = PickFillerArchetype(rng, settings);
                }

                const float nearestMaze = GetDistanceToNearestMaze(tileCoord, mazeTiles);
                if (nearestMaze < minLoopDistanceTiles)
                {
                    archetype = PickFillerArchetype(rng, settings);
                }
            }

            int rotation = PickRandomRotation(rng);
            if (archetype == TileArchetype::JungleGymShort)
            {
                rotation = PickShortLayoutRotationFacingCenter(jungleShort, tileCenter, rng);
            }

            const StructureLayout& base = LayoutForArchetype(archetype, jungleLong, jungleShort, ltwalls, shack, fourLane, fillerA, fillerB);
            const StructureLayout rotated = base.ApplyRotation(rotation);
            EmitLayout(map, rotated, tileCenter);

            if (IsMazeArchetype(archetype))
            {
                mazeTiles.push_back(tileCoord);
                ++loopsPlaced;
            }

            map.tiles.push_back(GeneratedMap::TileDebug{
                tileCenter,
                glm::vec3{kTileHalfExtent, 0.05F, kTileHalfExtent},
                key,
                static_cast<int>(archetype),
            });
        }
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
} // namespace game::maps
