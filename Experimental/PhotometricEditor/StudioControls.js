/*==========================================================================================================================================
                                                            STUDIOCONTROLS.JS
==========================================================================================================================================*/
// Shared Frontier studio widget library: SVG icon registry (extended with Project Zero photometric & environment light icons),
// SliderPill, ValuePill, TogglePill, SegmentedPill, SelectPill, CollapsibleSection, PropertyRow.

const ICON_SVG_PROPS = {
    viewBox: '0 0 16 16',
    fill: 'none',
    stroke: 'currentColor',
    strokeWidth: '1.35',
    strokeLinecap: 'round',
    strokeLinejoin: 'round'
};

const GLYPH_BODIES = {
    Close: '<path d="M4.2 4.2l7.6 7.6M11.8 4.2l-7.6 7.6"/>',
    Chevron: '<path d="M5.75 4.25l4.25 3.75-4.25 3.75"/>',
    Plus: '<path d="M8 3.25v9.5M3.25 8h9.5"/>',
    Trash: '<path d="M2.75 4.75h10.5M6 4.75V3.25a1 1 0 0 1 1-1h2a1 1 0 0 1 1 1v1.5M4.5 4.75l.65 8.1a1.1 1.1 0 0 0 1.1 1h3.5a1.1 1.1 0 0 0 1.1-1l.65-8.1"/>',
    Duplicate: '<rect x="5.25" y="5.25" width="7.5" height="7.5" rx="1.2"/><path d="M3.25 10.75v-6.3a1.2 1.2 0 0 1 1.2-1.2h6.3"/>',
    EyeOpen: '<path d="M1.65 8S4 4.1 8 4.1 14.35 8 14.35 8 12 11.9 8 11.9 1.65 8 1.65 8z"/><circle cx="8" cy="8" r="1.8"/>',
    EyeClosed: '<path d="M2.4 2.4l11.2 11.2M6.1 4.4A6.7 6.7 0 0 1 8 4.1c4 0 6.35 3.9 6.35 3.9a12.4 12.4 0 0 1-2.25 2.55M4.3 5.7A11.8 11.8 0 0 0 1.65 8S4 11.9 8 11.9c.7 0 1.35-.1 1.95-.27"/>',
    Play: '<path d="M5 3.4l7.6 4.6L5 12.6z" fill="currentColor" stroke="none"/>',
    Pause: '<path d="M5 3.75h2.25v8.5H5zM9 3.75h2.25v8.5H9z" fill="currentColor" stroke="none"/>',
    Restart: '<path d="M3.2 7.75A4.9 4.9 0 1 1 4.65 11.5M3.15 4.45v3.4h3.4"/>',
    StepBack: '<path d="M4.2 3.75v8.5M11.8 3.9L6.4 8l5.4 4.1z"/>',
    StepFwd: '<path d="M11.8 3.75v8.5M4.2 3.9L9.6 8l-5.4 4.1z"/>',
    Download: '<path d="M8 2.5v7.6M5 7.45l3 2.9 3-2.9M2.75 11.85v.9a1.2 1.2 0 0 0 1.2 1.2h8.1a1.2 1.2 0 0 0 1.2-1.2v-.9"/>',
    Upload: '<path d="M8 10.6V2.8M5 5.45l3-2.9 3 2.9M2.75 11.85v.9a1.2 1.2 0 0 0 1.2 1.2h8.1a1.2 1.2 0 0 0 1.2-1.2v-.9"/>',
    Grid: '<path d="M2.25 5.75h11.5M2.25 10.25h11.5M5.75 2.25v11.5M10.25 2.25v11.5"/>',
    Camera: '<rect x="1.8" y="4.3" width="12.4" height="8.7" rx="1.5"/><path d="M5.6 4.3L6.6 2.8h2.8l1 1.5"/><circle cx="8" cy="8.7" r="2.2"/>',
    Sun: '<circle cx="8" cy="8" r="2.8"/><path d="M8 1.6v1.65M8 12.75v1.65M1.6 8h1.65M12.75 8h1.65M3.45 3.45l1.15 1.15M11.4 11.4l1.15 1.15M12.55 3.45L11.4 4.6M4.6 11.4l-1.15 1.15"/>',
    PointLight: '<circle cx="8" cy="8" r="2.4" fill="currentColor" fill-opacity="0.22"/><path d="M8 2v2M8 12v2M2 8h2M12 8h2M3.75 3.75l1.4 1.4M10.85 10.85l1.4 1.4M12.25 3.75l-1.4 1.4M5.15 10.85l-1.4 1.4"/>',
    SpotLight: '<path d="M6.2 2.4h3.6l1.3 3.2H4.9z"/><path d="M5.1 5.6L2.3 13.5M10.9 5.6l2.8 7.9M8 5.6v7.9" stroke-dasharray="1.8 1.5"/>',
    IesLight: '<rect x="6" y="2.2" width="4" height="2" rx="0.5"/><path d="M8 4.2v2.2c0 1.4-4 2.1-4 4.4 0 1.7 1.6 2.8 4 2.8s4-1.1 4-2.8c0-2.3-4-3-4-4.4" fill="currentColor" fill-opacity="0.16"/>',
    AreaLight: '<rect x="2.6" y="3.2" width="10.8" height="6.2" rx="1.1"/><path d="M4.8 11.6l-.8 2M8 11.4v2.3M11.2 11.6l.8 2"/>',
    TubeLight: '<rect x="2" y="6.3" width="12" height="3.4" rx="1.7"/><path d="M4 4.2v1M8 4.2v1M12 4.2v1M4 10.8v1M8 10.8v1M12 10.8v1"/>',
    LedLight: '<rect x="4" y="3" width="8" height="4.8" rx="1"/><circle cx="6.2" cy="5.4" r="0.7" fill="currentColor" stroke="none"/><circle cx="9.8" cy="5.4" r="0.7" fill="currentColor" stroke="none"/><path d="M5 7.8l-1.8 5.4M11 7.8l1.8 5.4"/>',
    LedStrip: '<path d="M2 7.5h12M2 9.8h12"/><circle cx="4" cy="8.65" r="0.65" fill="currentColor" stroke="none"/><circle cx="8" cy="8.65" r="0.65" fill="currentColor" stroke="none"/><circle cx="12" cy="8.65" r="0.65" fill="currentColor" stroke="none"/>',
    EnvDome: '<path d="M2.2 10.5a5.8 5.8 0 0 1 11.6 0z"/><path d="M1.8 10.5h12.4M4.8 13h6.4"/>',
    Moon: '<path d="M10.8 11.6A4.9 4.9 0 0 1 6.2 3a5.2 5.2 0 1 0 7.1 7.1c-.7.75-1.55 1.25-2.5 1.5z"/>',
    Stars: '<path d="M8 2.2l1.1 2.7 2.8.2-2.2 1.8.8 2.8L8 8.2 5.5 9.7l.8-2.8-2.2-1.8 2.8-.2z"/><circle cx="3" cy="12.5" r="0.7" fill="currentColor" stroke="none"/><circle cx="13" cy="11.8" r="0.7" fill="currentColor" stroke="none"/>',
    LensFlare: '<circle cx="8" cy="8" r="2.1"/><path d="M1.5 8h13M8 1.5v13M4.2 4.2l7.6 7.6M11.8 4.2l-7.6 7.6"/>',
    Fog: '<path d="M2.5 5.4h11M3.8 8.2h9.4M2.2 11h10.2"/>',
    Post: '<circle cx="8" cy="8" r="5.2"/><path d="M8 2.8v10.4M8 8l4.4-2.6"/>',
    Photometry: '<circle cx="8" cy="6.2" r="4.5" stroke-dasharray="2 1.6"/><path d="M8 3.2v9.6M4.8 11.5L8 3.2l3.2 8.3"/>',
    Atmosphere: '<path d="M2.5 11.8a5.7 5.7 0 0 1 11 0M4.3 11.8a3.85 3.85 0 0 1 7.4 0M1.75 11.8h12.5"/>',
    Presets: '<rect x="2.2" y="2.2" width="4.7" height="4.7" rx="1"/><rect x="9.1" y="2.2" width="4.7" height="4.7" rx="1"/><rect x="2.2" y="9.1" width="4.7" height="4.7" rx="1"/><rect x="9.1" y="9.1" width="4.7" height="4.7" rx="1"/>',
    Search: '<circle cx="7" cy="7" r="4.3"/><path d="M10.4 10.4l3.3 3.3"/>',
    Log: '<path d="M2.6 4.2h10.8M2.6 8h7.5M2.6 11.8h9.2"/>',
    Palette: '<path d="M8 2.1a5.9 5.9 0 0 0-5.9 5.9 4.2 4.2 0 0 0 4.2 4.2h1.2c.65 0 1.05.45 1.05 1.05 0 .35-.15.65-.15.95 0 .45.4.85 1 .85 2.75 0 4.5-2.65 4.5-6.1A6.9 6.9 0 0 0 8 2.1z"/><circle cx="5.2" cy="7.2" r="0.7" fill="currentColor" stroke="none"/><circle cx="7.4" cy="4.9" r="0.7" fill="currentColor" stroke="none"/><circle cx="10.4" cy="5.7" r="0.7" fill="currentColor" stroke="none"/>'
};

export function CreateSvgGlyph(Name, ClassName = 'st-icon')
{
    const Svg = document.createElementNS('http://www.w3.org/2000/svg', 'svg');
    Svg.setAttribute('class', ClassName);
    Svg.setAttribute('viewBox', ICON_SVG_PROPS.viewBox);
    Svg.setAttribute('fill', ICON_SVG_PROPS.fill);
    Svg.setAttribute('stroke', ICON_SVG_PROPS.stroke);
    Svg.setAttribute('stroke-width', ICON_SVG_PROPS.strokeWidth);
    Svg.setAttribute('stroke-linecap', ICON_SVG_PROPS.strokeLinecap);
    Svg.setAttribute('stroke-linejoin', ICON_SVG_PROPS.strokeLinejoin);
    Svg.setAttribute('aria-hidden', 'true');
    Svg.innerHTML = GLYPH_BODIES[Name] || GLYPH_BODIES.Photometry;
    return Svg;
}

export function HydrateIcons(Root = document)
{
    Root.querySelectorAll('[data-icon]').forEach((Host) => {
        const Name = Host.getAttribute('data-icon');
        if (!Name || !GLYPH_BODIES[Name]) return;
        Host.replaceChildren(CreateSvgGlyph(Name, Host.getAttribute('data-icon-class') || 'st-icon'));
    });
}

export function SnapValueToStep(Value, Min, Max, Step)
{
    const Clamped = Math.min(Max, Math.max(Min, Value));
    if (!Number.isFinite(Step) || Step <= 0) return Clamped;
    const Steps = Math.round((Clamped - Min) / Step);
    const Snapped = Min + Steps * Step;
    return Number(Math.min(Max, Math.max(Min, Snapped)).toFixed(6));
}

export function CreateValuePill({ Value, Min, Max, Step = 0.01, Precision = 2, Suffix = '', OnChange, Disabled = false, Title = '' })
{
    let Current = Number.isFinite(Value) ? Value : Min;
    const Format = (V) => {
        const Rounded = Number(V).toFixed(Precision);
        return Suffix ? `${Rounded} ${Suffix}` : Rounded;
    };

    const El = document.createElement('div');
    El.className = 'value-pill';
    if (Title) El.title = Title;
    if (Disabled) El.classList.add('is-disabled');

    const Readout = document.createElement('span');
    Readout.className = 'value-pill-text';
    Readout.textContent = Format(Current);
    El.appendChild(Readout);

    let Editing = false;
    let Scrubbing = false;
    let DragStartX = 0;
    let DragStartVal = 0;
    let Moved = false;

    const CommitValue = (Next) => {
        Current = SnapValueToStep(Next, Min, Max, Step);
        Readout.textContent = Format(Current);
        OnChange?.(Current);
    };

    const OpenEditor = () => {
        if (Editing || Disabled) return;
        Editing = true;
        El.classList.add('is-editing');
        const Input = document.createElement('input');
        Input.type = 'text';
        Input.className = 'value-pill-input';
        Input.value = Number(Current).toFixed(Precision);
        El.replaceChildren(Input);
        Input.focus();
        Input.select();

        const Finish = (Commit) => {
            if (!Editing) return;
            Editing = false;
            El.classList.remove('is-editing');
            const Parsed = parseFloat(Input.value);
            El.replaceChildren(Readout);
            if (Commit && Number.isFinite(Parsed)) CommitValue(Parsed);
            else Readout.textContent = Format(Current);
        };
        Input.addEventListener('blur', () => Finish(true));
        Input.addEventListener('keydown', (Ev) => {
            if (Ev.key === 'Enter') { Ev.preventDefault(); Finish(true); }
            else if (Ev.key === 'Escape') { Ev.preventDefault(); Finish(false); }
        });
    };

    El.addEventListener('pointerdown', (Ev) => {
        if (Disabled || Editing || Ev.button !== 0) return;
        Scrubbing = true;
        Moved = false;
        DragStartX = Ev.clientX;
        DragStartVal = Current;
        El.classList.add('is-scrubbing');
        El.setPointerCapture(Ev.pointerId);
    });
    El.addEventListener('pointermove', (Ev) => {
        if (!Scrubbing) return;
        const DX = Ev.clientX - DragStartX;
        if (Math.abs(DX) > 2.5) Moved = true;
        if (!Moved) return;
        const Span = Max - Min;
        const Rate = (Span / 180) * (Ev.shiftKey ? 0.15 : 1.0);
        CommitValue(DragStartVal + DX * Rate);
    });
    const EndScrub = (Ev) => {
        if (!Scrubbing) return;
        Scrubbing = false;
        El.classList.remove('is-scrubbing');
        try { El.releasePointerCapture(Ev.pointerId); } catch {}
        if (!Moved) OpenEditor();
    };
    El.addEventListener('pointerup', EndScrub);
    El.addEventListener('pointercancel', EndScrub);

    return {
        Element: El,
        SetValue: (Next) => {
            if (!Number.isFinite(Next)) return;
            Current = SnapValueToStep(Next, Min, Max, Step);
            if (!Editing) Readout.textContent = Format(Current);
        },
        SetBounds: (NextMin, NextMax) => { Min = NextMin; Max = NextMax; }
    };
}

export function CreateSliderPill({ Value, Min, Max, Step = 0.01, Precision = 2, Suffix = '', OnChange, Disabled = false })
{
    let Current = Number.isFinite(Value) ? Value : Min;
    const Row = document.createElement('div');
    Row.className = 'slider-row';

    const Track = document.createElement('div');
    Track.className = 'slider-pill';
    Track.setAttribute('role', 'slider');
    Track.setAttribute('tabindex', Disabled ? '-1' : '0');
    Track.setAttribute('aria-valuemin', String(Min));
    Track.setAttribute('aria-valuemax', String(Max));
    Track.setAttribute('aria-valuenow', String(Current));

    const Fill = document.createElement('div');
    Fill.className = 'slider-pill-fill';
    const Thumb = document.createElement('div');
    Thumb.className = 'slider-pill-thumb';
    Track.append(Fill, Thumb);

    const ValuePill = CreateValuePill({
        Value: Current, Min, Max, Step, Precision, Suffix, Disabled,
        OnChange: (Next) => {
            Current = Next;
            SyncVisual();
            OnChange?.(Current);
        }
    });

    const SyncVisual = () => {
        const Pct = Max > Min ? ((Current - Min) / (Max - Min)) * 100 : 0;
        const ClampedPct = Math.max(0, Math.min(100, Pct));
        Fill.style.width = `${ClampedPct}%`;
        Thumb.style.left = `calc(${ClampedPct}% - 4px)`;
        Track.setAttribute('aria-valuenow', String(Current));
    };

    const SetFromPointer = (ClientX) => {
        const Rect = Track.getBoundingClientRect();
        const Ratio = Math.max(0, Math.min(1, (ClientX - Rect.left) / Math.max(1, Rect.width)));
        Current = SnapValueToStep(Min + Ratio * (Max - Min), Min, Max, Step);
        SyncVisual();
        ValuePill.SetValue(Current);
        OnChange?.(Current);
    };

    let Active = false;
    Track.addEventListener('pointerdown', (Ev) => {
        if (Disabled || Ev.button !== 0) return;
        Active = true;
        Track.classList.add('is-dragging');
        Track.setPointerCapture(Ev.pointerId);
        SetFromPointer(Ev.clientX);
    });
    Track.addEventListener('pointermove', (Ev) => { if (Active) SetFromPointer(Ev.clientX); });
    const EndDrag = (Ev) => {
        if (!Active) return;
        Active = false;
        Track.classList.remove('is-dragging');
        try { Track.releasePointerCapture(Ev.pointerId); } catch {}
    };
    Track.addEventListener('pointerup', EndDrag);
    Track.addEventListener('pointercancel', EndDrag);
    Track.addEventListener('keydown', (Ev) => {
        const Delta = (Ev.shiftKey ? 10 : 1) * (Step || (Max - Min) / 100);
        if (Ev.key === 'ArrowRight' || Ev.key === 'ArrowUp') {
            Ev.preventDefault();
            Current = SnapValueToStep(Current + Delta, Min, Max, Step);
            SyncVisual(); ValuePill.SetValue(Current); OnChange?.(Current);
        } else if (Ev.key === 'ArrowLeft' || Ev.key === 'ArrowDown') {
            Ev.preventDefault();
            Current = SnapValueToStep(Current - Delta, Min, Max, Step);
            SyncVisual(); ValuePill.SetValue(Current); OnChange?.(Current);
        }
    });

    SyncVisual();
    Row.append(Track, ValuePill.Element);
    return {
        Element: Row,
        SetValue: (Next) => {
            if (!Number.isFinite(Next)) return;
            Current = SnapValueToStep(Next, Min, Max, Step);
            SyncVisual();
            ValuePill.SetValue(Current);
        }
    };
}

export function CreateTogglePill({ Checked = false, OnChange, AriaLabel = 'Toggle' })
{
    let State = Boolean(Checked);
    const Btn = document.createElement('button');
    Btn.type = 'button';
    Btn.className = 'toggle-pill';
    Btn.setAttribute('role', 'switch');
    Btn.setAttribute('aria-label', AriaLabel);
    const Sync = () => {
        Btn.classList.toggle('is-checked', State);
        Btn.setAttribute('aria-checked', State ? 'true' : 'false');
    };
    Btn.addEventListener('click', () => {
        State = !State;
        Sync();
        OnChange?.(State);
    });
    Sync();
    return { Element: Btn, SetChecked: (V) => { State = Boolean(V); Sync(); } };
}

export function CreateSelectPill({ Value, Options, OnChange })
{
    const Wrap = document.createElement('div');
    Wrap.className = 'select-pill-wrap';
    const Sel = document.createElement('select');
    Sel.className = 'select-pill';
    for (const Opt of Options) {
        const O = document.createElement('option');
        O.value = typeof Opt === 'string' ? Opt : Opt.Value;
        O.textContent = typeof Opt === 'string' ? Opt : Opt.Label;
        Sel.appendChild(O);
    }
    Sel.value = Value;
    Sel.addEventListener('change', () => OnChange?.(Sel.value));
    const Caret = CreateSvgGlyph('Chevron', 'select-caret');
    Wrap.append(Sel, Caret);
    return { Element: Wrap, SetValue: (V) => { Sel.value = V; } };
}

export function CreatePropertyRow(LabelText, ControlNode, { Compact = false, Hint = '' } = {})
{
    const Row = document.createElement('div');
    Row.className = Compact ? 'prop-row prop-row-compact' : 'prop-row';
    const Label = document.createElement('label');
    Label.className = 'prop-label';
    Label.textContent = LabelText;
    if (Hint) Label.title = Hint;
    const Slot = document.createElement('div');
    Slot.className = 'prop-control';
    Slot.appendChild(ControlNode);
    Row.append(Label, Slot);
    return Row;
}

export function CreateCollapsibleSection({ Title, Badge = '', Open = true, AccentClass = '' })
{
    const Section = document.createElement('section');
    Section.className = `inspector-section ${AccentClass}`.trim();
    if (!Open) Section.classList.add('is-collapsed');

    const Header = document.createElement('button');
    Header.type = 'button';
    Header.className = 'section-header';
    Header.setAttribute('aria-expanded', Open ? 'true' : 'false');

    const TitleWrap = document.createElement('span');
    TitleWrap.className = 'section-title';
    const Chevron = CreateSvgGlyph('Chevron', 'section-chevron');
    const Label = document.createElement('span');
    Label.textContent = Title;
    TitleWrap.append(Chevron, Label);
    Header.appendChild(TitleWrap);

    if (Badge) {
        const BadgeEl = document.createElement('span');
        BadgeEl.className = 'section-badge';
        BadgeEl.textContent = Badge;
        Header.appendChild(BadgeEl);
    }

    const Body = document.createElement('div');
    Body.className = 'section-body';

    Header.addEventListener('click', () => {
        const IsOpen = Section.classList.toggle('is-collapsed');
        Header.setAttribute('aria-expanded', !IsOpen ? 'true' : 'false');
    });

    Section.append(Header, Body);
    return { Section, Body, Header };
}
