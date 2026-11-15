/*============================================================================================================================================
                                                         AUTHENTICATIONSCOPEPOLICY.H
============================================================================================================================================*/
// 🧩 Identity consent scope enforcement, explicit continuance-token approval window, token redaction, and session policy degradation

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace Frontier::NetworkingSpace
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

inline constexpr double   ContinuanceApprovalTimeoutSeconds = 180.0; // [s] - Maximum explicit profile-creation consent window
inline constexpr uint32_t RequiredHexStorageKeyCharacters   = 64u;   // [count] - Exact hexadecimal characters for cloud cipher key

//------------------------------------------------------------------------------------------------------------------------
//                                                      ENUMERATIONS
//------------------------------------------------------------------------------------------------------------------------

enum class IdentityScopeMask : uint32_t
{
    None                      = 0u,      // [mask] - Zero scopes requested
    EnableBasicProfile        = 1u << 0, // [mask] - Display name and account identifier consent
    EnableFriendsRoster       = 1u << 1, // [mask] - Social roster visibility consent
    EnablePresenceBroadcast   = 1u << 2, // [mask] - Online session presence consent
    EnableCountryCode         = 1u << 3, // [mask] - ISO country code consent (required by portal policy)
    EnableMandatoryPortalMask = 0x0Fu,   // [mask] - All four required Epic Account Services scopes
};

enum class ContinuanceConsentPhase : uint8_t
{
    Dormant         = 0, // [-] - No continuance token awaiting user approval
    AwaitingConsent = 1, // [-] - Continuance token retained; waiting for explicit user approval
    Approved        = 2, // [-] - User explicitly approved creating a new product profile
    Declined        = 3, // [-] - User declined or cancelled; never auto-retry authentication
    Expired         = 4, // [-] - 180-second approval window elapsed; token invalidated
};

enum class SessionRegistrationResolution : uint8_t
{
    FullBackendRegistration       = 0, // [-] - Backend RegisterPlayers succeeded normally
    LifecycleOnlyClientPolicySkip = 1, // [-] - Client-policy restriction code degraded to unregistered lifecycle start
    TerminalBackendFailure        = 2, // [-] - Non-policy error aborted session preparation
};

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct ContinuanceConsentGate
{
    ContinuanceConsentPhase Phase            = ContinuanceConsentPhase::Dormant; // [-] - Current consent gate phase
    uint64_t                TokenFingerprint = 0u;                               // [-] - Non-secret hash of active continuance token
    double                  ElapsedSeconds   = 0.0;                              // [s] - Time elapsed since consent prompt opened
    bool                    ExplicitOptIn    = false;                            // [-] - Advance without prompt only when pre-authorized
};

struct SessionRegistrationOutcome
{
    SessionRegistrationResolution Resolution       = SessionRegistrationResolution::TerminalBackendFailure; // [-] - Evaluated resolution
    bool                          UnregisteredMode = false;                                                 // [-] - Match active without registration
    std::string                   DiagnosticNote   {};                                                      // [-] - Redacted operator diagnostic
};

//------------------------------------------------------------------------------------------------------------------------
//                                                  FUNCTION PROTOTYPES
//------------------------------------------------------------------------------------------------------------------------

[[nodiscard]] constexpr uint32_t BuildMandatoryIdentityScopes(bool LoadFriendsAfterLogin) noexcept;
[[nodiscard]] constexpr bool     VerifyMandatoryIdentityScopes(uint32_t RequestedScopeMask) noexcept;

void                             RegisterContinuancePrompt(ContinuanceConsentGate& Gate, uint64_t TokenFingerprint) noexcept;
void                             AdvanceContinuanceWindow(ContinuanceConsentGate& Gate, double Δt) noexcept;
[[nodiscard]] bool               ApproveContinuanceProfileCreation(ContinuanceConsentGate& Gate, uint64_t TokenFingerprint) noexcept;
void                             DeclineContinuanceProfileCreation(ContinuanceConsentGate& Gate) noexcept;
void                             ResetContinuanceGate(ContinuanceConsentGate& Gate) noexcept;

[[nodiscard]] bool               VerifyHexStorageKeyFormat(std::string_view CandidateHexKey) noexcept;
[[nodiscard]] bool               VerifyJwtTokenCharacters(std::string_view CandidateJwt) noexcept;
[[nodiscard]] std::string        RedactCredentialText(std::string_view RawDiagnosticText);
void                             ZeroSensitiveBytes(std::span<char> SensitiveBuffer) noexcept;

[[nodiscard]] SessionRegistrationOutcome EvaluateSessionRegistrationPolicy(
    int32_t BackendResultCode,
    int32_t SuccessResultCode,
    int32_t ClientPolicyMissingResultCode);

//------------------------------------------------------------------------------------------------------------------------
//                                                TEMPLATE IMPLEMENTATIONS
//------------------------------------------------------------------------------------------------------------------------

constexpr uint32_t BuildMandatoryIdentityScopes([[maybe_unused]] bool LoadFriendsAfterLogin) noexcept
{
    // 💡 Loading friends after login is a post-authentication query preference, never an omission of a required portal scope
    return static_cast<uint32_t>(IdentityScopeMask::EnableMandatoryPortalMask);
}

constexpr bool VerifyMandatoryIdentityScopes(uint32_t RequestedScopeMask) noexcept
{
    const uint32_t Mandatory = static_cast<uint32_t>(IdentityScopeMask::EnableMandatoryPortalMask);
    return (RequestedScopeMask & Mandatory) == Mandatory;
}

} // namespace Frontier::NetworkingSpace
