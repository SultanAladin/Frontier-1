/*============================================================================================================================================
                                                        EFFECTLAYERSPECIFICATION.H
============================================================================================================================================*/
// 🧩 Data-driven effect layer specifications parsed from LayerSpecifications.toml, TOML subset reader, validation, and FNV-1a specification hash
// Reference: Experimental/FluidEditor/Engine/LayerSpecifications.toml L1-L2274, LayerSpecificationValidation.js L1-L109, TomlSubset.js L1-L202

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace Frontier::SimulationSpace::EffectLayers
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

inline constexpr uint32_t kFluidRegionLayerCode  = 101u; // [-] - Required GasVolume layer (TOML token: "domain")
inline constexpr uint32_t kReleaseSourceLayerCode = 102u; // [-] - Linked GasVolume source layer (TOML token: "source")
inline constexpr uint32_t kParticleSpawnLayerCode = 201u; // [-] - Required ParticleCloud layer (TOML token: "spawn")
inline constexpr uint32_t kParticleXpbdLayerCode       = 220u; // [-] - Opt-in Frontier GPU Orthotropic XPBD layer
inline constexpr uint32_t kParticleGpuCollisionLayerCode = 221u; // [-] - Opt-in Frontier GPU Collisions layer
inline constexpr uint32_t kDefaultMaximumLayers   = 32u;  // [-] - Stack capacity fallback

//------------------------------------------------------------------------------------------------------------------------
//                                                      ENUMERATIONS
//------------------------------------------------------------------------------------------------------------------------

enum class ParameterTypeOrdinal : uint8_t
{
    Float32      = 0u, // [-] - Floating-point scalar with [Minimum, Maximum]
    Int32        = 1u, // [-] - Signed integer scalar with [Minimum, Maximum]
    UInt32       = 2u, // [-] - Unsigned integer scalar with [Minimum, Maximum]
    Bool         = 3u, // [-] - Boolean switch (stepped)
    Vector3      = 4u, // [-] - 3-component floating-point vector
    ColourLinear = 5u, // [-] - "#rrggbb" hex colour blended in linear sRGB light
    Choice       = 6u, // [-] - Explicit integer-coded enumeration resolved to Token string
    Ramp8        = 7u, // [-] - Up to 8 hex colour stops
    Token        = 8u, // [-] - Arbitrary string token (stepped)
};

enum class LayerPhaseOrdinal : uint8_t
{
    Inject  = 0u, // [-] - Region / Source / Spawn phase
    Advance = 1u, // [-] - Dynamics / Forces / Thermodynamics / Derez / Swarm phase
    Shade   = 2u, // [-] - Smoke / Fire / Colour / Sprite / Trail / Strand phase
    Overlay = 3u, // [-] - Heat shimmer / Glitch / Distortion screen-space phase
};

enum class LayerStackOrdinal : uint8_t
{
    GasVolume     = 1u, // [-] - GasVolumeCategory = 1 (LayerStack.js L28)
    ParticleCloud = 2u, // [-] - ParticleCloudCategory = 2 (LayerStack.js L29)
};

enum class ParameterTargetOrdinal : uint8_t
{
    Region    = 0u, // [-] - Writes GasVolume Region block (TOML Target: "Domain")
    Volume    = 1u, // [-] - Writes GasVolume Volume shading block (TOML Target: "Volume")
    Link      = 2u, // [-] - Links to a scene release source (TOML Target: "Link")
    Particles = 3u, // [-] - Writes ParticleCloud block (TOML Target: "Particles")
};

enum class LayerBlendOrdinal : uint8_t
{
    Replace  = 0u, // [-] - Weighted linear blend toward amount (weight 1 copies verbatim)
    Add      = 1u, // [-] - current + (amount - Neutral) * weight
    Multiply = 2u, // [-] - current * (1 + (amount - 1) * weight)
    Maximum  = 3u, // [-] - max(current, weightedAmount)
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    TOML SUBSET VALUE TREE
//------------------------------------------------------------------------------------------------------------------------

struct TomlValue;

struct TomlField
{
    std::string                 Key   {}; // [-] - Bare TOML key name
    std::shared_ptr<TomlValue>  Value {}; // [-] - Parsed value
};

struct TomlTable
{
    std::vector<TomlField> Fields {}; // [-] - Insertion-ordered key/value entries

    [[nodiscard]] const TomlValue* Find(std::string_view key) const noexcept;
    [[nodiscard]] TomlValue* FindMutable(std::string_view key) noexcept;
    TomlValue& Set(std::string_view key, TomlValue value);
};

struct TomlArray
{
    std::vector<std::shared_ptr<TomlValue>> Entries {}; // [-] - Ordered array entries
};

struct TomlValue
{
    using VariantType = std::variant<std::monostate, bool, int64_t, double, std::string, TomlArray, TomlTable>;
    VariantType Storage {}; // [-] - Concrete scalar, array, or table storage

    [[nodiscard]] bool IsBool() const noexcept { return std::holds_alternative<bool>(Storage); }
    [[nodiscard]] bool IsInt() const noexcept { return std::holds_alternative<int64_t>(Storage); }
    [[nodiscard]] bool IsNumber() const noexcept { return std::holds_alternative<int64_t>(Storage) || std::holds_alternative<double>(Storage); }
    [[nodiscard]] bool IsString() const noexcept { return std::holds_alternative<std::string>(Storage); }
    [[nodiscard]] bool IsArray() const noexcept { return std::holds_alternative<TomlArray>(Storage); }
    [[nodiscard]] bool IsTable() const noexcept { return std::holds_alternative<TomlTable>(Storage); }

    [[nodiscard]] double AsDouble(double fallback = 0.0) const noexcept;
    [[nodiscard]] int64_t AsInt64(int64_t fallback = 0) const noexcept;
    [[nodiscard]] bool AsBool(bool fallback = false) const noexcept;
    [[nodiscard]] std::string_view AsString(std::string_view fallback = {}) const noexcept;
    [[nodiscard]] const TomlArray* AsArray() const noexcept { return std::get_if<TomlArray>(&Storage); }
    [[nodiscard]] const TomlTable* AsTable() const noexcept { return std::get_if<TomlTable>(&Storage); }
};

/// Parses the Frontier TOML subset (TomlSubset.js L1-L152). Returns std::nullopt and writes outError on syntax failure.
[[nodiscard]] std::optional<TomlTable> ParseTomlSubset(std::string_view text, std::string* outError = nullptr);

/// Serializes a TomlValue to sorted-key canonical JSON (LayerSpecificationValidation.js L93-L98).
[[nodiscard]] std::string FormatCanonicalJson(const TomlValue& value);

/// Computes 64-bit FNV-1a hex digest over UTF-8 text (LayerSpecificationValidation.js L101-L108).
[[nodiscard]] std::string ComputeFnv1a64Hex(std::string_view utf8Text);

//------------------------------------------------------------------------------------------------------------------------
//                                                  SPECIFICATION STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

using LayerParameterValue = std::variant<
    double,
    int64_t,
    bool,
    std::array<double, 3>,
    std::string,
    std::vector<std::string>>;

struct ChoiceOptionDescriptor
{
    int64_t     Code  = 0;  // [-] - Explicit integer code
    std::string Token {};   // [-] - Serialized token string
    std::string Label {};   // [-] - UI display label
};

struct ParameterSpecification
{
    uint32_t                            Code       = 0u;                                 // [-] - Stable per-layer parameter code (>= 1)
    std::string                         Name       {};                                   // [-] - Evaluated block field name
    std::string                         Label      {};                                   // [-] - UI label
    ParameterTypeOrdinal                Type       = ParameterTypeOrdinal::Float32;      // [-] - Value type
    ParameterTargetOrdinal              Target     = ParameterTargetOrdinal::Region;     // [-] - Output block target
    double                              Minimum    = 0.0;                                // [-] - Numeric minimum
    double                              Maximum    = 1.0;                                // [-] - Numeric maximum
    double                              Step       = 0.0;                                // [-] - UI step hint
    bool                                Keyable    = false;                              // [-] - True when KeyTracks can animate this parameter
    LayerParameterValue                 DefaultVal = 0.0;                                // [-] - Initial value when adding layer
    LayerParameterValue                 NeutralVal = 0.0;                                // [-] - No-op neutral value when layer is absent
    std::vector<ChoiceOptionDescriptor> Options    {};                                   // [-] - Valid Choice options
};

struct LayerCategorySpecification
{
    uint32_t                            Code        = 0u;                              // [-] - Layer code (101..109 gas, 201..219 particle)
    std::string                         Token       {};                                // [-] - Lower-case hyphenated token
    std::string                         Name        {};                                // [-] - Display name
    LayerStackOrdinal                   Stack       = LayerStackOrdinal::GasVolume;    // [-] - Owning stack kind
    LayerPhaseOrdinal                   Phase       = LayerPhaseOrdinal::Advance;      // [-] - Evaluation phase order
    bool                                Required    = false;                           // [-] - Must be present at index 0
    uint32_t                            Capacity    = 1u;                              // [-] - Max layers of this category per stack
    std::string                         Linked      {};                                // [-] - Optional linked node category (e.g., "Emitter")
    std::vector<ParameterSpecification> Parameters  {};                                // [-] - Parameter specifications
};

struct EffectLayerSpecificationSet
{
    uint32_t                                 Revision              = 1u;                    // [-] - Document.Revision
    uint32_t                                 MaximumLayers         = kDefaultMaximumLayers; // [-] - Document.MaximumLayers
    std::string                              SpecificationHash     {};                      // [-] - 16-hex FNV-1a over full canonical JSON
    std::string                              CoreSpecificationHash {};                      // [-] - 16-hex FNV-1a over canonical 28 HTML layers (Code < 220)
    std::vector<LayerCategorySpecification>  Layers                {};                      // [-] - All layer category specs

    [[nodiscard]] const LayerCategorySpecification* FindByCode(uint32_t categoryCode) const noexcept;
    [[nodiscard]] const LayerCategorySpecification* FindByToken(std::string_view token) const noexcept;
    [[nodiscard]] const ParameterSpecification* FindParameter(const LayerCategorySpecification& layer, std::string_view name) const noexcept;
};

//------------------------------------------------------------------------------------------------------------------------
//                                                  LOADING & COERCION API
//------------------------------------------------------------------------------------------------------------------------

/// Parses and validates a LayerSpecifications.toml string into an EffectLayerSpecificationSet.
[[nodiscard]] std::optional<EffectLayerSpecificationSet> LoadLayerSpecificationsFromToml(
    std::string_view tomlText,
    std::string* outError = nullptr);

/// Reads a LayerSpecifications.toml file from disk and parses it.
[[nodiscard]] std::optional<EffectLayerSpecificationSet> LoadLayerSpecificationsFromFile(
    std::string_view filePath,
    std::string* outError = nullptr);

/// Converts a TomlValue into a typed LayerParameterValue according to parameter.Type (with Choice token/code coercion).
[[nodiscard]] LayerParameterValue DecodeParameterValueFromToml(
    const ParameterSpecification& parameter,
    const TomlValue& rawValue);

/// Clamps / coerces an existing LayerParameterValue to parameter constraints (LayerStack.js L63-L76).
[[nodiscard]] LayerParameterValue CoerceParameterValue(
    const ParameterSpecification& parameter,
    const LayerParameterValue& value);

/// Resolves a Choice option integer code to its canonical Token string (LayerStack.js L59).
[[nodiscard]] std::string_view ResolveChoiceToken(const ParameterSpecification& parameter, int64_t code) noexcept;

/// Resolves a Choice option Token string to its canonical integer code (LayerStack.js L60).
[[nodiscard]] int64_t ResolveChoiceCode(const ParameterSpecification& parameter, std::string_view token) noexcept;

} // namespace Frontier::SimulationSpace::EffectLayers
