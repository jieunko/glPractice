#version 410

layout (location = 0) in vec4 VS_IN_Position;
layout (location = 1) in vec4 VS_IN_TexCoord;
layout (location = 2) in vec4 VS_IN_Normal;
layout (location = 3) in vec4 VS_IN_Tangent;
layout (location = 4) in vec4 VS_IN_Bitangent;
layout (std140) uniform Transforms //#binding 0
{ 
	mat4 model;
	mat4 view;
	mat4 projection;
};
out vec3 PS_IN_FragPos;
out vec3 PS_IN_Normal;
out vec2 PS_IN_TexCoord;
void main()
{
    vec4 position = model * vec4(VS_IN_Position.xyz, 1.0);
	PS_IN_FragPos = position.xyz;
	PS_IN_Normal = mat3(model) * VS_IN_Normal.xyz;
	PS_IN_TexCoord = VS_IN_TexCoord.xy;
    gl_Position = projection * view * position;
}
