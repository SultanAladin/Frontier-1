/*====================================================================================================================================
                                                           SOLIDARCKNIFE.H
====================================================================================================================================*/
// 🧩 Knife inserts (KitOps-style boolean library): built-in cutters authored procedurally and STEP files from a library
//    folder, each normalized to the insert frame and kept as canonical STEP text plus its edge polylines for the ghost

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                    INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Workspaces/ParametricSketcher/Document/SketcherDocument.h"

#include "Engine/GeometrySpace/Parametric/Construction/BodyTransform.h"

#include <array>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace Frontier::Workspaces::Sketcher
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

inline constexpr double SketcherInsertLift = 0.05;                                            // [-] - Insert top above its surface (insert widths)

//------------------------------------------------------------------------------------------------------------------------
//                                                    STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 Insert frame: unit width across x / y, centred on the z axis, top at z = +SketcherInsertLift, the cutter reaching
//    down −z. The surface it is placed on is z = 0 with outward normal +z, so a Difference cuts a pocket of Depth.
//    A built-in keeps its Author: another depth is authored again (analytic faces stay analytic) rather than stretched.
using SketcherInsertAuthor = std::function<std::string(SketcherDocument& Scratch, std::string& Key, double Bottom)>;

// Remade built-ins by depth (shared by every copy of the insert; the knife restages often at one depth).
struct SketcherInsertRemakes
{
    std::mutex                       Guard = {};                                             // [-] - One remake at a time
    std::map<long long, std::string> Texts = {};                                             // [-] - Depth (1e-9 widths) → STEP text
};

struct SketcherInsert
{
    std::string                                             Name     = {};                    // [-] - Caption ("Round hole")
    std::string                                             Path     = {};                    // [-] - Library file (empty: built in)
    std::string                                             Exchange = {};                    // [-] - Canonical STEP text, insert frame
    double                                                  Depth    = 0.0;                   // [-] - Reach below the surface (insert widths)
    std::vector<std::array<GeometrySpace::Parametric::Vector3, 2>> Segments = {};                    // [-] - Edge polylines, insert frame (ghost)
    GeometrySpace::Parametric::Vector2                             Anchor   = { 0.5, 0.0 };          // [-] - Outline point farthest from the axis (drag grip)
    SketcherInsertAuthor                                    Author   = {};                    // [-] - Built-in: authors it at a template bottom
    double                                                  Width    = 1.0;                   // [len] - Authored width (before normalizing)
    double                                                  Top      = 0.0;                   // [len] - Authored top z (before normalizing)
    double                                                  Shallow  = 0.0;                   // [-] - Least depth a built-in takes (insert widths)
    std::shared_ptr<SketcherInsertRemakes>                  Remakes  = std::make_shared<SketcherInsertRemakes>();   // [-] - Other depths
};

// The insert reaching Depth widths below its surface: the STEP text to import and the z stretch of its placement
// (1 for a built-in authored again at that depth; Depth / Insert.Depth for a library file, whose curved faces the
// transform then carries as NURBS).
struct SketcherInsertReach
{
    std::string Exchange = {};                                                               // [-] - STEP text, insert frame
    double      Stretch  = 1.0;                                                              // [-] - Insert-frame z scale
    std::string Refusal  = {};                                                               // [-] - Why not (empty: made)
};

struct SketcherInsertLibrary
{
    std::string                 Folder       = {};                                            // [-]  - Folder scanned for *.step / *.stp
    std::vector<SketcherInsert> Inserts      = {};                                            // [-]  - Built-ins first, then files by name
    std::vector<std::string>    Notes        = {};                                            // [-]  - Inserts that could not be made / read
    double                      Milliseconds = 0.0;                                           // [ms] - Assembly time
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// 💡 Built-ins (square pocket, round hole, countersink, slot, hex pocket, triangle, cross) are drawn and swept in a
//    scratch document by the same commands a user runs; every STEP file in Folder is read by ReadSketcherStepFile (first
//    solid). Each body is scaled to unit width, centred and lifted (NormalizeSketcherInsert) before its text is written.
[[nodiscard]] SketcherInsertLibrary AssembleSketcherInserts(const std::string& Folder);

// Insert frame → world: Spot's origin on the surface, its x̂ turned by Turn [rad] about n̂, everything scaled by Size
// [len]. RaisedCondition flips the insert about x̂ so it stands out of the surface (Union: a boss instead of a pocket).
// Stretch scales the insert-frame z (SketcherInsertReach::Stretch).
// Aspect scales the insert-frame y on top of Size (a base dragged corner to corner: x and y apart).
[[nodiscard]] GeometrySpace::Parametric::AffineMapping PlaceSketcherInsert(const GeometrySpace::Parametric::OrthonormalBasis& Spot, double Size, double Turn, bool RaisedCondition,
                                                              double Stretch = 1.0, double Aspect = 1.0);

// 💡 The insert at Depth (insert widths below its surface): a built-in is authored again with its bottom there (cached
//    per depth), a library file is stretched along z. Refused below a built-in's Shallow (a countersink's cone).
[[nodiscard]] SketcherInsertReach ReachSketcherInsert(const SketcherInsert& Insert, double Depth);

// An insert-frame ghost point as ReachSketcherInsert shapes the cutter at Depth: a built-in keeps its lift and moves its
// floor (z < 0 scaled), a library file stretches whole.
[[nodiscard]] GeometrySpace::Parametric::Vector3 ReachSketcherInsertPoint(const SketcherInsert& Insert, GeometrySpace::Parametric::Vector3 Point, double Depth);

// Writes body Key as Folder/Name.step (the next free Name2, Name3 … when taken); the library normalizes it on load.
// Returns a refusal (empty on success); Path receives the file written.
[[nodiscard]] std::string SaveSketcherInsert(const SketcherDocument& Document, const std::string& Key, const std::string& Folder, const std::string& Name, std::string& Path);

// 💡 User-authored STEP inserts only (no 1-stage built-in duplicates; deduplicates against built-in compound cutter names).
[[nodiscard]] SketcherInsertLibrary AssembleSketcherUserInserts(const std::string& Folder);

// 💡 World-space wireframe segments for Stage 1 and Stage 2 of a multi-stage compound cutter (for interactive viewport ghost).
[[nodiscard]] std::vector<std::array<GeometrySpace::Parametric::Vector3, 2>> BuildSketcherCompoundCutterSegments(const SketcherCompoundCutterSpec& Spec);

} // namespace Frontier::Workspaces::Sketcher
