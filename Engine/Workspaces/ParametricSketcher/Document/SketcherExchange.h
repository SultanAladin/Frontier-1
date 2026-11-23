/*====================================================================================================================================
                                                          SOLIDARCEXCHANGE.H
====================================================================================================================================*/
// 🧩 STEP / STL exchange for the document (priority 4 X4): bodies out as STEP AP214 or binary STL in millimetres, STEP
//    files in as one canonical single-body STEP text per solid, ready to embed in Import steps

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                    INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Workspaces/ParametricSketcher/Document/SketcherBlendDecoder.h"
#include "Engine/Workspaces/ParametricSketcher/Document/SketcherDocument.h"

#include <string>
#include <vector>

namespace Frontier::Workspaces::Sketcher
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    TYPES
//------------------------------------------------------------------------------------------------------------------------

enum class SketcherExchangeFormat : uint8_t
{
    Unknown = 0,                                                                             // Extension not recognised
    Step    = 1,                                                                             // .step / .stp — ISO 10303-21, AP214
    Stl     = 2,                                                                             // .stl — binary STL
    Toml    = 3,                                                                             // .toml — VehicleCurves (import) / document
    Blend   = 4,                                                                             // .blend — Blender high-poly vehicle reference
};

// 📝 One export: which bodies went out, how big the file is and what the writer reported.
struct SketcherExportOutcome
{
    std::string              Path         = {};                                              // [-]  - File written
    SketcherExchangeFormat   Format       = SketcherExchangeFormat::Unknown;                 // [-]  - By extension
    std::vector<std::string> Bodies       = {};                                              // [-]  - Body keys written
    double                   Bytes        = 0.0;                                             // [B]  - File size
    int                      Faces        = 0;                                               // [-]  - STEP: ADVANCED_FACE written
    int                      Triangles    = 0;                                               // [-]  - STL: facets written
    int                      Approximated = 0;                                               // [-]  - STEP: cubic stand-ins (rolling-ball surfaces, traced curves)
    double                   Milliseconds = 0.0;                                             // [ms]
    std::string              Refusal      = {};                                              // [-]  - Empty on success (nothing written otherwise)
};

// 💡 A STEP file read for import: every solid is assembled and audited by the kernel (ReadStep), then written again
//    on its own by the kernel's writer. The Import step embeds that canonical text, so the document needs neither the
//    original file nor its quirks (another kernel's seams, loop directions, units) to replay: export → import → export
//    is a byte fixed point (ExchangeValidation EX9).
struct SketcherStepReading
{
    std::vector<std::string> Texts               = {};                                       // [-]  - Canonical STEP text, one per solid, in file order
    std::string              Stem                = {};                                       // [-]  - File name without folder and extension (captions)
    int                      Faces               = 0;                                        // [-]  - Faces read
    double                   FileUnitMillimetres = 1.0;                                      // [mm] - Length unit the file declared
    int                      ReorientedFaces     = 0;                                        // [-]  - Loop directions repaired
    int                      TolerantEdges       = 0;                                        // [-]  - Edges given a local precision
    std::vector<std::string> Notes               = {};                                       // [-]  - Reader remarks
    double                   Milliseconds        = 0.0;                                      // [ms] - Read + canonical rewrite
    std::string              Refusal             = {};                                       // [-]  - Empty on success
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// Format from the extension (case-insensitive): .step / .stp, .stl, .toml.
[[nodiscard]] SketcherExchangeFormat SketcherExchangeFormatOf(const std::string& Path);
[[nodiscard]] const char*            SketcherExchangeFormatLabel(SketcherExchangeFormat Format) noexcept;

// Default export path beside the document: <document stem>.step (Untitled.step before the first save).
[[nodiscard]] std::string SketcherExportPath(const std::string& DocumentPath, const std::string& Extension = ".step");

// The bodies an export takes: the selected bodies when any are picked, else every shown body (hidden ones left out).
[[nodiscard]] std::vector<std::string> SketcherExportBodies(const SketcherDocument& Document);

// Writes Keys (empty: SketcherExportBodies) to Path in the format of its extension, metres → millimetres. STEP refuses
// sheet bodies (open panels) by name; STL tessellates them as they are. Nothing is written on a refusal.
[[nodiscard]] SketcherExportOutcome ExportSketcherBodies(const SketcherDocument& Document, const std::string& Path,
                                                         const std::vector<std::string>& Keys = {});

// Reads a STEP file in the document's unit (metres) and returns one canonical single-body STEP text per solid.
// Surface models (sheets) are refused: 🚧 the writer takes solids only.
[[nodiscard]] SketcherStepReading ReadSketcherStepFile(const std::string& Path);

// Imports a Blender (.blend) file into Document.ReferenceSurfaces with modifier stacks evaluated for GPU display
// and BVH-accelerated precision curve tracing.
[[nodiscard]] SketcherBlendImportReport ImportSketcherBlend(
    SketcherDocument&                 Document,
    const std::string&                Path,
    const SketcherBlendImportOptions& Options = {});

} // namespace Frontier::Workspaces::Sketcher
