#ifdef __EMSCRIPTEN__

#include "renderer_gl.h"
#include "textureatlas.h"
#include "log.h"

#include <GLES3/gl3.h>
#include <cglm/cglm.h>
#include <cmath>
#include <stdexcept>

// ─────────────────────────────────────────────────────────────────────────────
// Embedded GLSL ES 3.0 shaders
// ─────────────────────────────────────────────────────────────────────────────
static const char* WORLD_VERT_SRC = R"(#version 300 es
uniform mat4 mvp;
in vec3 inPos;
in vec2 inUVRaw;
in float inTileBaseU;
out vec2 fragUVRaw;
out float fragTileBaseU;
void main() {
    gl_Position = mvp * vec4(inPos, 1.0);
    fragUVRaw = inUVRaw;
    fragTileBaseU = inTileBaseU;
}
)";

static const char* WORLD_FRAG_SRC = R"(#version 300 es
precision mediump float;
uniform sampler2D texAtlas;
in vec2 fragUVRaw;
in float fragTileBaseU;
out vec4 outColor;
void main() {
    const float tileSizeU = 1.0 / 12.0;
    vec2 uv = vec2(fragTileBaseU + fract(fragUVRaw.x) * tileSizeU,
                   1.0 - fract(fragUVRaw.y));
    vec4 col = texture(texAtlas, uv);
    if (col.a < 0.01) discard;
    outColor = col;
}
)";

static const char* UI_VERT_SRC = R"(#version 300 es
in vec2 inPos;
in vec2 inUV;
out vec2 fragUV;
void main() {
    // Negate Y: UI vertices are built in Vulkan NDC (Y=+1 is bottom),
    // but OpenGL NDC has Y=+1 at the top.
    gl_Position = vec4(inPos.x, -inPos.y, 0.0, 1.0);
    fragUV = inUV;
}
)";

static const char* UI_FRAG_SRC = R"(#version 300 es
precision mediump float;
uniform sampler2D texAtlas;
in vec2 fragUV;
out vec4 outColor;
void main() {
    if (fragUV.x < 0.0001 && fragUV.y < 0.0001) {
        outColor = vec4(1.0, 1.0, 1.0, 0.85);
        return;
    }
    outColor = texture(texAtlas, fragUV);
}
)";

// ─────────────────────────────────────────────────────────────────────────────
// Shader helpers
// ─────────────────────────────────────────────────────────────────────────────
GLuint GLRenderer::compileShader(GLenum type, const char* src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    GLint ok; glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char buf[512]; glGetShaderInfoLog(s, 512, nullptr, buf);
        glDeleteShader(s);
        throw std::runtime_error(std::string("Shader compile error: ") + buf);
    }
    return s;
}

GLuint GLRenderer::linkProgram(GLuint vert, GLuint frag) {
    GLuint prog = glCreateProgram();
    glAttachShader(prog, vert);
    glAttachShader(prog, frag);
    glLinkProgram(prog);
    GLint ok; glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        char buf[512]; glGetProgramInfoLog(prog, 512, nullptr, buf);
        glDeleteProgram(prog);
        throw std::runtime_error(std::string("Program link error: ") + buf);
    }
    glDeleteShader(vert);
    glDeleteShader(frag);
    return prog;
}

// ─────────────────────────────────────────────────────────────────────────────
// Atlas creation
// ─────────────────────────────────────────────────────────────────────────────
void GLRenderer::createAtlas() {
    auto pixels = generateAtlas();
    glGenTextures(1, &atlasTexture);
    glBindTexture(GL_TEXTURE_2D, atlasTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, ATLAS_W, ATLAS_H, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);
}

// ─────────────────────────────────────────────────────────────────────────────
// init
// ─────────────────────────────────────────────────────────────────────────────
void GLRenderer::init(GLFWwindow* win) {
    window = win;
    logMsg("GLRenderer::init");

    glfwMakeContextCurrent(window);

    // World shader
    GLuint wv = compileShader(GL_VERTEX_SHADER,   WORLD_VERT_SRC);
    GLuint wf = compileShader(GL_FRAGMENT_SHADER, WORLD_FRAG_SRC);
    worldProgram = linkProgram(wv, wf);
    locMVP   = glGetUniformLocation(worldProgram, "mvp");
    locAtlas = glGetUniformLocation(worldProgram, "texAtlas");

    // UI shader
    GLuint uv = compileShader(GL_VERTEX_SHADER,   UI_VERT_SRC);
    GLuint uf = compileShader(GL_FRAGMENT_SHADER, UI_FRAG_SRC);
    uiProgram = linkProgram(uv, uf);
    locUIAtlas = glGetUniformLocation(uiProgram, "texAtlas");

    // Atlas texture
    createAtlas();

    // UI VAO/VBO (will be filled by uploadUIVertices)
    glGenVertexArrays(1, &uiVAO);
    glGenBuffers(1, &uiVBO);

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);

    logMsg("GLRenderer::init done");
}

// ─────────────────────────────────────────────────────────────────────────────
// uploadChunkMesh
// ─────────────────────────────────────────────────────────────────────────────
void GLRenderer::uploadChunkMesh(const UploadRequest& req) {
    destroyChunkBuffers(req.pos);

    if (req.vertices.empty()) return;

    GLChunkBuffers cb{};
    cb.vertexCount = (GLsizei)(req.vertices.size() / 6);

    glGenVertexArrays(1, &cb.vao);
    glGenBuffers(1, &cb.vbo);

    glBindVertexArray(cb.vao);
    glBindBuffer(GL_ARRAY_BUFFER, cb.vbo);
    glBufferData(GL_ARRAY_BUFFER,
                 (GLsizeiptr)(req.vertices.size() * sizeof(float)),
                 req.vertices.data(),
                 GL_STATIC_DRAW);

    // attrib 0: vec3 pos (offset 0, stride 6*4)
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6*4, (void*)0);
    // attrib 1: vec2 uvRaw (offset 3*4)
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 6*4, (void*)(3*4));
    // attrib 2: float tileBaseU (offset 5*4)
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, 6*4, (void*)(5*4));

    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    chunkBuffers[req.pos] = cb;
}

// ─────────────────────────────────────────────────────────────────────────────
// destroyChunkBuffers
// ─────────────────────────────────────────────────────────────────────────────
void GLRenderer::destroyChunkBuffers(ChunkPos pos) {
    auto it = chunkBuffers.find(pos);
    if (it == chunkBuffers.end()) return;
    glDeleteVertexArrays(1, &it->second.vao);
    glDeleteBuffers(1, &it->second.vbo);
    chunkBuffers.erase(it);
}

// ─────────────────────────────────────────────────────────────────────────────
// uploadUIVertices
// ─────────────────────────────────────────────────────────────────────────────
void GLRenderer::uploadUIVertices(const std::vector<float>& verts) {
    uiVertexCount = (GLsizei)(verts.size() / 4);

    glBindVertexArray(uiVAO);
    glBindBuffer(GL_ARRAY_BUFFER, uiVBO);
    glBufferData(GL_ARRAY_BUFFER,
                 (GLsizeiptr)(verts.size() * sizeof(float)),
                 verts.empty() ? nullptr : verts.data(),
                 GL_DYNAMIC_DRAW);

    // attrib 0: vec2 pos (offset 0, stride 4*4)
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4*4, (void*)0);
    // attrib 1: vec2 uv (offset 2*4)
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4*4, (void*)(2*4));

    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

// ─────────────────────────────────────────────────────────────────────────────
// drawFrame
// ─────────────────────────────────────────────────────────────────────────────
bool GLRenderer::drawFrame(const Camera& camera, bool wireframe) {
    int w, h;
    glfwGetFramebufferSize(window, &w, &h);
    if (w == 0 || h == 0) return false;

    glViewport(0, 0, w, h);
    glClearColor(0.53f, 0.81f, 0.98f, 1.f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    // ── World pass ───────────────────────────────────────────────────────────
    glEnable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glFrontFace(GL_CCW);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);

    // Wireframe mode (glPolygonMode) is not available in WebGL2/GLES3 — ignored.

    glUseProgram(worldProgram);

    // MVP
    vec3 front = {
        cosf(glm_rad(camera.yaw)) * cosf(glm_rad(camera.pitch)),
        sinf(glm_rad(camera.pitch)),
        sinf(glm_rad(camera.yaw)) * cosf(glm_rad(camera.pitch))
    };
    glm_vec3_normalize(front);
    vec3 center; glm_vec3_add((float*)camera.position, front, center);
    vec3 up = {0.f, 1.f, 0.f};
    mat4 view; glm_lookat((float*)camera.position, center, up, view);
    mat4 proj; glm_perspective(glm_rad(70.f), (float)w/(float)h, 0.1f, 2000.f, proj);
    // No proj[1][1] *= -1 — that's Vulkan only
    mat4 mvp; glm_mat4_mul(proj, view, mvp);
    glUniformMatrix4fv(locMVP, 1, GL_FALSE, (const GLfloat*)mvp);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, atlasTexture);
    glUniform1i(locAtlas, 0);

    for (auto& [pos, cb] : chunkBuffers) {
        if (cb.vertexCount == 0) continue;
        glBindVertexArray(cb.vao);
        glDrawArrays(GL_TRIANGLES, 0, cb.vertexCount);
    }
    glBindVertexArray(0);

    // ── UI pass ──────────────────────────────────────────────────────────────
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_CULL_FACE);

    if (uiVertexCount > 0) {
        glUseProgram(uiProgram);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, atlasTexture);
        glUniform1i(locUIAtlas, 0);
        glBindVertexArray(uiVAO);
        glDrawArrays(GL_TRIANGLES, 0, uiVertexCount);
        glBindVertexArray(0);
    }

    glfwSwapBuffers(window);
    return false;
}

// ─────────────────────────────────────────────────────────────────────────────
// onWindowResized — no-op (glViewport is called per-frame in drawFrame)
// ─────────────────────────────────────────────────────────────────────────────
void GLRenderer::onWindowResized() {
    // Nothing to do — viewport is set at the top of drawFrame
}

// ─────────────────────────────────────────────────────────────────────────────
// waitIdle
// ─────────────────────────────────────────────────────────────────────────────
void GLRenderer::waitIdle() {
    glFinish();
}

// ─────────────────────────────────────────────────────────────────────────────
// cleanup
// ─────────────────────────────────────────────────────────────────────────────
void GLRenderer::cleanup() {
    for (auto& [pos, cb] : chunkBuffers) {
        glDeleteVertexArrays(1, &cb.vao);
        glDeleteBuffers(1, &cb.vbo);
    }
    chunkBuffers.clear();

    glDeleteVertexArrays(1, &uiVAO);
    glDeleteBuffers(1, &uiVBO);
    glDeleteTextures(1, &atlasTexture);
    glDeleteProgram(worldProgram);
    glDeleteProgram(uiProgram);
}

#endif // __EMSCRIPTEN__
