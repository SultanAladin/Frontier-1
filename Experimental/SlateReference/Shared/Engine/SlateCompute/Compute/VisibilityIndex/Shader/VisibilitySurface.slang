//============================================================================================================================================
//                                                          VISIBILITYSURFACE.SLANG
//============================================================================================================================================
// 🧩 The fragment half of `16` §3's hardware raster — which partition and which triangle one pixel resolved to, and nothing further.

#include "SlateCompute/Compute/VisibilityIndex/Shader/VisibilityUniform.slang"

namespace Slate
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    WHAT IS WRITTEN
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 What one fragment writes — the two colour targets this recording produces at the resolved pixel.
/// note  🔴 Depth is not among them. It is the recorded comparison's own output under `VK_COMPARE_OP_GREATER`,
///        and a fragment writing it explicitly surrenders the early comparison the whole arrangement rests on —
///        every fragment shaded before its depth is known, on a document whose entire purpose is to resolve the
///        visible surface before anything shades at all.
/// note  ⚠️ 🚧 `MotionSurface` is `08` §2's fourth production and is absent here, because it is the difference
///        between this projection and the previous rotation's and no previous projection has crossed the device
///        edge yet. `16` §4.2 places it in this recording and `64` §8 depends on it; it lands with the second
///        transform, and the recording declares three attachments until it does.
/// tag   guarantee
struct WrittenPixel
{
    Unsigned32x2  Written    : SV_Target0 ;   // [-] - the partition ordinal and the triangle ordinal
    Real32        Occupancy  : SV_Target1 ;   // [-] - unity where a surface resolved; the clear carries nought
};

//------------------------------------------------------------------------------------------------------------------------
//                                                     THE RESOLUTION
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 Writes the resolved partition, the resolved triangle and the occupancy at one pixel.
/// in    Resolved  [-]  what the vertex entry point carried, both ordinals flat
/// out   Written   [-]  the two ordinals and the occupancy
/// note  🔴 Nothing is computed here, and that is the whole document. `16` §4 writes depth, identity, coverage
///         and motion and **no attribute whatsoever** — every perpendicular, tangent basis and domain coordinate
///         is reconstructed in `18` from the face this word names. A fragment resolving one attribute here has
///         widened the target set at every pixel of the display extent, every rotation, for one consumer.
/// note  ⚠️ 🚧 `16` §3.1's coverage test discards a cutout fragment below its material's threshold and is absent
///         here: it samples channel 8 at `20`'s coarsest guaranteed-resident level, and `20` is unbuilt. A cutout
///         owner therefore rasterises solid until it stands, which is the conservative reading of the two —
///         the hole appears late rather than the surface disappearing early.
/// cost  ✔️
/// tag   nonallocating, nonthrowing
[shader("fragment")]
WrittenPixel ResolveVisibilityPixel(ResolvedCorner Resolved)
{
    WrittenPixel Written;

    Written.Written   = Unsigned32x2(Resolved.PartitionIndex, Resolved.TriangleIndex);
    Written.Occupancy = 1.0f;

    return Written;
}

}   // namespace Slate
