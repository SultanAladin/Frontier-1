/*============================================================================================================================================
                                                           VECTORSURFACE.SLANG
============================================================================================================================================*/
// 🧩 GPU Float32 colour, height, normal, roughness, metalness, opacity and occlusion maps from shared vector expressions.

struct VectorCoverageInstruction
{
    uint4 Topology;
    float4 Bounds;
    float4 Transform;
    float4 Colour;
    float4 ColourEnd;
    float4 StrokeColour;
    float4 Paint;
};
struct VectorCubicSegment { float4 StartControl; float4 ControlEnd; };
// 📝 Mirror VectorCoverageInstructionCapacity/VectorCoverageExtensionCapacity in VectorCoverage.h exactly
// (this header cannot be included from Slang, so these two constants must stay hand-synchronized).
static const uint InstructionCapacity = 256;
struct TileDirectoryEntry { uint Offset; uint Count; };
// 📝 TileSize/TilesPerRow sit right after Root so the first six words exactly match VectorCoverage.slang's
// own CoverageProjection layout; Channel/Contributions (surface-only) are appended last.
struct CoverageProjection { uint Width; uint Height; uint Count; uint Root; uint TileSize; uint TilesPerRow; uint Channel; uint Contributions; };
[[vk::push_constant]] ConstantBuffer<CoverageProjection> Projection;
[[vk::binding(0, 0)]] StructuredBuffer<VectorCoverageInstruction> Instructions;
[[vk::binding(1, 0)]] [[vk::image_format("rgba32f")]] RWTexture2D<float4> Coverage;
[[vk::binding(2, 0)]] StructuredBuffer<VectorCubicSegment> Segments;
// 📝 CPU-binned per-tile leaf culling (Engine/Render/VectorCoverageTileBinning.h); see VectorCoverage.slang
// for the full rationale -- this Float32 channel shader shares the identical gating, including inside
// EvaluateSurfaceHeight's four neighbour-offset resamples below (each resample's own Point maps to its own
// tile, which VectorLeafVisible recomputes from Point directly rather than trusting the dispatch thread ID).
[[vk::binding(3, 0)]] StructuredBuffer<TileDirectoryEntry> TileDirectory;
[[vk::binding(4, 0)]] StructuredBuffer<uint> TileIndices;
bool VectorLeafVisible(uint Index, float2 Point)
{
    float2 PixelPosition = clamp(Point / 512.0 * float2(Projection.Width, Projection.Height),
        0.0, float2(Projection.Width, Projection.Height) - 1.0);
    uint TilesPerColumn = max((Projection.Height + Projection.TileSize - 1) / Projection.TileSize, 1);
    uint TileX = min(uint(PixelPosition.x) / Projection.TileSize, max(Projection.TilesPerRow, 1) - 1);
    uint TileY = min(uint(PixelPosition.y) / Projection.TileSize, TilesPerColumn - 1);
    TileDirectoryEntry Entry = TileDirectory[TileY * Projection.TilesPerRow + TileX];
    for (uint Scan = 0; Scan < Entry.Count; ++Scan)
        if (TileIndices[Entry.Offset + Scan] == Index) return true;
    return false;
}

float SegmentDistance(float2 Point, float2 Start, float2 End)
{
    float2 Direction = End - Start;
    return length(Point - Start - Direction * saturate(dot(Point - Start, Direction) / max(dot(Direction, Direction), 1e-20)));
}
float2 CubicPoint(VectorCubicSegment Segment, float Parameter)
{
    float2 First = lerp(Segment.StartControl.xy, Segment.StartControl.zw, Parameter);
    float2 Second = lerp(Segment.StartControl.zw, Segment.ControlEnd.xy, Parameter);
    float2 Third = lerp(Segment.ControlEnd.xy, Segment.ControlEnd.zw, Parameter);
    return lerp(lerp(First, Second, Parameter), lerp(Second, Third, Parameter), Parameter);
}
float CubicWinding(VectorCubicSegment Segment, float2 Point)
{
    // 📝 Partition at analytic Y extrema, then solve each monotonic crossing on the GPU.
    float4 Heights = float4(Segment.StartControl.y, Segment.StartControl.w, Segment.ControlEnd.y, Segment.ControlEnd.w);
    float A = -Heights.x + 3 * Heights.y - 3 * Heights.z + Heights.w;
    float B = 3 * Heights.x - 6 * Heights.y + 3 * Heights.z;
    float C = -3 * Heights.x + 3 * Heights.y;
    float Cuts[4]; Cuts[0] = 0; uint Count = 1;
    if (abs(A) > 1e-8)
    {
        float Discriminant = B * B - 3 * A * C;
        if (Discriminant > 0)
        {
            float First = (-B - sqrt(Discriminant)) / (3 * A);
            float Second = (-B + sqrt(Discriminant)) / (3 * A);
            if (First > Second) { float Exchange = First; First = Second; Second = Exchange; }
            if (First > 0 && First < 1) Cuts[Count++] = First;
            if (Second > 0 && Second < 1) Cuts[Count++] = Second;
        }
    }
    else if (abs(B) > 1e-8)
    {
        float Critical = -C / (2 * B);
        if (Critical > 0 && Critical < 1) Cuts[Count++] = Critical;
    }
    Cuts[Count++] = 1;
    float Winding = 0;
    for (uint Interval = 1; Interval < Count; ++Interval)
    {
        float Lower = Cuts[Interval - 1], Upper = Cuts[Interval];
        float StartY = CubicPoint(Segment, Lower).y, EndY = CubicPoint(Segment, Upper).y;
        bool Ascending = EndY > StartY;
        if (!(Point.y >= min(StartY, EndY) && Point.y < max(StartY, EndY))) continue;
        for (uint Iteration = 0; Iteration < 24; ++Iteration)
        {
            float Centre = (Lower + Upper) * .5;
            if ((CubicPoint(Segment, Centre).y < Point.y) == Ascending) Lower = Centre; else Upper = Centre;
        }
        if (CubicPoint(Segment, (Lower + Upper) * .5).x > Point.x) Winding += Ascending ? 1 : -1;
    }
    return Winding;
}
float ShapeDistance(VectorCoverageInstruction Instruction, float2 Local)
{
    uint Category = Instruction.Topology.x;
    float2 HalfExtent = Instruction.Bounds.zw * .5;
    if (Category == 0)
    {
        float2 Outside = abs(Local) - HalfExtent;
        return length(max(Outside, 0)) + min(max(Outside.x, Outside.y), 0);
    }
    if (Category == 1) return (length(Local / HalfExtent) - 1) * min(HalfExtent.x, HalfExtent.y);
    if (Category == 6)
    {
        // 📝 Unpainted construction contours evaluate no fill/stroke response.
        if (Instruction.Paint.w == 0 && Instruction.Transform.y <= 0) return 1e10;
        float Antialias = 512.0 / min(Projection.Width, Projection.Height);
        float Knee = max(Instruction.Transform.y, 0) * .5 + Antialias * .5;
        float Winding = 0, Distance = 1e10;
        for (uint Curve = 0; Curve < Instruction.Topology.z; ++Curve)
        {
            VectorCubicSegment Segment = Segments[Instruction.Topology.y + Curve];
            Segment.StartControl *= float4(Instruction.Bounds.zw, Instruction.Bounds.zw);
            Segment.ControlEnd *= float4(Instruction.Bounds.zw, Instruction.Bounds.zw);
            // 📝 Control-cage rejects: rows outside the convex-hull cage cross nothing (exact zero
            // winding), and cages beyond the capped best minimum cannot move the saturated fill/stroke
            // response. Both skips are output-identical; they only elide provably dead curve work.
            float2 CageMinimum = min(min(Segment.StartControl.xy, Segment.StartControl.zw),
                                     min(Segment.ControlEnd.xy, Segment.ControlEnd.zw));
            float2 CageMaximum = max(max(Segment.StartControl.xy, Segment.StartControl.zw),
                                     max(Segment.ControlEnd.xy, Segment.ControlEnd.zw));
            if (Local.y >= CageMinimum.y && Local.y < CageMaximum.y)
                Winding += CubicWinding(Segment, Local);
            float2 Outside = max(max(CageMinimum - Local, Local - CageMaximum), 0);
            if (length(Outside) > min(Distance, Knee) + 1e-3) continue;
            // 📝 Wang's formula (mirror EstimateCubicFlattenSegments in VectorCoverage.h exactly): small/flat
            // ornamental curves collapse to as few as 2 chords instead of the former fixed 32, while sharp or
            // large curves still subdivide enough to stay within a quarter-pixel of the analytic curve. The
            // winding above stays exact regardless (it solves the cubic analytically, never flattens), so
            // this only ever changes antialiasing/stroke-edge smoothness, never fill/hole correctness.
            float2 SecondDifferenceA = Segment.StartControl.xy - 2 * Segment.StartControl.zw + Segment.ControlEnd.xy;
            float2 SecondDifferenceB = Segment.StartControl.zw - 2 * Segment.ControlEnd.xy + Segment.ControlEnd.zw;
            float ControlDeviation = max(length(SecondDifferenceA), length(SecondDifferenceB));
            float FlattenTolerance = max(Antialias * .25, 1e-4);
            uint SegmentCount = uint(clamp(ceil(sqrt(ControlDeviation * 3.0 / (4.0 * FlattenTolerance))), 2.0, 32.0));
            float2 Start = Segment.StartControl.xy;
            for (uint Interval = 1; Interval <= SegmentCount; ++Interval)
            {
                float2 End = CubicPoint(Segment, Interval / float(SegmentCount));
                Distance = min(Distance, SegmentDistance(Local, Start, End)); Start = End;
            }
        }
        bool Interior = (Instruction.Topology.w & 1) != 0 ? (uint(abs(Winding)) % 2 != 0) : abs(Winding) > .5;
        return Interior ? -Distance : Distance;
    }
    uint Count = Category == 2 ? 4 : Category == 3 ? 3 : uint(Instruction.Paint.z);
    if (Category == 5) Count *= 2;
    float Distance = 1e10; bool Inside = false;
    float2 Previous = 0;
    for (uint Corner = 0; Corner <= Count; ++Corner)
    {
        float Angle = -1.57079632679 + 6.28318530718 * (Corner % Count) / Count;
        float Radius = Category == 5 && Corner % 2 == 1 ? Instruction.Paint.y : 1;
        float2 Current = float2(cos(Angle), sin(Angle)) * HalfExtent * Radius;
        if (Corner > 0)
        {
            Distance = min(Distance, SegmentDistance(Local, Previous, Current));
            if ((Previous.y > Local.y) != (Current.y > Local.y) && Local.x < (Current.x - Previous.x) * (Local.y - Previous.y) / (Current.y - Previous.y) + Previous.x) Inside = !Inside;
        }
        Previous = Current;
    }
    return Inside ? -Distance : Distance;
}
void EvaluateVectorPoint(float2 Point, float Antialias, out float4 Evaluations[InstructionCapacity])
{
    for (uint Index = 0; Index < Projection.Count; ++Index)
    {
        VectorCoverageInstruction Instruction = Instructions[Index];
        uint Category = Instruction.Topology.x;
        float4 Result = 0;
        if (Category <= 6 && VectorLeafVisible(Index, Point))
        {
            float2 Translation = Point - Instruction.Bounds.xy;
            float Sine = sin(Instruction.Transform.x), Cosine = cos(Instruction.Transform.x);
            float2 Local = float2(Cosine * Translation.x + Sine * Translation.y, -Sine * Translation.x + Cosine * Translation.y);
            float Distance = ShapeDistance(Instruction, Local);
            float Fill = Instruction.Paint.w != 0 ? saturate(.5 - Distance / Antialias) : 0;
            float Stroke = Instruction.Transform.y > 0 ? saturate(.5 - (abs(Distance) - Instruction.Transform.y * .5) / Antialias) : 0;
            float Fraction = Instruction.Transform.w == 1 ? saturate(.5 + dot(Local / Instruction.Bounds.zw, float2(cos(Instruction.Paint.x), sin(Instruction.Paint.x)))) : Instruction.Transform.w == 2 ? saturate(length(Local / Instruction.Bounds.zw) * 2) : 0;
            float4 FillColour = lerp(Instruction.Colour, Instruction.ColourEnd, Fraction);
            float4 StrokeColour = (Instruction.Topology.w & 0x80000000) != 0 ? Instruction.StrokeColour : Instruction.Colour;
            float FillAlpha = Fill * FillColour.a * Instruction.Transform.z;
            float StrokeAlpha = Stroke * StrokeColour.a * Instruction.Transform.z;
            Result.a = StrokeAlpha + FillAlpha * (1 - StrokeAlpha);
            Result.rgb = Result.a > 0
                ? (StrokeColour.rgb * StrokeAlpha + FillColour.rgb * FillAlpha * (1 - StrokeAlpha)) / Result.a
                : 0;
        }
        else if (Category > 6)
        {
            float4 Lower = Evaluations[Instruction.Topology.y], Upper = Evaluations[Instruction.Topology.z];
            Result = Lower;
            if (Category == 16) Result.a = max(Lower.a, Upper.a);
            if (Category == 17) Result.a = min(Lower.a, 1 - Upper.a);
            if (Category == 18) Result.a = min(Lower.a, Upper.a);
            if (Category == 19) Result.a = max(min(Lower.a, 1 - Upper.a), min(Upper.a, 1 - Lower.a));
            if (Category == 24 || Category == 25)
            {
                float Mask = Upper.a * (Category == 25 ? dot(Upper.rgb, float3(.2126, .7152, .0722)) : 1);
                if (Instruction.Topology.w != 0) Mask = 1 - Mask;
                Result.a = Lower.a * Mask * Instruction.Transform.z;
            }
            if (Category == 28)
            {
                Result.a = Upper.a + Lower.a * (1 - Upper.a);
                Result.rgb = Result.a > 0 ? (Upper.rgb * Upper.a + Lower.rgb * Lower.a * (1 - Upper.a)) / Result.a : 0;
            }
        }
        Evaluations[Index] = Result;
    }
}
float EvaluateSurfaceHeight(float2 Point, float Antialias)
{
    float4 Evaluations[InstructionCapacity];
    EvaluateVectorPoint(Point, Antialias, Evaluations);
    float Height = 0;
    for (uint Index = 0; Index < Projection.Contributions; ++Index)
    {
        VectorCoverageInstruction Contribution = Instructions[InstructionCapacity + Index];
        float Alpha = Evaluations[Contribution.Topology.x].a;
        uint Enabled = Contribution.Topology.z;
        // Derived normals consume a layer only when both authored channels are
        // present; Normal strength scales its height contribution, not the height map.
        if ((Enabled & (1u << 1)) != 0 && (Enabled & (1u << 2)) != 0)
            Height = lerp(Height, Contribution.Transform.w * Contribution.Bounds.y * Contribution.Bounds.z, saturate(Alpha));
    }
    return Height;
}
[shader("compute")]
[numthreads(8, 8, 1)]
void VectorSurfaceCompute(uint3 Pixel : SV_DispatchThreadID)
{
    if (Pixel.x >= Projection.Width || Pixel.y >= Projection.Height) return;
    float2 Point = (float2(Pixel.xy) + .5) / float2(Projection.Width, Projection.Height) * 512;
    float Antialias = 512.0 / min(Projection.Width, Projection.Height);
    float4 Evaluations[InstructionCapacity];
    EvaluateVectorPoint(Point, Antialias, Evaluations);
    float4 Colour = 0;
    float ChannelAlpha = 0;
    for (uint Index = 0; Index < Projection.Contributions; ++Index)
    {
        VectorCoverageInstruction Contribution = Instructions[InstructionCapacity + Index];
        if ((Contribution.Topology.z & (1u << Projection.Channel)) == 0) continue;
        float4 LayerColour = Evaluations[Contribution.Topology.x];
        float OutputAlpha = LayerColour.a + ChannelAlpha * (1 - LayerColour.a);
        Colour.rgb = OutputAlpha > 0
            ? (LayerColour.rgb * LayerColour.a + Colour.rgb * ChannelAlpha * (1 - LayerColour.a)) / OutputAlpha
            : 0;
        ChannelAlpha = OutputAlpha;
    }
    float3 Surface = float3(.5, .5, .5);
    if (Projection.Channel == 0)
    {
        float3 Display = lerp(float3(.5), Colour.rgb, ChannelAlpha);
        Surface = select(Display <= .04045, Display / 12.92, pow((Display + .055) / 1.055, 2.4));
    }
    else if (Projection.Channel == 1)
    {
        Surface = 0;
        for (uint Index = 0; Index < Projection.Contributions; ++Index)
        {
            VectorCoverageInstruction Contribution = Instructions[InstructionCapacity + Index];
            float CoverageAlpha = Evaluations[Contribution.Topology.x].a;
            if ((Contribution.Topology.z & 2u) != 0)
                Surface = lerp(Surface, float3(Contribution.Transform.w * Contribution.Bounds.y), saturate(CoverageAlpha));
        }
    }
    else if (Projection.Channel == 2)
    {
        float Step = 512.0 / min(Projection.Width, Projection.Height);
        float Left = EvaluateSurfaceHeight(Point - float2(Step, 0), Antialias);
        float Right = EvaluateSurfaceHeight(Point + float2(Step, 0), Antialias);
        float Upper = EvaluateSurfaceHeight(Point - float2(0, Step), Antialias);
        float Lower = EvaluateSurfaceHeight(Point + float2(0, Step), Antialias);
        float3 Normal = normalize(float3(-(Right - Left), -(Lower - Upper), 1));
        Surface = Normal * .5 + .5;
    }
    else
    {
        float Default = Projection.Channel == 3 ? .7 : Projection.Channel == 5 ? ChannelAlpha : Projection.Channel == 6 ? 1 : 0;
        Surface = float3(Default);
        for (uint Index = 0; Index < Projection.Contributions; ++Index)
        {
            VectorCoverageInstruction Contribution = Instructions[InstructionCapacity + Index];
            float CoverageAlpha = Evaluations[Contribution.Topology.x].a;
            uint Channel = Projection.Channel;
            if ((Contribution.Topology.z & (1u << Channel)) == 0) continue;
            float Strength = Channel < 4 ? Contribution.Bounds[Channel] : Contribution.Transform[Channel - 4];
            float Target = Channel == 6 ? 1 - Strength : Strength;
            if (Channel == 5) Surface = float3(ChannelAlpha);
            else Surface = lerp(Surface, float3(Target), saturate(CoverageAlpha));
        }
    }
    Coverage[Pixel.xy] = float4(Surface, ChannelAlpha);
}
