#pragma once

#include "chunk.h"
#include "threadpool.h"

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <queue>
#include <unordered_map>

// Integer 2D chunk coordinate
struct ChunkPos {
    int x, z;
    bool operator==(const ChunkPos& o) const { return x==o.x && z==o.z; }
};

struct ChunkPosHash {
    size_t operator()(const ChunkPos& p) const {
        // Cantor-style hash
        size_t hx = std::hash<int>{}(p.x);
        size_t hz = std::hash<int>{}(p.z);
        return hx ^ (hz * 2654435761u);
    }
};

// Lifecycle of a chunk slot
enum class ChunkStatus {
    Pending,    // queued for generation
    Generating, // being built on a worker thread
    MeshReady,  // mesh built, waiting for GPU upload on main thread
    Uploaded,   // GPU buffer exists, ready to draw
};

struct ChunkGPUData {
    // Filled on main thread after upload
    // Opaque pointer — actual VkBuffer lives in App and is indexed by ChunkPos
    uint32_t vertexCount = 0;
};

// Data passed from worker thread back to main thread
struct UploadRequest {
    ChunkPos           pos;
    std::vector<float> vertices; // xyz rgb interleaved
};

class ChunkManager {
public:
    explicit ChunkManager(unsigned int seed = 42);
    ~ChunkManager() = default;

    // Called every frame with the player's current chunk coordinate.
    // Queues new chunks for generation and marks out-of-range chunks for removal.
    void update(int playerChunkX, int playerChunkZ);

    // Drain up to maxPerFrame finished meshes into outRequests.
    // Called on the main thread so the app can do GPU uploads.
    void drainUploadQueue(std::vector<UploadRequest>& outRequests, int maxPerFrame = 4);

    // Mark a chunk as fully uploaded (called by App after GPU work is done).
    void markUploaded(ChunkPos pos);

    // Returns list of chunk positions that should no longer be rendered
    // and whose GPU buffers should be freed. Clears the internal list.
    std::vector<ChunkPos> drainUnloadQueue();

    // Returns all currently Uploaded chunk positions (for rendering)
    void getUploadedChunks(std::vector<ChunkPos>& out) const;

private:
    unsigned int seed;
    ThreadPool   pool;

    mutable std::mutex                                        mapMtx;
    std::unordered_map<ChunkPos, ChunkStatus, ChunkPosHash>  chunkStatus;

    std::mutex                  uploadMtx;
    std::queue<UploadRequest>   uploadQueue;  // worker → main thread

    std::mutex               unloadMtx;
    std::vector<ChunkPos>    unloadQueue;     // main thread consumption

    void scheduleChunk(ChunkPos pos);
};
