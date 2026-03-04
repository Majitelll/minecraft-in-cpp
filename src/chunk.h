#pragma once

#include "textureatlas.h"
#include <functional>
#include <vector>
#include <cstdint>

static constexpr int CHUNK_SIZE   = 16;
static constexpr int CHUNK_HEIGHT = 64;
static constexpr int RENDER_DIST  = 8;

enum class BlockType : uint8_t {
    Air     = 0,
    Bedrock = 1,
    Stone   = 2,
    Dirt    = 3,
    Grass   = 4,
    Wood    = 5,
    Leaves  = 6,
    Snow    = 7,
};

enum class Biome : uint8_t {
    Plains    = 0,
    Hills     = 1,
    Mountains = 2,
};

struct Chunk {
    BlockType blocks[CHUNK_SIZE][CHUNK_HEIGHT][CHUNK_SIZE]{};

    void generateTerrain(int chunkX, int chunkZ, unsigned int seed = 42);

    using NeighborFn = std::function<Chunk*(int chunkX, int chunkZ)>;
    void generateDecorations(int chunkX, int chunkZ,
                              unsigned int seed,
                              const NeighborFn& getNeighbor);

    // neighbors[dx+1][dz+1] for dx,dz in {-1,0,1}
    // neighbors[1][1] is this chunk (can be nullptr for edge chunks)
    // Used for inter-chunk face culling
    std::vector<float> buildMesh(int chunkX, int chunkZ,
                                 Chunk* neighbors[3][3] = nullptr) const;

    bool isSolid(int x, int y, int z) const;

    // Returns solid state checking neighbor chunks if x/z out of bounds
    bool isSolidWorld(int x, int y, int z, Chunk* neighbors[3][3]) const;

    static void setWorldBlock(int wx, int wy, int wz, BlockType type,
                              int chunkX, int chunkZ,
                              const NeighborFn& getNeighbor);
};
