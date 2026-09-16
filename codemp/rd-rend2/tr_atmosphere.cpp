#include "tr_local.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace {
constexpr int skyWidth = 256, skyHeight = 128;
constexpr int viewSamples = 32, sunSamples = 12, ambientHeights = 32;
constexpr double pi = 3.14159265358979323846;

struct AtmosphereProfile {
	float radius, thickness, observerHeight, rayHeight, mieHeight, anisotropy, illuminance;
	vec3_t rayleigh, mie, absorption, groundAlbedo;
};

// All integration distances are kilometres. Double precision prevents loss of
// ground-level altitude when adding the small observer height to planet radius.
double ExitDistance(double radius, double height, double cosine, double top)
{
	const double r = radius + height;
	return -r * cosine + sqrt(r * r * cosine * cosine + (top - r) * (top + r));
}

void SunOpticalDepth(const AtmosphereProfile &p, double height, double cosine, double &ray, double &mie)
{
	const double distance = ExitDistance(p.radius, height, cosine, double(p.radius) + p.thickness);
	ray = mie = 0;
	for (int i = 0; i < sunSamples; ++i)
	{
		const double a = distance * (double(i) / sunSamples) * (double(i) / sunSamples);
		const double b = distance * (double(i + 1) / sunSamples) * (double(i + 1) / sunSamples);
		const double t = (a + b) * 0.5, r = p.radius + height;
		const double h = sqrt(r * r + t * t + 2 * r * t * cosine) - p.radius;
		ray += exp(-h / p.rayHeight) * (b - a);
		mie += exp(-h / p.mieHeight) * (b - a);
	}
}

void SkyRadiance(const AtmosphereProfile &p, const vec3_t sun, double azimuth, double elevation,
	float *color, const vec3_t *ambient = nullptr)
{
	const double direction[3] = {cos(azimuth) * cos(elevation), sin(azimuth) * cos(elevation), sin(elevation)};
	const double mu = direction[0] * sun[0] + direction[1] * sun[1] + direction[2] * sun[2];
	const double g = p.anisotropy;
	const double rayPhase = 3 * (1 + mu * mu) / (16 * pi);
	const double miePhase = (1 - g * g) / (4 * pi * pow(1 + g * g - 2 * g * mu, 1.5));
	const double distance = ExitDistance(p.radius, p.observerHeight, direction[2], double(p.radius) + p.thickness);
	const double observerRadius = double(p.radius) + p.observerHeight;
	double transmission[3] = {1, 1, 1};
	for (int i = 0; i < viewSamples; ++i)
	{
		const double a = distance * (double(i) / viewSamples) * (double(i) / viewSamples);
		const double b = distance * (double(i + 1) / viewSamples) * (double(i + 1) / viewSamples);
		const double t = (a + b) * 0.5;
		const double point[3] = {direction[0] * t, direction[1] * t, observerRadius + direction[2] * t};
		const double r = sqrt(point[0] * point[0] + point[1] * point[1] + point[2] * point[2]);
		const double h = r - p.radius;
		const double rayDensity = exp(-h / p.rayHeight), mieDensity = exp(-h / p.mieHeight);
		const double sunCosine = (point[0] * sun[0] + point[1] * sun[1] + point[2] * sun[2]) / r;
		double rayDepth, mieDepth;
		SunOpticalDepth(p, h, sunCosine, rayDepth, mieDepth);
		const double level = std::min(sqrt(std::max(h, 0.0) / p.thickness), 1.0) * (ambientHeights - 1);
		const int low = int(level), high = std::min(low + 1, ambientHeights - 1);
		for (int c = 0; c < 3; ++c)
		{
			const double mieExtinction = p.mie[c] + p.absorption[c];
			const double extinction = p.rayleigh[c] * rayDensity + mieExtinction * mieDensity;
			const double stepTransmission = exp(-extinction * (b - a));
			const double sunTransmission = exp(-p.rayleigh[c] * rayDepth - mieExtinction * mieDepth);
			const double scattering = p.rayleigh[c] * rayDensity * rayPhase + p.mie[c] * mieDensity * miePhase;
			double source = p.illuminance * sunTransmission * scattering;
			if (ambient)
				source += (p.rayleigh[c] * rayDensity + p.mie[c] * mieDensity) *
					(ambient[low][c] * (1 - (level - low)) + ambient[high][c] * (level - low));
			color[c] += float(transmission[c] * source *
				(1 - stepTransmission) / std::max(extinction, 1e-12));
			transmission[c] *= stepTransmission;
		}
	}
	color[3] = 1;
}

void AmbientRadiance(const AtmosphereProfile &p, const vec3_t sun, vec3_t *ambient)
{
	// A bounded second-order approximation: hemisphere-averaged single
	// scattering plus Lambertian ground bounce. Higher orders remain deferred.
	double groundRay, groundMie;
	SunOpticalDepth(p, 0, sun[2], groundRay, groundMie);
	for (int level = 0; level < ambientHeights; ++level)
	{
		AtmosphereProfile sample = p;
		const double fraction = double(level) / (ambientHeights - 1);
		sample.observerHeight = float(std::max(0.001, fraction * fraction * (p.thickness - 0.001)));
		for (int direction = 0; direction < 16; ++direction)
		{
			vec4_t color = {};
			SkyRadiance(sample, sun, direction * 2.3999632297, asin((direction + 0.5) / 16), color);
			for (int c = 0; c < 3; ++c) ambient[level][c] += color[c] / 32;
		}
		for (int c = 0; c < 3; ++c)
		{
			const double rayDepth = p.rayHeight * (1 - exp(-sample.observerHeight / p.rayHeight));
			const double mieDepth = p.mieHeight * (1 - exp(-sample.observerHeight / p.mieHeight));
			const double transmission = exp(-p.rayleigh[c] * (groundRay + rayDepth) -
				(p.mie[c] + p.absorption[c]) * (groundMie + mieDepth));
			ambient[level][c] += float(0.5 * p.groundAlbedo[c] * p.illuminance * sun[2] * transmission / pi);
		}
	}
}
}

static void BuildAtmosphere(world_t *world, const Atmosphere::Profile &settings)
{
	using namespace Atmosphere;
	AtmosphereProfile profile = {};
	profile.radius = settings.values[Radius][0];
	profile.thickness = settings.values[Thickness][0];
	profile.observerHeight = settings.values[ObserverHeight][0];
	profile.rayHeight = settings.values[RayHeight][0];
	profile.mieHeight = settings.values[MieHeight][0];
	profile.anisotropy = settings.values[Anisotropy][0];
	profile.illuminance = settings.values[Illuminance][0];
	VectorCopy(settings.values[Rayleigh], profile.rayleigh);
	VectorCopy(settings.values[Mie], profile.mie);
	VectorCopy(settings.values[Absorption], profile.absorption);
	VectorCopy(settings.values[GroundAlbedo], profile.groundAlbedo);
	VectorCopy(settings.values[SunDirection], world->atmosphereSun);
	world->atmosphereSun[3] = settings.values[SunRadius][0];
	VectorSet4(world->atmosphereParams, 0, settings.values[CloudStrength][0],
		settings.values[SunDisk][0], settings.values[SkyBlend][0]);
	VectorCopy(settings.values[CloudColor], world->atmosphereCloudColor);
	Q_strncpyz(world->atmosphereSky, settings.sky, sizeof(world->atmosphereSky));
	VectorNormalize(world->atmosphereSun);
	const int start = ri.Milliseconds();
	vec3_t ambient[ambientHeights] = {};
	AmbientRadiance(profile, world->atmosphereSun, ambient);
	std::vector<float> pixels(skyWidth * skyHeight * 4, 0);
	for (int y = 0; y < skyHeight; ++y)
		for (int x = 0; x < skyWidth; ++x)
		{
			const double v = double(y) / (skyHeight - 1);
			SkyRadiance(profile, world->atmosphereSun, (double(x) / (skyWidth - 1) - 0.5) * 2 * pi,
				v * v * pi * 0.5, &pixels[(y * skyWidth + x) * 4], ambient);
		}
	// Reloads update the same texture. Normal renderer shutdown owns cleanup.
	const char *name = va("*atmosphere:%s", world->baseName);
	const int flags = IMGFLAG_CLAMPTOEDGE | IMGFLAG_NO_COMPRESSION | IMGFLAG_NOLIGHTSCALE;
	world->atmosphereImage = R_GetLoadedImage(name, flags);
	if (!world->atmosphereImage)
		world->atmosphereImage = R_CreateImage(name, nullptr, skyWidth, skyHeight, IMGTYPE_COLORALPHA, flags, GL_RGBA16F);
	GL_Bind(world->atmosphereImage);
	qglTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, skyWidth, skyHeight, GL_RGBA, GL_FLOAT, pixels.data());
	world->atmosphereProfile = settings;
	ri.Printf(PRINT_ALL, "Atmosphere: %s, sky=%s, 256x128 LUT, %d ms\n",
		world->baseName, world->atmosphereSky, ri.Milliseconds() - start);
}

void R_LoadAtmosphere(world_t *world)
{
	world->atmosphereImage = nullptr;
#ifdef REND2_SP
	COM_ParseSession session;
#else
	COM_BeginParseSession("atmosphere");
#endif
	char *buffer = nullptr;
	const int length = ri.FS_ReadFile(va("maps/%s.atmosphere", world->baseName), (void **)&buffer);
	if (!buffer) return;
	Atmosphere::Profile settings = {};
	Q_strncpyz(settings.map, world->baseName, sizeof(settings.map));
	settings.values[Atmosphere::SkyBlend][0] = 1;
	bool seen[Atmosphere::Count] = {};
	const char *text = buffer;
	bool valid = length > 0 && length < 4096 && strlen(world->baseName) + 13 < MAX_QPATH;
	valid = valid && !Q_stricmp(COM_ParseExt(&text, qtrue), "atmosphere");
	valid = valid && !Q_stricmp(COM_ParseExt(&text, qtrue), "1");
	valid = valid && !Q_stricmp(COM_ParseExt(&text, qtrue), "sky");
	const char *sky = COM_ParseExt(&text, qtrue);
	valid = valid && strlen(sky) < sizeof(settings.sky);
	Q_strncpyz(settings.sky, sky, sizeof(settings.sky));
	while (valid)
	{
		const char *token = COM_ParseExt(&text, qtrue);
		if (!*token) break;
		int field = 0;
		while (field < Atmosphere::Count && Q_stricmp(token, Atmosphere::Fields()[field].name)) ++field;
		if (field == Atmosphere::Count || seen[field]) { valid = false; break; }
		seen[field] = true;
		for (int c = 0; c < Atmosphere::Fields()[field].components; ++c)
		{
			token = COM_ParseExt(&text, qtrue);
			char *end;
			settings.values[field][c] = strtof(token, &end);
			valid = valid && end != token && !*end;
		}
	}
	for (int i = 0; i < Atmosphere::Count; ++i) valid = valid && (seen[i] || i == Atmosphere::SkyBlend);
	ri.FS_FreeFile(buffer);
	if (!valid || !Atmosphere::Valid(settings))
	{
		ri.Printf(PRINT_WARNING, "Invalid atmosphere profile: %s\n", world->baseName);
		return;
	}
	BuildAtmosphere(world, settings);
}

bool RE_GetAtmosphere(Atmosphere::Profile *profile)
{
	if (!profile || !tr.world || !tr.world->atmosphereImage) return false;
	*profile = tr.world->atmosphereProfile;
	return true;
}

bool RE_ApplyAtmosphere(const Atmosphere::Profile *profile)
{
	if (!profile || !Atmosphere::Valid(*profile) || !tr.world ||
		Q_stricmp(profile->map, tr.world->baseName)) return false;
	bool referenced = false;
	for (int i = 0; i < tr.world->numShaders; ++i)
		if (!Q_stricmp(tr.world->shaders[i].shader, profile->sky)) { referenced = true; break; }
	shader_t *sky = R_FindShaderByName(profile->sky);
	if (!referenced || !sky || !sky->isSky || !sky->sky.cubemap) return false;
	R_IssuePendingRenderCommands();
	BuildAtmosphere(tr.world, *profile);
	return true;
}

void R_AtmosphereReload_f()
{
	if (!tr.world) return;
	R_IssuePendingRenderCommands();
	R_LoadAtmosphere(tr.world);
}
