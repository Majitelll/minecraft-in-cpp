# Minecraft in C++

> ⚠️ Work in progress — under active development.

A Minecraft-inspired voxel engine written in C++ using Vulkan. Built from scratch as a learning project, focusing on modern graphics programming and game engine architecture.

![Platform](https://img.shields.io/badge/platform-Windows%20%7C%20Linux%20%7C%20macOS-blue)
![Language](https://img.shields.io/badge/language-C%2B%2B17-orange)
![Graphics](https://img.shields.io/badge/graphics-Vulkan-red)

---

## Features

- **Vulkan renderer** — modern GPU API with validation layers, depth buffering, and dual solid/wireframe pipelines
- **Infinite world** — procedurally generated terrain using fractal noise, loads and unloads chunks as you move
- **Multithreaded chunk loading** — world generation and mesh building run on background threads, GPU uploads happen on the main thread
- **Face culling** — only visible block faces are rendered, no wasted geometry
- **Block types** — Bedrock, Stone, Dirt, and Grass with per-face color shading
- **Free-fly camera** — WASD movement, mouse look, Space/Ctrl for vertical movement
- **Wireframe mode** — toggle with E
- **Cross-platform** — Windows, Linux (x86_64 + ARM64), and macOS (Apple Silicon)

---

## Controls

| Key | Action |
|-----|--------|
| W A S D | Move |
| Mouse | Look |
| Space | Move up |
| Left Ctrl | Move down |
| E | Toggle wireframe |
| Escape | Quit |

---

## Building

### Prerequisites

- CMake 3.20+
- Vulkan SDK — [download from LunarG](https://vulkan.lunarg.com/sdk/home)
- A C++17 compiler (MSVC, GCC, Clang)

On **macOS**, install MoltenVK and shaderc via Homebrew:
```bash
brew install vulkan-headers molten-vk shaderc
```

On **Linux**:
```bash
sudo apt-get install libvulkan-dev vulkan-tools glslc \
  libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev
```

### Clone and build

```bash
git clone --recurse-submodules https://github.com/Majitelll/minecraft-in-cpp.git
cd minecraft-in-cpp
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

On **macOS**, pass the Vulkan paths explicitly:
```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release \
  -DVulkan_INCLUDE_DIR=$(brew --prefix vulkan-headers)/include \
  -DVulkan_LIBRARY=$(brew --prefix molten-vk)/lib/libMoltenVK.dylib \
  -DGLSLC=$(brew --prefix shaderc)/bin/glslc
```

---

## Project Structure

```
src/
  main.cpp          — entry point
  app.h / app.cpp   — Vulkan application (instance, swapchain, pipelines, render loop)
  camera.h/.cpp     — camera state and input handling
  chunk.h/.cpp      — block data, procedural generation, mesh building
  chunkmanager.h/.cpp — chunk lifecycle, load/unload, thread coordination
  threadpool.h      — fixed-size thread pool
  shaders/
    vert.vert       — vertex shader
    frag.frag       — fragment shader
```

---

## Dependencies

All dependencies are included as git submodules — no manual installation needed on Windows/Linux.

- [GLFW](https://github.com/glfw/glfw) — window and input (same library used by Minecraft via LWJGL)
- [cglm](https://github.com/recp/cglm) — C math library for vectors and matrices
- Vulkan SDK — system installed

---

## Releases

Pre-built binaries for Windows, Linux x86_64, Linux ARM64, and macOS Apple Silicon are available on the [Releases](../../releases) page.
