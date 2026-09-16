/*[Fragment]*/
uniform vec4 u_HazeColor;  // RGB, maximum opacity
uniform vec4 u_HazeParams; // density, reference height, height falloff, start distance
uniform vec4 u_HazeMins;   // box minimum, scattering enabled
uniform vec4 u_HazeMaxs;   // box maximum, maximum view distance

vec3 ApplyMapHaze(vec3 color, vec3 origin, vec3 position)
{
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
