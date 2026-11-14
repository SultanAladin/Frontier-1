//============================================================================================================================================
//                                                        PARTICLEINFOGRAPHICS.JS
//============================================================================================================================================
// 📊 Particle-Specific Interactive Visual Infographics for Overview Card #1 (Particle Effects, Force Field Sources & Wind Field).
//    Replaces generic X/Y line/curve plots with tailored, live-animated, tactile domain infographics matched to each particle's physics.

import { PE } from './Engine/pe.js';

function LinToCss(Col, Alpha = 1) {
    if (!Col) return `rgba(240, 207, 160, ${Alpha})`;
    const ToB = V => Math.round(Math.min(1, Math.max(0, Math.pow(Math.max(0, V), 1 / 2.2))) * 255);
    return `rgba(${ToB(Col[0])}, ${ToB(Col[1])}, ${ToB(Col[2])}, ${Math.min(1, Math.max(0, Alpha)).toFixed(3)})`;
}

function RampSample(P, T, Alpha = 1) {
    const Stops = PE.getColorStops ? PE.getColorStops(P) : null;
    if (Stops && Stops.length && PE.evalColorRampAt) {
        const C = PE.evalColorRampAt(Stops, Math.max(0, Math.min(1, T)), P);
        return LinToCss(C, Alpha);
    }
    return LinToCss(T < 0.5 ? P?.colA : (P?.colC || P?.colB), Alpha);
}

function PickArchetype(P, Cls) {
    if (Cls.IsGrid) return 'grid';
    if (Cls.IsFerro) return 'ferro';
    if (Cls.IsCrack) return 'crack';
    if (Cls.IsMud) return 'mud';
    if (Cls.IsBeads) return 'beads';
    if (Cls.IsSeedHead) return 'seed';
    if (Cls.IsJelly) return 'jelly';
    if (Cls.IsWeb) return 'web';
    if (Cls.IsDyn) return 'dyn';
    if (Cls.IsArc || Cls.IsBeam) return 'arc';
    if (Cls.IsCurl) return (P.curlMode === 3 ? 'bokeh' : 'curl');
    if (Cls.IsBoids) return 'boids';
    if (Cls.IsTransition) return 'transition';
    if (P.blackHole || P.heatShimmer || P.shape === 17) return 'lens';
    if (P.shape === 10 || P.shape === 11 || P.shape === 12 || P.shape === 15 || P.shape === 16) return 'shield';
    return 'emitter';
}

function SetupCanvas(Canvas, Host, Height) {
    const Dpr = Math.min(2, window.devicePixelRatio || 1);
    const W = Math.max(220, Math.round(Canvas.clientWidth || Host.clientWidth || 308));
    if (Canvas.width !== Math.round(W * Dpr) || Canvas.height !== Math.round(Height * Dpr)) {
        Canvas.width = Math.round(W * Dpr);
        Canvas.height = Math.round(Height * Dpr);
    }
    const Ctx = Canvas.getContext('2d');
    Ctx.setTransform(Dpr, 0, 0, Dpr, 0, 0);
    Ctx.clearRect(0, 0, W, Height);
    return { Ctx, W, H: Height };
}

function DrawPill(Ctx, Text, X, Y, Align = 'left', Accent = 'rgba(255,255,255,0.68)', Bg = 'rgba(255,255,255,0.06)') {
    Ctx.save();
    Ctx.font = '600 8.5px "DM Sans", system-ui, sans-serif';
    const Tw = Ctx.measureText(Text).width;
    const Pw = Tw + 12, Ph = 16;
    const Rx = Align === 'right' ? X - Pw : (Align === 'center' ? X - Pw * 0.5 : X);
    const Ry = Y - Ph * 0.5;
    Ctx.fillStyle = Bg;
    Ctx.beginPath();
    if (Ctx.roundRect) Ctx.roundRect(Rx, Ry, Pw, Ph, 8);
    else Ctx.rect(Rx, Ry, Pw, Ph);
    Ctx.fill();
    Ctx.fillStyle = Accent;
    Ctx.textAlign = 'center';
    Ctx.textBaseline = 'middle';
    Ctx.fillText(Text, Rx + Pw * 0.5, Y + 0.5);
    Ctx.restore();
}

function DrawRingGauge(Ctx, Cx, Cy, R, Ratio, Color, Label, ValText, SubText = '') {
    const Clamped = Math.max(0, Math.min(1, Ratio));
    Ctx.save();
    Ctx.lineWidth = 5;
    Ctx.lineCap = 'round';
    Ctx.strokeStyle = 'rgba(255,255,255,0.07)';
    Ctx.beginPath();
    Ctx.arc(Cx, Cy, R, -Math.PI * 0.5, Math.PI * 1.5);
    Ctx.stroke();

    if (Clamped > 0.005) {
        Ctx.strokeStyle = Color;
        Ctx.beginPath();
        Ctx.arc(Cx, Cy, R, -Math.PI * 0.5, -Math.PI * 0.5 + Clamped * Math.PI * 2);
        Ctx.stroke();
    }
    Ctx.fillStyle = '#f0f0f0';
    Ctx.font = '500 13px "DM Sans", system-ui, sans-serif';
    Ctx.textAlign = 'center';
    Ctx.textBaseline = 'middle';
    Ctx.fillText(ValText, Cx, Cy - (SubText ? 3 : 1));
    if (SubText) {
        Ctx.fillStyle = 'rgba(255,255,255,0.45)';
        Ctx.font = '500 8px "DM Sans", system-ui, sans-serif';
        Ctx.fillText(SubText, Cx, Cy + 10);
    }
    if (Label) {
        Ctx.fillStyle = 'rgba(255,255,255,0.46)';
        Ctx.font = '600 8.5px "DM Sans", system-ui, sans-serif';
        Ctx.fillText(Label, Cx, Cy + R + 13);
    }
    Ctx.restore();
}

export function ParticleInfographic(Parent, Effect, Cls, Hooks = {}) {
    const Height = Hooks.Height || 152;
    const Wrap = document.createElement('div');
    Wrap.className = 'diagram-wrap infographic-wrap';
    const Canvas = document.createElement('canvas');
    Canvas.className = 'infographic-canvas';
    Canvas.style.cssText = `width:100%;height:${Height}px;display:block;cursor:pointer;touch-action:none;`;
    const Caption = document.createElement('div');
    Caption.className = 'plot-caption infographic-caption';
    Wrap.append(Canvas, Caption);
    Parent.append(Wrap);

    const Archetype = PickArchetype(Effect.P, Cls);
    let Dragging = false;
    let HoverPt = null;
    let PulseFlash = 0;
    let SwipePhase = 0;
    let LastLive = { alive: 0, cap: Effect.P.capacity || 1, time: 0, wind: [0, 0, 0] };

    const ApplyInteract = (X, Y, W, H, IsClick) => {
        const P = Effect.P;
        const Nx = Math.max(0, Math.min(1, X / Math.max(1, W)));
        const Ny = Math.max(0, Math.min(1, Y / Math.max(1, H)));
        if (Archetype === 'grid') {
            // Drag on left 68% matrix maps directly to Effector A XZ relative to origin
            const MatW = W * 0.66;
            if (X <= MatW + 8) {
                const U = Math.max(0, Math.min(1, (X - 10) / Math.max(20, MatW - 20))) * 2 - 1;
                const V = Math.max(0, Math.min(1, (Y - 22) / Math.max(20, H - 36))) * 2 - 1;
                const Span = Math.max(1.2, P.radius || P.boxHalf?.[0] || 1.7);
                const Org = P.origin || [0, 1.5, 0];
                P.effPos = [+(Org[0] + U * Span * 1.15).toFixed(2), +(P.effPos?.[1] ?? Org[1]).toFixed(2), +(Org[2] + V * Span * 1.15).toFixed(2)];
                if ((P.effSize ?? 0) < 0.2) P.effSize = 1.15;
                if (P.gridEffector !== 5 && !P.effBlend) P.effBlend = true;
                Hooks.OnChange?.('effPos');
            }
        } else if (Archetype === 'ferro') {
            // Drag on left polar map steers the 3D magnetic pole!
            const Cx = W * 0.36, Cy = H * 0.52, R = Math.min(W * 0.28, H * 0.40);
            const Dx = (X - Cx) / Math.max(10, R), Dy = (Y - Cy) / Math.max(10, R);
            const Dist = Math.min(1.25, Math.hypot(Dx, Dy));
            const Ang = Math.atan2(Dy, Dx);
            const PoolR = Math.max(0.5, P.radius || 2.0);
            const Org = P.origin || [0, 0, 0];
            P.ferroMagnetFree = true;
            P.effPos = [
                +(Org[0] + Math.cos(Ang) * Dist * PoolR).toFixed(2),
                +(Org[1] + (P.ferroMode === 2 ? 0.25 : 0.62)).toFixed(2),
                +(Org[2] + Math.sin(Ang) * Dist * PoolR).toFixed(2),
            ];
            Hooks.OnChange?.('ferroMagnet');
        } else if (Archetype === 'crack' && IsClick) {
            const MapW = W * 0.64;
            const U = Math.max(-1, Math.min(1, ((X - 12) / Math.max(20, MapW - 24)) * 2 - 1));
            const V = Math.max(-1, Math.min(1, ((Y - 20) / Math.max(20, H - 34)) * 2 - 1));
            const ExtX = (Array.isArray(P.boxHalf) ? P.boxHalf[0] : 2.6) || 2.6;
            const ExtZ = (Array.isArray(P.boxHalf) ? P.boxHalf[2] : 2.6) || 2.6;
            const Org = P.origin || [0, 0, 0];
            PulseFlash = 1;
            Hooks.OnCrackStrike?.(Org[0] + U * ExtX, Org[2] + V * ExtZ);
        } else if (Archetype === 'arc' && IsClick) {
            PulseFlash = 1;
            Hooks.OnBurst?.();
        } else if (Archetype === 'bokeh' && IsClick) {
            // 4 clickable aperture lens cards
            const Slot = Math.max(0, Math.min(3, Math.floor((X / Math.max(1, W)) * 4)));
            P.headStyle = Slot;
            PulseFlash = 1;
            Hooks.OnChange?.('headStyle');
        } else if (Archetype === 'boids') {
            // Drag inside equilateral triangle (Cohere top, Align bottom-left, Separate bottom-right)
            const Cx = W * 0.36, Cy = H * 0.56, R = 50;
            const Ax = Cx, Ay = Cy - R;
            const Bx = Cx - R * 0.92, By = Cy + R * 0.62;
            const Dx = Cx + R * 0.92, Dy = Cy + R * 0.62;
            const Det = (By - Dy) * (Ax - Dx) + (Dx - Bx) * (Ay - Dy) || 1;
            let W1 = ((By - Dy) * (X - Dx) + (Dx - Bx) * (Y - Dy)) / Det;
            let W2 = ((Dy - Ay) * (X - Dx) + (Ax - Dx) * (Y - Dy)) / Det;
            let W3 = 1 - W1 - W2;
            W1 = Math.max(0.06, W1); W2 = Math.max(0.06, W2); W3 = Math.max(0.06, W3);
            const Sum = W1 + W2 + W3;
            const Scale = 5.65;
            P.boidsCohere = +((W1 / Sum) * Scale).toFixed(2);
            P.boidsAlign = +((W2 / Sum) * Scale).toFixed(2);
            P.boidsSeparate = +((W3 / Sum) * Scale).toFixed(2);
            Hooks.OnChange?.('boids');
        } else if (Archetype === 'dyn') {
            P.dynSag = +(Math.max(0.02, Math.min(1.4, Ny * 1.15))).toFixed(2);
            P.dynStiff = +(Math.max(0.35, Math.min(3.5, 0.4 + Nx * 2.8))).toFixed(2);
            Hooks.OnChange?.('dyn');
        } else if (Archetype === 'beads') {
            SwipePhase = (SwipePhase + 0.65) % (Math.PI * 2);
            P.dynWaveSpeed = +(0.6 + Nx * 3.8).toFixed(2);
            Hooks.OnChange?.('beads');
        } else if (Archetype === 'seed') {
            const Cx = W * 0.34, Cy = H * 0.52;
            const Dist = Math.hypot(X - Cx, Y - Cy);
            const Ratio = Math.max(0.15, Math.min(1.0, Dist / 52));
            P.dynTearThresh = +(0.6 + Ratio * 7.5).toFixed(2);
            Hooks.OnChange?.('seed');
        } else if (Archetype === 'jelly') {
            P.dynWaveSpeed = +(1.2 + Nx * 7.5).toFixed(2);
            P.bounce = +(Math.max(0.15, Math.min(0.95, 1 - Ny * 0.85))).toFixed(2);
            PulseFlash = 1;
            Hooks.OnChange?.('jelly');
        } else if (Archetype === 'web') {
            P.webSag = +(Math.max(0.02, Math.min(0.65, Ny * 0.55))).toFixed(2);
            Hooks.OnChange?.('web');
        } else if (Archetype === 'curl') {
            P.curlAmp = +(Math.max(0.2, Math.min(6.0, 0.3 + Nx * 5.2))).toFixed(2);
            P.curlFreq = +(Math.max(0.2, Math.min(2.8, 0.25 + (1 - Ny) * 2.3))).toFixed(2);
            Hooks.OnChange?.('curl');
        } else if (Archetype === 'transition' && IsClick) {
            PulseFlash = 1;
            Hooks.OnReset?.();
        } else if (Archetype === 'mud' && IsClick) {
            PulseFlash = 1;
            Hooks.OnBurst?.();
        } else if (Archetype === 'shield' && IsClick) {
            PulseFlash = 1;
            Hooks.OnBurst?.();
        } else if (Archetype === 'lens') {
            if (P.blackHole) {
                const Cx = W * 0.36, Cy = H * 0.52;
                const R = Math.hypot(X - Cx, Y - Cy);
                P.blackHole.einstein = +(Math.max(0.35, Math.min(2.6, R / 28))).toFixed(2);
                Hooks.OnChange?.('lens');
            } else if (P.heatShimmer) {
                P.heatShimmer.strength = +(Math.max(0.004, Math.min(0.045, 0.005 + Nx * 0.036))).toFixed(3);
                Hooks.OnChange?.('lens');
            }
        } else if (Archetype === 'emitter') {
            // Drag launch cone center to tune spread & speed, click left ring to burst
            const ConeCx = W * 0.56, ConeCy = H * 0.62;
            const Dx = X - ConeCx, Dy = ConeCy - Y;
            if (IsClick && X < W * 0.32) {
                PulseFlash = 1;
                Hooks.OnBurst?.();
            } else if (X >= W * 0.30) {
                const Dist = Math.hypot(Dx, Dy);
                const Spread = Math.max(0.05, Math.min(1.55, Math.abs(Math.atan2(Dx, Math.max(12, Dy))) * 1.35));
                P.spread = +Spread.toFixed(2);
                if (Dist > 12 && (P.speedMax ?? 0) > 0.05) {
                    const NewMax = Math.max(0.4, Math.min(18, (Dist / 44) * 6.5));
                    P.speedMax = +NewMax.toFixed(2);
                    if (P.speedMin > P.speedMax) P.speedMin = +(P.speedMax * 0.55).toFixed(2);
                }
                Hooks.OnChange?.('emitter');
            }
        }
        Draw(LastLive);
    };

    Canvas.addEventListener('pointerdown', E => {
        const Rect = Canvas.getBoundingClientRect();
        Dragging = true;
        Canvas.setPointerCapture?.(E.pointerId);
        ApplyInteract(E.clientX - Rect.left, E.clientY - Rect.top, Rect.width, Rect.height, true);
    });
    Canvas.addEventListener('pointermove', E => {
        const Rect = Canvas.getBoundingClientRect();
        HoverPt = [E.clientX - Rect.left, E.clientY - Rect.top];
        if (Dragging) ApplyInteract(HoverPt[0], HoverPt[1], Rect.width, Rect.height, false);
    });
    const EndDrag = () => { Dragging = false; };
    Canvas.addEventListener('pointerup', EndDrag);
    Canvas.addEventListener('pointercancel', EndDrag);
    Canvas.addEventListener('pointerleave', () => { HoverPt = null; Dragging = false; });

    function Draw(Live = LastLive) {
        LastLive = Live;
        const { Ctx, W, H } = SetupCanvas(Canvas, Wrap, Height);
        const P = Effect.P;
        const T = Live.time || 0;
        const Cap = Math.max(1, P.capacity || 1);
        const Alive = Math.min(Cap, Math.max(0, Live.alive || 0));
        const FillRatio = Alive / Cap;
        const Col0 = RampSample(P, 0.15, 0.95);
        const ColMid = RampSample(P, 0.52, 0.95);
        const Col1 = RampSample(P, 0.92, 0.95);
        PulseFlash *= 0.86;

        // Subtle recessed blueprint grid glow
        Ctx.fillStyle = 'rgba(255,255,255,0.015)';
        Ctx.fillRect(0, 0, W, H);

        if (Archetype === 'grid') {
            const MatX = 12, MatY = 22, MatW = W * 0.60, MatH = H - 34;
            const Cols = 11, Rows = 7;
            const Cw = MatW / Cols, Ch = MatH / Rows;
            const Span = Math.max(1.2, P.radius || P.boxHalf?.[0] || 1.7);
            const Org = P.origin || [0, 1.5, 0];
            const EffMode = Math.round(P.gridEffector ?? 0);
            const ShowPlace = EffMode === 5 || !!P.effBlend;
            const EffAU = ShowPlace && P.effPos ? (P.effPos[0] - Org[0]) / (Span * 1.15) : 0;
            const EffAV = ShowPlace && P.effPos ? (P.effPos[2] - Org[2]) / (Span * 1.15) : 0;
            const EffBU = ShowPlace && P.effPos2 && (P.effSize2 || 0) > 0.05 ? (P.effPos2[0] - Org[0]) / (Span * 1.15) : 99;
            const EffBV = ShowPlace && P.effPos2 && (P.effSize2 || 0) > 0.05 ? (P.effPos2[2] - Org[2]) / (Span * 1.15) : 99;
            let ActiveSum = 0;

            for (let R = 0; R < Rows; R++) {
                for (let C = 0; C < Cols; C++) {
                    const U = ((C + 0.5) / Cols) * 2 - 1;
                    const V = ((R + 0.5) / Rows) * 2 - 1;
                    let Wv = 0.15;
                    if (EffMode === 0) Wv = 0.5 + 0.5 * Math.sin(U * 3.1 + V * 2.4 - T * (P.curlAmp || 0.7) * 2.4);
                    else if (EffMode === 1) {
                        const Ox = Math.cos(T * 1.2) * 0.65, Oy = Math.sin(T * 1.2) * 0.65;
                        Wv = Math.max(0, 1 - Math.hypot(U - Ox, V - Oy) * 1.15);
                    } else if (EffMode === 2) Wv = 0.5 + 0.5 * Math.sin(U * 4.2 + T) * Math.cos(V * 3.6 - T * 0.8);
                    else if (EffMode === 3) Wv = 0.5 + 0.5 * Math.sin(Math.hypot(U, V) * 5.2 - T * 3.2);
                    else Wv = 0.06;
                    if (ShowPlace) {
                        const R1 = Math.max(0.28, (P.effSize || 1.1) / (Span * 1.25));
                        const WA = Math.max(0, 1 - Math.hypot(U - EffAU, V - EffAV) / R1);
                        const R2 = Math.max(0.28, (P.effSize2 || 0) / (Span * 1.25));
                        const WB = (P.effSize2 || 0) > 0.05 ? Math.max(0, 1 - Math.hypot(U - EffBU, V - EffBV) / R2) : 0;
                        Wv = EffMode === 5 ? Math.max(WA, WB) : Math.max(Wv, WA, WB);
                    }
                    Wv = Math.pow(Math.max(0, Math.min(1, Wv)), Math.max(0.45, P.gridContrast || 1.6));
                    ActiveSum += Wv;
                    const Px = MatX + (C + 0.5) * Cw;
                    const Py = MatY + (R + 0.5) * Ch;
                    const Rad = Math.max(1.5, Math.min(Cw, Ch) * (0.16 + 0.30 * Wv));
                    Ctx.fillStyle = RampSample(P, Wv, 0.35 + 0.65 * Wv);
                    const Gs = Math.round(P.gridShape ?? 2);
                    if (Gs === 0 || Gs === 6) {
                        Ctx.beginPath(); Ctx.arc(Px, Py, Rad, 0, Math.PI * 2); Ctx.fill();
                    } else if (Gs === 5) {
                        Ctx.beginPath();
                        for (let K = 0; K < 6; K++) {
                            const A = (K / 6) * Math.PI * 2;
                            K === 0 ? Ctx.moveTo(Px + Math.cos(A) * Rad, Py + Math.sin(A) * Rad) : Ctx.lineTo(Px + Math.cos(A) * Rad, Py + Math.sin(A) * Rad);
                        }
                        Ctx.closePath(); Ctx.fill();
                    } else {
                        Ctx.fillRect(Px - Rad, Py - Rad, Rad * 2, Rad * 2);
                    }
                }
            }
            if (ShowPlace) {
                const DrawEffDot = (U, V, Tag, Col) => {
                    const Ex = MatX + (U * 0.5 + 0.5) * MatW;
                    const Ey = MatY + (V * 0.5 + 0.5) * MatH;
                    Ctx.strokeStyle = Col;
                    Ctx.lineWidth = 1.4;
                    Ctx.setLineDash([3, 3]);
                    Ctx.beginPath(); Ctx.arc(Ex, Ey, 16, 0, Math.PI * 2); Ctx.stroke();
                    Ctx.setLineDash([]);
                    Ctx.fillStyle = Col;
                    Ctx.beginPath(); Ctx.arc(Ex, Ey, 4, 0, Math.PI * 2); Ctx.fill();
                    Ctx.fillStyle = '#fff';
                    Ctx.font = '700 8px "DM Sans", sans-serif';
                    Ctx.fillText(Tag, Ex + 7, Ey + 3);
                };
                if ((P.effSize || 0) > 0.05) DrawEffDot(Math.max(-1, Math.min(1, EffAU)), Math.max(-1, Math.min(1, EffAV)), 'A', '#3ec46d');
                if ((P.effSize2 || 0) > 0.05) DrawEffDot(Math.max(-1, Math.min(1, EffBU)), Math.max(-1, Math.min(1, EffBV)), 'B', '#f5b041');
            }
            const AvgPct = Math.round((ActiveSum / (Cols * Rows)) * 100);
            DrawRingGauge(Ctx, W * 0.82, H * 0.48, 34, AvgPct / 100, Col1, 'EFFECTOR', `${AvgPct}%`, `${Cap} tiles`);
            DrawPill(Ctx, 'EFFECTOR FIELD MAP', 10, 11, 'left');
            DrawPill(Ctx, 'DRAG TO MOVE EFF A', W - 10, 11, 'right', '#83d6a1', 'rgba(62,196,109,0.14)');
            Caption.textContent = `Drag inside the matrix to steer Effector A · ${Cap.toLocaleString()} active instances`;
        }
        else if (Archetype === 'ferro') {
            const Cx = W * 0.36, Cy = H * 0.54, R = Math.min(W * 0.25, H * 0.36);
            // Magnetic flux rings
            for (let K = 1; K <= 3; K++) {
                Ctx.strokeStyle = `rgba(255,255,255,${0.04 + K * 0.025})`;
                Ctx.lineWidth = 1;
                Ctx.beginPath(); Ctx.arc(Cx, Cy, R * (K / 3), 0, Math.PI * 2); Ctx.stroke();
            }
            const PoolR = Math.max(0.5, P.radius || 2.0);
            const Org = P.origin || [0, 0, 0];
            let Mu, Mv;
            if (P.ferroMagnetFree && P.effPos) {
                Mu = Math.max(-1.2, Math.min(1.2, (P.effPos[0] - Org[0]) / PoolR));
                Mv = Math.max(-1.2, Math.min(1.2, (P.effPos[2] - Org[2]) / PoolR));
            } else {
                const Ph = T * (P.ferroMagnetSpeed ?? 1.35);
                Mu = Math.cos(Ph) * 0.72;
                Mv = Math.sin(Ph * 1.25) * 0.62;
            }
            const Mx = Cx + Mu * R, My = Cy + Mv * R;
            // Rosensweig spike crown radiating toward magnet
            const Spikes = 28;
            Ctx.beginPath();
            for (let I = 0; I <= Spikes; I++) {
                const A = (I / Spikes) * Math.PI * 2;
                const Px = Cx + Math.cos(A) * R * 0.62, Py = Cy + Math.sin(A) * R * 0.62;
                const Pull = Math.max(0, 1 - Math.hypot(Px - Mx, Py - My) / (R * 1.55));
                const SpikeR = R * (0.42 + 0.14 * Math.abs(Math.sin(I * 3.1 + T * 3)) + 0.44 * Pull * Pull);
                const Sx = Cx + Math.cos(A) * SpikeR, Sy = Cy + Math.sin(A) * SpikeR;
                I === 0 ? Ctx.moveTo(Sx, Sy) : Ctx.lineTo(Sx, Sy);
            }
            Ctx.closePath();
            Ctx.fillStyle = RampSample(P, 0.28, 0.35);
            Ctx.fill();
            Ctx.strokeStyle = Col1;
            Ctx.lineWidth = 1.6;
            Ctx.stroke();
            // Magnet pole handle
            Ctx.strokeStyle = '#ff6b6b';
            Ctx.lineWidth = 1.2;
            Ctx.beginPath(); Ctx.moveTo(Cx, Cy); Ctx.lineTo(Mx, My); Ctx.stroke();
            Ctx.fillStyle = '#ff5c5c';
            Ctx.beginPath(); Ctx.arc(Mx, My, 6, 0, Math.PI * 2); Ctx.fill();
            Ctx.strokeStyle = '#fff'; Ctx.lineWidth = 1.5; Ctx.stroke();

            const SpikeCnt = Math.round(P.ferroSpikes ?? 520);
            DrawRingGauge(Ctx, W * 0.79, H * 0.52, 33, Math.min(1, SpikeCnt / Cap), Col1, 'SPIKES', `${SpikeCnt}`, P.ferroMagnetFree ? 'MANUAL' : 'ORBIT');
            DrawPill(Ctx, 'MAGNETIC FLUX CROWN', 10, 11, 'left');
            DrawPill(Ctx, 'DRAG MAGNET POLE', W - 10, 11, 'right', '#ff9b9b', 'rgba(255,92,92,0.14)');
            Caption.textContent = P.ferroMagnetFree ? 'Manual magnetic pole active — drag here or in the 3D viewport' : 'Drag the red pole to take manual 3D control of the magnet';
        }
        else if (Archetype === 'crack') {
            const MapX = 14, MapY = 22, MapW = W * 0.58, MapH = H - 34;
            const Org = P.origin || [0, 0, 0];
            const ExtX = (Array.isArray(P.boxHalf) ? P.boxHalf[0] : 2.6) || 2.6;
            const ExtZ = (Array.isArray(P.boxHalf) ? P.boxHalf[2] : 2.6) || 2.6;
            const HitU = Effect.CrackHit ? Math.max(-0.9, Math.min(0.9, (Effect.CrackHit.x - Org[0]) / ExtX)) : 0;
            const HitV = Effect.CrackHit ? Math.max(-0.9, Math.min(0.9, (Effect.CrackHit.z - Org[2]) / ExtZ)) : 0;
            const Ex = MapX + (HitU * 0.5 + 0.5) * MapW;
            const Ey = MapY + (HitV * 0.5 + 0.5) * MapH;
            Ctx.fillStyle = 'rgba(255,255,255,0.03)';
            Ctx.strokeStyle = 'rgba(255,255,255,0.12)';
            Ctx.lineWidth = 1;
            Ctx.fillRect(MapX, MapY, MapW, MapH);
            Ctx.strokeRect(MapX, MapY, MapW, MapH);
            // Radiating fracture fissures
            const Prog = (T * (P.crackSpeed || 2.4) * 0.16) % 1;
            for (let R = 0; R < 9; R++) {
                const Ang = (R / 9) * Math.PI * 2 + 0.25;
                Ctx.strokeStyle = RampSample(P, 0.72, 0.75 + PulseFlash * 0.25);
                Ctx.lineWidth = 1.5;
                Ctx.beginPath();
                Ctx.moveTo(Ex, Ey);
                let Px = Ex, Py = Ey;
                for (let S = 1; S <= 4; S++) {
                    const J = Math.sin(R * 5.1 + S * 2.7) * 0.34;
                    const Len = (S / 4) * Math.min(MapW, MapH) * 0.56 * Math.min(1, Prog * 1.45 + 0.25);
                    Px = Math.max(MapX, Math.min(MapX + MapW, Ex + Math.cos(Ang + J) * Len));
                    Py = Math.max(MapY, Math.min(MapY + MapH, Ey + Math.sin(Ang + J) * Len));
                    Ctx.lineTo(Px, Py);
                }
                Ctx.stroke();
            }
            Ctx.fillStyle = Col1;
            Ctx.beginPath(); Ctx.arc(Ex, Ey, 4.5 + PulseFlash * 4, 0, Math.PI * 2); Ctx.fill();
            const Heave = P.crackDepth ?? 0.44;
            DrawRingGauge(Ctx, W * 0.81, H * 0.52, 33, Math.min(1, Heave / 1.2), Col1, 'HEAVE', `${Heave.toFixed(2)}m`, `${(P.crackSpeed || 2.6).toFixed(1)}m/s`);
            DrawPill(Ctx, 'VORONOI FRACTURE MAP', 10, 11, 'left');
            DrawPill(Ctx, 'CLICK EPICENTER', W - 10, 11, 'right', '#f0cfa0', 'rgba(240,207,160,0.14)');
            Caption.textContent = 'Click anywhere on the plate to trigger a live 3D fracture shockwave';
        }
        else if (Archetype === 'bokeh') {
            const Styles = [
                { id: 0, name: 'Mixed', desc: 'Depth Bokeh' },
                { id: 1, name: 'Sharp', desc: 'Pinpoint' },
                { id: 2, name: 'Creamy', desc: 'Soft Disc' },
                { id: 3, name: 'Rim Ring', desc: 'Aperture' },
            ];
            const Cur = Math.max(0, Math.min(3, Math.round(P.headStyle ?? 0)));
            const SlotW = (W - 20) / 4;
            for (let I = 0; I < 4; I++) {
                const Bx = 10 + I * SlotW, By = 26, Bw = SlotW - 6, Bh = H - 38;
                const Active = I === Cur;
                Ctx.fillStyle = Active ? 'rgba(240,207,160,0.13)' : 'rgba(255,255,255,0.03)';
                Ctx.strokeStyle = Active ? Col1 : 'rgba(255,255,255,0.08)';
                Ctx.lineWidth = Active ? 1.5 : 1;
                Ctx.beginPath();
                if (Ctx.roundRect) Ctx.roundRect(Bx, By, Bw, Bh, 10);
                else Ctx.rect(Bx, By, Bw, Bh);
                Ctx.fill(); Ctx.stroke();
                const Cx = Bx + Bw * 0.5, Cy = By + Bh * 0.40;
                const Sc = Math.max(0.55, Math.min(1.4, P.headScale ?? 1));
                const Rad = 14 * Sc * 0.85;
                if (I === 0) {
                    Ctx.fillStyle = RampSample(P, 0.65, 0.38);
                    Ctx.beginPath(); Ctx.arc(Cx - 6, Cy - 3, Rad * 0.72, 0, Math.PI * 2); Ctx.fill();
                    Ctx.fillStyle = '#fff';
                    Ctx.beginPath(); Ctx.arc(Cx + 7, Cy + 5, 3.5, 0, Math.PI * 2); Ctx.fill();
                } else if (I === 1) {
                    Ctx.fillStyle = RampSample(P, 0.85, 0.35);
                    Ctx.beginPath(); Ctx.arc(Cx, Cy, Rad * 0.75, 0, Math.PI * 2); Ctx.fill();
                    Ctx.fillStyle = '#fff';
                    Ctx.beginPath(); Ctx.arc(Cx, Cy, 4.2, 0, Math.PI * 2); Ctx.fill();
                } else if (I === 2) {
                    const G = Ctx.createRadialGradient(Cx, Cy, 0, Cx, Cy, Rad * 1.1);
                    G.addColorStop(0, RampSample(P, 0.85, 0.85));
                    G.addColorStop(0.75, RampSample(P, 0.65, 0.42));
                    G.addColorStop(1, RampSample(P, 0.5, 0));
                    Ctx.fillStyle = G;
                    Ctx.beginPath(); Ctx.arc(Cx, Cy, Rad * 1.1, 0, Math.PI * 2); Ctx.fill();
                } else {
                    Ctx.strokeStyle = Col1;
                    Ctx.lineWidth = 2.4;
                    Ctx.beginPath(); Ctx.arc(Cx, Cy, Rad * 0.86, 0, Math.PI * 2); Ctx.stroke();
                    Ctx.fillStyle = RampSample(P, 0.75, 0.16);
                    Ctx.fill();
                }
                Ctx.fillStyle = Active ? '#f5f5f5' : '#9c9c9c';
                Ctx.font = `${Active ? 600 : 500} 9.5px "DM Sans", sans-serif`;
                Ctx.textAlign = 'center';
                Ctx.fillText(Styles[I].name, Cx, By + Bh - 18);
                Ctx.fillStyle = 'rgba(255,255,255,0.42)';
                Ctx.font = '500 8px "DM Sans", sans-serif';
                Ctx.fillText(Styles[I].desc, Cx, By + Bh - 7);
            }
            DrawPill(Ctx, 'OPTICAL BOKEH APERTURE', 10, 11, 'left');
            DrawPill(Ctx, `SCALE ${(P.headScale ?? 1).toFixed(2)}×`, W - 10, 11, 'right', '#83d6a1', 'rgba(62,196,109,0.14)');
            Caption.textContent = 'Click any aperture lens to switch bokeh head style live across all ribbons';
        }
        else if (Archetype === 'boids') {
            const Cx = W * 0.36, Cy = H * 0.58, R = 48;
            const Ax = Cx, Ay = Cy - R;
            const Bx = Cx - R * 0.92, By = Cy + R * 0.62;
            const Dx = Cx + R * 0.92, Dy = Cy + R * 0.62;
            Ctx.strokeStyle = 'rgba(255,255,255,0.14)';
            Ctx.fillStyle = 'rgba(255,255,255,0.03)';
            Ctx.lineWidth = 1.2;
            Ctx.beginPath(); Ctx.moveTo(Ax, Ay); Ctx.lineTo(Bx, By); Ctx.lineTo(Dx, Dy); Ctx.closePath();
            Ctx.fill(); Ctx.stroke();

            const Wc = Math.max(0.05, P.boidsCohere ?? 1.15);
            const Wa = Math.max(0.05, P.boidsAlign ?? 1.65);
            const Ws = Math.max(0.05, P.boidsSeparate ?? 2.85);
            const Sum = Wc + Wa + Ws;
            const Px = (Ax * Wc + Bx * Wa + Dx * Ws) / Sum;
            const Py = (Ay * Wc + By * Wa + Dy * Ws) / Sum;

            // Animated mini flock arrows swirling around balance reticle
            for (let I = 0; I < 9; I++) {
                const Ang = T * 1.8 + I * 0.72;
                const Fr = 14 + (I % 3) * 7;
                const Fx = Px + Math.cos(Ang) * Fr * 0.85, Fy = Py + Math.sin(Ang) * Fr * 0.55;
                Ctx.fillStyle = RampSample(P, I / 9, 0.78);
                Ctx.beginPath(); Ctx.arc(Fx, Fy, 2.2, 0, Math.PI * 2); Ctx.fill();
            }
            Ctx.fillStyle = Col1;
            Ctx.beginPath(); Ctx.arc(Px, Py, 5.5, 0, Math.PI * 2); Ctx.fill();
            Ctx.strokeStyle = '#111'; Ctx.lineWidth = 1.8; Ctx.stroke();

            Ctx.fillStyle = 'rgba(255,255,255,0.65)';
            Ctx.font = '600 8px "DM Sans", sans-serif';
            Ctx.textAlign = 'center';
            Ctx.fillText(`COHERE ${Wc.toFixed(1)}`, Ax, Ay - 6);
            Ctx.fillText(`ALIGN ${Wa.toFixed(1)}`, Bx + 4, By + 12);
            Ctx.fillText(`SEPARATE ${Ws.toFixed(1)}`, Dx - 6, Dy + 12);

            DrawRingGauge(Ctx, W * 0.80, H * 0.52, 33, Math.min(1, (P.boidsSpeed ?? 3.4) / 8), ColMid, 'FLIGHT', `${(P.boidsSpeed ?? 3.4).toFixed(1)}`, 'm/s');
            DrawPill(Ctx, 'FLOCKING RULE TRIANGLE', 10, 11, 'left');
            DrawPill(Ctx, 'DRAG BALANCE POINT', W - 10, 11, 'right', '#83d6a1', 'rgba(62,196,109,0.14)');
            Caption.textContent = 'Drag inside the triangle to rebalance Cohesion, Alignment & Separation live';
        }
        else if (Archetype === 'arc') {
            const Ax = 34, Ay = H * 0.54, Bx = W * 0.64, By = H * 0.54;
            const Org = P.origin || [0, 1.8, 0];
            const Tgt = P.targetPos || [Org[0] + 4, Org[1], Org[2]];
            const Gap = Math.hypot(Tgt[0] - Org[0], Tgt[1] - Org[1], Tgt[2] - Org[2]);
            // Electrode spheres
            for (const [Ex, Ey, Lbl] of [[Ax, Ay, 'A'], [Bx, By, 'B']]) {
                const G = Ctx.createRadialGradient(Ex, Ey, 2, Ex, Ey, 15);
                G.addColorStop(0, '#fff');
                G.addColorStop(0.45, Col1);
                G.addColorStop(1, 'rgba(0,0,0,0)');
                Ctx.fillStyle = G;
                Ctx.beginPath(); Ctx.arc(Ex, Ey, 15, 0, Math.PI * 2); Ctx.fill();
                Ctx.fillStyle = '#0e0e0e'; Ctx.font = '700 8px "DM Sans", sans-serif'; Ctx.textAlign = 'center'; Ctx.fillText(Lbl, Ex, Ey + 3);
            }
            // Live animated discharge bolt or beam
            const IsBeam = Cls.IsBeam;
            if (IsBeam) {
                Ctx.strokeStyle = RampSample(P, 0.6, 0.35);
                Ctx.lineWidth = 10 + PulseFlash * 6;
                Ctx.beginPath(); Ctx.moveTo(Ax + 10, Ay); Ctx.lineTo(Bx - 10, By); Ctx.stroke();
                Ctx.strokeStyle = '#ffffff';
                Ctx.lineWidth = 2.6;
                Ctx.beginPath(); Ctx.moveTo(Ax + 10, Ay); Ctx.lineTo(Bx - 10, By); Ctx.stroke();
            } else {
                const Segs = 12;
                const Amp = 12 + (P.arcJitter ?? 0.35) * 24 + PulseFlash * 10;
                Ctx.strokeStyle = Col1;
                Ctx.lineWidth = 2.2;
                Ctx.beginPath();
                Ctx.moveTo(Ax + 10, Ay);
                for (let S = 1; S < Segs; S++) {
                    const U = S / Segs;
                    const Off = Math.sin(S * 7.3 + Math.floor(T * 18) * 3.1) * Amp * Math.sin(U * Math.PI);
                    Ctx.lineTo(Ax + 10 + (Bx - Ax - 20) * U, Ay + Off);
                }
                Ctx.lineTo(Bx - 10, By);
                Ctx.stroke();
            }
            DrawRingGauge(Ctx, W * 0.83, H * 0.52, 32, Math.min(1, Gap / 8), Col1, 'GAP SPAN', `${Gap.toFixed(1)}m`, IsBeam ? 'BEAM' : `${P.arcCount || 1} bolt`);
            DrawPill(Ctx, IsBeam ? 'COLLIMATED BEAM LINK' : 'DIELECTRIC DISCHARGE', 10, 11, 'left');
            DrawPill(Ctx, 'CLICK TO STRIKE', W - 10, 11, 'right', '#f0cfa0', 'rgba(240,207,160,0.14)');
            Caption.textContent = 'Drag electrodes A & B in 3D viewport · Click here to fire an instant surge';
        }
        else if (Archetype === 'dyn') {
            const BaseX = 24, TopY = 30, SpanW = W * 0.56, BaseH = H - 46;
            const Sag = Math.max(0.05, Math.min(1.3, P.dynSag ?? 0.35));
            const Stiff = Math.max(0.3, Math.min(3.5, P.dynStiff ?? 1.25));
            const Wave = T * (P.dynWaveSpeed ?? 2.2);
            const Strands = 6;
            for (let S = 0; S < Strands; S++) {
                const V = S / Math.max(1, Strands - 1);
                Ctx.strokeStyle = RampSample(P, V, 0.82);
                Ctx.lineWidth = 2.2;
                Ctx.beginPath();
                for (let K = 0; K <= 16; K++) {
                    const U = K / 16;
                    const Sway = Math.sin(Wave - U * 3.4 + S * 0.45) * (10 / Math.sqrt(Stiff)) * U;
                    const Droop = Math.sin(U * Math.PI) * Sag * 32;
                    const X = BaseX + U * SpanW;
                    const Y = TopY + 14 + V * (BaseH - 36) + Droop + Sway * 0.45;
                    K === 0 ? Ctx.moveTo(X, Y) : Ctx.lineTo(X, Y);
                }
                Ctx.stroke();
            }
            DrawRingGauge(Ctx, W * 0.80, H * 0.52, 33, Math.min(1, Stiff / 3.5), Col1, 'STIFFNESS', `${Stiff.toFixed(1)}×`, `sag ${Sag.toFixed(2)}m`);
            DrawPill(Ctx, 'STRUCTURAL FLEX DIAGRAM', 10, 11, 'left');
            DrawPill(Ctx, 'DRAG SAG / STIFF', W - 10, 11, 'right', '#83d6a1', 'rgba(62,196,109,0.14)');
            Caption.textContent = `Drag horizontally for stiffness, vertically for sag · ${P.dynRows || 28} strands`;
        }
        else if (Archetype === 'beads') {
            const Count = 11, StartX = 20, SpanW = W * 0.58;
            const Spd = P.dynWaveSpeed ?? 1.8;
            Ctx.strokeStyle = 'rgba(255,255,255,0.22)';
            Ctx.lineWidth = 2;
            Ctx.beginPath(); Ctx.moveTo(StartX - 6, 28); Ctx.lineTo(StartX + SpanW + 6, 28); Ctx.stroke();
            for (let I = 0; I < Count; I++) {
                const U = I / (Count - 1);
                const X0 = StartX + U * SpanW;
                const Sway = Math.sin(T * Spd * 1.4 + I * 0.55 + SwipePhase) * 14;
                const Len = 62 + (P.beadsMode === 2 ? Math.sin(U * Math.PI) * 32 : 24);
                Ctx.strokeStyle = RampSample(P, U, 0.85);
                Ctx.lineWidth = P.beadsMode === 2 ? 3.5 : 1.8;
                Ctx.beginPath(); Ctx.moveTo(X0, 28); Ctx.quadraticCurveTo(X0 + Sway * 0.4, 28 + Len * 0.55, X0 + Sway, 28 + Len); Ctx.stroke();
                if (P.beadsMode === 1) {
                    for (let B = 1; B <= 4; B++) {
                        const Tt = B / 4;
                        Ctx.fillStyle = RampSample(P, Tt, 0.92);
                        Ctx.beginPath(); Ctx.arc(X0 + Sway * Tt * Tt, 28 + Len * Tt, 2.3, 0, Math.PI * 2); Ctx.fill();
                    }
                }
            }
            DrawRingGauge(Ctx, W * 0.81, H * 0.52, 33, Math.min(1, Spd / 4.5), Col1, 'HARMONIC', `${Spd.toFixed(1)}Hz`, `${P.dynRows || 24} strands`);
            DrawPill(Ctx, 'PENDULUM RESONANCE', 10, 11, 'left');
            DrawPill(Ctx, 'SWIPE TO RIPPLE', W - 10, 11, 'right', '#83d6a1', 'rgba(62,196,109,0.14)');
            Caption.textContent = 'Swipe across the curtain/chimes to excite wave motion and tune resonance';
        }
        else if (Archetype === 'seed') {
            const Cx = W * 0.34, Cy = H * 0.54, R = 42;
            const Thresh = P.dynTearThresh ?? 3.0;
            const Ratio = Math.min(1, Thresh / 8.0);
            Ctx.strokeStyle = 'rgba(255,255,255,0.10)';
            Ctx.setLineDash([3, 3]);
            Ctx.beginPath(); Ctx.arc(Cx, Cy, R * Ratio + 10, 0, Math.PI * 2); Ctx.stroke();
            Ctx.setLineDash([]);
            for (let I = 0; I < 20; I++) {
                const A = (I / 20) * Math.PI * 2 + T * 0.25;
                const Drift = (I % 4 === 0) ? (10 + ((T * 14 + I * 7) % 24)) : 0;
                const R1 = R * 0.72 + Drift;
                Ctx.strokeStyle = RampSample(P, I / 20, Drift > 0 ? 0.5 : 0.88);
                Ctx.lineWidth = 1.2;
                Ctx.beginPath(); Ctx.moveTo(Cx + Math.cos(A) * 8, Cy + Math.sin(A) * 8); Ctx.lineTo(Cx + Math.cos(A) * R1, Cy + Math.sin(A) * R1); Ctx.stroke();
                Ctx.fillStyle = RampSample(P, 0.85, 0.9);
                Ctx.beginPath(); Ctx.arc(Cx + Math.cos(A) * R1, Cy + Math.sin(A) * R1, 2.2, 0, Math.PI * 2); Ctx.fill();
            }
            DrawRingGauge(Ctx, W * 0.79, H * 0.52, 33, Ratio, Col1, 'DETACH', `${Thresh.toFixed(1)}`, 'm/s gust');
            DrawPill(Ctx, 'SEED CROWN RETENTION', 10, 11, 'left');
            DrawPill(Ctx, 'DRAG THRESHOLD', W - 10, 11, 'right', '#83d6a1', 'rgba(62,196,109,0.14)');
            Caption.textContent = 'Drag radially from the crown to tune the wind detachment gust threshold';
        }
        else if (Archetype === 'jelly') {
            const Cx = W * 0.35, Cy = H * 0.54;
            const Freq = P.dynWaveSpeed ?? 4.5;
            const Sq = Math.sin(T * Freq) * (0.18 + PulseFlash * 0.14);
            const Rx = 36 * (1 + Sq), Ry = 34 * (1 - Sq);
            const G = Ctx.createRadialGradient(Cx - 8, Cy - 8, 4, Cx, Cy, Math.max(Rx, Ry));
            G.addColorStop(0, RampSample(P, 0.9, 0.95));
            G.addColorStop(0.65, RampSample(P, 0.45, 0.72));
            G.addColorStop(1, RampSample(P, 0.1, 0.25));
            Ctx.fillStyle = G;
            Ctx.beginPath(); Ctx.ellipse(Cx, Cy, Rx, Ry, 0, 0, Math.PI * 2); Ctx.fill();
            Ctx.strokeStyle = Col1; Ctx.lineWidth = 1.5; Ctx.stroke();
            DrawRingGauge(Ctx, W * 0.79, H * 0.52, 33, Math.min(1, P.bounce ?? 0.65), Col1, 'BOUNCE', `${Math.round((P.bounce ?? 0.65) * 100)}%`, `${Freq.toFixed(1)}Hz`);
            DrawPill(Ctx, 'ELASTIC VOLUME BLOB', 10, 11, 'left');
            DrawPill(Ctx, 'DRAG WOBBLE / BOUNCE', W - 10, 11, 'right', '#83d6a1', 'rgba(62,196,109,0.14)');
            Caption.textContent = 'Drag X for wobble frequency, Y for bounce restitution · Throw Ball to stretch tendrils';
        }
        else if (Archetype === 'web') {
            const Cx = W * 0.36, Cy = H * 0.54, R = 48;
            const Spokes = Math.max(6, Math.min(28, Math.round(P.webSpokes ?? 18)));
            const Rings = Math.max(4, Math.min(16, Math.round(P.webRings ?? 14)));
            for (let S = 0; S < Spokes; S++) {
                const A = (S / Spokes) * Math.PI * 2;
                Ctx.strokeStyle = 'rgba(255,255,255,0.22)';
                Ctx.lineWidth = 0.9;
                Ctx.beginPath(); Ctx.moveTo(Cx, Cy); Ctx.lineTo(Cx + Math.cos(A) * R, Cy + Math.sin(A) * R); Ctx.stroke();
            }
            const Droop = (P.webSag ?? 0.16) * 8;
            for (let Rg = 1; Rg <= Math.min(7, Rings); Rg++) {
                const Fr = Rg / Math.min(7, Rings);
                Ctx.strokeStyle = RampSample(P, Fr, 0.75);
                Ctx.lineWidth = 1.0;
                Ctx.beginPath();
                for (let S = 0; S <= Spokes; S++) {
                    const A = (S / Spokes) * Math.PI * 2;
                    const X = Cx + Math.cos(A) * R * Fr, Y = Cy + Math.sin(A) * R * Fr + Droop * Fr;
                    S === 0 ? Ctx.moveTo(X, Y) : Ctx.lineTo(X, Y);
                }
                Ctx.stroke();
            }
            DrawRingGauge(Ctx, W * 0.80, H * 0.52, 33, Math.min(1, Spokes / 28), Col1, 'ARCHITECTURE', `${Spokes}×${Rings}`, `sag ${(P.webSag ?? 0.16).toFixed(2)}`);
            DrawPill(Ctx, 'SILK WEB SCHEMATIC', 10, 11, 'left');
            DrawPill(Ctx, 'DRAG SILK SAG', W - 10, 11, 'right', '#83d6a1', 'rgba(62,196,109,0.14)');
            Caption.textContent = 'Drag vertically to adjust catenary silk droop · Live spoke × spiral layout';
        }
        else if (Archetype === 'mud') {
            const Cx = W * 0.36, Cy = H * 0.54;
            if (P.mudSplashMode === 2) {
                // Figure-8 rally track radar
                Ctx.strokeStyle = 'rgba(255,255,255,0.16)';
                Ctx.lineWidth = 8;
                Ctx.beginPath();
                for (let I = 0; I <= 48; I++) {
                    const A = (I / 48) * Math.PI * 2;
                    const X = Cx + Math.sin(A) * 58, Y = Cy + Math.sin(A) * Math.cos(A) * 46;
                    I === 0 ? Ctx.moveTo(X, Y) : Ctx.lineTo(X, Y);
                }
                Ctx.stroke();
                const Ph = (T * (P.carSpeed || 4.2)) / Math.max(1.2, P.carTrackRadius || 3.15);
                const CarX = Cx + Math.sin(Ph) * 58, CarY = Cy + Math.sin(Ph) * Math.cos(Ph) * 46;
                Ctx.fillStyle = Col1;
                Ctx.beginPath(); Ctx.arc(CarX, CarY, 5.5, 0, Math.PI * 2); Ctx.fill();
            } else {
                for (let K = 0; K < 4; K++) {
                    const R = ((T * 28 + K * 14) % 48) + 6;
                    Ctx.strokeStyle = RampSample(P, K / 4, Math.max(0.08, 1 - R / 54));
                    Ctx.lineWidth = 1.6;
                    Ctx.beginPath(); Ctx.arc(Cx, Cy, R, 0, Math.PI * 2); Ctx.stroke();
                }
            }
            DrawRingGauge(Ctx, W * 0.80, H * 0.52, 33, FillRatio, Col1, 'DROPLETS', `${Alive}`, `/${Cap}`);
            DrawPill(Ctx, P.mudSplashMode === 2 ? 'FIGURE-8 TRACK RADAR' : 'RIPPLE BASIN RADAR', 10, 11, 'left');
            DrawPill(Ctx, 'CLICK TO SPLASH', W - 10, 11, 'right', '#f0cfa0', 'rgba(240,207,160,0.14)');
            Caption.textContent = 'Click the basin to inject a fresh splash burst';
        }
        else if (Archetype === 'curl') {
            const Cx = W * 0.36, Cy = H * 0.54;
            const Amp = P.curlAmp ?? 2.2, Freq = P.curlFreq ?? 0.85;
            for (let L = 0; L < 7; L++) {
                Ctx.strokeStyle = RampSample(P, L / 6, 0.82);
                Ctx.lineWidth = 1.8;
                Ctx.beginPath();
                for (let S = 0; S <= 20; S++) {
                    const U = S / 20;
                    const X = 18 + U * (W * 0.54);
                    const Y = 30 + (L / 6) * (H - 48) + Math.sin(U * 5 * Freq + T * Amp * 0.9 + L * 0.8) * (7 + Amp * 1.8);
                    S === 0 ? Ctx.moveTo(X, Y) : Ctx.lineTo(X, Y);
                }
                Ctx.stroke();
            }
            DrawRingGauge(Ctx, W * 0.81, H * 0.52, 33, Math.min(1, Amp / 6), Col1, 'CURL AMP', `${Amp.toFixed(1)}`, `f ${Freq.toFixed(2)}`);
            DrawPill(Ctx, 'CURL VECTOR STREAMLINES', 10, 11, 'left');
            DrawPill(Ctx, 'DRAG AMP / FREQ', W - 10, 11, 'right', '#83d6a1', 'rgba(62,196,109,0.14)');
            Caption.textContent = 'Drag horizontally for curl drive amplitude, vertically for spatial frequency';
        }
        else if (Archetype === 'transition') {
            const Tr = P.transition || {};
            const WaveName = (Tr.wave || 'corner').toUpperCase();
            const Cell = 17, Gap = 4, StartX = 24, StartY = 34;
            const Cyc = (T * 0.42) % 1;
            for (let R = 0; R < 4; R++) {
                for (let C = 0; C < 4; C++) {
                    const Ord = Tr.wave === 'melt' ? (3 - R) / 3 : (Tr.wave === 'glitch' ? ((C * 7 + R * 11) % 16) / 15 : (C + R) / 6);
                    const Released = Cyc > Ord * 0.72;
                    const Lift = Released ? Math.min(1, (Cyc - Ord * 0.72) * 3.2) : 0;
                    const X = StartX + C * (Cell + Gap) + (Tr.wave === 'glitch' && Released ? Math.sin(T * 12 + C) * 4 : 0);
                    const Y = StartY + R * (Cell + Gap) - Lift * 8;
                    Ctx.fillStyle = RampSample(P, Released ? 0.85 : 0.25, Math.max(0.15, 1 - Lift * 0.65));
                    Ctx.fillRect(X, Y, Cell, Cell);
                }
            }
            DrawRingGauge(Ctx, W * 0.78, H * 0.52, 34, Cyc, Col1, `${WaveName} WAVE`, `${Math.round(Cyc * 100)}%`, Tr.assemble ? 'ASSEMBLE' : 'DEREZ');
            DrawPill(Ctx, 'VOXEL RELEASE WAVE', 10, 11, 'left');
            DrawPill(Ctx, 'CLICK TO REPLAY', W - 10, 11, 'right', '#f0cfa0', 'rgba(240,207,160,0.14)');
            Caption.textContent = 'Click anywhere on the card to replay the mesh transition cycle from frame 0';
        }
        else if (Archetype === 'shield') {
            const Cx = W * 0.35, Cy = H * 0.54, R = 44;
            if (P.shape === 15) {
                // Tactical Radar / Sonar scope
                for (let K = 1; K <= 3; K++) {
                    Ctx.strokeStyle = 'rgba(255,255,255,0.12)';
                    Ctx.beginPath(); Ctx.arc(Cx, Cy, R * (K / 3), 0, Math.PI * 2); Ctx.stroke();
                }
                const Ang = T * (P.shieldSpin || 1.2) * 2.2;
                const Grad = Ctx.createRadialGradient(Cx, Cy, 2, Cx, Cy, R);
                Grad.addColorStop(0, RampSample(P, 0.9, 0.55));
                Grad.addColorStop(1, RampSample(P, 0.5, 0.05));
                Ctx.fillStyle = Grad;
                Ctx.beginPath(); Ctx.moveTo(Cx, Cy); Ctx.arc(Cx, Cy, R, Ang - 0.85, Ang); Ctx.closePath(); Ctx.fill();
                Ctx.strokeStyle = Col1; Ctx.lineWidth = 2;
                Ctx.beginPath(); Ctx.moveTo(Cx, Cy); Ctx.lineTo(Cx + Math.cos(Ang) * R, Cy + Math.sin(Ang) * R); Ctx.stroke();
            } else {
                const Sides = P.shieldForm === 2 ? 3 : (P.shieldForm === 3 || P.shieldForm === 5 ? 4 : (P.shieldForm === 4 ? 6 : 24));
                const Rot = (P.shieldForm === 5 ? Math.PI / 4 : -Math.PI / 2) + T * (P.shieldSpin || 0.25);
                for (let Layer = 0; Layer < 2; Layer++) {
                    const Lr = R * (Layer === 0 ? 1 : 0.65);
                    Ctx.strokeStyle = Layer === 0 ? Col1 : ColMid;
                    Ctx.lineWidth = Layer === 0 ? 2 : 1.1;
                    Ctx.beginPath();
                    for (let S = 0; S <= Sides; S++) {
                        const A = Rot + (S / Sides) * Math.PI * 2;
                        S === 0 ? Ctx.moveTo(Cx + Math.cos(A) * Lr, Cy + Math.sin(A) * Lr) : Ctx.lineTo(Cx + Math.cos(A) * Lr, Cy + Math.sin(A) * Lr);
                    }
                    Ctx.closePath(); Ctx.stroke();
                }
            }
            DrawRingGauge(Ctx, W * 0.79, H * 0.52, 33, FillRatio, Col1, 'ACTIVE', `${Alive}`, `/${Cap}`);
            DrawPill(Ctx, P.shape === 15 ? 'PHOSPHOR RADAR SCOPE' : 'TACTICAL FIELD APERTURE', 10, 11, 'left');
            DrawPill(Ctx, 'CLICK TO PULSE', W - 10, 11, 'right', '#f0cfa0', 'rgba(240,207,160,0.14)');
            Caption.textContent = 'Click the schematic to emit an immediate tactical pulse';
        }
        else if (Archetype === 'lens') {
            const Cx = W * 0.36, Cy = H * 0.54;
            if (P.blackHole) {
                const Ein = P.blackHole.einstein ?? 1.25;
                const R = Math.min(44, 16 + Ein * 16);
                const G = Ctx.createRadialGradient(Cx, Cy, R * 0.45, Cx, Cy, R * 1.45);
                G.addColorStop(0, '#000');
                G.addColorStop(0.56, RampSample(P, 0.85, 0.95));
                G.addColorStop(1, 'rgba(0,0,0,0)');
                Ctx.fillStyle = G;
                Ctx.beginPath(); Ctx.arc(Cx, Cy, R * 1.45, 0, Math.PI * 2); Ctx.fill();
                Ctx.fillStyle = '#050507';
                Ctx.beginPath(); Ctx.arc(Cx, Cy, R * 0.52, 0, Math.PI * 2); Ctx.fill();
                DrawRingGauge(Ctx, W * 0.80, H * 0.52, 33, Math.min(1, Ein / 2.5), Col1, 'EINSTEIN', `${Ein.toFixed(2)}m`, 'lensing');
                DrawPill(Ctx, 'RELATIVISTIC LENS DIAL', 10, 11, 'left');
                DrawPill(Ctx, 'DRAG RING RADIUS', W - 10, 11, 'right', '#f0cfa0', 'rgba(240,207,160,0.14)');
                Caption.textContent = 'Drag radially from the event horizon to scale gravitational lensing';
            } else if (P.heatShimmer) {
                const Str = P.heatShimmer.strength ?? 0.018;
                for (let K = 0; K < 5; K++) {
                    Ctx.strokeStyle = RampSample(P, K / 4, 0.65);
                    Ctx.lineWidth = 1.6;
                    Ctx.beginPath();
                    for (let S = 0; S <= 20; S++) {
                        const U = S / 20;
                        const X = 20 + U * (W * 0.50);
                        const Y = 34 + K * 18 + Math.sin(U * 10 - T * 4.5 + K) * (Str * 360);
                        S === 0 ? Ctx.moveTo(X, Y) : Ctx.lineTo(X, Y);
                    }
                    Ctx.stroke();
                }
                DrawRingGauge(Ctx, W * 0.80, H * 0.52, 33, Math.min(1, Str / 0.04), Col1, 'REFRACT', `${(Str * 1000).toFixed(0)}`, 'mrad');
                DrawPill(Ctx, 'THERMAL REFRACTION FIELD', 10, 11, 'left');
                DrawPill(Ctx, 'DRAG STRENGTH', W - 10, 11, 'right', '#f0cfa0', 'rgba(240,207,160,0.14)');
                Caption.textContent = 'Drag horizontally to tune heat-haze index distortion';
            } else {
                // Aurora spectral curtain bars
                const Bars = 18, BarW = (W * 0.56) / Bars;
                for (let B = 0; B < Bars; B++) {
                    const U = B / (Bars - 1);
                    const Bh = 32 + 54 * (0.5 + 0.5 * Math.sin(U * 5.2 + T * 1.4));
                    const G = Ctx.createLinearGradient(0, H - 22, 0, H - 22 - Bh);
                    G.addColorStop(0, RampSample(P, 0.15, 0.9));
                    G.addColorStop(0.6, RampSample(P, 0.55, 0.65));
                    G.addColorStop(1, RampSample(P, 0.95, 0.0));
                    Ctx.fillStyle = G;
                    Ctx.fillRect(16 + B * BarW, H - 22 - Bh, Math.max(2, BarW - 3), Bh);
                }
                DrawRingGauge(Ctx, W * 0.81, H * 0.52, 33, FillRatio, Col1, 'CURTAINS', `${Alive}`, `/${Cap}`);
                DrawPill(Ctx, 'SPECTRAL AURORA RIBBONS', 10, 11, 'left');
                Caption.textContent = 'Geomagnetic vertical spectral curtain emission profile';
            }
        }
        else {
            // Standard Emitter: Left Capacity Ring + Center Interactive Launch Cone / Force Triad + Bottom Colour-Ramp Lifecycle Strip
            DrawRingGauge(Ctx, 54, 66, 30, FillRatio, Col1, 'CAPACITY', `${Math.round(FillRatio * 100)}%`, `${Alive}/${Cap}`);

            // Interactive Launch Cone & Force Triad
            const Cx = W * 0.61, Cy = 94, R = 52;
            const Spread = Math.max(0.08, Math.min(1.45, (P.spread ?? 0.45) * 0.85));
            const Dir = P.dir || [0, 1, 0];
            const Tilt = Math.atan2(Dir[0], Math.max(0.08, Math.abs(Dir[1]))) * 0.65;
            const A0 = -Math.PI * 0.5 + Tilt - Spread;
            const A1 = -Math.PI * 0.5 + Tilt + Spread;
            Ctx.strokeStyle = 'rgba(255,255,255,0.08)';
            Ctx.lineWidth = 1;
            Ctx.beginPath(); Ctx.arc(Cx, Cy, R, Math.PI, Math.PI * 2); Ctx.stroke();
            Ctx.beginPath(); Ctx.arc(Cx, Cy, R * 0.55, Math.PI, Math.PI * 2); Ctx.stroke();

            const Grad = Ctx.createRadialGradient(Cx, Cy, 2, Cx, Cy, R);
            Grad.addColorStop(0, RampSample(P, 0.1, 0.65));
            Grad.addColorStop(0.55, RampSample(P, 0.5, 0.34));
            Grad.addColorStop(1, RampSample(P, 0.9, 0.04));
            Ctx.fillStyle = Grad;
            Ctx.beginPath();
            Ctx.moveTo(Cx, Cy);
            Ctx.arc(Cx, Cy, R, A0, A1);
            Ctx.closePath();
            Ctx.fill();
            Ctx.strokeStyle = Col1;
            Ctx.lineWidth = 1.4;
            Ctx.beginPath();
            Ctx.moveTo(Cx, Cy); Ctx.lineTo(Cx + Math.cos(A0) * R, Cy + Math.sin(A0) * R);
            Ctx.moveTo(Cx, Cy); Ctx.lineTo(Cx + Math.cos(A1) * R, Cy + Math.sin(A1) * R);
            Ctx.stroke();

            // Animated mini particles spraying up the launch cone
            const SpdFactor = Math.min(1, Math.max(0.2, (P.speedMax ?? 3) / 10));
            for (let I = 0; I < 8; I++) {
                const Ph = ((T * (0.7 + SpdFactor) + I * 0.125) % 1);
                const Ang = A0 + (A1 - A0) * (((I * 37) % 11) / 10);
                const Px = Cx + Math.cos(Ang) * R * Ph;
                const Py = Cy + Math.sin(Ang) * R * Ph + (P.gravity ?? 0) * Ph * Ph * 5;
                Ctx.fillStyle = RampSample(P, Ph, (1 - Ph * 0.6) * 0.95);
                Ctx.beginPath(); Ctx.arc(Px, Py, 1.8 + (1 - Ph) * 1.6, 0, Math.PI * 2); Ctx.fill();
            }
            Ctx.fillStyle = Col0;
            Ctx.beginPath(); Ctx.arc(Cx, Cy, 4.5 + PulseFlash * 3, 0, Math.PI * 2); Ctx.fill();

            // Bottom Lifecycle Colour & Size Bead Strip (Birth -> Mid -> Fade)
            const StripX = 16, StripY = H - 18, StripW = W - 32;
            const Beads = 11;
            const S0 = Math.max(0.02, P.sizeStart ?? 0.2), S1 = Math.max(0.01, P.sizeEnd ?? 0.05);
            const MaxS = Math.max(S0, S1, 0.05);
            for (let I = 0; I < Beads; I++) {
                const U = I / (Beads - 1);
                const Sz = S0 * (1 - U) + S1 * U;
                const Rad = 2.2 + (Sz / MaxS) * 4.2;
                Ctx.fillStyle = RampSample(P, U, 0.92);
                Ctx.beginPath(); Ctx.arc(StripX + U * StripW, StripY, Rad, 0, Math.PI * 2); Ctx.fill();
            }
            DrawPill(Ctx, `CONE ${Math.round((P.spread ?? 0.45) * 57.3)}° · ${(P.speedMax ?? 0).toFixed(1)}m/s`, W - 10, 11, 'right', '#83d6a1', 'rgba(62,196,109,0.14)');
            DrawPill(Ctx, 'LIFECYCLE & LAUNCH', 10, 11, 'left');
            Caption.textContent = 'Drag the launch cone to tune spread & speed · Click capacity ring to fire a burst';
        }
    }

    const Ro = new ResizeObserver(() => Draw(LastLive));
    Ro.observe(Canvas);
    Draw(LastLive);
    return { Wrap, Refresh: Draw, Destroy: () => Ro.disconnect() };
}

// Interactive Top-Down Radial Reach Envelope & Vector Swirl Map for Force Field Sources (replaces C.CurvePlot)
export function ForceFieldInfographic(Parent, Scene, Src, FC, OnMutate) {
    const Height = 148;
    const Wrap = document.createElement('div');
    Wrap.className = 'diagram-wrap infographic-wrap';
    const Canvas = document.createElement('canvas');
    Canvas.className = 'infographic-canvas';
    Canvas.style.cssText = `width:100%;height:${Height}px;display:block;cursor:ew-resize;touch-action:none;`;
    const Caption = document.createElement('div');
    Caption.className = 'plot-caption infographic-caption';
    Wrap.append(Canvas, Caption);
    Parent.append(Wrap);

    let Dragging = false;
    const ContribColor = C => C === 'Accelerate' ? '#f0cfa0' : (C === 'Damp' ? '#a78bfa' : (C === 'Formula' ? '#f5b041' : '#52d1a0'));

    const UpdateRadiusFromPointer = (X, Y, W, H) => {
        const Cx = W * 0.36, Cy = H * 0.54;
        const Dist = Math.hypot(X - Cx, Y - Cy);
        const NewR = Math.max(0.5, Math.min(24, +(Dist / 44 * 6.0).toFixed(1)));
        Src.Radius = NewR;
        OnMutate?.();
        Refresh();
    };

    Canvas.addEventListener('pointerdown', E => {
        const Rect = Canvas.getBoundingClientRect();
        Dragging = true;
        Canvas.setPointerCapture?.(E.pointerId);
        UpdateRadiusFromPointer(E.clientX - Rect.left, E.clientY - Rect.top, Rect.width, Rect.height);
    });
    Canvas.addEventListener('pointermove', E => {
        if (!Dragging) return;
        const Rect = Canvas.getBoundingClientRect();
        UpdateRadiusFromPointer(E.clientX - Rect.left, E.clientY - Rect.top, Rect.width, Rect.height);
    });
    Canvas.addEventListener('pointerup', () => { Dragging = false; });
    Canvas.addEventListener('pointercancel', () => { Dragging = false; });

    function Refresh() {
        const { Ctx, W, H } = SetupCanvas(Canvas, Wrap, Height);
        const Meta = (FC?.TypeById && FC.TypeById[Src.Type]) || { Name: Src.Type, Contribution: 'Flow' };
        const Accent = Src.UseFormula ? '#ffb454' : (Meta.Colour || ContribColor(Meta.Contribution));
        const Cx = W * 0.36, Cy = H * 0.54;
        const RadM = Math.max(0.5, Src.Radius || 4);
        const VisualR = Math.max(18, Math.min(48, (RadM / 8) * 44));
        const Reach = Src.Reach || 'Sphere';

        // Concentric falloff zone rings
        for (let K = 3; K >= 1; K--) {
            const Fr = K / 3;
            Ctx.strokeStyle = Accent;
            Ctx.globalAlpha = K === 3 ? 0.55 : 0.18;
            Ctx.lineWidth = K === 3 ? 1.6 : 1;
            Ctx.setLineDash(K === 3 ? [4, 3] : []);
            Ctx.beginPath();
            if (Reach === 'Box') Ctx.rect(Cx - VisualR * Fr, Cy - VisualR * Fr, VisualR * 2 * Fr, VisualR * 2 * Fr);
            else Ctx.arc(Cx, Cy, VisualR * Fr, 0, Math.PI * 2);
            Ctx.stroke();
        }
        Ctx.setLineDash([]);
        Ctx.globalAlpha = 1;

        // Directional / Swirl / Radial field arrows inside reach envelope
        const Bearing = ((Src.Bearing ?? 70) * Math.PI) / 180;
        const DirX = Math.sin(Bearing), DirY = -Math.cos(Bearing);
        const IsSwirl = Src.Type === 'Tornado' || Src.Type === 'Orbit' || Src.Type === 'Magnetic';
        const IsRadial = Src.Type === 'Attractor' || Src.Type === 'Repulsor' || Src.Type === 'Blast' || Src.Type === 'Microburst';
        const Sign = (Src.Type === 'Attractor') ? -1 : 1;

        for (let Ring = 0; Ring < 2; Ring++) {
            const N = Ring === 0 ? 4 : 8;
            const R = VisualR * (Ring === 0 ? 0.38 : 0.74);
            for (let I = 0; I < N; I++) {
                const A = (I / N) * Math.PI * 2;
                const Px = Cx + Math.cos(A) * R, Py = Cy + Math.sin(A) * R;
                let Vx = DirX, Vy = DirY;
                if (IsSwirl) { Vx = -Math.sin(A); Vy = Math.cos(A); }
                else if (IsRadial) { Vx = Math.cos(A) * Sign; Vy = Math.sin(A) * Sign; }
                const Len = 9;
                const Ex = Px + Vx * Len, Ey = Py + Vy * Len;
                Ctx.strokeStyle = Accent;
                Ctx.lineWidth = 1.4;
                Ctx.beginPath(); Ctx.moveTo(Px, Py); Ctx.lineTo(Ex, Ey); Ctx.stroke();
                const Ha = Math.atan2(Vy, Vx);
                Ctx.beginPath();
                Ctx.moveTo(Ex, Ey);
                Ctx.lineTo(Ex - Math.cos(Ha - 0.5) * 4, Ey - Math.sin(Ha - 0.5) * 4);
                Ctx.moveTo(Ex, Ey);
                Ctx.lineTo(Ex - Math.cos(Ha + 0.5) * 4, Ey - Math.sin(Ha + 0.5) * 4);
                Ctx.stroke();
            }
        }
        // Center origin node + outer drag handle
        Ctx.fillStyle = Accent;
        Ctx.beginPath(); Ctx.arc(Cx, Cy, 4.5, 0, Math.PI * 2); Ctx.fill();
        Ctx.fillStyle = '#fff';
        Ctx.beginPath(); Ctx.arc(Cx + VisualR, Cy, 4, 0, Math.PI * 2); Ctx.fill();

        const Str = Number.isFinite(Src.Strength) ? Src.Strength : 0;
        DrawRingGauge(Ctx, W * 0.80, H * 0.52, 33, Math.min(1, Math.abs(Str) / 14), Accent, (Src.Falloff || 'Smooth').toUpperCase(), `${RadM.toFixed(1)}m`, Reach);
        DrawPill(Ctx, `${Meta.Contribution.toUpperCase()} · ${Reach.toUpperCase()}`, 10, 11, 'left', Accent, 'rgba(255,255,255,0.06)');
        DrawPill(Ctx, 'DRAG REACH RIM', W - 10, 11, 'right', '#83d6a1', 'rgba(62,196,109,0.14)');
        Caption.textContent = `Centre (${(Src.Centre?.[0] ?? 0).toFixed(1)}, ${(Src.Centre?.[1] ?? 0).toFixed(1)}, ${(Src.Centre?.[2] ?? 0).toFixed(1)}) m · Drag node in 3D viewport or drag rim here`;
    }

    const Ro = new ResizeObserver(Refresh);
    Ro.observe(Canvas);
    Refresh();
    return { Wrap, Refresh, Destroy: () => Ro.disconnect() };
}

// 8-Sector Wind Rose & Velocity Distribution Infographic (replaces plotted line trace on Field Overview)
export function DrawWindRoseInfographic(Canvas, Speed, BearingDeg, History = []) {
    if (!Canvas) return;
    const Dpr = Math.min(2, window.devicePixelRatio || 1);
    const W = Canvas.clientWidth || 160, H = Canvas.clientHeight || 56;
    if (Canvas.width !== Math.round(W * Dpr) || Canvas.height !== Math.round(H * Dpr)) {
        Canvas.width = Math.round(W * Dpr);
        Canvas.height = Math.round(H * Dpr);
    }
    const Ctx = Canvas.getContext('2d');
    Ctx.setTransform(Dpr, 0, 0, Dpr, 0, 0);
    Ctx.clearRect(0, 0, W, H);

    // 8-bar Beaufort energy spectrum + directional compass rose on left
    const Cx = 26, Cy = H * 0.5, R = Math.min(20, H * 0.40);
    Ctx.strokeStyle = 'rgba(255,255,255,0.10)';
    Ctx.lineWidth = 1;
    Ctx.beginPath(); Ctx.arc(Cx, Cy, R, 0, Math.PI * 2); Ctx.stroke();
    const Rad = ((BearingDeg || 0) * Math.PI) / 180;
    const Len = Math.max(5, Math.min(R - 2, (Speed / 12) * (R - 2)));
    Ctx.strokeStyle = '#3ec46d';
    Ctx.lineWidth = 2.2;
    Ctx.beginPath(); Ctx.moveTo(Cx, Cy); Ctx.lineTo(Cx + Math.sin(Rad) * Len, Cy - Math.cos(Rad) * Len); Ctx.stroke();
    Ctx.fillStyle = '#3ec46d';
    Ctx.beginPath(); Ctx.arc(Cx + Math.sin(Rad) * Len, Cy - Math.cos(Rad) * Len, 2.5, 0, Math.PI * 2); Ctx.fill();

    // Right: crisp 10-band speed intensity meter bars (not a plotted line graph!)
    const Bars = 10, Bx = 58, Bw = (W - Bx - 6) / Bars;
    const Peak = Math.max(10, Speed * 1.25);
    const Ratio = Math.min(1, Speed / Peak);
    for (let I = 0; I < Bars; I++) {
        const BarU = (I + 1) / Bars;
        const Active = Ratio >= BarU - 0.08;
        const Bh = 10 + BarU * (H - 20);
        const Col = I >= 8 ? '#ff5c5c' : (I >= 5 ? '#e8c27a' : '#3ec46d');
        Ctx.fillStyle = Active ? Col : 'rgba(255,255,255,0.08)';
        const Rx = Bx + I * Bw, Ry = H - 8 - Bh;
        Ctx.beginPath();
        if (Ctx.roundRect) Ctx.roundRect(Rx, Ry, Math.max(3, Bw - 3), Bh, 2);
        else Ctx.rect(Rx, Ry, Math.max(3, Bw - 3), Bh);
        Ctx.fill();
    }
}
