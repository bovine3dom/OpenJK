// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <cmath>
#include <cstring>

namespace Atmosphere {

enum Parameter {
	Radius, Thickness, ObserverHeight, RayHeight, MieHeight, Rayleigh, Mie,
	Absorption, GroundAlbedo, Anisotropy, Illuminance, SunDirection, SunRadius,
	SunDisk, CloudStrength, CloudColor, SkyBlend, Count
};

struct Field {
	const char *name, *label, *unit, *help;
	int components;
	float minimum, maximum;
	bool logarithmic;
};

inline const Field* Fields() {
	static const Field fields[Count] = {
		{"radius", "Planet radius", "km", "Controls curvature and the horizon. Leave this fixed for ordinary color tuning.", 1, 1000, 10000, false},
		{"thickness", "Atmosphere thickness", "km", "Upper limit of the optical calculation. This is not cloud thickness.", 1, 20, 200, false},
		{"observerHeight", "Observer altitude", "km", "Fixed virtual altitude, not player height. 0.15 km is 150 metres.", 1, .001f, 10, true},
		{"rayHeight", "Rayleigh scale height", "km", "Molecular density falls to about 37 percent over this height. Larger values spread it higher.", 1, 1, 20, false},
		{"mieHeight", "Aerosol scale height", "km", "Aerosol density falls to about 37 percent over this height. The graph compares both density curves.", 1, .1f, 10, true},
		{"rayleigh", "Rayleigh scattering", "1/km", "RGB molecular scattering. A larger blue value makes the sky bluer. Similar channels reduce the blue bias. The swatch shows relative coefficients, not the resulting sky color.", 3, .0001f, .1f, true},
		{"mie", "Aerosol scattering", "1/km", "RGB scattering from dust and mist. Higher, similar values give a more neutral haze. Brightness also changes.", 3, .0001f, .1f, true},
		{"absorption", "Aerosol absorption", "1/km", "Removes light in each RGB channel. More blue absorption removes more blue. The swatch shows absorbed channels.", 3, 0, .1f, true},
		{"groundAlbedo", "Ground reflectance", "RGB", "Ground color used for the approximate sky bounce. This does not edit the terrain material.", 3, 0, 1, false},
		{"anisotropy", "Forward scattering", "", "Zero scatters equally in all directions. Higher values concentrate the aerosol halo toward the sun.", 1, 0, .9f, false},
		{"illuminance", "Atmosphere brightness", "relative", "Light intensity driving the sky calculation. Lower values darken it. Baked surface lighting is not changed.", 1, .1f, 64, true},
		{"sunDirection", "Sun direction", "XYZ", "Direction toward the light. Z must be positive and vector length must exceed 0.5. Rendering normalizes it. Compass: north is +Y, east is +X; center is overhead.", 3, -1, 1, false},
		{"sunRadius", "Sun disc radius", "radians", "Angular radius of the added disc. 0.006 is about 0.34 degrees. It has no visible disc effect when sunDisk is zero.", 1, .001f, .03f, true},
		{"sunDisk", "Sun disc brightness", "relative", "Zero disables the added disc, but the atmosphere still receives sunlight. Keep zero when preserving a painted sun.", 1, 0, 32, false},
		{"cloudStrength", "Retained cloud strength", "", "Strength of the cloud layer estimated from source colors. This does not create clouds or change their shapes.", 1, 0, 1, false},
		{"cloudColor", "Retained cloud light", "RGB", "Light contribution for the retained clouds. Lower, similar channels produce dimmer, greyer clouds. The swatch shows relative RGB balance.", 3, 0, 2, false},
		{"skyBlend", "Atmosphere blend", "", "Zero keeps the source sky. One uses the full generated result within its mask. Higher values can fade painted planets and treetops.", 1, 0, 1, false}
	};
	return fields;
}

// POD exchanged across the renderer API. No renderer-owned pointers escape.
struct Profile {
	char map[64], sky[64];
	float values[Count][3];
};

inline bool Valid(const Profile& profile) {
	if (!std::memchr(profile.map, 0, sizeof(profile.map)) || !profile.map[0] ||
		!std::memchr(profile.sky, 0, sizeof(profile.sky)) || !profile.sky[0]) return false;
	for (int i = 0; i < Count; ++i)
		for (int c = 0; c < Fields()[i].components; ++c) {
			const float v = profile.values[i][c];
			if (!std::isfinite(v) || v < Fields()[i].minimum || v > Fields()[i].maximum) return false;
		}
	const float* sun = profile.values[SunDirection];
	return sun[2] > 0 && sun[0]*sun[0] + sun[1]*sun[1] + sun[2]*sun[2] > .25f;
}

} // namespace Atmosphere
