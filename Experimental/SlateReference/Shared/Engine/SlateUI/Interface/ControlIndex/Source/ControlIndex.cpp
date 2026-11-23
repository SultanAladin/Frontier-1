//============================================================================================================================================
//                                                          CONTROLINDEX.CPP
//============================================================================================================================================
// 🧩 The one grab, the one open popup, and the two fades every registered control carries.

#include "SlateUI/Interface/ControlIndex/Api/ControlIndex.h"

namespace Slate
{

//------------------------------------------------------------------------------------------------------------------------
//                                                        CONSTRUCTION
//------------------------------------------------------------------------------------------------------------------------

Deliver<bool> ControlIndex::AttachMotion(MotionIntegrator& Incoming)
{
    if (Motion != nullptr)
    {
        return Deliver<bool>::Refuse(Refusal{ RefusalReason::ContentUnsupported,
                                                   "ControlIndex is already constructed" });
    }

    Motion = &Incoming;

    return Deliver<bool>::Result(true);
}

Deliver<ControlIdentity> ControlIndex::Register()
{
    if (Motion == nullptr)
    {
        return Deliver<ControlIdentity>::Refuse(Refusal{ RefusalReason::CapabilityAbsent,
                                                          "ControlIndex was not constructed" });
    }

    if (RegisteredControlCount >= ControlCapacity)
    {
        return Deliver<ControlIdentity>::Refuse(Refusal{ RefusalReason::ExtentExhausted,
                                                          "ControlIndex holds no further control slot" });
    }

    // 📝 🔴 Both fades are registered before the slot is claimed. Target first and refusing second would leave
    //    a slot registered against an interpolant that does not exist, and every later read of it would return
    //    the ordinal zero — which is another control's fade.
    const Deliver<std::uint32_t> HoverRegistered = Motion->RegisterEased(0.0);

    if (!HoverRegistered.Resolved)
    {
        return Deliver<ControlIdentity>::Refuse(Refusal{ RefusalReason::ExtentExhausted,
                                                          "the integrator rejected a hover fade" });
    }

    const Deliver<std::uint32_t> TakeRegistered = Motion->RegisterEased(0.0);

    if (!TakeRegistered.Resolved)
    {
        return Deliver<ControlIdentity>::Refuse(Refusal{ RefusalReason::ExtentExhausted,
                                                          "the integrator rejected a take fade" });
    }

    const std::uint32_t Target = RegisteredControlCount;
    ++RegisteredControlCount;

    // 📝 The generation rises with every Reset and never falls, so an identity registered before one resolves
    //    false afterwards rather than naming whatever now occupies its ordinal.
    Generations[Target] = RegisteredGeneration + 1u;

    Poses[Target].HoverIndex = HoverRegistered.Resolve();
    Poses[Target].TakeIndex  = TakeRegistered.Resolve();
    Poses[Target].Registered     = true;

    return Deliver<ControlIdentity>::Result(ControlIdentity{ Target, Generations[Target] });
}

std::uint32_t ControlIndex::ResolveIndex(ControlIdentity Target) const
{
    if (!Target.IdentityDeclared() || Target.SlotIndex >= RegisteredControlCount)
        return ControlCapacity;

    if (Generations[Target.SlotIndex] != Target.SlotGeneration)
        return ControlCapacity;

    return Target.SlotIndex;
}

bool ControlIndex::Resolves(ControlIdentity Target) const
{
    return ResolveIndex(Target) != ControlCapacity;
}

std::uint32_t ControlIndex::RegisteredCount() const
{
    return RegisteredControlCount;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                          THE TICK
//------------------------------------------------------------------------------------------------------------------------

void ControlIndex::Advance(const PointerCondition& Sampled, double Elapsed)
{
    static_cast<void>(Elapsed);

    // 📝 The tick's pointer is retained so that Grab can stamp the arrival ordinates without every control
    //    having to pass them in — and, more to the point, without a control being able to pass in a position
    //    it computed rather than the one the window system reported.
    SampledX  = Sampled.PositionX;
    SampledY = Sampled.PositionY;

    // 📝 🔴 The release is retired here and not at the seizing control. A control whose extent left the
    //    arrangement between two ticks never runs again, and a grab retired only by that control would
    //    stand for the life of the process — every later contact rejected because something invisible holds it.
    ReleasedControl = {};
    ReleasedPart    = ControlPart::Nothing;

    if (GrabbedPart != ControlPart::Nothing && !Sampled.ContactHeld)
    {
        ReleasedControl    = GrabbedControl;
        ReleasedPart       = GrabbedPart;
        GrabbedControl      = {};
        GrabbedPart         = ControlPart::Nothing;
        GrabbedInitial     = 0.0f;
        InitialRecorded   = false;
    }
}

bool ControlIndex::Grab(ControlIdentity Target, ControlPart Part)
{
    if (Part == ControlPart::Nothing || ResolveIndex(Target) == ControlCapacity)
        return false;

    // 🔴 One grab. A second claim while one stands is rejected rather than replacing it, which is what
    //    keeps a drag addressing the control it began on when the pointer crosses a neighbour.
    if (GrabbedPart != ControlPart::Nothing)
        return false;

    GrabbedControl      = Target;
    GrabbedPart         = Part;
    GrabbedOriginX  = SampledX;
    GrabbedOriginY = SampledY;
    GrabbedInitial     = 0.0f;
    InitialRecorded   = false;

    return true;
}

bool ControlIndex::Holding(ControlIdentity Target) const
{
    return GrabbedPart != ControlPart::Nothing && GrabbedControl == Target && ResolveIndex(Target) != ControlCapacity;
}

ControlPart ControlIndex::HeldPart(ControlIdentity Target) const
{
    return Holding(Target) ? GrabbedPart : ControlPart::Nothing;
}

bool ControlIndex::Released(ControlIdentity Target) const
{
    return ReleasedControl.IdentityDeclared() && ReleasedControl == Target;
}

ControlPart ControlIndex::ReleasedControlPart(ControlIdentity Target) const
{
    return Released(Target) ? ReleasedPart : ControlPart::Nothing;
}

bool ControlIndex::RecordInitial(ControlIdentity Target, float Coordinate)
{
    if (!Holding(Target))
        return false;

    GrabbedInitial   = Coordinate;
    InitialRecorded = true;

    return true;
}

Deliver<float> ControlIndex::InitialReading(ControlIdentity Target) const
{
    if (!Holding(Target) || !InitialRecorded)
    {
        return Deliver<float>::Refuse(Refusal{ RefusalReason::IdentityStale,
                                                       "this control holds no grab to depart from" });
    }

    return Deliver<float>::Result(GrabbedInitial);
}

float ControlIndex::OriginX() const
{
    return GrabbedOriginX;
}

float ControlIndex::OriginY() const
{
    return GrabbedOriginY;
}

void ControlIndex::Abandon()
{
    GrabbedControl    = {};
    GrabbedPart       = ControlPart::Nothing;
    ReleasedControl  = {};
    ReleasedPart     = ControlPart::Nothing;
    GrabbedInitial   = 0.0f;
    InitialRecorded = false;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                       THE DISCLOSURE
//------------------------------------------------------------------------------------------------------------------------

bool ControlIndex::Disclose(ControlIdentity Target)
{
    if (ResolveIndex(Target) == ControlCapacity)
        return false;

    // 🔴 Whatever stood before is closed by the assignment itself. Two open popups cannot be represented.
    DisclosedControl = Target;

    return true;
}

void ControlIndex::Withdraw()
{
    DisclosedControl = {};
}

bool ControlIndex::Disclosed(ControlIdentity Target) const
{
    return DisclosedControl.IdentityDeclared() && DisclosedControl == Target
        && ResolveIndex(Target) != ControlCapacity;
}

bool ControlIndex::AnyDisclosed() const
{
    return DisclosedControl.IdentityDeclared();
}

//------------------------------------------------------------------------------------------------------------------------
//                                                         THE FADES
//------------------------------------------------------------------------------------------------------------------------

namespace
{

/// 🧩 Departs one eased traverse toward a declared pose, if it is not already heading there.
/// note  📐 Previous from where it **stands**, never from zero or one. A fade re-departed from its endpoint
///        jumps backward the moment the pointer crosses an edge twice inside one duration, which is exactly
///        what a pointer travelling along a column of rows does.
/// cost  ✔️
bool DepartToward(MotionIntegrator& Motion, std::uint32_t Index, bool Toward, double Duration,
                  EaseCurve Shape = EaseCurve::Standard)
{
    EasedInterpolant& Fade    = Motion.Eased(Index);
    const double      Arrival = Toward ? 1.0 : 0.0;

    if (Fade.Settled && Fade.Current() == Arrival)
        return false;

    if (!Fade.Settled && Fade.Incoming == Arrival)
        return false;

    Fade.Depart(Fade.Current(), Arrival, Duration, 0.0, Shape);

    return true;
}

}   // namespace

bool ControlIndex::DeclareHovered(ControlIdentity Target, bool Hovered, double Duration)
{
    const std::uint32_t Index = ResolveIndex(Target);

    if (Index == ControlCapacity || Motion == nullptr || !Poses[Index].Registered)
        return false;

    return DepartToward(*Motion, Poses[Index].HoverIndex, Hovered, Duration);
}

bool ControlIndex::DeclareTaken(ControlIdentity Target, bool Taken, double Duration, EaseCurve Shape)
{
    const std::uint32_t Index = ResolveIndex(Target);

    if (Index == ControlCapacity || Motion == nullptr || !Poses[Index].Registered)
        return false;

    return DepartToward(*Motion, Poses[Index].TakeIndex, Taken, Duration, Shape);
}

float ControlIndex::HoveredFraction(ControlIdentity Target) const
{
    const std::uint32_t Index = ResolveIndex(Target);

    if (Index == ControlCapacity || Motion == nullptr || !Poses[Index].Registered)
        return 0.0f;

    return static_cast<float>(Motion->Eased(Poses[Index].HoverIndex).Current());
}

float ControlIndex::TakenFraction(ControlIdentity Target) const
{
    const std::uint32_t Index = ResolveIndex(Target);

    if (Index == ControlCapacity || Motion == nullptr || !Poses[Index].Registered)
        return 0.0f;

    return static_cast<float>(Motion->Eased(Poses[Index].TakeIndex).Current());
}

//------------------------------------------------------------------------------------------------------------------------
//                                                        RETIREMENT
//------------------------------------------------------------------------------------------------------------------------

void ControlIndex::Reset()
{
    // 🔴 The generation rises past every ordinal ever registered, so no identity from before this call can ever
    //    resolve again. Rewinding it to zero would make a stale identity name a fresh slot silently.
    ++RegisteredGeneration;

    for (std::uint32_t Index = 0u; Index < RegisteredControlCount; ++Index)
    {
        Generations[Index] = 0u;
        Poses[Index]       = ControlPose{};
    }

    RegisteredControlCount    = 0u;
    DisclosedControl = {};

    Abandon();
}

}   // namespace Slate
