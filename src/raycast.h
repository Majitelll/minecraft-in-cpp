#pragma once
#include "chunk.h"
#include "chunkmanager.h"
#include <cmath>

struct RayHit {
    bool      hit     = false;
    int       blockX  = 0;
    int       blockY  = 0;
    int       blockZ  = 0;
    int       normalX = 0;
    int       normalY = 0;
    int       normalZ = 0;
    BlockType type    = BlockType::Air;
};

inline RayHit castRay(const vec3 origin, const vec3 dir, float maxDist,
                      ChunkManager& cm)
{
    RayHit result;

    float len = sqrtf(dir[0]*dir[0] + dir[1]*dir[1] + dir[2]*dir[2]);
    if (len < 1e-6f) return result;
    float dx = dir[0]/len, dy = dir[1]/len, dz = dir[2]/len;

    int ix = (int)floorf(origin[0]);
    int iy = (int)floorf(origin[1]);
    int iz = (int)floorf(origin[2]);

    int stepX = dx>0?1:-1, stepY = dy>0?1:-1, stepZ = dz>0?1:-1;

    float tdx = (dx==0.f)?1e30f:fabsf(1.f/dx);
    float tdy = (dy==0.f)?1e30f:fabsf(1.f/dy);
    float tdz = (dz==0.f)?1e30f:fabsf(1.f/dz);

    // Distance from origin to first voxel boundary on each axis
    float tmx = (dx==0.f)?1e30f:((stepX>0?(float)(ix+1)-origin[0]:origin[0]-(float)ix)/fabsf(dx));
    float tmy = (dy==0.f)?1e30f:((stepY>0?(float)(iy+1)-origin[1]:origin[1]-(float)iy)/fabsf(dy));
    float tmz = (dz==0.f)?1e30f:((stepZ>0?(float)(iz+1)-origin[2]:origin[2]-(float)iz)/fabsf(dz));

    int lastNX=0, lastNY=0, lastNZ=0;

    // We always advance at least once so we never hit the block the camera is inside
    bool first = true;

    for (;;) {
        // Advance to next voxel boundary
        if (tmx < tmy && tmx < tmz) {
            if (tmx > maxDist) break;
            lastNX = -stepX; lastNY = 0; lastNZ = 0;
            ix += stepX; tmx += tdx;
        } else if (tmy < tmz) {
            if (tmy > maxDist) break;
            lastNX = 0; lastNY = -stepY; lastNZ = 0;
            iy += stepY; tmy += tdy;
        } else {
            if (tmz > maxDist) break;
            lastNX = 0; lastNY = 0; lastNZ = -stepZ;
            iz += stepZ; tmz += tdz;
        }

        if (iy < 0 || iy >= CHUNK_HEIGHT) continue;

        int cx = (int)floorf((float)ix / (float)CHUNK_SIZE);
        int cz = (int)floorf((float)iz / (float)CHUNK_SIZE);
        int lx = ix - cx*CHUNK_SIZE;
        int lz = iz - cz*CHUNK_SIZE;

        Chunk* chunk = cm.getChunk(cx, cz);
        if (!chunk) continue;

        BlockType bt = chunk->blocks[lx][iy][lz];
        if (bt != BlockType::Air) {
            result.hit    = true;
            result.blockX = ix; result.blockY = iy; result.blockZ = iz;
            result.normalX = lastNX; result.normalY = lastNY; result.normalZ = lastNZ;
            result.type   = bt;
            return result;
        }
    }
    return result;
}
