#version 330 core

in vec2 v_uv;
in vec4 v_color;
in vec3 v_worldNormal;

uniform sampler2D u_tex;
uniform int       u_useTex;
uniform int       u_isUI;
uniform float     u_lightLevel;
uniform vec4      u_hurtTint;
uniform int       u_fullbright;

out vec4 FragColor;

void main() {
    vec4 base = v_color;
    if (u_useTex == 1) {
        base *= texture(u_tex, v_uv);
    }
    if (base.a < 0.05) discard;

    if (u_isUI == 1 || u_fullbright == 1) {
        FragColor = base;
        return;
    }

    // Classic Mojang two-point entity directional lighting
    vec3 n = normalize(v_worldNormal);
    float d1 = max(dot(n, normalize(vec3(0.2, 1.0, -0.7))), 0.0);
    float d2 = max(dot(n, normalize(vec3(-0.2, 1.0, 0.7))), 0.0);
    float dirShade = clamp(0.42 + 0.58 * (d1 * 0.6 + d2 * 0.4), 0.35, 1.0);

    vec3 lit = base.rgb * dirShade * max(u_lightLevel, 0.06);
    lit = mix(lit, u_hurtTint.rgb, u_hurtTint.a);

    FragColor = vec4(lit, base.a);
}