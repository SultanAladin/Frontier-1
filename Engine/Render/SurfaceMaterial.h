/*==========================================================================================================================================
                                                          SURFACEMATERIAL.H
==========================================================================================================================================*/
// 🧩 Bounded authored material definitions, typed texture pyramids and immutable shader parameter compilation.

#pragma once
#include "Engine/ContentInterchange/MaterialAuthoring.h"
#include "ScatteringSequence.h"
#include "AutomotivePaint.h"
#include "SurfaceNormalProjection.h"
#include <filesystem>
#include <map>
#include <optional>
#include <span>
#include <string_view>

namespace Frontier
{
//------------------------------------------------------------------------------------------------------------------------
//                                                     MATERIAL AND SHADER CONTRACT
//------------------------------------------------------------------------------------------------------------------------

// 📝 Each stratum selects albedo/ORM/normal array slices; radiance remains material-wide.
// The appended fields preserve the lighting prefix while extending the authored material ABI.
struct alignas(16) MaterialScattering
{
    PreviewScattering Illumination;
    std::array<float,4> UvScaleOffset{1,1,0,0};
    std::array<float,4> RadianceNormal{0,0,0,1};
    std::array<std::array<uint32_t,4>,8> TextureUse{};
    std::array<std::array<float,4>,8> StratumUv = []
    {
        std::array<std::array<float,4>,8> Coordinates{};
        Coordinates.fill({1,1,0,0});
        return Coordinates;
    }();
    std::array<std::array<float,4>,8> StratumNormal = []
    {
        std::array<std::array<float,4>,8> Normals{};
        Normals.fill({1,1,0,0});
        return Normals;
    }();
    std::array<std::array<float,4>,8> DirectionalOptics = []
    {
        std::array<std::array<float,4>,8> Optics{};
        Optics.fill({0,0,0,.5f});
        return Optics;
    }(); // [-,rad,enum,0-1] - anisotropy, rotation, exact Fresnel, dielectric specular level
    AutomotivePaint Paint;
    std::array<uint32_t, 4> PreviewConfiguration{}; // Runtime-only specimen classification and studio lighting; reserved zero.
};
static_assert(sizeof(MaterialScattering) == 1760);
static_assert(offsetof(MaterialScattering,Paint) == 1472);
static_assert(offsetof(MaterialScattering,UvScaleOffset) == 928);
static_assert(offsetof(MaterialScattering,TextureUse) == 960);
static_assert(offsetof(MaterialScattering,StratumUv) == 1088);
static_assert(offsetof(MaterialScattering,StratumNormal) == 1216);
static_assert(offsetof(MaterialScattering,DirectionalOptics) == 1344);

enum class MaterialTextureCategory : uint32_t
{
    Albedo = 0,
    OcclusionRoughnessMetallic = 1,
    TangentNormal = 2,
    Radiance = 3
};

struct MaterialProceduralChannel
{
    std::string Name; // Albedo | Orm | Normal | Radiance
    std::string Op;   // scratches | "" (file or off)
    std::string Link; // pool identity; empty decodes as "shared"
    std::map<std::string,double> Params;
    std::map<std::string,std::string> Texts;
};

// 📝 Surface-independent volume coefficients; geometry supplies distance, never a coating thickness.
struct DielectricVolume
{
    float Ior = 1.5f;                         // [-] - exterior-relative refractive index
    std::array<float,3> Absorption{};          // [1/m] - interior RGB extinction
    bool operator==(const DielectricVolume &) const = default;
};

inline bool ValidateDielectricVolume(const DielectricVolume &Volume)
{
    if (!std::isfinite(Volume.Ior) || Volume.Ior < 1 || Volume.Ior > 3) return false;
    for (const float Coefficient : Volume.Absorption)
        if (!std::isfinite(Coefficient) || Coefficient < 0 || Coefficient > 100) return false;
    return true;
}

struct MaterialDefinition
{
    std::string Name = "Untitled material";
    MaterialScattering Scattering;
    std::array<std::string,4> TextureSources;
    std::array<std::array<std::string,4>,8> StratumTextureSources;
    std::array<std::vector<MaterialProceduralChannel>,8> StratumChannels;
    // 📝 Runtime-only rebake overrides keyed by (slot, source); populated by a procedural rebake, attached at
    // compile time, persisted to PNG files on save. Never serialized into the material source.
    std::map<std::pair<size_t,std::string>, SurfaceTextureImage> ProceduralMaps;
    // 📝 Optional [Authoring] layer-stack document; decoded/encoded verbatim through the strict normalize
    // gate so HTML-authored stacks survive native round trips without silent field loss.
    std::optional<MaterialAuthoringDocument> Authoring;
    std::optional<DielectricVolume> ClosedVolume = std::nullopt;
};

struct MaterialMipRegion
{
    uint32_t Width = 0;
    uint32_t Height = 0;
    size_t Offset = 0;
};

struct MaterialTexturePyramid
{
    MaterialTextureCategory Category = MaterialTextureCategory::Albedo;
    std::vector<uint8_t> Rgba;
    std::vector<MaterialMipRegion> Levels;
    uint32_t Layers = 1;
    std::vector<float> FloatRgba;
};

enum class MaterialAuthoringMap : uint32_t
{
    Colour = 0,
    TangentNormal = 1,
    Height = 2,
    Roughness = 3,
    Metalness = 4,
    Occlusion = 5
};

struct CompiledSurfaceMaterial
{
    std::string Name;
    MaterialScattering Scattering;
    std::array<MaterialTexturePyramid,4> Textures;
    std::array<MaterialTexturePyramid,6> AuthoringMaps;
    std::optional<DielectricVolume> ClosedVolume = std::nullopt;
};

inline bool ValidateMaterialScattering(const MaterialScattering &Material)
{
    // 📝 Preview slot 5 is the draped-cloth inspection surface; rejecting it
    // crashed cloth selection with a dormant-contract severe failure. Slot [2] carries the
    // environment archive layer since the baked-environment change; leaving it pinned to zero
    // froze the preview on every non-default environment (Windows test, 2026-10-22), so bound
    // it to the catalogue capacity instead of forbidding it.
    if (Material.PreviewConfiguration[0] > 5 || Material.PreviewConfiguration[1] > 1 || Material.PreviewConfiguration[2] > 63 || Material.PreviewConfiguration[3]) return false;
    if (!ValidateAutomotivePaint(Material.Paint)) return false;
    if (!ValidatePreviewScattering(Material.Illumination) || Material.Illumination.Configuration[1] != 0) return false;
    for (float Coefficient : Material.UvScaleOffset)
        if (!std::isfinite(Coefficient) || std::abs(Coefficient) > 1024) return false;
    for (size_t Channel = 0; Channel < 4; ++Channel)
        if (!std::isfinite(Material.RadianceNormal[Channel]) || Material.RadianceNormal[Channel] < 0 ||
            Material.RadianceNormal[Channel] > (Channel == 3 ? 4 : 64)) return false;
    for (size_t Index = 0; Index < 8; ++Index)
    {
        const auto &Selection = Material.TextureUse[Index];
        if (Selection[0] > 15 || Selection[1] > 7 || Selection[2] > 7 || Selection[3] > 7) return false;
        for (float Coefficient : Material.StratumUv[Index])
            if (!std::isfinite(Coefficient) || std::abs(Coefficient) > 1024) return false;
        const auto &Normal = Material.StratumNormal[Index];
        if (!std::isfinite(Normal[0]) || Normal[0] < 0 || Normal[0] > 4 ||
            !std::isfinite(Normal[1]) || Normal[1] < 0 || Normal[1] > 4 || Normal[2] || Normal[3]) return false;
        const auto &Optics = Material.DirectionalOptics[Index];
        for (float Coefficient : Optics) if (!std::isfinite(Coefficient)) return false;
        if (std::abs(Optics[0]) > .9f || std::abs(Optics[1]) > 100 || (Optics[2] != 0 && Optics[2] != 1) ||
            Optics[3] < 0 || Optics[3] > 1) return false;
        if (Index < Material.Illumination.Configuration[0] && Optics[2] == 1 && Material.Illumination.Strata[Index].Optics[0] != 0) return false;
    }
    return true;
}

MaterialDefinition DecodeSurfaceMaterial(std::string_view Text);
std::string EncodeSurfaceMaterial(const MaterialDefinition &Material);
// 📝 Shared-pool resolution: locate TargetKey inside stratum channel params in stratum/channel order, so recipe
// controls and the baker answer to the same procedural definition.
struct MaterialPoolLocation
{
    uint32_t Stratum = 0;
    std::string Channel;
    double Value = 0;
};
[[nodiscard]] std::optional<MaterialPoolLocation> QueryProceduralPool(const MaterialDefinition &Material, std::string_view Key);
// 📝 Derived texture-source key for a procedural channel without an explicit file; the baker and the save path
// agree on this spelling, and the encoder never persists it.
[[nodiscard]] std::string EncodeProceduralSource(size_t Stratum, std::string_view Channel);
// 📝 Bake-on-open: channels naming an op but carrying no rebake overrides compile at proxy extent, so shipped
// procedurals (and repaired procedural/* file sources) render without a manual rebake gesture; per-channel
// failures journal and fall back to neutral maps instead of refusing the material.
void EnsureProceduralChannelBakes(MaterialDefinition &Material, uint32_t Extent);
MaterialTexturePyramid ConstructMaterialPyramid(const SurfaceTextureImage &Image,MaterialTextureCategory Category);
inline bool ValidateMaterialPyramid(const MaterialTexturePyramid &Texture)
{
    if (uint32_t(Texture.Category) > 3 || Texture.Levels.empty() || Texture.Levels.size() > 13 || !Texture.Layers || Texture.Layers > 8 ||
        (Texture.Rgba.empty() == Texture.FloatRgba.empty())) return false;
    const size_t BytesPerComponent = Texture.FloatRgba.empty() ? sizeof(uint8_t) : sizeof(float);
    uint32_t Width = Texture.Levels[0].Width, Height = Texture.Levels[0].Height;
    if (!Width || !Height || Width > 4096 || Height > 4096) return false;
    size_t Offset = 0;
    for (const auto &Level : Texture.Levels)
    {
        if (Level.Width != Width || Level.Height != Height || Level.Offset != Offset) return false;
        if (Width == 1 && Height == 1 && &Level != &Texture.Levels.back()) return false;
        Offset += size_t(Width) * Height * 4 * Texture.Layers * BytesPerComponent;
        Width = std::max(1u, Width / 2);
        Height = std::max(1u, Height / 2);
    }
    if (Texture.Levels.back().Width != 1 || Texture.Levels.back().Height != 1 ||
        Offset != (Texture.FloatRgba.empty() ? Texture.Rgba.size() : Texture.FloatRgba.size() * sizeof(float))) return false;
    return std::all_of(Texture.FloatRgba.begin(), Texture.FloatRgba.end(), [](float Sample) { return std::isfinite(Sample); });
}

MaterialTexturePyramid ConstructFloatMaterialPyramid(uint32_t Width, uint32_t Height,
    MaterialTextureCategory Category, std::span<const float> FloatRgba);
std::array<MaterialTexturePyramid,6> ConstructAuthoringMaterialMaps(uint32_t Width, uint32_t Height,
    const std::array<std::span<const float>,6> &FloatRgba);

CompiledSurfaceMaterial CompileSurfaceMaterial(const MaterialDefinition &Material,const std::filesystem::path &ContentDirectory);
} // namespace Frontier
