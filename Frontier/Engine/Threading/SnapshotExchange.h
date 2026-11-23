/*==========================================================================================================================================
                                                 LOCK-FREE TRIPLE-BUFFER SNAPSHOT EXCHANGE
                                     ------------------------------------------------------------------
                                     One producer publishes, one consumer takes the latest whole copy.
==========================================================================================================================================*/

// 🧩 Lock-free triple-buffer snapshot exchange: one producer publishes, one consumer takes the latest whole copy.

#pragma once

#include <atomic>
#include <cstdint>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    SNAPSHOT EXCHANGE
//------------------------------------------------------------------------------------------------------------------------

// The game-division → render-division handshake. Three slots rotate through write / ready / read ownership
// packed into one atomic word, so the producer never waits and the consumer never observes a torn payload:
// a slot is only ever touched by its current owner. Single producer, single consumer.
template <typename Payload>
class SnapshotExchange
{
  public:
    SnapshotExchange() noexcept
        : State(Pack(0, 1, 2, false))
    {
    }

    SnapshotExchange(const SnapshotExchange&)            = delete;
    SnapshotExchange& operator=(const SnapshotExchange&) = delete;

    // Producer side: copy into the write slot, then swap write↔ready and raise the fresh flag.
    void Publish(const Payload& Value) noexcept
    {
        uint32_t Current = State.load(std::memory_order_acquire);
        Slots[WriteIndex(Current)] = Value;

        for (;;)
        {
            const uint32_t Next = Pack(ReadyIndex(Current), WriteIndex(Current), ReadIndex(Current), true);
            if (State.compare_exchange_weak(Current, Next, std::memory_order_acq_rel))
            {
                return;
            }
            // The consumer swapped ready↔read meanwhile; our write slot index is unchanged (only the
            // producer ever moves it), so the payload is already in the right slot — just retry the swap.
        }
    }

    // Consumer side: when fresh data exists, swap ready↔read and copy the new read slot out.
    // Returns false when nothing new was published since the last successful Take.
    [[nodiscard]] bool Take(Payload& Out) noexcept
    {
        uint32_t Current = State.load(std::memory_order_acquire);
        for (;;)
        {
            if (!Fresh(Current))
            {
                return false;
            }
            const uint32_t Next = Pack(WriteIndex(Current), ReadIndex(Current), ReadyIndex(Current), false);
            if (State.compare_exchange_weak(Current, Next, std::memory_order_acq_rel))
            {
                Out = Slots[ReadIndex(Next)];
                return true;
            }
        }
    }

    [[nodiscard]] bool QueryFresh() const noexcept
    {
        return Fresh(State.load(std::memory_order_acquire));
    }

  private:
    static constexpr uint32_t Pack(uint32_t Write, uint32_t Ready, uint32_t Read, bool IsFresh) noexcept
    {
        return (Write & 3u) | ((Ready & 3u) << 2) | ((Read & 3u) << 4) | (IsFresh ? 1u << 6 : 0u);
    }
    static constexpr uint32_t WriteIndex(uint32_t Packed) noexcept { return Packed & 3u; }
    static constexpr uint32_t ReadyIndex(uint32_t Packed) noexcept { return (Packed >> 2) & 3u; }
    static constexpr uint32_t ReadIndex(uint32_t Packed) noexcept  { return (Packed >> 4) & 3u; }
    static constexpr bool     Fresh(uint32_t Packed) noexcept       { return (Packed & (1u << 6)) != 0; }

    Payload               Slots[3];   // [payloads] - Rotating ownership: write / ready / read
    std::atomic<uint32_t> State;      // [-] - Packed indices + fresh flag
};

} // namespace Frontier
