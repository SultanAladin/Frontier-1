/*============================================================================================================================================
                                                         TIEREDWORLDRENDERSEQUENCE.H
============================================================================================================================================*/
// 🧩 Complete Ordered GTX Tier 1 + RTX Tier 2 World Render Sequence bound to ViewportProjectionPane (Visibility -> Direct -> GI -> Specular)

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/DistanceFieldSpace/SignedDistanceVolume.h"
#include "Engine/GeometricRaster/DirectLighting/LinearCosineLightIntegration.h"
#include "Engine/GeometricRaster/GlobalIllumination/IrradianceLatticeSequence.h"
#include "Engine/GeometricRaster/GlobalIllumination/SpecularReflectionSequence.h"
#include "Engine/MaterialSpace/CompactMaterialRegister.h"
#include "Engine/MaterialSpace/MaterialSurfaceLayer/BounceLayerSummary.h"
#include "Engine/SpatialAcceleration/ClusterPyramid.h"
#include "Engine/SpatialAcceleration/WideBoundingTree.h"

#include <cstdint>
#include <span>
#include <vector>

namespace Frontier::GeometricRaster
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      ENUMERATIONS
//------------------------------------------------------------------------------------------------------------------------

enum class WorldRenderTier : uint8_t
{
    Tier1GtxCalibrated    = 0u, // [-] - Shared 64-bit Vis + 3.9 mm Contact Clip/SDF Sun + 6-Cone SDF GI + Hi-Z SSR & Glass Peel
    Tier2RtxDeterministic = 1u, // [-] - Shared 64-bit Vis + Vogel Solar BVH4 + 1/pi Octahedral Chebyshev GI + VNDF RT Specular/Glass
};

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct WorldSceneSpecification
{
    std::vector<RigidClusterInstance>            ClusterInstances {};
    MaterialSpace::CompactMaterialRegister       MaterialRegister {};
    std::vector<MaterialSpace::BounceSlabEntry>  BounceSummaries  {};
    SpatialAcceleration::WideBoundingTree        SceneTree        {};
    DistanceFieldSpace::SignedDistanceVolume     SceneSdf         {};
    SolarDirectionalEmitter                      Sun              {};
    std::vector<PolygonalAreaQuadLight>          AreaLights       {};
    IrradianceLatticeConfiguration               LatticeConfig    {};
    Mathematics::LinearRGB                       SkyRadiance      { 0.15f, 0.21f, 0.32f };
};

struct WorldRenderFrameResult
{
    uint32_t                            WidthPx             = 0u;   // [px]    - Viewport width
    uint32_t                            HeightPx            = 0u;   // [px]    - Viewport height
    WorldRenderTier                     ActiveTier          = WorldRenderTier::Tier2RtxDeterministic;
    std::vector<Mathematics::LinearRGB> CompositeLinearHdr  {};     // [cd/m2] - Final Direct + Diffuse GI + Specular + Glass HDR
    std::vector<Mathematics::LinearRGB> IndirectDiffuseHdr  {};     // [cd/m2] - Isolated diffuse global illumination buffer
    std::vector<uint8_t>                DisplayRgba8        {};     // [rgba8] - ACES filmic + sRGB8 viewport output (ready for ImGui/Pane!)
    TwoPhaseCullReport                  VisibilityCull      {};     // [-]     - Cluster culling telemetry
    DirectLightingFrameOutput           DirectLighting      {};     // [-]     - Solar shadow, LTC, & chassis AO telemetry
    SpecularReflectionBuffer            SpecularGlass    {};     // [-]     - Reflection & glass refraction telemetry
};

//------------------------------------------------------------------------------------------------------------------------
//                                            TIERED WORLD RENDER SEQUENCE
//------------------------------------------------------------------------------------------------------------------------

class TieredWorldRenderSequence
{
public:
    // Compiles compact 32 B BounceSlabEntries for every registered OpenPBR material slot
    static void CompileBounceSummaries(WorldSceneSpecification& InOutScene);

    // Precomputes static/cached scene structures (BounceSlabEntries, 2-Cascade Contact Clips, & RTX Octahedral Irradiance Lattice)
    void PrepareWorldScene(WorldSceneSpecification& InOutScene);

    // Compiles BounceSlabEntries and propagates only the Tier 2 RTX Irradiance Lattice (skips CPU 2-cascade shadow clips for GPU path)
    void PrepareRtxIrradianceLatticeOnly(WorldSceneSpecification& InOutScene);

    // Adopts an externally baked Tier 2 RTX Irradiance Lattice (e.g. from background propagation)
    void AssignIrradianceLattice(IrradianceLatticeSequence&& BakedLattice) noexcept { RtxLattice = std::move(BakedLattice); }

    // Executes the complete ordered world render frame for either Tier 1 GTX or Tier 2 RTX
    [[nodiscard]] WorldRenderFrameResult RenderWorldFrame(
        WorldRenderTier                Tier,
        const PerspectiveCameraFrame&  CurrentCamera,
        const PerspectiveCameraFrame&  PreviousCamera,
        const WorldSceneSpecification& PreparedScene);

    [[nodiscard]] const IrradianceLatticeSequence& InspectIrradianceLattice() const noexcept { return RtxLattice; }

private:
    OrthographicShadowCascade FineContactClip   {};
    OrthographicShadowCascade CoarseContactClip {};
    IrradianceLatticeSequence RtxLattice        {};
    DepthPyramidSequence      PreviousHiZ       {};
};

} // namespace Frontier::GeometricRaster
