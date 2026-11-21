/*====================================================================================================================================
                                                          SOLIDARCSCENE.CPP
====================================================================================================================================*/
// 🧩 Display arrays of the current body (per-face corners, edge segments, name tables) and the CPU ray pick

//------------------------------------------------------------------------------------------------------------------------
//                                                    INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Workspaces/ParametricSketcher/Document/SketcherScene.h"

#include "Engine/GeometrySpace/Parametric/Containment/WindingNumber.h"
#include "Engine/GeometrySpace/Parametric/Diagnostics/OperationTrace.h"

#include "Engine/GeometrySpace/Parametric/Geometry/CurveGeometry.h"
#include "Engine/GeometrySpace/Parametric/Geometry/NurbsCarrier.h"
#include "Engine/GeometrySpace/Parametric/Geometry/SurfaceGeometry.h"
#include "Engine/GeometrySpace/Parametric/Sketch/SketchProfile.h"
#include "Engine/GeometrySpace/Parametric/Sketch/SketchSpline.h"
#include "Engine/GeometrySpace/Parametric/Tessellation/BodyTessellation.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <limits>
#include <mutex>
#include <numbers>
#include <set>

namespace Frontier::Workspaces::Sketcher
{

using namespace Frontier::GeometrySpace::Parametric;

namespace
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    INTERNAL FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

using Triple = std::array<double, 3>;

[[nodiscard]] Triple Difference(const Triple& Left, const Triple& Right) { return { Left[0] - Right[0], Left[1] - Right[1], Left[2] - Right[2] }; }
[[nodiscard]] double Dot(const Triple& Left, const Triple& Right) { return Left[0] * Right[0] + Left[1] * Right[1] + Left[2] * Right[2]; }

[[nodiscard]] Triple Cross(const Triple& Left, const Triple& Right)
{
    return { Left[1] * Right[2] - Left[2] * Right[1], Left[2] * Right[0] - Left[0] * Right[2], Left[0] * Right[1] - Left[1] * Right[0] };
}

[[nodiscard]] Triple Widen(const float Source[3]) { return { Source[0], Source[1], Source[2] }; }
[[nodiscard]] Triple Widen(const Vector3& Source) { return { Source.X, Source.Y, Source.Z }; }

void Narrow(const Triple& Source, float Target[3])
{
    for (int Axis = 0; Axis < 3; ++Axis) Target[Axis] = static_cast<float>(Source[Axis]);
}

[[nodiscard]] Triple Normalised(const Triple& Source)
{
    const double Length = std::sqrt(Dot(Source, Source));
    if (Length <= 0.0) return { 0.0, 0.0, 1.0 };
    return { Source[0] / Length, Source[1] / Length, Source[2] / Length };
}

// Möller–Trumbore: ray parameter of the hit, or nothing.
[[nodiscard]] std::optional<double> IntersectTriangle(const OrbitRay& Ray, const Triple& First, const Triple& Second, const Triple& Third)
{
    const Triple SideA = Difference(Second, First);                                          // [len] - First → second
    const Triple SideB = Difference(Third, First);                                           // [len] - First → third
    const Triple Pivot = Cross(Ray.Direction, SideB);
    const double Determinant = Dot(SideA, Pivot);
    if (std::abs(Determinant) < 1.0e-14) return std::nullopt;
    const double Inverse = 1.0 / Determinant;
    const Triple Offset  = Difference(Ray.Origin, First);
    const double Along   = Dot(Offset, Pivot) * Inverse;                                     // [-] - Barycentric weight of Second
    if (Along < 0.0 || Along > 1.0) return std::nullopt;
    const Triple Lever   = Cross(Offset, SideA);
    const double Across  = Dot(Ray.Direction, Lever) * Inverse;                              // [-] - Barycentric weight of Third
    if (Across < 0.0 || Along + Across > 1.0) return std::nullopt;
    const double Reach   = Dot(SideB, Lever) * Inverse;                                      // [len] - Ray parameter
    if (Reach <= 0.0) return std::nullopt;
    return Reach;
}

// 📝 Closest approach of the ray and a segment: (ray parameter, separation). Standard two-line solve, clamped.
[[nodiscard]] std::pair<double, double> ApproachSegment(const OrbitRay& Ray, const Triple& Start, const Triple& End)
{
    const Triple Span   = Difference(End, Start);
    const Triple Offset = Difference(Ray.Origin, Start);
    const double SpanSquared = Dot(Span, Span);
    const double Coupling    = Dot(Ray.Direction, Span);
    const double RayOffset   = Dot(Ray.Direction, Offset);
    const double SpanOffset  = Dot(Span, Offset);
    const double Denominator = SpanSquared - Coupling * Coupling;                            // [len²] - |d|² = 1
    double Fraction = Denominator > 1.0e-18 ? (SpanOffset - Coupling * RayOffset) / Denominator : 0.0;
    Fraction        = std::clamp(Fraction, 0.0, 1.0);
    const Triple Spot = { Start[0] + Span[0] * Fraction, Start[1] + Span[1] * Fraction, Start[2] + Span[2] * Fraction };
    const double Reach = std::max(0.0, Dot(Difference(Spot, Ray.Origin), Ray.Direction));
    const Triple Foot  = { Ray.Origin[0] + Ray.Direction[0] * Reach, Ray.Origin[1] + Ray.Direction[1] * Reach, Ray.Origin[2] + Ray.Direction[2] * Reach };
    const Triple Gap   = Difference(Spot, Foot);
    return { Reach, std::sqrt(Dot(Gap, Gap)) };
}

struct BodyTessellationEntry
{
    uint64_t            Signature = 0;                                                       // [-] - Exact topology + carrier + settings fingerprint
    size_t              FaceCount = 0;                                                       // [-] - Guard against 64-bit hash collision
    size_t              EdgeCount = 0;                                                       // [-] - Guard against 64-bit hash collision
    TessellationOutcome Outcome   = {};                                                      // [-] - Cached mesh + edge samplings
};

[[nodiscard]] uint64_t FingerprintBody(const ModelPartition& Partition, BodyToken Body, const TessellationSettings& Settings,
                                       size_t& OutFaces, size_t& OutEdges)
{
    uint64_t Hash = 14695981039346656037ull;
    const auto MixWord = [&](uint64_t Value) noexcept
    {
        Hash ^= Value;
        Hash *= 1099511628211ull;
    };
    const auto MixReal = [&](double Value) noexcept
    {
        uint64_t Bits = 0;
        std::memcpy(&Bits, &Value, sizeof(Bits));
        MixWord(Bits);
    };
    const auto MixVec = [&](Vector3 Point) noexcept { MixReal(Point.X); MixReal(Point.Y); MixReal(Point.Z); };

    MixWord(Body.Slot);
    MixWord(Body.Generation);
    MixReal(Settings.SagRatio);
    MixReal(Settings.EdgeLengthRatio);
    const BodyInventory Inventory = Partition.CollectInventory(Body);
    OutFaces = Inventory.Faces.size();
    OutEdges = Inventory.Edges.size();
    MixWord(OutFaces);
    MixWord(OutEdges);
    MixWord(Inventory.Vertices.size());
    for (VertexToken Vertex : Inventory.Vertices)
    {
        MixWord(Vertex.Slot);
        MixWord(Vertex.Generation);
        MixVec(Partition.Vertices.Resolve(Vertex).Position);
    }
    for (EdgeToken Edge : Inventory.Edges)
    {
        const EdgeProfile& Profile = Partition.Edges.Resolve(Edge);
        MixWord(Edge.Slot);
        MixWord(Edge.Generation);
        MixReal(Profile.IntervalLow);
        MixReal(Profile.IntervalHigh);
        if (Profile.Curve.PresentCondition())
        {
            const CurveGeometry& Curve = Partition.Curves.Resolve(Profile.Curve);
            MixWord(Curve.index());
            if (const auto* Line = std::get_if<StraightLine>(&Curve)) { MixVec(Line->Origin); MixVec(Line->Direction); }
            else if (const auto* Arc = std::get_if<CircularArc>(&Curve)) { MixVec(Arc->Frame.Origin); MixVec(Arc->Frame.ZAxis); MixReal(Arc->Radius); }
            else if (const auto* Ellipse = std::get_if<EllipticalArc>(&Curve)) { MixVec(Ellipse->Frame.Origin); MixReal(Ellipse->MajorRadius); MixReal(Ellipse->MinorRadius); }
            else if (const auto* Spline = std::get_if<NurbsCurve>(&Curve); Spline && Spline->Definition)
            {
                MixWord(reinterpret_cast<uintptr_t>(Spline->Definition.get()));
                MixWord(Spline->Definition->Poles.size());
                if (!Spline->Definition->Poles.empty()) { MixVec(Spline->Definition->Poles.front()); MixVec(Spline->Definition->Poles.back()); }
            }
            else return 0;                                                                   // Intersection / Contact curves: always tessellate fresh
        }
    }
    for (FaceToken Face : Inventory.Faces)
    {
        const FaceProfile& Profile = Partition.Faces.Resolve(Face);
        MixWord(Face.Slot);
        MixWord(Face.Generation);
        MixWord(static_cast<uint64_t>(Profile.Sense));
        MixWord(Profile.Loops.size());
        for (LoopToken Loop : Profile.Loops)
            Partition.EnumerateLoopFins(Loop, [&](FinToken Fin) { MixWord(Partition.Fins.Resolve(Fin).Edge.Slot); MixWord(static_cast<uint64_t>(Partition.Fins.Resolve(Fin).Sense)); });
        if (Profile.Surface.PresentCondition())
        {
            const SurfaceGeometry& Surface = Partition.Surfaces.Resolve(Profile.Surface);
            MixWord(Surface.index());
            if (const auto* Plane = std::get_if<PlaneSurface>(&Surface)) { MixVec(Plane->Frame.Origin); MixVec(Plane->Frame.ZAxis); }
            else if (const auto* Cyl = std::get_if<CylinderSurface>(&Surface)) { MixVec(Cyl->Frame.Origin); MixVec(Cyl->Frame.ZAxis); MixReal(Cyl->Radius); }
            else if (const auto* Cone = std::get_if<ConeSurface>(&Surface)) { MixVec(Cone->Frame.Origin); MixVec(Cone->Frame.ZAxis); MixReal(Cone->OriginRadius); MixReal(Cone->RadialSlope); }
            else if (const auto* Sph = std::get_if<SphereSurface>(&Surface)) { MixVec(Sph->Frame.Origin); MixReal(Sph->Radius); }
            else if (const auto* Tor = std::get_if<TorusSurface>(&Surface)) { MixVec(Tor->Frame.Origin); MixVec(Tor->Frame.ZAxis); MixReal(Tor->MajorRadius); MixReal(Tor->MinorRadius); }
            else if (const auto* Spline = std::get_if<NurbsSurface>(&Surface); Spline && Spline->Definition)
            {
                MixWord(reinterpret_cast<uintptr_t>(Spline->Definition.get()));
                MixWord(Spline->Definition->Poles.size());
                if (!Spline->Definition->Poles.empty()) { MixVec(Spline->Definition->Poles.front()); MixVec(Spline->Definition->Poles.back()); }
            }
            else return 0;
        }
    }
    return Hash == 0 ? 1 : Hash;
}

[[nodiscard]] TessellationOutcome TessellateBodyCached(const ModelPartition& Partition, BodyToken Body, const TessellationSettings& Settings)
{
    static std::mutex                         Guard;
    static std::vector<BodyTessellationEntry> Cache;

    size_t         FaceCount = 0, EdgeCount = 0;
    const uint64_t Signature = FingerprintBody(Partition, Body, Settings, FaceCount, EdgeCount);
    if (Signature != 0)
    {
        std::lock_guard<std::mutex> Lock(Guard);
        for (size_t Index = 0; Index < Cache.size(); ++Index)
        {
            if (Cache[Index].Signature != Signature || Cache[Index].FaceCount != FaceCount || Cache[Index].EdgeCount != EdgeCount) continue;
            BodyTessellationEntry Hit = Cache[Index];
            if (Index + 1 < Cache.size())
            {
                Cache.erase(Cache.begin() + static_cast<std::ptrdiff_t>(Index));
                Cache.push_back(Hit);
            }
            return Hit.Outcome;
        }
    }
    TessellationOutcome Fresh = TessellateBody(Partition, Body, Settings);
    if (Signature != 0 && Fresh.Refusal.empty())
    {
        std::lock_guard<std::mutex> Lock(Guard);
        if (Cache.size() >= 32) Cache.erase(Cache.begin());
        Cache.push_back({ Signature, FaceCount, EdgeCount, Fresh });
    }
    return Fresh;
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

SketcherDeviationPlot MeasureSketcherDeviation(const SketcherDocument& Document)
{
    SketcherDeviationPlot Map;
    Map.Revision = Document.Revision;
    if (Document.Regeneration.Panels.empty()) return Map;
    constexpr int Grid = 8;                                                                  // 9 × 9 seeds per panel
    for (const FeatureStep& Step : Document.Tree.Steps)
    {
        if (Step.Kind != FeatureStepCategory::SpaceCurve) continue;
        for (const Vector3& Point : Step.Points)
        {
            SketcherDeviationNeedle Needle;
            Needle.Point    = Point;
            Needle.Distance = std::numeric_limits<double>::infinity();
            for (const auto& [Name, Surface] : Document.Regeneration.Panels)
            {
                const NurbsDomain AlongU = MeasureNurbsSurfaceDomain(*Surface.Definition, true);
                const NurbsDomain AlongV = MeasureNurbsSurfaceDomain(*Surface.Definition, false);
                Vector2 Seed  = { 0.5, 0.5 };
                double  Least = std::numeric_limits<double>::infinity();
                for (int First = 0; First <= Grid; ++First)
                {
                    for (int Second = 0; Second <= Grid; ++Second)
                    {
                        const Vector2 Parameter = { AlongU.Low + (AlongU.High - AlongU.Low) * First / Grid, AlongV.Low + (AlongV.High - AlongV.Low) * Second / Grid };
                        const double  Gap       = Length(EvaluateNurbsSurface(Surface, Parameter).Position - Point);
                        if (Gap < Least) { Least = Gap; Seed = Parameter; }
                    }
                }
                const Vector3 Foot = EvaluateNurbsSurface(Surface, ProjectOntoNurbsSurface(Surface, Point, Seed)).Position;
                const double  Gap  = Length(Foot - Point);
                if (Gap < Needle.Distance) { Needle.Distance = Gap; Needle.Foot = Foot; }
            }
            Map.Worst = std::max(Map.Worst, Needle.Distance);
            Map.Mean += Needle.Distance;
            Map.Needles.push_back(Needle);
        }
    }
    if (!Map.Needles.empty()) Map.Mean /= double(Map.Needles.size());
    return Map;
}

double SketcherZebraPhase(Vector3 Normal, Vector3 Forward, Vector3 Up, double Stripes) noexcept
{
    const Vector3 Reflected = Forward - Normal * (2.0 * Dot(Forward, Normal));
    return Stripes * std::acos(std::clamp(Dot(Reflected, Up), -1.0, 1.0)) / std::numbers::pi;
}

SketcherScene BuildSketcherScene(const SketcherDocument& Document, double SagRatio, bool FineCondition)
{
    FRONTIER_TRACE_SCOPE("Scene", std::string("BuildSketcherScene · ") + std::to_string(Document.Regeneration.Bodies.size()) + " body(ies)" + (FineCondition ? " · fine" : " · coarse"));
    const auto    Start = std::chrono::steady_clock::now();
    SketcherScene Scene;
    static std::atomic<uint64_t> BuildCounter{0};
    Scene.Revision   = Document.Revision;
    Scene.Generation = ++BuildCounter;
    if (!Document.Summary.PresentCondition || !Document.Partition) return Scene;
    const ModelPartition& Partition = *Document.Partition;

    TessellationSettings Settings;
    Settings.SagRatio = SagRatio;
    if (FineCondition) Settings.EdgeLengthRatio = FineEdgeRatio;
    bool BoundsCondition = false;
    for (int Pass = 0; Pass < 2; ++Pass)
    {
        for (const RegeneratedBody& Regenerated : Document.Regeneration.Bodies)
        {
            if (!Regenerated.Body.PresentCondition() || !Partition.Bodies.Authenticate(Regenerated.Body)) continue;
            if (Document.Appearance.HiddenBodies.contains(Regenerated.Key)) continue;                // Plasticity H: out of view and picking
            const bool LiveCutterCondition = Document.Appearance.LiveCutters.contains(Regenerated.Key);
            if ((Pass == 1) != LiveCutterCondition) continue;                                        // Pass 0: opaque solids · Pass 1: semitransparent live cutters
            const TessellationOutcome Outcome = TessellateBodyCached(Partition, Regenerated.Body, Settings);
            Scene.Sag = std::max(Scene.Sag, Outcome.Sag);
            if (!Outcome.Refusal.empty()) Scene.Refusal += (Scene.Refusal.empty() ? "" : "; ") + Outcome.Refusal;
            const PolygonNetworkStructure&  Complex   = Outcome.Complex;
            const BodyNaming&      Naming    = Regenerated.Naming;
            const uint32_t         BodyIndex = static_cast<uint32_t>(Scene.Bodies.size());
            SketcherSceneBody      Listed;
            Listed.Key       = Regenerated.Key;
            Listed.Caption   = SketcherBodyCaption(Document, Regenerated.Key);
            Listed.FirstFace = static_cast<uint32_t>(Scene.Faces.size());
            Listed.FirstEdge = static_cast<uint32_t>(Scene.Edges.size());

            // ① faces: three corners per triangle (both opaque solids and semitransparent LiveCutters emit full faces for shading & picking)
            for (const PolygonNetFace& Patch : Complex.Faces)
            {
                SketcherSceneFace Face;
                const auto Named  = Naming.Faces.find(Patch.Face.Slot);
                Face.Name         = Named != Naming.Faces.end() ? Named->second : "Face" + std::to_string(Patch.Face.Slot);
                Face.Body         = BodyIndex;
                Face.Slot         = Patch.Face.Slot;
                Face.FirstCorner  = static_cast<uint32_t>(Scene.Corners.size());
                const FaceProfile& Profile = Partition.Faces.Resolve(Patch.Face);
                const bool CarrierCondition = Profile.Surface.PresentCondition();
                const double Sense = Profile.Sense == Orientation::Forward ? 1.0 : -1.0;
                if (CarrierCondition) Face.Surface = SurfaceCategoryLabel(Partition.Surfaces.Resolve(Profile.Surface));
                const uint32_t FaceIndex = static_cast<uint32_t>(Scene.Faces.size());
                for (int Triangle = Patch.FirstTriangle; Triangle < Patch.FirstTriangle + Patch.TriangleCount; ++Triangle)
                {
                    const std::array<int, 3>& Corner = Complex.Triangles[Triangle];
                    const Triple First  = Widen(Complex.Positions[Corner[0]]);
                    const Triple Second = Widen(Complex.Positions[Corner[1]]);
                    const Triple Third  = Widen(Complex.Positions[Corner[2]]);
                    const Triple Facet  = Cross(Difference(Second, First), Difference(Third, First));
                    Face.Area          += 0.5 * std::sqrt(Dot(Facet, Facet));
                    const Triple FacetNormal = Normalised(Facet);
                    for (int Index = 0; Index < 3; ++Index)
                    {
                        Triple Normal = FacetNormal;
                        if (CarrierCondition)
                        {
                            const Vector2 Chart = Complex.TriangleCharts[Triangle][Index];
                            Triple Exact = Widen(EvaluateSurfaceNormal(Partition.Surfaces.Resolve(Profile.Surface), Chart));
                            Exact        = { Exact[0] * Sense, Exact[1] * Sense, Exact[2] * Sense };
                            if (std::isfinite(Exact[0]) && Dot(Exact, FacetNormal) > 0.25) Normal = Normalised(Exact);
                        }
                        SketcherSceneCorner Packed;
                        Narrow(Widen(Complex.Positions[Corner[Index]]), Packed.Position);
                        Narrow(Normal, Packed.Normal);
                        Packed.Face = FaceIndex;
                        Scene.Corners.push_back(Packed);
                    }
                }
                Face.CornerCount = static_cast<uint32_t>(Scene.Corners.size()) - Face.FirstCorner;
                Scene.Faces.push_back(std::move(Face));
            }

            // ② edges: the shared samplings, so edges sit exactly on the face boundaries
            for (const PolygonNetEdge& Sampled : Complex.Edges)
            {
                SketcherSceneEdge Edge;
                const auto Named   = Naming.Edges.find(Sampled.Edge.Slot);
                Edge.Name          = Named != Naming.Edges.end() ? Named->second : "Edge" + std::to_string(Sampled.Edge.Slot);
                Edge.Body          = BodyIndex;
                Edge.Slot          = Sampled.Edge.Slot;
                Edge.FirstSegment  = static_cast<uint32_t>(Scene.Segments.size());
                const EdgeProfile& Profile = Partition.Edges.Resolve(Sampled.Edge);
                if (Profile.Curve.PresentCondition()) Edge.Curve = CurveCategoryLabel(Partition.Curves.Resolve(Profile.Curve));
                const uint32_t EdgeIndex = static_cast<uint32_t>(Scene.Edges.size());
                for (size_t Index = 1; Index < Sampled.Vertices.size(); ++Index)
                {
                    const Triple From = Widen(Complex.Positions[Sampled.Vertices[Index - 1]]);
                    const Triple To   = Widen(Complex.Positions[Sampled.Vertices[Index]]);
                    const Triple Span = Difference(To, From);
                    Edge.Length      += std::sqrt(Dot(Span, Span));
                    SketcherSceneSegment Packed;
                    Narrow(From, Packed.Start);
                    Narrow(To, Packed.End);
                    Packed.Edge = EdgeIndex;
                    Scene.Segments.push_back(Packed);
                }
                Edge.SegmentCount = static_cast<uint32_t>(Scene.Segments.size()) - Edge.FirstSegment;
                Scene.Edges.push_back(std::move(Edge));
            }

            // ②′ vertices: the named body vertices (mode 1 picks them for a direct reshape)
            for (const VertexToken Vertex : Partition.CollectInventory(Regenerated.Body).Vertices)
            {
                const auto Named = Naming.Vertices.find(Vertex.Slot);
                if (Named == Naming.Vertices.end()) continue;
                const Vector3 Position = Partition.Vertices.Resolve(Vertex).Position;
                Scene.Vertices.push_back({ Named->second, Vertex.Slot, BodyIndex, { Position.X, Position.Y, Position.Z } });
            }

            // ③ bounds: the union over every body
            if (!Complex.Positions.empty())
            {
                if (!BoundsCondition)
                {
                    Scene.BoundsLow  = { std::numeric_limits<double>::max(), std::numeric_limits<double>::max(), std::numeric_limits<double>::max() };
                    Scene.BoundsHigh = { std::numeric_limits<double>::lowest(), std::numeric_limits<double>::lowest(), std::numeric_limits<double>::lowest() };
                    BoundsCondition  = true;
                }
                for (const Vector3& Position : Complex.Positions)
                {
                    const Triple Point = Widen(Position);
                    for (int Axis = 0; Axis < 3; ++Axis)
                    {
                        Scene.BoundsLow[Axis]  = std::min(Scene.BoundsLow[Axis], Point[Axis]);
                        Scene.BoundsHigh[Axis] = std::max(Scene.BoundsHigh[Axis], Point[Axis]);
                    }
                }
            }
            Listed.FaceCount = static_cast<uint32_t>(Scene.Faces.size()) - Listed.FirstFace;
            Listed.EdgeCount = static_cast<uint32_t>(Scene.Edges.size()) - Listed.FirstEdge;
            Scene.Bodies.push_back(std::move(Listed));
        }
        if (Pass == 0)
        {
            Scene.SolidCornerCount  = static_cast<uint32_t>(Scene.Corners.size());
            Scene.SolidSegmentCount = static_cast<uint32_t>(Scene.Segments.size());
        }
    }
    Scene.Milliseconds = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - Start).count();
    return Scene;
}

std::optional<SketcherPick> PickSketcherScene(const SketcherScene& Scene, const OrbitRay& Ray, double EdgeReach, bool XRayEnabled,
                                              const SketcherSectionClip* Section)
{
    const auto Side = [&](const Triple& Point)
    {
        return Section ? Section->Plane[0] * Point[0] + Section->Plane[1] * Point[1] + Section->Plane[2] * Point[2] + Section->Plane[3] : 0.0;
    };
    const uint32_t SolidLimit  = Scene.SolidCornerCount > 0 && Scene.SolidCornerCount <= Scene.Corners.size() ? Scene.SolidCornerCount : static_cast<uint32_t>(Scene.Corners.size());
    const uint32_t SolidSegs   = Scene.SolidSegmentCount > 0 && Scene.SolidSegmentCount <= Scene.Segments.size() ? Scene.SolidSegmentCount : static_cast<uint32_t>(Scene.Segments.size());
    std::optional<SketcherPick> Face;
    for (size_t Corner = 0; Corner + 2 < Scene.Corners.size(); Corner += 3)
    {
        const Triple First  = Widen(Scene.Corners[Corner].Position);
        const Triple Second = Widen(Scene.Corners[Corner + 1].Position);
        const Triple Third  = Widen(Scene.Corners[Corner + 2].Position);
        const std::optional<double> Hit = IntersectTriangle(Ray, First, Second, Third);
        const double Bias = Corner >= SolidLimit ? 1.0e-5 : 0.0;                             // Semitransparent cutter wins on coincident pocket walls
        if (!Hit || (Face && *Hit - Bias >= Face->Distance)) continue;
        const uint32_t FaceIndex = Scene.Corners[Corner].Face;
        if (Section)
        {
            const Triple Point = { Ray.Origin[0] + Ray.Direction[0] * *Hit, Ray.Origin[1] + Ray.Direction[1] * *Hit, Ray.Origin[2] + Ray.Direction[2] * *Hit };
            if (Side(Point) < 0.0) continue;                                                 // Removed half: discarded on the GPU too
            // 📝 Outward CCW triangles (as the rasterizer's front face): the back is seen when the normal runs along the ray.
            const bool BackCondition  = Dot(Cross(Difference(Second, First), Difference(Third, First)), Ray.Direction) > 0.0;
            const bool SheetCondition = FaceIndex < Section->SheetFaces.size() && Section->SheetFaces[FaceIndex] != 0;
            if (BackCondition && !SheetCondition) { Face = SketcherPick{ IdentitySectionCap, *Hit }; continue; }
        }
        Face = SketcherPick{ FaceIndex + 1u, *Hit };
    }
    std::optional<SketcherPick> Edge;
    double                      Closest = EdgeReach;                                         // [len] - Best separation so far
    for (size_t SegIndex = 0; SegIndex < Scene.Segments.size(); ++SegIndex)
    {
        const SketcherSceneSegment& Segment = Scene.Segments[SegIndex];
        const auto [Reach, Separation] = ApproachSegment(Ray, Widen(Segment.Start), Widen(Segment.End));
        if (Separation > Closest || Reach <= 0.0) continue;
        const bool CutterSeg = SegIndex >= SolidSegs;                                        // Submerged live-cutter edges stay pickable through solids
        if (Face && !XRayEnabled && !CutterSeg && Reach > Face->Distance + EdgeReach) continue;
        if (Section && Side({ Ray.Origin[0] + Ray.Direction[0] * Reach, Ray.Origin[1] + Ray.Direction[1] * Reach, Ray.Origin[2] + Ray.Direction[2] * Reach }) < -EdgeReach) continue;
        Closest = Separation;
        Edge    = SketcherPick{ IdentityEdgeBit | (Segment.Edge + 1u), Reach };
    }
    return Edge ? Edge : Face;
}

SketcherSectionClip ComposeSketcherSectionClip(const SketcherScene& Scene, const SketcherDocument& Document, std::array<double, 4> Plane)
{
    SketcherSectionClip Clip;
    Clip.Plane = Plane;
    Clip.SheetFaces.assign(Scene.Faces.size(), 0);
    for (const SketcherSceneBody& Body : Scene.Bodies)
    {
        bool SheetCondition = false;
        for (const SketcherBodyFigures& Figures : Document.Summary.Bodies) if (Figures.Key == Body.Key) SheetCondition = Figures.SheetCondition;
        if (!SheetCondition) continue;
        for (uint32_t Index = Body.FirstFace; Index < Body.FirstFace + Body.FaceCount && Index < Clip.SheetFaces.size(); ++Index) Clip.SheetFaces[Index] = 1;
    }
    return Clip;
}

SketcherSceneStates ComposeSketcherStates(const SketcherScene& Scene, const SketcherSelection& Selection, uint32_t HoverIdentity)
{
    SketcherSceneStates States;
    States.Faces.assign(Scene.Faces.size(), 0u);
    States.Edges.assign(Scene.Edges.size(), 0u);
    // A name the picked body carries lights there only (a cut's other piece carries the same names).
    uint32_t Picked = ~0u;
    for (uint32_t Index = 0; Index < Scene.Bodies.size(); ++Index) if (!Selection.PickedBody.empty() && Scene.Bodies[Index].Key == Selection.PickedBody) Picked = Index;
    const auto Mark = [&](const auto& Table, std::vector<uint32_t>& Words, const std::vector<std::string>& Names, bool ActiveCondition)
    {
        std::set<std::string> Owned;
        if (Picked != ~0u)
            for (const auto& Entry : Table) if (Entry.Body == Picked) Owned.insert(Entry.Name);
        for (size_t Index = 0; Index < Table.size(); ++Index)
        {
            const auto Found = std::find(Names.begin(), Names.end(), Table[Index].Name);
            if (Found == Names.end()) continue;
            if (Table[Index].Body != Picked && Owned.contains(Table[Index].Name)) continue;
            Words[Index] |= 1u;
            if (ActiveCondition && Found == Names.end() - 1) Words[Index] |= 2u;
        }
    };
    Mark(Scene.Faces, States.Faces, Selection.Faces, Selection.LastKind == SketcherSelectionCategory::Face);
    Mark(Scene.Edges, States.Edges, Selection.Edges, Selection.LastKind == SketcherSelectionCategory::Edge);
    for (const SketcherSceneBody& Body : Scene.Bodies)
    {
        const auto Found = std::find(Selection.Bodies.begin(), Selection.Bodies.end(), Body.Key);
        if (Found == Selection.Bodies.end()) continue;
        const uint32_t Word = 1u | (Selection.LastKind == SketcherSelectionCategory::Body && Found == Selection.Bodies.end() - 1 ? 2u : 0u);
        for (uint32_t Index = Body.FirstFace; Index < Body.FirstFace + Body.FaceCount; ++Index) States.Faces[Index] |= Word;
        for (uint32_t Index = Body.FirstEdge; Index < Body.FirstEdge + Body.EdgeCount; ++Index) States.Edges[Index] |= Word;
    }
    if (HoverIdentity != 0u)
    {
        const uint32_t Index = (HoverIdentity & ~IdentityEdgeBit) - 1u;
        std::vector<uint32_t>& Words = (HoverIdentity & IdentityEdgeBit) ? States.Edges : States.Faces;
        if (Index < Words.size()) Words[Index] |= 4u;
    }
    return States;
}

SketcherEdgeApproach ApproachSketcherEdge(const SketcherScene& Scene, const OrbitRay& Ray, uint32_t EdgeIndex)
{
    SketcherEdgeApproach Best;
    if (EdgeIndex >= Scene.Edges.size()) return Best;
    const SketcherSceneEdge& Edge = Scene.Edges[EdgeIndex];
    for (uint32_t Index = Edge.FirstSegment; Index < Edge.FirstSegment + Edge.SegmentCount; ++Index)
    {
        const auto [Reach, Separation] = ApproachSegment(Ray, Widen(Scene.Segments[Index].Start), Widen(Scene.Segments[Index].End));
        if (Separation < Best.Separation) Best = { Reach, Separation };
    }
    return Best;
}

std::string NameSketcherIdentity(const SketcherScene& Scene, uint32_t Identity)
{
    if (Identity == 0) return {};
    const uint32_t Index = (Identity & ~IdentityEdgeBit) - 1u;
    if (Identity & IdentityEdgeBit) return Index < Scene.Edges.size() ? Scene.Edges[Index].Name : std::string{};
    return Index < Scene.Faces.size() ? Scene.Faces[Index].Name : std::string{};
}

SketcherSelectionCategory SketcherIdentityKind(uint32_t Identity) noexcept
{
    return (Identity & IdentityEdgeBit) ? SketcherSelectionCategory::Edge : SketcherSelectionCategory::Face;
}

std::vector<uint32_t> ComposeSketcherMatcapLayers(const SketcherScene& Scene, const SketcherAppearance& Appearance, const SketcherMatcapLibrary& Library)
{
    std::vector<uint32_t> Layers(Scene.Faces.size(), 0u);
    const uint32_t CutterDefault = SketcherMatcapLayer(Library, "Copper");
    for (size_t Index = 0; Index < Scene.Faces.size(); ++Index)
    {
        const SketcherSceneFace& Face = Scene.Faces[Index];
        const std::string&       Key  = Face.Body < Scene.Bodies.size() ? Scene.Bodies[Face.Body].Key : std::string{};
        const bool CustomAssigned     = Appearance.FaceMatcaps.contains(Face.Name) || Appearance.BodyMatcaps.contains(Key);
        if (!CustomAssigned && Appearance.LiveCutters.contains(Key)) Layers[Index] = CutterDefault << SketcherMatcapLayerShift;
        else Layers[Index] = SketcherMatcapLayer(Library, SketcherFaceMatcap(Appearance, Key, Face.Name)) << SketcherMatcapLayerShift;
    }
    return Layers;
}

size_t CountDanglingMatcaps(const SketcherScene& Scene, const SketcherAppearance& Appearance)
{
    std::set<std::string> Present;
    for (const SketcherSceneFace& Face : Scene.Faces) Present.insert(Face.Name);
    size_t Dangling = 0;
    for (const auto& Entry : Appearance.FaceMatcaps) Dangling += Present.contains(Entry.first) ? 0 : 1;
    std::set<std::string> Keys;
    for (const SketcherSceneBody& Body : Scene.Bodies) Keys.insert(Body.Key);
    for (const auto& Entry : Appearance.BodyMatcaps) Dangling += Keys.contains(Entry.first) ? 0 : 1;
    return Dangling;
}

double MeasureSketcherProfileSink(const SketcherDocument& Document, const SketcherScene& Scene, const std::string& SketchLabel)
{
    const auto Found = std::find_if(Document.Tree.Steps.begin(), Document.Tree.Steps.end(),
                                    [&](const FeatureStep& Step) { return Step.Label == SketchLabel && Step.Kind == FeatureStepCategory::Sketch; });
    if (Found == Document.Tree.Steps.end()) return 0.0;
    OrthonormalBasis Plane = Found->Plane;
    Plane.Origin           = Plane.Origin + Plane.ZAxis * EvaluateFeatureValue(Document.Tree, Found->Elevation);
    const auto        Solved = Document.Regeneration.Sketches.find(SketchLabel);
    const SketchModel& Sketch = Solved != Document.Regeneration.Sketches.end() ? Solved->second : Found->Sketch;
    std::vector<Vector3> Samples;
    for (const SketchCurve& Curve : Sketch.Curves)
    {
        if (Curve.Construction) continue;
        for (const Vector2& Point : SampleSketchCurve(Sketch, Curve)) Samples.push_back(Plane.Origin + Plane.XAxis * Point.X + Plane.YAxis * Point.Y);
    }
    if (Samples.empty()) return 0.0;
    Vector3 Low = Samples.front(), High = Samples.front();
    for (const Vector3& Point : Samples)
    {
        Low  = { std::min(Low.X, Point.X), std::min(Low.Y, Point.Y), std::min(Low.Z, Point.Z) };
        High = { std::max(High.X, Point.X), std::max(High.Y, Point.Y), std::max(High.Z, Point.Z) };
    }
    const double Reach = Length(High - Low);                                                 // [len] - Profile size: the deepest landing allowed
    const double Lead  = 1.0e-4;                                                             // [len] - Ray start above the plane
    double       Depth = 0.0;
    for (const Vector3& Point : Samples)
    {
        OrbitRay Ray;
        const Vector3 Start = Point + Plane.ZAxis * Lead;
        Ray.Origin    = { Start.X, Start.Y, Start.Z };
        Ray.Direction = { -Plane.ZAxis.X, -Plane.ZAxis.Y, -Plane.ZAxis.Z };
        const std::optional<SketcherPick> Pick = PickSketcherScene(Scene, Ray, 0.0);
        if (!Pick || Pick->Identity == 0 || Pick->Identity == IdentitySectionCap || (Pick->Identity & IdentityEdgeBit)) return 0.0;
        const uint32_t Face = Pick->Identity - 1u;
        if (Face >= Scene.Faces.size() || Scene.Faces[Face].Surface == "Plane") return 0.0;
        const double Landing = Pick->Distance - Lead;
        if (Landing > Reach) return 0.0;
        Depth = std::max(Depth, Landing);
    }
    return Depth > 1.0e-7 ? Depth + std::max(Scene.Sag, 1.0e-4) : 0.0;
}

int MeasureSketcherProfileMaterial(const SketcherDocument& Document, const std::string& SketchLabel, const std::string& Target)
{
    const auto Found = std::find_if(Document.Tree.Steps.begin(), Document.Tree.Steps.end(),
                                    [&](const FeatureStep& Step) { return Step.Label == SketchLabel && Step.Kind == FeatureStepCategory::Sketch; });
    if (Found == Document.Tree.Steps.end() || !Document.Partition) return 0;
    const std::string Key = Target.empty() ? Document.Regeneration.Active : Target;
    const auto Body = std::find_if(Document.Regeneration.Bodies.begin(), Document.Regeneration.Bodies.end(), [&](const RegeneratedBody& Entry) { return Entry.Key == Key; });
    if (Body == Document.Regeneration.Bodies.end() || Document.Partition->Bodies.Resolve(Body->Body).Category != BodyCategory::Solid) return 0;
    OrthonormalBasis Plane = Found->Plane;
    Plane.Origin           = Plane.Origin + Plane.ZAxis * EvaluateFeatureValue(Document.Tree, Found->Elevation);
    const auto         Solved  = Document.Regeneration.Sketches.find(SketchLabel);
    const SketchModel& Sketch  = Solved != Document.Regeneration.Sketches.end() ? Solved->second : Found->Sketch;
    const SketchProfile Profile = ExtractSketchProfile(Sketch);
    if (!Profile.Refusal.empty()) return 0;
    const BodyContainment Containment = PrepareBodyContainment(*Document.Partition, Body->Body);
    const double Inward = 1.0e-3 * std::max(Profile.Scale, 1.0e-6);                          // [len] - Probe step into the region
    const double Lift   = 5.0e-5;                                                            // [len] - Probe height off the plane (nearest-normal reach)
    int Above = 0, Below = 0, Count = 0;
    for (const ProfileLoop& Loop : Profile.Loops)
    {
        if (!(Loop.SignedArea > 0.0)) continue;                                              // Outer loops run counter-clockwise: the region is on the left
        const size_t Probes = std::min<size_t>(Loop.Segments.size(), 8);
        for (size_t Index = 0; Index < Probes; ++Index)
        {
            const ProfileSegment& Segment = Loop.Segments[Index * Loop.Segments.size() / Probes];
            const Vector2 Tangent = Normalize(EvaluateProfileSegment(Segment, 0.5 + 1.0e-4) - EvaluateProfileSegment(Segment, 0.5 - 1.0e-4));
            const Vector2 Point   = EvaluateProfileSegment(Segment, 0.5) + Vector2{ -Tangent.Y, Tangent.X } * Inward;
            const Vector3 Spot    = Plane.Origin + Plane.XAxis * Point.X + Plane.YAxis * Point.Y;
            ++Count;
            Above += ClassifySolidPoint(Containment, Spot + Plane.ZAxis * Lift).Membership == PointMembership::Inside ? 1 : 0;
            Below += ClassifySolidPoint(Containment, Spot - Plane.ZAxis * Lift).Membership == PointMembership::Inside ? 1 : 0;
        }
    }
    // A clear side only: most probes find material there and none on the other (a profile half under a body decides nothing).
    if (Below * 2 > Count && Above == 0) return -1;
    if (Above * 2 > Count && Below == 0) return 1;
    return 0;
}

} // namespace Frontier::Workspaces::Sketcher
