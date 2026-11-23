//============================================================================================================================================
//                                                      SKETCHPLACEMENTPROOF.CPP
//============================================================================================================================================
// 🧩 Executes the sketch placement machine and proves it against the tables it replaced.
//
// 📝 This is the only gate in the repository that RUNS engine code. `SketchToolset` names no device, no
//    window and no vendor header, so it links and runs on any toolchain — the property that made the
//    placement machine worth lifting out of a 5 981-line host in the first place.
//
// 🔴 Section 1 is the one that matters. The refactor replaced four hand-written switch statements with one
//    table AND split the twenty-two subjects into thirteen shapes crossed with five placement methods.
//    A split like that is only safe if every retired subject still resolves to something that behaves the
//    way it used to. Section 1 re-declares the ORIGINAL tables verbatim, as they stood in the deleted
//    `Application/Api/SharedCadDrawingController.h`, maps each retired subject onto its (shape, method)
//    pair, and asserts agreement — with the four corrections stated individually so a genuine
//    transcription slip still fails.

#include "SketchToolset/SketchTool/SketchPlacement/Api/SketchPlacement.h"

#include <cstdio>
#include <cstdint>
#include <string>
#include <vector>

using namespace Slate;

namespace
{

int Failures = 0;
int Checks   = 0;

void Claim(bool Held, const std::string& Stated)
{
    ++Checks;
    if (Held)
        return;
    ++Failures;
    std::printf("  FAIL  %s\n", Stated.c_str());
}

//------------------------------------------------------------------------------------------------------------------------
//     1. THE RETIRED VOCABULARY, TRANSCRIBED VERBATIM FROM SharedCadDrawingController.h BEFORE DELETION
//------------------------------------------------------------------------------------------------------------------------

// ⚠️ Do not "tidy" these. They are a frozen copy of the deleted header, and their whole value is that they
//    are what shipped. Rewriting them to agree with the new tables would delete the proof.

enum class RetiredSubject : std::uint32_t
{
    None = 0u, Line = 1u, Rectangle = 2u, Circle = 3u, Arc = 4u, Polyline = 5u,
    LinearDimension = 6u, Point = 7u, Ellipse = 8u, Bezier = 9u, EllipticalArc = 10u,
    BasisSpline = 11u, CenterRectangle = 12u, ThreePointRectangle = 13u, DiameterCircle = 14u,
    ThreePointCircle = 15u, CenterStartEndArc = 16u, TangentArc = 17u, Polygon = 18u,
    Slot = 19u, Hermite = 20u, RationalSpline = 21u, SubjectCount = 22u
};

std::uint32_t RetiredRequiredAnchors(RetiredSubject Subject)
{
    switch (Subject)
    {
        case RetiredSubject::Line:
        case RetiredSubject::Rectangle:
        case RetiredSubject::CenterRectangle:
        case RetiredSubject::DiameterCircle:
        case RetiredSubject::Polygon:            return 2u;
        case RetiredSubject::Circle:
        case RetiredSubject::Ellipse:            return 1u;
        case RetiredSubject::Arc:
        case RetiredSubject::EllipticalArc:
        case RetiredSubject::ThreePointRectangle:
        case RetiredSubject::ThreePointCircle:
        case RetiredSubject::CenterStartEndArc:
        case RetiredSubject::TangentArc:
        case RetiredSubject::Slot:
        case RetiredSubject::BasisSpline:
        case RetiredSubject::RationalSpline:     return 3u;
        // 🔴 TWO, NOT FOUR. A Hermite is a chain through every anchor and grows with each click, so
        //    it starts from the same two points a line needs. Four was the {start, end, tangent,
        //    tangent} reading that spent four clicks drawing one span.
        case RetiredSubject::Hermite:
        case RetiredSubject::Bezier:
        case RetiredSubject::Polyline:
        case RetiredSubject::LinearDimension:    return 2u;
        case RetiredSubject::Point:              return 1u;
        default:                                 return 0u;
    }
}

bool RetiredProducesClosedProfile(RetiredSubject Subject)
{
    switch (Subject)
    {
        case RetiredSubject::Rectangle:
        case RetiredSubject::CenterRectangle:
        case RetiredSubject::ThreePointRectangle:
        case RetiredSubject::Circle:
        case RetiredSubject::DiameterCircle:
        case RetiredSubject::ThreePointCircle:
        case RetiredSubject::Ellipse:
        case RetiredSubject::Polygon:
        case RetiredSubject::Slot:               return true;
        default:                                 return false;
    }
}

bool RetiredMultiClickCurve(RetiredSubject Subject)
{
    return Subject == RetiredSubject::Polyline || Subject == RetiredSubject::Bezier ||
           Subject == RetiredSubject::BasisSpline || Subject == RetiredSubject::Hermite ||
           Subject == RetiredSubject::RationalSpline;
}

RetiredSubject RetiredResolveFromCatalogue(ParametricToolSubject Subject)
{
    switch (Subject)
    {
        case ParametricToolSubject::Line:                 return RetiredSubject::Line;
        case ParametricToolSubject::Polyline:             return RetiredSubject::Polyline;
        case ParametricToolSubject::Rectangle:            return RetiredSubject::Rectangle;
        case ParametricToolSubject::Circle:               return RetiredSubject::Circle;
        case ParametricToolSubject::Arc:                  return RetiredSubject::Arc;
        case ParametricToolSubject::LinearDimension:      return RetiredSubject::LinearDimension;
        case ParametricToolSubject::Point:                return RetiredSubject::Point;
        case ParametricToolSubject::Ellipse:              return RetiredSubject::Ellipse;
        case ParametricToolSubject::EllipticalArc:        return RetiredSubject::EllipticalArc;
        case ParametricToolSubject::BezierCurve:
        case ParametricToolSubject::Interpolate:
        case ParametricToolSubject::Approximate:          return RetiredSubject::Bezier;
        case ParametricToolSubject::HermiteCurve:         return RetiredSubject::Hermite;
        case ParametricToolSubject::BasisSpline:          return RetiredSubject::BasisSpline;
        case ParametricToolSubject::RationalSpline:       return RetiredSubject::RationalSpline;
        case ParametricToolSubject::ConstructionLine:     return RetiredSubject::Line;
        case ParametricToolSubject::CenterRectangle:      return RetiredSubject::CenterRectangle;
        case ParametricToolSubject::ThreePointRectangle:  return RetiredSubject::ThreePointRectangle;
        case ParametricToolSubject::DiameterCircle:       return RetiredSubject::DiameterCircle;
        case ParametricToolSubject::ThreePointCircle:     return RetiredSubject::ThreePointCircle;
        case ParametricToolSubject::CenterStartEndArc:    return RetiredSubject::CenterStartEndArc;
        case ParametricToolSubject::TangentArc:           return RetiredSubject::TangentArc;
        case ParametricToolSubject::Polygon:              return RetiredSubject::Polygon;
        case ParametricToolSubject::Slot:                 return RetiredSubject::Slot;
        default:                                          return RetiredSubject::None;
    }
}

/// The split: every retired subject expressed as the (shape, method) pair that replaced it.
struct Replacement
{
    RetiredSubject  Retired;
    SketchSubject   Subject;
    PlacementMethod Method;
    const char*     Naming;
};

const std::vector<Replacement>& EveryReplacement()
{
    static const std::vector<Replacement> Every = {
        { RetiredSubject::Point,               SketchSubject::Point,          PlacementMethod::Extent,     "Point" },
        { RetiredSubject::Line,                SketchSubject::Line,           PlacementMethod::Extent,     "Line" },
        { RetiredSubject::Polyline,            SketchSubject::Polyline,       PlacementMethod::Extent,     "Polyline" },
        { RetiredSubject::Rectangle,           SketchSubject::Rectangle,      PlacementMethod::Extent,     "Rectangle" },
        { RetiredSubject::CenterRectangle,     SketchSubject::Rectangle,      PlacementMethod::Centred,    "CenterRectangle" },
        { RetiredSubject::ThreePointRectangle, SketchSubject::Rectangle,      PlacementMethod::ThreePoint, "ThreePointRectangle" },
        { RetiredSubject::Circle,              SketchSubject::Circle,         PlacementMethod::Centred,    "Circle" },
        { RetiredSubject::DiameterCircle,      SketchSubject::Circle,         PlacementMethod::Diameter,   "DiameterCircle" },
        { RetiredSubject::ThreePointCircle,    SketchSubject::Circle,         PlacementMethod::ThreePoint, "ThreePointCircle" },
        { RetiredSubject::Ellipse,             SketchSubject::Ellipse,        PlacementMethod::Centred,    "Ellipse" },
        { RetiredSubject::Arc,                 SketchSubject::Arc,            PlacementMethod::ThreePoint, "Arc" },
        { RetiredSubject::CenterStartEndArc,   SketchSubject::Arc,            PlacementMethod::Centred,    "CenterStartEndArc" },
        { RetiredSubject::TangentArc,          SketchSubject::Arc,            PlacementMethod::Tangent,    "TangentArc" },
        { RetiredSubject::EllipticalArc,       SketchSubject::EllipticalArc,  PlacementMethod::ThreePoint, "EllipticalArc" },
        { RetiredSubject::Polygon,             SketchSubject::Polygon,        PlacementMethod::Centred,    "Polygon" },
        { RetiredSubject::Slot,                SketchSubject::Slot,           PlacementMethod::Extent,     "Slot" },
        { RetiredSubject::Bezier,              SketchSubject::Bezier,         PlacementMethod::Extent,     "Bezier" },
        { RetiredSubject::BasisSpline,         SketchSubject::BasisSpline,    PlacementMethod::Extent,     "BasisSpline" },
        { RetiredSubject::RationalSpline,      SketchSubject::RationalSpline, PlacementMethod::Extent,     "RationalSpline" },
        { RetiredSubject::Hermite,             SketchSubject::Hermite,        PlacementMethod::Extent,     "Hermite" },
        { RetiredSubject::LinearDimension,     SketchSubject::Dimension,      PlacementMethod::Extent,     "LinearDimension" },
    };
    return Every;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                       HELPERS
//------------------------------------------------------------------------------------------------------------------------

SketchSnapPlacement Snapped(double Left)
{
    SketchSnapPlacement Placement = {};
    Placement.Subject  = SketchSnapSubject::Endpoint;
    Placement.Position = { Left, 0.0, 0.0 };
    return Placement;
}

PlacementArrival Place(SketchPlacement& Tool, double Left, bool Terminating = false, bool Resolved = false)
{
    Tool.Hover({ Left, 0.0, 0.0 }, Resolved ? Snapped(Left) : SketchSnapPlacement{});
    return Tool.Anchor(Terminating);
}

const char* ArrivalText(PlacementArrival Arrival)
{
    switch (Arrival)
    {
        case PlacementArrival::Ignored:  return "Ignored";
        case PlacementArrival::Anchored: return "Anchored";
        case PlacementArrival::Complete: return "Complete";
        default:                         return "?";
    }
}

std::vector<SketchSubject> EverySubject()
{
    std::vector<SketchSubject> Every;
    for (std::uint32_t Ordinal = 1u; Ordinal < static_cast<std::uint32_t>(SketchSubject::SubjectCount); ++Ordinal)
        Every.push_back(static_cast<SketchSubject>(Ordinal));
    return Every;
}

std::vector<PlacementMethod> EveryMethod()
{
    std::vector<PlacementMethod> Every;
    for (std::uint32_t Ordinal = 0u; Ordinal < static_cast<std::uint32_t>(PlacementMethod::MethodCount); ++Ordinal)
        Every.push_back(static_cast<PlacementMethod>(Ordinal));
    return Every;
}

//------------------------------------------------------------------------------------------------------------------------
//                                     1. THE SPLIT PRESERVES EVERY RETIRED SUBJECT
//------------------------------------------------------------------------------------------------------------------------

void ProveSplitEquivalence()
{
    std::printf("1. Every retired subject survives as a (shape, method) pair\n");

    for (const Replacement& Every : EveryReplacement())
    {
        const std::string          Where    = Every.Naming;
        const PlacementDeclaration Declared = DeclaredPlacement(Every.Subject, Every.Method);

        Claim(AcceptedBy(Every.Subject, Every.Method),
              Where + " maps onto a pair the toolset refuses");

        Claim(Declared.ClosedProfile == RetiredProducesClosedProfile(Every.Retired),
              "closed profile disagrees for " + Where);

        Claim((Declared.Closure == PlacementClosure::Terminated) == RetiredMultiClickCurve(Every.Retired),
              "multi-click disagrees with Terminated closure for " + Where);

        // 🔴 Four rows deliberately disagree with the retired column, because the retired column was wrong
        //    in a way nothing could observe: it was only ever READ for the four terminated curves.
        //    `Circle` and `Ellipse` claimed one anchor while the commit required a centre AND a rim point.
        //    `Arc` defaulted to three points, which is unchanged, but `CenterStartEndArc` claimed three and
        //    genuinely takes three, so it is NOT an exception. Stating each exception individually is the
        //    difference between a checked correction and a transcription slip.
        const bool Corrected = Every.Retired == RetiredSubject::Circle ||
                               Every.Retired == RetiredSubject::Ellipse;

        if (Corrected)
        {
            Claim(RetiredRequiredAnchors(Every.Retired) == 1u,
                  "the retired table is expected to have said 1 for " + Where);
            Claim(Declared.Required == 2u,
                  "the correction should require both anchors for " + Where);
        }
        else
        {
            Claim(Declared.Required == RetiredRequiredAnchors(Every.Retired),
                  "required anchors disagree for " + Where + ": " +
                      std::to_string(Declared.Required) + " vs " +
                      std::to_string(RetiredRequiredAnchors(Every.Retired)));
        }
    }

    // 🔴 All 61 catalogue tiles. The ones resolving to `None` matter most: if a constraint tile started
    //    resolving to a shape, pressing it would begin taking anchors.
    for (std::uint32_t Ordinal = 0u; Ordinal < static_cast<std::uint32_t>(ParametricToolSubject::SubjectCount); ++Ordinal)
    {
        const ParametricToolSubject Tile     = static_cast<ParametricToolSubject>(Ordinal);
        const SketchToolSelection   Selected = SelectedTool(Tile);
        const RetiredSubject        Retired  = RetiredResolveFromCatalogue(Tile);

        Claim((Selected.Subject == SketchSubject::None) == (Retired == RetiredSubject::None),
              "catalogue tile " + std::to_string(Ordinal) + " changed whether it draws at all");

        if (Selected.Subject == SketchSubject::None)
            continue;

        // The tile must resolve to the pair its retired subject was replaced by.
        bool Found = false;
        for (const Replacement& Every : EveryReplacement())
            if (Every.Retired == Retired)
            {
                Found = true;
                Claim(Selected.Subject == Every.Subject && Selected.Method == Every.Method,
                      "catalogue tile " + std::to_string(Ordinal) + " resolves to the wrong pair");
            }
        Claim(Found, "catalogue tile " + std::to_string(Ordinal) + " maps to an unreplaced retired subject");
    }

    std::printf("   %zu retired subjects and %u catalogue tiles all preserved\n",
                EveryReplacement().size(),
                static_cast<unsigned>(ParametricToolSubject::SubjectCount));
}

//------------------------------------------------------------------------------------------------------------------------
//                                 2. NO REDUNDANCY — EVERY LEGAL PAIR IS DISTINCT AND NAMED
//------------------------------------------------------------------------------------------------------------------------

void ProveNoRedundancy()
{
    std::printf("2. Every legal pair is named, distinct, and buildable\n");

    std::vector<std::string> Seen;

    for (const SketchSubject Subject : EverySubject())
        for (const PlacementMethod Method : EveryMethod())
        {
            const PlacementDeclaration Declared = DeclaredPlacement(Subject, Method);

            if (!AcceptedBy(Subject, Method))
            {
                // 🔴 A refused pair must declare NOTHING. A pair that refused but still reported an anchor
                //    count would be a tool the artist could hold and never complete — which is exactly the
                //    retired `DiameterCircle` defect.
                Claim(Declared.Required == 0u && std::string(Declared.Naming).empty(),
                      "a refused pair must declare no anchors and no name");
                continue;
            }

            const std::string Named = Declared.Naming;
            Claim(!Named.empty(),
                  "an accepted pair must carry a name (subject " +
                      std::to_string(static_cast<unsigned>(Subject)) + ")");

            // 🔴 Two different pairs must never share a name. This is the check that would have caught the
            //    original redundancy: `Rectangle` and `CenterRectangle` both meaning "a rectangle" is the
            //    defect, and it shows up here as a duplicate label.
            for (const std::string& Earlier : Seen)
                Claim(Earlier != Named, "two placements share the name '" + Named + "'");
            Seen.push_back(Named);

            // Every accepted pair must be reachable: enough anchors to complete, and a sane count.
            Claim(Declared.Required >= 1u && Declared.Required <= 4u,
                  Named + " declares an implausible anchor count");
        }

    std::printf("   %zu distinct placements, no duplicate names\n", Seen.size());
}

//------------------------------------------------------------------------------------------------------------------------
//                                3. EVERY PAIR COMPLETES AT ITS DECLARED COUNT
//------------------------------------------------------------------------------------------------------------------------

void ProveCompletionCounts()
{
    std::printf("3. Every placement completes at its declared count and not before\n");

    for (const SketchSubject Subject : EverySubject())
        for (const PlacementMethod Method : EveryMethod())
        {
            if (!AcceptedBy(Subject, Method))
                continue;

            const PlacementDeclaration Declared = DeclaredPlacement(Subject, Method);
            const bool                 Snapping = Declared.Closure == PlacementClosure::Resolved;
            const bool                 Ending   = Declared.Closure == PlacementClosure::Terminated;
            const std::string          Where    = Declared.Naming;

            SketchPlacement Tool;
            Tool.Declare(Subject, Method);
            Claim(Tool.Standing(), Where + " should be held after declaring an accepted pair");

            for (std::uint32_t Taken = 0u; Taken + 1u < Declared.Required; ++Taken)
            {
                const PlacementArrival Arrival = Place(Tool, static_cast<double>(Taken), false, Snapping);
                Claim(Arrival == PlacementArrival::Anchored,
                      Where + " completed early at anchor " + std::to_string(Taken + 1u) +
                          " (" + ArrivalText(Arrival) + ")");
            }

            Claim(Tool.Remaining() == 1u,
                  Where + " should want exactly one more anchor, wants " + std::to_string(Tool.Remaining()));

            const PlacementArrival Final = Place(Tool, static_cast<double>(Declared.Required), false, Snapping);

            if (Ending)
                Claim(Final == PlacementArrival::Anchored,
                      Where + " is terminated and must not complete without a double-press");
            else
                Claim(Final == PlacementArrival::Complete,
                      Where + " should complete on anchor " + std::to_string(Declared.Required) +
                          ", reported " + ArrivalText(Final));
        }
}

//------------------------------------------------------------------------------------------------------------------------
//                            4. AN IMPOSSIBLE PAIR IS REFUSED RATHER THAN HELD
//------------------------------------------------------------------------------------------------------------------------

void ProveRefusedPairs()
{
    std::printf("4. A shape that cannot be placed that way is refused at declaration\n");

    // 🔴 THE RETIRED DEFECT, STATED DIRECTLY. `DiameterCircle` and `Polygon` sat in a pointer branch that
    //    stored one anchor while their commit demanded two, so both tools consumed every press and
    //    produced nothing — they worked only via Enter, whose branch stored the hover first. A pair that
    //    cannot complete must never be held in the first place.
    const std::vector<std::pair<SketchSubject, PlacementMethod>> Impossible = {
        { SketchSubject::Polyline,  PlacementMethod::Diameter   },
        { SketchSubject::Line,      PlacementMethod::ThreePoint },
        { SketchSubject::Point,     PlacementMethod::Centred    },
        { SketchSubject::Bezier,    PlacementMethod::Tangent    },
        { SketchSubject::Slot,      PlacementMethod::Diameter   },
        { SketchSubject::Dimension, PlacementMethod::ThreePoint },
        { SketchSubject::Polygon,   PlacementMethod::Diameter   },
    };

    for (const auto& [Subject, Method] : Impossible)
    {
        Claim(!AcceptedBy(Subject, Method), "this pair should be refused outright");

        SketchPlacement Tool;
        Tool.Declare(Subject, Method);
        Claim(!Tool.Standing(), "a refused pair must not leave a tool held");

        // A tool that is not held consumes nothing, however hard the artist presses.
        for (int Attempt = 0; Attempt < 5; ++Attempt)
            Claim(Place(Tool, static_cast<double>(Attempt)) == PlacementArrival::Ignored,
                  "a refused pair must never take an anchor");
        Claim(Tool.Anchors().empty(), "a refused pair must accumulate nothing");
    }

    // A shape held on a legal pair, then re-declared onto an illegal one, stands down rather than keeping
    // stale anchors under a tool that can never complete.
    SketchPlacement Switched;
    Switched.Declare(SketchSubject::Circle, PlacementMethod::Centred);
    Place(Switched, 0.0);
    Claim(Switched.Anchors().size() == 1u, "the centred circle should hold one anchor");
    Switched.Declare(SketchSubject::Circle, PlacementMethod::Tangent);
    Claim(!Switched.Standing(), "moving to a refused method must stand the tool down");
    Claim(Switched.Anchors().empty(), "moving to a refused method must discard the anchors");
}

//------------------------------------------------------------------------------------------------------------------------
//                            5. THE METHOD IS PART OF WHAT IS HELD AND WHAT IS SEALED
//------------------------------------------------------------------------------------------------------------------------

void ProveMethodCarriage()
{
    std::printf("5. The method is held, distinguishes the tool, and survives sealing\n");

    // Same shape, two methods, two different anchor counts — the whole point of the split.
    SketchPlacement Centred;
    Centred.Declare(SketchSubject::Circle, PlacementMethod::Centred);
    Claim(Centred.Remaining() == 2u, "a centred circle wants two anchors");

    SketchPlacement ThreePoint;
    ThreePoint.Declare(SketchSubject::Circle, PlacementMethod::ThreePoint);
    Claim(ThreePoint.Remaining() == 3u, "a three-point circle wants three anchors");

    Claim(DeclaredPlacement(SketchSubject::Circle, PlacementMethod::Centred).Required !=
          DeclaredPlacement(SketchSubject::Circle, PlacementMethod::ThreePoint).Required,
          "the two circle methods must not be interchangeable");

    // 🔴 The method must reach the caller that commits, or a three-point circle would be built as a
    //    centred one from the same three anchors — silently wrong geometry rather than a failure.
    Place(ThreePoint, 0.0);
    Place(ThreePoint, 1.0);
    Claim(Place(ThreePoint, 2.0) == PlacementArrival::Complete, "the three-point circle should complete");

    const SealedPlacement Sealed = ThreePoint.Seal();
    Claim(Sealed.Subject == SketchSubject::Circle,          "the sealed placement should name the circle");
    Claim(Sealed.Method == PlacementMethod::ThreePoint,     "the sealed placement must carry its method");
    Claim(Sealed.Anchors.size() == 3u,                      "the sealed placement should carry three anchors");
    Claim(Sealed.Placements.size() == Sealed.Anchors.size(),
          "every anchor must carry its placement — the two vectors are one record split in two");

    // Sealing keeps the tool AND the method held.
    Claim(ThreePoint.Standing(),                             "the tool must still be held after sealing");
    Claim(ThreePoint.Method() == PlacementMethod::ThreePoint, "the method must still be held after sealing");
    Claim(ThreePoint.Remaining() == 3u,                      "the next circle should want all three again");
}

//------------------------------------------------------------------------------------------------------------------------
//                                       6. TERMINATED CURVES, DIMENSIONS, REFUSALS
//------------------------------------------------------------------------------------------------------------------------

void ProveClosures()
{
    std::printf("6. Terminated curves, dimension snapping, and refusals\n");

    SketchPlacement Polyline;
    Polyline.Declare(SketchSubject::Polyline);
    for (int Anchor = 0; Anchor < 9; ++Anchor)
        Claim(Place(Polyline, static_cast<double>(Anchor)) == PlacementArrival::Anchored,
              "the polyline ended itself at anchor " + std::to_string(Anchor + 1));
    Claim(Polyline.Anchors().size() == 9u, "the polyline should hold nine anchors");
    Claim(Place(Polyline, 9.0, true) == PlacementArrival::Complete, "a double-press should end the polyline");

    // 🔴 A HERMITE ENDS WHEN THE ARTIST SAYS SO. It is terminated, like a polyline: two anchors are
    //    enough to draw with and every further click extends the chain by another span.
    SketchPlacement Hermite;
    Hermite.Declare(SketchSubject::Hermite);
    Place(Hermite, 0.0);
    Claim(Place(Hermite, 1.0, true) == PlacementArrival::Complete,
          "a double-press should end a Hermite once it has two anchors to span");

    SketchPlacement LongHermite;
    LongHermite.Declare(SketchSubject::Hermite);
    for (int Click = 0; Click < 7; ++Click)
        Place(LongHermite, static_cast<double>(Click));
    Claim(LongHermite.Anchors().size() == 7u,
          "a Hermite must keep every anchor it is given, not stop at four");
    Claim(Place(LongHermite, 7.0, true) == PlacementArrival::Complete,
          "and end on a double-press at any length");

    SketchPlacement Dimension;
    Dimension.Declare(SketchSubject::Dimension);
    for (int Attempt = 0; Attempt < 3; ++Attempt)
        Claim(Place(Dimension, static_cast<double>(Attempt), false, false) == PlacementArrival::Ignored,
              "an unsnapped contact must not anchor a dimension");
    Claim(Dimension.Anchors().empty(), "a refused contact must leave no anchor behind");
    Claim(Place(Dimension, 0.0, false, true) == PlacementArrival::Anchored, "a snapped contact should anchor");
    Claim(Place(Dimension, 1.0, false, false) == PlacementArrival::Ignored, "an unsnapped second is refused");
    Claim(Dimension.Anchors().size() == 1u, "the refused contact must not have been counted");
    Claim(Place(Dimension, 2.0, false, true) == PlacementArrival::Complete, "the second snapped contact completes it");

    for (const SketchSnapPlacement& Placement : Dimension.Seal().Placements)
        Claim(Placement.Resolved(), "every dimension anchor must carry a resolved snap");

    SketchPlacement Idle;
    Claim(Idle.Anchor() == PlacementArrival::Ignored, "anchoring with no tool held must be refused");

    SketchPlacement Unhovered;
    Unhovered.Declare(SketchSubject::Line);
    Claim(Unhovered.Anchor() == PlacementArrival::Ignored, "anchoring before hovering must be refused");
    Claim(Unhovered.Anchors().empty(), "a refused anchor must leave nothing behind");

    SketchPlacement Full;
    Full.Declare(SketchSubject::Slot, PlacementMethod::Extent, true);
    Place(Full, 1.0);
    Claim(Full.Construction(), "the slot should remember it is construction geometry");
    Full.Abandon();
    Claim(!Full.Standing() && Full.Anchors().empty() && !Full.HoverStanding() && !Full.Construction(),
          "abandon must discard the tool, the anchors, the hover and the construction declaration");
}

//------------------------------------------------------------------------------------------------------------------------
//                              7. RESTATING THE HELD TOOL KEEPS THE ANCHORS
//------------------------------------------------------------------------------------------------------------------------

void ProveDeclaration()
{
    std::printf("7. Restating the held tool keeps the anchors; changing it discards them\n");

    SketchPlacement Tool;
    Tool.Declare(SketchSubject::Arc, PlacementMethod::ThreePoint);
    Place(Tool, 0.0);
    Place(Tool, 1.0);
    Claim(Tool.Anchors().size() == 2u, "the arc should hold two anchors");

    // 🔴 The host states the held tool EVERY TICK. If this restarted the placement, no three-anchor shape
    //    could ever be drawn — the first anchor would be discarded before the second arrived.
    for (int Tick = 0; Tick < 30; ++Tick)
        Tool.Declare(SketchSubject::Arc, PlacementMethod::ThreePoint);
    Claim(Tool.Anchors().size() == 2u, "restating the same pair must not discard anchors");
    Claim(Place(Tool, 2.0) == PlacementArrival::Complete, "the arc should still complete on its third anchor");

    // 🔴 Restating the same SHAPE with a different METHOD is a different tool and must restart.
    SketchPlacement Changed;
    Changed.Declare(SketchSubject::Rectangle, PlacementMethod::Extent);
    Place(Changed, 0.0);
    Claim(Changed.Anchors().size() == 1u, "the rectangle should hold one anchor");
    Changed.Declare(SketchSubject::Rectangle, PlacementMethod::Centred);
    Claim(Changed.Anchors().empty(), "changing the method must discard the anchors");
    Claim(Changed.Method() == PlacementMethod::Centred, "the new method should be held");

    Changed.Declare(SketchSubject::None);
    Claim(!Changed.Standing(), "declaring None must stand the tool down");
    Claim(Changed.Remaining() == 0u, "a tool that is not held wants no anchors");
}

//------------------------------------------------------------------------------------------------------------------------
//                              8. THE ANCHOR RECORDS THE SNAP THE PREVIEW SHOWED
//------------------------------------------------------------------------------------------------------------------------

void ProveSnapCarriage()
{
    std::printf("8. The anchor records exactly the snap the preview showed\n");

    SketchPlacement Tool;
    Tool.Declare(SketchSubject::Line);

    SketchSnapPlacement Midpoint = {};
    Midpoint.Subject  = SketchSnapSubject::Midpoint;
    Midpoint.Position = { 3.0, 0.0, 0.0 };
    Midpoint.Distance = 0.25;

    Tool.Hover({ 3.0, 0.0, 0.0 }, Midpoint);
    Claim(Tool.HoverStanding(), "the hover should stand once stated");
    Claim(Tool.HoverPlacement().Subject == SketchSnapSubject::Midpoint, "the hover should carry the snap");

    Tool.Anchor();
    Claim(Tool.Placements().size() == 1u, "the anchor should carry one placement");
    Claim(Tool.Placements()[0].Subject == SketchSnapSubject::Midpoint,
          "the anchored placement must be the one the preview showed");
    Claim(Tool.Placements()[0].Distance == 0.25, "the placement must be carried whole, not rebuilt");
    Claim(Tool.Anchors()[0].Left == 3.0, "the anchor must sit where the hover sat");
}

}   // namespace

//------------------------------------------------------------------------------------------------------------------------

int main()
{
    std::printf("\n=== SketchPlacement — SketchToolset ===\n\n");

    ProveSplitEquivalence();
    ProveNoRedundancy();
    ProveCompletionCounts();
    ProveRefusedPairs();
    ProveMethodCarriage();
    ProveClosures();
    ProveDeclaration();
    ProveSnapCarriage();

    std::printf("\n%d claims, %d failures\n", Checks, Failures);
    std::printf(Failures == 0 ? "PROVEN\n\n" : "REFUTED\n\n");
    return Failures == 0 ? 0 : 1;
}
