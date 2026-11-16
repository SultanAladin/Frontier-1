/*============================================================================================================================================
                                                              FORCEMOTIONPATH.H
============================================================================================================================================*/
// 🧩 Deterministic 3D LineAB, Catmull-Rom spline, and helical orbit trajectory evaluator for ForceSourceRecordEntry

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/SimulationSpace/ForceFields/ForceFieldDescriptors.h"

#include <cstdint>
#include <span>

namespace Frontier::SimulationSpace::ForceFields
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct MotionSamplePoint
{
    SpatialVector3 PositionMeters {};    // [m]   - Sampled world coordinate along curve
    SpatialVector3 TangentVector  {};    // [-]   - Unit forward tangent vector along motion
    float          NormalizedPhase = 0.0f;// [0-1] - Normalized cycle parameter after cycle wrap
};

//------------------------------------------------------------------------------------------------------------------------
//                                                  FUNCTION PROTOTYPES
//------------------------------------------------------------------------------------------------------------------------

void AssignPresetMotionPath(
    ForceSourceRecordEntry& Entry,
    MotionPathTopology      PresetTopology) noexcept;

[[nodiscard]] float ResolveCycleNormalizedPhase(
    double              TimeSeconds,
    float               ArcLengthMeters,
    float               SpeedMetresPerSecond,
    float               PhaseOffset,
    MotionCycleBehavior CycleBehavior) noexcept;

[[nodiscard]] float ComputeMotionPathArcLengthMeters(const ForceSourceRecordEntry& Entry) noexcept;

[[nodiscard]] MotionSamplePoint SampleForceMotionAtNormalizedPhase(
    const ForceSourceRecordEntry& Entry,
    float                         NormalizedPhase) noexcept;

[[nodiscard]] MotionSamplePoint EvaluateForceMotionAtTime(
    const ForceSourceRecordEntry& Entry,
    double                        TimeSeconds) noexcept;

void StepAnimatedForceSources(
    std::span<ForceSourceRecordEntry> Sources,
    double                            TimeSeconds,
    bool                              AdvanceMotion = true) noexcept;

} // namespace Frontier::SimulationSpace::ForceFields
