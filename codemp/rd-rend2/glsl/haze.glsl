/*[Fragment]*/
uniform vec4 u_HazeColor;  // RGB, maximum opacity
uniform vec4 u_HazeParams; // density, reference height, height falloff, start distance
uniform vec4 u_HazeMins;   // box minimum, scattering enabled
uniform vec4 u_HazeMaxs;   // box maximum, maximum view distance
uniform sampler2D u_VolumeMap;
uniform vec4 u_VolumeParams; // enabled, scattering enabled
uniform mat4 u_VolumeMatrix;

vec3 ApplyLocalFog(vec3 color, vec3 position)
{
	vec3 projected = (u_VolumeMatrix * vec4(position, 1.0)).xyz;
	if (projected.z <= 0.0) return color;
	vec2 uv = clamp(projected.xy / projected.z * 0.5 + 0.5,
		vec2(0.5 / 64.0, 0.5 / 36.0), vec2(1.0 - 0.5 / 64.0, 1.0 - 0.5 / 36.0));
	float slice = clamp(projected.z / 2048.0, 0.0, 1.0) * 32.0;
	float low = floor(slice), high = min(low + 1.0, 32.0);
	vec4 fog = mix(texture(u_VolumeMap, vec2(uv.x, (low + uv.y) / 33.0)),
		texture(u_VolumeMap, vec2(uv.x, (high + uv.y) / 33.0)), fract(slice));
	return color * fog.a + fog.rgb * u_VolumeParams.y;
}

vec3 ApplyMapHaze(vec3 color, vec3 origin, vec3 position)
{
	if (u_VolumeParams.x > 0.0) return ApplyLocalFog(color, position);
	if (u_HazeParams.x <= 0.0) return color;
	vec3 ray = position - origin;
	float distanceToSurface = length(ray);
	if (distanceToSurface <= u_HazeParams.w) return color;
	ray /= distanceToSurface;
	float enter = u_HazeParams.w;
	float leave = min(distanceToSurface, u_HazeMaxs.w);
	for (int axis = 0; axis < 3; ++axis)
	{
		if (abs(ray[axis]) < 0.00001)
		{
			if (origin[axis] < u_HazeMins[axis] || origin[axis] > u_HazeMaxs[axis]) return color;
		}
		else
		{
			float a = (u_HazeMins[axis] - origin[axis]) / ray[axis];
			float b = (u_HazeMaxs[axis] - origin[axis]) / ray[axis];
			enter = max(enter, min(a, b));
			leave = min(leave, max(a, b));
		}
	}
	if (leave <= enter) return color;
	float dz = ray.z * (leave - enter) * u_HazeParams.z;
	float integral = abs(dz) < 0.01 ? 1.0 - dz * 0.5 + dz * dz / 6.0
		: (1.0 - exp(clamp(-dz, -16.0, 16.0))) / dz;
	float density = u_HazeParams.x * exp(clamp(
		-(origin.z + ray.z * enter - u_HazeParams.y) * u_HazeParams.z, -8.0, 8.0));
	float opacity = min(1.0 - exp(-density * (leave - enter) * integral), u_HazeColor.a);
	return color * (1.0 - opacity) + u_HazeColor.rgb * (opacity * u_HazeMins.w);
}
