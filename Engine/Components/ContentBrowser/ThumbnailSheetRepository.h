/*============================================================================================================================================
                                                        THUMBNAILSHEETREPOSITORY.H
============================================================================================================================================*/
// 🧩 Pre-baked ThumbnailSheet pages, flat 64-byte ContentIndex directory, workspace domain masks, and two-step quarantine import intake

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Components/PixelSpace.h"
#include "Engine/Configuration/EngineConfiguration.h"
#include "Engine/Threading/Threads/Streaming/AssetStreamThread.h"

#include <imgui.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      ENUMERATIONS
//------------------------------------------------------------------------------------------------------------------------

enum class ContentCategory : uint8_t
{
    All         = 0,  // [-] - Every category permitted by active WorkspaceCategory
    Colours     = 1,  // [-] - Named ColourCodex swatches (.ColourSwatch / .ColourSpace)
    Geometry    = 2,  // [-] - Analytic & polygonal 3D solids (.GeometrySpace / .Sketch)
    UVLayouts   = 3,  // [-] - UDIM & UVTile parameterization sets (.UVSpace)
    Materials   = 4,  // [-] - Physical BSDF shading definitions (.MaterialSpace)
    Textures    = 5,  // [-] - Multi-UDIM channel-packed textures (.TextureSpace / .Pigment)
    Images      = 6,  // [-] - SDR/HDR reference and backdrop images (.ImageSpace)
    Vectors     = 7,  // [-] - Exact 2D curves and vector icons (.VectorSpace)
    Fonts       = 8,  // [-] - Optical typeface packages (.FontSpace)
    Simulations = 9,  // [-] - Rigid, fluid, fracture, and force-field setups (.SimulationSpace)
    Volumetrics = 10, // [-] - Sparse signed-distance and density volumes (.VolumetricSpace)
    Particles   = 11, // [-] - GPU particulate trajectory setups (.ParticleSpace)
    Photometry  = 12, // [-] - LM-63 .IES luminaires and sky distributions (.Photometry)
    Audio       = 13, // [-] - Spatial acoustic waveforms and buses (.Acoustic)
    Shaders     = 14, // [-] - Slang (.slang) compute and raster units (.Render)
};

enum class ContentOriginFilter : uint8_t
{
    All      = 0, // [-] - EngineContent + Project Content + Imported entries
    Project  = 1, // [-] - Project Content plus linked EngineContent references
    Imported = 2, // [-] - Quarantine-converted external assets in Imported destination
    Engine   = 3, // [-] - Read-only stock entries under EngineContent/
};

enum class ImportDestinationMode : uint8_t
{
    ProjectImportedFolder = 0, // [-] - Quarantine -> Projects/<Project>/Content/Imported/<Category>/ (Scratch fallback if unsaved)
    ScratchTempFolder     = 1, // [-] - Quarantine -> Projects/_Scratch/Saved/Imported/<Category>/ (explicit temp storage)
    UserCustomFolder      = 2, // [-] - Quarantine -> User-selected directory created on demand
};

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

// 📝 Exact 64-byte (1 CPU cache line) flat binary index entry stored contiguously in ContentIndex.Space
struct alignas(64) ContentIndexEntry64
{
    uint64_t ContentHash64              = 0ull; // [hash] - Deterministic 64-bit asset identifier
    uint64_t SheetSliceByteOffset       = 0ull; // [B]    - 64-byte-aligned tile slice offset inside ThumbnailSheet.ImageSpace
    uint32_t TitleStringOffset          = 0u;   // [B]    - UTF-8 byte offset inside string section
    uint32_t RelativePathStringOffset   = 0u;   // [B]    - UTF-8 path offset inside string section
    uint32_t WorkspaceCompatibilityMask = 0u;   // [mask] - Bitmask of WorkspaceCategory bit positions allowed to view/use entry
    uint32_t SheetSliceByteLength       = 0u;   // [B]    - Byte length of pre-baked RGBA32 tile pixels
    uint16_t SheetPageOrdinal           = 0u;   // [idx]  - Pre-baked ThumbnailSheet page index
    uint16_t TileColumn                 = 0u;   // [idx]  - Tile X slot within sheet page
    uint16_t TileRow                    = 0u;   // [idx]  - Tile Y slot within sheet page
    uint16_t TilePixelExtent            = 128u; // [px]   - Square tile width/height in pixels
    uint8_t  CategoryCode               = 0u;   // [-]    - ContentCategory encoded ordinal
    uint8_t  OriginCode                 = 0u;   // [-]    - ContentOriginFilter encoded ordinal
    uint8_t  TintRed                    = 180u; // [0-255]- Primary preview tint R
    uint8_t  TintGreen                  = 180u; // [0-255]- Primary preview tint G
    uint8_t  TintBlue                   = 180u; // [0-255]- Primary preview tint B
    uint8_t  ReadOnlyProtected          = 1u;   // [0/1]  - Non-zero for read-only EngineContent stock entries
    uint8_t  ReservedAlignment[18]      {};     // [-]    - Zero-filled padding to exact 64-byte cache line
};
static_assert(sizeof(ContentIndexEntry64) == 64u, "ContentIndexEntry64 must occupy exactly 64 bytes");

struct ContentRepositoryEntry
{
    ContentIndexEntry64   IndexHeader     {};     // [-] - 64-byte binary directory header
    std::string           DisplayTitle    {};     // [-] - Human-readable content title
    std::string           ContainerFormat {};     // [-] - Canonical Frontier container extension (e.g. .MaterialSpace)
    std::filesystem::path DiskPath        {};     // [-] - Resolved path under EngineContent/ or Projects/
    bool                  TileResident    = false;// [-] - True once pre-baked tile pixels are resident in ImTextureData
};

struct ThumbnailUvQuad
{
    ImTextureData* TexturePointer = nullptr;     // [-]   - Pre-baked sheet ImTextureData pointer
    Point2D        UvMin          { 0.0f, 0.0f };// [0-1] - Normalized top-left UV inside sheet page
    Point2D        UvMax          { 1.0f, 1.0f };// [0-1] - Normalized bottom-right UV inside sheet page
    bool           Valid          = false;       // [-]   - True when sheet page texture is ready
};

struct ImportIntakeConfiguration
{
    ImportDestinationMode Mode                  = ImportDestinationMode::ProjectImportedFolder; // [-] - User-selected destination policy
    std::filesystem::path EngineContentRoot     {};                                             // [-] - EngineContent/ directory path
    std::filesystem::path ActiveProjectRoot     {};                                             // [-] - Projects/<Project>/ (empty if unsaved)
    std::filesystem::path ScratchSessionRoot    {};                                             // [-] - Projects/_Scratch/ fallback root
    std::filesystem::path CustomImportDirectory {};                                             // [-] - Optional user-chosen directory
};

struct ImportIntakeResult
{
    bool                  Accepted          = false; // [-]   - True when validated, converted, and indexed
    std::size_t           RepositoryIndex   = 0u;    // [idx] - Index of newly registered ContentRepositoryEntry
    ContentCategory   DetectedCategory  = ContentCategory::Geometry; // [-] - Inferred asset category
    std::filesystem::path QuarantinePath    {};      // [-]   - Transient Saved/ImportIntake path used during step 1
    std::filesystem::path FinalSpacePath    {};      // [-]   - Final .Space or .ColourSwatch path written in step 2
    std::string           DiagnosticMessage {};      // [-]   - Rejection reason or conversion summary
};

//------------------------------------------------------------------------------------------------------------------------
//                                                  FUNCTION PROTOTYPES
//------------------------------------------------------------------------------------------------------------------------

class ThumbnailSheetRepository
{
public:
    static constexpr uint32_t SheetDimensionPixels = 1024u; // [px] - Square RGBA32 page width & height (1024x1024)
    static constexpr uint32_t TileDimensionPixels  = 128u;  // [px] - Pre-baked thumbnail tile extent (128x128)
    static constexpr uint32_t TilesPerRow          = SheetDimensionPixels / TileDimensionPixels; // [8]
    static constexpr uint32_t TilesPerPage         = TilesPerRow * TilesPerRow;                  // [64]

    ThumbnailSheetRepository();
    ~ThumbnailSheetRepository() noexcept;

    ThumbnailSheetRepository(const ThumbnailSheetRepository&)            = delete;
    ThumbnailSheetRepository& operator=(const ThumbnailSheetRepository&) = delete;

    [[nodiscard]] static uint32_t            QueryAllowedCategoryMask(WorkspaceCategory Workspace) noexcept;
    [[nodiscard]] static bool                QueryCategoryPermitted(WorkspaceCategory Workspace, ContentCategory Category) noexcept;
    [[nodiscard]] static uint32_t            ComputeWorkspacesAllowingCategory(ContentCategory Category) noexcept;
    [[nodiscard]] static const char*         QueryCategoryTitle(ContentCategory Category) noexcept;
    [[nodiscard]] static const char*         QueryCanonicalExtension(ContentCategory Category) noexcept;
    [[nodiscard]] static const char*         QueryWorkspaceTitle(WorkspaceCategory Workspace) noexcept;
    [[nodiscard]] static ContentCategory ClassifyExternalExtension(const std::filesystem::path& SourceFile) noexcept;

    void ConfigureImportPolicy(const ImportIntakeConfiguration& Policy);
    [[nodiscard]] const ImportIntakeConfiguration& QueryImportPolicy() const noexcept;
    [[nodiscard]] std::filesystem::path            ResolveQuarantineDirectory() const;
    [[nodiscard]] std::filesystem::path            ResolveFinalImportDirectory(ContentCategory Category) const;

    void PopulateDefaultEngineAndProjectEntries();

    std::size_t RegisterPrebakedEntry(
        std::string_view             DisplayTitle,
        ContentCategory          Category,
        ContentOriginFilter          Origin,
        ColorRgba                    PreviewTint,
        const std::filesystem::path& DiskPath = {},
        bool                         BakeImmediately = true);

    [[nodiscard]] bool SaveBinaryIndexAndSheets(
        const std::filesystem::path& IndexFilePath,
        const std::filesystem::path& SheetFilePath,
        std::string&                 OutDiagnostic) const;

    [[nodiscard]] bool LoadBinaryIndexHeaderOnly(
        const std::filesystem::path& IndexFilePath,
        const std::filesystem::path& SheetFilePath,
        std::string&                 OutDiagnostic);

    void RequestVisibleTileStreaming(
        std::span<const std::size_t>   VisibleEntryIndices,
        Threading::AssetStreamThread&  Streamer);

    [[nodiscard]] ImportIntakeResult IntakeExternalAssetDrop(
        const std::filesystem::path& DroppedSourceFile,
        WorkspaceCategory          ActiveWorkspace);

    [[nodiscard]] uint32_t MigrateScratchImportsToProject(
        const std::filesystem::path& SavedProjectRoot,
        std::string&                 OutDiagnostic);

    [[nodiscard]] std::vector<std::size_t> QueryFilteredEntries(
        WorkspaceCategory Workspace,
        ContentCategory Category,
        ContentOriginFilter Origin,
        std::string_view    SearchQuery = {}) const;

    [[nodiscard]] std::span<const ContentRepositoryEntry> QueryAllEntries() const noexcept;
    [[nodiscard]] const ContentRepositoryEntry*           QueryEntryAt(std::size_t EntryIndex) const noexcept;
    [[nodiscard]] ThumbnailUvQuad                         QueryTileUv(std::size_t EntryIndex) const noexcept;
    [[nodiscard]] uint32_t                                PageCount() const noexcept;

private:
    struct SheetPageTexture
    {
        ImTextureData        TextureStorage {}; // [-] - Dear ImGui RGBA32 texture backing (1024x1024)
        std::vector<uint8_t> BackingBytes   {}; // [B] - Persistent RGBA32 pixel storage for binary serialization
    };

    SheetPageTexture& EnsurePageAllocated(uint16_t PageOrdinal);
    void              BakeProceduralTileIntoPage(ContentRepositoryEntry& Entry);
    void              CopyTileBytesIntoPage(const ContentIndexEntry64& Header, std::span<const uint8_t> TileRgbaBytes);

    ImportIntakeConfiguration                     ImportPolicy      {}; // [-] - Active Engine/Project/Imported directory rules
    std::filesystem::path                         BoundSheetFile    {}; // [-] - Active ThumbnailSheet.ImageSpace file for streaming
    std::vector<ContentRepositoryEntry>           Entries           {}; // [-] - Indexed content entries
    std::vector<std::unique_ptr<SheetPageTexture>> SheetPages       {}; // [-] - Pre-baked 1024x1024 RGBA32 sheet pages
};

} // namespace Frontier
