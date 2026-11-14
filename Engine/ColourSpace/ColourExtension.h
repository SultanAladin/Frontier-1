/*============================================================================================================================================
                                                              COLOUREXTENSION.H
============================================================================================================================================*/
// 🎨 Designated ColourSpace lifecycle coordinator managing engine-wide ramp/swatch repository, GPU LUT sheet & WorkspaceExchange publication

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/ColourSpace/ColourCodex.h"
#include "Engine/ColourSpace/ColourDocument.h"
#include "Engine/ColourSpace/ColourPresetRepository.h"
#include "Engine/Workspaces/WorkspaceExchange.h"
#include "Engine/Workspaces/WorkspaceToken.h"

#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace Frontier::ColourSpace
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct ColourExtensionMetrics
{
    uint32_t RegisteredRampCount   = 0u;   // [count] - Total built-in + custom ramps in repository
    uint32_t RegisteredSwatchCount = 0u;   // [count] - Total custom swatches in repository
    uint32_t RegisteredAliasCount  = 0u;   // [count] - Total named aliases in unified ColourCodex (solids, ramps, illuminants, custom)
    uint32_t BakedLutCount         = 0u;   // [count] - Pre-baked 64-entry GPU LUT rows ready for upload
    uint64_t RevisionGeneration    = 0ull; // [gen]   - Monotonic cache invalidation sequence
    bool     Initialized           = false;// [-]     - True between Initialize() and Finalize()
    bool     Suspended             = false;// [-]     - True while paused
};

class ColourExtension
{
public:
    ColourExtension() = default;

    bool Initialize(Workspaces::WorkspaceExchange* ExchangeBus = nullptr, Workspaces::WorkspaceToken OwnerToken = {}) noexcept;
    void Tick(double DeltaSeconds) noexcept;
    void Suspend() noexcept;
    void Resume() noexcept;
    void Finalize() noexcept;

    [[nodiscard]] ColourPresetRepository&       AccessRepository() noexcept { return Repository; }
    [[nodiscard]] const ColourPresetRepository& QueryRepository() const noexcept { return Repository; }
    [[nodiscard]] ColourCodex&                  AccessCodex() noexcept { return DomainCodex; }
    [[nodiscard]] const ColourCodex&            QueryCodex() const noexcept { return DomainCodex; }

    std::size_t RegisterOrUpdateRamp(ColourRampPresetEntry PresetEntry);
    bool        RemoveCustomRamp(std::string_view Identifier);
    void        RebuildLutSheet();

    [[nodiscard]] const ColourRampLut64*           QueryBakedLutByIdentifier(std::string_view Identifier) const noexcept;
    [[nodiscard]] std::span<const ColourRampLut64> QueryBakedLutSheet() const noexcept { return BakedLutSheet; }
    [[nodiscard]] ColourExtensionMetrics           InspectMetrics() const noexcept;

private:
    void PublishChangeNotice(std::string_view Summary) noexcept;

    ColourPresetRepository         Repository          {};      // [-]   - Engine-wide ramp & swatch repository
    ColourCodex                    DomainCodex         {};      // [-]   - Unified engine-wide named alias codex across all 7 modular colour formats
    std::vector<ColourRampLut64>   BakedLutSheet       {};      // [-]   - Contiguous 64-entry GPU LUT rows aligned 1:1 with RampPresets
    Workspaces::WorkspaceExchange* Exchange            = nullptr;// [-]  - Optional cross-workspace notification bus
    Workspaces::WorkspaceToken     DocumentToken       {};      // [-]   - Owning .ColourSpace workspace token
    uint64_t                       Generation          = 0ull;  // [gen] - Monotonic revision stamp
    bool                           InitializationActive= false; // [-]   - Lifecycle initialization marker
    bool                           SuspensionActive    = false; // [-]   - Lifecycle suspension marker
    bool                           LutRebuildPending   = true;  // [-]   - True when BakedLutSheet requires refresh
};

} // namespace Frontier::ColourSpace
