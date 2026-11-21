/*====================================================================================================================================
                                                          SOLIDARCSCRIPT.CPP
====================================================================================================================================*/
// 🧩 Scripted pointer / keyboard input for headless workspace captures, with expectations on the resulting state

//------------------------------------------------------------------------------------------------------------------------
//                                                    INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Workspaces/ParametricSketcher/Interface/SketcherScript.h"
#include "Engine/GeometrySpace/Parametric/Measurement/MassProperties.h"
#include "Engine/GeometrySpace/Parametric/Sketch/SketchProfile.h"
#include "Engine/GeometrySpace/Parametric/Sketch/SketchSpline.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <map>
#include <numbers>
#include <sstream>

namespace Frontier::Workspaces::Sketcher
{

namespace
{

constexpr int RevealPatience = 4;                                                            // [frame] - Frames a clipped click target may take to scroll into view

//------------------------------------------------------------------------------------------------------------------------
//                                                    PARSING
//------------------------------------------------------------------------------------------------------------------------

std::vector<std::string> SplitWords(const std::string& Line)
{
    std::vector<std::string> Words;
    std::istringstream       Stream(Line);
    std::string              Word;
    while (Stream >> Word) Words.push_back(Word);
    return Words;
}

ImGuiKey KeyByName(const std::string& Name)
{
    static const std::map<std::string, ImGuiKey> Keys = {
        { "A", ImGuiKey_A }, { "B", ImGuiKey_B }, { "C", ImGuiKey_C }, { "D", ImGuiKey_D }, { "E", ImGuiKey_E }, { "F", ImGuiKey_F },
        { "G", ImGuiKey_G }, { "H", ImGuiKey_H }, { "I", ImGuiKey_I }, { "J", ImGuiKey_J }, { "K", ImGuiKey_K }, { "L", ImGuiKey_L }, { "M", ImGuiKey_M }, { "N", ImGuiKey_N }, { "O", ImGuiKey_O }, { "P", ImGuiKey_P }, { "Q", ImGuiKey_Q }, { "R", ImGuiKey_R },
        { "S", ImGuiKey_S }, { "T", ImGuiKey_T }, { "U", ImGuiKey_U }, { "V", ImGuiKey_V }, { "W", ImGuiKey_W }, { "X", ImGuiKey_X }, { "Y", ImGuiKey_Y }, { "Z", ImGuiKey_Z },
        { "0", ImGuiKey_0 }, { "1", ImGuiKey_1 }, { "2", ImGuiKey_2 }, { "3", ImGuiKey_3 }, { "4", ImGuiKey_4 }, { "5", ImGuiKey_5 },
        { "6", ImGuiKey_6 }, { "7", ImGuiKey_7 }, { "8", ImGuiKey_8 }, { "9", ImGuiKey_9 },
        { "Keypad0", ImGuiKey_Keypad0 }, { "Keypad1", ImGuiKey_Keypad1 }, { "Keypad2", ImGuiKey_Keypad2 }, { "Keypad3", ImGuiKey_Keypad3 },
        { "Keypad4", ImGuiKey_Keypad4 }, { "Keypad5", ImGuiKey_Keypad5 }, { "Keypad6", ImGuiKey_Keypad6 }, { "Keypad7", ImGuiKey_Keypad7 },
        { "Keypad8", ImGuiKey_Keypad8 }, { "Keypad9", ImGuiKey_Keypad9 },
        { "KeypadDecimal", ImGuiKey_KeypadDecimal }, { "KeypadAdd", ImGuiKey_KeypadAdd },
        { "KeypadSubtract", ImGuiKey_KeypadSubtract }, { "KeypadEnter", ImGuiKey_KeypadEnter },
        { "Period", ImGuiKey_Period },
        { "Space", ImGuiKey_Space }, { "Enter", ImGuiKey_Enter }, { "Escape", ImGuiKey_Escape },
        { "Delete", ImGuiKey_Delete }, { "Backspace", ImGuiKey_Backspace }, { "Tab", ImGuiKey_Tab }, { "Home", ImGuiKey_Home }, { "End", ImGuiKey_End },
        { "LeftBracket", ImGuiKey_LeftBracket }, { "RightBracket", ImGuiKey_RightBracket }, { "Equal", ImGuiKey_Equal }, { "Minus", ImGuiKey_Minus },
        { "LeftAlt", ImGuiKey_LeftAlt },
    };
    const auto Found = Keys.find(Name);
    return Found == Keys.end() ? ImGuiKey_None : Found->second;
}

int ButtonByName(const std::string& Name)
{
    if (Name == "Right") return ImGuiMouseButton_Right;
    if (Name == "Middle") return ImGuiMouseButton_Middle;
    return ImGuiMouseButton_Left;
}

// Consumes a target from Words at Index: "@Widget", "world X Y Z", "viewport U V" or "X Y", plus optional "+DX +DY".
bool ReadTarget(const std::vector<std::string>& Words, size_t& Index, SketcherScriptTarget& Target)
{
    if (Index >= Words.size()) return false;
    try
    {
        if (Words[Index][0] == '@')
        {
            Target.Widget = Words[Index].substr(1);
            Index += 1;
        }
        else if (Words[Index] == "world" && Index + 3 < Words.size())
        {
            Target.World = true;
            for (int Axis = 0; Axis < 3; ++Axis) Target.Values[Axis] = std::stod(Words[Index + 1 + Axis]);
            Index += 4;
        }
        else if (Words[Index] == "viewport" && Index + 2 < Words.size())
        {
            Target.Fraction  = true;
            Target.Values[0] = std::stod(Words[Index + 1]);
            Target.Values[1] = std::stod(Words[Index + 2]);
            Index += 3;
        }
        else if (Index + 1 < Words.size())
        {
            Target.Values[0] = std::stod(Words[Index]);
            Target.Values[1] = std::stod(Words[Index + 1]);
            Index += 2;
        }
        else
        {
            return false;
        }
        if (Index + 1 < Words.size() && Words[Index] == "offset")
        {
            Target.OffsetX = std::stof(Words[Index + 1]);
            Target.OffsetY = Index + 2 < Words.size() ? std::stof(Words[Index + 2]) : 0.0f;
            Index += 3;
        }
    }
    catch (...)
    {
        return false;
    }
    return true;
}

void ReadModifiers(const std::vector<std::string>& Words, size_t Index, bool& Control, bool& Shift, bool& Alternate)
{
    for (; Index < Words.size(); ++Index)
    {
        if (Words[Index] == "Ctrl") Control = true;
        if (Words[Index] == "Shift") Shift = true;
        if (Words[Index] == "Alt") Alternate = true;
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    EXECUTION
//------------------------------------------------------------------------------------------------------------------------

bool ResolveTarget(const SketcherScriptTarget& Target, const SketcherWorkspaceStatus& State, ImVec2& Point)
{
    const auto& Rect = State.ViewportRect;
    if (!Target.Widget.empty())
    {
        const ImVec4* Found = State.Widgets.Find(Target.Widget);
        if (!Found) return false;
        Point = ImVec2(0.5f * (Found->x + Found->z), 0.5f * (Found->y + Found->w));
    }
    else if (Target.World)
    {
        const OrbitCameraBasis Frame = FrameOrbitCamera(State.Camera, Rect[2], Rect[3]);
        double Clip[4] = {};
        for (int Row = 0; Row < 4; ++Row)
            Clip[Row] = Frame.ClipFromWorld[0 * 4 + Row] * Target.Values[0] + Frame.ClipFromWorld[1 * 4 + Row] * Target.Values[1] +
                        Frame.ClipFromWorld[2 * 4 + Row] * Target.Values[2] + Frame.ClipFromWorld[3 * 4 + Row];
        if (Clip[3] <= 0.0) return false;
        Point = ImVec2(Rect[0] + float((Clip[0] / Clip[3] * 0.5 + 0.5) * Rect[2]), Rect[1] + float((Clip[1] / Clip[3] * 0.5 + 0.5) * Rect[3]));
    }
    else if (Target.Fraction)
    {
        Point = ImVec2(Rect[0] + float(Target.Values[0]) * Rect[2], Rect[1] + float(Target.Values[1]) * Rect[3]);
    }
    else
    {
        Point = ImVec2(float(Target.Values[0]), float(Target.Values[1]));
    }
    Point.x += Target.OffsetX;
    Point.y += Target.OffsetY;
    return true;
}

// 📝 Observed value of a named state field, as text and (when numeric) as a number.
bool ObserveField(const SketcherWorkspaceStatus& State, const std::string& Field, std::string& Text, double& Number)
{
    const SketcherDocument& Document = State.Document;
    auto Numeric = [&](double Value)
    {
        Number = Value;
        char Buffer[64];
        std::snprintf(Buffer, sizeof(Buffer), "%.10g", Value);
        Text = Buffer;
        return true;
    };
    auto Join = [&](const std::vector<std::string>& Names)
    {
        Text.clear();
        for (const std::string& Name : Names) Text += (Text.empty() ? "" : ",") + Name;
        Number = double(Names.size());
        return true;
    };
    if (Field == "faces") return Numeric(Document.Summary.Faces);
    if (Field == "edges") return Numeric(Document.Summary.Edges);
    if (Field == "genus") return Numeric(Document.Summary.Genus);
    if (Field == "volume") return Numeric(Document.Summary.Volume);
    if (Field == "sound") return Numeric(Document.Summary.SoundCondition ? 1 : 0);
    if (Field == "offset-linked-count") return Numeric(std::count_if(Document.Tree.Steps.begin(),Document.Tree.Steps.end(),[](const auto& S){return !S.CurveOffsetGuide.empty();}));
    if (Field == "last-curve-mid-x") {
        for(auto It=Document.Tree.Steps.rbegin();It!=Document.Tree.Steps.rend();++It)
            if(auto C=Document.Regeneration.Curves.find(It->Label);C!=Document.Regeneration.Curves.end()) return Numeric(GeometrySpace::Parametric::EvaluateNurbsCurve(*C->second.Curve.Definition,.5).Position.X);
        return Numeric(0);
    }
    if (Field == "steps") return Numeric(double(Document.Tree.Steps.size()));
    if (Field == "refused") return Numeric(Document.Regeneration.Refused);
    if (Field == "replay-resumed") return Numeric(Document.Regeneration.Resumed);
    if (Field == "replay-ms") return Numeric(Document.Summary.RegenerationMs);
    if (Field == "preview-resumed")
    {
        // The staged preview of the open session (bevel, move / rotate / scale, revolve / sweep, joint, dragged value); −1 without one.
        const SketcherDocument* Preview = State.Bevel.Preview ? State.Bevel.Preview.get() : State.Transform.Preview ? State.Transform.Preview.get() : State.Sweep.Preview ? State.Sweep.Preview.get()
                                        : State.Joint.Preview ? State.Joint.Preview.get() : State.ParameterDrag.Preview.get();
        return Numeric(Preview ? Preview->Regeneration.Resumed : -1);
    }
    if (Field == "live-volume")
    {
        // Volume of the staged gizmo / sketch-point drag preview (mid-drag evidence); the document's volume without one.
        const SketcherDocument* Preview = State.Gizmo.DragCondition && State.Gizmo.Preview ? State.Gizmo.Preview.get()
                                        : State.SketchPoints.DragCondition && State.SketchPoints.Preview ? State.SketchPoints.Preview.get() : nullptr;
        if (!Preview && State.Gizmo.DragCondition && State.Gizmo.RigidCondition)
            return Numeric(RigidPreviewVolume(Document, State.Gizmo.Names, GizmoPreviewMap(State.Gizmo)));
        return Numeric(Preview ? Preview->Summary.Volume : Document.Summary.Volume);
    }
    if (Field == "arc-widest" || Field == "live-arc-widest")
    {
        // Widest sketch arc sweep [deg] (a fillet stays under 180); live-: of the staged sketch-point drag preview.
        const SketcherDocument* Preview = Field == "live-arc-widest" && State.SketchPoints.DragCondition && State.SketchPoints.Preview ? State.SketchPoints.Preview.get() : &Document;
        double Widest = 0.0;
        for (const auto& [Label, Sketch] : Preview->Regeneration.Sketches)
            for (const GeometrySpace::Parametric::SketchCurve& Curve : Sketch.Curves)
            {
                if (Curve.Kind != GeometrySpace::Parametric::SketchCurveCategory::Arc || Curve.Centre < 0) continue;
                const GeometrySpace::Parametric::Vector2 Centre = Sketch.Points[Curve.Centre].Position;
                const GeometrySpace::Parametric::Vector2 Start  = Sketch.Points[Curve.Start].Position - Centre;
                const GeometrySpace::Parametric::Vector2 End    = Sketch.Points[Curve.End].Position - Centre;
                double Sweep = std::atan2(GeometrySpace::Parametric::Cross(Start, End), GeometrySpace::Parametric::Dot(Start, End));
                if (Sweep <= 0.0) Sweep += 2.0 * std::numbers::pi;
                Widest = std::max(Widest, Sweep * 180.0 / std::numbers::pi);
            }
        return Numeric(Widest);
    }
    if (Field == "live-staged") return Numeric((State.Gizmo.DragCondition && (State.Gizmo.Preview || State.Gizmo.RigidCondition)) || (State.SketchPoints.DragCondition && State.SketchPoints.Preview) ? 1 : 0);
    if (Field == "picked-points") return Numeric(double(State.SketchPoints.Points.size()));     // picked mode-1 sketch points
    if (Field == "point-commits") return Numeric(double(State.SketchPoints.Commits));
    if (Field == "picked-point-x" || Field == "picked-point-y")
    {
        // Plane coordinates of the first picked sketch point (as solved); NaN without one.
        const SketcherSketchPointSession& Points = State.SketchPoints;
        const auto Solved = Document.Regeneration.Sketches.find(Points.Sketch);
        if (Points.Points.empty() || Solved == Document.Regeneration.Sketches.end() || Points.Points[0] >= int(Solved->second.Points.size())) return Numeric(std::nan(""));
        const GeometrySpace::Parametric::Vector2 Position = Solved->second.Points[Points.Points[0]].Position;
        return Numeric(Field == "picked-point-x" ? Position.X : Position.Y);
    }
    if (Field == "undo") return Numeric(double(Document.UndoEntries.size()));
    if (Field == "redo") return Numeric(double(Document.RedoEntries.size()));
    if (Field == "selected-faces") return Join(Document.Selection.Faces);
    if (Field == "selected-edges") return Join(Document.Selection.Edges);
    if (Field == "selected-vertices") return Join(Document.Selection.Vertices);
    if (Field == "selected-bodies") return Join(Document.Selection.Bodies);
    if (Field == "bodies") return Numeric(double(Document.Summary.Bodies.size()));
    if (Field == "active-body") { Text = Document.Regeneration.Active; Number = double(Text.size()); return true; }
    if (Field == "body-captions")
    {
        std::vector<std::string> Captions;
        for (const SketcherBodyFigures& Figures : Document.Summary.Bodies) Captions.push_back(Figures.Caption);
        return Join(Captions);
    }
    if (Field == "body-volumes")
    {
        // "Block1 1.000000 · Cylinder1 0.392699" (creation order), Number = the count.
        Text.clear();
        for (const SketcherBodyFigures& Figures : Document.Summary.Bodies)
        {
            char Line[96];
            std::snprintf(Line, sizeof(Line), "%s%s %.6f", Text.empty() ? "" : " · ", Figures.Caption.c_str(), Figures.Volume);
            Text += Line;
        }
        Number = double(Document.Summary.Bodies.size());
        return true;
    }
    if (Field == "body-matcaps") return Numeric(double(Document.Appearance.BodyMatcaps.size()));
    if (Field == "boolean") return Numeric(State.Boolean.ActiveCondition ? 1 : 0);
    if (Field == "boolean-commits") return Numeric(double(State.Boolean.Commits));
    if (Field == "transform") return Numeric(State.Transform.ActiveCondition ? 1 : 0);
    if (Field == "transform-commits") return Numeric(double(State.Transform.Commits));
    if (Field == "transform-readout") { Text = State.Transform.Readout; Number = double(Text.size()); return true; }
    if (Field == "transform-shift-x") return Numeric(State.Transform.Map.Shift.X);
    if (Field == "transform-shift-y") return Numeric(State.Transform.Map.Shift.Y);
    if (Field == "transform-shift-z") return Numeric(State.Transform.Map.Shift.Z);
    if (Field == "transform-preview-volume")
        return Numeric(State.Transform.Preview ? State.Transform.Preview->Summary.Volume
                       : State.Transform.RigidCondition ? RigidPreviewVolume(Document, State.Transform.Keys, State.Transform.StagedMap) : 0.0);
    if (Field == "profile-sink") return Numeric(State.Bevel.ProfileSink);
    if (Field == "extrude-start")
    {
        // Start offset of the last Extrude step (0 without one).
        for (auto Step = Document.Tree.Steps.rbegin(); Step != Document.Tree.Steps.rend(); ++Step)
            if (Step->Kind == GeometrySpace::Parametric::FeatureStepCategory::Extrude) return Numeric(GeometrySpace::Parametric::EvaluateFeatureValue(Document.Tree, Step->Start));
        return Numeric(0.0);
    }
    if (Field == "transform-rigid") return Numeric(State.Transform.RigidCondition ? 1 : 0);
    if (Field == "gizmo-rigid") return Numeric(State.Gizmo.DragCondition && State.Gizmo.RigidCondition ? 1 : 0);
    if (Field == "sweep") return Numeric(State.Sweep.ActiveCondition ? 1 : 0);
    if (Field == "sweep-commits") return Numeric(double(State.Sweep.Commits));
    if (Field == "sweep-amount") return Numeric(State.Sweep.Amount);
    if (Field == "sweep-refusal") { Text = State.Sweep.Refusal; Number = double(Text.size()); return true; }
    if (Field == "sweep-outline") return Numeric(double(State.Sweep.Outline.size()));
    if (Field == "sweep-verb") { Text = State.Sweep.Verb == SketcherSweepVerb::Offset ? "Offset" : State.Sweep.Verb == SketcherSweepVerb::Pipe ? "Pipe" : State.Sweep.Verb == SketcherSweepVerb::Revolve ? "Revolve" : "Evolution"; Number = double(int(State.Sweep.Verb)); return true; }
    if (Field == "sweep-axis") { Text = DescribeSketcherRevolveAxis(State.Document, State.Sweep.Path, State.Sweep.Axis); Number = double(State.Sweep.Axis.Line); return true; }
    if (Field == "sweep-path") { Text = State.Sweep.Path; Number = double(Text.size()); return true; }
    if (Field == "sweep-profile") { Text = State.Sweep.Profile; Number = double(Text.size()); return true; }
    if (Field == "sweep-hover") { Text = State.Sweep.HoverSketch; Number = double(State.Sweep.HoverCurve); return true; }
    if (Field == "sweep-preview") return Numeric(State.Sweep.Preview ? 1 : 0);
    if (Field == "hidden") return Numeric(double(Document.Appearance.HiddenBodies.size()));
    if (Field == "hidden-curves") return Numeric(double(Document.Appearance.HiddenCurves.size()));
    if (Field == "removed-curves") return Numeric(double(Document.Appearance.RemovedCurves.size()));
    if (Field == "isolate-active") return Numeric(State.Isolate.ActiveCondition ? 1 : 0);
    if (Field == "dimensions") return Numeric(double(Document.Dimensions.size()));
    if (Field == "dimension-active") return Numeric(State.DimensionTool.ActiveCondition ? 1 : 0);
    if (Field == "dimension-stage") return Numeric(double(State.DimensionTool.Stage));
    if (Field == "dimension-text")
    {
        Text.clear();
        for (const SketcherDimension& Dim : Document.Dimensions)
            Text += (Text.empty() ? "" : " · ") + Dim.Id + "=" + FormatSketcherDimension(Dim);
        Number = double(Document.Dimensions.size());
        return true;
    }
    if (Field == "outliner-hover") { Text = State.Outliner.HoveredCondition ? State.Outliner.Hovered.Key : std::string(); Number = State.Outliner.HoveredCondition ? 1.0 : 0.0; return true; }
    if (Field == "locked") return Numeric(double(Document.Appearance.LockedFigures.size()));
    if (Field == "face-plane") { Text = State.Drawing.FacePlaneCondition ? State.Drawing.FaceName : std::string(); Number = State.Drawing.FacePlaneCondition ? 1.0 : 0.0; return true; }
    if (Field == "face-plane-z") return Numeric(State.Drawing.FacePlaneCondition ? State.Drawing.FacePlane.Origin.Z : 0.0);
    if (Field == "last-sketch-z" || Field == "last-sketch-normal-z")
    {
        // 📝 The newest sketch step's plane: its origin height, or the z of its normal.
        for (auto Step = Document.Tree.Steps.rbegin(); Step != Document.Tree.Steps.rend(); ++Step)
            if (Step->Kind == GeometrySpace::Parametric::FeatureStepCategory::Sketch) return Numeric(Field == "last-sketch-z" ? Step->Plane.Origin.Z : Step->Plane.ZAxis.Z);
        return Numeric(0.0);
    }
    if (Field == "last-step") { Text = Document.Tree.Steps.empty() ? std::string() : Document.Tree.Steps.back().Label; Number = Document.Tree.Steps.empty() || !Document.Tree.Steps.back().ClosedCondition ? 0.0 : 1.0; return true; }
    if (Field == "scene-bodies") return Numeric(double(State.Scene.Bodies.size()));
    if (Field == "clipboard") return Numeric(double(State.Clipboard.Keys.size()));
    if (Field == "constrain") return Numeric(State.Constrain.ActiveCondition ? 1 : 0);
    if (Field == "constrain-commits") return Numeric(double(State.Constrain.Commits));
    if (Field == "constrain-refusal") { Text = State.Constrain.Refusal; Number = double(Text.size()); return true; }
    if (Field.starts_with("constrain") || Field.starts_with("constraint") || (Field.starts_with("sketch-") && Field != "sketch-curve-total") || Field == "picked-constraint")
    {
        // 📝 The K session's sketch (else the focused sketch step) as regenerated.
        const std::string Label  = SketcherConstraintSketch(State);
        const auto        Solved = Document.Regeneration.Sketches.find(Label);
        const GeometrySpace::Parametric::SketchModel*        Sketch = Solved != Document.Regeneration.Sketches.end() ? &Solved->second : nullptr;
        const GeometrySpace::Parametric::SketchSolveOutcome* Solve  = nullptr;
        for (const GeometrySpace::Parametric::FeatureStepReport& Report : Document.Regeneration.Steps) if (Report.Label == Label) Solve = &Report.Solve;
        if (!Sketch) { Text.clear(); Number = -1.0; return true; }
        if (Field == "constrain-picks")
        {
            std::vector<std::string> Names;
            for (const SketcherSketchPick& Pick : State.Constrain.Picks)
            {
                if (Pick.Sketch.empty()) { Names.push_back(GeometrySpace::Parametric::NameSketchPick(*Sketch, Pick.Pick)); continue; }
                const auto Other   = Document.Regeneration.Sketches.find(Pick.Sketch);
                const auto Caption = Document.Captions.find(Pick.Sketch);
                Names.push_back((Other != Document.Regeneration.Sketches.end() ? GeometrySpace::Parametric::NameSketchPick(Other->second, Pick.Pick) : std::string("?")) + " of "
                                + (Caption != Document.Captions.end() ? Caption->second : Pick.Sketch));
            }
            return Join(Names);
        }
        if (Field == "sketch-links")
        {
            // Linked points + curves of the sketch: "Point2,Line2.Start,…,Line2", Number = the count.
            std::vector<std::string> Names;
            for (int Point = 0; Point < int(Sketch->Points.size()); ++Point)
                if (!Sketch->Points[Point].Link.Sketch.empty()) Names.push_back(GeometrySpace::Parametric::NameSketchPick(*Sketch, { GeometrySpace::Parametric::SketchPickCategory::Point, Point }));
            for (int Curve = 0; Curve < int(Sketch->Curves.size()); ++Curve)
                if (!Sketch->Curves[Curve].Link.Sketch.empty()) Names.push_back(GeometrySpace::Parametric::SketchCurveName(Sketch->Curves[Curve]));
            return Join(Names);
        }
        if (Field == "constraint-count") return Numeric(double(Sketch->Constraints.size()));
        if (Field == "constraints")
        {
            std::vector<std::string> Labels;
            for (const GeometrySpace::Parametric::SketchConstraint& Constraint : Sketch->Constraints) Labels.push_back(Constraint.Label.empty() ? std::string("-") : Constraint.Label);
            return Join(Labels);
        }
        if (Field == "sketch-dof") return Numeric(Solve ? double(Solve->DegreesOfFreedom) : -1.0);
        if (Field == "sketch-status")
        {
            Text   = !Solve ? std::string("unsolved") : (!Solve->ConflictGroups.empty() || !Solve->Converged) ? std::string("conflicting") : Solve->DegreesOfFreedom == 0 ? std::string("fully constrained") : std::string("under-constrained");
            Number = Solve ? double(Solve->DegreesOfFreedom) : -1.0;
            return true;
        }
        if (Field == "picked-constraint")
        {
            const int Picked = State.Constrain.ActiveCondition ? State.Constrain.PickedConstraint : -1;
            Text   = Picked >= 0 && Picked < int(Sketch->Constraints.size()) ? Sketch->Constraints[Picked].Label + " " + GeometrySpace::Parametric::DescribeSketchConstraint(*Sketch, Sketch->Constraints[Picked]) : std::string{};
            Number = double(Picked);
            return true;
        }
        if (Field == "sketch-points")
        {
            // "(0, 0) (3, 0) …" at %.6g, Number = the count.
            Text.clear();
            for (const GeometrySpace::Parametric::SketchPoint& Point : Sketch->Points)
            {
                char Pair[80];
                std::snprintf(Pair, sizeof(Pair), "%s(%.6g, %.6g)", Text.empty() ? "" : " ", std::abs(Point.Position.X) < 5.0e-13 ? 0.0 : Point.Position.X, std::abs(Point.Position.Y) < 5.0e-13 ? 0.0 : Point.Position.Y);
                Text += Pair;
            }
            Number = double(Sketch->Points.size());
            return true;
        }
    }
    if (Field == "knife") return Numeric(State.Knife.ActiveCondition ? 1 : 0);
    if (Field == "knife-commits") return Numeric(double(State.Knife.Commits));
    if (Field == "knife-depth") return Numeric(State.Knife.Depth);
    if (Field == "knife-size") return Numeric(State.Knife.Size);
    if (Field == "knife-aspect") return Numeric(State.Knife.Aspect);
    if (Field == "knife-size-by-move") return Numeric(State.Knife.SizeByMoveCondition ? 1 : 0);
    if (Field == "knife-blends") return Numeric(double(State.Knife.Blends.size()));
    if (Field == "knife-blend-focus") return Numeric(double(State.Knife.BlendFocus + 1));
    if (Field == "knife-blend-size") return Numeric(State.Knife.BlendFocus >= 0 && State.Knife.BlendFocus < int(State.Knife.Blends.size()) ? State.Knife.Blends[State.Knife.BlendFocus].Size : 0.0);
    if (Field == "knife-blend-shape") return Numeric(State.Knife.BlendFocus >= 0 && State.Knife.BlendFocus < int(State.Knife.Blends.size()) ? double(int(State.Knife.Blends[State.Knife.BlendFocus].Shape)) : -1.0);
    if (Field == "knife-turn") return Numeric(State.Knife.Turn * 180.0 / std::numbers::pi);
    if (Field == "knife-through") return Numeric(State.Knife.ThroughCondition ? 1 : 0);
    if (Field == "knife-placed") return Numeric(State.Knife.PlacedCondition ? 1 : 0);
    if (Field == "knife-removed") return Numeric(State.Knife.Removed);
    if (Field == "knife-sizing") return Numeric(State.Knife.SizingCondition ? 1 : 0);
    if (Field == "knife-insert-depth") return Numeric(State.Knife.InsertDepth);
    if (Field == "knife-insert-through") return Numeric(State.Knife.InsertThroughCondition ? 1 : 0);
    if (Field == "knife-hover") return Numeric(State.Knife.HoverCondition ? 1 : 0);
    if (Field == "knife-pending") return Numeric(State.Knife.DrawPendingCondition ? 1 : 0);
    if (Field == "knife-sink") return Numeric(State.Knife.Sink);
    if (Field == "knife-align") return Numeric(State.Knife.AlignCondition ? 1 : 0);
    if (Field == "knife-rigid") return Numeric(State.Knife.RigidCondition ? 1 : 0);
    if (Field == "knife-slice")
    {
        // An open chain slices (as the interface's KnifeSliceCondition).
        const auto Solved = State.Document.Regeneration.Sketches.find(State.Knife.Sketch);
        const bool Slice  = State.Knife.Mode == SketcherKnifeMode::Sketch && Solved != State.Document.Regeneration.Sketches.end() &&
                            ExtractSketchProfile(Solved->second).Refusal.rfind("open profile", 0) == 0;
        return Numeric(Slice ? 1 : 0);
    }
    if (Field == "knife-inserts") return Numeric(double(SketcherCompoundCutterCount + int(State.Knife.Library.Inserts.size())));
    if (Field == "knife-mode")
    {
        // "Sketch" / "Insert" / "Object", Number = 0 / 1 / 2
        static const char* Modes[] = { "Sketch", "Insert", "Object" };
        Text   = Modes[std::clamp(int(State.Knife.Mode), 0, 2)];
        Number = double(int(State.Knife.Mode));
        return true;
    }
    if (Field == "knife-insert")
    {
        // The picked compound cutter or custom user insert's name, Number = its index
        const int Total = SketcherCompoundCutterCount + int(State.Knife.Library.Inserts.size());
        const bool Present = State.Knife.Insert >= 0 && State.Knife.Insert < Total;
        if (Present && State.Knife.Insert < SketcherCompoundCutterCount)
            Text = SketcherCompoundCutterLabel(static_cast<SketcherCompoundCutterCategory>(State.Knife.Insert));
        else if (Present)
            Text = State.Knife.Library.Inserts[size_t(State.Knife.Insert - SketcherCompoundCutterCount)].Name;
        else
            Text.clear();
        Number = Present ? double(State.Knife.Insert) : -1.0;
        return true;
    }
    if (Field == "knife-target")
    {
        // Caption of the body the staged knife cuts (empty: none yet)
        Text   = State.Knife.Target.empty() ? std::string{} : SketcherBodyCaption(Document, State.Knife.Target);
        Number = State.Knife.Target.empty() ? 0.0 : 1.0;
        return true;
    }
    if (Field == "knife-refusal")
    {
        Text   = State.Knife.Refusal;
        Number = State.Knife.Refusal.empty() ? 0.0 : 1.0;
        return true;
    }
    if (Field == "knife-live") return Numeric(State.Knife.LiveCondition ? 1 : 0);
    if (Field == "boolean-live") return Numeric(State.Boolean.LiveCondition ? 1 : 0);
    if (Field == "modifiers") return Numeric(double(Document.Tree.Modifiers.size()));
    if (Field == "edge-groups") return Numeric(double(Document.Tree.EdgeGroups.size()));
    if (Field == "live-cutters") return Numeric(double(Document.Appearance.LiveCutters.size()));
    if (Field == "variables")
    {
        int Count = 0;
        for (const GeometrySpace::Parametric::FeatureParameter& Param : Document.Tree.Parameters)
            if (SketcherUserVariableCondition(Param)) ++Count;
        return Numeric(double(Count));
    }
    if (Field == "fill") return Numeric(State.Fill.ActiveCondition ? 1 : 0);
    if (Field == "fill-commits") return Numeric(double(State.Fill.Commits));
    if (Field == "fill-loops") return Numeric(State.Fill.HoverCondition ? double(State.Fill.Outlines.size()) : 0.0);
    if (Field == "fill-hover")
    {
        // "Circle1" when the region is a sketch's own loop, "region" for a new cell, empty when nothing is under the pointer
        Text.clear();
        if (State.Fill.HoverCondition)
        {
            const auto Caption = Document.Captions.find(State.Fill.WholeSketch);
            Text = State.Fill.WholeSketch.empty() ? std::string("region") : Caption != Document.Captions.end() ? Caption->second : State.Fill.WholeSketch;
        }
        Number = State.Fill.HoverCondition ? 1.0 : 0.0;
        return true;
    }
    if (Field == "sketch-filled")
    {
        for (const GeometrySpace::Parametric::FeatureStep& Step : Document.Tree.Steps)
            if (Step.Label == State.FocusedStep && Step.Kind == GeometrySpace::Parametric::FeatureStepCategory::Sketch) return Numeric(Step.FillCondition ? 1 : 0);
        return Numeric(-1);
    }
    if (Field == "flips")
        return Numeric(double(std::count_if(Document.Tree.Steps.begin(), Document.Tree.Steps.end(), [](const GeometrySpace::Parametric::FeatureStep& Step) { return Step.Kind == GeometrySpace::Parametric::FeatureStepCategory::Flip; })));
    if (Field == "tint-loops")
    {
        size_t Loops = 0;
        for (const auto& [Label, Tint] : State.Tints) Loops += Tint.Outers.size();
        return Numeric(double(Loops));
    }
    if (Field == "trim") return Numeric(State.Trim.ActiveCondition ? 1 : 0);
    if (Field == "split") return Numeric(State.Trim.ActiveCondition && State.Trim.SplitCondition ? 1 : 0);
    if (Field == "trim-breaks") return Numeric(double(State.Trim.Breaks.size()));
    if (Field == "insert") return Numeric(State.Trim.ActiveCondition && State.Trim.InsertCondition ? 1 : 0);   // Point tool (V) open
    if (Field == "insert-point") return Numeric(double(State.Trim.InsertedPoint));                         // The point the hovered insert adds
    if (Field == "point-total")
    {
        double Total = 0.0;
        for (const auto& [Label, Sketch] : Document.Regeneration.Sketches) Total += double(Sketch.Points.size());
        return Numeric(Total);
    }
    if (Field == "inspector-tab") return Numeric(int(State.InspectorTab));                               // 0 geometry · 1 transform · 2 topology · 3 history
    if (Field == "flow-swap") return Numeric(State.FlowSwapCondition ? 1 : 0);
    if (Field == "trim-commits") return Numeric(double(State.Trim.Commits));
    if (Field == "trim-point")                                                               // Cut with Ctrl: the snapped point's source
    {
        Text   = State.Trim.PointCondition ? State.Trim.PointSource : std::string();
        Number = double(Text.size());
        return true;
    }
    if (Field == "trim-point-curves") return Numeric(State.Trim.PointCondition ? double(State.Trim.PointCurves.size()) : 0.0);
    if (Field == "trim-refusal")
    {
        Text   = State.Trim.Refusal;
        Number = double(Text.size());
        return true;
    }
    if (Field == "trim-hover")
    {
        Text.clear();
        if (State.Trim.HoverCondition)
        {
            const auto Caption = Document.Captions.find(State.Trim.Sketch);
            const auto Solved  = Document.Regeneration.Sketches.find(State.Trim.Sketch);
            Text = (Caption != Document.Captions.end() ? Caption->second : State.Trim.Sketch);
            if (Solved != Document.Regeneration.Sketches.end() && State.Trim.Curve >= 0 && State.Trim.Curve < int(Solved->second.Curves.size()))
                Text += " · " + GeometrySpace::Parametric::SketchCurveName(Solved->second.Curves[State.Trim.Curve]);
        }
        Number = double(Text.size());
        return true;
    }
    if (Field == "sketch-curve-total")
    {
        double Total = 0.0;
        for (const auto& [Label, Sketch] : Document.Regeneration.Sketches) Total += double(Sketch.Curves.size());
        return Numeric(Total);
    }
    if (Field == "pick-mode") return Numeric(int(State.PickMode));
    if (Field == "split-layout") return Numeric(int(State.SplitView.Layout));
    if (Field == "split-panes") return Numeric(SketcherViewportPaneCount(State.SplitView.Layout));
    if (Field == "active-pane") return Numeric(State.SplitView.ActivePane);
    if (Field == "split-x") return Numeric(double(State.SplitView.SplitX));
    if (Field == "split-y") return Numeric(double(State.SplitView.SplitY));
    if (Field == "transform-axis") return Numeric(State.Transform.ActiveCondition ? State.Transform.Axis : -2);
    if (Field == "transform-verb") return Numeric(State.Transform.ActiveCondition ? int(State.Transform.Verb) : -1);
    if (Field == "gizmo-visible-handles")
    {
        uint32_t Mask = 0;
        for (const GizmoVertex& V : State.Style.Gizmo.Vertices)
            if (V.Handle > 0 && V.Handle <= 13) Mask |= (1u << V.Handle);
        int Count = 0;
        for (int Bit = 1; Bit <= 13; ++Bit) if (Mask & (1u << Bit)) ++Count;
        return Numeric(double(Count));
    }
    if (Field == "surfaces") return Numeric(State.Style.SurfacesEnabled ? 1 : 0);
    if (Field == "xray") return Numeric(State.Style.XRayEnabled ? 1 : 0);
    if (Field == "gizmo-modes") return Numeric(double(State.Gizmo.Modes));                  // 1 move · 2 rotate · 4 scale (sums)
    if (Field == "gizmo-commits") return Numeric(double(State.Gizmo.Commits));
    if (Field == "gizmo-targets") return Numeric(double(State.Gizmo.Target == SketcherGizmoTarget::None ? 0 : State.Gizmo.Names.size()));
    if (Field == "gizmo-flat") return Numeric(State.Gizmo.FlatCondition ? 1 : 0);           // 1 in a 90° view (2D gizmo)
    if (Field == "gizmo-arrows")                                                            // Move arrows shown: 2 flat · 3 in 3D
    {
        int Shown = 0;
        for (int Axis = 0; Axis < 3; ++Axis) Shown += ((State.Gizmo.Pose.Hidden >> (uint32_t(GizmoHandle::TranslateX) + uint32_t(Axis))) & 1u) ? 0 : 1;
        return Numeric(State.Gizmo.Target == SketcherGizmoTarget::None ? 0.0 : double(Shown));
    }
    if (Field == "gizmo-target")
    {
        Text   = State.Gizmo.Target == SketcherGizmoTarget::Faces ? "faces" : State.Gizmo.Target == SketcherGizmoTarget::Bodies ? "bodies"
               : State.Gizmo.Target == SketcherGizmoTarget::Sketches ? "sketches"
               : (State.Gizmo.Target == SketcherGizmoTarget::Points || State.Gizmo.Target == SketcherGizmoTarget::Poles) ? "points"
               : State.Gizmo.Target == SketcherGizmoTarget::Shape ? (State.Gizmo.VertexCondition ? "vertices" : "edges") : "";
        Number = double(State.Gizmo.Target == SketcherGizmoTarget::Poles ? int(SketcherGizmoTarget::Points) : int(State.Gizmo.Target));
        return true;
    }
    if (Field == "measure")
    {
        // Text = subject (or the refusal), Number = 1 while Measure (I) is shown.
        const SketcherMeasurement& Measurement = State.Measure.Measurement;
        Text   = !State.Measure.ActiveCondition ? std::string("off") : Measurement.Refusal.empty() ? Measurement.Subject : Measurement.Refusal;
        Number = State.Measure.ActiveCondition ? 1.0 : 0.0;
        return true;
    }
    if (Field == "measure-distance") return Numeric(State.Measure.Measurement.Distance);
    if (Field == "measure-angle") return Numeric(State.Measure.Measurement.Angle);
    if (Field == "measure-area") return Numeric(State.Measure.Measurement.Area);
    if (Field == "measure-length") return Numeric(State.Measure.Measurement.Length);
    if (Field == "measure-volume") return Numeric(State.Measure.Measurement.Volume);
    if (Field == "measure-refreshes") return Numeric(State.Measure.Refreshes);
    if (Field == "measure-interference") { Text = State.Measure.Measurement.Interference; Number = double(Text.size()); return true; }
    if (Field == "measure-overlap") return Numeric(State.Measure.Measurement.OverlapVolume);
    if (Field == "measure-overlap-edges") return Numeric(double(State.Measure.Measurement.Overlap.size()));
    if (Field == "joint") return Numeric(State.Joint.ActiveCondition ? 1 : 0);
    if (Field == "joint-phase") return Numeric(State.Joint.ActiveCondition ? State.Joint.Phase : -1);
    if (Field == "joint-commits") return Numeric(double(State.Joint.Commits));
    if (Field == "joint-kind") { Text = GeometrySpace::Parametric::JointKindLabel(State.Joint.Request.Kind); Number = GeometrySpace::Parametric::JointFreedom(State.Joint.Request.Kind); return true; }
    if (Field == "joint-angle") return Numeric(State.Joint.Request.Angle);
    if (Field == "joint-flipped") return Numeric(State.Joint.Request.Flipped ? 1 : 0);
    if (Field == "joint-refusal") { Text = State.Joint.Refusal; Number = double(Text.size()); return true; }
    if (Field == "joint-preview") return Numeric(State.Joint.Preview ? 1 : 0);
    if (Field == "joint-hover")
    {
        // "circle centre on Link1" for the entity under the pointer (or its refusal), Number = 1 when it offers an origin.
        const SketcherJointOrigin& Hover = State.Joint.Hover;
        Text   = State.Joint.HoverName.empty() ? std::string() : Hover.Refusal.empty() ? Hover.Description + " on " + SketcherBodyCaption(Document, Hover.Body) : Hover.Refusal;
        Number = !State.Joint.HoverName.empty() && Hover.Refusal.empty() ? 1.0 : 0.0;
        return true;
    }
    if (Field == "joint-picks")
    {
        Text   = State.Joint.Moving.Description + (State.Joint.Moving.Body.empty() ? std::string() : " on " + SketcherBodyCaption(Document, State.Joint.Moving.Body)) + " -> "
               + State.Joint.Fixed.Description + (State.Joint.Fixed.Body.empty() ? std::string() : " on " + SketcherBodyCaption(Document, State.Joint.Fixed.Body));
        Number = double(State.Joint.Phase);
        return true;
    }
    if (Field == "joints" || Field == "joint-gap")
    {
        // Joints of the regeneration; joint-gap = the largest distance between a joint's two origins (0 when assembled).
        double Gap = 0.0;
        for (const GeometrySpace::Parametric::RegeneratedJoint& Joint : Document.Regeneration.Joints) Gap = std::max(Gap, GeometrySpace::Parametric::Distance(Joint.MovingOrigin.Origin, Joint.FixedOrigin.Origin));
        return Numeric(Field == "joints" ? double(Document.Regeneration.Joints.size()) : Gap);
    }
    if (Field == "parameter-drag") return Numeric(double(State.ParameterDrag.Stagings));
    if (Field.starts_with("bounds:"))
    {
        // "bounds:Block3" → "(x0, y0, z0) … (x1, y1, z1)" over that body's vertices at %.6f, Number = the x extent.
        const std::string Wanted = Field.substr(7);
        for (const GeometrySpace::Parametric::RegeneratedBody& Body : Document.Regeneration.Bodies)
        {
            if (Body.Key != Wanted && SketcherBodyCaption(Document, Body.Key) != Wanted) continue;
            GeometrySpace::Parametric::Vector3 Low = { 1.0e9, 1.0e9, 1.0e9 }, High = { -1.0e9, -1.0e9, -1.0e9 };
            for (const GeometrySpace::Parametric::VertexToken Vertex : Document.Partition->CollectInventory(Body.Body).Vertices)
            {
                const GeometrySpace::Parametric::Vector3 Point = Document.Partition->Vertices.Resolve(Vertex).Position;
                Low  = { std::min(Low.X, Point.X), std::min(Low.Y, Point.Y), std::min(Low.Z, Point.Z) };
                High = { std::max(High.X, Point.X), std::max(High.Y, Point.Y), std::max(High.Z, Point.Z) };
            }
            const auto Clean = [](double Value) { return std::abs(Value) < 5.0e-7 ? 0.0 : Value; };          // No "-0.000000"
            char Box[200];
            std::snprintf(Box, sizeof(Box), "(%.6f, %.6f, %.6f) … (%.6f, %.6f, %.6f)", Clean(Low.X), Clean(Low.Y), Clean(Low.Z), Clean(High.X), Clean(High.Y), Clean(High.Z));
            Text   = Box;
            Number = High.X - Low.X;
            return true;
        }
        Text = "no body " + Wanted;
        Number = 0.0;
        return true;
    }
    if (Field.starts_with("centroid:"))
    {
        // "centroid:Link2" → "(x, y, z)" at %.6f of that body (caption or key), Number = x.
        const std::string Wanted = Field.substr(9);
        for (const GeometrySpace::Parametric::RegeneratedBody& Body : Document.Regeneration.Bodies)
        {
            if (Body.Key != Wanted && SketcherBodyCaption(Document, Body.Key) != Wanted) continue;
            const GeometrySpace::Parametric::BodyMassProperties Mass = GeometrySpace::Parametric::MeasureBodyMass(*Document.Partition, Body.Body);
            const auto Clean = [](double Value) { return std::abs(Value) < 5.0e-7 ? 0.0 : Value; };          // No "-0.000000"
            char Triple[120];
            std::snprintf(Triple, sizeof(Triple), "(%.6f, %.6f, %.6f)", Clean(Mass.Centroid.X), Clean(Mass.Centroid.Y), Clean(Mass.Centroid.Z));
            Text   = Triple;
            Number = Mass.Centroid.X;
            return true;
        }
        Text = "no body " + Wanted;
        Number = 0.0;
        return true;
    }
    if (Field == "section")
    {
        // Text = request key ("y 1.25", "face F3 -0.5 flipped") or the refusal, Number = 1 while Section (Alt+S) is on.
        const SketcherSection& Section = State.Section.Section;
        Text   = !State.Section.ActiveCondition ? std::string("off") : Section.Refusal.empty() ? SketcherSectionKey(State.Section.Request) : Section.Refusal;
        Number = State.Section.ActiveCondition ? 1.0 : 0.0;
        return true;
    }
    if (Field == "section-clip") return Numeric(State.Style.SectionEnabled ? 1 : 0);
    if (Field == "drawing-sheet")
    {
        // Text = "A3 1:20 First" (paper, scale, angle) or the refusal, Number = 1 while the sheet (Alt+D) is shown.
        const SketcherDrawingSheet& Sheet = State.DrawingSheet.Sheet;
        Text   = !State.DrawingSheet.ActiveCondition ? std::string("off") : !Sheet.Refusal.empty() ? Sheet.Refusal
               : std::string(GeometrySpace::Parametric::DrawingPaperLabel(State.Document.DrawingSheet.Paper)) + " " + Sheet.Sheet.ScaleLabel + " " + SketcherProjectionAngleLabel(Sheet.Sheet.Angle);
        Number = State.DrawingSheet.ActiveCondition ? 1.0 : 0.0;
        return true;
    }
    if (Field == "drawing-sheet-scale")
    {
        Text   = State.DrawingSheet.Sheet.Sheet.ScaleLabel;
        Number = State.DrawingSheet.Sheet.Sheet.Scale;
        return true;
    }
    if (Field == "drawing-sheet-views")
    {
        // Text = view labels in order ("Front Top Right Isometric"), Number = view count.
        Text.clear();
        for (const GeometrySpace::Parametric::PlacedDrawingView& View : State.DrawingSheet.Sheet.Sheet.Views) Text += (Text.empty() ? "" : " ") + View.Label;
        Number = double(State.DrawingSheet.Sheet.Sheet.Views.size());
        return true;
    }
    if (Field == "drawing-sheet-dimensions")
    {
        // Text = every dimension text ("4000 2500 ⌀600 …"), Number = dimension count.
        Text.clear();
        for (const GeometrySpace::Parametric::DrawingDimension& Dimension : State.DrawingSheet.Sheet.Sheet.Dimensions) Text += (Text.empty() ? "" : " ") + Dimension.Text;
        Number = double(State.DrawingSheet.Sheet.Sheet.Dimensions.size());
        return true;
    }
    if (Field == "drawing-sheet-texts")
    {
        // Text = every sheet text (title block, labels, dimensions) joined by " | ", Number = text count.
        Text.clear();
        for (const GeometrySpace::Parametric::SheetText& Entry : State.DrawingSheet.Sheet.Sheet.Texts) Text += (Text.empty() ? "" : " | ") + Entry.Content;
        Number = double(State.DrawingSheet.Sheet.Sheet.Texts.size());
        return true;
    }
    if (Field == "drawing-sheet-lines") return Numeric(double(State.DrawingSheet.Sheet.Sheet.Lines.size()));
    if (Field == "drawing-sheet-section-area")
    {
        double Area = 0.0;                                                                   // [len²] - Section A-A cap (model units)
        for (const GeometrySpace::Parametric::PlacedDrawingView& View : State.DrawingSheet.Sheet.Sheet.Views) Area += View.View.SectionArea;
        return Numeric(Area);
    }
    if (Field == "drawing-sheet-refreshes") return Numeric(State.DrawingSheet.Refreshes);
    if (Field == "drawing-sheet-zoom") return Numeric(State.DrawingSheet.Zoom);
    if (Field == "drawing-sheet-saved") return Numeric(DefaultSketcherDrawingSheetCondition(State.Document.DrawingSheet) ? 0 : 1);
    if (Field == "drawing-sheet-export")
    {
        // Text = the last written path (or its refusal), Number = its size in bytes.
        Text   = State.DrawingSheet.ExportRefusal.empty() ? State.DrawingSheet.ExportPath : State.DrawingSheet.ExportRefusal;
        Number = State.DrawingSheet.ExportBytes;
        return true;
    }
    if (Field == "exchange-export")
    {
        // Text = the last written path (or its refusal), Number = its size in bytes.
        Text   = State.Exchange.Export.Refusal.empty() ? State.Exchange.Export.Path : State.Exchange.Export.Refusal;
        Number = State.Exchange.Export.Bytes;
        return true;
    }
    if (Field == "exchange-export-bodies")
    {
        // Text = the captions written, in order; Number = how many.
        for (const std::string& Key : State.Exchange.Export.Bodies) Text += (Text.empty() ? "" : " ") + SketcherBodyCaption(State.Document, Key);
        Number = double(State.Exchange.Export.Bodies.size());
        return true;
    }
    if (Field == "exchange-export-faces") return Numeric(State.Exchange.Export.Faces);
    if (Field == "exchange-export-facets") return Numeric(State.Exchange.Export.Triangles);
    if (Field == "exchange-exports") return Numeric(State.Exchange.Exports);
    if (Field == "exchange-import")
    {
        // Text = the last STEP file read (or its refusal), Number = the solids it added.
        Text   = State.Exchange.ImportRefusal.empty() ? State.Exchange.ImportPath : State.Exchange.ImportRefusal;
        Number = State.Exchange.ImportSolids;
        return true;
    }
    if (Field == "exchange-import-faces") return Numeric(State.Exchange.ImportFaces);
    if (Field == "exchange-import-repairs") return Numeric(State.Exchange.ImportRepairs);
    if (Field == "exchange-imports") return Numeric(State.Exchange.Imports);
    if (Field == "section-offset") return Numeric(State.Section.Request.Offset);
    if (Field == "section-area") return Numeric(State.Section.Section.Area);
    if (Field == "section-perimeter") return Numeric(State.Section.Section.Perimeter);
    if (Field == "section-regions") return Numeric(State.Section.Section.Regions);
    if (Field == "section-loops") return Numeric(State.Section.Section.Loops);
    if (Field == "section-bodies") return Numeric(State.Section.Section.Bodies);
    if (Field == "section-iuu") return Numeric(State.Section.Section.SecondMoments[0]);
    if (Field == "section-ivv") return Numeric(State.Section.Section.SecondMoments[1]);
    if (Field == "section-refreshes") return Numeric(State.Section.Refreshes);
    if (Field == "construct-open") return Numeric(State.ConstructShown ? 1 : 0);
    if (Field == "construct-combination") return Numeric(int(State.Drawing.ActiveCondition && State.Drawing.SolidCondition ? State.Drawing.Combination : State.ConstructForm.Combination));
    if (Field == "construct-section") return Numeric(State.ConstructSection);
    if (Field == "bevel-active") return Numeric(State.Bevel.ActiveCondition ? 1 : 0);
    if (Field == "bevel-size") return Numeric(State.Bevel.Size);
    if (Field == "bevel-axis") return Numeric(State.Bevel.AxisCondition ? 1 : 0);
    if (Field == "bevel-stages") return Numeric(State.Bevel.StageCount);
    if (Field == "bevel-pinned") return Numeric(State.Bevel.PinnedCondition ? 1 : 0);
    if (Field == "bevel-chamfer") return Numeric(State.Bevel.Shape == GeometrySpace::Parametric::BlendProfile::Chamfer ? 1 : 0);
    if (Field == "bevel-variable") return Numeric(State.Bevel.VariableEnabled ? 1 : 0);
    if (Field == "bevel-end-size") return Numeric(State.Bevel.EndSize);
    if (Field == "bevel-flipped") return Numeric(State.Bevel.ReversedCondition ? 1 : 0);
    if (Field == "bevel-preview-volume") return Numeric(State.Bevel.Preview ? State.Bevel.Preview->Summary.Volume : 0.0);
    if (Field == "bevel-refusal") { Text = State.Bevel.Refusal; Number = double(Text.size()); return true; }
    if (Field == "bevel-handles") return Numeric(double(State.Bevel.Handles.size()));
    if (Field == "bevel-corners")
    {
        int Count = 0;
        for (size_t Index = 1; Index < State.Bevel.CornerSizes.size(); ++Index) Count += State.Bevel.CornerSizes[Index] > 0.0 ? 1 : 0;
        return Numeric(Count);
    }
    if (Field.rfind("bevel-corner-", 0) == 0)
    {
        const size_t Index = size_t(std::atoi(Field.c_str() + 13));
        return Numeric(Index < State.Bevel.CornerSizes.size() && State.Bevel.CornerSizes[Index] > 0.0 ? State.Bevel.CornerSizes[Index] : State.Bevel.Size);
    }
    if (Field == "bevel-span") return Numeric(State.Bevel.Fit.Span);
    if (Field == "bevel-reach") return Numeric(State.Bevel.Shape == GeometrySpace::Parametric::BlendProfile::Chamfer ? State.Bevel.Fit.ChamferReach : State.Bevel.Fit.FilletReach);
    if (Field == "bevel-fitted") return Numeric(State.Bevel.Fit.Fitted);
    if (Field == "bevel-showing") return Numeric(State.Bevel.GoodCondition ? State.Bevel.GoodSize : 0.0);
    if (Field == "bevel-limit") return Numeric(State.Bevel.LimitCeiling > 0.0 ? State.Bevel.LimitGood : 0.0);
    if (Field == "bevel-ceiling") return Numeric(State.Bevel.LimitCeiling);
    if (Field == "bevel-limit-parameter") return Numeric(State.Bevel.LimitParameter);
    if (Field == "bevel-limit-stops") return Numeric(State.Bevel.LimitStops);
    if (Field == "bevel-merge-snaps") return Numeric(State.Bevel.MergeSnaps);
    if (Field == "bevel-merges") return Numeric(double((State.Bevel.Shape == Frontier::GeometrySpace::Parametric::BlendProfile::Chamfer ? State.Bevel.Fit.ChamferMerges : State.Bevel.Fit.FilletMerges).size()));
    if (Field == "bevel-merge-first")
    {
        const std::vector<double>& Merges = State.Bevel.Shape == Frontier::GeometrySpace::Parametric::BlendProfile::Chamfer ? State.Bevel.Fit.ChamferMerges : State.Bevel.Fit.FilletMerges;
        return Numeric(Merges.empty() ? 0.0 : Merges.front());
    }
    if (Field == "bevel-probe") return Numeric(State.Bevel.LimitProbeCondition ? 1 : 0);
    if (Field == "preview-faces") return Numeric(State.Bevel.Preview ? State.Bevel.Preview->Summary.Faces : 0);
    if (Field == "camera-yaw") return Numeric(State.Camera.Azimuth);
    if (Field == "camera-pitch") return Numeric(State.Camera.Elevation);
    if (Field == "camera-distance") return Numeric(State.Camera.Distance);
    if (Field == "camera-target-x") return Numeric(State.Camera.Target[0]);
    if (Field == "camera-target-z") return Numeric(State.Camera.Target[2]);
    if (Field == "orthographic") return Numeric(State.Camera.OrthographicEnabled ? 1 : 0);
    if (Field == "grid") return Numeric(State.Style.GridEnabled ? 1 : 0);
    if (Field == "grid-plane") return Numeric(State.Style.GridPlane);
    if (Field == "dirty") return Numeric(State.DirtyCondition ? 1 : 0);
    if (Field == "welcome") return Numeric(State.WelcomeOpen ? 1 : 0);
    if (Field == "focused-step") { Text = State.FocusedStep; Number = double(Text.size()); return true; }
    if (Field == "status") { Text = State.Status; Number = double(Text.size()); return true; }
    if (Field == "title") { Text = Document.Title; Number = double(Text.size()); return true; }
    if (Field == "document-path") { Text = State.DocumentPath; Number = double(Text.size()); return true; }
    if (Field == "hover")
    {
        Text   = State.HoverIdentity == IdentitySectionCap ? std::string("section cap")
               : State.HoverIdentity ? NameSketcherIdentity(DisplayedSketcherScene(State), State.HoverIdentity) : std::string();
        Number = double(State.HoverIdentity);
        return true;
    }
    if (Field == "drawing")
    {
        Text   = State.Drawing.ActiveCondition ? SketcherDrawingLabel(State.Drawing.Kind) : "";
        Number = State.Drawing.ActiveCondition ? 1.0 : 0.0;
        return true;
    }
    if (Field == "selected-spline-spans") return Numeric(double(State.SplineSpans.Indices.size()));
    if (Field == "selected-spline-poles") return Numeric(double(State.PoleDrag.PickedPoles.size()));
    if (Field == "last-control-count") return Numeric(State.Document.Tree.Steps.empty() ? 0 : double(State.Document.Tree.Steps.back().Points.size()));
    if (Field == "surface-trace") return Numeric(State.SurfaceTraceEnabled ? 1 : 0);
    if (Field == "full-xyz") return Numeric(State.FullXYZEnabled ? 1 : 0);
    if (Field == "drawing-surface") return Numeric(State.Drawing.SurfaceCondition ? 1 : 0);
    if (Field == "drawing-hit") return Numeric(State.Drawing.PointerCondition ? 1 : 0);
    if (Field == "drawing-last-z") return Numeric(State.Drawing.Lifted.empty() || !State.Drawing.Lifted.back() ? std::nan("") : State.Drawing.Lifted.back()->Z);
    if (Field == "drawing-points") return Numeric(double(State.Drawing.Points.size()));
    if (Field == "drawing-solid") return Numeric(State.Drawing.ActiveCondition && State.Drawing.SolidCondition ? 1 : 0);
    if (Field == "drawing-solid-kind") { Text = SketcherConstructLabel(State.Drawing.Solid); Number = double(int(State.Drawing.Solid)); return true; }
    if (Field == "drawing-lift") return Numeric(State.Drawing.Lift);
    if (Field == "drawing-elevation") return Numeric(State.Drawing.Elevation);
    if (Field == "drawing-sides") return Numeric(State.Drawing.Sides);
    if (Field == "drawing-form") { Text = GeometrySpace::Parametric::SketchSplineFormLabel(State.Drawing.Form); Number = double(int(State.Drawing.Form)); return true; }
    if (Field == "drawing-centre") return Numeric(State.Drawing.CentreCondition ? 1 : 0);
    if (Field == "drawing-plane") return Numeric(State.Drawing.GridPlane);
    if (Field == "drawing-refusal") { Text = State.Drawing.Refusal; Number = double(Text.size()); return true; }
    if (Field == "drawing-last-x") return Numeric(State.Drawing.Points.empty() ? std::nan("") : State.Drawing.Points.back().X);
    if (Field == "drawing-last-y") return Numeric(State.Drawing.Points.empty() ? std::nan("") : State.Drawing.Points.back().Y);
    if (Field == "drawing-pointer-x") return Numeric(State.Drawing.PointerCondition ? State.Drawing.Pointer.X : std::nan(""));
    if (Field == "drawing-pointer-y") return Numeric(State.Drawing.PointerCondition ? State.Drawing.Pointer.Y : std::nan(""));
    if (Field == "drawing-square") return Numeric(State.Drawing.SquareCondition ? 1 : 0);
    if (Field == "drawing-segment") return Numeric(State.Drawing.SegmentCondition ? 1 : 0);
    if (Field == "snap")
    {
        const SketcherSnap& Snap = State.Picker.Current;
        Text   = Snap.Kind == SketcherSnapCategory::None ? std::string("none") : std::string(SketcherSnapLabel(Snap.Kind)) + " · " + Snap.Source;
        Number = double(int(Snap.Kind));
        return true;
    }
    if (Field == "snap-x") return Numeric(State.Picker.Current.Position.X);
    if (Field == "snap-y") return Numeric(State.Picker.Current.Position.Y);
    if (Field == "snap-z") return Numeric(State.Picker.Current.Position.Z);
    if (Field == "snap-lock") { Text = State.Picker.Lock == SketcherSnapLockCategory::None ? std::string("none") : std::string(SketcherSnapLockLabel(State.Picker.Lock)); Number = double(int(State.Picker.Lock)); return true; }
    if (Field == "snap-enabled") return Numeric(State.Picker.Enabled ? 1 : 0);
    if (Field == "snap-grid") return Numeric(State.Picker.GridEnabled ? 1 : 0);
    if (Field == "ring-picks") return Numeric(State.RingPicks);
    if (Field == "path-picks") return Numeric(State.PathPicks);
    if (Field == "wrap-curves") return Numeric(State.WrapCurves);
    if (Field == "curve-radius" || Field == "curve-center-u" || Field == "curve-center-v")                                                            // First circle / arc of the first picked sketch
    {
        if (Document.Selection.Curves.empty()) return Numeric(0);
        const auto Solved = Document.Regeneration.Sketches.find(Document.Selection.Curves.front());
        if (Solved == Document.Regeneration.Sketches.end()) return Numeric(0);
        for (const GeometrySpace::Parametric::SketchCurve& Curve : Solved->second.Curves)
        {
            if (Field != "curve-radius" && (Curve.Kind == GeometrySpace::Parametric::SketchCurveCategory::Circle || Curve.Kind == GeometrySpace::Parametric::SketchCurveCategory::Arc))
            {
                const auto Centre = Solved->second.Points.at(size_t(Curve.Centre)).Position;
                return Numeric(Field == "curve-center-u" ? Centre.X : Centre.Y);
            }
            if (Curve.Kind == GeometrySpace::Parametric::SketchCurveCategory::Circle) return Numeric(Curve.Radius);
            if (Curve.Kind == GeometrySpace::Parametric::SketchCurveCategory::Arc)
                return Numeric(GeometrySpace::Parametric::Distance(Solved->second.Points[size_t(Curve.Centre)].Position, Solved->second.Points[size_t(Curve.Start)].Position));
        }
        return Numeric(0);
    }
    if (Field == "wrap-pending") return Numeric(State.WrapAxisPending ? 1 : 0);
    if (Field == "wrap-auto") { Text = State.WrapAutoNote; Number = State.WrapAutoNote.empty() ? 0.0 : 1.0; return true; }
    if (Field == "wrap-note") { Text = State.WrapNote; Number = State.WrapNote.empty() ? 0.0 : 1.0; return true; }
    if (Field == "path-edges") return Numeric(State.PathLast.Refusal.empty() ? int(State.PathLast.Edges.size()) : 0);
    if (Field == "path-length") return Numeric(State.PathLast.Refusal.empty() ? State.PathLast.Length : 0.0);
    if (Field == "path-mode") return Numeric(int(State.PathMode));
    if (Field == "path-refused") return Numeric(State.PathLast.Refusal.empty() ? 0 : 1);
    if (Field == "plane-normal-x" || Field == "plane-normal-y" || Field == "plane-normal-z")
    {
        // Plane normal of the newest sketch step (a drawn sketch, or the section of a Construct solid); 0 without one.
        const auto Newest = std::find_if(Document.Tree.Steps.rbegin(), Document.Tree.Steps.rend(), [](const GeometrySpace::Parametric::FeatureStep& Step) { return Step.Kind == GeometrySpace::Parametric::FeatureStepCategory::Sketch; });
        if (Newest == Document.Tree.Steps.rend()) return Numeric(0.0);
        const GeometrySpace::Parametric::Vector3 Normal = Newest->Plane.ZAxis;
        return Numeric(Field.back() == 'x' ? Normal.X : Field.back() == 'y' ? Normal.Y : Normal.Z);
    }
    if (Field == "plane-offset")
    {
        // Signed distance of the newest sketch plane from the world origin along its normal (n̂ · origin, elevation included).
        const auto Newest = std::find_if(Document.Tree.Steps.rbegin(), Document.Tree.Steps.rend(), [](const GeometrySpace::Parametric::FeatureStep& Step) { return Step.Kind == GeometrySpace::Parametric::FeatureStepCategory::Sketch; });
        if (Newest == Document.Tree.Steps.rend()) return Numeric(0.0);
        return Numeric(GeometrySpace::Parametric::Dot(Newest->Plane.Origin, Newest->Plane.ZAxis) + GeometrySpace::Parametric::EvaluateFeatureValue(Document.Tree, Newest->Elevation));
    }
    if (Field.rfind("scene-high-", 0) == 0 || Field.rfind("scene-low-", 0) == 0)
    {
        // World bounds of the displayed bodies (scene-high-x … scene-low-z).
        const int Axis = Field.back() == 'x' ? 0 : Field.back() == 'y' ? 1 : 2;
        return Numeric(Field[6] == 'h' ? State.Scene.BoundsHigh[Axis] : State.Scene.BoundsLow[Axis]);
    }
    if (Field == "face-orient") return Numeric(State.Picker.FaceOrientEnabled ? 1 : 0);
    if (Field == "drawing-on-face") return Numeric(State.Drawing.ActiveCondition && State.Drawing.FacePlaneCondition ? 1 : 0);
    if (Field == "drawing-normal-x" || Field == "drawing-normal-y" || Field == "drawing-normal-z")
    {
        // Normal of the open drawing's plane (the face it is oriented to, else the grid plane's).
        const GeometrySpace::Parametric::Vector3 Normal = State.Drawing.FacePlaneCondition ? State.Drawing.FacePlane.ZAxis : SketcherSketchPlane(State.Drawing.GridPlane).ZAxis;
        return Numeric(Field.back() == 'x' ? Normal.X : Field.back() == 'y' ? Normal.Y : Normal.Z);
    }
    if (Field == "pole-snap-gap") return Numeric(State.PoleDrag.ActiveCondition && State.Picker.Current.Kind != SketcherSnapCategory::None ? GeometrySpace::Parametric::Distance(State.PoleDrag.Position, State.Picker.Current.Position) : std::nan(""));
    if (Field == "pole-drag-x") return Numeric(State.PoleDrag.Position.X);
    if (Field == "pole-drag-y") return Numeric(State.PoleDrag.Position.Y);
    if (Field == "pole-drag-z") return Numeric(State.PoleDrag.Position.Z);
    if (Field == "snapped-points") return Numeric(double(State.Picker.Snapped));
    if (Field == "curves" || Field == "selected-curves")
    {
        std::vector<std::string> Captions;
        auto CaptionOf = [&](const std::string& Label)
        {
            const auto Caption = Document.Captions.find(Label);
            return Caption != Document.Captions.end() ? Caption->second : Label;
        };
        if (Field == "selected-curves") for (const std::string& Label : Document.Selection.Curves) Captions.push_back(CaptionOf(Label));
        else for (const auto& [Label, Fit] : Document.Regeneration.Curves) Captions.push_back(CaptionOf(Label));
        return Join(Captions);
    }
    if (Field == "sheet") return Numeric(Document.Summary.SheetCondition ? 1 : 0);
    if (Field == "panels") return Numeric(double(Document.Regeneration.Panels.size()));
    if (Field == "zebra") return Numeric(State.ZebraEnabled ? 1 : 0);
    if (Field == "image-planes") return Numeric(double(State.Document.ImagePlanes.size()));
    if (Field == "image-planes-drawn") return Numeric(double(State.Pictures.Drawn));
    if (Field == "image-focused") { Text = State.FocusedImage; Number = double(Text.size()); return true; }
    if (Field.rfind("image-", 0) == 0)
    {
        // The focused image plane's properties (the first plane when none is focused).
        const SketcherImagePlane* Plane = State.Document.ImagePlanes.empty() ? nullptr : &State.Document.ImagePlanes.front();
        for (const SketcherImagePlane& Each : State.Document.ImagePlanes) if (Each.Name == State.FocusedImage) Plane = &Each;
        if (!Plane) return Numeric(0.0);
        if (Field == "image-opacity") return Numeric(Plane->Opacity);
        if (Field == "image-photo") return Numeric(Plane->PhotoCondition ? 1 : 0);
        if (Field == "image-visible") return Numeric(Plane->VisibleCondition ? 1 : 0);
        if (Field == "image-locked") return Numeric(Plane->LockCondition ? 1 : 0);
        if (Field == "image-mirrored") return Numeric((Plane->FlipAcrossCondition ? 1 : 0) + (Plane->FlipDownCondition ? 2 : 0));
        if (Field == "image-width") return Numeric(GeometrySpace::Parametric::Length(Plane->Right));
        if (Field == "image-height") return Numeric(GeometrySpace::Parametric::Length(Plane->Down));
        if (Field == "image-turn") return Numeric(Plane->Turn);
        if (Field == "image-crop-left") return Numeric(Plane->Crop[0]);
        if (Field == "image-crop-right") return Numeric(Plane->Crop[2]);
        if (Field == "image-corner-x") return Numeric(Plane->Corner.X);
        if (Field == "image-centre-x") return Numeric((Plane->Corner + Plane->Right * 0.5 + Plane->Down * 0.5).X);
        if (Field == "image-centre-y") return Numeric((Plane->Corner + Plane->Right * 0.5 + Plane->Down * 0.5).Y);
        if (Field == "image-centre-z") return Numeric((Plane->Corner + Plane->Right * 0.5 + Plane->Down * 0.5).Z);
    }
    if (Field == "body-matcap") { Text = Document.Appearance.BodyMatcap; Number = double(Text.size()); return true; }
    if (Field == "face-matcaps") return Numeric(double(Document.Appearance.FaceMatcaps.size()));
    if (Field == "selection-matcap")
    {
        // The matcap every selected face (else body) shows ("mixed" when they differ), the default when nothing is selected.
        const auto FaceShows = [&](const std::string& Face)
        { return SketcherFaceMatcap(Document.Appearance, SketcherBodyOfName(Document, SketcherSelectionCategory::Face, Face), Face); };
        if (!Document.Selection.Faces.empty())
        {
            Text = FaceShows(Document.Selection.Faces.front());
            for (const std::string& Face : Document.Selection.Faces) if (FaceShows(Face) != Text) Text = "mixed";
        }
        else if (!Document.Selection.Bodies.empty())
        {
            Text = SketcherBodyMatcap(Document.Appearance, Document.Selection.Bodies.front());
            for (const std::string& Key : Document.Selection.Bodies) if (SketcherBodyMatcap(Document.Appearance, Key) != Text) Text = "mixed";
        }
        else Text = Document.Appearance.BodyMatcap;
        Number = double(Text.size());
        return true;
    }
    if (Field == "matcap-layers") return Numeric(double(State.Matcaps.Library.Names.size()));
    if (Field == "matcap-thumbnails") return Numeric(double(std::count_if(State.Matcaps.Thumbnails.begin(), State.Matcaps.Thumbnails.end(), [](const auto& Texture) { return Texture && Texture->Pixels; })));
    if (Field == "matcap-picker") return Numeric(State.Matcaps.PickerOpen ? 1 : 0);
    if (Field == "matcap-dangling") return Numeric(double(CountDanglingMatcaps(State.Scene, Document.Appearance)));
    if (Field == "triangles") return Numeric(double(State.Scene.Corners.size() / 3));
    if (Field == "deviation-worst" || Field == "deviation-points")
    {
        const SketcherDeviationPlot Map = MeasureSketcherDeviation(Document);
        return Numeric(Field == "deviation-worst" ? Map.Worst : double(Map.Needles.size()));
    }
    if (Field == "loft-continuity") return Numeric(State.LoftContinuity);
    if (Field == "panel-names")
    {
        std::vector<std::string> Names;
        for (const auto& [Name, Surface] : Document.Regeneration.Panels) Names.push_back(Name);
        return Join(Names);
    }
    if (Field == "pole-drags") return Numeric(State.PoleDrag.Commits);
    if (Field == "box-selections") return Numeric(State.Box.Commits);
    if (Field == "box-edges") return Numeric(State.Box.Edges);
    if (Field == "pole-drag-active") return Numeric(State.PoleDrag.ActiveCondition ? 1 : 0);
    if (Field == "pole-edits")
    {
        size_t Edits = 0;
        for (const GeometrySpace::Parametric::FeatureStep& Step : Document.Tree.Steps) Edits += Step.PoleEdits.size();
        return Numeric(double(Edits));
    }
    if (Field == "curve-fit-worst")
    {
        double Worst = 0.0;
        for (const auto& [Label, Fit] : Document.Regeneration.Curves) Worst = std::max(Worst, Fit.WorstDeviation);
        return Numeric(Worst);
    }
    if (Field == "sketches")
    {
        std::vector<std::string> Captions;
        for (const GeometrySpace::Parametric::FeatureStep& Step : Document.Tree.Steps)
        {
            if (Step.Kind != GeometrySpace::Parametric::FeatureStepCategory::Sketch) continue;
            if (Document.Appearance.RemovedCurves.contains(Step.Label)) continue;              // Deleted, kept for the steps that read it
            const auto Caption = Document.Captions.find(Step.Label);
            Captions.push_back(Caption != Document.Captions.end() ? Caption->second : Step.Label);
        }
        return Join(Captions);
    }
    if (Field == "sketch-curves")
    {
        const auto Solved = Document.Regeneration.Sketches.find(State.FocusedStep);
        return Numeric(Solved != Document.Regeneration.Sketches.end() ? double(Solved->second.Curves.size()) : 0.0);
    }
    if (Field == "focused-caption")
    {
        const auto Caption = Document.Captions.find(State.FocusedStep);
        Text   = Caption != Document.Captions.end() ? Caption->second : std::string();
        Number = double(Text.size());
        return true;
    }
    if (Field == "bevel-verb") return Numeric(int(State.Bevel.Verb));
    if (Field == "bevel-depth") return Numeric(State.Bevel.Depth);
    if (Field == "bevel-depth-pointer") return Numeric(State.Bevel.DepthEnabled ? 1 : 0);
    if (Field == "bevel-auto") return Numeric(State.Bevel.AutoCombinationCondition ? 1 : 0);
    if (Field == "bevel-material") return Numeric(State.Bevel.ProfileMaterial);
    if (Field == "bevel-combination")
    {
        Text   = State.Bevel.Combination == GeometrySpace::Parametric::FeatureCombination::New ? "New" : GeometrySpace::Parametric::FeatureCombinationLabel(State.Bevel.Combination);
        Number = double(int(State.Bevel.Combination));
        return true;
    }
    if (Field.rfind("file:", 0) == 0) return Numeric(std::filesystem::exists(Field.substr(5)) ? 1 : 0);
    return false;
}

SketcherExpectation Evaluate(const SketcherScriptStep& Step, const SketcherWorkspaceStatus& State)
{
    SketcherExpectation Result{ Step.Line, Step.Text, {}, false };
    const std::vector<std::string> Words = SplitWords(Step.Text);
    if (Words.size() < 3)
    {
        Result.Observed = "malformed expectation";
        return Result;
    }
    std::string Expected;
    for (size_t Index = 2; Index < Words.size(); ++Index) Expected += (Index > 2 ? " " : "") + Words[Index];
    double Number = 0.0;
    if (!ObserveField(State, Words[0], Result.Observed, Number))
    {
        Result.Observed = "unknown field " + Words[0];
        return Result;
    }
    const std::string& Operator = Words[1];
    if (Operator == "contains")
    {
        Result.Passed = Result.Observed.find(Expected) != std::string::npos;
        return Result;
    }
    if (Operator == "lacks")
    {
        Result.Passed = Result.Observed.find(Expected) == std::string::npos;
        return Result;
    }
    double Target = 0.0;
    try
    {
        Target = std::stod(Expected);
    }
    catch (...)
    {
        Result.Passed = Operator == "==" ? Result.Observed == Expected : Operator == "!=" ? Result.Observed != Expected : false;
        return Result;
    }
    if (Operator == "==") Result.Passed = Number == Target;
    else if (Operator == "!=") Result.Passed = Number != Target;
    else if (Operator == "<") Result.Passed = Number < Target;
    else if (Operator == ">") Result.Passed = Number > Target;
    else if (Operator == "~") Result.Passed = std::abs(Number - Target) <= 1.0e-6 * std::max(1.0, std::abs(Target));
    return Result;
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

SketcherScript ParseSketcherScript(const std::string& Text)
{
    SketcherScript     Script;
    std::istringstream Stream(Text);
    std::string        Line;
    int                LineNumber = 0;
    auto Push = [&](SketcherScriptStep Step)
    {
        Step.Line = LineNumber;
        Script.Steps.push_back(std::move(Step));
    };
    auto Refuse = [&](const std::string& Reason)
    {
        if (Script.Refusal.empty()) Script.Refusal = "line " + std::to_string(LineNumber) + ": " + Reason;
    };
    while (std::getline(Stream, Line))
    {
        LineNumber += 1;
        // Comments are whole lines starting with '#' (names such as Extrude#2 contain the character).
        const size_t First = Line.find_first_not_of(" \t");
        if (First == std::string::npos || Line[First] == '#') continue;
        const std::vector<std::string> Words = SplitWords(Line);
        if (Words.empty()) continue;
        const std::string& Verb = Words[0];
        size_t             Index = 1;
        if (Verb == "frames")
        {
            const int Count = Words.size() > 1 ? std::max(1, std::stoi(Words[1])) : 1;
            for (int Frame = 0; Frame < Count; ++Frame) Push({});
        }
        else if (Verb == "move")
        {
            // move TARGET (remembered as the anchor) · move offset DX DY (pixels from the last anchor, e.g. mid-drag)
            SketcherScriptStep Step{ SketcherStepCategory::Move };
            if (Words.size() == 4 && Words[1] == "offset")
            {
                Step.Anchored       = true;
                Step.Target.OffsetX = std::stof(Words[2]);
                Step.Target.OffsetY = std::stof(Words[3]);
                Push(Step);
                Push({});
                continue;
            }
            Step.Anchoring = true;
            if (!ReadTarget(Words, Index, Step.Target)) { Refuse("move target"); continue; }
            ReadModifiers(Words, Index, Step.Control, Step.Shift, Step.Alternate);   // move … Ctrl: hover with Ctrl held
            Push(Step);
            Push({});
        }
        else if (Verb == "click" || Verb == "press" || Verb == "release")
        {
            SketcherScriptStep Move{ SketcherStepCategory::Move };
            if (Verb == "click" && !ReadTarget(Words, Index, Move.Target)) { Refuse("click target"); continue; }
            SketcherScriptStep Down{ SketcherStepCategory::Button };
            Down.Button = Index < Words.size() ? ButtonByName(Words[Index]) : ImGuiMouseButton_Left;
            ReadModifiers(Words, Index, Down.Control, Down.Shift, Down.Alternate);
            Down.Down = true;
            SketcherScriptStep Up = Down;
            Up.Down = false;
            if (Verb == "click") { Push(Move); Push({}); }
            if (Verb != "release") Push(Down);
            if (Verb != "press") Push(Up);
            Push({});
        }
        else if (Verb == "drag")
        {
            // drag BUTTON FROM by DX DY STEPS [Ctrl] [Shift]
            if (Words.size() < 2) { Refuse("drag button"); continue; }
            const int Button = ButtonByName(Words[1]);
            Index = 2;
            SketcherScriptTarget From;
            if (!ReadTarget(Words, Index, From) || Index + 3 >= Words.size() || Words[Index] != "by") { Refuse("drag FROM by DX DY STEPS"); continue; }
            const float TravelX = std::stof(Words[Index + 1]);
            const float TravelY = std::stof(Words[Index + 2]);
            const int   Count   = std::max(1, std::stoi(Words[Index + 3]));
            bool Control = false, Shift = false, Alternate = false;
            ReadModifiers(Words, Index + 4, Control, Shift, Alternate);
            SketcherScriptStep Start{ SketcherStepCategory::Move };
            Start.Target    = From;
            Start.Anchoring = true;
            Push(Start);
            Push({});
            SketcherScriptStep Down{ SketcherStepCategory::Button };
            Down.Button  = Button;
            Down.Down    = true;
            Down.Control   = Control;
            Down.Shift     = Shift;
            Down.Alternate = Alternate;
            Push(Down);
            for (int Frame = 1; Frame <= Count; ++Frame)
            {
                SketcherScriptStep Along{ SketcherStepCategory::Move };
                Along.Anchored       = true;
                Along.Target.OffsetX = float(Frame) / float(Count) * TravelX;
                Along.Target.OffsetY = float(Frame) / float(Count) * TravelY;
                Along.Control         = Control;
                Along.Shift           = Shift;
                Along.Alternate       = Alternate;
                Push(Along);
            }
            SketcherScriptStep Up = Down;
            Up.Down = false;
            Push(Up);
            Push({});
        }
        else if (Verb == "wheel")
        {
            SketcherScriptStep Move{ SketcherStepCategory::Move };
            if (!ReadTarget(Words, Index, Move.Target) || Index >= Words.size()) { Refuse("wheel target"); continue; }
            Push(Move);
            Push({});
            SketcherScriptStep Wheel{ SketcherStepCategory::Wheel };
            Wheel.Wheel = std::stof(Words[Index]);
            Push(Wheel);
            Push({});
        }
        else if (Verb == "key")
        {
            if (Words.size() < 2 || KeyByName(Words[1]) == ImGuiKey_None) { Refuse("unknown key"); continue; }
            SketcherScriptStep Down{ SketcherStepCategory::Key };
            Down.Key  = KeyByName(Words[1]);
            Down.Down = true;
            ReadModifiers(Words, 2, Down.Control, Down.Shift, Down.Alternate);
            SketcherScriptStep Up = Down;
            Up.Down = false;
            Push(Down);
            Push(Up);
            Push({});
        }
        else if (Verb == "type")
        {
            SketcherScriptStep Typed{ SketcherStepCategory::Text };
            Typed.Text = Line.substr(Line.find("type") + 4);
            Typed.Text.erase(0, Typed.Text.find_first_not_of(' '));
            Typed.Text.erase(Typed.Text.find_last_not_of(" \r") + 1);
            Push(Typed);
            Push({});
        }
        else if (Verb == "capture")
        {
            if (Words.size() < 2) { Refuse("capture name"); continue; }
            Push({});
            SketcherScriptStep Capture{ SketcherStepCategory::Capture };
            Capture.Text = Words[1];
            Push(Capture);
        }
        else if (Verb == "snap")
        {
            // snap grid on | off: the 0.1 drawing grid (off by default: free placement, Ctrl snaps to objects).
            if (Words.size() < 3 || Words[1] != "grid" || (Words[2] != "on" && Words[2] != "off")) { Refuse("snap grid on | off"); continue; }
            SketcherScriptStep Setting{ SketcherStepCategory::Setting };
            Setting.Text = "grid " + Words[2];
            Push(Setting);
        }
        else if (Verb == "expect")
        {
            SketcherScriptStep Expect{ SketcherStepCategory::Expect };
            Expect.Text = Line.substr(Line.find("expect") + 6);
            Expect.Text.erase(0, Expect.Text.find_first_not_of(' '));
            Expect.Text.erase(Expect.Text.find_last_not_of(" \r") + 1);
            Push(Expect);
        }
        else
        {
            Refuse("unknown command " + Verb);
        }
    }
    return Script;
}

std::string AdvanceSketcherScript(SketcherScript& Script, SketcherWorkspaceStatus& State)
{
    ImGuiIO& Io = ImGui::GetIO();
    // Expectations run back to back without consuming frames.
    while (Script.Cursor < Script.Steps.size() && Script.Steps[Script.Cursor].Kind == SketcherStepCategory::Expect)
        Script.Expectations.push_back(Evaluate(Script.Steps[Script.Cursor++], State));
    if (Script.Cursor >= Script.Steps.size()) return {};
    const SketcherScriptStep& Step = Script.Steps[Script.Cursor++];
    switch (Step.Kind)
    {
    case SketcherStepCategory::Idle:
        break;
    case SketcherStepCategory::Move:
    {
        ImVec2 Point;
        if (Step.Anchored)
        {
            Io.AddMousePosEvent(Script.Anchor.x + Step.Target.OffsetX, Script.Anchor.y + Step.Target.OffsetY);
        }
        else if (!Step.Target.Widget.empty() && State.Widgets.ClippedCondition(Step.Target.Widget) && Script.RevealFrames < RevealPatience)
        {
            // Scrolled out of its panel: ask it to scroll into view and retry this step next frame.
            State.Widgets.RequestReveal(Step.Target.Widget);
            ++Script.RevealFrames;
            --Script.Cursor;
        }
        else if (!Step.Target.Widget.empty() && State.Widgets.Find(Step.Target.Widget) && !State.Widgets.SettledCondition(Step.Target.Widget) &&
                 Script.RevealFrames < RevealPatience)
        {
            // Still moving (a scroll clamp after the panel shrank): wait for it to sit still.
            ++Script.RevealFrames;
            --Script.Cursor;
        }
        else if (ResolveTarget(Step.Target, State, Point))
        {
            Script.RevealFrames = 0;
            if (Step.Anchoring) Script.Anchor = Point;
            Io.AddKeyEvent(ImGuiMod_Ctrl, Step.Control);                                  // held for this move (drags carry theirs)
            Io.AddKeyEvent(ImGuiMod_Shift, Step.Shift);
            Io.AddKeyEvent(ImGuiMod_Alt, Step.Alternate);
            Io.AddMousePosEvent(Point.x, Point.y);
        }
        else Script.Expectations.push_back({ Step.Line, "target resolves", "unresolved target " + Step.Target.Widget, false });
        break;
    }
    case SketcherStepCategory::Button:
        if (Step.Down)
        {
            Io.AddKeyEvent(ImGuiMod_Ctrl, Step.Control);
            Io.AddKeyEvent(ImGuiMod_Shift, Step.Shift);
            Io.AddKeyEvent(ImGuiMod_Alt, Step.Alternate);
        }
        Io.AddMouseButtonEvent(Step.Button, Step.Down);
        if (!Step.Down)
        {
            Io.AddKeyEvent(ImGuiMod_Ctrl, false);
            Io.AddKeyEvent(ImGuiMod_Shift, false);
            Io.AddKeyEvent(ImGuiMod_Alt, false);
        }
        break;
    case SketcherStepCategory::Key:
        if (Step.Down)
        {
            Io.AddKeyEvent(ImGuiMod_Ctrl, Step.Control);
            Io.AddKeyEvent(ImGuiMod_Shift, Step.Shift);
            Io.AddKeyEvent(ImGuiMod_Alt, Step.Alternate);
            Io.AddKeyEvent(Step.Key, true);
        }
        else
        {
            Io.AddKeyEvent(Step.Key, false);
            Io.AddKeyEvent(ImGuiMod_Ctrl, false);
            Io.AddKeyEvent(ImGuiMod_Shift, false);
            Io.AddKeyEvent(ImGuiMod_Alt, false);
        }
        break;
    case SketcherStepCategory::Text:
        Io.AddInputCharactersUTF8(Step.Text.c_str());
        break;
    case SketcherStepCategory::Wheel:
        Io.AddMouseWheelEvent(0.0f, Step.Wheel);
        break;
    case SketcherStepCategory::Capture:
        return Step.Text;
    case SketcherStepCategory::Setting:
        if (Step.Text == "grid on") State.Picker.GridEnabled = true;
        if (Step.Text == "grid off") State.Picker.GridEnabled = false;
        break;
    case SketcherStepCategory::Expect:
        break;
    }
    return {};
}

bool FinishedSketcherScript(const SketcherScript& Script)
{
    return Script.Cursor >= Script.Steps.size();
}

} // namespace Frontier::Workspaces::Sketcher
