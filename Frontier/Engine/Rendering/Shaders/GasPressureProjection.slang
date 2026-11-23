//============================================================================================================================================
//                                                      GASPRESSUREPROJECTION.SLANG
//============================================================================================================================================
// 🧩 Experimental closed-domain gas projection with matched face-flux, pressure and gradient stencils.
//    Each velocity component is the flux through that cell's positive face; .w marks a static solid cell.
//    This is NOT compatible with the demo's cell-centred advection texture without a separate conversion/advection design.

struct GasProjectionFootprint
{
    uint4 Extent;   // xyz cell dimensions; w reserved zero
    float4 Control; // x cell spacing, y weighted-Jacobi relaxation, zw reserved zero
};

[[vk::binding(0, 0)]] ConstantBuffer<GasProjectionFootprint> Gas;
[[vk::binding(1, 0)]] StructuredBuffer<float4> FaceVelocity;
[[vk::binding(2, 0)]] RWStructuredBuffer<float> Divergence;
[[vk::binding(3, 0)]] StructuredBuffer<float> PressureSource;
[[vk::binding(4, 0)]] RWStructuredBuffer<float> PressureTarget;
[[vk::binding(5, 0)]] RWStructuredBuffer<float4> ProjectedVelocity;

bool GasDimensionsValid()
{
    return Gas.Extent.x > 0 && Gas.Extent.x <= 256 && Gas.Extent.y > 0 && Gas.Extent.y <= 256 &&
           Gas.Extent.z > 0 && Gas.Extent.z <= 256 && Gas.Extent.w == 0 &&
           isfinite(Gas.Control.x) && Gas.Control.x >= 0.0001 && Gas.Control.x <= 10000.0 &&
           isfinite(Gas.Control.y) && Gas.Control.y > 0.0 && Gas.Control.y < 1.0 &&
           Gas.Control.z == 0.0 && Gas.Control.w == 0.0;
}
uint GasCellCount() { return Gas.Extent.x * Gas.Extent.y * Gas.Extent.z; }
bool GasInside(int3 Cell)
{
    return all(Cell >= int3(0, 0, 0)) && all(Cell < int3(Gas.Extent.xyz));
}
uint GasIndex(int3 Cell)
{
    return (uint(Cell.z) * Gas.Extent.y + uint(Cell.y)) * Gas.Extent.x + uint(Cell.x);
}
bool GasFluid(int3 Cell)
{
    if (!GasInside(Cell)) return false;
    return FaceVelocity[GasIndex(Cell)].w <= 0.5;
}
bool GasFaceOpen(int3 Cell, int3 Offset)
{
    return GasFluid(Cell) && GasFluid(Cell + Offset);
}
float GasNeighbourPressure(int3 Cell, float Centre)
{
    if (GasFluid(Cell)) return PressureSource[GasIndex(Cell)];
    return Centre;
}

// 📝 Negative-face flux is the preceding fluid cell's positive-face flux; all outer/solid faces are closed.
[shader("compute")]
[numthreads(4, 4, 4)]
void MeasureGasDivergence(uint3 DispatchThread : SV_DispatchThreadID)
{
    if (!GasDimensionsValid() || any(DispatchThread >= Gas.Extent.xyz)) return;
    uint VelocityCount, VelocityStride, DivergenceCount, DivergenceStride;
    FaceVelocity.GetDimensions(VelocityCount, VelocityStride);
    Divergence.GetDimensions(DivergenceCount, DivergenceStride);
    if (VelocityCount < GasCellCount() || DivergenceCount < GasCellCount()) return;
    int3 Cell = int3(DispatchThread);
    uint Index = GasIndex(Cell);
    if (!GasFluid(Cell)) { Divergence[Index] = 0.0; return; }
    int3 X = int3(1, 0, 0), Y = int3(0, 1, 0), Z = int3(0, 0, 1);
    float Flux = 0.0;
    if (GasFaceOpen(Cell, X)) Flux += FaceVelocity[Index].x;
    if (GasFaceOpen(Cell - X, X)) Flux -= FaceVelocity[GasIndex(Cell - X)].x;
    if (GasFaceOpen(Cell, Y)) Flux += FaceVelocity[Index].y;
    if (GasFaceOpen(Cell - Y, Y)) Flux -= FaceVelocity[GasIndex(Cell - Y)].y;
    if (GasFaceOpen(Cell, Z)) Flux += FaceVelocity[Index].z;
    if (GasFaceOpen(Cell - Z, Z)) Flux -= FaceVelocity[GasIndex(Cell - Z)].z;
    Divergence[Index] = Flux / Gas.Control.x;
}

// 📝 Weighted Jacobi damps the closed-grid checkerboard mode; the constant-pressure gauge remains free.
[shader("compute")]
[numthreads(4, 4, 4)]
void RelaxGasPressure(uint3 DispatchThread : SV_DispatchThreadID)
{
    if (!GasDimensionsValid() || any(DispatchThread >= Gas.Extent.xyz)) return;
    uint VC, VS, DC, DS, PC, PS, TC, TS;
    FaceVelocity.GetDimensions(VC, VS); Divergence.GetDimensions(DC, DS);
    PressureSource.GetDimensions(PC, PS); PressureTarget.GetDimensions(TC, TS);
    uint Count = GasCellCount();
    if (VC < Count || DC < Count || PC < Count || TC < Count) return;
    int3 Cell = int3(DispatchThread);
    uint Index = GasIndex(Cell);
    if (!GasFluid(Cell)) { PressureTarget[Index] = 0.0; return; }
    float Centre = PressureSource[Index];
    int3 X = int3(1, 0, 0), Y = int3(0, 1, 0), Z = int3(0, 0, 1);
    float Sum = GasNeighbourPressure(Cell + X, Centre) + GasNeighbourPressure(Cell - X, Centre)
              + GasNeighbourPressure(Cell + Y, Centre) + GasNeighbourPressure(Cell - Y, Centre)
              + GasNeighbourPressure(Cell + Z, Centre) + GasNeighbourPressure(Cell - Z, Centre);
    float Jacobi = (Sum - Divergence[Index] * Gas.Control.x * Gas.Control.x) / 6.0;
    PressureTarget[Index] = Centre + Gas.Control.y * (Jacobi - Centre);
}

// 📝 Face gradient is paired with the backward face-divergence; D(G(q)) is exactly the Jacobi Laplacian.
[shader("compute")]
[numthreads(4, 4, 4)]
void ProjectGasVelocity(uint3 DispatchThread : SV_DispatchThreadID)
{
    if (!GasDimensionsValid() || any(DispatchThread >= Gas.Extent.xyz)) return;
    uint VC, VS, PC, PS, OC, OS;
    FaceVelocity.GetDimensions(VC, VS); PressureSource.GetDimensions(PC, PS);
    ProjectedVelocity.GetDimensions(OC, OS);
    uint Count = GasCellCount();
    if (VC < Count || PC < Count || OC < Count) return;
    int3 Cell = int3(DispatchThread);
    uint Index = GasIndex(Cell);
    float4 Own = FaceVelocity[Index];
    if (!GasFluid(Cell)) { ProjectedVelocity[Index] = Own; return; }
    float3 Result = float3(0.0, 0.0, 0.0);
    float Centre = PressureSource[Index];
    int3 X = int3(1, 0, 0), Y = int3(0, 1, 0), Z = int3(0, 0, 1);
    if (GasFaceOpen(Cell, X)) Result.x = Own.x - (PressureSource[GasIndex(Cell + X)] - Centre) / Gas.Control.x;
    if (GasFaceOpen(Cell, Y)) Result.y = Own.y - (PressureSource[GasIndex(Cell + Y)] - Centre) / Gas.Control.x;
    if (GasFaceOpen(Cell, Z)) Result.z = Own.z - (PressureSource[GasIndex(Cell + Z)] - Centre) / Gas.Control.x;
    ProjectedVelocity[Index] = float4(Result, Own.w);
}
