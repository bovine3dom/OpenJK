/*[Vertex]*/
in vec3 attr_Position;
layout(std140) uniform Camera
{
	mat4 u_viewProjectionMatrix;
	vec4 u_ViewInfo;
	vec3 u_ViewOrigin;
	vec3 u_ViewForward;
	vec3 u_ViewLeft;
	vec3 u_ViewUp;
};
layout(std140) uniform Entity
{
	mat4 u_ModelMatrix;
};
out vec3 var_Direction;
void main()
{
	gl_Position = u_viewProjectionMatrix * u_ModelMatrix * vec4(attr_Position, 1.0);
	var_Direction = attr_Position;
}

/*[Fragment]*/
uniform samplerCube u_DiffuseMap;
uniform vec4 u_Color;
in vec3 var_Direction;
out vec4 out_Color;
void main()
{
	out_Color = texture(u_DiffuseMap, var_Direction) * u_Color;
}
