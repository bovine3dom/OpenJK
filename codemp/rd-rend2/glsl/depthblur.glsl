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
uniform sampler2D u_ScreenImageMap;
uniform sampler2D u_ScreenDepthMap;
uniform vec4 u_ViewInfo; // zfar / znear, zfar
uniform vec4 u_SSAOParams; // wider half-resolution denoising

in vec2 var_ScreenTex;

out vec4 out_Color;

float gauss[5] = float[5](0.30, 0.23, 0.097, 0.024, 0.0033);
float wideGauss[5] = float[5](0.1995, 0.1760, 0.1210, 0.0648, 0.0270);
//float gauss[4] = float[4](0.40, 0.24, 0.054, 0.0044);
//float gauss[3] = float[3](0.60, 0.19, 0.0066);
#define GAUSS_SIZE 5

float getLinearDepth(sampler2D depthMap, const vec2 tex, const float zFarDivZNear)
{
		float sampleZDivW = texture(depthMap, tex).r;
		return 1.0 / mix(zFarDivZNear, 1.0, sampleZDivW);
}

vec4 depthGaussian1D(sampler2D imageMap, sampler2D depthMap, vec2 tex, float zFarDivZNear, float zFar)
{
	float scale = 1.0 / 256.0;
	if (u_ViewInfo.z > 0.0)
	{
#if defined(USE_HORIZONTAL_BLUR)
		scale = 1.0 / float(textureSize(imageMap, 0).x);
#else
		scale = 1.0 / float(textureSize(imageMap, 0).y);
#endif
	}

#if defined(USE_HORIZONTAL_BLUR)
    vec2 direction = vec2(1.0, 0.0) * scale;
#else // if defined(USE_VERTICAL_BLUR)
	vec2 direction = vec2(0.0, 1.0) * scale;
#endif

	float hardwareDepth = texture(depthMap, tex).r;
	float depthCenter = zFar / mix(zFarDivZNear, 1.0, hardwareDepth);
	// Hardware depth is affine across a projected plane; view-space Z is not.
	float planeDepth = u_ViewInfo.z > 0.0 ? hardwareDepth : depthCenter;
	vec2 centerSlope = vec2(dFdx(planeDepth), dFdy(planeDepth)) / vec2(dFdx(tex.x), dFdy(tex.y));
	// GTAO sky samples are white, and the filter rejects cross-sky taps.
	// Keep derivatives above this exit for adjacent foreground fragments.
	if (u_ViewInfo.z > 0.0 && depthCenter >= zFar * 0.9999) return vec4(1.0);
	bool wide = u_ViewInfo.z > 0.0 && u_SSAOParams.x > 0.0;
	float centerWeight = wide ? wideGauss[0] : gauss[0];
	vec4 result = texture(imageMap, tex) * centerWeight;
	float total = centerWeight;

	int i, j;
	for (i = 0; i < 2; i++)
	{
		for (j = 1; j < GAUSS_SIZE; j++)
		{
			if (u_ViewInfo.z > 0.0 && !wide && j > 2) break;
			vec2 offset = direction * j;
			float depthSample = zFar * getLinearDepth(depthMap, tex + offset, zFarDivZNear);
			float depthExpected = planeDepth + dot(centerSlope, offset);
			if (u_ViewInfo.z > 0.0)
				depthExpected = zFar / mix(zFarDivZNear, 1.0, clamp(depthExpected, 0.0, 1.0));
			float threshold = u_ViewInfo.z > 0.0 ? max(0.05, u_ViewInfo.w * 0.02) : 5.0;
			if(abs(depthSample - depthExpected) < threshold &&
				(u_ViewInfo.z == 0.0 || (depthSample < zFar * 0.9999) == (depthCenter < zFar * 0.9999)))
			{
				float weight = wide ? wideGauss[j] : gauss[j];
				result += texture(imageMap, tex + offset) * weight;
				total += weight;
			}
		}

		direction = -direction;
	}

	return result / total;
}

vec4 upsampleAO(vec2 uv)
{
	float hardwareDepth = texture(u_ScreenDepthMap, uv).r;
	float z = u_ViewInfo.y / mix(u_ViewInfo.x, 1.0, hardwareDepth);
	if (z >= u_ViewInfo.y * 0.9999) return vec4(1.0);
	vec2 pixel = 1.0 / vec2(textureSize(u_ScreenDepthMap, 0));
	vec2 slope;
	for (int axis = 0; axis < 2; ++axis)
	{
		vec2 offset = vec2(0.0);
		offset[axis] = pixel[axis];
		float left = hardwareDepth - texture(u_ScreenDepthMap, uv - offset).r;
		float right = texture(u_ScreenDepthMap, uv + offset).r - hardwareDepth;
		slope[axis] = (abs(left) < abs(right) ? left : right) / pixel[axis];
	}
	vec2 size = vec2(textureSize(u_ScreenImageMap, 0));
	vec2 coordinate = uv * size - 0.5;
	vec2 base = floor(coordinate);
	vec2 fraction = fract(coordinate);
	float total = 0.0;
	vec4 result = vec4(0.0);
	for (int y = 0; y < 2; ++y)
	for (int x = 0; x < 2; ++x)
	{
		vec2 tap = clamp((base + vec2(x, y) + 0.5) / size, 0.5 / size, 1.0 - 0.5 / size);
		float sampleZ = u_ViewInfo.y * getLinearDepth(u_ScreenDepthMap, tap, u_ViewInfo.x);
		if (sampleZ >= u_ViewInfo.y * 0.9999) continue;
		float expectedDepth = clamp(hardwareDepth + dot(slope, tap - uv), 0.0, 1.0);
		float error = abs(sampleZ - u_ViewInfo.y / mix(u_ViewInfo.x, 1.0, expectedDepth));
		float weight = (x == 0 ? 1.0 - fraction.x : fraction.x) *
			(y == 0 ? 1.0 - fraction.y : fraction.y) *
			max(0.0, 1.0 - error / max(0.05, u_ViewInfo.w * 0.02));
		result += texture(u_ScreenImageMap, tap) * weight;
		total += weight;
	}
	return total > 0.00001 ? result / total : vec4(1.0);
}

void main()
{
	out_Color = u_ViewInfo.z > 1.5 ? upsampleAO(var_ScreenTex) :
		depthGaussian1D(u_ScreenImageMap, u_ScreenDepthMap, var_ScreenTex, u_ViewInfo.x, u_ViewInfo.y);
}
