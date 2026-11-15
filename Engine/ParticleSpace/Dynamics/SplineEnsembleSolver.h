/*============================================================================================================================================
                                                          SPLINEENSEMBLESOLVER.H
============================================================================================================================================*/
// 🧩 1:1 C++20 spline & filament solvers: Curl Stream Ribbons, Bokeh Object Pour, Catenary Spiderwebs, and Dielectric Lightning Arcs

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/ParticleSpace/Dynamics/SimulationDynamicsTypes.h"
#include "Engine/ParticleSpace/ParticleBuffers.h"
#include "Engine/ParticleSpace/ParticleDocument.h"
#include "Engine/SimulationSpace/ForceFields/ForceFieldAccumulator.h"

#include <cstdint>
#include <span>
#include <vector>

namespace Frontier::ParticleSpace
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

inline constexpr uint32_t kCurlRibbonVertexCount = 18u;
inline constexpr uint32_t kWebSplineVertexCount  = 20u;

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct ElectrodeSphereProp
{
    float CentreMeters[3]    = { 0.0f, 0.0f, 0.0f };
    float RadiusMeters       = 0.45f;
    float ShellLinearRgb[3]  = { 0.04f, 0.05f, 0.08f };
    float RingLinearRgb[3]   = { 0.46f, 0.80f, 1.0f };
    bool  FloorRingEnabled   = true;
    bool  VerticalPost       = false;
};

struct ConductorLineProp
{
    float StartMeters[3]  = { 0.0f, 0.0f, 0.0f };
    float EndMeters[3]    = { 0.0f, 0.0f, 0.0f };
    float LinearRgb[3]    = { 0.14f, 0.19f, 0.28f };
    float Opacity         = 0.75f;
};

struct DielectricArcSegment
{
    float StartMeters[3]  = { 0.0f, 0.0f, 0.0f };
    float StartWidthM     = 0.03f;
    float EndMeters[3]    = { 0.0f, 0.0f, 0.0f };
    float EndWidthM       = 0.03f;
    float LinearRgb[3]    = { 0.45f, 0.72f, 1.0f };
    float Intensity       = 1.0f;
};

struct DielectricArcFlash
{
    float CentreMeters[3] = { 0.0f, 0.0f, 0.0f };
    float RadiusMeters    = 0.25f;
    float LinearRgb[3]    = { 0.55f, 0.85f, 1.0f };
    float Intensity       = 1.0f;
};

struct DielectricArcGeometry
{
    std::vector<ElectrodeSphereProp>  Electrodes;
    std::vector<ConductorLineProp>    Conductors;
    std::vector<DielectricArcSegment> Segments;
    std::vector<DielectricArcFlash>   Flashes;
};

//------------------------------------------------------------------------------------------------------------------------
//                                                  FUNCTION PROTOTYPES
//------------------------------------------------------------------------------------------------------------------------

/// Evaluates analytic 3D curl velocity, bokeh stream ribbons, and sphere/box/cylinder surface pour ribbons.
void StepCurlStreamRibbonEnsemble(
    const ParticleRig&                                          Rig,
    const CurlManifoldSpec&                                     Spec,
    const float                                                 OriginMeters[3],
    const float                                                 AmbientWindMps[3],
    const Frontier::SimulationSpace::ForceFields::ForceFieldAccumulator& Fields,
    std::span<ParticleSlot>                                     Slots,
    std::span<StrandVertex>                                     Strands,
    float                                                       ElapsedSeconds,
    float                                                       DeltaSeconds) noexcept;

/// Evaluates 20-vertex catenary spiderweb splines (Orb-Weave spiral + Messy Attic Cobwebs) with live wind sway.
void StepSpiderwebSplineEnsemble(
    const ParticleRig&                                          Rig,
    const SpiderwebWeaveSpec&                                   Spec,
    const float                                                 OriginMeters[3],
    const float                                                 AmbientWindMps[3],
    const Frontier::SimulationSpace::ForceFields::ForceFieldAccumulator& Fields,
    const InteractiveColliderSpec&                              Collider,
    std::span<ParticleSlot>                                     Slots,
    std::span<StrandVertex>                                     Strands,
    float                                                       ElapsedSeconds,
    float                                                       DeltaSeconds) noexcept;

/// Advances ionization spark particles emitted around Tesla electrodes, plasma cages, and storm nodes.
void StepDielectricSparkEnsemble(
    const ParticleRig&             Rig,
    const DielectricArcSpec&       Spec,
    const float                    CathodeMeters[3],
    const float                    AnodeMeters[3],
    std::span<ParticleSlot>        Slots,
    float                          ElapsedSeconds,
    float                          DeltaSeconds) noexcept;

/// Generates recursive midpoint-subdivided lightning bolts, leader forks, electrode props, and contact flashes.
[[nodiscard]] DielectricArcGeometry BuildDielectricArcGeometry(
    const ParticleRig&             Rig,
    const DielectricArcSpec&       Spec,
    const float                    CathodeMeters[3],
    const float                    AnodeMeters[3],
    const InteractiveColliderSpec& Collider,
    float                          ElapsedSeconds,
    bool                           ForcePeakDischarge = false) noexcept;

} // namespace Frontier::ParticleSpace
