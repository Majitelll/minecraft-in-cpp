#pragma once

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <cglm/cglm.h>

struct Camera {
    vec3  position;
    float yaw;
    float pitch;
    float speed;
    float sensitivity;
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
