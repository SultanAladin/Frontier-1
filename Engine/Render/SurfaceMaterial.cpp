/*==========================================================================================================================================
                                                          SURFACEMATERIAL.CPP
==========================================================================================================================================*/
// 🧩 Strict TOML material authoring, contained PNG/JPEG ingestion and colour-correct/normal-aware mip compilation.

#include "SurfaceMaterial.h"
#include "MaterialProceduralBaking.h"
#include "Engine/Diagnostics/DiagnosticJournal.h"
#include <toml++/toml.hpp>
#include <cctype>
#include <fstream>
#include <sstream>
#include <memory>
// 📝 The PNG/JPEG implementation lives in StbImageImplementation.cpp so every static-library member shares one.
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-but-set-variable"
#endif
#include <stb_image.h>
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

namespace Frontier
{
namespace
{
void ValidateKeys(const toml::table &Table,std::initializer_list<std::string_view> Allowed)
{
    for (const auto &[Key,Content] : Table)
    {
        (void)Content;
        if (std::find(Allowed.begin(),Allowed.end(),Key.str()) == Allowed.end())
            throw std::runtime_error("Unsupported material field: "+std::string(Key.str()));
    }
}

template<size_t Count>
std::array<float,Count> DecodeSpectrum(const toml::node_view<toml::node> &Field,std::array<float,Count> Fallback)
{
    if (!Field) return Fallback;
    const auto *Channels = Field.as_array();
    if (!Channels || Channels->size() != Count) throw std::runtime_error("Material vector has the wrong channel count.");
    for (size_t Index = 0; Index < Count; ++Index)
    {
        const auto Coefficient = Channels->get(Index)->value<double>();
        if (!Coefficient || !std::isfinite(*Coefficient) || std::abs(*Coefficient) > 1e6)
            throw std::runtime_error("Material vector channels must be finite bounded numbers.");
        Fallback[Index] = float(*Coefficient);
    }
    return Fallback;
}

float DecodeCoefficient(const toml::node_view<toml::node> &Field,float Fallback)
{
    if (!Field) return Fallback;
    const auto Coefficient = Field.value<double>();
    if (!Coefficient || !std::isfinite(*Coefficient) || std::abs(*Coefficient) > 1e6)
        throw std::runtime_error("Material coefficient must be a finite bounded number.");
    return float(*Coefficient);
}

template<size_t Count>
toml::array EncodeSpectrum(const std::array<float,Count> &Channels)
{
    toml::array Result;
    for (const auto Channel : Channels) Result.push_back(double(Channel));
    return Result;
}

double DecodeSrgb(double Encoded)
{
    return Encoded <= .04045 ? Encoded/12.92 : std::pow((Encoded+.055)/1.055,2.4);
}
double EncodeSrgb(double Linear)
{
    return Linear <= .0031308 ? Linear*12.92 : 1.055*std::pow(Linear,1/2.4)-.055;
}
} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                     STRICT AUTHORED TOML
//------------------------------------------------------------------------------------------------------------------------

MaterialDefinition DecodeSurfaceMaterial(std::string_view Text)
{
    if (Text.size() > 1048576) throw std::runtime_error("Material TOML exceeds 1 MiB.");
    auto Table = toml::parse(Text);
    ValidateKeys(Table,{"Format","Name","UvScaleOffset","Radiance","NormalStrength","Textures","Strata","AutomotivePaint","Authoring","DielectricVolume"});
    if (Table["Format"].value<int64_t>() != 1) throw std::runtime_error("Unsupported material format; expected Format = 1.");
    MaterialDefinition Material;
    const auto Name = Table["Name"].value<std::string>();
    if (!Name || Name->empty() || Name->size() > 128) throw std::runtime_error("Material Name must contain 1..128 bytes.");
    Material.Name = *Name;
    if (Table.contains("DielectricVolume"))
    {
        auto *Volume = Table["DielectricVolume"].as_table();
        if (!Volume) throw std::runtime_error("DielectricVolume must be a table.");
        ValidateKeys(*Volume,{"Ior","Absorption"});
        if (!Volume->contains("Ior") || !Volume->contains("Absorption"))
            throw std::runtime_error("DielectricVolume requires Ior and RGB Absorption in inverse metres.");
        Material.ClosedVolume = DielectricVolume{DecodeCoefficient((*Volume)["Ior"],1.5f),
                                               DecodeSpectrum<3>((*Volume)["Absorption"],{})};
        if (!ValidateDielectricVolume(*Material.ClosedVolume))
            throw std::runtime_error("DielectricVolume requires Ior 1..3 and finite RGB Absorption 0..100.");
    }
    Material.Scattering.UvScaleOffset = DecodeSpectrum(Table["UvScaleOffset"],Material.Scattering.UvScaleOffset);
    const auto Radiance = DecodeSpectrum<3>(Table["Radiance"],{0,0,0});
    std::copy(Radiance.begin(),Radiance.end(),Material.Scattering.RadianceNormal.begin());
    Material.Scattering.RadianceNormal[3] = DecodeCoefficient(Table["NormalStrength"],1);
    if (Table.contains("Textures"))
    {
        const auto *Textures = Table["Textures"].as_table();
        if (!Textures) throw std::runtime_error("Textures must be a table.");
        ValidateKeys(*Textures,{"Albedo","Orm","Normal","Radiance"});
        const std::array<const char *,4> Names{"Albedo","Orm","Normal","Radiance"};
        for (size_t Index = 0; Index < Names.size(); ++Index)
            if (Textures->contains(Names[Index]))
            {
                const auto Source = (*Textures)[Names[Index]].value<std::string>();
                if (!Source || Source->empty() || Source->size() > 512) throw std::runtime_error("Invalid material texture path.");
                Material.TextureSources[Index] = *Source;
            }
    }
    if (Table.contains("AutomotivePaint"))
    {
        auto *PaintTable = Table["AutomotivePaint"].as_table();
        if (!PaintTable) throw std::runtime_error("AutomotivePaint must be a table.");
        ValidateKeys(*PaintTable, {"Enabled", "Layers", "ColourMode", "Distribution", "Finish", "Relief", "Palette"});
        auto &Paint = Material.Scattering.Paint;
        const auto Enabled = (*PaintTable)["Enabled"].value<bool>();
        const auto Layers = (*PaintTable)["Layers"].value<int64_t>();
        const auto Mode = (*PaintTable)["ColourMode"].value<std::string>();
        if (!Enabled || !Layers || *Layers < 1 || *Layers > 4 || !Mode ||
            (*Mode != "Palette" && *Mode != "Ramp" && *Mode != "Single"))
            throw std::runtime_error("Paint requires Enabled, Layers (1..4) and ColourMode (Palette, Ramp or Single).");
        Paint.Configuration[0] = *Enabled ? 1 : 0;
        Paint.Configuration[1] = uint32_t(*Layers);
        Paint.Configuration[2] = *Mode == "Single" ? 2 : *Mode == "Ramp" ? 1 : 0;
        Paint.Distribution = DecodeSpectrum<4>((*PaintTable)["Distribution"], Paint.Distribution);
        Paint.Finish = DecodeSpectrum<4>((*PaintTable)["Finish"], Paint.Finish);
        Paint.Relief = DecodeSpectrum<4>((*PaintTable)["Relief"], Paint.Relief);
        auto *Palette = (*PaintTable)["Palette"].as_array();
        if (!Palette || Palette->empty() || Palette->size() > 12) throw std::runtime_error("Paint requires 1..12 linear RGBA palette stops.");
        Paint.Configuration[3] = uint32_t(Palette->size());
        for (size_t Index = 0; Index < Palette->size(); ++Index)
            Paint.Palette[Index] = DecodeSpectrum<4>(toml::node_view<toml::node>(Palette->get(Index)), {});
    }
    auto *Strata = Table["Strata"].as_array();
    if (!Strata || Strata->empty() || Strata->size() > 8) throw std::runtime_error("Material requires 1..8 ordered Strata.");
    Material.Scattering.Illumination.Configuration[0] = uint32_t(Strata->size());
    for (size_t Index = 0; Index < Strata->size(); ++Index)
    {
        auto *Stratum = Strata->get(Index)->as_table();
        if (!Stratum) throw std::runtime_error("Stratum must be a table.");
        ValidateKeys(*Stratum,{"Category","Albedo","Roughness","Metallic","Ior","Coverage","Absorption","Thickness","TextureMask","Textures","Channels","UvScaleOffset","NormalStrength","Anisotropy","Rotation","Fresnel","Specular","HeightStrength","SheenColor","SheenIntensity","SheenRoughness"});
        const auto Category = (*Stratum)["Category"].value<std::string>();
        if (!Category || (*Category != "Opaque" && *Category != "Coating"))
            throw std::runtime_error("Unsupported scattering Category; supported: Opaque, Coating. Transmission/SSS are not implemented.");
        auto &Compiled = Material.Scattering.Illumination.Strata[Index];
        Compiled.Classification[0] = *Category == "Coating" ? 1 : 0;
        const auto Albedo = DecodeSpectrum<3>((*Stratum)["Albedo"],{.5f,.5f,.5f});
        std::copy(Albedo.begin(),Albedo.end(),Compiled.AlbedoRoughness.begin());
        Compiled.AlbedoRoughness[3] = DecodeCoefficient((*Stratum)["Roughness"],.4f);
        Compiled.Optics = {DecodeCoefficient((*Stratum)["Metallic"],0),DecodeCoefficient((*Stratum)["Ior"],1.5f),
                           DecodeCoefficient((*Stratum)["Coverage"],1),0};
        const auto Absorption = DecodeSpectrum<3>((*Stratum)["Absorption"],{0,0,0});
        std::copy(Absorption.begin(),Absorption.end(),Compiled.AbsorptionThickness.begin());
        Compiled.AbsorptionThickness[3] = DecodeCoefficient((*Stratum)["Thickness"],0);
        // 📝 Fabric sheen (fuzz rim): zero by default, so every non-fabric material stays bit-identical.
        const auto SheenColour = DecodeSpectrum<3>((*Stratum)["SheenColor"],{0,0,0});
        std::copy(SheenColour.begin(),SheenColour.end(),Compiled.Sheen.begin());
        Compiled.Sheen[3] = DecodeCoefficient((*Stratum)["SheenIntensity"],0);
        Compiled.SheenFinish[0] = DecodeCoefficient((*Stratum)["SheenRoughness"],0);
        const auto Mask = (*Stratum)["TextureMask"].value<int64_t>();
        if ((*Stratum).contains("TextureMask") && (!Mask || *Mask < 0 || *Mask > 15))
            throw std::runtime_error("TextureMask must be an integer within 0..15.");
        Material.Scattering.TextureUse[Index][0] = uint32_t(Mask.value_or(0));
        Material.Scattering.StratumUv[Index] = DecodeSpectrum<4>((*Stratum)["UvScaleOffset"],{1,1,0,0});
        Material.Scattering.StratumNormal[Index][0] = DecodeCoefficient((*Stratum)["NormalStrength"],1);
        Material.Scattering.StratumNormal[Index][1] = DecodeCoefficient((*Stratum)["HeightStrength"],1);
        auto &Directional = Material.Scattering.DirectionalOptics[Index];
        Directional[0] = DecodeCoefficient((*Stratum)["Anisotropy"],0);
        Directional[1] = DecodeCoefficient((*Stratum)["Rotation"],0);
        Directional[3] = DecodeCoefficient((*Stratum)["Specular"],.5f);
        if ((*Stratum).contains("Fresnel"))
        {
            const auto Fresnel = (*Stratum)["Fresnel"].value<std::string>();
            if (!Fresnel || (*Fresnel != "Schlick" && *Fresnel != "Dielectric"))
                throw std::runtime_error("Fresnel must be Schlick or Dielectric; exact conductor optical constants are not implemented.");
            Directional[2] = *Fresnel == "Dielectric" ? 1.f : 0.f;
        }
        if ((*Stratum).contains("Textures"))
        {
            const auto *Textures = (*Stratum)["Textures"].as_table();
            if (!Textures) throw std::runtime_error("Stratum Textures must be a table.");
            ValidateKeys(*Textures,{"Albedo","Orm","Normal","Radiance"});
            const std::array<const char *,4> Names{"Albedo","Orm","Normal","Radiance"};
            for (size_t Slot = 0; Slot < 4; ++Slot)
                if (Textures->contains(Names[Slot]))
                {
                    const auto Source = (*Textures)[Names[Slot]].value<std::string>();
                    if (!Source || Source->empty() || Source->size() > 512) throw std::runtime_error("Invalid stratum texture path.");
                    Material.StratumTextureSources[Index][Slot] = *Source;
                }
        }
        if ((*Stratum).contains("Channels"))
        {
            const auto *Channels = (*Stratum)["Channels"].as_array();
            if (!Channels || Channels->empty() || Channels->size() > 4) throw std::runtime_error("Stratum Channels must hold 1..4 entries.");
            for (const auto &ChannelTerm : *Channels)
            {
                const auto *Channel = ChannelTerm.as_table();
                if (!Channel) throw std::runtime_error("Stratum channel must be a table.");
                ValidateKeys(*Channel,{"Name","Op","Link","Params"});
                MaterialProceduralChannel Entry;
                const auto Name = (*Channel)["Name"].value<std::string>();
                if (!Name || (*Name != "Albedo" && *Name != "Orm" && *Name != "Normal" && *Name != "Radiance"))
                    throw std::runtime_error("Stratum channel Name must be Albedo, Orm, Normal or Radiance.");
                Entry.Name = *Name;
                for (const auto &Prior : Material.StratumChannels[Index])
                    if (Prior.Name == Entry.Name) throw std::runtime_error("Stratum channel Name is duplicated.");
                Entry.Op = (*Channel)["Op"].value_or(std::string{});
                if (Entry.Op.size() > 32) throw std::runtime_error("Stratum channel Op is unbounded.");
                Entry.Link = (*Channel)["Link"].value_or(std::string{});
                if (Entry.Link.empty()) Entry.Link = "shared";
                if (Entry.Link.size() > 32) throw std::runtime_error("Stratum channel Link is unbounded.");
                if (const auto *Params = (*Channel)["Params"].as_table())
                {
                    if (Params->size() > 64) throw std::runtime_error("Stratum channel Params are unbounded.");
                    for (const auto &[Key, Value] : *Params)
                    {
                        const std::string Param(Key.str());
                        if (Param.empty() || Param.size() > 64) throw std::runtime_error("Stratum channel param name is unbounded.");
                        if (Value.is_number() || Value.is_boolean())
                        {
                            const auto Scalar = Value.value<double>();
                            if (Scalar) Entry.Params[Param] = *Scalar;
                            else if (const auto Flag = Value.value<bool>()) Entry.Params[Param] = *Flag ? 1 : 0;
                            else throw std::runtime_error("Stratum channel param is not a scalar.");
                        }
                        else if (const auto Text = Value.value<std::string>()) Entry.Texts[Param] = *Text;
                        else throw std::runtime_error("Stratum channel param must be a scalar or text.");
                    }
                }
                else if ((*Channel).contains("Params")) throw std::runtime_error("Stratum channel Params must be a table.");
                Material.StratumChannels[Index].push_back(std::move(Entry));
            }
            for (const auto &Entry : Material.StratumChannels[Index])
            {
                if (Entry.Op.empty()) continue;
                const size_t Slot =
                    Entry.Name == "Albedo" ? 0 : Entry.Name == "Orm" ? 1 : Entry.Name == "Normal" ? 2 : 3;
                if (Material.StratumTextureSources[Index][Slot].empty())
                    Material.StratumTextureSources[Index][Slot] = EncodeProceduralSource(Index, Entry.Name);
                // 📝 Channel ops sample their baked maps: derive the mask bit so shipped procedurals
                // without an authored TextureMask still reach the shader (idempotent under round trip).
                Material.Scattering.TextureUse[Index][0] |= uint32_t(1u << Slot);
            }
        }
    }
    if (Table.contains("Authoring"))
    {
        const auto *Authoring = Table["Authoring"].as_table();
        if (!Authoring) throw std::runtime_error("Authoring must be a table.");
        Material.Authoring = DecodeMaterialAuthoring(*Authoring);
    }
    if (!ValidateMaterialScattering(Material.Scattering)) throw std::runtime_error("Material coefficients violate scattering limits.");
    return Material;
}

std::string EncodeProceduralSource(size_t Stratum, std::string_view Channel)
{
    std::string Key = "procedural/" + std::to_string(Stratum) + "/";
    for (char Byte : Channel) Key += char(std::tolower((unsigned char)Byte));
    return Key;
}

std::optional<MaterialPoolLocation> QueryProceduralPool(const MaterialDefinition &Material, std::string_view Key)
{
    const std::string Name(Key);
    for (size_t Index = 0; Index < Material.StratumChannels.size(); ++Index)
        for (const auto &Entry : Material.StratumChannels[Index])
        {
            if (Entry.Op.empty()) continue;
            const auto Value = Entry.Params.find(Name);
            if (Value != Entry.Params.end()) return MaterialPoolLocation{uint32_t(Index), Entry.Name, Value->second};
        }
    return std::nullopt;
}

std::string EncodeSurfaceMaterial(const MaterialDefinition &Material)
{
    if ((Material.ClosedVolume && !ValidateDielectricVolume(*Material.ClosedVolume)) ||
        !ValidateMaterialScattering(Material.Scattering) || Material.Name.empty() || Material.Name.size() > 128)
        throw std::runtime_error("Cannot encode invalid material.");
    toml::table Table{{"Format",1},{"Name",Material.Name},{"UvScaleOffset",EncodeSpectrum(Material.Scattering.UvScaleOffset)},
        {"Radiance",EncodeSpectrum<3>({Material.Scattering.RadianceNormal[0],Material.Scattering.RadianceNormal[1],Material.Scattering.RadianceNormal[2]})},
        {"NormalStrength",double(Material.Scattering.RadianceNormal[3])}};
    if (Material.ClosedVolume)
        Table.insert("DielectricVolume",toml::table{{"Ior",double(Material.ClosedVolume->Ior)},
            {"Absorption",EncodeSpectrum(Material.ClosedVolume->Absorption)}});
    const auto &Paint = Material.Scattering.Paint;
    if (Paint.Configuration[3])
    {
        toml::array Palette;
        for (size_t Index = 0; Index < Paint.Configuration[3]; ++Index) Palette.push_back(EncodeSpectrum(Paint.Palette[Index]));
        Table.insert("AutomotivePaint", toml::table{{"Enabled", bool(Paint.Configuration[0])},
            {"Layers", int64_t(Paint.Configuration[1])}, {"ColourMode", Paint.Configuration[2] == 2 ? "Single" : Paint.Configuration[2] == 1 ? "Ramp" : "Palette"},
            {"Distribution", EncodeSpectrum(Paint.Distribution)}, {"Finish", EncodeSpectrum(Paint.Finish)},
            {"Relief", EncodeSpectrum(Paint.Relief)}, {"Palette", std::move(Palette)}});
    }
    toml::table Textures;
    const std::array<const char *,4> Names{"Albedo","Orm","Normal","Radiance"};
    for (size_t Index = 0; Index < 4; ++Index)
        if (!Material.TextureSources[Index].empty()) Textures.insert(Names[Index],Material.TextureSources[Index]);
    Table.insert("Textures",std::move(Textures));
    toml::array Strata;
    for (size_t Index = 0; Index < Material.Scattering.Illumination.Configuration[0]; ++Index)
    {
        const auto &Stratum = Material.Scattering.Illumination.Strata[Index];
        Strata.push_back(toml::table{{"Category",Stratum.Classification[0] ? "Coating" : "Opaque"},
            {"Albedo",EncodeSpectrum<3>({Stratum.AlbedoRoughness[0],Stratum.AlbedoRoughness[1],Stratum.AlbedoRoughness[2]})},
            {"Roughness",double(Stratum.AlbedoRoughness[3])},{"Metallic",double(Stratum.Optics[0])},
            {"Ior",double(Stratum.Optics[1])},{"Coverage",double(Stratum.Optics[2])},
            {"Absorption",EncodeSpectrum<3>({Stratum.AbsorptionThickness[0],Stratum.AbsorptionThickness[1],Stratum.AbsorptionThickness[2]})},
            {"Thickness",double(Stratum.AbsorptionThickness[3])},{"TextureMask",int64_t(Material.Scattering.TextureUse[Index][0])},
            {"SheenColor",EncodeSpectrum<3>({Stratum.Sheen[0],Stratum.Sheen[1],Stratum.Sheen[2]})},
            {"SheenIntensity",double(Stratum.Sheen[3])},{"SheenRoughness",double(Stratum.SheenFinish[0])}});
    }
    for (size_t Index = 0; Index < Material.Scattering.Illumination.Configuration[0]; ++Index)
    {
        auto &Stratum = *Strata.get(Index)->as_table();
        Stratum.insert("UvScaleOffset",EncodeSpectrum(Material.Scattering.StratumUv[Index]));
        Stratum.insert("NormalStrength",double(Material.Scattering.StratumNormal[Index][0]));
        Stratum.insert("Anisotropy",double(Material.Scattering.DirectionalOptics[Index][0]));
        Stratum.insert("Rotation",double(Material.Scattering.DirectionalOptics[Index][1]));
        Stratum.insert("Specular",double(Material.Scattering.DirectionalOptics[Index][3]));
        Stratum.insert("HeightStrength",double(Material.Scattering.StratumNormal[Index][1]));
        Stratum.insert("Fresnel",Material.Scattering.DirectionalOptics[Index][2] == 1 ? "Dielectric" : "Schlick");
        toml::table Textures;
        const std::array<const char *,4> Names{"Albedo","Orm","Normal","Radiance"};
        for (size_t Slot = 0; Slot < 4; ++Slot)
            if (!Material.StratumTextureSources[Index][Slot].empty() &&
                !Material.StratumTextureSources[Index][Slot].starts_with("procedural/"))
                Textures.insert(Names[Slot],Material.StratumTextureSources[Index][Slot]);
        if (!Textures.empty()) Stratum.insert("Textures",std::move(Textures));
        toml::array Channels;
        for (const auto &Entry : Material.StratumChannels[Index])
        {
            toml::table Params;
            for (const auto &[Key, Value] : Entry.Params) Params.insert(Key,Value);
            for (const auto &[Key, Value] : Entry.Texts) Params.insert(Key,Value);
            Channels.push_back(toml::table{{"Name",Entry.Name},{"Op",Entry.Op},{"Link",Entry.Link},{"Params",std::move(Params)}});
        }
        if (!Channels.empty()) Stratum.insert("Channels",std::move(Channels));
    }
    Table.insert("Strata",std::move(Strata));
    if (Material.Authoring) Table.insert("Authoring",EncodeMaterialAuthoring(*Material.Authoring));
    std::ostringstream Output;
    Output << toml::toml_formatter{Table};
    return Output.str();
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     COLOUR-CORRECT TEXTURE PYRAMIDS
//------------------------------------------------------------------------------------------------------------------------

MaterialTexturePyramid ConstructFloatMaterialPyramid(uint32_t Width, uint32_t Height,
    MaterialTextureCategory Category, std::span<const float> FloatRgba)
{
    if (uint32_t(Category) > 3 || !Width || !Height || Width > 4096 || Height > 4096 ||
        FloatRgba.size() != size_t(Width) * Height * 4 ||
        !std::all_of(FloatRgba.begin(), FloatRgba.end(), [](float Sample) { return std::isfinite(Sample); }))
        throw std::runtime_error("Invalid Float32 material texture.");
    MaterialTexturePyramid Pyramid;
    Pyramid.Category = Category;
    Pyramid.Levels.push_back({Width, Height, 0});
    Pyramid.FloatRgba.assign(FloatRgba.begin(), FloatRgba.end());
    while (Width > 1 || Height > 1)
    {
        const auto Previous = Pyramid.Levels.back();
        const uint32_t Columns = std::max(1u, Width / 2), Rows = std::max(1u, Height / 2);
        const size_t Start = Pyramid.FloatRgba.size();
        Pyramid.Levels.push_back({Columns, Rows, Start * sizeof(float)});
        Pyramid.FloatRgba.resize(Start + size_t(Columns) * Rows * 4);
        if (Width == Columns * 2 && Height == Rows * 2)
        {
            // 📝 Exact-halving float box: unit area weights make this mathematically identical to the
            // generic integral, so Float32 authoring mip chains build an order of magnitude faster with
            // only float-rounding divergence (far inside golden tolerances).
            const float *Source = Pyramid.FloatRgba.data() + Previous.Offset / sizeof(float);
            float *Target = Pyramid.FloatRgba.data() + Start;
            const bool Renormalize = Category == MaterialTextureCategory::TangentNormal;
            for (uint32_t Row = 0; Row < Rows; ++Row)
                for (uint32_t Column = 0; Column < Columns; ++Column)
                {
                    const size_t Base = (size_t(Row) * 2 * Width + size_t(Column) * 2) * 4;
                    float Spectrum[4]{};
                    for (size_t Tap = 0; Tap < 4; ++Tap)
                    {
                        const size_t Pixel = Base + (Tap / 2) * size_t(Width) * 4 + (Tap % 2) * 4;
                        for (size_t Channel = 0; Channel < 4; ++Channel)
                        {
                            float Coefficient = Source[Pixel + Channel];
                            if (Renormalize && Channel < 3) Coefficient = Coefficient * 2 - 1;
                            Spectrum[Channel] += Coefficient;
                        }
                    }
                    for (size_t Channel = 0; Channel < 4; ++Channel) Spectrum[Channel] *= .25f;
                    if (Renormalize)
                    {
                        const float Length = std::sqrt(Spectrum[0] * Spectrum[0] + Spectrum[1] * Spectrum[1] +
                            Spectrum[2] * Spectrum[2]);
                        for (size_t Channel = 0; Channel < 3; ++Channel)
                            Spectrum[Channel] = (Length > 1e-8f ? Spectrum[Channel] / Length : float(Channel == 2)) * .5f + .5f;
                    }
                    float *Pixel = Target + (size_t(Row) * Columns + Column) * 4;
                    for (size_t Channel = 0; Channel < 4; ++Channel) Pixel[Channel] = Spectrum[Channel];
                }
            Width = Columns;
            Height = Rows;
            continue;
        }
        for (uint32_t Row = 0; Row < Rows; ++Row)
            for (uint32_t Column = 0; Column < Columns; ++Column)
            {
                std::array<double, 4> Spectrum{};
                double Total = 0;
                const double Left = double(Column) * Width / Columns, Right = double(Column + 1) * Width / Columns;
                const double Top = double(Row) * Height / Rows, Bottom = double(Row + 1) * Height / Rows;
                for (uint32_t Vertical = uint32_t(Top); Vertical < uint32_t(std::ceil(Bottom)); ++Vertical)
                    for (uint32_t Horizontal = uint32_t(Left); Horizontal < uint32_t(std::ceil(Right)); ++Horizontal)
                    {
                        const double Weight = (std::min(Right, double(Horizontal + 1)) - std::max(Left, double(Horizontal))) *
                                              (std::min(Bottom, double(Vertical + 1)) - std::max(Top, double(Vertical)));
                        const size_t Pixel = Previous.Offset / sizeof(float) + (size_t(Vertical) * Width + Horizontal) * 4;
                        for (size_t Channel = 0; Channel < 4; ++Channel)
                        {
                            double Coefficient = Pyramid.FloatRgba[Pixel + Channel];
                            if (Channel < 3 && Category == MaterialTextureCategory::TangentNormal) Coefficient = Coefficient * 2 - 1;
                            Spectrum[Channel] += Coefficient * Weight;
                        }
                        Total += Weight;
                    }
                for (auto &Channel : Spectrum) Channel /= Total;
                if (Category == MaterialTextureCategory::TangentNormal)
                {
                    const double Length = std::sqrt(Spectrum[0] * Spectrum[0] + Spectrum[1] * Spectrum[1] + Spectrum[2] * Spectrum[2]);
                    for (size_t Channel = 0; Channel < 3; ++Channel)
                        Spectrum[Channel] = (Length > 1e-8 ? Spectrum[Channel] / Length : double(Channel == 2)) * .5 + .5;
                }
                const size_t Pixel = Start + (size_t(Row) * Columns + Column) * 4;
                for (size_t Channel = 0; Channel < 4; ++Channel)
                    Pyramid.FloatRgba[Pixel + Channel] = float(Spectrum[Channel]);
            }
        Width = Columns;
        Height = Rows;
    }
    if (!ValidateMaterialPyramid(Pyramid)) throw std::runtime_error("Float32 material mip chain failed validation.");
    return Pyramid;
}

std::array<MaterialTexturePyramid,6> ConstructAuthoringMaterialMaps(uint32_t Width,uint32_t Height,
    const std::array<std::span<const float>,6> &FloatRgba)
{
    constexpr std::array<MaterialTextureCategory,6> Categories{
        MaterialTextureCategory::Albedo,MaterialTextureCategory::TangentNormal,
        MaterialTextureCategory::OcclusionRoughnessMetallic,MaterialTextureCategory::OcclusionRoughnessMetallic,
        MaterialTextureCategory::OcclusionRoughnessMetallic,MaterialTextureCategory::OcclusionRoughnessMetallic};
    std::array<MaterialTexturePyramid,6> Pyramids;
    for (size_t Index = 0; Index < Pyramids.size(); ++Index)
        Pyramids[Index] = ConstructFloatMaterialPyramid(Width,Height,Categories[Index],FloatRgba[Index]);
    return Pyramids;
}

MaterialTexturePyramid ConstructMaterialPyramid(const SurfaceTextureImage &Image,MaterialTextureCategory Category)
{
    if (!ValidateSurfaceTextureImage(Image) || uint32_t(Category) > 3) throw std::runtime_error("Invalid material texture image.");
    MaterialTexturePyramid Pyramid{Category,Image.Rgba,{{Image.Width,Image.Height,0}}};
    uint32_t Width = Image.Width, Height = Image.Height;
    while (Width > 1 || Height > 1)
    {
        const auto Previous = Pyramid.Levels.back();
        const uint32_t Columns = std::max(1u,Width/2), Rows = std::max(1u,Height/2);
        const size_t Start = Pyramid.Rgba.size();
        Pyramid.Levels.push_back({Columns,Rows,Start});
        Pyramid.Rgba.resize(Start+size_t(Columns)*Rows*4);
        for (uint32_t Row = 0; Row < Rows; ++Row)
            for (uint32_t Column = 0; Column < Columns; ++Column)
            {
                std::array<double,4> Spectrum{};
                double Total = 0;
                const double Left = double(Column)*Width/Columns, Right = double(Column+1)*Width/Columns;
                const double Top = double(Row)*Height/Rows, Bottom = double(Row+1)*Height/Rows;
                for (uint32_t Vertical = uint32_t(Top); Vertical < uint32_t(std::ceil(Bottom)); ++Vertical)
                    for (uint32_t Horizontal = uint32_t(Left); Horizontal < uint32_t(std::ceil(Right)); ++Horizontal)
                    {
                        const double Weight = (std::min(Right,double(Horizontal+1))-std::max(Left,double(Horizontal)))*
                                              (std::min(Bottom,double(Vertical+1))-std::max(Top,double(Vertical)));
                        for (size_t Channel = 0; Channel < 4; ++Channel)
                        {
                            double Coefficient = Pyramid.Rgba[Previous.Offset+(size_t(Vertical)*Width+Horizontal)*4+Channel]/255.0;
                            if (Channel < 3 && (Category == MaterialTextureCategory::Albedo || Category == MaterialTextureCategory::Radiance))
                                Coefficient = DecodeSrgb(Coefficient);
                            if (Channel < 3 && Category == MaterialTextureCategory::TangentNormal) Coefficient = Coefficient*2-1;
                            Spectrum[Channel] += Coefficient*Weight;
                        }
                        Total += Weight;
                    }
                for (auto &Channel : Spectrum) Channel /= Total;
                if (Category == MaterialTextureCategory::TangentNormal)
                {
                    const double Length = std::sqrt(Spectrum[0]*Spectrum[0]+Spectrum[1]*Spectrum[1]+Spectrum[2]*Spectrum[2]);
                    for (size_t Channel = 0; Channel < 3; ++Channel)
                        Spectrum[Channel] = (Length > 1e-8 ? Spectrum[Channel]/Length : double(Channel == 2))*.5+.5;
                }
                for (size_t Channel = 0; Channel < 4; ++Channel)
                {
                    double Coefficient = Spectrum[Channel];
                    if (Channel < 3 && (Category == MaterialTextureCategory::Albedo || Category == MaterialTextureCategory::Radiance))
                        Coefficient = EncodeSrgb(Coefficient);
                    Pyramid.Rgba[Start+(size_t(Row)*Columns+Column)*4+Channel] = uint8_t(std::lround(std::clamp(Coefficient,0.0,1.0)*255));
                }
            }
        Width = Columns;
        Height = Rows;
    }
    return Pyramid;
}

void EnsureProceduralChannelBakes(MaterialDefinition &Material, uint32_t Extent)
{
    if (!Extent || Extent > 1024) return;
    const size_t Count = Material.Scattering.Illumination.Configuration[0];
    for (size_t Index = 0; Index < Count && Index < 8; ++Index)
        for (const auto &Entry : Material.StratumChannels[Index])
        {
            if (Entry.Op.empty()) continue;
            const size_t Slot =
                Entry.Name == "Albedo" ? 0 : Entry.Name == "Orm" ? 1 : Entry.Name == "Normal" ? 2 : 3;
            const std::string Derived = EncodeProceduralSource(Index, Entry.Name);
            const std::string &Live = Material.StratumTextureSources[Index][Slot].empty() ?
                Material.TextureSources[Slot] : Material.StratumTextureSources[Index][Slot];
            const bool LiveIsProcedural = Live.empty() || Live == Derived || Live.rfind("procedural/", 0) == 0;
            if (!LiveIsProcedural) continue; // File-backed: compile loads full resolution from disk; a proxy must not shadow it.
            const bool DerivedReady = Material.ProceduralMaps.find({Slot, Derived}) != Material.ProceduralMaps.end();
            const bool LiveReady = Live == Derived ||
                Material.ProceduralMaps.find({Slot, Live}) != Material.ProceduralMaps.end();
            if (DerivedReady && LiveReady) continue;
            try
            {
                auto Rgba = BakeProceduralChannelRgba(Entry.Op, Entry.Name, Entry.Params, Entry.Texts, Extent);
                SurfaceTextureImage BakedImage{Extent, Extent, std::move(Rgba)};
                Material.ProceduralMaps[{Slot, Derived}] = BakedImage;
                if (!Live.empty() && Live != Derived) Material.ProceduralMaps[{Slot, Live}] = std::move(BakedImage);
            }
            catch (const std::exception &Failure)
            {
                DiagnosticJournal::Append("ProceduralBake", "Channel bake skipped (" + Entry.Op + "/" +
                    Entry.Name + "): " + Failure.what());
            }
        }
}

CompiledSurfaceMaterial CompileSurfaceMaterial(const MaterialDefinition &Material,const std::filesystem::path &ContentDirectory)
{
    if (!ValidateMaterialScattering(Material.Scattering)) throw std::runtime_error("Material scattering cannot be compiled.");
    if (Material.ClosedVolume && !ValidateDielectricVolume(*Material.ClosedVolume))
        throw std::runtime_error("Invalid dielectric volume cannot be compiled.");
    CompiledSurfaceMaterial Compiled{Material.Name,Material.Scattering,{},{},Material.ClosedVolume};
    auto &Paint = Compiled.Scattering.Paint;
    const std::array<float,4> NeutralColour{1,1,1,0}, NeutralNormal{.5f,.5f,1,0};
    const std::array<float,4> NeutralRoughness{1,0,0,0}, NeutralMetalness{0,0,0,0}, NeutralOcclusion{1,0,0,0};
    Compiled.AuthoringMaps = {
        ConstructFloatMaterialPyramid(1,1,MaterialTextureCategory::Albedo,NeutralColour),
        ConstructFloatMaterialPyramid(1,1,MaterialTextureCategory::TangentNormal,NeutralNormal),
        ConstructFloatMaterialPyramid(1,1,MaterialTextureCategory::OcclusionRoughnessMetallic,NeutralRoughness),
        ConstructFloatMaterialPyramid(1,1,MaterialTextureCategory::OcclusionRoughnessMetallic,NeutralRoughness),
        ConstructFloatMaterialPyramid(1,1,MaterialTextureCategory::OcclusionRoughnessMetallic,NeutralMetalness),
        ConstructFloatMaterialPyramid(1,1,MaterialTextureCategory::OcclusionRoughnessMetallic,NeutralOcclusion)};
    Paint.AverageTint = {};
    if (Paint.Configuration[0])
        for (uint32_t Sample = 0; Sample < 48; ++Sample)
        {
            const float Position = (float(Sample) + .5f) / 48;
            auto Spectrum = Paint.Palette[0];
            if (Paint.Configuration[2] == 0) Spectrum = Paint.Palette[std::min(Paint.Configuration[3] - 1, uint32_t(Position * Paint.Configuration[3]))];
            else if (Paint.Configuration[2] == 1)
                for (size_t Index = 1; Index < Paint.Configuration[3]; ++Index)
                {
                    const float Blend = std::clamp((Position - Paint.Palette[Index - 1][3]) /
                        std::max(.0001f, Paint.Palette[Index][3] - Paint.Palette[Index - 1][3]), 0.f, 1.f);
                    for (size_t Channel = 0; Channel < 3; ++Channel) Spectrum[Channel] = std::lerp(Spectrum[Channel], Paint.Palette[Index][Channel], Blend);
                }
            for (size_t Channel = 0; Channel < 3; ++Channel) Paint.AverageTint[Channel] += Spectrum[Channel] / 48;
        }
    const std::array<std::array<uint8_t,4>,4> Defaults{{{255,255,255,255},{255,255,255,255},{128,128,255,255},{255,255,255,255}}};
    const auto Directory = std::filesystem::weakly_canonical(ContentDirectory);
    const auto DecodeTexture = [&](const std::string &Source,size_t Slot)
    {
        SurfaceTextureImage Image{1,1,{Defaults[Slot].begin(),Defaults[Slot].end()}};
        const auto Override = Material.ProceduralMaps.find({Slot,Source});
        if (Override != Material.ProceduralMaps.end())
        {
            const auto &Memory = Override->second;
            if (!Memory.Width || !Memory.Height || Memory.Width > 4096 || Memory.Height > 4096 ||
                Memory.Rgba.size() != size_t(Memory.Width) * Memory.Height * 4)
                throw std::runtime_error("Procedural rebake produced an invalid map: "+Source);
            Image = Memory;
        }
        else if (!Source.empty() && Source.starts_with("procedural/"))
        {
            // 📝 A procedural file source without a rebake override means the bake never ran (or failed);
            // keep the neutral map instead of refusing the whole material.
            DiagnosticJournal::Append("ProceduralBake", "Unbaked procedural source falls back to neutral: " + Source);
        }
        else if (!Source.empty())
        {
            const std::filesystem::path Relative(Source);
            if (Relative.is_absolute() || Source.find(':') != std::string::npos || Source.find('\\') != std::string::npos)
                throw std::runtime_error("Material texture paths must be portable ContentDirectory-relative paths.");
            const auto Path = std::filesystem::weakly_canonical(Directory/Relative);
            const auto Within = Path.lexically_relative(Directory);
            if (Within.empty() || *Within.begin() == "..") throw std::runtime_error("Material texture escapes ContentDirectory.");
            std::ifstream Input(Path,std::ios::binary | std::ios::ate);
            const auto Bytes = Input.tellg();
            if (!Input || Bytes <= 0 || Bytes > 67108864) throw std::runtime_error("Missing/oversized material texture: "+Source);
            std::vector<uint8_t> Encoded(static_cast<size_t>(Bytes));
            Input.seekg(0);
            Input.read(reinterpret_cast<char *>(Encoded.data()),Bytes);
            int Width = 0, Height = 0, Channels = 0;
            if (!Input || !stbi_info_from_memory(Encoded.data(),int(Bytes),&Width,&Height,&Channels) ||
                Width < 1 || Height < 1 || Width > 4096 || Height > 4096)
                throw std::runtime_error("Unsupported/oversized PNG or JPEG material texture: "+Source);
            std::unique_ptr<stbi_uc,decltype(&stbi_image_free)> Pixels(stbi_load_from_memory(Encoded.data(),int(Bytes),&Width,&Height,&Channels,4),stbi_image_free);
            if (!Pixels) throw std::runtime_error("Material texture decode failed: "+Source);
            Image = {uint32_t(Width),uint32_t(Height),std::vector<uint8_t>(Pixels.get(),Pixels.get()+size_t(Width)*Height*4)};
        }
        return ConstructMaterialPyramid(Image,MaterialTextureCategory(Slot));
    };
    size_t TotalBytes = 0;
    size_t ResidentBytes = 0;
    constexpr size_t MaximumBytes = 268435456;
    for (size_t Slot = 0; Slot < 4; ++Slot)
    {
        std::vector<std::string> Sources;
        std::vector<MaterialTexturePyramid> Slices;
        const size_t Count = Slot == 3 ? 1 : Material.Scattering.Illumination.Configuration[0];
        for (size_t Index = 0; Index < Count; ++Index)
        {
            const auto &Source = Slot < 4 && !Material.StratumTextureSources[Index][Slot].empty() ?
                Material.StratumTextureSources[Index][Slot] : Material.TextureSources[Slot];
            auto Position = std::find(Sources.begin(),Sources.end(),Source);
            size_t Slice = size_t(Position-Sources.begin());
            if (Position == Sources.end())
            {
                auto Texture = DecodeTexture(Source,Slot);
                if (Texture.Rgba.size() > MaximumBytes-TotalBytes) throw std::runtime_error("Material texture compilation exceeds 256 MiB.");
                TotalBytes += Texture.Rgba.size();
                Sources.push_back(Source);
                Slices.push_back(std::move(Texture));
            }
            if (Slot < 3) Compiled.Scattering.TextureUse[Index][Slot+1] = uint32_t(Slice);
        }
        uint32_t Width = 1, Height = 1;
        for (const auto &Slice : Slices)
        {
            Width = std::max(Width,Slice.Levels[0].Width);
            Height = std::max(Height,Slice.Levels[0].Height);
        }
        for (const auto &Slice : Slices)
            if ((Slice.Levels[0].Width != Width || Slice.Levels[0].Height != Height) &&
                (Slice.Levels[0].Width != 1 || Slice.Levels[0].Height != 1))
                throw std::runtime_error("Stratum textures in each slot must share dimensions; neutral 1x1 images may expand.");
        MaterialTexturePyramid Combined;
        Combined.Category = MaterialTextureCategory(Slot);
        Combined.Layers = uint32_t(Slices.size());
        size_t RequiredBytes = 0;
        for (uint32_t Columns = Width, Rows = Height;; Columns = std::max(1u,Columns/2), Rows = std::max(1u,Rows/2))
        {
            RequiredBytes += size_t(Columns)*Rows*4*Slices.size();
            if (Columns == 1 && Rows == 1) break;
        }
        if (RequiredBytes > MaximumBytes-ResidentBytes)
            throw std::runtime_error("Compiled material texture arrays exceed 256 MiB.");
        Combined.Rgba.reserve(RequiredBytes);
        size_t Level = 0;
        while (true)
        {
            Combined.Levels.push_back({Width,Height,Combined.Rgba.size()});
            const size_t Bytes = size_t(Width)*Height*4;
            if (Combined.Rgba.size()+Bytes*Slices.size() > MaximumBytes)
                throw std::runtime_error("Material array texture exceeds 256 MiB.");
            for (const auto &Slice : Slices)
            {
                if (Slice.Levels[0].Width == 1 && Slice.Levels[0].Height == 1)
                    for (size_t Pixel = 0; Pixel < Bytes/4; ++Pixel)
                        Combined.Rgba.insert(Combined.Rgba.end(),Slice.Rgba.begin(),Slice.Rgba.begin()+4);
                else
                {
                    const auto Start = Slice.Rgba.begin()+Slice.Levels[Level].Offset;
                    Combined.Rgba.insert(Combined.Rgba.end(),Start,Start+Bytes);
                }
            }
            if (Width == 1 && Height == 1) break;
            Width = std::max(1u,Width/2);
            Height = std::max(1u,Height/2);
            ++Level;
        }
        if (Combined.Rgba.size() > MaximumBytes-ResidentBytes)
            throw std::runtime_error("Compiled material texture arrays exceed 256 MiB.");
        ResidentBytes += Combined.Rgba.size();
        Compiled.Textures[Slot] = std::move(Combined);
    }
    return Compiled;
}
} // namespace Frontier
