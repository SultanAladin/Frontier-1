/*==========================================================================================================================================
                                                         PROVING GROUNDS CODE IMAGE
                                     ------------------------------------------------------------------
                                 Owns the spin simulation; the host advances it, snapshots render it later.
==========================================================================================================================================*/

// 🧩 Proving Grounds code image: owns the spin simulation; the host advances it, snapshots render it later.

#include "ProvingGrounds.h"

#include <ProjectInterchange.h>

#include <chrono>
#include <cstdio>

namespace
{

using SteadyTime = std::chrono::steady_clock::time_point;

FrontierHostInterchange   Host = {};
bool                      HostReady = false;
Frontier::ProvingGroundsSimulation Simulation;
SteadyTime                LastAdvance = {};

void EmitDiagnostic(const char* SubjectName, const char* Explanation)
{
    if (!HostReady || Host.ReceiveDiagnostic == nullptr)
    {
        return;
    }
    FrontierProjectDiagnostic Diagnostic = {};
    Diagnostic.StructureSize             = sizeof(Diagnostic);
    Diagnostic.SubjectName               = SubjectName;
    Diagnostic.Explanation               = Explanation;
    Host.ReceiveDiagnostic(&Diagnostic, Host.ProjectReception);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                 INTERCHANGE FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// 📝 The simulation snapshot (ProvingGroundsSimulation::TakeSnapshot) is consumed by the host's
//    viewport when the SDK render path lands; until then Exhibits/RenderInfrastructure drives
//    this same project source directly for validation and CPU capture.
bool FRONTIER_CODE_IMAGE_CALL ConstructProject(
    const FrontierHostInterchange* HostInterchange, void* ProjectReception)
{
    if (HostInterchange == nullptr || HostInterchange->StructureSize < sizeof(*HostInterchange) ||
        HostInterchange->ReceiveDiagnostic == nullptr)
    {
        return false;
    }

    Host        = *HostInterchange;
    HostReady   = true;
    Simulation  = Frontier::ProvingGroundsSimulation{};
    LastAdvance = std::chrono::steady_clock::now();
    (void)ProjectReception;

    EmitDiagnostic("Project-ProvingGrounds", "Proving Grounds constructed · spin simulation ready");
    return true;
}

void FRONTIER_CODE_IMAGE_CALL AdvanceProject()
{
    const SteadyTime Now     = std::chrono::steady_clock::now();
    const double     Elapsed = std::chrono::duration<double>(Now - LastAdvance).count();
    LastAdvance              = Now;

    // Clamp hitches (debugger pauses, window drags) so the cube never lurches.
    Simulation.Advance(Elapsed > 0.1 ? 0.1 : Elapsed > 0 ? Elapsed : 0);
}

void FRONTIER_CODE_IMAGE_CALL RetireProject()
{
    EmitDiagnostic("Project-ProvingGrounds", "Proving Grounds retired by host");
    HostReady = false;
}

} // namespace

extern "C"
{

bool FRONTIER_CODE_IMAGE_CALL ConstructProjectInterchange(
    uint32_t InterchangeNumber, uint64_t InterfaceFingerprint,
    FrontierProjectInterchange* InterchangeDelivery, char* RefusalExplanation,
    size_t RefusalExplanationCapacity)
{
    const auto Refuse = [&](const char* Explanation) -> bool {
        if (RefusalExplanation != nullptr && RefusalExplanationCapacity > 0)
        {
            std::snprintf(RefusalExplanation, RefusalExplanationCapacity, "%s", Explanation);
        }
        return false;
    };

    if (InterchangeDelivery == nullptr ||
        InterchangeDelivery->StructureSize < sizeof(FrontierProjectInterchange))
    {
        return Refuse("Interchange delivery record is missing or undersized.");
    }
    if (InterchangeNumber != FRONTIER_CODE_INTERCHANGE_NUMBER)
    {
        return Refuse("Project-ProvingGrounds does not implement the requested code interchange number.");
    }
    if (InterfaceFingerprint != FRONTIER_INTERFACE_FINGERPRINT)
    {
        return Refuse("Project-ProvingGrounds interface fingerprint does not match the host.");
    }

    InterchangeDelivery->ConstructProject = &ConstructProject;
    InterchangeDelivery->AdvanceProject   = &AdvanceProject;
    InterchangeDelivery->RetireProject    = &RetireProject;
    return true;
}

} // extern "C"
