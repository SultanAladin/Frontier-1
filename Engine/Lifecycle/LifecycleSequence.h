/*============================================================================================================================================
                                                            LIFECYCLESEQUENCE.H
============================================================================================================================================*/
// 🧩 Deterministic C++20 component lifecycle sequence enforcing forward initialization and reverse-order teardown

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include <cmath>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace Frontier::Lifecycle
{

//------------------------------------------------------------------------------------------------------------------------
//                                                        CONCEPTS
//------------------------------------------------------------------------------------------------------------------------

template <typename ParticipantType>
concept LifecycleParticipant = requires(ParticipantType& Participant, double Δt)
{
    { Participant.Initialize() } noexcept -> std::same_as<bool>;
    { Participant.Tick(Δt) }     noexcept -> std::same_as<void>;
    { Participant.Finalize() }   noexcept -> std::same_as<void>;
};

template <typename ParticipantType>
concept SuspendableParticipant = LifecycleParticipant<ParticipantType> && requires(ParticipantType& Participant)
{
    { Participant.Suspend() } noexcept -> std::same_as<void>;
    { Participant.Resume() }  noexcept -> std::same_as<void>;
};

//------------------------------------------------------------------------------------------------------------------------
//                                                      ENUMERATIONS
//------------------------------------------------------------------------------------------------------------------------

enum class LifecyclePhase : uint8_t
{
    Dormant     = 0, // [-] - Sequence constructed; zero participants initialized
    Active      = 1, // [-] - All registered participants initialized and ticking
    Suspended   = 2, // [-] - Sequence paused; participants retain resident resources
    Finalized   = 3, // [-] - All participants torn down in reverse order
};

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct LifecycleStepMetrics
{
    uint64_t TickCount       = 0u;   // [count] - Total completed sequence ticks since Initialize
    double   Δt              = 0.0;  // [s]     - Clamped step duration of most recent tick
    double   ElapsedSeconds  = 0.0;  // [s]     - Cumulative active runtime excluding suspended intervals
    uint32_t RegisteredCount = 0u;   // [count] - Total registered participant slots
    uint32_t ActiveCount     = 0u;   // [count] - Currently initialized participant slots
};

//------------------------------------------------------------------------------------------------------------------------
//                                                  FUNCTION PROTOTYPES
//------------------------------------------------------------------------------------------------------------------------

class LifecycleSequence
{
public:
    LifecycleSequence() noexcept = default;
    ~LifecycleSequence() noexcept
    {
        Finalize();
    }

    LifecycleSequence(const LifecycleSequence&)            = delete;
    LifecycleSequence& operator=(const LifecycleSequence&) = delete;
    LifecycleSequence(LifecycleSequence&&)                 = delete;
    LifecycleSequence& operator=(LifecycleSequence&&)      = delete;

    template <LifecycleParticipant ParticipantType>
    [[nodiscard]] uint32_t RegisterParticipant(std::string_view Identifier, ParticipantType& Instance);

    [[nodiscard]] bool Initialize() noexcept
    {
        if (TransitionLocked)
        {
            return false;
        }
        if (CurrentPhase == LifecyclePhase::Active || CurrentPhase == LifecyclePhase::Suspended)
        {
            return true;
        }

        TransitionLocked  = true;
        std::size_t Ready = 0u;
        for (std::size_t Index = 0u; Index < Slots.size(); ++Index)
        {
            ParticipantSlot& Slot = Slots[Index];
            if (!Slot.InitializeDispatch(Slot.InstancePointer))
            {
                // 🔴 CRITICAL: Roll back already-initialized participants in strict reverse allocation order.
                while (Ready > 0u)
                {
                    --Ready;
                    Slots[Ready].Initialized = false;
                    Slots[Ready].Suspended   = false;
                    Slots[Ready].FinalizeDispatch(Slots[Ready].InstancePointer);
                }
                CurrentPhase     = LifecyclePhase::Dormant;
                TransitionLocked = false;
                RefreshCounts();
                return false;
            }
            Slot.Initialized = true;
            Slot.Suspended   = false;
            ++Ready;
        }

        Metrics.TickCount      = 0u;
        Metrics.Δt             = 0.0;
        Metrics.ElapsedSeconds = 0.0;
        CurrentPhase           = LifecyclePhase::Active;
        TransitionLocked       = false;
        RefreshCounts();
        return true;
    }

    void Tick(double Δt) noexcept
    {
        if (TransitionLocked || CurrentPhase != LifecyclePhase::Active)
        {
            return;
        }

        const double SafeStep = (std::isfinite(Δt) && Δt > 0.0) ? Δt : 0.0;
        TransitionLocked      = true;

        for (std::size_t Index = 0u; Index < Slots.size(); ++Index)
        {
            ParticipantSlot& Slot = Slots[Index];
            if (Slot.Initialized && !Slot.Suspended)
            {
                Slot.TickDispatch(Slot.InstancePointer, SafeStep);
            }
        }

        Metrics.Δt              = SafeStep;
        Metrics.ElapsedSeconds += SafeStep;
        ++Metrics.TickCount;
        TransitionLocked = false;
    }

    [[nodiscard]] bool Suspend() noexcept
    {
        if (TransitionLocked)
        {
            return false;
        }
        if (CurrentPhase == LifecyclePhase::Suspended)
        {
            return true;
        }
        if (CurrentPhase != LifecyclePhase::Active)
        {
            return false;
        }

        TransitionLocked = true;
        for (std::size_t Reverse = Slots.size(); Reverse > 0u; --Reverse)
        {
            ParticipantSlot& Slot = Slots[Reverse - 1u];
            if (Slot.Initialized && !Slot.Suspended)
            {
                if (Slot.SuspendDispatch != nullptr)
                {
                    Slot.SuspendDispatch(Slot.InstancePointer);
                }
                Slot.Suspended = true;
            }
        }
        CurrentPhase     = LifecyclePhase::Suspended;
        TransitionLocked = false;
        return true;
    }

    [[nodiscard]] bool Resume() noexcept
    {
        if (TransitionLocked)
        {
            return false;
        }
        if (CurrentPhase == LifecyclePhase::Active)
        {
            return true;
        }
        if (CurrentPhase != LifecyclePhase::Suspended)
        {
            return false;
        }

        TransitionLocked = true;
        for (std::size_t Index = 0u; Index < Slots.size(); ++Index)
        {
            ParticipantSlot& Slot = Slots[Index];
            if (Slot.Initialized && Slot.Suspended)
            {
                if (Slot.ResumeDispatch != nullptr)
                {
                    Slot.ResumeDispatch(Slot.InstancePointer);
                }
                Slot.Suspended = false;
            }
        }
        CurrentPhase     = LifecyclePhase::Active;
        TransitionLocked = false;
        return true;
    }

    void Finalize() noexcept
    {
        if (TransitionLocked)
        {
            return;
        }
        if (CurrentPhase == LifecyclePhase::Dormant || CurrentPhase == LifecyclePhase::Finalized)
        {
            CurrentPhase = LifecyclePhase::Finalized;
            RefreshCounts();
            return;
        }

        TransitionLocked = true;
        // 🔴 CRITICAL: Destroy and finalize registered participants in strict reverse registration order.
        for (std::size_t Reverse = Slots.size(); Reverse > 0u; --Reverse)
        {
            ParticipantSlot& Slot = Slots[Reverse - 1u];
            if (Slot.Initialized)
            {
                Slot.Initialized = false;
                Slot.Suspended   = false;
                Slot.FinalizeDispatch(Slot.InstancePointer);
            }
        }
        CurrentPhase     = LifecyclePhase::Finalized;
        TransitionLocked = false;
        RefreshCounts();
    }

    void ClearRegistrations() noexcept
    {
        Finalize();
        Slots.clear();
        Metrics      = {};
        CurrentPhase = LifecyclePhase::Dormant;
    }

    [[nodiscard]] LifecyclePhase              QueryPhase() const noexcept       { return CurrentPhase; }
    [[nodiscard]] const LifecycleStepMetrics& InspectMetrics() const noexcept   { return Metrics; }
    [[nodiscard]] std::size_t                 ParticipantCount() const noexcept { return Slots.size(); }

    [[nodiscard]] std::string_view InspectParticipantIdentifier(std::size_t SlotIndex) const noexcept
    {
        return SlotIndex < Slots.size() ? std::string_view{ Slots[SlotIndex].Identifier } : std::string_view{};
    }

private:
    using InitializeFn = bool (*)(void*) noexcept;
    using TickFn       = void (*)(void*, double) noexcept;
    using TransitionFn = void (*)(void*) noexcept;

    struct ParticipantSlot
    {
        std::string  Identifier         {};      // [-] - Diagnostic slot identifier
        void*        InstancePointer    = nullptr;// [-] - Non-owning participant address
        InitializeFn InitializeDispatch = nullptr;// [-] - Type-erased Initialize entry point
        TickFn       TickDispatch       = nullptr;// [-] - Type-erased Tick entry point
        TransitionFn SuspendDispatch    = nullptr;// [-] - Optional Suspend entry point
        TransitionFn ResumeDispatch     = nullptr;// [-] - Optional Resume entry point
        TransitionFn FinalizeDispatch   = nullptr;// [-] - Type-erased Finalize entry point
        bool         Initialized        = false; // [-] - True while participant holds initialized resources
        bool         Suspended          = false; // [-] - True while participant is paused
    };

    void RefreshCounts() noexcept
    {
        uint32_t ReadyCount = 0u;
        for (const ParticipantSlot& Slot : Slots)
        {
            if (Slot.Initialized)
            {
                ++ReadyCount;
            }
        }
        Metrics.RegisteredCount = static_cast<uint32_t>(Slots.size());
        Metrics.ActiveCount     = ReadyCount;
    }

    std::vector<ParticipantSlot> Slots            {};                      // [-] - Ordered participant registration slots
    LifecycleStepMetrics         Metrics          {};                      // [-] - Step counters and elapsed active runtime
    LifecyclePhase               CurrentPhase     = LifecyclePhase::Dormant;// [-] - Current sequence phase
    bool                         TransitionLocked = false;                 // [-] - Re-entrancy guard during hooks
};

//------------------------------------------------------------------------------------------------------------------------
//                                                TEMPLATE IMPLEMENTATIONS
//------------------------------------------------------------------------------------------------------------------------

template <LifecycleParticipant ParticipantType>
uint32_t LifecycleSequence::RegisterParticipant(std::string_view Identifier, ParticipantType& Instance)
{
    if (CurrentPhase == LifecyclePhase::Finalized)
    {
        CurrentPhase = LifecyclePhase::Dormant;
    }

    ParticipantSlot Slot{};
    Slot.Identifier         = std::string{ Identifier };
    Slot.InstancePointer    = static_cast<void*>(&Instance);
    Slot.InitializeDispatch = [](void* Raw) noexcept -> bool
    {
        return static_cast<ParticipantType*>(Raw)->Initialize();
    };
    Slot.TickDispatch = [](void* Raw, double Δt) noexcept
    {
        static_cast<ParticipantType*>(Raw)->Tick(Δt);
    };
    if constexpr (SuspendableParticipant<ParticipantType>)
    {
        Slot.SuspendDispatch = [](void* Raw) noexcept
        {
            static_cast<ParticipantType*>(Raw)->Suspend();
        };
        Slot.ResumeDispatch = [](void* Raw) noexcept
        {
            static_cast<ParticipantType*>(Raw)->Resume();
        };
    }
    Slot.FinalizeDispatch = [](void* Raw) noexcept
    {
        static_cast<ParticipantType*>(Raw)->Finalize();
    };

    Slots.push_back(std::move(Slot));
    RefreshCounts();
    return static_cast<uint32_t>(Slots.size() - 1u);
}

} // namespace Frontier::Lifecycle
