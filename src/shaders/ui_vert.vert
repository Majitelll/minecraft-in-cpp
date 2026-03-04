#version 450

// No UBO needed — coordinates are already in NDC
layout(location = 0) in vec2 inPos;
layout(location = 1) in vec2 inUV;

layout(location = 0) out vec2 fragUV;

void main() {
    gl_Position = vec4(inPos, 0.0, 1.0);
    fragUV = inUV;
}
