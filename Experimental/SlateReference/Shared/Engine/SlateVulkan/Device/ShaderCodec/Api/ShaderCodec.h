//============================================================================================================================================
//                                                              SHADERCODEC.H
//============================================================================================================================================
// 🧩 Lowered shader streams — read once, verified as SPIR-V, held as vendor modules and specialised at construction.

#pragma once

#include "Foundation/DeliveryGuarantee.h"
#include "SlateVulkan/Device/VulkanExchange/Api/VulkanExchange.h"

#include <vulkan/vulkan.h>

#include <cstdint>
#include <deque>
#include <string>
#include <vector>

namespace Slate
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      THE STREAM
//------------------------------------------------------------------------------------------------------------------------

// 📝 No module; never a valid module ordinal. Sibling of `ByteSpace`'s `AbsentExtent`.
inline constexpr std::uint32_t AbsentModule = 0xFFFFFFFFu;   // [-] - the claim names no module

// 📝 🔴 The first word of every SPIR-V stream, as the specification fixes it. A stream whose first word is
//    the byte-reversed spelling was written by a differently-ordered host and is rejected rather than swapped:
//    the toolchain that produced it is the build's own, so a reversed stream means the wrong file was read.
inline constexpr std::uint32_t SpirvStreamMarker = 0x07230203u;   // [-] - the magic number, verbatim

/// 🧩 One constant the module is specialised with at construction rather than read at execution.
/// note  🔴 `06` §2.1 accepts specialisation because a workgroup extent read from a span is one the vendor
///       cannot fold into its own scheduling. `16`'s partition extent and `20`'s tile extent are both of that
///       character — fixed for the run and read on every invocation.
/// tag   nonallocating, nonthrowing
struct SpecialisedConstant
{
    std::uint32_t  ConstantIndex = 0u;   // [-] - the shader's constant_id
    std::uint32_t  Fixed           = 0u;   // [-] - what it is fixed to; widths above 32 bits are declared as two
};

//------------------------------------------------------------------------------------------------------------------------
//                                                   THE MODULE INDEX
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 Every lowered shader the engine holds, read from what the build lowered and constructed once.
/// note  📝 Every stream is read through `04` §1's `FileInterchange`. The read is a whole-file one of a build
///       product, which is what that surface delivers; nothing here opens a file itself.
/// note  ⚠️ A module is destroyed once every program constructed against it stands. It is held here for the
///       run regardless, because `06` §4.2's recovery reconstructs every program and a module discarded at
///       bring-up would have to be read from disk again inside the recovery.
/// tag   owning
class ShaderCodec
{
public:

    ShaderCodec()                              = default;
    ShaderCodec(const ShaderCodec&)            = delete;
    ShaderCodec& operator=(const ShaderCodec&) = delete;
    ~ShaderCodec();

    /// 🧩 Takes the device and the directory the build lowered its streams into.
    /// in    Exchange    [-]  the created device; borrowed and outlives this component
    /// in    StreamDirectory [-]  where `<Unit>/<Stem>.spv` is found; the build's output, never a source directory
    /// out   Result         [-]  refuses with CapabilityAbsent when no device is active
    /// cost  ✔️
    /// tag   api, nonthrowing
    Deliver<bool> AttachShaderStreams(const VulkanExchange& Exchange, const std::string& StreamDirectory);

    /// 🧩 Reads one lowered stream, verifies it, and constructs the vendor module from it.
    /// in    UnitName    [-]  the unit the stream was lowered under, for example "SlateVulkan"
    /// in    StreamStem  [-]  the source's stem without its extension, for example "VisibilitySurface"
    /// out   Result     [-]  the module ordinal; refuses with HostDenied when the stream cannot be read,
    ///                        ContentUnsupported when it is not SPIR-V or its length is not a whole word count
    /// note  A stream already read is not read twice; the standing ordinal is delivered instead.
    /// cost  🔴
    /// tag   api, nonthrowing
    Deliver<std::uint32_t> Resolve(const std::string& UnitName, const std::string& StreamStem);

    /// 🧩 The stage declaration one module supplies to a program, with its specialisation folded in.
    /// in    ModuleIndex [-]  a module this component resolved
    /// in    Reading       [-]  which stage the module is read as
    /// in    Fixed         [-]  the constants; empty declares no specialisation
    /// out   Result       [-]  refuses with ContentUnsupported for an unresolved ordinal
    /// note  🔴 The specialisation declaration is held **here** and not returned by value. The vendor reads it
    ///       at the program's construction, and a declaration returned by value is read after the call that
    ///       produced it has already surrendered its stack.
    /// cost  ✔️
    /// tag   api, nonthrowing
    Deliver<VkPipelineShaderStageCreateInfo> Stage(std::uint32_t                             ModuleIndex,
                                                   VkShaderStageFlagBits                     Reading,
                                                   const std::vector<SpecialisedConstant>&   Fixed);

    /// 🧩 Destroys every module and every specialisation held for it.
    /// pre   the device is idle and every program constructed against them stands or is itself reclaimed
    /// cost  🚩
    /// tag   api, nonthrowing
    void Reclaim();

    std::uint32_t ResolvedCount() const;

private:

    struct HeldSpecialisation
    {
        std::vector<VkSpecializationMapEntry>  Declared = {};   // [-] - one entry per constant
        std::vector<std::uint32_t>             Fixed    = {};   // [-] - the words the entries index into
        VkSpecializationInfo                   Read     = {};   // [-] - what the vendor reads at construction
    };

    struct HeldModule
    {
        VkShaderModule  Constructed = VK_NULL_HANDLE;   // [-] - the vendor module
        std::string     UnitName    = {};               // [-] - which unit lowered it
        std::string     StreamStem  = {};               // [-] - the source's stem
    };

    /// 🧩 Reads one whole file into a word run, refusing rather than truncating.
    /// out   Result  [-]  refuses with HostDenied when it cannot be opened or read whole
    Deliver<std::vector<std::uint32_t>> ReadStream(const std::string& StreamPath) const;

    // 📝 🔴 The specialisations sit in a run whose entries never move rather than beside their module. The
    //    vendor reads each declaration by address at the program's construction, and an entry that a later
    //    resolution relocated is one the vendor reads after it has moved — which reports as a program built
    //    from constants nobody wrote.
    const VulkanExchange*          DeviceEdge      = nullptr;   // [-] - borrowed; never owned
    std::string                    StreamRoot      = {};        // [-] - what the build lowered into
    std::vector<HeldModule>        Modules         = {};        // [-] - every module read, held for the run
    std::deque<HeldSpecialisation> Specialisations = {};        // [-] - one per Stage call; addresses are stable
};

}   // namespace Slate
