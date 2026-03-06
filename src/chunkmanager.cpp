#include "chunkmanager.h"
#include <cmath>
#include <algorithm>

void logMsg(const std::string& msg);

ChunkManager::ChunkManager(unsigned int seed)
    : seed(seed)
    , pool(std::max(2u, std::thread::hardware_concurrency() - 1))
{
    logMsg("ChunkManager: seed=" + std::to_string(seed) +
           " threads=" + std::to_string(std::max(2u, std::thread::hardware_concurrency() - 1)));
}

// ─────────────────────────────────────────────────────────────────────────────
// Public API
// ─────────────────────────────────────────────────────────────────────────────
void ChunkManager::update(int playerChunkX, int playerChunkZ) {
    std::unordered_map<ChunkPos, bool, ChunkPosHash> desired;
    for (int dx = -RENDER_DIST; dx <= RENDER_DIST; dx++) {
        for (int dz = -RENDER_DIST; dz <= RENDER_DIST; dz++) {
            if (dx*dx + dz*dz > RENDER_DIST*RENDER_DIST) continue;
            desired[{playerChunkX+dx, playerChunkZ+dz}] = true;
        }
    }

    std::unique_lock<std::mutex> lock(mapMtx);

    // ── Schedule terrain for new chunks ──────────────────────────────────────
    for (auto& [pos, _] : desired) {
        if (chunkStatus.find(pos) == chunkStatus.end()) {
            chunkStatus[pos] = ChunkStatus::TerrainPending;
            chunkData[pos]   = std::make_unique<Chunk>();
            lock.unlock();
            scheduleTerrainGen(pos);
            lock.lock();
        }
    }

    // ── Advance TerrainDone → DecorationPending ───────────────────────────────
    // Requires all 8 neighbors to be at least TerrainDone
    std::vector<ChunkPos> readyForDeco;
    for (auto& [pos, status] : chunkStatus) {
        if (status == ChunkStatus::TerrainDone && allNeighborsAtLeast(pos, ChunkStatus::TerrainDone))
            readyForDeco.push_back(pos);
    }
    for (auto& pos : readyForDeco) {
        chunkStatus[pos] = ChunkStatus::DecorationPending;
        lock.unlock();
        scheduleDecoration(pos);
        lock.lock();
    }

    // ── Advance DecorationDone → MeshPending ─────────────────────────────────
    // Requires all 8 neighbors to also be DecorationDone or later
    // This ensures no neighbor will write leaves into us after our mesh is built
    std::vector<ChunkPos> readyForMesh;
    for (auto& [pos, status] : chunkStatus) {
        if (status == ChunkStatus::DecorationDone && allNeighborsAtLeast(pos, ChunkStatus::DecorationDone))
            readyForMesh.push_back(pos);
    }
    for (auto& pos : readyForMesh) {
        chunkStatus[pos] = ChunkStatus::MeshPending;
        lock.unlock();
        scheduleMesh(pos);
        lock.lock();
    }

    // ── Unload out-of-range chunks ────────────────────────────────────────────
    std::vector<ChunkPos> toRemove;
    for (auto& [pos, _] : chunkStatus)
        if (desired.find(pos) == desired.end())
            toRemove.push_back(pos);

    lock.unlock();

    if (!toRemove.empty()) {
        {
            std::unique_lock<std::mutex> ul(unloadMtx);
            for (auto& p : toRemove) unloadQueue.push_back(p);
        }
        std::unique_lock<std::mutex> ml(mapMtx);
        for (auto& p : toRemove) {
            chunkStatus.erase(p);
            chunkData.erase(p);
        }
    }
}

void ChunkManager::drainUploadQueue(std::vector<UploadRequest>& out, int maxPerFrame) {
    std::unique_lock<std::mutex> lock(uploadMtx);
    int count = 0;
    while (!uploadQueue.empty() && count < maxPerFrame) {
        out.push_back(std::move(uploadQueue.front()));
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

Chunk* ChunkManager::getChunk(int cx, int cz) {
    std::unique_lock<std::mutex> lock(mapMtx);
    auto it = chunkData.find({cx, cz});
    if (it == chunkData.end()) return nullptr;
    return it->second.get();
}

// ─────────────────────────────────────────────────────────────────────────────
// Internal helpers
// ─────────────────────────────────────────────────────────────────────────────
bool ChunkManager::allNeighborsAtLeast(ChunkPos pos, ChunkStatus minStatus) {
    // mapMtx must be held by caller
    for (int dx = -1; dx <= 1; dx++) {
        for (int dz = -1; dz <= 1; dz++) {
            if (dx == 0 && dz == 0) continue;
            auto it = chunkStatus.find({pos.x+dx, pos.z+dz});
            if (it == chunkStatus.end()) return false;
            if (static_cast<int>(it->second) < static_cast<int>(minStatus)) return false;
        }
    }
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Stage 1: Terrain generation
// ─────────────────────────────────────────────────────────────────────────────
void ChunkManager::scheduleTerrainGen(ChunkPos pos) {
    logMsg("scheduleTerrainGen: (" + std::to_string(pos.x) + "," + std::to_string(pos.z) + ")");
    pool.enqueue([this, pos]() {
        Chunk* myChunk = nullptr;
        {
            std::unique_lock<std::mutex> lock(mapMtx);
            auto it = chunkData.find(pos);
            if (it == chunkData.end()) return;
            myChunk = it->second.get();
        }
        if (!myChunk) return;

        myChunk->generateTerrain(pos.x, pos.z, seed);

        {
            std::unique_lock<std::mutex> lock(mapMtx);
            auto sit = chunkStatus.find(pos);
            if (sit != chunkStatus.end()) sit->second = ChunkStatus::TerrainDone;
        }
    });
}

// ─────────────────────────────────────────────────────────────────────────────
// Stage 2: Decoration (trees) — neighbors are all TerrainDone
// ─────────────────────────────────────────────────────────────────────────────
void ChunkManager::scheduleDecoration(ChunkPos pos) {
    logMsg("scheduleDecoration: (" + std::to_string(pos.x) + "," + std::to_string(pos.z) + ")");
    pool.enqueue([this, pos]() {
        Chunk* myChunk = nullptr;
        {
            std::unique_lock<std::mutex> lock(mapMtx);
            auto it = chunkData.find(pos);
            if (it == chunkData.end()) return;
            myChunk = it->second.get();
        }
        if (!myChunk) return;

        // neighborFn: gets neighbor chunk pointer safely
        auto neighborFn = [this](int cx, int cz) -> Chunk* {
            std::unique_lock<std::mutex> lock(mapMtx);
            auto it = chunkData.find({cx, cz});
            if (it == chunkData.end()) return nullptr;
            return it->second.get();
        };

        {
            // Hold decoMtx for the entire generateDecorations call.
            // Decoration workers write leaves into *neighbor* chunks via
            // neighborFn, so two adjacent decoration tasks running in parallel
            // would race on the same blocks[] array — undefined behaviour that
            // reliably crashes on Linux/macOS Release builds.  Serialising the
            // write phase here is the simplest correct fix; terrain generation
            // and mesh building remain fully parallel.
            std::unique_lock<std::mutex> decoLock(decoMtx);
            myChunk->generateDecorations(pos.x, pos.z, seed, neighborFn);
        }

        {
            std::unique_lock<std::mutex> lock(mapMtx);
            auto sit = chunkStatus.find(pos);
            if (sit != chunkStatus.end()) sit->second = ChunkStatus::DecorationDone;
        }
        logMsg("decoration done: (" + std::to_string(pos.x) + "," + std::to_string(pos.z) + ")");
    });
}

// ─────────────────────────────────────────────────────────────────────────────
// Stage 3: Mesh building — all neighbors are DecorationDone
// Passes neighbor chunks so inter-chunk face culling works correctly
// ─────────────────────────────────────────────────────────────────────────────
void ChunkManager::scheduleMesh(ChunkPos pos) {
    logMsg("scheduleMesh: (" + std::to_string(pos.x) + "," + std::to_string(pos.z) + ")");
    pool.enqueue([this, pos]() {
        Chunk* myChunk = nullptr;
        {
            std::unique_lock<std::mutex> lock(mapMtx);
            auto it = chunkData.find(pos);
            if (it == chunkData.end()) return;
            myChunk = it->second.get();
        }
        if (!myChunk) return;

        // Collect neighbor pointers for inter-chunk face culling
        // Safe to read since all neighbors are DecorationDone (no more writes)
        Chunk* neighbors[3][3]{};
        {
            std::unique_lock<std::mutex> lock(mapMtx);
            for (int dx = -1; dx <= 1; dx++) {
                for (int dz = -1; dz <= 1; dz++) {
                    auto it = chunkData.find({pos.x+dx, pos.z+dz});
                    if (it != chunkData.end())
                        neighbors[dx+1][dz+1] = it->second.get();
                }
            }
        }

        std::vector<float> verts = myChunk->buildMesh(pos.x, pos.z, neighbors);

        {
            std::unique_lock<std::mutex> lock(mapMtx);
            auto sit = chunkStatus.find(pos);
            if (sit != chunkStatus.end()) sit->second = ChunkStatus::MeshReady;
        }

        {
            std::unique_lock<std::mutex> lock(uploadMtx);
            uploadQueue.push(UploadRequest{pos, std::move(verts)});
        }
    });
}
