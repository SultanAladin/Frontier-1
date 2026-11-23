/*============================================================================================================================================
                                                     PATTERNSURFACECHANNELS.JS
============================================================================================================================================*/
// 🧩 Explicit surface contributions and repeat-aware height-derived normal export.

export const PatternChannelCaptions = { Color: "Colour", Height: "Height", Normal: "Normal", Roughness: "Roughness", Metalness: "Metalness", Opacity: "Opacity", Occlusion: "Ambient occlusion" };
export function NormalizePatternChannels(Input)
{
    if (!Input || typeof Input !== "object") return undefined;
    const Channels = {};
    for (const Caption of Object.keys(PatternChannelCaptions))
    {
        if (!Input[Caption] || typeof Input[Caption] !== "object") continue;
        const Channel = Input[Caption], Strength = Number(Channel.Strength);
        Channels[Caption] = { Enabled: Channel.Enabled !== false, Strength: Number.isFinite(Strength) ? Math.max(0, Math.min(Caption === "Normal" ? 4 : 1, Strength)) : 1 };
        if (Caption === "Normal" && Channel.Source)
        {
            const Source = String(Channel.Source);
            if (!/^data:image\/(png|jpeg|webp);base64,[a-z\d+/=\s]+$/i.test(Source) || Source.length > 2000000) throw new Error("Normal maps must be embedded PNG, JPEG or WebP under 1.5 MB.");
            Channels[Caption].Source = Source;
        }
    }
    return Channels;
}
export const PatternChannelEnabled = (Layer, Caption) => Caption === "Occlusion" ? Layer.SurfaceChannels?.[Caption]?.Enabled === true : Layer.SurfaceChannels?.[Caption]?.Enabled !== false;
export const PatternChannelStrength = (Layer, Caption) => Layer.SurfaceChannels?.[Caption]?.Strength ?? 1;
export function DerivePatternNormalPixels(HeightPixels, Width, Height, Strength = 1, RepeatX = true, RepeatY = true)
{
    const Pixels = new Uint8ClampedArray(HeightPixels.length);
    const Sample = (X, Y) =>
    {
        X = RepeatX ? (X + Width) % Width : Math.max(0, Math.min(Width - 1, X));
        Y = RepeatY ? (Y + Height) % Height : Math.max(0, Math.min(Height - 1, Y));
        const Offset = (Y * Width + X) * 4, Alpha = HeightPixels[Offset + 3] / 255;
        return (HeightPixels[Offset] - 128) * Alpha / 255;
    };
    for (let Y = 0; Y < Height; ++Y) for (let X = 0; X < Width; ++X)
    {
        const Δx = (Sample(X - 1, Y) - Sample(X + 1, Y)) * Strength * Width / 64;
        const Δy = (Sample(X, Y + 1) - Sample(X, Y - 1)) * Strength * Height / 64;
        const Length = Math.hypot(Δx, Δy, 1), Offset = (Y * Width + X) * 4;
        Pixels[Offset] = (Δx / Length * .5 + .5) * 255; Pixels[Offset + 1] = (Δy / Length * .5 + .5) * 255;
        Pixels[Offset + 2] = (1 / Length * .5 + .5) * 255; Pixels[Offset + 3] = 255;
    }
    return Pixels;
}
