/*[Vertex]*/
out vec2 var_ScreenTex;

void main()
{
	const vec2 positions[] = vec2[3](
		vec2(-1.0f,  1.0f),
		vec2(-1.0f, -3.0f),
		vec2( 3.0f,  1.0f)
	);

	const vec2 texcoords[] = vec2[3](
		vec2( 0.0f,  1.0f),
		vec2( 0.0f, -1.0f),
		vec2( 2.0f,  1.0f)
	);

	gl_Position = vec4(positions[gl_VertexID], 0.0, 1.0);
	var_ScreenTex = texcoords[gl_VertexID];
}

/*[Fragment]*/
uniform sampler2D u_ScreenDepthMap;
uniform sampler2D u_ScreenImageMap;
uniform vec4 u_ViewInfo; // zfar / znear, zfar
uniform vec4 u_SSAOParams; // 0 legacy or GTAO quality + 1, radius, projection scale XY
uniform int u_SSAODebug;
uniform vec3 u_ViewForward, u_ViewLeft, u_ViewUp;

in vec2 var_ScreenTex;

out vec4 out_Color;

vec2 poissonDisc[9] = vec2[9](
vec2(-0.7055767, 0.196515),    vec2(0.3524343, -0.7791386),
vec2(0.2391056, 0.9189604),    vec2(-0.07580382, -0.09224417),
vec2(0.5784913, -0.002528916), vec2(0.192888, 0.4064181),
vec2(-0.6335801, -0.5247476),  vec2(-0.5579782, 0.7491854),
vec2(0.7320465, 0.6317794)
);

// Input: It uses texture coords as the random number seed.
// Output: Random number: [0,1), that is between 0.0 and 0.999999... inclusive.
// Author: Michael Pohoreski
// Copyright: Copyleft 2012 :-)
// Source: http://stackoverflow.com/questions/5149544/can-i-generate-a-random-number-inside-a-pixel-shader

float random( const vec2 p )
{
  // We need irrationals for pseudo randomness.
  // Most (all?) known transcendental numbers will (generally) work.
  const vec2 r = vec2(
    23.1406926327792690,  // e^pi (Gelfond's constant)
     2.6651441426902251); // 2^sqrt(2) (Gelfond-Schneider constant)
  //return fract( cos( mod( 123456789., 1e-7 + 256. * dot(p,r) ) ) );
  return mod( 123456789., 1e-7 + 256. * dot(p,r) );
}

mat2 randomRotation( const vec2 p )
{
	float r = random(p);
	float sinr = sin(r);
	float cosr = cos(r);
	return mat2(cosr, sinr, -sinr, cosr);
}

float getLinearDepth(sampler2D depthMap, const vec2 tex, const float zFarDivZNear)
{
		float sampleZDivW = texture(depthMap, tex).r;
		return 1.0 / mix(zFarDivZNear, 1.0, sampleZDivW);
}

float ambientOcclusion(sampler2D depthMap, const vec2 tex, const float zFarDivZNear, const float zFar)
{
	float result = 0;

	float sampleZ = zFar * getLinearDepth(depthMap, tex, zFarDivZNear);

	vec2 expectedSlope = vec2(dFdx(sampleZ), dFdy(sampleZ)) / vec2(dFdx(tex.x), dFdy(tex.y));

	// Compute derivatives before this branch, including at silhouette edges.
	if (sampleZ >= zFar || length(expectedSlope) > 5000.0)
		return 1.0;

	vec2 offsetScale = (3.0 * u_SSAOParams.y / sampleZ) * u_SSAOParams.zw;

	mat2 rmat = randomRotation(tex);

	int i;
	for (i = 0; i < 9; i++)
	{
		vec2 offset = rmat * poissonDisc[i] * offsetScale;
		float sampleZ2 = zFar * getLinearDepth(depthMap, tex + offset, zFarDivZNear);

		if (abs(sampleZ - sampleZ2) > 20.0)
			result += 1.0;
		else
		{
			float expectedZ = sampleZ + dot(expectedSlope, offset);
			result += step(expectedZ - 1.0, sampleZ2);
		}
	}

	result *= 0.11111;

	return result;
}

// View-space hemisphere slices with the cosine-weighted GTAO integral.
// Jimenez et al., Practical Real-Time Strategies for Accurate Indirect Occlusion (2016).
vec3 positionAt(vec2 uv)
{
	float z = u_ViewInfo.y * getLinearDepth(u_ScreenDepthMap, uv, u_ViewInfo.x);
	vec2 tanHalfFov = vec2(0.83909963, 0.62932472) / u_SSAOParams.zw;
	return vec3((uv * 2.0 - 1.0) * tanHalfFov * z, z);
}

vec3 surfaceNormalAt(vec2 uv, vec3 p, vec2 pixel)
{
	vec3 l = p - positionAt(uv - vec2(pixel.x, 0));
	vec3 r = positionAt(uv + vec2(pixel.x, 0)) - p;
	vec3 b = p - positionAt(uv - vec2(0, pixel.y));
	vec3 t = positionAt(uv + vec2(0, pixel.y)) - p;
	vec3 normal = cross(abs(l.z) < abs(r.z) ? l : r, abs(b.z) < abs(t.z) ? b : t);
	float lengthSquared = dot(normal, normal);
	if (lengthSquared < 1e-12 || any(isnan(normal)) || any(isinf(normal)))
		return vec3(0.0);
	normal *= inversesqrt(lengthSquared);
	if (dot(normal, -p) < 0.0) normal = -normal;
	return normal;
}

#if defined(USE_GTAO_BENT_NORMALS)
vec3 safeDirection(vec3 direction, vec3 fallback)
{
	float lengthSquared = dot(direction, direction);
	if (lengthSquared < 1e-8 || any(isnan(direction)) || any(isinf(direction)))
		return fallback;
	return direction * inversesqrt(lengthSquared);
}

vec4 packVisibility(float visibility, vec3 bentNormal)
{
	if (isnan(visibility) || isinf(visibility)) visibility = 1.0;
	return vec4(clamp(visibility, 0.0, 1.0), bentNormal * 0.5 + 0.5);
}

float gtao(vec2 uv, out vec3 bentNormal)
#else
float gtao(vec2 uv)
#endif
{
	vec2 pixel = 1.0 / vec2(textureSize(u_ScreenDepthMap, 0));
	vec3 p = positionAt(uv);
	if (p.z >= u_ViewInfo.y * 0.9999)
	{
#if defined(USE_GTAO_BENT_NORMALS)
		bentNormal = vec3(0.0, 0.0, -1.0);
#endif
		return 1.0;
	}
	vec3 view = normalize(-p);
	vec3 normal = surfaceNormalAt(uv, p, pixel);
	if (dot(normal, normal) < 1e-12)
	{
#if defined(USE_GTAO_BENT_NORMALS)
		bentNormal = view;
#endif
		return 1.0;
	}
	int quality = int(u_SSAOParams.x) - 1;
	int slices = quality == 3 ? 3 : 2;
	int steps = quality == 0 ? 1 : (quality == 1 ? 2 : 4);
	float radius = 12.0 * u_SSAOParams.y;
	vec2 tanHalfFov = vec2(0.83909963, 0.62932472) / u_SSAOParams.zw;
	vec2 extent = radius / (2.0 * tanHalfFov * p.z);
	// Fixed spatial noise: no frame history or temporal jitter is required.
	// Index the AO pixel grid, not the full-resolution depth grid. Subsampling
	// the latter skips noise samples and introduces a pattern at half resolution.
	float noise = fract(52.9829189 * fract(dot(floor(gl_FragCoord.xy), vec2(0.06711056, 0.00583715))));
	float visibility = 0.0;
#if defined(USE_GTAO_BENT_NORMALS)
	vec3 bentSum = vec3(0.0);
#endif
	for (int slice = 0; slice < slices; ++slice)
	{
		float angle = 3.14159265 * (float(slice) + noise) / float(slices);
		vec2 direction = vec2(cos(angle), sin(angle));
		vec3 tangent = vec3(direction, 0.0);
		tangent = normalize(tangent - view * dot(tangent, view));
		vec3 axis = cross(tangent, view);
		vec3 projected = normal - axis * dot(normal, axis);
		float lengthN = length(projected);
		float n = lengthN > 0.000001 ? atan(dot(projected, tangent), dot(projected, view)) : 0.0;
		vec2 horizons = vec2(-1.0);
		for (int side = 0; side < 2; ++side)
		{
			float signSide = side == 0 ? -1.0 : 1.0;
			for (int stepIndex = 0; stepIndex < steps; ++stepIndex)
			{
				float distanceUV = (float(stepIndex) + 0.5 + 0.5 * noise) / float(steps);
				vec2 offset = signSide * direction * extent * distanceUV * distanceUV;
				vec2 sampleUV = uv + offset;
				if (any(lessThan(sampleUV, pixel * 0.5)) || any(greaterThan(sampleUV, 1.0 - pixel * 0.5))) continue;
				vec3 delta = positionAt(sampleUV) - p;
				float distanceSquared = dot(delta, delta);
				if (distanceSquared < 0.000001 || distanceSquared >= radius * radius) continue;
				float inverseDistance = inversesqrt(distanceSquared);
				float distanceVS = distanceSquared * inverseDistance;
				float horizon = dot(delta, view) * inverseDistance;
				float weight = 1.0 - smoothstep(radius * 0.6, radius, distanceVS);
				horizons[side] = max(horizons[side], mix(-1.0, horizon, weight));
			}
		}
		vec2 h = vec2(-1.0, 1.0) * acos(clamp(horizons, -1.0, 1.0));
		h = clamp(h, n - 1.57079633, n + 1.57079633);
		vec2 integral = cos(n) + 2.0 * h * sin(n) - cos(2.0 * h - n);
		visibility += lengthN * 0.25 * (integral.x + integral.y);
#if defined(USE_GTAO_BENT_NORMALS)
		float t0 = (6.0 * sin(h.x - n) - sin(3.0 * h.x - n) +
			6.0 * sin(h.y - n) - sin(3.0 * h.y - n) + 16.0 * sin(n) -
			3.0 * (sin(h.x + n) + sin(h.y + n))) / 12.0;
		float t1 = (-cos(3.0 * h.x - n) - cos(3.0 * h.y - n) + 8.0 * cos(n) -
			3.0 * (cos(h.x + n) + cos(h.y + n))) / 12.0;
		bentSum += (tangent * t0 + view * t1) * lengthN;
#endif
	}
#if defined(USE_GTAO_BENT_NORMALS)
	bentNormal = safeDirection(bentSum, normal);
#endif
	return clamp(visibility / float(slices), 0.0, 1.0);
}

void main()
{
	if (u_SSAODebug == 1)
	{
		out_Color = vec4(vec3(texture(u_ScreenDepthMap, var_ScreenTex).r >= 1.0 ? 1.0 : 0.0), 1.0);
		return;
	}
	if (u_SSAODebug == 2)
	{
		out_Color = vec4(vec3(texture(u_ScreenImageMap, var_ScreenTex).r), 1.0);
		return;
	}
	if (u_SSAODebug == 3)
	{
		if (texture(u_ScreenDepthMap, var_ScreenTex).r >= 0.9999)
		{
			out_Color = vec4(1.0);
			return;
		}
		vec4 sampleValue = texture(u_ScreenImageMap, var_ScreenTex);
		vec3 bentNormal = sampleValue.gba * 2.0 - 1.0;
		float lengthSquared = dot(bentNormal, bentNormal);
		vec3 p = positionAt(var_ScreenTex);
		vec3 normal = surfaceNormalAt(var_ScreenTex, p, 1.0 / vec2(textureSize(u_ScreenDepthMap, 0)));
		if (lengthSquared < 1e-8 || any(isnan(bentNormal)) || any(isinf(bentNormal)) ||
			dot(normal, normal) < 1e-12 || dot(bentNormal, normal) < -0.01)
		{
			out_Color = vec4(1.0, 0.0, 1.0, 1.0);
			return;
		}
		bentNormal *= inversesqrt(lengthSquared);
		vec3 worldNormal = -bentNormal.x * normalize(u_ViewLeft) +
			bentNormal.y * normalize(u_ViewUp) + bentNormal.z * normalize(u_ViewForward);
		out_Color = vec4(normalize(worldNormal) * 0.5 + 0.5, 1.0);
		return;
	}
#if defined(USE_GTAO_BENT_NORMALS)
	vec3 bentNormal;
	float result = gtao(var_ScreenTex, bentNormal);
	out_Color = packVisibility(result, bentNormal);
#else
	float result = u_SSAOParams.x > 0.0 ? gtao(var_ScreenTex) :
		ambientOcclusion(u_ScreenDepthMap, var_ScreenTex, u_ViewInfo.x, u_ViewInfo.y);
	out_Color = vec4(vec3(result), 1.0);
#endif
}
