//============================================================================================================================================
//                                               WORLDSKETCHANNOTATIONPRIORITY.H
//============================================================================================================================================
// 🧩 Who wins when a typed dimension and a standing constraint cannot both be true: the dimension does,
//    and the constraints that contradict it are withdrawn.
//
// 🔴 THE DIMENSION IS THE ARTIST SAYING A NUMBER OUT LOUD. A constraint is usually something they asked
//    for once and stopped thinking about. When the two disagree -- two lines held Equal, and one of them
//    typed to 65 while the other is 55 -- refusing the edit is the wrong answer twice over: the artist is
//    told "no" by a drawing that will not say which of its own past rules is in the way, and the only
//    route forward is to hunt down the constraint by hand. So the number wins and the contradicting
//    constraint is retired.
//
// 🔴 WHICH CONSTRAINT CONTRADICTS IS DECIDED BY EXPERIMENT, NOT BY A TABLE. There is no list here of
//    "Equal fights length, Parallel fights angle". Each standing constraint is applied, on its own, to a
//    sketch where the dimension already holds, and if the dimension stops holding then THAT constraint is
//    the one in the way. A hand-written table of conflicting pairs is wrong the day a subject is added to
//    either enum, and wrong silently -- it simply stops noticing a conflict. The experiment cannot fall
//    out of date because it asks the solvers themselves.
//
// 🔴 ONLY WHAT ACTUALLY FIGHTS IS RETIRED. A Parallel between two lines does not care how long they are,
//    so typing a length must not disturb it. Retiring every constraint touching the dimensioned geometry
//    would be far simpler and would quietly dismantle the artist's model one edit at a time.
//
// 📝 Retiring, not erasing -- see `WorldConstraintSpecification::Retired`. A constraint's name is its
//    position, and those names are stored elsewhere.

#pragma once

#include "Foundation/DeliveryGuarantee.h"
#include "SlateShape/World/WorldSketchStructure/Api/WorldSketchStructure.h"

#include <cstdint>
#include <vector>

namespace Slate
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       THE OUTCOME
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 How many constraints one edit may withdraw before the result stops resembling what the artist drew.
/// note  📝 Not a performance bound. An edit that would retire more relations than this is far likelier
///        to be a mistake -- a wrong unit, a stray keystroke -- than a genuine intent to dismantle the
///        model, so it is refused whole and the artist is told.
constexpr std::uint32_t AnnotationPriorityRetirementLimit = 4u;

/// 🧩 What happened when a dimension was pushed through against the standing constraints.
struct AnnotationPriorityOutcome
{
    /// 🧩 Whether the dimension now holds.
    bool Applied = false;

    /// 🧩 The constraints withdrawn to make it hold, in the order they were found.
    std::vector<WorldConstraintName> Retired = {};

    /// 🧩 Set when the edit was abandoned because it would have retired too much.
    /// note  📝 Distinct from a plain refusal: the sketch COULD have taken the value, but the price was
    ///        higher than an edit should ever silently pay.
    bool RefusedAsTooCostly = false;
};

//------------------------------------------------------------------------------------------------------------------------
//                                                     THE ARBITRATION
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 The constraints that cannot hold at the same time as the given dimension, found by experiment.
/// out  Delivered  [-] the offending constraint names, empty when the dimension sits happily
/// note  🔴 Asks the solvers rather than reasoning about subjects. Each standing constraint is applied
///        alone to a sketch in which the dimension already holds; the ones that break it are the answer.
/// note  📝 Already-retired constraints are skipped -- they enforce nothing, so they cannot conflict.
/// cost  🚩🚩
/// tag   api, nonthrowing
Deliver<bool> ResolveContradictingConstraints(const WorldSketchStructure& Declared,
                                              WorldDimensionName Subject,
                                              double Target,
                                              std::vector<WorldConstraintName>& Delivered);

/// 🧩 Sets a dimension to a value, withdrawing whatever constraints stand in the way.
/// in    Target    [mm] the value the artist typed
/// out   Outcome   [-]  what held, and what was given up for it
/// note  🔴 THE SKETCH IS ONLY WRITTEN ON SUCCESS. Everything happens on a copy, so an edit that cannot
///        be made leaves the drawing exactly as it was rather than half-solved against a value it never
///        accepted -- the state an artist cannot undo their way out of.
/// note  ⚠️ Refuses without retiring anything when the dimension cannot be met even with every
///        contradicting constraint gone. The geometry itself is the obstacle then, and dismantling
///        constraints to chase an impossible number would destroy the model for nothing.
/// cost  🚩🚩
/// tag   api, nonthrowing
Deliver<bool> ApplyDimensionOverConstraints(WorldSketchStructure& Declared,
                                            WorldDimensionName Subject,
                                            double Target,
                                            AnnotationPriorityOutcome& Outcome);

} // namespace Slate
