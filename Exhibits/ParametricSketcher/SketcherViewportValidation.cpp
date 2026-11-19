/*============================================================================================================================================
                                                      SKETCHERVIEWPORTVALIDATION.CPP
============================================================================================================================================*/
// 🧩 Validation exhibit for Phase 3 Parametric Sketcher Vulkan viewport rasterization, MatCaps, R32 identity pick & Slang shaders

#include "Engine/GeometricRaster/VulkanInterchange.h"
#include "Engine/GeometrySpace/Parametric/Surfacing/CurveFitting.h"
#include "Engine/Workspaces/ParametricSketcher/Document/SketcherDocument.h"
#include "Engine/Workspaces/ParametricSketcher/Document/SketcherDocumentCodec.h"
#include "Engine/Workspaces/ParametricSketcher/Document/SketcherMatcap.h"
#include "Engine/Workspaces/ParametricSketcher/Document/SketcherScene.h"
#include "Engine/Workspaces/ParametricSketcher/Viewport/SketcherPortableImage.h"
#include "Engine/Workspaces/ParametricSketcher/Viewport/SketcherRasterization.h"
#include "Engine/Workspaces/ParametricSketcher/Viewport/SketcherSnapping.h"

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
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

    // 2. Curved-surface retopology visual proof + 3D SpaceCurve x SpaceCurve Intersection snap
    {
        auto SpherePoint = [](double Radius, double Theta, double Phi) -> Vector3
        {
            const double CosPhi = std::cos(Phi);
            return { Radius * CosPhi * std::cos(Theta), Radius * CosPhi * std::sin(Theta), Radius * std::sin(Phi) };
        };
        auto SampleSphereArc = [&](double Radius, double Theta0, double Phi0, double Theta1, double Phi1, int Count = 9) -> NurbsCurve
        {
            std::vector<Vector3> Pts;
            Pts.reserve(size_t(Count));
            for (int I = 0; I < Count; ++I)
            {
                const double U = double(I) / double(Count - 1);
                Pts.push_back(SpherePoint(Radius, Theta0 + (Theta1 - Theta0) * U, Phi0 + (Phi1 - Phi0) * U));
            }
            return FitSpaceCurve(Pts, {}).Curve;
        };

        SketcherDocument Retopo = CreateSketcherDocument("CurvedRetopologyProof");
        SketcherConstructRequest RefSphere {};
        RefSphere.Kind   = SketcherConstructKind::Sphere;
        RefSphere.Radius = 2.0;
        if (!VerifyCheck(ConstructSketcherPrimitive(Retopo, RefSphere).AcceptedCondition, "Construct curved Clay reference sphere"))
        {
            return 1;
        }
        const std::string RefBody = Retopo.Summary.Bodies.front().Key;
        SelectSketcherName(Retopo, SketcherSelectionKind::Body, RefBody, false);
        (void)AssignSketcherMatcap(Retopo, "Clay");
        (void)ToggleSketcherLock(Retopo, RefBody);

        // Trace crossing 3D curves on the curved surface (slightly proud at R=2.012 so CAD patches read crisply over Clay)
        const double RCurve = 2.012;
        // Latitudinal curves (phi = 0.12 and phi = 0.48, spanning theta = -0.95 to 0.30)
        // Meridional curves (theta = -0.75, theta = -0.35 -> curved Square; theta = 0.15 -> curved Rectangle)
        // Diagonal crossing curves forming a curved Triangle on top of the Square (crossing LatHigh inside [-0.75, -0.35])
        const auto LatLow  = SampleSphereArc(RCurve, -0.95, 0.12,  0.30, 0.12);
        const auto LatHigh = SampleSphereArc(RCurve, -0.95, 0.48,  0.30, 0.48);
        const auto MerLeft = SampleSphereArc(RCurve, -0.75, -0.02, -0.75, 0.62);
        const auto MerMid  = SampleSphereArc(RCurve, -0.35, -0.02, -0.35, 0.62);
        const auto MerRgt  = SampleSphereArc(RCurve,  0.15, -0.02,  0.15, 0.62);
        const auto TriLeft = SampleSphereArc(RCurve, -0.71, 0.40, -0.52, 0.86);
        const auto TriRgt  = SampleSphereArc(RCurve, -0.39, 0.40, -0.58, 0.86);

        const std::pair<const char*, NurbsCurve> TraceDefs[] = {
            { "LatLow",  LatLow  }, { "LatHigh", LatHigh },
            { "MerLeft", MerLeft }, { "MerMid",  MerMid  }, { "MerRgt", MerRgt },
            { "TriLeft", TriLeft }, { "TriRgt",  TriRgt  }
        };
        for (const auto& [Tag, CurveDef] : TraceDefs)
        {
            const auto Out = DrawSketcherProjectedCurve(Retopo, CurveDef, false, true, 0.001, true);
            if (!Out.AcceptedCondition)
            {
                std::fprintf(stderr, "Curve %s failed: %s\n", Tag, Out.Refusal.c_str());
                return 1;
            }
        }

        int RebuiltFaces = 0;
        for (const auto& Body : Retopo.Summary.Bodies)
        {
            if (Body.Key == RefBody) continue;
            RebuiltFaces += Body.Faces;
            SelectSketcherName(Retopo, SketcherSelectionKind::Body, Body.Key, false);
            (void)AssignSketcherMatcap(Retopo, "Ceramic");
        }
        if (!VerifyCheck(RebuiltFaces >= 3, "Curved retopology cage built Square, Rectangle, and Triangle CAD patches"))
        {
            std::fprintf(stderr, "RebuiltFaces=%d\n", RebuiltFaces);
            return 1;
        }
        ClearSketcherSelection(Retopo);

        const SketcherScene RetopoScene = BuildSketcherScene(Retopo);
        const SketcherMatcapLibrary RetopoMatcaps = AssembleSketcherMatcaps(Retopo.Appearance);

        // Verify 3D SpaceCurve x SpaceCurve Intersection snap at crossing (-0.35, 0.48)
        OrbitCamera RetopoCam {};
        RetopoCam.Target    = { 0.0, 0.0, 0.35 };
        RetopoCam.Azimuth   = -0.45;
        RetopoCam.Elevation = 0.42;
        RetopoCam.Distance  = 5.2;
        const OrbitCameraFrame RetopoFrame = FrameOrbitCamera(RetopoCam, 1280.0, 720.0);

        SketcherSnapCatalogue RetopoCatalogue = CatalogueSketcherSnaps(Retopo, RetopoScene);
        const Vector3 CrossPos = SpherePoint(RCurve, -0.35, 0.48);
        double CrossPx = 0.0, CrossPy = 0.0;
        if (!VerifyCheck(ProjectSketcherPoint(RetopoFrame, CrossPos, CrossPx, CrossPy), "Project SpaceCurve crossing to viewport"))
        {
            return 1;
        }
        SketcherSnapQuery CrossQ {};
        CrossQ.Frame  = RetopoFrame;
        CrossQ.PixelX = CrossPx;
        CrossQ.PixelY = CrossPy;
        CrossQ.Filter = { false, false, false, false, true, false, false, false, false };
        CrossQ.FreeCurvesOnly = true;
        CrossQ.OverlayCurvesVisible = true;
        const SketcherSnap CrossHit = FindSketcherSnap(RetopoCatalogue, Retopo, RetopoScene, CrossQ);
        if (!VerifyCheck(CrossHit.Kind == SketcherSnapKind::Intersection && Distance(CrossHit.Position, CrossPos) < 0.02,
                         "FindSketcherSnap detects 3D SpaceCurve x SpaceCurve Intersection"))
        {
            return 1;
        }
        ++Passed;

        // Save .solidarc for live FrontierSketcher UI capture
        const std::filesystem::path SolidArcOut = std::filesystem::temp_directory_path() / "CurvedRetopologyProof.solidarc";
        {
            std::ofstream Out(SolidArcOut, std::ios::binary);
            Out << EncodeSketcherDocument(Retopo);
        }

        // Render 1280x720 curved retopology viewport proof
        Raster.Resize(1280u, 720u);
        Raster.UploadMatcaps(RetopoMatcaps.Pictures);
        Raster.UploadScene(RetopoScene);
        SketcherViewportStyle RetopoStyle {};
        RetopoStyle.GridEnabled = true;
        VkCommandBuffer RetopoCmd = Device.BeginSingleTimeCommands();
        Raster.Record(RetopoCmd, RetopoFrame, RetopoStyle, true, 640, 360);
        vkEndCommandBuffer(RetopoCmd);
        const uint64_t RetopoTick = Device.DispatchCommandBufferAsync(RetopoCmd);
        if (!VerifyCheck(Device.AwaitTimelineTick(RetopoTick), "Await curved retopology GPU render"))
        {
            return 1;
        }
        Device.FreeCommandBuffer(RetopoCmd);
        const std::filesystem::path RetopoPng = RepoRoot / "Exhibits/Images/ParametricSketcher/CurvedRetopologyViewportProof.png";
        if (!VerifyCheck(WriteSketcherPng(RetopoPng.string(), 1280u, 720u, Raster.ReadbackColour()),
                         "Write CurvedRetopologyViewportProof.png"))
        {
            return 1;
        }
        ++Passed;
    }

    Raster.Destroy();
    Device.TeardownInterchange();

    std::printf("[SketcherViewportValidation] Passed=%u Failed=0 (NonZeroIds=%u)\n", Passed, NonZeroIds);
    return 0;
}
