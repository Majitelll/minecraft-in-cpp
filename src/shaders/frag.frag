#version 450

layout(binding = 1) uniform sampler2D texAtlas;

layout(location = 0) in vec2  fragUVRaw;
layout(location = 1) in float fragTileBaseU;

layout(location = 0) out vec4 outColor;

void main() {
    // uRaw/vRaw range 0..N (N = merged quad extent in tiles).
    // fract() gives per-tile repetition; scale by tile width in atlas.
    const float tileSizeU = 1.0 / 12.0;  // ATLAS_COLS = 12
    // V is flipped: atlas v=0 is the TOP of each tile (e.g. green strip on grass).
    // vRaw increases going UP the block face, so invert it so green appears at the top.
    vec2 uv = vec2(fragTileBaseU + fract(fragUVRaw.x) * tileSizeU,
                   1.0 - fract(fragUVRaw.y));
    vec4 col = texture(texAtlas, uv);
    if (col.a < 0.01) discard;
    outColor = col;
}
