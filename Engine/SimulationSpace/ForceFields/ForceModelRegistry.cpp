/*============================================================================================================================================
                                                         FORCEMODELREGISTRY.CPP
============================================================================================================================================*/
// 🧩 Authoritative 20-model analytic force registry implementation (1:1 ForceGeneratorCatalog.js L1-L458)
// Reference: Experimental/FluidEditor/Engine/ForceGeneratorCatalog.js L1-L458

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/SimulationSpace/ForceFields/ForceModelRegistry.h"

#include <array>

namespace Frontier::SimulationSpace::ForceFields
{

namespace
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    STATIC REGISTRY TABLES
//------------------------------------------------------------------------------------------------------------------------

// Reference: Experimental/FluidEditor/Engine/ForceGeneratorCatalog.js L13-L21
inline constexpr std::array<std::string_view, kForceFalloffCount> kFalloffTokens =
{
    "SmoothStep",
    "Linear",
    "InverseSquare",
    "Constant",
    "Gaussian",
    "Plateau",
    "Cosine",
};

inline constexpr std::array<std::string_view, kForceFalloffCount> kFalloffLabels =
{
    "Smooth",
    "Linear",
    "Inverse\xC2\xB2",
    "Infinite",
    "Gaussian",
    "Plateau",
    "Interpolated",
};

inline constexpr std::array<std::string_view, kForceCategoryCount> kForceCategoryNames =
{
    "Directional",
    "Attraction",
    "Rotational",
    "Turbulence",
    "Burst",
    "Damping",
    "Thermal",
};

inline constexpr std::array<std::string_view, 3u> kReleaseSourceShapeTokens =
{
    "Sphere",
    "Disc",
    "Box",
};

inline constexpr std::array<std::string_view, 3u> kFluidBoundaryTokens =
{
    "ClosedBox",
    "OpenTop",
    "OpenAll",
};

// Reference: Experimental/FluidEditor/Engine/ForceGeneratorCatalog.js L29-L176
// Order preserved 1:1 with ForceGeneratorTypes; GasVelocitySaturated matches FluidShaders.js L709:
//   limited = !(type == 5 || type == 6 || type == 11 || type == 12 || type == 13 || type == 14 || type == 16)
inline constexpr std::array<ForceModelDescriptor, kForceModelCount> kForceModelDescriptors =
{{
    // ---------------------------------------------------------------- Directional
    {
        "DirectionalWind", ForceModelOrdinal::DirectionalWind, ForceCategoryOrdinal::Directional, "Directional",
        "Directional Wind", "wind", "#38bdf8",
        "Uniform directional push across the whole domain or a bounded reach, with optional gusting.",
        { 6.0f, 2.0f, { 1.0f, 0.0f, 0.0f }, ForceFalloffOrdinal::Constant, 0.6f, 0.35f, 0.0f },
        { "Gust Rate", "Gust Depth", {} },
        true,
    },
    {
        "WindShear", ForceModelOrdinal::WindShear, ForceCategoryOrdinal::Directional, "Directional",
        "Wind Shear", "shear", "#60a5fa",
        "Crosswind whose heading rotates with height along the axis \xE2\x80\x94 layered, twisting flow.",
        { 5.0f, 2.0f, { 0.0f, 1.0f, 0.0f }, ForceFalloffOrdinal::Constant, 0.0f, 1.0f, 0.0f },
        { {}, "Twist (half-turns)", {} },
        true,
    },
    {
        "FlowStream", ForceModelOrdinal::DirectedStream, ForceCategoryOrdinal::Directional, "Directional",
        "Flow Stream", "stream", "#7dd3fc",
        "Steers gas toward a target velocity along the axis \xE2\x80\x94 a soft conveyor rather than a push.",
        { 3.0f, 1.2f, { 1.0f, 0.0f, 0.0f }, ForceFalloffOrdinal::SmoothStep, 2.5f, 0.0f, 0.0f },
        { "Target Speed", {}, {} },
        false,
    },
    {
        "DirectionalBurst", ForceModelOrdinal::DirectionalBurst, ForceCategoryOrdinal::Burst, "Burst",
        "Directional Burst", "dburst", "#fb923c",
        "Periodic impulse along one axis \xE2\x80\x94 cannon shots, piston puffs, muzzle blasts.",
        { 45.0f, 0.9f, { 0.0f, 1.0f, 0.0f }, ForceFalloffOrdinal::SmoothStep, 1.5f, 0.1f, 0.0f },
        { "Pulse Period", "Pulse Width", "Start Delay" },
        false,
    },
    // ---------------------------------------------------------------- Attraction
    {
        "PointAttractor", ForceModelOrdinal::PointAttractor, ForceCategoryOrdinal::Attraction, "Attraction",
        "Point Attractor", "attractor", "#a78bfa",
        "Pulls fluid toward a centre; negative strength repels.",
        { 14.0f, 1.4f, { 0.0f, 1.0f, 0.0f }, ForceFalloffOrdinal::SmoothStep, 0.0f, 0.1f, 0.0f },
        { {}, "Core Softness", {} },
        true,
    },
    {
        "PointRepulsor", ForceModelOrdinal::PointRepulsor, ForceCategoryOrdinal::Attraction, "Attraction",
        "Point Repulsor", "repulsor", "#f472b6",
        "Pushes fluid away from a centre in all directions.",
        { 14.0f, 1.2f, { 0.0f, 1.0f, 0.0f }, ForceFalloffOrdinal::SmoothStep, 0.0f, 0.1f, 0.0f },
        { {}, "Core Softness", {} },
        true,
    },
    {
        "AxisAttractor", ForceModelOrdinal::AxisAttractor, ForceCategoryOrdinal::Attraction, "Attraction",
        "Axis Attractor", "axis", "#22d3ee",
        "Pulls fluid toward an infinite line through its centre.",
        { 10.0f, 1.8f, { 0.0f, 1.0f, 0.0f }, ForceFalloffOrdinal::Linear, 0.0f, 0.0f, 0.0f },
        { {}, {}, {} },
        true,
    },
    {
        "Orbit", ForceModelOrdinal::Orbit, ForceCategoryOrdinal::Attraction, "Attraction",
        "Orbit", "orbit", "#c4b5fd",
        "Tangential circulation about a point with centripetal pull \xE2\x80\x94 gas orbits the centre.",
        { 10.0f, 1.6f, { 0.0f, 1.0f, 0.0f }, ForceFalloffOrdinal::Linear, 0.0f, 0.5f, 0.0f },
        { {}, "Centripetal Pull", {} },
        true,
    },
    {
        "Containment", ForceModelOrdinal::Containment, ForceCategoryOrdinal::Attraction, "Attraction",
        "Containment", "containment", "#2dd4bf",
        "Spring that pushes gas back inside its radius \xE2\x80\x94 an invisible soft bottle.",
        { 20.0f, 1.2f, { 0.0f, 1.0f, 0.0f }, ForceFalloffOrdinal::Linear, 0.0f, 0.4f, 0.0f },
        { {}, "Wall Softness", {} },
        false,
    },
    {
        "MagneticDipole", ForceModelOrdinal::MagneticDipole, ForceCategoryOrdinal::Attraction, "Attraction",
        "Magnetic Dipole", "dipole", "#e879f9",
        "Dipole field lines looping pole to pole \xE2\x80\x94 gas follows the magnetic flux.",
        { 10.0f, 1.8f, { 0.0f, 1.0f, 0.0f }, ForceFalloffOrdinal::SmoothStep, 0.0f, 0.25f, 0.0f },
        { {}, "Core Radius", {} },
        true,
    },
    // ---------------------------------------------------------------- Rotational
    {
        "VortexColumn", ForceModelOrdinal::RotationalColumn, ForceCategoryOrdinal::Rotational, "Rotational",
        "Vortex Column", "vortex", "#34d399",
        "Swirls fluid around an axis with optional inward pull and axial lift.",
        { 12.0f, 1.6f, { 0.0f, 1.0f, 0.0f }, ForceFalloffOrdinal::Linear, 0.4f, 2.5f, 0.0f },
        { "Inward Pull", "Axial Lift", {} },
        true,
    },
    {
        "VortexRing", ForceModelOrdinal::RotationalRing, ForceCategoryOrdinal::Rotational, "Rotational",
        "Vortex Ring", "ring", "#c084fc",
        "Toroidal circulation around a ring \xE2\x80\x94 launches smoke rings.",
        { 16.0f, 0.9f, { 0.0f, 1.0f, 0.0f }, ForceFalloffOrdinal::SmoothStep, 0.35f, 0.0f, 0.0f },
        { "Tube Radius", {}, {} },
        true,
    },
    {
        "Spin", ForceModelOrdinal::Spin, ForceCategoryOrdinal::Rotational, "Rotational",
        "Rigid Spin", "spin", "#4ade80",
        "Solid-body rotation: tangential force grows with distance from the axis.",
        { 6.0f, 1.5f, { 0.0f, 1.0f, 0.0f }, ForceFalloffOrdinal::Plateau, 0.0f, 0.0f, 0.0f },
        { {}, {}, {} },
        true,
    },
    // ---------------------------------------------------------------- Turbulence
    {
        "CurlNoiseTurbulence", ForceModelOrdinal::CurlNoiseTurbulence, ForceCategoryOrdinal::Turbulence, "Turbulence",
        "Curl Noise Turbulence", "turbulence", "#fbbf24",
        "Divergence-free animated curl noise that breaks up laminar flow.",
        { 9.0f, 2.4f, { 0.0f, 1.0f, 0.0f }, ForceFalloffOrdinal::SmoothStep, 1.8f, 0.7f, 0.0f },
        { "Noise Scale", "Evolution Speed", {} },
        true,
    },
    {
        "BrownianJitter", ForceModelOrdinal::BrownianJitter, ForceCategoryOrdinal::Turbulence, "Turbulence",
        "Brownian Jitter", "brownian", "#facc15",
        "Per-cell random kicks re-rolled at a fixed rate \xE2\x80\x94 fine-grained, incoherent agitation.",
        { 6.0f, 1.5f, { 0.0f, 1.0f, 0.0f }, ForceFalloffOrdinal::SmoothStep, 6.0f, 12.0f, 0.0f },
        { "Cell Scale", "Re-roll Rate (Hz)", {} },
        true,
    },
    // ---------------------------------------------------------------- Burst
    {
        "RadialBurst", ForceModelOrdinal::RadialBurst, ForceCategoryOrdinal::Burst, "Burst",
        "Radial Burst", "burst", "#fb7185",
        "Pulsing outward blast filling the reach \xE2\x80\x94 detonation kick.",
        { 40.0f, 1.5f, { 0.0f, 1.0f, 0.0f }, ForceFalloffOrdinal::Linear, 1.2f, 0.12f, 0.0f },
        { "Pulse Period", "Pulse Width", "Start Delay" },
        false,
    },
    {
        "ShockwaveShell", ForceModelOrdinal::ShockwaveShell, ForceCategoryOrdinal::Burst, "Burst",
        "Shockwave Shell", "shockwave", "#f87171",
        "A thin spherical pressure front expanding from the centre to the reach radius each period.",
        { 60.0f, 2.0f, { 0.0f, 1.0f, 0.0f }, ForceFalloffOrdinal::Constant, 1.6f, 0.12f, 0.0f },
        { "Period", "Shell Thickness", "Start Delay" },
        false,
    },
    {
        "Implosion", ForceModelOrdinal::Implosion, ForceCategoryOrdinal::Burst, "Burst",
        "Implosion", "implosion", "#fda4af",
        "Periodic inward collapse toward the centre \xE2\x80\x94 the pre-blast suck of a fireball.",
        { 35.0f, 1.5f, { 0.0f, 1.0f, 0.0f }, ForceFalloffOrdinal::Linear, 1.5f, 0.15f, 0.0f },
        { "Pulse Period", "Pulse Width", "Start Delay" },
        false,
    },
    // ---------------------------------------------------------------- Damping / Thermal
    {
        "DragBaffle", ForceModelOrdinal::DragBaffle, ForceCategoryOrdinal::Damping, "Damping",
        "Drag Baffle", "drag", "#94a3b8",
        "Damps velocity inside its reach \xE2\x80\x94 a soft brake on the flow.",
        { 4.0f, 1.0f, { 0.0f, 1.0f, 0.0f }, ForceFalloffOrdinal::SmoothStep, 0.0f, 0.0f, 0.0f },
        { {}, {}, {} },
        false,
    },
    {
        "ThermalUpdraft", ForceModelOrdinal::ThermalUpdraft, ForceCategoryOrdinal::Thermal, "Thermal",
        "Thermal Updraft", "updraft", "#f97316",
        "Cylindrical column of lift along its axis.",
        { 8.0f, 0.8f, { 0.0f, 1.0f, 0.0f }, ForceFalloffOrdinal::SmoothStep, 0.0f, 0.0f, 0.0f },
        { {}, {}, {} },
        true,
    },
}};

// Direct O(1) index table from ForceModelOrdinal code (0..19) -> index inside kForceModelDescriptors
constexpr std::array<uint8_t, kForceModelCount> BuildCodeToDescriptorIndex() noexcept
{
    std::array<uint8_t, kForceModelCount> table {};
    for (uint8_t i = 0u; i < static_cast<uint8_t>(kForceModelCount); ++i)
    {
        const uint32_t code = static_cast<uint32_t>(kForceModelDescriptors[i].Code);
        table[code] = i;
    }
    return table;
}

inline constexpr std::array<uint8_t, kForceModelCount> kCodeToDescriptorIndex = BuildCodeToDescriptorIndex();

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                  REGISTRY QUERY FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

std::span<const ForceModelDescriptor> QueryAllForceModelDescriptors() noexcept
{
    return kForceModelDescriptors;
}

const ForceModelDescriptor* FindForceModelByOrdinal(const ForceModelOrdinal code) noexcept
{
    const uint32_t raw = static_cast<uint32_t>(code);
    if (raw >= kForceModelCount)
    {
        return nullptr;
    }
    return &kForceModelDescriptors[kCodeToDescriptorIndex[raw]];
}

const ForceModelDescriptor* FindForceModelByTomlId(const std::string_view tomlId) noexcept
{
    for (const ForceModelDescriptor& descriptor : kForceModelDescriptors)
    {
        if (descriptor.TomlId == tomlId)
        {
            return &descriptor;
        }
    }
    return nullptr;
}

bool IsForceModelGasVelocitySaturated(const ForceModelOrdinal code) noexcept
{
    const ForceModelDescriptor* descriptor = FindForceModelByOrdinal(code);
    return descriptor != nullptr ? descriptor->GasVelocitySaturated : false;
}

std::string_view QueryFalloffTomlToken(const ForceFalloffOrdinal falloff) noexcept
{
    const uint32_t raw = static_cast<uint32_t>(falloff);
    if (raw >= kForceFalloffCount)
    {
        return kFalloffTokens[0u];
    }
    return kFalloffTokens[raw];
}

std::string_view QueryFalloffDisplayLabel(const ForceFalloffOrdinal falloff) noexcept
{
    const uint32_t raw = static_cast<uint32_t>(falloff);
    if (raw >= kForceFalloffCount)
    {
        return kFalloffLabels[0u];
    }
    return kFalloffLabels[raw];
}

std::optional<ForceFalloffOrdinal> ParseFalloffTomlToken(const std::string_view token) noexcept
{
    for (uint32_t i = 0u; i < kForceFalloffCount; ++i)
    {
        if (kFalloffTokens[i] == token)
        {
            return static_cast<ForceFalloffOrdinal>(i);
        }
    }
    return std::nullopt;
}

std::span<const std::string_view> QueryForceCategoryNames() noexcept
{
    return kForceCategoryNames;
}

std::span<const std::string_view> QueryReleaseSourceShapeTokens() noexcept
{
    return kReleaseSourceShapeTokens;
}

std::span<const std::string_view> QueryFluidBoundaryTokens() noexcept
{
    return kFluidBoundaryTokens;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                FACTORY & GPU RECORD PACKING
//------------------------------------------------------------------------------------------------------------------------

ForceModelNode ConstructDefaultForceNode(const ForceModelOrdinal code, const std::string_view nodeId) noexcept
{
    const ForceModelDescriptor* descriptor = FindForceModelByOrdinal(code);
    if (descriptor == nullptr)
    {
        descriptor = &kForceModelDescriptors[0u];
    }

    ForceModelNode node {};
    node.NodeId         = std::string(nodeId);
    node.ModelCode      = descriptor->Code;
    node.Name           = std::string(descriptor->DisplayName);
    node.Enabled        = true;
    node.Visible        = true;
    node.PositionMeters = { 0.0f, 0.0f, 0.0f };
    node.Direction      = descriptor->Defaults.Direction;
    node.Strength       = descriptor->Defaults.Strength;
    node.RadiusMeters   = descriptor->Defaults.RadiusMeters;
    node.Falloff        = descriptor->Defaults.Falloff;
    node.Frequency      = descriptor->Defaults.Frequency;
    node.Secondary      = descriptor->Defaults.Secondary;
    node.Tertiary       = descriptor->Defaults.Tertiary;
    node.StartTimeSec   = 0.0f;
    node.DurationSec    = 0.0f;
    node.FadeSec        = kDefaultFadeSeconds;
    node.RepeatSec      = 0.0f;
    node.TargetChannels = static_cast<uint32_t>(ForceTargetChannelBits::All);
    return node;
}

ForceModelGpuRecord PackForceModelGpuRecord(const ForceModelNode& node, const float timingWeight) noexcept
{
    // Reference: Experimental/FluidEditor/Engine/FluidSolver.js L419-L438
    ForceModelGpuRecord record {};
    record.PositionRadius[0]    = node.PositionMeters[0];
    record.PositionRadius[1]    = node.PositionMeters[1];
    record.PositionRadius[2]    = node.PositionMeters[2];
    record.PositionRadius[3]    = node.RadiusMeters;

    record.DirectionStrength[0] = node.Direction[0];
    record.DirectionStrength[1] = node.Direction[1];
    record.DirectionStrength[2] = node.Direction[2];
    record.DirectionStrength[3] = node.Enabled ? (node.Strength * timingWeight) : 0.0f;

    record.TypeFalloffParams[0] = static_cast<float>(static_cast<uint32_t>(node.ModelCode));
    record.TypeFalloffParams[1] = static_cast<float>(static_cast<uint32_t>(node.Falloff));
    record.TypeFalloffParams[2] = node.Frequency;
    record.TypeFalloffParams[3] = node.Secondary;

    record.TimingChannelMask[0] = node.Tertiary;
    record.TimingChannelMask[1] = 0.0f;
    record.TimingChannelMask[2] = static_cast<float>(node.TargetChannels);
    record.TimingChannelMask[3] = node.Enabled ? 1.0f : 0.0f;
    return record;
}

} // namespace Frontier::SimulationSpace::ForceFields
