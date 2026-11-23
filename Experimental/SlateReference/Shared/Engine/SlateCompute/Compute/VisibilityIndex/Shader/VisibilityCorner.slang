//============================================================================================================================================
//                                                           VISIBILITYCORNER.SLANG
//============================================================================================================================================
// 🧩 The vertex half of `16` §3's hardware raster — one triangle corner resolved from the declared spans and projected.

#include "SlateCompute/Compute/VisibilityIndex/Shader/VisibilityUniform.slang"

namespace Slate
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      WHAT IS READ
//------------------------------------------------------------------------------------------------------------------------

// 📝 🔴 No vertex input declaration and none may be — `16` §4's raster reads positions and ordinals from declared
//    spans by the vertex ordinal. One program then serves every partitioning without a per-topology input
//    declaration, and the attribute set stays out of a document that writes no attribute.
//
// ⚠️ The four are declared in the set order `ProgramIndex::ReachLayout` resolves — position in the run is the set
//    ordinal the host declares, so a span moved here without its declaration moving with it is read as another.
//
// 🔴 An indirect draw reaches the triangle through `SurvivingTriangles`: its issued corners are contiguous from
//    nought over whatever survived the cull. A direct draw instead reaches the triangle ordinal directly. Its
//    fourth descriptor is the triangle storage itself (only to keep one set layout), so interpreting that record
//    as a uint permutation corrupts every direct triangle after the first word.
ConstantBuffer<VisibilityUniform>  Projecting          ;
StructuredBuffer<ObjectPosition>   ObjectPositions     ;
StructuredBuffer<DrawnTriangle>    DrawnTriangles      ;
StructuredBuffer<Unsigned32>       SurvivingTriangles  ;

//------------------------------------------------------------------------------------------------------------------------
//                                                     THE RESOLUTION
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 Resolves one triangle corner from the declared spans and projects it.
/// in    VertexIndex   [-]  the running corner ordinal; three per drawn triangle
/// out   ResolvedCorner  [-]  the clip position and the two ordinals the fragment writes
/// note  🔴 The corner is reached by division and remainder rather than through an index span, because the host
///         has already fanned every n-gon into the triangle run. `10` accepts faces of any corner count and `16`
///         §1 partitions them by the fan count, so a triangle here is a triangle and never a face — the two agree
///         only because `PartitionStructure` counts the fan triangles the spanned faces amount to.
/// note  ⚠️ The vendor supplies no bound on either span read. A drawn count exceeding what the spans carry is
///         rejected by the host at the recording rather than clamped here, because a clamp resolves the wrong
///         triangle at every invocation past the end and reads as geometry rather than as the refusal it is.
/// note  🔴 `SurvivingResolved` makes one program serve both recording routes without pretending the direct
///         route has an identity permutation. `16` §2's cull compacts survivor ordinals for the indirect route;
///         the direct route has no such run and uses the issued triangle ordinal itself.
/// cost  ✔️
/// tag   nonallocating, nonthrowing
[shader("vertex")]
ResolvedCorner ResolveVisibilityCorner(Unsigned32 VertexIndex : SV_VertexID)
{
    const Unsigned32 IssuedTriangle = VertexIndex / 3u;
    const Unsigned32 CornerEntry    = VertexIndex % 3u;
    const Unsigned32 DrawnTriangleIndex = Projecting.SurvivingResolved != 0u
                                        ? SurvivingTriangles[IssuedTriangle]
                                        : IssuedTriangle;

    const DrawnTriangle Drawn = DrawnTriangles[DrawnTriangleIndex];

    Unsigned32 VertexEntry = Drawn.CornerVertex0;

    if (CornerEntry == 1u)
        VertexEntry = Drawn.CornerVertex1;
    else if (CornerEntry == 2u)
        VertexEntry = Drawn.CornerVertex2;

    ResolvedCorner Resolved;

    Resolved.ClipPosition     = ProjectObjectPosition(Projecting, ObjectPositions[VertexEntry]);
    Resolved.PartitionIndex = Drawn.PartitionIndex;
    Resolved.TriangleIndex  = Drawn.TriangleIndex;

    return Resolved;
}

}   // namespace Slate
