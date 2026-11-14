#version 330 core

layout(location = 0) in vec3 a_pos;
layout(location = 1) in vec3 a_normal;
layout(location = 2) in vec2 a_uv;
layout(location = 3) in vec4 a_color;

uniform mat4 u_viewProj;
uniform vec3 u_worldOffset;
uniform vec3 u_pivot;
uniform vec3 u_rotation;
uniform vec3 u_scale;
uniform int  u_isUI;

out vec2 v_uv;
out vec4 v_color;
out vec3 v_worldNormal;

vec3 rotateEulerXYZ(vec3 p, vec3 r) {
    float cx = cos(r.x), sx = sin(r.x);
    p = vec3(p.x, p.y * cx - p.z * sx, p.y * sx + p.z * cx);

    float cy = cos(r.y), sy = sin(r.y);
    p = vec3(p.x * cy + p.z * sy, p.y, -p.x * sy + p.z * cy);

    float cz = cos(r.z), sz = sin(r.z);
    p = vec3(p.x * cz - p.y * sz, p.x * sz + p.y * cz, p.z);
    return p;
}

void main() {
    v_uv    = a_uv;
    v_color = a_color;

    if (u_isUI == 1) {
        v_worldNormal = vec3(0.0, 1.0, 0.0);
        gl_Position   = u_viewProj * vec4(a_pos, 1.0);
    } else {
        vec3 scaledPos  = a_pos * u_scale;
        vec3 rotatedPos = rotateEulerXYZ(scaledPos - u_pivot, u_rotation) + u_pivot;
        v_worldNormal   = normalize(rotateEulerXYZ(a_normal, u_rotation));
        gl_Position     = u_viewProj * vec4(rotatedPos + u_worldOffset, 1.0);
    }
}