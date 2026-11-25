/*====================================================================================================================================
                                                            SECTIONLOFT.H
====================================================================================================================================*/
// 🧩 Loft (skinning) through section curves: compatible sections → interpolation across them → one exact NURBS sheet face

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                    INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometrySpace/Parametric/Feature/ProfileSweep.h"
#include "Engine/GeometrySpace/Parametric/Geometry/NurbsCarrier.h"

#include <string>
#include <vector>

namespace Frontier::GeometrySpace::Parametric
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    TYPES
//------------------------------------------------------------------------------------------------------------------------

// 📝 Continuity of one loft end with a neighbouring panel that has the end section as one of its boundaries:
//    Order 0 = G0 (position only), 1 = G1 (tangent plane), 2 = G2 (curvature).
struct LoftContinuity
{
    int          Order     = 0;                                                              // [-] - 0 G0 · 1 G1 · 2 G2
    NurbsSurface Reference = {};                                                             // [-] - Neighbour panel on [0, 1]² (non-rational)
};

struct LoftSettings
{
    int            DegreeAcross    = 3;                                                      // [-] - Degree in 𝑣 (lowered to sections − 1)
    bool           AlignCondition  = true;                                                   // [-] - Reverse open sections running against the first
    LoftContinuity Start           = {};                                                     // [-] - Condition at the first section
    LoftContinuity End             = {};                                                     // [-] - Condition at the last section
    std::vector<double> Parameters = {};                                                     // [-] - Fixed 𝑣ₖ (0 … 1, increasing); empty: averaged chord length
};

struct LoftSurfaceOutcome
{
    NurbsSurface            Surface            = {};                                         // [-] - 𝑢 along the sections, 𝑣 across, both on [0, 1]
    std::vector<NurbsCurve> Sections           = {};                                         // [-] - Compatible sections (same degree and knots)
    std::vector<double>     SectionParameters  = {};                                         // [-] - 𝑣ₖ where section k is interpolated
    int                     Reversed           = 0;                                          // [-] - Sections turned to agree with the first
    bool                    ClosedCondition    = false;                                      // [-] - Every section closed (a tube: one seam)
    int                     Elevated           = 0;                                          // [-] - Sections raised to the common degree
    double                  StartScale         = 0.0;                                        // [-] - α at the start condition (0: none)
    double                  EndScale           = 0.0;                                        // [-] - α at the end condition (0: none)
    std::string             Refusal            = {};                                         // [-] - "Loft: …" when refused
};

struct LoftSheetOutcome
{
    LoftSurfaceOutcome Loft  = {};                                                           // [-] - The carrier and its sections
    SweepOutcome       Sheet = {};                                                           // [-] - Sheet body (one face) and its role
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// 💡 Skinning (Piegl & Tiller § 10.3): sections mapped onto 𝑢 ∈ [0, 1], made compatible by merging their knot vectors
//    (Boehm insertion — exact), 𝑣ₖ from the averaged chord length of each pole column, averaged 𝑣-knots, then each
//    pole column interpolated in homogeneous coordinates so S(𝑢, 𝑣ₖ) ≡ section k (rational sections stay exact: circles
//    loft into true cylinders and cones). Sections of lower degree are raised exactly (Greville collocation).
//    Continuity (vehicle plan V6): an end section shared with a Reference panel boundary gets, per pole column, the
//    reference's cross derivatives as end conditions — ∂𝑣 = σ·α·R𝑣 (G1) and ∂𝑣𝑣 = α²·R𝑣𝑣 (G2), α the ratio of the
//    across-chord lengths, σ the side — after the reference is turned to run with the section and both are refined
//    to one 𝑢-knot vector (exact), so the shared rows coincide and the panels meet G1 / G2 to rounding. Refuses fewer
//    than 2 sections, periodic carriers, mixed open / closed sections, coincident sections, a non-positive
//    interpolated weight, a reference that does not carry the end section as a boundary, or rational continuity.
[[nodiscard]] LoftSurfaceOutcome LoftSectionSurface(const std::vector<NurbsCurve>& Sections, const LoftSettings& Settings);

// 💡 The loft as a sheet body: one NURBS face bounded by the first and last sections and the two 𝑢-boundary iso-curves
//    (closed sections share one seam edge used twice), every boundary carried by an exact iso-line chart curve.
[[nodiscard]] LoftSheetOutcome LoftSectionSheet(ModelPartition& Partition, const std::vector<NurbsCurve>& Sections, const LoftSettings& Settings);

// 💡 Singular Coons patch over 3 open curves meeting end-to-end at 3 distinct corners (𝐴 → 𝐵, 𝐵 → 𝐶, 𝐴 → 𝐶), with
//    𝑣 = 1 collapsed onto corner 𝐶 so AssemblePanels omits that singular row and yields one exact 3-sided B-Rep face.
[[nodiscard]] LoftSurfaceOutcome LoftTriangularLoop(const std::vector<NurbsCurve>& Curves, double CornerTolerance = 1.0e-4);

}
