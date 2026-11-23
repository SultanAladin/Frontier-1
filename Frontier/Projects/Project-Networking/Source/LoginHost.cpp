//============================================================================================================================================
//                                                               LOGINHOST.CPP
//============================================================================================================================================
// 📦 Runs the real EOS login from a console without creating a game window.

#include "EpicExchange.h"
#include "AuthPolicy.h"
#include "PlatformDiagnostics.h"
#include "TransportRouter.h"
#include "TransportCodec.h"
#include "PhotonTransport.h"
#include "EcomOwnership.h"
#include "XsollaStore.h"
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <thread>
#include <vector>

namespace
{
void PrintDiagnostic(const char* Text)
{
    std::puts(Text);
    std::fflush(stdout);
}
}

int main(int ArgumentCount, char** Arguments)
{
    if (ArgumentCount == 2 && std::strcmp(Arguments[1], "--platform-check") == 0)
    {
        const bool Ready = Networking::VerifyEpicPlatform(PrintDiagnostic);
        const bool Retired = Networking::ShutdownEpic(PrintDiagnostic);
        return Ready && Retired ? 0 : 2;
    }
    if (ArgumentCount == 2 && std::strcmp(Arguments[1], "--sdk-check") == 0)
    {
        const bool Ready = Networking::VerifyEpicRuntime(PrintDiagnostic);
        const bool Retired = Networking::ShutdownEpic(PrintDiagnostic);
        return Ready && Retired ? 0 : 2;
    }
    if (ArgumentCount == 2 && std::strcmp(Arguments[1], "--lifecycle-check") == 0)
    {
        constexpr int Scopes = static_cast<int>(Networking::ChargeAuthScopes());
        static_assert((Scopes & static_cast<int>(EOS_EAuthScopeFlags::EOS_AS_Country)) != 0);
        static_assert((Scopes & static_cast<int>(EOS_EAuthScopeFlags::EOS_AS_BasicProfile)) != 0);
        static_assert((Scopes & static_cast<int>(EOS_EAuthScopeFlags::EOS_AS_FriendsList)) != 0);
        static_assert((Scopes & static_cast<int>(EOS_EAuthScopeFlags::EOS_AS_Presence)) != 0);
        PrintDiagnostic("PASS Charge required scopes include Country, Basic Profile, Friends List and Presence");
        char Oversized[81]{};
        std::memset(Oversized, 'A', 80);
        char Boundary[65]{};
        std::memset(Boundary, 'A', 64);
        bool Accepted = Networking::ValidateEpicCredentials(Oversized, "test-client") != nullptr &&
            Networking::ValidateEpicCredentials("test-secret", Oversized) != nullptr &&
            Networking::ValidateEpicCredentials("test secret", "test-client") != nullptr &&
            Networking::ValidateEpicCredentials("test-secret", "test\nclient") != nullptr &&
            Networking::ValidateEpicCredentials(nullptr, "test-client") != nullptr &&
            Networking::ValidateEpicCredentials(Boundary, "test-client") == nullptr;
        const char* Raw = "ClientCredentials.ClientSecret must be an ANSI string between 1 and 64 in length";
        Accepted = Networking::ClassifyPlatformDiagnostic(Raw) == 1 && Accepted;
        Accepted = std::strstr(Networking::DescribePlatformDiagnostic(Networking::ClassifyPlatformDiagnostic(
            "unknown SDK message: token=DO_NOT_LOG_THIS_VALUE")), "DO_NOT_LOG_THIS_VALUE") == nullptr && Accepted;
        PrintDiagnostic(Accepted ? "PASS credential bounds and SDK diagnostic redaction" : "FAIL credential validation");
        Accepted = Networking::VerifyEpicRuntime(PrintDiagnostic) && Accepted;
        Accepted = Networking::VerifyEpicRuntime(PrintDiagnostic) && Accepted;
        const Networking::LoginSpecification Missing{nullptr, nullptr, "developer", "PlayerOne", false};
        Accepted = !Networking::ConstructEpic(Missing, PrintDiagnostic) && Accepted;
        Networking::RetireEpic();
        Accepted = Networking::VerifyEpicRuntime(PrintDiagnostic) && Accepted;
        Accepted = !Networking::QueryEpicFriends() && !Networking::ShowEpicFriends() &&
            !Networking::HideEpicFriends() && !Networking::EpicOverlayOwnsInput() && Accepted;
        const auto& Profile = Networking::InspectEpicProfile();
        Accepted = !Networking::QueryEpicProfile() && !Networking::ApproveEpicUserCreation() &&
            !Profile.Available && !Profile.Pending && !Profile.DisplayName[0] && !Profile.Country[0] &&
            !Profile.Language[0] && Networking::InspectFriendCount() == 0 && Accepted;
        PrintDiagnostic(Accepted ? "PASS signed-out profile/creation guards and cleared profile state" : "FAIL profile/creation guards");
        Accepted = Networking::ShutdownEpic(PrintDiagnostic) && Accepted;
        Accepted = Networking::ShutdownEpic(PrintDiagnostic) && Accepted;
        Accepted = !Networking::VerifyEpicRuntime(PrintDiagnostic) && Accepted;
        std::puts(Accepted ? "PASS same-process SDK reuse, retry, social guards and terminal shutdown" : "FAIL SDK lifecycle");
        return Accepted ? 0 : 2;
    }
    if (ArgumentCount == 2 && std::strcmp(Arguments[1], "--transport-check") == 0)
    {
        // No network, no login: codec, selection rules, signed-out guards, redaction.
        using namespace Networking;
        bool Accepted = true;
        std::vector<std::uint8_t> Frame;
        static constexpr char Tag[] = "frontier-photon-v1";
        Accepted = PacketCodec::Encode(ReplicationKind::Probe,
            reinterpret_cast<const std::uint8_t*>(Tag), sizeof(Tag) - 1, Frame) && Accepted;
        ReplicationKind Kind = ReplicationKind::Input;
        const std::uint8_t* Payload = nullptr;
        std::size_t PayloadLength = 0;
        Accepted = PacketCodec::Decode(Frame.data(), Frame.size(), Kind, Payload, PayloadLength) &&
            Kind == ReplicationKind::Probe && Accepted;
        Frame[Frame.size() - 1] ^= 0xFF;
        Accepted = !PacketCodec::Decode(Frame.data(), Frame.size() - 1, Kind, Payload, PayloadLength) && Accepted;
        Accepted = SelectTransportKind(true, true) == TransportKind::Photon &&
            SelectTransportKind(true, false) == TransportKind::Eos &&
            SelectTransportKind(false, true) == TransportKind::Eos && Accepted;
        char Token[64]{};
        Accepted = !CopyEpicIdentityToken(Token, sizeof(Token)) && Token[0] == 0 && Accepted;
        Accepted = !CopyOwnershipToken(Token, sizeof(Token)) && Token[0] == 0 && Accepted;
        Accepted = !SendPhotonPacket(Frame.data(), Frame.size(), true) && Accepted;
        const TransportStatus Status = InspectTransportStatus();
        const PhotonStatus Photon = InspectPhotonStatus();
        Accepted = Status.Active == TransportKind::None && Status.Reading[0] != 0 &&
            Photon.Reading[0] != 0 && std::strstr(Status.Reading, "token=") == nullptr &&
            std::strstr(Photon.Reading, "token=") == nullptr && Accepted;
        StopMultiplayerTransport();
        std::puts(Accepted ? "PASS transport codec, selection, guards and redaction" : "FAIL transport checks");
        return Accepted ? 0 : 2;
    }
    if (ArgumentCount == 2 && std::strcmp(Arguments[1], "--xsolla-check") == 0)
    {
        using namespace Networking;
        char Line[320]{};
        DescribeXsollaConfig(Line, sizeof(Line));
        PrintDiagnostic(Line);
        const XsollaClientConfig Config = InspectXsollaConfig();
        bool Accepted = std::strstr(Line, "xsolla_sdk=rest") && std::strstr(Line, "binaries=none") &&
            std::strstr(Line, "cpp_zip=unpublished") && std::strstr(Line, "merchant_secret=") &&
            !std::strstr(Line, "XSOLLA_API_KEY") && XsollaCatalogCount == 6;
        char Url[256]{};
        Accepted = !BuildPayStationUrl(Config.Sandbox, "bad token", Url, sizeof(Url)) && Accepted;
        Accepted = BuildPayStationUrl(true, "sandboxToken_1", Url, sizeof(Url)) &&
            std::strstr(Url, XsollaPayStationSandbox) && Accepted;
        char Redacted[256]{};
        Accepted = RedactPayStationUrl(Url, Redacted, sizeof(Redacted)) &&
            std::strstr(Redacted, "token=[redacted]") && !std::strstr(Redacted, "sandboxToken_1") && Accepted;
        XsollaEntitlement Entitlement;
        Accepted = ParseXsollaEntitlement("{\"premium\":\"none\",\"nitron\":0}", Entitlement) &&
            !EntitlementGrantsPremium(Entitlement) && Accepted;
        std::puts(Accepted ? "PASS xsolla rest client, no vendor binaries" : "FAIL xsolla checks");
        return Accepted ? 0 : 2;
    }
    if (ArgumentCount != 1)
    {
        std::puts("Usage: LoginHost [--sdk-check | --lifecycle-check | --platform-check | --transport-check | --xsolla-check]");
        return 2;
    }
    std::puts("Project-Networking: REAL EOS login; no simulated provider");
    if (!Networking::ConstructEpic(PrintDiagnostic))
    {
        Networking::ShutdownEpic(PrintDiagnostic);
        return 2;
    }
    while (Networking::InspectLogin() != Networking::LoginProgress::Connected &&
           Networking::InspectLogin() != Networking::LoginProgress::Refused)
    {
        Networking::AdvanceEpic();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    const bool Connected = Networking::InspectLogin() == Networking::LoginProgress::Connected;
    Networking::ShutdownEpic(PrintDiagnostic);
    return Connected ? 0 : 1;
}
