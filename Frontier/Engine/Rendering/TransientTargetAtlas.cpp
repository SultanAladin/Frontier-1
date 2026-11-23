/*==========================================================================================================================================
                                              INTERVAL PARTITIONING OVER SUBMISSION POSITIONS
                                     ------------------------------------------------------------------
                                         First bin freed strictly before a target's first use wins.
==========================================================================================================================================*/

// 🧩 Interval partitioning over submission positions: first bin freed strictly before a target's first use wins.

#include "TransientTargetAtlas.h"

#include "Engine/Diagnostics/LogStream.h"

#include <algorithm>
#include <cstdio>
#include <limits>
#include <unordered_map>

namespace Frontier
{
namespace
{

uint32_t RenderToken()
{
    const LogCategory* Category = LogCategories().QueryByName("Render");
    return Category != nullptr ? Category->Token : 0;
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

bool ResolveTransientTargetAtlas(const std::vector<SubmissionUnitDeclaration>&  Units,
                                 const ResolvedSubmissionOrder&                 Order,
                                 const std::vector<RenderTargetDeclaration>&    Targets,
                                 ResolvedTransientAtlas&                        Out,
                                 std::string&                                   Diagnostic)
{
    Out        = ResolvedTransientAtlas{};
    Diagnostic.clear();

    // First/last touch per target name, in submission positions.
    struct Span
    {
        uint32_t First = std::numeric_limits<uint32_t>::max();
        uint32_t Last  = 0;
        bool     Seen  = false;
    };
    std::unordered_map<std::string, Span> Spans;
    const auto Touch = [&](const std::string& Name, uint32_t Position) {
        Span& Record = Spans[Name];
        Record.First = std::min(Record.First, Position);
        Record.Last  = std::max(Record.Last, Position);
        Record.Seen  = true;
    };
    for (uint32_t Position = 0; Position < Order.Order.size(); ++Position)
    {
        const SubmissionUnitDeclaration& Unit = Units[Order.Order[Position]];
        for (const TargetTouch& Use : Unit.Touches)
        {
            Touch(Use.Target, Position);
        }
    }

    std::unordered_map<std::string, uint32_t> Declared;
    for (uint32_t Index = 0; Index < Targets.size(); ++Index)
    {
        Declared.emplace(Targets[Index].Name, Index);
    }

    // Every used target must be sized (imported targets are declared Persistent by the caller).
    for (const auto& Entry : Spans)
    {
        if (Declared.count(Entry.first) == 0)
        {
            Diagnostic = "target '" + Entry.first + "' is used but has no size declaration";
            WriteLog(RenderToken(), LogSeverity::Error, LogVerbosity::Log,
                     "Transient atlas refused · " + Diagnostic, "TransientTargetAtlas.cpp");
            return false;
        }
    }

    Out.Placements.reserve(Targets.size());
    std::vector<uint32_t> Transient;   // declaration indices taking part in aliasing
    for (uint32_t Index = 0; Index < Targets.size(); ++Index)
    {
        const RenderTargetDeclaration& Target = Targets[Index];
        const auto                        Found  = Spans.find(Target.Name);

        AtlasPlacement Placement;
        Placement.Name = Target.Name;
        Placement.Bin  = std::numeric_limits<uint32_t>::max();
        Placement.FirstUse = Found != Spans.end() ? Found->second.First : 0;
        Placement.LastUse  = Found != Spans.end() ? Found->second.Last : 0;
        Out.Placements.push_back(Placement);

        if (Target.Persistent)
        {
            continue;
        }
        if (Found == Spans.end() || !Found->second.Seen)
        {
            Diagnostic = "transient target '" + Target.Name + "' is declared but never used";
            WriteLog(RenderToken(), LogSeverity::Error, LogVerbosity::Log,
                     "Transient atlas refused · " + Diagnostic, "TransientTargetAtlas.cpp");
            return false;
        }
        Transient.push_back(Index);
        Out.UnaliasedBytes += RenderTargetBytes(Target);
    }

    // Greedy interval partitioning in first-use order (ties: larger first, fewer oversized bins).
    std::sort(Transient.begin(), Transient.end(), [&](uint32_t Left, uint32_t Right) {
        const AtlasPlacement& A = Out.Placements[Left];
        const AtlasPlacement& B = Out.Placements[Right];
        if (A.FirstUse != B.FirstUse)
        {
            return A.FirstUse < B.FirstUse;
        }
        return RenderTargetBytes(Targets[Left]) > RenderTargetBytes(Targets[Right]);
    });

    std::vector<uint32_t>     BinFreeAfter;   // [position] - Last use of the bin's latest occupant
    std::vector<TargetFormat> BinFormats;     // [format] - Aliasing requires compatible formats (exact match)
    for (uint32_t Index : Transient)
    {
        AtlasPlacement& Placement = Out.Placements[Index];

        uint32_t Chosen = std::numeric_limits<uint32_t>::max();
        for (uint32_t Bin = 0; Bin < BinFreeAfter.size(); ++Bin)
        {
            if (BinFreeAfter[Bin] < Placement.FirstUse && BinFormats[Bin] == Targets[Index].Format)
            {
                Chosen = Bin;
                break;
            }
        }
        if (Chosen == std::numeric_limits<uint32_t>::max())
        {
            Chosen = static_cast<uint32_t>(BinFreeAfter.size());
            BinFreeAfter.push_back(0);
            BinFormats.push_back(Targets[Index].Format);
            Out.BinBytes.push_back(0);
        }

        Placement.Bin        = Chosen;
        BinFreeAfter[Chosen] = Placement.LastUse;
        Out.BinBytes[Chosen] = std::max(Out.BinBytes[Chosen], RenderTargetBytes(Targets[Index]));
    }

    for (uint64_t Bytes : Out.BinBytes)
    {
        Out.TotalBytes += Bytes;
    }

    char Line[320] = {};
    std::snprintf(Line, sizeof(Line),
                  "Transient atlas resolved · %zu targets -> %zu bins · %llu bytes (unaliased %llu, saved %llu)",
                  Transient.size(), Out.BinBytes.size(),
                  static_cast<unsigned long long>(Out.TotalBytes),
                  static_cast<unsigned long long>(Out.UnaliasedBytes),
                  static_cast<unsigned long long>(Out.UnaliasedBytes - Out.TotalBytes));
    WriteLog(RenderToken(), LogSeverity::Info, LogVerbosity::Log, Line, "TransientTargetAtlas.cpp");
    return true;
}

} // namespace Frontier
