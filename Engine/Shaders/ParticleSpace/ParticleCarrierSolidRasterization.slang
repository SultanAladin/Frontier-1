/*============================================================================================================================================
                                                PARTICLECARRIERSOLIDRASTERIZATION.SLANG
============================================================================================================================================*/
// 🧩 Pure .slang instanced 3D carrier solid PBR rasterization, 3 backing surfaces (ObsidianGlossy, BrushedChrome, GhostWireframe) & Derez seam glow
// Reference: Experimental/FluidEditor/Engine/ParticleShaders.js L1341-L1551 (ObjectRenderVertex & ObjectFragment)

import ForceFieldNoise;
import ParticleShapeAnchor;
import ParticleShadingCommon;

//------------------------------------------------------------------------------------------------------------------------
//                                                    DATA & UNIFORM BLOCKS
//------------------------------------------------------------------------------------------------------------------------

public struct ParticleStateRecord
{
    public float4 PositionAge;  // xyz = world position (m), w = age (s)
    public float4 VelocitySeed; // xyz = velocity (m/s), w = seed [0..1]
};

/// Matches C++ ParticleCarrierSolidUniformBlock byte-for-byte (544 bytes, 16-byte aligned).
public struct ParticleCarrierSolidUniformBlock
{
    public ParticleShapeUniformBlock Shape;
    public ParticleShadeUniformBlock Shade;
    public float4x4 ViewProjection;          // Row-major world -> clip
    public float4   CameraPositionFocal;     // xyz = cameraPosition, w = focalPixels
    public float4   CameraForwardPixelRatio; // xyz = cameraForward, w = pixelRatio
    public float4   SolidCountsStride;       // x = solidVertexCount, y = solidTriangleCount, z = carrierStride, w = wireMode (0/1)
    public float4   BackingParams;           // x = isBacking (0/1), y = backingScale, z = solidYawRad, w = backingMode (0..3)
    public float4   SizeParams;              // x = sizeStart, y = sizeEnd, z = sizeRandom, w = sizeMode (0=World, 1=Pixels)
    public float4   SpinTiltRoughMetal;      // x = solidSpin, y = solidTilt, z = solidRoughness, w = solidMetallic
    public float4   BackingColourGlow;       // xyz = backingColour RGB, w = solidGlow
};

public struct CarrierSolidVertexOutput
{
    float4 ClipPosition : SV_Position;
    float3 WorldNormal  : TEXCOORD0;
    float3 Colour       : COLOR0;
    float3 WorldPos     : TEXCOORD1;
    float3 LocalPos     : TEXCOORD2;
    float  Alpha        : TEXCOORD3;
    float  Ridge        : TEXCOORD4;
};

[[vk::binding(0, 0)]] ConstantBuffer<ParticleCarrierSolidUniformBlock> uParams;
[[vk::binding(1, 0)]] StructuredBuffer<ParticleStateRecord>            gStates;
[[vk::binding(2, 0)]] StructuredBuffer<float4>                         gAnchorTexels;
[[vk::binding(3, 0)]] StructuredBuffer<float4>                         gSolidTexels;

//------------------------------------------------------------------------------------------------------------------------
//                                                VERTEX ENTRY (L1341-L1470)
//------------------------------------------------------------------------------------------------------------------------

[shader("vertex")]
CarrierSolidVertexOutput ParticleCarrierSolidVertexEntry(uint vertexId : SV_VertexID)
{
    int triangles = max(int(uParams.SolidCountsStride.y + 0.5), 1);
    int vertices  = max(int(uParams.SolidCountsStride.x + 0.5), 3);
    int wireMode  = int(uParams.SolidCountsStride.w + 0.5);
    int isBacking = int(uParams.BackingParams.x + 0.5);
    int carrierStride = max(int(uParams.SolidCountsStride.z + 0.5), 1);

    int stride = (wireMode == 1) ? (triangles * 6) : vertices;
    int instanceIndex = int(vertexId) / stride;
    int particle = (isBacking == 1) ? 0 : (instanceIndex * carrierStride);
    int slot = int(vertexId) - instanceIndex * stride;

    int vertexIndex;
    if (wireMode == 1)
    {
        int triIndex = slot / 6;
        int edge = slot - triIndex * 6;
        int corner = (edge + 1) / 2;
        if (corner > 2) corner = 0;
        vertexIndex = triIndex * 3 + corner;
    }
    else
    {
        vertexIndex = slot;
    }

    float4 first  = gSolidTexels[vertexIndex * 2];
    float4 second = gSolidTexels[vertexIndex * 2 + 1];
    float3 local  = first.xyz;
    float3 normal = float3(first.w, second.xy);

    int side = max(1, int(uParams.Shape.AnchorLayout.z + 0.5));
    int safeParticle = clamp(particle, 0, side * side - 1);
    ParticleStateRecord state = gStates[safeParticle];
    ParticleShadeResult shaded = EvaluateParticleShade(
        uParams.Shape, uParams.Shade, gAnchorTexels, safeParticle, state.PositionAge, state.VelocitySeed);

    float3 base = (isBacking == 1) ? uParams.Shape.OriginRadius.xyz : state.PositionAge.xyz;

    // 3D staccato Glitch jumps also affect 3D carrier particles (L1405-L1415).
    float glitchAmount     = uParams.Shape.GlitchParams.x;
    float glitchRate       = uParams.Shape.GlitchParams.y;
    float glitchStepJitter = uParams.Shape.GlitchParams.z;
    int   derezMode        = int(uParams.Shape.EffectorAxisDerez.y + 0.5);
    float derezMix         = uParams.Shape.EffectorAxisDerez.z;
    float simTime          = uParams.Shade.TimingRegionHeight.y;
    if (isBacking == 0 && ((glitchAmount > 0.001 && glitchStepJitter > 0.001) || (derezMode == 5 && shaded.DerezReleased > 0.5)))
    {
        float rate = max(glitchRate, 1.0);
        float stepTick = floor(simTime * rate);
        float3 jSeed = float3(float(safeParticle) * 0.13 + stepTick, state.VelocitySeed.w * 31.7 + stepTick, stepTick * 0.73);
        float gate = step(0.42 - glitchAmount * 0.35, Hash31(jSeed + 4.1));
        float amp = glitchAmount * glitchStepJitter * gate
            + ((derezMode == 5 && shaded.DerezReleased > 0.5) ? (0.09 * derezMix) : 0.0);
        base += (float3(Hash31(jSeed), Hash31(jSeed + 7.3), Hash31(jSeed + 13.9)) - 0.5) * 2.0 * amp;
    }

    float sizeSeed   = frac(state.VelocitySeed.w * 41.7);
    float sizeRandom = exp2((sizeSeed - 0.5) * 3.0 * uParams.SizeParams.z);
    float radius     = (isBacking == 1)
        ? uParams.BackingParams.y
        : (lerp(uParams.SizeParams.x, uParams.SizeParams.y, shaded.Life) * sizeRandom);

    int   effectorMode       = int(uParams.Shape.MocapKickEffector.y + 0.5);
    float effectorStrength   = uParams.Shape.MocapKickEffector.z;
    float effectorScaleBoost = uParams.Shape.EffectorMotion.z;
    if (isBacking == 0 && effectorMode > 0 && effectorStrength > 0.0001 && abs(effectorScaleBoost) > 0.0001)
    {
        radius *= max(0.08, 1.0 + shaded.EffectorWeight * effectorScaleBoost);
    }

    float depth    = dot(base - uParams.CameraPositionFocal.xyz, uParams.CameraForwardPixelRatio.xyz);
    int   sizeMode = int(uParams.SizeParams.w + 0.5);
    if (isBacking == 0 && sizeMode == 1)
    {
        radius = radius * uParams.CameraForwardPixelRatio.w * max(depth, 0.05) / max(uParams.CameraPositionFocal.w, 1.0);
    }

    float3 spun;
    float3 spunNormal;
    if (isBacking == 1)
    {
        // Backing surface matches the exact SolidYaw + (t, axis, b) orientation used by ObjectSurface anchors (L1432-L1441).
        float yaw = uParams.BackingParams.z;
        float cy = cos(yaw), sy = sin(yaw);
        float3 yawed       = float3(local.x * cy + local.z * sy, local.y, -local.x * sy + local.z * cy);
        float3 yawedNormal = float3(normal.x * cy + normal.z * sy, normal.y, -normal.x * sy + normal.z * cy);
        float3 axis = uParams.Shape.AxisFill.xyz;
        float3 tAxis, bAxis;
        ShapeBasis(axis, tAxis, bAxis);
        spun       = tAxis * yawed.x + axis * yawed.y + bAxis * yawed.z;
        spunNormal = normalize(tAxis * yawedNormal.x + axis * yawedNormal.y + bAxis * yawedNormal.z);
    }
    else
    {
        float angle = uParams.SpinTiltRoughMetal.x * simTime + state.VelocitySeed.w * 6.2831853;
        float ca = cos(angle), sa = sin(angle);
        float tilt = uParams.SpinTiltRoughMetal.y * (frac(state.VelocitySeed.w * 3.7) - 0.5) * 1.6;
        float ct = cos(tilt), st = sin(tilt);
        spun       = float3(local.x * ca + local.z * sa, local.y, -local.x * sa + local.z * ca);
        spunNormal = float3(normal.x * ca + normal.z * sa, normal.y, -normal.x * sa + normal.z * ca);
        spun       = float3(spun.x, spun.y * ct - spun.z * st, spun.y * st + spun.z * ct);
        spunNormal = float3(spunNormal.x, spunNormal.y * ct - spunNormal.z * st, spunNormal.y * st + spunNormal.z * ct);
    }

    float3 world = base + spun * radius;
    float outAlpha = (isBacking == 1) ? 1.0 : shaded.Alpha;

    CarrierSolidVertexOutput o;
    o.WorldPos     = world;
    o.WorldNormal  = spunNormal;
    o.LocalPos     = local;
    o.Colour       = shaded.Colour;
    o.Alpha        = outAlpha;
    o.Ridge        = second.w;
    o.ClipPosition = (outAlpha <= 0.002) ? float4(2.0, 2.0, 2.0, 1.0) : mul(uParams.ViewProjection, float4(world, 1.0));
    return o;
}

//------------------------------------------------------------------------------------------------------------------------
//                                               FRAGMENT ENTRY (L1471-L1551)
//------------------------------------------------------------------------------------------------------------------------

[shader("fragment")]
float4 ParticleCarrierSolidFragmentEntry(CarrierSolidVertexOutput input) : SV_Target
{
    if (input.Alpha <= 0.002) discard;

    float3 N = normalize(input.WorldNormal);
    float3 V = normalize(uParams.CameraPositionFocal.xyz - input.WorldPos);
    if (dot(N, V) < 0.0) N = -N;

    float3 keyLight = normalize(float3(0.42, 0.82, 0.46));
    float3 rimLight = normalize(float3(-0.55, 0.28, -0.62));
    float  fresnel  = pow(1.0 - clamp(dot(N, V), 0.0, 1.0), 4.0);

    int    backingMode   = int(uParams.BackingParams.w + 0.5);
    int    wireMode      = int(uParams.SolidCountsStride.w + 0.5);
    float3 backingColour = uParams.BackingColourGlow.xyz;
    float  solidGlow     = uParams.BackingColourGlow.w;
    float3 rgb;

    if (backingMode == 0)
    {
        // Solid PBR-ish carrier: Blinn-Phong wrapped diffuse, metallic specular boost, grazing Fresnel rim, ridge accent.
        float  roughness = clamp(uParams.SpinTiltRoughMetal.z, 0.02, 1.0);
        float  metallic  = clamp(uParams.SpinTiltRoughMetal.w, 0.0, 1.0);
        float  specPower = lerp(96.0, 12.0, roughness);
        float3 halfKey   = normalize(keyLight + V);
        float3 halfRim   = normalize(rimLight + V);
        float  diffuse   = max(dot(N, keyLight), 0.0) * 0.72 + max(dot(N, rimLight), 0.0) * 0.24 + 0.10;
        float  specular  = pow(max(dot(N, halfKey), 0.0), specPower)
                         + pow(max(dot(N, halfRim), 0.0), specPower * 0.55) * 0.45;
        float  metallicBoost = lerp(0.35, 1.15, metallic);
        float  luma      = dot(input.Colour, float3(0.30, 0.59, 0.11));
        float3 albedo    = lerp(input.Colour, float3(luma, luma, luma), metallic * 0.55);
        rgb  = albedo * diffuse * (1.0 - metallic * 0.35) + float3(specular * metallicBoost) * lerp(0.55, 1.0, metallic);
        rgb += input.Colour * fresnel * (0.24 + solidGlow);
        rgb += input.Colour * input.Ridge * 0.22;
    }
    else if (backingMode == 1)
    {
        // ObsidianGlossy: jet-black plinth with two overhead softbox panels and a deep Fresnel edge.
        float3 base  = float3(0.022, 0.026, 0.036);
        float3 boxA  = normalize(float3(-0.35, 0.92, 0.28));
        float3 boxB  = normalize(float3(0.48, 0.74, -0.44));
        float  softA = pow(max(dot(N, normalize(boxA + V)), 0.0), 120.0);
        float  softB = pow(max(dot(N, normalize(boxB + V)), 0.0), 84.0);
        float  edge  = pow(1.0 - clamp(dot(N, V), 0.0, 1.0), 4.5);
        rgb  = base + float3(softA * 1.5 + softB * 0.9) * backingColour + float3(edge * 0.42) * backingColour;
        rgb += input.Colour * 0.06;
    }
    else if (backingMode == 2)
    {
        // BrushedChrome: horizontal anisotropy from local solid height, bright horizon reflection band.
        float brushed = 0.5 + 0.5 * sin(input.LocalPos.y * 210.0);
        float horizon = smoothstep(0.15, 0.9, abs(N.y));
        float spec    = pow(max(dot(N, normalize(keyLight + V)), 0.0), 64.0);
        rgb  = lerp(float3(0.30, 0.33, 0.38), float3(0.86, 0.90, 0.96), horizon) * (0.55 + 0.45 * brushed);
        rgb += float3(spec) * 0.9 + float3(fresnel * 0.35) * backingColour;
    }
    else
    {
        // GhostWireframe: emissive silhouette only, brightest where surface turns away.
        rgb  = backingColour * (0.45 + 1.35 * fresnel) * ((wireMode == 1) ? 1.15 : 0.85);
        rgb *= 0.85 + 0.3 * input.Ridge;
    }

    float alpha = input.Alpha;
    if (backingMode == 3) alpha *= 0.55 + 0.45 * fresnel;
    return float4(rgb, alpha);
}
