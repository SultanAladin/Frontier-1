//============================================================================================================================================
//                                                      MATERIALIMAGESAMPLING.CPP
//============================================================================================================================================

#include "SlateCompute/Compute/MaterialImageSampling/Api/MaterialImageSampling.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

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

Deliver<std::vector<std::uint8_t>> ReadAll(const std::string& Path)
{
    std::ifstream Input(Path, std::ios::binary);
    if (!Input)
        return Deliver<std::vector<std::uint8_t>>::Refuse(
            { RefusalReason::ContentUnsupported, "the imported image reference is absent" });
    Input.seekg(0, std::ios::end);
    const std::streamoff Count = Input.tellg();
    if (Count <= 0)
        return Deliver<std::vector<std::uint8_t>>::Refuse(
            { RefusalReason::ContentUnsupported, "the imported image reference is empty" });
    Input.seekg(0, std::ios::beg);
    std::vector<std::uint8_t> Bytes(static_cast<std::size_t>(Count));
    Input.read(reinterpret_cast<char*>(Bytes.data()), Count);
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

void WritePixel(MaterialImageRaster& Raster,
                std::uint32_t X,
                std::uint32_t Y,
                float Red,
                float Green,
                float Blue,
                float Alpha)
{
    const std::size_t Base = (static_cast<std::size_t>(Y) * Raster.Width + X) * 4u;
    Raster.Texels[Base + 0u] = Red;
    Raster.Texels[Base + 1u] = Green;
    Raster.Texels[Base + 2u] = Blue;
    Raster.Texels[Base + 3u] = Alpha;
}

Deliver<MaterialImageRaster> DecodeBmp(const std::vector<std::uint8_t>& Bytes,
                                       const WorkspaceMaterialImageReference& Reference)
{
    if (Bytes.size() < 54u || Bytes[0] != 'B' || Bytes[1] != 'M')
        return Deliver<MaterialImageRaster>::Refuse({ RefusalReason::ContentUnsupported, "the bitmap header is unsupported" });

    const std::uint32_t DataOffset = Le32(Bytes, 10u);
    const std::uint32_t Width = Le32(Bytes, 18u);
    const std::uint32_t Height = Le32(Bytes, 22u);
    const std::uint16_t Planes = Le16(Bytes, 26u);
    const std::uint16_t Bits = Le16(Bytes, 28u);
    const std::uint32_t Compression = Le32(Bytes, 30u);
    if (Planes != 1u || Compression != 0u || Width == 0u || Height == 0u || (Bits != 24u && Bits != 32u))
        return Deliver<MaterialImageRaster>::Refuse({ RefusalReason::ContentUnsupported, "only uncompressed 24/32-bit BMP material images are decoded" });

    const std::uint32_t BytesPerPixel = Bits / 8u;
    const std::uint32_t RowStride = ((Width * BytesPerPixel + 3u) / 4u) * 4u;
    if (DataOffset + static_cast<std::size_t>(RowStride) * Height > Bytes.size())
        return Deliver<MaterialImageRaster>::Refuse({ RefusalReason::ContentUnsupported, "the bitmap pixel span is incomplete" });

    MaterialImageRaster Raster;
    Raster.OriginPath = Reference.OriginPath;
    Raster.Width = Width;
    Raster.Height = Height;
    Raster.ComponentCount = Bits == 32u ? 4u : 3u;
    Raster.ColourData = Reference.ColourData;
    Raster.Texels.assign(static_cast<std::size_t>(Width) * Height * 4u, 1.0f);
    for (std::uint32_t Y = 0u; Y < Height; ++Y)
    {
        const std::uint32_t SourceY = Height - 1u - Y;
        const std::size_t Row = DataOffset + static_cast<std::size_t>(SourceY) * RowStride;
        for (std::uint32_t X = 0u; X < Width; ++X)
        {
            const std::size_t Pixel = Row + static_cast<std::size_t>(X) * BytesPerPixel;
            const float Blue = static_cast<float>(Bytes[Pixel + 0u]) / 255.0f;
            const float Green = static_cast<float>(Bytes[Pixel + 1u]) / 255.0f;
            const float Red = static_cast<float>(Bytes[Pixel + 2u]) / 255.0f;
            const float Alpha = Bits == 32u ? static_cast<float>(Bytes[Pixel + 3u]) / 255.0f : 1.0f;
            WritePixel(Raster, X, Y, Red, Green, Blue, Alpha);
        }
    }
    return Deliver<MaterialImageRaster>::Result(std::move(Raster));
}

Deliver<MaterialImageRaster> DecodeTga(const std::vector<std::uint8_t>& Bytes,
                                       const WorkspaceMaterialImageReference& Reference)
{
    if (Bytes.size() < 18u)
        return Deliver<MaterialImageRaster>::Refuse({ RefusalReason::ContentUnsupported, "the TGA header is incomplete" });

    const std::uint8_t IdLength = Bytes[0];
    const std::uint8_t ColourMap = Bytes[1];
    const std::uint8_t ImageType = Bytes[2];
    const std::uint32_t Width = Le16(Bytes, 12u);
    const std::uint32_t Height = Le16(Bytes, 14u);
    const std::uint8_t Bits = Bytes[16u];
    const std::uint8_t Descriptor = Bytes[17u];
    if (ColourMap != 0u || Width == 0u || Height == 0u || (ImageType != 2u && ImageType != 3u) ||
        (Bits != 8u && Bits != 24u && Bits != 32u))
    {
        return Deliver<MaterialImageRaster>::Refuse(
            { RefusalReason::ContentUnsupported, "only uncompressed greyscale/RGB/RGBA TGA material images are decoded" });
    }

    const std::uint32_t BytesPerPixel = Bits / 8u;
    const std::size_t DataOffset = 18u + IdLength;
    if (DataOffset + static_cast<std::size_t>(Width) * Height * BytesPerPixel > Bytes.size())
        return Deliver<MaterialImageRaster>::Refuse({ RefusalReason::ContentUnsupported, "the TGA pixel span is incomplete" });

    MaterialImageRaster Raster;
    Raster.OriginPath = Reference.OriginPath;
    Raster.Width = Width;
    Raster.Height = Height;
    Raster.ComponentCount = ImageType == 3u ? 1u : (Bits == 32u ? 4u : 3u);
    Raster.ColourData = Reference.ColourData;
    Raster.Texels.assign(static_cast<std::size_t>(Width) * Height * 4u, 1.0f);
    const bool TopOrigin = (Descriptor & 0x20u) != 0u;
    for (std::uint32_t Y = 0u; Y < Height; ++Y)
    {
        const std::uint32_t SourceY = TopOrigin ? Y : Height - 1u - Y;
        for (std::uint32_t X = 0u; X < Width; ++X)
        {
            const std::size_t Pixel = DataOffset + (static_cast<std::size_t>(SourceY) * Width + X) * BytesPerPixel;
            if (ImageType == 3u)
            {
                const float Grey = static_cast<float>(Bytes[Pixel]) / 255.0f;
                WritePixel(Raster, X, Y, Grey, Grey, Grey, 1.0f);
            }
            else
            {
                const float Blue = static_cast<float>(Bytes[Pixel + 0u]) / 255.0f;
                const float Green = static_cast<float>(Bytes[Pixel + 1u]) / 255.0f;
                const float Red = static_cast<float>(Bytes[Pixel + 2u]) / 255.0f;
                const float Alpha = Bits == 32u ? static_cast<float>(Bytes[Pixel + 3u]) / 255.0f : 1.0f;
                WritePixel(Raster, X, Y, Red, Green, Blue, Alpha);
            }
        }
    }
    return Deliver<MaterialImageRaster>::Result(std::move(Raster));
}

double Address(double Coordinate, MaterialImageAddressing Addressing)
{
    if (Addressing == MaterialImageAddressing::Clamp)
        return std::max(0.0, std::min(1.0, Coordinate));

    double Wrapped = Coordinate - std::floor(Coordinate);
    if (Addressing == MaterialImageAddressing::Mirror)
    {
        const double Period = Coordinate - std::floor(Coordinate / 2.0) * 2.0;
        Wrapped = Period > 1.0 ? 2.0 - Period : Period;
    }
    return Wrapped;
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
    const std::filesystem::path Root = std::filesystem::temp_directory_path();
    return (Root / (std::string("slate_") + Stem + "_" + std::to_string(++Counter) + Extension)).string();
}

Deliver<MaterialImageRaster> DecodeExternal(const WorkspaceMaterialImageReference& Reference)
{
    if (Reference.Width == 0u || Reference.Height == 0u)
        return Deliver<MaterialImageRaster>::Refuse(
            { RefusalReason::ContentUnsupported, "the imported image reference has no declared extent for decoding" });

    const std::string Raw = TemporaryPath("material_decode", ".rgba");
    const std::string Command = "convert " + ShellQuote(Reference.OriginPath) +
        " -auto-orient -resize " + std::to_string(Reference.Width) + "x" + std::to_string(Reference.Height) + "!" +
        " -depth 8 rgba:" + ShellQuote(Raw);
    if (std::system(Command.c_str()) != 0)
    {
        std::remove(Raw.c_str());
        return Deliver<MaterialImageRaster>::Refuse(
            { RefusalReason::ContentUnsupported, "the external image decoder rejected the referenced material image" });
    }

    const Deliver<std::vector<std::uint8_t>> Bytes = ReadAll(Raw);
    std::remove(Raw.c_str());
    if (!Bytes.Resolved) return Deliver<MaterialImageRaster>::Refuse(Bytes.Error);

    const std::size_t Expected = static_cast<std::size_t>(Reference.Width) * Reference.Height * 4u;
    if (Bytes.Resolve().size() < Expected)
        return Deliver<MaterialImageRaster>::Refuse(
            { RefusalReason::ContentUnsupported, "the external image decoder produced an incomplete RGBA span" });

    MaterialImageRaster Raster;
    Raster.OriginPath = Reference.OriginPath;
    Raster.Width = Reference.Width;
    Raster.Height = Reference.Height;
    Raster.ComponentCount = 4u;
    Raster.ColourData = Reference.ColourData;
    Raster.Texels.assign(Expected, 1.0f);
    for (std::size_t Index = 0u; Index < Expected; ++Index)
        Raster.Texels[Index] = static_cast<float>(Bytes.Resolve()[Index]) / 255.0f;
    return Deliver<MaterialImageRaster>::Result(std::move(Raster));
}

} // namespace

std::uint64_t FingerprintMaterialImageReference(const WorkspaceMaterialImageReference& Reference)
{
    std::uint64_t Hash = HashSeed;
    HashString(Hash, Reference.ReferenceName);
    HashString(Hash, Reference.OriginPath);
    HashValue(Hash, Reference.Width);
    HashValue(Hash, Reference.Height);
    HashValue(Hash, Reference.ComponentCount);
    HashValue(Hash, Reference.BitsPerComponent);
    HashValue(Hash, Reference.ColourData);
    return Hash;
}

Deliver<MaterialImageRaster> MaterialImageSampling::OpenReference(const WorkspaceMaterialImageReference& Reference) const
{
    const Deliver<std::vector<std::uint8_t>> Bytes = ReadAll(Reference.OriginPath);
    if (!Bytes.Resolved) return Deliver<MaterialImageRaster>::Refuse(Bytes.Error);

    if (Bytes.Resolve().size() >= 2u && Bytes.Resolve()[0] == 'B' && Bytes.Resolve()[1] == 'M')
    {
        const Deliver<MaterialImageRaster> Native = DecodeBmp(Bytes.Resolve(), Reference);
        if (Native.Resolved) return Native;
    }
    if (Bytes.Resolve().size() >= 3u && Bytes.Resolve()[1] == 0u &&
        (Bytes.Resolve()[2] == 2u || Bytes.Resolve()[2] == 3u))
    {
        const Deliver<MaterialImageRaster> Native = DecodeTga(Bytes.Resolve(), Reference);
        if (Native.Resolved) return Native;
    }

    return DecodeExternal(Reference);
}

Deliver<MaterialImageSample> MaterialImageSampling::SampleReference(const WorkspaceMaterialImageReference& Reference,
                                                                    ChannelSubject Channel,
                                                                    double CoordinateU,
                                                                    double CoordinateV,
                                                                    MaterialImageAddressing AddressU,
                                                                    MaterialImageAddressing AddressV) const
{
    const Deliver<MaterialImageRaster> Opened = OpenReference(Reference);
    if (!Opened.Resolved) return Deliver<MaterialImageSample>::Refuse(Opened.Error);
    const MaterialImageRaster Raster = Opened.Resolve();
    if (Raster.Width == 0u || Raster.Height == 0u || Raster.Texels.empty())
        return Deliver<MaterialImageSample>::Refuse({ RefusalReason::ContentUnsupported, "the imported image has no sampleable pixels" });

    const double U = Address(CoordinateU, AddressU);
    const double V = Address(CoordinateV, AddressV);
    const std::uint32_t X = std::min<std::uint32_t>(Raster.Width - 1u, static_cast<std::uint32_t>(U * Raster.Width));
    const std::uint32_t Y = std::min<std::uint32_t>(Raster.Height - 1u, static_cast<std::uint32_t>(V * Raster.Height));
    const std::size_t Base = (static_cast<std::size_t>(Y) * Raster.Width + X) * 4u;

    MaterialImageSample Sample;
    Sample.Channel = Channel;
    Sample.Colour = { Raster.Texels[Base + 0u], Raster.Texels[Base + 1u], Raster.Texels[Base + 2u], WorkingSpaceIdentity };
    Sample.Scalar = Raster.Texels[Base + 0u];
    Sample.Alpha = Raster.Texels[Base + 3u];
    Sample.ReferenceFingerprint = FingerprintMaterialImageReference(Reference);
    Sample.ColourSample = Reference.ColourData;
    return Deliver<MaterialImageSample>::Result(Sample);
}

Deliver<MaterialImageSample> MaterialImageSampling::SampleMaterialChannel(const WorkspaceMaterialRecord& Material,
                                                                          const MaterialImageSampleRequest& Request) const
{
    if (Request.Channel == ChannelSubject::ChannelCount)
        return Deliver<MaterialImageSample>::Refuse({ RefusalReason::ContentUnsupported, "the closed channel count is not sampleable" });

    const ChannelSpecification& Channel = Material.Material.Channel(Request.Channel);
    if (!Channel.ChannelDeclared || Channel.Source != ChannelSource::Imported)
        return Deliver<MaterialImageSample>::Refuse({ RefusalReason::ContentUnsupported, "the requested material channel is not imported imagery" });
    if (Channel.SourceIndex >= Material.Images.size())
        return Deliver<MaterialImageSample>::Refuse({ RefusalReason::ContentUnsupported, "the imported image source index is outside the material" });

    const Deliver<MaterialImageSample> Sampled = SampleReference(Material.Images[Channel.SourceIndex], Request.Channel,
                                                                 Request.CoordinateU, Request.CoordinateV,
                                                                 Request.AddressU, Request.AddressV);
    if (!Sampled.Resolved) return Sampled;

    MaterialImageSample Produced = Sampled.Resolve();
    Produced.SourceIndex = Channel.SourceIndex;
    Produced.ColourSample = MeasureCarriesColour(Channel.Measured);
    return Deliver<MaterialImageSample>::Result(Produced);
}

MaterialImageSamplingCapabilities MaterialImageSampling::Capabilities() const
{
    return {};
}

} // namespace Slate
