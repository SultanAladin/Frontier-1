/*==========================================================================================================================================
                                                        ACTIVATIONSEQUENCE.H
==========================================================================================================================================*/
// 🧩 Ordered activation, suspension, integration, and reclamation shared by UI and scene code.

#pragma once

#include <cmath>
#include <exception>
#include <memory>

#ifndef FRONTIER_EDITOR
#define FRONTIER_EDITOR 0
#endif

namespace Frontier
{
//------------------------------------------------------------------------------------------------------------------------
//                                                   ACTIVATION PHASE
//------------------------------------------------------------------------------------------------------------------------

enum class ActivationPhase
{
    Dormant,
    Ready,
    Active,
    Finalized
};

class ActivationSequence;

struct ActivationReclamation
{
    void operator()(ActivationSequence* Allocation) const noexcept;
};

using ActivationOwnership = std::unique_ptr<ActivationSequence, ActivationReclamation>;

//------------------------------------------------------------------------------------------------------------------------
//                                                  ACTIVATION SEQUENCE
//------------------------------------------------------------------------------------------------------------------------

// Single-thread ownership. Hooks must not throw; hooks cannot recursively change the lifecycle.
// Construct derived allocations into ActivationOwnership so finalization precedes virtual destruction.
// A C++ destructor cannot safely dispatch derived finalization hooks; the deleter does this explicitly.
class ActivationSequence
{
public:
    ActivationSequence(const ActivationSequence&)            = delete;
    ActivationSequence& operator=(const ActivationSequence&) = delete;
    ActivationSequence(ActivationSequence&&)                 = delete;
    ActivationSequence& operator=(ActivationSequence&&)      = delete;

    [[nodiscard]] ActivationPhase QueryPhase() const noexcept { return Phase; }

    [[nodiscard]] bool Initialize() noexcept
    {
        if (TransitionPending || Phase == ActivationPhase::Finalized) return false;
        if (Phase != ActivationPhase::Dormant) return true;

        TransitionPending = true;
        const bool Initialized = InitializeContent();
        if (Initialized)
        {
#if FRONTIER_EDITOR
            InitializeEditorContent();
#endif
            Phase = ActivationPhase::Ready;
        }
        else
        {
            // 📝 Failed initialization must release any partially acquired resources too.
            FinalizeContent();
            Phase = ActivationPhase::Finalized;
        }
        TransitionPending = false;
        return Initialized;
    }

    [[nodiscard]] bool Activate() noexcept
    {
        if (TransitionPending) return false;
        if (Phase == ActivationPhase::Active) return true;
        if (Phase != ActivationPhase::Ready) return false;

        TransitionPending = true;
        const bool Activated = ActivateContent();
        if (Activated) Phase = ActivationPhase::Active;
        TransitionPending = false;
        return Activated;
    }

    [[nodiscard]] bool Suspend() noexcept
    {
        if (TransitionPending) return false;
        if (Phase == ActivationPhase::Ready) return true;
        if (Phase != ActivationPhase::Active) return false;

        TransitionPending = true;
        SuspendContent();
        Phase             = ActivationPhase::Ready;
        TransitionPending = false;
        return true;
    }

    [[nodiscard]] bool Integrate(double Δτ) noexcept
    {
        if (TransitionPending || Phase != ActivationPhase::Active || !std::isfinite(Δτ) || Δτ < 0.0)
            return false;

        TransitionPending = true;
        IntegrateContent(Δτ);
        TransitionPending = false;
        return true;
    }

    [[nodiscard]] bool Finalize() noexcept
    {
        if (TransitionPending) return false;
        if (Phase == ActivationPhase::Finalized) return true;

        TransitionPending = true;
        if (Phase == ActivationPhase::Active) SuspendContent();
        if (Phase != ActivationPhase::Dormant)
        {
#if FRONTIER_EDITOR
            FinalizeEditorContent();
#endif
            FinalizeContent();
        }
        Phase             = ActivationPhase::Finalized;
        TransitionPending = false;
        return true;
    }

protected:
    ActivationSequence() noexcept          = default;
    virtual ~ActivationSequence() noexcept = default;

    // Hook defaults allow implementations to override only the behaviour they need.
    virtual bool InitializeContent() noexcept { return true; }
    virtual bool ActivateContent() noexcept { return true; }
    virtual void SuspendContent() noexcept {}
    virtual void IntegrateContent(double) noexcept {}
    virtual void FinalizeContent() noexcept {}
#if FRONTIER_EDITOR
    virtual void InitializeEditorContent() noexcept {}
    virtual void FinalizeEditorContent() noexcept {}
#endif

private:
    friend struct ActivationReclamation;

    ActivationPhase Phase             = ActivationPhase::Dormant;
    bool            TransitionPending = false;
};

inline void ActivationReclamation::operator()(ActivationSequence* Allocation) const noexcept
{
    if (!Allocation) return;
    const bool Finalized = Allocation->Finalize();
    // 🔴 Reclaim only outside callbacks; deleting during a hook would invalidate its active call.
    if (!Finalized) std::terminate();
    delete Allocation;
}
}
