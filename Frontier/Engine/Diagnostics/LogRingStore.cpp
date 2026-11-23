/*==========================================================================================================================================
                                                               LOG RING STORE
                                     ------------------------------------------------------------------
                      Ring-store writes publish by sequence; reads re-verify the sequence after copying to refuse torn
                                                                   slots.
==========================================================================================================================================*/

// 🧩 Ring-store writes publish by sequence; reads re-verify the sequence after copying to refuse torn slots.

#include "LogRingStore.h"

#include <algorithm>
#include <chrono>
#include <cstring>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    INTERNAL FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

namespace
{

void CopyTruncated(char* Destination, size_t DestinationSize, std::string_view Text) noexcept
{
    if (DestinationSize == 0)
    {
        return;
    }
    const size_t Count = std::min(Text.size(), DestinationSize - 1);
    if (Count > 0)
    {
        std::memcpy(Destination, Text.data(), Count);
    }
    Destination[Count] = '\0';
}

uint64_t SystemClockMicros() noexcept
{
    const auto Now = std::chrono::system_clock::now().time_since_epoch();
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(Now).count());
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

LogRingStore::LogRingStore()
    : Slots(std::make_unique<Slot[]>(Capacity))
{
}

LogRingStore::~LogRingStore() = default;

uint64_t LogRingStore::Write(uint32_t     Category,
                             LogSeverity  Severity,
                             LogVerbosity Verbosity,
                             std::string_view Message,
                             std::string_view Source) noexcept
{
    return WriteAt(Category, Severity, Verbosity, Message, Source, SystemClockMicros());
}

uint64_t LogRingStore::WriteAt(uint32_t     Category,
                               LogSeverity  Severity,
                               LogVerbosity Verbosity,
                               std::string_view Message,
                               std::string_view Source,
                               uint64_t     WallMicros) noexcept
{
    const uint64_t Sequence = NextSequence.fetch_add(1, std::memory_order_relaxed) + 1;
    Slot&          Target   = Slots[(Sequence - 1) & (Capacity - 1)];

    Target.Entry.Sequence   = Sequence;
    Target.Entry.WallMicros = WallMicros;
    Target.Entry.Category   = Category;
    Target.Entry.Severity   = Severity;
    Target.Entry.Verbosity  = Verbosity;
    CopyTruncated(Target.Entry.Message, sizeof(Target.Entry.Message), Message);
    CopyTruncated(Target.Entry.Source, sizeof(Target.Entry.Source), Source);

    // 📝 Release-store publishes the payload writes above to any reader that acquire-loads this sequence.
    Target.WrittenSequence.store(Sequence, std::memory_order_release);
    return Sequence;
}

uint64_t LogRingStore::QueryWritten() const noexcept
{
    return NextSequence.load(std::memory_order_acquire);
}

uint64_t LogRingStore::QueryOldestSequence() const noexcept
{
    const uint64_t Written = QueryWritten();
    if (Written == 0)
    {
        return 0;
    }
    return Written > Capacity ? Written - Capacity + 1 : 1;
}

uint64_t LogRingStore::QueryRetainedCount() const noexcept
{
    const uint64_t Written = QueryWritten();
    return Written > Capacity ? Capacity : Written;
}

uint32_t LogRingStore::ReadSince(uint64_t  AfterSequence,
                                 LogEntry* Out,
                                 uint32_t  OutCapacity,
                                 uint64_t& NewCursor,
                                 uint64_t& SkippedSequences) const noexcept
{
    NewCursor        = AfterSequence;
    SkippedSequences = 0;
    if (Out == nullptr || OutCapacity == 0)
    {
        return 0;
    }

    const uint64_t Written = QueryWritten();
    uint64_t       Cursor  = AfterSequence;
    if (Cursor >= Written)
    {
        NewCursor = Written;
        return 0;
    }

    // 📝 Anything older than the oldest retained sequence was overwritten before this reader arrived.
    const uint64_t Oldest = QueryOldestSequence();
    if (Cursor + 1 < Oldest)
    {
        SkippedSequences += Oldest - (Cursor + 1);
        Cursor            = Oldest - 1;
    }

    uint32_t Count = 0;
    while (Cursor < Written && Count < OutCapacity)
    {
        const uint64_t Wanted = Cursor + 1;
        const Slot&    Source = Slots[(Wanted - 1) & (Capacity - 1)];

        const uint64_t Before = Source.WrittenSequence.load(std::memory_order_acquire);
        if (Before != Wanted)
        {
            // 📝 Not yet published (a writer claimed it but is still formatting) — stop here and retry next call,
            //    unless it was already overwritten by a full lap, in which case it is gone for good.
            if (Before < Wanted)
            {
                break;
            }
            SkippedSequences += 1;
            Cursor            = Wanted;
            continue;
        }

        LogEntry Copy               = Source.Entry;
        const uint64_t After        = Source.WrittenSequence.load(std::memory_order_acquire);
        Cursor                      = Wanted;
        if (After != Wanted || Copy.Sequence != Wanted)
        {
            SkippedSequences += 1;   // [entries] - Torn copy refused; the slot lapped this reader
            continue;
        }

        Out[Count] = Copy;
        Count     += 1;
    }

    NewCursor = Cursor;
    return Count;
}

} // namespace Frontier
