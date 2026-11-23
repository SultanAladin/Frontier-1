#pragma once
#include <eos_auth_types.h>

namespace Networking
{
// Matches Charge's required EAS application permissions shown by Epic's consent UI.
// Loading friends is optional; omitting a required scope is not a valid opt-out.
constexpr EOS_EAuthScopeFlags ChargeAuthScopes() noexcept
{
    return static_cast<EOS_EAuthScopeFlags>(
        static_cast<int>(EOS_EAuthScopeFlags::EOS_AS_BasicProfile) |
        static_cast<int>(EOS_EAuthScopeFlags::EOS_AS_FriendsList) |
        static_cast<int>(EOS_EAuthScopeFlags::EOS_AS_Presence) |
        static_cast<int>(EOS_EAuthScopeFlags::EOS_AS_Country));
}
}
