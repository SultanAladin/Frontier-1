/*==========================================================================================================================================
                                                   EDGE DERIVATION MIRRORS THE PLAN (§2)
                                     ------------------------------------------------------------------
                                     Declared reads/writes → adjacency → topological submission order.
==========================================================================================================================================*/

// 🧩 Edge derivation mirrors the plan (§2): declared reads/writes → adjacency → topological submission order.

#include "SubmissionOrder.h"

#include "Engine/Diagnostics/LogStream.h"

#include <algorithm>
#include <cstdio>
#include <functional>
#include <unordered_map>
#include <unordered_set>

namespace Frontier
{
namespace
{

uint32_t RenderToken()
{
    const LogCategory* Category = LogCategories().QueryByName("Render");
    return Category != nullptr ? Category->Token : 0;
}

void LogRefusal(const std::string& Diagnostic)
{
    WriteLog(RenderToken(), LogSeverity::Error, LogVerbosity::Log,
             "Submission order refused · " + Diagnostic, "SubmissionOrder.cpp");
}

// One concrete cycle among the unresolved units, for an actionable diagnostic. Returns declaration indices.
std::vector<uint32_t> FindCycle(const std::vector<std::vector<uint32_t>>& Edges,
                                const std::vector<char>&                  Unresolved)
{
    const uint32_t              Count = static_cast<uint32_t>(Edges.size());
    std::vector<char>           Colour(Count, 0);   // 0 unvisited · 1 on stack · 2 done
    std::vector<uint32_t>       Stack;
    std::vector<uint32_t>       Cycle;

    // Iterative DFS restricted to unresolved nodes (resolved ones cannot be on a cycle).
    std::function<bool(uint32_t)> Visit = [&](uint32_t Node) -> bool {
        Colour[Node] = 1;
        Stack.push_back(Node);
        for (uint32_t Next : Edges[Node])
        {
            if (!Unresolved[Next])
            {
                continue;
            }
            if (Colour[Next] == 1)
            {
                const auto Begin = std::find(Stack.begin(), Stack.end(), Next);
                Cycle.assign(Begin, Stack.end());
                Cycle.push_back(Next);
                return true;
            }
            if (Colour[Next] == 0 && Visit(Next))
            {
                return true;
            }
        }
        Stack.pop_back();
        Colour[Node] = 2;
        return false;
    };

    for (uint32_t Node = 0; Node < Count; ++Node)
    {
        if (Unresolved[Node] && Colour[Node] == 0 && Visit(Node))
        {
            break;
        }
    }
    return Cycle;
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

bool ResolveSubmissionOrder(const std::vector<SubmissionUnitDeclaration>& Units,
                            ResolvedSubmissionOrder&                    Out,
                            std::string&                              Diagnostic)
{
    Out        = ResolvedSubmissionOrder{};
    Diagnostic.clear();

    const uint32_t Count = static_cast<uint32_t>(Units.size());

    std::unordered_map<std::string, uint32_t> ByName;
    for (uint32_t Index = 0; Index < Count; ++Index)
    {
        if (Units[Index].Name.empty() || !ByName.emplace(Units[Index].Name, Index).second)
        {
            Diagnostic = "duplicate or empty unit name: '" + Units[Index].Name + "'";
            LogRefusal(Diagnostic);
            return false;
        }
    }

    // -- Derive edges: adjacency[a] lists units that must submit after a ------------------------------------------
    std::vector<std::vector<uint32_t>>        Edges(Count);
    std::vector<std::unordered_set<uint32_t>> EdgeSeen(Count);
    const auto AddEdge = [&](uint32_t From, uint32_t To) {
        if (From != To && EdgeSeen[From].insert(To).second)
        {
            Edges[From].push_back(To);
        }
    };

    struct TargetHistory
    {
        int64_t               LastWriter = -1;
        std::vector<uint32_t> ReadersSinceWrite;
    };
    std::unordered_map<std::string, TargetHistory> History;
    std::unordered_set<std::string>                Imported;

    for (uint32_t Index = 0; Index < Count; ++Index)
    {
        const SubmissionUnitDeclaration& Unit = Units[Index];

        for (const std::string& Predecessor : Unit.After)
        {
            const auto Found = ByName.find(Predecessor);
            if (Found == ByName.end())
            {
                Diagnostic = "unit '" + Unit.Name + "' orders after unknown unit '" + Predecessor + "'";
                LogRefusal(Diagnostic);
                return false;
            }
            AddEdge(Found->second, Index);
        }

        // Reads first: an in-place unit reads the previous writer's version, never its own.
        for (const TargetTouch& Touch : Unit.Touches)
        {
            if (!TargetUsageReads(Touch.Usage))
            {
                continue;
            }
            const std::string& Target = Touch.Target;
            TargetHistory& Record = History[Target];
            if (Record.LastWriter >= 0)
            {
                AddEdge(static_cast<uint32_t>(Record.LastWriter), Index);
            }
            else
            {
                Imported.insert(Target);
            }
            Record.ReadersSinceWrite.push_back(Index);
        }
        for (const TargetTouch& Touch : Unit.Touches)
        {
            if (!TargetUsageWrites(Touch.Usage))
            {
                continue;
            }
            const std::string& Target = Touch.Target;
            TargetHistory& Record = History[Target];
            if (Record.LastWriter >= 0)
            {
                AddEdge(static_cast<uint32_t>(Record.LastWriter), Index);   // WAW
            }
            for (uint32_t Reader : Record.ReadersSinceWrite)
            {
                AddEdge(Reader, Index);                                    // WAR
            }
            Record.LastWriter = static_cast<int64_t>(Index);
            Record.ReadersSinceWrite.clear();
        }
    }

    // -- Kahn resolution, smallest declaration index first (deterministic, declaration-faithful) -----------------
    std::vector<uint32_t> InDegree(Count, 0);
    for (uint32_t From = 0; From < Count; ++From)
    {
        for (uint32_t To : Edges[From])
        {
            InDegree[To] += 1;
        }
    }

    std::vector<char> Done(Count, 0);
    Out.Order.reserve(Count);
    while (Out.Order.size() < Count)
    {
        int64_t Pick = -1;
        for (uint32_t Index = 0; Index < Count; ++Index)
        {
            if (!Done[Index] && InDegree[Index] == 0)
            {
                Pick = static_cast<int64_t>(Index);
                break;
            }
        }
        if (Pick < 0)
        {
            std::vector<char> Unresolved(Count, 0);
            for (uint32_t Index = 0; Index < Count; ++Index)
            {
                Unresolved[Index] = Done[Index] ? 0 : 1;
            }
            const std::vector<uint32_t> Cycle = FindCycle(Edges, Unresolved);

            Diagnostic = "dependency cycle:";
            if (Cycle.empty())
            {
                for (uint32_t Index = 0; Index < Count; ++Index)
                {
                    if (Unresolved[Index])
                    {
                        Diagnostic += " " + Units[Index].Name;
                    }
                }
            }
            else
            {
                for (uint32_t Node : Cycle)
                {
                    Diagnostic += " " + Units[Node].Name + " ->";
                }
                Diagnostic.pop_back();
                Diagnostic.pop_back();
            }
            LogRefusal(Diagnostic);
            return false;
        }

        Done[static_cast<size_t>(Pick)] = 1;
        Out.Order.push_back(static_cast<uint32_t>(Pick));
        for (uint32_t To : Edges[static_cast<size_t>(Pick)])
        {
            InDegree[To] -= 1;
        }
    }

    Out.ImportedTargets.assign(Imported.begin(), Imported.end());
    std::sort(Out.ImportedTargets.begin(), Out.ImportedTargets.end());

    char Line[256] = {};
    std::snprintf(Line, sizeof(Line), "Submission order resolved · %u units · %zu imported targets",
                  Count, Out.ImportedTargets.size());
    WriteLog(RenderToken(), LogSeverity::Info, LogVerbosity::Log, Line, "SubmissionOrder.cpp");
    return true;
}

} // namespace Frontier
