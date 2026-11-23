//============================================================================================================================================
//                                                              TICKSEQUENCE.H
//============================================================================================================================================
// 🧩 Monotonically increasing ordering points, stamped at arrival and never derived at consumption.

#pragma once

#include <cstdint>

namespace Slate
{

//------------------------------------------------------------------------------------------------------------------------
//                                                   ORDERING POINTS
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 One ordering point on the monotonic host timeline.
/// note  An input sample stamped at consumption has the display rate baked into it. `22` reconstructs the
///       path the artist drew from arrival stamps, which is the only reason arrival stamps exist.
/// tag   nonallocating, nonthrowing
struct TickPoint
{
    std::uint64_t  Index = 0u;   // [ns] - host-monotonic, zero at process start
};

/// 🧩 The monotonic host timeline. One instance per process, constructed at bring-up.
/// tag   owning
class TickSequence
{
public:

    /// 🧩 Fixes the timeline origin against the host performance counter.
    /// cost  ✔️
    TickSequence();

    /// 🧩 Reads the current ordering point.
    /// out   TickPoint  [ns]  nanoseconds since the origin, never decreasing between two reads
    /// err   never refuses
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    TickPoint Advance() const;

    /// 🧩 The ordering point a raw host counter reading stands for.
    /// in    HostCount  [-]   a reading of the same counter this timeline fixed its origin against
    /// out   TickPoint  [ns]  zero for any reading at or before the origin
    /// note  🔴 A device that stamps its own samples reports them in the host counter's units, not in this
    ///       timeline's. `04` §3 requires the stamp to survive to `22`, and a sample restamped when the
    ///       message carrying it was drained carries the drain rate rather than the device rate — which is
    ///       the defect arrival stamping exists to prevent, reintroduced one layer lower. Projecting the
    ///       device's own reading keeps the resolution the device actually reported at.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    TickPoint Project(std::uint64_t HostCount) const;

    /// 🧩 Duration between two ordering points.
    /// in    Earlier    [ns]  the point taken first
    /// in    Later      [ns]  the point taken second
    /// out   Duration   [ms]  zero when Later precedes Earlier, never negative
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    static double Span(TickPoint Earlier, TickPoint Later);

private:

    std::uint64_t  OriginCount    = 0u;   // [-]    - counter reading at construction
    double         CountToSeconds = 0.0;  // [s]    - seconds carried by one counter increment
};

}   // namespace Slate
