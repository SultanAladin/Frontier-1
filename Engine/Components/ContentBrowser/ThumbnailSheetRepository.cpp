/*============================================================================================================================================
                                                       THUMBNAILSHEETREPOSITORY.CPP
============================================================================================================================================*/
// 🧩 Implementation of pre-baked ThumbnailSheet pages, flat 64-byte ContentIndex directory, workspace masks, and quarantine import intake

#include "Engine/Components/ContentBrowser/ThumbnailSheetRepository.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <fstream>
#include <mutex>

namespace Frontier
{

namespace
{

constexpr uint32_t CategoryBit(ContentCategory Category) noexcept
{
    return 1u << static_cast<uint32_t>(Category);
}

constexpr uint32_t WorkspaceBit(WorkspaceCategory Workspace) noexcept
{
    return 1u << static_cast<uint32_t>(Workspace);
}

[[nodiscard]] uint64_t ComputeFnv1a64(std::string_view Text) noexcept
{
    uint64_t Hash = 14695981039346656037ull;
    for (const unsigned char Byte : Text)
    {
        Hash ^= static_cast<uint64_t>(Byte);
        Hash *= 1099511628211ull;
    }
    return Hash;
}

[[nodiscard]] std::string LowercaseAscii(std::string_view Input)
{
    std::string Result;
    Result.reserve(Input.size());
    for (const unsigned char Ch : Input)
    {
        Result.push_back(static_cast<char>(std::tolower(Ch)));
    }
    return Result;
}

[[nodiscard]] uint8_t ClampChannel(float Intensity) noexcept
{
    const float Clamped = std::clamp(Intensity, 0.0f, 255.0f);
    return static_cast<uint8_t>(Clamped + 0.5f);
}

void WriteTilePixel(
    std::span<uint8_t> TileRgba,
    uint32_t           X,
    uint32_t           Y,
    uint8_t            Red,
    uint8_t            Green,
    uint8_t            Blue,
    uint8_t            Alpha = 255u) noexcept
{
    if (X >= ThumbnailSheetRepository::TileDimensionPixels || Y >= ThumbnailSheetRepository::TileDimensionPixels)
    {
        return;
    }
    const std::size_t Offset = (static_cast<std::size_t>(Y) * ThumbnailSheetRepository::TileDimensionPixels + X) * 4u;
    TileRgba[Offset + 0u]    = Red;
    TileRgba[Offset + 1u]    = Green;
    TileRgba[Offset + 2u]    = Blue;
    TileRgba[Offset + 3u]    = Alpha;
}

void SynthesizePrebakedTileRgba(
    ContentCategory Category,
    ColorRgba           Tint,
    std::span<uint8_t>  OutTileRgba) noexcept
{
    constexpr uint32_t Dim  = ThumbnailSheetRepository::TileDimensionPixels;
    constexpr float    Half = static_cast<float>(Dim) * 0.5f;

    // Dark studio backdrop inside every tile
    for (uint32_t Y = 0u; Y < Dim; ++Y)
    {
        for (uint32_t X = 0u; X < Dim; ++X)
        {
            const float Vignette = 1.0f - 0.25f * std::hypot((static_cast<float>(X) - Half) / Half, (static_cast<float>(Y) - Half) / Half);
            const uint8_t Bg     = ClampChannel(28.0f * Vignette);
            WriteTilePixel(OutTileRgba, X, Y, Bg, Bg, static_cast<uint8_t>(Bg + 3u), 255u);
        }
    }

    for (uint32_t Y = 0u; Y < Dim; ++Y)
    {
        for (uint32_t X = 0u; X < Dim; ++X)
        {
            const float Nx = (static_cast<float>(X) + 0.5f - Half) / Half;
            const float Ny = (static_cast<float>(Y) + 0.5f - Half) / Half;
            const float R  = std::hypot(Nx, Ny);

            switch (Category)
            {
                case ContentCategory::Materials:
                {
                    if (R <= 0.72f)
                    {
                        const float Snx   = Nx / 0.72f;
                        const float Sny   = Ny / 0.72f;
                        const float Snz   = std::sqrt(std::max(0.0f, 1.0f - Snx * Snx - Sny * Sny));
                        const float Diff  = std::max(0.12f, -0.48f * Snx - 0.58f * Sny + 0.66f * Snz);
                        const float Spec  = std::pow(std::max(0.0f, -0.32f * Snx - 0.42f * Sny + 0.85f * Snz), 28.0f);
                        const float Rim   = std::pow(1.0f - Snz, 3.0f) * 0.35f;
                        const float Edge  = std::clamp((0.72f - R) * 48.0f, 0.0f, 1.0f);
                        WriteTilePixel(
                            OutTileRgba,
                            X,
                            Y,
                            ClampChannel((Tint.Red * Diff + 245.0f * Spec + 180.0f * Rim) * Edge + 26.0f * (1.0f - Edge)),
                            ClampChannel((Tint.Green * Diff + 245.0f * Spec + 190.0f * Rim) * Edge + 26.0f * (1.0f - Edge)),
                            ClampChannel((Tint.Blue * Diff + 250.0f * Spec + 210.0f * Rim) * Edge + 29.0f * (1.0f - Edge)));
                    }
                    break;
                }

                case ContentCategory::Geometry:
                {
                    // Isometric 3-facet analytic solid hexagon
                    const float Ax = std::fabs(Nx);
                    if (Ax <= 0.60f && std::fabs(Ny) + Ax * 0.55f <= 0.74f)
                    {
                        float Shade = 0.72f;
                        if (Ny < -Ax * 0.45f)
                        {
                            Shade = 1.08f; // Top facet
                        }
                        else if (Nx < 0.0f)
                        {
                            Shade = 0.84f; // Left facet
                        }
                        else
                        {
                            Shade = 0.54f; // Right facet
                        }
                        WriteTilePixel(
                            OutTileRgba,
                            X,
                            Y,
                            ClampChannel(Tint.Red * Shade),
                            ClampChannel(Tint.Green * Shade),
                            ClampChannel(Tint.Blue * Shade));
                    }
                    break;
                }

                case ContentCategory::UVLayouts:
                {
                    if (std::fabs(Nx) <= 0.70f && std::fabs(Ny) <= 0.70f)
                    {
                        const int   CellX = static_cast<int>((Nx + 0.70f) / 0.35f);
                        const int   CellY = static_cast<int>((Ny + 0.70f) / 0.35f);
                        const bool  Light = ((CellX + CellY) & 1) == 0;
                        const bool  Seam  = (std::fabs(std::fabs(Nx) - 0.70f) < 0.035f) || (std::fabs(std::fabs(Ny) - 0.70f) < 0.035f) ||
                                            (std::fabs(Nx) < 0.022f) || (std::fabs(Ny) < 0.022f);
                        if (Seam)
                        {
                            WriteTilePixel(OutTileRgba, X, Y, 86u, 224u, 232u);
                        }
                        else if (Light)
                        {
                            WriteTilePixel(OutTileRgba, X, Y, Tint.Red, Tint.Green, Tint.Blue);
                        }
                        else
                        {
                            WriteTilePixel(OutTileRgba, X, Y, 48u, 52u, 58u);
                        }
                    }
                    break;
                }

                case ContentCategory::Textures:
                {
                    if (std::fabs(Nx) <= 0.70f && std::fabs(Ny) <= 0.70f)
                    {
                        if (Nx < 0.0f && Ny < 0.0f)
                        {
                            WriteTilePixel(OutTileRgba, X, Y, Tint.Red, Tint.Green, Tint.Blue); // Albedo
                        }
                        else if (Nx >= 0.0f && Ny < 0.0f)
                        {
                            WriteTilePixel(OutTileRgba, X, Y, 128u, 132u, 252u); // Tangent normal
                        }
                        else if (Nx < 0.0f && Ny >= 0.0f)
                        {
                            const uint8_t Rough = ClampChannel(90.0f + (Nx + 0.70f) * 140.0f);
                            WriteTilePixel(OutTileRgba, X, Y, Rough, Rough, Rough); // Roughness
                        }
                        else
                        {
                            WriteTilePixel(OutTileRgba, X, Y, 214u, 186u, 118u); // Metallic
                        }
                    }
                    break;
                }

                case ContentCategory::Images:
                {
                    if (std::fabs(Nx) <= 0.74f && std::fabs(Ny) <= 0.58f)
                    {
                        if (Ny > 0.14f)
                        {
                            WriteTilePixel(OutTileRgba, X, Y, 36u, 42u, 52u);
                        }
                        else
                        {
                            const float T = (Ny + 0.58f) / 0.72f;
                            WriteTilePixel(
                                OutTileRgba,
                                X,
                                Y,
                                ClampChannel(Tint.Red * (0.55f + 0.45f * T)),
                                ClampChannel(Tint.Green * (0.45f + 0.55f * T)),
                                ClampChannel(Tint.Blue * (0.75f - 0.25f * T)));
                            if (std::hypot(Nx - 0.26f, Ny + 0.18f) < 0.15f)
                            {
                                WriteTilePixel(OutTileRgba, X, Y, 252u, 224u, 146u);
                            }
                        }
                    }
                    break;
                }

                case ContentCategory::Vectors:
                {
                    const float CurveY = 0.36f * std::sin(Nx * 3.1f);
                    if (std::fabs(Nx) <= 0.72f && std::fabs(Ny - CurveY) < 0.065f)
                    {
                        WriteTilePixel(OutTileRgba, X, Y, Tint.Red, Tint.Green, Tint.Blue);
                    }
                    if (std::hypot(Nx + 0.55f, Ny - 0.36f * std::sin(-0.55f * 3.1f)) < 0.09f ||
                        std::hypot(Nx - 0.55f, Ny - 0.36f * std::sin(0.55f * 3.1f)) < 0.09f)
                    {
                        WriteTilePixel(OutTileRgba, X, Y, 245u, 245u, 248u);
                    }
                    break;
                }

                case ContentCategory::Fonts:
                {
                    // Bold geometric "A" stroke
                    const bool LeftLeg  = (Ny >= -0.50f && Ny <= 0.54f && std::fabs(Nx + (Ny + 0.50f) * 0.42f) < 0.075f);
                    const bool RightLeg = (Ny >= -0.50f && Ny <= 0.54f && std::fabs(Nx - (Ny + 0.50f) * 0.42f) < 0.075f);
                    const bool CrossBar = (std::fabs(Ny - 0.16f) < 0.065f && std::fabs(Nx) < 0.28f);
                    if (LeftLeg || RightLeg || CrossBar)
                    {
                        WriteTilePixel(OutTileRgba, X, Y, Tint.Red, Tint.Green, Tint.Blue);
                    }
                    break;
                }

                case ContentCategory::Simulations:
                {
                    // Concentric force-field / turbulent curl rings
                    if (R <= 0.74f && (std::fabs(R - 0.24f) < 0.045f || std::fabs(R - 0.46f) < 0.045f || std::fabs(R - 0.68f) < 0.045f))
                    {
                        WriteTilePixel(OutTileRgba, X, Y, Tint.Red, Tint.Green, Tint.Blue);
                    }
                    break;
                }

                case ContentCategory::Volumetrics:
                {
                    const float D1 = std::hypot(Nx + 0.20f, Ny - 0.06f);
                    const float D2 = std::hypot(Nx - 0.18f, Ny - 0.08f);
                    const float D3 = std::hypot(Nx, Ny + 0.14f);
                    const float Density = std::clamp(1.1f - std::min({ D1 / 0.38f, D2 / 0.36f, D3 / 0.42f }), 0.0f, 1.0f);
                    if (Density > 0.05f)
                    {
                        WriteTilePixel(
                            OutTileRgba,
                            X,
                            Y,
                            ClampChannel(28.0f + (Tint.Red - 28.0f) * Density),
                            ClampChannel(28.0f + (Tint.Green - 28.0f) * Density),
                            ClampChannel(31.0f + (Tint.Blue - 31.0f) * Density));
                    }
                    break;
                }

                case ContentCategory::Particles:
                {
                    const float Angle = std::atan2(Ny, Nx);
                    const float Spokes = std::fabs(std::sin(Angle * 6.0f));
                    if (R <= 0.72f && (R < 0.14f || (Spokes > 0.92f && R > 0.18f)))
                    {
                        WriteTilePixel(OutTileRgba, X, Y, Tint.Red, Tint.Green, Tint.Blue);
                    }
                    break;
                }

                case ContentCategory::Photometry:
                {
                    // Polar candela lobe below luminaire origin
                    const float Dy = Ny + 0.42f;
                    const float LobeR = std::hypot(Nx * 1.35f, Dy - 0.44f);
                    if (Dy >= 0.0f && std::fabs(LobeR - 0.42f) < 0.055f)
                    {
                        WriteTilePixel(OutTileRgba, X, Y, Tint.Red, Tint.Green, Tint.Blue);
                    }
                    break;
                }

                case ContentCategory::Audio:
                {
                    const int BarIdx = static_cast<int>((Nx + 0.72f) / 0.18f);
                    const float RelX = std::fmod(Nx + 0.72f, 0.18f);
                    constexpr std::array<float, 8> Heights = { 0.22f, 0.46f, 0.64f, 0.38f, 0.68f, 0.50f, 0.30f, 0.18f };
                    if (Nx >= -0.72f && Nx < 0.72f && BarIdx >= 0 && BarIdx < 8 && RelX < 0.11f && std::fabs(Ny) <= Heights[static_cast<std::size_t>(BarIdx)])
                    {
                        WriteTilePixel(OutTileRgba, X, Y, Tint.Red, Tint.Green, Tint.Blue);
                    }
                    break;
                }

                case ContentCategory::Shaders:
                {
                    const bool LeftChevron  = (Nx >= -0.64f && Nx <= -0.22f && std::fabs(std::fabs(Ny) - (Nx + 0.62f)) < 0.07f);
                    const bool RightChevron = (Nx >= 0.22f && Nx <= 0.64f && std::fabs(std::fabs(Ny) - (0.62f - Nx)) < 0.07f);
                    const bool Slash        = (std::fabs(Nx + Ny * 0.30f) < 0.055f && std::fabs(Ny) < 0.48f);
                    if (LeftChevron || RightChevron || Slash)
                    {
                        WriteTilePixel(OutTileRgba, X, Y, Tint.Red, Tint.Green, Tint.Blue);
                    }
                    break;
                }

                case ContentCategory::Colours:
                case ContentCategory::All:
                {
                    if (R <= 0.68f)
                    {
                        WriteTilePixel(OutTileRgba, X, Y, Tint.Red, Tint.Green, Tint.Blue);
                    }
                    break;
                }
            }
        }
    }
}

struct BinaryIndexFilePrefix
{
    char     Magic[8]         { 'F', 'C', 'I', 'D', 'X', '6', '4', '\0' };
    uint32_t Version          = 1u;
    uint32_t EntryCount       = 0u;
    uint32_t StringBytesTotal = 0u;
    uint32_t PageCount        = 0u;
    uint8_t  Reserved[40]     {};
};
static_assert(sizeof(BinaryIndexFilePrefix) == 64u, "BinaryIndexFilePrefix must equal 64 bytes");

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                        THUMBNAILSHEETREPOSITORY IMPLEMENTATION
//------------------------------------------------------------------------------------------------------------------------

ThumbnailSheetRepository::ThumbnailSheetRepository()
{
    ImportPolicy.EngineContentRoot  = "EngineContent";
    ImportPolicy.ScratchSessionRoot = std::filesystem::temp_directory_path() / "FrontierProjects/_Scratch";
    PopulateDefaultEngineAndProjectEntries();
}

ThumbnailSheetRepository::~ThumbnailSheetRepository() noexcept = default;

uint32_t ThumbnailSheetRepository::QueryAllowedCategoryMask(WorkspaceCategory Workspace) noexcept
{
    switch (Workspace)
    {
        case WorkspaceCategory::SceneWorld:
            return CategoryBit(ContentCategory::All) |
                   CategoryBit(ContentCategory::Colours) |
                   CategoryBit(ContentCategory::Geometry) |
                   CategoryBit(ContentCategory::UVLayouts) |
                   CategoryBit(ContentCategory::Materials) |
                   CategoryBit(ContentCategory::Textures) |
                   CategoryBit(ContentCategory::Images) |
                   CategoryBit(ContentCategory::Fonts) |
                   CategoryBit(ContentCategory::Simulations) |
                   CategoryBit(ContentCategory::Volumetrics) |
                   CategoryBit(ContentCategory::Particles) |
                   CategoryBit(ContentCategory::Photometry) |
                   CategoryBit(ContentCategory::Audio) |
                   CategoryBit(ContentCategory::Shaders);

        case WorkspaceCategory::UV:
            // 🔴 Strict UV workspace filter: only Geometry, UVLayouts, Colours, Materials, Textures, and Images exist
            return CategoryBit(ContentCategory::All) |
                   CategoryBit(ContentCategory::Colours) |
                   CategoryBit(ContentCategory::Geometry) |
                   CategoryBit(ContentCategory::UVLayouts) |
                   CategoryBit(ContentCategory::Materials) |
                   CategoryBit(ContentCategory::Textures) |
                   CategoryBit(ContentCategory::Images);

        case WorkspaceCategory::GeometryCad:
            return CategoryBit(ContentCategory::All) |
                   CategoryBit(ContentCategory::Colours) |
                   CategoryBit(ContentCategory::Geometry) |
                   CategoryBit(ContentCategory::UVLayouts) |
                   CategoryBit(ContentCategory::Materials) |
                   CategoryBit(ContentCategory::Images) |
                   CategoryBit(ContentCategory::Vectors) |
                   CategoryBit(ContentCategory::Fonts);

        case WorkspaceCategory::Material:
            return CategoryBit(ContentCategory::All) |
                   CategoryBit(ContentCategory::Colours) |
                   CategoryBit(ContentCategory::Geometry) |
                   CategoryBit(ContentCategory::Materials) |
                   CategoryBit(ContentCategory::Textures) |
                   CategoryBit(ContentCategory::Images) |
                   CategoryBit(ContentCategory::Volumetrics) |
                   CategoryBit(ContentCategory::Photometry) |
                   CategoryBit(ContentCategory::Shaders);

        case WorkspaceCategory::TexturePaint:
            return CategoryBit(ContentCategory::All) |
                   CategoryBit(ContentCategory::Colours) |
                   CategoryBit(ContentCategory::Geometry) |
                   CategoryBit(ContentCategory::UVLayouts) |
                   CategoryBit(ContentCategory::Materials) |
                   CategoryBit(ContentCategory::Textures) |
                   CategoryBit(ContentCategory::Images) |
                   CategoryBit(ContentCategory::Vectors) |
                   CategoryBit(ContentCategory::Fonts);

        case WorkspaceCategory::Image:
            return CategoryBit(ContentCategory::All) |
                   CategoryBit(ContentCategory::Colours) |
                   CategoryBit(ContentCategory::Textures) |
                   CategoryBit(ContentCategory::Images) |
                   CategoryBit(ContentCategory::Vectors) |
                   CategoryBit(ContentCategory::Fonts);

        case WorkspaceCategory::Vector:
            return CategoryBit(ContentCategory::All) |
                   CategoryBit(ContentCategory::Colours) |
                   CategoryBit(ContentCategory::Images) |
                   CategoryBit(ContentCategory::Vectors) |
                   CategoryBit(ContentCategory::Fonts);

        case WorkspaceCategory::Simulation:
            return CategoryBit(ContentCategory::All) |
                   CategoryBit(ContentCategory::Colours) |
                   CategoryBit(ContentCategory::Geometry) |
                   CategoryBit(ContentCategory::Simulations) |
                   CategoryBit(ContentCategory::Volumetrics) |
                   CategoryBit(ContentCategory::Particles) |
                   CategoryBit(ContentCategory::Shaders);

        case WorkspaceCategory::Particle:
            return CategoryBit(ContentCategory::All) |
                   CategoryBit(ContentCategory::Colours) |
                   CategoryBit(ContentCategory::Geometry) |
                   CategoryBit(ContentCategory::Materials) |
                   CategoryBit(ContentCategory::Textures) |
                   CategoryBit(ContentCategory::Images) |
                   CategoryBit(ContentCategory::Simulations) |
                   CategoryBit(ContentCategory::Volumetrics) |
                   CategoryBit(ContentCategory::Particles) |
                   CategoryBit(ContentCategory::Shaders);

        case WorkspaceCategory::Audio:
            return CategoryBit(ContentCategory::All) |
                   CategoryBit(ContentCategory::Audio);
    }
    return 0xFFFFFFFFu;
}

bool ThumbnailSheetRepository::QueryCategoryPermitted(
    WorkspaceCategory Workspace,
    ContentCategory Category) noexcept
{
    return (QueryAllowedCategoryMask(Workspace) & CategoryBit(Category)) != 0u;
}

uint32_t ThumbnailSheetRepository::ComputeWorkspacesAllowingCategory(ContentCategory Category) noexcept
{
    uint32_t Mask = 0u;
    for (uint8_t Ord = 0u; Ord <= static_cast<uint8_t>(WorkspaceCategory::Audio); ++Ord)
    {
        const auto Domain = static_cast<WorkspaceCategory>(Ord);
        if (QueryCategoryPermitted(Domain, Category))
        {
            Mask |= WorkspaceBit(Domain);
        }
    }
    return Mask;
}

const char* ThumbnailSheetRepository::QueryCategoryTitle(ContentCategory Category) noexcept
{
    switch (Category)
    {
        case ContentCategory::All:         return "All";
        case ContentCategory::Colours:     return "Colours";
        case ContentCategory::Geometry:    return "Geometry";
        case ContentCategory::UVLayouts:   return "UV Layouts";
        case ContentCategory::Materials:   return "Materials";
        case ContentCategory::Textures:    return "Textures";
        case ContentCategory::Images:      return "Images";
        case ContentCategory::Vectors:     return "Vectors";
        case ContentCategory::Fonts:       return "Fonts";
        case ContentCategory::Simulations: return "Simulations";
        case ContentCategory::Volumetrics: return "Volumetrics";
        case ContentCategory::Particles:   return "Particles";
        case ContentCategory::Photometry:  return "Photometry";
        case ContentCategory::Audio:       return "Audio";
        case ContentCategory::Shaders:     return "Shaders";
    }
    return "All";
}

const char* ThumbnailSheetRepository::QueryCanonicalExtension(ContentCategory Category) noexcept
{
    switch (Category)
    {
        case ContentCategory::All:         return ".Space";
        case ContentCategory::Colours:     return ".ColourSwatch";
        case ContentCategory::Geometry:    return ".GeometrySpace";
        case ContentCategory::UVLayouts:   return ".UVSpace";
        case ContentCategory::Materials:   return ".MaterialSpace";
        case ContentCategory::Textures:    return ".TextureSpace";
        case ContentCategory::Images:      return ".ImageSpace";
        case ContentCategory::Vectors:     return ".VectorSpace";
        case ContentCategory::Fonts:       return ".FontSpace";
        case ContentCategory::Simulations: return ".SimulationSpace";
        case ContentCategory::Volumetrics: return ".VolumetricSpace";
        case ContentCategory::Particles:   return ".ParticleSpace";
        case ContentCategory::Photometry:  return ".Photometry";
        case ContentCategory::Audio:       return ".Acoustic";
        case ContentCategory::Shaders:     return ".slang";
    }
    return ".Space";
}

const char* ThumbnailSheetRepository::QueryWorkspaceTitle(WorkspaceCategory Workspace) noexcept
{
    switch (Workspace)
    {
        case WorkspaceCategory::SceneWorld:   return "Scene";
        case WorkspaceCategory::UV:           return "UV";
        case WorkspaceCategory::GeometryCad:  return "Geometry";
        case WorkspaceCategory::Material:     return "Material";
        case WorkspaceCategory::TexturePaint: return "Texture";
        case WorkspaceCategory::Image:        return "Image";
        case WorkspaceCategory::Vector:       return "Vector";
        case WorkspaceCategory::Simulation:   return "Simulation";
        case WorkspaceCategory::Particle:     return "Particle";
        case WorkspaceCategory::Audio:        return "Audio";
    }
    return "Scene";
}

ContentCategory ThumbnailSheetRepository::ClassifyExternalExtension(const std::filesystem::path& SourceFile) noexcept
{
    const std::string Ext = LowercaseAscii(SourceFile.extension().string());
    if (Ext == ".colourswatch" || Ext == ".colourspace" || Ext == ".toml")
    {
        return ContentCategory::Colours;
    }
    if (Ext == ".gltf" || Ext == ".glb" || Ext == ".fbx" || Ext == ".obj" || Ext == ".step" || Ext == ".geometryspace" || Ext == ".assembly" || Ext == ".sketch")
    {
        return ContentCategory::Geometry;
    }
    if (Ext == ".uvspace")
    {
        return ContentCategory::UVLayouts;
    }
    if (Ext == ".mtlx" || Ext == ".materialspace")
    {
        return ContentCategory::Materials;
    }
    if (Ext == ".ktx2" || Ext == ".dds" || Ext == ".texturespace" || Ext == ".pigment")
    {
        return ContentCategory::Textures;
    }
    if (Ext == ".png" || Ext == ".jpg" || Ext == ".jpeg" || Ext == ".exr" || Ext == ".hdr" || Ext == ".imagespace")
    {
        return ContentCategory::Images;
    }
    if (Ext == ".svg" || Ext == ".vectorspace")
    {
        return ContentCategory::Vectors;
    }
    if (Ext == ".ttf" || Ext == ".otf" || Ext == ".fontspace")
    {
        return ContentCategory::Fonts;
    }
    if (Ext == ".simulationspace")
    {
        return ContentCategory::Simulations;
    }
    if (Ext == ".vdb" || Ext == ".volumetricspace")
    {
        return ContentCategory::Volumetrics;
    }
    if (Ext == ".particlespace")
    {
        return ContentCategory::Particles;
    }
    if (Ext == ".ies" || Ext == ".photometry")
    {
        return ContentCategory::Photometry;
    }
    if (Ext == ".wav" || Ext == ".flac" || Ext == ".ogg" || Ext == ".acoustic")
    {
        return ContentCategory::Audio;
    }
    if (Ext == ".slang" || Ext == ".render")
    {
        return ContentCategory::Shaders;
    }
    return ContentCategory::Geometry;
}

void ThumbnailSheetRepository::ConfigureImportPolicy(const ImportIntakeConfiguration& Policy)
{
    ImportPolicy = Policy;
    if (ImportPolicy.ScratchSessionRoot.empty())
    {
        ImportPolicy.ScratchSessionRoot = std::filesystem::temp_directory_path() / "FrontierProjects/_Scratch";
    }
}

const ImportIntakeConfiguration& ThumbnailSheetRepository::QueryImportPolicy() const noexcept
{
    return ImportPolicy;
}

std::filesystem::path ThumbnailSheetRepository::ResolveQuarantineDirectory() const
{
    if (!ImportPolicy.ActiveProjectRoot.empty() && ImportPolicy.Mode == ImportDestinationMode::ProjectImportedFolder)
    {
        return ImportPolicy.ActiveProjectRoot / "Saved/ImportIntake";
    }
    return ImportPolicy.ScratchSessionRoot / "Saved/ImportIntake";
}

std::filesystem::path ThumbnailSheetRepository::ResolveFinalImportDirectory(ContentCategory Category) const
{
    const char* CategoryFolder = QueryCategoryTitle(Category);
    if (ImportPolicy.Mode == ImportDestinationMode::UserCustomFolder && !ImportPolicy.CustomImportDirectory.empty())
    {
        return ImportPolicy.CustomImportDirectory / CategoryFolder;
    }
    if (ImportPolicy.Mode == ImportDestinationMode::ProjectImportedFolder && !ImportPolicy.ActiveProjectRoot.empty())
    {
        return ImportPolicy.ActiveProjectRoot / "Content/Imported" / CategoryFolder;
    }
    // Fallback when unsaved or when ScratchTempFolder mode is chosen explicitly by user
    return ImportPolicy.ScratchSessionRoot / "Saved/Imported" / CategoryFolder;
}

ThumbnailSheetRepository::SheetPageTexture& ThumbnailSheetRepository::EnsurePageAllocated(uint16_t PageOrdinal)
{
    while (SheetPages.size() <= PageOrdinal)
    {
        auto Page = std::make_unique<SheetPageTexture>();
        const std::size_t TotalBytes = static_cast<std::size_t>(SheetDimensionPixels) * SheetDimensionPixels * 4u;
        Page->BackingBytes.assign(TotalBytes, 0u);
        Page->TextureStorage.Create(ImTextureFormat_RGBA32, static_cast<int>(SheetDimensionPixels), static_cast<int>(SheetDimensionPixels));
        std::memset(Page->TextureStorage.Pixels, 0, TotalBytes);
        Page->TextureStorage.UseColors = true;
        SheetPages.push_back(std::move(Page));
    }
    return *SheetPages[PageOrdinal];
}

void ThumbnailSheetRepository::CopyTileBytesIntoPage(
    const ContentIndexEntry64& Header,
    std::span<const uint8_t>   TileRgbaBytes)
{
    const std::size_t ExpectedBytes = static_cast<std::size_t>(TileDimensionPixels) * TileDimensionPixels * 4u;
    if (TileRgbaBytes.size() < ExpectedBytes)
    {
        return;
    }

    SheetPageTexture& Page  = EnsurePageAllocated(Header.SheetPageOrdinal);
    const uint32_t    DestX = static_cast<uint32_t>(Header.TileColumn) * TileDimensionPixels;
    const uint32_t    DestY = static_cast<uint32_t>(Header.TileRow) * TileDimensionPixels;

    for (uint32_t Row = 0u; Row < TileDimensionPixels; ++Row)
    {
        const std::size_t SrcOffset = static_cast<std::size_t>(Row) * TileDimensionPixels * 4u;
        const std::size_t DstOffset = (static_cast<std::size_t>(DestY + Row) * SheetDimensionPixels + DestX) * 4u;
        std::memcpy(Page.BackingBytes.data() + DstOffset, TileRgbaBytes.data() + SrcOffset, TileDimensionPixels * 4u);
        if (Page.TextureStorage.Pixels != nullptr)
        {
            std::memcpy(Page.TextureStorage.Pixels + DstOffset, TileRgbaBytes.data() + SrcOffset, TileDimensionPixels * 4u);
        }
    }

    ImTextureData& Texture = Page.TextureStorage;
    if (Texture.Status == ImTextureStatus_OK || Texture.Status == ImTextureStatus_WantUpdates)
    {
        const uint32_t X0 = Texture.Status == ImTextureStatus_WantUpdates
            ? std::min<uint32_t>(Texture.UpdateRect.x, DestX) : DestX;
        const uint32_t Y0 = Texture.Status == ImTextureStatus_WantUpdates
            ? std::min<uint32_t>(Texture.UpdateRect.y, DestY) : DestY;
        const uint32_t X1 = Texture.Status == ImTextureStatus_WantUpdates
            ? std::max<uint32_t>(Texture.UpdateRect.x + Texture.UpdateRect.w, DestX + TileDimensionPixels)
            : DestX + TileDimensionPixels;
        const uint32_t Y1 = Texture.Status == ImTextureStatus_WantUpdates
            ? std::max<uint32_t>(Texture.UpdateRect.y + Texture.UpdateRect.h, DestY + TileDimensionPixels)
            : DestY + TileDimensionPixels;
        Texture.UpdateRect = {
            static_cast<unsigned short>(X0),
            static_cast<unsigned short>(Y0),
            static_cast<unsigned short>(X1 - X0),
            static_cast<unsigned short>(Y1 - Y0),
        };
        Texture.SetStatus(ImTextureStatus_WantUpdates);
    }
}

void ThumbnailSheetRepository::BakeProceduralTileIntoPage(ContentRepositoryEntry& Entry)
{
    std::vector<uint8_t> TileRgba(static_cast<std::size_t>(TileDimensionPixels) * TileDimensionPixels * 4u, 0u);
    SynthesizePrebakedTileRgba(
        static_cast<ContentCategory>(Entry.IndexHeader.CategoryCode),
        ColorRgba{ Entry.IndexHeader.TintRed, Entry.IndexHeader.TintGreen, Entry.IndexHeader.TintBlue, 255u },
        TileRgba);
    CopyTileBytesIntoPage(Entry.IndexHeader, TileRgba);
    Entry.TileResident = true;
}

std::size_t ThumbnailSheetRepository::RegisterPrebakedEntry(
    std::string_view             DisplayTitle,
    ContentCategory          Category,
    ContentOriginFilter          Origin,
    ColorRgba                    PreviewTint,
    const std::filesystem::path& DiskPath,
    bool                         BakeImmediately)
{
    const std::size_t SlotOrdinal = Entries.size();
    const uint16_t    PageOrd     = static_cast<uint16_t>(SlotOrdinal / TilesPerPage);
    const uint32_t    InPageIdx   = static_cast<uint32_t>(SlotOrdinal % TilesPerPage);
    const uint16_t    Col         = static_cast<uint16_t>(InPageIdx % TilesPerRow);
    const uint16_t    Row         = static_cast<uint16_t>(InPageIdx / TilesPerRow);
    const uint32_t    TileBytes   = TileDimensionPixels * TileDimensionPixels * 4u;

    ContentRepositoryEntry Entry{};
    Entry.DisplayTitle    = std::string(DisplayTitle);
    Entry.ContainerFormat = QueryCanonicalExtension(Category);
    Entry.DiskPath        = DiskPath.empty()
                                ? (ImportPolicy.EngineContentRoot / QueryCategoryTitle(Category) / (Entry.DisplayTitle + Entry.ContainerFormat))
                                : DiskPath;

    Entry.IndexHeader.ContentHash64              = ComputeFnv1a64(Entry.DiskPath.generic_string() + "#" + Entry.DisplayTitle);
    Entry.IndexHeader.WorkspaceCompatibilityMask = ComputeWorkspacesAllowingCategory(Category);
    Entry.IndexHeader.SheetSliceByteOffset       = 64ull + static_cast<uint64_t>(SlotOrdinal) * TileBytes;
    Entry.IndexHeader.SheetSliceByteLength       = TileBytes;
    Entry.IndexHeader.SheetPageOrdinal           = PageOrd;
    Entry.IndexHeader.TileColumn                 = Col;
    Entry.IndexHeader.TileRow                    = Row;
    Entry.IndexHeader.TilePixelExtent            = static_cast<uint16_t>(TileDimensionPixels);
    Entry.IndexHeader.CategoryCode              = static_cast<uint8_t>(Category);
    Entry.IndexHeader.OriginCode                = static_cast<uint8_t>(Origin);
    Entry.IndexHeader.TintRed                    = PreviewTint.Red;
    Entry.IndexHeader.TintGreen                  = PreviewTint.Green;
    Entry.IndexHeader.TintBlue                   = PreviewTint.Blue;
    Entry.IndexHeader.ReadOnlyProtected          = (Origin == ContentOriginFilter::Engine) ? 1u : 0u;

    if (BakeImmediately)
    {
        BakeProceduralTileIntoPage(Entry);
    }

    Entries.push_back(std::move(Entry));
    return Entries.size() - 1u;
}

std::size_t ThumbnailSheetRepository::RegisterStudioMatCapEntry(
    uint8_t                  MatCapSlot,
    std::string_view         DisplayTitle,
    ColorRgba                PreviewTint,
    std::span<const uint8_t> StudioAtlasRgba)
{
    const std::filesystem::path EntryPath =
        ImportPolicy.EngineContentRoot / "MatCaps" / (std::string(DisplayTitle) + ".MaterialSpace");

    const std::size_t SlotOrdinal = RegisterPrebakedEntry(
        DisplayTitle,
        ContentCategory::Materials,
        ContentOriginFilter::Engine,
        PreviewTint,
        EntryPath,
        false);

    ContentRepositoryEntry& Entry               = Entries[SlotOrdinal];
    Entry.IndexHeader.MatCapPresetMarker        = 1u;
    Entry.IndexHeader.MatCapSlotOrdinal         = static_cast<uint8_t>(MatCapSlot & 15u);

    constexpr std::size_t AtlasByteCount = 1024u * 1024u * 4u;
    if (StudioAtlasRgba.size() >= AtlasByteCount)
    {
        constexpr uint32_t Dim  = TileDimensionPixels;
        constexpr float    Half = static_cast<float>(Dim) * 0.5f;
        std::vector<uint8_t> TileRgba(static_cast<std::size_t>(Dim) * Dim * 4u, 0u);

        const uint32_t AtlasTileX = (static_cast<uint32_t>(MatCapSlot & 15u) % 4u) * 256u;
        const uint32_t AtlasTileY = (static_cast<uint32_t>(MatCapSlot & 15u) / 4u) * 256u;

        for (uint32_t Y = 0u; Y < Dim; ++Y)
        {
            for (uint32_t X = 0u; X < Dim; ++X)
            {
                const float Nx       = (static_cast<float>(X) + 0.5f - Half) / Half;
                const float Ny       = (static_cast<float>(Y) + 0.5f - Half) / Half;
                const float Radius   = std::hypot(Nx, Ny);
                const float Vignette = 1.0f - 0.25f * Radius;
                const float Bg       = 28.0f * Vignette;

                if (Radius <= 0.80f)
                {
                    const float SphereU = std::clamp((Nx / 0.80f) * 0.485f + 0.5f, 0.0f, 0.999f);
                    const float SphereV = std::clamp((Ny / 0.80f) * 0.485f + 0.5f, 0.0f, 0.999f);
                    const uint32_t SrcX = AtlasTileX + std::min(254u, static_cast<uint32_t>(SphereU * 255.0f));
                    const uint32_t SrcY = AtlasTileY + std::min(254u, static_cast<uint32_t>(SphereV * 255.0f));

                    float SumR = 0.0f;
                    float SumG = 0.0f;
                    float SumB = 0.0f;
                    for (uint32_t Dy = 0u; Dy < 2u; ++Dy)
                    {
                        for (uint32_t Dx = 0u; Dx < 2u; ++Dx)
                        {
                            const std::size_t Off = (static_cast<std::size_t>(SrcY + Dy) * 1024u + (SrcX + Dx)) * 4u;
                            SumR += static_cast<float>(StudioAtlasRgba[Off + 0u]);
                            SumG += static_cast<float>(StudioAtlasRgba[Off + 1u]);
                            SumB += static_cast<float>(StudioAtlasRgba[Off + 2u]);
                        }
                    }
                    const float EdgeCoverage = std::clamp((0.80f - Radius) * 44.0f, 0.0f, 1.0f);
                    WriteTilePixel(
                        TileRgba,
                        X,
                        Y,
                        ClampChannel((SumR * 0.25f) * EdgeCoverage + Bg * (1.0f - EdgeCoverage)),
                        ClampChannel((SumG * 0.25f) * EdgeCoverage + Bg * (1.0f - EdgeCoverage)),
                        ClampChannel((SumB * 0.25f) * EdgeCoverage + (Bg + 3.0f) * (1.0f - EdgeCoverage)),
                        255u);
                }
                else
                {
                    WriteTilePixel(TileRgba, X, Y, ClampChannel(Bg), ClampChannel(Bg), ClampChannel(Bg + 3.0f), 255u);
                }
            }
        }
        CopyTileBytesIntoPage(Entry.IndexHeader, TileRgba);
        Entry.TileResident = true;
    }
    else
    {
        BakeProceduralTileIntoPage(Entry);
    }

    return SlotOrdinal;
}

void ThumbnailSheetRepository::PopulateDefaultEngineAndProjectEntries()
{
    Entries.clear();
    SheetPages.clear();

    struct MatCapSeed
    {
        uint8_t     Slot;
        const char* Title;
        ColorRgba   Tint;
    };

    constexpr std::array<MatCapSeed, 16> StudioMatCaps = {{
        { 0u,  "CNC Aluminum",     { 196u, 202u, 210u, 255u } },
        { 1u,  "Grade-5 Titanium", { 172u, 178u, 188u, 255u } },
        { 2u,  "CAD Clay",         { 198u, 168u, 152u, 255u } },
        { 3u,  "Class-A Chrome",   { 218u, 226u, 236u, 255u } },
        { 4u,  "Gunmetal Ano",     { 96u,  104u, 118u, 255u } },
        { 5u,  "Steel Blue",       { 92u,  144u, 198u, 255u } },
        { 6u,  "Turned Brass",     { 214u, 178u, 96u,  255u } },
        { 7u,  "Zebra CAD",        { 226u, 228u, 232u, 255u } },
        { 8u,  "Vulcan Rubber",    { 76u,  80u,  88u,  255u } },
        { 9u,  "Normal Map",       { 132u, 138u, 244u, 255u } },
        { 10u, "Car Flake Blue",   { 56u,  122u, 224u, 255u } },
        { 11u, "Car Flake Ruby",   { 206u, 52u,  74u,  255u } },
        { 12u, "Carbon Fibre",     { 84u,  88u,  96u,  255u } },
        { 13u, "Woven Fabric",     { 156u, 146u, 134u, 255u } },
        { 14u, "Velvet Suede",     { 138u, 96u,  126u, 255u } },
        { 15u, "Ripstop Nylon",    { 112u, 132u, 118u, 255u } },
    }};

    std::vector<uint8_t> StudioAtlasBytes{};
    {
        const std::array<std::filesystem::path, 4> CandidatePaths = {{
            ImportPolicy.EngineContentRoot / "MatCaps/MatCapStudioAtlas.rgba",
            "EngineContent/MatCaps/MatCapStudioAtlas.rgba",
            "../EngineContent/MatCaps/MatCapStudioAtlas.rgba",
            "../../EngineContent/MatCaps/MatCapStudioAtlas.rgba",
        }};
        constexpr std::size_t ExpectedAtlasBytes = 1024u * 1024u * 4u;
        for (const std::filesystem::path& Candidate : CandidatePaths)
        {
            std::error_code IoErr{};
            if (std::filesystem::exists(Candidate, IoErr) && std::filesystem::file_size(Candidate, IoErr) >= ExpectedAtlasBytes)
            {
                std::ifstream AtlasIn(Candidate, std::ios::binary);
                if (AtlasIn.is_open())
                {
                    StudioAtlasBytes.resize(ExpectedAtlasBytes);
                    AtlasIn.read(reinterpret_cast<char*>(StudioAtlasBytes.data()), static_cast<std::streamsize>(ExpectedAtlasBytes));
                    if (!AtlasIn.good())
                    {
                        StudioAtlasBytes.clear();
                    }
                    else
                    {
                        break;
                    }
                }
            }
        }
    }

    for (const MatCapSeed& Seed : StudioMatCaps)
    {
        (void)RegisterStudioMatCapEntry(Seed.Slot, Seed.Title, Seed.Tint, StudioAtlasBytes);
    }

    struct SeedEntry
    {
        const char*         Title;
        ContentCategory Category;
        ContentOriginFilter Origin;
        ColorRgba           Tint;
    };

    constexpr std::array<SeedEntry, 29> StockEntries = {{
        // Materials (EngineContent/Materials/)
        { "Standard Preset",      ContentCategory::Materials,   ContentOriginFilter::Engine,  { 199u, 173u, 160u, 255u } },
        { "Dielectric Glass",     ContentCategory::Materials,   ContentOriginFilter::Engine,  { 176u, 222u, 242u, 255u } },
        { "Brushed Titanium",     ContentCategory::Materials,   ContentOriginFilter::Engine,  { 188u, 194u, 204u, 255u } },
        { "Automotive Clearcoat", ContentCategory::Materials,   ContentOriginFilter::Engine,  { 204u, 52u,  62u,  255u } },
        { "Subsurface Wax",       ContentCategory::Materials,   ContentOriginFilter::Project, { 228u, 188u, 146u, 255u } },
        // Geometry (EngineContent/Worlds/ & Project)
        { "Calibration BevelBox", ContentCategory::Geometry,    ContentOriginFilter::Engine,  { 184u, 196u, 214u, 255u } },
        { "Controlled Integrator", ContentCategory::Geometry,   ContentOriginFilter::Engine,  { 196u, 209u, 181u, 255u } },
        { "Aero Coupe GT",         ContentCategory::Geometry,   ContentOriginFilter::Engine,  { 158u, 188u, 204u, 255u } },
        { "Tactical Crate",        ContentCategory::Geometry,   ContentOriginFilter::Engine,  { 194u, 171u, 137u, 255u } },
        { "Armored Bust",          ContentCategory::Geometry,   ContentOriginFilter::Engine,  { 177u, 153u, 145u, 255u } },
        { "UDIM Arch Kit",         ContentCategory::Geometry,   ContentOriginFilter::Engine,  { 193u, 190u, 161u, 255u } },
        { "Shader Ball Solid",    ContentCategory::Geometry,    ContentOriginFilter::Engine,  { 212u, 192u, 166u, 255u } },
        { "Modular Hull Assembly",ContentCategory::Geometry,    ContentOriginFilter::Project, { 152u, 198u, 178u, 255u } },
        // UV Layouts (.UVSpace)
        { "UDIM 1001..1004 Grid", ContentCategory::UVLayouts,   ContentOriginFilter::Engine,  { 118u, 196u, 216u, 255u } },
        { "TrimSheet Atlas UV",   ContentCategory::UVLayouts,   ContentOriginFilter::Project, { 216u, 178u, 112u, 255u } },
        // Textures (.TextureSpace)
        { "Brushed Aniso Pack",   ContentCategory::Textures,    ContentOriginFilter::Engine,  { 174u, 184u, 198u, 255u } },
        { "Weathered Concrete",   ContentCategory::Textures,    ContentOriginFilter::Project, { 164u, 158u, 148u, 255u } },
        // Images (.ImageSpace)
        { "Studio Courtyard HDRI",ContentCategory::Images,      ContentOriginFilter::Engine,  { 234u, 176u, 118u, 255u } },
        { "ColorChecker Chart",   ContentCategory::Images,      ContentOriginFilter::Engine,  { 126u, 186u, 232u, 255u } },
        // Vectors & Fonts
        { "UI Contour Stencil",   ContentCategory::Vectors,     ContentOriginFilter::Engine,  { 226u, 198u, 146u, 255u } },
        { "Space Grotesk Face",   ContentCategory::Fonts,       ContentOriginFilter::Engine,  { 232u, 224u, 210u, 255u } },
        { "JetBrains Mono Face",  ContentCategory::Fonts,       ContentOriginFilter::Engine,  { 168u, 206u, 238u, 255u } },
        // Simulations, Volumetrics, Particles, Photometry, Audio, Shaders
        { "Turbulent Wind Field", ContentCategory::Simulations, ContentOriginFilter::Engine,  { 102u, 208u, 196u, 255u } },
        { "Voronoi Fracture Rig", ContentCategory::Simulations, ContentOriginFilter::Project, { 232u, 148u, 96u,  255u } },
        { "Cumulus Density SDF",  ContentCategory::Volumetrics, ContentOriginFilter::Engine,  { 198u, 214u, 232u, 255u } },
        { "Ember Fountain GPU",   ContentCategory::Particles,   ContentOriginFilter::Engine,  { 250u, 166u, 68u,  255u } },
        { "Downlight Narrow IES", ContentCategory::Photometry,  ContentOriginFilter::Engine,  { 248u, 224u, 134u, 255u } },
        { "Turbine Loop Bus",     ContentCategory::Audio,       ContentOriginFilter::Engine,  { 124u, 218u, 156u, 255u } },
        { "MicrofacetGGX.slang",  ContentCategory::Shaders,     ContentOriginFilter::Engine,  { 186u, 156u, 238u, 255u } },
    }};

    for (const SeedEntry& Seed : StockEntries)
    {
        const std::filesystem::path RootDir =
            (Seed.Origin == ContentOriginFilter::Engine)
                ? ImportPolicy.EngineContentRoot
                : (ImportPolicy.ActiveProjectRoot.empty() ? std::filesystem::path("Projects/SampleProject/Content") : (ImportPolicy.ActiveProjectRoot / "Content"));
        const std::filesystem::path EntryPath =
            RootDir / QueryCategoryTitle(Seed.Category) / (std::string(Seed.Title) + QueryCanonicalExtension(Seed.Category));
        (void)RegisterPrebakedEntry(Seed.Title, Seed.Category, Seed.Origin, Seed.Tint, EntryPath, true);
    }
}

bool ThumbnailSheetRepository::SaveBinaryIndexAndSheets(
    const std::filesystem::path& IndexFilePath,
    const std::filesystem::path& SheetFilePath,
    std::string&                 OutDiagnostic) const
{
    std::error_code IoErr{};
    if (IndexFilePath.has_parent_path())
    {
        std::filesystem::create_directories(IndexFilePath.parent_path(), IoErr);
    }
    if (SheetFilePath.has_parent_path())
    {
        std::filesystem::create_directories(SheetFilePath.parent_path(), IoErr);
    }

    std::vector<ContentIndexEntry64> PackedHeaders;
    PackedHeaders.reserve(Entries.size());
    std::string StringBlob;

    for (const ContentRepositoryEntry& Entry : Entries)
    {
        ContentIndexEntry64 Header        = Entry.IndexHeader;
        Header.TitleStringOffset          = static_cast<uint32_t>(StringBlob.size());
        StringBlob.append(Entry.DisplayTitle);
        StringBlob.push_back('\0');
        Header.RelativePathStringOffset   = static_cast<uint32_t>(StringBlob.size());
        StringBlob.append(Entry.DiskPath.generic_string());
        StringBlob.push_back('\0');
        PackedHeaders.push_back(Header);
    }

    BinaryIndexFilePrefix Prefix{};
    Prefix.EntryCount       = static_cast<uint32_t>(PackedHeaders.size());
    Prefix.StringBytesTotal = static_cast<uint32_t>(StringBlob.size());
    Prefix.PageCount        = static_cast<uint32_t>(SheetPages.size());

    {
        std::ofstream IndexOut(IndexFilePath, std::ios::binary | std::ios::trunc);
        if (!IndexOut.is_open())
        {
            OutDiagnostic = "Unable to write ContentIndex.Space: " + IndexFilePath.string();
            return false;
        }
        IndexOut.write(reinterpret_cast<const char*>(&Prefix), sizeof(Prefix));
        if (!PackedHeaders.empty())
        {
            IndexOut.write(
                reinterpret_cast<const char*>(PackedHeaders.data()),
                static_cast<std::streamsize>(PackedHeaders.size() * sizeof(ContentIndexEntry64)));
        }
        if (!StringBlob.empty())
        {
            IndexOut.write(StringBlob.data(), static_cast<std::streamsize>(StringBlob.size()));
        }
    }

    {
        std::ofstream SheetOut(SheetFilePath, std::ios::binary | std::ios::trunc);
        if (!SheetOut.is_open())
        {
            OutDiagnostic = "Unable to write ThumbnailSheet.ImageSpace: " + SheetFilePath.string();
            return false;
        }
        char SheetMagic[64]{};
        std::memcpy(SheetMagic, "FRONTIER_THUMBNAIL_SHEET_V1", 27u);
        SheetOut.write(SheetMagic, sizeof(SheetMagic));

        const uint32_t TileBytes = TileDimensionPixels * TileDimensionPixels * 4u;
        std::vector<uint8_t> ScratchTile(TileBytes, 0u);
        for (const ContentRepositoryEntry& Entry : Entries)
        {
            const uint16_t PageOrd = Entry.IndexHeader.SheetPageOrdinal;
            if (PageOrd < SheetPages.size())
            {
                const SheetPageTexture& Page = *SheetPages[PageOrd];
                const uint32_t SrcX = static_cast<uint32_t>(Entry.IndexHeader.TileColumn) * TileDimensionPixels;
                const uint32_t SrcY = static_cast<uint32_t>(Entry.IndexHeader.TileRow) * TileDimensionPixels;
                for (uint32_t Row = 0u; Row < TileDimensionPixels; ++Row)
                {
                    const std::size_t SrcOff = (static_cast<std::size_t>(SrcY + Row) * SheetDimensionPixels + SrcX) * 4u;
                    const std::size_t DstOff = static_cast<std::size_t>(Row) * TileDimensionPixels * 4u;
                    std::memcpy(ScratchTile.data() + DstOff, Page.BackingBytes.data() + SrcOff, TileDimensionPixels * 4u);
                }
            }
            SheetOut.write(reinterpret_cast<const char*>(ScratchTile.data()), static_cast<std::streamsize>(TileBytes));
        }
    }

    OutDiagnostic.clear();
    return true;
}

bool ThumbnailSheetRepository::LoadBinaryIndexHeaderOnly(
    const std::filesystem::path& IndexFilePath,
    const std::filesystem::path& SheetFilePath,
    std::string&                 OutDiagnostic)
{
    std::ifstream IndexIn(IndexFilePath, std::ios::binary);
    if (!IndexIn.is_open())
    {
        OutDiagnostic = "Unable to open ContentIndex.Space: " + IndexFilePath.string();
        return false;
    }

    BinaryIndexFilePrefix Prefix{};
    IndexIn.read(reinterpret_cast<char*>(&Prefix), sizeof(Prefix));
    if (!IndexIn.good() || std::strncmp(Prefix.Magic, "FCIDX64", 7u) != 0)
    {
        OutDiagnostic = "Invalid ContentIndex.Space 64-byte header.";
        return false;
    }

    std::vector<ContentIndexEntry64> Headers(Prefix.EntryCount);
    if (Prefix.EntryCount > 0u)
    {
        IndexIn.read(
            reinterpret_cast<char*>(Headers.data()),
            static_cast<std::streamsize>(Headers.size() * sizeof(ContentIndexEntry64)));
    }

    std::string StringBlob(Prefix.StringBytesTotal, '\0');
    if (Prefix.StringBytesTotal > 0u)
    {
        IndexIn.read(StringBlob.data(), static_cast<std::streamsize>(StringBlob.size()));
    }

    Entries.clear();
    SheetPages.clear();
    BoundSheetFile = SheetFilePath;

    Entries.reserve(Headers.size());
    for (const ContentIndexEntry64& Header : Headers)
    {
        ContentRepositoryEntry Entry{};
        Entry.IndexHeader     = Header;
        const auto Category   = static_cast<ContentCategory>(Header.CategoryCode);
        Entry.ContainerFormat = QueryCanonicalExtension(Category);
        if (Header.TitleStringOffset < StringBlob.size())
        {
            Entry.DisplayTitle = std::string(StringBlob.c_str() + Header.TitleStringOffset);
        }
        if (Header.RelativePathStringOffset < StringBlob.size())
        {
            Entry.DiskPath = std::string(StringBlob.c_str() + Header.RelativePathStringOffset);
        }
        Entry.TileResident = false;
        (void)EnsurePageAllocated(Header.SheetPageOrdinal);
        Entries.push_back(std::move(Entry));
    }

    OutDiagnostic.clear();
    return true;
}

void ThumbnailSheetRepository::RequestVisibleTileStreaming(
    std::span<const std::size_t>  VisibleEntryIndices,
    Threading::AssetStreamThread& Streamer)
{
    if (BoundSheetFile.empty())
    {
        return;
    }

    std::mutex PageWriteMutex{};
    Threading::CompletionCounter Counter{};

    uint16_t Rank = 0u;
    for (const std::size_t EntryIdx : VisibleEntryIndices)
    {
        if (EntryIdx >= Entries.size() || Entries[EntryIdx].TileResident)
        {
            continue;
        }

        ContentRepositoryEntry& Entry = Entries[EntryIdx];
        Counter.AddPrerequisite(1u);

        Threading::StreamSliceRequest Request{};
        Request.SourcePath       = BoundSheetFile;
        Request.ByteOffset       = Entry.IndexHeader.SheetSliceByteOffset;
        Request.ByteLength       = Entry.IndexHeader.SheetSliceByteLength;
        Request.SheetPageOrdinal = Entry.IndexHeader.SheetPageOrdinal;
        Request.PriorityRank     = Rank++;
        Request.CompletionTarget = &Counter;
        Request.NotifySliceReady = [this, EntryIdx, &PageWriteMutex](std::vector<uint8_t>&& TileBytes, bool Ok) {
            if (!Ok)
            {
                return;
            }
            std::lock_guard<std::mutex> Guard(PageWriteMutex);
            CopyTileBytesIntoPage(Entries[EntryIdx].IndexHeader, TileBytes);
            Entries[EntryIdx].TileResident = true;
        };
        (void)Streamer.EnqueueSliceRead(std::move(Request));
    }

    Streamer.FlushPendingSynchronously();
}

ImportIntakeResult ThumbnailSheetRepository::IntakeExternalAssetDrop(
    const std::filesystem::path& DroppedSourceFile,
    WorkspaceCategory          ActiveWorkspace)
{
    ImportIntakeResult Result{};
    if (DroppedSourceFile.empty())
    {
        Result.DiagnosticMessage = "Empty file path dropped into ContentBrowser.";
        return Result;
    }

    const ContentCategory Detected = ClassifyExternalExtension(DroppedSourceFile);
    Result.DetectedCategory            = Detected;

    // 🔴 Enforce hard workspace domain blocking before quarantine or import conversion
    if (!QueryCategoryPermitted(ActiveWorkspace, Detected))
    {
        Result.Accepted          = false;
        Result.DiagnosticMessage = std::string("Blocked in ") + QueryWorkspaceTitle(ActiveWorkspace) +
                                   " workspace: " + QueryCategoryTitle(Detected) + " assets are excluded.";
        return Result;
    }

    std::error_code IoErr{};
    if (!std::filesystem::exists(DroppedSourceFile, IoErr))
    {
        Result.DiagnosticMessage = "Dropped source file does not exist: " + DroppedSourceFile.string();
        return Result;
    }

    // Step 1: Copy raw dropped bytes into transient quarantine (Saved/ImportIntake/)
    const std::filesystem::path QuarantineDir = ResolveQuarantineDirectory();
    std::filesystem::create_directories(QuarantineDir, IoErr);
    Result.QuarantinePath = QuarantineDir / DroppedSourceFile.filename();
    std::filesystem::copy_file(DroppedSourceFile, Result.QuarantinePath, std::filesystem::copy_options::overwrite_existing, IoErr);
    if (IoErr)
    {
        Result.DiagnosticMessage = "Failed to quarantine dropped asset: " + IoErr.message();
        return Result;
    }

    // Step 2: Convert/wrap into persistent Imported folder (or ScratchTempFolder / UserCustomFolder)
    const std::filesystem::path FinalDir = ResolveFinalImportDirectory(Detected);
    std::filesystem::create_directories(FinalDir, IoErr);

    const std::string Stem        = DroppedSourceFile.stem().string().empty() ? "ImportedAsset" : DroppedSourceFile.stem().string();
    const char*       CanonicalEx = QueryCanonicalExtension(Detected);
    Result.FinalSpacePath         = FinalDir / (Stem + CanonicalEx);

    {
        std::ifstream RawIn(Result.QuarantinePath, std::ios::binary);
        std::ofstream SpaceOut(Result.FinalSpacePath, std::ios::binary | std::ios::trunc);
        if (!RawIn.is_open() || !SpaceOut.is_open())
        {
            std::filesystem::remove(Result.QuarantinePath, IoErr);
            Result.DiagnosticMessage = "Unable to write converted container into Imported folder.";
            return Result;
        }
        SpaceOut << "# Frontier Converted Import Container (" << CanonicalEx << ")\n";
        SpaceOut << "SourceFilename = \"" << DroppedSourceFile.filename().string() << "\"\n";
        SpaceOut << "Category = \"" << QueryCategoryTitle(Detected) << "\"\n\n";
        SpaceOut << RawIn.rdbuf();
    }

    // Clean up transient quarantine copy after atomic conversion succeeds
    std::filesystem::remove(Result.QuarantinePath, IoErr);

    const uint64_t HashColor = ComputeFnv1a64(Stem);
    const ColorRgba Tint{
        static_cast<uint8_t>(110u + (HashColor & 0x7Fu)),
        static_cast<uint8_t>(110u + ((HashColor >> 8u) & 0x7Fu)),
        static_cast<uint8_t>(110u + ((HashColor >> 16u) & 0x7Fu)),
        255u,
    };

    Result.RepositoryIndex   = RegisterPrebakedEntry(Stem, Detected, ContentOriginFilter::Imported, Tint, Result.FinalSpacePath, true);
    Result.Accepted          = true;
    Result.DiagnosticMessage = "Imported " + Stem + CanonicalEx + " -> " + Result.FinalSpacePath.generic_string();
    return Result;
}

uint32_t ThumbnailSheetRepository::MigrateScratchImportsToProject(
    const std::filesystem::path& SavedProjectRoot,
    std::string&                 OutDiagnostic)
{
    if (SavedProjectRoot.empty())
    {
        OutDiagnostic = "Target ProjectRoot cannot be empty during scratch migration.";
        return 0u;
    }

    uint32_t        MigratedCount = 0u;
    std::error_code IoErr{};
    const std::filesystem::path ScratchImportedRoot = ImportPolicy.ScratchSessionRoot / "Saved/Imported";

    for (ContentRepositoryEntry& Entry : Entries)
    {
        if (static_cast<ContentOriginFilter>(Entry.IndexHeader.OriginCode) != ContentOriginFilter::Imported)
        {
            continue;
        }
        const std::string GenericDisk = Entry.DiskPath.generic_string();
        if (GenericDisk.find(ScratchImportedRoot.generic_string()) == std::string::npos)
        {
            continue;
        }

        const auto Category = static_cast<ContentCategory>(Entry.IndexHeader.CategoryCode);
        const std::filesystem::path DestDir  = SavedProjectRoot / "Content/Imported" / QueryCategoryTitle(Category);
        const std::filesystem::path DestFile = DestDir / Entry.DiskPath.filename();
        std::filesystem::create_directories(DestDir, IoErr);
        if (std::filesystem::exists(Entry.DiskPath, IoErr))
        {
            std::filesystem::copy_file(Entry.DiskPath, DestFile, std::filesystem::copy_options::overwrite_existing, IoErr);
            if (!IoErr)
            {
                std::filesystem::remove(Entry.DiskPath, IoErr);
                Entry.DiskPath = DestFile;
                ++MigratedCount;
            }
        }
    }

    ImportPolicy.ActiveProjectRoot = SavedProjectRoot;
    ImportPolicy.Mode              = ImportDestinationMode::ProjectImportedFolder;
    OutDiagnostic.clear();
    return MigratedCount;
}

std::vector<std::size_t> ThumbnailSheetRepository::QueryFilteredEntries(
    WorkspaceCategory Workspace,
    ContentCategory Category,
    ContentOriginFilter Origin,
    std::string_view    SearchQuery,
    bool                MatCapMaterialsOnly) const
{
    std::vector<std::size_t> Filtered;
    if (!QueryCategoryPermitted(Workspace, Category))
    {
        return Filtered;
    }

    const std::string LowerQuery = LowercaseAscii(SearchQuery);
    for (std::size_t Idx = 0u; Idx < Entries.size(); ++Idx)
    {
        const ContentRepositoryEntry& Entry         = Entries[Idx];
        const auto                    EntryCategory = static_cast<ContentCategory>(Entry.IndexHeader.CategoryCode);
        const auto                    EntryOrigin   = static_cast<ContentOriginFilter>(Entry.IndexHeader.OriginCode);

        // Hard workspace compatibility gate
        if ((Entry.IndexHeader.WorkspaceCompatibilityMask & WorkspaceBit(Workspace)) == 0u)
        {
            continue;
        }

        if (MatCapMaterialsOnly && Entry.IndexHeader.MatCapPresetMarker == 0u)
        {
            continue;
        }

        if (Category != ContentCategory::All && EntryCategory != Category)
        {
            continue;
        }

        // Origin routing:
        // - Engine   -> strictly EngineContent/
        // - Project  -> Project Content + linked EngineContent/ references
        // - Imported -> strictly Imported destination
        if (Origin == ContentOriginFilter::Engine && EntryOrigin != ContentOriginFilter::Engine)
        {
            continue;
        }
        if (Origin == ContentOriginFilter::Project &&
            EntryOrigin != ContentOriginFilter::Project &&
            EntryOrigin != ContentOriginFilter::Engine)
        {
            continue;
        }
        if (Origin == ContentOriginFilter::Imported && EntryOrigin != ContentOriginFilter::Imported)
        {
            continue;
        }

        if (!LowerQuery.empty())
        {
            const std::string Haystack = LowercaseAscii(Entry.DisplayTitle + " " + Entry.ContainerFormat);
            if (Haystack.find(LowerQuery) == std::string::npos)
            {
                continue;
            }
        }

        Filtered.push_back(Idx);
    }
    return Filtered;
}

std::span<const ContentRepositoryEntry> ThumbnailSheetRepository::QueryAllEntries() const noexcept
{
    return Entries;
}

const ContentRepositoryEntry* ThumbnailSheetRepository::QueryEntryAt(std::size_t EntryIndex) const noexcept
{
    return EntryIndex < Entries.size() ? &Entries[EntryIndex] : nullptr;
}

ThumbnailUvQuad ThumbnailSheetRepository::QueryTileUv(std::size_t EntryIndex) const noexcept
{
    ThumbnailUvQuad Quad{};
    if (EntryIndex >= Entries.size())
    {
        return Quad;
    }
    const ContentRepositoryEntry& Entry   = Entries[EntryIndex];
    const uint16_t                PageOrd = Entry.IndexHeader.SheetPageOrdinal;
    if (PageOrd >= SheetPages.size() || !Entry.TileResident)
    {
        return Quad;
    }

    SheetPageTexture& Page      = *SheetPages[PageOrd];
    const float       InvSheet  = 1.0f / static_cast<float>(SheetDimensionPixels);
    const float       PixelX0   = static_cast<float>(Entry.IndexHeader.TileColumn * TileDimensionPixels);
    const float       PixelY0   = static_cast<float>(Entry.IndexHeader.TileRow * TileDimensionPixels);
    const float       PixelSpan = static_cast<float>(TileDimensionPixels);

    Quad.TexturePointer = &Page.TextureStorage;
    Quad.UvMin          = { PixelX0 * InvSheet, PixelY0 * InvSheet };
    Quad.UvMax          = { (PixelX0 + PixelSpan) * InvSheet, (PixelY0 + PixelSpan) * InvSheet };
    Quad.Valid          = true;
    return Quad;
}

uint32_t ThumbnailSheetRepository::PageCount() const noexcept
{
    return static_cast<uint32_t>(SheetPages.size());
}

} // namespace Frontier
