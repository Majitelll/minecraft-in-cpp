#pragma once
#include "chunkmanager.h"
#include "camera.h"
#include <vector>

struct GLFWwindow;

// Abstract renderer interface — implemented by VulkanRenderer (desktop) and GLRenderer (web/Emscripten)
class Renderer {
public:
    virtual ~Renderer() = default;
    virtual void init(GLFWwindow* window) = 0;
    virtual void uploadChunkMesh(const UploadRequest& req) = 0;
    virtual void destroyChunkBuffers(ChunkPos pos) = 0;
    // Upload pre-built UI vertex data (xy uv, 4 floats per vertex)
    virtual void uploadUIVertices(const std::vector<float>& verts) = 0;
    // Draw one frame. Returns true if the swapchain/context was recreated (caller should re-upload UI).
    virtual bool drawFrame(const Camera& camera, bool wireframe) = 0;
    // Called when the GLFW framebuffer was resized externally.
    virtual void onWindowResized() = 0;
    virtual void waitIdle() = 0;
    virtual void cleanup() = 0;
};
