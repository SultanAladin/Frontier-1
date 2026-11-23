//============================================================================================================================================
//                                                             TICKSEQUENCE.CPP
//============================================================================================================================================
// 🧩 Host timeline over the operating system's monotonic counter.

#include "SlateMath/Platform/TickSequence/Api/TickSequence.h"

// 📝 Every operating-system conditional in the repository lives under `SlateMath/Platform`, and this is one
//    of the two files in it that carries one. `04` §7 gates that; nothing above it may add another.
#if defined(_WIN32)
    // 📝 Construct.ps1 defines both on the command line. They are declared here as well so the file still
    //    compiles standalone, and guarded so the two declarations cannot collide into a C4005.
    #if !defined(WIN32_LEAN_AND_MEAN)
        #define WIN32_LEAN_AND_MEAN
    #endif
    #if !defined(NOMINMAX)
        #define NOMINMAX
    #endif
    #include <windows.h>
#else
    #include <ctime>
#endif

namespace Slate
{

//------------------------------------------------------------------------------------------------------------------------
//                                                     CONSTRUCTION
//------------------------------------------------------------------------------------------------------------------------

TickSequence::TickSequence()
{
#if defined(_WIN32)

    LARGE_INTEGER CounterRate   = {};
    LARGE_INTEGER CounterOrigin = {};
    QueryPerformanceFrequency(&CounterRate);
    QueryPerformanceCounter(&CounterOrigin);

    OriginCount    = static_cast<std::uint64_t>(CounterOrigin.QuadPart);
    CountToSeconds = 1.0 / static_cast<double>(CounterRate.QuadPart);

#else

    timespec Origin = {};
    clock_gettime(CLOCK_MONOTONIC, &Origin);

    OriginCount    = static_cast<std::uint64_t>(Origin.tv_sec) * 1000000000ull
                   + static_cast<std::uint64_t>(Origin.tv_nsec);
    CountToSeconds = 1.0e-9;

#endif
}

//------------------------------------------------------------------------------------------------------------------------
//                                                       ADVANCE
//------------------------------------------------------------------------------------------------------------------------

TickPoint TickSequence::Advance() const
{
    TickPoint Reading;

#if defined(_WIN32)

    LARGE_INTEGER CurrentCount = {};
    QueryPerformanceCounter(&CurrentCount);

    const std::uint64_t Elapsed = static_cast<std::uint64_t>(CurrentCount.QuadPart) - OriginCount;
    Reading.Index = static_cast<std::uint64_t>(static_cast<double>(Elapsed) * CountToSeconds * 1.0e9);

#else

    timespec Current = {};
    clock_gettime(CLOCK_MONOTONIC, &Current);

    const std::uint64_t CurrentCount = static_cast<std::uint64_t>(Current.tv_sec) * 1000000000ull
                                     + static_cast<std::uint64_t>(Current.tv_nsec);
    Reading.Index = CurrentCount - OriginCount;

#endif

    return Reading;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    A HOST READING
//------------------------------------------------------------------------------------------------------------------------

TickPoint TickSequence::Project(std::uint64_t HostCount) const
{
    TickPoint Reading;

    if (HostCount <= OriginCount)
        return Reading;

    const std::uint64_t Elapsed = HostCount - OriginCount;

#if defined(_WIN32)

    Reading.Index = static_cast<std::uint64_t>(static_cast<double>(Elapsed) * CountToSeconds * 1.0e9);

#else

    // 📝 The counter is already nanoseconds under the monotonic clock, so the conversion is the identity and
    //    is written as one rather than as a multiply by unity that rounds through a double on the way.
    Reading.Index = Elapsed;

#endif

    return Reading;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                         SPAN
//------------------------------------------------------------------------------------------------------------------------

double TickSequence::Span(TickPoint Earlier, TickPoint Later)
{
    if (Later.Index <= Earlier.Index)
        return 0.0;

    return static_cast<double>(Later.Index - Earlier.Index) * 1.0e-6;
}

}   // namespace Slate
