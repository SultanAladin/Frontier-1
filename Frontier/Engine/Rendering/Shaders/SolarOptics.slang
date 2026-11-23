/*==========================================================================================================================================
                                                        SOLAROPTICS.SLANG
==========================================================================================================================================*/
// 🧩 Opt-in finite solar source, opaque visibility and simultaneous parametric flare layers.
// 📝 The point-spread function and flare strengths are unmeasured; clouds are a scalar approximation.
static const uint SolarSampleCount = SolarSourceSampleCount;
static const float SolarPi = 3.14159265358979323846;
groupshared float3 SolarVisibilityReduction[64];
float4 SolarPayload(uint Entry)
{
    uint Offset = Extent.Primary.y + 48u + 4u * Entry;
    return float4(Angles[Offset], Angles[Offset + 1u], Angles[Offset + 2u], Angles[Offset + 3u]);
}
uint SolarVisibilityStart() { return Extent.Dimensions.x * 24u + 13889u; }
void SolarBasis(float3 Sun, float3 CameraRight, float3 CameraUp, out float3 Right, out float3 Up)
{
    Right = Unit(CameraRight - Sun * dot(CameraRight, Sun));
    Up = Unit(CameraUp - Sun * dot(CameraUp, Sun) - Right * dot(CameraUp, Right));
}
float3 SolarRayTransmission(float3 Direction)
{
    float Horizon = SolarPayload(7).x;
    float Transmission = 1;
    if (Horizon > -1)
    {
        float Separation = Direction.z - Horizon;
        if (Separation < -1e-7) return 0;
        if (abs(Separation) <= 1e-7) Transmission = .5;
    }
    return OccludedGeometry(CameraProjection(0).xyz, Direction, 1e30) ? float3(0) : float3(Transmission);
}
[shader("compute")][numthreads(64, 1, 1)]
void ResolveSolarVisibility(uint3 Thread : SV_GroupThreadID)
{
    uint Lane = Thread.x;
    float4 Source = SolarPayload(0);
    float3 Sun = Unit(Source.xyz), Right, Up;
    SolarBasis(Sun, CameraProjection(2).xyz, CameraProjection(3).xyz, Right, Up);
    float Facing = dot(Sun, CameraProjection(1).xyz);
    float3 Accumulated = 0;
    for (uint Index = Lane; Index < SolarSampleCount; Index += 64u)
    {
        float4 Sample = SolarPayload(8u + Index);
        float3 Direction = Unit(Sun + Right * Sample.x + Up * Sample.y);
        float3 Transmission = Facing > 1e-6 ? SolarRayTransmission(Direction) : float3(0);
        OutputColour[SolarVisibilityStart() + Index] = float4(Transmission, 0);
        Accumulated += Sample.z * Transmission;
    }
    SolarVisibilityReduction[Lane] = Accumulated;
    GroupMemoryBarrierWithGroupSync();
    if (Lane == 0)
    {
        float3 Total = 0;
        for (uint Index = 0; Index < 64u; ++Index) Total += SolarVisibilityReduction[Index];
        Counters[20] = asuint(Total.x); Counters[21] = asuint(Total.y); Counters[22] = asuint(Total.z);
    }
}
// Abramowitz–Stegun 7.1.26; approximately 1.5e-7 max absolute error in exact arithmetic.
float SolarErf(float Argument)
{
    float Variable = 1 / (1 + .3275911 * abs(Argument));
    float Polynomial = (((((1.061405429 * Variable - 1.453152027) * Variable + 1.421413741) * Variable
                          - .284496736) * Variable + .254829592) * Variable);
    float Result = 1 - Polynomial * exp(-Argument * Argument);
    return Argument < 0 ? -Result : Result;
}
float SolarResolvedRadiance(float Angle, float4 Source, float4 Disc, float Sigma)
{
    float RadiusRatio = sin(Angle) / sin(Source.w);
    float Mu = RadiusRatio < 1 ? sqrt(max(0, 1 - RadiusRatio * RadiusRatio)) : 0;
    float Limb = 1 - Disc.x * (1 - Mu) - Disc.y * (1 - Mu) * (1 - Mu);
    return Limb * .5 * (1 - SolarErf((Angle - Source.w) / (1.4142135623730951 * Sigma)));
}
float3 SolarDiscPixel(float2 Delta, float2 PixelSize, float3 Sun, float3 Right, float3 Up)
{
    float4 Source = SolarPayload(0), Beam = SolarPayload(1), Disc = SolarPayload(2);
    if (Disc.w == 0) return 0;
    float RadiusPixels = Source.w / max(PixelSize.x, PixelSize.y);
    float Blend = saturate((6 - RadiusPixels) / 3);
    float SmallWeight = Blend * Blend * (3 - 2 * Blend);
    float Sigma = max(Beam.w, 1e-9), SmallSigma = max(Sigma, .20 * min(PixelSize.x, PixelSize.y));
    if (length(Delta) > Source.w + .5 * length(PixelSize) + 6 * max(SmallSigma, Sigma)) return 0;
    float PixelArea = PixelSize.x * PixelSize.y;
    float3 Scalar = 0;
    if (SmallWeight > 0)
    {
        float Scale = 1 / (1.4142135623730951 * SmallSigma);
        float3 Mass = 0;
        for (uint Index = 0; Index < SolarSampleCount; ++Index)
        {
            float4 Sample = SolarPayload(8u + Index);
            float2 Low = (Delta - .5 * PixelSize - Sample.xy) * Scale;
            float2 High = (Delta + .5 * PixelSize - Sample.xy) * Scale;
            float XMass = max(0, .5 * (SolarErf(High.x) - SolarErf(Low.x)));
            float YMass = max(0, .5 * (SolarErf(High.y) - SolarErf(Low.y)));
            Mass += Sample.z * XMass * YMass * OutputColour[SolarVisibilityStart() + Index].xyz;
        }
        Scalar += SmallWeight * Mass / PixelArea;
    }
    if (SmallWeight < 1)
    {
        float3 Average = 0;
        for (uint Y = 0; Y < 4u; ++Y)
            for (uint X = 0; X < 4u; ++X)
            {
                float2 Position = Delta + ((float2(X, Y) + .5) / 4 - .5) * PixelSize;
                float3 Direction = Unit(Sun + Right * Position.x + Up * Position.y);
                Average += SolarResolvedRadiance(length(Position), Source, Disc, Sigma) * SolarRayTransmission(Direction);
            }
        float SolidAngle = 4 * SolarPi * sin(.5 * Source.w) * sin(.5 * Source.w);
        float Mean = 1 - Disc.x / 3 - Disc.y / 6;
        Scalar += (1 - SmallWeight) * Average * Disc.z / (16 * SolidAngle * Mean);
    }
    return Beam.xyz * Scalar * SolarPayload(6).w;
}
float SolarPolygonEdge(float Angle, float Radius, float Blades, float Rotation)
{
    float Period = 2 * SolarPi / max(Blades, 3);
    float Local = Angle - Rotation - Period * floor((Angle - Rotation) / Period + .5);
    return Radius * cos(SolarPi / max(Blades, 3)) / cos(Local);
}
float3 SolarLensFlare(float2 Pixel, float2 Sun, float3 Flux)
{
    float4 Shape = SolarPayload(3), Reach = SolarPayload(4), Lines = SolarPayload(5), Layers = SolarPayload(6);
    float3 Result = 0;
    static const float3 GhostGeometry[6] = {
        float3(.25,.06,.50), float3(.55,.13,.30), float3(.90,.04,.65),
        float3(1.25,.20,.18), float3(1.70,.08,.40), float3(2.30,.30,.10)};
    static const float4 GhostAppearance[6] = {
        float4(1,.74,.48,0), float4(.46,.96,.62,.82), float4(.56,.66,1,0),
        float4(1,.52,.58,0), float4(.86,.96,.52,.78), float4(.52,.90,1,0)};
    if (Shape.z > 0)
        for (uint Index = 0; Index < 6u; ++Index)
        {
            float3 Geometry = GhostGeometry[Index]; float4 Appearance = GhostAppearance[Index];
            float2 Offset = Pixel + Sun * Geometry.x; float Radius = length(Offset);
            if (Radius > Geometry.y * 1.15) continue;
            float Angle = atan2(Offset.y, Offset.x);
            float3 ChannelScale = float3(1.012, 1, .988), Form = 0;
            for (uint Channel = 0; Channel < 3u; ++Channel)
            {
                float Edge = SolarPolygonEdge(Angle, Geometry.y * ChannelScale[Channel], Shape.x, Shape.y);
                float Interior = saturate((Edge - Radius) / (.10 * Geometry.y));
                Interior = Interior * Interior * (3 - 2 * Interior);
                if (Appearance.w > 0)
                {
                    float D = (Radius - Appearance.w * Edge) / (.10 * Geometry.y);
                    Form[Channel] = exp(-D * D) + .14 * Interior;
                }
                else
                {
                    float Rim = Radius / Geometry.y;
                    Form[Channel] = Interior * (.55 + .45 * Rim * Rim);
                }
            }
            Result += .16 * Shape.z * Geometry.z * Appearance.xyz * Flux * Form;
        }
    float2 Offset = Pixel - Sun; float Radius = length(Offset);
    if (Shape.w > 0 && Reach.x > 0)
    {
        uint Blades = uint(Shape.x), Spikes = Blades % 2u == 0u ? Blades : 2u * Blades;
        float Angle = atan2(Offset.y, Offset.x) - Shape.y;
        float Radial = pow(1 / (1 + Radius / (.012 + .05 * Reach.x)), 1.7) * exp(-Radius / Reach.x);
        float3 Angular = 0;
        for (uint Spike = 0; Spike < Spikes; ++Spike)
        {
            float D = Angle - 2 * SolarPi * float(Spike) / float(Spikes);
            D -= 2 * SolarPi * floor(D / (2 * SolarPi) + .5);
            float3 Scaled = D / (.020 * float3(1.10, 1, .92));
            Angular += exp(-Scaled * Scaled);
        }
        Result += .85 * Shape.w * Flux * Angular * Radial;
    }
    if (Reach.y > 0 && Reach.w > 0 && Lines.x > 0)
    {
        float Along = Offset.x * cos(Reach.z) + Offset.y * sin(Reach.z);
        float Across = -Offset.x * sin(Reach.z) + Offset.y * cos(Reach.z);
        float Wide = exp(-pow(Across / Lines.x, 2)) * exp(-abs(Along) / Reach.w);
        float Core = exp(-pow(Across / (.35 * Lines.x), 2)) * exp(-abs(Along) / (.42 * Reach.w));
        Result += Flux * float3(.34, .54, 1) * .55 * Reach.y * (Wide + .85 * Core);
    }
    if (Lines.y > 0)
    {
        float Spread = clamp(Lines.w, .25, 2);
        float Along = .5 * (Offset.x * cos(Lines.z) + Offset.y * sin(Lines.z));
        float Across = .5 * (-Offset.x * sin(Lines.z) + Offset.y * cos(Lines.z));
        float Warm = .65 * Lines.y * exp(-(160 * abs(Across) + 3 * abs(Along)) / Spread);
        Result += Flux * float3(1, .8, .55) * Warm;
    }
    float Glow = Layers.x * (.50 * exp(-pow(Radius / .035, 2)) + .025 * exp(-pow(Radius / .30, 2)));
    float Veil = Layers.z * (.004 + .008 * exp(-pow(Radius / 1.2, 2)));
    Result += Flux * (Glow + Veil);
    float SunDistance = length(Sun);
    if (Layers.y > 0 && SunDistance > 1e-6)
    {
        float HaloRadius = length(Pixel + Sun);
        float3 D = (HaloRadius - SunDistance - float3(.004, 0, -.004)) / .030;
        Result += Flux * exp(-D * D) * (.035 * Layers.y);
    }
    return Result;
}
[shader("compute")][numthreads(64, 1, 1)]
void CompositeSolarOptics(uint3 Dispatch : SV_DispatchThreadID)
{
    uint Pixel = Dispatch.x;
    if (Pixel >= Extent.Dimensions.x) return;
    float4 Source = SolarPayload(0), Beam = SolarPayload(1);
    float3 Sun = Unit(Source.xyz);
    float4 Forward = CameraProjection(1), CameraRight = CameraProjection(2), CameraUp = CameraProjection(3);
    float SunDepth = dot(Sun, Forward.xyz);
    if (SunDepth <= 1e-6 || CameraRight.w <= 0 || CameraUp.w <= 0) return;
    float3 Right, Up;
    SolarBasis(Sun, CameraRight.xyz, CameraUp.xyz, Right, Up);
    float SunU = dot(Sun, CameraRight.xyz) / SunDepth, SunV = dot(Sun, CameraUp.xyz) / SunDepth;
    float ScaleX = SunDepth * dot(CameraRight.xyz, Right), ScaleY = SunDepth * dot(CameraUp.xyz, Up);
    float Shear = SunDepth * dot(CameraUp.xyz, Right);
    float2 PixelSize = float2(2 * CameraRight.w * ScaleX / Extent.History.x,
                              2 * CameraUp.w * ScaleY / Extent.History.y);
    if (any(PixelSize <= 1e-12)) return;
    uint2 Coordinate = uint2(Pixel % Extent.History.x, Pixel / Extent.History.x);
    float2 Ndc = (float2(Coordinate) + .5) / float2(Extent.History.xy) * 2 - 1;
    Ndc.y = -Ndc.y;
    float2 Projection = Ndc * float2(CameraRight.w, CameraUp.w) - float2(SunU, SunV);
    float2 Delta = float2(Projection.x * ScaleX + Projection.y * Shear, Projection.y * ScaleY);
    float3 Disc = SolarDiscPixel(Delta, PixelSize, Sun, Right, Up);
    float2 Screen = float2((2 * (float(Coordinate.x) + .5) - Extent.History.x) / Extent.History.y,
                           (Extent.History.y - 2 * (float(Coordinate.y) + .5)) / Extent.History.y);
    float2 ScreenSun = float2(SunU, SunV) / CameraUp.w;
    float3 Visibility = float3(asfloat(Counters[20]), asfloat(Counters[21]), asfloat(Counters[22]));
    float3 Flare = SolarLensFlare(Screen, ScreenSun, Beam.xyz * Visibility * SolarPayload(6).w);
    float4 Previous = OutputColour[Pixel];
    OutputColour[Pixel] = float4(Previous.xyz + Disc + Flare, Previous.w);
}
