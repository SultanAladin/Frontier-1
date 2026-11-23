//============================================================================================================================================
//                                                              PROGRAMINDEX.H
//============================================================================================================================================
// 🧩 Graphics and compute programs constructed once at bring-up, against the layouts and modules already declared.

#pragma once

#include "Foundation/DeliveryGuarantee.h"
#include "SlateVulkan/Device/DescriptorIndex/Api/DescriptorIndex.h"
#include "SlateVulkan/Device/DiagnosticExtension/Api/DiagnosticExtension.h"
#include "SlateVulkan/Device/ShaderCodec/Api/ShaderCodec.h"
#include "SlateVulkan/Device/VulkanExchange/Api/VulkanExchange.h"

#include <vulkan/vulkan.h>

#include <cstdint>
#include <vector>

namespace Slate
{

//------------------------------------------------------------------------------------------------------------------------
//                                                  WHAT IS DECLARED
//------------------------------------------------------------------------------------------------------------------------

// 📝 No program; never a valid program ordinal. Sibling of `ByteSpace`'s `AbsentExtent` and `ShaderCodec`'s
//    `AbsentModule`.
inline constexpr std::uint32_t AbsentProgram = 0xFFFFFFFFu;   // [-] - the resolution names no program

/// 🧩 How one graphics program resolves depth — tested, written, and against which comparison.
/// note  🔴 The comparison defaults to `VK_COMPARE_OP_GREATER` because `Foundation/`'s `NearPlaneDepth` is unity
///       and `FarPlaneDepth` is nought. A program declaring the ordinary less-than comparison against a
///       reversed target resolves the furthest surface at every pixel, and the image is the inside of the
///       object rather than an image that sorts wrongly — which is the failure `02` §6 declares the two
///       constants to prevent.
/// tag   nonallocating, nonthrowing
struct DepthDeclaration
{
    bool         DepthTested     = true;                    // [-] - the comparison is made at all
    bool         DepthWritten    = true;                    // [-] - a passing fragment amends the depth target
    VkCompareOp  DepthComparison = VK_COMPARE_OP_GREATER;   // [-] - the vendor spelling; reversed by default
};

/// 🧩 One graphics program, as the recording that constructs it declares it.
/// note  🔴 No vertex input is declared and none may be. `16` §4's raster reads positions and ordinals from
///       declared spans by the vertex ordinal, which is what lets one program serve every partitioning without
///       a per-topology input declaration — and what keeps the attribute set out of a document that writes no
///       attribute.
/// note  📝 Colour combination is declared nowhere. Every target `16` produces is written whole per `08` §2,
///       and a program that combined its output with what stood there would be reading a target it produces.
/// note  ⚠️ Every render construct Slate declares carries exactly one recorded division, so none is named
///       here. A program naming a second would be constructed against a division `AttachmentIndex` does not
///       declare, and the vendor reports that at the draw rather than at the construction.
/// tag   owning
struct GraphicsDeclaration
{
    std::uint32_t                     VertexModule          = AbsentModule;   // [-] - a module `ShaderCodec` resolved
    std::uint32_t                     FragmentModule        = AbsentModule;   // [-] - likewise
    std::vector<std::uint32_t>        LayoutIndexs        = {};             // [-] - declared layouts, in set order
    std::vector<SpecialisedConstant>  VertexFixed           = {};             // [-] - empty declares no specialisation
    std::vector<SpecialisedConstant>  FragmentFixed         = {};             // [-] - likewise
    VkRenderPass                      RenderConstruct       = VK_NULL_HANDLE; // [-] - what `AttachmentIndex` declared
    std::uint32_t                     ColourAttachmentCount = 0u;             // [-] - as that construct declares
    std::uint32_t                     ConstantBytes         = 0u;             // [B] - the recorded constant run
    VkPrimitiveTopology               Assembled             = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkCullModeFlags                   FacingCulled          = VK_CULL_MODE_BACK_BIT;
    DepthDeclaration                  Depth                 = {};             // [-] - reversed unless amended
};

/// 🧩 One compute program, as the dispatching document declares it.
/// note  📝 The workgroup extent is not declared here. It is the entry point's own `[numthreads]`, or a
///       specialised constant supplied through `Fixed` — `06` §2.1's reason for accepting specialisation at
///       all, and either way it is the shader's declaration rather than a second one held beside it.
/// tag   owning
struct ComputeDeclaration
{
    std::uint32_t                     ModuleIndex   = AbsentModule;   // [-] - a module `ShaderCodec` resolved
    std::vector<std::uint32_t>        LayoutIndexs  = {};             // [-] - declared layouts, in set order
    std::vector<SpecialisedConstant>  Fixed           = {};             // [-] - empty declares no specialisation
    std::uint32_t                     ConstantBytes   = 0u;             // [B] - the recorded constant run
};

//------------------------------------------------------------------------------------------------------------------------
//                                                THE CONSTRUCTED PROGRAM
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 What a recording is handed — the program, the layout its descriptors reach through, and how it is recorded.
/// note  ⚠️ The layout is delivered beside the program because every descriptor and every constant the
///       recording writes is written **through** it. A recording resolving the program alone would then reach
///       for a layout it derived separately, and two derivations of one layout is `00` §2's case exactly.
/// tag   nonallocating, nonthrowing
struct ConstructedProgram
{
    VkPipeline           Constructed   = VK_NULL_HANDLE;                  // [-] - the vendor program
    VkPipelineLayout     ReachedLayout = VK_NULL_HANDLE;                  // [-] - descriptors and constants reach through it
    VkPipelineBindPoint  RecordedAs    = VK_PIPELINE_BIND_POINT_GRAPHICS; // [-] - graphics or compute, as declared
};

//------------------------------------------------------------------------------------------------------------------------
//                                                   THE PROGRAM INDEX
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 Every program the engine constructs, resolved by the ordinal its declaration returned.
/// note  🔴 `Pipeline` is banned as Slate's own spelling and the substitution is not a euphemism: what is held
///       is a constructed program — modules, layouts and fixed constants resolved into one object the device
///       executes. The vendor spelling stays verbatim inside every call, per `06`'s opening rule.
/// note  🔴 Constructed at bring-up and never during a recording. Program construction reads the shader
///       streams and the layouts, both of which `06` §7 fixes before the first rotation; constructing one
///       inside a recording is a device stall of unbounded duration in the middle of an image.
/// note  ⚠️ The layout is owned here rather than by `DescriptorIndex`. `DescriptorIndex` declares descriptor
///       set layouts, which are what a **set** is written against; this is the program layout, which is what a
///       **program** reaches its sets and its constants through. They are different vendor objects and the two
///       lifetimes differ — the set layouts outlive every program constructed from them.
/// tag   owning
class ProgramIndex
{
public:

    ProgramIndex()                               = default;
    ProgramIndex(const ProgramIndex&)            = delete;
    ProgramIndex& operator=(const ProgramIndex&) = delete;
    ~ProgramIndex();

    /// 🧩 Takes the device, the modules every program is constructed from, and the layouts it reaches through.
    /// in    Exchange     [-]  the created device; borrowed and outlives this component
    /// in    Modules      [-]  where the stage declarations come from; borrowed and outlives this component
    /// in    Descriptors  [-]  where the set layouts come from; borrowed and outlives this component
    /// in    Naming       [-]  names every program and every layout; borrowed and outlives this component
    /// out   Result      [-]  refuses with CapabilityAbsent when no device is active
    /// post  no program is declared
    /// note  🔴 `06` §7's diagnostic-name gate. The program and the layout it reaches through are named
    ///        separately and by the same ordinal, because they are two vendor objects a recording binds one
    ///        after the other — and the errors the two raise read alike until the objects are told apart.
    /// cost  ✔️
    /// tag   api, nonthrowing
    Deliver<bool> ConstructProgramIndex(const VulkanExchange&      Exchange,
                            ShaderCodec&               Modules,
                            const DescriptorIndex&     Descriptors,
                            const DiagnosticExtension& Naming);

    /// 🧩 Constructs one graphics program, returning the ordinal every later resolution names it by.
    /// in    Declaring  [-]  the modules, layouts, render construct and depth behaviour
    /// out   Result    [-]  refuses with ContentUnsupported for an unresolved module, an undeclared layout or
    ///                       an absent render construct, and with HostDenied when the device declines it
    /// post  nothing is retained on a refusal — the layout is destroyed before the refusal returns
    /// note  🔴 The two stage declarations are taken from `ShaderCodec` rather than assembled here, so the
    ///       specialisation the vendor reads at construction is held where its addresses stay put.
    /// cost  🔴
    /// tag   api, nonthrowing
    Deliver<std::uint32_t> DeclareGraphics(const GraphicsDeclaration& Declaring);

    /// 🧩 Constructs one compute program, returning the ordinal every later resolution names it by.
    /// in    Declaring  [-]  the module, the layouts and the constant run
    /// out   Result    [-]  refuses as DeclareGraphics does, less the render construct
    /// post  nothing is retained on a refusal
    /// cost  🔴
    /// tag   api, nonthrowing
    Deliver<std::uint32_t> DeclareCompute(const ComputeDeclaration& Declaring);

    /// 🧩 The program one ordinal names, for the recording that records against it.
    /// in    ProgramIndex  [-]  an ordinal this component registered
    /// out   Result         [-]  refuses with ContentUnsupported for an ordinal naming no program
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    Deliver<ConstructedProgram> Resolve(std::uint32_t ProgramIndex) const;

    /// 🧩 Destroys every program and every layout constructed for one.
    /// pre   the device is idle and no recording that reads them is still executing
    /// cost  🚩
    /// tag   api, nonthrowing
    void Reclaim();

    std::uint32_t DeclaredCount() const;

private:

    struct HeldProgram
    {
        VkPipeline           Constructed   = VK_NULL_HANDLE;                  // [-] - the vendor program
        VkPipelineLayout     ReachedLayout = VK_NULL_HANDLE;                  // [-] - owned here, one per program
        VkPipelineBindPoint  RecordedAs    = VK_PIPELINE_BIND_POINT_GRAPHICS; // [-] - as declared
    };

    /// 🧩 Constructs the layout one program reaches its declared sets and its constant run through.
    /// in    LayoutIndexs  [-]  declared layouts, in set order; an empty run declares no set
    /// in    ConstantBytes   [B]  the recorded constant run; nought declares none
    /// in    ReachingStages  [-]  which stages read the constant run, as the vendor spells them
    /// out   Result         [-]  refuses with ContentUnsupported for an undeclared layout ordinal
    Deliver<VkPipelineLayout> ReachLayout(const std::vector<std::uint32_t>&  LayoutIndexs,
                                          std::uint32_t                     ConstantBytes,
                                          VkShaderStageFlags                ReachingStages);

    const VulkanExchange*      DeviceEdge     = nullptr;   // [-] - borrowed; never owned
    ShaderCodec*               ModuleEdge     = nullptr;   // [-] - borrowed; the stage declaration is held there
    const DescriptorIndex*     DescriptorEdge = nullptr;   // [-] - borrowed; never owned
    const DiagnosticExtension* NamingEdge     = nullptr;   // [-] - borrowed; never owned
    std::vector<HeldProgram>   Programs       = {};        // [-] - every program constructed, held for the run
};

}   // namespace Slate
