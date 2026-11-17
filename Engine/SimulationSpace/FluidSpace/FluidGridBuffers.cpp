/*============================================================================================================================================
                                                           FLUIDGRIDBUFFERS.CPP
============================================================================================================================================*/
// 🧩 Allocation, indexing, and trilinear sampling for Hero Pyro Grid & Continuous World Drift Smoke Grid

#include "Engine/SimulationSpace/FluidSpace/FluidGridBuffers.h"

#include <algorithm>
#include <cmath>

namespace Frontier::SimulationSpace::FluidSpace
{

void FluidGridVolume::Allocate(uint32_t NewResolution)
{
    Resolution = std::clamp(NewResolution, 8u, 128u);
    const size_t Count = static_cast<size_t>(Resolution) * Resolution * Resolution;
    VelocitySolid[0].assign(Count, {});
    VelocitySolid[1].assign(Count, {});
    Thermo[0].assign(Count, {});
    Thermo[1].assign(Count, {});
    Curl.assign(Count, {});
    Divergence.assign(Count, {});
    Pressure[0].assign(Count, {});
    Pressure[1].assign(Count, {});
    SceneSolidMask.assign(Count, 0.0f);
    PingIndex = 0u;
}

void FluidGridVolume::ClearState() noexcept
{
    const auto ZeroVec = [](std::vector<FluidVoxelSample4>& V) { std::fill(V.begin(), V.end(), FluidVoxelSample4{}); };
    ZeroVec(VelocitySolid[0]);
    ZeroVec(VelocitySolid[1]);
    ZeroVec(Thermo[0]);
    ZeroVec(Thermo[1]);
    ZeroVec(Curl);
    ZeroVec(Divergence);
    ZeroVec(Pressure[0]);
    ZeroVec(Pressure[1]);
    std::fill(SceneSolidMask.begin(), SceneSolidMask.end(), 0.0f);
    PingIndex = 0u;
}

uint32_t FluidGridVolume::FlatIndex(int32_t IX, int32_t IY, int32_t IZ, uint32_t Res) noexcept
{
    const int32_t MaxI = static_cast<int32_t>(Res) - 1;
    const int32_t CX   = std::clamp(IX, 0, MaxI);
    const int32_t CY   = std::clamp(IY, 0, MaxI);
    const int32_t CZ   = std::clamp(IZ, 0, MaxI);
    return static_cast<uint32_t>(CX + (CY + CZ * static_cast<int32_t>(Res)) * static_cast<int32_t>(Res));
}

FluidVoxelSample4 FluidGridVolume::SampleTrilinear(
    const std::vector<FluidVoxelSample4>& Buffer,
    const Mathematics::Axis3D&            Uvw) const noexcept
{
    if (Resolution == 0u || Buffer.empty()) return {};
    const float FRes = static_cast<float>(Resolution);
    const float PX   = std::clamp(Uvw.X * FRes - 0.5f, 0.0f, FRes - 1.0001f);
    const float PY   = std::clamp(Uvw.Y * FRes - 0.5f, 0.0f, FRes - 1.0001f);
    const float PZ   = std::clamp(Uvw.Z * FRes - 0.5f, 0.0f, FRes - 1.0001f);

    const int32_t X0 = static_cast<int32_t>(std::floor(PX));
    const int32_t Y0 = static_cast<int32_t>(std::floor(PY));
    const int32_t Z0 = static_cast<int32_t>(std::floor(PZ));
    const float   FX = PX - static_cast<float>(X0);
    const float   FY = PY - static_cast<float>(Y0);
    const float   FZ = PZ - static_cast<float>(Z0);

    const auto Fetch = [&](int32_t IX, int32_t IY, int32_t IZ) noexcept -> const FluidVoxelSample4&
    {
        return Buffer[FlatIndex(IX, IY, IZ, Resolution)];
    };
    const auto Mix4 = [](const FluidVoxelSample4& A, const FluidVoxelSample4& B, float T) noexcept -> FluidVoxelSample4
    {
        return {
            A.X + (B.X - A.X) * T,
            A.Y + (B.Y - A.Y) * T,
            A.Z + (B.Z - A.Z) * T,
            A.W + (B.W - A.W) * T,
        };
    };

    const auto C00 = Mix4(Fetch(X0, Y0, Z0),     Fetch(X0 + 1, Y0, Z0),     FX);
    const auto C10 = Mix4(Fetch(X0, Y0 + 1, Z0), Fetch(X0 + 1, Y0 + 1, Z0), FX);
    const auto C01 = Mix4(Fetch(X0, Y0, Z0 + 1), Fetch(X0 + 1, Y0, Z0 + 1), FX);
    const auto C11 = Mix4(Fetch(X0, Y0 + 1, Z0 + 1), Fetch(X0 + 1, Y0 + 1, Z0 + 1), FX);
    return Mix4(Mix4(C00, C10, FY), Mix4(C01, C11, FY), FZ);
}

FluidVoxelSample4 FluidGridVolume::SampleWorldTrilinear(
    const std::vector<FluidVoxelSample4>& Buffer,
    const Mathematics::Axis3D&            WorldPoint) const noexcept
{
    const float DX = std::max(1.0e-4f, BoxMaxWorld.X - BoxMinWorld.X);
    const float DY = std::max(1.0e-4f, BoxMaxWorld.Y - BoxMinWorld.Y);
    const float DZ = std::max(1.0e-4f, BoxMaxWorld.Z - BoxMinWorld.Z);
    const Mathematics::Axis3D Uvw {
        (WorldPoint.X - BoxMinWorld.X) / DX,
        (WorldPoint.Y - BoxMinWorld.Y) / DY,
        (WorldPoint.Z - BoxMinWorld.Z) / DZ,
    };
    if (Uvw.X < 0.0f || Uvw.X > 1.0f || Uvw.Y < 0.0f || Uvw.Y > 1.0f || Uvw.Z < 0.0f || Uvw.Z > 1.0f)
    {
        return {};
    }
    return SampleTrilinear(Buffer, Uvw);
}

void FluidWorldDriftVolume::Allocate(uint32_t NewResolution, float NewSpanMeters, const Mathematics::Axis3D& Center)
{
    Resolution  = std::clamp(NewResolution, 8u, 96u);
    SpanMeters  = std::max(4.0f, NewSpanMeters);
    CenterWorld = Center;
    ToroidalShift = { 0, 0, 0 };
    const size_t Count = static_cast<size_t>(Resolution) * Resolution * Resolution;
    VelocitySolid.assign(Count, {});
    Thermo.assign(Count, {});
}

void FluidWorldDriftVolume::ClearState() noexcept
{
    ToroidalShift = { 0, 0, 0 };
    std::fill(VelocitySolid.begin(), VelocitySolid.end(), FluidVoxelSample4{});
    std::fill(Thermo.begin(), Thermo.end(), FluidVoxelSample4{});
}

uint32_t FluidWorldDriftVolume::FlatIndexWrapped(int32_t IX, int32_t IY, int32_t IZ) const noexcept
{
    const int32_t R   = static_cast<int32_t>(Resolution);
    const auto    Mod = [R](int32_t V) noexcept -> int32_t
    {
        const int32_t M = V % R;
        return (M < 0) ? (M + R) : M;
    };
    const int32_t WX = Mod(IX + ToroidalShift[0]);
    const int32_t WY = Mod(IY + ToroidalShift[1]);
    const int32_t WZ = Mod(IZ + ToroidalShift[2]);
    return static_cast<uint32_t>(WX + (WY + WZ * R) * R);
}

FluidVoxelSample4 FluidWorldDriftVolume::SampleWorldTrilinear(
    const std::vector<FluidVoxelSample4>& Buffer,
    const Mathematics::Axis3D&            WorldPoint) const noexcept
{
    if (Resolution == 0u || Buffer.empty()) return {};
    const float Half = 0.5f * SpanMeters;
    const float UX   = (WorldPoint.X - (CenterWorld.X - Half)) / SpanMeters;
    const float UY   = (WorldPoint.Y - (CenterWorld.Y - Half)) / SpanMeters;
    const float UZ   = (WorldPoint.Z - (CenterWorld.Z - Half)) / SpanMeters;
    if (UX < 0.0f || UX > 1.0f || UY < 0.0f || UY > 1.0f || UZ < 0.0f || UZ > 1.0f)
    {
        return {};
    }
    const float FRes = static_cast<float>(Resolution);
    const float PX   = std::clamp(UX * FRes - 0.5f, 0.0f, FRes - 1.0001f);
    const float PY   = std::clamp(UY * FRes - 0.5f, 0.0f, FRes - 1.0001f);
    const float PZ   = std::clamp(UZ * FRes - 0.5f, 0.0f, FRes - 1.0001f);

    const int32_t X0 = static_cast<int32_t>(std::floor(PX));
    const int32_t Y0 = static_cast<int32_t>(std::floor(PY));
    const int32_t Z0 = static_cast<int32_t>(std::floor(PZ));
    const int32_t MaxI = static_cast<int32_t>(Resolution) - 1;
    const int32_t X1 = std::min(MaxI, X0 + 1);
    const int32_t Y1 = std::min(MaxI, Y0 + 1);
    const int32_t Z1 = std::min(MaxI, Z0 + 1);
    const float   FX = PX - static_cast<float>(X0);
    const float   FY = PY - static_cast<float>(Y0);
    const float   FZ = PZ - static_cast<float>(Z0);

    const auto Fetch = [&](int32_t IX, int32_t IY, int32_t IZ) noexcept -> const FluidVoxelSample4&
    {
        return Buffer[FlatIndexWrapped(IX, IY, IZ)];
    };
    const auto Mix4 = [](const FluidVoxelSample4& A, const FluidVoxelSample4& B, float T) noexcept -> FluidVoxelSample4
    {
        return {
            A.X + (B.X - A.X) * T,
            A.Y + (B.Y - A.Y) * T,
            A.Z + (B.Z - A.Z) * T,
            A.W + (B.W - A.W) * T,
        };
    };

    const auto C00 = Mix4(Fetch(X0, Y0, Z0), Fetch(X1, Y0, Z0), FX);
    const auto C10 = Mix4(Fetch(X0, Y1, Z0), Fetch(X1, Y1, Z0), FX);
    const auto C01 = Mix4(Fetch(X0, Y0, Z1), Fetch(X1, Y0, Z1), FX);
    const auto C11 = Mix4(Fetch(X0, Y1, Z1), Fetch(X1, Y1, Z1), FX);
    return Mix4(Mix4(C00, C10, FY), Mix4(C01, C11, FY), FZ);
}

} // namespace Frontier::SimulationSpace::FluidSpace
