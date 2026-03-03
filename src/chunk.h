#pragma once

#include <vector>
#include <cstdint>

static constexpr int CHUNK_SIZE   = 16;
static constexpr int RENDER_DIST  = 4;   // chunks in each direction

enum class BlockType : uint8_t {
    Air     = 0,
    Bedrock = 1,
    Stone   = 2,
    Dirt    = 3,
    Grass   = 4,
};

struct Chunk {
    BlockType blocks[CHUNK_SIZE][CHUNK_SIZE][CHUNK_SIZE]{};

    // chunkX/chunkZ are integer chunk coords (not world-space pixels)
    void generate(int chunkX, int chunkZ, unsigned int seed = 42);

    // Returns interleaved x y z r g b floats (6 floats per vertex)
    // worldX/worldZ are applied as offsets so vertices are in world space
    std::vector<float> buildMesh(int chunkX, int chunkZ) const;

    bool isSolid(int x, int y, int z) const;
};
