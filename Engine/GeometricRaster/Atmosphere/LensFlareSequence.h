/*============================================================================================================================================
                                                           LENSFLARESEQUENCE.H
============================================================================================================================================*/
// 🧩 Baked multi-archetype Lens Flare optical kernel atlas (Anamorphic, Streaks, Halos), GPU bake dispatch & CPU/GPU evaluation

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometricRaster/Atmosphere/VolumetricCloudSequence.h"
#include "Engine/GeometricRaster/VulkanInterchange.h"
#include "Engine/SpatialAcceleration/WideBoundingTree.h"

#include <cstdint>
#include <filesystem>
#include <span>
#include <vector>

namespace Frontier::GeometricRaster
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct ProjectedLensFlareFrame
{
    float                  ScreenU       = 0.5f;                       // [uv]  - Projected solar source horizontal UV
    float                  ScreenV       = 0.5f;                       // [uv]  - Projected solar source vertical UV
    float                  EffectiveGain = 0.0f;                       // [x]   - Occlusion & offscreen-attenuated gain
    Mathematics::LinearRGB SourceTint    { 1.0f, 0.95f, 0.88f };       // [lin] - Transmittance-weighted solar tint
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    LENS FLARE SEQUENCE
//------------------------------------------------------------------------------------------------------------------------

class LensFlareSequence
{
public:
    static constexpr uint32_t AtlasTileWidthPx   = 512u;
    static constexpr uint32_t AtlasTileHeightPx  = 256u;
    static constexpr uint32_t AtlasStyleCount    = 3u;
    static constexpr float    AtlasHdrScale      = 2.5f;

    // Applies canonical physical defaults for any of the 3 LensFlareStyle presets
    static void ApplyLensFlarePreset(
        LensFlareConfiguration& InOutConfig,
        LensFlareStyle          Style) noexcept;

    // Bakes the 512 x 768 RGBA8 optical kernel atlas (Row 0: Anamorphic, Row 1: Streaks, Row 2: Halos) on CPU
    [[nodiscard]] static std::vector<uint8_t> BakeOpticalKernelAtlasRgba8();

    // Bakes the 512 x 768 RGBA8 optical kernel atlas on GPU via LensFlareBakeCompute.comp.spv
    [[nodiscard]] static bool BakeOpticalAtlasOnGpu(
        VulkanInterchange&           Device,
        const std::filesystem::path& CompiledShaderDirectory,
        std::vector<uint8_t>&        OutAtlasRgba8);

    // Persists & loads the baked 512 x 768 optical atlas in EngineContent/Optics/LensFlareBakedAtlas.ImageSpace
    [[nodiscard]] static bool SaveBakedAtlasToImageSpace(
        const std::filesystem::path& TargetPath,
        std::span<const uint8_t>     AtlasRgba8);

    [[nodiscard]] static bool LoadBakedAtlasFromImageSpace(
        const std::filesystem::path& SourcePath,
        std::vector<uint8_t>&        OutAtlasRgba8);

    // Returns cached 512 x 768 RGBA8 baked atlas (loads .ImageSpace or bakes & saves it automatically)
    [[nodiscard]] static const std::vector<uint8_t>& QueryBakedLensFlareAtlasRgba8();

    // Projects the Sun through CameraFrame + checks solar disc visibility & transmittance
    [[nodiscard]] static ProjectedLensFlareFrame ResolveSolarFlareProjection(
        const Mathematics::Axis3D&                  CameraOrigin,
        const Mathematics::Axis3D&                  CameraForward,
        const Mathematics::Axis3D&                  CameraRight,
        const Mathematics::Axis3D&                  CameraUp,
        float                                       TanHalfFovX,
        float                                       TanHalfFovY,
        const WorldAtmosphereConfiguration&         Atmosphere,
        const SpatialAcceleration::WideBoundingTree* OptionalSceneTree) noexcept;

    // Evaluates the baked 2D kernel tile + spectral halos + coated aperture ghosts at screen UV
    [[nodiscard]] static Mathematics::LinearRGB EvaluateBakedLensFlare(
        const LensFlareConfiguration&  Config,
        const ProjectedLensFlareFrame& Projection,
        float                          ScreenU,
        float                          ScreenV,
        float                          AspectRatio) noexcept;
};

} // namespace Frontier::GeometricRaster
