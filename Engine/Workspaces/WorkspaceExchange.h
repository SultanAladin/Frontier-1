/*============================================================================================================================================
                                                            WORKSPACEEXCHANGE.H
============================================================================================================================================*/
// 🧩 Lock-free generation-stamped cross-workspace asset publication bus (e.g. .UVSpace UVTile -> .TextureSpace / .MaterialSpace)

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Codecs/SpaceContainerCodec.h"
#include "Engine/Threading/Sync/SpinLock.h"
#include "Engine/Workspaces/WorkspaceToken.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <string_view>

namespace Frontier::Workspaces
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct CrossWorkspacePublication
{
    uint64_t                       PublicationGeneration  = 0ull;  // [gen]  - Monotonic publication sequence
    WorkspaceToken                 SourceDocument         {};      // [-]    - Originating .Space document token
    Codecs::SpaceExtensionCategory SourceCategory         = Codecs::SpaceExtensionCategory::UVSpace; // [-] - Domain tag
    uint32_t                       AffectedUDIMNumber     = 1001u; // [code] - Updated UDIM UVTile code (when applicable)
    double                         TexelDensityPxPerMeter = 512.0; // [px/m] - Published texel density or scalar metric
    char                           PublicationNote[64]    {};      // [-]    - Compact UTF-8 publication summary
};

class WorkspaceExchange
{
public:
    WorkspaceExchange() noexcept = default;

    uint64_t PublishUpdate(
        WorkspaceToken   SourceDocument,
        uint32_t         AffectedUDIMNumber,
        double           TexelDensityPxPerMeter,
        std::string_view Note) noexcept;

    [[nodiscard]] CrossWorkspacePublication InspectLatestPublication() const noexcept;
    [[nodiscard]] uint64_t                  QueryLatestGeneration() const noexcept
    {
        return GenerationCounter.load(std::memory_order_acquire);
    }

private:
    std::array<CrossWorkspacePublication, 4u> RingPublications  {};     // [-]   - Quad-buffered lock-free publication ring
    std::atomic<uint32_t>                     ReadableSlotIndex { 0u }; // [idx] - Current published slot index
    std::atomic<uint64_t>                     GenerationCounter { 0ull }; // [gen] - Monotonic publication generation
    uint32_t                                  NextWriteSlot     = 1u;   // [idx] - Next producer slot
    Threading::SpinLock                       ProducerGuard     {};     // [-]   - Leaf spin lock serializing producers
};

} // namespace Frontier::Workspaces
