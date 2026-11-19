/*====================================================================================================================================
                                                       SOLIDARCDRAWINGSHEET.CPP
====================================================================================================================================*/
// 🧩 Drawing sheet (Alt+D): shown solid bodies → kernel ComposeDrawingSheet (metres → millimetres) → SVG / DXF R12

//------------------------------------------------------------------------------------------------------------------------
//                                                    INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Workspaces/ParametricSketcher/Document/SketcherDrawingSheet.h"

#include "Engine/GeometrySpace/Parametric/Drawing/DrawingExport.h"

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <vector>

namespace Frontier::Workspaces::Sketcher
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    INTERNAL
//------------------------------------------------------------------------------------------------------------------------

namespace
{

[[nodiscard]] bool ShownSolidCondition(const SketcherDocument& Document, const GeometrySpace::Parametric::RegeneratedBody& Body, int& Sheets)
{
    if (Document.Appearance.HiddenBodies.contains(Body.Key) || !Body.Body.PresentCondition()) return false;
    for (const SketcherBodyFigures& Figures : Document.Summary.Bodies)
    {
        if (Figures.Key == Body.Key && Figures.SheetCondition) { ++Sheets; return false; }
    }
    return true;
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

const char* SketcherProjectionAngleLabel(GeometrySpace::Parametric::DrawingProjectionAngle Angle) noexcept
{
    return Angle == GeometrySpace::Parametric::DrawingProjectionAngle::First ? "First" : "Third";
}

bool ParseSketcherPaperSize(const std::string& Label, GeometrySpace::Parametric::DrawingPaperSize& Paper) noexcept
{
    for (int Index = 0; Index < 5; ++Index)
    {
        if (Label == GeometrySpace::Parametric::DrawingPaperLabel(static_cast<GeometrySpace::Parametric::DrawingPaperSize>(Index))) { Paper = static_cast<GeometrySpace::Parametric::DrawingPaperSize>(Index); return true; }
    }
    return false;
}

bool ParseSketcherProjectionAngle(const std::string& Label, GeometrySpace::Parametric::DrawingProjectionAngle& Angle) noexcept
{
    if (Label == "First") { Angle = GeometrySpace::Parametric::DrawingProjectionAngle::First; return true; }
    if (Label == "Third") { Angle = GeometrySpace::Parametric::DrawingProjectionAngle::Third; return true; }
    return false;
}

std::string SketcherDrawingSheetKey(const SketcherDrawingSheetSettings& Settings)
{
    const SketcherTitleFields& Title = Settings.Title;
    char Text[160];
    std::snprintf(Text, sizeof(Text), "%s|%s|%.17g|%d%d%d%d%d|", GeometrySpace::Parametric::DrawingPaperLabel(Settings.Paper), SketcherProjectionAngleLabel(Settings.Angle), Settings.Scale,
                  Settings.IsometricEnabled, Settings.SectionEnabled, Settings.HiddenLinesEnabled, Settings.TangentEdgesEnabled, Settings.DimensionsEnabled);
    std::string Key = Text;
    if (Settings.SectionEnabled) Key += SketcherSectionKey(Settings.Section);
    for (const std::string* Field : { &Title.Title, &Title.DrawingNumber, &Title.Author, &Title.Date, &Title.Material, &Title.Revision, &Title.Company }) Key += "|" + *Field;
    return Key;
}

std::string SketcherDrawingSheetCacheKey(const SketcherDocument& Document, const SketcherDrawingSheetSettings& Settings)
{
    std::string Key = SketcherDrawingSheetKey(Settings) + "#";
    for (const std::string& Hidden : Document.Appearance.HiddenBodies) Key += Hidden + ";";
    return Key;
}

bool DefaultSketcherDrawingSheetCondition(const SketcherDrawingSheetSettings& Settings)
{
    return SketcherDrawingSheetKey(Settings) == SketcherDrawingSheetKey(SketcherDrawingSheetSettings{}) && SketcherSectionKey(Settings.Section) == SketcherSectionKey(SketcherDrawingSheetSettings{}.Section);
}

GeometrySpace::Parametric::OrthonormalBasis SketcherSheetSectionFrame(const GeometrySpace::Parametric::OrthonormalBasis& Plane)
{
    return GeometrySpace::Parametric::ConstructFrame(Plane.Origin, Plane.ZAxis * -1.0, Plane.XAxis);
}

SketcherDrawingSheet ComposeSketcherDrawingSheet(const SketcherDocument& Document, const SketcherDrawingSheetSettings& Settings)
{
    const auto Start = std::chrono::steady_clock::now();
    SketcherDrawingSheet Out;
    Out.Revision    = Document.Revision;
    Out.SettingsKey = SketcherDrawingSheetCacheKey(Document, Settings);
    const auto Finish = [&]()
    {
        Out.Milliseconds = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - Start).count();
        return Out;
    };
    std::vector<GeometrySpace::Parametric::BodyToken> Bodies;
    if (Document.Partition)
    {
        for (const GeometrySpace::Parametric::RegeneratedBody& Body : Document.Regeneration.Bodies) if (ShownSolidCondition(Document, Body, Out.Sheets)) Bodies.push_back(Body.Body);
    }
    if (Bodies.empty())
    {
        Out.Refusal = Out.Sheets ? "Drawing sheet: no shown solid body (sheet bodies are not drawn)" : "Drawing sheet: no shown solid body";
        return Finish();
    }
    Out.Bodies = static_cast<int>(Bodies.size());

    GeometrySpace::Parametric::DrawingSheetRequest Request;
    Request.Paper                = Settings.Paper;
    Request.Angle                = Settings.Angle;
    Request.Scale                = Settings.Scale;
    Request.ModelUnitMillimetres = SketcherModelUnitMillimetres;
    Request.IsometricEnabled     = Settings.IsometricEnabled;
    Request.HiddenLinesEnabled   = Settings.HiddenLinesEnabled;
    Request.TangentEdgesEnabled  = Settings.TangentEdgesEnabled;
    Request.DimensionsEnabled    = Settings.DimensionsEnabled;
    const SketcherTitleFields& Title = Settings.Title;
    Request.Title = { Title.Title.empty() ? Document.Title : Title.Title, Title.DrawingNumber, Title.Author, Title.Date, Title.Material, Title.Revision, Title.Company };
    if (Settings.SectionEnabled)
    {
        const SketcherSection Plane = ResolveSketcherSectionPlane(Document, Settings.Section);
        if (!Plane.PlaneCondition)
        {
            Out.Refusal = "Drawing sheet: section A-A — " + (Plane.Refusal.empty() ? std::string("the plane does not resolve") : Plane.Refusal);
            return Finish();
        }
        Request.Section = SketcherSheetSectionFrame(Plane.Plane);
    }
    Out.Sheet   = GeometrySpace::Parametric::ComposeDrawingSheet(*Document.Partition, Bodies, Request);
    Out.Refusal = Out.Sheet.Refusal;
    return Finish();
}

std::string SketcherDrawingSheetExportPath(const std::string& DocumentPath, const std::string& Extension)
{
    if (DocumentPath.empty()) return "Untitled" + Extension;
    std::string Stem = DocumentPath;
    for (const char* Suffix : { ".sketcher", ".drafting.toml", ".toml" })
    {
        const std::string Tail = Suffix;
        if (Stem.size() > Tail.size() && Stem.compare(Stem.size() - Tail.size(), Tail.size(), Tail) == 0) { Stem.resize(Stem.size() - Tail.size()); break; }
    }
    return Stem + Extension;
}

std::string ExportSketcherDrawingSheet(const SketcherDrawingSheet& Sheet, const std::string& Path)
{
    if (!Sheet.Refusal.empty()) return "Export: the drawing sheet was refused (" + Sheet.Refusal + ")";
    return GeometrySpace::Parametric::ExportDrawingFile(Sheet.Sheet, Path);
}

} // namespace Frontier::Workspaces::Sketcher
