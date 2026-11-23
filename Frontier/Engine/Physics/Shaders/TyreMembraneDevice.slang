/*==========================================================================================================================================
                                                   TYREMEMBRANEDEVICE.SLANG
==========================================================================================================================================*/
// 🧩 The retained VehicleHMI orthotropic curved shell/membrane tyre, one sequential accumulation per tyre.
//    This is an authored contact-target deformation model, not an independently calibrated force solver.
#include "TyreProjectionDevice.slang"

[shader("compute")]
[numthreads(32, 1, 1)]
void ResetTyreMembrane(uint3 Thread : SV_DispatchThreadID)
{
    uint Tyre = Thread.x;
    if (Tyre >= InvocationExtent.TyreCount || !HasBuffers(Tyre)) return;
    TyreProjectionCommand Command = Commands[Tyre];
    if (!CommandValid(Command))
    {
        Results[Tyre].ForceStatus = float4(0.0, 0.0, 0.0, 0.0);
        return;
    }
    uint Base = Tyre * ParticleCount;
    const float Radii[5] = {0.3448, 0.4865, 0.50855, 0.4865, 0.3448};
    const float Widths[5] = {-0.115, -0.118, 0.0, 0.118, 0.115};
    for (uint Sector = 0; Sector < CircumferentialCount; ++Sector)
    {
        float Theta = 2.0 * Pi * float(Sector) / float(CircumferentialCount);
        float Cosine = cos(Theta), Sine = sin(Theta);
        for (uint Meridian = 0; Meridian < MeridionalCount; ++Meridian)
        {
            float3 Rest = float3(Radii[Meridian] * Cosine, Widths[Meridian], Radii[Meridian] * Sine);
            TyreProjectionParticle Particle;
            Particle.RestInverseMass = float4(Rest, Meridian == 0 || Meridian == 4 ? 0.0 : 1.0);
            Particle.Local = float4(Rest, 0.0);
            Particle.Previous = float4(Rest, 0.0);
            Particle.Velocity = float4(0.0, 0.0, 0.0, 0.0);
            Particle.World = float4(Command.Centre.xyz + RotateVector(Command.Orientation, Rest), 0.0);
            Particles[Base + Sector * MeridionalCount + Meridian] = Particle;
        }
    }
    Results[Tyre].ForceStatus = float4(0.0, 1.0, 0.0, 0.0);
}

[shader("compute")]
[numthreads(32, 1, 1)]
void AdvanceTyreMembrane(uint3 Thread : SV_DispatchThreadID)
{
    uint Tyre = Thread.x;
    if (Tyre >= InvocationExtent.TyreCount || !HasBuffers(Tyre)) return;
    TyreProjectionCommand Command = Commands[Tyre];
    if (!CommandValid(Command))
    {
        Results[Tyre].ForceStatus = float4(0.0, 0.0, 0.0, 0.0);
        return;
    }
    uint Base = Tyre * ParticleCount;
    float4 Q = Command.Orientation;
    float3 Down = RotateVector(float4(-Q.xyz, Q.w), -Command.GroundNormalDeflection.xyz);
    float ContactPhi = atan2(Down.z, Down.x);
    float Deflection = Command.GroundNormalDeflection.w;
    float PressureNorm = max(0.08, Command.PressureLateralSeconds.x / ReferencePressure);
    uint Substeps = Command.IterationCounts.x;
    float Dt = min(Command.PressureLateralSeconds.z / float(Substeps), 1.0 / 240.0);
    float Damping = exp(-36.0 * Dt);
    float LateralShift = clamp(Command.PressureLateralSeconds.y / 145000.0, -0.042, 0.042);
    float PatchSpan = clamp(0.44 / sqrt(max(0.15, PressureNorm)), 0.22, 1.10);
    float E1 = 1800.0 * (0.4 + 0.6 * PressureNorm), E2 = 1200.0 * (0.4 + 0.6 * PressureNorm);
    float3 Forces[ParticleCount];

    for (uint Step = 0; Step < Substeps; ++Step)
    {
        for (uint N = 0; N < ParticleCount; ++N) Forces[N] = float3(0.0, 0.0, 0.0);
        for (uint Sector = 0; Sector < CircumferentialCount; ++Sector)
        {
            uint NextSector = (Sector + 1) % CircumferentialCount;
            for (uint Meridian = 0; Meridian < MeridionalCount - 1; ++Meridian)
            {
                uint N0 = Sector * MeridionalCount + Meridian;
                uint N1 = NextSector * MeridionalCount + Meridian;
                uint N3 = Sector * MeridionalCount + Meridian + 1;
                TyreProjectionParticle P0 = Particles[Base + N0], P1 = Particles[Base + N1], P3 = Particles[Base + N3];
                float3 A1 = P1.RestInverseMass.xyz - P0.RestInverseMass.xyz;
                float3 A2 = P3.RestInverseMass.xyz - P0.RestInverseMass.xyz;
                float A11 = dot(A1, A1), A22 = dot(A2, A2), A12 = dot(A1, A2);
                float Area = sqrt(max(0.00000001, A11 * A22 - A12 * A12));
                float3 DeformedA1 = P1.Local.xyz - P0.Local.xyz, DeformedA2 = P3.Local.xyz - P0.Local.xyz;
                float Strain1 = 0.5 * (dot(DeformedA1, DeformedA1) - A11) / max(0.000001, A11);
                float Strain2 = 0.5 * (dot(DeformedA2, DeformedA2) - A22) / max(0.000001, A22);
                float S11 = E1 * (Strain1 + 0.38 * Strain2) * Area;
                float S22 = E2 * (Strain2 + 0.38 * Strain1) * Area;
                float3 F1 = S11 * DeformedA1, F2 = S22 * DeformedA2;
                Forces[N0] += F1 + F2;
                Forces[N1] -= F1;
                Forces[N3] -= F2;
            }
            uint B1 = Sector * MeridionalCount + 1, B2 = Sector * MeridionalCount + 2,
                 B3 = Sector * MeridionalCount + 3;
            TyreProjectionParticle P1 = Particles[Base + B1], P2 = Particles[Base + B2], P3 = Particles[Base + B3];
            float Curvature = (P1.Local.y + P3.Local.y - 2.0 * P2.Local.y) -
                              (P1.RestInverseMass.y + P3.RestInverseMass.y - 2.0 * P2.RestInverseMass.y);
            Forces[B2].y += 120.0 * Curvature;
            Forces[B1].y -= 60.0 * Curvature;
            Forces[B3].y -= 60.0 * Curvature;
        }
        for (uint Sector = 0; Sector < CircumferentialCount; ++Sector)
        {
            float Theta = 2.0 * Pi * float(Sector) / float(CircumferentialCount);
            float Delta = Theta - ContactPhi;
            Delta -= floor(Delta / (2.0 * Pi) + 0.5) * (2.0 * Pi);
            float Distance = abs(Delta);
            for (uint Meridian = 1; Meridian <= 3; ++Meridian)
            {
                uint N = Sector * MeridionalCount + Meridian, Index = Base + N;
                TyreProjectionParticle Particle = Particles[Index];
                float3 Velocity = (Particle.Velocity.xyz + Dt * Particle.RestInverseMass.w * Forces[N]) * Damping;
                float3 Next = Particle.Local.xyz + Dt * Velocity;
                float Radius = Meridian == 2 ? 0.50855 : 0.4865;
                float Fraction = (Radius - 0.3448) / (0.50855 - 0.3448);
                float RadialOffset = 0.0, WidthOffset = 0.0;
                if (Deflection > 0.00001)
                {
                    if (Distance < PatchSpan)
                    {
                        float Envelope = 0.5 * (1.0 + cos(Distance / PatchSpan * Pi));
                        RadialOffset = -Deflection * Envelope * Fraction;
                        if (Meridian != 2)
                            WidthOffset += (Meridian == 1 ? -1.0 : 1.0) * Deflection * 0.48 /
                                (0.4 + 0.6 * sqrt(PressureNorm)) * Envelope;
                    }
                    else if (Distance < PatchSpan * 1.5)
                        RadialOffset = sin((Distance - PatchSpan) / (0.5 * PatchSpan) * Pi) *
                            (0.11 * Deflection * Fraction);
                    if (Distance < PatchSpan * 1.2)
                        WidthOffset += LateralShift * 0.5 * (1.0 + cos(Distance / (PatchSpan * 1.2) * Pi)) * Fraction;
                }
                float GoalRadius = max(0.3448 + 0.008, Radius + RadialOffset);
                float3 Target = float3(GoalRadius * cos(Theta), Particle.RestInverseMass.y + WidthOffset,
                                       GoalRadius * sin(Theta));
                Next += (Target - Next) * 0.72;
                Particle.Previous = Particle.Local;
                Particle.Velocity.xyz = (Next - Particle.Local.xyz) / Dt;
                Particle.Local.xyz = Next;
                Particles[Index] = Particle;
            }
        }
    }
    SynchronizeWorld(Base, Command);
    Results[Tyre].ForceStatus = float4(Deflection * (25000.0 + 0.88 * Command.PressureLateralSeconds.x),
                                     1.0, 0.0, 0.0);
}
