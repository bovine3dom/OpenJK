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
uniform sampler2D u_AtmosphereMap;
uniform vec4 u_AtmosphereParams, u_AtmosphereSun;
uniform vec3 u_AtmosphereCloudColor;
in vec3 var_Direction;
uniform vec3 u_HazeOrigin;
out vec4 out_Color;
out vec4 out_Glow;
void main()
{
	vec3 direction = normalize(var_Direction);
	out_Color = texture(u_DiffuseMap, direction);
	if (u_AtmosphereParams.x > 0.0)
	{
		float elevation = asin(clamp(direction.z, 0.0, 1.0));
		vec2 uv = vec2(atan(direction.y, direction.x) / (2.0 * M_PI) + 0.5,
			sqrt(elevation / (0.5 * M_PI)));
		uv = (uv * vec2(255.0, 127.0) + 0.5) / vec2(256.0, 128.0);
		vec3 sky = texture(u_AtmosphereMap, uv).rgb;
		float sunAngle = acos(clamp(dot(direction, u_AtmosphereSun.xyz), -1.0, 1.0));
		// Retain the authored clouds, but remove the old painted sun from this layer.
		float cloud = smoothstep(0.60, 0.94, out_Color.r / max(out_Color.b, 0.001));
		cloud *= smoothstep(0.08, 0.2, direction.z) * smoothstep(0.03, 0.12, sunAngle);
		sky = mix(sky, sky * 0.65 + u_AtmosphereCloudColor, cloud * u_AtmosphereParams.y);
		sky += vec3(1.0, 0.94, 0.82) * u_AtmosphereParams.z *
			(1.0 - smoothstep(u_AtmosphereSun.w * 0.8, u_AtmosphereSun.w * 1.2, sunAngle));
		// The desert mountains are below the upper angular bound and have a
		// warm color. Use their source silhouette at the horizon transition.
		float horizonMask = smoothstep(-0.015, 0.025, out_Color.b - out_Color.r);
		float skyMask = max(smoothstep(0.025, 0.06, direction.z),
			horizonMask * smoothstep(-0.06, -0.02, direction.z));
		out_Color.rgb = mix(out_Color.rgb, sky, skyMask);
	}
	out_Color *= u_Color;
	out_Color.rgb = ApplyMapHaze(out_Color.rgb, u_HazeOrigin, u_HazeOrigin + normalize(var_Direction) * 8192.0);
	// Both scene attachments are active. An unwritten glow output is undefined.
	out_Glow = vec4(0.0, 0.0, 0.0, out_Color.a);
}
