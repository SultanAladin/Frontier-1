/*====================================================================================================================================
                                                       SKETCHCATALOGUE.H
====================================================================================================================================*/
// 🧩 Sketch corpus: under-, well-, over-constrained and singular sketches with hand-derived expectations

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                    INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometrySpace/Parametric/Sketch/SketchSolver.h"

#include <string>
#include <utility>
#include <vector>

namespace Frontier::GeometrySpace::Parametric
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 A sketch as a user would draw it (roughly placed) plus what the solver must report. NaN area = no area check.
struct SketchSpecimen
{
    std::string                          Name                     = {};                      // [-]    - Catalogue key
    std::string                          Description              = {};                      // [-]    - One line
    SketchModel                          Sketch                   = {};                      // [-]    - Unsolved sketch
    int                                  ExpectedDegreesOfFreedom = 0;                       // [-]    - n − rank
    int                                  ExpectedRedundant        = 0;                       // [-]    - Consistent dependants
    int                                  ExpectedConflictGroups   = 0;                       // [-]    - Inconsistent groups
    std::vector<std::string>             ExpectedConflictLabels   = {};                      // [-]    - Labels that must appear in a group
    bool                                 ExpectedSingular         = false;                   // [-]    - Positional rank drop
    bool                                 ExpectedProfile          = false;                   // [-]    - Closed profile expected
    double                               ExpectedArea             = 0.0;                     // [len²] - Closed-form profile area (NaN: skip)
    std::vector<std::pair<int, Vector2>> ExpectedPositions        = {};                      // [len]  - Closed-form point positions
    std::vector<int>                     ExpectedFixedPoints      = {};                      // [-]    - Points that must be fixed ({−1}: all, {−2}: none)
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

[[nodiscard]] std::vector<SketchSpecimen> BuildSketchRepository();

// Rectangle shared with the feature catalogue: corners FirstPoint + 0 (low-left) … + 3 counter-clockwise, lines
// FirstCurve + 0 … + 3 (bottom, right, top, left) with identifiers FirstIdentifier … + 3, constrained H / V / H / V.
struct SketchRectangle
{
    int FirstPoint = 0;                                                                      // [-] - Low-left corner index
    int FirstCurve = 0;                                                                      // [-] - Bottom line index
};

SketchRectangle AppendSketchRectangle(SketchModel& Sketch, Vector2 Low, Vector2 High, int FirstIdentifier);

// Uniform scale of every position, radius and length dimension (validation of scale invariance).
[[nodiscard]] SketchModel ScaleSketch(const SketchModel& Sketch, double Factor);

} // namespace Frontier::GeometrySpace::Parametric
