//============================================================================================================================================
//                                                      MATERIALTEXTUREEXPORT.CPP
//============================================================================================================================================

#include "SlateCompute/Compute/MaterialTextureExport/Api/MaterialTextureExport.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

namespace Slate
{
namespace
{

std::string ShellQuote(const std::string& Path)
{
    std::string Quoted = "'";
    for (char Character : Path)
    {
        if (Character == '\'') Quoted += "'\\''";
        else Quoted.push_back(Character);
    }
    Quoted.push_back('\'');
    return Quoted;
}

std::string TexturePath(const MaterialExportPackage& Package, const MaterialExportImageDeclaration& Image)
{
    return (std::filesystem::path(Package.Options.OutputDirectory) /
            (Package.Options.OutputName + Image.Suffix + MaterialExportFormatExtension(Image.Format))).string();
}

double ClampUnit(double Value)
{
    return Value < 0.0 ? 0.0 : (Value > 1.0 ? 1.0 : Value);
}

std::uint8_t Byte(double Value)
{
    return static_cast<std::uint8_t>(ClampUnit(Value) * 255.0 + 0.5);
}

ColourSpecification ChannelColour(const WorkspaceMaterialRecord& Material, ChannelSubject Channel)
{
    const ChannelSpecification& Declared = Material.Material.Channel(Channel);
    if (Declared.ChannelDeclared && MeasureCarriesColour(Declared.Measured))
        return Declared.Source == ChannelSource::Constant ? Declared.ConstantColour : Declared.DefaultColour;
    if (Channel == ChannelSubject::Emission) return { 0.0, 0.0, 0.0, WorkingSpaceIdentity };
    return { 1.0, 1.0, 1.0, WorkingSpaceIdentity };
}

double ChannelScalar(const WorkspaceMaterialRecord& Material, ChannelSubject Channel, double U, double V)
{
    const ChannelSpecification& Declared = Material.Material.Channel(Channel);
    if (Declared.ChannelDeclared && Declared.Source == ChannelSource::Imported && Declared.SourceIndex < Material.Images.size())
    {
        MaterialImageSampling Sampling;
        MaterialImageSampleRequest Request;
        Request.Channel = Channel;
        Request.CoordinateU = U;
        Request.CoordinateV = V;
        const Deliver<MaterialImageSample> Sampled = Sampling.SampleMaterialChannel(Material, Request);
        if (Sampled.Resolved) return Sampled.Resolve().ColourSample ? Sampled.Resolve().Colour.RedCoordinate
                                                                    : Sampled.Resolve().Scalar;
    }
    if (Declared.ChannelDeclared && Declared.Measured == ChannelMeasure::Scalar)
        return Declared.Source == ChannelSource::Constant ? Declared.ConstantScalar : Declared.DefaultScalar;
    switch (Channel)
    {
        case ChannelSubject::Roughness: return 0.5;
        case ChannelSubject::Opacity: return 1.0;
        case ChannelSubject::AmbientOcclusion: return 1.0;
        case ChannelSubject::NormalIncidenceReflectance: return 0.04;
        case ChannelSubject::SurfaceOrientation: return 0.5;
        default: return 0.0;
    }
}

double ChannelLaneValue(const WorkspaceMaterialRecord& Material,
                        const MaterialExportLaneDeclaration& Lane,
                        double U,
                        double V)
{
    if (!Lane.Occupied || Lane.Channel == ChannelSubject::ChannelCount)
        return 1.0;

    double Value = 0.0;
    if (Lane.Channel == ChannelSubject::AlbedoColour || Lane.Channel == ChannelSubject::Emission)
    {
        const ColourSpecification Colour = ChannelColour(Material, Lane.Channel);
        if (Lane.Lane == MaterialExportLane::Red) Value = Colour.RedCoordinate;
        else if (Lane.Lane == MaterialExportLane::Green) Value = Colour.GreenCoordinate;
        else if (Lane.Lane == MaterialExportLane::Blue) Value = Colour.BlueCoordinate;
        else Value = 1.0;
    }
    else if (Lane.Channel == ChannelSubject::SurfaceOrientation)
    {
        Value = Lane.Lane == MaterialExportLane::Blue ? 1.0 : 0.5;
    }
    else
    {
        Value = ChannelScalar(Material, Lane.Channel, U, V);
    }

    return Lane.Invert ? 1.0 - ClampUnit(Value) : ClampUnit(Value);
}

Deliver<bool> WriteTga(const FlattenedMaterialTexture& Texture, const std::string& Path)
{
    if (Texture.Width == 0u || Texture.Height == 0u || Texture.Texels.size() < static_cast<std::size_t>(Texture.Width) * Texture.Height * 4u)
        return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "the flattened texture has no whole image" });

    std::ofstream Output(Path, std::ios::binary);
    if (!Output)
        return Deliver<bool>::Refuse({ RefusalReason::HostDenied, "the texture output file could not be opened" });

    std::uint8_t Header[18] = {};
    Header[2] = 2u;
    Header[12] = static_cast<std::uint8_t>(Texture.Width & 0xFFu);
    Header[13] = static_cast<std::uint8_t>((Texture.Width >> 8u) & 0xFFu);
    Header[14] = static_cast<std::uint8_t>(Texture.Height & 0xFFu);
    Header[15] = static_cast<std::uint8_t>((Texture.Height >> 8u) & 0xFFu);
    Header[16] = 32u;
    Header[17] = 0x20u | 8u;
    Output.write(reinterpret_cast<const char*>(Header), sizeof(Header));

    for (std::uint32_t Y = 0u; Y < Texture.Height; ++Y)
    {
        for (std::uint32_t X = 0u; X < Texture.Width; ++X)
        {
            const std::size_t Base = (static_cast<std::size_t>(Y) * Texture.Width + X) * 4u;
            const std::uint8_t Pixel[4] = { Byte(Texture.Texels[Base + 2u]), Byte(Texture.Texels[Base + 1u]),
                                            Byte(Texture.Texels[Base + 0u]), Byte(Texture.Texels[Base + 3u]) };
            Output.write(reinterpret_cast<const char*>(Pixel), sizeof(Pixel));
        }
    }
    return Deliver<bool>::Result(true);
}

Deliver<bool> ConvertTga(const std::string& TgaPath, const std::string& FinalPath)
{
    const std::string Command = "convert " + ShellQuote(TgaPath) + " " + ShellQuote(FinalPath);
    if (std::system(Command.c_str()) != 0)
        return Deliver<bool>::Refuse({ RefusalReason::HostDenied, "the external image writer rejected the flattened texture" });
    return Deliver<bool>::Result(true);
}

} // namespace

Deliver<FlattenedMaterialTexture> MaterialTextureExport::FlattenImage(
    const WorkspaceMaterialRecord& Material,
    const MaterialExportPackage& Package,
    const MaterialExportImageDeclaration& Image) const
{
    if (Package.Options.Resolution == 0u)
        return Deliver<FlattenedMaterialTexture>::Refuse({ RefusalReason::ContentUnsupported, "the export resolution is zero" });

    FlattenedMaterialTexture Texture;
    Texture.Path = TexturePath(Package, Image);
    Texture.Declaration = Image;
    Texture.Width = Package.Options.Resolution;
    Texture.Height = Package.Options.Resolution;
    Texture.Texels.assign(static_cast<std::size_t>(Texture.Width) * Texture.Height * 4u, 1.0f);

    for (std::uint32_t Y = 0u; Y < Texture.Height; ++Y)
    {
        const double V = (static_cast<double>(Y) + 0.5) / static_cast<double>(Texture.Height);
        for (std::uint32_t X = 0u; X < Texture.Width; ++X)
        {
            const double U = (static_cast<double>(X) + 0.5) / static_cast<double>(Texture.Width);
            const std::size_t Base = (static_cast<std::size_t>(Y) * Texture.Width + X) * 4u;
            for (const MaterialExportLaneDeclaration& Lane : Image.Lanes)
            {
                if (!Lane.Occupied) continue;
                Texture.Texels[Base + static_cast<std::uint32_t>(Lane.Lane)] =
                    static_cast<float>(ChannelLaneValue(Material, Lane, U, V));
            }
        }
    }

    return Deliver<FlattenedMaterialTexture>::Result(std::move(Texture));
}

Deliver<bool> MaterialTextureExport::WriteImage(const FlattenedMaterialTexture& Texture) const
{
    std::filesystem::create_directories(std::filesystem::path(Texture.Path).parent_path());
    if (Texture.Declaration.Format == MaterialExportImageFormat::Tga)
        return WriteTga(Texture, Texture.Path);

    const std::string TemporaryTga = Texture.Path + ".slate_tmp.tga";
    const Deliver<bool> Temporary = WriteTga(Texture, TemporaryTga);
    if (!Temporary.Resolved) return Temporary;
    const Deliver<bool> Converted = ConvertTga(TemporaryTga, Texture.Path);
    std::remove(TemporaryTga.c_str());
    return Converted;
}

Deliver<MaterialTextureExportReport> MaterialTextureExport::WritePackage(const WorkspaceMaterialRecord& Material,
                                                                         const MaterialExportPackage& Package) const
{
    MaterialTextureExportReport Report;
    for (const MaterialExportImageDeclaration& Image : Package.Images)
    {
        const Deliver<FlattenedMaterialTexture> Flattened = FlattenImage(Material, Package, Image);
        if (!Flattened.Resolved) return Deliver<MaterialTextureExportReport>::Refuse(Flattened.Error);
        const Deliver<bool> Written = WriteImage(Flattened.Resolve());
        if (!Written.Resolved) return Deliver<MaterialTextureExportReport>::Refuse(Written.Error);
        Report.WrittenFiles.push_back(Flattened.Resolve().Path);
        Report.PixelCount += Flattened.Resolve().Width * Flattened.Resolve().Height;
    }

    const Deliver<std::string> Manifest = EncodeMaterialExportManifest(Package);
    if (!Manifest.Resolved) return Deliver<MaterialTextureExportReport>::Refuse(Manifest.Error);
    std::filesystem::create_directories(Package.Options.OutputDirectory);
    Report.ManifestPath = (std::filesystem::path(Package.Options.OutputDirectory) /
                           (Package.Options.OutputName + "_material_manifest.json")).string();
    std::ofstream ManifestFile(Report.ManifestPath);
    if (!ManifestFile)
        return Deliver<MaterialTextureExportReport>::Refuse({ RefusalReason::HostDenied, "the export manifest could not be opened" });
    ManifestFile << Manifest.Resolve();
    Report.WrittenFiles.push_back(Report.ManifestPath);
    Report.ImageCount = static_cast<std::uint32_t>(Package.Images.size());
    return Deliver<MaterialTextureExportReport>::Result(std::move(Report));
}

} // namespace Slate
