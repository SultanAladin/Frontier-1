/*============================================================================================================================================
                                                            GEOMETRYINSTANCE.H
============================================================================================================================================*/
// 🧩 Generation-authenticated GeometryInstance and contiguous GeometryInstanceRegistry with non-kinship index tree connectivity

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometrySpace/Network/PolygonNetwork.h"
#include "Engine/Mathematics/AxisCoordinates.h"

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Frontier::GeometrySpace
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      ENUMERATIONS
//------------------------------------------------------------------------------------------------------------------------

enum class InstanceAttributeMask : uint8_t
{
    None                   = 0u,      // [-] - Default unlocked, hidden instance
    EnableVisibility       = 1u << 0, // [-] - Included in 3D/2D GPU rasterization
    EnablePickLock         = 1u << 1, // [-] - Excluded from interactive viewport picking
    EnableWireframeOverlay = 1u << 2, // [-] - Renders anti-aliased wireframe overlay
};

[[nodiscard]] constexpr InstanceAttributeMask operator|(InstanceAttributeMask Left, InstanceAttributeMask Right) noexcept
{
    return static_cast<InstanceAttributeMask>(static_cast<uint8_t>(Left) | static_cast<uint8_t>(Right));
}

[[nodiscard]] constexpr InstanceAttributeMask operator&(InstanceAttributeMask Left, InstanceAttributeMask Right) noexcept
{
    return static_cast<InstanceAttributeMask>(static_cast<uint8_t>(Left) & static_cast<uint8_t>(Right));
}

[[nodiscard]] constexpr bool QueryInstanceAttributeEnabled(InstanceAttributeMask Mask, InstanceAttributeMask Directive) noexcept
{
    return (static_cast<uint8_t>(Mask) & static_cast<uint8_t>(Directive)) != 0u;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct GeometryInstanceToken
{
    uint32_t SlotIndex  = 0u; // [idx]   - 1-based contiguous slot index (0 = invalid)
    uint32_t Generation = 0u; // [count] - Monotonic generation guard preventing stale handle reuse

    [[nodiscard]] constexpr bool Valid() const noexcept
    {
        return SlotIndex != 0u && Generation != 0u;
    }

    [[nodiscard]] constexpr bool operator==(const GeometryInstanceToken&) const noexcept = default;
};

struct alignas(8) GeometryInstanceIdentity
{
    GeometryInstanceToken Token             {};                  // [8 B] - Generation-checked slot token
    uint32_t              NetworkSlotIndex  = InvalidNetworkIndex; // [idx] - Referenced PolygonNetwork index
    uint32_t              EnclosingIndex    = InvalidNetworkIndex; // [idx] - Enclosing instance slot (replaces kinship terms)
    uint32_t              FirstNestedIndex  = InvalidNetworkIndex; // [idx] - First nested instance slot
    uint32_t              NextPeerIndex     = InvalidNetworkIndex; // [idx] - Next peer instance slot at same depth
    uint16_t              DepthLevel        = 0u;                  // [lvl] - Nesting depth (0 = root level)
    InstanceAttributeMask AttributeMask     = InstanceAttributeMask::EnableVisibility | InstanceAttributeMask::EnableWireframeOverlay; // [mask]
    uint8_t               ReservedAlignment = 0u;                  // [-]   - Explicit 32-byte header alignment pad
};

static_assert(sizeof(GeometryInstanceIdentity) == 32u, "GeometryInstanceIdentity must occupy exactly 32 bytes.");

struct GeometryInstance
{
    GeometryInstanceIdentity      Identity      {};                   // [32 B] - Compact 32-byte identity & non-kinship links
    Mathematics::SpatialTransform Transform     {};                   // [36 B] - Position (Axis3D), Rotation (Axis3D), Scale (Scalar3D)
    std::string                   InstanceTitle { "GeometryInstance" }; // [-]  - Display title in Outliner & Inspector
};

class GeometryInstanceRegistry
{
public:
    GeometryInstanceRegistry() = default;

    [[nodiscard]] GeometryInstanceToken IssueInstance(
        std::string_view                     Title,
        uint32_t                             NetworkSlotIndex,
        const Mathematics::SpatialTransform& InitialTransform = {});

    bool ReclaimInstance(GeometryInstanceToken Token) noexcept;

    bool AttachNestedInstance(
        GeometryInstanceToken EnclosingToken,
        GeometryInstanceToken NestedToken) noexcept;

    [[nodiscard]] bool                    AuthenticateToken(GeometryInstanceToken Token) const noexcept;
    [[nodiscard]] GeometryInstance*       ResolveInstance(GeometryInstanceToken Token) noexcept;
    [[nodiscard]] const GeometryInstance* InspectInstance(GeometryInstanceToken Token) const noexcept;
    [[nodiscard]] uint32_t                QueryLiveInstanceCount() const noexcept;
    [[nodiscard]] std::vector<GeometryInstanceToken> EnumerateLiveInstances() const;

private:
    void DetachFromEnclosingLinks(uint32_t ZeroBasedSlot) noexcept;

    std::vector<GeometryInstance> Slots            {};   // [-]     - Contiguous instance storage
    std::vector<uint32_t>         RecycledSlots    {};   // [idx]   - Zero-based recycled slot stack
    std::vector<uint8_t>          SlotOccupiedMask {};   // [0/1]   - 1 when slot holds a live GeometryInstance
    uint32_t                      LiveCount        = 0u; // [count] - Active GeometryInstance count
};

} // namespace Frontier::GeometrySpace
