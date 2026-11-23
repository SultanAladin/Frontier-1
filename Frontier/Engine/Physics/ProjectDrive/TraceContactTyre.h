//============================================================================================================================================
//                                                           TRACECONTACTTYRE.H
//============================================================================================================================================
// 📦 The wheel contact: a fan of ground samples around the rim, averaged into one patch, one normal and one load.
//
// 📐 This is GRIT's `ComputeSuspensionDisplacements` (VehicleSolver.cpp @984dfbb, the Chaos build) with the Unreal
//    line traces replaced by the solver's own GroundQuery. The trajectory atlas is the same: `LateralCount` slices
//    across the tread, each with a fan of directions ±FanHalfAngle about straight down in the wheel plane, every
//    sample a point on the rigid tyre circle. A sample counts as a hit when the ground surface lies above it; the
//    hits average into the patch centre and normal, and the tyre deflection is how far the hub sits below
//    (patch + normal × radius). The load is a residual radial spring + damper on that deflection — the tyre's own
//    vertical stiffness (≈ 200–300 kN/m for a passenger tyre), which GRIT lumped into the strut and we keep
//    separate because the strut here is a real two-mass unit (SuspensionSpecification.h).
//
// Cost: LateralCount × (2·FanHalfAngle/FanStep + 1) ground queries per wheel per step — 3 × 19 = 57 by default.
//    A step for 240 tyres is ~14 k queries, well inside a millisecond, and the answer does not depend on any
//    iteration schedule, so it is a usable road-feedback source.
//
// Frames (VehicleMath.h): right-handed, +Z up; hub frame +X forward, +Y axle (left), +Z up; steer and camber
//    live in the hub rotation, spin does not.

#pragma once

#include "RigidWheel.h"
#include <cmath>
#include <cstdint>

namespace Frontier {
namespace Vehicle {

struct TraceContactParameters
{
    float    FanHalfAngleRad   = 0.7854f;   // [rad]   ±45° about straight down, in the wheel plane
    float    FanStepRad        = 0.0873f;   // [rad]   5° between fan directions
    uint32_t LateralCount      = 3u;        // [-]     slices across the tread (1 = centre line only)
    float    VerticalStiffness = 250000.0f; // [N/m]   radial stiffness of the carcass AT ReferencePressure
    float    ReferencePressure = 110000.0f; // [Pa]    gauge pressure VerticalStiffness was tuned for
    float    PressureShare     = 0.75f;     // [-]     fraction of the stiffness carried by the air: k = k0·(1 − s + s·p/p_ref).
                                            //         A flat tyre keeps the carcass quarter; double pressure is 1.75× stiffer.
    float    VerticalDamping   = 1500.0f;   // [N·s/m] radial damping of the carcass
    float    MaxDeflection     = 0.08f;     // [m]     deflection beyond which the spring is clamped (rim contact guard)
};

// One wheel's contact for this step, in the shape the solver already consumes (TyreReaction) plus the raw figures.
struct TraceContactReading
{
    TyreReaction Reaction{};        // SupportReaction / RimForce / PatchCentre / ContactCount filled; the rest zero
    Vec3         Normal{0.0f, 0.0f, 1.0f};
    float        Deflection = 0.0f; // [m]   radial tyre compression along the averaged normal
    float        Rate       = 0.0f; // [m/s] compression rate (+ = compressing)
    uint32_t     HitCount   = 0u;   // [-]   samples that found ground
    uint32_t     SampleCount = 0u;  // [-]   samples cast
};

// hubRot carries steer (and camber), never spin. hubVel is the hub's own velocity including strut travel.
// Radial stiffness at the live gauge pressure (thermal state when it runs, the cold inflation otherwise).
[[nodiscard]] inline float PressureStiffness(const TraceContactParameters& P, float GaugePressure) noexcept
{
    const float Reference = P.ReferencePressure > 1.0f ? P.ReferencePressure : 1.0f;
    const float Ratio = std::isfinite(GaugePressure) && GaugePressure >= 0.0f ? GaugePressure / Reference : 1.0f;
    const float Share = P.PressureShare < 0.0f ? 0.0f : (P.PressureShare > 1.0f ? 1.0f : P.PressureShare);
    return P.VerticalStiffness * ((1.0f - Share) + Share * (Ratio < 4.0f ? Ratio : 4.0f));
}

inline TraceContactReading TraceWheelContact(const TraceContactParameters& P, float Radius, float Width,
                                             const Vec3& Hub, const Quat& HubRot, const Vec3& HubVel,
                                             const GroundQuery& Ground) noexcept
{
    TraceContactReading Out{};
    if (!Ground || Radius <= 0.0f) return Out;
    const Vec3 Forward = HubRot.Rotate({1.0f, 0.0f, 0.0f});
    const Vec3 Axle    = HubRot.Rotate({0.0f, 1.0f, 0.0f});
    const Vec3 Up      = HubRot.Rotate({0.0f, 0.0f, 1.0f});
    const float HalfAngle = std::fabs(P.FanHalfAngleRad);
    const float StepAngle = P.FanStepRad > 1.0e-4f ? P.FanStepRad : 0.0873f;
    const int32_t HalfCount = static_cast<int32_t>(HalfAngle / StepAngle);
    const uint32_t Slices = P.LateralCount == 0u ? 1u : P.LateralCount;

    Vec3 SumPoint{}, SumNormal{};
    float SumLateral = 0.0f;
    uint32_t Hits = 0u;
    for (uint32_t Slice = 0u; Slice < Slices; ++Slice)
    {
        const float Fraction = Slices == 1u ? 0.5f : static_cast<float>(Slice) / static_cast<float>(Slices - 1u);
        const Vec3 Centre = Hub + Axle * ((Fraction - 0.5f) * Width);
        for (int32_t Index = -HalfCount; Index <= HalfCount; ++Index)
        {
            const float Angle = static_cast<float>(Index) * StepAngle;
            const Vec3 Direction = Forward * std::sin(Angle) - Up * std::cos(Angle);   // GRIT: (sin a, 0, -cos a)
            const Vec3 Sample = Centre + Direction * Radius;
            ++Out.SampleCount;
            Vec3 Surface{}, Normal{};
            if (!Ground(Sample, Surface, Normal)) continue;
            const float NormalLength = Normal.Length();
            if (NormalLength < 1.0e-6f) continue;
            Normal = Normal * (1.0f / NormalLength);
            // Below the surface along its normal: this rim point is inside the ground. (A heightfield answers the
            //    column above the sample; a true mesh query answers the nearest surface — both read the same way.)
            if (Dot(Surface - Sample, Normal) <= 0.0f) continue;
            SumPoint = SumPoint + Surface;
            SumNormal = SumNormal + Normal;
            SumLateral += (Fraction - 0.5f) * 2.0f;
            ++Hits;
        }
    }
    Out.HitCount = Hits;
    if (Hits == 0u) return Out;

    const float Inverse = 1.0f / static_cast<float>(Hits);
    const Vec3 Patch = SumPoint * Inverse;
    Vec3 N = SumNormal * Inverse;
    const float NLength = N.Length();
    N = NLength > 1.0e-6f ? N * (1.0f / NLength) : Up;
    // GRIT: PhysicsWheelCenter = AvgLoc + AvgNorm·R; how far the real hub sits below that is the tyre deflection.
    const Vec3 ImpliedHub = Patch + N * Radius;
    float Deflection = Dot(ImpliedHub - Hub, N);
    if (!(Deflection > 0.0f))
    {   // touching but not compressed: no load, still a patch
        Out.Normal = N; Out.Reaction.PatchCentre = Patch; Out.Reaction.ContactNormal = N; Out.Reaction.ContactCount = Hits;
        return Out;
    }
    if (Deflection > P.MaxDeflection) Deflection = P.MaxDeflection;
    const float Rate = -Dot(HubVel, N);                     // hub moving into the ground compresses the tyre
    float Load = P.VerticalStiffness * Deflection + P.VerticalDamping * Rate;
    if (Load < 0.0f) Load = 0.0f;

    Out.Normal = N;
    Out.Deflection = Deflection;
    Out.Rate = Rate;
    Out.Reaction.SupportReaction = N * Load;
    Out.Reaction.NormalReaction  = N * Load;
    Out.Reaction.RimForce        = N * Load;                // the carcass pushes the hub away from the ground
    Out.Reaction.Force           = Vec3{0.0f, 0.0f, Load};
    Out.Reaction.PatchCentre     = Patch;
    Out.Reaction.ContactCount    = Hits;
    Out.Reaction.ContactNormal   = N;
    Out.Reaction.Deflection      = Deflection;
    Out.Reaction.PatchLateral    = SumLateral * Inverse;
    return Out;
}

} // namespace Vehicle
} // namespace Frontier
