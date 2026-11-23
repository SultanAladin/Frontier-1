/*============================================================================================================================================
                                                        THREADBOUNDARYEXCHANGE.H
============================================================================================================================================*/
// 🧩 Generic bidirectional cross-thread boundary pairing wait-free multi-reader reading publication with lock-free directive intake

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Threading/Sync/AtomicQueue.h"
#include "Engine/Threading/Sync/PublishedReadingExchange.h"

#include <atomic>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <utility>

namespace Frontier::Threading
{

//------------------------------------------------------------------------------------------------------------------------
//                                                        CONCEPTS
//------------------------------------------------------------------------------------------------------------------------

template <typename DirectiveType>
concept BoundaryDirective = std::default_initializable<DirectiveType> && std::movable<DirectiveType>;

template <typename ReceiverType, typename DirectiveType>
concept DirectiveIntakeReceiver = requires(ReceiverType&& Receiver, const DirectiveType& IncomingDirective)
{
    { Receiver(IncomingDirective) } -> std::same_as<void>;
};

//------------------------------------------------------------------------------------------------------------------------
//                                                       CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

inline constexpr std::size_t DefaultBoundaryDirectiveCapacity = 1024u; // [count] - Default lock-free inbound directive ring depth

//------------------------------------------------------------------------------------------------------------------------
//                                                  FUNCTION PROTOTYPES
//------------------------------------------------------------------------------------------------------------------------

template <
    PublishableReading ReadingType,
    BoundaryDirective   DirectiveType,
    uint32_t            BankSlotCount = DefaultReadingBankSlots>
class ThreadBoundaryExchange
{
public:
    explicit ThreadBoundaryExchange(std::size_t RequestedDirectiveCapacity = DefaultBoundaryDirectiveCapacity);
    ~ThreadBoundaryExchange() noexcept = default;

    ThreadBoundaryExchange(const ThreadBoundaryExchange&)            = delete;
    ThreadBoundaryExchange& operator=(const ThreadBoundaryExchange&) = delete;

    // Cross-thread producer intake (callable concurrently from GameThread, EditorThread, or ExecutionChannels)
    [[nodiscard]] bool EnqueueDirective(DirectiveType IncomingDirective) noexcept;

    // Single-consumer drain invoked on the authoritative owner thread (e.g., PhysicsThread) prior to simulation step
    template <DirectiveIntakeReceiver<DirectiveType> ReceiverType>
    std::size_t DrainDirectives(
        ReceiverType&& Receiver,
        std::size_t    MaximumCount = DefaultBoundaryDirectiveCapacity) noexcept;

    // Authoritative owner-thread reading publication API
    void InitializeReadingSeed(
        const ReadingType& InitialReading,
        double              SimulationSeconds = 0.0,
        uint64_t            StepOrdinal       = 0ull) noexcept
        requires std::assignable_from<ReadingType&, const ReadingType&>;

    [[nodiscard]] ReadingType* TryAcquireWritableSlot(uint32_t& OutSlotIndex) noexcept;
    [[nodiscard]] ReadingType* AcquireWritableSlot(
        uint32_t& OutSlotIndex,
        uint32_t  MaximumSpinYields = 1024u) noexcept;

    void PublishWritableSlot(
        uint32_t SlotIndex,
        double   SimulationSeconds = 0.0,
        uint64_t StepOrdinal       = 0ull) noexcept;

    template <ReadingWriteProcedure<ReadingType> ProcedureType>
    [[nodiscard]] bool PublishReadingInPlace(
        ProcedureType&& WriterProcedure,
        double          SimulationSeconds = 0.0,
        uint64_t        StepOrdinal       = 0ull) noexcept;

    [[nodiscard]] bool PublishReadingCopy(
        const ReadingType& SourceReading,
        double              SimulationSeconds = 0.0,
        uint64_t            StepOrdinal       = 0ull) noexcept
        requires std::assignable_from<ReadingType&, const ReadingType&>;

    // Wait-free multi-reader inspection API (callable concurrently from any engine thread)
    [[nodiscard]] ReadingLease<ReadingType, BankSlotCount> AcquireReadLease() const noexcept;

    template <ReadingConsumer<ReadingType> ConsumerType>
    [[nodiscard]] ReadingEpochHeader InspectConsistentReading(ConsumerType&& Consumer) const noexcept;

    [[nodiscard]] const PublishedReadingExchange<ReadingType, BankSlotCount>& AccessReadingExchange() const noexcept;
    [[nodiscard]] uint64_t            QueryPublishedGeneration() const noexcept;
    [[nodiscard]] ReadingEpochHeader QueryLatestHeader() const noexcept;
    [[nodiscard]] std::size_t         QueryPendingDirectiveDepth() const noexcept;
    [[nodiscard]] std::size_t         QueryDirectiveCapacity() const noexcept;
    [[nodiscard]] uint64_t            QueryEnqueuedDirectiveCount() const noexcept;
    [[nodiscard]] uint64_t            QueryDrainedDirectiveCount() const noexcept;
    [[nodiscard]] uint64_t            QueryDroppedDirectiveCount() const noexcept;

private:
    PublishedReadingExchange<ReadingType, BankSlotCount> OutboundReadings       {};      // [-]     - Wait-free multi-reader reading bank
    AtomicQueue<DirectiveType>                             InboundDirectives       ;        // [-]     - Lock-free MPMC directive intake ring
    alignas(64) std::atomic<uint64_t>                      EnqueuedDirectiveCount  { 0ull };// [count] - Directives accepted into intake ring
    alignas(64) std::atomic<uint64_t>                      DrainedDirectiveCount   { 0ull };// [count] - Directives consumed by owner thread
    alignas(64) std::atomic<uint64_t>                      DroppedDirectiveCount   { 0ull };// [count] - Directives rejected when ring saturated
};

//------------------------------------------------------------------------------------------------------------------------
//                                              TEMPLATE IMPLEMENTATIONS
//------------------------------------------------------------------------------------------------------------------------

template <PublishableReading ReadingType, BoundaryDirective DirectiveType, uint32_t BankSlotCount>
ThreadBoundaryExchange<ReadingType, DirectiveType, BankSlotCount>::ThreadBoundaryExchange(
    std::size_t RequestedDirectiveCapacity)
    : InboundDirectives(RequestedDirectiveCapacity)
{
}

template <PublishableReading ReadingType, BoundaryDirective DirectiveType, uint32_t BankSlotCount>
bool ThreadBoundaryExchange<ReadingType, DirectiveType, BankSlotCount>::EnqueueDirective(
    DirectiveType IncomingDirective) noexcept
{
    if (InboundDirectives.TryEnqueue(std::move(IncomingDirective)))
    {
        EnqueuedDirectiveCount.fetch_add(1ull, std::memory_order_relaxed);
        return true;
    }
    DroppedDirectiveCount.fetch_add(1ull, std::memory_order_relaxed);
    return false;
}

template <PublishableReading ReadingType, BoundaryDirective DirectiveType, uint32_t BankSlotCount>
template <DirectiveIntakeReceiver<DirectiveType> ReceiverType>
std::size_t ThreadBoundaryExchange<ReadingType, DirectiveType, BankSlotCount>::DrainDirectives(
    ReceiverType&& Receiver,
    std::size_t    MaximumCount) noexcept
{
    std::size_t   Consumed = 0u;
    DirectiveType Pending  {};
    while (Consumed < MaximumCount && InboundDirectives.TryDequeue(Pending))
    {
        Receiver(Pending);
        ++Consumed;
    }
    if (Consumed > 0u)
    {
        DrainedDirectiveCount.fetch_add(static_cast<uint64_t>(Consumed), std::memory_order_relaxed);
    }
    return Consumed;
}

template <PublishableReading ReadingType, BoundaryDirective DirectiveType, uint32_t BankSlotCount>
void ThreadBoundaryExchange<ReadingType, DirectiveType, BankSlotCount>::InitializeReadingSeed(
    const ReadingType& InitialReading,
    double              SimulationSeconds,
    uint64_t            StepOrdinal) noexcept
    requires std::assignable_from<ReadingType&, const ReadingType&>
{
    OutboundReadings.InitializeSeed(InitialReading, SimulationSeconds, StepOrdinal);
}

template <PublishableReading ReadingType, BoundaryDirective DirectiveType, uint32_t BankSlotCount>
ReadingType* ThreadBoundaryExchange<ReadingType, DirectiveType, BankSlotCount>::TryAcquireWritableSlot(
    uint32_t& OutSlotIndex) noexcept
{
    return OutboundReadings.TryAcquireWritableSlot(OutSlotIndex);
}

template <PublishableReading ReadingType, BoundaryDirective DirectiveType, uint32_t BankSlotCount>
ReadingType* ThreadBoundaryExchange<ReadingType, DirectiveType, BankSlotCount>::AcquireWritableSlot(
    uint32_t& OutSlotIndex,
    uint32_t  MaximumSpinYields) noexcept
{
    return OutboundReadings.AcquireWritableSlot(OutSlotIndex, MaximumSpinYields);
}

template <PublishableReading ReadingType, BoundaryDirective DirectiveType, uint32_t BankSlotCount>
void ThreadBoundaryExchange<ReadingType, DirectiveType, BankSlotCount>::PublishWritableSlot(
    uint32_t SlotIndex,
    double   SimulationSeconds,
    uint64_t StepOrdinal) noexcept
{
    OutboundReadings.PublishWritableSlot(SlotIndex, SimulationSeconds, StepOrdinal);
}

template <PublishableReading ReadingType, BoundaryDirective DirectiveType, uint32_t BankSlotCount>
template <ReadingWriteProcedure<ReadingType> ProcedureType>
bool ThreadBoundaryExchange<ReadingType, DirectiveType, BankSlotCount>::PublishReadingInPlace(
    ProcedureType&& WriterProcedure,
    double          SimulationSeconds,
    uint64_t        StepOrdinal) noexcept
{
    return OutboundReadings.PublishInPlace(
        std::forward<ProcedureType>(WriterProcedure),
        SimulationSeconds,
        StepOrdinal);
}

template <PublishableReading ReadingType, BoundaryDirective DirectiveType, uint32_t BankSlotCount>
bool ThreadBoundaryExchange<ReadingType, DirectiveType, BankSlotCount>::PublishReadingCopy(
    const ReadingType& SourceReading,
    double              SimulationSeconds,
    uint64_t            StepOrdinal) noexcept
    requires std::assignable_from<ReadingType&, const ReadingType&>
{
    return OutboundReadings.PublishCopy(SourceReading, SimulationSeconds, StepOrdinal);
}

template <PublishableReading ReadingType, BoundaryDirective DirectiveType, uint32_t BankSlotCount>
ReadingLease<ReadingType, BankSlotCount>
ThreadBoundaryExchange<ReadingType, DirectiveType, BankSlotCount>::AcquireReadLease() const noexcept
{
    return OutboundReadings.AcquireReadLease();
}

template <PublishableReading ReadingType, BoundaryDirective DirectiveType, uint32_t BankSlotCount>
template <ReadingConsumer<ReadingType> ConsumerType>
ReadingEpochHeader ThreadBoundaryExchange<ReadingType, DirectiveType, BankSlotCount>::InspectConsistentReading(
    ConsumerType&& Consumer) const noexcept
{
    return OutboundReadings.InspectConsistentReading(std::forward<ConsumerType>(Consumer));
}

template <PublishableReading ReadingType, BoundaryDirective DirectiveType, uint32_t BankSlotCount>
const PublishedReadingExchange<ReadingType, BankSlotCount>&
ThreadBoundaryExchange<ReadingType, DirectiveType, BankSlotCount>::AccessReadingExchange() const noexcept
{
    return OutboundReadings;
}

template <PublishableReading ReadingType, BoundaryDirective DirectiveType, uint32_t BankSlotCount>
uint64_t ThreadBoundaryExchange<ReadingType, DirectiveType, BankSlotCount>::QueryPublishedGeneration() const noexcept
{
    return OutboundReadings.QueryPublishedGeneration();
}

template <PublishableReading ReadingType, BoundaryDirective DirectiveType, uint32_t BankSlotCount>
ReadingEpochHeader ThreadBoundaryExchange<ReadingType, DirectiveType, BankSlotCount>::QueryLatestHeader() const noexcept
{
    return OutboundReadings.QueryLatestHeader();
}

template <PublishableReading ReadingType, BoundaryDirective DirectiveType, uint32_t BankSlotCount>
std::size_t ThreadBoundaryExchange<ReadingType, DirectiveType, BankSlotCount>::QueryPendingDirectiveDepth() const noexcept
{
    return InboundDirectives.ApproximateDepth();
}

template <PublishableReading ReadingType, BoundaryDirective DirectiveType, uint32_t BankSlotCount>
std::size_t ThreadBoundaryExchange<ReadingType, DirectiveType, BankSlotCount>::QueryDirectiveCapacity() const noexcept
{
    return InboundDirectives.Capacity();
}

template <PublishableReading ReadingType, BoundaryDirective DirectiveType, uint32_t BankSlotCount>
uint64_t ThreadBoundaryExchange<ReadingType, DirectiveType, BankSlotCount>::QueryEnqueuedDirectiveCount() const noexcept
{
    return EnqueuedDirectiveCount.load(std::memory_order_relaxed);
}

template <PublishableReading ReadingType, BoundaryDirective DirectiveType, uint32_t BankSlotCount>
uint64_t ThreadBoundaryExchange<ReadingType, DirectiveType, BankSlotCount>::QueryDrainedDirectiveCount() const noexcept
{
    return DrainedDirectiveCount.load(std::memory_order_relaxed);
}

template <PublishableReading ReadingType, BoundaryDirective DirectiveType, uint32_t BankSlotCount>
uint64_t ThreadBoundaryExchange<ReadingType, DirectiveType, BankSlotCount>::QueryDroppedDirectiveCount() const noexcept
{
    return DroppedDirectiveCount.load(std::memory_order_relaxed);
}

} // namespace Frontier::Threading
