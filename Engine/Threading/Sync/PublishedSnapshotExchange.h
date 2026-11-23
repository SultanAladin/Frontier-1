/*============================================================================================================================================
                                                      PUBLISHEDSNAPSHOTEXCHANGE.H
============================================================================================================================================*/
// 🧩 Generic wait-free multi-reader, single-writer generation-stamped snapshot bank for cross-thread publication without locks or tearing

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include <array>
#include <atomic>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <thread>
#include <utility>

namespace Frontier::Threading
{

//------------------------------------------------------------------------------------------------------------------------
//                                                        CONCEPTS
//------------------------------------------------------------------------------------------------------------------------

template <typename SnapshotType>
concept PublishableSnapshot = std::default_initializable<SnapshotType> && std::destructible<SnapshotType>;

template <typename ProcedureType, typename SnapshotType>
concept SnapshotWriteProcedure = requires(ProcedureType&& Procedure, SnapshotType& WritablePayload)
{
    { Procedure(WritablePayload) } -> std::same_as<void>;
};

template <typename ConsumerType, typename SnapshotType>
concept SnapshotReadConsumer = requires(ConsumerType&& Consumer, const SnapshotType& ImmutablePayload)
{
    { Consumer(ImmutablePayload) } -> std::same_as<void>;
};

//------------------------------------------------------------------------------------------------------------------------
//                                                       CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

inline constexpr uint32_t DefaultSnapshotBankSlots  = 8u;   // [count] - Default recycled snapshot pages supporting multi-thread readers
inline constexpr uint32_t MinimumSnapshotBankSlots  = 3u;   // [count] - Minimum bank depth (published + active write + 1 trailing reader)
inline constexpr uint32_t MaximumSnapshotBankSlots  = 255u; // [count] - Maximum bank depth encodable in 8-bit control slot index

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct SnapshotEpochHeader
{
    uint64_t GenerationSequence = 0ull; // [count] - Monotonic publication generation counter
    double   SimulationSeconds  = 0.0;  // [s]     - Authoritative simulation clock at publication instant
    uint64_t StepOrdinal        = 0ull; // [count] - Authoritative fixed simulation step index at publication
};

template <PublishableSnapshot SnapshotType, uint32_t BankSlotCount = DefaultSnapshotBankSlots>
class PublishedSnapshotExchange;

//------------------------------------------------------------------------------------------------------------------------
//                                                  FUNCTION PROTOTYPES
//------------------------------------------------------------------------------------------------------------------------

template <PublishableSnapshot SnapshotType, uint32_t BankSlotCount = DefaultSnapshotBankSlots>
class SnapshotReadLease
{
public:
    SnapshotReadLease() noexcept = default;
    ~SnapshotReadLease() noexcept;

    SnapshotReadLease(const SnapshotReadLease&)            = delete;
    SnapshotReadLease& operator=(const SnapshotReadLease&) = delete;

    SnapshotReadLease(SnapshotReadLease&& IncomingLease) noexcept;
    SnapshotReadLease& operator=(SnapshotReadLease&& IncomingLease) noexcept;

    [[nodiscard]] bool                Valid() const noexcept;
    [[nodiscard]] const SnapshotType& InspectPayload() const noexcept;
    [[nodiscard]] const SnapshotType* operator->() const noexcept;
    [[nodiscard]] const SnapshotType& operator*() const noexcept;
    [[nodiscard]] SnapshotEpochHeader InspectHeader() const noexcept;
    [[nodiscard]] uint32_t            QuerySlotIndex() const noexcept;

    void Release() noexcept;

private:
    friend class PublishedSnapshotExchange<SnapshotType, BankSlotCount>;

    SnapshotReadLease(
        std::atomic<int64_t>* InReaderBalance,
        const SnapshotType*   InPayload,
        SnapshotEpochHeader   InHeader,
        uint32_t              InSlotIndex) noexcept;

    std::atomic<int64_t>* BoundReaderBalance = nullptr; // [-]   - Pointer to owning bank slot atomic reader counter
    const SnapshotType*   BoundPayload       = nullptr; // [-]   - Immutable published snapshot payload
    SnapshotEpochHeader   BoundHeader        {};        // [-]   - Generation and simulation timestamp metadata
    uint32_t              BoundSlotIndex     = 0u;      // [idx] - Acquired bank slot index for diagnostics
};

template <PublishableSnapshot SnapshotType, uint32_t BankSlotCount>
class PublishedSnapshotExchange
{
    static_assert(
        BankSlotCount >= MinimumSnapshotBankSlots && BankSlotCount <= MaximumSnapshotBankSlots,
        "PublishedSnapshotExchange BankSlotCount must lie within [3, 255].");

public:
    PublishedSnapshotExchange() noexcept;
    ~PublishedSnapshotExchange() noexcept = default;

    PublishedSnapshotExchange(const PublishedSnapshotExchange&)            = delete;
    PublishedSnapshotExchange& operator=(const PublishedSnapshotExchange&) = delete;

    // Single-writer initialization & publication API (invoke strictly from owning producer thread)
    void InitializeSeed(
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
    [[nodiscard]] bool PublishInPlace(
        ProcedureType&& WriterProcedure,
        double          SimulationSeconds = 0.0,
        uint64_t        StepOrdinal       = 0ull) noexcept;

    [[nodiscard]] bool PublishCopy(
        const SnapshotType& SourceSnapshot,
        double              SimulationSeconds = 0.0,
        uint64_t            StepOrdinal       = 0ull) noexcept
        requires std::assignable_from<SnapshotType&, const SnapshotType&>;

    // Wait-free multi-reader inspection API (safe simultaneously from GameThread, RenderThread, EditorThread, or workers)
    [[nodiscard]] SnapshotReadLease<SnapshotType, BankSlotCount> AcquireReadLease() const noexcept;

    template <SnapshotReadConsumer<SnapshotType> ConsumerType>
    [[nodiscard]] SnapshotEpochHeader InspectConsistentSnapshot(ConsumerType&& Consumer) const noexcept;

    [[nodiscard]] uint64_t            QueryPublishedGeneration() const noexcept;
    [[nodiscard]] SnapshotEpochHeader QueryLatestHeader() const noexcept;
    [[nodiscard]] uint32_t            QueryBankCapacity() const noexcept;
    [[nodiscard]] uint64_t            QueryReadAcquisitionCount() const noexcept;
    [[nodiscard]] uint64_t            QueryReclaimedSlotCount() const noexcept;

private:
    static constexpr uint64_t SlotIndexBitMask     = 0xFFull;              // [mask]  - Lower 8 bits encode active published slot index
    static constexpr uint32_t PendingShiftBits     = 8u;                   // [count] - Bit offset of 24-bit pending reader counter
    static constexpr uint64_t PendingCountMask     = 0xFFFFFFull;          // [mask]  - 24-bit pending reader entry mask
    static constexpr uint64_t PendingReaderUnit    = 1ull << 8u;           // [count] - Atomic increment per entering reader
    static constexpr uint32_t EpochLowShiftBits    = 32u;                  // [count] - Upper 32 bits carry rolling publication epoch

    [[nodiscard]] static constexpr uint64_t PackControlWord(
        uint32_t SlotIndex,
        uint32_t PendingReaders,
        uint32_t EpochLow32) noexcept;
    [[nodiscard]] static constexpr uint32_t ExtractSlotIndex(uint64_t ControlWord) noexcept;
    [[nodiscard]] static constexpr uint32_t ExtractPendingReaders(uint64_t ControlWord) noexcept;

    // 💡 Cache-line alignment isolates each bank slot's reader counter from adjacent slots and writer cursors
    struct alignas(64) SnapshotBankSlot
    {
        mutable std::atomic<int64_t> ActiveReaderBalance { 0 }; // [count] - Signed split-reference balance (0 when safely reclaimable)
        SnapshotEpochHeader          Header              {};    // [-]     - Epoch metadata written before publication
        SnapshotType                 Payload             {};    // [-]     - Recycled immutable-while-published payload instance
    };

    std::array<SnapshotBankSlot, BankSlotCount>   BankSlots              {};      // [-]     - Preallocated snapshot page bank
    alignas(64) mutable std::atomic<uint64_t>     PublishedControl       { 0ull };// [mask]  - Packed (SlotIndex | PendingReaders | Epoch)
    alignas(64) std::atomic<uint64_t>             PublishedGeneration    { 0ull };// [count] - Monotonic publication counter
    alignas(64) mutable std::atomic<uint64_t>     ReadAcquisitionCounter { 0ull };// [count] - Total wait-free leases acquired
    uint32_t                                      CurrentPublishedSlot   = 0u;    // [idx]   - Writer-private index of currently live slot
    uint32_t                                      NextScanCursor         = 1u;    // [idx]   - Writer-private round-robin reclamation cursor
    uint64_t                                      ReclaimedSlotCounter   = 0ull;  // [count] - Writer-private count of recycled bank slots
};

//------------------------------------------------------------------------------------------------------------------------
//                                              TEMPLATE IMPLEMENTATIONS
//------------------------------------------------------------------------------------------------------------------------

template <PublishableSnapshot SnapshotType, uint32_t BankSlotCount>
SnapshotReadLease<SnapshotType, BankSlotCount>::SnapshotReadLease(
    std::atomic<int64_t>* InReaderBalance,
    const SnapshotType*   InPayload,
    SnapshotEpochHeader   InHeader,
    uint32_t              InSlotIndex) noexcept
    : BoundReaderBalance(InReaderBalance)
    , BoundPayload(InPayload)
    , BoundHeader(InHeader)
    , BoundSlotIndex(InSlotIndex)
{
}

template <PublishableSnapshot SnapshotType, uint32_t BankSlotCount>
SnapshotReadLease<SnapshotType, BankSlotCount>::~SnapshotReadLease() noexcept
{
    Release();
}

template <PublishableSnapshot SnapshotType, uint32_t BankSlotCount>
SnapshotReadLease<SnapshotType, BankSlotCount>::SnapshotReadLease(SnapshotReadLease&& IncomingLease) noexcept
    : BoundReaderBalance(std::exchange(IncomingLease.BoundReaderBalance, nullptr))
    , BoundPayload(std::exchange(IncomingLease.BoundPayload, nullptr))
    , BoundHeader(IncomingLease.BoundHeader)
    , BoundSlotIndex(IncomingLease.BoundSlotIndex)
{
}

template <PublishableSnapshot SnapshotType, uint32_t BankSlotCount>
SnapshotReadLease<SnapshotType, BankSlotCount>&
SnapshotReadLease<SnapshotType, BankSlotCount>::operator=(SnapshotReadLease&& IncomingLease) noexcept
{
    if (this != &IncomingLease)
    {
        Release();
        BoundReaderBalance = std::exchange(IncomingLease.BoundReaderBalance, nullptr);
        BoundPayload       = std::exchange(IncomingLease.BoundPayload, nullptr);
        BoundHeader        = IncomingLease.BoundHeader;
        BoundSlotIndex     = IncomingLease.BoundSlotIndex;
    }
    return *this;
}

template <PublishableSnapshot SnapshotType, uint32_t BankSlotCount>
bool SnapshotReadLease<SnapshotType, BankSlotCount>::Valid() const noexcept
{
    return BoundPayload != nullptr;
}

template <PublishableSnapshot SnapshotType, uint32_t BankSlotCount>
const SnapshotType& SnapshotReadLease<SnapshotType, BankSlotCount>::InspectPayload() const noexcept
{
    return *BoundPayload;
}

template <PublishableSnapshot SnapshotType, uint32_t BankSlotCount>
const SnapshotType* SnapshotReadLease<SnapshotType, BankSlotCount>::operator->() const noexcept
{
    return BoundPayload;
}

template <PublishableSnapshot SnapshotType, uint32_t BankSlotCount>
const SnapshotType& SnapshotReadLease<SnapshotType, BankSlotCount>::operator*() const noexcept
{
    return *BoundPayload;
}

template <PublishableSnapshot SnapshotType, uint32_t BankSlotCount>
SnapshotEpochHeader SnapshotReadLease<SnapshotType, BankSlotCount>::InspectHeader() const noexcept
{
    return BoundHeader;
}

template <PublishableSnapshot SnapshotType, uint32_t BankSlotCount>
uint32_t SnapshotReadLease<SnapshotType, BankSlotCount>::QuerySlotIndex() const noexcept
{
    return BoundSlotIndex;
}

template <PublishableSnapshot SnapshotType, uint32_t BankSlotCount>
void SnapshotReadLease<SnapshotType, BankSlotCount>::Release() noexcept
{
    if (BoundReaderBalance != nullptr)
    {
        // 📝 Release ordering publishes completion of all reader loads before the producer may recycle this bank slot
        BoundReaderBalance->fetch_sub(1, std::memory_order_release);
        BoundReaderBalance = nullptr;
        BoundPayload       = nullptr;
    }
}

template <PublishableSnapshot SnapshotType, uint32_t BankSlotCount>
constexpr uint64_t PublishedSnapshotExchange<SnapshotType, BankSlotCount>::PackControlWord(
    uint32_t SlotIndex,
    uint32_t PendingReaders,
    uint32_t EpochLow32) noexcept
{
    return (static_cast<uint64_t>(SlotIndex) & SlotIndexBitMask)
        | ((static_cast<uint64_t>(PendingReaders) & PendingCountMask) << PendingShiftBits)
        | (static_cast<uint64_t>(EpochLow32) << EpochLowShiftBits);
}

template <PublishableSnapshot SnapshotType, uint32_t BankSlotCount>
constexpr uint32_t PublishedSnapshotExchange<SnapshotType, BankSlotCount>::ExtractSlotIndex(uint64_t ControlWord) noexcept
{
    return static_cast<uint32_t>(ControlWord & SlotIndexBitMask);
}

template <PublishableSnapshot SnapshotType, uint32_t BankSlotCount>
constexpr uint32_t PublishedSnapshotExchange<SnapshotType, BankSlotCount>::ExtractPendingReaders(uint64_t ControlWord) noexcept
{
    return static_cast<uint32_t>((ControlWord >> PendingShiftBits) & PendingCountMask);
}

template <PublishableSnapshot SnapshotType, uint32_t BankSlotCount>
PublishedSnapshotExchange<SnapshotType, BankSlotCount>::PublishedSnapshotExchange() noexcept
{
    // 💡 Seed slot 0 with sentinel balance +1 ("currently published") so readers always observe a valid snapshot
    BankSlots[0u].ActiveReaderBalance.store(1, std::memory_order_relaxed);
    PublishedControl.store(PackControlWord(0u, 0u, 0u), std::memory_order_release);
    CurrentPublishedSlot = 0u;
    NextScanCursor       = 1u;
}

template <PublishableSnapshot SnapshotType, uint32_t BankSlotCount>
void PublishedSnapshotExchange<SnapshotType, BankSlotCount>::InitializeSeed(
    const SnapshotType& InitialSnapshot,
    double              SimulationSeconds,
    uint64_t            StepOrdinal) noexcept
    requires std::assignable_from<SnapshotType&, const SnapshotType&>
{
    (void)PublishCopy(InitialSnapshot, SimulationSeconds, StepOrdinal);
}

template <PublishableSnapshot SnapshotType, uint32_t BankSlotCount>
SnapshotType* PublishedSnapshotExchange<SnapshotType, BankSlotCount>::TryAcquireWritableSlot(uint32_t& OutSlotIndex) noexcept
{
    for (uint32_t Attempt = 0u; Attempt < BankSlotCount; ++Attempt)
    {
        const uint32_t Candidate = (NextScanCursor + Attempt) % BankSlotCount;
        if (Candidate == CurrentPublishedSlot)
        {
            continue;
        }
        // 📝 Acquire load synchronizes-with the final retiring reader's release fetch_sub(1) on this slot
        if (BankSlots[Candidate].ActiveReaderBalance.load(std::memory_order_acquire) == 0)
        {
            OutSlotIndex   = Candidate;
            NextScanCursor = (Candidate + 1u) % BankSlotCount;
            ++ReclaimedSlotCounter;
            return &BankSlots[Candidate].Payload;
        }
    }
    return nullptr;
}

template <PublishableSnapshot SnapshotType, uint32_t BankSlotCount>
SnapshotType* PublishedSnapshotExchange<SnapshotType, BankSlotCount>::AcquireWritableSlot(
    uint32_t& OutSlotIndex,
    uint32_t  MaximumSpinYields) noexcept
{
    for (uint32_t YieldOrdinal = 0u; YieldOrdinal <= MaximumSpinYields; ++YieldOrdinal)
    {
        if (SnapshotType* FreePayload = TryAcquireWritableSlot(OutSlotIndex))
        {
            return FreePayload;
        }
        if (YieldOrdinal < MaximumSpinYields)
        {
            std::this_thread::yield();
        }
    }
    return nullptr;
}

template <PublishableSnapshot SnapshotType, uint32_t BankSlotCount>
void PublishedSnapshotExchange<SnapshotType, BankSlotCount>::PublishWritableSlot(
    uint32_t SlotIndex,
    double   SimulationSeconds,
    uint64_t StepOrdinal) noexcept
{
    if (SlotIndex >= BankSlotCount || SlotIndex == CurrentPublishedSlot)
    {
        return;
    }

    const uint64_t NextGeneration = PublishedGeneration.load(std::memory_order_relaxed) + 1ull;

    // ① Stamp header and arm live sentinel (+1) on the newly populated slot before exposing it to readers
    SnapshotBankSlot& IncomingSlot   = BankSlots[SlotIndex];
    IncomingSlot.Header.GenerationSequence = NextGeneration;
    IncomingSlot.Header.SimulationSeconds  = SimulationSeconds;
    IncomingSlot.Header.StepOrdinal        = StepOrdinal;
    IncomingSlot.ActiveReaderBalance.store(1, std::memory_order_relaxed);

    // ② Atomically publish the new slot and harvest the exact reader entry count from the retired slot
    const uint64_t RetiredControl = PublishedControl.exchange(
        PackControlWord(SlotIndex, 0u, static_cast<uint32_t>(NextGeneration & 0xFFFFFFFFull)),
        std::memory_order_acq_rel);

    const uint32_t RetiredSlotIndex = ExtractSlotIndex(RetiredControl);
    const uint32_t EnteredReaders   = ExtractPendingReaders(RetiredControl);

    // ③ Transfer entered readers minus the retired +1 sentinel into the retired slot's balance
    // 💡 acq_rel ensures happens-before visibility even if all readers already decremented before this transfer
    const int64_t NetTransfer = static_cast<int64_t>(EnteredReaders) - 1;
    BankSlots[RetiredSlotIndex].ActiveReaderBalance.fetch_add(NetTransfer, std::memory_order_acq_rel);

    CurrentPublishedSlot = SlotIndex;
    PublishedGeneration.store(NextGeneration, std::memory_order_release);
}

template <PublishableSnapshot SnapshotType, uint32_t BankSlotCount>
template <SnapshotWriteProcedure<SnapshotType> ProcedureType>
bool PublishedSnapshotExchange<SnapshotType, BankSlotCount>::PublishInPlace(
    ProcedureType&& WriterProcedure,
    double          SimulationSeconds,
    uint64_t        StepOrdinal) noexcept
{
    uint32_t WritableSlotIndex = 0u;
    SnapshotType* WritablePayload = AcquireWritableSlot(WritableSlotIndex);
    if (WritablePayload == nullptr)
    {
        return false;
    }
    WriterProcedure(*WritablePayload);
    PublishWritableSlot(WritableSlotIndex, SimulationSeconds, StepOrdinal);
    return true;
}

template <PublishableSnapshot SnapshotType, uint32_t BankSlotCount>
bool PublishedSnapshotExchange<SnapshotType, BankSlotCount>::PublishCopy(
    const SnapshotType& SourceSnapshot,
    double              SimulationSeconds,
    uint64_t            StepOrdinal) noexcept
    requires std::assignable_from<SnapshotType&, const SnapshotType&>
{
    return PublishInPlace(
        [&SourceSnapshot](SnapshotType& Destination) noexcept
        {
            Destination = SourceSnapshot;
        },
        SimulationSeconds,
        StepOrdinal);
}

template <PublishableSnapshot SnapshotType, uint32_t BankSlotCount>
SnapshotReadLease<SnapshotType, BankSlotCount>
PublishedSnapshotExchange<SnapshotType, BankSlotCount>::AcquireReadLease() const noexcept
{
    // 📝 Wait-free single atomic RMW: claims a reader ticket on the live slot and synchronizes-with writer exchange
    const uint64_t ObservedControl = PublishedControl.fetch_add(PendingReaderUnit, std::memory_order_acq_rel);
    const uint32_t SlotIndex       = ExtractSlotIndex(ObservedControl);
    const SnapshotBankSlot& Slot   = BankSlots[SlotIndex];

    ReadAcquisitionCounter.fetch_add(1ull, std::memory_order_relaxed);
    return SnapshotReadLease<SnapshotType, BankSlotCount>(
        &Slot.ActiveReaderBalance,
        &Slot.Payload,
        Slot.Header,
        SlotIndex);
}

template <PublishableSnapshot SnapshotType, uint32_t BankSlotCount>
template <SnapshotReadConsumer<SnapshotType> ConsumerType>
SnapshotEpochHeader PublishedSnapshotExchange<SnapshotType, BankSlotCount>::InspectConsistentSnapshot(
    ConsumerType&& Consumer) const noexcept
{
    const SnapshotReadLease<SnapshotType, BankSlotCount> Lease = AcquireReadLease();
    Consumer(Lease.InspectPayload());
    return Lease.InspectHeader();
}

template <PublishableSnapshot SnapshotType, uint32_t BankSlotCount>
uint64_t PublishedSnapshotExchange<SnapshotType, BankSlotCount>::QueryPublishedGeneration() const noexcept
{
    return PublishedGeneration.load(std::memory_order_acquire);
}

template <PublishableSnapshot SnapshotType, uint32_t BankSlotCount>
SnapshotEpochHeader PublishedSnapshotExchange<SnapshotType, BankSlotCount>::QueryLatestHeader() const noexcept
{
    const SnapshotReadLease<SnapshotType, BankSlotCount> Lease = AcquireReadLease();
    return Lease.InspectHeader();
}

template <PublishableSnapshot SnapshotType, uint32_t BankSlotCount>
uint32_t PublishedSnapshotExchange<SnapshotType, BankSlotCount>::QueryBankCapacity() const noexcept
{
    return BankSlotCount;
}

template <PublishableSnapshot SnapshotType, uint32_t BankSlotCount>
uint64_t PublishedSnapshotExchange<SnapshotType, BankSlotCount>::QueryReadAcquisitionCount() const noexcept
{
    return ReadAcquisitionCounter.load(std::memory_order_relaxed);
}

template <PublishableSnapshot SnapshotType, uint32_t BankSlotCount>
uint64_t PublishedSnapshotExchange<SnapshotType, BankSlotCount>::QueryReclaimedSlotCount() const noexcept
{
    return ReclaimedSlotCounter;
}

} // namespace Frontier::Threading
