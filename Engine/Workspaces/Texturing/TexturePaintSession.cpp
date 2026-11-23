/*==========================================================================================================================================
                                                         TEXTUREPAINTSESSION.CPP
==========================================================================================================================================*/
// 🎨 Implementation of TexturePaintSession: 22 PBR channels, 8 layer kinds, 3D/2D PBR rasterizer, gradient points & Brush Studio presets.

#include "Engine/Workspaces/Texturing/TexturePaintSession.h"

#include "Engine/GeometricRaster/Baking/StudyGeometryBuilder.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace Frontier::Workspaces
{

using namespace Frontier::GeometricRaster::Baking;

struct PaintGeometryCacheStorage
{
    PaintStudyAsset        CachedAsset      = PaintStudyAsset::Count;
    PreparedStudyPair      PreparedStudy    {};
    uint32_t               LastViewportW    = 0u;
    uint32_t               LastViewportH    = 0u;
    std::vector<uint8_t>   HitMask          {};
    std::vector<float>     HitUvU           {};
    std::vector<float>     HitUvV           {};
    std::vector<float>     HitWorldX        {};
    std::vector<float>     HitWorldY        {};
    std::vector<float>     HitWorldZ        {};
    std::vector<uint8_t>   HitPart          {};
};

namespace
{

constexpr std::array<PaintChannelDescriptor, TexturePaintSession::ChannelCount> PaintChannelEntries {{
    { PaintSurfaceChannel::Albedo,         PaintChannelGroup::Primary,  "Albedo",              "Surface Colour",        "Al",  { 232u, 194u, 122u, 255u },  0.0f,   1.0f,  1.0f,  true  },
    { PaintSurfaceChannel::Roughness,      PaintChannelGroup::Primary,  "Roughness",           "Microsurface Scatter",  "Ro",  { 154u, 154u, 154u, 255u },  0.02f,  1.0f,  0.42f, false },
    { PaintSurfaceChannel::Metalness,      PaintChannelGroup::Primary,  "Metalness",           "Conductor Mask",        "Me",  { 201u, 210u, 220u, 255u },  0.0f,   1.0f,  0.0f,  false },
    { PaintSurfaceChannel::Height,         PaintChannelGroup::Primary,  "Height",              "Displacement Relief",   "He",  { 127u, 127u, 255u, 255u }, -1.0f,   1.0f,  0.0f,  false },
    { PaintSurfaceChannel::NormalStrength, PaintChannelGroup::Primary,  "Normal",              "Tangent Perturbation",  "No",  { 143u, 143u, 255u, 255u },  0.0f,   2.0f,  1.0f,  false },
    { PaintSurfaceChannel::Occlusion,      PaintChannelGroup::Primary,  "Ambient Occlusion",   "Cavity Shadowing",      "AO",  { 185u, 185u, 185u, 255u },  0.0f,   1.0f,  1.0f,  false },
    { PaintSurfaceChannel::Opacity,        PaintChannelGroup::Primary,  "Opacity",             "Alpha Cutout Weight",   "Op",  { 217u, 217u, 217u, 255u },  0.0f,   1.0f,  1.0f,  false },
    { PaintSurfaceChannel::Specular,       PaintChannelGroup::Surface,  "Specular",            "Dielectric F0 Weight",  "Sp",  { 232u, 232u, 232u, 255u },  0.0f,   1.0f,  0.50f, false },
    { PaintSurfaceChannel::SpecColour,     PaintChannelGroup::Surface,  "Specular Tint",       "F0 Reflectance Tint",   "Sc",  { 255u, 255u, 255u, 255u },  0.0f,   1.0f,  1.0f,  true  },
    { PaintSurfaceChannel::Anisotropy,     PaintChannelGroup::Surface,  "Anisotropy",          "Directional Brush",     "An",  { 168u, 180u, 196u, 255u },  0.0f,   1.0f,  0.0f,  false },
    { PaintSurfaceChannel::Clearcoat,      PaintChannelGroup::Coat,     "Clearcoat",           "Lacquer Layer Weight",  "Cc",  { 207u, 227u, 255u, 255u },  0.0f,   1.0f,  0.0f,  false },
    { PaintSurfaceChannel::CoatRough,      PaintChannelGroup::Coat,     "Coat Roughness",      "Lacquer Micro-Scatter", "Cr",  { 143u, 163u, 191u, 255u },  0.02f,  1.0f,  0.15f, false },
    { PaintSurfaceChannel::Sheen,          PaintChannelGroup::Coat,     "Sheen",               "Grazing Velvet Fuzz",   "Sh",  { 230u, 201u, 240u, 255u },  0.0f,   1.0f,  0.0f,  false },
    { PaintSurfaceChannel::SheenRough,     PaintChannelGroup::Coat,     "Sheen Roughness",     "Fiber Scatter Width",   "Sr",  { 180u, 156u, 192u, 255u },  0.02f,  1.0f,  0.50f, false },
    { PaintSurfaceChannel::SheenColour,    PaintChannelGroup::Coat,     "Sheen Tint",          "Grazing Fuzz Colour",   "ShC", { 243u, 230u, 255u, 255u },  0.0f,   1.0f,  1.0f,  true  },
    { PaintSurfaceChannel::Iridescence,    PaintChannelGroup::Optics,   "Iridescence",         "Thin-Film Interference","Ir",  { 158u, 230u, 216u, 255u },  0.0f,   1.0f,  0.0f,  false },
    { PaintSurfaceChannel::IridFilm,       PaintChannelGroup::Optics,   "Iridescence Film",    "Film Thickness (nm)",   "If",  { 127u, 201u, 224u, 255u },  0.0f,   1.0f,  0.38f, false },
    { PaintSurfaceChannel::Transmission,   PaintChannelGroup::Optics,   "Transmission",        "Refractive Glass Pass", "Tr",  { 191u, 232u, 255u, 255u },  0.0f,   1.0f,  0.0f,  false },
    { PaintSurfaceChannel::Subsurface,     PaintChannelGroup::Optics,   "Thickness",           "Subsurface Scatter",    "Ss",  { 255u, 183u, 160u, 255u },  0.0f,   1.0f,  0.0f,  false },
    { PaintSurfaceChannel::SubsurfTint,    PaintChannelGroup::Optics,   "Attenuation Colour",  "Volume Absorption Tint","St",  { 255u, 217u, 204u, 255u },  0.0f,   1.0f,  1.0f,  true  },
    { PaintSurfaceChannel::Emissive,       PaintChannelGroup::Radiance, "Emissive",            "Self-Luminous Colour",  "Em",  { 255u, 122u, 59u,  255u },  0.0f,   1.0f,  0.0f,  true  },
    { PaintSurfaceChannel::GlowPower,      PaintChannelGroup::Radiance, "Emissive Intensity",  "Radiance Multiplier",   "Gp",  { 255u, 176u, 122u, 255u },  0.0f,   4.0f,  1.0f,  false },
}};

constexpr std::array<const char*, 5u> PaintGroupLabels {{
    "Core",
    "Surface",
    "Coat",
    "Optics",
    "Emission",
}};

constexpr std::array<PaintStudyDescriptor, 5u> PaintStudyEntries {{
    { PaintStudyAsset::UtahTeapot, "Utah Teapot", 4u, 32u },
    { PaintStudyAsset::ShaderBall, "Shader Ball", 4u, 18u },
    { PaintStudyAsset::ChamferBox, "Chamfer Box", 1u,  6u },
    { PaintStudyAsset::UvSphere,   "UV Sphere",   1u,  1u },
    { PaintStudyAsset::TorusKnot,  "Torus Knot",  1u,  1u },
}};

constexpr std::array<const char*, 4u> TeapotPartNames {{
    "Body",
    "Lid",
    "Handle",
    "Spout",
}};

constexpr std::array<BrushLibraryPresetDescriptor, 12u> BrushLibraryEntries {{
    { 0u,  "Round Studio #2",      "Brush",  BrushTipShape::Round,    16, 0.84f, 0.10f, 0.92f, 0.28f, { 232u, 194u, 122u, 255u } },
    { 1u,  "Rigger Liner #0",      "Liner",  BrushTipShape::Round,     8, 0.92f, 0.07f, 0.98f, 0.48f, { 143u, 184u, 222u, 255u } },
    { 2u,  "Filbert Soft #8",      "Brush",  BrushTipShape::Round,    22, 0.58f, 0.14f, 0.78f, 0.22f, { 216u, 144u, 168u, 255u } },
    { 3u,  "Flat Shader #10",      "Shader", BrushTipShape::Chisel,   24, 0.78f, 0.12f, 0.85f, 0.20f, { 118u, 178u, 216u, 255u } },
    { 4u,  "Bright Stiff #14",     "Bristle",BrushTipShape::Chisel,   28, 0.86f, 0.16f, 0.90f, 0.18f, { 235u, 152u,  62u, 255u } },
    { 5u,  "Fan Blender #6",       "Blender",BrushTipShape::Sponge,   34, 0.36f, 0.20f, 0.62f, 0.30f, { 158u, 230u, 216u, 255u } },
    { 6u,  "Hog Bristle Flat #12", "Bristle",BrushTipShape::Chisel,   26, 0.76f, 0.14f, 0.88f, 0.24f, { 239u, 244u, 207u, 255u } },
    { 7u,  "Sumi Ink Wash",        "Wash",   BrushTipShape::Round,    30, 0.44f, 0.11f, 0.72f, 0.36f, { 180u, 190u, 206u, 255u } },
    { 8u,  "Charcoal Stick",       "Dry",    BrushTipShape::Chalk,    24, 0.68f, 0.18f, 0.80f, 0.16f, { 200u, 164u, 128u, 255u } },
    { 9u,  "Chisel Marker",        "Marker", BrushTipShape::Chisel,   20, 0.90f, 0.08f, 0.94f, 0.26f, {  72u, 178u, 128u, 255u } },
    { 10u, "Airbrush Soft",        "Spray",  BrushTipShape::Round,    38, 0.22f, 0.15f, 0.56f, 0.20f, { 174u, 118u, 218u, 255u } },
    { 11u, "Splatter Texture",     "Spray",  BrushTipShape::Splatter, 32, 0.74f, 0.32f, 0.84f, 0.10f, { 255u, 122u,  59u, 255u } },
}};

constexpr std::array<const char*, 6u> FontPresetLabels {{
    "DM Sans",
    "Serif",
    "Mono",
    "Condensed",
    "Display",
    "Script",
}};

constexpr std::array<const char*, 12u> MaterialPresetNames {{
    "Ceramic Glaze",
    "Brushed Steel",
    "Hammered Copper",
    "Anodized Titanium",
    "Car Paint Clearcoat",
    "Velvet Fabric",
    "Jade Subsurface",
    "Volcanic Basalt",
    "Weathered Brass",
    "Carbon Fiber",
    "Gold Leaf",
    "Frosted Glass",
}};

constexpr std::array<const char*, 23u> PaintViewChannelLabels {{
    "3D Mesh",
    "Albedo",
    "Roughness",
    "Metalness",
    "Height",
    "Normal",
    "Ambient Occlusion",
    "Opacity",
    "Specular",
    "Specular Tint",
    "Anisotropy",
    "Clearcoat",
    "Coat Roughness",
    "Sheen",
    "Sheen Roughness",
    "Sheen Tint",
    "Iridescence",
    "Iridescence Film",
    "Transmission",
    "Thickness",
    "Attenuation Colour",
    "Emissive",
    "Emissive Intensity",
}};

constexpr std::array<const char*, 10u> PaintBlendLabels {{
    "Normal",
    "Multiply",
    "Screen",
    "Overlay",
    "Add",
    "Soft Light",
    "Darken",
    "Lighten",
    "Color Dodge",
    "Height Blend",
}};

constexpr std::array<ColorRgba, 8u> BrushQuickSwatches {{
    { 232u, 194u, 122u, 255u }, // Warm Gold
    { 225u,  75u,  75u, 255u }, // Signal Red
    { 235u, 152u,  62u, 255u }, // Amber Rust
    {  72u, 178u, 128u, 255u }, // Oxide Mint
    {  82u, 148u, 232u, 255u }, // Cobalt Blue
    { 174u, 118u, 218u, 255u }, // Violet Ink
    { 244u, 245u, 248u, 255u }, // Pure White
    {  24u,  26u,  30u, 255u }, // Carbon Black
}};

void EnablePrimaryFourChannels(std::array<bool, PaintLayerEntry::ChannelCount>& Channels) noexcept
{
    Channels.fill(false);
    Channels[static_cast<std::size_t>(PaintSurfaceChannel::Albedo)]    = true;
    Channels[static_cast<std::size_t>(PaintSurfaceChannel::Roughness)] = true;
    Channels[static_cast<std::size_t>(PaintSurfaceChannel::Metalness)] = true;
    Channels[static_cast<std::size_t>(PaintSurfaceChannel::Height)]    = true;
}

void InitializeDefaultLayerChannels(PaintLayerEntry& Layer) noexcept
{
    for (std::size_t I = 0u; I < PaintLayerEntry::ChannelCount; ++I)
    {
        Layer.ChannelScalars[I]      = PaintChannelEntries[I].DefaultScalar;
        Layer.ChannelOpacities[I]    = 1.0f;
        Layer.ChannelBlendMethods[I] = 0u; // Inherit
        Layer.ChannelTints[I]        = PaintChannelEntries[I].SwatchTint;
        Layer.ExpandedChannels[I]    = false;
    }
    Layer.ChannelTints[static_cast<std::size_t>(PaintSurfaceChannel::Albedo)] = Layer.PrimerTint;
}

[[nodiscard]] StudyGeometryCategory MapToStudyGeometryCategory(PaintStudyAsset Asset) noexcept
{
    switch (Asset)
    {
        case PaintStudyAsset::UtahTeapot: return StudyGeometryCategory::UtahTeapot;
        case PaintStudyAsset::ShaderBall: return StudyGeometryCategory::ShaderBall;
        case PaintStudyAsset::ChamferBox: return StudyGeometryCategory::ChamferBox;
        case PaintStudyAsset::UvSphere:   return StudyGeometryCategory::UvSphere;
        case PaintStudyAsset::TorusKnot:  return StudyGeometryCategory::TorusKnot;
        default:                          return StudyGeometryCategory::UtahTeapot;
    }
}

[[nodiscard]] float BlendChannelValue(float BottomVal, float TopVal, PaintBlendMethod Method, float Alpha) noexcept
{
    const float A = std::clamp(Alpha, 0.0f, 1.0f);
    if (A <= 1.0e-5f)
    {
        return BottomVal;
    }
    float Combined = TopVal;
    switch (Method)
    {
        case PaintBlendMethod::Multiply:    Combined = BottomVal * TopVal; break;
        case PaintBlendMethod::Screen:      Combined = 1.0f - (1.0f - BottomVal) * (1.0f - TopVal); break;
        case PaintBlendMethod::Overlay:     Combined = (BottomVal < 0.5f) ? (2.0f * BottomVal * TopVal) : (1.0f - 2.0f * (1.0f - BottomVal) * (1.0f - TopVal)); break;
        case PaintBlendMethod::Add:         Combined = std::min(1.0f, BottomVal + TopVal); break;
        case PaintBlendMethod::SoftLight:   Combined = (1.0f - 2.0f * TopVal) * BottomVal * BottomVal + 2.0f * TopVal * BottomVal; break;
        case PaintBlendMethod::Darken:      Combined = std::min(BottomVal, TopVal); break;
        case PaintBlendMethod::Lighten:     Combined = std::max(BottomVal, TopVal); break;
        case PaintBlendMethod::ColorDodge:  Combined = (TopVal >= 0.999f) ? 1.0f : std::min(1.0f, BottomVal / (1.0f - TopVal)); break;
        case PaintBlendMethod::HeightBlend: Combined = std::clamp(BottomVal + (TopVal - 0.5f) * 1.4f, 0.0f, 1.0f); break;
        case PaintBlendMethod::Normal:
        default:                            Combined = TopVal; break;
    }
    return BottomVal + (Combined - BottomVal) * A;
}

[[nodiscard]] float EvaluateProceduralNoise2D(float U, float V, float Scale) noexcept
{
    const float X = U * Scale;
    const float Y = V * Scale;
    const float S = std::sin(X * 12.9898f + Y * 78.233f) * 43758.5453f;
    const float Frac = S - std::floor(S);
    const float Wave = 0.5f + 0.5f * std::sin(X * 3.14159f) * std::cos(Y * 3.14159f);
    return std::clamp(0.55f * Wave + 0.45f * Frac, 0.0f, 1.0f);
}

struct EvaluatedPbrTexel
{
    float AlbedoR    = 0.76f;
    float AlbedoG    = 0.77f;
    float AlbedoB    = 0.80f;
    float Roughness  = 0.32f;
    float Metalness  = 0.04f;
    float Height     = 0.0f;
    float Clearcoat  = 0.0f;
    float Sheen      = 0.0f;
    float EmissiveR  = 0.0f;
    float EmissiveG  = 0.0f;
    float EmissiveB  = 0.0f;
    float Alpha      = 1.0f;
};

[[nodiscard]] EvaluatedPbrTexel EvaluateLayerStackAtSurfacePoint(
    const std::vector<PaintLayerEntry>& Layers,
    float                               U,
    float                               V,
    float                               WorldX,
    float                               WorldY,
    float                               WorldZ,
    float                               NormalZ,
    uint32_t                            PartOrdinal) noexcept
{
    EvaluatedPbrTexel Out {};
    const float WrappedU = U - std::floor(U);
    const float WrappedV = V - std::floor(V);

    for (const auto& Layer : Layers)
    {
        if (!Layer.Visible || Layer.Category == PaintLayerCategory::Folder || Layer.Opacity <= 1.0e-4f)
        {
            continue;
        }
        if (Layer.FolderOwnerId != 0u)
        {
            bool FolderHidden = false;
            for (const auto& Other : Layers)
            {
                if (Other.Id == Layer.FolderOwnerId && !Other.Visible)
                {
                    FolderHidden = true;
                    break;
                }
            }
            if (FolderHidden)
            {
                continue;
            }
        }
        if (Layer.PartFilter >= 0 && static_cast<uint32_t>(Layer.PartFilter) != PartOrdinal)
        {
            continue;
        }

        float MaskWeight = 1.0f;
        if (Layer.Mask.Attached && Layer.Mask.Enabled)
        {
            float GenMask = 1.0f;
            if (Layer.Mask.Generator == PaintMaskGenerator::EdgeWear)
            {
                const float EdgeSignal = std::abs( std::sin(WrappedU * 18.85f) * std::cos(WrappedV * 18.85f) );
                GenMask = std::clamp((EdgeSignal - (1.0f - Layer.Mask.Threshold)) / std::max(0.05f, Layer.Mask.Softness), 0.0f, 1.0f);
            }
            else if (Layer.Mask.Generator == PaintMaskGenerator::CreviceDirt)
            {
                const float Cavity = 1.0f - std::clamp(NormalZ * 0.5f + 0.5f, 0.0f, 1.0f);
                GenMask = std::clamp((Cavity - (1.0f - Layer.Mask.Threshold)) / std::max(0.05f, Layer.Mask.Softness), 0.0f, 1.0f);
            }
            else if (Layer.Mask.Generator == PaintMaskGenerator::TopDust)
            {
                GenMask = std::clamp((NormalZ - (1.0f - Layer.Mask.Threshold)) / std::max(0.05f, Layer.Mask.Softness), 0.0f, 1.0f);
            }
            else if (Layer.Mask.Generator == PaintMaskGenerator::TriplanarGrunge)
            {
                GenMask = EvaluateProceduralNoise2D(WrappedU, WrappedV, 8.0f);
            }
            if (Layer.Mask.Invert)
            {
                GenMask = 1.0f - GenMask;
            }
            MaskWeight = std::clamp(GenMask * Layer.Mask.Opacity, 0.0f, 1.0f);
        }
        if (MaskWeight <= 1.0e-4f)
        {
            continue;
        }

        float LayerCoverage = 0.0f;
        float LayerR = static_cast<float>(Layer.PrimerTint.Red) / 255.0f;
        float LayerG = static_cast<float>(Layer.PrimerTint.Green) / 255.0f;
        float LayerB = static_cast<float>(Layer.PrimerTint.Blue) / 255.0f;
        float LayerRough = Layer.ChannelScalars[static_cast<std::size_t>(PaintSurfaceChannel::Roughness)];
        float LayerMetal = Layer.ChannelScalars[static_cast<std::size_t>(PaintSurfaceChannel::Metalness)];
        float LayerHeight = Layer.ChannelScalars[static_cast<std::size_t>(PaintSurfaceChannel::Height)];

        if (Layer.Category == PaintLayerCategory::Fill || Layer.Category == PaintLayerCategory::Material)
        {
            LayerCoverage = 1.0f;
            const ColorRgba AlbedoSwatch = Layer.ChannelTints[static_cast<std::size_t>(PaintSurfaceChannel::Albedo)];
            LayerR = static_cast<float>(AlbedoSwatch.Red) / 255.0f;
            LayerG = static_cast<float>(AlbedoSwatch.Green) / 255.0f;
            LayerB = static_cast<float>(AlbedoSwatch.Blue) / 255.0f;

            if (Layer.Category == PaintLayerCategory::Fill && Layer.PatternRepeats > 1)
            {
                const float Rep    = static_cast<float>(Layer.PatternRepeats);
                const float PatMod = std::sin(WrappedU * Rep * 6.28318f) * std::sin(WrappedV * Rep * 6.28318f);
                const float Mod    = 0.94f + 0.06f * PatMod;
                LayerR = std::clamp(LayerR * Mod, 0.0f, 1.0f);
                LayerG = std::clamp(LayerG * Mod, 0.0f, 1.0f);
                LayerB = std::clamp(LayerB * Mod, 0.0f, 1.0f);
                LayerHeight += PatMod * Layer.PatternHeightGain * 0.25f;
            }
            else if (Layer.Category == PaintLayerCategory::Material)
            {
                const float Grain = EvaluateProceduralNoise2D(WrappedU, WrappedV, Layer.GrainScale) - 0.5f;
                LayerRough = std::clamp(LayerRough + Grain * Layer.GrainContrast * 0.35f, 0.02f, 1.0f);
                LayerHeight += Grain * Layer.GrainContrast * 0.25f;
            }
        }
        else if (Layer.Category == PaintLayerCategory::Gradient && !Layer.GradientPoints.empty())
        {
            LayerCoverage = 1.0f;
            const float FalloffExp = std::max(0.25f, Layer.GradientFalloff * 2.2f);
            float BgWeight = 0.18f;
            float SumW  = BgWeight;
            float AccR  = (static_cast<float>(Layer.GradientBackgroundTint.Red) / 255.0f) * BgWeight;
            float AccG  = (static_cast<float>(Layer.GradientBackgroundTint.Green) / 255.0f) * BgWeight;
            float AccB  = (static_cast<float>(Layer.GradientBackgroundTint.Blue) / 255.0f) * BgWeight;
            float AccRo = Layer.GradientBackgroundRoughness * BgWeight;
            float AccMe = Layer.GradientBackgroundMetalness * BgWeight;
            float AccHe = Layer.GradientBackgroundHeight * BgWeight;

            for (const auto& Pt : Layer.GradientPoints)
            {
                const float Dx = WorldX - Pt.PositionX;
                const float Dy = WorldY - Pt.PositionY;
                const float Dz = WorldZ - Pt.PositionZ;
                const float Dist = std::sqrt(Dx * Dx + Dy * Dy + Dz * Dz);
                const float NormD = Dist / std::max(0.12f, Pt.Radius);
                const float W = Pt.Strength / (0.04f + std::pow(NormD, FalloffExp));
                SumW  += W;
                AccR  += (static_cast<float>(Pt.TintColor.Red) / 255.0f) * W;
                AccG  += (static_cast<float>(Pt.TintColor.Green) / 255.0f) * W;
                AccB  += (static_cast<float>(Pt.TintColor.Blue) / 255.0f) * W;
                AccRo += Pt.Roughness * W;
                AccMe += Pt.Metalness * W;
                AccHe += Pt.Height * W;
            }
            const float InvW = 1.0f / std::max(1.0e-5f, SumW);
            LayerR      = AccR * InvW;
            LayerG      = AccG * InvW;
            LayerB      = AccB * InvW;
            LayerRough  = AccRo * InvW;
            LayerMetal  = AccMe * InvW;
            LayerHeight = AccHe * InvW;
        }
        else if (Layer.Category == PaintLayerCategory::WearGen)
        {
            const float Noise = EvaluateProceduralNoise2D(WrappedU, WrappedV, Layer.NoiseScale);
            float Signal = Noise;
            if (Layer.WearPreset == PaintMaskGenerator::TopDust)
            {
                Signal = std::clamp(NormalZ * 0.7f + 0.3f * Noise, 0.0f, 1.0f);
            }
            else if (Layer.WearPreset == PaintMaskGenerator::CreviceDirt)
            {
                Signal = std::clamp((1.0f - NormalZ) * 0.65f + 0.35f * Noise, 0.0f, 1.0f);
            }
            if (Layer.GeneratorInvert)
            {
                Signal = 1.0f - Signal;
            }
            LayerCoverage = std::clamp((Signal - (1.0f - Layer.GeneratorThreshold)) / std::max(0.05f, Layer.GeneratorSoftness), 0.0f, 1.0f);
        }
        else if (Layer.Category == PaintLayerCategory::Stamp || Layer.Category == PaintLayerCategory::Text)
        {
            for (const auto& St : Layer.Stamps)
            {
                if (!St.Visible) continue;
                const float Du = (WrappedU - St.U) / std::max(0.03f, St.Scale * 0.5f);
                const float Dv = (WrappedV - St.V) / std::max(0.03f, St.Scale * 0.5f);
                const float DistSq = Du * Du + Dv * Dv;
                if (DistSq <= 1.0f)
                {
                    const float Ring = (Layer.Category == PaintLayerCategory::Text)
                        ? (std::abs(Dv) < 0.48f && std::abs(Du) < 0.82f ? 1.0f : 0.0f)
                        : std::clamp((1.0f - std::sqrt(DistSq)) * 3.0f, 0.0f, 1.0f);
                    if (Ring > 0.01f)
                    {
                        LayerCoverage = std::max(LayerCoverage, Ring * St.Opacity);
                        LayerR = static_cast<float>(St.TintColor.Red) / 255.0f;
                        LayerG = static_cast<float>(St.TintColor.Green) / 255.0f;
                        LayerB = static_cast<float>(St.TintColor.Blue) / 255.0f;
                        LayerHeight = St.HeightGain * 0.4f;
                    }
                }
            }
        }
        else if (Layer.Category == PaintLayerCategory::Paint)
        {
            for (const auto& Dab : Layer.StrokeDabs)
            {
                float Du = std::abs(WrappedU - Dab.U);
                if (Du > 0.5f) Du = 1.0f - Du;
                float Dv = std::abs(WrappedV - Dab.V);
                if (Dv > 0.5f) Dv = 1.0f - Dv;
                const float Dist = std::sqrt(Du * Du + Dv * Dv);
                if (Dist < Dab.RadiusUv)
                {
                    const float EdgeT = Dist / std::max(1.0e-4f, Dab.RadiusUv);
                    const float SoftSpan = std::max(0.08f, 1.0f - Dab.Hardness);
                    const float Falloff = std::clamp((1.0f - EdgeT) / SoftSpan, 0.0f, 1.0f);
                    const float DabW = Falloff * Falloff * (3.0f - 2.0f * Falloff) * Dab.Opacity;
                    if (Dab.EraserMode)
                    {
                        LayerCoverage = std::max(0.0f, LayerCoverage * (1.0f - DabW));
                    }
                    else if (DabW > 0.001f)
                    {
                        LayerCoverage = LayerCoverage + (1.0f - LayerCoverage) * DabW;
                        LayerR = LayerR + (static_cast<float>(Dab.Tint.Red) / 255.0f - LayerR) * DabW;
                        LayerG = LayerG + (static_cast<float>(Dab.Tint.Green) / 255.0f - LayerG) * DabW;
                        LayerB = LayerB + (static_cast<float>(Dab.Tint.Blue) / 255.0f - LayerB) * DabW;
                        LayerRough  = Dab.Roughness;
                        LayerMetal  = Dab.Metalness;
                        LayerHeight = Dab.HeightGain;
                    }
                }
            }
        }

        const float EffectiveWeight = std::clamp(LayerCoverage * Layer.Opacity * MaskWeight, 0.0f, 1.0f);
        if (EffectiveWeight <= 1.0e-4f)
        {
            continue;
        }

        if (Layer.ActiveChannels[static_cast<std::size_t>(PaintSurfaceChannel::Albedo)])
        {
            const float ChW = EffectiveWeight * Layer.ChannelOpacities[static_cast<std::size_t>(PaintSurfaceChannel::Albedo)];
            Out.AlbedoR = BlendChannelValue(Out.AlbedoR, LayerR, Layer.BlendMethod, ChW);
            Out.AlbedoG = BlendChannelValue(Out.AlbedoG, LayerG, Layer.BlendMethod, ChW);
            Out.AlbedoB = BlendChannelValue(Out.AlbedoB, LayerB, Layer.BlendMethod, ChW);
        }
        if (Layer.ActiveChannels[static_cast<std::size_t>(PaintSurfaceChannel::Roughness)])
        {
            const float ChW = EffectiveWeight * Layer.ChannelOpacities[static_cast<std::size_t>(PaintSurfaceChannel::Roughness)];
            Out.Roughness = BlendChannelValue(Out.Roughness, LayerRough, Layer.BlendMethod, ChW);
        }
        if (Layer.ActiveChannels[static_cast<std::size_t>(PaintSurfaceChannel::Metalness)])
        {
            const float ChW = EffectiveWeight * Layer.ChannelOpacities[static_cast<std::size_t>(PaintSurfaceChannel::Metalness)];
            Out.Metalness = BlendChannelValue(Out.Metalness, LayerMetal, Layer.BlendMethod, ChW);
        }
        if (Layer.ActiveChannels[static_cast<std::size_t>(PaintSurfaceChannel::Height)])
        {
            const float ChW = EffectiveWeight * Layer.ChannelOpacities[static_cast<std::size_t>(PaintSurfaceChannel::Height)];
            Out.Height = Out.Height + LayerHeight * ChW;
        }
        if (Layer.ActiveChannels[static_cast<std::size_t>(PaintSurfaceChannel::Clearcoat)])
        {
            const float ChW = EffectiveWeight * Layer.ChannelOpacities[static_cast<std::size_t>(PaintSurfaceChannel::Clearcoat)];
            Out.Clearcoat = BlendChannelValue(Out.Clearcoat, Layer.ChannelScalars[static_cast<std::size_t>(PaintSurfaceChannel::Clearcoat)], Layer.BlendMethod, ChW);
        }
        if (Layer.ActiveChannels[static_cast<std::size_t>(PaintSurfaceChannel::Sheen)])
        {
            const float ChW = EffectiveWeight * Layer.ChannelOpacities[static_cast<std::size_t>(PaintSurfaceChannel::Sheen)];
            Out.Sheen = BlendChannelValue(Out.Sheen, Layer.ChannelScalars[static_cast<std::size_t>(PaintSurfaceChannel::Sheen)], Layer.BlendMethod, ChW);
        }
        if (Layer.ActiveChannels[static_cast<std::size_t>(PaintSurfaceChannel::Emissive)])
        {
            const float ChW = EffectiveWeight * Layer.ChannelOpacities[static_cast<std::size_t>(PaintSurfaceChannel::Emissive)];
            const ColorRgba EmCol = Layer.ChannelTints[static_cast<std::size_t>(PaintSurfaceChannel::Emissive)];
            const float Glow = Layer.ChannelScalars[static_cast<std::size_t>(PaintSurfaceChannel::GlowPower)];
            Out.EmissiveR += (static_cast<float>(EmCol.Red) / 255.0f) * Glow * ChW * 0.45f;
            Out.EmissiveG += (static_cast<float>(EmCol.Green) / 255.0f) * Glow * ChW * 0.45f;
            Out.EmissiveB += (static_cast<float>(EmCol.Blue) / 255.0f) * Glow * ChW * 0.45f;
        }
    }
    return Out;
}

[[nodiscard]] uint8_t EncodeAcesSrgbByte(float LinearVal) noexcept
{
    const float X      = std::max(0.0f, LinearVal * 0.92f);
    const float Mapped = std::clamp((X * (2.51f * X + 0.03f)) / (X * (2.43f * X + 0.59f) + 0.14f), 0.0f, 1.0f);
    const float Srgb   = (Mapped <= 0.0031308f) ? (12.92f * Mapped) : (1.055f * std::pow(Mapped, 1.0f / 2.4f) - 0.055f);
    return static_cast<uint8_t>(std::lround(std::clamp(Srgb, 0.0f, 1.0f) * 255.0f));
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                               LAYER ENTRY METHODS
//------------------------------------------------------------------------------------------------------------------------

std::size_t PaintLayerEntry::CountActiveChannels() const noexcept
{
    std::size_t Total = 0u;
    for (bool Enabled : ActiveChannels)
    {
        if (Enabled)
        {
            ++Total;
        }
    }
    return Total;
}

std::string PaintLayerEntry::FormatSubtitle() const
{
    const std::size_t ChCount = CountActiveChannels();
    const char* BlendStr = "normal";
    switch (BlendMethod)
    {
        case PaintBlendMethod::Multiply:    BlendStr = "multiply";     break;
        case PaintBlendMethod::Screen:      BlendStr = "screen";       break;
        case PaintBlendMethod::Overlay:     BlendStr = "overlay";      break;
        case PaintBlendMethod::Add:         BlendStr = "add";          break;
        case PaintBlendMethod::SoftLight:   BlendStr = "soft light";   break;
        case PaintBlendMethod::Darken:      BlendStr = "darken";       break;
        case PaintBlendMethod::Lighten:     BlendStr = "lighten";      break;
        case PaintBlendMethod::ColorDodge:  BlendStr = "color dodge";  break;
        case PaintBlendMethod::HeightBlend: BlendStr = "height blend"; break;
        case PaintBlendMethod::Normal:
        default:                            BlendStr = "normal";       break;
    }

    char Buf[96];
    const int OpPct = static_cast<int>(std::lround(Opacity * 100.0f));
    if (Category == PaintLayerCategory::Folder)
    {
        std::snprintf(Buf, sizeof(Buf), "folder · %s · %d%%", BlendStr, OpPct);
    }
    else if (PartFilter >= 0 && PartFilter < 4)
    {
        std::snprintf(Buf, sizeof(Buf), "%s · %d%% · %zu ch · %s", BlendStr, OpPct, ChCount, TeapotPartNames[static_cast<std::size_t>(PartFilter)]);
    }
    else
    {
        std::snprintf(Buf, sizeof(Buf), "%s · %d%% · %zu ch", BlendStr, OpPct, ChCount);
    }
    return std::string(Buf);
}

//------------------------------------------------------------------------------------------------------------------------
//                                              SESSION LIFECYCLE & SETUP
//------------------------------------------------------------------------------------------------------------------------

TexturePaintSession::TexturePaintSession() noexcept
    : GeometryCache(std::make_unique<PaintGeometryCacheStorage>())
{
    ResetDefaults();
}

TexturePaintSession::~TexturePaintSession() noexcept = default;

void TexturePaintSession::ResetDefaults() noexcept
{
    DocumentName      = "Teapot Paint";
    ResolutionPx      = 512u;
    Study             = PaintStudyAsset::UtahTeapot;
    ViewportLayout    = PaintViewportLayout::ThreeDOnly;
    ViewChannelIndex  = 0u;
    ActiveTool        = PaintToolCategory::Brush;
    InspectorTab      = BrushInspectorTab::Brush;
    InspectorSubTab   = BrushDetailSubTab::LibraryGrid;
    WireframeOverlay  = false;
    MeshPartsExpanded = false;
    LayerFilter       = PaintFilterTab::AllLayers;
    SearchQuery.clear();
    ChannelQuery.clear();
    BrushLibraryQuery.clear();
    StatusBanner      = "Vulkan GPU";
    InspectorStatus   = "Saved locally";
    ChannelGroupOpen  = { true, false, false, false, false };

    ResetOrbitCamera();

    UndoHistory.clear();
    RedoHistory.clear();
    Layers.clear();

    // Layer 0 (bottom): Base Ceramic Coat (Fill layer)
    PaintLayerEntry CeramicCoat {};
    CeramicCoat.Id               = 1u;
    CeramicCoat.Category         = PaintLayerCategory::Fill;
    CeramicCoat.Name             = "Base Ceramic Coat";
    CeramicCoat.PrimerTint       = ColorRgba{ 196u, 200u, 208u, 255u };
    CeramicCoat.InspectedChannel = PaintSurfaceChannel::Albedo;
    EnablePrimaryFourChannels(CeramicCoat.ActiveChannels);
    InitializeDefaultLayerChannels(CeramicCoat);
    CeramicCoat.ChannelScalars[static_cast<std::size_t>(PaintSurfaceChannel::Roughness)] = 0.28f;
    CeramicCoat.ChannelScalars[static_cast<std::size_t>(PaintSurfaceChannel::Metalness)] = 0.04f;
    CeramicCoat.PatternRepeats = 1;
    Layers.push_back(std::move(CeramicCoat));

    // Layer 1 (top, selected): Hand Painted Layer (Paint layer with visible starter strokes)
    PaintLayerEntry HandPainted {};
    HandPainted.Id               = 2u;
    HandPainted.Category         = PaintLayerCategory::Paint;
    HandPainted.Name             = "Hand Painted Layer";
    HandPainted.PrimerTint       = ColorRgba{ 232u, 194u, 122u, 255u };
    HandPainted.InspectedChannel = PaintSurfaceChannel::Albedo;
    EnablePrimaryFourChannels(HandPainted.ActiveChannels);
    InitializeDefaultLayerChannels(HandPainted);
    HandPainted.ChannelScalars[static_cast<std::size_t>(PaintSurfaceChannel::Roughness)] = 0.24f;
    HandPainted.ChannelScalars[static_cast<std::size_t>(PaintSurfaceChannel::Metalness)] = 0.65f;
    HandPainted.ChannelScalars[static_cast<std::size_t>(PaintSurfaceChannel::Height)]    = 0.18f;

    // Seed decorative hand-painted brushwork across the teapot body so the 3D teapot & thumbnail show rich strokes immediately
    for (int Step = 0; Step < 14; ++Step)
    {
        const float T = static_cast<float>(Step) / 13.0f;
        PaintStrokeDabSpec Dab {};
        Dab.U          = 0.16f + T * 0.68f;
        Dab.V          = 0.46f + 0.10f * std::sin(T * 6.28318f);
        Dab.RadiusUv   = 0.075f - 0.02f * std::abs(T - 0.5f);
        Dab.Hardness   = 0.68f;
        Dab.Opacity    = 0.88f;
        Dab.Tint       = ColorRgba{ 232u, 194u, 122u, 255u };
        Dab.Roughness  = 0.22f;
        Dab.Metalness  = 0.72f;
        Dab.HeightGain = 0.16f;
        HandPainted.StrokeDabs.push_back(Dab);
    }
    for (int Step = 0; Step < 9; ++Step)
    {
        const float T = static_cast<float>(Step) / 8.0f;
        PaintStrokeDabSpec AccentDab {};
        AccentDab.U          = 0.22f + T * 0.54f;
        AccentDab.V          = 0.64f - 0.06f * std::cos(T * 3.14159f);
        AccentDab.RadiusUv   = 0.052f;
        AccentDab.Hardness   = 0.74f;
        AccentDab.Opacity    = 0.82f;
        AccentDab.Tint       = ColorRgba{ 78u, 152u, 196u, 255u };
        AccentDab.Roughness  = 0.30f;
        AccentDab.Metalness  = 0.25f;
        AccentDab.HeightGain = 0.10f;
        HandPainted.StrokeDabs.push_back(AccentDab);
    }
    Layers.push_back(std::move(HandPainted));

    SelectedLayerId  = 2u;
    NextLayerId      = 3u;
    NextStampId      = 1u;
    NextGradientPtId = 3u;
    ++ContentRevision;

    Brush = PaintBrushProfile{};
    EnablePrimaryFourChannels(Brush.PaintChannels);
    EnablePrimaryFourChannels(Brush.EraseChannels);
    EnablePrimaryFourChannels(Brush.FillChannels);

    Brush.Curves[0] = { { 0.0f, 1.0f }, { 0.45f, 0.86f }, { 0.80f, 0.35f }, { 1.0f, 0.0f } };
    Brush.Curves[1] = { { 0.0f, 1.0f }, { 0.50f, 1.00f }, { 1.0f, 1.00f } };
    Brush.Curves[2] = { { 0.0f, 1.0f }, { 0.50f, 1.00f }, { 1.0f, 1.00f } };
    Brush.Curves[3] = { { 0.0f, 0.5f }, { 0.50f, 0.50f }, { 1.0f, 0.50f } };
}

void TexturePaintSession::SelectTool(PaintToolCategory Tool) noexcept
{
    ActiveTool = Tool;
    switch (Tool)
    {
        case PaintToolCategory::Brush:      InspectorTab = BrushInspectorTab::Brush;    break;
        case PaintToolCategory::Eraser:     InspectorTab = BrushInspectorTab::Erase;    break;
        case PaintToolCategory::Fill:       InspectorTab = BrushInspectorTab::Fill;     break;
        case PaintToolCategory::Gradient3D: InspectorTab = BrushInspectorTab::Gradient; break;
        case PaintToolCategory::Curve3D:    InspectorTab = BrushInspectorTab::Curve;    break;
        case PaintToolCategory::Stamp:      InspectorTab = BrushInspectorTab::Stamp;    break;
        case PaintToolCategory::Text:       InspectorTab = BrushInspectorTab::Text;     break;
        case PaintToolCategory::Sample:     InspectorTab = BrushInspectorTab::Pick;     break;
        case PaintToolCategory::Move:       InspectorTab = BrushInspectorTab::Move;     break;
        default:                            InspectorTab = BrushInspectorTab::Brush;    break;
    }
}

void TexturePaintSession::SelectStudy(PaintStudyAsset NextStudy) noexcept
{
    if (static_cast<std::size_t>(NextStudy) < PaintStudyEntries.size() && Study != NextStudy)
    {
        Study = NextStudy;
        ResetOrbitCamera();
        ++ContentRevision;
    }
}

void TexturePaintSession::AssignResolution(uint32_t NextResPx) noexcept
{
    if (NextResPx == 512u || NextResPx == 1024u || NextResPx == 2048u || NextResPx == 4096u)
    {
        PushUndoSnapshot();
        ResolutionPx    = NextResPx;
        InspectorStatus = "Resolution updated";
        ++ContentRevision;
    }
}

void TexturePaintSession::SelectBrushPreset(uint32_t PresetIndex) noexcept
{
    if (PresetIndex >= BrushLibraryEntries.size())
    {
        return;
    }
    const auto& Preset        = BrushLibraryEntries[PresetIndex];
    Brush.ActivePresetIndex   = PresetIndex;
    Brush.TipShape            = Preset.TipShape;
    Brush.PixelRadius         = Preset.PixelRadius;
    Brush.Hardness            = Preset.Hardness;
    Brush.Spacing             = Preset.Spacing;
    Brush.DeliveryRate        = Preset.Delivery;
    Brush.Stabilizer          = Preset.Stabilizer;
    InspectorStatus           = std::string(Preset.Label) + " selected";
}

void TexturePaintSession::SelectTextFont(uint32_t FontIndex) noexcept
{
    if (FontIndex < FontPresetLabels.size())
    {
        Brush.TextFontIndex = FontIndex;
        if (auto* Sel = AccessSelectedLayer())
        {
            Sel->TextFontIndex = FontIndex;
            ++ContentRevision;
        }
        InspectorStatus = std::string("Font: ") + FontPresetLabels[FontIndex];
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                              CAMERA & GEOMETRY PREPARATION
//------------------------------------------------------------------------------------------------------------------------

void TexturePaintSession::ResetOrbitCamera() noexcept
{
    CameraYawRadians     = 0.68f;
    CameraPitchRadians   = 0.34f;
    CameraDistanceMeters = 2.08f;
    CameraPivotX         = 0.0f;
    CameraPivotY         = 0.0f;
    CameraPivotZ         = 0.44f;
}

void TexturePaintSession::OrbitCameraByDelta(float DeltaYawRad, float DeltaPitchRad) noexcept
{
    CameraYawRadians  += DeltaYawRad;
    CameraPitchRadians = std::clamp(CameraPitchRadians + DeltaPitchRad, -1.42f, 1.42f);
}

void TexturePaintSession::PanCameraByDelta(float RightMeters, float UpMeters) noexcept
{
    const float Cy = std::cos(CameraYawRadians);
    const float Sy = std::sin(CameraYawRadians);
    CameraPivotX += -Sy * RightMeters;
    CameraPivotY +=  Cy * RightMeters;
    CameraPivotZ +=  UpMeters;
}

void TexturePaintSession::ZoomCameraByFactor(float ScaleMultiplier) noexcept
{
    CameraDistanceMeters = std::clamp(CameraDistanceMeters * ScaleMultiplier, 0.45f, 12.0f);
}

void TexturePaintSession::EnsureStudyGeometryPrepared()
{
    if (!GeometryCache)
    {
        GeometryCache = std::make_unique<PaintGeometryCacheStorage>();
    }
    if (GeometryCache->CachedAsset != Study || GeometryCache->PreparedStudy.LowSurfaceVertices.empty())
    {
        GeometryCache->PreparedStudy = StudyGeometryBuilder::ConstructStudioStudy(MapToStudyGeometryCategory(Study));
        GeometryCache->CachedAsset   = Study;
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                              UNDO / REDO & LAYER STACK
//------------------------------------------------------------------------------------------------------------------------

void TexturePaintSession::PushUndoSnapshot() noexcept
{
    if (UndoHistory.size() >= 32u)
    {
        UndoHistory.erase(UndoHistory.begin());
    }
    UndoHistory.push_back(PaintDocumentSnapshot{ Layers, SelectedLayerId, NextLayerId, ResolutionPx });
    RedoHistory.clear();
}

bool TexturePaintSession::ApplyUndo() noexcept
{
    if (UndoHistory.empty())
    {
        return false;
    }
    RedoHistory.push_back(PaintDocumentSnapshot{ Layers, SelectedLayerId, NextLayerId, ResolutionPx });
    PaintDocumentSnapshot Prev = std::move(UndoHistory.back());
    UndoHistory.pop_back();
    Layers          = std::move(Prev.Layers);
    SelectedLayerId = Prev.SelectedLayerId;
    NextLayerId     = Prev.NextLayerId;
    ResolutionPx    = Prev.ResolutionPx;
    InspectorStatus = "Undone";
    ++ContentRevision;
    return true;
}

bool TexturePaintSession::ApplyRedo() noexcept
{
    if (RedoHistory.empty())
    {
        return false;
    }
    UndoHistory.push_back(PaintDocumentSnapshot{ Layers, SelectedLayerId, NextLayerId, ResolutionPx });
    PaintDocumentSnapshot Next = std::move(RedoHistory.back());
    RedoHistory.pop_back();
    Layers          = std::move(Next.Layers);
    SelectedLayerId = Next.SelectedLayerId;
    NextLayerId     = Next.NextLayerId;
    ResolutionPx    = Next.ResolutionPx;
    InspectorStatus = "Redone";
    ++ContentRevision;
    return true;
}

void TexturePaintSession::AddLayer(PaintLayerCategory Category) noexcept
{
    if (Layers.size() >= MaxLayerCount)
    {
        return;
    }
    PushUndoSnapshot();

    PaintLayerEntry Created {};
    Created.Id               = NextLayerId++;
    Created.Category         = Category;
    Created.InspectedChannel = PaintSurfaceChannel::Albedo;
    EnablePrimaryFourChannels(Created.ActiveChannels);
    InitializeDefaultLayerChannels(Created);

    if (const auto* Sel = InspectSelectedLayer())
    {
        if (Sel->Category == PaintLayerCategory::Folder)
        {
            Created.FolderOwnerId = Sel->Id;
        }
        else
        {
            Created.FolderOwnerId = Sel->FolderOwnerId;
        }
    }

    char NameBuf[64];
    switch (Category)
    {
        case PaintLayerCategory::Fill:
            std::snprintf(NameBuf, sizeof(NameBuf), "Fill Layer %u", Created.Id);
            Created.PrimerTint = Brush.InkColor;
            Created.ChannelTints[static_cast<std::size_t>(PaintSurfaceChannel::Albedo)] = Brush.InkColor;
            break;
        case PaintLayerCategory::Material:
            std::snprintf(NameBuf, sizeof(NameBuf), "Material Layer %u", Created.Id);
            Created.PrimerTint = ColorRgba{ 168u, 174u, 184u, 255u };
            Created.ChannelTints[static_cast<std::size_t>(PaintSurfaceChannel::Albedo)] = Created.PrimerTint;
            Created.ChannelScalars[static_cast<std::size_t>(PaintSurfaceChannel::Metalness)] = 0.85f;
            Created.ChannelScalars[static_cast<std::size_t>(PaintSurfaceChannel::Roughness)] = 0.24f;
            break;
        case PaintLayerCategory::Stamp:
            std::snprintf(NameBuf, sizeof(NameBuf), "Stamp Layer %u", Created.Id);
            Created.PrimerTint = Brush.StampTint;
            Created.Stamps.push_back(PaintStampPlacement{ NextStampId++, Brush.ActiveStampName, true, false, 0.50f, 0.50f, 0.30f, 0.0f, 1.0f, 0.5f, Brush.StampTint });
            Created.SelectedStampId = Created.Stamps.back().Id;
            break;
        case PaintLayerCategory::Text:
            std::snprintf(NameBuf, sizeof(NameBuf), "Text: %s", Brush.TextContent.substr(0u, 12u).c_str());
            Created.PrimerTint = Brush.InkColor;
            Created.Stamps.push_back(PaintStampPlacement{ NextStampId++, Brush.TextContent, true, false, 0.50f, 0.50f, 0.34f, 0.0f, 1.0f, 0.65f, Brush.InkColor });
            Created.SelectedStampId = Created.Stamps.back().Id;
            break;
        case PaintLayerCategory::WearGen:
            std::snprintf(NameBuf, sizeof(NameBuf), "Edge Wear %u", Created.Id);
            Created.PrimerTint = ColorRgba{ 212u, 176u, 122u, 255u };
            Created.ChannelTints[static_cast<std::size_t>(PaintSurfaceChannel::Albedo)] = Created.PrimerTint;
            break;
        case PaintLayerCategory::Gradient:
        {
            std::snprintf(NameBuf, sizeof(NameBuf), "Gradient Layer");
            Created.PrimerTint = ColorRgba{ 181u, 140u, 255u, 255u };
            PaintGradientPointSpec Pt1 {};
            Pt1.Id        = 1u;
            Pt1.Label     = "Point 1";
            Pt1.PositionX = -0.38f;
            Pt1.PositionY = -0.18f;
            Pt1.PositionZ = 0.58f;
            Pt1.TintColor = ColorRgba{ 232u, 194u, 122u, 255u };
            Pt1.Roughness = 0.22f;
            Pt1.Metalness = 0.85f;
            Pt1.Height    = 0.20f;
            Pt1.Radius    = 1.10f;
            Pt1.Strength  = 1.00f;

            PaintGradientPointSpec Pt2 {};
            Pt2.Id        = 2u;
            Pt2.Label     = "Point 2";
            Pt2.PositionX = 0.42f;
            Pt2.PositionY = 0.16f;
            Pt2.PositionZ = 0.34f;
            Pt2.TintColor = ColorRgba{ 47u, 109u, 117u, 255u };
            Pt2.Roughness = 0.72f;
            Pt2.Metalness = 0.00f;
            Pt2.Height    = -0.10f;
            Pt2.Radius    = 1.10f;
            Pt2.Strength  = 1.00f;

            Created.GradientPoints.push_back(Pt1);
            Created.GradientPoints.push_back(Pt2);
            Created.SelectedGradientPointIndex = 0;
            break;
        }
        case PaintLayerCategory::Folder:
            std::snprintf(NameBuf, sizeof(NameBuf), "Layer Group %u", Created.Id);
            Created.ActiveChannels.fill(false);
            break;
        case PaintLayerCategory::Paint:
        default:
            std::snprintf(NameBuf, sizeof(NameBuf), "Paint Layer %u", Created.Id);
            break;
    }
    Created.Name = NameBuf;

    Layers.push_back(std::move(Created));
    SelectedLayerId = Layers.back().Id;
    InspectorStatus = "Layer added";
    ++ContentRevision;
}

void TexturePaintSession::DuplicateSelectedLayer() noexcept
{
    const int32_t Idx = FindLayerVectorIndex(SelectedLayerId);
    if (Idx < 0 || Layers.size() >= MaxLayerCount)
    {
        return;
    }
    PushUndoSnapshot();
    PaintLayerEntry Copy = Layers[static_cast<std::size_t>(Idx)];
    Copy.Id              = NextLayerId++;
    Copy.Name           += " copy";
    for (auto& St : Copy.Stamps)
    {
        St.Id = NextStampId++;
    }
    Copy.SelectedStampId = Copy.Stamps.empty() ? 0u : Copy.Stamps.front().Id;
    Layers.insert(Layers.begin() + Idx + 1, std::move(Copy));
    SelectedLayerId = Layers[static_cast<std::size_t>(Idx + 1)].Id;
    InspectorStatus = "Layer duplicated";
    ++ContentRevision;
}

void TexturePaintSession::DeleteSelectedLayer() noexcept
{
    if (Layers.size() <= 1u)
    {
        return;
    }
    const int32_t Idx = FindLayerVectorIndex(SelectedLayerId);
    if (Idx < 0)
    {
        return;
    }
    if (Layers[static_cast<std::size_t>(Idx)].Locked)
    {
        InspectorStatus = "Layer is locked";
        return;
    }
    PushUndoSnapshot();
    const uint32_t RemovedId = Layers[static_cast<std::size_t>(Idx)].Id;
    Layers.erase(Layers.begin() + Idx);
    for (auto& L : Layers)
    {
        if (L.FolderOwnerId == RemovedId)
        {
            L.FolderOwnerId = 0u;
        }
    }
    const int32_t NextIdx = std::clamp(Idx - 1, 0, static_cast<int32_t>(Layers.size()) - 1);
    SelectedLayerId       = Layers[static_cast<std::size_t>(NextIdx)].Id;
    InspectorStatus       = "Layer removed";
    ++ContentRevision;
}

bool TexturePaintSession::MoveSelectedLayerUp() noexcept
{
    const int32_t Idx = FindLayerVectorIndex(SelectedLayerId);
    if (Idx < 0 || static_cast<std::size_t>(Idx) + 1u >= Layers.size())
    {
        return false;
    }
    PushUndoSnapshot();
    std::swap(Layers[static_cast<std::size_t>(Idx)], Layers[static_cast<std::size_t>(Idx) + 1u]);
    ++ContentRevision;
    return true;
}

bool TexturePaintSession::MoveSelectedLayerDown() noexcept
{
    const int32_t Idx = FindLayerVectorIndex(SelectedLayerId);
    if (Idx <= 0)
    {
        return false;
    }
    PushUndoSnapshot();
    std::swap(Layers[static_cast<std::size_t>(Idx)], Layers[static_cast<std::size_t>(Idx) - 1u]);
    ++ContentRevision;
    return true;
}

void TexturePaintSession::ToggleOrCreateMaskOnSelectedLayer() noexcept
{
    auto* Sel = AccessSelectedLayer();
    if (Sel == nullptr || Sel->Locked)
    {
        return;
    }
    PushUndoSnapshot();
    if (!Sel->Mask.Attached)
    {
        Sel->Mask.Attached       = true;
        Sel->Mask.EditingMask    = true;
        Sel->Mask.Enabled        = true;
        Sel->Mask.FillBrightness = 255u;
        InspectorStatus          = "Mask added";
    }
    else
    {
        Sel->Mask.Attached    = false;
        Sel->Mask.EditingMask = false;
        InspectorStatus       = "Mask removed";
    }
    ++ContentRevision;
}

void TexturePaintSession::AddChannelToSelectedLayer(PaintSurfaceChannel Channel) noexcept
{
    auto* Sel = AccessSelectedLayer();
    const std::size_t Idx = static_cast<std::size_t>(Channel);
    if (Sel == nullptr || Sel->Locked || Idx >= ChannelCount)
    {
        return;
    }
    PushUndoSnapshot();
    Sel->ActiveChannels[Idx]   = true;
    Sel->ExpandedChannels[Idx] = true;
    Sel->InspectedChannel      = Channel;
    ++ContentRevision;
}

void TexturePaintSession::RemoveChannelFromSelectedLayer(PaintSurfaceChannel Channel) noexcept
{
    auto* Sel = AccessSelectedLayer();
    const std::size_t Idx = static_cast<std::size_t>(Channel);
    if (Sel == nullptr || Sel->Locked || Idx >= ChannelCount)
    {
        return;
    }
    PushUndoSnapshot();
    Sel->ActiveChannels[Idx] = false;
    if (Sel->InspectedChannel == Channel)
    {
        for (std::size_t I = 0u; I < ChannelCount; ++I)
        {
            if (Sel->ActiveChannels[I])
            {
                Sel->InspectedChannel = static_cast<PaintSurfaceChannel>(I);
                break;
            }
        }
    }
    ++ContentRevision;
}

void TexturePaintSession::ToggleChannelOnSelectedLayer(PaintSurfaceChannel Channel) noexcept
{
    auto* Sel = AccessSelectedLayer();
    const std::size_t Idx = static_cast<std::size_t>(Channel);
    if (Sel == nullptr || Sel->Locked || Idx >= ChannelCount)
    {
        return;
    }
    PushUndoSnapshot();
    Sel->ActiveChannels[Idx] = !Sel->ActiveChannels[Idx];
    if (Sel->ActiveChannels[Idx])
    {
        Sel->ExpandedChannels[Idx] = true;
        Sel->InspectedChannel      = Channel;
    }
    ++ContentRevision;
}

void TexturePaintSession::ToggleChannelGroupOnSelectedLayer(PaintChannelGroup Group) noexcept
{
    auto* Sel = AccessSelectedLayer();
    if (Sel == nullptr || Sel->Locked)
    {
        return;
    }
    PushUndoSnapshot();
    bool AllEnabled = true;
    for (std::size_t I = 0u; I < ChannelCount; ++I)
    {
        if (PaintChannelEntries[I].Group == Group && !Sel->ActiveChannels[I])
        {
            AllEnabled = false;
            break;
        }
    }
    for (std::size_t I = 0u; I < ChannelCount; ++I)
    {
        if (PaintChannelEntries[I].Group == Group)
        {
            Sel->ActiveChannels[I] = !AllEnabled;
        }
    }
    ++ContentRevision;
}

void TexturePaintSession::ApplyChannelBulkPreset(uint32_t BulkMode) noexcept
{
    auto* Sel = AccessSelectedLayer();
    if (Sel == nullptr || Sel->Locked)
    {
        return;
    }
    PushUndoSnapshot();
    if (BulkMode == 0u) // All 22
    {
        Sel->ActiveChannels.fill(true);
    }
    else if (BulkMode == 1u) // Core (Primary 7)
    {
        for (std::size_t I = 0u; I < ChannelCount; ++I)
        {
            Sel->ActiveChannels[I] = (PaintChannelEntries[I].Group == PaintChannelGroup::Primary);
        }
    }
    else // One (Albedo only)
    {
        Sel->ActiveChannels.fill(false);
        Sel->ActiveChannels[static_cast<std::size_t>(PaintSurfaceChannel::Albedo)] = true;
    }
    ++ContentRevision;
}

void TexturePaintSession::AddStampToSelectedLayer(bool TextStampMode) noexcept
{
    auto* Sel = AccessSelectedLayer();
    if (Sel == nullptr || Sel->Locked || Sel->Category == PaintLayerCategory::Folder)
    {
        return;
    }
    PushUndoSnapshot();
    PaintStampPlacement Placement {};
    Placement.Id        = NextStampId++;
    Placement.Name      = TextStampMode ? Brush.TextContent : Brush.ActiveStampName;
    Placement.TintColor = TextStampMode ? Brush.InkColor : Brush.StampTint;
    Sel->Stamps.push_back(std::move(Placement));
    Sel->SelectedStampId = Sel->Stamps.back().Id;
    ++ContentRevision;
}

void TexturePaintSession::RemoveStampFromSelectedLayer(uint32_t StampId) noexcept
{
    auto* Sel = AccessSelectedLayer();
    if (Sel == nullptr || Sel->Locked)
    {
        return;
    }
    PushUndoSnapshot();
    Sel->Stamps.erase(
        std::remove_if(Sel->Stamps.begin(), Sel->Stamps.end(), [StampId](const PaintStampPlacement& S) { return S.Id == StampId; }),
        Sel->Stamps.end());
    Sel->SelectedStampId = Sel->Stamps.empty() ? 0u : Sel->Stamps.front().Id;
    ++ContentRevision;
}

void TexturePaintSession::AddGradientPointToSelectedLayer() noexcept
{
    auto* Sel = AccessSelectedLayer();
    if (Sel == nullptr || Sel->Locked || Sel->GradientPoints.size() >= 12u)
    {
        return;
    }
    PushUndoSnapshot();
    PaintGradientPointSpec NextPt {};
    NextPt.Id   = NextGradientPtId++;
    char LabelBuf[32];
    std::snprintf(LabelBuf, sizeof(LabelBuf), "Point %zu", Sel->GradientPoints.size() + 1u);
    NextPt.Label = LabelBuf;

    if (!Sel->GradientPoints.empty())
    {
        const auto& Ref = Sel->GradientPoints.back();
        NextPt.PositionX = -Ref.PositionX;
        NextPt.PositionY =  Ref.PositionY;
        NextPt.PositionZ = -Ref.PositionZ + 0.45f;
        NextPt.TintColor = Brush.InkColor;
        NextPt.Roughness = Ref.Roughness;
        NextPt.Metalness = Ref.Metalness;
        NextPt.Height    = Ref.Height;
    }
    Sel->GradientPoints.push_back(NextPt);
    Sel->SelectedGradientPointIndex = static_cast<int32_t>(Sel->GradientPoints.size()) - 1;
    InspectorStatus = "Point added · drag on model";
    ++ContentRevision;
}

void TexturePaintSession::RemoveGradientPointFromSelectedLayer() noexcept
{
    auto* Sel = AccessSelectedLayer();
    if (Sel == nullptr || Sel->Locked || Sel->GradientPoints.size() <= 1u || Sel->SelectedGradientPointIndex < 0)
    {
        return;
    }
    PushUndoSnapshot();
    const std::size_t Idx = static_cast<std::size_t>(std::clamp(
        Sel->SelectedGradientPointIndex,
        0,
        static_cast<int32_t>(Sel->GradientPoints.size()) - 1));
    Sel->GradientPoints.erase(Sel->GradientPoints.begin() + static_cast<std::ptrdiff_t>(Idx));
    for (std::size_t I = 0u; I < Sel->GradientPoints.size(); ++I)
    {
        char LabelBuf[32];
        std::snprintf(LabelBuf, sizeof(LabelBuf), "Point %zu", I + 1u);
        Sel->GradientPoints[I].Label = LabelBuf;
    }
    Sel->SelectedGradientPointIndex = std::clamp(
        Sel->SelectedGradientPointIndex - 1,
        0,
        static_cast<int32_t>(Sel->GradientPoints.size()) - 1);
    InspectorStatus = "Point removed";
    ++ContentRevision;
}

//------------------------------------------------------------------------------------------------------------------------
//                                        3D SURFACE & 2D UV RASTERIZATION
//------------------------------------------------------------------------------------------------------------------------

bool TexturePaintSession::ProjectWorldPointToViewport(
    float    WorldX,
    float    WorldY,
    float    WorldZ,
    float    ViewportWidth,
    float    ViewportHeight,
    Point2D& OutViewportPoint) const noexcept
{
    if (ViewportWidth <= 4.0f || ViewportHeight <= 4.0f)
    {
        return false;
    }
    const float CosP = std::cos(CameraPitchRadians);
    const float SinP = std::sin(CameraPitchRadians);
    const float CosY = std::cos(CameraYawRadians);
    const float SinY = std::sin(CameraYawRadians);

    const float EyeX = CameraPivotX + CameraDistanceMeters * CosP * CosY;
    const float EyeY = CameraPivotY + CameraDistanceMeters * CosP * SinY;
    const float EyeZ = CameraPivotZ + CameraDistanceMeters * SinP;

    float FwdX = CameraPivotX - EyeX;
    float FwdY = CameraPivotY - EyeY;
    float FwdZ = CameraPivotZ - EyeZ;
    const float FwdLen = std::max(1.0e-5f, std::sqrt(FwdX * FwdX + FwdY * FwdY + FwdZ * FwdZ));
    FwdX /= FwdLen; FwdY /= FwdLen; FwdZ /= FwdLen;

    // Right = normalize(cross(Fwd, WorldUp(0,0,1)))
    float RightX = FwdY;
    float RightY = -FwdX;
    float RightZ = 0.0f;
    const float RightLen = std::max(1.0e-5f, std::sqrt(RightX * RightX + RightY * RightY));
    RightX /= RightLen; RightY /= RightLen;

    // CamUp = cross(Right, Fwd)
    const float UpX = RightY * FwdZ - RightZ * FwdY;
    const float UpY = RightZ * FwdX - RightX * FwdZ;
    const float UpZ = RightX * FwdY - RightY * FwdX;

    const float RelX = WorldX - EyeX;
    const float RelY = WorldY - EyeY;
    const float RelZ = WorldZ - EyeZ;

    const float CamZ = RelX * FwdX + RelY * FwdY + RelZ * FwdZ;
    if (CamZ <= 0.08f)
    {
        return false;
    }
    const float CamX   = RelX * RightX + RelY * RightY + RelZ * RightZ;
    const float CamY   = RelX * UpX    + RelY * UpY    + RelZ * UpZ;
    const float Aspect = ViewportWidth / ViewportHeight;
    const float TanHalfFov = 0.39f;

    const float NdcX = CamX / (CamZ * TanHalfFov * Aspect);
    const float NdcY = CamY / (CamZ * TanHalfFov);

    OutViewportPoint.X = (NdcX * 0.5f + 0.5f) * ViewportWidth;
    OutViewportPoint.Y = (0.5f - NdcY * 0.5f) * ViewportHeight;
    return true;
}

bool TexturePaintSession::RenderSpatialViewportRgba8(
    uint32_t              WidthPx,
    uint32_t              HeightPx,
    std::vector<uint8_t>& OutRgba8)
{
    if (WidthPx < 16u || HeightPx < 16u)
    {
        return false;
    }
    EnsureStudyGeometryPrepared();
    const auto& Verts = GeometryCache->PreparedStudy.LowSurfaceVertices;
    if (Verts.size() < 3u)
    {
        return false;
    }

    const std::size_t PixelCount = static_cast<std::size_t>(WidthPx) * HeightPx;
    OutRgba8.resize(PixelCount * 4u);

    GeometryCache->LastViewportW = WidthPx;
    GeometryCache->LastViewportH = HeightPx;
    GeometryCache->HitMask.assign(PixelCount, 0u);
    GeometryCache->HitUvU.assign(PixelCount, 0.0f);
    GeometryCache->HitUvV.assign(PixelCount, 0.0f);
    GeometryCache->HitWorldX.assign(PixelCount, 0.0f);
    GeometryCache->HitWorldY.assign(PixelCount, 0.0f);
    GeometryCache->HitWorldZ.assign(PixelCount, 0.0f);
    GeometryCache->HitPart.assign(PixelCount, 0u);

    // 1. Pre-evaluate 2D UV layer stack on a 192x192 UV grid for fast triangle shading
    constexpr uint32_t AtlasDim = 192u;
    std::vector<EvaluatedPbrTexel> UvAtlas(static_cast<std::size_t>(AtlasDim) * AtlasDim);
    bool Has3DGradientOrWear = false;
    for (const auto& L : Layers)
    {
        if (L.Visible && (L.Category == PaintLayerCategory::Gradient || L.Category == PaintLayerCategory::WearGen || L.PartFilter >= 0))
        {
            Has3DGradientOrWear = true;
        }
    }
    for (uint32_t Av = 0u; Av < AtlasDim; ++Av)
    {
        const float V = (static_cast<float>(Av) + 0.5f) / static_cast<float>(AtlasDim);
        for (uint32_t Au = 0u; Au < AtlasDim; ++Au)
        {
            const float U = (static_cast<float>(Au) + 0.5f) / static_cast<float>(AtlasDim);
            UvAtlas[static_cast<std::size_t>(Av) * AtlasDim + Au] =
                EvaluateLayerStackAtSurfacePoint(Layers, U, V, 0.0f, 0.0f, 0.45f, 0.65f, 0u);
        }
    }

    // 2. Fill radial studio backdrop (#1a1c21 center -> #0e1014 vignette + subtle floor shadow)
    const float InvW = 1.0f / static_cast<float>(WidthPx);
    const float InvH = 1.0f / static_cast<float>(HeightPx);
    for (uint32_t Py = 0u; Py < HeightPx; ++Py)
    {
        const float Ny = static_cast<float>(Py) * InvH;
        const float Cy = Ny * 2.0f - 1.0f;
        for (uint32_t Px = 0u; Px < WidthPx; ++Px)
        {
            const float Nx = static_cast<float>(Px) * InvW;
            const float Cx = Nx * 2.0f - 1.0f;
            const float RadSq = Cx * Cx + Cy * Cy;
            const float Vig   = std::clamp(1.0f - 0.38f * RadSq, 0.35f, 1.0f);
            // Subtle floor shadow ellipse around (0.0, 0.42)
            const float FloorDx = Cx * 1.35f;
            const float FloorDy = (Cy - 0.46f) * 3.8f;
            const float FloorShadow = 1.0f - 0.32f * std::exp(-(FloorDx * FloorDx + FloorDy * FloorDy) * 2.4f);
            const float BaseLum = (0.095f + 0.032f * (1.0f - Ny)) * Vig * FloorShadow;

            const std::size_t Idx = (static_cast<std::size_t>(Py) * WidthPx + Px) * 4u;
            OutRgba8[Idx + 0u] = static_cast<uint8_t>(std::clamp(BaseLum * 245.0f, 0.0f, 255.0f));
            OutRgba8[Idx + 1u] = static_cast<uint8_t>(std::clamp(BaseLum * 250.0f, 0.0f, 255.0f));
            OutRgba8[Idx + 2u] = static_cast<uint8_t>(std::clamp(BaseLum * 268.0f, 0.0f, 255.0f));
            OutRgba8[Idx + 3u] = 255u;
        }
    }

    // 3. Setup Camera basis (Right-Handed Z-Up matching StudyGeometryBuilder)
    const float CosP = std::cos(CameraPitchRadians);
    const float SinP = std::sin(CameraPitchRadians);
    const float CosY = std::cos(CameraYawRadians);
    const float SinY = std::sin(CameraYawRadians);

    const float EyeX = CameraPivotX + CameraDistanceMeters * CosP * CosY;
    const float EyeY = CameraPivotY + CameraDistanceMeters * CosP * SinY;
    const float EyeZ = CameraPivotZ + CameraDistanceMeters * SinP;

    float FwdX = CameraPivotX - EyeX;
    float FwdY = CameraPivotY - EyeY;
    float FwdZ = CameraPivotZ - EyeZ;
    const float FwdLen = std::max(1.0e-5f, std::sqrt(FwdX * FwdX + FwdY * FwdY + FwdZ * FwdZ));
    FwdX /= FwdLen; FwdY /= FwdLen; FwdZ /= FwdLen;

    float RightX = FwdY;
    float RightY = -FwdX;
    float RightZ = 0.0f;
    const float RightLen = std::max(1.0e-5f, std::sqrt(RightX * RightX + RightY * RightY));
    RightX /= RightLen; RightY /= RightLen;

    const float UpX = RightY * FwdZ - RightZ * FwdY;
    const float UpY = RightZ * FwdX - RightX * FwdZ;
    const float UpZ = RightX * FwdY - RightY * FwdX;

    const float Aspect     = static_cast<float>(WidthPx) / static_cast<float>(HeightPx);
    const float TanHalfFov = 0.39f;
    const float HalfW      = 0.5f * static_cast<float>(WidthPx);
    const float HalfH      = 0.5f * static_cast<float>(HeightPx);

    std::vector<float> DepthBuffer(PixelCount, 1.0e30f);

    struct ProjectedVertex
    {
        float ScreenX;
        float ScreenY;
        float CamZ;
        float InvZ;
    };

    const auto ProjectVertex = [&](const GpuLowSurfaceVertex& Vtx) noexcept -> ProjectedVertex
    {
        const float Rx = Vtx.Position[0] - EyeX;
        const float Ry = Vtx.Position[1] - EyeY;
        const float Rz = Vtx.Position[2] - EyeZ;
        const float Cz = Rx * FwdX   + Ry * FwdY   + Rz * FwdZ;
        const float Cx = Rx * RightX + Ry * RightY + Rz * RightZ;
        const float Cy = Rx * UpX    + Ry * UpY    + Rz * UpZ;
        const float SafeZ = std::max(0.05f, Cz);
        const float InvZ  = 1.0f / SafeZ;
        const float NdcX  = (Cx * InvZ) / (TanHalfFov * Aspect);
        const float NdcY  = (Cy * InvZ) / TanHalfFov;
        return {
            (NdcX + 1.0f) * HalfW,
            (1.0f - NdcY) * HalfH,
            Cz,
            InvZ
        };
    };

    // Studio key, fill & rim directions in Z-Up world space
    const float KeyX = 0.48f,  KeyY = 0.42f,  KeyZ = 0.77f;
    const float FillX = -0.68f, FillY = 0.52f, FillZ = 0.51f;
    const float RimX = -0.32f, RimY = -0.78f, RimZ = 0.54f;

    const std::size_t TriCount = Verts.size() / 3u;
    for (std::size_t TriIdx = 0u; TriIdx < TriCount; ++TriIdx)
    {
        const auto& V0 = Verts[TriIdx * 3u + 0u];
        const auto& V1 = Verts[TriIdx * 3u + 1u];
        const auto& V2 = Verts[TriIdx * 3u + 2u];

        const ProjectedVertex P0 = ProjectVertex(V0);
        const ProjectedVertex P1 = ProjectVertex(V1);
        const ProjectedVertex P2 = ProjectVertex(V2);

        if (P0.CamZ <= 0.08f || P1.CamZ <= 0.08f || P2.CamZ <= 0.08f)
        {
            continue;
        }

        const float Area2 = (P1.ScreenX - P0.ScreenX) * (P2.ScreenY - P0.ScreenY) -
                            (P1.ScreenY - P0.ScreenY) * (P2.ScreenX - P0.ScreenX);
        if (std::abs(Area2) < 1.0e-3f)
        {
            continue;
        }
        const float InvArea = 1.0f / Area2;

        const int MinX = std::max(0, static_cast<int>(std::floor(std::min({ P0.ScreenX, P1.ScreenX, P2.ScreenX }))));
        const int MaxX = std::min(static_cast<int>(WidthPx) - 1, static_cast<int>(std::ceil(std::max({ P0.ScreenX, P1.ScreenX, P2.ScreenX }))));
        const int MinY = std::max(0, static_cast<int>(std::floor(std::min({ P0.ScreenY, P1.ScreenY, P2.ScreenY }))));
        const int MaxY = std::min(static_cast<int>(HeightPx) - 1, static_cast<int>(std::ceil(std::max({ P0.ScreenY, P1.ScreenY, P2.ScreenY }))));

        if (MinX > MaxX || MinY > MaxY)
        {
            continue;
        }

        const uint32_t PartOrd = static_cast<uint32_t>(std::max(0.0f, V0.PartOrdinal)) % 4u;

        for (int Py = MinY; Py <= MaxY; ++Py)
        {
            const float Sy = static_cast<float>(Py) + 0.5f;
            for (int Px = MinX; Px <= MaxX; ++Px)
            {
                const float Sx = static_cast<float>(Px) + 0.5f;

                float W0 = ((P1.ScreenX - Sx) * (P2.ScreenY - Sy) - (P1.ScreenY - Sy) * (P2.ScreenX - Sx)) * InvArea;
                float W1 = ((P2.ScreenX - Sx) * (P0.ScreenY - Sy) - (P2.ScreenY - Sy) * (P0.ScreenX - Sx)) * InvArea;
                float W2 = 1.0f - W0 - W1;
                if (W0 < -1.0e-4f || W1 < -1.0e-4f || W2 < -1.0e-4f)
                {
                    continue;
                }

                const float PerspW0 = W0 * P0.InvZ;
                const float PerspW1 = W1 * P1.InvZ;
                const float PerspW2 = W2 * P2.InvZ;
                const float InvSum  = 1.0f / std::max(1.0e-7f, PerspW0 + PerspW1 + PerspW2);
                const float B0      = PerspW0 * InvSum;
                const float B1      = PerspW1 * InvSum;
                const float B2      = PerspW2 * InvSum;

                const float DepthZ = B0 * P0.CamZ + B1 * P1.CamZ + B2 * P2.CamZ;
                const std::size_t PixIdx = static_cast<std::size_t>(Py) * WidthPx + static_cast<std::size_t>(Px);
                if (DepthZ >= DepthBuffer[PixIdx])
                {
                    continue;
                }
                DepthBuffer[PixIdx] = DepthZ;

                const float U = std::clamp(B0 * V0.UvCoordU + B1 * V1.UvCoordU + B2 * V2.UvCoordU, 0.0f, 1.0f);
                const float V = std::clamp(B0 * V0.UvCoordV + B1 * V1.UvCoordV + B2 * V2.UvCoordV, 0.0f, 1.0f);

                const float Wx = B0 * V0.Position[0] + B1 * V1.Position[0] + B2 * V2.Position[0];
                const float Wy = B0 * V0.Position[1] + B1 * V1.Position[1] + B2 * V2.Position[1];
                const float Wz = B0 * V0.Position[2] + B1 * V1.Position[2] + B2 * V2.Position[2];

                float Nx = B0 * V0.SmoothNormal[0] + B1 * V1.SmoothNormal[0] + B2 * V2.SmoothNormal[0];
                float Ny = B0 * V0.SmoothNormal[1] + B1 * V1.SmoothNormal[1] + B2 * V2.SmoothNormal[1];
                float Nz = B0 * V0.SmoothNormal[2] + B1 * V1.SmoothNormal[2] + B2 * V2.SmoothNormal[2];
                const float NLen = std::max(1.0e-5f, std::sqrt(Nx * Nx + Ny * Ny + Nz * Nz));
                Nx /= NLen; Ny /= NLen; Nz /= NLen;

                // Record O(1) surface hit buffer for interactive 3D painting & picking
                GeometryCache->HitMask[PixIdx]   = 1u;
                GeometryCache->HitUvU[PixIdx]    = U;
                GeometryCache->HitUvV[PixIdx]    = V;
                GeometryCache->HitWorldX[PixIdx] = Wx;
                GeometryCache->HitWorldY[PixIdx] = Wy;
                GeometryCache->HitWorldZ[PixIdx] = Wz;
                GeometryCache->HitPart[PixIdx]   = static_cast<uint8_t>(PartOrd);

                EvaluatedPbrTexel Surf {};
                if (Has3DGradientOrWear)
                {
                    Surf = EvaluateLayerStackAtSurfacePoint(Layers, U, V, Wx, Wy, Wz, Nz, PartOrd);
                }
                else
                {
                    const uint32_t Au = std::min(AtlasDim - 1u, static_cast<uint32_t>(U * static_cast<float>(AtlasDim)));
                    const uint32_t Av = std::min(AtlasDim - 1u, static_cast<uint32_t>(V * static_cast<float>(AtlasDim)));
                    Surf = UvAtlas[static_cast<std::size_t>(Av) * AtlasDim + Au];
                }

                float OutR = 0.0f;
                float OutG = 0.0f;
                float OutB = 0.0f;

                if (ViewChannelIndex == 0u) // Full Lit OpenPBR
                {
                    float Vx = EyeX - Wx;
                    float Vy = EyeY - Wy;
                    float Vz = EyeZ - Wz;
                    const float VLen = std::max(1.0e-5f, std::sqrt(Vx * Vx + Vy * Vy + Vz * Vz));
                    Vx /= VLen; Vy /= VLen; Vz /= VLen;

                    // Double-sided normal alignment for thin teapot rim
                    if (Nx * Vx + Ny * Vy + Nz * Vz < 0.0f)
                    {
                        Nx = -Nx; Ny = -Ny; Nz = -Nz;
                    }
                    const float NdotV = std::max(0.02f, Nx * Vx + Ny * Vy + Nz * Vz);
                    const float Rough = std::clamp(Surf.Roughness, 0.04f, 1.0f);
                    const float Metal = std::clamp(Surf.Metalness, 0.0f, 1.0f);

                    const float DiffR = Surf.AlbedoR * (1.0f - Metal);
                    const float DiffG = Surf.AlbedoG * (1.0f - Metal);
                    const float DiffB = Surf.AlbedoB * (1.0f - Metal);

                    const float F0R = 0.04f * (1.0f - Metal) + Surf.AlbedoR * Metal;
                    const float F0G = 0.04f * (1.0f - Metal) + Surf.AlbedoG * Metal;
                    const float F0B = 0.04f * (1.0f - Metal) + Surf.AlbedoB * Metal;

                    const float Fresnel = std::pow(1.0f - NdotV, 5.0f);
                    const float SpecEnvScale = (1.0f - Rough * 0.72f);
                    const float FrR = F0R + (std::max(1.0f - Rough, F0R) - F0R) * Fresnel;
                    const float FrG = F0G + (std::max(1.0f - Rough, F0G) - F0G) * Fresnel;
                    const float FrB = F0B + (std::max(1.0f - Rough, F0B) - F0B) * Fresnel;

                    const auto AccumLight = [&](float Lx, float Ly, float Lz, float Lr, float Lg, float Lb) noexcept
                    {
                        const float NdotL = std::max(0.0f, Nx * Lx + Ny * Ly + Nz * Lz);
                        if (NdotL <= 0.0f) return;
                        float Hx = Lx + Vx, Hy = Ly + Vy, Hz = Lz + Vz;
                        const float HLen = std::max(1.0e-5f, std::sqrt(Hx * Hx + Hy * Hy + Hz * Hz));
                        Hx /= HLen; Hy /= HLen; Hz /= HLen;
                        const float NdotH = std::max(0.0f, Nx * Hx + Ny * Hy + Nz * Hz);
                        const float Alpha = Rough * Rough;
                        const float A2    = std::max(0.001f, Alpha * Alpha);
                        const float Denom = (NdotH * NdotH * (A2 - 1.0f) + 1.0f);
                        const float D     = A2 / std::max(1.0e-4f, 3.14159f * Denom * Denom);
                        const float Spec  = std::min(12.0f, D * 0.25f) * NdotL;

                        OutR += (DiffR * (NdotL / 3.14159f) + FrR * Spec) * Lr;
                        OutG += (DiffG * (NdotL / 3.14159f) + FrG * Spec) * Lg;
                        OutB += (DiffB * (NdotL / 3.14159f) + FrB * Spec) * Lb;
                    };

                    AccumLight(KeyX,  KeyY,  KeyZ,  2.55f, 2.48f, 2.38f);
                    AccumLight(FillX, FillY, FillZ, 1.15f, 1.28f, 1.48f);
                    AccumLight(RimX,  RimY,  RimZ,  1.30f, 1.40f, 1.62f);

                    // Studio hemisphere IBL
                    const float SkyT = std::clamp(Nz * 0.5f + 0.5f, 0.0f, 1.0f);
                    const float AmbR = 0.08f + 0.14f * SkyT;
                    const float AmbG = 0.09f + 0.16f * SkyT;
                    const float AmbB = 0.11f + 0.20f * SkyT;
                    OutR += DiffR * AmbR + FrR * AmbR * 1.35f * SpecEnvScale + Surf.EmissiveR;
                    OutG += DiffG * AmbG + FrG * AmbG * 1.35f * SpecEnvScale + Surf.EmissiveG;
                    OutB += DiffB * AmbB + FrB * AmbB * 1.35f * SpecEnvScale + Surf.EmissiveB;
                }
                else if (ViewChannelIndex == 1u) // Albedo
                {
                    OutR = Surf.AlbedoR; OutG = Surf.AlbedoG; OutB = Surf.AlbedoB;
                }
                else if (ViewChannelIndex == 2u) // Roughness
                {
                    OutR = OutG = OutB = Surf.Roughness;
                }
                else if (ViewChannelIndex == 3u) // Metalness
                {
                    OutR = OutG = OutB = Surf.Metalness;
                }
                else if (ViewChannelIndex == 4u) // Height
                {
                    OutR = OutG = OutB = std::clamp(Surf.Height * 0.5f + 0.5f, 0.0f, 1.0f);
                }
                else if (ViewChannelIndex == 5u) // Normal
                {
                    OutR = Nx * 0.5f + 0.5f;
                    OutG = Ny * 0.5f + 0.5f;
                    OutB = Nz * 0.5f + 0.5f;
                }
                else
                {
                    OutR = Surf.AlbedoR; OutG = Surf.AlbedoG; OutB = Surf.AlbedoB;
                }

                if (WireframeOverlay)
                {
                    const float MinBary = std::min({ B0, B1, B2 });
                    if (MinBary < 0.045f)
                    {
                        OutR = OutR * 0.35f + 0.60f;
                        OutG = OutG * 0.35f + 0.62f;
                        OutB = OutB * 0.35f + 0.52f;
                    }
                }

                const std::size_t ByteIdx = PixIdx * 4u;
                if (ViewChannelIndex == 0u)
                {
                    OutRgba8[ByteIdx + 0u] = EncodeAcesSrgbByte(OutR);
                    OutRgba8[ByteIdx + 1u] = EncodeAcesSrgbByte(OutG);
                    OutRgba8[ByteIdx + 2u] = EncodeAcesSrgbByte(OutB);
                }
                else
                {
                    OutRgba8[ByteIdx + 0u] = static_cast<uint8_t>(std::lround(std::clamp(OutR, 0.0f, 1.0f) * 255.0f));
                    OutRgba8[ByteIdx + 1u] = static_cast<uint8_t>(std::lround(std::clamp(OutG, 0.0f, 1.0f) * 255.0f));
                    OutRgba8[ByteIdx + 2u] = static_cast<uint8_t>(std::lround(std::clamp(OutB, 0.0f, 1.0f) * 255.0f));
                }
                OutRgba8[ByteIdx + 3u] = 255u;
            }
        }
    }

    return true;
}

bool TexturePaintSession::RenderUvCanvasRgba8(
    uint32_t              WidthPx,
    uint32_t              HeightPx,
    std::vector<uint8_t>& OutRgba8)
{
    if (WidthPx < 16u || HeightPx < 16u)
    {
        return false;
    }
    EnsureStudyGeometryPrepared();
    OutRgba8.resize(static_cast<std::size_t>(WidthPx) * HeightPx * 4u);

    for (uint32_t Py = 0u; Py < HeightPx; ++Py)
    {
        const float V = static_cast<float>(Py) / static_cast<float>(HeightPx);
        for (uint32_t Px = 0u; Px < WidthPx; ++Px)
        {
            const float U = static_cast<float>(Px) / static_cast<float>(WidthPx);
            const EvaluatedPbrTexel Surf = EvaluateLayerStackAtSurfacePoint(
                Layers,
                U,
                V,
                (U - 0.5f) * 1.4f,
                (V - 0.5f) * 1.4f,
                0.45f,
                0.7f,
                0u);
            const std::size_t Idx = (static_cast<std::size_t>(Py) * WidthPx + Px) * 4u;
            OutRgba8[Idx + 0u] = static_cast<uint8_t>(std::lround(std::clamp(Surf.AlbedoR, 0.0f, 1.0f) * 255.0f));
            OutRgba8[Idx + 1u] = static_cast<uint8_t>(std::lround(std::clamp(Surf.AlbedoG, 0.0f, 1.0f) * 255.0f));
            OutRgba8[Idx + 2u] = static_cast<uint8_t>(std::lround(std::clamp(Surf.AlbedoB, 0.0f, 1.0f) * 255.0f));
            OutRgba8[Idx + 3u] = 255u;
        }
    }

    // Subtle UV island wireframe overlay from PreparedStudy
    const auto& Verts = GeometryCache->PreparedStudy.LowSurfaceVertices;
    const auto DrawUvLine = [&](float U0, float V0, float U1, float V1) noexcept
    {
        const int X0 = static_cast<int>(std::clamp(U0, 0.0f, 0.999f) * static_cast<float>(WidthPx));
        const int Y0 = static_cast<int>(std::clamp(V0, 0.0f, 0.999f) * static_cast<float>(HeightPx));
        const int X1 = static_cast<int>(std::clamp(U1, 0.0f, 0.999f) * static_cast<float>(WidthPx));
        const int Y1 = static_cast<int>(std::clamp(V1, 0.0f, 0.999f) * static_cast<float>(HeightPx));
        const int Steps = std::max(std::abs(X1 - X0), std::abs(Y1 - Y0));
        if (Steps == 0 || Steps > static_cast<int>(WidthPx) / 2)
        {
            return;
        }
        for (int S = 0; S <= Steps; ++S)
        {
            const int X = X0 + (X1 - X0) * S / Steps;
            const int Y = Y0 + (Y1 - Y0) * S / Steps;
            if (X >= 0 && X < static_cast<int>(WidthPx) && Y >= 0 && Y < static_cast<int>(HeightPx))
            {
                const std::size_t Idx = (static_cast<std::size_t>(Y) * WidthPx + static_cast<std::size_t>(X)) * 4u;
                OutRgba8[Idx + 0u] = static_cast<uint8_t>((OutRgba8[Idx + 0u] * 3u + 235u) / 4u);
                OutRgba8[Idx + 1u] = static_cast<uint8_t>((OutRgba8[Idx + 1u] * 3u + 240u) / 4u);
                OutRgba8[Idx + 2u] = static_cast<uint8_t>((OutRgba8[Idx + 2u] * 3u + 210u) / 4u);
            }
        }
    };

    const std::size_t TriCount = Verts.size() / 3u;
    for (std::size_t T = 0u; T < TriCount; T += 2u)
    {
        const auto& A = Verts[T * 3u + 0u];
        const auto& B = Verts[T * 3u + 1u];
        const auto& C = Verts[T * 3u + 2u];
        DrawUvLine(A.UvCoordU, A.UvCoordV, B.UvCoordU, B.UvCoordV);
        DrawUvLine(B.UvCoordU, B.UvCoordV, C.UvCoordU, C.UvCoordV);
    }

    return true;
}

void TexturePaintSession::RenderLayerThumbnailRgba8(
    const PaintLayerEntry& Layer,
    uint32_t               ThumbSizePx,
    std::vector<uint8_t>&  OutRgba8) const noexcept
{
    const uint32_t Dim = std::max(8u, ThumbSizePx);
    OutRgba8.resize(static_cast<std::size_t>(Dim) * Dim * 4u);
    std::vector<PaintLayerEntry> SingleLayerStack { Layer };
    SingleLayerStack[0].Visible = true;
    SingleLayerStack[0].FolderOwnerId = 0u;

    for (uint32_t Py = 0u; Py < Dim; ++Py)
    {
        const float V = (static_cast<float>(Py) + 0.5f) / static_cast<float>(Dim);
        for (uint32_t Px = 0u; Px < Dim; ++Px)
        {
            const float U = (static_cast<float>(Px) + 0.5f) / static_cast<float>(Dim);
            const bool CheckerLight = (((Px / 6u) + (Py / 6u)) & 1u) == 0u;
            const float CheckC = CheckerLight ? 0.16f : 0.10f;

            EvaluatedPbrTexel Base {};
            Base.AlbedoR = CheckC;
            Base.AlbedoG = CheckC;
            Base.AlbedoB = CheckC * 1.08f;

            const EvaluatedPbrTexel Eval = EvaluateLayerStackAtSurfacePoint(
                SingleLayerStack,
                U,
                V,
                (U - 0.5f) * 1.2f,
                (V - 0.5f) * 1.2f,
                0.45f,
                0.75f,
                0u);

            const std::size_t Idx = (static_cast<std::size_t>(Py) * Dim + Px) * 4u;
            OutRgba8[Idx + 0u] = static_cast<uint8_t>(std::lround(std::clamp(Eval.AlbedoR, 0.0f, 1.0f) * 255.0f));
            OutRgba8[Idx + 1u] = static_cast<uint8_t>(std::lround(std::clamp(Eval.AlbedoG, 0.0f, 1.0f) * 255.0f));
            OutRgba8[Idx + 2u] = static_cast<uint8_t>(std::lround(std::clamp(Eval.AlbedoB, 0.0f, 1.0f) * 255.0f));
            OutRgba8[Idx + 3u] = 255u;
        }
    }
}

bool TexturePaintSession::ApplyViewportPointerStroke(
    float NormX,
    float NormY,
    bool  InitialPress,
    bool  TwoDUvSpace) noexcept
{
    auto* Sel = AccessSelectedLayer();
    if (Sel == nullptr || Sel->Locked)
    {
        return false;
    }

    float TargetU = std::clamp(NormX, 0.0f, 1.0f);
    float TargetV = std::clamp(NormY, 0.0f, 1.0f);
    float WorldX  = (TargetU - 0.5f) * 1.2f;
    float WorldY  = (TargetV - 0.5f) * 1.2f;
    float WorldZ  = 0.45f;

    if (!TwoDUvSpace && GeometryCache && GeometryCache->LastViewportW > 0u && GeometryCache->LastViewportH > 0u)
    {
        const uint32_t Px = std::min(
            GeometryCache->LastViewportW - 1u,
            static_cast<uint32_t>(std::clamp(NormX, 0.0f, 1.0f) * static_cast<float>(GeometryCache->LastViewportW)));
        const uint32_t Py = std::min(
            GeometryCache->LastViewportH - 1u,
            static_cast<uint32_t>(std::clamp(NormY, 0.0f, 1.0f) * static_cast<float>(GeometryCache->LastViewportH)));
        const std::size_t PixIdx = static_cast<std::size_t>(Py) * GeometryCache->LastViewportW + Px;
        if (PixIdx < GeometryCache->HitMask.size() && GeometryCache->HitMask[PixIdx] != 0u)
        {
            TargetU = GeometryCache->HitUvU[PixIdx];
            TargetV = GeometryCache->HitUvV[PixIdx];
            WorldX  = GeometryCache->HitWorldX[PixIdx];
            WorldY  = GeometryCache->HitWorldY[PixIdx];
            WorldZ  = GeometryCache->HitWorldZ[PixIdx];
        }
        else
        {
            return false;
        }
    }

    if (InitialPress)
    {
        PushUndoSnapshot();
    }

    if (ActiveTool == PaintToolCategory::Sample)
    {
        const EvaluatedPbrTexel Sampled = EvaluateLayerStackAtSurfacePoint(Layers, TargetU, TargetV, WorldX, WorldY, WorldZ, 0.7f, 0u);
        Brush.InkColor = ColorRgba{
            static_cast<uint8_t>(std::lround(std::clamp(Sampled.AlbedoR, 0.0f, 1.0f) * 255.0f)),
            static_cast<uint8_t>(std::lround(std::clamp(Sampled.AlbedoG, 0.0f, 1.0f) * 255.0f)),
            static_cast<uint8_t>(std::lround(std::clamp(Sampled.AlbedoB, 0.0f, 1.0f) * 255.0f)),
            255u,
        };
        InspectorStatus = "Sampled surface colour";
        return true;
    }

    if (ActiveTool == PaintToolCategory::Fill)
    {
        Sel->PrimerTint = Brush.InkColor;
        Sel->ChannelTints[static_cast<std::size_t>(PaintSurfaceChannel::Albedo)] = Brush.InkColor;
        InspectorStatus = "Filled layer";
        ++ContentRevision;
        return true;
    }

    if (ActiveTool == PaintToolCategory::Gradient3D || Sel->Category == PaintLayerCategory::Gradient)
    {
        if (!Sel->GradientPoints.empty())
        {
            const int32_t PtIdx = std::clamp(Sel->SelectedGradientPointIndex, 0, static_cast<int32_t>(Sel->GradientPoints.size()) - 1);
            auto& Pt = Sel->GradientPoints[static_cast<std::size_t>(PtIdx)];
            Pt.PositionX = WorldX;
            Pt.PositionY = WorldY;
            Pt.PositionZ = WorldZ;
            ++ContentRevision;
            return true;
        }
    }

    if (ActiveTool == PaintToolCategory::Stamp || ActiveTool == PaintToolCategory::Text)
    {
        if (InitialPress)
        {
            AddStampToSelectedLayer(ActiveTool == PaintToolCategory::Text);
        }
        if (!Sel->Stamps.empty())
        {
            Sel->Stamps.back().U = TargetU;
            Sel->Stamps.back().V = TargetV;
            ++ContentRevision;
            return true;
        }
    }

    if (Sel->Category == PaintLayerCategory::Paint)
    {
        if (Sel->StrokeDabs.size() > 240u)
        {
            Sel->StrokeDabs.erase(Sel->StrokeDabs.begin());
        }
        PaintStrokeDabSpec Dab {};
        Dab.U          = TargetU;
        Dab.V          = TargetV;
        Dab.RadiusUv   = std::clamp(static_cast<float>(Brush.PixelRadius) / 380.0f, 0.015f, 0.25f);
        Dab.Hardness   = Brush.Hardness;
        Dab.Opacity    = Brush.Opacity * Brush.DeliveryRate;
        Dab.Tint       = Brush.InkColor;
        Dab.Roughness  = Brush.Roughness;
        Dab.Metalness  = Brush.Metalness;
        Dab.HeightGain = Brush.HeightRelief;
        Dab.EraserMode = (ActiveTool == PaintToolCategory::Eraser);
        Sel->StrokeDabs.push_back(Dab);
        ++ContentRevision;
        return true;
    }

    return false;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  QUERY HELPERS
//------------------------------------------------------------------------------------------------------------------------

PaintLayerEntry* TexturePaintSession::AccessSelectedLayer() noexcept
{
    const int32_t Idx = FindLayerVectorIndex(SelectedLayerId);
    return (Idx >= 0) ? &Layers[static_cast<std::size_t>(Idx)] : nullptr;
}

const PaintLayerEntry* TexturePaintSession::InspectSelectedLayer() const noexcept
{
    const int32_t Idx = FindLayerVectorIndex(SelectedLayerId);
    return (Idx >= 0) ? &Layers[static_cast<std::size_t>(Idx)] : nullptr;
}

int32_t TexturePaintSession::FindLayerVectorIndex(uint32_t LayerId) const noexcept
{
    for (std::size_t I = 0u; I < Layers.size(); ++I)
    {
        if (Layers[I].Id == LayerId)
        {
            return static_cast<int32_t>(I);
        }
    }
    return -1;
}

std::size_t TexturePaintSession::CountVisibleLayers() const noexcept
{
    std::size_t Total = 0u;
    for (const auto& L : Layers)
    {
        if (L.Category != PaintLayerCategory::Folder && L.Visible)
        {
            ++Total;
        }
    }
    return Total;
}

std::size_t TexturePaintSession::CountHiddenLayers() const noexcept
{
    const std::size_t NonFolder = CountNonFolderLayers();
    const std::size_t Vis       = CountVisibleLayers();
    return (NonFolder >= Vis) ? (NonFolder - Vis) : 0u;
}

std::size_t TexturePaintSession::CountNonFolderLayers() const noexcept
{
    std::size_t Total = 0u;
    for (const auto& L : Layers)
    {
        if (L.Category != PaintLayerCategory::Folder)
        {
            ++Total;
        }
    }
    return Total;
}

std::size_t TexturePaintSession::CountAttachedMasks() const noexcept
{
    std::size_t Total = 0u;
    for (const auto& L : Layers)
    {
        if (L.Mask.Attached && L.Mask.Enabled)
        {
            ++Total;
        }
    }
    return Total;
}

std::size_t TexturePaintSession::CountPaintedChannels() const noexcept
{
    std::array<bool, ChannelCount> Written {};
    for (const auto& L : Layers)
    {
        if (!L.Visible || L.Category == PaintLayerCategory::Folder)
        {
            continue;
        }
        for (std::size_t I = 0u; I < ChannelCount; ++I)
        {
            if (L.ActiveChannels[I])
            {
                Written[I] = true;
            }
        }
    }
    std::size_t Total = 0u;
    for (bool On : Written)
    {
        if (On) ++Total;
    }
    return Total;
}

bool TexturePaintSession::QueryMovableUp() const noexcept
{
    const int32_t Idx = FindLayerVectorIndex(SelectedLayerId);
    return Idx >= 0 && static_cast<std::size_t>(Idx) + 1u < Layers.size();
}

bool TexturePaintSession::QueryMovableDown() const noexcept
{
    return FindLayerVectorIndex(SelectedLayerId) > 0;
}

const PaintStudyDescriptor& TexturePaintSession::QueryActiveStudyDescriptor() const noexcept
{
    const std::size_t Index = static_cast<std::size_t>(Study);
    return PaintStudyEntries[Index < PaintStudyEntries.size() ? Index : 0u];
}

const std::array<PaintChannelDescriptor, TexturePaintSession::ChannelCount>& TexturePaintSession::QueryChannelRegistry() noexcept
{
    return PaintChannelEntries;
}

const PaintChannelDescriptor& TexturePaintSession::QueryChannelDescriptor(PaintSurfaceChannel Channel) noexcept
{
    const std::size_t Index = static_cast<std::size_t>(Channel);
    return PaintChannelEntries[Index < ChannelCount ? Index : 0u];
}

const std::array<const char*, 5u>& TexturePaintSession::QueryChannelGroupLabels() noexcept
{
    return PaintGroupLabels;
}

const std::array<PaintStudyDescriptor, 5u>& TexturePaintSession::QueryStudyRegistry() noexcept
{
    return PaintStudyEntries;
}

const std::array<const char*, 4u>& TexturePaintSession::QueryTeapotPartLabels() noexcept
{
    return TeapotPartNames;
}

const std::array<BrushLibraryPresetDescriptor, 12u>& TexturePaintSession::QueryBrushLibraryRegistry() noexcept
{
    return BrushLibraryEntries;
}

const std::array<const char*, 6u>& TexturePaintSession::QueryFontLabels() noexcept
{
    return FontPresetLabels;
}

const std::array<const char*, 12u>& TexturePaintSession::QueryMaterialPresetLabels() noexcept
{
    return MaterialPresetNames;
}

const std::array<const char*, 23u>& TexturePaintSession::QueryViewChannelLabels() noexcept
{
    return PaintViewChannelLabels;
}

const std::array<const char*, 10u>& TexturePaintSession::QueryBlendMethodLabels() noexcept
{
    return PaintBlendLabels;
}

const std::array<ColorRgba, 8u>& TexturePaintSession::QueryBrushQuickSwatches() noexcept
{
    return BrushQuickSwatches;
}

} // namespace Frontier::Workspaces
