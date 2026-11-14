/*============================================================================================================================================
                                                             COLOUREXTENSION.CPP
============================================================================================================================================*/
// 🎨 Implementation of ColourExtension lifecycle coordinator, contiguous GPU LUT cache & WorkspaceExchange live publication

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/ColourSpace/ColourExtension.h"

#include "Engine/ColourSpace/ColourRampSampler.h"

namespace Frontier::ColourSpace
{

//------------------------------------------------------------------------------------------------------------------------
//                                                 LIFECYCLE OPERATIONS
//------------------------------------------------------------------------------------------------------------------------

bool ColourExtension::Initialize(Workspaces::WorkspaceExchange* ExchangeBus, Workspaces::WorkspaceToken OwnerToken) noexcept
{
    Exchange             = ExchangeBus;
    DocumentToken        = OwnerToken;
    if (!DocumentToken.VerifyValid())
    {
        DocumentToken.SlotIndex         = 0u;
        DocumentToken.Generation        = 1u;
        DocumentToken.ExtensionCategory = Codecs::SpaceExtensionCategory::ColourSpace;
    }
    InitializationActive = true;
    SuspensionActive     = false;
    LutRebuildPending    = true;
    RebuildLutSheet();
    return true;
}

void ColourExtension::Tick(double /*DeltaSeconds*/) noexcept
{
    if (!InitializationActive || SuspensionActive)
    {
        return;
    }
    if (LutRebuildPending)
    {
        RebuildLutSheet();
    }
}

void ColourExtension::Suspend() noexcept
{
    SuspensionActive = true;
}

void ColourExtension::Resume() noexcept
{
    SuspensionActive = false;
}

void ColourExtension::Finalize() noexcept
{
    BakedLutSheet.clear();
    Exchange             = nullptr;
    InitializationActive = false;
    SuspensionActive     = false;
    LutRebuildPending    = false;
}

//------------------------------------------------------------------------------------------------------------------------
//                                             RAMP REGISTRATION & LUT CACHE
//------------------------------------------------------------------------------------------------------------------------

std::size_t ColourExtension::RegisterOrUpdateRamp(ColourRampPresetEntry PresetEntry)
{
    const std::string NoticeId = PresetEntry.Identifier;
    (void)DomainCodex.SyncRampPreset(PresetEntry);
    const std::size_t Index    = Repository.AddOrUpdateCustomRamp(std::move(PresetEntry));
    LutRebuildPending          = true;
    RebuildLutSheet();
    PublishChangeNotice(NoticeId);
    return Index;
}

bool ColourExtension::RemoveCustomRamp(std::string_view Identifier)
{
    if (!Repository.RemoveCustomRamp(Identifier))
    {
        return false;
    }
    (void)DomainCodex.RemoveCustomAlias(Identifier);
    LutRebuildPending = true;
    RebuildLutSheet();
    PublishChangeNotice(Identifier);
    return true;
}

void ColourExtension::RebuildLutSheet()
{
    const auto Ramps = Repository.QueryRampPresets();
    BakedLutSheet.clear();
    BakedLutSheet.reserve(Ramps.size());
    for (const ColourRampPresetEntry& Preset : Ramps)
    {
        BakedLutSheet.push_back(PackColourRampLut64(ConstructProfileFromRampPreset(Preset)));
    }
    ++Generation;
    LutRebuildPending = false;
}

const ColourRampLut64* ColourExtension::QueryBakedLutByIdentifier(std::string_view Identifier) const noexcept
{
    const auto Ramps = Repository.QueryRampPresets();
    const std::size_t Limit = (Ramps.size() < BakedLutSheet.size()) ? Ramps.size() : BakedLutSheet.size();
    for (std::size_t Index = 0u; Index < Limit; ++Index)
    {
        if (Ramps[Index].Identifier == Identifier)
        {
            return &BakedLutSheet[Index];
        }
    }
    return nullptr;
}

ColourExtensionMetrics ColourExtension::InspectMetrics() const noexcept
{
    return {
        static_cast<uint32_t>(Repository.QueryRampPresets().size()),
        static_cast<uint32_t>(Repository.QuerySwatches().size()),
        static_cast<uint32_t>(DomainCodex.QueryAliases().size()),
        static_cast<uint32_t>(BakedLutSheet.size()),
        Generation,
        InitializationActive,
        SuspensionActive,
    };
}

void ColourExtension::PublishChangeNotice(std::string_view Summary) noexcept
{
    if (Exchange != nullptr && InitializationActive)
    {
        Exchange->PublishUpdate(DocumentToken, static_cast<uint32_t>(BakedLutSheet.size()), 0.0, Summary);
    }
}

} // namespace Frontier::ColourSpace
