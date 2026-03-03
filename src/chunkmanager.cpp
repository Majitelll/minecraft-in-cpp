#include "chunkmanager.h"
#include <cmath>

ChunkManager::ChunkManager(unsigned int seed)
    : seed(seed)
    , pool(std::max(2u, std::thread::hardware_concurrency() - 1))
    // Leave one core for the main/render thread
{}

void ChunkManager::update(int playerChunkX, int playerChunkZ) {
    // ── 1. Build the set of chunks that SHOULD exist ──────────────────────
    std::unordered_map<ChunkPos, bool, ChunkPosHash> desired;
    for (int dx = -RENDER_DIST; dx <= RENDER_DIST; dx++) {
        for (int dz = -RENDER_DIST; dz <= RENDER_DIST; dz++) {
            // Optional: circular render distance (feels more natural)
            if (dx*dx + dz*dz > RENDER_DIST*RENDER_DIST) continue;
            ChunkPos p{playerChunkX + dx, playerChunkZ + dz};
            desired[p] = true;
        }
    }

    std::unique_lock<std::mutex> lock(mapMtx);

    // ── 2. Schedule generation for new chunks ─────────────────────────────
    for (auto& [pos, _] : desired) {
        if (chunkStatus.find(pos) == chunkStatus.end()) {
            chunkStatus[pos] = ChunkStatus::Pending;
            scheduleChunk(pos);
        }
    }

    // ── 3. Mark chunks outside range for unloading ────────────────────────
    std::vector<ChunkPos> toRemove;
    for (auto& [pos, status] : chunkStatus) {
        if (desired.find(pos) == desired.end()) {
            // Only unload chunks that are already uploaded (or pending/generating)
            // For pending/generating we just let them finish and immediately unload
            toRemove.push_back(pos);
        }
    }
    lock.unlock();

    if (!toRemove.empty()) {
        std::unique_lock<std::mutex> ulock(unloadMtx);
        for (auto& p : toRemove) unloadQueue.push_back(p);
        std::unique_lock<std::mutex> mlock(mapMtx);
        for (auto& p : toRemove) chunkStatus.erase(p);
    }
}

void ChunkManager::scheduleChunk(ChunkPos pos) {
    // Note: mapMtx is already held by the caller (update)
    chunkStatus[pos] = ChunkStatus::Generating;

    pool.enqueue([this, pos]() {
        // ── Stage 1: generate block data ──────────────────────────────────
        auto chunk = std::make_unique<Chunk>();
        chunk->generate(pos.x, pos.z, seed);

        // ── Stage 2: build mesh ───────────────────────────────────────────
        std::vector<float> verts = chunk->buildMesh(pos.x, pos.z);

        // ── Check if chunk was unloaded while we were working ─────────────
        {
            std::unique_lock<std::mutex> lock(mapMtx);
            auto it = chunkStatus.find(pos);
            if (it == chunkStatus.end()) return; // was unloaded, discard
            it->second = ChunkStatus::MeshReady;
        }

        // ── Stage 3: hand off to main thread for GPU upload ───────────────
        {
            std::unique_lock<std::mutex> lock(uploadMtx);
            uploadQueue.push(UploadRequest{pos, std::move(verts)});
        }
    });
}

void ChunkManager::drainUploadQueue(std::vector<UploadRequest>& outRequests, int maxPerFrame) {
    std::unique_lock<std::mutex> lock(uploadMtx);
    int count = 0;
    while (!uploadQueue.empty() && count < maxPerFrame) {
        outRequests.push_back(std::move(uploadQueue.front()));
        uploadQueue.pop();
        count++;
    }
}

void ChunkManager::markUploaded(ChunkPos pos) {
    std::unique_lock<std::mutex> lock(mapMtx);
    auto it = chunkStatus.find(pos);
    if (it != chunkStatus.end()) it->second = ChunkStatus::Uploaded;
}

std::vector<ChunkPos> ChunkManager::drainUnloadQueue() {
    std::unique_lock<std::mutex> lock(unloadMtx);
    std::vector<ChunkPos> out = std::move(unloadQueue);
    unloadQueue.clear();
    return out;
}

void ChunkManager::getUploadedChunks(std::vector<ChunkPos>& out) const {
    std::unique_lock<std::mutex> lock(mapMtx);
    for (auto& [pos, status] : chunkStatus)
        if (status == ChunkStatus::Uploaded) out.push_back(pos);
}
