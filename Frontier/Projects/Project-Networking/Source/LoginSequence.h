//============================================================================================================================================
//                                                              LOGINSEQUENCE.H
//============================================================================================================================================
// 📦 Tracks Auth acceptance before Connect acceptance; terminal refusal cannot become success.

#pragma once

namespace Networking
{
enum class LoginProgress
{
    WaitingForAuth,
    WaitingForConnect,
    WaitingForCreationConsent,
    Connected,
    Refused
};

struct LoginSequence
{
    LoginProgress Progress = LoginProgress::WaitingForAuth;

    void AcceptAuth(bool ValidAccount) noexcept
    {
        if (Progress != LoginProgress::WaitingForAuth)
            return;
        Progress = ValidAccount ? LoginProgress::WaitingForConnect : LoginProgress::Refused;
    }

    void RequestCreationConsent() noexcept
    {
        if (Progress == LoginProgress::WaitingForConnect)
            Progress = LoginProgress::WaitingForCreationConsent;
    }

    bool ApproveCreation() noexcept
    {
        if (Progress != LoginProgress::WaitingForCreationConsent) return false;
        Progress = LoginProgress::WaitingForConnect;
        return true;
    }

    void AcceptConnect(bool ValidProductUser) noexcept
    {
        if (Progress != LoginProgress::WaitingForConnect)
            return;
        Progress = ValidProductUser ? LoginProgress::Connected : LoginProgress::Refused;
    }

    void Refuse() noexcept
    {
        if (Progress != LoginProgress::Connected)
            Progress = LoginProgress::Refused;
    }

    bool Finished() const noexcept
    {
        return Progress == LoginProgress::Connected || Progress == LoginProgress::Refused;
    }
};
}
