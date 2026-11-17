/*====================================================================================================================================
                                                      IMPLICITSURFACE.CPP
====================================================================================================================================*/
// 🧩 Signed distances, rational Bézier conversion of lines and arcs, homogeneous implicit composition

#include "Engine/GeometrySpace/Parametric/Intersection/ImplicitSurface.h"

#include "Engine/GeometrySpace/Parametric/Geometry/NurbsCarrier.h"
#include "Engine/GeometrySpace/Parametric/Geometry/ParallelCarrier.h"
#include "Engine/GeometrySpace/Parametric/Geometry/RollingBallCarrier.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <optional>
#include <type_traits>

namespace Frontier::GeometrySpace::Parametric
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    INTERNAL FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

namespace
{
    [[nodiscard]] Vector3 LocalCoordinates(const OrthonormalFrame& Frame, Vector3 Position) noexcept
    {
        const Vector3 Offset = Position - Frame.Origin;                                      // [len] - From the origin
        return { Dot(Offset, Frame.XAxis), Dot(Offset, Frame.YAxis), Dot(Offset, Frame.ZAxis) };
    }

    [[nodiscard]] const OrthonormalFrame& FrameOf(const SurfaceGeometry& Surface) noexcept
    {
        return std::visit([](const auto& Typed) -> const OrthonormalFrame&
        {
            using Kind = std::decay_t<decltype(Typed)>;
            if constexpr (std::is_same_v<Kind, RollingBallSurface> || std::is_same_v<Kind, NurbsSurface> || std::is_same_v<Kind, ParallelSurface>) { static const OrthonormalFrame Unused{}; return Unused; }   // No frame: handled first
            else return Typed.Frame;
        }, Surface);
    }

    // Unit radial direction of a local point; on the axis any perpendicular direction is a valid gradient choice.
    [[nodiscard]] Vector3 RadialDirection(const OrthonormalFrame& Frame, Vector3 Local, double ρ) noexcept
    {
        return ρ > 0.0 ? (Frame.XAxis * Local.X + Frame.YAxis * Local.Y) * (1.0 / ρ) : Frame.XAxis;
    }

    // 📝 Homogeneous local coordinates of a rational segment as Bernstein polynomials: x(𝑡)·W, y(𝑡)·W, z(𝑡)·W and W.
    struct HomogeneousCoordinates
    {
        BernsteinPolynomial X = {};                                                          // [len] - Σ wᵢ·xᵢ·Bᵢ
        BernsteinPolynomial Y = {};                                                          // [len] - Σ wᵢ·yᵢ·Bᵢ
        BernsteinPolynomial Z = {};                                                          // [len] - Σ wᵢ·zᵢ·Bᵢ
        BernsteinPolynomial W = {};                                                          // [-]   - Σ wᵢ·Bᵢ
    };

    [[nodiscard]] HomogeneousCoordinates ComposeHomogeneous(const OrthonormalFrame& Frame, const RationalSegment& Segment)
    {
        HomogeneousCoordinates Coordinates;
        for (size_t Index = 0; Index < Segment.ControlPoints.size(); ++Index)
        {
            const Vector3 Local  = LocalCoordinates(Frame, Segment.ControlPoints[Index]);
            const double  Weight = Segment.Weights[Index];                                    // [-] - wᵢ
            Coordinates.X.Coefficients.push_back(Local.X * Weight);
            Coordinates.Y.Coefficients.push_back(Local.Y * Weight);
            Coordinates.Z.Coefficients.push_back(Local.Z * Weight);
            Coordinates.W.Coefficients.push_back(Weight);
        }
        return Coordinates;
    }

    [[nodiscard]] BernsteinPolynomial Square(const BernsteinPolynomial& Polynomial) { return MultiplyBernstein(Polynomial, Polynomial); }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

SurfaceDistance MeasureSurfaceDistance(const SurfaceGeometry& Surface, Vector3 Position) noexcept
{
    if (const auto* Carrier = std::get_if<RollingBallSurface>(&Surface))
    {
        const RollingBallDistance Measure = MeasureRollingBallDistance(*Carrier, Position);   // |X − C(𝑢*)| − r, exact
        return { Measure.Distance, Measure.Normal, Measure.Foot };
    }
    if (const auto* Offset = std::get_if<ParallelSurface>(&Surface))
    {
        // 📝 Same normal line as the basis: the signed distance drops by d and the foot moves out by d·N.
        const SurfaceDistance Basis = MeasureSurfaceDistance(SurfaceGeometry{ Offset->Definition->Basis }, Position);
        return { Basis.Distance - Offset->Definition->Distance, Basis.Normal, Basis.Foot + Basis.Normal * Offset->Definition->Distance };
    }
    if (const auto* Spline = std::get_if<NurbsSurface>(&Surface))
    {
        // 📝 Foot by global inversion; d = (X − S)·n is the signed distance (exact at interior feet, where X − S ∥ n).
        const NurbsDomain U = MeasureNurbsSurfaceDomain(*Spline->Definition, true), V = MeasureNurbsSurfaceDomain(*Spline->Definition, false);
        const Vector2 Parameter = ProjectOntoNurbsSurface(*Spline, Position, { 0.5 * (U.Low + U.High), 0.5 * (V.Low + V.High) });
        const Vector3 Foot      = EvaluateNurbsSurface(*Spline, Parameter).Position;
        const Vector3 Normal    = EvaluateNurbsNormal(*Spline, Parameter);
        return { Dot(Position - Foot, Normal), Normal, Foot };
    }
    const OrthonormalFrame& Frame = FrameOf(Surface);
    const Vector3           Local = LocalCoordinates(Frame, Position);                       // [len] - Frame coordinates
    const double            ρ     = std::hypot(Local.X, Local.Y);                            // [len] - Distance from the axis
    SurfaceDistance         Measure;
    if (std::holds_alternative<PlaneSurface>(Surface))
    {
        Measure.Distance = Local.Z;
        Measure.Normal   = Frame.ZAxis;
    }
    else if (const auto* Cylinder = std::get_if<CylinderSurface>(&Surface))
    {
        Measure.Distance = ρ - Cylinder->Radius;
        Measure.Normal   = RadialDirection(Frame, Local, ρ);
    }
    else if (const auto* Cone = std::get_if<ConeSurface>(&Surface))
    {
        // 📝 Meridian half-plane (ρ, z): the cone is the line ρ = r₀ + s·z and its mirror ρ = −r₀ − s·z (second nappe);
        //    the nearer line gives the distance. Natural normal (1, −s)/√(1+s²) on the first nappe.
        const double  s       = Cone->RadialSlope;                                           // [-] - dr/d𝑣
        const double  Norm    = std::sqrt(1.0 + s * s);                                      // [-] - Line normaliser
        const double  First   = (ρ - Cone->OriginRadius - s * Local.Z) / Norm;                 // [len] - To the first nappe
        const double  Second  = (ρ + Cone->OriginRadius + s * Local.Z) / Norm;                 // [len] - To the mirror nappe
        const Vector3 Radial  = RadialDirection(Frame, Local, ρ);
        if (std::fabs(First) <= std::fabs(Second))
        {
            Measure.Distance = First;
            Measure.Normal   = (Radial - Frame.ZAxis * s) * (1.0 / Norm);
        }
        else
        {
            Measure.Distance = Second;
            Measure.Normal   = (Radial + Frame.ZAxis * s) * (1.0 / Norm);
        }
    }
    else if (const auto* Sphere = std::get_if<SphereSurface>(&Surface))
    {
        const double Radius = Length(Local);                                                 // [len] - From the centre
        Measure.Distance = Radius - Sphere->Radius;
        Measure.Normal   = Radius > 0.0 ? (Position - Frame.Origin) * (1.0 / Radius) : Frame.ZAxis;
    }
    else
    {
        const auto&  Torus  = std::get<TorusSurface>(Surface);
        const double Δρ     = ρ - Torus.MajorRadius;                                           // [len] - From the tube axis, radially
        const double Radius = std::hypot(Δρ, Local.Z);                                        // [len] - From the tube axis
        Measure.Distance = Radius - Torus.MinorRadius;
        Measure.Normal   = Radius > 0.0 ? (RadialDirection(Frame, Local, ρ) * Δρ + Frame.ZAxis * Local.Z) * (1.0 / Radius) : Frame.ZAxis;
    }
    Measure.Foot = Position - Measure.Normal * Measure.Distance;
    return Measure;
}

SurfaceDistance MeasureSurfaceDistanceNear(const SurfaceGeometry& Surface, Vector3 Position, Vector2& Hint) noexcept
{
    if (const auto* Carrier = std::get_if<RollingBallSurface>(&Surface))
    {
        const RollingBallDistance Measure = MeasureRollingBallDistanceNear(*Carrier, Position, Hint);
        return { Measure.Distance, Measure.Normal, Measure.Foot };
    }
    const auto*         Offset = std::get_if<ParallelSurface>(&Surface);
    const NurbsSurface* Spline = Offset ? &Offset->Definition->Basis : std::get_if<NurbsSurface>(&Surface);
    if (Spline == nullptr) return MeasureSurfaceDistance(Surface, Position);
    std::optional<Vector2> Parameter;
    if (std::isfinite(Hint.X) && std::isfinite(Hint.Y)) Parameter = PolishNurbsFootLocally(*Spline, Position, Hint);
    if (!Parameter)
    {
        const NurbsDomain U = MeasureNurbsSurfaceDomain(*Spline->Definition, true), V = MeasureNurbsSurfaceDomain(*Spline->Definition, false);
        Parameter = ProjectOntoNurbsSurface(*Spline, Position, { 0.5 * (U.Low + U.High), 0.5 * (V.Low + V.High) });
    }
    Hint = *Parameter;
    const Vector3 Foot     = EvaluateNurbsSurface(*Spline, *Parameter).Position;
    const Vector3 Normal   = EvaluateNurbsNormal(*Spline, *Parameter);
    const double  Distance = Dot(Position - Foot, Normal);                                   // [len] - Signed, basis
    if (Offset) return { Distance - Offset->Definition->Distance, Normal, Foot + Normal * Offset->Definition->Distance };
    return { Distance, Normal, Foot };
}

std::vector<RationalSegment> ConvertToRationalSegments(const CurveGeometry& Curve, double Low, double High)
{
    std::vector<RationalSegment> Segments;
    if (const auto* Line = std::get_if<StraightLine>(&Curve))
    {
        Segments.push_back({ { Line->Origin + Line->Direction * Low, Line->Origin + Line->Direction * High }, { 1.0, 1.0 }, Low, High });
        return Segments;
    }
    if (const auto* Spline = std::get_if<NurbsCurve>(&Curve))
    {
        // 📝 Bézier extraction is exact and keeps τ linear in 𝑡 on every piece.
        for (NurbsBezierPiece& Piece : DecomposeNurbsCurve(*Spline->Definition, Low, High))
            Segments.push_back({ std::move(Piece.Poles), std::move(Piece.Weights), Piece.PieceLow, Piece.PieceHigh });
        return Segments;
    }
    // 📝 Arc of sweep Δ ≤ π/2: P₀, P₂ on the circle, P₁ at the tangent intersection, w₁ = cos(Δ/2) — exact conic.
    //    An ellipse is the affine image of a circle and rational Bézier form is affine invariant: same weights.
    const ConicForm Conic   = *ResolveConicForm(Curve);
    const int    PieceCount = std::max(1, static_cast<int>(std::ceil((High - Low) / (0.5 * std::numbers::pi) - 1.0e-12)));
    const double Sweep      = (High - Low) / PieceCount;                                     // [rad] - Δ per piece
    const auto   Point      = [&](double Angle, double Scale)
    {
        return Conic.Centre + (Conic.CosineAxis * std::cos(Angle) + Conic.SineAxis * std::sin(Angle)) * Scale;
    };
    for (int PieceIndex = 0; PieceIndex < PieceCount; ++PieceIndex)
    {
        const double Start  = Low + PieceIndex * Sweep;                                      // [rad] - Piece start
        const double Middle = Start + 0.5 * Sweep;                                           // [rad] - Piece middle
        const double Weight = std::cos(0.5 * Sweep);                                         // [-]   - Middle weight
        Segments.push_back({ { Point(Start, 1.0), Point(Middle, 1.0 / Weight), Point(Start + Sweep, 1.0) }, { 1.0, Weight, 1.0 }, Start, Start + Sweep });
    }
    return Segments;
}

Vector3 EvaluateRationalSegment(const RationalSegment& Segment, double 𝑡)
{
    const int Degree = static_cast<int>(Segment.ControlPoints.size()) - 1;                   // [-] - Any degree
    if (Degree == 1) return Segment.ControlPoints[0] * (1.0 - 𝑡) + Segment.ControlPoints[1] * 𝑡;
    // de Casteljau on the homogeneous points (wᵢPᵢ, wᵢ).
    std::vector<Vector3> Points(Segment.ControlPoints.size());
    std::vector<double>  Weights = Segment.Weights;
    for (size_t Index = 0; Index < Points.size(); ++Index) Points[Index] = Segment.ControlPoints[Index] * Segment.Weights[Index];
    for (int Level = 1; Level <= Degree; ++Level)
        for (int Index = 0; Index + Level <= Degree; ++Index)
        {
            Points[Index]  = Points[Index] * (1.0 - 𝑡) + Points[Index + 1] * 𝑡;
            Weights[Index] = Weights[Index] * (1.0 - 𝑡) + Weights[Index + 1] * 𝑡;
        }
    return Points[0] * (1.0 / Weights[0]);
}

BernsteinPolynomial ComposeImplicitEquation(const SurfaceGeometry& Surface, const RationalSegment& Segment)
{
    // ⚠️ The rolling-ball and NURBS carriers have no polynomial implicit form; IntersectCurveSurface scans their distance instead.
    if (ProceduralSurfaceCondition(Surface)) return BernsteinPolynomial{ { 1.0 } };
    const HomogeneousCoordinates H = ComposeHomogeneous(FrameOf(Surface), Segment);
    if (std::holds_alternative<PlaneSurface>(Surface)) return H.Z;                             // z
    const BernsteinPolynomial Planar = AddBernstein(Square(H.X), Square(H.Y));               // x² + y²
    const BernsteinPolynomial W2     = Square(H.W);                                          // W²
    if (const auto* Cylinder = std::get_if<CylinderSurface>(&Surface))                       // x² + y² − r²W²
    {
        return AddBernstein(Planar, ScaleBernstein(W2, -Cylinder->Radius * Cylinder->Radius));
    }
    if (const auto* Cone = std::get_if<ConeSurface>(&Surface))                               // x² + y² − (r₀W + s·z)²
    {
        const BernsteinPolynomial Radius = AddBernstein(ScaleBernstein(H.W, Cone->OriginRadius), ScaleBernstein(H.Z, Cone->RadialSlope));
        return AddBernstein(Planar, ScaleBernstein(Square(Radius), -1.0));
    }
    const BernsteinPolynomial Spatial = AddBernstein(Planar, Square(H.Z));                   // x² + y² + z²
    if (const auto* Sphere = std::get_if<SphereSurface>(&Surface))                           // x² + y² + z² − r²W²
    {
        return AddBernstein(Spatial, ScaleBernstein(W2, -Sphere->Radius * Sphere->Radius));
    }
    // 📝 Torus: (x² + y² + z² + (R² − r²)W²)² − 4R²·W²·(x² + y²)
    const auto&  Torus = std::get<TorusSurface>(Surface);
    const double R2    = Torus.MajorRadius * Torus.MajorRadius;                               // [len²] - R²
    const double r2    = Torus.MinorRadius * Torus.MinorRadius;                               // [len²] - r²
    const BernsteinPolynomial Inner = AddBernstein(Spatial, ScaleBernstein(W2, R2 - r2));
    return AddBernstein(Square(Inner), ScaleBernstein(MultiplyBernstein(W2, Planar), -4.0 * R2));
}

double ImplicitMagnitude(const SurfaceGeometry& Surface, const RationalSegment& Segment)
{
    // 📝 Characteristic length L (control points + radii); the composed coefficients scale as L^d.
    const OrthonormalFrame& Frame = FrameOf(Surface);
    double Scale = 0.0;                                                                      // [len] - L
    for (const Vector3& Control : Segment.ControlPoints) Scale = std::max(Scale, Length(LocalCoordinates(Frame, Control)));
    std::visit([&](const auto& Typed)
    {
        using Kind = std::decay_t<decltype(Typed)>;
        if constexpr (std::is_same_v<Kind, CylinderSurface> || std::is_same_v<Kind, SphereSurface>) Scale = std::max(Scale, Typed.Radius);
        if constexpr (std::is_same_v<Kind, ConeSurface>) Scale = std::max(Scale, std::fabs(Typed.OriginRadius));
        if constexpr (std::is_same_v<Kind, TorusSurface>) Scale = std::max(Scale, Typed.MajorRadius + Typed.MinorRadius);
    }, Surface);
    if (ProceduralSurfaceCondition(Surface)) return 1.0;                                    // Scanned, never composed
    const int Degree = std::holds_alternative<PlaneSurface>(Surface) ? 1 : std::holds_alternative<TorusSurface>(Surface) ? 4 : 2;
    return std::pow(std::max(Scale, 1.0e-300), Degree);
}

} // namespace Frontier::GeometrySpace::Parametric
