/*==========================================================================================================================================
                                                          RENDER TARGET VOCABULARY
                                     ------------------------------------------------------------------
                                Usages, formats and declarations shared by ordering, aliasing and barriers.
==========================================================================================================================================*/

// 🧩 Render target vocabulary: usages, formats and declarations shared by ordering, aliasing and barriers.

#pragma once

#include <cstdint>
#include <string>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                         USAGES
//------------------------------------------------------------------------------------------------------------------------

// How one unit touches one target. These abstract the Vulkan image layouts the SDK
// phase will bind (Sampled→SHADER_READ_ONLY, StorageRead/Write→GENERAL,
// ColourWrite→COLOR_ATTACHMENT, Depth*→DEPTH_*, Present→PRESENT_SRC).
enum class TargetUsage : uint8_t
{
    Undefined,            // No defined contents/layout yet (transient birth state)
    Sampled,              // [read]  - Texture sampling in a shader
    StorageRead,          // [read]  - Storage image load
    DepthRead,            // [read]  - Depth tested/sampled read
    TransferSource,       // [read]  - Copy/blit source
    StorageWrite,         // [write] - Storage image store
    StorageReadWrite,     // [read+write] - In-place update within one unit
    ColourWrite,          // [write] - Colour attachment output
    DepthWrite,           // [write] - Depth attachment output
    TransferDestination,  // [write] - Copy/blit destination
    Present               // [write] - Handed to the display engine (swapchain rest state)
};

[[nodiscard]] constexpr bool TargetUsageReads(TargetUsage Usage) noexcept
{
    switch (Usage)
    {
        case TargetUsage::Sampled:
        case TargetUsage::StorageRead:
        case TargetUsage::DepthRead:
        case TargetUsage::TransferSource:
        case TargetUsage::StorageReadWrite:
            return true;
        default:
            return false;
    }
}

[[nodiscard]] constexpr bool TargetUsageWrites(TargetUsage Usage) noexcept
{
    switch (Usage)
    {
        case TargetUsage::StorageWrite:
        case TargetUsage::StorageReadWrite:
        case TargetUsage::ColourWrite:
        case TargetUsage::DepthWrite:
        case TargetUsage::TransferDestination:
        case TargetUsage::Present:
            return true;
        default:
            return false;
    }
}

[[nodiscard]] constexpr const char* TargetUsageText(TargetUsage Usage) noexcept
{
    switch (Usage)
    {
        case TargetUsage::Undefined:           return "Undefined";
        case TargetUsage::Sampled:             return "Sampled";
        case TargetUsage::StorageRead:         return "StorageRead";
        case TargetUsage::DepthRead:           return "DepthRead";
        case TargetUsage::TransferSource:      return "TransferSource";
        case TargetUsage::StorageWrite:        return "StorageWrite";
        case TargetUsage::StorageReadWrite:    return "StorageReadWrite";
        case TargetUsage::ColourWrite:         return "ColourWrite";
        case TargetUsage::DepthWrite:          return "DepthWrite";
        case TargetUsage::TransferDestination: return "TransferDestination";
        case TargetUsage::Present:             return "Present";
    }
    return "Undefined";
}

// One unit's touch of one target, in declared order within the unit.
struct TargetTouch
{
    std::string Target;
    TargetUsage Usage = TargetUsage::Undefined;
};

//------------------------------------------------------------------------------------------------------------------------
//                                                         FORMATS
//------------------------------------------------------------------------------------------------------------------------

enum class TargetFormat : uint8_t
{
    Rgba16Float,   // 8 bytes/px - HDR surface/radiance
    Rgba8Unorm,    // 4 bytes/px - LDR colour
    Bgra8Unorm,    // 4 bytes/px - swapchain order
    Rg16Float,     // 4 bytes/px - velocity/moments
    R32Float,      // 4 bytes/px - scalar fields
    R32Uint,       // 4 bytes/px - visibility ids
    Rg32Uint,      // 8 bytes/px - packed id pairs (reservoir records class)
    Depth32Float   // 4 bytes/px - depth
};

[[nodiscard]] constexpr uint32_t TargetFormatBytesPerPixel(TargetFormat Format) noexcept
{
    switch (Format)
    {
        case TargetFormat::Rgba16Float: return 8;
        case TargetFormat::Rg32Uint:    return 8;
        default:                        return 4;
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                       DECLARATION
//------------------------------------------------------------------------------------------------------------------------

struct RenderTargetDeclaration
{
    std::string  Name;                                  // [-] - As used in unit touches
    TargetFormat Format       = TargetFormat::Rgba16Float;
    uint32_t     Width        = 0;                      // [px]
    uint32_t     Height       = 0;                      // [px]
    bool         Persistent   = false;                  // [-] - History/swapchain: never aliased
    TargetUsage  RestingUsage = TargetUsage::Undefined; // [-] - Layout at frame start (history rest state)
};

// [bytes] - Tight-packed size (the atlas binning bound; the SDK allocator owns pitch/alignment).
[[nodiscard]] inline uint64_t RenderTargetBytes(const RenderTargetDeclaration& Declaration) noexcept
{
    return static_cast<uint64_t>(Declaration.Width) * Declaration.Height *
           TargetFormatBytesPerPixel(Declaration.Format);
}

} // namespace Frontier
