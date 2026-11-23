#!/usr/bin/env python3
"""Numerical contract for DynamicSkySurface's default analytic day/twilight response."""

import math


def smooth(a, b, x):
    t = max(0.0, min(1.0, (x - a) / (b - a)))
    return t * t * (3.0 - 2.0 * t)


def mass(sine):
    raised = max(sine, 0.0)
    return min(40.0, 1.0 / max(raised + 0.025 * math.exp(-11.0 * raised), 0.025))


def sky(sun_degrees, view_y, mu):
    sun_y = math.sin(math.radians(sun_degrees))
    view_mass, solar_mass = mass(view_y), mass(sun_y)
    beta_r = (0.18, 0.42, 1.0)
    ozone = (0.08, 0.18, 0.025)
    transmission = tuple(math.exp(-(r * 0.080 + 0.018 + o * 0.13) * solar_mass)
                         for r, o in zip(beta_r, ozone))
    rayleigh = tuple(1.0 - math.exp(-r * view_mass * 0.115) for r in beta_r)
    mie = 1.0 - math.exp(-view_mass * 0.032)
    rayleigh_phase = 0.0596831 * (1.0 + mu * mu)
    g = 0.8
    mie_base = max(1.0 + g * g - 2.0 * g * mu, 0.0001)
    mie_phase = 0.1193662 * (1.0 - g * g) * (1.0 + mu * mu) / ((2.0 + g * g) * mie_base ** 1.5)
    daylight = smooth(-0.105, 0.035, sun_y)
    civil = smooth(-0.105, 0.02, sun_y) * (1.0 - smooth(0.035, 0.30, sun_y))
    nautical = smooth(-0.208, -0.105, sun_y) * (1.0 - smooth(-0.105, -0.07, sun_y)) * (1.0 - civil)
    astronomical = smooth(-0.31, -0.18, sun_y) * (1.0 - smooth(-0.18, -0.12, sun_y))
    night = 1.0 - smooth(-0.31, -0.105, sun_y)
    energy = 4.8
    colour = [(r * rayleigh_phase + c * mie * mie_phase * 0.24) * t * energy * daylight * 2.2
              for r, c, t in zip(rayleigh, (1.0, 0.91, 0.76), transmission)]

    horizon = math.exp(-abs(view_y) * 65.0)
    near = smooth(-0.12, 0.055, sun_y) * (1.0 - smooth(0.07, 0.28, sun_y))
    toward = max(mu, 0.0) ** 2.25
    white = smooth(-0.018, 0.012, sun_y)
    horizon_colour = tuple((1.0 - white) * a + white * b
                           for a, b in zip((1.0, 0.34, 0.025), (1.0, 0.91, 0.70)))
    for channel in range(3):
        colour[channel] += horizon_colour[channel] * horizon * (0.28 + 0.72 * toward) * near * energy * 0.52

    blue_band = math.exp(-abs(view_y) * 7.0)
    for channel, blue in enumerate((0.012, 0.035, 0.105)):
        colour[channel] += blue * blue_band * (nautical + astronomical * 0.42)
    twilight_zenith = math.sqrt(max(view_y, 0.0)) * civil
    for channel, blue in enumerate((0.006, 0.022, 0.080)):
        colour[channel] += blue * twilight_zenith
    zenith = max(0.0, min(1.0, view_y))
    for channel, (low, high) in enumerate(zip((0.00055, 0.00085, 0.0017), (0.0015, 0.0032, 0.0085))):
        colour[channel] += ((1.0 - zenith) * low + zenith * high) * night
    return tuple((c / (1.0 + c)) ** (1.0 / 2.2) for c in colour)


def require(condition, message):
    print(("[pass] " if condition else "[FAIL] ") + message)
    if not condition:
        raise SystemExit(1)


day = sky(35.0, 1.0, math.sin(math.radians(35.0)))
sunrise_toward = sky(0.0, 0.001, 1.0)
sunrise_away = sky(0.0, 0.001, -1.0)
sunset = sky(-3.0, 0.001, 1.0)
night = sky(-25.0, 1.0, math.sin(math.radians(-25.0)))

require(day[2] > day[1] > day[0], "day zenith remains blue")
require(min(sunrise_toward) > 0.65 and sunrise_toward[2] / sunrise_toward[0] > 0.75,
        "sunrise produces a bright near-white horizon toward the sun")
require(min(sunrise_away) > 0.45, "sunrise horizon band continues across the horizon")
require(sunset[0] > sunset[1] > sunset[2] * 1.7,
        "sunset is orange rather than a red-blue purple mixture")
require(night[2] > night[1] * 1.35 > night[0] and max(night) < 0.2,
        "night is dark blue-black")
print("[done] Dynamic analytic sky contract passed")
