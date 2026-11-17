/*====================================================================================================================================
                                                         PROFILESWEEP.H
====================================================================================================================================*/
// 🧩 Solids from sketch profiles: linear extrude (planes, cylinders, holes) and revolve, full or partial (plane … torus, poles)

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                    INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometrySpace/Parametric/Construction/BodyAssembly.h"
#include "Engine/GeometrySpace/Parametric/Sketch/SketchProfile.h"

#include <numbers>
#include <string>
#include <vector>

namespace Frontier::GeometrySpace::Parametric
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    STRUCTS
//------------------------------------------------------------------------------------------------------------------------

enum class SweepFaceRole : uint8_t
{
    Side     = 0,                                                                            // Swept by one profile curve
    StartCap = 1,                                                                            // Extrude cap on the sketch plane
    EndCap   = 2,                                                                            // Extrude cap at the far end
};

// 📝 Feature-semantic role of a result face (research § L 🥉): which profile curve swept it, or which cap it is.
struct SweepFace
{
    SweepFaceRole Role  = SweepFaceRole::Side;                                               // [-] - Side / cap
    std::string   Curve = {};                                                                // [-] - Sketch curve name (sides)
    int           Loop  = 0;                                                                 // [-] - Profile loop index (caps: outer loop)
};

struct SweepOutcome
{
    BodyToken              Body    = {};                                                     // [-] - New solid
    std::string            Refusal = {};                                                     // [-] - "Extrude: …" / "Revolve: …" when refused
    std::vector<FaceToken> Faces   = {};                                                     // [-] - Result faces
    std::vector<SweepFace> Roles   = {};                                                     // [-] - Role per entry of Faces
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// 💡 Extrude: the profile on Plane (sketch x → XAxis, y → YAxis) swept along ZAxis·Extent (negative: below the
//    plane). Lines → planar sides, arcs / circles → cylinders (full circles get a seam), one cap pair per outer loop with
//    its holes; multiple outer loops make multiple solid regions. Without caps (CapsCondition false: an unfilled profile)
//    the side walls alone make an open sheet of zero thickness.
[[nodiscard]] SweepOutcome ExtrudeProfile(ModelPartition& Partition, const SketchProfile& Profile, const OrthonormalFrame& Plane, double Extent, bool CapsCondition = true);

// 💡 Revolve by Angle (default 360°) about the sketch line through AxisPoint along AxisDirection, turning from the sketch
//    plane toward the side the right-hand rule about the axis gives: segments ⟂ axis → planes (disc / annulus / sector),
//    ∥ axis → cylinders, oblique → cones (apex vertex on the axis), arcs centred on the axis → spheres (poles), other arcs
//    → tori, a full circle off the axis → a torus, splines → exact NURBS surfaces of revolution (rational circles). Segments on the axis vanish (partial: they bound both caps); the
//    profile must not cross the axis. Partial turns add planar caps at 𝑢 = 0 and 𝑢 = Angle (G2: pipe on an arc).
[[nodiscard]] SweepOutcome RevolveProfile(ModelPartition& Partition, const SketchProfile& Profile, const OrthonormalFrame& Plane, Vector2 AxisPoint, Vector2 AxisDirection,
                                          double Angle = 2.0 * std::numbers::pi);

// "Side(Line7)", "Cap(Start)", "Cap(End)".
[[nodiscard]] std::string SweepRoleName(const SweepFace& Role);

} // namespace Frontier::GeometrySpace::Parametric
