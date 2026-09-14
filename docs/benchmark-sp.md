# SP Benchmark

Use this external controller with a native SP package and native game assets.
It does not use Xvfb. The default environment is:

```text
SDL_VIDEODRIVER=offscreen
EGL_PLATFORM=surfaceless
SDL_AUDIODRIVER=dummy
```

The controller removes inherited `LIBGL_ALWAYS_SOFTWARE`. By default,
`GL_RENDERER` must contain `Intel`. It must not contain `llvmpipe` or
`softpipe`. The controller checks the reported `MODE` and the final PNG
dimensions. A resolution fallback causes failure. Use a package with the
SDL window-mode fallback correction for offscreen 1280 x 720 operation.
This controller does not change that engine code.

## Commands

From the repository root:

```sh
python3 scripts/benchmark-sp.py --renderer rdsp-rend2 --ffprobe
python3 scripts/benchmark-sp.py --renderer rdsp-vanilla
OJK_ASSETS=/path/to/GameData python3 scripts/benchmark-sp.py --package build/ready
```

The defaults are 1280 x 720, 15 measurement seconds, 5 warmup seconds,
3 runs, SSAO 1, and shadows 3. Use `--ssao 0` to disable SSAO.
`--shadows` accepts 1, 2, or 3. `--timeout` sets the startup, command, and
exit timeout; its default is 180 seconds. Use `--help` for all options.
The default asset directory is `GameData` in the repository.

Use `--reloads N` to measure N same-process map reloads after the FPS
measurement. Results include `reload_receipt_seconds` and `program_cache`
statistics (`linked`, `reused`, and `unused_released`). Reload times use
console receipt, not GPU completion. This option passed runtime checks with
the old and new renderer versions.

Use `--cvar NAME VALUE` for numeric or simple-word overrides. You can repeat
this option. The controller records the settings and writes a configuration
file instead of a large startup command line.

To use a desktop display, select its native driver:

```sh
python3 scripts/benchmark-sp.py --video-driver x11 --gpu Intel
python3 scripts/benchmark-sp.py --video-driver wayland --gpu NVIDIA
```

The desktop modes do not set `EGL_PLATFORM`. They still disable sound and
use a window. `--gpu` changes the required renderer substring. Software
renderers remain prohibited. Record the display mode for each comparison.

## Scene

Only `--map t2_wedge` is supported. The controller selects a fixed natural
view in the Krildor interior: `setviewpos 2688 640 -60 315`, third person,
FOV 80, aspect adjustment on, and HUD off. God mode protects the player.
Native NPCs stay active. The controller does not kill or add NPCs, freeze
AI, fire weapons, or use `ai-memory.cfg`.

This is not a deterministic actor stress test. NPCs, scripts, projectiles,
and player displacement can change the view. Check the final screenshot.
A controlled character-count scene requires a separate fixture in future.

## Measurement

Each run uses one new process, a unique `OJK_PROFILE`, and the package's
`launch-sp.sh`. A generated configuration and empty autoexec control settings
without exceeding the engine's startup-command limit. Runs share a separate
Mesa cache directory. Warmup periods
and fresh profiles are controlled. OS and driver cache state is not
controlled. The controller does not clear global caches. These are not
cold-cache runs; the first run can have different cache costs.

Startup timing starts immediately before process creation. It ends when
the controller receives the initial `OJK_BENCH_ACTIVE` echo from
`activeAction`. The action is set before `devmap`. No wait or quit command
is added after the map command. This time includes launcher and load work.
Console delivery can delay the receipt. It does not prove GPU completion.

After scene setup and warmup, the controller enables `com_speeds` between
the BEGIN and END console markers. Approximate throughput is the number
of work records divided by the monotonic marker-receipt interval. Command
delivery can extend the requested interval. Pipe receipt times are not
frame times. Console output and the controller can affect the result.

`engine_work_ms` contains nearest-rank p50, p95, and p99 values from the
integer `all` field. The `rf` and `bk` fields have separate work statistics.
These values are engine work times, not presentation frame times, GPU
timer results, or input latency. Startup time is separate from this phase.

VSync, frame caps, engine log files, and sound are disabled. The result
records the requested settings, including normal and specular mapping,
SSAO, and shadows. Renderer-specific settings need not affect both
renderers. This is not a claim of equal image quality.

## Results

Results are under `build/benchmark-sp/`. Each run has a JSON result, a
console log, and a profile with `screenshots/benchmark_end.png`. The suite
JSON includes per-run results and median approximate throughput. GPU
identity and available GLSL shader counts and load times come from the
console. An empty GLSL list means no matching summary was reported.

The controller keeps raw console output in memory during measurement. It
writes logs and JSON after measurement. It requests the screenshot after
END, waits for its named acknowledgement, then requests a normal quit.
The PNG header check and optional `--ffprobe` decode are outside the
measurement interval. The probe requires `ffprobe` on `PATH`. It does not
check scene content or image quality.

GPU mismatch, resolution mismatch, missing work records, unexpected EOF,
and timeouts cause a nonzero exit. Failure cleanup kills the process group.
Partial suite results show how many runs completed; do not treat them as
a complete comparison. Run the controller on the target hardware before you
report performance.

## Initial P630 Results

Measured on 2026-09-14 with Mesa 26.2.1 and hardware Intel HD Graphics P630.
Package: `20260914T082147268102281-af2e4d1c`. Each case used three processes,
five warmup seconds, and fifteen measurement seconds. All cases used
`cg_shadows 3`, no MSAA, no bloom, no VSync, and the fixed view above.

| Renderer | Resolution | SSAO | Median approximate FPS |
| --- | --- | --- | --- |
| Vanilla | 1280 x 720 | Not supported | 178.37 |
| Rend2 | 1280 x 720 | Off | 17.56 |
| Rend2 | 1280 x 720 | On | 16.41 |
| Rend2 | 640 x 360 | On | 17.20 |

Native actors remain active, so these are initial comparisons, not deterministic
performance guarantees. SSAO differences overlap with run-to-run variation.
Neither disabling SSAO nor reducing pixel count removed the large gap.
This supports CPU and driver-stall profiling before reducing visual quality.

The SSAO-on case reached the active marker in 27.09 seconds on the first process
and 10.48 and 9.64 seconds on later processes. Vanilla took 8.25 to 8.62 seconds.
Rend2 initialized 938 GLSL programs twice during startup and map loading.
The first process reported 20.41 and 2.87 seconds in GLSL initialization.
Later processes reported approximately three seconds for each initialization.
The first process used a new benchmark cache directory; OS cache state was not
controlled. These measurements do not reproduce a tenfold load-time difference.

Raw results are in `build/benchmark-sp/` under `rdsp-vanilla._in1uasp`,
`rdsp-rend2.x9cf86e0`, `rdsp-rend2.0kvcy26p`, and `rdsp-rend2.v78nrk1s`.
No renderer performance fix is included in these baseline results.

## Recent P630 Comparison

The paired baseline is `build/benchmark-sp/rdsp-rend2.7eyabcfh`; the optimized
result is `build/benchmark-sp/rdsp-rend2.x15tz3wl`. Both used hardware P630 at
1280 x 720, SSAO 1, `cg_shadows 3`, and unlimited FPS. Each used three fresh
processes, five warmup seconds, and fifteen measurement seconds. Each suite
had a separate, initially empty benchmark shader cache. OS cache state was
not controlled. The natural NPC scene is not deterministic.

| Measurement | Paired baseline | Optimized |
| --- | --- | --- |
| Median approximate FPS | 13.52 | 85.37 |
| First-process startup, seconds | 34.88 | 31.87 |
| Later-process startup, seconds | 12.15, 12.14 | 8.35, 8.51 |
| Map GLSL initialization, seconds | 3.5-3.7 | 0.03 |

Optimized throughput was 88.04, 85.37, and 77.28 FPS. Map initialization
reused 938 programs and linked none. The initial 16.41 FPS baseline above
is a separate historical result, not the paired baseline.

Three old same-process reloads in `rdsp-rend2.nofs55l2` took 7.92, 7.31,
and 7.36 seconds. Three new reloads in `rdsp-rend2.x15tz3wl`, run 2, took
4.20, 3.81, and 3.76 seconds. Engine-work p99 was about 89 ms in
`rdsp-rend2.nofs55l2`, compared with 16-20 ms in the new runs. These are
engine work times, not presentation latency.

These final runs do not use the experimental frame-pose tangent cache.
Cold shader compilation remains costly. These results do not establish a
tenfold load improvement or predict NVIDIA performance.

## Next Measurements

- Add separate timers for shader compilation, linking, and uniform setup.
- Measure character skinning and tangent generation by render pass.
- Profile full-screen passes, frame-buffer copies, and fence waits separately.
- Add controlled character-count scenes and camera routes with fixed inputs.
- Measure character worst cases and GPU times with GPU timers.
- Record frame-boundary intervals with a high-resolution monotonic clock before making precise stutter claims.
- Compare 640 x 360, 1280 x 720, and 1920 x 1080 under the same workload.

Offscreen tests cannot measure mouse-to-photon latency, desktop compositor
behavior, monitor scanout, or GTX 1080 Ti performance. Software-renderer results
must remain separate from hardware results.
