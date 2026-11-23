//============================================================================================================================================
//                                                        SKETCHPOLYLINE.H
//============================================================================================================================================
// 🧩 Polyline evaluation of exact sketch curves for selection, snapping, booleans and visual proofing. The exact
//    sketch declarations remain authoritative; this is the shared approximation seam those consumers read.

#pragma once

#include "SlateShape/Geometry/CurveSpecification/Api/CurveSpecification.h"

#include <cstdint>
#include <vector>

namespace Slate
{

/// 🧩 How many chords one curve needs before its polyline stops looking like a smaller, flatter curve.
/// in    Geometry   [-]  the curve being approximated
/// in    Floor      [-]  the caller's minimum; the answer is never below it
/// out   -          [-]  a step count in `[Floor, CurveStepLimit]`
/// note  🔴 A FIXED STEP COUNT IS A SHRINKING CURVE. Every chord of an approximated arc lies INSIDE the
///        true curve, short of it by the sagitta `R(1 - cos(θ/2))` for a per-step angle `θ`. Hold the
///        step count constant and `θ` is constant, so that error grows in direct proportion to the
///        radius — draw the same circle twice as large and it is drawn twice as far inside where it
///        should be. That is exactly the reported "the longer the curve, the more it shrinks, because
///        there isn't enough geometry": the geometry count never moved while the curve did.
/// note  🔴 Inverting the sagitta bound gives `θ = 2·acos(1 - t/R)` for a tolerance `t`, so the count
///        rises with the SQUARE ROOT of the radius rather than linearly — a hundred-fold larger circle
///        needs ten times the chords, not a hundred. Splines have no single radius, so their control
///        polygon length stands in for arc length, of which it is an upper bound.
/// note  ⚠️ Clamped at `CurveStepLimit`. Without a limit a curve drawn kilometres wide would ask for
///        millions of chords and the packet would refuse them one at a time.
/// cost  ✔️
/// tag   nonthrowing
std::uint32_t ResolveCurveStepCount(const CurveSpecification& Geometry,
                                    std::uint32_t Floor = 48u);

/// 🧩 Raises the curvature-aware floor for a viewport-dependent detail scale. The scale is bounded so
///    close zoom improves quality without allowing one curve to exhaust the bounded CAD packet.
std::uint32_t ResolveCurveStepCountForDetail(const CurveSpecification& Geometry,
                                             std::uint32_t Floor,
                                             double DetailScale);

/// 🧩 The most chords any one curve may be given, however large it is drawn.
constexpr std::uint32_t CurveStepLimit = 512u;

/// 🧩 How far a chord may sit inside the curve it approximates, in world units.
constexpr double CurveChordTolerance = 0.05;

void AppendCurvePolyline(const CurveSpecification& Geometry,
                         std::vector<SpatialPoint>& Polyline,
                         std::uint32_t StepCount = 48u);

} // namespace Slate
