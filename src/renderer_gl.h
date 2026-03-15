#pragma once
#ifdef __EMSCRIPTEN__

#include "renderer.h"
#include <GLES3/gl3.h>
#include <unordered_map>
#include <vector>

struct GLChunkBuffers {
    GLuint  vao         = 0;
    GLuint  vbo         = 0;
    GLsizei vertexCount = 0;
};

class GLRenderer : public Renderer {
public:
    void init(GLFWwindow* window) override;
    void uploadChunkMesh(const UploadRequest& req) override;
    void destroyChunkBuffers(ChunkPos pos) override;
    void uploadUIVertices(const std::vector<float>& verts) override;
    bool drawFrame(const Camera& camera, bool wireframe) override;
    void onWindowResized() override;
    void waitIdle() override;
    void cleanup() override;

private:
    GLFWwindow* window = nullptr;

    GLuint worldProgram = 0;
    GLuint uiProgram    = 0;
    GLuint atlasTexture = 0;

    std::unordered_map<ChunkPos, GLChunkBuffers, ChunkPosHash> chunkBuffers;

    GLuint  uiVAO         = 0;
    GLuint  uiVBO         = 0;
    GLsizei uiVertexCount = 0;

    // Cached uniform locations
    GLint locMVP     = -1;
    GLint locAtlas   = -1;
    GLint locUIAtlas = -1;

    GLuint compileShader(GLenum type, const char* src);
    GLuint linkProgram(GLuint vert, GLuint frag);
    void   createAtlas();
};

#endif // __EMSCRIPTEN__
