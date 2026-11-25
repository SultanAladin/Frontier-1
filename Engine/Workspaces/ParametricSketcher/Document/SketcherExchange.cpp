/*====================================================================================================================================
                                                         SOLIDARCEXCHANGE.CPP
====================================================================================================================================*/
// 🧩 Document ↔ STEP / STL: kernel WriteStep / WriteStl (metres → millimetres) out, ReadStep + canonical WriteStep in

//------------------------------------------------------------------------------------------------------------------------
//                                                    INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Workspaces/ParametricSketcher/Document/SketcherExchange.h"

#include "Engine/Workspaces/ParametricSketcher/Document/SketcherDrawingSheet.h"

#include "Engine/GeometrySpace/Parametric/Exchange/StepExchange.h"
#include "Engine/GeometrySpace/Parametric/Exchange/StlExchange.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <filesystem>

namespace Frontier::Workspaces::Sketcher
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    INTERNAL
//------------------------------------------------------------------------------------------------------------------------

namespace
{

[[nodiscard]] double ElapsedMilliseconds(std::chrono::steady_clock::time_point Start)
{
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - Start).count();
}

// File name without folder and exchange / document extension ("Parts/Boss.step" → "Boss").
[[nodiscard]] std::string FileStem(const std::string& Path)
{
    std::string Stem = std::filesystem::path(Path).filename().string();
    for (const char* Suffix : { ".sketcher", ".drafting.toml", ".toml", ".step", ".stp", ".stl", ".STEP", ".STP", ".STL" })
    {
        const std::string Tail = Suffix;
        if (Stem.size() > Tail.size() && Stem.compare(Stem.size() - Tail.size(), Tail.size(), Tail) == 0) { Stem.resize(Stem.size() - Tail.size()); break; }
    }
    return Stem.empty() ? std::string("Untitled") : Stem;
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

SketcherExchangeFormat SketcherExchangeFormatOf(const std::string& Path)
{
    std::string Extension = std::filesystem::path(Path).extension().string();
    std::transform(Extension.begin(), Extension.end(), Extension.begin(), [](unsigned char Letter) { return static_cast<char>(std::tolower(Letter)); });
    if (Extension == ".step" || Extension == ".stp") return SketcherExchangeFormat::Step;
    if (Extension == ".stl") return SketcherExchangeFormat::Stl;
    if (Extension == ".toml") return SketcherExchangeFormat::Toml;
    if (Extension == ".blend") return SketcherExchangeFormat::Blend;
    return SketcherExchangeFormat::Unknown;
}

const char* SketcherExchangeFormatLabel(SketcherExchangeFormat Format) noexcept
{
    switch (Format)
    {
    case SketcherExchangeFormat::Step:    return "STEP";
    case SketcherExchangeFormat::Stl:     return "STL";
    case SketcherExchangeFormat::Toml:    return "TOML";
    case SketcherExchangeFormat::Blend:   return "BLEND";
    case SketcherExchangeFormat::Unknown: break;
    }
    return "unknown";
}

std::string SketcherExportPath(const std::string& DocumentPath, const std::string& Extension)
{
    return SketcherDrawingSheetExportPath(DocumentPath, Extension);
}

std::vector<std::string> SketcherExportBodies(const SketcherDocument& Document)
{
    std::vector<std::string> Keys;
    for (const std::string& Key : Document.Selection.Bodies)
    {
        const bool Present = std::any_of(Document.Regeneration.Bodies.begin(), Document.Regeneration.Bodies.end(),
                                         [&](const GeometrySpace::Parametric::RegeneratedBody& Body) { return Body.Key == Key && Body.Body.PresentCondition(); });
        if (Present && std::find(Keys.begin(), Keys.end(), Key) == Keys.end()) Keys.push_back(Key);
    }
    if (!Keys.empty()) return Keys;
    for (const GeometrySpace::Parametric::RegeneratedBody& Body : Document.Regeneration.Bodies)
        if (Body.Body.PresentCondition() && !Document.Appearance.HiddenBodies.contains(Body.Key) && !Document.Appearance.LiveCutters.contains(Body.Key)) Keys.push_back(Body.Key);
    return Keys;
}

SketcherExportOutcome ExportSketcherBodies(const SketcherDocument& Document, const std::string& Path, const std::vector<std::string>& Keys)
{
    const auto Start = std::chrono::steady_clock::now();
    SketcherExportOutcome Outcome;
    Outcome.Path   = Path;
    Outcome.Format = SketcherExchangeFormatOf(Path);
    Outcome.Bodies = Keys.empty() ? SketcherExportBodies(Document) : Keys;
    const auto Refuse = [&](const std::string& Reason) { Outcome.Refusal = "Export: " + Reason; Outcome.Milliseconds = ElapsedMilliseconds(Start); return Outcome; };
    if (Outcome.Format != SketcherExchangeFormat::Step && Outcome.Format != SketcherExchangeFormat::Stl) return Refuse(Path + " is neither .step / .stp nor .stl");
    if (!Document.Partition) return Refuse("the document has not been regenerated");
    if (Outcome.Bodies.empty()) return Refuse("no shown body to write");
    std::vector<GeometrySpace::Parametric::BodyToken> Tokens;
    for (const std::string& Key : Outcome.Bodies)
    {
        const auto Found = std::find_if(Document.Regeneration.Bodies.begin(), Document.Regeneration.Bodies.end(), [&](const GeometrySpace::Parametric::RegeneratedBody& Body) { return Body.Key == Key; });
        if (Found == Document.Regeneration.Bodies.end() || !Found->Body.PresentCondition()) return Refuse("no body named " + Key);
        Tokens.push_back(Found->Body);
    }
    std::error_code Error;
    const std::filesystem::path Folder = std::filesystem::path(Path).parent_path();
    if (!Folder.empty()) std::filesystem::create_directories(Folder, Error);                // Beside a document not saved yet
    std::string Failure;
    if (Outcome.Format == SketcherExchangeFormat::Step)
    {
        GeometrySpace::Parametric::StepRequest Request;
        Request.ModelUnitMillimetres = SketcherModelUnitMillimetres;
        Request.Name                 = FileStem(Path);
        const GeometrySpace::Parametric::StepExport Export = GeometrySpace::Parametric::WriteStep(*Document.Partition, Tokens, Request);
        if (!Export.Refusal.empty()) return Refuse(Export.Refusal);
        Failure              = GeometrySpace::Parametric::ExportStepFile(Export, Path);
        Outcome.Faces        = Export.Faces;
        Outcome.Approximated = Export.ApproximatedSurfaces + Export.ApproximatedCurves;
    }
    else
    {
        GeometrySpace::Parametric::StlRequest Request;
        Request.ModelUnitMillimetres = SketcherModelUnitMillimetres;
        Request.Name                 = FileStem(Path);
        const GeometrySpace::Parametric::StlExport Export = GeometrySpace::Parametric::WriteStl(*Document.Partition, Tokens, Request);
        if (!Export.Refusal.empty()) return Refuse(Export.Refusal);
        Failure           = GeometrySpace::Parametric::ExportStlFile(Export, Path);
        Outcome.Triangles = Export.Triangles;
    }
    if (!Failure.empty()) return Refuse(Failure);
    Outcome.Bytes        = double(std::filesystem::file_size(Path, Error));
    Outcome.Milliseconds = ElapsedMilliseconds(Start);
    return Outcome;
}

SketcherStepReading ReadSketcherStepFile(const std::string& Path)
{
    const auto Start = std::chrono::steady_clock::now();
    SketcherStepReading Reading;
    Reading.Stem = FileStem(Path);
    const auto Refuse = [&](const std::string& Reason) { Reading.Refusal = Reason; Reading.Texts.clear(); Reading.Milliseconds = ElapsedMilliseconds(Start); return Reading; };
    GeometrySpace::Parametric::ModelPartition    Scratch;
    GeometrySpace::Parametric::StepImportRequest Request;
    Request.ModelUnitMillimetres = SketcherModelUnitMillimetres;
    Request.Label                = "Import " + std::filesystem::path(Path).filename().string();
    const GeometrySpace::Parametric::StepImport Imported = GeometrySpace::Parametric::ImportStepFile(Scratch, Path, Request);
    if (!Imported.Refusal.empty()) return Refuse(Imported.Refusal);
    if (Imported.Sheets > 0) return Refuse(Request.Label + ": the file holds " + std::to_string(Imported.Sheets) + " surface model(s) (🚧 solids only)");
    Reading.Faces               = Imported.Faces;
    Reading.FileUnitMillimetres = Imported.FileUnitMillimetres;
    Reading.ReorientedFaces     = Imported.ReorientedFaces;
    Reading.TolerantEdges       = Imported.TolerantEdges;
    Reading.Notes               = Imported.Notes;
    for (size_t Index = 0; Index < Imported.Bodies.size(); ++Index)
    {
        GeometrySpace::Parametric::StepRequest Canonical;
        Canonical.ModelUnitMillimetres = SketcherModelUnitMillimetres;
        Canonical.Name                 = Imported.Bodies.size() == 1 ? Reading.Stem : Reading.Stem + "." + std::to_string(Index + 1);
        const GeometrySpace::Parametric::StepExport Export = GeometrySpace::Parametric::WriteStep(Scratch, { Imported.Bodies[Index] }, Canonical);
        if (!Export.Refusal.empty()) return Refuse(Request.Label + ": solid " + std::to_string(Index + 1) + " cannot be embedded: " + Export.Refusal);
        Reading.Texts.push_back(Export.Bytes);
    }
    Reading.Milliseconds = ElapsedMilliseconds(Start);
    return Reading;
}

SketcherBlendImportReport ImportSketcherBlend(
    SketcherDocument&                 Document,
    const std::string&                Path,
    const SketcherBlendImportOptions& Options)
{
    SketcherBlendImportReport Report = DecodeSketcherBlendFile(Path, Options);
    if (!Report.Refusal.empty()) return Report;

    // Ensure unique display name among existing ReferenceSurfaces
    std::string BaseName = Report.Surface.Name.empty() ? FileStem(Path) : Report.Surface.Name;
    std::string Unique   = BaseName;
    int Suffix           = 2;
    while (std::any_of(Document.ReferenceSurfaces.begin(), Document.ReferenceSurfaces.end(),
                       [&](const SketcherReferenceSurface& Existing) { return Existing.Name == Unique; }))
    {
        Unique = BaseName + "." + std::to_string(Suffix++);
    }
    Report.Surface.Name = Unique;
    Document.ReferenceSurfaces.push_back(Report.Surface);
    ++Document.ReferenceRevision;
    return Report;
}

} // namespace Frontier::Workspaces::Sketcher
