/*==========================================================================================================================================
                                                        AUTOMOTIVEPAINT.SLANG
==========================================================================================================================================*/
// 🧩 Object-space, derivative-filtered cellular automotive flakes, buried binder tint and independent orange-peel coating normals.

struct AutomotivePaint
{
    uint4 Configuration;
    float4 Distribution;
    float4 Finish;
    float4 Relief;
    float4 Palette[12];
    float4 AverageTint;
};
float HashPaintCell(float3 Position)
{
    Position = frac(Position * .1031);
    Position += dot(Position, Position.yzx + 33.33);
    return frac((Position.x + Position.y) * Position.z);
}
float EvaluatePaintNoise(float3 Position)
{
    float3 Cell = floor(Position), Blend = frac(Position);
    Blend = Blend * Blend * (3 - 2 * Blend);
    return lerp(lerp(lerp(HashPaintCell(Cell), HashPaintCell(Cell + float3(1,0,0)), Blend.x),
                     lerp(HashPaintCell(Cell + float3(0,1,0)), HashPaintCell(Cell + float3(1,1,0)), Blend.x), Blend.y),
                lerp(lerp(HashPaintCell(Cell + float3(0,0,1)), HashPaintCell(Cell + float3(1,0,1)), Blend.x),
                     lerp(HashPaintCell(Cell + float3(0,1,1)), HashPaintCell(Cell + float3(1,1,1)), Blend.x), Blend.y), Blend.z);
}
float4 EvaluatePaintCell(float3 Position, float Seed)
{
    float3 Cell = floor(Position), Local = frac(Position), Nearest = 0;
    float First = 1e8, Second = 1e8;
    for (int Z = -1; Z <= 1; ++Z)
        for (int Y = -1; Y <= 1; ++Y)
            for (int X = -1; X <= 1; ++X)
            {
                float3 Offset = float3(X,Y,Z), Id = Cell + Offset;
                float3 Site = .08 + .84 * float3(HashPaintCell(Id + Seed), HashPaintCell(Id + Seed + 17.31), HashPaintCell(Id + Seed + 43.97));
                float3 Delta = Offset + Site - Local;
                float Distance = dot(Delta, Delta);
                if (Distance < First) { Second = First; First = Distance; Nearest = Id; }
                else if (Distance < Second) Second = Distance;
            }
    return float4(max(0, sqrt(Second) - sqrt(First)), HashPaintCell(Nearest + Seed + 61.7),
                  HashPaintCell(Nearest + Seed + 127.3), HashPaintCell(Nearest + Seed + 223.1));
}
float3 SelectPaintTint(AutomotivePaint Paint, float Position)
{
    if (Paint.Configuration.z == 2) return Paint.Palette[0].rgb;
    if (Paint.Configuration.z == 0) return Paint.Palette[min(Paint.Configuration.w - 1, uint(Position * Paint.Configuration.w))].rgb;
    float3 Spectrum = Paint.Palette[0].rgb;
    for (uint Index = 1; Index < Paint.Configuration.w; ++Index)
        Spectrum = lerp(Spectrum, Paint.Palette[Index].rgb,
            saturate((Position - Paint.Palette[Index - 1].w) / max(.0001, Paint.Palette[Index].w - Paint.Palette[Index - 1].w)));
    return Spectrum;
}
struct PaintSurface
{
    float3 Albedo;
    float Roughness;
    float Metallic;
    float Coverage;
    float Random;
    float Resolved;
    float PeelHeight;
};
PaintSurface EvaluatePaintSurface(AutomotivePaint Paint, float3 Position, float3 Albedo, float Roughness, float Metallic
#ifdef FRONTIER_COMPUTE_PAINT
    , float PixelFootprint
#endif
)
{
    PaintSurface Surface;
    float3 Base = Albedo * exp(-Paint.Distribution.w * .28), Blended = Base;
    float Mask = 0, Random = 0;
    float Frequency = min(1e7, 52000 / max(.001, Paint.Distribution.y) * Paint.Distribution.z);
    float3 Domain = Position * Frequency;
#ifdef FRONTIER_COMPUTE_PAINT
    float Footprint = PixelFootprint * Frequency;
#else
    float Footprint = max(length(ddx(Domain)), length(ddy(Domain)));
#endif
    float Detail = 1 - smoothstep(.55, 1.6, Footprint);
    // 📝 Keep derivative evaluation in uniform flow; quad neighbours must not depend on a particle's occupancy.
    float3 Warp = float3(EvaluatePaintNoise(Domain * .31), EvaluatePaintNoise(Domain * .31 + 11.7), EvaluatePaintNoise(Domain * .31 + 37.4)) - .5;
    for (uint Layer = 0; Layer < Paint.Configuration.y; ++Layer)
    {
        float Level = float(Layer), Depth = float(Paint.Configuration.y - 1 - Layer) / max(float(Paint.Configuration.y - 1), 1);
        float3 Point = Domain * (.71 + Level * .37) + Warp * .7 + float3(Level * 27.17, Level * 51.73, Level * 19.43);
        // 📝 GLSL's column-major constructor is transposed explicitly for Slang's mul(matrix, vector).
        Point = mul(float3x3(.80,-.60,0, .36,.48,.80, -.48,-.64,.60), Point);
        float4 Cell = EvaluatePaintCell(Point, Level * 97.3 + 4.1);
#ifdef FRONTIER_COMPUTE_PAINT
        float Antialias = max(Footprint * .65, .009);
#else
        float Antialias = max(fwidth(Cell.x) * .65, .009);
#endif
        float Inset = .018 + Cell.w * .07;
        float Opacity = smoothstep(Inset - Antialias, Inset + Antialias, Cell.x) * step(1 - Paint.Distribution.x * .55, Cell.y);
        float3 Tint = lerp(SelectPaintTint(Paint, Cell.z), Base, Depth * Paint.Relief.y * .72);
        Blended = lerp(Blended, Tint, Opacity);
        Random = lerp(Random, Cell.w, Opacity);
        Mask = Opacity + Mask * (1 - Opacity);
    }
    float MeanMask = 1 - pow(1 - Paint.Distribution.x * .31, float(Paint.Configuration.y));
    Surface.Albedo = lerp(lerp(Base, lerp(Paint.AverageTint.rgb, Base, Paint.Relief.y * .26), MeanMask), Blended, Detail);
    Surface.Coverage = lerp(MeanMask, Mask, Detail);
    Surface.Random = lerp(.5, Random, Detail);
    Surface.Resolved = Detail;
    Surface.Roughness = clamp(lerp(Roughness, lerp(Paint.Finish.x, Paint.Finish.y, Surface.Random), Surface.Coverage), .045, 1);
    Surface.Metallic = lerp(Metallic, lerp(Paint.Finish.z, Paint.Finish.w, Surface.Random), Surface.Coverage);
    Surface.PeelHeight = (EvaluatePaintNoise(Position * Paint.Relief.w) + .35 * EvaluatePaintNoise(Position * Paint.Relief.w * 2.1)) * Paint.Relief.z * .003;
    return Surface;
}
#ifndef FRONTIER_COMPUTE_PAINT
float3 DerivePaintRelief(float Height, float3 Normal, float3 Position)
{
    float3 First = ddx(Position), Second = ddy(Position);
    float3 FirstReciprocal = cross(Second, Normal), SecondReciprocal = cross(Normal, First);
    float Determinant = dot(First, FirstReciprocal);
    float3 Gradient = sign(Determinant) * (ddx(Height) * FirstReciprocal + ddy(Height) * SecondReciprocal);
    return normalize(max(abs(Determinant), 1e-10) * Normal - Gradient);
}
float3 DerivePaintNormal(AutomotivePaint Paint, PaintSurface Surface, float3 Normal, float3 Position, bool Coating)
{
    Normal = DerivePaintRelief(Surface.PeelHeight * (Coating ? 1 : .2), Normal, Position);
    if (!Coating)
    {
        float3 Tangent = normalize(cross(Normal, abs(Normal.y) < .95 ? float3(0,1,0) : float3(1,0,0)));
        float3 Bitangent = cross(Normal, Tangent);
        float Angle = Surface.Random * 6.283;
        Normal = normalize(Normal + (Tangent * cos(Angle) + Bitangent * sin(Angle)) * Paint.Relief.x * Surface.Coverage * Surface.Resolved);
    }
    return Normal;
}

#endif
