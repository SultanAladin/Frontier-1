//============================================================================================================================================
//                                                       MATERIALIMAGESAMPLING.H
//============================================================================================================================================
// 🧩 CPU-side imported-image material sampling for the first UV/material preview pass. This reads referenced image
//    pixels on demand and never turns a missing reference into replacement content.

#pragma once

#include "Foundation/DeliveryGuarantee.h"
#include "SlateDocument/Document/MaterialSpecification/Api/MaterialSpecification.h"
#include "SlateDocument/Format/CodexInterchange/Api/WorkspaceCodex.h"

#include <cstdint>
#include <string>
#include <vector>

namespace Slate
{

enum class MaterialImageAddressing : std::uint32_t
{
    Clamp = 0u,
    Repeat = 1u,
    Mirror = 2u
};

struct MaterialImageRaster
{
    std::string OriginPath = {};
    std::uint32_t Width = 0u;
    std::uint32_t Height = 0u;
    std::uint32_t ComponentCount = 0u;
    bool ColourData = true;
    std::vector<float> Texels = {}; // [-] - RGBA float, row-major, top-left origin
};

struct MaterialImageSampleRequest
{
    ChannelSubject Channel = ChannelSubject::AlbedoColour;
    double CoordinateU = 0.0;
    double CoordinateV = 0.0;
    MaterialImageAddressing AddressU = MaterialImageAddressing::Repeat;
    MaterialImageAddressing AddressV = MaterialImageAddressing::Repeat;
};

struct MaterialImageSample
{
    ChannelSubject Channel = ChannelSubject::ChannelCount;
    ColourSpecification Colour = {};
    double Scalar = 0.0;
    double Alpha = 1.0;
    std::uint32_t SourceIndex = 0u;
    std::uint64_t ReferenceFingerprint = 0u;
    bool ColourSample = true;
};

struct MaterialImageSamplingCapabilities
{
    bool BitmapUncompressed = true;
    bool TgaUncompressed = true;
    bool PngDecoded = true;
    bool JpegDecoded = true;
    bool WebpDecoded = true;
    bool ExrDecoded = true;
    bool ExternalDecoder = true;
};

class MaterialImageSampling
{
public:
    Deliver<MaterialImageRaster> OpenReference(const WorkspaceMaterialImageReference& Reference) const;

    Deliver<MaterialImageSample> SampleReference(const WorkspaceMaterialImageReference& Reference,
                                                 ChannelSubject Channel,
                                                 double CoordinateU,
                                                 double CoordinateV,
                                                 MaterialImageAddressing AddressU = MaterialImageAddressing::Repeat,
                                                 MaterialImageAddressing AddressV = MaterialImageAddressing::Repeat) const;

    Deliver<MaterialImageSample> SampleMaterialChannel(const WorkspaceMaterialRecord& Material,
                                                       const MaterialImageSampleRequest& Request) const;

    MaterialImageSamplingCapabilities Capabilities() const;
};

std::uint64_t FingerprintMaterialImageReference(const WorkspaceMaterialImageReference& Reference);

} // namespace Slate
