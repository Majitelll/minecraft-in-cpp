#pragma once

#include <vector>
#include <cstdint>

static constexpr int CHUNK_SIZE = 16;

enum class BlockType : uint8_t {
    Air     = 0,
    Bedrock = 1,
    Stone   = 2,
    Dirt    = 3,
    Grass   = 4,
};

struct Chunk {
    BlockType blocks[CHUNK_SIZE][CHUNK_SIZE][CHUNK_SIZE]{};

    void generate(unsigned int seed = 42);

    // Returns interleaved x y z r g b floats (6 floats per vertex)
    std::vector<float> buildMesh() const;

    bool isSolid(int x, int y, int z) const;
};
