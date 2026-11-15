/*==========================================================================================================================================
                                                          STUDIOCONTROLS.JS
==========================================================================================================================================*/
// 🧩 Remastered Studio UI control kit for Force Field + Wind Studio: reactive sliders, switches, compass dials, time series,
//    radial falloff plots, vector rows, and inline SVG glyphs.

const Clamp = (V, Lo, Hi) => Math.min(Hi, Math.max(Lo, V));
const SvgNS = 'http://www.w3.org/2000/svg';

const GLYPH_PATHS = {
    Wind: '<path d="M17.7 7.7A2.5 2.5 0 1 1 19 12H2"/><path d="M9.6 4.6A2 2 0 1 1 11 8H2"/><path d="M12.6 19.4A2 2 0 1 0 14 16H2"/>',
    Tornado: '<path d="M21 4H3"/><path d="M18 8H6"/><path d="M19 12H9"/><path d="M16 16h-6"/><path d="M11 20h2"/>',
    Front: '<path d="M3 16c4-8 10-10 18-8"/><path d="M5 20c3-4 8-5 15-4"/>',
    Downburst: '<path d="M12 3v13"/><path d="M6 19c2-2 4-3 6-3s4 1 6 3"/><path d="m8 11 4 4 4-4"/>',
    Thermal: '<path d="M12 20V5"/><path d="m7 9 5-5 5 5"/><path d="M6 16c2 2 4 3 6 3s4-1 6-3"/>',
    Jet: '<path d="M4 12h15"/><path d="m14 7 5 5-5 5"/><path d="M4 7h6M4 17h6"/>',
    Puff: '<path d="M17.5 19H9a7 7 0 1 1 6.7-9h1.8a4.5 4.5 0 1 1 0 9Z"/>',
    Blast: '<circle cx="12" cy="12" r="3"/><circle cx="12" cy="12" r="7" stroke-dasharray="3 3"/><circle cx="12" cy="12" r="10" stroke-opacity="0.45"/>',
    Wake: '<path d="M4 12h16"/><path d="M4 12c4-4 10-6 16-6"/><path d="M4 12c4 4 10 6 16 6"/>',
    Current: '<path d="M3 8h15l-3-3"/><path d="M3 16h15l-3 3"/>',
    Attract: '<circle cx="12" cy="12" r="2.5"/><path d="M12 2v4M12 18v4M2 12h4M18 12h4"/>',
    Repel: '<circle cx="12" cy="12" r="2.5"/><path d="m12 6-2 2m2-2 2 2M12 18l-2-2m2 2 2-2M6 12l2-2m-2 2 2 2M18 12l-2-2m2 2-2 2"/>',
    Orbit: '<circle cx="12" cy="12" r="7"/><circle cx="12" cy="12" r="2"/><path d="M19 12a7 7 0 0 1-7 7"/>',
    Lift: '<path d="M12 19V5"/><path d="m6 11 6-6 6 6"/>',
    Magnet: '<path d="m6 15-4-4 6.7-6.7c.8-.8 2-.8 2.8 0l2.8 2.8c.8.8.8 2 0 2.8L7.6 16.6"/><path d="M16 11l3-3"/>',
    Gravity: '<circle cx="12" cy="12" r="8"/><path d="M12 8v8m-3-3 3 3 3-3"/>',
    Damp: '<rect x="5" y="5" width="14" height="14" rx="3"/><path d="M9 12h6"/>',
    Field: '<circle cx="12" cy="12" r="9"/><path d="M12 3v18M3 12h18"/>',
    Eye: '<path d="M2 12s3.5-7 10-7 10 7 10 7-3.5 7-10 7-10-7-10-7Z"/><circle cx="12" cy="12" r="3"/>',
    EyeOff: '<path d="M10.6 5A10.7 10.7 0 0 1 12 5c6.5 0 10 7 10 7a18.2 18.2 0 0 1-3.1 4.2"/><path d="M6.6 6.6A17.7 17.7 0 0 0 2 12s3.5 7 10 7a9.7 9.7 0 0 0 5.4-1.6"/><line x1="2" y1="2" x2="22" y2="22"/>',
    Plus: '<path d="M12 5v14M5 12h14"/>',
    Check: '<path d="M20 6 9 17l-5-5"/>',
    Layers: '<path d="m12 2 9 4.5-9 4.5-9-4.5Z"/><path d="m3 12 9 4.5 9-4.5"/><path d="m3 17.5 9 4.5 9-4.5"/>',
    Move: '<path d="M5 9l-3 3 3 3M9 5l3-3 3 3M19 9l3 3-3 3M9 19l3 3 3-3M2 12h20M12 2v20"/>',
    Activity: '<path d="M22 12h-4l-3 7-5-14-3 7H2"/>',
    Swirl: '<path d="M12 12c0-2 1.8-3 3-3 2.5 0 4 2.2 4 4.5 0 3.5-3.2 6-7 6-4.5 0-8-3.5-8-8 0-5.2 4.5-9 9.5-9"/>',
    Lattice: '<rect x="3" y="3" width="18" height="18" rx="2"/><path d="M3 9h18M3 15h18M9 3v18M15 3v18"/>',
    Globe: '<circle cx="12" cy="12" r="9"/><path d="M3 12h18"/><path d="M12 3a13 13 0 0 1 0 18 13 13 0 0 1 0-18Z"/>',
    Target: '<circle cx="12" cy="12" r="8"/><circle cx="12" cy="12" r="3"/><path d="M12 2v3m0 14v3M2 12h3m14 0h3"/>',
    Compass: '<circle cx="12" cy="12" r="9"/><polygon points="16.2 7.8 14.1 14.1 7.8 16.2 9.9 9.9"/>',
    Clock: '<circle cx="12" cy="12" r="9"/><path d="M12 7v5l3 2"/>',
    Gauge: '<path d="m12 14 4-4"/><path d="M3.3 19a10 10 0 1 1 17.4 0"/>',
    Sparkles: '<path d="m12 3 1.5 4.5L18 9l-4.5 1.5L12 15l-1.5-4.5L6 9l4.5-1.5Z"/><path d="m5 17 .8 2.2L8 20l-2.2.8L5 23l-.8-2.2L2 20l2.2-.8Z"/>',
    Cloud: '<path d="M17.5 19H9a7 7 0 1 1 6.7-9h1.8a4.5 4.5 0 1 1 0 9Z"/>',
    Play: '<polygon points="6 4 20 12 6 20 6 4"/>',
    Pause: '<rect x="6" y="4" width="4" height="16"/><rect x="14" y="4" width="4" height="16"/>',
    Zap: '<polygon points="13 2 3 14 12 14 11 22 21 10 12 10 13 2"/>',
    Copy: '<rect x="9" y="9" width="11" height="11" rx="2"/><path d="M5 15H4a2 2 0 0 1-2-2V4a2 2 0 0 1 2-2h9a2 2 0 0 1 2 2v1"/>',
    Reset: '<path d="M3 12a9 9 0 1 0 3-6.7L3 8"/><path d="M3 3v5h5"/>',
    Trash: '<path d="M3 6h18"/><path d="M8 6V4h8v2"/><path d="M19 6l-1 14H6L5 6"/>',
    Close: '<path d="M18 6 6 18M6 6l12 12"/>',
    Ruler: '<path d="M4 15h16M4 15v-4m4 4v-2m4 2v-3m4 3v-2m4 2v-4"/>',
    FlowLine: '<path d="M3 16c4-8 8 4 12-4 2-4 4-4 6-4"/><circle cx="19" cy="8" r="1.8" fill="currentColor"/>'
};

export function Construct(Tag, ClassName, Children)
{
    const El = document.createElement(Tag);
    if (ClassName) El.className = ClassName;
    if (Children !== undefined && Children !== null)
    {
        if (Array.isArray(Children)) { for (const Child of Children) if (Child !== null && Child !== undefined) El.append(Child); }
        else if (typeof Children === 'string' || typeof Children === 'number') El.textContent = String(Children);
        else El.append(Children);
    }
    return El;
}

export function ConstructSvg(Tag, Attrs = {}, Children = [])
{
    const El = document.createElementNS(SvgNS, Tag);
    for (const [K, V] of Object.entries(Attrs)) El.setAttribute(K, String(V));
    for (const Child of Array.isArray(Children) ? Children : [Children]) if (Child) El.append(Child);
    return El;
}

export function ConstructGlyph(Name)
{
    const Svg = ConstructSvg('svg', { viewBox: '0 0 24 24', fill: 'none', stroke: 'currentColor', 'stroke-width': '1.8', 'stroke-linecap': 'round', 'stroke-linejoin': 'round' });
    Svg.innerHTML = GLYPH_PATHS[Name] || GLYPH_PATHS.Field;
    return Svg;
}

export function ResolveCompassPoint(Deg)
{
    const Dirs = ['N', 'NNE', 'NE', 'ENE', 'E', 'ESE', 'SE', 'SSE', 'S', 'SSW', 'SW', 'WSW', 'W', 'WNW', 'NW', 'NNW'];
    return Dirs[Math.round((((Deg % 360) + 360) % 360) / 22.5) % 16];
}

//------------------------------------------------------------------------------------------------------------------------
//                                                 REACTIVE SCOPES
//------------------------------------------------------------------------------------------------------------------------

let Listeners = [];
let ActiveScope = null;

export function OpenScope()
{
    const Scope = new Set();
    ActiveScope = Scope;
    return Scope;
}

export function ReleaseScope(Scope)
{
    if (!Scope) return;
    Listeners = Listeners.filter((Entry) => !Scope.has(Entry));
}

export function Register(Fn)
{
    const Entry = Fn;
    Listeners.push(Entry);
    if (ActiveScope) ActiveScope.add(Entry);
    Fn();
}

export function Broadcast()
{
    for (const Fn of Listeners) Fn();
}

export function Toast(Msg)
{
    let Box = document.getElementById('StudioToast');
    if (!Box)
    {
        Box = Construct('div', 'studio-toast');
        Box.id = 'StudioToast';
        document.body.append(Box);
    }
    Box.textContent = Msg;
    Box.classList.add('shown');
    clearTimeout(Box._Timer);
    Box._Timer = setTimeout(() => Box.classList.remove('shown'), 1900);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                   INSPECTOR ROWS
//------------------------------------------------------------------------------------------------------------------------

export function ConstructMetric(Host, Value, Digits, Unit)
{
    Host.textContent = '';
    const Safe = Number.isFinite(Value) ? Value : 0;
    Host.append(document.createTextNode(Safe.toFixed(Digits)));
    if (Unit) Host.append(Construct('small', null, ' ' + Unit));
}

export function SectionCard(Host, { Id, Title, Glyph, Status })
{
    const Card = Construct('section', 'section-card');
    Card.dataset.sectionId = Id;
    const StatusEl = Construct('span', 'section-status');
    const Head = Construct('button', 'section-head', [
        Construct('div', 'section-head-left', [Construct('span', 'section-icon', ConstructGlyph(Glyph)), Construct('span', 'section-title', Title)]),
        StatusEl
    ]);
    Head.type = 'button';
    const Inner = Construct('div', 'section-body');
    Head.addEventListener('click', () => Card.classList.toggle('folded'));
    Card.append(Head, Inner);
    Host.append(Card);
    if (Status) Register(() => { StatusEl.textContent = Status(); });
    return { Card, Inner };
}

export function PillTabs(Host, B)
{
    const Wrap = Construct('div', 'pill-tabs');
    const Btns = B.Options.map(([Label, Val]) =>
    {
        const Btn = Construct('button', 'pill-tab', Label);
        Btn.type = 'button';
        Btn.addEventListener('click', () => { B.Write(Val); Broadcast(); });
        Wrap.append(Btn);
        return [Btn, Val];
    });
    Host.append(Wrap);
    Register(() =>
    {
        const Cur = B.Read();
        for (const [Btn, Val] of Btns) Btn.classList.toggle('active', Cur === Val);
    });
}

export function SegmentedRow(Host, B)
{
    const Wrap = Construct('div', 'control-row segmented-row');
    if (B.Label) Wrap.append(Construct('div', 'control-label', B.Label));
    const Bar = Construct('div', 'seg-bar');
    const Btns = B.Options.map(([Label, Val]) =>
    {
        const Btn = Construct('button', 'seg-btn', Label);
        Btn.type = 'button';
        Btn.addEventListener('click', () => { B.Write(Val); Broadcast(); });
        Bar.append(Btn);
        return [Btn, Val];
    });
    Wrap.append(Bar);
    Host.append(Wrap);
    Register(() =>
    {
        const Cur = B.Read();
        const Avail = B.Availability ? B.Availability() : { Enabled: true };
        Wrap.classList.toggle('disabled', !Avail.Enabled);
        for (const [Btn, Val] of Btns) Btn.classList.toggle('active', Cur === Val);
    });
}

export function SwitchRow(Host, B)
{
    const Row = Construct('label', 'control-row switch-row');
    const Text = Construct('div', 'switch-text', [
        Construct('div', 'control-label', B.Label),
        B.Detail ? Construct('div', 'control-detail', B.Detail) : null
    ]);
    const Toggle = Construct('button', 'switch-pill', Construct('i'));
    Toggle.type = 'button';
    Toggle.addEventListener('click', () => { B.Write(!B.Read()); Broadcast(); });
    Row.append(Text, Toggle);
    Host.append(Row);
    Register(() => { Toggle.classList.toggle('on', Boolean(B.Read())); });
}

export function ToggleTile(Host, B)
{
    const Btn = Construct('button', 'toggle-tile', [
        Construct('span', 'tile-glyph', ConstructGlyph(B.Glyph || 'Field')),
        Construct('span', 'tile-label', B.Label),
        Construct('span', 'tile-state')
    ]);
    Btn.type = 'button';
    Btn.addEventListener('click', () => { B.Write(!B.Read()); Broadcast(); });
    Host.append(Btn);
    Register(() =>
    {
        const On = Boolean(B.Read());
        Btn.classList.toggle('on', On);
        Btn.querySelector('.tile-state').textContent = On ? 'ON' : 'OFF';
    });
}

export function SliderRow(Host, B)
{
    const Row = Construct('div', 'property-row slider-row');
    const LabelEl = Construct('span', 'property-label', B.Label);
    const ValInput = Construct('input', 'num-input');
    ValInput.type = 'text';
    const UnitCell = Construct('span', 'unit-cell', B.Unit || '');
    const ValPill = Construct('div', 'value-pill', [ValInput, UnitCell]);

    const Range = Construct('input', 'pill-range');
    Range.type = 'range';
    const Min = B.Minimum ?? 0, Max = B.Maximum ?? 100;
    const Log = B.Scale === 'Logarithmic' && Min > 0;
    if (Log)
    {
        Range.min = '0';
        Range.max = '1000';
        Range.step = '1';
    }
    else
    {
        Range.min = String(Min);
        Range.max = String(Max);
        Range.step = String(B.Step ?? 0.1);
    }
    const ToSlider = (V) => Log ? Math.round((Math.log(Clamp(V, Min, Max) / Min) / Math.log(Max / Min)) * 1000) : Clamp(V, Min, Max);
    const FromSlider = (S) => Log ? Min * Math.pow(Max / Min, Clamp(S, 0, 1000) / 1000) : S;
    const FractionOf = (V) => Log
        ? Clamp(Math.log(Clamp(V, Min, Max) / Min) / Math.max(1e-6, Math.log(Max / Min)), 0, 1)
        : Clamp((V - Min) / Math.max(1e-6, Max - Min), 0, 1);

    Range.addEventListener('input', () =>
    {
        const Val = FromSlider(parseFloat(Range.value));
        Range.style?.setProperty?.('--fraction', FractionOf(Val).toFixed(4));
        B.Write(Log ? Number(Val.toPrecision(3)) : Val);
        Broadcast();
    });
    ValInput.addEventListener('change', () =>
    {
        const Parsed = B.Parse ? B.Parse(ValInput.value, B.Unit) : parseFloat(ValInput.value);
        if (Number.isFinite(Parsed)) { B.Write(Clamp(Parsed, Min, Max)); Broadcast(); }
        else Broadcast();
    });

    const Control = Construct('div', 'slider-control', [ValPill, Range]);
    Row.append(LabelEl, Control);
    if (B.Labels) Row.append(Construct('div', 'slider-ends', [Construct('span', null, B.Labels[0]), Construct('span', null, B.Labels[1])]));
    Host.append(Row);

    Register(() =>
    {
        const Cur = Number(B.Read() ?? 0);
        const Avail = B.Availability ? B.Availability() : { Enabled: true };
        Row.classList.toggle('unavailable', !Avail.Enabled);
        Row.classList.toggle('disabled', !Avail.Enabled);
        Range.disabled = !Avail.Enabled;
        Range.value = String(ToSlider(Cur));
        Range.style?.setProperty?.('--fraction', FractionOf(Cur).toFixed(4));
        if (document.activeElement !== ValInput)
        {
            if (B.Format)
            {
                const [Text, Unit] = B.Format(Cur);
                ValInput.value = Text;
                UnitCell.textContent = Unit;
            }
            else
            {
                ValInput.value = Cur.toFixed(B.Digits ?? 1);
                UnitCell.textContent = B.Unit || '';
            }
        }
    });
}

export function VectorRow(Host, B)
{
    const Row = Construct('div', 'control-row vector-row');
    Row.append(Construct('div', 'control-label', B.Label));
    const Grid = Construct('div', 'vector-inputs');
    const Axes = ['X', 'Y', 'Z'];
    const Inputs = Axes.map((Axis, Idx) =>
    {
        const Box = Construct('div', 'vec-cell');
        const Tag = Construct('span', 'vec-axis', Axis);
        const In = Construct('input', 'vec-input');
        In.type = 'number';
        In.step = String(B.Step ?? 1);
        In.addEventListener('change', () =>
        {
            const Arr = [...(B.Read() || [0, 0, 0])];
            const V = parseFloat(In.value);
            if (Number.isFinite(V))
            {
                Arr[Idx] = B.Minimum !== undefined ? Clamp(V, B.Minimum, B.Maximum ?? 1e6) : V;
                B.Write(Arr);
                Broadcast();
            }
        });
        Box.append(Tag, In);
        Grid.append(Box);
        return In;
    });
    Row.append(Grid);
    Host.append(Row);
    Register(() =>
    {
        const Arr = B.Read() || [0, 0, 0];
        Inputs.forEach((In, I) => { if (document.activeElement !== In) In.value = Number((Arr[I] || 0).toFixed(B.Digits ?? 1)); });
    });
}

export function ChipRow(Host, Builders)
{
    const Row = Construct('div', 'chip-row');
    const Chips = Builders.map((Resolve) =>
    {
        const Val = Construct('strong');
        const Sub = Construct('span');
        const Chip = Construct('div', 'info-chip', [Val, Sub]);
        Row.append(Chip);
        return [Val, Sub, Resolve];
    });
    Host.append(Row);
    Register(() =>
    {
        for (const [Val, Sub, Resolve] of Chips)
        {
            const [V, S] = Resolve();
            Val.textContent = V;
            Sub.textContent = S;
        }
    });
}

export function ChipToggles(Host, B)
{
    const Wrap = Construct('div', 'control-row');
    Wrap.append(Construct('div', 'control-label', B.Label));
    const Grid = Construct('div', 'chip-toggles');
    const Btns = B.Options.map(([Text, Key]) =>
    {
        const Btn = Construct('button', 'chip-toggle', Text);
        Btn.type = 'button';
        Btn.addEventListener('click', () =>
        {
            const Set = new Set(B.Read() || []);
            if (Set.has(Key)) Set.delete(Key); else Set.add(Key);
            B.Write([...Set]);
            Broadcast();
        });
        Grid.append(Btn);
        return [Btn, Key];
    });
    const Detail = Construct('div', 'control-detail');
    Wrap.append(Grid, Detail);
    Host.append(Wrap);
    Register(() =>
    {
        const Cur = new Set(B.Read() || []);
        for (const [Btn, Key] of Btns) Btn.classList.toggle('on', Cur.has(Key));
        if (B.Detail) Detail.textContent = B.Detail([...Cur]);
    });
}

export function ReadoutTile(Host, { Label, Glyph, Resolve })
{
    const Val = Construct('strong');
    const Sub = Construct('small');
    const Tile = Construct('div', 'readout-tile', [
        Construct('div', 'tile-head', [ConstructGlyph(Glyph || 'Gauge'), Construct('span', null, Label)]),
        Construct('div', 'tile-value', [Val, Sub])
    ]);
    Host.append(Tile);
    const Refresh = () => { const [A, B] = Resolve(); Val.textContent = A; Sub.textContent = ' ' + B; };
    Register(Refresh);
    return Refresh;
}

export function StatGrid(Host, Cells)
{
    const Grid = Construct('div', 'stat-grid');
    for (const [Label, Resolve] of Cells)
    {
        const V = Construct('b');
        Grid.append(Construct('div', 'stat-cell', [V, Construct('span', null, Label)]));
        Register(() => { V.textContent = Resolve(); });
    }
    Host.append(Grid);
}

export function ActionRow(Host, Actions)
{
    const Row = Construct('div', 'action-row');
    for (const A of Actions)
    {
        const Btn = Construct('button', 'action-btn' + (A.Tone ? ' ' + A.Tone : ''), [
            A.Glyph ? ConstructGlyph(A.Glyph) : null,
            Construct('span', null, A.Label)
        ]);
        Btn.type = 'button';
        Btn.addEventListener('click', A.Act);
        Row.append(Btn);
    }
    Host.append(Row);
}

export function PolarDial(Host, B)
{
    const Wrap = Construct('div', 'polar-dial-wrap');
    const Canvas = Construct('canvas', 'polar-dial');
    Canvas.width = 160;
    Canvas.height = 160;
    const Meta = Construct('div', 'polar-meta');
    Wrap.append(Canvas, Meta);
    Host.append(Wrap);

    const UpdateFromPointer = (E) =>
    {
        const R = Canvas.getBoundingClientRect();
        const DX = E.clientX - (R.left + R.width / 2);
        const DY = E.clientY - (R.top + R.height / 2);
        const Deg = ((Math.atan2(DX, -DY) * 180) / Math.PI + 360) % 360;
        B.Write(Math.round(Deg));
        Broadcast();
    };
    let Drag = false;
    Canvas.addEventListener('pointerdown', (E) => { Drag = true; Canvas.setPointerCapture(E.pointerId); UpdateFromPointer(E); });
    Canvas.addEventListener('pointermove', (E) => { if (Drag) UpdateFromPointer(E); });
    Canvas.addEventListener('pointerup', () => { Drag = false; });

    Register(() =>
    {
        const Deg = Number(B.Read() || 0);
        const Ctx = Canvas.getContext('2d');
        const W = Canvas.width, H = Canvas.height, CX = W / 2, CY = H / 2, Rad = 58;
        Ctx.clearRect(0, 0, W, H);
        Ctx.strokeStyle = 'rgba(255,255,255,0.12)';
        Ctx.lineWidth = 1;
        Ctx.beginPath(); Ctx.arc(CX, CY, Rad, 0, Math.PI * 2); Ctx.stroke();
        Ctx.beginPath(); Ctx.arc(CX, CY, Rad * 0.52, 0, Math.PI * 2); Ctx.stroke();
        Ctx.fillStyle = '#7e8694';
        Ctx.font = '500 10px sans-serif';
        Ctx.textAlign = 'center';
        Ctx.fillText('N', CX, CY - Rad - 6);
        Ctx.fillText('S', CX, CY + Rad + 14);
        Ctx.fillText('E', CX + Rad + 10, CY + 3);
        Ctx.fillText('W', CX - Rad - 10, CY + 3);

        const A = (Deg * Math.PI) / 180;
        const TX = CX + Math.sin(A) * Rad * 0.85;
        const TY = CY - Math.cos(A) * Rad * 0.85;
        Ctx.strokeStyle = '#8fd3c4';
        Ctx.lineWidth = 2.4;
        Ctx.beginPath(); Ctx.moveTo(CX, CY); Ctx.lineTo(TX, TY); Ctx.stroke();
        Ctx.fillStyle = '#d1f773';
        Ctx.beginPath(); Ctx.arc(TX, TY, 4.5, 0, Math.PI * 2); Ctx.fill();

        const Extra = B.Resolve ? B.Resolve() : null;
        Meta.textContent = `${Deg.toFixed(0)}° ${ResolveCompassPoint(Deg)}` + (Extra?.Caption ? ` · ${Extra.Caption}` : '');
    });
}

export function CurvePlot(Host, { Height = 126, Resolve })
{
    const Wrap = Construct('div', 'curve-plot-wrap');
    const Canvas = Construct('canvas', 'curve-plot');
    const Cap = Construct('div', 'curve-caption');
    Wrap.append(Canvas, Cap);
    Host.append(Wrap);

    const Draw = () =>
    {
        const Spec = Resolve();
        const DPR = Math.min(2, window.devicePixelRatio || 1);
        const W = Math.max(260, Wrap.clientWidth || 300);
        const H = Height;
        Canvas.width = Math.round(W * DPR);
        Canvas.height = Math.round(H * DPR);
        Canvas.style.width = `${W}px`;
        Canvas.style.height = `${H}px`;
        const Ctx = Canvas.getContext('2d');
        Ctx.setTransform(DPR, 0, 0, DPR, 0, 0);
        Ctx.clearRect(0, 0, W, H);
        const Pad = { L: 34, R: 12, T: 14, B: 22 };
        const PW = W - Pad.L - Pad.R, PH = H - Pad.T - Pad.B;
        const [X0, X1] = Spec.XRange || [0, 1];
        const [Y0, Y1] = Spec.YRange || [0, 1];
        const MapX = (X) => Pad.L + ((X - X0) / Math.max(1e-6, X1 - X0)) * PW;
        const MapY = (Y) => Pad.T + (1 - (Y - Y0) / Math.max(1e-6, Y1 - Y0)) * PH;

        Ctx.strokeStyle = 'rgba(255,255,255,0.07)';
        Ctx.lineWidth = 1;
        for (const [YV, Lab] of Spec.YTicks || [])
        {
            const Y = MapY(YV);
            Ctx.beginPath(); Ctx.moveTo(Pad.L, Y); Ctx.lineTo(W - Pad.R, Y); Ctx.stroke();
            Ctx.fillStyle = '#7b8290'; Ctx.font = '10px sans-serif'; Ctx.textAlign = 'right';
            Ctx.fillText(Lab, Pad.L - 5, Y + 3);
        }
        for (const [XV, Lab] of Spec.XTicks || [])
        {
            const X = MapX(XV);
            Ctx.fillStyle = '#7b8290'; Ctx.font = '10px sans-serif'; Ctx.textAlign = 'center';
            Ctx.fillText(Lab, X, H - 5);
        }
        for (const Guide of Spec.Guides || [])
        {
            const GY = MapY(Guide.Y);
            Ctx.save(); Ctx.setLineDash([4, 4]); Ctx.strokeStyle = 'rgba(255,255,255,0.22)';
            Ctx.beginPath(); Ctx.moveTo(Pad.L, GY); Ctx.lineTo(W - Pad.R, GY); Ctx.stroke();
            if (Guide.Text) { Ctx.fillStyle = '#9aa3b2'; Ctx.textAlign = 'left'; Ctx.fillText(Guide.Text, Pad.L + 4, GY - 4); }
            Ctx.restore();
        }
        for (const C of Spec.Curves || [])
        {
            if (!C.Points?.length) continue;
            Ctx.beginPath();
            C.Points.forEach(([X, Y], I) => { const PX = MapX(X), PY = MapY(Y); if (I) Ctx.lineTo(PX, PY); else Ctx.moveTo(PX, PY); });
            Ctx.strokeStyle = C.Stroke || '#8fd3c4';
            Ctx.lineWidth = C.Width || 1.8;
            Ctx.stroke();
            if (C.Fill)
            {
                Ctx.lineTo(MapX(C.Points[C.Points.length - 1][0]), MapY(Y0));
                Ctx.lineTo(MapX(C.Points[0][0]), MapY(Y0));
                Ctx.closePath();
                Ctx.fillStyle = C.Fill;
                Ctx.fill();
            }
        }
        for (const M of Spec.Markers || [])
        {
            const MX = MapX(M.X), MY = MapY(M.Y);
            Ctx.fillStyle = '#ffffff';
            Ctx.beginPath(); Ctx.arc(MX, MY, 3, 0, Math.PI * 2); Ctx.fill();
            if (M.Label) { Ctx.fillStyle = '#d8dee9'; Ctx.textAlign = 'left'; Ctx.fillText(M.Label, Math.min(W - 44, MX + 5), MY - 4); }
        }
        Cap.textContent = Spec.Caption || '';
    };
    Register(Draw);
    return { Refresh: Draw };
}

export function TimeSeries(Host, Spec)
{
    const Canvas = Construct('canvas', 'time-series-canvas');
    Host.append(Canvas);
    const Refresh = () =>
    {
        const DPR = Math.min(2, window.devicePixelRatio || 1);
        const W = Math.max(240, Host.clientWidth - 28 || 280);
        const H = Spec.Height || 98;
        Canvas.width = Math.round(W * DPR);
        Canvas.height = Math.round(H * DPR);
        Canvas.style.width = `${W}px`;
        Canvas.style.height = `${H}px`;
        const Ctx = Canvas.getContext('2d');
        Ctx.setTransform(DPR, 0, 0, DPR, 0, 0);
        Ctx.clearRect(0, 0, W, H);
        const Samples = Spec.Samples || [];
        const Target = typeof Spec.Target === 'function' ? Spec.Target() : (Spec.Target || 10);
        const MaxV = Math.max(Target * (Spec.Headroom || 1.2), ...Samples, 1);
        const Pad = { L: 6, R: 6, T: 10, B: 18 };
        const PW = W - Pad.L - Pad.R, PH = H - Pad.T - Pad.B;
        const GY = Pad.T + PH * (1 - Target / MaxV);
        Ctx.save(); Ctx.setLineDash([4, 4]); Ctx.strokeStyle = 'rgba(255,255,255,0.22)';
        Ctx.beginPath(); Ctx.moveTo(Pad.L, GY); Ctx.lineTo(W - Pad.R, GY); Ctx.stroke(); Ctx.restore();

        if (Samples.length > 1)
        {
            Ctx.beginPath();
            Samples.forEach((V, I) =>
            {
                const X = Pad.L + (I / Math.max(1, Spec.Capacity - 1)) * PW;
                const Y = Pad.T + PH * (1 - Clamp(V / MaxV, 0, 1));
                if (I) Ctx.lineTo(X, Y); else Ctx.moveTo(X, Y);
            });
            Ctx.strokeStyle = '#8fd3c4';
            Ctx.lineWidth = 1.8;
            Ctx.stroke();
            Ctx.lineTo(Pad.L + ((Samples.length - 1) / Math.max(1, Spec.Capacity - 1)) * PW, Pad.T + PH);
            Ctx.lineTo(Pad.L, Pad.T + PH);
            Ctx.closePath();
            Ctx.fillStyle = 'rgba(143,211,196,0.12)';
            Ctx.fill();
        }
        const Labels = Spec.TimeLabels || ['−10 s', '−5 s', 'now'];
        Ctx.fillStyle = '#717886'; Ctx.font = '10px sans-serif';
        Ctx.textAlign = 'left'; Ctx.fillText(Labels[0], Pad.L, H - 3);
        Ctx.textAlign = 'center'; Ctx.fillText(Labels[1], W / 2, H - 3);
        Ctx.textAlign = 'right'; Ctx.fillText(Labels[2], W - Pad.R, H - 3);
    };
    Refresh();
    return { Refresh };
}

export const StudioControls = {
    Construct, ConstructSvg, ConstructGlyph, ResolveCompassPoint,
    OpenScope, ReleaseScope, Register, Broadcast, Toast,
    ConstructMetric, SectionCard, PillTabs, SegmentedRow, SwitchRow, ToggleTile,
    SliderRow, VectorRow, ChipRow, ChipToggles, ReadoutTile, StatGrid, ActionRow, PolarDial, CurvePlot, TimeSeries
};
