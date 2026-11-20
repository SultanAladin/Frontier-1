/*============================================================================================================================================
                                                      STELLARTILECULLCOMPUTE.SLANG
============================================================================================================================================*/
// 🧩 eos 16x16 Screen-Space Stellar Tile Culling + J2000 Catalog Projection (1640 BSC5 Stars -> 64-entry Tile Bins for Erf/Moffat PSF)

static const float CullPiConst           = 3.14159265359;
static const float CullTauConst          = 6.28318530718;
static const float CullSiteSiderealZero  = 5.45 * (6.28318530718 / 24.0);
static const float CullSiteObsLatRad     = -26.19 * (3.14159265359 / 180.0);
static const uint  StellarTileStride     = 65u; // [Count, 64 x StarIndex]
static const uint  MaxStarsPerTile       = 64u;

struct StellarCullUniformBlock
{
    float4 CameraForward;     // xyz = UnitForward, w = CelestialRotationRad
    float4 CameraRightTanX;   // xyz = UnitRight,   w = TanHalfFovX
    float4 CameraUpTanY;      // xyz = UnitUp,      w = TanHalfFovY
    float4 ViewportCatalog;   // x = WidthPx, y = HeightPx, z = CatalogCount, w = LimitingMagnitude
    float4 SkyFlags;          // x = StarsEnabled, y = BrightCatalogEnabled, z = OrbitalView, w = BrightnessMultiplier
};

struct GpuRawStarSpec
{
    float4 EqDirMag;   // xyz = UnitJ2000Equatorial, w = MagnitudeV
    float4 ColorSeed;  // rgb = PlanckianRec709,   w = Seed01
};

struct GpuProjectedStar
{
    float4 PixPosFlux;       // x = PixX, y = PixY, z = PogsonFlux, w = Seed01
    float4 ChromaticityElev; // rgb = PlanckianRec709, w = HorizonSinElev
};

[[vk::binding(0, 0)]] ConstantBuffer<StellarCullUniformBlock> CullUniforms;
[[vk::binding(1, 0)]] StructuredBuffer<GpuRawStarSpec>        RawCatalogStars;
[[vk::binding(2, 0)]] RWStructuredBuffer<GpuProjectedStar>    ProjectedCatalogStars;
[[vk::binding(3, 0)]] RWStructuredBuffer<uint>                StellarTileBins;

float3 TransformEqToHorizonCull(float3 EqDir, float CelestialRotRad)
{
    float Lst    = CullSiteSiderealZero + CelestialRotRad;
    float Ra     = atan2(EqDir.y, EqDir.x);
    float Dec    = asin(clamp(EqDir.z, -1.0, 1.0));
    float CosD   = cos(Dec);
    float Hour   = Lst - Ra;
    float SinAlt = sin(Dec) * sin(CullSiteObsLatRad) + CosD * cos(CullSiteObsLatRad) * cos(Hour);
    float Alt    = asin(clamp(SinAlt, -1.0, 1.0));
    float CosAlt = max(1.0e-5, cos(Alt));
    float CosAz  = (sin(Dec) - SinAlt * sin(CullSiteObsLatRad)) / (CosAlt * cos(CullSiteObsLatRad));
    float SinAz  = -CosD * sin(Hour) / CosAlt;
    float Az     = atan2(SinAz, CosAz);
    return float3(CosAlt * cos(Az), -CosAlt * sin(Az), SinAlt);
}

[shader("compute")]
[numthreads(64, 1, 1)]
void ClearStellarTilesEntry(uint3 DispatchThreadID : SV_DispatchThreadID)
{
    uint W        = max(1u, uint(CullUniforms.ViewportCatalog.x + 0.5));
    uint H        = max(1u, uint(CullUniforms.ViewportCatalog.y + 0.5));
    uint Cols     = (W + 15u) / 16u;
    uint Rows     = (H + 15u) / 16u;
    uint TileIdx  = DispatchThreadID.x;
    if (TileIdx >= Cols * Rows)
    {
        return;
    }
    StellarTileBins[TileIdx * StellarTileStride] = 0u;
}

[shader("compute")]
[numthreads(64, 1, 1)]
void ProjectCullStellarTilesEntry(uint3 DispatchThreadID : SV_DispatchThreadID)
{
    uint StarIdx   = DispatchThreadID.x;
    uint StarCount = uint(CullUniforms.ViewportCatalog.z + 0.5);
    if (StarIdx >= StarCount)
    {
        return;
    }

    GpuProjectedStar Culled;
    Culled.PixPosFlux       = float4(-1000.0, -1000.0, 0.0, 0.0);
    Culled.ChromaticityElev = float4(0.0, 0.0, 0.0, 0.0);

    if (CullUniforms.SkyFlags.x < 0.5 || CullUniforms.SkyFlags.y < 0.5)
    {
        ProjectedCatalogStars[StarIdx] = Culled;
        return;
    }

    GpuRawStarSpec Raw    = RawCatalogStars[StarIdx];
    float          LimMag = CullUniforms.ViewportCatalog.w + 0.5;
    if (Raw.EqDirMag.w > LimMag)
    {
        ProjectedCatalogStars[StarIdx] = Culled;
        return;
    }

    float3 HorizonDir = TransformEqToHorizonCull(normalize(Raw.EqDirMag.xyz), CullUniforms.CameraForward.w);
    bool   Orbital    = (CullUniforms.SkyFlags.z > 0.5);
    if (!Orbital && HorizonDir.z < -0.02)
    {
        ProjectedCatalogStars[StarIdx] = Culled;
        return;
    }

    float Depth = dot(HorizonDir, CullUniforms.CameraForward.xyz);
    if (Depth <= 1.0e-4)
    {
        ProjectedCatalogStars[StarIdx] = Culled;
        return;
    }

    float W    = max(1.0, CullUniforms.ViewportCatalog.x);
    float H    = max(1.0, CullUniforms.ViewportCatalog.y);
    float NdcX = dot(HorizonDir, CullUniforms.CameraRightTanX.xyz) / (Depth * max(1.0e-4, CullUniforms.CameraRightTanX.w));
    float NdcY = dot(HorizonDir, CullUniforms.CameraUpTanY.xyz)    / (Depth * max(1.0e-4, CullUniforms.CameraUpTanY.w));
    float PixX = (0.5 + 0.5 * NdcX) * W;
    float PixY = (0.5 - 0.5 * NdcY) * H;

    float Reach = 12.0;
    if (PixX < -Reach || PixY < -Reach || PixX > W + Reach || PixY > H + Reach)
    {
        ProjectedCatalogStars[StarIdx] = Culled;
        return;
    }

    float Flux = 14.0 * pow(10.0, -0.4 * Raw.EqDirMag.w);
    Culled.PixPosFlux       = float4(PixX, PixY, Flux, Raw.ColorSeed.w);
    Culled.ChromaticityElev = float4(Raw.ColorSeed.xyz, HorizonDir.z);
    ProjectedCatalogStars[StarIdx] = Culled;

    uint Cols  = (uint(W + 0.5) + 15u) / 16u;
    uint Rows  = (uint(H + 0.5) + 15u) / 16u;
    int  LowX  = clamp(int(floor((PixX - Reach - 1.0) / 16.0)), 0, int(Cols) - 1);
    int  LowY  = clamp(int(floor((PixY - Reach - 1.0) / 16.0)), 0, int(Rows) - 1);
    int  HighX = clamp(int(floor((PixX + Reach + 1.0) / 16.0)), 0, int(Cols) - 1);
    int  HighY = clamp(int(floor((PixY + Reach + 1.0) / 16.0)), 0, int(Rows) - 1);

    for (int TY = LowY; TY <= HighY; ++TY)
    {
        for (int TX = LowX; TX <= HighX; ++TX)
        {
            uint TileIdx = uint(TY) * Cols + uint(TX);
            uint Slot    = 0u;
            InterlockedAdd(StellarTileBins[TileIdx * StellarTileStride], 1u, Slot);
            if (Slot < MaxStarsPerTile)
            {
                StellarTileBins[TileIdx * StellarTileStride + 1u + Slot] = StarIdx;
            }
        }
    }
}
