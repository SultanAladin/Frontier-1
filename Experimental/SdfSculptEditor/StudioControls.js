/*==========================================================================================================================================
                                                          STUDIOCONTROLS.JS
==========================================================================================================================================*/
// Sun Inspector + Fluid Editor + Vehicle Lab unified control kit for Frontier SDF Sculpt Studio:
// - Coherent flat vector glyphs + locally bundled Font Awesome Free solid icon helper
// - ControlPanel::SliderPill (92px black scrubbable value-pill + 26px spherical-thumb range track)
// - Sun quick-control feature tiles, KPI highlight trios (#eff4cf), PolarDial, Interactive FalloffCurveEditor & Histogram

import { Clamp, LinearToDisplay, DisplayToLinear, FALLOFF_PRESETS, EvaluateFalloff } from './Engine/SdfMath.js';

const SvgNamespace = 'http://www.w3.org/2000/svg';
const Refreshers = new Set();
const ScopeStack = [];

export const GlyphPaths = {
    Monitor:    '<rect x="2.5" y="4" width="19" height="13" rx="2.5"/><path d="M8 21h8M12 17v4"/>',
    Zap:        '<path d="M13 2.5 4.5 14h6.5l-1 7.5L18.5 10H12z"/>',
    Sparkles:   '<path d="M11 3.5l1.7 4.6 4.6 1.7-4.6 1.7L11 16.1l-1.7-4.6-4.6-1.7 4.6-1.7z"/><path d="M18.5 14.5v5M16 17h5"/>',
    Aperture:   '<circle cx="12" cy="12" r="9"/><path d="M14.3 3.3 9.5 11.5M20.7 9.6H11.2M18.4 18.3l-4.8-8.2M9.7 20.7l4.8-8.2M3.3 14.4h9.5M5.6 5.7l4.8 8.2"/>',
    Sun:        '<circle cx="12" cy="12" r="4"/><path d="M12 2.5v2M12 19.5v2M4.6 4.6l1.4 1.4M18 18l1.4 1.4M2.5 12h2M19.5 12h2M4.6 19.4 6 18M18 6l1.4-1.4"/>',
    Layers:     '<path d="M12 3 3 8l9 5 9-5z"/><path d="M3 12.5l9 5 9-5"/><path d="M3 17l9 5 9-5"/>',
    Scan:       '<path d="M4 8V5.5A1.5 1.5 0 0 1 5.5 4H8M16 4h2.5A1.5 1.5 0 0 1 20 5.5V8M20 16v2.5a1.5 1.5 0 0 1-1.5 1.5H16M8 20H5.5A1.5 1.5 0 0 1 4 18.5V16"/><path d="M4.5 12h15"/>',
    Gauge:      '<path d="M3.6 17.5a9 9 0 1 1 16.8 0"/><path d="M12 13.5l4.2-4.2"/><circle cx="12" cy="13.5" r="1.2"/>',
    Boxes:      '<path d="M12 2.8l8 4.4v9.6l-8 4.4-8-4.4V7.2z"/><path d="M12 12l8-4.6M12 12v9.2M12 12 4 7.4"/>',
    Lattice:    '<rect x="3" y="3" width="18" height="18" rx="2.5"/><path d="M3 9h18M3 15h18M9 3v18M15 3v18"/>',
    Activity:   '<path d="M3 12h4l3-8 4 16 3-8h4"/>',
    Chevron:    '<path d="M6 9l6 6 6-6"/>',
    Check:      '<path d="M5 12.5l4.2 4.2L19 7"/>',
    Alert:      '<path d="M12 3.5 2.5 20h19z"/><path d="M12 10v4.5M12 17.4v.2"/>',
    Eye:        '<path d="M2 12s3.6-7 10-7 10 7 10 7-3.6 7-10 7S2 12 2 12z"/><circle cx="12" cy="12" r="3"/>',
    EyeOff:     '<path d="M3 3l18 18"/><path d="M10.6 5.1A10.4 10.4 0 0 1 12 5c6.4 0 10 7 10 7a17.6 17.6 0 0 1-2.6 3.5M6.2 6.3C3.6 8.1 2 12 2 12s3.6 7 10 7c1.7 0 3.2-.5 4.5-1.2"/>',
    Search:     '<circle cx="11" cy="11" r="7"/><path d="M20 20l-3.8-3.8"/>',
    Sliders:    '<path d="M4 6h9M17 6h3M4 12h3M11 12h9M4 18h11M19 18h1"/><circle cx="15" cy="6" r="2"/><circle cx="9" cy="12" r="2"/><circle cx="17" cy="18" r="2"/>',
    Planet:     '<circle cx="12" cy="12" r="6.5"/><ellipse cx="12" cy="12" rx="10" ry="3.6" transform="rotate(-22 12 12)"/>',
    Copy:       '<rect x="9" y="9" width="11.5" height="11.5" rx="2.5"/><path d="M5.5 15H5a1.5 1.5 0 0 1-1.5-1.5V5A1.5 1.5 0 0 1 5 3.5h8.5A1.5 1.5 0 0 1 15 5v.5"/>',
    Reset:      '<path d="M3.5 12a8.5 8.5 0 1 0 2.6-6.1"/><path d="M3.5 4.5v4.6h4.6"/>',
    Redo:       '<path d="M20.5 12a8.5 8.5 0 1 1-2.6-6.1"/><path d="M20.5 4.5v4.6h-4.6"/>',
    Plus:       '<path d="M12 5v14M5 12h14"/>',
    Close:      '<path d="M6.5 6.5l11 11M17.5 6.5l-11 11"/>',
    Target:     '<circle cx="12" cy="12" r="9"/><circle cx="12" cy="12" r="4.5"/><circle cx="12" cy="12" r="0.8"/>',
    Folder:     '<path d="M3 7.5A2.5 2.5 0 0 1 5.5 5h3.6l2 2.2h7.4A2.5 2.5 0 0 1 21 9.7v7.8a2.5 2.5 0 0 1-2.5 2.5h-13A2.5 2.5 0 0 1 3 17.5z"/>',
    Dot:        '<circle cx="12" cy="12" r="3"/>',
    Tornado:    '<path d="M4 5.5c4-1.5 11-1.5 15 0M6 10c3.5-1.2 9-1.2 12.5 0M8.5 14.5c2.5-.8 6-.8 8.5 0M11 19h3.5"/>',
    Attract:    '<circle cx="12" cy="12" r="2.2"/><path d="M3 12h5M16 12h5M12 3v5M12 16v5M6 10l2 2-2 2M18 10l-2 2 2 2"/>',
    Orbit:      '<circle cx="12" cy="12" r="2.4"/><ellipse cx="12" cy="12" rx="9.5" ry="4.4" transform="rotate(-24 12 12)"/>',
    Magnet:     '<path d="M5 3.5h4.5v8a2.5 2.5 0 0 0 5 0v-8H19v8a7 7 0 0 1-14 0z"/><path d="M5 7.5h4.5M14.5 7.5H19"/>',
    Current:    '<ellipse cx="12" cy="6" rx="7" ry="3"/><path d="M5 6v12c0 1.66 3.13 3 7 3s7-1.34 7-3V6"/>',
    Trash:      '<path d="M4.5 7h15M9.5 7V4.5h5V7M6.5 7l1 13h9l1-13"/>',
    Crosshair:  '<circle cx="12" cy="12" r="7"/><path d="M12 2.5v5M12 16.5v5M2.5 12h5M16.5 12h5"/>',
    Compass:    '<circle cx="12" cy="12" r="9"/><path d="M15.5 8.5l-2 5-5 2 2-5z"/>',
    Leaf:       '<path d="M5 19c0-8 5-14 15-14 0 10-6 15-14 15"/><path d="M5 19l8-8"/>',
    Atom:       '<circle cx="12" cy="12" r="1.5"/><ellipse cx="12" cy="12" rx="9.5" ry="3.8"/><ellipse cx="12" cy="12" rx="9.5" ry="3.8" transform="rotate(60 12 12)"/><ellipse cx="12" cy="12" rx="9.5" ry="3.8" transform="rotate(120 12 12)"/>',
    Drop:       '<path d="M12 3s6.5 7 6.5 11.5a6.5 6.5 0 0 1-13 0C5.5 10 12 3 12 3z"/>',
    Move:       '<path d="M12 3v18M3 12h18M9.5 5.5 12 3l2.5 2.5M9.5 18.5 12 21l2.5-2.5M5.5 9.5 3 12l2.5 2.5M18.5 9.5 21 12l-2.5 2.5"/>',
    Palette:    '<path d="M12 3a9 9 0 0 0 0 18c1.4 0 2-1 2-2 0-1.4-1.2-1.6-1.2-2.8 0-1 .8-1.7 1.8-1.7H17a4 4 0 0 0 4-4C21 6.6 17 3 12 3z"/><circle cx="7.5" cy="11" r="1.1"/><circle cx="10" cy="7" r="1.1"/><circle cx="14.5" cy="7" r="1.1"/>',
    Download:   '<path d="M12 3v12m0 0-4-4m4 4 4-4M4 17v2a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2v-2"/>',
    Upload:     '<path d="M12 15V3m0 0-4 4m4-4 4 4M4 17v2a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2v-2"/>',
    Help:       '<circle cx="12" cy="12" r="9"/><path d="M9.5 9.5a2.5 2.5 0 0 1 5 .5c0 2-2.5 2-2.5 4M12 17.5h.01"/>',
    Focus:      '<path d="M4 9V5h4M16 5h4v4M20 15v4h-4M8 19H4v-4"/><circle cx="12" cy="12" r="2.2"/>',
    Maximize:   '<path d="M4 9V4h5M15 4h5v5M20 15v5h-5M9 20H4v-5"/>',
    Terminal:   '<rect x="3" y="4" width="18" height="16" rx="2.5"/><path d="M7.5 9.5 11 12l-3.5 2.5M13 14.5h3.5"/>',
    Grid:       '<rect x="3" y="3" width="7.5" height="7.5" rx="1.5"/><rect x="13.5" y="3" width="7.5" height="7.5" rx="1.5"/><rect x="3" y="13.5" width="7.5" height="7.5" rx="1.5"/><rect x="13.5" y="13.5" width="7.5" height="7.5" rx="1.5"/>',
    Compact:    '<path d="M7 5v14M17 5v14"/>',
    Lock:       '<rect x="5" y="11" width="14" height="9.5" rx="2.5"/><path d="M8 11V7.5a4 4 0 0 1 8 0V11"/>',
    Unlock:     '<rect x="5" y="11" width="14" height="9.5" rx="2.5"/><path d="M8 11V7.5a4 4 0 0 1 7.5-1.8"/>',
    Symmetry:   '<path d="M12 3v18"/><path d="M8.5 7 3.5 12l5 5z"/><path d="M15.5 7l5 5-5 5z"/>',
    Slice:      '<path d="M3 15l9 5 9-5"/><path d="M3 9l9 5 9-5-9-5z"/><path d="M4 4l16 16" stroke-dasharray="2.5 2.5"/>'
};

export function ConstructGlyph(Name, ClassName = 'glyph')
{
    const Svg = document.createElementNS(SvgNamespace, 'svg');
    Svg.setAttribute('viewBox', '0 0 24 24');
    Svg.setAttribute('class', ClassName);
    Svg.setAttribute('aria-hidden', 'true');
    Svg.innerHTML = GlyphPaths[Name] || GlyphPaths.Dot;
    return Svg;
}

export function Construct(Tag, ClassName, Content)
{
    const Element = document.createElement(Tag);
    if (ClassName) Element.className = ClassName;
    if (Content === undefined || Content === null) return Element;
    if (typeof Content === 'string' || typeof Content === 'number') Element.textContent = String(Content);
    else if (Array.isArray(Content)) Content.forEach(Part => { if (Part !== null && Part !== undefined) Element.append(Part); });
    else Element.append(Content);
    return Element;
}

export function ConstructSvg(Tag, Attributes, Text)
{
    const Element = document.createElementNS(SvgNamespace, Tag);
    for (const [Key, Setting] of Object.entries(Attributes || {})) Element.setAttribute(Key, Setting);
    if (Text !== undefined) Element.textContent = Text;
    return Element;
}

export function OpenScope() { const Scope = new Set(); ScopeStack.push(Scope); return Scope; }
export function CloseScope(Scope) { const Top = ScopeStack.pop(); if (Top !== Scope && Scope) ScopeStack.splice(ScopeStack.indexOf(Scope), 1); }
export function ReleaseScope(Scope) { if (!Scope) return; for (const R of Scope) Refreshers.delete(R); Scope.clear(); }
export function Register(Refresher) { Refreshers.add(Refresher); if (ScopeStack.length) ScopeStack[ScopeStack.length - 1].add(Refresher); }
export function Broadcast() { for (const Refresher of Refreshers) Refresher(); }

const CompassPoints = ['N', 'NNE', 'NE', 'ENE', 'E', 'ESE', 'SE', 'SSE', 'S', 'SSW', 'SW', 'WSW', 'W', 'WNW', 'NW', 'NNW'];
export const ResolveCompassPoint = Degrees => CompassPoints[Math.round((((Degrees % 360) + 360) % 360) / 22.5) % 16];

function ResolveState(Binding)
{
    return Binding.Availability ? Binding.Availability() : { Enabled: true };
}

function ApplyAvailability(Node, Binding, Extra)
{
    const State = ResolveState(Binding);
    Node.classList.toggle('unavailable', !State.Enabled);
    if (Extra) Extra(State);
    if (!State.Enabled && State.Reason) Node.title = State.Reason;
    else Node.removeAttribute('title');
    return State;
}

export function SectionCard(Parent, { Id, Title, Glyph, Status, Badge, Hint, Open = true })
{
    const Details = Construct('details', 'property-group');
    if (Open) Details.open = true;
    if (Id) Details.dataset.section = Id;
    const StatusNode = Status ? Construct('span', 'section-badge') : null;
    const Summary = Construct('summary', null, [
        Glyph ? ConstructGlyph(Glyph, 'glyph section-glyph') : null,
        Construct('span', 'section-title', Title),
        StatusNode || (Badge ? Construct('span', 'section-badge', Badge) : null)
    ]);
    const Content = Construct('div', 'group-content');
    if (Hint) Content.append(Construct('p', 'property-hint', Hint));
    Details.append(Summary, Content);
    Parent.append(Details);
    if (StatusNode) Register(() => { StatusNode.textContent = typeof Status === 'function' ? Status() : Status; });
    return { Card: Details, Inner: Content };
}

export function SliderRow(Parent, Binding)
{
    const Step = Binding.Step ?? 0.01;
    const Digits = Binding.Digits ?? (Step >= 1 ? 0 : Step >= 0.1 ? 1 : Step >= 0.01 ? 2 : 3);
    const Label = Construct('label', 'property-label', Binding.Label);
    const NumberInput = Construct('input', 'pill-number');
    NumberInput.type = 'number';
    NumberInput.min = String(Binding.Minimum);
    NumberInput.max = String(Binding.Maximum);
    NumberInput.step = String(Step);
    NumberInput.setAttribute('aria-label', Binding.Label + ' numeric value');
    const UnitCell = Construct('span', 'unit-cell', Binding.Unit || '—');
    const ValuePill = Construct('div', 'value-pill', [NumberInput, UnitCell]);

    const Range = Construct('input', 'pill-range');
    Range.type = 'range';
    Range.min = String(Binding.Minimum);
    Range.max = String(Binding.Maximum);
    Range.step = String(Step);
    Range.setAttribute('aria-label', Binding.Label);

    const Control = Construct('div', 'slider-control', [ValuePill, Range]);
    const Row = Construct('div', 'property-row slider-row', [Label, Control]);
    Parent.append(Row);

    const Snap = Raw =>
    {
        const Clamped = Clamp(Raw, Binding.Minimum, Binding.Maximum);
        const Precision = Math.pow(10, Digits + 2);
        return Math.round(Clamped * Precision) / Precision;
    };
    const SyncFraction = Value =>
    {
        const Span = (Binding.Maximum - Binding.Minimum) || 1;
        const Frac = Clamp((Value - Binding.Minimum) / Span, 0, 1);
        Range.style.setProperty('--fraction', Frac.toFixed(4));
    };

    const Commit = Raw =>
    {
        if (!Number.isFinite(Raw)) return;
        const Value = Snap(Raw);
        Binding.Write(Value);
        SyncFraction(Value);
        Broadcast();
    };

    Range.addEventListener('input', () => Commit(Number(Range.value)));
    NumberInput.addEventListener('change', () => Commit(Number(NumberInput.value)));

    let Scrub = null;
    NumberInput.addEventListener('pointerdown', Event =>
    {
        if (document.activeElement === NumberInput) return;
        Scrub = { X: Event.clientX, Moved: false, Start: Number(Binding.Read()) || 0 };
        NumberInput.setPointerCapture(Event.pointerId);
    });
    NumberInput.addEventListener('pointermove', Event =>
    {
        if (!Scrub) return;
        const Dx = Event.clientX - Scrub.X;
        if (!Scrub.Moved && Math.abs(Dx) < 3) return;
        if (!Scrub.Moved) { Scrub.Moved = true; NumberInput.classList.add('scrubbing'); }
        const Span = Binding.Maximum - Binding.Minimum;
        const Factor = Event.shiftKey ? 0.15 : Event.altKey ? 4.0 : 1.0;
        const Next = Scrub.Start + (Dx / 180) * Span * Factor;
        Commit(Math.round(Next / Step) * Step);
    });
    const EndScrub = Event =>
    {
        if (!Scrub) return;
        const WasMoved = Scrub.Moved;
        Scrub = null;
        NumberInput.classList.remove('scrubbing');
        if (WasMoved) Event.preventDefault();
    };
    NumberInput.addEventListener('pointerup', EndScrub);
    NumberInput.addEventListener('pointercancel', EndScrub);

    Register(() =>
    {
        const Value = Number(Binding.Read());
        Range.value = String(Value);
        if (document.activeElement !== NumberInput) NumberInput.value = Number.isFinite(Value) ? Value.toFixed(Digits) : '0';
        SyncFraction(Number.isFinite(Value) ? Value : Binding.Minimum);
        ApplyAvailability(Row, Binding, State => { Range.disabled = NumberInput.disabled = !State.Enabled; });
    });
    return Row;
}

export function SwitchRow(Parent, Binding)
{
    const Input = Construct('input');
    Input.type = 'checkbox';
    Input.setAttribute('aria-label', Binding.Label);
    const Track = Construct('span');
    const Switch = Construct('label', 'switch', [Input, Track]);
    const Copy = Construct('div', 'switch-copy', [Construct('span', 'property-label', Binding.Label)]);
    if (Binding.Detail) Copy.append(Construct('small', 'property-detail', Binding.Detail));
    const Row = Construct('div', 'property-row toggle-row', [Copy, Switch]);
    Parent.append(Row);

    Input.addEventListener('change', () => { Binding.Write(Input.checked); Broadcast(); });
    Register(() =>
    {
        Input.checked = !!Binding.Read();
        ApplyAvailability(Row, Binding, State => { Input.disabled = !State.Enabled; });
    });
    return Row;
}

export function SelectRow(Parent, Binding)
{
    const Select = Construct('select');
    Select.setAttribute('aria-label', Binding.Label);
    for (const [Label, Value] of Binding.Options)
    {
        const Option = Construct('option', null, Label);
        Option.value = String(Value);
        Select.append(Option);
    }
    const Row = Construct('div', 'property-row select-row', [Construct('label', 'property-label', Binding.Label), Select]);
    Parent.append(Row);
    Select.addEventListener('change', () =>
    {
        const Found = Binding.Options.find(([, Value]) => String(Value) === Select.value);
        Binding.Write(Found ? Found[1] : Select.value);
        Broadcast();
    });
    Register(() =>
    {
        Select.value = String(Binding.Read());
        ApplyAvailability(Row, Binding, State => { Select.disabled = !State.Enabled; });
    });
    return Row;
}

export function SegmentedRow(Parent, Binding)
{
    const Track = Construct('div', 'segmented-track');
    const Buttons = Binding.Options.map(([Label, Value]) =>
    {
        const Btn = Construct('button', null, Label);
        Btn.type = 'button';
        Btn.addEventListener('click', () => { Binding.Write(Value); Broadcast(); });
        Track.append(Btn);
        return [Btn, Value];
    });
    const Row = Construct('div', 'property-row select-row', [Construct('span', 'property-label', Binding.Label), Track]);
    Parent.append(Row);
    Register(() =>
    {
        const Current = Binding.Read();
        for (const [Btn, Value] of Buttons) Btn.classList.toggle('active', Current === Value);
        ApplyAvailability(Row, Binding);
    });
    return Row;
}

export function VectorRow(Parent, Binding)
{
    const Axes = ['X', 'Y', 'Z'];
    const AxisColors = ['#ef5350', '#69d06d', '#5b8cff'];
    const Step = Binding.Step ?? 0.05;
    const Digits = Binding.Digits ?? 2;
    const Grid = Construct('div', 'vector-grid');
    const Inputs = Axes.map((Axis, Index) =>
    {
        const Input = Construct('input', 'vector-number');
        Input.type = 'number';
        Input.step = String(Step);
        Input.setAttribute('aria-label', `${Binding.Label} ${Axis}`);
        if (Binding.Minimum !== undefined) Input.min = String(Binding.Minimum);
        if (Binding.Maximum !== undefined) Input.max = String(Binding.Maximum);
        const Tag = Construct('span', 'vector-axis', Axis);
        Tag.style.color = AxisColors[Index];
        const Cell = Construct('label', 'vector-cell', [Tag, Input]);
        Input.addEventListener('change', () =>
        {
            const Vec = Binding.Read().slice();
            Vec[Index] = Number(Input.value) || 0;
            Binding.Write(Vec);
            Broadcast();
        });
        Grid.append(Cell);
        return Input;
    });
    const Head = Construct('div', 'vector-head', [
        Construct('span', 'property-label', Binding.Label),
        Binding.Unit ? Construct('span', 'vector-unit', Binding.Unit) : null
    ]);
    const Row = Construct('div', 'property-row vector-row', [Head, Grid]);
    Parent.append(Row);
    Register(() =>
    {
        const Vec = Binding.Read() || [0, 0, 0];
        const State = ApplyAvailability(Row, Binding);
        Inputs.forEach((Input, I) =>
        {
            Input.disabled = !State.Enabled;
            if (document.activeElement !== Input) Input.value = Number(Vec[I] ?? 0).toFixed(Digits);
        });
    });
    return Row;
}

export function ColourRow(Parent, Binding)
{
    const HexInput = Construct('input', 'colour-picker');
    HexInput.type = 'color';
    HexInput.setAttribute('aria-label', Binding.Label);
    const Code = Construct('span', 'colour-hex');
    const Right = Construct('div', 'colour-right', [Code, HexInput]);
    const Top = Construct('div', 'colour-top', [Construct('span', 'property-label', Binding.Label), Right]);
    const Row = Construct('div', 'property-row colour-row', [Top]);

    HexInput.addEventListener('input', () =>
    {
        const Hex = HexInput.value;
        const R = DisplayToLinear(parseInt(Hex.slice(1, 3), 16) / 255);
        const G = DisplayToLinear(parseInt(Hex.slice(3, 5), 16) / 255);
        const B = DisplayToLinear(parseInt(Hex.slice(5, 7), 16) / 255);
        Binding.Write([R, G, B]);
        Broadcast();
    });

    Parent.append(Row);
    Register(() =>
    {
        const Lin = Binding.Read() || [0.68, 0.42, 0.31];
        const ToByte = C => Math.round(Clamp(LinearToDisplay(Clamp(C, 0, 1)), 0, 1) * 255).toString(16).padStart(2, '0');
        const Hex = `#${ToByte(Lin[0])}${ToByte(Lin[1])}${ToByte(Lin[2])}`;
        HexInput.value = Hex;
        Code.textContent = Hex.toLowerCase();
        ApplyAvailability(Row, Binding, State => { HexInput.disabled = !State.Enabled; });
    });
    return Row;
}

export function ToggleTile(Parent, Binding)
{
    const Icon = Construct('div', 'switch-icon', ConstructGlyph(Binding.Glyph || 'Check'));
    const Name = Construct('div', 'switch-name', Binding.Label);
    const StateLabel = Construct('div', 'switch-state');
    const Tile = Construct('button', 'property-switch', [Icon, Name, StateLabel]);
    Tile.type = 'button';
    Tile.addEventListener('click', () =>
    {
        if (!ResolveState(Binding).Enabled) return;
        Binding.Write(!Binding.Read());
        Broadcast();
    });
    Parent.append(Tile);
    Register(() =>
    {
        const On = !!Binding.Read();
        const Avail = ApplyAvailability(Tile, Binding);
        Tile.classList.toggle('is-off', !On);
        Tile.disabled = !Avail.Enabled;
        StateLabel.textContent = !Avail.Enabled ? 'LOCKED' : On ? 'ON' : 'OFF';
    });
    return Tile;
}

export function ActionRow(Parent, Actions)
{
    const Row = Construct('div', 'action-row');
    for (const Act of Actions)
    {
        const Btn = Construct('button', 'action-button' + (Act.Tone ? ' ' + Act.Tone : ''), [
            Act.Glyph ? ConstructGlyph(Act.Glyph) : null,
            Construct('span', null, Act.Label)
        ]);
        Btn.type = 'button';
        if (Act.Title) Btn.title = Act.Title;
        Btn.addEventListener('click', () => { if (!Btn.disabled) { Act.Act(); Broadcast(); } });
        Row.append(Btn);
        if (Act.Availability)
        {
            Register(() =>
            {
                const St = Act.Availability();
                Btn.disabled = !St.Enabled;
                if (!St.Enabled && St.Reason) Btn.title = St.Reason;
            });
        }
    }
    Parent.append(Row);
    return Row;
}

export function MetricTrio(Parent, Items)
{
    const Grid = Construct('div', 'kpi-trio');
    const RefreshersLocal = Items.map(Item =>
    {
        const Val = Construct('strong', 'kpi-val');
        const Foot = Construct('small', 'kpi-sub');
        const Cell = Construct('div', 'kpi-card' + (Item.Highlight ? ' highlight' : ''), [
            Construct('span', 'kpi-label', Item.Label),
            Val,
            Foot
        ]);
        Grid.append(Cell);
        return () =>
        {
            const [Text, Sub] = Item.Resolve();
            Val.textContent = Text;
            Foot.textContent = Sub || '';
        };
    });
    Parent.append(Grid);
    const Refresh = () => RefreshersLocal.forEach(R => R());
    Register(Refresh);
    return { Grid, Refresh };
}

/**
 * Interactive Brush Falloff Curve Editor (graph-only grid inside graph editor per UI_COMPONENT_GUIDELINES.md).
 * Lets the user pick named profiles OR drag control handles directly, applying immediately to brush evaluation.
 */
export function FalloffCurveEditor(Host, FalloffState, OnChange)
{
    const Wrap = Construct('div', 'falloff-editor');
    const TopBar = Construct('div', 'falloff-toolbar');
    const ModeTrack = Construct('div', 'falloff-pills');

    const Modes = [
        ['Smooth', 'smooth'],
        ['Plateau', 'plateau'],
        ['Sharp', 'sharp'],
        ['Linear', 'linear'],
        ['Sphere', 'sphere']
    ];

    const ModeButtons = Modes.map(([Label, Key]) =>
    {
        const Btn = Construct('button', 'falloff-pill', Label);
        Btn.type = 'button';
        Btn.addEventListener('click', () =>
        {
            FalloffState.Mode = Key;
            FalloffState.Custom = false;
            FalloffState.Points = FALLOFF_PRESETS[Key].Points.map(P => P.slice());
            if (OnChange) OnChange();
            Broadcast();
        });
        ModeTrack.append(Btn);
        return [Btn, Key];
    });

    const InterpSelect = Construct('select', 'falloff-interp-select');
    InterpSelect.setAttribute('aria-label', 'Curve interpolation');
    InterpSelect.append(
        Object.assign(Construct('option', null, 'Hermite'), { value: 'hermite' }),
        Object.assign(Construct('option', null, 'Linear'), { value: 'linear' })
    );
    InterpSelect.addEventListener('change', () =>
    {
        FalloffState.Interpolation = InterpSelect.value;
        FalloffState.Custom = true;
        if (OnChange) OnChange();
        Broadcast();
    });

    TopBar.append(ModeTrack, InterpSelect);

    const Height = 126;
    const DiagramBox = Construct('div', 'diagram-wrap curve-editor-box');
    const Svg = ConstructSvg('svg', { class: 'diagram curve-svg', height: Height });
    const Caption = Construct('div', 'plot-caption', 'Drag control handles to customize radial brush kernel (0 = center, 1 R = edge)');
    DiagramBox.append(Svg, Caption);
    Wrap.append(TopBar, DiagramBox);
    Host.append(Wrap);

    let DragIndex = -1;

    const GetPlotRect = () =>
    {
        const W = Math.max(220, Svg.clientWidth || Host.clientWidth || 310);
        return { W, Left: 28, Right: W - 12, Top: 10, Bottom: Height - 18 };
    };

    const Refresh = () =>
    {
        for (const [Btn, Key] of ModeButtons)
        {
            Btn.classList.toggle('active', !FalloffState.Custom && FalloffState.Mode === Key);
        }
        InterpSelect.value = FalloffState.Interpolation || 'hermite';

        const { W, Left, Right, Top, Bottom } = GetPlotRect();
        const SpanX = Right - Left, SpanY = Bottom - Top;
        const AtX = X => Left + X * SpanX;
        const AtY = Y => Bottom - Y * SpanY;

        Svg.textContent = '';
        const Defs = ConstructSvg('defs');
        const Grad = ConstructSvg('linearGradient', { id: 'FalloffAreaGrad', x1: '0', y1: '0', x2: '0', y2: '1' });
        Grad.append(
            ConstructSvg('stop', { offset: '0%', 'stop-color': '#e8c27a', 'stop-opacity': '0.30' }),
            ConstructSvg('stop', { offset: '100%', 'stop-color': '#e8c27a', 'stop-opacity': '0.02' })
        );
        Defs.append(Grad);
        Svg.append(Defs);

        // Graph-editor internal grid lines
        for (const YVal of [0, 0.5, 1.0])
        {
            const Y = AtY(YVal);
            Svg.append(
                ConstructSvg('line', { x1: Left, x2: Right, y1: Y, y2: Y, stroke: '#ffffff', 'stroke-opacity': 0.06, 'stroke-dasharray': '2 4' }),
                ConstructSvg('text', { x: Left - 5, y: Y + 3, 'text-anchor': 'end', fill: '#6a6a6a', 'font-size': 9 }, YVal.toFixed(1))
            );
        }
        for (const [XVal, Label] of [[0, '0.0 R'], [0.5, '0.5 R'], [1, '1.0 R']])
        {
            const X = AtX(XVal);
            Svg.append(
                ConstructSvg('line', { x1: X, x2: X, y1: Top, y2: Bottom, stroke: '#ffffff', 'stroke-opacity': 0.05, 'stroke-dasharray': '2 4' }),
                ConstructSvg('text', { x: X, y: Height - 3, 'text-anchor': XVal === 0 ? 'start' : XVal === 1 ? 'end' : 'middle', fill: '#6a6a6a', 'font-size': 9 }, Label)
            );
        }

        // Sample true evaluated falloff curve across 48 segments
        const Samples = [];
        for (let S = 0; S <= 48; ++S)
        {
            const R = S / 48;
            Samples.push([AtX(R), AtY(EvaluateFalloff(R, FalloffState))]);
        }
        const PathStr = Samples.map(([X, Y], I) => `${I ? 'L' : 'M'}${X.toFixed(1)} ${Y.toFixed(1)}`).join(' ');
        Svg.append(
            ConstructSvg('path', { d: `${PathStr} L${Right} ${Bottom} L${Left} ${Bottom} Z`, fill: 'url(#FalloffAreaGrad)' }),
            ConstructSvg('path', { d: PathStr, fill: 'none', stroke: '#f0cfa0', 'stroke-width': 1.8, 'stroke-linejoin': 'round' })
        );

        // Draggable control points
        const Pts = FalloffState.Points || FALLOFF_PRESETS.smooth.Points;
        Pts.forEach((Pt, Idx) =>
        {
            const Cx = AtX(Pt[0]), Cy = AtY(Pt[1]);
            const Handle = ConstructSvg('circle', {
                cx: Cx.toFixed(1),
                cy: Cy.toFixed(1),
                r: DragIndex === Idx ? 6 : 4.6,
                fill: FalloffState.Custom ? '#eff4cf' : '#e8c27a',
                stroke: '#121212',
                'stroke-width': 1.8,
                class: 'curve-handle'
            });
            Handle.dataset.idx = String(Idx);
            Svg.append(Handle);
        });
    };

    const PointerToPoint = Event =>
    {
        const Bounds = Svg.getBoundingClientRect();
        const { Left, Right, Top, Bottom } = GetPlotRect();
        const Mx = Event.clientX - Bounds.left;
        const My = Event.clientY - Bounds.top;
        return [
            Clamp((Mx - Left) / Math.max(1, Right - Left), 0, 1),
            Clamp((Bottom - My) / Math.max(1, Bottom - Top), 0, 1)
        ];
    };

    Svg.addEventListener('pointerdown', Event =>
    {
        const Pts = FalloffState.Points;
        if (!Pts) return;
        const [Ux, Uy] = PointerToPoint(Event);
        let Best = -1, BestDist = 0.12;
        Pts.forEach((Pt, I) =>
        {
            const D = Math.hypot(Pt[0] - Ux, Pt[1] - Uy);
            if (D < BestDist) { BestDist = D; Best = I; }
        });
        if (Best >= 0)
        {
            DragIndex = Best;
            FalloffState.Custom = true;
            Svg.setPointerCapture(Event.pointerId);
            Refresh();
        }
    });

    Svg.addEventListener('pointermove', Event =>
    {
        if (DragIndex < 0) return;
        const Pts = FalloffState.Points;
        const [Ux, Uy] = PointerToPoint(Event);
        if (DragIndex === 0)
        {
            Pts[0][0] = 0;
            Pts[0][1] = Uy;
        }
        else if (DragIndex === Pts.length - 1)
        {
            Pts[DragIndex][0] = 1;
            Pts[DragIndex][1] = Uy;
        }
        else
        {
            const MinX = Pts[DragIndex - 1][0] + 0.04;
            const MaxX = Pts[DragIndex + 1][0] - 0.04;
            Pts[DragIndex][0] = Clamp(Ux, MinX, MaxX);
            Pts[DragIndex][1] = Uy;
        }
        if (OnChange) OnChange();
        Broadcast();
    });

    const StopDrag = () => { if (DragIndex >= 0) { DragIndex = -1; Refresh(); } };
    Svg.addEventListener('pointerup', StopDrag);
    Svg.addEventListener('pointercancel', StopDrag);

    new ResizeObserver(Refresh).observe(Svg);
    Register(Refresh);
    return { Wrap, Refresh };
}

/**
 * Sun-Inspector SDF Distance Histogram & Zero-Isosurface Distribution Diagram.
 */
export function DistanceHistogram(Host, GetBins)
{
    const Height = 88;
    const Wrap = Construct('div', 'diagram-wrap');
    const Svg = ConstructSvg('svg', { class: 'diagram', height: Height });
    Wrap.append(Svg);
    Host.append(Wrap);

    const Refresh = () =>
    {
        const Bins = GetBins() || [];
        const W = Math.max(200, Svg.clientWidth || Host.clientWidth || 310);
        const Top = 8, Bottom = Height - 16;
        const MaxVal = Math.max(1, ...Bins);
        Svg.textContent = '';

        const N = Bins.length || 24;
        const BarW = W / N;
        for (let I = 0; I < N; ++I)
        {
            const Frac = Math.pow((Bins[I] || 0) / MaxVal, 0.65);
            const Bh = Math.max(2, Frac * (Bottom - Top));
            const IsInterior = I < N * 0.5;
            Svg.append(ConstructSvg('rect', {
                x: (I * BarW + 1).toFixed(1),
                y: (Bottom - Bh).toFixed(1),
                width: Math.max(2, BarW - 2.2).toFixed(1),
                height: Bh.toFixed(1),
                rx: 2,
                fill: IsInterior ? '#3ec46d' : '#e8c27a',
                'fill-opacity': IsInterior ? 0.58 : 0.42
            }));
        }

        // Zero-level isosurface center marker
        const ZeroX = W * 0.5;
        Svg.append(
            ConstructSvg('line', { x1: ZeroX, x2: ZeroX, y1: 4, y2: Bottom + 2, stroke: '#eff4cf', 'stroke-width': 1.4, 'stroke-dasharray': '3 3' }),
            ConstructSvg('text', { x: 4, y: Height - 3, fill: '#6bc98e', 'font-size': 9 }, 'd < 0 (interior clay)'),
            ConstructSvg('text', { x: ZeroX, y: Height - 3, 'text-anchor': 'middle', fill: '#eff4cf', 'font-size': 9 }, 'd = 0'),
            ConstructSvg('text', { x: W - 4, y: Height - 3, 'text-anchor': 'end', fill: '#cfa968', 'font-size': 9 }, 'd > 0 (exterior)')
        );
    };
    new ResizeObserver(Refresh).observe(Svg);
    Register(Refresh);
    return { Wrap, Refresh };
}

export function PolarDial(Host, Specification)
{
    const Size = Specification.Size || 136;
    const Panel = Construct('div', 'diagram-wrap polar-dial');
    const Svg = ConstructSvg('svg', { class: 'diagram dial-svg', height: Size, viewBox: `0 0 ${Size} ${Size}` });
    const Side = Construct('div', 'dial-side');
    Panel.append(Svg, Side);
    Host.append(Panel);
    const Centre = Size / 2, Radius = Size / 2 - 15;
    const Refresh = () =>
    {
        const Bearing = Specification.Read();
        const Turn = Bearing * Math.PI / 180;
        const Tip = [Centre + Math.sin(Turn) * Radius, Centre - Math.cos(Turn) * Radius];
        Svg.textContent = '';
        Svg.setAttribute('width', Size);
        Svg.append(
            ConstructSvg('circle', { cx: Centre, cy: Centre, r: Radius, fill: '#111111', stroke: '#2c2c2c' }),
            ConstructSvg('circle', { cx: Centre, cy: Centre, r: Radius * 0.6, fill: 'none', stroke: '#ffffff', 'stroke-opacity': 0.06, 'stroke-dasharray': '2 5' })
        );
        for (const [Text, Angle] of [['N', 0], ['E', 90], ['S', 180], ['W', 270]])
        {
            const A = Angle * Math.PI / 180;
            Svg.append(ConstructSvg('text', { x: Centre + Math.sin(A) * (Radius + 9), y: Centre - Math.cos(A) * (Radius + 9) + 3.5, 'text-anchor': 'middle', fill: Text === 'N' ? '#e0e0e0' : '#6c6c6c', 'font-size': 9.5 }, Text));
        }
        Svg.append(
            ConstructSvg('line', { x1: Centre, y1: Centre, x2: Tip[0], y2: Tip[1], stroke: '#e8c27a', 'stroke-width': 2, 'stroke-linecap': 'round' }),
            ConstructSvg('circle', { cx: Tip[0], cy: Tip[1], r: 6, fill: '#f2e0ba', stroke: '#111111', 'stroke-width': 2 }),
            ConstructSvg('circle', { cx: Centre, cy: Centre, r: 3, fill: '#ffffff' })
        );
        const Info = Specification.Resolve ? Specification.Resolve() : { Caption: '' };
        Side.textContent = '';
        Side.append(
            Construct('div', 'dial-label', Specification.Label || 'Key Light Azimuth'),
            Construct('div', 'dial-reading', [document.createTextNode(Bearing.toFixed(0)), Construct('span', null, '°')]),
            Construct('div', 'dial-point', ResolveCompassPoint(Bearing)),
            Info.Caption ? Construct('div', 'dial-caption', Info.Caption) : null
        );
    };
    const BearingAt = Event =>
    {
        const Bounds = Svg.getBoundingClientRect();
        const Scale = Size / Math.max(1, Bounds.width);
        const X = (Event.clientX - Bounds.left) * Scale - Centre, Y = (Event.clientY - Bounds.top) * Scale - Centre;
        return ((Math.round(Math.atan2(X, -Y) * 180 / Math.PI) % 360) + 360) % 360;
    };
    let Dragging = false;
    Svg.addEventListener('pointerdown', Event => { Dragging = true; Svg.setPointerCapture(Event.pointerId); Specification.Write(BearingAt(Event)); Broadcast(); });
    Svg.addEventListener('pointermove', Event => { if (Dragging) { Specification.Write(BearingAt(Event)); Broadcast(); } });
    Svg.addEventListener('pointerup', () => { Dragging = false; });
    Svg.addEventListener('pointercancel', () => { Dragging = false; });
    Register(Refresh);
    return { Panel, Refresh };
}

export function SmoothScroll(Scroller, Content)
{
    let Target = Scroller.scrollTop, Current = Target, Running = false, LastWheel = 0;
    const Limit = () => Math.max(0, Scroller.scrollHeight - Scroller.clientHeight);
    const Advance = Now =>
    {
        const Max = Limit();
        Target = Now - LastWheel > 120 ? Clamp(Target, 0, Max) : Clamp(Target, -64, Max + 64);
        Current += (Target - Current) * 0.22;
        if (Math.abs(Target - Current) < 0.25) Current = Target;
        const Clamped = Clamp(Current, 0, Max);
        Scroller.scrollTop = Clamped;
        const Stretch = Current - Clamped;
        Content.style.transform = Stretch ? `translateY(${(-Stretch * 0.35).toFixed(2)}px)` : '';
        if (Current !== Target || Stretch !== 0) requestAnimationFrame(Advance);
        else Running = false;
    };
    const Start = () => { if (!Running) { Running = true; requestAnimationFrame(Advance); } };
    Scroller.addEventListener('wheel', Event =>
    {
        if (Event.ctrlKey || Math.abs(Event.deltaX) > Math.abs(Event.deltaY)) return;
        Event.preventDefault();
        if (!Running) Current = Target = Scroller.scrollTop;
        Target += Event.deltaMode === 1 ? Event.deltaY * 18 : Event.deltaY;
        LastWheel = performance.now();
        Start();
    }, { passive: false });
    Scroller.addEventListener('scroll', () => { if (!Running) Current = Target = Scroller.scrollTop; });
    return {
        ScrollTo(Position)
        {
            if (!Running) Current = Scroller.scrollTop;
            Target = Clamp(Position, 0, Limit());
            LastWheel = 0;
            Start();
        }
    };
}

export function Toast(Text)
{
    let Element = document.querySelector('.toast');
    if (!Element) { Element = Construct('div', 'toast'); document.body.append(Element); }
    Element.textContent = Text;
    Element.classList.add('shown');
    clearTimeout(Toast.Timer);
    Toast.Timer = setTimeout(() => Element.classList.remove('shown'), 2200);
}
