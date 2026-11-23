//============================================================================================================================================
//                                                     MATERIALPREVIEWATLAS.H
//============================================================================================================================================
// 🧩 Stable preview-atlas placement for immutable Engine Content and compiled workspace material surfaces.

#pragma once

#include "Foundation/DeliveryGuarantee.h"

#include <cstdint>
#include <vector>

namespace Slate
{

enum class MaterialPreviewState : std::uint32_t
{
    Missing,
    BakeOwed,
    Ready
};

struct MaterialPreviewTile
{
    std::uint32_t MaterialIndex = 0u;
    std::uint32_t Revision = 0u;
    std::uint32_t AtlasIndex = 0u;
    std::uint32_t TileIndex = 0u;
    std::uint64_t Fingerprint = 0u;
    std::uint64_t BakedFingerprint = 0u;
    MaterialPreviewState State = MaterialPreviewState::Missing;
    bool Active = false;
};

/// 🧩 Assigns stable fixed-size preview tiles; baking and GPU image residency consume these identities later.
class MaterialPreviewAtlas
{
public:
    static constexpr std::uint32_t TileExtent = 256u;
    static constexpr std::uint32_t TilesPerAxis = 16u;
    static constexpr std::uint32_t TilesPerAtlas = TilesPerAxis * TilesPerAxis;

    Deliver<MaterialPreviewTile> Reserve(std::uint32_t MaterialIndex, std::uint32_t Revision,
                                         std::uint64_t Fingerprint);
    Deliver<MaterialPreviewTile> Resolve(std::uint32_t MaterialIndex) const;

    /// Marks a tile ready only for the exact material revision a bake consumed.
    Deliver<MaterialPreviewTile> MarkBaked(std::uint32_t MaterialIndex, std::uint64_t Fingerprint);
    void Retire(std::uint32_t MaterialIndex);
    std::uint32_t DeclaredCount() const;

private:
    std::vector<MaterialPreviewTile> Tiles = {};
};

} // namespace Slate
