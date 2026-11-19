/*============================================================================================================================================
                                                            FLUIDPRESETS.CPP
============================================================================================================================================*/
// 🧩 Built-in Fire/Smoke/Explosion presets & 6 physical/stylized blackbody palettes

#include "Engine/SimulationSpace/FluidSpace/FluidPresets.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace Frontier::SimulationSpace::FluidSpace
{

namespace
{

constexpr std::array<FluidPresetEntry, 12> BuiltInPresets {{
    { "ue5_pyro_default",    "Pyro plume",          "fire",   "Rolling flame · dark soot" },
    { "oil_well_inferno",    "Oil well inferno",    "fire",   "Pressurised fuel column" },
    { "shrapnel_airburst",   "Shrapnel airburst",   "blast",  "Ballistic smoke streamers" },
    { "megaton_open_bounds", "Wide-bounds blast",   "blast",  "Large expanding fireball (uncapped headroom)" },
    { "wildfire_tornado",    "Fire tornado",        "fire",   "High-swirl flame vortex + debris" },
    { "dust_tornado",        "Dust tornado",        "dust",   "Swirling dust funnel + flying debris" },
    { "camp_fire_steady",    "Camp fire steady",    "fire",   "Warm flickering hearth plume" },
    { "cold_dust_fall",      "Cold dust fall",      "dust",   "Heavy settling mineral dust cloud" },
    { "deflector_obstacle",  "Deflector obstacle",  "smoke",  "Flow wrapping around solid obstacle" },
    { "far_cheap_plume",     "Far cheap plume",     "smoke",  "Distant world-drifting smoke column" },
    { "tyre_burnout_ring",   "Tyre burnout ring",   "smoke",  "Toroidal collar smoke & embers" },
    { "plasma_reactor_arc",  "Cherenkov plasma",    "plasma", "High-energy blue-violet ionized plume" },
}};

[[nodiscard]] float SmoothStepF(float Edge0, float Edge1, float X) noexcept
{
    const float T = std::clamp((X - Edge0) / std::max(1.0e-5f, Edge1 - Edge0), 0.0f, 1.0f);
    return T * T * (3.0f - 2.0f * T);
}

[[nodiscard]] Mathematics::LinearRGB MixRgb(
    const Mathematics::LinearRGB& A,
    const Mathematics::LinearRGB& B,
    float                         T) noexcept
{
    const float S = std::clamp(T, 0.0f, 1.0f);
    return {
        A.R + (B.R - A.R) * S,
        A.G + (B.G - A.G) * S,
        A.B + (B.B - A.B) * S,
    };
}

} // namespace

std::span<const FluidPresetEntry> FluidPresets::ListPresets() noexcept
{
    return BuiltInPresets;
}

FluidSceneDocument FluidPresets::ConstructPreset(std::string_view PresetKey) noexcept
{
    FluidSceneDocument Doc {};
    Doc.PresetKey = std::string(PresetKey);

    if (PresetKey == "oil_well_inferno")
    {
        Doc.SceneName                       = "Oil well inferno";
        Doc.Combustion.BurnRate             = 2.3f;
        Doc.Combustion.Buoyancy             = 7.8f;
        Doc.Combustion.SootGeneration       = 2.6f;
        Doc.Combustion.CombustionExpansion  = 5.6f;
        Doc.Emitter.UpwardVelocityMps       = 6.4f;
        Doc.Emitter.Fuel                    = 4.8f;
        Doc.Emitter.Temperature             = 5.2f;
        Doc.Shading.ColorPalette            = FluidColorScheme::CinemaNapalm;
        Doc.Shading.FireIntensity           = 6.4f;
        Doc.Shading.DensityExtinction       = 22.0f;
    }
    else if (PresetKey == "shrapnel_airburst")
    {
        Doc.SceneName                       = "Shrapnel airburst";
        Doc.Emitter.Enabled                 = false;
        Doc.Blast.StrengthMps               = 12.0f;
        Doc.Blast.RadiusMeters              = 0.16f;
        Doc.Blast.Temperature               = 6.0f;
        Doc.Blast.Fuel                      = 4.8f;
        Doc.Blast.Smoke                     = 2.4f;
        Doc.Blast.Lobes                     = 7.0f;
        Doc.Particles.ShrapnelEnabled       = true;
        Doc.Particles.EmberCount            = 650u;
        Doc.Domain.DynamicBounds            = true;
        Doc.Domain.AdaptiveCeilingEnabled   = true;
        Doc.Shading.ColorPalette            = FluidColorScheme::ThermobaricWhiteHot;
    }
    else if (PresetKey == "megaton_open_bounds")
    {
        Doc.SceneName                       = "Wide-bounds blast";
        Doc.Emitter.Enabled                 = false;
        Doc.Domain.GridResolution           = 96u;
        Doc.Domain.BoundsWidthMeters        = 3.9f;
        Doc.Domain.BoundsHeightMeters       = 4.0f;
        Doc.Domain.DynamicBounds            = true;
        Doc.Domain.AdaptiveCeilingEnabled   = true;
        Doc.Domain.MaxExplosionCeilingMeters = 28.0f;
        Doc.Blast.CenterWorld               = { 0.0f, 0.0f, 0.45f };
        Doc.Blast.RadiusMeters              = 0.20f;
        Doc.Blast.StrengthMps               = 11.5f;
        Doc.Blast.Temperature               = 6.4f;
        Doc.Blast.Fuel                      = 5.2f;
        Doc.Blast.Smoke                     = 2.8f;
        Doc.Blast.Lobes                     = 6.0f;
        Doc.Combustion.Buoyancy             = 6.2f;
        Doc.Combustion.CombustionExpansion  = 5.4f;
        Doc.Combustion.BurnRate             = 2.2f;
        Doc.Combustion.SootGeneration       = 2.2f;
        Doc.Combustion.CoolingRate          = 2.05f;
        Doc.Shading.FireIntensity           = 6.2f;
        Doc.Shading.DensityExtinction       = 20.0f;
    }
    else if (PresetKey == "camp_fire_steady")
    {
        Doc.SceneName                       = "Camp fire steady";
        Doc.Domain.BoundsWidthMeters        = 1.4f;
        Doc.Domain.BoundsHeightMeters       = 1.8f;
        Doc.Emitter.RadiusMeters            = 0.24f;
        Doc.Emitter.UpwardVelocityMps       = 2.6f;
        Doc.Emitter.Swirl                   = 1.8f;
        Doc.Emitter.Temperature             = 3.8f;
        Doc.Emitter.Fuel                    = 2.8f;
        Doc.Emitter.Smoke                   = 0.65f;
        Doc.Combustion.Buoyancy             = 4.6f;
        Doc.Combustion.SootGeneration       = 0.85f;
        Doc.Shading.FireIntensity           = 5.8f;
        Doc.Particles.EmberCount            = 320u;
    }
    else if (PresetKey == "cold_dust_fall")
    {
        Doc.SceneName                       = "Cold dust fall";
        Doc.Emitter.HeightMeters            = 1.45f;
        Doc.Emitter.UpwardVelocityMps       = -0.4f;
        Doc.Emitter.Temperature             = 0.0f;
        Doc.Emitter.Fuel                    = 0.0f;
        Doc.Emitter.Smoke                   = 3.8f;
        Doc.Combustion.Buoyancy             = 0.4f;
        Doc.Combustion.SmokeWeight          = 3.8f;
        Doc.Combustion.WindVelocityXY       = { 0.85f, 0.15f };
        Doc.Shading.SmokeAlbedo             = 0.58f;
        Doc.Shading.DensityExtinction       = 14.0f;
        Doc.Particles.ShowEmbers            = false;
    }
    else if (PresetKey == "deflector_obstacle")
    {
        Doc.SceneName                       = "Deflector obstacle";
        Doc.Obstacle.Type                   = FluidObstacleType::DeflectorSlab;
        Doc.Obstacle.CenterMeters           = { 0.0f, 0.0f, 0.55f };
        Doc.Obstacle.RadiusMeters           = 0.24f;
        Doc.Emitter.UpwardVelocityMps       = 5.2f;
        Doc.Combustion.VorticityConfinement = 9.0f;
    }
    else if (PresetKey == "far_cheap_plume")
    {
        Doc.SceneName                       = "Far cheap plume";
        Doc.Domain.GridResolution           = 32u;
        Doc.Domain.WorldGridEnabled         = true;
        Doc.Domain.WorldGridSpanMeters      = 32.0f;
        Doc.Combustion.WindVelocityXY       = { 2.4f, 0.6f };
        Doc.Combustion.SmokeDissipation     = 0.08f;
        Doc.Shading.RaymarchSteps           = 44u;
    }
    else if (PresetKey == "tyre_burnout_ring")
    {
        Doc.SceneName                       = "Tyre burnout ring";
        Doc.Obstacle.Type                   = FluidObstacleType::TyreRing;
        Doc.Obstacle.CenterMeters           = { 0.0f, 0.0f, 0.28f };
        Doc.Obstacle.RadiusMeters           = 0.26f;
        Doc.Emitter.Fuel                    = 0.4f;
        Doc.Emitter.Smoke                   = 4.6f;
        Doc.Emitter.Swirl                   = 4.8f;
        Doc.Shading.SmokeAlbedo             = 0.14f;
        Doc.Shading.DensityExtinction       = 24.0f;
    }
    else if (PresetKey == "plasma_reactor_arc")
    {
        Doc.SceneName                       = "Cherenkov plasma";
        Doc.Shading.ColorPalette            = FluidColorScheme::CherenkovBluePlasma;
        Doc.Shading.FireIntensity           = 6.4f;
        Doc.Emitter.Temperature             = 5.2f;
        Doc.Emitter.Smoke                   = 0.45f;
        Doc.Combustion.SootGeneration       = 0.40f;
        Doc.Combustion.VorticityConfinement = 10.5f;
    }
    else if (PresetKey == "wildfire_tornado")
    {
        Doc.SceneName                       = "Fire tornado";
        Doc.Domain.BoundsWidthMeters        = 2.3f;
        Doc.Domain.BoundsHeightMeters       = 3.2f;
        Doc.Domain.DynamicBounds            = false;
        Doc.Emitter.RadiusMeters            = 0.135f;
        Doc.Emitter.HeightMeters            = 0.08f;
        Doc.Emitter.UpwardVelocityMps       = 4.6f;
        Doc.Emitter.Swirl                   = 6.4f;
        Doc.Emitter.Temperature             = 3.6f;
        Doc.Emitter.Fuel                    = 2.8f;
        Doc.Emitter.Smoke                   = 1.45f;
        Doc.Combustion.VorticityConfinement = 10.6f;
        Doc.Combustion.Buoyancy             = 6.2f;
        Doc.Combustion.SmokeWeight          = 0.55f;
        Doc.Combustion.TurbulenceStrength   = 3.8f;
        Doc.Combustion.SootGeneration       = 1.85f;
        Doc.Combustion.CoolingRate          = 1.95f;
        Doc.Combustion.WindVelocityXY       = { 0.06f, 0.0f };
        Doc.Shading.ColorPalette            = FluidColorScheme::CinemaNapalm;
        Doc.Shading.FireIntensity           = 5.2f;
        Doc.Shading.DensityExtinction       = 18.5f;
        Doc.Particles.EmberCount            = 480u;
        Doc.Particles.EmberLifetime         = 1.9f;
        Doc.Particles.EmberAshiness         = 0.25f;
        Doc.Particles.EnableTornadoDebrisArray = true;
        Doc.Particles.TornadoDebrisRatio    = 0.52f;
        Doc.Particles.DebrisTypes           = {
            FluidDebrisCategory::RockChunk,
            FluidDebrisCategory::TwigStick,
            FluidDebrisCategory::Leaf,
            FluidDebrisCategory::PaperSheet,
            FluidDebrisCategory::WoodSplinter,
            FluidDebrisCategory::MetalScrap,
        };
    }
    else if (PresetKey == "dust_tornado")
    {
        Doc.SceneName                       = "Dust tornado";
        Doc.Domain.BoundsWidthMeters        = 2.3f;
        Doc.Domain.BoundsHeightMeters       = 3.4f;
        Doc.Domain.DynamicBounds            = false;
        Doc.Emitter.RadiusMeters            = 0.21f;
        Doc.Emitter.HeightMeters            = 0.08f;
        Doc.Emitter.UpwardVelocityMps       = 5.2f;
        Doc.Emitter.Swirl                   = 6.8f;
        Doc.Emitter.Temperature             = 0.20f;
        Doc.Emitter.Fuel                    = 0.0f;
        Doc.Emitter.Smoke                   = 3.4f;
        Doc.Combustion.VorticityConfinement = 11.2f;
        Doc.Combustion.Buoyancy             = 2.2f;
        Doc.Combustion.SmokeWeight          = 0.18f;
        Doc.Combustion.BurnRate             = 0.1f;
        Doc.Combustion.CombustionExpansion  = 0.0f;
        Doc.Combustion.CoolingRate          = 3.2f;
        Doc.Combustion.SmokeDissipation     = 0.06f;
        Doc.Combustion.VelocityDamping      = 0.04f;
        Doc.Combustion.TurbulenceStrength   = 2.6f;
        Doc.Combustion.TurbulenceScale      = 3.0f;
        Doc.Combustion.WindVelocityXY       = { 0.08f, 0.02f };
        Doc.Shading.DensityExtinction       = 14.5f;
        Doc.Shading.SmokeAlbedo             = 0.68f;
        Doc.Shading.ShadowDensity           = 7.0f;
        Doc.Shading.FireIntensity           = 0.20f;
        Doc.Shading.InternalScattering      = 0.5f;
        Doc.Particles.ShowEmbers            = true;
        Doc.Particles.EmberCount            = 520u;
        Doc.Particles.EmberSize             = 1.05f;
        Doc.Particles.EmberIntensity        = 0.25f;
        Doc.Particles.EmberLifetime         = 2.3f;
        Doc.Particles.EmberAshiness         = 1.0f;
        Doc.Particles.EnableTornadoDebrisArray = true;
        Doc.Particles.TornadoDebrisRatio    = 0.85f;
        Doc.Particles.DebrisTypes           = {
            FluidDebrisCategory::RockChunk,
            FluidDebrisCategory::TwigStick,
            FluidDebrisCategory::Leaf,
            FluidDebrisCategory::PaperSheet,
            FluidDebrisCategory::WoodSplinter,
            FluidDebrisCategory::MetalScrap,
        };
    }
    else
    {
        Doc.SceneName = "Pyro plume";
        Doc.PresetKey = "ue5_pyro_default";
    }

    return Doc;
}

Mathematics::LinearRGB FluidPresets::EvaluateBlackbodyPalette(
    float             RawTemperature,
    float             TemperatureScale,
    FluidColorScheme Palette) noexcept
{
    // Exact 1:1 port of Experimental/FluidEditor/src/shaders-wgsl.js :: evaluateBlackbodyPalette
    const float T = std::clamp(RawTemperature * TemperatureScale * 0.28f, 0.0f, 1.60f);
    if (T <= 0.01f)
    {
        return {};
    }

    Mathematics::LinearRGB Col {};
    float Gain = T * T * 1.65f;
    switch (Palette)
    {
        case FluidColorScheme::CinemaNapalm:
        {
            Col = MixRgb({ 0.0f, 0.0f, 0.0f }, { 0.65f, 0.02f, 0.002f }, SmoothStepF(0.0f, 0.25f, T));
            Col = MixRgb(Col, { 1.00f, 0.16f, 0.005f }, SmoothStepF(0.20f, 0.62f, T));
            Col = MixRgb(Col, { 1.00f, 0.56f, 0.06f }, SmoothStepF(0.58f, 1.02f, T));
            Col = MixRgb(Col, { 1.00f, 0.90f, 0.48f }, SmoothStepF(0.95f, 1.48f, T));
            break;
        }
        case FluidColorScheme::ThermobaricWhiteHot:
        {
            Col = MixRgb({ 0.0f, 0.0f, 0.0f }, { 0.72f, 0.12f, 0.01f }, SmoothStepF(0.0f, 0.18f, T));
            Col = MixRgb(Col, { 1.00f, 0.45f, 0.08f }, SmoothStepF(0.15f, 0.42f, T));
            Col = MixRgb(Col, { 1.00f, 0.90f, 0.55f }, SmoothStepF(0.38f, 0.75f, T));
            Col = MixRgb(Col, { 1.25f, 1.22f, 1.35f }, SmoothStepF(0.70f, 1.22f, T));
            Gain = T * T * 2.10f;
            break;
        }
        case FluidColorScheme::WildfireCopperEmerald:
        {
            Col = MixRgb({ 0.0f, 0.0f, 0.0f }, { 0.03f, 0.35f, 0.14f }, SmoothStepF(0.0f, 0.22f, T));
            Col = MixRgb(Col, { 0.10f, 0.92f, 0.38f }, SmoothStepF(0.18f, 0.54f, T));
            Col = MixRgb(Col, { 0.62f, 1.00f, 0.52f }, SmoothStepF(0.50f, 0.90f, T));
            Col = MixRgb(Col, { 0.95f, 1.00f, 0.92f }, SmoothStepF(0.84f, 1.36f, T));
            Gain = T * T * 1.70f;
            break;
        }
        case FluidColorScheme::CherenkovBluePlasma:
        {
            Col = MixRgb({ 0.0f, 0.0f, 0.0f }, { 0.04f, 0.10f, 0.62f }, SmoothStepF(0.0f, 0.20f, T));
            Col = MixRgb(Col, { 0.12f, 0.52f, 1.00f }, SmoothStepF(0.16f, 0.50f, T));
            Col = MixRgb(Col, { 0.58f, 0.88f, 1.00f }, SmoothStepF(0.46f, 0.88f, T));
            Col = MixRgb(Col, { 0.94f, 0.98f, 1.08f }, SmoothStepF(0.82f, 1.35f, T));
            Gain = T * T * 1.85f;
            break;
        }
        case FluidColorScheme::ArcaneVoidMagenta:
        {
            Col = MixRgb({ 0.0f, 0.0f, 0.0f }, { 0.38f, 0.02f, 0.48f }, SmoothStepF(0.0f, 0.20f, T));
            Col = MixRgb(Col, { 0.88f, 0.14f, 0.98f }, SmoothStepF(0.16f, 0.52f, T));
            Col = MixRgb(Col, { 1.00f, 0.52f, 0.98f }, SmoothStepF(0.48f, 0.90f, T));
            Col = MixRgb(Col, { 1.00f, 0.92f, 1.00f }, SmoothStepF(0.84f, 1.36f, T));
            Gain = T * T * 1.75f;
            break;
        }
        case FluidColorScheme::PlanckianBlackbody:
        default:
        {
            Col = MixRgb({ 0.0f, 0.0f, 0.0f }, { 0.55f, 0.03f, 0.005f }, SmoothStepF(0.0f, 0.22f, T));
            Col = MixRgb(Col, { 1.00f, 0.24f, 0.01f }, SmoothStepF(0.18f, 0.52f, T));
            Col = MixRgb(Col, { 1.00f, 0.68f, 0.10f }, SmoothStepF(0.48f, 0.88f, T));
            Col = MixRgb(Col, { 1.00f, 0.96f, 0.78f }, SmoothStepF(0.82f, 1.35f, T));
            break;
        }
    }

    return { Col.R * Gain, Col.G * Gain, Col.B * Gain };
}

std::string_view FluidPresets::QueryPaletteLabel(FluidColorScheme Palette) noexcept
{
    switch (Palette)
    {
        case FluidColorScheme::CinemaNapalm:          return "Cinema Napalm";
        case FluidColorScheme::ThermobaricWhiteHot:   return "Thermobaric White-Hot";
        case FluidColorScheme::WildfireCopperEmerald: return "Wildfire Copper Emerald";
        case FluidColorScheme::CherenkovBluePlasma:   return "Cherenkov Blue Plasma";
        case FluidColorScheme::ArcaneVoidMagenta:     return "Arcane Void Magenta";
        case FluidColorScheme::PlanckianBlackbody:
        default:                                       return "Planckian Blackbody";
    }
}

std::string_view FluidPresets::QueryObstacleLabel(FluidObstacleType Type) noexcept
{
    switch (Type)
    {
        case FluidObstacleType::Sphere:             return "Sphere";
        case FluidObstacleType::VerticalCylinder:   return "Vertical Cylinder";
        case FluidObstacleType::HorizontalCylinder: return "Horizontal Cylinder";
        case FluidObstacleType::DeflectorSlab:      return "Deflector Slab";
        case FluidObstacleType::TyreRing:           return "Tyre Ring";
        case FluidObstacleType::None:
        default:                                    return "None";
    }
}

std::string_view FluidPresets::QueryChannelLabel(FluidRenderChannel Channel) noexcept
{
    switch (Channel)
    {
        case FluidRenderChannel::VoxelDda:           return "Voxel DDA";
        case FluidRenderChannel::SmokeDensity:       return "Smoke Density";
        case FluidRenderChannel::Temperature:        return "Temperature";
        case FluidRenderChannel::Fuel:               return "Fuel";
        case FluidRenderChannel::Velocity:           return "Velocity";
        case FluidRenderChannel::Vorticity:          return "Vorticity";
        case FluidRenderChannel::PressureIrradiance: return "Pressure / Irradiance";
        case FluidRenderChannel::CrossSection:       return "Cross-Section";
        case FluidRenderChannel::LitVolume:
        default:                                     return "Lit Volume";
    }
}

} // namespace Frontier::SimulationSpace::FluidSpace
