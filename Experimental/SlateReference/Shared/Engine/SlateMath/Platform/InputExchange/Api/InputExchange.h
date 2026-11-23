//============================================================================================================================================
//                                                             INPUTEXCHANGE.H
//============================================================================================================================================
// 🧩 Timestamped device samples crossing in, with absent axes distinguishable from zero-valued ones.

#pragma once

#include "Foundation/DeliveryGuarantee.h"
#include "SlateMath/Platform/TickSequence/Api/TickSequence.h"

#include <cstdint>

namespace Slate
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    AXIS PRESENCE
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 Which optional axes the reporting device supplied on one sample.
/// note  🔴 Absent is distinct from zero. A tablet reporting no tilt and a stylus held perfectly upright
///       are different facts, and `22` treats them differently.
/// tag   nonallocating, nonthrowing
struct AxisPresence
{
    bool  PressureReported = false;   // [-] - the device supplied a pressure reading
    bool  TiltReported     = false;   // [-] - the device supplied both tilt angles
    bool  RotationReported = false;   // [-] - the device supplied a barrel rotation
};

//------------------------------------------------------------------------------------------------------------------------
//                                                      ONE SAMPLE
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 One pointer sample, stamped at arrival by `TickSequence`.
/// note  Stamped at arrival, never at consumption. A stroke sampled at device rate and consumed at display
///       rate reconstructs only if the arrival stamps survive.
/// tag   nonallocating, nonthrowing
struct PointerSample
{
    TickPoint     Arrival     = {};       // [ns]  - stamped by TickSequence when the sample crossed in
    double        PositionX   = 0.0;      // [px]  - in the window's drawable extent
    double        PositionY   = 0.0;      // [px]  - in the window's drawable extent
    double        Pressure    = 0.0;      // [-]   - normalised; meaningful only when reported
    double        TiltX   = 0.0;      // [deg] - meaningful only when reported
    double        TiltY  = 0.0;      // [deg] - meaningful only when reported
    double        Rotation    = 0.0;      // [deg] - barrel rotation; meaningful only when reported
    AxisPresence  Supplied    = {};       // [-]   - read this before reading any optional axis above
    std::uint32_t ContactMask = 0u;       // [-]   - bit per pointer contact currently down
};

//------------------------------------------------------------------------------------------------------------------------
//                                                 THE ARRIVAL SEQUENCE
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 The bounded arrival ordering of pointer samples, drained once per tick by the consumer.
/// note  ⏱️ Bounded and non-allocating: the oldest sample is discarded when the extent is full. A stroke
///       that outruns the drain loses its oldest samples, which is visible, rather than allocating during
///       an interaction, which is not.
/// tag   owning
class InputExchange
{
public:

    static constexpr std::uint32_t ArrivalCapacity = 4096u;   // [-] - samples held between two drains

    InputExchange()                                = default;
    InputExchange(const InputExchange&)            = delete;
    InputExchange& operator=(const InputExchange&) = delete;
    ~InputExchange();

    /// 🧩 Takes the native window's pointer stream and records every sample the device reports on it.
    /// in    NativeWindowSlot  [-]  `WindowInterchange::NativeHandle`; borrowed and outlives this attachment
    /// in    HostTimeline      [-]  the process's one timeline, for arrival stamps
    /// out   Result           [-]  refuses with HostDenied when the window declines the attachment, and with
    ///                              ExtentExhausted when this exchange is already attached
    /// note  🔴 The axes are read from the operating system's own pointer surface and not from the window
    ///        system's, because the window system reports a position and nothing else. `04` §3 requires an
    ///        unreported axis to stay distinguishable from a zero-valued one, and the operating system is the
    ///        only surface in the chain that states which axes the device actually supplied.
    /// note  ⚠️ Chains to whatever the window system already installed rather than replacing it. `14`'s
    ///        interface reads the same device through the window system's accumulated condition, and a
    ///        replacement here would take that stream away from it silently.
    /// cost  🚩
    /// tag   api, nonthrowing
    Deliver<bool> Attach(void* NativeWindowSlot, const TickSequence& HostTimeline);

    /// 🧩 Returns the pointer stream to whoever held it before this attachment.
    /// note  Called by the destructor as well, so an exchange that outlives its window releases nothing twice.
    /// cost  ✔️
    /// tag   api, nonthrowing
    void Detach();

    /// 🧩 Stamps an arrival against the attached timeline, for a device that reported no reading of its own.
    /// out   TickPoint  [ns]  the process origin while nothing is attached
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    TickPoint ArrivalStamp() const;

    /// 🧩 Stamps an arrival from the device's own host-counter reading.
    /// in    HostCount  [-]   the device's reading; zero falls back to the timeline
    /// out   TickPoint  [ns]  the process origin while nothing is attached
    /// note  🔴 `04` §3 stamps at arrival and never at consumption, and the device's own reading is the
    ///        earliest arrival there is. Taking the timeline here instead would stamp the sample when the
    ///        process drained the message carrying it, which is the consumption rate wearing an arrival's name.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    TickPoint ArrivalStamp(std::uint64_t HostCount) const;

    /// 🧩 Records one incoming sample against the supplied timeline.
    /// in    Incoming   [-]  the sample as the device reported it
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    void Record(const PointerSample& Incoming);

    /// 🧩 Reads one held sample in arrival order.
    /// in    ArrivalIndex [-]  zero is the oldest sample still held
    /// pre   ArrivalIndex is below HeldCount
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    const PointerSample& Sample(std::uint32_t ArrivalIndex) const;

    /// 🧩 How many samples are held.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    std::uint32_t HeldCount() const;

    /// 🧩 Discards every held sample. Called by the consumer once it has read them.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    void Reclaim();

private:

    PointerSample        ArrivalOrder[ArrivalCapacity] = {};        // [-] - cyclic; oldest discarded when full
    std::uint32_t        OldestIndex                 = 0u;        // [-] - where the oldest held sample sits
    std::uint32_t        OccupiedCount                 = 0u;        // [-] - how many are held
    void*                AttachedWindowSlot            = nullptr;   // [-] - the native window this reads from
    void*                PrecedingReceiver             = nullptr;   // [-] - what held the stream before Attach
    const TickSequence*  Timeline                      = nullptr;   // [-] - borrowed; stamps every arrival
};

}   // namespace Slate
