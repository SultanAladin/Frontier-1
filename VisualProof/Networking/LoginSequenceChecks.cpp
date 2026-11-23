//============================================================================================================================================
//                                                   LOGINSEQUENCECHECKS.CPP
//============================================================================================================================================
// 📦 Exercises login acceptance ordering only; never contacts EOS and cannot prove a player authenticated.

#include "../../Frontier/Projects/Project-Networking/Source/LoginSequence.h"
#include <cstdio>

int main()
{
    using namespace Networking;
    int Failures = 0;
    const auto Require = [&Failures](bool Accepted, const char* Explanation)
    {
        std::printf("%s %s\n", Accepted ? "PASS" : "FAIL", Explanation);
        if (!Accepted)
            ++Failures;
    };
    LoginSequence Ordered;
    Require(!Ordered.Finished(), "initial login is not success");
    Ordered.AcceptConnect(true);
    Require(Ordered.Progress == LoginProgress::WaitingForAuth, "Connect cannot bypass Auth");
    Ordered.AcceptAuth(true);
    Require(!Ordered.Finished(), "Auth alone does not finish login");
    Ordered.AcceptConnect(true);
    Require(Ordered.Progress == LoginProgress::Connected, "ordered valid completions finish login");
    LoginSequence InvalidAccount;
    InvalidAccount.AcceptAuth(false);
    InvalidAccount.AcceptConnect(true);
    Require(InvalidAccount.Progress == LoginProgress::Refused, "invalid Epic account cannot authenticate");
    LoginSequence InvalidProductUser;
    InvalidProductUser.AcceptAuth(true);
    InvalidProductUser.AcceptConnect(false);
    Require(InvalidProductUser.Progress == LoginProgress::Refused, "invalid PUID cannot authenticate");
    LoginSequence Expired;
    Expired.AcceptAuth(true);
    Expired.Refuse();
    Expired.AcceptConnect(true);
    Require(Expired.Progress == LoginProgress::Refused, "late completion cannot reverse timeout/refusal");
    LoginSequence Consent;
    Require(!Consent.ApproveCreation(), "creation cannot bypass Auth or explicit consent state");
    Consent.AcceptAuth(true);
    Consent.RequestCreationConsent();
    Consent.AcceptConnect(true);
    Require(Consent.Progress == LoginProgress::WaitingForCreationConsent && !Consent.Finished(),
        "missing product user waits for consent without restarting Auth or claiming success");
    Require(Consent.ApproveCreation(), "explicit consent resumes the existing Connect flow");
    Require(!Consent.ApproveCreation(), "duplicate creation approval is rejected");
    Consent.AcceptConnect(true);
    Consent.AcceptAuth(true);
    Consent.RequestCreationConsent();
    Require(Consent.Progress == LoginProgress::Connected, "verified login stays connected without another Auth flow");
    LoginSequence CancelledConsent;
    CancelledConsent.AcceptAuth(true);
    CancelledConsent.RequestCreationConsent();
    CancelledConsent.Refuse();
    Require(!CancelledConsent.ApproveCreation(), "cancelled or timed-out consent cannot create a product user");
    std::puts("Scope: synthetic acceptance ordering only. LIVE EOS AUTHENTICATION NOT RUN.");
    return Failures ? 1 : 0;
}
