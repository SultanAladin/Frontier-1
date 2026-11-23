#pragma once
#include <cstring>

namespace Networking
{
// Never return SDK text: it may contain credentials, account identifiers or paths.
inline unsigned ClassifyPlatformDiagnostic(const char* Message) noexcept
{
    if (!Message) return 0;
    if (std::strstr(Message, "ClientCredentials.ClientSecret")) return 1u << 0;
    if (std::strstr(Message, "ClientCredentials.ClientId")) return 1u << 1;
    if (std::strstr(Message, "Invalid product id")) return 1u << 2;
    if (std::strstr(Message, "Invalid sandbox id")) return 1u << 3;
    if (std::strstr(Message, "Invalid deployment id")) return 1u << 4;
    if (std::strstr(Message, "CacheDirectory") || std::strstr(Message, "cache directory")) return 1u << 5;
    if (std::strstr(Message, "ApiVersion") || std::strstr(Message, "API version")) return 1u << 6;
    if (std::strstr(Message, "Invalid reserved")) return 1u << 7;
    return 1u << 8;
}
inline const char* DescribePlatformDiagnostic(unsigned Bit) noexcept
{
    switch (Bit)
    {
    case 1u << 0: return "platform_error=client_secret; SDK rejected the local client-secret parameter. Replace it in Setup; do not paste it into chat.";
    case 1u << 1: return "platform_error=client_id; SDK rejected the local client-ID parameter. Check Setup > Advanced.";
    case 1u << 2: return "platform_error=product_id; SDK rejected the configured product ID. Report this configuration error.";
    case 1u << 3: return "platform_error=sandbox_id; SDK rejected the configured sandbox ID. Report this configuration error.";
    case 1u << 4: return "platform_error=deployment_id; SDK rejected the configured deployment ID. Report this configuration error.";
    case 1u << 5: return "platform_error=cache_directory; SDK reported a local cache-directory problem.";
    case 1u << 6: return "platform_error=api_version; SDK reported an incompatible API version. Extract a fresh complete package.";
    case 1u << 7: return "platform_error=reserved_field; SDK rejected reserved platform options. Report this application defect.";
    default: return "platform_error=unclassified_sdk_error; SDK diagnostic text withheld to protect credentials and identifiers.";
    }
}
}
