#version 330 core

in vec2 v_localUV;
flat in uint v_terrainSlot;
in vec3 v_shadeColor;
in float v_skyLight;
in float v_blockLight;
in float v_fogDist;

uniform sampler2D u_terrainTex;    // 256x256 terrain.png (16x16 tiles)
uniform float     u_skyBrightness; // [0.20 at night .. 1.0 at noon]
uniform float     u_torchFlicker;  // Continuous oscillation driver
uniform vec3      u_fogColor;
uniform float     u_fogStart;
uniform float     u_fogEnd;
uniform int       u_fogMode;       // 0 = Linear Overworld fog, 1 = Exponential Water/Lava fog
uniform float     u_fogDensity;

out vec4 FragColor;

void main() {
    float col = float(v_terrainSlot & 15u);
    float row = float(v_terrainSlot >> 4u);

    vec2 tileOrigin = vec2(col, row) / 16.0;
    vec2 wrappedUV  = fract(v_localUV);

    // Derive continuous time from u_torchFlicker (sin(time * 12.0) * 0.015)
    float animPhase = u_torchFlicker * 66.6667;

    // 1. Classic Beta 1.7.3 Water (205) & Lava (237) flow scroll
    if (v_terrainSlot == 205u || v_terrainSlot == 237u) {
        wrappedUV = fract(wrappedUV + vec2(0.0, animPhase * 0.35));
    }
    // 2. Classic Beta 1.7.3 Fire (31) flicker distortion
    else if (v_terrainSlot == 31u) {
        float wave = sin(wrappedUV.y * 12.56637 + animPhase * 4.0) * 0.06 * (1.0 - wrappedUV.y);
        wrappedUV.x = clamp(wrappedUV.x + wave, 0.01, 0.99);
    }

    // Half-texel inset (0.25 / 256) prevents bleeding across adjacent tiles
    vec2 insetUV = clamp(wrappedUV, vec2(0.002), vec2(0.998));
    vec2 atlasUV = tileOrigin + (insetUV / 16.0);

    vec4 texColor = texture(u_terrainTex, atlasUV);

    // 3. Authentic Notch TexturePortalFX.java Swirling Purple Vortex (Slot 14 = Portal)
    if (v_terrainSlot == 14u) {
        vec2 p = wrappedUV * 2.0 - 1.0;
        float dist  = length(p);
        float angle = atan(p.y, p.x);
        // Two overlapping rotating spiral waves matching Notch's 32-frame portal lookup
        float s1 = sin(dist * 10.0 - animPhase * 2.2 + angle * 2.0);
        float s2 = cos(dist * 7.0  + animPhase * 1.7 - angle * 3.0);
        float swirl = clamp((s1 + s2) * 0.25 + 0.5, 0.0, 1.0);
        vec3 portalRgb = vec3(
            0.35 + swirl * 0.55,
            0.06 + swirl * 0.18,
            0.65 + swirl * 0.35
        );
        texColor = vec4(portalRgb, 0.82);
    }

    if (texColor.a < 0.1) discard;

    // 4. Exact Notch WorldProvider.generateLightBrightnessTable() formula:
    //    float f1 = 1.0F - i / 15.0F;
    //    lightBrightnessTable[i] = (1.0F - f1) / (f1 * 3.0F + 1.0F) * (1.0F - minLight) + minLight;
    float effectiveSky   = v_skyLight * u_skyBrightness;
    float effectiveBlock = clamp(v_blockLight + u_torchFlicker * v_blockLight, 0.0, 1.0);
    float lightLevel     = clamp(max(effectiveSky, effectiveBlock), 0.0, 1.0);

    float f1        = 1.0 - lightLevel;
    float betaLight = ((1.0 - f1) / (f1 * 3.0 + 1.0)) * 0.95 + 0.05;

    // Warm torch/lava/glowstone tint bias
    vec3 warmBias = mix(vec3(1.0), vec3(1.06, 0.96, 0.84), clamp(effectiveBlock - effectiveSky, 0.0, 1.0));
    vec3 litColor = texColor.rgb * v_shadeColor * betaLight * warmBias;

    // Fullbright emissive boost for Nether Portal
    if (v_terrainSlot == 14u) {
        litColor = texColor.rgb * 1.15;
    }

    // Authentic Beta 1.7.3 translucent water alpha
    float finalAlpha = texColor.a;
    if (v_terrainSlot == 205u) {
        finalAlpha = 0.72;
    }

    // 5. Classic Beta 1.7.3 Distance / Underwater Fog
    float fogFactor = 0.0;
    if (u_fogMode == 1) {
        fogFactor = 1.0 - exp(-u_fogDensity * v_fogDist);
    } else {
        fogFactor = (v_fogDist - u_fogStart) / max(0.001, (u_fogEnd - u_fogStart));
    }
    fogFactor = clamp(fogFactor, 0.0, 1.0);

    vec3 finalRgb = mix(litColor, u_fogColor, fogFactor);
    FragColor = vec4(finalRgb, finalAlpha);
}