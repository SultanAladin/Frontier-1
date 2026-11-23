/*============================================================================================================================================
                                                       CONTENTHASHCALCULATOR.CPP
============================================================================================================================================*/
// 🧩 Pure ISO C++20 xxHash64, FIPS 180-4 SHA-256, and FNV-1a 64 implementations with zero third-party crypto dependencies

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Serialization/DependencyOrder/ContentHashCalculator.h"

#include <bit>
#include <cstring>

namespace Frontier::Serialization
{

namespace
{

constexpr uint64_t XxPrime1 = 0x9E3779B185EBCA87ULL;
constexpr uint64_t XxPrime2 = 0xC2B2AE3D27D4EB4FULL;
constexpr uint64_t XxPrime3 = 0x165667B19E3779F9ULL;
constexpr uint64_t XxPrime4 = 0x85EBCA77C2B2AE63ULL;
constexpr uint64_t XxPrime5 = 0x27D4EB2F165667C5ULL;

constexpr std::array<uint32_t, 64u> Sha256RoundConstants = {
    0x428A2F98u, 0x71374491u, 0xB5C0FBCFu, 0xE9B5DBA5u, 0x3956C25Bu, 0x59F111F1u, 0x923F82A4u, 0xAB1C5ED5u,
    0xD807AA98u, 0x12835B01u, 0x243185BEu, 0x550C7DC3u, 0x72BE5D74u, 0x80DEB1FEu, 0x9BDC06A7u, 0xC19BF174u,
    0xE49B69C1u, 0xEFBE4786u, 0x0FC19DC6u, 0x240CA1CCu, 0x2DE92C6Fu, 0x4A7484AAu, 0x5CB0A9DCu, 0x76F988DAu,
    0x983E5152u, 0xA831C66Du, 0xB00327C8u, 0xBF597FC7u, 0xC6E00BF3u, 0xD5A79147u, 0x06CA6351u, 0x14292967u,
    0x27B70A85u, 0x2E1B2138u, 0x4D2C6DFCu, 0x53380D13u, 0x650A7354u, 0x766A0ABBu, 0x81C2C92Eu, 0x92722C85u,
    0xA2BFE8A1u, 0xA81A664Bu, 0xC24B8B70u, 0xC76C51A3u, 0xD192E819u, 0xD6990624u, 0xF40E3585u, 0x106AA070u,
    0x19A4C116u, 0x1E376C08u, 0x2748774Cu, 0x34B0BCB5u, 0x391C0CB3u, 0x4ED8AA4Au, 0x5B9CCA4Fu, 0x682E6FF3u,
    0x748F82EEu, 0x78A5636Fu, 0x84C87814u, 0x8CC70208u, 0x90BEFFFAu, 0xA4506CEBu, 0xBEF9A3F7u, 0xC67178F2u
};

[[nodiscard]] uint64_t ReadLittleEndian64(const std::byte* Source) noexcept
{
    uint64_t Output = 0u;
    for (uint32_t Offset = 0u; Offset < 8u; ++Offset)
    {
        Output |= static_cast<uint64_t>(std::to_integer<uint8_t>(Source[Offset])) << (Offset * 8u);
    }
    return Output;
}

[[nodiscard]] uint32_t ReadLittleEndian32(const std::byte* Source) noexcept
{
    uint32_t Output = 0u;
    for (uint32_t Offset = 0u; Offset < 4u; ++Offset)
    {
        Output |= static_cast<uint32_t>(std::to_integer<uint8_t>(Source[Offset])) << (Offset * 8u);
    }
    return Output;
}

[[nodiscard]] uint32_t ReadBigEndian32(const uint8_t* Source) noexcept
{
    return (static_cast<uint32_t>(Source[0u]) << 24u) |
           (static_cast<uint32_t>(Source[1u]) << 16u) |
           (static_cast<uint32_t>(Source[2u]) << 8u)  |
           static_cast<uint32_t>(Source[3u]);
}

[[nodiscard]] uint64_t XxRound(uint64_t Accumulator, uint64_t Lane) noexcept
{
    Accumulator += Lane * XxPrime2;
    Accumulator  = std::rotl(Accumulator, 31);
    Accumulator *= XxPrime1;
    return Accumulator;
}

[[nodiscard]] uint64_t XxMergeRound(uint64_t Accumulator, uint64_t Lane) noexcept
{
    Lane         = XxRound(0u, Lane);
    Accumulator ^= Lane;
    Accumulator  = Accumulator * XxPrime1 + XxPrime4;
    return Accumulator;
}

void CompressSha256Block(const uint8_t Block[64u], std::array<uint32_t, 8u>& WorkingWords) noexcept
{
    std::array<uint32_t, 64u> Schedule {};
    for (uint32_t Index = 0u; Index < 16u; ++Index)
    {
        Schedule[Index] = ReadBigEndian32(Block + (Index * 4u));
    }
    for (uint32_t Index = 16u; Index < 64u; ++Index)
    {
        const uint32_t Sigma0 = std::rotr(Schedule[Index - 15u], 7) ^ std::rotr(Schedule[Index - 15u], 18) ^ (Schedule[Index - 15u] >> 3u);
        const uint32_t Sigma1 = std::rotr(Schedule[Index - 2u], 17) ^ std::rotr(Schedule[Index - 2u], 19) ^ (Schedule[Index - 2u] >> 10u);
        Schedule[Index] = Schedule[Index - 16u] + Sigma0 + Schedule[Index - 7u] + Sigma1;
    }

    uint32_t A = WorkingWords[0u];
    uint32_t B = WorkingWords[1u];
    uint32_t C = WorkingWords[2u];
    uint32_t D = WorkingWords[3u];
    uint32_t E = WorkingWords[4u];
    uint32_t F = WorkingWords[5u];
    uint32_t G = WorkingWords[6u];
    uint32_t H = WorkingWords[7u];

    for (uint32_t Round = 0u; Round < 64u; ++Round)
    {
        const uint32_t S1    = std::rotr(E, 6) ^ std::rotr(E, 11) ^ std::rotr(E, 25);
        const uint32_t Ch    = (E & F) ^ ((~E) & G);
        const uint32_t Temp1 = H + S1 + Ch + Sha256RoundConstants[Round] + Schedule[Round];
        const uint32_t S0    = std::rotr(A, 2) ^ std::rotr(A, 13) ^ std::rotr(A, 22);
        const uint32_t Maj   = (A & B) ^ (A & C) ^ (B & C);
        const uint32_t Temp2 = S0 + Maj;

        H = G;
        G = F;
        F = E;
        E = D + Temp1;
        D = C;
        C = B;
        B = A;
        A = Temp1 + Temp2;
    }

    WorkingWords[0u] += A;
    WorkingWords[1u] += B;
    WorkingWords[2u] += C;
    WorkingWords[3u] += D;
    WorkingWords[4u] += E;
    WorkingWords[5u] += F;
    WorkingWords[6u] += G;
    WorkingWords[7u] += H;
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                 HASHING PRIMITIVES
//------------------------------------------------------------------------------------------------------------------------

uint64_t ComputeFnv1a64(std::span<const std::byte> InputBytes) noexcept
{
    uint64_t Hash = 14695981039346656037ULL;
    for (const std::byte Octet : InputBytes)
    {
        Hash ^= static_cast<uint64_t>(std::to_integer<uint8_t>(Octet));
        Hash *= 1099511628211ULL;
    }
    return Hash;
}

uint64_t ComputeXxHash64(std::span<const std::byte> InputBytes, uint64_t Seed) noexcept
{
    const std::byte* Cursor = InputBytes.data();
    const std::byte* End    = Cursor + InputBytes.size();
    uint64_t         Hash   = 0u;

    if (InputBytes.size() >= 32u)
    {
        uint64_t Lane1 = Seed + XxPrime1 + XxPrime2;
        uint64_t Lane2 = Seed + XxPrime2;
        uint64_t Lane3 = Seed;
        uint64_t Lane4 = Seed - XxPrime1;

        const std::byte* Limit = End - 32u;
        do
        {
            Lane1 = XxRound(Lane1, ReadLittleEndian64(Cursor));
            Cursor += 8u;
            Lane2 = XxRound(Lane2, ReadLittleEndian64(Cursor));
            Cursor += 8u;
            Lane3 = XxRound(Lane3, ReadLittleEndian64(Cursor));
            Cursor += 8u;
            Lane4 = XxRound(Lane4, ReadLittleEndian64(Cursor));
            Cursor += 8u;
        }
        while (Cursor <= Limit);

        Hash = std::rotl(Lane1, 1) + std::rotl(Lane2, 7) + std::rotl(Lane3, 12) + std::rotl(Lane4, 18);
        Hash = XxMergeRound(Hash, Lane1);
        Hash = XxMergeRound(Hash, Lane2);
        Hash = XxMergeRound(Hash, Lane3);
        Hash = XxMergeRound(Hash, Lane4);
    }
    else
    {
        Hash = Seed + XxPrime5;
    }

    Hash += static_cast<uint64_t>(InputBytes.size());

    while (Cursor + 8u <= End)
    {
        const uint64_t Lane = XxRound(0u, ReadLittleEndian64(Cursor));
        Hash ^= Lane;
        Hash  = std::rotl(Hash, 27) * XxPrime1 + XxPrime4;
        Cursor += 8u;
    }
    if (Cursor + 4u <= End)
    {
        Hash ^= static_cast<uint64_t>(ReadLittleEndian32(Cursor)) * XxPrime1;
        Hash  = std::rotl(Hash, 23) * XxPrime2 + XxPrime3;
        Cursor += 4u;
    }
    while (Cursor < End)
    {
        Hash ^= static_cast<uint64_t>(std::to_integer<uint8_t>(*Cursor)) * XxPrime5;
        Hash  = std::rotl(Hash, 11) * XxPrime1;
        ++Cursor;
    }

    Hash ^= Hash >> 33u;
    Hash *= XxPrime2;
    Hash ^= Hash >> 29u;
    Hash *= XxPrime3;
    Hash ^= Hash >> 32u;
    return Hash;
}

uint64_t ComputeXxHash64String(std::string_view Text, uint64_t Seed) noexcept
{
    return ComputeXxHash64(std::as_bytes(std::span<const char>(Text.data(), Text.size())), Seed);
}

Sha256Hash ComputeSha256(std::span<const std::byte> InputBytes) noexcept
{
    std::array<uint32_t, 8u> WorkingWords = {
        0x6A09E667u, 0xBB67AE85u, 0x3C6EF372u, 0xA54FF53Au,
        0x510E527Fu, 0x9B05688Cu, 0x1F83D9ABu, 0x5BE0CD19u
    };

    const uint8_t* RawPtr = reinterpret_cast<const uint8_t*>(InputBytes.data());
    std::size_t    Offset = 0u;
    while (Offset + 64u <= InputBytes.size())
    {
        CompressSha256Block(RawPtr + Offset, WorkingWords);
        Offset += 64u;
    }

    std::array<uint8_t, 128u> TailBuffer {};
    const std::size_t Remaining = InputBytes.size() - Offset;
    if (Remaining > 0u)
    {
        std::memcpy(TailBuffer.data(), RawPtr + Offset, Remaining);
    }
    TailBuffer[Remaining] = 0x80u;

    const std::size_t PaddedBlocks = (Remaining < 56u) ? 1u : 2u;
    const uint64_t    BitLength    = static_cast<uint64_t>(InputBytes.size()) * 8u;
    const std::size_t LengthOffset = (PaddedBlocks * 64u) - 8u;
    for (uint32_t ByteIdx = 0u; ByteIdx < 8u; ++ByteIdx)
    {
        TailBuffer[LengthOffset + ByteIdx] = static_cast<uint8_t>((BitLength >> ((7u - ByteIdx) * 8u)) & 0xFFu);
    }

    CompressSha256Block(TailBuffer.data(), WorkingWords);
    if (PaddedBlocks == 2u)
    {
        CompressSha256Block(TailBuffer.data() + 64u, WorkingWords);
    }

    Sha256Hash Result{};
    for (uint32_t WordIdx = 0u; WordIdx < 8u; ++WordIdx)
    {
        Result.Octets[(WordIdx * 4u) + 0u] = static_cast<uint8_t>((WorkingWords[WordIdx] >> 24u) & 0xFFu);
        Result.Octets[(WordIdx * 4u) + 1u] = static_cast<uint8_t>((WorkingWords[WordIdx] >> 16u) & 0xFFu);
        Result.Octets[(WordIdx * 4u) + 2u] = static_cast<uint8_t>((WorkingWords[WordIdx] >> 8u) & 0xFFu);
        Result.Octets[(WordIdx * 4u) + 3u] = static_cast<uint8_t>(WorkingWords[WordIdx] & 0xFFu);
    }
    return Result;
}

Sha256Hash ComputeSha256String(std::string_view Text) noexcept
{
    return ComputeSha256(std::as_bytes(std::span<const char>(Text.data(), Text.size())));
}

std::string FormatSha256Hex(const Sha256Hash& Hash) noexcept
{
    static constexpr char HexAlphabet[] = "0123456789abcdef";
    std::string Output(64u, '0');
    for (std::size_t Index = 0u; Index < 32u; ++Index)
    {
        const uint8_t Octet = Hash.Octets[Index];
        Output[(Index * 2u) + 0u] = HexAlphabet[(Octet >> 4u) & 0x0Fu];
        Output[(Index * 2u) + 1u] = HexAlphabet[Octet & 0x0Fu];
    }
    return Output;
}

std::string FormatHex64(uint64_t Checksum64) noexcept
{
    static constexpr char HexAlphabet[] = "0123456789ABCDEF";
    std::string Output = "0x0000000000000000";
    for (uint32_t Nibble = 0u; Nibble < 16u; ++Nibble)
    {
        const uint32_t Shift = (15u - Nibble) * 4u;
        Output[2u + Nibble]  = HexAlphabet[(Checksum64 >> Shift) & 0x0FULL];
    }
    return Output;
}

bool ParseHex64(std::string_view HexText, uint64_t& OutChecksum64) noexcept
{
    if (HexText.size() >= 2u && HexText[0u] == '0' && (HexText[1u] == 'x' || HexText[1u] == 'X'))
    {
        HexText.remove_prefix(2u);
    }
    if (HexText.empty() || HexText.size() > 16u)
    {
        return false;
    }

    uint64_t Accumulated = 0u;
    for (const char Character : HexText)
    {
        uint64_t Digit = 0u;
        if (Character >= '0' && Character <= '9')
        {
            Digit = static_cast<uint64_t>(Character - '0');
        }
        else if (Character >= 'a' && Character <= 'f')
        {
            Digit = static_cast<uint64_t>(10 + (Character - 'a'));
        }
        else if (Character >= 'A' && Character <= 'F')
        {
            Digit = static_cast<uint64_t>(10 + (Character - 'A'));
        }
        else
        {
            return false;
        }
        Accumulated = (Accumulated << 4u) | Digit;
    }
    OutChecksum64 = Accumulated;
    return true;
}

} // namespace Frontier::Serialization
