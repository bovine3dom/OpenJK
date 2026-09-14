# Weapon Wheel

Scroll up or down to cycle weapons. The existing `weapprev` and `weapnext`
bindings now show a radial weapon display instead of the bottom selection bar.
Number-key selection and automatic weapon changes use the same display.

The selected weapon has a larger icon, a warm-yellow sector, and an IBM Plex
Mono name label. The wheel shows owned weapons, with the existing empty-ammo
icons where applicable. The concussion rifle sits between the flechette and
rocket launcher, matching the game's cycling order.

Selection takes effect immediately. The wheel stays visible briefly and fades
in real time. It does not capture the mouse, block movement or attacks, or slow
game time. The game's weapon-switch delay, ammo checks, and weapon locks still
apply. Detpacks retain their existing empty-ammo selection rule.

The latest selection takes precedence. Scrolling closes a held Force wheel and
removes its slow-time multiplier. Force cycling or opening the held Force wheel
closes the weapon display. The two wheels do not overlap.

Vehicles and controlled entities keep their existing displays. The legacy
weapon bar remains the fallback when the radial UI is unavailable.

## Tests

Run these commands from the repository root:

```sh
c++ -std=c++11 -I shared tests/radial_wheel.cpp -o build/radial-wheel-test
build/radial-wheel-test
python3 scripts/test-force-wheel.py build/ready --renderer rdsp-vanilla --weapons
python3 scripts/test-force-wheel.py build/ready --renderer rdsp-rend2 --weapons
```

The headless checks use real scroll events. They cover selection order,
empty-ammo rules, continued movement and attacks, normal time scaling,
Force/weapon display ownership, and renderer restart. Logs and screenshots are
stored under `build/force-wheel-tests`.

`weaponwheel_status` prints the displayed and equipped weapon, inventory count,
visibility, and time scale for diagnosis.
