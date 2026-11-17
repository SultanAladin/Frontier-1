/*============================================================================================================================================
                                                      SKETCHERDOCUMENTVALIDATION.CPP
============================================================================================================================================*/
// 🧩 Validation exhibit for Phase 2 Parametric Sketcher Document, Codec, Scene, DrawingSheet & MatCap layer

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

    std::printf("[SketcherDocumentValidation] Passed=%u Failed=0\n", Passed);
    return 0;
}
