#pragma once

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <cglm/cglm.h>
#include <functional>

struct Camera {
    vec3  position;      // eye position (feet + EYE_HEIGHT)
    float yaw;
    float pitch;
    float sensitivity;
    vec3  velocity;      // units/s
    bool  onGround;
    bool  jumpRequested;
};

// Global camera state
extern Camera camera;
extern float  lastX;
extern float  lastY;
extern bool   firstMouse;
extern float  deltaTime;
extern float  lastFrame;
extern bool   wireframe;
extern bool   ePrevious;

void mouseCallback(GLFWwindow* window, double xpos, double ypos);
void processInput(GLFWwindow* window);
void applyPhysics(float dt, std::function<bool(int,int,int)> isSolid);
