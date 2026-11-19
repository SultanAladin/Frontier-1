/*============================================================================================================================================
                                                      SKETCHERDOCUMENTVALIDATION.CPP
============================================================================================================================================*/
// 🧩 Validation exhibit for Phase 2 Parametric Sketcher Document, Codec, Scene, DrawingSheet & MatCap layer

#include "Engine/GeometrySpace/Parametric/Geometry/SurfaceGeometry.h"
#include "Engine/GeometrySpace/Parametric/Surfacing/CurveFitting.h"
#include "Engine/Workspaces/ParametricSketcher/Document/SketcherDocument.h"
#include "Engine/Workspaces/ParametricSketcher/Document/SketcherDocumentCodec.h"
#include "Engine/Workspaces/ParametricSketcher/Document/SketcherDrawingSheet.h"
#include "Engine/Workspaces/ParametricSketcher/Document/SketcherImagePlane.h"
#include "Engine/Workspaces/ParametricSketcher/Document/SketcherMatcap.h"
#include "Engine/Workspaces/ParametricSketcher/Document/SketcherScene.h"

#include <cmath>
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
        std::fprintf(stderr, "[SketcherDocumentValidation] FAILED: %s\n", Message);
        return false;
    }
    return true;
}

} // namespace

int main()
{
    uint32_t Passed = 0u;

    // 1. Construct primitive + sketch + extrude + scene tessellation & CPU pick
    SketcherDocument Doc = CreateSketcherDocument("Bracket");
    {
        SketcherConstructRequest BoxReq {};
        BoxReq.Kind     = SketcherConstructKind::Block;
        BoxReq.Position = { -1.5, -1.0, 0.0 };
        BoxReq.Width    = 3.0;
        BoxReq.Depth    = 2.0;
        BoxReq.Height   = 1.2;
        const SketcherCommandOutcome BoxOut = ConstructSketcherPrimitive(Doc, BoxReq);
        if (!VerifyCheck(BoxOut.AcceptedCondition && Doc.Summary.Bodies.size() == 1u,
                         "Construct Block primitive in SketcherDocument"))
        {
            return 1;
        }

        SketcherDrawingRequest CircleReq {};
        CircleReq.Kind            = SketcherDrawingKind::Circle;
        CircleReq.GridPlane       = 0;
        CircleReq.CentreCondition = true;
        CircleReq.FillCondition   = true;
        CircleReq.Points          = { { 0.0, 0.0 }, { 0.45, 0.0 } };
        const SketcherCommandOutcome SketchOut = DrawSketcherSketch(Doc, CircleReq);
        if (!VerifyCheck(SketchOut.AcceptedCondition, "Draw filled circle sketch"))
        {
            return 1;
        }

        const std::string SketchLabel = Doc.Tree.Steps.back().Label;
        const SketcherCommandOutcome CutOut = ExtrudeSketcherSketch(
            Doc,
            SketchLabel,
            2.0,
            FeatureCombination::Difference);
        if (!VerifyCheck(CutOut.AcceptedCondition && Doc.Summary.Bodies.size() == 1u,
                         "Extrude circle Difference cut"))
        {
            return 1;
        }

        const SketcherScene Scene = BuildSketcherScene(Doc);
        if (!VerifyCheck(!Scene.Corners.empty() && !Scene.Segments.empty() && !Scene.Faces.empty(),
                         "BuildSketcherScene produces renderable face corners and edge segments"))
        {
            return 1;
        }

        const OrbitCamera Cam {};
        const OrbitCameraFrame Frame = FrameOrbitCamera(Cam, 1280.0, 720.0);
        const OrbitRay CenterRay = CastOrbitRay(Frame, 640.0, 360.0);
        const auto Hit = PickSketcherScene(Scene, CenterRay, 0.05);
        if (!VerifyCheck(Hit.has_value(), "PickSketcherScene hits extruded bracket"))
        {
            return 1;
        }
        ++Passed;
    }

    // 2. Deterministic TOML save -> load -> save roundtrip
    {
        const std::string Encoded1 = EncodeSketcherDocument(Doc);
        const SketcherDocumentFile Parsed = DecodeSketcherDocument(Encoded1);
        if (!VerifyCheck(Parsed.Refusal.empty() && Parsed.Title == "Bracket" && !Parsed.Tree.Steps.empty(),
                         "DecodeSketcherDocument parses saved document cleanly"))
        {
            return 1;
        }
        SketcherDocument Reloaded = CreateSketcherDocument(Parsed.Title);
        const auto ReplaceOut = ReplaceSketcherTree(Reloaded, Parsed.Tree, Parsed.Captions, "Reload");
        if (!VerifyCheck(ReplaceOut.AcceptedCondition, "ReplaceSketcherTree regenerates loaded document"))
        {
            return 1;
        }
        const std::string Encoded2 = EncodeSketcherDocument(Reloaded);
        if (!VerifyCheck(Encoded1 == Encoded2, "EncodeSketcherDocument roundtrip is byte-identical"))
        {
            return 1;
        }
        ++Passed;
    }

    // 3. Undo / Redo + Built-in MatCap Library + Drawing Sheet Composition
    {
        if (!VerifyCheck(!Doc.UndoEntries.empty(), "Document records undo history"))
        {
            return 1;
        }
        const auto& Matcaps = SketcherBuiltInMatcaps();
        if (!VerifyCheck(!Matcaps.empty(), "MatCap library populated"))
        {
            return 1;
        }
        const SketcherDrawingSheet Sheet = ComposeSketcherDrawingSheet(Doc, Doc.DrawingSheet);
        if (!VerifyCheck(Sheet.Refusal.empty() && Sheet.Bodies == 1, "ComposeSketcherDrawingSheet succeeds for bracket"))
        {
            return 1;
        }
        ++Passed;
    }

    // 4. .ImageSpace export -> decode roundtrip & SketcherImagePlane validation
    {
        SketcherPicture Source {};
        Source.Width  = 4u;
        Source.Height = 4u;
        Source.Pixels.resize(4u * 4u * 4u, 0u);
        for (uint32_t Y = 0u; Y < Source.Height; ++Y)
        {
            for (uint32_t X = 0u; X < Source.Width; ++X)
            {
                const size_t Base = (size_t(Y) * Source.Width + X) * 4u;
                Source.Pixels[Base + 0u] = static_cast<uint8_t>((X * 64u) + 32u);
                Source.Pixels[Base + 1u] = static_cast<uint8_t>((Y * 64u) + 16u);
                Source.Pixels[Base + 2u] = 200u;
                Source.Pixels[Base + 3u] = 255u;
            }
        }

        const std::filesystem::path TempImageSpace =
            std::filesystem::temp_directory_path() / "SketcherDocTestRef.ImageSpace";
        std::string ImgDiag {};
        if (!VerifyCheck(ExportSketcherPictureAsImageSpace(Source, TempImageSpace, ImgDiag, "DocRefPlane"),
                         "ExportSketcherPictureAsImageSpace writes .ImageSpace"))
        {
            std::fprintf(stderr, "ImageSpace export diag: %s\n", ImgDiag.c_str());
            return 1;
        }
        const SketcherPicture Decoded = DecodeSketcherPicture(TempImageSpace.string());
        std::filesystem::remove(TempImageSpace);
        if (!VerifyCheck(Decoded.Refusal.empty() && Decoded.Width == 4u && Decoded.Height == 4u &&
                             Decoded.Pixels.size() == Source.Pixels.size(),
                         "DecodeSketcherPicture reads .ImageSpace back cleanly"))
        {
            return 1;
        }
        for (size_t Idx = 0u; Idx < Source.Pixels.size(); ++Idx)
        {
            if (!VerifyCheck(std::abs(int(Decoded.Pixels[Idx]) - int(Source.Pixels[Idx])) <= 2,
                             ".ImageSpace sRGB<->Linear pixel roundtrip matches within 2 LSB"))
            {
                return 1;
            }
        }
        SketcherImagePlane Plane {};
        Plane.Name   = "Blueprint";
        Plane.Path   = TempImageSpace.string();
        Plane.Corner = { -2.0, 0.0, 1.0 };
        Plane.Right  = { 4.0, 0.0, 0.0 };
        Plane.Down   = { 0.0, 0.0, -2.0 };
        if (!VerifyCheck(ExamineSketcherImagePlane(Plane).empty(), "ExamineSketcherImagePlane accepts valid plane"))
        {
            return 1;
        }
        ++Passed;
    }

    // 5. Curved-surface crossing curves -> split + auto-surface (Triangle, Square, Rectangle, Multi-loop Cage, Fill & Loft)
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

        // 5a. Triangle on curved sphere (3 crossing curves with overhanging tails, auto-split + auto-surface)
        {
            SketcherDocument RetopoDoc = CreateSketcherDocument();
            SketcherConstructRequest RefSphere {};
            RefSphere.Kind   = SketcherConstructKind::Sphere;
            RefSphere.Radius = 2.0;
            const auto RefOut = ConstructSketcherPrimitive(RetopoDoc, RefSphere);
            if (!VerifyCheck(RefOut.AcceptedCondition && RetopoDoc.Summary.Bodies.size() == 1u, "Construct curved reference sphere"))
            {
                return 1;
            }
            const std::string RefBody = RetopoDoc.Summary.Bodies.front().Key;
            (void)ToggleSketcherLock(RetopoDoc, RefBody);

            const double R = 2.0;
            // Base curve (phi = 0.20, theta in [-0.35, 0.55])
            // Left slanted curve (theta: -0.25 -> 0.15, phi: 0.10 -> 0.80)
            // Right slanted curve (theta: 0.45 -> 0.05, phi: 0.10 -> 0.80)
            const auto C1 = SampleSphereArc(R, -0.35, 0.20, 0.55, 0.20);
            const auto C2 = SampleSphereArc(R, -0.25, 0.10, 0.15, 0.80);
            const auto C3 = SampleSphereArc(R,  0.45, 0.10, 0.05, 0.80);
            const auto O1 = DrawSketcherProjectedCurve(RetopoDoc, C1, false, true, 0.001, true);
            const auto O2 = DrawSketcherProjectedCurve(RetopoDoc, C2, false, true, 0.001, true);
            const auto O3 = DrawSketcherProjectedCurve(RetopoDoc, C3, false, true, 0.001, true);
            if (!VerifyCheck(O1.AcceptedCondition && O2.AcceptedCondition && O3.AcceptedCondition,
                             "Curved triangle crossing curves accepted"))
            {
                std::fprintf(stderr, "Refusal: %s / %s / %s\n", O1.Refusal.c_str(), O2.Refusal.c_str(), O3.Refusal.c_str());
                return 1;
            }
            if (!VerifyCheck(RetopoDoc.Summary.Bodies.size() == 2u,
                             "Auto-split + Auto-surface builds triangular NURBS surface on curved sphere"))
            {
                std::fprintf(stderr, "Bodies=%zu Curves=%zu Steps=%zu\n",
                             RetopoDoc.Summary.Bodies.size(), RetopoDoc.Regeneration.Curves.size(), RetopoDoc.Tree.Steps.size());
                return 1;
            }
            const SketcherScene Scene = BuildSketcherScene(RetopoDoc);
            bool VerifiedPatch = false;
            for (const auto& Face : Scene.Faces)
            {
                if (Scene.Bodies[Face.Body].Key == RefBody) continue;
                VerifiedPatch = Face.CornerCount >= 3u;
                for (uint32_t K = 0u; K < Face.CornerCount; ++K)
                {
                    const auto& P = Scene.Corners[Face.FirstCorner + K].Position;
                    const double RSurf = std::sqrt(P[0] * P[0] + P[1] * P[1] + P[2] * P[2]);
                    if (!VerifyCheck(std::abs(RSurf - R) < 0.08, "Curved triangle patch conforms to sphere curvature"))
                    {
                        std::fprintf(stderr, "RSurf=%.4f expected ~%.4f\n", RSurf, R);
                        return 1;
                    }
                }
            }
            if (!VerifyCheck(VerifiedPatch, "Curved triangle patch produces valid tessellated faces"))
            {
                return 1;
            }
            ++Passed;
        }

        // 5b. Square + Rectangle + Multi-cell cage on curved sphere
        {
            SketcherDocument CageDoc = CreateSketcherDocument();
            SketcherConstructRequest RefSphere {};
            RefSphere.Kind   = SketcherConstructKind::Sphere;
            RefSphere.Radius = 2.0;
            const auto RefOut = ConstructSketcherPrimitive(CageDoc, RefSphere);
            if (!VerifyCheck(RefOut.AcceptedCondition, "Construct curved reference sphere for multi-cell cage")) return 1;
            const std::string RefBody = CageDoc.Summary.Bodies.front().Key;
            (void)ToggleSketcherLock(CageDoc, RefBody);

            const double R = 2.0;
            // Grid of 2 latitudinal + 3 meridional crossing curves -> creates BOTH a curved Square AND an adjacent curved Rectangle!
            const auto Lat1 = SampleSphereArc(R, -0.35, 0.15, 0.95, 0.15);
            const auto Lat2 = SampleSphereArc(R, -0.35, 0.55, 0.95, 0.55);
            const auto Mer1 = SampleSphereArc(R, -0.20, 0.00, -0.20, 0.70);
            const auto Mer2 = SampleSphereArc(R,  0.20, 0.00,  0.20, 0.70);
            const auto Mer3 = SampleSphereArc(R,  0.80, 0.00,  0.80, 0.70);
            if (!VerifyCheck(DrawSketcherProjectedCurve(CageDoc, Lat1, false, true, 0.001, true).AcceptedCondition &&
                             DrawSketcherProjectedCurve(CageDoc, Lat2, false, true, 0.001, true).AcceptedCondition &&
                             DrawSketcherProjectedCurve(CageDoc, Mer1, false, true, 0.001, true).AcceptedCondition &&
                             DrawSketcherProjectedCurve(CageDoc, Mer2, false, true, 0.001, true).AcceptedCondition &&
                             DrawSketcherProjectedCurve(CageDoc, Mer3, false, true, 0.001, true).AcceptedCondition,
                             "Crossing curves for square + rectangle cage accepted"))
            {
                return 1;
            }
            int RetopoFaces = 0;
            for (const auto& B : CageDoc.Summary.Bodies)
            {
                if (B.Key != RefBody) RetopoFaces += B.Faces;
            }
            if (!VerifyCheck(RetopoFaces >= 2,
                             "Multi-cell crossing cage builds both square and rectangle CAD surfaces"))
            {
                std::fprintf(stderr, "CageDoc Bodies=%zu RetopoFaces=%d Curves=%zu Steps=%zu\n",
                             CageDoc.Summary.Bodies.size(), RetopoFaces, CageDoc.Regeneration.Curves.size(), CageDoc.Tree.Steps.size());
                return 1;
            }
            ++Passed;
        }

        // 5c. Fill hover/apply + Loft on 1-curve closed & 4-curve rectangle loops without AutoSurface
        {
            SketcherDocument ManualDoc = CreateSketcherDocument();
            const double R = 2.0;
            const auto Lat1 = SampleSphereArc(R, -0.45, 0.15, 0.45, 0.15);
            const auto Lat2 = SampleSphereArc(R, -0.45, 0.50, 0.45, 0.50);
            const auto Mer1 = SampleSphereArc(R, -0.30, 0.00, -0.30, 0.65);
            const auto Mer2 = SampleSphereArc(R,  0.30, 0.00,  0.30, 0.65);
            (void)DrawSketcherProjectedCurve(ManualDoc, Lat1, false, true, 0.001, false);
            (void)DrawSketcherProjectedCurve(ManualDoc, Lat2, false, true, 0.001, false);
            (void)DrawSketcherProjectedCurve(ManualDoc, Mer1, false, true, 0.001, false);
            (void)DrawSketcherProjectedCurve(ManualDoc, Mer2, false, true, 0.001, false);
            if (!VerifyCheck(ManualDoc.Summary.Bodies.empty(), "AutoSurface=false leaves cage unsurfaced until Fill or Build"))
            {
                return 1;
            }
            // Ray aimed straight at center of curved rectangle (theta=0, phi=0.325)
            const Vector3 Target = SpherePoint(R, 0.0, 0.325);
            const Vector3 Eye    = Target * 2.5;
            const Vector3 Dir    = Normalize(Target - Eye);
            const auto Preview   = PreviewSketcherSpaceLoop(ManualDoc, Eye, Dir, 0.001);
            if (!VerifyCheck(Preview.Refusal.empty() && Preview.Labels.size() == 4u,
                             "PreviewSketcherSpaceLoop detects 4-edge curved rectangle under pointer"))
            {
                std::fprintf(stderr, "Preview refusal: %s labels=%zu\n", Preview.Refusal.c_str(), Preview.Labels.size());
                return 1;
            }
            const auto FillOut = FillSketcherSpaceLoop(ManualDoc, Preview.Labels, 0.001);
            if (!VerifyCheck(FillOut.AcceptedCondition && ManualDoc.Summary.Bodies.size() == 1u,
                             "FillSketcherSpaceLoop surfaces hovered curved rectangle loop"))
            {
                std::fprintf(stderr, "Fill refusal: %s\n", FillOut.Refusal.c_str());
                return 1;
            }

            // Single closed smooth 3D curve on sphere -> LoftSketcherCurves
            std::vector<Vector3> LoopPts;
            for (int I = 0; I <= 24; ++I)
            {
                const double Ang = 2.0 * 3.14159265358979323846 * (double(I) / 24.0);
                LoopPts.push_back(SpherePoint(R, 0.25 * std::cos(Ang), -0.40 + 0.18 * std::sin(Ang)));
            }
            const auto ClosedFit = FitSpaceCurve(LoopPts, {});
            if (!VerifyCheck(ClosedFit.Refusal.empty() && ClosedFit.Curve.Definition != nullptr,
                             "Fit smooth closed 3D curve on sphere"))
            {
                std::fprintf(stderr, "Fit refusal: %s\n", ClosedFit.Refusal.c_str());
                return 1;
            }
            const auto ClosedCurveOut = DrawSketcherProjectedCurve(ManualDoc, ClosedFit.Curve, true, false, 0.001, false);
            if (!VerifyCheck(ClosedCurveOut.AcceptedCondition, "Draw single closed 3D curve"))
            {
                std::fprintf(stderr, "ClosedCurve refusal: %s\n", ClosedCurveOut.Refusal.c_str());
                return 1;
            }
            const std::string ClosedLabel = ManualDoc.Tree.Steps.back().Label;
            const auto LoftOut = LoftSketcherCurves(ManualDoc, { ClosedLabel }, 0, false);
            int TotalManualFaces = 0;
            for (const auto& B : ManualDoc.Summary.Bodies) TotalManualFaces += B.Faces;
            if (!VerifyCheck(LoftOut.AcceptedCondition && TotalManualFaces == 2,
                             "LoftSketcherCurves surfaces single closed 3D curve"))
            {
                std::fprintf(stderr, "Loft refusal: %s bodies=%zu faces=%d\n",
                             LoftOut.Refusal.c_str(), ManualDoc.Summary.Bodies.size(), TotalManualFaces);
                return 1;
            }
            ++Passed;
        }
    }

    std::printf("[SketcherDocumentValidation] Passed=%u Failed=0\n", Passed);
    return 0;
}
