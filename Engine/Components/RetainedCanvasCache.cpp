/*============================================================================================================================================
                                                         RETAINEDCANVASCACHE.CPP
============================================================================================================================================*/
// 🧩 Implementation of RetainedCanvasCache replaying unchanged ImDrawList vertex/index/command spans without re-tessellating UI controls

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Components/RetainedCanvasCache.h"

#include <cmath>
#include <cstring>

namespace Frontier
{

namespace
{

[[nodiscard]] bool PointInsideExpanded(Rectangle2D Bounds, Point2D Pt, float MarginPx) noexcept
{
    return Pt.X >= (Bounds.X - MarginPx) &&
           Pt.Y >= (Bounds.Y - MarginPx) &&
           Pt.X <= (Bounds.Right() + MarginPx) &&
           Pt.Y <= (Bounds.Bottom() + MarginPx);
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                       HASH MIXING
//------------------------------------------------------------------------------------------------------------------------

uint64_t RetainedCanvasCache::MixHash(uint64_t Seed, uint64_t Quantity) noexcept
{
    uint64_t Mixed = Seed ^ (Quantity + 0x9E3779B97F4A7C15ull + (Seed << 6u) + (Seed >> 2u));
    Mixed ^= (Mixed >> 30u);
    Mixed *= 0xBF58476D1CE4E5B9ull;
    Mixed ^= (Mixed >> 27u);
    return Mixed;
}

uint64_t RetainedCanvasCache::MixBounds(uint64_t Seed, Rectangle2D Bounds, float ScaleFactor) noexcept
{
    Seed = MixHash(Seed, static_cast<uint64_t>(std::llround(Bounds.X * 4.0f)));
    Seed = MixHash(Seed, static_cast<uint64_t>(std::llround(Bounds.Y * 4.0f)));
    Seed = MixHash(Seed, static_cast<uint64_t>(std::llround(Bounds.Width * 4.0f)));
    Seed = MixHash(Seed, static_cast<uint64_t>(std::llround(Bounds.Height * 4.0f)));
    Seed = MixHash(Seed, static_cast<uint64_t>(std::llround(ScaleFactor * 1024.0f)));
    return Seed;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                 CAPTURE & REPLAY OPERATIONS
//------------------------------------------------------------------------------------------------------------------------

void RetainedCanvasCache::Invalidate() noexcept
{
    CacheValid    = false;
    CaptureActive = false;
}

bool RetainedCanvasCache::QueryReusable(
    uint64_t    CandidateSignature,
    Rectangle2D PhysicalBounds,
    Point2D     CurrentMousePhysical,
    bool        ForceRebuild) const noexcept
{
    if (ForceRebuild || !CacheValid || CandidateSignature != CachedSignature)
    {
        return false;
    }
    if (PhysicalBounds.Width <= 2.0f || PhysicalBounds.Height <= 2.0f)
    {
        return false;
    }
    // Rebuild whenever the pointer is inside this pane (or just exited it on this tick so hover highlights clear).
    if (PointInsideExpanded(PhysicalBounds, CurrentMousePhysical, 4.0f) ||
        PointInsideExpanded(CachedPhysicalBounds, LastMousePhysical, 4.0f))
    {
        return false;
    }
    return true;
}

void RetainedCanvasCache::BeginCapture(ImDrawList* TargetCanvas) noexcept
{
    CaptureActive = false;
    if (TargetCanvas == nullptr)
    {
        return;
    }
    CaptureStartCmd      = TargetCanvas->CmdBuffer.Size;
    CapturePriorTailElem = (CaptureStartCmd > 0)
        ? TargetCanvas->CmdBuffer.Data[CaptureStartCmd - 1].ElemCount
        : 0u;
    CaptureStartVtx      = TargetCanvas->VtxBuffer.Size;
    CaptureStartIdx      = TargetCanvas->IdxBuffer.Size;
    CaptureActive        = true;
}

void RetainedCanvasCache::EndCapture(
    ImDrawList* TargetCanvas,
    uint64_t    CompletedSignature,
    Rectangle2D PhysicalBounds,
    Point2D     CurrentMousePhysical)
{
    LastMousePhysical = CurrentMousePhysical;
    if (!CaptureActive || TargetCanvas == nullptr)
    {
        CaptureActive = false;
        CacheValid    = false;
        return;
    }
    CaptureActive = false;

    const int32_t EndCmd = TargetCanvas->CmdBuffer.Size;
    const int32_t EndVtx = TargetCanvas->VtxBuffer.Size;
    const int32_t EndIdx = TargetCanvas->IdxBuffer.Size;
    if (EndCmd < CaptureStartCmd || EndVtx < CaptureStartVtx || EndIdx < CaptureStartIdx)
    {
        CacheValid = false;
        return;
    }

    const uint32_t DeltaVtx = static_cast<uint32_t>(EndVtx - CaptureStartVtx);
    const uint32_t DeltaIdx = static_cast<uint32_t>(EndIdx - CaptureStartIdx);
    if (sizeof(ImDrawIdx) == 2u && DeltaVtx > 65000u)
    {
        CacheValid = false;
        return;
    }

    for (int32_t CmdIdx = CaptureStartCmd; CmdIdx < EndCmd; ++CmdIdx)
    {
        if (TargetCanvas->CmdBuffer.Data[CmdIdx].UserCallback != nullptr)
        {
            CacheValid = false;
            return;
        }
    }

    CachedVertices.resize(DeltaVtx);
    if (DeltaVtx > 0u)
    {
        std::memcpy(
            CachedVertices.data(),
            TargetCanvas->VtxBuffer.Data + CaptureStartVtx,
            static_cast<std::size_t>(DeltaVtx) * sizeof(ImDrawVert));
    }

    CachedIndices.clear();
    CachedIndices.reserve(DeltaIdx);
    CachedCommands.clear();

    auto AppendLocalSpan = [&](const ImDrawCmd& SrcCmd, uint32_t AbsIdxOffset, uint32_t SpanElemCount) -> bool
    {
        if (SpanElemCount == 0u)
        {
            return true;
        }
        if (static_cast<uint64_t>(AbsIdxOffset) + SpanElemCount > static_cast<uint64_t>(TargetCanvas->IdxBuffer.Size))
        {
            return false;
        }
        const uint32_t LocalStart = static_cast<uint32_t>(CachedIndices.size());
        CachedIndices.resize(LocalStart + SpanElemCount);
        const ImDrawIdx* SrcIdxPtr = TargetCanvas->IdxBuffer.Data + AbsIdxOffset;
        ImDrawIdx*       DstIdxPtr = CachedIndices.data() + LocalStart;
        for (uint32_t K = 0u; K < SpanElemCount; ++K)
        {
            const uint32_t AbsVtx = SrcCmd.VtxOffset + static_cast<uint32_t>(SrcIdxPtr[K]);
            if (AbsVtx < static_cast<uint32_t>(CaptureStartVtx))
            {
                return false;
            }
            const uint32_t RelVtx = AbsVtx - static_cast<uint32_t>(CaptureStartVtx);
            if (RelVtx >= DeltaVtx || (sizeof(ImDrawIdx) == 2u && RelVtx > 65535u))
            {
                return false;
            }
            DstIdxPtr[K] = static_cast<ImDrawIdx>(RelVtx);
        }

        RetainedCommandSpan Span {};
        Span.ClipBounds     = SrcCmd.ClipRect;
        Span.TextureHandle  = SrcCmd.TexRef;
        Span.LocalIdxOffset = LocalStart;
        Span.IndexCount     = SpanElemCount;
        CachedCommands.push_back(Span);
        return true;
    };

    if (CaptureStartCmd > 0)
    {
        const ImDrawCmd& PriorCmd = TargetCanvas->CmdBuffer.Data[CaptureStartCmd - 1];
        if (PriorCmd.UserCallback == nullptr && PriorCmd.ElemCount > CapturePriorTailElem)
        {
            const uint32_t ExtraElems = PriorCmd.ElemCount - CapturePriorTailElem;
            if (!AppendLocalSpan(PriorCmd, static_cast<uint32_t>(CaptureStartIdx), ExtraElems))
            {
                CacheValid = false;
                return;
            }
        }
    }

    for (int32_t CmdIdx = CaptureStartCmd; CmdIdx < EndCmd; ++CmdIdx)
    {
        const ImDrawCmd& Cmd = TargetCanvas->CmdBuffer.Data[CmdIdx];
        if (Cmd.ElemCount > 0u)
        {
            if (!AppendLocalSpan(Cmd, Cmd.IdxOffset, Cmd.ElemCount))
            {
                CacheValid = false;
                return;
            }
        }
    }

    CachedSignature      = CompletedSignature;
    CachedPhysicalBounds = PhysicalBounds;
    CacheValid           = true;
    ++CaptureCount;
}

bool RetainedCanvasCache::ReplayInto(ImDrawList* TargetCanvas, Point2D CurrentMousePhysical) noexcept
{
    if (!CacheValid || TargetCanvas == nullptr)
    {
        return false;
    }

    LastMousePhysical = CurrentMousePhysical;
    if (CachedVertices.empty() || CachedIndices.empty() || CachedCommands.empty())
    {
        ++ReplayCount;
        return true;
    }

    const uint32_t StartVtxOffset = static_cast<uint32_t>(TargetCanvas->VtxBuffer.Size);
    const uint32_t StartIdxOffset = static_cast<uint32_t>(TargetCanvas->IdxBuffer.Size);
    const uint32_t VtxCount       = static_cast<uint32_t>(CachedVertices.size());
    const uint32_t IdxCount       = static_cast<uint32_t>(CachedIndices.size());
    const bool     UseVtxOffset   = (TargetCanvas->Flags & ImDrawListFlags_AllowVtxOffset) != 0;

    if (!UseVtxOffset && sizeof(ImDrawIdx) == 2u && (StartVtxOffset + VtxCount) > 65535u)
    {
        return false;
    }

    TargetCanvas->VtxBuffer.resize(static_cast<int>(StartVtxOffset + VtxCount));
    std::memcpy(
        TargetCanvas->VtxBuffer.Data + StartVtxOffset,
        CachedVertices.data(),
        static_cast<std::size_t>(VtxCount) * sizeof(ImDrawVert));

    TargetCanvas->IdxBuffer.resize(static_cast<int>(StartIdxOffset + IdxCount));
    if (UseVtxOffset)
    {
        std::memcpy(
            TargetCanvas->IdxBuffer.Data + StartIdxOffset,
            CachedIndices.data(),
            static_cast<std::size_t>(IdxCount) * sizeof(ImDrawIdx));
    }
    else
    {
        ImDrawIdx*       DstIdx = TargetCanvas->IdxBuffer.Data + StartIdxOffset;
        const ImDrawIdx* SrcIdx = CachedIndices.data();
        const ImDrawIdx  Bias   = static_cast<ImDrawIdx>(StartVtxOffset);
        for (uint32_t K = 0u; K < IdxCount; ++K)
        {
            DstIdx[K] = static_cast<ImDrawIdx>(SrcIdx[K] + Bias);
        }
    }

    if (TargetCanvas->CmdBuffer.Size > 0 &&
        TargetCanvas->CmdBuffer.back().ElemCount == 0u &&
        TargetCanvas->CmdBuffer.back().UserCallback == nullptr)
    {
        TargetCanvas->CmdBuffer.pop_back();
    }

    for (const RetainedCommandSpan& Span : CachedCommands)
    {
        ImDrawCmd ReplayedCmd {};
        ReplayedCmd.ClipRect  = Span.ClipBounds;
        ReplayedCmd.TexRef    = Span.TextureHandle;
        ReplayedCmd.VtxOffset = UseVtxOffset ? StartVtxOffset : 0u;
        ReplayedCmd.IdxOffset = StartIdxOffset + Span.LocalIdxOffset;
        ReplayedCmd.ElemCount = Span.IndexCount;
        ReplayedCmd.UserCallbackDataOffset = -1;
        TargetCanvas->CmdBuffer.push_back(ReplayedCmd);
    }

    TargetCanvas->_VtxWritePtr = TargetCanvas->VtxBuffer.Data + TargetCanvas->VtxBuffer.Size;
    TargetCanvas->_IdxWritePtr = TargetCanvas->IdxBuffer.Data + TargetCanvas->IdxBuffer.Size;
    if (UseVtxOffset)
    {
        TargetCanvas->_CmdHeader.VtxOffset = static_cast<unsigned int>(TargetCanvas->VtxBuffer.Size);
        TargetCanvas->_VtxCurrentIdx       = 0u;
    }
    else
    {
        TargetCanvas->_CmdHeader.VtxOffset = 0u;
        TargetCanvas->_VtxCurrentIdx       = static_cast<unsigned int>(TargetCanvas->VtxBuffer.Size);
    }
    TargetCanvas->AddDrawCmd();
    ++ReplayCount;
    return true;
}

} // namespace Frontier
