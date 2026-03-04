#version 450

layout(binding = 1) uniform sampler2D texAtlas;

layout(location = 0) in vec2 fragUV;
layout(location = 0) out vec4 outColor;

void main() {
    vec4 col = texture(texAtlas, fragUV);
    if (col.a < 0.01) discard;  // only discard fully empty pixels
    outColor = col;
}
