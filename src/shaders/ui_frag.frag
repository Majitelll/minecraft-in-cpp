#version 450

layout(binding = 1) uniform sampler2D texAtlas;

layout(location = 0) in  vec2 fragUV;
layout(location = 0) out vec4 outColor;

void main() {
    // UV (0,0) is used for the crosshair — render as solid white
    if (fragUV.x == 0.0 && fragUV.y == 0.0) {
        outColor = vec4(1.0, 1.0, 1.0, 0.85);
        return;
    }
    outColor = texture(texAtlas, fragUV);
}
