/*====================================================================================================================================
                                                       BODYTRANSFORM.CPP
====================================================================================================================================*/
// 🧩 Transformed copies of a body — carriers mapped per kind, SP-curves re-fitted, loops reversed by mirrors

#include "Engine/GeometrySpace/Parametric/Construction/BodyTransform.h"

#include "Engine/GeometrySpace/Parametric/Construction/BodyArrangement.h"
#include "Engine/GeometrySpace/Parametric/Geometry/ProjectedCurve.h"
#include "Engine/GeometrySpace/Parametric/Intersection/IntersectionCurve.h"
#include "Engine/GeometrySpace/Parametric/Geometry/NurbsCarrier.h"
#include "Engine/GeometrySpace/Parametric/Geometry/ParallelCarrier.h"
#include "Engine/GeometrySpace/Parametric/Geometry/RollingBallCarrier.h"
#include "Engine/GeometrySpace/Parametric/Surfacing/CarrierApproximation.h"

#include <cmath>
#include <map>
#include <set>
#include <memory>

namespace Frontier::GeometrySpace::Parametric
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    INTERNAL FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

namespace
{
    constexpr double SimilaritySlack = 1.0e-12;                                              // [-] - Relative Gram-matrix agreement

    // 📝 Linear part, determinant, and the similarity split M = s·Q when it exists.
    struct MappingAnalysis
    {
        AffineMapping Map         = {};                                                          // [-] - The map
        double    Determinant = 1.0;                                                         // [-] - det M
        bool      Similar     = true;                                                        // [-] - M = s·Q
        double    Scale       = 1.0;                                                         // [-] - s
    };

    [[nodiscard]] Vector3 ApplyLinear(const AffineMapping& Map, Vector3 Vector) noexcept
    {
        return Map.ColumnX * Vector.X + Map.ColumnY * Vector.Y + Map.ColumnZ * Vector.Z;
    }

    [[nodiscard]] Vector3 ApplyPoint(const AffineMapping& Map, Vector3 Point) noexcept { return ApplyLinear(Map, Point) + Map.Shift; }

    [[nodiscard]] MappingAnalysis AnalyseMap(const AffineMapping& Map)
    {
        MappingAnalysis Analysis;
        Analysis.Map         = Map;
        Analysis.Determinant = Dot(Map.ColumnX, Cross(Map.ColumnY, Map.ColumnZ));
        const double Square  = std::cbrt(Analysis.Determinant * Analysis.Determinant);       // [-] - s² if similar
        const auto   Near    = [&](double Value, double Target) { return std::fabs(Value - Target) <= SimilaritySlack * std::max(1.0, Square); };
        Analysis.Similar = Near(Dot(Map.ColumnX, Map.ColumnX), Square) && Near(Dot(Map.ColumnY, Map.ColumnY), Square) && Near(Dot(Map.ColumnZ, Map.ColumnZ), Square)
                        && Near(Dot(Map.ColumnX, Map.ColumnY), 0.0) && Near(Dot(Map.ColumnY, Map.ColumnZ), 0.0) && Near(Dot(Map.ColumnZ, Map.ColumnX), 0.0);
        Analysis.Scale = std::sqrt(Square);
        return Analysis;
    }

    // Q·v for a similarity (unit result for unit input).
    [[nodiscard]] Vector3 Rotate(const MappingAnalysis& Analysis, Vector3 Vector) noexcept { return ApplyLinear(Analysis.Map, Vector) * (1.0 / Analysis.Scale); }

    // Frame of a mapped surface: X′ = Q·X, Y′ = ±Q·Y (sign of det), hence Z′ = Q·Z and the natural normal maps with Q.
    [[nodiscard]] OrthonormalBasis MapSurfaceFrame(const MappingAnalysis& Analysis, const OrthonormalBasis& Frame)
    {
        const Vector3 X = Rotate(Analysis, Frame.XAxis);
        const Vector3 Y = Rotate(Analysis, Frame.YAxis) * (Analysis.Determinant > 0.0 ? 1.0 : -1.0);
        return { ApplyPoint(Analysis.Map, Frame.Origin), X, Y, Cross(X, Y) };
    }

    // Frame of a mapped circle / ellipse: X′ = Q·X, Y′ = Q·Y keeps τ (Z′ = X′ × Y′ is only the plane normal).
    [[nodiscard]] OrthonormalBasis MapCurveFrame(const MappingAnalysis& Analysis, const OrthonormalBasis& Frame)
    {
        const Vector3 X = Rotate(Analysis, Frame.XAxis), Y = Rotate(Analysis, Frame.YAxis);
        return { ApplyPoint(Analysis.Map, Frame.Origin), X, Y, Cross(X, Y) };
    }

    // Axial similarity along a quadric's ZAxis: M maps X, Y, Z orthogonally with |M·X| = |M·Y| = Radial and |M·Z| = Axial.
    struct AxialSurfaceScale
    {
        bool             ExactCondition = false;                                             // [-] - Orthogonal with equal radial scale
        OrthonormalBasis Frame          = {};                                                // [-] - Mapped axis frame (Z′ = +M·Z / Axial)
        double           Radial         = 1.0;                                               // [-] - |M·X| = |M·Y|
        double           Axial          = 1.0;                                               // [-] - |M·Z|
    };

    [[nodiscard]] AxialSurfaceScale AnalyseAxialSurface(const MappingAnalysis& Analysis, const OrthonormalBasis& Frame) noexcept
    {
        AxialSurfaceScale Result;
        const Vector3 ImageX = ApplyLinear(Analysis.Map, Frame.XAxis);
        const Vector3 ImageY = ApplyLinear(Analysis.Map, Frame.YAxis);
        const Vector3 ImageZ = ApplyLinear(Analysis.Map, Frame.ZAxis);
        const double  ScaleX = Length(ImageX), ScaleY = Length(ImageY), ScaleZ = Length(ImageZ);
        if (!(ScaleX > 1.0e-12 && ScaleY > 1.0e-12 && ScaleZ > 1.0e-12)) return Result;
        const double ScaleMax = std::max({ 1.0, ScaleX, ScaleY, ScaleZ });
        if (std::fabs(ScaleX - ScaleY) > SimilaritySlack * ScaleMax) return Result;
        if (std::fabs(Dot(ImageX, ImageY)) > SimilaritySlack * ScaleX * ScaleY) return Result;
        if (std::fabs(Dot(ImageX, ImageZ)) > SimilaritySlack * ScaleX * ScaleZ) return Result;
        if (std::fabs(Dot(ImageY, ImageZ)) > SimilaritySlack * ScaleY * ScaleZ) return Result;
        const Vector3 X = ImageX * (1.0 / ScaleX);
        const Vector3 Y = ImageY * ((Analysis.Determinant > 0.0 ? 1.0 : -1.0) / ScaleY);
        Result.ExactCondition = true;
        Result.Frame          = { ApplyPoint(Analysis.Map, Frame.Origin), X, Y, Cross(X, Y) };
        Result.Radial         = 0.5 * (ScaleX + ScaleY);
        Result.Axial          = ScaleZ;
        return Result;
    }

    // Orthogonal conic image: M maps CosineAxis and SineAxis orthogonally without phase shift (|M·a| ≥ |M·b|).
    [[nodiscard]] std::optional<CurveGeometry> TryMapOrthogonalConic(const MappingAnalysis& Analysis, const CurveGeometry& Curve) noexcept
    {
        const std::optional<ConicForm> Conic = ResolveConicForm(Curve);
        if (!Conic) return std::nullopt;
        const Vector3 ImageA = ApplyLinear(Analysis.Map, Conic->CosineAxis);
        const Vector3 ImageB = ApplyLinear(Analysis.Map, Conic->SineAxis);
        const double  Major  = Length(ImageA), Minor = Length(ImageB);
        if (!(Major > 1.0e-12 && Minor > 1.0e-12)) return std::nullopt;
        if (std::fabs(Dot(ImageA, ImageB)) > SimilaritySlack * Major * Minor) return std::nullopt;
        const Vector3 X      = ImageA * (1.0 / Major);
        const Vector3 Y      = ImageB * (1.0 / Minor);
        const Vector3 Origin = ApplyPoint(Analysis.Map, Conic->Centre);
        const OrthonormalBasis Frame{ Origin, X, Y, Cross(X, Y) };
        if (std::fabs(Major - Minor) <= SimilaritySlack * std::max(1.0, Major))
            return CurveGeometry{ CircularArc{ Frame, 0.5 * (Major + Minor) } };
        if (Major > Minor)
            return CurveGeometry{ EllipticalArc{ Frame, Major, Minor } };
        return std::nullopt;
    }

    struct MappedSurface
    {
        SurfaceGeometry Surface = PlaneSurface{};                                            // [-] - Image carrier
        bool            Flip    = false;                                                     // [-] - Face sense inverts
        std::string     Refusal = {};                                                        // [-] - Why not mappable
    };

    [[nodiscard]] CurveGeometry MapCurveImage(const MappingAnalysis& Analysis, const CurveGeometry& Curve);   // MapCurve(...).Curve
    [[nodiscard]] double        MapCurveStretch(const MappingAnalysis& Analysis, const CurveGeometry& Curve); // MapCurve(...).Stretch

    [[nodiscard]] MappedSurface MapSurface(const MappingAnalysis& Analysis, const SurfaceGeometry& Surface)
    {
        MappedSurface Mapped;
        if (const auto* Spline = std::get_if<NurbsSurface>(&Surface))
        {
            // 📝 Any affine map acts exactly on the poles; the chart is kept, so S𝑢 × S𝑣 maps to det·M⁻ᵀ·n and the face
            //    sense flips exactly when det M < 0.
            Mapped.Surface = MapNurbsSurface(*Spline->Definition, [&](Vector3 Point) { return ApplyPoint(Analysis.Map, Point); });
            Mapped.Flip    = Analysis.Determinant < 0.0;
            return Mapped;
        }
        if (const auto* Offset = std::get_if<ParallelSurface>(&Surface))
        {
            // 📝 M(S + d·N) = M·S + d·s·(M·N)/s; the image basis's normal is sign(det)·(M·N)/s, so d′ = d·s·sign(det) and
            //    the face sense flips with det < 0 as for NURBS. A non-similar map bends the normal lines: no exact image.
            if (!Analysis.Similar) { Mapped.Refusal = "Transform: a non-similar map has no exact image of an offset NURBS surface (🚧)"; return Mapped; }
            const NurbsSurface Basis = MapNurbsSurface(*Offset->Definition->Basis.Definition, [&](Vector3 Point) { return ApplyPoint(Analysis.Map, Point); });
            Mapped.Surface = ConstructParallelSurface(Basis, Offset->Definition->Distance * Analysis.Scale * (Analysis.Determinant < 0.0 ? -1.0 : 1.0));
            Mapped.Flip    = Analysis.Determinant < 0.0;
            return Mapped;
        }
        if (const auto* Plane = std::get_if<PlaneSurface>(&Surface))
        {
            if (Analysis.Similar) { Mapped.Surface = PlaneSurface{ MapSurfaceFrame(Analysis, Plane->Frame) }; return Mapped; }
            // General affine: X′ ∥ M·X, Y′ from M·Y (Gram–Schmidt); Z′ ∝ M·X × M·Y = det·M⁻ᵀ·Z, so the sense flips when det < 0.
            const Vector3 X = Normalize(ApplyLinear(Analysis.Map, Plane->Frame.XAxis));
            const Vector3 Image = ApplyLinear(Analysis.Map, Plane->Frame.YAxis);
            const Vector3 Y = Normalize(Image - X * Dot(Image, X));
            Mapped.Surface = PlaneSurface{ { ApplyPoint(Analysis.Map, Plane->Frame.Origin), X, Y, Cross(X, Y) } };
            Mapped.Flip    = Analysis.Determinant < 0.0;
            return Mapped;
        }
        if (!Analysis.Similar)
        {
            if (const auto* Cylinder = std::get_if<CylinderSurface>(&Surface))
            {
                const AxialSurfaceScale Axial = AnalyseAxialSurface(Analysis, Cylinder->Frame);
                if (Axial.ExactCondition) { Mapped.Surface = CylinderSurface{ Axial.Frame, Cylinder->Radius * Axial.Radial }; return Mapped; }
            }
            if (const auto* Cone = std::get_if<ConeSurface>(&Surface))
            {
                const AxialSurfaceScale Axial = AnalyseAxialSurface(Analysis, Cone->Frame);
                if (Axial.ExactCondition)
                {
                    Mapped.Surface = ConeSurface{ Axial.Frame, Cone->OriginRadius * Axial.Radial, Cone->RadialSlope * (Axial.Radial / Axial.Axial) };
                    return Mapped;
                }
            }
            Mapped.Refusal = "Transform: a non-similar map has no exact rolling-ball image (the procedural carrier is not rational; NURBS promotion would approximate it)";
            return Mapped;
        }
        const double Scale = Analysis.Scale;                                                 // [-] - s
        if (const auto* Ball = std::get_if<RollingBallSurface>(&Surface))
        {
            // 📝 Geometric definition: the image spine and guide rebuild the carrier with radius s·r.
            //    A reflection flips the handedness so the image chart is M·P(𝑢, 𝑣) and every SP-curve keeps its (𝑢, 𝑣);
            //    ∂𝑢 × ∂𝑣 (the natural normal) then flips relative to M·n, so the face sense flips with it.
            const MappedSurface Guide = MapSurface(Analysis, Ball->Definition->Guide);
            if (!Guide.Refusal.empty()) { Mapped.Refusal = Guide.Refusal; return Mapped; }
            const MappedSurface Partner = MapSurface(Analysis, Ball->Definition->Partner);
            if (!Partner.Refusal.empty()) { Mapped.Refusal = Partner.Refusal; return Mapped; }
            // A canal's r(𝑢) = Radius + RadiusRate·𝑢 on the stretched 𝑢′ = Stretch·𝑢: RadiusRate′ = s·RadiusRate / Stretch.
            const double Rate = Ball->Definition->RadiusRate == 0.0 ? 0.0 : Ball->Definition->RadiusRate * Scale / MapCurveStretch(Analysis, Ball->Definition->Spine);
            Mapped.Surface = ConstructRollingBallSurface(MapCurveImage(Analysis, Ball->Definition->Spine), Ball->Definition->Radius * Scale, Guide.Surface,
                                                         Ball->Definition->Handedness * (Analysis.Determinant < 0.0 ? -1.0 : 1.0), Ball->Definition->Section, Partner.Surface, Rate);
            Mapped.Flip    = Analysis.Determinant < 0.0;
            return Mapped;
        }
        std::visit([&](const auto& Carrier)
        {
            using Kind = std::decay_t<decltype(Carrier)>;
            if constexpr (std::is_same_v<Kind, RollingBallSurface> || std::is_same_v<Kind, NurbsSurface> || std::is_same_v<Kind, ParallelSurface>) return;   // Mapped above
            else
            {
            const OrthonormalBasis Frame = MapSurfaceFrame(Analysis, Carrier.Frame);
            if constexpr (std::is_same_v<Kind, CylinderSurface>) Mapped.Surface = CylinderSurface{ Frame, Carrier.Radius * Scale };
            else if constexpr (std::is_same_v<Kind, ConeSurface>) Mapped.Surface = ConeSurface{ Frame, Carrier.OriginRadius * Scale, Carrier.RadialSlope };
            else if constexpr (std::is_same_v<Kind, SphereSurface>) Mapped.Surface = SphereSurface{ Frame, Carrier.Radius * Scale };
            else if constexpr (std::is_same_v<Kind, TorusSurface>) Mapped.Surface = TorusSurface{ Frame, Carrier.MajorRadius * Scale, Carrier.MinorRadius * Scale };
            }
        }, Surface);
        return Mapped;
    }

    struct MappedCurve
    {
        CurveGeometry Curve      = StraightLine{};                                           // [-] - Image carrier
        double        Stretch    = 1.0;                                                      // [-] - τ′ = Stretch · τ
        std::string   Refusal    = {};                                                       // [-] - Why not mappable
    };

    [[nodiscard]] MappedCurve MapCurve(const MappingAnalysis& Analysis, const CurveGeometry& Curve)
    {
        MappedCurve Mapped;
        if (const auto* Spline = std::get_if<NurbsCurve>(&Curve))
        {
            Mapped.Curve = MapNurbsCurve(*Spline->Definition, [&](Vector3 Point) { return ApplyPoint(Analysis.Map, Point); });
            return Mapped;                                                                   // τ kept exactly
        }
        if (const auto* Line = std::get_if<StraightLine>(&Curve))
        {
            const Vector3 Image = ApplyLinear(Analysis.Map, Line->Direction);                // [-] - M·d
            Mapped.Stretch = Length(Image);
            Mapped.Curve   = StraightLine{ ApplyPoint(Analysis.Map, Line->Origin), Image * (1.0 / Mapped.Stretch) };
            return Mapped;
        }
        if (!Analysis.Similar)
        {
            if (const std::optional<CurveGeometry> Conic = TryMapOrthogonalConic(Analysis, Curve))
            {
                Mapped.Curve = *Conic;
                return Mapped;
            }
            Mapped.Refusal = "Transform: a non-similar map on a traced or contact edge (its chord-length τ does not stretch uniformly, 🚧)";
            return Mapped;
        }
        if (const auto* Traced = std::get_if<IntersectionCurve>(&Curve))
        {
            // 📝 The carrier is geometric (Newton onto both surfaces in the guide's normal plane, 𝐭 oriented with H′), so a
            //    similarity maps it exactly: surfaces and guide map, closed knots (2π·chord / perimeter) are invariant,
            //    open knots (chord length) scale by s together with the edge interval; dH/dτ follows.
            const TracedIntersection& Source = *Traced->Trace;
            auto Image = std::make_shared<TracedIntersection>(Source);
            Image->First  = MapSurface(Analysis, Source.First).Surface;
            Image->Second = MapSurface(Analysis, Source.Second).Surface;
            Mapped.Stretch = Source.Closed ? 1.0 : Analysis.Scale;
            if (Source.Law)
            {
                // 📝 A ball-law spine runs on its edge's τ, so it stretches like the mapped edge; the size scales by s.
                const MappedCurve Edge = MapCurve(Analysis, Source.Law->Edge);
                if (!Edge.Refusal.empty()) { Mapped.Refusal = Edge.Refusal; return Mapped; }
                auto Law = std::make_shared<BallBlendProfile>(*Source.Law);
                // Size(t) = Size + SizeRate·t on the stretched t′ = Stretch·t: Size′ = s·Size, SizeRate′ = s·SizeRate / Stretch.
                Law->Edge     = Edge.Curve;
                Law->Size    *= Analysis.Scale;
                Law->SizeRate = Law->SizeRate * Analysis.Scale / Edge.Stretch;
                Image->Law     = std::move(Law);
                Mapped.Stretch = Edge.Stretch;
            }
            for (Vector3& Point : Image->Points) Point = ApplyPoint(Analysis.Map, Point);
            for (Vector3& Tangent : Image->Tangents) Tangent = ApplyLinear(Analysis.Map, Tangent) * (1.0 / Mapped.Stretch);
            for (double& Knot : Image->Knots) Knot *= Mapped.Stretch;
            Image->Perimeter *= Analysis.Scale;
            Mapped.Curve = IntersectionCurve{ std::move(Image) };
            return Mapped;
        }
        if (const auto* Contact = std::get_if<ContactCurve>(&Curve))
        {
            // 📝 A rail is the foot of its spine on its support: map both and rebuild; τ stretches like the spine's.
            const ContactDefinition& Source = *Contact->Definition;
            const MappedCurve        Spine  = MapCurve(Analysis, Source.Spine);
            const MappedSurface      Support = MapSurface(Analysis, Source.Support);
            if (!Spine.Refusal.empty() || !Support.Refusal.empty()) { Mapped.Refusal = Spine.Refusal.empty() ? Support.Refusal : Spine.Refusal; return Mapped; }
            const ContactCurve Forward = ConstructContactCurve(Spine.Curve, Support.Surface);
            Mapped.Curve   = Source.Direction > 0.0 ? CurveGeometry{ Forward } : ReverseContactCurve(*Forward.Definition);
            Mapped.Stretch = Spine.Stretch;
            return Mapped;
        }
        if (const auto* Arc = std::get_if<CircularArc>(&Curve)) Mapped.Curve = CircularArc{ MapCurveFrame(Analysis, Arc->Frame), Arc->Radius * Analysis.Scale };
        else
        {
            const auto& Ellipse = std::get<EllipticalArc>(Curve);
            Mapped.Curve = EllipticalArc{ MapCurveFrame(Analysis, Ellipse.Frame), Ellipse.MajorRadius * Analysis.Scale, Ellipse.MinorRadius * Analysis.Scale };
        }
        return Mapped;
    }

    CurveGeometry MapCurveImage(const MappingAnalysis& Analysis, const CurveGeometry& Curve) { return MapCurve(Analysis, Curve).Curve; }
    double        MapCurveStretch(const MappingAnalysis& Analysis, const CurveGeometry& Curve) { return MapCurve(Analysis, Curve).Stretch; }

    // Chart image of a point for the periodic-image choice: 𝑢 → ±𝑢 (mirror), 𝑣 scales along the axis on cylinders / cones.
    [[nodiscard]] Vector2 MapAnchor(const MappingAnalysis& Analysis, const SurfaceGeometry& Surface, Vector2 Parameter)
    {
        if (std::holds_alternative<PlaneSurface>(Surface)) return Parameter;                // No periodic image to choose
        if (std::holds_alternative<RollingBallSurface>(Surface)) return Parameter;          // Chart carried exactly (handedness)
        if (ResolveNurbsChart(Surface) != nullptr) return Parameter;                        // Poles mapped, chart kept
        const double U = Analysis.Determinant > 0.0 ? Parameter.X : -Parameter.X;
        if (!Analysis.Similar)
        {
            if (const auto* Cylinder = std::get_if<CylinderSurface>(&Surface)) return { U, Parameter.Y * AnalyseAxialSurface(Analysis, Cylinder->Frame).Axial };
            if (const auto* Cone = std::get_if<ConeSurface>(&Surface)) return { U, Parameter.Y * AnalyseAxialSurface(Analysis, Cone->Frame).Axial };
        }
        const bool Linear = std::holds_alternative<CylinderSurface>(Surface) || std::holds_alternative<ConeSurface>(Surface);
        return { U, Linear ? Parameter.Y * Analysis.Scale : Parameter.Y };
    }

    // Chart box of a face from its SP-curves (periodic images are already unwrapped per loop).
    [[nodiscard]] std::pair<Vector2, Vector2> MeasureFaceChartBox(const ModelPartition& Partition, FaceToken Face, int Samples = 32)
    {
        Vector2 Low{ INFINITY, INFINITY }, High{ -INFINITY, -INFINITY };
        for (LoopToken Loop : Partition.Faces.Resolve(Face).Loops)
        {
            Partition.EnumerateLoopFins(Loop, [&](FinToken Fin)
            {
                const FinProfile&  Use  = Partition.Fins.Resolve(Fin);
                const EdgeProfile& Edge = Partition.Edges.Resolve(Use.Edge);
                if (!Use.SurfaceCurve) return;
                for (int Sample = 0; Sample <= Samples; ++Sample)
                {
                    const Vector2 Point = EvaluateParameterCurve(*Use.SurfaceCurve, Edge.IntervalLow + (Edge.IntervalHigh - Edge.IntervalLow) * Sample / Samples).Position;
                    Low  = { std::min(Low.X, Point.X), std::min(Low.Y, Point.Y) };
                    High = { std::max(High.X, Point.X), std::max(High.Y, Point.Y) };
                }
            });
        }
        return { Low, High };
    }

    // Curved analytic carriers promote to NURBS only when the map cannot carry them as exact analytic quadrics.
    [[nodiscard]] bool PromotableSurface(const MappingAnalysis& Analysis, const SurfaceGeometry& Surface) noexcept
    {
        if (std::holds_alternative<PlaneSurface>(Surface) || ProceduralSurfaceCondition(Surface)) return false;
        if (const auto* Cylinder = std::get_if<CylinderSurface>(&Surface)) return !AnalyseAxialSurface(Analysis, Cylinder->Frame).ExactCondition;
        if (const auto* Cone = std::get_if<ConeSurface>(&Surface)) return !AnalyseAxialSurface(Analysis, Cone->Frame).ExactCondition;
        return true;
    }

    // 💡 One blueprint walk for both transforms and NURBS promotion: PromoteAll converts every analytic face and every
    //    line / conic edge before mapping; a non-similar map promotes the curved ones it could not otherwise carry.
    //    With Approximation (PromoteAll only) the procedural carriers left over — rolling-ball / parallel faces, traced
    //    edges — are replaced by certified cubic B-splines in their own parameterisation, tallied into Tally.
    [[nodiscard]] TransformOutcome TransformBodyWith(const ModelPartition& Partition, BodyToken Source, ModelPartition& Target, const AffineMapping& Map, bool PromoteAll,
                                                     const ApproximationSettings* Approximation = nullptr, ApproximationOutcome* Tally = nullptr, bool ReverseCondition = false,
                                                     double KnownVolume = std::numeric_limits<double>::quiet_NaN(), const std::set<uint32_t>* ReverseFaces = nullptr)
    {
        TransformOutcome Outcome;
        const MappingAnalysis Analysis = AnalyseMap(Map);
        if (!(std::fabs(Analysis.Determinant) > 1.0e-12)) { Outcome.Refusal = "Transform: the map is singular"; return Outcome; }
        const BodyInventory Inventory = Partition.CollectInventory(Source);
        BodyBlueprint Blueprint;
        Blueprint.Category = Partition.Bodies.Resolve(Source).Category;

        // ① Vertices and edges (line intervals stretch by |M·d|; circle / ellipse / NURBS parameters are kept).
        std::map<uint32_t, int> VertexIndex, EdgeIndex;                                      // [-] - Source slot → blueprint index
        std::vector<char>       TolerantEdges;                                               // [-] - Edges touching a stand-in
        for (VertexToken Vertex : Inventory.Vertices)
        {
            VertexIndex[Vertex.Slot] = static_cast<int>(Blueprint.Vertices.size());
            Blueprint.Vertices.push_back({ ApplyPoint(Map, Partition.Vertices.Resolve(Vertex).Position) });
        }
        for (EdgeToken Edge : Inventory.Edges)
        {
            const EdgeProfile& Profile = Partition.Edges.Resolve(Edge);
            if (!Profile.Curve.PresentCondition()) { Outcome.Refusal = "Transform: edge without a carrier"; return Outcome; }
            CurveGeometry Curve = Partition.Curves.Resolve(Profile.Curve);
            const bool Promote = PromoteAll ? !std::holds_alternative<NurbsCurve>(Curve)
                                            : (!Analysis.Similar && ResolveConicForm(Curve).has_value() && !TryMapOrthogonalConic(Analysis, Curve).has_value());
            if (Promote)
            {
                // 📝 Exact conversion over the edge interval with τ kept, so vertices and SP-curve parameters still agree.
                if (const std::optional<NurbsCurve> Spline = ConvertCurveToNurbs(Curve, Profile.IntervalLow, Profile.IntervalHigh)) Curve = *Spline;
            }
            if (Approximation != nullptr && !std::holds_alternative<NurbsCurve>(Curve))
            {
                // 📝 No exact NURBS form (traced intersection, contact curve): certified stand-in over the same τ interval.
                const CurveApproximation Stand = ApproximateCurveAsNurbs(Curve, Profile.IntervalLow, Profile.IntervalHigh, *Approximation);
                if (!Stand.Refusal.empty()) { Outcome.Refusal = "Approximate: edge " + std::to_string(Edge.Slot) + " — " + Stand.Refusal; return Outcome; }
                Curve = *Stand.Curve;
                Tally->CurveDeviation = std::max(Tally->CurveDeviation, Stand.Deviation);
                ++Tally->ApproximatedEdges;
                TolerantEdges.resize(Blueprint.Edges.size() + 1, 0);
                TolerantEdges[Blueprint.Edges.size()] = 1;
            }
            const MappedCurve Mapped = MapCurve(Analysis, Curve);
            if (!Mapped.Refusal.empty()) { Outcome.Refusal = Mapped.Refusal; return Outcome; }
            EdgeIndex[Edge.Slot] = static_cast<int>(Blueprint.Edges.size());
            Blueprint.Edges.push_back({ Mapped.Curve, Profile.IntervalLow * Mapped.Stretch, Profile.IntervalHigh * Mapped.Stretch, VertexIndex.at(Profile.Start.Slot), VertexIndex.at(Profile.End.Slot) });
        }

        // ② Faces: mapped carrier, sense kept (flipped only for mirrored general-affine planes and NURBS), SP-curves
        //    re-fitted with the mapped chart midpoint as the periodic anchor; a mirror reverses every loop.
        for (FaceToken Face : Inventory.Faces)
        {
            const FaceProfile& Profile = Partition.Faces.Resolve(Face);
            SurfaceGeometry Surface = Partition.Surfaces.Resolve(Profile.Surface);
            const bool Promote = PromoteAll ? !ProceduralSurfaceCondition(Surface)
                                            : !Analysis.Similar && PromotableSurface(Analysis, Surface);
            if (Promote)
            {
                // 📝 The NURBS image keeps the analytic chart up to the rational reparameterisation of angles, so the old
                //    SP-curve midpoints remain valid periodic anchors.
                const auto [BoxLow, BoxHigh] = MeasureFaceChartBox(Partition, Face);
                if (const std::optional<NurbsSurface> Spline = ConvertSurfaceToNurbs(Surface, BoxLow, BoxHigh)) Surface = *Spline;
            }
            if (Approximation != nullptr && (std::holds_alternative<RollingBallSurface>(Surface) || std::holds_alternative<ParallelSurface>(Surface)))
            {
                // 📝 Same chart, so the SP-curve anchors below stay valid; the box is sampled densely because the spline
                //    clamps outside it.
                const auto [BoxLow, BoxHigh] = MeasureFaceChartBox(Partition, Face, 256);
                const SurfaceApproximation Stand = ApproximateSurfaceAsNurbs(Surface, BoxLow, BoxHigh, *Approximation);
                if (!Stand.Refusal.empty()) { Outcome.Refusal = std::string("Approximate: ") + SurfaceCategoryLabel(Surface) + " face — " + Stand.Refusal; return Outcome; }
                Tally->SurfaceDeviation = std::max(Tally->SurfaceDeviation, Stand.Deviation);
                Tally->PoleCount += Stand.Surface->Definition->CountU * Stand.Surface->Definition->CountV;
                ++Tally->ApproximatedFaces;
                Surface = *Stand.Surface;
            }
            const bool          StandIn = Approximation != nullptr && std::holds_alternative<NurbsSurface>(Surface) && ProceduralSurfaceCondition(Partition.Surfaces.Resolve(Profile.Surface))
                                          && !std::holds_alternative<NurbsSurface>(Partition.Surfaces.Resolve(Profile.Surface));
            const MappedSurface Mapped = MapSurface(Analysis, Surface);
            if (!Mapped.Refusal.empty()) { Outcome.Refusal = Mapped.Refusal; return Outcome; }
            // ReverseFaces (flip some pieces): only the faces named turn over.
            const bool    Turn = ReverseCondition && (ReverseFaces == nullptr || ReverseFaces->contains(Face.Slot));
            BlueprintFace Image;
            Image.Surface = Mapped.Surface;
            Image.Sense   = Mapped.Flip != Turn ? (Profile.Sense == Orientation::Forward ? Orientation::Reversed : Orientation::Forward) : Profile.Sense;
            for (LoopToken Loop : Profile.Loops)
            {
                std::vector<BlueprintUse> Uses;
                bool Valid = true;
                Partition.EnumerateLoopFins(Loop, [&](FinToken Fin)
                {
                    const FinProfile&  Use  = Partition.Fins.Resolve(Fin);
                    const int          Edge = EdgeIndex.at(Use.Edge.Slot);
                    const BlueprintEdge& EdgeImage = Blueprint.Edges[Edge];
                    const EdgeProfile& UsedEdge = Partition.Edges.Resolve(Use.Edge);
                    if (!Use.SurfaceCurve) { Valid = false; return; }
                    const Vector2 Middle = EvaluateParameterCurve(*Use.SurfaceCurve, 0.5 * (UsedEdge.IntervalLow + UsedEdge.IntervalHigh)).Position;
                    const Vector2 Anchor = MapAnchor(Analysis, Surface, Middle);
                    BlueprintUse Placement;
                    Placement.Edge         = Edge;
                    if (StandIn) { TolerantEdges.resize(Blueprint.Edges.size(), 0); TolerantEdges[Edge] = 1; }
                    Placement.Sense        = Use.Sense;
                    Placement.SurfaceCurve = FitSurfaceCurve(Image.Surface, EdgeImage.Curve, EdgeImage.Low, EdgeImage.High, { Anchor - Vector2{ 1.0, 1.0 }, Anchor + Vector2{ 1.0, 1.0 } }, Anchor);
                    Uses.push_back(std::move(Placement));
                });
                if (!Valid) { Outcome.Refusal = "Transform: fin without an SP-curve"; return Outcome; }
                if ((Analysis.Determinant < 0.0) != Turn)                                    // Mirror or flip: loops run the other way
                {
                    std::reverse(Uses.begin(), Uses.end());
                    for (BlueprintUse& Placement : Uses) Placement.Sense = Placement.Sense == Orientation::Forward ? Orientation::Reversed : Orientation::Forward;
                }
                Image.Loops.push_back(std::move(Uses));
            }
            Blueprint.Faces.push_back(std::move(Image));
        }
        // 📝 Tolerant model (Parasolid-style local precision): an edge stand-in and a face stand-in are each within the
        //    tolerance of the exact geometry, so an edge touching one lies within 2 × tolerance of its faces — that is its
        //    tube, and its vertices' spheres are at least as large (vertex ≥ edge ≥ face). The audit verifies the claim.
        if (Approximation != nullptr)
        {
            TolerantEdges.resize(Blueprint.Edges.size(), 0);
            const double Tube = 2.0 * Approximation->Tolerance;                              // [len] - Edge tube radius
            for (size_t Edge = 0; Edge < Blueprint.Edges.size(); ++Edge)
            {
                if (!TolerantEdges[Edge]) continue;
                BlueprintEdge& Image = Blueprint.Edges[Edge];
                Image.Precision = Tube;
                Blueprint.Vertices[Image.Start].Precision = Tube;
                Blueprint.Vertices[Image.End].Precision   = Tube;
                ++Tally->TolerantEdges;
            }
        }
        // 📝 A one-shell source of known volume V maps to det M · V: handed to the assembly, which uses it only when the
        //    image is one shell too (a cavity or a second lump falls back to integration).
        if (std::isfinite(KnownVolume) && !ReverseCondition && Approximation == nullptr && !PromoteAll) Blueprint.ShellVolumes = { KnownVolume * Analysis.Determinant };
        // ③ The blueprint is complete: assembling into Target (possibly the same partition) cannot disturb the reads above.
        const ArrangementOutcome Assembly = AssembleBody(Target, Blueprint, Approximation != nullptr ? "Approximate" : PromoteAll ? "Promote" : "Transform");
        if (!Assembly.Refusal.empty()) { Outcome.Refusal = Assembly.Refusal; return Outcome; }
        Outcome.Body        = Assembly.Body;
        Outcome.SourceFaces = Inventory.Faces;
        Outcome.Faces       = Assembly.Faces;
        // 📝 The assembly already integrated every shell (to tell outer shells from cavities); their signed sum IS the
        //    body volume (IntegrateBodyVolume sums the same integrals), so a caller need not pay for it twice.
        if (Blueprint.Category == BodyCategory::Solid && !Assembly.ShellVolumes.empty())
        {
            Outcome.Volume = 0.0;
            for (double Volume : Assembly.ShellVolumes) Outcome.Volume += Volume;
        }
        return Outcome;
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

AffineMapping TranslationMap(Vector3 Displacement) noexcept
{
    AffineMapping Map;
    Map.Shift = Displacement;
    return Map;
}

AffineMapping RotationMap(Vector3 AxisPoint, Vector3 AxisDirection, double Angle) noexcept
{
    // Rodrigues: R·v = v cos θ + (k × v) sin θ + k (k·v)(1 − cos θ).
    const Vector3 Axis = Normalize(AxisDirection);
    const double  Cosine = std::cos(Angle), Sine = std::sin(Angle);
    const auto    Turn = [&](Vector3 Vector) { return Vector * Cosine + Cross(Axis, Vector) * Sine + Axis * (Dot(Axis, Vector) * (1.0 - Cosine)); };
    AffineMapping Map{ Turn({ 1.0, 0.0, 0.0 }), Turn({ 0.0, 1.0, 0.0 }), Turn({ 0.0, 0.0, 1.0 }), {} };
    Map.Shift = AxisPoint - ApplyLinear(Map, AxisPoint);
    return Map;
}

AffineMapping ReflectionMap(Vector3 PlanePoint, Vector3 PlaneNormal) noexcept
{
    // M = I − 2 n nᵀ, Shift = 2 (p·n) n.
    const Vector3 Normal = Normalize(PlaneNormal);
    const auto    Mirror = [&](Vector3 Vector) { return Vector - Normal * (2.0 * Dot(Normal, Vector)); };
    return { Mirror({ 1.0, 0.0, 0.0 }), Mirror({ 0.0, 1.0, 0.0 }), Mirror({ 0.0, 0.0, 1.0 }), Normal * (2.0 * Dot(PlanePoint, Normal)) };
}

AffineMapping ScalingMap(Vector3 Centre, Vector3 Factors) noexcept
{
    AffineMapping Map{ { Factors.X, 0.0, 0.0 }, { 0.0, Factors.Y, 0.0 }, { 0.0, 0.0, Factors.Z }, {} };
    Map.Shift = Centre - ApplyLinear(Map, Centre);
    return Map;
}

TransformOutcome TransformBodyAcross(const ModelPartition& Partition, BodyToken Source, ModelPartition& Target, const AffineMapping& Map)
{
    return TransformBodyWith(Partition, Source, Target, Map, false);
}

TransformOutcome PromoteBodyToNurbs(ModelPartition& Partition, BodyToken Source)
{
    return TransformBodyWith(Partition, Source, Partition, TranslationMap({}), true);
}

ApproximationOutcome ApproximateBodyAsNurbs(ModelPartition& Partition, BodyToken Source, const ApproximationSettings& Settings)
{
    ApproximationOutcome Outcome;
    Outcome.Transform = TransformBodyWith(Partition, Source, Partition, TranslationMap({}), true, &Settings, &Outcome);
    return Outcome;
}

TransformOutcome TransformBody(ModelPartition& Partition, BodyToken Source, const AffineMapping& Map, double KnownVolume)
{
    return TransformBodyWith(Partition, Source, Partition, Map, false, nullptr, nullptr, false, KnownVolume);
}

TransformOutcome ReverseBody(ModelPartition& Partition, BodyToken Source)
{
    if (Partition.Bodies.Resolve(Source).Category != BodyCategory::Sheet)
    {
        TransformOutcome Refused;
        Refused.Refusal = "Flip: only a sheet (open surface) turns over; a solid's faces point out of its material by definition";
        return Refused;
    }
    return TransformBodyWith(Partition, Source, Partition, TranslationMap({}), false, nullptr, nullptr, true);
}

TransformOutcome ReverseBodyPieces(ModelPartition& Partition, BodyToken Source, const std::vector<FaceToken>& Seeds, int* Pieces)
{
    TransformOutcome Refused;
    if (Partition.Bodies.Resolve(Source).Category != BodyCategory::Sheet)
    {
        Refused.Refusal = "Flip: only a sheet (open surface) turns over; a solid's faces point out of its material by definition";
        return Refused;
    }
    if (Seeds.empty()) { Refused.Refusal = "Flip: no face picked"; return Refused; }
    // ① Pieces: faces joined through shared edges (union–find over the body's faces).
    const BodyInventory Inventory = Partition.CollectInventory(Source);
    std::map<uint32_t, uint32_t> Root;
    for (FaceToken Face : Inventory.Faces) Root[Face.Slot] = Face.Slot;
    const auto Find = [&](uint32_t Slot)
    {
        while (Root[Slot] != Slot) { Root[Slot] = Root[Root[Slot]]; Slot = Root[Slot]; }
        return Slot;
    };
    std::map<uint32_t, uint32_t> EdgeFace;                                                    // [-] - Edge slot → first face using it
    for (FaceToken Face : Inventory.Faces)
        for (LoopToken Loop : Partition.Faces.Resolve(Face).Loops)
            Partition.EnumerateLoopFins(Loop, [&](FinToken Fin)
            {
                const uint32_t Edge = Partition.Fins.Resolve(Fin).Edge.Slot;
                const auto [Seen, Fresh] = EdgeFace.emplace(Edge, Face.Slot);
                if (!Fresh) Root[Find(Face.Slot)] = Find(Seen->second);
            });
    // ② Every face whose piece holds a seed turns over; the other pieces stay.
    std::set<uint32_t> Chosen, Turned;
    for (FaceToken Seed : Seeds)
    {
        if (!Root.contains(Seed.Slot)) { Refused.Refusal = "Flip: a picked face is not on this sheet"; return Refused; }
        Chosen.insert(Find(Seed.Slot));
    }
    for (FaceToken Face : Inventory.Faces) if (Chosen.contains(Find(Face.Slot))) Turned.insert(Face.Slot);
    if (Pieces != nullptr) *Pieces = static_cast<int>(Chosen.size());
    return TransformBodyWith(Partition, Source, Partition, TranslationMap({}), false, nullptr, nullptr, true, std::numeric_limits<double>::quiet_NaN(), &Turned);
}

TransformOutcome MirrorBody(ModelPartition& Partition, BodyToken Source, Vector3 PlanePoint, Vector3 PlaneNormal)
{
    return TransformBody(Partition, Source, ReflectionMap(PlanePoint, PlaneNormal));
}

std::vector<TransformOutcome> ArrayBody(ModelPartition& Partition, BodyToken Source, Vector3 Step, int Count)
{
    std::vector<TransformOutcome> Copies;
    for (int Instance = 1; Instance < Count; ++Instance) Copies.push_back(TransformBody(Partition, Source, TranslationMap(Step * static_cast<double>(Instance))));
    return Copies;
}

std::vector<TransformOutcome> CircularArrayBody(ModelPartition& Partition, BodyToken Source, Vector3 AxisPoint, Vector3 AxisDirection, int Count, double Sweep)
{
    std::vector<TransformOutcome> Copies;
    if (Count < 1) return Copies;
    const double Pitch = Sweep / static_cast<double>(Count);                                 // [rad] - Angle between instances
    for (int Instance = 1; Instance < Count; ++Instance)
        Copies.push_back(TransformBody(Partition, Source, RotationMap(AxisPoint, AxisDirection, Pitch * static_cast<double>(Instance))));
    return Copies;
}

} // namespace Frontier::GeometrySpace::Parametric
