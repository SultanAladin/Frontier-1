/*==========================================================================================================================================
                                                TYRESURFACEPROJECTION.H
==========================================================================================================================================*/
// 🧩 Standalone spring-profile and 32×5 continuum displacement projection for tyre vertices.
// 📝 These are visual deformation functions, not tyre/road force models.

#pragma once

#include "TyreContinuum.h"

#include <algorithm>
#include <cmath>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                   DEFORMATION INPUTS
//------------------------------------------------------------------------------------------------------------------------

struct TyreDeformationRecord
{
    float ContactAngle      = 0.0f;   // [rad]  contact direction in the spun wheel frame
    float Deflection        = 0.0f;   // [m]    carcass radial deflection
    float GaugePressure     = 0.0f;   // [Pa]
    float LateralDistortion = 0.0f;   // [m]    cornering shear offset at the tread, clamped ±40 mm
    bool  Shredded          = false;  // [-]    rubber gone: tyre vertices collapse to the axle
};

//------------------------------------------------------------------------------------------------------------------------
//                                                   VERTEX PROJECTION
//------------------------------------------------------------------------------------------------------------------------

// 📝 ResolveWheelVertex transcribed: ① contact-patch flattening with a cosine envelope over the pressure-scaled patch
//    span, ② approximate sidewall bulge, ③ shoulder expansion wave just outside the patch, ④ lateral
//    cornering shear, and the shading-normal tilt. Position/Normal are wheel-local (axle along Y), metres, float —
//    the WGSL's own precision. TyreSurface false (rim/hub/brake) returns the vertex unchanged; a shredded tyre's
//    vertices collapse to the axle.
inline void ResolveDeformedWheelVertex(const TyreDeformationRecord& Deformation, bool TyreSurface,
                                       const float Position[3], const float Normal[3],
                                       float OutPosition[3], float OutNormal[3])
{
    const float TyreRimRadius = 0.3448f;
    const float TyreOuterRadius = 0.50855f;
    const float Pi = 3.141592653589793f;
    OutPosition[0] = Position[0]; OutPosition[1] = Position[1]; OutPosition[2] = Position[2];
    OutNormal[0] = Normal[0]; OutNormal[1] = Normal[1]; OutNormal[2] = Normal[2];
    if (TyreSurface && Deformation.Shredded)
    {
        OutPosition[0] = 0.0f; OutPosition[1] = 0.0f; OutPosition[2] = 0.0f;
        return;
    }
    if (!TyreSurface)
        return;
    const float Deflection = Deformation.Deflection;
    if (Deflection <= 1.0e-5f)
        return;
    const float ContactAngle = Deformation.ContactAngle;
    const float Pressure = std::fmax(0.0f, Deformation.GaugePressure);
    const float LateralDistortion = Deformation.LateralDistortion;
    const float R = std::sqrt(Position[0] * Position[0] + Position[2] * Position[2]);
    if (R <= TyreRimRadius + 1.0e-4f)
        return;
    const float Phi = std::atan2(Position[2], Position[0]);
    float DPhi = Phi - ContactAngle;
    DPhi = DPhi - 2.0f * Pi * std::round(DPhi / (2.0f * Pi));
    const float PNorm = std::fmax(0.05f, Pressure / 220000.0f);
    const float PatchSpan = std::fmax(0.20f, std::fmin(1.15f, 0.44f / std::sqrt(std::fmax(0.15f, PNorm))));
    const float RadialFrac = std::fmax(0.0f, std::fmin(1.0f, (R - TyreRimRadius) / (TyreOuterRadius - TyreRimRadius)));
    float DR = 0.0f;
    float DY = 0.0f;
    const float AbsDPhi = std::fabs(DPhi);
    // ① Contact flattening constraint + ② volumetric sidewall bulge.
    if (AbsDPhi < PatchSpan)
    {
        const float U = AbsDPhi / PatchSpan;
        const float Envelope = 0.5f * (1.0f + std::cos(U * Pi));
        DR = -Deflection * Envelope * RadialFrac;
        const float SidewallFactor = std::sin(RadialFrac * Pi);
        const float BulgeIntensity = 0.48f / (0.4f + 0.6f * std::sqrt(PNorm));
        const float SignY = Position[1] > 0.0f ? 1.0f : (Position[1] < 0.0f ? -1.0f : 0.0f);
        DY = SignY * (Deflection * BulgeIntensity * Envelope * SidewallFactor);
    }
    // ③ Volumetric wave expansion (bead/shoulder transition zone just outside contact).
    const float WaveOuter = PatchSpan * 1.5f;
    if (AbsDPhi >= PatchSpan && AbsDPhi < WaveOuter)
    {
        const float V = (AbsDPhi - PatchSpan) / (WaveOuter - PatchSpan);
        DR = DR + std::sin(V * Pi) * (0.12f * Deflection * RadialFrac);
    }
    // ④ Lateral cornering slip / shear strain.
    if (AbsDPhi < PatchSpan * 1.2f)
    {
        const float UShear = AbsDPhi / (PatchSpan * 1.2f);
        const float ShearEnv = 0.5f * (1.0f + std::cos(UShear * Pi));
        DY = DY + LateralDistortion * ShearEnv * RadialFrac;
    }
    const float NewR = std::fmax(TyreRimRadius, R + DR);
    OutPosition[0] = NewR * std::cos(Phi);
    OutPosition[1] = Position[1] + DY;
    OutPosition[2] = NewR * std::sin(Phi);
    if (AbsDPhi < PatchSpan)
    {
        const float Tilt = -std::sin((DPhi / PatchSpan) * Pi) * (Deflection / (TyreOuterRadius * PatchSpan));
        float Tilted[3] = { Normal[0] + (-std::sin(Phi)) * Tilt, Normal[1], Normal[2] + std::cos(Phi) * Tilt };
        const float L = std::sqrt(Tilted[0] * Tilted[0] + Tilted[1] * Tilted[1] + Tilted[2] * Tilted[2]);
        if (L > 1.0e-12f)
        {
            OutNormal[0] = Tilted[0] / L; OutNormal[1] = Tilted[1] / L; OutNormal[2] = Tilted[2] / L;
        }
    }
}

// 📝 Current demo's 32×5 nodal interpolation path, including its tangential normal adjustment.
//    A zero-load or shredded tyre follows the same guards as the retained VehicleVisual.wgsl.
inline void ResolveContinuumWheelVertex(const TyreDeformationRecord& Deformation, bool TyreSurface,
                                        const TyreContinuumPositions& Rest, const TyreContinuumPositions& Local,
                                        const float Position[3], const float Normal[3],
                                        float OutPosition[3], float OutNormal[3])
{
    for (int Axis = 0; Axis < 3; ++Axis) { OutPosition[Axis] = Position[Axis]; OutNormal[Axis] = Normal[Axis]; }
    if (TyreSurface && Deformation.Shredded)
    {
        OutPosition[0] = OutPosition[1] = OutPosition[2] = 0.0f;
        return;
    }
    if (!TyreSurface || Deformation.Deflection <= 1.0e-5f) return;
    constexpr float Pi = 3.14159265358979323846f, RimRadius = 0.3448f, OuterRadius = 0.50855f;
    const float Radius = std::sqrt(Position[0]*Position[0] + Position[2]*Position[2]);
    if (Radius <= RimRadius + 1e-4f) return;
    const float Phi = std::atan2(Position[2], Position[0]);
    float Turns = Phi / (2.0f * Pi) + 1.0f;
    Turns -= std::floor(Turns);
    const float SectorCoord = Turns * 32.0f;
    const int S0 = static_cast<int>(std::floor(SectorCoord)) & 31, S1 = (S0 + 1) & 31;
    const float SectorFraction = SectorCoord - std::floor(SectorCoord);
    const float RingCoord = std::fmax(0.5f, std::fmin(3.5f, (Position[1] / 0.118f) * 1.35f + 2.0f));
    const int M0 = std::min(static_cast<int>(std::floor(RingCoord)), 3), M1 = M0 + 1;
    const float RingFraction = std::fmax(0.0f, std::fmin(1.0f, RingCoord - M0));
    const auto Displacement = [&](int Sector, int Ring, int Axis)
    {
        const int Index = (Sector * 5 + Ring) * 3 + Axis;
        return Local[Index] - Rest[Index];
    };
    const float Fraction = std::fmax(0.0f, std::fmin(1.0f, (Radius - RimRadius) / (OuterRadius - RimRadius)));
    const float U = std::fmax(0.0f, std::fmin(1.0f, Fraction / 0.88f));
    const float Weight = U * U * (3.0f - 2.0f * U);
    float U00[3], U10[3];
    for (int Axis = 0; Axis < 3; ++Axis)
    {
        U00[Axis] = Displacement(S0, M0, Axis); U10[Axis] = Displacement(S1, M0, Axis);
        const float U01 = Displacement(S0, M1, Axis), U11 = Displacement(S1, M1, Axis);
        const float Row0 = U00[Axis] * (1.0f - SectorFraction) + U10[Axis] * SectorFraction;
        const float Row1 = U01 * (1.0f - SectorFraction) + U11 * SectorFraction;
        OutPosition[Axis] += (Row0 * (1.0f - RingFraction) + Row1 * RingFraction) * Weight;
    }
    const float SinPhi = std::sin(Phi), CosPhi = std::cos(Phi);
    const float Tilt = ((U10[0] - U00[0]) * CosPhi + (U10[2] - U00[2]) * SinPhi) * Weight * 4.5f;
    OutNormal[0] = Normal[0] + SinPhi * Tilt;
    OutNormal[1] = Normal[1];
    OutNormal[2] = Normal[2] - CosPhi * Tilt;
    const float Magnitude = std::sqrt(OutNormal[0]*OutNormal[0] + OutNormal[1]*OutNormal[1] + OutNormal[2]*OutNormal[2]);
    if (Magnitude > 1e-12f) for (int Axis = 0; Axis < 3; ++Axis) OutNormal[Axis] /= Magnitude;
}

} // namespace Frontier
