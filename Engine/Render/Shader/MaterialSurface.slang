/*==========================================================================================================================================
                                                         MATERIALSURFACE.SLANG
==========================================================================================================================================*/
// 🧩 Authored UV/tangent material evaluation with typed trilinear textures and the shared ordered-scattering equations.

#define FRONTIER_ENVIRONMENT_MAP 1
#include "ScatteringEvaluation.slang"
#include "AutomotivePaint.slang"

struct SurfaceNormalProjection
{
    column_major float4x4 ClipFromSurface;
    float4 ViewNormalRow0;
    float4 ViewNormalRow1;
    float4 ViewNormalRow2;
    float4 SurfaceTint;
};
struct MaterialScattering
{
    PreviewScattering Illumination;
    float4 UvScaleOffset;
    float4 RadianceNormal;
    uint4 TextureUse[8];
    float4 StratumUv[8];
    float4 StratumNormal[8];
    float4 DirectionalOptics[8];
    AutomotivePaint Paint;
    uint4 PreviewConfiguration;
};
[[vk::push_constant]] ConstantBuffer<SurfaceNormalProjection> Projection;
[[vk::binding(0,0)]] Texture2DArray<float4> MaterialTextures[4];
[[vk::binding(1,0)]] SamplerState MaterialSampler;
[[vk::binding(2,0)]] Texture2DArray<float4> AuthoringMaps[6];
[[vk::binding(3,0)]] Texture2DArray<float4> EnvironmentMap;
[[vk::binding(0,1)]] ConstantBuffer<MaterialScattering> Material;

// 📝 Baked equirect studio archives (EngineContent/EnvironmentArchives), one layer per entry in
// scan order. Directions arrive in view space; the map lives in the fixed world (surface) frame,
// so highlights sweep across the specimen while the camera orbits, exactly like the key light.
float3 SampleEnvironmentMapRadiance(PreviewScattering Scattering,float3 Direction)
{
    float3 World = NormalizeDirection(Direction.x*Projection.ViewNormalRow0.xyz+
        Direction.y*Projection.ViewNormalRow1.xyz+Direction.z*Projection.ViewNormalRow2.xyz);
    float2 Coordinates = float2(atan2(World.x,World.z)/(2*π)+.5,acos(clamp(World.y,-1,1))/π);
    // 📝 Mip 2, not base level: the 8-sample MIS quadrature turns bright small panels into
    // per-pixel fireflies at full resolution (visible stipple on the Windows build). The
    // prefiltered level keeps panel shapes while collapsing hit/miss contrast.
    return EnvironmentMap.SampleLevel(MaterialSampler,float3(Coordinates,Material.PreviewConfiguration.z),2).rgb;
}

struct MaterialInput
{
    float3 Position : POSITION0;
    float3 Normal : NORMAL0;
    float2 Coordinates : TEXCOORD0;
    float4 Tangent : TANGENT0;
};
struct MaterialPixel
{
    float4 Position : SV_Position;
    float3 ViewPosition : TEXCOORD0;
    float3 ViewNormal : NORMAL0;
    float2 Coordinates : TEXCOORD1;
    float4 ViewTangent : TEXCOORD2;
    float3 MaterialPosition : TEXCOORD3;
    nointerpolation uint SubjectRole : TEXCOORD4;
};

[shader("vertex")]
MaterialPixel MaterialSurfaceVertex(MaterialInput Corner)
{
    MaterialPixel Pixel;
    Pixel.Position = mul(Projection.ClipFromSurface,float4(Corner.Position,1));
    Pixel.ViewPosition = mul(Material.Illumination.ViewFromSurface,float4(Corner.Position,1)).xyz;
    Pixel.ViewNormal = float3(dot(Projection.ViewNormalRow0.xyz,Corner.Normal),
                             dot(Projection.ViewNormalRow1.xyz,Corner.Normal),
                             dot(Projection.ViewNormalRow2.xyz,Corner.Normal));
    float3x3 Linear = (float3x3)Material.Illumination.ViewFromSurface;
    Pixel.ViewTangent = float4(mul(Linear,Corner.Tangent.xyz),Corner.Tangent.w*(determinant(Linear)<0 ? -1 : 1));
    Pixel.Coordinates = Corner.Coordinates*Material.UvScaleOffset.xy+Material.UvScaleOffset.zw;
    Pixel.SubjectRole = (Material.PreviewConfiguration.x == 1 || Material.PreviewConfiguration.x == 5) && Corner.Coordinates.x < 0 ? uint(-Corner.Coordinates.x) : 0;
    Pixel.MaterialPosition = Corner.Position;
    if (Material.PreviewConfiguration.x > 1) Pixel.MaterialPosition.x -= 16 * float(Material.PreviewConfiguration.x - 1);
    if (Material.PreviewConfiguration.x == 1 && Pixel.SubjectRole == 0)
    {
        float Angle = Corner.Coordinates.y * π, Longitude = Corner.Coordinates.x * 2 * π;
        float Height = 1.43 * cos(Angle);
        float Radius = max(.001, 1.43 * sin(Angle) - .032 * exp(-pow(abs((Height - .69) / .021), 6)) -
            .023 * exp(-pow(abs((Height - .6) / .014), 6)) - .028 * exp(-pow(abs((Height + .81) / .018), 6)) -
            .028 * exp(-pow(abs((Height + .89) / .018), 6)) - .028 * exp(-pow(abs((Height + .97) / .018), 6)));
        Pixel.MaterialPosition = float3(Radius * sin(Longitude), Height, Radius * cos(Longitude));
    }
    return Pixel;
}

struct MaterialAngularStratum
{
    float3 Normal;
    float3 Tangent;
    float3 Bitangent;
    float2 Spread;
    float Occlusion;
};
float3 ProjectAngularDirection(MaterialAngularStratum Angular,float3 Direction)
{
    return float3(dot(Angular.Tangent,Direction),dot(Angular.Bitangent,Direction),dot(Angular.Normal,Direction));
}
float3 EvaluateMaterialSequence(PreviewScattering Scattering,MaterialAngularStratum Angular[8],
                                float3 View,float3 Light,bool EnvironmentEnabled)
{
    float3 Accumulated = 0;
    float3 Half = NormalizeDirection(View+Light);
    float ViewHalf = saturate(dot(View,Half));
    for (uint Index = 0; Index < Scattering.Configuration.x; ++Index)
    {
        ScatteringStratum Stratum = Scattering.Strata[Index];
        float3 LocalView = ProjectAngularDirection(Angular[Index],View);
        float3 LocalLight = ProjectAngularDirection(Angular[Index],Light);
        float3 LocalHalf = ProjectAngularDirection(Angular[Index],Half);
        bool ExactEnabled = Material.DirectionalOptics[Index].z != 0;
        float Distribution = EvaluateAnisotropicDistribution(LocalHalf,Angular[Index].Spread);
        float Visibility = EvaluateAnisotropicVisibility(LocalView,LocalLight,Angular[Index].Spread);
        float SpecularLevel = Stratum.Classification.x == 1 ? .5 : Material.DirectionalOptics[Index].w;
        float3 Evaluation = EvaluateScatteringReflection(Stratum,Stratum.AlbedoRoughness.rgb,ViewHalf,
                                                        Distribution,Visibility,ExactEnabled,SpecularLevel,
                                                        LocalView.z,LocalLight.z,LocalHalf.z)*max(0,LocalLight.z);
        if (LocalView.z <= 0 || LocalLight.z <= 0) Evaluation = 0;
        if (EnvironmentEnabled) Evaluation *= Angular[Index].Occlusion;
        if (Stratum.Classification.x == 1)
        {
            float Reflectance = (Stratum.Optics.y-1)/(Stratum.Optics.y+1);
            Reflectance *= Reflectance;
            float Entrance = ExactEnabled ? EvaluateExactDielectricReflectance(LocalLight.z,1,Stratum.Optics.y) :
                Reflectance+(1-Reflectance)*pow(1-saturate(LocalLight.z),5);
            float Exit = ExactEnabled ? EvaluateExactDielectricReflectance(LocalView.z,1,Stratum.Optics.y) :
                Reflectance+(1-Reflectance)*pow(1-saturate(LocalView.z),5);
            Entrance = LocalLight.z <= 0 ? 1 : Entrance;
            Exit = LocalView.z <= 0 ? 1 : Exit;
            float3 Absorption = exp(-Stratum.AbsorptionThickness.rgb*Stratum.AbsorptionThickness.w*
                                    (1/max(.001,LocalView.z)+1/max(.001,LocalLight.z)));
            Evaluation += Accumulated*((1-Entrance)*(1-Exit))*Absorption;
        }
        Accumulated = lerp(Accumulated,Evaluation,Stratum.Optics.z);
    }
    return Accumulated;
}
float3 IntegrateMaterialEnvironment(PreviewScattering Scattering,MaterialAngularStratum Angular[8],float3 GeometricNormal,float3 View)
{
    if (Scattering.EnvironmentHorizon.w == 0) return 0;
    float3 Tangent = NormalizeDirection(cross(abs(GeometricNormal.z)<.999 ? float3(0,0,1) : float3(0,1,0),GeometricNormal));
    float3 Bitangent = cross(GeometricNormal,Tangent);
    float3 Accumulated = 0;
    uint Components = Scattering.Configuration.x+1;
    // 📝 Environment quadrature samples the baked studio archive bound at set 0 binding 3.
    const uint Samples = 8;
    for (uint Component = 0; Component < Components; ++Component)
        for (uint SampleIndex = 0; SampleIndex < Samples; ++SampleIndex)
        {
            float First = (SampleIndex+.5)/Samples;
            float Second = frac(SampleIndex*.61803398875+Component*.371);
            float φ = 2*π*Second;
            float3 Direction = float3(sqrt(First)*cos(φ),sqrt(First)*sin(φ),sqrt(1-First));
            float3 Light = Tangent*Direction.x+Bitangent*Direction.y+GeometricNormal*Direction.z;
            if (Component != 0)
            {
                MaterialAngularStratum Stratum = Angular[Component-1];
                // 📝 Stretch an isotropic GGX slope sample, then normalize; its density is D(H)*NdotH, not a visible-normal density.
                float Radius = sqrt(First/(1-First));
                Direction = NormalizeDirection(float3(Stratum.Spread*float2(cos(φ),sin(φ))*Radius,1));
                float3 Half = Stratum.Tangent*Direction.x+Stratum.Bitangent*Direction.y+Stratum.Normal*Direction.z;
                if (dot(View,Half) <= 0) continue;
                Light = reflect(-View,Half);
            }
            float3 Half = NormalizeDirection(View+Light);
            float ViewHalf = max(1e-6,dot(View,Half));
            float Density = max(0,dot(GeometricNormal,Light))/π;
            for (uint Index = 0; Index < Scattering.Configuration.x; ++Index)
            {
                float3 LocalHalf = ProjectAngularDirection(Angular[Index],Half);
                Density += EvaluateAnisotropicDistribution(LocalHalf,Angular[Index].Spread)*max(0,LocalHalf.z)/(4*ViewHalf);
            }
            Density /= Components;
            if (Density <= 1e-12) continue;
            Accumulated += EvaluateMaterialSequence(Scattering,Angular,View,Light,true)*
                           EvaluateEnvironmentRadiance(Scattering,Light)/Density;
        }
    Accumulated /= Components*Samples;
    return Accumulated;
}

[shader("fragment")]
float4 MaterialSurfaceFragment(MaterialPixel Pixel) : SV_Target
{
    PreviewScattering Scattering = Material.Illumination;
    if (Pixel.SubjectRole != 0)
    {
        Scattering.Configuration.x = Pixel.SubjectRole == 1 ? 2 : 1;
        // 📝 Role 4: cloth support ball (#16191f physical, web Viewport parity).
        float3 Tint = Pixel.SubjectRole == 1 ? float3(.015209,.015996,.019382) :
                      Pixel.SubjectRole == 2 ? float3(.006512,.006995,.008568) :
                      Pixel.SubjectRole == 4 ? float3(0.008023,0.009721,0.013702) : float3(.135633,.144128,.165132);
        Scattering.Strata[0].AlbedoRoughness = float4(Tint, Pixel.SubjectRole == 1 ? .5 : Pixel.SubjectRole == 2 ? .35 : Pixel.SubjectRole == 4 ? .4 : .32);
        Scattering.Strata[0].Optics = float4(Pixel.SubjectRole == 1 ? .5 : Pixel.SubjectRole == 2 ? .6 : Pixel.SubjectRole == 4 ? .4 : .85,1.5,1,0);
        Scattering.Strata[0].Classification = 0;
        Scattering.Strata[0].AbsorptionThickness = 0;
        if (Pixel.SubjectRole == 1)
        {
            Scattering.Strata[1].AlbedoRoughness = float4(1,1,1,.1);
            Scattering.Strata[1].Optics = float4(0,1.5,.1,0);
            Scattering.Strata[1].Classification = uint4(1,0,0,0);
            Scattering.Strata[1].AbsorptionThickness = 0;
        }
    }
    PaintSurface Paint;
    bool PaintEnabled = Material.Paint.Configuration.x != 0 && Pixel.SubjectRole == 0;
    if (PaintEnabled) Paint = EvaluatePaintSurface(Material.Paint, Pixel.MaterialPosition,
        Scattering.Strata[0].AlbedoRoughness.rgb, Scattering.Strata[0].AlbedoRoughness.w, Scattering.Strata[0].Optics.x);
    MaterialAngularStratum Angular[8];
    float3 GeometricNormal = NormalizeDirection(Pixel.ViewNormal);
    float3 Tangent = NormalizeDirection(Pixel.ViewTangent.xyz-GeometricNormal*dot(GeometricNormal,Pixel.ViewTangent.xyz));
    float3 Bitangent = cross(GeometricNormal,Tangent)*(Pixel.ViewTangent.w < 0 ? -1 : 1);
    uint Selection = 0;
    for (uint Index = 0; Index < Scattering.Configuration.x; ++Index)
    {
        uint4 TextureSelection = Material.TextureUse[Index];
        uint Mask = Pixel.SubjectRole == 0 ? TextureSelection.x : 0;
        Selection |= Mask;
        float2 Coordinates = Pixel.Coordinates*Material.StratumUv[Index].xy+Material.StratumUv[Index].zw;
        float4 Albedo = MaterialTextures[0].Sample(MaterialSampler,float3(Coordinates,TextureSelection.y));
        float3 Orm = MaterialTextures[1].Sample(MaterialSampler,float3(Coordinates,TextureSelection.z)).rgb;
        float3 TangentNormal = MaterialTextures[2].Sample(MaterialSampler,float3(Coordinates,TextureSelection.w)).rgb*2-1;
        float4 AuthoredColour = AuthoringMaps[0].Sample(MaterialSampler,float3(Coordinates,0));
        float4 AuthoredNormal = AuthoringMaps[1].Sample(MaterialSampler,float3(Coordinates,0));
        float4 AuthoredHeight = AuthoringMaps[2].Sample(MaterialSampler,float3(Coordinates,0));
        float4 AuthoredRoughness = AuthoringMaps[3].Sample(MaterialSampler,float3(Coordinates,0));
        float4 AuthoredMetalness = AuthoringMaps[4].Sample(MaterialSampler,float3(Coordinates,0));
        float4 AuthoredOcclusion = AuthoringMaps[5].Sample(MaterialSampler,float3(Coordinates,0));
        float AuthoringEnabled = Pixel.SubjectRole == 0 ? 1 : 0;
        if ((Mask & 1) != 0) Scattering.Strata[Index].AlbedoRoughness.rgb *= Albedo.rgb;
        if (Index == 0 && Pixel.SubjectRole == 0) Scattering.Strata[Index].AlbedoRoughness.rgb *= Projection.SurfaceTint.rgb;
        Scattering.Strata[Index].AlbedoRoughness.rgb = lerp(Scattering.Strata[Index].AlbedoRoughness.rgb,
            AuthoredColour.rgb,saturate(AuthoredColour.a)*AuthoringEnabled);
        if ((Mask & 2) != 0)
        {
            Scattering.Strata[Index].AlbedoRoughness.w = clamp(Scattering.Strata[Index].AlbedoRoughness.w*Orm.g,.045,1);
            if (Scattering.Strata[Index].Classification.x == 0) Scattering.Strata[Index].Optics.x *= Orm.b;
        }
        Scattering.Strata[Index].AlbedoRoughness.w = clamp(lerp(Scattering.Strata[Index].AlbedoRoughness.w,
            AuthoredRoughness.r,saturate(AuthoredRoughness.a)*AuthoringEnabled),.045,1);
        if (Scattering.Strata[Index].Classification.x == 0)
            Scattering.Strata[Index].Optics.x = lerp(Scattering.Strata[Index].Optics.x,
                AuthoredMetalness.r,saturate(AuthoredMetalness.a)*AuthoringEnabled);
        float3 Normal = GeometricNormal;
        if (PaintEnabled)
        {
            if (Index == 0)
            {
                Scattering.Strata[0].AlbedoRoughness.rgb = Paint.Albedo * Projection.SurfaceTint.rgb;
                Scattering.Strata[0].AlbedoRoughness.w = Paint.Roughness;
                Scattering.Strata[0].Optics.x = Paint.Metallic;
            }
            Normal = DerivePaintNormal(Material.Paint, Paint, Normal, Pixel.ViewPosition, Scattering.Strata[Index].Classification.x != 0);
        }
        // 📝 Evaluate derivatives before the channel-coverage branch so all fragments share a valid quad footprint.
        float2 CoordinateX = ddx(Coordinates);
        float2 CoordinateY = ddy(Coordinates);
        if (AuthoredHeight.a > 0 && AuthoringEnabled > 0)
        {
            // 📝 Convert adjacent height samples into a tangent-space slope; the independent normal map remains composable.
            float HeightLeft = AuthoringMaps[2].Sample(MaterialSampler,float3(Coordinates-CoordinateX,0)).r;
            float HeightRight = AuthoringMaps[2].Sample(MaterialSampler,float3(Coordinates+CoordinateX,0)).r;
            float HeightUpper = AuthoringMaps[2].Sample(MaterialSampler,float3(Coordinates-CoordinateY,0)).r;
            float HeightLower = AuthoringMaps[2].Sample(MaterialSampler,float3(Coordinates+CoordinateY,0)).r;
            float HeightStrength = Material.StratumNormal[Index].y;
            float3 HeightNormal = NormalizeDirection(float3(-(HeightRight-HeightLeft)*HeightStrength,
                -(HeightLower-HeightUpper)*HeightStrength,1));
            TangentNormal = NormalizeDirection(lerp(TangentNormal,NormalizeDirection(TangentNormal+HeightNormal-float3(0,0,1)),
                saturate(AuthoredHeight.a)*AuthoringEnabled));
        }
        if ((Mask & 4) != 0 || (AuthoredNormal.a > 0 && AuthoringEnabled > 0) || (AuthoredHeight.a > 0 && AuthoringEnabled > 0))
        {
            if (AuthoredNormal.a > 0 && AuthoringEnabled > 0)
                TangentNormal = NormalizeDirection(lerp(TangentNormal,AuthoredNormal.rgb*2-1,
                    saturate(AuthoredNormal.a)*AuthoringEnabled));
            float2 Scale = Material.UvScaleOffset.xy*Material.StratumUv[Index].xy;
            TangentNormal.xy *= Material.RadianceNormal.w*Material.StratumNormal[Index].x*
                                float2(Scale.x < 0 ? -1 : 1,Scale.y < 0 ? -1 : 1);
            TangentNormal = NormalizeDirection(TangentNormal);
            Normal = NormalizeDirection(Tangent*TangentNormal.x+Bitangent*TangentNormal.y+Normal*TangentNormal.z);
        }
        float3 LocalTangent = NormalizeDirection(Tangent-Normal*dot(Normal,Tangent));
        float3 LocalBitangent = cross(Normal,LocalTangent)*(Pixel.ViewTangent.w < 0 ? -1 : 1);
        float Sine;
        float Cosine;
        sincos((Pixel.SubjectRole == 0 ? Material.DirectionalOptics[Index].y : 0),Sine,Cosine);
        Angular[Index].Normal = Normal;
        Angular[Index].Tangent = LocalTangent*Cosine+LocalBitangent*Sine;
        Angular[Index].Bitangent = -LocalTangent*Sine+LocalBitangent*Cosine;
        Angular[Index].Spread = DeriveAnisotropicSpread(Scattering.Strata[Index].AlbedoRoughness.w,Pixel.SubjectRole == 0 ? Material.DirectionalOptics[Index].x : 0);
        Angular[Index].Occlusion = ((Mask & 2) != 0 ? Orm.r : 1)*
            lerp(1,AuthoredOcclusion.r,saturate(AuthoredOcclusion.a)*AuthoringEnabled);
    }
    float3 View = Scattering.Configuration.z != 0 ? NormalizeDirection(-Pixel.ViewPosition) : float3(0,0,1);
    float3 Light = NormalizeDirection(Scattering.LightDirectionIntensity.xyz);
    float3 Radiance = EvaluateMaterialSequence(Scattering,Angular,View,Light,false)*
                      Scattering.LightDirectionIntensity.w*Scattering.LightColourExposure.rgb;
    Radiance += IntegrateMaterialEnvironment(Scattering,Angular,GeometricNormal,View);
    // 📝 The radiance slice belongs to the first stratum (packing keeps exactly one), so it is
    // sampled through that stratum's UV scale/offset like every other channel; raw coordinates
    // would stretch the emitter mask across the whole specimen (the LED blow-out).
    float3 Luminous = MaterialTextures[3].Sample(MaterialSampler,
        float3(Pixel.Coordinates*Material.StratumUv[0].xy+Material.StratumUv[0].zw,0)).rgb;
    if (Pixel.SubjectRole == 0) Radiance += Material.RadianceNormal.rgb*((Selection & 8) != 0 ? Luminous : float3(1));
    return float4(EncodeDisplay(Radiance,Scattering.LightColourExposure.w),1);
}
