/*====================================================================================================================================
                                                SKETCHERBLENDIMPORTVALIDATION.CPP
====================================================================================================================================*/
// 🧩 Headless Vulkan validation exhibit: native SolidArc Blender (.blend) import + modifier stack evaluation + GPU VRAM
//    high-poly rendering + BVH-accelerated precision curve retopology tracing across Egoist, Liger, and Quicksilver

//------------------------------------------------------------------------------------------------------------------------
//                                                    INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometricRaster/VulkanInterchange.h"
#include "Engine/Workspaces/ParametricSketcher/Document/SketcherDocument.h"
#include "Engine/Workspaces/ParametricSketcher/Document/SketcherExchange.h"
#include "Engine/Workspaces/ParametricSketcher/Document/SketcherScene.h"
#include "Engine/Workspaces/ParametricSketcher/Viewport/SketcherOrbitCamera.h"
#include "Engine/Workspaces/ParametricSketcher/Viewport/SketcherRasterization.h"
#include "Engine/Workspaces/ParametricSketcher/Viewport/SketcherSnapping.h"
#include "Engine/Workspaces/ParametricSketcher/Viewport/SketcherSurfaceTracing.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

namespace
{

using namespace Frontier;
using namespace Frontier::Workspaces::Sketcher;
using GeometrySpace::Parametric::Vector3;

constexpr uint32_t RenderWidth  = 1280u;
constexpr uint32_t RenderHeight = 720u;

struct ValidationTally
{
    int Passed = 0;
    int Failed = 0;

    void Check(bool Condition, const std::string& Label)
    {
        if (Condition)
        {
            ++Passed;
            std::printf("[PASS] %s\n", Label.c_str());
        }
        else
        {
            ++Failed;
            std::fprintf(stderr, "[FAIL] %s\n", Label.c_str());
        }
    }
};

void WriteRgbaPng(const std::filesystem::path& OutPath, const uint32_t* Pixels, uint32_t Width, uint32_t Height)
{
    std::filesystem::create_directories(OutPath.parent_path());
    stbi_write_png(OutPath.string().c_str(), int(Width), int(Height), 4, Pixels, int(Width * 4u));
}

uint32_t CountNonBackgroundPixels(const uint32_t* Pixels, uint32_t Width, uint32_t Height)
{
    if (!Pixels || Width == 0u || Height == 0u) return 0u;
    const uint32_t Bg = Pixels[0];
    uint32_t Count = 0u;
    const size_t Total = size_t(Width) * Height;
    for (size_t I = 0u; I < Total; ++I)
    {
        if (Pixels[I] != Bg) ++Count;
    }
    return Count;
}

bool RenderVehicleView(
    GeometricRaster::VulkanInterchange& Device,
    SketcherRasterization&              Raster,
    const SketcherDocument&             Document,
    const SketcherScene&                Scene,
    const OrbitCamera&                  Camera,
    const SketcherViewportStyle&        Style,
    const std::filesystem::path&        OutPng,
    uint32_t&                           OutCoveredPixels,
    double&                             OutRenderMs)
{
    const auto Start = std::chrono::steady_clock::now();
    Raster.UploadScene(Scene);
    Raster.UploadReferenceSurfaces(Document.ReferenceSurfaces, Document.ReferenceRevision);

    const OrbitCameraBasis Basis = FrameOrbitCamera(Camera, double(RenderWidth), double(RenderHeight));
    VkCommandBuffer Cmd = Device.BeginSingleTimeCommands();
    if (Cmd == VK_NULL_HANDLE) return false;

    Raster.Record(Cmd, Basis, Style, true, int(RenderWidth / 2u), int(RenderHeight / 2u));
    const uint64_t Tick = Device.DispatchCommandBufferAsync(Cmd);
    if (!Device.AwaitTimelineTick(Tick))
    {
        Device.FreeCommandBuffer(Cmd);
        return false;
    }
    Device.FreeCommandBuffer(Cmd);

    OutRenderMs      = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - Start).count();
    OutCoveredPixels = CountNonBackgroundPixels(Raster.ReadbackColour(), RenderWidth, RenderHeight);
    if (!OutPng.empty()) WriteRgbaPng(OutPng, Raster.ReadbackColour(), RenderWidth, RenderHeight);
    return true;
}

OrbitCamera BuildHeroCamera(const std::array<double, 3>& Low, const std::array<double, 3>& High, double AzimuthDeg, double ElevationDeg, double ZoomScale = 0.88)
{
    OrbitCamera Cam;
    FitOrbitCamera(Cam, Low, High);
    Cam.Azimuth     = AzimuthDeg * 3.14159265358979323846 / 180.0;
    Cam.Elevation   = ElevationDeg * 3.14159265358979323846 / 180.0;
    Cam.Distance   *= ZoomScale;
    Cam.SceneRadius = std::max(1.0, 0.5 * std::hypot(High[0] - Low[0], High[1] - Low[1], High[2] - Low[2]));
    return Cam;
}

} // namespace

int main()
{
    ValidationTally Tally;
    const std::filesystem::path RepoRoot(FRONTIER_REPO_ROOT_DIR);
    const std::filesystem::path ShaderDir(FRONTIER_COMPILED_SHADER_DIR);
    const std::filesystem::path ImageDir = RepoRoot / "Exhibits" / "Images";

    // 1. Exchange format registration checks
    Tally.Check(SketcherExchangeFormatOf("Egoist.blend") == SketcherExchangeFormat::Blend, "Exchange format recognizes .blend");
    Tally.Check(SketcherExchangeFormatOf("LIGER.BLEND") == SketcherExchangeFormat::Blend, "Exchange format recognizes uppercase .BLEND");
    Tally.Check(std::string(SketcherExchangeFormatLabel(SketcherExchangeFormat::Blend)) == "BLEND", "Exchange label returns BLEND");

    // 2. Initialize headless Vulkan + SolidArc GPU Rasterizer
    GeometricRaster::VulkanInterchange Device;
    std::string InitDiag;
    bool HasGpu = Device.InitializeInterchange(RepoRoot, &InitDiag);
    SketcherRasterization Raster;
    if (HasGpu)
    {
        Tally.Check(true, "Headless Vulkan initialized (" + std::string(Device.QueryAdapterName()) + ")");
        std::string RasterDiag;
        const bool RasterOk = Raster.Create(Device, ShaderDir, RasterDiag, 1u);
        Tally.Check(RasterOk, "SketcherRasterization created with SketcherReferenceSurface.slang pipelines" + (RasterOk ? "" : (" (" + RasterDiag + ")")));
        if (!RasterOk) return 1;
        Raster.Resize(RenderWidth, RenderHeight);
    }
    else
    {
#if defined(_WIN32)
        std::printf("[SKIP] No Vulkan ICD available on headless Windows runner (%s); running CPU decode, modifier & BVH CAD trace checks.\n", InitDiag.c_str());
#else
        Tally.Check(false, "Headless Vulkan initialized (" + InitDiag + ")");
        return 1;
#endif
    }

    // 3. Validate all 3 Google Drive vehicles: Egoist, Liger, Quicksilver
    struct VehicleCase
    {
        const char* Name;
        const char* RelPath;
        uint32_t    MinParts;
        uint32_t    MinTris;
        uint32_t    MinMods;
        double      AzimuthDeg;
        double      ElevationDeg;
    };
    const VehicleCase Vehicles[] = {
        { "Egoist",      "Content/Geometry/Vehicles/Blends/Egoist.blend",      44u, 200000u, 75u,  -38.0, 20.0 },
        { "Liger",       "Content/Geometry/Vehicles/Blends/Liger.blend",       18u, 200000u, 30u,  -42.0, 22.0 },
        { "Quicksilver", "Content/Geometry/Vehicles/Blends/Quicksilver.blend", 32u, 750000u, 55u,  -36.0, 20.0 },
    };

    SketcherDocument EgoistDoc = CreateSketcherDocument("EgoistRetopology");
    bool EgoistReady = false;

    for (const VehicleCase& Case : Vehicles)
    {
        const std::filesystem::path FullPath = RepoRoot / Case.RelPath;
        SketcherDocument Doc = CreateSketcherDocument(Case.Name);
        const SketcherBlendImportReport Report = ImportSketcherBlend(Doc, FullPath.string());

        char Msg[256];
        std::snprintf(Msg, sizeof(Msg), "%s decoded (Blender %s · %u parts · %u tris · %u creases · %u mods · %.0f ms)",
            Case.Name, Report.BlenderVersion.c_str(), Report.SourceObjects, Report.OutputTriangles, Report.CreaseSegments,
            Report.EvaluatedModifiers, Report.ElapsedMs);
        Tally.Check(Report.Refusal.empty(), std::string(Msg) + (Report.Refusal.empty() ? "" : (" -> " + Report.Refusal)));
        Tally.Check(Report.SourceObjects >= Case.MinParts, std::string(Case.Name) + " visible parts >= expected");
        Tally.Check(Report.OutputTriangles >= Case.MinTris, std::string(Case.Name) + " evaluated modifier triangles >= expected");
        Tally.Check(Report.EvaluatedModifiers >= Case.MinMods, std::string(Case.Name) + " executed modifiers >= expected");

        if (Report.Refusal.empty() && !Doc.ReferenceSurfaces.empty())
        {
            const SketcherReferenceSurface& Surf = Doc.ReferenceSurfaces.front();
            // Verify symmetric Y mirroring produced bilateral vehicle body (MinY < 0 < MaxY)
            Tally.Check(Surf.MinBound[1] < -10.0 && Surf.MaxBound[1] > 10.0,
                std::string(Case.Name) + " Mirror modifier produces bilateral Y span");

            if (HasGpu)
            {
                // Render 1280x720 studio hero shot on GPU
                SketcherScene EmptyScene = BuildSketcherScene(Doc);
                OrbitCamera HeroCam = BuildHeroCamera(Surf.MinBound, Surf.MaxBound, Case.AzimuthDeg, Case.ElevationDeg, 0.85);
                SketcherViewportStyle StudioStyle;
                StudioStyle.GridEnabled     = true;
                StudioStyle.SurfacesEnabled = true;
                StudioStyle.EdgesEnabled    = true;
                uint32_t Covered = 0u;
                double RenderMs  = 0.0;
                const std::filesystem::path StudioPng = ImageDir / ("SolidArc_BlendImport_" + std::string(Case.Name) + "_Studio.png");
                const bool Rendered = RenderVehicleView(Device, Raster, Doc, EmptyScene, HeroCam, StudioStyle, StudioPng, Covered, RenderMs);
                char RenderMsg[200];
                std::snprintf(RenderMsg, sizeof(RenderMsg), "%s GPU studio render (%u covered px · %.0f ms -> %s)",
                    Case.Name, Covered, RenderMs, StudioPng.filename().string().c_str());
                Tally.Check(Rendered && Covered > 45000u, RenderMsg);

                if (std::string(Case.Name) == "Egoist")
                {
                    // Render Zebra Class-A curvature check on Egoist
                    SketcherViewportStyle ZebraStyle = StudioStyle;
                    ZebraStyle.ZebraStripes = 14.0f;
                    uint32_t ZebraCovered = 0u;
                    double ZebraMs = 0.0;
                    const std::filesystem::path ZebraPng = ImageDir / "SolidArc_BlendImport_Egoist_Zebra.png";
                    const bool ZebraOk = RenderVehicleView(Device, Raster, Doc, EmptyScene, HeroCam, ZebraStyle, ZebraPng, ZebraCovered, ZebraMs);
                    Tally.Check(ZebraOk && ZebraCovered > 45000u, "Egoist GPU Zebra Class-A curvature render");
                }
            }

            if (std::string(Case.Name) == "Egoist")
            {
                EgoistDoc   = std::move(Doc);
                EgoistReady = true;
            }
        }
    }

    // 4. Precision CAD Retopology Curve Tracing + Loft Panel Rebuild on Egoist GPU Vehicle
    if (EgoistReady && !EgoistDoc.ReferenceSurfaces.empty())
    {
        SketcherReferenceSurface& Ref = EgoistDoc.ReferenceSurfaces.front();
        // Set semi-translucent ghost opacity for crisp CAD curve tracing over vehicle surface
        Ref.Opacity = 0.48f;
        ++EgoistDoc.ReferenceRevision;

        const OrbitCamera TopTraceCam = BuildHeroCamera(Ref.MinBound, Ref.MaxBound, -38.0, 32.0, 0.82);
        const OrbitCameraBasis TraceBasis = FrameOrbitCamera(TopTraceCam, double(RenderWidth), double(RenderHeight));
        SketcherScene WorkScene = BuildSketcherScene(EgoistDoc);
        SketcherSnapRepository SnapRepo {};

        // Trace two smooth parallel Class-A styling profiles along Egoist's sculpted front hood deck
        const double SweepStartX[2] = { 0.54, 0.50 };
        const double SweepStartY[2] = { 0.53, 0.57 };
        const double SweepEndX[2]   = { 0.65, 0.61 };
        const double SweepEndY[2]   = { 0.59, 0.63 };
        std::vector<std::string> TracedCurveLabels;
        int TotalRayHits = 0;

        for (int CurveIdx = 0; CurveIdx < 2; ++CurveIdx)
        {
            std::vector<Vector3> Controls;
            for (int Step = 0; Step < 6; ++Step)
            {
                const double T = double(Step) / 5.0;
                const double ScreenX = RenderWidth  * (SweepStartX[CurveIdx] + (SweepEndX[CurveIdx] - SweepStartX[CurveIdx]) * T);
                const double ScreenY = RenderHeight * (SweepStartY[CurveIdx] + (SweepEndY[CurveIdx] - SweepStartY[CurveIdx]) * T);
                const SketcherSnap Hit = SnapSketcherTrace(SnapRepo, EgoistDoc, WorkScene, TraceBasis, ScreenX, ScreenY, false, false);
                if (Hit.Kind == SketcherSnapCategory::Face)
                {
                    ++TotalRayHits;
                    if (Controls.empty() || GeometrySpace::Parametric::Distance(Controls.back(), Hit.Position) > 2.0)
                        Controls.push_back(Hit.Position);
                }
            }

            if (Controls.size() >= 4u)
            {
                const SketcherSurfaceTrace Projected = ProjectSketcherTrace(
                    SnapRepo, EgoistDoc, WorkScene, TraceBasis, Controls,
                    SketcherDrawingCategory::Curve, GeometrySpace::Parametric::SketchSplineForm::Hermite,
                    false, true, 0.5);
                char TraceMsg[200];
                std::snprintf(TraceMsg, sizeof(TraceMsg), "Precision surface-traced NURBS profile #%d (%zu controls -> %zu poles, dev %.4f mm)",
                    CurveIdx + 1, Controls.size(),
                    Projected.Fit.Curve.Definition ? Projected.Fit.Curve.Definition->Poles.size() : 0u,
                    Projected.WorstSurfaceDeviation);
                Tally.Check(Projected.Refusal.empty() && Projected.Fit.Curve.Definition != nullptr,
                    std::string(TraceMsg) + (Projected.Refusal.empty() ? "" : (" -> " + Projected.Refusal)));

                if (Projected.Refusal.empty() && Projected.Fit.Curve.Definition)
                {
                    const SketcherCommandOutcome Added = DrawSketcherProjectedCurve(EgoistDoc, Projected.Fit.Curve, false);
                    Tally.Check(Added.AcceptedCondition, "Committed traced NURBS SpaceCurve #" + std::to_string(CurveIdx + 1) + " to SolidArc Document");
                    if (Added.AcceptedCondition && !EgoistDoc.Tree.Steps.empty())
                    {
                        TracedCurveLabels.push_back(EgoistDoc.Tree.Steps.back().Label);
                    }
                }
            }
        }
        Tally.Check(TotalRayHits >= 8, "BVH SnapSketcherTrace achieved >= 8 exact surface hits on 240K tri Egoist");

        // Loft a rebuilt CAD body sheet panel directly across the two surface-traced precision NURBS profiles!
        if (TracedCurveLabels.size() == 2u)
        {
            const SketcherCommandOutcome Lofted = LoftSketcherCurves(EgoistDoc, TracedCurveLabels, 0, false);
            Tally.Check(Lofted.AcceptedCondition, "Lofted rebuilt CAD Class-A panel from traced Egoist profiles" + (Lofted.AcceptedCondition ? "" : (" -> " + Lofted.Refusal)));
        }

        // Render combined GPU exhibit: ghost Egoist vehicle + traced NURBS curves + rebuilt CAD lofted panel
        if (HasGpu)
        {
            SketcherScene FinalScene = BuildSketcherScene(EgoistDoc);
            SketcherViewportStyle RetopoStyle;
            RetopoStyle.GridEnabled     = true;
            RetopoStyle.SurfacesEnabled = true;
            RetopoStyle.EdgesEnabled    = true;
            RetopoStyle.EdgeStroke      = 2.2f;
            uint32_t RetopoCovered = 0u;
            double RetopoMs = 0.0;
            const std::filesystem::path RetopoPng = ImageDir / "SolidArc_BlendImport_Egoist_RetopologyTrace.png";
            const bool RetopoOk = RenderVehicleView(Device, Raster, EgoistDoc, FinalScene, TopTraceCam, RetopoStyle, RetopoPng, RetopoCovered, RetopoMs);
            Tally.Check(RetopoOk && RetopoCovered > 45000u, "GPU render of Ghost Egoist + Traced NURBS Curves + Rebuilt CAD Panel");
        }
    }

    if (HasGpu) Raster.Destroy();
    std::printf("[SketcherBlendImportValidation] Passed=%d Failed=%d\n", Tally.Passed, Tally.Failed);
    return Tally.Failed == 0 ? 0 : 1;
}
