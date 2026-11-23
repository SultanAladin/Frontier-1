//============================================================================================================================================
//                                                              IMAGECODEC.CPP
//============================================================================================================================================
// 🧩 `10` §1 — image streams translated to the texels the file carried, at the depth it carried them, and nothing else.

#include "SlateDocument/Format/ImageCodec/Api/ImageCodec.h"

#include <cstddef>
#include <cstring>

// 📝 The vendored decoder is compiled into this translation unit and nowhere else, so exactly one copy of it
//    exists in the process. Its warnings are silenced around the include rather than engine-wide, because /W4
//    is what keeps engine sources clean and a global suppression would take that away from every file.
#pragma warning(push, 0)
#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO
#define STBI_NO_GIF
#define STBI_NO_PIC
#define STBI_NO_PNM
#define STBI_NO_PSD
#include "stb/stb_image.h"
#pragma warning(pop)

namespace Slate
{

namespace
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    THE SIGNATURES
//------------------------------------------------------------------------------------------------------------------------

const std::uint8_t  PortableNetworkSignature[8] = { 0x89u, 0x50u, 0x4Eu, 0x47u, 0x0Du, 0x0Au, 0x1Au, 0x0Au };
const std::uint8_t  JointPhotographicSignature[3] = { 0xFFu, 0xD8u, 0xFFu };

const char* const RadianceSignature      = "#?RADIANCE";
const char* const RadianceSignatureShort = "#?RGBE";

/// 🧩 Whether the leading bytes begin with one declared signature.
bool LeadingMatches(const std::vector<std::uint8_t>& Leading, const std::uint8_t* Signature, std::size_t Spanned)
{
    if (Leading.size() < Spanned) { return false; }

    return std::memcmp(Leading.data(), Signature, Spanned) == 0;
}

/// 🧩 Whether the leading bytes begin with one declared textual signature.
bool LeadingMatchesText(const std::vector<std::uint8_t>& Leading, const char* Signature)
{
    const std::size_t Spanned = std::strlen(Signature);

    if (Leading.size() < Spanned) { return false; }

    return std::memcmp(Leading.data(), Signature, Spanned) == 0;
}

// 📝 Truevision carries no signature at all, so it is identified from the two enumerated bytes its heading
//    declares rather than from a magic number. This is why it is tested last: every other layout has a
//    signature that decides the question outright, and a heading test that ran first would claim streams
//    that are not Truevision at all.
bool TruevisionPlausible(const std::vector<std::uint8_t>& Leading)
{
    if (Leading.size() < 18u) { return false; }

    const std::uint8_t ColourMapPresent = Leading[1];
    const std::uint8_t ContentSubject   = Leading[2];

    if (ColourMapPresent > 1u) { return false; }

    return ContentSubject == 1u || ContentSubject == 2u || ContentSubject == 3u
        || ContentSubject == 9u || ContentSubject == 10u || ContentSubject == 11u;
}

}   // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                  THE CLASSIFICATION
//------------------------------------------------------------------------------------------------------------------------

ImageContentSubject ClassifyContent(const std::vector<std::uint8_t>& Leading)
{
    if (LeadingMatches(Leading, PortableNetworkSignature, sizeof(PortableNetworkSignature)))
    {
        return ImageContentSubject::PortableNetwork;
    }

    if (LeadingMatches(Leading, JointPhotographicSignature, sizeof(JointPhotographicSignature)))
    {
        return ImageContentSubject::JointPhotographic;
    }

    if (LeadingMatchesText(Leading, RadianceSignature) || LeadingMatchesText(Leading, RadianceSignatureShort))
    {
        return ImageContentSubject::Radiance;
    }

    if (TruevisionPlausible(Leading))
    {
        return ImageContentSubject::Truevision;
    }

    return ImageContentSubject::Unrecognised;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                   THE TRANSLATION
//------------------------------------------------------------------------------------------------------------------------

Deliver<DecodedImage> Translate(const std::vector<std::uint8_t>& Stream, const std::string& OriginPath)
{
    if (Stream.empty())
    {
        return Deliver<DecodedImage>::Refuse(
            { RefusalReason::ContentUnsupported, "an image stream of no bytes carries no image" });
    }

    if (Stream.size() > static_cast<std::size_t>(0x7FFFFFFF))
    {
        return Deliver<DecodedImage>::Refuse(
            { RefusalReason::ExtentExhausted, "the image stream is longer than the decoder can address" });
    }

    const std::vector<std::uint8_t>  Leading(Stream.begin(),
                                             Stream.begin() + static_cast<std::ptrdiff_t>(
                                                 Stream.size() < SignatureExtent ? Stream.size() : SignatureExtent));
    const ImageContentSubject        Classified = ClassifyContent(Leading);

    if (Classified == ImageContentSubject::Unrecognised)
    {
        return Deliver<DecodedImage>::Refuse(
            { RefusalReason::ContentUnsupported, "the stream's leading bytes name no accepted image layout — `10` §1" });
    }

    const stbi_uc* const  Reading  = Stream.data();
    const int             Spanned  = static_cast<int>(Stream.size());

    int  DecodedWidth     = 0;
    int  DecodedHeight    = 0;
    int  DecodedComponents = 0;

    DecodedImage  Produced;

    // 📝 Which of the three reads is taken is decided by what the stream declares, never by what the caller
    //    would prefer. A sixteen-bit source read through the eight-bit path decodes without complaint and
    //    loses half its precision on the way in, with nothing downstream able to say where it went.
    if (Classified == ImageContentSubject::Radiance || stbi_is_hdr_from_memory(Reading, Spanned) != 0)
    {
        float* const Decoded = stbi_loadf_from_memory(Reading, Spanned, &DecodedWidth, &DecodedHeight, &DecodedComponents, 0);

        if (Decoded == nullptr)
        {
            return Deliver<DecodedImage>::Refuse(
                { RefusalReason::ContentUnsupported, "the decoder rejected the floating-point image stream" });
        }

        const std::size_t Occupied = static_cast<std::size_t>(DecodedWidth)
                                   * static_cast<std::size_t>(DecodedHeight)
                                   * static_cast<std::size_t>(DecodedComponents)
                                   * sizeof(float);

        Produced.Original.resize(Occupied);
        std::memcpy(Produced.Original.data(), Decoded, Occupied);
        stbi_image_free(Decoded);

        Produced.BitDepth = 32u;
    }
    else if (stbi_is_16_bit_from_memory(Reading, Spanned) != 0)
    {
        stbi_us* const Decoded = stbi_load_16_from_memory(Reading, Spanned, &DecodedWidth, &DecodedHeight, &DecodedComponents, 0);

        if (Decoded == nullptr)
        {
            return Deliver<DecodedImage>::Refuse(
                { RefusalReason::ContentUnsupported, "the decoder rejected the sixteen-bit image stream" });
        }

        const std::size_t Occupied = static_cast<std::size_t>(DecodedWidth)
                                   * static_cast<std::size_t>(DecodedHeight)
                                   * static_cast<std::size_t>(DecodedComponents)
                                   * sizeof(stbi_us);

        Produced.Original.resize(Occupied);
        std::memcpy(Produced.Original.data(), Decoded, Occupied);
        stbi_image_free(Decoded);

        Produced.BitDepth = 16u;
    }
    else
    {
        stbi_uc* const Decoded = stbi_load_from_memory(Reading, Spanned, &DecodedWidth, &DecodedHeight, &DecodedComponents, 0);

        if (Decoded == nullptr)
        {
            return Deliver<DecodedImage>::Refuse(
                { RefusalReason::ContentUnsupported, "the decoder rejected the image stream" });
        }

        const std::size_t Occupied = static_cast<std::size_t>(DecodedWidth)
                                   * static_cast<std::size_t>(DecodedHeight)
                                   * static_cast<std::size_t>(DecodedComponents);

        Produced.Original.resize(Occupied);
        std::memcpy(Produced.Original.data(), Decoded, Occupied);
        stbi_image_free(Decoded);

        Produced.BitDepth = 8u;
    }

    if (DecodedWidth <= 0 || DecodedHeight <= 0 || DecodedComponents <= 0)
    {
        return Deliver<DecodedImage>::Refuse(
            { RefusalReason::ContentUnsupported, "the decoded image carries no extent or no component" });
    }

    Produced.OriginPath     = OriginPath;
    Produced.Width          = static_cast<std::uint32_t>(DecodedWidth);
    Produced.Height         = static_cast<std::uint32_t>(DecodedHeight);
    Produced.ComponentCount = static_cast<std::uint32_t>(DecodedComponents);

    // 🔴 `10` §1's gate is that every decoded image declares a colour space. None of the layouts above exposes
    //    one through this decoder, so nothing is declared here and the absence is reported rather than filled.
    //    `50` §3 records the assumption at intake, where `86` presents it — which is the whole difference
    //    between a colour that is wrong for a reason anybody can find and one that is wrong for none.
    Produced.SpaceIdentity  = 0u;
    Produced.SpaceDeclared  = false;

    return Deliver<DecodedImage>::Result(Produced);
}

}   // namespace Slate
