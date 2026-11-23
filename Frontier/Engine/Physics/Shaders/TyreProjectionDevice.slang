/*==========================================================================================================================================
                                                  TYREPROJECTIONDEVICE.SLANG
==========================================================================================================================================*/
// 🧩 The VehicleHMI 32 x 5 XPBD carcass, with one invocation per independent tyre.
//    Sequential distance constraints deliberately preserve the demo's order; this is not a throughput claim.

#include "TyreProjectionRecords.slang"

[[vk::binding(0, 0)]] StructuredBuffer<TyreProjectionCommand> Commands;
[[vk::binding(1, 0)]] RWStructuredBuffer<TyreProjectionParticle> Particles;
[[vk::binding(2, 0)]] RWStructuredBuffer<TyreProjectionResult> Results;
[[vk::push_constant]] ConstantBuffer<TyreInvocationExtent> InvocationExtent;

static const uint CircumferentialCount = 32;
static const uint MeridionalCount = 5;
static const uint ParticleCount = CircumferentialCount * MeridionalCount;
static const float Pi = 3.14159265358979323846;
static const float ReferencePressure = 220000.0;

float3 RotateVector(float4 Q, float3 V)
{
    float3 T = 2.0 * cross(Q.xyz, V);
    return V + Q.w * T + cross(Q.xyz, T);
}

bool CommandValid(TyreProjectionCommand Command)
{
    float4 Ground = Command.GroundNormalDeflection;
    float4 Dynamics = Command.PressureLateralSeconds;
    return all(abs(Command.Centre.xyz) <= 1000000.0) &&
        all(abs(Command.Orientation) <= 2.0) && all(abs(Ground.xyz) <= 2.0) &&
        abs(dot(Command.Orientation, Command.Orientation) - 1.0) <= 0.001 &&
        abs(dot(Ground.xyz, Ground.xyz) - 1.0) <= 0.001 &&
        Ground.w >= 0.0 && Ground.w <= 1.0 && Dynamics.x >= 0.0 && Dynamics.x <= 2000000.0 &&
        abs(Dynamics.y) <= 10000000.0 && Dynamics.z >= 0.000001 && Dynamics.z <= 1.0 &&
        Dynamics.w == 0.0 && Command.Centre.w == 0.0 &&
        Command.IterationCounts.x >= 1 && Command.IterationCounts.x <= 32 &&
        all(Command.IterationCounts.yzw == uint3(0, 0, 0));
}

bool HasBuffers(uint Tyre)
{
    uint CommandCount, CommandStride, TotalParticles, ParticleStride, ResultCount, ResultStride;
    Commands.GetDimensions(CommandCount, CommandStride);
    Particles.GetDimensions(TotalParticles, ParticleStride);
    Results.GetDimensions(ResultCount, ResultStride);
    return Tyre < CommandCount && Tyre < ResultCount && TotalParticles >= ParticleCount &&
        Tyre <= (TotalParticles - ParticleCount) / ParticleCount;
}

void ProjectDistance(uint Base, uint IndexA, uint IndexB, float Compliance, float InverseDtSquared)
{
    TyreProjectionParticle A = Particles[Base + IndexA];
    TyreProjectionParticle B = Particles[Base + IndexB];
    float WeightA = A.RestInverseMass.w;
    float WeightB = B.RestInverseMass.w;
    if (WeightA + WeightB <= 0.0) return;
    float3 Difference = B.Local.xyz - A.Local.xyz;
    float Distance = length(Difference);
    if (Distance < 0.0000001) return;
    float RestDistance = length(B.RestInverseMass.xyz - A.RestInverseMass.xyz);
    float Multiplier = -(Distance - RestDistance) /
        (WeightA + WeightB + Compliance * InverseDtSquared) / Distance;
    if (WeightA > 0.0) A.Local.xyz -= WeightA * Multiplier * Difference;
    if (WeightB > 0.0) B.Local.xyz += WeightB * Multiplier * Difference;
    Particles[Base + IndexA] = A;
    Particles[Base + IndexB] = B;
}

void SynchronizeWorld(uint Base, TyreProjectionCommand Command)
{
    for (uint N = 0; N < ParticleCount; ++N)
    {
        TyreProjectionParticle Particle = Particles[Base + N];
        Particle.World = float4(Command.Centre.xyz + RotateVector(Command.Orientation, Particle.Local.xyz), 0.0);
        Particles[Base + N] = Particle;
    }
}

[shader("compute")]
[numthreads(32, 1, 1)]
void ResetTyreProjection(uint3 Thread : SV_DispatchThreadID)
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
        float Cosine = cos(Theta);
        float Sine = sin(Theta);
        for (uint Meridian = 0; Meridian < MeridionalCount; ++Meridian)
        {
            uint Index = Base + Sector * MeridionalCount + Meridian;
            float3 Rest = float3(Radii[Meridian] * Cosine, Widths[Meridian], Radii[Meridian] * Sine);
            TyreProjectionParticle Particle;
            Particle.RestInverseMass = float4(Rest, Meridian == 0 || Meridian == 4 ? 0.0 : 10.0);
            Particle.Local = float4(Rest, 0.0);
            Particle.Previous = float4(Rest, 0.0);
            Particle.Velocity = float4(0.0, 0.0, 0.0, 0.0);
            Particle.World = float4(Command.Centre.xyz + RotateVector(Command.Orientation, Rest), 0.0);
            Particles[Index] = Particle;
        }
    }
    Results[Tyre].ForceStatus = float4(0.0, 1.0, 0.0, 0.0);
}

[shader("compute")]
[numthreads(32, 1, 1)]
void AdvanceTyreProjection(uint3 Thread : SV_DispatchThreadID)
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
    float InverseDtSquared = 1.0 / (Dt * Dt);
    float Damping = exp(-32.0 * Dt);
    float LateralShift = clamp(Command.PressureLateralSeconds.y / 145000.0, -0.042, 0.042);
    float PatchSpan = clamp(0.44 / sqrt(max(0.15, PressureNorm)), 0.22, 1.10);

    for (uint Step = 0; Step < Substeps; ++Step)
    {
        for (uint N = 0; N < ParticleCount; ++N)
        {
            TyreProjectionParticle Particle = Particles[Base + N];
            if (Particle.RestInverseMass.w <= 0.0) continue;
            Particle.Previous = Particle.Local;
            float Restore = 420.0 * (0.35 + 0.65 * PressureNorm);
            float3 Velocity = (Particle.Velocity.xyz + Dt * Restore *
                (Particle.RestInverseMass.xyz - Particle.Local.xyz)) * Damping;
            Particle.Local.xyz += Dt * Velocity;
            Particles[Base + N] = Particle;
        }
        for (uint Sector = 0; Sector < CircumferentialCount; ++Sector)
        {
            uint Next = (Sector + 1) % CircumferentialCount;
            for (uint Meridian = 1; Meridian <= 3; ++Meridian)
                ProjectDistance(Base, Sector * MeridionalCount + Meridian, Next * MeridionalCount + Meridian,
                                Meridian == 2 ? 0.000015 : 0.000035, InverseDtSquared);
            for (uint Meridian = 0; Meridian < MeridionalCount - 1; ++Meridian)
                ProjectDistance(Base, Sector * MeridionalCount + Meridian, Sector * MeridionalCount + Meridian + 1,
                                0.000025, InverseDtSquared);
            for (uint Meridian = 0; Meridian < MeridionalCount - 1; ++Meridian)
                ProjectDistance(Base, Sector * MeridionalCount + Meridian, Next * MeridionalCount + Meridian + 1,
                                0.000060, InverseDtSquared);
        }
        for (uint Sector = 0; Sector < CircumferentialCount; ++Sector)
        {
            float Theta = 2.0 * Pi * float(Sector) / float(CircumferentialCount);
            float Delta = Theta - ContactPhi;
            Delta -= floor(Delta / (2.0 * Pi) + 0.5) * (2.0 * Pi);
            float Distance = abs(Delta);
            for (uint Meridian = 1; Meridian <= 3; ++Meridian)
            {
                uint Index = Base + Sector * MeridionalCount + Meridian;
                TyreProjectionParticle Particle = Particles[Index];
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
                Particle.Local.xyz += (Target - Particle.Local.xyz) * 0.75;
                Particle.Velocity.xyz = (Particle.Local.xyz - Particle.Previous.xyz) / Dt;
                Particles[Index] = Particle;
            }
        }
    }
    SynchronizeWorld(Base, Command);
    float Force = Deflection * (25000.0 + 0.88 * Command.PressureLateralSeconds.x);
    Results[Tyre].ForceStatus = float4(Force, 1.0, 0.0, 0.0);
}
