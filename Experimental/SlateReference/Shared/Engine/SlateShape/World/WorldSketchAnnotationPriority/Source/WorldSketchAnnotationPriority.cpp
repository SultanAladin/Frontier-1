//============================================================================================================================================
//                                              WORLDSKETCHANNOTATIONPRIORITY.CPP
//============================================================================================================================================

#include "SlateShape/World/WorldSketchAnnotationPriority/Api/WorldSketchAnnotationPriority.h"

#include "SlateShape/World/WorldSketchConstraintSolver/Api/WorldSketchConstraintSolver.h"
#include "SlateShape/World/WorldSketchDimensionSolver/Api/WorldSketchDimensionSolver.h"

#include <cmath>

namespace Slate
{

namespace
{

/// 🧩 How far a dimension may drift from its target and still count as holding, in millimetres.
/// note  📝 Matches the tolerance `ResolveWorldDimensionConflict` already judges by, so "the dimension
///        holds" means one thing across the whole annotation path rather than two nearly-equal things.
constexpr double PriorityHoldTolerance = 1.0e-4;

/// 🧩 Whether the dimension reads its target in this sketch.
bool DimensionHolds(const WorldSketchStructure& Declared, WorldDimensionName Subject, double Target)
{
    const Deliver<double> Measured = ResolveWorldDimensionValue(Declared, Subject);
    if (!Measured.Resolved)
        return false;
    return std::fabs(Measured.Delivered - Target) <= PriorityHoldTolerance;
}

/// 🧩 Whether every dimension that held in the reference drawing still holds here.
/// note  🔴 ALL OF THEM, NOT JUST THE ONE BEING EDITED, and that distinction is the whole difference
///        between this working and not. The constraint solvers are DIRECTIONAL: `Equal` drags its
///        secondary curve to match its primary, so typing a new length into the PRIMARY's dimension is
///        propagated happily and the edited dimension still reads what was typed. The contradiction
///        surfaces on the OTHER line -- whose own dimension is now being violated. Watching only the
///        edited dimension sees a clean drawing and retires nothing, which is exactly the bug this
///        function was written to avoid.
/// note  📝 A dimension already violated in the reference drawing is ignored, because a constraint
///        cannot be blamed for damage that was there before it was applied.
bool AllDimensionsHold(const WorldSketchStructure& Reference,
                       const WorldSketchStructure& Trial)
{
    for (std::uint32_t Index = 1u; Index <= Reference.DimensionCount(); ++Index)
    {
        const WorldDimensionSpecification& Dimension = Reference.Dimensions()[Index - 1u];
        if (!Dimension.Declared())
            continue;

        if (!DimensionHolds(Reference, { Index }, Dimension.Target))
            continue;

        if (!DimensionHolds(Trial, { Index }, Dimension.Target))
            return false;
    }
    return true;
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------

Deliver<bool> ResolveContradictingConstraints(const WorldSketchStructure& Declared,
                                              WorldDimensionName Subject,
                                              double Target,
                                              std::vector<WorldConstraintName>& Delivered)
{
    Delivered.clear();

    if (!Subject.Assigned() || Subject.IssuedIndex > Declared.DimensionCount())
        return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported,
                                       "no such world dimension is declared" });
    if (Target <= 0.0)
        return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported,
                                       "a dimension target must be positive" });

    // ── Put the dimension where the artist asked, with nothing else enforced ────────────────────────
    // 📝 This is the reference drawing: the shape as it would be if the number were the only rule. Each
    //    constraint is then tried against THIS, so what is measured is that constraint's own effect and
    //    not the accumulated drift of everything before it.
    WorldSketchStructure Ideal = Declared;
    WorldDimensionSpecification* Held = Ideal.Resolve(Subject);
    if (Held == nullptr)
        return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported,
                                       "the world dimension is absent" });
    Held->Target = Target;

    const Deliver<bool> Solved = ApplyWorldDimension(Ideal, Subject);
    if (!Solved.Resolved)
        return Deliver<bool>::Refuse(Solved.Error);

    // 🔴 THE EXPERIMENT. Each standing constraint is applied ALONE to the ideal drawing. If the
    //    dimension no longer reads its target afterwards, that constraint is what is fighting it.
    //    Nothing here knows that Equal fights length or that Parallel does not -- it finds out.
    for (std::uint32_t Index = 1u; Index <= Ideal.ConstraintCount(); ++Index)
    {
        const WorldConstraintSpecification& Standing = Ideal.Constraints()[Index - 1u];

        // 📝 A retired constraint enforces nothing, so it cannot be in the way.
        if (Standing.Retired || !Standing.Declared())
            continue;

        WorldSketchStructure Trial = Ideal;
        const Deliver<bool> Enforced = ApplyWorldConstraint(Trial, { Index });

        // ⚠️ A constraint that cannot even be applied to this geometry is not evidence of a conflict
        //    with the dimension -- it is broken on its own terms, and retiring it here would be
        //    blaming the dimension for damage it did not do.
        if (!Enforced.Resolved)
            continue;

        // 🔴 THE TEST IS AGAINST EVERY DIMENSION, for the reason set out on `AllDimensionsHold`. The
        //    artist's own case -- two lines held Equal, one typed to 65 while the other says 55 -- shows
        //    the conflict on the OTHER line's dimension, never on the one being edited.
        if (!AllDimensionsHold(Ideal, Trial))
            Delivered.push_back({ Index });
    }

    return Deliver<bool>::Result(true);
}

//------------------------------------------------------------------------------------------------------------------------

Deliver<bool> ApplyDimensionOverConstraints(WorldSketchStructure& Declared,
                                            WorldDimensionName Subject,
                                            double Target,
                                            AnnotationPriorityOutcome& Outcome)
{
    Outcome = {};

    std::vector<WorldConstraintName> Offending;
    const Deliver<bool> Found = ResolveContradictingConstraints(Declared, Subject, Target, Offending);
    if (!Found.Resolved)
        return Deliver<bool>::Refuse(Found.Error);

    // ⚠️ An edit that dismantles half the model is far likelier to be a slip than an intention.
    if (Offending.size() > AnnotationPriorityRetirementLimit)
    {
        Outcome.RefusedAsTooCostly = true;
        return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported,
                                       "the dimension would withdraw too many constraints to be safe" });
    }

    // 🔴 EVERYTHING ON A COPY UNTIL IT IS KNOWN TO WORK. A partly-applied edit is the one state the
    //    artist cannot reason about or undo cleanly, so the real sketch is not touched until the value
    //    is proven to hold against the constraints that remain.
    WorldSketchStructure Trial = Declared;

    for (const WorldConstraintName& Retiring : Offending)
    {
        WorldConstraintSpecification* Standing = Trial.Resolve(Retiring);
        if (Standing == nullptr)
            return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported,
                                           "a contradicting world constraint could not be resolved" });
        Standing->Retired = true;
    }

    WorldDimensionSpecification* Held = Trial.Resolve(Subject);
    if (Held == nullptr)
        return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported,
                                       "the world dimension is absent" });
    Held->Target = Target;

    const Deliver<bool> Solved = ApplyWorldDimension(Trial, Subject);
    if (!Solved.Resolved)
        return Deliver<bool>::Refuse(Solved.Error);

    // 📝 The surviving constraints are re-settled around the new value. They were all found NOT to
    //    fight it, so this tightens the drawing back up rather than undoing what was just asked for.
    const Deliver<bool> Resettled = ApplyWorldConstraints(Trial);
    if (!Resettled.Resolved)
        return Deliver<bool>::Refuse(Resettled.Error);

    // 🔴 THE LAST WORD IS THE MEASUREMENT, NOT THE SOLVER'S RETURN CODE. Every step above can report
    //    success and still leave the dimension off its target, because each constraint was cleared
    //    ALONE against the ideal drawing while `ApplyWorldConstraints` above re-applies all the
    //    survivors together, eight passes deep. Individually-innocent constraints interacting is not a
    //    case the per-constraint experiment can see.
    // ⚠️ HONEST NOTE: this guard is DEFENSIVE and no fixture in `Tools/AnnotationProof` currently
    //    reaches it -- deleting it leaves every claim passing. It is kept because the alternative to a
    //    guard that has not yet earned its keep is silently handing back a drawing that does not match
    //    the number the artist typed, which is the one outcome this whole unit exists to prevent. It
    //    should be deleted only alongside a proof that the interaction it guards against cannot happen.
    if (!DimensionHolds(Trial, Subject, Target))
        return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported,
                                       "the dimension does not hold even with the contradicting constraints withdrawn" });

    Declared = Trial;
    Outcome.Applied = true;
    Outcome.Retired = Offending;
    return Deliver<bool>::Result(true);
}

} // namespace Slate
