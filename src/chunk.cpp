#include "chunk.h"
#include <cmath>
#include <functional>

// ─────────────────────────────────────────────────────────────────────────────
// Noise primitives
// ─────────────────────────────────────────────────────────────────────────────
static float hashf(int x, int z, unsigned int seed) {
    unsigned int h = (unsigned int)(x*1619 + z*31337 + seed*6971);
    h = (h^(h>>16))*0x45d9f3b;
    h = (h^(h>>16))*0x45d9f3b;
    h ^= (h>>16);
    return (float)(h&0xFFFF)/65535.f;
}

static float smoothNoise(float x, float z, unsigned int seed) {
    int ix=(int)floorf(x), iz=(int)floorf(z);
    float fx=x-(float)ix, fz=z-(float)iz;
    float ux=fx*fx*(3.f-2.f*fx), uz=fz*fz*(3.f-2.f*fz);
    return hashf(ix,  iz,  seed)*(1-ux)*(1-uz)
         + hashf(ix+1,iz,  seed)*ux*(1-uz)
         + hashf(ix,  iz+1,seed)*(1-ux)*uz
         + hashf(ix+1,iz+1,seed)*ux*uz;
}

static float fractalNoise(float x, float z, unsigned int seed, int octaves=4) {
    float val=0,amp=1,freq=1,total=0;
    for(int i=0;i<octaves;i++){
        val  +=smoothNoise(x*freq,z*freq,seed+i*1000)*amp;
        total+=amp; amp*=0.5f; freq*=2.f;
    }
    return val/total;
}

// ─────────────────────────────────────────────────────────────────────────────
// Biome
// ─────────────────────────────────────────────────────────────────────────────
static Biome getBiome(int wx, int wz, unsigned int seed) {
    // Very low frequency noise for large biome regions
    float bx = (float)wx / 256.f;
    float bz = (float)wz / 256.f;
    float n = fractalNoise(bx, bz, seed + 9999, 2);
    if (n < 0.35f) return Biome::Plains;
    if (n < 0.65f) return Biome::Hills;
    return Biome::Mountains;
}

// Returns terrain height (surface y) for a world column
static int getHeight(int wx, int wz, unsigned int seed) {
    // Sample biome at this column and neighbors for smooth blending
    // We blend over a small radius so biome transitions are gradual
    float totalWeight = 0.f;
    float totalHeight = 0.f;

    for (int dx = -4; dx <= 4; dx++) {
        for (int dz = -4; dz <= 4; dz++) {
            float weight = 1.f / (1.f + dx*dx + dz*dz);
            Biome b = getBiome(wx+dx, wz+dz, seed);

            float nx = (float)(wx+dx) / (float)(CHUNK_SIZE * 2.f);
            float nz = (float)(wz+dz) / (float)(CHUNK_SIZE * 2.f);
            float n  = fractalNoise(nx, nz, seed);

            float h;
            switch (b) {
                case Biome::Plains:
                    // Flat with slight variation, height 6-12
                    h = 6.f + fractalNoise(nx*0.5f, nz*0.5f, seed+1, 2) * 6.f;
                    break;
                case Biome::Hills:
                    // Moderate hills, height 8-24
                    h = 8.f + n * 16.f;
                    break;
                case Biome::Mountains:
                    // Dramatic peaks using ridged noise, height 12-52
                    {
                        // Ridged noise = 1 - |noise*2 - 1|, makes sharp peaks
                        float ridge = fractalNoise(nx*1.5f, nz*1.5f, seed+2, 6);
                        ridge = 1.f - fabsf(ridge * 2.f - 1.f);
                        ridge = ridge * ridge; // sharpen peaks
                        h = 12.f + ridge * 40.f;
                    }
                    break;
            }
            totalHeight += h * weight;
            totalWeight += weight;
        }
    }

    int height = (int)(totalHeight / totalWeight);
    if (height < 2) height = 2;
    if (height >= CHUNK_HEIGHT - 4) height = CHUNK_HEIGHT - 5;
    return height;
}

// ─────────────────────────────────────────────────────────────────────────────
// Block color
// ─────────────────────────────────────────────────────────────────────────────
// Returns the TileID for a given block type and face index
// face: 0=+X, 1=-X, 2=+Y(top), 3=-Y(bottom), 4=+Z, 5=-Z
static TileID blockTile(BlockType type, int face) {
    bool isTop    = (face == 2);
    bool isBottom = (face == 3);
    switch (type) {
        case BlockType::Grass:
            if (isTop)    return TileID::GrassTop;
            if (isBottom) return TileID::Dirt;
            return TileID::GrassSide;
        case BlockType::Dirt:    return TileID::Dirt;
        case BlockType::Stone:   return TileID::Stone;
        case BlockType::Bedrock: return TileID::Bedrock;
        case BlockType::Wood:
            if (isTop || isBottom) return TileID::WoodTop;
            return TileID::WoodSide;
        case BlockType::Leaves:  return TileID::Leaves;
        case BlockType::Snow:
            if (isTop)    return TileID::Snow;
            if (isBottom) return TileID::Stone;
            return TileID::SnowSide; // snow strip + stone sides
        default: return TileID::Stone;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Greedy meshing axis tables
// ─────────────────────────────────────────────────────────────────────────────
// Per-face: slice axis, u axis, v axis, face plane offset (0 or 1), normal sign
static const int GM_SA[6] = {0, 0, 1, 1, 2, 2};
static const int GM_UA[6] = {2, 2, 0, 0, 0, 0};
static const int GM_VA[6] = {1, 1, 2, 2, 1, 1};
static const int GM_PO[6] = {1, 0, 1, 0, 1, 0};  // plane coord = s + PO
static const int GM_NS[6] = {1,-1, 1,-1, 1,-1};   // neighbor direction along slice axis

// (ul_is_full, vl_is_full) for each of 6 vertices — matched to original kFaceVerts winding
// 0 = use 0, 1 = use uw or vw
static const int GM_UVP[6][6][2] = {
    {{0,0},{0,1},{1,1},{1,1},{1,0},{0,0}},  // +X
    {{1,0},{1,1},{0,1},{0,1},{0,0},{1,0}},  // -X
    {{0,0},{0,1},{1,1},{1,1},{1,0},{0,0}},  // +Y
    {{0,1},{0,0},{1,0},{1,0},{1,1},{0,1}},  // -Y
    {{0,0},{1,0},{1,1},{1,1},{0,1},{0,0}},  // +Z
    {{1,0},{0,0},{0,1},{0,1},{1,1},{1,0}},  // -Z
};

// ─────────────────────────────────────────────────────────────────────────────
bool Chunk::isSolid(int x, int y, int z) const {
    if(x<0||x>=CHUNK_SIZE||y<0||y>=CHUNK_HEIGHT||z<0||z>=CHUNK_SIZE) return false;
    return blocks[x][y][z] != BlockType::Air;
}

// ─────────────────────────────────────────────────────────────────────────────
// Pass 1: terrain
// ─────────────────────────────────────────────────────────────────────────────
void Chunk::generateTerrain(int chunkX, int chunkZ, unsigned int seed) {
    for(int x=0;x<CHUNK_SIZE;x++)
        for(int y=0;y<CHUNK_HEIGHT;y++)
            for(int z=0;z<CHUNK_SIZE;z++)
                blocks[x][y][z]=BlockType::Air;

    for(int x=0;x<CHUNK_SIZE;x++) {
        for(int z=0;z<CHUNK_SIZE;z++) {
            int wx = chunkX*CHUNK_SIZE+x;
            int wz = chunkZ*CHUNK_SIZE+z;
            int height = getHeight(wx, wz, seed);
            Biome biome = getBiome(wx, wz, seed);

            for(int y=0;y<=height;y++) {
                BlockType bt;
                bool isMountainSnow = (biome==Biome::Mountains && height >= 38);
                if(y==0) {
                    bt = BlockType::Bedrock;
                } else if(y==height) {
                    bt = isMountainSnow ? BlockType::Snow : BlockType::Grass;
                } else if(isMountainSnow) {
                    // Mountains with snow caps: stone all the way up, no dirt
                    bt = BlockType::Stone;
                } else if(y >= height-3) {
                    bt = BlockType::Dirt;
                } else {
                    bt = BlockType::Stone;
                }
                blocks[x][y][z] = bt;
            }
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// World-block setter (can write into neighbor chunks)
// ─────────────────────────────────────────────────────────────────────────────
void Chunk::setWorldBlock(int wx, int wy, int wz, BlockType type,
                          int chunkX, int chunkZ,
                          const NeighborFn& getNeighbor) {
    if(wy < 0 || wy >= CHUNK_HEIGHT) return;

    // Figure out which chunk owns this world position
    int targetChunkX = (int)floorf((float)wx / (float)CHUNK_SIZE);
    int targetChunkZ = (int)floorf((float)wz / (float)CHUNK_SIZE);
    int lx = wx - targetChunkX * CHUNK_SIZE;
    int lz = wz - targetChunkZ * CHUNK_SIZE;

    Chunk* target = getNeighbor(targetChunkX, targetChunkZ);
    if(!target) return;

    // Don't overwrite solid terrain blocks with leaves
    BlockType existing = target->blocks[lx][wy][lz];
    if(type == BlockType::Leaves && existing != BlockType::Air) return;

    target->blocks[lx][wy][lz] = type;
}

// ─────────────────────────────────────────────────────────────────────────────
// Tree planting helper
// ─────────────────────────────────────────────────────────────────────────────
static void plantTree(int wx, int groundY, int wz,
                      int chunkX, int chunkZ,
                      const Chunk::NeighborFn& getNeighbor) {
    int trunkHeight = 4 + (int)(hashf(wx, wz, 77777) * 3.f); // 4-6 tall

    // Trunk
    for(int y=1; y<=trunkHeight; y++)
        Chunk::setWorldBlock(wx, groundY+y, wz, BlockType::Wood, chunkX, chunkZ, getNeighbor);

    // Canopy — 3 layers
    int topY = groundY + trunkHeight;
    for(int layer=0; layer<3; layer++) {
        int radius = (layer==0) ? 2 : (layer==1) ? 2 : 1;
        int ly = topY - 1 + layer;
        for(int dx=-radius; dx<=radius; dx++) {
            for(int dz=-radius; dz<=radius; dz++) {
                // Clip corners for a rounder look
                if(abs(dx)==radius && abs(dz)==radius) continue;
                Chunk::setWorldBlock(wx+dx, ly, wz+dz, BlockType::Leaves,
                                     chunkX, chunkZ, getNeighbor);
            }
        }
    }
    // Top leaf cap
    Chunk::setWorldBlock(wx, topY+1, wz, BlockType::Leaves, chunkX, chunkZ, getNeighbor);
}

// ─────────────────────────────────────────────────────────────────────────────
// Pass 2: decorations (trees)
// ─────────────────────────────────────────────────────────────────────────────
void Chunk::generateDecorations(int chunkX, int chunkZ,
                                unsigned int seed,
                                const NeighborFn& getNeighbor) {
    for(int x=0; x<CHUNK_SIZE; x++) {
        for(int z=0; z<CHUNK_SIZE; z++) {
            int wx = chunkX*CHUNK_SIZE+x;
            int wz = chunkZ*CHUNK_SIZE+z;

            Biome biome = getBiome(wx, wz, seed);

            // No trees on mountains or snow
            if(biome == Biome::Mountains) continue;

            // Find surface y
            int surfaceY = -1;
            for(int y=CHUNK_HEIGHT-1; y>=1; y--) {
                if(blocks[x][y][z] != BlockType::Air) { surfaceY=y; break; }
            }
            if(surfaceY < 0) continue;
            if(blocks[x][surfaceY][z] != BlockType::Grass) continue;

            // Tree density varies by biome
            float baseDensity = (biome==Biome::Plains) ? 0.03f : 0.07f;

            // Cluster trees using a low-freq noise
            float clusterNoise = fractalNoise((float)wx/32.f, (float)wz/32.f, seed+5555, 2);
            float density = baseDensity * clusterNoise * 3.f;

            // Per-column random check
            float roll = hashf(wx, wz, seed + 1234);
            if(roll > density) continue;

            // Need at least 8 blocks of headroom for the tree
            if(surfaceY + 8 >= CHUNK_HEIGHT) continue;

            plantTree(wx, surfaceY, wz, chunkX, chunkZ, getNeighbor);
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Mesh building — with inter-chunk face culling
// ─────────────────────────────────────────────────────────────────────────────
bool Chunk::isSolidWorld(int x, int y, int z, Chunk* neighbors[3][3]) const {
    if (y < 0 || y >= CHUNK_HEIGHT) return false;

    // Within this chunk
    if (x >= 0 && x < CHUNK_SIZE && z >= 0 && z < CHUNK_SIZE)
        return blocks[x][y][z] != BlockType::Air;

    if (!neighbors) return false;

    // Map to neighbor chunk
    int cx = 1, cz = 1; // default = this chunk
    int lx = x, lz = z;

    if (x < 0)          { cx = 0; lx = x + CHUNK_SIZE; }
    else if (x >= CHUNK_SIZE) { cx = 2; lx = x - CHUNK_SIZE; }

    if (z < 0)          { cz = 0; lz = z + CHUNK_SIZE; }
    else if (z >= CHUNK_SIZE) { cz = 2; lz = z - CHUNK_SIZE; }

    Chunk* nb = neighbors[cx][cz];
    if (!nb) return false;
    return nb->blocks[lx][y][lz] != BlockType::Air;
}

std::vector<float> Chunk::buildMesh(int chunkX, int chunkZ, Chunk* neighbors[3][3]) const {
    std::vector<float> verts;
    float offX = (float)(chunkX * CHUNK_SIZE);
    float offZ = (float)(chunkZ * CHUNK_SIZE);

    // Chunk dimensions indexed by axis: 0=X, 1=Y, 2=Z
    static const int DIMS[3] = {CHUNK_SIZE, CHUNK_HEIGHT, CHUNK_SIZE};

    // Greedy merge mask: [u][v], max uDim=CHUNK_SIZE=16, max vDim=CHUNK_HEIGHT=64
    // -1 = empty or already processed; otherwise stores TileID
    int16_t mask[CHUNK_SIZE][CHUNK_HEIGHT];

    for (int f = 0; f < 6; f++) {
        const int sa    = GM_SA[f];
        const int ua    = GM_UA[f];
        const int va    = GM_VA[f];
        const int sliceDim = DIMS[sa];
        const int uDim     = DIMS[ua];
        const int vDim     = DIMS[va];

        for (int s = 0; s < sliceDim; s++) {
            // ── Build face mask ────────────────────────────────────────────
            for (int u = 0; u < uDim; u++) {
                for (int v = 0; v < vDim; v++) {
                    int c[3];
                    c[sa] = s; c[ua] = u; c[va] = v;
                    if (blocks[c[0]][c[1]][c[2]] == BlockType::Air) {
                        mask[u][v] = -1;
                        continue;
                    }
                    int nc[3] = {c[0], c[1], c[2]};
                    nc[sa] += GM_NS[f];
                    mask[u][v] = isSolidWorld(nc[0], nc[1], nc[2], neighbors)
                                 ? -1
                                 : (int16_t)blockTile(blocks[c[0]][c[1]][c[2]], f);
                }
            }

            // ── Greedy merge ───────────────────────────────────────────────
            for (int u = 0; u < uDim; u++) {
                for (int v = 0; v < vDim; ) {
                    if (mask[u][v] < 0) { v++; continue; }
                    const int16_t tileID = mask[u][v];

                    // Extend in v direction within current column u
                    int vw = 1;
                    while (v + vw < vDim && mask[u][v + vw] == tileID) vw++;

                    // Extend in u direction, requiring all rows v..v+vw-1 to match
                    int uw = 1;
                    bool ok = true;
                    while (u + uw < uDim && ok) {
                        for (int dv = 0; dv < vw; dv++) {
                            if (mask[u + uw][v + dv] != tileID) { ok = false; break; }
                        }
                        if (ok) uw++;
                    }

                    // Mark merged cells as processed
                    for (int du = 0; du < uw; du++)
                        for (int dv = 0; dv < vw; dv++)
                            mask[u + du][v + dv] = -1;

                    // Emit merged quad — 6 vertices, 6 floats each (x,y,z, uRaw,vRaw, tileBaseU)
                    // uRaw/vRaw go 0..uw/vw so the fragment shader can tile with fract()
                    const float tileBaseU = (float)(int)tileID / (float)ATLAS_COLS;
                    const int   planeCoord = s + GM_PO[f];

                    for (int vi = 0; vi < 6; vi++) {
                        const int ul = GM_UVP[f][vi][0] ? uw : 0;
                        const int vl = GM_UVP[f][vi][1] ? vw : 0;
                        int c2[3];
                        c2[sa] = planeCoord;
                        c2[ua] = u + ul;
                        c2[va] = v + vl;
                        verts.push_back((float)c2[0] + offX);
                        verts.push_back((float)c2[1]);
                        verts.push_back((float)c2[2] + offZ);
                        verts.push_back((float)ul);   // uRaw: 0..uw
                        verts.push_back((float)vl);   // vRaw: 0..vw
                        verts.push_back(tileBaseU);
                    }
                    v += vw;
                }
            }
        }
    }
    return verts;
}
