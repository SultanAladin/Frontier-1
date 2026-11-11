/*============================================================================================================================================
                                                            ARTWORKNAMES.JS
============================================================================================================================================*/
// 🧩 Stable, collision-free artwork captions without changing document order or geometry.

export function NormalizeArtworkNames(Layers)
{
    const Captions = Layers.map(Layer => String(Layer.name || "Artwork").trim() || "Artwork");
    const Populations = new Map();
    for (const Caption of Captions) Populations.set(Caption.toLowerCase(), (Populations.get(Caption.toLowerCase()) || 0) + 1);
    const Reserved = new Set(Captions.filter(Caption => Populations.get(Caption.toLowerCase()) === 1).map(Caption => Caption.toLowerCase()));
    return Layers.map((Layer, Index) =>
    {
        const Caption = Captions[Index];
        if (Populations.get(Caption.toLowerCase()) === 1) return Layer.name === Caption ? Layer : { ...Layer, name: Caption };
        const Stem = Caption.slice(0, 52);
        let Ordinal = 1, Candidate;
        do { Candidate = `${Stem} ${String(Ordinal++).padStart(2, "0")}`; } while (Reserved.has(Candidate.toLowerCase()));
        Reserved.add(Candidate.toLowerCase());
        return { ...Layer, name: Candidate };
    });
}
