/*============================================================================================================================================
                                                        AUTHENTICATIONSCOPEPOLICY.CPP
============================================================================================================================================*/
// 🧩 Identity consent scope enforcement, continuance-token approval gate, credential redaction, and client-policy fallback

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/NetworkingSpace/Policies/AuthenticationScopePolicy.h"

#include <algorithm>
#include <array>

namespace Frontier::NetworkingSpace
{

namespace
{

[[nodiscard]] bool MatchCaseInsensitivePrefix(std::string_view Text, std::size_t Offset, std::string_view Prefix) noexcept
{
    if (Offset + Prefix.size() > Text.size())
    {
        return false;
    }
    for (std::size_t Index = 0u; Index < Prefix.size(); ++Index)
    {
        char Left  = Text[Offset + Index];
        char Right = Prefix[Index];
        if (Left >= 'A' && Left <= 'Z') Left = static_cast<char>(Left + ('a' - 'A'));
        if (Right >= 'A' && Right <= 'Z') Right = static_cast<char>(Right + ('a' - 'A'));
        if (Left != Right)
        {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool QuerySecretTerminator(char Character) noexcept
{
    return Character == ' ' || Character == '\t' || Character == '\r' || Character == '\n' ||
           Character == ';' || Character == ','  || Character == '"'  || Character == '\'' ||
           Character == ')' || Character == ']'  || Character == '}';
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                              CONTINUANCE CONSENT OPERATIONS
//------------------------------------------------------------------------------------------------------------------------

void RegisterContinuancePrompt(ContinuanceConsentGate& Gate, uint64_t TokenFingerprint) noexcept
{
    if (TokenFingerprint == 0u)
    {
        Gate.Phase = ContinuanceConsentPhase::Declined;
        return;
    }
    Gate.TokenFingerprint = TokenFingerprint;
    Gate.ElapsedSeconds   = 0.0;
    Gate.Phase            = Gate.ExplicitOptIn
                                ? ContinuanceConsentPhase::Approved
                                : ContinuanceConsentPhase::AwaitingConsent;
}

void AdvanceContinuanceWindow(ContinuanceConsentGate& Gate, double Δt) noexcept
{
    if (Gate.Phase != ContinuanceConsentPhase::AwaitingConsent)
    {
        return;
    }
    if (Δt > 0.0)
    {
        Gate.ElapsedSeconds += Δt;
    }
    if (Gate.ElapsedSeconds >= ContinuanceApprovalTimeoutSeconds)
    {
        Gate.Phase            = ContinuanceConsentPhase::Expired;
        Gate.TokenFingerprint = 0u;
    }
}

bool ApproveContinuanceProfileCreation(ContinuanceConsentGate& Gate, uint64_t TokenFingerprint) noexcept
{
    // 🔴 Reject duplicate approvals, expired windows, cancelled prompts, or mismatched continuance tokens
    if (Gate.Phase != ContinuanceConsentPhase::AwaitingConsent ||
        TokenFingerprint == 0u ||
        Gate.TokenFingerprint != TokenFingerprint ||
        Gate.ElapsedSeconds >= ContinuanceApprovalTimeoutSeconds)
    {
        return false;
    }
    Gate.Phase = ContinuanceConsentPhase::Approved;
    return true;
}

void DeclineContinuanceProfileCreation(ContinuanceConsentGate& Gate) noexcept
{
    Gate.Phase            = ContinuanceConsentPhase::Declined;
    Gate.TokenFingerprint = 0u;
}

void ResetContinuanceGate(ContinuanceConsentGate& Gate) noexcept
{
    const bool PreserveOptIn = Gate.ExplicitOptIn;
    Gate                     = ContinuanceConsentGate{};
    Gate.ExplicitOptIn       = PreserveOptIn;
}

//------------------------------------------------------------------------------------------------------------------------
//                                           CREDENTIAL VALIDATION & REDACTION
//------------------------------------------------------------------------------------------------------------------------

bool VerifyHexStorageKeyFormat(std::string_view CandidateHexKey) noexcept
{
    if (CandidateHexKey.size() != RequiredHexStorageKeyCharacters)
    {
        return false;
    }
    for (char Character : CandidateHexKey)
    {
        const bool Digit = (Character >= '0' && Character <= '9');
        const bool Lower = (Character >= 'a' && Character <= 'f');
        const bool Upper = (Character >= 'A' && Character <= 'F');
        if (!Digit && !Lower && !Upper)
        {
            return false;
        }
    }
    return true;
}

bool VerifyJwtTokenCharacters(std::string_view CandidateJwt) noexcept
{
    if (CandidateJwt.size() < 16u || CandidateJwt.size() > 8192u)
    {
        return false;
    }
    uint32_t SegmentDotCount = 0u;
    for (char Character : CandidateJwt)
    {
        if (Character == '.')
        {
            ++SegmentDotCount;
            continue;
        }
        const bool AlphaNum = (Character >= 'A' && Character <= 'Z') ||
                              (Character >= 'a' && Character <= 'z') ||
                              (Character >= '0' && Character <= '9') ||
                              Character == '-' || Character == '_';
        if (!AlphaNum)
        {
            return false;
        }
    }
    return SegmentDotCount == 2u;
}

std::string RedactCredentialText(std::string_view RawDiagnosticText)
{
    static constexpr std::array<std::string_view, 8u> SensitiveKeyPrefixes = {{
        "token=",
        "id_token=",
        "secret=",
        "client_secret=",
        "password=",
        "storage_key=",
        "continuance=",
        "bearer ",
    }};

    std::string Sanitized;
    Sanitized.reserve(RawDiagnosticText.size());

    std::size_t Cursor = 0u;
    while (Cursor < RawDiagnosticText.size())
    {
        // ① Scrub key=secret / Bearer <token> assignments
        bool RedactedKey = false;
        for (std::string_view KeyPrefix : SensitiveKeyPrefixes)
        {
            if (MatchCaseInsensitivePrefix(RawDiagnosticText, Cursor, KeyPrefix))
            {
                Sanitized.append(RawDiagnosticText.substr(Cursor, KeyPrefix.size()));
                Sanitized.append("[REDACTED]");
                Cursor += KeyPrefix.size();
                while (Cursor < RawDiagnosticText.size() && !QuerySecretTerminator(RawDiagnosticText[Cursor]))
                {
                    ++Cursor;
                }
                RedactedKey = true;
                break;
            }
        }
        if (RedactedKey)
        {
            continue;
        }

        // ② Scrub inline URL-safe JWTs starting with "eyJ"
        if (Cursor + 12u < RawDiagnosticText.size() &&
            RawDiagnosticText[Cursor] == 'e' &&
            RawDiagnosticText[Cursor + 1u] == 'y' &&
            RawDiagnosticText[Cursor + 2u] == 'J')
        {
            std::size_t EndCursor = Cursor;
            while (EndCursor < RawDiagnosticText.size() && !QuerySecretTerminator(RawDiagnosticText[EndCursor]))
            {
                ++EndCursor;
            }
            if (VerifyJwtTokenCharacters(RawDiagnosticText.substr(Cursor, EndCursor - Cursor)))
            {
                Sanitized.append("[REDACTED_JWT]");
                Cursor = EndCursor;
                continue;
            }
        }

        Sanitized.push_back(RawDiagnosticText[Cursor]);
        ++Cursor;
    }

    return Sanitized;
}

void ZeroSensitiveBytes(std::span<char> SensitiveBuffer) noexcept
{
    volatile char* VolatileCursor = SensitiveBuffer.data();
    for (std::size_t Index = 0u; Index < SensitiveBuffer.size(); ++Index)
    {
        VolatileCursor[Index] = '\0';
    }
}

SessionRegistrationOutcome EvaluateSessionRegistrationPolicy(
    int32_t BackendResultCode,
    int32_t SuccessResultCode,
    int32_t ClientPolicyMissingResultCode)
{
    SessionRegistrationOutcome Outcome{};
    if (BackendResultCode == SuccessResultCode)
    {
        Outcome.Resolution       = SessionRegistrationResolution::FullBackendRegistration;
        Outcome.UnregisteredMode = false;
        Outcome.DiagnosticNote   = "session_registration=verified";
        return Outcome;
    }

    if (BackendResultCode == ClientPolicyMissingResultCode)
    {
        Outcome.Resolution       = SessionRegistrationResolution::LifecycleOnlyClientPolicySkip;
        Outcome.UnregisteredMode = true;
        Outcome.DiagnosticNote   = "session_registration=client_policy_restricted; continuing in lifecycle-only session mode";
        return Outcome;
    }

    Outcome.Resolution       = SessionRegistrationResolution::TerminalBackendFailure;
    Outcome.UnregisteredMode = false;
    Outcome.DiagnosticNote   = "session_registration=failed; code=" + std::to_string(BackendResultCode);
    return Outcome;
}

} // namespace Frontier::NetworkingSpace
