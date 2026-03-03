#include "chunk.h"
#include <cmath>

// ─────────────────────────────────────────────────────────────────────────────
// Fractal noise heightmap
// ─────────────────────────────────────────────────────────────────────────────
static float hashf(int x, int z, unsigned int seed) {
    unsigned int h = (unsigned int)(x * 1619 + z * 31337 + seed * 6971);
    h = (h ^ (h >> 16)) * 0x45d9f3b;
    h = (h ^ (h >> 16)) * 0x45d9f3b;
    h ^= (h >> 16);
    return (float)(h & 0xFFFF) / 65535.f;
}

static float smoothNoise(float x, float z, unsigned int seed) {
    int   ix = (int)floorf(x), iz = (int)floorf(z);
    float fx = x - (float)ix,  fz = z - (float)iz;
    float ux = fx*fx*(3.f-2.f*fx), uz = fz*fz*(3.f-2.f*fz);
    float a = hashf(ix,   iz,   seed);
    float b = hashf(ix+1, iz,   seed);
    float c = hashf(ix,   iz+1, seed);
    float d = hashf(ix+1, iz+1, seed);
    return a*(1-ux)*(1-uz) + b*ux*(1-uz) + c*(1-ux)*uz + d*ux*uz;
}

static float fractalNoise(float x, float z, unsigned int seed) {
    float val=0.f, amp=1.f, freq=1.f, total=0.f;
    for (int i=0; i<4; i++) {
        val   += smoothNoise(x*freq, z*freq, seed+i*1000) * amp;
        total += amp; amp *= 0.5f; freq *= 2.f;
    }
    return val / total;
}

// ─────────────────────────────────────────────────────────────────────────────
// Block face colors
// ─────────────────────────────────────────────────────────────────────────────
struct FaceColor { float r, g, b; };

static FaceColor blockColor(BlockType type, int face) {
    bool isTop = (face == 2), isBottom = (face == 3);
    switch (type) {
        case BlockType::Grass:
            if (isTop)    return {0.27f, 0.62f, 0.18f};
            if (isBottom) return {0.42f, 0.27f, 0.13f};
            return            {0.35f, 0.50f, 0.20f};
        case BlockType::Dirt:
            if (isTop)    return {0.45f, 0.30f, 0.15f};
            if (isBottom) return {0.35f, 0.22f, 0.10f};
            return            {0.42f, 0.27f, 0.13f};
        case BlockType::Stone:
            if (isTop)    return {0.60f, 0.60f, 0.60f};
            if (isBottom) return {0.45f, 0.45f, 0.45f};
            return            {0.55f, 0.55f, 0.55f};
        case BlockType::Bedrock:
            return {0.10f, 0.10f, 0.10f};
        default:
            return {1.f, 0.f, 1.f};
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Face geometry
// ─────────────────────────────────────────────────────────────────────────────
static const float kFaceVerts[6][18] = {
    { 0.5f,-0.5f,-0.5f,  0.5f, 0.5f,-0.5f,  0.5f, 0.5f, 0.5f,  0.5f, 0.5f, 0.5f,  0.5f,-0.5f, 0.5f,  0.5f,-0.5f,-0.5f},
    {-0.5f,-0.5f, 0.5f, -0.5f, 0.5f, 0.5f, -0.5f, 0.5f,-0.5f, -0.5f, 0.5f,-0.5f, -0.5f,-0.5f,-0.5f, -0.5f,-0.5f, 0.5f},
    {-0.5f, 0.5f,-0.5f, -0.5f, 0.5f, 0.5f,  0.5f, 0.5f, 0.5f,  0.5f, 0.5f, 0.5f,  0.5f, 0.5f,-0.5f, -0.5f, 0.5f,-0.5f},
    {-0.5f,-0.5f, 0.5f, -0.5f,-0.5f,-0.5f,  0.5f,-0.5f,-0.5f,  0.5f,-0.5f,-0.5f,  0.5f,-0.5f, 0.5f, -0.5f,-0.5f, 0.5f},
    {-0.5f,-0.5f, 0.5f,  0.5f,-0.5f, 0.5f,  0.5f, 0.5f, 0.5f,  0.5f, 0.5f, 0.5f, -0.5f, 0.5f, 0.5f, -0.5f,-0.5f, 0.5f},
    { 0.5f,-0.5f,-0.5f, -0.5f,-0.5f,-0.5f, -0.5f, 0.5f,-0.5f, -0.5f, 0.5f,-0.5f,  0.5f, 0.5f,-0.5f,  0.5f,-0.5f,-0.5f}
};
static const int kNeighbors[6][3] = {{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}};

// ─────────────────────────────────────────────────────────────────────────────
bool Chunk::isSolid(int x, int y, int z) const {
    if (x<0||x>=CHUNK_SIZE||y<0||y>=CHUNK_SIZE||z<0||z>=CHUNK_SIZE) return false;
    return blocks[x][y][z] != BlockType::Air;
}

void Chunk::generate(int chunkX, int chunkZ, unsigned int seed) {
    for (int x=0;x<CHUNK_SIZE;x++)
        for (int y=0;y<CHUNK_SIZE;y++)
            for (int z=0;z<CHUNK_SIZE;z++)
                blocks[x][y][z] = BlockType::Air;

    // World-space column position for seamless noise across chunk borders
    for (int x=0; x<CHUNK_SIZE; x++) {
        for (int z=0; z<CHUNK_SIZE; z++) {
            int wx = chunkX * CHUNK_SIZE + x;
            int wz = chunkZ * CHUNK_SIZE + z;
            float nx = (float)wx / ((float)CHUNK_SIZE * 3.f);
            float nz = (float)wz / ((float)CHUNK_SIZE * 3.f);
            float n  = fractalNoise(nx, nz, seed);
            int height = 4 + (int)(n * 10.f);
            if (height >= CHUNK_SIZE) height = CHUNK_SIZE - 1;
            for (int y=0; y<=height; y++) {
                if (y == 0)              blocks[x][y][z] = BlockType::Bedrock;
                else if (y == height)    blocks[x][y][z] = BlockType::Grass;
                else if (y >= height-3)  blocks[x][y][z] = BlockType::Dirt;
                else                     blocks[x][y][z] = BlockType::Stone;
            }
        }
    }
}

std::vector<float> Chunk::buildMesh(int chunkX, int chunkZ) const {
    std::vector<float> verts;
    float offX = (float)(chunkX * CHUNK_SIZE);
    float offZ = (float)(chunkZ * CHUNK_SIZE);

    for (int x=0; x<CHUNK_SIZE; x++) {
        for (int y=0; y<CHUNK_SIZE; y++) {
            for (int z=0; z<CHUNK_SIZE; z++) {
                BlockType bt = blocks[x][y][z];
                if (bt == BlockType::Air) continue;
                for (int f=0; f<6; f++) {
                    int nx=x+kNeighbors[f][0], ny=y+kNeighbors[f][1], nz=z+kNeighbors[f][2];
                    if (isSolid(nx,ny,nz)) continue;
                    FaceColor col = blockColor(bt, f);
                    for (int v=0; v<18; v+=3) {
                        verts.push_back(kFaceVerts[f][v]   + (float)x + offX);
                        verts.push_back(kFaceVerts[f][v+1] + (float)y);
                        verts.push_back(kFaceVerts[f][v+2] + (float)z + offZ);
                        verts.push_back(col.r);
                        verts.push_back(col.g);
                        verts.push_back(col.b);
                    }
                }
            }
        }
    }
    return verts;
}
