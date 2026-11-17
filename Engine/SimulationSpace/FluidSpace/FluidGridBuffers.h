/*============================================================================================================================================
                                                            FLUIDGRIDBUFFERS.H
============================================================================================================================================*/
// 🧩 Dual-tier 3D Eulerian fluid voxel buffers (Hero Pyro Grid + Sliding World Smoke Drift Grid) for CPU & Vulkan GPU compute

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Mathematics/AxisCoordinates.h"

#include <array>
#include <cstdint>
#include <vector>

namespace Frontier::SimulationSpace::FluidSpace
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct FluidVoxelSample4
{
    float X = 0.0f;
    float Y = 0.0f;
    float Z = 0.0f;
    float W = 0.0f;
};

static_assert(sizeof(FluidVoxelSample4) == 16u, "FluidVoxelSample4 must remain 16 bytes (float4) for GPU SSBO parity.");

struct FluidGridVolume
{
    uint32_t                     Resolution  = 0u;
    Mathematics::Axis3D          BoxMinWorld { -0.925f, -0.925f, -0.6f };
    Mathematics::Axis3D          BoxMaxWorld {  0.925f,  0.925f,  1.5f };

    std::vector<FluidVoxelSample4> VelocitySolid[2] {}; // xyz = Velocity (m/s), w = SolidMask (0/1)
    std::vector<FluidVoxelSample4> Thermo[2]        {}; // x = Smoke, y = Temperature, z = Fuel, w = FireIrradiance
    std::vector<FluidVoxelSample4> Curl             {}; // xyz = curl(V), w = |curl(V)|
    std::vector<FluidVoxelSample4> Divergence       {}; // x = div(V) - expansion, y = ReactionRate
    std::vector<FluidVoxelSample4> Pressure[2]      {}; // x = Pressure, y = DiffusedFireIrradiance
    std::vector<float>             SceneSolidMask   {}; // [0/1] Static+Dynamic GI SDF solid occupancy
    uint32_t                       PingIndex        = 0u;

    void Allocate(uint32_t NewResolution);
    void ClearState() noexcept;

    [[nodiscard]] uint32_t VoxelCount() const noexcept { return Resolution * Resolution * Resolution; }
    [[nodiscard]] uint32_t ReadIdx() const noexcept  { return PingIndex & 1u; }
    [[nodiscard]] uint32_t WriteIdx() const noexcept { return (PingIndex ^ 1u) & 1u; }
    void                   SwapPingPong() noexcept   { PingIndex ^= 1u; }

    [[nodiscard]] static uint32_t FlatIndex(int32_t IX, int32_t IY, int32_t IZ, uint32_t Res) noexcept;
    [[nodiscard]] FluidVoxelSample4 SampleTrilinear(
        const std::vector<FluidVoxelSample4>& Buffer,
        const Mathematics::Axis3D&            Uvw) const noexcept;
    [[nodiscard]] FluidVoxelSample4 SampleWorldTrilinear(
        const std::vector<FluidVoxelSample4>& Buffer,
        const Mathematics::Axis3D&            WorldPoint) const noexcept;
};

struct FluidWorldDriftVolume
{
    uint32_t                       Resolution    = 0u;
    Mathematics::Axis3D            CenterWorld   { 0.0f, 0.0f, 11.4f };
    float                          SpanMeters    = 24.0f;
    std::array<int32_t, 3>         ToroidalShift { 0, 0, 0 }; // Integer voxel shift for continuous sliding window
    std::vector<FluidVoxelSample4> VelocitySolid {};          // xyz = World wind+smoke velocity, w = SolidMask
    std::vector<FluidVoxelSample4> Thermo        {};          // x = Smoke, y = Temp, z = Fuel, w = Irradiance

    void Allocate(uint32_t NewResolution, float NewSpanMeters, const Mathematics::Axis3D& Center);
    void ClearState() noexcept;
    [[nodiscard]] uint32_t FlatIndexWrapped(int32_t IX, int32_t IY, int32_t IZ) const noexcept;
    [[nodiscard]] FluidVoxelSample4 SampleWorldTrilinear(
        const std::vector<FluidVoxelSample4>& Buffer,
        const Mathematics::Axis3D&            WorldPoint) const noexcept;
};

} // namespace Frontier::SimulationSpace::FluidSpace
