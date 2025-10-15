#include <glad/glad.h>
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <cglm/cglm.h>
#include <vector>
#include <iostream>

const char* vertexShaderSource = R"glsl(
    #version 330 core
    layout(location = 0) in vec3 aPos;
    uniform mat4 uMVP;
    void main() {
        gl_Position = uMVP * vec4(aPos, 1.0);
    }
)glsl";

const char* fragmentShaderSource = R"glsl(
    #version 330 core
    out vec4 FragColor;
    void main() {
        FragColor = vec4(1.0, 0.0, 0.0, 1.0);
    }
)glsl";

struct Camera {
    vec3 position;
    float yaw;
    float pitch;
    float speed;
    float sensitivity;
};

Camera camera = {{0.0f, 0.0f, 50.0f}, -90.0f, 0.0f, 10.0f, 0.1f};
float lastX = 400, lastY = 300;
bool firstMouse = true;
float deltaTime = 0.0f, lastFrame = 0.0f;
bool wireframe = false;
bool ePressedLastFrame = false;

void framebuffer_size_callback(GLFWwindow* window, int width, int height) { glViewport(0, 0, width, height); }
void mouse_callback(GLFWwindow* window, double xpos, double ypos) {
    if (firstMouse) { lastX = (float)xpos; lastY = (float)ypos; firstMouse = false; }
    float xoffset = (float)(xpos - lastX), yoffset = (float)(lastY - ypos);
    lastX = (float)xpos; lastY = (float)ypos;
    xoffset *= camera.sensitivity; yoffset *= camera.sensitivity;
    camera.yaw += xoffset; camera.pitch += yoffset;
    if (camera.pitch > 89.0f) camera.pitch = 89.0f;
    if (camera.pitch < -89.0f) camera.pitch = -89.0f;
}

void processInput(GLFWwindow* window) {
    float velocity = camera.speed * deltaTime;
    vec3 front = {cos(glm_rad(camera.yaw)) * cos(glm_rad(camera.pitch)),
                  sin(glm_rad(camera.pitch)),
                  sin(glm_rad(camera.yaw)) * cos(glm_rad(camera.pitch))};
    glm_vec3_normalize(front);
    vec3 right; vec3 worldUp = {0.0f, 1.0f, 0.0f};
    glm_vec3_cross(front, worldUp, right); glm_vec3_normalize(right);
    vec3 up; glm_vec3_cross(right, front, up);

    if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) { vec3 tmp; glm_vec3_scale(front, velocity, tmp); glm_vec3_add(camera.position, tmp, camera.position); }
    if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) { vec3 tmp; glm_vec3_scale(front, velocity, tmp); glm_vec3_sub(camera.position, tmp, camera.position); }
    if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) { vec3 tmp; glm_vec3_scale(right, velocity, tmp); glm_vec3_sub(camera.position, tmp, camera.position); }
    if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) { vec3 tmp; glm_vec3_scale(right, velocity, tmp); glm_vec3_add(camera.position, tmp, camera.position); }
    if (glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS) { vec3 tmp; glm_vec3_scale(up, velocity, tmp); glm_vec3_add(camera.position, tmp, camera.position); }
    if (glfwGetKey(window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS) { vec3 tmp; glm_vec3_scale(up, velocity, tmp); glm_vec3_sub(camera.position, tmp, camera.position); }

    bool ePressedNow = glfwGetKey(window, GLFW_KEY_E) == GLFW_PRESS;
    if (ePressedNow && !ePressedLastFrame) {
        wireframe = !wireframe;
        glPolygonMode(GL_FRONT_AND_BACK, wireframe ? GL_LINE : GL_FILL);
    }
    ePressedLastFrame = ePressedNow;
}

GLuint compileShader(GLenum type, const char* source) {
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);
    GLint success;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success) { char info[512]; glGetShaderInfoLog(shader, 512, nullptr, info); std::cerr << info << std::endl; }
    return shader;
}

// Cube face vertices
float faceVertices[6][18] = {
    // +X
    {0.5f, -0.5f, -0.5f,  0.5f, 0.5f, -0.5f,  0.5f, 0.5f, 0.5f,  0.5f, 0.5f, 0.5f,  0.5f, -0.5f, 0.5f,  0.5f, -0.5f, -0.5f},
    // -X
    {-0.5f, -0.5f, 0.5f,  -0.5f, 0.5f, 0.5f,  -0.5f, 0.5f, -0.5f,  -0.5f, 0.5f, -0.5f,  -0.5f, -0.5f, -0.5f,  -0.5f, -0.5f, 0.5f},
    // +Y
    {-0.5f, 0.5f, -0.5f,  -0.5f, 0.5f, 0.5f,  0.5f, 0.5f, 0.5f,  0.5f, 0.5f, 0.5f,  0.5f, 0.5f, -0.5f,  -0.5f, 0.5f, -0.5f},
    // -Y
    {-0.5f, -0.5f, 0.5f,  -0.5f, -0.5f, -0.5f,  0.5f, -0.5f, -0.5f,  0.5f, -0.5f, -0.5f,  0.5f, -0.5f, 0.5f,  -0.5f, -0.5f, 0.5f},
    // +Z
    {-0.5f, -0.5f, 0.5f,  0.5f, -0.5f, 0.5f,  0.5f, 0.5f, 0.5f,  0.5f, 0.5f, 0.5f,  -0.5f, 0.5f, 0.5f,  -0.5f, -0.5f, 0.5f},
    // -Z
    {0.5f, -0.5f, -0.5f,  -0.5f, -0.5f, -0.5f,  -0.5f, 0.5f, -0.5f,  -0.5f, 0.5f, -0.5f,  0.5f, 0.5f, -0.5f,  0.5f, -0.5f, -0.5f}
};

int main() {
    if (!glfwInit()) return -1;
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    GLFWwindow* window = glfwCreateWindow(800, 600, "Optimized 16x16x16 Cube", nullptr, nullptr);
    if (!window) { glfwTerminate(); return -1; }
    glfwMakeContextCurrent(window);
    glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);
    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
    glfwSetCursorPosCallback(window, mouse_callback);
    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) return -1;

    // Cube presence map
    const int SIZE = 16;
    bool cube[SIZE][SIZE][SIZE];
    for(int x=0;x<SIZE;x++) for(int y=0;y<SIZE;y++) for(int z=0;z<SIZE;z++) cube[x][y][z]=true;

    std::vector<float> vertices;

    // Build vertices for only visible faces
    for(int x=0;x<SIZE;x++){
        for(int y=0;y<SIZE;y++){
            for(int z=0;z<SIZE;z++){
                if(!cube[x][y][z]) continue;
                // Check neighbors: +X, -X, +Y, -Y, +Z, -Z
                int neighbor[6][3]={{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}};
                for(int f=0;f<6;f++){
                    int nx=x+neighbor[f][0], ny=y+neighbor[f][1], nz=z+neighbor[f][2];
                    if(nx<0||nx>=SIZE||ny<0||ny>=SIZE||nz<0||nz>=SIZE||!cube[nx][ny][nz]){
                        // Add this face
                        for(int v=0;v<18;v+=3){
                            vertices.push_back(faceVertices[f][v]+x-SIZE/2.0f);
                            vertices.push_back(faceVertices[f][v+1]+y-SIZE/2.0f);
                            vertices.push_back(faceVertices[f][v+2]+z-SIZE/2.0f);
                        }
                    }
                }
            }
        }
    }

    GLuint VBO, VAO;
    glGenVertexArrays(1,&VAO);
    glGenBuffers(1,&VBO);
    glBindVertexArray(VAO);
    glBindBuffer(GL_ARRAY_BUFFER,VBO);
    glBufferData(GL_ARRAY_BUFFER, vertices.size()*sizeof(float), vertices.data(), GL_STATIC_DRAW);
    glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,3*sizeof(float),(void*)0);
    glEnableVertexAttribArray(0);
    glBindVertexArray(0);

    GLuint shaderProgram = glCreateProgram();
    GLuint vertexShader = compileShader(GL_VERTEX_SHADER, vertexShaderSource);
    GLuint fragmentShader = compileShader(GL_FRAGMENT_SHADER, fragmentShaderSource);
    glAttachShader(shaderProgram, vertexShader);
    glAttachShader(shaderProgram, fragmentShader);
    glLinkProgram(shaderProgram);
    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);

    glEnable(GL_DEPTH_TEST);

    while(!glfwWindowShouldClose(window)){
        float currentFrame=(float)glfwGetTime();
        deltaTime=currentFrame-lastFrame;
        lastFrame=currentFrame;
        processInput(window);

        glClearColor(0.1f,0.2f,0.3f,1.0f);
        glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);

        vec3 front={cos(glm_rad(camera.yaw))*cos(glm_rad(camera.pitch)),
                    sin(glm_rad(camera.pitch)),
                    sin(glm_rad(camera.yaw))*cos(glm_rad(camera.pitch))};
        glm_vec3_normalize(front);
        vec3 center; glm_vec3_add(camera.position, front, center);
        mat4 view; vec3 upVec={0.0f,1.0f,0.0f};
        glm_lookat(camera.position, center, upVec, view);
        int width,height; glfwGetFramebufferSize(window,&width,&height);
        float aspect=(float)width/(float)height;
        mat4 proj; glm_perspective(glm_rad(45.0f), aspect, 0.1f, 500.0f, proj);
        mat4 mvp; glm_mat4_mul(proj, view, mvp);

        glUseProgram(shaderProgram);
        GLint mvpLoc=glGetUniformLocation(shaderProgram,"uMVP");
        glUniformMatrix4fv(mvpLoc,1,GL_FALSE,(const GLfloat*)mvp);

        glBindVertexArray(VAO);
        glDrawArrays(GL_TRIANGLES,0,(GLsizei)vertices.size()/3);
        glBindVertexArray(0);

        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    glDeleteVertexArrays(1,&VAO);
    glDeleteBuffers(1,&VBO);
    glDeleteProgram(shaderProgram);
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}