//============================================================================================================================================
//                                                     PLACEMENTCOMMITPROOF.CPP
//============================================================================================================================================
// 🧩 Proves that a sealed placement becomes the shape the artist asked for — and, above all, that the
//    METHOD is honoured rather than quietly ignored.

#include "SlateWorkspace/Discipline/PlacementCommit/Api/PlacementCommit.h"

#include "SlateShape/Sketch/SketchPolyline/Api/SketchPolyline.h"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace Slate;

namespace
{

std::uint32_t Claims = 0u;
std::uint32_t Failures = 0u;

void Claim(bool Held, const char* Sentence)
{
    ++Claims;
    if (!Held)
    {
        ++Failures;
        std::printf("  FAILED  %s\n", Sentence);
    }
}

void ClaimNamed(bool Held, const std::string& Sentence)
{
    Claim(Held, Sentence.c_str());
}

const char* SubjectText(SketchSubject Subject)
{
    switch (Subject)
    {
        case SketchSubject::Point:          return "Point";
        case SketchSubject::Line:           return "Line";
        case SketchSubject::Rectangle:      return "Rectangle";
        case SketchSubject::Circle:         return "Circle";
        case SketchSubject::Ellipse:        return "Ellipse";
        case SketchSubject::Arc:            return "Arc";
        case SketchSubject::EllipticalArc:  return "EllipticalArc";
        case SketchSubject::Slot:           return "Slot";
        case SketchSubject::Polygon:        return "Polygon";
        case SketchSubject::Polyline:       return "Polyline";
        case SketchSubject::Bezier:         return "Bezier";
        case SketchSubject::BasisSpline:    return "BasisSpline";
        case SketchSubject::RationalSpline: return "RationalSpline";
        case SketchSubject::Hermite:        return "Hermite";
        case SketchSubject::Dimension:      return "Dimension";
        case SketchSubject::None:           return "None";
        case SketchSubject::SubjectCount:   break;
    }
    return "?";
}

const char* MethodText(PlacementMethod Method)
{
    switch (Method)
    {
        case PlacementMethod::Extent:     return "Extent";
        case PlacementMethod::Centred:    return "Centred";
        case PlacementMethod::ThreePoint: return "ThreePoint";
        case PlacementMethod::Diameter:   return "Diameter";
        case PlacementMethod::Tangent:    return "Tangent";
        case PlacementMethod::MethodCount: break;
    }
    return "?";
}

/// 🧩 A sketch on the ground plane with a fresh directory, ready to be drawn into.
struct Bench
{
    WorkspaceNameIndex        Naming;
    SketchStructure           Sketch;
    WorkspaceRecordStructure  Records;
    WorkspaceRevisionSequence Revisions;

    Bench()
    {
        SketchPlane Ground;
        Ground.Origin         = { 0.0, 0.0, 0.0 };
        Ground.Normal         = { 0.0, 1.0, 0.0 };
        Ground.AlongDirection = { 1.0, 0.0, 0.0 };
        Sketch.DeclarePlane(Ground);
    }
};

/// 🧩 Three anchors that suit every shape: not collinear, well apart, none degenerate.
SealedPlacement Place(SketchSubject Subject, PlacementMethod Method, std::uint32_t Anchors)
{
    const SpatialPoint Points[4] =
    {
        {  0.0, 0.0,  0.0 },
        { 40.0, 0.0,  0.0 },
        { 40.0, 0.0, 30.0 },
        {  0.0, 0.0, 30.0 },
    };

    SealedPlacement Placed = {};
    Placed.Subject = Subject;
    Placed.Method  = Method;
    for (std::uint32_t Index = 0u; Index < Anchors && Index < 4u; ++Index)
        Placed.Anchors.push_back(Points[Index]);
    return Placed;
}

//========================================================================================================
// 1. EVERY ACCEPTED PAIR IS ANSWERED
//========================================================================================================

// 🔴 THE DEFECT THIS UNIT EXISTS TO REMOVE. The host chose an arm by walking a chain in source order and
//    most arms tested only the subject, so a method-specific pair was caught by whichever bare arm came
//    first. Nine of twenty-seven pairs reached an arm not written for them, and two arms were unreachable
//    dead code. This walks every pair the catalogue accepts and insists it is answered.

void ProveEveryPairAnswered()
{
    std::printf("\n1. Every pair the catalogue accepts is answered\n");

    std::uint32_t Accepted   = 0u;
    std::uint32_t Supported  = 0u;
    std::uint32_t Unanswered = 0u;

    for (std::uint32_t S = 1u; S < static_cast<std::uint32_t>(SketchSubject::SubjectCount); ++S)
    for (std::uint32_t M = 0u; M < static_cast<std::uint32_t>(PlacementMethod::MethodCount); ++M)
    {
        const SketchSubject   Subject = static_cast<SketchSubject>(S);
        const PlacementMethod Method  = static_cast<PlacementMethod>(M);
        if (!AcceptedBy(Subject, Method))
            continue;

        ++Accepted;
        if (CommitSupported(Subject, Method))
            ++Supported;
        else
        {
            ++Unanswered;
            std::printf("      unanswered: %s / %s\n", SubjectText(Subject), MethodText(Method));
        }
    }

    Claim(Accepted > 20u, "the catalogue really does accept a spread of pairs");
    ClaimNamed(Unanswered == 0u,
               "every one of the " + std::to_string(Accepted) + " accepted pairs has an arm written for it");
    Claim(Supported == Accepted, "and none is left to fall through to a neighbour");
}

//========================================================================================================
// 2. EVERY ACCEPTED PAIR ACTUALLY DECLARES SOMETHING
//========================================================================================================

void ProveEveryPairDeclares()
{
    std::printf("\n2. Every accepted pair declares geometry and seals one revision\n");

    for (std::uint32_t S = 1u; S < static_cast<std::uint32_t>(SketchSubject::SubjectCount); ++S)
    for (std::uint32_t M = 0u; M < static_cast<std::uint32_t>(PlacementMethod::MethodCount); ++M)
    {
        const SketchSubject   Subject = static_cast<SketchSubject>(S);
        const PlacementMethod Method  = static_cast<PlacementMethod>(M);
        if (!AcceptedBy(Subject, Method))
            continue;

        // A dimension measures existing geometry rather than declaring its own, so it is covered
        // separately in section 5 where there is something for it to measure.
        if (Subject == SketchSubject::Dimension)
            continue;

        Bench Stage;
        const PlacementDeclaration Declared = DeclaredPlacement(Subject, Method);
        const Deliver<WorkspaceRecordName> Made =
            CommitPlacement(Stage.Naming, Stage.Sketch, Stage.Records, Stage.Revisions,
                            Place(Subject, Method, Declared.Required));

        const std::string Name = std::string(SubjectText(Subject)) + " / " + MethodText(Method);
        ClaimNamed(Made.Resolved, Name + " declares rather than refusing");
        if (!Made.Resolved)
            continue;

        ClaimNamed(Made.Delivered.Assigned(), Name + " hands back a named record");
        ClaimNamed(Stage.Records.Resolve(Made.Delivered) != nullptr, Name + " whose record resolves");

        // ⚠️ ONE revision, whatever the shape. A rectangle declares four curves and a profile, and one
        //    press of undo must remove the rectangle - not one side of it.
        ClaimNamed(Stage.Revisions.DeclaredCount() == 1u,
                   Name + " seals exactly ONE revision however many curves it took");
    }
}

//========================================================================================================
// 3. THE METHOD CHANGES THE SHAPE
//========================================================================================================

// ⚠️ AppendCurvePolyline CLEARS the vector it is handed, in spite of its name. Sampling a whole sketch
//    therefore cannot loop it over one accumulator - every curve but the last would be discarded, and a
//    four-arc circle profile would measure as a single quadrant. Sample each curve on its own and gather.
std::vector<SpatialPoint> SampleWholeSketch(const SketchStructure& Sketch, std::uint32_t StepCount)
{
    std::vector<SpatialPoint> Gathered;
    for (const DeclaredSketchCurve& Curve : Sketch.Curves())
    {
        std::vector<SpatialPoint> Points;
        AppendCurvePolyline(Curve.Geometry, Points, StepCount);
        Gathered.insert(Gathered.end(), Points.begin(), Points.end());
    }
    return Gathered;
}

// 🔴 THE HEART OF IT. Two placements with identical anchors and different methods must produce different
//    geometry, or the method is being ignored - which is precisely what the host did.

void ProveMethodIsHonoured()
{
    std::printf("\n3. The same anchors, a different method, a different shape\n");

    // A circle from anchors at (0,0) and (40,0).
    //   Centred  -> centre (0,0), radius 40
    //   Diameter -> centre (20,0), radius 20
    {
        Bench Centred;
        const Deliver<WorkspaceRecordName> A =
            CommitPlacement(Centred.Naming, Centred.Sketch, Centred.Records, Centred.Revisions,
                            Place(SketchSubject::Circle, PlacementMethod::Centred, 2u));
        Bench Diameter;
        const Deliver<WorkspaceRecordName> B =
            CommitPlacement(Diameter.Naming, Diameter.Sketch, Diameter.Records, Diameter.Revisions,
                            Place(SketchSubject::Circle, PlacementMethod::Diameter, 2u));

        Claim(A.Resolved && B.Resolved, "a centred circle and a diameter circle both declare");

        const std::vector<SpatialPoint> CentredPoints  = SampleWholeSketch(Centred.Sketch, 64u);
        const std::vector<SpatialPoint> DiameterPoints = SampleWholeSketch(Diameter.Sketch, 64u);

        Claim(!CentredPoints.empty() && !DiameterPoints.empty(), "both produce sampleable geometry");

        // 📝 The along span of a circle is its DIAMETER, so the radius is half of it. Measuring the span
        //    and calling it the radius was my mistake, and it made a correct circle look twice too big.
        const auto CentreAlong = [](const std::vector<SpatialPoint>& Points)
        {
            double MinAlong = 1.0e30, MaxAlong = -1.0e30;
            for (const SpatialPoint& Point : Points)
            {
                MinAlong = Point.Left < MinAlong ? Point.Left : MinAlong;
                MaxAlong = Point.Left > MaxAlong ? Point.Left : MaxAlong;
            }
            return (MinAlong + MaxAlong) * 0.5;
        };

        const auto Radius = [](const std::vector<SpatialPoint>& Points)
        {
            double MinAlong = 1.0e30, MaxAlong = -1.0e30;
            for (const SpatialPoint& Point : Points)
            {
                MinAlong = Point.Left < MinAlong ? Point.Left : MinAlong;
                MaxAlong = Point.Left > MaxAlong ? Point.Left : MaxAlong;
            }
            return (MaxAlong - MinAlong) * 0.5;
        };

        const double CentredRadius  = Radius(CentredPoints);
        const double DiameterRadius = Radius(DiameterPoints);

        // Anchors at (0,0) and (40,0), forty apart.
        //   Centred  -> centre on the first anchor, radius 40, so it spans -40..40
        //   Diameter -> the two anchors ARE the diameter, radius 20, spanning 0..40
        Claim(std::fabs(CentredRadius - 40.0) < 1.0,
              "the CENTRED circle takes the first anchor as its centre - radius 40");
        Claim(std::fabs(DiameterRadius - 20.0) < 1.0,
              "the DIAMETER circle spans the two anchors - radius 20");
        Claim(std::fabs(CentredRadius - DiameterRadius) > 10.0,
              "so the method genuinely changes the shape, from the same two anchors");

        // ⚠️ And WHERE, not only how big.
        Claim(std::fabs(CentreAlong(CentredPoints) - 0.0) < 1.0,
              "the CENTRED circle sits on the first anchor");
        Claim(std::fabs(CentreAlong(DiameterPoints) - 20.0) < 1.0,
              "the DIAMETER circle sits midway between the two");
    }

    // 🔴 THE ARC ARM THE HOST COULD NEVER REACH. A centred arc takes centre, start, end. A three-point
    //    arc takes three points ON the arc. From the same anchors those are different curves, and the
    //    host built the second for both because its bare Arc arm came first in the chain.
    {
        Bench ThreePoint;
        const Deliver<WorkspaceRecordName> A =
            CommitPlacement(ThreePoint.Naming, ThreePoint.Sketch, ThreePoint.Records, ThreePoint.Revisions,
                            Place(SketchSubject::Arc, PlacementMethod::ThreePoint, 3u));
        Bench Centred;
        const Deliver<WorkspaceRecordName> B =
            CommitPlacement(Centred.Naming, Centred.Sketch, Centred.Records, Centred.Revisions,
                            Place(SketchSubject::Arc, PlacementMethod::Centred, 3u));

        Claim(A.Resolved, "a three-point arc declares");
        Claim(B.Resolved, "a CENTRED arc declares - the arm the host left unreachable");

        const std::vector<SpatialPoint> ThreePointPoints = SampleWholeSketch(ThreePoint.Sketch, 64u);
        const std::vector<SpatialPoint> CentredPoints    = SampleWholeSketch(Centred.Sketch, 64u);

        Claim(!ThreePointPoints.empty() && !CentredPoints.empty(), "both produce sampleable geometry");

        // A three-point arc PASSES THROUGH its middle anchor; a centred arc has it as a radius endpoint.
        const auto NearestTo = [](const std::vector<SpatialPoint>& Points, const SpatialPoint& Target)
        {
            double Best = 1.0e30;
            for (const SpatialPoint& Point : Points)
            {
                const double DX = Point.Left - Target.Left;
                const double DZ = Point.Forward - Target.Forward;
                const double Distance = std::sqrt(DX * DX + DZ * DZ);
                Best = Distance < Best ? Distance : Best;
            }
            return Best;
        };

        const SpatialPoint Middle = { 40.0, 0.0, 0.0 };
        Claim(NearestTo(ThreePointPoints, Middle) < 1.0,
              "the THREE-POINT arc passes through its middle anchor");
        Claim(NearestTo(CentredPoints, { 0.0, 0.0, 0.0 }) > 1.0,
              "the CENTRED arc does not pass through its centre - it turns about it");
    }

    // A rectangle: extent corner-to-corner, centred about the first anchor.
    {
        const auto Extents = [](const SketchStructure& Sketch)
        {
            double MinAlong = 1.0e30, MaxAlong = -1.0e30;
            for (const DeclaredSketchCurve& Curve : Sketch.Curves())
            {
                std::vector<SpatialPoint> Points;
                AppendCurvePolyline(Curve.Geometry, Points, 8u);
                for (const SpatialPoint& Point : Points)
                {
                    MinAlong = Point.Left < MinAlong ? Point.Left : MinAlong;
                    MaxAlong = Point.Left > MaxAlong ? Point.Left : MaxAlong;
                }
            }
            return MaxAlong - MinAlong;
        };

        Bench Extent;
        const Deliver<WorkspaceRecordName> A =
            CommitPlacement(Extent.Naming, Extent.Sketch, Extent.Records, Extent.Revisions,
                            Place(SketchSubject::Rectangle, PlacementMethod::Extent, 2u));
        Bench Centred;
        const Deliver<WorkspaceRecordName> B =
            CommitPlacement(Centred.Naming, Centred.Sketch, Centred.Records, Centred.Revisions,
                            Place(SketchSubject::Rectangle, PlacementMethod::Centred, 2u));

        Claim(A.Resolved && B.Resolved, "an extent rectangle and a centred rectangle both declare");
        Claim(std::fabs(Extents(Extent.Sketch) - 40.0) < 1.0,
              "an EXTENT rectangle spans corner to corner - 40 wide");
        Claim(std::fabs(Extents(Centred.Sketch) - 80.0) < 1.0,
              "a CENTRED rectangle grows both ways from its centre - 80 wide");
    }
}

//========================================================================================================
// 4. A PAIR WITH NO ARM IS REFUSED, NOT GUESSED AT
//========================================================================================================

void ProveRefusals()
{
    std::printf("\n4. What cannot be placed is refused by name\n");

    // 🔴 A refusal, never a neighbouring arm. Silence here means drawing a shape the artist did not ask
    //    for, which is worse than declining.
    {
        Bench Stage;
        const Deliver<WorkspaceRecordName> Made =
            CommitPlacement(Stage.Naming, Stage.Sketch, Stage.Records, Stage.Revisions,
                            Place(SketchSubject::Polygon, PlacementMethod::Extent, 2u));
        Claim(!Made.Resolved, "a polygon placed by extent is refused - the catalogue does not accept it");
        Claim(Made.Error.Detail != nullptr && Made.Error.Detail[0] != 0,
              "and the refusal says so in words");
        Claim(Stage.Revisions.DeclaredCount() == 0u, "a refusal seals no revision");
        Claim(Stage.Sketch.Curves().empty(), "and declares no geometry");
    }

    // Too few anchors is a refusal, not a crash and not a half-shape.
    {
        Bench Stage;
        const Deliver<WorkspaceRecordName> Made =
            CommitPlacement(Stage.Naming, Stage.Sketch, Stage.Records, Stage.Revisions,
                            Place(SketchSubject::Arc, PlacementMethod::ThreePoint, 2u));
        Claim(!Made.Resolved, "an arc with two anchors is refused");
        Claim(Stage.Sketch.Curves().empty(), "with nothing half-declared behind it");
    }

    {
        Bench Stage;
        const Deliver<WorkspaceRecordName> Made =
            CommitPlacement(Stage.Naming, Stage.Sketch, Stage.Records, Stage.Revisions,
                            Place(SketchSubject::Line, PlacementMethod::Extent, 0u));
        Claim(!Made.Resolved, "a line with no anchors at all is refused");
    }

    // ⚠️ A degenerate shape is refused rather than declared as a zero-size thing the artist cannot see
    //    but can still select.
    {
        Bench Stage;
        SealedPlacement Placed = {};
        Placed.Subject = SketchSubject::Circle;
        Placed.Method  = PlacementMethod::Centred;
        Placed.Anchors = { { 10.0, 0.0, 10.0 }, { 10.0, 0.0, 10.0 } };
        const Deliver<WorkspaceRecordName> Made =
            CommitPlacement(Stage.Naming, Stage.Sketch, Stage.Records, Stage.Revisions, Placed);
        Claim(!Made.Resolved, "a circle of zero radius is refused");
        Claim(Stage.Revisions.DeclaredCount() == 0u, "and seals nothing");
    }

    {
        Bench Stage;
        SealedPlacement Placed = {};
        Placed.Subject = SketchSubject::Arc;
        Placed.Method  = PlacementMethod::ThreePoint;
        Placed.Anchors = { { 0.0, 0.0, 0.0 }, { 20.0, 0.0, 0.0 }, { 40.0, 0.0, 0.0 } };
        const Deliver<WorkspaceRecordName> Made =
            CommitPlacement(Stage.Naming, Stage.Sketch, Stage.Records, Stage.Revisions, Placed);
        Claim(!Made.Resolved, "three collinear points make no arc, and are refused");
    }
}

//========================================================================================================
// 5. WHAT A PLACEMENT LEAVES BEHIND
//========================================================================================================

void ProveAftermath()
{
    std::printf("\n5. The record, the naming and the history\n");

    Bench Stage;

    const Deliver<WorkspaceRecordName> First =
        CommitPlacement(Stage.Naming, Stage.Sketch, Stage.Records, Stage.Revisions,
                        Place(SketchSubject::Line, PlacementMethod::Extent, 2u));
    Claim(First.Resolved, "a line declares");

    const WorkspaceRecord* Record = Stage.Records.Resolve(First.Delivered);
    Claim(Record != nullptr, "its record resolves");
    if (Record != nullptr)
    {
        Claim(!Record->Naming.empty(), "and carries a name for the outliner");
        Claim(Record->SketchCurve.Assigned(), "pointing at the curve it declared");
    }

    const Deliver<WorkspaceRecordName> Second =
        CommitPlacement(Stage.Naming, Stage.Sketch, Stage.Records, Stage.Revisions,
                        Place(SketchSubject::Line, PlacementMethod::Extent, 2u));
    Claim(Second.Resolved, "a second line declares");
    Claim(Second.Delivered.IssuedIndex != First.Delivered.IssuedIndex, "under a name of its own");

    const WorkspaceRecord* SecondRecord = Stage.Records.Resolve(Second.Delivered);
    if (Record != nullptr && SecondRecord != nullptr)
        Claim(Record->Naming != SecondRecord->Naming, "and the two names differ");

    Claim(Stage.Revisions.DeclaredCount() == 2u, "two placements, two revisions, in order");

    // A dimension measures what is already there.
    {
        SealedPlacement Placed = {};
        Placed.Subject = SketchSubject::Dimension;
        Placed.Method  = PlacementMethod::Extent;
        Placed.Anchors = { { 0.0, 0.0, 0.0 }, { 40.0, 0.0, 0.0 } };

        SketchSnapPlacement A = {};
        A.Subject  = SketchSnapSubject::Endpoint;
        A.Position = { 0.0, 0.0, 0.0 };
        A.SketchPoint = { 1u };
        SketchSnapPlacement B = {};
        B.Subject  = SketchSnapSubject::Endpoint;
        B.Position = { 40.0, 0.0, 0.0 };
        B.SketchPoint = { 2u };
        Placed.Placements = { A, B };

        const std::uint32_t Before = Stage.Revisions.DeclaredCount();
        const Deliver<WorkspaceRecordName> Measured =
            CommitPlacement(Stage.Naming, Stage.Sketch, Stage.Records, Stage.Revisions, Placed);
        Claim(Measured.Resolved, "a dimension between two resolved placements declares");
        Claim(Stage.Revisions.DeclaredCount() == Before + 1u, "sealing one more revision");
    }
}

} // namespace

int main()
{
    std::printf("=========================================================================\n");
    std::printf("PLACEMENT COMMIT PROOF\n");
    std::printf("=========================================================================\n");

    ProveEveryPairAnswered();
    ProveEveryPairDeclares();
    ProveMethodIsHonoured();
    ProveRefusals();
    ProveAftermath();

    std::printf("\n=========================================================================\n");
    std::printf("%u claims, %u failures -> %s\n", Claims, Failures,
                Failures == 0u ? "PROVEN" : "REFUTED");
    std::printf("=========================================================================\n");
    return Failures == 0u ? 0 : 1;
}
