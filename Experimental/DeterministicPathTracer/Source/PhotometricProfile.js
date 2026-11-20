/*====================================================================================================================================
                                                      PHOTOMETRICPROFILE.JS
====================================================================================================================================*/
// 🧩 IESNA LM-63 (.ies) parser and the fixed angular table the shader samples (Type C photometry)

//------------------------------------------------------------------------------------------------------------------------
//                                                    CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 Every profile is resampled onto the same grid: θ ∈ [0, π] in ProfileVerticalSamples steps (endpoints included),
//    φ ∈ [0, 2π) in ProfileHorizontalSamples steps (wrapping). RadianceIntegration.wgsl EvaluateProfile reads it bilinearly.
export const ProfileVerticalSamples   = 128;
export const ProfileHorizontalSamples = 32;
export const ProfileFloatStride       = ProfileVerticalSamples * ProfileHorizontalSamples;

const PhotometricTypeC = 1;


//------------------------------------------------------------------------------------------------------------------------
//                                                    PARSING
//------------------------------------------------------------------------------------------------------------------------

// 📝 LM-63-1995/2002 layout: header lines, keyword lines "[KEY] value", "TILT=…", then whitespace-separated numbers
//    (line breaks carry no meaning): lamps, lumens/lamp, multiplier, #V, #H, photometric type, units, width, length,
//    height, ballast factor, future use / ballast-lamp factor, input watts, V angles, H angles, #H rows of #V candela.
export function ParseIesProfile(Text, Label = 'profile')
{
    const Lines = Text.replace(/\r/g, '').split('\n');
    const TiltLine = Lines.findIndex(Line => /^\s*TILT\s*=/i.test(Line));
    if (TiltLine < 0) throw new Error(`${Label}: no TILT line (not an LM-63 file)`);
    const Keywords = {};
    for (const Line of Lines.slice(0, TiltLine))
    {
        const Match = /^\s*\[([^\]]+)\]\s*(.*)$/.exec(Line);
        if (Match) Keywords[Match[1].trim().toUpperCase()] = Match[2].trim();
    }
    const Tokens = Lines.slice(TiltLine + 1).join(' ').trim().split(/[\s,]+/).filter(Boolean).map(Number);
    let Cursor = 0;
    const Take = Count => { const Values = Tokens.slice(Cursor, Cursor + Count); Cursor += Count; if (Values.length < Count || Values.some(Number.isNaN)) throw new Error(`${Label}: truncated or malformed numeric block`); return Values; };

    // 📝 TILT=INCLUDE carries a lamp-tilt table (geometry, count, angles, factors) before the photometric block; the
    //    tilt factors only apply to lamps operated off-vertical and are skipped here.
    const Tilt = Lines[TiltLine].split('=')[1].trim().toUpperCase();
    if (Tilt === 'INCLUDE') { Take(1); const PairCount = Take(1)[0]; Take(PairCount * 2); }
    else if (Tilt !== 'NONE') throw new Error(`${Label}: TILT file references are not supported`);

    const [, , Multiplier, VerticalCount, HorizontalCount, PhotometricType] = Take(10);
    const [BallastFactor, LampFactor] = Take(3);
    if (PhotometricType !== PhotometricTypeC) throw new Error(`${Label}: photometric type ${PhotometricType} (only Type C is supported)`);
    const Vertical   = Take(VerticalCount);
    const Horizontal = Take(HorizontalCount);
    const Scale = Multiplier * (BallastFactor || 1) * (LampFactor || 1);
    const Candela = [];
    for (let Row = 0; Row < HorizontalCount; Row++) Candela.push(Take(VerticalCount).map(Value => Math.max(Value * Scale, 0)));
    return { Label, Keywords, Vertical, Horizontal, Candela };
}


//------------------------------------------------------------------------------------------------------------------------
//                                                    EVALUATION
//------------------------------------------------------------------------------------------------------------------------

// 📝 Maps any φ ∈ [0°, 360°) onto the tabulated horizontal range using the file's symmetry (LM-63 conventions):
//    one angle = rotationally symmetric, last 90 = quadrant, last 180 = bilateral about 0–180, first 90 / last 270 =
//    bilateral about 90–270, otherwise a full 0–360 table.
function FoldHorizontal(Profile, Angle)
{
    const H = Profile.Horizontal, First = H[0], Last = H[H.length - 1];
    if (H.length === 1) return First;
    if (First === 0 && Last === 90)  { const A = Angle % 180; return A > 90 ? 180 - A : A; }
    if (First === 0 && Last === 180) return Angle > 180 ? 360 - Angle : Angle;
    if (First === 90 && Last === 270) return Angle < 90 ? 180 - Angle : (Angle > 270 ? 540 - Angle : Angle);
    return Angle;
}

function Bracket(Angles, Value)
{
    if (Value <= Angles[0]) return [0, 0, 0];
    if (Value >= Angles[Angles.length - 1]) return [Angles.length - 1, Angles.length - 1, 0];
    let Upper = 1;
    while (Angles[Upper] < Value) Upper++;
    const Lower = Upper - 1;
    return [Lower, Upper, (Value - Angles[Lower]) / (Angles[Upper] - Angles[Lower])];
}

// 📝 Candela toward (θ, φ) in degrees; zero outside the tabulated vertical range (e.g. 0–90 files emit nothing upward).
export function EvaluateCandela(Profile, Vertical, Horizontal)
{
    const V = Profile.Vertical;
    if (Vertical < V[0] - 1e-6 || Vertical > V[V.length - 1] + 1e-6) return 0;
    const [V0, V1, Vt] = Bracket(V, Vertical);
    const [H0, H1, Ht] = Bracket(Profile.Horizontal, FoldHorizontal(Profile, ((Horizontal % 360) + 360) % 360));
    const Row = Index => Profile.Candela[Index][V0] * (1 - Vt) + Profile.Candela[Index][V1] * Vt;
    return Row(H0) * (1 - Ht) + Row(H1) * Ht;
}

// 📝 Resampled table normalized to its peak (the light record carries peak candela × colour), plus what the light tree
//    needs: flux of the normalized table [sr-weighted] and the emission half-angle θe beyond which the bilinear lookup is
//    exactly zero (the next grid angle after the last non-zero sample — conservative, so the tree never culls light).
export function ResampleProfile(Profile)
{
    const Table = new Float32Array(ProfileFloatStride);
    let Peak = 0;
    for (let H = 0; H < ProfileHorizontalSamples; H++)
    {
        for (let V = 0; V < ProfileVerticalSamples; V++)
        {
            const Value = EvaluateCandela(Profile, V * 180 / (ProfileVerticalSamples - 1), H * 360 / ProfileHorizontalSamples);
            Table[H * ProfileVerticalSamples + V] = Value;
            Peak = Math.max(Peak, Value);
        }
    }
    if (Peak <= 0) throw new Error(`${Profile.Label}: emits no light`);
    let LastLit = 0, SolidAngleFlux = 0;
    const Step = Math.PI / (ProfileVerticalSamples - 1);
    for (let V = 0; V < ProfileVerticalSamples; V++)
    {
        let RowSum = 0;
        for (let H = 0; H < ProfileHorizontalSamples; H++)
        {
            const Index = H * ProfileVerticalSamples + V;
            Table[Index] /= Peak;
            RowSum += Table[Index];
            if (Table[Index] > 0) LastLit = V;
        }
        const Band = V === 0 || V === ProfileVerticalSamples - 1 ? 0.5 : 1.0;
        SolidAngleFlux += Band * (RowSum / ProfileHorizontalSamples) * 2 * Math.PI * Math.sin(V * Step) * Step;
    }
    return { Table, PeakCandela: Peak, NormalizedFlux: SolidAngleFlux, EmissionAngle: Math.min(Math.PI, (LastLit + 1) * Step) };
}
