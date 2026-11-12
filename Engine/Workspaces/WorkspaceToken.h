/*============================================================================================================================================
                                                              WORKSPACETOKEN.H
============================================================================================================================================*/
// 🧩 Generation-checked workspace document token preventing stale-slot access across open .Space sessions

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Codecs/SpaceContainerCodec.h"

#include <cstdint>

namespace Frontier::Workspaces
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct WorkspaceToken
{
    uint32_t                      SlotIndex         = 0u;                                        // [idx] - 0-based WorkspaceRegistry slot
    uint32_t                      Generation        = 0u;                                        // [gen] - Monotonic slot generation (0 = invalid)
    Codecs::SpaceExtensionCategory ExtensionCategory = Codecs::SpaceExtensionCategory::ProjectSpace; // [-] - Bound .Space domain category

    [[nodiscard]] constexpr bool VerifyValid() const noexcept
    {
        return Generation != 0u;
    }

    [[nodiscard]] constexpr bool operator==(const WorkspaceToken& Other) const noexcept = default;
};

} // namespace Frontier::Workspaces
