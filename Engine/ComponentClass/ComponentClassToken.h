/*============================================================================================================================================
                                                          COMPONENTCLASSTOKEN.H
============================================================================================================================================*/
// 🧩 Generation-checked identity handles for ComponentClass assets, typed component instances, and world placements

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include <cstdint>

namespace Frontier::ComponentClass
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct ComponentClassToken
{
    uint32_t SlotIndex  = 0u; // [idx]   - 1-based registry slot index (0 = invalid)
    uint32_t Generation = 0u; // [count] - Monotonic generation guard

    [[nodiscard]] constexpr bool Valid() const noexcept
    {
        return SlotIndex != 0u && Generation != 0u;
    }

    [[nodiscard]] constexpr bool operator==(const ComponentClassToken&) const noexcept = default;
};

struct ComponentInstanceToken
{
    uint32_t SlotIndex  = 0u; // [idx]   - 1-based contiguous component instance slot (0 = invalid)
    uint32_t Generation = 0u; // [count] - Monotonic generation guard

    [[nodiscard]] constexpr bool Valid() const noexcept
    {
        return SlotIndex != 0u && Generation != 0u;
    }

    [[nodiscard]] constexpr bool operator==(const ComponentInstanceToken&) const noexcept = default;
};

struct WorldPlacementToken
{
    uint32_t SlotIndex  = 0u; // [idx]   - 1-based world placement slot (0 = invalid)
    uint32_t Generation = 0u; // [count] - Monotonic generation guard

    [[nodiscard]] constexpr bool Valid() const noexcept
    {
        return SlotIndex != 0u && Generation != 0u;
    }

    [[nodiscard]] constexpr bool operator==(const WorldPlacementToken&) const noexcept = default;
};

inline constexpr uint32_t InvalidHierarchyIndex = 0xFFFFFFFFu; // [idx] - Unlinked EnclosingIndex / FirstNestedIndex / NextPeerIndex

} // namespace Frontier::ComponentClass
