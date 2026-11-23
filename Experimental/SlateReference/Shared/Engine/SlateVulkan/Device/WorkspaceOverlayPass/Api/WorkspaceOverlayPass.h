//============================================================================================================================================
//                                                               OVERLAYPASS.H
//============================================================================================================================================
// 🧩 The editor overlay's own GPU pass — the grid, the gizmo and (later) wireframe
//    drawn by a dedicated graphics program in a pass of their own, instead of
//    through the interface's ImGui draw lists.
//
//    🔴 WHY A SEPARATE PASS. The interface records everything through ImGui, which
//       tessellates every polyline on the CPU and blends premultiplied alpha —
//       a dense lattice or a high-poly wireframe bogged the frame down, and a
//       low-alpha line over a bright sky read washed out. This pass consumes the
//       CPU-side `OverlayGeometry` record (a few hundred primitives), uploads it
//       when its generation changes, expands lines and dots in the VERTEX SHADER
//       (two CPU points per line, one per dot), and blends straight alpha with no
//       tone mapping — vivid colours, and the CPU never tessellates.
//
//    The pass records INSIDE the host's dynamic-rendering scope, after the
//    interface: the host calls `Record` between `BeginRendering` and `Complete`.
//    It is self-contained — its own pipeline, layout, descriptor set and buffer —
//    following `AtmospherePresentationSurface`'s precedent of owning its objects directly.

#pragma once

#include "Foundation/ExtentBands.h"
#include "Foundation/DeliveryGuarantee.h"
#include "Shared/OverlayGeometry.slang.h"
#include "SlateVulkan/Device/DiagnosticExtension/Api/DiagnosticExtension.h"
#include "SlateVulkan/Device/ShaderCodec/Api/ShaderCodec.h"
#include "SlateVulkan/Device/VulkanExchange/Api/VulkanExchange.h"

#include <vulkan/vulkan.h>

#include <cstdint>

namespace Slate
{

/// 🧩 Owns the overlay pipeline, its descriptor set and its vertex buffer, and records the overlay
///    primitives after the interface within the open dynamic-rendering scope.
/// tag   owning, nonallocating, nonthrowing
class WorkspaceOverlayPass
{
public:

    static constexpr std::uint32_t LineCapacity     = OverlayGeometry::LineLimit;     // [-] - line records
    static constexpr std::uint32_t DotCapacity      = OverlayGeometry::DotLimit;      // [-] - dot records
    static constexpr std::uint32_t TriangleCapacity = OverlayGeometry::TriangleLimit; // [-] - triangle records

    WorkspaceOverlayPass()                              = default;
    WorkspaceOverlayPass(const WorkspaceOverlayPass&)            = delete;
    WorkspaceOverlayPass& operator=(const WorkspaceOverlayPass&) = delete;
    ~WorkspaceOverlayPass();

    /// 🧩 Constructs the pipeline, the descriptor set and the vertex buffer against the active device.
    /// in    Exchange     [-]  the created device; borrowed and outlives this component
    /// in    Naming       [-]  names every object; borrowed and outlives this component
    /// in    Streams      [-]  the lowered shader streams; `WorkspaceOverlayVertex` and `WorkspaceOverlayFragment`
    ///                         are resolved here, borrowed and outlives this component
    /// in    ColourFormat [-]  the display image's format; the pipeline's sole colour attachment
    /// out   Result       [-]  refuses with CapabilityAbsent when no device is active, HostDenied when
    ///                         either shader stream is absent (the build lowered nothing), and
    ///                         ContentUnsupported when the device declines any object
    /// cost  🔴
    /// tag   api, nonthrowing
    Deliver<bool> ConstructWorkspaceOverlayPass(const VulkanExchange&      Exchange,
                            const DiagnosticExtension& Naming,
                            ShaderCodec&               Streams,
                            VkFormat                   ColourFormat);

    /// 🧩 Copies the CPU overlay record into the mapped vertex buffer.
    /// in    Overlay      [-]  the panel's record; lines, dots and triangles are copied into their
    ///                          own regions of the one buffer, converted to the shader's record shapes
    /// in    PixelScale   [-]  logical UI points to physical framebuffer pixels; defaults to 1 for
    ///                          an unscaled display
    /// note  🔴 Called BETWEEN frames, at most once per generation change: the buffer is
    ///        host-coherent and the previous frame's submission has completed, so the copy is
    ///        visible to the next draw with no barrier of its own.
    /// note  🔴 Overlay records are authored beside the pointer in logical points, while this pass's
    ///        viewport is the physical swapchain. Converting only the scissor leaves dots, gizmos and
    ///        selection markers in a different coordinate system from the CAD pass.
    /// cost  🚩
    /// tag   api, nonthrowing
    void Upload(const OverlayGeometry& Overlay, float PixelScale = 1.0f);

    /// 🧩 Records the overlay primitives inside the open dynamic-rendering scope, clipped to one
    ///    viewport leaf's box.
    /// note  🔴 The lines draw as `4 × count`, the dots as `4 × count`, the triangles as
    ///        `3 × count` — the vertex stage expands by `SV_VertexID`, so nothing here tessellates.
    /// in    Command  [-]  the recording, between `vkCmdBeginRendering` and `vkCmdEndRendering`
    /// in    Width    [px] the display extent the viewport state is set against
    /// in    Height   [px]
    /// in    LeafX0   [px] the viewport leaf's WHOLE box -- what the camera is mapped across
    /// in    LeafY0   [px]
    /// in    LeafX1   [px]
    /// in    LeafY1   [px]
    /// in    ScissorX0 [px] the visible sub-rectangle -- what is allowed to be textured
    /// in    ScissorY0 [px]
    /// in    ScissorX1 [px]
    /// in    ScissorY1 [px]
    /// note  🔴 THESE ARE TWO DIFFERENT RECTANGLES AND CONFLATING THEM IS THE GRID BUG. The leaf box
    ///        is geometry: the fragment stage maps the camera's field of view across it, so shrinking
    ///        it does not clip the grid, it SQUASHES the camera into a smaller box. The scissor is
    ///        visibility: it hides what a drawer covers without moving anything. One value served both
    ///        roles, so hiding the covered strip squashed the grid and un-squashing it un-hid the
    ///        strip -- the two symptoms traded back and forth because they were the same number.
    ///        Pass the WHOLE leaf as the leaf box even when most of it is hidden.
    /// note  ⚠️ Both are PHYSICAL pixels. Logical points silently clip the wrong region.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    void Record(VkCommandBuffer Command, std::uint32_t Width, std::uint32_t Height,
                float LeafX0, float LeafY0, float LeafX1, float LeafY1,
                float ScissorX0, float ScissorY0, float ScissorX1, float ScissorY1);

    /// 🧩 Records the same pass across the scissor, keeping clear of one withheld box.
    /// in    WithheldX0  [px] a box to keep clear; a zero-area box records the scissor unchanged
    /// note  🔴 THIS PASS RECORDS AFTER THE INTERFACE, so the grid and the axes drew straight over any
    ///        menu opened from the viewport footer -- which reads as a TRANSPARENT dropdown, because what
    ///        shows through it is the viewport behind. Every menu plate was opaque the whole time.
    /// note  🔴 A scissor is ONE rectangle, so the remainder is recorded as up to four disjoint bands
    ///        around the withheld box. Suppressing the pass outright while a menu stands is the same trade
    ///        that once made an open drawer erase the whole sketch.
    /// note  ⚠️ The CAMERA rectangle is passed through to every band untouched; only the scissor is cut.
    /// note  At most four recordings, and exactly one whenever nothing is withheld.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    void RecordAround(VkCommandBuffer Command, std::uint32_t Width, std::uint32_t Height,
                      float CameraX0, float CameraY0, float CameraX1, float CameraY1,
                      float ScissorX0, float ScissorY0, float ScissorX1, float ScissorY1,
                      float WithheldX0, float WithheldY0, float WithheldX1, float WithheldY1);

    /// 🧩 Whether the pass stands — what the host tests before it records the GPU overlay.
    /// note  🔴 The pass refuses when the build lowered no shaders; the host now reports that the GPU
    ///        overlay is required rather than drawing a second CPU path.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    bool Standing() const
    {
        return DeviceEdge != nullptr && OverlayPipeline != VK_NULL_HANDLE;
    }

    /// 🧩 Destroys every object and forgets the device handles, ahead of a device rebuild.
    /// cost  🔴
    /// tag   api, nonthrowing
    void Reclaim();

private:

    const VulkanExchange*    DeviceEdge = nullptr;   // [-] - borrowed; never owned
    const DiagnosticExtension* NamingEdge = nullptr; // [-] - borrowed; never owned

    VkBuffer                 VertexBuffer   = VK_NULL_HANDLE;   // [-] - lines, dots, triangles, one buffer
    VkDeviceMemory           VertexMemory   = VK_NULL_HANDLE;   // [-]
    std::uint8_t*            MappedSlot     = nullptr;          // [-] - persistent mapping

    VkDescriptorSetLayout    OverlayLayout  = VK_NULL_HANDLE;   // [-] - three storage bindings
    VkDescriptorPool         OverlayPool    = VK_NULL_HANDLE;   // [-]
    VkDescriptorSet          OverlaySet     = VK_NULL_HANDLE;   // [-]

    VkPipelineLayout         OverlayPipelineLayout = VK_NULL_HANDLE;   // [-] - the set + the push constant
    VkPipeline               OverlayPipeline       = VK_NULL_HANDLE;   // [-]

    std::uint32_t            LineBytes      = 0u;               // [B] - the line region's extent
    std::uint32_t            DotBytes       = 0u;               // [B] - the dot region's extent
    std::uint32_t            TriangleBytes  = 0u;               // [B] - the triangle region's extent

    // 📐 The analytic ground's pose, taken from the uploaded record. It is a
    //    camera rather than a vertex count, so it lives beside the counts and not
    //    in the vertex buffer.
    OverlayGroundPose        OverlayGround        = {};

    std::uint32_t            OverlayLineCount     = 0u;         // [-] - the uploaded record's counts
    std::uint32_t            OverlayDotCount      = 0u;         // [-]
    std::uint32_t            OverlayTriangleCount = 0u;         // [-]
};

} // namespace Slate
