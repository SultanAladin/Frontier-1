/*==========================================================================================================================================
                                                          MATERIALAUTHORING.H
==========================================================================================================================================*/
// 🧩 Separate PBR-layer addresses, procedural-field inputs and designer projections over shared material parameters.

#pragma once
#include <array>
#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace Frontier
{
//------------------------------------------------------------------------------------------------------------------------
//                                                     PBR CHANNEL TABLE
//------------------------------------------------------------------------------------------------------------------------

// 📝 One row per HTML PbrChannels entry; Target is the flat recipe key the channel projects onto.
struct MaterialPbrChannel
{
    const char *Identity = "";
    const char *Caption = "";
    const char *Category = ""; // scalar | colour | vector
    const char *Target = "";
    double Minimum = 0, Maximum = 0;
    bool Bounded = false;
};

[[nodiscard]] std::span<const MaterialPbrChannel> QueryMaterialPbrChannels() noexcept;
[[nodiscard]] const MaterialPbrChannel *QueryMaterialPbrChannel(std::string_view Identity) noexcept;

//------------------------------------------------------------------------------------------------------------------------
//                                                     AUTHORING DOCUMENT
//------------------------------------------------------------------------------------------------------------------------

// 📝 Quantities mirror the HTML value domain: finite numbers, flags, texts (colours ride as #rrggbb), vec3s.
using MaterialAuthoringQuantity = std::variant<double, bool, std::string, std::array<double, 3>>;
// 📝 Flat authored parameters outside the document (the recipe scalars/texts the addresses resolve into).
using MaterialAuthoringStore = std::map<std::string, MaterialAuthoringQuantity>;

struct MaterialAuthoringLayer
{
    std::string Identity, Name, Model, Generator; // Model: surface | flakes | coat | detail
    bool Enabled = true;
    std::vector<std::string> Channels;
};

struct MaterialAuthoringField
{
    std::string Identity, Name, Category, Procedure;
    bool Enabled = true, Coded = false;
    std::vector<std::string> Inputs, Outputs;
};

struct MaterialAuthoringExposure
{
    std::string Name;
    double Minimum = 0, Maximum = 1;
    bool Bounded = false;
};

struct MaterialAuthoringVariable
{
    std::string Identity, Name, Address, Category; // Category: scalar | colour | vector
    double Minimum = 0, Maximum = 1;
    bool Bounded = false;
    std::optional<MaterialAuthoringExposure> Exposure;
};

struct MaterialAuthoringTexture
{
    std::string Name, DataUrl, Origin;
    uint32_t Width = 0, Height = 0;
};

struct MaterialAuthoringSource
{
    std::string Mode = "value"; // value | solid | texture | linear | radial | conic | ramp | field | pattern
    std::optional<bool> Direct;
    std::string Generator;
    std::vector<std::string> Stops;
    std::optional<MaterialAuthoringTexture> Texture;
};

struct MaterialAuthoringDocument
{
    int64_t Version = 2;
    std::vector<MaterialAuthoringLayer> Layers;
    std::vector<MaterialAuthoringField> Generators;
    std::vector<MaterialAuthoringVariable> Variables;
    std::map<std::string, std::string> Links;
    MaterialAuthoringStore Inputs, Designer;
    std::map<std::string, MaterialAuthoringSource> Sources;
};

[[nodiscard]] std::string IssueMaterialAuthoringIdentity();
// 📝 Strict document gate; throws std::runtime_error with the HTML diagnostic text on the first violation.
void NormalizeMaterialAuthoring(MaterialAuthoringDocument &Document);
[[nodiscard]] std::string MaterialChannelAddress(const MaterialAuthoringLayer &Layer, std::string_view Channel);
// 📝 Link chain resolution; throws on cycles and missing variables, like the HTML resolver.
[[nodiscard]] std::string ResolveMaterialAddress(const MaterialAuthoringDocument &Document, std::string_view Address);
[[nodiscard]] std::optional<MaterialAuthoringQuantity> ReadMaterialParameter(
    const MaterialAuthoringDocument &Document, const MaterialAuthoringStore &Store, std::string_view Address);
void WriteMaterialParameter(MaterialAuthoringDocument &Document, MaterialAuthoringStore &Store,
    std::string_view Address, const MaterialAuthoringQuantity &Quantity);
// 📝 Appends a coded-field instance with the shared scale/seed/angle/amount inputs (plus the camouflage
// palette); returns its identity so a channel source can reference it.
std::string DeclareCodedField(MaterialAuthoringDocument &Document, std::string_view Procedure,
    std::string_view Name, std::string_view Category);

//------------------------------------------------------------------------------------------------------------------------
//                                                     DECLARATION AND PROJECTION
//------------------------------------------------------------------------------------------------------------------------

struct MaterialAuthoringControlTarget
{
    std::string Key;
    double Minimum = 0, Maximum = 1;
    bool Integer = false;
};

struct MaterialAuthoringControl
{
    std::string Key;
    bool Direct = false;
    double Minimum = 0, Maximum = 1, Step = .01;
    std::vector<MaterialAuthoringControlTarget> Targets;
};

// 📝 The recipe facts the declaration reads; the catalogue entry supplies them without UI involvement.
struct MaterialAuthoringDeclaration
{
    int64_t Type = 0;
    double Coat = 0, Sheen = 0, Anisotropy = 0, Translucency = 0;
    bool MetalScratches = false, Woven = false;
    std::string RecipeIdentity, RecipeCaption;
    std::vector<MaterialAuthoringControl> Controls;
};

[[nodiscard]] MaterialAuthoringDocument DeclareMaterialAuthoring(const MaterialAuthoringDeclaration &Declaration);

struct MaterialAuthoringProjection
{
    double FlakeWeight = 1;
    bool FlakeColour = false, FlakeRoughness = false, FlakeMetallic = false, FlakeNormal = false;
};

// 📝 Projects link-resolved inputs into the flat store and applies layer/field enablement exactly like
// ResolveAuthoringParameters: retired channels fall back to channel defaults, a retired non-coded field
// neutralizes the procedural type, and flake outputs report through the returned projection.
MaterialAuthoringProjection ResolveAuthoringParameters(const MaterialAuthoringDocument &Document,
    MaterialAuthoringStore &Store, bool ProjectionEnabled = true);

[[nodiscard]] double DesignerControlQuantity(const MaterialAuthoringDocument &Document,
    const MaterialAuthoringStore &Store, const MaterialAuthoringControl &Control);
void ProjectDesignerControl(MaterialAuthoringDocument &Document, MaterialAuthoringStore &Store,
    const MaterialAuthoringControl &Control, double Quantity);
} // namespace Frontier

namespace toml
{
inline namespace v3
{
class table;
}
}

namespace Frontier
{
//------------------------------------------------------------------------------------------------------------------------
//                                                     TOML SERIALIZATION
//------------------------------------------------------------------------------------------------------------------------

// 📝 [Authoring] table codec for .material documents; decode finishes with the strict normalize gate.
[[nodiscard]] toml::table EncodeMaterialAuthoring(const MaterialAuthoringDocument &Document);
[[nodiscard]] MaterialAuthoringDocument DecodeMaterialAuthoring(const toml::table &Table);
} // namespace Frontier
