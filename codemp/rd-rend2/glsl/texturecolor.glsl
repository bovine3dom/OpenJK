/*[Vertex]*/
in vec3 attr_Position;
in vec4 attr_TexCoord0;

uniform mat4 u_ModelViewProjectionMatrix;

out vec2 var_Tex1;


void main()
{
	gl_Position = u_ModelViewProjectionMatrix * vec4(attr_Position, 1.0);
	var_Tex1 = attr_TexCoord0.st;
}

/*[Fragment]*/
uniform sampler2D u_DiffuseMap;
uniform vec4 u_Color;
#ifdef REND2_SP
uniform int u_AlphaTestType;
uniform vec4 u_EnableTextures;
#endif

in vec2 var_Tex1;

out vec4 out_Color;


void main()
{
	out_Color = texture(u_DiffuseMap, var_Tex1) * u_Color;
#ifdef REND2_SP
	if (u_AlphaTestType == ALPHA_TEST_LT128 && out_Color.a >= 0.5)
		discard;
	if (u_EnableTextures.x > 0.0)
	{
		float light = clamp(dot(out_Color.rgb, vec3(0.299, 0.587, 0.114)) * 2.4 + 0.08, 0.0, 1.0);
		out_Color = vec4(vec3(0.75, 0.43, 0.07) * light, 1.0);
	}
#endif
}
