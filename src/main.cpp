#include <glad/glad.h>
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <cglm/cglm.h>
#include <iostream>

// Vertex shader source
const char* vertexShaderSource = R"glsl(
    #version 330 core
    layout(location = 0) in vec3 aPos;

    uniform mat4 uMVP;

    void main() {
        gl_Position = uMVP * vec4(aPos, 1.0);
    }
)glsl";

// Fragment shader source
const char* fragmentShaderSource = R"glsl(
    #version 330 core
    out vec4 FragColor;
    void main() {
        FragColor = vec4(1.0, 0.0, 0.0, 1.0);  // Red color
    }
)glsl";

// Camera state
struct Camera {
    vec3 position;
    float yaw;
    float pitch;
    float speed;
    float sensitivity;
};
Camera camera = {
    .position = {0.0f, 0.0f, 3.0f},
    .yaw = -90.0f,     // facing -Z initially
    .pitch = 0.0f,
    .speed = 2.5f,     // units per second
    .sensitivity = 0.1f
};

float lastX = 400, lastY = 300;
bool firstMouse = true;

float deltaTime = 0.0f;
float lastFrame = 0.0f;

// Mouse callback to update yaw/pitch
void mouse_callback(GLFWwindow* window, double xpos, double ypos) {
    if (firstMouse) {
        lastX = (float)xpos;
        lastY = (float)ypos;
        firstMouse = false;
    }

    float xoffset = (float)(xpos - lastX);
    float yoffset = (float)(lastY - ypos); // reversed: y-coords go from bottom to top

    lastX = (float)xpos;
    lastY = (float)ypos;

    xoffset *= camera.sensitivity;
    yoffset *= camera.sensitivity;

    camera.yaw += xoffset;
    camera.pitch += yoffset;

    if (camera.pitch > 89.0f)
        camera.pitch = 89.0f;
    if (camera.pitch < -89.0f)
        camera.pitch = -89.0f;
}

// Process keyboard input for movement
void processInput(GLFWwindow* window) {
    float velocity = camera.speed * deltaTime;
    vec3 front;
    front[0] = cos(glm_rad(camera.yaw)) * cos(glm_rad(camera.pitch));
    front[1] = sin(glm_rad(camera.pitch));
    front[2] = sin(glm_rad(camera.yaw)) * cos(glm_rad(camera.pitch));
    glm_vec3_normalize(front);

    vec3 right;
    glm_vec3_cross(front, (vec3){0.0f, 1.0f, 0.0f}, right);
    glm_vec3_normalize(right);

    vec3 up;
    glm_vec3_cross(right, front, up);

    if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) {
        glm_vec3_scale(front, velocity, front);
        glm_vec3_add(camera.position, front, camera.position);
    }
    if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) {
        glm_vec3_scale(front, velocity, front);
        glm_vec3_sub(camera.position, front, camera.position);
    }
    if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) {
        glm_vec3_scale(right, velocity, right);
        glm_vec3_sub(camera.position, right, camera.position);
    }
    if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) {
        glm_vec3_scale(right, velocity, right);
        glm_vec3_add(camera.position, right, camera.position);
    }
    if (glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS) {
        glm_vec3_scale(up, velocity, up);
        glm_vec3_add(camera.position, up, camera.position);
    }
    if (glfwGetKey(window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS) {
        glm_vec3_scale(up, velocity, up);
        glm_vec3_sub(camera.position, up, camera.position);
    }
}

// Shader compilation utility (same as yours)
GLuint compileShader(GLenum type, const char* source) {
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);
    GLint success;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        char infoLog[512];
        glGetShaderInfoLog(shader, 512, nullptr, infoLog);
        std::cerr << "Shader compilation failed:\n" << infoLog << std::endl;
    }
    return shader;
}

int main() {
    if (!glfwInit()) {
        std::cerr << "Failed to initialize GLFW\n";
        return -1;
    }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    GLFWwindow* window = glfwCreateWindow(800, 600, "Red Square with Camera", nullptr, nullptr);
    if (!window) {
        std::cerr << "Failed to create GLFW window\n";
        glfwTerminate();
        return -1;
    }
    glfwMakeContextCurrent(window);

    // Capture and hide cursor for free look
    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
    glfwSetCursorPosCallback(window, mouse_callback);

    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
        std::cerr << "Failed to initialize GLAD\n";
        return -1;
    }

    // Square vertices in 3D (z=0)
    float vertices[] = {
        -0.5f, -0.5f, 0.0f,  // bottom-left
         0.5f, -0.5f, 0.0f,  // bottom-right
         0.5f,  0.5f, 0.0f,  // top-right

        -0.5f, -0.5f, 0.0f,  // bottom-left
         0.5f,  0.5f, 0.0f,  // top-right
        -0.5f,  0.5f, 0.0f   // top-left
    };

    GLuint VBO, VAO;
    glGenVertexArrays(1, &VAO);
    glGenBuffers(1, &VBO);
    glBindVertexArray(VAO);

    glBindBuffer(GL_ARRAY_BUFFER, VBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

    // position attribute
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glBindVertexArray(0);

    GLuint vertexShader = compileShader(GL_VERTEX_SHADER, vertexShaderSource);
    GLuint fragmentShader = compileShader(GL_FRAGMENT_SHADER, fragmentShaderSource);

    GLuint shaderProgram = glCreateProgram();
    glAttachShader(shaderProgram, vertexShader);
    glAttachShader(shaderProgram, fragmentShader);
    glLinkProgram(shaderProgram);

    GLint success;
    glGetProgramiv(shaderProgram, GL_LINK_STATUS, &success);
    if (!success) {
        char infoLog[512];
        glGetProgramInfoLog(shaderProgram, 512, nullptr, infoLog);
        std::cerr << "Shader linking failed:\n" << infoLog << std::endl;
    }

    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);

    glEnable(GL_DEPTH_TEST);

    while (!glfwWindowShouldClose(window)) {
        // Frame timing
        float currentFrame = (float)glfwGetTime();
        deltaTime = currentFrame - lastFrame;
        lastFrame = currentFrame;

        processInput(window);

        glClearColor(0.1f, 0.2f, 0.3f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        // Build view matrix
        vec3 front;
        front[0] = cos(glm_rad(camera.yaw)) * cos(glm_rad(camera.pitch));
        front[1] = sin(glm_rad(camera.pitch));
        front[2] = sin(glm_rad(camera.yaw)) * cos(glm_rad(camera.pitch));
        glm_vec3_normalize(front);

        vec3 center;
        glm_vec3_add(camera.position, front, center);

        mat4 view;
        glm_lookat(camera.position, center, (vec3){0.0f, 1.0f, 0.0f}, view);

        // Projection matrix (perspective)
        int width, height;
        glfwGetFramebufferSize(window, &width, &height);
        float aspect = (float)width / (float)height;

        mat4 proj;
        glm_perspective(glm_rad(45.0f), aspect, 0.1f, 100.0f, proj);

        // MVP = projection * view (model is identity)
        mat4 mvp;
        glm_mat4_mul(proj, view, mvp);

        glUseProgram(shaderProgram);
        GLint mvpLoc = glGetUniformLocation(shaderProgram, "uMVP");
        glUniformMatrix4fv(mvpLoc, 1, GL_FALSE, (const GLfloat*)mvp);

        glBindVertexArray(VAO);
        glDrawArrays(GL_TRIANGLES, 0, 6);
        glBindVertexArray(0);

        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    glDeleteVertexArrays(1, &VAO);
    glDeleteBuffers(1, &VBO);
    glDeleteProgram(shaderProgram);

    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}