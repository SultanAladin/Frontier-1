//============================================================================================================================================
//                                                               EPICEXCHANGE.H
//============================================================================================================================================
// 📦 Declares the project-owned EOS login lifetime and redacted diagnostic reception.

#pragma once
#include "LoginSequence.h"
#include <cstddef>

namespace Networking
{
using DiagnosticReception = void (*)(const char*);

struct LoginSpecification
{
    const char* Secret;
    const char* ClientId;
    const char* Method;
    const char* DeveloperCredential;
    bool AllowCreation;
    bool EnableSocial = true;
    const char* StorageKey = nullptr;
    // PersistentAuth first (desktop saved Epic token, no browser), then the
    // normal method as fallback. Ignored for developer logins.
    bool AutoLogin = false;
};

const char* ValidateEpicCredentials(const char* Secret, const char* ClientId) noexcept;
bool ConstructEpic(const LoginSpecification& Specification, DiagnosticReception Reception) noexcept;
bool VerifyEpicPlatform(DiagnosticReception Reception) noexcept;
bool VerifyEpicRuntime(DiagnosticReception Reception) noexcept;
bool ConstructEpic(DiagnosticReception Reception) noexcept;
bool ShutdownEpic(DiagnosticReception Reception = nullptr) noexcept;
struct AccountProfile
{
    char DisplayName[256]{};
    char Country[128]{};
    char Language[64]{};
    bool Pending = false;
    bool Available = false;
};
const AccountProfile& InspectEpicProfile() noexcept;
bool QueryEpicProfile() noexcept;
bool ApproveEpicUserCreation() noexcept;
bool QueryEpicFriends() noexcept;
bool ShowEpicFriends() noexcept;
bool HideEpicFriends() noexcept;
bool EpicOverlayOwnsInput() noexcept;
int InspectFriendCount() noexcept;
const char* InspectFriendName(int Index) noexcept;
const char* InspectFriendship(int Index) noexcept;
const char* InspectFriendsReading() noexcept;
bool FriendsQueryPending() noexcept;
const char* InspectOverlayReading() noexcept;
// Revokes the desktop persistent token (DeletePersistentAuth). Needs a live
// platform (call while logged in, or right after the next login). Async; the
// result lands in the diagnostic feed.
bool RevokeEpicPersistentAuth() noexcept;
void AdvanceEpic() noexcept;
void RetireEpic() noexcept;
LoginProgress InspectLogin() noexcept;
// Multiplayer transport support. The identity token is copied for Photon's Epic
// authentication and must never be logged; the opaque handles back the Ecom query.
bool CopyEpicIdentityToken(char* Out, std::size_t Capacity) noexcept;
void* InspectEpicPlatformHandle() noexcept;
void* InspectEpicAccountHandle() noexcept;
}
