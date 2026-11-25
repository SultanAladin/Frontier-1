/*==========================================================================================================================================
                                                         STUDIOCONTROLS.JS
==========================================================================================================================================*/
// Shared 3D Clay Studio (Sun Inspector + Fluid Editor + Vehicle Lab) UI Primitives for SDF Terrain Studio:
// - Coherent flat SVG glyphs (ConstructGlyph)
// - 22px Fluid parameter cards (.property-group)
// - ControlPanel::SliderPill (92px black scrubbable value-pill + spherical-thumb .pill-range)
// - KPI highlight trios (#eff4cf), Sun quick-control tiles, and Interactive Boundary Falloff Curve Editor

import { EvaluateBoundaryFalloff } from './Engine/TerrainLayerStack.js';

const SvgNs = 'http://www.w3.org/2000/svg';
export const Clamp = (V, Lo, Hi) => (V < Lo ? Lo : V > Hi ? Hi : V);

export const GlyphPaths = {
    Mountain:  '<path d="m8 3 4 8 5-5 5 15H2L8 3z"/>',
    Map:       '<path d="M3 6l6-3 6 3 6-3v15l-6 3-6-3-6 3z"/><path d="M9 3v15M15 6v15"/>',
    Planet:    '<circle cx="12" cy="12" r="8.5"/><path d="M3.6 9h16.8M3.6 15h16.8M12 3.5a13 13 0 0 1 0 17M12 3.5a13 13 0 0 0 0 17"/>',
    Columns:   '<rect x="3" y="4" width="18" height="16" rx="2.5"/><path d="M12 4v16"/>',
    Spline:    '<circle cx="5" cy="18" r="2.2"/><circle cx="12" cy="7" r="2.2"/><circle cx="19" cy="15" r="2.2"/><path d="M6.6 16.5C8.5 12 9.8 7 12 7s3.6 6 5.4 7.5"/>',
    Gizmo:     '<path d="M12 3v18M12 3 9.2 6M12 3l2.8 3M12 21l-2.8-3M12 21l2.8-3M4 13h16M20 13l-2.6-2.4M20 13l-2.6 2.4"/>',
    Plus:      '<path d="M12 5v14M5 12h14"/>',
    Trash:     '<path d="M4.5 7h15M9.5 7V4.5h5V7M6.5 7l1 13h9l1-13"/>',
    Eye:       '<path d="M2 12s3.6-7 10-7 10 7 10 7-3.6 7-10 7S2 12 2 12z"/><circle cx="12" cy="12" r="3"/>',
    EyeOff:    '<path d="M3 3l18 18"/><path d="M10.6 5.1A10.4 10.4 0 0 1 12 5c6.4 0 10 7 10 7a17.6 17.6 0 0 1-2.6 3.5M6.2 6.3C3.6 8.1 2 12 2 12s3.6 7 10 7c1.7 0 3.2-.5 4.5-1.2"/>',
    Search:    '<circle cx="11" cy="11" r="7"/><path d="M20 20l-3.8-3.8"/>',
    Layers:    '<path d="M12 3 3 8l9 5 9-5z"/><path d="M3 12.5l9 5 9-5"/><path d="M3 17l9 5 9-5"/>',
    Boxes:     '<path d="M12 2.8l8 4.4v9.6l-8 4.4-8-4.4V7.2z"/><path d="M12 12l8-4.6M12 12v9.2M12 12 4 7.4"/>',
    Scan:      '<path d="M4 8V5.5A1.5 1.5 0 0 1 5.5 4H8M16 4h2.5A1.5 1.5 0 0 1 20 5.5V8M20 16v2.5a1.5 1.5 0 0 1-1.5 1.5H16M8 20H5.5A1.5 1.5 0 0 1 4 18.5V16"/>',
    Brush:     '<path d="m18.5 3.5 2 2-9.5 9.5-3 1 1-3z"/><path d="M5 17c-1.5 1.5-2 3.5-2 3.5s2-.5 3.5-2a2.1 2.1 0 0 0-1.5-1.5z"/>',
    Eraser:    '<path d="m7 21-4.3-4.3a2 2 0 0 1 0-2.8L14.5 2.1a2 2 0 0 1 2.8 0l4.6 4.6a2 2 0 0 1 0 2.8L11 21z"/><path d="M22 21H7"/>',
    Polygon:   '<polygon points="5,4 19,6 20,17 9,20 4,12"/>',
    Move:      '<path d="M12 3v18M3 12h18M9.5 5.5 12 3l2.5 2.5M9.5 18.5 12 21l2.5-2.5M5.5 9.5 3 12l2.5 2.5M18.5 9.5 21 12l-2.5 2.5"/>',
    Sun:       '<circle cx="12" cy="12" r="4"/><path d="M12 2.5v2M12 19.5v2M4.6 4.6l1.4 1.4M18 18l1.4 1.4M2.5 12h2M19.5 12h2M4.6 19.4 6 18M18 6l1.4-1.4"/>',
    Water:     '<path d="M2 12c2-2 4-2 6 0s4 2 6 0 4-2 6 0"/><path d="M2 17c2-2 4-2 6 0s4 2 6 0 4-2 6 0"/>',
    Sliders:   '<path d="M4 6h9M17 6h3M4 12h3M11 12h9M4 18h11M19 18h1"/><circle cx="15" cy="6" r="2"/><circle cx="9" cy="12" r="2"/><circle cx="17" cy="18" r="2"/>',
    Reset:     '<path d="M3.5 12a8.5 8.5 0 1 0 2.6-6.1"/><path d="M3.5 4.5v4.6h4.6"/>',
    Redo:      '<path d="M20.5 12a8.5 8.5 0 1 1-2.6-6.1"/><path d="M20.5 4.5v4.6h-4.6"/>',
    Focus:     '<path d="M4 9V5h4M16 5h4v4M20 15v4h-4M8 19H4v-4"/><circle cx="12" cy="12" r="2.2"/>',
    Maximize:  '<path d="M4 9V4h5M15 4h5v5M20 15v5h-5M9 20H4v-5"/>',
    ArrowUp:   '<path d="M12 19V5M6 11l6-6 6 6"/>',
    ArrowDown: '<path d="M12 5v14M6 13l6 6 6-6"/>',
    Sparkles:  '<path d="M11 3.5l1.7 4.6 4.6 1.7-4.6 1.7L11 16.1l-1.7-4.6-4.6-1.7 4.6-1.7z"/><path d="M18.5 14.5v5M16 17h5"/>',
    Download:  '<path d="M12 3v12m0 0-4-4m4 4 4-4M4 17v2a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2v-2"/>',
    Activity:  '<path d="M3 12h4l3-8 4 16 3-8h4"/>',
    Arch:      '<path d="M4 20V12a8 8 0 0 1 16 0v8h-4v-7a4 4 0 0 0-8 0v7z"/>'
};

export function ConstructGlyph(Name, ClassName = 'glyph')
{
    const Svg = document.createElementNS(SvgNs, 'svg');
    Svg.setAttribute('viewBox', '0 0 24 24');
    Svg.setAttribute('class', ClassName);
    Svg.setAttribute('aria-hidden', 'true');
    Svg.innerHTML = GlyphPaths[Name] || GlyphPaths.Mountain;
    return Svg;
}

export function MakeEl(Tag, ClassName = '', Content = null)
{
    const El = document.createElement(Tag);
    if (ClassName) El.className = ClassName;
    if (Content !== null && Content !== undefined)
    {
        if (typeof Content === 'string' || typeof Content === 'number') El.textContent = String(Content);
        else if (Array.isArray(Content)) Content.forEach(C => { if (C) El.append(C); });
        else El.append(Content);
    }
    return El;
}

/*==========================================================================================================================================
                                         22px FLUID PARAMETER CARD (.property-group)
==========================================================================================================================================*/
export function MakePropertyGroup(Title, GlyphName, BadgeText = '', StartOpen = true, HintText = '')
{
    const Details = MakeEl('details', 'property-group');
    if (StartOpen) Details.open = true;

    const Summary = MakeEl('summary', '', [
        GlyphName ? ConstructGlyph(GlyphName, 'glyph section-glyph') : null,
        MakeEl('span', 'section-title', Title),
        BadgeText ? MakeEl('span', 'section-badge', BadgeText) : null
    ]);

    const Body = MakeEl('div', 'group-content');
    if (HintText) Body.appendChild(MakeEl('p', 'property-hint', HintText));

    Details.append(Summary, Body);
    return { Group: Details, Body };
}

/*==========================================================================================================================================
                              FLUID ControlPanel::SliderPill (92px .value-pill + .pill-range)
==========================================================================================================================================*/
export function MakeSliderPillRow(LabelText, Min, Max, Step, InitialValue, Decimals, UnitSuffix, OnInput, OnCommit = null)
{
    const Row = MakeEl('div', 'property-row');
    Row.appendChild(MakeEl('label', 'property-label', LabelText));

    const Ctrl = MakeEl('div', 'slider-control');
    const Pill = MakeEl('div', 'value-pill');
    const NumInput = MakeEl('input');
    NumInput.type = 'number';
    NumInput.step = String(Step || 0.1);
    const UnitCell = MakeEl('span', 'unit-cell', UnitSuffix || '·');
    Pill.append(NumInput, UnitCell);

    const Range = MakeEl('input', 'pill-range');
    Range.type = 'range';
    Range.min = String(Min);
    Range.max = String(Max);
    Range.step = String(Step || 0.01);

    Ctrl.append(Pill, Range);
    Row.appendChild(Ctrl);

    let Current = Clamp(Number(InitialValue) || 0, Min, Max);

    const Sync = Val =>
    {
        Current = Clamp(Val, Min, Max);
        const T = Max > Min ? ((Current - Min) / (Max - Min)) * 100 : 0;
        Range.value = String(Current);
        Range.style.setProperty('--fill', `${T.toFixed(2)}%`);
        NumInput.value = Decimals === 0 ? String(Math.round(Current)) : Current.toFixed(Decimals);
    };
    Sync(Current);

    Range.addEventListener('input', () =>
    {
        const Next = Clamp(parseFloat(Range.value), Min, Max);
        Sync(Next);
        OnInput?.(Current);
    });
    Range.addEventListener('change', () => OnCommit?.(Current));

    NumInput.addEventListener('change', () =>
    {
        const Parsed = parseFloat(NumInput.value);
        if (Number.isFinite(Parsed))
        {
            Sync(Clamp(Parsed, Min, Max));
            OnInput?.(Current);
            OnCommit?.(Current);
        }
        else
        {
            Sync(Current);
        }
    });

    Row.SetValue = Sync;
    return Row;
}

export function MakeSelectRow(LabelText, OptionsList, CurrentVal, OnChange)
{
    const Row = MakeEl('div', 'property-row');
    if (LabelText) Row.appendChild(MakeEl('label', 'property-label', LabelText));
    const Sel = MakeEl('select', 'studio-select');
    for (const [Label, Val] of OptionsList)
    {
        const Opt = MakeEl('option', '', Label);
        Opt.value = String(Val);
        if (String(Val) === String(CurrentVal)) Opt.selected = true;
        Sel.appendChild(Opt);
    }
    Sel.addEventListener('change', () => OnChange?.(Sel.value));
    Row.appendChild(Sel);
    return Row;
}

/*==========================================================================================================================================
                              INTERACTIVE BOUNDARY FALLOFF PROFILE GRAPH (EDGE -> CENTER PREVIEW)
==========================================================================================================================================*/
export function MakeBoundaryFalloffGraph(FalloffSpec, OnChange, OnCommit = null)
{
    const Wrap = MakeEl('div', 'diagram-wrap');
    const Canvas = MakeEl('canvas', 'falloff-curve-canvas');
    Canvas.width = 280;
    Canvas.height = 96;
    const Caption = MakeEl('p', 'plot-caption');
    Wrap.append(Canvas, Caption);

    const Ctx = Canvas.getContext('2d');
    let DragIdx = -1;

    const Draw = () =>
    {
        const W = Canvas.width, H = Canvas.height;
        const Pad = 11;
        Ctx.clearRect(0, 0, W, H);

        Ctx.strokeStyle = 'rgba(255,255,255,0.06)';
        Ctx.lineWidth = 1;
        for (let I = 1; I < 4; ++I)
        {
            const X = Pad + ((W - Pad * 2) * I) / 4;
            const Y = Pad + ((H - Pad * 2) * I) / 4;
            Ctx.beginPath(); Ctx.moveTo(X, Pad); Ctx.lineTo(X, H - Pad); Ctx.stroke();
            Ctx.beginPath(); Ctx.moveTo(Pad, Y); Ctx.lineTo(W - Pad, Y); Ctx.stroke();
        }

        const DistSpan = Math.max(4.0, Number(FalloffSpec.distance) || 260.0);

        Ctx.beginPath();
        for (let Px = 0; Px <= W - Pad * 2; ++Px)
        {
            const T = Px / (W - Pad * 2);
            const Weight = EvaluateBoundaryFalloff(T * DistSpan, T * DistSpan, FalloffSpec);
            const Cx = Pad + Px;
            const Cy = (H - Pad) - Weight * (H - Pad * 2);
            if (Px === 0) Ctx.moveTo(Cx, Cy);
            else Ctx.lineTo(Cx, Cy);
        }
        Ctx.lineTo(W - Pad, H - Pad);
        Ctx.lineTo(Pad, H - Pad);
        Ctx.closePath();

        const Grad = Ctx.createLinearGradient(Pad, 0, W - Pad, 0);
        Grad.addColorStop(0.0, 'rgba(239, 244, 207, 0.08)');
        Grad.addColorStop(1.0, 'rgba(239, 244, 207, 0.26)');
        Ctx.fillStyle = Grad;
        Ctx.fill();

        Ctx.beginPath();
        for (let Px = 0; Px <= W - Pad * 2; ++Px)
        {
            const T = Px / (W - Pad * 2);
            const Weight = EvaluateBoundaryFalloff(T * DistSpan, T * DistSpan, FalloffSpec);
            const Cx = Pad + Px;
            const Cy = (H - Pad) - Weight * (H - Pad * 2);
            if (Px === 0) Ctx.moveTo(Cx, Cy);
            else Ctx.lineTo(Cx, Cy);
        }
        Ctx.strokeStyle = '#eff4cf';
        Ctx.lineWidth = 2.0;
        Ctx.stroke();

        if (FalloffSpec.mode === 'custom' && Array.isArray(FalloffSpec.points))
        {
            for (let I = 0; I < FalloffSpec.points.length; ++I)
            {
                const [U, V] = FalloffSpec.points[I];
                const Cx = Pad + U * (W - Pad * 2);
                const Cy = (H - Pad) - V * (H - Pad * 2);
                Ctx.beginPath();
                Ctx.arc(Cx, Cy, I === DragIdx ? 5.2 : 4.0, 0, Math.PI * 2);
                Ctx.fillStyle = '#eff4cf';
                Ctx.fill();
            }
        }

        Ctx.font = '400 9.5px "DM Sans", sans-serif';
        Ctx.fillStyle = '#7e7e7e';
        Ctx.fillText('0 m (Coastline)', Pad + 2, H - 2);
        Ctx.textAlign = 'right';
        Ctx.fillText(FalloffSpec.mode === 'none' ? 'Full Cliff (100%)' : `+${Math.round(DistSpan)} m Inland`, W - Pad - 2, H - 2);
        Ctx.textAlign = 'left';

        if (FalloffSpec.mode === 'none')
        {
            Caption.textContent = '100% strength right up to the 2D mask edge — forms sheer vertical sea cliffs.';
        }
        else if (FalloffSpec.mode === 'rim')
        {
            Caption.textContent = 'Peaks along a coastal rim ring, then relaxes into a calm caldera/atoll interior.';
        }
        else if (FalloffSpec.mode === 'custom')
        {
            Caption.textContent = 'Drag curve knots to sculpt custom coastline-to-center falloff.';
        }
        else
        {
            Caption.textContent = `Soft at mask edge (${Math.round((FalloffSpec.edgeFloor || 0) * 100)}%) → 100% strength ${Math.round(DistSpan)} m toward center.`;
        }
    };

    const ToUv = E =>
    {
        const Rect = Canvas.getBoundingClientRect();
        const PadX = (11 / Canvas.width) * Rect.width;
        const PadY = (11 / Canvas.height) * Rect.height;
        const U = Clamp((E.clientX - Rect.left - PadX) / Math.max(1, Rect.width - PadX * 2), 0, 1);
        const V = Clamp(1.0 - (E.clientY - Rect.top - PadY) / Math.max(1, Rect.height - PadY * 2), 0, 1);
        return [U, V];
    };

    Canvas.addEventListener('pointerdown', E =>
    {
        if (FalloffSpec.mode !== 'custom' || !Array.isArray(FalloffSpec.points)) return;
        const [U, V] = ToUv(E);
        let Best = -1, BestD = 0.14;
        for (let I = 0; I < FalloffSpec.points.length; ++I)
        {
            const D = Math.hypot(FalloffSpec.points[I][0] - U, FalloffSpec.points[I][1] - V);
            if (D < BestD) { BestD = D; Best = I; }
        }
        if (Best >= 0)
        {
            DragIdx = Best;
            Canvas.setPointerCapture(E.pointerId);
            Draw();
        }
    });

    Canvas.addEventListener('pointermove', E =>
    {
        if (DragIdx < 0) return;
        const [U, V] = ToUv(E);
        const Pts = FalloffSpec.points;
        if (DragIdx === 0) Pts[0] = [0.0, V];
        else if (DragIdx === Pts.length - 1) Pts[Pts.length - 1] = [1.0, V];
        else Pts[DragIdx] = [Clamp(U, Pts[DragIdx - 1][0] + 0.04, Pts[DragIdx + 1][0] - 0.04), V];
        Draw();
        OnChange?.();
    });

    const EndDrag = E =>
    {
        if (DragIdx >= 0)
        {
            if (Canvas.hasPointerCapture(E.pointerId)) Canvas.releasePointerCapture(E.pointerId);
            DragIdx = -1;
            Draw();
            OnCommit?.();
        }
    };
    Canvas.addEventListener('pointerup', EndDrag);
    Canvas.addEventListener('pointercancel', EndDrag);

    Draw();
    Wrap.Redraw = Draw;
    return Wrap;
}
