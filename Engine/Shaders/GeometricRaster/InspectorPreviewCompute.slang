/*============================================================================================================================================
                                                      INSPECTORPREVIEWCOMPUTE.SLANG
============================================================================================================================================*/
// 🧩 Unified Pure-GPU Compute Baker for all Inspector previews, LUTs, sky domes, clouds, stars, fogs, rainbows, UVs & SDF slices

struct InspectorPreviewUniforms
{
    uint4  DimensionsAndMode; // x = WidthPx, y = HeightPx, z = ModeIndex, w = Flags
    float4 ParamA;            // Mode-specific scalar params 0..3
    float4 ParamB;            // Mode-specific scalar params 4..7
    float4 ColourA;           // Mode-specific RGB tint A + alpha
    float4 ColourB;           // Mode-specific RGB tint B + alpha
};

[[vk::binding(0, 0)]] ConstantBuffer<InspectorPreviewUniforms> Params;
[[vk::binding(1, 0)]] RWStructuredBuffer<uint>                 OutPixelsRgba8;
[[vk::binding(2, 0)]] StructuredBuffer<uint>                   LunarSurfaceAtlasTexels;

float3 UnpackRgb8(uint Packed)
{
    return float3(
        float(Packed & 0xFFu),
        float((Packed >> 8u) & 0xFFu),
        float((Packed >> 16u) & 0xFFu)) / 255.0;
}

float3 SampleLunarAtlasGpu(uint PresetIdx, float U, float V)
{
    const uint TileW = 512u;
    const uint TileH = 256u;
    const uint SafePreset = min(PresetIdx, 5u);
    float Wu = frac(U);
    float Cv = clamp(V, 0.0, 1.0);
    float Fx = Wu * float(TileW) - 0.5;
    float Fy = Cv * float(TileH) - 0.5;
    int X0 = int(floor(Fx));
    X0 = ((X0 % int(TileW)) + int(TileW)) % int(TileW);
    int X1 = (X0 + 1) % int(TileW);
    int Y0 = clamp(int(floor(Fy)), 0, int(TileH) - 1);
    int Y1 = min(Y0 + 1, int(TileH) - 1);
    float Tx = clamp(Fx - floor(Fx), 0.0, 1.0);
    float Ty = clamp(Fy - floor(Fy), 0.0, 1.0);
    uint RowOff = SafePreset * TileH;
    float3 C00 = UnpackRgb8(LunarSurfaceAtlasTexels[(RowOff + uint(Y0)) * TileW + uint(X0)]);
    float3 C10 = UnpackRgb8(LunarSurfaceAtlasTexels[(RowOff + uint(Y0)) * TileW + uint(X1)]);
    float3 C01 = UnpackRgb8(LunarSurfaceAtlasTexels[(RowOff + uint(Y1)) * TileW + uint(X0)]);
    float3 C11 = UnpackRgb8(LunarSurfaceAtlasTexels[(RowOff + uint(Y1)) * TileW + uint(X1)]);
    return lerp(lerp(C00, C10, Tx), lerp(C01, C11, Tx), Ty);
}

uint PackRgba8(float3 Rgb, float Alpha)
{
    uint R = uint(clamp(Rgb.r * 255.0 + 0.5, 0.0, 255.0));
    uint G = uint(clamp(Rgb.g * 255.0 + 0.5, 0.0, 255.0));
    uint B = uint(clamp(Rgb.b * 255.0 + 0.5, 0.0, 255.0));
    uint A = uint(clamp(Alpha * 255.0 + 0.5, 0.0, 255.0));
    return R | (G << 8u) | (B << 16u) | (A << 24u);
}

float Hash21(float2 P)
{
    float H = dot(P, float2(127.1, 311.7));
    return frac(sin(H) * 43758.5453123);
}

float ValueNoise2D(float2 P)
{
    float2 I = floor(P);
    float2 F = frac(P);
    float2 U = F * F * (3.0 - 2.0 * F);
    float A = Hash21(I + float2(0.0, 0.0));
    float B = Hash21(I + float2(1.0, 0.0));
    float C = Hash21(I + float2(0.0, 1.0));
    float D = Hash21(I + float2(1.0, 1.0));
    return lerp(lerp(A, B, U.x), lerp(C, D, U.x), U.y);
}

float Fbm2D(float2 P)
{
    float Sum = 0.0;
    float Amp = 0.52;
    for (int I = 0; I < 4; ++I)
    {
        Sum += Amp * ValueNoise2D(P);
        P = P * 2.04 + float2(13.7, 7.3);
        Amp *= 0.5;
    }
    return Sum;
}

float3 EvaluateSkyGradient(float Elevation01, float Rayleigh, float Mie, float Ozone, float SunElev)
{
    float H = clamp(Elevation01, 0.0, 1.0);
    float3 Zenith  = float3(0.07, 0.22, 0.52) * clamp(Rayleigh, 0.2, 2.5);
    float3 Horizon = float3(0.46, 0.64, 0.84) + float3(0.24, 0.14, 0.04) * clamp(Mie, 0.0, 2.5);
    float3 Sky = lerp(Horizon, Zenith, pow(H, 0.55));
    float Twilight = saturate(1.0 - abs(SunElev) / 28.0);
    Sky = lerp(Sky, Sky * float3(1.18, 0.84, 0.68), Twilight * (1.0 - H) * 0.55);
    Sky *= float3(1.0 - 0.08 * Ozone, 1.0 - 0.14 * Ozone, 1.0);
    return saturate(Sky);
}

[shader("compute")]
[numthreads(8, 8, 1)]
void ComputeEntry(uint3 Tid : SV_DispatchThreadID)
{
    uint W = max(1u, Params.DimensionsAndMode.x);
    uint H = max(1u, Params.DimensionsAndMode.y);
    if (Tid.x >= W || Tid.y >= H) return;

    uint Mode    = Params.DimensionsAndMode.z;
    uint SubMode = Params.DimensionsAndMode.w;
    float U = (float(Tid.x) + 0.5) / float(W);
    float V = (float(Tid.y) + 0.5) / float(H);
    float3 Col = float3(0.08, 0.08, 0.09);
    float  OutAlpha = 1.0;

    if (Mode == 0u) // Atmosphere Limb / Planetary Horizon preview (curved limb matching CPU fallback, 1:1 circular!)
    {
        float Ray  = clamp(Params.ParamA.x, 0.2, 2.5);
        float Mie  = clamp(Params.ParamA.y, 0.0, 2.5);
        float Oz   = clamp(Params.ParamA.z, 0.0, 2.5);
        float SunE = Params.ParamA.w;
        float Aspect = float(W) / float(H);
        float2 P = float2((U - 0.5) * Aspect, V - 2.18);
        float PlanetR = 1.62;
        float Dist = length(P) - PlanetR;
        if (Dist < 0.0)
        {
            float Depth = saturate(-Dist / 0.36);
            Col = lerp(float3(0.28, 0.31, 0.37), float3(0.16, 0.19, 0.24), Depth);
        }
        else
        {
            float Warm = saturate(1.0 - abs(SunE - 6.0) / 28.0);
            float RimGlow = exp(-Dist * 14.0);
            float UpperRayleigh = exp(-Dist * 4.2) * Ray;
            float3 SpaceBg = float3(0.065, 0.070, 0.085);
            float3 RimCol  = lerp(float3(0.45, 0.72, 1.0), float3(0.95, 0.80, 0.70), Warm) * (0.75 + 0.20 * Mie);
            float3 SkyCol  = float3(0.22, 0.42, 0.76) * (1.0 - 0.12 * Oz);
            Col = SpaceBg + SkyCol * UpperRayleigh * 0.42 + RimCol * RimGlow * 0.85;
            Col = saturate(Col);
        }
    }
    else if (Mode == 1u) // Atmosphere Outer-Space Globe (1:1 circular sphere + full Rayleigh halo with safe margin)
    {
        float2 P = float2((U - 0.5) * (float(W) / float(H)), V - 0.5) * 2.15;
        float R = length(P);
        Col = float3(0.02, 0.025, 0.04);
        float PlanetR = 0.62;
        float Halo = exp(-max(0.0, R - PlanetR) * 11.0) * 0.85;
        Col += float3(0.28, 0.58, 0.98) * Halo * clamp(Params.ParamA.x, 0.2, 2.5);
        if (R < PlanetR)
        {
            float Z = sqrt(max(0.0, PlanetR * PlanetR - R * R)) / PlanetR;
            float3 N = normalize(float3(P.x / PlanetR, -P.y / PlanetR, Z));
            float3 L = normalize(float3(0.68, 0.36, 0.64));
            float NdotL = max(0.0, dot(N, L));
            float Limb = pow(1.0 - Z, 2.8);
            Col = float3(0.06, 0.16, 0.30) * (0.14 + 0.86 * NdotL) + float3(0.32, 0.66, 1.0) * Limb * (0.35 + 0.65 * NdotL);
        }
    }
    else if (Mode == 2u) // Transmittance LUT (256x64)
    {
        float Ray = max(0.1, Params.ParamA.x);
        float Mie = max(0.0, Params.ParamA.y);
        float Oz  = max(0.0, Params.ParamA.z);
        float Mu  = U * 2.0 - 1.0;
        float Alt = 1.0 - V;
        float AirMass = 1.0 / max(0.08, saturate(Mu * 0.5 + 0.5) * 0.85 + Alt * 0.45);
        float3 Tau = (float3(0.14, 0.32, 0.78) * Ray + float3(0.18, 0.18, 0.18) * Mie + float3(0.06, 0.14, 0.02) * Oz) * AirMass * (1.0 - Alt * 0.78);
        Col = exp(-Tau);
    }
    else if (Mode == 3u) // SkyView LUT (256x128)
    {
        float Elev = 1.0 - V;
        Col = EvaluateSkyGradient(Elev, Params.ParamA.x, Params.ParamA.y, Params.ParamA.z, Params.ParamA.w);
        float AzDiff = abs(U - 0.5) * 3.14159;
        float SunE = clamp(Params.ParamA.w / 90.0, 0.02, 0.95);
        float Ang = length(float2(AzDiff * 0.55, (Elev - SunE) * 1.4));
        Col += float3(1.0, 0.84, 0.62) * exp(-Ang * Ang / 0.04) * 0.65;
    }
    else if (Mode == 4u) // Solar Now / Bake LUT
    {
        float TempNorm = clamp((Params.ParamA.x - 2000.0) / 8000.0, 0.0, 1.0);
        float3 SunCol = lerp(float3(1.0, 0.54, 0.22), float3(0.78, 0.88, 1.0), TempNorm);
        float Elev = 1.0 - V;
        Col = lerp(float3(0.08, 0.10, 0.16), float3(0.16, 0.28, 0.48), Elev);
        float D = length(float2((U - 0.5) * (float(W) / float(H)), V - 0.45));
        float DiscR = clamp(Params.ParamA.y * 0.08, 0.05, 0.24);
        Col += SunCol * exp(-D * D / (DiscR * DiscR * 4.5)) * 0.75;
        if (D < DiscR) Col = lerp(SunCol * 0.9, float3(1.0, 0.98, 0.94), sqrt(max(0.0, 1.0 - (D / DiscR) * (D / DiscR))));
    }
    else if (Mode == 5u) // Volumetric Cloud Swath / Deck
    {
        float Cov = clamp(Params.ParamA.x, 0.0, 1.0);
        float Den = clamp(Params.ParamA.y, 0.1, 3.0);
        float N = Fbm2D(float2(U * 5.2 + Params.ParamA.z, V * 3.4));
        float Cloud = smoothstep(1.0 - Cov, 1.0 - Cov * 0.35 + 0.18, N);
        float3 Sky = EvaluateSkyGradient(1.0 - V, 1.0, 0.5, 0.8, 32.0);
        float3 CCol = lerp(float3(0.42, 0.46, 0.54), float3(0.95, 0.96, 0.98), saturate(1.0 - V * 0.65));
        Col = lerp(Sky, CCol, saturate(Cloud * Den * 0.85));
    }
    else if (Mode == 6u) // Lunar Phase Globe (samples real EngineContent/Skies/LunarSurfaceAtlas.ImageSpace textures)
    {
        uint Flags = Params.DimensionsAndMode.w;
        uint Preset = Flags & 7u;
        bool ApplyPhase = (Flags & 8u) != 0u;
        bool NightBg    = (Flags & 16u) != 0u;

        float MinDim = float(min(W, H));
        float DiscR  = clamp(Params.ParamA.w, 0.15, 0.96) * (MinDim * 0.5);
        float2 Delta = float2(float(Tid.x) - float(W) * 0.5, float(Tid.y) - float(H) * 0.5);
        float D      = length(Delta);

        if (NightBg)
        {
            Col = float3(11.0 + V * 14.0, 16.0 + V * 19.0, 30.0 + V * 28.0) / 255.0;
            float Halo = exp(-D / max(1.0, DiscR * 1.45)) * (46.0 / 255.0) * clamp(Params.ParamA.z, 0.0, 2.5);
            Col += float3(0.90, 0.94, 1.05) * Halo;
            if (D > DiscR + 2.0)
            {
                uint Hs = (Tid.x * 73856093u) ^ (Tid.y * 19349663u);
                if ((Hs & 2047u) < 3u)
                {
                    float Bri = (110.0 + float(Hs & 95u)) / 255.0;
                    Col = max(Col, float3(Bri, Bri, Bri + 0.06));
                }
            }
            OutAlpha = 1.0;
        }
        else
        {
            Col = float3(0.125, 0.125, 0.125);
            OutAlpha = 0.0;
        }

        if (D <= DiscR + 1.0)
        {
            float EdgeCov = clamp(DiscR + 0.5 - D, 0.0, 1.0);
            float Nx = clamp(Delta.x / max(1.0, DiscR), -1.0, 1.0);
            float Ny = clamp(Delta.y / max(1.0, DiscR), -1.0, 1.0);
            float Nz = sqrt(max(0.0, 1.0 - Nx * Nx - Ny * Ny));

            float LonU = atan2(Nx, Nz) * (0.5 / 3.14159265) + 0.5;
            float LatV = acos(clamp(-Ny, -1.0, 1.0)) / 3.14159265;
            float3 TexRgb = SampleLunarAtlasGpu(Preset, LonU, LatV);

            float Light = 0.96 * (0.58 + 0.42 * pow(Nz, 0.30));
            if (ApplyPhase)
            {
                float PhaseRad = (Params.ParamA.x - 0.5) * 6.2831853;
                float3 L = normalize(float3(sin(PhaseRad), -0.14, cos(PhaseRad)));
                float NdotL = Nx * L.x + Ny * L.y + Nz * L.z;
                float Lit = clamp((NdotL + 0.10) / 0.28, 0.0, 1.0);
                Light = (0.09 + 0.96 * Lit) * (0.55 + 0.45 * pow(Nz, 0.32));
            }

            float3 GlobeRgb = TexRgb * Light;
            if (NightBg)
            {
                Col = lerp(Col, GlobeRgb, EdgeCov);
                OutAlpha = 1.0;
            }
            else
            {
                Col = GlobeRgb;
                OutAlpha = EdgeCov;
            }
        }
    }
    else if (Mode == 7u) // Starfield Chart / Star Sheet Bake
    {
        Col = float3(0.025, 0.032, 0.058) + float3(0.04, 0.05, 0.09) * exp(-pow((U - 0.5) * 2.6 + (V - 0.5) * 1.4, 2.0) * 3.5);
        float2 Cell = floor(float2(U * 48.0, V * 28.0));
        float2 Local = frac(float2(U * 48.0, V * 28.0)) - 0.5;
        float Seed = Hash21(Cell);
        if (Seed > 0.84)
        {
            float Bright = pow((Seed - 0.84) / 0.16, 1.8) * clamp(Params.ParamA.x, 0.3, 3.0);
            float D = length(Local);
            float3 StarCol = lerp(float3(1.0, 0.82, 0.64), float3(0.72, 0.86, 1.0), Hash21(Cell + 19.3));
            Col += StarCol * exp(-D * D * 42.0) * Bright;
        }
    }
    else if (Mode == 8u) // Fog Layer / Ridge / Glow
    {
        float Dens = clamp(Params.ParamA.x, 0.0, 1.0);
        float Falloff = clamp(Params.ParamA.y, 0.1, 4.0);
        float Alt = 1.0 - V;
        float Fog = saturate(exp(-Alt * Falloff * 2.2) * (0.25 + 0.75 * Dens));
        float3 Bg = lerp(float3(0.10, 0.14, 0.20), float3(0.05, 0.07, 0.12), Alt);
        Col = lerp(Bg, Params.ColourA.rgb, Fog);
    }
    else if (Mode == 9u) // Rainbow Optical Arc
    {
        float2 P = float2((U - 0.5) * 1.65, 0.94 - V);
        float R = length(P);
        Col = EvaluateSkyGradient(1.0 - V, 0.9, 0.4, 0.7, 25.0) * 0.55;
        float Band = (R - 0.54) / 0.12;
        if (Band >= 0.0 && Band <= 1.0)
        {
            float3 Spec = float3(
                smoothstep(0.50, 0.90, Band),
                sin(Band * 3.14159),
                1.0 - smoothstep(0.10, 0.55, Band));
            float Env = sin(Band * 3.14159) * clamp(Params.ParamA.x, 0.1, 2.0);
            Col += Spec * Env * 0.68;
        }
    }
    else if (Mode == 10u) // Environment Light IBL Dome
    {
        float Elev = 1.0 - V;
        Col = EvaluateSkyGradient(Elev, 1.0, 0.45, 0.8, 34.0) * clamp(Params.ParamA.x, 0.2, 2.5);
        if (V > 0.64)
        {
            Col = lerp(Col * 0.5, float3(0.08, 0.08, 0.09), saturate((V - 0.64) / 0.36));
        }
    }
    else if (Mode == 11u) // Geometry SDF Slice (cyan interior + glowing amber iso-surface)
    {
        float2 P = (float2(U, V) - 0.5) * float2(float(W) / float(H), 1.0);
        float HalfS = 0.36;
        float SD = max(abs(P.x), abs(P.y)) - HalfS;
        Col = float3(26.0, 32.0, 42.0) / 255.0;
        if (SD < 0.0)
        {
            float Depth = saturate(-SD / (HalfS * 0.55));
            Col = (float3(62.0, 182.0, 224.0) + Depth * float3(42.0, 34.0, 24.0)) / 255.0;
        }
        float Iso = exp(-pow(SD / 0.011, 2.0));
        Col = lerp(Col, float3(244.0, 174.0, 76.0) / 255.0, Iso);
    }
    else if (Mode == 12u) // Geometry UV Atlas (6-island UDIM 1001 + thumbnails)
    {
        static const float3 IslandCols[6] = {
            float3(0.34, 0.56, 0.58), float3(0.58, 0.32, 0.48),
            float3(0.44, 0.32, 0.58), float3(0.54, 0.50, 0.28),
            float3(0.56, 0.30, 0.34), float3(0.30, 0.54, 0.40)
        };
        Col = float3(16.0, 16.0, 18.0) / 255.0;
        if (U < 0.72)
        {
            float LU = U / 0.72;
            uint DarkChk = ((uint(LU * 8.0) + uint(V * 8.0)) & 1u);
            Col = (DarkChk == 0u) ? (float3(24.0, 25.0, 28.0) / 255.0) : (float3(38.0, 39.0, 43.0) / 255.0);
            if (LU < 0.666)
            {
                uint C = (LU < 0.333) ? 0u : 1u;
                uint R = min(2u, uint(V * 3.0));
                Col = lerp(Col, IslandCols[R * 2u + C], 0.58);
            }
        }
        else if (U > 0.75)
        {
            float Fv = frac(V * 3.0);
            if (Fv > 0.06 && Fv < 0.94)
            {
                uint DarkChk = (((Tid.x / 10u) + (Tid.y / 10u)) & 1u);
                Col = (DarkChk == 0u) ? (float3(22.0, 22.0, 24.0) / 255.0) : (float3(44.0, 44.0, 46.0) / 255.0);
            }
        }
    }
    else if (Mode == 13u) // Lens Flare Optical Composite & 4x2 Atlas
    {
        float Gain = clamp(Params.ParamA.x, 0.0, 3.0);
        if (SubMode == 0u)
        {
            float HaloR = clamp(Params.ParamA.y * 0.92, 0.12, 0.42);
            float Spread = max(0.05, Params.ParamA.z);
            uint Flags = uint(Params.ParamA.w + 0.5);
            float2 Core = float2(0.27, 0.43);
            float2 D = float2(U - Core.x, (V - Core.y) * (float(H) / float(W)));
            float Dist = length(D);
            Col = float3(0.02, 0.02, 0.025);
            Col += float3(1.0, 0.92, 0.78) * exp(-Dist * Dist * 48.0) * (1.25 * Gain);
            if ((Flags & 1u) != 0u) // Anamorphic horizontal streak
            {
                float Streak = exp(-abs(D.y) * 52.0) * exp(-abs(D.x) * (3.6 / Spread));
                Col += float3(0.45, 0.72, 1.0) * Streak * 0.85 * Gain;
            }
            if ((Flags & 2u) != 0u) // Diffraction star spikes
            {
                float Ang = atan2(D.y, D.x);
                float Spikes = pow(abs(cos(Ang * 3.0)), 18.0) * exp(-Dist * 7.5);
                Col += float3(1.0, 0.86, 0.68) * Spikes * 0.70 * Gain;
            }
            if ((Flags & 4u) != 0u) // Chromatic halo ring + 5 ghosts
            {
                float Ring = exp(-pow((Dist - HaloR) / 0.022, 2.0));
                Col += float3(0.55, 0.78, 1.0) * Ring * 0.42 * Gain;
                for (int G = 0; G < 5; ++G)
                {
                    float T = 0.34 + float(G) * 0.16;
                    float2 Gp = lerp(Core, float2(1.0 - Core.x, 1.0 - Core.y), T);
                    float Gd = length(float2(U - Gp.x, (V - Gp.y) * (float(H) / float(W))));
                    float Gr = 0.055 - float(G) * 0.006;
                    float Disc = smoothstep(Gr, Gr * 0.3, Gd) * 0.35 * Gain;
                    float3 Gc = float3(0.42 + 0.10 * float(G), 0.56 - 0.04 * float(G), 0.38 + 0.08 * float(G));
                    Col += Gc * Disc;
                }
            }
        }
        else // 4x2 Optical Flare Kernel Atlas
        {
            uint CellX = min(3u, uint(U * 4.0));
            uint CellY = min(1u, uint(V * 2.0));
            float2 Lu = float2(frac(U * 4.0) * 2.0 - 1.0, frac(V * 2.0) * 2.0 - 1.0);
            float R = length(Lu);
            Col = float3(0.035, 0.035, 0.04);
            if (CellY == 0u)
            {
                float Sp = 4.0 + float(CellX) * 2.0;
                float Ang = atan2(Lu.y, Lu.x);
                float Star = exp(-R * 4.2) + pow(abs(cos(Ang * (2.0 + float(CellX)))), 14.0) * exp(-R * Sp * 0.65);
                Col += float3(1.0, 0.92, 0.80) * Star * 0.72 * Gain;
            }
            else
            {
                static const float3 GhostCols[4] = {
                    float3(0.92, 0.58, 0.30), float3(0.55, 0.78, 0.42),
                    float3(0.58, 0.64, 0.72), float3(0.84, 0.44, 0.56)
                };
                float Ring = exp(-pow((R - 0.58) / 0.14, 2.0)) * 0.65 + smoothstep(0.68, 0.10, R) * 0.32;
                Col += GhostCols[CellX] * Ring * Gain;
            }
        }
    }
    else if (Mode == 14u) // Camera Body Studio Backdrop
    {
        float D = length(float2((U - 0.50) * 1.4, (V - 0.42) * 1.6));
        float Glow = exp(-D * D * 2.2) * (24.0 / 255.0);
        Col = float3(18.0 / 255.0 + Glow, 19.0 / 255.0 + Glow, 22.0 / 255.0 + Glow * 1.15);
    }
    else if (Mode == 15u) // CameraSettings Renderer Hero Sphere
    {
        float2 P = float2((U - 0.5) * (float(W) / float(H)), V - 0.45);
        float Rad = 0.296;
        Col = lerp(float3(0.11, 0.12, 0.14), float3(0.05, 0.055, 0.065), V);
        float D = length(P);
        if (D <= Rad)
        {
            float2 Nxy = P / Rad;
            float Nz = sqrt(max(0.0, 1.0 - dot(Nxy, Nxy)));
            float3 N = normalize(float3(Nxy.x, -Nxy.y, Nz));
            float3 L = normalize(float3(-0.45, 0.58, 0.68));
            float3 Hvec = normalize(L + float3(0.0, 0.0, 1.0));
            float NdotL = max(0.0, dot(N, L));
            float Spec = pow(max(0.0, dot(N, Hvec)), 42.0);
            float Rim = pow(1.0 - Nz, 2.6);
            uint Eng = uint(Params.ParamA.x + 0.5);
            float3 BaseCol = (Eng == 0u) ? float3(0.76, 0.50, 0.36) :
                             (Eng == 4u || Eng == 5u) ? float3(0.64, 0.72, 0.86) :
                             float3(0.70, 0.74, 0.79);
            Col = BaseCol * (0.14 + 0.86 * NdotL) + float3(1.0, 0.96, 0.90) * Spec * 0.65 + float3(0.45, 0.62, 0.88) * Rim * 0.35;
        }
        else if (V > 0.76)
        {
            float Shadow = exp(-pow(P.x / 0.28, 2.0)) * exp(-pow((V - 0.81) / 0.06, 2.0));
            Col *= (1.0 - 0.55 * Shadow);
        }
    }
    else if (Mode == 16u) // Photometric Lights (0=Point, 1=Spot, 2=IES, 3=Area, 4=Tube, 5=LED, 6=LEDStrip)
    {
        float3 Tint = max(Params.ColourA.rgb, float3(0.15, 0.15, 0.15));
        float IntGain = clamp(Params.ParamA.x, 0.25, 3.0);
        Col = float3(0.075, 0.078, 0.086);
        float2 Center = float2(0.5, 0.5);
        float Aspect = float(W) / float(H);
        float2 P = float2((U - Center.x) * Aspect, V - Center.y);
        if (SubMode == 0u) // Point Light radial falloff
        {
            float R = length(P);
            float Falloff = 1.0 / (1.0 + pow(R * 5.2, clamp(Params.ParamA.z, 1.0, 3.0)));
            Col += Tint * Falloff * (0.95 * IntGain) + float3(1.0, 0.98, 0.94) * exp(-R * R * 120.0);
        }
        else if (SubMode == 1u) // Spot Light cone envelope
        {
            float2 Sp = float2((U - 0.5) * Aspect, V - 0.16);
            float Dist = max(0.01, length(Sp));
            float Ang = atan2(abs(Sp.x), max(0.001, Sp.y));
            float InnerR = radians(clamp(Params.ParamA.y, 4.0, 65.0)) * 0.5;
            float OuterR = radians(clamp(Params.ParamA.z, 8.0, 82.0)) * 0.5;
            float Cone = 1.0 - smoothstep(min(InnerR, OuterR), max(InnerR + 0.02, OuterR), Ang);
            float Atten = saturate(1.0 - Dist * 1.15);
            Col += Tint * Cone * Atten * 0.88 * IntGain + float3(1.0, 0.97, 0.90) * exp(-Dist * Dist * 160.0);
        }
        else if (SubMode == 2u) // IES polar candela web
        {
            float2 Sp = float2((U - 0.5) * Aspect, V - 0.18);
            float R = length(Sp);
            float Ang = atan2(abs(Sp.x), max(0.001, Sp.y));
            float Lobe = pow(max(0.0, cos(Ang * 1.45)), 2.2) * (0.72 + 0.28 * cos(Ang * 6.0));
            float Fill = smoothstep(Lobe * 0.68, Lobe * 0.62, R);
            float Rings = exp(-pow(abs(frac(R * 5.0) - 0.5) * 2.0, 14.0)) * 0.08;
            Col += float3(0.24, 0.28, 0.34) * Rings + Tint * Fill * 0.72 * IntGain;
        }
        else if (SubMode == 3u) // Area Light aperture emitter
        {
            float2 HalfE = float2(clamp(Params.ParamA.y * 0.18, 0.10, 0.45), clamp(Params.ParamA.z * 0.18, 0.08, 0.32));
            float2 Q = max(abs(P) - HalfE, float2(0.0, 0.0));
            float D = length(Q);
            float Core = smoothstep(0.015, 0.0, D);
            float Glow = exp(-D * 6.5) * 0.62 * IntGain;
            Col += Tint * Glow + lerp(Tint, float3(1.0, 1.0, 1.0), 0.55) * Core;
        }
        else if (SubMode == 4u) // Tube Light capsule emitter
        {
            float HalfLen = clamp(Params.ParamA.y * 0.26, 0.16, 0.55);
            float2 Q = float2(max(0.0, abs(P.x) - HalfLen), P.y);
            float D = length(Q);
            Col += Tint * exp(-D * 7.5) * 0.72 * IntGain + lerp(Tint, float3(1.0, 1.0, 1.0), 0.6) * smoothstep(0.055, 0.032, D);
        }
        else if (SubMode == 5u) // LED package domed die
        {
            float R = length(P);
            float Die = smoothstep(0.11, 0.08, R);
            float Lens = exp(-R * 5.8) * 0.75 * IntGain;
            float Bezel = smoothstep(0.22, 0.19, max(abs(P.x), abs(P.y))) * (1.0 - smoothstep(0.16, 0.13, R));
            Col += float3(0.22, 0.24, 0.28) * Bezel + Tint * Lens + lerp(Tint, float3(1.0, 1.0, 1.0), 0.65) * Die;
        }
        else // LED Strip multi-emitter ribbon
        {
            float Ribbon = smoothstep(0.11, 0.09, abs(P.y));
            Col = lerp(Col, float3(0.15, 0.16, 0.18), Ribbon);
            float Count = clamp(Params.ParamA.y, 4.0, 12.0);
            float Fx = (frac(U * Count) - 0.5) / Count * Aspect;
            float D = length(float2(Fx, P.y));
            Col += Tint * exp(-D * 18.0) * 0.90 * IntGain + float3(1.0, 0.98, 0.95) * smoothstep(0.038, 0.015, D);
        }
    }
    else if (Mode == 17u) // Geometry 96x96 Specimen Preview (0=Analytic, 1=Native, 2=Imported, 3=Procedural)
    {
        float2 P = float2((U - 0.5) * 2.0, (V - 0.5) * 2.0);
        float R = length(P);
        Col = lerp(float3(0.13, 0.14, 0.16), float3(0.08, 0.085, 0.095), V);
        float Rad = 0.72;
        if (SubMode == 3u)
        {
            Rad += 0.06 * sin(atan2(P.y, P.x) * 6.0) * cos(R * 8.0);
        }
        if (R <= Rad)
        {
            float2 Nxy = P / max(0.001, Rad);
            float Nz = sqrt(max(0.0, 1.0 - dot(Nxy, Nxy)));
            float3 N = normalize(float3(Nxy.x, -Nxy.y, Nz));
            float3 L = normalize(float3(-0.48, 0.62, 0.62));
            float3 Hvec = normalize(L + float3(0.0, 0.0, 1.0));
            float Diff = max(0.0, dot(N, L));
            float Spec = pow(max(0.0, dot(N, Hvec)), 36.0);
            float Rim = pow(1.0 - Nz, 2.4);
            float3 Base = max(Params.ColourA.rgb, float3(0.45, 0.52, 0.60));
            if (SubMode == 2u) // Imported wireframe tint overlay
            {
                float Wire = max(exp(-pow(abs(frac(U * 8.0) - 0.5) * 2.0, 10.0)),
                                 exp(-pow(abs(frac(V * 8.0) - 0.5) * 2.0, 10.0)));
                Base = lerp(Base, float3(0.42, 0.78, 0.96), Wire * 0.25);
            }
            Col = Base * (0.18 + 0.82 * Diff) + float3(1.0, 0.97, 0.92) * Spec * 0.55 + float3(0.42, 0.62, 0.92) * Rim * 0.35;
        }
    }

    OutPixelsRgba8[Tid.y * W + Tid.x] = PackRgba8(saturate(Col), OutAlpha);
}
