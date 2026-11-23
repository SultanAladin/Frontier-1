/*============================================================================================================================================
                                                       VECTORAUTHORINGPROPERTIES.H
============================================================================================================================================*/
// 🧩 Shared channel contributions, changed-field patches and enclosure properties without vector or workspace dependencies.

#pragma once
#include <array>
#include <cstdint>
#include <optional>
#include <string>

namespace Frontier
{
//------------------------------------------------------------------------------------------------------------------------
//                                                     CHANNEL AND ENCLOSURE PROPERTIES
//------------------------------------------------------------------------------------------------------------------------

enum class VectorAuthoringSurfaceChannel : uint32_t
{
    Colour, Height, Normal, Roughness, Metalness, Opacity, Occlusion, Transmission, Clearcoat
};
enum class VectorAuthoringSurfaceStructure : uint32_t
{
    Freeform, Stitch, Weave
};
// 📝 Stamp authoring provenance for a layer's contributed instructions/segments. Freeform covers
// ordinary hand-drawn Pattern shapes (and remains the default so existing Pattern documents are
// unaffected); Text/ImportedVector/ImportedImage are Stamp-only component kinds that retain their
// original source alongside the baked cubic-path instructions so the component can be re-edited.
enum class VectorAuthoringComponentKind : uint32_t
{
    Freeform, Text, ImportedVector, ImportedImage
};
enum class VectorAuthoringTextileGeometry : uint32_t
{
    Unclassified, NeedleEntry, StitchThread, UpperThreadPass, WeaveYarn
};
struct VectorAuthoringChannelContribution
{
    bool  Enabled  = true;
    float Strength = 1;
    bool operator==(const VectorAuthoringChannelContribution &) const = default;
};
struct VectorAuthoringEnclosure
{
    uint32_t    Token                   = 0;
    uint32_t    EnclosureToken          = 0;
    std::string Name;
    bool        FolderEnabled           = false;
    bool        VisibilityEnabled       = true;
    bool        LockEnabled             = false;
    bool        GroupWideEditingEnabled = false;
    bool operator==(const VectorAuthoringEnclosure &) const = default;
};
using VectorAuthoringSurfaceChannels = std::array<VectorAuthoringChannelContribution, 9>;

struct VectorAuthoringChannelPatch
{
    std::array<std::optional<bool>, 9>  Enabled;
    std::array<std::optional<float>, 9> Strength;
};

constexpr VectorAuthoringSurfaceChannels ConstructVectorAuthoringSurfaceChannels() noexcept
{
    // 📝 Occlusion, Transmission and Clearcoat stay disabled until explicitly painted so an untouched
    // layer never overrides a material's native ambient occlusion, transmission or clearcoat with a
    // neutral value; indexed explicitly rather than via back() so extending the channel list is safe.
    VectorAuthoringSurfaceChannels Contributions{};
    Contributions[size_t(VectorAuthoringSurfaceChannel::Occlusion)].Enabled     = false;
    Contributions[size_t(VectorAuthoringSurfaceChannel::Transmission)].Enabled = false;
    Contributions[size_t(VectorAuthoringSurfaceChannel::Clearcoat)].Enabled    = false;
    return Contributions;
}

void ValidateVectorAuthoringSurfaceChannels(const VectorAuthoringSurfaceChannels &Channels);
void ValidateVectorAuthoringChannelPatch(const VectorAuthoringChannelPatch &Patch);
bool ApplyVectorAuthoringSurfacePatch(VectorAuthoringSurfaceChannels &Channels, const VectorAuthoringChannelPatch &Patch);
}
