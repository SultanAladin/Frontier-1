/*============================================================================================================================================
                                                         PARTICLESTRANDRENDERER.CPP
============================================================================================================================================*/
// 🧩 Perspective Z-buffered 3D rasterizer for dynamic strands (hollow chain links, slender leafy vines, seamless quad-mesh flags/sails & beads)

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/ParticleSpace/Simulation/ParticleStrandRenderer.h"

#include "Engine/ParticleSpace/Dynamics/DynamicStrandSolver.h"

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
    const Vec3 MinorCol { 0.022f, 0.028f, 0.038f };
    const Vec3 MajorCol { 0.044f, 0.056f, 0.072f };

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
    else if (const auto* Box = std::get_if<AnalyticBoxOrigin>(&Origin))
        for (int K = 0; K < 3; ++K) Out[K] = Box->CenterMeters[K];
    else if (const auto* Cloth = std::get_if<ClothGridOrigin>(&Origin))
        for (int K = 0; K < 3; ++K) Out[K] = Cloth->OriginMeters[K];
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

} // namespace

void RenderDynamicStrandRigRgba8(
    const ParticleRig&            Rig,
    std::span<const ParticleSlot> Slots,
    std::span<const StrandVertex> StrandVertices,
    const StrandCameraSpec&       Camera,
    int                           Width,
    int                           Height,
    std::vector<uint8_t>&         OutRgba8)
{
    const DynamicStrandRigView View { &Rig, Slots, StrandVertices };
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
        if (Entry.Rig != nullptr && Entry.Rig->Visible)
        {
            RenderRigIntoPixelCanvas(Fb, Cam, *Entry.Rig, Entry.Slots, Entry.StrandVertices);
        }
    }

    Fb.ExportSrgbRgba8(OutRgba8);
}

} // namespace Frontier::ParticleSpace
