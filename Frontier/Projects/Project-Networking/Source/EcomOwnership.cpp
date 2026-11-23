//============================================================================================================================================
//                                                           ECOMOWNERSHIP.CPP
//============================================================================================================================================
// 📦 Dormant catalog-ownership token query. No catalog IDs means no SDK call (Dormant). With IDs,
// EOS_Ecom_QueryOwnershipToken runs async; the signed JWT is copied into a bounded buffer while
// the callback runs. The token is handed to Photon only and never reaches diagnostics.

#include "EcomOwnership.h"
#include <eos_sdk.h>
#include <eos_ecom.h>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace Networking
{
namespace
{
constexpr unsigned MaxCatalogIds = 8;
constexpr std::size_t CatalogIdLength = 128;
constexpr std::size_t TokenLength = 8192;

DiagnosticReception Reception = nullptr;
OwnershipState Phase = OwnershipState::Dormant;
char Reading[256]{};
char CatalogIds[MaxCatalogIds][CatalogIdLength]{};
EOS_Ecom_CatalogItemId QueryPtrs[MaxCatalogIds]{};
unsigned CatalogCount = 0;
char Token[TokenLength]{};
bool TokenReady = false;
unsigned Generation = 0;

void Emit(const char* Text) noexcept
{
    if (Reception)
        Reception(Text);
}

void EOS_CALL ReceiveOwnershipToken(const EOS_Ecom_QueryOwnershipTokenCallbackInfo* Completion)
{
    if (!Completion || Completion->ClientData != reinterpret_cast<void*>(Generation))
        return; // Stale completion from a previous generation; ignore.
    if (Phase != OwnershipState::Querying)
        return;
    if (Completion->ResultCode != EOS_EResult::EOS_Success || !Completion->OwnershipToken ||
        !*Completion->OwnershipToken)
    {
        Phase = OwnershipState::Failed;
        std::snprintf(Reading, sizeof(Reading), "ownership=failed result=%s",
            EOS_EResult_ToString(Completion->ResultCode));
        Emit(Reading);
        return;
    }
    std::snprintf(Token, sizeof(Token), "%s", Completion->OwnershipToken);
    TokenReady = std::strlen(Token) > 0 && std::strlen(Completion->OwnershipToken) < TokenLength;
    if (!TokenReady)
        Token[0] = 0;
    Phase = TokenReady ? OwnershipState::Ready : OwnershipState::Failed;
    std::snprintf(Reading, sizeof(Reading), "%s",
        TokenReady ? "ownership=ready token_withheld_from_logs" : "ownership=failed token_too_long");
    Emit(Reading);
}

void WipeToken() noexcept
{
    volatile char* Bytes = Token;
    for (std::size_t Index = 0; Index < sizeof(Token); ++Index)
        Bytes[Index] = 0;
    TokenReady = false;
}
}

bool StartOwnershipQuery(const char* const* ItemIds, unsigned Count, DiagnosticReception ActiveReception) noexcept
{
    StopOwnershipQuery();
    Reception = ActiveReception;
    if (Count == 0 || !ItemIds)
    {
        Phase = OwnershipState::Dormant;
        std::snprintf(Reading, sizeof(Reading), "ownership=dormant catalog_ids=0");
        Emit(Reading);
        return true;
    }
    auto* Platform = static_cast<EOS_HPlatform>(InspectEpicPlatformHandle());
    auto Account = static_cast<EOS_EpicAccountId>(InspectEpicAccountHandle());
    if (!Platform || !Account)
    {
        Phase = OwnershipState::Failed;
        std::snprintf(Reading, sizeof(Reading), "ownership=failed no_signed_in_account");
        Emit(Reading);
        return true; // Router may still try Photon without the token.
    }
    EOS_HEcom Ecom = EOS_Platform_GetEcomInterface(Platform);
    if (!Ecom)
    {
        Phase = OwnershipState::Failed;
        std::snprintf(Reading, sizeof(Reading), "ownership=failed ecom_unavailable");
        Emit(Reading);
        return true;
    }
    CatalogCount = Count > MaxCatalogIds ? MaxCatalogIds : Count;
    for (unsigned Index = 0; Index < CatalogCount; ++Index)
    {
        if (!ItemIds[Index] || !*ItemIds[Index])
        {
            Phase = OwnershipState::Failed;
            std::snprintf(Reading, sizeof(Reading), "ownership=failed empty_catalog_id");
            Emit(Reading);
            return true;
        }
        std::snprintf(CatalogIds[Index], sizeof(CatalogIds[Index]), "%s", ItemIds[Index]);
    }
    const EOS_Ecom_CatalogItemId* Query = reinterpret_cast<const EOS_Ecom_CatalogItemId*>(CatalogIds);
    EOS_Ecom_QueryOwnershipTokenOptions Options{};
    Options.ApiVersion = EOS_ECOM_QUERYOWNERSHIPTOKEN_API_LATEST;
    Options.LocalUserId = Account;
    Options.CatalogItemIds = const_cast<EOS_Ecom_CatalogItemId*>(Query);
    Options.CatalogItemIdCount = CatalogCount;
    ++Generation;
    Phase = OwnershipState::Querying;
    std::snprintf(Reading, sizeof(Reading), "ownership=querying catalog_ids=%u", CatalogCount);
    Emit(Reading);
    EOS_Ecom_QueryOwnershipToken(Ecom, &Options, reinterpret_cast<void*>(Generation), ReceiveOwnershipToken);
    return true;
}

void StopOwnershipQuery() noexcept
{
    ++Generation; // Orphans any in-flight SDK completion.
    Phase = OwnershipState::Dormant;
    Reading[0] = 0;
    CatalogCount = 0;
    for (auto& Id : CatalogIds)
        Id[0] = 0;
    WipeToken();
    Reception = nullptr;
}

bool CopyOwnershipToken(char* Out, std::size_t Capacity) noexcept
{
    if (!Out || Capacity == 0 || Phase != OwnershipState::Ready || !TokenReady)
        return false;
    std::snprintf(Out, Capacity, "%s", Token);
    return Out[0] != 0;
}

OwnershipStatus InspectOwnershipStatus() noexcept
{
    OwnershipStatus Status;
    Status.State = Phase;
    Status.CatalogCount = CatalogCount;
    std::snprintf(Status.Reading, sizeof(Status.Reading), "%s", Reading[0] ? Reading : "ownership=dormant");
    return Status;
}
}
