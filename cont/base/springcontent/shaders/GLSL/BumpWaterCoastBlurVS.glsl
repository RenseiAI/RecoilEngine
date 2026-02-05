#version 130

uniform mat4 u_mvpMatrix;

in vec3 pos;
in vec4 uv;

out vec4 vTexCoord;

void main() {
	vTexCoord = uv;
	gl_Position = u_mvpMatrix * vec4(pos, 1.0);
}