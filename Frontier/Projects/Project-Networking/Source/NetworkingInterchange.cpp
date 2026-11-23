//============================================================================================================================================
//                                                         NETWORKINGINTERCHANGE.CPP
//============================================================================================================================================
// 📦 Ticks project-owned EOS login through the shared Frontier host C ABI.

#include <ProjectInterchange.h>
#include "EpicExchange.h"
#include <cstdio>

static_assert(FrontierCodeInterchangeNumber == 3u);
static_assert(FrontierCodeInterchangeFingerprint == UINT64_C(0xb6725f24d0869173));

namespace
{
FrontierProjectHostInterchange Host{};
bool Active = false;

void EmitDiagnostic(const char* Text)
{
    if (Host.ReceiveDiagnostic)
    {
        FrontierProjectDiagnostic Diagnostic{};
        Diagnostic.StructureSize = sizeof(Diagnostic);
        Diagnostic.SubjectName = "Project-Networking/EOS";
        Diagnostic.Explanation = Text;
        Host.ReceiveDiagnostic(&Diagnostic, Host.ProjectReception);
    }
}

uint32_t Refuse(FrontierProjectRefusal* Refusal, const char* Text)
{
    if (Refusal)
    {
        Refusal->Number = FrontierProjectRefusalConstruction;
        std::snprintf(Refusal->Explanation, sizeof(Refusal->Explanation), "%s", Text);
    }
    return 0u;
}

uint32_t FRONTIER_CODE_IMAGE_CALL ConstructProject(
    const FrontierProjectLaunch* Launch,
    const FrontierProjectHostInterchange* Reception,
    void** Record,
    FrontierProjectRefusal* Refusal)
{
    if (!Launch || Launch->StructureSize < sizeof(*Launch) || !Reception ||
        Reception->StructureSize < sizeof(*Reception) || !Record || Active)
        return Refuse(Refusal, "Invalid or duplicate networking project construction");
    *Record = nullptr;
    Host = *Reception;
    if (!Networking::ConstructEpic(EmitDiagnostic))
    {
        Networking::RetireEpic();
        Host = {};
        return Refuse(Refusal, "EOS initialization refused; inspect networking diagnostics and local credentials");
    }
    Active = true;
    *Record = &Host;
    return 1u;
}

uint32_t FRONTIER_CODE_IMAGE_CALL AdvanceProject(
    void* Record,
    const FrontierProjectCycle* Cycle,
    FrontierProjectRefusal* Refusal)
{
    if (!Active || Record != &Host || !Cycle || Cycle->StructureSize < sizeof(*Cycle))
        return Refuse(Refusal, "Invalid networking project cycle");
    Networking::AdvanceEpic();
    return 1u;
}

void FRONTIER_CODE_IMAGE_CALL RetireProject(void* Record)
{
    if (Record != &Host || !Active)
        return;
    Networking::ShutdownEpic(EmitDiagnostic);
    Active = false;
    Host = {};
}
}

extern "C" FRONTIER_CODE_IMAGE_EXPORT uint32_t FRONTIER_CODE_IMAGE_CALL ConstructProjectInterchange(
    uint32_t RequestedNumber,
    uint64_t RequestedFingerprint,
    FrontierProjectInterchange* Delivery,
    FrontierProjectRefusal* Refusal)
{
    if (!Delivery || RequestedNumber != FrontierCodeInterchangeNumber ||
        RequestedFingerprint != FrontierCodeInterchangeFingerprint)
        return Refuse(Refusal, "Incompatible Frontier project interchange");
    *Delivery = {};
    Delivery->StructureSize = sizeof(*Delivery);
    Delivery->CodeInterchangeNumber = FrontierCodeInterchangeNumber;
    Delivery->InterfaceFingerprint = FrontierCodeInterchangeFingerprint;
    Delivery->ConstructProject = ConstructProject;
    Delivery->AdvanceProject = AdvanceProject;
    Delivery->RetireProject = RetireProject;
    return 1u;
}
