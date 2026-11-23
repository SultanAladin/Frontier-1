/*==========================================================================================================================================
                                                        MATERIALRAYSCATTERING.SLANG
==========================================================================================================================================*/
// 🧩 Reuse authored scattering strata and cellular paint with explicit ray footprints and bounded baked-map pyramids.

#include "../../../../Engine/Render/Shader/ScatteringEvaluation.slang"
#define FRONTIER_COMPUTE_PAINT 1
#include "../../../../Engine/Render/Shader/AutomotivePaint.slang"

//------------------------------------------------------------------------------------------------------------------------
//                                                   FLOAT-ATLAS MATERIAL ACCESS
//------------------------------------------------------------------------------------------------------------------------

float4 MaterialCoefficients(uint Offset)
{
    return float4(Angles[Offset], Angles[Offset + 1], Angles[Offset + 2], Angles[Offset + 3]);
}
uint MaterialAddress(SurfacePoint R) { return R.ExpectedDirect.w > 0 ? uint(R.ExpectedDirect.w) : 0; }
uint MaterialTransport(SurfacePoint R)
{
    uint Address = MaterialAddress(R);
    return Address > 0 ? uint(Angles[Address]) : 0;
}
PreviewScattering ReadMaterialScattering(SurfacePoint R)
{
    uint Offset = MaterialAddress(R) - 1;
    PreviewScattering S = (PreviewScattering)0;
    S.Configuration.x = uint(Angles[Offset]);
    for (uint I = 0; I < S.Configuration.x; ++I)
    {
        uint P = Offset + 8 + I * 24;
        S.Strata[I].AlbedoRoughness = MaterialCoefficients(P);
        S.Strata[I].Optics = MaterialCoefficients(P + 4);
        S.Strata[I].AbsorptionThickness = MaterialCoefficients(P + 8);
        S.Strata[I].Classification = uint4(MaterialCoefficients(P + 12));
        S.Strata[I].Sheen = MaterialCoefficients(P + 16);
        S.Strata[I].SheenFinish = MaterialCoefficients(P + 20);
    }
    S.Strata[0].AlbedoRoughness = float4(R.AlbedoIor.xyz, R.NormalRoughness.w);
    S.Strata[0].Optics.x = R.ViewMetallic.w;
    if (MaterialTransport(R) == 2)
    {
        S.Strata[0].AlbedoRoughness.xyz = .04;
        S.Strata[0].Optics.x = 1;
    }
    return S;
}
float3 MaterialTexel(uint Offset, uint Size, int2 P)
{
    uint2 Q = uint2((P % int(Size) + int(Size)) % int(Size));
    uint At = Offset + (Q.y * Size + Q.x) * 3;
    return float3(Angles[At], Angles[At + 1], Angles[At + 2]);
}
float3 SampleMaterialPyramid(uint Offset, float2 Uv, float Footprint)
{
    uint Size = uint(Angles[Offset]);
    Offset += 4;
    while (Size > 1 && Footprint * Size > 1)
    {
        Offset += Size * Size * 3;
        Size /= 2;
    }
    float2 P = frac(Uv) * Size - .5;
    int2 Low = int2(floor(P));
    float2 Blend = frac(P);
    return lerp(lerp(MaterialTexel(Offset, Size, Low), MaterialTexel(Offset, Size, Low + int2(1, 0)), Blend.x),
                lerp(MaterialTexel(Offset, Size, Low + int2(0, 1)), MaterialTexel(Offset, Size, Low + int2(1, 1)), Blend.x), Blend.y);
}
void ApplyRayMaterial(inout SurfacePoint R, FacetCorners F, float2 Barycentric)
{
    if (F.Emissive.w <= 0) return;
    uint Offset = uint(F.Emissive.w) - 1;
    R.ExpectedDirect.w = F.Emissive.w;
    R.ExpectedDirect.xyz = 0;
    float4 Surface = MaterialCoefficients(Offset + 8);
    float4 Optics = MaterialCoefficients(Offset + 12);
    R.AlbedoIor = float4(Surface.xyz, Optics.y);
    R.NormalRoughness.w = Surface.w;
    R.ViewMetallic.w = Optics.x;
    float3 Weights = float3(1 - Barycentric.x - Barycentric.y, Barycentric);
    float2 Uv = float2(dot(float3(F.A.w, F.B.w, F.C.w), Weights),
                       dot(float3(F.NormalA.w, F.NormalB.w, F.NormalC.w), Weights));
    float4 Mapping = MaterialCoefficients(Offset + 4);
    Uv = Uv * Mapping.xy + Mapping.zw;
    float3 Eye = CameraProjection(0).xyz;
    float Footprint = 2 * length(R.PositionHit.xyz - Eye) * CameraProjection(3).w /
        (Extent.History.y * max(.05, dot(R.NormalRoughness.xyz, R.ViewMetallic.xyz)));
    uint Albedo = uint(Angles[Offset + 2]), Normal = uint(Angles[Offset + 3]);
    float TexelFootprint = Footprint * max(abs(Mapping.x), abs(Mapping.y));
    if (Albedo > 0) R.AlbedoIor.xyz *= SampleMaterialPyramid(Albedo, Uv, TexelFootprint);
    float3 N = R.NormalRoughness.xyz;
    float3 T = Unit(cross(abs(N.z) < .95 ? float3(0, 0, 1) : float3(0, 1, 0), N));
    if (Normal > 0)
    {
        float3 M = Unit(SampleMaterialPyramid(Normal, Uv, TexelFootprint));
        float2 Ua = float2(F.A.w, F.NormalA.w) * Mapping.xy;
        float2 Ub = float2(F.B.w, F.NormalB.w) * Mapping.xy - Ua;
        float2 Uc = float2(F.C.w, F.NormalC.w) * Mapping.xy - Ua;
        float D = Ub.x * Uc.y - Ub.y * Uc.x;
        float3 E = F.B.xyz - F.A.xyz, G = F.C.xyz - F.A.xyz;
        float3 Bitangent = cross(N, T);
        if (abs(D) > 1e-10)
        {
            T = Unit((E * Uc.y - G * Ub.y) / D);
            T = Unit(T - N * dot(N, T));
            float3 AlongV = (G * Ub.x - E * Uc.x) / D;
            Bitangent = cross(N, T) * (dot(cross(N, T), AlongV) < 0 ? -1 : 1);
        }
        R.NormalRoughness.xyz = Unit(T * M.x + Bitangent * M.y + N * max(.1, M.z));
    }
    uint P = Offset + 200;
    if (Angles[P] > 0)
    {
        AutomotivePaint Paint = (AutomotivePaint)0;
        Paint.Configuration = uint4(MaterialCoefficients(P));
        Paint.Distribution = MaterialCoefficients(P + 4);
        Paint.Finish = MaterialCoefficients(P + 8);
        Paint.Relief = MaterialCoefficients(P + 12);
        for (uint I = 0; I < 12; ++I) Paint.Palette[I] = MaterialCoefficients(P + 16 + I * 4);
        Paint.AverageTint = MaterialCoefficients(P + 64);
        PaintSurface Flakes = EvaluatePaintSurface(Paint, R.PositionHit.xyz, R.AlbedoIor.xyz,
                                                   R.NormalRoughness.w, R.ViewMetallic.w, Footprint);
        R.AlbedoIor.xyz = Flakes.Albedo;
        R.NormalRoughness.w = Flakes.Roughness;
        R.ViewMetallic.w = Flakes.Metallic;
        float Angle = Flakes.Random * 6.283;
        R.NormalRoughness.xyz = Unit(N + (T * cos(Angle) + cross(N, T) * sin(Angle)) *
            Paint.Relief.x * Flakes.Coverage * Flakes.Resolved);
    }
}
