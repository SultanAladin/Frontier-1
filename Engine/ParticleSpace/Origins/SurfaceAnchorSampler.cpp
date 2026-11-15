/*============================================================================================================================================
                                                          SURFACEANCHORSAMPLER.CPP
============================================================================================================================================*/
// 🧩 Implementation of all 17 procedural surface anchor samplers (0..16) & area-weighted triangle network sampler

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/ParticleSpace/Origins/SurfaceAnchorSampler.h"

#include "Engine/ParticleSpace/Origins/AnalyticOrigins.h"
#include "Engine/ParticleSpace/Origins/ControlVehicleSurfaceLoader.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace Frontier::ParticleSpace
{

namespace
{

constexpr float Pi    = 3.14159265358979323846f;
constexpr float TwoPi = 6.28318530717958647692f;
constexpr float DegToRad = Pi / 180.0f;

constexpr std::array<const char*, static_cast<std::size_t>(SurfacePresetCategory::Count)> SurfaceLabels = {{
    "XZ Floor Matrix",
    "XY Wall Matrix",
    "3D Obsidian Sphere",
    "3D Obsidian Hand + Chrome Ball",
    "3D Torus Ring",
    "3D Cubic Volume",
    "Utah Teapot",
    "Sports Car Silhouette",
    "Sculpted Head / Bust",
    "Torus Knot (3,2)",
    "Precision Gear / Cog",
    "Quartz Crystal Cluster",
    "Architectural Archway",
    "Bevelled Monolith Cube",
    "Faceted Spire / Cone",
    "Stellated Star Polyhedron",
    "Custom Imported PolygonNetwork",
    "Controlled Integrator Vehicle",
}};

void NormalizeVec3(float V[3]) noexcept
{
    const float L = std::sqrt(V[0] * V[0] + V[1] * V[1] + V[2] * V[2]);
    if (L > 1e-6f)
    {
        const float Inv = 1.0f / L;
        V[0] *= Inv;
        V[1] *= Inv;
        V[2] *= Inv;
    }
    else
    {
        V[0] = 0.0f;
        V[1] = 1.0f;
        V[2] = 0.0f;
    }
}

void ApplyEulerDegrees(float P[3], const float EulerDeg[3]) noexcept
{
    const float Rx = EulerDeg[0] * DegToRad;
    const float Ry = EulerDeg[1] * DegToRad;
    const float Rz = EulerDeg[2] * DegToRad;

    if (std::fabs(Rx) > 1e-5f)
    {
        const float C = std::cos(Rx), S = std::sin(Rx);
        const float Y = P[1] * C - P[2] * S;
        const float Z = P[1] * S + P[2] * C;
        P[1] = Y; P[2] = Z;
    }
    if (std::fabs(Ry) > 1e-5f)
    {
        const float C = std::cos(Ry), S = std::sin(Ry);
        const float X =  P[0] * C + P[2] * S;
        const float Z = -P[0] * S + P[2] * C;
        P[0] = X; P[2] = Z;
    }
    if (std::fabs(Rz) > 1e-5f)
    {
        const float C = std::cos(Rz), S = std::sin(Rz);
        const float X = P[0] * C - P[1] * S;
        const float Y = P[0] * S + P[1] * C;
        P[0] = X; P[1] = Y;
    }
}

/// Evaluates one deterministic local-space unit sample (roughly inside [-1, 1]^3) for presets 0..15.
ParticleAnchorSample EvaluateUnitPresetSample(
    SurfacePresetCategory Preset,
    uint32_t              Index,
    uint32_t              Count,
    DeterministicPcgRng&  Rng) noexcept
{
    ParticleAnchorSample S {};
    const float U = Rng.NextUnitFloat();
    const float V = Rng.NextUnitFloat();
    const float W = Rng.NextUnitFloat();

    // Fibonacci sphere helper
    auto SampleUnitSphere = [&](float OutP[3], float OutN[3]) {
        const float Z = 1.0f - 2.0f * (static_cast<float>(Index) + 0.5f) / static_cast<float>(std::max(1u, Count));
        const float R = std::sqrt(std::max(0.0f, 1.0f - Z * Z));
        const float Phi = static_cast<float>(Index) * 2.39996323f;
        OutP[0] = R * std::cos(Phi);
        OutP[1] = Z;
        OutP[2] = R * std::sin(Phi);
        OutN[0] = OutP[0]; OutN[1] = OutP[1]; OutN[2] = OutP[2];
    };

    switch (Preset)
    {
        case SurfacePresetCategory::FloorMatrixXZ:
        {
            const uint32_t Side = std::max(1u, static_cast<uint32_t>(std::ceil(std::sqrt(static_cast<double>(Count)))));
            const uint32_t C    = Index % Side;
            const uint32_t R    = Index / Side;
            S.PositionMeters[0] = (Side > 1u ? static_cast<float>(C) / static_cast<float>(Side - 1u) * 2.0f - 1.0f : 0.0f);
            S.PositionMeters[1] = 0.0f;
            S.PositionMeters[2] = (Side > 1u ? static_cast<float>(R) / static_cast<float>(Side - 1u) * 2.0f - 1.0f : 0.0f);
            S.NormalUnit[1]     = 1.0f;
            S.CurvatureWeight   = 0.25f;
            break;
        }
        case SurfacePresetCategory::WallMatrixXY:
        {
            const uint32_t Side = std::max(1u, static_cast<uint32_t>(std::ceil(std::sqrt(static_cast<double>(Count)))));
            const uint32_t C    = Index % Side;
            const uint32_t R    = Index / Side;
            S.PositionMeters[0] = (Side > 1u ? static_cast<float>(C) / static_cast<float>(Side - 1u) * 2.0f - 1.0f : 0.0f);
            S.PositionMeters[1] = (Side > 1u ? static_cast<float>(R) / static_cast<float>(Side - 1u) * 2.0f - 1.0f : 0.0f);
            S.PositionMeters[2] = 0.0f;
            S.NormalUnit[2]     = 1.0f;
            S.CurvatureWeight   = 0.25f;
            break;
        }
        case SurfacePresetCategory::ObsidianSphere:
        case SurfacePresetCategory::CustomPolygonNetwork:
        {
            SampleUnitSphere(S.PositionMeters, S.NormalUnit);
            S.CurvatureWeight = 0.50f;
            break;
        }
        case SurfacePresetCategory::ObsidianHandOrb:
        {
            // Sculpted palm + 5 arched fingers cradling an elevated chrome sphere orb
            if (U < 0.32f)
            {
                // Levitating orb above palm
                const float Z = V * 2.0f - 1.0f;
                const float R = std::sqrt(std::max(0.0f, 1.0f - Z * Z));
                const float A = W * TwoPi;
                S.PositionMeters[0] = R * std::cos(A) * 0.38f;
                S.PositionMeters[1] = 0.36f + Z * 0.38f;
                S.PositionMeters[2] = R * std::sin(A) * 0.38f;
                S.NormalUnit[0]     = R * std::cos(A);
                S.NormalUnit[1]     = Z;
                S.NormalUnit[2]     = R * std::sin(A);
                S.GroupIndex        = 1u;
                S.CurvatureWeight   = 0.85f;
            }
            else if (U < 0.62f)
            {
                // Cupped palm ellipsoid
                const float Z = V * 2.0f - 1.0f;
                const float R = std::sqrt(std::max(0.0f, 1.0f - Z * Z));
                const float A = W * TwoPi;
                S.PositionMeters[0] = R * std::cos(A) * 0.55f;
                S.PositionMeters[1] = -0.32f + Z * 0.16f - 0.10f * (1.0f - R);
                S.PositionMeters[2] = R * std::sin(A) * 0.52f;
                S.NormalUnit[0]     = R * std::cos(A);
                S.NormalUnit[1]     = Z;
                S.NormalUnit[2]     = R * std::sin(A);
                S.CurvatureWeight   = 0.55f;
            }
            else
            {
                // 5 curved fingers cradling the orb
                const int Finger = static_cast<int>(Rng.NextUint32() % 5u);
                const float Fan  = (static_cast<float>(Finger) - 2.0f) * 0.42f;
                const float T    = V;
                const float Ang  = W * TwoPi;
                const float Rad  = 0.075f * (1.0f - 0.30f * T);
                const float ArcX = std::sin(Fan) * (0.48f + 0.36f * std::sin(T * 1.45f));
                const float ArcY = -0.26f + 0.78f * T - 0.14f * T * T;
                const float ArcZ = std::cos(Fan) * (0.44f + 0.25f * std::sin(T * 1.3f)) - 0.16f * T * T;
                S.PositionMeters[0] = ArcX + std::cos(Ang) * Rad;
                S.PositionMeters[1] = ArcY + std::sin(Ang) * Rad * 0.5f;
                S.PositionMeters[2] = ArcZ + std::sin(Ang) * Rad;
                S.NormalUnit[0]     = std::cos(Ang);
                S.NormalUnit[1]     = 0.3f;
                S.NormalUnit[2]     = std::sin(Ang);
                S.CurvatureWeight   = 0.75f;
                S.GroupIndex        = static_cast<uint32_t>(2 + Finger);
            }
            break;
        }
        case SurfacePresetCategory::TorusRing:
        {
            const float Major = 0.72f, Minor = 0.28f;
            const float Phi   = U * TwoPi;
            const float Theta = V * TwoPi;
            const float R     = Major + Minor * std::cos(Theta);
            S.PositionMeters[0] = R * std::cos(Phi);
            S.PositionMeters[1] = Minor * std::sin(Theta);
            S.PositionMeters[2] = R * std::sin(Phi);
            S.NormalUnit[0]     = std::cos(Theta) * std::cos(Phi);
            S.NormalUnit[1]     = std::sin(Theta);
            S.NormalUnit[2]     = std::cos(Theta) * std::sin(Phi);
            S.CurvatureWeight   = 0.5f + 0.5f * std::cos(Theta);
            break;
        }
        case SurfacePresetCategory::CubicVolume:
        case SurfacePresetCategory::BevelledMonolith:
        {
            const int Face = static_cast<int>(Rng.NextUint32() % 6u);
            const float A  = U * 2.0f - 1.0f;
            const float B  = V * 2.0f - 1.0f;
            const float Sign = (Face % 2 == 0) ? 1.0f : -1.0f;
            const float ScaleY = (Preset == SurfacePresetCategory::BevelledMonolith) ? 1.25f : 0.85f;
            const float ScaleXZ = (Preset == SurfacePresetCategory::BevelledMonolith) ? 0.65f : 0.85f;
            if (Face < 2)      { S.PositionMeters[0] = Sign * ScaleXZ; S.PositionMeters[1] = A * ScaleY; S.PositionMeters[2] = B * ScaleXZ; S.NormalUnit[0] = Sign; }
            else if (Face < 4) { S.PositionMeters[0] = A * ScaleXZ; S.PositionMeters[1] = Sign * ScaleY; S.PositionMeters[2] = B * ScaleXZ; S.NormalUnit[1] = Sign; }
            else               { S.PositionMeters[0] = A * ScaleXZ; S.PositionMeters[1] = B * ScaleY; S.PositionMeters[2] = Sign * ScaleXZ; S.NormalUnit[2] = Sign; }
            S.CurvatureWeight = std::max(std::fabs(A), std::fabs(B));
            break;
        }
        case SurfacePresetCategory::UtahTeapot:
        {
            // Recognizable Utah teapot silhouette: belly lathe + lid knob + S-curved spout + D-loop handle
            const float Phi = V * TwoPi;
            if (U < 0.64f)
            {
                // Belly lathe y in [-0.55, 0.35]
                const float T = W;
                const float Y = -0.52f + T * 0.88f;
                const float R = 0.24f + 0.44f * std::sin(T * Pi * 0.92f + 0.12f);
                S.PositionMeters[0] = R * std::cos(Phi);
                S.PositionMeters[1] = Y;
                S.PositionMeters[2] = R * std::sin(Phi);
                S.NormalUnit[0]     = std::cos(Phi);
                S.NormalUnit[1]     = 0.15f * std::cos(T * Pi);
                S.NormalUnit[2]     = std::sin(Phi);
                S.CurvatureWeight   = 0.55f;
            }
            else if (U < 0.74f)
            {
                // Lid dome + finial knob
                const float T = W;
                const float R = 0.44f * (1.0f - T) + 0.09f * std::sin(T * Pi);
                S.PositionMeters[0] = R * std::cos(Phi);
                S.PositionMeters[1] = 0.36f + T * 0.28f;
                S.PositionMeters[2] = R * std::sin(Phi);
                S.NormalUnit[0]     = std::cos(Phi) * 0.4f;
                S.NormalUnit[1]     = 0.9f;
                S.NormalUnit[2]     = std::sin(Phi) * 0.4f;
                S.GroupIndex        = 1u;
                S.CurvatureWeight   = 0.82f;
            }
            else if (U < 0.87f)
            {
                // S-curved spout on +X
                const float T = W;
                const float Rad = 0.11f * (1.0f - 0.48f * T);
                const float Sx = 0.58f + 0.42f * T + 0.08f * std::sin(T * Pi);
                const float Sy = -0.18f + 0.56f * T * T + 0.08f * T;
                S.PositionMeters[0] = Sx + std::cos(Phi) * Rad * 0.4f;
                S.PositionMeters[1] = Sy + std::sin(Phi) * Rad;
                S.PositionMeters[2] = std::cos(Phi) * Rad;
                S.NormalUnit[0]     = 0.3f;
                S.NormalUnit[1]     = std::sin(Phi);
                S.NormalUnit[2]     = std::cos(Phi);
                S.GroupIndex        = 2u;
                S.CurvatureWeight   = 0.90f;
            }
            else
            {
                // C-loop handle on -X
                const float A   = -Pi * 0.55f + W * Pi * 1.15f;
                const float Rad = 0.065f;
                const float Hx  = -0.56f - 0.34f * std::cos(A);
                const float Hy  =  0.02f + 0.32f * std::sin(A);
                S.PositionMeters[0] = Hx - std::cos(Phi) * Rad * std::cos(A);
                S.PositionMeters[1] = Hy + std::cos(Phi) * Rad * std::sin(A);
                S.PositionMeters[2] = std::sin(Phi) * Rad;
                S.NormalUnit[0]     = -std::cos(A);
                S.NormalUnit[1]     =  std::sin(A);
                S.NormalUnit[2]     =  std::sin(Phi);
                S.GroupIndex        = 3u;
                S.CurvatureWeight   = 0.88f;
            }
            break;
        }
        case SurfacePresetCategory::SportsCarSilhouette:
        {
            // Aerodynamic low-slung coupe body + greenhouse cabin + 4 wheels
            if (U < 0.74f)
            {
                const float X = (V * 2.0f - 1.0f) * 1.0f;
                const float Z = (W * 2.0f - 1.0f) * 0.46f;
                // Sleek roofline profile along X (-1 rear .. +1 nose)
                float TopY = 0.06f * (1.0f - 0.35f * X * X);
                if (X > -0.58f && X < 0.34f)
                {
                    const float C = (X + 0.12f) / 0.46f;
                    const float Arch = std::max(0.0f, 1.0f - C * C);
                    TopY += 0.30f * Arch * std::max(0.0f, 1.0f - (Z / 0.42f) * (Z / 0.42f) * 0.45f);
                }
                const float BotY = -0.22f;
                const bool SideWall = (Rng.NextUnitFloat() < 0.35f);
                S.PositionMeters[0] = X;
                S.PositionMeters[1] = SideWall ? (BotY + Rng.NextUnitFloat() * (TopY - BotY)) : TopY;
                S.PositionMeters[2] = SideWall ? ((Z >= 0.0f ? 1.0f : -1.0f) * 0.46f) : Z;
                S.NormalUnit[0]     = 0.0f;
                S.NormalUnit[1]     = SideWall ? 0.1f : 1.0f;
                S.NormalUnit[2]     = SideWall ? (Z >= 0.0f ? 1.0f : -1.0f) : 0.0f;
                S.CurvatureWeight   = SideWall ? 0.65f : 0.80f;
            }
            else
            {
                const int Wheel = static_cast<int>(Rng.NextUint32() % 4u);
                const float Wx  = (Wheel < 2) ? -0.58f : 0.58f;
                const float Wz  = (Wheel % 2 == 0) ? -0.44f : 0.44f;
                const float A   = V * TwoPi;
                const float R   = 0.21f;
                S.PositionMeters[0] = Wx + std::cos(A) * R;
                S.PositionMeters[1] = -0.18f + std::sin(A) * R;
                S.PositionMeters[2] = Wz + (W - 0.5f) * 0.12f;
                S.NormalUnit[0]     = std::cos(A);
                S.NormalUnit[1]     = std::sin(A);
                S.NormalUnit[2]     = 0.0f;
                S.GroupIndex        = static_cast<uint32_t>(1 + Wheel);
                S.CurvatureWeight   = 0.92f;
            }
            break;
        }
        case SurfacePresetCategory::SculptedBust:
        {
            // Head cranium + neck column + draped classical shoulders
            const float Phi = V * TwoPi;
            if (U < 0.52f)
            {
                // Sculpted ovoid head
                const float Z = W * 2.0f - 1.0f;
                const float R = std::sqrt(std::max(0.0f, 1.0f - Z * Z));
                const float Jaw = 0.82f + 0.18f * std::clamp((Z + 0.6f) * 0.8f, 0.0f, 1.0f);
                S.PositionMeters[0] = R * std::cos(Phi) * 0.34f * Jaw;
                S.PositionMeters[1] = 0.38f + Z * 0.44f;
                S.PositionMeters[2] = R * std::sin(Phi) * 0.40f * Jaw + 0.04f * std::max(0.0f, std::sin(Phi)) * std::exp(-Z * Z * 4.0f);
                S.NormalUnit[0]     = R * std::cos(Phi);
                S.NormalUnit[1]     = Z;
                S.NormalUnit[2]     = R * std::sin(Phi);
                S.CurvatureWeight   = 0.72f;
            }
            else if (U < 0.68f)
            {
                // Neck cylinder
                const float Y = -0.16f + W * 0.24f;
                S.PositionMeters[0] = 0.18f * std::cos(Phi);
                S.PositionMeters[1] = Y;
                S.PositionMeters[2] = 0.18f * std::sin(Phi);
                S.NormalUnit[0]     = std::cos(Phi);
                S.NormalUnit[2]     = std::sin(Phi);
                S.GroupIndex        = 1u;
                S.CurvatureWeight   = 0.45f;
            }
            else
            {
                // Classical draped shoulder plinth
                const float T = W;
                const float Y = -0.78f + T * 0.62f;
                const float Rx = 0.78f * std::sin(T * Pi * 0.75f + 0.25f);
                const float Rz = 0.34f * std::sin(T * Pi * 0.75f + 0.25f);
                S.PositionMeters[0] = Rx * std::cos(Phi);
                S.PositionMeters[1] = Y;
                S.PositionMeters[2] = Rz * std::sin(Phi);
                S.NormalUnit[0]     = std::cos(Phi);
                S.NormalUnit[1]     = 0.25f;
                S.NormalUnit[2]     = std::sin(Phi);
                S.GroupIndex        = 2u;
                S.CurvatureWeight   = 0.65f;
            }
            break;
        }
        case SurfacePresetCategory::TorusKnot32:
        {
            // (p=3, q=2) Trefoil torus knot tube
            const float T   = U * TwoPi;
            const float Phi = V * TwoPi;
            const float P   = 3.0f, Q = 2.0f;
            const float R0  = 0.56f + 0.24f * std::cos(Q * T);
            const float Cx  = R0 * std::cos(P * T);
            const float Cy  = 0.30f * std::sin(Q * T);
            const float Cz  = R0 * std::sin(P * T);
            const float Tube = 0.14f;
            const float RadX = std::cos(P * T);
            const float RadZ = std::sin(P * T);
            S.PositionMeters[0] = Cx + Tube * (std::cos(Phi) * RadX);
            S.PositionMeters[1] = Cy + Tube * std::sin(Phi);
            S.PositionMeters[2] = Cz + Tube * (std::cos(Phi) * RadZ);
            S.NormalUnit[0]     = std::cos(Phi) * RadX;
            S.NormalUnit[1]     = std::sin(Phi);
            S.NormalUnit[2]     = std::cos(Phi) * RadZ;
            S.CurvatureWeight   = 0.85f;
            break;
        }
        case SurfacePresetCategory::PrecisionGear:
        {
            // 12-tooth machined cogwheel with central bore
            const float Ang   = U * TwoPi;
            const float Tooth = std::clamp(std::sin(Ang * 12.0f) * 3.0f, -1.0f, 1.0f);
            const float OuterR = 0.78f + 0.14f * Tooth;
            const float InnerR = 0.26f;
            const float Thick  = 0.18f;
            if (V < 0.55f)
            {
                const float R = InnerR + std::sqrt(W) * (OuterR - InnerR);
                const float Sgn = (Rng.NextUnitFloat() < 0.5f) ? 1.0f : -1.0f;
                S.PositionMeters[0] = R * std::cos(Ang);
                S.PositionMeters[1] = Sgn * Thick;
                S.PositionMeters[2] = R * std::sin(Ang);
                S.NormalUnit[1]     = Sgn;
                S.CurvatureWeight   = (R > 0.72f) ? 0.92f : 0.40f;
            }
            else
            {
                const float R = (V < 0.85f) ? OuterR : InnerR;
                S.PositionMeters[0] = R * std::cos(Ang);
                S.PositionMeters[1] = (W * 2.0f - 1.0f) * Thick;
                S.PositionMeters[2] = R * std::sin(Ang);
                S.NormalUnit[0]     = std::cos(Ang) * (V < 0.85f ? 1.0f : -1.0f);
                S.NormalUnit[2]     = std::sin(Ang) * (V < 0.85f ? 1.0f : -1.0f);
                S.CurvatureWeight   = 0.95f;
            }
            break;
        }
        case SurfacePresetCategory::QuartzCrystalCluster:
        {
            // 7 hexagonal quartz prisms radiating from base
            const int Shard = static_cast<int>(Rng.NextUint32() % 7u);
            const float Tilt = (Shard == 0) ? 0.0f : (0.32f + 0.06f * static_cast<float>(Shard));
            const float Az   = static_cast<float>(Shard) * 1.0472f;
            const float Len  = (Shard == 0) ? 1.55f : (0.95f + 0.10f * static_cast<float>(Shard % 3));
            const float Hex  = static_cast<float>(Rng.NextUint32() % 6u) * (Pi / 3.0f);
            const float Edge = V;
            const float H    = W * Len;
            const float Rad  = (H < Len * 0.76f) ? 0.16f : 0.16f * std::max(0.0f, 1.0f - (H - Len * 0.76f) / (Len * 0.24f));
            const float A0 = Hex, A1 = Hex + Pi / 3.0f;
            const float Lx = Rad * (std::cos(A0) * (1.0f - Edge) + std::cos(A1) * Edge);
            const float Lz = Rad * (std::sin(A0) * (1.0f - Edge) + std::sin(A1) * Edge);
            const float Ly = H - 0.65f;
            // Tilt outward along Az
            const float Ct = std::cos(Tilt), St = std::sin(Tilt);
            const float Tx = Lx * Ct + Ly * St;
            const float Ty = -Lx * St + Ly * Ct;
            const float Ca = std::cos(Az), Sa = std::sin(Az);
            S.PositionMeters[0] = Tx * Ca - Lz * Sa;
            S.PositionMeters[1] = Ty;
            S.PositionMeters[2] = Tx * Sa + Lz * Ca;
            S.NormalUnit[0]     = std::cos(A0 + Pi / 6.0f);
            S.NormalUnit[1]     = (H >= Len * 0.76f) ? 0.65f : 0.0f;
            S.NormalUnit[2]     = std::sin(A0 + Pi / 6.0f);
            S.GroupIndex        = static_cast<uint32_t>(Shard);
            S.CurvatureWeight   = (H >= Len * 0.76f) ? 0.98f : 0.70f;
            break;
        }
        case SurfacePresetCategory::ArchitecturalArchway:
        {
            // Twin fluted pillars + semi-circular Roman arch
            if (U < 0.56f)
            {
                const float Side = (U < 0.28f) ? -0.66f : 0.66f;
                const float Ang  = V * TwoPi;
                S.PositionMeters[0] = Side + 0.18f * std::cos(Ang);
                S.PositionMeters[1] = -0.88f + W * 1.12f;
                S.PositionMeters[2] = 0.18f * std::sin(Ang);
                S.NormalUnit[0]     = std::cos(Ang);
                S.NormalUnit[2]     = std::sin(Ang);
                S.CurvatureWeight   = 0.55f;
            }
            else
            {
                const float Theta = V * Pi;
                const float Phi   = W * TwoPi;
                const float Major = 0.66f, Minor = 0.18f;
                const float R     = Major + Minor * std::cos(Phi);
                S.PositionMeters[0] = R * std::cos(Theta);
                S.PositionMeters[1] = 0.24f + R * std::sin(Theta);
                S.PositionMeters[2] = Minor * std::sin(Phi);
                S.NormalUnit[0]     = std::cos(Phi) * std::cos(Theta);
                S.NormalUnit[1]     = std::cos(Phi) * std::sin(Theta);
                S.NormalUnit[2]     = std::sin(Phi);
                S.GroupIndex        = 1u;
                S.CurvatureWeight   = 0.85f;
            }
            break;
        }
        case SurfacePresetCategory::FacetedSpire:
        {
            // Octagonal faceted spire / obelisk cone
            const float H   = 1.0f - std::sqrt(U); // area-weighted toward wider base
            const int Side  = static_cast<int>(Rng.NextUint32() % 8u);
            const float A0  = static_cast<float>(Side) * (TwoPi / 8.0f);
            const float A1  = A0 + (TwoPi / 8.0f);
            const float R   = 0.72f * (1.0f - H);
            S.PositionMeters[0] = R * (std::cos(A0) * (1.0f - V) + std::cos(A1) * V);
            S.PositionMeters[1] = -0.85f + H * 1.75f;
            S.PositionMeters[2] = R * (std::sin(A0) * (1.0f - V) + std::sin(A1) * V);
            S.NormalUnit[0]     = std::cos(A0 + Pi / 8.0f);
            S.NormalUnit[1]     = 0.40f;
            S.NormalUnit[2]     = std::sin(A0 + Pi / 8.0f);
            S.CurvatureWeight   = 0.4f + 0.6f * H;
            break;
        }
        case SurfacePresetCategory::StellatedStarPoly:
        {
            // 12-spike stellated dodecahedron star polyhedron
            SampleUnitSphere(S.PositionMeters, S.NormalUnit);
            const float Spike = std::pow(
                std::max(0.0f, std::fabs(S.NormalUnit[0] * S.NormalUnit[1]) + std::fabs(S.NormalUnit[1] * S.NormalUnit[2]) + std::fabs(S.NormalUnit[2] * S.NormalUnit[0])),
                0.7f);
            const float Rad = 0.48f + 0.72f * std::pow(std::max(0.0f, std::cos(std::acos(std::clamp(S.NormalUnit[1], -1.0f, 1.0f)) * 3.0f) * std::cos(std::atan2(S.NormalUnit[2], S.NormalUnit[0]) * 5.0f)), 2.0f)
                            + 0.25f * Spike;
            S.PositionMeters[0] *= Rad;
            S.PositionMeters[1] *= Rad;
            S.PositionMeters[2] *= Rad;
            S.CurvatureWeight = std::clamp((Rad - 0.48f) / 0.75f, 0.0f, 1.0f);
            break;
        }
        default:
            SampleUnitSphere(S.PositionMeters, S.NormalUnit);
            break;
    }

    NormalizeVec3(S.NormalUnit);
    return S;
}

} // namespace

std::string_view QuerySurfacePresetLabel(SurfacePresetCategory Preset) noexcept
{
    const auto Idx = static_cast<std::size_t>(Preset);
    if (Idx >= SurfaceLabels.size()) return SurfaceLabels[0];
    return SurfaceLabels[Idx];
}

std::vector<ParticleAnchorSample> SampleProceduralSurfaceAnchors(
    const SurfaceAnchorOrigin& Origin,
    uint32_t                   SampleCount,
    uint32_t                   Seed)
{
    if (Origin.Preset == SurfacePresetCategory::ControlVehicleIntegrator)
    {
        auto Out = SampleControlVehicleAnchors(SampleCount, Seed, true, false);
        for (auto& S : Out)
        {
            S.PositionMeters[0] *= Origin.ScaleMeters[0];
            S.PositionMeters[1] *= Origin.ScaleMeters[1];
            S.PositionMeters[2] *= Origin.ScaleMeters[2];
            ApplyEulerDegrees(S.PositionMeters, Origin.EulerDegrees);
            ApplyEulerDegrees(S.NormalUnit, Origin.EulerDegrees);
            NormalizeVec3(S.NormalUnit);

            S.PositionMeters[0] += Origin.CenterMeters[0];
            S.PositionMeters[1] += Origin.CenterMeters[1];
            S.PositionMeters[2] += Origin.CenterMeters[2];
        }
        return Out;
    }

    std::vector<ParticleAnchorSample> Out;
    Out.reserve(SampleCount);
    DeterministicPcgRng Rng(Seed);

    for (uint32_t I = 0u; I < SampleCount; ++I)
    {
        ParticleAnchorSample S = EvaluateUnitPresetSample(Origin.Preset, I, SampleCount, Rng);

        // Apply SurfaceSamplingStrategy modulation
        if (Origin.SamplingStrategy == SurfaceSamplingStrategy::SurfaceInteriorVolume && (I % 3u == 0u))
        {
            const float Shrink = std::cbrt(Rng.NextUnitFloat());
            S.PositionMeters[0] *= Shrink;
            S.PositionMeters[1] *= Shrink;
            S.PositionMeters[2] *= Shrink;
        }
        else if (Origin.SamplingStrategy == SurfaceSamplingStrategy::RidgeCreaseAccent && S.CurvatureWeight < 0.55f)
        {
            // Re-draw up to 3 attempts to bias toward high-curvature ridges
            for (int Retry = 0; Retry < 3; ++Retry)
            {
                const ParticleAnchorSample Cand = EvaluateUnitPresetSample(Origin.Preset, I + static_cast<uint32_t>(Retry * 17 + 7), SampleCount, Rng);
                if (Cand.CurvatureWeight > S.CurvatureWeight) S = Cand;
            }
        }

        // Apply scale, Euler rotation, and world translation
        S.PositionMeters[0] *= Origin.ScaleMeters[0];
        S.PositionMeters[1] *= Origin.ScaleMeters[1];
        S.PositionMeters[2] *= Origin.ScaleMeters[2];
        ApplyEulerDegrees(S.PositionMeters, Origin.EulerDegrees);
        ApplyEulerDegrees(S.NormalUnit, Origin.EulerDegrees);
        NormalizeVec3(S.NormalUnit);

        S.PositionMeters[0] += Origin.CenterMeters[0];
        S.PositionMeters[1] += Origin.CenterMeters[1];
        S.PositionMeters[2] += Origin.CenterMeters[2];
        Out.push_back(S);
    }

    return Out;
}

std::vector<ParticleAnchorSample> SampleTriangleSurfaceAnchors(
    std::span<const SurfaceTriangleInput> Triangles,
    const SurfaceAnchorOrigin&            TransformOrigin,
    uint32_t                              SampleCount,
    uint32_t                              Seed)
{
    if (Triangles.empty())
    {
        return SampleProceduralSurfaceAnchors(TransformOrigin, SampleCount, Seed);
    }

    // Build cumulative triangle area distribution for exact area-weighted sampling
    std::vector<float> Cdf;
    Cdf.reserve(Triangles.size());
    float TotalArea = 0.0f;
    for (const auto& Tri : Triangles)
    {
        const float E1x = Tri.VertexB[0] - Tri.VertexA[0];
        const float E1y = Tri.VertexB[1] - Tri.VertexA[1];
        const float E1z = Tri.VertexB[2] - Tri.VertexA[2];
        const float E2x = Tri.VertexC[0] - Tri.VertexA[0];
        const float E2y = Tri.VertexC[1] - Tri.VertexA[1];
        const float E2z = Tri.VertexC[2] - Tri.VertexA[2];
        const float Cx  = E1y * E2z - E1z * E2y;
        const float Cy  = E1z * E2x - E1x * E2z;
        const float Cz  = E1x * E2y - E1y * E2x;
        TotalArea += 0.5f * std::sqrt(Cx * Cx + Cy * Cy + Cz * Cz);
        Cdf.push_back(TotalArea);
    }
    if (TotalArea <= 1e-8f)
    {
        return SampleProceduralSurfaceAnchors(TransformOrigin, SampleCount, Seed);
    }

    std::vector<ParticleAnchorSample> Out;
    Out.reserve(SampleCount);
    DeterministicPcgRng Rng(Seed);

    for (uint32_t I = 0u; I < SampleCount; ++I)
    {
        const float Pick = Rng.NextUnitFloat() * TotalArea;
        const auto It    = std::lower_bound(Cdf.begin(), Cdf.end(), Pick);
        const std::size_t TriIdx = static_cast<std::size_t>(std::min<std::ptrdiff_t>(
            std::distance(Cdf.begin(), It), static_cast<std::ptrdiff_t>(Triangles.size() - 1u)));
        const SurfaceTriangleInput& Tri = Triangles[TriIdx];

        const float R1 = std::sqrt(Rng.NextUnitFloat());
        const float R2 = Rng.NextUnitFloat();
        const float W0 = 1.0f - R1;
        const float W1 = R1 * (1.0f - R2);
        const float W2 = R1 * R2;

        ParticleAnchorSample S {};
        for (int K = 0; K < 3; ++K)
        {
            S.PositionMeters[K] = (Tri.VertexA[K] * W0 + Tri.VertexB[K] * W1 + Tri.VertexC[K] * W2) * TransformOrigin.ScaleMeters[K];
        }

        const float E1x = Tri.VertexB[0] - Tri.VertexA[0];
        const float E1y = Tri.VertexB[1] - Tri.VertexA[1];
        const float E1z = Tri.VertexB[2] - Tri.VertexA[2];
        const float E2x = Tri.VertexC[0] - Tri.VertexA[0];
        const float E2y = Tri.VertexC[1] - Tri.VertexA[1];
        const float E2z = Tri.VertexC[2] - Tri.VertexA[2];
        S.NormalUnit[0] = E1y * E2z - E1z * E2y;
        S.NormalUnit[1] = E1z * E2x - E1x * E2z;
        S.NormalUnit[2] = E1x * E2y - E1y * E2x;

        ApplyEulerDegrees(S.PositionMeters, TransformOrigin.EulerDegrees);
        ApplyEulerDegrees(S.NormalUnit, TransformOrigin.EulerDegrees);
        NormalizeVec3(S.NormalUnit);

        S.PositionMeters[0] += TransformOrigin.CenterMeters[0];
        S.PositionMeters[1] += TransformOrigin.CenterMeters[1];
        S.PositionMeters[2] += TransformOrigin.CenterMeters[2];
        S.CurvatureWeight    = 0.6f;
        S.GroupIndex         = static_cast<uint32_t>(TriIdx);
        Out.push_back(S);
    }

    return Out;
}

} // namespace Frontier::ParticleSpace
