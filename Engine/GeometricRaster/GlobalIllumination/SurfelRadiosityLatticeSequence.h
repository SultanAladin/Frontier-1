/*============================================================================================================================================
                                                     SURFELRADIOSITYLATTICESEQUENCE.H
============================================================================================================================================*/
// 🧩 Order 2 GI Candidate 2: 2-bounce Bunnell finite-disk radiosity + Dual-Cascade C1-Hermite Octahedral Irradiance Lattice

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometricRaster/DirectLighting/HybridConeShadowSequence.h"
#include "Engine/Mathematics/AxisCoordinates.h"
#include "Engine/Mathematics/LinearColors.h"
#include "Engine/SpatialAcceleration/CompressedWideTree.h"

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace Frontier::GeometricRaster::GlobalIllumination
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct RadiositySurfelDisk
{
    Mathematics::Axis3D    Position        {};
    Mathematics::Axis3D    Normal          { 0.0f, 0.0f, 1.0f };
    Mathematics::LinearRGB AlbedoRgb       {};
    Mathematics::LinearRGB EmissiveRgb     {};
    float                  AreaMeters      = 0.1f;
    float                  RadiusMeters    = 0.18f;
    bool                   EmissiveSurface = false;
    bool                   TwoSidedSurface = false;
    Mathematics::LinearRGB ExitantRadiance {}; // Emissive + 2-bounce relaxed diffuse exitant radiance [W/(m^2*sr)]
};

struct OctahedralIrradianceCascade
{
    uint32_t                          GridDimX     = 0u;
    uint32_t                          GridDimY     = 0u;
    uint32_t                          GridDimZ     = 0u;
    uint32_t                          OctaSheetRes = 8u; // 8x8 on GtxLowEnd, 16x16 on HighEnd
    Mathematics::Axis3D               BoundsMin    {};
    Mathematics::Axis3D               BoundsMax    {};
    Mathematics::Axis3D               CellSpacing  {};
    std::vector<Mathematics::LinearRGB> IrradianceSheets {}; // [EntryCount * OctaSheetRes * OctaSheetRes]
    std::vector<float>                ChebyshevMoments   {}; // [EntryCount * OctaSheetRes * OctaSheetRes * 2] (MeanD, MeanD^2)
};

struct SurfelLatticeTierSettings
{
    bool     HighEndZeroSdfTier = false; // false = GtxLowEnd (12x10x4 + 8x6x3, 8x8 sheet); true = HighEnd (19x15x6 + 9x7x4, 16x16 sheet, 96 BVH4 rays)
    uint32_t FibonacciMomentRays = 96u;  // [cnt] - HighEnd BVH4 Chebyshev depth moment rays per lattice node
    Mathematics::Axis3D SunDirection    { 0.45f, -0.35f, 0.82f };
    Mathematics::LinearRGB SunRadiance  { 0.0f, 0.0f, 0.0f };
};

//------------------------------------------------------------------------------------------------------------------------
//                                            SURFEL RADIOSITY LATTICE SEQUENCE
//------------------------------------------------------------------------------------------------------------------------

// Singularity-free Bunnell (GPU Gems 2, Ch. 14) disk-to-point Lambertian form factor in [0, 1]
[[nodiscard]] float EvaluateBunnellDiskFormFactor(
    Mathematics::Axis3D ReceiverPos,
    Mathematics::Axis3D ReceiverNormal,
    Mathematics::Axis3D DiskPos,
    Mathematics::Axis3D DiskNormal,
    float               DiskArea,
    bool                TwoSidedDisk = false) noexcept;

class SurfelRadiosityLatticeSequence
{
public:
    std::vector<RadiositySurfelDisk> Surfels      {};
    OctahedralIrradianceCascade      InnerCascade {}; // GtxLowEnd: 12x10x4; HighEnd: 19x15x6
    OctahedralIrradianceCascade      OuterCascade {}; // GtxLowEnd:  8x6x3;  HighEnd:  9x7x4

    void Reset() noexcept;

    void BuildRelaxedLatticeFromScene(
        std::span<const SpatialAcceleration::RayTrianglePrimitive> Primitives,
        std::span<const Mathematics::LinearRGB>                    MaterialAlbedoBySlot,
        std::span<const Mathematics::LinearRGB>                    MaterialEmissiveBySlot,
        const DirectLighting::HybridOccluderEnvironment&           Occluders,
        const SurfelLatticeTierSettings&                           Settings);

    // Evaluates near-field emissive Bunnell form factor + C1-Hermite Dual-Cascade Octahedral Lattice indirect irradiance
    [[nodiscard]] Mathematics::LinearRGB EvaluateTotalIrradiance(
        Mathematics::Axis3D                              ReceiverPos,
        Mathematics::Axis3D                              ReceiverNormal,
        const DirectLighting::HybridOccluderEnvironment& Occluders,
        const SurfelLatticeTierSettings&                 Settings,
        Mathematics::LinearRGB*                          OptionalOutNearEmissiveE = nullptr,
        Mathematics::LinearRGB*                          OptionalOutCascadeGiE    = nullptr) const noexcept;
};

} // namespace Frontier::GeometricRaster::GlobalIllumination
