//============================================================================================================================================
//                                                           CONTROLINDEX.H
//============================================================================================================================================
// 🧩 A generational slot index of live control interaction — the one grab, the one open popup, and every fade.

#pragma once

#include "Foundation/DeliveryGuarantee.h"
#include "Foundation/Identity.h"
#include "SlateUI/Interface/AppearanceSpecification/Api/AppearanceSpecification.h"
#include "SlateUI/Interface/InterfaceExchange/Api/RecordingSurface.h"
#include "SlateUI/Interface/MotionIntegrator/Api/MotionIntegrator.h"
#include "SlateUI/Interface/RedrawScheduler/Api/RedrawScheduler.h"

#include <cstdint>

namespace Slate
{

//------------------------------------------------------------------------------------------------------------------------
//                                                     THE CONTROL SUBJECT
//------------------------------------------------------------------------------------------------------------------------

// 📝 The tag exists only to make Identity<ControlSubject> a distinct type. A ControlIdentity passed where an
//    OwnerIdentity is expected is a compile error, which is the whole reason the tag is declared.
struct ControlSubject {};

using ControlIdentity = Identity<ControlSubject>;

//------------------------------------------------------------------------------------------------------------------------
//                                                      WHAT ONE CONTACT DID
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 Which part of a control one contact grabbed when it arrived.
/// note  🔴 Decided once, at arrival, and never re-decided while the contact stands. A part re-derived every
///        tick changes underneath a drag the moment the extent it was tested against moves — and for the
///        ruler and the slider, the drag is what moves it. `DrawerSpace` records the same rule for the same
///        reason.
/// tag   guarantee
enum class ControlPart : std::uint32_t
{
    Nothing   = 0u,   // [-] - the contact grabbed no part of any control
    Body      = 1u,   // [-] - the control's own extent — a row, a toggle, a stop
    Chevron   = 2u,   // [-] - the selection field's trailing cell
    Option    = 3u,   // [-] - one row of an open selection menu
    Track     = 4u,   // [-] - a slider's track, grabbed away from its thumb
    Thumb     = 5u,   // [-] - a slider's thumb
    Strip     = 6u,   // [-] - the rotation ruler's tick strip
    PartCount = 7u    // [-] - the closed count, never a part
};

/// 🧩 What one control reports after a tick of arbitration.
/// note  The mark is the control's own; a caller folds it into the panel's mark with `Dearer`. A control that
///       only recoloured must not force the re-record its neighbour needs.
/// tag   guarantee, nonallocating, nonthrowing
struct ControlVerdict
{
    bool        ReadingAltered = false;              // [-] - the caller's datum was written this tick
    bool        ContactTaken    = false;              // [-] - this control holds the pointer
    RedrawMark  Mark            = RedrawMark::Quiet;  // [-] - what presenting it again costs
};

//------------------------------------------------------------------------------------------------------------------------
//                                                       THE ROUSE FADE
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 The two interpolants every registered control carries, and the pose they interpolate between.
/// note  📐 Hover and take are separate traverses because the source fades them over different durations and
///        they overlap constantly — a row that is hovered while it is being taken is running both. One
///        interpolant carrying both would make the second transition restart the first.
/// tag   guarantee, nonallocating, nonthrowing
struct ControlPose
{
    std::uint32_t  HoverIndex = 0u;      // [-] - eased, zero quiet to one hovered
    std::uint32_t  TakeIndex  = 0u;      // [-] - eased, zero absent to one taken
    bool           Registered     = false;   // [-] - both ordinals were delivered by the integrator
};

//------------------------------------------------------------------------------------------------------------------------
//                                                        THE INDEX
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 Every slot of live control interaction, keyed by a generational identity registered once at bring-up.
/// note  🔴 This component holds **interaction** and never a datum the artist edits. `14` §1 requires a panel
///        to store none of what it presents; a dropdown that remembers it is open and a ruler that remembers
///        where a drag arrived are interaction, which `14` §4.1 places here — "panel layout, scroll,
///        expansion", owned by `14`, beside the document, and never a transaction.
/// note  🔴 Exactly one control may hold the contact and exactly one popup may stand open. Both are single
///        members rather than a per-slot condition, so a second claim is a refusal the type system reports
///        instead of a second popup nobody notices until two are drawn overlapping.
/// note  ⚠️ Advance exactly once per tick, before any control is arbitrated. The grab it resolves is what
///        every `Grab` call that tick tests against.
/// tag   owning
class ControlIndex
{
public:

    // 🔴 One index is SHARED by every panel a host constructs, so this ceiling is a whole-host budget and
    //    not a per-panel one. At 256 the validation host registered 31 sheet controls and 128 shell controls
    //    and then rejected the layer stack's 240 outright — `Construct` returned "no further control slot",
    //    the host reported it and exited 1 before its first frame. The layer stack's own static_assert
    //    could not catch that: it weighed 240 against 256 in isolation, knowing nothing of the 159 already
    //    claimed. 512 applied every panel the hosts built until the content browser was added, whose lattice
    //    and sources take a further 100 and carried the shared total to 519 — one refusal past the ceiling.
    //    640 then applied every panel until the shell's Scene Directory card grew its two tab strips, its
    //    metadata actions, its per-row kebabs and its folding property cards: 618 of 640 claimed, twenty-two
    //    spare, which is fewer than one outline row's worth. 768 restores the margin. Each slot costs two
    //    eased interpolants against MotionIntegrator::EaseCapacity, which is 3072.
    static constexpr std::uint32_t ControlCapacity = 768u;   // [-] - registered controls; never allocated, never grown

    ControlIndex()                                   = default;
    ControlIndex(const ControlIndex&)            = delete;
    ControlIndex& operator=(const ControlIndex&) = delete;
    ~ControlIndex()                                  = default;

    /// 🧩 Borrows the integrator every registered control's fades are registered into.
    /// in    Motion   [-]  borrowed; outlives this component
    /// out   Result  [-]  refuses with ContentUnsupported when a construction already stands
    /// post  the index is empty and Register may be called
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    Deliver<bool> AttachMotion(MotionIntegrator& Motion);

    /// 🧩 Reservations one slot and delivers the identity the caller holds for the life of the interface.
    /// out   Result  [-]  refuses with CapabilityAbsent before Construct, and with ExtentExhausted when the
    ///                     index is full or the integrator declines either fade
    /// note  🔴 Called at bring-up and never per tick. An identity claimed inside the tick loop exhausts the
    ///        index in a few seconds and reports it as a refusal at a call site that looks correct.
    /// post  the delivered identity carries a generation of at least one and resolves until Reset
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    Deliver<ControlIdentity> Register();

    /// 🧩 Whether one identity still names the slot it was registered for.
    /// out   Resolved  [-]  false for a default-constructed identity and for one registered before a Reset
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    bool Resolves(ControlIdentity Target) const;

    /// 🧩 Advances one tick — samples the arrived contact and retires a grab whose contact was released.
    /// in    Sampled  [-]   what `RecordingSurface::Pointer` sampled this tick
    /// in    Elapsed  [ms]  what the same tick's display condition measured
    /// note  🔴 The release is observed here rather than at the seizing control, because a control whose
    ///        extent left the arrangement between two ticks never runs again and would hold the grab for
    ///        the life of the process.
    /// post  a grab released this tick is still readable through `Released` and is gone next tick
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    void Advance(const PointerCondition& Sampled, double Elapsed);

    /// 🧩 Grabs the contact for one control and one part, if nothing else already holds it.
    /// in    Target  [-]  the seizing control
    /// in    Part     [-]  what it grabbed; `Nothing` never seizes
    /// out   Grabbed   [-]  false when another control holds the contact, or the identity is stale
    /// note  The arrival ordinates are recorded at grab so that a drag law reads its own origin rather
    ///       than the pointer's position two ticks ago.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    bool Grab(ControlIdentity Target, ControlPart Part);

    /// 🧩 Whether one control holds the contact right now.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    bool Holding(ControlIdentity Target) const;

    /// 🧩 Which part the standing grab took; `Nothing` when no grab stands.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    ControlPart HeldPart(ControlIdentity Target) const;

    /// 🧩 Whether the grab this control held was released during this tick.
    /// note  Exactly one tick reports it, which is what a tap resolves on.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    bool Released(ControlIdentity Target) const;

    /// 🧩 Which part the released grab addressed during its one reported tick.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    ControlPart ReleasedControlPart(ControlIdentity Target) const;

    /// 🧩 Where the standing contact arrived, and what the caller's datum read at that moment.
    /// out   Sampled  [-]  refuses with IdentityStale when this control holds no grab
    /// use   A drag law reads `Previous + (Position − Origin) × Rate`, never an accumulated per-tick delta,
    ///       which drifts by a pixel for every tick the pointer was outside the extent.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    Deliver<float> InitialReading(ControlIdentity Target) const;

    /// 🧩 Records the datum the seizing control departed from, once, at grab.
    /// out   Recorded  [-]  false when this control holds no grab
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    bool RecordInitial(ControlIdentity Target, float Coordinate);

    /// 🧩 Where the contact stood when it arrived.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    float OriginX() const;
    float OriginY() const;

    /// 🧩 Opens one popup, closing whichever stood before it.
    /// out   Opened  [-]  false when the identity is stale
    /// note  🔴 Closing the previous one here is what makes "exactly one popup" a property of the index
    ///        rather than a discipline every call site has to remember.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    bool Disclose(ControlIdentity Target);

    /// 🧩 Closes the standing popup, whichever control owns it.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    void Withdraw();

    /// 🧩 Whether this control's popup stands open.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    bool Disclosed(ControlIdentity Target) const;

    /// 🧩 Whether any popup stands open — what a panel tests before treating a contact as a row press.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    bool AnyDisclosed() const;

    /// 🧩 Declares whether one control is hovered, and departs its fade when the condition changed.
    /// in    Hovered    [-]   whether the pointer stands over it this tick
    /// in    Duration  [ms]  what the source declares for this control's fade
    /// out   Altered   [-]   true when the condition changed and a fade departed
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    bool DeclareHovered(ControlIdentity Target, bool Hovered, double Duration);

    /// 🧩 Declares whether one control is taken, and departs its fade when the condition changed.
    /// in    Shape  [-]  the declared cubic; Standard keeps every existing control's transition
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    bool DeclareTaken(ControlIdentity Target, bool Taken, double Duration,
                      EaseCurve Shape = EaseCurve::Standard);

    /// 🧩 How far through its hover fade one control stands, zero quiet to one hovered.
    /// out   Fraction  [-]  zero for a stale identity; a caller never has to test before interpolating
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    float HoveredFraction(ControlIdentity Target) const;

    /// 🧩 How far through its take fade one control stands, zero absent to one taken.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    float TakenFraction(ControlIdentity Target) const;

    /// 🧩 Drops the standing grab without reporting a release — what a rearrangement does.
    /// post  the next Advance reports no grab until a fresh contact arrives
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    void Abandon();

    /// 🧩 Returns the index to its constructed condition, retiring every registered identity.
    /// note  🔴 Generations are **not** rewound. An identity registered before a Reset resolves false afterwards,
    ///        which is the whole purpose of carrying a generation; rewinding them would make a stale identity
    ///        silently name a fresh slot.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    void Reset();

    /// 🧩 How many slots stand registered.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    std::uint32_t RegisteredCount() const;

private:

    /// 🧩 The slot ordinal one identity names, or the capacity when it names none.
    std::uint32_t ResolveIndex(ControlIdentity Target) const;

    MotionIntegrator*  Motion                        = nullptr;   // [-] - borrowed; never owned
    ControlPose        Poses[ControlCapacity]        = {};        // [-] - one per registered control
    std::uint32_t      Generations[ControlCapacity]  = {};        // [-] - zero declares the slot never registered
    std::uint32_t      RegisteredControlCount                 = 0u;        // [-] - how many have been claimed
    std::uint32_t      RegisteredGeneration              = 0u;        // [-] - rises with every Reset, never falls

    float              SampledX                  = 0.0f;      // [px] - this tick's pointer, sampled at Advance
    float              SampledY                 = 0.0f;      // [px]

    ControlIdentity    GrabbedControl                 = {};        // 🔴 [-] - at most one, ever
    ControlPart        GrabbedPart                    = ControlPart::Nothing;   // [-]
    ControlIdentity    ReleasedControl               = {};        // [-] - the grab this tick retired
    ControlPart        ReleasedPart                  = ControlPart::Nothing;   // [-] - its addressed part
    float              GrabbedOriginX             = 0.0f;      // [px] - where the contact arrived
    float              GrabbedOriginY            = 0.0f;      // [px]
    float              GrabbedInitial                = 0.0f;      // [-]  - the datum at grab
    bool               InitialRecorded              = false;     // [-]  - RecordInitial was called for it

    ControlIdentity    DisclosedControl              = {};        // 🔴 [-] - at most one, ever
};

}   // namespace Slate
