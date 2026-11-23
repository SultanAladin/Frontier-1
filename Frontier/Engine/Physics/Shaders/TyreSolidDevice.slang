/*==========================================================================================================================================
                                                    TYRESOLIDDEVICE.SLANG
==========================================================================================================================================*/
// 🧩 The VehicleHMI 128-cell Hex8 solid, one sequential force accumulation per independent tyre.
//    The contact shape and force remain prescribed by the demo; this is not a solved road reaction.
#include "TyreProjectionRecords.slang"
#include "TyreSolidConstitutive.slang"

struct TyreSolidCellFootprint
{
    uint4 Corners;
    float4 GradientsVolume[4];
};
struct TyreSolidDeviceResult
{
    float4 ForceStatusJacobianStress;
    uint4 ContactCount;
};

[[vk::binding(0, 0)]] StructuredBuffer<TyreProjectionCommand> Commands;
[[vk::binding(1, 0)]] RWStructuredBuffer<TyreProjectionParticle> Particles;
[[vk::binding(2, 0)]] RWStructuredBuffer<TyreSolidDeviceResult> Results;
[[vk::binding(3, 0)]] StructuredBuffer<TyreSolidCellFootprint> Cells;
[[vk::push_constant]] ConstantBuffer<TyreInvocationExtent> InvocationExtent;

static const uint CircumferentialCount = 32;
static const uint MeridionalCount = 5;
static const uint ParticleCount = 160;
static const uint CellCount = 128;
static const float Pi = 3.14159265358979323846;
static const float ReferencePressure = 220000.0;

float3 RotateSolidVector(float4 Q, float3 V)
{
    float3 T = 2.0 * cross(Q.xyz, V);
    return V + Q.w * T + cross(Q.xyz, T);
}

bool SolidCommandValid(TyreProjectionCommand Command)
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

bool SolidBuffersPresent(uint Tyre)
{
    uint CommandCount, CommandStride, TotalParticles, ParticleStride, ResultCount, ResultStride, TotalCells, CellStride;
    Commands.GetDimensions(CommandCount, CommandStride);
    Particles.GetDimensions(TotalParticles, ParticleStride);
    Results.GetDimensions(ResultCount, ResultStride);
    Cells.GetDimensions(TotalCells, CellStride);
    return Tyre < CommandCount && Tyre < ResultCount && TotalCells >= CellCount && TotalParticles >= ParticleCount &&
        Tyre <= (TotalParticles - ParticleCount) / ParticleCount;
}

bool SolidCellsValid()
{
    for (uint Cell = 0; Cell < CellCount; ++Cell)
    {
        TyreSolidCellFootprint Shape = Cells[Cell];
        if (any(Shape.Corners >= ParticleCount)) return false;
        if (!(Shape.GradientsVolume[0].w > 0.0) || !(Shape.GradientsVolume[0].w <= 1000000.0)) return false;
        for (uint Corner = 0; Corner < 4; ++Corner)
        {
            if (!all(abs(Shape.GradientsVolume[Corner].xyz) <= 1000000.0)) return false;
            if (Corner != 0 && Shape.GradientsVolume[Corner].w != 0.0) return false;
        }
    }
    return true;
}

void RejectSolidCommand(uint Tyre)
{
    TyreSolidDeviceResult Result;
    Result.ForceStatusJacobianStress = float4(0.0, 0.0, 0.0, 0.0);
    Result.ContactCount = uint4(0, 0, 0, 0);
    Results[Tyre] = Result;
}

[shader("compute")]
[numthreads(32, 1, 1)]
void ResetTyreSolid(uint3 Thread : SV_DispatchThreadID)
{
    uint Tyre = Thread.x;
    if (Tyre >= InvocationExtent.TyreCount || !SolidBuffersPresent(Tyre)) return;
    TyreProjectionCommand Command = Commands[Tyre];
    if (!SolidCommandValid(Command) || !SolidCellsValid())
    {
        RejectSolidCommand(Tyre);
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
            float InverseMass = Meridian == 0 || Meridian == 4 ? 0.0 : (Meridian == 2 ? 0.85 : 1.10);
            TyreProjectionParticle Particle;
            Particle.RestInverseMass = float4(Rest, InverseMass);
            Particle.Local = float4(Rest, 0.0);
            Particle.Previous = float4(Rest, 0.0);
            Particle.Velocity = float4(0.0, 0.0, 0.0, 0.0);
            Particle.World = float4(Command.Centre.xyz + RotateSolidVector(Command.Orientation, Rest), 0.0);
            Particles[Base + Sector * MeridionalCount + Meridian] = Particle;
        }
    }
    TyreSolidDeviceResult Result;
    Result.ForceStatusJacobianStress = float4(0.0, 1.0, 1.0, 0.0);
    Result.ContactCount = uint4(0, 0, 0, 0);
    Results[Tyre] = Result;
}

[shader("compute")]
[numthreads(32, 1, 1)]
void AdvanceTyreSolid(uint3 Thread : SV_DispatchThreadID)
{
    uint Tyre = Thread.x;
    if (Tyre >= InvocationExtent.TyreCount || !SolidBuffersPresent(Tyre)) return;
    TyreProjectionCommand Command = Commands[Tyre];
    if (!SolidCommandValid(Command) || !SolidCellsValid())
    {
        RejectSolidCommand(Tyre);
        return;
    }
    uint Base = Tyre * ParticleCount;
    float4 Q = Command.Orientation;
    float3 Down = RotateSolidVector(float4(-Q.xyz, Q.w), -Command.GroundNormalDeflection.xyz);
    float ContactPhi = atan2(Down.z, Down.x);
    float Deflection = Command.GroundNormalDeflection.w;
    float PressureNorm = max(0.08, Command.PressureLateralSeconds.x / ReferencePressure);
    float Young = 12000.0 * (0.32 + 0.68 * PressureNorm), Nu = 0.44;
    float Mu = Young / (2.0 * (1.0 + Nu)), Lambda = Young * Nu / ((1.0 + Nu) * (1.0 - 2.0 * Nu));
    uint Substeps = Command.IterationCounts.x;
    float Dt = min(Command.PressureLateralSeconds.z / float(Substeps), 1.0 / 240.0);
    float Damping = exp(-38.0 * Dt);
    float LateralShift = clamp(Command.PressureLateralSeconds.y / 145000.0, -0.042, 0.042);
    float PatchSpan = clamp(0.44 / sqrt(max(0.15, PressureNorm)), 0.22, 1.10);
    float JacobianSum = 0.0, StressSum = 0.0;
    uint Contacts = 0;
    float3 Forces[ParticleCount];

    for (uint Step = 0; Step < Substeps; ++Step)
    {
        for (uint N = 0; N < ParticleCount; ++N) Forces[N] = float3(0.0, 0.0, 0.0);
        JacobianSum = 0.0;
        StressSum = 0.0;
        for (uint Cell = 0; Cell < CellCount; ++Cell)
        {
            TyreSolidCellFootprint Shape = Cells[Cell];
            float3 F0 = float3(1.0, 0.0, 0.0), F1 = float3(0.0, 1.0, 0.0), F2 = float3(0.0, 0.0, 1.0);
            for (uint Corner = 0; Corner < 4; ++Corner)
            {
                TyreProjectionParticle Particle = Particles[Base + Shape.Corners[Corner]];
                float3 U = Particle.Local.xyz - Particle.RestInverseMass.xyz;
                float3 Gradient = Shape.GradientsVolume[Corner].xyz;
                F0 += U.x * Gradient;
                F1 += U.y * Gradient;
                F2 += U.z * Gradient;
            }
            TyreSolidStress Stress = EvaluateTyreSolidStress(F0, F1, F2, Mu, Lambda);
            JacobianSum += Stress.Jacobian;
            float3 P0 = Stress.Row0, P1 = Stress.Row1, P2 = Stress.Row2;
            StressSum += length(float3(P0.x, P1.y, P2.z));
            for (uint Corner = 0; Corner < 4; ++Corner)
            {
                uint N = Shape.Corners[Corner];
                if (Particles[Base + N].RestInverseMass.w <= 0.0) continue;
                float3 Gradient = Shape.GradientsVolume[Corner].xyz;
                Forces[N] -= Shape.GradientsVolume[0].w * float3(dot(P0, Gradient), dot(P1, Gradient), dot(P2, Gradient));
            }
        }
        Contacts = 0;
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
                float3 Displacement = Particle.Local.xyz - Particle.RestInverseMass.xyz;
                float3 Velocity = (Particle.Velocity.xyz + Dt * Particle.RestInverseMass.w *
                    (Forces[Sector * MeridionalCount + Meridian] - 450.0 * (0.4 + 0.6 * PressureNorm) * Displacement)) * Damping;
                float3 Next = Particle.Local.xyz + Dt * Velocity;
                if (Deflection > 0.00001)
                {
                    if (Distance < PatchSpan) ++Contacts;
                    float Radius = Meridian == 2 ? 0.50855 : 0.4865;
                    float Fraction = (Radius - 0.3448) / (0.50855 - 0.3448);
                    float RadialOffset = 0.0, WidthOffset = 0.0;
                    if (Distance < PatchSpan)
                    {
                        float Envelope = 0.5 * (1.0 + cos(Distance / PatchSpan * Pi));
                        RadialOffset = -Deflection * Envelope * Fraction;
                        if (Meridian != 2)
                            WidthOffset += (Meridian == 1 ? -1.0 : 1.0) * Deflection * 0.52 /
                                (0.4 + 0.6 * sqrt(PressureNorm)) * Envelope;
                    }
                    else if (Distance < PatchSpan * 1.5)
                        RadialOffset = sin((Distance - PatchSpan) / (0.5 * PatchSpan) * Pi) *
                            (0.12 * Deflection * Fraction);
                    if (Distance < PatchSpan * 1.2)
                        WidthOffset += LateralShift * 0.5 * (1.0 + cos(Distance / (PatchSpan * 1.2) * Pi)) * Fraction;
                    float GoalRadius = max(0.3448 + 0.008, Radius + RadialOffset);
                    float3 Target = float3(GoalRadius * cos(Theta), Particle.RestInverseMass.y + WidthOffset,
                                           GoalRadius * sin(Theta));
                    float Blend = Distance < PatchSpan * 1.5 ? 0.78 : 0.45;
                    Next += (Target - Next) * Blend;
                }
                Particle.Previous = Particle.Local;
                Particle.Velocity.xyz = (Next - Particle.Local.xyz) / Dt;
                Particle.Local.xyz = Next;
                Particles[Index] = Particle;
            }
        }
    }
    for (uint N = 0; N < ParticleCount; ++N)
    {
        TyreProjectionParticle Particle = Particles[Base + N];
        Particle.World = float4(Command.Centre.xyz + RotateSolidVector(Command.Orientation, Particle.Local.xyz), 0.0);
        Particles[Base + N] = Particle;
    }
    TyreSolidDeviceResult Result;
    Result.ForceStatusJacobianStress = float4(Deflection * (25000.0 + 0.90 * Command.PressureLateralSeconds.x),
                                               1.0, JacobianSum / float(CellCount), StressSum / float(CellCount));
    Result.ContactCount = uint4(Contacts, 0, 0, 0);
    Results[Tyre] = Result;
}
