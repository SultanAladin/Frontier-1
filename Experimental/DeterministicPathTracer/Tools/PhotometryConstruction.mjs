/*====================================================================================================================================
                                                    PHOTOMETRYCONSTRUCTION.MJS
====================================================================================================================================*/
// 🧩 Writes Assets/Photometry/*.ies: three luminaire distributions in IESNA LM-63-2002 format (Type C photometry)

// Usage:  node Tools/PhotometryConstruction.mjs
// 📝 Authored for this prototype (no manufacturer data is redistributed). The renderer reads them through the same LM-63
//    parser that accepts real manufacturer files (Source/PhotometricProfile.js), so any Type C .ies can replace them.
// 📝 Luminaire frame (PhotometricProfile.js): vertical angle θ is measured from the photometric axis (Type C nadir),
//    horizontal angle φ from the reference direction. For the car lamps the axis is the beam direction and φ = 0 is
//    "down" (perpendicular to the beam); φ = 90° is the lamp's left (axis × reference).

import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

//------------------------------------------------------------------------------------------------------------------------
//                                                    CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

const OutputFolder = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../Assets/Photometry');
const Degree       = Math.PI / 180;


//------------------------------------------------------------------------------------------------------------------------
//                                                    DISTRIBUTIONS
//------------------------------------------------------------------------------------------------------------------------

function SmoothStep(Lower, Upper, Value)
{
    const T = Math.min(Math.max((Value - Lower) / (Upper - Lower), 0), 1);
    return T * T * (3 - 2 * T);
}

// 📝 Car-park downlight: batwing (peak ≈ 55° off nadir) so a 5 m mounting height spreads evenly, cut off by 85°.
function DownlightCandela(Vertical)
{
    if (Vertical >= 85) return 0;
    const Batwing = Math.cos(Vertical * Degree) * (1 + 1.4 * Math.sin(Vertical * Degree) ** 2);
    return 1400 * Batwing * (1 - SmoothStep(70, 85, Vertical));
}

// 📝 Low-beam headlamp: a wide hot spot 1.5° below the axis, a sharp cut-off just above it that rises by 1° toward the
//    kerb side (φ > 0, the lamp's left) — the asymmetric "15° step" of a real low beam, simplified — and a soft spill
//    that reaches the ground in front of the car. Nothing behind the lamp (θ ≥ 90°).
function LowBeamCandela(Vertical, Horizontal)
{
    if (Vertical >= 90) return 0;
    const Elevation = -Vertical * Math.cos(Horizontal * Degree);   // [°] above the axis (φ = 0 points down)
    const Lateral   =  Vertical * Math.sin(Horizontal * Degree);   // [°] toward the lamp's left
    const CutOff    = 0.4 + 1.0 * SmoothStep(0, 15, Lateral);
    const Gate      = 1 - SmoothStep(CutOff - 0.8, CutOff + 0.6, Elevation);
    const HotSpot   = 17000 * Math.exp(-((Lateral / 11) ** 2) - (((Elevation + 1.5) / 2.6) ** 2));
    const Spread    = 3200 * Math.exp(-((Lateral / 32) ** 2) - (((Elevation + 5) / 6) ** 2));
    const Spill     = 260 * Math.cos(Vertical * Degree) ** 2;
    return (HotSpot + Spread) * Gate + Spill * (0.25 + 0.75 * Gate);
}

// 📝 Tail lamp: broad Lambertian-like lobe (stop/tail regulations want visibility over a wide angle).
function TailLampCandela(Vertical)
{
    if (Vertical >= 90) return 0;
    return 25 * Math.cos(Vertical * Degree) ** 1.2;
}


//------------------------------------------------------------------------------------------------------------------------
//                                                    LM-63 WRITER
//------------------------------------------------------------------------------------------------------------------------

function Range(First, Last, Step)
{
    const Values = [];
    for (let Value = First; Value <= Last + 1e-9; Value += Step) Values.push(Number(Value.toFixed(4)));
    return Values;
}

// 📝 Luminous flux from the tabulated candela (trapezoid in θ, rectangle in φ): Φ = ∫∫ I sinθ dθ dφ.
function MeasureFlux(Vertical, Horizontal, Table)
{
    const Full = Horizontal.length === 1 ? [0, 360] : Horizontal;
    let Flux = 0;
    for (let H = 0; H + 1 < Full.length; H++)
    {
        const Row = Table[Math.min(H, Table.length - 1)];
        const Width = (Full[H + 1] - Full[H]) * Degree;
        for (let V = 0; V + 1 < Vertical.length; V++)
        {
            const A = Vertical[V] * Degree, B = Vertical[V + 1] * Degree;
            Flux += Width * 0.5 * (Row[V] + Row[V + 1]) * (Math.cos(A) - Math.cos(B));
        }
    }
    return Flux;
}

function WriteProfile(Name, Keywords, Vertical, Horizontal, Candela)
{
    const Table = Horizontal.map(H => Vertical.map(V => Candela(V, H)));
    const Flux  = MeasureFlux(Vertical, Horizontal, Table);
    const Wrap  = Values => { const Lines = []; for (let I = 0; I < Values.length; I += 10) Lines.push(Values.slice(I, I + 10).join(' ')); return Lines.join('\n'); };
    const Text = [
        'IESNA:LM-63-2002',
        ...Object.entries(Keywords).map(([Key, Value]) => `[${Key}] ${Value}`),
        'TILT=NONE',
        // lamps, lumens/lamp (-1 = absolute photometry), multiplier, #vertical, #horizontal, type C, metres, w × l × h
        `1 -1 1.0 ${Vertical.length} ${Horizontal.length} 1 2 0.10 0.10 0.02`,
        '1.0 1.0 0',
        Wrap(Vertical),
        Wrap(Horizontal),
        ...Table.map(Row => Wrap(Row.map(Value => Value.toFixed(2))))
    ].join('\n') + '\n';
    fs.writeFileSync(path.join(OutputFolder, Name), Text);
    console.log(`${Name}: ${Vertical.length} × ${Horizontal.length} angles, peak ${Math.max(...Table.flat()).toFixed(0)} cd, ${Flux.toFixed(0)} lm`);
}


//------------------------------------------------------------------------------------------------------------------------
//                                                    ENTRY
//------------------------------------------------------------------------------------------------------------------------

fs.mkdirSync(OutputFolder, { recursive: true });
const Common = { TEST: 'Authored distribution (not a measurement)', MANUFAC: 'Frontier DeterministicPathTracer', ISSUEDATE: '2026-11-13' };
WriteProfile('CarParkDownlight.ies', { ...Common, LUMCAT: 'FR-DL-BW', LUMINAIRE: 'Car-park downlight, batwing, 5 m mounting' },
             Range(0, 180, 2.5), [0], DownlightCandela);
WriteProfile('LowBeamHeadlamp.ies', { ...Common, LUMCAT: 'FR-LB-01', LUMINAIRE: 'Low-beam headlamp, asymmetric cut-off (axis = beam, H0 = down)' },
             Range(0, 90, 1), Range(0, 360, 15), LowBeamCandela);
WriteProfile('TailLamp.ies', { ...Common, LUMCAT: 'FR-TL-01', LUMINAIRE: 'Tail lamp, wide lobe (axis = rearward)' },
             Range(0, 90, 5), [0], TailLampCandela);
