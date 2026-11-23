/*==========================================================================================================================================
                                                       MATERIALLIBRARYRENDERING.H
==========================================================================================================================================*/
// 🧩 Share bounded GPU thumbnail residency, material baking and a separate interactive preview between native applications.

#pragma once
#include "MaterialThumbnailAtlas.h"
#include "MaterialPreviewConfiguration.h"
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace Frontier
{
struct MaterialThumbnailSelection
{
    uint64_t Generation = 0;
    ThumbnailUvRectangle Uv;
    bool Ready = false;
};

// 📝 CPU-rendered sphere previews of every scanned environment archive, packed into one square
// RGBA8 atlas with the thumbnail atlas gutter convention; hosts stage it verbatim.
struct EnvironmentPreviewAtlas
{
    std::span<const uint8_t> Pixels;
    uint64_t Generation = 0;
    uint32_t Count = 0;
};

class MaterialLibraryRendering
{
  public:
    MaterialLibraryRendering();
    ~MaterialLibraryRendering();
    MaterialLibraryRendering(const MaterialLibraryRendering &) = delete;
    MaterialLibraryRendering &operator=(const MaterialLibraryRendering &) = delete;
    static constexpr uint32_t EnvironmentPreviewTile = 96;
    static constexpr uint32_t EnvironmentPreviewColumns = 8;
    void Initialize(const VulkanRasterAccess &Access, const std::filesystem::path &ShaderDirectory);
    // 📝 (Re)scan a directory of equirect PNG environment archives: decodes, derives each key
    // light analytically, uploads the array texture and re-renders the sphere preview atlas.
    // Safe to call again after the user drops new files into the folder (the import path).
    void ConfigureEnvironments(const std::filesystem::path &Directory);
    [[nodiscard]] std::vector<std::string> QueryEnvironmentNames() const;
    [[nodiscard]] EnvironmentPreviewAtlas QueryEnvironmentPreviews() const noexcept;
    void RequestLibrary(const std::filesystem::path &Directory, const std::vector<std::filesystem::path> &Visible);
    // 📝 Opt-in disk cache for the GPU-baked gallery atlas: call once after Initialize. On the next
    // RequestLibrary that changes the library directory, a matching on-disk atlas is loaded wholesale
    // and only slots whose source content changed since the save are rebaked live; once every pinned
    // slot is settled, the atlas is read back and saved automatically. Entirely best-effort -- any
    // failure (missing file, digest drift, GPU error) silently falls back to the normal live bake and
    // never throws, matching the atlas's own crash-never contract for this host-facing feature.
    void ConfigureAtlasCache(const std::filesystem::path &File, uint64_t FormatDigest);
    [[nodiscard]] bool QueryAtlasCacheSettled() const noexcept;
    void ConfigurePreview(const MaterialPreviewConfiguration &Configuration);
    [[nodiscard]] MaterialPreviewConfiguration QueryPreviewConfiguration() const;
    // 📝 Display-size preview: the square target follows the viewport pixel edge (256..2048) so the cover
    // fit stops upscaling. Replacement retires at the submission boundary; hosts must rebind the preview
    // view when QueryPreviewView changes.
    void ConfigurePreviewExtent(uint32_t Extent);
    [[nodiscard]] uint32_t QueryPreviewExtent() const noexcept;
    void ConfigureSelection(const std::filesystem::path &RelativePath, const CompiledSurfaceMaterial &Material);
    void ConstructCommands(VkCommandBuffer Commands, uint64_t CompletedSerial, uint64_t SubmissionSerial);
    void ConfirmSubmission() noexcept;
    void InvalidateThumbnails() noexcept;
    void InvalidateThumbnail(const std::filesystem::path &RelativePath) noexcept;
    [[nodiscard]] bool AuthenticateThumbnail(const std::filesystem::path &RelativePath, uint64_t Generation) const;
    [[nodiscard]] uint64_t QueryBakeCount() const noexcept;
    [[nodiscard]] VkImage QueryPreviewImage() const noexcept;
    [[nodiscard]] VkImage QueryAtlasImage() const noexcept;
    [[nodiscard]] uint32_t QueryAtlasExtent() const noexcept;
    [[nodiscard]] std::string QueryThumbnailDiagnostic(const std::filesystem::path &RelativePath) const;
    [[nodiscard]] MaterialThumbnailSelection QueryThumbnail(const std::filesystem::path &RelativePath) const;
    [[nodiscard]] VkImageView QueryAtlasView() const noexcept;
    [[nodiscard]] VkImageView QueryPreviewView() const noexcept;
    [[nodiscard]] bool QueryPreviewReady() const noexcept;
    [[nodiscard]] const std::string &QueryDiagnostic() const noexcept;

  private:
    struct RenderingAllocation;
    std::unique_ptr<RenderingAllocation> Allocation;
};
} // namespace Frontier
