/*============================================================================================================================================
                                                            FORCEINSPECTORVIEW.CPP
============================================================================================================================================*/
// 🧩 Complete 1:1 ForceFields Inspector implementation matching VectorFieldVisualizer across all 4 tabs and 19 ForceModel laws

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Editor/Workspaces/Simulation/ForceInspectorView.h"

#include "Engine/Codecs/VectorFieldCodec.h"
#include "Engine/Components/Controls/DropdownSelector.h"
#include "Engine/Components/Controls/ScalarSlider.h"
#include "Engine/SimulationSpace/ForceFields/FieldFormulaCompiler.h"
#include "Engine/SimulationSpace/ForceFields/ForceMotionPath.h"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <sstream>
#include <vector>

namespace Frontier::Editor::Simulation
{

namespace
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    COLORS & GEOMETRY
//------------------------------------------------------------------------------------------------------------------------

namespace InspectorSurfaceColors
{
inline constexpr ColorRgba RootBg         { 18,  18,  18,  255 };
inline constexpr ColorRgba PanelBg        { 32,  32,  32,  255 };
inline constexpr ColorRgba SubWellBg      { 23,  23,  24,  255 };
inline constexpr ColorRgba InsetBg        { 21,  21,  23,  255 };
inline constexpr ColorRgba RecessDark     { 13,  13,  15,  255 };
inline constexpr ColorRgba EditorBg       { 13,  15,  19,  255 };
inline constexpr ColorRgba HairlineBorder { 255, 255, 255, 14  };
inline constexpr ColorRgba SubtleBorder   { 255, 255, 255, 24  };
inline constexpr ColorRgba TabInactiveBg  { 24,  24,  24,  255 };
inline constexpr ColorRgba TabActiveBg    { 39,  39,  39,  255 };
inline constexpr ColorRgba TabHoverBg     { 46,  46,  46,  255 };
inline constexpr ColorRgba ButtonBg       { 27,  27,  31,  255 };
inline constexpr ColorRgba InkPrimary     { 240, 240, 240, 255 };
inline constexpr ColorRgba InkHeading     { 222, 222, 222, 255 };
inline constexpr ColorRgba InkLabel       { 169, 175, 181, 255 };
inline constexpr ColorRgba InkMuted       { 132, 138, 144, 255 };
inline constexpr ColorRgba InkDim         { 102, 108, 114, 255 };
inline constexpr ColorRgba EmeraldSoft    { 150, 222, 192, 255 };
inline constexpr ColorRgba EmeraldLine    { 146, 216, 194, 255 };
inline constexpr ColorRgba EmeraldFill    { 88,  153, 121, 72  };
inline constexpr ColorRgba EmeraldLow     { 88,  153, 121, 8   };
inline constexpr ColorRgba SwitchGreen    { 52,  199, 89,  255 };
inline constexpr ColorRgba SwitchOff      { 47,  47,  51,  255 };
inline constexpr ColorRgba AxisRed        { 226, 116, 112, 255 };
inline constexpr ColorRgba AxisGreen      { 103, 196, 133, 255 };
inline constexpr ColorRgba AxisBlue       { 110, 167, 229, 255 };
inline constexpr ColorRgba AmberAccent    { 255, 180, 84,  255 };
inline constexpr ColorRgba TealCode       { 169, 217, 200, 255 };
inline constexpr ColorRgba CyanCode       { 147, 201, 232, 255 };
inline constexpr ColorRgba FooterBg       { 21,  21,  21,  255 };
} // namespace InspectorSurfaceColors

//------------------------------------------------------------------------------------------------------------------------
//                                              METEOROLOGY & BEAUFORT BANDS
//------------------------------------------------------------------------------------------------------------------------

struct BeaufortEntry
{
    float       UpperMs;
    uint32_t    Scale;
    const char* Name;
    const char* LandDescription;
};

constexpr std::array<BeaufortEntry, 13> BeaufortScaleBands = {{
    { 0.5f,    0u,  "Calm",            "smoke rises straight up"         },
    { 1.5f,    1u,  "Light air",       "smoke drifts"                    },
    { 3.3f,    2u,  "Light breeze",    "leaves rustle"                   },
    { 5.5f,    3u,  "Gentle breeze",   "flags stir, leaves move"         },
    { 7.9f,    4u,  "Moderate breeze", "dust lifts, small branches move" },
    { 10.7f,   5u,  "Fresh breeze",    "small trees sway"                },
    { 13.8f,   6u,  "Strong breeze",   "large branches move"             },
    { 17.1f,   7u,  "Near gale",       "whole trees in motion"           },
    { 20.7f,   8u,  "Gale",            "twigs break off"                 },
    { 24.4f,   9u,  "Strong gale",     "slates lift"                     },
    { 28.4f,   10u, "Storm",           "trees uprooted"                  },
    { 32.6f,   11u, "Violent storm",   "widespread storm gusts"          },
    { 1.0e9f,  12u, "Hurricane force", "extreme structural wind load"    },
}};

[[nodiscard]] const BeaufortEntry& ResolveBeaufortEntry(float SpeedMs) noexcept
{
    for (const auto& Band : BeaufortScaleBands)
    {
        if (SpeedMs < Band.UpperMs)
        {
            return Band;
        }
    }
    return BeaufortScaleBands.back();
}

[[nodiscard]] const char* ResolveCompassLabel(float AzimuthDeg) noexcept
{
    static constexpr std::array<const char*, 16> Directions = {{
        "N", "NNE", "NE", "ENE", "E", "ESE", "SE", "SSE",
        "S", "SSW", "SW", "WSW", "W", "WNW", "NW", "NNW",
    }};
    float Wrapped = std::fmod(AzimuthDeg, 360.0f);
    if (Wrapped < 0.0f)
    {
        Wrapped += 360.0f;
    }
    const auto Index = static_cast<std::size_t>(std::lround(Wrapped / 22.5f)) % Directions.size();
    return Directions[Index];
}

//------------------------------------------------------------------------------------------------------------------------
//                                           ALL 19 FORCEMODEL PARAMETER SCHEMAS
//------------------------------------------------------------------------------------------------------------------------

enum class ForceScalarSlot : uint8_t
{
    None            = 0u,
    Strength        = 1u,
    AzimuthYaw      = 2u,
    ElevationPitch  = 3u,
    FalloffExponent = 4u,
    EyeRadius      = 5u,
    FrequencyHz     = 6u,
    TurbulenceScale = 7u,
    SwirlRatio      = 8u,
    ImpulseDuration = 9u,
    BuoyancyRatio   = 10u,
    DragCoeff       = 11u,
    OctaveFloat     = 12u,
};

struct ForceParameterDescriptor
{
    ForceScalarSlot Slot;
    const char*     Label;
    float           Minimum;
    float           Maximum;
    const char*     UnitText;
    uint32_t        DecimalDigits;
};

struct ForceModelDescriptor
{
    const char*                              DisplayTitle;
    const char*                              SystemCaption;
    const char*                              ChannelCaption;
    const char*                              EquationText;
    uint32_t                                 ParamCount;
    std::array<ForceParameterDescriptor, 6>  Params;
};

constexpr std::array<ForceModelDescriptor, 19> ForceModelRepository = {{
    // 0: LinearWind
    {
        "Linear Wind", "VectorField", "Flow", "v = v0*(1 + s*z)*d_hat + Gust(x,t)", 5u,
        {{
            { ForceScalarSlot::Strength,        "Mean Speed",    0.0f,   50.0f,  "m/s", 1u },
            { ForceScalarSlot::AzimuthYaw,      "Coming From", -180.0f, 180.0f,  "deg", 0u },
            { ForceScalarSlot::FrequencyHz,     "Gust Freq",     0.02f,  2.5f,   "Hz",  2u },
            { ForceScalarSlot::TurbulenceScale, "Turb Scale",    4.0f,   80.0f,  "m",   1u },
            { ForceScalarSlot::FalloffExponent, "Alt Shear",     0.1f,   4.0f,   "exp", 2u },
            { ForceScalarSlot::None, "", 0.0f, 1.0f, "", 0u },
        }}
    },
    // 1: Buoyancy
    {
        "Buoyancy", "VectorField", "Accelerate", "a_z = g*((rho_f - rho_b)/rho_b)*A(r)", 3u,
        {{
            { ForceScalarSlot::Strength,        "Buoyancy Scale", -30.0f, 40.0f, "m/s2", 1u },
            { ForceScalarSlot::BuoyancyRatio,   "Density Ratio",   0.1f,  3.5f,  "x",    2u },
            { ForceScalarSlot::FalloffExponent, "Alt Lapse",       0.1f,  4.0f,  "exp",  2u },
            { ForceScalarSlot::None, "", 0.0f, 1.0f, "", 0u },
            { ForceScalarSlot::None, "", 0.0f, 1.0f, "", 0u },
            { ForceScalarSlot::None, "", 0.0f, 1.0f, "", 0u },
        }}
    },
    // 2: RadialBurst
    {
        "Radial Burst", "VectorField", "Accelerate", "R(t) = R0 + v_wave*t * dv = F*dt/m", 4u,
        {{
            { ForceScalarSlot::Strength,        "Peak Thrust",   0.0f, 100.0f, "m/s2", 0u },
            { ForceScalarSlot::ImpulseDuration, "Impulse Width", 0.2f,  12.0f, "s",    2u },
            { ForceScalarSlot::EyeRadius,      "Origin R0",     0.5f,  25.0f, "m",    1u },
            { ForceScalarSlot::FrequencyHz,     "Repeat Rate",   0.05f,  4.0f, "Hz",   2u },
            { ForceScalarSlot::None, "", 0.0f, 1.0f, "", 0u },
            { ForceScalarSlot::None, "", 0.0f, 1.0f, "", 0u },
        }}
    },
    // 3: Attractor
    {
        "Point Attractor", "VectorField", "Accelerate", "a = -S*A(r)*r_hat + w*(z_hat x r_hat)", 3u,
        {{
            { ForceScalarSlot::Strength,        "Inward Pull",    0.0f,  60.0f, "m/s2", 1u },
            { ForceScalarSlot::EyeRadius,      "Softening",      0.5f,  25.0f, "m",    1u },
            { ForceScalarSlot::SwirlRatio,      "Orbit Swirl",   -2.5f,   2.5f, "x",    2u },
            { ForceScalarSlot::None, "", 0.0f, 1.0f, "", 0u },
            { ForceScalarSlot::None, "", 0.0f, 1.0f, "", 0u },
            { ForceScalarSlot::None, "", 0.0f, 1.0f, "", 0u },
        }}
    },
    // 4: Repulsor
    {
        "Point Repulsor", "VectorField", "Accelerate", "a = +S*A(r)*r_hat + w*(z_hat x r_hat)", 3u,
        {{
            { ForceScalarSlot::Strength,        "Push Force",     0.0f,  60.0f, "m/s2", 1u },
            { ForceScalarSlot::EyeRadius,      "Softening",      0.5f,  25.0f, "m",    1u },
            { ForceScalarSlot::SwirlRatio,      "Deflect Spin",  -2.5f,   2.5f, "x",    2u },
            { ForceScalarSlot::None, "", 0.0f, 1.0f, "", 0u },
            { ForceScalarSlot::None, "", 0.0f, 1.0f, "", 0u },
            { ForceScalarSlot::None, "", 0.0f, 1.0f, "", 0u },
        }}
    },
    // 5: AxisAttractor
    {
        "Axis Attractor", "VectorField", "Accelerate", "a = -S*A(r_perp)*n_hat + S_ax*axis_hat", 4u,
        {{
            { ForceScalarSlot::Strength,       "Radial Clamp",  0.0f,   50.0f, "m/s2", 1u },
            { ForceScalarSlot::SwirlRatio,     "Helix Twist",  -2.5f,    2.5f, "x",    2u },
            { ForceScalarSlot::AzimuthYaw,     "Axis Azimuth", -180.0f, 180.0f,"deg",  0u },
            { ForceScalarSlot::ElevationPitch, "Axis Pitch",   -75.0f,  75.0f, "deg",  0u },
            { ForceScalarSlot::None, "", 0.0f, 1.0f, "", 0u },
            { ForceScalarSlot::None, "", 0.0f, 1.0f, "", 0u },
        }}
    },
    // 6: Containment
    {
        "Containment Shell", "VectorField", "Accelerate", "Zero inside r < R_in; Restoring [R_in, R]", 3u,
        {{
            { ForceScalarSlot::Strength,        "Wall Stiffness", 0.0f, 60.0f, "m/s2", 0u },
            { ForceScalarSlot::EyeRadius,      "Quiet Radius",   1.0f, 35.0f, "m",    1u },
            { ForceScalarSlot::SwirlRatio,      "Boundary Swirl",-2.0f,  2.0f, "x",    2u },
            { ForceScalarSlot::None, "", 0.0f, 1.0f, "", 0u },
            { ForceScalarSlot::None, "", 0.0f, 1.0f, "", 0u },
            { ForceScalarSlot::None, "", 0.0f, 1.0f, "", 0u },
        }}
    },
    // 7: RotationalColumn (Cyclonic column UI label)
    {
        "Vortex Column", "VectorField", "Flow", "v_t = S*(2*rho)/(1+rho^2) * v_r * v_z", 5u,
        {{
            { ForceScalarSlot::Strength,        "Tangential Peak", 0.0f, 60.0f, "m/s", 1u },
            { ForceScalarSlot::EyeRadius,      "Eyewall Radius",  1.0f, 25.0f, "m",   1u },
            { ForceScalarSlot::SwirlRatio,      "Spin Chirality", -2.5f,  2.5f, "x",   2u },
            { ForceScalarSlot::DragCoeff,       "Base Inflow",     0.0f, 10.0f, "1/s", 2u },
            { ForceScalarSlot::FalloffExponent, "Radial Falloff",  0.1f,  4.0f, "exp", 2u },
            { ForceScalarSlot::None, "", 0.0f, 1.0f, "", 0u },
        }}
    },
    // 8: Torque
    {
        "Torque Field", "VectorField", "Accelerate", "tau = S * A(r) * normalize(axis_hat x r)", 4u,
        {{
            { ForceScalarSlot::Strength,       "Torque Gain",   0.0f,   50.0f, "rad/s2", 1u },
            { ForceScalarSlot::SwirlRatio,     "Spin Sign",    -2.0f,    2.0f, "x",      2u },
            { ForceScalarSlot::AzimuthYaw,     "Axis Azimuth", -180.0f, 180.0f,"deg",    0u },
            { ForceScalarSlot::ElevationPitch, "Axis Tilt",    -90.0f,  90.0f, "deg",    0u },
            { ForceScalarSlot::None, "", 0.0f, 1.0f, "", 0u },
            { ForceScalarSlot::None, "", 0.0f, 1.0f, "", 0u },
        }}
    },
    // 9: Drag
    {
        "Aerodynamic Drag", "VectorField", "Damp", "a_d = -(k1 + k2*|v|) * A(r) * v_rel", 3u,
        {{
            { ForceScalarSlot::DragCoeff,       "Linear Damping", 0.0f,  8.0f, "1/s", 2u },
            { ForceScalarSlot::Strength,        "Velocity Scale", 0.0f, 40.0f, "m/s", 1u },
            { ForceScalarSlot::FalloffExponent, "Falloff Shape",  0.1f,  4.0f, "exp", 2u },
            { ForceScalarSlot::None, "", 0.0f, 1.0f, "", 0u },
            { ForceScalarSlot::None, "", 0.0f, 1.0f, "", 0u },
            { ForceScalarSlot::None, "", 0.0f, 1.0f, "", 0u },
        }}
    },
    // 10: CurlNoise
    {
        "Curl Noise", "TurbulenceField", "Flow", "v = S*A(r)*(curl psi(p/lambda - v_d*t))", 4u,
        {{
            { ForceScalarSlot::Strength,        "Eddy Speed",   0.0f, 40.0f, "m/s", 1u },
            { ForceScalarSlot::TurbulenceScale, "Eddy Scale",   2.0f, 60.0f, "m",   1u },
            { ForceScalarSlot::OctaveFloat,     "Octaves",      1.0f,  5.0f, "oct", 0u },
            { ForceScalarSlot::FrequencyHz,     "Advect Rate",  0.05f, 3.0f, "Hz",  2u },
            { ForceScalarSlot::None, "", 0.0f, 1.0f, "", 0u },
            { ForceScalarSlot::None, "", 0.0f, 1.0f, "", 0u },
        }}
    },
    // 11: AtmosphericGust
    {
        "Atmospheric Gust", "TurbulenceField", "Flow", "v = S*A(r)*(d_hat + GustWave(x, t))", 4u,
        {{
            { ForceScalarSlot::Strength,        "Peak Gust",   0.0f,   50.0f,  "m/s", 1u },
            { ForceScalarSlot::AzimuthYaw,      "Gust Heading",-180.0f, 180.0f, "deg", 0u },
            { ForceScalarSlot::FrequencyHz,     "Gust Freq",   0.05f,  2.5f,   "Hz",  2u },
            { ForceScalarSlot::TurbulenceScale, "Plume Scale", 2.0f,   50.0f,  "m",   1u },
            { ForceScalarSlot::None, "", 0.0f, 1.0f, "", 0u },
            { ForceScalarSlot::None, "", 0.0f, 1.0f, "", 0u },
        }}
    },
    // 12: VorticityConfinement
    {
        "Vorticity Confinement", "TurbulenceField", "Accelerate", "a_v = eps*h*(eta_hat x omega)", 4u,
        {{
            { ForceScalarSlot::Strength,        "Confinement Gain", 0.0f, 45.0f, "m/s2", 1u },
            { ForceScalarSlot::SwirlRatio,      "Spin Bias",       -2.0f,  2.0f, "x",    2u },
            { ForceScalarSlot::TurbulenceScale, "Cell Step h",      2.0f, 40.0f, "m",    1u },
            { ForceScalarSlot::OctaveFloat,     "Octaves",          1.0f,  5.0f, "oct",  0u },
            { ForceScalarSlot::None, "", 0.0f, 1.0f, "", 0u },
            { ForceScalarSlot::None, "", 0.0f, 1.0f, "", 0u },
        }}
    },
    // 13: BrownianNoise
    {
        "Brownian Drift", "TurbulenceField", "Accelerate", "a = S*A(r)*SmoothCellHash(x, y, z, t)", 3u,
        {{
            { ForceScalarSlot::Strength,        "Jitter Amp",  0.0f, 35.0f, "m/s2", 1u },
            { ForceScalarSlot::TurbulenceScale, "Cell Size",   2.0f, 40.0f, "m",    1u },
            { ForceScalarSlot::FrequencyHz,     "Tempo Rate",  0.1f, 12.0f, "Hz",   2u },
            { ForceScalarSlot::None, "", 0.0f, 1.0f, "", 0u },
            { ForceScalarSlot::None, "", 0.0f, 1.0f, "", 0u },
            { ForceScalarSlot::None, "", 0.0f, 1.0f, "", 0u },
        }}
    },
    // 14: LorentzField
    {
        "Lorentz E + v x B", "VectorField", "Accelerate", "a = (q/m)*A(r)*(E + v_drift x B)", 5u,
        {{
            { ForceScalarSlot::Strength,       "Field Scale",   0.0f,   45.0f, "m/s2", 1u },
            { ForceScalarSlot::BuoyancyRatio,  "q/m Ratio",    -4.0f,    4.0f, "C/kg", 2u },
            { ForceScalarSlot::SwirlRatio,     "Magnetic B",   -2.5f,    2.5f, "T",    2u },
            { ForceScalarSlot::AzimuthYaw,     "B Azimuth",   -180.0f, 180.0f, "deg",  0u },
            { ForceScalarSlot::ElevationPitch, "B Elevation",  -90.0f,  90.0f, "deg",  0u },
            { ForceScalarSlot::None, "", 0.0f, 1.0f, "", 0u },
        }}
    },
    // 15: MagneticDipole
    {
        "Magnetic Dipole", "VectorField", "Accelerate", "B(r) = S*[3(m_hat.r_hat)r_hat - m_hat]/r^3", 4u,
        {{
            { ForceScalarSlot::Strength,       "Dipole Moment",  0.0f,   45.0f, "m/s2", 1u },
            { ForceScalarSlot::EyeRadius,     "Pole Softening", 1.0f,   25.0f, "m",    1u },
            { ForceScalarSlot::AzimuthYaw,     "Moment Azimuth",-180.0f,180.0f, "deg",  0u },
            { ForceScalarSlot::ElevationPitch, "Moment Tilt",   -90.0f,  90.0f, "deg",  0u },
            { ForceScalarSlot::None, "", 0.0f, 1.0f, "", 0u },
            { ForceScalarSlot::None, "", 0.0f, 1.0f, "", 0u },
        }}
    },
    // 16: FlowStream
    {
        "Flow Stream", "StreamTransport", "Flow", "v = S*g(t)*A(r)*d_stream", 5u,
        {{
            { ForceScalarSlot::Strength,       "Stream Speed", 0.0f,   50.0f, "m/s", 1u },
            { ForceScalarSlot::AzimuthYaw,     "Flow Azimuth",-180.0f,180.0f, "deg", 0u },
            { ForceScalarSlot::ElevationPitch, "Stream Pitch", -75.0f, 75.0f, "deg", 0u },
            { ForceScalarSlot::EyeRadius,     "Nozzle Width",  1.0f,  25.0f, "m",   1u },
            { ForceScalarSlot::SwirlRatio,     "Wake Roll",    -2.5f,   2.5f, "x",   2u },
            { ForceScalarSlot::None, "", 0.0f, 1.0f, "", 0u },
        }}
    },
    // 17: SplineForce
    {
        "Spline Conduit", "StreamTransport", "Flow", "a = S*T_hat(s*) - k_c*(p - C(s*))", 4u,
        {{
            { ForceScalarSlot::Strength,        "Guide Speed",  0.0f,   45.0f, "m/s", 1u },
            { ForceScalarSlot::EyeRadius,      "Tube Radius",  1.0f,   25.0f, "m",   1u },
            { ForceScalarSlot::SwirlRatio,      "Ribbon Twist",-2.0f,    2.0f, "x",   2u },
            { ForceScalarSlot::AzimuthYaw,      "Heading",    -180.0f, 180.0f, "deg", 0u },
            { ForceScalarSlot::None, "", 0.0f, 1.0f, "", 0u },
            { ForceScalarSlot::None, "", 0.0f, 1.0f, "", 0u },
        }}
    },
    // 18: BoundaryAdhesion
    {
        "Boundary Adhesion", "StreamTransport", "Accelerate", "a = -S*tanh(dh/sigma)*z_hat + Glide*d_hat", 4u,
        {{
            { ForceScalarSlot::Strength,        "Cling Pull",    0.0f,   45.0f, "m/s2", 1u },
            { ForceScalarSlot::EyeRadius,      "Layer Width",   1.0f,   25.0f, "m",    1u },
            { ForceScalarSlot::DragCoeff,       "Surface Glide", 0.0f,   10.0f, "1/s",  2u },
            { ForceScalarSlot::AzimuthYaw,      "Glide Azimuth",-180.0f,180.0f, "deg",  0u },
            { ForceScalarSlot::None, "", 0.0f, 1.0f, "", 0u },
            { ForceScalarSlot::None, "", 0.0f, 1.0f, "", 0u },
        }}
    },
}};

[[nodiscard]] const ForceModelDescriptor& QueryForceModelDescriptor(SimulationSpace::ForceFields::ForceModel Model) noexcept
{
    const auto Ordinal = static_cast<std::size_t>(Model);
    return (Ordinal < ForceModelRepository.size()) ? ForceModelRepository[Ordinal] : ForceModelRepository[0];
}

[[nodiscard]] float ReadScalarSlot(const SimulationSpace::ForceFields::ForceSourceRecordEntry& Src, ForceScalarSlot Slot) noexcept
{
    switch (Slot)
    {
        case ForceScalarSlot::Strength:        return Src.Strength;
        case ForceScalarSlot::AzimuthYaw:      return Src.EulerDegrees.Z;
        case ForceScalarSlot::ElevationPitch:  return Src.EulerDegrees.X;
        case ForceScalarSlot::FalloffExponent: return Src.FalloffExponent;
        case ForceScalarSlot::EyeRadius:      return Src.CoreRadiusMeters;
        case ForceScalarSlot::FrequencyHz:     return Src.FrequencyHertz;
        case ForceScalarSlot::TurbulenceScale: return Src.TurbulenceScaleMeters;
        case ForceScalarSlot::SwirlRatio:      return Src.SwirlRatio;
        case ForceScalarSlot::ImpulseDuration: return Src.ImpulseDurationSec;
        case ForceScalarSlot::BuoyancyRatio:   return Src.BuoyancyDensityRatio;
        case ForceScalarSlot::DragCoeff:       return Src.DragCoefficient;
        case ForceScalarSlot::OctaveFloat:     return static_cast<float>(Src.OctaveCount);
        default:                               return 0.0f;
    }
}

void WriteScalarSlot(SimulationSpace::ForceFields::ForceSourceRecordEntry& Src, ForceScalarSlot Slot, float NextScalar) noexcept
{
    switch (Slot)
    {
        case ForceScalarSlot::Strength:        Src.Strength              = NextScalar; break;
        case ForceScalarSlot::AzimuthYaw:      Src.EulerDegrees.Z        = NextScalar; break;
        case ForceScalarSlot::ElevationPitch:  Src.EulerDegrees.X        = NextScalar; break;
        case ForceScalarSlot::FalloffExponent: Src.FalloffExponent       = NextScalar; break;
        case ForceScalarSlot::EyeRadius:      Src.CoreRadiusMeters      = NextScalar; break;
        case ForceScalarSlot::FrequencyHz:     Src.FrequencyHertz        = NextScalar; break;
        case ForceScalarSlot::TurbulenceScale: Src.TurbulenceScaleMeters = NextScalar; break;
        case ForceScalarSlot::SwirlRatio:      Src.SwirlRatio            = NextScalar; break;
        case ForceScalarSlot::ImpulseDuration: Src.ImpulseDurationSec    = NextScalar; break;
        case ForceScalarSlot::BuoyancyRatio:   Src.BuoyancyDensityRatio  = NextScalar; break;
        case ForceScalarSlot::DragCoeff:       Src.DragCoefficient       = NextScalar; break;
        case ForceScalarSlot::OctaveFloat:
            Src.OctaveCount = static_cast<uint32_t>(std::clamp(static_cast<int>(std::lround(NextScalar)), 1, 5));
            break;
        default: break;
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                              LABEL LOOKUPS & FORMATTING
//------------------------------------------------------------------------------------------------------------------------

[[nodiscard]] const char* ReachLabel(SimulationSpace::ForceFields::ReachEnvelopeCategory Reach) noexcept
{
    switch (Reach)
    {
        case SimulationSpace::ForceFields::ReachEnvelopeCategory::Sphere:      return "Sphere";
        case SimulationSpace::ForceFields::ReachEnvelopeCategory::Column:      return "Column";
        case SimulationSpace::ForceFields::ReachEnvelopeCategory::OrientedBox: return "OrientedBox";
        case SimulationSpace::ForceFields::ReachEnvelopeCategory::PlanarBox:   return "PlanarBox";
    }
    return "Sphere";
}

[[nodiscard]] const char* AttenuationLabel(SimulationSpace::ForceFields::ForceAttenuationMode Atten) noexcept
{
    switch (Atten)
    {
        case SimulationSpace::ForceFields::ForceAttenuationMode::SmoothStep:    return "SmoothStep";
        case SimulationSpace::ForceFields::ForceAttenuationMode::Linear:        return "Linear";
        case SimulationSpace::ForceFields::ForceAttenuationMode::InverseSquare: return "InverseSquare";
        case SimulationSpace::ForceFields::ForceAttenuationMode::None:          return "None";
    }
    return "SmoothStep";
}

[[nodiscard]] const char* DurationLabel(SimulationSpace::ForceFields::ForceDurationMode Dur) noexcept
{
    switch (Dur)
    {
        case SimulationSpace::ForceFields::ForceDurationMode::Continuous: return "Continuous";
        case SimulationSpace::ForceFields::ForceDurationMode::Modulated:  return "Modulated";
        case SimulationSpace::ForceFields::ForceDurationMode::Impulse:    return "Impulse";
    }
    return "Continuous";
}

[[nodiscard]] const char* TopologyLabel(SimulationSpace::ForceFields::MotionPathTopology Topo) noexcept
{
    switch (Topo)
    {
        case SimulationSpace::ForceFields::MotionPathTopology::Static:        return "Static (None)";
        case SimulationSpace::ForceFields::MotionPathTopology::LinearSpan:    return "Linear A -> B";
        case SimulationSpace::ForceFields::MotionPathTopology::CatmullSpline: return "3D Spline Curve";
        case SimulationSpace::ForceFields::MotionPathTopology::HelicalOrbit:  return "Orbit Loop";
    }
    return "Static (None)";
}

[[nodiscard]] const char* CycleLabel(SimulationSpace::ForceFields::MotionCycleBehavior Cycle) noexcept
{
    switch (Cycle)
    {
        case SimulationSpace::ForceFields::MotionCycleBehavior::PingPong: return "PingPong";
        case SimulationSpace::ForceFields::MotionCycleBehavior::WrapLoop: return "Loop";
        case SimulationSpace::ForceFields::MotionCycleBehavior::PlayOnce: return "Once";
    }
    return "PingPong";
}

[[nodiscard]] const char* StreamProfileLabel(SimulationSpace::ForceFields::FlowStreamProfile Profile) noexcept
{
    switch (Profile)
    {
        case SimulationSpace::ForceFields::FlowStreamProfile::Current:         return "Current";
        case SimulationSpace::ForceFields::FlowStreamProfile::ThermalUpdraft:  return "ThermalUpdraft";
        case SimulationSpace::ForceFields::FlowStreamProfile::SlipstreamJet:   return "SlipstreamJet";
        case SimulationSpace::ForceFields::FlowStreamProfile::NavigationField: return "NavigationField";
        case SimulationSpace::ForceFields::FlowStreamProfile::VesselWake:      return "VesselWake";
    }
    return "Current";
}

[[nodiscard]] const char* HeatmapFieldLabel(SimulationSpace::ForceFields::HeatmapChannelMode Mode) noexcept
{
    switch (Mode)
    {
        case SimulationSpace::ForceFields::HeatmapChannelMode::Speed:      return "Speed";
        case SimulationSpace::ForceFields::HeatmapChannelMode::ForceAccel: return "Acceleration";
        case SimulationSpace::ForceFields::HeatmapChannelMode::Curl:       return "Curl";
        case SimulationSpace::ForceFields::HeatmapChannelMode::Divergence: return "Divergence";
        case SimulationSpace::ForceFields::HeatmapChannelMode::Pressure:   return "Pressure";
    }
    return "Speed";
}

[[nodiscard]] const char* ColorRampLabel(uint32_t Ordinal) noexcept
{
    static constexpr std::array<const char*, 4> Names = {{ "Engineering", "Turbo", "Plasma", "Diverging" }};
    return (Ordinal < Names.size()) ? Names[Ordinal] : Names[0];
}

[[nodiscard]] const char* StreamColorLabel(SimulationSpace::ForceFields::StreamlineColorMode Mode) noexcept
{
    switch (Mode)
    {
        case SimulationSpace::ForceFields::StreamlineColorMode::SpeedRamp:    return "Speed Color Ramp";
        case SimulationSpace::ForceFields::StreamlineColorMode::CyanSingle:   return "Single Color · Classic Cyan";
        case SimulationSpace::ForceFields::StreamlineColorMode::CustomSingle: return "Single Color · Custom";
        case SimulationSpace::ForceFields::StreamlineColorMode::Elevation:    return "Altitude 3D Gradient";
    }
    return "Speed Color Ramp";
}

[[nodiscard]] float ComputeRadialAttenuation(
    SimulationSpace::ForceFields::ForceAttenuationMode Mode,
    float                                              Distance,
    float                                              Radius) noexcept
{
    if (Radius <= 1.0e-5f || Distance >= Radius)
    {
        return 0.0f;
    }
    const float U = std::clamp(Distance / Radius, 0.0f, 1.0f);
    switch (Mode)
    {
        case SimulationSpace::ForceFields::ForceAttenuationMode::None:          return 1.0f;
        case SimulationSpace::ForceFields::ForceAttenuationMode::Linear:        return 1.0f - U;
        case SimulationSpace::ForceFields::ForceAttenuationMode::InverseSquare:
        {
            const float Tail = 1.0f - std::clamp((U - 0.82f) / 0.18f, 0.0f, 1.0f);
            return (1.0f / (1.0f + 9.0f * U * U)) * Tail;
        }
        case SimulationSpace::ForceFields::ForceAttenuationMode::SmoothStep:
        default:
        {
            const float Soft = 1.0f - U * U;
            return Soft * Soft;
        }
    }
}

[[nodiscard]] float ComputeDurationEnvelope(
    const SimulationSpace::ForceFields::ForceSourceRecordEntry& Src,
    double                                                      TimeSec) noexcept
{
    if (!Src.Enabled)
    {
        return 0.0f;
    }
    switch (Src.DurationMode)
    {
        case SimulationSpace::ForceFields::ForceDurationMode::Continuous:
            return 1.0f;
        case SimulationSpace::ForceFields::ForceDurationMode::Modulated:
        {
            const double Freq = std::max(0.02, static_cast<double>(Src.FrequencyHertz));
            return static_cast<float>(0.5 + 0.5 * std::sin(6.28318530718 * Freq * TimeSec));
        }
        case SimulationSpace::ForceFields::ForceDurationMode::Impulse:
        {
            const float Local = static_cast<float>(TimeSec) - Src.ImpulseTriggerTimeSec;
            if (Local < 0.0f)
            {
                return 0.0f;
            }
            const float HalfW = std::max(0.25f, Src.ImpulseDurationSec);
            return std::exp(-0.5f * (Local / HalfW));
        }
    }
    return 1.0f;
}

[[nodiscard]] ColorRgba UnpackRgbaHex(uint32_t Rgba) noexcept
{
    return ColorRgba{
        static_cast<uint8_t>((Rgba >> 24u) & 0xFFu),
        static_cast<uint8_t>((Rgba >> 16u) & 0xFFu),
        static_cast<uint8_t>((Rgba >> 8u)  & 0xFFu),
        static_cast<uint8_t>( Rgba         & 0xFFu),
    };
}

//------------------------------------------------------------------------------------------------------------------------
//                                           CONCISE VISUAL COMPONENT HELPERS
//------------------------------------------------------------------------------------------------------------------------

void ConstructGroupBox(PixelSpace& Surface, Rectangle2D Bounds) noexcept
{
    Surface.FillRectangle(Bounds, InspectorSurfaceColors::PanelBg, 22.0f);
    Surface.StrokeRectangle(Bounds, InspectorSurfaceColors::HairlineBorder, 22.0f, 1.0f);
}

void ConstructCapsuleButton(
    PixelSpace&             Surface,
    Rectangle2D             Bounds,
    std::string_view        Caption,
    bool                    Selected,
    bool                    Hovered,
    float                   FontSize,
    ColorRgba               TextInk) noexcept
{
    const float Radius = Bounds.Height * 0.5f;
    const ColorRgba Bg = Selected
        ? InspectorSurfaceColors::TabActiveBg
        : (Hovered ? InspectorSurfaceColors::TabHoverBg : InspectorSurfaceColors::TabInactiveBg);
    const ColorRgba Border = Selected
        ? ColorRgba{ 255, 255, 255, 42 }
        : InspectorSurfaceColors::HairlineBorder;

    Surface.FillRectangle(Bounds, Bg, Radius);
    Surface.StrokeRectangle(Bounds, Border, Radius, 1.0f);

    const float Tw = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Medium, FontSize, Caption);
    WorkspaceAppearance::ConstructTruncatedText(
        Surface,
        FontWeightClass::Medium,
        { Bounds.X + std::max(6.0f, (Bounds.Width - Tw) * 0.5f), Bounds.Y + (Bounds.Height - FontSize * 1.15f) * 0.5f },
        std::max(10.0f, Bounds.Width - 10.0f),
        FontSize,
        TextInk,
        Caption);
}

bool ConstructSelectTrigger(
    PixelSpace&            Surface,
    Rectangle2D            Bounds,
    const char*            Text,
    bool                   Open,
    bool                   Interactive,
    const ControlPointer&  Pointer) noexcept
{
    const bool Hovered = Interactive && Bounds.Contains({ Pointer.X, Pointer.Y });
    Surface.FillRectangle(Bounds, Hovered ? ColorRgba{ 24, 24, 28, 255 } : InspectorSurfaceColors::RecessDark, 10.0f);
    Surface.StrokeRectangle(Bounds, Open ? InspectorSurfaceColors::AmberAccent : InspectorSurfaceColors::SubtleBorder, 10.0f, 1.0f);

    WorkspaceAppearance::ConstructTruncatedText(
        Surface,
        FontWeightClass::Regular,
        { Bounds.X + 10.0f, Bounds.Y + 8.5f },
        std::max(16.0f, Bounds.Width - 26.0f),
        10.5f,
        InspectorSurfaceColors::InkHeading,
        Text);

    VectorIconRepository::ConstructControl(
        Surface,
        Open ? ControlIcon::ChevronUp : ControlIcon::ChevronDown,
        VectorIconPlacement{ Bounds.Right() - 18.0f, Bounds.Y + 9.0f, 12.0f, InspectorSurfaceColors::InkMuted, 1.6f });

    return Hovered && Pointer.Pressed;
}

bool ConstructGreenSwitchRow(
    PixelSpace&             Surface,
    Rectangle2D             Bounds,
    const char*             Caption,
    bool&                   ToggleBit,
    bool                    Interactive,
    const PaneInputReading& Input) noexcept
{
    const bool Hovered = Interactive && Bounds.Contains({ Input.PrimaryPointer.X, Input.PrimaryPointer.Y });
    Surface.FillRectangle(Bounds, Hovered ? ColorRgba{ 29, 29, 32, 255 } : InspectorSurfaceColors::SubWellBg, 14.0f);
    Surface.StrokeRectangle(Bounds, InspectorSurfaceColors::HairlineBorder, 14.0f, 1.0f);

    WorkspaceAppearance::ConstructTruncatedText(
        Surface,
        FontWeightClass::Regular,
        { Bounds.X + 11.0f, Bounds.Y + (Bounds.Height - 11.0f) * 0.5f },
        std::max(24.0f, Bounds.Width - 54.0f),
        10.0f,
        ToggleBit ? InspectorSurfaceColors::InkPrimary : InspectorSurfaceColors::InkMuted,
        Caption);

    const Rectangle2D Track{ Bounds.Right() - 40.0f, Bounds.Center().Y - 9.0f, 30.0f, 18.0f };
    Surface.FillRectangle(Track, ToggleBit ? InspectorSurfaceColors::SwitchGreen : InspectorSurfaceColors::SwitchOff, 9.0f);
    const float KnobX = ToggleBit ? (Track.Right() - 9.0f) : (Track.X + 9.0f);
    Surface.FillCircle({ KnobX, Track.Center().Y }, 7.0f, ColorRgba{ 248, 248, 250, 255 });

    if (Hovered && Input.PrimaryPointer.Pressed)
    {
        ToggleBit = !ToggleBit;
        return true;
    }
    return false;
}

void ConstructSplitNumericReadout(
    PixelSpace& Surface,
    Rectangle2D Bounds,
    const char* NumberText,
    const char* UnitText) noexcept
{
    const float Radius   = Bounds.Height * 0.5f;
    const float UnitW    = (UnitText != nullptr && UnitText[0] != '\0') ? 30.0f : 0.0f;
    const float DividerX = Bounds.Right() - UnitW;

    Surface.FillRectangle(Bounds, InspectorSurfaceColors::RecessDark, Radius);
    Surface.StrokeRectangle(Bounds, InspectorSurfaceColors::HairlineBorder, Radius, 1.0f);
    if (UnitW > 0.0f)
    {
        Surface.StrokeLine(
            { DividerX, Bounds.Y + 4.0f },
            { DividerX, Bounds.Bottom() - 4.0f },
            ColorRgba{ 255, 255, 255, 18 },
            1.0f);
        const float Uw = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 9.0f, UnitText);
        WorkspaceAppearance::ConstructWeightedText(
            Surface,
            FontWeightClass::Regular,
            { DividerX + (UnitW - Uw) * 0.5f, Bounds.Y + 7.5f },
            9.0f,
            InspectorSurfaceColors::InkMuted,
            UnitText);
    }

    const float NumW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 10.5f, NumberText);
    const float LeftSpan = std::max(12.0f, DividerX - Bounds.X);
    WorkspaceAppearance::ConstructTruncatedText(
        Surface,
        FontWeightClass::Regular,
        { Bounds.X + std::max(6.0f, (LeftSpan - NumW) * 0.5f), Bounds.Y + 7.0f },
        LeftSpan - 8.0f,
        10.5f,
        InspectorSurfaceColors::InkPrimary,
        NumberText);
}

void ConstructProjectZeroSliderRow(
    PixelSpace&             Surface,
    float                   LeftX,
    float                   TopY,
    float                   InnerW,
    const char*             Caption,
    const char*             UnitHint,
    float                   Minimum,
    float                   Maximum,
    float&                  ScalarReading,
    uint32_t                DecimalDigits,
    int32_t                 SliderId,
    int32_t&                ActiveSliderId,
    bool                    Interactive,
    const PaneInputReading& Input) noexcept
{
    WorkspaceAppearance::ConstructWeightedText(
        Surface,
        FontWeightClass::Regular,
        { LeftX, TopY },
        10.5f,
        InspectorSurfaceColors::InkLabel,
        Caption);
    if (UnitHint != nullptr && UnitHint[0] != '\0')
    {
        WorkspaceAppearance::ConstructRightAlignedText(
            Surface,
            FontWeightClass::Regular,
            { LeftX + InnerW, TopY + 0.5f },
            9.5f,
            InspectorSurfaceColors::InkMuted,
            UnitHint);
    }

    const float       SliderTop = TopY + 17.0f;
    constexpr float   ReadoutW  = 92.0f;
    constexpr float   Gap       = 8.0f;
    const float       SliderW   = std::max(40.0f, InnerW - ReadoutW - Gap);
    const Rectangle2D TrackRect{ LeftX, SliderTop, SliderW, 26.0f };

    ControlPointer AdjPtr = Input.PrimaryPointer;
    AdjPtr.Enabled        = Interactive;
    float Updated         = ScalarReading;
    const ControlContact Hit = ScalarSlider::Construct(
        Surface,
        TrackRect,
        Minimum,
        Maximum,
        ScalarReading,
        ActiveSliderId == SliderId,
        AdjPtr,
        Updated,
        1.0f,
        26.0f,
        24.0f);

    if (Hit.Pressed)
    {
        ActiveSliderId = SliderId;
    }
    if (ActiveSliderId == SliderId && Input.PrimaryPointer.Down && Interactive)
    {
        ScalarReading = std::clamp(Updated, std::min(Minimum, Maximum), std::max(Minimum, Maximum));
    }

    char NumBuf[32]{};
    std::snprintf(NumBuf, sizeof(NumBuf), "%.*f", static_cast<int>(DecimalDigits), ScalarReading);
    ConstructSplitNumericReadout(Surface, { LeftX + SliderW + Gap, SliderTop, ReadoutW, 26.0f }, NumBuf, UnitHint);
}

void ConstructAxisTripletRow(
    PixelSpace&             Surface,
    float                   LeftX,
    float                   TopY,
    float                   InnerW,
    SimulationSpace::ForceFields::SpatialVector3& Coord,
    int32_t                 FirstAxisTagId,
    int32_t&                ActiveAxisDragId,
    float&                  DragStartX,
    float&                  DragStartVal,
    bool                    Interactive,
    const PaneInputReading& Input,
    bool*                   OutModified = nullptr) noexcept
{
    static constexpr std::array<const char*, 3> Axes   = {{ "X", "Y", "Z" }};
    static constexpr std::array<ColorRgba, 3>   AxisInk = {{
        InspectorSurfaceColors::AxisRed,
        InspectorSurfaceColors::AxisGreen,
        InspectorSurfaceColors::AxisBlue,
    }};

    const float Gap  = 6.0f;
    const float CellW = (InnerW - Gap * 2.0f) / 3.0f;
    std::array<float*, 3> Fields = {{ &Coord.X, &Coord.Y, &Coord.Z }};
    const Point2D Cursor{ Input.PrimaryPointer.X, Input.PrimaryPointer.Y };

    for (std::size_t Ax = 0u; Ax < 3u; ++Ax)
    {
        const Rectangle2D Box{ LeftX + static_cast<float>(Ax) * (CellW + Gap), TopY, CellW, 28.0f };
        const int32_t     AxisTag = FirstAxisTagId + static_cast<int32_t>(Ax);
        const bool        Hovered = Interactive && Box.Contains(Cursor);

        if (Hovered && Input.PrimaryPointer.Pressed)
        {
            ActiveAxisDragId = AxisTag;
            DragStartX       = Cursor.X;
            DragStartVal     = *Fields[Ax];
        }
        if (ActiveAxisDragId == AxisTag && Input.PrimaryPointer.Down && Interactive)
        {
            const float Sensitivity = Input.ShiftHeld ? 0.05f : 0.25f;
            const float NextVal     = DragStartVal + (Cursor.X - DragStartX) * Sensitivity;
            if (std::fabs(NextVal - *Fields[Ax]) > 1.0e-4f)
            {
                *Fields[Ax] = NextVal;
                if (OutModified != nullptr)
                {
                    *OutModified = true;
                }
            }
        }

        Surface.FillRectangle(Box, Hovered ? ColorRgba{ 22, 22, 26, 255 } : InspectorSurfaceColors::RecessDark, 12.0f);
        Surface.StrokeRectangle(
            Box,
            (ActiveAxisDragId == AxisTag) ? InspectorSurfaceColors::AmberAccent : InspectorSurfaceColors::SubtleBorder,
            12.0f,
            1.0f);

        WorkspaceAppearance::ConstructWeightedText(
            Surface,
            FontWeightClass::SemiBold,
            { Box.X + 9.0f, Box.Y + 7.5f },
            10.0f,
            AxisInk[Ax],
            Axes[Ax]);

        char NumBuf[24]{};
        std::snprintf(NumBuf, sizeof(NumBuf), "%.1f", *Fields[Ax]);
        WorkspaceAppearance::ConstructRightAlignedText(
            Surface,
            FontWeightClass::Regular,
            { Box.Right() - 9.0f, Box.Y + 7.5f },
            10.5f,
            InspectorSurfaceColors::InkPrimary,
            NumBuf);
    }
}

void ConstructHighlightedFormulaBox(
    PixelSpace&      Surface,
    Rectangle2D      Bounds,
    std::string_view Code,
    bool             CompileOk) noexcept
{
    Surface.FillRectangle(Bounds, InspectorSurfaceColors::EditorBg, 10.0f);
    Surface.StrokeRectangle(
        Bounds,
        CompileOk ? ColorRgba{ 255, 255, 255, 26 } : ColorRgba{ 224, 96, 96, 180 },
        10.0f,
        1.0f);

    float CurX = Bounds.X + 10.0f;
    float CurY = Bounds.Y + 9.0f;
    const float LineH   = 15.0f;
    const float RightLim = Bounds.Right() - 10.0f;

    std::size_t Pos = 0u;
    while (Pos < Code.size() && (CurY + LineH) <= (Bounds.Bottom() - 6.0f))
    {
        if (Code[Pos] == '\n')
        {
            CurX = Bounds.X + 10.0f;
            CurY += LineH;
            ++Pos;
            continue;
        }
        if (Code[Pos] == ' ' || Code[Pos] == '\t')
        {
            CurX += 6.2f;
            ++Pos;
            continue;
        }

        // Tokenize keyword, operator, function, number, or identifier for crisp syntax coloring
        std::size_t End = Pos + 1u;
        ColorRgba   Ink = InspectorSurfaceColors::InkPrimary;

        if (std::isdigit(static_cast<unsigned char>(Code[Pos])) || Code[Pos] == '.')
        {
            while (End < Code.size() && (std::isdigit(static_cast<unsigned char>(Code[End])) || Code[End] == '.'))
            {
                ++End;
            }
            Ink = ColorRgba{ 232, 208, 132, 255 }; // Gold numeric literal
        }
        else if (std::isalpha(static_cast<unsigned char>(Code[Pos])) || Code[Pos] == '_')
        {
            while (End < Code.size() && (std::isalnum(static_cast<unsigned char>(Code[End])) || Code[End] == '_'))
            {
                ++End;
            }
            const std::string_view Tok = Code.substr(Pos, End - Pos);
            if (Tok == "Flow" || Tok == "Accelerate" || Tok == "Damp" || Tok == "Torque")
            {
                Ink = ColorRgba{ 255, 140, 107, 255 }; // Channel keyword
            }
            else if (Tok == "let" || Tok == "return")
            {
                Ink = ColorRgba{ 255, 180, 84, 255 };  // Amber binding keyword
            }
            else if (Tok == "r_hat" || Tok == "x_hat" || Tok == "y_hat" || Tok == "z_hat" || Tok == "theta_hat")
            {
                Ink = ColorRgba{ 120, 210, 198, 255 }; // Teal basis vector
            }
            else if (Tok == "curlNoise" || Tok == "noise3" || Tok == "normalize" || Tok == "cross" ||
                     Tok == "dot" || Tok == "sin" || Tok == "cos" || Tok == "exp" || Tok == "A")
            {
                Ink = ColorRgba{ 114, 182, 249, 255 }; // Blue builtin
            }
            else
            {
                Ink = ColorRgba{ 222, 228, 234, 255 };
            }
        }
        else
        {
            Ink = ColorRgba{ 232, 168, 124, 255 }; // Operators & punctuation
        }

        const std::string_view TokenStr = Code.substr(Pos, End - Pos);
        const float TokW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 10.0f, TokenStr, true);
        if (CurX + TokW > RightLim)
        {
            CurX = Bounds.X + 10.0f;
            CurY += LineH;
            if ((CurY + LineH) > (Bounds.Bottom() - 6.0f))
            {
                break;
            }
        }
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { CurX, CurY }, 10.0f, Ink, TokenStr, true);
        CurX += TokW;
        Pos = End;
    }
}

void ConstructBeaufortGradientBar(PixelSpace& Surface, Rectangle2D BarBounds, float SpeedMs) noexcept
{
    static constexpr std::array<ColorRgba, 5> Stops = {{
        { 63,  88,  102, 255 }, // #3f5866
        { 88,  153, 121, 255 }, // #589979
        { 185, 169, 105, 255 }, // #b9a969
        { 219, 132, 70,  255 }, // #db8446
        { 218, 71,  60,  255 }, // #da473c
    }};

    const uint32_t Segments = 24u;
    const float    SegW     = BarBounds.Width / static_cast<float>(Segments);
    for (uint32_t S = 0u; S < Segments; ++S)
    {
        const float T     = (static_cast<float>(S) + 0.5f) / static_cast<float>(Segments);
        const float FSpan = T * static_cast<float>(Stops.size() - 1u);
        const auto  Idx0  = std::min<std::size_t>(Stops.size() - 2u, static_cast<std::size_t>(FSpan));
        const float Local = FSpan - static_cast<float>(Idx0);
        const auto& C0    = Stops[Idx0];
        const auto& C1    = Stops[Idx0 + 1u];
        const ColorRgba Blend = ColorRgba::Blend(C0, C1, Local);
        const Rectangle2D Slice{ BarBounds.X + static_cast<float>(S) * SegW, BarBounds.Y, SegW + 0.8f, BarBounds.Height };
        Surface.FillRectangle(Slice, Blend, (S == 0u || S + 1u == Segments) ? 5.0f : 0.0f);
    }

    const float Share = std::clamp(SpeedMs / 36.0f, 0.0f, 0.985f);
    const float PinX  = BarBounds.X + Share * BarBounds.Width;
    Surface.FillRectangle({ PinX - 2.0f, BarBounds.Y - 3.0f, 4.0f, BarBounds.Height + 6.0f }, ColorRgba{ 244, 244, 246, 255 }, 2.0f);
}

void ConstructRadialEnvelopePlot(
    PixelSpace&                                                 Surface,
    Rectangle2D                                                 PlotBox,
    const SimulationSpace::ForceFields::ForceSourceRecordEntry& Src,
    double                                                      SimTimeSec) noexcept
{
    Surface.FillRectangle(PlotBox, InspectorSurfaceColors::InsetBg, 12.0f);
    Surface.StrokeRectangle(PlotBox, InspectorSurfaceColors::HairlineBorder, 12.0f, 1.0f);

    const float PadL = 10.0f, PadR = 10.0f, PadT = 10.0f, PadB = 18.0f;
    const float IW   = std::max(20.0f, PlotBox.Width - PadL - PadR);
    const float IH   = std::max(14.0f, PlotBox.Height - PadT - PadB);
    const float AxisBottomY = PlotBox.Y + PadT + IH;
    const float MaxR  = std::max(8.0f, Src.RadiusMeters * 1.25f);
    const float RLineX = PlotBox.X + PadL + (Src.RadiusMeters / MaxR) * IW;

    // Horizontal baseline & vertical dashed R cutoff
    Surface.StrokeLine({ PlotBox.X + PadL, AxisBottomY }, { PlotBox.Right() - PadR, AxisBottomY }, ColorRgba{ 255, 255, 255, 32 }, 1.0f);
    for (float Dy = PlotBox.Y + PadT; Dy < AxisBottomY - 2.0f; Dy += 6.0f)
    {
        Surface.StrokeLine({ RLineX, Dy }, { RLineX, std::min(AxisBottomY, Dy + 3.0f) }, ColorRgba{ 255, 180, 84, 115 }, 1.0f);
    }

    constexpr std::size_t Samples = 41u;
    std::vector<Point2D>  SharePts(Samples);
    std::vector<Point2D>  MagPts(Samples);
    for (std::size_t I = 0u; I < Samples; ++I)
    {
        const float U     = static_cast<float>(I) / static_cast<float>(Samples - 1u);
        const float R     = U * MaxR;
        const float Share = ComputeRadialAttenuation(Src.Attenuation, R, Src.RadiusMeters);
        float       Shape = Share;
        if (Src.Model == SimulationSpace::ForceFields::ForceModel::RotationalColumn)
        {
            const float Rho = R / std::max(1.0f, Src.CoreRadiusMeters);
            Shape           = Share * (2.0f * Rho) / (1.0f + Rho * Rho);
        }
        else if (Src.Model == SimulationSpace::ForceFields::ForceModel::RadialBurst)
        {
            const float Shell = std::exp(-0.5f * std::pow((R - Src.RadiusMeters * 0.45f) / std::max(2.0f, Src.RadiusMeters * 0.14f), 2.0f));
            Shape             = Share * Shell;
        }

        const float X = PlotBox.X + PadL + U * IW;
        SharePts[I]   = { X, PlotBox.Y + PadT + (1.0f - std::clamp(Share, 0.0f, 1.0f)) * IH };
        MagPts[I]     = { X, PlotBox.Y + PadT + (1.0f - std::clamp(Shape, 0.0f, 1.0f)) * IH };
    }

    // Dashed teal A(r) & solid amber magnitude curve
    for (std::size_t I = 0u; I + 1u < Samples; I += 2u)
    {
        Surface.StrokeLine(SharePts[I], SharePts[I + 1u], ColorRgba{ 134, 211, 189, 136 }, 1.3f);
    }
    Surface.StrokePolyline(MagPts, InspectorSurfaceColors::AmberAccent, 1.85f, false);

    // Axis ticks bottom
    WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { PlotBox.X + PadL, AxisBottomY + 4.0f }, 8.5f, InspectorSurfaceColors::InkDim, "0 m");
    char RBuf[32]{};
    std::snprintf(RBuf, sizeof(RBuf), "R=%.0fm", Src.RadiusMeters);
    WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { RLineX - 14.0f, AxisBottomY + 4.0f }, 8.5f, ColorRgba{ 217, 168, 97, 255 }, RBuf);

    char EnvBuf[32]{};
    std::snprintf(EnvBuf, sizeof(EnvBuf), "g(t)=%.2f", ComputeDurationEnvelope(Src, SimTimeSec));
    WorkspaceAppearance::ConstructRightAlignedText(Surface, FontWeightClass::Regular, { PlotBox.Right() - PadR, AxisBottomY + 4.0f }, 8.5f, InspectorSurfaceColors::InkDim, EnvBuf);
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                              CONFIGURATION & ACCESSORS
//------------------------------------------------------------------------------------------------------------------------

void ForceInspectorView::ConfigureActiveTab(ForceInspectorTab Tab) noexcept
{
    if (ActiveTab != Tab)
    {
        ActiveTab     = Tab;
        ScrollOffsetY = 0.0f;
        OpenDropdown  = OpenDropdownCategory::None;
    }
}

void ForceInspectorView::ConfigureScrollOffset(float OffsetY) noexcept
{
    ScrollOffsetY = std::max(0.0f, OffsetY);
}

ForceInspectorTab ForceInspectorView::QueryActiveTab() const noexcept
{
    return ActiveTab;
}

float ForceInspectorView::QueryScrollOffset() const noexcept
{
    return ScrollOffsetY;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                 TOP-LEVEL RENDER
//------------------------------------------------------------------------------------------------------------------------

bool ForceInspectorView::Render(
    PixelSpace&                       Surface,
    Rectangle2D                       Bounds,
    const ForceInspectorViewTargets& Targets,
    const PaneInputReading&           Input,
    double                            Δt)
{
    if (Bounds.Width <= 4.0f || Bounds.Height <= 4.0f)
    {
        return false;
    }

    if (CopyAckTimerSec > 0.0)
    {
        CopyAckTimerSec = std::max(0.0, CopyAckTimerSec - Δt);
        if (CopyAckTimerSec <= 0.0)
        {
            FormulaCopiedAck = false;
        }
    }

    if (!Input.PrimaryPointer.Down)
    {
        ActiveSliderId   = -1;
        ActiveAxisDragId = -1;
    }

    const Point2D Cursor{ Input.PrimaryPointer.X, Input.PrimaryPointer.Y };
    const bool    SheetActiveBefore = (OpenDropdown != OpenDropdownCategory::None);

    Surface.PushClip(Bounds);
    Surface.FillRectangle(Bounds, InspectorSurfaceColors::RootBg);

    // 1. Header (54px): ForceModel 18x18 Icon + Source Name + Delete Capsule
    constexpr float   HeaderH = 54.0f;
    const Rectangle2D HeaderBounds{ Bounds.X, Bounds.Y, Bounds.Width, HeaderH };
    Surface.FillRectangle(HeaderBounds, InspectorSurfaceColors::RootBg);
    Surface.StrokeLine({ HeaderBounds.X, HeaderBounds.Bottom() }, { HeaderBounds.Right(), HeaderBounds.Bottom() }, InspectorSurfaceColors::HairlineBorder, 1.0f);

    const Rectangle2D DeleteBtn{ HeaderBounds.Right() - 16.0f - 58.0f, HeaderBounds.Y + 15.0f, 58.0f, 24.0f };
    const float       IconStartX = HeaderBounds.X + 16.0f;
    float             TitleX     = IconStartX;

    if (Targets.ActiveForce != nullptr)
    {
        const uint32_t  LawOrd = static_cast<uint32_t>(Targets.ActiveForce->Model);
        const ColorRgba LawInk = Targets.ActiveForce->FormulaEnabled
            ? InspectorSurfaceColors::AmberAccent
            : WorkspaceAppearance::QueryForceModelTint(LawOrd);
        WorkspaceAppearance::ConstructForceModelIcon(
            Surface,
            LawOrd,
            { IconStartX, HeaderBounds.Center().Y - 9.0f },
            18.0f,
            LawInk);
        TitleX += 26.0f;
    }

    const std::string_view HeaderTitle = (Targets.ActiveForce != nullptr)
        ? std::string_view(Targets.ActiveForce->Label)
        : (Targets.OutlinerEntry != nullptr ? std::string_view(Targets.OutlinerEntry->Title) : std::string_view("No Source Selected"));
    const float TitleMaxW = std::max(40.0f, DeleteBtn.X - TitleX - 10.0f);
    WorkspaceAppearance::ConstructTruncatedText(
        Surface,
        FontWeightClass::Light,
        { TitleX, HeaderBounds.Y + 17.5f },
        TitleMaxW,
        16.0f,
        InspectorSurfaceColors::InkPrimary,
        HeaderTitle);

    const bool DelHovered = !SheetActiveBefore && DeleteBtn.Contains(Cursor);
    ConstructCapsuleButton(
        Surface,
        DeleteBtn,
        "Delete",
        false,
        DelHovered,
        10.0f,
        DelHovered ? InspectorSurfaceColors::InkPrimary : ColorRgba{ 180, 185, 192, 255 });
    if (DelHovered && Input.PrimaryPointer.Pressed && Targets.Outliner != nullptr && Targets.OutlinerEntry != nullptr)
    {
        Targets.Outliner->DeleteSlot(Targets.OutlinerEntry->Token);
    }

    // 2. Tabs Bar (38px: 8px top, 26px height, 4px bottom, 14px left/right)
    constexpr float   TabBarH = 38.0f;
    const Rectangle2D TabBar{ Bounds.X, HeaderBounds.Bottom(), Bounds.Width, TabBarH };
    const float       TabGap  = 4.0f;
    const float       TabLeft = TabBar.X + 14.0f;
    const float       TabAreaW = std::max(80.0f, TabBar.Width - 28.0f);
    const float       TabW    = (TabAreaW - TabGap * 3.0f) * 0.25f;

    static constexpr std::array<std::pair<ForceInspectorTab, const char*>, 4> Tabs = {{
        { ForceInspectorTab::Field,   "Field"   },
        { ForceInspectorTab::Formula, "Formula" },
        { ForceInspectorTab::Layers,  "Layers"  },
        { ForceInspectorTab::Cache,   "Cache"   },
    }};
    for (std::size_t TIdx = 0u; TIdx < Tabs.size(); ++TIdx)
    {
        const Rectangle2D TabRect{ TabLeft + static_cast<float>(TIdx) * (TabW + TabGap), TabBar.Y + 8.0f, TabW, 26.0f };
        const bool        Selected = (ActiveTab == Tabs[TIdx].first);
        const bool        Hovered  = !SheetActiveBefore && TabRect.Contains(Cursor);
        ConstructCapsuleButton(
            Surface,
            TabRect,
            Tabs[TIdx].second,
            Selected,
            Hovered,
            10.0f,
            Selected ? InspectorSurfaceColors::InkPrimary : InspectorSurfaceColors::InkMuted);
        if (Hovered && Input.PrimaryPointer.Pressed)
        {
            ConfigureActiveTab(Tabs[TIdx].first);
        }
    }

    // 3. Scroll Viewport & Footer Geometry
    constexpr float   FooterH = 45.0f;
    const Rectangle2D ScrollVp{
        Bounds.X,
        TabBar.Bottom(),
        Bounds.Width,
        std::max(1.0f, Bounds.Bottom() - FooterH - TabBar.Bottom())
    };

    if (!SheetActiveBefore && ScrollVp.Contains(Cursor) && std::fabs(Input.WheelDeltaY) > 1.0e-4f)
    {
        ScrollOffsetY -= Input.WheelDeltaY * 42.0f;
    }
    ScrollOffsetY = std::max(0.0f, ScrollOffsetY);
    if (LastContentH > 0.0f)
    {
        ScrollOffsetY = std::min(ScrollOffsetY, std::max(0.0f, LastContentH - ScrollVp.Height));
    }

    Surface.PushClip(ScrollVp);

    const float          LeftX   = ScrollVp.X + 14.0f;
    const float          InnerW  = std::max(120.0f, ScrollVp.Width - 28.0f);
    const float          OriginY = ScrollVp.Y + 8.0f - ScrollOffsetY;
    float                HeightUsed = 0.0f;
    DeferredSheetOverlay Deferred{};

    switch (ActiveTab)
    {
        case ForceInspectorTab::Field:
            HeightUsed = RenderFieldTab(Surface, ScrollVp, LeftX, OriginY, InnerW, Targets, Input, Deferred);
            break;
        case ForceInspectorTab::Formula:
            HeightUsed = RenderFormulaTab(Surface, ScrollVp, LeftX, OriginY, InnerW, Targets, Input);
            break;
        case ForceInspectorTab::Layers:
            HeightUsed = RenderLayersTab(Surface, ScrollVp, LeftX, OriginY, InnerW, Targets, Input, Deferred);
            break;
        case ForceInspectorTab::Cache:
            HeightUsed = RenderCacheTab(Surface, ScrollVp, LeftX, OriginY, InnerW, Targets, Input);
            break;
    }
    LastContentH = HeightUsed + 24.0f;
    ScrollOffsetY = std::clamp(ScrollOffsetY, 0.0f, std::max(0.0f, LastContentH - ScrollVp.Height));

    WorkspaceAppearance::ConstructVerticalScrollIndicator(Surface, ScrollVp, LastContentH, ScrollOffsetY, Cursor);
    Surface.PopClip();

    // 4. Footer (45px height, #151515)
    const Rectangle2D Footer{ Bounds.X, Bounds.Bottom() - FooterH, Bounds.Width, FooterH };
    Surface.FillRectangle(Footer, InspectorSurfaceColors::FooterBg);
    Surface.StrokeLine({ Footer.X, Footer.Y }, { Footer.Right(), Footer.Y }, InspectorSurfaceColors::HairlineBorder, 1.0f);

    char SlotSummary[64]{};
    const std::size_t ActiveSlotCount = (Targets.Accumulator != nullptr) ? Targets.Accumulator->QuerySources().size() : 0u;
    std::snprintf(SlotSummary, sizeof(SlotSummary), "EvalCompositeGPU · %zu / 64 slots", ActiveSlotCount);
    WorkspaceAppearance::ConstructWeightedText(
        Surface,
        FontWeightClass::Regular,
        { Footer.X + 15.0f, Footer.Y + 16.5f },
        10.0f,
        InspectorSurfaceColors::InkMuted,
        SlotSummary);
    WorkspaceAppearance::ConstructRightAlignedText(
        Surface,
        FontWeightClass::Regular,
        { Footer.Right() - 15.0f, Footer.Y + 16.5f },
        10.0f,
        ColorRgba{ 146, 154, 160, 255 },
        "SimulationSpace Ready");

    // 5. Render Any Open Dropdown Option Sheet Above Scroll Clip (Zero Clipping)
    if (OpenDropdown != OpenDropdownCategory::None)
    {
        std::vector<const char*> Options;
        uint32_t                 CurrentIdx = 0u;

        if (OpenDropdown == OpenDropdownCategory::ForceModelSelector && Targets.ActiveForce != nullptr)
        {
            for (const auto& Law : ForceModelRepository)
            {
                Options.push_back(Law.DisplayTitle);
            }
            CurrentIdx = static_cast<uint32_t>(Targets.ActiveForce->Model);
        }
        else if (OpenDropdown == OpenDropdownCategory::ReachShape && Targets.ActiveForce != nullptr)
        {
            Options = { "Sphere", "Column", "OrientedBox", "PlanarBox" };
            CurrentIdx = static_cast<uint32_t>(Targets.ActiveForce->Envelope);
        }
        else if (OpenDropdown == OpenDropdownCategory::Attenuation && Targets.ActiveForce != nullptr)
        {
            Options = { "SmoothStep", "Linear", "InverseSquare", "None" };
            CurrentIdx = static_cast<uint32_t>(Targets.ActiveForce->Attenuation);
        }
        else if (OpenDropdown == OpenDropdownCategory::DurationMode && Targets.ActiveForce != nullptr)
        {
            Options = { "Continuous", "Modulated", "Impulse" };
            CurrentIdx = static_cast<uint32_t>(Targets.ActiveForce->DurationMode);
        }
        else if (OpenDropdown == OpenDropdownCategory::MotionTopology && Targets.ActiveForce != nullptr)
        {
            Options = { "Static (None)", "Linear A -> B", "3D Spline Curve", "Orbit Loop" };
            CurrentIdx = static_cast<uint32_t>(Targets.ActiveForce->Motion.Topology);
        }
        else if (OpenDropdown == OpenDropdownCategory::MotionCycle && Targets.ActiveForce != nullptr)
        {
            Options = { "PingPong", "Loop", "Once" };
            CurrentIdx = static_cast<uint32_t>(Targets.ActiveForce->Motion.CycleBehavior);
        }
        else if (OpenDropdown == OpenDropdownCategory::StreamProfile && Targets.ActiveForce != nullptr)
        {
            Options = { "Current", "ThermalUpdraft", "SlipstreamJet", "NavigationField", "VesselWake" };
            CurrentIdx = static_cast<uint32_t>(Targets.ActiveForce->StreamProfile);
        }
        else if (OpenDropdown == OpenDropdownCategory::HeatmapField && Targets.OverlaySettings != nullptr)
        {
            Options = { "Speed", "Acceleration", "Curl", "Divergence", "Pressure" };
            CurrentIdx = static_cast<uint32_t>(Targets.OverlaySettings->HeatmapChannel);
        }
        else if (OpenDropdown == OpenDropdownCategory::ColorRamp && Targets.OverlaySettings != nullptr)
        {
            Options = { "Engineering", "Turbo", "Plasma", "Diverging" };
            CurrentIdx = std::min(Targets.OverlaySettings->HeatmapColorSchemeOrdinal, 3u);
        }
        else if (OpenDropdown == OpenDropdownCategory::StreamColorMode && Targets.OverlaySettings != nullptr)
        {
            Options = { "Speed Color Ramp", "Single Color · Classic Cyan", "Single Color · Custom", "Altitude 3D Gradient" };
            CurrentIdx = static_cast<uint32_t>(Targets.OverlaySettings->StreamColorMode);
        }

        if (!Options.empty())
        {
            const Rectangle2D SheetRaw = DropdownSelector::QuerySheetBounds(OpenTriggerRect, static_cast<uint32_t>(Options.size()));
            const Rectangle2D AnchorBounds{
                OpenTriggerRect.X,
                std::min(OpenTriggerRect.Y, std::max(Bounds.Y + 8.0f, Bounds.Bottom() - SheetRaw.Height - OpenTriggerRect.Height - 10.0f)),
                std::max(OpenTriggerRect.Width, 168.0f),
                OpenTriggerRect.Height,
            };
            const Rectangle2D SheetBounds = DropdownSelector::QuerySheetBounds(AnchorBounds, static_cast<uint32_t>(Options.size()));
            uint32_t          ChosenIdx   = CurrentIdx;

            if (DropdownSelector::ConstructOptionSheet(
                    Surface,
                    AnchorBounds,
                    Options.data(),
                    static_cast<uint32_t>(Options.size()),
                    CurrentIdx,
                    Input.PrimaryPointer,
                    ChosenIdx).Clicked)
            {
                if (OpenDropdown == OpenDropdownCategory::ForceModelSelector && Targets.ActiveForce != nullptr)
                {
                    Targets.ActiveForce->Model  = static_cast<SimulationSpace::ForceFields::ForceModel>(ChosenIdx);
                    Targets.ActiveForce->Domain = SimulationSpace::ForceFields::QueryDomainForModel(Targets.ActiveForce->Model);
                    if (Targets.OutlinerEntry != nullptr)
                    {
                        Targets.OutlinerEntry->ForceModelOrdinal = ChosenIdx;
                        Targets.OutlinerEntry->CategoryLabel   = QueryForceModelDescriptor(Targets.ActiveForce->Model).DisplayTitle;
                    }
                }
                else if (OpenDropdown == OpenDropdownCategory::ReachShape && Targets.ActiveForce != nullptr)
                {
                    Targets.ActiveForce->Envelope = static_cast<SimulationSpace::ForceFields::ReachEnvelopeCategory>(ChosenIdx);
                }
                else if (OpenDropdown == OpenDropdownCategory::Attenuation && Targets.ActiveForce != nullptr)
                {
                    Targets.ActiveForce->Attenuation = static_cast<SimulationSpace::ForceFields::ForceAttenuationMode>(ChosenIdx);
                }
                else if (OpenDropdown == OpenDropdownCategory::DurationMode && Targets.ActiveForce != nullptr)
                {
                    Targets.ActiveForce->DurationMode = static_cast<SimulationSpace::ForceFields::ForceDurationMode>(ChosenIdx);
                }
                else if (OpenDropdown == OpenDropdownCategory::MotionTopology && Targets.ActiveForce != nullptr)
                {
                    SimulationSpace::ForceFields::AssignPresetMotionPath(
                        *Targets.ActiveForce,
                        static_cast<SimulationSpace::ForceFields::MotionPathTopology>(ChosenIdx));
                }
                else if (OpenDropdown == OpenDropdownCategory::MotionCycle && Targets.ActiveForce != nullptr)
                {
                    Targets.ActiveForce->Motion.CycleBehavior = static_cast<SimulationSpace::ForceFields::MotionCycleBehavior>(ChosenIdx);
                }
                else if (OpenDropdown == OpenDropdownCategory::StreamProfile && Targets.ActiveForce != nullptr)
                {
                    Targets.ActiveForce->StreamProfile = static_cast<SimulationSpace::ForceFields::FlowStreamProfile>(ChosenIdx);
                }
                else if (OpenDropdown == OpenDropdownCategory::HeatmapField && Targets.OverlaySettings != nullptr)
                {
                    Targets.OverlaySettings->HeatmapChannel = static_cast<SimulationSpace::ForceFields::HeatmapChannelMode>(ChosenIdx);
                }
                else if (OpenDropdown == OpenDropdownCategory::ColorRamp && Targets.OverlaySettings != nullptr)
                {
                    Targets.OverlaySettings->HeatmapColorSchemeOrdinal = ChosenIdx;
                }
                else if (OpenDropdown == OpenDropdownCategory::StreamColorMode && Targets.OverlaySettings != nullptr)
                {
                    Targets.OverlaySettings->StreamColorMode = static_cast<SimulationSpace::ForceFields::StreamlineColorMode>(ChosenIdx);
                    if (Targets.OverlaySettings->StreamColorMode == SimulationSpace::ForceFields::StreamlineColorMode::CyanSingle)
                    {
                        Targets.OverlaySettings->CustomStreamColorRgba = 0x68DCFFFFu;
                    }
                }
                OpenDropdown = OpenDropdownCategory::None;
            }
            else if (SheetActiveBefore && Input.PrimaryPointer.Pressed && !SheetBounds.Contains(Cursor) && !OpenTriggerRect.Contains(Cursor))
            {
                OpenDropdown = OpenDropdownCategory::None;
            }
        }
    }

    Surface.PopClip();
    return true;
}

//------------------------------------------------------------------------------------------------------------------------
//                                             TAB 1: FIELD (ANEMOMETER + SAMPLE + MODEL)
//------------------------------------------------------------------------------------------------------------------------

float ForceInspectorView::RenderFieldTab(
    PixelSpace&                       Surface,
    Rectangle2D                       ScrollViewport,
    float                             LeftX,
    float                             TopY,
    float                             InnerW,
    const ForceInspectorViewTargets& Targets,
    const PaneInputReading&           Input,
    DeferredSheetOverlay&             OutOverlay)
{
    (void)OutOverlay;
    float CurY = TopY;
    const Point2D Cursor{ Input.PrimaryPointer.X, Input.PrimaryPointer.Y };
    const bool    Interactive = (OpenDropdown == OpenDropdownCategory::None) && ScrollViewport.Contains(Cursor);

    SimulationSpace::ForceFields::SpatialVector3 SamplePos{ 0.0f, 0.0f, 8.0f };
    if (Targets.OverlaySettings != nullptr)
    {
        SamplePos = Targets.OverlaySettings->AnemometerWorldMeters;
    }

    SimulationSpace::ForceFields::ForceDiagnosticsReading Diag{};
    if (Targets.Accumulator != nullptr)
    {
        Diag = Targets.Accumulator->QueryAnemometerDiagnostics(SamplePos, Targets.SimulationTimeSec);
    }

    const auto& Vec      = Diag.TotalForce.AdvectionVelocity;
    const float WindSpd  = SimulationSpace::ForceFields::VectorLength(Vec);
    const float AccelMag = SimulationSpace::ForceFields::VectorLength(Diag.TotalForce.LinearAcceleration);
    const float TotalSpd = (WindSpd > 0.01f) ? WindSpd : AccelMag;

    float Peak60s = TotalSpd;
    float Lull60s = TotalSpd;
    if (!Targets.AnemometerSamples.empty())
    {
        Peak60s = Targets.AnemometerSamples.front();
        Lull60s = Targets.AnemometerSamples.front();
        for (float V : Targets.AnemometerSamples)
        {
            Peak60s = std::max(Peak60s, V);
            Lull60s = std::min(Lull60s, V);
        }
    }

    const BeaufortEntry& Bft = ResolveBeaufortEntry(TotalSpd);

    // --------------------------------------------------------------------------------------------------------------------
    // Section 1: ANEMOMETER · PROBE WIND (.pcard.wf-trace)
    // --------------------------------------------------------------------------------------------------------------------
    {
        constexpr float   PadX = 15.0f;
        const float       AnemH = 310.0f;
        const Rectangle2D Box{ LeftX, CurY, InnerW, AnemH };
        ConstructGroupBox(Surface, Box);

        const float ColX = Box.X + PadX;
        const float ColW = Box.Width - PadX * 2.0f;
        float       Y    = Box.Y + 14.0f;

        // Head: ANEMOMETER · PROBE WIND + Bft pill
        WorkspaceAppearance::ConstructWeightedText(
            Surface,
            FontWeightClass::SemiBold,
            { ColX, Y + 4.0f },
            9.5f,
            InspectorSurfaceColors::InkMuted,
            "ANEMOMETER · PROBE WIND");

        char BftLabel[48]{};
        std::snprintf(BftLabel, sizeof(BftLabel), "Bft %u · %s", Bft.Scale, Bft.Name);
        const float BftTextW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Medium, 9.5f, BftLabel);
        const Rectangle2D BftCapsule{ Box.Right() - PadX - (BftTextW + 18.0f), Y, BftTextW + 18.0f, 20.0f };
        Surface.FillRectangle(BftCapsule, ColorRgba{ 88, 153, 121, 34 }, 10.0f);
        Surface.StrokeRectangle(BftCapsule, ColorRgba{ 88, 153, 121, 88 }, 10.0f, 1.0f);
        WorkspaceAppearance::ConstructWeightedText(
            Surface,
            FontWeightClass::Medium,
            { BftCapsule.X + 9.0f, BftCapsule.Y + 4.5f },
            9.5f,
            InspectorSurfaceColors::EmeraldSoft,
            BftLabel);

        Y += 26.0f;

        // Hero numeric speed + Compass pill
        char SpeedBuf[24]{};
        std::snprintf(SpeedBuf, sizeof(SpeedBuf), "%.1f", TotalSpd);
        const char* DotPos = std::strchr(SpeedBuf, '.');
        const std::string IntPart(SpeedBuf, DotPos != nullptr ? static_cast<std::size_t>(DotPos - SpeedBuf) : std::strlen(SpeedBuf));
        const std::string DecPart(DotPos != nullptr ? DotPos : ".0");

        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Light, { ColX, Y }, 36.0f, ColorRgba{ 244, 244, 246, 255 }, IntPart);
        const float IntW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Light, 36.0f, IntPart);
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Light, { ColX + IntW, Y }, 36.0f, ColorRgba{ 244, 244, 246, 90 }, DecPart);
        const float DecW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Light, 36.0f, DecPart);
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { ColX + IntW + DecW + 5.0f, Y + 19.0f }, 12.0f, InspectorSurfaceColors::InkMuted, "m/s");

        char CompassBuf[32]{};
        std::snprintf(CompassBuf, sizeof(CompassBuf), "%s · %.0f°", ResolveCompassLabel(Diag.CompassAzimuthDeg), Diag.CompassAzimuthDeg);
        const float CmpW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Medium, 10.0f, CompassBuf);
        const Rectangle2D CompassCapsule{ Box.Right() - PadX - (CmpW + 18.0f), Y + 12.0f, CmpW + 18.0f, 22.0f };
        Surface.FillRectangle(CompassCapsule, InspectorSurfaceColors::SubWellBg, 11.0f);
        Surface.StrokeRectangle(CompassCapsule, InspectorSurfaceColors::HairlineBorder, 11.0f, 1.0f);
        WorkspaceAppearance::ConstructWeightedText(
            Surface,
            FontWeightClass::Medium,
            { CompassCapsule.X + 9.0f, CompassCapsule.Y + 5.5f },
            10.0f,
            ColorRgba{ 207, 214, 220, 255 },
            CompassBuf);

        Y += 46.0f;

        // 74px Rolling 60s Trace Canvas
        const Rectangle2D TraceRect{ ColX, Y, ColW, 74.0f };
        float TraceMax = 20.0f;
        for (float V : Targets.AnemometerSamples)
        {
            TraceMax = std::max(TraceMax, V + 3.0f);
        }
        TraceMax = std::ceil(TraceMax / 10.0f) * 10.0f;

        for (float Step = 10.0f; Step < TraceMax; Step += 10.0f)
        {
            const float Gy = TraceRect.Bottom() - 6.0f - (Step / TraceMax) * (TraceRect.Height - 12.0f);
            Surface.StrokeLine({ TraceRect.X, Gy }, { TraceRect.Right(), Gy }, ColorRgba{ 255, 255, 255, 16 }, 1.0f);
            char StepBuf[16]{};
            std::snprintf(StepBuf, sizeof(StepBuf), "%.0f", Step);
            WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { TraceRect.X + 3.0f, Gy - 11.0f }, 8.5f, ColorRgba{ 255, 255, 255, 72 }, StepBuf);
        }

        if (Targets.AnemometerSamples.size() >= 2u)
        {
            std::vector<Point2D> TracePts(Targets.AnemometerSamples.size());
            for (std::size_t I = 0u; I < Targets.AnemometerSamples.size(); ++I)
            {
                const float T = static_cast<float>(I) / static_cast<float>(Targets.AnemometerSamples.size() - 1u);
                const float V = std::clamp(Targets.AnemometerSamples[I] / TraceMax, 0.0f, 1.0f);
                TracePts[I]   = { TraceRect.X + 2.0f + T * (TraceRect.Width - 6.0f), TraceRect.Bottom() - 6.0f - V * (TraceRect.Height - 12.0f) };
            }

            // Translucent emerald fill strip beneath curve
            for (std::size_t I = 0u; I + 1u < TracePts.size(); ++I)
            {
                const Point2D& P0 = TracePts[I];
                const Point2D& P1 = TracePts[I + 1u];
                const Point2D  Quad[4] = { P0, P1, { P1.X, TraceRect.Bottom() }, { P0.X, TraceRect.Bottom() } };
                Surface.FillPolygon(std::span<const Point2D>{ Quad, 4u }, InspectorSurfaceColors::EmeraldFill);
            }
            Surface.StrokePolyline(TracePts, InspectorSurfaceColors::EmeraldLine, 1.65f, false);
            Surface.FillCircle(TracePts.back(), 3.2f, ColorRgba{ 255, 255, 255, 255 });
        }

        Y += 76.0f;
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { ColX, Y }, 8.5f, InspectorSurfaceColors::InkDim, "-60 s");
        const float MidW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 8.5f, "-30 s");
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { ColX + (ColW - MidW) * 0.5f, Y }, 8.5f, InspectorSurfaceColors::InkDim, "-30 s");
        WorkspaceAppearance::ConstructRightAlignedText(Surface, FontWeightClass::Regular, { ColX + ColW, Y }, 8.5f, InspectorSurfaceColors::InkDim, "now");

        Y += 16.0f;
        char DescHead[32]{};
        std::snprintf(DescHead, sizeof(DescHead), "Beaufort %u", Bft.Scale);
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::SemiBold, { ColX, Y }, 10.0f, ColorRgba{ 236, 236, 236, 255 }, DescHead);
        const float HeadW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::SemiBold, 10.0f, DescHead);
        char DescTail[96]{};
        std::snprintf(DescTail, sizeof(DescTail), " — %s.", Bft.LandDescription);
        WorkspaceAppearance::ConstructTruncatedText(Surface, FontWeightClass::Regular, { ColX + HeadW, Y }, std::max(40.0f, ColW - HeadW), 10.0f, InspectorSurfaceColors::InkMuted, DescTail);

        Y += 20.0f;
        // 4-cell metric rail (KNOTS, KM/H, DYN Q, ALTITUDE)
        const float RailGap = 5.0f;
        const float RailW   = (ColW - RailGap * 3.0f) * 0.25f;
        char KnotsBuf[24]{}, KmhBuf[24]{}, DynQBuf[24]{}, AltBuf[24]{};
        std::snprintf(KnotsBuf, sizeof(KnotsBuf), "%.1f kt", TotalSpd * 1.94384f);
        std::snprintf(KmhBuf,   sizeof(KmhBuf),   "%.1f",    TotalSpd * 3.6f);
        std::snprintf(DynQBuf,  sizeof(DynQBuf),  "%.0f Pa", Diag.DynamicPressurePa);
        std::snprintf(AltBuf,   sizeof(AltBuf),   "%.0f m",  SamplePos.Z);
        const std::array<std::pair<const char*, const char*>, 4> RailCells = {{
            { "KNOTS", KnotsBuf }, { "KM/H", KmhBuf }, { "DYN Q", DynQBuf }, { "ALTITUDE", AltBuf }
        }};
        for (std::size_t C = 0u; C < RailCells.size(); ++C)
        {
            const Rectangle2D Cell{ ColX + static_cast<float>(C) * (RailW + RailGap), Y, RailW, 34.0f };
            Surface.FillRectangle(Cell, InspectorSurfaceColors::SubWellBg, 10.0f);
            Surface.StrokeRectangle(Cell, InspectorSurfaceColors::HairlineBorder, 10.0f, 1.0f);
            WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { Cell.X + 8.0f, Cell.Y + 5.5f }, 8.5f, ColorRgba{ 148, 154, 160, 255 }, RailCells[C].first);
            WorkspaceAppearance::ConstructTruncatedText(Surface, FontWeightClass::Medium, { Cell.X + 8.0f, Cell.Y + 17.5f }, Cell.Width - 12.0f, 10.0f, InspectorSurfaceColors::InkPrimary, RailCells[C].second);
        }

        Y += 42.0f;
        // 2-cell Gust / Lull duo
        const float DuoGap = 7.0f;
        const float DuoW   = (ColW - DuoGap) * 0.5f;
        char PeakBuf[24]{}, LullBuf[24]{};
        std::snprintf(PeakBuf, sizeof(PeakBuf), "%.1f m/s", Peak60s);
        std::snprintf(LullBuf, sizeof(LullBuf), "%.1f m/s", Lull60s);
        const std::array<std::pair<const char*, const char*>, 2> DuoCells = {{
            { "Gusting to (60s peak)", PeakBuf },
            { "Lulling to (60s min)",  LullBuf },
        }};
        for (std::size_t D = 0u; D < DuoCells.size(); ++D)
        {
            const Rectangle2D Cell{ ColX + static_cast<float>(D) * (DuoW + DuoGap), Y, DuoW, 38.0f };
            Surface.FillRectangle(Cell, InspectorSurfaceColors::SubWellBg, 12.0f);
            Surface.StrokeRectangle(Cell, InspectorSurfaceColors::HairlineBorder, 12.0f, 1.0f);
            WorkspaceAppearance::ConstructTruncatedText(Surface, FontWeightClass::Regular, { Cell.X + 10.0f, Cell.Y + 7.0f }, Cell.Width - 16.0f, 8.5f, InspectorSurfaceColors::InkMuted, DuoCells[D].first);
            WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Medium, { Cell.X + 10.0f, Cell.Y + 20.0f }, 11.5f, InspectorSurfaceColors::InkPrimary, DuoCells[D].second);
        }

        Y += 48.0f;
        // 13-Band Beaufort Bar + ticks
        ConstructBeaufortGradientBar(Surface, { ColX, Y, ColW, 10.0f }, TotalSpd);
        Y += 15.0f;
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { ColX, Y }, 8.5f, InspectorSurfaceColors::InkDim, "0 Calm");
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { ColX + ColW * 0.30f, Y }, 8.5f, InspectorSurfaceColors::InkDim, "5 Fresh");
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { ColX + ColW * 0.60f, Y }, 8.5f, InspectorSurfaceColors::InkDim, "8 Gale");
        WorkspaceAppearance::ConstructRightAlignedText(Surface, FontWeightClass::Regular, { ColX + ColW, Y }, 8.5f, InspectorSurfaceColors::InkDim, "12 Hurricane");

        CurY += Box.Height + 10.0f;
    }

    // --------------------------------------------------------------------------------------------------------------------
    // Section 2: Probe Sample
    // --------------------------------------------------------------------------------------------------------------------
    {
        constexpr float   PadX   = 15.0f;
        const Rectangle2D Box{ LeftX, CurY, InnerW, 174.0f };
        ConstructGroupBox(Surface, Box);

        const float ColX = Box.X + PadX;
        const float ColW = Box.Width - PadX * 2.0f;
        float       Y    = Box.Y + 14.0f;

        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Medium, { ColX, Y }, 12.0f, InspectorSurfaceColors::InkHeading, "Probe Sample");
        char ContribBuf[32]{};
        std::snprintf(ContribBuf, sizeof(ContribBuf), "%u Contributing", Diag.ActiveForceCount);
        WorkspaceAppearance::ConstructRightAlignedText(Surface, FontWeightClass::Regular, { ColX + ColW, Y + 1.5f }, 9.5f, InspectorSurfaceColors::InkMuted, ContribBuf);

        Y += 24.0f;
        if (Targets.OverlaySettings != nullptr)
        {
            ConstructAxisTripletRow(
                Surface,
                ColX,
                Y,
                ColW,
                Targets.OverlaySettings->AnemometerWorldMeters,
                1000,
                ActiveAxisDragId,
                AxisDragStartX,
                AxisDragOrigin,
                Interactive,
                Input);
        }

        Y += 36.0f;
        const float GridGap = 6.0f;
        const float CellW   = (ColW - GridGap) * 0.5f;
        char WindBuf[32]{}, AccBuf[32]{}, CurlBuf[32]{}, DivBuf[32]{};
        std::snprintf(WindBuf, sizeof(WindBuf), "%.1f m/s",  WindSpd);
        std::snprintf(AccBuf,  sizeof(AccBuf),  "%.1f m/s2", AccelMag);
        std::snprintf(CurlBuf, sizeof(CurlBuf), "%.3f /s",   Diag.CurlMagnitude);
        std::snprintf(DivBuf,  sizeof(DivBuf),  "%+.3f /s",  Diag.Divergence);

        const std::array<std::pair<const char*, const char*>, 4> TensorCells = {{
            { "FLOW VELOCITY",   WindBuf },
            { "ACCELERATION",    AccBuf  },
            { "VORTICITY |curl|",CurlBuf },
            { "DIVERGENCE div",  DivBuf  },
        }};
        for (std::size_t Idx = 0u; Idx < TensorCells.size(); ++Idx)
        {
            const float Rx = ColX + static_cast<float>(Idx % 2u) * (CellW + GridGap);
            const float Ry = Y + static_cast<float>(Idx / 2u) * 46.0f;
            const Rectangle2D Cell{ Rx, Ry, CellW, 41.0f };
            Surface.FillRectangle(Cell, InspectorSurfaceColors::SubWellBg, 14.0f);
            Surface.StrokeRectangle(Cell, InspectorSurfaceColors::HairlineBorder, 14.0f, 1.0f);
            WorkspaceAppearance::ConstructTruncatedText(Surface, FontWeightClass::Regular, { Cell.X + 11.0f, Cell.Y + 7.5f }, Cell.Width - 18.0f, 8.0f, InspectorSurfaceColors::InkMuted, TensorCells[Idx].first);
            WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { Cell.X + 11.0f, Cell.Y + 21.0f }, 12.5f, InspectorSurfaceColors::InkPrimary, TensorCells[Idx].second);
        }

        CurY += Box.Height + 10.0f;
    }

    // --------------------------------------------------------------------------------------------------------------------
    // Section 3: Selected ForceModel + 19-Law Params + Radial Envelope + Motion Path
    // --------------------------------------------------------------------------------------------------------------------
    if (Targets.ActiveForce == nullptr)
    {
        const Rectangle2D EmptyBox{ LeftX, CurY, InnerW, 64.0f };
        ConstructGroupBox(Surface, EmptyBox);
        WorkspaceAppearance::ConstructWeightedText(
            Surface,
            FontWeightClass::Regular,
            { EmptyBox.X + 16.0f, EmptyBox.Y + 24.0f },
            11.0f,
            InspectorSurfaceColors::InkMuted,
            "Select or construct a ForceField in Outliner to inspect.");
        CurY += EmptyBox.Height + 12.0f;
        return CurY - TopY;
    }

    auto& Src = *Targets.ActiveForce;
    const ForceModelDescriptor& Law = QueryForceModelDescriptor(Src.Model);
    const ColorRgba LawAccent = Src.FormulaEnabled
        ? InspectorSurfaceColors::AmberAccent
        : WorkspaceAppearance::QueryForceModelTint(static_cast<uint32_t>(Src.Model));

    const bool  StreamProfileActive = (Src.Model == SimulationSpace::ForceFields::ForceModel::FlowStream);
    const bool  MotionPathActive    = (Src.Motion.Topology != SimulationSpace::ForceFields::MotionPathTopology::Static);
    const bool  HelicalOrbitActive  = (Src.Motion.Topology == SimulationSpace::ForceFields::MotionPathTopology::HelicalOrbit);
    const uint32_t ParamRows        = Law.ParamCount;
    const uint32_t WpRows           = (!MotionPathActive || HelicalOrbitActive) ? 0u : std::max(2u, Src.Motion.WaypointCount);

    float MotionH = 114.0f;
    if (MotionPathActive)
    {
        MotionH += 98.0f; // Speed + Scrub sliders
        MotionH += HelicalOrbitActive ? 52.0f : (32.0f + static_cast<float>(WpRows) * 32.0f);
    }

    const float SelBoxH = 312.0f +
                          (StreamProfileActive ? 50.0f : 0.0f) +
                          static_cast<float>(ParamRows) * 50.0f +
                          88.0f + // Radial Envelope f(r) plot
                          MotionH;

    const Rectangle2D SelBox{ LeftX, CurY, InnerW, SelBoxH };
    ConstructGroupBox(Surface, SelBox);

    constexpr float PadX = 15.0f;
    const float     ColX = SelBox.X + PadX;
    const float     ColW = SelBox.Width - PadX * 2.0f;
    float           Y    = SelBox.Y + 14.0f;

    // Header row: ForceIcon + Model Switcher Title + System · Channel
    WorkspaceAppearance::ConstructForceModelIcon(Surface, static_cast<uint32_t>(Src.Model), { ColX, Y + 1.0f }, 16.0f, LawAccent);
    const Rectangle2D LawPickerBtn{ ColX + 22.0f, Y - 2.0f, std::min(168.0f, ColW * 0.56f), 22.0f };
    if (LawPickerBtn.Contains(Cursor) && Interactive)
    {
        Surface.FillRectangle(LawPickerBtn, ColorRgba{ 42, 42, 46, 255 }, 6.0f);
    }
    WorkspaceAppearance::ConstructTruncatedText(Surface, FontWeightClass::Medium, { LawPickerBtn.X + 4.0f, Y + 2.0f }, LawPickerBtn.Width - 20.0f, 12.0f, InspectorSurfaceColors::InkHeading, Law.DisplayTitle);
    VectorIconRepository::ConstructControl(
        Surface,
        ControlIcon::ChevronDown,
        VectorIconPlacement{ LawPickerBtn.Right() - 15.0f, Y + 3.0f, 11.0f, InspectorSurfaceColors::InkMuted, 1.5f });
    if (Interactive && LawPickerBtn.Contains(Cursor) && Input.PrimaryPointer.Pressed)
    {
        OpenDropdown    = OpenDropdownCategory::ForceModelSelector;
        OpenTriggerRect = LawPickerBtn;
    }

    char SysBuf[64]{};
    std::snprintf(SysBuf, sizeof(SysBuf), "%s · %s", Law.SystemCaption, Law.ChannelCaption);
    WorkspaceAppearance::ConstructRightAlignedText(Surface, FontWeightClass::Regular, { ColX + ColW, Y + 3.0f }, 9.5f, LawAccent, SysBuf);

    Y += 26.0f;

    // Equation Box + [Formula] / [Edit fx] button
    const Rectangle2D EqBox{ ColX, Y, ColW, 34.0f };
    Surface.FillRectangle(EqBox, InspectorSurfaceColors::InsetBg, 12.0f);
    Surface.StrokeRectangle(EqBox, InspectorSurfaceColors::HairlineBorder, 12.0f, 1.0f);

    const Rectangle2D EqFxBtn{ EqBox.Right() - 68.0f, EqBox.Y + 6.0f, 60.0f, 22.0f };
    const float       EqTextW = std::max(30.0f, EqFxBtn.X - EqBox.X - 18.0f);
    WorkspaceAppearance::ConstructTruncatedText(
        Surface,
        FontWeightClass::Regular,
        { EqBox.X + 10.0f, EqBox.Y + 11.0f },
        EqTextW,
        9.5f,
        InspectorSurfaceColors::TealCode,
        Src.FormulaEnabled ? std::string_view(Src.FormulaSourceText) : std::string_view(Law.EquationText),
        true);

    const bool EqBtnHov = Interactive && EqFxBtn.Contains(Cursor);
    ConstructCapsuleButton(Surface, EqFxBtn, Src.FormulaEnabled ? "Edit fx" : "Formula", false, EqBtnHov, 9.5f, InspectorSurfaceColors::InkHeading);
    if (EqBtnHov && Input.PrimaryPointer.Pressed)
    {
        if (!Src.FormulaEnabled)
        {
            Src.FormulaEnabled = true;
            if (Src.FormulaSourceText.empty())
            {
                Src.FormulaSourceText = SimulationSpace::ForceFields::QueryBuiltInFormulaPresets()[0].FormulaSource;
            }
            if (Targets.OutlinerEntry != nullptr)
            {
                Targets.OutlinerEntry->ForceFormulaOverride = true;
            }
        }
        ConfigureActiveTab(ForceInspectorTab::Formula);
    }

    Y += 42.0f;

    // 3 Centre Axis boxes
    bool MovedCentre = false;
    ConstructAxisTripletRow(Surface, ColX, Y, ColW, Src.CentreMeters, 2000, ActiveAxisDragId, AxisDragStartX, AxisDragOrigin, Interactive, Input, &MovedCentre);
    if (MovedCentre)
    {
        Src.EvaluatedCentreMeters = Src.CentreMeters;
        if (Targets.OutlinerEntry != nullptr)
        {
            Targets.OutlinerEntry->PositionX = Src.CentreMeters.X;
            Targets.OutlinerEntry->PositionY = Src.CentreMeters.Y;
            Targets.OutlinerEntry->PositionZ = Src.CentreMeters.Z;
        }
    }

    Y += 36.0f;

    // 2x2 Geometry/Envelope Selectors
    const float HalfGap = 6.0f;
    const float HalfW   = (ColW - HalfGap) * 0.5f;

    WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { ColX, Y }, 9.5f, InspectorSurfaceColors::InkMuted, "Reach Shape");
    WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { ColX + HalfW + HalfGap, Y }, 9.5f, InspectorSurfaceColors::InkMuted, "Attenuation");
    Y += 15.0f;
    const Rectangle2D ReachBtn{ ColX, Y, HalfW, 30.0f };
    const Rectangle2D AttenBtn{ ColX + HalfW + HalfGap, Y, HalfW, 30.0f };
    if (ConstructSelectTrigger(Surface, ReachBtn, ReachLabel(Src.Envelope), OpenDropdown == OpenDropdownCategory::ReachShape, Interactive, Input.PrimaryPointer))
    {
        OpenDropdown    = OpenDropdownCategory::ReachShape;
        OpenTriggerRect = ReachBtn;
    }
    if (ConstructSelectTrigger(Surface, AttenBtn, AttenuationLabel(Src.Attenuation), OpenDropdown == OpenDropdownCategory::Attenuation, Interactive, Input.PrimaryPointer))
    {
        OpenDropdown    = OpenDropdownCategory::Attenuation;
        OpenTriggerRect = AttenBtn;
    }

    Y += 38.0f;
    WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { ColX, Y }, 9.5f, InspectorSurfaceColors::InkMuted, "Effect Duration");
    WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { ColX + HalfW + HalfGap, Y }, 9.5f, InspectorSurfaceColors::InkMuted, "Radius (m)");
    Y += 15.0f;
    const Rectangle2D DurBtn{ ColX, Y, HalfW, 30.0f };
    const Rectangle2D RadBox{ ColX + HalfW + HalfGap, Y, HalfW, 28.0f };
    if (ConstructSelectTrigger(Surface, DurBtn, DurationLabel(Src.DurationMode), OpenDropdown == OpenDropdownCategory::DurationMode, Interactive, Input.PrimaryPointer))
    {
        OpenDropdown    = OpenDropdownCategory::DurationMode;
        OpenTriggerRect = DurBtn;
    }
    char RadBuf[24]{};
    std::snprintf(RadBuf, sizeof(RadBuf), "%.0f", Src.RadiusMeters);
    ConstructSplitNumericReadout(Surface, RadBox, RadBuf, "m");

    Y += 40.0f;

    if (StreamProfileActive)
    {
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { ColX, Y }, 9.5f, InspectorSurfaceColors::InkMuted, "FlowStream Profile");
        const Rectangle2D ProfBtn{ ColX, Y + 15.0f, ColW, 30.0f };
        if (ConstructSelectTrigger(Surface, ProfBtn, StreamProfileLabel(Src.StreamProfile), OpenDropdown == OpenDropdownCategory::StreamProfile, Interactive, Input.PrimaryPointer))
        {
            OpenDropdown    = OpenDropdownCategory::StreamProfile;
            OpenTriggerRect = ProfBtn;
        }
        Y += 50.0f;
    }

    // Bounded Radius Slider
    ConstructProjectZeroSliderRow(
        Surface, ColX, Y, ColW,
        "Bounded Radius", "2 – 120 m",
        2.0f, 120.0f, Src.RadiusMeters, 1u,
        10, ActiveSliderId, Interactive, Input);
    Y += 50.0f;

    // Per-Model Parameters (19-Law Specific!)
    for (uint32_t PIdx = 0u; PIdx < Law.ParamCount; ++PIdx)
    {
        const auto& P   = Law.Params[PIdx];
        float       Val = ReadScalarSlot(Src, P.Slot);
        const float Prev = Val;
        ConstructProjectZeroSliderRow(
            Surface, ColX, Y, ColW,
            P.Label, P.UnitText,
            P.Minimum, P.Maximum, Val, P.DecimalDigits,
            20 + static_cast<int32_t>(PIdx), ActiveSliderId, Interactive, Input);
        if (std::fabs(Val - Prev) > 1.0e-5f)
        {
            WriteScalarSlot(Src, P.Slot, Val);
        }
        Y += 50.0f;
    }

    // Radial Envelope f(r) plot
    WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { ColX, Y }, 10.5f, InspectorSurfaceColors::InkLabel, "Radial Envelope f(r)");
    WorkspaceAppearance::ConstructRightAlignedText(Surface, FontWeightClass::Regular, { ColX + ColW, Y + 0.5f }, 9.0f, InspectorSurfaceColors::InkMuted, "Solid=Mag · Dashed=A(r)");
    Y += 16.0f;
    ConstructRadialEnvelopePlot(Surface, { ColX, Y, ColW, 74.0f }, Src, Targets.SimulationTimeSec);
    Y += 84.0f;

    // Curve & Path Animation sub-section
    Surface.StrokeLine({ ColX, Y }, { ColX + ColW, Y }, InspectorSurfaceColors::SubtleBorder, 1.0f);
    Y += 10.0f;
    WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::SemiBold, { ColX, Y }, 11.0f, ColorRgba{ 220, 228, 235, 255 }, "Curve & Path Animation");
    if (MotionPathActive)
    {
        const float ArcLen = (Src.Motion.EvaluatedArcLengthMeters > 0.1f)
            ? Src.Motion.EvaluatedArcLengthMeters
            : SimulationSpace::ForceFields::ComputeMotionPathArcLengthMeters(Src);
        char ArcSummary[40]{};
        std::snprintf(ArcSummary, sizeof(ArcSummary), "%.0f m · %.1f s", ArcLen, ArcLen / std::max(0.5f, Src.Motion.SpeedMetresPerSecond));
        WorkspaceAppearance::ConstructRightAlignedText(Surface, FontWeightClass::Regular, { ColX + ColW, Y + 1.0f }, 9.5f, InspectorSurfaceColors::AmberAccent, ArcSummary);
    }
    else
    {
        WorkspaceAppearance::ConstructRightAlignedText(Surface, FontWeightClass::Regular, { ColX + ColW, Y + 1.0f }, 9.5f, InspectorSurfaceColors::InkMuted, "Static");
    }

    Y += 20.0f;
    const std::array<std::pair<const char*, SimulationSpace::ForceFields::MotionPathTopology>, 3> QuickModes = {{
        { "A -> B Line",     SimulationSpace::ForceFields::MotionPathTopology::LinearSpan    },
        { "3D Spline Curve", SimulationSpace::ForceFields::MotionPathTopology::CatmullSpline },
        { "Orbit",           SimulationSpace::ForceFields::MotionPathTopology::HelicalOrbit  },
    }};
    const uint32_t BtnCount = MotionPathActive ? 4u : 3u;
    const float    BtnGap   = 5.0f;
    const float    QBtnW    = (ColW - BtnGap * static_cast<float>(BtnCount - 1u)) / static_cast<float>(BtnCount);
    for (uint32_t QIdx = 0u; QIdx < 3u; ++QIdx)
    {
        const Rectangle2D QRect{ ColX + static_cast<float>(QIdx) * (QBtnW + BtnGap), Y, QBtnW, 24.0f };
        const bool Sel = (Src.Motion.Topology == QuickModes[QIdx].second);
        const bool Hov = Interactive && QRect.Contains(Cursor);
        ConstructCapsuleButton(Surface, QRect, QuickModes[QIdx].first, Sel, Hov, 9.5f, Sel ? InspectorSurfaceColors::AmberAccent : InspectorSurfaceColors::InkHeading);
        if (Hov && Input.PrimaryPointer.Pressed)
        {
            SimulationSpace::ForceFields::AssignPresetMotionPath(Src, QuickModes[QIdx].second);
            Src.DynamicMotion = true;
            if (Targets.SelectedWaypoint != nullptr)
            {
                *Targets.SelectedWaypoint = 0;
            }
        }
    }
    if (MotionPathActive)
    {
        const Rectangle2D ClrRect{ ColX + 3.0f * (QBtnW + BtnGap), Y, QBtnW, 24.0f };
        const bool Hov = Interactive && ClrRect.Contains(Cursor);
        ConstructCapsuleButton(Surface, ClrRect, "Clear", false, Hov, 9.5f, InspectorSurfaceColors::InkMuted);
        if (Hov && Input.PrimaryPointer.Pressed)
        {
            Src.Motion.Topology = SimulationSpace::ForceFields::MotionPathTopology::Static;
            Src.DynamicMotion   = false;
            if (Targets.SelectedWaypoint != nullptr)
            {
                *Targets.SelectedWaypoint = -1;
            }
        }
    }

    Y += 32.0f;
    WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { ColX, Y }, 9.5f, InspectorSurfaceColors::InkMuted, "Path Mode");
    WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { ColX + HalfW + HalfGap, Y }, 9.5f, InspectorSurfaceColors::InkMuted, "Loop Mode");
    Y += 15.0f;
    const Rectangle2D PathModeBtn{ ColX, Y, HalfW, 30.0f };
    const Rectangle2D LoopModeBtn{ ColX + HalfW + HalfGap, Y, HalfW, 30.0f };
    if (ConstructSelectTrigger(Surface, PathModeBtn, TopologyLabel(Src.Motion.Topology), OpenDropdown == OpenDropdownCategory::MotionTopology, Interactive, Input.PrimaryPointer))
    {
        OpenDropdown    = OpenDropdownCategory::MotionTopology;
        OpenTriggerRect = PathModeBtn;
    }
    if (ConstructSelectTrigger(Surface, LoopModeBtn, CycleLabel(Src.Motion.CycleBehavior), OpenDropdown == OpenDropdownCategory::MotionCycle, Interactive && MotionPathActive, Input.PrimaryPointer))
    {
        OpenDropdown    = OpenDropdownCategory::MotionCycle;
        OpenTriggerRect = LoopModeBtn;
    }
    Y += 36.0f;

    if (MotionPathActive)
    {
        ConstructProjectZeroSliderRow(
            Surface, ColX, Y, ColW,
            "Travel Speed", "m/s",
            0.5f, 30.0f, Src.Motion.SpeedMetresPerSecond, 1u,
            40, ActiveSliderId, Interactive, Input);
        Y += 49.0f;

        char PctBuf[24]{};
        std::snprintf(PctBuf, sizeof(PctBuf), "%.0f%%", Src.Motion.NormalizedProgress * 100.0f);
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { ColX, Y }, 10.5f, InspectorSurfaceColors::InkLabel, "Path Scrub / Progress");
        WorkspaceAppearance::ConstructRightAlignedText(Surface, FontWeightClass::Regular, { ColX + ColW, Y + 0.5f }, 9.5f, InspectorSurfaceColors::AmberAccent, PctBuf);
        const float SliderW = std::max(40.0f, ColW - 92.0f - 8.0f);
        ControlPointer ScrubPtr = Input.PrimaryPointer;
        ScrubPtr.Enabled        = Interactive;
        float NextScrub         = Src.Motion.NormalizedProgress;
        if (ScalarSlider::Construct(
                Surface, { ColX, Y + 17.0f, SliderW, 26.0f },
                0.0f, 1.0f, Src.Motion.NormalizedProgress,
                ActiveSliderId == 41, ScrubPtr, NextScrub, 1.0f, 26.0f, 24.0f).Pressed)
        {
            ActiveSliderId = 41;
        }
        if (ActiveSliderId == 41 && Input.PrimaryPointer.Down && Interactive)
        {
            Src.Motion.NormalizedProgress = NextScrub;
            Src.Motion.PhaseOffset        = NextScrub;
            Src.EvaluatedCentreMeters     = SimulationSpace::ForceFields::SampleForceMotionAtNormalizedPhase(Src, NextScrub).PositionMeters;
        }
        char PhiBuf[16]{};
        std::snprintf(PhiBuf, sizeof(PhiBuf), "%.2f", Src.Motion.PhaseOffset);
        ConstructSplitNumericReadout(Surface, { ColX + SliderW + 8.0f, Y + 17.0f, 92.0f, 26.0f }, PhiBuf, "phi");
        Y += 49.0f;

        if (HelicalOrbitActive)
        {
            ConstructProjectZeroSliderRow(
                Surface, ColX, Y, ColW,
                "Orbit Radius", "m",
                3.0f, 60.0f, Src.Motion.OrbitRadiusMeters, 1u,
                42, ActiveSliderId, Interactive, Input);
            Y += 49.0f;
        }
        else
        {
            WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { ColX, Y + 4.0f }, 9.0f, ColorRgba{ 143, 160, 176, 255 }, "Click tag or 3D sphere for gizmo focus");
            const Rectangle2D AddKnotBtn{ ColX + ColW - 56.0f, Y, 56.0f, 20.0f };
            const bool AddHov = Interactive && AddKnotBtn.Contains(Cursor);
            ConstructCapsuleButton(Surface, AddKnotBtn, "+ Knot", false, AddHov, 9.0f, InspectorSurfaceColors::InkHeading);
            if (AddHov && Input.PrimaryPointer.Pressed && Src.Motion.WaypointCount < SimulationSpace::ForceFields::MaxForceMotionWaypoints)
            {
                const uint32_t Last = std::max(1u, Src.Motion.WaypointCount - 1u);
                const auto P0       = Src.Motion.Waypoints[Last - 1u];
                const auto P1       = Src.Motion.Waypoints[Last];
                Src.Motion.Waypoints[Last + 1u] = P1;
                Src.Motion.Waypoints[Last]      = { 0.5f * (P0.X + P1.X), 0.5f * (P0.Y + P1.Y), std::max(P0.Z, P1.Z) + 4.0f };
                ++Src.Motion.WaypointCount;
            }
            Y += 28.0f;

            const uint32_t Count = std::min(Src.Motion.WaypointCount, SimulationSpace::ForceFields::MaxForceMotionWaypoints);
            for (uint32_t WIdx = 0u; WIdx < Count; ++WIdx)
            {
                const bool ActiveWp = (Targets.SelectedWaypoint != nullptr && *Targets.SelectedWaypoint == static_cast<int32_t>(WIdx));
                const Rectangle2D WpRow{ ColX, Y, ColW, 28.0f };
                Surface.FillRectangle(WpRow, ActiveWp ? ColorRgba{ 46, 38, 28, 255 } : InspectorSurfaceColors::RecessDark, 10.0f);
                Surface.StrokeRectangle(WpRow, ActiveWp ? InspectorSurfaceColors::AmberAccent : InspectorSurfaceColors::HairlineBorder, 10.0f, 1.0f);

                char TagBuf[8]{};
                if (WIdx == 0u) std::snprintf(TagBuf, sizeof(TagBuf), "A");
                else if (WIdx + 1u == Count) std::snprintf(TagBuf, sizeof(TagBuf), "B");
                else std::snprintf(TagBuf, sizeof(TagBuf), "P%u", WIdx);

                const Rectangle2D TagBtn{ WpRow.X + 4.0f, WpRow.Y + 4.0f, 26.0f, 20.0f };
                ConstructCapsuleButton(Surface, TagBtn, TagBuf, ActiveWp, Interactive && TagBtn.Contains(Cursor), 9.0f, ActiveWp ? InspectorSurfaceColors::AmberAccent : InspectorSurfaceColors::InkPrimary);
                if (Interactive && TagBtn.Contains(Cursor) && Input.PrimaryPointer.Pressed && Targets.SelectedWaypoint != nullptr)
                {
                    *Targets.SelectedWaypoint = static_cast<int32_t>(WIdx);
                }

                const float CoordX = TagBtn.Right() + 8.0f;
                const float DelW   = 22.0f;
                const float AxisW  = (WpRow.Right() - DelW - 6.0f - CoordX) / 3.0f;
                const auto& Wp     = Src.Motion.Waypoints[WIdx];
                const std::array<float, 3> Vals = {{ Wp.X, Wp.Y, Wp.Z }};
                for (std::size_t A = 0u; A < 3u; ++A)
                {
                    char CBuf[16]{};
                    std::snprintf(CBuf, sizeof(CBuf), "%.1f", Vals[A]);
                    WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { CoordX + static_cast<float>(A) * AxisW, WpRow.Y + 8.0f }, 9.5f, InspectorSurfaceColors::InkPrimary, CBuf);
                }

                const Rectangle2D DelWpBtn{ WpRow.Right() - DelW - 4.0f, WpRow.Y + 4.0f, 20.0f, 20.0f };
                if (Count > 2u && Interactive && DelWpBtn.Contains(Cursor) && Input.PrimaryPointer.Pressed)
                {
                    for (uint32_t Shift = WIdx; Shift + 1u < Count; ++Shift)
                    {
                        Src.Motion.Waypoints[Shift] = Src.Motion.Waypoints[Shift + 1u];
                    }
                    --Src.Motion.WaypointCount;
                    break;
                }
                WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { DelWpBtn.X + 6.0f, DelWpBtn.Y + 4.0f }, 10.5f, (Count > 2u) ? InspectorSurfaceColors::InkMuted : ColorRgba{ 80, 80, 80, 120 }, "x");
                Y += 32.0f;
            }
        }
    }

    CurY += SelBox.Height + 12.0f;
    return CurY - TopY;
}

//------------------------------------------------------------------------------------------------------------------------
//                                        TAB 2: FORMULA (VECTORFIELDLANG GPU COMPILER)
//------------------------------------------------------------------------------------------------------------------------

float ForceInspectorView::RenderFormulaTab(
    PixelSpace&                       Surface,
    Rectangle2D                       ScrollViewport,
    float                             LeftX,
    float                             TopY,
    float                             InnerW,
    const ForceInspectorViewTargets& Targets,
    const PaneInputReading&           Input)
{
    const Point2D Cursor{ Input.PrimaryPointer.X, Input.PrimaryPointer.Y };
    const bool    Interactive = ScrollViewport.Contains(Cursor);

    if (Targets.ActiveForce == nullptr)
    {
        const Rectangle2D EmptyBox{ LeftX, TopY, InnerW, 74.0f };
        ConstructGroupBox(Surface, EmptyBox);
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { EmptyBox.X + 16.0f, EmptyBox.Y + 26.0f }, 11.0f, InspectorSurfaceColors::InkMuted, "Select a force source to compile a symbolic GPU equation.");
        return 86.0f;
    }

    auto& Src = *Targets.ActiveForce;
    if (Src.FormulaSourceText.empty())
    {
        Src.FormulaSourceText = SimulationSpace::ForceFields::QueryBuiltInFormulaPresets()[0].FormulaSource;
    }

    const auto CompileRes = SimulationSpace::ForceFields::CompileFieldFormula(Src.FormulaSourceText);

    const auto Presets = SimulationSpace::ForceFields::QueryBuiltInFormulaPresets();
    const float PanelH = 528.0f;
    const Rectangle2D Panel{ LeftX, TopY, InnerW, PanelH };
    ConstructGroupBox(Surface, Panel);

    constexpr float PadX = 15.0f;
    const float     ColX = Panel.X + PadX;
    const float     ColW = Panel.Width - PadX * 2.0f;
    float           Y    = Panel.Y + 14.0f;

    WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Medium, { ColX, Y }, 12.0f, InspectorSurfaceColors::InkHeading, "Symbolic GPU Language");
    WorkspaceAppearance::ConstructRightAlignedText(Surface, FontWeightClass::Regular, { ColX + ColW, Y + 1.5f }, 9.5f, InspectorSurfaceColors::AmberAccent, Src.Label);
    Y += 24.0f;

    if (ConstructGreenSwitchRow(Surface, { ColX, Y, ColW, 38.0f }, "Evaluate Custom Formula on GPU", Src.FormulaEnabled, Interactive, Input))
    {
        if (Targets.OutlinerEntry != nullptr)
        {
            Targets.OutlinerEntry->ForceFormulaOverride = Src.FormulaEnabled;
        }
    }
    Y += 46.0f;

    // Presets grid
    WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { ColX, Y }, 10.0f, InspectorSurfaceColors::InkLabel, "Presets");
    WorkspaceAppearance::ConstructRightAlignedText(Surface, FontWeightClass::Regular, { ColX + ColW, Y + 0.5f }, 9.0f, InspectorSurfaceColors::InkMuted, "Click to load");
    Y += 16.0f;
    const float PBtnW = (ColW - 6.0f) * 0.5f;
    for (std::size_t PIdx = 0u; PIdx < std::min<std::size_t>(Presets.size(), 5u); ++PIdx)
    {
        const float Px = ColX + static_cast<float>(PIdx % 2u) * (PBtnW + 6.0f);
        const float Py = Y + static_cast<float>(PIdx / 2u) * 28.0f;
        const Rectangle2D PBtn{ Px, Py, PBtnW, 24.0f };
        const bool Selected = (Src.FormulaSourceText == Presets[PIdx].FormulaSource);
        const bool Hovered  = Interactive && PBtn.Contains(Cursor);
        ConstructCapsuleButton(Surface, PBtn, Presets[PIdx].Title, Selected, Hovered, 9.0f, Selected ? InspectorSurfaceColors::AmberAccent : InspectorSurfaceColors::InkHeading);
        if (Hovered && Input.PrimaryPointer.Pressed)
        {
            Src.FormulaSourceText = Presets[PIdx].FormulaSource;
            Src.FormulaEnabled    = true;
            if (Targets.OutlinerEntry != nullptr)
            {
                Targets.OutlinerEntry->ForceFormulaOverride = true;
            }
        }
    }
    Y += 90.0f;

    // Symbol Library
    WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { ColX, Y }, 10.0f, InspectorSurfaceColors::InkLabel, "Symbol Library");
    WorkspaceAppearance::ConstructRightAlignedText(Surface, FontWeightClass::Regular, { ColX + ColW, Y + 0.5f }, 9.0f, InspectorSurfaceColors::InkMuted, "Insert symbol");
    Y += 16.0f;
    static constexpr std::array<std::pair<const char*, const char*>, 10> Symbols = {{
        { "*", " * " }, { "+", " + " }, { "r_hat", "r_hat" }, { "x_hat", "x_hat" }, { "y_hat", "y_hat" },
        { "z_hat", "z_hat" }, { "theta", "theta_hat" }, { "A(r)", "A(r)" }, { "S", "S" }, { "curl", "curlNoise(p)" },
    }};
    const float SymW = (ColW - 4.0f * 4.0f) / 5.0f;
    for (std::size_t SIdx = 0u; SIdx < Symbols.size(); ++SIdx)
    {
        const Rectangle2D SBtn{ ColX + static_cast<float>(SIdx % 5u) * (SymW + 4.0f), Y + static_cast<float>(SIdx / 5u) * 24.0f, SymW, 20.0f };
        const bool Hov = Interactive && SBtn.Contains(Cursor);
        ConstructCapsuleButton(Surface, SBtn, Symbols[SIdx].first, false, Hov, 8.5f, InspectorSurfaceColors::TealCode);
        if (Hov && Input.PrimaryPointer.Pressed)
        {
            Src.FormulaSourceText += Symbols[SIdx].second;
        }
    }
    Y += 54.0f;

    // Syntax-highlighted VectorFieldLang box
    ConstructHighlightedFormulaBox(Surface, { ColX, Y, ColW, 74.0f }, Src.FormulaSourceText, CompileRes.Compiled);
    Y += 80.0f;

    // Diagnostic Bar
    const Rectangle2D DiagBox{ ColX, Y, ColW, 28.0f };
    Surface.FillRectangle(DiagBox, CompileRes.Compiled ? ColorRgba{ 88, 153, 121, 32 } : ColorRgba{ 218, 71, 60, 36 }, 8.0f);
    Surface.StrokeRectangle(DiagBox, CompileRes.Compiled ? ColorRgba{ 88, 153, 121, 90 } : ColorRgba{ 218, 71, 60, 120 }, 8.0f, 1.0f);
    WorkspaceAppearance::ConstructTruncatedText(
        Surface,
        FontWeightClass::Regular,
        { DiagBox.X + 10.0f, DiagBox.Y + 8.5f },
        DiagBox.Width - 20.0f,
        9.0f,
        CompileRes.Compiled ? InspectorSurfaceColors::EmeraldSoft : ColorRgba{ 255, 168, 162, 255 },
        CompileRes.Compiled
            ? "Compiled cleanly · Output: Vector3 · GPU SPIR-V + CPU synchronized"
            : CompileRes.DiagnosticMessage);
    Y += 36.0f;

    // Strength (S) & Swirl (omega) sliders
    ConstructProjectZeroSliderRow(Surface, ColX, Y, ColW, "Strength (S)", "m/s", -60.0f, 60.0f, Src.Strength, 1u, 60, ActiveSliderId, Interactive, Input);
    Y += 49.0f;
    ConstructProjectZeroSliderRow(Surface, ColX, Y, ColW, "Swirl (omega_swirl)", "x", -3.0f, 3.0f, Src.SwirlRatio, 2u, 61, ActiveSliderId, Interactive, Input);
    Y += 49.0f;

    // Emitted Slang / Bytecode Kernel preview
    WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { ColX, Y }, 9.5f, InspectorSurfaceColors::EmeraldSoft, "Generated SPIR-V / Slang GPU Expression");
    Y += 14.0f;
    const Rectangle2D CodeOut{ ColX, Y, ColW, 44.0f };
    Surface.FillRectangle(CodeOut, InspectorSurfaceColors::EditorBg, 8.0f);
    Surface.StrokeRectangle(CodeOut, InspectorSurfaceColors::HairlineBorder, 8.0f, 1.0f);
    char BcBuf[192]{};
    std::snprintf(BcBuf, sizeof(BcBuf), "%u ops | %s",
        CompileRes.Program.InstructionCount,
        CompileRes.SlangExpression.empty() ? "float3(0.0, 0.0, 0.0)" : CompileRes.SlangExpression.c_str());
    WorkspaceAppearance::ConstructTruncatedText(
        Surface, FontWeightClass::Regular, { CodeOut.X + 10.0f, CodeOut.Y + 15.0f }, CodeOut.Width - 20.0f, 9.0f, InspectorSurfaceColors::CyanCode, BcBuf, true);

    return PanelH + 12.0f;
}

//------------------------------------------------------------------------------------------------------------------------
//                                        TAB 3: LAYERS (GPU SLICE & RK4 STREAMLINES)
//------------------------------------------------------------------------------------------------------------------------

float ForceInspectorView::RenderLayersTab(
    PixelSpace&                       Surface,
    Rectangle2D                       ScrollViewport,
    float                             LeftX,
    float                             TopY,
    float                             InnerW,
    const ForceInspectorViewTargets& Targets,
    const PaneInputReading&           Input,
    DeferredSheetOverlay&             OutOverlay)
{
    (void)OutOverlay;
    float CurY = TopY;
    const Point2D Cursor{ Input.PrimaryPointer.X, Input.PrimaryPointer.Y };
    const bool    Interactive = (OpenDropdown == OpenDropdownCategory::None) && ScrollViewport.Contains(Cursor);

    if (Targets.OverlaySettings == nullptr)
    {
        return 0.0f;
    }
    auto& Viz = *Targets.OverlaySettings;

    // Panel 1: GPU Slice & Colorimetry
    {
        const Rectangle2D Box{ LeftX, CurY, InnerW, 434.0f };
        ConstructGroupBox(Surface, Box);
        const float ColX = Box.X + 15.0f;
        const float ColW = Box.Width - 30.0f;
        float       Y    = Box.Y + 14.0f;

        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Medium, { ColX, Y }, 12.0f, InspectorSurfaceColors::InkHeading, "GPU Slice & Colorimetry");
        char SliceMeta[48]{};
        std::snprintf(SliceMeta, sizeof(SliceMeta), "Plane @ %.0f m", Viz.CrossSectionOffsetMeters);
        WorkspaceAppearance::ConstructRightAlignedText(Surface, FontWeightClass::Regular, { ColX + ColW, Y + 1.5f }, 9.5f, InspectorSurfaceColors::InkMuted, SliceMeta);
        Y += 26.0f;

        const float HalfGap = 6.0f;
        const float HalfW   = (ColW - HalfGap) * 0.5f;
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { ColX, Y }, 9.5f, InspectorSurfaceColors::InkMuted, "Evaluated Field");
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { ColX + HalfW + HalfGap, Y }, 9.5f, InspectorSurfaceColors::InkMuted, "Color Ramp");
        Y += 15.0f;

        const Rectangle2D FieldBtn{ ColX, Y, HalfW, 30.0f };
        const Rectangle2D RampBtn { ColX + HalfW + HalfGap, Y, HalfW, 30.0f };
        if (ConstructSelectTrigger(Surface, FieldBtn, HeatmapFieldLabel(Viz.HeatmapChannel), OpenDropdown == OpenDropdownCategory::HeatmapField, Interactive, Input.PrimaryPointer))
        {
            OpenDropdown    = OpenDropdownCategory::HeatmapField;
            OpenTriggerRect = FieldBtn;
        }
        if (ConstructSelectTrigger(Surface, RampBtn, ColorRampLabel(Viz.HeatmapColorSchemeOrdinal), OpenDropdown == OpenDropdownCategory::ColorRamp, Interactive, Input.PrimaryPointer))
        {
            OpenDropdown    = OpenDropdownCategory::ColorRamp;
            OpenTriggerRect = RampBtn;
        }
        Y += 40.0f;

        ConstructProjectZeroSliderRow(Surface, ColX, Y, ColW, "Slice Plane Offset", "m", -20.0f, 60.0f, Viz.CrossSectionOffsetMeters, 1u, 70, ActiveSliderId, Interactive, Input);
        Y += 49.0f;
        ConstructProjectZeroSliderRow(Surface, ColX, Y, ColW, "Fixed Scale Ceiling", "max", 10.0f, 80.0f, Viz.HeatmapCeilingScale, 0u, 71, ActiveSliderId, Interactive, Input);
        Y += 49.0f;
        ConstructProjectZeroSliderRow(Surface, ColX, Y, ColW, "Arrow Grid Spacing", "m", 2.0f, 24.0f, Viz.ArrowSpacingMeters, 1u, 72, ActiveSliderId, Interactive, Input);
        Y += 49.0f;

        float OpacityPct = Viz.HeatmapOpacity * 100.0f;
        ConstructProjectZeroSliderRow(Surface, ColX, Y, ColW, "Heatmap Opacity", "%", 10.0f, 95.0f, OpacityPct, 0u, 73, ActiveSliderId, Interactive, Input);
        Viz.HeatmapOpacity = std::clamp(OpacityPct * 0.01f, 0.1f, 0.95f);
        Y += 49.0f;

        float LayersF = static_cast<float>(Viz.ArrowLayerCount);
        ConstructProjectZeroSliderRow(Surface, ColX, Y, ColW, "3D Arrow Layers", "lvl", 1.0f, 3.0f, LayersF, 0u, 74, ActiveSliderId, Interactive, Input);
        Viz.ArrowLayerCount = static_cast<uint32_t>(std::clamp(static_cast<int>(std::lround(LayersF)), 1, 3));
        Y += 50.0f;

        // 2x2 Green Toggle Grid
        const float TogW = (ColW - 6.0f) * 0.5f;
        ConstructGreenSwitchRow(Surface, { ColX,               Y,        TogW, 38.0f }, "GPU Heatmap",     Viz.ShowHeatmap,     Interactive, Input);
        ConstructGreenSwitchRow(Surface, { ColX + TogW + 6.0f, Y,        TogW, 38.0f }, "3D GPU Arrows",   Viz.ShowVectors,     Interactive, Input);
        ConstructGreenSwitchRow(Surface, { ColX,               Y + 44.0f,TogW, 38.0f }, "RK4 Streamlines", Viz.ShowStreamlines, Interactive, Input);
        ConstructGreenSwitchRow(Surface, { ColX + TogW + 6.0f, Y + 44.0f,TogW, 38.0f }, "Reach Bounds",    Viz.ShowBounds,      Interactive, Input);

        CurY += Box.Height + 10.0f;
    }

    // Panel 2: RK4 Streamlines
    {
        const Rectangle2D Box{ LeftX, CurY, InnerW, 404.0f };
        ConstructGroupBox(Surface, Box);
        const float ColX = Box.X + 15.0f;
        const float ColW = Box.Width - 30.0f;
        float       Y    = Box.Y + 14.0f;

        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Medium, { ColX, Y }, 12.0f, InspectorSurfaceColors::InkHeading, "RK4 Streamlines");
        char StreamMeta[48]{};
        std::snprintf(StreamMeta, sizeof(StreamMeta), "%u lines · %u steps", Viz.StreamLineCount, Viz.StreamCurveResolution);
        WorkspaceAppearance::ConstructRightAlignedText(Surface, FontWeightClass::Regular, { ColX + ColW, Y + 1.5f }, 9.5f, InspectorSurfaceColors::InkMuted, StreamMeta);
        Y += 26.0f;

        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { ColX, Y }, 9.5f, InspectorSurfaceColors::InkMuted, "Color Visualization");
        Y += 15.0f;
        const Rectangle2D VisModeBtn{ ColX, Y, ColW, 30.0f };
        if (ConstructSelectTrigger(Surface, VisModeBtn, StreamColorLabel(Viz.StreamColorMode), OpenDropdown == OpenDropdownCategory::StreamColorMode, Interactive, Input.PrimaryPointer))
        {
            OpenDropdown    = OpenDropdownCategory::StreamColorMode;
            OpenTriggerRect = VisModeBtn;
        }
        Y += 38.0f;

        // Single Color / Tint + 5 preset circular swatches
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { ColX, Y }, 9.5f, InspectorSurfaceColors::InkMuted, "Single Color / Tint");
        Y += 16.0f;
        const Rectangle2D SwatchBox{ ColX, Y, 44.0f, 26.0f };
        const ColorRgba ActiveTint = (Viz.StreamColorMode == SimulationSpace::ForceFields::StreamlineColorMode::CyanSingle)
            ? ColorRgba{ 104, 220, 255, 255 }
            : UnpackRgbaHex(Viz.CustomStreamColorRgba);
        Surface.FillRectangle(SwatchBox, ActiveTint, 8.0f);
        Surface.StrokeRectangle(SwatchBox, InspectorSurfaceColors::SubtleBorder, 8.0f, 1.0f);

        static constexpr std::array<uint32_t, 5> Swatches = {{
            0x68DCFFFFu, 0x84C7FFFFu, 0x54E6A4FFu, 0xFFC857FFu, 0xFF7461FFu,
        }};
        for (std::size_t SIdx = 0u; SIdx < Swatches.size(); ++SIdx)
        {
            const Point2D   Ctr{ SwatchBox.Right() + 20.0f + static_cast<float>(SIdx) * 28.0f, SwatchBox.Center().Y };
            const bool      Match = (Viz.CustomStreamColorRgba == Swatches[SIdx]) ||
                                    (SIdx == 0u && Viz.StreamColorMode == SimulationSpace::ForceFields::StreamlineColorMode::CyanSingle);
            Surface.FillCircle(Ctr, 10.0f, UnpackRgbaHex(Swatches[SIdx]));
            Surface.StrokeCircle(Ctr, 10.5f, Match ? InspectorSurfaceColors::AmberAccent : ColorRgba{ 255, 255, 255, 42 }, Match ? 2.0f : 1.0f);

            const float Dx = Cursor.X - Ctr.X;
            const float Dy = Cursor.Y - Ctr.Y;
            if (Interactive && (Dx * Dx + Dy * Dy) <= 132.0f && Input.PrimaryPointer.Pressed)
            {
                Viz.CustomStreamColorRgba = Swatches[SIdx];
                Viz.StreamColorMode       = (SIdx == 0u)
                    ? SimulationSpace::ForceFields::StreamlineColorMode::CyanSingle
                    : SimulationSpace::ForceFields::StreamlineColorMode::CustomSingle;
            }
        }
        Y += 36.0f;

        ConstructProjectZeroSliderRow(Surface, ColX, Y, ColW, "Thickness", "px", 0.25f, 2.0f, Viz.StreamRibbonThickness, 2u, 80, ActiveSliderId, Interactive, Input);
        Y += 49.0f;
        ConstructProjectZeroSliderRow(Surface, ColX, Y, ColW, "Emissive Glow", "x", 0.25f, 2.4f, Viz.StreamEmissiveBoost, 2u, 81, ActiveSliderId, Interactive, Input);
        Y += 49.0f;

        float StreamOpPct = Viz.StreamOpacity * 100.0f;
        ConstructProjectZeroSliderRow(Surface, ColX, Y, ColW, "Ribbon Opacity", "%", 15.0f, 100.0f, StreamOpPct, 0u, 82, ActiveSliderId, Interactive, Input);
        Viz.StreamOpacity = std::clamp(StreamOpPct * 0.01f, 0.15f, 1.0f);
        Y += 49.0f;

        float ResF = static_cast<float>(Viz.StreamCurveResolution);
        ConstructProjectZeroSliderRow(Surface, ColX, Y, ColW, "Curve Resolution", "seg", 10.0f, 64.0f, ResF, 0u, 83, ActiveSliderId, Interactive, Input);
        Viz.StreamCurveResolution = static_cast<uint32_t>(std::clamp(static_cast<int>(std::lround(ResF)), 10, 64));
        Y += 49.0f;

        float LinesF = static_cast<float>(Viz.StreamLineCount);
        ConstructProjectZeroSliderRow(Surface, ColX, Y, ColW, "Number of Lines", "ln", 20.0f, 480.0f, LinesF, 0u, 84, ActiveSliderId, Interactive, Input);
        Viz.StreamLineCount = static_cast<uint32_t>(std::clamp(static_cast<int>(std::lround(LinesF)), 20, 480));

        CurY += Box.Height + 12.0f;
    }

    return CurY - TopY;
}

//------------------------------------------------------------------------------------------------------------------------
//                                        TAB 4: CACHE (4-TIER CLIPMAP & TOML)
//------------------------------------------------------------------------------------------------------------------------

float ForceInspectorView::RenderCacheTab(
    PixelSpace&                       Surface,
    Rectangle2D                       ScrollViewport,
    float                             LeftX,
    float                             TopY,
    float                             InnerW,
    const ForceInspectorViewTargets& Targets,
    const PaneInputReading&           Input)
{
    const Point2D Cursor{ Input.PrimaryPointer.X, Input.PrimaryPointer.Y };
    const bool    Interactive = ScrollViewport.Contains(Cursor);

    const uint32_t Tiers = SimulationSpace::ForceFields::FieldCascadeTierCount;
    double TotalBytes = 0.0;
    for (uint32_t T = 0u; T < Tiers; ++T)
    {
        const uint32_t Res = (Targets.LatticeCascade != nullptr)
            ? Targets.LatticeCascade->QueryTierDescriptor(T).GridResolution
            : 16u;
        TotalBytes += static_cast<double>(Res) * static_cast<double>(Res) * static_cast<double>(Res) * 16.0;
    }
    const double TotalMb = TotalBytes / (1024.0 * 1024.0);

    const float BoxH = 446.0f;
    const Rectangle2D Box{ LeftX, TopY, InnerW, BoxH };
    ConstructGroupBox(Surface, Box);

    const float ColX = Box.X + 15.0f;
    const float ColW = Box.Width - 30.0f;
    float       Y    = Box.Y + 14.0f;

    WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Medium, { ColX, Y }, 12.0f, InspectorSurfaceColors::InkHeading, "4-Tier Cascade Clipmap");
    char TotalMbBuf[32]{};
    std::snprintf(TotalMbBuf, sizeof(TotalMbBuf), "%.2f MB Total", TotalMb);
    WorkspaceAppearance::ConstructRightAlignedText(Surface, FontWeightClass::Regular, { ColX + ColW, Y + 1.5f }, 9.5f, InspectorSurfaceColors::InkMuted, TotalMbBuf);
    Y += 26.0f;

    const float Gap   = 6.0f;
    const float CellW = (ColW - Gap) * 0.5f;
    for (uint32_t Tier = 0u; Tier < std::min(Tiers, 4u); ++Tier)
    {
        const auto TierDesc = (Targets.LatticeCascade != nullptr)
            ? Targets.LatticeCascade->QueryTierDescriptor(Tier)
            : SimulationSpace::ForceFields::FieldLatticeTierDescriptor{ Tier, 16u, 32.0f * std::pow(8.0f, static_cast<float>(Tier)), {} };
        const float SpanM = TierDesc.WorldHalfExtentMeters * 2.0f;
        const float CellM = SpanM / static_cast<float>(std::max(1u, TierDesc.GridResolution));
        const Rectangle2D Cell{ ColX + static_cast<float>(Tier % 2u) * (CellW + Gap), Y + static_cast<float>(Tier / 2u) * 46.0f, CellW, 41.0f };
        Surface.FillRectangle(Cell, InspectorSurfaceColors::SubWellBg, 14.0f);
        Surface.StrokeRectangle(Cell, InspectorSurfaceColors::HairlineBorder, 14.0f, 1.0f);
        char Cap[24]{}, Val[48]{};
        std::snprintf(Cap, sizeof(Cap), "CASCADE %u", Tier);
        if (SpanM >= 2000.0f)
        {
            std::snprintf(Val, sizeof(Val), "%.0fm · %.0fkm", CellM, SpanM * 0.001f);
        }
        else
        {
            std::snprintf(Val, sizeof(Val), "%.1fm · %.0fm", CellM, SpanM);
        }
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { Cell.X + 11.0f, Cell.Y + 7.5f }, 8.0f, InspectorSurfaceColors::InkMuted, Cap);
        WorkspaceAppearance::ConstructTruncatedText(Surface, FontWeightClass::Regular, { Cell.X + 11.0f, Cell.Y + 21.0f }, Cell.Width - 18.0f, 12.0f, InspectorSurfaceColors::InkPrimary, Val);
    }
    Y += 102.0f;

    WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Medium, { ColX, Y }, 12.0f, InspectorSurfaceColors::InkHeading, "SimulationSpace TOML");
    const Rectangle2D CopyBtn{ ColX + ColW - 64.0f, Y - 2.0f, 64.0f, 22.0f };
    const bool CopyHov = Interactive && CopyBtn.Contains(Cursor);
    ConstructCapsuleButton(Surface, CopyBtn, FormulaCopiedAck ? "Copied" : "Copy", FormulaCopiedAck, CopyHov, 9.5f, FormulaCopiedAck ? InspectorSurfaceColors::EmeraldSoft : InspectorSurfaceColors::InkHeading);

    std::string TomlText;
    if (Targets.Accumulator != nullptr)
    {
        Codecs::VectorFieldSpaceDocument Doc{};
        Doc.DocumentTitle     = "VectorFieldWorkspace";
        Doc.CascadeResolution = (Targets.LatticeCascade != nullptr) ? Targets.LatticeCascade->QueryTierDescriptor(0u).GridResolution : 16u;
        const auto Live       = Targets.Accumulator->QuerySources();
        Doc.ForceSources.assign(Live.begin(), Live.end());
        if (Targets.OverlaySettings != nullptr)
        {
            Doc.EditorOverlay    = *Targets.OverlaySettings;
            Doc.HasEditorOverlay = true;
        }
        const toml::table Manifest = Codecs::VectorFieldCodec::Encoder::BuildPayload(Doc).DomainManifest;
        std::ostringstream TomlStream;
        TomlStream << "[VectorFieldSpace]\n";
        TomlStream << "DocumentTitle = \"" << Doc.DocumentTitle << "\"\n";
        TomlStream << "CoordinateSystem = \"RightHandZUp\"\n";
        TomlStream << "CascadeResolution = " << Doc.CascadeResolution << "\n\n";
        for (const auto& SrcEntry : Doc.ForceSources)
        {
            TomlStream << "[[ForceSources]]\n";
            TomlStream << "Label = \"" << SrcEntry.Label << "\"\n";
            TomlStream << "Model = \"" << SimulationSpace::ForceFields::QueryForceModelDisplayName(SrcEntry.Model) << "\"\n";
            TomlStream << "RadiusMeters = " << static_cast<int>(SrcEntry.RadiusMeters) << "\n";
            TomlStream << "Strength = " << SrcEntry.Strength << "\n\n";
        }
        TomlStream << Manifest;
        TomlText = TomlStream.str();
    }

    if (CopyHov && Input.PrimaryPointer.Pressed && !TomlText.empty())
    {
        ImGui::SetClipboardText(TomlText.c_str());
        FormulaCopiedAck = true;
        CopyAckTimerSec  = 1.25;
    }

    Y += 26.0f;
    const Rectangle2D TomlBox{ ColX, Y, ColW, 272.0f };
    Surface.FillRectangle(TomlBox, InspectorSurfaceColors::RecessDark, 10.0f);
    Surface.StrokeRectangle(TomlBox, InspectorSurfaceColors::SubtleBorder, 10.0f, 1.0f);

    std::istringstream Lines(TomlText);
    std::string        Line;
    float              LineY = TomlBox.Y + 10.0f;
    while (std::getline(Lines, Line) && (LineY + 14.0f) <= (TomlBox.Bottom() - 6.0f))
    {
        const bool SectionHeading = !Line.empty() && Line[0] == '[';
        WorkspaceAppearance::ConstructTruncatedText(
            Surface,
            FontWeightClass::Regular,
            { TomlBox.X + 10.0f, LineY },
            TomlBox.Width - 20.0f,
            9.0f,
            SectionHeading ? InspectorSurfaceColors::EmeraldSoft : ColorRgba{ 183, 194, 200, 255 },
            Line,
            true);
        LineY += 14.0f;
    }

    return BoxH + 12.0f;
}

} // namespace Frontier::Editor::Simulation
