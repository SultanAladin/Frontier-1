/*============================================================================================================================================
                                                      UVSPACEVALIDATION.CPP
============================================================================================================================================*/
// 🧩 Headless native UV surface, multi-matcap, EON lighting and PCF validation with deterministic visual output

#include "Engine/GeometricRaster/SurfaceShading.h"
#include "Engine/UVSpace/UVSurface.h"
#include "Exhibits/SoftwareCanvasRasterizer.h"

#include <imgui.h>

#include <array>
#include <cmath>
#include <cstdio>
#include <filesystem>

namespace
{

using Frontier::GeometricRaster::DirectionalLightDescription;
using Frontier::GeometricRaster::DirectionalShadowDescription;
using Frontier::GeometricRaster::DirectionVector;
using Frontier::GeometricRaster::EonSpecularDescription;
using Frontier::GeometricRaster::EvaluateEonSpecular;
using Frontier::GeometricRaster::EvaluateFastSurfaceLighting;
using Frontier::GeometricRaster::LinearColor;
using Frontier::GeometricRaster::MatcapLayer;
using Frontier::GeometricRaster::SampleDirectionalShadowVisibility;
using Frontier::UVSpace::ResolveUdimOrigin;
using Frontier::UVSpace::ResolveUdimTile;
using Frontier::UVSpace::SurfaceCorner;
using Frontier::UVSpace::SurfaceFace;
using Frontier::UVSpace::UVCoordinate;
using Frontier::UVSpace::UVSurface;

bool VerifyAssertion(bool Condition, const char* Description) noexcept
{
    if (!Condition)
    {
        std::fprintf(stderr, "[UVSpaceValidation] FAILED: %s\n", Description);
    }
    return Condition;
}

UVSurface ConstructValidationSurface()
{
    UVSurface Surface{};
    Surface.Identifier = "MultiMatcapQuad";
    Surface.Positions = {{-1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, {-1.0f, 1.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {1.0f, 1.0f, 0.0f}};
    Surface.Coordinates = {{0.0f, 0.0f}, {0.5f, 0.0f}, {1.0f, 0.0f}, {0.0f, 1.0f}, {0.5f, 1.0f}, {1.0f, 1.0f}};
    Surface.Faces = {
        SurfaceFace{"Left", {{0u, 0u}, {1u, 1u}, {4u, 4u}, {3u, 3u}}, 0u, 2u},
        SurfaceFace{"Right", {{1u, 1u}, {2u, 2u}, {5u, 5u}, {4u, 4u}}, 2u, 1u}};
    Surface.MaterialLayers = {
        MatcapLayer{10u, {0.7f, 0.2f, 0.1f}, 0.75f},
        MatcapLayer{11u, {0.1f, 0.4f, 0.8f}, 0.25f},
        MatcapLayer{12u, {0.8f, 0.7f, 0.2f}, 1.0f}};
    return Surface;
}

bool ValidateSurfaceFoundation()
{
    UVSurface Surface = ConstructValidationSurface();
    if (!VerifyAssertion(Surface.Validate(), "Initial corner and packed-layer ranges validate")) return false;
    if (!VerifyAssertion(Surface.Faces[0].MaterialCount == 2u && Surface.Faces[1].MaterialCount == 1u,
            "One face supports several matcaps independently")) return false;

    const std::array<MatcapLayer, 3> Replacement = {
        MatcapLayer{20u, {0.2f, 0.8f, 0.4f}, 0.2f},
        MatcapLayer{21u, {0.8f, 0.2f, 0.4f}, 0.3f},
        MatcapLayer{22u, {0.4f, 0.2f, 0.8f}, 0.5f}};
    if (!VerifyAssertion(Surface.AssignFaceLayers(0u, Replacement), "Face layer replacement succeeds")) return false;
    if (!VerifyAssertion(Surface.Faces[1].MaterialFirst == 3u && Surface.MaterialLayers[3].TextureToken == 12u,
            "Replacement preserves and reindexes the following face")) return false;
    const std::span<const MatcapLayer> AliasedLayers(Surface.MaterialLayers.data(), 2u);
    if (!VerifyAssertion(Surface.AssignFaceLayers(1u, AliasedLayers) && Surface.MaterialLayers[3].TextureToken == 20u,
            "Aliased layer spans survive packed-array replacement")) return false;

    if (!VerifyAssertion(ResolveUdimTile(UVCoordinate{1.25f, 2.75f}) == 1022u, "UDIM uses 1001 + U + 10V")) return false;
    const UVCoordinate Origin = ResolveUdimOrigin(1022u);
    if (!VerifyAssertion(Origin.U == 1.0f && Origin.V == 2.0f, "UDIM origin round-trip")) return false;
    if (!VerifyAssertion(!ResolveUdimTile(UVCoordinate{-0.01f, 0.0f}).has_value(), "Negative UDIM coordinates reject")) return false;

    if (!VerifyAssertion(Surface.ConfigureSeam({1u, 4u}, true), "Shared edge becomes a seam")) return false;
    const auto Chart = Surface.ExtractFaceChart(0u);
    if (!VerifyAssertion(Chart.size() == 1u && Chart[0] == 0u, "Seam separates face charts")) return false;

    const std::array<uint32_t, 3> SelectedCoordinates = {0u, 0u, 1u};
    if (!VerifyAssertion(Surface.TransformUVSelection(SelectedCoordinates, {0.0f, 0.0f}, {1.0f, 2.0f}, {2.0f, 2.0f}, 0.0f),
            "Selection transform succeeds with duplicate indices")) return false;
    if (!VerifyAssertion(std::abs(Surface.Coordinates[0].U - 1.0f) < 1.0e-5f &&
            std::abs(Surface.Coordinates[0].V - 2.0f) < 1.0e-5f,
            "Duplicate UV selection transforms exactly once")) return false;
    const std::array<uint32_t, 2> InvalidSelection = {0u, 9999u};
    if (!VerifyAssertion(!Surface.TransformUVSelection(InvalidSelection, {0.0f, 0.0f}, {9.0f, 9.0f}, {1.0f, 1.0f}, 0.0f) &&
            std::abs(Surface.Coordinates[0].U - 1.0f) < 1.0e-5f,
            "Invalid selection rejects atomically before changing coordinates")) return false;
    return true;
}

bool ValidateFastLightingAndShadows()
{
    const DirectionVector Normal{0.0f, 0.0f, 1.0f};
    const DirectionVector View{0.0f, 0.0f, 1.0f};
    const float AlignedSpecular = EvaluateEonSpecular(Normal, View, Normal, 48.0f);
    const float OrthogonalSpecular = EvaluateEonSpecular(Normal, View, {1.0f, 0.0f, 0.0f}, 48.0f);
    if (!VerifyAssertion(AlignedSpecular > 2.0f && OrthogonalSpecular < 1.0e-5f,
            "Normalized EON lobe peaks at the halfway direction")) return false;

    const std::array<float, 9> DepthTexels = {0.5f, 0.5f, 0.5f, 0.5f, 0.9f, 0.5f, 0.5f, 0.5f, 0.5f};
    const DirectionalShadowDescription Shadow{DepthTexels, 3u, 3u, 0.001f, 1u};
    const float Visibility = SampleDirectionalShadowVisibility(0.5f, 0.5f, 0.7f, Shadow);
    if (!VerifyAssertion(std::abs(Visibility - 1.0f / 9.0f) < 1.0e-5f, "3×3 PCF returns fractional visibility")) return false;

    const DirectionalLightDescription Light{{0.0f, 0.0f, 1.0f}, {1.0f, 1.0f, 1.0f}, 0.1f};
    const EonSpecularDescription Specular{{0.04f, 0.04f, 0.04f}, 48.0f, 1.0f};
    const LinearColor Lit = EvaluateFastSurfaceLighting(Normal, View, Light, Specular, {0.5f, 0.25f, 0.125f}, 1.0f);
    const LinearColor Shadowed = EvaluateFastSurfaceLighting(Normal, View, Light, Specular, {0.5f, 0.25f, 0.125f}, 0.0f);
    if (!VerifyAssertion(Lit.Red > Shadowed.Red && Shadowed.Red > 0.0f,
            "Shadow removes direct radiance but retains ambient fill")) return false;
    return true;
}

bool RenderValidationImage()
{
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();
    ImGuiIO& Io = ImGui::GetIO();
    Io.DisplaySize = ImVec2(1200.0f, 700.0f);
    Io.DeltaTime = 1.0f / 60.0f;
    Io.IniFilename = nullptr;
    Io.Fonts->Build();

    Frontier::Exhibits::SoftwareRasterCanvas Canvas(1200u, 700u, {15u, 17u, 20u, 255u});
    ImGui::NewFrame();
    ImDrawList* Draw = ImGui::GetBackgroundDrawList();
    Draw->AddRectFilled(ImVec2(24.0f, 24.0f), ImVec2(1176.0f, 676.0f), IM_COL32(25, 28, 33, 255), 12.0f);
    Draw->AddText(ImVec2(52.0f, 48.0f), IM_COL32(239, 244, 207, 255), "FRONTIER  /  NATIVE UV SPACE FOUNDATION");
    Draw->AddText(ImVec2(52.0f, 76.0f), IM_COL32(148, 157, 166, 255), "Corner UV topology  |  Multi-matcap faces  |  EON + directional PCF");

    Draw->AddRectFilled(ImVec2(52.0f, 124.0f), ImVec2(575.0f, 620.0f), IM_COL32(18, 20, 24, 255), 8.0f);
    Draw->AddText(ImVec2(74.0f, 144.0f), IM_COL32(220, 224, 228, 255), "UV / UDIM 1001");
    for (uint32_t Line = 0u; Line <= 8u; ++Line)
    {
        const float Offset = 38.0f + static_cast<float>(Line) * 52.0f;
        Draw->AddLine(ImVec2(Offset, 190.0f), ImVec2(Offset, 574.0f), IM_COL32(55, 60, 67, 255));
        Draw->AddLine(ImVec2(90.0f, 152.0f + static_cast<float>(Line) * 48.0f), ImVec2(506.0f, 152.0f + static_cast<float>(Line) * 48.0f), IM_COL32(55, 60, 67, 255));
    }
    Draw->AddRect(ImVec2(90.0f, 190.0f), ImVec2(506.0f, 574.0f), IM_COL32(112, 121, 130, 255), 0.0f, 0, 2.0f);
    Draw->AddLine(ImVec2(90.0f, 574.0f), ImVec2(298.0f, 190.0f), IM_COL32(239, 244, 207, 255), 3.0f);
    Draw->AddLine(ImVec2(298.0f, 190.0f), ImVec2(298.0f, 574.0f), IM_COL32(239, 244, 207, 255), 3.0f);
    Draw->AddLine(ImVec2(298.0f, 574.0f), ImVec2(506.0f, 190.0f), IM_COL32(127, 184, 222, 255), 3.0f);
    Draw->AddLine(ImVec2(506.0f, 190.0f), ImVec2(298.0f, 190.0f), IM_COL32(127, 184, 222, 255), 3.0f);
    Draw->AddLine(ImVec2(298.0f, 574.0f), ImVec2(298.0f, 190.0f), IM_COL32(232, 130, 98, 255), 4.0f);
    Draw->AddText(ImVec2(74.0f, 588.0f), IM_COL32(232, 130, 98, 255), "Orange: marked seam / independent UV charts");

    Draw->AddRectFilled(ImVec2(603.0f, 124.0f), ImVec2(1148.0f, 620.0f), IM_COL32(18, 20, 24, 255), 8.0f);
    Draw->AddText(ImVec2(625.0f, 144.0f), IM_COL32(220, 224, 228, 255), "SURFACE APPEARANCE / FACE LAYERS");
    Draw->AddTriangleFilled(ImVec2(666.0f, 292.0f), ImVec2(858.0f, 228.0f), ImVec2(666.0f, 488.0f), IM_COL32(166, 79, 56, 255));
    Draw->AddTriangleFilled(ImVec2(858.0f, 228.0f), ImVec2(858.0f, 424.0f), ImVec2(666.0f, 488.0f), IM_COL32(70, 117, 157, 255));
    Draw->AddTriangleFilled(ImVec2(858.0f, 228.0f), ImVec2(1050.0f, 292.0f), ImVec2(858.0f, 424.0f), IM_COL32(206, 180, 67, 255));
    Draw->AddTriangleFilled(ImVec2(1050.0f, 292.0f), ImVec2(1050.0f, 488.0f), ImVec2(858.0f, 424.0f), IM_COL32(113, 91, 44, 255));
    Draw->AddLine(ImVec2(858.0f, 228.0f), ImVec2(858.0f, 424.0f), IM_COL32(239, 244, 207, 255), 3.0f);
    Draw->AddText(ImVec2(668.0f, 510.0f), IM_COL32(232, 147, 113, 255), "Face A: 2 matcap layers");
    Draw->AddText(ImVec2(902.0f, 510.0f), IM_COL32(224, 203, 112, 255), "Face B: 1 matcap");
    Draw->AddText(ImVec2(625.0f, 552.0f), IM_COL32(148, 157, 166, 255), "Packed ranges retain independent assignments after editing.");
    Draw->AddText(ImVec2(625.0f, 578.0f), IM_COL32(148, 157, 166, 255), "CPU reference validates EON energy and 3x3 PCF visibility.");
    ImGui::Render();
    Canvas.RasterizeImGuiBatches(ImGui::GetDrawData());

    const std::filesystem::path RepositoryRoot = FRONTIER_REPO_ROOT_DIR;
    const bool Written = Canvas.WritePngFile(RepositoryRoot / "Exhibits/Images/UVSpace/01-NativeFoundation.png");
    ImGui::DestroyContext();
    return VerifyAssertion(Written, "Native UV foundation PNG written");
}

} // namespace

int main()
{
    const bool Valid = ValidateSurfaceFoundation() && ValidateFastLightingAndShadows() && RenderValidationImage();
    if (Valid)
    {
        std::printf("[UVSpaceValidation] Passed UV topology, multi-matcap, EON and PCF checks.\n");
    }
    return Valid ? 0 : 1;
}
