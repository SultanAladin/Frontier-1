/*============================================================================================================================================
                                                           COLOURASSETREGISTER.CPP
============================================================================================================================================*/
// 🧩 Implementation of ColourAssetRegister lifecycle participant, contiguous GPU LUT cache & WorkspaceExchange live publication

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/ColourSpace/ColourAssetRegister.h"

#include "Engine/ColourSpace/ColourRampSampler.h"

namespace Frontier::ColourSpace
{

//------------------------------------------------------------------------------------------------------------------------
//                                                 LIFECYCLE OPERATIONS
//------------------------------------------------------------------------------------------------------------------------

bool ColourAssetRegister::Initialize(Workspaces::WorkspaceExchange* ExchangeBus, Workspaces::WorkspaceToken OwnerToken) noexcept
{
    Exchange             = ExchangeBus;
    DocumentToken        = OwnerToken;
    if (!DocumentToken.VerifyValid())
    {
        DocumentToken.SlotIndex         = 0u;
        DocumentToken.Generation        = 1u;
        DocumentToken.SuffixCategory = Codecs::SpaceSuffixCategory::ColourSpace;
    }
    InitializationActive = true;
    SuspensionActive     = false;
    LutRebuildPending    = true;
    RebuildLutSheet();
    return true;
}

void ColourAssetRegister::Tick(double /*DeltaSeconds*/) noexcept
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

void ColourAssetRegister::Suspend() noexcept
{
    SuspensionActive = true;
}

void ColourAssetRegister::Resume() noexcept
{
    SuspensionActive = false;
}

void ColourAssetRegister::Finalize() noexcept
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

std::size_t ColourAssetRegister::RegisterOrUpdateRamp(ColourRampPresetEntry PresetEntry)
{
    const std::string NoticeId = PresetEntry.Identifier;
    (void)DomainAliasRepository.SyncRampPreset(PresetEntry);
    const std::size_t Index    = Repository.AddOrUpdateCustomRamp(std::move(PresetEntry));
    LutRebuildPending          = true;
    RebuildLutSheet();
    PublishChangeNotice(NoticeId);
    return Index;
}

bool ColourAssetRegister::RemoveCustomRamp(std::string_view Identifier)
{
    if (!Repository.RemoveCustomRamp(Identifier))
    {
        return false;
    }
    (void)DomainAliasRepository.RemoveCustomAlias(Identifier);
    LutRebuildPending = true;
    RebuildLutSheet();
    PublishChangeNotice(Identifier);
    return true;
}

void ColourAssetRegister::RebuildLutSheet()
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

const ColourRampLut64* ColourAssetRegister::QueryBakedLutByIdentifier(std::string_view Identifier) const noexcept
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

ColourAssetRegisterMetrics ColourAssetRegister::InspectMetrics() const noexcept
{
    return {
        static_cast<uint32_t>(Repository.QueryRampPresets().size()),
        static_cast<uint32_t>(Repository.QuerySwatches().size()),
        static_cast<uint32_t>(DomainAliasRepository.QueryAliases().size()),
        static_cast<uint32_t>(BakedLutSheet.size()),
        Generation,
        InitializationActive,
        SuspensionActive,
    };
}

void ColourAssetRegister::PublishChangeNotice(std::string_view Summary) noexcept
{
    if (Exchange != nullptr && InitializationActive)
    {
        Exchange->PublishUpdate(DocumentToken, static_cast<uint32_t>(BakedLutSheet.size()), 0.0, Summary);
    }
}

} // namespace Frontier::ColourSpace
