/*====================================================================================================================================
                                                       OPERATIONTRACE.CPP
====================================================================================================================================*/
// 🧩 Development trace of operations: what began, what it decided, how long it took (FRONTIER_DEVELOPMENT_TRACE builds)

#include "Engine/GeometrySpace/Parametric/Diagnostics/OperationTrace.h"

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <fstream>
#include <mutex>

namespace Frontier::GeometrySpace::Parametric
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    INTERNAL FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

namespace
{
    constexpr size_t TailLimit     = 4000;                                                   // [-]  - Lines kept in memory
    constexpr double SlowThreshold = 250.0;                                                  // [ms] - A ◀ line at or above this is flagged

    struct TraceDivision
    {
        std::mutex                            Guard;                                         // [-] - Serialises file and tail
        std::ofstream                         File;                                          // [-] - Open trace file (may be closed)
        std::filesystem::path                 Path;                                          // [-] - Its path
        std::deque<std::string>               Tail;                                          // [-] - Newest lines
        std::atomic<size_t>                   Revision{ 0 };                                 // [-] - Lines written so far
        std::atomic<int>                      Threads{ 0 };                                  // [-] - Thread numbers handed out
        std::chrono::steady_clock::time_point Origin = std::chrono::steady_clock::now();     // [s] - Time zero of the elapsed column

        TraceDivision()
        {
            if (const char* Env = std::getenv("FRONTIER_TRACE_PATH"); Env && *Env)
            {
                File.open(Env, std::ios::out | std::ios::trunc | std::ios::binary);
                if (File.is_open()) Path = Env;
            }
        }
    };

    TraceDivision& Division()
    {
        static TraceDivision Instance;
        return Instance;
    }

    thread_local int TraceDepth  = 0;                                                        // [-] - Open scopes on this thread
    thread_local int TraceThread = -1;                                                       // [-] - Small number of this thread

    int ThreadNumber()
    {
        if (TraceThread < 0) TraceThread = Division().Threads.fetch_add(1);
        return TraceThread;
    }

    // "   12.345 s  T0  Blend      │ ▶ message": fixed columns so a long log reads as a tree.
    std::string FormatLine(const char* Category, const std::string& Message, int Depth)
    {
        TraceDivision& Trace   = Division();
        const double   Elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - Trace.Origin).count();
        char           Prefix[64];
        std::snprintf(Prefix, sizeof(Prefix), "%10.3f s  T%-2d %-10s ", Elapsed, ThreadNumber(), Category ? Category : "-");
        std::string Line = Prefix;
        for (int Level = 0; Level < Depth; ++Level) Line += "│ ";
        Line += Message;
        return Line;
    }

    void Commit(std::string Line, bool FlushCondition)
    {
        TraceDivision&              Trace = Division();
        const std::lock_guard<std::mutex> Lock(Trace.Guard);
        if (Trace.File.is_open())
        {
            Trace.File << Line << '\n';
            if (FlushCondition) Trace.File.flush();
        }
        Trace.Tail.push_back(std::move(Line));
        while (Trace.Tail.size() > TailLimit) Trace.Tail.pop_front();
        Trace.Revision.fetch_add(1);
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

bool OpenOperationTrace(const std::filesystem::path& Path)
{
    TraceDivision&                    Trace = Division();
    const std::lock_guard<std::mutex> Lock(Trace.Guard);
    {
        if (Trace.File.is_open()) Trace.File.close();
        std::error_code Failure;
        if (std::filesystem::exists(Path, Failure))
        {
            std::filesystem::path Previous = Path;
            Previous.replace_extension();
            Previous += ".previous";
            Previous += Path.extension();
            std::filesystem::remove(Previous, Failure);
            std::filesystem::rename(Path, Previous, Failure);
        }
        Trace.File.open(Path, std::ios::out | std::ios::trunc | std::ios::binary);
        Trace.Path = Trace.File.is_open() ? Path : std::filesystem::path();
        if (Trace.File.is_open())
        {
            // Earlier lines (before the file existed) open the file, so start-up is not lost.
            for (const std::string& Line : Trace.Tail) Trace.File << Line << '\n';
            Trace.File.flush();
        }
    }
    return Trace.File.is_open();
}

void CloseOperationTrace()
{
    TraceDivision&                    Trace = Division();
    const std::lock_guard<std::mutex> Lock(Trace.Guard);
    if (Trace.File.is_open()) Trace.File.close();
    Trace.Path.clear();
}

std::filesystem::path OperationTracePath()
{
    TraceDivision&                    Trace = Division();
    const std::lock_guard<std::mutex> Lock(Trace.Guard);
    return Trace.Path;
}

void WriteOperationTrace(const char* Category, const std::string& Message)
{
    Commit(FormatLine(Category, "· " + Message, TraceDepth), TraceDepth == 0);
}

std::vector<std::string> RecentOperationTrace(size_t Count)
{
    TraceDivision&                    Trace = Division();
    const std::lock_guard<std::mutex> Lock(Trace.Guard);
    const size_t                      First = Trace.Tail.size() > Count ? Trace.Tail.size() - Count : 0;
    return std::vector<std::string>(Trace.Tail.begin() + static_cast<std::ptrdiff_t>(First), Trace.Tail.end());
}

size_t OperationTraceRevision()
{
    return Division().Revision.load();
}

std::string TraceNumber(double Value)
{
    char Text[32];
    std::snprintf(Text, sizeof(Text), "%.6g", Value);
    return Text;
}

bool OperationTraceCompiledCondition()
{
#ifdef FRONTIER_DEVELOPMENT_TRACE
    return true;
#else
    return false;
#endif
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    CLASSES
//------------------------------------------------------------------------------------------------------------------------

OperationTraceScope::OperationTraceScope(const char* Category, std::string Message)
    : Category(Category), Message(std::move(Message)), Start(std::chrono::steady_clock::now())
{
    Commit(FormatLine(this->Category, "▶ " + this->Message, TraceDepth), TraceDepth == 0);
    ++TraceDepth;
}

OperationTraceScope::~OperationTraceScope()
{
    --TraceDepth;
    const double Milliseconds = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - Start).count();
    char         Duration[48];
    std::snprintf(Duration, sizeof(Duration), " · %.3f ms", Milliseconds);
    Commit(FormatLine(Category, "◀ " + Message + Duration + (Milliseconds >= SlowThreshold ? "  ⚠️ slow" : ""), TraceDepth),
           TraceDepth == 0 || Milliseconds >= SlowThreshold);
}

} // namespace Frontier::GeometrySpace::Parametric
