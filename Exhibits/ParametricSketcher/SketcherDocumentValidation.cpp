/*============================================================================================================================================
                                                      SKETCHERDOCUMENTVALIDATION.CPP
============================================================================================================================================*/
// 🧩 Validation exhibit for Phase 2 Parametric Sketcher Document, Codec, Scene, DrawingSheet & MatCap layer

#include "Engine/Workspaces/ParametricSketcher/Document/SketcherDocument.h"
#include "Engine/Workspaces/ParametricSketcher/Document/SketcherDocumentCodec.h"
#include "Engine/Workspaces/ParametricSketcher/Document/SketcherDrawingSheet.h"
#include "Engine/Workspaces/ParametricSketcher/Document/SketcherMatcap.h"
#include "Engine/Workspaces/ParametricSketcher/Document/SketcherScene.h"

#include <cmath>
#include <cstdio>
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

    std::printf("[SketcherDocumentValidation] Passed=%u Failed=0\n", Passed);
    return 0;
}
