/*============================================================================================================================================
                                                        TIEREDWORLDRENDERSEQUENCE.CPP
============================================================================================================================================*/
// 🧩 Complete Ordered GTX Tier 1 + RTX Tier 2 World Render Sequence bound to ViewportProjectionPane (Visibility -> Direct -> GI -> Specular)

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometricRaster/GlobalIllumination/TieredWorldRenderSequence.h"

#include <algorithm>
#include <cmath>

namespace Frontier::GeometricRaster
{

namespace
{

[[nodiscard]] float EncodeLinearToSrgb(float LinearVal) noexcept
{
    const float C = std::clamp(LinearVal, 0.0f, 1.0f);
    return (C <= 0.0031308f) ? (12.92f * C) : (1.055f * std::pow(C, 1.0f / 2.4f) - 0.055f);
}

[[nodiscard]] Mathematics::LinearRGB ApplyAcesFilmic(Mathematics::LinearRGB Hdr) noexcept
{
    const auto Curve = [](float X) noexcept -> float
    {
        const float V = std::max(0.0f, X * 0.85f);
        return std::clamp((V * (2.51f * V + 0.03f)) / (V * (2.43f * V + 0.59f) + 0.14f), 0.0f, 1.0f);
    };
    return { Curve(Hdr.R), Curve(Hdr.G), Curve(Hdr.B) };
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                            TIERED WORLD RENDER SEQUENCE
//------------------------------------------------------------------------------------------------------------------------

void TieredWorldRenderSequence::PrepareWorldScene(WorldSceneSpecification& InOutScene)
{
    // 1. Compile 32 B BounceSlabEntries for every registered material slot!
    const uint32_t SlotCount = InOutScene.MaterialRegister.QuerySlotCount();
    InOutScene.BounceSummaries.resize(SlotCount);
    for (uint32_t Slot = 0u; Slot < SlotCount; ++Slot)
    {
        InOutScene.BounceSummaries[Slot] = MaterialSpace::CompileBounceSlabEntry(
            InOutScene.MaterialRegister.InspectAuthoredMaterial(static_cast<uint16_t>(Slot)));
    }

    // 2. Build Tier 1 GTX 2-Cascade 3.9 mm Orthographic Contact Shadow Clips
    const Mathematics::Axis3D FocusCenter = (InOutScene.SceneTree.RootBoundsMin + InOutScene.SceneTree.RootBoundsMax) * 0.5f;
    SolarShadowSequence::BuildTwoCascadeContactClips(
        InOutScene.Sun, FocusCenter, InOutScene.SceneTree, FineContactClip, CoarseContactClip);

    // 3. Build & Propagate Multi-Bounce Tier 2 RTX 1/pi Octahedral Irradiance Lattice (with 16x16 Chebyshev Depth!)
    RtxLattice.PropagateRtxIrradianceLattice(
        InOutScene.LatticeConfig,
        InOutScene.SceneTree,
        InOutScene.BounceSummaries,
        InOutScene.Sun,
        InOutScene.SkyRadiance);
}

WorldRenderFrameResult TieredWorldRenderSequence::RenderWorldFrame(
    WorldRenderTier                Tier,
    const PerspectiveCameraFrame&  CurrentCamera,
    const PerspectiveCameraFrame&  PreviousCamera,
    const WorldSceneSpecification& PreparedScene)
{
    WorldRenderFrameResult Result {};
    Result.WidthPx    = std::max(1u, CurrentCamera.ViewportWidthPx);
    Result.HeightPx   = std::max(1u, CurrentCamera.ViewportHeightPx);
    Result.ActiveTier = Tier;
    const std::size_t TotalPixels = static_cast<std::size_t>(Result.WidthPx) * Result.HeightPx;

    Result.CompositeLinearHdr.assign(TotalPixels, { 0.0f, 0.0f, 0.0f });
    Result.IndirectDiffuseHdr.assign(TotalPixels, { 0.0f, 0.0f, 0.0f });
    Result.DisplayRgba8.assign(TotalPixels * 4u, 255u);

    //====================================================================================================================
    // PASS 1: SHARED 64-BIT (8 B/PX) PRIMARY VISIBILITY, TWO-PHASE HI-Z CULL & 2-LAYER GLASS PEEL
    //====================================================================================================================
    const DepthPyramidSequence* PrevHiZPtr =
        (PreviousHiZ.QueryMipCount() > 0u &&
         PreviousHiZ.InspectMipLevel(0u).Width == Result.WidthPx &&
         PreviousHiZ.InspectMipLevel(0u).Height == Result.HeightPx)
            ? &PreviousHiZ
            : nullptr;

    Result.VisibilityCull = ClusterCullEncoder::EncodePhaseOneVisibility(
        CurrentCamera, PreparedScene.ClusterInstances, PrevHiZPtr, &PreviousCamera);

    VisibilityBufferTargets VisTargets {};
    VisTargets.AllocateClearedImage(Result.WidthPx, Result.HeightPx);
    VisibilityClusterRasterization::RasterizeOpaqueClusters(
        CurrentCamera, PreparedScene.ClusterInstances,
        Result.VisibilityCull.HardwareClusterBatches,
        Result.VisibilityCull.MicroSoftwareClusters,
        VisTargets);

    DepthPyramidSequence CurrentHiZ {};
    CurrentHiZ.BuildFromVisibilityBuffer64(Result.WidthPx, Result.HeightPx, VisTargets.OpaquePrimary64);

    if (!Result.VisibilityCull.PhaseOneDeferredQueue.empty())
    {
        ClusterCullEncoder::ResolvePhaseTwoDisocclusions(
            CurrentCamera, PreparedScene.ClusterInstances, CurrentHiZ, Result.VisibilityCull);
        VisTargets.AllocateClearedImage(Result.WidthPx, Result.HeightPx);
        VisibilityClusterRasterization::RasterizeOpaqueClusters(
            CurrentCamera, PreparedScene.ClusterInstances,
            Result.VisibilityCull.HardwareClusterBatches,
            Result.VisibilityCull.MicroSoftwareClusters,
            VisTargets);
        CurrentHiZ.BuildFromVisibilityBuffer64(Result.WidthPx, Result.HeightPx, VisTargets.OpaquePrimary64);
    }

    VisibilityClusterRasterization::RasterizeTwoLayerGlassPeel(
        CurrentCamera, PreparedScene.ClusterInstances, Result.VisibilityCull.TransparentGlassBatches, VisTargets);

    //====================================================================================================================
    // PASS 2: ANALYTIC PRIMARY HIT RECONSTRUCTION & CALIBRATED DIRECT LIGHTING (SOLAR + LTC AREA LIGHTS + CHASSIS AO)
    //====================================================================================================================
    const OrthonormalCameraBasis CurBasis  = ComputeCameraBasis(CurrentCamera);
    const OrthonormalCameraBasis PrevBasis = ComputeCameraBasis(PreviousCamera);

    std::vector<AnalyticResolvedSurfaceSample> PrimarySamples(TotalPixels);
    for (uint32_t PY = 0u; PY < Result.HeightPx; ++PY)
    {
        for (uint32_t PX = 0u; PX < Result.WidthPx; ++PX)
        {
            const std::size_t Off = static_cast<std::size_t>(PY) * Result.WidthPx + PX;
            PrimarySamples[Off] = VisibilityMaterialResolve::ReconstructAnalyticPixelHit(
                PX, PY, VisTargets.OpaquePrimary64[Off], CurBasis, PrevBasis, PreparedScene.ClusterInstances);
        }
    }

    const DirectShadowTier ShadowTier = (Tier == WorldRenderTier::Tier2RtxDeterministic)
        ? DirectShadowTier::RtxDeterministicSolarRay
        : DirectShadowTier::GtxContactClipSdfCone;

    Result.DirectLighting = LinearCosineLightIntegration::EvaluateDirectLightingFrame(
        ShadowTier,
        CurrentCamera,
        PreparedScene.ClusterInstances,
        VisTargets,
        PreparedScene.MaterialRegister,
        PreparedScene.SceneTree,
        PreparedScene.Sun,
        PreparedScene.AreaLights,
        &PreparedScene.SceneSdf,
        &FineContactClip,
        &CoarseContactClip);

    //====================================================================================================================
    // PASS 3: GLOBAL ILLUMINATION (RTX 1/PI OCTAHEDRAL CHEBYSHEV LATTICE vs GTX 6-CONE SDF GI) + SPECULAR & GLASS
    //====================================================================================================================
    if (Tier == WorldRenderTier::Tier2RtxDeterministic)
    {
        Result.SpecularGlass = SpecularReflectionSequence::EvaluateRtxSpecularRefraction(
            CurrentCamera, PreparedScene.ClusterInstances, VisTargets, PrimarySamples,
            PreparedScene.MaterialRegister, PreparedScene.BounceSummaries,
            PreparedScene.SceneTree, RtxLattice, PreparedScene.Sun, PreparedScene.SkyRadiance);
    }
    else
    {
        Result.SpecularGlass = SpecularReflectionSequence::EvaluateGtxHiZSpecularRefraction(
            CurrentCamera, PreparedScene.ClusterInstances, VisTargets, CurrentHiZ, PrimarySamples,
            Result.DirectLighting.DirectHdrRadiance, PreparedScene.MaterialRegister, PreparedScene.SkyRadiance);
    }

    const uint32_t MatCount = PreparedScene.MaterialRegister.QuerySlotCount();

    for (std::size_t Off = 0u; Off < TotalPixels; ++Off)
    {
        const auto& Hit = PrimarySamples[Off];
        Mathematics::LinearRGB FinalHdr = Result.DirectLighting.DirectHdrRadiance[Off];

        if (Hit.ValidHit && Hit.MaterialSlot < MatCount)
        {
            const auto& Mat = PreparedScene.MaterialRegister.InspectAuthoredMaterial(Hit.MaterialSlot);

            // Evaluate 1/pi-normalized diffuse indirect radiance (multi-bounce color bleeding!)
            Mathematics::LinearRGB IndirectOverPi {};
            if (Tier == WorldRenderTier::Tier2RtxDeterministic)
            {
                IndirectOverPi = RtxLattice.SampleChebyshevIrradianceOverPi(
                    Hit.WorldPosition, Hit.TangentFrame.NormalZ, Hit.WorldViewDirection);
            }
            else
            {
                IndirectOverPi = RtxLattice.EvaluateGtxSignedDistanceConeGi(
                    Hit.WorldPosition, Hit.TangentFrame.NormalZ, PreparedScene.SceneSdf,
                    PreparedScene.SceneTree, PreparedScene.BounceSummaries, PreparedScene.Sun, PreparedScene.SkyRadiance);
            }

            const float DiffAo     = Result.DirectLighting.CombinedDiffuseAo[Off];
            const float SpecAo     = Result.DirectLighting.LagardeSpecularAo[Off];
            const float DiffFactor = (1.0f - Mat.Specular.Metalness) *
                                     (1.0f - Mat.Transmission.TransmissionWeight) *
                                     Mat.Diffuse.BaseWeight;

            // L_diffuse_indirect = (E(N) / pi) * pi * (rho / pi) = (E(N) / pi) * rho! (Zero pi factor mismatch!)
            const Mathematics::LinearRGB DiffuseGi {
                IndirectOverPi.R * Mat.Diffuse.BaseColor.R * DiffFactor * (0.35f + 0.65f * DiffAo),
                IndirectOverPi.G * Mat.Diffuse.BaseColor.G * DiffFactor * (0.35f + 0.65f * DiffAo),
                IndirectOverPi.B * Mat.Diffuse.BaseColor.B * DiffFactor * (0.35f + 0.65f * DiffAo),
            };
            Result.IndirectDiffuseHdr[Off] = DiffuseGi;

            FinalHdr = FinalHdr + DiffuseGi + Result.SpecularGlass.IndirectSpecularHdr[Off] * SpecAo;
        }

        // Composite 2-Layer Abbe Crown Glass Refraction & Fresnel over pixel if peeled glass shell present!
        if (VisTargets.GlassPeelFront64[Off] != 0ull)
        {
            FinalHdr = Result.SpecularGlass.RefractedGlassHdr[Off];
        }

        Result.CompositeLinearHdr[Off] = FinalHdr;
        const auto Tone = ApplyAcesFilmic(FinalHdr);
        Result.DisplayRgba8[Off * 4u + 0u] = static_cast<uint8_t>(std::round(EncodeLinearToSrgb(Tone.R) * 255.0f));
        Result.DisplayRgba8[Off * 4u + 1u] = static_cast<uint8_t>(std::round(EncodeLinearToSrgb(Tone.G) * 255.0f));
        Result.DisplayRgba8[Off * 4u + 2u] = static_cast<uint8_t>(std::round(EncodeLinearToSrgb(Tone.B) * 255.0f));
        Result.DisplayRgba8[Off * 4u + 3u] = 255u;
    }

    PreviousHiZ = std::move(CurrentHiZ);
    return Result;
}

} // namespace Frontier::GeometricRaster
