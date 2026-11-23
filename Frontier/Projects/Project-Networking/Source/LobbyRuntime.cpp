#include "RoomRuntime.h"
#include <eos_lobby.h>
#include <eos_sessions.h>
#include <eos_rtc.h>
#include <eos_rtc_audio.h>
#include <algorithm>
#include <chrono>
#include <cstring>
#include <ctime>

namespace Networking
{
namespace
{
RoomReading Room;
EOS_HLobby Lobby = nullptr;
EOS_HSessions Sessions = nullptr;
EOS_HRTCAudio Audio = nullptr;
EOS_ProductUserId User = nullptr;
DiagnosticReception Log = nullptr;
std::uintptr_t Generation = 1;
void* Cookie() { return reinterpret_cast<void*>(Generation); }
bool Current(void* C) { return User && C == Cookie(); }
std::vector<EOS_ProductUserId> RealMembers, Registered;
std::array<bool, 3> DummyReady{};
unsigned Dummies = 3;
bool OwnerAttributesDirty = false;
bool Dirty = true, Operation = false, SessionExists = false, WantLeave = false, LobbyLeaveSent = false;
bool PendingMicrophone = false, PendingListening = true;
bool ReadyDesired = false, AttributesDirty = false, AutoArmed = true, MatchRecorded = false;
bool LiveSettingsDirty = false, UnregisterPending = false, JoinSessionPending = false;
std::string Name = "You", VoiceRoom, JoinAfterLeave;
std::chrono::steady_clock::time_point LastRefresh{}, MatchClock{};
// Settings survive Room resets so pre-login edits apply to the auto-created lobby.
LobbyCreationSettings PendingLobbySettings;
MatchSessionSettings PendingSessionSettings;
EOS_NotificationId MemberUpdate = EOS_INVALID_NOTIFICATIONID, MemberStatus = EOS_INVALID_NOTIFICATIONID,
    RoomConnection = EOS_INVALID_NOTIFICATIONID;
// Search handles and retained session handles for Join.
EOS_HLobbySearch LobbySearchHandle = nullptr;
EOS_HSessionSearch SessionSearchHandle = nullptr;
std::vector<EOS_HSessionDetails> SessionResultHandles;
std::string JoinSessionTargetId;

EOS_ELobbyPermissionLevel ToEosLobbyPermission(LobbyPermission P) noexcept
{
    switch (P)
    {
    case LobbyPermission::PublicAdvertised: return EOS_ELobbyPermissionLevel::EOS_LPL_PUBLICADVERTISED;
    case LobbyPermission::JoinViaPresence: return EOS_ELobbyPermissionLevel::EOS_LPL_JOINVIAPRESENCE;
    case LobbyPermission::InviteOnly: return EOS_ELobbyPermissionLevel::EOS_LPL_INVITEONLY;
    }
    return EOS_ELobbyPermissionLevel::EOS_LPL_JOINVIAPRESENCE;
}
EOS_EOnlineSessionPermissionLevel ToEosSessionPermission(SessionPermission P) noexcept
{
    switch (P)
    {
    case SessionPermission::PublicAdvertised: return EOS_EOnlineSessionPermissionLevel::EOS_OSPF_PublicAdvertised;
    case SessionPermission::JoinViaPresence: return EOS_EOnlineSessionPermissionLevel::EOS_OSPF_JoinViaPresence;
    case SessionPermission::InviteOnly: return EOS_EOnlineSessionPermissionLevel::EOS_OSPF_InviteOnly;
    }
    return EOS_EOnlineSessionPermissionLevel::EOS_OSPF_InviteOnly;
}
const char* SessionNameC() noexcept { return Room.SessionSettings.SessionName.c_str(); }
void SyncSessionFlags()
{
    Room.SessionExists = SessionExists;
    Room.SessionBusy = Operation && (Room.Phase == RoomPhase::Preparing || Room.Phase == RoomPhase::Starting ||
        Room.Phase == RoomPhase::Ending || UnregisterPending || JoinSessionPending);
    Room.SessionRegistered = static_cast<unsigned>(Registered.size());
}
void Report(const char* Op, EOS_EResult Result)
{
    std::string Text = std::string(Op) + " result=" + EOS_EResult_ToString(Result);
    if (Log) Log(Text.c_str());
    if (Result != EOS_EResult::EOS_Success) Room.Status = Text;
}
bool Success(const char* Op, EOS_EResult Result)
{ Report(Op, Result); return Result == EOS_EResult::EOS_Success; }
void Refresh();
void Prepare();
void Created();
void ReleaseSearchHandles()
{
    if (LobbySearchHandle) { EOS_LobbySearch_Release(LobbySearchHandle); LobbySearchHandle = nullptr; }
    if (SessionSearchHandle) { EOS_SessionSearch_Release(SessionSearchHandle); SessionSearchHandle = nullptr; }
    for (auto H : SessionResultHandles) if (H) EOS_SessionDetails_Release(H);
    SessionResultHandles.clear();
}
void ResetDummyReadiness() { DummyReady.fill(false); ReadyDesired = false; AttributesDirty = true; Dirty = true; }
void SaveMatch(const char* Kind)
{
    if (!Room.StartedAt || MatchRecorded) return;
    const auto Duration = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - MatchClock).count();
    RecordHistory(Kind, Room, Duration); MatchRecorded = true;
}
void EOS_CALL Changed(const EOS_Lobby_LobbyMemberUpdateReceivedCallbackInfo* C)
{ if (Current(C->ClientData) && C->LobbyId && Room.LobbyId == C->LobbyId) Dirty = true; }
void EOS_CALL Members(const EOS_Lobby_LobbyMemberStatusReceivedCallbackInfo* C)
{
    if (!Current(C->ClientData) || !C->LobbyId || Room.LobbyId != C->LobbyId) return;
    Dirty = true;
    if (C->TargetUserId == User && (C->CurrentStatus == EOS_ELobbyMemberStatus::EOS_LMS_KICKED ||
        C->CurrentStatus == EOS_ELobbyMemberStatus::EOS_LMS_CLOSED || C->CurrentStatus == EOS_ELobbyMemberStatus::EOS_LMS_DISCONNECTED))
    {
        SaveMatch("abandoned"); Room.VoiceConnected = Room.Microphone = false;
        WantLeave = true; Room.Status = "Removed from lobby; closing local match session";
    }
}
void EOS_CALL VoiceChanged(const EOS_Lobby_RTCRoomConnectionChangedCallbackInfo* C)
{
    if (!Current(C->ClientData) || !C->LobbyId || Room.LobbyId != C->LobbyId || C->LocalUserId != User) return;
    Room.VoiceConnected = C->bIsConnected == EOS_TRUE;
    if (!Room.VoiceConnected) Room.Microphone = false;
    Room.VoiceStatus = Room.VoiceConnected ? "EOS RTC connected - microphone muted initially" :
        std::string("EOS RTC disconnected: ") + EOS_EResult_ToString(C->DisconnectReason);
    if (Log) Log(Room.VoiceConnected ? "rtc=connected; dummy players are not voice participants" : "rtc=disconnected");
    Dirty = true;
}
void Subscribe()
{
    EOS_Lobby_AddNotifyLobbyMemberUpdateReceivedOptions A{}; A.ApiVersion = EOS_LOBBY_ADDNOTIFYLOBBYMEMBERUPDATERECEIVED_API_LATEST;
    MemberUpdate = EOS_Lobby_AddNotifyLobbyMemberUpdateReceived(Lobby, &A, Cookie(), Changed);
    EOS_Lobby_AddNotifyLobbyMemberStatusReceivedOptions B{}; B.ApiVersion = EOS_LOBBY_ADDNOTIFYLOBBYMEMBERSTATUSRECEIVED_API_LATEST;
    MemberStatus = EOS_Lobby_AddNotifyLobbyMemberStatusReceived(Lobby, &B, Cookie(), Members);
    EOS_Lobby_AddNotifyRTCRoomConnectionChangedOptions C{}; C.ApiVersion = EOS_LOBBY_ADDNOTIFYRTCROOMCONNECTIONCHANGED_API_LATEST;
    RoomConnection = EOS_Lobby_AddNotifyRTCRoomConnectionChanged(Lobby, &C, Cookie(), VoiceChanged);
}
void Refresh()
{
    if (!Lobby || Room.LobbyId.empty()) return;
    Dirty = false; LastRefresh = std::chrono::steady_clock::now();
    EOS_Lobby_CopyLobbyDetailsHandleOptions O{};
    O.ApiVersion = EOS_LOBBY_COPYLOBBYDETAILSHANDLE_API_LATEST; O.LobbyId = Room.LobbyId.c_str(); O.LocalUserId = User;
    EOS_HLobbyDetails Details = nullptr;
    if (EOS_Lobby_CopyLobbyDetailsHandle(Lobby, &O, &Details) != EOS_EResult::EOS_Success)
    { Room.Players.clear(); RealMembers.clear(); Room.LocalReady = false; return; }
    EOS_LobbyDetails_GetLobbyOwnerOptions Owner{}; Owner.ApiVersion = EOS_LOBBYDETAILS_GETLOBBYOWNER_API_LATEST;
    Room.Owner = EOS_LobbyDetails_GetLobbyOwner(Details, &Owner) == User;
    EOS_LobbyDetails_GetMemberCountOptions Count{}; Count.ApiVersion = EOS_LOBBYDETAILS_GETMEMBERCOUNT_API_LATEST;
    const uint32_t N = EOS_LobbyDetails_GetMemberCount(Details, &Count);
    Room.Players.clear(); RealMembers.clear(); Room.LocalReady = false;
    for (uint32_t I = 0; I < std::min(N, 16u); ++I)
    {
        EOS_LobbyDetails_GetMemberByIndexOptions Index{}; Index.ApiVersion = EOS_LOBBYDETAILS_GETMEMBERBYINDEX_API_LATEST; Index.MemberIndex = I;
        const auto Id = EOS_LobbyDetails_GetMemberByIndex(Details, &Index);
        if (!Id) continue;
        RealMembers.push_back(Id);
        RoomPlayer Player; Player.Local = Id == User;
        Player.Name = Player.Local ? Name : "EOS player " + std::to_string(I + 1);
        EOS_LobbyDetails_CopyMemberAttributeByKeyOptions Key{};
        Key.ApiVersion = EOS_LOBBYDETAILS_COPYMEMBERATTRIBUTEBYKEY_API_LATEST; Key.TargetUserId = Id; Key.AttrKey = "ready";
        EOS_Lobby_Attribute* Attribute = nullptr;
        if (EOS_LobbyDetails_CopyMemberAttributeByKey(Details, &Key, &Attribute) == EOS_EResult::EOS_Success && Attribute)
        {
            Player.Ready = Attribute->Data && Attribute->Data->ValueType == EOS_ELobbyAttributeType::EOS_AT_BOOLEAN && Attribute->Data->Value.AsBool == EOS_TRUE;
            EOS_Lobby_Attribute_Release(Attribute);
        }
        if (!Player.Local)
        {
            Key.AttrKey = "display_name"; Attribute = nullptr;
            if (EOS_LobbyDetails_CopyMemberAttributeByKey(Details, &Key, &Attribute) == EOS_EResult::EOS_Success && Attribute)
            {
                if (Attribute->Data && Attribute->Data->ValueType == EOS_ELobbyAttributeType::EOS_AT_STRING && Attribute->Data->Value.AsUtf8)
                    Player.Name.assign(Attribute->Data->Value.AsUtf8, std::min<size_t>(64, std::strlen(Attribute->Data->Value.AsUtf8)));
                EOS_Lobby_Attribute_Release(Attribute);
            }
        }
        if (Player.Local) Room.LocalReady = Player.Ready;
        Room.Players.push_back(Player);
    }
    if (!Room.Owner)
    {
        std::string RemoteState;
        const auto PreviousSession = Room.SessionId;
        for (const char* KeyName : {"session_id", "match_state"})
        {
            EOS_LobbyDetails_CopyAttributeByKeyOptions A{}; A.ApiVersion = EOS_LOBBYDETAILS_COPYATTRIBUTEBYKEY_API_LATEST; A.AttrKey = KeyName;
            EOS_Lobby_Attribute* V = nullptr;
            if (EOS_LobbyDetails_CopyAttributeByKey(Details, &A, &V) == EOS_EResult::EOS_Success && V)
            {
                if (V->Data && V->Data->ValueType == EOS_ELobbyAttributeType::EOS_AT_STRING && V->Data->Value.AsUtf8)
                {
                    if (std::strcmp(KeyName, "session_id") == 0) Room.SessionId = V->Data->Value.AsUtf8;
                    else RemoteState = V->Data->Value.AsUtf8;
                }
                EOS_Lobby_Attribute_Release(V);
            }
        }
        if (PreviousSession != Room.SessionId && RemoteState == "waiting")
        { ReadyDesired = false; AttributesDirty = true; }
        if (!WantLeave && RemoteState == "running" && Room.Phase != RoomPhase::Running)
        {
            OwnerAttributesDirty = true;
            Room.Phase = RoomPhase::Running; MatchClock = std::chrono::steady_clock::now();
            Room.StartedAt = std::time(nullptr); MatchRecorded = false;
            Room.Status = "Host match in progress. Join the match session to get a local session.";
            Room.SessionStatus = Room.SessionExists ? "Match session joined." : "Host is running. Use Join match session.";
        }
        else if (RemoteState == "complete" && Room.Phase == RoomPhase::Running)
        { SaveMatch("completed"); Room.Phase = RoomPhase::Complete; Room.Status = "Host ended the match"; }
        else if (RemoteState == "waiting" && Room.Phase == RoomPhase::Complete)
        { Room.Phase = RoomPhase::Waiting; Room.StartedAt = 0; ReadyDesired = false; AttributesDirty = true; }
    }
    EOS_LobbyDetails_Release(Details);
    for (unsigned I = 0; I < Dummies; ++I)
        Room.Players.push_back({"Test player " + std::to_string(I + 1), DummyReady[I], true, false});
    char Buffer[512]{}; uint32_t Capacity = sizeof(Buffer);
    EOS_Lobby_GetRTCRoomNameOptions R{}; R.ApiVersion = EOS_LOBBY_GETRTCROOMNAME_API_LATEST; R.LobbyId = Room.LobbyId.c_str(); R.LocalUserId = User;
    if (EOS_Lobby_GetRTCRoomName(Lobby, &R, Buffer, &Capacity) == EOS_EResult::EOS_Success) VoiceRoom = Buffer;
    EOS_Lobby_IsRTCRoomConnectedOptions V{}; V.ApiVersion = EOS_LOBBY_ISRTCROOMCONNECTED_API_LATEST; V.LobbyId = Room.LobbyId.c_str(); V.LocalUserId = User;
    EOS_Bool Connected = EOS_FALSE;
    if (EOS_Lobby_IsRTCRoomConnected(Lobby, &V, &Connected) == EOS_EResult::EOS_Success)
    { Room.VoiceConnected = Connected == EOS_TRUE; if (!Room.VoiceConnected) Room.Microphone = false; }
    SyncSessionFlags();
}
void EOS_CALL AttributesDone(const EOS_Lobby_UpdateLobbyCallbackInfo* C)
{
    if (!Current(C->ClientData)) return;
    Room.ReadyPending = false; Dirty = true;
    if (!Success("lobby_member_update", C->ResultCode)) { ReadyDesired = Room.LocalReady; AttributesDirty = false; AutoArmed = false; }
}
void PublishAttributes()
{
    if (!Lobby || Room.LobbyId.empty() || Room.ReadyPending || (!AttributesDirty && !OwnerAttributesDirty && !LiveSettingsDirty) || WantLeave) return;
    EOS_Lobby_UpdateLobbyModificationOptions O{};
    O.ApiVersion = EOS_LOBBY_UPDATELOBBYMODIFICATION_API_LATEST; O.LocalUserId = User; O.LobbyId = Room.LobbyId.c_str();
    EOS_HLobbyModification Modification = nullptr;
    if (!Success("lobby_modify", EOS_Lobby_UpdateLobbyModification(Lobby, &O, &Modification))) { AttributesDirty = false; return; }
    EOS_Lobby_AttributeData D{}; D.ApiVersion = EOS_LOBBY_ATTRIBUTEDATA_API_LATEST; D.Key = "ready";
    D.ValueType = EOS_ELobbyAttributeType::EOS_AT_BOOLEAN; D.Value.AsBool = ReadyDesired ? EOS_TRUE : EOS_FALSE;
    EOS_LobbyModification_AddMemberAttributeOptions A{}; A.ApiVersion = EOS_LOBBYMODIFICATION_ADDMEMBERATTRIBUTE_API_LATEST;
    A.Attribute = &D; A.Visibility = EOS_ELobbyAttributeVisibility::EOS_LAT_PUBLIC;
    auto Result = EOS_LobbyModification_AddMemberAttribute(Modification, &A);
    if (Result == EOS_EResult::EOS_Success)
    {
        D.Key = "display_name"; D.ValueType = EOS_ELobbyAttributeType::EOS_AT_STRING; D.Value.AsUtf8 = Name.c_str();
        Result = EOS_LobbyModification_AddMemberAttribute(Modification, &A);
    }
    if (Result == EOS_EResult::EOS_Success && Room.Owner && (OwnerAttributesDirty || LiveSettingsDirty))
    {
        EOS_LobbyModification_AddAttributeOptions Pub{}; Pub.ApiVersion = EOS_LOBBYMODIFICATION_ADDATTRIBUTE_API_LATEST;
        Pub.Attribute = &D; Pub.Visibility = EOS_ELobbyAttributeVisibility::EOS_LAT_PUBLIC;
        D.Key = "session_id"; D.ValueType = EOS_ELobbyAttributeType::EOS_AT_STRING; D.Value.AsUtf8 = Room.SessionId.c_str();
        Result = EOS_LobbyModification_AddAttribute(Modification, &Pub);
        D.Key = "match_state"; D.Value.AsUtf8 = Room.Phase == RoomPhase::Running ? "running" : Room.Phase == RoomPhase::Complete ? "complete" : "waiting";
        if (Result == EOS_EResult::EOS_Success) Result = EOS_LobbyModification_AddAttribute(Modification, &Pub);
        const auto& S = Room.LobbySettings;
        const std::pair<const char*, const std::string*> Custom[] = {
            {"map", &S.MapName}, {"mode", &S.ModeName}, {"region", &S.Region}, {"note", &S.Note},
            {"style", &S.Style}, {"language", &S.LanguageTag}};
        for (const auto& [Key, Value] : Custom)
        {
            if (Result != EOS_EResult::EOS_Success || Value->empty()) continue;
            D.Key = Key; D.ValueType = EOS_ELobbyAttributeType::EOS_AT_STRING; D.Value.AsUtf8 = Value->c_str();
            Result = EOS_LobbyModification_AddAttribute(Modification, &Pub);
        }
        if (Result == EOS_EResult::EOS_Success && LiveSettingsDirty)
        {
            EOS_LobbyModification_SetMaxMembersOptions M{}; M.ApiVersion = EOS_LOBBYMODIFICATION_SETMAXMEMBERS_API_LATEST;
            M.MaxMembers = std::clamp<unsigned>(Room.LobbySettings.MaxMembers, 2, 16);
            Result = EOS_LobbyModification_SetMaxMembers(Modification, &M);
            if (Result == EOS_EResult::EOS_Success)
            {
                EOS_LobbyModification_SetPermissionLevelOptions P{}; P.ApiVersion = EOS_LOBBYMODIFICATION_SETPERMISSIONLEVEL_API_LATEST;
                P.PermissionLevel = ToEosLobbyPermission(Room.LobbySettings.Permission);
                Result = EOS_LobbyModification_SetPermissionLevel(Modification, &P);
            }
            if (Result == EOS_EResult::EOS_Success)
            {
                EOS_LobbyModification_SetInvitesAllowedOptions Iv{}; Iv.ApiVersion = EOS_LOBBYMODIFICATION_SETINVITESALLOWED_API_LATEST;
                Iv.bInvitesAllowed = Room.LobbySettings.AllowInvites ? EOS_TRUE : EOS_FALSE;
                Result = EOS_LobbyModification_SetInvitesAllowed(Modification, &Iv);
            }
            if (Result == EOS_EResult::EOS_Success && ValidBucketId(Room.LobbySettings.BucketId))
            {
                EOS_LobbyModification_SetBucketIdOptions Bk{}; Bk.ApiVersion = EOS_LOBBYMODIFICATION_SETBUCKETID_API_LATEST;
                Bk.BucketId = Room.LobbySettings.BucketId.c_str();
                Result = EOS_LobbyModification_SetBucketId(Modification, &Bk);
            }
        }
    }
    AttributesDirty = false; OwnerAttributesDirty = false; LiveSettingsDirty = false;
    if (Success("lobby_attributes", Result))
    {
        EOS_Lobby_UpdateLobbyOptions U{}; U.ApiVersion = EOS_LOBBY_UPDATELOBBY_API_LATEST; U.LobbyModificationHandle = Modification;
        Room.ReadyPending = true; EOS_Lobby_UpdateLobby(Lobby, &U, Cookie(), AttributesDone);
    }
    EOS_LobbyModification_Release(Modification);
}
void EOS_CALL Prepared(const EOS_Sessions_UpdateSessionCallbackInfo* C)
{
    if (!Current(C->ClientData)) return;
    Operation = false;
    if (!Success("session_create", C->ResultCode))
    {
        Room.Phase = RoomPhase::Failed; AutoArmed = false;
        Room.SessionStatus = std::string("Match session failed: ") + EOS_EResult_ToString(C->ResultCode);
        SyncSessionFlags(); return;
    }
    OwnerAttributesDirty = true;
    SessionExists = true; Room.SessionId = C->SessionId ? C->SessionId : "";
    Room.SessionOwner = true; Room.SessionJoined = false; Room.SessionWithoutRegistration = false;
    Room.Phase = RoomPhase::Waiting; Room.Status = "Lobby open - match session prepared";
    Room.SessionStatus = "Match session prepared. Waiting for readiness.";
    SyncSessionFlags();
    RecordHistory("session", Room);
}
void AddSessionStringAttribute(EOS_HSessionModification M, const char* Key, const std::string& Value, EOS_EResult& Result)
{
    if (Result != EOS_EResult::EOS_Success || Value.empty()) return;
    EOS_Sessions_AttributeData D{}; D.ApiVersion = EOS_SESSIONS_ATTRIBUTEDATA_API_LATEST;
    D.Key = Key; D.ValueType = EOS_ESessionAttributeType::EOS_AT_STRING; D.Value.AsUtf8 = Value.c_str();
    EOS_SessionModification_AddAttributeOptions A{}; A.ApiVersion = EOS_SESSIONMODIFICATION_ADDATTRIBUTE_API_LATEST;
    A.SessionAttribute = &D; A.AdvertisementType = EOS_ESessionAttributeAdvertisementType::EOS_SAAT_Advertise;
    Result = EOS_SessionModification_AddAttribute(M, &A);
}
void Prepare()
{
    if (!User || !Sessions || !Room.Owner || SessionExists || Operation || WantLeave) return;
    if (!ValidSessionName(Room.SessionSettings.SessionName) || !ValidBucketId(Room.SessionSettings.BucketId))
    {
        Room.Phase = RoomPhase::Failed;
        Room.SessionStatus = "Session settings invalid. Check session name and bucket.";
        return;
    }
    Room.Phase = RoomPhase::Preparing;
    Room.SessionStatus = "Preparing match session...";
    SyncSessionFlags();
    const auto& S = Room.SessionSettings;
    EOS_Sessions_CreateSessionModificationOptions O{};
    O.ApiVersion = EOS_SESSIONS_CREATESESSIONMODIFICATION_API_LATEST; O.SessionName = S.SessionName.c_str();
    O.BucketId = S.BucketId.c_str(); O.MaxPlayers = std::clamp<unsigned>(S.MaxPlayers, 2, 16);
    O.LocalUserId = User; O.bPresenceEnabled = S.PresenceEnabled ? EOS_TRUE : EOS_FALSE;
    EOS_HSessionModification M = nullptr;
    if (!Success("session_prepare", EOS_Sessions_CreateSessionModification(Sessions, &O, &M))) { Room.Phase = RoomPhase::Failed; return; }
    EOS_SessionModification_SetPermissionLevelOptions P{}; P.ApiVersion = EOS_SESSIONMODIFICATION_SETPERMISSIONLEVEL_API_LATEST;
    P.PermissionLevel = ToEosSessionPermission(S.Permission);
    auto Result = EOS_SessionModification_SetPermissionLevel(M, &P);
    EOS_SessionModification_SetJoinInProgressAllowedOptions J{}; J.ApiVersion = EOS_SESSIONMODIFICATION_SETJOININPROGRESSALLOWED_API_LATEST;
    J.bAllowJoinInProgress = S.JoinInProgressAllowed ? EOS_TRUE : EOS_FALSE;
    if (Result == EOS_EResult::EOS_Success) Result = EOS_SessionModification_SetJoinInProgressAllowed(M, &J);
    EOS_SessionModification_SetMaxPlayersOptions Mx{}; Mx.ApiVersion = EOS_SESSIONMODIFICATION_SETMAXPLAYERS_API_LATEST;
    Mx.MaxPlayers = std::clamp<unsigned>(S.MaxPlayers, 2, 16);
    if (Result == EOS_EResult::EOS_Success) Result = EOS_SessionModification_SetMaxPlayers(M, &Mx);
    EOS_SessionModification_SetInvitesAllowedOptions Iv{}; Iv.ApiVersion = EOS_SESSIONMODIFICATION_SETINVITESALLOWED_API_LATEST;
    Iv.bInvitesAllowed = EOS_TRUE;
    if (Result == EOS_EResult::EOS_Success) Result = EOS_SessionModification_SetInvitesAllowed(M, &Iv);
    AddSessionStringAttribute(M, "map", S.MapName, Result);
    AddSessionStringAttribute(M, "mode", S.ModeName, Result);
    if (Success("session_options", Result))
    {
        EOS_Sessions_UpdateSessionOptions U{}; U.ApiVersion = EOS_SESSIONS_UPDATESESSION_API_LATEST; U.SessionModificationHandle = M;
        Operation = true; EOS_Sessions_UpdateSession(Sessions, &U, Cookie(), Prepared);
    }
    else Room.Phase = RoomPhase::Failed;
    EOS_SessionModification_Release(M);
    SyncSessionFlags();
}
void Created()
{
    Dirty = true; AttributesDirty = true; OwnerAttributesDirty = true; ReadyDesired = false; AutoArmed = true;
    Room.Status = "Lobby created - joining EOS voice muted"; Room.Phase = RoomPhase::Waiting;
    Room.SessionStatus = Room.Owner ? "Preparing match session..." : "Host manages the match session.";
    Refresh(); Prepare();
}
void EOS_CALL LobbyCreated(const EOS_Lobby_CreateLobbyCallbackInfo* C)
{
    if (!Current(C->ClientData)) return;
    Operation = false;
    if (!Success("lobby_create", C->ResultCode)) { Room.Phase = RoomPhase::Failed; return; }
    Room.LobbyId = C->LobbyId; Created();
}
void EOS_CALL Joined(const EOS_Lobby_JoinLobbyByIdCallbackInfo* C)
{
    if (!Current(C->ClientData)) return;
    Operation = false;
    if (!Success("lobby_join", C->ResultCode)) { Room.Phase = RoomPhase::Failed; return; }
    Room.LobbyId = C->LobbyId; Dummies = 0; Created();
}
void EOS_CALL Started(const EOS_Sessions_StartSessionCallbackInfo* C)
{
    if (!Current(C->ClientData)) return;
    Operation = false;
    if (!Success("session_start", C->ResultCode)) { Room.Phase = RoomPhase::Failed; AutoArmed = false; SyncSessionFlags(); return; }
    OwnerAttributesDirty = true;
    Room.Phase = RoomPhase::Running; MatchClock = std::chrono::steady_clock::now();
    Room.StartedAt = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now()); MatchRecorded = false;
    Room.Status = "EOS match session in progress - lifecycle test, no gameplay simulation";
    Room.SessionStatus = Room.SessionWithoutRegistration ?
        "Match running WITHOUT player registration (client policy). Lifecycle only." : "Match session running.";
    SyncSessionFlags();
    Refresh();
    if (WantLeave || !EveryoneReady(Room) || (!Room.SessionWithoutRegistration && RealMembers != Registered)) EndRoomMatch();
}
void RequestStartSession()
{
    EOS_Sessions_StartSessionOptions O{}; O.ApiVersion = EOS_SESSIONS_STARTSESSION_API_LATEST; O.SessionName = SessionNameC();
    Operation = true; SyncSessionFlags(); EOS_Sessions_StartSession(Sessions, &O, Cookie(), Started);
}
void EOS_CALL RegisteredPlayers(const EOS_Sessions_RegisterPlayersCallbackInfo* C)
{
    if (!Current(C->ClientData)) return;
    Operation = false;
    if (C->ResultCode == EOS_EResult::EOS_ClientPolicyMissingAction)
    {
        Report("session_register_real_players", C->ResultCode);
        Room.SessionWithoutRegistration = true;
        Room.SessionStatus = "RegisterPlayers blocked by EOS Client Policy. Enable Sessions actions for this client in the Dev Portal, or continue unregistered for lifecycle tests.";
        if (Log) Log("session_policy=missing; grant Sessions Register/Unregister/Start/End/Destroy to the Dev client policy, then retry. Continuing unregistered is lifecycle-only.");
        Room.Phase = RoomPhase::Waiting; AutoArmed = false; SyncSessionFlags(); return;
    }
    if (!Success("session_register_real_players", C->ResultCode)) { Room.Phase = RoomPhase::Failed; AutoArmed = false; SyncSessionFlags(); return; }
    Refresh();
    if (WantLeave || !EveryoneReady(Room) || RealMembers != Registered)
    { Room.Phase = RoomPhase::Failed; AutoArmed = false; Room.Status = "Readiness or membership changed. Start cancelled."; SyncSessionFlags(); return; }
    RequestStartSession();
}
void EOS_CALL Ended(const EOS_Sessions_EndSessionCallbackInfo* C)
{
    if (!Current(C->ClientData)) return;
    Operation = false;
    if (!Success("session_end", C->ResultCode)) { Room.Phase = RoomPhase::Running; AutoArmed = false; WantLeave = false; SyncSessionFlags(); return; }
    SaveMatch(WantLeave ? "abandoned" : "completed");
    OwnerAttributesDirty = true;
    Room.Phase = RoomPhase::Complete; Room.Status = "Match ended - history queued for EOS cloud sync";
    Room.SessionStatus = "Match ended. Prepare next match or leave.";
    SyncSessionFlags();
    ResetDummyReadiness();
}
void EOS_CALL Unregistered(const EOS_Sessions_UnregisterPlayersCallbackInfo* C)
{
    if (!Current(C->ClientData)) return;
    UnregisterPending = false; Operation = false;
    Report("session_unregister", C->ResultCode);
    Registered.clear(); SyncSessionFlags();
}
void RequestUnregisterAll()
{
    if (!Sessions || Registered.empty() || UnregisterPending || !SessionExists) return;
    EOS_Sessions_UnregisterPlayersOptions O{}; O.ApiVersion = EOS_SESSIONS_UNREGISTERPLAYERS_API_LATEST;
    O.SessionName = SessionNameC(); O.PlayersToUnregister = Registered.data();
    O.PlayersToUnregisterCount = static_cast<uint32_t>(Registered.size());
    UnregisterPending = true; Operation = true; SyncSessionFlags();
    EOS_Sessions_UnregisterPlayers(Sessions, &O, Cookie(), Unregistered);
}
void EOS_CALL DestroyedSession(const EOS_Sessions_DestroySessionCallbackInfo* C)
{
    if (!Current(C->ClientData)) return;
    Operation = false;
    if (!Success("session_destroy", C->ResultCode)) { Room.Phase = RoomPhase::Failed; WantLeave = false; SyncSessionFlags(); return; }
    SessionExists = false; Room.SessionId.clear(); Registered.clear(); Room.StartedAt = 0;
    Room.SessionOwner = false; Room.SessionJoined = false; JoinSessionPending = false;
    Room.SessionStatus = WantLeave ? "Match session closed." : "Match session destroyed.";
    SyncSessionFlags();
    if (!WantLeave) { ResetDummyReadiness(); AutoArmed = true; Prepare(); }
}
void DestroySessionAsync()
{
    if (!Sessions || !SessionExists || Operation) return;
    if (!Registered.empty() && Room.SessionOwner && !UnregisterPending)
    {
        // Unregister first; Tick will destroy once the unregister completes.
        RequestUnregisterAll(); return;
    }
    if (UnregisterPending) return;
    EOS_Sessions_DestroySessionOptions O{}; O.ApiVersion = EOS_SESSIONS_DESTROYSESSION_API_LATEST; O.SessionName = SessionNameC();
    Operation = true; SyncSessionFlags(); EOS_Sessions_DestroySession(Sessions, &O, Cookie(), DestroyedSession);
}
void FinishLeave(EOS_EResult Result)
{
    Operation = false; LobbyLeaveSent = false;
    if (Result != EOS_EResult::EOS_Success && Result != EOS_EResult::EOS_NotFound)
    { Report("lobby_leave", Result); WantLeave = false; Room.Phase = RoomPhase::Failed; SyncSessionFlags(); return; }
    Room.LobbyId.clear(); Room.Players.clear(); RealMembers.clear(); VoiceRoom.clear();
    Room.VoiceConnected = Room.Microphone = false; Room.Owner = false; Room.Phase = RoomPhase::Offline;
    Room.Status = "Lobby closed"; Room.SessionStatus = "No match session."; WantLeave = false;
    SyncSessionFlags();
    if (!JoinAfterLeave.empty()) { auto Id = JoinAfterLeave; JoinAfterLeave.clear(); JoinRoom(Id.c_str()); }
}
void EOS_CALL DestroyedLobby(const EOS_Lobby_DestroyLobbyCallbackInfo* C) { if (Current(C->ClientData)) FinishLeave(C->ResultCode); }
void EOS_CALL LeftLobby(const EOS_Lobby_LeaveLobbyCallbackInfo* C) { if (Current(C->ClientData)) FinishLeave(C->ResultCode); }
void EOS_CALL Sending(const EOS_RTCAudio_UpdateSendingCallbackInfo* C)
{
    if (!Current(C->ClientData)) return;
    Room.VoicePending = false;
    const bool Ok = Success("rtc_sending", C->ResultCode);
    if (Ok) Room.Microphone = PendingMicrophone;
    Room.VoiceStatus = Ok ? (Room.Microphone ? "Microphone on - EOS RTC" : "Microphone muted - EOS RTC") : "Microphone update failed";
}
void EOS_CALL Receiving(const EOS_RTCAudio_UpdateReceivingCallbackInfo* C)
{
    if (!Current(C->ClientData)) return;
    Room.VoicePending = false;
    if (Success("rtc_receiving", C->ResultCode)) Room.Listening = PendingListening;
    else Room.VoiceStatus = "Speaker update failed; previous state retained";
}
// ---- Search ----
void ParseLobbyDetails(EOS_HLobbyDetails Details, LobbySearchResult& Out)
{
    EOS_LobbyDetails_CopyInfoOptions I{}; I.ApiVersion = EOS_LOBBYDETAILS_COPYINFO_API_LATEST;
    EOS_LobbyDetails_Info* Info = nullptr;
    if (EOS_LobbyDetails_CopyInfo(Details, &I, &Info) == EOS_EResult::EOS_Success && Info)
    {
        if (Info->LobbyId) Out.LobbyId = Info->LobbyId;
        if (Info->BucketId) Out.BucketId = Info->BucketId;
        Out.MaxMembers = Info->MaxMembers;
        Out.Members = Info->MaxMembers > Info->AvailableSlots ? Info->MaxMembers - Info->AvailableSlots : 0;
        Out.Permission = static_cast<int>(Info->PermissionLevel);
        Out.RtcEnabled = Info->bRTCRoomEnabled == EOS_TRUE;
        EOS_LobbyDetails_Info_Release(Info);
    }
    const std::pair<const char*, std::string*> Keys[] = {
        {"map", &Out.MapName}, {"mode", &Out.ModeName}, {"region", &Out.Region},
        {"style", &Out.Style}, {"language", &Out.Language}, {"match_state", &Out.MatchState}};
    for (const auto& [Key, Value] : Keys)
    {
        EOS_LobbyDetails_CopyAttributeByKeyOptions A{}; A.ApiVersion = EOS_LOBBYDETAILS_COPYATTRIBUTEBYKEY_API_LATEST; A.AttrKey = Key;
        EOS_Lobby_Attribute* V = nullptr;
        if (EOS_LobbyDetails_CopyAttributeByKey(Details, &A, &V) == EOS_EResult::EOS_Success && V)
        {
            if (V->Data && V->Data->ValueType == EOS_ELobbyAttributeType::EOS_AT_STRING && V->Data->Value.AsUtf8)
                *Value = V->Data->Value.AsUtf8;
            EOS_Lobby_Attribute_Release(V);
        }
    }
    EOS_LobbyDetails_GetMemberCountOptions C{}; C.ApiVersion = EOS_LOBBYDETAILS_GETMEMBERCOUNT_API_LATEST;
    const uint32_t N = EOS_LobbyDetails_GetMemberCount(Details, &C);
    if (N && !Out.Members) Out.Members = std::min<uint32_t>(N, 64);
}
void EOS_CALL LobbySearchDone(const EOS_LobbySearch_FindCallbackInfo* C)
{
    const bool Mine = Current(C->ClientData);
    Room.Search.LobbyBusy = false;
    EOS_HLobbySearch Handle = LobbySearchHandle; LobbySearchHandle = nullptr;
    if (!Mine) { if (Handle) EOS_LobbySearch_Release(Handle); return; }
    if (C->ResultCode != EOS_EResult::EOS_Success)
    {
        Report("lobby_search", C->ResultCode);
        Room.Search.LobbyStatus = std::string("Lobby search failed: ") + EOS_EResult_ToString(C->ResultCode);
        if (Handle) EOS_LobbySearch_Release(Handle); return;
    }
    Report("lobby_search", C->ResultCode);
    Room.Search.Lobbies.clear();
    if (Handle)
    {
        EOS_LobbySearch_GetSearchResultCountOptions O{}; O.ApiVersion = EOS_LOBBYSEARCH_GETSEARCHRESULTCOUNT_API_LATEST;
        const uint32_t N = EOS_LobbySearch_GetSearchResultCount(Handle, &O);
        for (uint32_t I = 0; I < std::min<uint32_t>(N, 20); ++I)
        {
            EOS_LobbySearch_CopySearchResultByIndexOptions S{}; S.ApiVersion = EOS_LOBBYSEARCH_COPYSEARCHRESULTBYINDEX_API_LATEST; S.LobbyIndex = I;
            EOS_HLobbyDetails Details = nullptr;
            if (EOS_LobbySearch_CopySearchResultByIndex(Handle, &S, &Details) != EOS_EResult::EOS_Success || !Details) continue;
            LobbySearchResult R; ParseLobbyDetails(Details, R);
            EOS_LobbyDetails_Release(Details);
            if (!R.LobbyId.empty()) Room.Search.Lobbies.push_back(R);
        }
        EOS_LobbySearch_Release(Handle);
    }
    Room.Search.LobbyStatus = Room.Search.Lobbies.empty() ?
        "No public lobbies found. Create one or check the bucket filter." :
        std::to_string(Room.Search.Lobbies.size()) + " lobbies found.";
}
void ParseSessionDetails(EOS_HSessionDetails Details, SessionSearchResult& Out)
{
    EOS_SessionDetails_CopyInfoOptions I{}; I.ApiVersion = EOS_SESSIONDETAILS_COPYINFO_API_LATEST;
    EOS_SessionDetails_Info* Info = nullptr;
    if (EOS_SessionDetails_CopyInfo(Details, &I, &Info) == EOS_EResult::EOS_Success && Info)
    {
        if (Info->SessionId) Out.SessionId = Info->SessionId;
        Out.OpenConnections = Info->NumOpenPublicConnections;
        if (Info->Settings)
        {
            if (Info->Settings->BucketId) Out.BucketId = Info->Settings->BucketId;
            Out.MaxConnections = Info->Settings->NumPublicConnections;
            Out.Permission = static_cast<int>(Info->Settings->PermissionLevel);
            Out.JoinInProgress = Info->Settings->bAllowJoinInProgress == EOS_TRUE;
        }
        EOS_SessionDetails_Info_Release(Info);
    }
    for (const char* Key : {"map", "mode"})
    {
        EOS_SessionDetails_CopySessionAttributeByKeyOptions A{}; A.ApiVersion = EOS_SESSIONDETAILS_COPYSESSIONATTRIBUTEBYKEY_API_LATEST; A.AttrKey = Key;
        EOS_SessionDetails_Attribute* V = nullptr;
        if (EOS_SessionDetails_CopySessionAttributeByKey(Details, &A, &V) == EOS_EResult::EOS_Success && V)
        {
            if (V->Data && V->Data->ValueType == EOS_ESessionAttributeType::EOS_AT_STRING && V->Data->Value.AsUtf8)
            {
                if (std::strcmp(Key, "map") == 0) Out.MapName = V->Data->Value.AsUtf8;
                else Out.ModeName = V->Data->Value.AsUtf8;
            }
            EOS_SessionDetails_Attribute_Release(V);
        }
    }
}
void EOS_CALL SessionSearchDone(const EOS_SessionSearch_FindCallbackInfo* C)
{
    const bool Mine = Current(C->ClientData);
    Room.Search.SessionBusy = false;
    EOS_HSessionSearch Handle = SessionSearchHandle; SessionSearchHandle = nullptr;
    if (!Mine) { if (Handle) EOS_SessionSearch_Release(Handle); return; }
    if (C->ResultCode != EOS_EResult::EOS_Success)
    {
        Report("session_search", C->ResultCode);
        Room.Search.SessionStatus = std::string("Session search failed: ") + EOS_EResult_ToString(C->ResultCode);
        if (Handle) EOS_SessionSearch_Release(Handle); return;
    }
    Report("session_search", C->ResultCode);
    Room.Search.Sessions.clear();
    for (auto H : SessionResultHandles) if (H) EOS_SessionDetails_Release(H);
    SessionResultHandles.clear();
    if (Handle)
    {
        EOS_SessionSearch_GetSearchResultCountOptions O{}; O.ApiVersion = EOS_SESSIONSEARCH_GETSEARCHRESULTCOUNT_API_LATEST;
        const uint32_t N = EOS_SessionSearch_GetSearchResultCount(Handle, &O);
        for (uint32_t I = 0; I < std::min<uint32_t>(N, 20); ++I)
        {
            EOS_SessionSearch_CopySearchResultByIndexOptions S{}; S.ApiVersion = EOS_SESSIONSEARCH_COPYSEARCHRESULTBYINDEX_API_LATEST; S.SessionIndex = I;
            EOS_HSessionDetails Details = nullptr;
            if (EOS_SessionSearch_CopySearchResultByIndex(Handle, &S, &Details) != EOS_EResult::EOS_Success || !Details) continue;
            SessionSearchResult R; ParseSessionDetails(Details, R);
            if (!R.SessionId.empty()) { Room.Search.Sessions.push_back(R); SessionResultHandles.push_back(Details); }
            else EOS_SessionDetails_Release(Details);
        }
        EOS_SessionSearch_Release(Handle);
    }
    Room.Search.SessionStatus = Room.Search.Sessions.empty() ?
        "No sessions found. New sessions can take a few seconds to index." :
        std::to_string(Room.Search.Sessions.size()) + " sessions found.";
    // Direct join-by-session-ID flow for lobby members.
    if (!JoinSessionTargetId.empty() && !Room.Search.Sessions.empty())
    {
        for (size_t I = 0; I < Room.Search.Sessions.size(); ++I)
            if (Room.Search.Sessions[I].SessionId == JoinSessionTargetId) { JoinSessionTargetId.clear(); JoinSessionResult(I); break; }
        JoinSessionTargetId.clear();
    }
    else JoinSessionTargetId.clear();
}
void EOS_CALL JoinedSession(const EOS_Sessions_JoinSessionCallbackInfo* C)
{
    if (!Current(C->ClientData)) return;
    JoinSessionPending = false; Operation = false;
    if (!Success("session_join", C->ResultCode))
    {
        Room.SessionStatus = std::string("Join match session failed: ") + EOS_EResult_ToString(C->ResultCode);
        SyncSessionFlags(); return;
    }
    SessionExists = true; Room.SessionJoined = true; Room.SessionOwner = false;
    Room.SessionStatus = "Joined host match session.";
    SyncSessionFlags();
    if (Log) Log("session_join=success; local match session created from host session");
}
}
const RoomReading& InspectRoom() noexcept { return Room; }
bool RoomIsClosed() noexcept { return !Operation && !SessionExists && Room.LobbyId.empty(); }
void BindRoomRuntime(EOS_HPlatform Platform, EOS_ProductUserId LocalUser, DiagnosticReception Reception,
    const std::filesystem::path& Root, bool CloudEnabled)
{
    DetachRoomRuntime();
    User = LocalUser; Log = Reception; Lobby = EOS_Platform_GetLobbyInterface(Platform); Sessions = EOS_Platform_GetSessionsInterface(Platform);
    auto Rtc = EOS_Platform_GetRTCInterface(Platform); Audio = Rtc ? EOS_RTC_GetAudioInterface(Rtc) : nullptr;
    Room.LobbySettings = PendingLobbySettings; Room.SessionSettings = PendingSessionSettings;
    BindHistory(Platform, User, Log, Root, CloudEnabled); RecordHistory("login", Room);
    if (!Lobby || !Sessions) { Room.Phase = RoomPhase::Failed; Room.Status = "EOS lobby/session interfaces unavailable"; return; }
    Subscribe(); CreateRoom();
}
bool CreateRoom()
{
    if (!Lobby || !User || Operation || !Room.LobbyId.empty() || SessionExists) return false;
    const auto& S = Room.LobbySettings;
    if (!ValidBucketId(S.BucketId) || S.MaxMembers < 2 || S.MaxMembers > 16 ||
        !ValidLobbyAttribute(S.MapName) || !ValidLobbyAttribute(S.ModeName) ||
        !ValidLobbyAttribute(S.Region) || !ValidLobbyAttribute(S.Note) ||
        !ValidLobbyAttribute(S.Style) || !ValidLobbyAttribute(S.LanguageTag))
    {
        Room.Phase = RoomPhase::Failed; Room.Status = "Lobby settings invalid. Check bucket and attributes.";
        return false;
    }
    WantLeave = false; AutoArmed = true; ResetDummyReadiness();
    Room.Phase = RoomPhase::Creating; Room.Status = "Creating EOS lobby and voice room...";
    EOS_Lobby_LocalRTCOptions R{}; R.ApiVersion = EOS_LOBBY_LOCALRTCOPTIONS_API_LATEST; R.bLocalAudioDeviceInputStartsMuted = EOS_TRUE;
    EOS_Lobby_CreateLobbyOptions O{}; O.ApiVersion = EOS_LOBBY_CREATELOBBY_API_LATEST; O.LocalUserId = User;
    O.MaxLobbyMembers = S.MaxMembers; O.PermissionLevel = ToEosLobbyPermission(S.Permission);
    O.bPresenceEnabled = S.PresenceEnabled ? EOS_TRUE : EOS_FALSE;
    O.bAllowInvites = S.AllowInvites ? EOS_TRUE : EOS_FALSE; O.BucketId = S.BucketId.c_str();
    O.bDisableHostMigration = S.DisableHostMigration ? EOS_TRUE : EOS_FALSE;
    O.bEnableRTCRoom = S.EnableRtcRoom ? EOS_TRUE : EOS_FALSE;
    O.LocalRTCOptions = S.EnableRtcRoom ? &R : nullptr;
    O.bEnableJoinById = S.EnableJoinById ? EOS_TRUE : EOS_FALSE;
    O.bRejoinAfterKickRequiresInvite = S.RejoinAfterKickRequiresInvite ? EOS_TRUE : EOS_FALSE;
    O.RTCRoomJoinActionType = S.RtcAutoJoin ?
        EOS_ELobbyRTCRoomJoinActionType::EOS_LRRJAT_AutomaticJoin : EOS_ELobbyRTCRoomJoinActionType::EOS_LRRJAT_ManualJoin;
    Operation = true; EOS_Lobby_CreateLobby(Lobby, &O, Cookie(), LobbyCreated); return true;
}
bool JoinRoom(const char* Id)
{
    if (!Lobby || !Id || !*Id || std::strlen(Id) > 128 || Operation || Room.Phase == RoomPhase::Running || Room.Phase == RoomPhase::Starting) return false;
    if (!RoomIsClosed()) { JoinAfterLeave = Id; LeaveRoom(); return true; }
    EOS_Lobby_LocalRTCOptions R{}; R.ApiVersion = EOS_LOBBY_LOCALRTCOPTIONS_API_LATEST; R.bLocalAudioDeviceInputStartsMuted = EOS_TRUE;
    EOS_Lobby_JoinLobbyByIdOptions O{}; O.ApiVersion = EOS_LOBBY_JOINLOBBYBYID_API_LATEST;
    O.LobbyId = Id; O.LocalUserId = User; O.bPresenceEnabled = EOS_TRUE; O.LocalRTCOptions = &R;
    O.RTCRoomJoinActionType = EOS_ELobbyRTCRoomJoinActionType::EOS_LRRJAT_AutomaticJoin;
    Operation = true; Room.Phase = RoomPhase::Creating; EOS_Lobby_JoinLobbyById(Lobby, &O, Cookie(), Joined); return true;
}
bool SetRoomReady(bool Ready)
{
    if (Room.Phase != RoomPhase::Waiting || Room.ReadyPending || Operation || !User || WantLeave) return false;
    ReadyDesired = Ready; AttributesDirty = true; AutoArmed = true; PublishAttributes(); return true;
}
void SetRoomDisplayName(const char* Value) { Name = Value && *Value ? Value : "You"; if (Name.size() > 64) Name.resize(64); AttributesDirty = true; Dirty = true; }
void SetDummyCount(unsigned Count)
{ if (Room.Phase == RoomPhase::Waiting && Room.Owner) { Dummies = std::min(Count, 3u); DummyReady.fill(false); Dirty = true; AutoArmed = true; } }
void SetDummyReady(unsigned Index, bool Ready)
{ if (Room.Phase == RoomPhase::Waiting && Room.Owner && Index < Dummies) { DummyReady[Index] = Ready; Dirty = true; AutoArmed = true; } }
void SetAutoStart(bool Enabled) { Room.AutoStart = Enabled; AutoArmed = true; }
bool StartRoomMatch()
{
    if (!Sessions || !SessionExists || Operation || Room.ReadyPending || AttributesDirty || WantLeave || Room.Phase != RoomPhase::Waiting || !Room.Owner) return false;
    Refresh(); if (!EveryoneReady(Room) || RealMembers.empty()) return false;
    if (Room.SessionWithoutRegistration)
    {
        // Client policy blocked RegisterPlayers; allow a lifecycle-only start.
        Room.Phase = RoomPhase::Starting; Room.Status = "Starting match without player registration (client policy)...";
        if (Log) Log("session_start=unregistered; RegisterPlayers was blocked by client policy");
        RequestStartSession(); return true;
    }
    Registered = RealMembers; Room.Phase = RoomPhase::Starting; Room.Status = "Registering real EOS members and starting match...";
    EOS_Sessions_RegisterPlayersOptions O{}; O.ApiVersion = EOS_SESSIONS_REGISTERPLAYERS_API_LATEST;
    O.SessionName = SessionNameC(); O.PlayersToRegister = Registered.data(); O.PlayersToRegisterCount = static_cast<uint32_t>(Registered.size());
    Operation = true; AutoArmed = false; SyncSessionFlags(); EOS_Sessions_RegisterPlayers(Sessions, &O, Cookie(), RegisteredPlayers); return true;
}
bool EndRoomMatch()
{
    if (!Sessions || !Room.Owner || !SessionExists || Operation || Room.Phase != RoomPhase::Running) return false;
    Room.Phase = RoomPhase::Ending; Operation = true; SyncSessionFlags();
    EOS_Sessions_EndSessionOptions O{}; O.ApiVersion = EOS_SESSIONS_ENDSESSION_API_LATEST; O.SessionName = SessionNameC();
    EOS_Sessions_EndSession(Sessions, &O, Cookie(), Ended); return true;
}
bool PrepareNextMatch()
{
    if (!Sessions || !Room.Owner || Operation || WantLeave || (Room.Phase != RoomPhase::Complete && Room.Phase != RoomPhase::Failed)) return false;
    if (!SessionExists) { Prepare(); return true; }
    DestroySessionAsync(); return true;
}
bool SetRoomMicrophone(bool Enabled)
{
    if (!Audio || !Room.VoiceConnected || VoiceRoom.empty() || Room.VoicePending || WantLeave) return false;
    EOS_RTCAudio_UpdateSendingOptions O{}; O.ApiVersion = EOS_RTCAUDIO_UPDATESENDING_API_LATEST;
    O.LocalUserId = User; O.RoomName = VoiceRoom.c_str();
    O.AudioStatus = Enabled ? EOS_ERTCAudioStatus::EOS_RTCAS_Enabled : EOS_ERTCAudioStatus::EOS_RTCAS_Disabled;
    PendingMicrophone = Enabled; Room.VoicePending = true; EOS_RTCAudio_UpdateSending(Audio, &O, Cookie(), Sending); return true;
}
bool SetRoomListening(bool Enabled)
{
    if (!Audio || !Room.VoiceConnected || VoiceRoom.empty() || Room.VoicePending || WantLeave) return false;
    EOS_RTCAudio_UpdateReceivingOptions O{}; O.ApiVersion = EOS_RTCAUDIO_UPDATERECEIVING_API_LATEST;
    O.LocalUserId = User; O.RoomName = VoiceRoom.c_str(); O.bAudioEnabled = Enabled ? EOS_TRUE : EOS_FALSE;
    PendingListening = Enabled; Room.VoicePending = true; EOS_RTCAudio_UpdateReceiving(Audio, &O, Cookie(), Receiving); return true;
}
void LeaveRoom() { if (!User) return; WantLeave = true; AutoArmed = false; }
bool ApplyLobbySettings(const LobbyCreationSettings& Settings)
{
    if (!ValidBucketId(Settings.BucketId) || Settings.MaxMembers < 2 || Settings.MaxMembers > 16 ||
        !ValidLobbyAttribute(Settings.MapName) || !ValidLobbyAttribute(Settings.ModeName) ||
        !ValidLobbyAttribute(Settings.Region) || !ValidLobbyAttribute(Settings.Note) ||
        !ValidLobbyAttribute(Settings.Style) || !ValidLobbyAttribute(Settings.LanguageTag)) return false;
    PendingLobbySettings = Settings; Room.LobbySettings = Settings;
    if (!Room.LobbyId.empty() && Room.Owner) { OwnerAttributesDirty = true; LiveSettingsDirty = true; }
    Dirty = true; return true;
}
bool ApplySessionSettings(const MatchSessionSettings& Settings)
{
    if (!ValidSessionName(Settings.SessionName) || !ValidBucketId(Settings.BucketId) ||
        Settings.MaxPlayers < 2 || Settings.MaxPlayers > 16 ||
        !ValidLobbyAttribute(Settings.MapName) || !ValidLobbyAttribute(Settings.ModeName)) return false;
    PendingSessionSettings = Settings; Room.SessionSettings = Settings;
    return true;
}
bool UpdateLobbyLiveSettings()
{
    if (!Lobby || Room.LobbyId.empty() || !Room.Owner || WantLeave) return false;
    OwnerAttributesDirty = true; LiveSettingsDirty = true; PublishAttributes(); return true;
}
bool RefreshLobbySearch(const LobbyTagFilter& Tags, const char* TextFilter)
{
    if (!Lobby || !User || Room.Search.LobbyBusy) return false;
    if (LobbySearchHandle) { EOS_LobbySearch_Release(LobbySearchHandle); LobbySearchHandle = nullptr; }
    Room.Search.LobbyFilter = TextFilter ? TextFilter : "";
    Room.Search.ActiveTags = Tags; // owns tag strings for the async Find below
    Room.Search.Lobbies.clear();
    EOS_Lobby_CreateLobbySearchOptions O{}; O.ApiVersion = EOS_LOBBY_CREATELOBBYSEARCH_API_LATEST; O.MaxResults = 20;
    if (EOS_Lobby_CreateLobbySearch(Lobby, &O, &LobbySearchHandle) != EOS_EResult::EOS_Success || !LobbySearchHandle)
    { Room.Search.LobbyStatus = "Could not create lobby search."; return false; }
    const std::pair<const char*, const std::string*> Params[] = {
        {"style", &Room.Search.ActiveTags.Style}, {"language", &Room.Search.ActiveTags.Language},
        {"map", &Room.Search.ActiveTags.Map}, {"mode", &Room.Search.ActiveTags.Mode}};
    for (const auto& [Key, Value] : Params)
    {
        if (Value->empty()) continue;
        EOS_Lobby_AttributeData Data{};
        Data.ApiVersion = EOS_LOBBY_ATTRIBUTEDATA_API_LATEST;
        Data.Key = Key;
        Data.ValueType = EOS_ELobbyAttributeType::EOS_AT_STRING;
        Data.Value.AsUtf8 = Value->c_str();
        EOS_LobbySearch_SetParameterOptions P{};
        P.ApiVersion = EOS_LOBBYSEARCH_SETPARAMETER_API_LATEST;
        P.Parameter = &Data;
        P.ComparisonOp = EOS_EComparisonOp::EOS_CO_EQUAL;
        const EOS_EResult ParamResult = EOS_LobbySearch_SetParameter(LobbySearchHandle, &P);
        if (ParamResult != EOS_EResult::EOS_Success)
            Report("lobby_search_param", ParamResult);
    }
    Room.Search.LobbyBusy = true; Room.Search.LobbyStatus = "Searching lobbies...";
    EOS_LobbySearch_FindOptions F{}; F.ApiVersion = EOS_LOBBYSEARCH_FIND_API_LATEST; F.LocalUserId = User;
    EOS_LobbySearch_Find(LobbySearchHandle, &F, Cookie(), LobbySearchDone); return true;
}
bool RefreshSessionSearch(const char* Filter)
{
    if (!Sessions || !User || Room.Search.SessionBusy) return false;
    if (SessionSearchHandle) { EOS_SessionSearch_Release(SessionSearchHandle); SessionSearchHandle = nullptr; }
    Room.Search.SessionFilter = Filter ? Filter : "";
    Room.Search.Sessions.clear();
    for (auto H : SessionResultHandles) if (H) EOS_SessionDetails_Release(H);
    SessionResultHandles.clear();
    EOS_Sessions_CreateSessionSearchOptions O{}; O.ApiVersion = EOS_SESSIONS_CREATESESSIONSEARCH_API_LATEST; O.MaxSearchResults = 20;
    if (EOS_Sessions_CreateSessionSearch(Sessions, &O, &SessionSearchHandle) != EOS_EResult::EOS_Success || !SessionSearchHandle)
    { Room.Search.SessionStatus = "Could not create session search."; return false; }
    Room.Search.SessionBusy = true; Room.Search.SessionStatus = "Searching sessions...";
    EOS_SessionSearch_FindOptions F{}; F.ApiVersion = EOS_SESSIONSEARCH_FIND_API_LATEST; F.LocalUserId = User;
    EOS_SessionSearch_Find(SessionSearchHandle, &F, Cookie(), SessionSearchDone); return true;
}
bool JoinLobbyResult(size_t Index)
{
    if (Index >= Room.Search.Lobbies.size()) return false;
    return JoinRoom(Room.Search.Lobbies[Index].LobbyId.c_str());
}
bool JoinSessionResult(size_t Index)
{
    if (!Sessions || !User || Index >= Room.Search.Sessions.size() || Index >= SessionResultHandles.size()) return false;
    if (SessionExists || Operation || JoinSessionPending) return false;
    auto Handle = SessionResultHandles[Index];
    if (!Handle) return false;
    EOS_Sessions_JoinSessionOptions O{}; O.ApiVersion = EOS_SESSIONS_JOINSESSION_API_LATEST;
    O.SessionName = SessionNameC(); O.SessionHandle = Handle; O.LocalUserId = User;
    O.bPresenceEnabled = Room.SessionSettings.PresenceEnabled ? EOS_TRUE : EOS_FALSE;
    Room.SessionId = Room.Search.Sessions[Index].SessionId;
    JoinSessionPending = true; Operation = true; SyncSessionFlags();
    Room.SessionStatus = "Joining match session...";
    EOS_Sessions_JoinSession(Sessions, &O, Cookie(), JoinedSession); return true;
}
bool JoinMatchSession()
{
    if (!Sessions || !User || SessionExists || Operation || JoinSessionPending || Room.SessionId.empty()) return false;
    // Find the host session by its backend session ID, then join it.
    if (SessionSearchHandle) { EOS_SessionSearch_Release(SessionSearchHandle); SessionSearchHandle = nullptr; }
    EOS_Sessions_CreateSessionSearchOptions O{}; O.ApiVersion = EOS_SESSIONS_CREATESESSIONSEARCH_API_LATEST; O.MaxSearchResults = 4;
    if (EOS_Sessions_CreateSessionSearch(Sessions, &O, &SessionSearchHandle) != EOS_EResult::EOS_Success || !SessionSearchHandle) return false;
    EOS_SessionSearch_SetSessionIdOptions S{}; S.ApiVersion = EOS_SESSIONSEARCH_SETSESSIONID_API_LATEST; S.SessionId = Room.SessionId.c_str();
    if (EOS_SessionSearch_SetSessionId(SessionSearchHandle, &S) != EOS_EResult::EOS_Success)
    { EOS_SessionSearch_Release(SessionSearchHandle); SessionSearchHandle = nullptr; return false; }
    JoinSessionTargetId = Room.SessionId;
    Room.Search.SessionBusy = true;
    Room.SessionStatus = "Finding host match session..."; SyncSessionFlags();
    EOS_SessionSearch_FindOptions F{}; F.ApiVersion = EOS_SESSIONSEARCH_FIND_API_LATEST; F.LocalUserId = User;
    EOS_SessionSearch_Find(SessionSearchHandle, &F, Cookie(), SessionSearchDone); return true;
}
bool LeaveMatchSession()
{
    if (!Sessions || !SessionExists || Operation) return false;
    if (Room.SessionOwner && Room.Phase == RoomPhase::Running) { EndRoomMatch(); return true; }
    if (Room.Phase == RoomPhase::Running && !Room.SessionOwner) SaveMatch("abandoned");
    DestroySessionAsync(); return true;
}
void TickRoomRuntime()
{
    TickHistory();
    if (!User) return;
    if (Dirty || std::chrono::steady_clock::now() - LastRefresh > std::chrono::seconds(1)) Refresh();
    if (WantLeave && !Operation && !Room.ReadyPending && !UnregisterPending)
    {
        if (Room.Phase == RoomPhase::Running && Room.Owner && SessionExists) { EndRoomMatch(); return; }
        if (Room.Phase == RoomPhase::Running) SaveMatch("abandoned");
        if (SessionExists)
        {
            SaveMatch("abandoned"); Room.Phase = RoomPhase::Leaving;
            Room.SessionStatus = "Closing match session..."; SyncSessionFlags();
            DestroySessionAsync();
            if (Operation || UnregisterPending) return;
        }
        if (UnregisterPending) return;
        if (!Room.LobbyId.empty() && !LobbyLeaveSent)
        {
            Room.Phase = RoomPhase::Leaving; Operation = LobbyLeaveSent = true;
            if (Room.Owner)
            {
                EOS_Lobby_DestroyLobbyOptions O{}; O.ApiVersion = EOS_LOBBY_DESTROYLOBBY_API_LATEST;
                O.LocalUserId = User; O.LobbyId = Room.LobbyId.c_str(); EOS_Lobby_DestroyLobby(Lobby, &O, Cookie(), DestroyedLobby);
            }
            else
            {
                EOS_Lobby_LeaveLobbyOptions O{}; O.ApiVersion = EOS_LOBBY_LEAVELOBBY_API_LATEST;
                O.LocalUserId = User; O.LobbyId = Room.LobbyId.c_str(); EOS_Lobby_LeaveLobby(Lobby, &O, Cookie(), LeftLobby);
            }
            return;
        }
    }
    // Owner keeps the registered set in sync when members leave mid-match tests.
    if (!WantLeave && SessionExists && Room.SessionOwner && !Operation && !UnregisterPending && !Registered.empty())
    {
        bool Stale = false;
        for (auto Id : Registered)
            if (std::find(RealMembers.begin(), RealMembers.end(), Id) == RealMembers.end()) { Stale = true; break; }
        if (Stale && Room.Phase != RoomPhase::Running) RequestUnregisterAll();
    }
    if (!WantLeave) PublishAttributes();
    if (!WantLeave && Room.Phase == RoomPhase::Waiting)
    {
        if (Room.AutoStart && AutoArmed && Room.Owner && !Room.ReadyPending && !AttributesDirty && EveryoneReady(Room)) StartRoomMatch();
    }
}
void DetachRoomRuntime()
{
    SaveMatch("abandoned"); ++Generation;
    if (Lobby)
    {
        if (MemberUpdate != EOS_INVALID_NOTIFICATIONID) EOS_Lobby_RemoveNotifyLobbyMemberUpdateReceived(Lobby, MemberUpdate);
        if (MemberStatus != EOS_INVALID_NOTIFICATIONID) EOS_Lobby_RemoveNotifyLobbyMemberStatusReceived(Lobby, MemberStatus);
        if (RoomConnection != EOS_INVALID_NOTIFICATIONID) EOS_Lobby_RemoveNotifyRTCRoomConnectionChanged(Lobby, RoomConnection);
    }
    MemberUpdate = MemberStatus = RoomConnection = EOS_INVALID_NOTIFICATIONID;
    ReleaseSearchHandles();
    DetachHistory(); Room = {}; Lobby = nullptr; Sessions = nullptr; Audio = nullptr; User = nullptr; Log = nullptr;
    RealMembers.clear(); Registered.clear(); VoiceRoom.clear(); JoinAfterLeave.clear(); Name = "You";
    JoinSessionTargetId.clear();
    OwnerAttributesDirty = false;
    Dirty = true; Operation = SessionExists = WantLeave = LobbyLeaveSent = false;
    ReadyDesired = AttributesDirty = MatchRecorded = false; AutoArmed = true; Dummies = 3; DummyReady.fill(false);
    LiveSettingsDirty = UnregisterPending = JoinSessionPending = false;
}
}
