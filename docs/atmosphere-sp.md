# t1_sour Sky Atmosphere Prototype

## Try the Prototype

Use the published Rend2 build. On `t1_sour`, enter:

```text
r_atmosphere 1
```

Set `r_atmosphere 0` to restore the stock sky. Both settings are live. The default
is `0` until desktop visual review is complete. Sky filtering, reconstructed sky
assets, and local fog have separate controls.

For a fixed rooftop preview, enter:

```text
exec rend2-atmosphere-preview.cfg
```

This fixture starts a new `t1_sour` map. It enables cheats and flight, equips a
blaster, and moves the camera above the rooftops. Use it outside
a campaign session that you need to save. Toggle `r_atmosphere` to compare the
same view. Look up to inspect the sun and its surrounding haze.

`r_compareEnhancements 2` compares all enhancements: base left, enhanced right.
Sky portals now take part in that comparison. Use `r_atmosphere` alone to isolate
the sky change from tone mapping and the other graphics settings.

## Current Scope

This increment changes the visible sky. It produces a continuous sky gradient,
a warm horizon, and directional sun haze. It retains the stock mountain image
and cloud pattern. Fixed-exposure checks found no change in the foreground
buildings or enclosed interiors.

The optical model has wavelength-dependent Rayleigh scattering, aerosol
scattering and absorption, and a forward-scattering phase function. A bounded
second-order approximation adds hemisphere-averaged sky scattering and ground
bounce. This is not the full multi-order Hillaire model.

A 256×128 RGBA16F sky table is built on the CPU during map loading. It uses 32
view samples and 12 sun-path samples. A 32-height ambient table uses 16 sky
directions per height. Double-precision integration preserves small altitudes
relative to the planet radius. The GPU sky table uses 0.25 MiB. Rendering samples
the table without a per-frame ray march or temporal history.

The first profile uses a fixed virtual observer altitude and fixed daylight.
The main view and sky portal sample the same directional sky. The portal origin
does not become a second atmosphere observer. The sun direction was measured
from the bright region of `desert_up.jpg`: approximately `(0.011, -0.146, 0.989)`.
The profile replaces the painted disc with one procedural disc at that direction.

Cloud coverage is estimated from the selected source image's color ratios.
The horizon mask combines its warm terrain colors with angular bounds. These
are art-specific approximations for the stock desert sky. They need review if
the source artwork changes. This increment does not create volumetric clouds.

The stock desert bottom face is 32×32; the other faces are 1024×1024. The cube
loader now resamples smaller square faces before upload. The original images
remain available for base comparison.

Aerial perspective on city geometry, outdoor path clipping, dynamic observer
altitude, and additional sun sources remain in the next integration stage.
The prototype does not change the sun light on baked surfaces.

## Profile Editing

The packaged profile is `OpenJK/maps/t1_sour.atmosphere`. For local edits, place
an override at `maps/t1_sour.atmosphere` in the active `OpenJK` profile. Run
`r_atmosphereReload` after an edit. Reloading updates the existing texture.

The versioned file starts with `atmosphere 1`, then `sky <shader-name>`. All named
fields in `scripts/maps/t1_sour.atmosphere` are required. Duplicate, unknown,
missing, out-of-range, and non-finite fields reject the profile and select the
stock sky. Missing map profiles also select stock rendering.

Distances are kilometres. Scattering and absorption coefficients are inverse
kilometres. `illuminance` and `sunDisk` use relative renderer light units.
`groundAlbedo` controls reflected ground light in the second-order estimate.
`cloudColor` and `cloudStrength` control the retained cloud layer. `sunRadius`
is an angular radius in radians. Only an above-horizon sun is supported here.

## Verification and Cost

```sh
python3 scripts/test-atmosphere.py
python3 scripts/test-atmosphere.py --msaa 4
python3 scripts/test-atmosphere.py --invalid-profile
```

The fixture writes stock, atmosphere, and restored captures for two interiors,
an outdoor opening, a rooftop, and the sun. It also creates `roof-comparison.png`.
It checks foreground preservation, invalid profiles, live profile reload,
renderer restart, save/load, map change, sky bloom, and sky-portal split comparison.

The MSAA 4 run measured a mean rooftop image change of about 6 on a 0–255 scale.
The upward sun view changed by about 29. The foreground, interior, live restore,
reload, restart, save/load, and split checks had zero image difference. The sky
region was black in the glow-only capture. A non-finite profile produced stock
images in all views. Both renderer smoke tests passed.

The P630 benchmark used a sky-heavy rooftop view, hidden entities, three
eight-second samples, and five-second warmups. It used the third-person camera.
These are isolated sky cost checks, not full gameplay frame-rate estimates.

| Resolution | Stock FPS | Atmosphere FPS | Stock sky-portal GPU time | Atmosphere sky-portal GPU time |
| --- | ---: | ---: | ---: | ---: |
| 1280×720 | 92.68 | 89.23 | 1.715 ms | 1.872 ms |
| 1920×1080 | 51.00 | 49.30 | 3.792 ms | 4.181 ms |

GPU figures are medians of the per-run medians. The added sky-pass cost was
about 0.16 ms at 720p and 0.39 ms at 1080p. Shared server load can affect throughput.
Table construction took about 0.57–0.64 seconds during load or explicit reload.
The table is currently prepared even when the live effect is off. Desktop GPU
measurements and final art approval are still outstanding.

See [the atmosphere plan](procedural-atmosphere-plan.md) for the next stages.
