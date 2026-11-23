//============================================================================================================================================
//                                                             SHADERCODEC.CPP
//============================================================================================================================================
// 🧩 The whole-file read, the stream verification that refuses before the vendor sees it, and the held specialisation.

#include "SlateVulkan/Device/ShaderCodec/Api/ShaderCodec.h"
#include "SlateMath/Platform/FileInterchange/Api/FileInterchange.h"

namespace Slate
{

//------------------------------------------------------------------------------------------------------------------------
//                                                     CONSTRUCTION
//------------------------------------------------------------------------------------------------------------------------

Deliver<bool> ShaderCodec::AttachShaderStreams(const VulkanExchange& Exchange, const std::string& StreamDirectory)
{
    if (Exchange.ActiveDevice() == VK_NULL_HANDLE)
        return Deliver<bool>::Refuse({ RefusalReason::CapabilityAbsent, "no device is active" });

    DeviceEdge = &Exchange;
    StreamRoot = StreamDirectory;

    // 📝 A trailing separator is appended once here rather than at every path assembly below, so that a caller
    //    passing either spelling reaches the same file.
    if (!StreamRoot.empty() && StreamRoot.back() != '\\' && StreamRoot.back() != '/')
        StreamRoot.push_back('\\');

    return Deliver<bool>::Result(true);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                       THE READ
//------------------------------------------------------------------------------------------------------------------------

Deliver<std::vector<std::uint32_t>> ShaderCodec::ReadStream(const std::string& StreamPath) const
{
    // 📝 Read through `04` §1's `FileInterchange`, which is the one stream surface over three file systems.
    //    A whole-file read of a build product is exactly what that surface delivers, and reading around it
    //    here would put a fourth spelling of "open a file" in the unit furthest from the file system.
    const Deliver<std::vector<std::uint8_t>> Read = FileInterchange::ReadStream(StreamPath);

    if (!Read.Resolved)
    {
        return Deliver<std::vector<std::uint32_t>>::Refuse(
            { RefusalReason::HostDenied, "the lowered stream could not be read; was the shader stage run" });
    }

    const std::vector<std::uint8_t>& ReadBack = Read.Resolve();

    if (ReadBack.empty())
    {
        return Deliver<std::vector<std::uint32_t>>::Refuse(
            { RefusalReason::ContentUnsupported, "the lowered stream is empty" });
    }

    // 🔴 A whole word count or nothing. SPIR-V is a run of 32-bit words by definition, and a stream whose
    //    length is not a multiple of four was truncated — the vendor reads the partial word as an instruction
    //    and reports a malformed module, which names the driver rather than the truncated file.
    if ((ReadBack.size() % sizeof(std::uint32_t)) != 0u)
    {
        return Deliver<std::vector<std::uint32_t>>::Refuse(
            { RefusalReason::ContentUnsupported, "the lowered stream is not a whole count of words" });
    }

    // 📝 Copied word by word rather than reinterpreted in place. The byte extent carries no alignment
    //    guarantee for a 32-bit read, and the vendor takes a word pointer — a cast would be undefined on
    //    every host and merely happen to work on the two that tolerate a misaligned load.
    std::vector<std::uint32_t> Words(ReadBack.size() / sizeof(std::uint32_t), 0u);

    for (std::size_t Index = 0u; Index < Words.size(); ++Index)
    {
        const std::size_t Byte = Index * sizeof(std::uint32_t);

        Words[Index] = static_cast<std::uint32_t>(ReadBack[Byte])
                       | (static_cast<std::uint32_t>(ReadBack[Byte + 1u]) << 8)
                       | (static_cast<std::uint32_t>(ReadBack[Byte + 2u]) << 16)
                       | (static_cast<std::uint32_t>(ReadBack[Byte + 3u]) << 24);
    }

    if (Words[0] != SpirvStreamMarker)
    {
        return Deliver<std::vector<std::uint32_t>>::Refuse(
            { RefusalReason::ContentUnsupported, "the stream carries no SPIR-V marker" });
    }

    return Deliver<std::vector<std::uint32_t>>::Result(Words);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    THE RESOLUTION
//------------------------------------------------------------------------------------------------------------------------

Deliver<std::uint32_t> ShaderCodec::Resolve(const std::string& UnitName, const std::string& StreamStem)
{
    if (DeviceEdge == nullptr)
        return Deliver<std::uint32_t>::Refuse({ RefusalReason::CapabilityAbsent, "no device is active" });

    if (UnitName.empty() || StreamStem.empty())
        return Deliver<std::uint32_t>::Refuse({ RefusalReason::ContentUnsupported, "a stream naming no unit or no stem" });

    // 📝 A stream already read is delivered rather than read again. Several programs are constructed against
    //    one module — `16`'s two raster paths share their entry point — and reading it per program would
    //    construct a second vendor module carrying the same instructions.
    for (std::size_t Index = 0u; Index < Modules.size(); ++Index)
    {
        if (Modules[Index].UnitName == UnitName && Modules[Index].StreamStem == StreamStem)
            return Deliver<std::uint32_t>::Result(static_cast<std::uint32_t>(Index));
    }

    const std::string StreamPath = StreamRoot + UnitName + "\\" + StreamStem + ".spv";

    const Deliver<std::vector<std::uint32_t>> Words = ReadStream(StreamPath);

    if (!Words.Resolved)
        return Deliver<std::uint32_t>::Refuse(Words.Error);

    const std::vector<std::uint32_t> Stream = Words.Resolve();

    VkShaderModuleCreateInfo ModuleDeclaration = {};
    ModuleDeclaration.sType                    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    ModuleDeclaration.codeSize                 = Stream.size() * sizeof(std::uint32_t);
    ModuleDeclaration.pCode                    = Stream.data();

    HeldModule Incoming;
    Incoming.UnitName   = UnitName;
    Incoming.StreamStem = StreamStem;

    if (vkCreateShaderModule(DeviceEdge->ActiveDevice(), &ModuleDeclaration, nullptr, &Incoming.Constructed)
        != VK_SUCCESS)
    {
        return Deliver<std::uint32_t>::Refuse(
            { RefusalReason::ContentUnsupported, "the device rejected the lowered stream as a module" });
    }

    Modules.push_back(Incoming);

    return Deliver<std::uint32_t>::Result(static_cast<std::uint32_t>(Modules.size() - 1u));
}

//------------------------------------------------------------------------------------------------------------------------
//                                                      THE STAGE
//------------------------------------------------------------------------------------------------------------------------

Deliver<VkPipelineShaderStageCreateInfo> ShaderCodec::Stage(std::uint32_t                            ModuleIndex,
                                                            VkShaderStageFlagBits                    Reading,
                                                            const std::vector<SpecialisedConstant>&  Fixed)
{
    if (static_cast<std::size_t>(ModuleIndex) >= Modules.size())
    {
        return Deliver<VkPipelineShaderStageCreateInfo>::Refuse(
            { RefusalReason::ContentUnsupported, "no module stands at that ordinal" });
    }

    VkPipelineShaderStageCreateInfo StageDeclaration = {};
    StageDeclaration.sType                           = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    StageDeclaration.stage                           = Reading;
    StageDeclaration.module                          = Modules[ModuleIndex].Constructed;

    // 📝 🔴 The entry point is the one slangc emitted, and slangc emits the name the `[shader(...)]` attribute
    //    carried. Slate's entry points sit inside `namespace Slate`, which is exactly why the build passes no
    //    `-entry` — the attribute names them, and the emitted name is the unqualified one.
    StageDeclaration.pName = "main";

    if (Fixed.empty())
        return Deliver<VkPipelineShaderStageCreateInfo>::Result(StageDeclaration);

    HeldSpecialisation Held;
    Held.Declared.reserve(Fixed.size());
    Held.Fixed.reserve(Fixed.size());

    for (const SpecialisedConstant& Constant : Fixed)
    {
        VkSpecializationMapEntry Entry = {};
        Entry.constantID               = Constant.ConstantIndex;
        Entry.offset                   = static_cast<std::uint32_t>(Held.Fixed.size() * sizeof(std::uint32_t));
        Entry.size                     = sizeof(std::uint32_t);

        Held.Declared.push_back(Entry);
        Held.Fixed.push_back(Constant.Fixed);
    }

    Specialisations.push_back(Held);

    HeldSpecialisation& Current = Specialisations.back();

    Current.Read.mapEntryCount = static_cast<std::uint32_t>(Current.Declared.size());
    Current.Read.pMapEntries   = Current.Declared.data();
    Current.Read.dataSize      = Current.Fixed.size() * sizeof(std::uint32_t);
    Current.Read.pData         = Current.Fixed.data();

    StageDeclaration.pSpecializationInfo = &Current.Read;

    return Deliver<VkPipelineShaderStageCreateInfo>::Result(StageDeclaration);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     RECLAMATION
//------------------------------------------------------------------------------------------------------------------------

std::uint32_t ShaderCodec::ResolvedCount() const
{
    return static_cast<std::uint32_t>(Modules.size());
}

void ShaderCodec::Reclaim()
{
    if (DeviceEdge != nullptr && DeviceEdge->ActiveDevice() != VK_NULL_HANDLE)
    {
        for (HeldModule& Held : Modules)
        {
            if (Held.Constructed != VK_NULL_HANDLE)
            {
                vkDestroyShaderModule(DeviceEdge->ActiveDevice(), Held.Constructed, nullptr);
                Held.Constructed = VK_NULL_HANDLE;
            }
        }
    }

    Modules.clear();
    Specialisations.clear();
}

ShaderCodec::~ShaderCodec()
{
    Reclaim();
}

}   // namespace Slate
