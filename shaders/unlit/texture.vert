#version 450

layout(location = 0) in vec2 a_position;
layout(location = 1) in vec2 a_texcoord;

layout(location = 0) out vec2 v_texcoord;

layout(set = 1, binding = 0) uniform Transform {
    mat4 u_model;
    mat4 u_view;
    mat4 u_proj;
};

void main() {
    gl_Position = u_proj * u_view * u_model * vec4(a_position, 0.0, 1.0);
    v_texcoord = a_texcoord;
}
