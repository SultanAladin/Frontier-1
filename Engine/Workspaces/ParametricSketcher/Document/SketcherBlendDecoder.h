/*====================================================================================================================================
                                                      SKETCHERBLENDDECODER.H
====================================================================================================================================*/
// 🧩 Native cross-platform Blender (.blend) reader and modifier stack evaluator for SolidArc high-poly vehicle import

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                    INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Workspaces/ParametricSketcher/Document/SketcherReferenceSurface.h"

#include <cstdint>
#include <string>

namespace Frontier::Workspaces::Sketcher
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    STRUCTS
//------------------------------------------------------------------------------------------------------------------------

struct SketcherBlendImportOptions
{
    bool   IncludeHiddenCondition    = false;                                                // [-] - Skip viewport-hidden helpers by default
    bool   EvaluateModifiersCondition = true;                                                // [-] - Run Mirror, Subsurf, Solidify, Shrinkwrap, Mask, Array, Bevel, Skin
    int    MaxSubdivisionLevel       = 3;                                                    // [-] - Safety ceiling per Subsurf modifier (0 .. 4)
    double UnitScale                 = 1.0;                                                  // [-] - Uniform world scale multiplier
    double AutoSmoothDegrees         = 30.0;                                                 // [deg] - Default dihedral crease split angle
};

struct SketcherBlendImportReport
{
    SketcherReferenceSurface Surface         = {};                                           // [-]  - GPU-ready evaluated reference surface + BVH
    uint32_t                 SourceObjects   = 0u;                                           // [-]  - Visible mesh objects imported
    uint32_t                 SkippedHidden   = 0u;                                           // [-]  - Viewport-hidden objects skipped
    uint32_t                 EvaluatedModifiers = 0u;                                        // [-]  - Total enabled modifiers executed
    uint32_t                 RawPolygons     = 0u;                                           // [-]  - Base control-cage polygons before modifiers
    uint32_t                 OutputTriangles = 0u;                                           // [-]  - Final GPU triangles
    uint32_t                 CreaseSegments  = 0u;                                           // [-]  - Final GPU feature/crease line segments
    double                   ElapsedMs       = 0.0;                                          // [ms] - Wall time for decode + modifiers + BVH
    std::string              BlenderVersion  = {};                                           // [-]  - Parsed header version (e.g. "5.02")
    std::string              Refusal         = {};                                           // [-]  - Empty on success
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// Decode a .blend file (ZSTD multi-frame, GZIP, or raw uncompressed; Blender 2.8x through 5.2+) and evaluate its
// viewport-enabled modifier stacks into a GPU-ready SketcherReferenceSurface with BVH acceleration.
[[nodiscard]] SketcherBlendImportReport DecodeSketcherBlendFile(
    const std::string&                FilePath,
    const SketcherBlendImportOptions& Options = {});

} // namespace Frontier::Workspaces::Sketcher
