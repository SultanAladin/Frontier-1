/*============================================================================================================================================
                                                    TRANSMISSIVESURFACERASTERIZATION.SLANG
============================================================================================================================================*/
// 🧩 Forward refractive & luminous surface overlay (Ocean Water refraction + Beer-Lambert depth, Hexagonal Energy Barrier, Smoked Rain Glass)

module TransmissiveSurfaceRasterization;

import StudioEnvironmentRadiance;

public float3 ResolveTransmissiveWaterComposite(
    float3 OpaqueSurfaceRadiance,
    float3 BackdropRadiance,
    float3 Normal,
    float3 ViewDir,
    float3 WaterDeepColor,
    float3 WaterShallowColor,
    float  CrestScatter,
    float  WaterIor)
{
    float NdotV   = saturate(dot(Normal, ViewDir));
    float F0      = pow((WaterIor - 1.0) / (WaterIor + 1.0), 2.0);
    float Fresnel = F0 + (1.0 - F0) * pow(1.0 - NdotV, 5.0);

    // Beer-Lambert depth absorption across the refracted interior chord
    float  ChordDepth  = lerp(1.35, 0.35, NdotV);
    float3 TransmitAtt = exp(-float3(1.65, 0.55, 0.38) * ChordDepth);
    float3 Refracted   = BackdropRadiance * TransmitAtt + lerp(WaterDeepColor, WaterShallowColor, CrestScatter) * (1.0 - TransmitAtt);
    float3 Reflected   = SampleCalibratedStudioEnvironment(reflect(-ViewDir, Normal), 0.04);

    return lerp(Refracted, Reflected, saturate(Fresnel + 0.14)) + OpaqueSurfaceRadiance * 0.55;
}

public float3 ResolveTranslucentBarrierComposite(
    float3 SurfaceSpecular,
    float3 LuminousBarrierRadiance,
    float3 BackdropRadiance,
    float  OpacityAlpha)
{
    float Alpha = saturate(OpacityAlpha);
    return BackdropRadiance * (1.0 - Alpha * 0.72) + SurfaceSpecular * Alpha + LuminousBarrierRadiance;
}
