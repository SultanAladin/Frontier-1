/*============================================================================================================================================
                                                         PARTICLESTRANDRENDERER.CPP
============================================================================================================================================*/
// 🧩 Perspective Z-buffered 3D rasterizer for dynamic strands (hollow chain links, slender leafy vines, seamless quad-mesh flags/sails & beads)

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/ParticleSpace/Simulation/ParticleStrandRenderer.h"

#include "Engine/ParticleSpace/Dynamics/DynamicStrandSolver.h"
#include "Engine/ParticleSpace/Dynamics/EffectorGridSolver.h"
#include "Engine/ParticleSpace/Dynamics/SplineEnsembleSolver.h"
#include "Engine/ParticleSpace/Origins/ControlVehicleSurfaceLoader.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace Frontier::ParticleSpace
{

namespace
{

constexpr float kPi    = 3.14159265358979323846f;
constexpr float kTwoPi = 6.28318530717958647692f;

struct Vec3
{
    float X = 0.0f, Y = 0.0f, Z = 0.0f;
};

inline Vec3 operator+(Vec3 A, Vec3 B) noexcept { return { A.X + B.X, A.Y + B.Y, A.Z + B.Z }; }
inline Vec3 operator-(Vec3 A, Vec3 B) noexcept { return { A.X - B.X, A.Y - B.Y, A.Z - B.Z }; }
inline Vec3 operator*(Vec3 A, float S) noexcept { return { A.X * S, A.Y * S, A.Z * S }; }
inline float Dot3(Vec3 A, Vec3 B) noexcept { return A.X * B.X + A.Y * B.Y + A.Z * B.Z; }
inline Vec3 Cross3(Vec3 A, Vec3 B) noexcept { return { A.Y * B.Z - A.Z * B.Y, A.Z * B.X - A.X * B.Z, A.X * B.Y - A.Y * B.X }; }
inline float Len3(Vec3 A) noexcept { return std::sqrt(Dot3(A, A)); }
inline Vec3 Norm3(Vec3 A) noexcept
{
    const float L = Len3(A);
    return (L > 1e-6f) ? (A * (1.0f / L)) : Vec3 { 0.0f, 1.0f, 0.0f };
}
inline Vec3 Mix3(Vec3 A, Vec3 B, float T) noexcept { return A + (B - A) * T; }

inline float SmoothStepInv(float Edge0, float Edge1, float X) noexcept
{
    const float T = std::clamp((X - Edge0) / (Edge1 - Edge0), 0.0f, 1.0f);
    return T * T * (3.0f - 2.0f * T);
}

inline uint8_t LinearToSrgbByte(float C) noexcept
{
    const float Clamped = std::clamp(C, 0.0f, 1.0f);
    const float Srgb    = (Clamped <= 0.0031308f)
        ? (Clamped * 12.92f)
        : (1.055f * std::pow(Clamped, 1.0f / 2.2f) - 0.055f);
    return static_cast<uint8_t>(std::clamp(Srgb * 255.0f + 0.5f, 0.0f, 255.0f));
}

struct ResolvedCamera
{
    Vec3  Eye     {};
    Vec3  Right   {};
    Vec3  Up      {};
    Vec3  Forward {};
    float FocalPx = 400.0f;
    float HalfW   = 320.0f;
    float HalfH   = 240.0f;

    bool Project(Vec3 WorldPos, float& OutSx, float& OutSy, float& OutViewZ) const noexcept
    {
        const Vec3 Rel = WorldPos - Eye;
        const float Z  = Dot3(Rel, Forward);
        if (Z <= 0.08f) return false;
        const float InvZ = FocalPx / Z;
        OutSx    = HalfW + Dot3(Rel, Right) * InvZ;
        OutSy    = HalfH - Dot3(Rel, Up) * InvZ;
        OutViewZ = Z;
        return true;
    }
};

ResolvedCamera BuildResolvedCamera(const StrandCameraSpec& Spec, int W, int H) noexcept
{
    ResolvedCamera C {};
    const float CosP = std::cos(Spec.PitchRadians);
    const float SinP = std::sin(Spec.PitchRadians);
    const Vec3 Focus { Spec.FocusMeters[0], Spec.FocusMeters[1], Spec.FocusMeters[2] };
    C.Eye = {
        Focus.X + Spec.DistanceMeters * CosP * std::sin(Spec.YawRadians),
        Focus.Y + Spec.DistanceMeters * SinP,
        Focus.Z + Spec.DistanceMeters * CosP * std::cos(Spec.YawRadians)
    };
    C.Forward = Norm3(Focus - C.Eye);
    C.Right   = Norm3(Cross3(C.Forward, { 0.0f, 1.0f, 0.0f }));
    C.Up      = Cross3(C.Right, C.Forward);
    const float SensorH = std::max(8.0f, Spec.SensorWidthMm / 1.5f);
    const float VFov    = 2.0f * std::atan(SensorH / (2.0f * std::max(8.0f, Spec.FocalMm)));
    C.HalfW   = static_cast<float>(W) * 0.5f;
    C.HalfH   = static_cast<float>(H) * 0.5f;
    C.FocalPx = C.HalfH / std::tan(VFov * 0.5f);
    return C;
}

struct PixelCanvas
{
    int                Width  = 0;
    int                Height = 0;
    std::vector<float> RgbLin {};
    std::vector<float> Depth  {};

    PixelCanvas(int W, int H)
        : Width(W), Height(H)
        , RgbLin(static_cast<std::size_t>(W * H * 3), 0.0f)
        , Depth(static_cast<std::size_t>(W * H), 1e30f)
    {
        // Match HTML Particle Studio dark slate viewport backdrop
        for (int Y = 0; Y < H; ++Y)
        {
            const float V = static_cast<float>(Y) / static_cast<float>(std::max(1, H));
            const float R = 0.0038f + V * 0.0024f;
            const float G = 0.0046f + V * 0.0030f;
            const float B = 0.0066f + V * 0.0044f;
            for (int X = 0; X < W; ++X)
            {
                const std::size_t I = static_cast<std::size_t>((Y * W + X) * 3);
                RgbLin[I + 0] = R; RgbLin[I + 1] = G; RgbLin[I + 2] = B;
            }
        }
    }

    void BlendPixel(int X, int Y, float ViewZ, Vec3 ColLin, float Alpha, bool WriteDepth) noexcept
    {
        if (X < 0 || X >= Width || Y < 0 || Y >= Height || Alpha <= 0.003f) return;
        const std::size_t PIdx = static_cast<std::size_t>(Y * Width + X);
        if (ViewZ > Depth[PIdx] + 0.008f) return;
        const float A = std::clamp(Alpha, 0.0f, 1.0f);
        const std::size_t CIdx = PIdx * 3u;
        RgbLin[CIdx + 0] = RgbLin[CIdx + 0] * (1.0f - A) + ColLin.X * A;
        RgbLin[CIdx + 1] = RgbLin[CIdx + 1] * (1.0f - A) + ColLin.Y * A;
        RgbLin[CIdx + 2] = RgbLin[CIdx + 2] * (1.0f - A) + ColLin.Z * A;
        if (WriteDepth && A > 0.45f) Depth[PIdx] = std::min(Depth[PIdx], ViewZ);
    }

    void AddPixel(int X, int Y, float ViewZ, Vec3 ColLin, float Alpha, bool DepthTest = true) noexcept
    {
        if (X < 0 || X >= Width || Y < 0 || Y >= Height || Alpha <= 0.002f) return;
        const std::size_t PIdx = static_cast<std::size_t>(Y * Width + X);
        if (DepthTest && ViewZ > Depth[PIdx] + 0.015f) return;
        const float A = std::max(0.0f, Alpha);
        const std::size_t CIdx = PIdx * 3u;
        RgbLin[CIdx + 0] += ColLin.X * A;
        RgbLin[CIdx + 1] += ColLin.Y * A;
        RgbLin[CIdx + 2] += ColLin.Z * A;
    }

    void ExportSrgbRgba8(std::vector<uint8_t>& OutRgba8) const
    {
        OutRgba8.resize(static_cast<std::size_t>(Width * Height * 4));
        for (int I = 0; I < Width * Height; ++I)
        {
            OutRgba8[static_cast<std::size_t>(I * 4 + 0)] = LinearToSrgbByte(RgbLin[static_cast<std::size_t>(I * 3 + 0)]);
            OutRgba8[static_cast<std::size_t>(I * 4 + 1)] = LinearToSrgbByte(RgbLin[static_cast<std::size_t>(I * 3 + 1)]);
            OutRgba8[static_cast<std::size_t>(I * 4 + 2)] = LinearToSrgbByte(RgbLin[static_cast<std::size_t>(I * 3 + 2)]);
            OutRgba8[static_cast<std::size_t>(I * 4 + 3)] = 255u;
        }
    }
};

struct ScreenVert
{
    float Sx = 0.0f, Sy = 0.0f, InvZ = 1.0f;
    Vec3  Col {};
};

void RasterizeFlatTriangle(PixelCanvas& Fb, const ScreenVert& A, const ScreenVert& B, const ScreenVert& C, float Alpha, bool WriteDepth) noexcept
{
    const int Ix0 = std::clamp(static_cast<int>(std::floor(std::min({ A.Sx, B.Sx, C.Sx }))), 0, Fb.Width - 1);
    const int Ix1 = std::clamp(static_cast<int>(std::ceil(std::max({ A.Sx, B.Sx, C.Sx }))), 0, Fb.Width - 1);
    const int Iy0 = std::clamp(static_cast<int>(std::floor(std::min({ A.Sy, B.Sy, C.Sy }))), 0, Fb.Height - 1);
    const int Iy1 = std::clamp(static_cast<int>(std::ceil(std::max({ A.Sy, B.Sy, C.Sy }))), 0, Fb.Height - 1);
    if (Ix0 > Ix1 || Iy0 > Iy1) return;

    const float Area = (B.Sx - A.Sx) * (C.Sy - A.Sy) - (B.Sy - A.Sy) * (C.Sx - A.Sx);
    if (std::fabs(Area) < 1e-4f) return;
    const float InvArea = 1.0f / Area;

    for (int Y = Iy0; Y <= Iy1; ++Y)
    {
        const float Py = static_cast<float>(Y) + 0.5f;
        for (int X = Ix0; X <= Ix1; ++X)
        {
            const float Px = static_cast<float>(X) + 0.5f;
            const float W0 = ((B.Sx - Px) * (C.Sy - Py) - (B.Sy - Py) * (C.Sx - Px)) * InvArea;
            const float W1 = ((C.Sx - Px) * (A.Sy - Py) - (C.Sy - Py) * (A.Sx - Px)) * InvArea;
            const float W2 = 1.0f - W0 - W1;
            if (W0 < -1e-4f || W1 < -1e-4f || W2 < -1e-4f) continue;

            const float InvZ = W0 * A.InvZ + W1 * B.InvZ + W2 * C.InvZ;
            if (InvZ <= 1e-6f) continue;
            const float ViewZ = 1.0f / InvZ;
            const float P0 = (W0 * A.InvZ) * ViewZ;
            const float P1 = (W1 * B.InvZ) * ViewZ;
            const float P2 = (W2 * C.InvZ) * ViewZ;
            const Vec3 Col = A.Col * P0 + B.Col * P1 + C.Col * P2;
            Fb.BlendPixel(X, Y, ViewZ, Col, Alpha, WriteDepth);
        }
    }
}

void RasterizeScreenSegment(
    PixelCanvas& Fb,
    float X0, float Y0, float Z0,
    float X1, float Y1, float Z1,
    float HalfWidthPx,
    Vec3 ColLin,
    float Alpha,
    bool WriteDepth) noexcept
{
    const float MinX = std::floor(std::min(X0, X1) - HalfWidthPx - 1.5f);
    const float MaxX = std::ceil(std::max(X0, X1) + HalfWidthPx + 1.5f);
    const float MinY = std::floor(std::min(Y0, Y1) - HalfWidthPx - 1.5f);
    const float MaxY = std::ceil(std::max(Y0, Y1) + HalfWidthPx + 1.5f);

    const int Ix0 = std::clamp(static_cast<int>(MinX), 0, Fb.Width - 1);
    const int Ix1 = std::clamp(static_cast<int>(MaxX), 0, Fb.Width - 1);
    const int Iy0 = std::clamp(static_cast<int>(MinY), 0, Fb.Height - 1);
    const int Iy1 = std::clamp(static_cast<int>(MaxY), 0, Fb.Height - 1);
    if (Ix0 > Ix1 || Iy0 > Iy1) return;

    const float Dx = X1 - X0;
    const float Dy = Y1 - Y0;
    const float Len2 = std::max(1e-5f, Dx * Dx + Dy * Dy);
    const float InvZ0 = 1.0f / std::max(0.08f, Z0);
    const float InvZ1 = 1.0f / std::max(0.08f, Z1);

    for (int Y = Iy0; Y <= Iy1; ++Y)
    {
        const float Py = static_cast<float>(Y) + 0.5f;
        for (int X = Ix0; X <= Ix1; ++X)
        {
            const float Px = static_cast<float>(X) + 0.5f;
            const float T  = std::clamp(((Px - X0) * Dx + (Py - Y0) * Dy) / Len2, 0.0f, 1.0f);
            const float Cx = X0 + Dx * T;
            const float Cy = Y0 + Dy * T;
            const float D  = std::hypot(Px - Cx, Py - Cy);
            if (D > HalfWidthPx + 1.0f) continue;
            const float Cov = SmoothStepInv(HalfWidthPx + 0.85f, std::max(0.0f, HalfWidthPx - 0.35f), D);
            const float Iz  = InvZ0 * (1.0f - T) + InvZ1 * T;
            Fb.BlendPixel(X, Y, 1.0f / std::max(1e-6f, Iz), ColLin, Alpha * Cov, WriteDepth);
        }
    }
}

void RasterizeAdditiveSegment(
    PixelCanvas& Fb,
    float X0, float Y0, float Z0,
    float X1, float Y1, float Z1,
    float HalfWidthPx,
    Vec3 ColLin,
    float Alpha,
    bool DepthTest = true) noexcept
{
    if (Alpha <= 0.002f) return;
    const float Feather = std::max(0.9f, HalfWidthPx * 0.45f);
    const float Pad     = HalfWidthPx + Feather + 0.6f;
    const int Ix0 = std::clamp(static_cast<int>(std::floor(std::min(X0, X1) - Pad)), 0, Fb.Width - 1);
    const int Ix1 = std::clamp(static_cast<int>(std::ceil(std::max(X0, X1) + Pad)), 0, Fb.Width - 1);
    const int Iy0 = std::clamp(static_cast<int>(std::floor(std::min(Y0, Y1) - Pad)), 0, Fb.Height - 1);
    const int Iy1 = std::clamp(static_cast<int>(std::ceil(std::max(Y0, Y1) + Pad)), 0, Fb.Height - 1);
    if (Ix0 > Ix1 || Iy0 > Iy1) return;

    const float Dx = X1 - X0;
    const float Dy = Y1 - Y0;
    const float Len2 = std::max(1e-5f, Dx * Dx + Dy * Dy);
    const float InvZ0 = 1.0f / std::max(0.08f, Z0);
    const float InvZ1 = 1.0f / std::max(0.08f, Z1);

    for (int Y = Iy0; Y <= Iy1; ++Y)
    {
        const float Py = static_cast<float>(Y) + 0.5f;
        for (int X = Ix0; X <= Ix1; ++X)
        {
            const float Px = static_cast<float>(X) + 0.5f;
            const float T  = std::clamp(((Px - X0) * Dx + (Py - Y0) * Dy) / Len2, 0.0f, 1.0f);
            const float Cx = X0 + Dx * T;
            const float Cy = Y0 + Dy * T;
            const float D  = std::hypot(Px - Cx, Py - Cy);
            if (D > HalfWidthPx + Feather) continue;
            const float Cov = SmoothStepInv(HalfWidthPx + Feather, std::max(0.0f, HalfWidthPx - 0.35f), D);
            const float Iz  = InvZ0 * (1.0f - T) + InvZ1 * T;
            Fb.AddPixel(X, Y, 1.0f / std::max(1e-6f, Iz), ColLin, Alpha * Cov, DepthTest);
        }
    }
}

/// Depth-of-field bokeh aperture disc or glowing pinpoint star core (1:1 SoftwarePreviewEngine.js lines 5366-5408).
void RasterizeBokehDisc(
    PixelCanvas& Fb,
    float Cx, float Cy, float ViewZ,
    float RadiusPx,
    Vec3 ColMid,
    Vec3 ColBright,
    float Alpha,
    bool IsLargeApertureBokeh) noexcept
{
    if (Alpha <= 0.004f || RadiusPx <= 0.15f) return;
    const float MaxR = IsLargeApertureBokeh ? (RadiusPx + 1.5f) : (RadiusPx * 2.5f + 1.5f);
    const int Ix0 = std::clamp(static_cast<int>(std::floor(Cx - MaxR)), 0, Fb.Width - 1);
    const int Ix1 = std::clamp(static_cast<int>(std::ceil(Cx + MaxR)), 0, Fb.Width - 1);
    const int Iy0 = std::clamp(static_cast<int>(std::floor(Cy - MaxR)), 0, Fb.Height - 1);
    const int Iy1 = std::clamp(static_cast<int>(std::ceil(Cy + MaxR)), 0, Fb.Height - 1);
    if (Ix0 > Ix1 || Iy0 > Iy1) return;

    const Vec3 White { 1.0f, 1.0f, 1.0f };
    for (int Y = Iy0; Y <= Iy1; ++Y)
    {
        const float Dy = static_cast<float>(Y) + 0.5f - Cy;
        for (int X = Ix0; X <= Ix1; ++X)
        {
            const float Dx = static_cast<float>(X) + 0.5f - Cx;
            const float Dist = std::hypot(Dx, Dy);
            if (IsLargeApertureBokeh)
            {
                if (Dist > RadiusPx + 0.9f) continue;
                const float U = Dist / std::max(0.5f, RadiusPx);
                // Calibrated linear-space translucent lens bokeh (matches Canvas 2D sRGB alpha 0.36..0.44 -> linear ~0.07..0.13)
                float Weight = 0.0f;
                Vec3  Tint   = ColBright;
                if (U < 0.72f)
                {
                    const float K = U / 0.72f;
                    Weight = 0.095f * (1.0f - K) + 0.055f * K;
                    Tint   = Mix3(ColBright, ColMid, K * 0.55f);
                }
                else if (U < 0.93f)
                {
                    const float K = (U - 0.72f) / 0.21f;
                    Weight = 0.055f + 0.068f * std::sin(K * kPi);
                    Tint   = Mix3(ColMid, ColBright, 0.85f);
                }
                else
                {
                    const float K = std::clamp((U - 0.93f) / 0.10f, 0.0f, 1.0f);
                    Weight = 0.065f * (1.0f - K);
                    Tint   = ColBright;
                }
                Fb.AddPixel(X, Y, ViewZ, Tint, Alpha * Weight, true);
            }
            else
            {
                const float HaloR = RadiusPx * 2.45f;
                if (Dist > HaloR) continue;
                const float U = Dist / std::max(0.4f, HaloR);
                const float HaloW = (1.0f - U) * (1.0f - U) * 0.22f;
                const float CoreR = std::max(0.95f, RadiusPx * 0.62f);
                const float CoreW = SmoothStepInv(CoreR + 0.65f, std::max(0.0f, CoreR - 0.30f), Dist) * 0.94f;
                const Vec3  Col   = ColMid * HaloW + Mix3(ColBright, White, 0.78f) * CoreW;
                Fb.AddPixel(X, Y, ViewZ, Col, Alpha, true);
            }
        }
    }
}

/// Additive radial flash (white-hot centre -> colored halo) for dielectric arc contacts & plasma cores.
void RasterizeRadialFlash(
    PixelCanvas& Fb,
    float Cx, float Cy, float ViewZ,
    float RadiusPx,
    Vec3 ColHalo,
    float Intensity,
    bool DepthTest = false) noexcept
{
    if (Intensity <= 0.005f || RadiusPx <= 0.5f) return;
    const int Ix0 = std::clamp(static_cast<int>(std::floor(Cx - RadiusPx - 1.0f)), 0, Fb.Width - 1);
    const int Ix1 = std::clamp(static_cast<int>(std::ceil(Cx + RadiusPx + 1.0f)), 0, Fb.Width - 1);
    const int Iy0 = std::clamp(static_cast<int>(std::floor(Cy - RadiusPx - 1.0f)), 0, Fb.Height - 1);
    const int Iy1 = std::clamp(static_cast<int>(std::ceil(Cy + RadiusPx + 1.0f)), 0, Fb.Height - 1);
    if (Ix0 > Ix1 || Iy0 > Iy1) return;

    const Vec3 White { 1.0f, 1.0f, 1.0f };
    for (int Y = Iy0; Y <= Iy1; ++Y)
    {
        const float Dy = static_cast<float>(Y) + 0.5f - Cy;
        for (int X = Ix0; X <= Ix1; ++X)
        {
            const float Dx = static_cast<float>(X) + 0.5f - Cx;
            const float U  = std::hypot(Dx, Dy) / std::max(0.5f, RadiusPx);
            if (U >= 1.0f) continue;
            const float Inv = 1.0f - U;
            const float CoreW = std::pow(Inv, 4.2f) * 0.95f;
            const float HaloW = std::pow(Inv, 1.85f) * 0.55f;
            const Vec3  Rgb   = White * CoreW + ColHalo * HaloW;
            Fb.AddPixel(X, Y, ViewZ, Rgb, Intensity, DepthTest);
        }
    }
}

/// Forward declaration for solid sphere shading helper defined below.
void RasterizeLitSphere(PixelCanvas& Fb, const ResolvedCamera& Cam, const HardwareSphereAccent& Sph) noexcept;

/// Renders a 3D dark translucent core sphere (occluder) + glowing horizontal rings (bokeh-object-pulse & electrodes).
void RasterizeDarkCoreSphereWithRings(
    PixelCanvas& Fb,
    const ResolvedCamera& Cam,
    Vec3 Centre,
    float Radius,
    Vec3 BaseLin,
    Vec3 RingCol,
    bool IncludeCrownRing) noexcept
{
    // Dark obsidian translucent sphere writing depth so back-side ribbons occlude cleanly (no warm highlight)
    float Cx, Cy, Cz;
    if (Cam.Project(Centre, Cx, Cy, Cz))
    {
        const float RadPx = (Radius * Cam.FocalPx) / std::max(0.25f, Cz);
        const int Ix0 = std::clamp(static_cast<int>(std::floor(Cx - RadPx - 1.0f)), 0, Fb.Width - 1);
        const int Ix1 = std::clamp(static_cast<int>(std::ceil(Cx + RadPx + 1.0f)), 0, Fb.Width - 1);
        const int Iy0 = std::clamp(static_cast<int>(std::floor(Cy - RadPx - 1.0f)), 0, Fb.Height - 1);
        const int Iy1 = std::clamp(static_cast<int>(std::ceil(Cy + RadPx + 1.0f)), 0, Fb.Height - 1);
        for (int Y = Iy0; Y <= Iy1; ++Y)
        {
            const float Dy = (static_cast<float>(Y) + 0.5f - Cy) / std::max(1.0f, RadPx);
            for (int X = Ix0; X <= Ix1; ++X)
            {
                const float Dx = (static_cast<float>(X) + 0.5f - Cx) / std::max(1.0f, RadPx);
                const float R2 = Dx * Dx + Dy * Dy;
                if (R2 > 1.0f) continue;
                const float Nz   = std::sqrt(std::max(0.0f, 1.0f - R2));
                const float Rim  = std::pow(1.0f - Nz, 2.8f);
                const Vec3  Col  = BaseLin * (0.65f + 0.35f * Nz) + RingCol * (Rim * 0.085f);
                const float DepthZ = Cz - Nz * Radius;
                Fb.BlendPixel(X, Y, DepthZ, Col, 0.86f, true);
            }
        }
    }

    // Subtle glowing rim ring on equator
    constexpr int Segs = 40;
    const float EqR = Radius * 1.015f;
    for (int S = 0; S < Segs; ++S)
    {
        const float A0 = (static_cast<float>(S) / static_cast<float>(Segs)) * kTwoPi;
        const float A1 = (static_cast<float>(S + 1) / static_cast<float>(Segs)) * kTwoPi;
        const Vec3 P0 { Centre.X + std::cos(A0) * EqR, Centre.Y, Centre.Z + std::sin(A0) * EqR };
        const Vec3 P1 { Centre.X + std::cos(A1) * EqR, Centre.Y, Centre.Z + std::sin(A1) * EqR };
        float X0, Y0, Z0, X1, Y1, Z1;
        if (Cam.Project(P0, X0, Y0, Z0) && Cam.Project(P1, X1, Y1, Z1))
        {
            RasterizeAdditiveSegment(Fb, X0, Y0, Z0 - 0.04f, X1, Y1, Z1 - 0.04f, 0.65f, RingCol, 0.16f, true);
        }
    }

    if (IncludeCrownRing)
    {
        const float CrownR = Radius * 0.42f;
        const float CrownY = Centre.Y + Radius * 0.92f;
        for (int S = 0; S < 28; ++S)
        {
            const float A0 = (static_cast<float>(S) / 28.0f) * kTwoPi;
            const float A1 = (static_cast<float>(S + 1) / 28.0f) * kTwoPi;
            const Vec3 P0 { Centre.X + std::cos(A0) * CrownR, CrownY, Centre.Z + std::sin(A0) * CrownR };
            const Vec3 P1 { Centre.X + std::cos(A1) * CrownR, CrownY, Centre.Z + std::sin(A1) * CrownR };
            float X0, Y0, Z0, X1, Y1, Z1;
            if (Cam.Project(P0, X0, Y0, Z0) && Cam.Project(P1, X1, Y1, Z1))
            {
                RasterizeAdditiveSegment(Fb, X0, Y0, Z0 - 0.04f, X1, Y1, Z1 - 0.04f, 0.78f, RingCol, 0.28f, true);
            }
        }
    }
}

void RasterizeWorldLine(PixelCanvas& Fb, const ResolvedCamera& Cam, Vec3 P0, Vec3 P1, Vec3 ColLin, float Alpha) noexcept
{
    float X0, Y0, Z0, X1, Y1, Z1;
    if (!Cam.Project(P0, X0, Y0, Z0) || !Cam.Project(P1, X1, Y1, Z1)) return;
    RasterizeScreenSegment(Fb, X0, Y0, Z0, X1, Y1, Z1, 0.55f, ColLin, Alpha, false);
}

void FloorGridEnabled(PixelCanvas& Fb, const ResolvedCamera& Cam, float CentreX, float CentreZ) noexcept
{
    const float SnapX = std::floor(CentreX);
    const float SnapZ = std::floor(CentreZ);
    const Vec3 MinorCol { 0.011f, 0.014f, 0.020f };
    const Vec3 MajorCol { 0.021f, 0.027f, 0.036f };

    for (int I = -6; I <= 6; ++I)
    {
        const float Fi = static_cast<float>(I) * 2.0f;
        const Vec3 Col = (I % 2 == 0) ? MajorCol : MinorCol;
        const float A  = (I % 2 == 0) ? 0.58f : 0.38f;
        for (int S = -6; S < 6; ++S)
        {
            const float S0 = static_cast<float>(S) * 2.0f;
            const float S1 = static_cast<float>(S + 1) * 2.0f;
            RasterizeWorldLine(Fb, Cam, { SnapX + S0, 0.0f, SnapZ + Fi }, { SnapX + S1, 0.0f, SnapZ + Fi }, Col, A);
            RasterizeWorldLine(Fb, Cam, { SnapX + Fi, 0.0f, SnapZ + S0 }, { SnapX + Fi, 0.0f, SnapZ + S1 }, Col, A);
        }
    }
}

void RasterizeLitCylinder(PixelCanvas& Fb, const ResolvedCamera& Cam, const HardwareCylinderSegment& Cyl) noexcept
{
    const Vec3 P0 { Cyl.StartMeters[0], Cyl.StartMeters[1], Cyl.StartMeters[2] };
    const Vec3 P1 { Cyl.EndMeters[0], Cyl.EndMeters[1], Cyl.EndMeters[2] };
    const Vec3 Axis = Norm3(P1 - P0);
    const Vec3 Ref  = (std::fabs(Axis.Y) < 0.92f) ? Vec3 { 0.0f, 1.0f, 0.0f } : Vec3 { 1.0f, 0.0f, 0.0f };
    const Vec3 U    = Norm3(Cross3(Ref, Axis));
    const Vec3 V    = Cross3(Axis, U);
    const Vec3 LightDir = Norm3({ 0.45f, 0.65f, 0.60f });
    const Vec3 CylCol { Cyl.ColourLinear[0], Cyl.ColourLinear[1], Cyl.ColourLinear[2] };

    constexpr int Sides = 12;
    for (int S = 0; S < Sides; ++S)
    {
        const float A0 = kTwoPi * (static_cast<float>(S) / static_cast<float>(Sides));
        const float A1 = kTwoPi * (static_cast<float>(S + 1) / static_cast<float>(Sides));
        const Vec3 N0 = U * std::cos(A0) + V * std::sin(A0);
        const Vec3 N1 = U * std::cos(A1) + V * std::sin(A1);

        const Vec3 W00 = P0 + N0 * Cyl.RadiusMeters;
        const Vec3 W01 = P0 + N1 * Cyl.RadiusMeters;
        const Vec3 W10 = P1 + N0 * Cyl.RadiusMeters;
        const Vec3 W11 = P1 + N1 * Cyl.RadiusMeters;

        const float L0 = 0.44f + 0.56f * std::max(0.0f, Dot3(N0, LightDir));
        const float L1 = 0.44f + 0.56f * std::max(0.0f, Dot3(N1, LightDir));
        const float Spec0 = std::pow(std::max(0.0f, Dot3(N0, Norm3(LightDir + Norm3(Cam.Eye - W00)))), 18.0f) * 0.22f;
        const float Spec1 = std::pow(std::max(0.0f, Dot3(N1, Norm3(LightDir + Norm3(Cam.Eye - W01)))), 18.0f) * 0.22f;

        ScreenVert Sv[4] {};
        const Vec3 Pts[4]  = { W00, W01, W11, W10 };
        const Vec3 Cols[4] = {
            CylCol * L0 + Vec3 { Spec0, Spec0, Spec0 },
            CylCol * L1 + Vec3 { Spec1, Spec1, Spec1 },
            CylCol * L1 + Vec3 { Spec1, Spec1, Spec1 },
            CylCol * L0 + Vec3 { Spec0, Spec0, Spec0 }
        };
        bool AllOk = true;
        for (int K = 0; K < 4; ++K)
        {
            float Z = 1.0f;
            if (!Cam.Project(Pts[K], Sv[K].Sx, Sv[K].Sy, Z)) { AllOk = false; break; }
            Sv[K].InvZ = 1.0f / Z;
            Sv[K].Col  = Cols[K];
        }
        if (!AllOk) continue;

        RasterizeFlatTriangle(Fb, Sv[0], Sv[1], Sv[2], Cyl.ColourLinear[3], true);
        RasterizeFlatTriangle(Fb, Sv[0], Sv[2], Sv[3], Cyl.ColourLinear[3], true);
    }
}

void RasterizeLitSphere(PixelCanvas& Fb, const ResolvedCamera& Cam, const HardwareSphereAccent& Sph) noexcept
{
    const Vec3 C { Sph.CentreMeters[0], Sph.CentreMeters[1], Sph.CentreMeters[2] };
    float Cx, Cy, Cz;
    if (!Cam.Project(C, Cx, Cy, Cz)) return;
    const float RadPx = (Sph.RadiusMeters * Cam.FocalPx) / Cz;
    const int Ix0 = std::clamp(static_cast<int>(std::floor(Cx - RadPx - 1.0f)), 0, Fb.Width - 1);
    const int Ix1 = std::clamp(static_cast<int>(std::ceil(Cx + RadPx + 1.0f)), 0, Fb.Width - 1);
    const int Iy0 = std::clamp(static_cast<int>(std::floor(Cy - RadPx - 1.0f)), 0, Fb.Height - 1);
    const int Iy1 = std::clamp(static_cast<int>(std::ceil(Cy + RadPx + 1.0f)), 0, Fb.Height - 1);
    const Vec3 Col { Sph.ColourLinear[0], Sph.ColourLinear[1], Sph.ColourLinear[2] };
    const Vec3 Light = Norm3({ -0.35f, 0.65f, 0.68f });

    for (int Y = Iy0; Y <= Iy1; ++Y)
    {
        for (int X = Ix0; X <= Ix1; ++X)
        {
            const float Nx = (static_cast<float>(X) + 0.5f - Cx) / std::max(0.5f, RadPx);
            const float Ny = -(static_cast<float>(Y) + 0.5f - Cy) / std::max(0.5f, RadPx);
            const float D2 = Nx * Nx + Ny * Ny;
            if (D2 > 1.0f) continue;
            const float Nz   = std::sqrt(std::max(0.0f, 1.0f - D2));
            const float Diff = 0.44f + 0.56f * std::max(0.0f, Nx * Light.X + Ny * Light.Y + Nz * Light.Z);
            const float Spec = std::pow(std::max(0.0f, Nx * Light.X + Ny * Light.Y + Nz * Light.Z), 22.0f) * 0.78f;
            const Vec3 Shaded = Col * Diff + Vec3 { Spec, Spec * 0.92f, Spec * 0.65f };
            Fb.BlendPixel(X, Y, Cz - Nz * Sph.RadiusMeters, Shaded, SmoothStepInv(1.0f, 0.78f, std::sqrt(D2)), true);
        }
    }
}

void RasterizeRing(PixelCanvas& Fb, const ResolvedCamera& Cam, const HardwareRingAccent& Ring) noexcept
{
    const uint32_t N = std::max(8u, Ring.Segments);
    const Vec3 Col { Ring.ColourLinear[0], Ring.ColourLinear[1], Ring.ColourLinear[2] };
    for (uint32_t I = 0u; I < N; ++I)
    {
        const float A0 = kTwoPi * (static_cast<float>(I) / static_cast<float>(N));
        const float A1 = kTwoPi * (static_cast<float>(I + 1u) / static_cast<float>(N));
        const Vec3 P0 { Ring.CentreMeters[0] + std::cos(A0) * Ring.RadiusMeters, Ring.CentreMeters[1], Ring.CentreMeters[2] + std::sin(A0) * Ring.RadiusMeters };
        const Vec3 P1 { Ring.CentreMeters[0] + std::cos(A1) * Ring.RadiusMeters, Ring.CentreMeters[1], Ring.CentreMeters[2] + std::sin(A1) * Ring.RadiusMeters };
        RasterizeWorldLine(Fb, Cam, P0, P1, Col, Ring.ColourLinear[3]);
    }
}

void RasterizeHollowChainOval(
    PixelCanvas& Fb,
    float Cx, float Cy, float ViewZ,
    float AngleRad,
    float Rx, float Ry,
    float StrokePx,
    Vec3 ColLin) noexcept
{
    // Exact hollow oval metallic chain link stroked as a crisp 14-segment closed ellipse loop (zero gradient blur!)
    const float CosA   = std::cos(AngleRad);
    const float SinA   = std::sin(AngleRad);
    const float SafeRx = std::max(2.5f, Rx);
    const float SafeRy = std::max(1.6f, Ry);
    const float HalfW  = std::max(0.68f, StrokePx * 0.42f);

    constexpr int Segs = 14;
    float PrevX = Cx + SafeRx * CosA;
    float PrevY = Cy + SafeRx * SinA;
    for (int S = 1; S <= Segs; ++S)
    {
        const float T  = kTwoPi * (static_cast<float>(S) / static_cast<float>(Segs));
        const float Lx = SafeRx * std::cos(T);
        const float Ly = SafeRy * std::sin(T);
        const float Nx = Cx + Lx * CosA - Ly * SinA;
        const float Ny = Cy + Lx * SinA + Ly * CosA;
        const float Spec = 0.88f + 0.22f * std::max(0.0f, -std::sin(T));
        RasterizeScreenSegment(Fb, PrevX, PrevY, ViewZ, Nx, Ny, ViewZ, HalfW, ColLin * Spec, 0.95f, true);
        PrevX = Nx;
        PrevY = Ny;
    }
}

void RasterizeGlossyBeadDisc(PixelCanvas& Fb, float Cx, float Cy, float ViewZ, float RadPx, Vec3 ColLin, float Alpha) noexcept
{
    const float R = std::max(1.8f, RadPx);
    const int Ix0 = std::clamp(static_cast<int>(std::floor(Cx - R - 1.5f)), 0, Fb.Width - 1);
    const int Ix1 = std::clamp(static_cast<int>(std::ceil(Cx + R + 1.5f)), 0, Fb.Width - 1);
    const int Iy0 = std::clamp(static_cast<int>(std::floor(Cy - R - 1.5f)), 0, Fb.Height - 1);
    const int Iy1 = std::clamp(static_cast<int>(std::ceil(Cy + R + 1.5f)), 0, Fb.Height - 1);

    const float Hx = Cx - R * 0.28f;
    const float Hy = Cy - R * 0.28f;
    const float Hr = std::max(0.85f, R * 0.30f);

    for (int Y = Iy0; Y <= Iy1; ++Y)
    {
        const float Py = static_cast<float>(Y) + 0.5f;
        for (int X = Ix0; X <= Ix1; ++X)
        {
            const float Px = static_cast<float>(X) + 0.5f;
            const float D  = std::hypot(Px - Cx, Py - Cy);
            if (D > R + 0.9f) continue;
            const float Cov = SmoothStepInv(R + 0.85f, std::max(0.0f, R - 0.45f), D);
            const float Nr  = std::min(1.0f, D / R);
            const float Nz  = std::sqrt(std::max(0.0f, 1.0f - Nr * Nr));
            const float Dh  = std::hypot(Px - Hx, Py - Hy);
            const float Spec = SmoothStepInv(Hr + 0.6f, 0.0f, Dh) * 0.72f;
            const Vec3 Shaded = ColLin * (0.68f + 0.42f * Nz) + Vec3 { Spec, Spec, Spec };
            Fb.BlendPixel(X, Y, ViewZ - 0.02f, Shaded, Cov * Alpha, true);
        }
    }
}

void RasterizeDelicateIvyLeaf(PixelCanvas& Fb, const ResolvedCamera& Cam, const ParticleSlot& Slot, Vec3 ColC, Vec3 ColB) noexcept
{
    const Vec3 Pos { Slot.PositionMeters[0], Slot.PositionMeters[1], Slot.PositionMeters[2] };
    float Cx, Cy, Cz;
    if (!Cam.Project(Pos, Cx, Cy, Cz)) return;
    // Exact SoftwarePreviewEngine.js vine ivy leaf sizing: max(3.2, radius * 195 / max(1.8, z))
    const float ScaleRatio = static_cast<float>(Fb.Height) / 480.0f;
    const float LRad = std::max(3.0f, Slot.SizeMeters * 195.0f / std::max(1.8f, Cz)) * ScaleRatio;
    const float Ang  = std::sin(Slot.SpinRadians) * 0.85f;
    const float CosA = std::cos(Ang);
    const float SinA = std::sin(Ang);
    const Vec3 LeafCol = Mix3(ColC, ColB, 0.25f + 0.55f * Slot.SeedScalar);

    const int Pad = static_cast<int>(std::ceil(LRad + 2.0f));
    const int Ix0 = std::clamp(static_cast<int>(std::floor(Cx)) - Pad, 0, Fb.Width - 1);
    const int Ix1 = std::clamp(static_cast<int>(std::ceil(Cx)) + Pad, 0, Fb.Width - 1);
    const int Iy0 = std::clamp(static_cast<int>(std::floor(Cy)) - Pad, 0, Fb.Height - 1);
    const int Iy1 = std::clamp(static_cast<int>(std::ceil(Cy)) + Pad, 0, Fb.Height - 1);

    for (int Y = Iy0; Y <= Iy1; ++Y)
    {
        const float Dy = (static_cast<float>(Y) + 0.5f) - Cy;
        for (int X = Ix0; X <= Ix1; ++X)
        {
            const float Dx = (static_cast<float>(X) + 0.5f) - Cx;
            const float Lx = ( Dx * CosA + Dy * SinA) / std::max(1.0f, LRad);
            const float Ly = (-Dx * SinA + Dy * CosA) / std::max(1.0f, LRad);
            if (Ly < -1.0f || Ly > 1.0f) continue;
            const float HalfW = 0.44f * (1.0f - Ly * Ly);
            const float Ax = std::fabs(Lx);
            if (Ax > HalfW + 0.14f) continue;
            const float Cov = SmoothStepInv(HalfW + 0.12f, std::max(0.0f, HalfW - 0.08f), Ax);
            const float Vein = std::exp(-Ax * Ax * 32.0f) * 0.28f;
            Fb.BlendPixel(X, Y, Cz - 0.01f, Mix3(LeafCol, ColB * 1.15f, Vein), Cov * 0.92f, true);
        }
    }
}

Vec3 EvalCatmull(std::span<const StrandVertex> Nodes, uint32_t FromK, uint32_t ToK, float ULocal) noexcept
{
    const uint32_t Span = std::max<uint32_t>(1u, ToK - FromK);
    const float F       = std::clamp(ULocal, 0.0f, 1.0f) * static_cast<float>(Span);
    const uint32_t I1   = std::min<uint32_t>(ToK - 1u, FromK + static_cast<uint32_t>(std::floor(F)));
    const float T       = std::clamp(F - static_cast<float>(I1 - FromK), 0.0f, 1.0f);
    const uint32_t I0   = (I1 > FromK) ? (I1 - 1u) : FromK;
    const uint32_t I2   = std::min<uint32_t>(ToK, I1 + 1u);
    const uint32_t I3   = std::min<uint32_t>(ToK, I1 + 2u);
    const auto Load     = [&](uint32_t K) noexcept -> Vec3 {
        return { Nodes[K].PositionMeters[0], Nodes[K].PositionMeters[1], Nodes[K].PositionMeters[2] };
    };
    const Vec3 P0 = Load(I0), P1 = Load(I1), P2 = Load(I2), P3 = Load(I3);
    const float T2 = T * T, T3 = T2 * T;
    return {
        0.5f * ((2.0f * P1.X) + (-P0.X + P2.X) * T + (2.0f * P0.X - 5.0f * P1.X + 4.0f * P2.X - P3.X) * T2 + (-P0.X + 3.0f * P1.X - 3.0f * P2.X + P3.X) * T3),
        0.5f * ((2.0f * P1.Y) + (-P0.Y + P2.Y) * T + (2.0f * P0.Y - 5.0f * P1.Y + 4.0f * P2.Y - P3.Y) * T2 + (-P0.Y + 3.0f * P1.Y - 3.0f * P2.Y + P3.Y) * T3),
        0.5f * ((2.0f * P1.Z) + (-P0.Z + P2.Z) * T + (2.0f * P0.Z - 5.0f * P1.Z + 4.0f * P2.Z - P3.Z) * T2 + (-P0.Z + 3.0f * P1.Z - 3.0f * P2.Z + P3.Z) * T3)
    };
}

void ExtractGradientStops(const ParticleRig& Rig, Vec3& OutColA, Vec3& OutColC, Vec3& OutColB) noexcept
{
    OutColA = { 0.3f, 0.5f, 0.8f };
    OutColC = { 0.5f, 0.7f, 0.9f };
    OutColB = { 0.9f, 0.95f, 1.0f };
    if (const auto* Grad = std::get_if<ColourSpace::MultiStopGradientSource>(&Rig.ColourProfile.Source))
    {
        if (Grad->Stops.size() >= 3u)
        {
            OutColA = { Grad->Stops[0].LinearTint.Red, Grad->Stops[0].LinearTint.Green, Grad->Stops[0].LinearTint.Blue };
            OutColC = { Grad->Stops[1].LinearTint.Red, Grad->Stops[1].LinearTint.Green, Grad->Stops[1].LinearTint.Blue };
            OutColB = { Grad->Stops[2].LinearTint.Red, Grad->Stops[2].LinearTint.Green, Grad->Stops[2].LinearTint.Blue };
        }
    }
}

void ExtractOriginMeters(const ReleaseOriginSpec& Origin, float Out[3]) noexcept
{
    Out[0] = 0.0f; Out[1] = 0.0f; Out[2] = 0.0f;
    if (const auto* Pt = std::get_if<AnalyticPointOrigin>(&Origin))
        for (int K = 0; K < 3; ++K) Out[K] = Pt->CenterMeters[K];
    else if (const auto* Sph = std::get_if<AnalyticSphereOrigin>(&Origin))
        for (int K = 0; K < 3; ++K) Out[K] = Sph->CenterMeters[K];
    else if (const auto* Box = std::get_if<AnalyticBoxOrigin>(&Origin))
        for (int K = 0; K < 3; ++K) Out[K] = Box->CenterMeters[K];
    else if (const auto* Cloth = std::get_if<ClothGridOrigin>(&Origin))
        for (int K = 0; K < 3; ++K) Out[K] = Cloth->OriginMeters[K];
    else if (const auto* Web = std::get_if<SpiderwebOrigin>(&Origin))
        for (int K = 0; K < 3; ++K) Out[K] = Web->CenterMeters[K];
    else if (const auto* Elec = std::get_if<DualElectrodeOrigin>(&Origin))
        for (int K = 0; K < 3; ++K) Out[K] = Elec->CathodeMeters[K];
}

/// Renders Curl Wave Field (mode 1), Spherical Curl Plasma (mode 2), Bokeh Stream Ribbons & Object Surface Pour Ribbons (mode 3).
void RenderCurlStreamRigIntoCanvas(
    PixelCanvas&                  Fb,
    const ResolvedCamera&         Cam,
    const ParticleRig&            Rig,
    std::span<const ParticleSlot> Slots,
    std::span<const StrandVertex> StrandVertices)
{
    if (!Rig.Dynamics.Curl.has_value()) return;
    const CurlManifoldSpec& Spec = *Rig.Dynamics.Curl;

    float OriginMeters[3] {};
    ExtractOriginMeters(Rig.Origin, OriginMeters);
    const Vec3 Org { OriginMeters[0], OriginMeters[1], OriginMeters[2] };

    Vec3 ColA {}, ColC {}, ColB {};
    ExtractGradientStops(Rig, ColA, ColC, ColB);

    uint32_t VertsPerStrand = 3u;
    if (const auto* Rib = std::get_if<RibbonStrandCarrier>(&Rig.Carrier))
        VertsPerStrand = std::max<uint32_t>(3u, Rib->VerticesPerStrand);

    const float ScaleRatio = static_cast<float>(Fb.Height) / 480.0f;

    // 1. Optional dark translucent sphere core + equatorial & crown rings (bokeh-object-pulse, exact SoftwarePreviewEngine.js lines 5306-5325)
    if (Spec.ManifoldMode == 3u && Spec.PulseMode > 0u && Spec.ShowObjectCore && Spec.ObjectShape == 1u)
    {
        const float RCore = std::max(0.35f, Spec.BoxHalfMeters[0]) * 0.96f;
        const Vec3  CoreBase { 0.0055f, 0.0085f, 0.0135f };
        const Vec3  CyanRing { 0.18f, 0.72f, 0.92f };
        RasterizeDarkCoreSphereWithRings(Fb, Cam, Org, RCore, CoreBase, CyanRing, true);
    }

    // 2. Render curved micro-filaments (modes 1 & 2) OR 18-vertex tapered stream ribbons + bokeh discs (mode 3)
    const Vec3 White { 1.0f, 1.0f, 1.0f };
    if (Spec.ManifoldMode == 3u && VertsPerStrand >= 6u)
    {
        // Depth-order heads back-to-front so large translucent bokeh discs compose harmoniously
        struct StreamItem { uint32_t Idx; float DepthZ; };
        std::vector<StreamItem> Order {};
        Order.reserve(Slots.size());
        for (uint32_t I = 0u; I < static_cast<uint32_t>(Slots.size()); ++I)
        {
            if (!IsParticleSlotAlive(Slots[I])) continue;
            float Sx, Sy, Sz;
            const Vec3 P { Slots[I].PositionMeters[0], Slots[I].PositionMeters[1], Slots[I].PositionMeters[2] };
            if (Cam.Project(P, Sx, Sy, Sz)) Order.push_back({ I, Sz });
        }
        std::sort(Order.begin(), Order.end(), [](const StreamItem& A, const StreamItem& B) { return A.DepthZ > B.DepthZ; });

        struct ProjPt { float X, Y, Z; };
        std::vector<ProjPt> RibbonPts {};
        RibbonPts.reserve(VertsPerStrand);

        for (const auto& Entry : Order)
        {
            const uint32_t I = Entry.Idx;
            const ParticleSlot& Slot = Slots[I];
            const std::size_t Off = static_cast<std::size_t>(I) * VertsPerStrand;
            if (Off + VertsPerStrand > StrandVertices.size()) continue;

            const float LifeFrac = std::clamp(Slot.AgeSeconds / std::max(0.1f, Slot.LifespanSeconds), 0.0f, 1.0f);
            const float Env = std::min(1.0f, LifeFrac / 0.14f) * std::min(1.0f, (1.0f - LifeFrac) / 0.22f);
            if (Env <= 0.01f) continue;

            const float Dof   = std::clamp(Slot.OriginAnchor[0], 0.0f, 1.0f);
            const float Pulse = std::max(0.0f, Slot.OriginAnchor[1]);
            const float Wake  = std::clamp(Slot.OriginAnchor[2], 0.05f, 1.0f);
            const float Crest = std::clamp(Slot.AuxScalar, 0.0f, 1.0f);

            RibbonPts.clear();
            // StrandVertices[Off + 0] is head; [Off + VertsPerStrand - 1] is oldest tail
            for (int K = static_cast<int>(VertsPerStrand) - 1; K >= 0; --K)
            {
                const auto& V = StrandVertices[Off + static_cast<std::size_t>(K)];
                float Sx, Sy, Sz;
                if (Cam.Project({ V.PositionMeters[0], V.PositionMeters[1], V.PositionMeters[2] }, Sx, Sy, Sz))
                    RibbonPts.push_back({ Sx, Sy, Sz });
            }
            if (RibbonPts.size() < 2u) continue;

            const auto& Head = RibbonPts.back();
            // Exact SoftwarePreviewEngine.js lines 5287-5289 (never square Wake!) + linear-calibrated alpha
            const float PulseScale = (Spec.PulseMode > 0u) ? std::min(1.18f, 0.22f + 0.92f * Wake) : 1.0f;
            const float HeadScale  = (Spec.PulseMode > 0u) ? (0.58f + 0.72f * Pulse) : 1.0f;
            const float RibAlpha   = std::min(1.0f, (0.55f + 0.45f * Crest) * Env * PulseScale);
            const float BaseW      = std::clamp((Slot.SizeMeters * Spec.StreakThin * 155.0f) / std::max(1.8f, Head.Z) * ScaleRatio, 0.45f, 1.35f * ScaleRatio);

            // Smooth fading hair-thin luminous stream ribbons from tail (U=0) -> head (U=1)
            const std::size_t SegCount = RibbonPts.size() - 1u;
            for (std::size_t S = 0u; S < SegCount; ++S)
            {
                const float U0 = static_cast<float>(S) / static_cast<float>(SegCount);
                const float U1 = static_cast<float>(S + 1u) / static_cast<float>(SegCount);
                const float Um = 0.5f * (U0 + U1);
                const float Fade = std::pow(Um, 1.25f);
                const float W    = std::max(0.35f, BaseW * (0.45f + 0.55f * Um));
                const Vec3  Col  = (Um < 0.52f)
                    ? Mix3(ColA, ColC, Um / 0.52f)
                    : Mix3(ColC, ColB, (Um - 0.52f) / 0.48f);
                RasterizeAdditiveSegment(Fb, RibbonPts[S].X, RibbonPts[S].Y, RibbonPts[S].Z,
                                             RibbonPts[S + 1u].X, RibbonPts[S + 1u].Y, RibbonPts[S + 1u].Z,
                                             W * 0.48f, Col, RibAlpha * Fade * 0.24f, true);

                // Crisp white-cyan leading thread over front 44% of stream ribbon
                if (Um > 0.56f)
                {
                    const float LeadU = (Um - 0.56f) / 0.44f;
                    RasterizeAdditiveSegment(Fb, RibbonPts[S].X, RibbonPts[S].Y, RibbonPts[S].Z,
                                                 RibbonPts[S + 1u].X, RibbonPts[S + 1u].Y, RibbonPts[S + 1u].Z,
                                                 std::max(0.28f, W * 0.24f), Mix3(ColB, White, 0.55f),
                                                 RibAlpha * LeadU * 0.34f, true);
                }
            }

            // Depth-of-field Bokeh Disc (Dof > 0.62) OR Glowing Pinpoint Star Head (Dof <= 0.62)
            const float RawR  = (Dof > 0.62f)
                ? (3.2f + (Dof - 0.62f) * 16.5f) * (7.5f / std::max(2.4f, Head.Z)) * ScaleRatio
                : (1.0f + Dof * 1.75f) * (7.5f / std::max(2.4f, Head.Z)) * ScaleRatio;
            const float HeadR = std::clamp(RawR * HeadScale, 0.9f, 15.0f * ScaleRatio);
            const float BAlpha = std::min(1.0f, Env * PulseScale * 0.88f);
            RasterizeBokehDisc(Fb, Head.X, Head.Y, Head.Z - 0.01f, HeadR, ColC, ColB, BAlpha, Dof > 0.62f);
        }
        return;
    }

    // Modes 1 & 2: Curl Wave Field & Spherical Curl Plasma — luminous curved 3-vertex micro-filaments
    for (uint32_t I = 0u; I < static_cast<uint32_t>(Slots.size()); ++I)
    {
        const ParticleSlot& Slot = Slots[I];
        if (!IsParticleSlotAlive(Slot)) continue;
        const std::size_t Off = static_cast<std::size_t>(I) * VertsPerStrand;
        if (Off + 3u > StrandVertices.size()) continue;

        const float LifeFrac = std::clamp(Slot.AgeSeconds / std::max(0.1f, Slot.LifespanSeconds), 0.0f, 1.0f);
        const float Env = std::min(1.0f, LifeFrac / 0.14f) * std::min(1.0f, (1.0f - LifeFrac) / 0.22f);
        if (Env <= 0.01f) continue;

        const Vec3 H3 { StrandVertices[Off + 0u].PositionMeters[0], StrandVertices[Off + 0u].PositionMeters[1], StrandVertices[Off + 0u].PositionMeters[2] };
        const Vec3 M3 { StrandVertices[Off + 1u].PositionMeters[0], StrandVertices[Off + 1u].PositionMeters[1], StrandVertices[Off + 1u].PositionMeters[2] };
        const Vec3 T3 { StrandVertices[Off + 2u].PositionMeters[0], StrandVertices[Off + 2u].PositionMeters[1], StrandVertices[Off + 2u].PositionMeters[2] };

        float Hx, Hy, Hz, Mx, My, Mz, Tx, Ty, Tz;
        if (!Cam.Project(H3, Hx, Hy, Hz) || !Cam.Project(M3, Mx, My, Mz) || !Cam.Project(T3, Tx, Ty, Tz)) continue;

        const float Crest = std::clamp(Slot.AuxScalar, 0.0f, 1.0f);
        float Alpha = (0.12f + 0.42f * Crest) * Env;

        // Spherical Curl Plasma: attenuate back-hemisphere filaments for crisp 3D depth volume
        if (Spec.ManifoldMode == 2u)
        {
            const Vec3 ShellN = Norm3(H3 - Org);
            const Vec3 ToCam  = Norm3(Cam.Eye - H3);
            if (Dot3(ShellN, ToCam) < -0.06f) Alpha *= 0.26f;
            else                              Alpha *= (0.78f + 0.55f * Crest);
        }

        const Vec3 Col = (Crest < 0.52f)
            ? Mix3(ColA, ColC, Crest / 0.52f)
            : Mix3(ColC, ColB, (Crest - 0.52f) / 0.48f);

        const float W = std::clamp((Slot.SizeMeters * Spec.StreakThin * 165.0f) / std::max(1.8f, Hz) * ScaleRatio, 0.45f, 1.45f * ScaleRatio);
        RasterizeAdditiveSegment(Fb, Tx, Ty, Tz, Mx, My, Mz, W * 0.42f, Mix3(ColA, Col, 0.65f), Alpha * 0.45f, false);
        RasterizeAdditiveSegment(Fb, Mx, My, Mz, Hx, Hy, Hz, W * 0.52f, Col, Alpha * 0.72f, false);

        if (Crest > 0.58f)
        {
            RasterizeAdditiveSegment(Fb, Mx, My, Mz, Hx, Hy, Hz, std::max(0.28f, W * 0.22f), Mix3(ColB, White, 0.55f), Alpha * 0.48f, false);
        }
    }
}

/// Renders Orb-Weave Spiderweb & Messy Attic Cobwebs (20-vertex catenary silk + spinneret tip + morning dew beads).
void RenderSpiderwebRigIntoCanvas(
    PixelCanvas&                  Fb,
    const ResolvedCamera&         Cam,
    const ParticleRig&            Rig,
    std::span<const ParticleSlot> Slots,
    std::span<const StrandVertex> StrandVertices)
{
    if (!Rig.Dynamics.Spiderweb.has_value()) return;
    const SpiderwebWeaveSpec& Spec = *Rig.Dynamics.Spiderweb;

    Vec3 ColA {}, ColC {}, ColB {};
    ExtractGradientStops(Rig, ColA, ColC, ColB);

    uint32_t VertsPerStrand = kWebSplineVertexCount;
    if (const auto* Rib = std::get_if<RibbonStrandCarrier>(&Rig.Carrier))
        VertsPerStrand = std::max<uint32_t>(4u, Rib->VerticesPerStrand);

    const float ScaleRatio = static_cast<float>(Fb.Height) / 480.0f;
    const Vec3  White { 1.0f, 1.0f, 1.0f };

    struct ProjPt { float X, Y, Z; };
    std::vector<ProjPt> Pts {};
    Pts.reserve(VertsPerStrand);

    for (uint32_t I = 0u; I < static_cast<uint32_t>(Slots.size()); ++I)
    {
        const ParticleSlot& Slot = Slots[I];
        if (!IsParticleSlotAlive(Slot) || Slot.AuxScalar < 0.5f) continue;

        const std::size_t Off = static_cast<std::size_t>(I) * VertsPerStrand;
        if (Off + VertsPerStrand > StrandVertices.size()) continue;

        Pts.clear();
        for (uint32_t K = 0u; K < VertsPerStrand; ++K)
        {
            const auto& V = StrandVertices[Off + K];
            float Sx, Sy, Sz;
            if (Cam.Project({ V.PositionMeters[0], V.PositionMeters[1], V.PositionMeters[2] }, Sx, Sy, Sz))
                Pts.push_back({ Sx, Sy, Sz });
        }
        if (Pts.size() < 2u) continue;

        const float Role = std::clamp(Slot.OriginAnchor[0], 0.0f, 1.0f);
        const float Grow = std::clamp(Slot.OriginAnchor[1], 0.0f, 1.0f);
        const float Bead = std::clamp(Slot.OriginAnchor[2], 0.0f, 1.0f);

        const auto& Mid = Pts[Pts.size() / 2u];
        // Ultra-fine translucent gossamer spider silk matching HTML silver-blue sheen
        const float SilkPx = std::clamp((Spec.SilkWidthMeters * Spec.StreakThin * 155.0f) / std::max(1.8f, Mid.Z) * ScaleRatio, 0.28f, 0.68f * ScaleRatio);
        const float BaseA  = Spec.MessyAtticCobweb ? 0.22f : 0.34f;
        const float Alpha  = (Role < 0.18f) ? (BaseA * 1.18f) : ((Role < 0.45f) ? (BaseA * 0.96f) : (BaseA * 0.78f));

        const Vec3 SilkCol = (Role < 0.45f)
            ? Mix3(ColA, ColC, 0.35f + (Role / 0.45f) * 0.65f)
            : Mix3(ColC, ColB, (Role - 0.45f) / 0.55f);

        // Gossamer translucent catenary polyline (non-depth-blocking so overlapping attic cobweb drapes layer softly)
        for (std::size_t K = 0u; K + 1u < Pts.size(); ++K)
        {
            RasterizeScreenSegment(Fb, Pts[K].X, Pts[K].Y, Pts[K].Z,
                                       Pts[K + 1u].X, Pts[K + 1u].Y, Pts[K + 1u].Z,
                                       SilkPx * 0.52f, SilkCol * 0.72f, Alpha, false);
        }

        // Active extruded silk spinneret tip glow while spinning in
        if (Grow > 0.02f && Grow < 0.99f)
        {
            const auto& Tip = Pts.back();
            RasterizeRadialFlash(Fb, Tip.X, Tip.Y, Tip.Z - 0.01f, std::max(2.2f, SilkPx * 3.8f), White, 0.55f, false);
        }

        // Delicate morning dew droplets clinging along catenary midpoint
        if (Bead > 0.08f && Spec.DewHighlight > 0.05f)
        {
            const float DewR = std::clamp(SilkPx * (1.2f + Bead * 1.1f), 0.85f, 2.0f * ScaleRatio);
            const Vec3  DewCol = Mix3(ColC, White, 0.55f);
            RasterizeGlossyBeadDisc(Fb, Mid.X, Mid.Y, Mid.Z - 0.01f, DewR, DewCol, 0.55f * Spec.DewHighlight);
        }
    }
}

/// Renders Dielectric Lightning, Static Shock, Plasma Cage & Storm Lightning Grid (electrodes, cage, 3-layer arcs, contact flashes & sparks).
void RenderDielectricArcRigIntoCanvas(
    PixelCanvas&                  Fb,
    const ResolvedCamera&         Cam,
    const ParticleRig&            Rig,
    std::span<const ParticleSlot> Slots,
    const InteractiveColliderSpec& Collider,
    float                         ElapsedSeconds,
    bool                          ForcePeakDischarge)
{
    if (!Rig.Dynamics.DielectricArc.has_value()) return;
    const DielectricArcSpec& Spec = *Rig.Dynamics.DielectricArc;

    float Cathode[3] { 0.0f, 2.0f, 0.0f };
    float Anode[3]   { 3.0f, 2.0f, 0.0f };
    if (const auto* Elec = std::get_if<DualElectrodeOrigin>(&Rig.Origin))
    {
        for (int K = 0; K < 3; ++K)
        {
            Cathode[K] = Elec->CathodeMeters[K];
            Anode[K]   = Elec->AnodeMeters[K];
        }
    }
    else
    {
        ExtractOriginMeters(Rig.Origin, Cathode);
        Anode[0] = Cathode[0] + 3.2f; Anode[1] = Cathode[1]; Anode[2] = Cathode[2];
    }

    const DielectricArcGeometry Data = BuildDielectricArcGeometry(
        Rig, Spec, Cathode, Anode, Collider, ElapsedSeconds, ForcePeakDischarge);

    const float ScaleRatio = static_cast<float>(Fb.Height) / 480.0f;
    const Vec3  White { 1.0f, 1.0f, 1.0f };

    // 1. Structural cage perimeter conductor wires & bracing (Plasma Cage)
    for (const auto& Strut : Data.Conductors)
    {
        const Vec3 A { Strut.StartMeters[0], Strut.StartMeters[1], Strut.StartMeters[2] };
        const Vec3 B { Strut.EndMeters[0],   Strut.EndMeters[1],   Strut.EndMeters[2]   };
        const Vec3 C { Strut.LinearRgb[0],   Strut.LinearRgb[1],   Strut.LinearRgb[2]   };
        float X0, Y0, Z0, X1, Y1, Z1;
        if (Cam.Project(A, X0, Y0, Z0) && Cam.Project(B, X1, Y1, Z1))
        {
            const float HalfW = 1.1f * ScaleRatio;
            RasterizeScreenSegment(Fb, X0, Y0, Z0, X1, Y1, Z1, HalfW, C, Strut.Opacity, true);
        }
    }

    // 2. Shaded metallic electrode spheres + vertical collector posts + glowing equatorial & floor rings
    for (const auto& El : Data.Electrodes)
    {
        const Vec3 C { El.CentreMeters[0], El.CentreMeters[1], El.CentreMeters[2] };
        const Vec3 Base { El.ShellLinearRgb[0], El.ShellLinearRgb[1], El.ShellLinearRgb[2] };
        const Vec3 Ring { El.RingLinearRgb[0],  El.RingLinearRgb[1],  El.RingLinearRgb[2]  };

        if (El.VerticalPost && C.Y > 0.08f)
        {
            HardwareCylinderSegment Post {};
            Post.StartMeters[0]  = C.X; Post.StartMeters[1] = 0.0f; Post.StartMeters[2] = C.Z;
            Post.EndMeters[0]    = C.X; Post.EndMeters[1]   = C.Y;  Post.EndMeters[2]   = C.Z;
            Post.RadiusMeters    = std::max(0.035f, El.RadiusMeters * 0.24f);
            Post.ColourLinear[0] = 0.14f; Post.ColourLinear[1] = 0.17f; Post.ColourLinear[2] = 0.24f; Post.ColourLinear[3] = 1.0f;
            RasterizeLitCylinder(Fb, Cam, Post);
        }

        HardwareSphereAccent Sph {};
        Sph.CentreMeters[0] = C.X; Sph.CentreMeters[1] = C.Y; Sph.CentreMeters[2] = C.Z;
        Sph.RadiusMeters    = El.RadiusMeters;
        Sph.ColourLinear[0] = Base.X; Sph.ColourLinear[1] = Base.Y; Sph.ColourLinear[2] = Base.Z; Sph.ColourLinear[3] = 1.0f;
        RasterizeLitSphere(Fb, Cam, Sph);

        // Glowing equatorial corona ring around every electrode sphere
        constexpr int Segs = 32;
        const float EqR = El.RadiusMeters * 1.05f;
        for (int S = 0; S < Segs; ++S)
        {
            const float A0 = (static_cast<float>(S) / static_cast<float>(Segs)) * kTwoPi;
            const float A1 = (static_cast<float>(S + 1) / static_cast<float>(Segs)) * kTwoPi;
            const Vec3 P0 { C.X + std::cos(A0) * EqR, C.Y, C.Z + std::sin(A0) * EqR };
            const Vec3 P1 { C.X + std::cos(A1) * EqR, C.Y, C.Z + std::sin(A1) * EqR };
            float X0, Y0, Z0, X1, Y1, Z1;
            if (Cam.Project(P0, X0, Y0, Z0) && Cam.Project(P1, X1, Y1, Z1))
            {
                RasterizeAdditiveSegment(Fb, X0, Y0, Z0 - 0.03f, X1, Y1, Z1 - 0.03f, 0.9f * ScaleRatio, Ring, 0.58f, true);
            }
        }

        if (El.FloorRingEnabled)
        {
            HardwareRingAccent FlRing {};
            FlRing.CentreMeters[0] = C.X; FlRing.CentreMeters[1] = 0.02f; FlRing.CentreMeters[2] = C.Z;
            FlRing.RadiusMeters    = std::max(0.14f, El.RadiusMeters * 0.68f);
            FlRing.Segments        = 24u;
            FlRing.ColourLinear[0] = Ring.X; FlRing.ColourLinear[1] = Ring.Y; FlRing.ColourLinear[2] = Ring.Z; FlRing.ColourLinear[3] = 0.22f;
            RasterizeRing(Fb, Cam, FlRing);
        }
    }

    // 3. Three-layer razor-crisp additive lightning strokes (calibrated linear UV halo -> electric sheath -> white-hot filament core)
    for (const auto& Seg : Data.Segments)
    {
        const Vec3 A { Seg.StartMeters[0], Seg.StartMeters[1], Seg.StartMeters[2] };
        const Vec3 B { Seg.EndMeters[0],   Seg.EndMeters[1],   Seg.EndMeters[2]   };
        float X0, Y0, Z0, X1, Y1, Z1;
        if (!Cam.Project(A, X0, Y0, Z0) || !Cam.Project(B, X1, Y1, Z1)) continue;

        const float MidZ   = std::max(1.2f, 0.5f * (Z0 + Z1));
        const float AvgW   = 0.5f * (Seg.StartWidthM + Seg.EndWidthM);
        const float BasePx = std::clamp((AvgW * 340.0f) / MidZ * ScaleRatio, 0.55f, 2.2f * ScaleRatio);
        const Vec3  Sheath { Seg.LinearRgb[0], Seg.LinearRgb[1], Seg.LinearRgb[2] };
        const Vec3  Outer  { Sheath.X * 0.42f, Sheath.Y * 0.52f, std::min(1.0f, Sheath.Z * 1.15f) };
        const float Int    = std::clamp(Seg.Intensity, 0.05f, 2.0f);

        // Layer 1: Soft outer UV ionization halo
        RasterizeAdditiveSegment(Fb, X0, Y0, Z0 - 0.02f, X1, Y1, Z1 - 0.02f, BasePx * 1.95f, Outer, 0.048f * Int, false);
        // Layer 2: Electric cyan/violet plasma sheath
        RasterizeAdditiveSegment(Fb, X0, Y0, Z0 - 0.02f, X1, Y1, Z1 - 0.02f, BasePx * 0.82f, Sheath, 0.24f * Int, false);
        // Layer 3: Razor-sharp white-hot plasma core
        RasterizeAdditiveSegment(Fb, X0, Y0, Z0 - 0.02f, X1, Y1, Z1 - 0.02f, std::max(0.32f, BasePx * 0.26f), White, 0.90f * std::min(1.1f, Int), false);
    }

    // 4. Radial contact flashes & plasma orb corona (calibrated linear intensity)
    for (const auto& Fl : Data.Flashes)
    {
        const Vec3 P { Fl.CentreMeters[0], Fl.CentreMeters[1], Fl.CentreMeters[2] };
        float Sx, Sy, Sz;
        if (!Cam.Project(P, Sx, Sy, Sz)) continue;
        const float RadPx = std::clamp((Fl.RadiusMeters * 420.0f) / std::max(1.4f, Sz) * ScaleRatio, 5.0f, 44.0f * ScaleRatio);
        const Vec3  Col { Fl.LinearRgb[0], Fl.LinearRgb[1], Fl.LinearRgb[2] };
        RasterizeRadialFlash(Fb, Sx, Sy, Sz - 0.03f, RadPx, Col, Fl.Intensity * 0.42f, false);
    }

    // 5. Ionization spark motes dancing around live discharge channels
    Vec3 ColA {}, ColC {}, ColB {};
    ExtractGradientStops(Rig, ColA, ColC, ColB);
    for (const auto& Slot : Slots)
    {
        if (!IsParticleSlotAlive(Slot)) continue;
        const Vec3 P { Slot.PositionMeters[0], Slot.PositionMeters[1], Slot.PositionMeters[2] };
        float Sx, Sy, Sz;
        if (!Cam.Project(P, Sx, Sy, Sz)) continue;
        const float LifeFrac = std::clamp(Slot.AgeSeconds / std::max(0.05f, Slot.LifespanSeconds), 0.0f, 1.0f);
        const float Env      = (1.0f - LifeFrac);
        const float RadPx    = std::clamp(Slot.SizeMeters * 165.0f / std::max(1.6f, Sz) * ScaleRatio, 1.0f, 3.6f * ScaleRatio);
        RasterizeBokehDisc(Fb, Sx, Sy, Sz - 0.01f, RadPx, ColC, ColB, 0.65f * Env, false);
    }
}

void RenderRigIntoPixelCanvas(
    PixelCanvas&                  Fb,
    const ResolvedCamera&         Cam,
    const ParticleRig&            Rig,
    std::span<const ParticleSlot> Slots,
    std::span<const StrandVertex> StrandVertices)
{
    if (!Rig.Dynamics.Strand.has_value()) return;

    float OriginMeters[3] {};
    ExtractOriginMeters(Rig.Origin, OriginMeters);

    const StrandConstraintSpec& Spec = *Rig.Dynamics.Strand;
    Vec3 ColA {}, ColC {}, ColB {};
    ExtractGradientStops(Rig, ColA, ColC, ColB);

    // 1. Hardware props (pylons, flagpole + golden finial, ship mast + yardarm + boom + stays, doorway lintel + jambs)
    const StrandRiggingHardware Props = BuildStrandHardwareProps(Spec, OriginMeters);
    for (const auto& Ring : Props.Rings)     RasterizeRing(Fb, Cam, Ring);
    for (const auto& Cyl  : Props.Cylinders) RasterizeLitCylinder(Fb, Cam, Cyl);
    for (const auto& Sph  : Props.Spheres)   RasterizeLitSphere(Fb, Cam, Sph);

    const float ScaleRatio = static_cast<float>(Fb.Height) / 480.0f;

    // 2A. VARIANT 3 (Wind Flag & Full-Rigged Ship Sail): True Seamless 2D Quad Mesh with 3D Normal Lighting!
    if (Spec.StrandVariant == 3u)
    {
        const Vec3 SunDir = Norm3({ 0.42f, 0.58f, 0.69f });
        std::vector<uint32_t> ActiveRows {};
        for (uint32_t I = 0u; I < static_cast<uint32_t>(Slots.size()); ++I)
        {
            if (IsParticleSlotAlive(Slots[I]) && (Slots[I].PackedTraits & kParticleTraitStrandValidBit) != 0u)
            {
                ActiveRows.push_back(I);
            }
        }
        const uint32_t NRows = static_cast<uint32_t>(ActiveRows.size());
        for (uint32_t R = 0u; R + 1u < NRows; ++R)
        {
            const uint32_t S0Idx = ActiveRows[R];
            const uint32_t S1Idx = ActiveRows[R + 1u];
            const std::size_t Off0 = static_cast<std::size_t>(S0Idx) * kDynamicStrandVertexCount;
            const std::size_t Off1 = static_cast<std::size_t>(S1Idx) * kDynamicStrandVertexCount;
            if (Off0 + kDynamicStrandVertexCount > StrandVertices.size() || Off1 + kDynamicStrandVertexCount > StrandVertices.size()) continue;

            const float RowV = (NRows > 1u) ? (static_cast<float>(R) / static_cast<float>(NRows - 1u)) : 0.5f;
            for (uint32_t K = 0u; K + 1u < kDynamicStrandVertexCount; ++K)
            {
                const auto& V00 = StrandVertices[Off0 + K];
                const auto& V01 = StrandVertices[Off0 + K + 1u];
                const auto& V11 = StrandVertices[Off1 + K + 1u];
                const auto& V10 = StrandVertices[Off1 + K];
                if (V00.NormalisedSpanU < -0.5f || V01.NormalisedSpanU < -0.5f || V11.NormalisedSpanU < -0.5f || V10.NormalisedSpanU < -0.5f) continue;

                const Vec3 A3 { V00.PositionMeters[0], V00.PositionMeters[1], V00.PositionMeters[2] };
                const Vec3 B3 { V01.PositionMeters[0], V01.PositionMeters[1], V01.PositionMeters[2] };
                const Vec3 C3 { V11.PositionMeters[0], V11.PositionMeters[1], V11.PositionMeters[2] };
                const Vec3 D3 { V10.PositionMeters[0], V10.PositionMeters[1], V10.PositionMeters[2] };

                const Vec3 Nrm = Norm3(Cross3(B3 - A3, D3 - A3));
                const float Sun = std::fabs(Dot3(Nrm, SunDir));
                const float Lit = 0.48f + 0.58f * Sun;
                const float UFrac = static_cast<float>(K) / 19.0f;

                Vec3 ClothRgb = Mix3(ColA, ColC, std::min(1.0f, UFrac * 1.15f));
                if (Spec.SubMode == 0u && std::cos(RowV * kPi * 4.0f) > 0.0f)
                {
                    ClothRgb = Mix3(ClothRgb, ColB, 0.32f);
                }
                const Vec3 Shaded = ClothRgb * Lit;

                ScreenVert Sv[4] {};
                const Vec3 Quad[4] = { A3, B3, C3, D3 };
                bool Ok = true;
                for (int Q = 0; Q < 4; ++Q)
                {
                    float Z = 1.0f;
                    if (!Cam.Project(Quad[Q], Sv[Q].Sx, Sv[Q].Sy, Z)) { Ok = false; break; }
                    Sv[Q].InvZ = 1.0f / Z;
                    Sv[Q].Col  = Shaded;
                }
                if (!Ok) continue;

                RasterizeFlatTriangle(Fb, Sv[0], Sv[1], Sv[2], 0.98f, true);
                RasterizeFlatTriangle(Fb, Sv[0], Sv[2], Sv[3], 0.98f, true);
            }
        }
        return;
    }

    // 2B. VARIANT 4 (Crystal Bead Doorway Curtain): Thin Dark-Indigo Hanging Cords + 12 Glossy Shaded Crystal Beads!
    if (Spec.StrandVariant == 4u)
    {
        struct ProjectedVertex { float X, Y, Z, U; };
        for (std::size_t I = 0u; I < Slots.size(); ++I)
        {
            const ParticleSlot& Slot = Slots[I];
            if (!IsParticleSlotAlive(Slot) || (Slot.PackedTraits & kParticleTraitStrandValidBit) == 0u) continue;
            const std::size_t Off = I * kDynamicStrandVertexCount;
            if (Off + kDynamicStrandVertexCount > StrandVertices.size()) continue;

            std::vector<ProjectedVertex> Pts {};
            Pts.reserve(kDynamicStrandVertexCount);
            for (uint32_t K = 0u; K < kDynamicStrandVertexCount; ++K)
            {
                const auto& V = StrandVertices[Off + K];
                if (V.NormalisedSpanU < -0.5f) continue;
                float Sx, Sy, Sz;
                if (Cam.Project({ V.PositionMeters[0], V.PositionMeters[1], V.PositionMeters[2] }, Sx, Sy, Sz))
                {
                    Pts.push_back({ Sx, Sy, Sz, static_cast<float>(K) / 19.0f });
                }
            }
            if (Pts.size() < 2u) continue;

            const float MidZ = Pts[Pts.size() / 2u].Z;
            const float RefPx = std::max(1.5f, (Slot.SizeMeters * Rig.Kinematics.SizeMultiplier * 240.0f) / std::max(1.8f, MidZ)) * ScaleRatio;
            const float CordHalfW = std::max(0.55f, RefPx * 0.12f);

            // Hanging indigo cord
            for (std::size_t K = 0u; K + 1u < Pts.size(); ++K)
            {
                RasterizeScreenSegment(Fb, Pts[K].X, Pts[K].Y, Pts[K].Z, Pts[K + 1u].X, Pts[K + 1u].Y, Pts[K + 1u].Z, CordHalfW, ColA * 1.45f, 0.78f, true);
            }

            // 12 crisp glossy spherical crystal beads along each vertical cord
            const float BeadRad = std::max(2.2f, RefPx * 0.72f);
            for (int B = 1; B <= 12; ++B)
            {
                const std::size_t K = std::min<std::size_t>(
                    Pts.size() - 1u,
                    static_cast<std::size_t>(std::lround((static_cast<float>(B) / 12.0f) * static_cast<float>(Pts.size() - 1u))));
                const auto& Bp = Pts[K];
                Vec3 BCol = Mix3(Mix3(ColA, ColC, Bp.U), ColB, (Bp.U > 0.55f) ? 0.35f : 0.0f);
                RasterizeGlossyBeadDisc(Fb, Bp.X, Bp.Y, Bp.Z, BeadRad, BCol, 0.95f);
            }
        }
        return;
    }

    // 2C. VARIANTS 1 & 2 (Swinging Cables/Hollow Oval Chain Links & Hanging Leafy Vines)
    const float Thin = std::max(0.10f, Spec.WidthScale);
    for (std::size_t I = 0u; I < Slots.size(); ++I)
    {
        const ParticleSlot& Slot = Slots[I];
        if (!IsParticleSlotAlive(Slot) || (Slot.PackedTraits & kParticleTraitStrandValidBit) == 0u) continue;

        if ((Slot.PackedTraits & kParticleTraitStrandLeafBit) != 0u)
        {
            RasterizeDelicateIvyLeaf(Fb, Cam, Slot, ColC, ColB);
            continue;
        }

        const std::size_t Off = I * kDynamicStrandVertexCount;
        if (Off + kDynamicStrandVertexCount > StrandVertices.size()) continue;
        const std::span<const StrandVertex> Nodes = StrandVertices.subspan(Off, kDynamicStrandVertexCount);

        constexpr int SubSegs = 16;
        struct SplinePt { float X, Y, Z, U; };
        std::vector<SplinePt> Curve {};
        Curve.reserve(static_cast<std::size_t>(SubSegs + 1));
        for (int S = 0; S <= SubSegs; ++S)
        {
            const float U  = static_cast<float>(S) / static_cast<float>(SubSegs);
            const Vec3 Wp  = EvalCatmull(Nodes, 0u, kDynamicStrandVertexCount - 1u, U);
            float Sx, Sy, Sz;
            if (Cam.Project(Wp, Sx, Sy, Sz)) Curve.push_back({ Sx, Sy, Sz, U });
        }
        if (Curve.size() < 2u) continue;

        const float ZAvg  = Curve[Curve.size() / 2u].Z;
        const float RefPx = std::max(1.0f, (Slot.SizeMeters * Rig.Kinematics.SizeMultiplier * Thin * 260.0f) / std::max(1.8f, ZAvg)) * ScaleRatio;

        if (Spec.StrandVariant == 1u && Spec.SubMode >= 1u)
        {
            // Exact SoftwarePreviewEngine.js hollow interlocking oval metallic chain links!
            const float LinkStroke = std::max(1.4f, RefPx * 0.42f);
            const float LinkMinor  = std::max(1.8f, RefPx * 0.55f);
            for (std::size_t K = 0u; K + 1u < Curve.size(); ++K)
            {
                const auto& A = Curve[K];
                const auto& B = Curve[K + 1u];
                const float Mx  = 0.5f * (A.X + B.X);
                const float My  = 0.5f * (A.Y + B.Y);
                const float Mz  = 0.5f * (A.Z + B.Z);
                const float Ang = std::atan2(B.Y - A.Y, B.X - A.X);
                const float Major = std::max(3.0f, std::hypot(B.X - A.X, B.Y - A.Y) * 0.62f);
                const Vec3 LnkCol = ((K % 2u) == 0u) ? ColB : ColC;
                RasterizeHollowChainOval(Fb, Mx, My, Mz, Ang, Major, LinkMinor, LinkStroke, LnkCol);
            }
        }
        else
        {
            // Slender braided hanging emerald vines / steel cables (exact SoftwarePreviewEngine.js stroke)
            const float HalfW = std::max(0.65f, RefPx * 0.48f);
            for (std::size_t K = 0u; K + 1u < Curve.size(); ++K)
            {
                const auto& A = Curve[K];
                const auto& B = Curve[K + 1u];
                const Vec3 SegCol = Mix3(ColA, ColC, std::min(1.0f, A.U * 1.15f));
                RasterizeScreenSegment(Fb, A.X, A.Y, A.Z, B.X, B.Y, B.Z, HalfW, SegCol, 0.95f, true);
            }
        }
    }
}

// ============================================================================================================
// Motion & Grids Z-Buffered 3D Effector Renderer (Controlled Integrator Vehicle + Kinetic Hex Torus + Halftone)
// ============================================================================================================

inline Vec3 ShadeObsidianSurfacePbr(
    Vec3  WorldPos,
    Vec3  NormalUnit,
    Vec3  CamEye,
    Vec3  AccentMid,
    Vec3  AccentPeak,
    uint32_t MaterialSlotCode,
    EffectorBackingFinish Finish) noexcept
{
    Vec3 N = Norm3(NormalUnit);
    const Vec3 V = Norm3(CamEye - WorldPos);
    if (Dot3(N, V) < 0.0f) N = N * -1.0f;

    const Vec3 KeyL  = Norm3({  0.42f, 0.86f,  0.46f });
    const Vec3 FillL = Norm3({ -0.68f, 0.52f, -0.44f });
    const Vec3 RimL  = Norm3({  0.12f, 0.35f, -0.92f });

    const float NdotV = std::clamp(Dot3(N, V), 0.0f, 1.0f);
    const float NdotK = std::max(0.0f, Dot3(N, KeyL));
    const float NdotF = std::max(0.0f, Dot3(N, FillL));
    const Vec3  Refl  = Norm3(N * (2.0f * NdotV) - V);

    const float Hk = std::max(0.0f, Dot3(N, Norm3(KeyL + V)));
    const float Hf = std::max(0.0f, Dot3(N, Norm3(FillL + V)));
    const float Hr = std::max(0.0f, Dot3(N, Norm3(RimL + V)));
    const float Fres = std::pow(1.0f - NdotV, 3.0f);

    // Overhead twin studio softbox reflection bands (matches HTML studioChromeEnv wet-obsidian PBR)
    const float Softbox1 = SmoothStepInv(0.55f, 0.88f, Refl.Y) * SmoothStepInv(-0.20f, 0.55f, Refl.X);
    const float Softbox2 = SmoothStepInv(0.42f, 0.78f, Refl.Y) * SmoothStepInv(0.15f, -0.65f, Refl.Z) * 0.55f;
    const float HorizonBand = std::exp(-std::pow((Refl.Y - 0.06f) * 6.5f, 2.0f));

    if (Finish == EffectorBackingFinish::BrushedChrome)
    {
        const Vec3 ChromeBase { 0.24f, 0.27f, 0.32f };
        const Vec3 Spec = Vec3{ 0.96f, 0.98f, 1.00f } * (std::pow(Hk, 32.0f) * 1.1f + Softbox1 * 0.55f + Softbox2 * 0.32f);
        return ChromeBase * (0.28f + 0.65f * NdotK + 0.30f * NdotF) + Spec + AccentPeak * (Fres * 0.22f);
    }

    // Material-aware glossy obsidian automotive finish for Controlled Integrator (or pure obsidian for Torus/Sphere)
    if (MaterialSlotCode == 13u)
    {
        // Front muscle-car fascia grille: sinister stealth smoked obsidian satin metallic
        const Vec3 GrilleBase { 0.014f, 0.016f, 0.024f };
        return GrilleBase * (0.45f + 0.85f * NdotK + 0.30f * NdotF)
               + Vec3{ 0.34f, 0.40f, 0.52f } * (std::pow(Hk, 42.0f) * 0.55f + Fres * 0.28f)
               + AccentMid * (Fres * 0.16f);
    }
    if (MaterialSlotCode == 12u || MaterialSlotCode == 15u)
    {
        // Stealth smoked trim recess & black anodized plate housing
        const Vec3 TrimBase { 0.014f, 0.016f, 0.022f };
        return TrimBase * (0.40f + 0.80f * NdotK) + Vec3{ 0.16f, 0.20f, 0.26f } * (std::pow(Hk, 28.0f) * 0.35f + Fres * 0.22f);
    }
    if (MaterialSlotCode == 14u)
    {
        // Deep ruby-red smoked LED taillight signature bar
        return Vec3{ 0.92f, 0.035f, 0.045f } * (0.58f + 0.35f * NdotV);
    }
    if (MaterialSlotCode == 9u)
    {
        // Matte satin vulcanized rubber tyre (deep rich charcoal)
        const Vec3 RubberBase { 0.014f, 0.015f, 0.018f };
        return RubberBase * (0.42f + 0.85f * NdotK + 0.32f * NdotF) + Vec3{ 0.045f, 0.050f, 0.062f } * (std::pow(Hk, 18.0f) * 0.32f + Fres * 0.38f);
    }
    if (MaterialSlotCode == 10u || MaterialSlotCode == 11u)
    {
        // Forged dark titanium alloy rims & ceramic brake rotor
        const Vec3 AlloyBase { 0.042f, 0.048f, 0.062f };
        return AlloyBase * (0.35f + 0.92f * NdotK + 0.45f * NdotF)
               + Vec3{ 0.65f, 0.72f, 0.84f } * (std::pow(Hk, 48.0f) * 0.88f + std::pow(Hf, 28.0f) * 0.32f)
               + AccentMid * (Fres * 0.16f);
    }
    if (MaterialSlotCode == 7u)
    {
        // Ultra-dark mirror-tinted obsidian cabin glass
        const Vec3 GlassBase { 0.004f, 0.006f, 0.010f };
        return GlassBase
               + Vec3{ 0.74f, 0.84f, 0.98f } * (Softbox1 * 0.42f + Softbox2 * 0.20f + std::pow(Hk, 110.0f) * 1.05f)
               + Vec3{ 0.12f, 0.17f, 0.26f } * (Fres * 0.75f + HorizonBand * 0.15f)
               + AccentMid * (Fres * 0.10f);
    }

    // Hero glossy liquid-obsidian sculpted clearcoat (BodyCoat / Torus / Sphere)
    const Vec3 DeepObsidian { 0.008f, 0.010f, 0.015f };
    const Vec3 DiffuseTerm  = DeepObsidian * (0.50f + 0.85f * NdotK + 0.38f * NdotF);
    const Vec3 StudioRefl   = Vec3{ 0.48f, 0.56f, 0.70f } * (Softbox1 * 0.26f + Softbox2 * 0.14f)
                            + Vec3{ 0.15f, 0.19f, 0.28f } * (HorizonBand * 0.18f)
                            + Vec3{ 0.90f, 0.94f, 1.00f } * (std::pow(Hk, 96.0f) * 0.68f + std::pow(Hf, 48.0f) * 0.22f + std::pow(Hr, 36.0f) * 0.26f);
    const Vec3 RimTint      = Mix3({ 0.10f, 0.14f, 0.22f }, AccentMid, 0.28f) * (Fres * 0.40f)
                            + AccentPeak * (Fres * Softbox1 * 0.10f);
    return DiffuseTerm + StudioRefl + RimTint;
}

void RasterizeWorldLitTriangle(
    PixelCanvas&          Fb,
    const ResolvedCamera& Cam,
    Vec3                  Pa,
    Vec3                  Pb,
    Vec3                  Pc,
    Vec3                  Na,
    Vec3                  Nb,
    Vec3                  Nc,
    Vec3                  AccentMid,
    Vec3                  AccentPeak,
    uint32_t              MaterialSlotCode,
    EffectorBackingFinish Finish) noexcept
{
    // Backface cull in world space against camera ray (skip culling only if thin/degenerate)
    const Vec3 FaceN = Cross3(Pb - Pa, Pc - Pa);
    const Vec3 Cent  = (Pa + Pb + Pc) * (1.0f / 3.0f);
    if (Dot3(FaceN, Cam.Eye - Cent) < -1e-5f) return;

    float Sxa, Sya, Sza, Sxb, Syb, Szb, Sxc, Syc, Szc;
    if (!Cam.Project(Pa, Sxa, Sya, Sza) || !Cam.Project(Pb, Sxb, Syb, Szb) || !Cam.Project(Pc, Sxc, Syc, Szc))
    {
        return;
    }

    ScreenVert Va { Sxa, Sya, 1.0f / std::max(0.08f, Sza), ShadeObsidianSurfacePbr(Pa, Na, Cam.Eye, AccentMid, AccentPeak, MaterialSlotCode, Finish) };
    ScreenVert Vb { Sxb, Syb, 1.0f / std::max(0.08f, Szb), ShadeObsidianSurfacePbr(Pb, Nb, Cam.Eye, AccentMid, AccentPeak, MaterialSlotCode, Finish) };
    ScreenVert Vc { Sxc, Syc, 1.0f / std::max(0.08f, Szc), ShadeObsidianSurfacePbr(Pc, Nc, Cam.Eye, AccentMid, AccentPeak, MaterialSlotCode, Finish) };
    RasterizeFlatTriangle(Fb, Va, Vb, Vc, 1.0f, true);
}

void RasterizeSolidFacetedTriangle(
    PixelCanvas&          Fb,
    const ResolvedCamera& Cam,
    Vec3                  Pa,
    Vec3                  Pb,
    Vec3                  Pc,
    Vec3                  FaceNormal,
    Vec3                  BaseLinearCol,
    float                 EffectorGlow,
    float                 Alpha) noexcept
{
    const Vec3 Cent = (Pa + Pb + Pc) * (1.0f / 3.0f);
    const Vec3 V    = Norm3(Cam.Eye - Cent);
    Vec3 N = Norm3(FaceNormal);
    if (Dot3(N, V) < 0.0f) return; // Backface cull closed 3D prisms & cubes cleanly!

    float Sxa, Sya, Sza, Sxb, Syb, Szb, Sxc, Syc, Szc;
    if (!Cam.Project(Pa, Sxa, Sya, Sza) || !Cam.Project(Pb, Sxb, Syb, Szb) || !Cam.Project(Pc, Sxc, Syc, Szc))
    {
        return;
    }

    const Vec3 L1 = Norm3({  0.48f, 0.82f,  0.40f });
    const Vec3 L2 = Norm3({ -0.60f, 0.45f, -0.52f });
    const float Ndot1 = std::max(0.0f, Dot3(N, L1));
    const float Ndot2 = std::max(0.0f, Dot3(N, L2));
    const float Spec  = std::pow(std::max(0.0f, Dot3(N, Norm3(L1 + V))), 32.0f);
    const float Fres  = std::pow(1.0f - std::clamp(Dot3(N, V), 0.0f, 1.0f), 2.5f);

    // Chroma-preserving studio lighting (rich saturated crimson / molten gold / electric cyan without pastel blowout)
    const float Shade = 0.22f + 0.62f * Ndot1 + 0.18f * Ndot2 + 0.26f * EffectorGlow;
    const Vec3  Lit   = BaseLinearCol * Shade
                      + Mix3(BaseLinearCol, Vec3{ 1.00f, 0.98f, 0.95f }, 0.45f) * (Spec * (0.18f + 0.22f * EffectorGlow))
                      + BaseLinearCol * (Fres * (0.18f + 0.26f * EffectorGlow));

    const float DepthBias = 0.003f;
    ScreenVert Va { Sxa, Sya, 1.0f / std::max(0.08f, Sza - DepthBias), Lit };
    ScreenVert Vb { Sxb, Syb, 1.0f / std::max(0.08f, Szb - DepthBias), Lit };
    ScreenVert Vc { Sxc, Syc, 1.0f / std::max(0.08f, Szc - DepthBias), Lit };
    RasterizeFlatTriangle(Fb, Va, Vb, Vc, Alpha, true);
}

void RasterizeOrientedBox3D(
    PixelCanvas&          Fb,
    const ResolvedCamera& Cam,
    Vec3                  BaseCenter,
    Vec3                  AxisT,
    Vec3                  AxisN,
    Vec3                  AxisB,
    float                 HalfW,
    float                 HeightH,
    Vec3                  ColLin,
    float                 EffectorGlow,
    float                 Alpha) noexcept
{
    const Vec3 T = AxisT * HalfW;
    const Vec3 B = AxisB * HalfW;
    const Vec3 H = AxisN * HeightH;

    // 8 corners: 0..3 base ring, 4..7 top ring (CCW seen from +AxisN)
    const Vec3 P[8] = {
        BaseCenter - T - B,
        BaseCenter + T - B,
        BaseCenter + T + B,
        BaseCenter - T + B,
        BaseCenter - T - B + H,
        BaseCenter + T - B + H,
        BaseCenter + T + B + H,
        BaseCenter - T + B + H,
    };

    const Vec3 TopCol  = ColLin * (1.06f + 0.22f * EffectorGlow);
    const Vec3 SideCol = ColLin * 0.90f;

    // Top face (+N)
    RasterizeSolidFacetedTriangle(Fb, Cam, P[4], P[5], P[6], AxisN, TopCol, EffectorGlow, Alpha);
    RasterizeSolidFacetedTriangle(Fb, Cam, P[4], P[6], P[7], AxisN, TopCol, EffectorGlow, Alpha);
    // 4 Side walls
    RasterizeSolidFacetedTriangle(Fb, Cam, P[0], P[1], P[5], AxisB * -1.0f, SideCol, EffectorGlow, Alpha);
    RasterizeSolidFacetedTriangle(Fb, Cam, P[0], P[5], P[4], AxisB * -1.0f, SideCol, EffectorGlow, Alpha);
    RasterizeSolidFacetedTriangle(Fb, Cam, P[1], P[2], P[6], AxisT,         SideCol, EffectorGlow, Alpha);
    RasterizeSolidFacetedTriangle(Fb, Cam, P[1], P[6], P[5], AxisT,         SideCol, EffectorGlow, Alpha);
    RasterizeSolidFacetedTriangle(Fb, Cam, P[2], P[3], P[7], AxisB,         SideCol, EffectorGlow, Alpha);
    RasterizeSolidFacetedTriangle(Fb, Cam, P[2], P[7], P[6], AxisB,         SideCol, EffectorGlow, Alpha);
    RasterizeSolidFacetedTriangle(Fb, Cam, P[3], P[0], P[4], AxisT * -1.0f, SideCol, EffectorGlow, Alpha);
    RasterizeSolidFacetedTriangle(Fb, Cam, P[3], P[4], P[7], AxisT * -1.0f, SideCol, EffectorGlow, Alpha);
}

void RasterizeOrientedHexPrism3D(
    PixelCanvas&          Fb,
    const ResolvedCamera& Cam,
    Vec3                  BaseCenter,
    Vec3                  AxisT,
    Vec3                  AxisN,
    Vec3                  AxisB,
    float                 HexRadius,
    float                 ExtrudeHeight,
    Vec3                  ColLin,
    float                 EffectorGlow,
    float                 Alpha) noexcept
{
    constexpr float CosHex[6] = { 1.0f,  0.5f, -0.5f, -1.0f, -0.5f,  0.5f };
    constexpr float SinHex[6] = { 0.0f,  0.8660254f,  0.8660254f,  0.0f, -0.8660254f, -0.8660254f };

    const Vec3 TopCenter = BaseCenter + AxisN * ExtrudeHeight;
    const float TopBevelR = HexRadius * 0.90f; // Crisp subtle chamfered hex top edge!

    Vec3 BaseRing[6] {};
    Vec3 TopRing[6]  {};
    for (int K = 0; K < 6; ++K)
    {
        const Vec3 Radial = AxisT * CosHex[K] + AxisB * SinHex[K];
        BaseRing[K] = BaseCenter + Radial * HexRadius;
        TopRing[K]  = TopCenter  + Radial * TopBevelR;
    }

    const Vec3 CapCol  = ColLin * (1.08f + 0.25f * EffectorGlow);
    const Vec3 WallCol = ColLin * 0.82f;

    for (int K = 0; K < 6; ++K)
    {
        const int Next = (K + 1) % 6;
        // Top hexagonal cap triangle
        RasterizeSolidFacetedTriangle(Fb, Cam, TopCenter, TopRing[K], TopRing[Next], AxisN, CapCol, EffectorGlow, Alpha);

        // Beveled side prism wall (2 triangles)
        const Vec3 MidRadial = Norm3(AxisT * (CosHex[K] + CosHex[Next]) + AxisB * (SinHex[K] + SinHex[Next]) + AxisN * 0.28f);
        RasterizeSolidFacetedTriangle(Fb, Cam, BaseRing[K], BaseRing[Next], TopRing[Next], MidRadial, WallCol, EffectorGlow * 0.85f, Alpha);
        RasterizeSolidFacetedTriangle(Fb, Cam, BaseRing[K], TopRing[Next],  TopRing[K],    MidRadial, WallCol, EffectorGlow * 0.85f, Alpha);
    }
}

float EvaluateSignedDistEquilateralTri(float U, float V) noexcept
{
    constexpr float K = 1.7320508f;
    float Px = std::fabs(U) - 1.0f;
    float Py = V + 1.0f / K;
    if (Px + K * Py > 0.0f)
    {
        const float Nx = (Px - K * Py) * 0.5f;
        const float Ny = (-K * Px - Py) * 0.5f;
        Px = Nx;
        Py = Ny;
    }
    Px -= std::clamp(Px, -2.0f, 0.0f);
    return -std::sqrt(Px * Px + Py * Py) * (Py < 0.0f ? -1.0f : 1.0f);
}

float EvaluateSignedDistHex2D(float U, float V) noexcept
{
    const float Ax = std::fabs(U);
    const float Ay = std::fabs(V);
    return std::max(Ax * 0.8660254f + Ay * 0.5f, Ay) - 0.8660254f;
}

void RasterizeSurfaceSdfPlate(
    PixelCanvas&           Fb,
    const ResolvedCamera&  Cam,
    Vec3                   Center,
    Vec3                   AxisT,
    Vec3                   AxisN,
    Vec3                   AxisB,
    float                  HalfSpan,
    EffectorPrimitiveShape Shape,
    Vec3                   ColLin,
    float                  EffectorGlow,
    float                  Alpha) noexcept
{
    // Backface cull surface-conforming plates facing away from camera
    const Vec3 ToCam = Norm3(Cam.Eye - Center);
    const float Facing = Dot3(AxisN, ToCam);
    if (Facing < -0.08f) return;

    const Vec3 T = AxisT * HalfSpan;
    const Vec3 B = AxisB * HalfSpan;
    const Vec3 Corners[4] = {
        Center - T - B,
        Center + T - B,
        Center + T + B,
        Center - T + B,
    };

    float Sx[4] {}, Sy[4] {}, Sz[4] {};
    for (int K = 0; K < 4; ++K)
    {
        if (!Cam.Project(Corners[K], Sx[K], Sy[K], Sz[K])) return;
    }

    const int Ix0 = std::clamp(static_cast<int>(std::floor(std::min({ Sx[0], Sx[1], Sx[2], Sx[3] }))), 0, Fb.Width - 1);
    const int Ix1 = std::clamp(static_cast<int>(std::ceil(std::max({ Sx[0], Sx[1], Sx[2], Sx[3] }))), 0, Fb.Width - 1);
    const int Iy0 = std::clamp(static_cast<int>(std::floor(std::min({ Sy[0], Sy[1], Sy[2], Sy[3] }))), 0, Fb.Height - 1);
    const int Iy1 = std::clamp(static_cast<int>(std::ceil(std::max({ Sy[0], Sy[1], Sy[2], Sy[3] }))), 0, Fb.Height - 1);
    if (Ix0 > Ix1 || Iy0 > Iy1) return;

    const float L1Dot = std::max(0.0f, Dot3(AxisN, Norm3({ 0.48f, 0.82f, 0.40f })));
    const float LightGain = 0.55f + 0.52f * L1Dot + 0.40f * EffectorGlow;

    const int TriIdx[2][3] = { { 0, 1, 2 }, { 0, 2, 3 } };
    const float QuadU[4] = { -1.0f,  1.0f, 1.0f, -1.0f };
    const float QuadV[4] = { -1.0f, -1.0f, 1.0f,  1.0f };

    for (int Ti = 0; Ti < 2; ++Ti)
    {
        const int Ia = TriIdx[Ti][0], Ib = TriIdx[Ti][1], Ic = TriIdx[Ti][2];
        const float Area = (Sx[Ib] - Sx[Ia]) * (Sy[Ic] - Sy[Ia]) - (Sy[Ib] - Sy[Ia]) * (Sx[Ic] - Sx[Ia]);
        if (std::fabs(Area) < 1e-4f) continue;
        const float InvArea = 1.0f / Area;
        const float IzA = 1.0f / std::max(0.08f, Sz[Ia]);
        const float IzB = 1.0f / std::max(0.08f, Sz[Ib]);
        const float IzC = 1.0f / std::max(0.08f, Sz[Ic]);

        for (int Y = Iy0; Y <= Iy1; ++Y)
        {
            const float Py = static_cast<float>(Y) + 0.5f;
            for (int X = Ix0; X <= Ix1; ++X)
            {
                const float Px = static_cast<float>(X) + 0.5f;
                const float W0 = ((Sx[Ib] - Px) * (Sy[Ic] - Py) - (Sy[Ib] - Py) * (Sx[Ic] - Px)) * InvArea;
                const float W1 = ((Sx[Ic] - Px) * (Sy[Ia] - Py) - (Sy[Ic] - Py) * (Sx[Ia] - Px)) * InvArea;
                const float W2 = 1.0f - W0 - W1;
                if (W0 < -1e-4f || W1 < -1e-4f || W2 < -1e-4f) continue;

                const float InvZ = W0 * IzA + W1 * IzB + W2 * IzC;
                if (InvZ <= 1e-6f) continue;
                const float ViewZ = (1.0f / InvZ) - 0.005f;

                const float P0 = (W0 * IzA) / InvZ;
                const float P1 = (W1 * IzB) / InvZ;
                const float P2 = (W2 * IzC) / InvZ;
                const float U  = QuadU[Ia] * P0 + QuadU[Ib] * P1 + QuadU[Ic] * P2;
                const float V  = QuadV[Ia] * P0 + QuadV[Ib] * P1 + QuadV[Ic] * P2;

                float Mask = 0.0f;
                float DomeShade = 1.0f;
                if (Shape == EffectorPrimitiveShape::FlatSquareTile)
                {
                    const float Sq = std::max(std::fabs(U), std::fabs(V));
                    if (Sq > 0.94f) continue;
                    Mask = SmoothStepInv(0.94f, 0.72f, Sq);
                    DomeShade = 0.86f + 0.26f * (1.0f - Sq);
                }
                else if (Shape == EffectorPrimitiveShape::TrianglePlate)
                {
                    const float Td = EvaluateSignedDistEquilateralTri(U * 1.12f, V * 1.12f);
                    if (Td > 0.0f) continue;
                    Mask = SmoothStepInv(0.0f, -0.16f, Td);
                    DomeShade = 0.88f + 0.25f * std::clamp(-Td * 2.0f, 0.0f, 1.0f);
                }
                else if (Shape == EffectorPrimitiveShape::HexagonPlate)
                {
                    const float Hd = EvaluateSignedDistHex2D(U * 1.06f, V * 1.06f);
                    if (Hd > 0.0f) continue;
                    Mask = SmoothStepInv(0.0f, -0.15f, Hd);
                    DomeShade = 0.86f + 0.28f * std::clamp(-Hd * 2.0f, 0.0f, 1.0f);
                }
                else
                {
                    // Antialiased glowing halftone circle disc
                    const float R2 = U * U + V * V;
                    if (R2 > 1.0f) continue;
                    const float R = std::sqrt(R2);
                    Mask = SmoothStepInv(1.0f, 0.70f, R);
                    DomeShade = 0.82f + 0.34f * std::sqrt(std::max(0.0f, 1.0f - R2));
                }

                const Vec3 OutCol = ColLin * (DomeShade * LightGain) + Vec3{ 0.14f, 0.04f, 0.06f } * (EffectorGlow * 0.45f);
                Fb.BlendPixel(X, Y, ViewZ, OutCol, Alpha * Mask, true);
            }
        }
    }
}

void RasterizeEffectorVolumeWireframe(
    PixelCanvas&                    Fb,
    const ResolvedCamera&           Cam,
    const EffectorVolumeDescriptor& Vol,
    Vec3                            TintLin) noexcept
{
    if (Vol.RadiusMeters <= 0.05f) return;
    const Vec3 C { Vol.CenterM[0], Vol.CenterM[1], Vol.CenterM[2] };
    const float R = Vol.RadiusMeters;

    if (Vol.Shape == EffectorVolumeShape::Box)
    {
        const Vec3 B[8] = {
            { C.X - R, C.Y - R, C.Z - R }, { C.X + R, C.Y - R, C.Z - R },
            { C.X + R, C.Y + R, C.Z - R }, { C.X - R, C.Y + R, C.Z - R },
            { C.X - R, C.Y - R, C.Z + R }, { C.X + R, C.Y - R, C.Z + R },
            { C.X + R, C.Y + R, C.Z + R }, { C.X - R, C.Y + R, C.Z + R },
        };
        constexpr int Edges[12][2] = {
            {0,1},{1,2},{2,3},{3,0},{4,5},{5,6},{6,7},{7,4},{0,4},{1,5},{2,6},{3,7}
        };
        for (const auto& E : Edges)
        {
            float X0, Y0, Z0, X1, Y1, Z1;
            if (Cam.Project(B[E[0]], X0, Y0, Z0) && Cam.Project(B[E[1]], X1, Y1, Z1))
            {
                RasterizeAdditiveSegment(Fb, X0, Y0, Z0, X1, Y1, Z1, 1.15f, TintLin, 0.36f, true);
            }
        }
        return;
    }

    // 3 orthogonal latitude/longitude rings for Sphere / Torus / Disc
    constexpr int Segs = 32;
    for (int Plane = 0; Plane < 3; ++Plane)
    {
        for (int S = 0; S < Segs; ++S)
        {
            const float A0 = (static_cast<float>(S) / static_cast<float>(Segs)) * kTwoPi;
            const float A1 = (static_cast<float>(S + 1) / static_cast<float>(Segs)) * kTwoPi;
            auto RingPt = [&](float A) -> Vec3 {
                if (Plane == 0) return { C.X + std::cos(A) * R, C.Y, C.Z + std::sin(A) * R };
                if (Plane == 1) return { C.X + std::cos(A) * R, C.Y + std::sin(A) * R, C.Z };
                return { C.X, C.Y + std::sin(A) * R, C.Z + std::cos(A) * R };
            };
            float X0, Y0, Z0, X1, Y1, Z1;
            if (Cam.Project(RingPt(A0), X0, Y0, Z0) && Cam.Project(RingPt(A1), X1, Y1, Z1))
            {
                RasterizeAdditiveSegment(Fb, X0, Y0, Z0, X1, Y1, Z1, 1.05f, TintLin, 0.30f, true);
            }
        }
    }
}

void RenderEffectorGridRigIntoCanvas(
    PixelCanvas&                  Fb,
    const ResolvedCamera&         Cam,
    const ParticleRig&            Rig,
    std::span<const ParticleSlot> Slots,
    float                         ElapsedSeconds)
{
    if (!Rig.Dynamics.Effector.has_value()) return;
    const EffectorLatticeSpec& Spec = *Rig.Dynamics.Effector;

    Vec3 Org { 0.0f, 0.0f, 0.0f };
    if (const auto* Surf = std::get_if<SurfaceAnchorOrigin>(&Rig.Origin))
        Org = { Surf->CenterMeters[0], Surf->CenterMeters[1], Surf->CenterMeters[2] };
    else if (const auto* Lat = std::get_if<SurfaceLatticeOrigin>(&Rig.Origin))
        Org = { Lat->CenterMeters[0], Lat->CenterMeters[1], Lat->CenterMeters[2] };
    else if (const auto* Box = std::get_if<AnalyticBoxOrigin>(&Rig.Origin))
        Org = { Box->CenterMeters[0], Box->CenterMeters[1], Box->CenterMeters[2] };

    const Vec3 AccentMid  { Spec.MidColorLinear[0],  Spec.MidColorLinear[1],  Spec.MidColorLinear[2]  };
    const Vec3 AccentPeak { Spec.PeakColorLinear[0], Spec.PeakColorLinear[1], Spec.PeakColorLinear[2] };

    // 1. Rasterize the sculpted 3D obsidian host geometry into the shared Z-buffer first for exact occlusion!
    if (Spec.BackingFinish != EffectorBackingFinish::HiddenBacking)
    {
        if (Spec.HostSurface == EffectorHostSurfaceKind::ControlVehicle)
        {
            const auto& Vehicle = QueryControlVehicleGeometrySet();
            const float Scale   = std::max(0.20f, Spec.OriginExtentM[0]);
            const float YawRad  = Spec.VehicleYawDegrees * (kPi / 180.0f);
            const float CosY    = std::cos(YawRad);
            const float SinY    = std::sin(YawRad);

            auto XformPos = [&](const float P[3]) -> Vec3 {
                const float Sx = P[0] * Scale;
                const float Sy = P[1] * Scale;
                const float Sz = P[2] * Scale;
                return { Org.X + Sx * CosY + Sz * SinY, Org.Y + Sy, Org.Z - Sx * SinY + Sz * CosY };
            };
            auto XformNorm = [&](const float N[3]) -> Vec3 {
                return Norm3({ N[0] * CosY + N[2] * SinY, N[1], -N[0] * SinY + N[2] * CosY });
            };

            // Ground ambient contact shadow ellipse beneath the Controlled Integrator
            constexpr int ShadowSegs = 28;
            const float ShRx = Vehicle.HalfLengthM * Scale * 0.96f;
            const float ShRz = 1.32f * Scale;
            const Vec3 ShC { Org.X, Org.Y + 0.004f, Org.Z };
            for (int S = 0; S < ShadowSegs; ++S)
            {
                const float A0 = (static_cast<float>(S) / static_cast<float>(ShadowSegs)) * kTwoPi;
                const float A1 = (static_cast<float>(S + 1) / static_cast<float>(ShadowSegs)) * kTwoPi;
                auto ShPt = [&](float A) -> Vec3 {
                    const float Lx = std::cos(A) * ShRx;
                    const float Lz = std::sin(A) * ShRz;
                    return { Org.X + Lx * CosY + Lz * SinY, Org.Y + 0.004f, Org.Z - Lx * SinY + Lz * CosY };
                };
                float Cx, Cy, Cz, X0, Y0, Z0, X1, Y1, Z1;
                if (Cam.Project(ShC, Cx, Cy, Cz) && Cam.Project(ShPt(A0), X0, Y0, Z0) && Cam.Project(ShPt(A1), X1, Y1, Z1))
                {
                    ScreenVert Vc { Cx, Cy, 1.0f / std::max(0.08f, Cz), { 0.002f, 0.003f, 0.005f } };
                    ScreenVert V0 { X0, Y0, 1.0f / std::max(0.08f, Z0), { 0.005f, 0.007f, 0.010f } };
                    ScreenVert V1 { X1, Y1, 1.0f / std::max(0.08f, Z1), { 0.005f, 0.007f, 0.010f } };
                    RasterizeFlatTriangle(Fb, Vc, V0, V1, 0.68f, false);
                }
            }

            // Full 25,068-triangle Controlled Integrator (Body + 4 Rims + 4 Tyres) into Z-buffer
            for (const auto& Tri : Vehicle.Triangles)
            {
                RasterizeWorldLitTriangle(
                    Fb, Cam,
                    XformPos(Tri.PositionA), XformPos(Tri.PositionB), XformPos(Tri.PositionC),
                    XformNorm(Tri.NormalA),  XformNorm(Tri.NormalB),  XformNorm(Tri.NormalC),
                    AccentMid, AccentPeak, Tri.MaterialSlotCode, Spec.BackingFinish);
            }
        }
        else if (Spec.HostSurface == EffectorHostSurfaceKind::TorusRing)
        {
            // Smooth analytic Wet-Obsidian 3D Torus matching Kinetic Hex Torus Matrix
            constexpr int SegU = 64;
            constexpr int SegV = 32;
            const float Rmaj = std::max(0.5f, Spec.OriginExtentM[0]) * 0.992f;
            const float Rmin = Rmaj * 0.376f;
            const float Rev  = ElapsedSeconds * 0.22f;

            auto TorusPt = [&](int Iu, int Iv, Vec3& OutN) -> Vec3 {
                const float Phi   = (static_cast<float>(Iu) / static_cast<float>(SegU)) * kTwoPi + Rev;
                const float Theta = (static_cast<float>(Iv) / static_cast<float>(SegV)) * kTwoPi;
                const float Rad   = Rmaj + Rmin * std::cos(Theta);
                OutN = { std::cos(Phi) * std::cos(Theta), std::sin(Theta), std::sin(Phi) * std::cos(Theta) };
                return { Org.X + std::cos(Phi) * Rad, Org.Y + std::sin(Theta) * Rmin, Org.Z + std::sin(Phi) * Rad };
            };

            for (int U = 0; U < SegU; ++U)
            {
                for (int V = 0; V < SegV; ++V)
                {
                    Vec3 N00, N10, N11, N01;
                    const Vec3 P00 = TorusPt(U,     V,     N00);
                    const Vec3 P10 = TorusPt(U + 1, V,     N10);
                    const Vec3 P11 = TorusPt(U + 1, V + 1, N11);
                    const Vec3 P01 = TorusPt(U,     V + 1, N01);
                    RasterizeWorldLitTriangle(Fb, Cam, P00, P01, P11, N00, N01, N11, AccentMid, AccentPeak, 6u, Spec.BackingFinish);
                    RasterizeWorldLitTriangle(Fb, Cam, P00, P11, P10, N00, N11, N10, AccentMid, AccentPeak, 6u, Spec.BackingFinish);
                }
            }
        }
        else if (Spec.HostSurface == EffectorHostSurfaceKind::ObsidianSphere)
        {
            constexpr int Rings = 28;
            constexpr int Sectors = 48;
            const float Rad = std::max(0.35f, Spec.OriginExtentM[0]) * 0.992f;
            auto SphPt = [&](int R, int S, Vec3& OutN) -> Vec3 {
                const float Theta = (static_cast<float>(R) / static_cast<float>(Rings)) * kPi;
                const float Phi   = (static_cast<float>(S) / static_cast<float>(Sectors)) * kTwoPi;
                OutN = { std::sin(Theta) * std::cos(Phi), std::cos(Theta), std::sin(Theta) * std::sin(Phi) };
                return Org + OutN * Rad;
            };
            for (int R = 0; R < Rings; ++R)
            {
                for (int S = 0; S < Sectors; ++S)
                {
                    Vec3 N00, N10, N11, N01;
                    const Vec3 P00 = SphPt(R,     S,     N00);
                    const Vec3 P10 = SphPt(R + 1, S,     N10);
                    const Vec3 P11 = SphPt(R + 1, S + 1, N11);
                    const Vec3 P01 = SphPt(R,     S + 1, N01);
                    RasterizeWorldLitTriangle(Fb, Cam, P00, P10, P11, N00, N10, N11, AccentMid, AccentPeak, 6u, Spec.BackingFinish);
                    RasterizeWorldLitTriangle(Fb, Cam, P00, P11, P01, N00, N11, N01, AccentMid, AccentPeak, 6u, Spec.BackingFinish);
                }
            }
        }
    }

    // 2. Rasterize every depth-tested surface-conforming Effector primitive (3D Hex Plates, Extruded Cubes, Raised Tiles, Halftone Dots)
    for (const auto& Slot : Slots)
    {
        if (!IsParticleSlotAlive(Slot)) continue;
        if (Slot.SizeMeters <= 0.001f || Slot.ColourLinear[3] <= 0.01f) continue;

        const Vec3 Pos { Slot.PositionMeters[0], Slot.PositionMeters[1], Slot.PositionMeters[2] };
        const Vec3 N   = Norm3({ Slot.VelocityMps[0], Slot.VelocityMps[1], Slot.VelocityMps[2] });
        // Align primitive tangent frame smoothly along longitudinal car +X axis (or world Y on Torus/Sphere) for sleek panel alignment
        Vec3 Ref {};
        if (Spec.HostSurface == EffectorHostSurfaceKind::ControlVehicle)
        {
            Ref = (std::fabs(N.X) > 0.88f) ? Vec3{ 0.0f, 0.0f, 1.0f } : Vec3{ 1.0f, 0.0f, 0.0f };
        }
        else
        {
            Ref = (std::fabs(N.Y) > 0.92f) ? Vec3{ 1.0f, 0.0f, 0.0f } : Vec3{ 0.0f, 1.0f, 0.0f };
        }
        const Vec3 T   = Norm3(Cross3(Ref, N));
        const Vec3 B   = Cross3(N, T);

        const Vec3  Col { Slot.ColourLinear[0], Slot.ColourLinear[1], Slot.ColourLinear[2] };
        const float Alpha    = std::clamp(Slot.ColourLinear[3], 0.0f, 1.0f);
        const float W        = std::clamp(Slot.AuxScalar, 0.0f, 1.0f);
        const float ExtrudeH = Slot.SpinRadians;
        const auto  Shape    = Spec.PrimitiveShape;

        if (Shape == EffectorPrimitiveShape::HexagonPlate)
        {
            RasterizeOrientedHexPrism3D(Fb, Cam, Pos, T, N, B, Slot.SizeMeters * 0.92f, std::max(0.012f, ExtrudeH), Col, W, Alpha);
        }
        else if (Shape == EffectorPrimitiveShape::HeightExtrudedCube ||
                 Shape == EffectorPrimitiveShape::UniformBox ||
                 (Shape == EffectorPrimitiveShape::AutoHalftoneUpperTiles && ExtrudeH > 0.002f))
        {
            RasterizeOrientedBox3D(Fb, Cam, Pos, T, N, B, Slot.SizeMeters * 0.52f, std::max(0.010f, ExtrudeH), Col, W, Alpha);
        }
        else
        {
            const EffectorPrimitiveShape SdfShape =
                (Shape == EffectorPrimitiveShape::AutoHalftoneUpperTiles) ? EffectorPrimitiveShape::HalftoneCircle : Shape;
            RasterizeSurfaceSdfPlate(Fb, Cam, Pos, T, N, B, Slot.SizeMeters * 0.72f, SdfShape, Col, W, Alpha);
        }
    }

    // 3. Subtle translucent 3D gizmo outlines when Placeable 3D Effectors are active
    if (Spec.Driver == EffectorDriverCategory::Placeable3DEffectors || Spec.BlendPlaceable)
    {
        RasterizeEffectorVolumeWireframe(Fb, Cam, Spec.PrimaryVolume,   { 0.96f, 0.78f, 0.24f });
        RasterizeEffectorVolumeWireframe(Fb, Cam, Spec.SecondaryVolume, { 0.14f, 0.84f, 0.42f });
    }
}

} // namespace

void RenderDynamicStrandRigRgba8(
    const ParticleRig&            Rig,
    std::span<const ParticleSlot> Slots,
    std::span<const StrandVertex> StrandVertices,
    const StrandCameraSpec&       Camera,
    int                           Width,
    int                           Height,
    std::vector<uint8_t>&         OutRgba8,
    float                         ElapsedSeconds,
    bool                          ForcePeakArcDischarge)
{
    const DynamicStrandRigView View { &Rig, Slots, StrandVertices, ElapsedSeconds, ForcePeakArcDischarge, {} };
    RenderDynamicStrandSceneRgba8(std::span<const DynamicStrandRigView>(&View, 1u), Camera, Width, Height, OutRgba8);
}

void RenderDynamicStrandSceneRgba8(
    std::span<const DynamicStrandRigView> Rigs,
    const StrandCameraSpec&               Camera,
    int                                   Width,
    int                                   Height,
    std::vector<uint8_t>&                 OutRgba8)
{
    PixelCanvas Fb(Width, Height);
    const ResolvedCamera Cam = BuildResolvedCamera(Camera, Width, Height);
    FloorGridEnabled(Fb, Cam, Camera.FocusMeters[0], Camera.FocusMeters[2]);

    for (const auto& Entry : Rigs)
    {
        if (Entry.Rig == nullptr || !Entry.Rig->Visible) continue;
        const auto& D = Entry.Rig->Dynamics;
        if (D.Strand.has_value())
        {
            RenderRigIntoPixelCanvas(Fb, Cam, *Entry.Rig, Entry.Slots, Entry.StrandVertices);
        }
        else if (D.Curl.has_value())
        {
            RenderCurlStreamRigIntoCanvas(Fb, Cam, *Entry.Rig, Entry.Slots, Entry.StrandVertices);
        }
        else if (D.Spiderweb.has_value())
        {
            RenderSpiderwebRigIntoCanvas(Fb, Cam, *Entry.Rig, Entry.Slots, Entry.StrandVertices);
        }
        else if (D.DielectricArc.has_value())
        {
            RenderDielectricArcRigIntoCanvas(Fb, Cam, *Entry.Rig, Entry.Slots, Entry.Collider, Entry.ElapsedSeconds, Entry.ForcePeakArcDischarge);
        }
        else if (D.Effector.has_value())
        {
            RenderEffectorGridRigIntoCanvas(Fb, Cam, *Entry.Rig, Entry.Slots, Entry.ElapsedSeconds);
        }
    }

    Fb.ExportSrgbRgba8(OutRgba8);
}

} // namespace Frontier::ParticleSpace
