//============================================================================================================================================
//                                                               WINDOWHOST.CPP
//============================================================================================================================================
// 📦 Standalone ImGui/GLFW login diagnostic; the Frontier game host and project DLL remain separate.

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <commdlg.h>
#include <wincred.h>
#endif
#include "EpicExchange.h"
#include "LobbyPanel.h"
#include "LocalConfiguration.h"
#include "AppSettings.h"
#include "Localization.h"
#include "TransportRouter.h"
#include "TransportCodec.h"
#include "PhotonTransport.h"
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl2.h>
#include <GLFW/glfw3.h>
#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace
{
char Secret[512]{};
char StorageKey[65]{};
bool DisconnectRequested = false;
bool CloseAfterDisconnect = false;
std::chrono::steady_clock::time_point DisconnectStarted;
void RequestDisconnect()
{
    if (DisconnectRequested) return;
    DisconnectRequested = true;
    DisconnectStarted = std::chrono::steady_clock::now();
    Networking::LeaveRoom();
}
char ClientId[256] = "xyza7891AKjtZj8wTzcmI5F3oc1zLU4s";
char Credential[128] = "PlayerOne";
char Diagnostics[32768]{};
size_t DiagnosticLength = 0;
bool AllowCreation = false;
bool EnableSocial = true;
char ClipboardText[8192]{};
bool Busy = false;
bool Attempted = false;
bool Verified = false;
bool Cancelled = false;
bool SdkReady = false;
bool ScrollPending = false;
int Method = 1;
bool RememberCredential = false;
bool AutoLoginEnabled = false;
bool PendingForget = false;
bool PremiumMultiplayer = false;
int LangIndex = 0;
bool AutoStarted = false;
bool ContinueAfterSetup = false;
bool SetupRequested = false;
const char* SetupError = "";
ImFont* TitleFont = nullptr;
struct LogReading { char Text[2048]{}; char Time[16]{}; int Severity = 0; };
LogReading LogReadings[128]{};
int LogCount = 0;
const ImVec4 SuccessColour(0.48f, 0.85f, 0.52f, 1);
const ImVec4 WarningColour(0.91f, 0.72f, 0.39f, 1);
const ImVec4 ErrorColour(0.96f, 0.51f, 0.47f, 1);
const ImVec4 Muted(0.54f, 0.54f, 0.56f, 1.0f);
const ImVec4 Accent(0.12f, 0.37f, 1.0f, 1.0f);

void WipeClipboard() noexcept
{
    volatile char* Bytes = ClipboardText;
    for (size_t Index = 0; Index < sizeof(ClipboardText); ++Index)
        Bytes[Index] = 0;
}

void WipeSecret() noexcept
{
    volatile char* Bytes = Secret;
    for (size_t Index = 0; Index < sizeof(Secret); ++Index)
        Bytes[Index] = 0;
}

void ReceiveDiagnostic(const char* Text)
{
    const size_t Length = std::strlen(Text);
    if (DiagnosticLength + Length + 2 >= sizeof(Diagnostics))
    {
        DiagnosticLength = 0;
        Diagnostics[0] = 0;
    }
    const int Written = std::snprintf(Diagnostics + DiagnosticLength, sizeof(Diagnostics) - DiagnosticLength,
        "%s\n", Text);
    if (Written > 0)
        DiagnosticLength += std::min(static_cast<size_t>(Written), sizeof(Diagnostics) - DiagnosticLength - 1);
    if (LogCount == 128)
    {
        std::move(LogReadings + 1, LogReadings + 128, LogReadings);
        --LogCount;
    }
    auto& Reading = LogReadings[LogCount++];
    std::snprintf(Reading.Text, sizeof(Reading.Text), "%s", Text);
    static const auto Start = std::chrono::steady_clock::now();
    const auto Seconds = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - Start).count();
    std::snprintf(Reading.Time, sizeof(Reading.Time), "%02lld:%02lld", static_cast<long long>(Seconds / 60), static_cast<long long>(Seconds % 60));
    std::string Lower(Text);
    std::transform(Lower.begin(), Lower.end(), Lower.begin(), [](unsigned char C) { return static_cast<char>(std::tolower(C)); });
    const auto Has = [&Lower](const char* Word) { return Lower.find(Word) != std::string::npos; };
    Reading.Severity = Has("failed") || Has("error") || Has("refused") || Has("could not") ? 3 :
        Has("too long") || Has("whitespace") || Has("missing") || Has("not ready") || Has("empty") || Has("cancelled") || Has("canceled") || Has("not bootstrapped") ? 2 :
        Has("=success") || Has("eos_success") || Has("sdk_initialized_once=1") ? 1 : 0;
    ScrollPending = true;
}

#if defined(_WIN32)
const char* ReadUnicodeClipboard(ImGuiContext*)
{
    WipeClipboard();
    if (!IsClipboardFormatAvailable(CF_UNICODETEXT))
    {
        ReceiveDiagnostic("Clipboard has no plain Unicode text. Use the portal copy icon or type the field manually.");
        return ClipboardText;
    }
    if (!OpenClipboard(nullptr))
    {
        ReceiveDiagnostic("Clipboard is busy. Try pasting again.");
        return ClipboardText;
    }
    HANDLE Content = GetClipboardData(CF_UNICODETEXT);
    const auto* Text = Content ? static_cast<const wchar_t*>(GlobalLock(Content)) : nullptr;
    bool Copied = false;
    if (Text)
    {
        const size_t Capacity = GlobalSize(Content) / sizeof(wchar_t);
        size_t Length = 0;
        while (Length < Capacity && Text[Length])
            ++Length;
        if (Length < Capacity && Length < 4096)
        {
            const int Count = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, Text, static_cast<int>(Length),
                ClipboardText, static_cast<int>(sizeof(ClipboardText)) - 1, nullptr, nullptr);
            Copied = Count > 0 || Length == 0;
            if (Count > 0)
                ClipboardText[Count] = 0;
        }
        GlobalUnlock(Content);
    }
    CloseClipboard();
    if (!Copied)
        ReceiveDiagnostic("Clipboard text could not be read safely. Copy only the field text and retry.");
    return ClipboardText;
}
#endif

#if defined(_WIN32)
constexpr wchar_t CredentialTarget[] = L"Frontier/Charge/Dev/EOSClient";

bool LoadCredential()
{
    PCREDENTIALW Saved = nullptr;
    if (!CredReadW(CredentialTarget, CRED_TYPE_GENERIC, 0, &Saved))
        return false;
    bool Valid = Saved->CredentialBlob && Saved->CredentialBlobSize > 0 &&
        Saved->CredentialBlobSize < sizeof(Secret) && Saved->UserName;
    char LoadedClient[sizeof(ClientId)]{};
    if (Valid)
        Valid = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, Saved->UserName, -1,
            LoadedClient, sizeof(LoadedClient), nullptr, nullptr) > 0;
    if (Valid)
    {
        WipeSecret();
        std::memcpy(Secret, Saved->CredentialBlob, Saved->CredentialBlobSize);
        std::memcpy(ClientId, LoadedClient, sizeof(ClientId));
        RememberCredential = true;
    }
    if (Saved->CredentialBlob)
        SecureZeroMemory(Saved->CredentialBlob, Saved->CredentialBlobSize);
    CredFree(Saved);
    return Valid;
}

bool SaveCredential()
{
    wchar_t User[256]{};
    if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, ClientId, -1, User, 256))
        return false;
    CREDENTIALW Saved{};
    Saved.Type = CRED_TYPE_GENERIC;
    Saved.TargetName = const_cast<wchar_t*>(CredentialTarget);
    Saved.UserName = User;
    Saved.CredentialBlobSize = static_cast<DWORD>(std::strlen(Secret));
    Saved.CredentialBlob = reinterpret_cast<LPBYTE>(Secret);
    Saved.Persist = CRED_PERSIST_LOCAL_MACHINE;
    const bool Stored = CredWriteW(&Saved, 0) != FALSE;
    ReceiveDiagnostic(Stored ? "Application credential saved in your Windows user vault." : "Could not save application credential.");
    return Stored;
}

bool ForgetCredential(bool WipeInput = true)
{
    if (CredDeleteW(CredentialTarget, CRED_TYPE_GENERIC, 0) || GetLastError() == ERROR_NOT_FOUND)
    {
        RememberCredential = false;
        if (WipeInput) WipeSecret();
        ReceiveDiagnostic("No saved application credential remains.");
        return true;
    }
    else
        ReceiveDiagnostic("Could not remove saved credential. Use Windows Credential Manager.");
    return false;
}
#endif

bool LoadPortableConfiguration()
{
    Networking::PortableSettings Settings;
    if (!Networking::ReadPortableSettings(Settings)) return false;
    std::snprintf(Secret, sizeof(Secret), "%s", Settings.Secret.c_str());
    std::snprintf(ClientId, sizeof(ClientId), "%s", Settings.ClientId.c_str());
    std::snprintf(StorageKey, sizeof(StorageKey), "%s", Settings.StorageKey.c_str());
    Networking::WipeSettings(Settings);
    ReceiveDiagnostic("Private portable configuration loaded. Keep that file private; it contains readable credentials and the data key.");
    return true;
}

void PersistAppSettings()
{
    Networking::AppSettings Settings;
    Settings.Language = Networking::CurrentLanguage;
    Settings.AutoLogin = AutoLoginEnabled;
    Settings.ForgetPersistent = PendingForget;
    Settings.PremiumMultiplayer = PremiumMultiplayer;
    if (!Networking::SaveAppSettings(Settings))
        ReceiveDiagnostic("Could not save app settings on this PC.");
}

void BeginLogin()
{
    if (Busy || Verified)
        return;
    Verified = false;
    Cancelled = false;
    Attempted = true;
    if (!Secret[0])
    {
        ReceiveDiagnostic("Client secret is empty. Enter the ROTATED EOS application secret; it is not your Epic password.");
        return;
    }
    if (!ClientId[0] || (Method == 0 && !Credential[0]))
    {
        ReceiveDiagnostic("Client ID or Developer Auth Tool credential NAME is missing.");
        return;
    }
    if (const char* Error = Networking::ValidateEpicCredentials(Secret, ClientId))
    {
        ReceiveDiagnostic("configuration=refused; credentials were not submitted to EOS");
        ReceiveDiagnostic(Error);
        SetupError = Error;
        SetupRequested = true;
        return;
    }
    Networking::RetireEpic();
    const Networking::LoginSpecification Specification{
        Secret, ClientId, Method == 0 ? "developer" : "accountportal", Credential, AllowCreation, EnableSocial, StorageKey, AutoLoginEnabled};
    Busy = Networking::ConstructEpic(Specification, ReceiveDiagnostic);
    WipeSecret();
    WipeClipboard();
    ReceiveDiagnostic(RememberCredential ? "Temporary secret cleared. The saved Windows credential will be reused on retry." :
        "Temporary secret cleared. Enter it in Setup before retrying, or enable Remember on this PC.");
    if (!Busy)
        Networking::RetireEpic();
}

void CancelLogin()
{
    Networking::RetireEpic();
    Busy = false;
    Verified = false;
    Cancelled = true;
    WipeSecret();
    ReceiveDiagnostic("Login cancelled locally. No successful authentication is claimed.");
}

void CopyDiagnostics()
{
    const std::string Text = std::string("Project-Networking diagnostic\nlogin_verified=") +
        (Verified ? "1\n" : "0\n") + Diagnostics;
#if defined(_WIN32)
    const int Count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, Text.c_str(), -1, nullptr, 0);
    if (Count <= 0)
    {
        ReceiveDiagnostic("Could not encode the log for the clipboard.");
        return;
    }
    HGLOBAL Storage = GlobalAlloc(GMEM_MOVEABLE, static_cast<SIZE_T>(Count) * sizeof(wchar_t));
    auto* Content = Storage ? static_cast<wchar_t*>(GlobalLock(Storage)) : nullptr;
    if (!Content)
    {
        if (Storage) GlobalFree(Storage);
        ReceiveDiagnostic("Could not allocate clipboard storage.");
        return;
    }
    const bool Encoded = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, Text.c_str(), -1, Content, Count) == Count;
    GlobalUnlock(Storage);
    const HWND Owner = GetActiveWindow();
    if (!Encoded || !Owner || !OpenClipboard(Owner))
    {
        GlobalFree(Storage);
        ReceiveDiagnostic("Clipboard unavailable. Click Copy log again.");
        return;
    }
    const bool Copied = EmptyClipboard() && SetClipboardData(CF_UNICODETEXT, Storage);
    CloseClipboard();
    if (!Copied) GlobalFree(Storage); // Windows owns the allocation only after success.
    ReceiveDiagnostic(Copied ? "Redacted log copied. Paste it into your message." : "Could not copy the log. Try again.");
#else
    ImGui::SetClipboardText(Text.c_str());
    ReceiveDiagnostic("Redacted log sent to the clipboard.");
#endif
}

void ConfigureAppearance()
{
    ImGui::StyleColorsDark();
    ImGuiStyle& Style = ImGui::GetStyle();
    Style.WindowPadding = ImVec2(24, 20);
    Style.FramePadding = ImVec2(12, 8);
    Style.ItemSpacing = ImVec2(10, 8);
    Style.WindowRounding = 0;
    Style.ChildRounding = 12;
    Style.FrameRounding = 16;
    Style.PopupRounding = 12;
    Style.ScrollbarSize = 7;
    Style.ScrollbarRounding = 8;
    Style.ChildBorderSize = 1;
    Style.FrameBorderSize = 1;
    Style.Colors[ImGuiCol_WindowBg] = ImVec4(0.039f, 0.039f, 0.039f, 1); // #0A0A0A
    Style.Colors[ImGuiCol_ChildBg] = ImVec4(0.070f, 0.070f, 0.070f, 1);  // #121212
    Style.Colors[ImGuiCol_PopupBg] = ImVec4(0.070f, 0.070f, 0.070f, 1);
    Style.Colors[ImGuiCol_FrameBg] = ImVec4(0.0f, 0.0f, 0.0f, 1);
    Style.Colors[ImGuiCol_FrameBgHovered] = ImVec4(0.08f, 0.08f, 0.08f, 1);
    Style.Colors[ImGuiCol_Border] = ImVec4(1.0f, 1.0f, 1.0f, 0.06f);
    Style.Colors[ImGuiCol_Text] = ImVec4(1.0f, 1.0f, 1.0f, 1);
    Style.Colors[ImGuiCol_TextDisabled] = Muted;
    Style.Colors[ImGuiCol_Button] = ImVec4(0.10f, 0.10f, 0.10f, 1);
    Style.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.13f, 0.13f, 0.13f, 1);
    Style.Colors[ImGuiCol_ButtonActive] = ImVec4(0.16f, 0.16f, 0.16f, 1);
    Style.Colors[ImGuiCol_Header] = Style.Colors[ImGuiCol_Button];
    Style.Colors[ImGuiCol_HeaderHovered] = Style.Colors[ImGuiCol_ButtonHovered];
    Style.Colors[ImGuiCol_HeaderActive] = Style.Colors[ImGuiCol_ButtonActive];
    Style.Colors[ImGuiCol_CheckMark] = ImVec4(0.30f, 0.50f, 1.0f, 1);
    Style.Colors[ImGuiCol_SliderGrab] = ImVec4(0.30f, 0.50f, 1.0f, 1);
    Style.Colors[ImGuiCol_SliderGrabActive] = ImVec4(0.12f, 0.37f, 1.0f, 1);
}

void PresentSetup()
{
    const auto* Viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(Viewport->WorkPos.x + Viewport->WorkSize.x * 0.5f,
        Viewport->WorkPos.y + Viewport->WorkSize.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(std::min(550.0f, Viewport->WorkSize.x - 32),
        std::min(730.0f, Viewport->WorkSize.y - 32)), ImGuiCond_Always);
    if (!ImGui::BeginPopupModal(Networking::T("One-time setup"), nullptr, ImGuiWindowFlags_NoResize))
        return;
    ImGui::BeginChild("Setup fields", ImVec2(0, -140));
    ImGui::TextWrapped("%s", Networking::T("Install EpicOnlineServicesInstaller.exe once, then open Charge.exe (the included Epic launcher). The app can then ask Epic to display its login UI."));
    ImGui::Spacing();
    ImGui::TextUnformatted(Networking::T("Application credential"));
    ImGui::TextColored(Muted, "%s", Networking::T("Not your Epic account password. Use a rotated secret."));
    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##secret", Networking::T("EOS client secret"), Secret, sizeof(Secret),
        ImGuiInputTextFlags_Password | ImGuiInputTextFlags_NoUndoRedo | ImGuiInputTextFlags_AutoSelectAll);
#if defined(_WIN32)
    if (ImGui::SmallButton(Networking::T("Paste secret")))
    {
        const char* Text = ReadUnicodeClipboard(nullptr);
        if (std::strlen(Text) < sizeof(Secret))
            std::snprintf(Secret, sizeof(Secret), "%s", Text);
        else
            ReceiveDiagnostic("Clipboard text too long; copy only the client secret.");
        WipeClipboard();
    }
    ImGui::Checkbox(Networking::T("Remember on this PC (Windows Credential Manager)"), &RememberCredential);
#endif
    if (ImGui::Checkbox(Networking::T("Auto-login on this PC (saved Epic token)"), &AutoLoginEnabled)) PersistAppSettings();
    ImGui::TextColored(Muted, "%s", Networking::T("Auto-login signs you in on startup when credentials are saved."));
    if (ImGui::Combo(Networking::T("App language"), &LangIndex, Networking::LanguageComboItems()))
    {
        Networking::SetAppLanguage(LangIndex == 1 ? Networking::AppLanguage::ChineseSimplified : Networking::AppLanguage::English);
        PersistAppSettings();
    }
    if (ImGui::Checkbox(Networking::T("Premium multiplayer (Photon)"), &PremiumMultiplayer))
    {
        Networking::SetPremiumMultiplayer(PremiumMultiplayer);
        PersistAppSettings();
    }
    ImGui::TextColored(Muted, "%s", Networking::T("Route match traffic over Photon after login. Off stays on EOS only."));
    ImGui::TextColored(Muted, "%s", Networking::T("Premium is a manual switch for now. Xsolla will decide it later."));
    if (ImGui::CollapsingHeader(Networking::T("Advanced")))
    {
        ImGui::TextWrapped("%s", Networking::T("Optional portable setup: put your private Charge.local.ini next to Charge.exe. It contains readable credentials; never share or upload it. Carry the same data key between PCs."));
        ImGui::TextUnformatted(Networking::T("Cloud storage encryption key (64 hex characters)"));
        ImGui::SetNextItemWidth(-1);
        ImGui::InputText("##storage-key", StorageKey, sizeof(StorageKey), ImGuiInputTextFlags_Password | ImGuiInputTextFlags_NoUndoRedo | ImGuiInputTextFlags_AutoSelectAll);
        ImGui::TextUnformatted("EOS client ID");
        ImGui::SetNextItemWidth(-1);
        ImGui::InputText("##client", ClientId, sizeof(ClientId), ImGuiInputTextFlags_AutoSelectAll);
        ImGui::SetNextItemWidth(-1);
        ImGui::Combo("##method", &Method, "Developer Auth Tool\0Epic Account Portal\0");
        if (Method == 0)
        {
            ImGui::SetNextItemWidth(-1);
            ImGui::InputText(Networking::T("Saved developer credential"), Credential, sizeof(Credential));
            ImGui::TextWrapped("%s", Networking::T("Keep Epic's Developer Auth Tool running on localhost:6547."));
        }
        ImGui::TextWrapped("%s", Networking::T("Charge requires Basic Profile, Friends List, Presence and Country permissions. Epic will ask for consent."));
        ImGui::Checkbox(Networking::T("Load friends after login"), &EnableSocial);
        ImGui::Checkbox(Networking::T("Allow NEW Dev product-user creation"), &AllowCreation);
        if (AllowCreation)
            ImGui::TextWrapped("%s", Networking::T("This allows a real new PUID to be created. Do not use it to bypass identity linking."));
        if (ImGui::SmallButton(Networking::T("Check SDK")))
            SdkReady = Networking::VerifyEpicRuntime(ReceiveDiagnostic);
#if defined(_WIN32)
        ImGui::SameLine();
        if (ImGui::SmallButton(Networking::T("Forget saved credential")))
            ForgetCredential();
#endif
        ImGui::SameLine();
        if (ImGui::SmallButton(Networking::T("Forget auto-login")))
        {
            AutoLoginEnabled = false;
            PendingForget = true;
            PersistAppSettings();
            ReceiveDiagnostic("Auto-login disabled. The saved Epic token will be revoked on the next login.");
        }
        ImGui::TextWrapped("%s", Networking::InspectOverlayReading());
    }
    ImGui::Spacing();
    ImGui::EndChild();
    if (ImGui::Button(ContinueAfterSetup ? Networking::T("Save & log in") : Networking::T("Apply setup"), ImVec2(220, 42)))
    {
        if (const char* Error = Networking::ValidateEpicCredentials(Secret, ClientId))
            SetupError = Error;
        else
        {
            bool Stored = true;
#if defined(_WIN32)
            Stored = RememberCredential ? SaveCredential() : ForgetCredential(false);
#endif
            if (Stored)
            {
                SetupError = "";
                ImGui::CloseCurrentPopup();
                if (ContinueAfterSetup)
                    BeginLogin();
            }
            else
                SetupError = "Windows could not update saved credentials. Check Windows Credential Manager and retry.";
        }
    }
    ImGui::SameLine();
    if (ImGui::Button(Networking::T("Cancel"), ImVec2(110, 42)))
    {
        WipeSecret();
        WipeClipboard();
        ImGui::CloseCurrentPopup();
    }
    if (*SetupError)
        ImGui::TextWrapped("%s", SetupError);
    ImGui::EndPopup();
}

void PresentLogin()
{
    const ImGuiViewport* View = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(View->WorkPos);
    ImGui::SetNextWindowSize(View->WorkSize);
    ImGui::Begin("Charge", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoSavedSettings);
    ImGui::BeginDisabled(Networking::EpicOverlayOwnsInput());
    ImGui::TextColored(ImVec4(0.30f, 0.50f, 1.0f, 1.0f), "FRONTIER");
    ImGui::SameLine();
    ImGui::TextColored(Muted, "CHARGE  /  ONLINE ACCESS");
    ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - 66);
    ImGui::BeginDisabled(Busy || Verified);
    if (ImGui::SmallButton(Networking::T("Setup")))
    {
#if defined(_WIN32)
        if (!Secret[0] && !LoadPortableConfiguration()) LoadCredential();
#endif
        ContinueAfterSetup = false;
        SetupError = "";
        ImGui::OpenPopup(Networking::T("One-time setup"));
    }
    ImGui::EndDisabled();
    ImGui::Spacing();
    ImGui::Spacing();

    const bool CreationConsent = Networking::InspectLogin() == Networking::LoginProgress::WaitingForCreationConsent;
    const float Available = ImGui::GetContentRegionAvail().x;
    const float Height = ImGui::GetContentRegionAvail().y - 37;
    ImGui::BeginChild("Login card", ImVec2(Available * 0.455f, Height), ImGuiChildFlags_Borders);
    const ImVec2 Origin = ImGui::GetCursorScreenPos();
    ImDrawList* Ink = ImGui::GetWindowDrawList();
    const ImVec2 Centre(Origin.x + 47, Origin.y + 48);
    Ink->AddCircleFilled(Centre, 44, IM_COL32(40, 48, 40, 255), 64);
    Ink->AddCircle(Centre, 44, IM_COL32(91, 108, 88, 120), 64, 1.0f);
    Ink->AddCircle(Centre, 33, IM_COL32(164, 185, 149, 70), 64, 1.0f);
    Ink->AddLine(ImVec2(Centre.x + 6, Centre.y - 19), ImVec2(Centre.x - 10, Centre.y + 3), IM_COL32(201, 221, 181, 255), 4);
    Ink->AddLine(ImVec2(Centre.x - 10, Centre.y + 3), ImVec2(Centre.x + 9, Centre.y + 3), IM_COL32(201, 221, 181, 255), 4);
    Ink->AddLine(ImVec2(Centre.x + 9, Centre.y + 3), ImVec2(Centre.x - 6, Centre.y + 22), IM_COL32(201, 221, 181, 255), 4);
    ImGui::Dummy(ImVec2(0, Verified || CreationConsent ? 90 : 117));
    ImGui::TextColored(Muted, "%s", Networking::T("YOUR SPACE. YOUR NEXT CHAPTER."));
    ImGui::Spacing();
    if (TitleFont) ImGui::PushFont(TitleFont);
    ImGui::TextUnformatted(Verified ? Networking::T("You're in.") : CreationConsent ? Networking::T("One last step.") : Networking::T("Welcome\nback."));
    if (TitleFont) ImGui::PopFont();
    ImGui::Spacing();
    ImGui::TextColored(Muted, "%s", Verified ? Networking::T("Your Epic identity is connected.") : Networking::T("Log in to access your content."));
    if (CreationConsent)
        ImGui::TextWrapped("%s", Networking::T("Epic sign-in succeeded. This account has no product user in this Dev deployment. Create a NEW Dev profile only if you do not need to link an existing game identity. Cancel otherwise."));
    else if (Verified)
    {
        const auto& Profile = Networking::InspectEpicProfile();
        ImGui::TextWrapped("%s", Profile.DisplayName[0] ? Profile.DisplayName :
            Profile.Pending ? Networking::T("Loading your Epic profile...") : Networking::T("Display name unavailable"));
        ImGui::TextColored(SuccessColour, "%s", Networking::T("Epic Auth verified  /  EOS Connect verified"));
        ImGui::TextWrapped(Networking::T("Transport: %s"), Networking::T(Networking::InspectTransportDisplayName()));
        ImGui::TextWrapped(Networking::T("Country: %s"), Profile.Country[0] ? Profile.Country : Networking::T("Not provided by Epic"));
        ImGui::TextWrapped(Networking::T("Language: %s"), Profile.Language[0] ? Profile.Language : Networking::T("Not provided by Epic"));
        ImGui::BeginDisabled(Profile.Pending);
        if (ImGui::SmallButton(Networking::T("Refresh profile"))) Networking::QueryEpicProfile();
        ImGui::EndDisabled();
    }
    else ImGui::TextColored(Muted, "%s", Networking::T("Secure sign-in with your Epic account."));
    ImGui::Dummy(ImVec2(0, 25));
    ImGui::PushStyleColor(ImGuiCol_Button, Accent);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(.39f,.57f,1,1));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(.17f,.35f,.86f,1));
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 1, 1));
    ImGui::BeginDisabled(Busy && !CreationConsent);
    if (ImGui::Button(Verified ? Networking::T("Open Epic friends") : CreationConsent ? Networking::T("Create NEW Dev profile & continue") : Busy ? Networking::T("Waiting for Epic...") : Networking::T("Log in with Epic    ->"), ImVec2(-1, 54)))
    {
        if (CreationConsent)
            Networking::ApproveEpicUserCreation();
        else if (Verified)
            Networking::ShowEpicFriends();
        else
        {
#if defined(_WIN32)
            if (!Secret[0] && !LoadPortableConfiguration()) LoadCredential();
#endif
            if (!Secret[0])
                SetupRequested = true;
            else
                BeginLogin();
        }
    }
    ImGui::EndDisabled();
    ImGui::PopStyleColor(4);
    if (Busy && ImGui::Button(Networking::T("Cancel"), ImVec2(-1, 38)))
        CancelLogin();
    if (Verified && ImGui::SmallButton(Networking::T("Disconnect")))
    {
        Networking::RetireEpic();
        Verified = false;
        Attempted = false;
        ReceiveDiagnostic("Disconnected locally. Ready for another login.");
    }
    if (Verified && ImGui::CollapsingHeader(Networking::T("Friends")))
    {
        ImGui::TextWrapped("%s", Networking::InspectFriendsReading());
        ImGui::BeginDisabled(Networking::FriendsQueryPending());
        if (ImGui::SmallButton(Networking::T("Refresh friends"))) Networking::QueryEpicFriends();
        ImGui::EndDisabled();
        ImGui::BeginChild("Friend names", ImVec2(0, 100));
        for (int Index = 0; Index < Networking::InspectFriendCount(); ++Index)
        {
            ImGui::TextUnformatted(Networking::InspectFriendName(Index));
            ImGui::TextColored(Muted, "%s", Networking::InspectFriendship(Index));
        }
        ImGui::EndChild();
        ImGui::TextWrapped("%s", Networking::T("Only friends authorized for this app may be listed."));
    }
    ImGui::Spacing();
    const char* Status = CreationConsent ? Networking::T("Waiting for your consent - no login retry") : Busy ? Networking::T("Waiting for Epic...") : Verified ? Networking::T("Auth + Connect verified") :
        Cancelled ? Networking::T("Cancelled") : Attempted ? Networking::T("Not signed in - check the activity log") : Networking::T("Ready when you are");
    ImGui::TextColored(Verified ? SuccessColour : Attempted && !Busy ? WarningColour : Muted, "%s", Status);
    ImGui::Dummy(ImVec2(0, 16));
    ImGui::TextColored(Muted, "DEV SANDBOX   /   EPIC ONLINE SERVICES");
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("Activity card", ImVec2(0, Height), ImGuiChildFlags_Borders);
    ImGui::TextUnformatted(Networking::T("Activity"));
    ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - 82);
    if (ImGui::SmallButton(Networking::T("Copy log")))
        CopyDiagnostics();
    ImGui::TextColored(Muted, "%s", Networking::T("Live events. No secrets or tokens."));
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    ImGui::BeginChild("Console", ImVec2(0, -30));
    for (int Index = 0; Index < LogCount; ++Index)
    {
        const auto& Reading = LogReadings[Index];
        const ImVec4 Tone = Reading.Severity == 3 ? ErrorColour : Reading.Severity == 2 ? WarningColour :
            Reading.Severity == 1 ? SuccessColour : Muted;
        ImGui::TextColored(Muted, "%s", Reading.Time);
        ImGui::SameLine();
        ImGui::TextColored(Tone, "%s", Reading.Severity == 3 ? "ERROR" : Reading.Severity == 2 ? "WARN" :
            Reading.Severity == 1 ? "OK" : "INFO");
        ImGui::PushStyleColor(ImGuiCol_Text, Tone);
        ImGui::PushTextWrapPos(0);
        ImGui::TextUnformatted(Reading.Text);
        ImGui::PopTextWrapPos();
        ImGui::PopStyleColor();
        ImGui::Spacing();
    }
    if (ScrollPending)
    {
        ImGui::SetScrollHereY(1);
        ScrollPending = false;
    }
    ImGui::EndChild();
    ImGui::TextColored(SdkReady ? SuccessColour : WarningColour, "%s", SdkReady ? Networking::T("*  SDK ready") : Networking::T("*  SDK unavailable"));
    ImGui::SameLine();
    ImGui::TextColored(Muted, "    %s", Networking::T("Player login is verified separately."));
    ImGui::EndChild();
    ImGui::TextColored(Muted, "Project-Networking                                               %s", Networking::T("Private credentials. Clear feedback."));
    if (SetupRequested)
    {
        SetupRequested = false;
        ContinueAfterSetup = true;
        ImGui::OpenPopup(Networking::T("One-time setup"));
    }
    PresentSetup();
    ImGui::EndDisabled();
    ImGui::End();
}

bool CaptureWindow(int Width, int Height, const char* Filename = "WindowProof.bmp")
{
    if (Width <= 0 || Height <= 0)
        return false;
    const unsigned RowBytes = (static_cast<unsigned>(Width) * 3 + 3) & ~3u;
    std::vector<unsigned char> Pixels(static_cast<size_t>(RowBytes) * Height);
    glPixelStorei(GL_PACK_ALIGNMENT, 4);
    glReadPixels(0, 0, Width, Height, GL_RGB, GL_UNSIGNED_BYTE, Pixels.data());
    const auto Range = std::minmax_element(Pixels.begin(), Pixels.end());
    if (*Range.first == *Range.second)
        return false;
    if (glGetError() != GL_NO_ERROR)
        return false;
    for (int Y = 0; Y < Height; ++Y)
        for (int X = 0; X < Width; ++X)
            std::swap(Pixels[Y * RowBytes + X * 3], Pixels[Y * RowBytes + X * 3 + 2]);
    unsigned char Header[54]{};
    const auto Encode = [&Header](int Offset, unsigned Number)
    {
        for (int Index = 0; Index < 4; ++Index)
            Header[Offset + Index] = static_cast<unsigned char>((Number >> (8 * Index)) & 255);
    };
    Header[0] = 'B'; Header[1] = 'M'; Header[26] = 1; Header[28] = 24;
    Encode(2, 54 + static_cast<unsigned>(Pixels.size()));
    Encode(10, 54); Encode(14, 40); Encode(18, Width); Encode(22, Height);
    std::ofstream File(Filename, std::ios::binary);
    File.write(reinterpret_cast<const char*>(Header), sizeof(Header));
    File.write(reinterpret_cast<const char*>(Pixels.data()), static_cast<std::streamsize>(Pixels.size()));
    File.close();
    return static_cast<bool>(File);
}

bool CheckTransportSelfTest()
{
    using namespace Networking;
    std::vector<std::uint8_t> Frame;
    static constexpr char Tag[] = "smoke";
    if (!PacketCodec::Encode(ReplicationKind::Probe,
            reinterpret_cast<const std::uint8_t*>(Tag), sizeof(Tag) - 1, Frame))
        return false;
    ReplicationKind Kind = ReplicationKind::Input;
    const std::uint8_t* Payload = nullptr;
    std::size_t PayloadLength = 0;
    if (!PacketCodec::Decode(Frame.data(), Frame.size(), Kind, Payload, PayloadLength))
        return false;
    if (Kind != ReplicationKind::Probe || PayloadLength != sizeof(Tag) - 1)
        return false;
    if (SelectTransportKind(true, true) != TransportKind::Photon)
        return false;
    if (SelectTransportKind(false, true) != TransportKind::Eos)
        return false;
    if (SelectTransportKind(true, false) != TransportKind::Eos)
        return false;
    const std::vector<std::uint8_t> Oversized(PacketCodec::MaxPayload + 1, 0);
    if (PacketCodec::Encode(ReplicationKind::Snapshot, Oversized.data(), Oversized.size(), Frame))
        return false;
    Frame[0] = 'X';
    if (PacketCodec::Decode(Frame.data(), Frame.size(), Kind, Payload, PayloadLength))
        return false;
    const char* Display = InspectTransportDisplayName();
    return Display && *Display;
}

int RunWindow(bool Smoke)
{
    glfwSetErrorCallback([](int Number, const char* Description)
    {
        char Text[1024]{};
        std::snprintf(Text, sizeof(Text), "GLFW error %d: %s", Number, Description);
        ReceiveDiagnostic(Text);
        std::ofstream("WindowChecks.log") << Diagnostics;
    });
    if (!glfwInit())
        return 2;
    GLFWwindow* Window = glfwCreateWindow(1180, 760, "Charge | Project-Networking", nullptr, nullptr);
    if (!Window)
    {
        glfwTerminate();
        return 2;
    }
    glfwSetWindowSizeLimits(Window, 960, 720, GLFW_DONT_CARE, GLFW_DONT_CARE);
    glfwMakeContextCurrent(Window);
    glfwSwapInterval(1);
    Networking::AppSettings Startup;
    if (!Networking::LoadAppSettings(Startup))
        ReceiveDiagnostic("Saved app settings were invalid; defaults were restored.");
    Networking::SetAppLanguage(Startup.Language);
    AutoLoginEnabled = Startup.AutoLogin;
    PendingForget = Startup.ForgetPersistent;
    PremiumMultiplayer = Startup.PremiumMultiplayer;
    Networking::SetPremiumMultiplayer(PremiumMultiplayer);
    LangIndex = Startup.Language == Networking::AppLanguage::ChineseSimplified ? 1 : 0;
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& Io = ImGui::GetIO();
    Io.IniFilename = nullptr;
    Io.LogFilename = nullptr;
    Io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
#if defined(_WIN32)
    Io.Fonts->AddFontFromFileTTF("C:/Windows/Fonts/segoeui.ttf", 17.0f);
    TitleFont = Io.Fonts->AddFontFromFileTTF("C:/Windows/Fonts/segoeui.ttf", 42.0f);
    ImFontConfig Merge{}; Merge.MergeMode = true; Merge.FontNo = 0;
    ImFont* BodyCjk = Io.Fonts->AddFontFromFileTTF("C:/Windows/Fonts/msyh.ttc", 17.0f, &Merge,
        Io.Fonts->GetGlyphRangesChineseSimplifiedCommon());
    ImFontConfig MergeTitle = Merge; MergeTitle.DstFont = TitleFont;
    ImFont* TitleCjk = Io.Fonts->AddFontFromFileTTF("C:/Windows/Fonts/msyh.ttc", 42.0f, &MergeTitle,
        Io.Fonts->GetGlyphRangesChineseSimplifiedCommon());
    if (!BodyCjk || !TitleCjk)
        ReceiveDiagnostic("CJK font (msyh.ttc) unavailable; Chinese text may show as boxes.");
#endif
    ConfigureAppearance();
    if (!ImGui_ImplGlfw_InitForOpenGL(Window, true) || !ImGui_ImplOpenGL2_Init())
    {
        ImGui::DestroyContext();
        glfwDestroyWindow(Window);
        glfwTerminate();
        return 2;
    }
    ReceiveDiagnostic("Ready. Credentials remain on this PC; no player login has been attempted.");
    if (Smoke)
    {
        ReceiveDiagnostic("CI rendering check; authentication NOT attempted.");
        ReceiveDiagnostic(reinterpret_cast<const char*>(glGetString(GL_RENDERER)));
    }
#if defined(_WIN32)
    ImGui::GetPlatformIO().Platform_GetClipboardTextFn = ReadUnicodeClipboard;
#endif
    SdkReady = Networking::VerifyEpicRuntime(ReceiveDiagnostic);
    int Result = 0;
    int Cycles = 0;
    const auto AppStart = std::chrono::steady_clock::now();
    while (true)
    {
        glfwPollEvents();
        if (glfwWindowShouldClose(Window))
        {
            if (Smoke || !Verified) break;
            glfwSetWindowShouldClose(Window, GLFW_FALSE);
            CloseAfterDisconnect = true; RequestDisconnect();
        }
        Networking::AdvanceEpic();
        if (!Smoke && !AutoStarted && std::chrono::steady_clock::now() - AppStart > std::chrono::milliseconds(1500))
        {
            AutoStarted = true;
            if (AutoLoginEnabled && !Busy && !Verified && !Attempted)
            {
#if defined(_WIN32)
                if (!Secret[0] && !LoadPortableConfiguration()) LoadCredential();
#else
                if (!Secret[0]) LoadPortableConfiguration();
#endif
                if (Secret[0])
                {
                    ReceiveDiagnostic("auto_login=starting; saved credentials found");
                    BeginLogin();
                }
                else ReceiveDiagnostic("auto_login=skipped; no saved credentials. Open Setup to sign in.");
            }
        }
        if (DisconnectRequested)
        {
            const bool TimedOut = std::chrono::steady_clock::now() - DisconnectStarted > std::chrono::seconds(8);
            if ((Networking::RoomIsClosed() && !Networking::InspectHistory().Busy) || TimedOut)
            {
                if (TimedOut) ReceiveDiagnostic("Cleanup timed out; remote cleanup is not confirmed. Unsynced history remains in the local cache.");
                Networking::RetireEpic(); Verified = false; Busy = false; Attempted = false; DisconnectRequested = false;
                if (CloseAfterDisconnect) break;
            }
        }
        if (Busy)
        {
            const auto Progress = Networking::InspectLogin();
            if (Progress == Networking::LoginProgress::Connected || Progress == Networking::LoginProgress::Refused)
            {
                Verified = Progress == Networking::LoginProgress::Connected;
                Busy = false;
                if (Verified && EnableSocial)
                    Networking::QueryEpicFriends();
                if (Verified && PendingForget)
                {
                    PendingForget = false;
                    PersistAppSettings();
                    if (Networking::RevokeEpicPersistentAuth())
                        ReceiveDiagnostic("Saved Epic token revoked as requested.");
                    else
                        ReceiveDiagnostic("persistent_auth_delete=unavailable right after login.");
                }
                if (!Verified)
                    Networking::RetireEpic();
            }
        }
        else if (Verified && Networking::InspectLogin() != Networking::LoginProgress::Connected)
        {
            Verified = false;
            Networking::RetireEpic();
        }
        ImGui_ImplOpenGL2_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
        if (Smoke && Cycles >= 12)
        {
            Networking::RoomReading Preview;
            Preview.Phase = Networking::RoomPhase::Waiting;
            Preview.Status = "UI preview only - no EOS lobby, voice or match was created";
            Preview.SessionStatus = "Preview match session - not created";
            Preview.Players = {{"Preview - not signed in", false, false, true}, {"Test player 1", true, true, false},
                {"Test player 2", false, true, false}, {"Test player 3", true, true, false}};
            Preview.Search.LobbyStatus = "Preview only - search not executed";
            Preview.Search.SessionStatus = "Preview only - search not executed";
            Networking::RenderLobbyPanel(TitleFont, Diagnostics, CopyDiagnostics, RequestDisconnect, &Preview);
        }
        else if (Verified)
            Networking::RenderLobbyPanel(TitleFont, Diagnostics, CopyDiagnostics, RequestDisconnect);
        else PresentLogin();
        ImGui::Render();
        int Width = 0, Height = 0;
        glfwGetFramebufferSize(Window, &Width, &Height);
        glViewport(0, 0, Width, Height);
        glClearColor(0.039f, 0.039f, 0.039f, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL2_RenderDrawData(ImGui::GetDrawData());
        if (Smoke && ++Cycles == 12)
        {
            glFinish();
            const bool Captured = CaptureWindow(Width, Height);
            BeginLogin();
            const bool Refused = !Busy && !Verified && Attempted;
            Networking::RetireEpic();
            const bool Reused = Networking::VerifyEpicRuntime(ReceiveDiagnostic);
            const bool PlatformReady = Networking::VerifyEpicPlatform(ReceiveDiagnostic);
            const bool Guarded = !Networking::QueryEpicFriends() && !Networking::ShowEpicFriends();
            const bool TransportOk = CheckTransportSelfTest();
            Result = Captured && SdkReady && Refused && Reused && PlatformReady && Guarded && TransportOk ? 0 : 3;
            std::ofstream Proof("WindowChecks.log");
            Proof << "glfw_imgui_rendered=" << Captured << "\nsdk_check=" << SdkReady
                  << "\nmissing_credentials_refused=" << Refused
                  << "\nsdk_reused_same_process=" << Reused
                  << "\nplatform_created_without_auth=" << PlatformReady
                  << "\nsocial_requires_login=" << Guarded
                  << "\ntransport_selftest=" << TransportOk
                  << "\nphoton_linked=" << (Networking::PhotonLinkAvailable() ? 1 : 0)
                  << "\nauthentication=NOT_ATTEMPTED\n" << Diagnostics;
            if (!Proof)
                Result = 3;
        }
        if (Smoke && Cycles == 24)
        {
            glFinish();
            const bool LobbyCaptured = CaptureWindow(Width, Height, "LobbyProof.bmp");
            std::ofstream("WindowChecks.log", std::ios::app) << "lobby_ui_preview_rendered=" << LobbyCaptured
                << "\nlobby_ui_scope=layout_only authentication=NOT_ATTEMPTED\n";
            if (!LobbyCaptured) Result = 3;
            glfwSetWindowShouldClose(Window, GLFW_TRUE);
        }
        glfwSwapBuffers(Window);
    }
    Networking::ShutdownEpic(ReceiveDiagnostic);
    WipeSecret();
    WipeClipboard();
    ImGui_ImplOpenGL2_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(Window);
    glfwTerminate();
    return Result;
}
}

#if defined(_WIN32)
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR CommandLine, int)
{
    const bool Smoke = std::strcmp(CommandLine, "--ui-smoke") == 0;
    try
    {
        const int Result = RunWindow(Smoke);
        if (Result && !Smoke)
            MessageBoxW(nullptr, L"Could not initialize the login window. Check your graphics driver and included DLLs.",
                L"Project-Networking", MB_OK | MB_ICONERROR);
        return Result;
    }
    catch (...)
    {
        Networking::ShutdownEpic();
        WipeSecret();
        WipeClipboard();
        if (!Smoke)
            MessageBoxW(nullptr, L"The login window could not continue. No successful login is claimed.",
                L"Project-Networking", MB_OK | MB_ICONERROR);
        return 4;
    }
}
#else
int main(int ArgumentCount, char** Arguments)
{
    return RunWindow(ArgumentCount == 2 && std::strcmp(Arguments[1], "--ui-smoke") == 0);
}
#endif
