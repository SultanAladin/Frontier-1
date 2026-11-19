/*====================================================================================================================================
                                                   PRIMITIVECONSTRUCTION.CPP
====================================================================================================================================*/
// 🧩 Solid primitives assembled purely from Euler edits, then given exact analytic geometry and SP-curves

#include "Engine/GeometrySpace/Parametric/Construction/PrimitiveConstruction.h"

#include <cmath>
#include <numbers>

namespace Frontier::GeometrySpace::Parametric
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    INTERNAL FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

namespace
{
    constexpr double FullTurn = 2.0 * std::numbers::pi;                                      // [rad] - 2π

    // 📝 Unwrap an edit outcome inside a construction: a refusal aborts the construction with the same reason.
    #define SOLIDARC_REQUIRE(Target, ...)   \
        auto Target##Outcome = (__VA_ARGS__); \
        if (!Target##Outcome.SucceededCondition()) return { {}, Target##Outcome.Refusal }; \
        auto Target = Target##Outcome.Value

    // Image of a 3D curve on a plane's (𝑢, 𝑣) chart, parameterised by the same τ.
    [[nodiscard]] ParameterCurve ProjectCurveOntoPlane(const CurveGeometry& Curve, const PlaneSurface& Plane)
    {
        const OrthonormalBasis& Chart = Plane.Frame;
        const auto Chart2D = [&](Vector3 Position) -> Vector2
        {
            const Vector3 Offset = Position - Chart.Origin;                                  // [len] - From the chart origin
            return { Dot(Offset, Chart.XAxis), Dot(Offset, Chart.YAxis) };
        };
        const auto Direction2D = [&](Vector3 Direction) -> Vector2 { return { Dot(Direction, Chart.XAxis), Dot(Direction, Chart.YAxis) }; };
        if (const auto* Line = std::get_if<StraightLine>(&Curve))
        {
            return ParameterLine{ Chart2D(Line->Origin), Direction2D(Line->Direction) };
        }
        const ConicForm Conic = *ResolveConicForm(Curve);
        return ParameterEllipse{ Chart2D(Conic.Centre), Direction2D(Conic.CosineAxis), Direction2D(Conic.SineAxis), 1.0 };
    }

    // ① Lines for every edge without a curve; ② planes (Newell normal over all loops) for every face without a surface;
    // ③ projected SP-curves for every fin on a plane that has none.
    void AttachLinearGeometry(ModelPartition& Partition, BodyToken Body)
    {
        const BodyInventory Inventory = Partition.CollectInventory(Body);
        for (EdgeToken Edge : Inventory.Edges)
        {
            const EdgeProfile& Profile = Partition.Edges.Resolve(Edge);
            if (Profile.Curve.PresentCondition()) continue;
            const Vector3 Start  = Partition.Vertices.Resolve(Profile.Start).Position;       // [len] - τ = 0 point
            const Vector3 End    = Partition.Vertices.Resolve(Profile.End).Position;         // [len] - τ = length point
            const double  Extent = Distance(Start, End);                                     // [len] - Edge length
            Partition.AttachCurve(Edge, StraightLine{ Start, (End - Start) * (1.0 / Extent) }, 0.0, Extent);
        }
        for (FaceToken Face : Inventory.Faces)
        {
            if (Partition.Faces.Resolve(Face).Surface.PresentCondition()) continue;
            Vector3 Newell    = {};                                                          // [len²] - Twice the vector area
            Vector3 Origin    = {};                                                          // [len]  - First boundary point
            Vector3 Reference = {};                                                          // [-]    - First edge direction
            bool    FirstCondition = true;                                                   // [-]    - Origin not yet chosen
            for (LoopToken Loop : Partition.Faces.Resolve(Face).Loops)
            {
                Partition.EnumerateLoopFins(Loop, [&](FinToken Fin)
                {
                    const Vector3 Start = Partition.Vertices.Resolve(Partition.FinStart(Fin)).Position;
                    const Vector3 End   = Partition.Vertices.Resolve(Partition.FinEnd(Fin)).Position;
                    Newell = Newell + Cross(Start, End);
                    if (FirstCondition) { Origin = Start; Reference = End - Start; FirstCondition = false; }
                });
            }
            Partition.AttachSurface(Face, PlaneSurface{ ConstructFrame(Origin, Newell, Reference) }, Orientation::Forward);
        }
        for (FinToken Fin : Inventory.Fins)
        {
            const FinProfile&  Use     = Partition.Fins.Resolve(Fin);
            const FaceProfile& Carrier = Partition.Faces.Resolve(Partition.Loops.Resolve(Use.Loop).Face);
            const auto*        Plane   = std::get_if<PlaneSurface>(&Partition.Surfaces.Resolve(Carrier.Surface));
            if (Use.SurfaceCurve.has_value() || Plane == nullptr) continue;
            const CurveGeometry& Curve = Partition.Curves.Resolve(Partition.Edges.Resolve(Use.Edge).Curve);
            Partition.AttachSurfaceCurve(Fin, ProjectCurveOntoPlane(Curve, *Plane));
        }
    }

    // 💡 Mäntylä translational sweep: MEV lifts every vertex of the face's loop, then MEF closes one side face per
    //    profile edge. The swept face ends up as the far cap, keeping its orientation.
    [[nodiscard]] EditOutcome<std::vector<FaceToken>> SweepPlanarFace(ModelPartition& Partition, FaceToken Face, Vector3 Displacement)
    {
        const LoopToken Loop = Partition.Faces.Resolve(Face).Loops.front();                  // [-] - Profile loop
        std::vector<FinToken> Profile;
        Partition.EnumerateLoopFins(Loop, [&](FinToken Fin) { Profile.push_back(Fin); });
        std::vector<VertexToken> Lifted;
        for (FinToken Fin : Profile)
        {
            const Vector3 Footing = Partition.Vertices.Resolve(Partition.FinStart(Fin)).Position; // [len] - Profile vertex
            SOLIDARC_REQUIRE(Lift, ConstructEdgeVertex(Partition, { Loop, Fin }, Footing + Displacement));
            Lifted.push_back(Lift.Vertex);
        }
        std::vector<FaceToken> SideFaces;
        for (size_t ProfileIndex = 0; ProfileIndex < Lifted.size(); ++ProfileIndex)
        {
            const VertexToken From = Lifted[ProfileIndex];                                   // [-] - Lifted vertex k
            const VertexToken To   = Lifted[(ProfileIndex + 1) % Lifted.size()];             // [-] - Lifted vertex k + 1
            SOLIDARC_REQUIRE(Side, ConstructEdgeFace(Partition, { Loop, Partition.ResolveCorner(Loop, From) }, { Loop, Partition.ResolveCorner(Loop, To) }));
            SideFaces.push_back(Side.Face);
        }
        return { SideFaces };
    }

    // Closed polygon chain in a fresh body: MVFS, MEV per further vertex, MEF to close.
    // The acorn face keeps the counter-clockwise loop; the closing face receives the reversed one.
    struct PolygonArrangement
    {
        AcornOutcome    Acorn   = {};                                                        // [-] - MVFS records
        EdgeFaceOutcome Closure = {};                                                        // [-] - Closing MEF
    };

    [[nodiscard]] EditOutcome<PolygonArrangement> ConstructPolygon(ModelPartition& Partition, const std::vector<Vector3>& Profile)
    {
        if (Profile.size() < 3) return { {}, "Polygon: fewer than three vertices" };
        SOLIDARC_REQUIRE(Acorn, ConstructBodyFaceVertex(Partition, Profile.front()));
        FinToken FirstOutbound = {};                                                         // [-] - Fin leaving vertex 0
        FinToken Corner        = {};                                                         // [-] - Fin leaving the newest vertex
        for (size_t ProfileIndex = 1; ProfileIndex < Profile.size(); ++ProfileIndex)
        {
            SOLIDARC_REQUIRE(Step, ConstructEdgeVertex(Partition, { Acorn.Loop, Corner }, Profile[ProfileIndex]));
            if (ProfileIndex == 1) FirstOutbound = Step.OutboundFin;
            Corner = Step.ReturnFin;
        }
        SOLIDARC_REQUIRE(Closure, ConstructEdgeFace(Partition, { Acorn.Loop, Corner }, { Acorn.Loop, FirstOutbound }));
        return { { Acorn, Closure } };
    }

    [[nodiscard]] std::vector<Vector3> RectangleProfile(Vector3 Centre, double HalfX, double HalfY)
    {
        return {
            Centre + Vector3{ -HalfX, -HalfY, 0.0 }, Centre + Vector3{ HalfX, -HalfY, 0.0 },
            Centre + Vector3{ HalfX, HalfY, 0.0 },   Centre + Vector3{ -HalfX, HalfY, 0.0 },
        };
    }

    [[nodiscard]] OrthonormalBasis TranslateFrame(const OrthonormalBasis& Frame, Vector3 Offset)
    {
        return { Frame.Origin + Offset, Frame.XAxis, Frame.YAxis, Frame.ZAxis };
    }

    // Plane whose natural normal is −ZAxis of the frame (a downward-facing cap).
    [[nodiscard]] PlaneSurface InvertedPlane(const OrthonormalBasis& Frame)
    {
        return { { Frame.Origin, Frame.XAxis, -Frame.YAxis, -Frame.ZAxis } };
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

EditOutcome<PrismArrangement> ConstructPrism(ModelPartition& Partition, const std::vector<Vector3>& Profile, Vector3 Displacement)
{
    SOLIDARC_REQUIRE(Polygon, ConstructPolygon(Partition, Profile));
    SOLIDARC_REQUIRE(SideFaces, SweepPlanarFace(Partition, Polygon.Acorn.Face, Displacement));
    PrismArrangement Assembly{ Polygon.Acorn.Body, Polygon.Closure.Face, Polygon.Acorn.Face, SideFaces };
    AttachLinearGeometry(Partition, Assembly.Body);
    Partition.Bodies.Resolve(Assembly.Body).Category = BodyCategory::Solid;
    return { Assembly };
}

EditOutcome<BodyToken> ConstructBlock(ModelPartition& Partition, Vector3 Corner, Vector3 Extent)
{
    const std::vector<Vector3> Profile = RectangleProfile(Corner + Vector3{ Extent.X * 0.5, Extent.Y * 0.5, 0.0 }, Extent.X * 0.5, Extent.Y * 0.5);
    SOLIDARC_REQUIRE(Assembly, ConstructPrism(Partition, Profile, { 0.0, 0.0, Extent.Z }));
    return { Assembly.Body };
}

EditOutcome<BodyToken> ConstructCylinder(ModelPartition& Partition, const OrthonormalBasis& Frame, double Radius, double Height)
{
    if (!(Radius > 0.0) || !(Height > 0.0)) return { {}, "Cylinder: radius and height must be positive" };
    const Vector3 RimVertex = Frame.Origin + Frame.XAxis * Radius;                          // [len] - Seam foot
    const Vector3 Lift       = Frame.ZAxis * Height;                                         // [len] - Axis displacement

    // ① MVFS · MEF (closed base circle) · MEV (seam) · MEF (closed top circle).
    SOLIDARC_REQUIRE(Acorn, ConstructBodyFaceVertex(Partition, RimVertex));
    SOLIDARC_REQUIRE(Rim, ConstructEdgeFace(Partition, { Acorn.Loop, {} }, { Acorn.Loop, {} }));
    SOLIDARC_REQUIRE(Seam, ConstructEdgeVertex(Partition, { Acorn.Loop, Rim.RetainedFin }, RimVertex + Lift));
    SOLIDARC_REQUIRE(Top, ConstructEdgeFace(Partition, { Acorn.Loop, Seam.ReturnFin }, { Acorn.Loop, Seam.ReturnFin }));

    // ② Exact geometry: Acorn.Face = top cap, Rim.Face = bottom cap, Top.Face = lateral.
    Partition.AttachCurve(Rim.Edge, CircularArc{ Frame, Radius }, 0.0, FullTurn);
    Partition.AttachCurve(Top.Edge, CircularArc{ TranslateFrame(Frame, Lift), Radius }, 0.0, FullTurn);
    Partition.AttachCurve(Seam.Edge, StraightLine{ RimVertex, Frame.ZAxis }, 0.0, Height);
    Partition.AttachSurface(Acorn.Face, PlaneSurface{ TranslateFrame(Frame, Lift) }, Orientation::Forward);
    Partition.AttachSurface(Rim.Face, InvertedPlane(Frame), Orientation::Forward);
    Partition.AttachSurface(Top.Face, CylinderSurface{ Frame, Radius }, Orientation::Forward);

    // ③ SP-curves on the lateral chart [0, 2π] × [0, h]: forward seam at 𝑢 = 2π, reversed seam at 𝑢 = 0.
    Partition.AttachSurfaceCurve(Rim.RetainedFin, ParameterLine{ { 0.0, 0.0 }, { 1.0, 0.0 } });
    Partition.AttachSurfaceCurve(Seam.OutboundFin, ParameterLine{ { FullTurn, 0.0 }, { 0.0, 1.0 } });
    Partition.AttachSurfaceCurve(Top.TransferredFin, ParameterLine{ { 0.0, Height }, { 1.0, 0.0 } });
    Partition.AttachSurfaceCurve(Seam.ReturnFin, ParameterLine{ { 0.0, 0.0 }, { 0.0, 1.0 } });
    AttachLinearGeometry(Partition, Acorn.Body);
    Partition.Bodies.Resolve(Acorn.Body).Category = BodyCategory::Solid;
    return { Acorn.Body };
}

EditOutcome<BodyToken> ConstructCone(ModelPartition& Partition, const OrthonormalBasis& Frame, double Radius, double Height)
{
    if (!(Radius > 0.0) || !(Height > 0.0)) return { {}, "Cone: radius and height must be positive" };
    const Vector3 RimVertex = Frame.Origin + Frame.XAxis * Radius;                          // [len] - Seam foot
    const Vector3 Apex       = Frame.Origin + Frame.ZAxis * Height;                          // [len] - Apex vertex
    const double  Generator  = Distance(RimVertex, Apex);                                   // [len] - Seam length

    // ① MVFS · MEF (closed base circle) · MEV (seam to the apex).
    SOLIDARC_REQUIRE(Acorn, ConstructBodyFaceVertex(Partition, RimVertex));
    SOLIDARC_REQUIRE(Rim, ConstructEdgeFace(Partition, { Acorn.Loop, {} }, { Acorn.Loop, {} }));
    SOLIDARC_REQUIRE(Seam, ConstructEdgeVertex(Partition, { Acorn.Loop, Rim.RetainedFin }, Apex));

    // ② Acorn.Face = lateral, Rim.Face = bottom cap. The apex row 𝑣 = h is singular.
    Partition.AttachCurve(Rim.Edge, CircularArc{ Frame, Radius }, 0.0, FullTurn);
    Partition.AttachCurve(Seam.Edge, StraightLine{ RimVertex, (Apex - RimVertex) * (1.0 / Generator) }, 0.0, Generator);
    Partition.AttachSurface(Acorn.Face, ConeSurface{ Frame, Radius, -Radius / Height }, Orientation::Forward);
    Partition.AttachSurface(Rim.Face, InvertedPlane(Frame), Orientation::Forward);
    const double Ascent = Height / Generator;                                                // [-] - d𝑣/dτ along the seam
    Partition.AttachSurfaceCurve(Rim.RetainedFin, ParameterLine{ { 0.0, 0.0 }, { 1.0, 0.0 } });
    Partition.AttachSurfaceCurve(Seam.OutboundFin, ParameterLine{ { FullTurn, 0.0 }, { 0.0, Ascent } });
    Partition.AttachSurfaceCurve(Seam.ReturnFin, ParameterLine{ { 0.0, 0.0 }, { 0.0, Ascent } });
    AttachLinearGeometry(Partition, Acorn.Body);
    Partition.Bodies.Resolve(Acorn.Body).Category = BodyCategory::Solid;
    return { Acorn.Body };
}

EditOutcome<BodyToken> ConstructFrustum(ModelPartition& Partition, const OrthonormalBasis& Frame, double OriginRadius, double TopRadius, double Height)
{
    if (!(OriginRadius > 0.0) || !(TopRadius > 0.0) || !(Height > 0.0)) return { {}, "Frustum: radii and height must be positive" };
    const Vector3 RimVertex = Frame.Origin + Frame.XAxis * OriginRadius;                      // [len] - Seam foot
    const Vector3 Lift       = Frame.ZAxis * Height;                                         // [len] - Axis displacement
    const Vector3 TopVertex  = Frame.Origin + Lift + Frame.XAxis * TopRadius;                // [len] - Seam head
    const double  Generator  = Distance(RimVertex, TopVertex);                               // [len] - Seam length

    // ① MVFS · MEF (closed base circle) · MEV (seam) · MEF (closed top circle), as for the cylinder.
    SOLIDARC_REQUIRE(Acorn, ConstructBodyFaceVertex(Partition, RimVertex));
    SOLIDARC_REQUIRE(Rim, ConstructEdgeFace(Partition, { Acorn.Loop, {} }, { Acorn.Loop, {} }));
    SOLIDARC_REQUIRE(Seam, ConstructEdgeVertex(Partition, { Acorn.Loop, Rim.RetainedFin }, TopVertex));
    SOLIDARC_REQUIRE(Top, ConstructEdgeFace(Partition, { Acorn.Loop, Seam.ReturnFin }, { Acorn.Loop, Seam.ReturnFin }));

    // ② Acorn.Face = top cap, Rim.Face = bottom cap, Top.Face = lateral cone r(𝑣) = r₀ + 𝑣·(r₁ − r₀)/h.
    Partition.AttachCurve(Rim.Edge, CircularArc{ Frame, OriginRadius }, 0.0, FullTurn);
    Partition.AttachCurve(Top.Edge, CircularArc{ TranslateFrame(Frame, Lift), TopRadius }, 0.0, FullTurn);
    Partition.AttachCurve(Seam.Edge, StraightLine{ RimVertex, (TopVertex - RimVertex) * (1.0 / Generator) }, 0.0, Generator);
    Partition.AttachSurface(Acorn.Face, PlaneSurface{ TranslateFrame(Frame, Lift) }, Orientation::Forward);
    Partition.AttachSurface(Rim.Face, InvertedPlane(Frame), Orientation::Forward);
    Partition.AttachSurface(Top.Face, ConeSurface{ Frame, OriginRadius, (TopRadius - OriginRadius) / Height }, Orientation::Forward);

    // ③ SP-curves on the lateral chart [0, 2π] × [0, h]: forward seam at 𝑢 = 2π, reversed seam at 𝑢 = 0.
    const double Ascent = Height / Generator;                                                // [-] - d𝑣/dτ along the seam
    Partition.AttachSurfaceCurve(Rim.RetainedFin, ParameterLine{ { 0.0, 0.0 }, { 1.0, 0.0 } });
    Partition.AttachSurfaceCurve(Seam.OutboundFin, ParameterLine{ { FullTurn, 0.0 }, { 0.0, Ascent } });
    Partition.AttachSurfaceCurve(Top.TransferredFin, ParameterLine{ { 0.0, Height }, { 1.0, 0.0 } });
    Partition.AttachSurfaceCurve(Seam.ReturnFin, ParameterLine{ { 0.0, 0.0 }, { 0.0, Ascent } });
    AttachLinearGeometry(Partition, Acorn.Body);
    Partition.Bodies.Resolve(Acorn.Body).Category = BodyCategory::Solid;
    return { Acorn.Body };
}

EditOutcome<BodyToken> ConstructSphere(ModelPartition& Partition, const OrthonormalBasis& Frame, double Radius)
{
    if (!(Radius > 0.0)) return { {}, "Sphere: radius must be positive" };
    constexpr double HalfTurn = std::numbers::pi * 0.5;                                      // [rad] - π/2

    // ① MVFS (south pole) · MEV (seam to the north pole).
    SOLIDARC_REQUIRE(Acorn, ConstructBodyFaceVertex(Partition, Frame.Origin - Frame.ZAxis * Radius));
    SOLIDARC_REQUIRE(Seam, ConstructEdgeVertex(Partition, { Acorn.Loop, {} }, Frame.Origin + Frame.ZAxis * Radius));

    // ② Seam = meridian through +XAxis in the (X, Z) plane, τ = latitude; poles are singular rows of the chart.
    const OrthonormalBasis Meridian{ Frame.Origin, Frame.XAxis, Frame.ZAxis, Cross(Frame.XAxis, Frame.ZAxis) };
    Partition.AttachCurve(Seam.Edge, CircularArc{ Meridian, Radius }, -HalfTurn, HalfTurn);
    Partition.AttachSurface(Acorn.Face, SphereSurface{ Frame, Radius }, Orientation::Forward);
    Partition.AttachSurfaceCurve(Seam.OutboundFin, ParameterLine{ { FullTurn, 0.0 }, { 0.0, 1.0 } });
    Partition.AttachSurfaceCurve(Seam.ReturnFin, ParameterLine{ { 0.0, 0.0 }, { 0.0, 1.0 } });
    Partition.Bodies.Resolve(Acorn.Body).Category = BodyCategory::Solid;
    return { Acorn.Body };
}

EditOutcome<BodyToken> ConstructTorus(ModelPartition& Partition, const OrthonormalBasis& Frame, double MajorRadius, double MinorRadius)
{
    if (!(MinorRadius > 0.0) || !(MajorRadius > MinorRadius)) return { {}, "Torus: requires 0 < minor radius < major radius" };

    // ① MVFS · MEF (closed 𝑢-circle) · KFMRH (genus) · MEKR (closed 𝑣-circle joining the two loops).
    SOLIDARC_REQUIRE(Acorn, ConstructBodyFaceVertex(Partition, Frame.Origin + Frame.XAxis * (MajorRadius + MinorRadius)));
    SOLIDARC_REQUIRE(Equator, ConstructEdgeFace(Partition, { Acorn.Loop, {} }, { Acorn.Loop, {} }));
    SOLIDARC_REQUIRE(Genus, CollapseFaceConstructRing(Partition, Acorn.Face, Equator.Face));
    (void)Genus;
    SOLIDARC_REQUIRE(Meridian, ConstructEdgeCollapseRing(Partition, { Acorn.Loop, Equator.RetainedFin }, { Equator.Loop, Equator.TransferredFin }));

    // ② Chart [0, 2π]²: equator at 𝑣 = 0 and 𝑣 = 2π, meridian at 𝑢 = 2π and 𝑢 = 0.
    const OrthonormalBasis MeridianFrame{ Frame.Origin + Frame.XAxis * MajorRadius, Frame.XAxis, Frame.ZAxis, Cross(Frame.XAxis, Frame.ZAxis) };
    Partition.AttachCurve(Equator.Edge, CircularArc{ Frame, MajorRadius + MinorRadius }, 0.0, FullTurn);
    Partition.AttachCurve(Meridian.Edge, CircularArc{ MeridianFrame, MinorRadius }, 0.0, FullTurn);
    Partition.AttachSurface(Acorn.Face, TorusSurface{ Frame, MajorRadius, MinorRadius }, Orientation::Forward);
    Partition.AttachSurfaceCurve(Equator.RetainedFin, ParameterLine{ { 0.0, 0.0 }, { 1.0, 0.0 } });
    Partition.AttachSurfaceCurve(Meridian.OutboundFin, ParameterLine{ { FullTurn, 0.0 }, { 0.0, 1.0 } });
    Partition.AttachSurfaceCurve(Equator.TransferredFin, ParameterLine{ { 0.0, FullTurn }, { 1.0, 0.0 } });
    Partition.AttachSurfaceCurve(Meridian.ReturnFin, ParameterLine{ { 0.0, 0.0 }, { 0.0, 1.0 } });
    Partition.Bodies.Resolve(Acorn.Body).Category = BodyCategory::Solid;
    return { Acorn.Body };
}

EditOutcome<BodyToken> ConstructPerforatedBlock(ModelPartition& Partition, Vector3 Extent, Vector3 HoleExtent)
{
    if (!(HoleExtent.X < Extent.X) || !(HoleExtent.Y < Extent.Y) || !(HoleExtent.X > 0.0) || !(HoleExtent.Y > 0.0))
    {
        return { {}, "PerforatedBlock: the hole must lie strictly inside the block" };
    }
    const Vector3 Centre = { Extent.X * 0.5, Extent.Y * 0.5, 0.0 };                          // [len] - Profile centre
    const Vector3 Lift   = { 0.0, 0.0, Extent.Z };                                           // [len] - Sweep displacement

    // ① Outer prism: StartFace = bottom (z = 0), EndFace = top (z = h). Topology only; geometry comes last.
    SOLIDARC_REQUIRE(Outer, ConstructPolygon(Partition, RectangleProfile(Centre, Extent.X * 0.5, Extent.Y * 0.5)));
    SOLIDARC_REQUIRE(OuterSides, SweepPlanarFace(Partition, Outer.Acorn.Face, Lift));
    (void)OuterSides;
    const FaceToken Bottom  = Outer.Closure.Face;                                            // [-] - z = 0 face
    const FaceToken Top     = Outer.Acorn.Face;                                              // [-] - z = h face
    const LoopToken TopLoop = Partition.Faces.Resolve(Top).Loops.front();                    // [-] - Top boundary

    // ② Inner square in the top face: bridge MEV, three chain MEVs, closing MEF. The top face keeps the inner square.
    const std::vector<Vector3> Inner = RectangleProfile(Centre + Lift, HoleExtent.X * 0.5, HoleExtent.Y * 0.5);
    SOLIDARC_REQUIRE(Bridge, ConstructEdgeVertex(Partition, { TopLoop, Partition.Loops.Resolve(TopLoop).FirstFin }, Inner[0]));
    FinToken Corner        = Bridge.ReturnFin;                                               // [-] - Fin leaving the newest vertex
    FinToken FirstOutbound = {};                                                             // [-] - Fin leaving inner vertex 0
    for (size_t InnerIndex = 1; InnerIndex < Inner.size(); ++InnerIndex)
    {
        SOLIDARC_REQUIRE(Step, ConstructEdgeVertex(Partition, { TopLoop, Corner }, Inner[InnerIndex]));
        if (InnerIndex == 1) FirstOutbound = Step.OutboundFin;
        Corner = Step.ReturnFin;
    }
    SOLIDARC_REQUIRE(Square, ConstructEdgeFace(Partition, { TopLoop, Corner }, { TopLoop, FirstOutbound }));
    (void)Square;

    // ③ KEMR on the bridge turns the inner square into a ring of the outer top face.
    SOLIDARC_REQUIRE(Ring, CollapseEdgeConstructRing(Partition, Bridge.Edge));
    (void)Ring;

    // ④ Sweep the square down through the block, then KFMRH it into the bottom face — a through hole (genus 1).
    SOLIDARC_REQUIRE(Walls, SweepPlanarFace(Partition, Top, -Lift));
    (void)Walls;
    SOLIDARC_REQUIRE(Perforation, CollapseFaceConstructRing(Partition, Bottom, Top));
    (void)Perforation;

    AttachLinearGeometry(Partition, Outer.Acorn.Body);
    Partition.Bodies.Resolve(Outer.Acorn.Body).Category = BodyCategory::Solid;
    return { Outer.Acorn.Body };
}

EditOutcome<BodyToken> ConstructHollowBlock(ModelPartition& Partition, Vector3 Extent, Vector3 CavityExtent)
{
    if (!(CavityExtent.X < Extent.X) || !(CavityExtent.Y < Extent.Y) || !(CavityExtent.Z < Extent.Z))
    {
        return { {}, "HollowBlock: the cavity must lie strictly inside the block" };
    }
    SOLIDARC_REQUIRE(Outer, ConstructBlock(Partition, {}, Extent));
    const Vector3 CavityCorner = (Extent - CavityExtent) * 0.5;                              // [len] - Centred cavity corner
    SOLIDARC_REQUIRE(Cavity, ConstructBlock(Partition, CavityCorner, CavityExtent));

    // 📝 The cavity block's shell is turned inside out and handed to the outer block's solid region; the emptied
    //    cavity body is reclaimed by TransferShell.
    const RegionToken     SolidRegion = Partition.Bodies.Resolve(Outer).Regions[1];          // [-] - Outer solid region
    const SolidShellToken CavityShell = Partition.Regions.Resolve(Partition.Bodies.Resolve(Cavity).Regions[1]).Shells.front();
    ReverseShellOrientation(Partition, CavityShell);
    SOLIDARC_REQUIRE(Transfer, TransferShell(Partition, CavityShell, SolidRegion));
    (void)Transfer;
    return { Outer };
}

#undef SOLIDARC_REQUIRE

} // namespace Frontier::GeometrySpace::Parametric
