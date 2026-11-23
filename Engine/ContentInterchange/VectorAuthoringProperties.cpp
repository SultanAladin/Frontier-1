/*============================================================================================================================================
                                                      VECTORAUTHORINGPROPERTIES.CPP
============================================================================================================================================*/
// 🧩 Validate bounded surface contributions and apply only explicitly edited fields for reuse across authoring applications.

#include "VectorAuthoringProperties.h"
#include <cmath>
#include <stdexcept>

namespace Frontier
{
namespace
{
//------------------------------------------------------------------------------------------------------------------------
//                                                     CHANNEL DOMAIN
//------------------------------------------------------------------------------------------------------------------------

void ValidateChannelStrength(size_t Index, float Strength)
{
    const float Maximum = Index == size_t(VectorAuthoringSurfaceChannel::Normal) ? 4.f : 1.f;
    if (!std::isfinite(Strength) || Strength < 0 || Strength > Maximum)
        throw std::runtime_error("VectorAuthoring channel strength exceeds its finite authoring interval.");
}
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     CHANNEL PATCH EVALUATION
//------------------------------------------------------------------------------------------------------------------------

void ValidateVectorAuthoringSurfaceChannels(const VectorAuthoringSurfaceChannels &Channels)
{
    for (size_t Index = 0; Index < Channels.size(); ++Index) ValidateChannelStrength(Index, Channels[Index].Strength);
}

void ValidateVectorAuthoringChannelPatch(const VectorAuthoringChannelPatch &Patch)
{
    for (size_t Index = 0; Index < Patch.Strength.size(); ++Index)
        if (Patch.Strength[Index]) ValidateChannelStrength(Index, *Patch.Strength[Index]);
}

bool ApplyVectorAuthoringSurfacePatch(VectorAuthoringSurfaceChannels &Channels, const VectorAuthoringChannelPatch &Patch)
{
    ValidateVectorAuthoringSurfaceChannels(Channels);
    ValidateVectorAuthoringChannelPatch(Patch);
    auto Replacement = Channels;
    for (size_t Index = 0; Index < Channels.size(); ++Index)
    {
        if (Patch.Enabled[Index]) Replacement[Index].Enabled = *Patch.Enabled[Index];
        if (Patch.Strength[Index]) Replacement[Index].Strength = *Patch.Strength[Index];
    }
    if (Replacement == Channels) return false;
    Channels = Replacement;
    return true;
}
}
