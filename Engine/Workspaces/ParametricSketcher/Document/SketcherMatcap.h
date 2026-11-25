/*====================================================================================================================================
                                                           SOLIDARCMATCAP.H
====================================================================================================================================*/
// 🧩 Matcaps: the built-in engineering library (metals, plastics, rubber, clay, ceramic, paint) composed analytically, user
//    pictures, and the document appearance (body matcap + per-face matcaps by persistent name)
#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                    INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Workspaces/ParametricSketcher/Document/SketcherImagePlane.h"

#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace Frontier::Workspaces::Sketcher
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 Layer 0 is the analytic Studio shading of SketcherSurface.slang (no picture); built-in and user matcaps follow as
//    texture-array layers 1 … n. The layer travels in bits 8 … 15 of the per-face state word.
inline constexpr const char* SketcherStudioMatcap = "Studio";
constexpr uint32_t SketcherMatcapExtent     = 256;                                           // [px] - Square layer side
constexpr uint32_t SketcherMatcapLayerShift = 8;                                             // [-]  - State-word bit offset
constexpr uint32_t SketcherMatcapLayerLimit = 255;                                           // [-]  - Largest layer index

//------------------------------------------------------------------------------------------------------------------------
//                                                    TYPES
//------------------------------------------------------------------------------------------------------------------------

// A user matcap: a sphere picture (PNG / JPEG / BMP / GIF, e.g. a Blender matcap) under a document-unique name.
struct SketcherCustomMatcap
{
    std::string Name = {};                                                                   // [-] - "ShopSteel"
    std::string Path = {};                                                                   // [-] - Picture file
};

// 📝 Saved with the document and part of the undo history (appearance entries never regenerate the body). Face keys are
//    persistent names, so an assignment survives regeneration; a name the body no longer has is kept and reported, never
//    moved to another face.
struct SketcherAppearance
{
    std::string                        BodyMatcap    = SketcherStudioMatcap;                 // [-] - Default for every body without its own matcap
    std::map<std::string, std::string> BodyMatcaps   = {};                                   // [-] - Body key (creating step label) → matcap
    std::map<std::string, std::string> FaceMatcaps   = {};                                   // [-] - Persistent face name → matcap
    std::vector<SketcherCustomMatcap>  CustomMatcaps = {};                                   // [-] - User pictures, in layer order
    std::set<std::string>              HiddenBodies  = {};                                   // [-] - Body keys out of view and picking (Plasticity H)
    std::set<std::string>              HiddenCurves  = {};                                   // [-] - Sketch / curve labels out of view, picking and snapping
    std::set<std::string>              RemovedCurves = {};                                   // [-] - Deleted (X) yet read by a later step: hidden and off the Outliner
    std::set<std::string>              LockedFigures = {};                                   // [-] - Body keys and curve labels shown but not pickable
    std::set<std::string>              LiveCutters   = {};                                   // [-] - Live Boolean cutter body keys (semitransparent display, selectable & editable)
};

// One built-in matcap: its name, caption and the look it was composed for.
struct SketcherMatcapDescription
{
    std::string Name     = {};                                                               // [-] - "BrushedSteel"
    std::string Caption  = {};                                                               // [-] - "Brushed steel"
    std::string Category = {};                                                               // [-] - "Metal", "Plastic", "Other"
};

// 📝 The layers the viewport samples: Names[0] = Studio (no picture), Pictures[k − 1] is layer k. Key changes exactly when
//    the custom list changes (the built-ins are fixed), so the GPU array is re-uploaded only then.
struct SketcherMatcapLibrary
{
    std::vector<std::string>     Names    = {};                                              // [-] - Layer → matcap name
    std::vector<std::string>     Captions = {};                                              // [-] - Layer → caption
    std::vector<SketcherPicture> Pictures = {};                                              // [-] - Layers 1 … n, 256 × 256 RGBA8
    std::vector<std::string>     Refusals = {};                                              // [-] - Layer → decode refusal ("" fine)
    std::string                  Key      = {};                                              // [-] - Custom names + paths
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// The built-in library after Studio, in layer order (Chrome first).
[[nodiscard]] const std::vector<SketcherMatcapDescription>& SketcherBuiltInMatcaps();

// 💡 Composes a built-in matcap: pixel (i, j) holds the colour of a sphere whose view-space normal is
//    n = (x, y, √(1 − x² − y²)), x = 2(i + ½)/E − 1, y = 1 − 2(j + ½)/E, lit by a fixed studio rig (key and strip softboxes, top
//    panel, rim, floor). Metals reflect the rig tinted by Schlick F(θ) with F₀ = tint; plastics add a white Fresnel
//    reflection (F₀ 0.04) to a diffuse base; brushed metal averages the reflection along screen x (anisotropic). Pixels
//    outside the disc repeat the rim colour so linear filtering at the silhouette stays clean. Unknown name → refusal.
[[nodiscard]] SketcherPicture ComposeSketcherMatcap(const std::string& Name, uint32_t Extent = SketcherMatcapExtent);

// The analytic Studio shading of SketcherSurface.slang (layer 0) as a matcap picture: the Inspector thumbnail, and the
// gate that the shader formula and its mirror agree.
[[nodiscard]] SketcherPicture ComposeSketcherStudioMatcap(uint32_t Extent = SketcherMatcapExtent);

// Centre square crop + bilinear resample to Extent × Extent, alpha forced opaque (user pictures of any size).
[[nodiscard]] SketcherPicture FitSketcherMatcapPicture(const SketcherPicture& Source, uint32_t Extent = SketcherMatcapExtent);

// Studio + built-ins + the appearance's custom pictures (decoded and fitted; a refused picture keeps its layer, grey).
[[nodiscard]] SketcherMatcapLibrary AssembleSketcherMatcaps(const SketcherAppearance& Appearance);
[[nodiscard]] std::string           SketcherMatcapKey(const SketcherAppearance& Appearance);

// Layer of a matcap name in the library (0 = Studio, also for unknown names).
[[nodiscard]] uint32_t SketcherMatcapLayer(const SketcherMatcapLibrary& Library, const std::string& Name) noexcept;

// The matcap a face shows: its own assignment, else the body matcap.
// Resolution order: the face's own matcap → its body's → the default (BodyMatcap).
[[nodiscard]] const std::string& SketcherBodyMatcap(const SketcherAppearance& Appearance, const std::string& BodyKey);
[[nodiscard]] const std::string& SketcherFaceMatcap(const SketcherAppearance& Appearance, const std::string& BodyKey, const std::string& FaceName);

// Built-in or custom name known to this appearance.
[[nodiscard]] bool SketcherMatcapKnownCondition(const SketcherAppearance& Appearance, const std::string& Name);

} // namespace Frontier::Workspaces::Sketcher
