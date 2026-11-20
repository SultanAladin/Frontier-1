/*==========================================================================================================================================
                                                          STUDIOCONTROLS.JS
==========================================================================================================================================*/
// Unified Frontier Experimental Remastered UI component kit for Texture Studio:
//   SectionCard, SliderRow (scrubbable black value-pill + 26px track), SwitchRow, SelectRow, SegmentedRow,
//   ColorRow, TextRow, ActionRow, MetricTrio, SmoothScroll, Scope lifecycle, Toast.

import {
    GetFallbackColourSlot,
    HasColourDragData,
    NotifyColourApplied,
    ReadColourDragData,
    SetActiveColourSlot,
    SetFallbackColourSlot
} from './ColourCodex.js';

const SvgNamespace = 'http://www.w3.org/2000/svg';
const Refreshers = new Set();
const ScopeStack = [];

export const GlyphPaths = {
    Grid:       '<rect x="3" y="3" width="7.5" height="7.5" rx="1.5"/><rect x="13.5" y="3" width="7.5" height="7.5" rx="1.5"/><rect x="3" y="13.5" width="7.5" height="7.5" rx="1.5"/><rect x="13.5" y="13.5" width="7.5" height="7.5" rx="1.5"/>',
    List:       '<path d="M9 6h12M9 12h12M9 18h12"/><circle cx="4.5" cy="6" r="1.4"/><circle cx="4.5" cy="12" r="1.4"/><circle cx="4.5" cy="18" r="1.4"/>',
    Filter:     '<path d="M3 5h18l-7 8v5l-4 2v-7z"/>',
    Layers:     '<path d="M12 3 3 8l9 5 9-5z"/><path d="M3 12.5l9 5 9-5"/><path d="M3 17l9 5 9-5"/>',
    Folder:     '<path d="M3 7a2 2 0 0 1 2-2h4.2l2 2.2H19a2 2 0 0 1 2 2V18a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2z"/>',
    Brush:      '<path d="M18.5 2.5a2.8 2.8 0 0 1 3 3L11.8 15.2l-3.6.6.6-3.6z"/><path d="M6.2 15.4c-1.8 0-3.2 1.4-3.2 3.2 0 .9-.7 1.6-1.5 1.9 1.3.7 3.3.9 4.8.2 1.3-.6 2.1-1.9 2.1-3.3a2.2 2.2 0 0 0-2.2-2z"/>',
    Eraser:     '<path d="m7 21-4.3-4.3c-1-1-1-2.5 0-3.4l9.6-9.6c1-1 2.5-1 3.4 0l5.6 5.6c1 1 1 2.5 0 3.4L13 21"/><path d="M22 21H7"/><path d="m5 11 9 9"/>',
    Fill:       '<path d="m19 11-8-8-8.6 8.6a2 2 0 0 0 0 2.8l5.2 5.2c.8.8 2 .8 2.8 0L19 11Z"/><path d="m5 2 5 5"/><path d="M2 13h15"/><path d="M22 20a2 2 0 1 1-4 0c0-1.6 1.7-2.4 2-4 .3 1.6 2 2.4 2 4Z"/>',
    Stamp:      '<path d="M5 18h14v3H5z"/><path d="M7 14h10a2 2 0 0 1 2 2v2H5v-2a2 2 0 0 1 2-2z"/><path d="M12 14V9.5c0-1.2 2.2-2.2 2.2-4.2a2.2 2.2 0 1 0-4.4 0c0 2 2.2 3 2.2 4.2"/>',
    Pipette:    '<path d="m2 22 1-1h3l9-9"/><path d="M3 21v-3l9-9"/><path d="m15 6 3.4-3.4a2.1 2.1 0 1 1 3 3L18 9"/><path d="m14 5 5 5"/>',
    Material:   '<circle cx="12" cy="12" r="9"/><path d="M12 3a9 9 0 0 0 0 18"/><path d="M12 3c2.8 0 5 4 5 9s-2.2 9-5 9"/>',
    Pattern:    '<rect x="3" y="3" width="18" height="18" rx="2.5"/><path d="M3 9h18M3 15h18M9 3v18M15 3v18"/>',
    Bake:       '<path d="M12 3l8 4.5v9L12 21l-8-4.5v-9z"/><path d="M12 12l8-4.5M12 12v9M12 12 4 7.5"/><circle cx="12" cy="7.5" r="1.4"/>',
    Volume:     '<rect x="4" y="4" width="12" height="12" rx="2"/><path d="M8 16v2a2 2 0 0 0 2 2h8a2 2 0 0 0 2-2v-8a2 2 0 0 0-2-2h-2"/>',
    Noise:      '<path d="M3 13c2.5-6 4.5-6 6 0s3.5 6 6 0 3.5-6 6 0"/><path d="M3 8c1.5-2.5 3-2.5 4.5 0s3 2.5 4.5 0 3-2.5 4.5 0 3 2.5 4.5 0"/>',
    Modifier:   '<path d="M12 3v3M12 18v3M3 12h3M18 12h3"/><circle cx="12" cy="12" r="4"/>',
    Move:       '<path d="M12 3v18M3 12h18M9.5 5.5 12 3l2.5 2.5M9.5 18.5 12 21l2.5-2.5M5.5 9.5 3 12l2.5 2.5M18.5 9.5 21 12l-2.5 2.5"/>',
    Rotate:     '<path d="M20 12a8 8 0 1 1-2.4-5.7"/><path d="M20 4.5V9h-4.5"/>',
    Scale:      '<path d="M15 4h5v5M9 20H4v-5M20 4l-6 6M4 20l6-6"/>',
    Mirror:     '<path d="M12 3v18M8 7 4 12l4 5zM16 7l4 5-4 5z"/>',
    Search:     '<circle cx="11" cy="11" r="7"/><path d="M20 20l-3.8-3.8"/>',
    Eye:        '<path d="M2 12s3.6-7 10-7 10 7 10 7-3.6 7-10 7S2 12 2 12z"/><circle cx="12" cy="12" r="3"/>',
    EyeOff:     '<path d="M3 3l18 18"/><path d="M10.6 5.1A10.4 10.4 0 0 1 12 5c6.4 0 10 7 10 7a17.6 17.6 0 0 1-2.6 3.5M6.2 6.3C3.6 8.1 2 12 2 12s3.6 7 10 7c1.7 0 3.2-.5 4.5-1.2"/>',
    Lock:       '<rect x="5" y="11" width="14" height="9.5" rx="2"/><path d="M8 11V7.5a4 4 0 0 1 8 0V11"/>',
    Plus:       '<path d="M12 5v14M5 12h14"/>',
    Close:      '<path d="M6.5 6.5l11 11M17.5 6.5l-11 11"/>',
    Trash:      '<path d="M4.5 7h15M9.5 7V4.5h5V7M6.5 7l1 13h9l1-13"/>',
    Undo:       '<path d="M9 14 4 9l5-5"/><path d="M4 9h10.5a5.5 5.5 0 0 1 0 11H11"/>',
    Redo:       '<path d="M15 14l5-5-5-5"/><path d="M20 9H9.5a5.5 5.5 0 0 0 0 11H13"/>',
    Download:   '<path d="M12 3v12m0 0-4-4m4 4 4-4M4 17v2a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2v-2"/>',
    Upload:     '<path d="M12 15V3m0 0-4 4m4-4 4 4M4 17v2a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2v-2"/>',
    Help:       '<circle cx="12" cy="12" r="9"/><path d="M9.5 9.5a2.5 2.5 0 0 1 5 .5c0 2-2.5 2-2.5 4M12 17.5h.01"/>',
    Focus:      '<path d="M4 9V5h4M16 5h4v4M20 15v4h-4M8 19H4v-4"/><circle cx="12" cy="12" r="2.2"/>',
    Maximize:   '<path d="M15 3h6v6M9 21H3v-6M21 3l-7 7M3 21l7-7"/>',
    Minimize:   '<path d="M4 14h6v6M20 10h-6V4M14 10l7-7M10 14l-7 7"/>',
    Check:      '<path d="M5 12.5l4.2 4.2L19 7"/>',
    Sparkles:   '<path d="M11 3.5l1.7 4.6 4.6 1.7-4.6 1.7L11 16.1l-1.7-4.6-4.6-1.7 4.6-1.7z"/><path d="M18.5 14.5v5M16 17h5"/>',
    Sliders:    '<path d="M4 6h9M17 6h3M4 12h3M11 12h9M4 18h11M19 18h1"/><circle cx="15" cy="6" r="2"/><circle cx="9" cy="12" r="2"/><circle cx="17" cy="18" r="2"/>',
    Reset:      '<path d="M3.5 12a8.5 8.5 0 1 0 2.6-6.1"/><path d="M3.5 4.5v4.6h4.6"/>',
    Sun:        '<circle cx="12" cy="12" r="4"/><path d="M12 2v2M12 20v2M4.9 4.9l1.4 1.4M17.7 17.7l1.4 1.4M2 12h2M20 12h2M4.9 19.1l1.4-1.4M17.7 6.3l1.4-1.4"/>',
    Cube:       '<path d="M12 3 4 7.5v9L12 21l8-4.5v-9z"/><path d="m12 12 8-4.5M12 12v9M12 12 4 7.5"/>',
    Copy:       '<rect x="9" y="9" width="11" height="11" rx="2"/><path d="M5 15H4a2 2 0 0 1-2-2V4a2 2 0 0 1 2-2h9a2 2 0 0 1 2 2v1"/>',
    ArrowRight: '<path d="M5 12h14M13 5l7 7-7 7"/>',
    ArrowUp:    '<path d="M12 19V5M5 12l7-7 7 7"/>',
    ArrowDown:  '<path d="M12 5v14M19 12l-7 7-7-7"/>',
    Grip:       '<circle cx="9" cy="6" r="1.3"/><circle cx="15" cy="6" r="1.3"/><circle cx="9" cy="12" r="1.3"/><circle cx="15" cy="12" r="1.3"/><circle cx="9" cy="18" r="1.3"/><circle cx="15" cy="18" r="1.3"/>',
    Pointer:    '<path d="M5 3l14 7-6 2-2 6z"/>',
    Square:     '<rect x="4" y="4" width="16" height="16" rx="2"/>',
    Circle:     '<circle cx="12" cy="12" r="8.5"/>',
    Diamond:    '<path d="M12 3l9 9-9 9-9-9z"/>',
    Triangle:   '<path d="M12 4l9 16H3z"/>',
    Star:       '<path d="m12 3 2.7 5.6 6.2.9-4.5 4.4 1.1 6.1L12 17l-5.5 3 1.1-6.1-4.5-4.4 6.2-.9z"/>',
    Hexagon:    '<path d="M12 3l7.8 4.5v9L12 21l-7.8-4.5v-9z"/>',
    Stripe:     '<path d="M4 9h16v6H4z"/>',
    Type:       '<path d="M5 6V4h14v2M12 4v16M9 20h6"/>',
    Hand:       '<path d="M18 11V6a1.5 1.5 0 0 0-3 0v4M15 10V4.5a1.5 1.5 0 0 0-3 0V10M12 10V5.5a1.5 1.5 0 0 0-3 0v6.5L7.3 10.3a1.5 1.5 0 0 0-2.1 2.1l4.1 5.8A4.5 4.5 0 0 0 13 20h2.5a4.5 4.5 0 0 0 4.5-4.5V11a1.5 1.5 0 0 0-2 0z"/>',
    ChevronUp:  '<path d="m6 15 6-6 6 6"/>',
    ChevronDown:'<path d="m6 9 6 6 6-6"/>',
    ChevronRight:'<path d="m9 6 6 6-6 6"/>',
    FolderPlus: '<path d="M3 7a2 2 0 0 1 2-2h4.2l2 2.2H19a2 2 0 0 1 2 2V18a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2z"/><path d="M12 11v6M9 14h6"/>',
    Save:       '<path d="M19 21H5a2 2 0 0 1-2-2V5a2 2 0 0 1 2-2h11l5 5v11a2 2 0 0 1-2 2z"/><path d="M17 21v-8H7v8M7 3v5h8"/>',
    LockOpen:   '<rect x="5" y="11" width="14" height="9.5" rx="2"/><path d="M8 11V7.5a4 4 0 0 1 7.6-1.7"/>',
    Mask:       '<rect x="3.5" y="3.5" width="17" height="17" rx="3"/><circle cx="12" cy="12" r="4.5"/>',
    Document:   '<path d="M14 3H6a2 2 0 0 0-2 2v14a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2V9z"/><path d="M14 3v6h6"/>',
    Pin:        '<path d="M12 17v5M9 10.8a3 3 0 0 1 1-2.2l1-.9V4h2v3.7l1 .9a3 3 0 0 1 1 2.2V14H9z"/>',
    Image:      '<rect x="3" y="4" width="18" height="16" rx="2.5"/><circle cx="8.5" cy="9.5" r="1.5"/><path d="M21 15l-4.5-4.5L9 18"/>',
    Curve:      '<path d="M4 18C9 18 10 6 20 6"/><circle cx="4" cy="18" r="1.8"/><circle cx="12" cy="12" r="1.8"/><circle cx="20" cy="6" r="1.8"/>',
    Pen:        '<path d="m12 19 7-7 3 3-7 7-3-3z"/><path d="m18 13-1.5-7.5L2 2l3.5 14.5L13 18l5-5z"/><path d="m2 2 7.586 7.586"/><circle cx="11" cy="11" r="2"/>',
    Gradient:   '<circle cx="7" cy="17" r="3"/><circle cx="17" cy="7" r="3"/><circle cx="17" cy="17" r="2.2"/><path d="M9.2 14.8 14.8 9.2M10 17h4.5M17 10v4.5"/>',
    Dot:        '<circle cx="12" cy="12" r="3"/>'
};

export function ConstructGlyph(Name, ClassName = 'glyph')
{
    const Svg = document.createElementNS(SvgNamespace, 'svg');
    Svg.setAttribute('viewBox', '0 0 24 24');
    Svg.setAttribute('class', ClassName);
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

export function OpenScope() { const Scope = new Set(); ScopeStack.push(Scope); return Scope; }
export function CloseScope(Scope) { const Top = ScopeStack.pop(); if (Top !== Scope && Scope) { const idx = ScopeStack.indexOf(Scope); if (idx >= 0) ScopeStack.splice(idx, 1); } }
export function ReleaseScope(Scope) { if (!Scope) return; for (const R of Scope) Refreshers.delete(R); Scope.clear(); }
export function Register(Refresher) { Refreshers.add(Refresher); if (ScopeStack.length) ScopeStack[ScopeStack.length - 1].add(Refresher); }
export function Broadcast() { for (const Refresher of Refreshers) Refresher(); }

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
        const Clamped = Math.min(Binding.Maximum, Math.max(Binding.Minimum, Raw));
        const Precision = Math.pow(10, Digits + 2);
        return Math.round(Clamped * Precision) / Precision;
    };
    const SyncFraction = Value =>
    {
        const Span = (Binding.Maximum - Binding.Minimum) || 1;
        const Frac = Math.max(0, Math.min(1, (Value - Binding.Minimum) / Span));
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
    for (const [Label, Value] of Binding.Options)
    {
        const Option = Construct('option', null, Label);
        Option.value = String(Value);
        Select.append(Option);
    }
    const Caret = Construct('span', 'pill-caret-divider', ConstructGlyph('ChevronDown', 'glyph chev'));
    const Wrap = Construct('div', 'select-pill-wrap', [Select, Caret]);
    const Row = Construct('div', 'property-row select-row', [Construct('label', 'property-label', Binding.Label), Wrap]);
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

export function ColorRow(Parent, Binding)
{
    const Picker = Construct('input', 'color-native');
    Picker.type = 'color';
    Picker.setAttribute('aria-label', Binding.Label);
    const SwatchShell = Construct('label', 'color-circle-shell', Picker);
    const LabelSpan = Construct('span', 'property-label color-pill-label', Binding.Label);
    const Hex = Construct('input', 'color-hex');
    Hex.type = 'text';
    Hex.maxLength = 7;
    Hex.setAttribute('aria-label', `${Binding.Label} Hex`);
    const Control = Construct('div', 'color-pill-wrap', [SwatchShell, LabelSpan, Hex]);
    const Row = Construct('div', 'property-row color-row', Control);
    Parent.append(Row);

    const CommitHex = Raw =>
    {
        const Clean = String(Raw || '').trim();
        if (/^#[0-9a-fA-F]{6}$/.test(Clean)) {
            Binding.Write(Clean.toLowerCase());
            Broadcast();
        }
    };
    const SlotLabel = Binding.SlotLabel || Binding.Label || 'Colour slot';
    const AssignSlotHex = (NextHex) =>
    {
        const Clean = String(NextHex || '').trim().toLowerCase();
        if (!/^#[0-9a-f]{6}$/.test(Clean)) return;
        Picker.value = Clean;
        Hex.value = Clean;
        SwatchShell.style.background = Clean;
        Binding.Write(Clean);
        Broadcast();
    };
    const RememberSlot = () =>
    {
        SetActiveColourSlot({
            Key: SlotLabel,
            Label: SlotLabel,
            AssignHex: AssignSlotHex,
            Element: Row
        });
    };
    if (!GetFallbackColourSlot()) {
        SetFallbackColourSlot({
            Key: SlotLabel,
            Label: SlotLabel,
            AssignHex: AssignSlotHex,
            Element: Row
        });
    }

    Row.setAttribute('data-colour-slot', SlotLabel);
    Row.addEventListener('mousedown', RememberSlot);
    Picker.addEventListener('focus', RememberSlot);
    Hex.addEventListener('focus', RememberSlot);

    Row.addEventListener('dragenter', (E) => {
        if (!HasColourDragData(E)) return;
        E.preventDefault();
        E.stopPropagation();
        Row.setAttribute('data-colour-drop-over', 'true');
    });
    Row.addEventListener('dragover', (E) => {
        if (!HasColourDragData(E)) return;
        E.preventDefault();
        E.stopPropagation();
        if (E.dataTransfer) E.dataTransfer.dropEffect = 'copy';
        Row.setAttribute('data-colour-drop-over', 'true');
    });
    Row.addEventListener('dragleave', (E) => {
        if (E.relatedTarget && Row.contains?.(E.relatedTarget)) return;
        Row.removeAttribute('data-colour-drop-over');
    });
    Row.addEventListener('drop', (E) => {
        Row.removeAttribute('data-colour-drop-over');
        const Dropped = ReadColourDragData(E);
        if (!Dropped) return;
        E.preventDefault();
        E.stopPropagation();
        AssignSlotHex(Dropped.Hex);
        RememberSlot();
        NotifyColourApplied(Dropped, SlotLabel);
        Toast(`Applied ${Dropped.Name} (${Dropped.Hex}) -> ${SlotLabel}`);
    });

    Picker.addEventListener('input', () => {
        const val = Picker.value.toLowerCase();
        SwatchShell.style.background = val;
        Binding.Write(val);
        RememberSlot();
        Broadcast();
    });
    Hex.addEventListener('change', () => { CommitHex(Hex.value); RememberSlot(); });

    Register(() =>
    {
        const Val = String(Binding.Read() || '#ffffff').toLowerCase();
        if (/^#[0-9a-f]{6}$/.test(Val)) {
            Picker.value = Val;
            SwatchShell.style.background = Val;
        }
        if (document.activeElement !== Hex) Hex.value = Val;
        ApplyAvailability(Row, Binding);
    });
    return Row;
}

export function TextRow(Parent, Binding)
{
    const Input = Construct('input', 'pill-text-input');
    Input.type = 'text';
    if (Binding.Placeholder) Input.placeholder = Binding.Placeholder;
    const Row = Construct('div', 'property-row text-row', [
        Construct('label', 'property-label', Binding.Label),
        Input
    ]);
    Parent.append(Row);
    Input.addEventListener('input', () => { Binding.Write(Input.value); Broadcast(); });
    Register(() =>
    {
        const Val = String(Binding.Read() ?? '');
        if (document.activeElement !== Input && Input.value !== Val) Input.value = Val;
        ApplyAvailability(Row, Binding);
    });
    return Row;
}

export function FilterDropdownPill(Parent, Binding)
{
    const Dropdown = Construct('div', 'pill-dropdown');
    const Dot = Construct('span', 'row-dot');
    const GradSw = Construct('span', 'pill-grad-swatch');
    const DimSpan = Construct('span', 'trigger-dim');
    const LabelSpan = Construct('span', 'trigger-label');
    const Chev = ConstructGlyph('ChevronDown', 'glyph chev');
    const CaretBox = Construct('span', 'pill-caret-divider', Chev);

    const Trigger = Construct('button', 'pill-trigger', [Dot, GradSw, DimSpan, LabelSpan, CaretBox]);
    Trigger.type = 'button';
    Trigger.setAttribute('aria-haspopup', 'menu');
    Trigger.setAttribute('aria-expanded', 'false');

    const Menu = Construct('div', 'pill-menu' + (Binding.AlignRight ? ' align-right' : ''));
    const SearchInput = document.createElement('input');
    SearchInput.type = 'text';
    SearchInput.className = 'pill-menu-search-input';
    SearchInput.placeholder = Binding.Placeholder || 'Filter...';
    const SearchBox = Construct('div', 'pill-menu-search', [
        ConstructGlyph('Search'),
        SearchInput,
    ]);
    const MenuLabel = Construct('div', 'menu-label', Binding.MenuLabel || Binding.Label || 'Select Option');
    const MenuList = Construct('div', 'pill-menu-list');
    Menu.append(SearchBox, MenuLabel, MenuList);
    Dropdown.append(Trigger, Menu);

    const Row = Binding.Compact
        ? Dropdown
        : Construct('div', 'property-row dropdown-pill-row', [
            Binding.Label ? Construct('span', 'property-label', Binding.Label) : null,
            Dropdown,
        ]);
    Parent.append(Row);

    let IsOpen = false;
    const PositionMenu = () =>
    {
        const rect = Trigger.getBoundingClientRect?.();
        if (!rect || rect.width <= 0) return;
        const vw = window.innerWidth || 1280;
        const vh = window.innerHeight || 800;
        const mw = Math.max(236, Math.min(300, rect.width));
        Menu.style.position = 'fixed';
        Menu.style.width = `${mw}px`;
        Menu.style.left = `${Math.max(8, Math.min(rect.left, vw - mw - 8))}px`;
        const below = vh - rect.bottom - 12;
        if (below >= 220 || below >= rect.top)
        {
            Menu.style.top = `${Math.min(vh - 60, rect.bottom + 6)}px`;
            Menu.style.bottom = 'auto';
        }
        else
        {
            Menu.style.top = 'auto';
            Menu.style.bottom = `${Math.max(8, vh - rect.top + 6)}px`;
        }
    };

    const SetOpen = (Next) =>
    {
        IsOpen = !!Next;
        Menu.classList.toggle('open', IsOpen);
        Trigger.setAttribute('aria-expanded', IsOpen ? 'true' : 'false');
        if (IsOpen)
        {
            RenderMenu();
            PositionMenu();
            if (typeof SearchInput.focus === 'function') SearchInput.focus();
        }
    };

    const RenderMenu = () =>
    {
        MenuList.innerHTML = '';
        const q = (SearchInput.value || '').trim().toLowerCase();
        const opts = (typeof Binding.Options === 'function' ? Binding.Options() : Binding.Options) || [];
        const cur = Binding.Read ? Binding.Read() : null;
        let shown = 0;

        for (const opt of opts)
        {
            const optVal = opt.id !== undefined ? opt.id : opt.value;
            const text = `${opt.label || ''} ${opt.sub || ''}`.toLowerCase();
            if (q && !text.includes(q)) continue;
            shown++;
            const item = Construct('button', 'menu-item');
            item.type = 'button';
            item.dataset.optionId = String(optVal);
            item.setAttribute('aria-checked', optVal === cur ? 'true' : 'false');

            if (Array.isArray(opt.stops) && opt.stops.length >= 2)
            {
                const bar = Construct('span', 'menu-grad-bar');
                bar.style.background = `linear-gradient(90deg, ${opt.stops.join(', ')})`;
                item.append(bar);
            }
            else if (opt.dot)
            {
                const d = Construct('span', 'row-dot');
                d.style.background = '#9ea2aa';
                item.append(d);
            }
            const copy = Construct('span', 'menu-item-copy', [
                Construct('span', 'menu-item-title', opt.label),
                opt.sub ? Construct('small', 'menu-item-sub', opt.sub) : null,
            ]);
            item.append(copy);
            if (optVal === cur)
            {
                const rad = Construct('span', 'menu-radio');
                item.append(rad);
            }
            item.addEventListener('click', (e) =>
            {
                e.stopPropagation();
                Binding.Write?.(optVal, opt);
                SetOpen(false);
                if (!Binding.Transient) Broadcast();
                else Sync();
            });
            MenuList.append(item);
        }
        if (!shown)
        {
            MenuList.append(Construct('div', 'menu-empty-hint', 'No matching items'));
        }
    };

    const Sync = () =>
    {
        const opts = (typeof Binding.Options === 'function' ? Binding.Options() : Binding.Options) || [];
        const cur = Binding.Read ? Binding.Read() : null;
        const found = opts.find((o) => (o.id !== undefined ? o.id : o.value) === cur) || opts[0];
        DimSpan.textContent = Binding.Prefix ? `${Binding.Prefix} ` : '';
        DimSpan.style.display = Binding.Prefix ? '' : 'none';
        LabelSpan.textContent = found ? found.label : (Binding.TriggerText || 'Choose...');

        if (found?.stops && found.stops.length >= 2)
        {
            Dot.style.display = 'none';
            GradSw.style.display = '';
            GradSw.style.background = `linear-gradient(90deg, ${found.stops.join(', ')})`;
        }
        else if (found?.dot)
        {
            GradSw.style.display = 'none';
            Dot.style.display = '';
            Dot.style.background = '#9ea2aa';
        }
        else
        {
            Dot.style.display = 'none';
            GradSw.style.display = 'none';
        }
        if (IsOpen) RenderMenu();
        ApplyAvailability(Row, Binding);
    };

    Trigger.addEventListener('click', (e) =>
    {
        e.stopPropagation();
        SetOpen(!IsOpen);
    });
    SearchInput.addEventListener('input', RenderMenu);
    SearchInput.addEventListener('click', (e) => e.stopPropagation());

    const onDocDown = (e) =>
    {
        if (Dropdown.isConnected === false)
        {
            document.removeEventListener('pointerdown', onDocDown);
            if (typeof window !== 'undefined') window.removeEventListener('keydown', onDocKey);
            return;
        }
        if (IsOpen && !Dropdown.contains?.(e.target)) SetOpen(false);
    };
    const onDocKey = (e) =>
    {
        if (Dropdown.isConnected === false)
        {
            document.removeEventListener('pointerdown', onDocDown);
            if (typeof window !== 'undefined') window.removeEventListener('keydown', onDocKey);
            return;
        }
        if (IsOpen && e.key === 'Escape') { SetOpen(false); }
    };
    document.addEventListener('pointerdown', onDocDown);
    if (typeof window !== 'undefined') window.addEventListener('keydown', onDocKey);

    if (Binding.Transient) Sync();
    else Register(Sync);
    return Row;
}

export function ChannelMaskRow(Parent, Binding)
{
    const Channels = Binding.Channels || [
        ['albedo', 'Albedo', 'Base Colour', 'albedo', '#9ea2aa'],
        ['roughness', 'Roughness', 'Surface Gloss', 'rough', '#9ea2aa'],
        ['metalness', 'Metalness', 'Conductor Mask', 'metal', '#9ea2aa'],
        ['height', 'Height', 'Relief / Bump', 'height', '#9ea2aa'],
    ];
    const Strict = !!Binding.StrictPresence;
    const MakeMap = (Val) => Object.fromEntries(Channels.map(([Key]) => [Key, Val]));
    const IsActive = (MapObj, Key) => (Strict ? MapObj?.[Key] === true : MapObj?.[Key] !== false);

    const PillsWrap = Construct('div', 'pbr-pill-wrap');
    const Dropdown = Construct('div', 'pill-dropdown pbr-add-dropdown');
    const TriggerCount = Construct('span', 'trigger-dim');
    const Trigger = Construct('button', 'pill-trigger pbr-add-trigger', [
        ConstructGlyph('Plus', 'glyph'),
        Construct('span', null, Binding.AddLabel || 'Channel'),
        TriggerCount,
        Construct('span', 'pill-caret-divider', ConstructGlyph('ChevronDown', 'glyph chev')),
    ]);
    Trigger.type = 'button';
    Trigger.setAttribute('aria-haspopup', 'menu');
    Trigger.setAttribute('aria-expanded', 'false');

    const Menu = Construct('div', 'pill-menu pbr-chan-menu');
    const SearchInput = document.createElement('input');
    SearchInput.type = 'text';
    SearchInput.className = 'pill-menu-search-input';
    SearchInput.placeholder = 'Filter channels...';
    const SearchBox = Construct('div', 'pill-menu-search', [ConstructGlyph('Search'), SearchInput]);
    const MenuLabel = Construct('div', 'menu-label', 'Available Channels to Add');
    const MenuList = Construct('div', 'pill-menu-list');

    const BtnAddAll = Construct('button', null, 'Add All');
    BtnAddAll.type = 'button';
    const BtnClearAll = Construct('button', null, 'Clear');
    BtnClearAll.type = 'button';
    const QuickBar = Construct('div', 'menu-quick', [BtnAddAll, BtnClearAll]);

    Menu.append(SearchBox, MenuLabel, MenuList, Construct('div', 'menu-sep'), QuickBar);
    Dropdown.append(Trigger, Menu);

    const Strip = Construct('div', 'pbr-chan-strip', [PillsWrap, Dropdown]);
    const Row = Construct('div', 'property-row channel-mask-row', [
        Construct('span', 'property-label', Binding.Label || 'PBR Channels'),
        Strip,
    ]);
    Parent.append(Row);

    let IsOpen = false;
    const Commit = (NextMap) =>
    {
        Binding.Write(NextMap);
        if (!Binding.Transient) Broadcast();
        else Sync();
    };

    const RenderDropdownOptions = (MapObj) =>
    {
        MenuList.innerHTML = '';
        const q = (SearchInput.value || '').trim().toLowerCase();
        const available = Channels.filter(([Key, Title, Sub]) =>
        {
            if (IsActive(MapObj, Key)) return false; // already added -> lives as active pill!
            return !q || `${Title} ${Sub} ${Key}`.toLowerCase().includes(q);
        });

        for (const [Key, Title, Sub, Kind] of available)
        {
            const item = Construct('button', `menu-item chan-${Kind}`);
            item.type = 'button';
            item.dataset.channel = Key;
            const dot = Construct('span', 'row-dot');
            dot.style.background = '#9ea2aa';
            item.append(
                dot,
                Construct('span', 'menu-item-copy', [
                    Construct('span', 'menu-item-title', Title),
                    Construct('small', 'menu-item-sub', Sub),
                ]),
                Construct('span', 'menu-state', '+ Add'),
            );
            item.addEventListener('click', (e) =>
            {
                e.stopPropagation();
                const next = { ...(Strict ? MakeMap(false) : MakeMap(true)), ...MapObj, [Key]: true };
                Commit(next);
            });
            MenuList.append(item);
        }

        if (!available.length)
        {
            const anyRemaining = Channels.some(([Key]) => !IsActive(MapObj, Key));
            MenuList.append(
                Construct(
                    'div',
                    'menu-empty-hint',
                    anyRemaining ? 'No matching channels' : 'All channels added — click × on a pill to return here',
                ),
            );
        }
    };

    const Sync = () =>
    {
        const MapObj = Binding.Read() || (Strict ? MakeMap(false) : MakeMap(true));
        PillsWrap.innerHTML = '';
        let inactiveCount = 0;

        for (const [Key, Title, , Kind] of Channels)
        {
            if (!IsActive(MapObj, Key)) { inactiveCount++; continue; }

            const Dot = Construct('i', 'pbr-chan-dot');
            Dot.style.background = '#9ea2aa';
            const RemoveX = Construct('span', 'pbr-chan-remove', ConstructGlyph('Close'));
            const Pill = Construct('button', `pbr-chan-pill active chan-${Kind}`, [
                Dot,
                Construct('span', null, Title),
                RemoveX,
            ]);
            Pill.type = 'button';
            Pill.dataset.channel = Key;
            Pill.title = `Remove ${Title} (moves back to Add Channel dropdown)`;
            Pill.addEventListener('click', (e) =>
            {
                e.stopPropagation();
                const next = { ...(Strict ? MakeMap(false) : MakeMap(true)), ...MapObj, [Key]: false };
                Commit(next);
            });
            PillsWrap.append(Pill);
        }

        if (inactiveCount === Channels.length)
        {
            PillsWrap.append(Construct('span', 'pbr-none-hint', 'None selected — add from dropdown'));
        }

        TriggerCount.textContent = inactiveCount > 0 ? `(${inactiveCount})` : '✓';
        Trigger.classList.toggle('all-added', inactiveCount === 0);
        if (IsOpen) RenderDropdownOptions(MapObj);
        ApplyAvailability(Row, Binding);
    };

    const PositionChanMenu = () =>
    {
        const rect = Trigger.getBoundingClientRect?.();
        if (!rect || rect.width <= 0) return;
        const vw = window.innerWidth || 1280;
        const vh = window.innerHeight || 800;
        const mw = 246;
        Menu.style.position = 'fixed';
        Menu.style.width = `${mw}px`;
        Menu.style.left = `${Math.max(8, Math.min(rect.left, vw - mw - 8))}px`;
        const below = vh - rect.bottom - 12;
        if (below >= 220 || below >= rect.top)
        {
            Menu.style.top = `${Math.min(vh - 60, rect.bottom + 6)}px`;
            Menu.style.bottom = 'auto';
        }
        else
        {
            Menu.style.top = 'auto';
            Menu.style.bottom = `${Math.max(8, vh - rect.top + 6)}px`;
        }
    };

    const SetOpen = (Next) =>
    {
        IsOpen = !!Next;
        Menu.classList.toggle('open', IsOpen);
        Trigger.setAttribute('aria-expanded', IsOpen ? 'true' : 'false');
        if (IsOpen)
        {
            RenderDropdownOptions(Binding.Read() || {});
            PositionChanMenu();
            if (typeof SearchInput.focus === 'function') SearchInput.focus();
        }
    };

    Trigger.addEventListener('click', (e) =>
    {
        e.stopPropagation();
        SetOpen(!IsOpen);
    });
    SearchInput.addEventListener('input', () => RenderDropdownOptions(Binding.Read() || {}));
    SearchInput.addEventListener('click', (e) => e.stopPropagation());
    BtnAddAll.addEventListener('click', (e) =>
    {
        e.stopPropagation();
        Commit(MakeMap(true));
        SetOpen(false);
    });
    BtnClearAll.addEventListener('click', (e) =>
    {
        e.stopPropagation();
        Commit(MakeMap(false));
    });

    const onDocDown = (e) =>
    {
        if (Dropdown.isConnected === false) { document.removeEventListener('pointerdown', onDocDown); return; }
        if (IsOpen && !Dropdown.contains?.(e.target)) SetOpen(false);
    };
    document.addEventListener('pointerdown', onDocDown);

    if (Binding.Transient) Sync();
    else Register(Sync);
    return Row;
}

export function MultiColourRow(Parent, Binding)
{
    const Strip = Construct('div', 'colour-stops-strip');
    const PillsWrap = Construct('div', 'colour-stops-pills');
    const AddBtn = Construct('button', 'pbr-add-trigger pill-trigger colour-add-btn', [
        ConstructGlyph('Plus'),
        Construct('span', null, 'Colour'),
    ]);
    AddBtn.type = 'button';
    Strip.append(PillsWrap, AddBtn);

    const Row = Construct('div', 'property-row channel-mask-row', [
        Construct('span', 'property-label', Binding.Label || 'Palette Stops'),
        Strip,
    ]);
    Parent.append(Row);

    const Sync = () =>
    {
        const list = Array.isArray(Binding.Read()) ? [...Binding.Read()] : ['#ff3d2e', '#ff9d3b', '#2fe6a7'];
        PillsWrap.innerHTML = '';
        list.forEach((hex, idx) =>
        {
            const cInp = document.createElement('input');
            cInp.type = 'color';
            cInp.className = 'colour-stop-native';
            cInp.value = /^#[0-9a-f]{6}$/i.test(hex) ? hex : '#e8c27a';
            cInp.addEventListener('input', () =>
            {
                const next = [...list];
                next[idx] = cInp.value;
                Binding.Write(next);
                if (!Binding.Transient) Broadcast();
            });

            const chip = Construct('div', 'colour-stop-pill', [
                cInp,
                Construct('span', 'colour-stop-hex', cInp.value.toUpperCase()),
            ]);

            if (list.length > 2)
            {
                const rm = Construct('button', 'colour-stop-remove', ConstructGlyph('Close'));
                rm.type = 'button';
                rm.title = 'Remove colour stop';
                rm.addEventListener('click', (e) =>
                {
                    e.stopPropagation();
                    const next = list.filter((_, i) => i !== idx);
                    Binding.Write(next);
                    if (!Binding.Transient) Broadcast();
                    else Sync();
                });
                chip.append(rm);
            }
            PillsWrap.append(chip);
        });
        ApplyAvailability(Row, Binding);
    };

    AddBtn.addEventListener('click', () =>
    {
        const list = Array.isArray(Binding.Read()) ? [...Binding.Read()] : ['#ff3d2e', '#2fe6a7'];
        const fallbacks = ['#ff3d2e', '#2fe6a7', '#63a8ff', '#ffd166', '#d97aff', '#ff6d6d', '#e8c27a'];
        const nextHex = fallbacks[list.length % fallbacks.length];
        list.push(nextHex);
        Binding.Write(list);
        if (!Binding.Transient) Broadcast();
        else Sync();
    });

    if (Binding.Transient) Sync();
    else Register(Sync);
    return Row;
}

const DEFAULT_CURVE_PRESETS = [
    ['Linear', [[0, 0], [1, 1]]],
    ['Ease', [[0, 0], [0.35, 0.14], [0.7, 0.82], [1, 1]]],
    ['Punch', [[0, 0], [0.22, 0.72], [0.62, 0.94], [1, 1]]],
    ['S-Curve', [[0, 0], [0.28, 0.08], [0.72, 0.92], [1, 1]]],
    ['Bell', [[0, 0], [0.22, 0.86], [0.5, 1], [0.78, 0.86], [1, 0]]],
    ['Flat', [[0, 1], [1, 1]]],
];

function EvalCurveLocal(Pts, t)
{
    const x = Math.max(0, Math.min(1, t));
    if (!Array.isArray(Pts) || !Pts.length) return x;
    if (Pts.length === 1) return Math.max(0, Math.min(1, Pts[0][1]));
    const S = [...Pts].sort((a, b) => a[0] - b[0]);
    if (x <= S[0][0]) return Math.max(0, Math.min(1, S[0][1]));
    if (x >= S[S.length - 1][0]) return Math.max(0, Math.min(1, S[S.length - 1][1]));
    for (let i = 0; i < S.length - 1; i++)
    {
        const [x0, y0] = S[i], [x1, y1] = S[i + 1];
        if (x >= x0 && x <= x1)
        {
            const Span = Math.max(1e-5, x1 - x0);
            const u = (x - x0) / Span;
            const P0 = S[Math.max(0, i - 1)], P3 = S[Math.min(S.length - 1, i + 2)];
            const m0 = ((y1 - P0[1]) / Math.max(1e-4, x1 - P0[0])) * Span * 0.5;
            const m1 = ((P3[1] - y0) / Math.max(1e-4, P3[0] - x0)) * Span * 0.5;
            const u2 = u * u, u3 = u2 * u;
            return Math.max(0, Math.min(1, (2 * u3 - 3 * u2 + 1) * y0 + (u3 - 2 * u2 + u) * m0 + (-2 * u3 + 3 * u2) * y1 + (u3 - u2) * m1));
        }
    }
    return Math.max(0, Math.min(1, S[S.length - 1][1]));
}

export function CurveRow(Parent, Binding)
{
    const W = 200, H = 92, Pad = 8;
    const Svg = document.createElementNS(SvgNamespace, 'svg');
    Svg.setAttribute('viewBox', `0 0 ${W} ${H}`);
    Svg.setAttribute('class', 'curve-editor-svg');

    const Grid = document.createElementNS(SvgNamespace, 'g');
    Grid.setAttribute('class', 'curve-grid');
    for (const f of [0.25, 0.5, 0.75])
    {
        const gx = Pad + f * (W - Pad * 2), gy = Pad + f * (H - Pad * 2);
        const lx = document.createElementNS(SvgNamespace, 'line');
        lx.setAttribute('x1', String(gx)); lx.setAttribute('x2', String(gx));
        lx.setAttribute('y1', String(Pad)); lx.setAttribute('y2', String(H - Pad));
        const ly = document.createElementNS(SvgNamespace, 'line');
        ly.setAttribute('x1', String(Pad)); ly.setAttribute('x2', String(W - Pad));
        ly.setAttribute('y1', String(gy)); ly.setAttribute('y2', String(gy));
        Grid.append(lx, ly);
    }
    const Diag = document.createElementNS(SvgNamespace, 'line');
    Diag.setAttribute('x1', String(Pad)); Diag.setAttribute('y1', String(H - Pad));
    Diag.setAttribute('x2', String(W - Pad)); Diag.setAttribute('y2', String(Pad));
    Diag.setAttribute('class', 'curve-diag');

    const Area = document.createElementNS(SvgNamespace, 'path');
    Area.setAttribute('class', 'curve-area');
    const Stroke = document.createElementNS(SvgNamespace, 'path');
    Stroke.setAttribute('class', 'curve-path');
    const Handles = document.createElementNS(SvgNamespace, 'g');

    Svg.append(Grid, Diag, Area, Stroke, Handles);

    const PresetsStrip = Construct('div', 'chip-strip curve-presets');
    for (const [PName, PPoints] of (Binding.Presets || DEFAULT_CURVE_PRESETS))
    {
        const Btn = Construct('button', 'chip-btn', PName);
        Btn.type = 'button';
        Btn.addEventListener('click', () =>
        {
            Binding.Write(PPoints.map(([x, y]) => [x, y]));
            if (!Binding.Transient) Broadcast();
            else Draw();
        });
        PresetsStrip.append(Btn);
    }

    const Box = Construct('div', 'curve-editor-box', [
        Binding.Label ? Construct('div', 'curve-editor-head', [
            Construct('span', 'property-label', Binding.Label),
            Binding.Detail ? Construct('small', null, Binding.Detail) : null,
        ]) : null,
        Svg,
        PresetsStrip,
    ]);
    Parent.append(Box);

    let DragIndex = -1;
    const ToSvgCoords = (nx, ny) => [Pad + nx * (W - Pad * 2), H - Pad - ny * (H - Pad * 2)];
    const FromClient = Event =>
    {
        const Rect = Svg.getBoundingClientRect();
        const rw = Math.max(1, Rect.width), rh = Math.max(1, Rect.height);
        const sx = ((Event.clientX - Rect.left) / rw) * W;
        const sy = ((Event.clientY - Rect.top) / rh) * H;
        return [
            Math.max(0, Math.min(1, (sx - Pad) / (W - Pad * 2))),
            Math.max(0, Math.min(1, 1 - (sy - Pad) / (H - Pad * 2))),
        ];
    };

    const Draw = () =>
    {
        const Pts = [...(Binding.Read() || [[0, 0], [1, 1]])].sort((a, b) => a[0] - b[0]);
        let dLine = '';
        for (let i = 0; i <= 32; i++)
        {
            const tx = i / 32;
            const ty = EvalCurveLocal(Pts, tx);
            const [vx, vy] = ToSvgCoords(tx, ty);
            dLine += (i === 0 ? `M ${vx.toFixed(1)} ${vy.toFixed(1)}` : ` L ${vx.toFixed(1)} ${vy.toFixed(1)}`);
        }
        Stroke.setAttribute('d', dLine);
        Area.setAttribute('d', `${dLine} L ${W - Pad} ${H - Pad} L ${Pad} ${H - Pad} Z`);

        Handles.innerHTML = '';
        Pts.forEach(([px, py], idx) =>
        {
            const [cx, cy] = ToSvgCoords(px, py);
            const C = document.createElementNS(SvgNamespace, 'circle');
            C.setAttribute('cx', String(cx.toFixed(1)));
            C.setAttribute('cy', String(cy.toFixed(1)));
            C.setAttribute('r', idx === DragIndex ? '5.5' : '4.2');
            C.setAttribute('class', 'curve-handle' + (idx === DragIndex ? ' active' : ''));
            C.dataset.ptIndex = String(idx);
            Handles.append(C);
        });
    };

    Svg.addEventListener('pointerdown', Event =>
    {
        const Pts = [...(Binding.Read() || [[0, 0], [1, 1]])].sort((a, b) => a[0] - b[0]).map(([x, y]) => [x, y]);
        const [nx, ny] = FromClient(Event);
        let BestIdx = -1, BestDist = 0.085;
        Pts.forEach(([px, py], i) =>
        {
            const d = Math.hypot(px - nx, py - ny);
            if (d < BestDist) { BestDist = d; BestIdx = i; }
        });

        if (Event.button === 2)
        {
            Event.preventDefault();
            if (BestIdx > 0 && BestIdx < Pts.length - 1 && Pts.length > 2)
            {
                Pts.splice(BestIdx, 1);
                Binding.Write(Pts);
                if (!Binding.Transient) Broadcast(); else Draw();
            }
            return;
        }

        if (BestIdx < 0)
        {
            Pts.push([nx, ny]);
            Pts.sort((a, b) => a[0] - b[0]);
            BestIdx = Pts.findIndex(p => p[0] === nx && p[1] === ny);
            Binding.Write(Pts);
        }
        DragIndex = BestIdx;
        Svg.setPointerCapture?.(Event.pointerId);
        Draw();
    });

    Svg.addEventListener('pointermove', Event =>
    {
        if (DragIndex < 0) return;
        const Pts = [...(Binding.Read() || [[0, 0], [1, 1]])].sort((a, b) => a[0] - b[0]).map(([x, y]) => [x, y]);
        if (DragIndex >= Pts.length) return;
        const [nx, ny] = FromClient(Event);
        if (DragIndex === 0) Pts[0] = [0, ny];
        else if (DragIndex === Pts.length - 1) Pts[Pts.length - 1] = [1, ny];
        else
        {
            const lo = Pts[DragIndex - 1][0] + 0.02;
            const hi = Pts[DragIndex + 1][0] - 0.02;
            Pts[DragIndex] = [Math.max(lo, Math.min(hi, nx)), ny];
        }
        Binding.Write(Pts);
        if (!Binding.Transient) Broadcast(); else Draw();
    });

    const EndDrag = Event =>
    {
        if (DragIndex < 0) return;
        DragIndex = -1;
        Svg.releasePointerCapture?.(Event.pointerId);
        Draw();
    };
    Svg.addEventListener('pointerup', EndDrag);
    Svg.addEventListener('pointercancel', EndDrag);
    Svg.addEventListener('contextmenu', e => e.preventDefault());

    if (Binding.Transient) Draw();
    else Register(Draw);
    return Box;
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
        Btn.addEventListener('click', () => { if (!Btn.disabled) { Act.Act(); Broadcast(); } });
        Row.append(Btn);
        if (Act.Availability)
        {
            Register(() =>
            {
                const St = Act.Availability();
                Btn.disabled = !St.Enabled;
                if (!St.Enabled && St.Reason) Btn.title = St.Reason;
                else Btn.removeAttribute('title');
            });
        }
    }
    Parent.append(Row);
    return Row;
}

export function MetricTrio(Parent, Items)
{
    const Grid = Construct('div', 'kpi-trio');
    Items.forEach(Item =>
    {
        const Val = Construct('strong', 'kpi-val');
        const Foot = Construct('small', 'kpi-sub');
        const Cell = Construct('div', 'kpi-card' + (Item.Highlight ? ' highlight' : ''), [
            Construct('span', 'kpi-label', Item.Label),
            Val,
            Foot
        ]);
        Grid.append(Cell);
        Register(() =>
        {
            const Res = Item.Read();
            Val.textContent = Res.value;
            Foot.textContent = Res.sub || '';
        });
    });
    Parent.append(Grid);
    return Grid;
}

export function SmoothScroll(Scroller, Content)
{
    if (!Scroller || !Content) return { ScrollTo() {} };
    let Target = Scroller.scrollTop, Current = Target, Running = false, LastWheel = 0;
    const Limit = () => Math.max(0, Scroller.scrollHeight - Scroller.clientHeight);
    const Advance = Now =>
    {
        const Max = Limit();
        Target = Now - LastWheel > 120 ? Math.min(Math.max(Target, 0), Max) : Math.min(Math.max(Target, -72), Max + 72);
        Current += (Target - Current) * 0.2;
        if (Math.abs(Target - Current) < 0.25) Current = Target;
        const Clamped = Math.min(Math.max(Current, 0), Max);
        Scroller.scrollTop = Clamped;
        const Stretch = Current - Clamped;
        Content.style.transform = Stretch ? `translateY(${(-Stretch * 0.4).toFixed(2)}px)` : '';
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
            Target = Math.min(Math.max(Position, 0), Limit());
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
    Toast.Timer = setTimeout(() => Element.classList.remove('shown'), 2400);
}
