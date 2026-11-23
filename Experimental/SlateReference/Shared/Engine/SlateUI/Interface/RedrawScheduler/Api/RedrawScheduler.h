//============================================================================================================================================
//                                                             REDRAWSCHEDULER.H
//============================================================================================================================================
// 🧩 Three marks of rising cost, and the one question a tick asks before it presents anything at all.

#pragma once

#include "Foundation/DeliveryGuarantee.h"

#include <cstdint>

namespace Slate
{

//------------------------------------------------------------------------------------------------------------------------
//                                                        THE MARKS
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 What must be done to one panel before it may be presented again.
/// note  🔴 Ordered by cost, and the ordering is load-bearing: two marks incoming on one panel within one
///       tick resolve to the dearer by numeric comparison alone. Reordering these members reorders the
///       resolution, and the defect appears as a panel that recolours where it should have re-solved.
/// note  The three are separated because they cost three different amounts. Conflating them means paying
///       the dearest one every time something hovers, which is the whole expense the mechanism removes.
/// tag   guarantee
enum class RedrawMark : std::uint32_t
{
    Quiet     = 0u,   // [-] - nothing changed; the recorded content of the previous tick still stands
    Recolour  = 1u,   // [-] - rewrite colours only — a hover fade, an appearance fade, a selection moving
    Rerecord  = 2u,   // [-] - re-record this panel — text changed, an arrangement toggled, a subject set changed
    Rearrange = 3u,   // [-] - re-solve extents, then record — a resize, a breakpoint crossed, a disclosure opening
    MarkCount = 4u    // [-] - the closed count, never a mark
};

/// 🧩 The dearer of two marks.
/// cost  ✔️
constexpr RedrawMark Dearer(RedrawMark Current, RedrawMark Incoming)
{
    return (static_cast<std::uint32_t>(Incoming) > static_cast<std::uint32_t>(Current)) ? Incoming : Current;
}

/// 🧩 Static text naming one mark, for the diagnostic overlay and for nothing the artist reads.
/// cost  ✔️
constexpr const char* MarkText(RedrawMark Declared)
{
    switch (Declared)
    {
        case RedrawMark::Quiet:     return "quiet";
        case RedrawMark::Recolour:  return "recolour";
        case RedrawMark::Rerecord:  return "rerecord";
        case RedrawMark::Rearrange: return "rearrange";
        default:                    return "";
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                      THE ENROLMENT
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 Which panels stand marked, and whether the host may block instead of presenting.
/// note  🔴 ⏱️ The wake rule is the point of this component, not the marks. Under FIFO pacing
///        `DisplayScheduler` presents sixty identical images a second forever unless something declines to
///        rotate, and a UI that re-presents an unchanged image is not idle no matter how little it
///        recomputed. `Waking` is the only sanctioned reader of that question.
/// note  ⚠️ Marks do **not** propagate. A subject card marking itself never re-solves the workspace, and a
///        drawer re-solving never re-records a card whose extents did not move. The one exception is stated
///        by the caller through `MarkEvery`, which is what an appearance change and a display resize use.
/// tag   owning
class RedrawScheduler
{
public:

    static constexpr std::uint32_t PanelCapacity = 64u;   // [-] - registered panels, never allocated

    RedrawScheduler()                                  = default;
    RedrawScheduler(const RedrawScheduler&)            = delete;
    RedrawScheduler& operator=(const RedrawScheduler&) = delete;
    ~RedrawScheduler()                                 = default;

    /// 🧩 Registers one panel and delivers the ordinal it is marked by thereafter.
    /// in    Naming   [-]  static text naming the panel; presented by the diagnostic overlay only
    /// out   Result  [-]  refuses with ExtentExhausted when the capacity is full
    /// post  the registered panel stands at Rearrange — nothing has ever been solved for it
    /// note  🔴 A panel arrives marked at the dearest mark rather than quiet. The alternative is a panel
    ///        that registers during a quiet tick and is never recorded at all, which reads as a panel that
    ///        failed to open rather than as a mark that was never raised.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    Deliver<std::uint32_t> Register(const char* Naming);

    /// 🧩 Raises one panel's mark to the dearer of what it carries and what is declared.
    /// in    PanelIndex  [-]  what Register delivered; an unenrolled ordinal marks nothing
    /// note  Never lowers. A panel that already stands at Rearrange is not reduced to Recolour by a hover
    ///       incoming in the same tick, which is the ordering the enumeration's numbering encodes.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    void Mark(std::uint32_t PanelIndex, RedrawMark Declared);

    /// 🧩 Raises every registered panel's mark — a display resize, or an appearance resolved afresh.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    void MarkEvery(RedrawMark Declared);

    /// 🧩 The mark one panel stands at now.
    /// out   RedrawMark  [-]  Quiet for an unenrolled ordinal
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    RedrawMark Current(std::uint32_t PanelIndex) const;

    /// 🧩 Whether anything at all stands above Quiet.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    bool Marked() const;

    /// 🧩 The wake rule, evaluated once per tick, before anything is recorded.
    /// in    AnythingMoving  [-]  what `MotionIntegrator::Advance` returned for this tick
    /// in    ArrivalHeld     [-]  whether any device sample arrived since the previous tick
    /// out   Waking          [-]  false means the host blocks in the window system and presents nothing
    /// note  🔴 ⏱️ All three operands are required. Marks alone miss a settling spring; motion alone misses
    ///        a text change; input alone misses both. A rule missing one of the three produces a panel that
    ///        freezes mid-transition and resumes when the artist happens to move the pointer, which is
    ///        attributed to the device rather than to the rule.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    bool Waking(bool AnythingMoving, bool ArrivalHeld) const;

    /// 🧩 Returns every panel to Quiet. Called once, after the tick's content has been sealed.
    /// note  ⚠️ Retiring before the content is sealed loses the tick's own marks, and the panel is recorded
    ///        one tick later than the change that demanded it — visible as a hover that lags by one image.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    void Retire();

    /// 🧩 Returns one panel to Quiet, for a caller that records panels independently.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    void Retire(std::uint32_t PanelIndex);

    /// 🧩 How many panels are registered.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    std::uint32_t RegisteredCount() const;

    /// 🧩 The static text one registered panel was named with.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    const char* Naming(std::uint32_t PanelIndex) const;

    /// 🧩 How many ticks have retired with nothing marked — the figure the idle gate is measured by.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    std::uint64_t QuietTicks() const;

    /// 🧩 How many ticks have retired with something marked.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    std::uint64_t RecordedTicks() const;

private:

    RedrawMark     Marks[PanelCapacity]   = {};   // [-] - one per registered panel, indexed by ordinal
    const char*    Namings[PanelCapacity] = {};   // [-] - static text; never allocated
    std::uint32_t  Registered               = 0u;   // [-]
    std::uint64_t  QuietCount             = 0u;   // [-] - ticks retired with nothing marked
    std::uint64_t  RecordedCount          = 0u;   // [-] - ticks retired with something marked
};

}   // namespace Slate
