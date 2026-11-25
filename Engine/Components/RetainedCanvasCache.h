/*============================================================================================================================================
                                                          RETAINEDCANVASCACHE.H
============================================================================================================================================*/
// 🧩 Retained ImDrawList vertex, index, and command-span replay buffer for dirty-invalidated UI panes and grids

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Components/PixelSpace.h"

#include <imgui.h>

#include <cstdint>
#include <vector>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct RetainedCommandSpan
{
    ImVec4       ClipBounds      {};   // [px]  - Physical clip rectangle (x1, y1, x2, y2)
    ImTextureRef TextureHandle   {};   // [-]   - Bound ImGui 1.92 font or image reference
    uint32_t     LocalIdxOffset  = 0u; // [idx] - Start offset inside CachedIndices
    uint32_t     IndexCount      = 0u; // [cnt] - Triangle index count (multiple of 3)
};

class RetainedCanvasCache
{
public:
    RetainedCanvasCache()  = default;
    ~RetainedCanvasCache() = default;

    void Invalidate() noexcept;

    [[nodiscard]] bool QueryReusable(
        uint64_t    CandidateSignature,
        Rectangle2D PhysicalBounds,
        Point2D     CurrentMousePhysical,
        bool        ForceRebuild) const noexcept;

    [[nodiscard]] bool QueryReusableStationary(
        uint64_t    CandidateSignature,
        Rectangle2D PhysicalBounds,
        Point2D     CurrentMousePhysical,
        bool        ForceRebuild) const noexcept;

    // Hover-zone-aware reuse: replays the entire cached draw list even while the cursor moves across non-interactive
    // background/cards inside the pane, rebuilding ONLY when the cursor enters/exits a registered interactive control.
    [[nodiscard]] bool QueryReusableHoverAware(
        uint64_t    CandidateSignature,
        Rectangle2D PhysicalBounds,
        Point2D     CurrentMousePhysical,
        bool        ForceRebuild) const noexcept;

    void ClearInteractiveZones() noexcept;
    void RegisterInteractiveZone(Rectangle2D PhysicalRect);
    [[nodiscard]] int32_t QueryHoveredZone(Point2D PhysicalPoint) const noexcept;

    void BeginCapture(ImDrawList* TargetCanvas) noexcept;
    void EndCapture(
        ImDrawList* TargetCanvas,
        uint64_t    CompletedSignature,
        Rectangle2D PhysicalBounds,
        Point2D     CurrentMousePhysical);

    [[nodiscard]] bool ReplayInto(ImDrawList* TargetCanvas, Point2D CurrentMousePhysical) noexcept;

    [[nodiscard]] uint64_t QueryCaptureCount() const noexcept { return CaptureCount; }
    [[nodiscard]] uint64_t QueryReplayCount()  const noexcept { return ReplayCount; }

    [[nodiscard]] static uint64_t MixHash(uint64_t Seed, uint64_t Quantity) noexcept;
    [[nodiscard]] static uint64_t MixBounds(uint64_t Seed, Rectangle2D Bounds, float ScaleFactor) noexcept;

private:
    std::vector<ImDrawVert>          CachedVertices       {};          // [-]   - Snapshot of pane vertices
    std::vector<ImDrawIdx>           CachedIndices        {};          // [idx] - Local 0-based vertex indices
    std::vector<RetainedCommandSpan> CachedCommands       {};          // [-]   - Snapshot of clip/texture spans
    std::vector<Rectangle2D>         InteractiveZones     {};          // [px]  - Registered physical hit-boxes of interactive controls
    uint64_t                         CachedSignature      = 0ull;      // [id]  - Last recorded content hash
    Rectangle2D                      CachedPhysicalBounds {};          // [px]  - Last recorded physical bounds
    Point2D                          LastMousePhysical    { -1.0e5f, -1.0e5f }; // [px] - Prior cursor sample
    int32_t                          LastHoveredZone      = -1;        // [idx] - Interactive zone hovered during capture/last replay
    int32_t                          CaptureStartCmd      = 0;         // [idx] - CmdBuffer start index
    uint32_t                         CapturePriorTailElem = 0u;        // [cnt] - Prior trailing command ElemCount
    int32_t                          CaptureStartVtx      = 0;         // [idx] - VtxBuffer start index
    int32_t                          CaptureStartIdx      = 0;         // [idx] - IdxBuffer start index
    bool                             CaptureActive        = false;     // [-]   - True between BeginCapture/EndCapture
    bool                             CacheValid           = false;     // [-]   - True when snapshot can be replayed
    uint64_t                         CaptureCount         = 0ull;      // [cnt] - Total full UI rebuilds recorded
    uint64_t                         ReplayCount          = 0ull;      // [cnt] - Total zero-rebuild replays executed
};

} // namespace Frontier
