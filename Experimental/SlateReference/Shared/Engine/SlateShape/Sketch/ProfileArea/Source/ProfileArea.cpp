//============================================================================================================================================
//                                                       PROFILEAREA.CPP
//============================================================================================================================================

#include "SlateShape/Sketch/ProfileArea/Api/ProfileArea.h"
#include "SlateShape/Sketch/SketchPolyline/Api/SketchPolyline.h"

#if __has_include("clipper2/clipper.h")
    #include "clipper2/clipper.h"
    #define SLATE_PROFILE_AREA_HAS_CLIPPER2 1
#else
    #define SLATE_PROFILE_AREA_HAS_CLIPPER2 0
#endif

#if __has_include("mapbox/earcut.hpp")
    #include "mapbox/earcut.hpp"
    #include <array>
    #define SLATE_PROFILE_AREA_HAS_EARCUT 1
#else
    #define SLATE_PROFILE_AREA_HAS_EARCUT 0
#endif

#include <algorithm>
#include <cmath>
#include <set>

namespace Slate
{

namespace
{
    struct SegmentSample
    {
        SketchCurveName Curve = {};
        std::vector<SpatialPoint> Points = {};
    };

    double DistanceSquared(const SpatialPoint& A, const SpatialPoint& B)
    {
        const double X = A.Left - B.Left;
        const double Y = A.Up - B.Up;
        const double Z = A.Forward - B.Forward;
        return X * X + Y * Y + Z * Z;
    }

    double Cross2(const SpatialPoint& A, const SpatialPoint& B, const SpatialPoint& C)
    {
        const double ABx = B.Left - A.Left;
        const double ABy = B.Forward - A.Forward;
        const double ACx = C.Left - A.Left;
        const double ACy = C.Forward - A.Forward;
        return ABx * ACy - ABy * ACx;
    }

    double SignedAreaOf(const std::vector<SpatialPoint>& Points)
    {
        if (Points.size() < 3u)
            return 0.0;
        double Area = 0.0;
        for (std::size_t Index = 0u; Index + 1u < Points.size(); ++Index)
            Area += Points[Index].Left * Points[Index + 1u].Forward - Points[Index + 1u].Left * Points[Index].Forward;
        return Area * 0.5;
    }

    bool PointsNear(const SpatialPoint& A, const SpatialPoint& B, double Tolerance)
    {
        return DistanceSquared(A, B) <= Tolerance * Tolerance;
    }

    bool SegmentIntersection(const SpatialPoint& A,
                             const SpatialPoint& B,
                             const SpatialPoint& C,
                             const SpatialPoint& D,
                             SpatialPoint& Intersection)
    {
        const double Rx = B.Left - A.Left;
        const double Ry = B.Forward - A.Forward;
        const double Sx = D.Left - C.Left;
        const double Sy = D.Forward - C.Forward;
        const double Denominator = Rx * Sy - Ry * Sx;
        if (std::abs(Denominator) <= 1.0e-12)
            return false;
        const double QPx = C.Left - A.Left;
        const double QPy = C.Forward - A.Forward;
        const double T = (QPx * Sy - QPy * Sx) / Denominator;
        const double U = (QPx * Ry - QPy * Rx) / Denominator;
        if (T <= 1.0e-7 || T >= 1.0 - 1.0e-7 || U <= 1.0e-7 || U >= 1.0 - 1.0e-7)
            return false;
        Intersection = { A.Left + Rx * T, A.Up + (B.Up - A.Up) * T, A.Forward + Ry * T };
        return true;
    }

    bool PointInsideLoop(const SpatialPoint& Point, const std::vector<SpatialPoint>& Loop)
    {
        bool Inside = false;
        for (std::size_t I = 0u, J = Loop.empty() ? 0u : Loop.size() - 1u; I < Loop.size(); J = I++)
        {
            const SpatialPoint& A = Loop[I];
            const SpatialPoint& B = Loop[J];
            const bool Crosses = ((A.Forward > Point.Forward) != (B.Forward > Point.Forward)) &&
                (Point.Left < (B.Left - A.Left) * (Point.Forward - A.Forward) / ((B.Forward - A.Forward) + 1.0e-300) + A.Left);
            if (Crosses)
                Inside = !Inside;
        }
        return Inside;
    }

    SpatialPoint CentroidOf(const std::vector<SpatialPoint>& Points)
    {
        SpatialPoint Result = {};
        if (Points.empty())
            return Result;
        for (const SpatialPoint& Point : Points)
        {
            Result.Left += Point.Left;
            Result.Up += Point.Up;
            Result.Forward += Point.Forward;
        }
        const double Count = static_cast<double>(Points.size());
        return { Result.Left / Count, Result.Up / Count, Result.Forward / Count };
    }

    void AppendTriangleFan(const ProfileAreaLoop& Loop,
                           std::vector<ProfileAreaTriangle>& Triangles)
    {
        if (Loop.Points.size() < 4u || Loop.Role == ProfileAreaLoopRole::Hole || Loop.SelfIntersecting)
            return;
        for (std::size_t Index = 1u; Index + 1u < Loop.Points.size() - 1u; ++Index)
            Triangles.push_back({ Loop.Points[0], Loop.Points[Index], Loop.Points[Index + 1u], Loop.Role });
    }

    void AppendEarcutTriangles(const ProfileAreaLoop& Loop,
                               std::vector<ProfileAreaTriangle>& Triangles)
    {
#if SLATE_PROFILE_AREA_HAS_EARCUT
        if (Loop.Points.size() < 4u || Loop.Role == ProfileAreaLoopRole::Hole || Loop.SelfIntersecting)
            return;
        using EarcutPoint = std::array<double, 2>;
        std::vector<std::vector<EarcutPoint>> Polygon(1u);
        for (std::size_t Index = 0u; Index + 1u < Loop.Points.size(); ++Index)
            Polygon[0].push_back({ Loop.Points[Index].Left, Loop.Points[Index].Forward });
        const std::vector<std::uint32_t> Indices = mapbox::earcut<std::uint32_t>(Polygon);
        for (std::size_t Index = 0u; Index + 2u < Indices.size(); Index += 3u)
            Triangles.push_back({ Loop.Points[Indices[Index]], Loop.Points[Indices[Index + 1u]], Loop.Points[Indices[Index + 2u]], Loop.Role });
#else
        AppendTriangleFan(Loop, Triangles);
#endif
    }

    std::set<std::uint32_t> CurveSetOf(const ProfileLoop& Loop)
    {
        std::set<std::uint32_t> Result;
        for (const ProfileCurveUse& Use : Loop.Traversal)
            Result.insert(Use.TraversedCurve.IssuedIndex);
        return Result;
    }

    bool ProfileAlreadyDeclared(const SketchStructure& Declared, const ProfileAreaLoop& Loop)
    {
        std::set<std::uint32_t> Candidate;
        for (SketchCurveName Curve : Loop.Curves)
            Candidate.insert(Curve.IssuedIndex);
        for (const ProfileSpecification& Profile : Declared.Profiles())
            for (const ProfileLoop& Existing : Profile.HeldLoops())
                if (CurveSetOf(Existing) == Candidate)
                    return true;
        return false;
    }

    ProfilePlane SketchPlaneAsProfilePlane(const SketchStructure& Declared)
    {
        return { Declared.HeldPlane().Origin, Declared.HeldPlane().Normal, Declared.HeldPlane().AlongDirection };
    }
}

ProfileAreaAnalysis AnalyzeProfileAreas(const SketchStructure& Declared,
                                             double ClosureTolerance)
{
    ProfileAreaAnalysis Analysis;
    Analysis.Clipper2BackendAvailable = SLATE_PROFILE_AREA_HAS_CLIPPER2 != 0;
    Analysis.EarcutBackendAvailable = SLATE_PROFILE_AREA_HAS_EARCUT != 0;
    if (!Declared.Declared())
        return Analysis;

    std::vector<SegmentSample> Samples;
    for (std::uint32_t Index = 0u; Index < Declared.Curves().size(); ++Index)
    {
        const CurveSpecification& Curve = Declared.Curves()[Index].Geometry;
        if (!Curve.Declared())
            continue;
        SegmentSample Sample;
        Sample.Curve = { Index + 1u };
        AppendCurvePolyline(Curve, Sample.Points, 64u);
        if (Sample.Points.size() >= 2u)
            Samples.push_back(Sample);
    }

#if SLATE_PROFILE_AREA_HAS_CLIPPER2
    // Clipper2 is intentionally touched here as the robust polygon-operation backend seam. Area previews still
    // keep exact sketch curves authoritative and consume only discretised paths.
    [[maybe_unused]] Clipper2Lib::PathsD PreparedPaths;
#endif

    std::vector<bool> Used(Samples.size(), false);
    for (std::size_t Start = 0u; Start < Samples.size(); ++Start)
    {
        if (Used[Start])
            continue;

        ProfileAreaLoop Loop;
        Loop.Curves.push_back(Samples[Start].Curve);
        Loop.Points = Samples[Start].Points;
        Used[Start] = true;

        bool Extended = true;
        while (Extended && !Loop.Points.empty())
        {
            Extended = false;
            const SpatialPoint Tail = Loop.Points.back();
            for (std::size_t Candidate = 0u; Candidate < Samples.size(); ++Candidate)
            {
                if (Used[Candidate])
                    continue;
                const std::vector<SpatialPoint>& Points = Samples[Candidate].Points;
                if (PointsNear(Tail, Points.front(), ClosureTolerance))
                {
                    Loop.Curves.push_back(Samples[Candidate].Curve);
                    Loop.Points.insert(Loop.Points.end(), Points.begin() + 1, Points.end());
                    Used[Candidate] = true;
                    Extended = true;
                    break;
                }
                if (PointsNear(Tail, Points.back(), ClosureTolerance))
                {
                    Loop.Curves.push_back(Samples[Candidate].Curve);
                    Loop.Points.insert(Loop.Points.end(), Points.rbegin() + 1, Points.rend());
                    Used[Candidate] = true;
                    Extended = true;
                    break;
                }
            }
        }

        if (Loop.Points.size() < 2u || !PointsNear(Loop.Points.front(), Loop.Points.back(), ClosureTolerance))
        {
            if (Loop.Points.size() >= 2u)
            {
                Analysis.Issues.push_back({ ProfileAreaIssueSubject::OpenLoop, Loop.Points.front(), Loop.Points.back(),
                                            Loop.Curves.front(), Loop.Curves.back(),
                                            std::sqrt(DistanceSquared(Loop.Points.front(), Loop.Points.back())) });
                Analysis.Issues.push_back({ ProfileAreaIssueSubject::Gap, Loop.Points.front(), Loop.Points.back(),
                                            Loop.Curves.front(), Loop.Curves.back(),
                                            std::sqrt(DistanceSquared(Loop.Points.front(), Loop.Points.back())) });
            }
            continue;
        }

        Loop.Points.back() = Loop.Points.front();
        Loop.SignedArea = SignedAreaOf(Loop.Points);
        for (std::size_t A = 0u; A + 1u < Loop.Points.size(); ++A)
        {
            for (std::size_t B = A + 1u; B + 1u < Loop.Points.size(); ++B)
            {
                if (B == A + 1u || (A == 0u && B + 2u == Loop.Points.size()))
                    continue;
                SpatialPoint Intersection = {};
                if (SegmentIntersection(Loop.Points[A], Loop.Points[A + 1u], Loop.Points[B], Loop.Points[B + 1u], Intersection))
                {
                    Loop.SelfIntersecting = true;
                    Analysis.Issues.push_back({ ProfileAreaIssueSubject::SelfIntersection, Intersection, Intersection,
                                                Loop.Curves.empty() ? SketchCurveName{} : Loop.Curves.front(),
                                                Loop.Curves.empty() ? SketchCurveName{} : Loop.Curves.back(), 0.0 });
                }
            }
        }
        Analysis.Loops.push_back(Loop);
    }

    std::vector<std::size_t> Order(Analysis.Loops.size());
    for (std::size_t Index = 0u; Index < Order.size(); ++Index)
        Order[Index] = Index;
    std::sort(Order.begin(), Order.end(), [&](std::size_t A, std::size_t B)
    {
        return std::abs(Analysis.Loops[A].SignedArea) > std::abs(Analysis.Loops[B].SignedArea);
    });

    for (std::size_t SortedIndex = 0u; SortedIndex < Order.size(); ++SortedIndex)
    {
        ProfileAreaLoop& Loop = Analysis.Loops[Order[SortedIndex]];
        const SpatialPoint Centre = CentroidOf(Loop.Points);
        std::uint32_t ContainerCount = 0u;
        for (std::size_t Prior = 0u; Prior < SortedIndex; ++Prior)
            if (PointInsideLoop(Centre, Analysis.Loops[Order[Prior]].Points))
                ++ContainerCount;
        Loop.Role = (ContainerCount % 2u) == 0u ? ProfileAreaLoopRole::Outer : ProfileAreaLoopRole::Hole;
    }

    for (const ProfileAreaLoop& Loop : Analysis.Loops)
        AppendEarcutTriangles(Loop, Analysis.Triangles);

    return Analysis;
}

Deliver<std::vector<ProfileNameInFeature>> AutoDeclareClosedAreaProfiles(SketchStructure& Declared,
                                                                            double ClosureTolerance)
{
    if (!Declared.Declared())
        return Deliver<std::vector<ProfileNameInFeature>>::Refuse({ RefusalReason::ContentUnsupported, "the sketch is not declared" });

    const ProfileAreaAnalysis Analysis = AnalyzeProfileAreas(Declared, ClosureTolerance);
    std::vector<ProfileNameInFeature> Produced;
    for (const ProfileAreaLoop& Loop : Analysis.Loops)
    {
        if (Loop.Role != ProfileAreaLoopRole::Outer || Loop.SelfIntersecting || ProfileAlreadyDeclared(Declared, Loop))
            continue;
        ProfileSpecification Profile;
        Profile.DeclarePlane(SketchPlaneAsProfilePlane(Declared));
        ProfileLoop DeclaredLoop;
        DeclaredLoop.Orientation = ProfileLoopOrientation::Outer;
        for (SketchCurveName Curve : Loop.Curves)
            DeclaredLoop.Traversal.push_back({ { Curve.IssuedIndex }, true });
        Profile.DeclareLoop(DeclaredLoop);

        for (const ProfileAreaLoop& Hole : Analysis.Loops)
        {
            if (Hole.Role != ProfileAreaLoopRole::Hole || Hole.SelfIntersecting || Hole.Points.empty())
                continue;
            if (!PointInsideLoop(CentroidOf(Hole.Points), Loop.Points))
                continue;
            ProfileLoop HoleLoop;
            HoleLoop.Orientation = ProfileLoopOrientation::Inner;
            for (SketchCurveName Curve : Hole.Curves)
                HoleLoop.Traversal.push_back({ { Curve.IssuedIndex }, true });
            Profile.DeclareLoop(HoleLoop);
        }

        Produced.push_back(Declared.DeclareProfile(Profile));
    }
    return Deliver<std::vector<ProfileNameInFeature>>::Result(Produced);
}

} // namespace Slate
