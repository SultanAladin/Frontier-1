/*==========================================================================================================================================
                                                          MATERIALAUTHORING.CPP
==========================================================================================================================================*/
// 🧩 Strict authoring-document gate, link resolution and designer projection matching the HTML specification.

#include "Engine/ContentInterchange/MaterialAuthoring.h"
#include "Engine/Render/MaterialProceduralBaking.h"
#include <toml++/toml.hpp>
#include <algorithm>
#include <cmath>
#include <random>
#include <set>
#include <stdexcept>

namespace Frontier
{
namespace
{
//------------------------------------------------------------------------------------------------------------------------
//                                                     VALIDATION SUBSTRATE
//------------------------------------------------------------------------------------------------------------------------

constexpr std::array<MaterialPbrChannel, 13> PbrChannelTable{{
    {"colour", "Colour", "colour", "color", 0, 0, false},
    {"roughness", "Roughness", "scalar", "roughness", 0, 1, true},
    {"metallic", "Metallic", "scalar", "metalness", 0, 1, true},
    {"normal", "Normal", "vector", "normal", 0, 0, false},
    {"microNormal", "Micro normal", "vector", "microNormal", 0, 0, false},
    {"ior", "IOR", "scalar", "ior", 1, 3, true},
    {"specular", "Specular", "scalar", "specular", 0, 1, true},
    {"thickness", "Thickness", "scalar", "depth", 0, 10, true},
    {"transmission", "Transmission", "scalar", "translucency", 0, 1, true},
    {"coverage", "Coverage", "scalar", "coverage", 0, 1, true},
    {"sheen", "Sheen", "scalar", "sheen", 0, 1, true},
    {"sheenRoughness", "Sheen roughness", "scalar", "sheenRoughness", 0, 1, true},
    {"anisotropy", "Anisotropy", "scalar", "anisotropy", 0, 1, true},
}};

bool IdentityValid(std::string_view Identity)
{
    if (Identity.size() != 36) return false;
    for (size_t Index = 0; Index < Identity.size(); ++Index)
    {
        const char Letter = Identity[Index];
        if (Index == 8 || Index == 13 || Index == 18 || Index == 23)
        {
            if (Letter != '-') return false;
        }
        else if (!((Letter >= '0' && Letter <= '9') || (Letter >= 'a' && Letter <= 'f') ||
                     (Letter >= 'A' && Letter <= 'F')))
            return false;
    }
    return true;
}

bool AddressValid(std::string_view Address)
{
    if (Address.empty() || Address.size() > 150) return false;
    if (Address == "__proto__" || Address == "constructor" || Address == "prototype") return false;
    for (const char Letter : Address)
        if (!((Letter >= 'a' && Letter <= 'z') || (Letter >= 'A' && Letter <= 'Z') ||
                (Letter >= '0' && Letter <= '9') || Letter == ':' || Letter == '-'))
            return false;
    return true;
}

bool ColourValid(std::string_view Colour)
{
    if (Colour.size() != 7 || Colour[0] != '#') return false;
    for (size_t Index = 1; Index < Colour.size(); ++Index)
    {
        const char Letter = Colour[Index];
        if (!((Letter >= '0' && Letter <= '9') || (Letter >= 'a' && Letter <= 'f') || (Letter >= 'A' && Letter <= 'F')))
            return false;
    }
    return true;
}

bool QuantityValid(const MaterialAuthoringQuantity &Quantity)
{
    if (const auto *Scalar = std::get_if<double>(&Quantity)) return std::isfinite(*Scalar);
    if (const auto *Text = std::get_if<std::string>(&Quantity)) return Text->size() <= 1024;
    if (const auto *Vector = std::get_if<std::array<double, 3>>(&Quantity))
        return std::all_of(Vector->begin(), Vector->end(), [](double Component) { return std::isfinite(Component); });
    return true;
}

// 📝 Embedded channel images stay bounded like the HTML source gate: PNG data URL, 300 kB ceiling,
// 1..256 px edges, and IHDR dimensions that agree with the declared extent.
bool TextureValid(const MaterialAuthoringTexture &Texture)
{
    constexpr std::string_view Prefix = "data:image/png;base64,";
    if (!Texture.DataUrl.starts_with(Prefix) || Texture.DataUrl.size() > 300000) return false;
    if (!Texture.Width || !Texture.Height || Texture.Width > 256 || Texture.Height > 256) return false;
    const std::string_view Encoded = std::string_view(Texture.DataUrl).substr(Prefix.size());
    constexpr const char *Alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::array<uint8_t, 36> Header{};
    size_t Produced = 0;
    uint32_t Accumulator = 0;
    int Bits = 0;
    for (const char Letter : Encoded.substr(0, 48))
    {
        if (Letter == '=') break;
        const char *Position = std::strchr(Alphabet, Letter);
        if (!Position) return false;
        Accumulator = (Accumulator << 6) | uint32_t(Position - Alphabet);
        Bits += 6;
        if (Bits >= 8)
        {
            Bits -= 8;
            if (Produced < Header.size()) Header[Produced++] = uint8_t((Accumulator >> Bits) & 0xff);
        }
    }
    if (Produced < 24) return false;
    constexpr std::array<uint8_t, 8> Signature{137, 80, 78, 71, 13, 10, 26, 10};
    if (!std::equal(Signature.begin(), Signature.end(), Header.begin())) return false;
    if (std::string_view(reinterpret_cast<const char *>(Header.data()) + 12, 4) != "IHDR") return false;
    const auto ReadExtent = [&Header](size_t Offset) {
        return (uint32_t(Header[Offset]) << 24) | (uint32_t(Header[Offset + 1]) << 16) |
               (uint32_t(Header[Offset + 2]) << 8) | uint32_t(Header[Offset + 3]);
    };
    return ReadExtent(16) == Texture.Width && ReadExtent(20) == Texture.Height;
}

double StoreScalar(const MaterialAuthoringStore &Store, std::string_view Key, double Fallback)
{
    const auto Found = Store.find(std::string(Key));
    if (Found == Store.end()) return Fallback;
    if (const auto *Scalar = std::get_if<double>(&Found->second)) return *Scalar;
    return Fallback;
}

const MaterialAuthoringVariable *FindVariable(const MaterialAuthoringDocument &Document, std::string_view Identity)
{
    for (const auto &Variable : Document.Variables)
        if (Variable.Identity == Identity) return &Variable;
    return nullptr;
}
} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                     PBR CHANNEL TABLE
//------------------------------------------------------------------------------------------------------------------------

std::span<const MaterialPbrChannel> QueryMaterialPbrChannels() noexcept { return PbrChannelTable; }

const MaterialPbrChannel *QueryMaterialPbrChannel(std::string_view Identity) noexcept
{
    for (const auto &Channel : PbrChannelTable)
        if (Identity == Channel.Identity) return &Channel;
    return nullptr;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     IDENTITY AND NORMALIZE
//------------------------------------------------------------------------------------------------------------------------

std::string IssueMaterialAuthoringIdentity()
{
    static thread_local std::mt19937_64 Generator{std::random_device{}()};
    std::uniform_int_distribution<uint32_t> Byte(0, 255);
    std::array<uint8_t, 16> Octets{};
    for (auto &Octet : Octets) Octet = uint8_t(Byte(Generator));
    Octets[6] = uint8_t((Octets[6] & 0x0f) | 0x40);
    Octets[8] = uint8_t((Octets[8] & 0x3f) | 0x80);
    constexpr const char *Digits = "0123456789abcdef";
    std::string Identity;
    Identity.reserve(36);
    for (size_t Index = 0; Index < Octets.size(); ++Index)
    {
        if (Index == 4 || Index == 6 || Index == 8 || Index == 10) Identity += '-';
        Identity += Digits[Octets[Index] >> 4];
        Identity += Digits[Octets[Index] & 0x0f];
    }
    return Identity;
}

void NormalizeMaterialAuthoring(MaterialAuthoringDocument &Document)
{
    if (Document.Version != 2 || Document.Layers.empty() || Document.Layers.size() > 16 ||
        Document.Generators.size() > 32 || Document.Variables.size() > 128)
        throw std::runtime_error("Invalid material authoring document.");
    std::set<std::string> Tokens, Names;
    const auto ClaimToken = [&Tokens](const std::string &Identity)
    {
        if (!IdentityValid(Identity) || Tokens.count(Identity))
            throw std::runtime_error("Invalid or duplicate authoring identity.");
        Tokens.insert(Identity);
    };
    for (const auto &Layer : Document.Layers)
    {
        ClaimToken(Layer.Identity);
        const std::set<std::string> Unique(Layer.Channels.begin(), Layer.Channels.end());
        if (Layer.Name.size() > 80 ||
            (Layer.Model != "surface" && Layer.Model != "flakes" && Layer.Model != "coat" && Layer.Model != "detail") ||
            Unique.size() != Layer.Channels.size() ||
            std::any_of(Layer.Channels.begin(), Layer.Channels.end(),
                [](const std::string &Channel) { return !QueryMaterialPbrChannel(Channel); }))
            throw std::runtime_error("Invalid PBR layer.");
    }
    for (const auto &Field : Document.Generators)
    {
        ClaimToken(Field.Identity);
        if (Field.Name.size() > 80 || Field.Category.size() > 60 || Field.Inputs.size() > 100 ||
            std::any_of(Field.Inputs.begin(), Field.Inputs.end(),
                [](const std::string &Address) { return Address.size() > 150; }))
            throw std::runtime_error("Invalid field declaration.");
    }
    for (const auto &Variable : Document.Variables)
    {
        ClaimToken(Variable.Identity);
        if (Variable.Name.empty() || Variable.Name.size() > 60 || Names.count(Variable.Name) ||
            Variable.Address.size() > 150 ||
            (Variable.Category != "scalar" && Variable.Category != "colour" && Variable.Category != "vector"))
            throw std::runtime_error("Invalid shared variable.");
        Names.insert(Variable.Name);
        if (Variable.Category == "scalar" &&
            (!std::isfinite(Variable.Minimum) || !std::isfinite(Variable.Maximum) || Variable.Minimum >= Variable.Maximum))
            throw std::runtime_error("Invalid variable range.");
        if (Variable.Exposure &&
            (Variable.Exposure->Name.empty() || Variable.Exposure->Name.size() > 80 ||
                (Variable.Category == "scalar" &&
                    (!std::isfinite(Variable.Exposure->Minimum) || !std::isfinite(Variable.Exposure->Maximum) ||
                        Variable.Exposure->Minimum < Variable.Minimum || Variable.Exposure->Maximum > Variable.Maximum ||
                        Variable.Exposure->Minimum >= Variable.Exposure->Maximum))))
            throw std::runtime_error("Invalid designer exposure.");
    }
    for (const auto &[Address, Reference] : Document.Links)
    {
        if (Address.size() > 150 || !FindVariable(Document, Reference))
            throw std::runtime_error("Missing shared source.");
        (void)ResolveMaterialAddress(Document, Address);
    }
    if (Document.Inputs.size() > 512 || Document.Sources.size() > 512 || Document.Designer.size() > 512)
        throw std::runtime_error("Invalid authoring parameter index.");
    for (const auto &Variable : Document.Variables) (void)ResolveMaterialAddress(Document, Variable.Address);
    for (const auto &Variable : Document.Variables)
        if (!AddressValid(Variable.Address)) throw std::runtime_error("Invalid parameter address.");
    for (const auto &Field : Document.Generators)
        if (std::any_of(Field.Inputs.begin(), Field.Inputs.end(),
                [](const std::string &Address) { return !AddressValid(Address); }) ||
            (Field.Coded && !QueryCodedField(Field.Procedure)))
            throw std::runtime_error("Unknown generator procedure or input.");
    for (const auto &[Address, Quantity] : Document.Inputs)
        if (!AddressValid(Address) || !QuantityValid(Quantity)) throw std::runtime_error("Invalid field input.");
    for (const auto &[Address, Source] : Document.Sources)
    {
        static constexpr std::array<const char *, 9> Modes{"value", "solid", "texture", "linear", "radial",
            "conic", "ramp", "field", "pattern"};
        if (!AddressValid(Address) ||
            std::none_of(Modes.begin(), Modes.end(), [&Source](const char *Mode) { return Source.Mode == Mode; }))
            throw std::runtime_error("Invalid channel source.");
        if (Source.Texture && !TextureValid(*Source.Texture))
            throw std::runtime_error("Invalid embedded channel image.");
        if (!Source.Stops.empty() &&
            (Source.Stops.size() < 2 || Source.Stops.size() > 8 ||
                std::any_of(Source.Stops.begin(), Source.Stops.end(),
                    [](const std::string &Colour) { return !ColourValid(Colour); })))
            throw std::runtime_error("Invalid channel colour ramp.");
        if (!Source.Generator.empty() &&
            std::none_of(Document.Generators.begin(), Document.Generators.end(),
                [&Source](const MaterialAuthoringField &Field) { return Field.Identity == Source.Generator && Field.Coded; }))
            throw std::runtime_error("Missing coded field instance.");
    }
    for (const auto &[Address, Reference] : Document.Links)
    {
        if (!AddressValid(Address)) throw std::runtime_error("Invalid destination address.");
        const auto *Variable = FindVariable(Document, Reference);
        const MaterialPbrChannel *Channel = nullptr;
        if (const size_t Colon = Address.rfind(':'); Colon != std::string::npos)
            Channel = QueryMaterialPbrChannel(Address.substr(Colon + 1));
        else
        {
            for (const auto &Candidate : PbrChannelTable)
                if (Address == Candidate.Target) Channel = &Candidate;
            if (!Channel)
            {
                if (Address == "coat") Channel = QueryMaterialPbrChannel("coverage");
                else if (Address == "coatRoughness") Channel = QueryMaterialPbrChannel("roughness");
                else if (Address == "coatIor") Channel = QueryMaterialPbrChannel("ior");
                else if (Address == "coatNormal") Channel = QueryMaterialPbrChannel("normal");
            }
        }
        if (Channel && Variable &&
            (Variable->Category != Channel->Category ||
                (std::string(Channel->Category) == "scalar" &&
                    (Variable->Minimum < Channel->Minimum || Variable->Maximum > Channel->Maximum))))
            throw std::runtime_error("Shared variable does not fit this channel domain.");
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     ADDRESS RESOLUTION
//------------------------------------------------------------------------------------------------------------------------

std::string MaterialChannelAddress(const MaterialAuthoringLayer &Layer, std::string_view Channel)
{
    if (Layer.Model == "coat")
    {
        if (Channel == "roughness") return "coatRoughness";
        if (Channel == "ior") return "coatIor";
        if (Channel == "coverage") return "coat";
        if (Channel == "normal") return "coatNormal";
        return "layer:" + Layer.Identity + ":" + std::string(Channel);
    }
    if (Layer.Model == "flakes") return "flake-output:" + std::string(Channel);
    if (Layer.Model == "surface")
        if (const auto *Specification = QueryMaterialPbrChannel(Channel)) return Specification->Target;
    return "layer:" + Layer.Identity + ":" + std::string(Channel);
}

std::string ResolveMaterialAddress(const MaterialAuthoringDocument &Document, std::string_view Address)
{
    std::set<std::string> Visited;
    std::string Current{Address};
    while (true)
    {
        if (Visited.count(Current)) throw std::runtime_error("Cyclic shared material variable.");
        Visited.insert(Current);
        const auto Reference = Document.Links.find(Current);
        if (Reference == Document.Links.end()) return Current;
        const auto *Variable = FindVariable(Document, Reference->second);
        if (!Variable) throw std::runtime_error("Shared variable is missing.");
        Current = Variable->Address;
    }
}

std::optional<MaterialAuthoringQuantity> ReadMaterialParameter(const MaterialAuthoringDocument &Document,
    const MaterialAuthoringStore &Store, std::string_view Address)
{
    const std::string Resolved = ResolveMaterialAddress(Document, Address);
    const auto &Index = Resolved.find(':') != std::string::npos ? Document.Inputs : Store;
    const auto Found = Index.find(Resolved);
    if (Found == Index.end()) return std::nullopt;
    return Found->second;
}

void WriteMaterialParameter(MaterialAuthoringDocument &Document, MaterialAuthoringStore &Store,
    std::string_view Address, const MaterialAuthoringQuantity &Quantity)
{
    if (const auto *Scalar = std::get_if<double>(&Quantity); Scalar && !std::isfinite(*Scalar))
        throw std::runtime_error("Material input must be finite.");
    const std::string Resolved = ResolveMaterialAddress(Document, Address);
    if (Resolved.find(':') != std::string::npos) Document.Inputs[Resolved] = Quantity;
    else Store[Resolved] = Quantity;
}

std::string DeclareCodedField(MaterialAuthoringDocument &Document, std::string_view Procedure,
    std::string_view Name, std::string_view Category)
{
    const std::string Identity = IssueMaterialAuthoringIdentity();
    MaterialAuthoringField Field;
    Field.Identity = Identity;
    Field.Name = std::string(Name).substr(0, 80);
    Field.Category = std::string(Category).substr(0, 60);
    Field.Procedure = std::string(Procedure);
    Field.Coded = true;
    const bool Camouflage = Procedure.starts_with("camouflage");
    std::vector<std::string> Keys{"scale", "seed", "angle", "amount"};
    if (Camouflage) Keys.insert(Keys.end(), {"colour0", "colour1", "colour2", "colour3", "softness", "pixelSize"});
    for (const auto &Key : Keys) Field.Inputs.push_back("field:" + Identity + ":" + Key);
    Field.Outputs = {"colour", "roughness", "metallic", "normal", "coverage"};
    Document.Generators.push_back(std::move(Field));
    if (Camouflage)
    {
        static constexpr std::array<const char *, 4> Palette{"#b5a477", "#566044", "#514334", "#232921"};
        for (size_t Index = 0; Index < Palette.size(); ++Index)
            Document.Inputs["field:" + Identity + ":colour" + std::to_string(Index)] = std::string(Palette[Index]);
        Document.Inputs["field:" + Identity + ":softness"] = .012;
        Document.Inputs["field:" + Identity + ":pixelSize"] = 4.;
    }
    Document.Inputs["field:" + Identity + ":scale"] = 8.;
    Document.Inputs["field:" + Identity + ":seed"] = 12.;
    Document.Inputs["field:" + Identity + ":angle"] = 0.;
    Document.Inputs["field:" + Identity + ":amount"] = 1.;
    return Identity;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     DECLARATION AND PROJECTION
//------------------------------------------------------------------------------------------------------------------------

MaterialAuthoringDocument DeclareMaterialAuthoring(const MaterialAuthoringDeclaration &Declaration)
{
    MaterialAuthoringDocument Document;
    if (Declaration.Type == 0)
    {
        MaterialAuthoringField Flakes;
        Flakes.Identity = IssueMaterialAuthoringIdentity();
        Flakes.Name = "Flake distribution";
        Flakes.Category = "Particle fields";
        Flakes.Procedure = "flakes";
        Flakes.Inputs = {"flakes", "flakeSize", "flakeScale", "flakeLayers", "flakeLayerDepth", "flakeTilt",
            "flakeRoughnessMin", "flakeRoughnessMax", "flakeMetalnessMin", "flakeMetalnessMax"};
        Flakes.Outputs = {"colour", "roughness", "metallic", "normal", "coverage"};
        Document.Generators.push_back(std::move(Flakes));
    }
    const std::string Procedure = Declaration.Type == 17 ? "solar" : Declaration.Type == 18 ? "dimples"
        : Declaration.Type == 29 ? "scratches" : Declaration.Woven ? "weave" : Declaration.RecipeIdentity;
    if (Declaration.Type != 0)
    {
        MaterialAuthoringField Field;
        Field.Identity = IssueMaterialAuthoringIdentity();
        Field.Name = Procedure == "solar" ? "Solar cell grid" : Procedure == "dimples" ? "Dimple distribution"
            : Procedure == "scratches" ? "Scratch field" : Procedure == "weave" ? "Yarn construction"
            : Declaration.RecipeCaption;
        Field.Category = Procedure == "weave" ? "Textile fields"
            : (Procedure == "dimples" || Procedure == "scratches") ? "Relief fields" : "Structural fields";
        Field.Procedure = Procedure;
        std::set<std::string> Seen;
        for (const auto &Control : Declaration.Controls)
        {
            std::vector<std::string> Keys;
            if (Control.Direct) Keys.push_back(Control.Key);
            else for (const auto &Target : Control.Targets) Keys.push_back(Target.Key);
            for (const auto &Key : Keys)
            {
                const bool ChannelTarget = std::any_of(PbrChannelTable.begin(), PbrChannelTable.end(),
                    [&Key](const MaterialPbrChannel &Channel) { return Key == Channel.Target; });
                if (ChannelTarget || Key == "coat" || Key == "coatRoughness" || Key == "coatIor") continue;
                if (Seen.insert(Key).second) Field.Inputs.push_back(Key);
            }
        }
        Field.Outputs = {"colour", "normal", "roughness", "metallic"};
        Document.Generators.push_back(std::move(Field));
    }
    if (Declaration.MetalScratches && Declaration.Type != 29)
    {
        MaterialAuthoringField Scratches;
        Scratches.Identity = IssueMaterialAuthoringIdentity();
        Scratches.Name = "Scratch field";
        Scratches.Category = "Relief fields";
        Scratches.Procedure = "scratches";
        Scratches.Inputs = {"scratchAngle", "scratchDensity", "scratchLength", "scratchWidth", "scratchDepth",
            "scratchSpread", "scratchBend", "scratchScale"};
        Scratches.Outputs = {"normal", "roughness", "colour"};
        Document.Generators.push_back(std::move(Scratches));
    }
    MaterialAuthoringLayer Surface;
    Surface.Identity = IssueMaterialAuthoringIdentity();
    Surface.Name = Declaration.Type == 0 ? "Paint body" : "Surface body";
    Surface.Model = "surface";
    Surface.Channels = {"colour", "roughness", "metallic", "normal", "ior", "specular", "thickness"};
    if (Declaration.Type == 5 || Declaration.Translucency > 0) Surface.Channels.emplace_back("transmission");
    if (Declaration.Sheen > 0)
    {
        Surface.Channels.emplace_back("sheen");
        Surface.Channels.emplace_back("sheenRoughness");
    }
    if (Declaration.Anisotropy > 0) Surface.Channels.emplace_back("anisotropy");
    Document.Layers.push_back(Surface);
    if (Declaration.Type == 0)
    {
        MaterialAuthoringLayer Flakes;
        Flakes.Identity = IssueMaterialAuthoringIdentity();
        Flakes.Name = "Metallic flakes";
        Flakes.Model = "flakes";
        for (const auto &Field : Document.Generators)
            if (Field.Procedure == "flakes") Flakes.Generator = Field.Identity;
        Flakes.Channels = {"colour", "roughness", "metallic", "normal", "coverage"};
        Document.Layers.insert(Document.Layers.begin(), std::move(Flakes));
    }
    if (Declaration.Type == 0 || Declaration.Coat > 0)
    {
        MaterialAuthoringLayer Coat;
        Coat.Identity = IssueMaterialAuthoringIdentity();
        Coat.Name = "Clearcoat";
        Coat.Model = "coat";
        Coat.Channels = {"coverage", "roughness", "ior"};
        Document.Layers.insert(Document.Layers.begin(), std::move(Coat));
    }
    NormalizeMaterialAuthoring(Document);
    return Document;
}

MaterialAuthoringProjection ResolveAuthoringParameters(const MaterialAuthoringDocument &Document,
    MaterialAuthoringStore &Store, bool ProjectionEnabled)
{
    MaterialAuthoringProjection Projection;
    std::set<std::string> Addresses;
    for (const auto &Field : Document.Generators)
        for (const auto &Input : Field.Inputs) Addresses.insert(Input);
    for (const auto &Layer : Document.Layers)
        for (const auto &Channel : Layer.Channels) Addresses.insert(MaterialChannelAddress(Layer, Channel));
    for (const auto &Variable : Document.Variables) Addresses.insert(Variable.Address);
    for (const auto &[Address, Reference] : Document.Links)
    {
        (void)Reference;
        Addresses.insert(Address);
    }
    for (const auto &Address : Addresses)
    {
        if (Address.find(':') != std::string::npos) continue;
        if (const auto Quantity = ReadMaterialParameter(Document, Store, Address)) Store[Address] = *Quantity;
    }
    if (!ProjectionEnabled) return Projection;
    const auto HasChannel = [](const MaterialAuthoringLayer &Layer, std::string_view Channel) {
        return std::find(Layer.Channels.begin(), Layer.Channels.end(), Channel) != Layer.Channels.end();
    };
    for (const auto &Layer : Document.Layers)
    {
        if (Layer.Model == "surface")
            for (const auto &Specification : PbrChannelTable)
                if (!Layer.Enabled || !HasChannel(Layer, Specification.Identity))
                {
                    if (std::string(Specification.Category) == "colour")
                        Store[Specification.Target] = std::string("#ffffff");
                    else if (std::string(Specification.Category) == "vector")
                        Store[Specification.Target] = std::array<double, 3>{0, 0, 1};
                    else
                        Store[Specification.Target] = Specification.Identity == std::string("roughness") ? .5
                            : Specification.Identity == std::string("ior") ? 1.5
                            : Specification.Identity == std::string("specular") ? 1.
                            : Specification.Identity == std::string("coverage") ? 1. : Specification.Minimum;
                }
        if (Layer.Model == "coat")
        {
            if (!Layer.Enabled || !HasChannel(Layer, "coverage")) Store["coat"] = 0.;
            if (!HasChannel(Layer, "roughness")) Store["coatRoughness"] = .5;
            if (!HasChannel(Layer, "ior")) Store["coatIor"] = 1.5;
        }
        if (Layer.Model == "flakes")
        {
            const std::string CoverageAddress = ResolveMaterialAddress(Document, MaterialChannelAddress(Layer, "coverage"));
            const auto CoverageSource = Document.Sources.find(CoverageAddress);
            Projection.FlakeWeight = CoverageSource != Document.Sources.end() && CoverageSource->second.Mode != "value"
                ? 1 : StoreScalar(Store, "flake-output:coverage", 1);
            if (const auto Coverage = ReadMaterialParameter(Document, Store, MaterialChannelAddress(Layer, "coverage"));
                Coverage && std::holds_alternative<double>(*Coverage))
                Projection.FlakeWeight = std::get<double>(*Coverage);
            const MaterialAuthoringField *Field = nullptr;
            for (const auto &Candidate : Document.Generators)
                if (Candidate.Identity == Layer.Generator) Field = &Candidate;
            if (!Layer.Enabled || !Field || !Field->Enabled || !HasChannel(Layer, "coverage")) Store["flakes"] = 0.;
            const auto OutputLive = [&Document, &Layer, &HasChannel](std::string_view Channel) {
                const std::string Address = ResolveMaterialAddress(Document, MaterialChannelAddress(Layer, Channel));
                const auto Source = Document.Sources.find(Address);
                return HasChannel(Layer, Channel) &&
                       (Source == Document.Sources.end() || Source->second.Mode == "value");
            };
            Projection.FlakeColour = OutputLive("colour");
            Projection.FlakeRoughness = OutputLive("roughness");
            Projection.FlakeMetallic = OutputLive("metallic");
            Projection.FlakeNormal = OutputLive("normal");
        }
    }
    for (const auto &Field : Document.Generators)
        if (!Field.Coded && !Field.Enabled && Field.Procedure != "flakes" && Field.Procedure != "scratches")
            Store["type"] = -1.;
    for (const auto &Field : Document.Generators)
        if (Field.Procedure == "scratches" && !Field.Enabled)
        {
            Store["metalScratches"] = false;
            if (StoreScalar(Store, "type", 0) == 29) Store["scratchDensity"] = 0.;
        }
    return Projection;
}

double DesignerControlQuantity(const MaterialAuthoringDocument &Document, const MaterialAuthoringStore &Store,
    const MaterialAuthoringControl &Control)
{
    const auto ReadScalar = [&Document, &Store](const std::string &Key, double Fallback) {
        const auto Quantity = ReadMaterialParameter(Document, Store, Key);
        return Quantity && std::holds_alternative<double>(*Quantity) ? std::get<double>(*Quantity) : Fallback;
    };
    if (Control.Direct) return ReadScalar(Control.Key, Control.Minimum);
    if (Control.Targets.empty()) return 0;
    const auto &Target = Control.Targets.front();
    const double Quantity = ReadScalar(Target.Key, Target.Minimum);
    return std::max(0., std::min(1., (Quantity - Target.Minimum) / (Target.Maximum - Target.Minimum)));
}

void ProjectDesignerControl(MaterialAuthoringDocument &Document, MaterialAuthoringStore &Store,
    const MaterialAuthoringControl &Control, double Quantity)
{
    if (!std::isfinite(Quantity)) return;
    if (Control.Direct)
    {
        const double Bounded = std::max(Control.Minimum, std::min(Control.Maximum, Quantity));
        WriteMaterialParameter(Document, Store, Control.Key, Control.Step == 1 ? std::round(Bounded) : Bounded);
        return;
    }
    const double Position = std::max(0., std::min(1., Quantity));
    for (const auto &Target : Control.Targets)
    {
        const double Mapped = Target.Minimum + (Target.Maximum - Target.Minimum) * Position;
        WriteMaterialParameter(Document, Store, Target.Key, Target.Integer ? std::round(Mapped) : Mapped);
    }
}

namespace
{
//------------------------------------------------------------------------------------------------------------------------
//                                                     TOML QUANTITIES
//------------------------------------------------------------------------------------------------------------------------

void InsertQuantity(toml::table &Table, const std::string &Key, const MaterialAuthoringQuantity &Quantity)
{
    if (const auto *Scalar = std::get_if<double>(&Quantity)) Table.insert(Key, *Scalar);
    else if (const auto *Flag = std::get_if<bool>(&Quantity)) Table.insert(Key, *Flag);
    else if (const auto *Text = std::get_if<std::string>(&Quantity)) Table.insert(Key, *Text);
    else if (const auto *Vector = std::get_if<std::array<double, 3>>(&Quantity))
    {
        toml::array Components;
        for (const double Component : *Vector) Components.push_back(Component);
        Table.insert(Key, std::move(Components));
    }
}

MaterialAuthoringQuantity ReadQuantity(const toml::node &Node)
{
    if (const auto Scalar = Node.value<double>()) return *Scalar;
    if (const auto Flag = Node.value<bool>()) return *Flag;
    if (const auto Text = Node.value<std::string>()) return *Text;
    if (const auto *Components = Node.as_array())
    {
        if (Components->size() != 3) throw std::runtime_error("Authoring vector requires three components.");
        std::array<double, 3> Vector{};
        for (size_t Index = 0; Index < 3; ++Index)
        {
            const auto Component = Components->get(Index)->value<double>();
            if (!Component || !std::isfinite(*Component)) throw std::runtime_error("Authoring vector is not finite.");
            Vector[Index] = *Component;
        }
        return Vector;
    }
    throw std::runtime_error("Authoring quantity is not a scalar, flag, text or vector.");
}

toml::array EncodeTexts(const std::vector<std::string> &Texts)
{
    toml::array Result;
    for (const auto &Text : Texts) Result.push_back(Text);
    return Result;
}

template <typename Viewed>
std::vector<std::string> DecodeTexts(const toml::node_view<Viewed> &Field)
{
    std::vector<std::string> Result;
    if (!Field) return Result;
    const auto *Entries = Field.as_array();
    if (!Entries) throw std::runtime_error("Authoring text list must be an array.");
    for (const auto &Entry : *Entries)
    {
        const auto Text = Entry.template value<std::string>();
        if (!Text) throw std::runtime_error("Authoring text list holds a non-text entry.");
        Result.push_back(*Text);
    }
    return Result;
}

std::string RequiredText(const toml::table &Table, std::string_view Key)
{
    const auto Text = Table[Key].value<std::string>();
    if (!Text) throw std::runtime_error("Authoring field is missing text: " + std::string(Key));
    return *Text;
}
} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                     TOML SERIALIZATION
//------------------------------------------------------------------------------------------------------------------------

toml::table EncodeMaterialAuthoring(const MaterialAuthoringDocument &Document)
{
    toml::table Table;
    Table.insert("Version", Document.Version);
    toml::array Layers;
    for (const auto &Layer : Document.Layers)
    {
        toml::table Entry{{"Identity", Layer.Identity}, {"Name", Layer.Name}, {"Model", Layer.Model},
            {"Enabled", Layer.Enabled}, {"Channels", EncodeTexts(Layer.Channels)}};
        if (!Layer.Generator.empty()) Entry.insert("Generator", Layer.Generator);
        Layers.push_back(std::move(Entry));
    }
    Table.insert("Layers", std::move(Layers));
    toml::array Generators;
    for (const auto &Field : Document.Generators)
    {
        toml::table Entry{{"Identity", Field.Identity}, {"Name", Field.Name}, {"Category", Field.Category},
            {"Procedure", Field.Procedure}, {"Enabled", Field.Enabled}, {"Coded", Field.Coded},
            {"Inputs", EncodeTexts(Field.Inputs)}, {"Outputs", EncodeTexts(Field.Outputs)}};
        Generators.push_back(std::move(Entry));
    }
    Table.insert("Generators", std::move(Generators));
    toml::array Variables;
    for (const auto &Variable : Document.Variables)
    {
        toml::table Entry{{"Identity", Variable.Identity}, {"Name", Variable.Name}, {"Address", Variable.Address},
            {"Type", Variable.Category}};
        if (Variable.Bounded)
        {
            Entry.insert("Minimum", Variable.Minimum);
            Entry.insert("Maximum", Variable.Maximum);
        }
        if (Variable.Exposure)
        {
            toml::table Exposure{{"Name", Variable.Exposure->Name}};
            if (Variable.Exposure->Bounded)
            {
                Exposure.insert("Minimum", Variable.Exposure->Minimum);
                Exposure.insert("Maximum", Variable.Exposure->Maximum);
            }
            Entry.insert("Exposure", std::move(Exposure));
        }
        Variables.push_back(std::move(Entry));
    }
    Table.insert("Variables", std::move(Variables));
    toml::table Links;
    for (const auto &[Address, Reference] : Document.Links) Links.insert(Address, Reference);
    Table.insert("Links", std::move(Links));
    toml::table Inputs, Designer;
    for (const auto &[Address, Quantity] : Document.Inputs) InsertQuantity(Inputs, Address, Quantity);
    for (const auto &[Address, Quantity] : Document.Designer) InsertQuantity(Designer, Address, Quantity);
    Table.insert("Inputs", std::move(Inputs));
    Table.insert("Designer", std::move(Designer));
    toml::array Sources;
    for (const auto &[Address, Source] : Document.Sources)
    {
        toml::table Entry{{"Address", Address}, {"Mode", Source.Mode}};
        if (Source.Direct) Entry.insert("Direct", *Source.Direct);
        if (!Source.Generator.empty()) Entry.insert("Generator", Source.Generator);
        if (!Source.Stops.empty()) Entry.insert("Stops", EncodeTexts(Source.Stops));
        if (Source.Texture)
        {
            toml::table Texture{{"Name", Source.Texture->Name}, {"Width", int64_t(Source.Texture->Width)},
                {"Height", int64_t(Source.Texture->Height)}, {"DataUrl", Source.Texture->DataUrl}};
            if (!Source.Texture->Origin.empty()) Texture.insert("Origin", Source.Texture->Origin);
            Entry.insert("Texture", std::move(Texture));
        }
        Sources.push_back(std::move(Entry));
    }
    Table.insert("Sources", std::move(Sources));
    return Table;
}

MaterialAuthoringDocument DecodeMaterialAuthoring(const toml::table &Table)
{
    MaterialAuthoringDocument Document;
    Document.Version = Table["Version"].value_or(int64_t(0));
    if (const auto *Layers = Table["Layers"].as_array())
        for (const auto &Entry : *Layers)
        {
            const auto *Row = Entry.as_table();
            if (!Row) throw std::runtime_error("Authoring layer must be a table.");
            MaterialAuthoringLayer Layer;
            Layer.Identity = RequiredText(*Row, "Identity");
            Layer.Name = RequiredText(*Row, "Name");
            Layer.Model = RequiredText(*Row, "Model");
            Layer.Enabled = (*Row)["Enabled"].value_or(true);
            Layer.Generator = (*Row)["Generator"].value_or(std::string{});
            Layer.Channels = DecodeTexts((*Row)["Channels"]);
            Document.Layers.push_back(std::move(Layer));
        }
    if (const auto *Generators = Table["Generators"].as_array())
        for (const auto &Entry : *Generators)
        {
            const auto *Row = Entry.as_table();
            if (!Row) throw std::runtime_error("Authoring generator must be a table.");
            MaterialAuthoringField Field;
            Field.Identity = RequiredText(*Row, "Identity");
            Field.Name = RequiredText(*Row, "Name");
            Field.Category = RequiredText(*Row, "Category");
            Field.Procedure = RequiredText(*Row, "Procedure");
            Field.Enabled = (*Row)["Enabled"].value_or(true);
            Field.Coded = (*Row)["Coded"].value_or(false);
            Field.Inputs = DecodeTexts((*Row)["Inputs"]);
            Field.Outputs = DecodeTexts((*Row)["Outputs"]);
            Document.Generators.push_back(std::move(Field));
        }
    if (const auto *Variables = Table["Variables"].as_array())
        for (const auto &Entry : *Variables)
        {
            const auto *Row = Entry.as_table();
            if (!Row) throw std::runtime_error("Authoring variable must be a table.");
            MaterialAuthoringVariable Variable;
            Variable.Identity = RequiredText(*Row, "Identity");
            Variable.Name = RequiredText(*Row, "Name");
            Variable.Address = RequiredText(*Row, "Address");
            Variable.Category = RequiredText(*Row, "Type");
            if (const auto Minimum = (*Row)["Minimum"].value<double>())
            {
                Variable.Minimum = *Minimum;
                Variable.Maximum = (*Row)["Maximum"].value_or(1.);
                Variable.Bounded = true;
            }
            if (const auto *Exposure = (*Row)["Exposure"].as_table())
            {
                MaterialAuthoringExposure Record;
                Record.Name = RequiredText(*Exposure, "Name");
                if (const auto Minimum = (*Exposure)["Minimum"].value<double>())
                {
                    Record.Minimum = *Minimum;
                    Record.Maximum = (*Exposure)["Maximum"].value_or(Variable.Maximum);
                    Record.Bounded = true;
                }
                Variable.Exposure = std::move(Record);
            }
            Document.Variables.push_back(std::move(Variable));
        }
    if (const auto *Links = Table["Links"].as_table())
        for (const auto &[Address, Reference] : *Links)
        {
            const auto Identity = Reference.value<std::string>();
            if (!Identity) throw std::runtime_error("Authoring link must name a variable identity.");
            Document.Links[std::string(Address.str())] = *Identity;
        }
    const auto DecodeStore = [&Table](std::string_view Key) {
        MaterialAuthoringStore Store;
        if (const auto *Entries = Table[Key].as_table())
            for (const auto &[Address, Quantity] : *Entries) Store[std::string(Address.str())] = ReadQuantity(Quantity);
        return Store;
    };
    Document.Inputs = DecodeStore("Inputs");
    Document.Designer = DecodeStore("Designer");
    if (const auto *Sources = Table["Sources"].as_array())
        for (const auto &Entry : *Sources)
        {
            const auto *Row = Entry.as_table();
            if (!Row) throw std::runtime_error("Authoring source must be a table.");
            MaterialAuthoringSource Source;
            Source.Mode = RequiredText(*Row, "Mode");
            if (const auto Direct = (*Row)["Direct"].value<bool>()) Source.Direct = *Direct;
            Source.Generator = (*Row)["Generator"].value_or(std::string{});
            Source.Stops = DecodeTexts((*Row)["Stops"]);
            if (const auto *Texture = (*Row)["Texture"].as_table())
            {
                MaterialAuthoringTexture Record;
                Record.Name = RequiredText(*Texture, "Name");
                Record.DataUrl = RequiredText(*Texture, "DataUrl");
                Record.Origin = (*Texture)["Origin"].value_or(std::string{});
                Record.Width = uint32_t((*Texture)["Width"].value_or(int64_t(0)));
                Record.Height = uint32_t((*Texture)["Height"].value_or(int64_t(0)));
                Source.Texture = std::move(Record);
            }
            Document.Sources[RequiredText(*Row, "Address")] = std::move(Source);
        }
    NormalizeMaterialAuthoring(Document);
    return Document;
}
} // namespace Frontier
