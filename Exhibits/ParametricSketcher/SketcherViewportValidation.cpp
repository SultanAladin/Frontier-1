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

    GeometricRaster::VulkanInterchange Device {};
    std::string Diag {};
    if (!VerifyCheck(Device.InitializeInterchange(RepoRoot, &Diag), "Initialize VulkanInterchange"))
    {
        std::fprintf(stderr, "Diagnostic: %s\n", Diag.c_str());
        return 1;
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
    Raster.Resize(640u, 360u);
    Raster.UploadMatcaps(Matcaps.Pictures);
    Raster.UploadScene(Scene);

    OrbitCamera Cam {};
    Cam.Distance = 4.5;
    const OrbitCameraFrame Frame = FrameOrbitCamera(Cam, 640.0, 360.0);
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
    uint32_t NonZeroIds = 0u;
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
