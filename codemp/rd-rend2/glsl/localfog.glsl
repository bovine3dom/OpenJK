/*[Vertex]*/
void main()
{
	vec2 p = vec2(gl_VertexID == 2 ? 3.0 : -1.0, gl_VertexID == 1 ? 3.0 : -1.0);
	gl_Position = vec4(p, 0.0, 1.0);
}

/*[Fragment]*/
uniform vec3 u_ViewOrigin, u_ViewForward, u_ViewLeft, u_ViewUp;
uniform vec4 u_ViewInfo;
uniform vec4 u_VolumeMins[4], u_VolumeMaxs[4], u_VolumeColor[4];
uniform vec4 u_VolumeTorchOrigin, u_VolumeTorchDirection, u_VolumeTorchParams;
uniform mat4 u_ShadowMvp;
uniform sampler2D u_TorchShadowMap;
out vec4 out_Color;

float torchLight(vec3 position)
{
	if (u_VolumeTorchOrigin.w <= 0.0) return 0.0;
	vec3 L = position - u_VolumeTorchOrigin.xyz;
	float distanceToLight = max(length(L), 0.001);
	float cone = smoothstep(u_VolumeTorchDirection.w, u_VolumeTorchParams.y,
		dot(L / distanceToLight, u_VolumeTorchDirection.xyz));
	float fade = 1.0 - smoothstep(u_VolumeTorchParams.x * 0.6, u_VolumeTorchParams.x, distanceToLight);
	if (cone * fade <= 0.0) return 0.0;
	vec4 projected = u_ShadowMvp * vec4(position, 1.0);
	if (projected.w <= 0.0) return 0.0;
	vec3 uv = projected.xyz / projected.w * 0.5 + 0.5;
	if (any(lessThan(uv, vec3(0.0))) || any(greaterThan(uv, vec3(1.0)))) return 0.0;
	float shadow = step(uv.z - 0.00001, texture(u_TorchShadowMap, uv.xy).r);
	return shadow * cone * fade * u_VolumeTorchOrigin.w / (1.0 + distanceToLight * distanceToLight / 36864.0);
}

void main()
{
	float slice = floor(gl_FragCoord.y / 36.0);
	if (slice == 0.0) { out_Color = vec4(0.0, 0.0, 0.0, 1.0); return; }
	vec2 uv = vec2(gl_FragCoord.x / 64.0, mod(gl_FragCoord.y, 36.0) / 36.0);
	vec3 ray = u_ViewForward - (uv.x * 2.0 - 1.0) * u_ViewInfo.z * u_ViewLeft
		+ (uv.y * 2.0 - 1.0) * u_ViewInfo.w * u_ViewUp;
	float stepDepth = slice * (2048.0 / 32.0) / 24.0;
	float stepLength = stepDepth * length(ray);
	vec3 scattering = vec3(0.0);
	float transmission = 1.0;
	for (int stepIndex = 0; stepIndex < 24; ++stepIndex)
	{
		vec3 position = u_ViewOrigin + ray * ((float(stepIndex) + 0.5) * stepDepth);
		float density = 0.0;
		vec3 mediumColor = vec3(0.0);
		for (int volume = 0; volume < 4; ++volume)
		{
			if (u_VolumeMins[volume].w <= 0.0) continue;
			vec3 edge = min(position - u_VolumeMins[volume].xyz, u_VolumeMaxs[volume].xyz - position);
			float weight = smoothstep(0.0, 32.0, min(edge.x, min(edge.y, edge.z)));
			float d = weight * u_VolumeMins[volume].w;
			density += d;
			mediumColor += d * u_VolumeColor[volume].rgb;
		}
		if (density <= 0.0) continue;
		mediumColor /= density;
		// Static world-space variation avoids temporal trails from moving lights.
		density *= 0.8 + 0.2 * sin(position.x * 0.007 + sin(position.y * 0.009) + position.z * 0.011);
		float extinction = exp(-density * stepLength);
		float opacity = min(transmission * (1.0 - extinction), max(transmission - 0.65, 0.0));
		scattering += opacity * (mediumColor + vec3(1.0, 0.94, 0.82) * torchLight(position));
		transmission -= opacity;
	}
	out_Color = vec4(scattering, transmission);
}
