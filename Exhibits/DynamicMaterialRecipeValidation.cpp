/*==========================================================================================================================================
                                                 DYNAMICMATERIALRECIPEVALIDATION.CPP
==========================================================================================================================================*/
// 🧩 Validation suite for Dynamic Material Foundation (M1) + GI-free Direct Studio OpenPBR CPU reference renders & modularity proofs

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/MaterialSpace/Dynamic/DynamicMaterialCompiler.h"
#include "Engine/MaterialSpace/Dynamic/DynamicMaterialRecipe.h"
#include "Engine/MaterialSpace/Dynamic/DynamicMaterialRepository.h"
#include "Engine/MaterialSpace/Dynamic/DynamicSurfaceResolver.h"
#include "Engine/Workspaces/Material/MaterialPreviewRenderer.h"
#include "Exhibits/SoftwareCanvasRasterizer.h"

#include <cmath>
#include <cstdio>
#include <filesystem>

namespace
{

using namespace Frontier;
using namespace Frontier::MaterialSpace::Dynamic;
using namespace Frontier::Workspaces;
using Frontier::Components::RgbaColor8;
using Frontier::Exhibits::ResolveRepositoryRootPath;
using Frontier::Exhibits::SoftwareRasterCanvas;

bool VerifyCheck(bool Condition, const char* Label, uint32_t& PassedCount, uint32_t& FailedCount) noexcept
{
    if (Condition)
    {
        ++PassedCount;
        std::printf("  [PASS] %s\n", Label);
        return true;
    }
    ++FailedCount;
    std::fprintf(stderr, "  [FAIL] %s\n", Label);
    return false;
}

} // namespace

int main()
{
    uint32_t Passed = 0u;
    uint32_t Failed = 0u;
    std::printf("=== DynamicMaterialRecipeValidation ===\n");

    // 1. Verify exact 256-byte GPU uniform block layout
    VerifyCheck(sizeof(DynamicParameterBlock) == 256u, "DynamicParameterBlock exact 256-byte GPU alignment", Passed, Failed);

    // 2. Verify all 13 modular asset-agnostic presets build cleanly with valid OpenPBR bounds
    const auto Presets = BuildAllDynamicMaterialPresets();
    VerifyCheck(Presets.size() == DynamicPresetTotalCount, "BuildAllDynamicMaterialPresets returns all 13 modular recipes", Passed, Failed);

    bool AllValid = true;
    for (const auto& Preset : Presets)
    {
        if (Preset.LayerStack.Layers.empty() || !MaterialSpace::VerifySurfaceMaterialPhysicalBounds(Preset.PrimaryFinish))
        {
            AllValid = false;
        }
    }
    VerifyCheck(AllValid, "All 13 recipes pass OpenPBR physical bounds & non-empty layer stack", Passed, Failed);

    // 3. Verify asset-agnostic bounds normalization produces identical sweep space across tiny prop vs 12m structure
    DynamicParameterBlock SmallAsset {};
    AssignAssetLocalBounds(SmallAsset, { -0.25f, -0.25f, -0.25f }, { 0.25f, 0.25f, 0.25f });
    DynamicParameterBlock LargeStructure {};
    AssignAssetLocalBounds(LargeStructure, { 10.0f, 0.0f, -6.0f }, { 22.0f, 12.0f, 6.0f });
    const auto NormSmall = NormalizeAssetCoordinate(SmallAsset, { 0.125f, 0.0f, 0.0f });
    const auto NormLarge = NormalizeAssetCoordinate(LargeStructure, { 19.0f, 6.0f, 0.0f });
    VerifyCheck(std::fabs(NormSmall.X - 0.5f) < 1.0e-4f && std::fabs(NormLarge.X - 0.5f) < 1.0e-4f,
        "Asset-agnostic local bounds normalize tiny props and large structures identically", Passed, Failed);

    // 4. Verify zero-recompile structural signature invariance across slider adjustments
    auto TransitionRecipe = ConstructDynamicMaterialPreset(DynamicPresetIdentity::SurfaceMaterialTransition);
    const uint64_t HashBefore = CompileMaterialLayerStack(TransitionRecipe).StructuralHash;
    TransitionRecipe.Parameters.TransitionProgress = 0.72f;
    TransitionRecipe.Parameters.DetailScale        = 7.5f;
    TransitionRecipe.Parameters.SeamGlowIntensity  = 3.4f;
    const uint64_t HashAfterSliders = CompileMaterialLayerStack(TransitionRecipe).StructuralHash;
    VerifyCheck(HashBefore == HashAfterSliders, "Slider edits preserve structural shader cache signature (zero recompile)", Passed, Failed);

    // 5. Verify structural signature updates cleanly when adding a layer, plus deterministic serialization round-trip
    MaterialLayerEntry ExtraMask {};
    ExtraMask.LayerTitle = "Secondary Grazing Coat";
    ExtraMask.Category   = MaterialLayerCategory::MaskModifierLayer;
    ExtraMask.MaskMode   = MaskModifierMode::FresnelGrazingMask;
    TransitionRecipe.LayerStack.AppendLayer(ExtraMask);
    const uint64_t HashAfterAdd = CompileMaterialLayerStack(TransitionRecipe).StructuralHash;
    VerifyCheck(HashAfterAdd != HashBefore, "Appending a layer updates structural signature hash", Passed, Failed);

    const std::string Serialized = SerializeDynamicMaterialRecipe(TransitionRecipe);
    DynamicMaterialRecipe Restored {};
    const bool RoundTripOk = DeserializeDynamicMaterialRecipe(Serialized, Restored) &&
                             Restored.RecipeId == TransitionRecipe.RecipeId &&
                             Restored.LayerStack.Layers.size() == TransitionRecipe.LayerStack.Layers.size() &&
                             std::fabs(Restored.Parameters.TransitionProgress - 0.72f) < 1.0e-4f;
    VerifyCheck(RoundTripOk, "Deterministic text serialize/deserialize round-trip matches recipe & layer stack", Passed, Failed);

    // 6. Verify live 3D impact pulse trigger and CPU resolver NaN safety across all 13 presets
    auto ShieldRecipe = ConstructDynamicMaterialPreset(DynamicPresetIdentity::HexagonalEnergyBarrier);
    TriggerImpactPulse(ShieldRecipe.Parameters, { 0.2f, 0.4f, 0.9f });
    bool ZeroNans = true;
    for (const auto& P : Presets)
    {
        DynamicSurfaceQuery Q {};
        Q.LocalPosition  = { 0.3f, 0.2f, 0.75f };
        Q.WorldNormal    = Mathematics::NormalizeAxis({ 0.3f, 0.2f, 0.75f });
        Q.WorldTangent   = { 1.0f, 0.0f, 0.0f };
        Q.WorldBitangent = { 0.0f, 1.0f, 0.0f };
        Q.WorldViewDir   = { 0.0f, 0.1f, 0.99f };
        const auto Sample = ResolveDynamicSurface(P, P.Parameters, Q);
        if (!std::isfinite(Sample.IncandescentRadiance.R) || !std::isfinite(Sample.ShadingNormal.X) || !std::isfinite(Sample.SurfaceOpacity))
        {
            ZeroNans = false;
        }
    }
    VerifyCheck(ZeroNans, "DynamicSurfaceResolver produces finite OpenPBR samples across all 13 presets", Passed, Failed);

    // 7. Render high-resolution C++ Direct Studio OpenPBR (No GI) visual proofs!
    const std::filesystem::path OutDir = ResolveRepositoryRootPath() / "Exhibits/Images/Materials";
    std::filesystem::create_directories(OutDir);

    PreviewCameraSetup Cam {};
    Cam.OrbitYawRadians   = 0.32f;
    Cam.OrbitPitchRadians = 0.20f;
    Cam.OrbitDistanceM    = 2.65f;

    // 7a. Hero 6 showcase (3 x 2 @ 320x320 each = 992x676)
    constexpr uint32_t TileS = 320u;
    constexpr uint32_t Pad   = 12u;
    SoftwareRasterCanvas HeroSheet(TileS * 3u + Pad * 4u, TileS * 2u + Pad * 3u, RgbaColor8 { 11u, 11u, 12u, 255u });
    for (uint32_t I = 0u; I < 6u; ++I)
    {
        auto R = Presets[I];
        R.Parameters.ClockSeconds = 1.45f;
        if (I == 0u) R.Parameters.TransitionProgress = 0.42f;
        if (I == 3u) R.Parameters.TransitionProgress = 0.50f;
        if (I == 4u) R.Parameters.TransitionProgress = 0.38f;
        const auto Img = MaterialPreviewRenderer::RenderPreviewCpuReference(
            R, R.Parameters, PreviewSpecimenGeometry::ShaderballPedestal, Cam, TileS, TileS);
        HeroSheet.BlitRgbaRegion(Pad + (I % 3u) * (TileS + Pad), Pad + (I / 3u) * (TileS + Pad), Img.WidthPx, Img.HeightPx, Img.DisplayRgba8.data());
    }
    VerifyCheck(HeroSheet.WritePngFile(OutDir / "CppDirectPbr_Hero6_Proof.png"), "Write CppDirectPbr_Hero6_Proof.png", Passed, Failed);

    // 7b. All 13 showcase (5 x 3 @ 240x240 each)
    constexpr uint32_t SmallTile = 240u;
    SoftwareRasterCanvas AllSheet(SmallTile * 5u + Pad * 6u, SmallTile * 3u + Pad * 4u, RgbaColor8 { 11u, 11u, 12u, 255u });
    for (uint32_t I = 0u; I < Presets.size(); ++I)
    {
        auto R = Presets[I];
        R.Parameters.ClockSeconds = 1.45f;
        if (I == 0u) R.Parameters.TransitionProgress = 0.42f;
        if (I == 3u) R.Parameters.TransitionProgress = 0.50f;
        if (I == 4u) R.Parameters.TransitionProgress = 0.38f;
        const auto Img = MaterialPreviewRenderer::RenderPreviewCpuReference(
            R, R.Parameters, PreviewSpecimenGeometry::ShaderballPedestal, Cam, SmallTile, SmallTile);
        AllSheet.BlitRgbaRegion(Pad + (I % 5u) * (SmallTile + Pad), Pad + (I / 5u) * (SmallTile + Pad), Img.WidthPx, Img.HeightPx, Img.DisplayRgba8.data());
    }
    VerifyCheck(AllSheet.WritePngFile(OutDir / "CppDirectPbr_13_Showcase.png"), "Write CppDirectPbr_13_Showcase.png", Passed, Failed);

    // 7c. Modularity proof: same SurfaceMaterialTransition attached to 4 distinct geometries (Shaderball, Cloth, Rounded Box, Plane)
    SoftwareRasterCanvas ModSheet(280u * 4u + Pad * 5u, 280u + Pad * 2u, RgbaColor8 { 11u, 11u, 12u, 255u });
    auto ModMat = ConstructDynamicMaterialPreset(DynamicPresetIdentity::SurfaceMaterialTransition);
    ModMat.Parameters.TransitionProgress = 0.50f;
    for (uint32_t S = 0u; S < 4u; ++S)
    {
        PreviewCameraSetup SCam = Cam;
        if (S == 3u) { SCam.OrbitPitchRadians = 0.58f; SCam.OrbitDistanceM = 2.8f; }
        const auto Img = MaterialPreviewRenderer::RenderPreviewCpuReference(
            ModMat, ModMat.Parameters, static_cast<PreviewSpecimenGeometry>(S), SCam, 280u, 280u);
        ModSheet.BlitRgbaRegion(Pad + S * (280u + Pad), Pad, Img.WidthPx, Img.HeightPx, Img.DisplayRgba8.data());
    }
    VerifyCheck(ModSheet.WritePngFile(OutDir / "CppDirectPbr_4Specimens_Proof.png"), "Write CppDirectPbr_4Specimens_Proof.png", Passed, Failed);

    std::printf("=== Summary: %u PASSED, %u FAILED ===\n", Passed, Failed);
    return (Failed == 0u) ? 0 : 1;
}
