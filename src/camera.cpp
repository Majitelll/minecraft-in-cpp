#include "camera.h"
#include <cmath>

static constexpr float WALK_SPEED = 5.0f;
static constexpr float JUMP_SPEED = 9.0f;
static constexpr float GRAVITY    = -25.0f;
static constexpr float MAX_FALL   = -50.0f;
static constexpr float PLAYER_HW  = 0.3f;   // half-width (player is 0.6 wide)
static constexpr float PLAYER_H   = 1.8f;   // full height
static constexpr float EYE_HEIGHT = 1.62f;  // eye above feet

Camera camera      = {{0.f, 70.f, 30.f}, -90.f, -20.f, 0.1f, {0.f, 0.f, 0.f}, false, false};
float  lastX       = 400.f;
float  lastY       = 300.f;
bool   firstMouse  = true;
float  deltaTime   = 0.f;
float  lastFrame   = 0.f;
bool   wireframe   = false;
bool   ePrevious   = false;

void mouseCallback(GLFWwindow*, double xpos, double ypos) {
    if (firstMouse) {
        lastX = (float)xpos;
        lastY = (float)ypos;
        firstMouse = false;
    }
    float xo = (float)(xpos - lastX);
    float yo = (float)(lastY - ypos);
    lastX = (float)xpos;
    lastY = (float)ypos;

    camera.yaw   += xo * camera.sensitivity;
    camera.pitch += yo * camera.sensitivity;
    if (camera.pitch >  89.f) camera.pitch =  89.f;
    if (camera.pitch < -89.f) camera.pitch = -89.f;
}

void processInput(GLFWwindow* window) {
    if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
        glfwSetWindowShouldClose(window, true);

    // Horizontal movement: flat XZ based on yaw only (no pitch tilt)
    float yawRad = glm_rad(camera.yaw);
    vec3 front = { cosf(yawRad), 0.f, sinf(yawRad) };  // already unit length
    vec3 worldUp = {0.f, 1.f, 0.f};
    vec3 right; glm_vec3_cross(front, worldUp, right); glm_vec3_normalize(right);

    vec3 moveDir = {0.f, 0.f, 0.f};
    auto addDir = [&](vec3 dir, float sign) {
        vec3 tmp; glm_vec3_scale(dir, sign, tmp);
        glm_vec3_add(moveDir, tmp, moveDir);
    };
    if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) addDir(front,  1.f);
    if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) addDir(front, -1.f);
    if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) addDir(right, -1.f);
    if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) addDir(right,  1.f);

    float mag = glm_vec3_norm(moveDir);
    if (mag > 0.001f) {
        camera.velocity[0] = moveDir[0] * (WALK_SPEED / mag);
        camera.velocity[2] = moveDir[2] * (WALK_SPEED / mag);
    } else {
        camera.velocity[0] = 0.f;
        camera.velocity[2] = 0.f;
    }

    // Jump (only when on the ground)
    if (glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS && camera.onGround)
        camera.jumpRequested = true;

    bool eNow = glfwGetKey(window, GLFW_KEY_E) == GLFW_PRESS;
    if (eNow && !ePrevious) wireframe = !wireframe;
    ePrevious = eNow;
}

void applyPhysics(float dt, std::function<bool(int,int,int)> isSolid) {
    // Handle jump request
    if (camera.jumpRequested && camera.onGround) {
        camera.velocity[1] = JUMP_SPEED;
        camera.onGround = false;
    }
    camera.jumpRequested = false;

    // Gravity
    camera.velocity[1] += GRAVITY * dt;
    if (camera.velocity[1] < MAX_FALL) camera.velocity[1] = MAX_FALL;

    // Work in feet-space (eye is EYE_HEIGHT above feet)
    float fx = camera.position[0];
    float fy = camera.position[1] - EYE_HEIGHT;
    float fz = camera.position[2];

    // Returns true if the player AABB at (px, py, pz) overlaps any solid block
    auto aabbSolid = [&](float px, float py, float pz) -> bool {
        int x0 = (int)floorf(px - PLAYER_HW);
        int x1 = (int)floorf(px + PLAYER_HW - 0.001f);
        int y0 = (int)floorf(py);
        int y1 = (int)floorf(py + PLAYER_H - 0.001f);
        int z0 = (int)floorf(pz - PLAYER_HW);
        int z1 = (int)floorf(pz + PLAYER_HW - 0.001f);
        for (int bx = x0; bx <= x1; bx++)
            for (int by = y0; by <= y1; by++)
                for (int bz = z0; bz <= z1; bz++)
                    if (isSolid(bx, by, bz))
                        return true;
        return false;
    };

    // Resolve X
    float nx = fx + camera.velocity[0] * dt;
    if (aabbSolid(nx, fy, fz)) {
        camera.velocity[0] = 0.f;
    } else {
        fx = nx;
    }

    // Resolve Y
    float ny = fy + camera.velocity[1] * dt;
    if (aabbSolid(fx, ny, fz)) {
        if (camera.velocity[1] < 0.f)
            camera.onGround = true;
        camera.velocity[1] = 0.f;
        // fy stays at old value (no penetration)
    } else {
        fy = ny;
        if (camera.velocity[1] < 0.f)
            camera.onGround = false;  // falling freely
    }

    // Resolve Z
    float nz = fz + camera.velocity[2] * dt;
    if (aabbSolid(fx, fy, nz)) {
        camera.velocity[2] = 0.f;
    } else {
        fz = nz;
    }

    // Write back eye position
    camera.position[0] = fx;
    camera.position[1] = fy + EYE_HEIGHT;
    camera.position[2] = fz;
}
