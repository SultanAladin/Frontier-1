/*============================================================================================================================================
                                                           THOUGHTLOG.CPP
============================================================================================================================================*/
// 🧠 Thought log implementation: MPSC sequence-stamped ring buffer in RAM + background aggregation thread.
//    Nothing touches the disk until Shutdown() flushes the aggregated report at application close.

#include "Engine/Lifecycle/ThoughtLog.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <memory>
#include <mutex>
#include <numeric>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace Frontier::Diagnostics
{
namespace
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct ThoughtRingSlot
{
    std::atomic<uint64_t> Stamp      { 0u }; // [-] - Sequence stamp: slot holds record (Stamp - 1) once published
    std::atomic<uint64_t> DurationNs { 0u }; // [ns] - Measured scope duration
    std::atomic<uint32_t> NameId     { 0u }; // [-] - Interned scope name id
    std::atomic<uint32_t> ThreadHash { 0u }; // [-] - Hashed std::thread::id of the recording thread
};

struct ThoughtNameStats
{
    std::string Name                                {};
    uint64_t    Calls                               = 0u;
    uint64_t    TotalNs                             = 0u;
    uint64_t    MinNs                               = UINT64_MAX;
    uint64_t    MaxNs                               = 0u;
    long double SumSquaresNs                        = 0.0L;
    static constexpr uint64_t   ReservoirCapacity   = 1024u;
    std::array<uint64_t, 1024u> Reservoir           {}; // [ns] - Reservoir sample for percentile estimates
    uint64_t    ReservoirSeen                       = 0u;
    uint64_t    ReservoirState                      = 0x9E3779B97F4A7C15ull;
};

struct ThoughtLogState
{
    static constexpr uint64_t RingCapacity = 1ull << 20; // 1,048,576 records (~24 MiB) held in RAM

    std::unique_ptr<ThoughtRingSlot[]>        Ring              {};
    std::atomic<uint64_t>                     WriteCursor       { 0u };
    std::atomic<uint64_t>                     OverwrittenCount  { 0u };
    std::atomic<bool>                         Running           { false };
    std::atomic<bool>                         Closed            { false };
    std::once_flag                            InitOnce          {};
    std::thread                               Worker            {};
    std::mutex                                WorkerMutex       {};
    std::condition_variable                   WorkerWake        {};
    std::atomic<bool>                         WorkerStop        { false };
    std::chrono::steady_clock::time_point     SessionStart      {};
    uint64_t                                  WorkerReadCursor  = 0u; // Worker-thread only
    std::mutex                                NameMutex         {};
    std::unordered_map<std::string, uint32_t> NameToId          {};
    std::vector<std::string>                  IdToName          {};
    std::vector<ThoughtNameStats>             Stats             {}; // Written by worker; read at flush under StatsMutex
    std::mutex                                StatsMutex        {};
};

// Meyer's singleton: safe to touch from any static initialization order or thread.
ThoughtLogState& AccessState()
{
    static ThoughtLogState State {};
    return State;
}

uint32_t HashCurrentThread() noexcept
{
    return static_cast<uint32_t>(std::hash<std::thread::id>{}(std::this_thread::get_id()));
}

uint64_t NextReservoirRandom(ThoughtNameStats& Stats) noexcept
{
    // xorshift64* — cheap deterministic stream for reservoir replacement decisions
    uint64_t X = Stats.ReservoirState;
    X ^= X >> 12;
    X ^= X << 25;
    X ^= X >> 27;
    Stats.ReservoirState = X;
    return X * 0x2545F4914F6CDD1Dull;
}

// Folds every published-but-unconsumed ring record into the per-name statistics (worker thread only).
void DrainRingIntoStats(ThoughtLogState& State)
{
    uint64_t Cursor = State.WorkerReadCursor;
    for (;;)
    {
        ThoughtRingSlot& Slot = State.Ring[static_cast<std::size_t>(Cursor & (ThoughtLogState::RingCapacity - 1u))];
        const uint64_t Stamp = Slot.Stamp.load(std::memory_order_acquire);
        if (Stamp == 0u || Stamp - 1u < Cursor)
        {
            break; // Next record not published yet
        }
        if (Stamp - 1u > Cursor)
        {
            // Producer lapped the worker: skip the overwritten gap and resynchronize
            State.OverwrittenCount.fetch_add((Stamp - 1u) - Cursor, std::memory_order_relaxed);
            Cursor = Stamp - 1u;
            continue;
        }

        const uint64_t DurationNs = Slot.DurationNs.load(std::memory_order_relaxed);
        const uint32_t NameId     = Slot.NameId.load(std::memory_order_relaxed);
        ++Cursor;

        std::lock_guard<std::mutex> StatsLock(State.StatsMutex);
        if (NameId >= State.Stats.size())
        {
            continue;
        }
        ThoughtNameStats& Entry = State.Stats[NameId];
        Entry.Calls   += 1u;
        Entry.TotalNs += DurationNs;
        Entry.MinNs    = std::min(Entry.MinNs, DurationNs);
        Entry.MaxNs    = std::max(Entry.MaxNs, DurationNs);
        Entry.SumSquaresNs += static_cast<long double>(DurationNs) * static_cast<long double>(DurationNs);

        constexpr uint64_t ReservoirCap = 1024u;
        ++Entry.ReservoirSeen;
        if (Entry.ReservoirSeen <= ReservoirCap)
        {
            Entry.Reservoir[static_cast<std::size_t>(Entry.ReservoirSeen - 1u)] = DurationNs;
        }
        else
        {
            const uint64_t Pick = NextReservoirRandom(Entry) % Entry.ReservoirSeen;
            if (Pick < ReservoirCap)
            {
                Entry.Reservoir[static_cast<std::size_t>(Pick)] = DurationNs;
            }
        }
    }
    State.WorkerReadCursor = Cursor;
}

void AggregatorThreadMain()
{
    ThoughtLogState& State = AccessState();
    while (!State.WorkerStop.load(std::memory_order_acquire))
    {
        {
            std::unique_lock<std::mutex> Lock(State.WorkerMutex);
            State.WorkerWake.wait_for(Lock, std::chrono::milliseconds(50));
        }
        DrainRingIntoStats(State);
    }
    DrainRingIntoStats(State); // Final drain so the closing report loses nothing
}

void StartOnce()
{
    ThoughtLogState& State = AccessState();
    if (State.Closed.load(std::memory_order_acquire))
    {
        return;
    }
    State.Ring = std::make_unique<ThoughtRingSlot[]>(static_cast<std::size_t>(ThoughtLogState::RingCapacity));
    State.SessionStart = std::chrono::steady_clock::now();
    State.WorkerStop.store(false, std::memory_order_release);
    State.Running.store(true, std::memory_order_release);
    State.Worker = std::thread(&AggregatorThreadMain);
    std::atexit([]() { ThoughtLog::Shutdown(); }); // Flush at application close even without an explicit call
}

uint64_t PercentileNs(const ThoughtNameStats& Entry, double Fraction)
{
    const uint64_t Count = std::min(Entry.ReservoirSeen, ThoughtNameStats::ReservoirCapacity);
    if (Count == 0u)
    {
        return 0u;
    }
    std::array<uint64_t, 1024u> Sorted {};
    std::copy_n(Entry.Reservoir.begin(), static_cast<std::size_t>(Count), Sorted.begin());
    std::sort(Sorted.begin(), Sorted.begin() + static_cast<std::ptrdiff_t>(Count));
    const auto Index = static_cast<std::size_t>(std::clamp(Fraction, 0.0, 1.0) * static_cast<double>(Count - 1u));
    return Sorted[Index];
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                    THOUGHT LOG
//------------------------------------------------------------------------------------------------------------------------

void ThoughtLog::Initialize()
{
    ThoughtLogState& State = AccessState();
    std::call_once(State.InitOnce, &StartOnce);
}

void ThoughtLog::Shutdown()
{
    ThoughtLogState& State = AccessState();
    if (State.Closed.exchange(true, std::memory_order_acq_rel))
    {
        return; // Already closed
    }
    Initialize(); // No-op if already started; guarantees state exists even if never sampled
    if (!State.Running.load(std::memory_order_acquire))
    {
        return;
    }
    State.WorkerStop.store(true, std::memory_order_release);
    State.WorkerWake.notify_all();
    if (State.Worker.joinable())
    {
        State.Worker.join();
    }
    State.Running.store(false, std::memory_order_release);

    const std::filesystem::path ReportPath = std::filesystem::current_path() / "Logs" / "FrontierThoughtLog.txt";
    FlushReport(ReportPath);
}

uint32_t ThoughtLog::ResolveNameId(const char* Name)
{
    ThoughtLogState& State = AccessState();
    const std::string Key = (Name != nullptr) ? Name : "<unnamed>";
    std::lock_guard<std::mutex> Lock(State.NameMutex);
    const auto Found = State.NameToId.find(Key);
    if (Found != State.NameToId.end())
    {
        return Found->second;
    }
    const auto NewId = static_cast<uint32_t>(State.IdToName.size());
    State.NameToId.emplace(Key, NewId);
    State.IdToName.push_back(Key);
    {
        std::lock_guard<std::mutex> StatsLock(State.StatsMutex);
        State.Stats.push_back(ThoughtNameStats{});
        State.Stats.back().Name = Key;
    }
    return NewId;
}

void ThoughtLog::RecordScopeSample(uint32_t NameId, uint64_t DurationNs) noexcept
{
    ThoughtLogState& State = AccessState();
    if (State.Closed.load(std::memory_order_relaxed))
    {
        return;
    }
    if (!State.Running.load(std::memory_order_relaxed))
    {
        Initialize();
        if (!State.Running.load(std::memory_order_relaxed))
        {
            return;
        }
    }

    const uint64_t Sequence = State.WriteCursor.fetch_add(1u, std::memory_order_relaxed);
    ThoughtRingSlot& Slot   = State.Ring[static_cast<std::size_t>(Sequence & (ThoughtLogState::RingCapacity - 1u))];
    Slot.DurationNs.store(DurationNs, std::memory_order_relaxed);
    Slot.NameId.store(NameId, std::memory_order_relaxed);
    Slot.ThreadHash.store(HashCurrentThread(), std::memory_order_relaxed);
    Slot.Stamp.store(Sequence + 1u, std::memory_order_release); // Publish last
}

void ThoughtLog::RecordSample(const char* Name, uint64_t DurationNs)
{
    RecordScopeSample(ResolveNameId(Name), DurationNs);
}

bool ThoughtLog::IsRunning() noexcept
{
    return AccessState().Running.load(std::memory_order_relaxed);
}

void ThoughtLog::FlushReport(const std::filesystem::path& TargetFile)
{
    ThoughtLogState& State = AccessState();

    std::vector<ThoughtNameStats> Snapshot {};
    {
        std::lock_guard<std::mutex> StatsLock(State.StatsMutex);
        Snapshot = State.Stats;
    }
    std::sort(Snapshot.begin(), Snapshot.end(), [](const ThoughtNameStats& A, const ThoughtNameStats& B) {
        return A.TotalNs > B.TotalNs;
    });

    const double SessionSeconds =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - State.SessionStart).count();
    const uint64_t TotalCalls = std::accumulate(
        Snapshot.begin(), Snapshot.end(), uint64_t{ 0u }, [](uint64_t Sum, const ThoughtNameStats& E) { return Sum + E.Calls; });
    const uint64_t GrandTotalNs = std::accumulate(
        Snapshot.begin(), Snapshot.end(), uint64_t{ 0u }, [](uint64_t Sum, const ThoughtNameStats& E) { return Sum + E.TotalNs; });

    std::error_code DirError {};
    std::filesystem::create_directories(TargetFile.parent_path(), DirError);

    std::ofstream Out(TargetFile, std::ios::trunc);
    if (!Out)
    {
        std::fprintf(stderr, "[ThoughtLog] Could not write report to %s\n", TargetFile.generic_string().c_str());
        return;
    }

    Out << "============================== FRONTIER THOUGHT LOG ==============================\n";
    Out << "Timing telemetry buffered entirely in RAM; flushed once at application close.\n";
    char Header[256];
    std::snprintf(
        Header, sizeof(Header),
        "Session: %.1f s | Samples: %llu | Overwritten (ring lapped): %llu | Dropped after close: 0\n",
        SessionSeconds,
        static_cast<unsigned long long>(TotalCalls),
        static_cast<unsigned long long>(State.OverwrittenCount.load(std::memory_order_relaxed)));
    Out << Header;
    Out << "Sorted by total time. Avg/P95 from a 1024-sample reservoir per name.\n\n";
    Out << "Name                                                 Calls    Total ms     Avg us    P50 us    P95 us    Max us   Share\n";
    Out << "------------------------------------------------------------------------------------------------------------------\n";

    for (const ThoughtNameStats& Entry : Snapshot)
    {
        if (Entry.Calls == 0u)
        {
            continue;
        }
        const double TotalMs  = static_cast<double>(Entry.TotalNs) / 1.0e6;
        const double AvgUs    = static_cast<double>(Entry.TotalNs) / static_cast<double>(Entry.Calls) / 1.0e3;
        const double P50Us    = static_cast<double>(PercentileNs(Entry, 0.50)) / 1.0e3;
        const double P95Us    = static_cast<double>(PercentileNs(Entry, 0.95)) / 1.0e3;
        const double MaxUs    = static_cast<double>(Entry.MaxNs) / 1.0e3;
        const double SharePct = (GrandTotalNs > 0u)
                                    ? (100.0 * static_cast<double>(Entry.TotalNs) / static_cast<double>(GrandTotalNs))
                                    : 0.0;
        char Line[512];
        std::snprintf(
            Line, sizeof(Line), "%-52s %8llu %11.3f %10.2f %9.2f %9.2f %9.2f %6.2f%%\n",
            Entry.Name.c_str(),
            static_cast<unsigned long long>(Entry.Calls),
            TotalMs, AvgUs, P50Us, P95Us, MaxUs, SharePct);
        Out << Line;
    }

    Out << "\nFrame summary (scopes named *.Frame):\n";
    for (const ThoughtNameStats& Entry : Snapshot)
    {
        if (Entry.Calls == 0u || Entry.Name.find(".Frame") == std::string::npos)
        {
            continue;
        }
        const double AvgMs = static_cast<double>(Entry.TotalNs) / static_cast<double>(Entry.Calls) / 1.0e6;
        char Line[256];
        std::snprintf(
            Line, sizeof(Line), "  %-50s frames=%llu  avg=%.3f ms (%.1f fps)  worst=%.3f ms\n",
            Entry.Name.c_str(),
            static_cast<unsigned long long>(Entry.Calls),
            AvgMs,
            (AvgMs > 0.0) ? (1000.0 / AvgMs) : 0.0,
            static_cast<double>(Entry.MaxNs) / 1.0e6);
        Out << Line;
    }
    Out << "==================================================================================\n";
    Out.flush();
    std::printf("[ThoughtLog] Flushed %llu samples from %zu scopes to %s\n",
        static_cast<unsigned long long>(TotalCalls),
        Snapshot.size(),
        TargetFile.generic_string().c_str());
}

} // namespace Frontier::Diagnostics
