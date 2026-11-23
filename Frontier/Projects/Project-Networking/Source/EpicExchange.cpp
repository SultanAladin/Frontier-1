//============================================================================================================================================
//                                                              EPICEXCHANGE.CPP
//============================================================================================================================================
// 📦 Authenticates through Epic Account Portal and exchanges the identity token for a product user.

#include "EpicExchange.h"
#include "RoomRuntime.h"
#include "TransportRouter.h"
#include "LocalConfiguration.h"
#if defined(_WIN32)
#include <Windows/eos_Windows.h>
#endif
#include "AuthPolicy.h"
#include "PlatformDiagnostics.h"
#include <eos_logging.h>
#include <atomic>

#include <eos_sdk.h>
#include <eos_auth.h>
#include <eos_connect.h>
#include <eos_version.h>
#include <eos_friends.h>
#include <eos_ui.h>
#include <eos_userinfo.h>
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <cstdio>

namespace Networking
{
namespace
{
EOS_HPlatform Platform = nullptr;
EOS_HAuth Auth = nullptr;
EOS_HConnect Connect = nullptr;
EOS_Auth_IdToken* IdentityToken = nullptr;
LoginSequence Login;
DiagnosticReception Reception = nullptr;
bool OwnsInitialization = false;
bool FinalShutdown = false;
bool Releasing = false;
bool SocialEnabled = false;
bool OverlayExclusive = false;
bool OverlayPending = false;
EOS_NotificationId OverlayNotification = EOS_INVALID_NOTIFICATIONID;
EOS_HFriends Friends = nullptr;
EOS_HUI SocialOverlay = nullptr;
EOS_HUserInfo UserInformation = nullptr;
EOS_EpicAccountId LocalAccount = nullptr;
EOS_ProductUserId LocalProductUser = nullptr;
EOS_EpicAccountId ProfileAccount = nullptr;
EOS_ContinuanceToken PendingCreation = nullptr;
AccountProfile Profile;
bool CloudEnabled = false;
constexpr int FriendCapacity = 128;
struct FriendReading
{
    EOS_EpicAccountId Account = nullptr;
    char Name[256]{};
    const char* Relationship = "Friend";
};
FriendReading FriendReadings[FriendCapacity]{};
int FriendCount = 0;
int NamesPending = 0;
bool QueryPending = false;
char FriendsReading[256] = "Sign in to load friends.";
char OverlayReading[256] = "Overlay readiness is checked when a platform is created.";
bool AllowCreation = false;
bool DeveloperMode = false;
bool WantAutoLogin = false;
bool UsingPersistent = false;
const char* DevCredentialName = "";
std::chrono::steady_clock::time_point Started;

std::atomic<bool> CapturePlatformDiagnostics{false};
std::atomic<unsigned> PlatformDiagnosticBits{0};
void EOS_CALL ReceiveSdkLog(const EOS_LogMessage* Message)
{
    if (CapturePlatformDiagnostics.load() && Message &&
        Message->Level <= EOS_ELogLevel::EOS_LOG_Warning)
        PlatformDiagnosticBits.fetch_or(ClassifyPlatformDiagnostic(Message->Message));
}

void Emit(const char* Text) noexcept
{
    if (Reception)
        Reception(Text);
}

void Report(const char* Operation, EOS_EResult Result) noexcept
{
    char Text[256]{};
    std::snprintf(Text, sizeof(Text), "%s result=%s", Operation, EOS_EResult_ToString(Result));
    Emit(Text);
}

void Refuse(const char* Operation, EOS_EResult Result) noexcept
{
    Report(Operation, Result);
    Login.Refuse();
}

bool CreatePlatform(const char* Secret, const char* ClientId, const char* StorageKey = nullptr) noexcept
{
    EOS_Platform_Options Options{};
    Options.ApiVersion = EOS_PLATFORM_OPTIONS_API_LATEST;
    Options.ProductId = "fbf3442817da41bda43997bd3d87e875";
    Options.SandboxId = "p-ewz29ujngay2pm7t5twt8drcvbr8ru";
    Options.DeploymentId = "bb5140b152114e8b92021b0afd8c9df1";
    Options.ClientCredentials.ClientId = ClientId;
    Options.ClientCredentials.ClientSecret = Secret;
    Options.bIsServer = EOS_FALSE;
    CloudEnabled = StorageKey && ValidStorageKey(StorageKey);
    Options.EncryptionKey = CloudEnabled ? StorageKey : nullptr;
    std::error_code CacheError;
    const auto CacheRoot = UserCacheRoot();
    std::filesystem::create_directories(CacheRoot / "sdk", CacheError);
    const auto Cache = PathUtf8(CacheRoot / "sdk");
    if (!CacheError) Options.CacheDirectory = Cache.c_str();
    else CloudEnabled = false;
    EOS_Platform_RTCOptions RtcOptions{};
    RtcOptions.ApiVersion = EOS_PLATFORM_RTCOPTIONS_API_LATEST;
    Options.RTCOptions = &RtcOptions;
#if defined(_WIN32)
    const auto XAudio = PathUtf8(ExecutableDirectory() / "xaudio2_9redist.dll");
    EOS_Windows_RTCOptions WindowsRtc{};
    WindowsRtc.ApiVersion = EOS_WINDOWS_RTCOPTIONS_API_LATEST;
    WindowsRtc.XAudio29DllPath = XAudio.c_str();
    RtcOptions.PlatformSpecificOptions = &WindowsRtc;
#endif
#if defined(_WIN32)
    Options.Flags = EOS_PF_WINDOWS_ENABLE_OVERLAY_OPENGL;
#else
    Options.Flags = 0;
#endif
    PlatformDiagnosticBits.store(0);
    CapturePlatformDiagnostics.store(true);
    Platform = EOS_Platform_Create(&Options);
    CapturePlatformDiagnostics.store(false);
    if (Platform)
    {
        Emit("platform=created; player authentication has not yet completed");
        return true;
    }
    Emit("platform=refused; EOS_Platform_Create returned null. Account Portal was NOT requested.");
    const unsigned Bits = PlatformDiagnosticBits.load();
    for (unsigned Bit = 1; Bit <= (1u << 8); Bit <<= 1)
        if (Bits & Bit) Emit(DescribePlatformDiagnostic(Bit));
    if (!Bits) Emit("platform_error=no_sdk_diagnostic; SDK supplied no classified warning/error during creation.");
    Emit("Use Copy log to share these redacted diagnostics. SDK ready only means the DLL initialized.");
    return false;
}

bool InitializeOnce(DiagnosticReception ActiveReception) noexcept
{
    if (FinalShutdown)
    {
        if (ActiveReception)
            ActiveReception("sdk=retired; restart the application before using EOS again");
        return false;
    }
    if (OwnsInitialization)
        return true;
    EOS_InitializeOptions Options{};
    Options.ApiVersion = EOS_INITIALIZE_API_LATEST;
    Options.ProductName = "Charge";
    Options.ProductVersion = "Networking-Dev-2";
    const EOS_EResult Result = EOS_Initialize(&Options);
    if (ActiveReception)
        ActiveReception(EOS_EResult_ToString(Result));
    if (Result != EOS_EResult::EOS_Success)
        return false;
    OwnsInitialization = true;
    const auto Logging = EOS_Logging_SetCallback(ReceiveSdkLog);
    if (Logging != EOS_EResult::EOS_Success && ActiveReception)
        ActiveReception("SDK platform diagnostics unavailable; raw SDK logging remains disabled.");
    if (ActiveReception)
        ActiveReception("sdk_initialized_once=1");
    return true;
}

void EOS_CALL ReceiveOverlayDisplay(const EOS_UI_OnDisplaySettingsUpdatedCallbackInfo* Completion)
{
    if (!Releasing)
        OverlayExclusive = Completion->bIsExclusiveInput == EOS_TRUE;
}

bool RefreshOverlayReadiness() noexcept
{
    if (!Platform)
        return false;
#if defined(_WIN32)
    EOS_Platform_GetDesktopCrossplayStatusOptions Options{};
    Options.ApiVersion = EOS_PLATFORM_GETDESKTOPCROSSPLAYSTATUS_API_LATEST;
    EOS_Platform_DesktopCrossplayStatusInfo Reading{};
    const auto Result = EOS_Platform_GetDesktopCrossplayStatus(Platform, &Options, &Reading);
    if (Result != EOS_EResult::EOS_Success)
    {
        std::snprintf(OverlayReading, sizeof(OverlayReading), "Overlay readiness: %s", EOS_EResult_ToString(Result));
        return false;
    }
    const char* Explanation = "Unknown overlay readiness status";
    switch (Reading.Status)
    {
    case EOS_EDesktopCrossplayStatus::EOS_DCS_OK: Explanation = "Epic overlay ready. Social overlay shortcut: Shift+F3."; break;
    case EOS_EDesktopCrossplayStatus::EOS_DCS_ApplicationNotBootstrapped: Explanation = "Launch through Epic's EOS Bootstrapper, not directly through the EXE."; break;
    case EOS_EDesktopCrossplayStatus::EOS_DCS_ServiceNotInstalled: Explanation = "Install Epic's EOS redistributable (separate from the shipped SDK DLL)."; break;
    case EOS_EDesktopCrossplayStatus::EOS_DCS_ServiceStartFailed: Explanation = "EOS redistributable service could not start."; break;
    case EOS_EDesktopCrossplayStatus::EOS_DCS_ServiceNotRunning: Explanation = "EOS redistributable service is no longer running."; break;
    case EOS_EDesktopCrossplayStatus::EOS_DCS_OverlayDisabled: Explanation = "Overlay disabled by SDK configuration."; break;
    case EOS_EDesktopCrossplayStatus::EOS_DCS_OverlayNotInstalled: Explanation = "Epic overlay is not installed; repair the EOS redistributable."; break;
    case EOS_EDesktopCrossplayStatus::EOS_DCS_OverlayTrustCheckFailed: Explanation = "Overlay signature check failed; check Windows certificates and system clock."; break;
    case EOS_EDesktopCrossplayStatus::EOS_DCS_OverlayLoadFailed: Explanation = "Epic overlay failed to load."; break;
    }
    std::snprintf(OverlayReading, sizeof(OverlayReading), "%s [status=%d service=%d]", Explanation,
        static_cast<int>(Reading.Status), Reading.ServiceInitResult);
    return Reading.Status == EOS_EDesktopCrossplayStatus::EOS_DCS_OK;
#else
    std::snprintf(OverlayReading, sizeof(OverlayReading), "This diagnostic's Epic overlay route targets Windows.");
    return false;
#endif
}

void EOS_CALL ReceiveFriendName(const EOS_UserInfo_QueryUserInfoCallbackInfo* Completion)
{
    if (Releasing || !UserInformation || EOS_EResult_IsOperationComplete(Completion->ResultCode) != EOS_TRUE)
        return;
    auto* Reading = static_cast<FriendReading*>(Completion->ClientData);
    if (!Reading || Reading->Account != Completion->TargetUserId)
        return;
    if (Completion->ResultCode == EOS_EResult::EOS_Success)
    {
        EOS_UserInfo_CopyUserInfoOptions Options{};
        Options.ApiVersion = EOS_USERINFO_COPYUSERINFO_API_LATEST;
        Options.LocalUserId = LocalAccount;
        Options.TargetUserId = Reading->Account;
        EOS_UserInfo* Information = nullptr;
        const auto Result = EOS_UserInfo_CopyUserInfo(UserInformation, &Options, &Information);
        if (Result == EOS_EResult::EOS_Success && Information)
        {
            const char* Name = Information->DisplayNameSanitized;
            if (Name && *Name)
                std::snprintf(Reading->Name, sizeof(Reading->Name), "%s", Name);
            EOS_UserInfo_Release(Information);
        }
    }
    if (NamesPending > 0)
        --NamesPending;
}

void EOS_CALL ReceiveFriends(const EOS_Friends_QueryFriendsCallbackInfo* Completion)
{
    if (Releasing || !Friends || EOS_EResult_IsOperationComplete(Completion->ResultCode) != EOS_TRUE)
        return;
    QueryPending = false;
    if (Completion->ResultCode != EOS_EResult::EOS_Success)
    {
        Report("friends_query", Completion->ResultCode);
        std::snprintf(FriendsReading, sizeof(FriendsReading), "Friends query refused: %s", EOS_EResult_ToString(Completion->ResultCode));
        return;
    }
    EOS_Friends_GetFriendsCountOptions CountOptions{};
    CountOptions.ApiVersion = EOS_FRIENDS_GETFRIENDSCOUNT_API_LATEST;
    CountOptions.LocalUserId = LocalAccount;
    const int Available = EOS_Friends_GetFriendsCount(Friends, &CountOptions);
    FriendCount = std::clamp(Available, 0, FriendCapacity);
    for (int Index = 0; Index < FriendCount; ++Index)
    {
        auto& Reading = FriendReadings[Index];
        Reading = {};
        std::snprintf(Reading.Name, sizeof(Reading.Name), "Friend %d (name unavailable)", Index + 1);
        EOS_Friends_GetFriendAtIndexOptions Options{};
        Options.ApiVersion = EOS_FRIENDS_GETFRIENDATINDEX_API_LATEST;
        Options.LocalUserId = LocalAccount;
        Options.Index = Index;
        Reading.Account = EOS_Friends_GetFriendAtIndex(Friends, &Options);
        if (EOS_EpicAccountId_IsValid(Reading.Account) != EOS_TRUE)
            continue;
        EOS_Friends_GetStatusOptions Status{};
        Status.ApiVersion = EOS_FRIENDS_GETSTATUS_API_LATEST;
        Status.LocalUserId = LocalAccount;
        Status.TargetUserId = Reading.Account;
        switch (EOS_Friends_GetStatus(Friends, &Status))
        {
        case EOS_EFriendsStatus::EOS_FS_Friends: Reading.Relationship = "Friend"; break;
        case EOS_EFriendsStatus::EOS_FS_InviteSent: Reading.Relationship = "Invite sent"; break;
        case EOS_EFriendsStatus::EOS_FS_InviteReceived: Reading.Relationship = "Invite received"; break;
        default: Reading.Relationship = "Not friends"; break;
        }
        EOS_UserInfo_QueryUserInfoOptions NameOptions{};
        NameOptions.ApiVersion = EOS_USERINFO_QUERYUSERINFO_API_LATEST;
        NameOptions.LocalUserId = LocalAccount;
        NameOptions.TargetUserId = Reading.Account;
        ++NamesPending;
        EOS_UserInfo_QueryUserInfo(UserInformation, &NameOptions, &Reading, ReceiveFriendName);
    }
    std::snprintf(FriendsReading, sizeof(FriendsReading), "Showing %d of %d friends visible to this application.", FriendCount, Available);
    Emit("friends_query=success; names and account IDs are omitted from logs");
}

void EOS_CALL ReceiveOverlayShow(const EOS_UI_ShowFriendsCallbackInfo* Completion)
{
    if (!Releasing && EOS_EResult_IsOperationComplete(Completion->ResultCode) == EOS_TRUE)
    {
        OverlayPending = false;
        Report("social_overlay_show", Completion->ResultCode);
    }
}

void EOS_CALL ReceiveOverlayHide(const EOS_UI_HideFriendsCallbackInfo* Completion)
{
    if (!Releasing && EOS_EResult_IsOperationComplete(Completion->ResultCode) == EOS_TRUE)
    {
        OverlayPending = false;
        Report("social_overlay_hide", Completion->ResultCode);
    }
}

void ReleaseToken() noexcept
{
    if (IdentityToken)
        EOS_Auth_IdToken_Release(IdentityToken);
    IdentityToken = nullptr;
}

void EOS_CALL ReceiveOwnProfile(const EOS_UserInfo_QueryUserInfoCallbackInfo* Completion)
{
    if (Releasing || !UserInformation || Login.Progress != LoginProgress::Connected ||
        Completion->LocalUserId != LocalAccount || Completion->TargetUserId != ProfileAccount ||
        EOS_EResult_IsOperationComplete(Completion->ResultCode) != EOS_TRUE)
        return;
    Profile.Pending = false;
    if (Completion->ResultCode != EOS_EResult::EOS_Success)
    {
        Report("profile_query", Completion->ResultCode);
        return; // Profile lookup failure must never restart Auth or undo Connect.
    }
    EOS_UserInfo_CopyUserInfoOptions Options{};
    Options.ApiVersion = EOS_USERINFO_COPYUSERINFO_API_LATEST;
    Options.LocalUserId = LocalAccount;
    Options.TargetUserId = ProfileAccount;
    EOS_UserInfo* Information = nullptr;
    const auto Result = EOS_UserInfo_CopyUserInfo(UserInformation, &Options, &Information);
    if (Result == EOS_EResult::EOS_Success && Information)
    {
        const char* Name = Information->DisplayNameSanitized ? Information->DisplayNameSanitized : Information->DisplayName;
        std::snprintf(Profile.DisplayName, sizeof(Profile.DisplayName), "%s", Name ? Name : "");
        std::snprintf(Profile.Country, sizeof(Profile.Country), "%s", Information->Country ? Information->Country : "");
        std::snprintf(Profile.Language, sizeof(Profile.Language), "%s", Information->PreferredLanguage ? Information->PreferredLanguage : "");
        Profile.Available = true;
        SetRoomDisplayName(Profile.DisplayName);
        Emit("profile=loaded; personal details are displayed only in the account card, not saved to logs");
    }
    else Report("profile_copy", Result);
    if (Information) EOS_UserInfo_Release(Information);
}

void AcceptProductUser(EOS_ProductUserId ProductUser) noexcept
{
    const bool Valid = EOS_ProductUserId_IsValid(ProductUser) == EOS_TRUE;
    LocalProductUser = Valid ? ProductUser : nullptr;
    Login.AcceptConnect(Valid);
    Emit(Valid ? "connect=success product_user_id_valid=1" : "connect=refused invalid_product_user_id");
    if (Login.Progress == LoginProgress::Connected)
    {
        Emit("LOGIN_VERIFIED auth=success connect=success");
        BindRoomRuntime(Platform, LocalProductUser, Reception, UserCacheRoot(), CloudEnabled);
        QueryEpicProfile();
        StartMultiplayerTransport(Reception);
        if (!SocialEnabled)
            std::snprintf(FriendsReading, sizeof(FriendsReading), "Automatic friends loading is disabled. Enable it in Setup before signing in.");
    }
}

void EOS_CALL ReceiveCreation(const EOS_Connect_CreateUserCallbackInfo* Completion)
{
    if (Releasing || Login.Finished() || EOS_EResult_IsOperationComplete(Completion->ResultCode) != EOS_TRUE)
        return;
    if (Completion->ResultCode != EOS_EResult::EOS_Success)
    {
        Refuse("connect_create_user", Completion->ResultCode);
        return;
    }
    AcceptProductUser(Completion->LocalUserId);
}

void EOS_CALL ReceiveConnect(const EOS_Connect_LoginCallbackInfo* Completion)
{
    if (Releasing || Login.Finished() || EOS_EResult_IsOperationComplete(Completion->ResultCode) != EOS_TRUE)
        return;
    ReleaseToken();
    if (Completion->ResultCode == EOS_EResult::EOS_InvalidUser && Completion->ContinuanceToken)
    {
        if (!AllowCreation)
        {
            PendingCreation = Completion->ContinuanceToken;
            Login.RequestCreationConsent();
            Started = std::chrono::steady_clock::now();
            Emit("connect=account_creation_required; Epic sign-in succeeded. Explicitly create a NEW Dev product user to continue, or cancel. No second login is required.");
            return;
        }
        EOS_Connect_CreateUserOptions Options{};
        Options.ApiVersion = EOS_CONNECT_CREATEUSER_API_LATEST;
        Options.ContinuanceToken = Completion->ContinuanceToken;
        EOS_Connect_CreateUser(Connect, &Options, nullptr, ReceiveCreation);
        return;
    }
    if (Completion->ResultCode != EOS_EResult::EOS_Success)
    {
        Refuse("connect_login", Completion->ResultCode);
        return;
    }
    AcceptProductUser(Completion->LocalUserId);
}

void EOS_CALL ReceiveAuth(const EOS_Auth_LoginCallbackInfo* Completion);
void IssueAuthLogin() noexcept
{
    EOS_Auth_Credentials Credentials{};
    Credentials.ApiVersion = EOS_AUTH_CREDENTIALS_API_LATEST;
    Credentials.Type = EOS_ELoginCredentialType::EOS_LCT_AccountPortal;
    const char* Method = "account_portal";
    if (UsingPersistent)
    {
        // Desktop PersistentAuth: Id/Token stay NULL; the SDK reads the token
        // the last AccountPortal login stored on this device.
        Credentials.Type = EOS_ELoginCredentialType::EOS_LCT_PersistentAuth;
        Method = "persistent";
    }
    else if (DeveloperMode)
    {
        Credentials.Type = EOS_ELoginCredentialType::EOS_LCT_Developer;
        Credentials.Id = "localhost:6547";
        Credentials.Token = DevCredentialName;
        Method = "developer";
    }
    EOS_Auth_LoginOptions LoginOptions{};
    LoginOptions.ApiVersion = EOS_AUTH_LOGIN_API_LATEST;
    LoginOptions.Credentials = &Credentials;
    LoginOptions.ScopeFlags = ChargeAuthScopes();
    Started = std::chrono::steady_clock::now();
    char Line[128]{};
    std::snprintf(Line, sizeof(Line), "auth=waiting method=%s timeout_seconds=180", Method);
    Emit(Line);
    EOS_Auth_Login(Auth, &LoginOptions, nullptr, ReceiveAuth);
}

void EOS_CALL ReceiveAuth(const EOS_Auth_LoginCallbackInfo* Completion)
{
    if (Releasing || Login.Finished() || EOS_EResult_IsOperationComplete(Completion->ResultCode) != EOS_TRUE)
        return;
    if (Completion->ResultCode != EOS_EResult::EOS_Success && UsingPersistent)
    {
        // Saved-token login failed (first run, revoked, expired): fall back to
        // the Epic Account Portal once instead of refusing the whole login.
        UsingPersistent = false;
        Report("persistent_auth", Completion->ResultCode);
#if defined(_WIN32)
        if (!RefreshOverlayReadiness())
        {
            Emit(OverlayReading);
            Emit("Account Portal requires Epic's Bootstrapper and EOS redistributable. The shipped SDK DLL alone is not enough.");
            Login.Refuse();
            return;
        }
#endif
        Emit("persistent_auth=failed; falling back to Epic Account Portal");
        IssueAuthLogin();
        return;
    }
    if (Completion->ResultCode != EOS_EResult::EOS_Success)
    {
        if (Completion->ResultCode == EOS_EResult::EOS_Canceled)
            Emit("Epic sign-in was cancelled or interrupted. No automatic login retry was started.");
        Refuse("auth_login", Completion->ResultCode);
        return;
    }
    Login.AcceptAuth(EOS_EpicAccountId_IsValid(Completion->LocalUserId) == EOS_TRUE &&
                     EOS_EpicAccountId_IsValid(Completion->SelectedAccountId) == EOS_TRUE);
    if (Login.Finished())
    {
        Emit("auth=refused invalid_epic_account_id");
        return;
    }
    LocalAccount = Completion->LocalUserId;
    ProfileAccount = Completion->SelectedAccountId;
    Emit("auth=success epic_account_id_valid=1");
    Emit(UsingPersistent ? "auth_method=persistent_token; the browser was skipped" :
         (DeveloperMode ? "auth_method=developer" :
                          "auth_method=account_portal; a persistent token was stored for auto-login"));
    EOS_Auth_CopyIdTokenOptions Copy{};
    Copy.ApiVersion = EOS_AUTH_COPYIDTOKEN_API_LATEST;
    Copy.AccountId = Completion->SelectedAccountId;
    const EOS_EResult Result = EOS_Auth_CopyIdToken(Auth, &Copy, &IdentityToken);
    if (Result != EOS_EResult::EOS_Success)
    {
        Refuse("auth_copy_id_token", Result);
        return;
    }
    EOS_Connect_Credentials Credentials{};
    Credentials.ApiVersion = EOS_CONNECT_CREDENTIALS_API_LATEST;
    Credentials.Type = EOS_EExternalCredentialType::EOS_ECT_EPIC_ID_TOKEN;
    if (!IdentityToken || !IdentityToken->JsonWebToken || !*IdentityToken->JsonWebToken)
    {
        Emit("auth=refused missing_identity_token");
        Login.Refuse();
        ReleaseToken();
        return;
    }
    Credentials.Token = IdentityToken->JsonWebToken;
    EOS_Connect_LoginOptions Options{};
    Options.ApiVersion = EOS_CONNECT_LOGIN_API_LATEST;
    Options.Credentials = &Credentials;
    EOS_Connect_Login(Connect, &Options, nullptr, ReceiveConnect);
}
}

bool VerifyEpicRuntime(DiagnosticReception ActiveReception) noexcept
{
    if (!ActiveReception || !InitializeOnce(ActiveReception))
        return false;
    ActiveReception("scope=sdk_runtime_only authentication=NOT_ATTEMPTED");
    ActiveReception(EOS_GetVersion());
    ActiveReception("sdk=ready; kept initialized for login and retries");
    return true;
}

const char* ValidateEpicCredentials(const char* Secret, const char* ClientId) noexcept
{
    if (!Secret || !*Secret) return "Client secret is empty. Enter the rotated application secret in Setup.";
    if (!ClientId || !*ClientId) return "Client ID is empty. Check Setup > Advanced.";
    if (std::strlen(Secret) > EOS_PLATFORM_CLIENTCREDENTIALS_CLIENTSECRET_MAX_LENGTH)
        return "Client secret is too long (EOS maximum: 64 characters). Use Paste secret to REPLACE the field, not append to it.";
    if (std::strlen(ClientId) > EOS_PLATFORM_CLIENTCREDENTIALS_CLIENTID_MAX_LENGTH)
        return "Client ID is too long (EOS maximum: 64 characters). Replace the field in Setup > Advanced.";
    for (const unsigned char* C = reinterpret_cast<const unsigned char*>(Secret); *C; ++C)
        if (*C <= 32 || *C >= 127)
            return "Client secret contains whitespace or non-ASCII text. Copy only the secret, not its label or surrounding text.";
    for (const unsigned char* C = reinterpret_cast<const unsigned char*>(ClientId); *C; ++C)
        if (*C <= 32 || *C >= 127)
            return "Client ID contains whitespace or non-ASCII text. Copy only the client ID.";
    return nullptr;
}

bool VerifyEpicPlatform(DiagnosticReception ActiveReception) noexcept
{
    if (Platform || FinalShutdown || !InitializeOnce(ActiveReception)) return false;
    Reception = ActiveReception;
    Emit("scope=platform_creation_only synthetic_credential=1 authentication=NOT_ATTEMPTED");
    const bool Ready = CreatePlatform("SYNTHETIC_TEST_CREDENTIAL_NOT_A_REAL_SECRET", "xyza7891AKjtZj8wTzcmI5F3oc1zLU4s");
    RetireEpic();
    return Ready;
}

bool ConstructEpic(DiagnosticReception ActiveReception) noexcept
{
    const char* Consent = std::getenv("EOS_ALLOW_CREATE_USER");
    const char* Auto = std::getenv("EOS_AUTO_LOGIN");
    const LoginSpecification Specification{
        std::getenv("EOS_CLIENT_SECRET"), std::getenv("EOS_CLIENT_ID"),
        std::getenv("EOS_LOGIN_METHOD"), std::getenv("EOS_DEVELOPER_CREDENTIAL"),
        Consent && std::strcmp(Consent, "1") == 0, true, nullptr,
        Auto && std::strcmp(Auto, "1") == 0};
    return ConstructEpic(Specification, ActiveReception);
}

bool ConstructEpic(const LoginSpecification& Specification, DiagnosticReception ActiveReception) noexcept
{
    if (Platform || FinalShutdown)
        return false;
    Reception = ActiveReception;
    Login = {};
    const char* Secret = Specification.Secret;
    if (!Secret || !*Secret)
    {
        Emit("configuration=refused missing_EOS_CLIENT_SECRET; use a rotated credential locally");
        Login.Refuse();
        return false;
    }
    const char* ClientId = Specification.ClientId;
    if (!ClientId || !*ClientId)
        ClientId = "xyza7891AKjtZj8wTzcmI5F3oc1zLU4s";
    if (const char* Error = ValidateEpicCredentials(Secret, ClientId))
    {
        Emit("configuration=refused; credentials were not submitted to EOS");
        Emit(Error);
        Login.Refuse();
        return false;
    }
    AllowCreation = Specification.AllowCreation;
    if (!InitializeOnce(ActiveReception))
    {
        Login.Refuse();
        return false;
    }
    SocialEnabled = Specification.EnableSocial;
    if (!CreatePlatform(Secret, ClientId, Specification.StorageKey))
    {
        Login.Refuse();
        RetireEpic();
        return false;
    }
    Auth = EOS_Platform_GetAuthInterface(Platform);
    Connect = EOS_Platform_GetConnectInterface(Platform);
    Friends = EOS_Platform_GetFriendsInterface(Platform);
    SocialOverlay = EOS_Platform_GetUIInterface(Platform);
    UserInformation = EOS_Platform_GetUserInfoInterface(Platform);
    RefreshOverlayReadiness();
    if (SocialOverlay)
    {
        EOS_UI_AddNotifyDisplaySettingsUpdatedOptions Display{};
        Display.ApiVersion = EOS_UI_ADDNOTIFYDISPLAYSETTINGSUPDATED_API_LATEST;
        OverlayNotification = EOS_UI_AddNotifyDisplaySettingsUpdated(SocialOverlay, &Display, nullptr, ReceiveOverlayDisplay);
    }
    if (!Auth || !Connect)
    {
        Emit("interfaces=refused");
        Login.Refuse();
        RetireEpic();
        return false;
    }
    const char* Method = Specification.Method;
    DeveloperMode = Method && std::strcmp(Method, "developer") == 0;
    if (Method && *Method && !DeveloperMode && std::strcmp(Method, "accountportal") != 0)
    {
        Emit("configuration=refused unknown_login_method");
        Login.Refuse();
        RetireEpic();
        return false;
    }
    DevCredentialName = Specification.DeveloperCredential ? Specification.DeveloperCredential : "";
    if (DeveloperMode && !*DevCredentialName)
    {
        Emit("configuration=refused missing_developer_credential_name");
        Login.Refuse();
        RetireEpic();
        return false;
    }
    WantAutoLogin = Specification.AutoLogin;
    UsingPersistent = WantAutoLogin && !DeveloperMode;
#if defined(_WIN32)
    // The saved-token attempt needs no overlay; the portal fallback checks it.
    if (!DeveloperMode && !UsingPersistent && !RefreshOverlayReadiness())
    {
        Emit(OverlayReading);
        Emit("Account Portal requires Epic's Bootstrapper and EOS redistributable. The shipped SDK DLL alone is not enough.");
        Login.Refuse();
        RetireEpic();
        return false;
    }
#endif
    Emit("auth_scopes=basic_profile,friends_list,presence,country; matches Charge required permissions");
    IssueAuthLogin();
    return true;
}

void EOS_CALL ReceiveDeletePersistent(const EOS_Auth_DeletePersistentAuthCallbackInfo* Completion)
{
    if (Releasing || !Completion ||
        EOS_EResult_IsOperationComplete(Completion->ResultCode) != EOS_TRUE)
        return;
    Report("persistent_auth_delete", Completion->ResultCode);
}

bool RevokeEpicPersistentAuth() noexcept
{
    if (!Auth || Releasing || !Platform)
        return false;
    EOS_Auth_DeletePersistentAuthOptions Options{};
    Options.ApiVersion = EOS_AUTH_DELETEPERSISTENTAUTH_API_LATEST;
    Options.RefreshToken = nullptr; // Desktop/mobile: the SDK picks the stored token.
    EOS_Auth_DeletePersistentAuth(Auth, &Options, nullptr, ReceiveDeletePersistent);
    Emit("persistent_auth_delete=requested; the saved Epic token on this PC will be revoked");
    return true;
}

void AdvanceEpic() noexcept
{
    if (!Platform)
        return;
    EOS_Platform_Tick(Platform);
    TickRoomRuntime();
    TickMultiplayerTransport();
    if (Login.Progress == LoginProgress::Connected &&
        (EOS_Auth_GetLoginStatus(Auth, LocalAccount) != EOS_ELoginStatus::EOS_LS_LoggedIn ||
         EOS_Connect_GetLoginStatus(Connect, LocalProductUser) != EOS_ELoginStatus::EOS_LS_LoggedIn))
    {
        Emit("session=not_logged_in; sign in again");
        Login.Progress = LoginProgress::Refused;
    }
    if (!Login.Finished() && std::chrono::steady_clock::now() - Started > std::chrono::seconds(180))
    {
        Emit("login=refused timeout");
        Login.Refuse();
    }
}

void RetireEpic() noexcept
{
    Releasing = true;
    DetachRoomRuntime();
    StopMultiplayerTransport();
    Login.Progress = LoginProgress::Refused;
    if (SocialOverlay && OverlayNotification != EOS_INVALID_NOTIFICATIONID)
        EOS_UI_RemoveNotifyDisplaySettingsUpdated(SocialOverlay, OverlayNotification);
    OverlayNotification = EOS_INVALID_NOTIFICATIONID;
    OverlayExclusive = false;
    OverlayPending = false;
    if (Platform)
        EOS_Platform_Release(Platform);
    Platform = nullptr;
    Auth = nullptr;
    Connect = nullptr;
    Friends = nullptr;
    SocialOverlay = nullptr;
    UserInformation = nullptr;
    LocalAccount = nullptr;
    LocalProductUser = nullptr;
    ProfileAccount = nullptr;
    PendingCreation = nullptr;
    Profile = {};
    DeveloperMode = false;
    WantAutoLogin = false;
    UsingPersistent = false;
    DevCredentialName = "";
    ReleaseToken();
    FriendCount = 0;
    NamesPending = 0;
    QueryPending = false;
    for (auto& Reading : FriendReadings)
        Reading = {};
    std::snprintf(FriendsReading, sizeof(FriendsReading), "Sign in to load friends.");
    Reception = nullptr;
    Releasing = false;
}

bool ShutdownEpic(DiagnosticReception ActiveReception) noexcept
{
    if (FinalShutdown)
        return true;
    RetireEpic();
    FinalShutdown = true;
    if (!OwnsInitialization)
        return true;
    EOS_Logging_SetCallback(nullptr);
    const EOS_EResult Result = EOS_Shutdown();
    OwnsInitialization = false;
    if (ActiveReception)
    {
        ActiveReception(EOS_EResult_ToString(Result));
        ActiveReception("sdk_shutdown_once=1");
    }
    return Result == EOS_EResult::EOS_Success;
}

const AccountProfile& InspectEpicProfile() noexcept { return Profile; }

bool QueryEpicProfile() noexcept
{
    if (!UserInformation || Login.Progress != LoginProgress::Connected || !ProfileAccount || Profile.Pending)
        return false;
    Profile.Pending = true;
    EOS_UserInfo_QueryUserInfoOptions Options{};
    Options.ApiVersion = EOS_USERINFO_QUERYUSERINFO_API_LATEST;
    Options.LocalUserId = LocalAccount;
    Options.TargetUserId = ProfileAccount;
    EOS_UserInfo_QueryUserInfo(UserInformation, &Options, nullptr, ReceiveOwnProfile);
    return true;
}

bool ApproveEpicUserCreation() noexcept
{
    if (!Connect || !PendingCreation || !Login.ApproveCreation()) return false;
    EOS_Connect_CreateUserOptions Options{};
    Options.ApiVersion = EOS_CONNECT_CREATEUSER_API_LATEST;
    Options.ContinuanceToken = PendingCreation;
    PendingCreation = nullptr;
    Started = std::chrono::steady_clock::now();
    Emit("connect_create_user=waiting; explicit new Dev product-user consent received");
    EOS_Connect_CreateUser(Connect, &Options, nullptr, ReceiveCreation);
    return true;
}

bool QueryEpicFriends() noexcept
{
    if (!Friends || !UserInformation || !SocialEnabled || Login.Progress != LoginProgress::Connected)
    {
        Emit("friends=unavailable; complete login with Friends + Presence permissions enabled");
        return false;
    }
    if (QueryPending || NamesPending > 0)
        return false;
    QueryPending = true;
    FriendCount = 0;
    std::snprintf(FriendsReading, sizeof(FriendsReading), "Querying Epic friends...");
    EOS_Friends_QueryFriendsOptions Options{};
    Options.ApiVersion = EOS_FRIENDS_QUERYFRIENDS_API_LATEST;
    Options.LocalUserId = LocalAccount;
    EOS_Friends_QueryFriends(Friends, &Options, nullptr, ReceiveFriends);
    return true;
}

bool ShowEpicFriends() noexcept
{
    if (!SocialOverlay || Login.Progress != LoginProgress::Connected || OverlayPending)
        return false;
    if (!RefreshOverlayReadiness())
    {
        Emit(OverlayReading);
        return false;
    }
    EOS_UI_ShowFriendsOptions Options{};
    Options.ApiVersion = EOS_UI_SHOWFRIENDS_API_LATEST;
    Options.LocalUserId = LocalAccount;
    OverlayPending = true;
    EOS_UI_ShowFriends(SocialOverlay, &Options, nullptr, ReceiveOverlayShow);
    return true;
}

bool HideEpicFriends() noexcept
{
    if (!SocialOverlay || Login.Progress != LoginProgress::Connected || OverlayPending)
        return false;
    EOS_UI_HideFriendsOptions Options{};
    Options.ApiVersion = EOS_UI_HIDEFRIENDS_API_LATEST;
    Options.LocalUserId = LocalAccount;
    OverlayPending = true;
    EOS_UI_HideFriends(SocialOverlay, &Options, nullptr, ReceiveOverlayHide);
    return true;
}

bool EpicOverlayOwnsInput() noexcept { return OverlayExclusive; }
int InspectFriendCount() noexcept { return FriendCount; }
const char* InspectFriendName(int Index) noexcept
{
    return Index >= 0 && Index < FriendCount ? FriendReadings[Index].Name : "";
}
const char* InspectFriendship(int Index) noexcept
{
    return Index >= 0 && Index < FriendCount ? FriendReadings[Index].Relationship : "";
}
const char* InspectFriendsReading() noexcept { return FriendsReading; }
bool FriendsQueryPending() noexcept { return QueryPending || NamesPending > 0; }
const char* InspectOverlayReading() noexcept { return OverlayReading; }

LoginProgress InspectLogin() noexcept
{
    return Login.Progress;
}

bool CopyEpicIdentityToken(char* Out, std::size_t Capacity) noexcept
{
    if (!Out || Capacity == 0 || !Auth || !LocalAccount || Login.Progress != LoginProgress::Connected)
        return false;
    EOS_Auth_CopyIdTokenOptions Copy{};
    Copy.ApiVersion = EOS_AUTH_COPYIDTOKEN_API_LATEST;
    Copy.AccountId = LocalAccount;
    EOS_Auth_IdToken* Fresh = nullptr;
    if (EOS_Auth_CopyIdToken(Auth, &Copy, &Fresh) != EOS_EResult::EOS_Success || !Fresh ||
        !Fresh->JsonWebToken || !*Fresh->JsonWebToken)
    {
        if (Fresh)
            EOS_Auth_IdToken_Release(Fresh);
        return false;
    }
    std::snprintf(Out, Capacity, "%s", Fresh->JsonWebToken);
    const bool Copied = Out[0] != 0 && std::strlen(Fresh->JsonWebToken) < Capacity;
    if (!Copied)
        Out[0] = 0;
    EOS_Auth_IdToken_Release(Fresh);
    return Copied;
}

void* InspectEpicPlatformHandle() noexcept { return Platform; }
void* InspectEpicAccountHandle() noexcept { return LocalAccount; }
}
