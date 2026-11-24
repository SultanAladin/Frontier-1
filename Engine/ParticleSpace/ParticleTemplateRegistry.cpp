/*============================================================================================================================================
                                                      PARTICLETEMPLATEREGISTRY.CPP
============================================================================================================================================*/
// 🧩 Authoritative registry loading all 71 ParticleTemplates/*.toml via EffectLayerStack and instantiating ParticleCloudNode instances
// Reference: Experimental/FluidEditor/Engine/ParticleCatalog.js L1-L276

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/ParticleSpace/ParticleTemplateRegistry.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace Frontier::ParticleSpace
{

namespace
{

constexpr std::array<std::string_view, 16u> kShapeTokens {{
    "Point", "Sphere", "Box", "Disc", "Ring", "Line", "Grid", "Torus",
    "SurfaceShell", "MocapFigure", "Dome", "ObjectSurface", "Spiderweb", "ClothGrid", "FloraBed", "FireworkBurst"
}};

constexpr std::array<std::string_view, 2u> kSpawnModeTokens {{ "Emit", "Cloner" }};
constexpr std::array<std::string_view, 7u> kColourModeTokens {{ "Life", "Speed", "Displacement", "Height", "Seed", "Swatch", "Cycle" }};
constexpr std::array<std::string_view, 2u> kRampSourceTokens {{ "Palette", "Ramp" }};
constexpr std::array<std::string_view, 2u> kBlendTokens {{ "Alpha", "Additive" }};
constexpr std::array<std::string_view, 2u> kStyleTokens {{ "Point", "Streak" }};
constexpr std::array<std::string_view, 18u> kSpriteTokens {{
    "Glow", "Disc", "Bokeh", "Star", "Square", "Cube3D", "Butterfly", "HexTile", "HoloGlyph",
    "SnowCrystal", "RainDrop", "LeafBlade", "BlossomPetal", "CrystalShard2D", "ShockRing",
    "RadarArc", "AuroraCurtain", "CoinDisc"
}};
constexpr std::array<std::string_view, 2u> kCarrierTokens {{ "Sprite", "Object3D" }};
constexpr std::array<std::string_view, 4u> kBackingTokens {{ "Hidden", "ObsidianGlossy", "BrushedChrome", "GhostWireframe" }};
constexpr std::array<std::string_view, 3u> kSampleModeTokens {{ "EvenSurface", "SurfaceVolume", "RidgeAccent" }};
constexpr std::array<std::string_view, 2u> kSizeModeTokens {{ "World", "Pixels" }};
constexpr std::array<std::string_view, 6u> kDerezTokens {{ "Off", "Derez", "ReverseDerez", "MeltDissolve", "ButterflyMorph", "CellGlitch" }};
constexpr std::array<std::string_view, 5u> kMocapTokens {{ "Off", "Sprint", "Dance", "Martial", "Levitate" }};
constexpr std::array<std::string_view, 5u> kEffectorTokens {{ "Off", "Ripple", "Orbiters", "PlaneWave", "NoiseField" }};
constexpr std::array<std::string_view, 3u> kEffectorAxisTokens {{ "Radial", "HeightY", "SurfaceNormal" }};
constexpr std::array<std::string_view, 4u> kDistortionTokens {{ "Off", "GravitationalLens", "ShockwaveRipple", "HeatBubble" }};
constexpr std::array<std::string_view, 10u> kFireworkTokens {{
    "ClassicBouquet", "SkyText", "Heart", "Star", "Smiley", "Saturn", "Crown", "Butterfly", "Spiral", "GrandFinale"
}};
constexpr std::array<std::string_view, 12u> kStrandTokens {{
    "Off", "OrbWeaveWeb", "MessyCobweb", "CablesChains", "HangingVines", "TallReeds",
    "WoodlandFern", "GiantKelp", "SeaAnemone", "TeslaArcs", "PlasmaCage", "StormWeb"
}};
constexpr std::array<std::string_view, 2u> kStrandVariantTokens {{ "Primary", "Secondary" }};
constexpr std::array<std::string_view, 3u> kTrailModeTokens {{ "Off", "Ribbon", "Only" }};
constexpr std::array<std::string_view, 2u> kTrailColourTokens {{ "Particle", "Along" }};
constexpr std::array<std::string_view, 5u> kXpbdTopologyTokens {{ "Off", "ClothGrid", "Spiderweb", "CablesChains", "FloraBed" }};

constexpr std::array<std::string_view, 8u> kParticleCategories {{
    "Transitions", "Mocap & MoGraph", "FX & Shields", "Cinematic", "Trails", "Emitters", "Cloners", "Environment"
}};

template <typename EnumT, size_t N>
[[nodiscard]] std::optional<EnumT> LookupTokenOrdinal(const std::array<std::string_view, N>& table, const std::string_view token) noexcept
{
    for (size_t i = 0u; i < N; ++i)
    {
        if (table[i] == token) return static_cast<EnumT>(i);
    }
    return std::nullopt;
}

[[nodiscard]] float ReadFloatField(const SimulationSpace::EffectLayers::EvaluatedParameterBlock& b, const std::string_view name, const float fallback) noexcept
{
    const auto* v = b.Find(name);
    if (v == nullptr) return fallback;
    if (const auto* d = std::get_if<double>(v)) return static_cast<float>(*d);
    if (const auto* i = std::get_if<int64_t>(v)) return static_cast<float>(*i);
    return fallback;
}

[[nodiscard]] uint32_t ReadUIntField(const SimulationSpace::EffectLayers::EvaluatedParameterBlock& b, const std::string_view name, const uint32_t fallback) noexcept
{
    const auto* v = b.Find(name);
    if (v == nullptr) return fallback;
    if (const auto* i = std::get_if<int64_t>(v)) return static_cast<uint32_t>(std::max<int64_t>(0, *i));
    if (const auto* d = std::get_if<double>(v)) return static_cast<uint32_t>(std::max(0.0, std::round(*d)));
    return fallback;
}

[[nodiscard]] bool ReadBoolField(const SimulationSpace::EffectLayers::EvaluatedParameterBlock& b, const std::string_view name, const bool fallback) noexcept
{
    const auto* v = b.Find(name);
    if (v == nullptr) return fallback;
    if (const auto* flag = std::get_if<bool>(v)) return *flag;
    if (const auto* i = std::get_if<int64_t>(v)) return *i != 0;
    if (const auto* d = std::get_if<double>(v)) return *d != 0.0;
    return fallback;
}

[[nodiscard]] std::string ReadStringField(const SimulationSpace::EffectLayers::EvaluatedParameterBlock& b, const std::string_view name, const std::string_view fallback)
{
    const auto* v = b.Find(name);
    if (v == nullptr) return std::string(fallback);
    if (const auto* s = std::get_if<std::string>(v)) return *s;
    return std::string(fallback);
}

[[nodiscard]] std::array<float, 3> ReadVec3Field(const SimulationSpace::EffectLayers::EvaluatedParameterBlock& b, const std::string_view name, const std::array<float, 3>& fallback) noexcept
{
    const auto* v = b.Find(name);
    if (v == nullptr) return fallback;
    if (const auto* vec = std::get_if<std::array<double, 3>>(v))
    {
        return { static_cast<float>((*vec)[0]), static_cast<float>((*vec)[1]), static_cast<float>((*vec)[2]) };
    }
    return fallback;
}

[[nodiscard]] std::string ReadFileUtf8(const std::filesystem::path& filePath)
{
    std::ifstream in(filePath, std::ios::binary);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                           TOKEN PARSING & QUERY TABLES
//------------------------------------------------------------------------------------------------------------------------

std::optional<ParticleShapeOrdinal>          ParseParticleShapeToken(const std::string_view t) noexcept          { return LookupTokenOrdinal<ParticleShapeOrdinal>(kShapeTokens, t); }
std::optional<ParticleSpawnModeOrdinal>      ParseParticleSpawnModeToken(const std::string_view t) noexcept      { return LookupTokenOrdinal<ParticleSpawnModeOrdinal>(kSpawnModeTokens, t); }
std::optional<ParticleColourModeOrdinal>     ParseParticleColourModeToken(const std::string_view t) noexcept     { return LookupTokenOrdinal<ParticleColourModeOrdinal>(kColourModeTokens, t); }
std::optional<ParticleRampSourceOrdinal>     ParseParticleRampSourceToken(const std::string_view t) noexcept     { return LookupTokenOrdinal<ParticleRampSourceOrdinal>(kRampSourceTokens, t); }
std::optional<ParticleBlendOrdinal>          ParseParticleBlendToken(const std::string_view t) noexcept          { return LookupTokenOrdinal<ParticleBlendOrdinal>(kBlendTokens, t); }
std::optional<ParticleRenderStyleOrdinal>    ParseParticleRenderStyleToken(const std::string_view t) noexcept    { return LookupTokenOrdinal<ParticleRenderStyleOrdinal>(kStyleTokens, t); }
std::optional<ParticleSpriteOrdinal>         ParseParticleSpriteToken(const std::string_view t) noexcept         { return LookupTokenOrdinal<ParticleSpriteOrdinal>(kSpriteTokens, t); }
std::optional<ParticleCarrierOrdinal>        ParseParticleCarrierToken(const std::string_view t) noexcept        { return LookupTokenOrdinal<ParticleCarrierOrdinal>(kCarrierTokens, t); }
std::optional<ParticleBackingSurfaceOrdinal> ParseParticleBackingSurfaceToken(const std::string_view t) noexcept { return LookupTokenOrdinal<ParticleBackingSurfaceOrdinal>(kBackingTokens, t); }
std::optional<SolidSampleModeOrdinal>        ParseSolidSampleModeToken(const std::string_view t) noexcept        { return LookupTokenOrdinal<SolidSampleModeOrdinal>(kSampleModeTokens, t); }
std::optional<ParticleSizeModeOrdinal>       ParseParticleSizeModeToken(const std::string_view t) noexcept       { return LookupTokenOrdinal<ParticleSizeModeOrdinal>(kSizeModeTokens, t); }
std::optional<ParticleDerezOrdinal>          ParseParticleDerezToken(const std::string_view t) noexcept          { return LookupTokenOrdinal<ParticleDerezOrdinal>(kDerezTokens, t); }
std::optional<ParticleMocapClipOrdinal>      ParseParticleMocapClipToken(const std::string_view t) noexcept      { return LookupTokenOrdinal<ParticleMocapClipOrdinal>(kMocapTokens, t); }
std::optional<ParticleEffectorOrdinal>       ParseParticleEffectorToken(const std::string_view t) noexcept       { return LookupTokenOrdinal<ParticleEffectorOrdinal>(kEffectorTokens, t); }
std::optional<ParticleEffectorAxisOrdinal>   ParseParticleEffectorAxisToken(const std::string_view t) noexcept   { return LookupTokenOrdinal<ParticleEffectorAxisOrdinal>(kEffectorAxisTokens, t); }
std::optional<ParticleDistortionOrdinal>     ParseParticleDistortionToken(const std::string_view t) noexcept     { return LookupTokenOrdinal<ParticleDistortionOrdinal>(kDistortionTokens, t); }
std::optional<FireworkPatternOrdinal>        ParseFireworkPatternToken(const std::string_view t) noexcept        { return LookupTokenOrdinal<FireworkPatternOrdinal>(kFireworkTokens, t); }
std::optional<ParticleStrandOrdinal>         ParseParticleStrandToken(const std::string_view t) noexcept         { return LookupTokenOrdinal<ParticleStrandOrdinal>(kStrandTokens, t); }
std::optional<ParticleStrandVariantOrdinal>  ParseParticleStrandVariantToken(const std::string_view t) noexcept  { return LookupTokenOrdinal<ParticleStrandVariantOrdinal>(kStrandVariantTokens, t); }
std::optional<ParticleTrailModeOrdinal>      ParseParticleTrailModeToken(const std::string_view t) noexcept      { return LookupTokenOrdinal<ParticleTrailModeOrdinal>(kTrailModeTokens, t); }
std::optional<ParticleTrailColourOrdinal>    ParseParticleTrailColourToken(const std::string_view t) noexcept    { return LookupTokenOrdinal<ParticleTrailColourOrdinal>(kTrailColourTokens, t); }
std::optional<ParticleXpbdTopologyOrdinal>   ParseParticleXpbdTopologyToken(const std::string_view t) noexcept   { return LookupTokenOrdinal<ParticleXpbdTopologyOrdinal>(kXpbdTopologyTokens, t); }

std::string_view QueryFireworkPatternToken(const FireworkPatternOrdinal pattern) noexcept
{
    const size_t idx = static_cast<size_t>(pattern);
    return (idx < kFireworkTokens.size()) ? kFireworkTokens[idx] : kFireworkTokens[0];
}

std::span<const std::string_view> QueryParticleCategoryNames() noexcept
{
    return kParticleCategories;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                EVALUATION APPLICATION
//------------------------------------------------------------------------------------------------------------------------

void ApplyParticleEvaluationToParameters(
    const SimulationSpace::EffectLayers::EvaluatedParameterBlock& b,
    ParticleCloudParameters& p) noexcept
{
    if (auto v = ParseParticleSpawnModeToken(ReadStringField(b, "Mode", "Emit"))) p.Mode = *v;
    if (auto v = ParseParticleShapeToken(ReadStringField(b, "Shape", "Point"))) p.Shape = *v;
    p.ShapeDetail = ReadFloatField(b, "ShapeDetail", p.ShapeDetail);
    p.SpawnSolid = ReadStringField(b, "SpawnObject", p.SpawnSolid);
    if (auto v = ParseSolidSampleModeToken(ReadStringField(b, "SpawnSampleMode", "EvenSurface"))) p.SpawnSampleMode = *v;
    p.SolidYawDegrees = ReadFloatField(b, "ObjectYaw", p.SolidYawDegrees);
    if (auto v = ParseParticleBackingSurfaceToken(ReadStringField(b, "BackingSurface", "Hidden"))) p.BackingSurface = *v;
    p.CustomSolidId = ReadStringField(b, "CustomObjectId", p.CustomSolidId);
    if (auto v = ParseFireworkPatternToken(ReadStringField(b, "FireworkPattern", "ClassicBouquet"))) p.FireworkPattern = *v;
    p.FireworkText = ReadStringField(b, "FireworkText", p.FireworkText);
    p.Position  = ReadVec3Field(b, "Position", p.Position);
    p.Direction = ReadVec3Field(b, "Direction", p.Direction);
    p.Radius    = ReadFloatField(b, "Radius", p.Radius);
    p.Count     = std::clamp(ReadUIntField(b, "Count", p.Count), 16u, kMaximumReferenceParticles);
    p.Rate      = ReadFloatField(b, "Rate", p.Rate);
    p.Lifetime  = ReadFloatField(b, "Lifetime", p.Lifetime);
    p.Fill      = ReadFloatField(b, "Fill", p.Fill);
    p.Linger    = ReadFloatField(b, "Linger", p.Linger);

    p.Dissolve       = ReadFloatField(b, "Dissolve", p.Dissolve);
    p.NoiseScale     = ReadFloatField(b, "NoiseScale", p.NoiseScale);
    p.NoiseSpeed     = ReadFloatField(b, "NoiseSpeed", p.NoiseSpeed);
    p.Speed          = ReadFloatField(b, "Speed", p.Speed);
    p.Spread         = ReadFloatField(b, "Spread", p.Spread);
    p.Radial         = ReadFloatField(b, "Radial", p.Radial);
    p.Gravity        = ReadFloatField(b, "Gravity", p.Gravity);
    p.Drag           = ReadFloatField(b, "Drag", p.Drag);
    p.Noise          = ReadFloatField(b, "Noise", p.Noise);
    p.FieldInfluence = ReadFloatField(b, "FieldInfluence", p.FieldInfluence);
    p.GasInfluence   = ReadFloatField(b, "GasInfluence", p.GasInfluence);
    p.Spring         = ReadFloatField(b, "Spring", p.Spring);
    p.Damping        = ReadFloatField(b, "Damping", p.Damping);
    p.Bounce         = ReadFloatField(b, "Bounce", p.Bounce);

    p.SizeStart  = ReadFloatField(b, "SizeStart", p.SizeStart);
    p.SizeEnd    = ReadFloatField(b, "SizeEnd", p.SizeEnd);
    p.SizeRandom = ReadFloatField(b, "SizeRandom", p.SizeRandom);
    if (auto v = ParseParticleSizeModeToken(ReadStringField(b, "SizeMode", "Pixels"))) p.SizeMode = *v;
    if (auto v = ParseParticleSpriteToken(ReadStringField(b, "Sprite", "Disc"))) p.Sprite = *v;
    if (auto v = ParseParticleCarrierToken(ReadStringField(b, "CarrierMode", "Sprite"))) p.CarrierMode = *v;
    p.CarrierSolid   = ReadStringField(b, "CarrierObject", p.CarrierSolid);
    p.CarrierCustomId = ReadStringField(b, "CarrierCustomObjectId", p.CarrierCustomId);
    p.SolidSpin      = ReadFloatField(b, "ObjectSpin", p.SolidSpin);
    p.SolidTilt      = ReadFloatField(b, "ObjectTilt", p.SolidTilt);
    p.SolidRoughness = ReadFloatField(b, "ObjectRoughness", p.SolidRoughness);
    p.SolidMetallic  = ReadFloatField(b, "ObjectMetallic", p.SolidMetallic);
    p.SolidGlow      = ReadFloatField(b, "ObjectGlow", p.SolidGlow);
    p.Glow           = ReadFloatField(b, "Glow", p.Glow);
    p.FocusDistance  = ReadFloatField(b, "FocusDistance", p.FocusDistance);
    p.Aperture       = ReadFloatField(b, "Aperture", p.Aperture);

    if (auto v = ParseParticleColourModeToken(ReadStringField(b, "ColourMode", "Life"))) p.ColourMode = *v;
    if (const auto* stopsVal = b.Find("Palette"))
    {
        if (const auto* arr = std::get_if<std::vector<std::string>>(stopsVal); arr && !arr->empty())
        {
            p.ColourStops.assign(arr->begin(), arr->begin() + std::min<size_t>(arr->size(), kMaximumColourStops));
        }
    }
    if (auto v = ParseParticleRampSourceToken(ReadStringField(b, "RampSource", "Palette"))) p.RampSource = *v;
    p.HueJitter         = ReadFloatField(b, "HueJitter", p.HueJitter);
    p.BrightnessJitter  = ReadFloatField(b, "BrightnessJitter", p.BrightnessJitter);
    p.CycleSpeed        = ReadFloatField(b, "CycleSpeed", p.CycleSpeed);
    p.Brightness        = ReadFloatField(b, "Brightness", p.Brightness);
    p.RampName          = ReadStringField(b, "Ramp", p.RampName);
    p.DisplacementRange = ReadFloatField(b, "DisplacementRange", p.DisplacementRange);
    p.MaxSpeed          = ReadFloatField(b, "MaxSpeed", p.MaxSpeed);
    p.Flicker           = ReadFloatField(b, "Flicker", p.Flicker);
    if (auto v = ParseParticleBlendToken(ReadStringField(b, "Blend", "Alpha"))) p.Blend = *v;
    if (auto v = ParseParticleRenderStyleToken(ReadStringField(b, "Style", "Point"))) p.Style = *v;

    if (auto v = ParseParticleTrailModeToken(ReadStringField(b, "Trail", "Off"))) p.Trail = *v;
    p.TrailLength       = std::clamp(ReadUIntField(b, "TrailLength", p.TrailLength), 2u, kMaximumTrailLength);
    p.TrailInterval     = ReadFloatField(b, "TrailInterval", p.TrailInterval);
    p.TrailWidth        = ReadFloatField(b, "TrailWidth", p.TrailWidth);
    p.TrailTaper        = ReadFloatField(b, "TrailTaper", p.TrailTaper);
    p.TrailFade         = ReadFloatField(b, "TrailFade", p.TrailFade);
    p.TrailSubdivisions = std::clamp(ReadUIntField(b, "TrailSubdivisions", p.TrailSubdivisions), 1u, 4u);
    if (auto v = ParseParticleTrailColourToken(ReadStringField(b, "TrailColour", "Particle"))) p.TrailColour = *v;
    p.TrailRatio        = ReadFloatField(b, "TrailRatio", p.TrailRatio);

    if (auto v = ParseParticleDerezToken(ReadStringField(b, "DerezMode", "Off"))) p.DerezMode = *v;
    p.DerezMix        = ReadFloatField(b, "DerezMix", p.DerezMix);
    p.DerezProgress   = ReadFloatField(b, "DerezProgress", p.DerezProgress);
    p.DerezAutoSpeed  = ReadFloatField(b, "DerezAutoSpeed", p.DerezAutoSpeed);
    p.DerezEdgeWidth  = ReadFloatField(b, "DerezEdgeWidth", p.DerezEdgeWidth);
    p.DerezEdgeGlow   = ReadFloatField(b, "DerezEdgeGlow", p.DerezEdgeGlow);
    p.DerezEdgeColour = ReadStringField(b, "DerezEdgeColour", p.DerezEdgeColour);
    p.DerezShatter    = ReadFloatField(b, "DerezShatter", p.DerezShatter);

    if (auto v = ParseParticleMocapClipToken(ReadStringField(b, "MocapClip", "Off"))) p.MocapClip = *v;
    p.MocapAmplitude = ReadFloatField(b, "MocapAmplitude", p.MocapAmplitude);
    p.MocapSpeed     = ReadFloatField(b, "MocapSpeed", p.MocapSpeed);
    p.MocapScale     = ReadFloatField(b, "MocapScale", p.MocapScale);
    p.MocapTrailKick = ReadFloatField(b, "MocapTrailKick", p.MocapTrailKick);

    if (auto v = ParseParticleEffectorToken(ReadStringField(b, "EffectorMode", "Off"))) p.EffectorMode = *v;
    p.EffectorStrength    = ReadFloatField(b, "EffectorStrength", p.EffectorStrength);
    p.EffectorFrequency   = ReadFloatField(b, "EffectorFrequency", p.EffectorFrequency);
    p.EffectorSpeed       = ReadFloatField(b, "EffectorSpeed", p.EffectorSpeed);
    p.EffectorPush        = ReadFloatField(b, "EffectorPush", p.EffectorPush);
    p.EffectorScaleBoost  = ReadFloatField(b, "EffectorScaleBoost", p.EffectorScaleBoost);
    p.EffectorColourShift = ReadFloatField(b, "EffectorColourShift", p.EffectorColourShift);
    if (auto v = ParseParticleEffectorAxisToken(ReadStringField(b, "EffectorAxis", "Radial"))) p.EffectorAxis = *v;

    p.SwarmStrength = ReadFloatField(b, "SwarmStrength", p.SwarmStrength);
    p.SwarmCohesion = ReadFloatField(b, "SwarmCohesion", p.SwarmCohesion);
    p.SwarmOrbit    = ReadFloatField(b, "SwarmOrbit", p.SwarmOrbit);
    p.SwarmWingRate = ReadFloatField(b, "SwarmWingRate", p.SwarmWingRate);

    p.GlitchAmount     = ReadFloatField(b, "GlitchAmount", p.GlitchAmount);
    p.GlitchRate       = ReadFloatField(b, "GlitchRate", p.GlitchRate);
    p.GlitchStepJitter = ReadFloatField(b, "GlitchStepJitter", p.GlitchStepJitter);
    p.GlitchScanlines  = ReadFloatField(b, "GlitchScanlines", p.GlitchScanlines);
    p.GlitchChromatic  = ReadFloatField(b, "GlitchChromatic", p.GlitchChromatic);

    if (auto v = ParseParticleDistortionToken(ReadStringField(b, "DistortionMode", "Off"))) p.DistortionMode = *v;
    p.DistortionStrength  = ReadFloatField(b, "DistortionStrength", p.DistortionStrength);
    p.DistortionRadius    = ReadFloatField(b, "DistortionRadius", p.DistortionRadius);
    p.DistortionHorizon   = ReadFloatField(b, "DistortionHorizon", p.DistortionHorizon);
    p.DistortionChromatic = ReadFloatField(b, "DistortionChromatic", p.DistortionChromatic);

    if (auto v = ParseParticleStrandToken(ReadStringField(b, "StrandMode", "Off"))) p.StrandMode = *v;
    if (auto v = ParseParticleStrandVariantToken(ReadStringField(b, "StrandVariant", "Primary"))) p.StrandVariant = *v;
    p.StrandCount     = ReadUIntField(b, "StrandCount", p.StrandCount);
    p.StrandDetail    = ReadUIntField(b, "StrandDetail", p.StrandDetail);
    p.StrandSag       = ReadFloatField(b, "StrandSag", p.StrandSag);
    p.StrandThickness = ReadFloatField(b, "StrandThickness", p.StrandThickness);
    p.StrandSway      = ReadFloatField(b, "StrandSway", p.StrandSway);
    p.StrandSpan      = ReadFloatField(b, "StrandSpan", p.StrandSpan);
    p.StrandGlow      = ReadFloatField(b, "StrandGlow", p.StrandGlow);

    // Additive Layer 220 (XPBD Constraints) — neutral/off when layer absent
    if (auto v = ParseParticleXpbdTopologyToken(ReadStringField(b, "XpbdTopology", "Off"))) p.XpbdTopology = *v;
    p.XpbdSubsteps      = ReadUIntField(b, "XpbdSubsteps", p.XpbdSubsteps);
    p.XpbdIterations    = ReadUIntField(b, "XpbdIterations", p.XpbdIterations);
    p.XpbdWarpModulus   = ReadFloatField(b, "XpbdWarpModulus", p.XpbdWarpModulus);
    p.XpbdWeftModulus   = ReadFloatField(b, "XpbdWeftModulus", p.XpbdWeftModulus);
    p.XpbdShearModulus  = ReadFloatField(b, "XpbdShearModulus", p.XpbdShearModulus);
    p.XpbdBendStiffness = ReadFloatField(b, "XpbdBendStiffness", p.XpbdBendStiffness);
    p.XpbdFriction      = ReadFloatField(b, "XpbdFriction", p.XpbdFriction);
    p.XpbdBreakRatio    = ReadFloatField(b, "XpbdBreakRatio", p.XpbdBreakRatio);
    p.XpbdAeroDrag      = ReadFloatField(b, "XpbdAeroDrag", p.XpbdAeroDrag);

    // Additive Layer 221 (GPU Collisions) — neutral/off when layer absent
    p.CollisionRigidCoupling  = ReadBoolField(b, "CollisionRigidCoupling", p.CollisionRigidCoupling);
    p.CollisionSelfHash       = ReadBoolField(b, "CollisionSelfHash", p.CollisionSelfHash);
    p.CollisionTwoWayImpulse  = ReadBoolField(b, "CollisionTwoWayImpulse", p.CollisionTwoWayImpulse);
    p.CollisionParticleRadius = ReadFloatField(b, "CollisionParticleRadius", p.CollisionParticleRadius);
    p.CollisionRestitution    = ReadFloatField(b, "CollisionRestitution", p.CollisionRestitution);
    p.CollisionFriction       = ReadFloatField(b, "CollisionFriction", p.CollisionFriction);
    p.CollisionCohesion       = ReadFloatField(b, "CollisionCohesion", p.CollisionCohesion);
    p.CollisionPositionRelax  = ReadFloatField(b, "CollisionPositionRelax", p.CollisionPositionRelax);
    p.CollisionSubsteps       = ReadUIntField(b, "CollisionSubsteps", p.CollisionSubsteps);
    p.CollisionIterations     = ReadUIntField(b, "CollisionIterations", p.CollisionIterations);
}

SimulationSpace::EffectLayers::StackEvaluationOutput EvaluateParticleCloudNode(
    const SimulationSpace::EffectLayers::EffectLayerSpecificationSet& specs,
    ParticleCloudNode& cloud,
    const std::optional<double> evaluationTimeSeconds)
{
    auto eval = SimulationSpace::EffectLayers::EvaluateEffectLayerStack(specs, cloud.Stack, {}, evaluationTimeSeconds);
    cloud.Halted = eval.IsStopped();
    if (!cloud.Halted)
    {
        ApplyParticleEvaluationToParameters(eval.ParticleFields, cloud.Evaluated);
    }
    return eval;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                   PARTICLETEMPLATEREGISTRY
//------------------------------------------------------------------------------------------------------------------------

bool ParticleTemplateRegistry::LoadFromDirectory(
    const SimulationSpace::EffectLayers::EffectLayerSpecificationSet& specs,
    const std::string_view templatesDirectory,
    std::string* const outError)
{
    namespace fs = std::filesystem;
    Templates.clear();

    const fs::path dirPath(templatesDirectory);
    if (!fs::exists(dirPath) || !fs::is_directory(dirPath))
    {
        if (outError) *outError = "Particle templates directory does not exist: " + std::string(templatesDirectory);
        return false;
    }

    for (const auto& dirEntry : fs::directory_iterator(dirPath))
    {
        if (!dirEntry.is_regular_file()) continue;
        const std::string ext = dirEntry.path().extension().string();
        if (ext != ".ParticleSpace" && ext != ".particlespace") continue;

        std::string parseErr;
        const auto docOpt = SimulationSpace::EffectLayers::ParseTomlSubset(ReadFileUtf8(dirEntry.path()), &parseErr);
        if (!docOpt.has_value())
        {
            if (outError) *outError = dirEntry.path().filename().string() + ": " + parseErr;
            return false;
        }
        const auto& doc = *docOpt;
        const auto* tmplVal = doc.Find("Template");
        if (tmplVal == nullptr || !tmplVal->IsTable()) continue;
        const auto& tmplTbl = *tmplVal->AsTable();

        ParticleTemplateEntry entry {};
        entry.Order    = static_cast<uint32_t>(tmplTbl.Find("Order") ? tmplTbl.Find("Order")->AsInt64(999) : 999);
        entry.Id       = tmplTbl.Find("Id") ? std::string(tmplTbl.Find("Id")->AsString()) : dirEntry.path().stem().string();
        entry.Name     = tmplTbl.Find("Name") ? std::string(tmplTbl.Find("Name")->AsString()) : entry.Id;
        entry.Category = tmplTbl.Find("Category") ? std::string(tmplTbl.Find("Category")->AsString()) : "Emitters";
        entry.Icon     = tmplTbl.Find("Icon") ? std::string(tmplTbl.Find("Icon")->AsString()) : "";
        entry.TintHex  = tmplTbl.Find("Tint") ? std::string(tmplTbl.Find("Tint")->AsString()) : "#38bdf8";
        entry.Summary  = tmplTbl.Find("Summary") ? std::string(tmplTbl.Find("Summary")->AsString()) : "";

        entry.Stack = SimulationSpace::EffectLayers::ParseEffectLayerStackFromToml(
            specs,
            doc,
            SimulationSpace::EffectLayers::LayerStackOrdinal::ParticleCloud);
        const auto eval = SimulationSpace::EffectLayers::EvaluateEffectLayerStack(specs, entry.Stack, {}, std::nullopt);
        entry.Problems      = eval.Problems;
        entry.StoppedReason = eval.StoppedReason;
        ApplyParticleEvaluationToParameters(eval.ParticleFields, entry.Defaults);

        if (const auto* timingVal = doc.Find("Timing"); timingVal != nullptr && timingVal->IsTable())
        {
            const auto& t = *timingVal->AsTable();
            if (const auto* v = t.Find("StartTime")) entry.Defaults.StartTime = static_cast<float>(v->AsDouble(0.0));
            if (const auto* v = t.Find("Duration"))  entry.Defaults.Duration  = static_cast<float>(v->AsDouble(0.0));
            if (const auto* v = t.Find("Fade"))      entry.Defaults.Fade      = static_cast<float>(v->AsDouble(0.15));
            if (const auto* v = t.Find("Repeat"))    entry.Defaults.Repeat    = static_cast<float>(v->AsDouble(0.0));
        }

        Templates.push_back(std::move(entry));
    }

    std::sort(Templates.begin(), Templates.end(), [](const ParticleTemplateEntry& a, const ParticleTemplateEntry& b) noexcept
    {
        if (a.Order != b.Order) return a.Order < b.Order;
        return a.Id < b.Id;
    });
    return !Templates.empty();
}

const ParticleTemplateEntry* ParticleTemplateRegistry::FindById(const std::string_view templateId) const noexcept
{
    for (const ParticleTemplateEntry& entry : Templates)
    {
        if (entry.Id == templateId) return &entry;
    }
    return nullptr;
}

std::optional<ParticleCloudNode> ParticleTemplateRegistry::ConstructParticleCloud(
    const SimulationSpace::EffectLayers::EffectLayerSpecificationSet& specs,
    const std::string_view templateId,
    const std::string_view nodeId) const
{
    const ParticleTemplateEntry* tmpl = FindById(templateId);
    if (tmpl == nullptr && !Templates.empty())
    {
        tmpl = &Templates.front();
    }
    if (tmpl == nullptr) return std::nullopt;

    ParticleCloudNode node {};
    node.NodeId     = nodeId.empty() ? ("particles-" + tmpl->Id) : std::string(nodeId);
    node.TemplateId = tmpl->Id;
    node.Name       = tmpl->Name;
    node.Stack      = tmpl->Stack;
    node.Evaluated  = tmpl->Defaults;
    (void)EvaluateParticleCloudNode(specs, node, std::nullopt);
    return node;
}

} // namespace Frontier::ParticleSpace
