/*============================================================================================================================================
                                                      SKETCHERVIEWPORTVALIDATION.CPP
============================================================================================================================================*/
// 🧩 Validation exhibit for Phase 3 Parametric Sketcher Vulkan viewport rasterization, MatCaps, R32 identity pick & Slang shaders

#include "Engine/GeometricRaster/VulkanInterchange.h"
#include "Engine/Workspaces/ParametricSketcher/Document/SketcherDocument.h"
#include "Engine/Workspaces/ParametricSketcher/Document/SketcherMatcap.h"
#include "Engine/Workspaces/ParametricSketcher/Document/SketcherScene.h"
#include "Engine/Workspaces/ParametricSketcher/Viewport/SketcherPortableImage.h"
#include "Engine/Workspaces/ParametricSketcher/Viewport/SketcherRasterization.h"
#include "Engine/Workspaces/ParametricSketcher/Viewport/SketcherSnapping.h"

#include <cstdio>
#include <filesystem>
#include <string>

namespace
{

using namespace Frontier;
using namespace Frontier::GeometrySpace::Parametric;
using namespace Frontier::Workspaces::Sketcher;

bool VerifyCheck(bool Condition, const char* Message) noexcept
{
    if (!Condition)
    {
        std::fprintf(stderr, "[SketcherViewportValidation] FAILED: %s\n", Message);
        return false;
    }
    return true;
}

} // namespace

int main()
{
    uint32_t Passed = 0u;

    const std::filesystem::path RepoRoot(FRONTIER_REPO_ROOT_DIR);
    const std::filesystem::path ShaderDir(FRONTIER_COMPILED_SHADER_DIR);

    SketcherDocument Doc = CreateSketcherDocument("ViewportBracket");
    {
        SketcherConstructRequest BoxReq {};
        BoxReq.Kind     = SketcherConstructKind::Block;
        BoxReq.Position = { -1.2, -0.9, -0.6 };
        BoxReq.Width    = 2.4;
        BoxReq.Depth    = 1.8;
        BoxReq.Height   = 1.2;
        if (!VerifyCheck(ConstructSketcherPrimitive(Doc, BoxReq).AcceptedCondition, "Construct Block"))
        {
            return 1;
        }

        SketcherDrawingRequest CircleReq {};
        CircleReq.Kind            = SketcherDrawingKind::Circle;
        CircleReq.GridPlane       = 0;
        CircleReq.CentreCondition = true;
        CircleReq.FillCondition   = true;
        CircleReq.Points          = { { 0.0, 0.0 }, { 0.45, 0.0 } };
        if (!VerifyCheck(DrawSketcherSketch(Doc, CircleReq).AcceptedCondition, "Draw Circle Sketch"))
        {
            return 1;
        }
        const std::string SketchLabel = Doc.Tree.Steps.back().Label;
        if (!VerifyCheck(ExtrudeSketcherSketch(Doc, SketchLabel, 3.0, FeatureCombination::Difference).AcceptedCondition, "Cut hole"))
        {
            return 1;
        }
    }

    const SketcherScene Scene = BuildSketcherScene(Doc);
    const SketcherMatcapLibrary Matcaps = AssembleSketcherMatcaps(Doc.Appearance);

    OrbitCamera Cam {};
    Cam.Distance = 4.5;
    const OrbitCameraFrame Frame = FrameOrbitCamera(Cam, 640.0, 360.0);

    // 1. Object snapping catalogue + SketcherSnapFilter gating (CPU)
    {
        SketcherSnapCatalogue Catalogue = CatalogueSketcherSnaps(Doc, Scene);
        double CornerPx = 0.0, CornerPy = 0.0;
        if (!VerifyCheck(!Catalogue.Points.empty() && !Catalogue.Curves.empty(), "Snap catalogue populated") ||
            !VerifyCheck(ProjectSketcherPoint(Frame, Vector3{ 1.2, 0.9, 0.6 }, CornerPx, CornerPy), "Project block corner to viewport"))
        {
            return 1;
        }
        SketcherSnapQuery Query {};
        Query.Frame  = Frame;
        Query.PixelX = CornerPx;
        Query.PixelY = CornerPy;
        const SketcherSnap HitCorner = FindSketcherSnap(Catalogue, Doc, Scene, Query);
        if (!VerifyCheck(HitCorner.Kind == SketcherSnapKind::Endpoint, "FindSketcherSnap snaps to corner Endpoint"))
        {
            return 1;
        }
        Query.Filter.EndpointEnabled = false;
        Query.Filter.CurveEnabled    = false;
        Query.Filter.FaceEnabled     = false;
        const SketcherSnap FilteredCorner = FindSketcherSnap(Catalogue, Doc, Scene, Query);
        if (!VerifyCheck(FilteredCorner.Kind != SketcherSnapKind::Endpoint && FilteredCorner.Kind != SketcherSnapKind::Curve &&
                             FilteredCorner.Kind != SketcherSnapKind::Face,
                         "SketcherSnapFilter disables Endpoint/Curve/Face snaps"))
        {
            return 1;
        }
        ++Passed;
    }

    uint32_t NonZeroIds = 0u;
    GeometricRaster::VulkanInterchange Device {};
    std::string Diag {};
    if (!Device.InitializeInterchange(RepoRoot, &Diag))
    {
#if defined(_WIN32)
        std::printf("[SKIP] No Vulkan ICD available on headless Windows runner (%s); skipping GPU viewport rasterization.\n", Diag.c_str());
        std::printf("[SketcherViewportValidation] Passed=%u Failed=0 (CPU checks)\n", Passed);
        return 0;
#else
        std::fprintf(stderr, "[SketcherViewportValidation] FAILED: Initialize VulkanInterchange: %s\n", Diag.c_str());
        return 1;
#endif
    }
    ++Passed;

    SketcherRasterization Raster {};
    std::string Refusal {};
    if (!VerifyCheck(Raster.Create(Device, ShaderDir, Refusal), "Create SketcherRasterization with all 12 Sketcher SPIR-V pipelines"))
    {
        std::fprintf(stderr, "Refusal: %s\n", Refusal.c_str());
        return 1;
    }
    ++Passed;

    Raster.Resize(640u, 360u);
    Raster.UploadMatcaps(Matcaps.Pictures);
    Raster.UploadScene(Scene);

    SketcherViewportStyle Style {};
    Style.GridEnabled = true;

    VkCommandBuffer Cmd = Device.BeginSingleTimeCommands();
    Raster.Record(Cmd, Frame, Style, true, 320, 180);
    vkEndCommandBuffer(Cmd);

    const uint64_t Tick = Device.DispatchCommandBufferAsync(Cmd);
    if (!VerifyCheck(Device.AwaitTimelineTick(Tick), "Await GPU viewport render timeline tick"))
    {
        return 1;
    }
    Device.FreeCommandBuffer(Cmd);

    const uint32_t* Colour   = Raster.ReadbackColour();
    const uint32_t* Identity = Raster.ReadbackIdentity();
    for (uint32_t Px = 0u; Px < 640u * 360u; ++Px)
    {
        if (Identity[Px] != 0u)
        {
            ++NonZeroIds;
        }
    }
    if (!VerifyCheck(NonZeroIds > 1000u, "R32_UINT identity buffer captures rendered solid faces and edges"))
    {
        return 1;
    }
    ++Passed;

    const std::filesystem::path ProofPath = RepoRoot / "Exhibits/Images/ParametricSketcher/SketcherViewportProof.png";
    std::filesystem::create_directories(ProofPath.parent_path());
    if (!VerifyCheck(WriteSketcherPng(ProofPath.string(), 640u, 360u, Colour), "Write SketcherViewportProof.png"))
    {
        return 1;
    }
    ++Passed;

    Raster.Destroy();
    Device.TeardownInterchange();

    std::printf("[SketcherViewportValidation] Passed=%u Failed=0 (NonZeroIds=%u)\n", Passed, NonZeroIds);
    return 0;
}
