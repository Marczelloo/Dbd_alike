#pragma once

#include <vector>

#include <glm/vec3.hpp>

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

struct GeneratedMap
{
    struct TileDebug
    {
        glm::vec3 center{0.0F};
        glm::vec3 halfExtents{5.0F, 0.05F, 5.0F};
        int loopId = 0;
        int archetype = 0;
    };

    std::vector<BoxSpawn> walls;
    std::vector<WindowSpawn> windows;
    std::vector<PalletSpawn> pallets;
    std::vector<TileDebug> tiles;
    glm::vec3 survivorSpawn{0.0F};
    glm::vec3 killerSpawn{0.0F};
};

class TileGenerator
{
public:
    struct GenerationSettings
    {
        float weightTLWalls = 1.0F;
        float weightJungleGymLong = 1.0F;
        float weightJungleGymShort = 1.0F;
        float weightShack = 1.0F;
        float weightFourLane = 1.0F;
        float weightFillerA = 1.0F;
        float weightFillerB = 1.0F;
        int maxLoops = 40;
        float minLoopDistanceTiles = 2.0F;
    };

    GeneratedMap GenerateTestMap() const;
    GeneratedMap GenerateMainMap(unsigned int seed) const;
    GeneratedMap GenerateMainMap(unsigned int seed, const GenerationSettings& settings) const;
    GeneratedMap GenerateCollisionTestMap() const;
};
} // namespace game::maps
