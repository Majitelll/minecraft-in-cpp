#include "app.h"
#include "camera.h"
#include "textureatlas.h"
#include "raycast.h"
#include "log.h"

#include <cmath>
#include <stdexcept>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#include "renderer_gl.h"
#else
#include "renderer_vulkan.h"
#endif

static constexpr int WIN_W = 800;
static constexpr int WIN_H = 600;

const BlockType App::HOTBAR_BLOCKS[App::HOTBAR_SIZE] = {
    BlockType::Grass,
    BlockType::Dirt,
    BlockType::Stone,
    BlockType::Wood,
    BlockType::Leaves,
    BlockType::Snow,
    BlockType::Bedrock,
};

// ─────────────────────────────────────────────────────────────────────────────
// Window
// ─────────────────────────────────────────────────────────────────────────────
void App::initWindow() {
    glfwInit();
#ifdef __EMSCRIPTEN__
    glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_ES_API);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
#else
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
#endif
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
#ifdef __EMSCRIPTEN__
    int initW = EM_ASM_INT({ return window.innerWidth;  });
    int initH = EM_ASM_INT({ return window.innerHeight; });
    window = glfwCreateWindow(initW, initH, "Minecraft", nullptr, nullptr);
#else
    window = glfwCreateWindow(WIN_W, WIN_H, "Minecraft", nullptr, nullptr);
#endif
    glfwSetWindowUserPointer(window, this);
    glfwSetFramebufferSizeCallback(window, [](GLFWwindow* w, int, int) {
        static_cast<App*>(glfwGetWindowUserPointer(w))->framebufferResized = true;
    });
    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
    glfwSetCursorPosCallback(window, mouseCallback);
    glfwSetScrollCallback(window, [](GLFWwindow* w, double, double dy) {
        static_cast<App*>(glfwGetWindowUserPointer(w))->scrollAccum += dy;
    });
    glfwSetMouseButtonCallback(window, [](GLFWwindow* w, int btn, int action, int) {
        App* app = static_cast<App*>(glfwGetWindowUserPointer(w));
        if (btn == GLFW_MOUSE_BUTTON_LEFT  && action == GLFW_PRESS) app->leftPressed  = true;
        if (btn == GLFW_MOUSE_BUTTON_RIGHT && action == GLFW_PRESS) app->rightPressed = true;
    });
}

// ─────────────────────────────────────────────────────────────────────────────
// Run
// ─────────────────────────────────────────────────────────────────────────────
void App::run() {
    logMsg("initWindow...");
    initWindow();

#ifdef __EMSCRIPTEN__
    renderer = std::make_unique<GLRenderer>();
#else
    renderer = std::make_unique<VulkanRenderer>();
#endif

    logMsg("renderer init...");
    renderer->init(window);
    logMsg("seed: " + std::to_string(chunkManager.getSeed()));

    {
        int fw, fh;
        glfwGetFramebufferSize(window, &fw, &fh);
        if (fw > 0 && fh > 0)
            renderer->uploadUIVertices(buildUIVertices());
    }

    logMsg("mainLoop...");

#ifdef __EMSCRIPTEN__
    emscripten_set_main_loop_arg([](void* arg) {
        static_cast<App*>(arg)->tick();
    }, this, 0, 1);
    // Not reached (simulate_infinite_loop=1 unwinds the stack)
#else
    while (!glfwWindowShouldClose(window))
        tick();
    renderer->waitIdle();
    renderer->cleanup();
    glfwDestroyWindow(window);
    glfwTerminate();
#endif
}

// ─────────────────────────────────────────────────────────────────────────────
// Tick (one frame of game logic + rendering)
// ─────────────────────────────────────────────────────────────────────────────
void App::tick() {
    float now = (float)glfwGetTime();
    deltaTime = now - lastFrame;
    lastFrame = now;
    glfwPollEvents();
    processInput(window);

    // Physics: gravity, jumping, AABB collision
    applyPhysics(deltaTime, [&](int wx, int wy, int wz) -> bool {
        if (wy < 0) return true;
        if (wy >= CHUNK_HEIGHT) return false;
        int cx = (int)floorf((float)wx / (float)CHUNK_SIZE);
        int cz = (int)floorf((float)wz / (float)CHUNK_SIZE);
        int lx = wx - cx * CHUNK_SIZE;
        int lz = wz - cz * CHUNK_SIZE;
        Chunk* chunk = chunkManager.getChunk(cx, cz);
        if (!chunk) return true;  // treat unloaded chunks as solid to prevent falling through
        return chunk->isSolid(lx, wy, lz);
    });

    // Hotbar key selection (1-7)
    static const int hotbarKeys[HOTBAR_SIZE] = {
        GLFW_KEY_1, GLFW_KEY_2, GLFW_KEY_3, GLFW_KEY_4,
        GLFW_KEY_5, GLFW_KEY_6, GLFW_KEY_7
    };
    for (int i = 0; i < HOTBAR_SIZE; i++) {
        if (glfwGetKey(window, hotbarKeys[i]) == GLFW_PRESS) {
            if (hotbarSlot != i) {
                hotbarSlot = i;
                renderer->uploadUIVertices(buildUIVertices());
            }
        }
    }

    // Fullscreen toggle (F11)
    bool f11Now = glfwGetKey(window, GLFW_KEY_F11) == GLFW_PRESS;
    if (f11Now && !f11Previous) {
        isFullscreen = !isFullscreen;
        if (isFullscreen) {
            glfwGetWindowPos(window, &savedWinX, &savedWinY);
            glfwGetWindowSize(window, &savedWinW, &savedWinH);
            GLFWmonitor* monitor = glfwGetPrimaryMonitor();
            const GLFWvidmode* mode = glfwGetVideoMode(monitor);
            glfwSetWindowMonitor(window, monitor, 0, 0, mode->width, mode->height, mode->refreshRate);
        } else {
            glfwSetWindowMonitor(window, nullptr, savedWinX, savedWinY, savedWinW, savedWinH, 0);
        }
    }
    f11Previous = f11Now;

    // Scroll wheel hotbar selection
    if (scrollAccum != 0.0) {
        int delta = (scrollAccum > 0) ? -1 : 1;
        hotbarSlot = (hotbarSlot + delta + HOTBAR_SIZE) % HOTBAR_SIZE;
        scrollAccum = 0.0;
        renderer->uploadUIVertices(buildUIVertices());
    }

    // Block break / place
    if (leftPressed || rightPressed) {
        handleBlockInteraction();
        leftPressed = rightPressed = false;
    }

    int playerChunkX = (int)floorf(camera.position[0] / (float)CHUNK_SIZE);
    int playerChunkZ = (int)floorf(camera.position[2] / (float)CHUNK_SIZE);
    chunkManager.update(playerChunkX, playerChunkZ);

    std::vector<UploadRequest> ready;
    chunkManager.drainUploadQueue(ready, 2);
    for (auto& req : ready) {
        if (!req.vertices.empty()) {
            renderer->uploadChunkMesh(req);
            chunkManager.markUploaded(req.pos);
        }
    }

    for (auto pos : chunkManager.drainUnloadQueue())
        renderer->destroyChunkBuffers(pos);

    if (chunkManager.pool.hasError())
        throw std::runtime_error("Worker thread error: " + chunkManager.pool.getError());

    // Handle external framebuffer resize (call before drawFrame to avoid double recreation)
    bool didResize = false;
    if (framebufferResized) {
        framebufferResized = false;
        renderer->onWindowResized();
        didResize = true;
    }

    bool swapchainRecreated = renderer->drawFrame(camera, wireframe);

    if (didResize || swapchainRecreated) {
        int fw, fh;
        glfwGetFramebufferSize(window, &fw, &fh);
        if (fw > 0 && fh > 0)
            renderer->uploadUIVertices(buildUIVertices());
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Block interaction
// ─────────────────────────────────────────────────────────────────────────────
void App::setBlockInWorld(int wx, int wy, int wz, BlockType type) {
    if (wy < 0 || wy >= CHUNK_HEIGHT) return;
    int cx = (int)floorf((float)wx / (float)CHUNK_SIZE);
    int cz = (int)floorf((float)wz / (float)CHUNK_SIZE);
    int lx = wx - cx * CHUNK_SIZE;
    int lz = wz - cz * CHUNK_SIZE;
    Chunk* chunk = chunkManager.getChunk(cx, cz);
    if (!chunk) return;
    chunk->blocks[lx][wy][lz] = type;
    auto remesh = [&](int ccx, int ccz) {
        Chunk* c = chunkManager.getChunk(ccx, ccz);
        if (!c) return;
        Chunk* neighbors[3][3]{};
        for (int dx=-1; dx<=1; dx++)
            for (int dz=-1; dz<=1; dz++)
                neighbors[dx+1][dz+1] = chunkManager.getChunk(ccx+dx, ccz+dz);
        auto verts = c->buildMesh(ccx, ccz, neighbors);
        UploadRequest req{ChunkPos{ccx,ccz}, std::move(verts)};
        renderer->uploadChunkMesh(req);
        chunkManager.markUploaded({ccx,ccz});
    };
    remesh(cx, cz);
    if (lx == 0)            remesh(cx-1, cz);
    if (lx == CHUNK_SIZE-1) remesh(cx+1, cz);
    if (lz == 0)            remesh(cx, cz-1);
    if (lz == CHUNK_SIZE-1) remesh(cx, cz+1);
}

void App::handleBlockInteraction() {
    vec3 front = {
        cosf(glm_rad(camera.yaw))  * cosf(glm_rad(camera.pitch)),
        sinf(glm_rad(camera.pitch)),
        sinf(glm_rad(camera.yaw))  * cosf(glm_rad(camera.pitch))
    };
    glm_vec3_normalize(front);
    vec3 rayOrigin = {camera.position[0], camera.position[1], camera.position[2]};
    RayHit hit = castRay(rayOrigin, front, 8.f, chunkManager);
    if (!hit.hit) return;
    if (leftPressed) {
        if (!(hit.type == BlockType::Bedrock && hit.blockY == 0))
            setBlockInWorld(hit.blockX, hit.blockY, hit.blockZ, BlockType::Air);
    } else if (rightPressed) {
        int px = hit.blockX + hit.normalX;
        int py = hit.blockY + hit.normalY;
        int pz = hit.blockZ + hit.normalZ;
        float dx = (float)px + 0.5f - camera.position[0];
        float dy = (float)py + 0.5f - camera.position[1];
        float dz = (float)pz + 0.5f - camera.position[2];
        if (fabsf(dx)<0.9f && fabsf(dy)<1.8f && fabsf(dz)<0.9f) return;
        setBlockInWorld(px, py, pz, HOTBAR_BLOCKS[hotbarSlot]);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// UI vertex building (CPU-side only — no GPU code here)
// ─────────────────────────────────────────────────────────────────────────────
std::vector<float> App::buildUIVertices() const {
    int w, h;
    glfwGetFramebufferSize(window, &w, &h);
    if (w == 0 || h == 0) return {};

    float ar        = (float)w / (float)h;
    float slotSize  = 0.10f;
    float slotSizeX = slotSize / ar;
    float padding   = 0.014f / ar;
    float totalW    = HOTBAR_SIZE * (slotSizeX + padding) - padding;
    float targetW   = 1.0f;
    float scale     = targetW / totalW;
    slotSizeX *= scale;
    slotSize  *= scale;
    padding   *= scale;
    totalW     = HOTBAR_SIZE * (slotSizeX + padding) - padding;
    float startX = -totalW * 0.5f;
    float baseY  = 1.0f - slotSize;

    std::vector<float> verts;
    verts.reserve(HOTBAR_SIZE * 6 * 4 * 3 + 12 * 4);

    auto pushQuad = [&](float x0, float y0, float x1, float y1,
                        float u0, float v0, float u1, float v1) {
        verts.insert(verts.end(), {x0,y0,u0,v1, x1,y0,u1,v1, x1,y1,u1,v0});
        verts.insert(verts.end(), {x0,y0,u0,v1, x1,y1,u1,v0, x0,y1,u0,v0});
    };

    for (int i = 0; i < HOTBAR_SIZE; i++) {
        float x0 = startX + i * (slotSizeX + padding);
        float x1 = x0 + slotSizeX;
        float y0 = baseY - 0.04f;
        float y1 = y0 + slotSize;

        TileUV slotUV = getTileUV(TileID::UISlot);
        pushQuad(x0, y0, x1, y1, slotUV.u0, slotUV.v0, slotUV.u1, slotUV.v1);

        BlockType bt = HOTBAR_BLOCKS[i];
        TileID tid;
        switch (bt) {
            case BlockType::Grass:   tid = TileID::GrassTop;  break;
            case BlockType::Dirt:    tid = TileID::Dirt;       break;
            case BlockType::Stone:   tid = TileID::Stone;      break;
            case BlockType::Wood:    tid = TileID::WoodTop;    break;
            case BlockType::Leaves:  tid = TileID::Leaves;     break;
            case BlockType::Snow:    tid = TileID::Snow;       break;
            case BlockType::Bedrock: tid = TileID::Bedrock;    break;
            default:                 tid = TileID::Stone;      break;
        }
        TileUV uv = getTileUV(tid);
        float inset = 0.025f;
        float ix0 = x0 + inset/ar, ix1 = x1 - inset/ar;
        float iy0 = y0 + inset,    iy1 = y1 - inset;
        pushQuad(ix0, iy0, ix1, iy1, uv.u0, uv.v0, uv.u1, uv.v1);

        if (i == hotbarSlot) {
            TileUV selUV = getTileUV(TileID::UISelector);
            float sDir = 0.005f;
            pushQuad(x0 - sDir/ar, y0 - sDir, x1 + sDir/ar, y1 + sDir,
                     selUV.u0, selUV.v0, selUV.u1, selUV.v1);
        }
    }

    // Crosshair (UV 0,0 signals solid white in both Vulkan and GL shaders)
    float chSize   = 0.025f;
    float chThick  = 0.004f;
    float chThickX = chThick / ar;
    pushQuad(-chSize,   -chThick,   chSize,   chThick,   0.f, 0.f, 0.f, 0.f);
    pushQuad(-chThickX, -chSize*ar, chThickX, chSize*ar, 0.f, 0.f, 0.f, 0.f);

    return verts;
}
