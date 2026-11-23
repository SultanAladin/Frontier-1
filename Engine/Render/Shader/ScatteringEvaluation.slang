/*==========================================================================================================================================
                                                     SCATTERINGEVALUATION.SLANG
==========================================================================================================================================*/
// 🧩 Shared angular scattering and scene-linear display conversion for preview and authored materials.

struct ScatteringStratum
{
    float4 AlbedoRoughness;
    float4 Optics;
    float4 AbsorptionThickness;
    uint4 Classification;
    // 📝 Charlie-distribution fabric sheen (fuzz rim); zero by default, bit-identical for every
    // non-fabric stratum. [rgb] tint colour, [a] intensity; SheenFinish.x is roughness.
    float4 Sheen;
    float4 SheenFinish;
};
struct PreviewScattering
{
    column_major float4x4 ViewFromSurface;
    float4 LightDirectionIntensity;
    float4 LightColourExposure;
    float4 EnvironmentHorizon;
    float4 EnvironmentZenith;
    float4 EnvironmentUp;
    uint4 Configuration;
    ScatteringStratum Strata[8];
};
static const float π = 3.141592653589793;

float3 NormalizeDirection(float3 Direction)
{
    float Maximum = max(abs(Direction.x),max(abs(Direction.y),abs(Direction.z)));
    Direction = Maximum > 1e-20 ? Direction/Maximum : float3(0,0,1);
    return normalize(Direction);
}
float EvaluateGgxDistribution(float NormalHalf,float Roughness)
{
    float α = Roughness*Roughness;
    float Denominator = NormalHalf*NormalHalf*(α*α-1)+1;
    return α*α/(π*Denominator*Denominator);
}
float2 DeriveAnisotropicSpread(float Roughness,float Anisotropy)
{
    float α = Roughness*Roughness;
    float Aspect = sqrt(1-.9*abs(Anisotropy));
    return Anisotropy >= 0 ? float2(α/Aspect,α*Aspect) : float2(α*Aspect,α/Aspect);
}
float EvaluateAnisotropicDistribution(float3 Half,float2 Spread)
{
    if (Half.z <= 0) return 0;
    float Denominator = dot(Half.xy/Spread,Half.xy/Spread)+Half.z*Half.z;
    return 1/(π*Spread.x*Spread.y*Denominator*Denominator);
}
float EvaluateAnisotropicVisibility(float3 View,float3 Light,float2 Spread)
{
    if (View.z <= 0 || Light.z <= 0) return 0;
    float ViewLength = length(float3(View.xy*Spread,View.z));
    float LightLength = length(float3(Light.xy*Spread,Light.z));
    return .5/max(1e-12,Light.z*ViewLength+View.z*LightLength);
}
// 📝 Charlie sheen lobe (Estevez & Kulla), identical to the CPU reference in ScatteringSequence.h
// so the fabric fuzz rim matches between the shader-ball preview and the host validation suite.
float EvaluateCharlieDistribution(float Roughness,float NormalHalf)
{
    float InverseAlpha = 1/max(Roughness*Roughness,1e-3);
    float SineSquared = max(1-NormalHalf*NormalHalf,.0078125);
    return (2+InverseAlpha)*pow(SineSquared,InverseAlpha*.5)/(2*π);
}
float EvaluateAshikhminVisibility(float NormalView,float NormalLight)
{
    return 1/max(1e-4,4*(NormalLight+NormalView-NormalLight*NormalView));
}
float EvaluateExactDielectricReflectance(float Cosine,float IncidentIor,float TransmittedIor)
{
    if (IncidentIor == TransmittedIor) return 0;
    Cosine = saturate(abs(Cosine));
    float Ratio = IncidentIor/TransmittedIor;
    float SineSquared = Ratio*Ratio*(1-Cosine*Cosine);
    if (SineSquared >= 1) return 1;
    float TransmittedCosine = sqrt(1-SineSquared);
    float Perpendicular = (IncidentIor*Cosine-TransmittedIor*TransmittedCosine)/
                          (IncidentIor*Cosine+TransmittedIor*TransmittedCosine);
    float Parallel = (TransmittedIor*Cosine-IncidentIor*TransmittedCosine)/
                     (TransmittedIor*Cosine+IncidentIor*TransmittedCosine);
    return .5*(Perpendicular*Perpendicular+Parallel*Parallel);
}
// 📝 Analytic hemisphere bound, identical to the retained scalar evaluator; no texture or LUT allocation.
float EvaluateSheenEnergyScale(float Roughness)
{
    float InverseAlpha = 1/max(Roughness*Roughness,1e-3);
    float Radius = sqrt(InverseAlpha/8);
    float Bound = (InverseAlpha+2)*atan(Radius)/(4*Radius);
    return 1/max(1,Bound);
}
float3 EvaluateDiffuseTransmission(float3 FresnelOrigin,float NormalView,float NormalLight)
{
    float3 Entrance = FresnelOrigin+(1-FresnelOrigin)*pow(1-NormalLight,5);
    float3 Exit = FresnelOrigin+(1-FresnelOrigin)*pow(1-NormalView,5);
    return (1-Entrance)*(1-Exit);
}
float3 EvaluateScatteringReflection(ScatteringStratum Stratum,float3 Albedo,float ViewHalf,
                                   float Distribution,float Visibility,bool ExactDielectricEnabled,float SpecularLevel,
                                   float NormalView,float NormalLight,float NormalHalf)
{
    float Reflectance = (Stratum.Optics.y-1)/(Stratum.Optics.y+1);
    Reflectance *= Reflectance;
    float3 DielectricReflectance = float3(Reflectance * SpecularLevel * 2);
    float3 FresnelOrigin = lerp(DielectricReflectance,Albedo,Stratum.Optics.x);
    float3 Fresnel = ExactDielectricEnabled ?
        lerp(float3(EvaluateExactDielectricReflectance(ViewHalf,1,Stratum.Optics.y) * SpecularLevel * 2),Albedo,Stratum.Optics.x) :
        FresnelOrigin+(1-FresnelOrigin)*pow(1-ViewHalf,5);
    float3 Evaluation = Distribution*Visibility*Fresnel;
    if (Stratum.Classification.x == 0)
    {
        Evaluation += EvaluateDiffuseTransmission(FresnelOrigin,NormalView,NormalLight)*(1-Stratum.Optics.x)*Albedo/π;
        // 📝 Fuzz coverage replaces base scattering rather than adding a second unrestricted energy budget.
        if (Stratum.Sheen.a > 0 && any(Stratum.Sheen.rgb > 0))
        {
            float3 FuzzCoverage = saturate(Stratum.Sheen.rgb)*Stratum.Sheen.a;
            float Sheen = EvaluateSheenEnergyScale(Stratum.SheenFinish.x)*
                EvaluateCharlieDistribution(Stratum.SheenFinish.x,NormalHalf)*EvaluateAshikhminVisibility(NormalView,NormalLight);
            Evaluation = lerp(Evaluation,float3(Sheen),FuzzCoverage);
        }
    }
    return Evaluation;
}

float3 EvaluateScatteringSequence(PreviewScattering Scattering,float3 Normal,float3 View,float3 Light,float3 AlbedoMultiplier)
{
    float NormalView = saturate(dot(Normal,View));
    float NormalLight = saturate(dot(Normal,Light));
    if (NormalView <= 0 || NormalLight <= 0) return 0;
    float3 Half = NormalizeDirection(View+Light);
    float NormalHalf = saturate(dot(Normal,Half));
    float ViewHalf = saturate(dot(View,Half));
    float3 Accumulated = 0;
    for (uint Index = 0; Index < Scattering.Configuration.x; ++Index)
    {
        ScatteringStratum Stratum = Scattering.Strata[Index];
        float3 Albedo = Stratum.AlbedoRoughness.rgb * (Index == 0 ? AlbedoMultiplier : float3(1));
        float α = Stratum.AlbedoRoughness.w*Stratum.AlbedoRoughness.w;
        float Visibility = 0.5/max(1e-12,NormalLight*sqrt(NormalView*NormalView*(1-α*α)+α*α) +
                                       NormalView*sqrt(NormalLight*NormalLight*(1-α*α)+α*α));
        float Distribution = EvaluateGgxDistribution(NormalHalf,Stratum.AlbedoRoughness.w);
        float Reflectance = (Stratum.Optics.y-1)/(Stratum.Optics.y+1);
        Reflectance *= Reflectance;
        float3 Evaluation = EvaluateScatteringReflection(Stratum,Albedo,ViewHalf,Distribution,Visibility,false,.5,
            NormalView,NormalLight,NormalHalf);
        if (Stratum.Classification.x == 1)
        {
            float Entrance = Reflectance+(1-Reflectance)*pow(1-NormalLight,5);
            float Exit = Reflectance+(1-Reflectance)*pow(1-NormalView,5);
            float3 Absorption = exp(-Stratum.AbsorptionThickness.rgb*Stratum.AbsorptionThickness.w*
                                    (1/max(.001,NormalView)+1/max(.001,NormalLight)));
            Evaluation += Accumulated*((1-Entrance)*(1-Exit))*Absorption;
        }
        Accumulated = lerp(Accumulated,Evaluation,Stratum.Optics.z);
    }
    return Accumulated;
}
#ifdef FRONTIER_ENVIRONMENT_MAP
// 📝 Map-lit translation units (authored materials) define this sampler over the baked equirect
// studio archives; the analytic gradient below remains for the lightweight preview program.
float3 SampleEnvironmentMapRadiance(PreviewScattering Scattering,float3 Direction);
float3 EvaluateEnvironmentRadiance(PreviewScattering Scattering,float3 Direction)
{
    return SampleEnvironmentMapRadiance(Scattering,Direction)*Scattering.EnvironmentHorizon.w;
}
#else
float3 EvaluateEnvironmentRadiance(PreviewScattering Scattering,float3 Direction)
{
    float Height = saturate(.5+.5*dot(NormalizeDirection(Scattering.EnvironmentUp.xyz),Direction));
    return lerp(Scattering.EnvironmentHorizon.rgb*.35,Scattering.EnvironmentZenith.rgb,Height)*Scattering.EnvironmentHorizon.w;
}
#endif
float3 IntegrateEnvironment(PreviewScattering Scattering,float3 Normal,float3 View,float3 AlbedoMultiplier)
{
    if (Scattering.EnvironmentHorizon.w == 0) return 0;
    // 📝 Integrate the cosine-weighted vertical sky analytically; approximate each rough specular lobe by its broadened reflection direction.
    // This removes the sparse per-pixel quadrature that produced visible stipple in polished preview materials.
    float3 Up = NormalizeDirection(Scattering.EnvironmentUp.xyz);
    float DiffuseHeight = saturate(.5+dot(Up,Normal)/3);
    float3 DiffuseRadiance = lerp(Scattering.EnvironmentHorizon.rgb*.35,
        Scattering.EnvironmentZenith.rgb,DiffuseHeight)*Scattering.EnvironmentHorizon.w;
    float3 Reflection = NormalizeDirection(reflect(-View,Normal));
    float3 Accumulated = 0;
    for (uint Index = 0; Index < Scattering.Configuration.x; ++Index)
    {
        ScatteringStratum Stratum = Scattering.Strata[Index];
        float3 Albedo = Stratum.AlbedoRoughness.rgb*(Index == 0 ? AlbedoMultiplier : float3(1));
        float Metallic = saturate(Stratum.Optics.x);
        float IorReflectance = (Stratum.Optics.y-1)/(Stratum.Optics.y+1);
        IorReflectance *= IorReflectance;
        float NormalView = saturate(dot(Normal,View));
        float3 F0 = lerp(float3(IorReflectance),Albedo,Metallic);
        float3 Fresnel = F0+(1-F0)*pow(1-NormalView,5);
        float Roughness = Stratum.AlbedoRoughness.w;
        float Broadening = saturate(Roughness*Roughness*1.5);
        float3 ReflectionDirection = NormalizeDirection(lerp(Reflection,Normal,Broadening));
        float3 SpecularRadiance = EvaluateEnvironmentRadiance(Scattering,ReflectionDirection);
        float3 Evaluation = Fresnel*SpecularRadiance;
        if (Stratum.Classification.x == 0)
            Evaluation += Albedo*(1-Metallic)*(1-Fresnel)*DiffuseRadiance;
        else
        {
            float NormalLight = saturate(dot(Normal,ReflectionDirection));
            float Entrance = IorReflectance+(1-IorReflectance)*pow(1-NormalView,5);
            float Exit = IorReflectance+(1-IorReflectance)*pow(1-NormalLight,5);
            float3 Absorption = exp(-Stratum.AbsorptionThickness.rgb*Stratum.AbsorptionThickness.w*
                (1/max(.001,NormalView)+1/max(.001,NormalLight)));
            Evaluation += Accumulated*((1-Entrance)*(1-Exit))*Absorption;
        }
        Accumulated = lerp(Accumulated,Evaluation,Stratum.Optics.z);
    }
    return Accumulated;
}
float3 EncodeDisplay(float3 Radiance,float Exposure)
{
    float3 Exposed = max(0,Radiance*Exposure);
    float3 Display = saturate(Exposed*(2.51*Exposed+.03)/(Exposed*(2.43*Exposed+.59)+.14));
    return select(Display<=.0031308,12.92*Display,1.055*pow(Display,float3(1.0/2.4))-.055);
}
