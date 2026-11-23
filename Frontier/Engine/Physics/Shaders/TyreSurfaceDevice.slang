/*==========================================================================================================================================
                                                    TYRESURFACEDEVICE.SLANG
==========================================================================================================================================*/
// 🧩 Wheel-local spring and continuum vertex deformation, reusable from a vertex or compute shader.
//    Continuum reads the same particle layout written by TyreProjectionDevice.slang; it does not solve contact.
#include "TyreProjectionRecords.slang"

struct TyreSurfaceVertex
{
    float3 Position;
    float3 Normal;
};

float3 TyreSurfaceSafeNormal(float3 Value)
{
    float Magnitude = length(Value);
    return Magnitude > 0.000000000001 ? Value / Magnitude : Value;
}

TyreSurfaceVertex ResolveSpringTyreVertex(bool TyreSurface, bool Shredded, float4 Deformation,
                                        float3 Position, float3 Normal)
{
    TyreSurfaceVertex Result;
    Result.Position = Position;
    Result.Normal = Normal;
    if (TyreSurface && Shredded) { Result.Position = 0.0; return Result; }
    if (!TyreSurface || Deformation.y <= 0.00001) return Result;
    const float Pi = 3.14159265358979323846;
    const float RimRadius = 0.3448, OuterRadius = 0.50855;
    float Radius = length(Position.xz);
    if (Radius <= RimRadius + 0.0001) return Result;
    float Phi = atan2(Position.z, Position.x);
    float Delta = Phi - Deformation.x;
    Delta -= 2.0 * Pi * round(Delta / (2.0 * Pi));
    float PressureNorm = max(0.05, max(0.0, Deformation.z) / 220000.0);
    float PatchSpan = clamp(0.44 / sqrt(max(0.15, PressureNorm)), 0.20, 1.15);
    float Fraction = clamp((Radius - RimRadius) / (OuterRadius - RimRadius), 0.0, 1.0);
    float RadialOffset = 0.0, WidthOffset = 0.0, Distance = abs(Delta);
    if (Distance < PatchSpan)
    {
        float Envelope = 0.5 * (1.0 + cos(Distance / PatchSpan * Pi));
        RadialOffset = -Deformation.y * Envelope * Fraction;
        float Sidewall = sin(Fraction * Pi);
        float Bulge = 0.48 / (0.4 + 0.6 * sqrt(PressureNorm));
        float SignY = Position.y > 0.0 ? 1.0 : (Position.y < 0.0 ? -1.0 : 0.0);
        WidthOffset = SignY * Deformation.y * Bulge * Envelope * Sidewall;
    }
    float WaveOuter = PatchSpan * 1.5;
    if (Distance >= PatchSpan && Distance < WaveOuter)
        RadialOffset += sin((Distance - PatchSpan) / (WaveOuter - PatchSpan) * Pi) *
            (0.12 * Deformation.y * Fraction);
    if (Distance < PatchSpan * 1.2)
        WidthOffset += Deformation.w * 0.5 * (1.0 + cos(Distance / (PatchSpan * 1.2) * Pi)) * Fraction;
    float NewRadius = max(RimRadius, Radius + RadialOffset);
    Result.Position = float3(NewRadius * cos(Phi), Position.y + WidthOffset, NewRadius * sin(Phi));
    if (Distance < PatchSpan)
    {
        float Tilt = -sin(Delta / PatchSpan * Pi) * (Deformation.y / (OuterRadius * PatchSpan));
        Result.Normal = TyreSurfaceSafeNormal(Normal + float3(-sin(Phi), 0.0, cos(Phi)) * Tilt);
    }
    return Result;
}

float3 QueryTyreDisplacement(StructuredBuffer<TyreProjectionParticle> Particles, uint Base, uint Sector, uint Meridian)
{
    TyreProjectionParticle Particle = Particles[Base + Sector * 5 + Meridian];
    return Particle.Local.xyz - Particle.RestInverseMass.xyz;
}

TyreSurfaceVertex ResolveContinuumTyreVertex(StructuredBuffer<TyreProjectionParticle> Particles, uint Tyre,
                                           bool TyreSurface, bool Shredded, float Deflection,
                                           float3 Position, float3 Normal)
{
    TyreSurfaceVertex Result;
    Result.Position = Position;
    Result.Normal = Normal;
    if (TyreSurface && Shredded) { Result.Position = 0.0; return Result; }
    if (!TyreSurface || Deflection <= 0.00001) return Result;
    uint TotalParticles, ParticleStride;
    Particles.GetDimensions(TotalParticles, ParticleStride);
    if (TotalParticles < 160 || Tyre > (TotalParticles - 160) / 160) return Result;
    const float Pi = 3.14159265358979323846;
    const float RimRadius = 0.3448, OuterRadius = 0.50855;
    float Radius = length(Position.xz);
    if (Radius <= RimRadius + 0.0001) return Result;
    float Phi = atan2(Position.z, Position.x);
    float Turns = Phi / (2.0 * Pi) + 1.0;
    Turns -= floor(Turns);
    float SectorCoordinate = Turns * 32.0;
    uint S0 = uint(floor(SectorCoordinate)) & 31, S1 = (S0 + 1) & 31;
    float SectorFraction = SectorCoordinate - floor(SectorCoordinate);
    float RingCoordinate = clamp((Position.y / 0.118) * 1.35 + 2.0, 0.5, 3.5);
    uint M0 = min(uint(floor(RingCoordinate)), 3), M1 = M0 + 1;
    float RingFraction = clamp(RingCoordinate - float(M0), 0.0, 1.0);
    uint Base = Tyre * 160;
    float3 U00 = QueryTyreDisplacement(Particles, Base, S0, M0);
    float3 U10 = QueryTyreDisplacement(Particles, Base, S1, M0);
    float3 U01 = QueryTyreDisplacement(Particles, Base, S0, M1);
    float3 U11 = QueryTyreDisplacement(Particles, Base, S1, M1);
    float Fraction = clamp((Radius - RimRadius) / (OuterRadius - RimRadius), 0.0, 1.0);
    float U = clamp(Fraction / 0.88, 0.0, 1.0);
    float Weight = U * U * (3.0 - 2.0 * U);
    float3 Row0 = U00 * (1.0 - SectorFraction) + U10 * SectorFraction;
    float3 Row1 = U01 * (1.0 - SectorFraction) + U11 * SectorFraction;
    Result.Position += (Row0 * (1.0 - RingFraction) + Row1 * RingFraction) * Weight;
    float Sine = sin(Phi), Cosine = cos(Phi);
    float Tilt = ((U10.x - U00.x) * Cosine + (U10.z - U00.z) * Sine) * Weight * 4.5;
    Result.Normal = TyreSurfaceSafeNormal(Normal + float3(Sine, 0.0, -Cosine) * Tilt);
    return Result;
}
