//============================================================================================================================================
//                                                          CONCAVEHORIZONCOMPUTE.SLANG
//============================================================================================================================================
// Parallel GPU candidate placement scorer evaluating concave skyline horizon height + wasted pocket area across columns & rotations

struct SkylineCandidateQuery
{
    uint   IslandFootprintCols; // Width in discrete skyline columns
    uint   RotationIndex;       // Candidate rotation index (0..3)
    uint   CandidateColumnX;    // Leftmost skyline column [0 .. GridCols - FootprintCols]
    uint   GridColumns;         // Skyline resolution (e.g. 256 or 512)
};

struct SkylineScoreResult
{
    float PlacementScore; // Lower is tighter (PeakY * 1.0 + WastedArea * 0.35 + ColumnX * 0.002)
    uint  LandingFloorY;   // Lowest collision-free Y row for this island footprint
    uint  CandidateIndex; // Echoed query index
    uint  PlacementValid;      // 1 = Fits inside tile ceiling, 0 = Overflow
};

struct SkylineDispatchParams
{
    uint CandidateCount;
    uint TileCeilingRows;
    uint IslandProfileOffset;
    uint ReservedPad;
};

[[vk::binding(0, 0)]] ConstantBuffer<SkylineDispatchParams>   Params;
[[vk::binding(1, 0)]] StructuredBuffer<uint>                  SkylineHeightColumns; // [GridColumns] current upper horizon
[[vk::binding(2, 0)]] StructuredBuffer<int2>                  IslandBottomTopSpan;  // .x = BottomOffset, .y = TopOffset per footprint column
[[vk::binding(3, 0)]] StructuredBuffer<SkylineCandidateQuery> Queries;
[[vk::binding(4, 0)]] RWStructuredBuffer<SkylineScoreResult>  Results;

[shader("compute")]
[numthreads(64, 1, 1)]
void ComputeEntry(uint3 DispatchThreadID : SV_DispatchThreadID)
{
    uint QIdx = DispatchThreadID.x;
    if (QIdx >= Params.CandidateCount)
    {
        return;
    }

    SkylineCandidateQuery Q = Queries[QIdx];
    SkylineScoreResult    Out;
    Out.CandidateIndex = QIdx;

    if (Q.CandidateColumnX + Q.IslandFootprintCols > Q.GridColumns || Q.IslandFootprintCols == 0u)
    {
        Out.PlacementScore = 1.0e9;
        Out.LandingFloorY   = Params.TileCeilingRows;
        Out.PlacementValid      = 0u;
        Results[QIdx]      = Out;
        return;
    }

    uint FloorY   = 0u;
    uint PeakTop = 0u;
    uint ProfileStart = Params.IslandProfileOffset + Q.RotationIndex * Q.GridColumns;

    for (uint Col = 0u; Col < Q.IslandFootprintCols; ++Col)
    {
        uint SkyH      = SkylineHeightColumns[Q.CandidateColumnX + Col];
        int2 Span      = IslandBottomTopSpan[ProfileStart + Col];
        int  RequiredY = int(SkyH) - Span.x;
        if (RequiredY > int(FloorY))
        {
            FloorY = uint(RequiredY);
        }
    }

    uint WastedCells = 0u;
    for (uint Col = 0u; Col < Q.IslandFootprintCols; ++Col)
    {
        uint SkyH      = SkylineHeightColumns[Q.CandidateColumnX + Col];
        int2 Span      = IslandBottomTopSpan[ProfileStart + Col];
        uint IslandBot = uint(max(0, int(FloorY) + Span.x));
        uint IslandTop = uint(max(0, int(FloorY) + Span.y));
        if (IslandBot > SkyH)
        {
            WastedCells += (IslandBot - SkyH);
        }
        if (IslandTop > PeakTop)
        {
            PeakTop = IslandTop;
        }
    }

    bool Fits          = (PeakTop <= Params.TileCeilingRows);
    Out.LandingFloorY   = FloorY;
    Out.PlacementValid      = Fits ? 1u : 0u;
    Out.PlacementScore = Fits
        ? (float(PeakTop) + float(WastedCells) * 0.28 + float(Q.CandidateColumnX) * 0.005)
        : (1.0e6 + float(PeakTop));
    Results[QIdx] = Out;
}
