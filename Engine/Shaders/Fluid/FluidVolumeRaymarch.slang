/*============================================================================================================================================
                                                        FLUIDVOLUMERAYMARCH.SLANG
============================================================================================================================================*/
// 🧩 Stage D (Slice S17) 1:1 Volumetric raymarcher: Wrenninge 3-octave scattering, 2-lobe HG, 1.35-growth sun shadows,
//    powder darkening, fire-lit smoke/floor, ACES filmic curve, analytic colliders & transparent flipbook bake
// Reference: Experimental/FluidEditor/Engine/FluidShaders.js SceneFragment (L1129-L1384)

import FluidVolumeAccess;

//------------------------------------------------------------------------------------------------------------------------
//                                                    UNIFORM BLOCK & BINDINGS
//------------------------------------------------------------------------------------------------------------------------

/// Matches C++ FluidVolumeRaymarchUniformBlock byte-for-byte (400 bytes, 16-byte aligned).
public struct FluidVolumeRaymarchUniformBlock
{
    public FluidVolumeUniformBlock Volume;                  // 160 B
    public float4x4                InverseViewProjection;   //  64 B
    public float4                  CameraPositionFireLight; // xyz = cameraPosition (m), w = fireLight
    public float4                  ViewportGroundExposure;  // xy = viewport (px), z = groundHeight (m), w = exposure
    public float4                  LightDirectionIntensity; // xyz = normalized sun dir, w = lightIntensity
    public float4                  LightColourAnisotropy;   // xyz = sun RGB, w = anisotropy (<= 0.95)
    public float4                  AlbedoDensityScale;      // xyz = smoke albedo, w = densityScale
    public float4                  AmbientColourAbsorption; // xyz = ambient RGB, w = absorption
    public float4                  FireEmberIntensity;      // xyz = ember RGB, w = fireIntensity
    public float4                  FireFlameThreshold;      // xyz = flame RGB, w = fireThreshold
    public float4                  FireCoreSoftness;        // xyz = core RGB, w = fireSoftness
    public float4                  FireOpacitySteps;        // x = fireOpacity, y = steps, z = shadowSteps, w = showVolume
    public int4                    FlagsSelection;          // x = transparent, y = voxelPreview, z = tickCounter, w = selectedCollider
};

[[vk::binding(0, 0)]] ConstantBuffer<FluidVolumeRaymarchUniformBlock> uParams;
[[vk::binding(1, 0)]] Texture3D<float4>                               gState;
[[vk::binding(2, 0)]] Texture3D<float4>                               gPressure;
[[vk::binding(3, 0)]] SamplerState                                    gLinearClamp;

struct FullscreenVertexOutput
{
    float4 Position : SV_Position;
};

//------------------------------------------------------------------------------------------------------------------------
//                                                     FULLSCREEN VERTEX
//------------------------------------------------------------------------------------------------------------------------

[shader("vertex")]
FullscreenVertexOutput FullscreenVertexEntry(uint vertexId : SV_VertexID)
{
    FullscreenVertexOutput outV;
    float2 pos = float2((vertexId == 1u) ? 3.0 : -1.0, (vertexId == 2u) ? 3.0 : -1.0);
    outV.Position = float4(pos, 0.0, 1.0);
    return outV;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    RAYMARCHING HELPERS
//------------------------------------------------------------------------------------------------------------------------

float2 IntersectBox(float3 origin, float3 direction, float3 boxMin, float3 boxMax)
{
    float3 inv  = 1.0 / direction;
    float3 t0   = (boxMin - origin) * inv;
    float3 t1   = (boxMax - origin) * inv;
    float3 tmin = min(t0, t1);
    float3 tmax = max(t0, t1);
    return float2(max(max(tmin.x, tmin.y), tmin.z), min(min(tmax.x, tmax.y), tmax.z));
}

float IntersectSphere(float3 origin, float3 direction, float3 centre, float radius)
{
    float3 oc = origin - centre;
    float b = dot(oc, direction);
    float c = dot(oc, oc) - radius * radius;
    float h = b * b - c;
    if (h < 0.0) return -1.0;
    return -b - sqrt(h);
}

float3 FireRadiance(float temperature)
{
    float threshold = uParams.FireFlameThreshold.w;
    float softness  = max(uParams.FireCoreSoftness.w, 0.05);
    float t = clamp((temperature - threshold) / softness, 0.0, 1.0);
    float3 colour = (t < 0.5)
        ? lerp(uParams.FireEmberIntensity.xyz, uParams.FireFlameThreshold.xyz, t * 2.0)
        : lerp(uParams.FireFlameThreshold.xyz, uParams.FireCoreSoftness.xyz, (t - 0.5) * 2.0);
    return colour * (0.2 + 4.0 * t * t);
}

float FirePresence(float temperature)
{
    float threshold = uParams.FireFlameThreshold.w;
    return smoothstep(threshold * 0.7, threshold * 1.15, temperature);
}

float HenyeyGreenstein(float cosTheta, float g)
{
    float g2 = g * g;
    return (1.0 - g2) / (4.0 * 3.14159265 * max(1.0e-4, pow(1.0 + g2 - 2.0 * g * cosTheta, 1.5)));
}

float SmokePhase(float cosTheta, float g)
{
    return lerp(HenyeyGreenstein(cosTheta, -0.25), HenyeyGreenstein(cosTheta, g), 0.72) * 4.0 * 3.14159265;
}

float StableJitter(float2 pixel)
{
    return frac(52.9829189 * frac(dot(pixel, float2(0.06711056, 0.00583715))));
}

float3 FireGlowColour()
{
    return lerp(uParams.FireEmberIntensity.xyz, uParams.FireFlameThreshold.xyz, 0.6);
}

float3 AcesFilm(float3 x)
{
    return clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0);
}

float GroundGrid(float3 world)
{
    float2 coordinate = world.xz;
    float2 minor = abs(frac(coordinate - 0.5) - 0.5) / max(fwidth(coordinate), float2(1.0e-5, 1.0e-5));
    float2 major = abs(frac(coordinate * 0.2 - 0.5) - 0.5) / max(fwidth(coordinate * 0.2), float2(1.0e-5, 1.0e-5));
    float minorLine = 1.0 - min(min(minor.x, minor.y), 1.0);
    float majorLine = 1.0 - min(min(major.x, major.y), 1.0);
    return max(minorLine * 0.35, majorLine * 0.7);
}

float SunOpticalDepth(float3 world, float3 boxMin, float3 boxMax)
{
    int shadowSteps = int(uParams.FireOpacitySteps.z);
    if (shadowSteps <= 0) return 0.0;

    float3 lightDir = uParams.LightDirectionIntensity.xyz;
    float2 span = IntersectBox(world, lightDir, boxMin, boxMax);
    float start  = max(span.x, 0.0);
    float length = span.y - start;
    if (length <= 0.0) return 0.0;

    const float kGrowth = 1.35;
    float stepLen = length * (kGrowth - 1.0) / (pow(kGrowth, float(shadowSteps)) - 1.0);
    float t = start;
    float optical = 0.0;
    for (int i = 0; i < 16; ++i)
    {
        if (i >= shadowSteps) break;
        float3 p = world + lightDir * (t + 0.5 * stepLen);
        optical += SampleVolumeDense(gState, gLinearClamp, uParams.Volume, CellFromWorld(uParams.Volume, p)).x * stepLen;
        t += stepLen;
        stepLen *= kGrowth;
    }
    return optical * uParams.AlbedoDensityScale.w * uParams.AmbientColourAbsorption.w;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    FRAGMENT ENTRY
//------------------------------------------------------------------------------------------------------------------------

[shader("fragment")]
float4 FragmentEntry(FullscreenVertexOutput input) : SV_Target0
{
    float2 viewport = max(uParams.ViewportGroundExposure.xy, float2(1.0, 1.0));
    float2 ndc = (input.Position.xy / viewport) * 2.0 - 1.0;
    float4 farPt = mul(uParams.InverseViewProjection, float4(ndc, 1.0, 1.0));
    float3 origin = uParams.CameraPositionFireLight.xyz;
    float3 direction = normalize(farPt.xyz / farPt.w - origin);

    int transparent = uParams.FlagsSelection.x;
    int voxelPreview = uParams.FlagsSelection.y;
    int selectedCollider = uParams.FlagsSelection.w;
    int showVolume = int(uParams.FireOpacitySteps.w);

    float horizon = clamp(direction.y * 0.5 + 0.5, 0.0, 1.0);
    float3 colour = (transparent == 1)
        ? float3(0.0, 0.0, 0.0)
        : lerp(float3(0.055, 0.058, 0.068), float3(0.028, 0.029, 0.034), pow(horizon, 0.6));

    float3 gridCentre = uParams.Volume.GridCentreCellSize.xyz;
    float  cellSize   = uParams.Volume.GridCentreCellSize.w;
    float  fireLight  = uParams.CameraPositionFireLight.w;
    float  fireIntensity = uParams.FireEmberIntensity.w;
    float  groundHeight  = uParams.ViewportGroundExposure.z;

    float sceneDepth = 1.0e9;
    if (direction.y < -1.0e-4 && transparent == 0)
    {
        float t = (groundHeight - origin.y) / direction.y;
        float3 hit = origin + direction * t;
        float fade = exp(-length(hit.xz) * 0.09);
        float3 ground = float3(0.075, 0.078, 0.088);
        float grid = GroundGrid(hit) * fade;
        colour = lerp(ground * fade + colour * (1.0 - fade), float3(0.32, 0.34, 0.40), grid);
        if (showVolume == 1)
        {
            float3 floorHalf = GridHalf(uParams.Volume);
            float shade = exp(-SunOpticalDepth(hit, gridCentre - floorHalf, gridCentre + floorHalf));
            colour *= lerp(0.4, 1.0, shade);
            float3 nearCell = CellFromWorld(uParams.Volume, hit + float3(0.0, cellSize * 1.5, 0.0));
            float3 gridF = float3(uParams.Volume.GridBoundary.xyz);
            if (all(nearCell >= float3(0.0, 0.0, 0.0)) && all(nearCell <= gridF))
            {
                colour += FireGlowColour() * SampleVolumeDense(gPressure, gLinearClamp, uParams.Volume, nearCell).y
                        * fireLight * fireIntensity * 0.08 * fade;
            }
        }
        sceneDepth = t;
    }

    int colliderCount = min(uParams.Volume.LookupOriginCount.w, kMaxFluidColliders);
    float3 lightDir = uParams.LightDirectionIntensity.xyz;
    for (int i = 0; i < kMaxFluidColliders; ++i)
    {
        if (i >= colliderCount || transparent == 1) break;
        float t = IntersectSphere(origin, direction, uParams.Volume.Colliders[i].xyz, uParams.Volume.Colliders[i].w);
        if (t > 0.0 && t < sceneDepth)
        {
            float3 hit = origin + direction * t;
            float3 n = normalize(hit - uParams.Volume.Colliders[i].xyz);
            float diffuse = max(dot(n, lightDir), 0.0);
            float rim = pow(1.0 - max(dot(n, -direction), 0.0), 3.0);
            float3 baseCol = (i == selectedCollider) ? float3(0.86, 0.72, 0.36) : float3(0.42, 0.44, 0.50);
            colour = baseCol * (0.18 + 0.82 * diffuse) + rim * 0.25;
            sceneDepth = t;
        }
    }

    float coverage = 0.0;
    if (showVolume == 1)
    {
        float3 halfExt = GridHalf(uParams.Volume);
        float2 span = IntersectBox(origin, direction, gridCentre - halfExt, gridCentre + halfExt);
        float entry = max(span.x, 0.0);
        float exitDist = min(span.y, sceneDepth);
        if (exitDist > entry)
        {
            int steps = max(1, int(uParams.FireOpacitySteps.y));
            float stepLen = (exitDist - entry) / float(steps);
            float jitter = StableJitter(input.Position.xy);
            float transmittance = 1.0;
            float3 accumulated = float3(0.0, 0.0, 0.0);

            float cosTheta = dot(direction, lightDir);
            float g = uParams.LightColourAnisotropy.w;
            float3 octavePhase = float3(
                SmokePhase(cosTheta, g),
                SmokePhase(cosTheta, g * 0.5),
                SmokePhase(cosTheta, g * 0.25));
            float3 sunLight     = uParams.LightColourAnisotropy.xyz * uParams.LightDirectionIntensity.w;
            float3 albedo       = uParams.AlbedoDensityScale.xyz;
            float  densityScale = uParams.AlbedoDensityScale.w;
            float3 ambientCol   = uParams.AmbientColourAbsorption.xyz;
            float  absorption   = uParams.AmbientColourAbsorption.w;
            float  fireOpacity  = uParams.FireOpacitySteps.x;

            for (int i = 0; i < 256; ++i)
            {
                if (i >= steps || transmittance < 0.005) break;
                float3 p = origin + direction * (entry + (float(i) + jitter) * stepLen);
                float3 coordinate = CellFromWorld(uParams.Volume, p);
                float4 state = (voxelPreview == 1)
                    ? FetchCellDense(gState, uParams.Volume, int3(floor(coordinate)))
                    : SampleVolumeDense(gState, gLinearClamp, uParams.Volume, coordinate);

                float density = state.x * densityScale;
                float temperature = state.y;
                float fire = FirePresence(temperature);
                if (density < 0.002 && fire < 0.002) continue;

                float3 emission = FireRadiance(temperature) * fireIntensity * fire;
                float sigmaSmoke = density * absorption;
                float sigmaFire  = fire * fireOpacity;
                float sigma      = sigmaSmoke + sigmaFire;

                float3 scattering = float3(0.0, 0.0, 0.0);
                if (sigmaSmoke > 0.0)
                {
                    float tau = SunOpticalDepth(p, gridCentre - halfExt, gridCentre + halfExt);
                    float sun = (octavePhase.x * exp(-tau)
                               + 0.5 * octavePhase.y * exp(-tau * 0.5)
                               + 0.25 * octavePhase.z * exp(-tau * 0.25)) / 1.75;
                    float powder = 1.0 - 0.45 * exp(-sigmaSmoke * 2.2);
                    float above = SampleVolumeDense(gState, gLinearClamp, uParams.Volume, coordinate + float3(0.0, 2.0, 0.0)).x * 2.0
                                + SampleVolumeDense(gState, gLinearClamp, uParams.Volume, coordinate + float3(0.0, 6.0, 0.0)).x * 4.0;
                    float sky = exp(-0.6 * above * cellSize * densityScale * absorption);
                    float3 glowLight = FireGlowColour()
                                     * SampleVolumeDense(gPressure, gLinearClamp, uParams.Volume, coordinate).y
                                     * fireLight * fireIntensity * 0.35;
                    scattering = albedo * (ambientCol * sky + sunLight * sun * powder + glowLight);
                }

                float3 source = scattering * sigmaSmoke + emission * (1.0 + sigmaFire);
                float stepTrans = exp(-sigma * stepLen);
                float3 contrib = (sigma > 1.0e-4) ? (source * (1.0 - stepTrans) / sigma) : (source * stepLen);
                accumulated += transmittance * contrib;
                transmittance *= stepTrans;
            }
            colour = colour * transmittance + accumulated;
            coverage = 1.0 - transmittance;
        }
    }

    colour = AcesFilm(colour * uParams.ViewportGroundExposure.w);
    colour = pow(colour, float3(1.0 / 2.2, 1.0 / 2.2, 1.0 / 2.2));
    float alpha = (transparent == 1) ? max(coverage, max(colour.r, max(colour.g, colour.b))) : 1.0;
    return float4(colour, alpha);
}
