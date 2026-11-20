/*==========================================================================================================================================
                                                              CADSTROKE.SLANG
==========================================================================================================================================*/
// 🧩 Shared CAD stroke expansion: homogeneous edge clipping, the six-corner pixel-width quad and its coverage ramp.
//    Used by Frontier CadEdge and the Sketcher edge programs, so both draw topology edges with the same geometry.

//----------------------------------------------------------------------------------------------------------------------
//                                                  SEGMENT CLIPPING
//----------------------------------------------------------------------------------------------------------------------

// Clips a homogeneous segment against one plane (distance ≥ 0 kept); false when both ends are outside.
bool ClipEdgeBoundary(inout float4 Start, inout float4 End, float StartDistance, float EndDistance)
{
    if (StartDistance < 0 && EndDistance < 0) return false;
    if (StartDistance < 0 || EndDistance < 0)
    {
        float4 Intersection = lerp(Start, End, StartDistance / (StartDistance - EndDistance));
        if (StartDistance < 0) Start = Intersection;
        else End = Intersection;
    }
    return true;
}

// 📝 Clip before perspective division, including the near-zero homogeneous denominator (w > 1e-5), then z ≥ 0.
bool ClipCadSegment(inout float4 Start, inout float4 End)
{
    if (!ClipEdgeBoundary(Start, End, Start.w - 1e-5, End.w - 1e-5)) return false;
    return ClipEdgeBoundary(Start, End, Start.z, End.z);
}

//----------------------------------------------------------------------------------------------------------------------
//                                                    STROKE QUAD
//----------------------------------------------------------------------------------------------------------------------

struct CadStrokeCorner
{
    float4 Position;                                                                         // [-]  - Clip position of the corner
    float  Distance;                                                                         // [px] - Signed distance from the centre line
    bool   EndCondition;                                                                     // [-]  - Corner sits at End (else at Start)
};

// 📝 Corner VertexIndex (0 … 5, two triangles 0 1 2 · 2 1 3) of the screen quad around the clipped segment: Reach px either
//    side of the centre line and Overlap px past each end (0 = butt ends; Sketcher overlaps polyline joints). Viewport is
//    the target size in px. False for a segment shorter than 1e-5 px, which draws nothing.
bool ExpandCadStroke(float4 Start, float4 End, uint VertexIndex, float2 Viewport, float Reach, float Overlap, out CadStrokeCorner Corner)
{
    static const uint Corners[6] = {0, 1, 2, 2, 1, 3};
    uint   Index     = Corners[VertexIndex];
    float2 Direction = (End.xy / End.w - Start.xy / Start.w) * Viewport * 0.5;
    float  Length    = length(Direction);
    Corner.Position     = float4(-2, -2, 0, 1);
    Corner.Distance     = 1000;
    Corner.EndCondition = Index >= 2;
    if (Length < 1e-5) return false;
    float2 Perpendicular = float2(-Direction.y, Direction.x) / Length;
    float  Side          = (Index == 1 || Index == 3) ? 1.0 : -1.0;
    float2 Along         = Direction / Length * (Index < 2 ? -Overlap : Overlap);
    Corner.Position     = Index < 2 ? Start : End;
    Corner.Position.xy += (Perpendicular * Side * Reach + Along) * 2.0 / Viewport * Corner.Position.w;
    Corner.Distance     = Side * Reach;
    return true;
}

// Antialiased coverage of a stroke pixel: 1 inside Inner px of the centre line, falling smoothly to 0 at Outer px.
float CadStrokeCoverage(float Distance, float Inner, float Outer)
{
    return 1.0 - smoothstep(Inner, Outer, abs(Distance));
}
