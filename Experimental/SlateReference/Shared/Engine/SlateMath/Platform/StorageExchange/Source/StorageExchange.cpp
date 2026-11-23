//============================================================================================================================================
//                                                           STORAGEEXCHANGE.CPP
//============================================================================================================================================
// 🧩 Declared byte ranges over the host's storage surface, drained in declaration order with the latency each took.

#include "SlateMath/Platform/StorageExchange/Api/StorageExchange.h"
#include "SlateMath/Platform/TickSequence/Api/TickSequence.h"

// 📝 Every operating-system conditional in the repository lives under `SlateMath/Platform` — `04` §7.
#if defined(_WIN32)
    #if !defined(WIN32_LEAN_AND_MEAN)
        #define WIN32_LEAN_AND_MEAN
    #endif
    #if !defined(NOMINMAX)
        #define NOMINMAX
    #endif
    #include <windows.h>
#else
    #include <cstdio>
#endif

namespace Slate
{

//------------------------------------------------------------------------------------------------------------------------
//                                                     PATH SPELLING
//------------------------------------------------------------------------------------------------------------------------

namespace
{

#if defined(_WIN32)

// 📝 The same widening `FileInterchange` performs, for the same reason: the narrow entry points interpret a
//    path in the process code page and would name a different file for any artist whose paths are not ASCII.
//    It is duplicated rather than shared because the two components are siblings and neither depends on the
//    other — an include between them for six lines would be a dependency edge bought with nothing.
std::wstring Widen(const std::string& Narrow)
{
    if (Narrow.empty())
        return std::wstring();

    const int Widths = MultiByteToWideChar(CP_UTF8, 0, Narrow.c_str(), static_cast<int>(Narrow.size()),
                                           nullptr, 0);

    if (Widths <= 0)
        return std::wstring();

    std::wstring Widened(static_cast<std::size_t>(Widths), L'\0');

    MultiByteToWideChar(CP_UTF8, 0, Narrow.c_str(), static_cast<int>(Narrow.size()),
                        Widened.data(), Widths);

    return Widened;
}

#endif

}   // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                    OPEN AND RECLAIM
//------------------------------------------------------------------------------------------------------------------------

Deliver<bool> StorageExchange::Open(const std::string& Path)
{
    if (StreamSlot != nullptr)
        return Deliver<bool>::Refuse({ RefusalReason::ExtentExhausted, "this exchange already holds a stream" });

    if (Path.empty())
        return Deliver<bool>::Refuse({ RefusalReason::HostDenied, "an empty path names nothing" });

#if defined(_WIN32)

    const std::wstring Widened = Widen(Path);

    if (Widened.empty())
        return Deliver<bool>::Refuse({ RefusalReason::HostDenied, "the path is not representable" });

    // 📝 FILE_FLAG_RANDOM_ACCESS tells the host not to read ahead sequentially. A codec declaring ranges is
    //    exactly the reader whose next range is not the one after this one, and the read-ahead would spend
    //    the storage device's throughput on bytes nobody asked for.
    const HANDLE Stream = CreateFileW(Widened.c_str(),
                                      GENERIC_READ,
                                      FILE_SHARE_READ,
                                      nullptr,
                                      OPEN_EXISTING,
                                      FILE_ATTRIBUTE_NORMAL | FILE_FLAG_RANDOM_ACCESS,
                                      nullptr);

    if (Stream == INVALID_HANDLE_VALUE)
        return Deliver<bool>::Refuse({ RefusalReason::HostDenied, "the stream could not be opened" });

    LARGE_INTEGER Spanned = {};

    if (GetFileSizeEx(Stream, &Spanned) == FALSE)
    {
        CloseHandle(Stream);
        return Deliver<bool>::Refuse({ RefusalReason::HostDenied, "the storage device rejected the extent" });
    }

    StreamSlot    = Stream;
    StreamSpanned = static_cast<std::uint64_t>(Spanned.QuadPart);

#else

    std::FILE* Stream = std::fopen(Path.c_str(), "rb");

    if (Stream == nullptr)
        return Deliver<bool>::Refuse({ RefusalReason::HostDenied, "the stream could not be opened" });

    std::fseek(Stream, 0, SEEK_END);

    const long Spanned = std::ftell(Stream);

    if (Spanned < 0)
    {
        std::fclose(Stream);
        return Deliver<bool>::Refuse({ RefusalReason::HostDenied, "the storage device rejected the extent" });
    }

    StreamSlot    = Stream;
    StreamSpanned = static_cast<std::uint64_t>(Spanned);

#endif

    DeclaredCount = 0u;
    PendingOrder.clear();
    PendingIndex.clear();
    DrainedRanges.clear();

    return Deliver<bool>::Result(true);
}

void StorageExchange::Reclaim()
{
    if (StreamSlot != nullptr)
    {
#if defined(_WIN32)
        CloseHandle(static_cast<HANDLE>(StreamSlot));
#else
        std::fclose(static_cast<std::FILE*>(StreamSlot));
#endif
        StreamSlot = nullptr;
    }

    StreamSpanned = 0u;
    DeclaredCount = 0u;
    PendingOrder.clear();
    PendingIndex.clear();
    DrainedRanges.clear();
}

StorageExchange::~StorageExchange()
{
    Reclaim();
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    DECLARED RANGES
//------------------------------------------------------------------------------------------------------------------------

Deliver<std::uint32_t> StorageExchange::Declare(RangeRequest Wanted)
{
    if (StreamSlot == nullptr)
        return Deliver<std::uint32_t>::Refuse({ RefusalReason::HostDenied, "no stream is open" });

    if (Wanted.SpannedBytes == 0u)
        return Deliver<std::uint32_t>::Refuse({ RefusalReason::HostDenied, "an empty range asks for nothing" });

    if (Wanted.SpannedBytes > RangeLimit)
        return Deliver<std::uint32_t>::Refuse({ RefusalReason::ExtentExhausted,
                                                "the range spans beyond the declared ceiling" });

    // 🔴 An offset **at** the extent is beyond the stream and refuses; a range that merely reaches past the end
    //    is truncated on arrival and delivers what is there. The two are different facts — the first is a
    //    reader that has lost track of the stream, the second is one reading its last range.
    if (Wanted.Offset >= StreamSpanned)
        return Deliver<std::uint32_t>::Refuse({ RefusalReason::ExtentExhausted,
                                                "the offset lies beyond the stream" });

    const std::uint32_t Registered = DeclaredCount;

    ++DeclaredCount;

    PendingOrder.push_back(Wanted);
    PendingIndex.push_back(Registered);

    return Deliver<std::uint32_t>::Result(Registered);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                       THE DRAIN
//------------------------------------------------------------------------------------------------------------------------

const std::vector<RangeArrival>& StorageExchange::Drain()
{
    DrainedRanges.clear();

    if (StreamSlot == nullptr || PendingOrder.empty())
        return DrainedRanges;

    // 📝 The timeline is constructed here rather than borrowed, because the latency reported is a **span**
    //    between two readings of it and a span is independent of where the origin sits. Borrowing the
    //    process timeline would make this component depend on bring-up order for a figure that does not
    //    need it.
    const TickSequence LatencyTimeline;

    DrainedRanges.reserve(PendingOrder.size());

    for (std::size_t Index = 0u; Index < PendingOrder.size(); ++Index)
    {
        const RangeRequest& Wanted = PendingOrder[Index];

        RangeArrival Sampled;
        Sampled.Declared = PendingIndex[Index];
        Sampled.Offset   = Wanted.Offset;

        const TickPoint Began = LatencyTimeline.Advance();

        // 📐 The read extent is the declared one clipped to what remains of the stream. A range reaching past
        //    the end is truncated rather than rejected, and the truncation is reported so a codec can tell a
        //    short last range from a storage device that gave up halfway through one.
        const std::uint64_t Remaining = StreamSpanned - Wanted.Offset;
        const std::uint64_t Reading   = Wanted.SpannedBytes > Remaining ? Remaining : Wanted.SpannedBytes;

        Sampled.Received.resize(static_cast<std::size_t>(Reading));

        bool Read = true;

#if defined(_WIN32)

        const HANDLE Stream = static_cast<HANDLE>(StreamSlot);

        OVERLAPPED Positioned  = {};
        Positioned.Offset      = static_cast<DWORD>(Wanted.Offset & 0xFFFFFFFFull);
        Positioned.OffsetHigh  = static_cast<DWORD>(Wanted.Offset >> 32);

        std::uint64_t Received = 0u;

        while (Received < Reading)
        {
            const std::uint64_t Outstanding = Reading - Received;
            const DWORD         Asked       = Outstanding > 0x40000000ull ? 0x40000000u
                                                                          : static_cast<DWORD>(Outstanding);

            const std::uint64_t Positioning = Wanted.Offset + Received;

            Positioned.Offset     = static_cast<DWORD>(Positioning & 0xFFFFFFFFull);
            Positioned.OffsetHigh = static_cast<DWORD>(Positioning >> 32);

            DWORD Delivered = 0u;

            if (ReadFile(Stream, Sampled.Received.data() + Received, Asked, &Delivered, &Positioned) == FALSE
                || Delivered == 0u)
            {
                Read = false;
                break;
            }

            Received += Delivered;
        }

#else

        std::FILE* Stream = static_cast<std::FILE*>(StreamSlot);

        if (std::fseek(Stream, static_cast<long>(Wanted.Offset), SEEK_SET) != 0)
        {
            Read = false;
        }
        else
        {
            const std::size_t Received = std::fread(Sampled.Received.data(), 1u, Sampled.Received.size(), Stream);

            Read = Received == Sampled.Received.size();
        }

#endif

        const TickPoint Ended = LatencyTimeline.Advance();

        Sampled.LatencyMillis = TickSequence::Span(Began, Ended);

        if (!Read)
        {
            // 🔴 Nothing landed, so nothing is delivered. Handing back a partially filled extent with a
            //    Rejected conclusion would leave a codec free to read bytes the storage device never
            //    produced, and the zeros it would find decode as content rather than as an absence.
            Sampled.Received.clear();
            Sampled.Completed = RangeConclusion::Rejected;
        }
        else
        {
            Sampled.Completed = Reading < Wanted.SpannedBytes ? RangeConclusion::Truncated
                                                              : RangeConclusion::Delivered;
        }

        DrainedRanges.push_back(std::move(Sampled));
    }

    PendingOrder.clear();
    PendingIndex.clear();

    return DrainedRanges;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                       READINGS
//------------------------------------------------------------------------------------------------------------------------

std::uint32_t StorageExchange::PendingCount() const
{
    return static_cast<std::uint32_t>(PendingOrder.size());
}

std::uint64_t StorageExchange::SpannedBytes() const
{
    return StreamSpanned;
}

}   // namespace Slate
