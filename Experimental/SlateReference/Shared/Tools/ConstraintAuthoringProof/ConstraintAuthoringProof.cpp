//============================================================================================================================================
//                                                   CONSTRAINTAUTHORINGPROOF.CPP
//============================================================================================================================================
// 🧩 Proves that every relationship the enumeration declares is answered, that what each one DEMANDS of
//    the selection is honoured, and that a badge hangs on the geometry it describes.

#include "SlateWorkspace/Discipline/ConstraintAuthoring/Api/ConstraintAuthoring.h"

#include "SlateShape/Sketch/SketchPolyline/Api/SketchPolyline.h"

#include <cmath>
#include <cstdio>
#include <cstring>
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

const char* SubjectText(ConstraintSubject Subject)
{
    switch (Subject)
    {
        case ConstraintSubject::Coincident:    return "Coincident";
        case ConstraintSubject::Horizontal:    return "Horizontal";
        case ConstraintSubject::Vertical:      return "Vertical";
        case ConstraintSubject::Parallel:      return "Parallel";
        case ConstraintSubject::Perpendicular: return "Perpendicular";
        case ConstraintSubject::Tangent:       return "Tangent";
        case ConstraintSubject::Equal:         return "Equal";
        case ConstraintSubject::Fixed:         return "Fixed";
        case ConstraintSubject::SubjectCount:  break;
    }
    return "?";
}

const char* DemandText(ConstraintDemand Demand)
{
    switch (Demand)
    {
        case ConstraintDemand::OneCurve:    return "one curve";
        case ConstraintDemand::TwoCurves:   return "two curves";
        case ConstraintDemand::TwoPoints:   return "two points";
        case ConstraintDemand::DemandCount: break;
    }
    return "?";
}

SketchCurveName Curve(std::uint32_t Index)
{
    SketchCurveName Named;
    Named.IssuedIndex = Index;
    return Named;
}

SketchPointName Point(std::uint32_t Index)
{
    SketchPointName Named;
    Named.IssuedIndex = Index;
    return Named;
}

//========================================================================================================
// 1. EVERY RELATIONSHIP THE ENUMERATION DECLARES IS ANSWERED
//========================================================================================================

// 🔴 Walked over the enumeration rather than over the table, so a subject added to the enumeration and
//    forgotten here is a failure rather than a silent gap. A table that only proves itself proves nothing.

void ProveEveryRelationshipIsAnswered()
{
    std::printf("\n1. Every relationship the enumeration declares is answered\n");

    for (std::uint32_t Index = 0u; Index < static_cast<std::uint32_t>(ConstraintSubject::SubjectCount); ++Index)
    {
        const ConstraintSubject Subject = static_cast<ConstraintSubject>(Index);

        ClaimNamed(ConstraintSupported(Subject),
                   std::string("the unit authors ") + SubjectText(Subject));

        const Deliver<ConstraintDeclaration> Declared = DeclaredConstraint(Subject);
        ClaimNamed(Declared.Resolved,
                   std::string("...and states what it needs: ") + SubjectText(Subject));

        if (!Declared.Resolved)
            continue;

        ClaimNamed(Declared.Delivered.Subject == Subject,
                   std::string("...and answers for the subject asked about: ") + SubjectText(Subject));

        // ⚠️ A glyph must be something the artist can tell apart, never the fallback.
        const char* Glyph = ConstraintGlyph(Subject);
        ClaimNamed(Glyph != nullptr && std::strlen(Glyph) > 0u && std::strcmp(Glyph, "?") != 0,
                   std::string("...and carries a badge of its own: ") + SubjectText(Subject));

        ClaimNamed(Declared.Delivered.Naming != nullptr && std::strlen(Declared.Delivered.Naming) > 0u,
                   std::string("...and a name the artist reads: ") + SubjectText(Subject));

        ClaimNamed(Declared.Delivered.Demand != ConstraintDemand::DemandCount,
                   std::string("...and a real demand: ") + SubjectText(Subject));
    }

    // 🔴 No two relationships may share a badge, or the artist cannot tell which is which.
    for (std::uint32_t Left = 0u; Left < static_cast<std::uint32_t>(ConstraintSubject::SubjectCount); ++Left)
    {
        for (std::uint32_t Right = Left + 1u; Right < static_cast<std::uint32_t>(ConstraintSubject::SubjectCount); ++Right)
        {
            const char* First  = ConstraintGlyph(static_cast<ConstraintSubject>(Left));
            const char* Second = ConstraintGlyph(static_cast<ConstraintSubject>(Right));
            ClaimNamed(std::strcmp(First, Second) != 0,
                       std::string("the badges differ: ") + SubjectText(static_cast<ConstraintSubject>(Left))
                       + " and " + SubjectText(static_cast<ConstraintSubject>(Right)));
        }
    }
}

//========================================================================================================
// 2. WHAT A RELATIONSHIP DEMANDS IS HONOURED
//========================================================================================================

// 🔴 THE DEFECT THIS UNIT WAS WRITTEN FOR. The host tested the subject in an `if`-chain and then reached
//    for whichever selection field the branch assumed, so what a constraint needed was decided by where
//    it sat in the chain. Here the demand is a property of the relationship, and every combination of
//    what the artist might have selected is walked against it.

void ProveTheDemandIsHonoured()
{
    std::printf("\n2. A relationship is refused unless the selection satisfies it\n");

    // 🔴 ⚠️ WHAT EACH RELATIONSHIP NEEDS, STATED HERE INDEPENDENTLY OF THE UNIT. Reading the demand from
    //    `DeclaredConstraint` and then checking the unit against it proves only that the unit agrees with
    //    itself — a sabotage that changed Coincident to want two curves survived exactly that way. These
    //    are derived from what the RELATIONSHIP means: coincident makes two points meet, horizontal and
    //    vertical and fixed each pin one curve, and the rest relate two curves to each other.
    const struct { ConstraintSubject Subject; ConstraintDemand Demand; } Expected[8] =
    {
        { ConstraintSubject::Coincident,    ConstraintDemand::TwoPoints },
        { ConstraintSubject::Horizontal,    ConstraintDemand::OneCurve  },
        { ConstraintSubject::Vertical,      ConstraintDemand::OneCurve  },
        { ConstraintSubject::Parallel,      ConstraintDemand::TwoCurves },
        { ConstraintSubject::Perpendicular, ConstraintDemand::TwoCurves },
        { ConstraintSubject::Tangent,       ConstraintDemand::TwoCurves },
        { ConstraintSubject::Equal,         ConstraintDemand::TwoCurves },
        { ConstraintSubject::Fixed,         ConstraintDemand::OneCurve  },
    };

    for (const auto& Row : Expected)
    {
        const Deliver<ConstraintDeclaration> Stated = DeclaredConstraint(Row.Subject);
        ClaimNamed(Stated.Resolved && Stated.Delivered.Demand == Row.Demand,
                   std::string("the unit demands what the relationship MEANS: ")
                   + SubjectText(Row.Subject) + " needs " + DemandText(Row.Demand));
    }

    for (std::uint32_t Index = 0u; Index < static_cast<std::uint32_t>(ConstraintSubject::SubjectCount); ++Index)
    {
        const ConstraintSubject Subject = static_cast<ConstraintSubject>(Index);
        const Deliver<ConstraintDeclaration> Declared = DeclaredConstraint(Subject);
        if (!Declared.Resolved)
            continue;

        const ConstraintDemand Demand = Declared.Delivered.Demand;

        // Every one of the sixteen selections the artist could be holding.
        for (std::uint32_t Mask = 0u; Mask < 16u; ++Mask)
        {
            const bool HasPrimaryCurve   = (Mask & 1u) != 0u;
            const bool HasSecondaryCurve = (Mask & 2u) != 0u;
            const bool HasPrimaryPoint   = (Mask & 4u) != 0u;
            const bool HasSecondaryPoint = (Mask & 8u) != 0u;

            const bool Met = ConstraintDemandMet(Demand, HasPrimaryCurve, HasSecondaryCurve,
                                                 HasPrimaryPoint, HasSecondaryPoint);

            // What the demand SHOULD say, derived here rather than read from the unit.
            const bool Expected = Demand == ConstraintDemand::OneCurve  ? HasPrimaryCurve
                                : Demand == ConstraintDemand::TwoCurves ? (HasPrimaryCurve && HasSecondaryCurve)
                                : Demand == ConstraintDemand::TwoPoints ? (HasPrimaryPoint && HasSecondaryPoint)
                                                                        : false;

            ClaimNamed(Met == Expected,
                       std::string("the demand is honoured for ") + SubjectText(Subject)
                       + " needing " + DemandText(Demand) + " at selection " + std::to_string(Mask));

            // 🔴 And building it agrees with asking about it, so the two cannot drift apart.
            const Deliver<ConstraintSpecification> Built =
                DeclareConstraintFrom(Subject,
                                      HasPrimaryCurve   ? Curve(1u) : SketchCurveName{},
                                      HasSecondaryCurve ? Curve(2u) : SketchCurveName{},
                                      HasPrimaryPoint   ? Point(0x101u) : SketchPointName{},
                                      HasSecondaryPoint ? Point(0x202u) : SketchPointName{});

            ClaimNamed(Built.Resolved == Expected,
                       std::string("...and declaring agrees with asking: ") + SubjectText(Subject)
                       + " at selection " + std::to_string(Mask));

            if (!Built.Resolved)
                continue;

            ClaimNamed(Built.Delivered.Subject == Subject,
                       std::string("...and the relationship built is the one asked for: ")
                       + SubjectText(Subject));

            // ⚠️ A one-curve relationship must NOT carry a secondary reference even when one was offered.
            if (Demand == ConstraintDemand::OneCurve)
                ClaimNamed(!Built.Delivered.Secondary.Declared(),
                           std::string("...and a one-curve relationship relates to nothing else: ")
                           + SubjectText(Subject));
        }
    }
}

//========================================================================================================
// 3. A RELATIONSHIP THE UNIT DOES NOT KNOW IS REFUSED BY NAME
//========================================================================================================

void ProveWhatIsRefused()
{
    std::printf("\n3. What the unit does not author, it refuses\n");

    const ConstraintSubject Stranger = static_cast<ConstraintSubject>(99u);

    Claim(!ConstraintSupported(Stranger), "a subject outside the enumeration is not authored");

    const Deliver<ConstraintDeclaration> Declared = DeclaredConstraint(Stranger);
    Claim(!Declared.Resolved, "...and asking about it is refused");
    Claim(!Declared.Resolved && Declared.Error.Detail != nullptr, "...by name");

    const Deliver<ConstraintSpecification> Built =
        DeclareConstraintFrom(Stranger, Curve(1u), Curve(2u), Point(0x101u), Point(0x202u));
    Claim(!Built.Resolved, "...and declaring it is refused even with everything selected");

    // 📝 A glyph still answers, because decoration must not refuse.
    Claim(std::strcmp(ConstraintGlyph(Stranger), "?") == 0,
          "an unknown relationship still reads as something rather than crashing");
}

//========================================================================================================
// 4. EVERY VERDICT THE SOLVER CAN RETURN IS NAMED
//========================================================================================================

// 🔴 The host named four of the six, so a sketch that was simply not solved yet and one carrying a
//    duplicate constraint both read as an unexplained failure.

void ProveEveryVerdictIsNamed()
{
    std::printf("\n4. Every verdict the solver can return reads as something\n");

    const ConstraintDisposition Every[6] =
    {
        ConstraintDisposition::NotRequested,
        ConstraintDisposition::InvalidSketch,
        ConstraintDisposition::UnsupportedConstraint,
        ConstraintDisposition::ConflictingConstraint,
        ConstraintDisposition::RepeatedConstraint,
        ConstraintDisposition::Produced,
    };

    for (const ConstraintDisposition Disposition : Every)
    {
        const char* Naming = ConstraintDispositionNaming(Disposition);
        ClaimNamed(Naming != nullptr && std::strlen(Naming) > 0u,
                   std::string("a verdict reads as something: ")
                   + std::to_string(static_cast<std::uint32_t>(Disposition)));
        ClaimNamed(std::strcmp(Naming, "unknown") != 0,
                   std::string("...and not as unknown: ")
                   + std::to_string(static_cast<std::uint32_t>(Disposition)));
    }

    // ⚠️ And no two verdicts read the same, or the readout cannot distinguish them.
    for (std::uint32_t Left = 0u; Left < 6u; ++Left)
        for (std::uint32_t Right = Left + 1u; Right < 6u; ++Right)
            ClaimNamed(std::strcmp(ConstraintDispositionNaming(Every[Left]),
                                   ConstraintDispositionNaming(Every[Right])) != 0,
                       std::string("verdicts read differently: ") + std::to_string(Left)
                       + " and " + std::to_string(Right));
}

//========================================================================================================
// 5. A BADGE HANGS ON THE GEOMETRY IT DESCRIBES
//========================================================================================================

void ProveTheBadgeHangsOnTheGeometry()
{
    std::printf("\n5. A badge hangs on what it describes, and vanishes with it\n");

    SketchStructure Sketch;
    Sketch.DeclarePlane({ { 0.0, 0.0, 0.0 }, { 0.0, 1.0, 0.0 }, { 1.0, 0.0, 0.0 } });
    const SketchCurveName Line = Sketch.DeclareLine({ 0.0, 0.0, 0.0 }, { 40.0, 0.0, 0.0 });

    ReferenceSpecification ToCurve = {};
    ToCurve.Subject     = ReferenceSubject::SketchCurve;
    ToCurve.SketchCurve = Line;

    SpatialPoint Anchor = {};
    Claim(ResolveConstraintAnchor(Sketch, ToCurve, Anchor), "a curve's badge finds an anchor");

    // 📝 The MIDDLE of the curve, so two badges on curves sharing an endpoint do not stack.
    Claim(std::fabs(Anchor.Left - 20.0) < 1.0,
          "...at the middle of the curve rather than at an end");
    Claim(std::fabs(Anchor.Up) < 1.0e-9 && std::fabs(Anchor.Forward) < 1.0e-9,
          "...and on the curve itself");

    // ⚠️ A reference to geometry the sketch does not hold must refuse, not answer the origin.
    ReferenceSpecification Missing = {};
    Missing.Subject     = ReferenceSubject::SketchCurve;
    Missing.SketchCurve = Curve(9999u);

    SpatialPoint Nowhere = { 7.0, 7.0, 7.0 };
    Claim(!ResolveConstraintAnchor(Sketch, Missing, Nowhere),
          "a badge for geometry that is gone finds nothing");
    Claim(std::fabs(Nowhere.Left - 7.0) < 1.0e-9,
          "...and does not write an answer it did not find");

    ReferenceSpecification Unassigned = {};
    Unassigned.Subject = ReferenceSubject::SketchCurve;
    Claim(!ResolveConstraintAnchor(Sketch, Unassigned, Nowhere),
          "an unassigned reference finds nothing");

    // A second curve is badged at its own middle, not the first one's.
    const SketchCurveName Second = Sketch.DeclareLine({ 0.0, 0.0, 60.0 }, { 40.0, 0.0, 60.0 });
    ReferenceSpecification ToSecond = {};
    ToSecond.Subject     = ReferenceSubject::SketchCurve;
    ToSecond.SketchCurve = Second;

    SpatialPoint Other = {};
    Claim(ResolveConstraintAnchor(Sketch, ToSecond, Other), "a second curve finds its own anchor");
    Claim(std::fabs(Other.Forward - 60.0) < 1.0,
          "...on itself, not on the first curve");
}

}   // namespace

int main()
{
    std::printf("=========================================================================\n");
    std::printf("CONSTRAINT AUTHORING PROOF\n");
    std::printf("=========================================================================\n");

    ProveEveryRelationshipIsAnswered();
    ProveTheDemandIsHonoured();
    ProveWhatIsRefused();
    ProveEveryVerdictIsNamed();
    ProveTheBadgeHangsOnTheGeometry();

    std::printf("\n=========================================================================\n");
    std::printf("%u claims, %u failures -> %s\n", Claims, Failures, Failures == 0u ? "PROVEN" : "REFUTED");
    std::printf("=========================================================================\n");
    return Failures == 0u ? 0 : 1;
}
