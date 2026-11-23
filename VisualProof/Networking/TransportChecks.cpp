#include "../../Frontier/Projects/Project-Networking/Source/TransportCodec.h"
#include "../../Frontier/Projects/Project-Networking/Source/TransportRouter.h"
#include "../../Frontier/Projects/Project-Networking/Source/AppSettings.h"
#include "../../Frontier/Projects/Project-Networking/Source/Localization.h"
#include <cstdio>
#include <string>
#include <vector>
int main()
{
    using namespace Networking;
    unsigned Failures = 0;
    auto Check = [&](bool Pass, const char* Name) { std::printf("%s %s\n", Pass ? "PASS" : "FAIL", Name); if (!Pass) ++Failures; };
    std::vector<std::uint8_t> Frame;
    static constexpr char Tag[] = "frontier-photon-v1";
    Check(PacketCodec::Encode(ReplicationKind::Probe,
        reinterpret_cast<const std::uint8_t*>(Tag), sizeof(Tag) - 1, Frame), "codec encodes probe");
    ReplicationKind Kind = ReplicationKind::Input;
    const std::uint8_t* Payload = nullptr;
    std::size_t PayloadLength = 0;
    Check(PacketCodec::Decode(Frame.data(), Frame.size(), Kind, Payload, PayloadLength) &&
        Kind == ReplicationKind::Probe && PayloadLength == sizeof(Tag) - 1, "codec round trip preserves kind and length");
    const std::vector<std::uint8_t> Oversized(PacketCodec::MaxPayload + 1, 0);
    Check(!PacketCodec::Encode(ReplicationKind::Snapshot, Oversized.data(), Oversized.size(), Frame),
        "oversized payload refused");
    Frame.resize(PacketCodec::HeaderLength + 4);
    Frame[0] = 'F'; Frame[1] = 'P'; Frame[2] = '1'; Frame[3] = PacketCodec::WireVersion;
    Frame[4] = static_cast<std::uint8_t>(ReplicationKind::Snapshot); Frame[5] = 0; Frame[6] = 4;
    Check(PacketCodec::Decode(Frame.data(), Frame.size(), Kind, Payload, PayloadLength) &&
        Kind == ReplicationKind::Snapshot && PayloadLength == 4, "snapshot frame decodes");
    Frame[6] = 5;
    Check(!PacketCodec::Decode(Frame.data(), Frame.size(), Kind, Payload, PayloadLength),
        "length mismatch refused");
    Frame[6] = 4; Frame[3] = 99;
    Check(!PacketCodec::Decode(Frame.data(), Frame.size(), Kind, Payload, PayloadLength),
        "unknown wire version refused");
    Frame[3] = PacketCodec::WireVersion; Frame[4] = 99;
    Check(!PacketCodec::Decode(Frame.data(), Frame.size(), Kind, Payload, PayloadLength),
        "unknown kind refused");
    Check(SelectTransportKind(true, true) == TransportKind::Photon, "premium plus linked selects Photon");
    Check(SelectTransportKind(false, true) == TransportKind::Eos, "standard stays on EOS");
    Check(SelectTransportKind(true, false) == TransportKind::Eos, "unlinked falls back to EOS");
    Check(SelectTransportKind(false, false) == TransportKind::Eos, "standard unlinked stays on EOS");
    AppSettings Settings;
    Check(ParseAppSettings("version=1\nlanguage=zh\nauto_login=1\nforget_persistent=0\npremium_multiplayer=1\n", Settings) &&
        Settings.PremiumMultiplayer && Settings.Language == AppLanguage::ChineseSimplified,
        "settings parse premium toggle");
    Check(ParseAppSettings("version=1\nlanguage=en\nauto_login=0\n", Settings) && !Settings.PremiumMultiplayer,
        "legacy settings default premium off");
    Check(EncodeAppSettings(Settings).find("premium_multiplayer=0") != std::string::npos,
        "settings encode keeps premium toggle");
    SetAppLanguage(AppLanguage::ChineseSimplified);
    Check(std::string(T("Premium multiplayer (Photon)")) == "高级多人联机（Photon）", "premium toggle translated");
    Check(std::string(T("Transport: %s")) == "传输：%s", "transport label translated");
    Check(std::string(T("Photon connecting")) == "Photon 连接中", "photon state translated");
    Check(std::string(T("EOS only")) == "仅 EOS", "eos state translated");
    SetAppLanguage(AppLanguage::English);
    Check(std::string(T("Photon unavailable")) == "Photon unavailable", "english display names unchanged");
    if (Failures) std::printf("FAILURES=%u\n", Failures);
    return Failures ? 1 : 0;
}
