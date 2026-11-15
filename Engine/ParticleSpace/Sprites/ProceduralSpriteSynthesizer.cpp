/*============================================================================================================================================
                                                   PROCEDURALSPRITESYNTHESIZER.CPP
============================================================================================================================================*/
// ❄️ Deterministic multi-channel procedural sprite, crystal, wing, and dissolve-mask synthesizer implementation

#include "Engine/ParticleSpace/Sprites/ProceduralSpriteSynthesizer.h"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace Frontier
{

namespace
{

constexpr float kPi    = 3.14159265358979323846f;
constexpr float kTwoPi = 6.28318530717958647692f;

[[nodiscard]] inline float Saturate(float V) noexcept
{
    return std::clamp(V, 0.0f, 1.0f);
}

[[nodiscard]] inline float SmoothStep(float Edge0, float Edge1, float X) noexcept
{
    if (std::fabs(Edge1 - Edge0) < 1e-6f)
    {
        return X >= Edge1 ? 1.0f : 0.0f;
    }
    const float T = Saturate((X - Edge0) / (Edge1 - Edge0));
    return T * T * (3.0f - 2.0f * T);
}

[[nodiscard]] inline float LerpScalar(float A, float B, float T) noexcept
{
    return A + (B - A) * T;
}

[[nodiscard]] inline uint8_t FloatToByte(float V) noexcept
{
    return static_cast<uint8_t>(std::lround(Saturate(V) * 255.0f));
}

[[nodiscard]] inline uint32_t MixSeed(uint32_t X) noexcept
{
    X ^= X >> 16u;
    X *= 0x7feb352du;
    X ^= X >> 15u;
    X *= 0x846ca68bu;
    X ^= X >> 16u;
    return X;
}

[[nodiscard]] inline float Hash2D(int32_t Ix, int32_t Iy, uint32_t Seed) noexcept
{
    const uint32_t H = MixSeed(static_cast<uint32_t>(Ix * 374761393) ^ static_cast<uint32_t>(Iy * 668265263) ^ (Seed * 2147483647u));
    return static_cast<float>(H & 0x00ffffffu) / 16777215.0f;
}

[[nodiscard]] inline float Hash2DAlt(int32_t Ix, int32_t Iy, uint32_t Seed) noexcept
{
    return Hash2D(Ix + 19, Iy - 47, Seed ^ 0x9e3779b9u);
}

[[nodiscard]] float ScalarNoise2D(float X, float Y, uint32_t Seed) noexcept
{
    const int32_t Ix = static_cast<int32_t>(std::floor(X));
    const int32_t Iy = static_cast<int32_t>(std::floor(Y));
    const float   Fx = X - static_cast<float>(Ix);
    const float   Fy = Y - static_cast<float>(Iy);
    const float   Ux = Fx * Fx * (3.0f - 2.0f * Fx);
    const float   Uy = Fy * Fy * (3.0f - 2.0f * Fy);

    const float N00 = Hash2D(Ix,     Iy,     Seed);
    const float N10 = Hash2D(Ix + 1, Iy,     Seed);
    const float N01 = Hash2D(Ix,     Iy + 1, Seed);
    const float N11 = Hash2D(Ix + 1, Iy + 1, Seed);
    return LerpScalar(LerpScalar(N00, N10, Ux), LerpScalar(N01, N11, Ux), Uy);
}

[[nodiscard]] float MultiOctaveNoise2D(float X, float Y, uint32_t Seed, int Octaves) noexcept
{
    float Sum = 0.0f;
    float Amp = 0.5f;
    float Freq = 1.0f;
    for (int I = 0; I < Octaves; ++I)
    {
        Sum += Amp * ScalarNoise2D(X * Freq, Y * Freq, Seed + static_cast<uint32_t>(I * 97));
        Freq *= 2.04f;
        Amp *= 0.5f;
    }
    return Sum;
}

[[nodiscard]] float SegmentDistance2D(float Px, float Py, float Ax, float Ay, float Bx, float By) noexcept
{
    const float Vx = Bx - Ax;
    const float Vy = By - Ay;
    const float Wx = Px - Ax;
    const float Wy = Py - Ay;
    const float LenSq = Vx * Vx + Vy * Vy;
    const float T = (LenSq > 1e-8f) ? Saturate((Wx * Vx + Wy * Vy) / LenSq) : 0.0f;
    return std::hypot(Wx - Vx * T, Wy - Vy * T);
}

[[nodiscard]] float RegularPolygonSdf(float Px, float Py, int Sides, float Radius) noexcept
{
    const float Sector = kTwoPi / static_cast<float>(Sides);
    const float Angle  = std::atan2(Py, Px);
    const float Local  = std::fmod(Angle + kTwoPi + Sector * 0.5f, Sector) - Sector * 0.5f;
    const float R      = std::hypot(Px, Py);
    return R * std::cos(Local) - Radius * std::cos(Sector * 0.5f);
}

struct VoronoiDistancePair
{
    float NearestDist = 9.0f;
    float SecondDist  = 9.0f;
    float CellHash    = 0.0f;
};

[[nodiscard]] VoronoiDistancePair SampleVoronoiPeriodic2D(float Ux, float Uy, float CellScale, uint32_t Seed) noexcept
{
    const int32_t Period = std::max<int32_t>(2, static_cast<int32_t>(std::lround(CellScale)));
    const float   Sx     = (Ux * 0.5f + 0.5f) * static_cast<float>(Period);
    const float   Sy     = (Uy * 0.5f + 0.5f) * static_cast<float>(Period);
    const int32_t Cx     = static_cast<int32_t>(std::floor(Sx));
    const int32_t Cy     = static_cast<int32_t>(std::floor(Sy));

    VoronoiDistancePair Result{};
    for (int32_t Dy = -1; Dy <= 1; ++Dy)
    {
        for (int32_t Dx = -1; Dx <= 1; ++Dx)
        {
            const int32_t Nx = ((Cx + Dx) % Period + Period) % Period;
            const int32_t Ny = ((Cy + Dy) % Period + Period) % Period;
            const float   Ox = 0.14f + 0.72f * Hash2D(Nx, Ny, Seed);
            const float   Oy = 0.14f + 0.72f * Hash2DAlt(Nx, Ny, Seed);
            const float   Px = static_cast<float>(Cx + Dx) + Ox;
            const float   Py = static_cast<float>(Cy + Dy) + Oy;
            const float   Dist = std::hypot(Px - Sx, Py - Sy);
            if (Dist < Result.NearestDist)
            {
                Result.SecondDist  = Result.NearestDist;
                Result.NearestDist = Dist;
                Result.CellHash    = Hash2D(Nx + 5, Ny + 13, Seed ^ 0x517cc1b7u);
            }
            else if (Dist < Result.SecondDist)
            {
                Result.SecondDist = Dist;
            }
        }
    }
    return Result;
}

[[nodiscard]] ProceduralSpriteSample SampleSnowCrystal(
    const ProceduralSpriteSpec& Spec,
    float                       UnitX,
    float                       UnitY,
    uint32_t                    TileSeed,
    int                         FoldCount,
    bool                        FernMode,
    bool                        PlateMode) noexcept
{
    ProceduralSpriteSample Out{};
    const float Rot = (static_cast<float>(MixSeed(TileSeed) & 0xffffu) / 65535.0f) * kTwoPi;
    const float C   = std::cos(Rot);
    const float S   = std::sin(Rot);
    const float Rx  = UnitX * C - UnitY * S;
    const float Ry  = UnitX * S + UnitY * C;

    const float Radius = std::hypot(Rx, Ry);
    const float Angle  = std::atan2(Ry, Rx);
    const float Sector = kTwoPi / static_cast<float>(FoldCount);
    const float Folded = std::fabs(std::fmod(Angle + kTwoPi + Sector * 0.5f, Sector) - Sector * 0.5f);
    const float Px     = Radius * std::cos(Folded);
    const float Py     = Radius * std::sin(Folded);

    float MinDist = 1.0f;
    if (PlateMode)
    {
        const float OuterHex = RegularPolygonSdf(Rx, Ry, 6, 0.78f);
        const float InnerHex = std::fabs(RegularPolygonSdf(Rx, Ry, 6, 0.46f)) - 0.032f;
        const float Spine    = SegmentDistance2D(Px, Py, 0.0f, 0.0f, 0.72f, 0.0f) - 0.022f;
        const float Terrace  = std::fabs(RegularPolygonSdf(Rx, Ry, 6, 0.24f)) - 0.024f;
        MinDist = std::min(OuterHex, std::min(Spine, std::min(InnerHex, Terrace)));

        const float InsidePlate = 1.0f - SmoothStep(-0.025f, 0.025f, OuterHex);
        const float Ridge       = 1.0f - SmoothStep(0.0f, 0.055f, std::min(std::fabs(InnerHex), std::min(std::fabs(Terrace), std::fabs(Spine))));
        Out.Alpha             = InsidePlate;
        Out.Height            = Saturate(InsidePlate * (0.48f + 0.48f * Ridge));
        Out.SignedDistance01  = Saturate(0.5f - OuterHex * 1.4f);
        Out.Glint             = Saturate(Ridge * 0.85f + SmoothStep(0.18f, 0.0f, Radius) * 0.4f);
        Out.DissolveThreshold = Saturate(1.0f - Radius * 1.05f);
    }
    else
    {
        const float ArmLen   = 0.84f;
        const float SpineW   = FernMode ? 0.022f : 0.030f;
        const float SpineD   = SegmentDistance2D(Px, Py, 0.0f, 0.0f, ArmLen, 0.0f) - SpineW * (1.0f - 0.35f * Saturate(Px / ArmLen));
        const float CenterHx = RegularPolygonSdf(Rx, Ry, 6, 0.16f + 0.05f * Spec.BranchDensity);
        MinDist = std::min(SpineD, CenterHx);

        const int BranchCount = FernMode ? 6 : 4;
        const float SeedJitter = (static_cast<float>(MixSeed(TileSeed ^ 0xa3c59ac3u) & 0xffu) / 255.0f) * 0.05f;
        for (int B = 0; B < BranchCount; ++B)
        {
            const float Frac = 0.22f + (static_cast<float>(B) / static_cast<float>(BranchCount)) * 0.56f + SeedJitter * 0.4f;
            const float AnchorX = Frac * ArmLen;
            const float Span    = (FernMode ? 0.26f : 0.30f) * (1.0f - 0.58f * Frac) * (0.65f + 0.55f * Spec.BranchDensity);
            const float TipX    = AnchorX + Span * 0.52f;
            const float TipY    = Span * 0.866f;
            const float Thick   = (FernMode ? 0.014f : 0.020f) * (1.0f - 0.35f * Frac);
            const float BranchD = SegmentDistance2D(Px, Py, AnchorX, 0.0f, TipX, TipY) - Thick;
            MinDist = std::min(MinDist, BranchD);

            if (FernMode || Spec.BranchDensity > 0.55f)
            {
                const float MidX  = LerpScalar(AnchorX, TipX, 0.52f);
                const float MidY  = LerpScalar(0.0f, TipY, 0.52f);
                const float SubD  = SegmentDistance2D(Px, Py, MidX, MidY, MidX + Span * 0.28f, MidY + Span * 0.02f) - Thick * 0.72f;
                MinDist = std::min(MinDist, SubD);
            }
        }

        const float Softness = LerpScalar(0.042f, 0.012f, Spec.EdgeCrispness);
        Out.Alpha             = 1.0f - SmoothStep(-Softness * 0.35f, Softness, MinDist);
        Out.Height            = Saturate(Out.Alpha * (0.35f + 0.65f * SmoothStep(Softness, -0.028f, MinDist)));
        Out.SignedDistance01  = Saturate(0.5f - MinDist * 3.0f);
        const float Sparkle   = MultiOctaveNoise2D(Rx * 12.0f, Ry * 12.0f, TileSeed + 17u, 2);
        Out.Glint             = Saturate(Out.Alpha * SmoothStep(0.56f, 0.92f, Sparkle) + SmoothStep(0.018f, -0.02f, MinDist) * 0.55f);
        Out.DissolveThreshold = Saturate(1.0f - Radius * 1.12f);
    }

    const float RimMix = Saturate(Radius * 0.85f + 0.15f * (1.0f - Out.Height));
    Out.Red   = Saturate(LerpScalar(Spec.PrimaryTintR, Spec.SecondaryTintR, RimMix) + Out.Glint * 0.16f);
    Out.Green = Saturate(LerpScalar(Spec.PrimaryTintG, Spec.SecondaryTintG, RimMix) + Out.Glint * 0.16f);
    Out.Blue  = Saturate(LerpScalar(Spec.PrimaryTintB, Spec.SecondaryTintB, RimMix) + Out.Glint * 0.18f);
    return Out;
}

[[nodiscard]] ProceduralSpriteSample SampleButterflyWingPair(
    const ProceduralSpriteSpec& Spec,
    float                       UnitX,
    float                       UnitY,
    uint32_t                    TileSeed) noexcept
{
    ProceduralSpriteSample Out{};
    const float Ax = std::fabs(UnitX);
    const float Ay = UnitY;

    // Upper forewing + lower hindwing analytical lobes
    const float ForeDx = (Ax - 0.44f) / 0.44f;
    const float ForeDy = (Ay + 0.18f - 0.24f * Ax) / 0.38f;
    const float ForeAngle = std::atan2(ForeDy, ForeDx);
    const float Scallop = 0.045f * std::sin(ForeAngle * 7.0f);
    const float ForeSdf = std::hypot(ForeDx, ForeDy) - (0.94f + Scallop);

    const float HindDx = (Ax - 0.31f) / 0.32f;
    const float HindDy = (Ay - 0.34f + 0.12f * Ax) / 0.34f;
    const float HindSdf = std::hypot(HindDx, HindDy) - 0.86f;

    const float BodySdf = std::hypot(Ax / 0.055f, (Ay - 0.04f) / 0.46f) - 1.0f;
    const float WingSdf = std::min(ForeSdf, HindSdf);
    const float FullSdf = std::min(WingSdf, BodySdf);

    const float Soft = LerpScalar(0.055f, 0.018f, Spec.EdgeCrispness);
    Out.Alpha = 1.0f - SmoothStep(-Soft * 0.3f, Soft, FullSdf);

    const float WingAngle = std::atan2(Ay + 0.05f, Ax + 0.06f);
    const float VeinWave  = std::pow(std::fabs(std::sin(WingAngle * 11.0f + Ax * 3.2f)), 10.0f);
    const float BorderBand = SmoothStep(-0.22f, -0.03f, WingSdf);
    const float InnerGlow  = 1.0f - BorderBand;
    const float SpotGrid   = SmoothStep(0.68f, 0.90f, MultiOctaveNoise2D(Ax * 10.0f, Ay * 10.0f, TileSeed + 31u, 2));
    const float BodyMask   = 1.0f - SmoothStep(-0.08f, 0.08f, BodySdf);

    float R = LerpScalar(Spec.PrimaryTintR, Spec.SecondaryTintR, Saturate(Ax * 0.95f));
    float G = LerpScalar(Spec.PrimaryTintG, Spec.SecondaryTintG, Saturate(Ax * 0.95f));
    float B = LerpScalar(Spec.PrimaryTintB, Spec.SecondaryTintB, Saturate(Ax * 0.95f));

    // Dark velvet margin + pale margin dots + dark wing veins
    const float DarkFactor = Saturate(BorderBand * 0.82f + VeinWave * 0.55f);
    R = LerpScalar(R, 0.04f, DarkFactor) + BorderBand * SpotGrid * 0.65f;
    G = LerpScalar(G, 0.05f, DarkFactor) + BorderBand * SpotGrid * 0.72f;
    B = LerpScalar(B, 0.08f, DarkFactor) + BorderBand * SpotGrid * 0.85f;

    R = LerpScalar(R, 0.06f, BodyMask);
    G = LerpScalar(G, 0.07f, BodyMask);
    B = LerpScalar(B, 0.09f, BodyMask);

    Out.Red               = Saturate(R);
    Out.Green             = Saturate(G);
    Out.Blue              = Saturate(B);
    Out.Height            = Saturate(Out.Alpha * (0.45f + 0.30f * VeinWave + 0.25f * BodyMask));
    Out.Glint             = Saturate(InnerGlow * (0.65f + 0.35f * (1.0f - VeinWave)) * Out.Alpha);
    Out.SignedDistance01  = Saturate(0.5f - FullSdf * 0.85f);
    Out.DissolveThreshold = Saturate(1.0f - Ax * 0.95f);
    return Out;
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

ProceduralSpriteFamily QueryProceduralSpriteFamily(ProceduralSpriteArchetype Archetype) noexcept
{
    switch (Archetype)
    {
        case ProceduralSpriteArchetype::SnowStellarDendrite:
        case ProceduralSpriteArchetype::SnowFernlikeDendrite:
        case ProceduralSpriteArchetype::SnowHexPlate:
        case ProceduralSpriteArchetype::SnowTwelveFoldStar:
            return ProceduralSpriteFamily::SnowCrystal;

        case ProceduralSpriteArchetype::OpticalHexBokeh:
        case ProceduralSpriteArchetype::OpticalStarburstFlare:
        case ProceduralSpriteArchetype::OpticalSoftGlow:
            return ProceduralSpriteFamily::OpticalFlare;

        case ProceduralSpriteArchetype::PyroFlameWisp:
        case ProceduralSpriteArchetype::PyroSmokePuff:
        case ProceduralSpriteArchetype::PyroEmberSpark:
            return ProceduralSpriteFamily::PyroPlume;

        case ProceduralSpriteArchetype::LiquidRainDrop:
        case ProceduralSpriteArchetype::LiquidSoapBubble:
            return ProceduralSpriteFamily::LiquidDroplet;

        case ProceduralSpriteArchetype::BotanicalMorphoButterfly:
        case ProceduralSpriteArchetype::BotanicalSakuraPetal:
            return ProceduralSpriteFamily::BotanicalWing;

        case ProceduralSpriteArchetype::TechHexEnergyShield:
        case ProceduralSpriteArchetype::TechVoronoiDissolveMask:
        case ProceduralSpriteArchetype::Count:
            return ProceduralSpriteFamily::TechShield;
    }
    return ProceduralSpriteFamily::SnowCrystal;
}

std::string_view QueryProceduralSpriteTitle(ProceduralSpriteArchetype Archetype) noexcept
{
    switch (Archetype)
    {
        case ProceduralSpriteArchetype::SnowStellarDendrite:      return "Snow Stellar Dendrite";
        case ProceduralSpriteArchetype::SnowFernlikeDendrite:     return "Snow Fernlike Dendrite";
        case ProceduralSpriteArchetype::SnowHexPlate:             return "Snow Hex Plate";
        case ProceduralSpriteArchetype::SnowTwelveFoldStar:       return "Snow Twelve-Fold Star";
        case ProceduralSpriteArchetype::OpticalHexBokeh:          return "Optical Hex Bokeh";
        case ProceduralSpriteArchetype::OpticalStarburstFlare:    return "Optical Starburst Flare";
        case ProceduralSpriteArchetype::OpticalSoftGlow:          return "Optical Soft Glow";
        case ProceduralSpriteArchetype::PyroFlameWisp:            return "Pyro Flame Wisp";
        case ProceduralSpriteArchetype::PyroSmokePuff:            return "Pyro Smoke Puff";
        case ProceduralSpriteArchetype::PyroEmberSpark:           return "Pyro Ember Spark";
        case ProceduralSpriteArchetype::LiquidRainDrop:           return "Liquid Rain Drop";
        case ProceduralSpriteArchetype::LiquidSoapBubble:         return "Liquid Soap Bubble";
        case ProceduralSpriteArchetype::BotanicalMorphoButterfly: return "Botanical Morpho Butterfly";
        case ProceduralSpriteArchetype::BotanicalSakuraPetal:     return "Botanical Sakura Petal";
        case ProceduralSpriteArchetype::TechHexEnergyShield:      return "Tech Hex Energy Shield";
        case ProceduralSpriteArchetype::TechVoronoiDissolveMask:  return "Tech Voronoi Dissolve Mask";
        case ProceduralSpriteArchetype::Count:                    return "Procedural Sprite";
    }
    return "Procedural Sprite";
}

ProceduralSpriteSpec QueryDefaultProceduralSpriteSpec(ProceduralSpriteArchetype Archetype) noexcept
{
    ProceduralSpriteSpec Spec{};
    Spec.Archetype = Archetype;

    switch (Archetype)
    {
        case ProceduralSpriteArchetype::SnowStellarDendrite:
        case ProceduralSpriteArchetype::SnowFernlikeDendrite:
        case ProceduralSpriteArchetype::SnowHexPlate:
        case ProceduralSpriteArchetype::SnowTwelveFoldStar:
            Spec.PrimaryTintR = 0.94f; Spec.PrimaryTintG = 0.98f; Spec.PrimaryTintB = 1.00f;
            Spec.SecondaryTintR = 0.56f; Spec.SecondaryTintG = 0.84f; Spec.SecondaryTintB = 1.00f;
            break;

        case ProceduralSpriteArchetype::OpticalHexBokeh:
            Spec.PrimaryTintR = 1.00f; Spec.PrimaryTintG = 0.92f; Spec.PrimaryTintB = 0.74f;
            Spec.SecondaryTintR = 0.96f; Spec.SecondaryTintG = 0.56f; Spec.SecondaryTintB = 0.24f;
            break;

        case ProceduralSpriteArchetype::OpticalStarburstFlare:
        case ProceduralSpriteArchetype::OpticalSoftGlow:
            Spec.PrimaryTintR = 1.00f; Spec.PrimaryTintG = 0.97f; Spec.PrimaryTintB = 0.90f;
            Spec.SecondaryTintR = 0.30f; Spec.SecondaryTintG = 0.84f; Spec.SecondaryTintB = 1.00f;
            break;

        case ProceduralSpriteArchetype::PyroFlameWisp:
        case ProceduralSpriteArchetype::PyroEmberSpark:
            Spec.PrimaryTintR = 1.00f; Spec.PrimaryTintG = 0.88f; Spec.PrimaryTintB = 0.44f;
            Spec.SecondaryTintR = 0.96f; Spec.SecondaryTintG = 0.24f; Spec.SecondaryTintB = 0.04f;
            break;

        case ProceduralSpriteArchetype::PyroSmokePuff:
            Spec.PrimaryTintR = 0.84f; Spec.PrimaryTintG = 0.86f; Spec.PrimaryTintB = 0.89f;
            Spec.SecondaryTintR = 0.34f; Spec.SecondaryTintG = 0.37f; Spec.SecondaryTintB = 0.42f;
            break;

        case ProceduralSpriteArchetype::LiquidRainDrop:
            Spec.PrimaryTintR = 0.84f; Spec.PrimaryTintG = 0.95f; Spec.PrimaryTintB = 1.00f;
            Spec.SecondaryTintR = 0.26f; Spec.SecondaryTintG = 0.64f; Spec.SecondaryTintB = 0.94f;
            break;

        case ProceduralSpriteArchetype::LiquidSoapBubble:
            Spec.PrimaryTintR = 0.66f; Spec.PrimaryTintG = 0.92f; Spec.PrimaryTintB = 1.00f;
            Spec.SecondaryTintR = 0.98f; Spec.SecondaryTintG = 0.62f; Spec.SecondaryTintB = 0.92f;
            break;

        case ProceduralSpriteArchetype::BotanicalMorphoButterfly:
            Spec.PrimaryTintR = 0.14f; Spec.PrimaryTintG = 0.82f; Spec.PrimaryTintB = 1.00f;
            Spec.SecondaryTintR = 0.28f; Spec.SecondaryTintG = 0.36f; Spec.SecondaryTintB = 0.98f;
            break;

        case ProceduralSpriteArchetype::BotanicalSakuraPetal:
            Spec.PrimaryTintR = 1.00f; Spec.PrimaryTintG = 0.80f; Spec.PrimaryTintB = 0.88f;
            Spec.SecondaryTintR = 0.94f; Spec.SecondaryTintG = 0.46f; Spec.SecondaryTintB = 0.66f;
            break;

        case ProceduralSpriteArchetype::TechHexEnergyShield:
            Spec.PrimaryTintR = 0.56f; Spec.PrimaryTintG = 0.95f; Spec.PrimaryTintB = 1.00f;
            Spec.SecondaryTintR = 0.08f; Spec.SecondaryTintG = 0.52f; Spec.SecondaryTintB = 0.94f;
            break;

        case ProceduralSpriteArchetype::TechVoronoiDissolveMask:
        case ProceduralSpriteArchetype::Count:
            Spec.PrimaryTintR = 0.96f; Spec.PrimaryTintG = 0.99f; Spec.PrimaryTintB = 1.00f;
            Spec.SecondaryTintR = 0.10f; Spec.SecondaryTintG = 0.78f; Spec.SecondaryTintB = 1.00f;
            break;
    }

    return Spec;
}

ProceduralSpriteSample SampleProceduralSpriteUnit(
    const ProceduralSpriteSpec& Spec,
    float                       UnitX,
    float                       UnitY,
    uint32_t                    TileSeed) noexcept
{
    switch (Spec.Archetype)
    {
        case ProceduralSpriteArchetype::SnowStellarDendrite:
            return SampleSnowCrystal(Spec, UnitX, UnitY, TileSeed, 6, false, false);
        case ProceduralSpriteArchetype::SnowFernlikeDendrite:
            return SampleSnowCrystal(Spec, UnitX, UnitY, TileSeed, 6, true, false);
        case ProceduralSpriteArchetype::SnowHexPlate:
            return SampleSnowCrystal(Spec, UnitX, UnitY, TileSeed, 6, false, true);
        case ProceduralSpriteArchetype::SnowTwelveFoldStar:
            return SampleSnowCrystal(Spec, UnitX, UnitY, TileSeed, 12, false, false);
        case ProceduralSpriteArchetype::BotanicalMorphoButterfly:
            return SampleButterflyWingPair(Spec, UnitX, UnitY, TileSeed);
        default:
            break;
    }

    ProceduralSpriteSample Out{};
    const float R     = std::hypot(UnitX, UnitY);
    const float Angle = std::atan2(UnitY, UnitX);

    switch (Spec.Archetype)
    {
        case ProceduralSpriteArchetype::OpticalHexBokeh:
        {
            const float HexD = RegularPolygonSdf(UnitX, UnitY, 6, 0.78f);
            const float Fill = 1.0f - SmoothStep(-0.04f, 0.04f, HexD);
            const float Rim  = SmoothStep(-0.18f, -0.02f, HexD) * Fill;
            const float SoftInner = 0.42f + 0.58f * Rim;
            Out.Alpha             = Fill * SoftInner;
            Out.Height            = Saturate(Out.Alpha);
            Out.Glint             = Rim;
            Out.SignedDistance01  = Saturate(0.5f - HexD * 1.4f);
            Out.DissolveThreshold = Saturate(1.0f - R);
            Out.Red               = LerpScalar(Spec.PrimaryTintR, Spec.SecondaryTintR, Rim);
            Out.Green             = LerpScalar(Spec.PrimaryTintG, Spec.SecondaryTintG, Rim);
            Out.Blue              = LerpScalar(Spec.PrimaryTintB, Spec.SecondaryTintB, Rim);
            return Out;
        }

        case ProceduralSpriteArchetype::OpticalStarburstFlare:
        {
            const float RaysH = std::exp(-std::fabs(UnitY) * 16.0f) * std::exp(-std::fabs(UnitX) * 2.2f);
            const float RaysV = std::exp(-std::fabs(UnitX) * 18.0f) * std::exp(-std::fabs(UnitY) * 3.0f);
            const float DiagA = std::exp(-std::fabs(UnitX - UnitY) * 14.0f) * std::exp(-R * 3.6f) * 0.55f;
            const float DiagB = std::exp(-std::fabs(UnitX + UnitY) * 14.0f) * std::exp(-R * 3.6f) * 0.55f;
            const float Heart = std::exp(-R * R * 12.0f);
            const float Glow  = Saturate(Heart + RaysH * 0.85f + RaysV * 0.65f + DiagA + DiagB);
            Out.Alpha             = Glow * SmoothStep(1.0f, 0.78f, R);
            Out.Height            = Out.Alpha;
            Out.Glint             = Saturate(Heart * 1.15f);
            Out.SignedDistance01  = Saturate(Out.Alpha);
            Out.DissolveThreshold = Saturate(1.0f - R);
            Out.Red               = LerpScalar(Spec.SecondaryTintR, Spec.PrimaryTintR, Saturate(Heart + 0.25f * RaysH));
            Out.Green             = LerpScalar(Spec.SecondaryTintG, Spec.PrimaryTintG, Saturate(Heart + 0.25f * RaysH));
            Out.Blue              = LerpScalar(Spec.SecondaryTintB, Spec.PrimaryTintB, Saturate(Heart + 0.25f * RaysH));
            return Out;
        }

        case ProceduralSpriteArchetype::OpticalSoftGlow:
        {
            const float Halo  = std::exp(-R * R * 4.5f) * SmoothStep(1.0f, 0.75f, R);
            const float Heart = std::exp(-R * R * 18.0f);
            Out.Alpha             = Halo;
            Out.Height            = Halo;
            Out.Glint             = Heart;
            Out.SignedDistance01  = Saturate(1.0f - R);
            Out.DissolveThreshold = Saturate(1.0f - R);
            Out.Red               = LerpScalar(Spec.SecondaryTintR, Spec.PrimaryTintR, Heart);
            Out.Green             = LerpScalar(Spec.SecondaryTintG, Spec.PrimaryTintG, Heart);
            Out.Blue              = LerpScalar(Spec.SecondaryTintB, Spec.PrimaryTintB, Heart);
            return Out;
        }

        case ProceduralSpriteArchetype::PyroFlameWisp:
        {
            const float Warp = (MultiOctaveNoise2D(UnitX * 3.2f + 1.5f, UnitY * 3.2f + 4.1f, TileSeed, 3) - 0.5f) * 0.45f * (0.4f + Spec.TurbulenceGain);
            const float Taper = LerpScalar(0.55f, 0.14f, Saturate((0.8f - UnitY) * 0.58f));
            const float Shape = std::hypot((UnitX + Warp) / std::max(0.12f, Taper), (UnitY - 0.12f) / 0.78f);
            const float Temp  = Saturate((1.0f - Shape) * (0.80f + 0.40f * MultiOctaveNoise2D(UnitX * 5.0f, UnitY * 5.0f, TileSeed + 19u, 3)));
            Out.Alpha             = SmoothStep(0.08f, 0.62f, Temp);
            Out.Height            = Temp;
            Out.Glint             = SmoothStep(0.55f, 0.92f, Temp);
            Out.SignedDistance01  = Saturate(1.0f - Shape);
            Out.DissolveThreshold = Temp;
            Out.Red               = Saturate(LerpScalar(Spec.SecondaryTintR, 1.0f, Temp));
            Out.Green             = Saturate(LerpScalar(Spec.SecondaryTintG * 0.5f, Spec.PrimaryTintG, Temp * Temp + Temp * 0.35f));
            Out.Blue              = Saturate(LerpScalar(Spec.SecondaryTintB, Spec.PrimaryTintB, Temp * Temp * Temp));
            return Out;
        }

        case ProceduralSpriteArchetype::PyroSmokePuff:
        {
            const float Billow = MultiOctaveNoise2D(UnitX * 2.8f + 2.3f, UnitY * 2.8f + 5.7f, TileSeed, 4);
            const float Density = Saturate((0.92f - R) * 1.25f - (1.0f - Billow) * (0.45f + 0.35f * Spec.TurbulenceGain));
            const float TopLit  = Saturate(0.55f - UnitY * 0.38f + Billow * 0.32f);
            Out.Alpha             = SmoothStep(0.05f, 0.55f, Density);
            Out.Height            = Density;
            Out.Glint             = 0.0f;
            Out.SignedDistance01  = Density;
            Out.DissolveThreshold = Density;
            Out.Red               = LerpScalar(Spec.SecondaryTintR, Spec.PrimaryTintR, TopLit);
            Out.Green             = LerpScalar(Spec.SecondaryTintG, Spec.PrimaryTintG, TopLit);
            Out.Blue              = LerpScalar(Spec.SecondaryTintB, Spec.PrimaryTintB, TopLit);
            return Out;
        }

        case ProceduralSpriteArchetype::PyroEmberSpark:
        {
            const float Streak = std::hypot(UnitX * 1.15f, UnitY * 4.4f);
            const float Glow   = std::exp(-Streak * Streak * 2.6f) * SmoothStep(1.0f, 0.72f, R);
            const float Heart  = std::exp(-Streak * Streak * 11.0f);
            Out.Alpha             = Glow;
            Out.Height            = Glow;
            Out.Glint             = Heart;
            Out.SignedDistance01  = Saturate(1.0f - Streak * 0.5f);
            Out.DissolveThreshold = Glow;
            Out.Red               = LerpScalar(Spec.SecondaryTintR, 1.0f, Saturate(Heart + 0.3f * Glow));
            Out.Green             = LerpScalar(Spec.SecondaryTintG, Spec.PrimaryTintG, Heart);
            Out.Blue              = LerpScalar(Spec.SecondaryTintB, Spec.PrimaryTintB, Heart * Heart);
            return Out;
        }

        case ProceduralSpriteArchetype::LiquidRainDrop:
        {
            const float Ys    = UnitY - 0.14f;
            const float Width = LerpScalar(0.10f, 0.58f, SmoothStep(-0.78f, 0.42f, Ys));
            const float DropD = std::hypot(UnitX / std::max(0.08f, Width), Ys / 0.72f) - 0.92f;
            const float Fill  = 1.0f - SmoothStep(-0.04f, 0.04f, DropD);
            const float SpecA = std::exp(-std::pow((UnitX + 0.14f) * 5.2f, 2.0f) - std::pow((UnitY - 0.08f) * 3.8f, 2.0f));
            const float RimB  = SmoothStep(-0.22f, -0.02f, DropD) * Fill * SmoothStep(-0.2f, 0.7f, UnitY);
            Out.Alpha             = Fill * (0.48f + 0.52f * Saturate(SpecA + RimB));
            Out.Height            = Saturate(Fill * std::sqrt(std::max(0.0f, 1.0f - std::clamp(DropD + 0.92f, 0.0f, 1.0f))));
            Out.Glint             = Saturate(SpecA + RimB * 0.7f);
            Out.SignedDistance01  = Saturate(0.5f - DropD);
            Out.DissolveThreshold = Out.Height;
            Out.Red               = Saturate(LerpScalar(Spec.SecondaryTintR, Spec.PrimaryTintR, Out.Glint));
            Out.Green             = Saturate(LerpScalar(Spec.SecondaryTintG, Spec.PrimaryTintG, Out.Glint));
            Out.Blue              = Saturate(LerpScalar(Spec.SecondaryTintB, Spec.PrimaryTintB, Out.Glint));
            return Out;
        }

        case ProceduralSpriteArchetype::LiquidSoapBubble:
        {
            const float Inside = 1.0f - SmoothStep(0.84f, 0.90f, R);
            const float Fresnel = std::pow(Saturate(R / 0.86f), 2.6f) * Inside;
            const float SpecArc = std::exp(-std::pow((UnitX + 0.34f) * 4.5f, 2.0f) - std::pow((UnitY + 0.34f) * 4.5f, 2.0f)) * Inside;
            const float Phase   = R * 9.0f + Angle * 2.0f;
            const float IridR   = 0.55f + 0.45f * std::sin(Phase);
            const float IridG   = 0.55f + 0.45f * std::sin(Phase + 2.094f);
            const float IridB   = 0.55f + 0.45f * std::sin(Phase + 4.188f);
            Out.Alpha             = Saturate(0.14f * Inside + Fresnel * 0.82f + SpecArc * 0.95f);
            Out.Height            = Inside * std::sqrt(std::max(0.0f, 1.0f - Saturate(R / 0.88f) * Saturate(R / 0.88f)));
            Out.Glint             = Saturate(SpecArc + Fresnel * 0.65f);
            Out.SignedDistance01  = Saturate(0.88f - R);
            Out.DissolveThreshold = Out.Height;
            Out.Red               = Saturate(LerpScalar(IridR, 1.0f, SpecArc));
            Out.Green             = Saturate(LerpScalar(IridG, 1.0f, SpecArc));
            Out.Blue              = Saturate(LerpScalar(IridB, 1.0f, SpecArc));
            return Out;
        }

        case ProceduralSpriteArchetype::BotanicalSakuraPetal:
        {
            const float Ys    = UnitY;
            const float Notch = std::exp(-std::pow(UnitX * 6.5f, 2.0f)) * SmoothStep(-0.1f, -0.75f, Ys) * 0.24f;
            const float PetD  = std::hypot(UnitX / 0.58f, (Ys - 0.04f) / 0.76f) - (0.94f - Notch);
            const float Fill  = 1.0f - SmoothStep(-0.03f, 0.03f, PetD);
            const float Vein  = std::exp(-std::fabs(UnitX) * 14.0f) * 0.28f;
            const float Grad  = SmoothStep(0.70f, -0.65f, Ys);
            Out.Alpha             = Fill;
            Out.Height            = Saturate(Fill * (0.45f + 0.35f * (1.0f - Saturate(PetD + 0.94f)) + Vein));
            Out.Glint             = Saturate(Fill * Vein);
            Out.SignedDistance01  = Saturate(0.5f - PetD);
            Out.DissolveThreshold = Saturate(1.0f - R);
            Out.Red               = LerpScalar(Spec.SecondaryTintR, Spec.PrimaryTintR, Grad);
            Out.Green             = LerpScalar(Spec.SecondaryTintG, Spec.PrimaryTintG, Grad);
            Out.Blue              = LerpScalar(Spec.SecondaryTintB, Spec.PrimaryTintB, Grad);
            return Out;
        }

        case ProceduralSpriteArchetype::TechHexEnergyShield:
        {
            const float HexD  = RegularPolygonSdf(UnitX, UnitY, 6, 0.84f);
            const float Mask  = 1.0f - SmoothStep(-0.025f, 0.025f, HexD);
            const float Edge  = SmoothStep(-0.14f, -0.015f, HexD) * Mask;
            const float Inner = 1.0f - SmoothStep(0.015f, 0.05f, std::fabs(RegularPolygonSdf(UnitX, UnitY, 6, 0.54f)));
            const float Scan  = 0.5f + 0.5f * std::sin(UnitY * 34.0f);
            const float Glow  = Saturate(Edge * 0.95f + Inner * 0.62f + Mask * (0.18f + 0.16f * Scan));
            Out.Alpha             = Mask * Saturate(0.32f + Glow * 0.72f);
            Out.Height            = Saturate(Edge * 0.85f + Inner * 0.55f + 0.20f * Mask);
            Out.Glint             = Saturate(Edge + Inner * 0.65f);
            Out.SignedDistance01  = Saturate(0.5f - HexD * 1.4f);
            Out.DissolveThreshold = Saturate(1.0f - R * 0.92f);
            Out.Red               = LerpScalar(Spec.SecondaryTintR, Spec.PrimaryTintR, Glow);
            Out.Green             = LerpScalar(Spec.SecondaryTintG, Spec.PrimaryTintG, Glow);
            Out.Blue              = LerpScalar(Spec.SecondaryTintB, Spec.PrimaryTintB, Glow);
            return Out;
        }

        case ProceduralSpriteArchetype::TechVoronoiDissolveMask:
        case ProceduralSpriteArchetype::Count:
        {
            const float Scale = 3.0f + 5.0f * Spec.BranchDensity;
            const VoronoiDistancePair V = SampleVoronoiPeriodic2D(UnitX, UnitY, Scale, TileSeed);
            const float EdgeLine = 1.0f - SmoothStep(0.02f, 0.14f, V.SecondDist - V.NearestDist);
            const float CellVal  = Saturate(0.18f + 0.82f * V.CellHash);
            const float Dissolve = Saturate(0.65f * CellVal + 0.35f * (1.0f - Saturate(V.NearestDist * 1.15f)));
            Out.Alpha             = 1.0f;
            Out.Height            = Saturate(Dissolve * (1.0f - 0.45f * EdgeLine));
            Out.Glint             = EdgeLine;
            Out.SignedDistance01  = Saturate(V.SecondDist - V.NearestDist);
            Out.DissolveThreshold = Dissolve;
            Out.Red               = LerpScalar(Spec.SecondaryTintR * CellVal, Spec.PrimaryTintR, EdgeLine);
            Out.Green             = LerpScalar(Spec.SecondaryTintG * CellVal, Spec.PrimaryTintG, EdgeLine);
            Out.Blue              = LerpScalar(Spec.SecondaryTintB * CellVal, Spec.PrimaryTintB, EdgeLine);
            return Out;
        }

        default:
            return Out;
    }
}

ProceduralSpriteChannelSet SynthesizeProceduralSpriteChannels(const ProceduralSpriteSpec& Spec)
{
    const uint32_t TileRes  = std::clamp<uint32_t>(Spec.TileResolution, 16u, 512u);
    const uint32_t GridCols = std::clamp<uint32_t>(Spec.AtlasColumns, 1u, 8u);
    const uint32_t FullW    = TileRes * GridCols;
    const uint32_t FullH    = TileRes * GridCols;

    ProceduralSpriteChannelSet Out{};
    Out.Width          = FullW;
    Out.Height         = FullH;
    Out.TileResolution = TileRes;
    Out.AtlasColumns   = GridCols;

    const std::size_t PixelCount = static_cast<std::size_t>(FullW) * FullH;
    Out.Rgba8.resize(PixelCount * 4u, 0u);
    Out.Height8.resize(PixelCount, 0u);
    Out.NormalRgba8.resize(PixelCount * 4u, 0u);
    Out.MaskRgba8.resize(PixelCount * 4u, 0u);

    std::vector<float> FloatHeights(PixelCount, 0.0f);

    for (uint32_t TileY = 0u; TileY < GridCols; ++TileY)
    {
        for (uint32_t TileX = 0u; TileX < GridCols; ++TileX)
        {
            const uint32_t TileSeed = Spec.Seed + (TileY * GridCols + TileX) * 7919u;
            for (uint32_t Py = 0u; Py < TileRes; ++Py)
            {
                const float Uy = ((static_cast<float>(Py) + 0.5f) / static_cast<float>(TileRes)) * 2.0f - 1.0f;
                for (uint32_t Px = 0u; Px < TileRes; ++Px)
                {
                    const float Ux = ((static_cast<float>(Px) + 0.5f) / static_cast<float>(TileRes)) * 2.0f - 1.0f;
                    const ProceduralSpriteSample Sample = SampleProceduralSpriteUnit(Spec, Ux, Uy, TileSeed);

                    const uint32_t FullX = TileX * TileRes + Px;
                    const uint32_t FullY = TileY * TileRes + Py;
                    const std::size_t Idx = static_cast<std::size_t>(FullY) * FullW + FullX;
                    const std::size_t Off = Idx * 4u;

                    Out.Rgba8[Off + 0u] = FloatToByte(Sample.Red);
                    Out.Rgba8[Off + 1u] = FloatToByte(Sample.Green);
                    Out.Rgba8[Off + 2u] = FloatToByte(Sample.Blue);
                    Out.Rgba8[Off + 3u] = FloatToByte(Sample.Alpha);

                    FloatHeights[Idx] = Sample.Height;
                    Out.Height8[Idx]  = FloatToByte(Sample.Height);

                    Out.MaskRgba8[Off + 0u] = FloatToByte(Sample.Glint);
                    Out.MaskRgba8[Off + 1u] = FloatToByte(Sample.SignedDistance01);
                    Out.MaskRgba8[Off + 2u] = FloatToByte(Sample.DissolveThreshold);
                    Out.MaskRgba8[Off + 3u] = 255u;
                }
            }
        }
    }

    // Derive tangent-space normals per tile via Sobel 3x3 gradient on Height
    const float Relief = std::clamp(Spec.NormalRelief, 0.25f, 12.0f);
    for (uint32_t TileY = 0u; TileY < GridCols; ++TileY)
    {
        const int32_t MinY = static_cast<int32_t>(TileY * TileRes);
        const int32_t MaxY = MinY + static_cast<int32_t>(TileRes) - 1;
        for (uint32_t TileX = 0u; TileX < GridCols; ++TileX)
        {
            const int32_t MinX = static_cast<int32_t>(TileX * TileRes);
            const int32_t MaxX = MinX + static_cast<int32_t>(TileRes) - 1;

            auto SampleH = [&](int32_t X, int32_t Y) noexcept -> float
            {
                const int32_t Cx = std::clamp(X, MinX, MaxX);
                const int32_t Cy = std::clamp(Y, MinY, MaxY);
                return FloatHeights[static_cast<std::size_t>(Cy) * FullW + static_cast<std::size_t>(Cx)];
            };

            for (int32_t Y = MinY; Y <= MaxY; ++Y)
            {
                for (int32_t X = MinX; X <= MaxX; ++X)
                {
                    const float Htl = SampleH(X - 1, Y - 1);
                    const float Htc = SampleH(X,     Y - 1);
                    const float Htr = SampleH(X + 1, Y - 1);
                    const float Hml = SampleH(X - 1, Y);
                    const float Hmr = SampleH(X + 1, Y);
                    const float Hbl = SampleH(X - 1, Y + 1);
                    const float Hbc = SampleH(X,     Y + 1);
                    const float Hbr = SampleH(X + 1, Y + 1);

                    const float Gx = (Htr + 2.0f * Hmr + Hbr) - (Htl + 2.0f * Hml + Hbl);
                    const float Gy = (Hbl + 2.0f * Hbc + Hbr) - (Htl + 2.0f * Htc + Htr);

                    float Nx = -Gx * Relief;
                    float Ny = -Gy * Relief;
                    float Nz = 1.0f;
                    const float InvLen = 1.0f / std::sqrt(Nx * Nx + Ny * Ny + Nz * Nz);
                    Nx *= InvLen;
                    Ny *= InvLen;
                    Nz *= InvLen;

                    const std::size_t Off = (static_cast<std::size_t>(Y) * FullW + static_cast<std::size_t>(X)) * 4u;
                    Out.NormalRgba8[Off + 0u] = FloatToByte(Nx * 0.5f + 0.5f);
                    Out.NormalRgba8[Off + 1u] = FloatToByte(Ny * 0.5f + 0.5f);
                    Out.NormalRgba8[Off + 2u] = FloatToByte(Nz * 0.5f + 0.5f);
                    Out.NormalRgba8[Off + 3u] = 255u;
                }
            }
        }
    }

    return Out;
}

} // namespace Frontier
