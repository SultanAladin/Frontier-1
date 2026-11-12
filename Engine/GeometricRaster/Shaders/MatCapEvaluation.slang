//============================================================================================================================================
//                                                           MATCAPEVALUATION.SLANG
//============================================================================================================================================
// Procedural 16-slot studio & engineering MatCap evaluator, Cook-Torrance microfacet BRDF, and analytic ground grid shading

module MatCapEvaluation;

public float3 EvaluateProceduralMatCap(uint SlotIndex, float3 ViewSpaceNormal)
{
    float2 MatUV = ViewSpaceNormal.xy * 0.5 + 0.5;
    float  Radial = saturate(length(ViewSpaceNormal.xy));
    float  NorthLight = saturate(dot(normalize(ViewSpaceNormal), normalize(float3(-0.38, 0.68, 0.62))));
    float  RimFactor  = pow(saturate(1.0 - max(ViewSpaceNormal.z, 0.0)), 2.6);
    float  SpecLobe   = pow(NorthLight, 28.0);
    float  SoftLobe   = pow(NorthLight, 5.0);

    uint ClampedSlot = SlotIndex & 15u;
    if (ClampedSlot == 0u) // 0: Anodized Aluminum
        return lerp(float3(0.16, 0.18, 0.22), float3(0.84, 0.88, 0.94), SoftLobe * 0.72 + NorthLight * 0.28) + SpecLobe * 0.42;
    if (ClampedSlot == 1u) // 1: Brushed Titanium
        return lerp(float3(0.19, 0.18, 0.17), float3(0.79, 0.76, 0.71), NorthLight) + SpecLobe * 0.35 + RimFactor * 0.12;
    if (ClampedSlot == 2u) // 2: Studio Sculpt Clay
        return lerp(float3(0.24, 0.20, 0.18), float3(0.78, 0.67, 0.59), NorthLight * 0.85 + 0.10) + RimFactor * 0.08;
    if (ClampedSlot == 3u) // 3: Warm Machined Brass
        return lerp(float3(0.22, 0.14, 0.05), float3(0.95, 0.74, 0.32), SoftLobe * 0.65 + NorthLight * 0.35) + SpecLobe * 0.55;
    if (ClampedSlot == 4u) // 4: Satin Chrome
        return lerp(float3(0.08, 0.09, 0.11), float3(0.96, 0.97, 0.99), pow(NorthLight, 3.2)) + pow(NorthLight, 48.0) * 0.75;
    if (ClampedSlot == 5u) // 5: Carpaint Cobalt
        return lerp(float3(0.05, 0.12, 0.28), float3(0.28, 0.58, 0.96), NorthLight) + pow(NorthLight, 36.0) * 0.68 + RimFactor * float3(0.12, 0.28, 0.55);
    if (ClampedSlot == 6u) // 6: Tactical Olive Polymer
        return lerp(float3(0.12, 0.15, 0.11), float3(0.46, 0.54, 0.42), NorthLight * 0.90 + 0.06) + SoftLobe * 0.14;
    if (ClampedSlot == 7u) // 7: Obsidian High-Gloss
        return lerp(float3(0.03, 0.03, 0.04), float3(0.16, 0.18, 0.22), NorthLight) + pow(NorthLight, 64.0) * 0.92 + RimFactor * 0.18;
    if (ClampedSlot == 8u) // 8: Smoked Canopy Glass
        return lerp(float3(0.05, 0.08, 0.12), float3(0.32, 0.46, 0.58), RimFactor * 0.75 + SoftLobe * 0.25) + pow(NorthLight, 56.0) * 0.88;
    if (ClampedSlot == 9u) // 9: Architectural Limestone
        return lerp(float3(0.28, 0.27, 0.25), float3(0.84, 0.82, 0.77), NorthLight * 0.88 + 0.12);
    if (ClampedSlot == 10u) // 10: Zebra Continuity Stripes
    {
        float Stripe = 0.5 + 0.5 * cos((ViewSpaceNormal.x * 9.0 + ViewSpaceNormal.y * 9.0) * 3.14159265);
        float Crisp  = smoothstep(0.42, 0.58, Stripe);
        return lerp(float3(0.08, 0.09, 0.11), float3(0.94, 0.96, 0.98), Crisp);
    }
    if (ClampedSlot == 11u) // 11: Forged Copper
        return lerp(float3(0.24, 0.09, 0.06), float3(0.94, 0.52, 0.34), SoftLobe * 0.6 + NorthLight * 0.4) + SpecLobe * 0.48;
    if (ClampedSlot == 12u) // 12: Ceramic Studio Ivory
        return lerp(float3(0.32, 0.33, 0.29), float3(0.94, 0.96, 0.84), NorthLight * 0.86 + 0.14) + SpecLobe * 0.32;
    if (ClampedSlot == 13u) // 13: Satin Carbon
        return lerp(float3(0.06, 0.06, 0.07), float3(0.34, 0.36, 0.40), SoftLobe) + SpecLobe * 0.26;
    if (ClampedSlot == 14u) // 14: Rim Silhouette Check
        return lerp(float3(0.06, 0.07, 0.09), float3(0.58, 0.86, 1.00), RimFactor);

    // 15: Normal Hemisphere Diagnostic
    return saturate(ViewSpaceNormal * 0.5 + 0.5) * (0.75 + 0.25 * (1.0 - Radial));
}

public float3 EvaluateStudioSurfaceMaterial(
    float3 WorldNormal,
    float3 ViewDir,
    float3 AlbedoLinear,
    float  Metallic,
    float  Roughness,
    float  Specular,
    uint   MatCapSlot,
    float  MatCapBlend,
    float3 CameraRight,
    float3 CameraUp,
    float3 CameraForward)
{
    float3 N = normalize(WorldNormal);
    float3 V = normalize(ViewDir);
    float3 KeyDir  = normalize(float3(0.52, 0.76, 0.39));
    float3 FillDir = normalize(float3(-0.64, 0.34, -0.68));
    float3 HalfKey = normalize(KeyDir + V);

    float NdotL = saturate(dot(N, KeyDir));
    float NdotF = saturate(dot(N, FillDir));
    float NdotH = saturate(dot(N, HalfKey));
    float NdotV = saturate(dot(N, V));

    float ClampedRough = clamp(Roughness, 0.05, 1.0);
    float Alpha2       = ClampedRough * ClampedRough * ClampedRough * ClampedRough;
    float Denom        = (NdotH * NdotH * (Alpha2 - 1.0) + 1.0);
    float GGX          = Alpha2 / max(3.14159265 * Denom * Denom, 1.0e-4);

    float3 F0      = lerp(float3(0.08 * Specular), AlbedoLinear, Metallic);
    float3 Fresnel = F0 + (1.0 - F0) * pow(1.0 - NdotV, 5.0);

    float3 DiffuseTerm  = AlbedoLinear * (1.0 - Metallic) * (0.20 + 0.82 * NdotL + 0.28 * NdotF);
    float3 SpecularTerm = Fresnel * min(GGX * 0.28, 3.5) * (0.25 + 0.75 * NdotL);
    float3 DirectPBR    = DiffuseTerm + SpecularTerm;

    float3 ViewNormal = normalize(float3(
        dot(N, CameraRight),
        dot(N, CameraUp),
        dot(N, -CameraForward)));
    float3 MatCapColor = EvaluateProceduralMatCap(MatCapSlot, ViewNormal);
    float3 TintedMatCap = lerp(MatCapColor, MatCapColor * (AlbedoLinear * 1.45 + 0.15), 0.55);

    return lerp(DirectPBR, TintedMatCap, saturate(MatCapBlend));
}

public float3 CompressRadiometricACESFilmic(float3 LinearHDR)
{
    float3 X = max(LinearHDR, float3(0.0));
    float3 CompressedTone = (X * (2.51 * X + 0.03)) / (X * (2.43 * X + 0.59) + 0.14);
    return pow(saturate(CompressedTone), float3(1.0 / 2.2));
}
