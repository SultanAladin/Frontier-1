//============================================================================================================================================
//                                                         PHOTONTRANSPORT.CPP
//============================================================================================================================================
// 📦 Real Photon Realtime Core C++ transport: Epic-auth connect, join-or-create room, first
// replicated packet. Compiled against the pinned GitHub headers on every build; linked only when
// the official /MD x64 libs are present (otherwise PhotonLinkStub.cpp is linked instead).
// Diagnostics carry states, counts and numeric causes only. Tokens are never logged.

#include "PhotonTransport.h"
#include "TransportCodec.h"
// The Photon headers do not detect the target; the consumer predefines it.
#if defined(_WIN32) && !defined(_EG_WINDOWS_PLATFORM)
#define _EG_WINDOWS_PLATFORM true
#endif
#include <LoadBalancing-cpp/inc/Client.h>
#include <LoadBalancing-cpp/inc/Enums/PeerStates.h>
#include <LoadBalancing-cpp/inc/Enums/DisconnectCause.h>
#include <LoadBalancing-cpp/inc/Enums/CustomAuthenticationType.h>
#include <Common-cpp/inc/Enums/TypeCode.h>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

namespace Networking
{
namespace
{
constexpr char BuiltInAppId[] = "08ec2e74-b472-42d4-942e-27894128e8fb";
constexpr char BuiltInAppVersion[] = "1.0";
constexpr char BuiltInRoom[] = "FrontierDevRoom";
constexpr nByte ReplicationEvent = 1;
constexpr nByte DevRoomMaxPlayers = 8;
constexpr int ConnectTimeoutSeconds = 30;
constexpr std::size_t MaxTokenLength = 8192;

DiagnosticReception Reception = nullptr;
std::unique_ptr<ExitGames::LoadBalancing::Client> Session;
PhotonState Phase = PhotonState::Idle;
char Failure[256]{};
int LastCause = 0;
unsigned Sent = 0;
unsigned Received = 0;
bool FirstSent = false;
std::chrono::steady_clock::time_point ProgressAt;

void Emit(const char* Text) noexcept
{
    if (Reception)
        Reception(Text);
}

const char* DescribeCause(int Cause) noexcept
{
    namespace CauseOf = ExitGames::LoadBalancing::DisconnectCause;
    switch (Cause)
    {
    case CauseOf::NONE: return "dispatch refused";
    case CauseOf::DISCONNECT_BY_SERVER_USER_LIMIT: return "server user limit";
    case CauseOf::EXCEPTION_ON_CONNECT: return "server unreachable";
    case CauseOf::DISCONNECT_BY_SERVER: return "disconnected by server";
    case CauseOf::DISCONNECT_BY_SERVER_LOGIC: return "disconnected by server logic";
    case CauseOf::TIMEOUT_DISCONNECT: return "timeout";
    case CauseOf::EXCEPTION: return "internal socket error";
    case CauseOf::INVALID_AUTHENTICATION: return "invalid AppId";
    case CauseOf::MAX_CCU_REACHED: return "CCU limit reached";
    case CauseOf::INVALID_REGION: return "region not allowed";
    case CauseOf::OPERATION_NOT_ALLOWED_IN_CURRENT_STATE: return "operation not allowed";
    case CauseOf::CUSTOM_AUTHENTICATION_FAILED: return "Epic authentication refused";
    case CauseOf::CLIENT_VERSION_TOO_OLD: return "client version too old";
    case CauseOf::CLIENT_VERSION_INVALID: return "client version invalid";
    case CauseOf::DASHBOARD_VERSION_INVALID: return "dashboard version invalid";
    case CauseOf::AUTHENTICATION_TICKET_EXPIRED: return "auth ticket expired";
    case CauseOf::DISCONNECT_BY_OPERATION_LIMIT: return "operation limit";
    default: return "unknown cause";
    }
}

void Fail(int Cause, const char* What) noexcept
{
    LastCause = Cause;
    Phase = PhotonState::Failed;
    std::snprintf(Failure, sizeof(Failure), "%s failed: %s (cause=%d)", What, DescribeCause(Cause), Cause);
    Emit(Failure);
}

bool ValidTokenChars(const char* Text) noexcept
{
    // JWTs are base64url plus dots; anything else would break the auth parameters.
    if (!Text || !*Text || std::strlen(Text) > MaxTokenLength)
        return false;
    for (const unsigned char* C = reinterpret_cast<const unsigned char*>(Text); *C; ++C)
    {
        const bool Word = (*C >= 'A' && *C <= 'Z') || (*C >= 'a' && *C <= 'z') || (*C >= '0' && *C <= '9');
        if (!Word && *C != '-' && *C != '_' && *C != '.')
            return false;
    }
    return true;
}

bool ValidAppId(const char* Text) noexcept
{
    if (!Text || std::strlen(Text) != 36)
        return false;
    for (const unsigned char* C = reinterpret_cast<const unsigned char*>(Text); *C; ++C)
    {
        const bool Hex = (*C >= '0' && *C <= '9') || (*C >= 'a' && *C <= 'f') || (*C >= 'A' && *C <= 'F');
        if (!Hex && *C != '-')
            return false;
    }
    return true;
}

const char* PickSetting(const char* Override, const char* Environment, const char* BuiltIn,
    char* Buffer, std::size_t Capacity) noexcept
{
    const char* Value = (Override && *Override) ? Override : BuiltIn;
    if (const char* Env = std::getenv(Environment))
    {
        if (*Env)
            Value = Env;
    }
    std::snprintf(Buffer, Capacity, "%s", Value);
    return Buffer;
}

class TransportListener : public ExitGames::LoadBalancing::Listener
{
public:
    void debugReturn(int /*Level*/, const ExitGames::Common::JString& /*Text*/) override {}
    void connectionErrorReturn(int ErrorCode) override
    {
        char Line[128]{};
        std::snprintf(Line, sizeof(Line), "photon_connection_error code=%d", ErrorCode);
        Emit(Line);
    }
    void clientErrorReturn(int ErrorCode) override
    {
        char Line[128]{};
        std::snprintf(Line, sizeof(Line), "photon_client_error code=%d", ErrorCode);
        Emit(Line);
    }
    void warningReturn(int WarningCode) override
    {
        char Line[128]{};
        std::snprintf(Line, sizeof(Line), "photon_warning code=%d", WarningCode);
        Emit(Line);
    }
    void serverErrorReturn(int ErrorCode) override
    {
        char Line[128]{};
        std::snprintf(Line, sizeof(Line), "photon_server_error code=%d", ErrorCode);
        Emit(Line);
    }
    void joinRoomEventAction(int PlayerNr, const ExitGames::Common::JVector<int>& /*Others*/,
        const ExitGames::LoadBalancing::Player& /*Player*/) override
    {
        char Line[128]{};
        std::snprintf(Line, sizeof(Line), "photon_player_joined slot=%d", PlayerNr);
        Emit(Line);
    }
    void leaveRoomEventAction(int PlayerNr, bool IsInactive) override
    {
        char Line[128]{};
        std::snprintf(Line, sizeof(Line), "photon_player_left slot=%d inactive=%d", PlayerNr, IsInactive ? 1 : 0);
        Emit(Line);
    }
    void customEventAction(int PlayerNr, nByte EventCode,
        const ExitGames::Common::Object& Content) override
    {
        if (EventCode != ReplicationEvent)
            return;
        if (Content.getType() != ExitGames::Common::TypeCode::BYTEARRAY || Content.getDimensions() != 1)
        {
            Emit("photon_packet_refused unexpected_content_type");
            return;
        }
        const ExitGames::Common::ValueObject<nByte*> Array(Content);
        nByte* const* BytesAt = Array.getDataAddress();
        const int* Sizes = Array.getSizes();
        if (!BytesAt || !*BytesAt || !Sizes || Sizes[0] < 0)
        {
            Emit("photon_packet_refused unreadable_content");
            return;
        }
        const auto* Bytes = static_cast<const std::uint8_t*>(*BytesAt);
        ReplicationKind Kind = ReplicationKind::Probe;
        const std::uint8_t* Payload = nullptr;
        std::size_t PayloadLength = 0;
        if (!PacketCodec::Decode(Bytes, static_cast<std::size_t>(Sizes[0]), Kind, Payload, PayloadLength))
        {
            Emit("photon_packet_refused bad_codec_frame");
            return;
        }
        ++Received;
        char Line[160]{};
        std::snprintf(Line, sizeof(Line), "photon_packet_received bytes=%d kind=%u slot=%d",
            Sizes[0], static_cast<unsigned>(Kind), PlayerNr);
        Emit(Line);
    }
    void connectReturn(int ErrorCode, const ExitGames::Common::JString& /*ErrorText*/,
        const ExitGames::Common::JString& /*Region*/, const ExitGames::Common::JString& /*Cluster*/) override
    {
        ProgressAt = std::chrono::steady_clock::now();
        if (Phase != PhotonState::Connecting)
            return;
        if (ErrorCode != 0)
        {
            Fail(ErrorCode, "photon_connect");
            return;
        }
        Emit("photon_auth=success; joining room");
        Phase = PhotonState::Joining;
        ExitGames::LoadBalancing::RoomOptions Options;
        Options.setIsVisible(true);
        Options.setIsOpen(true);
        Options.setMaxPlayers(DevRoomMaxPlayers);
        if (!Session || !Session->opJoinOrCreateRoom(PendingRoom(), Options))
            Fail(0, "photon_join_room_dispatch");
    }
    void disconnectReturn() override
    {
        if (Phase == PhotonState::Idle || Phase == PhotonState::Failed || Phase == PhotonState::Disconnected)
            return;
        Phase = PhotonState::Disconnected;
        std::snprintf(Failure, sizeof(Failure), "photon=disconnected");
        Emit(Failure);
    }
    void leaveRoomReturn(int ErrorCode, const ExitGames::Common::JString& /*ErrorText*/) override
    {
        char Line[128]{};
        std::snprintf(Line, sizeof(Line), "photon_leave_room code=%d", ErrorCode);
        Emit(Line);
    }
    void joinOrCreateRoomReturn(int /*LocalNr*/, const ExitGames::Common::Hashtable& /*RoomProps*/,
        const ExitGames::Common::Hashtable& /*PlayerProps*/, int ErrorCode,
        const ExitGames::Common::JString& /*ErrorText*/) override
    {
        ProgressAt = std::chrono::steady_clock::now();
        if (Phase != PhotonState::Joining)
            return;
        if (ErrorCode != 0)
        {
            char Line[160]{};
            std::snprintf(Line, sizeof(Line), "photon_join_room failed: operation error (code=%d)", ErrorCode);
            std::snprintf(Failure, sizeof(Failure), "%s", Line);
            LastCause = ErrorCode;
            Phase = PhotonState::Failed;
            Emit(Line);
            return;
        }
        Phase = PhotonState::Joined;
        Emit("photon_room=joined");
        SendFirstPacket();
    }
    static void SetPendingRoom(const char* Room)
    {
        std::snprintf(RoomStorage(), sizeof(RoomStorage()), "%s", Room ? Room : "");
    }
    static const char* PendingRoom() { return RoomStorage(); }

private:
    static char* RoomStorage()
    {
        static char Room[128]{};
        return Room;
    }
    static void SendFirstPacket();
};

TransportListener ActiveListener;

void TransportListener::SendFirstPacket()
{
    static constexpr char Tag[] = "frontier-photon-v1";
    std::vector<std::uint8_t> Frame;
    if (!PacketCodec::Encode(ReplicationKind::Probe,
            reinterpret_cast<const std::uint8_t*>(Tag), sizeof(Tag) - 1, Frame))
    {
        Emit("photon_first_packet_refused codec_error");
        return;
    }
    if (!Session || Session->getState() != ExitGames::LoadBalancing::PeerStates::Joined)
    {
        Emit("photon_first_packet_refused not_joined");
        return;
    }
    auto* Bytes = reinterpret_cast<nByte*>(Frame.data());
    if (!Session->opRaiseEvent(true, Bytes, static_cast<int>(Frame.size()), ReplicationEvent))
    {
        Emit("photon_first_packet_refused raise_failed");
        return;
    }
    FirstSent = true;
    ++Sent;
    char Line[128]{};
    std::snprintf(Line, sizeof(Line), "photon_first_packet_sent bytes=%u", static_cast<unsigned>(Frame.size()));
    Emit(Line);
}
}

bool PhotonLinkAvailable() noexcept { return true; }

bool StartPhotonTransport(const PhotonCredentials& Credentials, DiagnosticReception ActiveReception) noexcept
{
    StopPhotonTransport();
    if (!Credentials.IdTokenJwt || !ValidTokenChars(Credentials.IdTokenJwt))
    {
        if (ActiveReception)
            ActiveReception("photon=refused missing_or_malformed_identity_token");
        return false;
    }
    if (Credentials.OwnershipTokenOrNull && *Credentials.OwnershipTokenOrNull &&
        !ValidTokenChars(Credentials.OwnershipTokenOrNull))
    {
        if (ActiveReception)
            ActiveReception("photon=refused malformed_ownership_token");
        return false;
    }
    char AppId[128]{};
    char AppVersion[64]{};
    char Room[128]{};
    PickSetting(Credentials.AppId, "PHOTON_APP_ID", BuiltInAppId, AppId, sizeof(AppId));
    PickSetting(Credentials.AppVersion, "PHOTON_APP_VERSION", BuiltInAppVersion, AppVersion, sizeof(AppVersion));
    PickSetting(Credentials.RoomName, "PHOTON_ROOM", BuiltInRoom, Room, sizeof(Room));
    if (!ValidAppId(AppId))
    {
        if (ActiveReception)
            ActiveReception("photon=refused malformed_app_id");
        return false;
    }
    if (!*Room)
    {
        if (ActiveReception)
            ActiveReception("photon=refused empty_room_name");
        return false;
    }
    Reception = ActiveReception;
    TransportListener::SetPendingRoom(Room);
    Phase = PhotonState::Connecting;
    Failure[0] = 0;
    LastCause = 0;
    Sent = 0;
    Received = 0;
    FirstSent = false;
    ProgressAt = std::chrono::steady_clock::now();
    Emit("photon=connecting; Epic authentication in flight, tokens withheld from logs");
    try
    {
        Session = std::make_unique<ExitGames::LoadBalancing::Client>(ActiveListener, AppId, AppVersion);
    }
    catch (...)
    {
        Session.reset();
        Phase = PhotonState::Failed;
        std::snprintf(Failure, sizeof(Failure), "photon=refused client_construction_failed");
        Emit(Failure);
        return false;
    }
    std::string Parameters = "token=";
    Parameters += Credentials.IdTokenJwt;
    if (Credentials.OwnershipTokenOrNull && *Credentials.OwnershipTokenOrNull)
    {
        Parameters += "&ownershipToken=";
        Parameters += Credentials.OwnershipTokenOrNull;
    }
    ExitGames::LoadBalancing::AuthenticationValues Auth;
    Auth.setType(ExitGames::LoadBalancing::CustomAuthenticationType::EPIC);
    Auth.setParameters(Parameters.c_str());
    ExitGames::LoadBalancing::ConnectOptions Options;
    Options.setAuthenticationValues(Auth);
    Options.setUseBackgroundSendReceiveThread(false);
    if (!Session->connect(Options))
    {
        Fail(0, "photon_connect_dispatch");
        return false;
    }
    return true;
}

void TickPhotonTransport() noexcept
{
    if (!Session)
        return;
    if ((Phase == PhotonState::Connecting || Phase == PhotonState::Joining) &&
        std::chrono::steady_clock::now() - ProgressAt > std::chrono::seconds(ConnectTimeoutSeconds))
    {
        Session->disconnect();
        Fail(static_cast<int>(ExitGames::LoadBalancing::DisconnectCause::TIMEOUT_DISCONNECT), "photon_connect");
        return;
    }
    Session->service();
}

void StopPhotonTransport() noexcept
{
    if (Session)
    {
        Session->disconnect();
        Session->service();
        Session.reset();
    }
    TransportListener::SetPendingRoom("");
    Phase = PhotonState::Idle;
    Failure[0] = 0;
    LastCause = 0;
    Sent = 0;
    Received = 0;
    FirstSent = false;
    Reception = nullptr;
}

bool SendPhotonPacket(const std::uint8_t* Bytes, std::size_t Length, bool Reliable) noexcept
{
    if (!Session || Phase != PhotonState::Joined || !Bytes || Length == 0 ||
        Length > PacketCodec::MaxPacket ||
        Session->getState() != ExitGames::LoadBalancing::PeerStates::Joined)
        return false;
    auto* Payload = reinterpret_cast<nByte*>(const_cast<std::uint8_t*>(Bytes));
    if (!Session->opRaiseEvent(Reliable, Payload, static_cast<int>(Length), ReplicationEvent))
        return false;
    ++Sent;
    return true;
}

PhotonStatus InspectPhotonStatus() noexcept
{
    PhotonStatus Status;
    Status.State = Phase;
    Status.LastDisconnectCause = LastCause;
    Status.PacketsSent = Sent;
    Status.PacketsReceived = Received;
    Status.FirstPacketSent = FirstSent;
    switch (Phase)
    {
    case PhotonState::Connecting: std::snprintf(Status.Reading, sizeof(Status.Reading), "Photon: connecting"); break;
    case PhotonState::Joining: std::snprintf(Status.Reading, sizeof(Status.Reading), "Photon: joining room"); break;
    case PhotonState::Joined:
        std::snprintf(Status.Reading, sizeof(Status.Reading), "Photon: in room (sent=%u received=%u)",
            Sent, Received);
        break;
    case PhotonState::Failed:
        std::snprintf(Status.Reading, sizeof(Status.Reading), "Photon: %s",
            Failure[0] ? Failure : "attempt failed");
        break;
    case PhotonState::Disconnected:
        std::snprintf(Status.Reading, sizeof(Status.Reading), "Photon: disconnected"); break;
    case PhotonState::Idle: std::snprintf(Status.Reading, sizeof(Status.Reading), "Photon: idle"); break;
    case PhotonState::Unlinked: std::snprintf(Status.Reading, sizeof(Status.Reading), "Photon: not linked"); break;
    }
    return Status;
}
}
