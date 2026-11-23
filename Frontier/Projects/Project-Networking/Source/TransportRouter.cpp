//============================================================================================================================================
//                                                          TRANSPORTTROUTER.CPP
//============================================================================================================================================
// 📦 Selects EOS vs Photon after login and ticks the active transport. Photon failures fall back
// to EOS so login never breaks. Premium is a manual toggle (Setup switch, persisted) with a
// FRONTIER_PREMIUM_MULTIPLAYER env override; the Xsolla REST entitlement check
// (Source/XsollaStore.*, no vendor binaries) plugs into ResolvePremiumAccess later.

#include "TransportRouter.h"
#include "EcomOwnership.h"
#include "EosTransport.h"
#include "PhotonTransport.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>

namespace Networking
{
namespace
{
DiagnosticReception Reception = nullptr;
bool PremiumToggle = false;
TransportKind Wanted = TransportKind::None;
TransportKind Active = TransportKind::None;
TransportPhase Phase = TransportPhase::Idle;
char Reading[256]{};
bool FellBack = false;
char CatalogBuffer[1024]{};
const char* CatalogPtrs[8]{};
unsigned CatalogParsed = 0;
char IdentityToken[8192]{};
char OwnershipToken[8192]{};

void Emit(const char* Text) noexcept
{
    if (Reception)
        Reception(Text);
}

unsigned ParseCatalogIds() noexcept
{
    CatalogParsed = 0;
    const char* Env = std::getenv("EOS_CATALOG_ITEM_IDS");
    if (!Env || !*Env)
        return 0;
    std::snprintf(CatalogBuffer, sizeof(CatalogBuffer), "%s", Env);
    char* Cursor = CatalogBuffer;
    while (*Cursor && CatalogParsed < 8)
    {
        while (*Cursor == ' ' || *Cursor == '\t' || *Cursor == ',')
            ++Cursor;
        if (!*Cursor)
            break;
        CatalogPtrs[CatalogParsed++] = Cursor;
        while (*Cursor && *Cursor != ',')
            ++Cursor;
        if (*Cursor)
            *Cursor++ = 0;
        char* End = Cursor - 1;
        while (End > CatalogPtrs[CatalogParsed - 1] && (*(End - 1) == ' ' || *(End - 1) == '\t'))
            *--End = 0;
        if (!*CatalogPtrs[CatalogParsed - 1])
            --CatalogParsed;
    }
    return CatalogParsed;
}

bool ActivateEos(const char* Why) noexcept
{
    StopPhotonTransport();
    StopOwnershipQuery();
    if (!StartEosTransport(Reception))
    {
        Phase = TransportPhase::Failed;
        std::snprintf(Reading, sizeof(Reading), "transport=failed eos_start_refused");
        Emit(Reading);
        return false;
    }
    Active = TransportKind::Eos;
    Phase = TransportPhase::Active;
    std::snprintf(Reading, sizeof(Reading), "transport=eos %s", Why);
    Emit(Reading);
    return true;
}

bool BeginPhoton() noexcept
{
    Phase = TransportPhase::StartingOwnership;
    std::snprintf(Reading, sizeof(Reading), "transport=photon starting_ownership_check");
    Emit(Reading);
    const unsigned Count = ParseCatalogIds();
    StartOwnershipQuery(Count ? CatalogPtrs : nullptr, Count, Reception);
    return true;
}

bool BeginPhotonConnect() noexcept
{
    Phase = TransportPhase::StartingPhoton;
    if (!CopyEpicIdentityToken(IdentityToken, sizeof(IdentityToken)))
    {
        std::snprintf(Reading, sizeof(Reading), "transport=photon identity_token_unavailable");
        Emit(Reading);
        return false;
    }
    const bool HasOwnership = CopyOwnershipToken(OwnershipToken, sizeof(OwnershipToken));
    PhotonCredentials Credentials;
    Credentials.IdTokenJwt = IdentityToken;
    Credentials.OwnershipTokenOrNull = HasOwnership ? OwnershipToken : nullptr;
    if (!StartPhotonTransport(Credentials, Reception))
        return false;
    std::snprintf(Reading, sizeof(Reading), "transport=photon connecting ownership_token=%s",
        HasOwnership ? "attached" : "dormant");
    Emit(Reading);
    return true;
}

void WipeTokens() noexcept
{
    for (auto* Buffer : {IdentityToken, OwnershipToken})
    {
        volatile char* Bytes = Buffer;
        for (std::size_t Index = 0; Index < 8192; ++Index)
            Bytes[Index] = 0;
    }
}
}

bool ResolvePremiumAccess() noexcept
{
    if (const char* Env = std::getenv("FRONTIER_PREMIUM_MULTIPLAYER"))
    {
        if (std::strcmp(Env, "1") == 0)
            return true;
        if (std::strcmp(Env, "0") == 0)
            return false;
    }
    return PremiumToggle;
}

void SetPremiumMultiplayer(bool Enabled) noexcept { PremiumToggle = Enabled; }

bool StartMultiplayerTransport(DiagnosticReception ActiveReception) noexcept
{
    StopMultiplayerTransport();
    Reception = ActiveReception;
    FellBack = false;
    Wanted = SelectTransportKind(ResolvePremiumAccess(), PhotonLinkAvailable());
    Active = TransportKind::None;
    if (Wanted == TransportKind::Photon)
    {
        char Line[128]{};
        std::snprintf(Line, sizeof(Line), "transport_wanted=photon premium=1 linked=%d",
            PhotonLinkAvailable() ? 1 : 0);
        Emit(Line);
        return BeginPhoton();
    }
    char Line[128]{};
    std::snprintf(Line, sizeof(Line), "transport_wanted=eos premium=%d linked=%d",
        ResolvePremiumAccess() ? 1 : 0, PhotonLinkAvailable() ? 1 : 0);
    Emit(Line);
    return ActivateEos("premium_off_or_unlinked");
}

void TickMultiplayerTransport() noexcept
{
    if (!Reception || Wanted == TransportKind::None)
        return;
    if (Phase == TransportPhase::StartingOwnership)
    {
        const OwnershipStatus Ownership = InspectOwnershipStatus();
        if (Ownership.State == OwnershipState::Querying)
            return;
        if (!BeginPhotonConnect())
        {
            FellBack = true;
            ActivateEos("photon_start_refused");
        }
        return;
    }
    if (Active == TransportKind::Photon || Phase == TransportPhase::StartingPhoton)
    {
        TickPhotonTransport();
        const PhotonStatus Status = InspectPhotonStatus();
        if (Phase == TransportPhase::StartingPhoton && Status.State == PhotonState::Joined)
        {
            Active = TransportKind::Photon;
            Phase = TransportPhase::Active;
            std::snprintf(Reading, sizeof(Reading), "transport=photon active first_packet=%d",
                Status.FirstPacketSent ? 1 : 0);
            Emit(Reading);
        }
        else if ((Status.State == PhotonState::Failed || Status.State == PhotonState::Disconnected) && !FellBack)
        {
            FellBack = true;
            char Line[320]{};
            std::snprintf(Line, sizeof(Line), "transport=photon failed (%.200s); falling back to EOS",
                Status.Reading);
            Emit(Line);
            ActivateEos("photon_fallback");
        }
        return;
    }
    if (Active == TransportKind::Eos)
        TickEosTransport();
}

void StopMultiplayerTransport() noexcept
{
    StopPhotonTransport();
    StopOwnershipQuery();
    StopEosTransport();
    WipeTokens();
    Reception = nullptr;
    Wanted = TransportKind::None;
    Active = TransportKind::None;
    Phase = TransportPhase::Idle;
    Reading[0] = 0;
    FellBack = false;
    CatalogParsed = 0;
}

TransportStatus InspectTransportStatus() noexcept
{
    TransportStatus Status;
    Status.Active = Active;
    Status.Wanted = Wanted;
    Status.Phase = Phase;
    if (Active == TransportKind::Photon)
    {
        const PhotonStatus Photon = InspectPhotonStatus();
        std::snprintf(Status.Reading, sizeof(Status.Reading), "%.200s", Photon.Reading);
    }
    else if (Active == TransportKind::Eos)
    {
        const EosStatus Eos = InspectEosStatus();
        std::snprintf(Status.Reading, sizeof(Status.Reading), "%.200s", Eos.Reading);
    }
    else
        std::snprintf(Status.Reading, sizeof(Status.Reading), "%s",
            Reading[0] ? Reading : "transport: not started");
    return Status;
}

const char* InspectTransportDisplayName() noexcept
{
    if (Active == TransportKind::Photon)
    {
        const PhotonStatus Photon = InspectPhotonStatus();
        return Photon.State == PhotonState::Joined ? "Photon" : "Photon connecting";
    }
    if (Active == TransportKind::Eos)
        return "EOS only";
    if (Phase == TransportPhase::StartingOwnership || Phase == TransportPhase::StartingPhoton)
        return "Starting...";
    if (Wanted == TransportKind::Photon)
        return PhotonLinkAvailable() ? "Photon connecting" : "Photon unavailable";
    return "EOS only";
}
}
