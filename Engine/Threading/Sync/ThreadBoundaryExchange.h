/*============================================================================================================================================
                                                        THREADBOUNDARYEXCHANGE.H
============================================================================================================================================*/
// 🧩 Generic bidirectional cross-thread boundary pairing wait-free multi-reader snapshot publication with lock-free directive intake

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Threading/Sync/AtomicQueue.h"
#include "Engine/Threading/Sync/PublishedSnapshotExchange.h"

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
    PublishableSnapshot SnapshotType,
    BoundaryDirective   DirectiveType,
    uint32_t            BankSlotCount = DefaultSnapshotBankSlots>
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

    // Authoritative owner-thread snapshot publication API
    void InitializeSnapshotSeed(
        const SnapshotType& InitialSnapshot,
        double              SimulationSeconds = 0.0,
        uint64_t            StepOrdinal       = 0ull) noexcept
        requires std::assignable_from<SnapshotType&, const SnapshotType&>;

    [[nodiscard]] SnapshotType* TryAcquireWritableSlot(uint32_t& OutSlotIndex) noexcept;
    [[nodiscard]] SnapshotType* AcquireWritableSlot(
        uint32_t& OutSlotIndex,
        uint32_t  MaximumSpinYields = 1024u) noexcept;

    void PublishWritableSlot(
        uint32_t SlotIndex,
        double   SimulationSeconds = 0.0,
        uint64_t StepOrdinal       = 0ull) noexcept;

    template <SnapshotWriteProcedure<SnapshotType> ProcedureType>
    [[nodiscard]] bool PublishSnapshotInPlace(
        ProcedureType&& WriterProcedure,
        double          SimulationSeconds = 0.0,
        uint64_t        StepOrdinal       = 0ull) noexcept;

    [[nodiscard]] bool PublishSnapshotCopy(
        const SnapshotType& SourceSnapshot,
        double              SimulationSeconds = 0.0,
        uint64_t            StepOrdinal       = 0ull) noexcept
        requires std::assignable_from<SnapshotType&, const SnapshotType&>;

    // Wait-free multi-reader inspection API (callable concurrently from any engine thread)
    [[nodiscard]] SnapshotReadLease<SnapshotType, BankSlotCount> AcquireReadLease() const noexcept;

    template <SnapshotReadConsumer<SnapshotType> ConsumerType>
    [[nodiscard]] SnapshotEpochHeader InspectConsistentSnapshot(ConsumerType&& Consumer) const noexcept;

    [[nodiscard]] const PublishedSnapshotExchange<SnapshotType, BankSlotCount>& AccessSnapshotExchange() const noexcept;
    [[nodiscard]] uint64_t            QueryPublishedGeneration() const noexcept;
    [[nodiscard]] SnapshotEpochHeader QueryLatestHeader() const noexcept;
    [[nodiscard]] std::size_t         QueryPendingDirectiveDepth() const noexcept;
    [[nodiscard]] std::size_t         QueryDirectiveCapacity() const noexcept;
    [[nodiscard]] uint64_t            QueryEnqueuedDirectiveCount() const noexcept;
    [[nodiscard]] uint64_t            QueryDrainedDirectiveCount() const noexcept;
    [[nodiscard]] uint64_t            QueryDroppedDirectiveCount() const noexcept;

private:
    PublishedSnapshotExchange<SnapshotType, BankSlotCount> OutboundSnapshots       {};      // [-]     - Wait-free multi-reader snapshot bank
    AtomicQueue<DirectiveType>                             InboundDirectives       ;        // [-]     - Lock-free MPMC directive intake ring
    alignas(64) std::atomic<uint64_t>                      EnqueuedDirectiveCount  { 0ull };// [count] - Directives accepted into intake ring
    alignas(64) std::atomic<uint64_t>                      DrainedDirectiveCount   { 0ull };// [count] - Directives consumed by owner thread
    alignas(64) std::atomic<uint64_t>                      DroppedDirectiveCount   { 0ull };// [count] - Directives rejected when ring saturated
};

//------------------------------------------------------------------------------------------------------------------------
//                                              TEMPLATE IMPLEMENTATIONS
//------------------------------------------------------------------------------------------------------------------------

template <PublishableSnapshot SnapshotType, BoundaryDirective DirectiveType, uint32_t BankSlotCount>
ThreadBoundaryExchange<SnapshotType, DirectiveType, BankSlotCount>::ThreadBoundaryExchange(
    std::size_t RequestedDirectiveCapacity)
    : InboundDirectives(RequestedDirectiveCapacity)
{
}

template <PublishableSnapshot SnapshotType, BoundaryDirective DirectiveType, uint32_t BankSlotCount>
bool ThreadBoundaryExchange<SnapshotType, DirectiveType, BankSlotCount>::EnqueueDirective(
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

template <PublishableSnapshot SnapshotType, BoundaryDirective DirectiveType, uint32_t BankSlotCount>
template <DirectiveIntakeReceiver<DirectiveType> ReceiverType>
std::size_t ThreadBoundaryExchange<SnapshotType, DirectiveType, BankSlotCount>::DrainDirectives(
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

template <PublishableSnapshot SnapshotType, BoundaryDirective DirectiveType, uint32_t BankSlotCount>
void ThreadBoundaryExchange<SnapshotType, DirectiveType, BankSlotCount>::InitializeSnapshotSeed(
    const SnapshotType& InitialSnapshot,
    double              SimulationSeconds,
    uint64_t            StepOrdinal) noexcept
    requires std::assignable_from<SnapshotType&, const SnapshotType&>
{
    OutboundSnapshots.InitializeSeed(InitialSnapshot, SimulationSeconds, StepOrdinal);
}

template <PublishableSnapshot SnapshotType, BoundaryDirective DirectiveType, uint32_t BankSlotCount>
SnapshotType* ThreadBoundaryExchange<SnapshotType, DirectiveType, BankSlotCount>::TryAcquireWritableSlot(
    uint32_t& OutSlotIndex) noexcept
{
    return OutboundSnapshots.TryAcquireWritableSlot(OutSlotIndex);
}

template <PublishableSnapshot SnapshotType, BoundaryDirective DirectiveType, uint32_t BankSlotCount>
SnapshotType* ThreadBoundaryExchange<SnapshotType, DirectiveType, BankSlotCount>::AcquireWritableSlot(
    uint32_t& OutSlotIndex,
    uint32_t  MaximumSpinYields) noexcept
{
    return OutboundSnapshots.AcquireWritableSlot(OutSlotIndex, MaximumSpinYields);
}

template <PublishableSnapshot SnapshotType, BoundaryDirective DirectiveType, uint32_t BankSlotCount>
void ThreadBoundaryExchange<SnapshotType, DirectiveType, BankSlotCount>::PublishWritableSlot(
    uint32_t SlotIndex,
    double   SimulationSeconds,
    uint64_t StepOrdinal) noexcept
{
    OutboundSnapshots.PublishWritableSlot(SlotIndex, SimulationSeconds, StepOrdinal);
}

template <PublishableSnapshot SnapshotType, BoundaryDirective DirectiveType, uint32_t BankSlotCount>
template <SnapshotWriteProcedure<SnapshotType> ProcedureType>
bool ThreadBoundaryExchange<SnapshotType, DirectiveType, BankSlotCount>::PublishSnapshotInPlace(
    ProcedureType&& WriterProcedure,
    double          SimulationSeconds,
    uint64_t        StepOrdinal) noexcept
{
    return OutboundSnapshots.PublishInPlace(
        std::forward<ProcedureType>(WriterProcedure),
        SimulationSeconds,
        StepOrdinal);
}

template <PublishableSnapshot SnapshotType, BoundaryDirective DirectiveType, uint32_t BankSlotCount>
bool ThreadBoundaryExchange<SnapshotType, DirectiveType, BankSlotCount>::PublishSnapshotCopy(
    const SnapshotType& SourceSnapshot,
    double              SimulationSeconds,
    uint64_t            StepOrdinal) noexcept
    requires std::assignable_from<SnapshotType&, const SnapshotType&>
{
    return OutboundSnapshots.PublishCopy(SourceSnapshot, SimulationSeconds, StepOrdinal);
}

template <PublishableSnapshot SnapshotType, BoundaryDirective DirectiveType, uint32_t BankSlotCount>
SnapshotReadLease<SnapshotType, BankSlotCount>
ThreadBoundaryExchange<SnapshotType, DirectiveType, BankSlotCount>::AcquireReadLease() const noexcept
{
    return OutboundSnapshots.AcquireReadLease();
}

template <PublishableSnapshot SnapshotType, BoundaryDirective DirectiveType, uint32_t BankSlotCount>
template <SnapshotReadConsumer<SnapshotType> ConsumerType>
SnapshotEpochHeader ThreadBoundaryExchange<SnapshotType, DirectiveType, BankSlotCount>::InspectConsistentSnapshot(
    ConsumerType&& Consumer) const noexcept
{
    return OutboundSnapshots.InspectConsistentSnapshot(std::forward<ConsumerType>(Consumer));
}

template <PublishableSnapshot SnapshotType, BoundaryDirective DirectiveType, uint32_t BankSlotCount>
const PublishedSnapshotExchange<SnapshotType, BankSlotCount>&
ThreadBoundaryExchange<SnapshotType, DirectiveType, BankSlotCount>::AccessSnapshotExchange() const noexcept
{
    return OutboundSnapshots;
}

template <PublishableSnapshot SnapshotType, BoundaryDirective DirectiveType, uint32_t BankSlotCount>
uint64_t ThreadBoundaryExchange<SnapshotType, DirectiveType, BankSlotCount>::QueryPublishedGeneration() const noexcept
{
    return OutboundSnapshots.QueryPublishedGeneration();
}

template <PublishableSnapshot SnapshotType, BoundaryDirective DirectiveType, uint32_t BankSlotCount>
SnapshotEpochHeader ThreadBoundaryExchange<SnapshotType, DirectiveType, BankSlotCount>::QueryLatestHeader() const noexcept
{
    return OutboundSnapshots.QueryLatestHeader();
}

template <PublishableSnapshot SnapshotType, BoundaryDirective DirectiveType, uint32_t BankSlotCount>
std::size_t ThreadBoundaryExchange<SnapshotType, DirectiveType, BankSlotCount>::QueryPendingDirectiveDepth() const noexcept
{
    return InboundDirectives.ApproximateDepth();
}

template <PublishableSnapshot SnapshotType, BoundaryDirective DirectiveType, uint32_t BankSlotCount>
std::size_t ThreadBoundaryExchange<SnapshotType, DirectiveType, BankSlotCount>::QueryDirectiveCapacity() const noexcept
{
    return InboundDirectives.Capacity();
}

template <PublishableSnapshot SnapshotType, BoundaryDirective DirectiveType, uint32_t BankSlotCount>
uint64_t ThreadBoundaryExchange<SnapshotType, DirectiveType, BankSlotCount>::QueryEnqueuedDirectiveCount() const noexcept
{
    return EnqueuedDirectiveCount.load(std::memory_order_relaxed);
}

template <PublishableSnapshot SnapshotType, BoundaryDirective DirectiveType, uint32_t BankSlotCount>
uint64_t ThreadBoundaryExchange<SnapshotType, DirectiveType, BankSlotCount>::QueryDrainedDirectiveCount() const noexcept
{
    return DrainedDirectiveCount.load(std::memory_order_relaxed);
}

template <PublishableSnapshot SnapshotType, BoundaryDirective DirectiveType, uint32_t BankSlotCount>
uint64_t ThreadBoundaryExchange<SnapshotType, DirectiveType, BankSlotCount>::QueryDroppedDirectiveCount() const noexcept
{
    return DroppedDirectiveCount.load(std::memory_order_relaxed);
}

} // namespace Frontier::Threading
