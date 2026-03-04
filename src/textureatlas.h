#pragma once
#include <vector>
#include <cstdint>

enum class TileID : int {
    GrassTop   = 0,
    GrassSide  = 1,
    Dirt       = 2,
    Stone      = 3,
    Bedrock    = 4,
    WoodTop    = 5,
    WoodSide   = 6,
    Leaves     = 7,
    Snow       = 8,
    SnowSide   = 9,  // snow strip on top, stone below
    COUNT      = 10
};

static constexpr int TILE_SIZE  = 16;
static constexpr int ATLAS_COLS = 10;
static constexpr int ATLAS_ROWS = 1;
static constexpr int ATLAS_W    = TILE_SIZE * ATLAS_COLS;
static constexpr int ATLAS_H    = TILE_SIZE * ATLAS_ROWS;

std::vector<uint8_t> generateAtlas();

struct TileUV { float u0, v0, u1, v1; };
inline TileUV getTileUV(TileID id) {
    float tw = 1.f / (float)ATLAS_COLS;
    return { (int)id * tw, 0.f, (int)id * tw + tw, 1.f };
}
