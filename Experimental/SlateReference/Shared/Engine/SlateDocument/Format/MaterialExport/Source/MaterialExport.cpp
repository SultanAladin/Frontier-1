//============================================================================================================================================
//                                                         MATERIALEXPORT.CPP
//============================================================================================================================================

#include "SlateDocument/Format/MaterialExport/Api/MaterialExport.h"

#include <algorithm>
#include <cstdio>
#include <utility>

namespace Slate
{
namespace
{
constexpr std::uint64_t HashSeed = 1469598103934665603ull;
constexpr std::uint64_t HashPrime = 1099511628211ull;

void HashBytes(std::uint64_t& Hash, const void* Data, std::size_t Span)
{
    const auto* Bytes = static_cast<const unsigned char*>(Data);
    for (std::size_t Index = 0u; Index < Span; ++Index)
    {
        Hash ^= Bytes[Index];
        Hash *= HashPrime;
    }
}

template <typename ValueType>
void HashValue(std::uint64_t& Hash, const ValueType& Value)
{
    HashBytes(Hash, &Value, sizeof(Value));
}

void HashString(std::uint64_t& Hash, const std::string& Text)
{
    const std::uint64_t Count = static_cast<std::uint64_t>(Text.size());
    HashValue(Hash, Count);
    if (!Text.empty()) HashBytes(Hash, Text.data(), Text.size());
}

std::uint32_t ChannelBit(ChannelSubject Channel)
{
    return Channel == ChannelSubject::ChannelCount ? 0u : (1u << static_cast<std::uint32_t>(Channel));
}

MaterialExportLaneDeclaration Lane(MaterialExportLane Slot, ChannelSubject Channel, bool Invert = false)
{
    MaterialExportLaneDeclaration Declared;
    Declared.Lane = Slot;
    Declared.Channel = Channel;
    Declared.Invert = Invert;
    Declared.Occupied = Channel != ChannelSubject::ChannelCount;
    return Declared;
}

MaterialExportImageDeclaration Image(const char* Suffix,
                                     MaterialExportImageFormat Format,
                                     MaterialExportBitDepth BitDepth,
                                     MaterialExportNormalConvention NormalConvention,
                                     bool ColourData,
                                     MaterialExportLaneDeclaration Red,
                                     MaterialExportLaneDeclaration Green = {},
                                     MaterialExportLaneDeclaration Blue = {},
                                     MaterialExportLaneDeclaration Alpha = {})
{
    MaterialExportImageDeclaration Declared;
    Declared.Suffix = Suffix;
    Declared.Format = Format;
    Declared.BitDepth = BitDepth;
    Declared.NormalConvention = NormalConvention;
    Declared.ColourData = ColourData;
    Declared.Lanes = { Red, Green, Blue, Alpha };
    return Declared;
}

bool ExportsConsumedChannel(const MaterialSpecification& Material, const MaterialExportImageDeclaration& Declared)
{
    for (const MaterialExportLaneDeclaration& Lane_ : Declared.Lanes)
    {
        if (!Lane_.Occupied)
            continue;
        if (Lane_.Channel == ChannelSubject::Displacement)
            return true;
        if (ChannelConsumed(Material.Reflectance(), Lane_.Channel) || Material.Channel(Lane_.Channel).ChannelDeclared)
            return true;
    }
    return false;
}

std::string JsonEscape(const std::string& Text)
{
    std::string Out;
    Out.reserve(Text.size() + 8u);
    for (char Character : Text)
    {
        if (Character == '\\' || Character == '"')
        {
            Out.push_back('\\');
            Out.push_back(Character);
        }
        else if (Character == '\n')
            Out += "\\n";
        else
            Out.push_back(Character);
    }
    return Out;
}

std::string FileNameOf(const MaterialExportPackage& Package, const MaterialExportImageDeclaration& Image_)
{
    return Package.Options.OutputName + Image_.Suffix + MaterialExportFormatExtension(Image_.Format);
}

std::uint64_t FingerprintMaterial(const WorkspaceMaterialRecord& Material)
{
    std::uint64_t Hash = HashSeed;
    HashString(Hash, Material.Reference);
    const ReflectanceSelection Reflectance = Material.Material.Reflectance();
    HashValue(Hash, Reflectance);
    for (std::uint32_t ChannelIndex = 0u; ChannelIndex < static_cast<std::uint32_t>(ChannelSubject::ChannelCount); ++ChannelIndex)
    {
        const ChannelSpecification& Channel = Material.Material.Channel(static_cast<ChannelSubject>(ChannelIndex));
        HashValue(Hash, Channel.Source);
        HashValue(Hash, Channel.Measured);
        HashValue(Hash, Channel.SourceIndex);
        HashValue(Hash, Channel.ConstantScalar);
        HashValue(Hash, Channel.ConstantColour.RedCoordinate);
        HashValue(Hash, Channel.ConstantColour.GreenCoordinate);
        HashValue(Hash, Channel.ConstantColour.BlueCoordinate);
        HashValue(Hash, Channel.DefaultScalar);
        HashValue(Hash, Channel.ChannelDeclared);
    }
    for (const WorkspaceMaterialImageReference& Reference : Material.Images)
    {
        HashString(Hash, Reference.ReferenceName);
        HashString(Hash, Reference.OriginPath);
        HashValue(Hash, Reference.Width);
        HashValue(Hash, Reference.Height);
        HashValue(Hash, Reference.ComponentCount);
        HashValue(Hash, Reference.BitsPerComponent);
        HashValue(Hash, Reference.ColourData);
    }
    for (const LayerSpecification& Layer : Material.Layers.Entries())
    {
        HashString(Hash, Layer.Name);
        HashValue(Hash, Layer.Source);
        HashValue(Hash, Layer.ChannelMask);
        HashValue(Hash, Layer.Combination);
        HashValue(Hash, Layer.Coverage.UniformStrength);
        HashValue(Hash, Layer.Coverage.ChannelMask);
        HashValue(Hash, Layer.Coverage.Inverted);
        HashValue(Hash, Layer.Coverage.CoverageDeclared);
        HashValue(Hash, Layer.PresenceEnabled);
    }
    return Hash;
}

} // namespace

const char* MaterialExportTargetText(MaterialExportTarget Target)
{
    switch (Target)
    {
        case MaterialExportTarget::Slate:   return "Slate";
        case MaterialExportTarget::Blender: return "Blender";
        case MaterialExportTarget::Unreal:  return "Unreal";
        case MaterialExportTarget::Unity:   return "Unity";
        case MaterialExportTarget::Gltf:    return "glTF";
        default:                            return "Unknown";
    }
}

const char* MaterialExportFormatExtension(MaterialExportImageFormat Format)
{
    switch (Format)
    {
        case MaterialExportImageFormat::Png: return ".png";
        case MaterialExportImageFormat::Tga: return ".tga";
        case MaterialExportImageFormat::Exr: return ".exr";
        default:                             return ".img";
    }
}

const char* MaterialExportChannelText(ChannelSubject Channel)
{
    switch (Channel)
    {
        case ChannelSubject::AlbedoColour:               return "albedoColour";
        case ChannelSubject::Metallic:                   return "metallic";
        case ChannelSubject::Roughness:                  return "roughness";
        case ChannelSubject::NormalIncidenceReflectance: return "normalIncidenceReflectance";
        case ChannelSubject::SurfaceOrientation:         return "surfaceOrientation";
        case ChannelSubject::AmbientOcclusion:           return "ambientOcclusion";
        case ChannelSubject::Emission:                   return "emission";
        case ChannelSubject::Opacity:                    return "opacity";
        case ChannelSubject::Anisotropy:                 return "anisotropy";
        case ChannelSubject::AnisotropyDirection:        return "anisotropyDirection";
        case ChannelSubject::ClearCoat:                  return "clearCoat";
        case ChannelSubject::ClearCoatRoughness:         return "clearCoatRoughness";
        case ChannelSubject::ClearCoatOrientation:       return "clearCoatOrientation";
        case ChannelSubject::SheenColour:                return "sheenColour";
        case ChannelSubject::SheenRoughness:             return "sheenRoughness";
        case ChannelSubject::SubsurfaceColour:           return "subsurfaceColour";
        case ChannelSubject::SubsurfaceThickness:        return "subsurfaceThickness";
        case ChannelSubject::Transmission:               return "transmission";
        case ChannelSubject::RefractionRatio:            return "refractionRatio";
        case ChannelSubject::Displacement:               return "displacement";
        default:                                         return "none";
    }
}

Deliver<std::vector<MaterialExportImageDeclaration>> MaterialExportPreset(
    MaterialExportTarget Target,
    MaterialExportImageFormat Format,
    MaterialExportBitDepth BitDepth,
    MaterialExportNormalConvention NormalConvention)
{
    if (Target >= MaterialExportTarget::TargetCount || Format >= MaterialExportImageFormat::FormatCount ||
        BitDepth >= MaterialExportBitDepth::DepthCount)
    {
        return Deliver<std::vector<MaterialExportImageDeclaration>>::Refuse(
            { RefusalReason::ContentUnsupported, "the material export preset declaration is unsupported" });
    }

    std::vector<MaterialExportImageDeclaration> Images;
    Images.push_back(Image("_Albedo", Format, BitDepth, NormalConvention, true,
                           Lane(MaterialExportLane::Red, ChannelSubject::AlbedoColour),
                           Lane(MaterialExportLane::Green, ChannelSubject::AlbedoColour),
                           Lane(MaterialExportLane::Blue, ChannelSubject::AlbedoColour),
                           Lane(MaterialExportLane::Alpha, ChannelSubject::Opacity)));
    Images.push_back(Image("_Normal", Format, BitDepth, NormalConvention, false,
                           Lane(MaterialExportLane::Red, ChannelSubject::SurfaceOrientation),
                           Lane(MaterialExportLane::Green, ChannelSubject::SurfaceOrientation,
                                NormalConvention == MaterialExportNormalConvention::DirectX),
                           Lane(MaterialExportLane::Blue, ChannelSubject::SurfaceOrientation)));

    if (Target == MaterialExportTarget::Unreal || Target == MaterialExportTarget::Gltf)
    {
        Images.push_back(Image("_ORM", Format, BitDepth, NormalConvention, false,
                               Lane(MaterialExportLane::Red, ChannelSubject::AmbientOcclusion),
                               Lane(MaterialExportLane::Green, ChannelSubject::Roughness),
                               Lane(MaterialExportLane::Blue, ChannelSubject::Metallic),
                               Lane(MaterialExportLane::Alpha, ChannelSubject::Opacity)));
    }
    else if (Target == MaterialExportTarget::Unity)
    {
        Images.push_back(Image("_Mask", Format, BitDepth, NormalConvention, false,
                               Lane(MaterialExportLane::Red, ChannelSubject::Metallic),
                               Lane(MaterialExportLane::Green, ChannelSubject::AmbientOcclusion),
                               Lane(MaterialExportLane::Blue, ChannelSubject::Displacement),
                               Lane(MaterialExportLane::Alpha, ChannelSubject::Roughness, true)));
    }
    else
    {
        Images.push_back(Image("_RoughnessMetallic", Format, BitDepth, NormalConvention, false,
                               Lane(MaterialExportLane::Red, ChannelSubject::Roughness),
                               Lane(MaterialExportLane::Green, ChannelSubject::Metallic),
                               Lane(MaterialExportLane::Blue, ChannelSubject::AmbientOcclusion),
                               Lane(MaterialExportLane::Alpha, ChannelSubject::Opacity)));
    }

    Images.push_back(Image("_Emission", Format, BitDepth, NormalConvention, true,
                           Lane(MaterialExportLane::Red, ChannelSubject::Emission),
                           Lane(MaterialExportLane::Green, ChannelSubject::Emission),
                           Lane(MaterialExportLane::Blue, ChannelSubject::Emission)));
    return Deliver<std::vector<MaterialExportImageDeclaration>>::Result(std::move(Images));
}

Deliver<MaterialExportPackage> BuildMaterialExportPackage(const WorkspaceMaterialRecord& Material,
                                                          const MaterialExportOptions& Options)
{
    if (Options.Target >= MaterialExportTarget::TargetCount || Options.Format >= MaterialExportImageFormat::FormatCount ||
        Options.BitDepth >= MaterialExportBitDepth::DepthCount || Options.Resolution == 0u ||
        Options.OutputName.empty() || Options.OutputDirectory.empty())
    {
        return Deliver<MaterialExportPackage>::Refuse(
            { RefusalReason::ContentUnsupported, "the material export options are incomplete" });
    }

    const Deliver<std::vector<MaterialExportImageDeclaration>> Preset =
        MaterialExportPreset(Options.Target, Options.Format, Options.BitDepth, Options.NormalConvention);
    if (!Preset.Resolved) return Deliver<MaterialExportPackage>::Refuse(Preset.Error);

    MaterialExportPackage Package;
    Package.MaterialReference = Material.Reference;
    Package.Options = Options;
    Package.Reflectance = Material.Material.Reflectance();
    Package.ReferencedImageCount = static_cast<std::uint32_t>(Material.Images.size());
    Package.MaterialFingerprint = FingerprintMaterial(Material);

    for (const MaterialExportImageDeclaration& Candidate : Preset.Resolve())
    {
        if (!ExportsConsumedChannel(Material.Material, Candidate))
            continue;
        MaterialExportImageDeclaration Accepted = Candidate;
        for (const MaterialExportLaneDeclaration& Lane_ : Accepted.Lanes)
            if (Lane_.Occupied) Package.ExportedChannelMask |= ChannelBit(Lane_.Channel);
        Package.Images.push_back(std::move(Accepted));
    }

    if (Package.Images.empty())
        return Deliver<MaterialExportPackage>::Refuse(
            { RefusalReason::ContentUnsupported, "the material export preset produced no consumed channels" });

    return Deliver<MaterialExportPackage>::Result(std::move(Package));
}

Deliver<std::string> EncodeMaterialExportManifest(const MaterialExportPackage& Package)
{
    if (Package.Images.empty())
        return Deliver<std::string>::Refuse({ RefusalReason::ContentUnsupported, "the material export package has no images" });

    char Fingerprint[32] = {};
    std::snprintf(Fingerprint, sizeof(Fingerprint), "%016llx",
                  static_cast<unsigned long long>(Package.MaterialFingerprint));

    std::string Out;
    Out += "{\n";
    Out += "  \"material\": \"" + JsonEscape(Package.MaterialReference) + "\",\n";
    Out += "  \"target\": \"" + std::string(MaterialExportTargetText(Package.Options.Target)) + "\",\n";
    Out += "  \"directory\": \"" + JsonEscape(Package.Options.OutputDirectory) + "\",\n";
    Out += "  \"resolution\": " + std::to_string(Package.Options.Resolution) + ",\n";
    Out += "  \"dilation\": " + std::string(Package.Options.Dilation ? "true" : "false") + ",\n";
    Out += "  \"fingerprint\": \"" + std::string(Fingerprint) + "\",\n";
    Out += "  \"images\": [\n";
    for (std::size_t ImageIndex = 0u; ImageIndex < Package.Images.size(); ++ImageIndex)
    {
        const MaterialExportImageDeclaration& Image_ = Package.Images[ImageIndex];
        Out += "    { \"file\": \"" + JsonEscape(FileNameOf(Package, Image_)) + "\", ";
        Out += "\"colourData\": " + std::string(Image_.ColourData ? "true" : "false") + ", \"lanes\": [";
        bool FirstLane = true;
        for (const MaterialExportLaneDeclaration& Lane_ : Image_.Lanes)
        {
            if (!Lane_.Occupied) continue;
            if (!FirstLane) Out += ", ";
            FirstLane = false;
            Out += "{ \"lane\": " + std::to_string(static_cast<std::uint32_t>(Lane_.Lane));
            Out += ", \"channel\": \"" + std::string(MaterialExportChannelText(Lane_.Channel)) + "\"";
            Out += ", \"invert\": " + std::string(Lane_.Invert ? "true" : "false") + " }";
        }
        Out += "] }";
        Out += ImageIndex + 1u < Package.Images.size() ? ",\n" : "\n";
    }
    Out += "  ]\n";
    Out += "}\n";
    return Deliver<std::string>::Result(std::move(Out));
}

} // namespace Slate
