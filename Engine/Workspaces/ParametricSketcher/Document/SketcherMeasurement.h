/*====================================================================================================================================
                                                        SOLIDARCMEASUREMENT.H
====================================================================================================================================*/
// 🧩 Inspect: exact measurement of the selection (mass properties, areas, lengths, minimum distance, angles) and the exact
//    plane section of the shown solid bodies, both over the GeometryProcessing Measurement division
#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                    INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Workspaces/ParametricSketcher/Document/SketcherDocument.h"

#include <array>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

namespace Frontier::Workspaces::Sketcher
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

inline constexpr double SketcherUnmeasured = std::numeric_limits<double>::quiet_NaN();      // [-] - A figure the subject lacks

//------------------------------------------------------------------------------------------------------------------------
//                                                    TYPES
//------------------------------------------------------------------------------------------------------------------------

// 📝 One Inspector row: a caption and its formatted value (Number keeps the raw figure for scripts and gates).
struct SketcherMeasureRow
{
    std::string Caption = {};                                                                // [-] - "Area"
    std::string Value   = {};                                                                // [-] - "6.0000 m²"
    double      Number  = 0.0;                                                               // [-] - Raw figure (NaN when textual)
};

// 💡 What Measure (I) reports for the current selection (Fusion Inspect → Measure; Plasticity has none):
//    nothing / mode-4 body → mass properties · 1 face → area, perimeter, carrier · 1 edge → length, carrier ·
//    2 entities → minimum distance (+ angle when both carry a direction) · more → total area / length.
//    2 solid bodies (mode 4) → their totals plus interference (Assembly/AssemblyAnalysis.h): clear with the clearance
//    and its closest points, contact, overlap with the shared volume and its edges, or one contained in the other.
//    Figures the subject lacks stay NaN; the overlay reads the point fields.
struct SketcherMeasurement
{
    uint64_t                        Revision          = 0;                                   // [-]    - Document revision measured
    std::string                     SelectionKey      = {};                                  // [-]    - Selection it describes (cache key)
    std::string                     Subject           = {};                                  // [-]    - "Body Block1", "Face F3", "Distance"
    std::vector<SketcherMeasureRow> Rows              = {};                                  // [-]    - Inspector rows, in order
    double                          Distance          = SketcherUnmeasured;                  // [len]  - Minimum distance (2 entities)
    double                          Angle             = SketcherUnmeasured;                  // [deg]  - Angle between the directions
    double                          Area              = SketcherUnmeasured;                  // [len²] - Face area / total / body surface
    double                          Length            = SketcherUnmeasured;                  // [len]  - Edge length / total
    double                          Volume            = SketcherUnmeasured;                  // [len³] - Body volume
    bool                            DistanceCondition = false;                               // [-]    - First / Second hold closest points
    GeometrySpace::Parametric::Vector3               First             = {};                                  // [len]  - Closest point on the first entity
    GeometrySpace::Parametric::Vector3               Second            = {};                                  // [len]  - Closest point on the second entity
    bool                            CentroidCondition = false;                               // [-]    - Centroid is meaningful
    GeometrySpace::Parametric::Vector3               Centroid          = {};                                  // [len]  - Body / face centroid
    std::string                     Interference      = {};                                  // [-]    - 2 bodies: Clear / Contact / Overlap / Contained
    double                          OverlapVolume     = SketcherUnmeasured;                  // [len³] - 2 bodies: shared volume
    std::vector<std::vector<GeometrySpace::Parametric::Vector3>> Overlap = {};                                // [len]  - 2 bodies: edges of the shared volume (overlay)
    double                          Milliseconds      = 0.0;                                 // [ms]   - Measurement time
    std::string                     Refusal           = {};                                  // [-]    - Empty on success
};

// 💡 Exact section of every shown solid body (sheets skipped): the kernel SectionBody per body, figures combined —
//    areas, perimeters, regions and loops add; the centroid is area-weighted; Iuu Ivv Iuv are moved to the common
//    centroid by the parallel-axis theorem (u = Plane.XAxis, v = Plane.YAxis).
struct SketcherSection
{
    uint64_t                                    Revision     = 0;                            // [-]    - Document revision cut
    std::string                                 RequestKey   = {};                           // [-]    - Request it describes (cache key)
    GeometrySpace::Parametric::OrthonormalFrame                  Plane        = {};                           // [-]    - Cut plane (ZAxis = kept side)
    bool                                        PlaneCondition = false;                      // [-]    - Plane resolved (face found, planar)
    double                                      Area         = 0.0;                          // [len²] - Σ region areas
    double                                      Perimeter    = 0.0;                          // [len]  - Σ boundary lengths
    GeometrySpace::Parametric::Vector3                           Centroid     = {};                           // [len]  - Area centroid (world)
    std::array<double, 3>                       SecondMoments = {};                          // [len⁴] - Iuu Ivv Iuv about Centroid
    int                                         Regions      = 0;                            // [-]    - Disjoint pieces
    int                                         Loops        = 0;                            // [-]    - Outer loops + holes
    int                                         Bodies       = 0;                            // [-]    - Bodies the plane crosses
    int                                         Sheets       = 0;                            // [-]    - Sheet bodies skipped
    std::vector<std::vector<GeometrySpace::Parametric::Vector3>> Outline      = {};                           // [len]  - Boundary polylines (all bodies)
    double                                      Milliseconds = 0.0;                          // [ms]   - Cuts + integrals
    std::string                                 Refusal      = {};                           // [-]    - Empty on success
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// Exact measurement of the current selection; with nothing selected, the active body (the only body when there is one).
[[nodiscard]] SketcherMeasurement MeasureSketcherSelection(const SketcherDocument& Document);

// Selection fingerprint (faces, edges, bodies, in order) — Measure recomputes only when it or the revision changes.
[[nodiscard]] std::string SketcherSelectionKey(const SketcherSelection& Selection);

// Plane of a request (cheap: no cut). PlaneCondition is false when a Face request names no planar face.
[[nodiscard]] SketcherSection ResolveSketcherSectionPlane(const SketcherDocument& Document, const SketcherSectionRequest& Request);

// Exact section of the shown solid bodies by the request plane (OutlineStep = polyline spacing, 0 = automatic).
[[nodiscard]] SketcherSection SectionSketcherDocument(const SketcherDocument& Document, const SketcherSectionRequest& Request, double OutlineStep = 0.0);

// Request fingerprint (cache key and script readout).
[[nodiscard]] std::string SketcherSectionKey(const SketcherSectionRequest& Request);

} // namespace Frontier::Workspaces::Sketcher
