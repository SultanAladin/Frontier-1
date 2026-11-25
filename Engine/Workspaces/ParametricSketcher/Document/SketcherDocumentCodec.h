/*====================================================================================================================================
                                                       SOLIDARCDOCUMENTCODEC.H
====================================================================================================================================*/
// 🧩 Sketcher document files (.sketcher): parameters, steps, sketches, references, captions

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                    INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Workspaces/ParametricSketcher/Document/SketcherDocument.h"

#include <map>
#include <string>

namespace Frontier::Workspaces::Sketcher
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    STRUCTS
//------------------------------------------------------------------------------------------------------------------------

struct SketcherDocumentFile
{
    std::string                        Title    = {};                                        // [-] - Document caption
    GeometrySpace::Parametric::FeatureTree              Tree     = {};                                        // [-] - Steps + parameters (signatures re-captured on load)
    std::map<std::string, std::string> Captions = {};                                        // [-] - Step label → caption
    std::vector<SketcherImagePlane>       ImagePlanes       = {};                            // [-] - Blueprint references
    std::vector<SketcherReferenceSurface> ReferenceSurfaces = {};                            // [-] - Embedded compiled reference surfaces
    std::vector<uint8_t>                  PreviewPngBytes   = {};                            // [-] - Embedded studio preview PNG
    SketcherAppearance                    Appearance        = {};                            // [-] - Body / face matcaps
    SketcherDrawingSheetSettings          DrawingSheet      = {};                            // [-] - Alt+D sheet settings
    std::vector<SketcherDimension>        Dimensions        = {};                            // [-] - Persistent 3D CAD dimension annotations
    std::string                           Refusal           = {};                            // [-] - Parse failure (empty: fine)
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// 📝 The text is deterministic for one tree (keys in a fixed order), so a save → load → save round trip is byte-equal.
[[nodiscard]] std::string           EncodeSketcherDocument(const SketcherDocument& Document);
[[nodiscard]] SketcherDocumentFile DecodeSketcherDocument(const std::string& Text);

// File helpers: false / Refusal on an I/O or parse failure.
bool                               SaveSketcherDocument(const SketcherDocument& Document, const std::string& Path);
[[nodiscard]] SketcherDocumentFile LoadSketcherDocumentFile(const std::string& Path);

// Lightweight header reader for Project Browser thumbnails & captions (never decompresses reference geometry).
[[nodiscard]] bool LoadSketcherPreviewHeader(
    const std::string&    Path,
    std::string&          OutTitle,
    std::string&          OutDescription,
    std::vector<uint8_t>& OutPreviewPng);

} // namespace Frontier::Workspaces::Sketcher
