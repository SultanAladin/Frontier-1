/*==========================================================================================================================================
                                                          STUDIOCONTROLS.JS
==========================================================================================================================================*/
// Unified Frontier Experimental Remastered UI component kit for UV Studio:
//   SectionCard, SliderRow (with scrubbable black value-pill + 26px spherical track),
//   SwitchRow, SelectRow, SegmentedRow, TextRow, ToggleTile, ActionRow, MetricTrio, SmoothScroll, Toast.

const SvgNamespace = 'http://www.w3.org/2000/svg';
const Refreshers = new Set();
const ScopeStack = [];

export const GlyphPaths = {
    Grid:       '<rect x="3" y="3" width="7.5" height="7.5" rx="1.5"/><rect x="13.5" y="3" width="7.5" height="7.5" rx="1.5"/><rect x="3" y="13.5" width="7.5" height="7.5" rx="1.5"/><rect x="13.5" y="13.5" width="7.5" height="7.5" rx="1.5"/>',
    Layers:     '<path d="M12 3 3 8l9 5 9-5z"/><path d="M3 12.5l9 5 9-5"/><path d="M3 17l9 5 9-5"/>',
    Scissors:   '<circle cx="6.5" cy="6.5" r="2.5"/><circle cx="6.5" cy="17.5" r="2.5"/><path d="M8.5 8.5 19 17.5M8.5 15.5 19 6.5"/>',
    Pin:        '<path d="M12 17v5M8 4h8l-1.2 5.5 2.7 3v2H6.5v-2l2.7-3z"/>',
    PinOff:     '<path d="M3 3l18 18M12 17v5M8 4h8l-.9 4M15.6 12.5H6.5v2h11v-1.2"/>',
    Pack:       '<rect x="3.5" y="3.5" width="17" height="17" rx="2.5"/><path d="M3.5 11.5h10M13.5 3.5v17M13.5 14h7"/>',
    Unwrap:     '<path d="M4 7.5 12 3l8 4.5v9L12 21l-8-4.5z"/><path d="M12 12l8-4.5M12 12v9M12 12 4 7.5"/>',
    Move:       '<path d="M12 3v18M3 12h18M9.5 5.5 12 3l2.5 2.5M9.5 18.5 12 21l2.5-2.5M5.5 9.5 3 12l2.5 2.5M18.5 9.5 21 12l-2.5 2.5"/>',
    Rotate:     '<path d="M20 12a8 8 0 1 1-2.4-5.7"/><path d="M20 4.5V9h-4.5"/>',
    Scale:      '<path d="M15 4h5v5M9 20H4v-5M20 4l-6 6M4 20l6-6"/>',
    Select:     '<path d="M5 4l13 7-5.5 1.8L10.7 19z"/>',
    BoxSel:     '<rect x="4" y="4" width="16" height="16" rx="2" stroke-dasharray="3 2.5"/>',
    CircleSel:  '<circle cx="12" cy="12" r="8" stroke-dasharray="3 2.5"/>',
    Cursor:     '<circle cx="12" cy="12" r="6"/><path d="M12 2.5v4M12 17.5v4M2.5 12h4M17.5 12h4"/>',
    Vertex:     '<circle cx="6" cy="18" r="2.2"/><circle cx="18" cy="18" r="2.2"/><circle cx="12" cy="6" r="2.2"/><path d="M7.1 16L11 8M16.9 16 13 8M8.3 18h7.4"/>',
    Edge:       '<path d="M5 17 19 7"/><circle cx="5" cy="17" r="2"/><circle cx="19" cy="7" r="2"/>',
    Face:       '<polygon points="12,4 20,9 17,19 7,19 4,9"/>',
    Island:     '<path d="M5 8.5 11 4l8 3.5-2 9-9 2.5z"/><circle cx="12" cy="12" r="1.5"/>',
    Stitch:     '<path d="M4 6v12M20 6v12M8 9h8M8 15h8"/>',
    Merge:      '<path d="M5 6l7 6-7 6M19 6l-7 6 7 6"/>',
    Split:      '<path d="M9 5v14M15 5v14M4 12h5M15 12h5"/>',
    Relax:      '<path d="M4 15c3-6 5-6 8 0s5 6 8 0"/>',
    Align:      '<path d="M4 5v14M8 8h11v3H8zM8 14h7v3H8z"/>',
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
    Maximize:   '<path d="M4 9V4h5M15 4h5v5M20 15v5h-5M9 20H4v-5"/>',
    Compact:    '<path d="M7 5v14M17 5v14"/>',
    Alert:      '<path d="M12 3.5 2.5 20h19z"/><path d="M12 10v4.5M12 17.4v.2"/>',
    Check:      '<path d="M5 12.5l4.2 4.2L19 7"/>',
    Gauge:      '<path d="M3.6 17.5a9 9 0 1 1 16.8 0"/><path d="M12 13.5l4.2-4.2"/><circle cx="12" cy="13.5" r="1.2"/>',
    Sparkles:   '<path d="M11 3.5l1.7 4.6 4.6 1.7-4.6 1.7L11 16.1l-1.7-4.6-4.6-1.7 4.6-1.7z"/><path d="M18.5 14.5v5M16 17h5"/>',
    Sliders:    '<path d="M4 6h9M17 6h3M4 12h3M11 12h9M4 18h11M19 18h1"/><circle cx="15" cy="6" r="2"/><circle cx="9" cy="12" r="2"/><circle cx="17" cy="18" r="2"/>',
    Reset:      '<path d="M3.5 12a8.5 8.5 0 1 0 2.6-6.1"/><path d="M3.5 4.5v4.6h4.6"/>',
    Terminal:   '<rect x="3" y="4" width="18" height="16" rx="2.5"/><path d="M7.5 9.5 11 12l-3.5 2.5M13 14.5h3.5"/>',
    ChevronDown:'<path d="M6 9l6 6 6-6"/>',
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
export function CloseScope(Scope) { const Top = ScopeStack.pop(); if (Top !== Scope && Scope) ScopeStack.splice(ScopeStack.indexOf(Scope), 1); }
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
    Toast.Timer = setTimeout(() => Element.classList.remove('shown'), 2200);
}
