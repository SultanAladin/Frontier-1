/*====================================================================================================================================
                                                   INTERSECTIONCATALOGUE.H
====================================================================================================================================*/
// 🧩 Named surface–surface intersection scenes with independent expectations (counts, special points, exact length)

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                    INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometrySpace/Parametric/Inspection/SpecimenRepository.h"
#include "Engine/GeometrySpace/Parametric/Intersection/SurfaceIntersection.h"

#include <limits>
#include <map>
#include <string>
#include <vector>

namespace Frontier::GeometrySpace::Parametric
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 −1 / NaN = no independent expectation (the reference lattice still audits completeness).
struct IntersectionExpectation
{
    int    Branches       = -1;                                                              // [-]   - Branch count
    int    ClosedBranches = -1;                                                              // [-]   - Of which closed
    int    SpecialPoints  = -1;                                                              // [-]   - Singular + touch points
    double Length         = std::numeric_limits<double>::quiet_NaN();                        // [len] - Total exact length
};

struct IntersectionScene
{
    std::string                   Refusal     = {};                                          // [-] - Non-empty: unknown scene
    std::string                   Description = {};                                          // [-] - What the scene exercises
    SurfaceGeometry               First       = PlaneSurface{};                              // [-] - Lattice surface
    ParameterBox                  FirstBox    = {};                                          // [-] - Its chart box
    SurfaceGeometry               Second      = PlaneSurface{};                              // [-] - Other surface
    ParameterBox                  SecondBox   = {};                                          // [-] - Its chart box
    SurfaceIntersectionSettings   Settings    = {};                                          // [-] - Solver settings
    IntersectionExpectation       Expectation = {};                                          // [-] - Independent expectations
    std::map<std::string, double> Parameters  = {};                                          // [-] - Parameters actually used
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

[[nodiscard]] std::vector<std::string> EnumerateIntersectionScenes();

[[nodiscard]] bool IntersectionSceneCondition(const std::string& Name);

// Common keys on every scene: MarchingOnly (0/1), LoopDetection (0/1), ChordTolerance, Lattice.
[[nodiscard]] IntersectionScene ConstructIntersectionScene(const std::string& Name, const SpecimenParameters& Parameters);

// Composite 8-point Gauss–Legendre arc length of a parametric curve from its speed |r′(t)| (the length references).
template <typename SpeedFunction>
[[nodiscard]] double IntegrateArcLength(SpeedFunction Speed, double Low, double High, int Panels = 512)
{
    constexpr double Abscissae[8] = { -0.9602898564975363, -0.7966664774136267, -0.5255324099163290, -0.1834346424956498,
                                     0.1834346424956498,  0.5255324099163290,  0.7966664774136267,  0.9602898564975363 };
    constexpr double Weights[8] = {  0.1012285362903763,  0.2223810344533745,  0.3137066458778873,  0.3626837833783620,
                                     0.3626837833783620,  0.3137066458778873,  0.2223810344533745,  0.1012285362903763 };
    double Total = 0.0;                                                                      // [len] - Accumulated length
    const double Width = (High - Low) / Panels;                                              // [-]   - Panel width
    for (int Panel = 0; Panel < Panels; ++Panel)
    {
        const double Middle = Low + (Panel + 0.5) * Width;
        for (int Abscissa = 0; Abscissa < 8; ++Abscissa) Total += Weights[Abscissa] * Speed(Middle + 0.5 * Width * Abscissae[Abscissa]) * 0.5 * Width;
    }
    return Total;
}

} // namespace Frontier::GeometrySpace::Parametric
