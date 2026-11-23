/*==========================================================================================================================================
                                                          PROJECT ZERO CODE IMAGE
                                     ------------------------------------------------------------------
                             The reference v3 interchange implementation whose diagnostics feed the Log Stream.
==========================================================================================================================================*/

// 🧩 Project Zero code image: the reference v3 interchange implementation whose diagnostics feed the Log Stream.

#include <ProjectInterchange.h>

#include <chrono>
#include <cstdio>
#include <cstring>

namespace
{

using SteadyTime = std::chrono::steady_clock::time_point;

FrontierHostInterchange Host = {};
bool                    HostReady = false;
SteadyTime              LastEmit = {};
uint32_t                Tick = 0;
uint32_t                Emission = 0;
uint32_t                Random = 0;

constexpr std::chrono::milliseconds EmitCadence{2800};   // [ms] - Reference traffic period

uint32_t NextRandom()
{
    Random = Random * 1664525u + 1013904223u;
    return (Random >> 16) & 0x7fffu;
}

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

// Subjects carry the category after the slash: the host routes "Project-Zero/Foliage" into the project's
// declared Foliage category (pink, from ProjectZero.frontier), the rest into the project stream.
bool FRONTIER_CODE_IMAGE_CALL ConstructProject(
    const FrontierHostInterchange* HostInterchange, void* ProjectReception)
{
    if (HostInterchange == nullptr || HostInterchange->StructureSize < sizeof(*HostInterchange) ||
        HostInterchange->ReceiveDiagnostic == nullptr)
    {
        return false;
    }

    Host              = *HostInterchange;
    HostReady         = true;
    Random            = 2166136261u;
    Tick              = 0;
    Emission          = 0;
    LastEmit          = std::chrono::steady_clock::now() - EmitCadence;   // [ms] - First emission is immediate
    (void)ProjectReception;

    EmitDiagnostic("Project-Zero", "Reference project constructed · code image interfaced with host");
    return true;
}

void FRONTIER_CODE_IMAGE_CALL AdvanceProject()
{
    ++Tick;

    const SteadyTime Now = std::chrono::steady_clock::now();
    if (Now - LastEmit < EmitCadence)
    {
        return;
    }
    LastEmit = Now;
    ++Emission;

    char Line[256] = {};
    switch (Emission % 4)
    {
        case 0:
            std::snprintf(Line, sizeof(Line), "Reference project alive · tick %u", Tick);
            EmitDiagnostic("Project-Zero", Line);
            break;

        case 1:
            // 🍃 The plan's canonical example: the tree shedding leaves, logged in its own pink category.
            std::snprintf(Line, sizeof(Line), "Leaf cluster detached · %u leaves · gust %u.%u m/s · litter +40",
                          8 + NextRandom() % 30, 2 + NextRandom() % 8, NextRandom() % 10);
            EmitDiagnostic("Project-Zero/Foliage", Line);
            break;

        case 2:
            std::snprintf(Line, sizeof(Line), "Checkpoint %u reached · split %u",
                          1 + NextRandom() % 9, 1200 + NextRandom() % 8000);
            EmitDiagnostic("Project-Zero/Gameplay", Line);
            break;

        default:
            std::snprintf(Line, sizeof(Line), "Peer table · %u in session · median RTT %u ms",
                          2 + NextRandom() % 15, 18 + NextRandom() % 90);
            EmitDiagnostic("Project-Zero/Network", Line);
            break;
    }
}

void FRONTIER_CODE_IMAGE_CALL RetireProject()
{
    EmitDiagnostic("Project-Zero", "Reference project retired by host");
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
        return Refuse("Project-Zero does not implement the requested code interchange number.");
    }

    if (InterfaceFingerprint != FRONTIER_INTERFACE_FINGERPRINT)
    {
        return Refuse("Project-Zero interface fingerprint does not match the host.");
    }

    InterchangeDelivery->ConstructProject = &ConstructProject;
    InterchangeDelivery->AdvanceProject   = &AdvanceProject;
    InterchangeDelivery->RetireProject    = &RetireProject;
    return true;
}

} // extern "C"
