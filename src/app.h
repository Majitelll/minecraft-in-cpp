#pragma once
#include <GLFW/glfw3.h>
#include <memory>
#include <random>
#include <vector>
#include "renderer.h"
#include "chunkmanager.h"

class App {
public:
    void run();

    // Public so GLFW lambdas can write to them
    bool   framebufferResized = false;
    double scrollAccum        = 0.0;
    bool   leftPressed        = false;
    bool   rightPressed       = false;

private:
    GLFWwindow*               window = nullptr;
    std::unique_ptr<Renderer> renderer;
    ChunkManager              chunkManager{static_cast<unsigned int>(std::random_device{}())};

    static constexpr int HOTBAR_SIZE = 7;
    static const BlockType HOTBAR_BLOCKS[HOTBAR_SIZE];
    int  hotbarSlot   = 0;
    bool isFullscreen = false;
    int  savedWinX=0, savedWinY=0, savedWinW=0, savedWinH=0;
    bool f11Previous  = false;

    void initWindow();
    void tick();
    void handleBlockInteraction();
    void setBlockInWorld(int wx, int wy, int wz, BlockType type);
    std::vector<float> buildUIVertices() const;
};
