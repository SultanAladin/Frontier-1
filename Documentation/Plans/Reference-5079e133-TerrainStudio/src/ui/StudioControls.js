/*==========================================================================================================================================
                                                          STUDIOCONTROLS.JS
==========================================================================================================================================*/
// Experimental Remastered unified control kit for Frontier Terrain & Rock Studio:
// Glyphs, reactive scopes, SectionCard, Fluid ControlPanel::SliderPill (92px #0c0c0c scrubbable numeric value-pill +
// 26px pill-range track with 24px spherical thumb), 44x25 high-contrast Toggle Switch, SegmentedRow, and progressive DOM
// enhancers for Terrain Studio (.tp-amount / .tp-switch / .tp-check-row) and Rock Formation Studio (SLIDER_SCHEMA).

const SvgNamespace = 'http://www.w3.org/2000/svg';
const Refreshers = new Set();
const ScopeStack = [];

export const GlyphPaths = {
  Monitor:   '<rect x="2.5" y="4" width="19" height="13" rx="2.5"/><path d="M8 21h8M12 17v4"/>',
  Zap:       '<path d="M13 2.5 4.5 14h6.5l-1 7.5L18.5 10H12z"/>',
  Sparkles:  '<path d="M11 3.5l1.7 4.6 4.6 1.7-4.6 1.7L11 16.1l-1.7-4.6-4.6-1.7 4.6-1.7z"/><path d="M18.5 14.5v5M16 17h5"/>',
  Sun:       '<circle cx="12" cy="12" r="4"/><path d="M12 2.5v2M12 19.5v2M4.6 4.6l1.4 1.4M18 18l1.4 1.4M2.5 12h2M19.5 12h2M4.6 19.4 6 18M18 6l1.4-1.4"/>',
  Layers:    '<path d="M12 3 3 8l9 5 9-5z"/><path d="M3 12.5l9 5 9-5"/><path d="M3 17l9 5 9-5"/>',
  Sliders:   '<path d="M4 6h9M17 6h3M4 12h3M11 12h9M4 18h11M19 18h1"/><circle cx="15" cy="6" r="2"/><circle cx="9" cy="12" r="2"/><circle cx="17" cy="18" r="2"/>',
  Wind:      '<path d="M3 8.5h11a3 3 0 1 0-3-3M3 12.5h15.5a3 3 0 1 1-3 3M3 16.5h7"/>',
  Drop:      '<path d="M12 3s6.5 7 6.5 11.5a6.5 6.5 0 0 1-13 0C5.5 10 12 3 12 3z"/>',
  Play:      '<path d="M8 5.5v13l10.5-6.5z"/>',
  Pause:     '<path d="M8.5 5v14M15.5 5v14"/>',
  Step:      '<path d="M7 6v12l8-6z"/><path d="M17.5 6v12"/>',
  Reset:     '<path d="M3.5 12a8.5 8.5 0 1 0 2.6-6.1"/><path d="M3.5 4.5v4.6h4.6"/>',
  Plus:      '<path d="M12 5v14M5 12h14"/>',
  Check:     '<path d="M5 12.5l4.2 4.2L19 7"/>',
  Chevron:   '<path d="M6 9l6 6 6-6"/>',
  Eye:       '<path d="M2 12s3.6-7 10-7 10 7 10 7-3.6 7-10 7S2 12 2 12z"/><circle cx="12" cy="12" r="3"/>',
  EyeOff:    '<path d="M3 3l18 18"/><path d="M10.6 5.1A10.4 10.4 0 0 1 12 5c6.4 0 10 7 10 7a17.6 17.6 0 0 1-2.6 3.5M6.2 6.3C3.6 8.1 2 12 2 12s3.6 7 10 7c1.7 0 3.2-.5 4.5-1.2"/>',
  Compass:   '<circle cx="12" cy="12" r="9"/><path d="M15.5 8.5l-2 5-5 2 2-5z"/>',
  Fracture:  '<rect x="3" y="3" width="18" height="18" rx="3"/><path d="M3 9.5 11.5 12 8.5 21M11.5 12 21 7.5M11.5 12l5.5 9"/>',
  Dot:       '<circle cx="12" cy="12" r="3"/>',
};

function hasRealDom() {
  return typeof document !== 'undefined' && typeof document.createElement === 'function';
}

export function ConstructGlyph(Name, ClassName = 'glyph') {
  if (!hasRealDom() || typeof document.createElementNS !== 'function') return null;
  const Svg = document.createElementNS(SvgNamespace, 'svg');
  Svg.setAttribute('viewBox', '0 0 24 24');
  Svg.setAttribute('class', ClassName);
  Svg.innerHTML = GlyphPaths[Name] || GlyphPaths.Dot;
  return Svg;
}

export function Construct(Tag, ClassName, Content) {
  const Element = document.createElement(Tag);
  if (ClassName) Element.className = ClassName;
  if (Content === undefined || Content === null) return Element;
  if (typeof Content === 'string' || typeof Content === 'number') Element.textContent = String(Content);
  else if (Array.isArray(Content)) Content.forEach((Part) => { if (Part !== null && Part !== undefined) Element.append(Part); });
  else Element.append(Content);
  return Element;
}

export function OpenScope() { const Scope = new Set(); ScopeStack.push(Scope); return Scope; }
export function CloseScope(Scope) {
  const Top = ScopeStack.pop();
  if (Top !== Scope && Scope) ScopeStack.splice(ScopeStack.indexOf(Scope), 1);
}
export function ReleaseScope(Scope) {
  if (!Scope) return;
  for (const R of Scope) Refreshers.delete(R);
  Scope.clear();
}
export function Register(Refresher) {
  Refreshers.add(Refresher);
  if (ScopeStack.length) ScopeStack[ScopeStack.length - 1].add(Refresher);
}
export function Broadcast() {
  for (const Refresher of Refreshers) Refresher();
}

function dispatchInputEvent(target, type) {
  if (typeof Event === 'function') {
    target.dispatchEvent(new Event(type, { bubbles: true }));
  }
}

/**
 * Infer display multiplier, precision digits, and compact unit cell text from
 * a slider's [min, max, step] range and formatted output sample.
 */
export function resolveSliderDisplaySpec(min, max, step, sampleText = '') {
  const text = String(sampleText || '').trim();
  const unitMatch = text.match(/^[-+]?\d*(?:[.,]\d+)?\s*(.*)$/);
  let rawUnit = unitMatch ? unitMatch[1].trim() : '';

  if (/^frames?$/i.test(rawUnit)) rawUnit = 'frm';
  else if (/^days?$/i.test(rawUnit)) rawUnit = 'd';
  else if (/^years?$/i.test(rawUnit)) rawUnit = 'yr';
  else if (/^tris?$/i.test(rawUnit)) rawUnit = 'tri';
  else if (/^pts?$/i.test(rawUnit)) rawUnit = 'pt';
  else if (rawUnit.length > 4) rawUnit = rawUnit.slice(0, 4);

  let multiplier = 1;
  if (rawUnit === '%' && max <= 4.0001 && min >= -4.0001) {
    multiplier = 100;
  } else if (rawUnit === 'k' && max >= 1000) {
    multiplier = 0.001;
  }

  const displayStep = Math.max(1e-6, (Number(step) || 0.01) * multiplier);
  const digits = displayStep >= 1 - 1e-6
    ? 0
    : displayStep >= 0.1 - 1e-6
      ? 1
      : displayStep >= 0.01 - 1e-6
        ? 2
        : 3;

  return {
    unit: rawUnit || '—',
    multiplier,
    displayStep: Number(displayStep.toPrecision(6)),
    digits,
  };
}

function attachPillScrubbing(numberInput, getRaw, getBounds, commitRaw) {
  let scrub = null;
  numberInput.addEventListener('pointerdown', (event) => {
    if (document.activeElement === numberInput || numberInput.disabled) return;
    scrub = {
      x: event.clientX,
      moved: false,
      start: Number(getRaw()) || 0,
    };
    if (typeof numberInput.setPointerCapture === 'function') {
      try { numberInput.setPointerCapture(event.pointerId); } catch (_) { /* ignore synthetic events */ }
    }
  });

  numberInput.addEventListener('pointermove', (event) => {
    if (!scrub) return;
    const dx = event.clientX - scrub.x;
    if (!scrub.moved && Math.abs(dx) < 3) return;
    if (!scrub.moved) {
      scrub.moved = true;
      numberInput.classList.add('scrubbing');
    }
    const { min, max, step } = getBounds();
    const span = (max - min) || 1;
    const factor = event.shiftKey ? 0.15 : event.altKey ? 3.5 : 1.0;
    const rawNext = scrub.start + (dx / 170) * span * factor;
    const snapped = Math.min(max, Math.max(min, Math.round(rawNext / step) * step));
    commitRaw(snapped, false);
  });

  const endScrub = (event) => {
    if (!scrub) return;
    const moved = scrub.moved;
    scrub = null;
    numberInput.classList.remove('scrubbing');
    if (moved) {
      event.preventDefault();
      commitRaw(Number(getRaw()), true);
    }
  };
  numberInput.addEventListener('pointerup', endScrub);
  numberInput.addEventListener('pointercancel', endScrub);
}

// ControlPanel::SliderPill standalone builder (Fluid Editor / Experimental Remastered)
export function SliderRow(Parent, Binding) {
  if (!hasRealDom()) return null;
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

  const SyncFraction = (Value) => {
    const Span = (Binding.Maximum - Binding.Minimum) || 1;
    const Frac = Math.max(0, Math.min(1, (Value - Binding.Minimum) / Span));
    Range.style.setProperty('--fraction', Frac.toFixed(4));
  };

  const Commit = (Raw) => {
    if (!Number.isFinite(Raw)) return;
    const Clamped = Math.min(Binding.Maximum, Math.max(Binding.Minimum, Raw));
    Binding.Write(Clamped);
    SyncFraction(Clamped);
    Broadcast();
  };

  Range.addEventListener('input', () => Commit(Number(Range.value)));
  NumberInput.addEventListener('change', () => Commit(Number(NumberInput.value)));
  attachPillScrubbing(
    NumberInput,
    () => Binding.Read(),
    () => ({ min: Binding.Minimum, max: Binding.Maximum, step: Step }),
    (val) => Commit(val),
  );

  Register(() => {
    const Value = Number(Binding.Read());
    Range.value = String(Value);
    if (document.activeElement !== NumberInput) {
      NumberInput.value = Number.isFinite(Value) ? Value.toFixed(Digits) : '0';
    }
    SyncFraction(Number.isFinite(Value) ? Value : Binding.Minimum);
  });
  return Row;
}

// Fluid 44x25 high-contrast pill toggle switch standalone builder
export function SwitchRow(Parent, Binding) {
  if (!hasRealDom()) return null;
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
  Register(() => { Input.checked = Boolean(Binding.Read()); });
  return Row;
}

/**
 * Progressive enhancer: upgrades Terrain Studio `.tp-amount` slider containers
 * into Experimental Remastered `ControlPanel::SliderPill` rows (`[value-pill | pill-range]`)
 * while preserving the underlying `<input type="range">` and `<output>` elements and
 * all `data-*` attributes / event listeners.
 */
export function enhanceAmountSliders(root) {
  if (!hasRealDom() || !root || typeof root.querySelectorAll !== 'function') return;
  const amounts = root.querySelectorAll('.tp-amount');
  amounts.forEach((amountEl) => {
    if (amountEl.dataset.remasteredEnhanced === '1') return;
    const range = amountEl.querySelector('input[type="range"]');
    if (!range) return;
    const output = amountEl.querySelector('output');

    amountEl.dataset.remasteredEnhanced = '1';
    amountEl.classList.add('slider-control', 'tp-remastered-slider');
    range.classList.add('pill-range');

    const getBounds = () => ({
      min: Number(range.min) || 0,
      max: Number.isFinite(Number(range.max)) ? Number(range.max) : 1,
      step: Number(range.step) || 0.01,
    });

    const initialBounds = getBounds();
    const spec = resolveSliderDisplaySpec(
      initialBounds.min,
      initialBounds.max,
      initialBounds.step,
      output ? output.textContent : '',
    );

    const numInput = Construct('input', 'pill-number');
    numInput.type = 'number';
    numInput.min = String(Number((initialBounds.min * spec.multiplier).toPrecision(8)));
    numInput.max = String(Number((initialBounds.max * spec.multiplier).toPrecision(8)));
    numInput.step = String(spec.displayStep);
    numInput.disabled = Boolean(range.disabled);

    const labelText = amountEl.closest('.tp-control-row')
      ?.querySelector('.tp-control-label')
      ?.textContent?.trim();
    if (labelText) {
      numInput.setAttribute('aria-label', labelText);
      if (!range.getAttribute('aria-label')) range.setAttribute('aria-label', labelText);
    }

    const unitCell = Construct('span', 'unit-cell', spec.unit);
    const valuePill = Construct('div', 'value-pill', [numInput, unitCell]);

    if (output) output.classList.add('tp-sr-output');
    amountEl.insertBefore(valuePill, range);

    const syncFromRange = () => {
      const { min, max } = getBounds();
      const raw = Number(range.value);
      const safeRaw = Number.isFinite(raw) ? raw : min;
      const frac = Math.max(0, Math.min(1, (safeRaw - min) / Math.max(1e-6, max - min)));
      range.style.setProperty('--fraction', frac.toFixed(4));
      amountEl.style.setProperty('--fill', `${(frac * 100).toFixed(1)}%`);
      if (document.activeElement !== numInput) {
        numInput.value = (safeRaw * spec.multiplier).toFixed(spec.digits);
      }
      numInput.disabled = Boolean(range.disabled);
    };

    const commitRawToRange = (rawValue, fireChange) => {
      const { min, max } = getBounds();
      if (!Number.isFinite(rawValue)) return;
      const clamped = Math.min(max, Math.max(min, rawValue));
      const precision = 1e6;
      range.value = String(Math.round(clamped * precision) / precision);
      dispatchInputEvent(range, 'input');
      if (fireChange) dispatchInputEvent(range, 'change');
      syncFromRange();
    };

    range.addEventListener('input', syncFromRange);
    range.addEventListener('change', syncFromRange);

    numInput.addEventListener('change', () => {
      const entered = Number(numInput.value);
      if (!Number.isFinite(entered)) {
        syncFromRange();
        return;
      }
      commitRawToRange(entered / spec.multiplier, true);
    });
    numInput.addEventListener('keydown', (event) => {
      if (event.key === 'Enter') {
        event.preventDefault();
        numInput.blur();
      }
    });

    attachPillScrubbing(numInput, () => Number(range.value), getBounds, commitRawToRange);
    syncFromRange();
  });
}

/**
 * Progressive enhancer: upgrades `.tp-check-row` and `.tp-switch` checkboxes into
 * Experimental Remastered 44x25 high-contrast pill switches while preserving native
 * `<input type="checkbox">` IDs, labels, and change listeners.
 */
export function enhanceSwitches(root) {
  if (!hasRealDom() || !root || typeof root.querySelectorAll !== 'function') return;

  root.querySelectorAll('.tp-check-row').forEach((row) => {
    if (row.dataset.remasteredSwitch === '1') return;
    const checkbox = row.querySelector('input[type="checkbox"]');
    if (!checkbox) return;
    row.dataset.remasteredSwitch = '1';
    row.classList.add('toggle-row', 'tp-remastered-toggle-row');

    const switchWrap = Construct('span', 'switch');
    const track = Construct('span', 'switch-track');
    checkbox.parentNode.insertBefore(switchWrap, checkbox);
    switchWrap.append(checkbox, track);

    // Move switch pill to the right side of the row like Experimental Remastered ToggleRow
    row.append(switchWrap);
  });

  root.querySelectorAll('.tp-switch').forEach((sw) => {
    if (sw.dataset.remasteredSwitch === '1') return;
    const checkbox = sw.querySelector('input[type="checkbox"]');
    if (!checkbox) return;
    sw.dataset.remasteredSwitch = '1';
    sw.classList.add('switch', 'tp-remastered-inline-switch');
    const existingSpan = sw.querySelector('span');
    if (existingSpan) {
      existingSpan.classList.add('switch-track', 'tp-sr-switch-text');
    } else {
      sw.append(Construct('span', 'switch-track'));
    }
  });
}

/**
 * Builds Rock Formation Studio slider groups using Experimental Remastered
 * `ControlPanel::SliderPill` (`[92px #0c0c0c value-pill | 26px pill-range]`)
 * while populating `sliderRefs` with `{ input, out, numInput, sync, spec }`.
 */
export function buildRemasteredRockSliders(mount, specs, stateParams, onValueCommit, sliderRefs) {
  if (!hasRealDom() || !mount) return;
  mount.innerHTML = '';
  for (const spec of specs) {
    if (spec.type === 'heading') {
      const heading = Construct('div', 'slider-subhead section-subhead', spec.label);
      mount.appendChild(heading);
      continue;
    }

    const val = stateParams[spec.key];
    const display = resolveSliderDisplaySpec(spec.min, spec.max, spec.step, spec.fmt(val));

    const wrap = Construct('div', 'slider-group property-row slider-row');
    const labelRow = Construct('div', 'slider-label');
    const titleSpan = Construct('span', 'property-label', spec.label);
    const out = Construct('output', 'tp-sr-output', spec.fmt(val));
    labelRow.append(titleSpan, out);

    const numInput = Construct('input', 'pill-number');
    numInput.type = 'number';
    numInput.min = String(Number((spec.min * display.multiplier).toPrecision(8)));
    numInput.max = String(Number((spec.max * display.multiplier).toPrecision(8)));
    numInput.step = String(display.displayStep);
    numInput.setAttribute('aria-label', spec.label);

    const unitCell = Construct('span', 'unit-cell', display.unit);
    const valuePill = Construct('div', 'value-pill', [numInput, unitCell]);

    const input = Construct('input', 'pill-range');
    input.type = 'range';
    input.min = String(spec.min);
    input.max = String(spec.max);
    input.step = String(spec.step);
    input.value = String(val);
    input.setAttribute('aria-label', spec.label);

    const control = Construct('div', 'slider-control', [valuePill, input]);
    wrap.append(labelRow, control);
    mount.appendChild(wrap);

    const syncVisuals = (rawVal) => {
      const safe = Number.isFinite(rawVal) ? rawVal : spec.min;
      const frac = Math.max(0, Math.min(1, (safe - spec.min) / Math.max(1e-6, spec.max - spec.min)));
      input.value = String(safe);
      input.style.setProperty('--fraction', frac.toFixed(4));
      out.textContent = spec.fmt(safe);
      if (document.activeElement !== numInput) {
        numInput.value = (safe * display.multiplier).toFixed(display.digits);
      }
    };

    const commitRaw = (rawVal) => {
      if (!Number.isFinite(rawVal)) return;
      const clamped = Math.min(spec.max, Math.max(spec.min, rawVal));
      const precision = 1e6;
      const clean = Math.round(clamped * precision) / precision;
      syncVisuals(clean);
      onValueCommit(spec.key, clean);
    };

    input.addEventListener('input', () => commitRaw(Number(input.value)));
    numInput.addEventListener('change', () => {
      const entered = Number(numInput.value);
      if (!Number.isFinite(entered)) {
        syncVisuals(Number(stateParams[spec.key]));
        return;
      }
      commitRaw(entered / display.multiplier);
    });
    numInput.addEventListener('keydown', (event) => {
      if (event.key === 'Enter') {
        event.preventDefault();
        numInput.blur();
      }
    });

    attachPillScrubbing(
      numInput,
      () => Number(stateParams[spec.key]),
      () => ({ min: spec.min, max: spec.max, step: spec.step }),
      (nextVal) => commitRaw(nextVal),
    );

    syncVisuals(val);
    sliderRefs.set(spec.key, { input, out, numInput, sync: syncVisuals, spec });
  }
}

/**
 * Progressive enhancer: upgrades static `.slider-group` elements (such as Rock Studio's
 * curve-radius and sun-rig sliders) into Experimental Remastered `ControlPanel::SliderPill`
 * rows while preserving native IDs, `<output>` elements, and event listeners.
 */
export function enhanceStaticSliderGroups(root) {
  if (!hasRealDom() || !root || typeof root.querySelectorAll !== 'function') return;
  root.querySelectorAll('.slider-group').forEach((groupEl) => {
    if (groupEl.dataset.remasteredStatic === '1' || groupEl.querySelector('.slider-control')) return;
    const range = groupEl.querySelector('input[type="range"]');
    if (!range) return;
    const out = groupEl.querySelector('output');
    const labelSpan = groupEl.querySelector('.slider-label span');

    groupEl.dataset.remasteredStatic = '1';
    groupEl.classList.add('property-row', 'slider-row');
    if (labelSpan) labelSpan.classList.add('property-label');
    if (out) out.classList.add('tp-sr-output');
    range.classList.add('pill-range');

    const getBounds = () => ({
      min: Number(range.min) || 0,
      max: Number.isFinite(Number(range.max)) ? Number(range.max) : 1,
      step: Number(range.step) || 0.01,
    });
    const initBounds = getBounds();
    const display = resolveSliderDisplaySpec(
      initBounds.min,
      initBounds.max,
      initBounds.step,
      out ? out.textContent : '',
    );

    const numInput = Construct('input', 'pill-number');
    numInput.type = 'number';
    numInput.min = String(Number((initBounds.min * display.multiplier).toPrecision(8)));
    numInput.max = String(Number((initBounds.max * display.multiplier).toPrecision(8)));
    numInput.step = String(display.displayStep);
    if (labelSpan?.textContent) numInput.setAttribute('aria-label', labelSpan.textContent.trim());

    const unitCell = Construct('span', 'unit-cell', display.unit);
    const valuePill = Construct('div', 'value-pill', [numInput, unitCell]);
    const control = Construct('div', 'slider-control');

    range.parentNode.insertBefore(control, range);
    control.append(valuePill, range);

    const syncFromRange = () => {
      const { min, max } = getBounds();
      const raw = Number(range.value);
      const safe = Number.isFinite(raw) ? raw : min;
      const frac = Math.max(0, Math.min(1, (safe - min) / Math.max(1e-6, max - min)));
      range.style.setProperty('--fraction', frac.toFixed(4));
      if (document.activeElement !== numInput) {
        numInput.value = (safe * display.multiplier).toFixed(display.digits);
      }
    };

    const commitRaw = (rawValue, fireChange) => {
      const { min, max } = getBounds();
      if (!Number.isFinite(rawValue)) return;
      const clamped = Math.min(max, Math.max(min, rawValue));
      range.value = String(Math.round(clamped * 1e6) / 1e6);
      dispatchInputEvent(range, 'input');
      if (fireChange) dispatchInputEvent(range, 'change');
      syncFromRange();
    };

    range.addEventListener('input', syncFromRange);
    range.addEventListener('change', syncFromRange);
    numInput.addEventListener('change', () => {
      const entered = Number(numInput.value);
      if (!Number.isFinite(entered)) { syncFromRange(); return; }
      commitRaw(entered / display.multiplier, true);
    });
    attachPillScrubbing(numInput, () => Number(range.value), getBounds, commitRaw);
    range._remasteredSync = syncFromRange;
    syncFromRange();
  });
}

