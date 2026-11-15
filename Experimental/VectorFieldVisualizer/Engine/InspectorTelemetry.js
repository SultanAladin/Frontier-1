/*============================================================================================================================================
                                                    INSPECTORTELEMETRY.JS
============================================================================================================================================*/
// 🧩 Project Zero Inspector Depot telemetry renderers:
//    • Rolling 60-second Beaufort Wind Anemometer trace canvas (ported from InspectorDepot/panels/wind.js)
//    • Beaufort color ramp position & human description
//    • Crisp SVG radial attenuation f(r) and temporal duration/impulse profile plot

import {
    Clamp,
    EvaluateRadialProfile,
    ResolveBeaufort,
    ResolveDurationEnvelope
} from './VectorFieldCatalogue.js';

export function DrawAnemometerTrace(Canvas, History)
{
    if (!Canvas || !History || History.length < 2) return;
    const W = Canvas.clientWidth || 320;
    const H = Canvas.clientHeight || 82;
    const DPR = Math.min(window.devicePixelRatio || 1, 2);
    if (Canvas.width !== Math.round(W * DPR) || Canvas.height !== Math.round(H * DPR))
    {
        Canvas.width = Math.round(W * DPR);
        Canvas.height = Math.round(H * DPR);
    }
    const Ctx = Canvas.getContext('2d');
    Ctx.save();
    Ctx.scale(DPR, DPR);
    Ctx.clearRect(0, 0, W, H);

    const MaxV = Math.max(20, Math.ceil(Math.max(...History, 18) / 10) * 10);
    const PadTop = 6, PadBot = 6;
    const PlotH = H - PadTop - PadBot;

    // Subtle horizontal grid lines + labels matching InspectorDepot wind trace
    Ctx.strokeStyle = 'rgba(255,255,255,0.06)';
    Ctx.lineWidth = 1;
    Ctx.fillStyle = 'rgba(255,255,255,0.28)';
    Ctx.font = '10px "DM Sans", sans-serif';
    for (let Step = 10; Step < MaxV; Step += 10)
    {
        const Y = PadTop + PlotH * (1 - Step / MaxV);
        Ctx.beginPath();
        Ctx.moveTo(0, Math.round(Y) + 0.5);
        Ctx.lineTo(W, Math.round(Y) + 0.5);
        Ctx.stroke();
        Ctx.fillText(String(Step), 4, Y - 2);
    }

    const MapX = (I) => (I / Math.max(1, History.length - 1)) * (W - 6) + 2;
    const MapY = (V) => PadTop + PlotH * (1 - Clamp(V / MaxV, 0, 1));

    // Area fill
    const Grad = Ctx.createLinearGradient(0, PadTop, 0, H);
    Grad.addColorStop(0, 'rgba(88, 153, 121, 0.28)');
    Grad.addColorStop(1, 'rgba(88, 153, 121, 0.01)');
    Ctx.beginPath();
    Ctx.moveTo(MapX(0), H);
    for (let I = 0; I < History.length; ++I) Ctx.lineTo(MapX(I), MapY(History[I]));
    Ctx.lineTo(MapX(History.length - 1), H);
    Ctx.closePath();
    Ctx.fillStyle = Grad;
    Ctx.fill();

    // Crisp trace line
    Ctx.beginPath();
    for (let I = 0; I < History.length; ++I)
    {
        const X = MapX(I), Y = MapY(History[I]);
        if (I === 0) Ctx.moveTo(X, Y);
        else Ctx.lineTo(X, Y);
    }
    Ctx.strokeStyle = '#92d8c2';
    Ctx.lineWidth = 1.65;
    Ctx.lineJoin = 'round';
    Ctx.stroke();

    // Live terminal dot
    const LastX = MapX(History.length - 1);
    const LastY = MapY(History[History.length - 1]);
    Ctx.beginPath();
    Ctx.arc(LastX, LastY, 3.2, 0, Math.PI * 2);
    Ctx.fillStyle = '#ffffff';
    Ctx.fill();

    Ctx.restore();
}

export function BeaufortBarShare(Speed)
{
    // Non-linear scale matching 12-band Beaufort visual strip (0..38 m/s)
    return Clamp(Speed / 36, 0, 0.99);
}

export function SummarizeHistory(History)
{
    if (!History.length) return { Mean: 0, Peak: 0, Lull: 0, Beaufort: ResolveBeaufort(0) };
    let Sum = 0, Peak = 0, Lull = Infinity;
    for (const V of History)
    {
        Sum += V;
        if (V > Peak) Peak = V;
        if (V < Lull) Lull = V;
    }
    const Mean = Sum / History.length;
    return { Mean, Peak, Lull: Lull === Infinity ? 0 : Lull, Beaufort: ResolveBeaufort(Mean) };
}

export function BuildAttenuationCurveSvg(Source, SimTime)
{
    const { MaxR, Points } = EvaluateRadialProfile(Source, 40);
    const MaxMag = Math.max(1e-3, ...Points.map((P) => P.Mag));
    const W = 292, H = 74, PadL = 8, PadR = 8, PadT = 10, PadB = 16;
    const IW = W - PadL - PadR, IH = H - PadT - PadB;

    const SharePath = Points.map((P, Idx) => {
        const X = PadL + (P.R / MaxR) * IW;
        const Y = PadT + (1 - P.Share) * IH;
        return `${Idx === 0 ? 'M' : 'L'}${X.toFixed(1)},${Y.toFixed(1)}`;
    }).join(' ');

    const MagPath = Points.map((P, Idx) => {
        const X = PadL + (P.R / MaxR) * IW;
        const Y = PadT + (1 - P.Mag / MaxMag) * IH;
        return `${Idx === 0 ? 'M' : 'L'}${X.toFixed(1)},${Y.toFixed(1)}`;
    }).join(' ');

    const RLineX = PadL + (Source.Radius / MaxR) * IW;
    const Env = ResolveDurationEnvelope(Source, SimTime);

    return `
        <svg viewBox="0 0 ${W} ${H}" class="vf-curve-svg">
            <line x1="${PadL}" y1="${PadT + IH}" x2="${W - PadR}" y2="${PadT + IH}" stroke="rgba(255,255,255,0.12)" stroke-width="1"/>
            <line x1="${RLineX.toFixed(1)}" y1="${PadT}" x2="${RLineX.toFixed(1)}" y2="${PadT + IH}" stroke="rgba(255,180,84,0.45)" stroke-dasharray="3 3" stroke-width="1"/>
            <path d="${SharePath}" fill="none" stroke="rgba(134, 211, 189, 0.52)" stroke-width="1.3" stroke-dasharray="3 2"/>
            <path d="${MagPath}" fill="none" stroke="#ffb454" stroke-width="1.8"/>
            <text x="${PadL}" y="${H - 3}" fill="#6e777d" font-size="9">0 m</text>
            <text x="${(RLineX - 10).toFixed(1)}" y="${H - 3}" fill="#d9a861" font-size="9">R=${Math.round(Source.Radius)}m</text>
            <text x="${W - PadR}" y="${H - 3}" text-anchor="end" fill="#6e777d" font-size="9">g(t)=${Env.Gate.toFixed(2)}</text>
        </svg>
    `;
}
