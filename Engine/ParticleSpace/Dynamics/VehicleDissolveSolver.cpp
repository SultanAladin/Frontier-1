/*============================================================================================================================================
                                                       VEHICLEDISSOLVESOLVER.CPP
============================================================================================================================================*/
// 🚗 Controlled Integrator (ControlVehicle) 16^3 lattice Derez, Reverse-Rez, Dissolve-to-Nothing, and Dissolve-to-Butterfly implementation

#include "Engine/ParticleSpace/Dynamics/VehicleDissolveSolver.h"
#include "Engine/ParticleSpace/Sprites/ProceduralSpriteSynthesizer.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <unordered_map>

namespace Frontier::ParticleSpace
{

namespace
{

constexpr float kPi    = 3.14159265358979323846f;
constexpr float kTwoPi = 6.28318530717958647692f;

struct Vec3f
{
    float X = 0.0f;
    float Y = 0.0f;
    float Z = 0.0f;
};

[[nodiscard]] inline Vec3f operator+(Vec3f A, Vec3f B) noexcept { return { A.X + B.X, A.Y + B.Y, A.Z + B.Z }; }
[[nodiscard]] inline Vec3f operator-(Vec3f A, Vec3f B) noexcept { return { A.X - B.X, A.Y - B.Y, A.Z - B.Z }; }
[[nodiscard]] inline Vec3f operator*(Vec3f A, float S) noexcept { return { A.X * S, A.Y * S, A.Z * S }; }
[[nodiscard]] inline Vec3f operator*(Vec3f A, Vec3f B) noexcept { return { A.X * B.X, A.Y * B.Y, A.Z * B.Z }; }
[[nodiscard]] inline float Dot3(Vec3f A, Vec3f B) noexcept { return A.X * B.X + A.Y * B.Y + A.Z * B.Z; }
[[nodiscard]] inline Vec3f Cross3(Vec3f A, Vec3f B) noexcept
{
    return {
        A.Y * B.Z - A.Z * B.Y,
        A.Z * B.X - A.X * B.Z,
        A.X * B.Y - A.Y * B.X,
    };
}
[[nodiscard]] inline float Length3(Vec3f V) noexcept { return std::sqrt(Dot3(V, V)); }
[[nodiscard]] inline Vec3f Normalize3(Vec3f V) noexcept
{
    const float L = Length3(V);
    return (L > 1e-7f) ? (V * (1.0f / L)) : Vec3f{ 0.0f, 1.0f, 0.0f };
}

[[nodiscard]] inline float Saturate(float V) noexcept { return std::clamp(V, 0.0f, 1.0f); }
[[nodiscard]] inline float LerpF(float A, float B, float T) noexcept { return A + (B - A) * T; }
[[nodiscard]] inline Vec3f LerpV3(Vec3f A, Vec3f B, float T) noexcept
{
    return { LerpF(A.X, B.X, T), LerpF(A.Y, B.Y, T), LerpF(A.Z, B.Z, T) };
}
[[nodiscard]] inline float SmoothStep(float E0, float E1, float X) noexcept
{
    if (std::fabs(E1 - E0) < 1e-6f)
    {
        return X >= E1 ? 1.0f : 0.0f;
    }
    const float T = Saturate((X - E0) / (E1 - E0));
    return T * T * (3.0f - 2.0f * T);
}

[[nodiscard]] inline uint32_t MixHash(uint32_t X) noexcept
{
    X ^= X >> 16u;
    X *= 0x7feb352du;
    X ^= X >> 15u;
    X *= 0x846ca68bu;
    X ^= X >> 16u;
    return X;
}

[[nodiscard]] inline float HashUnit(uint32_t H) noexcept
{
    return static_cast<float>(MixHash(H) & 0x00ffffffu) / 16777215.0f;
}

[[nodiscard]] inline uint32_t HashLatticeCell(int32_t Ix, int32_t Iy, int32_t Iz, uint32_t Seed) noexcept
{
    return MixHash(
        static_cast<uint32_t>(Ix * 73856093) ^
        static_cast<uint32_t>(Iy * 19349663) ^
        static_cast<uint32_t>(Iz * 83492791) ^
        (Seed * 2654435761u));
}

// Closed-form ballistic motion under constant gravity G and linear air drag K:
// dv/dt = G - K * v  =>  v(t) = vTerm + (v0 - vTerm) * exp(-K*t),  p(t) = p0 + vTerm*t + (v0 - vTerm)*(1 - exp(-K*t))/K
inline void EvaluateLinearDragBallistic(
    Vec3f  P0,
    Vec3f  V0,
    float  GravityY,
    float  DragK,
    float  TimeSec,
    Vec3f& OutPos,
    Vec3f& OutVel) noexcept
{
    const float K = std::max(0.02f, DragK);
    const float ExpKt = std::exp(-K * TimeSec);
    const float InvK  = 1.0f / K;
    const Vec3f VTerm{ 0.0f, GravityY * InvK, 0.0f };
    const Vec3f DeltaV = V0 - VTerm;
    OutVel = VTerm + DeltaV * ExpKt;
    OutPos = P0 + VTerm * TimeSec + DeltaV * ((1.0f - ExpKt) * InvK);
}

struct EmissiveBounceLight
{
    Vec3f Position{};
    Vec3f Color{};
    float Radius = 1.2f;
};

struct CameraBasis
{
    Vec3f Eye{};
    Vec3f Forward{};
    Vec3f Right{};
    Vec3f Up{};
    float TanHalfFovY = 0.32f;
    float Aspect      = 1.3333f;
    float WidthF      = 720.0f;
    float HeightF     = 540.0f;
};

[[nodiscard]] CameraBasis BuildCameraBasis(const VehicleDissolveRenderCamera& Cam, uint32_t W, uint32_t H) noexcept
{
    CameraBasis B{};
    B.Eye     = { Cam.EyeX, Cam.EyeY, Cam.EyeZ };
    const Vec3f Target{ Cam.TargetX, Cam.TargetY, Cam.TargetZ };
    B.Forward = Normalize3(Target - B.Eye);
    B.Right   = Normalize3(Cross3(B.Forward, Vec3f{ 0.0f, 1.0f, 0.0f }));
    B.Up      = Normalize3(Cross3(B.Right, B.Forward));
    B.TanHalfFovY = std::tan(Cam.VerticalFovDeg * 0.5f * (kPi / 180.0f));
    B.WidthF  = static_cast<float>(W);
    B.HeightF = static_cast<float>(H);
    B.Aspect  = B.WidthF / std::max(1.0f, B.HeightF);
    return B;
}

[[nodiscard]] bool ProjectWorldPoint(const CameraBasis& Cam, Vec3f P, float& OutSx, float& OutSy, float& OutDepth) noexcept
{
    const Vec3f Rel = P - Cam.Eye;
    const float Z   = Dot3(Rel, Cam.Forward);
    if (Z <= 0.12f)
    {
        return false;
    }
    const float X = Dot3(Rel, Cam.Right);
    const float Y = Dot3(Rel, Cam.Up);
    const float NdcX = X / (Z * Cam.TanHalfFovY * Cam.Aspect);
    const float NdcY = Y / (Z * Cam.TanHalfFovY);
    OutSx    = (NdcX * 0.5f + 0.5f) * Cam.WidthF;
    OutSy    = (0.5f - NdcY * 0.5f) * Cam.HeightF;
    OutDepth = Z;
    return true;
}

[[nodiscard]] Vec3f EvaluateBounceLights(
    Vec3f                                  WorldPos,
    Vec3f                                  WorldNormal,
    const std::vector<EmissiveBounceLight>& Lights,
    float                                  Gain) noexcept
{
    Vec3f Sum{};
    for (const EmissiveBounceLight& L : Lights)
    {
        const Vec3f ToL = L.Position - WorldPos;
        const float DistSq = Dot3(ToL, ToL);
        const float MaxR = L.Radius;
        if (DistSq >= MaxR * MaxR)
        {
            continue;
        }
        const float Dist = std::sqrt(DistSq + 0.04f);
        const Vec3f Dir  = ToL * (1.0f / Dist);
        const float NdotL = std::max(0.08f, Dot3(WorldNormal, Dir));
        // Windowed inverse-square attenuation matching Frontier FireIllumination.wgsl
        const float Ratio = Dist / MaxR;
        const float Window = std::max(0.0f, 1.0f - Ratio * Ratio * Ratio * Ratio);
        const float Atten  = (Window * Window) / (DistSq + 0.18f);
        Sum = Sum + L.Color * (Atten * NdotL * Gain);
    }
    return Sum;
}

[[nodiscard]] Vec3f EvaluateStudioPbrSurface(
    Vec3f                                  WorldPos,
    Vec3f                                  Normal,
    Vec3f                                  ViewDir,
    uint8_t                                MaterialSlot,
    const std::vector<EmissiveBounceLight>& BounceLights,
    float                                  BounceGain,
    bool                                   EnableGi) noexcept
{
    Vec3f Albedo{ 0.022f, 0.026f, 0.035f }; // 6: Deep liquid obsidian automotive body coat
    Vec3f SelfEmit{};
    float Roughness = 0.10f;
    float Metalness = 0.28f;
    float Clearcoat = 1.00f;

    if (MaterialSlot == 7u) // Smoked automotive glass
    {
        Albedo    = { 0.030f, 0.045f, 0.068f };
        Roughness = 0.05f;
        Metalness = 0.12f;
        Clearcoat = 1.00f;
    }
    else if (MaterialSlot == 8u) // Dark satin trim plastic
    {
        Albedo    = { 0.052f, 0.056f, 0.064f };
        Roughness = 0.56f;
        Metalness = 0.04f;
        Clearcoat = 0.10f;
    }
    else if (MaterialSlot == 9u) // Matte vulcanized rubber tyre
    {
        Albedo    = { 0.042f, 0.044f, 0.048f };
        Roughness = 0.80f;
        Metalness = 0.02f;
        Clearcoat = 0.00f;
    }
    else if (MaterialSlot == 10u || MaterialSlot == 12u) // Alloy wheel rim / metallic trim
    {
        Albedo    = { 0.74f, 0.78f, 0.84f };
        Roughness = 0.20f;
        Metalness = 0.94f;
        Clearcoat = 0.18f;
    }
    else if (MaterialSlot == 11u) // Brake caliper / rotor
    {
        Albedo    = { 0.52f, 0.14f, 0.10f };
        Roughness = 0.34f;
        Metalness = 0.65f;
        Clearcoat = 0.25f;
    }
    else if (MaterialSlot == 13u) // Cool LED headlight
    {
        Albedo    = { 0.92f, 0.96f, 1.00f };
        SelfEmit  = { 1.85f, 2.25f, 2.65f };
        Roughness = 0.08f;
    }
    else if (MaterialSlot == 14u) // Crimson LED taillight bar
    {
        Albedo    = { 0.88f, 0.10f, 0.08f };
        SelfEmit  = { 2.15f, 0.18f, 0.12f };
        Roughness = 0.10f;
    }

    const Vec3f KeyDir  = Normalize3({ 0.56f, 0.78f, 0.42f });
    const Vec3f FillDir = Normalize3({ -0.68f, 0.46f, 0.54f });
    const Vec3f RimDir  = Normalize3({ -0.18f, 0.58f, -0.80f });
    const Vec3f TopDir  = Normalize3({ 0.0f, 1.0f, 0.06f });

    const float NdotV = std::max(0.02f, Dot3(Normal, ViewDir));
    const float F0    = LerpF(0.045f, 0.86f, Metalness);
    const float Fresnel = F0 + (1.0f - F0) * std::pow(1.0f - NdotV, 5.0f);

    auto SpecularLobe = [&](Vec3f LightDir, float Exp) noexcept -> float
    {
        const Vec3f H = Normalize3(LightDir + ViewDir);
        const float NdotH = std::max(0.0f, Dot3(Normal, H));
        const float NdotL = std::max(0.0f, Dot3(Normal, LightDir));
        return (NdotL > 0.0f) ? std::pow(NdotH, Exp) * NdotL : 0.0f;
    };

    const float DiffKey  = std::max(0.0f, Dot3(Normal, KeyDir));
    const float DiffFill = std::max(0.0f, Dot3(Normal, FillDir));
    const float DiffRim  = std::max(0.0f, Dot3(Normal, RimDir));
    const float HemiSky  = 0.5f + 0.5f * Normal.Y;

    const float SpecExp = LerpF(180.0f, 14.0f, Roughness);
    const float SpecA   = SpecularLobe(KeyDir, SpecExp) * 1.45f;
    const float SpecB   = SpecularLobe(FillDir, SpecExp * 0.65f) * 0.75f;
    const float SpecC   = SpecularLobe(RimDir, SpecExp * 0.55f) * 0.95f;
    const float CoatStrip = Clearcoat * SpecularLobe(TopDir, 90.0f) * 1.65f;

    // Studio reflection environment band (overhead softbox + horizon cyan rim glow)
    const Vec3f Refl = Normalize3(Normal * (2.0f * Dot3(Normal, ViewDir)) - ViewDir);
    const float OverheadBox = SmoothStep(0.68f, 0.94f, Refl.Y) * SmoothStep(0.62f, 0.10f, std::fabs(Refl.X));
    const float HorizonCyan = SmoothStep(-0.12f, 0.24f, Refl.Y) * (1.0f - SmoothStep(0.24f, 0.62f, Refl.Y));
    const Vec3f EnvRefl =
        Vec3f{ 0.94f, 0.96f, 1.00f } * (OverheadBox * (1.15f - Roughness * 0.8f)) +
        Vec3f{ 0.14f, 0.24f, 0.34f } * (HorizonCyan * 0.48f) +
        Vec3f{ 0.05f, 0.06f, 0.08f } * HemiSky;

    Vec3f Lit =
        Albedo * (Vec3f{ 0.96f, 0.98f, 1.00f } * (DiffKey * 0.85f) +
                  Vec3f{ 0.52f, 0.68f, 0.88f } * (DiffFill * 0.42f) +
                  Vec3f{ 0.44f, 0.86f, 1.00f } * (DiffRim * 0.32f) +
                  Vec3f{ 0.10f, 0.12f, 0.16f } * (0.35f + 0.65f * HemiSky));

    const Vec3f SpecTint = LerpV3(Vec3f{ 1.0f, 1.0f, 1.0f }, Albedo, Metalness * 0.8f);
    Lit = Lit + SpecTint * (SpecA + SpecB + SpecC) + Vec3f{ 0.92f, 0.96f, 1.00f } * CoatStrip + EnvRefl * (Fresnel * (0.45f + 0.55f * Clearcoat));

    if (EnableGi && !BounceLights.empty())
    {
        const Vec3f Gi = EvaluateBounceLights(WorldPos, Normal, BounceLights, BounceGain);
        const float GiSpecScale = 0.22f + 0.58f * Fresnel + 0.35f * Clearcoat;
        Lit = Lit + Albedo * (Gi * 1.4f) + Gi * (GiSpecScale * 0.38f);
    }

    return Lit + SelfEmit;
}

[[nodiscard]] Vec3f EvaluateLeadingRimEmission(float Proximity01, VehicleDissolveMode Mode) noexcept
{
    const float P = Saturate(Proximity01);
    const float Hot = P * P;
    if (Mode == VehicleDissolveMode::DissolveToNothing)
    {
        // Glowing ember amber -> incandescent white-gold sublimation rim
        const Vec3f Outer{ 1.15f, 0.36f, 0.08f };
        const Vec3f Mid{ 1.95f, 0.92f, 0.26f };
        const Vec3f Peak{ 3.10f, 2.75f, 2.15f };
        return (Hot < 0.55f) ? LerpV3(Outer, Mid, Hot / 0.55f) : LerpV3(Mid, Peak, (Hot - 0.55f) / 0.45f);
    }
    if (Mode == VehicleDissolveMode::DissolveToButterfly)
    {
        // Electric Morpho cyan -> iridescent gold-white liberation rim
        const Vec3f Outer{ 0.20f, 0.58f, 1.55f };
        const Vec3f Mid{ 0.42f, 1.65f, 2.25f };
        const Vec3f Peak{ 2.60f, 2.45f, 2.95f };
        return (Hot < 0.55f) ? LerpV3(Outer, Mid, Hot / 0.55f) : LerpV3(Mid, Peak, (Hot - 0.55f) / 0.45f);
    }
    // DerezCubes & ReverseRezCubes: deep sapphire -> blazing cyan -> white-hot plasma rim
    const Vec3f Outer{ 0.08f, 0.62f, 1.85f };
    const Vec3f Mid{ 0.34f, 1.85f, 2.45f };
    const Vec3f Peak{ 3.30f, 3.35f, 3.45f };
    return (Hot < 0.55f) ? LerpV3(Outer, Mid, Hot / 0.55f) : LerpV3(Mid, Peak, (Hot - 0.55f) / 0.45f);
}

[[nodiscard]] inline uint8_t EncodeFilmicSrgbByte(float LinearV) noexcept
{
    const float Compressed = LinearV / (1.0f + std::max(0.0f, LinearV) * 0.78f);
    const float Gamma      = std::pow(Saturate(Compressed), 1.0f / 2.2f);
    return static_cast<uint8_t>(std::lround(Gamma * 255.0f));
}

[[nodiscard]] Vec3f RotateEuler(Vec3f P, float Rx, float Ry, float Rz) noexcept
{
    const float Cx = std::cos(Rx), Sx = std::sin(Rx);
    const float Cy = std::cos(Ry), Sy = std::sin(Ry);
    const float Cz = std::cos(Rz), Sz = std::sin(Rz);
    Vec3f A{ P.X, P.Y * Cx - P.Z * Sx, P.Y * Sx + P.Z * Cx };
    Vec3f B{ A.X * Cy + A.Z * Sy, A.Y, -A.X * Sy + A.Z * Cy };
    return { B.X * Cz - B.Y * Sz, B.X * Sz + B.Y * Cz, B.Z };
}

void RasterizeZBufferedTriangle(
    Vec3f                 W0,
    Vec3f                 W1,
    Vec3f                 W2,
    Vec3f                 N0,
    Vec3f                 N1,
    Vec3f                 N2,
    Vec3f                 FlatColor,
    bool                  UseFlatColor,
    uint8_t               SurfaceClass,
    const ControlVehicleGeometrySet* DissolveVehicle,
    const VehicleDissolveParams*     DissolveParams,
    const std::vector<EmissiveBounceLight>& BounceLights,
    const CameraBasis&    Cam,
    uint32_t              Width,
    uint32_t              Height,
    std::vector<float>&   DepthBuf,
    std::vector<Vec3f>&   ColorBuf) noexcept
{
    float Sx0 = 0.0f, Sy0 = 0.0f, Z0 = 0.0f;
    float Sx1 = 0.0f, Sy1 = 0.0f, Z1 = 0.0f;
    float Sx2 = 0.0f, Sy2 = 0.0f, Z2 = 0.0f;
    if (!ProjectWorldPoint(Cam, W0, Sx0, Sy0, Z0) ||
        !ProjectWorldPoint(Cam, W1, Sx1, Sy1, Z1) ||
        !ProjectWorldPoint(Cam, W2, Sx2, Sy2, Z2))
    {
        return;
    }

    const float Area = (Sx1 - Sx0) * (Sy2 - Sy0) - (Sx2 - Sx0) * (Sy1 - Sy0);
    if (std::fabs(Area) < 1e-4f)
    {
        return;
    }
    const float InvArea = 1.0f / Area;

    const int32_t MinX = std::max<int32_t>(0, static_cast<int32_t>(std::floor(std::min({ Sx0, Sx1, Sx2 }))));
    const int32_t MaxX = std::min<int32_t>(static_cast<int32_t>(Width) - 1, static_cast<int32_t>(std::ceil(std::max({ Sx0, Sx1, Sx2 }))));
    const int32_t MinY = std::max<int32_t>(0, static_cast<int32_t>(std::floor(std::min({ Sy0, Sy1, Sy2 }))));
    const int32_t MaxY = std::min<int32_t>(static_cast<int32_t>(Height) - 1, static_cast<int32_t>(std::ceil(std::max({ Sy0, Sy1, Sy2 }))));

    const float InvZ0 = 1.0f / Z0;
    const float InvZ1 = 1.0f / Z1;
    const float InvZ2 = 1.0f / Z2;

    for (int32_t Py = MinY; Py <= MaxY; ++Py)
    {
        const float Cy = static_cast<float>(Py) + 0.5f;
        for (int32_t Px = MinX; Px <= MaxX; ++Px)
        {
            const float Cx = static_cast<float>(Px) + 0.5f;
            const float E0 = ((Sx1 - Cx) * (Sy2 - Cy) - (Sx2 - Cx) * (Sy1 - Cy)) * InvArea;
            const float E1 = ((Sx2 - Cx) * (Sy0 - Cy) - (Sx0 - Cx) * (Sy2 - Cy)) * InvArea;
            const float E2 = 1.0f - E0 - E1;
            if (E0 < -1e-4f || E1 < -1e-4f || E2 < -1e-4f)
            {
                continue;
            }

            const float InvZ = E0 * InvZ0 + E1 * InvZ1 + E2 * InvZ2;
            if (InvZ <= 1e-6f)
            {
                continue;
            }
            const float Depth = 1.0f / InvZ;
            const std::size_t PixIdx = static_cast<std::size_t>(Py) * Width + static_cast<std::size_t>(Px);
            if (Depth >= DepthBuf[PixIdx])
            {
                continue;
            }

            const float B0 = (E0 * InvZ0) * Depth;
            const float B1 = (E1 * InvZ1) * Depth;
            const float B2 = (E2 * InvZ2) * Depth;
            const Vec3f WorldPos = W0 * B0 + W1 * B1 + W2 * B2;

            Vec3f RimEmit{};
            if (DissolveVehicle != nullptr && DissolveParams != nullptr)
            {
                const float TRelease = EvaluateVehicleDissolveReleaseTime(WorldPos.X, WorldPos.Y, WorldPos.Z, *DissolveVehicle, *DissolveParams);
                const float Clock    = Saturate(DissolveParams->Progress);
                const float RimW     = std::max(0.025f, DissolveParams->LeadingRimWidth);

                if (DissolveParams->Mode == VehicleDissolveMode::ReverseRezCubes)
                {
                    // Reverse-Rez: pixel appears once Clock >= TRelease, with glowing lock-in cyan rim right after lock
                    if (Clock < TRelease)
                    {
                        continue;
                    }
                    const float LockAge = Clock - TRelease;
                    if (LockAge < RimW)
                    {
                        const float RimProx = 1.0f - LockAge / RimW;
                        RimEmit = EvaluateLeadingRimEmission(RimProx, DissolveParams->Mode);
                    }
                }
                else
                {
                    // DerezCubes, DissolveToNothing, DissolveToButterfly: discard when Clock >= TRelease
                    if (Clock >= TRelease)
                    {
                        continue;
                    }
                    const float Remaining = TRelease - Clock;
                    if (Remaining < RimW)
                    {
                        const float RimProx = 1.0f - Remaining / RimW;
                        RimEmit = EvaluateLeadingRimEmission(RimProx, DissolveParams->Mode);
                    }
                }
            }

            DepthBuf[PixIdx] = Depth;
            if (UseFlatColor)
            {
                ColorBuf[PixIdx] = FlatColor;
            }
            else
            {
                Vec3f WorldN = Normalize3(N0 * B0 + N1 * B1 + N2 * B2);
                const Vec3f ViewDir = Normalize3(Cam.Eye - WorldPos);
                if (Dot3(WorldN, ViewDir) < 0.0f)
                {
                    WorldN = WorldN * -1.0f;
                }
                const float GiGain = (DissolveParams != nullptr) ? DissolveParams->BounceLightGain : 1.0f;
                const bool  GiOn   = (DissolveParams != nullptr) ? DissolveParams->EnableTwoWayGi : true;
                ColorBuf[PixIdx] = EvaluateStudioPbrSurface(WorldPos, WorldN, ViewDir, SurfaceClass, BounceLights, GiGain, GiOn) + RimEmit;
            }
        }
    }
}

void Rasterize3DCubePiece(
    const VehicleDissolveCubePiece&         Cube,
    VehicleDissolveMode                     Mode,
    const std::vector<EmissiveBounceLight>& BounceLights,
    const CameraBasis&                      Cam,
    uint32_t                                Width,
    uint32_t                                Height,
    std::vector<float>&                     DepthBuf,
    std::vector<Vec3f>&                     ColorBuf) noexcept
{
    static constexpr std::array<Vec3f, 8> kUnitCorners = {{
        { -1.0f, -1.0f, -1.0f }, {  1.0f, -1.0f, -1.0f }, {  1.0f,  1.0f, -1.0f }, { -1.0f,  1.0f, -1.0f },
        { -1.0f, -1.0f,  1.0f }, {  1.0f, -1.0f,  1.0f }, {  1.0f,  1.0f,  1.0f }, { -1.0f,  1.0f,  1.0f },
    }};
    static constexpr std::array<std::array<uint8_t, 4>, 6> kFaceQuads = {{
        { 4, 5, 6, 7 }, { 1, 0, 3, 2 }, { 5, 1, 2, 6 },
        { 0, 4, 7, 3 }, { 7, 6, 2, 3 }, { 0, 1, 5, 4 },
    }};
    static constexpr std::array<Vec3f, 6> kFaceNormals = {{
        {  0.0f,  0.0f,  1.0f }, {  0.0f,  0.0f, -1.0f }, {  1.0f,  0.0f,  0.0f },
        { -1.0f,  0.0f,  0.0f }, {  0.0f,  1.0f,  0.0f }, {  0.0f, -1.0f,  0.0f },
    }};

    const Vec3f Center{ Cube.PosX, Cube.PosY, Cube.PosZ };
    std::array<Vec3f, 8> WorldVerts{};
    for (std::size_t I = 0u; I < 8u; ++I)
    {
        const Vec3f Local = kUnitCorners[I] * Cube.HalfExtent;
        WorldVerts[I] = Center + RotateEuler(Local, Cube.RotX, Cube.RotY, Cube.RotZ);
    }

    const Vec3f ViewDir = Normalize3(Cam.Eye - Center);
    const Vec3f Glow    = EvaluateLeadingRimEmission(Cube.GlowHeat, Mode) * (0.20f + 0.95f * Cube.GlowHeat * Cube.GlowHeat);

    for (std::size_t F = 0u; F < 6u; ++F)
    {
        const Vec3f WN = Normalize3(RotateEuler(kFaceNormals[F], Cube.RotX, Cube.RotY, Cube.RotZ));
        if (Dot3(WN, ViewDir) <= -0.08f)
        {
            continue;
        }
        const Vec3f FaceAlbedoLit = EvaluateStudioPbrSurface(Center, WN, ViewDir, Cube.MaterialSlot, BounceLights, 0.85f, true);
        const float RimFactor     = 0.25f + 0.75f * std::pow(1.0f - std::max(0.0f, Dot3(WN, ViewDir)), 1.6f);
        const Vec3f Shade         = FaceAlbedoLit + Glow * (0.32f + 0.68f * RimFactor);
        const auto& Q        = kFaceQuads[F];
        RasterizeZBufferedTriangle(
            WorldVerts[Q[0]], WorldVerts[Q[1]], WorldVerts[Q[2]],
            WN, WN, WN, Shade, true, Cube.MaterialSlot, nullptr, nullptr, BounceLights,
            Cam, Width, Height, DepthBuf, ColorBuf);
        RasterizeZBufferedTriangle(
            WorldVerts[Q[0]], WorldVerts[Q[2]], WorldVerts[Q[3]],
            WN, WN, WN, Shade, true, Cube.MaterialSlot, nullptr, nullptr, BounceLights,
            Cam, Width, Height, DepthBuf, ColorBuf);
    }
}

void RasterizeArticulatedButterfly(
    const VehicleDissolveButterflyPiece& B,
    const ProceduralSpriteSpec&          MorphoSpec,
    const ProceduralSpriteSpec&          MonarchSpec,
    const CameraBasis&                   Cam,
    uint32_t                             Width,
    uint32_t                             Height,
    std::vector<float>&                  DepthBuf,
    std::vector<Vec3f>&                  ColorBuf) noexcept
{
    const Vec3f Center{ B.PosX, B.PosY, B.PosZ };
    const float Cy = std::cos(B.HeadingYaw), Sy = std::sin(B.HeadingYaw);
    const float Cp = std::cos(B.PitchTilt),  Sp = std::sin(B.PitchTilt);
    const Vec3f Fwd{ Sy * Cp, Sp, Cy * Cp };
    const Vec3f RightAxis = Normalize3(Cross3(Fwd, Vec3f{ 0.0f, 1.0f, 0.0f }));
    const Vec3f UpAxis    = Normalize3(Cross3(RightAxis, Fwd));

    const float HalfSpan = B.WingSpan * 0.55f;
    const float Chord    = B.WingSpan * 0.72f;
    const float FlapCos  = std::cos(B.FlapAngle);
    const float FlapSin  = std::sin(B.FlapAngle);

    // Rasterize articulated Left (Side = -1) and Right (Side = +1) textured 3D wings
    for (int Side = -1; Side <= 1; Side += 2)
    {
        const float S = static_cast<float>(Side);
        const Vec3f WingOut = Normalize3(RightAxis * (S * FlapCos) + UpAxis * FlapSin);

        const Vec3f P00 = Center - Fwd * (Chord * 0.52f);                           // Hinge trailing
        const Vec3f P01 = Center + Fwd * (Chord * 0.52f);                           // Hinge leading
        const Vec3f P11 = Center + Fwd * (Chord * 0.58f) + WingOut * HalfSpan;      // Outer forewing tip
        const Vec3f P10 = Center - Fwd * (Chord * 0.50f) + WingOut * (HalfSpan * 0.88f); // Outer hindwing tip

        // Project all 4 quad corners and sample procedural butterfly wing sprite inside the 2D triangle pair
        const std::array<std::array<Vec3f, 3>, 2> Tris = {{
            { P00, P01, P11 },
            { P00, P11, P10 },
        }};
        const std::array<std::array<std::array<float, 2>, 3>, 2> Uvs = {{
            {{ { 0.0f, 1.0f }, { 0.0f, -1.0f }, { S, -1.0f } }},
            {{ { 0.0f, 1.0f }, { S, -1.0f },    { S,  1.0f } }},
        }};

        for (std::size_t T = 0u; T < 2u; ++T)
        {
            float Sx0 = 0.0f, Sy0 = 0.0f, Z0 = 0.0f;
            float Sx1 = 0.0f, Sy1 = 0.0f, Z1 = 0.0f;
            float Sx2 = 0.0f, Sy2 = 0.0f, Z2 = 0.0f;
            if (!ProjectWorldPoint(Cam, Tris[T][0], Sx0, Sy0, Z0) ||
                !ProjectWorldPoint(Cam, Tris[T][1], Sx1, Sy1, Z1) ||
                !ProjectWorldPoint(Cam, Tris[T][2], Sx2, Sy2, Z2))
            {
                continue;
            }
            const float Area = (Sx1 - Sx0) * (Sy2 - Sy0) - (Sx2 - Sx0) * (Sy1 - Sy0);
            if (std::fabs(Area) < 1e-4f)
            {
                continue;
            }
            const float InvArea = 1.0f / Area;
            const int32_t MinX = std::max<int32_t>(0, static_cast<int32_t>(std::floor(std::min({ Sx0, Sx1, Sx2 }))));
            const int32_t MaxX = std::min<int32_t>(static_cast<int32_t>(Width) - 1, static_cast<int32_t>(std::ceil(std::max({ Sx0, Sx1, Sx2 }))));
            const int32_t MinY = std::max<int32_t>(0, static_cast<int32_t>(std::floor(std::min({ Sy0, Sy1, Sy2 }))));
            const int32_t MaxY = std::min<int32_t>(static_cast<int32_t>(Height) - 1, static_cast<int32_t>(std::ceil(std::max({ Sy0, Sy1, Sy2 }))));

            const ProceduralSpriteSpec& ActiveSpec = (B.MorphoMix >= 0.5f) ? MorphoSpec : MonarchSpec;
            for (int32_t Py = MinY; Py <= MaxY; ++Py)
            {
                const float CyP = static_cast<float>(Py) + 0.5f;
                for (int32_t Px = MinX; Px <= MaxX; ++Px)
                {
                    const float CxP = static_cast<float>(Px) + 0.5f;
                    const float E0 = ((Sx1 - CxP) * (Sy2 - CyP) - (Sx2 - CxP) * (Sy1 - CyP)) * InvArea;
                    const float E1 = ((Sx2 - CxP) * (Sy0 - CyP) - (Sx0 - CxP) * (Sy2 - CyP)) * InvArea;
                    const float E2 = 1.0f - E0 - E1;
                    if (E0 < -1e-4f || E1 < -1e-4f || E2 < -1e-4f)
                    {
                        continue;
                    }
                    const float Depth = E0 * Z0 + E1 * Z1 + E2 * Z2;
                    const std::size_t PixIdx = static_cast<std::size_t>(Py) * Width + static_cast<std::size_t>(Px);
                    if (Depth >= DepthBuf[PixIdx])
                    {
                        continue;
                    }
                    const float U = E0 * Uvs[T][0][0] + E1 * Uvs[T][1][0] + E2 * Uvs[T][2][0];
                    const float V = E0 * Uvs[T][0][1] + E1 * Uvs[T][1][1] + E2 * Uvs[T][2][1];
                    const ProceduralSpriteSample WingSample = SampleProceduralSpriteUnit(ActiveSpec, U, V, 9103u);
                    if (WingSample.Alpha < 0.28f)
                    {
                        continue;
                    }
                    DepthBuf[PixIdx] = Depth;
                    const float EmissiveBoost = 0.85f + 1.25f * WingSample.Glint * B.Shimmer;
                    ColorBuf[PixIdx] = Vec3f{ WingSample.Red, WingSample.Green, WingSample.Blue } * EmissiveBoost;
                }
            }
        }
    }
}

void RasterizeDepthTestedVaporBillboard(
    const VehicleDissolveVaporPiece& V,
    const CameraBasis&               Cam,
    uint32_t                         Width,
    uint32_t                         Height,
    const std::vector<float>&        DepthBuf,
    std::vector<Vec3f>&              ColorBuf) noexcept
{
    float Sx = 0.0f, Sy = 0.0f, Depth = 0.0f;
    if (!ProjectWorldPoint(Cam, { V.PosX, V.PosY, V.PosZ }, Sx, Sy, Depth))
    {
        return;
    }
    const float RadiusPx = std::clamp((V.Radius * Cam.HeightF) / (2.0f * Depth * Cam.TanHalfFovY), 1.5f, 34.0f);
    const int32_t MinX = std::max<int32_t>(0, static_cast<int32_t>(std::floor(Sx - RadiusPx)));
    const int32_t MaxX = std::min<int32_t>(static_cast<int32_t>(Width) - 1, static_cast<int32_t>(std::ceil(Sx + RadiusPx)));
    const int32_t MinY = std::max<int32_t>(0, static_cast<int32_t>(std::floor(Sy - RadiusPx)));
    const int32_t MaxY = std::min<int32_t>(static_cast<int32_t>(Height) - 1, static_cast<int32_t>(std::ceil(Sy + RadiusPx)));

    const Vec3f EmberCol = LerpV3(Vec3f{ 0.36f, 0.52f, 0.68f }, Vec3f{ 2.65f, 1.38f, 0.34f }, V.Heat);
    for (int32_t Py = MinY; Py <= MaxY; ++Py)
    {
        for (int32_t Px = MinX; Px <= MaxX; ++Px)
        {
            const std::size_t PixIdx = static_cast<std::size_t>(Py) * Width + static_cast<std::size_t>(Px);
            // Strict Z-buffer occlusion behind intact car bodywork
            if (Depth >= DepthBuf[PixIdx] + 0.02f)
            {
                continue;
            }
            const float Dx = (static_cast<float>(Px) + 0.5f - Sx) / RadiusPx;
            const float Dy = (static_cast<float>(Py) + 0.5f - Sy) / RadiusPx;
            const float R2 = Dx * Dx + Dy * Dy;
            if (R2 >= 1.0f)
            {
                continue;
            }
            const float Falloff = std::exp(-R2 * (V.Heat > 0.45f ? 3.8f : 2.2f)) * (1.0f - R2);
            const float Alpha   = Saturate(Falloff * V.Opacity);
            if (V.Heat > 0.35f)
            {
                ColorBuf[PixIdx] = ColorBuf[PixIdx] + EmberCol * Alpha;
            }
            else
            {
                ColorBuf[PixIdx] = LerpV3(ColorBuf[PixIdx], EmberCol, Alpha * 0.55f);
            }
        }
    }
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

float EvaluateVehicleDissolveReleaseTime(
    float                            WorldX,
    float                            WorldY,
    float                            WorldZ,
    const ControlVehicleGeometrySet& Vehicle,
    const VehicleDissolveParams&     Params) noexcept
{
    const uint32_t Grid = std::clamp<uint32_t>(Params.LatticeResolution, 4u, 32u);
    const float SpanX = std::max(0.05f, Vehicle.BoundsMax[0] - Vehicle.BoundsMin[0]);
    const float SpanY = std::max(0.05f, Vehicle.BoundsMax[1] - Vehicle.BoundsMin[1]);
    const float SpanZ = std::max(0.05f, Vehicle.BoundsMax[2] - Vehicle.BoundsMin[2]);

    const float Ux = Saturate((WorldX - Vehicle.BoundsMin[0]) / SpanX);
    const float Uy = Saturate((WorldY - Vehicle.BoundsMin[1]) / SpanY);
    const float Uz = Saturate((WorldZ - Vehicle.BoundsMin[2]) / SpanZ);

    const int32_t Ix = std::min<int32_t>(static_cast<int32_t>(Grid) - 1, static_cast<int32_t>(Ux * static_cast<float>(Grid)));
    const int32_t Iy = std::min<int32_t>(static_cast<int32_t>(Grid) - 1, static_cast<int32_t>(Uy * static_cast<float>(Grid)));
    const int32_t Iz = std::min<int32_t>(static_cast<int32_t>(Grid) - 1, static_cast<int32_t>(Uz * static_cast<float>(Grid)));

    // ControlVehicle studio axis: +X is front nose, -X is rear tail, +Y is roof up
    const float CellX = (static_cast<float>(Ix) + 0.5f) / static_cast<float>(Grid);
    const float CellY = (static_cast<float>(Iy) + 0.5f) / static_cast<float>(Grid);
    const float Jitter = HashUnit(HashLatticeCell(Ix, Iy, Iz, Params.Seed)) * 0.145f;

    if (Params.Mode == VehicleDissolveMode::DissolveToNothing)
    {
        const float Sweep = (1.0f - Ux) * 0.72f + (1.0f - Uy) * 0.12f;
        return std::clamp(0.04f + Sweep + Jitter * 0.65f, 0.03f, 0.94f);
    }

    const float SweepWave = (1.0f - CellX) * 0.68f + (1.0f - CellY) * 0.14f;
    return std::clamp(0.05f + SweepWave + Jitter, 0.04f, 0.94f);
}

VehicleDissolveSnapshot BuildVehicleDissolveSnapshot(
    const ControlVehicleGeometrySet& Vehicle,
    const VehicleDissolveParams&     Params)
{
    VehicleDissolveSnapshot Snap{};
    Snap.Mode  = Params.Mode;
    Snap.Clock = Saturate(Params.Progress);

    if (Vehicle.Triangles.empty())
    {
        return Snap;
    }

    const uint32_t Grid = std::clamp<uint32_t>(Params.LatticeResolution, 4u, 32u);
    const float SpanX = std::max(0.05f, Vehicle.BoundsMax[0] - Vehicle.BoundsMin[0]);
    const float SpanY = std::max(0.05f, Vehicle.BoundsMax[1] - Vehicle.BoundsMin[1]);
    const float SpanZ = std::max(0.05f, Vehicle.BoundsMax[2] - Vehicle.BoundsMin[2]);

    struct LatticeCellAccumulator
    {
        Vec3f    PosSum{};
        Vec3f    NormalSum{};
        uint32_t SampleCount  = 0u;
        uint8_t  MaterialSlot = 6u;
        int32_t  Ix           = 0;
        int32_t  Iy           = 0;
        int32_t  Iz           = 0;
    };

    std::unordered_map<uint32_t, LatticeCellAccumulator> OccupiedCells;
    OccupiedCells.reserve(1600u);

    for (const ControlVehicleTriangle& Tri : Vehicle.Triangles)
    {
        const float Cx = (Tri.PositionA[0] + Tri.PositionB[0] + Tri.PositionC[0]) * (1.0f / 3.0f);
        const float Cy = (Tri.PositionA[1] + Tri.PositionB[1] + Tri.PositionC[1]) * (1.0f / 3.0f);
        const float Cz = (Tri.PositionA[2] + Tri.PositionB[2] + Tri.PositionC[2]) * (1.0f / 3.0f);

        const float Ux = Saturate((Cx - Vehicle.BoundsMin[0]) / SpanX);
        const float Uy = Saturate((Cy - Vehicle.BoundsMin[1]) / SpanY);
        const float Uz = Saturate((Cz - Vehicle.BoundsMin[2]) / SpanZ);

        const int32_t Ix = std::min<int32_t>(static_cast<int32_t>(Grid) - 1, static_cast<int32_t>(Ux * static_cast<float>(Grid)));
        const int32_t Iy = std::min<int32_t>(static_cast<int32_t>(Grid) - 1, static_cast<int32_t>(Uy * static_cast<float>(Grid)));
        const int32_t Iz = std::min<int32_t>(static_cast<int32_t>(Grid) - 1, static_cast<int32_t>(Uz * static_cast<float>(Grid)));

        const uint32_t Key = static_cast<uint32_t>(Ix) | (static_cast<uint32_t>(Iy) << 6u) | (static_cast<uint32_t>(Iz) << 12u);
        LatticeCellAccumulator& Cell = OccupiedCells[Key];
        Cell.PosSum    = Cell.PosSum + Vec3f{ Cx, Cy, Cz };
        Cell.NormalSum = Cell.NormalSum + Vec3f{
            Tri.NormalA[0] + Tri.NormalB[0] + Tri.NormalC[0],
            Tri.NormalA[1] + Tri.NormalB[1] + Tri.NormalC[1],
            Tri.NormalA[2] + Tri.NormalB[2] + Tri.NormalC[2],
        };
        if (Cell.SampleCount == 0u || Tri.MaterialSlotCode == 6u)
        {
            Cell.MaterialSlot = static_cast<uint8_t>(Tri.MaterialSlotCode);
        }
        Cell.Ix = Ix;
        Cell.Iy = Iy;
        Cell.Iz = Iz;
        ++Cell.SampleCount;
    }

    const float CubeHalf = std::clamp(std::min({ SpanX, SpanY, SpanZ }) / (static_cast<float>(Grid) * 2.55f), 0.028f, 0.046f);

    for (const auto& Entry : OccupiedCells)
    {
        const LatticeCellAccumulator& Cell = Entry.second;
        const float InvN = 1.0f / static_cast<float>(std::max<uint32_t>(1u, Cell.SampleCount));
        const Vec3f AnchorPos = Cell.PosSum * InvN;
        const Vec3f AnchorN   = Normalize3(Cell.NormalSum);

        const float TRelease = EvaluateVehicleDissolveReleaseTime(AnchorPos.X, AnchorPos.Y, AnchorPos.Z, Vehicle, Params);
        const uint32_t H0    = HashLatticeCell(Cell.Ix, Cell.Iy, Cell.Iz, Params.Seed);
        const float R1       = HashUnit(H0 + 11u);
        const float R2       = HashUnit(H0 + 29u);
        const float R3       = HashUnit(H0 + 53u);
        const float R4       = HashUnit(H0 + 89u);

        if (Params.Mode == VehicleDissolveMode::DerezCubes)
        {
            if (Snap.Clock < TRelease)
            {
                ++Snap.IntactAnchorCount;
                continue;
            }
            ++Snap.ReleasedCellCount;
            const float AgeSec = (Snap.Clock - TRelease) * 2.45f;
            const Vec3f InitVel =
                AnchorN * (0.75f + 1.35f * R1) +
                Vec3f{ (R2 - 0.5f) * 1.45f, 0.85f + 1.55f * R3, 0.55f + (R4 - 0.5f) * 1.25f };

            Vec3f Pos{};
            Vec3f Vel{};
            EvaluateLinearDragBallistic(AnchorPos, InitVel, Params.GravityY, Params.LinearDrag, AgeSec, Pos, Vel);

            if (Pos.Y > CubeHalf)
            {
                VehicleDissolveCubePiece Piece{};
                Piece.PosX         = Pos.X;
                Piece.PosY         = Pos.Y;
                Piece.PosZ         = Pos.Z;
                Piece.VelX         = Vel.X;
                Piece.VelY         = Vel.Y;
                Piece.VelZ         = Vel.Z;
                Piece.HalfExtent   = CubeHalf * (0.88f + 0.24f * R2);
                Piece.RotX         = AgeSec * (2.6f + 3.8f * R1);
                Piece.RotY         = AgeSec * (2.2f + 4.1f * R3);
                Piece.RotZ         = AgeSec * (1.8f + 3.4f * R4);
                Piece.GlowHeat     = std::exp(-AgeSec * 2.25f);
                Piece.MaterialSlot = Cell.MaterialSlot;
                Piece.IsSubShatter = 0u;
                Snap.Cubes.push_back(Piece);
            }
            else
            {
                // Floor impact: binary search impact time and shatter into 8 tumbling half-scale sub-cubes!
                float Lo = 0.0f;
                float Hi = AgeSec;
                for (int It = 0; It < 9; ++It)
                {
                    const float Mid = 0.5f * (Lo + Hi);
                    Vec3f Mp{}, Mv{};
                    EvaluateLinearDragBallistic(AnchorPos, InitVel, Params.GravityY, Params.LinearDrag, Mid, Mp, Mv);
                    if (Mp.Y > CubeHalf)
                    {
                        Lo = Mid;
                    }
                    else
                    {
                        Hi = Mid;
                    }
                }
                Vec3f ImpactPos{}, ImpactVel{};
                EvaluateLinearDragBallistic(AnchorPos, InitVel, Params.GravityY, Params.LinearDrag, Hi, ImpactPos, ImpactVel);
                ImpactPos.Y = std::max(CubeHalf * 0.5f, ImpactPos.Y);

                const float PostImpactSec = std::max(0.0f, AgeSec - Hi);
                const float SubHalf       = CubeHalf * 0.48f;
                ++Snap.ShatteredCubeCount;

                for (uint32_t Sub = 0u; Sub < 8u; ++Sub)
                {
                    const float Sx = (Sub & 1u) ? 1.0f : -1.0f;
                    const float Sy = (Sub & 2u) ? 1.0f : -1.0f;
                    const float Sz = (Sub & 4u) ? 1.0f : -1.0f;
                    const float Sr = HashUnit(H0 + Sub * 131u + 7u);

                    const Vec3f SubStart = ImpactPos + Vec3f{ Sx * SubHalf, std::max(0.0f, Sy * SubHalf), Sz * SubHalf };
                    const Vec3f SubVel{
                        ImpactVel.X * 0.55f + Sx * (0.65f + 0.85f * Sr),
                        std::fabs(ImpactVel.Y) * Params.ShatterRestitution * (0.65f + 0.55f * Sr) + 0.55f,
                        ImpactVel.Z * 0.55f + Sz * (0.65f + 0.85f * Sr),
                    };

                    Vec3f SubPos{}, SubNowVel{};
                    EvaluateLinearDragBallistic(SubStart, SubVel, Params.GravityY, Params.LinearDrag * 1.4f, PostImpactSec, SubPos, SubNowVel);
                    if (SubPos.Y < SubHalf)
                    {
                        SubPos.Y = SubHalf;
                    }

                    VehicleDissolveCubePiece SubPiece{};
                    SubPiece.PosX         = SubPos.X;
                    SubPiece.PosY         = SubPos.Y;
                    SubPiece.PosZ         = SubPos.Z;
                    SubPiece.VelX         = SubNowVel.X;
                    SubPiece.VelY         = SubNowVel.Y;
                    SubPiece.VelZ         = SubNowVel.Z;
                    SubPiece.HalfExtent   = SubHalf;
                    SubPiece.RotX         = (AgeSec + PostImpactSec) * (4.2f + 4.5f * Sr) * Sx;
                    SubPiece.RotY         = (AgeSec + PostImpactSec) * (3.6f + 4.1f * Sr) * Sy;
                    SubPiece.RotZ         = (AgeSec + PostImpactSec) * (3.1f + 3.8f * Sr) * Sz;
                    SubPiece.GlowHeat     = std::exp(-AgeSec * 2.40f) * 0.62f;
                    SubPiece.MaterialSlot = Cell.MaterialSlot;
                    SubPiece.IsSubShatter = 1u;
                    Snap.Cubes.push_back(SubPiece);
                }
            }
        }
        else if (Params.Mode == VehicleDissolveMode::ReverseRezCubes)
        {
            if (Snap.Clock >= TRelease)
            {
                ++Snap.IntactAnchorCount;
                continue;
            }
            ++Snap.ReleasedCellCount;
            const float LeadWindow = 0.42f;
            const float Remaining  = TRelease - Snap.Clock;
            if (Remaining > LeadWindow)
            {
                continue;
            }
            // U = 0 at far outer spiral shell, U = 1 when locked cleanly onto ControlVehicle surface
            const float U     = 1.0f - Saturate(Remaining / LeadWindow);
            const float Ease  = SmoothStep(0.0f, 1.0f, U);
            const float InvE  = 1.0f - Ease;
            const float SpiralAngle = InvE * (1.85f + 1.65f * R1) * ((R2 > 0.5f) ? 1.0f : -1.0f);
            const Vec3f RadialOut   = Normalize3(AnchorPos - Vec3f{ 0.0f, 0.72f, 0.0f });
            const Vec3f ShellOffset = RadialOut * (1.65f + 1.45f * R3) + Vec3f{ 0.0f, 0.85f + 0.95f * R4, 0.0f };
            const Vec3f RotOffset{
                ShellOffset.X * std::cos(SpiralAngle) - ShellOffset.Z * std::sin(SpiralAngle),
                ShellOffset.Y,
                ShellOffset.X * std::sin(SpiralAngle) + ShellOffset.Z * std::cos(SpiralAngle),
            };
            const Vec3f CurPos = AnchorPos + RotOffset * InvE;

            VehicleDissolveCubePiece Piece{};
            Piece.PosX         = CurPos.X;
            Piece.PosY         = std::max(CubeHalf, CurPos.Y);
            Piece.PosZ         = CurPos.Z;
            Piece.HalfExtent   = CubeHalf * LerpF(0.68f, 1.04f, Ease);
            Piece.RotX         = InvE * (4.2f + 2.8f * R1);
            Piece.RotY         = InvE * (3.6f + 3.1f * R2);
            Piece.RotZ         = InvE * (2.9f + 2.4f * R3);
            Piece.GlowHeat     = Saturate(0.35f + 0.65f * Ease * Ease);
            Piece.MaterialSlot = Cell.MaterialSlot;
            Snap.Cubes.push_back(Piece);
        }
        else if (Params.Mode == VehicleDissolveMode::DissolveToNothing)
        {
            if (Snap.Clock < TRelease)
            {
                ++Snap.IntactAnchorCount;
                continue;
            }
            ++Snap.ReleasedCellCount;
            const float AgeNorm = (Snap.Clock - TRelease) / 0.34f;
            if (AgeNorm > 1.15f)
            {
                continue; // Completely vaporized into thin air
            }
            // Spawn 2 buoyant curling wisps/embers per recently vaporized cell
            for (int K = 0; K < 2; ++K)
            {
                const float Kr  = HashUnit(H0 + static_cast<uint32_t>(K * 73 + 19));
                const float Kr2 = HashUnit(H0 + static_cast<uint32_t>(K * 97 + 41));
                const float LocalAge = AgeNorm + static_cast<float>(K) * 0.08f;
                if (LocalAge > 1.0f)
                {
                    continue;
                }
                const float DriftX = std::sin(LocalAge * 7.0f + Kr * kTwoPi) * 0.36f * LocalAge;
                const float DriftZ = std::cos(LocalAge * 6.0f + Kr2 * kTwoPi) * 0.32f * LocalAge + LocalAge * 0.42f;
                const float RiseY  = LocalAge * (1.15f + 1.35f * Kr) + LocalAge * LocalAge * 0.65f;

                VehicleDissolveVaporPiece V{};
                V.PosX    = AnchorPos.X + AnchorN.X * (0.08f + 0.25f * LocalAge) + DriftX;
                V.PosY    = AnchorPos.Y + RiseY;
                V.PosZ    = AnchorPos.Z + AnchorN.Z * (0.08f + 0.25f * LocalAge) + DriftZ;
                V.Radius  = (K == 0) ? LerpF(0.035f, 0.012f, LocalAge) : LerpF(0.065f, 0.165f, LocalAge);
                V.Heat    = (K == 0) ? Saturate(1.0f - LocalAge * 1.1f) : Saturate(0.35f - LocalAge * 0.45f);
                V.Opacity = Saturate((1.0f - LocalAge) * (1.0f - LocalAge) * (K == 0 ? 1.15f : 0.62f));
                Snap.Vapors.push_back(V);
            }
        }
        else // VehicleDissolveMode::DissolveToButterfly
        {
            if (Snap.Clock < TRelease)
            {
                ++Snap.IntactAnchorCount;
                continue;
            }
            ++Snap.ReleasedCellCount;
            // Sub-sample ~42% of lattice cells so the swarm reads as distinct, graceful, non-cluttered 3D butterflies
            if (R1 > 0.44f)
            {
                continue;
            }
            const float AgeSec = (Snap.Clock - TRelease) * 2.55f;
            const float FlapFreq = 12.5f + 5.5f * R2;
            const float Spiral   = AgeSec * (1.15f + 0.85f * R3) + R4 * kTwoPi;
            const float LiftY    = AgeSec * (0.95f + 1.15f * R2) + 0.12f * std::sin(AgeSec * 5.2f + R1 * kTwoPi);
            const float SweepX   = AnchorN.X * (0.35f + 0.85f * AgeSec) + std::cos(Spiral) * (0.28f + 0.55f * AgeSec);
            const float SweepZ   = AnchorN.Z * (0.35f + 0.70f * AgeSec) + std::sin(Spiral) * (0.28f + 0.45f * AgeSec) + 0.45f * AgeSec;

            VehicleDissolveButterflyPiece B{};
            B.PosX       = AnchorPos.X + SweepX;
            B.PosY       = std::max(0.14f, AnchorPos.Y + LiftY);
            B.PosZ       = AnchorPos.Z + SweepZ;
            B.HeadingYaw = std::atan2(SweepX + 0.001f, SweepZ + 0.001f);
            B.PitchTilt  = 0.24f + 0.18f * std::sin(AgeSec * 4.0f + R3 * kTwoPi);
            B.WingSpan   = LerpF(0.16f, 0.28f, R4) * SmoothStep(0.0f, 0.14f, AgeSec);
            B.FlapAngle  = 0.18f + 0.92f * std::sin(AgeSec * FlapFreq + R4 * kTwoPi);
            B.MorphoMix  = (R3 < 0.72f) ? 1.0f : 0.0f; // 72% Electric Morpho Cyan, 28% Golden Monarch
            B.Shimmer    = Saturate(0.75f + 0.45f * std::exp(-AgeSec * 0.55f));
            B.Opacity    = 1.0f;
            Snap.Butterflies.push_back(B);
        }
    }

    return Snap;
}

void RenderVehicleDissolveStudioRgba8(
    const ControlVehicleGeometrySet&   Vehicle,
    const VehicleDissolveParams&       Params,
    const VehicleDissolveSnapshot&     Snapshot,
    const VehicleDissolveRenderCamera& Camera,
    uint32_t                           Width,
    uint32_t                           Height,
    std::vector<uint8_t>&              OutRgba8)
{
    const std::size_t PixelCount = static_cast<std::size_t>(Width) * Height;
    OutRgba8.assign(PixelCount * 4u, 255u);
    std::vector<float> DepthBuf(PixelCount, std::numeric_limits<float>::infinity());
    std::vector<Vec3f> ColorBuf(PixelCount, Vec3f{ 0.0f, 0.0f, 0.0f });

    const CameraBasis Cam = BuildCameraBasis(Camera, Width, Height);

    // 1. Collect Two-Way GI Emissive Bounce Lights from active glowing cubes, leading rim band, and butterflies
    std::vector<EmissiveBounceLight> BounceLights;
    if (Params.EnableTwoWayGi)
    {
        BounceLights.reserve(40u);
        // Leading-edge dissolve wavefront anchor lights along the active +X -> -X plane
        const float WaveX = LerpF(Vehicle.BoundsMax[0], Vehicle.BoundsMin[0], Saturate(Params.Progress * 1.15f));
        const Vec3f WaveCol = EvaluateLeadingRimEmission(0.78f, Params.Mode) * 0.55f;
        if (Params.Progress > 0.04f && Params.Progress < 0.96f)
        {
            BounceLights.push_back({ { WaveX, 0.65f, -0.85f }, WaveCol, 2.8f });
            BounceLights.push_back({ { WaveX, 1.05f,  0.00f }, WaveCol * 1.15f, 3.1f });
            BounceLights.push_back({ { WaveX, 0.65f,  0.85f }, WaveCol, 2.8f });
        }

        const std::size_t CubeStride = std::max<std::size_t>(1u, Snapshot.Cubes.size() / 20u);
        for (std::size_t I = 0u; I < Snapshot.Cubes.size(); I += CubeStride)
        {
            const VehicleDissolveCubePiece& C = Snapshot.Cubes[I];
            if (C.GlowHeat > 0.08f)
            {
                BounceLights.push_back({
                    { C.PosX, C.PosY, C.PosZ },
                    EvaluateLeadingRimEmission(C.GlowHeat, Params.Mode) * (0.28f * C.GlowHeat),
                    2.1f,
                });
            }
        }

        const std::size_t ButterStride = std::max<std::size_t>(1u, Snapshot.Butterflies.size() / 14u);
        for (std::size_t I = 0u; I < Snapshot.Butterflies.size(); I += ButterStride)
        {
            const VehicleDissolveButterflyPiece& B = Snapshot.Butterflies[I];
            const Vec3f BCol = (B.MorphoMix >= 0.5f)
                ? Vec3f{ 0.16f, 0.85f, 1.35f }
                : Vec3f{ 1.25f, 0.72f, 0.16f };
            BounceLights.push_back({ { B.PosX, B.PosY, B.PosZ }, BCol * 0.32f, 2.2f });
        }
    }

    // 2. Render seamless dark studio cyclorama backdrop + reflective Y=0 studio floor with contact AO and GI bounce
    for (uint32_t Py = 0u; Py < Height; ++Py)
    {
        const float NdcY = 1.0f - ((static_cast<float>(Py) + 0.5f) / static_cast<float>(Height)) * 2.0f;
        for (uint32_t Px = 0u; Px < Width; ++Px)
        {
            const float NdcX = ((static_cast<float>(Px) + 0.5f) / static_cast<float>(Width)) * 2.0f - 1.0f;
            const Vec3f RayDir = Normalize3(
                Cam.Forward +
                Cam.Right * (NdcX * Cam.TanHalfFovY * Cam.Aspect) +
                Cam.Up * (NdcY * Cam.TanHalfFovY));

            const std::size_t PixIdx = static_cast<std::size_t>(Py) * Width + static_cast<std::size_t>(Px);
            const float Vignette = Saturate(1.0f - 0.34f * (NdcX * NdcX + NdcY * NdcY));
            Vec3f Bg = LerpV3(Vec3f{ 0.014f, 0.017f, 0.024f }, Vec3f{ 0.036f, 0.044f, 0.058f }, Saturate(0.5f + NdcY * 0.65f)) * Vignette;

            if (RayDir.Y < -0.001f && Cam.Eye.Y > 0.001f)
            {
                const float TFloor = -Cam.Eye.Y / RayDir.Y;
                if (TFloor > 0.1f && TFloor < 42.0f)
                {
                    const Vec3f Hit = Cam.Eye + RayDir * TFloor;
                    const float DistR = std::hypot(Hit.X, Hit.Z);
                    const float Fade  = SmoothStep(15.5f, 2.5f, DistR);

                    // Subtle studio hairline grid + soft car footprint AO shadow
                    const float Gx = std::fabs(std::fmod(std::fabs(Hit.X) + 0.5f, 1.0f) - 0.5f);
                    const float Gz = std::fabs(std::fmod(std::fabs(Hit.Z) + 0.5f, 1.0f) - 0.5f);
                    const float GridLine = (1.0f - SmoothStep(0.008f, 0.022f, std::min(Gx, Gz))) * 0.055f * Fade;

                    const float CarFootX = std::fabs(Hit.X) / 3.15f;
                    const float CarFootZ = std::fabs(Hit.Z) / 1.32f;
                    const float InFoot   = SmoothStep(1.15f, 0.45f, std::max(CarFootX, CarFootZ));
                    const float IntactRatio = (Params.Mode == VehicleDissolveMode::ReverseRezCubes)
                        ? Saturate(Params.Progress)
                        : Saturate(1.0f - Params.Progress * 0.85f);
                    const float ShadowAO = 1.0f - 0.72f * InFoot * IntactRatio;

                    Vec3f FloorCol = (Vec3f{ 0.024f, 0.028f, 0.036f } + Vec3f{ 0.12f, 0.16f, 0.22f } * GridLine) * ShadowAO;
                    if (Params.EnableTwoWayGi && !BounceLights.empty())
                    {
                        const Vec3f GiFloor = EvaluateBounceLights(Hit, Vec3f{ 0.0f, 1.0f, 0.0f }, BounceLights, Params.BounceLightGain * 0.95f);
                        FloorCol = FloorCol + GiFloor * 0.34f;
                    }
                    Bg = LerpV3(Bg, FloorCol, Fade);
                    DepthBuf[PixIdx] = TFloor;
                }
            }

            ColorBuf[PixIdx] = Bg;
        }
    }

    // 3. Rasterize intact 25,068-triangle Controlled Integrator (ControlVehicle) with per-pixel 16^3 lattice discard + blazing leading-edge rim
    for (const ControlVehicleTriangle& Tri : Vehicle.Triangles)
    {
        RasterizeZBufferedTriangle(
            { Tri.PositionA[0], Tri.PositionA[1], Tri.PositionA[2] },
            { Tri.PositionB[0], Tri.PositionB[1], Tri.PositionB[2] },
            { Tri.PositionC[0], Tri.PositionC[1], Tri.PositionC[2] },
            { Tri.NormalA[0],   Tri.NormalA[1],   Tri.NormalA[2]   },
            { Tri.NormalB[0],   Tri.NormalB[1],   Tri.NormalB[2]   },
            { Tri.NormalC[0],   Tri.NormalC[1],   Tri.NormalC[2]   },
            {},
            false,
            static_cast<uint8_t>(Tri.MaterialSlotCode),
            &Vehicle,
            &Params,
            BounceLights,
            Cam,
            Width,
            Height,
            DepthBuf,
            ColorBuf);
    }

    // 4. Rasterize detached 3D cubes & 8-way shattered floor sub-cubes into the SAME Z-buffer
    for (const VehicleDissolveCubePiece& Cube : Snapshot.Cubes)
    {
        Rasterize3DCubePiece(Cube, Params.Mode, BounceLights, Cam, Width, Height, DepthBuf, ColorBuf);
    }

    // 5. Rasterize articulated 3D flapping Morpho & Monarch butterflies into the SAME Z-buffer
    if (!Snapshot.Butterflies.empty())
    {
        ProceduralSpriteSpec MorphoSpec  = QueryDefaultProceduralSpriteSpec(ProceduralSpriteArchetype::BotanicalMorphoButterfly);
        ProceduralSpriteSpec MonarchSpec = MorphoSpec;
        MonarchSpec.PrimaryTintR   = 1.00f; MonarchSpec.PrimaryTintG   = 0.62f; MonarchSpec.PrimaryTintB   = 0.10f;
        MonarchSpec.SecondaryTintR = 0.96f; MonarchSpec.SecondaryTintG = 0.24f; MonarchSpec.SecondaryTintB = 0.04f;

        for (const VehicleDissolveButterflyPiece& B : Snapshot.Butterflies)
        {
            RasterizeArticulatedButterfly(B, MorphoSpec, MonarchSpec, Cam, Width, Height, DepthBuf, ColorBuf);
        }
    }

    // 6. Depth-tested alpha/additive sublimation embers & fading vapor sprites (occluded cleanly behind intact car bodywork)
    for (const VehicleDissolveVaporPiece& V : Snapshot.Vapors)
    {
        RasterizeDepthTestedVaporBillboard(V, Cam, Width, Height, DepthBuf, ColorBuf);
    }

    // 7. Reinhard-film compression + sRGB encode
    for (std::size_t I = 0u; I < PixelCount; ++I)
    {
        const Vec3f C = ColorBuf[I];
        OutRgba8[I * 4u + 0u] = EncodeFilmicSrgbByte(C.X);
        OutRgba8[I * 4u + 1u] = EncodeFilmicSrgbByte(C.Y);
        OutRgba8[I * 4u + 2u] = EncodeFilmicSrgbByte(C.Z);
        OutRgba8[I * 4u + 3u] = 255u;
    }
}

} // namespace Frontier::ParticleSpace
