//============================================================================================================================================
//                                                        MATERIALIMAGEIMPORT.H
//============================================================================================================================================
// 🧩 Metadata-only intake for material image references. Pixels remain external until image sampling/UV work lands.

#pragma once

#include "Foundation/DeliveryGuarantee.h"
#include "SlateDocument/Document/MaterialSpecification/Api/MaterialSpecification.h"
#include "SlateDocument/Format/CodexInterchange/Api/WorkspaceCodex.h"

#include <cstdint>
#include <string>

namespace Slate
{

enum class MaterialImageFormat : std::uint32_t
{
    Unsupported = 0u,
    Png = 1u,
    Jpeg = 2u,
    Bitmap = 3u,
    Tga = 4u,
    Webp = 5u,
    Exr = 6u
};

struct ImportedMaterialImage
{
    WorkspaceMaterialImageReference Reference = {};
    MaterialImageFormat Format = MaterialImageFormat::Unsupported;
    ChannelSubject SuggestedChannel = ChannelSubject::AlbedoColour;
};

MaterialImageFormat ClassifyMaterialImageFormat(const std::string& Path);
bool MaterialImageFormatSupported(const std::string& Path);
ChannelSubject SuggestMaterialImageChannel(const std::string& Path, bool& ColourData);
Deliver<ImportedMaterialImage> ImportMaterialImageReference(const std::string& Path);

} // namespace Slate
