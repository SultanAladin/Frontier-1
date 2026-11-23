//============================================================================================================================================
//                                                          INTERSECTIONOUTLINE.H
//============================================================================================================================================
// 🧩 `26` — the registered set's silhouette, derived from `16`'s targets alone and recorded over the display after `66`.

#pragma once

#include "Foundation/DeliveryGuarantee.h"
#include "Foundation/PrecisionGuarantee.h"
#include "SlateCompute/Compute/VisibilityIndex/Api/VisibilityIndex.h"
#include "SlateDocument/Document/RegistrationIndex/Api/RegistrationIndex.h"
#include "SlateDocument/Document/MaterialSpecification/Api/MaterialSpecification.h"
#include "SlateMath/Numeric/ColourProjection/Api/ColourProjection.h"
#include "SlateMath/Numeric/ReportSequence/Api/ReportSequence.h"
#include "SlateVulkan/Device/RenderSchedule/Api/RenderSchedule.h"

#include <cstdint>

namespace Slate
{

//------------------------------------------------------------------------------------------------------------------------
//                                                     THE OUTLINE
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 What the outline is drawn as — its extent in display pixels, and the two renderings occlusion selects between.
/// note  🔴 `26` §4: the width is declared in **display pixels** and never in domain or world units. A width that
///        scales with distance makes a selected owner lose its outline as the artist backs away from it, which
///        reads as the selection having been dropped.
/// note  🔴 `26` §2: the occluded rendering is visually distinct and **not merely dimmer**. The distinctness is
///        carried by the dash extent as well as by the colour, so a document authored against a display where the
///        two colours read alike still shows which part of the selection stands behind something.
/// note  🔴 Both colours are `ColourSpecification` values in the display space. The recording is display-referred
///        and is never tone-mapped, so a colour incoming working-referred would be presented uncompressed — and
///        `36` §1's rule that no bare triple exists anywhere is what makes that discoverable rather than silent.
/// tag   nonallocating, nonthrowing
struct OutlineSpecification
{
    double               OutlineWidth       = 2.0;   // [px] - in display pixels; the coverage falls to nothing across it
    double               OccludedDashExtent = 4.0;   // [px] - the dash the occluded rendering breaks into; nought draws solid
    ColourSpecification  VisibleColour      = {};    // [-]  - what stands in front, in the display space
    ColourSpecification  OccludedColour     = {};    // [-]  - what stands behind, in the display space
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    THE PROJECTION
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 `26` — registration resolved through `42`, the silhouette derived across it, and `08` §3 ⑨'s amendment.
/// note  🔴 No geometry is submitted. The outline derives from `16`'s targets and from nothing else — a second
///        submission of the selected owner would rasterise against a depth the visibility already resolved,
///        and the two disagree at exactly the silhouette the outline is meant to trace.
/// note  🔴 The owner is **resolved** through `42` and never derived here. `16` §6's gate is that every
///        consumer reads that one resolution; a partition ordinal read as though it were an owner ordinal is
///        `00` §10's conflict 15, and it presents as one object outlining another.
/// note  🔴 Registration is answered by `12`'s interval comparison over the compressed runs, not by a member test
///        this component keeps beside it. A second registration structure disagrees with the document exactly while
///        the artist is changing the selection.
/// tag   owning
class IntersectionOutline
{
public:

    // 📝 `08` §3 ⑨, after `66`'s tone line at 50. Everything ordered here is display-referred by construction,
    //    and `80`'s two recordings take the ordinals above it.
    static constexpr std::uint32_t AmendmentIndex = 60u;   // [-] - `08` §3 ⑨

    /// 🧩 Declares the width and the two renderings as one admission.
    /// in    Outlining_  [-]  the display-pixel width, the dash extent, and the two display-space colours
    /// out   Result     [-]  refuses with ContentUnsupported for a width of nothing, a negative dash extent, an
    ///                        undeclared colour, a colour that is not a coordinate in the display space, and two
    ///                        renderings that differ in neither colour nor dash
    /// post  the specification stands and every consumer below reads it
    /// note  🔴 Two renderings that coincide are rejected rather than accepted. `26` §2 requires the occluded
    ///        outline to be visually distinct, and a specification that satisfies the gate in neither colour nor
    ///        dash has silently withdrawn the only thing that says which part of a selection is behind something.
    /// note  🔴 Rejected in full and never in part. A half-accepted specification outlines what stands in front in
    ///        a validated colour and what stands behind in one that was rejected.
    /// cost  ✔️
    /// tag   api, nonthrowing
    Deliver<bool> Declare(const OutlineSpecification& Outlining_);

    /// 🧩 Contributes `08` §3 ⑨'s recording — coverage into `OutlineSurface`, the outline over `DisplaySurface`.
    /// out   Result  [-]  refuses with whatever the schedule rejected, and with ContentUnsupported before Declare
    /// note  🔴 Declared **display-referred**, unlike `66`. The recording amends the display surface after the
    ///        tone line, so an outline colour is display code already and the compression must never reach it.
    ///        Declared scene-referred it would be ordered among `66`'s own inputs and compressed with them, and
    ///        the artist meets that as a selection outline whose colour changes with the exposure.
    /// note  🔴 Reads `16`'s visibility and the depth, produces the coverage, amends the display. It writes
    ///        neither `VisibilityIndex` nor `OccupancySurface` — the outline is not an owner and nothing
    ///        downstream may resolve a pixel to it.
    /// cost  ✔️
    /// tag   api, nonthrowing
    Deliver<bool> Contribute(RenderSchedule& Schedule) const;

    /// 🧩 Whether the owner behind one pixel is registered in the selection subset.
    /// in    Written      [-]  the word read back from `16`'s target
    /// in    Visibility   [-]  where the partition ordinal is resolved to a partition identity
    /// in    Resolutions  [-]  `42`'s resolution; the only place a partition becomes an owner
    /// in    Registrations  [-]  `12`'s compressed registration
    /// out   Result      [-]  refuses with whatever `16` rejected for an unoccupied pixel or a stale resolution
    /// note  🔴 Two indexed lookups and one interval comparison — no search anywhere on this line. It runs once
    ///        per pixel of the display extent, and a member test that walked a selection would make the outline
    ///        grow in expense with the size of the selection rather than with the size of the display.
    /// note  🔴 The comparison is Tier A and integer throughout: the ordinal, the identity and the registration
    ///        interval are all whole. A silhouette derived from a real-valued comparison flickers along its own
    ///        edge between rotations that resolved the same geometry.
    /// cost  ✔️
    /// tag   api, nonthrowing
    Deliver<bool> ClassifyRegistration(VisibilityWord                  Written,
                                    const VisibilityIndex&          Visibility,
                                    const PartitionResolutionIndex& Resolutions,
                                    const RegistrationIndex&          Registrations) const;

    /// 🧩 The coverage one pixel carries, from how far the nearest disagreeing neighbour stands.
    /// in    DivergenceExtent  [px] to the nearest neighbour of the opposite registration, in display pixels
    /// out   Coverage          [-]  unity at the boundary itself, falling to nothing across the declared width
    /// note  🔴 `26` §4: the coverage is written as a **scalar** and never as a binary decision. A binary
    ///        coverage is a one-pixel staircase along every silhouette, and no amount of care downstream
    ///        recovers the edge the decision discarded.
    /// note  📐 Linear in the departure rather than a step, and measured against the declared width so that a
    ///        wider outline fades over a longer run rather than merely covering more pixels solidly.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    double ProjectCoverage(double DivergenceExtent) const;

    /// 🧩 Whether the registered surface at one pixel stands behind what the depth recorded.
    /// in    OutlineDepth   [-]  the registered owner's own coordinate, reversed — near is one
    /// in    RecordedDepth  [-]  the nearest coordinate `DepthSurface` recorded there, reversed
    /// out   Occluded       [-]  true only where something nearer than the registered surface stands
    /// note  📐 Reversed depth throughout, as `60` §3 uses it: the outline is occluded when its own coordinate is
    ///        **below** the recorded one. Written the other way round every selection reads as fully occluded,
    ///        and the artist meets that as the outline having changed colour for no reason.
    /// note  📝 No slope-scaled offset here, unlike `OcclusionProjection`. Both ordinates are read at the same
    ///        display pixel from the same projection, so there is no grazing self-comparison to clear.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    bool ClassifyOcclusion(double OutlineDepth, double RecordedDepth) const;

    /// 🧩 Whether the occluded rendering draws at one display position, given the declared dash extent.
    /// in    XCoordinate   [px] the display position, in display pixels
    /// in    YCoordinate  [px]
    /// out   DashCurrent    [-]  always true where the dash extent is nought — the occluded outline draws solid
    /// note  📐 The dash runs on the sum of the two ordinates rather than on either alone, so it breaks a
    ///        horizontal silhouette and a vertical one identically. Run on one coordinate it vanishes entirely
    ///        along every edge parallel to it, which reads as the outline being missing rather than dashed.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    bool DashCurrent(double XCoordinate, double YCoordinate) const;

    /// 🧩 The colour one outline pixel is recorded in.
    /// in    Occluded  [-]  as ClassifyOcclusion answered it
    /// out   Result   [-]  refuses with ContentUnsupported before Declare
    /// note  🔴 Delivered in the display space and recorded as it stands. Nothing between here and the display
    ///        surface tone-maps, reflects or accumulates it — `26` §6's gate, and the whole reason the recording
    ///        is ordered after `66`.
    /// cost  ✔️
    /// tag   api, nonthrowing
    Deliver<ColourSpecification> OutlineColour(bool Occluded) const;

    /// 🧩 Declares every measure; appends nothing.
    /// note  🔴 `26` appears in no row of `86` §4's register. A selection that is outlined is the component
    ///        working, and reporting each rotation would mean the register is never quiet.
    /// cost  🚩
    /// tag   api, nonthrowing
    void Report(MeasureIndex& Measured, TickPoint Sampled) const;

    const OutlineSpecification& Outline() const;

private:

    OutlineSpecification  Outlining          = {};      // [-] - as Declare validated it
    bool                  OutlineCurrent    = false;   // [-] - false until one declaration was accepted
};

// 📐 The partition ordinal, the identity comparison and the registration interval are Exact; the coverage across the
//    declared width is Bounded. `DisplaySurface` is Tier D and the amendment lands on it, so the component claims
//    Perceptual — `00` §3's transitivity rule folds to the weakest.
SLATE_DECLARES_PRECISION(PrecisionGuarantee::Perceptual,
                         PrecisionGuarantee::Perceptual,
                         PrecisionGuarantee::Bounded,
                         PrecisionGuarantee::Exact);

}   // namespace Slate
