/*============================================================================================================================================
                                                              COLOURDOCUMENT.CPP
============================================================================================================================================*/
// 🧩 Implementation of ColourDocument ramp/swatch indexing and deterministic LUT baking

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/ColourSpace/ColourDocument.h"

#include "Engine/ColourSpace/ColourRampSampler.h"

namespace Frontier::ColourSpace
{

//------------------------------------------------------------------------------------------------------------------------
//                                                   COLOUR DOCUMENT METHODS
//------------------------------------------------------------------------------------------------------------------------

void ColourDocument::PopulateDefaultStudioRamps()
{
    Ramps.clear();
    const auto& BuiltIns = QueryBuiltInColourRampPresets();
    Ramps.reserve(BuiltIns.size());
    for (const auto& Preset : BuiltIns)
    {
        AuthoredColourRamp Entry {};
        Entry.RampIdentifier = Preset.Identifier;
        Entry.DisplayTitle   = Preset.DisplayTitle;
        Entry.SeriesTitle    = Preset.SeriesTitle;
        Entry.Profile        = ConstructProfileFromRampPreset(Preset);
        Ramps.push_back(std::move(Entry));
    }
    RebuildBakedLuts64();
}

void ColourDocument::RebuildBakedLuts64()
{
    BakedLuts64.clear();
    BakedLuts64.reserve(Ramps.size());
    for (const AuthoredColourRamp& Ramp : Ramps)
    {
        BakedLuts64.push_back(PackColourRampLut64(Ramp.Profile));
    }
}

const AuthoredColourRamp* ColourDocument::FindRamp(std::string_view RampIdentifier) const noexcept
{
    for (const auto& Ramp : Ramps)
    {
        if (Ramp.RampIdentifier == RampIdentifier)
        {
            return &Ramp;
        }
    }
    return nullptr;
}

AuthoredColourRamp* ColourDocument::FindRampMutable(std::string_view RampIdentifier) noexcept
{
    for (auto& Ramp : Ramps)
    {
        if (Ramp.RampIdentifier == RampIdentifier)
        {
            return &Ramp;
        }
    }
    return nullptr;
}

const NamedColourSwatchEntry* ColourDocument::FindSwatch(std::string_view SwatchName) const noexcept
{
    for (const auto& Swatch : Swatches)
    {
        if (Swatch.SwatchName == SwatchName)
        {
            return &Swatch;
        }
    }
    return nullptr;
}

std::size_t ColourDocument::UpsertRamp(AuthoredColourRamp Ramp)
{
    for (std::size_t Index = 0u; Index < Ramps.size(); ++Index)
    {
        if (Ramps[Index].RampIdentifier == Ramp.RampIdentifier)
        {
            Ramps[Index] = std::move(Ramp);
            return Index;
        }
    }
    Ramps.push_back(std::move(Ramp));
    return Ramps.size() - 1u;
}

std::size_t ColourDocument::UpsertSwatch(NamedColourSwatchEntry Swatch)
{
    for (std::size_t Index = 0u; Index < Swatches.size(); ++Index)
    {
        if (Swatches[Index].SwatchName == Swatch.SwatchName)
        {
            Swatches[Index] = std::move(Swatch);
            return Index;
        }
    }
    Swatches.push_back(std::move(Swatch));
    return Swatches.size() - 1u;
}

} // namespace Frontier::ColourSpace
