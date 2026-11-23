/*============================================================================================================================================
                                                          MATERIALPROCEDURALBAKING.H
============================================================================================================================================*/
// 🧩 CPU port of the Alloy procedural kernels (alloy-procedural-v7): seeded value noise and the tapered scratch
// field bake into shared height/coverage maps, so every channel samples one consistent procedural definition.

#pragma once
#include <array>
#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace Frontier
{
// 📝 Bit-faithful intent, float-precision reality: these follow the GLSL line for line; GPU/CPU rounding may
// differ in the last ulp, so parity checks use tolerance, never exact equality.
[[nodiscard]] float ProceduralHash31(float X, float Y, float Z);
[[nodiscard]] float ProceduralNoise3(float X, float Y, float Z);
[[nodiscard]] float ProceduralStoneFbm(float X, float Y, float Z);

struct ScratchFieldParameters
{
    float Density = 0, Length = 0, Width = 0, Depth = 0, Spread = 0, Bend = 0, Scale = 1, AngleRadians = 0;
    uint32_t Seed = 0;
};

struct ScratchFieldSample
{
    float Coverage = 0, Height = 0, Rim = 0;
};

// 📝 The fragment shader reads screen-space derivatives for anti-aliasing; the baker substitutes the known
// texel step in scaled UV (Scale / Extent), which is what fwidth converges to on a flat preview card.
[[nodiscard]] ScratchFieldSample EvaluateScratchField(float U, float V, const ScratchFieldParameters &Parameters, float PixelUv);

struct ScratchBakeMaps
{
    std::vector<float> Height, Coverage;
    uint32_t Extent = 0;
};

// 📝 Row-major maps, V running down the rows like image data; Height holds signed groove depth in world units,
// Coverage the 0..1 cut mask shared by the roughness and occlusion channels.
[[nodiscard]] ScratchBakeMaps BakeScratchMaps(const ScratchFieldParameters &Parameters, uint32_t Extent);

// 📝 Tangent-space normal from relief (groove floor near -1): normalize(-dh/du, -dh/dv, 1) with clamped
// borders, matching the shipped detail-texture convention; the scratch bake is not periodic.
[[nodiscard]] std::vector<uint8_t> ConvertHeightToNormalRgba(const std::vector<float> &Relief, uint32_t Extent, float Strength);
// 📝 ORM multipliers over the authored scalars: occlusion darkens the cuts, green divides by the shader's +0.3
// ceiling so the authored scalar must equal min(1, BaseRoughness + .3), blue preserves full metalness.
[[nodiscard]] std::vector<uint8_t> ConvertScratchOrmRgba(const std::vector<float> &Coverage, uint32_t Extent, float BaseRoughness);

// 📝 Woven/knit/sand ports (Alloy types 4/7/19/37/38): the flat-patch baker evaluates one triplanar face, so
// the viewport's pp/weights selection collapses to plain 2D UV while every kernel formula stays verbatim.
// Colors ride as linear RGB triples (THREE.Color parity); the albedo converter sRGB-encodes them for upload.
struct WeaveFieldParameters
{
    // 📝 Type 4 fabric / 7 carbon; Weave selects plain 0, twill 1, satin 2, basket 3, rib 4,
    // herringbone 5, oxford 6, houndstooth 7, denim 8. FabricMode 1/2 bake nap modulation.
    int Type = 4, Weave = 1, FabricMode = 0;
    float Scale = 0, AngleRadians = 0, Relief = 0, FiberDetail = 0;
    float Fuzz = 0, Slub = 0, DenimFade = 0, Grain = 0, Seed = 0;
    float Warp[3] = {1, 1, 1}, Weft[3] = {1, 1, 1}, Secondary[3] = {0, 0, 0};
};

struct WeaveFieldSample
{
    float Albedo[3] = {1, 1, 1};
    float Relief = 0;
};

// 📝 PixelStep carries the scaled-UV texel rate (Scale / Extent) the shader's fwidth calls converge to.
[[nodiscard]] WeaveFieldSample EvaluateWeaveField(float U, float V, const WeaveFieldParameters &Parameters, float PixelStep);

struct WeaveBakeMaps
{
    std::vector<float> Albedo, Relief;
    uint32_t Extent = 0;
};

// 📝 Row-major maps; Albedo holds three linear floats per texel, Relief signed scene-unit height.
[[nodiscard]] WeaveBakeMaps BakeWeaveMaps(const WeaveFieldParameters &Parameters, uint32_t Extent);
// 📝 Analytic relief ceiling (arch + nap + slub terms) the offline bake divides by before normal encoding,
// mirroring the scratch Depth normalization so every weave answers to Strength 2 the same way.
[[nodiscard]] float WeaveReliefAmplitude(const WeaveFieldParameters &Parameters);

struct KnitFieldParameters
{
    float Scale = 0, Grain = 0, Fuzz = 0, Seed = 0, AngleRadians = 0;
    float Color[3] = {1, 1, 1}, Secondary[3] = {0, 0, 0};
};

struct KnitFieldSample
{
    float Albedo[3] = {1, 1, 1};
    float Relief = 0;
};

[[nodiscard]] KnitFieldSample EvaluateKnitField(float U, float V, const KnitFieldParameters &Parameters, float PixelStep);

struct KnitBakeMaps
{
    std::vector<float> Albedo, Relief;
    uint32_t Extent = 0;
};

[[nodiscard]] KnitBakeMaps BakeKnitMaps(const KnitFieldParameters &Parameters, uint32_t Extent);
[[nodiscard]] float KnitReliefAmplitude(const KnitFieldParameters &Parameters);

struct SandBedParameters
{
    float Color[3] = {1, 1, 1}, Pale[3] = {1, 1, 1};
    float Size = 0, SizeVariation = 0, Coverage = 0, Thickness = 0;
    float Roughness = 0, RoughnessVariation = 0, Specular = 0, SpecularVariation = 0;
    float Ior = 0, Roundness = 0, Tilt = 0, Relief = 0;
    double BedSeed = 0;
};

struct SandFieldParameters
{
    SandBedParameters Beds[4];
    int Count = 0, BandsMode = 0;
    float Scale = 1, Seed = 0, BandFrequency = 0, BandWarp = 0, BandAngleRadians = 0;
    float BaseColor[3] = {1, 1, 1};
};

struct SandFieldSample
{
    float Tint[3] = {1, 1, 1};
    float Roughness = 1, Relief = 0;
    float Facet[2] = {0, 0};
};

[[nodiscard]] SandFieldSample EvaluateSandField(float U, float V, const SandFieldParameters &Parameters, uint32_t Extent);

struct SandBakeMaps
{
    std::vector<float> Tint, Roughness, Relief, Facet;
    uint32_t Extent = 0;
};

// 📝 Facet holds two per-texel tilt components the normal converter adds to the relief gradient,
// reproducing the shader's post-relief facet perturbation that reads as grain sparkle.
[[nodiscard]] SandBakeMaps BakeSandMaps(const SandFieldParameters &Parameters, uint32_t Extent);
[[nodiscard]] float SandReliefAmplitude(const SandFieldParameters &Parameters);
// 📝 FNV-1a bed-id hash (sandLayerSeed parity) times .013; drives per-bed domain offsets.
[[nodiscard]] double SandBedSeed(std::string_view BedId);

// 📝 Linear-RGB albedo triples sRGB-encoded for the albedo upload slot, matching committed PNG convention.
[[nodiscard]] std::vector<uint8_t> ConvertLinearAlbedoRgba(const std::vector<float> &Linear, uint32_t Extent);
// 📝 #rrggbb recipe color to linear RGB; false when malformed so callers can fall back.
[[nodiscard]] bool DecodeHexLinearRgb(std::string_view Hex, float Rgb[3]);
// 📝 Absolute sand roughness: Alloy overwrites the roughness factor, so green carries it verbatim and the
// authored scalar must be 1. Red/blue stay neutral (no occlusion term, null metalness).
[[nodiscard]] std::vector<uint8_t> ConvertSandOrmRgba(const std::vector<float> &Roughness, uint32_t Extent);
// 📝 Tangent-space normal from relief plus a per-texel facet tilt added to the gradient terms.
[[nodiscard]] std::vector<uint8_t> ConvertFacetNormalRgba(const std::vector<float> &Relief, const std::vector<float> &Facet,
    uint32_t Extent, float Strength);

// 📝 Coded fields (HTML FieldSpecification parity): deterministic pure samples over centred, rotated
// coordinates, normalized to [0, 1]. Evaluation stays in double precision because the reference hashes
// with Math.sin; float would drift the cell hash and move camouflage band edges.
struct CodedFieldParameters
{
    double Scale = 8, Seed = 12, Softness = .012, PixelSize = 4;
    uint32_t ColourCount = 4;
};

[[nodiscard]] double EvaluateCodedField(std::string_view Identity, const CodedFieldParameters &Parameters,
    double Horizontal, double Vertical);
// 📝 Header-inline registry: the authoring gate and the op registry share one identity list without
// dragging the baking translation unit (and its STB write implementation) into every consumer.
inline constexpr std::array<const char *, 8> CodedFieldIdentities{"camouflage-smooth", "camouflage", "noise",
    "stripes", "checker", "cells", "ridged", "rings"};
[[nodiscard]] inline bool QueryCodedField(std::string_view Identity) noexcept
{
    for (const char *Candidate : CodedFieldIdentities)
        if (Identity == Candidate) return true;
    return false;
}
// 📝 Field-projection position mapping: rotate centred UV by AngleDegrees, sample, then re-centre the
// [0, 1] result around .5 scaled by Amount (HTML SamplePbrField field branch, verbatim).
[[nodiscard]] double EvaluateCodedFieldPosition(std::string_view Identity, const CodedFieldParameters &Parameters,
    double U, double V, double AngleDegrees, double Amount);

// 📝 Op registry: each procedural op declares the channels it can bake and the slider ranges its Alloy-keyed
// params expose. Params keep their Alloy uniform spellings so recipe controls resolve without translation.
struct ProceduralOpParam
{
    const char *Key = "";
    const char *Label = "";
    float Minimum = 0, Maximum = 1, Default = 0;
    bool Logarithmic = false;
    const char *Unit = "";
    float Step = .01f;
};
[[nodiscard]] std::vector<std::string> QueryProceduralOps();
[[nodiscard]] std::vector<std::string> QueryProceduralOpChannels(std::string_view Op);
[[nodiscard]] std::vector<ProceduralOpParam> QueryProceduralOpParams(std::string_view Op);
// 📝 Text params ride the channel Texts table (palette colours); scalars alone cannot carry #rrggbb.
struct ProceduralOpTextParam
{
    const char *Key = "";
    const char *Label = "";
    const char *Default = "";
};
[[nodiscard]] std::vector<ProceduralOpTextParam> QueryProceduralOpTextParams(std::string_view Op);
// 📝 Canonical pool spelling for a recipe target key; direction aliases collapse onto one stored angle.
[[nodiscard]] std::string ResolveProceduralParamKey(std::string_view Op, std::string_view Key);
// 📝 Maximum roughness lift an op can bake above its base; the material's roughness scalar carries
// base+lift as the ORM ceiling so green never clips. Ops without an ORM channel report zero.
[[nodiscard]] float QueryProceduralOpCeilingLift(std::string_view Op);
// 📝 Channel dispatcher: bake one channel's RGBA from a channel's op + params; the ORM base comes from the
// pool's roughness, and relief normalization plus strength match the offline pilot bake.
[[nodiscard]] std::vector<uint8_t> BakeProceduralChannelRgba(std::string_view Op, std::string_view Channel,
    const std::map<std::string, double> &Params, uint32_t Extent);
// 📝 Text-carrying dispatcher: coded-field ops read their palette from Texts; scalar-only ops ignore it.
[[nodiscard]] std::vector<uint8_t> BakeProceduralChannelRgba(std::string_view Op, std::string_view Channel,
    const std::map<std::string, double> &Params, const std::map<std::string, std::string> &Texts, uint32_t Extent);
// 📝 Deterministic PNG encoding shared by the offline baker, the save path and the regeneration tests.
[[nodiscard]] std::vector<uint8_t> EncodeProceduralPng(const std::vector<uint8_t> &Rgba, uint32_t Extent);
}
