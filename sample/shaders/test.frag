#version 400

out vec4 PS_OUT_Color;
in vec3 PS_IN_FragPos;
in vec3 PS_IN_Normal;
in vec2 PS_IN_TexCoord;
uniform sampler2D s_Diffuse; //#slot 0
void main()
{
	vec3 light_pos = vec3(-200.0, 200.0, 0.0);
	vec3 n = normalize(PS_IN_Normal);
	vec3 l = normalize(light_pos - PS_IN_FragPos);
	float lambert = max(0.0f, dot(n, l));
    vec3 diffuse = vec3(.0, 1.0, .0); //texture(s_Diffuse, PS_IN_TexCoord).xyz;// + vec3(1.0);
	vec3 ambient = diffuse * 0.03;
	vec3 color = diffuse * lambert + ambient;

    PS_OUT_Color = vec4(l, 1.0);

    // HDR tonemapping
    //color = color / (color + vec3(1.0));
    // gamma correct
    //color = pow(color, vec3(1.0 / 2.2));

    //PS_OUT_Color = vec4(color, 1.0);
    //PS_OUT_Color = vec4(vec3(1.0, 0.0, 0.0), 1.0);
}