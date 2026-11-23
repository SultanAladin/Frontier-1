//============================================================================================================================================
//                                                          MATERIALEXPORT.H
//============================================================================================================================================
// 🧩 Material texture-set export declarations: channel packing, target presets, and manifest generation. This does
//    not sample pixels; it describes what the export pass must write from document-owned material records.

#pragma once

#include "Foundation/DeliveryGuarantee.h"
#include "SlateDocument/Document/MaterialSpecification/Api/MaterialSpecification.h"
#include "SlateDocument/Format/CodexInterchange/Api/WorkspaceCodex.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace Slate
{

enum class MaterialExportTarget : std::uint32_t
{
    Slate = 0u,
    Blender = 1u,
    Unreal = 2u,
    Unity = 3u,
    Gltf = 4u,
    TargetCount = 5u
};

enum class MaterialExportImageFormat : std::uint32_t
{
    Png = 0u,
    Tga = 1u,
    Exr = 2u,
    FormatCount = 3u
};

enum class MaterialExportBitDepth : std::uint32_t
{
    Uint8 = 0u,
    Uint16 = 1u,
    Float32 = 2u,
    DepthCount = 3u
};

enum class MaterialExportNormalConvention : std::uint32_t
{
    OpenGl = 0u,
    DirectX = 1u
};

enum class MaterialExportLane : std::uint32_t
{
    Red = 0u,
    Green = 1u,
    Blue = 2u,
    Alpha = 3u,
    LaneCount = 4u
};

struct MaterialExportOptions
{
    MaterialExportTarget Target = MaterialExportTarget::Slate;
    MaterialExportImageFormat Format = MaterialExportImageFormat::Png;
    MaterialExportBitDepth BitDepth = MaterialExportBitDepth::Uint8;
    MaterialExportNormalConvention NormalConvention = MaterialExportNormalConvention::OpenGl;
    std::uint32_t Resolution = 2048u;
    bool Dilation = true;
    std::string OutputName = "UntitledMaterial";
    std::string OutputDirectory = "Project/Textures";
};

struct MaterialExportLaneDeclaration
{
    MaterialExportLane Lane = MaterialExportLane::Red;
    ChannelSubject Channel = ChannelSubject::ChannelCount;
    bool Invert = false;
    bool Occupied = false;
};

struct MaterialExportImageDeclaration
{
    std::string Suffix = {};
    std::array<MaterialExportLaneDeclaration, 4u> Lanes = {};
    MaterialExportImageFormat Format = MaterialExportImageFormat::Png;
    MaterialExportBitDepth BitDepth = MaterialExportBitDepth::Uint8;
    MaterialExportNormalConvention NormalConvention = MaterialExportNormalConvention::OpenGl;
    bool ColourData = false;
};

struct MaterialExportPackage
{
    std::string MaterialReference = {};
    MaterialExportOptions Options = {};
    ReflectanceSelection Reflectance = ReflectanceSelection::Standard;
    std::vector<MaterialExportImageDeclaration> Images = {};
    std::uint32_t ExportedChannelMask = 0u;
    std::uint32_t ReferencedImageCount = 0u;
    std::uint64_t MaterialFingerprint = 0u;
};

const char* MaterialExportTargetText(MaterialExportTarget Target);
const char* MaterialExportFormatExtension(MaterialExportImageFormat Format);
const char* MaterialExportChannelText(ChannelSubject Channel);

Deliver<std::vector<MaterialExportImageDeclaration>> MaterialExportPreset(MaterialExportTarget Target,
                                                                          MaterialExportImageFormat Format,
                                                                          MaterialExportBitDepth BitDepth,
                                                                          MaterialExportNormalConvention NormalConvention);

Deliver<MaterialExportPackage> BuildMaterialExportPackage(const WorkspaceMaterialRecord& Material,
                                                          const MaterialExportOptions& Options);

Deliver<std::string> EncodeMaterialExportManifest(const MaterialExportPackage& Package);

} // namespace Slate
