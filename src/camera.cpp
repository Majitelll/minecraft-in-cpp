#include "camera.h"

Camera camera      = {{0.f, 20.f, 30.f}, -90.f, -20.f, 15.f, 0.1f};
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

    float v = camera.speed * deltaTime;
    vec3 front = {
        cosf(glm_rad(camera.yaw))  * cosf(glm_rad(camera.pitch)),
        sinf(glm_rad(camera.pitch)),
        sinf(glm_rad(camera.yaw))  * cosf(glm_rad(camera.pitch))
    };
    glm_vec3_normalize(front);

    vec3 worldUp = {0.f, 1.f, 0.f};
    vec3 right; glm_vec3_cross(front, worldUp, right); glm_vec3_normalize(right);
    vec3 up;    glm_vec3_cross(right, front, up);

    auto move = [&](vec3 dir, float sign) {
        vec3 tmp; glm_vec3_scale(dir, sign * v, tmp);
        glm_vec3_add(camera.position, tmp, camera.position);
    };

    if (glfwGetKey(window, GLFW_KEY_W)            == GLFW_PRESS) move(front,  1.f);
    if (glfwGetKey(window, GLFW_KEY_S)            == GLFW_PRESS) move(front, -1.f);
    if (glfwGetKey(window, GLFW_KEY_A)            == GLFW_PRESS) move(right, -1.f);
    if (glfwGetKey(window, GLFW_KEY_D)            == GLFW_PRESS) move(right,  1.f);
    if (glfwGetKey(window, GLFW_KEY_SPACE)        == GLFW_PRESS) move(up,     1.f);
    if (glfwGetKey(window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS) move(up,    -1.f);

    bool eNow = glfwGetKey(window, GLFW_KEY_E) == GLFW_PRESS;
    if (eNow && !ePrevious) wireframe = !wireframe;
    ePrevious = eNow;
}
