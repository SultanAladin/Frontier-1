/*====================================================================================================================================
                                                          PATHSWEEP.H
====================================================================================================================================*/
// 🧩 Path verbs reduced to exact constructions — pipe along a line / arc, sweep along a line, loft of coaxial circles (G2)

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                    INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometrySpace/Parametric/Feature/ProfileSweep.h"
#include "Engine/GeometrySpace/Parametric/Geometry/SurfaceGeometry.h"

#include <string>
#include <vector>

namespace Frontier::GeometrySpace::Parametric
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    STRUCTS
//------------------------------------------------------------------------------------------------------------------------

struct PipeSettings
{
    double ToleranceRatio = 1.0e-4;                                                          // [-] - Allowed radial deviation / radius
    int    SectionLimit   = 513;                                                             // [-] - Most sections before refusing
    int    FirstSections  = 9;                                                               // [-] - Sections of the first attempt (doubles)
};

// 📝 A tube along a free-form path: one NURBS side face skinned through section circles, two plane caps.
struct PipeOutcome
{
    BodyToken              Body      = {};                                                   // [-]   - Closed solid
    FaceToken              Side      = {};                                                   // [-]   - The skinned face
    std::vector<FaceToken> Caps      = {};                                                   // [-]   - Start cap, end cap
    NurbsSurface           Surface   = {};                                                   // [-]   - Side carrier
    int                    Sections  = 0;                                                    // [-]   - Circles skinned
    double                 Deviation = 0.0;                                                  // [len] - Largest | |S − C(τ*)| − r | measured
    double                 Length    = 0.0;                                                  // [len] - Path length
    double                 Bend      = 0.0;                                                  // [-]   - r · κmax (must stay < 1)
    std::string            Refusal   = {};                                                   // [-]   - "Pipe: …" when refused
};

struct EvolutionSettings
{
    double ToleranceRatio = 1.0e-5;                                                          // [-] - Allowed deviation / profile scale
    int    SectionLimit   = 513;                                                             // [-] - Most stations before refusing
    int    FirstSections  = 9;                                                               // [-] - Stations of the first attempt (doubles)
};

// 📝 A sketch profile swept along a path (Plasticity Evolution): one NURBS side per profile curve, plane caps.
struct EvolutionOutcome
{
    SweepOutcome Sweep     = {};                                                             // [-]   - Body, faces and roles (Side(curve), caps)
    int          Sections  = 0;                                                              // [-]   - Stations skinned
    double       Deviation = 0.0;                                                            // [len] - Largest skin − true sweep gap measured midway
    double       Length    = 0.0;                                                            // [len] - Path length
    double       Bend      = 0.0;                                                            // [-]   - Reach · κmax (must stay < 1)
    std::string  Refusal   = {};                                                             // [-]   - "Evolution: …" when refused
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// 💡 Circle of Radius swept along Path over τ ∈ [Low, High], the section ⟂ the path: a line gives a cylinder, an arc a
//    partial torus (= the section circle revolved about the arc axis by High − Low; plan G2). Other paths 🚧.
[[nodiscard]] SweepOutcome PipeAlongPath(ModelPartition& Partition, const CurveGeometry& Path, double Low, double High, double Radius);

// 💡 Circle of Radius swept along a free-form NURBS path (Plasticity P on a curve). The section frames are rotation
//    minimising (double reflection, Wang et al. 2008, on 4096 path samples), so the tube does not twist; section
//    circles (exact rational) at equal arc-length stations are skinned (LoftSectionSurface) and the station count
//    doubles until the radial deviation between stations is within ToleranceRatio·r — the side is a NURBS
//    approximation of the true canal surface, the caps are exact plane discs ⟂ the path. Refused for closed paths
//    (🚧), a non-positive radius, r·κ ≥ 1 anywhere (the tube would fold over itself) or no convergence.
[[nodiscard]] PipeOutcome PipeAlongCurve(ModelPartition& Partition, const NurbsCurve& Path, double Radius, const PipeSettings& Settings = {});

// 💡 Evolution (Plasticity Shift+P): the profile on Plane carried rigidly along Path by its rotation-minimising frames
//    (double reflection on 4096 samples, as the pipe), keeping its pose relative to the path start — it need not be ⟂
//    the path or touch it. Each profile curve (line, arc, circle, spline: exact NURBS sections, rigid images stay exact)
//    is skinned through equal arc-length stations with one shared 𝑣-parametrisation, so neighbouring sides share their
//    rails bit-for-bit; stations double until every side is within ToleranceRatio·scale of the true sweep midway. The
//    caps are the exact profile and its rigid image, holes included. Refused: closed or cusped paths, a path leaving
//    along the profile plane, a profile reaching ≥ 1/κmax from the path start (the sweep would fold), no convergence.
[[nodiscard]] EvolutionOutcome EvolveProfile(ModelPartition& Partition, const SketchProfile& Profile, const OrthonormalFrame& Plane, const NurbsCurve& Path,
                                             const EvolutionSettings& Settings = {});
// Profile on Plane swept along Displacement: a displacement ∥ the plane normal is an extrude; oblique ones 🚧.
[[nodiscard]] SweepOutcome SweepProfileAlongLine(ModelPartition& Partition, const SketchProfile& Profile, const OrthonormalFrame& Plane, Vector3 Displacement);

// 💡 Ruled loft between two circles: coaxial ones (both normals ∥ the centre line) give a cylinder or a frustum.
//    Others are skinned surfaces outside the carrier kinds (🚧).
[[nodiscard]] SweepOutcome LoftCircles(ModelPartition& Partition, Vector3 FirstCentre, Vector3 FirstNormal, double FirstRadius,
                                       Vector3 SecondCentre, Vector3 SecondNormal, double SecondRadius);

} // namespace Frontier::GeometrySpace::Parametric
