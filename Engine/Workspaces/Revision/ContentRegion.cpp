/*============================================================================================================================================
                                                             CONTENTREGION.CPP
============================================================================================================================================*/
// 🧩 64-byte-aligned bounded document memory region implementation

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Workspaces/Revision/ContentRegion.h"

#include <algorithm>
#include <cstring>

namespace Frontier::Workspaces
{

//------------------------------------------------------------------------------------------------------------------------
//                                                  REGION OPERATIONS
//------------------------------------------------------------------------------------------------------------------------

ContentRegion::ContentRegion(std::size_t MaximumBudget) noexcept
    : MaximumBudgetBytes(AlignUp64(MaximumBudget == 0u ? Constants::ContainerBinaryAlignmentBytes : MaximumBudget))
{
}

void ContentRegion::Reset() noexcept
{
    StorageBytes.clear();
}

void ContentRegion::ConfigureMaximumBudget(std::size_t NewBudgetBytes) noexcept
{
    MaximumBudgetBytes = AlignUp64(NewBudgetBytes == 0u ? Constants::ContainerBinaryAlignmentBytes : NewBudgetBytes);
}

std::size_t ContentRegion::AppendAlignedPayload(std::span<const std::byte> PayloadBytes)
{
    const std::size_t AlignedLength = AlignUp64(PayloadBytes.empty() ? 1u : PayloadBytes.size());
    if (StorageBytes.size() + AlignedLength > MaximumBudgetBytes)
    {
        return InvalidOffset;
    }

    const std::size_t StartOffset = StorageBytes.size();
    StorageBytes.resize(StartOffset + AlignedLength, std::byte{ 0 });
    if (!PayloadBytes.empty())
    {
        std::memcpy(StorageBytes.data() + StartOffset, PayloadBytes.data(), PayloadBytes.size());
    }
    return StartOffset;
}

void ContentRegion::CompactLeadingBytes(std::size_t ReclaimedAlignedBytes) noexcept
{
    const std::size_t AlignedReclaim = AlignUp64(ReclaimedAlignedBytes);
    if (AlignedReclaim == 0u)
    {
        return;
    }
    if (AlignedReclaim >= StorageBytes.size())
    {
        StorageBytes.clear();
        return;
    }

    const std::size_t RemainingBytes = StorageBytes.size() - AlignedReclaim;
    std::memmove(StorageBytes.data(), StorageBytes.data() + AlignedReclaim, RemainingBytes);
    StorageBytes.resize(RemainingBytes);
}

void ContentRegion::TruncateToAlignedOffset(std::size_t AlignedOffsetBytes) noexcept
{
    const std::size_t ClampedOffset = std::min(AlignUp64(AlignedOffsetBytes), StorageBytes.size());
    StorageBytes.resize(ClampedOffset);
}

std::span<const std::byte> ContentRegion::InspectSpan(std::size_t ByteOffset, std::size_t ByteLength) const noexcept
{
    if (ByteOffset > StorageBytes.size() || ByteLength > (StorageBytes.size() - ByteOffset))
    {
        return {};
    }
    return std::span<const std::byte>(StorageBytes.data() + ByteOffset, ByteLength);
}

std::span<const std::byte> ContentRegion::InspectAllBytes() const noexcept
{
    return std::span<const std::byte>(StorageBytes.data(), StorageBytes.size());
}

bool ContentRegion::AssignRawBytes(std::span<const std::byte> RawBytes)
{
    const std::size_t AlignedTotal = AlignUp64(RawBytes.size());
    if (AlignedTotal > MaximumBudgetBytes)
    {
        return false;
    }
    StorageBytes.assign(AlignedTotal, std::byte{ 0 });
    if (!RawBytes.empty())
    {
        std::memcpy(StorageBytes.data(), RawBytes.data(), RawBytes.size());
    }
    return true;
}

} // namespace Frontier::Workspaces
