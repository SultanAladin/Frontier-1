/*============================================================================================================================================
                                                       PARTICLEPRESETCATALOGUE.CPP
============================================================================================================================================*/
// 🧩 Authoritative C++ catalogue of all 102 Frontier Particle Studio presets (93 active + 9 quarantined)

#include "Engine/ParticleSpace/Presets/ParticlePresetCatalogue.h"

#include "Engine/ColourSpace/ColourSourceTypes.h"
#include "Engine/ParticleSpace/ParticleTintDriver.h"

#include <algorithm>
#include <array>

namespace Frontier::ParticleSpace
{

namespace
{

constexpr std::array<std::string_view, 15> kCategoryList = {
    "Cloth & Rigging",
    "Foliage & Flora",
    "Ferrofluid & Fields",
    "Fracture & Cracks",
    "Motion & Grids",
    "Silk & Curl",
    "Transitions",
    "Space & Distortion",
    "Shields & Tech",
    "Atmosphere & Sky",
    "Ballistics & Sparks",
    "Pyro & Water",
    "Wind-Blown",
    "Storms & Weather",
    "Quarantine",
};

constexpr std::array<ParticlePresetMetadata, 102> kPresetTable = {{
    ParticlePresetMetadata {
        "coin-fountain", "Coin fountain", "Ballistics & Sparks", "Coin", "Gold coins fired upward from one point, arcing over and bouncing onto the floor. They scatter into a loose heap; particles do not collide, so there is no true pile.",
        false, 400u, 0.0f, 0u, 0u, 5u, 0u, 0u, 0u, 9u, false,
        { 0.0f, 0.1f, 0.0f }, 0.1f, { 0.0f, 0.0f, 0.0f },
        { 0.0f, 1.0f, 0.0f }, 0.45f, 5.0f, 8.0f,
        6.0f, 9.0f, 0.16f, 0.16f, 1.0f,
        1.0f, 0.08f, 0.15f, 0.35f, 0.0f, 0.0f,
        0.85f, 2.2f, 0.24f, 1.75f,
        { 1.0f, 0.5776f, 0.0467f, 1.0f },
        { 1.0f, 0.8963f, 0.4342f, 1.0f },
        { 1.0f, 0.4342f, 0.0144f, 1.0f },
        "modeDefault", "2026"
    },
    ParticlePresetMetadata {
        "magnetic-particles", "Magnetic particles", "Ballistics & Sparks", "Dipole Guided", "Glowing particles steered by a magnetic dipole: they follow the local field direction, so they circulate along the field lines.",
        false, 1200u, 220.0f, 1u, 0u, 3u, 0u, 0u, 0u, 5u, true,
        { 0.0f, 1.2f, 0.0f }, 2.4f, { 0.0f, 0.0f, 0.0f },
        { 0.0f, 1.0f, 0.0f }, 0.1f, 0.0f, 0.4f,
        6.0f, 10.0f, 0.07f, 0.05f, 1.0f,
        0.0f, 0.1f, 0.0f, 0.0f, 0.0f, 0.0f,
        0.85f, 2.2f, 0.24f, 1.75f,
        { 1.0f, 0.2542f, 0.0723f, 1.0f },
        { 1.0f, 1.0f, 1.0f, 1.0f },
        { 1.0f, 0.6795f, 0.3515f, 1.0f },
        "modeDefault", "2026"
    },
    ParticlePresetMetadata {
        "bubbles", "Bubbles", "Pyro & Water", "Bubble", "Soap bubbles: transparent spheres with a fresnel rim and a small highlight. They rise on buoyancy, sway, grow slightly and drift on the wind.",
        false, 400u, 25.0f, 2u, 0u, 6u, 0u, 0u, 0u, 11u, false,
        { 0.0f, 0.2f, 0.0f }, 0.8f, { 0.0f, 0.0f, 0.0f },
        { 0.0f, 1.0f, 0.0f }, 0.25f, 0.2f, 0.6f,
        5.0f, 8.0f, 0.12f, 0.2f, 1.0f,
        0.0f, 0.15f, 0.8f, 0.0f, 1.4f, 0.6f,
        0.85f, 2.2f, 0.24f, 1.75f,
        { 0.521f, 0.8148f, 1.0f, 1.0f },
        { 1.0f, 1.0f, 1.0f, 1.0f },
        { 0.807f, 0.9387f, 1.0f, 1.0f },
        "modeDefault", "2026"
    },
    ParticlePresetMetadata {
        "foam", "Foam", "Pyro & Water", "Bubble", "Dense, small bubbles that bunch up and bob near the floor, pushed by the wind. Good for surf or soapy water.",
        false, 1500u, 500.0f, 2u, 0u, 6u, 0u, 0u, 0u, 0u, false,
        { 0.0f, 0.1f, 0.0f }, 1.8f, { 0.0f, 0.0f, 0.0f },
        { 0.0f, 1.0f, 0.0f }, 0.6f, 0.2f, 1.0f,
        4.0f, 7.0f, 0.05f, 0.06f, 1.0f,
        1.0f, 1.5f, 1.2f, 0.2f, 0.2f, 0.3f,
        0.85f, 2.2f, 0.24f, 1.75f,
        { 0.9047f, 0.9647f, 1.0f, 1.0f },
        { 1.0f, 1.0f, 1.0f, 1.0f },
        { 0.6724f, 0.855f, 0.9734f, 1.0f },
        "modeDefault", "2026"
    },
    ParticlePresetMetadata {
        "sparks", "Sparks", "Ballistics & Sparks", "Streak", "Hot metal sparks: gravity, drag, ground bounce and wind coupling.",
        false, 3000u, 160.0f, 0u, 0u, 0u, 0u, 0u, 0u, 0u, true,
        { -3.0f, 0.4f, 0.0f }, 0.1f, { 0.0f, 0.0f, 0.0f },
        { 0.0f, 1.0f, 0.0f }, 0.6f, 2.0f, 6.0f,
        0.6f, 1.6f, 0.035f, 0.01f, 1.0f,
        1.0f, 0.5f, 0.25f, 0.35f, 0.0f, 0.0f,
        0.85f, 2.2f, 0.24f, 1.75f,
        { 1.0f, 0.92f, 0.55f, 1.0f },
        { 1.0f, 1.0f, 0.3f, 1.0f },
        { 1.0f, 0.35f, 0.05f, 0.0f },
        "modeDefault", "2026"
    },
    ParticlePresetMetadata {
        "rain", "Rain streaks", "Storms & Weather", "Streak", "High-speed rain sheets falling through wind and bursting into ground splash rings.",
        false, 5500u, 850.0f, 3u, 0u, 0u, 0u, 0u, 0u, 0u, false,
        { 0.0f, 7.6f, 0.0f }, 0.1f, { 5.5f, 0.35f, 5.5f },
        { 0.0f, -1.0f, 0.0f }, 0.04f, 8.5f, 11.5f,
        1.0f, 1.6f, 0.055f, 0.045f, 1.0f,
        0.95f, 0.04f, 0.85f, 0.26f, 0.0f, 0.05f,
        0.85f, 2.2f, 0.24f, 1.75f,
        { 0.76f, 0.9f, 1.0f, 0.88f },
        { 1.0f, 1.0f, 0.3f, 0.0f },
        { 0.64f, 0.82f, 0.98f, 0.25f },
        "modeDefault", "2026"
    },
    ParticlePresetMetadata {
        "hail", "Hail", "Storms & Weather", "Ice Hail", "Translucent faceted ice pellets dropping fast and ricocheting off vehicle bodywork and the ground.",
        false, 2000u, 160.0f, 3u, 0u, 8u, 0u, 0u, 0u, 0u, false,
        { 0.0f, 7.6f, 0.0f }, 0.1f, { 5.5f, 0.35f, 5.5f },
        { 0.0f, -1.0f, 0.0f }, 0.06f, 7.2f, 10.5f,
        2.5f, 4.0f, 0.09f, 0.085f, 1.0f,
        1.15f, 0.03f, 0.55f, 0.58f, 0.0f, 0.04f,
        0.85f, 2.2f, 0.24f, 1.75f,
        { 0.92f, 0.97f, 1.0f, 1.0f },
        { 1.0f, 1.0f, 0.3f, 0.0f },
        { 0.74f, 0.88f, 1.0f, 0.28f },
        "modeDefault", "2026"
    },
    ParticlePresetMetadata {
        "leaves", "Leaves", "Wind-Blown", "Wind-driven", "Leaves released from a canopy; they flutter, tumble and follow the wind field.",
        false, 1200u, 20.0f, 2u, 0u, 2u, 0u, 0u, 0u, 0u, false,
        { 0.0f, 4.6f, 0.0f }, 4.5f, { 0.0f, 0.0f, 0.0f },
        { 0.0f, -1.0f, 0.0f }, 0.5f, 0.2f, 0.9f,
        7.0f, 12.0f, 0.14f, 0.12f, 1.0f,
        0.35f, 1.4f, 2.2f, 0.0f, 0.0f, 1.2f,
        0.85f, 2.2f, 0.24f, 1.75f,
        { 0.42f, 0.72f, 0.22f, 1.0f },
        { 1.0f, 1.0f, 0.3f, 0.0f },
        { 0.8f, 0.52f, 0.16f, 1.0f },
        "modeDefault", "2026"
    },
    ParticlePresetMetadata {
        "paper", "Paper & debris", "Wind-Blown", "Wind-driven", "Light paper sheets: high wind coupling and strong sway.",
        false, 600u, 8.0f, 8u, 0u, 2u, 0u, 0u, 0u, 14u, false,
        { -2.0f, 3.0f, 2.0f }, 2.5f, { 0.0f, 0.0f, 0.0f },
        { 0.0f, -1.0f, 0.0f }, 0.4f, 0.1f, 0.5f,
        6.0f, 10.0f, 0.12f, 0.1f, 1.0f,
        0.25f, 0.6f, 3.0f, 0.0f, 0.0f, 2.0f,
        0.85f, 2.2f, 0.24f, 1.75f,
        { 0.95f, 0.95f, 0.9f, 1.0f },
        { 1.0f, 1.0f, 0.3f, 0.0f },
        { 0.8f, 0.82f, 0.85f, 1.0f },
        "modeDefault", "2026"
    },
    ParticlePresetMetadata {
        "fire", "Fire", "Pyro & Water", "Pyro / Glow", "Buoyant additive flame with wind-bent tongues.",
        false, 2500u, 280.0f, 1u, 5u, 3u, 0u, 0u, 0u, 11u, true,
        { -4.2f, 0.05f, -4.0f }, 0.12f, { 0.0f, 0.0f, 0.0f },
        { 0.0f, 1.0f, 0.0f }, 0.35f, 0.6f, 1.7f,
        0.7f, 1.5f, 0.35f, 0.12f, 1.0f,
        0.0f, 0.8f, 1.2f, 0.0f, 2.2f, 0.0f,
        0.85f, 2.2f, 0.24f, 1.75f,
        { 1.0f, 0.85f, 0.35f, 1.0f },
        { 1.0f, 1.0f, 0.3f, 0.0f },
        { 0.95f, 0.2f, 0.05f, 0.0f },
        "modeDefault", "2026"
    },
    ParticlePresetMetadata {
        "smoke", "Smoke", "Pyro & Water", "Pyro / Glow", "Alpha-blended soft smoke that expands and drifts with the wind.",
        false, 1500u, 70.0f, 1u, 5u, 3u, 0u, 0u, 0u, 11u, false,
        { -2.6f, 0.2f, -4.0f }, 0.2f, { 0.0f, 0.0f, 0.0f },
        { 0.0f, 1.0f, 0.0f }, 0.3f, 0.3f, 0.8f,
        3.0f, 6.0f, 0.2f, 1.3f, 1.0f,
        0.0f, 0.5f, 1.5f, 0.0f, 1.0f, 0.0f,
        0.85f, 2.2f, 0.24f, 1.75f,
        { 0.6f, 0.6f, 0.64f, 0.5f },
        { 1.0f, 1.0f, 0.3f, 0.0f },
        { 0.25f, 0.25f, 0.28f, 0.0f },
        "modeDefault", "2026"
    },
    ParticlePresetMetadata {
        "explosion", "Explosion", "Pyro & Water", "Pyro / Glow", "Spherical additive burst. Use the Burst button to fire it.",
        false, 2000u, 0.0f, 1u, 5u, 3u, 0u, 0u, 0u, 11u, true,
        { 0.0f, 1.2f, -4.0f }, 0.1f, { 0.0f, 0.0f, 0.0f },
        { 0.0f, 1.0f, 0.0f }, 3.1416f, 3.0f, 9.0f,
        0.5f, 1.2f, 0.15f, 0.6f, 1.0f,
        0.4f, 2.6f, 0.6f, 0.0f, 0.3f, 0.0f,
        0.85f, 2.2f, 0.24f, 1.75f,
        { 1.0f, 0.95f, 0.7f, 1.0f },
        { 1.0f, 1.0f, 0.3f, 0.0f },
        { 1.0f, 0.35f, 0.08f, 0.0f },
        "modeDefault", "2026"
    },
    ParticlePresetMetadata {
        "tornado", "Tornado debris", "Storms & Weather", "Wind-driven", "Debris lifted by a tornado. Adding it enables the Tornado wind vortex at this spot; the swirl is the grid itself.",
        false, 1500u, 90.0f, 8u, 0u, 2u, 0u, 0u, 0u, 14u, false,
        { 0.0f, 0.05f, -2.0f }, 1.6f, { 0.0f, 0.0f, 0.0f },
        { 0.0f, 1.0f, 0.0f }, 0.6f, 1.0f, 3.0f,
        4.0f, 7.0f, 0.14f, 0.1f, 1.0f,
        0.3f, 0.4f, 3.5f, 0.0f, 0.0f, 1.2f,
        0.85f, 2.2f, 0.24f, 1.75f,
        { 0.36f, 0.31f, 0.26f, 1.0f },
        { 1.0f, 1.0f, 0.3f, 0.0f },
        { 0.52f, 0.46f, 0.38f, 0.0f },
        "modeDefault", "2026"
    },
    ParticlePresetMetadata {
        "sandstorm", "Sandstorm", "Storms & Weather", "Wind-driven", "Dust driven across the scene. Adding it sets Prevailing wind to 8 m/s at bearing 70°.",
        false, 6000u, 900.0f, 3u, 0u, 3u, 0u, 0u, 0u, 9u, false,
        { -5.6f, 1.1f, 0.0f }, 0.1f, { 0.3f, 0.9f, 5.2f },
        { 0.94f, 0.0f, 0.342f }, 0.25f, 5.0f, 8.0f,
        4.0f, 7.0f, 0.07f, 0.05f, 1.0f,
        0.2f, 1.0f, 4.0f, 0.0f, 0.0f, 0.6f,
        0.85f, 2.2f, 0.24f, 1.75f,
        { 0.82f, 0.66f, 0.42f, 0.55f },
        { 1.0f, 1.0f, 0.3f, 0.0f },
        { 0.7f, 0.55f, 0.34f, 0.0f },
        "modeDefault", "2026"
    },
    ParticlePresetMetadata {
        "snow", "Snow", "Storms & Weather", "Snowflake", "Six-point crystalline snowflakes drifting and fluttering through wind currents, settling softly across the stage and vehicle.",
        false, 3500u, 280.0f, 3u, 0u, 7u, 0u, 0u, 0u, 0u, false,
        { 0.0f, 6.8f, 0.0f }, 0.1f, { 5.5f, 0.35f, 5.5f },
        { 0.0f, -1.0f, 0.0f }, 0.22f, 0.45f, 1.0f,
        8.0f, 12.0f, 0.095f, 0.07f, 1.0f,
        0.14f, 0.75f, 2.8f, 0.08f, 0.0f, 0.95f,
        0.85f, 2.2f, 0.24f, 1.75f,
        { 0.96f, 0.99f, 1.0f, 0.94f },
        { 1.0f, 1.0f, 0.3f, 0.0f },
        { 0.82f, 0.92f, 1.0f, 0.0f },
        "modeDefault", "2026"
    },
    ParticlePresetMetadata {
        "embers", "Embers", "Pyro & Water", "Pyro / Glow", "Glowing embers lifted on hot air; additive, short-lived, bent by the wind.",
        false, 900u, 45.0f, 1u, 5u, 3u, 0u, 0u, 0u, 11u, true,
        { -4.0f, 0.3f, -4.0f }, 0.5f, { 0.0f, 0.0f, 0.0f },
        { 0.0f, 1.0f, 0.0f }, 0.5f, 0.8f, 2.2f,
        2.0f, 4.2f, 0.035f, 0.012f, 1.0f,
        0.0f, 0.7f, 1.5f, 0.0f, 1.8f, 0.0f,
        0.85f, 2.2f, 0.24f, 1.75f,
        { 1.0f, 0.72f, 0.3f, 1.0f },
        { 1.0f, 1.0f, 0.3f, 0.0f },
        { 0.9f, 0.25f, 0.05f, 0.0f },
        "modeDefault", "2026"
    },
    ParticlePresetMetadata {
        "petals", "Cherry petals", "Wind-Blown", "Wind-driven", "Petals drifting off a canopy; they tumble and follow the wind closely.",
        false, 700u, 14.0f, 2u, 0u, 2u, 0u, 0u, 0u, 0u, false,
        { 2.5f, 4.4f, 2.5f }, 2.2f, { 0.0f, 0.0f, 0.0f },
        { 0.0f, -1.0f, 0.0f }, 0.5f, 0.2f, 0.7f,
        7.0f, 11.0f, 0.1f, 0.09f, 1.0f,
        0.3f, 1.3f, 2.4f, 0.0f, 0.0f, 1.6f,
        0.85f, 2.2f, 0.24f, 1.75f,
        { 1.0f, 0.74f, 0.82f, 1.0f },
        { 1.0f, 1.0f, 0.3f, 0.0f },
        { 0.96f, 0.58f, 0.72f, 1.0f },
        "modeDefault", "2026"
    },
    ParticlePresetMetadata {
        "sand", "Sand grains", "Storms & Weather", "Sphere", "Sand kicked up from the ground in short hops (saltation) and carried by the wind.",
        false, 4000u, 350.0f, 3u, 0u, 1u, 0u, 0u, 0u, 9u, false,
        { 0.0f, 0.08f, 0.0f }, 0.1f, { 5.0f, 0.0f, 5.0f },
        { 0.0f, 1.0f, 0.0f }, 0.35f, 0.8f, 2.2f,
        2.5f, 4.0f, 0.025f, 0.022f, 1.0f,
        1.0f, 0.1f, 2.5f, 0.2f, 0.0f, 0.0f,
        0.85f, 2.2f, 0.24f, 1.75f,
        { 0.86f, 0.74f, 0.52f, 1.0f },
        { 1.0f, 1.0f, 0.3f, 0.0f },
        { 0.7f, 0.58f, 0.4f, 0.0f },
        "modeDefault", "2026"
    },
    ParticlePresetMetadata {
        "ash", "Ash flakes", "Storms & Weather", "Wind-driven", "Grey ash lifted by a fire's heat, drifting and settling onto the ground.",
        false, 1500u, 30.0f, 1u, 0u, 3u, 0u, 0u, 0u, 11u, false,
        { -4.2f, 1.2f, -4.0f }, 0.6f, { 0.0f, 0.0f, 0.0f },
        { 0.0f, 1.0f, 0.0f }, 0.4f, 0.3f, 1.0f,
        6.0f, 10.0f, 0.05f, 0.08f, 1.0f,
        0.12f, 1.2f, 1.6f, 0.0f, 0.9f, 0.7f,
        0.85f, 2.2f, 0.24f, 1.75f,
        { 0.55f, 0.55f, 0.55f, 0.7f },
        { 1.0f, 1.0f, 0.3f, 0.0f },
        { 0.35f, 0.35f, 0.36f, 0.0f },
        "modeDefault", "2026"
    },
    ParticlePresetMetadata {
        "debris", "Small debris", "Storms & Weather", "Streak", "Twigs, bark and wood chips thrown up and clattering off the ground. Small pieces only, no stones.",
        false, 800u, 30.0f, 1u, 0u, 0u, 0u, 0u, 0u, 9u, false,
        { -2.0f, 0.25f, 4.0f }, 0.6f, { 0.0f, 0.0f, 0.0f },
        { 0.0f, 1.0f, 0.0f }, 0.9f, 2.0f, 5.0f,
        3.0f, 5.0f, 0.035f, 0.03f, 1.0f,
        1.0f, 0.3f, 1.8f, 0.35f, 0.0f, 0.0f,
        0.85f, 2.2f, 0.24f, 1.75f,
        { 0.46f, 0.32f, 0.18f, 1.0f },
        { 1.0f, 1.0f, 0.3f, 0.0f },
        { 0.3f, 0.22f, 0.14f, 0.0f },
        "modeDefault", "2026"
    },
    ParticlePresetMetadata {
        "flares", "Countermeasure flares", "Atmosphere & Sky", "IR Countermeasure Flares", "Military aircraft IR decoy flare salvo ('Angel of Death'): alternating twin-pod V-bursts of blinding white-hot magnesium flares trailing thick billowing curling smoke plumes and molten sparks.",
        false, 480u, 0.0f, 1u, 5u, 3u, 0u, 0u, 0u, 11u, false,
        { 0.0f, 4.6f, 0.0f }, 0.25f, { 0.0f, 0.0f, 0.0f },
        { -0.85f, -0.35f, 0.0f }, 0.42f, 8.5f, 13.5f,
        3.4f, 5.2f, 0.34f, 0.14f, 1.0f,
        0.68f, 1.55f, 0.85f, 0.25f, 0.35f, 0.0f,
        0.85f, 2.2f, 0.24f, 1.75f,
        { 1.0f, 0.99f, 0.92f, 1.0f },
        { 1.0f, 0.66f, 0.2f, 1.0f },
        { 0.84f, 0.88f, 0.94f, 0.0f },
        "modeDefault", "2026"
    },
    ParticlePresetMetadata {
        "pollen", "Pollen & dust motes", "Atmosphere & Sky", "Wind-driven", "Slow glowing motes that drift on the wind through the air. Light shafts are not modelled.",
        false, 2000u, 90.0f, 3u, 0u, 3u, 0u, 0u, 0u, 0u, false,
        { 0.0f, 2.5f, 0.0f }, 0.1f, { 5.0f, 1.5f, 5.0f },
        { 0.0f, 1.0f, 0.0f }, 0.5f, 0.05f, 0.2f,
        8.0f, 12.0f, 0.03f, 0.03f, 1.0f,
        0.02f, 0.6f, 1.6f, 0.0f, 0.0f, 0.6f,
        0.85f, 2.2f, 0.24f, 1.75f,
        { 1.0f, 0.95f, 0.6f, 0.6f },
        { 1.0f, 1.0f, 0.3f, 0.0f },
        { 1.0f, 0.9f, 0.5f, 0.0f },
        "modeDefault", "2026"
    },
    ParticlePresetMetadata {
        "watermist", "Water mist", "Pyro & Water", "Volumetric Water Mist", "Volumetric sunlit water mist billowing from a high-pressure spray bar: multi-lobe turbulent vapor plumes, subtle prismatic rainbow refraction, and glittering micro-droplet aerosol shimmer.",
        false, 1800u, 220.0f, 2u, 5u, 19u, 0u, 0u, 0u, 8u, false,
        { 2.6f, 2.35f, 0.0f }, 0.95f, { 0.0f, 0.0f, 0.0f },
        { -0.95f, -0.18f, 0.0f }, 0.82f, 0.9f, 2.6f,
        1.9f, 3.8f, 0.3f, 1.55f, 1.0f,
        0.08f, 1.9f, 1.35f, 0.0f, 0.22f, 0.9f,
        0.85f, 2.2f, 0.24f, 1.75f,
        { 0.9f, 0.97f, 1.0f, 0.32f },
        { 0.76f, 0.92f, 1.0f, 0.22f },
        { 0.88f, 0.96f, 1.0f, 0.0f },
        "modeDefault", "2026"
    },
    ParticlePresetMetadata {
        "splash", "Splash rings", "Quarantine", "Pyro / Glow", "Expanding ripple rings on the floor. Placed at random ground points, not at each impact.",
        true, 600u, 40.0f, 3u, 5u, 3u, 0u, 0u, 0u, 8u, false,
        { 0.0f, 0.02f, 0.0f }, 0.1f, { 5.0f, 0.0f, 5.0f },
        { 0.0f, 1.0f, 0.0f }, 0.0f, 0.0f, 0.0f,
        0.45f, 0.7f, 0.08f, 0.9f, 1.0f,
        0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
        0.85f, 2.2f, 0.24f, 1.75f,
        { 0.8f, 0.9f, 1.0f, 0.5f },
        { 1.0f, 1.0f, 0.3f, 0.0f },
        { 0.8f, 0.9f, 1.0f, 0.0f },
        "modeDefault", "2026"
    },
    ParticlePresetMetadata {
        "glass", "Glass shards", "Ballistics & Sparks", "3D Glass Shard", "Real 3D faceted glass shard meshes blown from a broken window, tumbling in 3D, bouncing, and catching specular glints.",
        false, 420u, 26.0f, 1u, 0u, 18u, 0u, 0u, 0u, 0u, false,
        { 3.0f, 2.5f, -3.0f }, 0.42f, { 0.0f, 0.0f, 0.0f },
        { 0.0f, 0.0f, 1.0f }, 1.0f, 2.2f, 5.4f,
        2.2f, 3.8f, 0.24f, 0.16f, 1.0f,
        1.0f, 0.14f, 0.4f, 0.42f, 0.0f, 0.0f,
        0.85f, 2.2f, 0.24f, 1.75f,
        { 0.8f, 0.94f, 1.0f, 0.84f },
        { 1.0f, 1.0f, 0.3f, 0.0f },
        { 0.56f, 0.84f, 0.98f, 0.0f },
        "modeDefault", "2026"
    },
    ParticlePresetMetadata {
        "lapilli", "Volcanic ash & lapilli", "Pyro & Water", "Sphere", "Hot, heavy ejecta thrown high by a vent: faster and bigger than ash flakes.",
        false, 500u, 25.0f, 1u, 0u, 1u, 0u, 0u, 0u, 0u, false,
        { -4.2f, 0.3f, -4.0f }, 0.4f, { 0.0f, 0.0f, 0.0f },
        { 0.0f, 1.0f, 0.0f }, 0.5f, 6.0f, 12.0f,
        3.0f, 5.0f, 0.08f, 0.06f, 1.0f,
        1.0f, 0.1f, 0.8f, 0.3f, 0.0f, 0.0f,
        0.85f, 2.2f, 0.24f, 1.75f,
        { 1.0f, 0.45f, 0.15f, 1.0f },
        { 1.0f, 1.0f, 0.3f, 0.0f },
        { 0.25f, 0.22f, 0.2f, 0.0f },
        "modeDefault", "2026"
    },
    ParticlePresetMetadata {
        "seeds", "Dandelion seeds", "Wind-Blown", "Wind-driven", "Very light seeds that float and flutter; the wind carries them.",
        false, 400u, 15.0f, 1u, 0u, 3u, 0u, 0u, 0u, 0u, false,
        { -3.0f, 2.2f, 3.0f }, 0.6f, { 0.0f, 0.0f, 0.0f },
        { 0.0f, 1.0f, 0.0f }, 0.6f, 0.1f, 0.4f,
        8.0f, 12.0f, 0.04f, 0.035f, 1.0f,
        0.08f, 1.4f, 3.0f, 0.0f, 0.0f, 1.4f,
        0.85f, 2.2f, 0.24f, 1.75f,
        { 1.0f, 1.0f, 0.97f, 0.8f },
        { 1.0f, 1.0f, 0.3f, 0.0f },
        { 1.0f, 1.0f, 1.0f, 0.0f },
        "modeDefault", "2026"
    },
    ParticlePresetMetadata {
        "feathers", "Feathers", "Wind-Blown", "Wind-driven", "Light feathers that drift, spin and sway in the wind.",
        false, 300u, 6.0f, 1u, 0u, 2u, 0u, 0u, 0u, 0u, false,
        { -3.0f, 3.2f, -3.0f }, 0.6f, { 0.0f, 0.0f, 0.0f },
        { 0.0f, 1.0f, 0.0f }, 0.5f, 0.1f, 0.3f,
        8.0f, 12.0f, 0.13f, 0.12f, 1.0f,
        0.2f, 1.2f, 2.6f, 0.0f, 0.0f, 1.8f,
        0.85f, 2.2f, 0.24f, 1.75f,
        { 0.97f, 0.97f, 0.94f, 1.0f },
        { 1.0f, 1.0f, 0.3f, 0.0f },
        { 0.75f, 0.75f, 0.78f, 1.0f },
        "modeDefault", "2026"
    },
    ParticlePresetMetadata {
        "confetti", "Confetti & streamers", "Wind-Blown", "Wind-driven", "Paper pieces that tumble and flutter; strong wind coupling.",
        false, 1200u, 25.0f, 8u, 0u, 2u, 0u, 0u, 0u, 14u, false,
        { 0.0f, 0.3f, -1.0f }, 0.5f, { 0.0f, 0.0f, 0.0f },
        { 0.0f, 1.0f, 0.0f }, 0.8f, 1.0f, 3.0f,
        5.0f, 8.0f, 0.09f, 0.08f, 1.0f,
        0.25f, 0.8f, 3.5f, 0.0f, 0.0f, 2.2f,
        0.85f, 2.2f, 0.24f, 1.75f,
        { 1.0f, 0.82f, 0.3f, 1.0f },
        { 1.0f, 1.0f, 0.3f, 0.0f },
        { 1.0f, 0.4f, 0.6f, 1.0f },
        "modeDefault", "2026"
    },
    ParticlePresetMetadata {
        "steam", "Steam vent", "Pyro & Water", "Pyro / Glow", "Hot steam rising from a vent and expanding as it cools.",
        false, 1600u, 60.0f, 2u, 5u, 3u, 0u, 0u, 0u, 11u, false,
        { 4.0f, 0.05f, 4.0f }, 0.35f, { 0.0f, 0.0f, 0.0f },
        { 0.0f, 1.0f, 0.0f }, 0.12f, 0.8f, 1.6f,
        3.0f, 5.0f, 0.15f, 1.6f, 1.0f,
        0.0f, 1.5f, 1.2f, 0.0f, 1.6f, 0.0f,
        0.85f, 2.2f, 0.24f, 1.75f,
        { 0.92f, 0.94f, 0.96f, 0.45f },
        { 1.0f, 1.0f, 0.3f, 0.0f },
        { 0.8f, 0.82f, 0.85f, 0.0f },
        "modeDefault", "2026"
    },
    ParticlePresetMetadata {
        "fireworks", "Fireworks", "Atmosphere & Sky", "Multi-Stage Fireworks", "Choreographed multi-stage Hanabi fireworks: ascending golden mortar rockets bursting into Chrysanthemum, Golden Willow, Brocade Palm, and Crossette shells with drooping ember trails and instant impact pop-vanish.",
        false, 1200u, 0.0f, 10u, 5u, 0u, 0u, 0u, 0u, 11u, true,
        { 0.0f, 0.05f, 0.0f }, 0.15f, { 0.0f, 0.0f, 0.0f },
        { 0.0f, 1.0f, 0.0f }, 3.1416f, 4.0f, 9.0f,
        1.8f, 2.9f, 0.15f, 0.035f, 1.0f,
        0.42f, 1.45f, 0.45f, 0.0f, 0.0f, 0.0f,
        0.85f, 2.2f, 0.24f, 1.75f,
        { 1.0f, 0.94f, 0.65f, 1.0f },
        { 1.0f, 0.48f, 0.22f, 1.0f },
        { 1.0f, 0.22f, 0.14f, 0.0f },
        "modeDefault", "2026"
    },
    ParticlePresetMetadata {
        "fireworks-spectacle", "New Year's Text & Shape Fireworks", "Atmosphere & Sky", "New Year's Pyro Spectacle", "Grand New Year's spectacle shells bursting into luminous 3D sky text (customizable: '2026', 'HAPPY NEW YEAR', etc.) plus Heart, Saturn, Star, Smiley, Crown, and Butterfly pyro formations with impact pop-vanish.",
        false, 1400u, 0.0f, 10u, 5u, 0u, 0u, 9u, 0u, 11u, true,
        { 0.0f, 0.05f, 0.0f }, 0.15f, { 0.0f, 0.0f, 0.0f },
        { 0.0f, 1.0f, 0.0f }, 3.1416f, 4.0f, 9.0f,
        2.0f, 3.2f, 0.16f, 0.04f, 1.0f,
        0.32f, 1.65f, 0.35f, 0.0f, 0.0f, 0.0f,
        0.85f, 2.2f, 0.24f, 1.75f,
        { 1.0f, 0.92f, 0.42f, 1.0f },
        { 0.32f, 0.92f, 1.0f, 1.0f },
        { 1.0f, 0.24f, 0.64f, 0.0f },
        "modeDefault", "2026"
    },
    ParticlePresetMetadata {
        "curl-fur-field", "Curl Wave Field", "Silk & Curl", "Curl Wave", "Dense volumetric bed of glowing micro-filaments sculpted by multi-octave 3D curl noise into indigo-to-cyan velvet dunes and crests.",
        false, 10000u, 1800.0f, 3u, 1u, 0u, 0u, 0u, 1u, 1u, true,
        { -2.1f, 1.35f, 1.2f }, 0.1f, { 1.9f, 0.24f, 1.9f },
        { 0.0f, 1.0f, 0.0f }, 0.25f, 0.3f, 1.1f,
        2.8f, 4.6f, 0.042f, 0.036f, 1.0f,
        0.0f, 2.6f, 0.55f, 0.0f, 0.0f, 0.0f,
        0.82f, 2.45f, 0.24f, 1.75f,
        { 0.0437f, 0.0666f, 1.0f, 1.0f },
        { 0.0194f, 0.314f, 1.0f, 1.0f },
        { 0.2623f, 0.9131f, 1.0f, 1.0f },
        "modeDefault", "2026"
    },
    ParticlePresetMetadata {
        "curl-sphere-plasma", "Spherical Curl Plasma", "Silk & Curl", "Curl Sphere", "Luminous 3D sphere manifold of deep electric cobalt dust and fast converging ice-cyan filament streams tangentially curving over the shell.",
        false, 9000u, 1500.0f, 1u, 1u, 0u, 0u, 0u, 2u, 1u, true,
        { 2.1f, 2.2f, 0.0f }, 1.65f, { 0.0f, 0.0f, 0.0f },
        { 0.0f, 1.0f, 0.0f }, 3.1416f, 0.4f, 1.6f,
        3.0f, 5.2f, 0.038f, 0.03f, 1.0f,
        0.0f, 2.2f, 0.45f, 0.0f, 0.0f, 0.0f,
        1.12f, 2.85f, 0.3f, 1.65f,
        { 0.0044f, 0.0908f, 1.0f, 1.0f },
        { 0.0212f, 0.4678f, 1.0f, 1.0f },
        { 0.6445f, 0.9387f, 1.0f, 1.0f },
        "modeDefault", "2026"
    },
    ParticlePresetMetadata {
        "bokeh-stream-ribbons", "Bokeh Stream Ribbons", "Silk & Curl", "Ribbon Stream", "Luminous white & mist-teal bokeh particles flowing along nested swirling eddies with smooth tapered ribbons trailing & fading behind every particle.",
        false, 3200u, 580.0f, 3u, 1u, 9u, 0u, 0u, 3u, 1u, true,
        { 0.0f, 2.0f, 0.0f }, 0.1f, { 3.8f, 2.1f, 3.2f },
        { 0.0f, 1.0f, 0.0f }, 0.2f, 0.3f, 1.0f,
        4.5f, 7.5f, 0.046f, 0.032f, 1.0f,
        0.0f, 1.8f, 0.4f, 0.0f, 0.0f, 0.0f,
        0.62f, 1.95f, 0.16f, 1.75f,
        { 0.0437f, 0.1022f, 0.1046f, 1.0f },
        { 0.2705f, 0.5711f, 0.5711f, 1.0f },
        { 0.8632f, 1.0f, 1.0f, 1.0f },
        "modeDefault", "2026"
    },
    ParticlePresetMetadata {
        "bokeh-object-pulse", "Object Surface Pour Ribbons", "Silk & Curl", "Surface Pour", "Like pouring water over a sphere: glowing bokeh particles & curved fading ribbons cascade directly over the 3D object surface from top pole to bottom.",
        false, 3400u, 620.0f, 1u, 1u, 9u, 0u, 0u, 3u, 1u, true,
        { 0.0f, 2.25f, 0.0f }, 1.65f, { 1.65f, 1.65f, 1.65f },
        { 0.0f, -1.0f, 0.0f }, 0.35f, 0.4f, 1.1f,
        3.4f, 5.2f, 0.048f, 0.032f, 1.0f,
        0.0f, 1.4f, 0.25f, 0.0f, 0.0f, 0.0f,
        0.85f, 1.55f, 0.18f, 1.75f,
        { 0.0262f, 0.0762f, 0.0802f, 1.0f },
        { 0.2086f, 0.5972f, 0.5841f, 1.0f },
        { 0.9047f, 1.0f, 1.0f, 1.0f },
        "modeDefault", "2026"
    },
    ParticlePresetMetadata {
        "tesla-sphere-lightning", "Tesla Sphere Lightning", "Silk & Curl", "Tesla Lightning", "Instantaneous high-voltage dielectric spark arc & forked stepped leaders discharging between two placeable 3D spheres (looping optional).",
        false, 600u, 0.0f, 1u, 0u, 0u, 0u, 0u, 0u, 12u, true,
        { -2.15f, 2.65f, 0.45f }, 0.46f, { 0.0f, 0.0f, 0.0f },
        { 1.0f, 0.0f, 0.0f }, 1.4f, 1.8f, 5.2f,
        0.16f, 0.42f, 0.026f, 0.004f, 1.0f,
        0.65f, 2.2f, 0.25f, 0.0f, 0.0f, 0.0f,
        0.85f, 2.2f, 0.24f, 1.75f,
        { 0.0437f, 0.2016f, 1.0f, 1.0f },
        { 0.2623f, 0.7758f, 1.0f, 1.0f },
        { 1.0f, 1.0f, 1.0f, 1.0f },
        "modeDefault", "2026"
    },
    ParticlePresetMetadata {
        "static-sphere-shock", "Static Shock Arcs", "Silk & Curl", "Static Shock", "Delicate high-voltage corona filaments & crackling electrostatic arcs jumping between two placeable 3D spheres (optional looping).",
        false, 600u, 0.0f, 1u, 0u, 9u, 0u, 0u, 0u, 0u, true,
        { -1.75f, 1.75f, -0.75f }, 0.4f, { 0.0f, 0.0f, 0.0f },
        { 1.0f, 0.0f, 0.0f }, 1.8f, 1.2f, 3.4f,
        0.14f, 0.36f, 0.022f, 0.004f, 1.0f,
        0.35f, 2.6f, 0.2f, 0.0f, 0.0f, 0.0f,
        0.85f, 2.2f, 0.24f, 1.75f,
        { 0.3325f, 0.0742f, 1.0f, 1.0f },
        { 0.1119f, 0.8879f, 1.0f, 1.0f },
        { 0.9131f, 0.9911f, 1.0f, 1.0f },
        "modeDefault", "2026"
    },
    ParticlePresetMetadata {
        "shockwave-ring", "Blast & Sonic Shockwave", "Ballistics & Sparks", "Mach Shockwave", "Supersonic expanding pressure rings and secondary Mach shockfronts bursting radially from an impact origin.",
        false, 96u, 2.2f, 0u, 5u, 10u, 0u, 0u, 0u, 0u, true,
        { 0.0f, 0.32f, 0.0f }, 0.1f, { 0.0f, 0.0f, 0.0f },
        { 0.0f, 1.0f, 0.0f }, 0.04f, 0.02f, 0.14f,
        0.72f, 1.05f, 0.38f, 6.4f, 1.0f,
        0.0f, 0.8f, 0.05f, 0.0f, 0.12f, 0.0f,
        0.85f, 2.2f, 0.24f, 1.75f,
        { 1.0f, 1.0f, 1.0f, 1.0f },
        { 0.2623f, 0.7454f, 1.0f, 1.0f },
        { 0.0252f, 0.2016f, 1.0f, 1.0f },
        "modeDefault", "2026"
    },
    ParticlePresetMetadata {
        "impact-dust-ring", "Ground Impact Dust Ring", "Quarantine", "Dust Shockwave", "Ground-hugging radial dust torus & shock compression wave rolling outward across the floor.",
        true, 160u, 1.5f, 8u, 5u, 10u, 0u, 0u, 0u, 14u, false,
        { 0.0f, 0.16f, 0.0f }, 0.28f, { 0.0f, 0.0f, 0.0f },
        { 0.0f, 1.0f, 0.0f }, 0.06f, 0.05f, 0.22f,
        0.95f, 1.45f, 0.45f, 6.8f, 1.0f,
        0.0f, 1.2f, 0.35f, 0.0f, 0.24f, 0.0f,
        0.85f, 2.2f, 0.24f, 1.75f,
        { 0.7379f, 0.6308f, 0.4793f, 1.0f },
        { 0.4621f, 0.3325f, 0.2086f, 1.0f },
        { 0.2016f, 0.1384f, 0.0844f, 1.0f },
        "modeDefault", "2026"
    },
    ParticlePresetMetadata {
        "emp-pulse-dome", "EMP Energy Pulse Dome", "Shields & Tech", "EMP Pulse Dome", "Expanding electromagnetic hemisphere pulse shell with interference ripples and high-energy Fresnel edge.",
        false, 96u, 1.1f, 0u, 5u, 11u, 0u, 0u, 0u, 5u, true,
        { 0.0f, 0.14f, 0.0f }, 0.1f, { 0.0f, 0.0f, 0.0f },
        { 0.0f, 1.0f, 0.0f }, 0.02f, 0.0f, 0.05f,
        0.9f, 1.25f, 0.36f, 6.8f, 1.0f,
        0.0f, 0.4f, 0.0f, 0.0f, 0.0f, 0.0f,
        0.85f, 2.2f, 0.24f, 1.75f,
        { 1.0f, 1.0f, 1.0f, 1.0f },
        { 0.0666f, 0.807f, 1.0f, 1.0f },
        { 0.1329f, 0.0467f, 1.0f, 1.0f },
        "modeDefault", "2026"
    },
    ParticlePresetMetadata {
        "radar-sweep", "Tactical Radar Sweep & Blips", "Shields & Tech", "Radar Sweep", "3D tactical radar grid with concentric range rings, compass ticks, phosphor sector sweep arm, and flaring contact ping blips.",
        false, 64u, 0.25f, 0u, 5u, 15u, 0u, 0u, 0u, 0u, true,
        { 0.0f, 0.16f, 0.0f }, 0.1f, { 0.0f, 0.0f, 0.0f },
        { 0.0f, 1.0f, 0.0f }, 0.0f, 0.0f, 0.0f,
        7.5f, 8.5f, 6.2f, 6.2f, 1.0f,
        0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
        0.85f, 2.2f, 2.2f, 1.75f,
        { 0.0762f, 1.0f, 0.4793f, 1.0f },
        { 0.0086f, 0.6584f, 0.2874f, 1.0f },
        { 0.004f, 0.3419f, 0.147f, 1.0f },
        "modeDefault", "2026"
    },
    ParticlePresetMetadata {
        "sonar-ping", "Acoustic Sonar Pulse Ping", "Shields & Tech", "Sonar Ping", "Expanding concentric cyan-emerald sonar pulse waves with bearing crosshairs and rim notches.",
        false, 64u, 1.2f, 8u, 5u, 15u, 0u, 0u, 0u, 14u, true,
        { 0.0f, 0.15f, 0.0f }, 0.1f, { 0.0f, 0.0f, 0.0f },
        { 0.0f, 1.0f, 0.0f }, 0.02f, 0.0f, 0.04f,
        1.1f, 1.5f, 0.45f, 7.4f, 1.0f,
        0.0f, 0.2f, 0.0f, 0.0f, 0.0f, 0.0f,
        0.85f, 2.2f, 0.24f, 1.75f,
        { 0.6445f, 1.0f, 0.9216f, 1.0f },
        { 0.0319f, 0.9131f, 0.6584f, 1.0f },
        { 0.0044f, 0.2051f, 0.2542f, 1.0f },
        "modeDefault", "2026"
    },
    ParticlePresetMetadata {
        "plasma-laser-beam", "Plasma & Laser Beam", "Ballistics & Sparks", "Laser / Plasma Beam", "Continuous coherent laser & plasma beam between a placeable muzzle and target, with muzzle corona flare and ricocheting impact sparks.",
        false, 1200u, 260.0f, 5u, 3u, 0u, 0u, 0u, 0u, 12u, true,
        { -2.45f, 2.25f, 0.25f }, 0.22f, { 0.0f, 0.0f, 0.0f },
        { 1.0f, -0.18f, 0.0f }, 0.95f, 3.2f, 9.5f,
        0.18f, 0.55f, 0.036f, 0.008f, 1.0f,
        0.65f, 1.4f, 0.18f, 0.35f, 0.0f, 0.0f,
        0.85f, 2.2f, 0.24f, 1.75f,
        { 1.0f, 1.0f, 1.0f, 1.0f },
        { 0.0452f, 0.8714f, 1.0f, 1.0f },
        { 0.0273f, 0.1559f, 1.0f, 1.0f },
        "modeDefault", "2026"
    },
    ParticlePresetMetadata {
        "plasma-tracers", "Bullet & Plasma Tracers", "Ballistics & Sparks", "Streak", "High-velocity supersonic tracer rounds streaking downrange with crisp floor ricochets and muzzle cadence.",
        false, 1200u, 36.0f, 0u, 0u, 0u, 0u, 0u, 0u, 0u, true,
        { -3.4f, 1.55f, -0.4f }, 0.04f, { 0.0f, 0.0f, 0.0f },
        { 1.0f, -0.09f, 0.06f }, 0.024f, 19.0f, 25.0f,
        0.55f, 0.95f, 0.068f, 0.045f, 1.0f,
        0.22f, 0.04f, 0.05f, 0.58f, 0.0f, 0.0f,
        0.85f, 2.2f, 0.24f, 1.75f,
        { 1.0f, 0.9387f, 0.6724f, 1.0f },
        { 1.0f, 0.402f, 0.0242f, 1.0f },
        { 1.0f, 0.0685f, 0.0044f, 1.0f },
        "modeDefault", "2026"
    },
    ParticlePresetMetadata {
        "projectile-trails", "Glowing Projectile Trails", "Ballistics & Sparks", "Projectile Trail", "Arcing ballistic plasma projectiles leaving smooth glowing tapered 3D ribbon trails through wind and gravity.",
        false, 256u, 14.0f, 1u, 1u, 9u, 0u, 0u, 0u, 0u, true,
        { -2.8f, 0.55f, 0.6f }, 0.12f, { 0.0f, 0.0f, 0.0f },
        { 0.82f, 0.78f, -0.14f }, 0.19f, 6.4f, 9.2f,
        2.0f, 3.1f, 0.075f, 0.03f, 1.0f,
        0.85f, 0.08f, 0.35f, 0.48f, 0.0f, 0.0f,
        0.85f, 2.2f, 0.24f, 1.75f,
        { 0.0194f, 0.107f, 1.0f, 1.0f },
        { 0.0844f, 0.8714f, 1.0f, 1.0f },
        { 1.0f, 1.0f, 1.0f, 1.0f },
        "modeDefault", "2026"
    },
    ParticlePresetMetadata {
        "welding-grinder-sparks", "Welding & Grinder Arc Spray", "Ballistics & Sparks", "Streak", "Dense directional spray of incandescent angle-grinder sparks showering down and scattering violently off the floor.",
        false, 3200u, 420.0f, 0u, 0u, 0u, 0u, 0u, 0u, 0u, true,
        { -1.85f, 1.5f, 0.15f }, 0.04f, { 0.0f, 0.0f, 0.0f },
        { 0.88f, -0.46f, 0.08f }, 0.18f, 7.0f, 13.8f,
        0.65f, 1.45f, 0.042f, 0.012f, 1.0f,
        1.28f, 0.36f, 0.15f, 0.66f, 0.0f, 0.0f,
        0.85f, 2.2f, 0.24f, 1.75f,
        { 1.0f, 0.9647f, 0.807f, 1.0f },
        { 1.0f, 0.5029f, 0.0369f, 1.0f },
        { 1.0f, 0.0802f, 0.0024f, 1.0f },
        "modeDefault", "2026"
    },
    ParticlePresetMetadata {
        "hologram-data-glitch", "Hologram & Data Glitch", "Shields & Tech", "Holo Glitch", "Rising procedural digital matrix glyphs and flickering horizontal holographic scanline glitch slices.",
        false, 900u, 110.0f, 3u, 5u, 12u, 0u, 0u, 0u, 0u, true,
        { 0.0f, 0.28f, 0.0f }, 0.1f, { 1.35f, 0.12f, 1.35f },
        { 0.0f, 1.0f, 0.0f }, 0.03f, 0.85f, 1.95f,
        1.8f, 3.3f, 0.2f, 0.16f, 1.0f,
        0.0f, 0.05f, 0.18f, 0.0f, 0.04f, 0.0f,
        0.85f, 2.2f, 0.24f, 1.75f,
        { 0.0091f, 1.0f, 0.3813f, 1.0f },
        { 0.007f, 0.7157f, 1.0f, 1.0f },
        { 0.2623f, 1.0f, 0.8388f, 1.0f },
        "modeDefault", "2026"
    },
    ParticlePresetMetadata {
        "boids-flock", "Boids Flock (Birds / Bats / Fish)", "Wind-Blown", "Boids Flock", "True cohesion, alignment & separation flocking with animated wingbeats (switch between Birds, Butterflies, or Fish schools).",
        false, 220u, 48.0f, 1u, 0u, 13u, 0u, 0u, 0u, 2u, false,
        { 0.0f, 3.0f, 0.0f }, 1.65f, { 0.0f, 0.0f, 0.0f },
        { 1.0f, 0.1f, 0.0f }, 3.14f, 2.2f, 3.8f,
        12.0f, 18.0f, 0.25f, 0.25f, 1.0f,
        0.0f, 0.05f, 0.65f, 0.0f, 0.0f, 1.5f,
        0.85f, 2.2f, 0.24f, 1.75f,
        { 0.0194f, 0.0307f, 0.0545f, 1.0f },
        { 0.0666f, 0.1248f, 0.2346f, 1.0f },
        { 0.2961f, 0.4179f, 0.5841f, 1.0f },
        "modeDefault", "2026"
    },
    ParticlePresetMetadata {
        "boids-butterflies", "Butterfly & Wisp Swarm", "Wind-Blown", "Butterfly Flock", "Luminous dual-wing butterflies & glowing wisps flocking cleanly with foreshortened wing-flap animation.",
        false, 180u, 36.0f, 8u, 0u, 13u, 0u, 0u, 0u, 14u, true,
        { 0.0f, 2.0f, 0.0f }, 1.8f, { 0.0f, 0.0f, 0.0f },
        { 0.0f, 1.0f, 0.0f }, 3.14f, 1.2f, 2.3f,
        10.0f, 16.0f, 0.26f, 0.24f, 1.0f,
        0.0f, 0.05f, 1.0f, 0.0f, 0.0f, 2.1f,
        0.85f, 2.2f, 0.24f, 1.75f,
        { 1.0f, 0.3419f, 0.0578f, 1.0f },
        { 1.0f, 0.7454f, 0.2232f, 1.0f },
        { 0.1119f, 0.8714f, 1.0f, 1.0f },
        "modeDefault", "2026"
    },
    ParticlePresetMetadata {
        "boids-fish-school", "Bioluminescent Fish School", "Wind-Blown", "Fish School", "Iridescent cyan & reef-coral schooling fish undulating with side-to-side tail propulsion and scattering around thrown spheres.",
        false, 260u, 64.0f, 8u, 0u, 13u, 0u, 0u, 0u, 14u, false,
        { 0.2f, 2.35f, -0.1f }, 1.75f, { 0.0f, 0.0f, 0.0f },
        { 1.0f, 0.06f, 0.2f }, 3.14f, 2.1f, 3.4f,
        14.0f, 22.0f, 0.22f, 0.21f, 1.0f,
        0.0f, 0.06f, 0.85f, 0.0f, 0.0f, 1.6f,
        0.85f, 2.2f, 0.24f, 1.75f,
        { 0.0103f, 0.2623f, 1.0f, 1.0f },
        { 0.0452f, 0.9131f, 1.0f, 1.0f },
        { 1.0f, 0.2747f, 0.107f, 1.0f },
        "modeDefault", "2026"
    },
    ParticlePresetMetadata {
        "viscous-dripping", "Viscous Dripping & Trails", "Pyro & Water", "Viscous Drip", "Pure-particle surface-tension liquid droplets slowly detaching and stretching viscous drip trails before splashing.",
        false, 320u, 26.0f, 2u, 1u, 14u, 0u, 0u, 0u, 0u, false,
        { 0.0f, 3.85f, 0.0f }, 0.95f, { 0.0f, 0.0f, 0.0f },
        { 0.0f, -1.0f, 0.0f }, 0.08f, 0.04f, 0.24f,
        1.8f, 2.9f, 0.13f, 0.1f, 1.0f,
        0.96f, 0.14f, 0.25f, 0.06f, 0.0f, 0.0f,
        0.85f, 2.2f, 0.24f, 1.75f,
        { 0.0319f, 0.7454f, 0.2874f, 1.0f },
        { 0.3813f, 1.0f, 0.7157f, 1.0f },
        { 0.0086f, 0.2623f, 0.0999f, 1.0f },
        "modeDefault", "2026"
    },
    ParticlePresetMetadata {
        "splatter-burst", "Slime / Paint / Mud Splatter", "Quarantine", "Viscous Drop", "Pure-particle viscous droplet splatter bursts forming glossy wet splatter puddles on floor impact (no volume grids).",
        true, 900u, 0.0f, 1u, 0u, 14u, 0u, 0u, 0u, 0u, false,
        { 0.0f, 1.35f, 0.0f }, 0.16f, { 0.0f, 0.0f, 0.0f },
        { 0.0f, 0.85f, 0.0f }, 1.18f, 2.8f, 6.8f,
        2.6f, 4.2f, 0.17f, 0.15f, 1.0f,
        1.12f, 0.28f, 0.22f, 0.02f, 0.0f, 0.0f,
        0.85f, 2.2f, 0.24f, 1.75f,
        { 1.0f, 0.0252f, 0.2384f, 1.0f },
        { 1.0f, 0.3419f, 0.6308f, 1.0f },
        { 0.3325f, 0.0037f, 0.0685f, 1.0f },
        "modeDefault", "2026"
    },
    ParticlePresetMetadata {
        "derez-cube", "Derez Cube", "Transitions", "Shatter Derez", "A cube lattice derezzes in a corner wave, bursting or falling. Each cube hitting the floor shatters into 8 smaller sub-cubes.",
        false, 1536u, 0.0f, 0u, 5u, 4u, 0u, 0u, 0u, 0u, false,
        { 0.0f, 0.65f, 0.0f }, 0.0f, { 0.6f, 0.6f, 0.6f },
        { 0.0f, 1.0f, 0.0f }, 0.5f, 0.6f, 2.6f,
        3.0f, 4.0f, 0.075f, 0.075f, 1.0f,
        1.0f, 0.4f, 0.3f, 0.0f, 0.0f, 0.0f,
        0.85f, 2.2f, 0.24f, 1.75f,
        { 0.0356f, 0.6724f, 1.0f, 1.0f },
        { 1.0f, 1.0f, 1.0f, 1.0f },
        { 0.0356f, 0.6724f, 1.0f, 1.0f },
        "modeDefault", "2026"
    },
    ParticlePresetMetadata {
        "cube-coins", "Cube to Coins", "Transitions", "Mesh Transition", "A gold cube of spun coins releasing in a wave, bursting outward and bouncing along the floor.",
        false, 384u, 0.0f, 0u, 5u, 5u, 0u, 0u, 0u, 9u, false,
        { 0.0f, 0.65f, 0.0f }, 0.0f, { 0.6f, 0.6f, 0.6f },
        { 0.0f, 1.0f, 0.0f }, 0.5f, 1.5f, 4.0f,
        4.0f, 6.0f, 0.14f, 0.14f, 1.0f,
        1.0f, 0.2f, 0.2f, 0.35f, 0.0f, 0.0f,
        0.85f, 2.2f, 0.24f, 1.75f,
        { 1.0f, 0.5776f, 0.0467f, 1.0f },
        { 1.0f, 0.8963f, 0.4342f, 1.0f },
        { 1.0f, 0.5776f, 0.0467f, 1.0f },
        "modeDefault", "2026"
    },
    ParticlePresetMetadata {
        "melt-away", "Melt Away", "Transitions", "Melt / Dissolve", "Surface blocks detach in organic patches across the cube and drift off on the wind.",
        false, 1536u, 0.0f, 0u, 5u, 4u, 0u, 0u, 0u, 0u, false,
        { 0.0f, 0.65f, 0.0f }, 0.0f, { 0.6f, 0.6f, 0.6f },
        { 0.0f, 1.0f, 0.0f }, 0.5f, 0.2f, 0.9f,
        6.0f, 8.0f, 0.075f, 0.05f, 1.0f,
        0.2f, 0.6f, 1.4f, 0.0f, 0.0f, 0.0f,
        0.85f, 2.2f, 0.24f, 1.75f,
        { 1.0f, 0.4508f, 0.063f, 1.0f },
        { 1.0f, 0.8796f, 0.6724f, 1.0f },
        { 1.0f, 0.1022f, 0.0137f, 1.0f },
        "modeDefault", "2026"
    },
    ParticlePresetMetadata {
        "reverse-derez", "Reverse Derez", "Transitions", "Reverse Derez", "Cubes fly in from an outer shell and snap onto the cube surface in a wave from one corner.",
        false, 1536u, 0.0f, 0u, 5u, 4u, 0u, 0u, 0u, 0u, false,
        { 0.0f, 0.65f, 0.0f }, 0.0f, { 0.6f, 0.6f, 0.6f },
        { 0.0f, 1.0f, 0.0f }, 0.5f, 0.5f, 2.0f,
        3.0f, 4.0f, 0.075f, 0.075f, 1.0f,
        0.0f, 0.4f, 0.0f, 0.0f, 0.0f, 0.0f,
        0.85f, 2.2f, 0.24f, 1.75f,
        { 0.0356f, 0.6724f, 1.0f, 1.0f },
        { 1.0f, 1.0f, 1.0f, 1.0f },
        { 0.0356f, 0.6724f, 1.0f, 1.0f },
        "modeDefault", "2026"
    },
    ParticlePresetMetadata {
        "voxel-explosion", "Voxel Explosion", "Transitions", "Mesh Transition", "A solid sphere of cubes holds briefly, then every voxel bursts radially and tumbles.",
        false, 1024u, 0.0f, 1u, 5u, 4u, 0u, 0u, 0u, 11u, false,
        { 0.0f, 2.2f, 0.0f }, 0.9f, { 0.0f, 0.0f, 0.0f },
        { 0.0f, 1.0f, 0.0f }, 0.5f, 2.0f, 4.5f,
        2.5f, 4.0f, 0.12f, 0.1f, 1.0f,
        0.12f, 0.9f, 0.2f, 0.0f, 0.0f, 0.0f,
        0.85f, 2.2f, 0.24f, 1.75f,
        { 1.0f, 0.1022f, 0.0452f, 1.0f },
        { 1.0f, 1.0f, 1.0f, 1.0f },
        { 1.0f, 0.6376f, 0.1329f, 1.0f },
        "modeDefault", "2026"
    },
    ParticlePresetMetadata {
        "coin-stack", "Coin Stack", "Transitions", "Coin Stack", "Gold coins drop sequentially into a vertical floor column and settle cleanly into a stack.",
        false, 64u, 0.0f, 0u, 5u, 5u, 0u, 0u, 0u, 9u, false,
        { 0.0f, 0.0f, 0.0f }, 0.0f, { 0.0f, 0.0f, 0.0f },
        { 0.0f, 1.0f, 0.0f }, 0.5f, 0.0f, 0.0f,
        1.0f, 1.0f, 0.4f, 0.4f, 1.0f,
        0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
        0.85f, 2.2f, 0.24f, 1.75f,
        { 1.0f, 0.5776f, 0.0467f, 1.0f },
        { 1.0f, 0.8963f, 0.4342f, 1.0f },
        { 1.0f, 0.5776f, 0.0467f, 1.0f },
        "modeDefault", "2026"
    },
    ParticlePresetMetadata {
        "dissolve-nothing", "Dissolve to Nothing", "Transitions", "Melt / Dissolve", "Surface blocks release in organic patches, drift on the wind, shrink and vanish.",
        false, 1536u, 0.0f, 0u, 5u, 4u, 0u, 0u, 0u, 0u, false,
        { 0.0f, 0.65f, 0.0f }, 0.0f, { 0.6f, 0.6f, 0.6f },
        { 0.0f, 1.0f, 0.0f }, 0.5f, 0.1f, 0.6f,
        2.5f, 4.0f, 0.075f, 0.0f, 1.0f,
        0.1f, 0.8f, 1.2f, 0.0f, 0.2f, 0.0f,
        0.85f, 2.2f, 0.24f, 1.75f,
        { 0.2122f, 0.6724f, 1.0f, 1.0f },
        { 1.0f, 1.0f, 1.0f, 1.0f },
        { 0.5776f, 0.8714f, 1.0f, 1.0f },
        "modeDefault", "2026"
    },
    ParticlePresetMetadata {
        "dissolve-butterflies", "Dissolve into Butterflies", "Transitions", "Butterfly Dissolve", "Surface blocks melt away in patches, each transforming into a flapping butterfly rising on the wind.",
        false, 1536u, 0.0f, 0u, 5u, 4u, 0u, 0u, 0u, 11u, false,
        { 0.0f, 0.65f, 0.0f }, 0.0f, { 0.6f, 0.6f, 0.6f },
        { 0.0f, 1.0f, 0.0f }, 0.5f, 0.2f, 0.9f,
        6.0f, 9.0f, 0.075f, 0.075f, 1.0f,
        0.0f, 0.8f, 0.9f, 0.0f, 0.6f, 1.2f,
        0.85f, 2.2f, 0.24f, 1.75f,
        { 1.0f, 0.3231f, 0.0452f, 1.0f },
        { 1.0f, 1.0f, 1.0f, 1.0f },
        { 0.0482f, 0.5776f, 1.0f, 1.0f },
        "modeDefault", "2026"
    },
    ParticlePresetMetadata {
        "glitch-dissolve", "Glitch Dissolve", "Transitions", "Glitch Dissolve", "Digital dissolve breaking surface cubes in quantized grid steps with staccato glitch jumps.",
        false, 1536u, 0.0f, 0u, 5u, 4u, 0u, 0u, 0u, 0u, false,
        { 0.0f, 0.65f, 0.0f }, 0.0f, { 0.6f, 0.6f, 0.6f },
        { 0.0f, 1.0f, 0.0f }, 0.5f, 0.1f, 0.6f,
        2.5f, 4.0f, 0.075f, 0.0f, 1.0f,
        0.1f, 0.8f, 1.0f, 0.0f, 0.2f, 0.0f,
        0.85f, 2.2f, 0.24f, 1.75f,
        { 0.0f, 1.0f, 0.6308f, 1.0f },
        { 1.0f, 1.0f, 1.0f, 1.0f },
        { 1.0f, 0.0262f, 0.159f, 1.0f },
        "modeDefault", "2026"
    },
    ParticlePresetMetadata {
        "black-hole", "Black Hole Accretion", "Space & Distortion", "Black Hole Lens", "Glowing accretion disc spiraling into a dark event horizon with screen-space Einstein ring gravitational lensing.",
        false, 3000u, 400.0f, 2u, 5u, 3u, 0u, 0u, 0u, 4u, true,
        { 0.0f, 2.2f, 0.0f }, 2.6f, { 0.0f, 0.0f, 0.0f },
        { 0.0f, 1.0f, 0.0f }, 0.05f, 0.0f, 0.2f,
        6.0f, 10.0f, 0.075f, 0.02f, 1.0f,
        0.0f, 0.2f, 0.0f, 0.0f, 0.0f, 0.0f,
        0.85f, 2.2f, 0.24f, 1.75f,
        { 1.0f, 0.6308f, 0.2542f, 1.0f },
        { 1.0f, 1.0f, 1.0f, 1.0f },
        { 1.0f, 0.1441f, 0.0103f, 1.0f },
        "modeDefault", "2026"
    },
    ParticlePresetMetadata {
        "heat-shimmer", "Heat Shimmer Distortion", "Space & Distortion", "Heat Shimmer", "Rising thermal plume bending the scene behind it via screen-space refractive noise distortion.",
        false, 200u, 30.0f, 1u, 0u, 3u, 0u, 0u, 0u, 11u, false,
        { 0.0f, 0.4f, 0.0f }, 0.5f, { 0.0f, 0.0f, 0.0f },
        { 0.0f, 1.0f, 0.0f }, 0.3f, 0.6f, 1.4f,
        2.0f, 3.5f, 0.3f, 0.9f, 1.0f,
        0.0f, 0.5f, 0.6f, 0.0f, 1.5f, 0.4f,
        0.85f, 2.2f, 0.24f, 1.75f,
        { 0.0232f, 0.0319f, 0.0437f, 1.0f },
        { 1.0f, 1.0f, 1.0f, 1.0f },
        { 0.0467f, 0.0666f, 0.0887f, 1.0f },
        "modeDefault", "2026"
    },
    ParticlePresetMetadata {
        "strikeSparks", "Arc Strike Sparks", "Ballistics & Sparks", "Streak", "Dedicated ground-scatter spark burst triggered automatically at arc contact whenever lightning fires.",
        false, 1600u, 0.0f, 0u, 0u, 0u, 0u, 0u, 0u, 0u, true,
        { 0.0f, 0.12f, 0.0f }, 0.1f, { 0.0f, 0.0f, 0.0f },
        { 0.0f, 1.0f, 0.0f }, 1.5f, 1.8f, 7.5f,
        0.45f, 1.15f, 0.034f, 0.008f, 1.0f,
        1.1f, 0.3f, 0.25f, 0.46f, 0.0f, 0.0f,
        0.85f, 2.2f, 0.24f, 1.75f,
        { 0.85f, 0.94f, 1.0f, 1.0f },
        { 1.0f, 1.0f, 1.0f, 1.0f },
        { 0.35f, 0.62f, 1.0f, 0.0f },
        "modeDefault", "2026"
    },
    ParticlePresetMetadata {
        "energy-shield-hex", "Energy Shield & Hex Ripples", "Shields & Tech", "Hex Energy Shield", "Volumetric spherical force-field barrier with stackable hex, circle, triangle, square, diamond, Voronoi shatter, and star pattern layers plus geodesic ball impact ripples.",
        false, 64u, 0.28f, 0u, 5u, 16u, 0u, 0u, 0u, 5u, true,
        { -2.25f, 1.9f, 0.3f }, 0.1f, { 0.0f, 0.0f, 0.0f },
        { 0.0f, 0.0f, 1.0f }, 0.0f, 0.0f, 0.0f,
        6.5f, 7.5f, 3.1f, 3.1f, 1.0f,
        0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
        2.1f, 1.15f, 0.55f, 1.75f,
        { 0.016f, 0.4793f, 1.0f, 1.0f },
        { 0.2623f, 0.9131f, 1.0f, 1.0f },
        { 0.0467f, 0.147f, 1.0f, 1.0f },
        "modeDefault", "2026"
    },
    ParticlePresetMetadata {
        "energy-shield-prism", "Multi-Shape Prism Barrier", "Shields & Tech", "Multi-Shape Shield", "Layered geometric energy shield combining Triangles + Circles + Diamonds + Voronoi crack lines with directional face orientation and projectile impact ripples.",
        false, 64u, 0.28f, 0u, 5u, 16u, 0u, 0u, 0u, 5u, true,
        { 2.25f, 1.9f, -0.3f }, 0.1f, { 0.0f, 0.0f, 0.0f },
        { -0.28f, 0.05f, 0.96f }, 0.0f, 0.0f, 0.0f,
        6.5f, 7.5f, 3.1f, 3.1f, 1.0f,
        0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
        2.2f, 1.25f, 0.62f, 1.75f,
        { 0.2016f, 0.0742f, 1.0f, 1.0f },
        { 0.0395f, 0.9473f, 0.6795f, 1.0f },
        { 1.0f, 0.0762f, 0.6105f, 1.0f },
        "modeDefault", "2026"
    },
    ParticlePresetMetadata {
        "aurora-borealis", "Aurora Borealis Curtains", "Atmosphere & Sky", "Aurora Curtain", "Shimmering vertical volumetric aurora ribbon curtains undulating across the sky with emerald-cyan-violet altitude gradients.",
        false, 180u, 16.0f, 3u, 5u, 17u, 0u, 0u, 0u, 0u, true,
        { 0.0f, 4.45f, -1.6f }, 0.1f, { 6.4f, 0.35f, 3.6f },
        { 1.0f, 0.0f, 0.25f }, 0.15f, 0.08f, 0.24f,
        6.5f, 10.5f, 2.4f, 2.75f, 1.0f,
        0.0f, 0.85f, 0.18f, 0.0f, 0.04f, 0.16f,
        1.0f, 2.2f, 0.52f, 1.75f,
        { 0.0273f, 1.0f, 0.3419f, 1.0f },
        { 0.013f, 0.6867f, 1.0f, 1.0f },
        { 0.3325f, 0.0467f, 1.0f, 1.0f },
        "modeDefault", "2026"
    },
    ParticlePresetMetadata {
        "spiderweb-orb", "Orb-Weave Spiderweb", "Silk & Curl", "Orb-Weave Web", "Real-time spinning orb-weaver web: 6-anchor silken bridge frame, outer guy lines, radial spokes, open hub zone, and a glowing spinneret extruding concentric Archimedean capture spirals with dewdrops and live wind-flex catenary sag.",
        false, 320u, 0.0f, 6u, 1u, 3u, 0u, 0u, 0u, 10u, false,
        { 1.65f, 2.05f, 0.25f }, 2.0f, { 0.0f, 0.0f, 0.0f },
        { 0.18f, 0.18f, 0.97f }, 0.0f, 0.0f, 0.0f,
        9999.0f, 9999.0f, 0.015f, 0.015f, 1.0f,
        0.45f, 0.6f, 1.15f, 0.0f, 0.0f, 0.14f,
        1.1f, 0.34f, 0.24f, 1.75f,
        { 0.8879f, 0.9301f, 1.0f, 1.0f },
        { 0.4793f, 0.6939f, 0.9387f, 1.0f },
        { 0.1559f, 0.2961f, 0.491f, 1.0f },
        "modeDefault", "2026"
    },
    ParticlePresetMetadata {
        "spiderweb-cobweb", "Messy Attic Cobwebs", "Silk & Curl", "Messy Cobwebs", "Real-time tangled 3D corner cobwebs: multi-pocket attic drapes, ragged overlapping gossamer hammock pockets, dangling broken tendrils fluttering in the wind, and drifting dust specks.",
        false, 300u, 0.0f, 6u, 1u, 3u, 0u, 0u, 0u, 10u, false,
        { -3.55f, 2.35f, -0.25f }, 0.1f, { 2.25f, 1.55f, 1.55f },
        { 0.18f, 0.08f, 0.98f }, 0.0f, 0.0f, 0.0f,
        9999.0f, 9999.0f, 0.016f, 0.016f, 1.0f,
        0.75f, 0.45f, 1.45f, 0.0f, 0.0f, 0.55f,
        1.35f, 0.5f, 0.24f, 1.75f,
        { 0.7758f, 0.8148f, 0.855f, 1.0f },
        { 0.4735f, 0.5271f, 0.5906f, 1.0f },
        { 0.1683f, 0.2051f, 0.2542f, 1.0f },
        "modeDefault", "2026"
    },
    ParticlePresetMetadata {
        "cables-chains", "Swinging Cables & Chains", "Cloth & Rigging", "Chain Links", "Multi-span catenary steel cables and heavy industrial chains suspended between 3D rigging pylons, swinging dynamically in gusts and rebounding or snapping when struck by thrown balls.",
        false, 64u, 0.0f, 7u, 1u, 3u, 0u, 0u, 0u, 10u, false,
        { -1.15f, 0.0f, 1.45f }, 0.1f, { 2.15f, 2.0f, 1.25f },
        { 1.0f, 0.0f, 0.0f }, 0.0f, 0.0f, 0.0f,
        9999.0f, 9999.0f, 0.045f, 0.045f, 1.0f,
        1.0f, 0.35f, 1.25f, 0.0f, 0.0f, 0.3f,
        0.75f, 0.55f, 0.24f, 1.75f,
        { 0.0976f, 0.1356f, 0.1812f, 1.0f },
        { 0.3185f, 0.3916f, 0.4793f, 1.0f },
        { 0.6445f, 0.7157f, 0.7913f, 1.0f },
        "modeDefault", "2026"
    },
    ParticlePresetMetadata {
        "vines-ropes", "Hanging Vines & Ropes", "Cloth & Rigging", "Leafy Vines", "Braided overhead ropes and leafy jungle ivy vines dangling in tiered canopy spans, whipping cleanly away from thrown balls and responding to Force Field gusts.",
        false, 96u, 0.0f, 7u, 1u, 2u, 0u, 0u, 0u, 10u, false,
        { 4.05f, 0.0f, 1.35f }, 0.1f, { 1.65f, 1.8f, 1.25f },
        { 0.0f, 1.0f, 0.0f }, 0.0f, 0.0f, 0.0f,
        9999.0f, 9999.0f, 0.036f, 0.032f, 1.0f,
        1.0f, 0.4f, 1.45f, 0.0f, 0.0f, 0.5f,
        0.95f, 0.65f, 0.24f, 1.75f,
        { 0.0437f, 0.1022f, 0.0262f, 1.0f },
        { 0.0931f, 0.2623f, 0.0409f, 1.0f },
        { 0.2747f, 0.6376f, 0.0976f, 1.0f },
        "modeDefault", "2026"
    },
    ParticlePresetMetadata {
        "flag-banner", "Wind Flag & Tearing Banner", "Cloth & Rigging", "Wind Flag", "Continuous 3D shaded fabric banner pinned to a flagpole mast, rippling with travelling wind waves, wrapping elastically around thrown balls, and tearing cleanly on high-speed impacts.",
        false, 64u, 0.0f, 7u, 1u, 3u, 0u, 0u, 0u, 10u, false,
        { -3.95f, 0.0f, -0.75f }, 0.1f, { 1.75f, 1.15f, 0.2f },
        { 1.0f, 0.0f, 0.0f }, 0.0f, 0.0f, 0.0f,
        9999.0f, 9999.0f, 0.065f, 0.065f, 1.0f,
        0.4f, 0.35f, 1.65f, 0.0f, 0.0f, 0.55f,
        1.15f, 0.85f, 0.24f, 1.75f,
        { 0.6724f, 0.0242f, 0.0242f, 1.0f },
        { 0.8879f, 0.4678f, 0.0369f, 1.0f },
        { 0.2542f, 0.0091f, 0.016f, 1.0f },
        "modeDefault", "2026"
    },
    ParticlePresetMetadata {
        "ship-sail", "Full-Rigged Ship Sail", "Cloth & Rigging", "Ship Sail", "Wide billowing square-rigged canvas sail stretched between an upper yardarm spar and lower boom, catching prevailing Force Field wind bellies and tearing or flexing when struck.",
        false, 64u, 0.0f, 7u, 1u, 3u, 0u, 0u, 0u, 10u, false,
        { 1.65f, 0.0f, -1.25f }, 0.1f, { 1.75f, 1.45f, 0.4f },
        { 0.0f, 0.0f, 1.0f }, 0.0f, 0.0f, 0.0f,
        9999.0f, 9999.0f, 0.075f, 0.075f, 1.0f,
        0.2f, 0.35f, 1.75f, 0.0f, 0.0f, 0.35f,
        0.85f, 0.72f, 0.24f, 1.75f,
        { 0.8963f, 0.8228f, 0.6795f, 1.0f },
        { 0.7157f, 0.6308f, 0.4678f, 1.0f },
        { 0.3419f, 0.2747f, 0.1812f, 1.0f },
        "modeDefault", "2026"
    },
    ParticlePresetMetadata {
        "grass-wheat-field", "Grass & Golden Wheat Field", "Foliage & Flora", "Wheat & Grass", "Dense interactive meadow of pointed grass blades and plumed golden wheat stalks driven by travelling stadium wind gusts, Force Field vortices, and parted cleanly by rolling or thrown balls.",
        false, 520u, 0.0f, 7u, 1u, 2u, 0u, 0u, 0u, 10u, false,
        { -0.2f, 0.0f, 0.4f }, 0.1f, { 3.2f, 0.0f, 2.4f },
        { 0.0f, 1.0f, 0.0f }, 0.0f, 0.0f, 0.0f,
        9999.0f, 9999.0f, 0.048f, 0.015f, 1.0f,
        0.0f, 0.4f, 1.55f, 0.0f, 0.0f, 0.4f,
        0.9f, 0.65f, 0.24f, 1.75f,
        { 0.0194f, 0.0999f, 0.0176f, 1.0f },
        { 0.1559f, 0.3467f, 0.0343f, 1.0f },
        { 0.807f, 0.5029f, 0.0685f, 1.0f },
        "modeDefault", "2026"
    },
    ParticlePresetMetadata {
        "tall-reeds", "Tall Lakeside Reeds", "Foliage & Flora", "Tall Reeds", "Clustered wetland reeds topped with brown velvet cattail spikes arching gracefully in breezes and bowing aside elastically when hit by thrown projectiles.",
        false, 220u, 0.0f, 7u, 1u, 3u, 0u, 0u, 0u, 10u, false,
        { -3.35f, 0.0f, 0.85f }, 0.1f, { 1.55f, 0.0f, 1.35f },
        { 0.0f, 1.0f, 0.0f }, 0.0f, 0.0f, 0.0f,
        9999.0f, 9999.0f, 0.056f, 0.018f, 1.0f,
        0.0f, 0.4f, 1.45f, 0.0f, 0.0f, 0.35f,
        0.8f, 0.6f, 0.24f, 1.75f,
        { 0.0137f, 0.0742f, 0.0212f, 1.0f },
        { 0.0908f, 0.2232f, 0.0382f, 1.0f },
        { 0.1683f, 0.063f, 0.0232f, 1.0f },
        "modeDefault", "2026"
    },
    ParticlePresetMetadata {
        "interactive-fern", "Lush Interactive Fern", "Foliage & Flora", "Pinnate Fern", "Broad woodland fern built from tiered arched compound fronds with feathered pinnate leaflets and glowing central veins; individual fronds flex, twist, and rebound when hit.",
        false, 128u, 0.0f, 7u, 1u, 2u, 0u, 0u, 0u, 10u, false,
        { -0.95f, 0.0f, 2.15f }, 0.1f, { 1.85f, 0.85f, 1.85f },
        { 0.0f, 1.0f, 0.0f }, 0.0f, 0.0f, 0.0f,
        9999.0f, 9999.0f, 0.34f, 0.06f, 1.0f,
        0.0f, 0.35f, 1.45f, 0.0f, 0.0f, 0.35f,
        1.05f, 0.52f, 0.24f, 1.75f,
        { 0.0103f, 0.0844f, 0.0176f, 1.0f },
        { 0.0545f, 0.314f, 0.0369f, 1.0f },
        { 0.2705f, 0.6939f, 0.0844f, 1.0f },
        "modeDefault", "2026"
    },
    ParticlePresetMetadata {
        "interactive-tree", "Swaying Interactive Canopy Tree", "Foliage & Flora", "Swaying Tree", "Articulated swaying tree with flexing trunk, 24 wind-responsive canopy boughs, and 490 trembling foliage sprigs; thrown balls shake the branches and knock loose falling leaves.",
        false, 520u, 0.0f, 7u, 1u, 2u, 0u, 0u, 0u, 10u, false,
        { 2.45f, 0.0f, -0.75f }, 0.1f, { 2.25f, 2.2f, 2.25f },
        { 0.0f, 1.0f, 0.0f }, 0.0f, 0.0f, 0.0f,
        9999.0f, 9999.0f, 0.28f, 0.24f, 1.0f,
        0.55f, 0.4f, 1.5f, 0.0f, 0.0f, 0.65f,
        0.85f, 0.62f, 0.24f, 1.75f,
        { 0.0762f, 0.0369f, 0.016f, 1.0f },
        { 0.0437f, 0.2423f, 0.0319f, 1.0f },
        { 0.2232f, 0.624f, 0.0762f, 1.0f },
        "modeDefault", "2026"
    },
    ParticlePresetMetadata {
        "effector-grid-cubes", "Effector Cube Matrix · Height Up/Down", "Motion & Grids", "Height Cubes", "Motion-graphics 3D cube matrix with fixed footprint where effector waves, Force Fields, and thrown balls drive column heights cleanly up and down.",
        false, 576u, 0.0f, 4u, 5u, 4u, 0u, 0u, 0u, 13u, false,
        { 3.55f, 0.04f, 0.2f }, 0.1f, { 1.7f, 0.25f, 1.7f },
        { 0.0f, 1.0f, 0.0f }, 0.0f, 0.0f, 0.0f,
        9999.0f, 9999.0f, 0.125f, 0.125f, 1.0f,
        0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
        1.15f, 0.65f, 0.24f, 1.75f,
        { 0.0103f, 0.013f, 0.0194f, 1.0f },
        { 1.0f, 0.016f, 0.0369f, 1.0f },
        { 1.0f, 0.2462f, 0.0578f, 1.0f },
        "modeDefault", "2026"
    },
    ParticlePresetMetadata {
        "effector-sphere-dots", "Halftone Effector Sphere · Circles", "Motion & Grids", "Halftone Circles", "Dark obsidian 3D sphere wrapped in surface-conforming halftone dots and tiles scaled by orbiting effectors, Force Fields, and thrown balls.",
        false, 720u, 0.0f, 9u, 5u, 1u, 2u, 0u, 0u, 13u, false,
        { -3.35f, 2.05f, 0.0f }, 1.32f, { 1.32f, 1.32f, 1.32f },
        { 0.0f, 1.0f, 0.0f }, 0.0f, 0.0f, 0.0f,
        9999.0f, 9999.0f, 0.12f, 0.12f, 1.0f,
        0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
        1.35f, 0.75f, 0.24f, 1.75f,
        { 0.0103f, 0.0123f, 0.0176f, 1.0f },
        { 1.0f, 0.0116f, 0.0296f, 1.0f },
        { 1.0f, 0.117f, 0.1878f, 1.0f },
        "modeDefault", "2026"
    },
    ParticlePresetMetadata {
        "effector-hand-halftone", "Obsidian Hand · Halftone & Tiles", "Motion & Grids", "Halftone & Tiles", "Sculpted obsidian hand holding a chrome sphere—swappable to any 3D object or imported OBJ/STL mesh—wrapped in wave- and effector-modulated halftone dots & raised tiles.",
        false, 1200u, 0.0f, 9u, 5u, 1u, 3u, 0u, 0u, 13u, false,
        { 0.0f, 2.05f, 0.1f }, 1.35f, { 1.35f, 1.35f, 1.35f },
        { 0.0f, 1.0f, 0.0f }, 0.0f, 0.0f, 0.0f,
        9999.0f, 9999.0f, 0.068f, 0.068f, 1.0f,
        0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
        1.45f, 0.58f, 0.24f, 1.75f,
        { 0.0116f, 0.0123f, 0.016f, 1.0f },
        { 1.0f, 0.011f, 0.0262f, 1.0f },
        { 1.0f, 0.0685f, 0.1559f, 1.0f },
        "modeDefault", "2026"
    },
    ParticlePresetMetadata {
        "effector-torus-hex", "Kinetic Hex Torus Matrix", "Motion & Grids", "Hex Grid", "Revolving torus lattice tiled with crisp hexagonal effector plates that ripple in radial waves and flare along passing Force Fields & balls.",
        false, 760u, 0.0f, 9u, 5u, 1u, 4u, 0u, 0u, 13u, false,
        { 0.0f, 2.15f, -3.25f }, 1.35f, { 1.35f, 1.35f, 1.35f },
        { 0.0f, 1.0f, 0.0f }, 0.0f, 0.0f, 0.0f,
        9999.0f, 9999.0f, 0.072f, 0.072f, 1.0f,
        0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
        1.25f, 0.72f, 0.24f, 1.75f,
        { 0.0075f, 0.0097f, 0.0232f, 1.0f },
        { 0.0666f, 0.6445f, 1.0f, 1.0f },
        { 1.0f, 0.7454f, 0.1329f, 1.0f },
        "modeDefault", "2026"
    },
    ParticlePresetMetadata {
        "effector-object-placer", "Placeable 3D Object · MoGraph & Effectors", "Motion & Grids", "Halftone & Tiles", "Place any 3D object from the built-in library or import any custom OBJ / STL / JSON mesh, transform it in 3D, and sculpt its particle surface with draggable Effectors A & B.",
        false, 1400u, 0.0f, 9u, 5u, 1u, 6u, 0u, 0u, 13u, false,
        { 0.0f, 1.85f, 0.0f }, 1.55f, { 1.55f, 1.55f, 1.55f },
        { 0.0f, 1.0f, 0.0f }, 0.0f, 0.0f, 0.0f,
        9999.0f, 9999.0f, 0.068f, 0.068f, 1.0f,
        0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
        1.35f, 0.65f, 0.24f, 1.75f,
        { 0.0091f, 0.0116f, 0.0212f, 1.0f },
        { 0.0482f, 0.552f, 0.1529f, 1.0f },
        { 0.8714f, 0.6867f, 0.1878f, 1.0f },
        "modeDefault", "2026"
    },
    ParticlePresetMetadata {
        "car-puddle-splash", "Car Puddle & Mud Splash", "Quarantine", "Car Mud Splash", "3D rally car cutting through a shallow-water puddle, shedding thin tire rooster-tail mud ligaments, bow-wave spray sheets, and fine droplets.",
        true, 240u, 88.0f, 2u, 0u, 14u, 0u, 0u, 0u, 8u, false,
        { 0.0f, 0.02f, 0.0f }, 1.85f, { 0.0f, 0.0f, 0.0f },
        { 0.0f, 1.0f, 0.0f }, 0.36f, 2.0f, 6.2f,
        0.42f, 0.92f, 0.022f, 0.008f, 1.0f,
        1.28f, 1.55f, 0.28f, 0.04f, 0.0f, 0.02f,
        1.15f, 1.1f, 0.24f, 1.75f,
        { 0.0242f, 0.011f, 0.0044f, 1.0f },
        { 0.0742f, 0.0356f, 0.013f, 1.0f },
        { 0.3185f, 0.2705f, 0.1946f, 1.0f },
        "modeDefault", "2026"
    },
    ParticlePresetMetadata {
        "puddle-ripple-splash", "Puddle Ripple Rings & Splash", "Quarantine", "Puddle Ripples", "Shallow-water wave puddle with crisp Rayleigh-Taylor drop crown fingers, central Worthington jets, and ball splash craters.",
        true, 150u, 50.0f, 2u, 0u, 14u, 0u, 0u, 0u, 8u, false,
        { -3.8f, 0.02f, -3.2f }, 2.0f, { 0.0f, 0.0f, 0.0f },
        { 0.0f, 1.0f, 0.0f }, 0.3f, 1.15f, 3.6f,
        0.44f, 0.94f, 0.016f, 0.006f, 1.0f,
        1.15f, 1.65f, 0.22f, 0.04f, 0.0f, 0.01f,
        1.0f, 0.9f, 0.24f, 1.75f,
        { 0.0437f, 0.0844f, 0.1248f, 1.0f },
        { 0.2384f, 0.4072f, 0.5395f, 1.0f },
        { 0.7913f, 0.8879f, 0.9387f, 1.0f },
        "modeDefault", "2026"
    },
    ParticlePresetMetadata {
        "mud-splatter-heavy", "Mud Splash & Sticky Clods", "Quarantine", "Mud Splatter", "Compact multi-fingered mud impact eruption firing ragged micro-clods, thin liquid filaments, and fine mist spray.",
        true, 180u, 0.0f, 2u, 0u, 14u, 0u, 0u, 0u, 9u, false,
        { 3.8f, 0.04f, 3.0f }, 0.65f, { 0.0f, 0.0f, 0.0f },
        { 0.0f, 1.0f, 0.0f }, 0.52f, 2.2f, 5.8f,
        0.55f, 1.15f, 0.024f, 0.01f, 1.0f,
        1.32f, 1.45f, 0.18f, 0.02f, 0.0f, 0.01f,
        1.2f, 1.15f, 0.24f, 1.75f,
        { 0.0152f, 0.006f, 0.0024f, 1.0f },
        { 0.0545f, 0.0203f, 0.0065f, 1.0f },
        { 0.147f, 0.0612f, 0.0203f, 1.0f },
        "modeDefault", "2026"
    },
    ParticlePresetMetadata {
        "ferrofluid-spikes", "Magnetic Ferrofluid Dish", "Ferrofluid & Fields", "Ferrofluid Pool", "Glossy wet jet-black obsidian ferrofluid pool swelling into smooth concave hexagonal Rosensweig cusps with crisp studio softbox reflections pulled by an orbiting neodymium magnet.",
        false, 220u, 0.0f, 2u, 5u, 1u, 0u, 0u, 0u, 5u, false,
        { -1.95f, 0.04f, 0.25f }, 2.05f, { 2.05f, 1.05f, 2.05f },
        { 0.0f, 1.0f, 0.0f }, 0.0f, 0.0f, 0.0f,
        9999.0f, 9999.0f, 0.235f, 0.045f, 1.0f,
        0.0f, 0.0f, 0.15f, 0.0f, 0.0f, 0.0f,
        1.15f, 1.35f, 0.24f, 1.75f,
        { 0.0009f, 0.0012f, 0.0021f, 1.0f },
        { 0.0262f, 0.0561f, 0.1248f, 1.0f },
        { 0.9047f, 0.956f, 1.0f, 1.0f },
        "modeDefault", "2026"
    },
    ParticlePresetMetadata {
        "ferrofluid-orb", "Levitated Ferrofluid Crown", "Ferrofluid & Fields", "Levitated Ferrofluid", "Free-floating 3D glossy wet-black obsidian ferrofluid sphere bristling with smooth concave Rosensweig liquid peaks and razor-sharp studio highlights.",
        false, 220u, 0.0f, 1u, 5u, 1u, 0u, 0u, 0u, 5u, false,
        { 2.45f, 2.05f, -0.2f }, 1.22f, { 1.22f, 0.98f, 1.22f },
        { 0.0f, 1.0f, 0.0f }, 0.0f, 0.0f, 0.0f,
        9999.0f, 9999.0f, 0.205f, 0.038f, 1.0f,
        0.0f, 0.0f, 0.15f, 0.0f, 0.0f, 0.0f,
        1.2f, 1.25f, 0.24f, 1.75f,
        { 0.0009f, 0.0012f, 0.0021f, 1.0f },
        { 0.0252f, 0.0595f, 0.1384f, 1.0f },
        { 0.807f, 0.9301f, 1.0f, 1.0f },
        "modeDefault", "2026"
    },
    ParticlePresetMetadata {
        "cracked-ice-sheet", "Movie Cracked Glacial Ice", "Fracture & Cracks", "Cracked Ice", "Real branching glacial ice cracks racing outward with 3D internal cleavage depth, sparkling frost bursts, and tumbling 3D ice shard meshes erupting from advancing crack tips.",
        false, 640u, 0.0f, 3u, 5u, 18u, 0u, 0u, 0u, 9u, false,
        { -2.45f, 0.02f, 0.2f }, 0.1f, { 2.35f, 0.14f, 2.35f },
        { 0.0f, 1.0f, 0.0f }, 0.45f, 1.4f, 3.8f,
        0.65f, 1.65f, 0.155f, 0.09f, 1.0f,
        0.95f, 0.85f, 0.3f, 0.38f, 0.0f, 0.0f,
        1.15f, 1.0f, 0.24f, 1.75f,
        { 0.3916f, 0.855f, 1.0f, 1.0f },
        { 0.7379f, 0.9387f, 1.0f, 1.0f },
        { 1.0f, 1.0f, 1.0f, 1.0f },
        "modeDefault", "2026"
    },
    ParticlePresetMetadata {
        "ground-crack-quake", "Seismic Ground Crack Fissure", "Fracture & Cracks", "Ground Fracture", "Branching tectonic ground chasm splitting dark basalt rock open over incandescent molten lava fissures with flying 3D basalt rock shards and rising fissure embers.",
        false, 640u, 0.0f, 3u, 5u, 18u, 0u, 0u, 0u, 9u, false,
        { 2.45f, 0.02f, -0.2f }, 0.1f, { 2.35f, 0.16f, 2.35f },
        { 0.0f, 1.0f, 0.0f }, 0.55f, 1.8f, 4.5f,
        0.7f, 1.65f, 0.165f, 0.09f, 1.0f,
        1.05f, 0.75f, 0.25f, 0.32f, 0.0f, 0.0f,
        1.25f, 1.15f, 0.24f, 1.75f,
        { 0.0437f, 0.0343f, 0.0296f, 1.0f },
        { 1.0f, 0.1022f, 0.0137f, 1.0f },
        { 1.0f, 0.6376f, 0.1329f, 1.0f },
        "modeDefault", "2026"
    },
    ParticlePresetMetadata {
        "plasma-cage-arcs", "Plasma Cage & Arc Collector", "Silk & Curl", "Plasma Cage", "Perimeter conductor ring and central plasma orb trapping crackling branching arcs that violently snap onto thrown balls passing near the cage.",
        false, 640u, 45.0f, 1u, 5u, 3u, 0u, 0u, 0u, 12u, true,
        { -2.4f, 2.15f, 0.2f }, 1.75f, { 1.75f, 1.45f, 1.75f },
        { 0.0f, 1.0f, 0.0f }, 3.1416f, 1.4f, 4.8f,
        0.14f, 0.36f, 0.095f, 0.014f, 1.0f,
        0.1f, 3.0f, 0.2f, 0.0f, 0.08f, 0.0f,
        0.85f, 2.2f, 0.24f, 1.75f,
        { 0.1683f, 0.7605f, 1.0f, 1.0f },
        { 1.0f, 1.0f, 1.0f, 1.0f },
        { 0.0844f, 0.162f, 1.0f, 1.0f },
        "modeDefault", "2026"
    },
    ParticlePresetMetadata {
        "lightning-web-mesh", "Dynamic Lightning Web & Storm Grid", "Silk & Curl", "Lightning Web", "Floating 3D matrix of high-voltage storm nodes woven together by multi-node zigzag lightning webs that redirect through passing balls.",
        false, 680u, 52.0f, 3u, 5u, 3u, 0u, 0u, 0u, 12u, true,
        { 2.4f, 2.15f, -0.2f }, 2.05f, { 2.1f, 1.5f, 2.1f },
        { 0.0f, 1.0f, 0.0f }, 3.1416f, 1.6f, 5.4f,
        0.12f, 0.34f, 0.09f, 0.012f, 1.0f,
        0.06f, 2.8f, 0.25f, 0.0f, 0.12f, 0.0f,
        0.85f, 2.2f, 0.24f, 1.75f,
        { 0.3185f, 0.807f, 1.0f, 1.0f },
        { 0.807f, 0.9301f, 1.0f, 1.0f },
        { 0.2542f, 0.1329f, 1.0f, 1.0f },
        "modeDefault", "2026"
    },
    ParticlePresetMetadata {
        "kelp-forest", "Underwater Giant Kelp Forest", "Foliage & Flora", "Kelp Forest", "Buoyant giant kelp stipes, ruffled side blades, and golden gas bladders drifting in gentle underwater currents and parting around swimming balls.",
        false, 240u, 0.0f, 7u, 1u, 1u, 0u, 0u, 0u, 10u, false,
        { -1.55f, 0.0f, 0.35f }, 0.1f, { 2.25f, 2.2f, 2.25f },
        { 0.0f, 1.0f, 0.0f }, 0.0f, 0.0f, 0.0f,
        9999.0f, 9999.0f, 0.16f, 0.04f, 1.0f,
        0.0f, 0.0f, 1.25f, 0.2f, 0.45f, 0.0f,
        0.85f, 1.2f, 0.24f, 1.75f,
        { 0.0103f, 0.0545f, 0.013f, 1.0f },
        { 0.0742f, 0.2542f, 0.0262f, 1.0f },
        { 0.7454f, 0.3813f, 0.0296f, 1.0f },
        "modeDefault", "2026"
    },
    ParticlePresetMetadata {
        "sea-anemone", "Bioluminescent Sea Anemone Colony", "Foliage & Flora", "Sea Anemone", "Clusters of glowing violet-and-cyan reef anemone tentacles waving hypnotically in water flow and curling inward defensively when touched.",
        false, 220u, 0.0f, 7u, 1u, 1u, 0u, 0u, 0u, 10u, false,
        { 2.35f, 0.0f, -0.4f }, 1.55f, { 1.55f, 1.0f, 1.55f },
        { 0.0f, 1.0f, 0.0f }, 0.0f, 0.0f, 0.0f,
        9999.0f, 9999.0f, 0.072f, 0.015f, 1.0f,
        0.0f, 0.0f, 1.0f, 0.2f, 0.25f, 0.0f,
        1.1f, 0.95f, 0.24f, 1.75f,
        { 0.0887f, 0.0116f, 0.147f, 1.0f },
        { 1.0f, 0.0482f, 0.2542f, 1.0f },
        { 0.0319f, 1.0f, 0.7913f, 1.0f },
        "modeDefault", "2026"
    },
    ParticlePresetMetadata {
        "beaded-door-curtain", "Crystal Bead Door Curtain", "Cloth & Rigging", "Bead Curtain", "Lintel-pinned vertical strands of shimmering faceted crystal & magenta beads that part elastically around thrown balls and clatter cleanly.",
        false, 280u, 0.0f, 3u, 5u, 1u, 0u, 0u, 0u, 0u, false,
        { -2.15f, 0.0f, 0.2f }, 0.1f, { 2.15f, 1.55f, 0.26f },
        { 0.0f, 1.0f, 0.0f }, 0.0f, 0.0f, 0.0f,
        9999.0f, 9999.0f, 0.072f, 0.072f, 1.0f,
        0.9f, 0.0f, 1.1f, 0.0f, 0.0f, 0.0f,
        0.95f, 1.15f, 0.24f, 1.75f,
        { 0.0395f, 0.0144f, 0.1195f, 1.0f },
        { 0.107f, 0.7529f, 1.0f, 1.0f },
        { 1.0f, 0.2462f, 0.6724f, 1.0f },
        "modeDefault", "2026"
    },
    ParticlePresetMetadata {
        "bamboo-wind-chimes", "Bamboo & Brass Wind Chimes", "Cloth & Rigging", "Bamboo Chimes", "Hollow tuned bamboo & brushed-brass chimes suspended from an arched striker crown around a dangling sail clapper that ring & glow when struck.",
        false, 120u, 0.0f, 2u, 5u, 1u, 0u, 0u, 0u, 0u, false,
        { 0.15f, 0.0f, 0.0f }, 0.56f, { 0.62f, 1.45f, 0.62f },
        { 0.0f, 1.0f, 0.0f }, 0.0f, 0.0f, 0.0f,
        9999.0f, 9999.0f, 0.088f, 0.088f, 1.0f,
        1.0f, 0.0f, 1.25f, 0.0f, 0.0f, 0.0f,
        0.85f, 1.0f, 0.24f, 1.75f,
        { 0.2159f, 0.0844f, 0.0137f, 1.0f },
        { 0.7379f, 0.3968f, 0.0666f, 1.0f },
        { 1.0f, 0.9131f, 0.5089f, 1.0f },
        "modeDefault", "2026"
    },
    ParticlePresetMetadata {
        "velvet-theater-drapes", "Velvet Theatre Drape Strips", "Cloth & Rigging", "Doorway Drapes", "Tall top-rail doorway curtain pleats billowing gently in indoor air drafts and parting softly around passing 3D projectiles.",
        false, 180u, 0.0f, 3u, 5u, 1u, 0u, 0u, 0u, 0u, false,
        { 2.3f, 0.0f, -0.2f }, 0.1f, { 2.05f, 1.5f, 0.22f },
        { 0.0f, 1.0f, 0.0f }, 0.0f, 0.0f, 0.0f,
        9999.0f, 9999.0f, 0.115f, 0.115f, 1.0f,
        1.0f, 0.0f, 1.35f, 0.0f, 0.0f, 0.0f,
        0.8f, 1.35f, 0.24f, 1.75f,
        { 0.0742f, 0.0024f, 0.0116f, 1.0f },
        { 0.402f, 0.0091f, 0.0513f, 1.0f },
        { 1.0f, 0.6654f, 0.1946f, 1.0f },
        "modeDefault", "2026"
    },
    ParticlePresetMetadata {
        "dandelion-puff", "Dandelion Puff & Wish Seeds", "Foliage & Flora", "Dandelion Seeds", "Delicate spherical pappus parachute seeds poised on swaying meadow stalks — blow away in swirling gusts or shatter into floating plumes on ball pass.",
        false, 460u, 0.0f, 1u, 5u, 2u, 0u, 0u, 0u, 14u, false,
        { -1.85f, 0.0f, 0.15f }, 0.54f, { 1.75f, 1.0f, 1.75f },
        { 0.0f, 1.0f, 0.0f }, 0.4f, 0.6f, 2.2f,
        12.0f, 18.0f, 0.088f, 0.085f, 1.0f,
        0.18f, 1.35f, 2.25f, 0.0f, 0.32f, 1.45f,
        0.95f, 1.45f, 0.24f, 1.75f,
        { 0.0307f, 0.147f, 0.0176f, 1.0f },
        { 0.855f, 0.9301f, 0.7682f, 1.0f },
        { 1.0f, 1.0f, 1.0f, 1.0f },
        "modeDefault", "2026"
    },
    ParticlePresetMetadata {
        "blossom-branch-detach", "Spring Blossom Detachment", "Quarantine", "Blossom Boughs", "Flowering orchard boughs trembling under wind eddies and shaking loose clouds of spinning sakura & apple blossoms when brushed or gusted.",
        true, 480u, 0.0f, 3u, 5u, 2u, 0u, 0u, 0u, 14u, false,
        { 2.1f, 0.0f, -0.15f }, 0.48f, { 1.95f, 1.3f, 1.75f },
        { 0.0f, 1.0f, 0.0f }, 0.5f, 0.5f, 1.9f,
        10.0f, 16.0f, 0.098f, 0.09f, 1.0f,
        0.24f, 1.15f, 2.45f, 0.0f, 0.15f, 1.85f,
        1.05f, 1.35f, 0.24f, 1.75f,
        { 0.0887f, 0.0331f, 0.0194f, 1.0f },
        { 1.0f, 0.4621f, 0.5906f, 1.0f },
        { 1.0f, 0.8879f, 0.9216f, 1.0f },
        "modeDefault", "2026"
    },
    ParticlePresetMetadata {
        "jelly-slime-blob", "Soft-Body Jelly Cube & Tendrils", "Quarantine", "Soft-Body Jelly", "Volume-preserving neon-lime gelatin body that wobbles harmonically, squashes cleanly against floor/balls, and pulls glossy sticky tendrils.",
        true, 440u, 0.0f, 3u, 5u, 1u, 0u, 0u, 0u, 8u, false,
        { -1.8f, 1.25f, 0.2f }, 1.15f, { 1.15f, 1.15f, 1.15f },
        { 0.0f, 1.0f, 0.0f }, 0.2f, 0.0f, 0.0f,
        9999.0f, 9999.0f, 0.115f, 0.115f, 1.0f,
        1.0f, 0.4f, 0.85f, 0.72f, 0.0f, 0.0f,
        1.25f, 0.85f, 0.24f, 1.75f,
        { 0.0027f, 0.3916f, 0.1274f, 1.0f },
        { 0.0319f, 1.0f, 0.402f, 1.0f },
        { 0.7157f, 1.0f, 0.8714f, 1.0f },
        "modeDefault", "2026"
    },
    ParticlePresetMetadata {
        "slime-tendril-cluster", "Elastic Slime Tendril Cluster", "Quarantine", "Slime Tendrils", "Glossy magenta-amber viscoelastic slime colony reaching rubbery adhesion strands toward passing spheres & wind fields before snapping back.",
        true, 440u, 0.0f, 1u, 5u, 1u, 0u, 0u, 0u, 8u, false,
        { 2.05f, 1.12f, -0.15f }, 1.08f, { 1.1f, 1.0f, 1.1f },
        { 0.0f, 1.0f, 0.0f }, 0.2f, 0.0f, 0.0f,
        9999.0f, 9999.0f, 0.11f, 0.11f, 1.0f,
        1.0f, 0.5f, 1.0f, 0.58f, 0.0f, 0.0f,
        1.35f, 1.0f, 0.24f, 1.75f,
        { 0.2346f, 0.006f, 0.1559f, 1.0f },
        { 1.0f, 0.0482f, 0.5776f, 1.0f },
        { 1.0f, 0.6376f, 0.9131f, 1.0f },
        "modeDefault", "2026"
    },
    ParticlePresetMetadata {
        "starling-murmuration", "Starling Murmuration & Predator Scatter", "Wind-Blown", "Murmuration Flock", "Hundreds of dusk-lit starlings sweeping in fluid murmuration waves and parting around fast thrown predator balls with flash-split evasion.",
        false, 520u, 95.0f, 1u, 0u, 13u, 0u, 0u, 0u, 2u, false,
        { 0.0f, 3.25f, 0.0f }, 2.35f, { 0.0f, 0.0f, 0.0f },
        { 1.0f, 0.14f, 0.0f }, 3.14f, 3.4f, 5.4f,
        16.0f, 24.0f, 0.21f, 0.2f, 1.0f,
        0.0f, 0.04f, 0.9f, 0.0f, 0.0f, 1.85f,
        0.85f, 2.2f, 0.24f, 1.75f,
        { 0.0103f, 0.016f, 0.0369f, 1.0f },
        { 0.0953f, 0.1559f, 0.305f, 1.0f },
        { 1.0f, 0.402f, 0.1559f, 1.0f },
        "modeDefault", "2026"
    },
}};

} // namespace

std::span<const ParticlePresetMetadata> QueryAllParticlePresets() noexcept
{
    return kPresetTable;
}

std::span<const std::string_view> QueryPresetCategories() noexcept
{
    return kCategoryList;
}

const ParticlePresetMetadata* FindParticlePresetById(std::string_view Identifier) noexcept
{
    for (const auto& Entry : kPresetTable)
    {
        if (Entry.Identifier == Identifier) return &Entry;
    }
    return nullptr;
}

ParticleRig InstantiateRigFromPreset(const ParticlePresetMetadata& P)
{
    ParticleRig Rig {};
    Rig.Identifier        = std::string("particle.preset.") + std::string(P.Identifier);
    Rig.DisplayName       = std::string(P.DisplayName);
    Rig.CategoryLabel     = std::string(P.CategoryLabel);
    Rig.SlotCapacity      = P.SlotCapacity;
    Rig.ReleaseRatePerSec = (P.ReleaseRate > 0.0f) ? P.ReleaseRate : std::max(40.0f, static_cast<float>(P.SlotCapacity) * 0.25f);

    // 1. Release origin variant
    const auto OrigCat = static_cast<ReleaseOriginCategory>(P.OriginCode);
    if (OrigCat == ReleaseOriginCategory::SurfaceAnchor)
    {
        SurfaceAnchorOrigin Surf {};
        Surf.Preset = static_cast<SurfacePresetCategory>(P.SurfacePreset % static_cast<uint8_t>(SurfacePresetCategory::Count));
        for (int K = 0; K < 3; ++K) Surf.CenterMeters[K] = P.OriginCenter[K];
        Surf.ScaleMeters[0] = std::max(0.6f, P.ShellRadius);
        Surf.ScaleMeters[1] = std::max(0.6f, P.ShellRadius);
        Surf.ScaleMeters[2] = std::max(0.6f, P.ShellRadius);
        Rig.Origin = std::move(Surf);
    }
    else if (OrigCat == ReleaseOriginCategory::FireworkPattern)
    {
        FireworkOrigin Fw {};
        Fw.Shape          = static_cast<FireworkShapeCategory>(P.FireworkShape % static_cast<uint8_t>(FireworkShapeCategory::Count));
        Fw.SkyTextMessage = std::string(P.FireworkText);
        for (int K = 0; K < 3; ++K) Fw.ShellOrigin[K] = P.OriginCenter[K];
        Fw.PatternRadius  = std::max(1.2f, P.ShellRadius);
        Rig.Origin = std::move(Fw);
    }
    else if (OrigCat == ReleaseOriginCategory::SphereVolume)
    {
        AnalyticSphereOrigin Sph {};
        for (int K = 0; K < 3; ++K) Sph.CenterMeters[K] = P.OriginCenter[K];
        Sph.RadiusMeters = std::max(0.15f, P.OriginRadius);
        Rig.Origin = Sph;
    }
    else if (OrigCat == ReleaseOriginCategory::BoxVolume)
    {
        AnalyticBoxOrigin Box {};
        for (int K = 0; K < 3; ++K) { Box.CenterMeters[K] = P.OriginCenter[K]; Box.HalfExtentsMeters[K] = std::max(0.15f, P.OriginHalf[K]); }
        Rig.Origin = Box;
    }
    else if (OrigCat == ReleaseOriginCategory::SurfaceLattice)
    {
        SurfaceLatticeOrigin Lat {};
        for (int K = 0; K < 3; ++K) Lat.CenterMeters[K] = P.OriginCenter[K];
        Rig.Origin = Lat;
    }
    else if (OrigCat == ReleaseOriginCategory::SpiderwebWeave)
    {
        SpiderwebOrigin Web {};
        for (int K = 0; K < 3; ++K) Web.CenterMeters[K] = P.OriginCenter[K];
        Rig.Origin = Web;
    }
    else if (OrigCat == ReleaseOriginCategory::ClothGrid)
    {
        ClothGridOrigin Grid {};
        for (int K = 0; K < 3; ++K) Grid.OriginMeters[K] = P.OriginCenter[K];
        Rig.Origin = Grid;
    }
    else if (OrigCat == ReleaseOriginCategory::BotanicalRoot)
    {
        BotanicalRootOrigin Bot {};
        for (int K = 0; K < 3; ++K) Bot.CenterMeters[K] = P.OriginCenter[K];
        Rig.Origin = Bot;
    }
    else
    {
        AnalyticPointOrigin Pt {};
        for (int K = 0; K < 3; ++K) Pt.CenterMeters[K] = P.OriginCenter[K];
        Rig.Origin = Pt;
    }

    // 2. Kinematics
    for (int K = 0; K < 3; ++K) Rig.Kinematics.LaunchDirection[K] = P.LaunchDir[K];
    Rig.Kinematics.SpreadRadians   = P.SpreadRad;
    Rig.Kinematics.SpeedMinMps     = P.SpeedMin;
    Rig.Kinematics.SpeedMaxMps     = P.SpeedMax;
    Rig.Kinematics.LifeMinSeconds  = std::max(0.2f, P.LifeMin);
    Rig.Kinematics.LifeMaxSeconds  = std::max(Rig.Kinematics.LifeMinSeconds, P.LifeMax);
    Rig.Kinematics.SizeBirthMeters = std::max(0.008f, P.SizeBirth);
    Rig.Kinematics.SizeDeathMeters = std::max(0.005f, P.SizeDeath);
    Rig.Kinematics.SizeMultiplier  = std::max(0.1f, P.SizeScale);

    // 3. Visual carrier
    if (P.CarrierCode == 1u)
    {
        RibbonStrandCarrier Rib {};
        Rib.VerticesPerStrand = 16u;
        Rig.Carrier = Rib;
    }
    else
    {
        SpriteBillboardCarrier Spr {};
        Spr.Cutout = static_cast<SpriteCutoutCategory>(P.SpriteCutout % static_cast<uint8_t>(SpriteCutoutCategory::Count));
        Rig.Carrier = Spr;
    }

    // 4. ColourSpace multi-stop gradient + scalar tint channel
    ColourSpace::MultiStopGradientSource Grad {};
    Grad.Stops = {
        { 0.00f, ColourSpace::LinearRgba { P.ColA[0],   P.ColA[1],   P.ColA[2],   P.ColA[3]   } },
        { 0.50f, ColourSpace::LinearRgba { P.ColMid[0], P.ColMid[1], P.ColMid[2], P.ColMid[3] } },
        { 1.00f, ColourSpace::LinearRgba { P.ColB[0],   P.ColB[1],   P.ColB[2],   P.ColB[3]   } },
    };
    Rig.ColourProfile.Source        = std::move(Grad);
    Rig.ColourProfile.BlendEquation = P.AdditiveBlend ? ColourSpace::OpticalBlendEquation::Additive : ColourSpace::OpticalBlendEquation::PremultipliedAlpha;
    Rig.TintDriver.Channel          = ParseScalarChannelKey(P.ColorByKey);

    // 5. Composable dynamics ensemble
    Rig.Dynamics.Ballistic.GravityScale       = P.GravityScale;
    Rig.Dynamics.Ballistic.AerodynamicDrag    = P.Drag;
    Rig.Dynamics.Ballistic.BuoyancyMps2       = P.Buoyancy;
    Rig.Dynamics.Ballistic.FloorRestitution   = P.Bounce;
    Rig.Dynamics.Ballistic.FlutterAmplitude   = P.Flutter;
    Rig.Dynamics.ForceCouple.WindCouplingRate = P.WindCoupling;

    if (P.CurlMode > 0u || P.DynamicsFamily == 1u)
    {
        CurlManifoldSpec Curl {};
        Curl.ManifoldMode    = (P.CurlMode > 0u) ? P.CurlMode : 1u;
        Curl.FrequencyPerM   = P.CurlFreq;
        Curl.DriveSpeedMps   = P.CurlAmp;
        Curl.EvolutionRateHz = P.CurlRate;
        Curl.ShellRadiusM    = P.ShellRadius;
        Rig.Dynamics.Curl    = Curl;
    }
    switch (P.DynamicsFamily)
    {
        case 2u:  Rig.Dynamics.Swarm         = SwarmFlockingSpec {}; break;
        case 3u:  Rig.Dynamics.SpeciesLife   = SpeciesLifeSpec {}; break;
        case 4u:  Rig.Dynamics.Orbital       = OrbitalGravitySpec {}; break;
        case 5u:  Rig.Dynamics.Electro       = ElectromagneticSpec {}; break;
        case 6u:  Rig.Dynamics.Harmonic      = HarmonicWaveSpec {}; break;
        case 7u:  Rig.Dynamics.Attractor     = AttractorDynamicsSpec {}; break;
        case 8u:  Rig.Dynamics.FluidSph      = FluidSphSpec {}; break;
        case 9u:  Rig.Dynamics.Granular      = GranularCollisionSpec {}; break;
        case 10u: Rig.Dynamics.ElasticStrand = ElasticStrandSpec {}; break;
        case 11u: Rig.Dynamics.Thermal       = ThermalFireworkSpec {}; break;
        case 12u: Rig.Dynamics.DielectricArc = DielectricArcSpec {}; break;
        case 13u: Rig.Dynamics.FieldMatrix   = FieldMatrixSpec {}; break;
        case 14u: Rig.Dynamics.Botanical     = BotanicalSwaySpec {}; break;
        case 15u: Rig.Dynamics.Morpho        = MorphogeneticGrowthSpec {}; break;
        default:  break;
    }

    // 6. Specialized domain solver & geometric 3D origin activations keyed by canonical preset identity
    const std::string_view Pid(P.Identifier);
    if (Pid == "ferrofluid-spikes" || Pid == "ferrofluid-orb")
    {
        FerrofluidMeniscusSpec Ferro {};
        Ferro.LevitatedCrown = (Pid == "ferrofluid-orb");
        Rig.Dynamics.Ferrofluid = Ferro;
        if (Ferro.LevitatedCrown)
        {
            SurfaceAnchorOrigin Orb {};
            Orb.Preset = SurfacePresetCategory::ObsidianSphere;
            for (int K = 0; K < 3; ++K) { Orb.CenterMeters[K] = P.OriginCenter[K]; Orb.ScaleMeters[K] = 1.35f; }
            Rig.Origin = Orb;
        }
        else
        {
            AnalyticDiscOrigin Dish {};
            for (int K = 0; K < 3; ++K) Dish.CenterMeters[K] = P.OriginCenter[K];
            Dish.RadiusMeters = 1.85f;
            Rig.Origin = Dish;
        }
        Rig.TintDriver.Channel = ParticleScalarChannel::WorldElevation;
        Rig.ColourProfile.Range.RangeMin = P.OriginCenter[1];
        Rig.ColourProfile.Range.RangeMax = P.OriginCenter[1] + 0.95f;
        ColourSpace::MultiStopGradientSource SpecGrad {};
        SpecGrad.Stops = {
            { 0.00f, ColourSpace::LinearRgba { 0.09f, 0.13f, 0.20f, 0.95f } },
            { 0.55f, ColourSpace::LinearRgba { 0.36f, 0.54f, 0.72f, 0.98f } },
            { 1.00f, ColourSpace::LinearRgba { 0.88f, 0.95f, 1.00f, 1.00f } },
        };
        Rig.ColourProfile.Source = std::move(SpecGrad);
    }
    else if (Pid == "cracked-ice-sheet" || Pid == "ground-crack-quake")
    {
        FractureFissureSpec Frac {};
        Frac.SeismicBasalt = (Pid == "ground-crack-quake");
        Rig.Dynamics.Fracture = Frac;
        AnalyticDiscOrigin Slab {};
        for (int K = 0; K < 3; ++K) Slab.CenterMeters[K] = P.OriginCenter[K];
        Slab.RadiusMeters = 2.65f;
        Rig.Origin = Slab;
    }
    else if (Pid == "car-puddle-splash" || Pid == "puddle-ripple-splash" || Pid == "mud-splatter-heavy" || Pid == "splatter-burst")
    {
        ShallowWaterSplashSpec Spl {};
        if (Pid == "car-puddle-splash")      Spl.SplashVariant = 2u;
        else if (Pid == "mud-splatter-heavy" || Pid == "splatter-burst") Spl.SplashVariant = 3u;
        else                                 Spl.SplashVariant = 1u;
        Rig.Dynamics.Splash = Spl;
        AnalyticDiscOrigin Puddle {};
        for (int K = 0; K < 3; ++K) Puddle.CenterMeters[K] = P.OriginCenter[K];
        Puddle.RadiusMeters = 2.35f;
        Rig.Origin = Puddle;
    }
    else if (Pid == "derez-cube" || Pid == "cube-coins" || Pid == "melt-away" || Pid == "reverse-derez"
          || Pid == "voxel-explosion" || Pid == "coin-stack" || Pid == "dissolve-nothing"
          || Pid == "dissolve-butterflies" || Pid == "glitch-dissolve" || Pid == "hologram-data-glitch")
    {
        TransitionDerezSpec Tr {};
        if (Pid == "derez-cube")               Tr.TransitionVariant = 1u;
        else if (Pid == "coin-stack")          Tr.TransitionVariant = 2u;
        else if (Pid == "melt-away" || Pid == "dissolve-nothing") Tr.TransitionVariant = 3u;
        else if (Pid == "reverse-derez")       Tr.TransitionVariant = 4u;
        else if (Pid == "voxel-explosion")     Tr.TransitionVariant = 5u;
        else if (Pid == "dissolve-butterflies")Tr.TransitionVariant = 6u;
        else if (Pid == "glitch-dissolve" || Pid == "hologram-data-glitch") Tr.TransitionVariant = 7u;
        Rig.Dynamics.Transition = Tr;
        SurfaceAnchorOrigin Cube {};
        Cube.Preset = SurfacePresetCategory::CubicVolume;
        for (int K = 0; K < 3; ++K) { Cube.CenterMeters[K] = P.OriginCenter[K]; Cube.ScaleMeters[K] = 1.45f; }
        Rig.Origin = Cube;
    }
    else if (Pid == "energy-shield-hex" || Pid == "energy-shield-prism" || Pid == "emp-pulse-dome")
    {
        ShieldBarrierSpec Sh {};
        Sh.Form = (Pid == "energy-shield-prism") ? ShieldBarrierForm::HexagonWall : ShieldBarrierForm::SphereDome;
        Rig.Dynamics.Shield = Sh;
        SurfaceAnchorOrigin Dome {};
        Dome.Preset = SurfacePresetCategory::ObsidianSphere;
        for (int K = 0; K < 3; ++K) { Dome.CenterMeters[K] = P.OriginCenter[K]; Dome.ScaleMeters[K] = 1.85f; }
        Rig.Origin = Dome;
    }
    else if (Pid == "spiderweb-orb" || Pid == "spiderweb-cobweb")
    {
        SpiderwebWeaveSpec Web {};
        Web.MessyAtticCobweb = (Pid == "spiderweb-cobweb");
        Rig.Dynamics.Spiderweb = Web;
        SpiderwebOrigin WebOrig {};
        for (int K = 0; K < 3; ++K) WebOrig.CenterMeters[K] = P.OriginCenter[K];
        WebOrig.MessyCobweb = Web.MessyAtticCobweb;
        Rig.Origin = WebOrig;
    }
    else if (Pid.rfind("effector-", 0u) == 0u)
    {
        EffectorLatticeSpec Eff {};
        if (Pid == "effector-sphere-dots")       Eff.Driver = EffectorDriverCategory::OrbitingDual;
        else if (Pid == "effector-torus-hex")    Eff.Driver = EffectorDriverCategory::ExpandingPulseRing;
        else if (Pid == "effector-object-placer")Eff.Driver = EffectorDriverCategory::Placeable3DEffectors;
        else                                     Eff.Driver = EffectorDriverCategory::FluidSimplexWave;
        Rig.Dynamics.Effector = Eff;
        SurfaceLatticeOrigin Lat {};
        Lat.Columns = 24u;
        Lat.Rows    = 24u;
        for (int K = 0; K < 3; ++K) Lat.CenterMeters[K] = P.OriginCenter[K];
        Lat.HalfExtentsMeters[0] = 2.6f;
        Lat.HalfExtentsMeters[2] = 2.6f;
        Rig.Origin = Lat;
        Rig.TintDriver.Channel = ParticleScalarChannel::WorldElevation;
        Rig.ColourProfile.Range.RangeMin = P.OriginCenter[1];
        Rig.ColourProfile.Range.RangeMax = P.OriginCenter[1] + 1.0f;
        ColourSpace::MultiStopGradientSource EffGrad {};
        EffGrad.Stops = {
            { 0.00f, ColourSpace::LinearRgba { 0.14f, 0.24f, 0.46f, 0.92f } },
            { 0.50f, ColourSpace::LinearRgba { 0.24f, 0.72f, 0.96f, 0.96f } },
            { 1.00f, ColourSpace::LinearRgba { 0.95f, 0.88f, 0.42f, 1.00f } },
        };
        Rig.ColourProfile.Source = std::move(EffGrad);
    }
    else if (Pid == "black-hole")
    {
        AnalyticDiscOrigin Disc {};
        for (int K = 0; K < 3; ++K) Disc.CenterMeters[K] = P.OriginCenter[K];
        Disc.RadiusMeters = 2.65f;
        Rig.Origin = Disc;
        OrbitalGravitySpec Orb {};
        Orb.CentralMassParam = 8.5f;
        Rig.Dynamics.Orbital = Orb;
    }
    else if (Pid == "flag-banner" || Pid == "ship-sail" || Pid == "velvet-theater-drapes" || Pid == "beaded-door-curtain")
    {
        ClothGridOrigin Cloth {};
        for (int K = 0; K < 3; ++K) Cloth.OriginMeters[K] = P.OriginCenter[K];
        Rig.Origin = Cloth;
        if (!Rig.Dynamics.ElasticStrand.has_value()) Rig.Dynamics.ElasticStrand = ElasticStrandSpec {};
    }
    else if (Pid == "interactive-tree" || Pid == "grass-wheat-field" || Pid == "tall-reeds" || Pid == "interactive-fern" || Pid == "kelp-forest" || Pid == "sea-anemone")
    {
        BotanicalRootOrigin Bot {};
        for (int K = 0; K < 3; ++K) Bot.CenterMeters[K] = P.OriginCenter[K];
        Rig.Origin = Bot;
        if (!Rig.Dynamics.Botanical.has_value()) Rig.Dynamics.Botanical = BotanicalSwaySpec {};
    }
    else if (Pid == "brownian" || Pid == "diffusion" || Pid == "crystallization" || Pid == "reaction")
    {
        MolecularKineticsSpec Mol {};
        Mol.ChemicalReaction = (Pid == "reaction");
        Rig.Dynamics.Molecular = Mol;
    }
    return Rig;
}

ParticleDocument InstantiateDocumentFromPresetId(std::string_view Identifier)
{
    ParticleDocument Doc = ConstructStudioDefaultDocument();
    if (const auto* Preset = FindParticlePresetById(Identifier))
    {
        Doc.DocumentTitle  = std::string(Preset->DisplayName);
        Doc.ActivePresetId = std::string(Preset->Identifier);
        Doc.Rigs.clear();
        Doc.Rigs.push_back(InstantiateRigFromPreset(*Preset));
    }
    return Doc;
}

} // namespace Frontier::ParticleSpace
