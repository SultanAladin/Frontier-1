//============================================================================================================================================
//                                                      SURFACESPECIFICATION.H
//============================================================================================================================================
// 🧩 Exact supporting-surface declarations for the CAD kernel, apart from trims, document ownership and GPU
//    presentation. A face later names one of these and declares how its loops trim it.

#pragma once

#include "SlateShape/Geometry/CurveSpecification/Api/CurveSpecification.h"

#include <cstdint>
#include <vector>

namespace Slate
{

struct SurfaceName
{
    std::uint32_t IssuedIndex = 0u;
    bool Assigned() const { return IssuedIndex != 0u; }
};

struct SurfaceParameterRange
{
    ParameterInterval Along = {};
    ParameterInterval Across = {};
    bool Declared() const { return Along.Declared() && Across.Declared(); }
};

enum class SurfaceForm : std::uint32_t
{
    Plane = 0u,
    Cylinder = 1u,
    Cone = 2u,
    Sphere = 3u,
    Torus = 4u,
    LinearExtrusion = 5u,
    BezierPatch = 6u,
    RationalPatch = 7u,
    SubjectCount = 8u
};

struct PlaneSurface
{
    SpatialPoint Origin = {};
    SpatialDirection Normal = {};
    SpatialDirection AlongDirection = {};
};

struct CylinderSurface
{
    SpatialPoint Origin = {};
    SpatialDirection Axis = {};
    SpatialDirection RadialDirection = {};
    double Radius = 0.0;
};

struct ConeSurface
{
    SpatialPoint Apex = {};
    SpatialDirection Axis = {};
    double HalfAngleRadians = 0.0;
};

struct SphereSurface
{
    SpatialPoint Centre = {};
    double Radius = 0.0;
};

struct TorusSurface
{
    SpatialPoint Centre = {};
    SpatialDirection Axis = {};
    double MajorRadius = 0.0;
    double MinorRadius = 0.0;
};

struct LinearExtrusionSurface
{
    CurveSpecification SectionCurve = {};
    SpatialDirection Direction = {};
};

struct PatchControlRow
{
    std::vector<SpatialPoint> ControlPoints = {};
    std::vector<double> Weights = {};
};

struct PatchSurface
{
    std::vector<PatchControlRow> ControlRows = {};
    std::uint32_t DegreeAlong = 0u;
    std::uint32_t DegreeAcross = 0u;
};

struct SurfaceTrimRelation
{
    CurveName TraversedCurve = {};
    bool SameSense = true;
};

class SurfaceSpecification
{
public:
    static SurfaceSpecification DeclarePlane(const PlaneSurface& Declared,
                                             const SurfaceParameterRange& Range);
    static SurfaceSpecification DeclareCylinder(const CylinderSurface& Declared,
                                                const SurfaceParameterRange& Range);
    static SurfaceSpecification DeclareCone(const ConeSurface& Declared,
                                            const SurfaceParameterRange& Range);
    static SurfaceSpecification DeclareSphere(const SphereSurface& Declared,
                                              const SurfaceParameterRange& Range);
    static SurfaceSpecification DeclareTorus(const TorusSurface& Declared,
                                             const SurfaceParameterRange& Range);
    static SurfaceSpecification DeclareLinearExtrusion(const LinearExtrusionSurface& Declared,
                                                       const SurfaceParameterRange& Range);
    static SurfaceSpecification DeclareBezierPatch(const PatchSurface& Declared,
                                                   const SurfaceParameterRange& Range);
    static SurfaceSpecification DeclareRationalPatch(const PatchSurface& Declared,
                                                     const SurfaceParameterRange& Range);

    SurfaceForm Subject() const { return HeldSubject; }
    const SurfaceParameterRange& Range() const { return HeldRange; }
    const PlaneSurface& HeldPlane() const { return Plane; }
    const CylinderSurface& HeldCylinder() const { return Cylinder; }
    const ConeSurface& HeldCone() const { return Cone; }
    const SphereSurface& HeldSphere() const { return Sphere; }
    const TorusSurface& HeldTorus() const { return Torus; }
    const LinearExtrusionSurface& HeldLinearExtrusion() const { return Extrusion; }
    const PatchSurface& HeldPatch() const { return Patch; }
    bool Declared() const;

private:
    SurfaceForm HeldSubject = SurfaceForm::Plane;
    SurfaceParameterRange HeldRange = {};
    PlaneSurface Plane = {};
    CylinderSurface Cylinder = {};
    ConeSurface Cone = {};
    SphereSurface Sphere = {};
    TorusSurface Torus = {};
    LinearExtrusionSurface Extrusion = {};
    PatchSurface Patch = {};
};

} // namespace Slate
