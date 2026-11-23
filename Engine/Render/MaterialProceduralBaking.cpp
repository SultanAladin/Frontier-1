/*============================================================================================================================================
                                                          MATERIALPROCEDURALBAKING.CPP
============================================================================================================================================*/
// 🧩 Exact CPU evaluation of the Alloy noise and scratch kernels; shared maps keep every baked channel consistent.

#include "Engine/Render/MaterialProceduralBaking.h"
#include <stb_image_write.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>
#include <unordered_map>

namespace Frontier
{
namespace
{
float ProceduralSmoothstep(float Edge0, float Edge1, float X)
{
    const float Span = Edge1 - Edge0;
    const float T = Span != 0 ? std::clamp((X - Edge0) / Span, 0.f, 1.f) : (X < Edge0 ? 0.f : 1.f);
    return T * T * (3 - 2 * T);
}

float ProceduralMix(float A, float B, float T) { return A + (B - A) * T; }
}

float ProceduralHash31(float X, float Y, float Z)
{
    // hash31: p=fract(p*.1031); p+=dot(p,p.yzx+33.33); fract((p.x+p.y)*p.z).
    X = X * .1031f; Y = Y * .1031f; Z = Z * .1031f;
    X -= std::floor(X); Y -= std::floor(Y); Z -= std::floor(Z);
    const float Dot = X * (Y + 33.33f) + Y * (Z + 33.33f) + Z * (X + 33.33f);
    X += Dot; Y += Dot; Z += Dot;
    const float Hash = (X + Y) * Z;
    return Hash - std::floor(Hash);
}

float ProceduralNoise3(float X, float Y, float Z)
{
    const float Ix = std::floor(X), Iy = std::floor(Y), Iz = std::floor(Z);
    float Fx = X - Ix, Fy = Y - Iy, Fz = Z - Iz;
    Fx = Fx * Fx * (3 - 2 * Fx); Fy = Fy * Fy * (3 - 2 * Fy); Fz = Fz * Fz * (3 - 2 * Fz);
    const float X00 = ProceduralMix(ProceduralHash31(Ix, Iy, Iz), ProceduralHash31(Ix + 1, Iy, Iz), Fx);
    const float X10 = ProceduralMix(ProceduralHash31(Ix, Iy + 1, Iz), ProceduralHash31(Ix + 1, Iy + 1, Iz), Fx);
    const float X01 = ProceduralMix(ProceduralHash31(Ix, Iy, Iz + 1), ProceduralHash31(Ix + 1, Iy, Iz + 1), Fx);
    const float X11 = ProceduralMix(ProceduralHash31(Ix, Iy + 1, Iz + 1), ProceduralHash31(Ix + 1, Iy + 1, Iz + 1), Fx);
    return ProceduralMix(ProceduralMix(X00, X10, Fy), ProceduralMix(X01, X11, Fy), Fz);
}

float ProceduralMirror(float T)
{
    // Triangle wave: seamless mirrored tiling for isotropic blotches (spangle, oxide).
    T -= std::floor(T);
    return T < .5f ? T * 2 : 2 - T * 2;
}

float ProceduralStoneFbm(float X, float Y, float Z)
{
    return .5333f * ProceduralNoise3(X, Y, Z) + .2667f * ProceduralNoise3(X * 2.03f + 7.1f, Y * 2.03f + 7.1f, Z * 2.03f + 7.1f) +
        .1333f * ProceduralNoise3(X * 4.17f + 19.3f, Y * 4.17f + 19.3f, Z * 4.17f + 19.3f) +
        .0667f * ProceduralNoise3(X * 8.31f + 41.f, Y * 8.31f + 41.f, Z * 8.31f + 41.f);
}

namespace
{
// 📝 One scratch per (layer, cell): every per-instance quantity (centre, direction, length, width
// jitter, depth) is a pure function of the cell id, yet a full-image bake re-derives it for every
// pixel in the 5x5 neighbourhood — hundreds of hash/trig evaluations per pixel. Resolving each
// instance once per bake is float-exact (identical expressions, identical order) and removes that
// factor; the reference entry point below resolves inline so both paths can be diffed bit-for-bit.
struct ScratchInstance
{
    bool Occupied = false;
    float A = 0, B = 0, CenterX = 0, CenterY = 0, DirectionX = 0, DirectionY = 0;
    float Length = 0, WidthJitter = 0, Depth = 0;
};
ScratchInstance ResolveScratchInstance(float IdX, float IdY, float LayerSeed, float Occupancy,
    const ScratchFieldParameters &Parameters)
{
    constexpr float Pi = 3.14159265f;
    ScratchInstance Instance;
    if (!(ProceduralHash31(IdX, IdY, LayerSeed) < Occupancy)) return Instance;
    Instance.Occupied = true;
    Instance.A = ProceduralHash31(IdX + 17.3f, IdY + 17.3f, LayerSeed + 3);
    Instance.B = ProceduralHash31(IdX + 39.7f, IdY + 39.7f, LayerSeed + 11);
    Instance.CenterX = IdX + .08f + .84f * Instance.A;
    Instance.CenterY = IdY + .08f + .84f * Instance.B;
    const float Angle = Parameters.AngleRadians +
        (ProceduralHash31(IdX + 81.2f, IdY + 81.2f, LayerSeed) - .5f) * Pi * Parameters.Spread;
    Instance.DirectionX = std::cos(Angle);
    Instance.DirectionY = std::sin(Angle);
    Instance.Length = Parameters.Length * ProceduralMix(.18f, 1, Instance.A * Instance.A);
    Instance.WidthJitter = ProceduralMix(.35f, 1.4f, ProceduralHash31(IdX - 31.1f, IdY - 31.1f, LayerSeed));
    Instance.Depth = Parameters.Depth * ProceduralMix(.35f, 1, Instance.B);
    return Instance;
}
template <typename ResolveInstance>
ScratchFieldSample EvaluateScratchFieldCore(float U, float V, const ScratchFieldParameters &Parameters,
    float PixelUv, ResolveInstance &&Resolve)
{
    ScratchFieldSample Sample;
    if (!(Parameters.Density > 0)) return Sample;
    constexpr float Pi = 3.14159265f;
    U *= Parameters.Scale; V *= Parameters.Scale;
    const float CellX = std::floor(U), CellY = std::floor(V);
    const float AntiAlias = std::max(PixelUv * .4f, .00015f);
    float Coverage = 0, Height = 0, RimTotal = 0;
    for (int Layer = 0; Layer < 12; ++Layer)
    {
        const float Batch = float(Layer / 4) * 4;
        if (Batch >= Parameters.Density) break;
        const float LayerSeed = float(Layer) * 113.7f + float(Parameters.Seed);
        const float Occupancy = std::clamp((Parameters.Density - Batch) * .25f, 0.f, 1.f);
        for (int Y = -2; Y <= 2; ++Y)
            for (int X = -2; X <= 2; ++X)
            {
                const float IdX = CellX + float(X), IdY = CellY + float(Y);
                const ScratchInstance Instance = Resolve(Layer, IdX, IdY, LayerSeed, Occupancy);
                if (!Instance.Occupied) continue;
                const float A = Instance.A, B = Instance.B;
                const float DeltaX = U - Instance.CenterX, DeltaY = V - Instance.CenterY;
                const float Length = Instance.Length;
                const float T = (DeltaX * Instance.DirectionX + DeltaY * Instance.DirectionY) / std::max(Length, .001f) + .5f;
                // 📝 Outside the segment the taper is exactly zero, so cut/bowl/lip all vanish;
                // skipping the remaining transcendental work is bit-exact for finite inputs.
                if (T <= 0 || T >= 1) continue;
                const float Along = std::clamp(T, 0.f, 1.f);
                const float Bend = Parameters.Bend * (B - .5f) * 2 * Length / std::max(Parameters.Length, .001f);
                const float Bow = (std::sin(Along * Pi) + .2f * std::sin(Along * 2 * Pi + A * 2) * std::sin(Along * Pi)) * Bend;
                const float Distance = std::abs(-DeltaX * Instance.DirectionY + DeltaY * Instance.DirectionX - Bow);
                const float Taper = ProceduralSmoothstep(0, .12f, T) * (1 - ProceduralSmoothstep(.84f, 1, T));
                const float Width = Parameters.Width * Instance.WidthJitter *
                    (.2f + .8f * std::sqrt(std::max(Taper, 0.f)));
                const float Depth = Instance.Depth;
                const float LipArgument = (Distance - Width * 1.65f) / std::max(Width * .45f, AntiAlias);
                // 📝 Beyond the cut edge and the lip gaussian's float range every contribution is
                // exactly zero (exp(-121) underflows float), so the pow/exp/smoothsteps are skipped.
                if (Distance >= Width + AntiAlias && LipArgument >= 11.f) continue;
                const float Cut = (1 - ProceduralSmoothstep(Width - AntiAlias, Width + AntiAlias, Distance)) * Taper *
                    std::min(1.f, Width / AntiAlias);
                // 📝 The pow base clamps at max(Width,.0001), so beyond that radius the bowl is
                // exactly zero and the pow can be skipped without changing a single bit.
                float Bowl = (Distance >= std::max(Width, .0001f) ? 0.f :
                    std::pow(std::max(0.f, 1 - Distance / std::max(Width, .0001f)), 1.3f)) * Taper;
                Bowl = ProceduralMix(Bowl, Cut * .5f, ProceduralSmoothstep(Width * .4f, Width * 1.5f, AntiAlias));
                const float Lip = std::exp(-LipArgument * LipArgument) * Taper * std::min(1.f, Width / AntiAlias);
                Height = std::min(Height, -Depth * Bowl);
                RimTotal = std::max(RimTotal, Lip * Depth * .16f);
                Coverage = std::max(Coverage, Cut);
            }
    }
    const float Resolved = 1 - ProceduralSmoothstep(.3f, 1.2f, PixelUv);
    Sample.Coverage = ProceduralMix(Parameters.Density * Parameters.Width * Parameters.Length * .75f, Coverage, Resolved);
    Sample.Height = (Height + RimTotal) * Resolved;
    Sample.Rim = RimTotal;
    return Sample;
}
}

ScratchFieldSample EvaluateScratchField(float U, float V, const ScratchFieldParameters &Parameters, float PixelUv)
{
    return EvaluateScratchFieldCore(U, V, Parameters, PixelUv,
        [&](int, float IdX, float IdY, float LayerSeed, float Occupancy)
        { return ResolveScratchInstance(IdX, IdY, LayerSeed, Occupancy, Parameters); });
}

ScratchBakeMaps BakeScratchMaps(const ScratchFieldParameters &Parameters, uint32_t Extent)
{
    if (!Extent || Extent > 2048) throw std::runtime_error("Scratch bake extent is unbounded.");
    ScratchBakeMaps Maps;
    Maps.Extent = Extent;
    Maps.Height.assign(size_t(Extent) * Extent, 0);
    Maps.Coverage.assign(size_t(Extent) * Extent, 0);
    const float PixelUv = Parameters.Scale / float(Extent);
    // 📝 Bake-wide instance memo: at most 12 layers x (Scale+4)^2 cells ever resolve, versus
    // Extent^2 x 25 neighbourhood visits. Cell ids are exact integers in float for any sane scale;
    // anything outside the packed range falls back to direct resolution (still exact).
    std::unordered_map<uint64_t, ScratchInstance> Instances;
    const auto Resolve = [&](int Layer, float IdX, float IdY, float LayerSeed, float Occupancy)
    {
        if (!(std::abs(IdX) < 1000000.f) || !(std::abs(IdY) < 1000000.f))
            return ResolveScratchInstance(IdX, IdY, LayerSeed, Occupancy, Parameters);
        const uint64_t Key = (uint64_t(uint32_t(Layer)) << 42) |
            (uint64_t(uint32_t(int32_t(IdX)) & 0x1FFFFFu) << 21) |
            uint64_t(uint32_t(int32_t(IdY)) & 0x1FFFFFu);
        const auto Existing = Instances.find(Key);
        if (Existing != Instances.end()) return Existing->second;
        const auto Instance = ResolveScratchInstance(IdX, IdY, LayerSeed, Occupancy, Parameters);
        Instances.emplace(Key, Instance);
        return Instance;
    };
    for (uint32_t Y = 0; Y < Extent; ++Y)
        for (uint32_t X = 0; X < Extent; ++X)
        {
            const auto Sample = EvaluateScratchFieldCore((float(X) + .5f) / float(Extent), (float(Y) + .5f) / float(Extent),
                Parameters, PixelUv, Resolve);
            Maps.Height[size_t(Y) * Extent + X] = Sample.Height;
            Maps.Coverage[size_t(Y) * Extent + X] = Sample.Coverage;
        }
    return Maps;
}

std::vector<uint8_t> ConvertHeightToNormalRgba(const std::vector<float> &Relief, uint32_t Extent, float Strength)
{
    if (!Extent || Relief.size() != size_t(Extent) * Extent) throw std::runtime_error("Normal source is not a square relief field.");
    std::vector<uint8_t> Rgba(size_t(Extent) * Extent * 4);
    for (uint32_t Y = 0; Y < Extent; ++Y)
        for (uint32_t X = 0; X < Extent; ++X)
        {
            const uint32_t Left = X > 0 ? X - 1 : 0, Right = X + 1 < Extent ? X + 1 : Extent - 1;
            const uint32_t Up = Y > 0 ? Y - 1 : 0, Down = Y + 1 < Extent ? Y + 1 : Extent - 1;
            const float Gx = (Relief[size_t(Y) * Extent + Right] - Relief[size_t(Y) * Extent + Left]) * .5f;
            const float Gy = (Relief[size_t(Down) * Extent + X] - Relief[size_t(Up) * Extent + X]) * .5f;
            const float Nx = -Gx * Strength, Ny = -Gy * Strength;
            const float Inverse = 1 / std::sqrt(Nx * Nx + Ny * Ny + 1);
            uint8_t *Pixel = &Rgba[(size_t(Y) * Extent + X) * 4];
            Pixel[0] = uint8_t(std::lround((Nx * Inverse * .5f + .5f) * 255));
            Pixel[1] = uint8_t(std::lround((Ny * Inverse * .5f + .5f) * 255));
            Pixel[2] = uint8_t(std::lround((Inverse * .5f + .5f) * 255));
            Pixel[3] = 255;
        }
    return Rgba;
}

std::vector<uint8_t> ConvertScratchOrmRgba(const std::vector<float> &Coverage, uint32_t Extent, float BaseRoughness)
{
    if (!Extent || Coverage.size() != size_t(Extent) * Extent || !(BaseRoughness > 0) || !(BaseRoughness <= 1))
        throw std::runtime_error("ORM source is not a square coverage field over a unit base roughness.");
    // 📝 ORM green multiplies the authored scalar, so it cannot lift roughness above the base. The authored
    // scalar carries the shader's +0.3 ceiling and green divides by it: field pixels reproduce the base exactly,
    // cuts reach the ceiling exactly, matching the Alloy mix on both ends.
    const float Ceiling = std::min(1.f, BaseRoughness + .3f);
    std::vector<uint8_t> Rgba(size_t(Extent) * Extent * 4);
    for (uint32_t Y = 0; Y < Extent; ++Y)
        for (uint32_t X = 0; X < Extent; ++X)
        {
            const float Mask = std::clamp(Coverage[size_t(Y) * Extent + X], 0.f, 1.f);
            uint8_t *Pixel = &Rgba[(size_t(Y) * Extent + X) * 4];
            Pixel[0] = uint8_t(std::lround(std::clamp(1 - .11f * Mask, 0.f, 1.f) * 255));
            Pixel[1] = uint8_t(std::lround(std::clamp(ProceduralMix(BaseRoughness, Ceiling, Mask) / Ceiling, 0.f, 1.f) * 255));
            Pixel[2] = 255;
            Pixel[3] = 255;
        }
    return Rgba;
}

namespace
{
float ProceduralFract(float X) { return X - std::floor(X); }

// 📝 GLSL mod floors toward negative infinity, unlike fmod; twill/houndstooth addressing needs the match.
float ProceduralMod(float X, float Y) { return X - Y * std::floor(X / Y); }

float ProceduralStep(float Edge, float X) { return X >= Edge ? 1.f : 0.f; }

// 📝 Alloy bakeSRGB: linear working values encode to sRGB bytes for the albedo upload slot.
float ProceduralLinearToSrgb(float Linear)
{
    const float Clamped = std::max(Linear, 0.f);
    return Clamped <= .0031308f ? Clamped * 12.92f : 1.055f * std::pow(Clamped, 1 / 2.4f) - .055f;
}

struct TwillSample
{
    float Light = 0, Height = 0, Direction = 0, Fiber = 0;
};

// 📝 Verbatim twill(): rotated cell addressing, per-pattern over/under masks, rounded-bundle arch with
// inter-yarn gaps, sub-fiber filtering and the resolved fade. Step is fwidth(uv) in scaled UV per texel.
TwillSample EvaluateTwill(float U, float V, const WeaveFieldParameters &P, float Step)
{
    const float C = std::cos(P.AngleRadians), S = std::sin(P.AngleRadians);
    const float Su = (C * U + S * V) * P.Scale, Sv = (-S * U + C * V) * P.Scale;
    const float CellX = std::floor(Su), CellY = std::floor(Sv);
    const float Fx = Su - CellX, Fy = Sv - CellY;
    float Over = ProceduralStep(2, ProceduralMod(CellX - CellY, 4));
    if (P.Type == 4)
    {
        if (P.Weave == 0) Over = ProceduralMod(CellX + CellY, 2);
        if (P.Weave == 2) Over = ProceduralStep(4, ProceduralMod(CellX + 2 * CellY, 5));
        if (P.Weave == 3) Over = ProceduralMod(std::floor(CellX * .5f) + std::floor(CellY * .5f), 2);
        if (P.Weave == 4) Over = ProceduralMod(CellX + std::floor(CellY * .5f), 2);
        if (P.Weave == 5)
        {
            const float Reverse = ProceduralMod(CellX, 16);
            Over = ProceduralStep(2, ProceduralMod((Reverse < 8 ? Reverse : 15 - Reverse) - CellY, 4));
        }
        if (P.Weave == 6) Over = ProceduralMod(std::floor(CellX * .5f) + CellY, 2);
        if (P.Weave == 8) Over = ProceduralStep(3, ProceduralMod(CellX - CellY, 4));
    }
    const float Across = ProceduralMix(Fx, Fy, Over), Coord = ProceduralMix(Su, Sv, Over);
    const float Edge = std::min(Across, 1 - Across);
    const float Chord = Across * 2 - 1;
    const float Bundle = std::sqrt(std::max(0.f, 1 - Chord * Chord));
    const float Gap = ProceduralSmoothstep(.008f, .045f, Edge);
    const float Along = ProceduralMix(Sv, Su, Over);
    const float FloatLength =
        (P.Type == 4 && P.Weave == 2) ? 5 : (P.Type == 4 && P.Weave == 8) ? 3 : (P.Type == 4 && P.Weave == 0) ? 1 : 2;
    const float Arch = .86f + .14f * std::cos((ProceduralFract(Along / FloatLength) * 2 - 1) * 3.14159f);
    const float Fiber = (std::sin(Coord * 6.283f * P.FiberDetail) * .5f + .5f) *
        (1 - ProceduralSmoothstep(.5f, 3.1f, Step * 6.283f * P.FiberDetail));
    const float Height = Bundle * Gap * Arch;
    float Light = (.32f + .68f * Bundle) * Gap * (.86f + .14f * Fiber);
    if (P.Type == 4 && P.FabricMode == 3) Light *= .88f + .16f * ProceduralNoise3(Su * .6f, Sv * .6f, 3);
    if (P.Type == 4 && P.Weave == 2) Light = (.72f + .28f * Bundle) * (.91f + .09f * Fiber) * ProceduralMix(.9f, 1.f, Gap);
    const float YarnResolved = 1 - ProceduralSmoothstep(.22f, .95f, Step);
    const float MeanDirection = (P.Type == 4 && P.Weave == 2) ? .2f : (P.Type == 4 && P.Weave == 8) ? .25f : .5f;
    return {ProceduralMix((P.Type == 4 && P.Weave == 2) ? .86f : .69f, Light, YarnResolved),
        ProceduralMix(.63f, Height, YarnResolved), ProceduralMix(MeanDirection, Over, YarnResolved), Fiber * YarnResolved};
}

void ProceduralBioCylinder(float U, float V, float FreqX, float FreqY, float Seed, float Out[3])
{
    // bioCylinder: wrap U around the fruit circumference so the tile edges meet seamlessly.
    constexpr float Tau = 6.2831853f;
    const float Angle = U * Tau;
    Out[0] = std::cos(Angle) * FreqX / Tau;
    Out[1] = std::sin(Angle) * FreqX / Tau;
    Out[2] = V * FreqY + Seed;
}

void ProceduralBioCell(float Px, float Py, float Seed, float Period, float Out[4])
{
    // bioCellAt: periodic Worley F1/F2 plus winner-site hashes; the lattice id wraps in X so pores
    // tile, while both boundary cells observe the same wrapped neighbours.
    const float CellX = std::floor(Px), CellY = std::floor(Py);
    const float Fx = Px - CellX, Fy = Py - CellY;
    float D1 = 100, D2 = 100, WinnerX = 0, WinnerY = 0;
    for (int Y = -1; Y <= 1; ++Y)
        for (int X = -1; X <= 1; ++X)
        {
            float IdX = CellX + float(X);
            if (Period > 0) IdX -= Period * std::floor(IdX / Period);
            const float IdY = CellY + float(Y);
            const float SiteX = .18f + .64f * ProceduralHash31(IdX, IdY, Seed);
            const float SiteY = .18f + .64f * ProceduralHash31(IdX + 19.3f, IdY + 19.3f, Seed);
            const float Dx = float(X) + SiteX - Fx, Dy = float(Y) + SiteY - Fy;
            const float Distance = std::hypot(Dx, Dy);
            if (Distance < D1)
            {
                D2 = D1;
                D1 = Distance;
                WinnerX = IdX;
                WinnerY = IdY;
            }
            else D2 = std::min(D2, Distance);
        }
    Out[0] = D1;
    Out[1] = std::max(0.f, D2 - D1);
    Out[2] = ProceduralHash31(WinnerX, WinnerY, Seed + 37);
    Out[3] = ProceduralHash31(WinnerX, WinnerY, Seed + 83);
}


void ProceduralCellular3(float Px, float Py, float Pz, float Seed, float Out[4])
{
    // cellular3: 3D Worley over the 27 neighbouring cells; returns border distance plus winner hashes.
    const float CellX = std::floor(Px), CellY = std::floor(Py), CellZ = std::floor(Pz);
    const float Fx = Px - CellX, Fy = Py - CellY, Fz = Pz - CellZ;
    float F1 = 1e8f, F2 = 1e8f, WinnerX = 0, WinnerY = 0, WinnerZ = 0;
    for (int Z = -1; Z <= 1; ++Z)
        for (int Y = -1; Y <= 1; ++Y)
            for (int X = -1; X <= 1; ++X)
            {
                const float IdX = CellX + float(X), IdY = CellY + float(Y), IdZ = CellZ + float(Z);
                const float SiteX = .08f + .84f * ProceduralHash31(IdX + Seed, IdY + Seed, IdZ + Seed);
                const float SiteY = .08f + .84f * ProceduralHash31(IdX + Seed + 17.31f, IdY + Seed + 17.31f, IdZ + Seed + 17.31f);
                const float SiteZ = .08f + .84f * ProceduralHash31(IdX + Seed + 43.97f, IdY + Seed + 43.97f, IdZ + Seed + 43.97f);
                const float Dx = float(X) + SiteX - Fx, Dy = float(Y) + SiteY - Fy, Dz = float(Z) + SiteZ - Fz;
                const float Distance = Dx * Dx + Dy * Dy + Dz * Dz;
                if (Distance < F1) { F2 = F1; F1 = Distance; WinnerX = IdX; WinnerY = IdY; WinnerZ = IdZ; }
                else if (Distance < F2) F2 = Distance;
            }
    Out[0] = std::max(0.f, std::sqrt(F2) - std::sqrt(F1));
    Out[1] = ProceduralHash31(WinnerX + Seed + 61.7f, WinnerY + Seed + 61.7f, WinnerZ + Seed + 61.7f);
    Out[2] = ProceduralHash31(WinnerX + Seed + 127.3f, WinnerY + Seed + 127.3f, WinnerZ + Seed + 127.3f);
    Out[3] = ProceduralHash31(WinnerX + Seed + 223.1f, WinnerY + Seed + 223.1f, WinnerZ + Seed + 223.1f);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     VECTOR HIDE SOURCE
//------------------------------------------------------------------------------------------------------------------------

// 📝 Analytic regeneration of the Alloy leather height atlas (leatherSource.js): the browser rasterizes
// a deterministic SVG (LCG seed 3917) of jittered-grid Voronoi furrow strokes, radial crowns and pores
// over a #bebebe ground, gaussian-blurred per patch. This builder replays the identical LCG stream and
// evaluates the same construction as a distance field — same sites, stroke widths and grays, with the
// blur carried as the stroke falloff — so no SVG rasterizer is needed at bake time.
struct HidePatchField
{
    int Patch = 0;
    int GridCount = 0;                 // patches 0..2: sites per axis
    float Step = 0;                    // patch pixels per grid cell (512 space)
    std::vector<std::array<float, 2>> Sites;      // row-major, patch pixels
    std::vector<std::array<float, 3>> Pores;      // x, y, radius (patch pixels)
    std::array<std::vector<int>, 256> PoreBuckets; // 16x16 buckets of 32px
    std::array<float, 9> BellyRows{};             // patch 3 row curve anchors
    struct BellyCell { float X, Width, Whisker[4][6]; }; // t, q1x, q1y, q2x, q2y, width
    std::array<std::array<BellyCell, 6>, 9> Belly{};
};

HidePatchField BuildHidePatch(int Patch)
{
    HidePatchField Field;
    Field.Patch = Patch;
    uint32_t State = 3917;
    const auto Random = [&]()
    {
        State = State * 1664525u + 1013904223u;
        return float(double(State) / 4294967296.0);
    };
    constexpr float S = 512;
    for (int Current = 0; Current <= Patch; ++Current)
    {
        const bool Capture = Current == Patch;
        if (Current == 3)
        {
            std::array<float, 9> Rows{};
            for (int Row = 0; Row < 9; ++Row) Rows[Row] = float(Row) * S / 9 + (Random() - .5f) * 13;
            if (Capture) Field.BellyRows = Rows;
            for (int Row = 0; Row < 9; ++Row)
            {
                const float Y = Rows[Row], Next = Row == 8 ? Rows[0] + S : Rows[Row + 1];
                for (int Column = 0; Column < 6; ++Column)
                {
                    HidePatchField::BellyCell Cell{};
                    Cell.X = float(Column) * S / 6 + (Random() - .5f) * 23;
                    Cell.Width = 2.6f + Random();
                    for (int Whisker = 0; Whisker < 4; ++Whisker)
                    {
                        Cell.Whisker[Whisker][0] = Y + 5 + Random() * (Next - Y - 10);
                        Cell.Whisker[Whisker][1] = -7 - Random() * 7;
                        Cell.Whisker[Whisker][2] = Random() * 4 - 2;
                        Cell.Whisker[Whisker][3] = -14 - Random() * 10;
                        Cell.Whisker[Whisker][4] = Random() * 9 - 4;
                        Cell.Whisker[Whisker][5] = .5f + Random() * .5f;
                    }
                    if (Capture) Field.Belly[Row][Column] = Cell;
                }
            }
        }
        else
        {
            const int Count = std::array<int, 3>{27, 18, 11}[Current];
            const float Step = S / float(Count);
            if (Capture) { Field.GridCount = Count; Field.Step = Step; Field.Sites.reserve(size_t(Count) * Count); }
            for (int Y = 0; Y < Count; ++Y)
                for (int X = 0; X < Count; ++X)
                {
                    const float OffsetX = (Random() - .5f) * Step * .92f;
                    const float OffsetY = (Random() - .5f) * Step * .92f;
                    if (Capture) Field.Sites.push_back({float(X) * Step + OffsetX, float(Y) * Step + OffsetY});
                }
            for (int Pore = 0; Pore < 950; ++Pore)
            {
                const float X = Random() * S, Y = Random() * S, Radius = .35f + Random() * .65f;
                if (Capture) Field.Pores.push_back({X, Y, Radius});
            }
            if (Capture)
                for (size_t Index = 0; Index < Field.Pores.size(); ++Index)
                {
                    const int BucketX = std::clamp(int(Field.Pores[Index][0] / 32), 0, 15);
                    const int BucketY = std::clamp(int(Field.Pores[Index][1] / 32), 0, 15);
                    Field.PoreBuckets[size_t(BucketY) * 16 + BucketX].push_back(int(Index));
                }
        }
    }
    return Field;
}

float HideStrokeCoverage(float Distance, float HalfWidth, float Softness)
{
    return 1 - ProceduralSmoothstep(HalfWidth - Softness, HalfWidth + Softness, Distance);
}

float SampleHidePatch(const HidePatchField &Field, float Fx, float Fy)
{
    // 📝 Patch space is [0,512) and wraps per patch, exactly like the 3x3 <use> tiling in the SVG.
    Fx -= 512 * std::floor(Fx / 512);
    Fy -= 512 * std::floor(Fy / 512);
    float Height = 190.f / 255;
    if (Field.Patch == 3)
    {
        // Belly plates: wavy horizontal furrows (cubic anchors -20/120/350/532), vertical connectors
        // and short whisker strokes, blurred at 1.5px.
        const auto RowCurveY = [](float AnchorY, float X)
        {
            float T = std::clamp(X / 512, 0.f, 1.f);
            for (int Iteration = 0; Iteration < 4; ++Iteration)
            {
                const float S = 1 - T;
                const float CurveX = S * S * S * -20 + 3 * S * S * T * 120 + 3 * S * T * T * 350 + T * T * T * 532;
                const float Slope = 3 * S * S * 140 + 6 * S * T * 230 + 3 * T * T * 182;
                T = std::clamp(T - (CurveX - X) / std::max(Slope, 1.f), 0.f, 1.f);
            }
            const float S = 1 - T;
            return S * S * S * AnchorY + 3 * S * S * T * (AnchorY - 8) + 3 * S * T * T * (AnchorY + 9) + T * T * T * AnchorY;
        };
        float RowDistance = 1e9f;
        int Band = 0;
        float BandTop = 0, BandBottom = 512;
        for (int Row = 0; Row < 9; ++Row)
            for (float Wrap : {-512.f, 0.f, 512.f})
            {
                const float Distance = std::abs(Fy - (RowCurveY(Field.BellyRows[Row], Fx) + Wrap));
                RowDistance = std::min(RowDistance, Distance);
            }
        Height = ProceduralMix(Height, 76.f / 255, HideStrokeCoverage(RowDistance, 1.8f, 2.2f));
        for (int Row = 0; Row < 9; ++Row)
        {
            const float Top = Field.BellyRows[Row], Bottom = Row == 8 ? Field.BellyRows[0] + 512 : Field.BellyRows[Row + 1];
            if (Fy >= Top - 4 && Fy <= Bottom + 4) { Band = Row; BandTop = Top; BandBottom = Bottom; break; }
            if (Row == 8) { Band = 8; BandTop = Top; BandBottom = Bottom; }
        }
        const auto ConnectorX = [](float X, float Top, float Bottom, float Y)
        {
            float T = std::clamp((Y - Top) / std::max(Bottom - Top, 1.f), 0.f, 1.f);
            for (int Iteration = 0; Iteration < 4; ++Iteration)
            {
                const float S = 1 - T;
                const float CurveY = S * S * S * Top + 3 * S * S * T * (Top + 16) + 3 * S * T * T * (Bottom - 12) + T * T * T * Bottom;
                const float Slope = 3 * S * S * 16 + 6 * S * T * (Bottom - Top - 28) + 3 * T * T * 12;
                T = std::clamp(T - (CurveY - Y) / std::max(Slope, 1.f), 0.f, 1.f);
            }
            const float S = 1 - T;
            return S * S * S * X + 3 * S * S * T * (X - 8) + 3 * S * T * T * (X + 7) + T * T * T * (X + 3);
        };
        const int Column = int(Fx / (512.f / 6));
        for (int Offset = -1; Offset <= 1; ++Offset)
        {
            const int Index = ((Column + Offset) % 6 + 6) % 6;
            const auto &Cell = Field.Belly[Band][Index];
            for (float Wrap : {-512.f, 0.f, 512.f})
            {
                const float Distance = std::abs(Fx - (ConnectorX(Cell.X, BandTop, BandBottom, Fy) + Wrap));
                Height = ProceduralMix(Height, 92.f / 255, HideStrokeCoverage(Distance, Cell.Width * .5f, 2.2f));
                for (const auto &Whisker : Cell.Whisker)
                {
                    const float Ax = Cell.X - 2 + Wrap, Ay = Whisker[0];
                    const float Cx = Ax + Whisker[1], Cy = Ay + Whisker[2];
                    const float Bx = Ax + Whisker[3], By = Ay + Whisker[4];
                    float DistanceToWhisker = 1e9f;
                    float PreviousX = Ax, PreviousY = Ay;
                    for (int StepIndex = 1; StepIndex <= 4; ++StepIndex)
                    {
                        const float T = float(StepIndex) / 4, S = 1 - T;
                        const float X = S * S * Ax + 2 * S * T * Cx + T * T * Bx;
                        const float Y = S * S * Ay + 2 * S * T * Cy + T * T * By;
                        const float SegmentX = X - PreviousX, SegmentY = Y - PreviousY;
                        const float Along = std::clamp(((Fx - PreviousX) * SegmentX + (Fy - PreviousY) * SegmentY) /
                            std::max(SegmentX * SegmentX + SegmentY * SegmentY, 1e-6f), 0.f, 1.f);
                        DistanceToWhisker = std::min(DistanceToWhisker,
                            std::hypot(Fx - (PreviousX + SegmentX * Along), Fy - (PreviousY + SegmentY * Along)));
                        PreviousX = X; PreviousY = Y;
                    }
                    Height = ProceduralMix(Height, 139.f / 255, HideStrokeCoverage(DistanceToWhisker, Whisker[5] * .5f, 2.2f));
                }
            }
        }
        return Height;
    }
    const int Count = Field.GridCount;
    const float Step = Field.Step;
    const int CellX = int(Fx / Step), CellY = int(Fy / Step);
    float D1 = 1e9f, D2 = 1e9f;
    for (int Y = -2; Y <= 2; ++Y)
        for (int X = -2; X <= 2; ++X)
        {
            const int WrappedX = ((CellX + X) % Count + Count) % Count;
            const int WrappedY = ((CellY + Y) % Count + Count) % Count;
            auto Site = Field.Sites[size_t(WrappedY) * Count + WrappedX];
            Site[0] += float(CellX + X - WrappedX) * Step;
            Site[1] += float(CellY + Y - WrappedY) * Step;
            const float Distance = std::hypot(Fx - Site[0], Fy - Site[1]);
            if (Distance < D1) { D2 = D1; D1 = Distance; }
            else D2 = std::min(D2, Distance);
        }
    const float CrownRadius = Step * .7f;
    if (D1 < CrownRadius)
    {
        const float T = D1 / CrownRadius;
        const float Alpha = .65f * (1 - T);
        Height = Height * (1 - Alpha) + ProceduralMix(222.f, 190.f, T) / 255 * Alpha;
    }
    const float Edge = (D2 - D1) * .5f;
    const float Width = std::array<float, 3>{.8f, 1.25f, 2.2f}[Field.Patch];
    const float Softness = (Field.Patch == 2 ? .7f : .45f) * 1.6f;
    Height = ProceduralMix(Height, 172.f / 255, HideStrokeCoverage(Edge, Width * 3.1f * .5f, Softness));
    Height = ProceduralMix(Height, std::array<float, 3>{142.f, 126.f, 110.f}[Field.Patch] / 255,
        HideStrokeCoverage(Edge, Width * .5f, Softness));
    const int BucketX = std::clamp(int(Fx / 32), 0, 15), BucketY = std::clamp(int(Fy / 32), 0, 15);
    for (int Y = -1; Y <= 1; ++Y)
        for (int X = -1; X <= 1; ++X)
        {
            const int WrappedX = ((BucketX + X) % 16 + 16) % 16, WrappedY = ((BucketY + Y) % 16 + 16) % 16;
            for (const int Index : Field.PoreBuckets[size_t(WrappedY) * 16 + WrappedX])
            {
                const auto &Pore = Field.Pores[size_t(Index)];
                float Dx = Fx - (Pore[0] + float(BucketX + X - WrappedX) * 32);
                float Dy = Fy - (Pore[1] + float(BucketY + Y - WrappedY) * 32);
                const float Ellipse = std::sqrt((Dx * Dx) / (Pore[2] * Pore[2]) + (Dy * Dy) / (Pore[2] * .58f * Pore[2] * .58f));
                Height = ProceduralMix(Height, 153.f / 255, 1 - ProceduralSmoothstep(.75f, 1.2f, Ellipse));
            }
        }
    return Height;
}

float EvaluateVectorHide(const HidePatchField &Field, float Px, float Py, float Variation, float Seed)
{
    // vectorHide: one coherent field, continuously warped, sampled through the periodic patch.
    Px += Seed * .173f;
    Py += Seed * .317f;
    float WarpX = ProceduralNoise3(Px * .43f, Py * .43f, 7) - .5f;
    float WarpY = ProceduralNoise3(Px * .43f, Py * .43f, 29) - .5f;
    WarpX += .13f * (ProceduralNoise3(Px * 1.7f, Py * 1.7f, 41) - .5f);
    WarpY += .13f * (ProceduralNoise3(Px * 1.7f, Py * 1.7f, 67) - .5f);
    return SampleHidePatch(Field, (Px + WarpX * Variation * 1.1f) * 512, (Py + WarpY * Variation * 1.1f) * 512);
}


//------------------------------------------------------------------------------------------------------------------------
//                                                     EPIDERMAL SURFACES
//------------------------------------------------------------------------------------------------------------------------

// 📝 Single-face bakes of the shared uType 31/32/33/36 block (leaf, grass blade, petal, stem): one
// complete UV surface, jittered epidermal cells over pigment, then per-type vein/rib/stomata work.
// fwidth terms become bake-resolution derivatives: analytic for the scaled cell domains, finite
// differences for the branch/rib coordinates, exactly as the fruit port treats its pore domain.
struct EpidermalParameters
{
    int Type = 31;
    float Scale = 10, CellScale = 120, CellRelief = .5f, VeinRelief = .65f, CellLobing = .65f;
    float StomataDensity = .22f, ColorGradient = .65f, SpotDensity = .4f, PlantRibs = 10, Seed = 17;
    float Base[3]{1, 1, 1}, Secondary[3]{1, 1, 1}, Tertiary[3]{1, 1, 1};
};
struct EpidermalMaps
{
    std::vector<float> Linear, Height;
};
EpidermalMaps BakeEpidermalMaps(const EpidermalParameters &P, uint32_t Extent)
{
    EpidermalMaps Maps;
    Maps.Linear.assign(size_t(Extent) * Extent * 3, 0);
    Maps.Height.assign(size_t(Extent) * Extent, 0);
    const float Aa = std::max(std::sqrt(2.f) / float(Extent), .0001f);
    const float Period = P.Type == 36 ? std::floor(P.CellScale + .5f) : 0;
    const float CellScaleX = P.Type == 36 ? Period : P.CellScale;
    const float CellAa = std::max(std::hypot(CellScaleX, P.CellScale * 1.5f) / float(Extent), .001f);
    const float CellsResolved = 1 - ProceduralSmoothstep(.3f, 1.25f, CellAa);
    const float StomataAa = std::max(std::hypot(P.CellScale * .31f, P.CellScale * .42f) / float(Extent), .001f);
    const auto CellDomain = [&](float U, float V, float &CellU, float &CellV)
    {
        CellU = U * CellScaleX;
        CellV = V * P.CellScale * 1.5f;
        if (P.Type == 36)
        {
            float Cylinder[3];
            ProceduralBioCylinder(U, V, 11, 11, P.Seed, Cylinder);
            CellU += ProceduralNoise3(Cylinder[0], Cylinder[1], Cylinder[2]) * .5f;
            ProceduralBioCylinder(U, V, 13, 13, 19, Cylinder);
            CellV += ProceduralNoise3(Cylinder[0], Cylinder[1], Cylinder[2]) * .5f;
        }
        else
        {
            CellU += ProceduralNoise3(U * 11, V * 11, P.Seed) * .5f;
            CellV += ProceduralNoise3(U * 13, V * 13, 19) * .5f;
        }
        if (P.Type == 31)
        {
            const float DomainU = CellU, DomainV = CellV;
            CellU += P.CellLobing * .24f * std::sin(DomainV * 5.5f + std::sin(DomainU * 3.7f));
            CellV += P.CellLobing * .24f * std::sin(DomainU * 5.1f + std::sin(DomainV * 4.3f));
        }
    };
    const auto BranchAt = [&](float U, float V)
    {
        const float Y = std::clamp(V, 0.f, 1.f), X = U - .5f;
        return Y * P.Scale - (std::abs(X) + X * .07f) * P.Scale * .55f +
            std::sin(std::abs(X) * 7 + Y * 2) * .14f +
            .16f * ProceduralNoise3(Y * 5, std::abs(X) * 4, P.Seed) + (X >= 0 ? .23f : 0.f);
    };
    const auto RibAt = [&](float U, float V)
    {
        return U * P.PlantRibs + .08f * std::sin(std::clamp(V, 0.f, 1.f) * 8);
    };
    const auto VeinAt = [&](float U, float V)
    {
        const float Y = std::clamp(V, 0.f, 1.f), X = U - .5f;
        return X * (12 + Y * 18) + .32f * ProceduralNoise3(U * 5, V * 5, P.Seed);
    };
    const float Pixel = 1.f / float(Extent);
    for (uint32_t YIndex = 0; YIndex < Extent; ++YIndex)
        for (uint32_t XIndex = 0; XIndex < Extent; ++XIndex)
        {
            const float U = (float(XIndex) + .5f) / float(Extent), V = (float(YIndex) + .5f) / float(Extent);
            const float Y = std::clamp(V, 0.f, 1.f), X = U - .5f;
            float CellU, CellV;
            CellDomain(U, V, CellU, CellV);
            float Cells[4];
            ProceduralBioCell(CellU, CellV, P.Seed, Period, Cells);
            const float CellWall = 1 - ProceduralSmoothstep(.025f, .07f + CellAa, Cells[1]);
            const float Micro = (.65f - CellWall * .18f + Cells[2] * .08f) * CellsResolved;
            float Pigment;
            if (P.Type == 36)
            {
                float Cylinder[3];
                ProceduralBioCylinder(U, V, 8, 8, P.Seed, Cylinder);
                Pigment = ProceduralStoneFbm(Cylinder[0], Cylinder[1], Cylinder[2]);
            }
            else Pigment = ProceduralStoneFbm(U * 8, V * 8, P.Seed * .1f);
            float Rgb[3]{P.Base[0], P.Base[1], P.Base[2]};
            float Height = 0;
            if (P.Type == 31)
            {
                const float MidWidth = ProceduralMix(.011f, .0025f, Y);
                const float Midrib = 1 - ProceduralSmoothstep(MidWidth - Aa * .5f, MidWidth + Aa * .5f, std::abs(X));
                const float Branch = BranchAt(U, V);
                const float BranchAa = std::max((std::abs(BranchAt(U + Pixel, V) - Branch) +
                    std::abs(BranchAt(U, V + Pixel) - Branch)) * .5f, .003f);
                const float VeinWidth = ProceduralMix(.026f, .008f, ProceduralSmoothstep(0, .45f, std::abs(X)));
                float Vein = 1 - ProceduralSmoothstep(std::max(0.f, VeinWidth - BranchAa), VeinWidth + BranchAa,
                    std::abs(ProceduralFract(Branch) - .5f));
                Vein *= ProceduralSmoothstep(.008f, .035f, std::abs(X)) * (1 - ProceduralSmoothstep(.39f, .5f, std::abs(X)));
                const float Tertiary = 1 - ProceduralSmoothstep(.02f, .09f + BranchAa * 2,
                    std::abs(ProceduralFract(Branch * 3 + std::abs(X) * 7) - .5f));
                const float Veins = std::max(Midrib, Vein * .58f);
                const float Aged = ProceduralSmoothstep(.4f, .85f, Pigment + Y * .25f) * P.ColorGradient;
                for (int Component = 0; Component < 3; ++Component)
                {
                    Rgb[Component] = ProceduralMix(Rgb[Component], P.Tertiary[Component], Aged * .7f) * (.86f + Pigment * .23f);
                    Rgb[Component] = ProceduralMix(Rgb[Component], P.Secondary[Component], Veins * .43f + Tertiary * .025f);
                    Rgb[Component] *= 1 - CellWall * .045f * CellsResolved;
                }
                const float StU = U * P.CellScale * .31f, StV = V * P.CellScale * .42f;
                const float StIdX = std::floor(StU), StIdY = std::floor(StV);
                const float StLocalX = (StU - StIdX - .5f) * 1.5f, StLocalY = (StV - StIdY - .5f) * .7f;
                const float StRandom = ProceduralHash31(StIdX, StIdY, P.Seed);
                const float StDistance = std::hypot(StLocalX, StLocalY);
                const float Stomata = (1 - ProceduralSmoothstep(.09f, .15f + StomataAa, StDistance)) *
                    (StRandom <= P.StomataDensity ? 1.f : 0.f) * CellsResolved;
                const float GuardRatio = (StDistance - .2f) / std::max(.07f, StomataAa);
                const float Guard = std::exp(-GuardRatio * GuardRatio) *
                    (StRandom <= P.StomataDensity ? 1.f : 0.f) * CellsResolved;
                Height = (Veins * .002f + Tertiary * .00012f) * P.VeinRelief +
                    (Micro * .00035f - Stomata * .00032f + Guard * .0001f) * P.CellRelief;
                for (int Component = 0; Component < 3; ++Component) Rgb[Component] *= 1 - Stomata * .025f;
            }
            else if (P.Type == 32)
            {
                const float RibCoord = RibAt(U, V);
                const float RibWidth = std::abs(RibAt(U + Pixel, V) - RibCoord) + std::abs(RibAt(U, V + Pixel) - RibCoord);
                float Ribs = std::sin(RibCoord * 6.2831853f) * .5f + .5f;
                Ribs = ProceduralMix(.5f, Ribs, 1 - ProceduralSmoothstep(.4f, 1, RibWidth));
                const float Center = std::exp(-std::abs(X) * 55);
                const float Gradient = std::pow(Y, 1.6f) * P.ColorGradient;
                for (int Component = 0; Component < 3; ++Component)
                    Rgb[Component] = ProceduralMix(Rgb[Component], P.Secondary[Component], Gradient) *
                        (.82f + Pigment * .22f + Ribs * .08f);
                Height = (Ribs * .0009f + Center * .0012f) * P.VeinRelief + Micro * P.CellRelief * .0002f;
            }
            else if (P.Type == 33)
            {
                const float Gradient = ProceduralSmoothstep(.03f, std::max(.12f, P.ColorGradient), Y);
                const float VeinCoord = VeinAt(U, V);
                const float VeinAa = std::max(std::abs(VeinAt(U + Pixel, V) - VeinCoord) +
                    std::abs(VeinAt(U, V + Pixel) - VeinCoord), .005f);
                const float Veins = 1 - ProceduralSmoothstep(.016f, .09f + VeinAa,
                    std::abs(ProceduralFract(VeinCoord) - .5f));
                const float Flecks = ProceduralSmoothstep(.68f, .86f, ProceduralNoise3(U * 52, V * 95, P.Seed)) *
                    P.SpotDensity * (1 - ProceduralSmoothstep(.2f, .7f, Y));
                for (int Component = 0; Component < 3; ++Component)
                {
                    Rgb[Component] = ProceduralMix(P.Secondary[Component], Rgb[Component], Gradient) * (.93f + Pigment * .1f);
                    Rgb[Component] = ProceduralMix(Rgb[Component], P.Tertiary[Component], Flecks * .8f);
                    Rgb[Component] *= 1 - Veins * .055f - CellWall * .025f * CellsResolved;
                }
                const float Cone = std::pow(std::max(0.f, 1 - Cells[0] / .58f), 1.3f) * CellsResolved;
                const float RidgePhase = CellU * 27 + CellV * 9;
                const float RidgeWidth = (27 * CellScaleX + 9 * P.CellScale * 1.5f) / float(Extent);
                const float Ridge = std::sin(RidgePhase) * (1 - ProceduralSmoothstep(.7f, 2.8f, RidgeWidth));
                Height = Veins * P.VeinRelief * .00055f + (Cone * .00045f + Ridge * Cone * .000045f) * P.CellRelief;
            }
            else
            {
                float Cylinder[3];
                ProceduralBioCylinder(U, V, P.Scale * 4, P.Scale * .3f, P.Seed, Cylinder);
                const float Fibers = ProceduralNoise3(Cylinder[0], Cylinder[1], Cylinder[2]);
                const float Cork = ProceduralSmoothstep(.48f, .8f, Pigment) * P.ColorGradient;
                ProceduralBioCylinder(U, V, 95, 190, P.Seed, Cylinder);
                const float Lenticels = ProceduralSmoothstep(.75f, .91f,
                    ProceduralNoise3(Cylinder[0], Cylinder[1], Cylinder[2])) * P.SpotDensity;
                for (int Component = 0; Component < 3; ++Component)
                {
                    Rgb[Component] = ProceduralMix(Rgb[Component], P.Secondary[Component], Cork);
                    Rgb[Component] = ProceduralMix(Rgb[Component], P.Tertiary[Component], Lenticels * .8f) *
                        (.9f + Fibers * .15f);
                }
                Height = (Fibers - .5f) * P.VeinRelief * .002f + Micro * P.CellRelief * .0002f + Lenticels * .0006f;
            }
            const size_t Index = size_t(YIndex) * Extent + XIndex;
            for (int Component = 0; Component < 3; ++Component) Maps.Linear[Index * 3 + Component] = Rgb[Component];
            Maps.Height[Index] = Height;
        }
    return Maps;
}

// 📝 Single-face bake of the uType==35 cactus block: ribbed columns with felted areoles over a waxy
// epidermis. bioPore (the areole mask) feeds the ORM pore-roughness term like the fruit pores.
struct CactusParameters
{
    float Scale = 15, CellScale = 120, CellRelief = .5f, VeinRelief = .55f, ColorGradient = .35f;
    float PlantRibs = 10, Seed = 17;
    float Base[3]{1, 1, 1}, Secondary[3]{1, 1, 1}, Tertiary[3]{1, 1, 1};
};
struct CactusMaps
{
    std::vector<float> Linear, Height, Pore;
};
CactusMaps BakeCactusMaps(const CactusParameters &P, uint32_t Extent)
{
    CactusMaps Maps;
    Maps.Linear.assign(size_t(Extent) * Extent * 3, 0);
    Maps.Height.assign(size_t(Extent) * Extent, 0);
    Maps.Pore.assign(size_t(Extent) * Extent, 0);
    const float Aa = std::max(std::hypot(P.PlantRibs, P.Scale) / float(Extent), .001f);
    const float Detail = 1 - ProceduralSmoothstep(.3f, 1.3f, P.CellScale * std::sqrt(2.f) / float(Extent));
    const float Period = std::floor(P.CellScale + .5f);
    const float Pixel = 1.f / float(Extent);
    const auto LocalAt = [&](float U, float V, float &LocalX, float &LocalY, float &Random)
    {
        float Qx = U * P.PlantRibs, Qy = V * P.Scale;
        Qy += std::floor(Qx) * .5f;
        LocalX = Qx - std::floor(Qx) - .5f;
        LocalY = Qy - std::floor(Qy) - .5f;
        float IdX = std::floor(Qx);
        IdX -= P.PlantRibs * std::floor(IdX / P.PlantRibs);
        const float IdY = std::floor(Qy);
        Random = ProceduralHash31(IdX, IdY, P.Seed);
        LocalX -= (Random - .5f) * .055f;
        LocalY -= (ProceduralHash31(IdX, IdY, 29) - .5f) * .12f;
    };
    const auto PhaseAt = [&](float U, float V)
    {
        float LocalX, LocalY, Random;
        LocalAt(U, V, LocalX, LocalY, Random);
        return std::atan2(LocalY + .000001f, LocalX + .000001f) * 19 + std::hypot(LocalX, LocalY) * 75;
    };
    for (uint32_t YIndex = 0; YIndex < Extent; ++YIndex)
        for (uint32_t XIndex = 0; XIndex < Extent; ++XIndex)
        {
            const float U = (float(XIndex) + .5f) / float(Extent), V = (float(YIndex) + .5f) / float(Extent);
            float LocalX, LocalY, Random;
            LocalAt(U, V, LocalX, LocalY, Random);
            const float AreoleRadius = ProceduralMix(.13f, .19f, Random);
            const float Ellipse = std::hypot(LocalX, LocalY * .75f);
            const float Areole = 1 - ProceduralSmoothstep(.04f, AreoleRadius + Aa, Ellipse);
            const float Ribs = std::pow(.5f + .5f * std::cos((U * P.PlantRibs - .5f) * 6.2831853f), .7f);
            float Cylinder[3];
            ProceduralBioCylinder(U, V, 12, 12, P.Seed, Cylinder);
            const float Wax = ProceduralStoneFbm(Cylinder[0], Cylinder[1], Cylinder[2]);
            float Epidermis[4];
            ProceduralBioCell(U * Period, V * P.CellScale, P.Seed, Period, Epidermis);
            const float Wall = 1 - ProceduralSmoothstep(.02f, .08f, Epidermis[1]);
            float Rgb[3]{P.Base[0], P.Base[1], P.Base[2]};
            for (int Component = 0; Component < 3; ++Component)
                Rgb[Component] = ProceduralMix(Rgb[Component], P.Secondary[Component], Wax * P.ColorGradient) *
                    (.85f + Ribs * .15f);
            ProceduralBioCylinder(U, V, 240, 370, P.Seed, Cylinder);
            const float Fuzz = ProceduralNoise3(Cylinder[0], Cylinder[1], Cylinder[2]);
            const float Phase = PhaseAt(U, V);
            const float PhaseWidth = std::abs(PhaseAt(U + Pixel, V) - Phase) + std::abs(PhaseAt(U, V + Pixel) - Phase);
            const float Filaments = (.5f + .5f * std::sin(Phase)) * (1 - ProceduralSmoothstep(.7f, 3, PhaseWidth));
            const float Felt = Areole * (.62f + .22f * Fuzz + .16f * Filaments);
            const float Cork = 1 - ProceduralSmoothstep(.025f, .055f + Aa, Ellipse);
            for (int Component = 0; Component < 3; ++Component)
            {
                Rgb[Component] = ProceduralMix(Rgb[Component], P.Tertiary[Component] * (.88f + Filaments * .18f), Felt * .9f);
                Rgb[Component] *= 1 - Cork * .13f;
            }
            const size_t Index = size_t(YIndex) * Extent + XIndex;
            for (int Component = 0; Component < 3; ++Component) Maps.Linear[Index * 3 + Component] = Rgb[Component];
            Maps.Height[Index] = (Ribs * .004f + Felt * .0014f) * P.VeinRelief - Wall * P.CellRelief * .0002f * Detail;
            Maps.Pore[Index] = Areole;
        }
    return Maps;
}

std::vector<uint8_t> ConvertMaskedOrmRgba(const std::vector<float> &Mask, uint32_t Extent, float BaseRoughness,
    float CeilingRoughness, bool MetalFromMask)
{
    // 📝 ORM green multiplies the authored scalar, which carries the ceiling (converter lift or the
    // absolute oxide/grout ceiling); dividing the spatially-mixed roughness by that ceiling
    // reproduces the Alloy mix at both ends. Blue carries the metalness multiplier for oxides.
    if (!Extent || Mask.size() != size_t(Extent) * Extent || !(CeilingRoughness > 0) || CeilingRoughness > 1)
        throw std::runtime_error("ORM source is not a square mask field over a unit ceiling.");
    std::vector<uint8_t> Rgba(size_t(Extent) * Extent * 4);
    for (size_t Pixel = 0; Pixel < Mask.size(); ++Pixel)
    {
        const float Weight = std::clamp(Mask[Pixel], 0.f, 1.f);
        uint8_t *Target = &Rgba[Pixel * 4];
        Target[0] = 255;
        Target[1] = uint8_t(std::lround(std::clamp(ProceduralMix(BaseRoughness, CeilingRoughness, Weight) /
            CeilingRoughness, 0.f, 1.f) * 255));
        Target[2] = MetalFromMask ? uint8_t(std::lround((1 - Weight) * 255)) : uint8_t(255);
        Target[3] = 255;
    }
    return Rgba;
}

float SegmentDistance(float Px, float Py, float Ax, float Ay, float Bx, float By)
{
    const float Dx = Bx - Ax, Dy = By - Ay;
    const float T = std::clamp(((Px - Ax) * Dx + (Py - Ay) * Dy) / (Dx * Dx + Dy * Dy), 0.f, 1.f);
    const float Ex = Px - Ax - Dx * T, Ey = Py - Ay - Dy * T;
    return std::sqrt(Ex * Ex + Ey * Ey);
}

// 📝 Verbatim knit(): V-loop pair per cell with sub-loop ribbing and the resolved fade to flat jersey.
float EvaluateKnitLoops(float U, float V, const KnitFieldParameters &P, float Step)
{
    const float C = std::cos(P.AngleRadians), S = std::sin(P.AngleRadians);
    const float Su = (C * U + S * V) * P.Scale, Sv = (-S * U + C * V) * P.Scale;
    const float Qx = ProceduralFract(Su) - .5f, Qy = ProceduralFract(Sv) - .5f;
    const float LoopD =
        std::min(SegmentDistance(Qx, Qy, -.34f, .48f, .02f, -.44f), SegmentDistance(Qx, Qy, .34f, .48f, -.02f, -.44f));
    const float Aa = std::max(Step * .35f, .005f);
    const float Yarn = 1 - ProceduralSmoothstep(.065f - Aa, .15f + Aa, LoopD);
    const float Sub = std::sin((Qx + Qy * .35f) * 100) * .5f + .5f;
    const float Ribbed = ProceduralMix(.5f, Sub, 1 - ProceduralSmoothstep(.02f, .1f, Step));
    return ProceduralMix(.47f, Yarn * (.82f + .18f * Ribbed), 1 - ProceduralSmoothstep(.3f, 1.4f, Step));
}

// 📝 Verbatim cellular3(): 3x3x3 Worley neighbourhood, F2-F1 edge metric plus the winning cell's three
// radiance hashes. Polycrystalline solar grain reads the second hash; the full triple stays for parity.
void EvaluateCellular3(float X, float Y, float Z, float Seed, float Out[4])
{
    const float CellX = std::floor(X), CellY = std::floor(Y), CellZ = std::floor(Z);
    const float Fx = X - CellX, Fy = Y - CellY, Fz = Z - CellZ;
    float F1 = 1e8f, F2 = 1e8f, NearestX = 0, NearestY = 0, NearestZ = 0;
    for (int Oz = -1; Oz <= 1; ++Oz)
        for (int Oy = -1; Oy <= 1; ++Oy)
            for (int Ox = -1; Ox <= 1; ++Ox)
            {
                const float IdX = CellX + float(Ox), IdY = CellY + float(Oy), IdZ = CellZ + float(Oz);
                const float SiteX = .08f + .84f * ProceduralHash31(IdX + Seed, IdY + Seed, IdZ + Seed);
                const float SiteY =
                    .08f + .84f * ProceduralHash31(IdX + Seed + 17.31f, IdY + Seed + 17.31f, IdZ + Seed + 17.31f);
                const float SiteZ =
                    .08f + .84f * ProceduralHash31(IdX + Seed + 43.97f, IdY + Seed + 43.97f, IdZ + Seed + 43.97f);
                const float Dx = float(Ox) + SiteX - Fx, Dy = float(Oy) + SiteY - Fy, Dz = float(Oz) + SiteZ - Fz;
                const float Metric = Dx * Dx + Dy * Dy + Dz * Dz;
                if (Metric < F1)
                {
                    F2 = F1;
                    F1 = Metric;
                    NearestX = IdX;
                    NearestY = IdY;
                    NearestZ = IdZ;
                }
                else if (Metric < F2) F2 = Metric;
            }
    Out[0] = std::max(0.f, std::sqrt(F2) - std::sqrt(F1));
    Out[1] = ProceduralHash31(NearestX + Seed + 61.7f, NearestY + Seed + 61.7f, NearestZ + Seed + 61.7f);
    Out[2] = ProceduralHash31(NearestX + Seed + 127.3f, NearestY + Seed + 127.3f, NearestZ + Seed + 127.3f);
    Out[3] = ProceduralHash31(NearestX + Seed + 223.1f, NearestY + Seed + 223.1f, NearestZ + Seed + 223.1f);
}

// 📝 Verbatim dimple(): staggered hex packing searched over a 3x3 neighbourhood, parabolic bowl with the
// resolved fade to the mean. Step is fwidth(uv) in scaled UV per texel.
float EvaluateDimple(float U, float V, float Scale, float Step)
{
    const float Su = U * Scale, Sv = V * Scale;
    const float CellX = std::floor(Su), CellY = std::floor(Sv / .8660254f);
    float Nearest = 2;
    for (int Y = -1; Y <= 1; ++Y)
        for (int X = -1; X <= 1; ++X)
        {
            const float IdX = CellX + float(X), IdY = CellY + float(Y);
            const float CenterX = IdX + .5f * ProceduralMod(IdY, 2), CenterY = IdY * .8660254f;
            const float Dx = Su - CenterX, Dy = Sv - CenterY;
            Nearest = std::min(Nearest, std::sqrt(Dx * Dx + Dy * Dy));
        }
    const float R = Nearest / .44f;
    const float Bowl = std::pow(std::max(0.f, 1 - R * R), 1.6f);
    const float Resolved = 1 - ProceduralSmoothstep(.3f, 1.2f, Step);
    return ProceduralMix(.24f, Bowl, Resolved);
}

struct SandCell
{
    float Metric = 0, Second = 0, Random = 0, Rough = 0, Spec = 0, Radius = 0;
    float DeltaX = 0, DeltaY = 0, IdX = 0, IdY = 0;
};

// 📝 Verbatim sandCellAt(): jittered-site Voronoi over a 5x5 neighbourhood with per-cell size/metric/radiance
// hashes; the winning cell's id drives grain tint, roughness, specular and facet angle.
SandCell EvaluateSandCell(float Qx, float Qy, float Seed, float Variation, float Nominal)
{
    const float CellX = std::floor(Qx), CellY = std::floor(Qy), Fx = Qx - CellX, Fy = Qy - CellY;
    SandCell Best;
    Best.Metric = Best.Second = 1e8f;
    Best.Random = Best.Rough = Best.Spec = Best.Radius = .5f;
    const float Lo = std::max(.0625f, Nominal * (1 - Variation * .75f)) / Nominal;
    const float Hi = std::min(2.f, Nominal * (1 + Variation * .75f)) / Nominal;
    for (int Y = -2; Y <= 2; ++Y)
        for (int X = -2; X <= 2; ++X)
        {
            const float IdX = CellX + X, IdY = CellY + Y;
            const float Radius = .55f * ProceduralMix(Lo, Hi, ProceduralHash31(IdX, IdY, Seed + 9.3f));
            const float SiteX = .08f + .84f * ProceduralHash31(IdX, IdY, Seed);
            const float SiteY = .08f + .84f * ProceduralHash31(IdX + 31.7f, IdY + 31.7f, Seed + 17.1f);
            const float Dx = float(X) + SiteX - Fx, Dy = float(Y) + SiteY - Fy;
            const float Metric = (Dx * Dx + Dy * Dy) / std::max(Radius * Radius, .0001f);
            if (Metric < Best.Metric)
            {
                Best.Second = Best.Metric;
                Best.Metric = Metric;
                Best.DeltaX = Dx;
                Best.DeltaY = Dy;
                Best.IdX = IdX;
                Best.IdY = IdY;
                Best.Radius = Radius;
                Best.Random = ProceduralHash31(IdX, IdY, Seed + 53.9f);
                Best.Rough = ProceduralHash31(IdX + 7.1f, IdY + 7.1f, Seed + 113);
                Best.Spec = ProceduralHash31(IdX + 21.7f, IdY + 21.7f, Seed + 181);
            }
            else if (Metric < Best.Second) Best.Second = Metric;
        }
    return Best;
}
}

bool DecodeHexLinearRgb(std::string_view Hex, float Rgb[3])
{
    // 📝 THREE.Color parity: #rrggbb reads as sRGB and lands in linear working space.
    if (Hex.size() != 7 || Hex[0] != '#') return false;
    for (int Channel = 0; Channel < 3; ++Channel)
    {
        unsigned int Byte = 0;
        for (int Nibble = 0; Nibble < 2; ++Nibble)
        {
            const char Digit = Hex[size_t(1 + Channel * 2 + Nibble)];
            Byte <<= 4;
            if (Digit >= '0' && Digit <= '9') Byte |= unsigned(Digit - '0');
            else if (Digit >= 'a' && Digit <= 'f') Byte |= unsigned(Digit - 'a' + 10);
            else if (Digit >= 'A' && Digit <= 'F') Byte |= unsigned(Digit - 'A' + 10);
            else return false;
        }
        const float Srgb = float(Byte) / 255;
        Rgb[Channel] = Srgb <= .04045f ? Srgb / 12.92f : std::pow((Srgb + .055f) / 1.055f, 2.4f);
    }
    return true;
}

WeaveFieldSample EvaluateWeaveField(float U, float V, const WeaveFieldParameters &Parameters, float PixelStep)
{
    if (!(Parameters.Scale > 0)) throw std::runtime_error("Weave field needs a positive scale.");
    const float C = std::cos(Parameters.AngleRadians), S = std::sin(Parameters.AngleRadians);
    // 📝 fwidth sums |d/dx|+|d/dy|; the rotated coordinate inherits the texel rate times the rotation row.
    const float Step = PixelStep * (std::abs(C) + std::abs(S));
    const TwillSample Weave = EvaluateTwill(U, V, Parameters, Step);
    const float YarnU = (C * U + S * V) * Parameters.Scale, YarnV = (-S * U + C * V) * Parameters.Scale;
    WeaveFieldSample Sample;
    // 📝 Constant-rate axes collapse onto the seed slice (grain-bake convention); pp maps to (u, seed, v).
    const float Grain = ProceduralNoise3(U * Parameters.Scale, Parameters.Seed, V * Parameters.Scale);
    if (Parameters.Type == 7)
    {
        const float Modulator = .18f + Weave.Light * .82f;
        Sample.Albedo[0] = Sample.Albedo[1] = Sample.Albedo[2] = Modulator;
    }
    else if (Parameters.FabricMode == 0 || Parameters.FabricMode == 3 || Parameters.FabricMode == 4)
    {
        float Yarn[3];
        for (int Channel = 0; Channel < 3; ++Channel)
            Yarn[Channel] = ProceduralMix(Parameters.Warp[Channel], Parameters.Weft[Channel], Weave.Direction);
        if (Parameters.Weave == 7)
        {
            const float WarpBand = ProceduralStep(2, ProceduralMod(std::floor(YarnU), 4));
            const float WeftBand = ProceduralStep(2, ProceduralMod(std::floor(YarnV), 4));
            const float ColorDetail = 1 - ProceduralSmoothstep(.35f, 1.5f, Step);
            const float BandMix = ProceduralMix(WarpBand, WeftBand, Weave.Direction);
            for (int Channel = 0; Channel < 3; ++Channel)
                Yarn[Channel] = ProceduralMix((Parameters.Warp[Channel] + Parameters.Weft[Channel]) * .5f,
                    ProceduralMix(Parameters.Warp[Channel], Parameters.Weft[Channel], BandMix), ColorDetail);
        }
        const float Shade = .48f + Weave.Light * .58f;
        for (int Channel = 0; Channel < 3; ++Channel) Sample.Albedo[Channel] = Yarn[Channel] * Shade;
    }
    else
    {
        const float Nap =
            ProceduralNoise3(U * Parameters.Scale * 18, Parameters.Seed, V * Parameters.Scale * 18);
        const float Modulator = .68f + .36f * Nap;
        Sample.Albedo[0] = Sample.Albedo[1] = Sample.Albedo[2] = Modulator;
    }
    const float Arch = (Parameters.Type == 4 && (Parameters.FabricMode == 1 || Parameters.FabricMode == 2))
        ? Grain * .15f
        : Weave.Height;
    Sample.Relief = Arch * Parameters.Relief * .022f / std::max(Parameters.Scale, .01f);
    if (Parameters.Type == 4)
    {
        const float FiberNap =
            ProceduralNoise3(U * Parameters.Scale * 18, Parameters.Seed, V * Parameters.Scale * 18);
        Sample.Relief += FiberNap * Parameters.Fuzz * .0004f;
        const float FuzzShade = 1 - Parameters.Fuzz * .12f + Parameters.Fuzz * FiberNap * .24f;
        for (int Channel = 0; Channel < 3; ++Channel) Sample.Albedo[Channel] *= FuzzShade;
        if (Parameters.Weave == 8)
        {
            const float SlubNoise = ProceduralNoise3(YarnU * 1.4f, YarnV * .09f, 21);
            const float Fade = (.15f + .85f * ProceduralSmoothstep(.3f, .8f, SlubNoise)) * Parameters.DenimFade *
                (1 - Weave.Direction);
            for (int Channel = 0; Channel < 3; ++Channel)
                Sample.Albedo[Channel] = ProceduralMix(Sample.Albedo[Channel], Parameters.Weft[Channel] * .85f, Fade);
            const float SlubShade = 1 + (SlubNoise - .5f) * Parameters.Slub * .4f;
            for (int Channel = 0; Channel < 3; ++Channel) Sample.Albedo[Channel] *= SlubShade;
            Sample.Relief += (SlubNoise - .5f) * Parameters.Slub * .001f * (1 - Weave.Direction);
        }
    }
    return Sample;
}

WeaveBakeMaps BakeWeaveMaps(const WeaveFieldParameters &Parameters, uint32_t Extent)
{
    if (!Extent || Extent > 2048) throw std::runtime_error("Weave bake extent is unbounded.");
    if (!(Parameters.Scale > 0)) throw std::runtime_error("Weave bake needs a positive scale.");
    WeaveBakeMaps Maps;
    Maps.Extent = Extent;
    Maps.Albedo.assign(size_t(Extent) * Extent * 3, 0);
    Maps.Relief.assign(size_t(Extent) * Extent, 0);
    const float PixelStep = Parameters.Scale / float(Extent);
    for (uint32_t Y = 0; Y < Extent; ++Y)
        for (uint32_t X = 0; X < Extent; ++X)
        {
            const auto Sample = EvaluateWeaveField((float(X) + .5f) / float(Extent), (float(Y) + .5f) / float(Extent),
                Parameters, PixelStep);
            for (int Channel = 0; Channel < 3; ++Channel)
                Maps.Albedo[(size_t(Y) * Extent + X) * 3 + Channel] = Sample.Albedo[Channel];
            Maps.Relief[size_t(Y) * Extent + X] = Sample.Relief;
        }
    return Maps;
}

float WeaveReliefAmplitude(const WeaveFieldParameters &Parameters)
{
    const float Arch = (Parameters.Type == 4 && (Parameters.FabricMode == 1 || Parameters.FabricMode == 2))
        ? Parameters.Grain * .15f
        : 1.f;
    float Amplitude = Arch * Parameters.Relief * .022f / std::max(Parameters.Scale, .01f);
    if (Parameters.Type == 4)
    {
        Amplitude += Parameters.Fuzz * .0004f;
        if (Parameters.Weave == 8) Amplitude += Parameters.Slub * .0005f;
    }
    return Amplitude;
}

KnitFieldSample EvaluateKnitField(float U, float V, const KnitFieldParameters &Parameters, float PixelStep)
{
    if (!(Parameters.Scale > 0)) throw std::runtime_error("Knit field needs a positive scale.");
    const float C = std::cos(Parameters.AngleRadians), S = std::sin(Parameters.AngleRadians);
    const float Step = PixelStep * (std::abs(C) + std::abs(S));
    const float Loops = EvaluateKnitLoops(U, V, Parameters, Step);
    // 📝 Melange mottling keeps its two in-plane rates; the out-of-plane axis takes the seed slice.
    const float Melange = ProceduralNoise3(U * Parameters.Scale * .25f, Parameters.Seed, V * Parameters.Scale * 2);
    const float Mask = ProceduralSmoothstep(.55f, .9f, Melange) * .32f;
    const float Shade = .48f + Loops * .57f;
    KnitFieldSample Sample;
    for (int Channel = 0; Channel < 3; ++Channel)
        Sample.Albedo[Channel] = ProceduralMix(Parameters.Color[Channel], Parameters.Secondary[Channel], Mask) * Shade;
    Sample.Relief = Loops * Parameters.Grain * .008f / std::max(std::sqrt(Parameters.Scale), .1f) +
        ProceduralNoise3(U * Parameters.Scale * 13, Parameters.Seed, V * Parameters.Scale * 13) * Parameters.Fuzz * .0004f;
    return Sample;
}

KnitBakeMaps BakeKnitMaps(const KnitFieldParameters &Parameters, uint32_t Extent)
{
    if (!Extent || Extent > 2048) throw std::runtime_error("Knit bake extent is unbounded.");
    if (!(Parameters.Scale > 0)) throw std::runtime_error("Knit bake needs a positive scale.");
    KnitBakeMaps Maps;
    Maps.Extent = Extent;
    Maps.Albedo.assign(size_t(Extent) * Extent * 3, 0);
    Maps.Relief.assign(size_t(Extent) * Extent, 0);
    const float PixelStep = Parameters.Scale / float(Extent);
    for (uint32_t Y = 0; Y < Extent; ++Y)
        for (uint32_t X = 0; X < Extent; ++X)
        {
            const auto Sample = EvaluateKnitField((float(X) + .5f) / float(Extent), (float(Y) + .5f) / float(Extent),
                Parameters, PixelStep);
            for (int Channel = 0; Channel < 3; ++Channel)
                Maps.Albedo[(size_t(Y) * Extent + X) * 3 + Channel] = Sample.Albedo[Channel];
            Maps.Relief[size_t(Y) * Extent + X] = Sample.Relief;
        }
    return Maps;
}

float KnitReliefAmplitude(const KnitFieldParameters &Parameters)
{
    return Parameters.Grain * .008f / std::max(std::sqrt(std::max(Parameters.Scale, 0.f)), .1f) +
        Parameters.Fuzz * .0004f;
}

double SandBedSeed(std::string_view BedId)
{
    // 📝 sandLayerSeed parity: FNV-1a over the id bytes, reduced mod 8191, times .013.
    uint32_t Hash = 2166136261u;
    for (const unsigned char Byte : BedId) Hash = (Hash ^ Byte) * 16777619u;
    return double(Hash % 8191) * .013;
}

SandFieldSample EvaluateSandField(float U, float V, const SandFieldParameters &Parameters, uint32_t Extent)
{
    if (Parameters.Count <= 0 || Parameters.Count > 4) throw std::runtime_error("Sand field needs one to four beds.");
    if (!Extent) throw std::runtime_error("Sand field needs a nonzero extent.");
    SandFieldSample Sample;
    for (int Channel = 0; Channel < 3; ++Channel) Sample.Tint[Channel] = Parameters.BaseColor[Channel];
    const SandBedParameters &Deepest = Parameters.Beds[Parameters.Count - 1];
    Sample.Roughness = std::min(1.f, Deepest.Roughness + .16f);
    float TotalBand = 0;
    for (int Bed = 0; Bed < Parameters.Count; ++Bed) TotalBand += Parameters.Beds[Bed].Thickness;
    const float C = std::cos(Parameters.BandAngleRadians), S = std::sin(Parameters.BandAngleRadians);
    const float BandX = C * U + S * V, BandY = -S * U + C * V;
    const float BandNoise = ProceduralNoise3(BandX * .7f, BandY * .7f, Parameters.Seed * .013f);
    const float Band = ProceduralFract(BandY * Parameters.BandFrequency + (BandNoise - .5f) * Parameters.BandWarp);
    float BandEdge = 0;
    int Selected = 0;
    for (int Bed = 0; Bed < 4; ++Bed)
        if (Bed < Parameters.Count)
        {
            const float End = BandEdge + Parameters.Beds[Bed].Thickness / std::max(TotalBand, .001f);
            if (Band >= BandEdge && Band < End) Selected = Bed;
            BandEdge = End;
        }
    // 📝 Bed 0 is uppermost: lower beds evaluate first and the selected top grain occludes them.
    for (int Reverse = 0; Reverse < 4; ++Reverse)
    {
        const int Layer = 3 - Reverse;
        if (Layer >= Parameters.Count || (Parameters.BandsMode == 1 && Layer != Selected)) continue;
        const SandBedParameters &Bed = Parameters.Beds[Layer];
        if (!(Bed.Coverage > 0)) continue;
        const float Frequency = 110 * Parameters.Scale / std::max(Bed.Size, .0625f);
        float DomainX = U * Frequency + float(Bed.BedSeed * 71.3);
        float DomainY = V * Frequency + float(Bed.BedSeed * 29.7);
        // 📝 Both warp taps read the pre-warp domain; GLSL evaluates the pair simultaneously.
        const float WarpX = ProceduralNoise3(DomainX * .19f, DomainY * .19f, Parameters.Seed);
        const float WarpY = ProceduralNoise3(DomainX * .19f + 19.1f, DomainY * .19f + 19.1f, Parameters.Seed);
        DomainX += (WarpX - .5f) * .45f;
        DomainY += (WarpY - .5f) * .45f;
        // 📝 The domain warp is low-rate, so the footprint converges to the nominal grain texel rate.
        const float Footprint = Frequency / float(Extent);
        const float Resolved = 1 - ProceduralSmoothstep(.55f, 1.8f, Footprint);
        float Mean[3], Tint[3];
        for (int Channel = 0; Channel < 3; ++Channel)
        {
            Mean[Channel] = Bed.Color[Channel] * .81f + Bed.Pale[Channel] * .17f + Bed.Color[Channel] * .38f * .02f;
            Tint[Channel] = Mean[Channel];
        }
        float Opacity = Bed.Coverage * .78f, Cap = 0, RoughGrain = Bed.Roughness, TiltX = 0, TiltY = 0;
        if (Resolved > .001f)
        {
            const float CellSeed = Parameters.Seed + float(Bed.BedSeed * 97.1);
            const SandCell Cell = EvaluateSandCell(DomainX, DomainY, CellSeed, Bed.SizeVariation, Bed.Size);
            const float Distance = std::sqrt(Cell.Metric), Aa = std::max(Footprint * .65f, .01f);
            const float Contact = ProceduralSmoothstep(0.f, std::max(.04f, Aa), std::sqrt(Cell.Second) - Distance);
            const float Outline = 1 - ProceduralSmoothstep(1 - Aa, 1 + Aa, Distance);
            const float Occupied =
                ProceduralStep(1 - Bed.Coverage, ProceduralHash31(Cell.IdX, Cell.IdY, CellSeed + 271));
            const float GrainMask = Outline * Contact * Occupied;
            for (int Channel = 0; Channel < 3; ++Channel)
                Tint[Channel] =
                    ProceduralMix(Bed.Color[Channel], Bed.Pale[Channel], ProceduralSmoothstep(.7f, .92f, Cell.Random));
            const float Dark = ProceduralStep(.97f, Cell.Random);
            const float Bright = .86f + .28f * Cell.Rough;
            for (int Channel = 0; Channel < 3; ++Channel)
                Tint[Channel] = ProceduralMix(Tint[Channel], Tint[Channel] * .38f, Dark) * Bright;
            const float Rounded =
                std::pow(std::max(0.f, 1 - Cell.Metric), ProceduralMix(.2f, .85f, Bed.Roundness));
            Cap = Rounded * GrainMask * Bed.Size * .01f * Bed.Relief * .62f / std::max(Parameters.Scale, .1f);
            RoughGrain = std::clamp(Bed.Roughness + (Cell.Rough - .5f) * Bed.RoughnessVariation, .04f, 1.f);
            const float Angle = Cell.Random * 6.2831853f;
            TiltX = std::cos(Angle) * Bed.Tilt * (.35f + .65f * Cell.Spec);
            TiltY = std::sin(Angle) * Bed.Tilt * (.35f + .65f * Cell.Spec);
            Opacity = ProceduralMix(Opacity, GrainMask, Resolved);
            for (int Channel = 0; Channel < 3; ++Channel) Tint[Channel] = ProceduralMix(Mean[Channel], Tint[Channel], Resolved);
            RoughGrain = ProceduralMix(Bed.Roughness, RoughGrain, Resolved);
        }
        for (int Channel = 0; Channel < 3; ++Channel)
            Sample.Tint[Channel] = ProceduralMix(Sample.Tint[Channel], Tint[Channel], Opacity);
        Sample.Relief = ProceduralMix(Sample.Relief, Cap, Opacity);
        Sample.Roughness = ProceduralMix(Sample.Roughness, RoughGrain, Opacity);
        Sample.Facet[0] = ProceduralMix(Sample.Facet[0], TiltX * Resolved, Opacity);
        Sample.Facet[1] = ProceduralMix(Sample.Facet[1], TiltY * Resolved, Opacity);
    }
    return Sample;
}

SandBakeMaps BakeSandMaps(const SandFieldParameters &Parameters, uint32_t Extent)
{
    if (!Extent || Extent > 2048) throw std::runtime_error("Sand bake extent is unbounded.");
    if (Parameters.Count <= 0 || Parameters.Count > 4) throw std::runtime_error("Sand bake needs one to four beds.");
    SandBakeMaps Maps;
    Maps.Extent = Extent;
    Maps.Tint.assign(size_t(Extent) * Extent * 3, 0);
    Maps.Roughness.assign(size_t(Extent) * Extent, 0);
    Maps.Relief.assign(size_t(Extent) * Extent, 0);
    Maps.Facet.assign(size_t(Extent) * Extent * 2, 0);
    for (uint32_t Y = 0; Y < Extent; ++Y)
        for (uint32_t X = 0; X < Extent; ++X)
        {
            const auto Sample = EvaluateSandField((float(X) + .5f) / float(Extent), (float(Y) + .5f) / float(Extent),
                Parameters, Extent);
            for (int Channel = 0; Channel < 3; ++Channel)
                Maps.Tint[(size_t(Y) * Extent + X) * 3 + Channel] = Sample.Tint[Channel];
            Maps.Roughness[size_t(Y) * Extent + X] = Sample.Roughness;
            Maps.Relief[size_t(Y) * Extent + X] = Sample.Relief;
            Maps.Facet[(size_t(Y) * Extent + X) * 2] = Sample.Facet[0];
            Maps.Facet[(size_t(Y) * Extent + X) * 2 + 1] = Sample.Facet[1];
        }
    return Maps;
}

float SandReliefAmplitude(const SandFieldParameters &Parameters)
{
    float Amplitude = 0;
    for (int Bed = 0; Bed < Parameters.Count; ++Bed)
        Amplitude = std::max(Amplitude, Parameters.Beds[Bed].Size * .01f * Parameters.Beds[Bed].Relief * .62f /
                std::max(Parameters.Scale, .1f));
    return Amplitude;
}

std::vector<uint8_t> ConvertLinearAlbedoRgba(const std::vector<float> &Linear, uint32_t Extent)
{
    if (!Extent || Linear.size() != size_t(Extent) * Extent * 3)
        throw std::runtime_error("Albedo source is not a square linear-RGB field.");
    std::vector<uint8_t> Rgba(size_t(Extent) * Extent * 4);
    for (size_t Pixel = 0; Pixel < size_t(Extent) * Extent; ++Pixel)
    {
        uint8_t *Out = &Rgba[Pixel * 4];
        for (int Channel = 0; Channel < 3; ++Channel)
            Out[Channel] = uint8_t(std::lround(std::clamp(ProceduralLinearToSrgb(Linear[Pixel * 3 + Channel]), 0.f, 1.f) * 255));
        Out[3] = 255;
    }
    return Rgba;
}

std::vector<uint8_t> ConvertSandOrmRgba(const std::vector<float> &Roughness, uint32_t Extent)
{
    if (!Extent || Roughness.size() != size_t(Extent) * Extent)
        throw std::runtime_error("ORM source is not a square roughness field.");
    std::vector<uint8_t> Rgba(size_t(Extent) * Extent * 4);
    for (size_t Pixel = 0; Pixel < Roughness.size(); ++Pixel)
    {
        uint8_t *Out = &Rgba[Pixel * 4];
        Out[0] = 255;
        Out[1] = uint8_t(std::lround(std::clamp(Roughness[Pixel], 0.f, 1.f) * 255));
        Out[2] = 255;
        Out[3] = 255;
    }
    return Rgba;
}

std::vector<uint8_t> ConvertFacetNormalRgba(const std::vector<float> &Relief, const std::vector<float> &Facet,
    uint32_t Extent, float Strength)
{
    if (!Extent || Relief.size() != size_t(Extent) * Extent || Facet.size() != size_t(Extent) * Extent * 2)
        throw std::runtime_error("Normal source is not a square relief/facet field pair.");
    std::vector<uint8_t> Rgba(size_t(Extent) * Extent * 4);
    for (uint32_t Y = 0; Y < Extent; ++Y)
        for (uint32_t X = 0; X < Extent; ++X)
        {
            const uint32_t Left = X > 0 ? X - 1 : 0, Right = X + 1 < Extent ? X + 1 : Extent - 1;
            const uint32_t Up = Y > 0 ? Y - 1 : 0, Down = Y + 1 < Extent ? Y + 1 : Extent - 1;
            const float Gx = (Relief[size_t(Y) * Extent + Right] - Relief[size_t(Y) * Extent + Left]) * .5f;
            const float Gy = (Relief[size_t(Down) * Extent + X] - Relief[size_t(Up) * Extent + X]) * .5f;
            const float Nx = -Gx * Strength + Facet[(size_t(Y) * Extent + X) * 2];
            const float Ny = -Gy * Strength + Facet[(size_t(Y) * Extent + X) * 2 + 1];
            const float Inverse = 1 / std::sqrt(Nx * Nx + Ny * Ny + 1);
            uint8_t *Pixel = &Rgba[(size_t(Y) * Extent + X) * 4];
            Pixel[0] = uint8_t(std::lround((Nx * Inverse * .5f + .5f) * 255));
            Pixel[1] = uint8_t(std::lround((Ny * Inverse * .5f + .5f) * 255));
            Pixel[2] = uint8_t(std::lround((Inverse * .5f + .5f) * 255));
            Pixel[3] = 255;
        }
    return Rgba;
}

namespace
{
//------------------------------------------------------------------------------------------------------------------------
//                                                     CODED FIELDS
//------------------------------------------------------------------------------------------------------------------------

// 📝 Line-for-line FieldSpecification.js port in double precision; see header note on the sin hash.
double CodedClamp(double Quantity) { return std::max(0., std::min(1., Quantity)); }

double CodedHashCell(double Column, double Row, double Seed)
{
    const double Product = std::sin(Column * 127.1 + Row * 311.7 + Seed * 17.13) * 43758.5453;
    return Product - std::floor(Product);
}

double CodedSmoothNoise(double Horizontal, double Vertical, double Seed)
{
    const double Column = std::floor(Horizontal), Row = std::floor(Vertical);
    const double FractionX = Horizontal - Column, FractionY = Vertical - Row;
    const double BlendX = FractionX * FractionX * (3 - 2 * FractionX);
    const double BlendY = FractionY * FractionY * (3 - 2 * FractionY);
    const double Top = CodedHashCell(Column, Row, Seed) * (1 - BlendX) + CodedHashCell(Column + 1, Row, Seed) * BlendX;
    const double Bottom = CodedHashCell(Column, Row + 1, Seed) * (1 - BlendX) + CodedHashCell(Column + 1, Row + 1, Seed) * BlendX;
    return Top * (1 - BlendY) + Bottom * BlendY;
}

double CodedCamouflage(const CodedFieldParameters &Parameters, double Horizontal, double Vertical, bool Smooth)
{
    // 📝 Periodic coordinate warping keeps opposite tile edges identical without image assets.
    const double Frequency = std::max(1., std::round(Parameters.Scale)), Seed = Parameters.Seed;
    const auto Periodic = [](double X, double Y, double Period, double Salt)
    {
        const double Column = std::floor(X), Row = std::floor(Y), FractionX = X - Column, FractionY = Y - Row;
        const double BlendX = FractionX * FractionX * (3 - 2 * FractionX), BlendY = FractionY * FractionY * (3 - 2 * FractionY);
        const auto Cell = [Period, Salt](double CellColumn, double CellRow)
        {
            const auto Wrap = [Period](double Coordinate)
            {
                const double Remainder = std::fmod(Coordinate, Period);
                return std::fmod(Remainder + Period, Period);
            };
            return CodedHashCell(Wrap(CellColumn), Wrap(CellRow), Salt);
        };
        return (Cell(Column, Row) * (1 - BlendX) + Cell(Column + 1, Row) * BlendX) * (1 - BlendY) +
               (Cell(Column, Row + 1) * (1 - BlendX) + Cell(Column + 1, Row + 1) * BlendX) * BlendY;
    };
    const double Grid = 256 / std::max(1., Parameters.PixelSize);
    const double PositionX = (Smooth ? Horizontal + .5 : std::floor((Horizontal + .5) * Grid) / Grid) * Frequency;
    const double PositionY = (Smooth ? Vertical + .5 : std::floor((Vertical + .5) * Grid) / Grid) * Frequency;
    const double WarpedX = PositionX + (Periodic(PositionX, PositionY, Frequency, Seed + 17) - .5) * 2.4;
    const double WarpedY = PositionY + (Periodic(PositionX, PositionY, Frequency, Seed + 71) - .5) * 2.4;
    const double Field = Periodic(WarpedX, WarpedY, Frequency, Seed) * .78 +
                         Periodic(WarpedX * 3, WarpedY * 3, Frequency * 3, Seed + 43) * .22;
    const uint32_t Count = std::max(2u, std::min(8u, Parameters.ColourCount));
    double Band = 0;
    for (uint32_t Index = 0; Index + 1 < Count; ++Index)
    {
        const double Threshold = Count == 2 ? .5 : .36 + double(Index) * .28 / double(Count - 2);
        const double Blend = CodedClamp((Field - Threshold) / std::max(.001, Parameters.Softness) + .5);
        Band += Smooth ? Blend * Blend * (3 - 2 * Blend) : (Field >= Threshold ? 1. : 0.);
    }
    return Band / double(Count - 1);
}

} // namespace

double EvaluateCodedField(std::string_view Identity, const CodedFieldParameters &Parameters,
    double Horizontal, double Vertical)
{
    double Sample = 0;
    if (Identity == "camouflage" || Identity == "camouflage-smooth")
        Sample = CodedCamouflage(Parameters, Horizontal, Vertical, Identity == "camouflage-smooth");
    else if (QueryCodedField(Identity))
    {
        const double Frequency = Parameters.Scale;
        const double PositionX = (Horizontal + .5) * Frequency, PositionY = (Vertical + .5) * Frequency;
        if (Identity == "stripes")
            Sample = .5 + .5 * std::sin(PositionY * 3.141592653589793 * 2 + Parameters.Seed);
        else if (Identity == "checker")
        {
            const double Parity = std::fmod(std::floor(PositionX) + std::floor(PositionY) + Parameters.Seed, 2.);
            Sample = std::abs(Parity);
        }
        else if (Identity == "cells")
        {
            const double Column = std::floor(PositionX), Row = std::floor(PositionY);
            double Distance = 2;
            for (int OffsetY = -1; OffsetY <= 1; ++OffsetY)
                for (int OffsetX = -1; OffsetX <= 1; ++OffsetX)
                {
                    const double CellX = Column + OffsetX, CellY = Row + OffsetY;
                    const double CenterX = CellX + CodedHashCell(CellX, CellY, Parameters.Seed);
                    const double CenterY = CellY + CodedHashCell(CellX, CellY, Parameters.Seed + 31);
                    Distance = std::min(Distance, std::hypot(PositionX - CenterX, PositionY - CenterY));
                }
            Sample = CodedClamp(Distance);
        }
        else
        {
            double Accumulated = 0, Amplitude = .5, Sum = 0;
            for (int Octave = 0; Octave < 4; ++Octave)
            {
                const double Rate = std::ldexp(1., Octave);
                Accumulated += CodedSmoothNoise(PositionX * Rate, PositionY * Rate, Parameters.Seed + Octave) * Amplitude;
                Sum += Amplitude;
                Amplitude *= .5;
            }
            const double Fractal = Accumulated / Sum;
            if (Identity == "ridged") Sample = 1 - std::abs(Fractal * 2 - 1);
            else if (Identity == "rings")
                Sample = .5 + .5 * std::sin(std::hypot(Horizontal, Vertical) * Frequency * 3.141592653589793 * 2 +
                             Fractal * 2 + Parameters.Seed);
            else Sample = Fractal;
        }
    }
    return std::isfinite(Sample) ? CodedClamp(Sample) : 0;
}

double EvaluateCodedFieldPosition(std::string_view Identity, const CodedFieldParameters &Parameters,
    double U, double V, double AngleDegrees, double Amount)
{
    const double Radians = AngleDegrees * 3.141592653589793 / 180;
    const double RotatedX = (U - .5) * std::cos(Radians) - (V - .5) * std::sin(Radians);
    const double RotatedY = (U - .5) * std::sin(Radians) + (V - .5) * std::cos(Radians);
    const double Sample = EvaluateCodedField(Identity, Parameters, RotatedX, RotatedY);
    return .5 + (Sample - .5) * Amount;
}

std::vector<std::string> QueryProceduralOps()
{
    std::vector<std::string> Ops{"scratches", "metalGrain", "polymerGrain", "ribbedSheet", "solar", "dimples",
        "stucco", "weave", "knit", "scratchedMetal", "latheMetal", "clay", "led", "fruit",
        "leaf", "grass", "petal", "stem", "cactus", "leather", "crocodile",
        "wax", "skin", "paper", "rust", "ceramic", "rubber", "tiles", "granite", "marble", "concrete"};
    for (const char *Identity : CodedFieldIdentities) Ops.emplace_back(Identity);
    return Ops;
}

std::vector<std::string> QueryProceduralOpChannels(std::string_view Op)
{
    if (Op == "scratches") return {"Orm", "Normal"};
    // 📝 Alloy varies no roughness term for bare metal, so metalGrain offers no ORM channel; polymer grain
    // modulates roughness and earns all three.
    if (Op == "metalGrain") return {"Albedo", "Normal"};
    if (Op == "polymerGrain") return {"Albedo", "Orm", "Normal"};
    if (Op == "ribbedSheet") return {"Albedo", "Orm", "Normal"};
    // 📝 Alloy varies no ORM term for dimpled covers, stucco, clay aggregate or yarn construction, so those
    // ops offer albedo plus normals; solar contacts and scratch overlays earn the full triple.
    if (Op == "solar" || Op == "scratchedMetal" || Op == "latheMetal") return {"Albedo", "Orm", "Normal"};
    // 📝 Alloy varies no roughness term across the emitter array, so LED earns albedo, normals and the
    // emitter mask; ORM stays scalar-authored while emission strength lives in RadianceNormal.
    if (Op == "led") return {"Albedo", "Normal", "Radiance"};
    // 📝 Fruit and cactus pores lift roughness by .24 in Alloy, rust mixes to the .94 oxide ceiling
    // (killing metalness), and grout mixes to .93; those earn the ORM channel, the rest stay scalar.
    if (Op == "fruit" || Op == "cactus" || Op == "rust" || Op == "tiles")
        return {"Albedo", "Orm", "Normal"};
    if (Op == "dimples" || Op == "stucco" || Op == "weave" || Op == "knit" || Op == "clay" ||
        Op == "leaf" || Op == "grass" || Op == "petal" || Op == "stem" || Op == "leather" ||
        Op == "crocodile" || Op == "wax" || Op == "skin" || Op == "paper" || Op == "ceramic" ||
        Op == "rubber" || Op == "granite" || Op == "marble" || Op == "concrete")
        return {"Albedo", "Normal"};
    // 📝 Coded fields bake colour (palette/ramp projection) and relief normals; ORM stays scalar-authored
    // because the HTML field carries no roughness ceiling convention the ORM ratio could divide by.
    if (QueryCodedField(Op)) return {"Albedo", "Normal"};
    return {};
}

float QueryProceduralOpCeilingLift(std::string_view Op)
{
    if (Op == "scratches") return .3f;
    if (Op == "polymerGrain") return .096f;
    if (Op == "ribbedSheet") return .35f;
    // 📝 Solar contacts lift roughness from the .2 base to .22; metal scratch overlays reuse the study's
    // +0.3 ceiling so the shared roughness slider keeps one convention across the family.
    if (Op == "solar") return .02f;
    if (Op == "scratchedMetal" || Op == "latheMetal") return .3f;
    // 📝 Fruit/cactus pores lift roughness by .24 (the Alloy bioPore term). Rust and tiles instead
    // mix to absolute oxide/grout ceilings (.94/.93); their lifts are authored against the shipped
    // base roughness (.36/.2) so the authored scalar lands on the ceiling.
    if (Op == "fruit" || Op == "cactus") return .24f;
    if (Op == "rust") return .58f;
    if (Op == "tiles") return .73f;
    return 0;
}

std::vector<ProceduralOpParam> QueryProceduralOpParams(std::string_view Op)
{
    // 📝 Ranges mirror the Alloy recipe controls; Param keys keep their uniform spellings, except the direction
    // alias below: type-29 studies target weaveAngle while metal overlays target scratchAngle, and both drive
    // the same radian uniform, so the pool stores one canonical angle in degrees.
    if (Op == "scratches")
        return {{"scratchDensity", "Scratch density", 0, 12, 6, false, "", .1f},
            {"scratchLength", "Scratch length", .1f, 1.65f, 1.15f, false, "", .01f},
            {"scratchWidth", "Scratch width", .002f, .035f, .012f, false, "", .001f},
            {"scratchDepth", "Scratch depth", 0, .006f, .0018f, false, "", .0001f},
            {"scratchSpread", "Direction spread", 0, 1, .8f, false, "", .01f},
            {"scratchBend", "Scratch curvature", 0, .7f, .42f, false, "", .01f},
            {"scratchScale", "Field scale", .01f, 1000, 6, true, "x", .01f},
            {"angle", "Preferred direction", -180, 180, 25, false, "deg", 1},
            {"surfaceSeed", "Pattern seed", 0, 9999, 17, false, "", 1},
            {"roughness", "Surface roughness", .045f, 1, .23f, false, "", .01f}};
    // 📝 Grain ranges mirror the Alloy recipe controls: bare-metal tooth and polymer molding share the noise
    // kernels but keep their own scale defaults and wear terms, and only the ORM baker holds roughness.
    if (Op == "metalGrain")
        return {{"grain", "Surface tooth", 0, 1, .25f, false, "", .01f},
            {"detailScale", "Grain scale", 4, 256, 65, false, "x", 1},
            {"surfaceSeed", "Pattern seed", 0, 9999, 17, false, "", 1}};
    if (Op == "polymerGrain")
        return {{"grain", "Molding grain", 0, 1, .6f, false, "", .01f},
            {"detailScale", "Grain scale", 4, 256, 130, false, "x", 1},
            {"wear", "Polishing wear", 0, 1, 0, false, "", .01f},
            {"wearSoftness", "Wear softness", 0, 1, .65f, false, "", .01f},
            {"wornRoughness", "Worn roughness", .045f, 1, .24f, false, "", .01f},
            {"scratchScale", "Scuff scale", 1, 128, 32, false, "x", 1},
            {"surfaceSeed", "Pattern seed", 0, 9999, 17, false, "", 1},
            {"roughness", "Surface roughness", .045f, 1, .5f, false, "", .01f}};
    // 📝 Rib ranges mirror the Alloy sheet controls (ribDepth/ribProfile/detailScale/weaveAngle/sheetSpangle/sheetOxidation).
    if (Op == "ribbedSheet")
        return {{"ribs", "Ribs per tile", 1, 12, 4, false, "", 1},
            {"ribDepth", "Rib depth", 0, .4f, .2f, false, "scene units", .01f},
            {"ribProfile", "Rib profile", 0, 1, 0, false, "0 round - 1 IBR", 1},
            {"weaveAngle", "Rib direction", -180, 180, 0, false, "deg", 1},
            {"sheetSpangle", "Spangle character", 0, 1, .3f, false, "", .01f},
            {"sheetOxidation", "Weathering", 0, 1, .1f, false, "", .01f},
            {"roughness", "Surface roughness", .045f, 1, .4f, false, "", .01f},
            {"surfaceSeed", "Pattern seed", 0, 9999, 17, false, "", 1}};
    // 📝 Machined-metal ranges mirror the Alloy scratch study plus the bare-metal tooth and the lathe
    // ring depth. The inspector renders these, and a newly attached field seeds its block from them.
    if (Op == "scratchedMetal")
        return {{"grain", "Surface tooth", 0, 1, .25f, false, "", .01f},
            {"detailScale", "Machining scale", 4, 256, 65, false, "x", 1},
            {"roughness", "Surface roughness", .045f, 1, .23f, false, "", .01f},
            {"scratchDensity", "Scratch density", 0, 12, 6, false, "", .1f},
            {"scratchLength", "Scratch length", .1f, 1.65f, 1.15f, false, "", .01f},
            {"scratchWidth", "Scratch width", .002f, .035f, .012f, false, "", .001f},
            {"scratchDepth", "Scratch depth", 0, .006f, .0018f, false, "", .0001f},
            {"scratchSpread", "Direction spread", 0, 1, .8f, false, "", .01f},
            {"scratchBend", "Scratch curvature", 0, .7f, .42f, false, "", .01f},
            {"scratchScale", "Field scale", .01f, 1000, 6, true, "x", .01f},
            {"angle", "Preferred direction", -180, 180, 25, false, "deg", 1},
            {"surfaceSeed", "Pattern seed", 0, 9999, 17, false, "", 1}};
    if (Op == "latheMetal")
        return {{"depth", "Ring depth", 0, .4f, .2f, false, "scene units", .01f},
            {"detailScale", "Machining scale", 4, 256, 65, false, "x", 1},
            {"roughness", "Surface roughness", .045f, 1, .23f, false, "", .01f},
            {"scratchDensity", "Scratch density", 0, 12, 6, false, "", .1f},
            {"scratchLength", "Scratch length", .1f, 1.65f, 1.15f, false, "", .01f},
            {"scratchWidth", "Scratch width", .002f, .035f, .012f, false, "", .001f},
            {"scratchDepth", "Scratch depth", 0, .006f, .0018f, false, "", .0001f},
            {"scratchSpread", "Direction spread", 0, 1, .8f, false, "", .01f},
            {"scratchBend", "Scratch curvature", 0, .7f, .42f, false, "", .01f},
            {"scratchScale", "Field scale", .01f, 1000, 6, true, "x", .01f},
            {"angle", "Preferred direction", -180, 180, 25, false, "deg", 1},
            {"surfaceSeed", "Pattern seed", 0, 9999, 17, false, "", 1}};
    // 📝 Botanical ranges mirror the Alloy botanical recipe controls; the same keys name the uniforms.
    if (Op == "leaf")
        return {{"detailScale", "Texture scale", 1, 64, 10, false, "x", .1f},
            {"cellScale", "Epidermal cell scale", 20, 400, 120, false, "", 1},
            {"cellRelief", "Cell relief", 0, 1, .5f, false, "", .01f},
            {"cellLobing", "Cell lobing", 0, 1, .65f, false, "", .01f},
            {"stomataDensity", "Stomata density", 0, 1, .22f, false, "", .01f},
            {"veinRelief", "Vein relief", 0, 1, .65f, false, "", .01f},
            {"colorGradient", "Ageing gradient", 0, 1, .18f, false, "", .01f},
            {"surfaceSeed", "Pattern seed", 0, 9999, 17, false, "", 1}};
    if (Op == "grass")
        return {{"plantRibs", "Blade ribs", 1, 40, 10, false, "", 1},
            {"cellScale", "Epidermal cell scale", 20, 400, 120, false, "", 1},
            {"cellRelief", "Cell relief", 0, 1, .5f, false, "", .01f},
            {"veinRelief", "Rib relief", 0, 1, .65f, false, "", .01f},
            {"colorGradient", "Tip gradient", 0, 1, .65f, false, "", .01f},
            {"surfaceSeed", "Pattern seed", 0, 9999, 17, false, "", 1}};
    if (Op == "petal")
        return {{"cellScale", "Epidermal cell scale", 20, 400, 160, false, "", 1},
            {"cellRelief", "Papilla relief", 0, 1, .45f, false, "", .01f},
            {"veinRelief", "Vein relief", 0, 1, .5f, false, "", .01f},
            {"colorGradient", "Throat gradient", 0, 1, .88f, false, "", .01f},
            {"spotDensity", "Fleck density", 0, 1, .06f, false, "", .01f},
            {"surfaceSeed", "Pattern seed", 0, 9999, 17, false, "", 1}};
    if (Op == "stem")
        return {{"detailScale", "Texture scale", 1, 128, 28, false, "x", .1f},
            {"cellScale", "Epidermal cell scale", 20, 400, 150, false, "", 1},
            {"cellRelief", "Cell relief", 0, 1, .5f, false, "", .01f},
            {"veinRelief", "Fibre relief", 0, 1, .6f, false, "", .01f},
            {"colorGradient", "Cork gradient", 0, 1, .55f, false, "", .01f},
            {"spotDensity", "Lenticel density", 0, 1, .65f, false, "", .01f},
            {"surfaceSeed", "Pattern seed", 0, 9999, 17, false, "", 1}};
    if (Op == "cactus")
        return {{"detailScale", "Texture scale", 1, 64, 15, false, "x", .1f},
            {"plantRibs", "Ribs", 3, 24, 10, false, "", 1},
            {"cellScale", "Epidermal cell scale", 20, 400, 120, false, "", 1},
            {"cellRelief", "Cell relief", 0, 1, .5f, false, "", .01f},
            {"veinRelief", "Rib relief", 0, 1, .55f, false, "", .01f},
            {"colorGradient", "Wax gradient", 0, 1, .35f, false, "", .01f},
            {"roughness", "Surface roughness", .045f, 1, .48f, false, "", .01f},
            {"surfaceSeed", "Pattern seed", 0, 9999, 17, false, "", 1}};
    if (Op == "leather")
        return {{"detailScale", "Grain scale", 4, 256, 43, false, "x", 1},
            {"grain", "Grain depth", 0, 1, .75f, false, "", .01f},
            {"hideVariant", "Hide variant", 0, 3, 1, false, "0 nappa - 3 belly", 1},
            {"hideVariation", "Hide variation", 0, 1, .65f, false, "", .01f},
            {"wear", "Polishing wear", 0, 1, .12f, false, "", .01f},
            {"wearSoftness", "Wear softness", 0, 1, .65f, false, "", .01f},
            {"wornRoughness", "Worn roughness", .045f, 1, .24f, false, "", .01f},
            {"scratchScale", "Scuff scale", 1, 128, 32, false, "x", 1},
            {"sheen", "Sheen", 0, 1, 0, false, "", .01f},
            {"surfaceSeed", "Pattern seed", 0, 9999, 17, false, "", 1}};
    if (Op == "crocodile")
        return {{"detailScale", "Plate scale", 1, 32, 5.2f, false, "x", .1f},
            {"grain", "Grain depth", 0, 1, .8f, false, "", .01f},
            {"groutWidth", "Crease width", .004f, .12f, .024f, false, "", .001f},
            {"poreDensity", "Pore density", 0, 1, .8f, false, "", .01f},
            {"hideVariation", "Hide variation", 0, 1, .65f, false, "", .01f},
            {"surfaceSeed", "Pattern seed", 0, 9999, 17, false, "", 1}};
    if (Op == "wax")
        return {{"detailScale", "Texture scale", 1, 256, 40, false, "x", 1},
            {"grain", "Marbling", 0, 1, .6f, false, "", .01f},
            {"depth", "Depth", 0, 2, .5f, false, "", .01f},
            {"scattering", "Scattering", 0, 1, 0, false, "", .01f},
            {"translucency", "Translucency", 0, 1, 0, false, "", .01f},
            {"surfaceSeed", "Pattern seed", 0, 9999, 17, false, "", 1}};
    if (Op == "skin")
        return {{"detailScale", "Texture scale", 1, 256, 90, false, "x", 1},
            {"grain", "Pore depth", 0, 1, .6f, false, "", .01f},
            {"freckles", "Freckles", 0, 1, 0, false, "", .01f},
            {"scattering", "Scattering", 0, 1, 0, false, "", .01f},
            {"surfaceSeed", "Pattern seed", 0, 9999, 17, false, "", 1}};
    if (Op == "paper")
        return {{"detailScale", "Texture scale", 1, 256, 60, false, "x", 1},
            {"grain", "Fibre relief", 0, 1, .6f, false, "", .01f},
            {"fiberContrast", "Fibre contrast", 0, 1, .3f, false, "", .01f},
            {"paperRibs", "Laid ribs", 0, 1, 0, false, "", .01f},
            {"surfaceSeed", "Pattern seed", 0, 9999, 17, false, "", 1}};
    if (Op == "rust")
        return {{"detailScale", "Texture scale", 1, 256, 48, false, "x", 1},
            {"grain", "Crust relief", 0, 1, .8f, false, "", .01f},
            {"oxidation", "Oxidation", 0, 1, .85f, false, "", .01f},
            {"roughness", "Metal roughness", .045f, 1, .36f, false, "", .01f},
            {"surfaceSeed", "Pattern seed", 0, 9999, 17, false, "", 1}};
    if (Op == "ceramic" || Op == "rubber")
        return {{"detailScale", "Grain scale", 1, 256, 80, false, "x", 1},
            {"depth", "Grain depth", 0, 2, .25f, false, "", .01f},
            {"surfaceSeed", "Pattern seed", 0, 9999, 17, false, "", 1}};
    if (Op == "tiles")
        return {{"detailScale", "Tiles per texture", 1, 64, 6, false, "", .1f},
            {"grain", "Bevel relief", 0, 1, .45f, false, "", .01f},
            {"groutWidth", "Grout width", .004f, .3f, .045f, false, "", .001f},
            {"tileVariation", "Tile variation", 0, 1, .1f, false, "", .01f},
            {"tileStagger", "Row stagger", 0, 1, 0, false, "", .01f},
            {"roughness", "Glaze roughness", .045f, 1, .2f, false, "", .01f},
            {"surfaceSeed", "Pattern seed", 0, 9999, 17, false, "", 1}};
    if (Op == "granite")
        return {{"detailScale", "Crystal scale", 1, 256, 26, false, "x", 1},
            {"grain", "Crystal relief", 0, 1, .5f, false, "", .01f},
            {"surfaceSeed", "Pattern seed", 0, 9999, 17, false, "", 1}};
    if (Op == "marble")
        return {{"detailScale", "Texture scale", 1, 128, 9, false, "x", .1f},
            {"grain", "Relief", 0, 1, .4f, false, "", .01f},
            {"veinWidth", "Vein width", .002f, .2f, .035f, false, "", .001f},
            {"surfaceSeed", "Pattern seed", 0, 9999, 17, false, "", 1}};
    if (Op == "concrete")
        return {{"detailScale", "Texture scale", 1, 256, 60, false, "x", 1},
            {"grain", "Aggregate relief", 0, 1, .7f, false, "", .01f},
            {"poreDensity", "Pore density", 0, 1, .5f, false, "", .01f},
            {"wallMode", "Board forming", 0, 1, 0, false, "0 cast - 1 board", 1},
            {"surfaceSeed", "Pattern seed", 0, 9999, 17, false, "", 1}};
    // 📝 Coded-field ranges mirror the HTML generator sheet (MaterialPanel GeneratorInputs limits).
    if (QueryCodedField(Op))
    {
        std::vector<ProceduralOpParam> Params;
        Params.reserve(5);
        Params.emplace_back(ProceduralOpParam{"scale", "Scale", 1, 200, 8, false, "", 1});
        Params.emplace_back(ProceduralOpParam{"seed", "Seed", 0, 9999, 12, false, "", 1});
        Params.emplace_back(ProceduralOpParam{"angle", "Angle", -180, 180, 0, false, "deg", 1});
        Params.emplace_back(ProceduralOpParam{"amount", Op.starts_with("camouflage") ? "Patch contrast" : "Amount", 0, 1, 1, false, "", .01f});
        if (Op == "camouflage-smooth") Params.emplace_back(ProceduralOpParam{"softness", "Softness", .001f, .1f, .012f, false, "", .001f});
        if (Op == "camouflage") Params.emplace_back(ProceduralOpParam{"pixelSize", "Pixel size", 1, 16, 4, false, "", 1});
        return Params;
    }
    return {};
}

std::vector<ProceduralOpTextParam> QueryProceduralOpTextParams(std::string_view Op)
{
    if (Op == "ribbedSheet") return {{"color", "Sheet colour", "#b5c0c2"}};
    if (Op == "leaf") return {{"color", "Leaf colour", "#355d28"}, {"secondaryColor", "Vein colour", "#829548"},
        {"tertiaryColor", "Aged colour", "#8c8a38"}};
    if (Op == "grass") return {{"color", "Blade colour", "#335b27"}, {"secondaryColor", "Tip colour", "#849348"}};
    if (Op == "petal") return {{"color", "Petal colour", "#b83d65"}, {"secondaryColor", "Throat colour", "#edb6bc"},
        {"tertiaryColor", "Fleck colour", "#802342"}};
    if (Op == "stem") return {{"color", "Stem colour", "#53673b"}, {"secondaryColor", "Cork colour", "#827051"},
        {"tertiaryColor", "Lenticel colour", "#b9aa80"}};
    if (Op == "cactus") return {{"color", "Skin colour", "#426d48"}, {"secondaryColor", "Wax colour", "#8faaa0"},
        {"tertiaryColor", "Felt colour", "#bbb095"}};
    if (Op == "crocodile") return {{"color", "Hide colour", "#70432b"}, {"secondaryColor", "Plate colour", "#865335"},
        {"tertiaryColor", "Crease colour", "#59321f"}};
    if (Op == "skin") return {{"color", "Skin colour", "#c9a189"}, {"secondaryColor", "Pigment colour", "#8a5a3c"}};
    if (Op == "paper") return {{"color", "Paper colour", "#d8cdb4"}, {"secondaryColor", "Fibre colour", "#b8a888"}};
    if (Op == "rust") return {{"color", "Metal colour", "#6a6d70"}, {"secondaryColor", "Oxide colour", "#8a4a26"}};
    if (Op == "tiles") return {{"color", "Glaze colour", "#cfd4cd"}, {"secondaryColor", "Grout colour", "#8f8c80"}};
    if (Op == "granite") return {{"color", "Quartz colour", "#b9b4a8"}, {"secondaryColor", "Feldspar colour", "#9c8f80"},
        {"tertiaryColor", "Mica colour", "#3c3a38"}};
    if (Op == "marble") return {{"color", "Stone colour", "#e8e6df"}, {"secondaryColor", "Vein colour", "#7d7a72"}};
    if (Op == "concrete") return {{"color", "Concrete colour", "#a9a49a"}, {"secondaryColor", "Pore colour", "#6f6a60"}};
    if (Op == "camouflage" || Op == "camouflage-smooth")
        return {{"colour0", "Sand colour", "#b5a477"}, {"colour1", "Olive colour", "#566044"},
            {"colour2", "Earth colour", "#514334"}, {"colour3", "Shadow colour", "#232921"}};
    return {};
}

std::string ResolveProceduralParamKey(std::string_view Op, std::string_view Key)
{
    if (Op == "scratches" && (Key == "weaveAngle" || Key == "scratchAngle")) return "angle";
    // 📝 Metal scratch overlays store the same canonical angle; their inspector addresses it as the
    // independent scratch direction, never the construction thread direction.
    if ((Op == "scratchedMetal" || Op == "latheMetal") && Key == "scratchAngle") return "angle";
    // 📝 The Alloy sheet recipe names its rib frequency detailScale; the bake stores ribs per tile.
    if (Op == "ribbedSheet" && Key == "detailScale") return "ribs";
    return std::string(Key);
}

struct GrainFieldSample
{
    float Albedo = 1, Relief = 0, Roughness = 0;
};

float ProceduralSmoothMinimum(float A, float B, float K)
{
    const float H = std::clamp(.5f + .5f * (B - A) / std::max(K, .0001f), 0.f, 1.f);
    return ProceduralMix(B, A, H) - K * H * (1 - H);
}

// 📝 Grain bake mapping: Alloy samples noise3 in object space (pp·uScale); the baker substitutes tile space
// ppm=(u·tiles, seed, v·tiles) so one map carries `tiles` noise cells. Absolute-rate terms (wear patches,
// scuff marks) keep their frequency RATIOS to the micro term, preserving the shader's relationships without
// object-space units; constant-rate axes collapse onto seed slices.
GrainFieldSample EvaluateMetalGrain(float U, float V, float Grain, float Tiles, float Seed)
{
    GrainFieldSample Sample;
    Sample.Albedo = .92f + .1f * ProceduralNoise3(U * Tiles, Seed, V * Tiles);
    Sample.Relief = ProceduralNoise3(U * Tiles, Seed + 43.97f, V * Tiles) * Grain * .003f / .004f;
    return Sample;
}

struct PolymerFieldParameters
{
    float Grain = 0, Tiles = 64, Wear = 0, WearSoftness = .65f, WornRoughness = .24f, ScuffScale = 32, Seed = 17, Base = .5f;
};

GrainFieldSample EvaluatePolymerGrain(float U, float V, const PolymerFieldParameters &P, uint32_t Extent)
{
    const float Px = U * P.Tiles, Pz = V * P.Tiles, PatchRate = 1 / P.Tiles;
    const float Base = ProceduralNoise3(Px, P.Seed, Pz);
    const float Micro = Base + .3f * ProceduralNoise3(Px * 2.7f, P.Seed, Pz * 2.7f);
    const float PatchNoise = ProceduralNoise3(Px * 5.3f * PatchRate, P.Seed, Pz * 5.3f * PatchRate) +
        .28f * ProceduralNoise3(Px * 11.7f * PatchRate, P.Seed, Pz * 11.7f * PatchRate);
    const float Transition = ProceduralMix(.035f, .32f, P.WearSoftness);
    const float WearField = ProceduralSmoothstep(.5f - Transition, .72f + Transition, PatchNoise);
    const float Ceiling = ProceduralMix(1.35f, .22f, std::pow(P.Wear * WearField, .55f));
    const float Polished = ProceduralMix(Micro,
        ProceduralSmoothMinimum(Micro, Ceiling, ProceduralMix(.035f, .24f, P.WearSoftness)),
        ProceduralSmoothstep(0, .08f, P.Wear));
    const float WearMask = ProceduralSmoothstep(.005f, .24f, std::max(0.f, Micro - Polished));
    const float ScuffRate = 8 * P.ScuffScale * PatchRate;
    const float Scuffs = ProceduralNoise3(Px * ScuffRate, P.Seed + 17.31f, Pz * ScuffRate);
    const float Fine = 1 - ProceduralSmoothstep(.5f, 2, 8 * P.ScuffScale / float(Extent));
    const float ScuffWear = ProceduralSmoothstep(.65f, .87f, Scuffs) * Fine * P.Wear * .2f * WearField;
    GrainFieldSample Sample;
    const float AlbedoBase = 1 - P.Grain * .12f + Micro * P.Grain * .15f;
    Sample.Albedo = ProceduralMix(AlbedoBase, AlbedoBase * 1.18f + .012f, WearMask * .6f + ScuffWear);
    Sample.Relief = (Polished * P.Grain * .004f - ScuffWear * .001f * (1 - WearMask * .85f)) / .004f;
    Sample.Roughness = ProceduralMix(std::clamp(P.Base + P.Grain * (Base - .5f) * .16f, .025f, 1.f), P.WornRoughness, WearMask);
    return Sample;
}

std::vector<uint8_t> ConvertGrainAlbedoRgba(const std::vector<float> &Modulator, uint32_t Extent)
{
    if (!Extent || Modulator.size() != size_t(Extent) * Extent)
        throw std::runtime_error("Albedo source is not a square modulation field.");
    // 📝 Grain albedo bakes the shader's brightness modulator as neutral gray; compile multiplies it under
    // the authored conductor or polymer color.
    std::vector<uint8_t> Rgba(size_t(Extent) * Extent * 4);
    for (size_t Index = 0; Index < Modulator.size(); ++Index)
    {
        const uint8_t Gray = uint8_t(std::lround(std::clamp(Modulator[Index], 0.f, 1.f) * 255));
        Rgba[Index * 4] = Rgba[Index * 4 + 1] = Rgba[Index * 4 + 2] = Gray;
        Rgba[Index * 4 + 3] = 255;
    }
    return Rgba;
}

std::vector<uint8_t> ConvertRadianceMaskRgba(const std::vector<float> &Mask, uint32_t Extent)
{
    if (!Extent || Mask.size() != size_t(Extent) * Extent)
        throw std::runtime_error("Radiance source is not a square emitter-mask field.");
    // 📝 The emitter mask is a linear multiplier like albedo, so it sRGB-encodes exactly like the albedo
    // bake; the float pyramid (or sRGB GPU format) restores linear before the shader scales by RadianceNormal.
    std::vector<uint8_t> Rgba(size_t(Extent) * Extent * 4);
    for (size_t Index = 0; Index < Mask.size(); ++Index)
    {
        const uint8_t Gray = uint8_t(std::lround(std::clamp(ProceduralLinearToSrgb(Mask[Index]), 0.f, 1.f) * 255));
        Rgba[Index * 4] = Rgba[Index * 4 + 1] = Rgba[Index * 4 + 2] = Gray;
        Rgba[Index * 4 + 3] = 255;
    }
    return Rgba;
}

std::vector<uint8_t> ConvertRibbedOrmRgba(const std::vector<float> &Roughness, const std::vector<float> &Occlusion,
    const std::vector<float> &Metallic, uint32_t Extent, float Ceiling)
{
    if (!Extent || Roughness.size() != size_t(Extent) * Extent || Occlusion.size() != Roughness.size() ||
        Metallic.size() != Roughness.size() || !(Ceiling > 0) || !(Ceiling <= 1))
        throw std::runtime_error("Ribbed ORM source is not square fields over a unit ceiling.");
    std::vector<uint8_t> Rgba(size_t(Extent) * Extent * 4);
    for (size_t Index = 0; Index < Roughness.size(); ++Index)
    {
        uint8_t *Pixel = &Rgba[Index * 4];
        Pixel[0] = uint8_t(std::lround(std::clamp(Occlusion[Index], 0.f, 1.f) * 255));
        Pixel[1] = uint8_t(std::lround(std::clamp(Roughness[Index] / Ceiling, 0.f, 1.f) * 255));
        Pixel[2] = uint8_t(std::lround(std::clamp(Metallic[Index], 0.f, 1.f) * 255));
        Pixel[3] = 255;
    }
    return Rgba;
}

std::vector<uint8_t> ConvertPolymerOrmRgba(const std::vector<float> &Roughness, uint32_t Extent, float Base, float Grain)
{
    if (!Extent || Roughness.size() != size_t(Extent) * Extent || !(Base > 0) || !(Base <= 1))
        throw std::runtime_error("ORM source is not a square roughness field over a unit base.");
    // 📝 The grain term lifts roughness by at most .08·grain above the base, so the authored scalar carries
    // that ceiling and green divides by it, mirroring the scratch ORM convention at a smaller lift.
    const float Ceiling = std::min(1.f, Base + .08f * Grain);
    std::vector<uint8_t> Rgba(size_t(Extent) * Extent * 4);
    for (size_t Index = 0; Index < Roughness.size(); ++Index)
    {
        uint8_t *Pixel = &Rgba[Index * 4];
        Pixel[0] = 255;
        Pixel[1] = uint8_t(std::lround(std::clamp(Roughness[Index] / Ceiling, 0.f, 1.f) * 255));
        Pixel[2] = 255;
        Pixel[3] = 255;
    }
    return Rgba;
}

std::vector<uint8_t> ConvertSolarOrmRgba(const std::vector<float> &Roughness, const std::vector<float> &Metallic,
    uint32_t Extent, float Ceiling)
{
    if (!Extent || Roughness.size() != size_t(Extent) * Extent || Metallic.size() != Roughness.size() ||
        !(Ceiling > 0) || !(Ceiling <= 1))
        throw std::runtime_error("Solar ORM source is not square fields over a unit ceiling.");
    // 📝 Green divides by the contact ceiling like the scratch convention; blue carries Alloy's absolute
    // cell/contact metallic mix, so the authored scalar stays 1.
    std::vector<uint8_t> Rgba(size_t(Extent) * Extent * 4);
    for (size_t Index = 0; Index < Roughness.size(); ++Index)
    {
        uint8_t *Pixel = &Rgba[Index * 4];
        Pixel[0] = 255;
        Pixel[1] = uint8_t(std::lround(std::clamp(Roughness[Index] / Ceiling, 0.f, 1.f) * 255));
        Pixel[2] = uint8_t(std::lround(std::clamp(Metallic[Index], 0.f, 1.f) * 255));
        Pixel[3] = 255;
    }
    return Rgba;
}

int WeavePatternIndex(std::string_view Pattern)
{
    if (Pattern == "plain") return 0;
    if (Pattern == "twill") return 1;
    if (Pattern == "satin") return 2;
    if (Pattern == "basket") return 3;
    if (Pattern == "rib") return 4;
    if (Pattern == "herringbone") return 5;
    if (Pattern == "oxford") return 6;
    if (Pattern == "houndstooth") return 7;
    if (Pattern == "denim") return 8;
    throw std::runtime_error("Weave pattern is not a known construction.");
}

std::vector<uint8_t> BakeProceduralChannelRgba(std::string_view Op, std::string_view Channel,
    const std::map<std::string, double> &Params, uint32_t Extent)
{
    return BakeProceduralChannelRgba(Op, Channel, Params, {}, Extent);
}

std::vector<uint8_t> BakeProceduralChannelRgba(std::string_view Op, std::string_view Channel,
    const std::map<std::string, double> &Params, const std::map<std::string, std::string> &Texts, uint32_t Extent)
{
    const auto Supported = QueryProceduralOpChannels(Op);
    if (std::find(Supported.begin(), Supported.end(), Channel) == Supported.end())
        throw std::runtime_error("Procedural op does not bake the requested channel.");
    const auto Value = [&](const char *Key, double Fallback)
    {
        const auto Found = Params.find(Key);
        return Found != Params.end() ? Found->second : Fallback;
    };
    if (Op == "metalGrain" || Op == "polymerGrain")
    {
        if (!Extent || Extent > 2048) throw std::runtime_error("Grain bake extent is unbounded.");
        PolymerFieldParameters P;
        P.Grain = float(Value("grain", 0));
        P.Tiles = float(Value("detailScale", 0));
        P.Wear = float(Value("wear", 0));
        P.WearSoftness = float(Value("wearSoftness", .65));
        P.WornRoughness = float(Value("wornRoughness", .24));
        P.ScuffScale = float(Value("scratchScale", 32));
        P.Seed = float(Value("surfaceSeed", 0));
        P.Base = float(std::clamp(Value("roughness", .5), .045, 1.));
        if (!(P.Tiles > 0)) throw std::runtime_error("Procedural grain params cannot bake.");
        std::vector<float> Field(size_t(Extent) * Extent);
        for (uint32_t Y = 0; Y < Extent; ++Y)
            for (uint32_t X = 0; X < Extent; ++X)
            {
                const float U = (float(X) + .5f) / float(Extent), V = (float(Y) + .5f) / float(Extent);
                const auto Sample = Op == "metalGrain" ? EvaluateMetalGrain(U, V, P.Grain, P.Tiles, P.Seed)
                                                      : EvaluatePolymerGrain(U, V, P, Extent);
                Field[size_t(Y) * Extent + X] =
                    Channel == "Albedo" ? Sample.Albedo : Channel == "Orm" ? Sample.Roughness : Sample.Relief;
            }
        if (Channel == "Albedo") return ConvertGrainAlbedoRgba(Field, Extent);
        if (Channel == "Orm") return ConvertPolymerOrmRgba(Field, Extent, P.Base, P.Grain);
        return ConvertHeightToNormalRgba(Field, Extent, 2.f);
    }
    if (Op == "ribbedSheet")
    {
        if (!Extent || Extent > 2048) throw std::runtime_error("Ribbed-sheet bake extent is unbounded.");
        const double Ribs = std::clamp(Value("ribs", 4), 1., 12.);
        const double Depth = std::clamp(Value("ribDepth", .2), 0., .4);
        // 📝 Alloy ribDepth is scene units against a .2 reference: full relief at .2, flat at 0.
        const float Amplitude = Depth <= 0 ? 0 : std::clamp(float(Depth * 5.), .3f, 1.f);
        const bool Trapezoid = Value("ribProfile", 0) > .5;
        const double Radians = Value("weaveAngle", 0) * 3.141592653589793 / 180;
        const float CosA = float(std::cos(Radians)), SinA = float(std::sin(Radians));
        const float Spangle = float(std::clamp(Value("sheetSpangle", .3), 0., 1.));
        const float Oxidation = float(std::clamp(Value("sheetOxidation", .1), 0., 1.));
        const float Base = float(std::clamp(Value("roughness", .4), .045, 1.));
        const float Seed = float(Value("surfaceSeed", 17));
        float SheetRgb[3]{1, 1, 1};
        if (Channel == "Albedo")
        {
            const auto Found = Texts.find("color");
            if (!DecodeHexLinearRgb(Found != Texts.end() ? Found->second : "#b5c0c2", SheetRgb))
                throw std::runtime_error("Ribbed-sheet colour is not #rrggbb.");
        }
        std::vector<float> Height(size_t(Extent) * Extent), Roughness(size_t(Extent) * Extent),
            Occlusion(size_t(Extent) * Extent), Metal(size_t(Extent) * Extent),
            Linear(size_t(Extent) * Extent * 3);
        for (uint32_t Y = 0; Y < Extent; ++Y)
            for (uint32_t X = 0; X < Extent; ++X)
            {
                const float U = (float(X) + .5f) / float(Extent), V = (float(Y) + .5f) / float(Extent);
                const float Along = U * CosA + V * SinA;
                float Phase = float(Along * Ribs);
                Phase -= std::floor(Phase);
                const float Sine = std::sin(Phase * 2 * 3.141592653589793f);
                const float Rib = Trapezoid ? std::copysign(std::pow(std::abs(Sine), .55f), Sine) : Sine;
                const float Relief01 = Rib * .5f + .5f;
                const float Mu = ProceduralMirror(U * 3 + Seed * .13f), Mv = ProceduralMirror(V * 3 + Seed * .29f);
                const float SpangleField = ProceduralStoneFbm(Mu * 4 + Seed, Mv * 4, Seed * 1.7f);
                const float Oxide = ProceduralSmoothstep(.68f, .95f,
                    ProceduralStoneFbm(Mu * 5 + Seed + 13.7f, Mv * 5 + 7.3f, Seed + 5.1f));
                const float Ao = .88f + .12f * Relief01;
                const float Tone = std::max(0.f,
                    Ao * (1 + (SpangleField - .5f) * .14f * Spangle) * (1 - Oxide * .5f * Oxidation));
                const size_t Index = size_t(Y) * Extent + X;
                Height[Index] = Rib * Amplitude;
                Roughness[Index] =
                    std::clamp(Base + (SpangleField - .5f) * .1f * Spangle + Oxide * .3f * Oxidation, .05f, 1.f);
                Occlusion[Index] = Ao;
                Metal[Index] = std::clamp(1 - Oxide * Oxidation, 0.f, 1.f);
                Linear[Index * 3] = SheetRgb[0] * Tone;
                Linear[Index * 3 + 1] = SheetRgb[1] * Tone;
                Linear[Index * 3 + 2] = SheetRgb[2] * Tone;
            }
        if (Channel == "Albedo") return ConvertLinearAlbedoRgba(Linear, Extent);
        if (Channel == "Orm")
        {
            const float Ceiling = std::min(1.f, Base + .05f * Spangle + .3f * Oxidation);
            return ConvertRibbedOrmRgba(Roughness, Occlusion, Metal, Extent, Ceiling);
        }
        return ConvertHeightToNormalRgba(Height, Extent, 6.f);
    }
    if (QueryCodedField(Op))
    {
        if (!Extent || Extent > 2048) throw std::runtime_error("Coded field bake extent is unbounded.");
        CodedFieldParameters Field;
        Field.Scale = Value("scale", 8);
        Field.Seed = Value("seed", 12);
        Field.Softness = Value("softness", .012);
        Field.PixelSize = Value("pixelSize", 4);
        const double Angle = Value("angle", 0), Amount = Value("amount", 1);
        // 📝 Palette colours decode like THREE.Color(hex): sRGB text into linear working triples, then the
        // shared albedo converter sRGB-encodes for upload, so the authored hex survives the round trip.
        std::vector<std::array<float, 3>> Palette;
        for (const auto &TextParam : QueryProceduralOpTextParams(Op))
        {
            const auto Found = Texts.find(TextParam.Key);
            float Rgb[3]{1, 1, 1};
            if (!DecodeHexLinearRgb(Found != Texts.end() ? Found->second : TextParam.Default, Rgb))
                throw std::runtime_error("Coded field palette colour is not #rrggbb.");
            Palette.push_back({Rgb[0], Rgb[1], Rgb[2]});
        }
        Field.ColourCount = Palette.empty() ? 4 : uint32_t(Palette.size());
        std::vector<float> Positions(size_t(Extent) * Extent);
        for (uint32_t Y = 0; Y < Extent; ++Y)
            for (uint32_t X = 0; X < Extent; ++X)
                Positions[size_t(Y) * Extent + X] = float(EvaluateCodedFieldPosition(Op, Field,
                    double(X) / Extent, double(Y) / Extent, Angle, Amount));
        if (Channel == "Normal") return ConvertHeightToNormalRgba(Positions, Extent, 2.f);
        std::vector<float> Linear(size_t(Extent) * Extent * 3);
        for (size_t Index = 0; Index < Positions.size(); ++Index)
        {
            const double Position = std::clamp(double(Positions[Index]), 0., 1.);
            std::array<float, 3> Colour{float(Position), float(Position), float(Position)};
            if (!Palette.empty())
            {
                const double Slot = Position * double(Palette.size() - 1);
                if (Op == "camouflage-smooth")
                {
                    const size_t First = std::min(Palette.size() - 2, size_t(std::floor(Slot)));
                    const float Blend = float(Slot - double(First));
                    for (size_t Component = 0; Component < 3; ++Component)
                        Colour[Component] = Palette[First][Component] +
                            (Palette[First + 1][Component] - Palette[First][Component]) * Blend;
                }
                else Colour = Palette[size_t(std::lround(Slot))];
            }
            Linear[Index * 3] = Colour[0];
            Linear[Index * 3 + 1] = Colour[1];
            Linear[Index * 3 + 2] = Colour[2];
        }
        return ConvertLinearAlbedoRgba(Linear, Extent);
    }
    if (Op == "solar")
    {
        if (!Extent || Extent > 2048) throw std::runtime_error("Solar bake extent is unbounded.");
        const float Tiles = float(Value("detailScale", 0));
        const float BusWidth = float(Value("busbarWidth", .016));
        const double PanelMode = Value("panelMode", 0);
        const float Base = float(std::clamp(Value("roughness", .2), .045, 1.));
        if (!(Tiles > 0)) throw std::runtime_error("Procedural solar params cannot bake.");
        float CellRgb[3]{1, 1, 1}, ContactRgb[3]{1, 1, 1};
        const auto CellText = Texts.find("color"), ContactText = Texts.find("secondaryColor");
        if (!DecodeHexLinearRgb(CellText != Texts.end() ? CellText->second : "#172742", CellRgb) ||
            !DecodeHexLinearRgb(ContactText != Texts.end() ? ContactText->second : "#bec6c4", ContactRgb))
            throw std::runtime_error("Solar cell colours are not #rrggbb.");
        // 📝 Single-face bake of the uType==17 block: cell separators, triple busbars, collection fingers
        // and the polycrystalline Worley grain, with contact-driven roughness and metallic answers.
        std::vector<float> Linear(size_t(Extent) * Extent * 3), Roughness(size_t(Extent) * Extent),
            Metal(size_t(Extent) * Extent), Height(size_t(Extent) * Extent);
        const float Step = Tiles / float(Extent);
        for (uint32_t Y = 0; Y < Extent; ++Y)
            for (uint32_t X = 0; X < Extent; ++X)
            {
                const float UvX = (float(X) + .5f) / float(Extent) * Tiles, UvY = (float(Y) + .5f) / float(Extent) * Tiles;
                const float Qx = ProceduralFract(UvX), Qy = ProceduralFract(UvY);
                const float Edge = std::min(std::min(Qx, 1 - Qx), std::min(Qy, 1 - Qy));
                const float CellMask = ProceduralSmoothstep(.013f, .013f + std::max(Step, .004f), Edge);
                const float Bus = 1 - ProceduralSmoothstep(BusWidth, BusWidth + std::max(Step, .002f),
                    std::abs(ProceduralFract(UvX * 3) - .5f) / 3);
                const float Fingers = ProceduralMix(.05f,
                    1 - ProceduralSmoothstep(.025f, .025f + std::max(Step * 36, .025f),
                        std::abs(ProceduralFract(UvY * 36) - .5f)),
                    1 - ProceduralSmoothstep(.2f, .8f, Step * 36));
                const float Contact = CellMask * std::max(Bus, Fingers * .7f);
                float Silicon[3]{CellRgb[0], CellRgb[1], CellRgb[2]};
                if (PanelMode > .5 && PanelMode < 1.5)
                {
                    float Crystal[4]{0, 0, 0, 0};
                    EvaluateCellular3(UvX * 9, UvY * 9, .3f, 7, Crystal);
                    for (int Component = 0; Component < 3; ++Component) Silicon[Component] *= .65f + Crystal[1] * .75f;
                }
                const size_t Index = size_t(Y) * Extent + X;
                for (int Component = 0; Component < 3; ++Component)
                    Linear[Index * 3 + size_t(Component)] = ProceduralMix(
                        ProceduralMix(.008f, Silicon[Component], CellMask), ContactRgb[Component], Contact);
                Roughness[Index] = ProceduralMix(Base, .22f, Contact);
                Metal[Index] = ProceduralMix(.25f, .96f, Contact);
                Height[Index] = CellMask * .001f + Contact * .0007f;
            }
        if (Channel == "Albedo") return ConvertLinearAlbedoRgba(Linear, Extent);
        if (Channel == "Orm") return ConvertSolarOrmRgba(Roughness, Metal, Extent, std::min(1.f, Base + .02f));
        for (float &Relief : Height) Relief /= .0017f;
        return ConvertHeightToNormalRgba(Height, Extent, 2.f);
    }
    if (Op == "led")
    {
        if (!Extent || Extent > 2048) throw std::runtime_error("LED bake extent is unbounded.");
        const float Tiles = float(Value("detailScale", 0));
        const float PixelFill = float(Value("pixelFill", .65));
        const float LensDome = float(Value("lensDome", .7));
        const float PackageDepth = float(Value("packageDepth", .6));
        const float Seed = float(Value("surfaceSeed", 0));
        if (!(Tiles > 0)) throw std::runtime_error("Procedural LED params cannot bake.");
        float SubstrateRgb[3]{1, 1, 1};
        const auto SubstrateText = Texts.find("color");
        if (!DecodeHexLinearRgb(SubstrateText != Texts.end() ? SubstrateText->second : "#111518", SubstrateRgb))
            throw std::runtime_error("LED substrate colour is not #rrggbb.");
        // 📝 Single-face bake of the uType==20 block: recessed package, domed lens, emitter die with
        // sub-dies, contacts, traces and bond wire. Pixel footprint stands in for fwidth exactly as the
        // solar bake does; relief normalizes by the fixed .017 full-depth nominal so the lens/package
        // sliders drive bump strength instead of normalizing it away.
        std::vector<float> Linear(size_t(Extent) * Extent * 3), Height(size_t(Extent) * Extent),
            Mask(size_t(Extent) * Extent);
        const float Step = Tiles / float(Extent);
        const float Aa = std::max(Step, .0015f);
        const float Radius = PixelFill * .33f;
        for (uint32_t Y = 0; Y < Extent; ++Y)
            for (uint32_t X = 0; X < Extent; ++X)
            {
                const float U = (float(X) + .5f) / float(Extent), V = (float(Y) + .5f) / float(Extent);
                const float UvX = U * Tiles, UvY = V * Tiles;
                const float Qx = ProceduralFract(UvX) - .5f, Qy = ProceduralFract(UvY) - .5f;
                const float BevelX = std::max(std::abs(Qx) - .31f, 0.f), BevelY = std::max(std::abs(Qy) - .27f, 0.f);
                const float Housing =
                    1 - ProceduralSmoothstep(.035f - Aa, .035f + Aa, std::hypot(BevelX, BevelY));
                const float Recess =
                    1 - ProceduralSmoothstep(-Aa, Aa, std::max(std::abs(Qx) - .265f, std::abs(Qy) - .235f));
                const float R = std::hypot(Qx, Qy) / std::max(Radius, .01f);
                const float Lens = 1 - ProceduralSmoothstep(1 - Aa / Radius, 1 + Aa / Radius, R);
                const float Dome = std::sqrt(std::max(0.f, 1 - R * R));
                const float Pads = 1 -
                    ProceduralSmoothstep(-Aa, Aa, std::max(std::abs(std::abs(Qx) - .423f) - .044f, std::abs(Qy) - .11f));
                const float Traces = 1 -
                    ProceduralSmoothstep(-Aa, Aa, std::max(std::abs(Qx) - .5f, std::abs(std::abs(Qy) - .447f) - .007f));
                float Wire = 1 - ProceduralSmoothstep(.003f, .003f + Aa, SegmentDistance(Qx, Qy, -.22f, .13f, .04f, 0));
                Wire *= Recess * (1 - Lens * .7f);
                const float Die = 1 - ProceduralSmoothstep(-Aa, Aa,
                    std::max(std::abs(Qx) - Radius * .45f, std::abs(Qy) - Radius * .38f));
                const float SubDies = 1 -
                    ProceduralSmoothstep(.03f, .1f, std::abs(ProceduralFract(Qx / std::max(Radius, .01f) * 4) - .5f));
                const float Ring = (1 - ProceduralSmoothstep(.018f, .018f + Aa, std::abs(std::hypot(Qx, Qy) - Radius * 1.13f))) *
                    (1 - Lens);
                const float Contact = std::max(Pads, std::max(Traces * .65f, Wire));
                const float BoardTone = .88f + .15f * ProceduralNoise3(U * 120, Seed, V * 120);
                const size_t Index = size_t(Y) * Extent + X;
                float Pixel[3]{SubstrateRgb[0] * BoardTone, SubstrateRgb[1] * BoardTone, SubstrateRgb[2] * BoardTone};
                constexpr float HousingRgb[3]{.055f, .062f, .07f}, RecessRgb[3]{.008f, .012f, .017f},
                    RingRgb[3]{.13f, .15f, .17f}, ContactRgb[3]{.66f, .47f, .17f}, DieRgb[3]{.34f, .3f, .18f};
                for (int Component = 0; Component < 3; ++Component)
                    Pixel[Component] = ProceduralMix(Pixel[Component], HousingRgb[Component], Housing);
                for (int Component = 0; Component < 3; ++Component)
                    Pixel[Component] = ProceduralMix(Pixel[Component], RecessRgb[Component], Recess);
                for (int Component = 0; Component < 3; ++Component)
                    Pixel[Component] = ProceduralMix(Pixel[Component], RingRgb[Component], Ring * .8f);
                for (int Component = 0; Component < 3; ++Component)
                    Pixel[Component] = ProceduralMix(Pixel[Component], ContactRgb[Component], Contact);
                for (int Component = 0; Component < 3; ++Component)
                    Pixel[Component] = ProceduralMix(Pixel[Component], DieRgb[Component], Die);
                Linear[Index * 3] = Pixel[0];
                Linear[Index * 3 + 1] = Pixel[1];
                Linear[Index * 3 + 2] = Pixel[2];
                Mask[Index] = Lens * (.2f + Die * (.65f + .15f * SubDies)) *
                    (.88f + .12f * ProceduralHash31(std::floor(UvX), std::floor(UvY), 19));
                Height[Index] = (Housing * .003f - Recess * .001f + Lens * Dome * LensDome * .012f + Pads * .002f) *
                    PackageDepth;
            }
        if (Channel == "Albedo") return ConvertLinearAlbedoRgba(Linear, Extent);
        if (Channel == "Radiance") return ConvertRadianceMaskRgba(Mask, Extent);
        for (float &Relief : Height) Relief /= .017f;
        return ConvertHeightToNormalRgba(Height, Extent, 2.f);
    }
    if (Op == "fruit")
    {
        if (!Extent || Extent > 2048) throw std::runtime_error("Fruit bake extent is unbounded.");
        const float Tiles = float(Value("detailScale", 0));
        const float SkinMode = float(Value("skinMode", 0));
        const float SpotDensity = float(Value("spotDensity", 0));
        const float ColorGradient = float(Value("colorGradient", 0));
        const float Grain = float(Value("grain", 0));
        const float Seed = float(Value("surfaceSeed", 0));
        if (!(Tiles > 0)) throw std::runtime_error("Procedural fruit params cannot bake.");
        float BaseRgb[3]{1, 1, 1}, SecondaryRgb[3]{1, 1, 1}, TertiaryRgb[3]{1, 1, 1};
        const auto BaseText = Texts.find("color");
        const auto SecondaryText = Texts.find("secondaryColor");
        const auto TertiaryText = Texts.find("tertiaryColor");
        if (!DecodeHexLinearRgb(BaseText != Texts.end() ? BaseText->second : "#8f2029", BaseRgb) ||
            !DecodeHexLinearRgb(SecondaryText != Texts.end() ? SecondaryText->second : "#c5ac52", SecondaryRgb) ||
            !DecodeHexLinearRgb(TertiaryText != Texts.end() ? TertiaryText->second : "#e3c697", TertiaryRgb))
            throw std::runtime_error("Fruit palette colour is not #rrggbb.");
        // 📝 Single-face bake of the uType==34 block: blush over stripes, lenticel/pit/seed pores and waxed
        // skin grain on the cylindrical fruit mapping. Two adaptations keep the tiling bake honest: pore rows
        // round to an integer count (93.6 rows would cut a seam at every tile edge) and the mesh pole cap is
        // dropped (it guards vProcUv poles, meaningless on a repeating tile).
        const float Around = std::floor(Tiles * (SkinMode > 1.5f ? 2.6f : 2.f) + .5f);
        const float PoreRows = std::round(Tiles * (SkinMode > 1.5f ? .9f : 1.3f));
        const float Aa = std::max(std::hypot(Around, PoreRows) / float(Extent) * .5f, .001f);
        const float Resolved = 1 - ProceduralSmoothstep(.35f, 1.3f, Aa);
        const float Nominal = SkinMode < .5f ? .000475f : SkinMode < 1.5f ? .002225f : .005f;
        std::vector<float> Linear(size_t(Extent) * Extent * 3), Height(size_t(Extent) * Extent);
        std::vector<float> PoreMask(size_t(Extent) * Extent, 0);
        float Cylinder[3];
        float Pore[4];
        for (uint32_t Y = 0; Y < Extent; ++Y)
            for (uint32_t X = 0; X < Extent; ++X)
            {
                const float U = (float(X) + .5f) / float(Extent), V = (float(Y) + .5f) / float(Extent);
                ProceduralBioCylinder(U, V, 8, 3, Seed, Cylinder);
                const float Pigment = ProceduralStoneFbm(Cylinder[0], Cylinder[1], Cylinder[2]);
                ProceduralBioCell(U * Around, V * PoreRows, Seed, Around, Pore);
                const size_t Index = size_t(Y) * Extent + X;
                if (SkinMode < .5f)
                {
                    ProceduralBioCylinder(U, V, 55, 6, Seed, Cylinder);
                    const float Stripes = ProceduralNoise3(Cylinder[0], Cylinder[1], Cylinder[2]);
                    const float Blush = ProceduralSmoothstep(.36f, .7f, Pigment + Stripes * .12f) * ColorGradient;
                    const float Spots = (1 - ProceduralSmoothstep(.025f, .065f + Aa, Pore[0])) *
                        (Pore[2] <= SpotDensity ? 1.f : 0.f) * Resolved;
                    for (int Component = 0; Component < 3; ++Component)
                        Linear[Index * 3 + Component] = ProceduralMix(ProceduralMix(BaseRgb[Component],
                            SecondaryRgb[Component], Blush), TertiaryRgb[Component], Spots * .8f);
                    ProceduralBioCylinder(U, V, 160, 160, Seed, Cylinder);
                    Height[Index] = (ProceduralNoise3(Cylinder[0], Cylinder[1], Cylinder[2]) - .5f) * Grain * .00045f -
                        Spots * .00025f;
                    PoreMask[Index] = Spots;
                }
                else if (SkinMode < 1.5f)
                {
                    const float GlandRadius = ProceduralMix(.2f, .34f, Pore[2]);
                    const float Pits = (1 - ProceduralSmoothstep(.055f, GlandRadius + Aa, Pore[0])) * Resolved;
                    const float Tone = .88f + Pigment * .24f - Pits * .065f;
                    for (int Component = 0; Component < 3; ++Component)
                        Linear[Index * 3 + Component] = BaseRgb[Component] * Tone;
                    ProceduralBioCylinder(U, V, Around * 2, Tiles * 2, Seed, Cylinder);
                    const float Rind = ProceduralNoise3(Cylinder[0], Cylinder[1], Cylinder[2]);
                    Height[Index] = ((.35f - Pits) * .003f + (Rind - .5f) * .00055f) * Grain;
                    PoreMask[Index] = Pits;
                }
                else
                {
                    const float Pit = (1 - ProceduralSmoothstep(.11f, .36f + Aa, Pore[0])) * Resolved;
                    const float SeedMask = (1 - ProceduralSmoothstep(.045f, .13f + Aa, Pore[0])) * Resolved;
                    const float Tone = .83f + Pigment * .25f;
                    for (int Component = 0; Component < 3; ++Component)
                        Linear[Index * 3 + Component] =
                            ProceduralMix(BaseRgb[Component] * Tone, SecondaryRgb[Component], SeedMask * .92f);
                    const float Ratio = Pore[0] / .13f;
                    Height[Index] = (-Pit * .005f + std::exp(-Ratio * Ratio) * Resolved * .004f) * Grain;
                    PoreMask[Index] = Pit;
                }
            }
        if (Channel == "Albedo") return ConvertLinearAlbedoRgba(Linear, Extent);
        // 📝 Alloy mixes roughness toward min(1, base+.24) inside pores (bioPore); the authored
        // scalar carries that ceiling through the .24 op lift.
        if (Channel == "Orm")
        {
            const float Base = float(std::clamp(Value("roughness", .34), .045, 1.));
            return ConvertMaskedOrmRgba(PoreMask, Extent, Base, std::min(1.f, Base + .24f), false);
        }
        for (float &Relief : Height) Relief /= Nominal;
        return ConvertHeightToNormalRgba(Height, Extent, 2.f);
    }
    if (Op == "dimples")
    {
        if (!Extent || Extent > 2048) throw std::runtime_error("Dimple bake extent is unbounded.");
        const float Tiles = float(Value("detailScale", 0));
        const float Depth = float(Value("dimpleDepth", 0));
        const float Seed = float(Value("surfaceSeed", 0));
        if (!(Tiles > 0) || !(Depth > 0)) throw std::runtime_error("Procedural dimple params cannot bake.");
        // 📝 Single-face bake of the uType==18 block; the albedo carries the cover grain modulator under
        // the authored colour, like the bare-metal tooth.
        std::vector<float> Field(size_t(Extent) * Extent);
        const float Step = Tiles / float(Extent);
        for (uint32_t Y = 0; Y < Extent; ++Y)
            for (uint32_t X = 0; X < Extent; ++X)
            {
                const float U = (float(X) + .5f) / float(Extent), V = (float(Y) + .5f) / float(Extent);
                const float Bowl = EvaluateDimple(U, V, Tiles, Step);
                Field[size_t(Y) * Extent + X] =
                    Channel == "Albedo" ? .97f + ProceduralNoise3(U * Tiles, Seed, V * Tiles) * .04f : -Bowl * Depth / Depth;
            }
        if (Channel == "Albedo") return ConvertGrainAlbedoRgba(Field, Extent);
        return ConvertHeightToNormalRgba(Field, Extent, 2.f);
    }
    if (Op == "stucco")
    {
        if (!Extent || Extent > 2048) throw std::runtime_error("Stucco bake extent is unbounded.");
        const float Tiles = float(Value("detailScale", 0));
        const float Grain = float(Value("grain", 0));
        const float Seed = float(Value("surfaceSeed", 0));
        if (!(Tiles > 0)) throw std::runtime_error("Procedural stucco params cannot bake.");
        // 📝 Single-face bake of the uType==26 block: sand aggregate over a broad cloud drift; the fixed
        // .011 nominal keeps the grain slider driving bump strength instead of normalizing it away.
        std::vector<float> Field(size_t(Extent) * Extent);
        for (uint32_t Y = 0; Y < Extent; ++Y)
            for (uint32_t X = 0; X < Extent; ++X)
            {
                const float U = (float(X) + .5f) / float(Extent), V = (float(Y) + .5f) / float(Extent);
                const float Sand = ProceduralStoneFbm(U * Tiles, Seed, V * Tiles);
                const float Cloud = ProceduralStoneFbm(U * 2.8f, Seed, V * 2.8f);
                Field[size_t(Y) * Extent + X] = Channel == "Albedo" ? .8f + Cloud * .28f + Sand * .09f
                    : (Sand - .5f) * Grain * .022f / .011f;
            }
        if (Channel == "Albedo") return ConvertGrainAlbedoRgba(Field, Extent);
        return ConvertHeightToNormalRgba(Field, Extent, 2.f);
    }
    if (Op == "clay")
    {
        if (!Extent || Extent > 2048) throw std::runtime_error("Clay bake extent is unbounded.");
        const float Tiles = float(Value("detailScale", 0));
        const float Grain = float(Value("grain", 0));
        const float Moisture = float(Value("moisture", 0));
        const float Seed = float(Value("surfaceSeed", 0));
        if (!(Tiles > 0)) throw std::runtime_error("Procedural clay params cannot bake.");
        // 📝 Single-face bake of the uType==12 block: fine aggregate over finger-scale hand variation, with
        // moisture darkening the tone and damping the relief. Relief keeps the shader's grain/moisture
        // relationship at the baker's unit nominal (the .006 scene factor belongs to the GL bump).
        std::vector<float> Field(size_t(Extent) * Extent);
        for (uint32_t Y = 0; Y < Extent; ++Y)
            for (uint32_t X = 0; X < Extent; ++X)
            {
                const float U = (float(X) + .5f) / float(Extent), V = (float(Y) + .5f) / float(Extent);
                const float Aggregate = ProceduralNoise3(U * Tiles, Seed, V * Tiles) +
                    .35f * ProceduralNoise3(U * Tiles * 3.7f, Seed, V * Tiles * 3.7f);
                const float Hand = ProceduralNoise3(U * 2.7f, Seed, V * 2.7f);
                Field[size_t(Y) * Extent + X] = Channel == "Albedo"
                    ? (.8f + .23f * Aggregate + .08f * Hand) * (1 - Moisture * .18f)
                    : (Aggregate - .5f) * Grain * 2 * (1 - Moisture * .5f);
            }
        if (Channel == "Albedo") return ConvertGrainAlbedoRgba(Field, Extent);
        return ConvertHeightToNormalRgba(Field, Extent, 2.f);
    }
    if (Op == "weave")
    {
        WeaveFieldParameters P;
        // 📝 Carbon fibre is the type-7 weave shading variant; every woven fabric stays type 4.
        P.Type = int(Value("surfaceType", 4));
        const auto Pattern = Texts.find("weavePattern");
        P.Weave = WeavePatternIndex(Pattern != Texts.end() ? Pattern->second : "twill");
        P.FabricMode = int(Value("fabricMode", 0));
        P.Scale = float(Value("detailScale", 0));
        P.AngleRadians = float(Value("weaveAngle", 0) * 3.141592653589793 / 180);
        P.Relief = float(Value("weaveRelief", 0));
        P.FiberDetail = float(Value("fiberDetail", 12));
        P.Fuzz = float(Value("fuzz", 0));
        P.Slub = float(Value("slub", 0));
        P.DenimFade = float(Value("denimFade", 0));
        P.Grain = float(Value("grain", 0));
        P.Seed = float(Value("surfaceSeed", 0));
        const auto WarpText = Texts.find("warpColor"), WeftText = Texts.find("weftColor");
        if (!DecodeHexLinearRgb(WarpText != Texts.end() ? WarpText->second : "#b5b0a2", P.Warp) ||
            !DecodeHexLinearRgb(WeftText != Texts.end() ? WeftText->second : "#b5b0a2", P.Weft))
            throw std::runtime_error("Weave yarn colours are not #rrggbb.");
        auto Maps = BakeWeaveMaps(P, Extent);
        if (Channel == "Albedo") return ConvertLinearAlbedoRgba(Maps.Albedo, Extent);
        const float Amplitude = WeaveReliefAmplitude(P);
        if (Amplitude > 0)
            for (float &Relief : Maps.Relief) Relief /= Amplitude;
        return ConvertHeightToNormalRgba(Maps.Relief, Extent, 2.f);
    }
    if (Op == "knit")
    {
        KnitFieldParameters P;
        P.Scale = float(Value("detailScale", 0));
        P.Grain = float(Value("grain", 0));
        P.Fuzz = float(Value("fuzz", 0));
        P.Seed = float(Value("surfaceSeed", 0));
        P.AngleRadians = float(Value("weaveAngle", 0) * 3.141592653589793 / 180);
        const auto DyeText = Texts.find("color"), MelangeText = Texts.find("secondaryColor");
        if (!DecodeHexLinearRgb(DyeText != Texts.end() ? DyeText->second : "#8d9294", P.Color) ||
            !DecodeHexLinearRgb(MelangeText != Texts.end() ? MelangeText->second : "#e2ddd0", P.Secondary))
            throw std::runtime_error("Knit yarn colours are not #rrggbb.");
        auto Maps = BakeKnitMaps(P, Extent);
        if (Channel == "Albedo") return ConvertLinearAlbedoRgba(Maps.Albedo, Extent);
        const float Amplitude = KnitReliefAmplitude(P);
        if (Amplitude > 0)
            for (float &Relief : Maps.Relief) Relief /= Amplitude;
        return ConvertHeightToNormalRgba(Maps.Relief, Extent, 2.f);
    }
    if (Op == "scratchedMetal" || Op == "latheMetal")
    {
        if (!Extent || Extent > 2048) throw std::runtime_error("Metal bake extent is unbounded.");
        const float Tiles = float(Value("detailScale", 0));
        if (!(Tiles > 0)) throw std::runtime_error("Procedural metal params cannot bake.");
        const float Seed = float(Value("surfaceSeed", 0));
        const float Base = float(std::clamp(Value("roughness", .3), .045, 1.));
        ScratchFieldParameters Cuts;
        Cuts.Density = float(Value("scratchDensity", 0));
        Cuts.Length = float(Value("scratchLength", 0));
        Cuts.Width = float(Value("scratchWidth", 0));
        Cuts.Depth = float(Value("scratchDepth", 0));
        Cuts.Spread = float(Value("scratchSpread", 0));
        Cuts.Bend = float(Value("scratchBend", 0));
        Cuts.Scale = float(Value("scratchScale", 1));
        Cuts.AngleRadians = float(Value("angle", 0) * 3.141592653589793 / 180);
        Cuts.Seed = Value("surfaceSeed", 0) < 0 ? 0 : uint32_t(Value("surfaceSeed", 0));
        if (Cuts.Density > 0 && (!(Cuts.Depth > 0) || !(Cuts.Scale > 0)))
            throw std::runtime_error("Procedural scratch params cannot bake.");
        // 📝 Bare type-1 tooth (or type-6 lathe rings) under the optional study cuts. Uncut metal matches
        // the metalGrain bytes exactly; the shared .004 nominal preserves Alloy's tooth-to-cut ratio.
        const float Tooth = Op == "scratchedMetal" ? float(Value("grain", 0)) : 0;
        const float Depth = Op == "latheMetal" ? float(Value("depth", 0)) : 0;
        const float RingStep = Tiles * 3.3f / float(Extent);
        // 📝 The cut field is baked once per channel through the memoized path instead of
        // re-evaluating every scratch instance per pixel (identical float results).
        const auto CutMaps = BakeScratchMaps(Cuts, Extent);
        std::vector<float> Field(size_t(Extent) * Extent);
        for (uint32_t Y = 0; Y < Extent; ++Y)
            for (uint32_t X = 0; X < Extent; ++X)
            {
                const float U = (float(X) + .5f) / float(Extent), V = (float(Y) + .5f) / float(Extent);
                const size_t Index = size_t(Y) * Extent + X;
                const float CutHeight = CutMaps.Height[Index], CutCoverage = CutMaps.Coverage[Index];
                if (Channel == "Orm")
                {
                    Field[Index] = CutCoverage;
                    continue;
                }
                if (Op == "scratchedMetal")
                {
                    const auto Bare = EvaluateMetalGrain(U, V, Tooth, Tiles, Seed);
                    Field[Index] = Channel == "Albedo" ? Bare.Albedo : Bare.Relief + CutHeight / .004f;
                    continue;
                }
                const float Rx = (U - .5f) * Tiles, Ry = (V - .5f) * Tiles;
                const float Phase = std::sqrt(Rx * Rx + Ry * Ry) * 3.3f;
                const float Lines =
                    std::sin(Phase) * .5f * (1 - ProceduralSmoothstep(.5f, 3.1f, RingStep)) + .5f;
                Field[Index] = Channel == "Albedo" ? .84f + Lines * .14f : (Lines * .0001f * Depth + CutHeight) / .004f;
            }
        if (Channel == "Albedo") return ConvertGrainAlbedoRgba(Field, Extent);
        if (Channel == "Orm") return ConvertScratchOrmRgba(Field, Extent, Base);
        return ConvertHeightToNormalRgba(Field, Extent, 2.f);
    }
    if (Op == "leaf" || Op == "grass" || Op == "petal" || Op == "stem")
    {
        if (!Extent || Extent > 2048) throw std::runtime_error("Epidermal bake extent is unbounded.");
        EpidermalParameters P;
        P.Type = Op == "leaf" ? 31 : Op == "grass" ? 32 : Op == "petal" ? 33 : 36;
        P.Scale = float(Value("detailScale", 0));
        P.CellScale = float(Value("cellScale", 0));
        P.CellRelief = float(Value("cellRelief", 0));
        P.VeinRelief = float(Value("veinRelief", 0));
        P.CellLobing = float(Value("cellLobing", 0));
        P.StomataDensity = float(Value("stomataDensity", 0));
        P.ColorGradient = float(Value("colorGradient", 0));
        P.SpotDensity = float(Value("spotDensity", 0));
        P.PlantRibs = float(Value("plantRibs", 0));
        P.Seed = float(Value("surfaceSeed", 0));
        if (!(P.CellScale > 0) || ((P.Type == 31 || P.Type == 36) && !(P.Scale > 0)) ||
            (P.Type == 32 && !(P.PlantRibs > 0)))
            throw std::runtime_error("Procedural epidermal params cannot bake.");
        const auto Text = [&](const char *Key, const char *Fallback, float Rgb[3])
        {
            const auto Found = Texts.find(Key);
            if (!DecodeHexLinearRgb(Found != Texts.end() ? Found->second : Fallback, Rgb))
                throw std::runtime_error("Epidermal palette colour is not #rrggbb.");
        };
        Text("color", "#355d28", P.Base);
        Text("secondaryColor", "#829548", P.Secondary);
        Text("tertiaryColor", "#8c8a38", P.Tertiary);
        auto Maps = BakeEpidermalMaps(P, Extent);
        if (Channel == "Albedo") return ConvertLinearAlbedoRgba(Maps.Linear, Extent);
        // 📝 Fixed per-type height nominals (the shader's own maxima at unit relief) keep the relief
        // sliders driving bump strength instead of normalizing it away.
        const float Nominal = P.Type == 31 ? .0025f : P.Type == 32 ? .0021f : P.Type == 33 ? .0011f : .0018f;
        for (float &Relief : Maps.Height) Relief /= Nominal;
        return ConvertHeightToNormalRgba(Maps.Height, Extent, 2.f);
    }
    if (Op == "cactus")
    {
        if (!Extent || Extent > 2048) throw std::runtime_error("Cactus bake extent is unbounded.");
        CactusParameters P;
        P.Scale = float(Value("detailScale", 0));
        P.CellScale = float(Value("cellScale", 0));
        P.CellRelief = float(Value("cellRelief", 0));
        P.VeinRelief = float(Value("veinRelief", 0));
        P.ColorGradient = float(Value("colorGradient", 0));
        P.PlantRibs = float(Value("plantRibs", 0));
        P.Seed = float(Value("surfaceSeed", 0));
        if (!(P.Scale > 0) || !(P.CellScale > 0) || !(P.PlantRibs > 0))
            throw std::runtime_error("Procedural cactus params cannot bake.");
        const auto Text = [&](const char *Key, const char *Fallback, float Rgb[3])
        {
            const auto Found = Texts.find(Key);
            if (!DecodeHexLinearRgb(Found != Texts.end() ? Found->second : Fallback, Rgb))
                throw std::runtime_error("Cactus palette colour is not #rrggbb.");
        };
        Text("color", "#426d48", P.Base);
        Text("secondaryColor", "#8faaa0", P.Secondary);
        Text("tertiaryColor", "#bbb095", P.Tertiary);
        auto Maps = BakeCactusMaps(P, Extent);
        if (Channel == "Albedo") return ConvertLinearAlbedoRgba(Maps.Linear, Extent);
        if (Channel == "Orm")
        {
            const float Base = float(std::clamp(Value("roughness", .48), .045, 1.));
            return ConvertMaskedOrmRgba(Maps.Pore, Extent, Base, std::min(1.f, Base + .24f), false);
        }
        for (float &Relief : Maps.Height) Relief /= .0054f;
        return ConvertHeightToNormalRgba(Maps.Height, Extent, 2.f);
    }
    if (Op == "leather")
    {
        if (!Extent || Extent > 2048) throw std::runtime_error("Leather bake extent is unbounded.");
        const float Tiles = float(Value("detailScale", 0));
        const float Grain = float(Value("grain", 0));
        const int Patch = std::clamp(int(Value("hideVariant", 1)), 0, 3);
        const float Variation = float(Value("hideVariation", .65));
        const float Wear = float(Value("wear", 0));
        const float WearSoftness = float(Value("wearSoftness", .65));
        const float ScuffScale = float(Value("scratchScale", 32));
        const float Seed = float(Value("surfaceSeed", 0));
        if (!(Tiles > 0)) throw std::runtime_error("Procedural leather params cannot bake.");
        // 📝 Single-face bake of the uType==11 block over hideGrain: the vector hide field is warped
        // per vectorHide, micro tooth and pores ride it at bake resolution, and the wear polish
        // (smooth-minimum ceiling) brightens high spots exactly as the shared 8..11 block does.
        // The worn-roughness ORM term stays scalar-authored (documented in the catalogue audit).
        const auto Hide = BuildHidePatch(Patch);
        const float Fine = 1 - ProceduralSmoothstep(.5f, 1.5f, Tiles * 10 * std::sqrt(2.f) / float(Extent));
        const float ScuffFine = 1 - ProceduralSmoothstep(.5f, 2, ScuffScale * 8 * std::sqrt(2.f) / float(Extent));
        std::vector<float> Field(size_t(Extent) * Extent);
        for (uint32_t Y = 0; Y < Extent; ++Y)
            for (uint32_t X = 0; X < Extent; ++X)
            {
                const float U = (float(X) + .5f) / float(Extent), V = (float(Y) + .5f) / float(Extent);
                const float HideScale = Tiles / 18 * (Patch == 2 ? 2.f : 1.f);
                const float Source = EvaluateVectorHide(Hide, U * HideScale, V * HideScale, Variation, Seed);
                const float MicroNoise = ProceduralNoise3(U * Tiles * 7.3f, Seed, V * Tiles * 7.3f);
                const float Pores = ProceduralSmoothstep(.72f, .88f, ProceduralNoise3(U * Tiles * 14, Seed, V * Tiles * 14));
                float Micro = Source + (MicroNoise - .5f) * .025f * Fine - Pores * .025f * Fine;
                const float Fold = std::clamp((.74f - Source) * 3, 0.f, 1.f);
                const float PatchNoise = ProceduralNoise3(U * 5.3f, Seed, V * 5.3f) +
                    .28f * ProceduralNoise3(U * 11.7f, Seed, V * 11.7f);
                const float Transition = ProceduralMix(.035f, .32f, WearSoftness);
                const float WearField = ProceduralSmoothstep(.5f - Transition, .72f + Transition, PatchNoise);
                const float Ceiling = ProceduralMix(1.35f, .22f, std::pow(std::max(Wear * WearField, 0.f), .55f));
                const float Polished = ProceduralMix(Micro,
                    ProceduralSmoothMinimum(Micro, Ceiling, ProceduralMix(.035f, .24f, WearSoftness)),
                    ProceduralSmoothstep(0, .08f, Wear));
                const float WearMask = ProceduralSmoothstep(.005f, .24f, std::max(0.f, Micro - Polished));
                const float Marks = ProceduralSmoothstep(.65f, .87f,
                    ProceduralNoise3(U * ScuffScale * 8, Seed, V * ScuffScale * 8)) * ScuffFine;
                const float ScratchWear = Marks * Wear * .2f * WearField;
                const size_t Index = size_t(Y) * Extent + X;
                if (Channel == "Albedo")
                {
                    float Tone = (1 - Fold * .035f - Pores * Fine * .02f) * (1 - Grain * .12f + Micro * Grain * .15f);
                    Tone = ProceduralMix(Tone, Tone * 1.18f + .012f, std::clamp(WearMask * .6f + ScratchWear, 0.f, 1.f));
                    Field[Index] = Tone;
                }
                else
                    Field[Index] = (Polished * Grain * .003f - ScratchWear * .001f * (1 - WearMask * .85f)) / .003f;
            }
        if (Channel == "Albedo") return ConvertGrainAlbedoRgba(Field, Extent);
        return ConvertHeightToNormalRgba(Field, Extent, 2.f);
    }
    if (Op == "crocodile")
    {
        if (!Extent || Extent > 2048) throw std::runtime_error("Crocodile bake extent is unbounded.");
        const float Tiles = float(Value("detailScale", 0));
        const float Grain = float(Value("grain", 0));
        const float GroutWidth = float(Value("groutWidth", .024));
        const float PoreDensity = float(Value("poreDensity", 0));
        const float Variation = float(Value("hideVariation", .65));
        const float Seed = float(Value("surfaceSeed", 0));
        if (!(Tiles > 0) || !(GroutWidth > 0)) throw std::runtime_error("Procedural crocodile params cannot bake.");
        float BaseRgb[3]{1, 1, 1}, SecondaryRgb[3]{1, 1, 1}, TertiaryRgb[3]{1, 1, 1};
        const auto Text = [&](const char *Key, const char *Fallback, float Rgb[3])
        {
            const auto Found = Texts.find(Key);
            if (!DecodeHexLinearRgb(Found != Texts.end() ? Found->second : Fallback, Rgb))
                throw std::runtime_error("Crocodile palette colour is not #rrggbb.");
        };
        Text("color", "#70432b", BaseRgb);
        Text("secondaryColor", "#865335", SecondaryRgb);
        Text("tertiaryColor", "#59321f", TertiaryRgb);
        // 📝 Single-face bake of the uType==30 block: belly-patch plates with the grout-width crease
        // remap; the bioJoint/leatherGrain roughness terms stay scalar-authored (catalogue audit).
        const auto Hide = BuildHidePatch(3);
        const float Resolved = 1 - ProceduralSmoothstep(.5f, 1.5f, Tiles * .14f * 160 * std::sqrt(2.f) / float(Extent));
        const float CreaseExponent = std::sqrt(.024f / std::max(GroutWidth, .001f));
        std::vector<float> Linear(size_t(Extent) * Extent * 3), Height(size_t(Extent) * Extent);
        for (uint32_t Y = 0; Y < Extent; ++Y)
            for (uint32_t X = 0; X < Extent; ++X)
            {
                const float U = (float(X) + .5f) / float(Extent), V = (float(Y) + .5f) / float(Extent);
                const float RawU = U * Tiles * .14f, RawV = V * Tiles * .14f;
                float Source = EvaluateVectorHide(Hide, RawU, RawV, Variation, Seed);
                Source = .7451f - .35f * std::pow(std::clamp((.7451f - Source) / .35f, 0.f, 1.f), CreaseExponent);
                const float Crease = std::clamp((.74f - Source) * 3, 0.f, 1.f);
                const float GrainNoise = ProceduralNoise3(RawU * 160, RawV * 160, 17);
                const float Drift = ProceduralNoise3(RawU * .7f, RawV * .7f, 13);
                const float Pores = ProceduralSmoothstep(.74f, .9f,
                    ProceduralNoise3(RawU * 140, RawV * 180, Seed)) * PoreDensity;
                const size_t Index = size_t(Y) * Extent + X;
                for (int Component = 0; Component < 3; ++Component)
                {
                    float Tone = ProceduralMix(BaseRgb[Component], TertiaryRgb[Component], Crease * .07f);
                    Tone = ProceduralMix(Tone, SecondaryRgb[Component], Drift * .08f);
                    Linear[Index * 3 + Component] = Tone;
                }
                Height[Index] = ((Source - .74f) * .004f + (GrainNoise - .5f) * .00007f * Resolved -
                    Pores * .00015f * Resolved) * Grain / .0014f;
            }
        if (Channel == "Albedo") return ConvertLinearAlbedoRgba(Linear, Extent);
        return ConvertHeightToNormalRgba(Height, Extent, 2.f);
    }
    if (Op == "wax" || Op == "skin" || Op == "paper" || Op == "rust" || Op == "ceramic" || Op == "rubber")
    {
        if (!Extent || Extent > 2048) throw std::runtime_error("Surface bake extent is unbounded.");
        const float Tiles = float(Value("detailScale", 0));
        const float Grain = float(Value("grain", 0));
        const float Seed = float(Value("surfaceSeed", 0));
        if (!(Tiles > 0)) throw std::runtime_error("Procedural surface params cannot bake.");
        // 📝 Single-face bakes of the extended-surface blocks (uType 13 wax, 14 skin, 15 paper,
        // 16 rust) and the type 2/3 grain one-liners, under the house tile-space mapping
        // ppm=(u*tiles, seed, v*tiles); absolute-rate terms keep their authored rates like clay.
        float BaseRgb[3]{1, 1, 1}, SecondaryRgb[3]{1, 1, 1};
        const bool Coloured = Op == "skin" || Op == "paper" || Op == "rust";
        if (Coloured)
        {
            const auto BaseText = Texts.find("color"), SecondaryText = Texts.find("secondaryColor");
            if (!DecodeHexLinearRgb(BaseText != Texts.end() ? BaseText->second : "#c9a189", BaseRgb) ||
                !DecodeHexLinearRgb(SecondaryText != Texts.end() ? SecondaryText->second : "#8a5a3c", SecondaryRgb))
                throw std::runtime_error("Surface palette colour is not #rrggbb.");
        }
        std::vector<float> Linear(size_t(Extent) * Extent * 3), Field(size_t(Extent) * Extent);
        std::vector<float> Mask(size_t(Extent) * Extent, 0);
        for (uint32_t Y = 0; Y < Extent; ++Y)
            for (uint32_t X = 0; X < Extent; ++X)
            {
                const float U = (float(X) + .5f) / float(Extent), V = (float(Y) + .5f) / float(Extent);
                const size_t Index = size_t(Y) * Extent + X;
                const float GrainNoise = ProceduralNoise3(U * Tiles, Seed, V * Tiles);
                if (Op == "ceramic" || Op == "rubber")
                {
                    const float Depth = float(Value("depth", 0));
                    Field[Index] = Channel == "Albedo" ? (Op == "ceramic" ? .6f + .65f * GrainNoise : .84f + .22f * GrainNoise)
                        : GrainNoise * Depth;
                    continue;
                }
                if (Op == "wax")
                {
                    const float Warp = ProceduralNoise3(U * 5.7f, Seed, V * 5.7f) * 2;
                    const float Marble = ProceduralNoise3(U * Tiles * .12f + Warp, Seed + Warp, V * Tiles * .12f + Warp);
                    Field[Index] = Channel == "Albedo" ? .9f + Marble * Grain * .28f : (GrainNoise - .5f) * Grain;
                    continue;
                }
                float Rgb[3]{BaseRgb[0], BaseRgb[1], BaseRgb[2]};
                float Height = 0;
                if (Op == "skin")
                {
                    const float Pit = std::pow(ProceduralSmoothstep(.48f, .88f, GrainNoise), 2.f);
                    const float Pigment = ProceduralNoise3(U * 7.3f, Seed, V * 7.3f);
                    const float Freckles = float(Value("freckles", 0));
                    const float FreckleMask = ProceduralSmoothstep(1 - std::max(.0001f, Freckles * .45f), 1,
                        ProceduralNoise3(U * Tiles * .27f, Seed, V * Tiles * .27f));
                    const float Tone = .94f + .09f * ProceduralNoise3(U * 3.1f, Seed, V * 3.1f);
                    for (int Component = 0; Component < 3; ++Component)
                        Rgb[Component] = ProceduralMix(Rgb[Component], SecondaryRgb[Component],
                            Pigment * .13f + FreckleMask * .5f) * Tone;
                    Height = (-Pit * Grain * .0007f + ProceduralNoise3(U * Tiles * 2.1f, Seed, V * Tiles * 2.1f) * .00015f) / .00085f;
                }
                else if (Op == "paper")
                {
                    // 📝 pp*vec3(s*.12, s*3.7, s): the front face varies pp.xy, so fibres stretch
                    // along U exactly as authored; the slice axis keeps the seed.
                    const float Fibers = ProceduralNoise3(U * Tiles * .12f, V * Tiles * 3.7f, Seed);
                    const float Crossing = ProceduralNoise3(U * Tiles * 2.1f, V * Tiles * .2f, Seed);
                    const float Pulp = ProceduralSmoothstep(.58f, .87f, Fibers) * (.4f + .6f * Crossing);
                    const float Contrast = float(Value("fiberContrast", .3));
                    for (int Component = 0; Component < 3; ++Component)
                        Rgb[Component] = ProceduralMix(Rgb[Component], SecondaryRgb[Component], Pulp * Contrast) *
                            (.95f + GrainNoise * .08f);
                    const float Wave = std::sin(U * 22) * .5f + .5f;
                    Height = ((Fibers * .6f + Crossing * .4f) * Grain * .0015f +
                        Wave * float(Value("paperRibs", 0)) * .005f) / .0015f;
                }
                else
                {
                    const float Patchiness = ProceduralNoise3(U * Tiles * .1f, Seed, V * Tiles * .1f) * .7f +
                        ProceduralNoise3(U * Tiles * .31f, Seed, V * Tiles * .31f) * .3f;
                    const float Oxidation = float(Value("oxidation", 0));
                    const float Oxide = Oxidation <= 0 ? 0 : Oxidation >= 1 ? 1 :
                        ProceduralSmoothstep(1 - Oxidation - .15f, 1 - Oxidation + .15f, Patchiness);
                    const float Crust = GrainNoise + .3f * ProceduralNoise3(U * Tiles * 3, Seed, V * Tiles * 3);
                    for (int Component = 0; Component < 3; ++Component)
                        Rgb[Component] = ProceduralMix(Rgb[Component], SecondaryRgb[Component] * (.65f + Crust * .7f), Oxide);
                    Height = Oxide * (.002f + Crust * Grain * .008f) / .0104f;
                    Mask[Index] = Oxide;
                }
                for (int Component = 0; Component < 3; ++Component) Linear[Index * 3 + Component] = Rgb[Component];
                Field[Index] = Height;
            }
        if (Channel == "Albedo")
            return Coloured ? ConvertLinearAlbedoRgba(Linear, Extent) : ConvertGrainAlbedoRgba(Field, Extent);
        if (Channel == "Orm")
        {
            // 📝 Oxide mixes roughness to .94 and kills metalness; the authored scalar carries .94.
            const float Base = float(std::clamp(Value("roughness", .36), .045, 1.));
            return ConvertMaskedOrmRgba(Mask, Extent, Base, std::max(Base, .94f), true);
        }
        return ConvertHeightToNormalRgba(Field, Extent, 2.f);
    }
    if (Op == "tiles")
    {
        if (!Extent || Extent > 2048) throw std::runtime_error("Tile bake extent is unbounded.");
        const float Tiles = float(Value("detailScale", 0));
        const float Grain = float(Value("grain", 0));
        const float GroutWidth = float(Value("groutWidth", .045));
        const float Variation = float(Value("tileVariation", 0));
        const float Stagger = float(Value("tileStagger", 0));
        const float Seed = float(Value("surfaceSeed", 0));
        if (!(Tiles > 0)) throw std::runtime_error("Procedural tile params cannot bake.");
        float BaseRgb[3]{1, 1, 1}, SecondaryRgb[3]{1, 1, 1};
        const auto BaseText = Texts.find("color"), SecondaryText = Texts.find("secondaryColor");
        if (!DecodeHexLinearRgb(BaseText != Texts.end() ? BaseText->second : "#cfd4cd", BaseRgb) ||
            !DecodeHexLinearRgb(SecondaryText != Texts.end() ? SecondaryText->second : "#8f8c80", SecondaryRgb))
            throw std::runtime_error("Tile palette colour is not #rrggbb.");
        const float Aa = std::max(Tiles / float(Extent), .001f);
        std::vector<float> Linear(size_t(Extent) * Extent * 3), Height(size_t(Extent) * Extent), Grout(size_t(Extent) * Extent);
        for (uint32_t Y = 0; Y < Extent; ++Y)
            for (uint32_t X = 0; X < Extent; ++X)
            {
                const float U = (float(X) + .5f) / float(Extent), V = (float(Y) + .5f) / float(Extent);
                float TileU = U * Tiles, TileV = V * Tiles;
                TileU += std::floor(TileV) * Stagger;
                const float TileX = std::floor(TileU), TileY = std::floor(TileV);
                const float Qu = TileU - TileX, Qv = TileV - TileY;
                const float Edge = std::min(std::min(Qu, 1 - Qu), std::min(Qv, 1 - Qv));
                const float GroutMask = 1 - ProceduralSmoothstep(GroutWidth * .5f - Aa, GroutWidth * .5f + Aa, Edge);
                const float Bevel = ProceduralSmoothstep(GroutWidth * .5f, GroutWidth * .5f + .065f, Edge);
                const float TileRandom = ProceduralHash31(TileX, TileY, 31);
                const float GrainNoise = ProceduralNoise3(U * Tiles, Seed, V * Tiles);
                const size_t Index = size_t(Y) * Extent + X;
                for (int Component = 0; Component < 3; ++Component)
                {
                    const float Glaze = BaseRgb[Component] * (1 + (TileRandom - .5f) * Variation);
                    Linear[Index * 3 + Component] = ProceduralMix(Glaze,
                        SecondaryRgb[Component] * (.9f + GrainNoise * .16f), GroutMask);
                }
                Height[Index] = (Bevel * Grain * .012f + (1 - GroutMask) *
                    ProceduralNoise3(U * Tiles * 5, Seed, V * Tiles * 5) * Variation * .003f) / .015f;
                Grout[Index] = GroutMask;
            }
        if (Channel == "Albedo") return ConvertLinearAlbedoRgba(Linear, Extent);
        if (Channel == "Orm")
        {
            const float Base = float(std::clamp(Value("roughness", .2), .045, 1.));
            return ConvertMaskedOrmRgba(Grout, Extent, Base, std::max(Base, .93f), false);
        }
        return ConvertHeightToNormalRgba(Height, Extent, 2.f);
    }
    if (Op == "granite" || Op == "marble" || Op == "concrete")
    {
        if (!Extent || Extent > 2048) throw std::runtime_error("Stone bake extent is unbounded.");
        const float Tiles = float(Value("detailScale", 0));
        const float Grain = float(Value("grain", 0));
        const float Seed = float(Value("surfaceSeed", 0));
        if (!(Tiles > 0)) throw std::runtime_error("Procedural stone params cannot bake.");
        float BaseRgb[3]{1, 1, 1}, SecondaryRgb[3]{1, 1, 1}, TertiaryRgb[3]{1, 1, 1};
        const auto Text = [&](const char *Key, const char *Fallback, float Rgb[3])
        {
            const auto Found = Texts.find(Key);
            if (!DecodeHexLinearRgb(Found != Texts.end() ? Found->second : Fallback, Rgb))
                throw std::runtime_error("Stone palette colour is not #rrggbb.");
        };
        Text("color", "#b9b4a8", BaseRgb);
        Text("secondaryColor", "#9c8f80", SecondaryRgb);
        Text("tertiaryColor", "#3c3a38", TertiaryRgb);
        std::vector<float> Linear(size_t(Extent) * Extent * 3), Height(size_t(Extent) * Extent);
        const float Pixel = 1.f / float(Extent);
        const auto MarbleField = [&](float U, float V)
        {
            const float Dx = U * Tiles, Dy = V * Tiles;
            const float WarpX = ProceduralStoneFbm(Dx * .6f, Dy * .6f, Seed * .6f);
            const float WarpY = ProceduralStoneFbm(Dx * .6f + 13, Dy * .6f + 13, Seed * .6f + 13);
            const float WarpZ = ProceduralStoneFbm(Dx * .6f + 37, Dy * .6f + 37, Seed * .6f + 37);
            return std::array<float, 4>{ProceduralStoneFbm(Dx + WarpX * 5.2f, Dy + WarpY * 5.2f, Seed + WarpZ * 5.2f),
                WarpX, WarpY, WarpZ};
        };
        for (uint32_t Y = 0; Y < Extent; ++Y)
            for (uint32_t X = 0; X < Extent; ++X)
            {
                const float U = (float(X) + .5f) / float(Extent), V = (float(Y) + .5f) / float(Extent);
                const size_t Index = size_t(Y) * Extent + X;
                float Rgb[3]{BaseRgb[0], BaseRgb[1], BaseRgb[2]};
                float Relief = 0;
                if (Op == "granite")
                {
                    const float Dx = U * Tiles, Dy = V * Tiles;
                    const float WarpX = ProceduralNoise3(Dx * .3f, Dy * .3f, Seed * .3f) * .6f;
                    const float WarpY = ProceduralNoise3(Dx * .3f + 7, Dy * .3f + 7, Seed * .3f + 7) * .6f;
                    const float WarpZ = ProceduralNoise3(Dx * .3f + 21, Dy * .3f + 21, Seed * .3f + 21) * .6f;
                    float Mineral[4];
                    ProceduralCellular3(Dx + WarpX, Dy + WarpY, Seed + WarpZ, 19, Mineral);
                    const float *Crystal = Mineral[1] < .23f ? TertiaryRgb : Mineral[1] < .58f ? SecondaryRgb : BaseRgb;
                    const float Seam = 1 - ProceduralSmoothstep(.005f, .035f, Mineral[0]);
                    const float Resolved = 1 - ProceduralSmoothstep(.4f, 1.5f, Tiles * std::sqrt(2.f) / float(Extent));
                    for (int Component = 0; Component < 3; ++Component)
                    {
                        const float Mean = TertiaryRgb[Component] * .23f + SecondaryRgb[Component] * .35f + BaseRgb[Component] * .42f;
                        Rgb[Component] = ProceduralMix(Mean,
                            Crystal[Component] * (.8f + Mineral[2] * .35f) * (1 - Seam * .13f), Resolved);
                    }
                    Relief = (Mineral[3] - .5f) * Grain * (1 - Seam) * Resolved;
                }
                else if (Op == "marble")
                {
                    const auto Field = MarbleField(U, V);
                    const float FieldAa = std::max(std::abs(MarbleField(U + Pixel, V)[0] - Field[0]) +
                        std::abs(MarbleField(U, V + Pixel)[0] - Field[0]), .001f);
                    const float VeinWidth = float(Value("veinWidth", .035));
                    float Vein = 1 - ProceduralSmoothstep(VeinWidth - FieldAa, VeinWidth + FieldAa, std::abs(Field[0] - .5f));
                    const float Fine = 1 - ProceduralSmoothstep(.007f, .018f,
                        std::abs(ProceduralNoise3(U * Tiles * 3.7f + Field[1] * 7, V * Tiles * 3.7f + Field[2] * 7,
                            Seed + Field[3] * 7) - .5f));
                    Vein = std::max(Vein, Fine * .26f);
                    for (int Component = 0; Component < 3; ++Component)
                        Rgb[Component] = ProceduralMix(Rgb[Component] * (.93f + .12f * Field[0]),
                            SecondaryRgb[Component], Vein * .86f);
                    Relief = (Field[0] - .5f) * Grain;
                }
                else
                {
                    const float Sand = ProceduralStoneFbm(U * Tiles, V * Tiles, Seed);
                    const float Cloud = ProceduralStoneFbm(U * 2.8f, V * 2.8f, Seed);
                    const float PoreDensity = float(Value("poreDensity", 0));
                    const float Pores = PoreDensity <= 0 ? 0 : ProceduralSmoothstep(1 - PoreDensity * .42f, 1,
                        ProceduralNoise3(U * Tiles * .45f, Seed, V * Tiles * .45f));
                    for (int Component = 0; Component < 3; ++Component)
                        Rgb[Component] = ProceduralMix(Rgb[Component] * (.8f + Cloud * .28f + Sand * .09f),
                            SecondaryRgb[Component], Pores * .65f);
                    float StoneHeight = (Sand - .5f) * Grain * .006f - Pores * .006f;
                    if (Value("wallMode", 0) > .5)
                    {
                        const float Seam = 1 - ProceduralSmoothstep(.009f, .025f,
                            std::abs(ProceduralFract(V * 1.7f) - .5f));
                        const float Timber = ProceduralNoise3(U * 5, V * 120, Seed);
                        StoneHeight -= Seam * .003f;
                        StoneHeight += Timber * .0007f;
                        for (int Component = 0; Component < 3; ++Component)
                            Rgb[Component] *= 1 - Seam * .13f + (Timber - .5f) * .06f;
                    }
                    Relief = StoneHeight / .006f;
                }
                for (int Component = 0; Component < 3; ++Component) Linear[Index * 3 + Component] = Rgb[Component];
                Height[Index] = Relief;
            }
        if (Channel == "Albedo") return ConvertLinearAlbedoRgba(Linear, Extent);
        return ConvertHeightToNormalRgba(Height, Extent, 2.f);
    }
    ScratchFieldParameters Field;
    Field.Density = float(Value("scratchDensity", 0));
    Field.Length = float(Value("scratchLength", 0));
    Field.Width = float(Value("scratchWidth", 0));
    Field.Depth = float(Value("scratchDepth", 0));
    Field.Spread = float(Value("scratchSpread", 0));
    Field.Bend = float(Value("scratchBend", 0));
    Field.Scale = float(Value("scratchScale", 1));
    Field.AngleRadians = float(Value("angle", 0) * 3.141592653589793 / 180);
    Field.Seed = Value("surfaceSeed", 0) < 0 ? 0 : uint32_t(Value("surfaceSeed", 0));
    if (!(Field.Density > 0) || !(Field.Depth > 0) || !(Field.Scale > 0))
        throw std::runtime_error("Procedural scratch params cannot bake.");
    auto Maps = BakeScratchMaps(Field, Extent);
    if (Channel == "Normal")
    {
        for (float &Height : Maps.Height) Height /= Field.Depth;
        return ConvertHeightToNormalRgba(Maps.Height, Extent, 2.f);
    }
    const float Base = float(std::clamp(Value("roughness", .2), .045, 1.));
    return ConvertScratchOrmRgba(Maps.Coverage, Extent, Base);
}

static void AppendProceduralPngBytes(void *Context, void *Data, int Size)
{
    auto *Png = static_cast<std::vector<uint8_t> *>(Context);
    const auto *Bytes = static_cast<const uint8_t *>(Data);
    Png->insert(Png->end(), Bytes, Bytes + Size);
}

std::vector<uint8_t> EncodeProceduralPng(const std::vector<uint8_t> &Rgba, uint32_t Extent)
{
    if (!Extent || Rgba.size() != size_t(Extent) * Extent * 4) throw std::runtime_error("Procedural PNG source is not square RGBA.");
    std::vector<uint8_t> Png;
    if (!stbi_write_png_to_func(AppendProceduralPngBytes, &Png, int(Extent), int(Extent), 4, Rgba.data(), 0)) throw std::runtime_error("Procedural PNG encode failed.");
    return Png;
}
}
