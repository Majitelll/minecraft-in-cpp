#pragma once

#include "chunk.h"
#include "threadpool.h"

#include <memory>
#include <mutex>
#include <queue>
#include <unordered_map>
#include <vector>

struct ChunkPos {
    int x, z;
    bool operator==(const ChunkPos& o) const { return x==o.x && z==o.z; }
};

struct ChunkPosHash {
    size_t operator()(const ChunkPos& p) const {
        return std::hash<int>{}(p.x) ^ (std::hash<int>{}(p.z) * 2654435761u);
    }
};

// Ordered — used for integer comparison in allNeighborsAtLeast
enum class ChunkStatus : int {
    TerrainPending    = 0,
    TerrainGenerating = 1,
    TerrainDone       = 2,
    DecorationPending = 3,
    DecorationDone    = 4,
    MeshPending       = 5,
    MeshReady         = 6,
    Uploaded          = 7,
};

struct UploadRequest {
    ChunkPos           pos;
    std::vector<float> vertices;
};

class ChunkManager {
public:
    explicit ChunkManager(unsigned int seed = 42);
    ~ChunkManager() = default;

    void update(int playerChunkX, int playerChunkZ);
    void drainUploadQueue(std::vector<UploadRequest>& out, int maxPerFrame = 4);
    void markUploaded(ChunkPos pos);
    std::vector<ChunkPos> drainUnloadQueue();
    void getUploadedChunks(std::vector<ChunkPos>& out) const;
    Chunk* getChunk(int cx, int cz);
    unsigned int getSeed() const { return seed; }

    // Public so App can check for background thread errors
    ThreadPool pool;

private:
    unsigned int seed;

    mutable std::mutex mapMtx;
    std::unordered_map<ChunkPos, ChunkStatus,         ChunkPosHash> chunkStatus;
    std::unordered_map<ChunkPos, std::unique_ptr<Chunk>, ChunkPosHash> chunkData;

    std::mutex                uploadMtx;
    std::queue<UploadRequest> uploadQueue;

    std::mutex            unloadMtx;
    std::vector<ChunkPos> unloadQueue;

    bool allNeighborsAtLeast(ChunkPos pos, ChunkStatus minStatus); // mapMtx held
    void scheduleTerrainGen(ChunkPos pos);
    void scheduleDecoration(ChunkPos pos);
    void scheduleMesh(ChunkPos pos);
};
