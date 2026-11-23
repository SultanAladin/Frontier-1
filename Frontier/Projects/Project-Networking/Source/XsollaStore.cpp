//============================================================================================================================================
//                                                             XSOLLASTORE.CPP
//============================================================================================================================================
// 📦 Reads public Xsolla client config from the environment. Merchant credentials are detected
// only so they can be refused — they are never copied, logged, or sent.

#include "XsollaStore.h"
#include <cstdlib>

namespace Networking
{
namespace
{
unsigned ParseProjectId(const char* Text) noexcept
{
    if (!Text || !*Text)
        return 0;
    unsigned Acc = 0;
    for (const char* Cursor = Text; *Cursor; ++Cursor)
    {
        if (*Cursor < '0' || *Cursor > '9')
            return 0;
        const unsigned Digit = static_cast<unsigned>(*Cursor - '0');
        if (Acc > (4294967295u - Digit) / 10u)
            return 0;
        Acc = Acc * 10u + Digit;
    }
    return Acc;
}
}

XsollaClientConfig InspectXsollaConfig() noexcept
{
    XsollaClientConfig Config;
    Config.ProjectId = ParseProjectId(std::getenv("XSOLLA_PROJECT_ID"));
    if (const char* Sandbox = std::getenv("XSOLLA_SANDBOX"))
        Config.Sandbox = std::strcmp(Sandbox, "0") != 0;
    const char* ApiKey = std::getenv("XSOLLA_API_KEY");
    const char* Merchant = std::getenv("XSOLLA_MERCHANT_ID");
    Config.MerchantSecretOnClient = (ApiKey && *ApiKey) || (Merchant && *Merchant);
    Config.Configured = Config.ProjectId != 0 && !Config.MerchantSecretOnClient;
    return Config;
}

void DescribeXsollaConfig(char* Out, std::size_t Capacity) noexcept
{
    if (!Out || Capacity < 8)
        return;
    const XsollaClientConfig Config = InspectXsollaConfig();
    std::snprintf(Out, Capacity,
        "xsolla_sdk=rest binaries=none cpp_zip=unpublished project=%u sandbox=%d configured=%d merchant_secret=%s catalog=%u",
        Config.ProjectId, Config.Sandbox ? 1 : 0, Config.Configured ? 1 : 0,
        Config.MerchantSecretOnClient ? "refused_on_client" : "absent", XsollaCatalogCount);
}
}
