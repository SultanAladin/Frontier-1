/*==========================================================================================================================================
                                                       THE WALK IS DELIBERATELY DUMB
                                     ------------------------------------------------------------------
                                   All intelligence lives in the declarations, so the dump is auditable.
==========================================================================================================================================*/

// 🧩 The walk is deliberately dumb: all intelligence lives in the declarations, so the dump is auditable.

#include "BarrierDerivation.h"

#include "Engine/Diagnostics/LogStream.h"

#include <cstdio>
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

bool Refuse(const std::string& Problem, std::string& Diagnostic)
{
    Diagnostic = Problem;
    WriteLog(RenderToken(), LogSeverity::Error, LogVerbosity::Log,
             "Barrier derivation refused · " + Problem, "BarrierDerivation.cpp");
    return false;
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

bool DeriveBarriers(const std::vector<SubmissionUnitDeclaration>& Units,
                    const ResolvedSubmissionOrder&                Order,
                    const std::vector<RenderTargetDeclaration>&   Targets,
                    DerivedBarriers&                              Out,
                    std::string&                                  Diagnostic)
{
    Out        = DerivedBarriers{};
    Diagnostic.clear();

    struct WalkState
    {
        TargetUsage Current  = TargetUsage::Undefined;
        bool        WroteLast = false;   // [-] - Previous touch of this target wrote
        bool        Touched   = false;
    };
    std::unordered_map<std::string, WalkState> States;

    // Resting-usage contract: transient targets are born Undefined (their contents are
    // discarded-safe by definition); persistent targets must declare where history rests,
    // because the first transition of the frame depends on it.
    for (const RenderTargetDeclaration& Target : Targets)
    {
        if (!Target.Persistent && Target.RestingUsage != TargetUsage::Undefined)
        {
            return Refuse("transient target '" + Target.Name + "' declares a resting usage", Diagnostic);
        }
        if (Target.Persistent && Target.RestingUsage == TargetUsage::Undefined)
        {
            return Refuse("persistent target '" + Target.Name + "' has no resting usage", Diagnostic);
        }
        WalkState State;
        State.Current = Target.RestingUsage;
        States.emplace(Target.Name, State);
    }

    for (uint32_t Position = 0; Position < Order.Order.size(); ++Position)
    {
        const SubmissionUnitDeclaration& Unit = Units[Order.Order[Position]];
        for (const TargetTouch& Use : Unit.Touches)
        {
            const auto Found = States.find(Use.Target);
            if (Found == States.end())
            {
                return Refuse("unit '" + Unit.Name + "' touches undeclared target '" + Use.Target + "'", Diagnostic);
            }
            if (Use.Usage == TargetUsage::Undefined)
            {
                return Refuse("unit '" + Unit.Name + "' touches '" + Use.Target + "' with Undefined usage", Diagnostic);
            }

            WalkState& State = Found->second;
            if (Use.Usage != State.Current)
            {
                Out.Barriers.push_back(DerivedBarrier{Use.Target, Position, State.Current, Use.Usage,
                                                      BarrierKind::LayoutTransition});
            }
            else if (State.Touched && (State.WroteLast || TargetUsageWrites(Use.Usage)))
            {
                Out.Barriers.push_back(DerivedBarrier{Use.Target, Position, State.Current, Use.Usage,
                                                      BarrierKind::ExecutionDependency});
            }

            State.Current   = Use.Usage;
            State.WroteLast = TargetUsageWrites(Use.Usage);
            State.Touched   = true;
        }
    }

    return true;
}

void DumpBarriers(const DerivedBarriers&                        Barriers,
                  const std::vector<SubmissionUnitDeclaration>& Units,
                  const ResolvedSubmissionOrder&                Order)
{
    uint32_t Layouts     = 0;
    uint32_t Executions  = 0;
    char     Line[384]   = {};

    for (const DerivedBarrier& Barrier : Barriers.Barriers)
    {
        const char* UnitName = Barrier.BeforePosition < Order.Order.size()
            ? Units[Order.Order[Barrier.BeforePosition]].Name.c_str()
            : "(frame end)";
        if (Barrier.Kind == BarrierKind::LayoutTransition)
        {
            Layouts += 1;
            std::snprintf(Line, sizeof(Line), "Barrier · %s layout %s -> %s before %s [#%u]",
                          Barrier.Target.c_str(), TargetUsageText(Barrier.From),
                          TargetUsageText(Barrier.To), UnitName, Barrier.BeforePosition);
        }
        else
        {
            Executions += 1;
            std::snprintf(Line, sizeof(Line), "Barrier · %s dependency (%s) before %s [#%u]",
                          Barrier.Target.c_str(), TargetUsageText(Barrier.To), UnitName,
                          Barrier.BeforePosition);
        }
        WriteLog(RenderToken(), LogSeverity::Info, LogVerbosity::Verbose, Line, "BarrierDerivation.cpp");
    }

    std::snprintf(Line, sizeof(Line), "Barriers derived · %u layout transitions · %u execution dependencies",
                  Layouts, Executions);
    WriteLog(RenderToken(), LogSeverity::Info, LogVerbosity::Log, Line, "BarrierDerivation.cpp");
}

} // namespace Frontier
