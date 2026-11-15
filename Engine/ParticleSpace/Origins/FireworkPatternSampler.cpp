/*============================================================================================================================================
                                                          FIREWORKPATTERNSAMPLER.CPP
============================================================================================================================================*/
// 🧩 Implementation of all 10 pyrotechnic burst shapes & crisp 5x7 ASCII sky-writing bitmap sampler

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/ParticleSpace/Origins/FireworkPatternSampler.h"

#include "Engine/ParticleSpace/Origins/AnalyticOrigins.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>

namespace Frontier::ParticleSpace
{

namespace
{

constexpr float Pi    = 3.14159265358979323846f;
constexpr float TwoPi = 6.28318530717958647692f;

constexpr std::array<const char*, static_cast<std::size_t>(FireworkShapeCategory::Count)> ShapeLabels = {{
    "Grand Bouquet (Multi-Shell Pyro)",
    "Sky Text (Custom Message)",
    "Glowing Heart",
    "5-Point Star",
    "Hanabi Smiley Face",
    "Saturn Ringed Planet",
    "Royal Crown",
    "Iridescent Butterfly",
    "Spiral Galaxy",
    "New Year's Grand Finale (Cycle All)",
}};

struct Glyph5x7
{
    char    Symbol;
    uint8_t Rows[7]; // 5 LSB bits per row (MSB=left column bit 4, LSB=right column bit 0)
};

constexpr Glyph5x7 GlyphTable[] = {
    { 'A', { 0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11 } },
    { 'B', { 0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E } },
    { 'C', { 0x0F, 0x10, 0x10, 0x10, 0x10, 0x10, 0x0F } },
    { 'D', { 0x1E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1E } },
    { 'E', { 0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F } },
    { 'F', { 0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10 } },
    { 'G', { 0x0F, 0x10, 0x10, 0x17, 0x11, 0x11, 0x0E } },
    { 'H', { 0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11 } },
    { 'I', { 0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x1F } },
    { 'J', { 0x07, 0x02, 0x02, 0x02, 0x12, 0x12, 0x0C } },
    { 'K', { 0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11 } },
    { 'L', { 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F } },
    { 'M', { 0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11 } },
    { 'N', { 0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x11 } },
    { 'O', { 0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E } },
    { 'P', { 0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10 } },
    { 'Q', { 0x0E, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0D } },
    { 'R', { 0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11 } },
    { 'S', { 0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E } },
    { 'T', { 0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04 } },
    { 'U', { 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E } },
    { 'V', { 0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04 } },
    { 'W', { 0x11, 0x11, 0x11, 0x15, 0x15, 0x1B, 0x11 } },
    { 'X', { 0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11 } },
    { 'Y', { 0x11, 0x11, 0x0A, 0x04, 0x04, 0x04, 0x04 } },
    { 'Z', { 0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F } },
    { '0', { 0x0E, 0x13, 0x15, 0x15, 0x19, 0x11, 0x0E } },
    { '1', { 0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E } },
    { '2', { 0x0E, 0x11, 0x01, 0x06, 0x08, 0x10, 0x1F } },
    { '3', { 0x1E, 0x01, 0x01, 0x0E, 0x01, 0x01, 0x1E } },
    { '4', { 0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02 } },
    { '5', { 0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E } },
    { '6', { 0x0E, 0x10, 0x1E, 0x11, 0x11, 0x11, 0x0E } },
    { '7', { 0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08 } },
    { '8', { 0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E } },
    { '9', { 0x0E, 0x11, 0x11, 0x0F, 0x01, 0x01, 0x0E } },
    { '!', { 0x04, 0x04, 0x04, 0x04, 0x04, 0x00, 0x04 } },
    { '.', { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04 } },
    { '-', { 0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00 } },
    { '+', { 0x00, 0x04, 0x04, 0x1F, 0x04, 0x04, 0x00 } },
    { '*', { 0x00, 0x15, 0x0E, 0x1F, 0x0E, 0x15, 0x00 } },
};

const Glyph5x7* FindGlyph(char Ch) noexcept
{
    const char Up = static_cast<char>(std::toupper(static_cast<unsigned char>(Ch)));
    for (const auto& G : GlyphTable)
    {
        if (G.Symbol == Up) return &G;
    }
    return nullptr;
}

void EmitNormalized(ParticleAnchorSample& S, float X, float Y, float Z, float RadiusScale, uint32_t Group) noexcept
{
    S.PositionMeters[0] = X * RadiusScale;
    S.PositionMeters[1] = Y * RadiusScale;
    S.PositionMeters[2] = Z * RadiusScale;
    const float L = std::sqrt(X * X + Y * Y + Z * Z);
    if (L > 1e-5f)
    {
        S.NormalUnit[0] = X / L;
        S.NormalUnit[1] = Y / L;
        S.NormalUnit[2] = Z / L;
    }
    else
    {
        S.NormalUnit[0] = 0.0f;
        S.NormalUnit[1] = 1.0f;
        S.NormalUnit[2] = 0.0f;
    }
    S.CurvatureWeight = std::clamp(L, 0.0f, 1.0f);
    S.GroupIndex      = Group;
}

} // namespace

std::string_view QueryFireworkShapeLabel(FireworkShapeCategory Shape) noexcept
{
    const auto Idx = static_cast<std::size_t>(Shape);
    if (Idx >= ShapeLabels.size()) return ShapeLabels[0];
    return ShapeLabels[Idx];
}

std::vector<ParticleAnchorSample> SampleFireworkPatternAnchors(
    const FireworkOrigin& Origin,
    uint32_t              SampleCount,
    uint32_t              FinaleCycleIndex,
    uint32_t              Seed)
{
    FireworkShapeCategory ActiveShape = Origin.Shape;
    if (ActiveShape == FireworkShapeCategory::GrandFinale)
    {
        constexpr std::array<FireworkShapeCategory, 8> FinaleSeq = {{
            FireworkShapeCategory::SkyText,
            FireworkShapeCategory::Heart,
            FireworkShapeCategory::Star,
            FireworkShapeCategory::Saturn,
            FireworkShapeCategory::Butterfly,
            FireworkShapeCategory::Crown,
            FireworkShapeCategory::Spiral,
            FireworkShapeCategory::Smiley,
        }};
        ActiveShape = FinaleSeq[FinaleCycleIndex % FinaleSeq.size()];
    }

    std::vector<ParticleAnchorSample> Out;
    Out.reserve(SampleCount);
    DeterministicPcgRng Rng(Seed ^ (FinaleCycleIndex * 0x9E3779B9u));

    // Pre-collect sky-text lit pixels if ActiveShape == SkyText
    struct PixelCell { float X; float Y; uint32_t CharIdx; };
    std::vector<PixelCell> TextPixels;
    if (ActiveShape == FireworkShapeCategory::SkyText)
    {
        const std::string_view Msg = Origin.SkyTextMessage.empty() ? std::string_view("2026") : std::string_view(Origin.SkyTextMessage);
        const float TotalCols = static_cast<float>(Msg.size() * 6u - 1u);
        const float HalfW     = std::max(3.0f, TotalCols * 0.5f);
        for (std::size_t Ci = 0u; Ci < Msg.size(); ++Ci)
        {
            const Glyph5x7* G = FindGlyph(Msg[Ci]);
            if (!G) continue;
            for (int Row = 0; Row < 7; ++Row)
            {
                for (int Col = 0; Col < 5; ++Col)
                {
                    if ((G->Rows[Row] & (1u << (4 - Col))) != 0u)
                    {
                        const float Px = (static_cast<float>(Ci * 6u + static_cast<std::size_t>(Col)) - HalfW) / HalfW;
                        const float Py = (3.0f - static_cast<float>(Row)) / 4.2f;
                        TextPixels.push_back({ Px, Py, static_cast<uint32_t>(Ci) });
                    }
                }
            }
        }
    }

    const float R = Origin.PatternRadius;
    for (uint32_t I = 0u; I < SampleCount; ++I)
    {
        ParticleAnchorSample S {};
        const float T = static_cast<float>(I) / static_cast<float>(std::max(1u, SampleCount));
        const float U = Rng.NextUnitFloat();
        const float V = Rng.NextUnitFloat();
        float X = 0.0f, Y = 0.0f, Z = 0.0f;
        uint32_t Grp = 0u;

        switch (ActiveShape)
        {
            case FireworkShapeCategory::Bouquet:
            {
                // Multi-shell spherical peony + golden pistil core
                const float CosTh = 1.0f - 2.0f * U;
                const float SinTh = std::sqrt(std::max(0.0f, 1.0f - CosTh * CosTh));
                const float Phi   = V * TwoPi;
                const bool Core   = (I % 4u == 0u);
                const float Rad   = Core ? (0.42f + 0.06f * Rng.NextSignedFloat()) : (0.94f + 0.08f * Rng.NextSignedFloat());
                X = SinTh * std::cos(Phi) * Rad;
                Y = CosTh * Rad;
                Z = SinTh * std::sin(Phi) * Rad;
                Grp = Core ? 1u : 0u;
                break;
            }
            case FireworkShapeCategory::SkyText:
            {
                if (!TextPixels.empty())
                {
                    const auto& Cell = TextPixels[I % TextPixels.size()];
                    X = Cell.X * 1.15f + Rng.NextSignedFloat() * 0.025f;
                    Y = Cell.Y * 0.65f + Rng.NextSignedFloat() * 0.025f;
                    Z = Rng.NextSignedFloat() * 0.06f;
                    Grp = Cell.CharIdx;
                }
                break;
            }
            case FireworkShapeCategory::Heart:
            {
                // Parametric classic heart curve
                const float A = T * TwoPi;
                const float Sx = 16.0f * std::pow(std::sin(A), 3.0f);
                const float Sy = 13.0f * std::cos(A) - 5.0f * std::cos(2.0f * A) - 2.0f * std::cos(3.0f * A) - std::cos(4.0f * A);
                const float Ring = (I % 5u == 0u) ? 0.68f : 1.0f;
                X = (Sx / 16.5f) * Ring + Rng.NextSignedFloat() * 0.025f;
                Y = (Sy / 16.5f) * Ring + Rng.NextSignedFloat() * 0.025f;
                Z = Rng.NextSignedFloat() * 0.08f;
                Grp = (I % 5u == 0u) ? 1u : 0u;
                break;
            }
            case FireworkShapeCategory::Star:
            {
                // Crisp 5-point star perimeter with inner rays
                const int Seg = static_cast<int>(I % 10u);
                const float A0 = static_cast<float>(Seg) * (TwoPi / 10.0f) + Pi * 0.5f;
                const float A1 = A0 + (TwoPi / 10.0f);
                const float R0 = (Seg % 2 == 0) ? 1.0f : 0.42f;
                const float R1 = (Seg % 2 == 0) ? 0.42f : 1.0f;
                X = R0 * std::cos(A0) * (1.0f - U) + R1 * std::cos(A1) * U;
                Y = R0 * std::sin(A0) * (1.0f - U) + R1 * std::sin(A1) * U;
                Z = Rng.NextSignedFloat() * 0.06f;
                Grp = static_cast<uint32_t>(Seg % 2);
                break;
            }
            case FireworkShapeCategory::Smiley:
            {
                if (U < 0.54f)
                {
                    // Outer face circle
                    const float A = V * TwoPi;
                    X = std::cos(A) * 0.92f;
                    Y = std::sin(A) * 0.92f;
                    Grp = 0u;
                }
                else if (U < 0.72f)
                {
                    // Left & right eyes
                    const float EyeX = (U < 0.63f) ? -0.34f : 0.34f;
                    const float A    = V * TwoPi;
                    const float Er   = 0.11f * std::sqrt(Rng.NextUnitFloat());
                    X = EyeX + std::cos(A) * Er;
                    Y = 0.28f + std::sin(A) * Er;
                    Grp = 1u;
                }
                else
                {
                    // Happy smile arc
                    const float A = Pi * 1.16f + V * Pi * 0.68f;
                    X = std::cos(A) * 0.54f;
                    Y = -0.04f + std::sin(A) * 0.48f;
                    Grp = 2u;
                }
                Z = Rng.NextSignedFloat() * 0.05f;
                break;
            }
            case FireworkShapeCategory::Saturn:
            {
                if (U < 0.45f)
                {
                    // Planet sphere core
                    const float Cz = 1.0f - 2.0f * V;
                    const float Sr = std::sqrt(std::max(0.0f, 1.0f - Cz * Cz));
                    const float Ph = Rng.NextUnitFloat() * TwoPi;
                    X = Sr * std::cos(Ph) * 0.46f;
                    Y = Cz * 0.46f;
                    Z = Sr * std::sin(Ph) * 0.46f;
                    Grp = 0u;
                }
                else
                {
                    // Tilted planetary ring disc
                    const float A   = V * TwoPi;
                    const float Rad = 0.68f + Rng.NextUnitFloat() * 0.42f;
                    const float Rx  = std::cos(A) * Rad;
                    const float Rz  = std::sin(A) * Rad;
                    // Tilt 26 degrees around Z & X
                    X = Rx * 0.90f;
                    Y = Rx * 0.42f + Rz * 0.18f;
                    Z = Rz * 0.88f;
                    Grp = 1u;
                }
                break;
            }
            case FireworkShapeCategory::Crown:
            {
                if (U < 0.30f)
                {
                    // Lower circlet band
                    X = (V * 2.0f - 1.0f) * 0.86f;
                    Y = -0.42f + Rng.NextSignedFloat() * 0.04f;
                    Grp = 0u;
                }
                else
                {
                    // 5 royal peaks
                    const float Px = (V * 2.0f - 1.0f) * 0.86f;
                    const float Peak = std::fabs(std::sin((V * 3.0f) * Pi));
                    X = Px;
                    Y = -0.36f + Peak * 0.88f;
                    Grp = 1u;
                }
                Z = Rng.NextSignedFloat() * 0.06f;
                break;
            }
            case FireworkShapeCategory::Butterfly:
            {
                // Fay's iconic butterfly curve
                const float A = T * TwoPi * 2.0f;
                const float E = std::exp(std::cos(A)) - 2.0f * std::cos(4.0f * A) - std::pow(std::sin(A / 12.0f), 5.0f);
                X = std::sin(A) * E * 0.28f;
                Y = std::cos(A) * E * 0.28f;
                Z = std::fabs(X) * 0.18f + Rng.NextSignedFloat() * 0.04f;
                Grp = (X >= 0.0f) ? 1u : 0u;
                break;
            }
            case FireworkShapeCategory::Spiral:
            default:
            {
                // 3-arm logarithmic spiral galaxy
                const uint32_t Arm = I % 3u;
                const float Rad    = 0.12f + 0.92f * std::sqrt(U);
                const float Ang    = Rad * 4.8f + static_cast<float>(Arm) * (TwoPi / 3.0f) + Rng.NextSignedFloat() * 0.14f;
                X = std::cos(Ang) * Rad;
                Y = std::sin(Ang) * Rad;
                Z = Rng.NextSignedFloat() * 0.07f;
                Grp = Arm;
                break;
            }
        }

        EmitNormalized(S, X, Y, Z, R, Grp);
        S.PositionMeters[0] += Origin.ShellOrigin[0];
        S.PositionMeters[1] += Origin.ShellOrigin[1];
        S.PositionMeters[2] += Origin.ShellOrigin[2];
        Out.push_back(S);
    }

    return Out;
}

} // namespace Frontier::ParticleSpace
