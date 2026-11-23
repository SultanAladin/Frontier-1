//============================================================================================================================================
//                                                      CONTAINMENTCLASSIFIER.SLANG.H
//============================================================================================================================================
// 🧩 Interval containment over the gapped labels `12` issues — one comparison, asked per owner per rotation.

#pragma once

#include "Shared/ToolchainInterchange.slang.h"

// 📐 `12` §2.1 answers "is A enclosed by B, at any depth" by comparing two interval labels rather than by walking
//    a relation, and `12` §3 compresses every subset into runs of ordinals for the same reason. Both reduce to
//    the three comparisons below, and both are asked by `16` and `26` **per owner per rotation**, on the
//    device, against the same labels the host registered.
//
// 🔴 That is why the predicate is here rather than inside `SceneStructure`. A containment test written twice —
//    once for the outliner and once for the shading dispatch — is two tests that must agree about strictness at
//    every boundary, and the rotation on which they stop agreeing is the one where a selection outlines the
//    wrong enclosure while the shading outlines the right one.

namespace Slate
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    ONE ORDINAL
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 Classifies one ordinal against a closed interval.
/// in    IntervalBegin  [-]  first ordinal of the interval
/// in    IntervalEnd    [-]  last ordinal of it
/// in    Index        [-]  the ordinal being classified
/// out   Containment    [-]  +1 strictly inside, 0 on a bound, −1 outside
/// note  🔴 A bound resolves to zero and not to inside. `12` §5's invariant 4 requires labels to nest strictly,
///        so an owner sitting exactly on its enclosure's bound is a label that was registered wrongly rather than
///        an owner that is enclosed — and reporting it as enclosed hides the defect that produced it.
/// cost  ✔️
/// note  Exact — an integer comparison; identical on the host and on the device by construction.
/// tag   shared, parity, nonallocating, nonthrowing
SLATE_SHARED SLATE_CONSTEXPR Signed32 ClassifyIndexContainment(Unsigned64 IntervalBegin,
                                                                 Unsigned64 IntervalEnd,
                                                                 Unsigned64 Index)
{
    return IntervalEnd < IntervalBegin ? -1
         : Index == IntervalBegin || Index == IntervalEnd ? 0
         : Index > IntervalBegin && Index < IntervalEnd ? 1
         : -1;
}

/// 🧩 Whether one ordinal is registered in a closed run.
/// out   Registered  [-]  true on a bound as well as strictly inside
/// note  ⚠️ Inclusive where the predicate above is strict, because `12` §3's registration runs are inclusive at
///        both ends while its interval labels nest strictly. Both spellings exist so that neither consumer has
///        to remember to adjust the other's answer by one.
/// cost  ✔️
/// tag   shared, parity, nonallocating, nonthrowing
SLATE_SHARED SLATE_CONSTEXPR bool IndexRegistered(Unsigned64 RunBegin, Unsigned64 RunEnd, Unsigned64 Index)
{
    return RunEnd >= RunBegin && Index >= RunBegin && Index <= RunEnd;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                   ONE INTERVAL
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 Classifies one interval against another.
/// in    OuterBegin   [-]  the candidate enclosing interval
/// in    OuterEnd     [-]
/// in    InnerBegin   [-]  the candidate enclosed interval
/// in    InnerEnd     [-]
/// out   Containment  [-]  +1 the outer strictly contains the inner, 0 they are identical, −1 otherwise
/// note  🔴 An interval never strictly contains itself, which is what makes `12` §2.1's predicate answer false
///        for an owner against itself without any consumer having to exclude it. Overlapping and inverted
///        intervals both resolve to −1: neither is containment, and `12` invariant 4 forbids the first outright.
/// cost  ✔️
/// note  Exact — integer comparisons throughout.
/// tag   shared, parity, nonallocating, nonthrowing
SLATE_SHARED SLATE_CONSTEXPR Signed32 ClassifyIntervalContainment(Unsigned64 OuterBegin, Unsigned64 OuterEnd,
                                                                  Unsigned64 InnerBegin, Unsigned64 InnerEnd)
{
    return OuterEnd < OuterBegin || InnerEnd < InnerBegin ? -1
         : OuterBegin == InnerBegin && OuterEnd == InnerEnd ? 0
         : OuterBegin < InnerBegin && InnerEnd < OuterEnd ? 1
         : -1;
}

/// 🧩 Whether two intervals share no ordinal at all.
/// out   Disjoint  [-]  true when the two are wholly apart
/// note  🔍 `12` invariant 4 requires disjoint enclosures never to overlap, and this is the shape that check
///        takes. Over one ordering it holds exactly when each label begins after the one before it ended, which
///        is why `SceneStructure` walks the ordering rather than comparing every pair.
/// cost  ✔️
/// tag   shared, parity, nonallocating, nonthrowing
SLATE_SHARED SLATE_CONSTEXPR bool IntervalsDisjoint(Unsigned64 FirstBegin, Unsigned64 FirstEnd,
                                                    Unsigned64 LaterBegin, Unsigned64 LaterEnd)
{
    return FirstEnd < LaterBegin || LaterEnd < FirstBegin;
}

}   // namespace Slate
