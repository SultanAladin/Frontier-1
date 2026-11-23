/*====================================================================================================================================
                                                       FACEOPERATIONS.CPP
====================================================================================================================================*/
// 🧩 Face verbs as carrier maps over ChangeFaces; shell through General Fuse; extrude / delete / heal / thicken by rebuild

//------------------------------------------------------------------------------------------------------------------------
//                                                    INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometrySpace/Parametric/Local/FaceOperations.h"

#include "Engine/GeometrySpace/Parametric/Diagnostics/OperationTrace.h"

#include "Engine/GeometrySpace/Parametric/Boolean/GeneralFuse.h"
#include "Engine/GeometrySpace/Parametric/Local/ConnectivityRebuild.h"
#include "Engine/GeometrySpace/Parametric/Geometry/NurbsCarrier.h"
#include "Engine/GeometrySpace/Parametric/Geometry/ParallelCarrier.h"
#include "Engine/GeometrySpace/Parametric/Geometry/RollingBallCarrier.h"
#include "Engine/GeometrySpace/Parametric/Intersection/IntersectionCurve.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <map>
#include <set>

namespace Frontier::GeometrySpace::Parametric
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    PRIVATE HELPERS
//------------------------------------------------------------------------------------------------------------------------

namespace
{
    // 📝 X ↦ k·R·X + T (R a rotation, k > 0): carriers keep their type and their frames stay aligned, so seam iso-lines and
    //    chart anchors of the old fins remain meaningful on the new carrier.
    struct SimilarityMotion
    {
        Vector3 Rows[3]     = { { 1.0, 0.0, 0.0 }, { 0.0, 1.0, 0.0 }, { 0.0, 0.0, 1.0 } };   // [-]   - Rotation rows
        double  Scale       = 1.0;                                                           // [-]   - Uniform factor k
        Vector3 Translation = {};                                                            // [len] - T

        [[nodiscard]] Vector3 Turn(Vector3 Direction) const { return { Dot(Rows[0], Direction), Dot(Rows[1], Direction), Dot(Rows[2], Direction) }; }
        [[nodiscard]] Vector3 Move(Vector3 Point) const { return Turn(Point) * Scale + Translation; }
        [[nodiscard]] OrthonormalBasis MoveFrame(const OrthonormalBasis& Frame) const { return { Move(Frame.Origin), Turn(Frame.XAxis), Turn(Frame.YAxis), Turn(Frame.ZAxis) }; }
    };

    [[nodiscard]] SimilarityMotion TranslationMotion(Vector3 Translation) { SimilarityMotion Motion; Motion.Translation = Translation; return Motion; }

    [[nodiscard]] SimilarityMotion ScalingMotion(Vector3 Centre, double Factor)
    {
        SimilarityMotion Motion;
        Motion.Scale       = Factor;
        Motion.Translation = Centre - Centre * Factor;
        return Motion;
    }

    // 📝 Rodrigues: R = cos θ·I + sin θ·[a]ₓ + (1 − cos θ)·a·aᵀ, about the line through Point.
    [[nodiscard]] SimilarityMotion RotationMotion(Vector3 Point, Vector3 Axis, double Angle)
    {
        const Vector3 Unit = Normalize(Axis);
        const double  Cosine = std::cos(Angle), Sine = std::sin(Angle), Rest = 1.0 - Cosine;
        SimilarityMotion Motion;
        Motion.Rows[0] = { Cosine + Unit.X * Unit.X * Rest, Unit.X * Unit.Y * Rest - Unit.Z * Sine, Unit.X * Unit.Z * Rest + Unit.Y * Sine };
        Motion.Rows[1] = { Unit.Y * Unit.X * Rest + Unit.Z * Sine, Cosine + Unit.Y * Unit.Y * Rest, Unit.Y * Unit.Z * Rest - Unit.X * Sine };
        Motion.Rows[2] = { Unit.Z * Unit.X * Rest - Unit.Y * Sine, Unit.Z * Unit.Y * Rest + Unit.X * Sine, Cosine + Unit.Z * Unit.Z * Rest };
        Motion.Translation = Point - Motion.Turn(Point);
        return Motion;
    }

    [[nodiscard]] SurfaceGeometry MoveSurface(const SurfaceGeometry& Surface, const SimilarityMotion& Motion);

    // 📝 Proper similarity of a carrier curve (rotation, uniform scale k, translation). Conic frames move; a traced curve
    //    moves its surfaces and guide (closed knots are invariant, open chord-length knots scale by k); a contact rail
    //    rebuilds from its moved spine and support.
    [[nodiscard]] CurveGeometry MoveCurve(const CurveGeometry& Curve, const SimilarityMotion& Motion)
    {
        if (const auto* Line = std::get_if<StraightLine>(&Curve)) return StraightLine{ Motion.Move(Line->Origin), Motion.Turn(Line->Direction) };
        if (const auto* Arc = std::get_if<CircularArc>(&Curve)) return CircularArc{ Motion.MoveFrame(Arc->Frame), Arc->Radius * Motion.Scale };
        if (const auto* Ellipse = std::get_if<EllipticalArc>(&Curve)) return EllipticalArc{ Motion.MoveFrame(Ellipse->Frame), Ellipse->MajorRadius * Motion.Scale, Ellipse->MinorRadius * Motion.Scale };
        if (const auto* Spline = std::get_if<NurbsCurve>(&Curve)) return MapNurbsCurve(*Spline->Definition, [&](Vector3 Point) { return Motion.Move(Point); });
        if (const auto* Traced = std::get_if<IntersectionCurve>(&Curve))
        {
            const TracedIntersection& Source = *Traced->Trace;
            auto Image = std::make_shared<TracedIntersection>(Source);
            double Stretch = Source.Closed ? 1.0 : Motion.Scale;                              // [-] - τ′ = Stretch·τ
            if (Source.Law)
            {
                // 📝 A ball-law spine runs on its edge's τ: it stretches like the edge (conics keep τ, open traces scale).
                auto Law = std::make_shared<BallBlendProfile>(*Source.Law);
                const auto* Edge = std::get_if<IntersectionCurve>(&Source.Law->Edge);
                Stretch = Edge && !Edge->Trace->Closed ? Motion.Scale : 1.0;
                Law->Edge      = MoveCurve(Source.Law->Edge, Motion);
                Law->Size     *= Motion.Scale;
                Law->SizeRate *= Motion.Scale / Stretch;                                     // Size(t) on t′ = Stretch·t
                Image->Law = std::move(Law);
            }
            Image->First  = MoveSurface(Source.First, Motion);
            Image->Second = MoveSurface(Source.Second, Motion);
            for (Vector3& Point : Image->Points) Point = Motion.Move(Point);
            for (Vector3& Tangent : Image->Tangents) Tangent = Motion.Turn(Tangent) * (Motion.Scale / Stretch);
            for (double& Knot : Image->Knots) Knot *= Stretch;
            Image->Perimeter *= Motion.Scale;
            return IntersectionCurve{ std::move(Image) };
        }
        const ContactDefinition& Source  = *std::get<ContactCurve>(Curve).Definition;
        const ContactCurve       Forward = ConstructContactCurve(MoveCurve(Source.Spine, Motion), MoveSurface(Source.Support, Motion));
        return Source.Direction > 0.0 ? CurveGeometry{ Forward } : ReverseContactCurve(*Forward.Definition);
    }

    SurfaceGeometry MoveSurface(const SurfaceGeometry& Surface, const SimilarityMotion& Motion)
    {
        if (const auto* Spline = std::get_if<NurbsSurface>(&Surface)) return MapNurbsSurface(*Spline->Definition, [&](Vector3 Point) { return Motion.Move(Point); });
        if (const auto* Offset = std::get_if<ParallelSurface>(&Surface))
        {
            // A similarity moves the basis poles; the distance scales with s (a proper motion keeps the normal's side).
            const NurbsSurface Basis = MapNurbsSurface(*Offset->Definition->Basis.Definition, [&](Vector3 Point) { return Motion.Move(Point); });
            return ConstructParallelSurface(Basis, Offset->Definition->Distance * Motion.Scale);
        }
        if (const auto* Ball = std::get_if<RollingBallSurface>(&Surface))
        {
            // A canal's RadiusRate scales by s / Stretch with its spine's τ (conic edges keep τ, open traces stretch by s).
            const RollingBallDefinition& Source = *Ball->Definition;
            double Stretch = 1.0;                                                            // [-] - Spine τ′ = Stretch·τ
            if (const auto* Traced = std::get_if<IntersectionCurve>(&Source.Spine))
            {
                const auto* Edge = Traced->Trace->Law ? std::get_if<IntersectionCurve>(&Traced->Trace->Law->Edge) : nullptr;
                Stretch = Traced->Trace->Law ? (Edge && !Edge->Trace->Closed ? Motion.Scale : 1.0) : (Traced->Trace->Closed ? 1.0 : Motion.Scale);
            }
            return ConstructRollingBallSurface(MoveCurve(Source.Spine, Motion), Source.Radius * Motion.Scale, MoveSurface(Source.Guide, Motion), Source.Handedness,
                                               Source.Section, MoveSurface(Source.Partner, Motion), Source.RadiusRate * Motion.Scale / Stretch);
        }
        return std::visit([&](const auto& Carrier) -> SurfaceGeometry
        {
            using Type  = std::decay_t<decltype(Carrier)>;
            if constexpr (std::is_same_v<Type, RollingBallSurface> || std::is_same_v<Type, NurbsSurface> || std::is_same_v<Type, ParallelSurface>) return Carrier;   // Moved above
            else
            {
            auto Moved  = Carrier;
            Moved.Frame = Motion.MoveFrame(Carrier.Frame);
            if constexpr (std::is_same_v<Type, CylinderSurface> || std::is_same_v<Type, SphereSurface>) Moved.Radius *= Motion.Scale;
            if constexpr (std::is_same_v<Type, ConeSurface>)  Moved.OriginRadius *= Motion.Scale;
            if constexpr (std::is_same_v<Type, TorusSurface>) { Moved.MajorRadius *= Motion.Scale; Moved.MinorRadius *= Motion.Scale; }
            return Moved;
            }
        }, Surface);
    }

    [[nodiscard]] double SenseSign(const ModelPartition& Partition, FaceToken Face) { return Partition.Faces.Resolve(Face).Sense == Orientation::Forward ? 1.0 : -1.0; }
    [[nodiscard]] const SurfaceGeometry& CarrierOf(const ModelPartition& Partition, FaceToken Face) { return Partition.Surfaces.Resolve(Partition.Faces.Resolve(Face).Surface); }

    // Edge samples of a body (vertices alone miss circles with a single vertex).
    template <typename Visit>
    void SampleBodyEdges(const ModelPartition& Partition, const BodyInventory& Inventory, Visit&& Visitor)
    {
        for (EdgeToken Edge : Inventory.Edges)
        {
            const EdgeProfile&   Profile = Partition.Edges.Resolve(Edge);
            const CurveGeometry& Curve   = Partition.Curves.Resolve(Profile.Curve);
            for (int Sample = 0; Sample <= 32; ++Sample) Visitor(EvaluateCurve(Curve, Profile.IntervalLow + (Profile.IntervalHigh - Profile.IntervalLow) * Sample / 32.0).Position);
        }
    }

    [[nodiscard]] std::optional<FaceReplacement> OffsetReplacement(const ModelPartition& Partition, FaceToken Face, double Distance)
    {
        const std::optional<SurfaceGeometry> Offset = OffsetSurface(CarrierOf(Partition, Face), Distance * SenseSign(Partition, Face));
        if (!Offset) return std::nullopt;
        return FaceReplacement{ Face, *Offset, Distance };
    }

    [[nodiscard]] std::vector<FaceToken> NeighbourFaces(const ModelPartition& Partition, const std::vector<FaceToken>& Faces)
    {
        std::set<uint32_t>     Selected, Seen;
        std::vector<FaceToken> Neighbours;
        for (FaceToken Face : Faces) Selected.insert(Face.Slot);
        for (FaceToken Face : Faces)
        {
            for (LoopToken Loop : Partition.Faces.Resolve(Face).Loops)
            {
                Partition.EnumerateLoopFins(Loop, [&](FinToken Fin)
                {
                    const FaceToken Across = Partition.Loops.Resolve(Partition.Fins.Resolve(Partition.RadialMate(Fin)).Loop).Face;
                    if (!Selected.count(Across.Slot) && Seen.insert(Across.Slot).second) Neighbours.push_back(Across);
                });
            }
        }
        return Neighbours;
    }

    //--------------------------------------------------------------------------------------------------------------------
    //                                                CARRIER MAPS
    //--------------------------------------------------------------------------------------------------------------------

    // 📝 Draft (research § K): a plane turns about its hinge line (plane ∩ neutral plane) so its outward normal tilts by
    //    Angle away from the pull direction (positive adds material above the neutral plane); a cylinder parallel to the
    //    pull becomes the cone with the same radius at the neutral plane and half-angle Angle.
    [[nodiscard]] std::string DraftReplacement(const ModelPartition& Partition, FaceToken Face, double Angle, Vector3 Pull, double NeutralHeight, FaceReplacement& Replacement)
    {
        const SurfaceGeometry& Carrier = CarrierOf(Partition, Face);
        const double           Sign    = SenseSign(Partition, Face);
        Replacement.Face = Face;
        if (const auto* Plane = std::get_if<PlaneSurface>(&Carrier))
        {
            const Vector3 Outward = Plane->Frame.ZAxis * Sign;
            const Vector3 Hinge   = Cross(Pull, Outward);
            if (Length(Hinge) < 1.0e-9) return LabelToken("F", Face) + " is perpendicular to the pull direction; nothing to draft";
            const Vector3 Climb   = Pull - Outward * Dot(Pull, Outward);                      // In-plane direction with pull component
            const double  Lift    = Dot(Plane->Frame.Origin, Pull) - NeutralHeight;
            const Vector3 Pivot   = Plane->Frame.Origin - Climb * (Lift / Dot(Climb, Pull));
            Replacement.Surface   = MoveSurface(Carrier, RotationMotion(Pivot, Hinge, Angle));
            return {};
        }
        if (const auto* Cylinder = std::get_if<CylinderSurface>(&Carrier))
        {
            const double Along = Dot(Cylinder->Frame.ZAxis, Pull);
            if (std::fabs(Along) < 1.0 - 1.0e-12) return LabelToken("F", Face) + ": cylinder axis is not along the pull direction (🚧)";
            ConeSurface Cone;
            Cone.Frame        = Cylinder->Frame;
            Cone.Frame.Origin = Cylinder->Frame.Origin + Cylinder->Frame.ZAxis * ((NeutralHeight - Dot(Cylinder->Frame.Origin, Pull)) / Along);
            Cone.OriginRadius = Cylinder->Radius;
            Cone.RadialSlope  = Sign * std::tan(Angle) * (Along > 0.0 ? 1.0 : -1.0);
            Replacement.Surface = Cone;
            return {};
        }
        return LabelToken("F", Face) + ": draft of a " + std::string(SurfaceCategoryLabel(Carrier)) + " face (🚧)";
    }

    //--------------------------------------------------------------------------------------------------------------------
    //                                                SCALE (HINGED NEIGHBOURS)
    //--------------------------------------------------------------------------------------------------------------------

    // 📝 Corpus E "scale": a planar face shrinks or grows about its boundary centre c (X ↦ c + k(X − c), its own plane
    //    is invariant), and each neighbour is re-hinged on its far boundary so that it passes through the scaled edge:
    //    plane → plane through (scaled edge, far vertices); coaxial cylinder → cone through (scaled rim, far rim). The
    //    Block row gives the frustum V = h/3·(A₀ + A₁ + √(A₀A₁)) = 1.75 of the corpus.
    [[nodiscard]] std::string ScaleReplacements(const ModelPartition& Partition, FaceToken Face, double Factor, std::vector<FaceReplacement>& Replacements)
    {
        const auto* ScaledPlane = std::get_if<PlaneSurface>(&CarrierOf(Partition, Face));
        if (!ScaledPlane) return LabelToken("F", Face) + ": scale of a curved face (🚧)";
        const Vector3          Centre = MeasureFaceBoundaryCentre(Partition, Face);
        const SimilarityMotion Motion = ScalingMotion(Centre, Factor);
        std::set<uint32_t>     OwnVertices;
        std::map<uint32_t, std::vector<EdgeToken>> Shared;                                    // Neighbour slot → edges shared with Face
        std::map<uint32_t, FaceToken>              Neighbours;
        double                 Scale = 0.0;
        for (LoopToken Loop : Partition.Faces.Resolve(Face).Loops)
        {
            Partition.EnumerateLoopFins(Loop, [&](FinToken Fin)
            {
                const FinProfile& Record = Partition.Fins.Resolve(Fin);
                const FaceToken   Across = Partition.Loops.Resolve(Partition.Fins.Resolve(Partition.RadialMate(Fin)).Loop).Face;
                OwnVertices.insert(Partition.FinStart(Fin).Slot);
                OwnVertices.insert(Partition.FinEnd(Fin).Slot);
                Shared[Across.Slot].push_back(Record.Edge);
                Neighbours[Across.Slot] = Across;
                Scale = std::max(Scale, Length(Partition.Vertices.Resolve(Partition.FinStart(Fin)).Position - Centre));
            });
        }
        const double Tolerance = 1.0e-9 * std::max(Scale, 1.0e-300);
        for (const auto& [Slot, Neighbour] : Neighbours)
        {
            if (Neighbour == Face) return LabelToken("F", Face) + " meets itself (seam); scale needs a bounded planar face";
            // Far boundary: vertices of the neighbour that the scaled face does not own (they stay where they are).
            std::vector<Vector3>   Far;
            std::vector<EdgeToken> FarRims;
            for (LoopToken Loop : Partition.Faces.Resolve(Neighbour).Loops)
            {
                Partition.EnumerateLoopFins(Loop, [&](FinToken Fin)
                {
                    const EdgeToken Edge = Partition.Fins.Resolve(Fin).Edge;
                    const VertexToken Start = Partition.FinStart(Fin);
                    if (!OwnVertices.count(Start.Slot)) Far.push_back(Partition.Vertices.Resolve(Start).Position);
                    const EdgeProfile& Profile = Partition.Edges.Resolve(Edge);
                    if (!OwnVertices.count(Profile.Start.Slot) && !OwnVertices.count(Profile.End.Slot)) FarRims.push_back(Edge);
                });
            }
            if (Far.empty()) return LabelToken("F", Neighbour) + " has no fixed far boundary to hinge on";
            const SurfaceGeometry& Carrier = CarrierOf(Partition, Neighbour);
            const double           Sign    = SenseSign(Partition, Neighbour);
            const EdgeProfile&     Edge    = Partition.Edges.Resolve(Shared[Slot].front());
            const CurveGeometry&   Curve   = Partition.Curves.Resolve(Edge.Curve);
            if (const auto* Plane = std::get_if<PlaneSurface>(&Carrier))
            {
                const auto* Line = std::get_if<StraightLine>(&Curve);
                if (!Line) return LabelToken("F", Neighbour) + ": planar neighbour meets the face along a curve";
                const Vector3 Anchor = Motion.Move(Line->Origin);
                Vector3 Normal{};
                for (const Vector3& Point : Far) if (Length(Normal = Cross(Line->Direction, Point - Anchor)) > Tolerance) break;
                if (Length(Normal) <= Tolerance) return LabelToken("F", Neighbour) + ": far boundary is collinear with the scaled edge";
                Normal = Normalize(Normal);
                if (Dot(Normal, Plane->Frame.ZAxis * Sign) < 0.0) Normal = Normal * -1.0;
                for (const Vector3& Point : Far)
                {
                    if (std::fabs(Dot(Point - Anchor, Normal)) > Tolerance) return LabelToken("F", Neighbour) + ": far boundary is not planar with the scaled edge (no single hinge)";
                }
                for (EdgeToken Other : Shared[Slot])
                {
                    const EdgeProfile& Profile = Partition.Edges.Resolve(Other);
                    for (VertexToken Vertex : { Profile.Start, Profile.End })
                    {
                        if (std::fabs(Dot(Motion.Move(Partition.Vertices.Resolve(Vertex).Position) - Anchor, Normal)) > Tolerance) return LabelToken("F", Neighbour) + ": shares a non-collinear boundary with the face";
                    }
                }
                Replacements.push_back({ Neighbour, PlaneSurface{ ConstructFrame(Anchor, Normal * Sign, Line->Direction) } });
                continue;
            }
            if (const auto* Cylinder = std::get_if<CylinderSurface>(&Carrier))
            {
                const auto* Rim = std::get_if<CircularArc>(&Curve);
                if (!Rim || std::fabs(std::fabs(Dot(Cylinder->Frame.ZAxis, ScaledPlane->Frame.ZAxis)) - 1.0) > 1.0e-12 || FarRims.size() != 1) return LabelToken("F", Neighbour) + ": cylinder is not a coaxial wall between two rims";
                const auto* Opposite = std::get_if<CircularArc>(&Partition.Curves.Resolve(Partition.Edges.Resolve(FarRims.front()).Curve));
                if (!Opposite) return LabelToken("F", Neighbour) + ": far rim is not a circle";
                const Vector3 Scaled = Motion.Move(Rim->Frame.Origin);
                const Vector3 Axis   = Cylinder->Frame.ZAxis;
                const Vector3 Drift  = Scaled - Cylinder->Frame.Origin;
                if (Length(Drift - Axis * Dot(Drift, Axis)) > Tolerance) return LabelToken("F", Neighbour) + ": the scaled rim leaves the cylinder axis (face centre off-axis)";
                const double Rise = Dot(Scaled - Opposite->Frame.Origin, Axis);
                ConeSurface Cone;
                Cone.Frame        = Cylinder->Frame;
                Cone.Frame.Origin = Cylinder->Frame.Origin + Axis * Dot(Opposite->Frame.Origin - Cylinder->Frame.Origin, Axis);
                Cone.OriginRadius = Opposite->Radius;
                Cone.RadialSlope  = (Rim->Radius * Factor - Opposite->Radius) / Rise;
                Replacements.push_back({ Neighbour, Cone });
                continue;
            }
            return LabelToken("F", Neighbour) + ": hinge refit of a " + std::string(SurfaceCategoryLabel(Carrier)) + " neighbour (🚧)";
        }
        return {};
    }

    [[nodiscard]] std::string CollectReplacements(const ModelPartition& Partition, BodyToken Body, const FaceOperationSettings& Settings, std::vector<FaceReplacement>& Replacements)
    {
        const BodyInventory Inventory = Partition.CollectInventory(Body);
        const std::vector<FaceToken> Faces = Settings.Faces.empty() ? Inventory.Faces : Settings.Faces;
        const auto Offset = [&](FaceToken Face, double Distance) -> std::string
        {
            const std::optional<FaceReplacement> Replacement = OffsetReplacement(Partition, Face, Distance);
            if (!Replacement) return LabelToken("F", Face) + ": offset " + std::to_string(Distance) + " degenerates the " + SurfaceCategoryLabel(CarrierOf(Partition, Face));
            Replacements.push_back(*Replacement);
            return {};
        };
        switch (Settings.Kind)
        {
            case FaceOperationCategory::Offset:
            case FaceOperationCategory::OffsetBody:
                for (FaceToken Face : Faces) if (std::string Refusal = Offset(Face, Settings.Distance); !Refusal.empty()) return Refusal;
                return {};
            case FaceOperationCategory::Extend:
            case FaceOperationCategory::Trim:
            {
                const double Distance = Settings.Kind == FaceOperationCategory::Extend ? Settings.Distance : -Settings.Distance;
                for (FaceToken Face : NeighbourFaces(Partition, Faces)) if (std::string Refusal = Offset(Face, Distance); !Refusal.empty()) return Refusal;
                return {};
            }
            case FaceOperationCategory::Push:
                for (FaceToken Face : Faces)
                {
                    const Vector3 Centre = MeasureFaceBoundaryCentre(Partition, Face);
                    const Vector3 Normal = EvaluateOutwardNormal(Partition, Face, Centre);
                    const bool    Planar = std::holds_alternative<PlaneSurface>(CarrierOf(Partition, Face));
                    Replacements.push_back({ Face, MoveSurface(CarrierOf(Partition, Face), TranslationMotion(Normal * Settings.Distance)),
                                             Planar ? Settings.Distance : std::numeric_limits<double>::quiet_NaN() });
                }
                return {};
            case FaceOperationCategory::Tweak:
                for (FaceToken Face : Faces) Replacements.push_back({ Face, MoveSurface(CarrierOf(Partition, Face), TranslationMotion(Settings.Vector)) });
                return {};
            case FaceOperationCategory::Scale:
                if (!(Settings.Factor > 0.0)) return "scale factor must be positive";
                for (FaceToken Face : Faces) if (std::string Refusal = ScaleReplacements(Partition, Face, Settings.Factor, Replacements); !Refusal.empty()) return Refusal;
                return {};
            case FaceOperationCategory::Rotate:
                if (Length(Settings.Axis) < 1.0e-12) return "rotation axis is zero";
                for (FaceToken Face : Faces)
                {
                    Replacements.push_back({ Face, MoveSurface(CarrierOf(Partition, Face), RotationMotion(MeasureFaceBoundaryCentre(Partition, Face), Settings.Axis, Settings.Angle)) });
                }
                return {};
            case FaceOperationCategory::Draft:
            {
                if (Length(Settings.PullDirection) < 1.0e-12) return "pull direction is zero";
                const Vector3 Pull = Normalize(Settings.PullDirection);
                double Neutral = 1e300;
                SampleBodyEdges(Partition, Inventory, [&](Vector3 Point) { Neutral = std::min(Neutral, Dot(Point, Pull)); });
                for (FaceToken Face : Faces)
                {
                    FaceReplacement Replacement;
                    if (std::string Refusal = DraftReplacement(Partition, Face, Settings.Angle, Pull, Neutral, Replacement); !Refusal.empty()) return Refusal;
                    Replacements.push_back(Replacement);
                }
                return {};
            }
            default:
                return "not a carrier verb";
        }
    }

    //--------------------------------------------------------------------------------------------------------------------
    //                                                HOLLOW
    //--------------------------------------------------------------------------------------------------------------------

    // 📝 Research § J / plan G6: cavity = every face offset inward by the wall except the open faces (kept in place), then
    //    body − cavity through General Fuse (the open faces coincide and cancel, leaving the rim).
    [[nodiscard]] std::string HollowBody(ModelPartition& Partition, BodyToken Body, const FaceOperationSettings& Settings, FaceOperationOutcome& Outcome)
    {
        if (!(Settings.Distance > 0.0)) return "wall thickness must be positive";
        const BodyInventory Inventory = Partition.CollectInventory(Body);
        std::set<uint32_t> Open;
        for (FaceToken Face : Settings.Faces) Open.insert(Face.Slot);
        std::vector<FaceReplacement> Replacements;
        for (FaceToken Face : Inventory.Faces)
        {
            if (Open.count(Face.Slot)) continue;
            const std::optional<FaceReplacement> Replacement = OffsetReplacement(Partition, Face, -Settings.Distance);
            if (!Replacement) return LabelToken("F", Face) + ": the wall is thicker than the curvature radius of the " + SurfaceCategoryLabel(CarrierOf(Partition, Face));
            Replacements.push_back(*Replacement);
        }
        const FaceChangeOutcome Cavity = ChangeFaces(Partition, Body, Replacements, "cavity");
        if (!Cavity.Refusal.empty()) return Cavity.Refusal;
        Outcome.Statistics = Cavity.Statistics;
        const BooleanOutcome Difference = ComputeBoolean(Partition, Body, Cavity.Body, BooleanOperation::Difference);
        if (!Difference.Refusal.empty()) return "body − cavity: " + Difference.Refusal;
        Outcome.Audit = AuditBody(Partition, Difference.Body);
        if (!Outcome.Audit.SoundCondition()) return "result fails audit " + std::string(Outcome.Audit.Findings.front().Code) + " on " + Outcome.Audit.Findings.front().Subject;
        std::map<uint32_t, FaceToken> CavitySource;
        for (const FaceChangeHistory& History : Cavity.FaceHistories) CavitySource[History.Result.Slot] = History.Face;
        for (const FaceHistory& History : Difference.FaceHistories)
        {
            if (History.Argument == 0) Outcome.FaceHistories.push_back({ History.Result, FaceChangeOrigin::Trimmed, History.Source, {} });
            else Outcome.FaceHistories.push_back({ History.Result, FaceChangeOrigin::Generated, CavitySource[History.Source.Slot], {} });
        }
        Outcome.Body   = Difference.Body;
        Outcome.Method = "cavity by face change (" + std::to_string(Replacements.size()) + " offsets) then General Fuse difference";
        return {};
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

Vector3 MeasureBodyBoxCentre(const ModelPartition& Partition, BodyToken Body)
{
    Vector3 Low{ 1e300, 1e300, 1e300 }, High{ -1e300, -1e300, -1e300 };
    SampleBodyEdges(Partition, Partition.CollectInventory(Body), [&](Vector3 Point)
    {
        Low  = { std::min(Low.X, Point.X), std::min(Low.Y, Point.Y), std::min(Low.Z, Point.Z) };
        High = { std::max(High.X, Point.X), std::max(High.Y, Point.Y), std::max(High.Z, Point.Z) };
    });
    return (Low + High) * 0.5;
}

Vector3 MeasureFaceBoundaryCentre(const ModelPartition& Partition, FaceToken Face)
{
    Vector3 Sum{};
    double  Total = 0.0;
    for (LoopToken Loop : Partition.Faces.Resolve(Face).Loops)
    {
        Partition.EnumerateLoopFins(Loop, [&](FinToken Fin)
        {
            const EdgeProfile&   Profile = Partition.Edges.Resolve(Partition.Fins.Resolve(Fin).Edge);
            const CurveGeometry& Curve   = Partition.Curves.Resolve(Profile.Curve);
            constexpr int Pieces = 256;                                                      // Midpoint rule: exact for lines, ~1e-5 rel. for arcs
            for (int Piece = 0; Piece < Pieces; ++Piece)
            {
                const double      τ      = Profile.IntervalLow + (Profile.IntervalHigh - Profile.IntervalLow) * (Piece + 0.5) / Pieces;
                const CurveSample Sample = EvaluateCurve(Curve, τ);
                const double      Weight = Length(Sample.Derivative) * (Profile.IntervalHigh - Profile.IntervalLow) / Pieces;
                Sum   = Sum + Sample.Position * Weight;
                Total += Weight;
            }
        });
    }
    return Total > 0.0 ? Sum * (1.0 / Total) : Sum;
}

static FaceOperationOutcome ApplyFaceOperationUntraced(ModelPartition& Partition, BodyToken Body, const FaceOperationSettings& Settings)
{
    const auto           Started = std::chrono::steady_clock::now();
    FaceOperationOutcome Outcome;
    const auto           Finish  = [&](std::string Refusal)
    {
        if (!Refusal.empty())
        {
            const std::string Label = FaceOperationLabel(Settings.Kind);
            Outcome.Refusal = Refusal.rfind(Label + ":", 0) == 0 ? Refusal : Label + ": " + Refusal;
            Outcome.Body    = {};
        }
        Outcome.Milliseconds = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - Started).count();
        return Outcome;
    };
    const BodyInventory Inventory = Partition.CollectInventory(Body);
    for (FaceToken Face : Settings.Faces)
    {
        if (std::find(Inventory.Faces.begin(), Inventory.Faces.end(), Face) == Inventory.Faces.end()) return Finish(LabelToken("F", Face) + " is not a face of the body");
    }
    switch (Settings.Kind)
    {
        case FaceOperationCategory::Hollow:   return Finish(HollowBody(Partition, Body, Settings, Outcome));
        case FaceOperationCategory::Extrude: return Finish(ExtrudePlanarFaces(Partition, Body, Settings.Faces, Settings.Distance, Outcome.Body, Outcome.FaceHistories, Outcome.Audit, Outcome.Method));
        case FaceOperationCategory::Inset:   return Finish(InsetPlanarFaces(Partition, Body, Settings.Faces, Settings.Distance, Settings.Depth, Outcome.Body, Outcome.FaceHistories, Outcome.Audit, Outcome.Method));
        case FaceOperationCategory::Delete:  return Finish(DeleteBodyFaces(Partition, Body, Settings.Faces, Outcome.Body, Outcome.Category, Outcome.FaceHistories, Outcome.Audit, Outcome.Method));
        case FaceOperationCategory::Heal:    return Finish(HealBody(Partition, Body, Outcome.Body, Outcome.FaceHistories, Outcome.Audit, Outcome.Method, Settings.Edges, Settings.Vertices));
        case FaceOperationCategory::Reshape: return Finish(ReshapeBodyVertices(Partition, Body, Settings.Vertices, Settings.Placement, Outcome.Body, Outcome.FaceHistories, Outcome.Audit, Outcome.Method));
        case FaceOperationCategory::Thicken: return Finish(ThickenSheet(Partition, Body, Settings.Distance, Outcome.Body, Outcome.FaceHistories, Outcome.Audit, Outcome.Method));
        default: break;
    }
    if (Settings.Faces.empty() && Settings.Kind != FaceOperationCategory::OffsetBody) return Finish("no faces selected");
    std::vector<FaceReplacement> Replacements;
    if (std::string Refusal = CollectReplacements(Partition, Body, Settings, Replacements); !Refusal.empty()) return Finish(Refusal);
    const FaceChangeOutcome Change = ChangeFaces(Partition, Body, Replacements, FaceOperationLabel(Settings.Kind));
    Outcome.Statistics = Change.Statistics;
    if (!Change.Refusal.empty()) return Finish(Change.Refusal);
    Outcome.Body          = Change.Body;
    Outcome.FaceHistories = Change.FaceHistories;
    Outcome.Audit         = Change.Audit;
    Outcome.Method        = "face change: " + std::to_string(Replacements.size()) + " carrier(s) replaced, edges and vertices re-solved";
    return Finish({});
}

// 📝 Traced entry (Diagnostics/OperationTrace.h): the request on entry, the outcome before the duration line.

FaceOperationOutcome ApplyFaceOperation(ModelPartition& Partition, BodyToken Body, const FaceOperationSettings& Settings)
{
    FRONTIER_TRACE_SCOPE("FaceChange", std::string(FaceOperationLabel(Settings.Kind)) + " · " + std::to_string(Settings.Faces.size()) + " face(s) · distance " +
                                       TraceNumber(Settings.Distance) + " · depth " + TraceNumber(Settings.Depth) + " · angle " + TraceNumber(Settings.Angle));
    FaceOperationOutcome Outcome = ApplyFaceOperationUntraced(Partition, Body, Settings);
    FRONTIER_TRACE("FaceChange", Outcome.Refusal.empty() ? "built by " + Outcome.Method : "refused: " + Outcome.Refusal);
    return Outcome;
}


const char* FaceOperationLabel(FaceOperationCategory Kind) noexcept
{
    switch (Kind)
    {
        case FaceOperationCategory::Offset:     return "OffsetFace";
        case FaceOperationCategory::Push:       return "Push";
        case FaceOperationCategory::Tweak:      return "Tweak";
        case FaceOperationCategory::Scale:      return "Scale";
        case FaceOperationCategory::Rotate:     return "Rotate";
        case FaceOperationCategory::Draft:      return "Draft";
        case FaceOperationCategory::Extend:     return "ExtendFace";
        case FaceOperationCategory::Trim:       return "TrimFace";
        case FaceOperationCategory::OffsetBody: return "OffsetBody";
        case FaceOperationCategory::Hollow:      return "Hollow";
        case FaceOperationCategory::Extrude:    return "ExtrudeFaces";
        case FaceOperationCategory::Delete:     return "DeleteFace";
        case FaceOperationCategory::Heal:       return "Heal";
        case FaceOperationCategory::Thicken:    return "Thicken";
        case FaceOperationCategory::Inset:      return "InsetFaces";
        case FaceOperationCategory::Reshape:    return "Reshape";
    }
    return "?";
}

} // namespace Frontier::GeometrySpace::Parametric
