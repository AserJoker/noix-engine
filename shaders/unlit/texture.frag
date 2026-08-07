#version 450

layout(location = 0) in vec2 v_texcoord;

layout(location = 0) out vec4 o_color;

layout(set = 2, binding = 0) uniform sampler2D u_albedo;

void main() {
    o_color = texture(u_albedo, v_texcoord);
}
