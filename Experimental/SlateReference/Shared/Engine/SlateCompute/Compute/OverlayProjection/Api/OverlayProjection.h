//============================================================================================================================================
//                                                         OVERLAYPROJECTION.H
//============================================================================================================================================
// 🧩 `80` — non-owner geometry the artist needs to see, as two recordings whose depth behaviour is opposite and never merged.

#pragma once

#include "Foundation/DeliveryGuarantee.h"
#include "Foundation/PrecisionGuarantee.h"
#include "SlateDocument/Document/ToolSequence/Api/ToolSequence.h"
#include "SlateMath/Numeric/ColourProjection/Api/ColourProjection.h"
#include "SlateMath/Numeric/ReportSequence/Api/ReportSequence.h"
#include "SlateVulkan/Device/RenderSchedule/Api/RenderSchedule.h"

#include <cstddef>
#include <cstdint>

namespace Slate
{

//------------------------------------------------------------------------------------------------------------------------
//                                                  THE DEPTH BEHAVIOUR
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 Which of `08` §3.2's two recordings one overlay belongs to.
/// note  🔴 `80` §1: depth testing is **recording state** and not a per-primitive decision, so the membership is a
///        property of the overlay and never a parameter beside it. A ground lattice that ignores depth is drawn over
///        the object standing on it; a manipulator that respects depth disappears inside the object it manipulates.
///        Both behaviours are correct and they are opposite, which is exactly why one recording cannot carry both.
/// tag   guarantee
enum class DepthSubject : std::uint32_t
{
    DepthTested = 0u,   // [-] - `08` §3 ⑩ — occludes and is occluded, and amends the depth it tested against
    DepthFree   = 1u,   // [-] - `08` §3 ⑪ — drawn over everything and amends no depth at all
    DepthCount  = 2u    // [-] - the closed count, never a behaviour
};

// 📐 `80` §1 and §3's table, as a computation rather than as a comment. Everything the artist reaches through the
//    workspace is depth-tested; only what they reach through directly — the manipulator and the pivot it turns about
//    — stands free of it.
constexpr DepthSubject DepthOfOverlay(OverlaySubject Current)
{
    return Current == OverlaySubject::Manipulator || Current == OverlaySubject::Pivot
         ? DepthSubject::DepthFree
         : DepthSubject::DepthTested;
}

// 🔴 `80` §3's table is fixed here at compile time rather than reviewed. An overlay that migrates between the two
//    recordings does so by editing this line and failing these assertions, and not by a caller passing the other
//    behaviour on a rotation where it looked better.
static_assert(DepthOfOverlay(OverlaySubject::GroundLattice)     == DepthSubject::DepthTested, "`80` §3: the ground lattice is depth-tested");
static_assert(DepthOfOverlay(OverlaySubject::Guide)             == DepthSubject::DepthTested, "`80` §3: guides are depth-tested");
static_assert(DepthOfOverlay(OverlaySubject::Wireframe)         == DepthSubject::DepthTested, "`80` §3: the wireframe is depth-tested");
static_assert(DepthOfOverlay(OverlaySubject::SeamDisplay)       == DepthSubject::DepthTested, "`80` §3: the seam display is depth-tested");
static_assert(DepthOfOverlay(OverlaySubject::SurfaceAnnotation) == DepthSubject::DepthTested, "`80` §3: surface annotation is depth-tested");
static_assert(DepthOfOverlay(OverlaySubject::Manipulator)       == DepthSubject::DepthFree,   "`80` §3: the manipulator is depth-free");
static_assert(DepthOfOverlay(OverlaySubject::Pivot)             == DepthSubject::DepthFree,   "`80` §3: the pivot is depth-free");

//------------------------------------------------------------------------------------------------------------------------
//                                                     ONE OVERLAY
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 How one overlay is drawn — its colour, its extent in display pixels, and what clears a coplanar comparison.
/// note  🔴 `80` §2: the colour is a display code value, chosen for contrast **against display values**, and is never
///        passed through exposure or a tone map. An overlay written scene-referred converges to white with the scene
///        as the exposure rises, and a wireframe that disappears when the artist brightens the workspace is a
///        wireframe that fails precisely while it is being used to see something.
/// note  📐 The offset exists because the wireframe and the seam display lie **exactly on** the surfaces they trace.
///        Compared without it the two ordinates are the same coordinate, and the overlay reads as stippled along every
///        face it follows rather than as a line.
/// tag   nonallocating, nonthrowing
struct OverlaySpecification
{
    ColourSpecification  OverlayColour = {};    // [-]  - the display-space code value the overlay is drawn in
    double               LineExtent    = 1.0;   // [px] - in display pixels, so the overlay does not thin with distance
    double               DepthOffset   = 0.0;   // [-]  - reversed-depth offset toward the camera; nought for what is not coplanar
};

//------------------------------------------------------------------------------------------------------------------------
//                                                   THE PROJECTION
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 `80` — the two recordings, what each contains, and the presence read out of `76` rather than held here.
/// note  🔴 `80` §4: neither recording writes `VisibilityIndex`, `OccupancySurface` or `MotionSurface`. `16` §6 gates
///        this from the other side and the consequences of breaking it are three separate defects at once — an
///        overlay in the visibility index would be picked by `74`, outlined by `26`, and shaded by `18`.
/// note  🔴 Presence is **read** from `76` and never held here — `80` §3's closing line and §5's fifth gate. A second
///        copy of what is shown disagrees with the toggle the artist just used, and the overlay then appears in one
///        of the two recordings and not the other.
/// note  🔴 No overlay is intersected by `74`. Overlay intersection, where it exists at all, belongs to whoever owns
///        the overlay — the manipulator's handles to `78` §4, a guide to the tool that placed it.
/// tag   owning
class OverlayProjection
{
public:

    // 📝 `08` §3 ⑩ and ⑪, both above `26`'s 60. Everything ordered here is display-referred by construction, and
    //    the two ordinals are apart so that a depth-tested overlay can never be recorded after a depth-free one.
    static constexpr std::uint32_t DepthTestedIndex = 70u;   // [-] - `08` §3 ⑩
    static constexpr std::uint32_t DepthFreeIndex   = 80u;   // [-] - `08` §3 ⑪

    /// 🧩 Declares how one overlay is drawn.
    /// in    Current  [-]  which of `80` §3's seven overlays
    /// in    Declaring  [-]  its display-space colour, its display-pixel extent, and its depth offset
    /// out   Result    [-]  refuses with ContentUnsupported for the closed count, an undeclared colour, a colour
    ///                       that is not a coordinate in the display space, an extent of nothing, and a depth offset
    ///                       declared on an overlay whose recording tests no depth
    /// post  the overlay is declared and its recording draws it wherever `76` says it is present
    /// note  🔴 A working-referred colour is rejected rather than accepted. Both recordings run after `66` and nothing
    ///        between here and the display surface compresses, so such a colour would be presented as display code
    ///        without ever crossing `36` — an overlay in a plausible but wrong hue rather than a visible mistake.
    /// note  🔴 An offset declared on a depth-free overlay is rejected rather than ignored. Silently ignored it reads
    ///        as an offset that had no effect, and the caller then raises it until something else breaks.
    /// cost  ✔️
    /// tag   api, nonthrowing
    Deliver<bool> Declare(OverlaySubject Current, const OverlaySpecification& Declaring);

    /// 🧩 Contributes both of `08` §3's overlay recordings — ⑩ depth-tested, ⑪ depth-free.
    /// in    Schedule  [-]  where the two declarations land
    /// out   Result   [-]  refuses with whatever the schedule rejected, and with ContentUnsupported before any
    ///                      overlay has been declared
    /// note  🔴 **Two** declarations, never one. `08` §3.2 and `80` §5's first gate: a single recording carrying both
    ///        behaviours would have to switch depth state per primitive, which is the merge both documents refuse.
    /// note  🔴 ⑩ amends `DepthSurface` as well as `DisplaySurface` — `08` §2 — because depth-tested overlays occlude
    ///        one another. ⑪ amends neither depth nor anything else beyond the display.
    /// note  🔴 Both are declared display-referred. Declared scene-referred they would be ordered among `66`'s own
    ///        inputs and compressed with the radiance, which is §2's failure exactly.
    /// cost  ✔️
    /// tag   api, nonthrowing
    Deliver<bool> Contribute(RenderSchedule& Schedule) const;

    /// 🧩 Whether one overlay is presented this rotation, as `76` holds it.
    /// in    Tooling    [-]  `76`, the one owner of overlay presence
    /// in    Current  [-]  which overlay
    /// out   Current   [-]  false for an overlay that was never declared, whatever `76` says about it
    /// note  🔴 A straight read of `76` §1's row, with no copy kept. `80` §5's fifth gate is that presence is never
    ///        held here, and this is the whole of the compliance — an overlay `76` has switched off is not drawn on
    ///        the same rotation the artist switched it off on.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    bool OverlayActive(const ToolSequence& Tooling, OverlaySubject Current) const;

    /// 🧩 Whether either recording has anything to draw this rotation.
    /// in    Tooling    [-]  `76`
    /// in    Behaviour  [-]  which of the two recordings
    /// out   Occupied   [-]  true where at least one declared overlay of that behaviour is present
    /// note  📝 Answered rather than assumed so a rotation with every overlay switched off submits no geometry. The
    ///        recordings are still both contributed — the schedule's shape is fixed at bring-up and does not vary
    ///        with a toggle, or `08`'s ordering would differ between two rotations of the same document.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    bool RecordingOccupied(const ToolSequence& Tooling, DepthSubject Behaviour) const;

    /// 🧩 How one declared overlay is drawn.
    /// in    Current  [-]  which overlay
    /// out   Result    [-]  refuses with ContentUnsupported for the closed count and for an overlay never declared
    /// cost  ✔️
    /// tag   api, nonthrowing
    Deliver<const OverlaySpecification*> Specification(OverlaySubject Current) const;

    /// 🧩 Declares how many overlays each recording drew; appends nothing.
    /// in    Tooling   [-]  `76`, read for presence
    /// in    Measured  [-]  where the two counts land
    /// in    Sampled   [ns] the tick's own reading
    /// note  🔴 `80` appears in no row of `86` §4's register. An overlay that is drawn is the component working, and
    ///        a report each rotation would mean the register is never quiet — `86` §2's own rule.
    /// cost  🚩
    /// tag   api, nonthrowing
    void Report(const ToolSequence& Tooling, MeasureIndex& Measured, TickPoint Sampled) const;

private:

    static constexpr std::size_t OverlaySpan = static_cast<std::size_t>(OverlaySubject::OverlayCount);

    OverlaySpecification  Declared[OverlaySpan]            = {};      // [-] - as Declare validated each of them
    bool                  DeclarationCurrent[OverlaySpan] = {};      // [-] - false until that overlay was declared
    bool                  OverlayDeclared                  = false;   // [-] - false until any overlay was declared
};

// 📐 The depth comparison and the recording membership are Exact; the line coverage across the declared extent is
//    Bounded. `DisplaySurface` is Tier D and both amendments land on it, so the component claims Perceptual —
//    `00` §3's transitivity rule folds to the weakest.
SLATE_DECLARES_PRECISION(PrecisionGuarantee::Perceptual,
                         PrecisionGuarantee::Perceptual,
                         PrecisionGuarantee::Bounded,
                         PrecisionGuarantee::Exact);

}   // namespace Slate
