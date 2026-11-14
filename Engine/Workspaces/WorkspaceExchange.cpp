/*============================================================================================================================================
                                                           WORKSPACEEXCHANGE.CPP
============================================================================================================================================*/
// 🧩 Lock-free cross-workspace publication exchange implementation

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Workspaces/WorkspaceExchange.h"

#include <algorithm>
#include <cstring>

namespace Frontier::Workspaces
{

//------------------------------------------------------------------------------------------------------------------------
//                                                 EXCHANGE OPERATIONS
//------------------------------------------------------------------------------------------------------------------------

uint64_t WorkspaceExchange::PublishUpdate(
    WorkspaceToken   SourceDocument,
    uint32_t         AffectedUDIMNumber,
    double           TexelDensityPxPerMeter,
    std::string_view Note) noexcept
{
    Threading::ScopedSpinGuard Guard(ProducerGuard);
    const uint32_t WriteSlot = NextWriteSlot;
    NextWriteSlot            = (NextWriteSlot + 1u) % static_cast<uint32_t>(RingPublications.size());
    if (NextWriteSlot == ReadableSlotIndex.load(std::memory_order_relaxed))
    {
        NextWriteSlot = (NextWriteSlot + 1u) % static_cast<uint32_t>(RingPublications.size());
    }

    const uint64_t NewGeneration = GenerationCounter.fetch_add(1ull, std::memory_order_relaxed) + 1ull;
    CrossWorkspacePublication& Target = RingPublications[WriteSlot];
    Target.PublicationGeneration      = NewGeneration;
    Target.SourceDocument             = SourceDocument;
    Target.SourceCategory             = SourceDocument.ExtensionCategory;
    Target.AffectedUDIMNumber         = AffectedUDIMNumber;
    Target.TexelDensityPxPerMeter     = TexelDensityPxPerMeter;
    std::memset(Target.PublicationNote, 0, sizeof(Target.PublicationNote));
    const std::size_t CopyLength = std::min<std::size_t>(Note.size(), sizeof(Target.PublicationNote) - 1u);
    if (CopyLength > 0u)
    {
        std::memcpy(Target.PublicationNote, Note.data(), CopyLength);
    }

    ReadableSlotIndex.store(WriteSlot, std::memory_order_release);
    return NewGeneration;
}

CrossWorkspacePublication WorkspaceExchange::InspectLatestPublication() const noexcept
{
    const uint32_t Slot = ReadableSlotIndex.load(std::memory_order_acquire);
    return RingPublications[Slot];
}

} // namespace Frontier::Workspaces
