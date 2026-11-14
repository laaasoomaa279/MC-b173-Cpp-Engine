#version 330 core

layout(location = 0) in uvec2 a_packedData;

uniform mat4 u_viewProj;
uniform vec3 u_chunkOffset;
uniform vec3 u_grassColor;
uniform vec3 u_foliageColor;

out vec2 v_localUV;
flat out uint v_terrainSlot;
out vec3 v_shadeColor;
out float v_skyLight;
out float v_blockLight;
out float v_fogDist;

void main() {
    uint lo = a_packedData.x;
    uint hi = a_packedData.y;

    // Decode 1/16th-block fixed-point coordinates [0.0 .. 16.0]
    float lx = float(lo & 0x1FFu) * 0.0625;
    float ly = float((lo >> 9u) & 0x1FFu) * 0.0625;
    float lz = float((lo >> 18u) & 0x1FFu) * 0.0625;

    uint faceDir  = (lo >> 27u) & 0x7u;
    uint aoLevel  = (lo >> 30u) & 0x3u;

    v_terrainSlot = hi & 0xFFu;
    float quadW   = float((hi >> 8u) & 0x1Fu);
    float quadH   = float((hi >> 13u) & 0x1Fu);
    uint tintMode = (hi >> 18u) & 0x3u;
    v_skyLight    = float((hi >> 20u) & 0xFu) / 15.0;
    v_blockLight  = float((hi >> 24u) & 0xFu) / 15.0;
    uint uvCorner = (hi >> 28u) & 0x3u;
    uint extra    = (hi >> 30u) & 0x3u;

    // Corner UV mapping (0:BL, 1:BR, 2:TR, 3:TL)
    if (uvCorner == 0u)      v_localUV = vec2(0.0, quadH);
    else if (uvCorner == 1u) v_localUV = vec2(quadW, quadH);
    else if (uvCorner == 2u) v_localUV = vec2(quadW, 0.0);
    else                     v_localUV = vec2(0.0, 0.0);

    // Exact Mojang RenderBlocks.java directional face multipliers
    float dirShade = 1.0;
    if (faceDir == 3u) {
        dirShade = 0.5; // Bottom (-Y)
    } else if (faceDir == 4u || faceDir == 5u) {
        dirShade = 0.8; // North / South (-Z / +Z)
    } else if (faceDir == 0u || faceDir == 1u) {
        dirShade = 0.6; // East / West (+X / -X)
    }

    // Mojang Smooth Lighting AO curve (0..3)
    float aoTable[4] = float[4](0.50, 0.66, 0.83, 1.0);
    float aoShade = aoTable[aoLevel];

    vec3 tint = vec3(1.0);
    if (tintMode == 1u) {
        tint = u_grassColor;
    } else if (tintMode == 2u) {
        tint = u_foliageColor;
    } else if (tintMode == 3u) {
        float p = float(extra) / 3.0;
        tint = vec3(0.35 + p * 0.65, p * 0.08, 0.0);
    }

    v_shadeColor = tint * (dirShade * aoShade);

    vec3 relPos = u_chunkOffset + vec3(lx, ly, lz);
    v_fogDist   = length(relPos);

    gl_Position = u_viewProj * vec4(relPos, 1.0);
}