/*====================================================================================================================================
                                                          SOLIDARCKNIFE.CPP
====================================================================================================================================*/
// 🧩 Knife insert library: procedural built-ins and library STEP files normalized to the insert frame

//------------------------------------------------------------------------------------------------------------------------
//                                                    INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Workspaces/ParametricSketcher/Document/SketcherKnife.h"

#include "Engine/Workspaces/ParametricSketcher/Document/SketcherDrawingSheet.h"
#include "Engine/Workspaces/ParametricSketcher/Document/SketcherExchange.h"
#include "Engine/Workspaces/ParametricSketcher/Document/SketcherScene.h"

#include "Engine/GeometrySpace/Parametric/Exchange/StepExchange.h"
#include "Engine/GeometrySpace/Parametric/Tessellation/BodyTessellation.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <limits>
#include <numbers>

namespace Frontier::Workspaces::Sketcher
{

using namespace GeometrySpace::Parametric;

//------------------------------------------------------------------------------------------------------------------------
//                                                    INTERNAL
//------------------------------------------------------------------------------------------------------------------------

namespace
{

[[nodiscard]] double ElapsedMilliseconds(std::chrono::steady_clock::time_point Start)
{
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - Start).count();
}

[[nodiscard]] const RegeneratedBody* FindBody(const SketcherDocument& Document, const std::string& Key)
{
    for (const RegeneratedBody& Body : Document.Regeneration.Bodies) if (Body.Key == Key) return &Body;
    return nullptr;
}

// Scene span of body Key (nullptr when the scene does not show it).
[[nodiscard]] const SketcherSceneBody* FindSceneBody(const SketcherScene& Scene, const std::string& Key)
{
    for (const SketcherSceneBody& Body : Scene.Bodies) if (Body.Key == Key) return &Body;
    return nullptr;
}

// 📝 ① box of the body's facets (kernel tessellation, doubles) ② Transform step: unit width, centred, top at +Lift
//    ③ canonical STEP text of the result ④ its edge polylines. Refusal (empty on success) when the body is missing, flat or the writer refuses it.
//    OriginCondition keeps the authored origin as the centre (a built-in's own pivot: a triangle turns about its
//    circumcentre, not the middle of its box); a library file is centred on its box.
[[nodiscard]] std::string NormalizeSketcherInsert(SketcherDocument& Scratch, const std::string& Key, SketcherInsert& Insert, bool OriginCondition = false)
{
    const RegeneratedBody* Source = FindBody(Scratch, Key);
    if (!Source || !Source->Body.PresentCondition() || !Scratch.Partition) return "the cutter body was not made";
    const TessellationOutcome Facets = TessellateBody(*Scratch.Partition, Source->Body, TessellationSettings{});   // Double positions: planar cutters stay exact
    if (!Facets.Refusal.empty() || Facets.Complex.Positions.empty()) return Facets.Refusal.empty() ? std::string("the cutter body has no facets") : Facets.Refusal;
    double Low[3]  = { std::numeric_limits<double>::max(), std::numeric_limits<double>::max(), std::numeric_limits<double>::max() };
    double High[3] = { -std::numeric_limits<double>::max(), -std::numeric_limits<double>::max(), -std::numeric_limits<double>::max() };
    for (const Vector3& Point : Facets.Complex.Positions)
    {
        const double Axis[3] = { Point.X, Point.Y, Point.Z };
        for (int Index = 0; Index < 3; ++Index)
        {
            Low[Index]  = std::min(Low[Index], Axis[Index]);
            High[Index] = std::max(High[Index], Axis[Index]);
        }
    }
    const double Width = std::max(High[0] - Low[0], High[1] - Low[1]);                      // [len]
    if (!(Width > 1.0e-9) || !(High[2] - Low[2] > 1.0e-9)) return "the cutter body is flat";
    const double Factor = 1.0 / Width;
    AffineMap Map;
    Map.ColumnX = { Factor, 0.0, 0.0 };
    Map.ColumnY = { 0.0, Factor, 0.0 };
    Map.ColumnZ = { 0.0, 0.0, Factor };
    Map.Shift   = OriginCondition ? Vector3{ 0.0, 0.0, SketcherInsertLift - High[2] * Factor }
                                  : Vector3{ -0.5 * (Low[0] + High[0]) * Factor, -0.5 * (Low[1] + High[1]) * Factor, SketcherInsertLift - High[2] * Factor };
    const SketcherCommandOutcome Placed = TransformSketcherBodies(Scratch, { Key }, Map, "Normalize");
    if (!Placed.AcceptedCondition) return Placed.Refusal;
    const RegeneratedBody* Body = FindBody(Scratch, Key);
    if (!Body && Scratch.Regeneration.Bodies.size() == 1) Body = &Scratch.Regeneration.Bodies.front();
    if (!Body || !Body->Body.PresentCondition() || !Scratch.Partition) return "the normalized cutter is missing";
    StepRequest Request;
    Request.ModelUnitMillimetres = SketcherModelUnitMillimetres;
    Request.Name                 = Insert.Name;
    const StepExport Export = WriteStep(*Scratch.Partition, { Body->Body }, Request);
    if (!Export.Refusal.empty()) return Export.Refusal;
    Insert.Exchange = Export.Bytes;
    Insert.Depth    = (High[2] - Low[2]) * Factor - SketcherInsertLift;
    Insert.Width    = Width;
    Insert.Top      = High[2];
    const SketcherScene      After = BuildSketcherScene(Scratch);
    const SketcherSceneBody* Shown = FindSceneBody(After, Body->Key);
    if (!Shown) return {};
    for (uint32_t Edge = Shown->FirstEdge; Edge < Shown->FirstEdge + Shown->EdgeCount; ++Edge)
    {
        const SketcherSceneEdge& Each = After.Edges[Edge];
        for (uint32_t Segment = Each.FirstSegment; Segment < Each.FirstSegment + Each.SegmentCount; ++Segment)
        {
            const SketcherSceneSegment& Piece = After.Segments[Segment];
            Insert.Segments.push_back({ Vector3{ Piece.Start[0], Piece.Start[1], Piece.Start[2] }, Vector3{ Piece.End[0], Piece.End[1], Piece.End[2] } });
        }
    }
    // 📝 Drag grip: the outline point farthest from the axis; ties (a square's corners, a circle) go to the least angle
    //    in [0, 2π), so a triangle grips its vertex at 90°, a hex its vertex at 0° and a square its corner at 45°.
    double Reach = 0.0, Bearing = 0.0;
    for (const std::array<Vector3, 2>& Segment : Insert.Segments)
        for (const Vector3& Point : Segment)
        {
            const double Radius = std::hypot(Point.X, Point.Y);
            double       Angle  = std::atan2(Point.Y, Point.X);
            if (Angle < -1.0e-9) Angle += 2.0 * std::numbers::pi;
            if (Radius > Reach + 1.0e-6 || (Radius > Reach - 1.0e-6 && Angle < Bearing - 1.0e-9)) { Reach = std::max(Reach, Radius); Bearing = Angle; Insert.Anchor = { Point.X, Point.Y }; }
        }
    return {};
}

// A filled closed drawing on the plane z = Top swept down to z = Bottom (a first body: New). Key receives the body.
[[nodiscard]] std::string SweepFlatCutter(SketcherDocument& Scratch, SketcherDrawingRequest Request, double Top, double Bottom, std::string& Key)
{
    Request.PlaneCondition = true;
    Request.Plane          = OrthonormalFrame{};
    Request.Plane.Origin   = { 0.0, 0.0, Top };
    Request.FillCondition  = true;
    const SketcherCommandOutcome Drawn = DrawSketcherSketch(Scratch, Request);
    if (!Drawn.AcceptedCondition) return Drawn.Refusal;
    const std::string Sketch = Scratch.Tree.Steps.back().Label;
    const SketcherCommandOutcome Swept = ExtrudeSketcherSketch(Scratch, Sketch, Bottom - Top, FeatureCombination::New);
    if (!Swept.AcceptedCondition) return Swept.Refusal;
    Key = Scratch.Tree.Steps.back().Label;
    return {};
}

// 📝 Countersunk hole: the half profile on the upright plane (sketch x → x, sketch y → z) turned a full turn about z.
//    Its cone ends at z = −0.25 (CountersinkCone); the bore goes on down to Bottom.
constexpr double CountersinkCone = -0.25;                                                    // [len] - Cone tip, template

[[nodiscard]] std::string TurnCountersink(SketcherDocument& Scratch, std::string& Key, double Bottom)
{
    SketcherDrawingRequest Request;
    Request.Kind            = SketcherDrawingKind::Line;
    Request.GridPlane       = 1;
    Request.ClosedCondition = true;
    Request.FillCondition   = true;
    Request.Points          = { { 0.0, 0.05 }, { 0.55, 0.05 }, { 0.25, CountersinkCone }, { 0.25, Bottom }, { 0.0, Bottom } };
    const SketcherCommandOutcome Drawn = DrawSketcherSketch(Scratch, Request);
    if (!Drawn.AcceptedCondition) return Drawn.Refusal;
    const std::string Sketch = Scratch.Tree.Steps.back().Label;
    SketcherRevolveAxis Axis;
    Axis.Origin    = { 0.0, 0.0 };
    Axis.Direction = { 0.0, 1.0 };
    const SketcherCommandOutcome Turned = RevolveSketcherSketch(Scratch, Sketch, Axis, 2.0 * std::numbers::pi);
    if (!Turned.AcceptedCondition) return Turned.Refusal;
    Key = Scratch.Tree.Steps.back().Label;
    return {};
}

// One built-in: authored by Author in a scratch document at Bottom, then normalized. Floor is the highest bottom the
// shape takes (a countersink's cone tip): Shallow is that floor in insert widths.
void AddBuiltIn(SketcherInsertLibrary& Library, const std::string& Name, SketcherInsertAuthor Author, double Bottom, double Floor)
{
    SketcherDocument Scratch = CreateSketcherDocument("Insert " + Name);
    SketcherInsert   Insert;
    Insert.Name = Name;
    std::string Key;
    std::string Refusal = Author(Scratch, Key, Bottom);
    if (Refusal.empty()) Refusal = NormalizeSketcherInsert(Scratch, Key, Insert, true);
    if (!Refusal.empty()) { Library.Notes.push_back(Name + ": " + Refusal); return; }
    Insert.Author  = std::move(Author);
    Insert.Shallow = std::max(0.0, (Insert.Top - Floor) / Insert.Width - SketcherInsertLift + 0.02);   // 0.02 widths of bore at least
    Library.Inserts.push_back(std::move(Insert));
}

[[nodiscard]] SketcherDrawingRequest Shape(SketcherDrawingKind Kind, std::vector<Vector2> Points, bool CentreCondition = false, int Sides = 6, bool ClosedCondition = false)
{
    SketcherDrawingRequest Request;
    Request.Kind            = Kind;
    Request.Points          = std::move(Points);
    Request.CentreCondition = CentreCondition;
    Request.Sides           = Sides;
    Request.ClosedCondition = ClosedCondition;
    return Request;
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

SketcherInsertLibrary AssembleSketcherInserts(const std::string& Folder)
{
    const auto Start = std::chrono::steady_clock::now();
    SketcherInsertLibrary Library;
    Library.Folder = Folder;
    const auto Flat = [](SketcherDrawingRequest Request) -> SketcherInsertAuthor
    {
        return [Request](SketcherDocument& Scratch, std::string& Key, double Bottom) { return SweepFlatCutter(Scratch, Request, 0.05, Bottom, Key); };
    };
    AddBuiltIn(Library, "Square pocket", Flat(Shape(SketcherDrawingKind::Rectangle, { { 0.0, 0.0 }, { 0.5, 0.5 } }, true)), -0.3, 0.05);
    AddBuiltIn(Library, "Round hole", Flat(Shape(SketcherDrawingKind::Circle, { { 0.0, 0.0 }, { 0.5, 0.0 } }, true)), -0.5, 0.05);
    AddBuiltIn(Library, "Countersink", TurnCountersink, -0.8, CountersinkCone);
    AddBuiltIn(Library, "Slot", Flat(Shape(SketcherDrawingKind::Slot, { { -0.35, 0.0 }, { 0.35, 0.0 }, { 0.35, 0.15 } })), -0.3, 0.05);
    AddBuiltIn(Library, "Hex pocket", Flat(Shape(SketcherDrawingKind::Polygon, { { 0.0, 0.0 }, { 0.5, 0.0 } }, true, 6)), -0.3, 0.05);
    AddBuiltIn(Library, "Triangle", Flat(Shape(SketcherDrawingKind::Polygon, { { 0.0, 0.0 }, { 0.0, 0.5 } }, true, 3)), -0.3, 0.05);
    AddBuiltIn(Library, "Cross", Flat(Shape(SketcherDrawingKind::Line, { { -0.15, -0.5 }, { 0.15, -0.5 }, { 0.15, -0.15 }, { 0.5, -0.15 }, { 0.5, 0.15 }, { 0.15, 0.15 },
                                                                           { 0.15, 0.5 }, { -0.15, 0.5 }, { -0.15, 0.15 }, { -0.5, 0.15 }, { -0.5, -0.15 }, { -0.15, -0.15 } },
                                                  false, 6, true)), -0.3, 0.05);
    std::error_code Error;
    std::vector<std::filesystem::path> Files;
    if (!Folder.empty() && std::filesystem::is_directory(Folder, Error))
        for (const std::filesystem::directory_entry& Entry : std::filesystem::directory_iterator(Folder, Error))
        {
            std::string Extension = Entry.path().extension().string();
            std::transform(Extension.begin(), Extension.end(), Extension.begin(), [](unsigned char Letter) { return char(std::tolower(Letter)); });
            if (Entry.is_regular_file(Error) && (Extension == ".step" || Extension == ".stp")) Files.push_back(Entry.path());
        }
    std::sort(Files.begin(), Files.end());
    for (const std::filesystem::path& File : Files)
    {
        const SketcherStepReading Reading = ReadSketcherStepFile(File.string());
        const std::string Name = File.stem().string();
        if (!Reading.Refusal.empty() || Reading.Texts.empty()) { Library.Notes.push_back(Name + ": " + (Reading.Refusal.empty() ? std::string("no solid") : Reading.Refusal)); continue; }
        SketcherStepReading First = Reading;
        First.Texts.resize(1);
        SketcherDocument Scratch = CreateSketcherDocument("Insert " + Name);
        const SketcherCommandOutcome Imported = ImportSketcherStep(Scratch, First);
        SketcherInsert Insert;
        Insert.Name = Name;
        Insert.Path = File.string();
        const std::string Refusal = Imported.AcceptedCondition ? NormalizeSketcherInsert(Scratch, Scratch.Tree.Steps.back().Label, Insert) : Imported.Refusal;
        if (!Refusal.empty()) { Library.Notes.push_back(Name + ": " + Refusal); continue; }
        Library.Inserts.push_back(std::move(Insert));
    }
    Library.Milliseconds = ElapsedMilliseconds(Start);
    return Library;
}

AffineMap PlaceSketcherInsert(const OrthonormalFrame& Spot, double Size, double Turn, bool RaisedCondition, double Stretch, double Aspect)
{
    const Vector3 Across = Spot.XAxis * std::cos(Turn) + Spot.YAxis * std::sin(Turn);
    const Vector3 Along  = Spot.YAxis * std::cos(Turn) - Spot.XAxis * std::sin(Turn);
    const double  Flip   = RaisedCondition ? -1.0 : 1.0;                                     // Half turn about x̂: det stays +1
    AffineMap Map;
    Map.ColumnX = Across * Size;
    Map.ColumnY = Along * (Size * Aspect * Flip);
    Map.ColumnZ = Spot.ZAxis * (Size * Flip * Stretch);
    Map.Shift   = Spot.Origin;
    return Map;
}

SketcherInsertReach ReachSketcherInsert(const SketcherInsert& Insert, double Depth)
{
    SketcherInsertReach Reach;
    if (!(Depth > 1.0e-9) || !std::isfinite(Depth)) { Reach.Refusal = "Knife: the insert needs a depth"; return Reach; }
    if (std::abs(Depth - Insert.Depth) <= 1.0e-9 * std::max(1.0, Insert.Depth)) { Reach.Exchange = Insert.Exchange; return Reach; }
    if (!Insert.Author)
    {
        // A library file: the placement stretches it along z (its lift with it).
        Reach.Exchange = Insert.Exchange;
        Reach.Stretch  = Depth / std::max(1.0e-12, Insert.Depth);
        return Reach;
    }
    if (Depth <= Insert.Shallow + 1.0e-6)
    {
        Reach.Refusal = "Knife: " + Insert.Name + " is too shallow there (it needs more than " + std::to_string(Insert.Shallow) + " widths)";
        return Reach;
    }
    // 📝 Normalized z = (z − Top)·(1 / Width) + Lift, so the floor at −Depth is the template bottom Top − (Depth + Lift)·Width.
    const long long Token = std::llround(Depth * 1.0e9);
    std::lock_guard<std::mutex> Lock(Insert.Remakes->Guard);
    if (const auto Found = Insert.Remakes->Texts.find(Token); Found != Insert.Remakes->Texts.end()) { Reach.Exchange = Found->second; return Reach; }
    SketcherDocument Scratch = CreateSketcherDocument("Insert " + Insert.Name);
    SketcherInsert   Remade;
    Remade.Name = Insert.Name;
    std::string Key;
    Reach.Refusal = Insert.Author(Scratch, Key, Insert.Top - (Depth + SketcherInsertLift) * Insert.Width);
    if (Reach.Refusal.empty()) Reach.Refusal = NormalizeSketcherInsert(Scratch, Key, Remade, true);
    if (!Reach.Refusal.empty()) return Reach;
    if (Insert.Remakes->Texts.size() > 64) Insert.Remakes->Texts.clear();                  // A long depth drag: keep it bounded
    Insert.Remakes->Texts.emplace(Token, Remade.Exchange);
    Reach.Exchange = std::move(Remade.Exchange);
    return Reach;
}

Vector3 ReachSketcherInsertPoint(const SketcherInsert& Insert, Vector3 Point, double Depth)
{
    const double Scale = Depth / std::max(1.0e-12, Insert.Depth);                            // [-]
    if (!Insert.Author) return { Point.X, Point.Y, Point.Z * Scale };
    return { Point.X, Point.Y, Point.Z < 0.0 ? Point.Z * Scale : Point.Z };
}

std::string SaveSketcherInsert(const SketcherDocument& Document, const std::string& Key, const std::string& Folder, const std::string& Name, std::string& Path)
{
    std::string Stem;
    for (const char Letter : Name) if (std::isalnum(static_cast<unsigned char>(Letter))) Stem.push_back(Letter);
    if (Stem.empty()) Stem = "Insert";
    if (Folder.empty()) return "no insert library folder (Files.InsertFolder in the options)";
    std::error_code Error;
    std::filesystem::create_directories(Folder, Error);
    std::filesystem::path Wanted = std::filesystem::path(Folder) / (Stem + ".step");
    for (int Ordinal = 2; std::filesystem::exists(Wanted, Error); ++Ordinal) Wanted = std::filesystem::path(Folder) / (Stem + std::to_string(Ordinal) + ".step");
    Path = Wanted.string();
    const SketcherExportOutcome Outcome = ExportSketcherBodies(Document, Path, { Key });
    return Outcome.Refusal;
}

SketcherInsertLibrary AssembleSketcherUserInserts(const std::string& Folder)
{
    const auto Start = std::chrono::steady_clock::now();
    SketcherInsertLibrary Library;
    Library.Folder = Folder;
    const auto Canonical = [](const std::string& Raw)
    {
        std::string Out;
        for (const char Ch : Raw)
            if (std::isalnum(static_cast<unsigned char>(Ch)))
                Out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(Ch))));
        return Out;
    };
    std::set<std::string> Seen = {
        "countersink", "counterbore", "steppedhole", "hexsocket", "steppedslot", "steppedpocket",
        "squarepocket", "roundhole", "slot", "hexpocket", "triangle", "circle", "rectangle", "polygon"
    };
    std::error_code Error;
    std::vector<std::filesystem::path> Files;
    if (!Folder.empty() && std::filesystem::is_directory(Folder, Error))
    {
        for (const std::filesystem::directory_entry& Entry : std::filesystem::directory_iterator(Folder, Error))
        {
            std::string Extension = Entry.path().extension().string();
            std::transform(Extension.begin(), Extension.end(), Extension.begin(), [](unsigned char Letter) { return char(std::tolower(Letter)); });
            if (Entry.is_regular_file(Error) && (Extension == ".step" || Extension == ".stp")) Files.push_back(Entry.path());
        }
    }
    std::sort(Files.begin(), Files.end());
    for (const std::filesystem::path& File : Files)
    {
        const std::string Name = File.stem().string();
        if (!Seen.insert(Canonical(Name)).second) continue;
        const SketcherStepReading Reading = ReadSketcherStepFile(File.string());
        if (!Reading.Refusal.empty() || Reading.Texts.empty())
        {
            Library.Notes.push_back(Name + ": " + (Reading.Refusal.empty() ? std::string("no solid") : Reading.Refusal));
            continue;
        }
        SketcherStepReading First = Reading;
        First.Texts.resize(1);
        SketcherDocument Scratch = CreateSketcherDocument("Insert " + Name);
        const SketcherCommandOutcome Imported = ImportSketcherStep(Scratch, First);
        SketcherInsert Insert;
        Insert.Name = Name;
        Insert.Path = File.string();
        const std::string Refusal = Imported.AcceptedCondition ? NormalizeSketcherInsert(Scratch, Scratch.Tree.Steps.back().Label, Insert) : Imported.Refusal;
        if (!Refusal.empty()) { Library.Notes.push_back(Name + ": " + Refusal); continue; }
        Library.Inserts.push_back(std::move(Insert));
    }
    Library.Milliseconds = ElapsedMilliseconds(Start);
    return Library;
}

std::vector<std::array<Vector3, 2>> BuildSketcherCompoundCutterSegments(const SketcherCompoundCutterSpec& Spec)
{
    std::vector<std::array<Vector3, 2>> Segments;
    if (!(Length(Spec.Spot.ZAxis) > 1.0e-9)) return Segments;
    const Vector3 Normal = Normalize(Spec.Spot.ZAxis);
    Vector3       BaseX  = Spec.Spot.XAxis - Normal * Dot(Spec.Spot.XAxis, Normal);
    if (!(Length(BaseX) > 1.0e-9))
        BaseX = std::abs(Normal.Z) < 0.9 ? Cross(Vector3{ 0.0, 0.0, 1.0 }, Normal) : Cross(Vector3{ 1.0, 0.0, 0.0 }, Normal);
    BaseX = Normalize(BaseX);
    const Vector3 BaseY  = Normalize(Cross(Normal, BaseX));
    const Vector3 Across = Normalize(BaseX * std::cos(Spec.Turn) + BaseY * std::sin(Spec.Turn));
    const bool    Boss   = Spec.Combination == FeatureCombination::Union;
    const Vector3 Dir    = Boss ? Normal : (Normal * -1.0);
    const Vector3 FrameY = Normalize(Cross(Dir, Across));
    const double  R1     = std::max(0.002, Spec.OuterRadius);
    const double  D1     = std::max(0.001, Spec.OuterDepth);
    const double  R2     = std::clamp(Spec.InnerRadius, 0.001, 0.96 * R1);
    const double  D2     = std::max(0.001, Spec.InnerDepth);

    const auto At = [&](double U, double V, double Depth) -> Vector3
    {
        return Spec.Spot.Origin + Across * U + FrameY * V + Dir * Depth;
    };
    const auto SampleLoop = [&](bool OuterStage, double Rad) -> std::vector<Vector2>
    {
        std::vector<Vector2> Loop;
        constexpr double TwoPi = 2.0 * std::numbers::pi;
        if (Spec.Kind == SketcherCompoundCutterKind::Countersink ||
            Spec.Kind == SketcherCompoundCutterKind::Counterbore ||
            Spec.Kind == SketcherCompoundCutterKind::SteppedHole ||
            (!OuterStage && Spec.Kind == SketcherCompoundCutterKind::HexSocket))
        {
            constexpr int Steps = 32;
            for (int Index = 0; Index < Steps; ++Index)
            {
                const double Angle = TwoPi * Index / Steps;
                Loop.push_back({ Rad * std::cos(Angle), Rad * std::sin(Angle) });
            }
        }
        else if (OuterStage && Spec.Kind == SketcherCompoundCutterKind::HexSocket)
        {
            for (int Index = 0; Index < 6; ++Index)
            {
                const double Angle = Index * std::numbers::pi / 3.0;
                Loop.push_back({ Rad * std::cos(Angle), Rad * std::sin(Angle) });
            }
        }
        else if (Spec.Kind == SketcherCompoundCutterKind::SteppedSlot)
        {
            const double HalfSpan = 1.15 * R1;
            constexpr int ArcSteps = 12;
            for (int Index = 0; Index <= ArcSteps; ++Index)
            {
                const double Angle = -0.5 * std::numbers::pi + std::numbers::pi * Index / ArcSteps;
                Loop.push_back({ +HalfSpan + Rad * std::cos(Angle), Rad * std::sin(Angle) });
            }
            for (int Index = 0; Index <= ArcSteps; ++Index)
            {
                const double Angle = 0.5 * std::numbers::pi + std::numbers::pi * Index / ArcSteps;
                Loop.push_back({ -HalfSpan + Rad * std::cos(Angle), Rad * std::sin(Angle) });
            }
        }
        else
        {
            Loop = { { -Rad, -Rad }, { +Rad, -Rad }, { +Rad, +Rad }, { -Rad, +Rad } };
        }
        return Loop;
    };
    const auto EmitLoop = [&](const std::vector<Vector2>& Loop, double Depth)
    {
        for (size_t Index = 0; Index < Loop.size(); ++Index)
        {
            const Vector2& A = Loop[Index];
            const Vector2& B = Loop[(Index + 1) % Loop.size()];
            Segments.push_back({ At(A.X, A.Y, Depth), At(B.X, B.Y, Depth) });
        }
    };

    const std::vector<Vector2> TopOuter = SampleLoop(true, R1);
    const std::vector<Vector2> Inner    = SampleLoop(false, R2);
    EmitLoop(TopOuter, 0.0);
    if (Spec.Kind == SketcherCompoundCutterKind::Countersink)
    {
        EmitLoop(Inner, D1);
        EmitLoop(Inner, D1 + D2);
        for (int Quadrant = 0; Quadrant < 4; ++Quadrant)
        {
            const double Angle = Quadrant * 0.5 * std::numbers::pi;
            const double C = std::cos(Angle), S = std::sin(Angle);
            Segments.push_back({ At(R1 * C, R1 * S, 0.0), At(R2 * C, R2 * S, D1) });
            Segments.push_back({ At(R2 * C, R2 * S, D1),  At(R2 * C, R2 * S, D1 + D2) });
        }
    }
    else
    {
        EmitLoop(TopOuter, D1);
        EmitLoop(Inner, D1);
        const double Tip = Spec.Kind == SketcherCompoundCutterKind::SteppedHole ? std::min(0.45 * D2, 0.58 * R2) : 0.0;
        const double ShaftFloor = D1 + D2 - Tip;
        EmitLoop(Inner, ShaftFloor);
        const int Ribs = static_cast<int>(TopOuter.size()) == 6 ? 6 : 4;
        for (int Index = 0; Index < Ribs; ++Index)
        {
            const Vector2 P1 = Ribs == 6 ? TopOuter[Index] : TopOuter[(Index * TopOuter.size()) / 4];
            Segments.push_back({ At(P1.X, P1.Y, 0.0), At(P1.X, P1.Y, D1) });
        }
        for (int Quadrant = 0; Quadrant < 4; ++Quadrant)
        {
            const Vector2 P2 = Inner[(Quadrant * Inner.size()) / 4];
            Segments.push_back({ At(P2.X, P2.Y, D1), At(P2.X, P2.Y, ShaftFloor) });
            if (Tip > 1.0e-4) Segments.push_back({ At(P2.X, P2.Y, ShaftFloor), At(0.0, 0.0, D1 + D2) });
        }
    }
    return Segments;
}

} // namespace Frontier::Workspaces::Sketcher
