#include "../../Frontier/Projects/Project-Networking/Source/XsollaStore.h"
#include <cstdio>
#include <cstring>
#include <string>
int main()
{
    using namespace Networking;
    unsigned Failures = 0;
    auto Check = [&](bool Pass, const char* Name) { std::printf("%s %s\n", Pass ? "PASS" : "FAIL", Name); if (!Pass) ++Failures; };
    Check(XsollaCatalogCount == 6, "catalog has premium SKUs and Nitron packs");
    Check(FindXsollaSku("premium_pass_lifetime") &&
        XsollaSkuGrantsPremium(*FindXsollaSku("premium_pass_lifetime")), "lifetime pass grants premium");
    Check(FindXsollaSku("premium_monthly") && FindXsollaSku("premium_yearly") &&
        XsollaSkuGrantsPremium(*FindXsollaSku("premium_monthly")), "subscription SKUs grant premium");
    Check(FindXsollaSku("nitron_stack_100") && FindXsollaSku("nitron_stack_100")->NitronUnits == 100 &&
        XsollaNitronBaseUnits(*FindXsollaSku("nitron_stack_100")) == 100, "stack pack is 100 with no bonus");
    Check(FindXsollaSku("nitron_vault_550") && XsollaNitronBaseUnits(*FindXsollaSku("nitron_vault_550")) == 500 &&
        FindXsollaSku("nitron_vault_550")->BonusPercent == 10, "vault pack is 500+10%");
    Check(FindXsollaSku("nitron_reserve_1200") && XsollaNitronBaseUnits(*FindXsollaSku("nitron_reserve_1200")) == 1000 &&
        FindXsollaSku("nitron_reserve_1200")->BonusPercent == 20, "reserve pack is 1000+20%");
    Check(!FindXsollaSku(nullptr) && !FindXsollaSku("") && !FindXsollaSku("not_a_sku"), "unknown SKU refused");
    Check(!XsollaSkuGrantsPremium(*FindXsollaSku("nitron_stack_100")), "Nitron packs are not premium");

    char Url[4096]{};
    Check(!BuildPayStationUrl(true, nullptr, Url, sizeof(Url)) && !Url[0], "empty token refused");
    Check(!BuildPayStationUrl(true, "", Url, sizeof(Url)), "blank token refused");
    Check(!BuildPayStationUrl(true, "abc def", Url, sizeof(Url)), "token whitespace refused");
    Check(!BuildPayStationUrl(true, "abc\nxyz", Url, sizeof(Url)), "token newline refused");
    Check(!BuildPayStationUrl(true, "abc/xyz", Url, sizeof(Url)), "token slash refused");
    Check(BuildPayStationUrl(true, "f4puMEFFDZcx9nv5HoNHIkPe9qghvBQo", Url, sizeof(Url)) &&
        std::strstr(Url, XsollaPayStationSandbox) == Url &&
        std::strstr(Url, "token=f4puMEFFDZcx9nv5HoNHIkPe9qghvBQo"), "sandbox Pay Station URL");
    Check(BuildPayStationUrl(false, "f4puMEFFDZcx9nv5HoNHIkPe9qghvBQo", Url, sizeof(Url)) &&
        std::strstr(Url, XsollaPayStationLive) == Url, "live Pay Station URL");
    char Redacted[4096]{};
    Check(RedactPayStationUrl(Url, Redacted, sizeof(Redacted)) &&
        std::strstr(Redacted, "token=[redacted]") &&
        !std::strstr(Redacted, "f4puMEFFDZcx9nv5HoNHIkPe9qghvBQo"), "Pay Station token redacted for logs");
    Check(!BuildPayStationUrl(true, "f4puMEFFDZcx9nv5HoNHIkPe9qghvBQo", Url, 32) && !Url[0],
        "undersized URL buffer wiped");

    Check(!BuildVirtualItemsUrl(0, Url, sizeof(Url)), "catalog URL needs a project id");
    Check(BuildVirtualItemsUrl(12345, Url, sizeof(Url)) &&
        std::string(Url) == "https://store.xsolla.com/api/v2/project/12345/items/virtual_items",
        "virtual items URL");
    Check(BuildOrderItemUrl(12345, "nitron_stack_100", Url, sizeof(Url)) &&
        std::string(Url) == "https://store.xsolla.com/api/v2/project/12345/payment/item/nitron_stack_100",
        "order URL for a known SKU");
    Check(!BuildOrderItemUrl(12345, "not_a_sku", Url, sizeof(Url)), "order URL refuses unknown SKU");

    XsollaEntitlement Entitlement;
    Check(ParseXsollaEntitlement("{\"premium\":\"lifetime\",\"nitron\":0}", Entitlement) &&
        Entitlement.Premium == XsollaPremiumKind::Lifetime && EntitlementGrantsPremium(Entitlement) &&
        Entitlement.Nitron == 0 && !Entitlement.SubExpiresUtc[0], "lifetime entitlement");
    Check(ParseXsollaEntitlement(" { \"premium\" : \"subscription\" , \"sub_expires_utc\" : \"2026-11-07T00:00:00Z\" , \"nitron\" : 300 } ",
        Entitlement) && Entitlement.Premium == XsollaPremiumKind::Subscription &&
        EntitlementGrantsPremium(Entitlement) && Entitlement.Nitron == 300 &&
        std::string(Entitlement.SubExpiresUtc) == "2026-11-07T00:00:00Z", "subscription entitlement");
    Check(ParseXsollaEntitlement("{\"premium\":\"none\",\"nitron\":100}", Entitlement) &&
        Entitlement.Premium == XsollaPremiumKind::None && !EntitlementGrantsPremium(Entitlement) &&
        Entitlement.Nitron == 100, "none entitlement keeps Nitron");
    Check(!ParseXsollaEntitlement("", Entitlement), "empty entitlement refused");
    Check(!ParseXsollaEntitlement(std::string(XsollaMaxEntitlementLength + 1, 'x'), Entitlement),
        "oversized entitlement refused");
    Check(!ParseXsollaEntitlement("{\"premium\":\"lifetime\",\"hack\":1}", Entitlement), "unknown key refused");
    Check(!ParseXsollaEntitlement("{\"nitron\":1}", Entitlement), "missing premium refused");
    Check(!ParseXsollaEntitlement("{\"premium\":\"subscription\"}", Entitlement), "subscription needs expiry");
    Check(!ParseXsollaEntitlement("{\"premium\":\"lifetime\",\"sub_expires_utc\":\"2026-11-07T00:00:00Z\"}", Entitlement),
        "lifetime must not carry expiry");
    Check(!ParseXsollaEntitlement("{\"premium\":\"none\",\"nitron\":-1}", Entitlement), "negative Nitron refused");
    Check(!ParseXsollaEntitlement("{\"premium\":\"none\",\"nitron\":01}", Entitlement), "leading-zero Nitron refused");
    if (Failures) std::printf("FAILURES=%u\n", Failures);
    return Failures ? 1 : 0;
}
