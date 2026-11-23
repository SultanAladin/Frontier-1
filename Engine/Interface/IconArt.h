#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace Frontier {

enum class IconSymbol : uint16_t {
#define FRONTIER_ICON(Symbol, File) Symbol,
#include "IconSymbols.inc"
#undef FRONTIER_ICON
    Count
};

enum class IconDomain : uint8_t { Editor, Texture, Fluid };

enum class IconResult : uint8_t { Ready, Unsupported, Missing, DecodeFailure, InvalidRequest, RuntimeFailure };
// Diagnostic rendering is opt-in. Unsupported SVG features must never masquerade as approved artwork.
enum class IconPolicy : uint8_t { Strict, Diagnostic };

struct IconRaster final {
    uint32_t Width = 0, Height = 0;
    // Tightly packed, straight-alpha RGBA8 in sRGB. Upload as UNORM for the existing ImGui presentation.
    // No ImGui identifier, graphics descriptor, or GPU resource belongs here.
    std::vector<uint8_t> Rgba;
    IconResult Result = IconResult::Ready;
    bool Substitute = false;
    std::string Diagnostic;
};

struct IconStatistics final {
    uint64_t Decodes = 0, Rasterizations = 0, Reuses = 0, Evictions = 0;
    size_t ResidentBytes = 0, ResidentCount = 0;
};

// Single calling thread, like the ImGui drawing thread. Initialization/termination must be serialized
// with the host's ThorVG lifetime. Each instance holds one balanced ThorVG runtime reference.
// Immutable returned rasters remain valid after eviction, Clear(), or destruction of this class.
class IconArt final {
public:
    explicit IconArt(std::filesystem::path Root, size_t ByteLimit = 16u * 1024u * 1024u);
    ~IconArt();
    IconArt(const IconArt&) = delete;
    IconArt& operator=(const IconArt&) = delete;
    IconArt(IconArt&&) = delete;
    IconArt& operator=(IconArt&&) = delete;

    [[nodiscard]] std::shared_ptr<const IconRaster> Rasterize(IconSymbol Symbol, float LogicalWidth,
        float LogicalHeight, float DisplayScale = 1.0f, IconPolicy Policy = IconPolicy::Strict, IconDomain Domain = IconDomain::Editor);
    void Clear(); // Explicit reload / theme change; SVG bytes are not polled during drawing.
    [[nodiscard]] IconStatistics Statistics() const;
    [[nodiscard]] static IconDomain Domain(IconSymbol Symbol) noexcept;
    [[nodiscard]] static bool Available(IconSymbol Symbol, IconDomain Workspace) noexcept;
    [[nodiscard]] static std::vector<IconSymbol> Catalogue(IconDomain Workspace);
    [[nodiscard]] static const char* Filename(IconSymbol Symbol) noexcept;
    [[nodiscard]] static const char* Name(IconSymbol Symbol) noexcept;
    [[nodiscard]] static const char* ResultName(IconResult Result) noexcept;
private:
    struct Details;
    std::unique_ptr<Details> Details_;
};
}
