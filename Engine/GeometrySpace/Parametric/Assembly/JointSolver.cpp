/*====================================================================================================================================
                                                       JOINTSOLVER.CPP
====================================================================================================================================*/
// 🧩 Assembly joints (priority 11, A1): joint connectors read exactly off body geometry and the placement each joint kind imposes

//------------------------------------------------------------------------------------------------------------------------
//                                                    INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometrySpace/Parametric/Assembly/JointSolver.h"

#include "Engine/GeometrySpace/Parametric/Geometry/CurveGeometry.h"
#include "Engine/GeometrySpace/Parametric/Geometry/SurfaceGeometry.h"
#include "Engine/GeometrySpace/Parametric/Measurement/MassProperties.h"

#include <cmath>
#include <cstdio>
#include <numbers>
#include <variant>

namespace Frontier::GeometrySpace::Parametric
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    INTERNAL
//------------------------------------------------------------------------------------------------------------------------

namespace
{
    // Rotation by Angle about a unit axis through the origin (Rodrigues).
    [[nodiscard]] AffineMap AxisRotation(Vector3 Axis, double Angle) noexcept
    {
        const double c = std::cos(Angle), s = std::sin(Angle), t = 1.0 - c;
        const Vector3 a = Axis;
        AffineMap Map;
        Map.ColumnX = { t * a.X * a.X + c,       t * a.X * a.Y + s * a.Z, t * a.X * a.Z - s * a.Y };
        Map.ColumnY = { t * a.X * a.Y - s * a.Z, t * a.Y * a.Y + c,       t * a.Y * a.Z + s * a.X };
        Map.ColumnZ = { t * a.X * a.Z + s * a.Y, t * a.Y * a.Z - s * a.X, t * a.Z * a.Z + c       };
        return Map;
    }

    // Outward unit normal of a plane face (surface normal times the face sense).
    [[nodiscard]] std::optional<Vector3> PlaneFaceNormal(const ModelPartition& Partition, FaceToken Face)
    {
        const FaceProfile& Profile = Partition.Faces.Resolve(Face);
        if (!Profile.Surface.PresentCondition()) return std::nullopt;
        const SurfaceGeometry& Surface = Partition.Surfaces.Resolve(Profile.Surface);
        const PlaneSurface* Plane = std::get_if<PlaneSurface>(&Surface);
        if (!Plane) return std::nullopt;
        return Profile.Sense == Orientation::Forward ? Plane->Frame.ZAxis : Plane->Frame.ZAxis * -1.0;
    }

    // First adjacent plane face of an edge whose normal is (anti)parallel to Axis, as its outward normal.
    [[nodiscard]] std::optional<Vector3> AdjacentPlaneNormal(const ModelPartition& Partition, const EdgeProfile& Edge, Vector3 Axis)
    {
        if (!Edge.RadialFin.PresentCondition()) return std::nullopt;
        FinToken Fin = Edge.RadialFin;
        for (int Guard = 0; Guard < 16; ++Guard)
        {
            const FinProfile& Use = Partition.Fins.Resolve(Fin);
            const FaceToken   Face = Partition.Loops.Resolve(Use.Loop).Face;
            if (const std::optional<Vector3> Normal = PlaneFaceNormal(Partition, Face); Normal && std::fabs(std::fabs(Dot(*Normal, Axis)) - 1.0) < 1.0e-9) return Normal;
            Fin = Use.RadialSuccessor;
            if (!Fin.PresentCondition() || Fin == Edge.RadialFin) break;
        }
        return std::nullopt;
    }

    // Any unit vector perpendicular to Axis (deterministic: the world axis least aligned with it, projected).
    [[nodiscard]] Vector3 Perpendicular(Vector3 Axis) noexcept
    {
        const Vector3 Seed = std::fabs(Axis.X) < 0.9 ? Vector3{ 1.0, 0.0, 0.0 } : Vector3{ 0.0, 1.0, 0.0 };
        return Normalize(Seed - Axis * Dot(Seed, Axis));
    }

    [[nodiscard]] std::string Degrees(double Angle)
    {
        char Text[32];
        std::snprintf(Text, sizeof Text, "%.4g°", Angle * 180.0 / std::numbers::pi);
        return Text;
    }

    [[nodiscard]] std::string Number(double Value)
    {
        char Text[32];
        std::snprintf(Text, sizeof Text, "%.6g", Value);
        return Text;
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    RIGID-MAP ALGEBRA
//------------------------------------------------------------------------------------------------------------------------

Vector3 MapDirection(const AffineMap& Map, Vector3 Direction) noexcept
{
    return Map.ColumnX * Direction.X + Map.ColumnY * Direction.Y + Map.ColumnZ * Direction.Z;
}

Vector3 MapPosition(const AffineMap& Map, Vector3 Position) noexcept
{
    return MapDirection(Map, Position) + Map.Shift;
}

AffineMap ComposeAffineMaps(const AffineMap& Outer, const AffineMap& Inner) noexcept
{
    AffineMap Map;
    Map.ColumnX = MapDirection(Outer, Inner.ColumnX);
    Map.ColumnY = MapDirection(Outer, Inner.ColumnY);
    Map.ColumnZ = MapDirection(Outer, Inner.ColumnZ);
    Map.Shift   = MapPosition(Outer, Inner.Shift);
    return Map;
}

AffineMap InvertRigidMap(const AffineMap& Map) noexcept
{
    // Mᵀ has the columns' components as rows.
    AffineMap Inverse;
    Inverse.ColumnX = { Map.ColumnX.X, Map.ColumnY.X, Map.ColumnZ.X };
    Inverse.ColumnY = { Map.ColumnX.Y, Map.ColumnY.Y, Map.ColumnZ.Y };
    Inverse.ColumnZ = { Map.ColumnX.Z, Map.ColumnY.Z, Map.ColumnZ.Z };
    Inverse.Shift   = MapDirection(Inverse, Map.Shift) * -1.0;
    return Inverse;
}

AffineMap FrameToWorldMap(const OrthonormalFrame& Frame) noexcept
{
    return { Frame.XAxis, Frame.YAxis, Frame.ZAxis, Frame.Origin };
}

OrthonormalFrame MapFrame(const AffineMap& Map, const OrthonormalFrame& Frame) noexcept
{
    return { MapPosition(Map, Frame.Origin), MapDirection(Map, Frame.XAxis), MapDirection(Map, Frame.YAxis), MapDirection(Map, Frame.ZAxis) };
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    CONNECTORS
//------------------------------------------------------------------------------------------------------------------------

JointConnector ResolveJointConnector(const ModelPartition& Partition, ReferenceKind Kind, uint32_t Slot)
{
    JointConnector Connector;
    if (Kind == ReferenceKind::Vertex)
    {
        const VertexToken Vertex = Partition.Vertices.TokenAt(Slot);
        if (!Partition.Vertices.Authenticate(Vertex)) { Connector.Refusal = "Joint: the vertex no longer exists"; return Connector; }
        Connector.Frame       = { Partition.Vertices.Resolve(Vertex).Position, { 1.0, 0.0, 0.0 }, { 0.0, 1.0, 0.0 }, { 0.0, 0.0, 1.0 } };
        Connector.Description = "vertex";
        return Connector;
    }
    if (Kind == ReferenceKind::Edge)
    {
        const EdgeToken Edge = Partition.Edges.TokenAt(Slot);
        if (!Partition.Edges.Authenticate(Edge)) { Connector.Refusal = "Joint: the edge no longer exists"; return Connector; }
        const EdgeProfile&   Profile = Partition.Edges.Resolve(Edge);
        const CurveGeometry& Curve   = Partition.Curves.Resolve(Profile.Curve);
        if (const std::optional<ConicForm> Conic = ResolveConicForm(Curve))
        {
            Vector3 Axis = Conic->Normal;
            if (const std::optional<Vector3> Outward = AdjacentPlaneNormal(Partition, Profile, Axis)) Axis = *Outward;
            Connector.Frame       = ConstructFrame(Conic->Centre, Axis, Conic->CosineAxis);
            Connector.Description = std::holds_alternative<CircularArc>(Curve) ? "circle centre" : "ellipse centre";
            return Connector;
        }
        if (const StraightLine* Line = std::get_if<StraightLine>(&Curve))
        {
            const Vector3 Middle = EvaluateCurve(Curve, 0.5 * (Profile.IntervalLow + Profile.IntervalHigh)).Position;
            Vector3 Reference = Perpendicular(Line->Direction);
            // Prefer the outward normal of an adjacent plane face as 𝑥 (it is perpendicular to the edge by construction).
            if (Profile.RadialFin.PresentCondition())
            {
                const FaceToken Face = Partition.Loops.Resolve(Partition.Fins.Resolve(Profile.RadialFin).Loop).Face;
                if (const std::optional<Vector3> Normal = PlaneFaceNormal(Partition, Face); Normal && std::fabs(Dot(*Normal, Line->Direction)) < 1.0e-9) Reference = *Normal;
            }
            Connector.Frame       = ConstructFrame(Middle, Line->Direction, Reference);
            Connector.Description = "line midpoint";
            return Connector;
        }
        Connector.Refusal = std::string("Joint: a ") + CurveCategoryLabel(Curve) + " edge has no joint origin (🚧 circles, ellipses and lines)";
        return Connector;
    }

    const FaceToken Face = Partition.Faces.TokenAt(Slot);
    if (!Partition.Faces.Authenticate(Face)) { Connector.Refusal = "Joint: the face no longer exists"; return Connector; }
    const FaceProfile&     Profile = Partition.Faces.Resolve(Face);
    const SurfaceGeometry& Surface = Partition.Surfaces.Resolve(Profile.Surface);
    if (const PlaneSurface* Plane = std::get_if<PlaneSurface>(&Surface))
    {
        const FaceAreaMeasure Area = MeasureFaceArea(Partition, Face);
        if (!(Area.Area > 0.0)) { Connector.Refusal = "Joint: the plane face has no area"; return Connector; }
        const Vector3 Normal = Profile.Sense == Orientation::Forward ? Plane->Frame.ZAxis : Plane->Frame.ZAxis * -1.0;
        Connector.Frame       = ConstructFrame(Area.Centroid, Normal, Plane->Frame.XAxis);
        Connector.Description = "plane centroid";
        return Connector;
    }
    const OrthonormalFrame* Axis = nullptr;
    if (const CylinderSurface* Cylinder = std::get_if<CylinderSurface>(&Surface)) Axis = &Cylinder->Frame;
    if (const ConeSurface* Cone = std::get_if<ConeSurface>(&Surface)) Axis = &Cone->Frame;
    if (Axis)
    {
        const FaceAreaMeasure Area = MeasureFaceArea(Partition, Face);
        if (!(Area.Area > 0.0)) { Connector.Refusal = "Joint: the face has no area"; return Connector; }
        const Vector3 Level = Axis->Origin + Axis->ZAxis * Dot(Area.Centroid - Axis->Origin, Axis->ZAxis);
        Connector.Frame       = { Level, Axis->XAxis, Axis->YAxis, Axis->ZAxis };
        Connector.Description = std::holds_alternative<CylinderSurface>(Surface) ? "cylinder axis" : "cone axis";
        return Connector;
    }
    if (const SphereSurface* Sphere = std::get_if<SphereSurface>(&Surface))
    {
        Connector.Frame       = Sphere->Frame;
        Connector.Description = "sphere centre";
        return Connector;
    }
    if (const TorusSurface* Torus = std::get_if<TorusSurface>(&Surface))
    {
        Connector.Frame       = Torus->Frame;
        Connector.Description = "torus centre";
        return Connector;
    }
    Connector.Refusal = std::string("Joint: a ") + SurfaceCategoryLabel(Surface) + " face has no joint origin (🚧 analytic faces only)";
    return Connector;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    PLACEMENT
//------------------------------------------------------------------------------------------------------------------------

AffineMap ComposeJointMotion(JointKind Kind, const JointMotion& Values) noexcept
{
    Vector3 Translation = { 0.0, 0.0, Values.Offset };
    switch (Kind)
    {
    case JointKind::Slider:
    case JointKind::Cylindrical: Translation.Z += Values.Slide; break;
    case JointKind::PinSlot:     Translation.X  = Values.Slide; break;
    case JointKind::Planar:      Translation.X  = Values.Slide; Translation.Y = Values.SecondSlide; break;
    default:                     break;
    }
    AffineMap Rotation = AxisRotation({ 0.0, 0.0, 1.0 }, Values.Angle);
    if (Kind == JointKind::Ball)
    {
        // Yaw about 𝑧, then pitch about the turned 𝑦, then roll about the twice-turned 𝑥 (intrinsic 𝑧𝑦𝑥).
        Rotation = ComposeAffineMaps(Rotation, ComposeAffineMaps(AxisRotation({ 0.0, 1.0, 0.0 }, Values.Pitch), AxisRotation({ 1.0, 0.0, 0.0 }, Values.Roll)));
    }
    Rotation.Shift = Translation;
    return Rotation;
}

JointPlacement SolveJointPlacement(const OrthonormalFrame& Moving, const OrthonormalFrame& Fixed, JointKind Kind, const JointMotion& Values, bool FlippedCondition,
                                   const std::optional<JointRange>& AngleRange, const std::optional<JointRange>& SlideRange)
{
    JointPlacement Placement;
    const bool Turning = Kind != JointKind::Rigid && Kind != JointKind::Slider;
    const bool Sliding = Kind == JointKind::Slider || Kind == JointKind::Cylindrical || Kind == JointKind::PinSlot || Kind == JointKind::Planar;
    if (AngleRange && Turning && (Values.Angle < AngleRange->Low - 1.0e-12 || Values.Angle > AngleRange->High + 1.0e-12))
    {
        Placement.Refusal = std::string("Joint: ") + JointKindLabel(Kind) + " angle " + Degrees(Values.Angle) + " is outside its limits [" + Degrees(AngleRange->Low) + ", " + Degrees(AngleRange->High) + "]";
        return Placement;
    }
    if (SlideRange && Sliding && (Values.Slide < SlideRange->Low - 1.0e-12 || Values.Slide > SlideRange->High + 1.0e-12))
    {
        Placement.Refusal = std::string("Joint: ") + JointKindLabel(Kind) + " slide " + Number(Values.Slide) + " is outside its limits [" + Number(SlideRange->Low) + ", " + Number(SlideRange->High) + "]";
        return Placement;
    }
    AffineMap Flip;
    if (!FlippedCondition)
    {
        Flip.ColumnY = { 0.0, -1.0, 0.0 };
        Flip.ColumnZ = { 0.0, 0.0, -1.0 };
    }
    const AffineMap Local = ComposeAffineMaps(ComposeJointMotion(Kind, Values), Flip);
    Placement.Map    = ComposeAffineMaps(ComposeAffineMaps(FrameToWorldMap(Fixed), Local), InvertRigidMap(FrameToWorldMap(Moving)));
    Placement.Target = MapFrame(Placement.Map, Moving);
    return Placement;
}

int JointFreedom(JointKind Kind) noexcept
{
    switch (Kind)
    {
    case JointKind::Rigid:       return 0;
    case JointKind::Revolute:    return 1;
    case JointKind::Slider:      return 1;
    case JointKind::Cylindrical: return 2;
    case JointKind::PinSlot:     return 2;
    case JointKind::Planar:      return 3;
    case JointKind::Ball:        return 3;
    }
    return 0;
}

const char* JointKindLabel(JointKind Kind) noexcept
{
    switch (Kind)
    {
    case JointKind::Rigid:       return "Rigid";
    case JointKind::Revolute:    return "Revolute";
    case JointKind::Slider:      return "Slider";
    case JointKind::Cylindrical: return "Cylindrical";
    case JointKind::PinSlot:     return "Pin-slot";
    case JointKind::Planar:      return "Planar";
    case JointKind::Ball:        return "Ball";
    }
    return "?";
}

} // namespace Frontier::GeometrySpace::Parametric
