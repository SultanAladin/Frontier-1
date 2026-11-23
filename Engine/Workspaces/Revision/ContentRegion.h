/*============================================================================================================================================
                                                               CONTENTREGION.H
============================================================================================================================================*/
// 🧩 64-byte-aligned bounded memory region enforcing the 215 MiB per-document parametric revision ceiling

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Constants/EngineConstants.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace Frontier::Workspaces
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

class ContentRegion
{
public:
    static constexpr std::size_t InvalidOffset = static_cast<std::size_t>(-1); // [B] - Marker indicating allocation rejection

    explicit ContentRegion(std::size_t MaximumBudgetBytes = Constants::DefaultDocumentRevisionBudgetBytes) noexcept;

    [[nodiscard]] static constexpr std::size_t AlignUp64(std::size_t ByteCount) noexcept
    {
        const std::size_t Alignment = Constants::ContainerBinaryAlignmentBytes;
        return (ByteCount + (Alignment - 1u)) & ~(Alignment - 1u);
    }

    void Reset() noexcept;
    void ConfigureMaximumBudget(std::size_t NewBudgetBytes) noexcept;

    [[nodiscard]] std::size_t AppendAlignedPayload(std::span<const std::byte> PayloadBytes);
    void                      CompactLeadingBytes(std::size_t ReclaimedAlignedBytes) noexcept;
    void                      TruncateToAlignedOffset(std::size_t AlignedOffsetBytes) noexcept;

    [[nodiscard]] std::span<const std::byte> InspectSpan(std::size_t ByteOffset, std::size_t ByteLength) const noexcept;
    [[nodiscard]] std::span<const std::byte> InspectAllBytes() const noexcept;
    bool                                     AssignRawBytes(std::span<const std::byte> RawBytes);

    [[nodiscard]] std::size_t QueryBytesUsed() const noexcept { return StorageBytes.size(); }
    [[nodiscard]] std::size_t QueryMaximumBudgetBytes() const noexcept { return MaximumBudgetBytes; }
    [[nodiscard]] std::size_t QueryRemainingBudgetBytes() const noexcept
    {
        return MaximumBudgetBytes > StorageBytes.size() ? (MaximumBudgetBytes - StorageBytes.size()) : 0u;
    }

private:
    std::size_t            MaximumBudgetBytes = Constants::DefaultDocumentRevisionBudgetBytes; // [B] - Hard ceiling (default 215 MiB)
    std::vector<std::byte> StorageBytes       {};                                              // [B] - 64B-stride contiguous payload buffer
};

} // namespace Frontier::Workspaces
