//==========================================================================================================================================
//                                                  PROCEDURALFIELDGENERATORS.SLANG
//==========================================================================================================================================
// Modular GPU procedural field generators matching DynamicSurfaceResolver.cpp (Voronoi faults, Hex lattice, Gerstner ocean, rain beads)

module ProceduralFieldGenerators;

public struct VoronoiEdgeGpuSample
{
    public float EdgeDistance;
    public float CellId;
};

public struct HexCellGpuSample
{
    public float EdgeDistance;
    public float CellHash;
};

public float Hash21Gpu(float2 P)
{
    return frac(sin(dot(P, float2(127.1, 311.7))) * 43758.5453123);
}

public float Hash31Gpu(float3 P)
{
    return frac(sin(dot(P, float3(127.1, 311.7, 74.7))) * 43758.5453123);
}

public float LatticeNoise2Gpu(float2 P)
{
    float2 I = floor(P);
    float2 F = frac(P);
    float2 U = F * F * (3.0 - 2.0 * F);
    float  A = Hash21Gpu(I);
    float  B = Hash21Gpu(I + float2(1.0, 0.0));
    float  C = Hash21Gpu(I + float2(0.0, 1.0));
    float  D = Hash21Gpu(I + float2(1.0, 1.0));
    return lerp(lerp(A, B, U.x), lerp(C, D, U.x), U.y);
}

public float Fbm2Gpu(float2 P)
{
    float  Sum = 0.0;
    float  Amp = 0.5;
    float2 Cur = P;
    [unroll]
    for (int Oct = 0; Oct < 4; ++Oct)
    {
        Sum += Amp * LatticeNoise2Gpu(Cur);
        Cur = float2(1.62 * Cur.x - 1.21 * Cur.y + 11.3, 1.21 * Cur.x + 1.62 * Cur.y + 7.1);
        Amp *= 0.5;
    }
    return Sum;
}

public VoronoiEdgeGpuSample EvaluateVoronoiEdgeGpu(float2 P)
{
    float2 I = floor(P);
    float2 F = frac(P);
    float  BestDist   = 8.0;
    float  SecondDist = 8.0;
    float  BestId     = 0.0;

    [unroll]
    for (int DY = -1; DY <= 1; ++DY)
    {
        [unroll]
        for (int DX = -1; DX <= 1; ++DX)
        {
            float2 G = I + float2(float(DX), float(DY));
            float2 O = float2(0.14 + 0.72 * Hash21Gpu(G), 0.14 + 0.72 * Hash21Gpu(G + float2(19.7, 41.3)));
            float2 R = float2(float(DX), float(DY)) + O - F;
            float  D = length(R);
            if (D < BestDist)
            {
                SecondDist = BestDist;
                BestDist   = D;
                BestId     = Hash21Gpu(G + float2(7.3, 13.1));
            }
            else if (D < SecondDist)
            {
                SecondDist = D;
            }
        }
    }

    VoronoiEdgeGpuSample Result;
    Result.EdgeDistance = max(0.0, SecondDist - BestDist);
    Result.CellId       = BestId;
    return Result;
}

public HexCellGpuSample EvaluateHexLatticeGpu(float2 P)
{
    float2 S = float2(1.0, 1.7320508);
    float2 H = 0.5 * S;
    float2 A = P - S * floor(P / S) - H;
    float2 B = (P - H) - S * floor((P - H) / S) - H;
    float2 G = (dot(A, A) < dot(B, B)) ? A : B;
    float2 Id = floor(P - G + 0.5);
    float2 AbsG = abs(G);
    float  EdgeDist = 0.5 - max(AbsG.x * 0.5 + AbsG.y * 0.8660254, AbsG.x);

    HexCellGpuSample Result;
    Result.EdgeDistance = max(0.0, EdgeDist);
    Result.CellHash     = Hash21Gpu(Id);
    return Result;
}

public float3 EvaluateMagmaBlackbodyGpu(float Heat)
{
    float  H         = saturate(Heat);
    float3 DarkCrust = float3(0.14, 0.015, 0.002);
    float3 DeepRed   = float3(0.92, 0.12,  0.01);
    float3 MoltenOrg = float3(1.00, 0.48,  0.04);
    float3 WhiteHot  = float3(1.00, 0.94,  0.64);
    if (H < 0.35)
    {
        return lerp(DarkCrust, DeepRed, H / 0.35);
    }
    if (H < 0.72)
    {
        return lerp(DeepRed, MoltenOrg, (H - 0.35) / 0.37);
    }
    return lerp(MoltenOrg, WhiteHot, (H - 0.72) / 0.28);
}

public float EvaluateGerstnerOceanHeightGpu(float2 Uv, float Scale, float TimeSec, float WaveAmplitude)
{
    float Amp = WaveAmplitude * 0.082;
    float W1  = sin(Uv.x * Scale * 4.2 + Uv.y * Scale * 2.2 + TimeSec * 1.7);
    float W2  = sin(-Uv.x * Scale * 3.1 + Uv.y * Scale * 5.0 - TimeSec * 1.35) * 0.68;
    float W3  = sin(Uv.x * Scale * 7.4 - Uv.y * Scale * 4.1 + TimeSec * 2.4) * 0.42;
    float Cap = (Fbm2Gpu(Uv * Scale * 6.5 + float2(TimeSec * 0.45, -TimeSec * 0.35)) - 0.5) * 0.60;
    return (W1 + W2 + W3 + Cap) * Amp;
}

public float EvaluateRainBeadHeightGpu(float2 Uv, float Scale, float TimeSec, float RainIntensity)
{
    // Staggered organic raindrop cells (non-grid jittered droplets + tiny condensation beads)
    float2 Grid1 = float2(Uv.x * Scale * 4.4, Uv.y * Scale * 2.6);
    float2 Cell1 = floor(Grid1);
    float  JitterX = (Hash21Gpu(Cell1 + float2(3.1, 7.7)) - 0.5) * 0.62;
    float  Speed1  = 0.35 + 0.65 * Hash21Gpu(float2(Cell1.x, 13.7));
    float  YSlide  = Uv.y * Scale * 2.6 - TimeSec * Speed1 + Hash21Gpu(float2(Cell1.x, 29.1)) * 11.0;
    float2 Local1  = float2(frac(Grid1.x) - 0.5 - JitterX * 0.55, frac(YSlide) - 0.5);
    float  Drop1   = smoothstep(0.24, 0.0, length(Local1 * float2(1.65, 1.25)));

    float2 Grid2   = Uv * Scale * 11.5;
    float2 Cell2   = floor(Grid2);
    float2 Jit2    = float2(Hash21Gpu(Cell2), Hash21Gpu(Cell2 + float2(17.3, 41.9))) - 0.5;
    float2 Local2  = frac(Grid2) - 0.5 - Jit2 * 0.58;
    float  Present = step(0.42, Hash21Gpu(Cell2 + float2(5.1, 2.7)));
    float  Micro   = smoothstep(0.22, 0.0, length(Local2)) * Present;
    return (Drop1 * 0.055 + Micro * 0.024) * RainIntensity;
}
