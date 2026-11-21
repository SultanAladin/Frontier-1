/*============================================================================================================================================
                                                             SOCKETTRANSPORT.CPP
============================================================================================================================================*/
// 🧩 Non-blocking BSD/Winsock2 UDP socket, RFC 5389 STUN reflexive discovery, RFC 8439 ChaCha20-Poly1305 AEAD, and zero-key relay

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/NetworkingSpace/Transports/SocketTransport.h"
#include "Engine/NetworkingSpace/Policies/AuthenticationScopePolicy.h"

#include <algorithm>
#include <cstring>

#if defined(_WIN32)
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #include <winsock2.h>
    #include <ws2tcpip.h>
#else
    #include <arpa/inet.h>
    #include <errno.h>
    #include <fcntl.h>
    #include <netinet/in.h>
    #include <sys/socket.h>
    #include <unistd.h>
#endif

namespace Frontier::NetworkingSpace
{

namespace
{

//------------------------------------------------------------------------------------------------------------------------
//                                        RFC 8439 CHACHA20 & POLY1305 PRIMITIVES
//------------------------------------------------------------------------------------------------------------------------

[[nodiscard]] constexpr uint32_t RotateLeft32(uint32_t Word, uint32_t Shift) noexcept
{
    return (Word << Shift) | (Word >> (32u - Shift));
}

[[nodiscard]] uint32_t LoadLittleEndian32(const uint8_t* Bytes) noexcept
{
    return static_cast<uint32_t>(Bytes[0]) |
           (static_cast<uint32_t>(Bytes[1]) << 8u) |
           (static_cast<uint32_t>(Bytes[2]) << 16u) |
           (static_cast<uint32_t>(Bytes[3]) << 24u);
}

void WriteLittleEndian32(uint8_t* Destination, uint32_t Word) noexcept
{
    Destination[0] = static_cast<uint8_t>(Word & 0xFFu);
    Destination[1] = static_cast<uint8_t>((Word >> 8u) & 0xFFu);
    Destination[2] = static_cast<uint8_t>((Word >> 16u) & 0xFFu);
    Destination[3] = static_cast<uint8_t>((Word >> 24u) & 0xFFu);
}

constexpr void ChaChaQuarterRound(uint32_t& A, uint32_t& B, uint32_t& C, uint32_t& D) noexcept
{
    A += B; D ^= A; D = RotateLeft32(D, 16u);
    C += D; B ^= C; B = RotateLeft32(B, 12u);
    A += B; D ^= A; D = RotateLeft32(D, 8u);
    C += D; B ^= C; B = RotateLeft32(B, 7u);
}

void ComputeChaCha20Block(
    std::span<const uint8_t, ChaCha20KeyByteCount>   Key,
    uint32_t                                         BlockCounter,
    std::span<const uint8_t, ChaCha20NonceByteCount> Nonce,
    std::array<uint8_t, 64u>&                        OutKeystreamBlock) noexcept
{
    std::array<uint32_t, 16u> Initial = {{
        0x61707865u, 0x3320646Eu, 0x79622D32u, 0x6B206574u,
        LoadLittleEndian32(Key.data() + 0u),
        LoadLittleEndian32(Key.data() + 4u),
        LoadLittleEndian32(Key.data() + 8u),
        LoadLittleEndian32(Key.data() + 12u),
        LoadLittleEndian32(Key.data() + 16u),
        LoadLittleEndian32(Key.data() + 20u),
        LoadLittleEndian32(Key.data() + 24u),
        LoadLittleEndian32(Key.data() + 28u),
        BlockCounter,
        LoadLittleEndian32(Nonce.data() + 0u),
        LoadLittleEndian32(Nonce.data() + 4u),
        LoadLittleEndian32(Nonce.data() + 8u),
    }};

    std::array<uint32_t, 16u> Working = Initial;
    for (uint32_t DoubleRound = 0u; DoubleRound < 10u; ++DoubleRound)
    {
        // Column rounds
        ChaChaQuarterRound(Working[0], Working[4], Working[8],  Working[12]);
        ChaChaQuarterRound(Working[1], Working[5], Working[9],  Working[13]);
        ChaChaQuarterRound(Working[2], Working[6], Working[10], Working[14]);
        ChaChaQuarterRound(Working[3], Working[7], Working[11], Working[15]);
        // Diagonal rounds
        ChaChaQuarterRound(Working[0], Working[5], Working[10], Working[15]);
        ChaChaQuarterRound(Working[1], Working[6], Working[11], Working[12]);
        ChaChaQuarterRound(Working[2], Working[7], Working[8],  Working[13]);
        ChaChaQuarterRound(Working[3], Working[4], Working[9],  Working[14]);
    }

    for (std::size_t Index = 0u; Index < 16u; ++Index)
    {
        WriteLittleEndian32(OutKeystreamBlock.data() + Index * 4u, Working[Index] + Initial[Index]);
    }
}

void ApplyChaCha20StreamXor(
    std::span<const uint8_t, ChaCha20KeyByteCount>   Key,
    uint32_t                                         InitialCounter,
    std::span<const uint8_t, ChaCha20NonceByteCount> Nonce,
    std::span<const uint8_t>                         InputBytes,
    std::span<uint8_t>                               OutputBytes) noexcept
{
    std::array<uint8_t, 64u> Keystream{};
    uint32_t Counter   = InitialCounter;
    std::size_t Offset = 0u;

    while (Offset < InputBytes.size())
    {
        ComputeChaCha20Block(Key, Counter++, Nonce, Keystream);
        const std::size_t Chunk = std::min<std::size_t>(64u, InputBytes.size() - Offset);
        for (std::size_t ByteIdx = 0u; ByteIdx < Chunk; ++ByteIdx)
        {
            OutputBytes[Offset + ByteIdx] = static_cast<uint8_t>(InputBytes[Offset + ByteIdx] ^ Keystream[ByteIdx]);
        }
        Offset += Chunk;
    }
}

// Exact 26-bit limb Poly1305 authenticator (RFC 8439 Section 2.5)
void ComputePoly1305Tag(
    std::span<const uint8_t, 32u>            OneTimeKey,
    std::span<const uint8_t>                 MessageBytes,
    std::array<uint8_t, Poly1305TagByteCount>& OutTag) noexcept
{
    const uint32_t R0 = (LoadLittleEndian32(OneTimeKey.data() + 0u)) & 0x3FFFFFFu;
    const uint32_t R1 = (LoadLittleEndian32(OneTimeKey.data() + 3u) >> 2u) & 0x3FFFF03u;
    const uint32_t R2 = (LoadLittleEndian32(OneTimeKey.data() + 6u) >> 4u) & 0x3FFC0FFu;
    const uint32_t R3 = (LoadLittleEndian32(OneTimeKey.data() + 9u) >> 6u) & 0x3F03FFFu;
    const uint32_t R4 = (LoadLittleEndian32(OneTimeKey.data() + 12u) >> 8u) & 0x00FFFFFu;

    const uint32_t S1 = R1 * 5u;
    const uint32_t S2 = R2 * 5u;
    const uint32_t S3 = R3 * 5u;
    const uint32_t S4 = R4 * 5u;

    uint32_t H0 = 0u;
    uint32_t H1 = 0u;
    uint32_t H2 = 0u;
    uint32_t H3 = 0u;
    uint32_t H4 = 0u;

    std::size_t Offset = 0u;
    while (Offset < MessageBytes.size())
    {
        std::array<uint8_t, 17u> Block{};
        const std::size_t Chunk = std::min<std::size_t>(16u, MessageBytes.size() - Offset);
        std::memcpy(Block.data(), MessageBytes.data() + Offset, Chunk);
        Block[Chunk] = 1u;

        const uint32_t T0 = LoadLittleEndian32(Block.data() + 0u);
        const uint32_t T1 = LoadLittleEndian32(Block.data() + 4u);
        const uint32_t T2 = LoadLittleEndian32(Block.data() + 8u);
        const uint32_t T3 = LoadLittleEndian32(Block.data() + 12u);
        const uint32_t HiBit = (Chunk == 16u) ? (1u << 24u) : 0u;

        H0 += T0 & 0x3FFFFFFu;
        H1 += ((T0 >> 26u) | (T1 << 6u)) & 0x3FFFFFFu;
        H2 += ((T1 >> 20u) | (T2 << 12u)) & 0x3FFFFFFu;
        H3 += ((T2 >> 14u) | (T3 << 18u)) & 0x3FFFFFFu;
        H4 += (T3 >> 8u) | HiBit;

        const uint64_t D0 = static_cast<uint64_t>(H0) * R0 + static_cast<uint64_t>(H1) * S4 +
                            static_cast<uint64_t>(H2) * S3 + static_cast<uint64_t>(H3) * S2 +
                            static_cast<uint64_t>(H4) * S1;
        uint64_t D1       = static_cast<uint64_t>(H0) * R1 + static_cast<uint64_t>(H1) * R0 +
                            static_cast<uint64_t>(H2) * S4 + static_cast<uint64_t>(H3) * S3 +
                            static_cast<uint64_t>(H4) * S2;
        uint64_t D2       = static_cast<uint64_t>(H0) * R2 + static_cast<uint64_t>(H1) * R1 +
                            static_cast<uint64_t>(H2) * R0 + static_cast<uint64_t>(H3) * S4 +
                            static_cast<uint64_t>(H4) * S3;
        uint64_t D3       = static_cast<uint64_t>(H0) * R3 + static_cast<uint64_t>(H1) * R2 +
                            static_cast<uint64_t>(H2) * R1 + static_cast<uint64_t>(H3) * R0 +
                            static_cast<uint64_t>(H4) * S4;
        uint64_t D4       = static_cast<uint64_t>(H0) * R4 + static_cast<uint64_t>(H1) * R3 +
                            static_cast<uint64_t>(H2) * R2 + static_cast<uint64_t>(H3) * R1 +
                            static_cast<uint64_t>(H4) * R0;

        uint32_t Carry = static_cast<uint32_t>(D0 >> 26u); H0 = static_cast<uint32_t>(D0) & 0x3FFFFFFu;
        D1 += Carry;   Carry = static_cast<uint32_t>(D1 >> 26u); H1 = static_cast<uint32_t>(D1) & 0x3FFFFFFu;
        D2 += Carry;   Carry = static_cast<uint32_t>(D2 >> 26u); H2 = static_cast<uint32_t>(D2) & 0x3FFFFFFu;
        D3 += Carry;   Carry = static_cast<uint32_t>(D3 >> 26u); H3 = static_cast<uint32_t>(D3) & 0x3FFFFFFu;
        D4 += Carry;   Carry = static_cast<uint32_t>(D4 >> 26u); H4 = static_cast<uint32_t>(D4) & 0x3FFFFFFu;
        H0 += Carry * 5u; Carry = H0 >> 26u; H0 &= 0x3FFFFFFu;
        H1 += Carry;

        Offset += Chunk;
    }

    uint32_t Carry = H1 >> 26u; H1 &= 0x3FFFFFFu;
    H2 += Carry; Carry = H2 >> 26u; H2 &= 0x3FFFFFFu;
    H3 += Carry; Carry = H3 >> 26u; H3 &= 0x3FFFFFFu;
    H4 += Carry; Carry = H4 >> 26u; H4 &= 0x3FFFFFFu;
    H0 += Carry * 5u; Carry = H0 >> 26u; H0 &= 0x3FFFFFFu;
    H1 += Carry;

    uint32_t G0 = H0 + 5u; Carry = G0 >> 26u; G0 &= 0x3FFFFFFu;
    uint32_t G1 = H1 + Carry; Carry = G1 >> 26u; G1 &= 0x3FFFFFFu;
    uint32_t G2 = H2 + Carry; Carry = G2 >> 26u; G2 &= 0x3FFFFFFu;
    uint32_t G3 = H3 + Carry; Carry = G3 >> 26u; G3 &= 0x3FFFFFFu;
    uint32_t G4 = H4 + Carry - (1u << 26u);

    const uint32_t Mask = (G4 >> 31u) - 1u;
    G0 &= Mask; G1 &= Mask; G2 &= Mask; G3 &= Mask; G4 &= Mask;
    const uint32_t InvMask = ~Mask;
    H0 = (H0 & InvMask) | G0;
    H1 = (H1 & InvMask) | G1;
    H2 = (H2 & InvMask) | G2;
    H3 = (H3 & InvMask) | G3;
    H4 = (H4 & InvMask) | G4;

    const uint64_t Word0 = static_cast<uint64_t>(H0 | (H1 << 26u)) + LoadLittleEndian32(OneTimeKey.data() + 16u);
    const uint64_t Word1 = static_cast<uint64_t>((H1 >> 6u) | (H2 << 20u)) + LoadLittleEndian32(OneTimeKey.data() + 20u) + (Word0 >> 32u);
    const uint64_t Word2 = static_cast<uint64_t>((H2 >> 12u) | (H3 << 14u)) + LoadLittleEndian32(OneTimeKey.data() + 24u) + (Word1 >> 32u);
    const uint64_t Word3 = static_cast<uint64_t>((H3 >> 18u) | (H4 << 8u)) + LoadLittleEndian32(OneTimeKey.data() + 28u) + (Word2 >> 32u);

    WriteLittleEndian32(OutTag.data() + 0u,  static_cast<uint32_t>(Word0));
    WriteLittleEndian32(OutTag.data() + 4u,  static_cast<uint32_t>(Word1));
    WriteLittleEndian32(OutTag.data() + 8u,  static_cast<uint32_t>(Word2));
    WriteLittleEndian32(OutTag.data() + 12u, static_cast<uint32_t>(Word3));
}

void BuildAeadMacBuffer(
    std::span<const uint8_t> AssociatedHeader,
    std::span<const uint8_t> Ciphertext,
    std::vector<uint8_t>&    OutMacInput)
{
    const std::size_t PadAad    = (16u - (AssociatedHeader.size() % 16u)) % 16u;
    const std::size_t PadCipher = (16u - (Ciphertext.size() % 16u)) % 16u;
    OutMacInput.clear();
    OutMacInput.reserve(AssociatedHeader.size() + PadAad + Ciphertext.size() + PadCipher + 16u);

    OutMacInput.insert(OutMacInput.end(), AssociatedHeader.begin(), AssociatedHeader.end());
    OutMacInput.insert(OutMacInput.end(), PadAad, 0u);
    OutMacInput.insert(OutMacInput.end(), Ciphertext.begin(), Ciphertext.end());
    OutMacInput.insert(OutMacInput.end(), PadCipher, 0u);

    const uint64_t AadLen    = AssociatedHeader.size();
    const uint64_t CipherLen = Ciphertext.size();
    for (uint32_t Shift = 0u; Shift < 64u; Shift += 8u) OutMacInput.push_back(static_cast<uint8_t>((AadLen >> Shift) & 0xFFu));
    for (uint32_t Shift = 0u; Shift < 64u; Shift += 8u) OutMacInput.push_back(static_cast<uint8_t>((CipherLen >> Shift) & 0xFFu));
}

#if defined(_WIN32)
void EnsureWinsockStarted() noexcept
{
    static bool Initialized = false;
    if (!Initialized)
    {
        WSADATA WsaStartupRecord{};
        if (WSAStartup(MAKEWORD(2, 2), &WsaStartupRecord) == 0)
        {
            Initialized = true;
        }
    }
}
#endif

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                           ADDRESS & RFC 5389 STUN OPERATIONS
//------------------------------------------------------------------------------------------------------------------------

bool ParseIpv4Literal(std::string_view DotDecimalText, std::array<uint8_t, 4u>& OutOctets) noexcept
{
    std::array<uint32_t, 4u> Parts{ 0u, 0u, 0u, 0u };
    std::size_t PartIndex      = 0u;
    bool DigitPresentInOctet   = false;

    for (char Character : DotDecimalText)
    {
        if (Character == '.')
        {
            if (!DigitPresentInOctet || PartIndex >= 3u)
            {
                return false;
            }
            ++PartIndex;
            DigitPresentInOctet = false;
            continue;
        }
        if (Character < '0' || Character > '9')
        {
            return false;
        }
        DigitPresentInOctet = true;
        Parts[PartIndex]    = Parts[PartIndex] * 10u + static_cast<uint32_t>(Character - '0');
        if (Parts[PartIndex] > 255u)
        {
            return false;
        }
    }

    if (!DigitPresentInOctet || PartIndex != 3u)
    {
        return false;
    }
    for (std::size_t Index = 0u; Index < 4u; ++Index)
    {
        OutOctets[Index] = static_cast<uint8_t>(Parts[Index]);
    }
    return true;
}

std::string FormatIpv4Endpoint(const IpSocketAddress& Address)
{
    return std::to_string(Address.Ipv4Octets[0]) + "." +
           std::to_string(Address.Ipv4Octets[1]) + "." +
           std::to_string(Address.Ipv4Octets[2]) + "." +
           std::to_string(Address.Ipv4Octets[3]) + ":" +
           std::to_string(Address.Port);
}

void BuildStunReflexiveRequest(
    std::span<const uint8_t, 12u> TransactionIdentifier,
    std::vector<uint8_t>&         OutStunPacket)
{
    // 20-byte RFC 5389 Reflexive Request: Type=0x0001, Length=0x0000, MagicCookie=0x2112A442, TransactionId[12]
    OutStunPacket.assign(20u, 0u);
    OutStunPacket[0] = 0x00u;
    OutStunPacket[1] = 0x01u;
    OutStunPacket[2] = 0x00u;
    OutStunPacket[3] = 0x00u;
    OutStunPacket[4] = 0x21u;
    OutStunPacket[5] = 0x12u;
    OutStunPacket[6] = 0xA4u;
    OutStunPacket[7] = 0x42u;
    std::memcpy(OutStunPacket.data() + 8u, TransactionIdentifier.data(), 12u);
}

void BuildStunReflexiveResponse(
    std::span<const uint8_t, 12u> TransactionIdentifier,
    const IpSocketAddress&        ObservedReflexiveAddress,
    std::vector<uint8_t>&         OutStunPacket)
{
    // 32-byte RFC 5389 Reflexive Success Response (0x0101) + 12-byte XOR-MAPPED-ADDRESS (0x0020)
    OutStunPacket.assign(32u, 0u);
    OutStunPacket[0] = 0x01u;
    OutStunPacket[1] = 0x01u;
    OutStunPacket[2] = 0x00u;
    OutStunPacket[3] = 0x0Cu; // 12-byte attribute body
    OutStunPacket[4] = 0x21u;
    OutStunPacket[5] = 0x12u;
    OutStunPacket[6] = 0xA4u;
    OutStunPacket[7] = 0x42u;
    std::memcpy(OutStunPacket.data() + 8u, TransactionIdentifier.data(), 12u);

    // Attribute Type = 0x0020 (XOR-MAPPED-ADDRESS), Length = 0x0008, Reserved=0x00, Family=0x01 (IPv4)
    OutStunPacket[20] = 0x00u;
    OutStunPacket[21] = 0x20u;
    OutStunPacket[22] = 0x00u;
    OutStunPacket[23] = 0x08u;
    OutStunPacket[24] = 0x00u;
    OutStunPacket[25] = 0x01u;

    const uint16_t XorPort = static_cast<uint16_t>(ObservedReflexiveAddress.Port ^ static_cast<uint16_t>(StunMagicCookieWord >> 16u));
    OutStunPacket[26] = static_cast<uint8_t>((XorPort >> 8u) & 0xFFu);
    OutStunPacket[27] = static_cast<uint8_t>(XorPort & 0xFFu);

    const std::array<uint8_t, 4u> CookieOctets = { 0x21u, 0x12u, 0xA4u, 0x42u };
    for (std::size_t Index = 0u; Index < 4u; ++Index)
    {
        OutStunPacket[28u + Index] = static_cast<uint8_t>(ObservedReflexiveAddress.Ipv4Octets[Index] ^ CookieOctets[Index]);
    }
}

bool DecodeStunReflexiveResponse(
    std::span<const uint8_t>      StunPacket,
    std::span<const uint8_t, 12u> ExpectedTransactionIdentifier,
    IpSocketAddress&              OutReflexiveAddress) noexcept
{
    if (StunPacket.size() < 20u ||
        StunPacket[0] != 0x01u || StunPacket[1] != 0x01u ||
        StunPacket[4] != 0x21u || StunPacket[5] != 0x12u ||
        StunPacket[6] != 0xA4u || StunPacket[7] != 0x42u)
    {
        return false;
    }

    if (std::memcmp(StunPacket.data() + 8u, ExpectedTransactionIdentifier.data(), 12u) != 0)
    {
        return false;
    }

    const uint16_t MessageBodyBytes = static_cast<uint16_t>((static_cast<uint16_t>(StunPacket[2]) << 8u) | StunPacket[3]);
    if (20u + static_cast<std::size_t>(MessageBodyBytes) > StunPacket.size())
    {
        return false;
    }

    std::size_t Cursor          = 20u;
    const std::size_t LimitByte = 20u + MessageBodyBytes;
    while (Cursor + 4u <= LimitByte)
    {
        const uint16_t AttrType   = static_cast<uint16_t>((static_cast<uint16_t>(StunPacket[Cursor]) << 8u) | StunPacket[Cursor + 1u]);
        const uint16_t AttrLength = static_cast<uint16_t>((static_cast<uint16_t>(StunPacket[Cursor + 2u]) << 8u) | StunPacket[Cursor + 3u]);
        Cursor += 4u;
        if (Cursor + AttrLength > LimitByte)
        {
            return false;
        }

        // 0x0020 = XOR-MAPPED-ADDRESS (IPv4 family = 0x01)
        if (AttrType == 0x0020u && AttrLength >= 8u && StunPacket[Cursor + 1u] == 0x01u)
        {
            const uint16_t XorPort = static_cast<uint16_t>((static_cast<uint16_t>(StunPacket[Cursor + 2u]) << 8u) | StunPacket[Cursor + 3u]);
            OutReflexiveAddress.Port = static_cast<uint16_t>(XorPort ^ static_cast<uint16_t>(StunMagicCookieWord >> 16u));

            const std::array<uint8_t, 4u> CookieOctets = { 0x21u, 0x12u, 0xA4u, 0x42u };
            for (std::size_t Index = 0u; Index < 4u; ++Index)
            {
                OutReflexiveAddress.Ipv4Octets[Index] = static_cast<uint8_t>(StunPacket[Cursor + 4u + Index] ^ CookieOctets[Index]);
            }
            return true;
        }

        const std::size_t PaddedAdvance = (static_cast<std::size_t>(AttrLength) + 3u) & ~std::size_t(3u);
        Cursor += PaddedAdvance;
    }

    return false;
}

//------------------------------------------------------------------------------------------------------------------------
//                                      CHACHA20-POLY1305 AEAD & STATELESS RELAY
//------------------------------------------------------------------------------------------------------------------------

void EncryptChaCha20Poly1305(
    std::span<const uint8_t, ChaCha20KeyByteCount>   SymmetricKey,
    std::span<const uint8_t, ChaCha20NonceByteCount> Nonce,
    std::span<const uint8_t>                         AssociatedHeader,
    std::span<const uint8_t>                         Plaintext,
    std::vector<uint8_t>&                            OutCiphertextWithTag)
{
    std::array<uint8_t, 64u> PolyKeyBlock{};
    ComputeChaCha20Block(SymmetricKey, 0u, Nonce, PolyKeyBlock);

    OutCiphertextWithTag.assign(Plaintext.size() + Poly1305TagByteCount, 0u);
    if (!Plaintext.empty())
    {
        ApplyChaCha20StreamXor(
            SymmetricKey,
            1u,
            Nonce,
            Plaintext,
            std::span<uint8_t>(OutCiphertextWithTag.data(), Plaintext.size()));
    }

    std::vector<uint8_t> MacInput;
    BuildAeadMacBuffer(
        AssociatedHeader,
        std::span<const uint8_t>(OutCiphertextWithTag.data(), Plaintext.size()),
        MacInput);

    std::array<uint8_t, Poly1305TagByteCount> Tag{};
    ComputePoly1305Tag(std::span<const uint8_t, 32u>(PolyKeyBlock.data(), 32u), MacInput, Tag);
    std::memcpy(OutCiphertextWithTag.data() + Plaintext.size(), Tag.data(), Poly1305TagByteCount);

    ZeroSensitiveBytes(std::span<char>(reinterpret_cast<char*>(PolyKeyBlock.data()), PolyKeyBlock.size()));
}

bool DecryptChaCha20Poly1305(
    std::span<const uint8_t, ChaCha20KeyByteCount>   SymmetricKey,
    std::span<const uint8_t, ChaCha20NonceByteCount> Nonce,
    std::span<const uint8_t>                         AssociatedHeader,
    std::span<const uint8_t>                         CiphertextWithTag,
    std::vector<uint8_t>&                            OutPlaintext)
{
    if (CiphertextWithTag.size() < Poly1305TagByteCount)
    {
        return false;
    }

    const std::size_t CipherBytes = CiphertextWithTag.size() - Poly1305TagByteCount;
    const std::span<const uint8_t> CipherSlice = CiphertextWithTag.subspan(0u, CipherBytes);
    const std::span<const uint8_t> ReceivedTag = CiphertextWithTag.subspan(CipherBytes, Poly1305TagByteCount);

    std::array<uint8_t, 64u> PolyKeyBlock{};
    ComputeChaCha20Block(SymmetricKey, 0u, Nonce, PolyKeyBlock);

    std::vector<uint8_t> MacInput;
    BuildAeadMacBuffer(AssociatedHeader, CipherSlice, MacInput);

    std::array<uint8_t, Poly1305TagByteCount> ExpectedTag{};
    ComputePoly1305Tag(std::span<const uint8_t, 32u>(PolyKeyBlock.data(), 32u), MacInput, ExpectedTag);
    ZeroSensitiveBytes(std::span<char>(reinterpret_cast<char*>(PolyKeyBlock.data()), PolyKeyBlock.size()));

    // Constant-time 16-byte authentication tag comparison
    uint8_t DiffAccumulator = 0u;
    for (std::size_t Index = 0u; Index < Poly1305TagByteCount; ++Index)
    {
        DiffAccumulator |= static_cast<uint8_t>(ExpectedTag[Index] ^ ReceivedTag[Index]);
    }
    if (DiffAccumulator != 0u)
    {
        OutPlaintext.clear();
        return false;
    }

    OutPlaintext.assign(CipherBytes, 0u);
    if (CipherBytes > 0u)
    {
        ApplyChaCha20StreamXor(SymmetricKey, 1u, Nonce, CipherSlice, OutPlaintext);
    }
    return true;
}

void WrapRelayEnvelope(
    uint32_t                 SessionIdentifier,
    uint32_t                 TargetEndpointIdentifier,
    std::span<const uint8_t> EncryptedPayload,
    std::vector<uint8_t>&    OutRelayDatagram)
{
    OutRelayDatagram.assign(RelayRoutingPrefixByteCount + EncryptedPayload.size(), 0u);
    OutRelayDatagram[0] = static_cast<uint8_t>('F');
    OutRelayDatagram[1] = static_cast<uint8_t>('R');
    OutRelayDatagram[2] = static_cast<uint8_t>('L');
    OutRelayDatagram[3] = static_cast<uint8_t>('Y');
    WriteLittleEndian32(OutRelayDatagram.data() + 4u, SessionIdentifier);
    WriteLittleEndian32(OutRelayDatagram.data() + 8u, TargetEndpointIdentifier);
    if (!EncryptedPayload.empty())
    {
        std::memcpy(OutRelayDatagram.data() + RelayRoutingPrefixByteCount, EncryptedPayload.data(), EncryptedPayload.size());
    }
}

bool InspectRelayEnvelopeDestination(
    std::span<const uint8_t>                  InboundDatagram,
    std::span<const StatelessRelayRouteEntry> RoutingEntries,
    IpSocketAddress&                          OutForwardAddress,
    std::span<const uint8_t>&                 OutOpaquePayload) noexcept
{
    if (InboundDatagram.size() <= RelayRoutingPrefixByteCount ||
        InboundDatagram[0] != static_cast<uint8_t>('F') ||
        InboundDatagram[1] != static_cast<uint8_t>('R') ||
        InboundDatagram[2] != static_cast<uint8_t>('L') ||
        InboundDatagram[3] != static_cast<uint8_t>('Y'))
    {
        return false;
    }

    const uint32_t SessionId  = LoadLittleEndian32(InboundDatagram.data() + 4u);
    const uint32_t TargetPeer = LoadLittleEndian32(InboundDatagram.data() + 8u);

    for (const StatelessRelayRouteEntry& Route : RoutingEntries)
    {
        if (Route.SessionIdentifier == SessionId && Route.EndpointIdentifier == TargetPeer)
        {
            OutForwardAddress = Route.PeerAddress;
            OutOpaquePayload  = InboundDatagram.subspan(RelayRoutingPrefixByteCount);
            return true;
        }
    }
    return false;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                 SOCKET TRANSPORT CLASS
//------------------------------------------------------------------------------------------------------------------------

SocketTransport::~SocketTransport() noexcept
{
    CloseChannel();
    DisableCipherProtection();
}

SocketTransport::SocketTransport(SocketTransport&& Other) noexcept
    : SocketDescriptor(Other.SocketDescriptor)
    , RequestedPortHostOrder(Other.RequestedPortHostOrder)
    , BoundPortHostOrder(Other.BoundPortHostOrder)
    , LocalEndpointIdentifier(Other.LocalEndpointIdentifier)
    , ActiveSessionIdentifier(Other.ActiveSessionIdentifier)
    , OutboundCipherCounter(Other.OutboundCipherCounter)
    , LocalClockSeconds(Other.LocalClockSeconds)
    , CipherEnabled(Other.CipherEnabled)
    , CipherKey(Other.CipherKey)
    , PeerRoutes(std::move(Other.PeerRoutes))
{
    Other.SocketDescriptor   = -1;
    Other.BoundPortHostOrder = 0u;
    Other.DisableCipherProtection();
}

SocketTransport& SocketTransport::operator=(SocketTransport&& Other) noexcept
{
    if (this != &Other)
    {
        CloseChannel();
        DisableCipherProtection();
        SocketDescriptor        = Other.SocketDescriptor;
        RequestedPortHostOrder  = Other.RequestedPortHostOrder;
        BoundPortHostOrder      = Other.BoundPortHostOrder;
        LocalEndpointIdentifier = Other.LocalEndpointIdentifier;
        ActiveSessionIdentifier = Other.ActiveSessionIdentifier;
        OutboundCipherCounter   = Other.OutboundCipherCounter;
        LocalClockSeconds       = Other.LocalClockSeconds;
        CipherEnabled           = Other.CipherEnabled;
        CipherKey               = Other.CipherKey;
        PeerRoutes              = std::move(Other.PeerRoutes);

        Other.SocketDescriptor   = -1;
        Other.BoundPortHostOrder = 0u;
        Other.DisableCipherProtection();
    }
    return *this;
}

void SocketTransport::ConfigureBindPort(uint16_t RequestedPort, uint32_t LocalEndpoint) noexcept
{
    RequestedPortHostOrder  = RequestedPort;
    LocalEndpointIdentifier = (LocalEndpoint != 0u) ? LocalEndpoint : 1u;
}

void SocketTransport::ConfigureSymmetricCipherKey(
    std::span<const uint8_t, ChaCha20KeyByteCount> KeyBytes,
    uint32_t                                       SessionIdentifier) noexcept
{
    std::memcpy(CipherKey.data(), KeyBytes.data(), ChaCha20KeyByteCount);
    ActiveSessionIdentifier = SessionIdentifier;
    CipherEnabled           = true;
}

void SocketTransport::DisableCipherProtection() noexcept
{
    ZeroSensitiveBytes(std::span<char>(reinterpret_cast<char*>(CipherKey.data()), CipherKey.size()));
    CipherEnabled = false;
}

void SocketTransport::RegisterPeerSocketAddress(uint32_t EndpointIdentifier, const IpSocketAddress& Address)
{
    if (EndpointIdentifier == 0u)
    {
        return;
    }
    for (PeerRouteEntry& Existing : PeerRoutes)
    {
        if (Existing.EndpointIdentifier == EndpointIdentifier)
        {
            Existing.Address = Address;
            return;
        }
    }
    PeerRoutes.push_back(PeerRouteEntry{ EndpointIdentifier, Address });
}

bool SocketTransport::OpenChannel() noexcept
{
    if (SocketDescriptor >= 0)
    {
        return true;
    }

#if defined(_WIN32)
    EnsureWinsockStarted();
    const SOCKET RawSocket = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (RawSocket == INVALID_SOCKET)
    {
        return false;
    }
    u_long NonBlockingMode = 1ul;
    if (::ioctlsocket(RawSocket, FIONBIO, &NonBlockingMode) != 0)
    {
        ::closesocket(RawSocket);
        return false;
    }
#else
    const int RawSocket = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (RawSocket < 0)
    {
        return false;
    }
    const int ExistingStatus = ::fcntl(RawSocket, F_GETFL, 0);
    if (ExistingStatus < 0 || ::fcntl(RawSocket, F_SETFL, ExistingStatus | O_NONBLOCK) < 0)
    {
        ::close(RawSocket);
        return false;
    }
#endif

    sockaddr_in BindAddr{};
    BindAddr.sin_family      = AF_INET;
    BindAddr.sin_addr.s_addr = htonl(INADDR_ANY);
    BindAddr.sin_port        = htons(RequestedPortHostOrder);

    if (::bind(RawSocket, reinterpret_cast<const sockaddr*>(&BindAddr), sizeof(BindAddr)) != 0)
    {
#if defined(_WIN32)
        ::closesocket(RawSocket);
#else
        ::close(RawSocket);
#endif
        return false;
    }

    sockaddr_in AssignedAddr{};
#if defined(_WIN32)
    int AssignedLength = static_cast<int>(sizeof(AssignedAddr));
#else
    socklen_t AssignedLength = static_cast<socklen_t>(sizeof(AssignedAddr));
#endif
    if (::getsockname(RawSocket, reinterpret_cast<sockaddr*>(&AssignedAddr), &AssignedLength) == 0)
    {
        BoundPortHostOrder = ntohs(AssignedAddr.sin_port);
    }
    else
    {
        BoundPortHostOrder = RequestedPortHostOrder;
    }

    SocketDescriptor = static_cast<intptr_t>(RawSocket);
    return true;
}

void SocketTransport::CloseChannel() noexcept
{
    if (SocketDescriptor >= 0)
    {
#if defined(_WIN32)
        ::closesocket(static_cast<SOCKET>(SocketDescriptor));
#else
        ::close(static_cast<int>(SocketDescriptor));
#endif
        SocketDescriptor   = -1;
        BoundPortHostOrder = 0u;
    }
}

void SocketTransport::AdvanceChannel(double Δt) noexcept
{
    if (Δt > 0.0)
    {
        LocalClockSeconds += Δt;
    }
}

bool SocketTransport::SendRawDatagramToAddress(const IpSocketAddress& Destination, std::span<const uint8_t> RawBytes) noexcept
{
    if (SocketDescriptor < 0 || Destination.Port == 0u || RawBytes.empty())
    {
        return false;
    }

    sockaddr_in DestAddr{};
    DestAddr.sin_family = AF_INET;
    DestAddr.sin_port   = htons(Destination.Port);
    std::memcpy(&DestAddr.sin_addr.s_addr, Destination.Ipv4Octets.data(), 4u);

#if defined(_WIN32)
    const int SentBytes = ::sendto(
        static_cast<SOCKET>(SocketDescriptor),
        reinterpret_cast<const char*>(RawBytes.data()),
        static_cast<int>(RawBytes.size()),
        0,
        reinterpret_cast<const sockaddr*>(&DestAddr),
        static_cast<int>(sizeof(DestAddr)));
#else
    const ssize_t SentBytes = ::sendto(
        static_cast<int>(SocketDescriptor),
        RawBytes.data(),
        RawBytes.size(),
        0,
        reinterpret_cast<const sockaddr*>(&DestAddr),
        sizeof(DestAddr));
#endif

    return SentBytes >= 0 && static_cast<std::size_t>(SentBytes) == RawBytes.size();
}

bool SocketTransport::PollRawDatagramFromSocket(IpSocketAddress& OutSender, std::vector<uint8_t>& OutRawBytes)
{
    if (SocketDescriptor < 0)
    {
        return false;
    }

    std::array<uint8_t, 1500u> ScratchBuffer{};
    sockaddr_in SenderAddr{};
#if defined(_WIN32)
    int SenderLength = static_cast<int>(sizeof(SenderAddr));
    const int Received = ::recvfrom(
        static_cast<SOCKET>(SocketDescriptor),
        reinterpret_cast<char*>(ScratchBuffer.data()),
        static_cast<int>(ScratchBuffer.size()),
        0,
        reinterpret_cast<sockaddr*>(&SenderAddr),
        &SenderLength);
#else
    socklen_t SenderLength = static_cast<socklen_t>(sizeof(SenderAddr));
    const ssize_t Received = ::recvfrom(
        static_cast<int>(SocketDescriptor),
        ScratchBuffer.data(),
        ScratchBuffer.size(),
        0,
        reinterpret_cast<sockaddr*>(&SenderAddr),
        &SenderLength);
#endif

    if (Received <= 0)
    {
        return false;
    }

    std::memcpy(OutSender.Ipv4Octets.data(), &SenderAddr.sin_addr.s_addr, 4u);
    OutSender.Port = ntohs(SenderAddr.sin_port);
    OutRawBytes.assign(ScratchBuffer.begin(), ScratchBuffer.begin() + Received);
    return true;
}

bool SocketTransport::TransmitDatagram(
    uint32_t                 TargetEndpointIdentifier,
    DeliveryChannel          Channel,
    std::span<const uint8_t> WireBytes)
{
    if (SocketDescriptor < 0 || WireBytes.empty())
    {
        return false;
    }

    const IpSocketAddress* RouteAddr = nullptr;
    for (const PeerRouteEntry& Route : PeerRoutes)
    {
        if (Route.EndpointIdentifier == TargetEndpointIdentifier)
        {
            RouteAddr = &Route.Address;
            break;
        }
    }
    if (RouteAddr == nullptr)
    {
        return false;
    }

    if (!CipherEnabled)
    {
        // Prefix 6-byte cleartext framing: 'F','U','D','P', SenderEndpoint8, Channel8
        std::vector<uint8_t> ClearDatagram;
        ClearDatagram.reserve(6u + WireBytes.size());
        ClearDatagram.push_back(static_cast<uint8_t>('F'));
        ClearDatagram.push_back(static_cast<uint8_t>('U'));
        ClearDatagram.push_back(static_cast<uint8_t>('D'));
        ClearDatagram.push_back(static_cast<uint8_t>('P'));
        ClearDatagram.push_back(static_cast<uint8_t>(LocalEndpointIdentifier & 0xFFu));
        ClearDatagram.push_back(static_cast<uint8_t>(Channel));
        ClearDatagram.insert(ClearDatagram.end(), WireBytes.begin(), WireBytes.end());
        return SendRawDatagramToAddress(*RouteAddr, ClearDatagram);
    }

    // Encrypted sovereign UDP wire framing:
    // AAD[18] = 'F','S','E','C', SenderEndpoint8, Channel8, Nonce[12] (Session32 + Counter64)
    const uint64_t PacketNonceSequence = OutboundCipherCounter++;
    std::array<uint8_t, ChaCha20NonceByteCount> Nonce{};
    WriteLittleEndian32(Nonce.data() + 0u, ActiveSessionIdentifier);
    WriteLittleEndian32(Nonce.data() + 4u, static_cast<uint32_t>(PacketNonceSequence & 0xFFFFFFFFull));
    WriteLittleEndian32(Nonce.data() + 8u, static_cast<uint32_t>((PacketNonceSequence >> 32u) & 0xFFFFFFFFull));

    std::array<uint8_t, 18u> AssociatedHeader{};
    AssociatedHeader[0] = static_cast<uint8_t>('F');
    AssociatedHeader[1] = static_cast<uint8_t>('S');
    AssociatedHeader[2] = static_cast<uint8_t>('E');
    AssociatedHeader[3] = static_cast<uint8_t>('C');
    AssociatedHeader[4] = static_cast<uint8_t>(LocalEndpointIdentifier & 0xFFu);
    AssociatedHeader[5] = static_cast<uint8_t>(Channel);
    std::memcpy(AssociatedHeader.data() + 6u, Nonce.data(), ChaCha20NonceByteCount);

    std::vector<uint8_t> CipherTagBytes;
    EncryptChaCha20Poly1305(CipherKey, Nonce, AssociatedHeader, WireBytes, CipherTagBytes);

    std::vector<uint8_t> EncryptedPacket;
    EncryptedPacket.reserve(AssociatedHeader.size() + CipherTagBytes.size());
    EncryptedPacket.insert(EncryptedPacket.end(), AssociatedHeader.begin(), AssociatedHeader.end());
    EncryptedPacket.insert(EncryptedPacket.end(), CipherTagBytes.begin(), CipherTagBytes.end());
    return SendRawDatagramToAddress(*RouteAddr, EncryptedPacket);
}

bool SocketTransport::ReceiveDatagram(PacketDatagram& OutDatagram)
{
    IpSocketAddress SenderAddr{};
    std::vector<uint8_t> RawDatagram;
    if (!PollRawDatagramFromSocket(SenderAddr, RawDatagram))
    {
        return false;
    }

    // Strip optional 'FRLY' 12-byte stateless relay header if routed via encrypted relay
    std::span<const uint8_t> ActiveView(RawDatagram);
    if (ActiveView.size() > RelayRoutingPrefixByteCount &&
        ActiveView[0] == static_cast<uint8_t>('F') &&
        ActiveView[1] == static_cast<uint8_t>('R') &&
        ActiveView[2] == static_cast<uint8_t>('L') &&
        ActiveView[3] == static_cast<uint8_t>('Y'))
    {
        ActiveView = ActiveView.subspan(RelayRoutingPrefixByteCount);
    }

    if (!CipherEnabled)
    {
        if (ActiveView.size() <= 6u ||
            ActiveView[0] != static_cast<uint8_t>('F') ||
            ActiveView[1] != static_cast<uint8_t>('U') ||
            ActiveView[2] != static_cast<uint8_t>('D') ||
            ActiveView[3] != static_cast<uint8_t>('P'))
        {
            return false;
        }
        const uint8_t RawChannel = ActiveView[5];
        if (RawChannel > static_cast<uint8_t>(DeliveryChannel::ReliableOrdered))
        {
            return false;
        }
        OutDatagram.PeerEndpointIdentifier  = ActiveView[4];
        OutDatagram.Channel                 = static_cast<DeliveryChannel>(RawChannel);
        OutDatagram.ArrivalTimestampSeconds = LocalClockSeconds;
        OutDatagram.WireBytes.assign(ActiveView.begin() + 6, ActiveView.end());
        return true;
    }

    if (ActiveView.size() <= 18u + Poly1305TagByteCount ||
        ActiveView[0] != static_cast<uint8_t>('F') ||
        ActiveView[1] != static_cast<uint8_t>('S') ||
        ActiveView[2] != static_cast<uint8_t>('E') ||
        ActiveView[3] != static_cast<uint8_t>('C'))
    {
        return false;
    }

    const uint8_t RawChannel = ActiveView[5];
    if (RawChannel > static_cast<uint8_t>(DeliveryChannel::ReliableOrdered))
    {
        return false;
    }

    std::span<const uint8_t, ChaCha20NonceByteCount> NonceSlice(ActiveView.data() + 6u, ChaCha20NonceByteCount);
    std::span<const uint8_t> AssociatedHeader = ActiveView.subspan(0u, 18u);
    std::span<const uint8_t> CipherTagBytes     = ActiveView.subspan(18u);

    std::vector<uint8_t> DecryptedBytes;
    if (!DecryptChaCha20Poly1305(CipherKey, NonceSlice, AssociatedHeader, CipherTagBytes, DecryptedBytes))
    {
        return false;
    }

    OutDatagram.PeerEndpointIdentifier  = ActiveView[4];
    OutDatagram.Channel                 = static_cast<DeliveryChannel>(RawChannel);
    OutDatagram.ArrivalTimestampSeconds = LocalClockSeconds;
    OutDatagram.WireBytes               = std::move(DecryptedBytes);
    return true;
}

} // namespace Frontier::NetworkingSpace
