#version 130

uniform mat4 transformMatrix = mat4(1.0);

in vec2 vertexPos;
in vec2 texCoords;

out vec2 vTexCoords;

void main()
{
	gl_Position = transformMatrix * vec4(vertexPos, 0.0, 1.0);
	vTexCoords = texCoords;
}