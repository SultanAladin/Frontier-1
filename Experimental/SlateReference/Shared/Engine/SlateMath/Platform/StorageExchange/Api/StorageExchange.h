//============================================================================================================================================
//                                                            STORAGEEXCHANGE.H
//============================================================================================================================================
// 🧩 Byte ranges incoming from the storage device, so a decode is driven by range arrival rather than by whole-stream completion.

#pragma once

#include "Foundation/DeliveryGuarantee.h"

#include <cstdint>
#include <string>
#include <vector>

namespace Slate
{

//------------------------------------------------------------------------------------------------------------------------
//                                                     A DECLARED RANGE
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 One byte range a reader asked the storage device for.
/// tag   nonallocating, nonthrowing
struct RangeRequest
{
    std::uint64_t  Offset       = 0u;   // [B] - where in the stream the range begins
    std::uint64_t  SpannedBytes = 0u;   // [B] - how much of it is wanted
};

/// 🧩 How one declared range ended.
/// note  ⚠️ Truncated is delivered and not rejected. A range that reached the end of the stream carries fewer
///       bytes than were asked for and every one of them is good; a codec reading the last range of a stream
///       asks for its own read extent and is handed what remains.
/// tag   guarantee
enum class RangeConclusion : std::uint32_t
{
    Pending   = 0u,   // [-] - declared; the storage device has not answered
    Delivered = 1u,   // [-] - the whole declared range landed
    Truncated = 2u,   // [-] - the stream ended inside the range; what landed is good
    Rejected  = 3u    // [-] - the storage device rejected; nothing landed
};

/// 🧩 One range as it came back, and how long the storage device took over it.
/// note  📝 The latency is carried per range rather than per stream, because `20`'s residency decides what to
///        ask for next from how long the last answer took — and a stream's mean latency is the one figure
///        that tells it nothing about the range it is about to declare.
/// tag   owning
struct RangeArrival
{
    std::uint32_t              Declared     = 0u;                        // [-]  - the ordinal Declare registered
    RangeConclusion            Completed    = RangeConclusion::Pending;  // [-]  - how it ended
    std::uint64_t              Offset       = 0u;                        // [B]  - where the landed bytes begin
    std::vector<std::uint8_t>  Received       = {};                        // [-]  - what arrived; owned by the reader
    double                     LatencyMillis = 0.0;                      // [ms] - declaration to arrival
};

//------------------------------------------------------------------------------------------------------------------------
//                                                     THE TRANSLATION
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 One opened stream, read by declared range and drained in arrival order.
/// note  🔴 This surface exists so a decode is driven by **range arrival** rather than by whole-stream
///        completion — `04` §4. A codec that waited for the whole stream would hold the tick for as long as
///        the largest document takes to read, and `34` §3 applies results on the tick.
/// note  ⚠️ 🚧 Ranges are read synchronously inside `Drain` for now, and the latency reported is the real one
///        that read took. The surface is the asynchronous one deliberately: every caller already declares,
///        goes away, and drains, so the read moving onto `34`'s workers changes nothing above this line.
///        Writing it the other way round would put a whole-stream assumption into every codec first.
/// tag   owning
class StorageExchange
{
public:

    StorageExchange()                                  = default;
    StorageExchange(const StorageExchange&)            = delete;
    StorageExchange& operator=(const StorageExchange&) = delete;
    ~StorageExchange();

    /// 🧩 Opens one stream for reading by range.
    /// in    Path     [-]  UTF-8
    /// out   Result  [-]  refuses with HostDenied when the stream cannot be opened, and with
    ///                     ExtentExhausted when this exchange already holds one
    /// cost  🚩
    /// tag   api, nonthrowing
    Deliver<bool> Open(const std::string& Path);

    /// 🧩 Closes the held stream and discards every range not yet drained.
    /// cost  ✔️
    /// tag   api, nonthrowing
    void Reclaim();

    /// 🧩 Declares one range the reader wants.
    /// in    Wanted   [-]  the offset and extent
    /// out   Result  [-]  the ordinal this range is drained under; refuses with HostDenied when no stream
    ///                     is open, and with ExtentExhausted when the offset lies beyond the stream
    /// note  📝 Declaring is not reading. The ordinal is registered here and the bytes arrive at a later Drain,
    ///        which is what lets a codec declare the next range while it decodes the one it has.
    /// cost  ✔️
    /// tag   api, nonthrowing
    Deliver<std::uint32_t> Declare(RangeRequest Wanted);

    /// 🧩 Delivers every range that has arrived since the last drain, in declaration order.
    /// out   Arrivals  [-]  ordered by declaration ordinal, never by which range the device answered first
    /// note  🔴 Declaration order and not arrival order. A storage device answers a short range faster than a
    ///        long one whatever order they were asked in, and a codec reassembling a stream from device order
    ///        would produce a different result on a device with a different reordering rule.
    /// cost  🔴
    /// tag   api, nonthrowing
    const std::vector<RangeArrival>& Drain();

    /// 🧩 How many declared ranges have not yet been drained.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    std::uint32_t PendingCount() const;

    /// 🧩 The whole extent of the open stream.
    /// out   SpannedBytes  [B]  zero while no stream is open
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    std::uint64_t SpannedBytes() const;

    // 📝 A single range beyond this refuses. A reader wanting more than this wants the whole stream, and
    //    `FileInterchange::ReadStream` is the surface that says so.
    static constexpr std::uint64_t RangeLimit = 256ull * 1024ull * 1024ull;   // [B] - largest declared range

private:

    void*                      StreamSlot     = nullptr;   // [-] - opaque; the host spelling stays in the source
    std::uint64_t              StreamSpanned  = 0u;        // [B] - the whole extent, read once at Open
    std::uint32_t              DeclaredCount  = 0u;        // [-] - ordinals registered over this stream's lifetime
    std::vector<RangeRequest>  PendingOrder   = {};        // [-] - declared, undrained, in declaration order
    std::vector<std::uint32_t> PendingIndex = {};        // [-] - the ordinal each pending range was registered
    std::vector<RangeArrival>  DrainedRanges  = {};        // [-] - what the last Drain delivered
};

}   // namespace Slate
