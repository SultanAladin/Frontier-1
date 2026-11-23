//============================================================================================================================================
//                                                    SURFACESPECIFICATION.CPP
//============================================================================================================================================

#include "SlateShape/Geometry/CurveSpecification/Api/CurveSpecification.h"
#include "SlateShape/Geometry/SurfaceSpecification/Api/SurfaceSpecification.h"

namespace Slate
{

namespace
{
    bool PatchDeclared(const PatchSurface& Declared, bool Rational)
    {
        if (Declared.ControlRows.size() < 2u)
            return false;
        if (Declared.DegreeAlong < 1u || Declared.DegreeAcross < 1u)
            return false;

        const std::size_t ColumnCount = Declared.ControlRows.front().ControlPoints.size();
        if (ColumnCount < 2u)
            return false;

        for (const PatchControlRow& Row : Declared.ControlRows)
        {
            if (Row.ControlPoints.size() != ColumnCount)
                return false;
            if (Rational && Row.Weights.size() != ColumnCount)
                return false;
            if (!Rational && !Row.Weights.empty())
                return false;
        }

        return Declared.DegreeAlong < Declared.ControlRows.size()
            && Declared.DegreeAcross < ColumnCount;
    }
}

SurfaceSpecification SurfaceSpecification::DeclarePlane(const PlaneSurface& Declared,
                                                        const SurfaceParameterRange& Range)
{
    SurfaceSpecification Held;
    Held.HeldSubject = SurfaceForm::Plane;
    Held.HeldRange = Range;
    Held.Plane = Declared;
    return Held;
}

SurfaceSpecification SurfaceSpecification::DeclareCylinder(const CylinderSurface& Declared,
                                                           const SurfaceParameterRange& Range)
{
    SurfaceSpecification Held;
    Held.HeldSubject = SurfaceForm::Cylinder;
    Held.HeldRange = Range;
    Held.Cylinder = Declared;
    return Held;
}

SurfaceSpecification SurfaceSpecification::DeclareCone(const ConeSurface& Declared,
                                                       const SurfaceParameterRange& Range)
{
    SurfaceSpecification Held;
    Held.HeldSubject = SurfaceForm::Cone;
    Held.HeldRange = Range;
    Held.Cone = Declared;
    return Held;
}

SurfaceSpecification SurfaceSpecification::DeclareSphere(const SphereSurface& Declared,
                                                         const SurfaceParameterRange& Range)
{
    SurfaceSpecification Held;
    Held.HeldSubject = SurfaceForm::Sphere;
    Held.HeldRange = Range;
    Held.Sphere = Declared;
    return Held;
}

SurfaceSpecification SurfaceSpecification::DeclareTorus(const TorusSurface& Declared,
                                                        const SurfaceParameterRange& Range)
{
    SurfaceSpecification Held;
    Held.HeldSubject = SurfaceForm::Torus;
    Held.HeldRange = Range;
    Held.Torus = Declared;
    return Held;
}

SurfaceSpecification SurfaceSpecification::DeclareLinearExtrusion(const LinearExtrusionSurface& Declared,
                                                                  const SurfaceParameterRange& Range)
{
    SurfaceSpecification Held;
    Held.HeldSubject = SurfaceForm::LinearExtrusion;
    Held.HeldRange = Range;
    Held.Extrusion = Declared;
    return Held;
}

SurfaceSpecification SurfaceSpecification::DeclareBezierPatch(const PatchSurface& Declared,
                                                              const SurfaceParameterRange& Range)
{
    SurfaceSpecification Held;
    Held.HeldSubject = SurfaceForm::BezierPatch;
    Held.HeldRange = Range;
    Held.Patch = Declared;
    return Held;
}

SurfaceSpecification SurfaceSpecification::DeclareRationalPatch(const PatchSurface& Declared,
                                                                const SurfaceParameterRange& Range)
{
    SurfaceSpecification Held;
    Held.HeldSubject = SurfaceForm::RationalPatch;
    Held.HeldRange = Range;
    Held.Patch = Declared;
    return Held;
}

bool SurfaceSpecification::Declared() const
{
    if (!HeldRange.Declared())
        return false;

    switch (HeldSubject)
    {
        case SurfaceForm::Plane:
            return LengthSquared(Plane.Normal) > 0.0 && LengthSquared(Plane.AlongDirection) > 0.0;

        case SurfaceForm::Cylinder:
            return Cylinder.Radius > 0.0
                && LengthSquared(Cylinder.Axis) > 0.0
                && LengthSquared(Cylinder.RadialDirection) > 0.0;

        case SurfaceForm::Cone:
            return LengthSquared(Cone.Axis) > 0.0 && Cone.HalfAngleRadians > 0.0;

        case SurfaceForm::Sphere:
            return Sphere.Radius > 0.0;

        case SurfaceForm::Torus:
            return LengthSquared(Torus.Axis) > 0.0
                && Torus.MajorRadius > 0.0
                && Torus.MinorRadius > 0.0;

        case SurfaceForm::LinearExtrusion:
            return Extrusion.SectionCurve.Declared() && LengthSquared(Extrusion.Direction) > 0.0;

        case SurfaceForm::BezierPatch:
            return PatchDeclared(Patch, false);

        case SurfaceForm::RationalPatch:
            return PatchDeclared(Patch, true);

        case SurfaceForm::SubjectCount:
            return false;
    }

    return false;
}

} // namespace Slate
