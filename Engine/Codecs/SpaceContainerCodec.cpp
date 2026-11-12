/*============================================================================================================================================
                                                          SPACECONTAINERCODEC.CPP
============================================================================================================================================*/
// 🧩 Two-Tier Hybrid .Space container encoder & decoder implementation (Option 1A + Option 2A, 64B alignment, FNV-1a)

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Codecs/SpaceContainerCodec.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace Frontier::Codecs
{

namespace
{

constexpr std::string_view BinaryBoundaryMarker = "\n# === FRONTIER_BINARY_BOUNDARY ===\n";

struct ExtensionMappingEntry
{
    SpaceExtensionCategory Category;
    std::string_view       CanonicalExtension;
    std::string_view       LowercaseExtension;
};

constexpr std::array<ExtensionMappingEntry, 20u> ExtensionRegistry = {{
    { SpaceExtensionCategory::ProjectSpace,    ".ProjectSpace",    ".projectspace"    },
    { SpaceExtensionCategory::ColourSpace,     ".ColourSpace",     ".colourspace"     },
    { SpaceExtensionCategory::GeometrySpace,   ".GeometrySpace",   ".geometryspace"   },
    { SpaceExtensionCategory::FontSpace,       ".FontSpace",       ".fontspace"       },
    { SpaceExtensionCategory::TextureSpace,    ".TextureSpace",    ".texturespace"    },
    { SpaceExtensionCategory::MaterialSpace,   ".MaterialSpace",   ".materialspace"   },
    { SpaceExtensionCategory::ImageSpace,      ".ImageSpace",      ".imagespace"      },
    { SpaceExtensionCategory::VectorSpace,     ".VectorSpace",     ".vectorspace"     },
    { SpaceExtensionCategory::SimulationSpace, ".SimulationSpace", ".simulationspace" },
    { SpaceExtensionCategory::VolumetricSpace, ".VolumetricSpace", ".volumetricspace" },
    { SpaceExtensionCategory::ParticleSpace,   ".ParticleSpace",   ".particlespace"   },
    { SpaceExtensionCategory::UVSpace,         ".UVSpace",         ".uvspace"         },
    { SpaceExtensionCategory::Pigment,         ".Pigment",         ".pigment"         },
    { SpaceExtensionCategory::Sketch,          ".Sketch",          ".sketch"          },
    { SpaceExtensionCategory::Assembly,        ".Assembly",        ".assembly"        },
    { SpaceExtensionCategory::Motion,          ".Motion",          ".motion"          },
    { SpaceExtensionCategory::Render,          ".Render",          ".render"          },
    { SpaceExtensionCategory::Photometry,      ".Photometry",      ".photometry"      },
    { SpaceExtensionCategory::Terrain,         ".Terrain",         ".terrain"         },
    { SpaceExtensionCategory::Acoustic,        ".Acoustic",        ".acoustic"        },
}};

struct SubFormatMappingEntry
{
    std::string_view CanonicalTag;
    std::string_view LowercaseTag;
};

constexpr std::array<SubFormatMappingEntry, 21u> SubFormatRegistry = {{
    { ".DocumentHeader",   ".documentheader"   },
    { ".SectionDirectory", ".sectiondirectory" },
    { ".Revision",         ".revision"         },
    { ".Baseline",         ".baseline"         },
    { ".UDIM",             ".udim"             },
    { ".UVTile",           ".uvtile"           },
    { ".Swatch",           ".swatch"           },
    { ".Ramp",             ".ramp"             },
    { ".Spectrum",         ".spectrum"         },
    { ".Kerning",          ".kerning"          },
    { ".CharacterSet",     ".characterset"     },
    { ".TexelLayer",       ".texellayer"       },
    { ".ChannelPack",      ".channelpack"      },
    { ".ShadingModel",     ".shadingmodel"     },
    { ".ParameterSlot",    ".parameterslot"    },
    { ".CanvasLayer",      ".canvaslayer"      },
    { ".LayerStackOrder",  ".layerstackorder"  },
    { ".PathCurve",        ".pathcurve"        },
    { ".StrokeAppearance", ".strokeappearance" },
    { ".ProjectSettings",  ".projectsettings"  },
    { ".DependencyLock",   ".dependencylock"   },
}};

[[nodiscard]] std::string ToAsciiLower(std::string_view Text)
{
    std::string Result(Text);
    std::transform(
        Result.begin(),
        Result.end(),
        Result.begin(),
        [](unsigned char Ch) noexcept { return static_cast<char>((Ch >= 'A' && Ch <= 'Z') ? (Ch + ('a' - 'A')) : Ch); });
    return Result;
}

[[nodiscard]] constexpr std::size_t Align64(std::size_t Offset) noexcept
{
    const std::size_t Alignment = Constants::ContainerBinaryAlignmentBytes;
    return (Offset + (Alignment - 1u)) & ~(Alignment - 1u);
}

[[nodiscard]] std::string FormatHex64(uint64_t Hash)
{
    std::ostringstream Stream;
    Stream << "0x" << std::hex << std::uppercase << std::setw(16) << std::setfill('0') << Hash;
    return Stream.str();
}

[[nodiscard]] bool ParseHex64(std::string_view HexText, uint64_t& OutHash) noexcept
{
    std::string_view Trimmed = HexText;
    if (Trimmed.size() >= 2u && Trimmed[0] == '0' && (Trimmed[1] == 'x' || Trimmed[1] == 'X'))
    {
        Trimmed.remove_prefix(2u);
    }
    if (Trimmed.empty() || Trimmed.size() > 16u)
    {
        return false;
    }

    uint64_t Accumulator = 0ull;
    for (char Ch : Trimmed)
    {
        uint64_t Nibble = 0ull;
        if (Ch >= '0' && Ch <= '9')
        {
            Nibble = static_cast<uint64_t>(Ch - '0');
        }
        else if (Ch >= 'a' && Ch <= 'f')
        {
            Nibble = static_cast<uint64_t>(10 + (Ch - 'a'));
        }
        else if (Ch >= 'A' && Ch <= 'F')
        {
            Nibble = static_cast<uint64_t>(10 + (Ch - 'A'));
        }
        else
        {
            return false;
        }
        Accumulator = (Accumulator << 4u) | Nibble;
    }
    OutHash = Accumulator;
    return true;
}

[[nodiscard]] bool NormalizeManifestKeysRecursive(
    const toml::table& SourceManifest,
    toml::table&       OutNormalizedManifest,
    std::string&       OutDiagnostic)
{
    for (const auto& [Key, Entry] : SourceManifest)
    {
        const std::string_view RawKey = Key.str();
        std::string            FinalKey(RawKey);
        if (!RawKey.empty() && RawKey.front() == '.')
        {
            if (!SpaceContainerEncoder::NormalizeSubFormatTag(RawKey, FinalKey, OutDiagnostic))
            {
                return false;
            }
        }
        else
        {
            const std::string LoweredKey = ToAsciiLower(RawKey);
            if (LoweredKey == "uvchart" || LoweredKey == "surface")
            {
                OutDiagnostic = "Prohibited legacy domain key encountered in Tier 1 manifest: " + std::string(RawKey);
                return false;
            }
        }

        if (const toml::table* SubManifest = Entry.as_table())
        {
            toml::table NormalizedNested;
            if (!NormalizeManifestKeysRecursive(*SubManifest, NormalizedNested, OutDiagnostic))
            {
                return false;
            }
            OutNormalizedManifest.insert_or_assign(FinalKey, std::move(NormalizedNested));
        }
        else if (const toml::array* SubArray = Entry.as_array())
        {
            toml::array NormalizedArray;
            for (const auto& ArrayEntry : *SubArray)
            {
                if (const toml::table* NestedManifest = ArrayEntry.as_table())
                {
                    toml::table NormalizedNested;
                    if (!NormalizeManifestKeysRecursive(*NestedManifest, NormalizedNested, OutDiagnostic))
                    {
                        return false;
                    }
                    NormalizedArray.push_back(std::move(NormalizedNested));
                }
                else
                {
                    NormalizedArray.push_back(ArrayEntry);
                }
            }
            OutNormalizedManifest.insert_or_assign(FinalKey, std::move(NormalizedArray));
        }
        else
        {
            OutNormalizedManifest.insert_or_assign(FinalKey, Entry);
        }
    }
    return true;
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                  ENCODER OPERATIONS
//------------------------------------------------------------------------------------------------------------------------

uint64_t SpaceContainerEncoder::ComputeFnv1aChecksum64(std::span<const std::byte> Bytes) noexcept
{
    constexpr uint64_t OffsetBasis = 14695981039346656037ull;
    constexpr uint64_t Prime       = 1099511628211ull;
    uint64_t           Digest      = OffsetBasis;
    for (const std::byte SingleByte : Bytes)
    {
        Digest ^= static_cast<uint64_t>(std::to_integer<uint8_t>(SingleByte));
        Digest *= Prime;
    }
    return Digest;
}

std::string_view SpaceContainerEncoder::QueryCanonicalExtension(SpaceExtensionCategory Category) noexcept
{
    for (const ExtensionMappingEntry& Mapping : ExtensionRegistry)
    {
        if (Mapping.Category == Category)
        {
            return Mapping.CanonicalExtension;
        }
    }
    return ".ProjectSpace";
}

bool SpaceContainerEncoder::NormalizeSubFormatTag(
    std::string_view RawTag,
    std::string&     OutCanonicalTag,
    std::string&     OutDiagnostic)
{
    const std::string Lowered = ToAsciiLower(RawTag);
    if (Lowered == ".uvchart" || Lowered == "uvchart")
    {
        OutDiagnostic = "Sub-format .UVChart is prohibited; use .UVTile.";
        return false;
    }
    if (Lowered == ".surface" || Lowered == "surface")
    {
        OutDiagnostic = "Sub-format .Surface is prohibited by Frontier naming governance.";
        return false;
    }

    for (const SubFormatMappingEntry& Entry : SubFormatRegistry)
    {
        if (Lowered == Entry.LowercaseTag)
        {
            OutCanonicalTag = std::string(Entry.CanonicalTag);
            return true;
        }
    }

    if (RawTag.empty() || RawTag.front() != '.')
    {
        OutDiagnostic = "Embedded sub-format tag must begin with '.': " + std::string(RawTag);
        return false;
    }

    OutCanonicalTag = std::string(RawTag);
    return true;
}

bool SpaceContainerEncoder::EncodeToBytes(
    const SpaceContainerPayload& Payload,
    std::vector<std::byte>&      OutContainerBytes,
    std::string&                 OutDiagnostic)
{
    OutDiagnostic.clear();
    OutContainerBytes.clear();

    toml::table RootManifest;
    if (!NormalizeManifestKeysRecursive(Payload.DomainManifest, RootManifest, OutDiagnostic))
    {
        return false;
    }

    toml::table HeaderSection;
    HeaderSection.insert_or_assign("ContainerExtension", std::string(QueryCanonicalExtension(Payload.ExtensionCategory)));
    HeaderSection.insert_or_assign("SchemaVersion", static_cast<int64_t>(Payload.SchemaVersion));
    HeaderSection.insert_or_assign("DocumentTitle", Payload.DocumentTitle);
    HeaderSection.insert_or_assign("AuthoringWorkspace", Payload.AuthoringWorkspace);
    HeaderSection.insert_or_assign("BinaryAlignmentBytes", static_cast<int64_t>(Constants::ContainerBinaryAlignmentBytes));
    RootManifest.insert_or_assign(".DocumentHeader", std::move(HeaderSection));

    toml::array DirectoryArray;
    std::size_t RelativeAlignedCursor = 0u;
    for (const BinarySectionBlock& Section : Payload.BinarySections)
    {
        if (Section.SectionIdentifier.empty())
        {
            OutDiagnostic = "BinarySectionBlock requires a non-empty SectionIdentifier.";
            return false;
        }

        std::string CanonicalSubTag;
        if (!NormalizeSubFormatTag(Section.SubFormatTag, CanonicalSubTag, OutDiagnostic))
        {
            return false;
        }

        const uint64_t    SectionDigest = ComputeFnv1aChecksum64(Section.PayloadBytes);
        const std::size_t AlignedOffset = Align64(RelativeAlignedCursor);

        toml::table DirectoryEntry;
        DirectoryEntry.insert_or_assign("SectionIdentifier", Section.SectionIdentifier);
        DirectoryEntry.insert_or_assign("SubFormatTag", CanonicalSubTag);
        DirectoryEntry.insert_or_assign("RelativeAlignedOffset", static_cast<int64_t>(AlignedOffset));
        DirectoryEntry.insert_or_assign("ByteLength", static_cast<int64_t>(Section.PayloadBytes.size()));
        DirectoryEntry.insert_or_assign("ChecksumFnv1a64", FormatHex64(SectionDigest));
        DirectoryArray.push_back(std::move(DirectoryEntry));

        RelativeAlignedCursor = Align64(AlignedOffset + Section.PayloadBytes.size());
    }

    if (!DirectoryArray.empty())
    {
        RootManifest.insert_or_assign(".SectionDirectory", std::move(DirectoryArray));
    }

    std::ostringstream ManifestStream;
    ManifestStream << "# Frontier Two-Tier Hybrid .Space Container (Canonical PascalCase Manifest)\n";
    ManifestStream << RootManifest;
    ManifestStream << BinaryBoundaryMarker;
    const std::string Tier1Text = ManifestStream.str();

    const std::size_t Tier2StartOffset = Align64(Tier1Text.size());
    const std::size_t TotalByteSize   = Tier2StartOffset + RelativeAlignedCursor;
    OutContainerBytes.assign(TotalByteSize, std::byte{ 0 });
    std::memcpy(OutContainerBytes.data(), Tier1Text.data(), Tier1Text.size());

    std::size_t WriteRelativeOffset = 0u;
    for (const BinarySectionBlock& Section : Payload.BinarySections)
    {
        const std::size_t AlignedOffset  = Align64(WriteRelativeOffset);
        const std::size_t AbsoluteOffset = Tier2StartOffset + AlignedOffset;
        if (!Section.PayloadBytes.empty())
        {
            std::memcpy(
                OutContainerBytes.data() + AbsoluteOffset,
                Section.PayloadBytes.data(),
                Section.PayloadBytes.size());
        }
        WriteRelativeOffset = Align64(AlignedOffset + Section.PayloadBytes.size());
    }

    return true;
}

bool SpaceContainerEncoder::EncodeToFile(
    const std::filesystem::path& TargetPath,
    const SpaceContainerPayload& Payload,
    std::string&                 OutDiagnostic)
{
    SpaceExtensionCategory PathCategory{};
    std::string            CanonicalExt;
    if (!SpaceContainerDecoder::ResolveExtensionCaseInsensitive(
            TargetPath.extension().string(),
            PathCategory,
            CanonicalExt,
            OutDiagnostic))
    {
        return false;
    }

    if (PathCategory != Payload.ExtensionCategory)
    {
        OutDiagnostic = "Target file extension (" + CanonicalExt + ") does not match payload category (" +
                        std::string(QueryCanonicalExtension(Payload.ExtensionCategory)) + ").";
        return false;
    }

    std::vector<std::byte> EncodedBytes;
    if (!EncodeToBytes(Payload, EncodedBytes, OutDiagnostic))
    {
        return false;
    }

    std::error_code FsError;
    if (TargetPath.has_parent_path())
    {
        std::filesystem::create_directories(TargetPath.parent_path(), FsError);
    }

    std::ofstream Output(TargetPath, std::ios::binary | std::ios::trunc);
    if (!Output.is_open())
    {
        OutDiagnostic = "Unable to open .Space container path for writing: " + TargetPath.string();
        return false;
    }

    if (!EncodedBytes.empty())
    {
        Output.write(
            reinterpret_cast<const char*>(EncodedBytes.data()),
            static_cast<std::streamsize>(EncodedBytes.size()));
    }
    if (!Output.good())
    {
        OutDiagnostic = "I/O failure while writing .Space container: " + TargetPath.string();
        return false;
    }
    return true;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  DECODER OPERATIONS
//------------------------------------------------------------------------------------------------------------------------

bool SpaceContainerDecoder::ResolveExtensionCaseInsensitive(
    std::string_view        RawExtension,
    SpaceExtensionCategory& OutCategory,
    std::string&            OutCanonicalExtension,
    std::string&            OutDiagnostic)
{
    const std::string Lowered = ToAsciiLower(RawExtension);
    if (Lowered == ".toml")
    {
        OutDiagnostic = "Raw .toml persistence files are prohibited; use a canonical .Space extension.";
        return false;
    }
    if (Lowered == ".surface" || Lowered == ".uvchart")
    {
        OutDiagnostic = "Prohibited legacy extension rejected: " + std::string(RawExtension);
        return false;
    }

    for (const ExtensionMappingEntry& Mapping : ExtensionRegistry)
    {
        if (Lowered == Mapping.LowercaseExtension)
        {
            OutCategory           = Mapping.Category;
            OutCanonicalExtension = std::string(Mapping.CanonicalExtension);
            return true;
        }
    }

    OutDiagnostic = "Unrecognized .Space container extension: " + std::string(RawExtension);
    return false;
}

bool SpaceContainerDecoder::DecodeFromBytes(
    std::span<const std::byte> InputBytes,
    SpaceContainerPayload&     OutPayload,
    std::string&               OutDiagnostic)
{
    OutDiagnostic.clear();
    OutPayload = SpaceContainerPayload{};
    if (InputBytes.empty())
    {
        OutDiagnostic = "Cannot decode an empty .Space byte buffer.";
        return false;
    }

    const std::string_view RawView(
        reinterpret_cast<const char*>(InputBytes.data()),
        InputBytes.size());
    const std::size_t MarkerOffset = RawView.find(BinaryBoundaryMarker);
    const std::size_t Tier1End     = MarkerOffset != std::string_view::npos ? MarkerOffset : InputBytes.size();
    const std::size_t Tier2Start    = MarkerOffset != std::string_view::npos
                                         ? Align64(MarkerOffset + BinaryBoundaryMarker.size())
                                         : Align64(InputBytes.size());

    toml::table ParsedManifest;
    try
    {
        ParsedManifest = toml::parse(RawView.substr(0u, Tier1End));
    }
    catch (const toml::parse_error& Error)
    {
        OutDiagnostic = std::string("Tier 1 TOML manifest parse failure: ") + std::string(Error.description());
        return false;
    }

    toml::table NormalizedManifest;
    if (!NormalizeManifestKeysRecursive(ParsedManifest, NormalizedManifest, OutDiagnostic))
    {
        return false;
    }

    const toml::table* Header = NormalizedManifest[".DocumentHeader"].as_table();
    if (Header == nullptr)
    {
        OutDiagnostic = "Tier 1 manifest is missing required ['.DocumentHeader'] section.";
        return false;
    }

    const std::string RawContainerExt = (*Header)["ContainerExtension"].value_or(std::string{});
    std::string       CanonicalExt;
    if (!ResolveExtensionCaseInsensitive(RawContainerExt, OutPayload.ExtensionCategory, CanonicalExt, OutDiagnostic))
    {
        return false;
    }

    OutPayload.SchemaVersion      = static_cast<uint32_t>((*Header)["SchemaVersion"].value_or(int64_t{ 1 }));
    OutPayload.DocumentTitle      = (*Header)["DocumentTitle"].value_or(std::string{ "Untitled" });
    OutPayload.AuthoringWorkspace = (*Header)["AuthoringWorkspace"].value_or(std::string{ "Frontier.Workspace" });
    OutPayload.Tier2StartOffset    = Tier2Start;

    if (const toml::array* Directory = NormalizedManifest[".SectionDirectory"].as_array())
    {
        if ((Tier2Start % Constants::ContainerBinaryAlignmentBytes) != 0u || Tier2Start > InputBytes.size())
        {
            OutDiagnostic = "Tier 2 binary start offset violates 64-byte alignment or exceeds container bounds.";
            return false;
        }

        for (const auto& DirectoryEntryRef : *Directory)
        {
            const toml::table* DirEntry = DirectoryEntryRef.as_table();
            if (DirEntry == nullptr)
            {
                OutDiagnostic = "Malformed entry inside [['.SectionDirectory']].";
                return false;
            }

            const std::string SectionId   = (*DirEntry)["SectionIdentifier"].value_or(std::string{});
            const std::string RawSubTag   = (*DirEntry)["SubFormatTag"].value_or(std::string{});
            const int64_t     RelOffset   = (*DirEntry)["RelativeAlignedOffset"].value_or(int64_t{ -1 });
            const int64_t     ByteLen     = (*DirEntry)["ByteLength"].value_or(int64_t{ -1 });
            const std::string ChecksumStr = (*DirEntry)["ChecksumFnv1a64"].value_or(std::string{});

            std::string CanonicalSubTag;
            if (!SpaceContainerEncoder::NormalizeSubFormatTag(RawSubTag, CanonicalSubTag, OutDiagnostic))
            {
                return false;
            }
            if (SectionId.empty() || RelOffset < 0 || ByteLen < 0)
            {
                OutDiagnostic = "Invalid offset or length in [['.SectionDirectory']] for section: " + SectionId;
                return false;
            }

            const std::size_t AbsoluteOffset = Tier2Start + static_cast<std::size_t>(RelOffset);
            const std::size_t LengthSize     = static_cast<std::size_t>(ByteLen);
            if ((AbsoluteOffset % Constants::ContainerBinaryAlignmentBytes) != 0u)
            {
                OutDiagnostic = "Binary section violates 64-byte alignment boundary: " + SectionId;
                return false;
            }
            if (AbsoluteOffset > InputBytes.size() || LengthSize > (InputBytes.size() - AbsoluteOffset))
            {
                OutDiagnostic = "Binary section extends past end of .Space container: " + SectionId;
                return false;
            }

            uint64_t ExpectedDigest = 0ull;
            if (!ParseHex64(ChecksumStr, ExpectedDigest))
            {
                OutDiagnostic = "Malformed ChecksumFnv1a64 in [['.SectionDirectory']] for section: " + SectionId;
                return false;
            }

            const std::span<const std::byte> SectionSlice = InputBytes.subspan(AbsoluteOffset, LengthSize);
            const uint64_t                   ActualDigest = SpaceContainerEncoder::ComputeFnv1aChecksum64(SectionSlice);
            if (ActualDigest != ExpectedDigest)
            {
                OutDiagnostic = "64-bit FNV-1a checksum mismatch in binary section: " + SectionId;
                return false;
            }

            BinarySectionBlock Block{};
            Block.SectionIdentifier         = SectionId;
            Block.SubFormatTag              = CanonicalSubTag;
            Block.PayloadBytes.assign(SectionSlice.begin(), SectionSlice.end());
            Block.Checksum64                = ActualDigest;
            Block.DecodedAbsoluteByteOffset = AbsoluteOffset;
            OutPayload.BinarySections.push_back(std::move(Block));
        }
    }

    NormalizedManifest.erase(".DocumentHeader");
    NormalizedManifest.erase(".SectionDirectory");
    OutPayload.DomainManifest = std::move(NormalizedManifest);
    return true;
}

bool SpaceContainerDecoder::DecodeFromFile(
    const std::filesystem::path& SourcePath,
    SpaceContainerPayload&       OutPayload,
    std::string&                 OutDiagnostic)
{
    SpaceExtensionCategory PathCategory{};
    std::string            CanonicalExt;
    if (!ResolveExtensionCaseInsensitive(SourcePath.extension().string(), PathCategory, CanonicalExt, OutDiagnostic))
    {
        return false;
    }

    std::ifstream Input(SourcePath, std::ios::binary | std::ios::ate);
    if (!Input.is_open())
    {
        OutDiagnostic = "Unable to open .Space container for reading: " + SourcePath.string();
        return false;
    }

    const std::streamsize FileLength = Input.tellg();
    if (FileLength <= 0)
    {
        OutDiagnostic = "Empty or unreadable .Space container file: " + SourcePath.string();
        return false;
    }

    std::vector<std::byte> Buffer(static_cast<std::size_t>(FileLength));
    Input.seekg(0, std::ios::beg);
    Input.read(reinterpret_cast<char*>(Buffer.data()), FileLength);
    if (!Input.good() && !Input.eof())
    {
        OutDiagnostic = "I/O error while reading .Space container: " + SourcePath.string();
        return false;
    }

    if (!DecodeFromBytes(Buffer, OutPayload, OutDiagnostic))
    {
        return false;
    }

    if (OutPayload.ExtensionCategory != PathCategory)
    {
        OutDiagnostic = "Container manifest extension does not match file path extension (" + CanonicalExt + ").";
        return false;
    }
    return true;
}

} // namespace Frontier::Codecs
