//============================================================================================================================================
//                                                       MATERIALIMAGEIMPORT.CPP
//============================================================================================================================================

#include "SlateDocument/Format/MaterialImageImport/Api/MaterialImageImport.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace Slate
{
namespace
{

std::string Lower(std::string Text)
{
    for (char& Character : Text)
        Character = static_cast<char>(std::tolower(static_cast<unsigned char>(Character)));
    return Text;
}

Deliver<std::vector<std::uint8_t>> ReadPrefix(const std::string& Path, std::size_t Count)
{
    std::ifstream Input(Path, std::ios::binary);
    if (!Input)
        return Deliver<std::vector<std::uint8_t>>::Refuse({ RefusalReason::ContentUnsupported, "the image file could not be opened" });
    std::vector<std::uint8_t> Bytes(Count);
    Input.read(reinterpret_cast<char*>(Bytes.data()), static_cast<std::streamsize>(Bytes.size()));
    Bytes.resize(static_cast<std::size_t>(Input.gcount()));
    if (Bytes.empty())
        return Deliver<std::vector<std::uint8_t>>::Refuse({ RefusalReason::ContentUnsupported, "the image file is empty" });
    return Deliver<std::vector<std::uint8_t>>::Result(std::move(Bytes));
}

std::uint16_t Le16(const std::vector<std::uint8_t>& Bytes, std::size_t Offset)
{
    return static_cast<std::uint16_t>(Bytes[Offset]) |
           (static_cast<std::uint16_t>(Bytes[Offset + 1u]) << 8u);
}

std::uint32_t Le32(const std::vector<std::uint8_t>& Bytes, std::size_t Offset)
{
    return static_cast<std::uint32_t>(Bytes[Offset]) |
           (static_cast<std::uint32_t>(Bytes[Offset + 1u]) << 8u) |
           (static_cast<std::uint32_t>(Bytes[Offset + 2u]) << 16u) |
           (static_cast<std::uint32_t>(Bytes[Offset + 3u]) << 24u);
}

std::uint32_t Be32(const std::vector<std::uint8_t>& Bytes, std::size_t Offset)
{
    return (static_cast<std::uint32_t>(Bytes[Offset]) << 24u) |
           (static_cast<std::uint32_t>(Bytes[Offset + 1u]) << 16u) |
           (static_cast<std::uint32_t>(Bytes[Offset + 2u]) << 8u) |
           static_cast<std::uint32_t>(Bytes[Offset + 3u]);
}

bool ParsePng(const std::vector<std::uint8_t>& Bytes, WorkspaceMaterialImageReference& Reference)
{
    const std::uint8_t Signature[8] = { 137u, 80u, 78u, 71u, 13u, 10u, 26u, 10u };
    if (Bytes.size() < 29u || !std::equal(Signature, Signature + 8u, Bytes.begin()))
        return false;
    Reference.Width = Be32(Bytes, 16u);
    Reference.Height = Be32(Bytes, 20u);
    Reference.BitsPerComponent = Bytes[24u];
    const std::uint8_t Colour = Bytes[25u];
    Reference.ComponentCount = Colour == 0u ? 1u : Colour == 2u ? 3u : Colour == 3u ? 1u : Colour == 4u ? 2u : 4u;
    return Reference.Width != 0u && Reference.Height != 0u;
}

bool ParseBmp(const std::vector<std::uint8_t>& Bytes, WorkspaceMaterialImageReference& Reference)
{
    if (Bytes.size() < 30u || Bytes[0] != 'B' || Bytes[1] != 'M')
        return false;
    Reference.Width = Le32(Bytes, 18u);
    Reference.Height = Le32(Bytes, 22u);
    const std::uint16_t Bits = Le16(Bytes, 28u);
    Reference.BitsPerComponent = Bits >= 24u ? 8u : Bits;
    Reference.ComponentCount = Bits >= 32u ? 4u : Bits >= 24u ? 3u : 1u;
    return Reference.Width != 0u && Reference.Height != 0u;
}

bool ParseTga(const std::vector<std::uint8_t>& Bytes, WorkspaceMaterialImageReference& Reference)
{
    if (Bytes.size() < 18u)
        return false;
    Reference.Width = Le16(Bytes, 12u);
    Reference.Height = Le16(Bytes, 14u);
    const std::uint8_t Bits = Bytes[16u];
    Reference.BitsPerComponent = Bits >= 24u ? 8u : Bits;
    Reference.ComponentCount = Bits >= 32u ? 4u : Bits >= 24u ? 3u : 1u;
    return Reference.Width != 0u && Reference.Height != 0u;
}

bool ParseJpeg(const std::vector<std::uint8_t>& Bytes, WorkspaceMaterialImageReference& Reference)
{
    if (Bytes.size() < 4u || Bytes[0] != 0xFFu || Bytes[1] != 0xD8u)
        return false;
    std::size_t Cursor = 2u;
    while (Cursor + 9u < Bytes.size())
    {
        if (Bytes[Cursor] != 0xFFu) { ++Cursor; continue; }
        const std::uint8_t Marker = Bytes[Cursor + 1u];
        if (Marker == 0xC0u || Marker == 0xC1u || Marker == 0xC2u)
        {
            Reference.BitsPerComponent = Bytes[Cursor + 4u];
            Reference.Height = static_cast<std::uint32_t>(Bytes[Cursor + 5u]) << 8u | Bytes[Cursor + 6u];
            Reference.Width = static_cast<std::uint32_t>(Bytes[Cursor + 7u]) << 8u | Bytes[Cursor + 8u];
            Reference.ComponentCount = Bytes[Cursor + 9u];
            return Reference.Width != 0u && Reference.Height != 0u;
        }
        const std::uint16_t Length = static_cast<std::uint16_t>(Bytes[Cursor + 2u]) << 8u | Bytes[Cursor + 3u];
        if (Length < 2u) break;
        Cursor += 2u + Length;
    }
    return false;
}

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

std::string TemporaryPath(const char* Stem, const char* Extension)
{
    static std::uint32_t Counter = 0u;
    return (std::filesystem::temp_directory_path() /
            (std::string("slate_") + Stem + "_" + std::to_string(++Counter) + Extension)).string();
}

bool IdentifyExternal(const std::string& Path, WorkspaceMaterialImageReference& Reference)
{
    const std::string Info = TemporaryPath("material_identify", ".txt");
    const std::string Command = "identify -format '%w %h %[channels] %[bit-depth]' " + ShellQuote(Path) + " > " + ShellQuote(Info);
    if (std::system(Command.c_str()) != 0)
    {
        std::remove(Info.c_str());
        return false;
    }

    std::ifstream Input(Info);
    std::string Channels;
    Input >> Reference.Width >> Reference.Height >> Channels >> Reference.BitsPerComponent;
    std::remove(Info.c_str());
    if (Reference.Width == 0u || Reference.Height == 0u)
        return false;
    const std::string LowerChannels = Lower(Channels);
    Reference.ComponentCount = LowerChannels.find('a') != std::string::npos ? 4u
                             : LowerChannels.find("rgb") != std::string::npos ? 3u : 1u;
    return true;
}

} // namespace

MaterialImageFormat ClassifyMaterialImageFormat(const std::string& Path)
{
    const std::string Extension = Lower(std::filesystem::path(Path).extension().string());
    if (Extension == ".png") return MaterialImageFormat::Png;
    if (Extension == ".jpg" || Extension == ".jpeg") return MaterialImageFormat::Jpeg;
    if (Extension == ".bmp") return MaterialImageFormat::Bitmap;
    if (Extension == ".tga") return MaterialImageFormat::Tga;
    if (Extension == ".webp") return MaterialImageFormat::Webp;
    if (Extension == ".exr") return MaterialImageFormat::Exr;
    return MaterialImageFormat::Unsupported;
}

bool MaterialImageFormatSupported(const std::string& Path)
{
    return ClassifyMaterialImageFormat(Path) != MaterialImageFormat::Unsupported;
}

ChannelSubject SuggestMaterialImageChannel(const std::string& Path, bool& ColourData)
{
    const std::string Stem = Lower(std::filesystem::path(Path).stem().string());
    ColourData = false;
    if (Stem.find("normal") != std::string::npos || Stem.find("_nrm") != std::string::npos) return ChannelSubject::SurfaceOrientation;
    if (Stem.find("rough") != std::string::npos) return ChannelSubject::Roughness;
    if (Stem.find("metal") != std::string::npos) return ChannelSubject::Metallic;
    if (Stem.find("opacity") != std::string::npos || Stem.find("alpha") != std::string::npos) return ChannelSubject::Opacity;
    if (Stem.find("ao") != std::string::npos || Stem.find("occlusion") != std::string::npos) return ChannelSubject::AmbientOcclusion;
    if (Stem.find("emit") != std::string::npos) { ColourData = true; return ChannelSubject::Emission; }
    ColourData = true;
    return ChannelSubject::AlbedoColour;
}

Deliver<ImportedMaterialImage> ImportMaterialImageReference(const std::string& Path)
{
    const MaterialImageFormat Format = ClassifyMaterialImageFormat(Path);
    if (Format == MaterialImageFormat::Unsupported)
        return Deliver<ImportedMaterialImage>::Refuse({ RefusalReason::ContentUnsupported, "the material image format is unsupported" });

    const Deliver<std::vector<std::uint8_t>> Prefix = ReadPrefix(Path, 65536u);
    if (!Prefix.Resolved) return Deliver<ImportedMaterialImage>::Refuse(Prefix.Error);

    ImportedMaterialImage Imported;
    Imported.Format = Format;
    Imported.Reference.OriginPath = Path;
    Imported.Reference.ReferenceName = std::filesystem::path(Path).filename().string();
    Imported.Reference.BitsPerComponent = 8u;
    bool Parsed = false;
    if (Format == MaterialImageFormat::Png) Parsed = ParsePng(Prefix.Resolve(), Imported.Reference);
    else if (Format == MaterialImageFormat::Jpeg) Parsed = ParseJpeg(Prefix.Resolve(), Imported.Reference);
    else if (Format == MaterialImageFormat::Bitmap) Parsed = ParseBmp(Prefix.Resolve(), Imported.Reference);
    else if (Format == MaterialImageFormat::Tga) Parsed = ParseTga(Prefix.Resolve(), Imported.Reference);

    if (!Parsed)
        Parsed = IdentifyExternal(Path, Imported.Reference);

    if (!Parsed)
        return Deliver<ImportedMaterialImage>::Refuse({ RefusalReason::ContentUnsupported, "the material image header is unsupported" });

    bool ColourData = true;
    Imported.SuggestedChannel = SuggestMaterialImageChannel(Path, ColourData);
    Imported.Reference.ColourData = ColourData;
    return Deliver<ImportedMaterialImage>::Result(Imported);
}

} // namespace Slate
