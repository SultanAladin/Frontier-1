/*============================================================================================================================================
                                                        CONTENTHASHCALCULATOR.H
============================================================================================================================================*/
// 🧩 Deterministic cross-platform xxHash64, FIPS 180-4 SHA-256, and FNV-1a 64 hashing primitives for DependencyOrder and container integrity

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace Frontier::Serialization
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct Sha256Hash
{
    std::array<uint8_t, 32u> Octets {}; // [B] - 256-bit cryptographic hash bytes

    [[nodiscard]] bool operator==(const Sha256Hash& Other) const noexcept = default;
};

//------------------------------------------------------------------------------------------------------------------------
//                                                  FUNCTION PROTOTYPES
//------------------------------------------------------------------------------------------------------------------------

[[nodiscard]] uint64_t ComputeFnv1a64(std::span<const std::byte> InputBytes) noexcept;

[[nodiscard]] uint64_t ComputeXxHash64(std::span<const std::byte> InputBytes, uint64_t Seed = 0u) noexcept;

[[nodiscard]] uint64_t ComputeXxHash64String(std::string_view Text, uint64_t Seed = 0u) noexcept;

[[nodiscard]] Sha256Hash ComputeSha256(std::span<const std::byte> InputBytes) noexcept;

[[nodiscard]] Sha256Hash ComputeSha256String(std::string_view Text) noexcept;

[[nodiscard]] std::string FormatSha256Hex(const Sha256Hash& Hash) noexcept;

[[nodiscard]] std::string FormatHex64(uint64_t Checksum64) noexcept;

[[nodiscard]] bool ParseHex64(std::string_view HexText, uint64_t& OutChecksum64) noexcept;

} // namespace Frontier::Serialization
