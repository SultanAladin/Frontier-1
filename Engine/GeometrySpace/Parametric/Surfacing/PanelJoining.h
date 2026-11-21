/*====================================================================================================================================
                                                         PANELASSEMBLY.H
====================================================================================================================================*/
// 🧩 Panels → body (vehicle plan V7): sewing of coincident boundaries, mirror copies, planar caps, sheet or solid

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                    INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometrySpace/Parametric/Geometry/SurfaceGeometry.h"
#include "Engine/GeometrySpace/Parametric/Connectivity/ModelPartition.h"

#include <string>
#include <vector>

namespace Frontier::GeometrySpace::Parametric
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    TYPES
//------------------------------------------------------------------------------------------------------------------------

// 📝 A panel is a NURBS patch on [0, 1]² used whole (its four iso-boundaries are its edges); first = last pole column
//    makes it closed in 𝑢 (one seam edge used twice). Its natural normal S𝑢 × S𝑣 is its outward side.
struct SurfacePanel
{
    NurbsSurface Surface = {};                                                               // [-] - Carrier on [0, 1]²
};

struct PanelJoiningSettings
{
    double SewingGap    = 1.0e-6;                                                            // [len] - Boundaries closer than this are one edge
    bool   CapCondition = false;                                                             // [-]   - Close planar rim loops with plane faces → solid
    double PlanarGap    = 1.0e-7;                                                            // [len] - Rim loop off its plane by more: no cap (refused)
};

struct PanelJoiningOutcome
{
    BodyToken              Body             = {};                                            // [-]   - New body
    std::vector<FaceToken> Faces            = {};                                            // [-]   - Per panel
    std::vector<FaceToken> Caps             = {};                                            // [-]   - Plane faces closing rim loops
    int                    SewnEdges        = 0;                                             // [-]   - Boundaries shared by two panels
    int                    LaminarEdges     = 0;                                             // [-]   - Boundaries left open (sheet rim)
    bool                   SolidCondition   = false;                                         // [-]   - Closed: a solid body
    bool                   FlippedCondition = false;                                         // [-]   - Panels turned so the closed body faces outward
    double                 Volume           = 0.0;                                           // [len³] - Enclosed volume when solid (quadrature)
    std::string            Refusal          = {};                                            // [-]   - Non-empty when refused
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// 💡 ① Corners merge within SewingGap; ② two panel boundaries with the same corners that coincide point-for-point at
//    equal (or mirrored) τ become one edge used by both — a boundary coinciding only geometrically (different
//    parametrisation) is refused (🚧), as is a boundary claimed by three panels; ③ with CapCondition, the remaining
//    rim loops must be planar within PlanarGap and each gets a plane face (exact SP-curves by projection); the signed
//    enclosed volume decides the outward side (all panels turn in 𝑢 when it is negative). Unclosed rims make a sheet.
[[nodiscard]] PanelJoiningOutcome AssemblePanels(ModelPartition& Partition, const std::vector<SurfacePanel>& Panels, const PanelJoiningSettings& Settings,
                                                  const std::string& Label);

// Mirror copy through the plane (PlanePoint, PlaneNormal), 𝑢 reversed so the copy's natural normal is the mirror image of
// the original's (a reflection alone would turn it inward): halves sharing a boundary on the plane sew consistently.
[[nodiscard]] SurfacePanel MirrorPanel(const SurfacePanel& Panel, Vector3 PlanePoint, Vector3 PlaneNormal);

} // namespace Frontier::GeometrySpace::Parametric
