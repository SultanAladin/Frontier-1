/*============================================================================================================================================
                                                        INSPECTOR
============================================================================================================================================*/
// 🧩 Source inspector: identity, generator choice, params, finite envelope, attenuation, duration, motion, compiled-formula workspace and the live
// readouts that prove the source is bounded and doing what its equation says. Rebuilt only when the authored document or selection changes,
// so transport ticks never interrupt typing or dragging a slider.

import { El, Icon, IconEl, IconButton, Section, Panel, RangeField, SelectField, Segmented, Toggle, VectorField, TextField, Rows, Metrics, Chip, Toast, FormatNumber } from './Kit.js';
import { LawDiagram } from './LawDiagrams.js';
import { FORCE_LAWS, LawById, ForceDomains, EnvelopeKinds, AttenuationKinds, DurationKinds, MotionKinds, LoopPolicies, DomainColor, ParamSchema } from '../Engine/ForceFieldCatalog.js';
import { CompileFieldProgram, FORMULA_PRESETS } from '../Engine/VectorFieldLang.js';
import { EvaluateAt } from '../Engine/FieldEvaluator.js';
import * as M from '../Engine/FieldMath.js';

const Decimation = (step) => (String(step).includes('.') ? String(step).split('.')[1].length : 0);
const Pretty = (key) => key.replace(/([A-Z])/g, ' $1').replace(/^./, (c) => c.toUpperCase());

export function CreateInspector(state) {
  const root = El('section', { class: 'dock right', 'aria-label': 'Force source inspector' });
  const tabStrip = El('div', { class: 'tab-strip' }, El('button', { class: 'document-tab active', type: 'button' }, IconEl('sliders', 13), 'Inspector'));
  const scroll = El('div', { class: 'inspector-scroll' });
  root.append(tabStrip, scroll);

  let diagram = null;
  let liveReadout = null;
  let formulaStatus = null;
  let formulaArea = null;

  function rebuild() {
    scroll.innerHTML = '';
    const source = state.SelectedSource();
    if (!source) {
      scroll.append(
        El('div', { class: 'crumbs' }, 'NO SELECTION'),
        El('h1', { style: { fontSize: '22px' } }, 'Field inspector'),
        El('p', { class: 'muted', style: { fontSize: '11px', lineHeight: '1.7', color: '#7d7d7d', margin: '10px 0 16px' } },
          'Select a force source in the outliner, or add a generator to begin. Every source is bounded by a finite envelope, so the field never leaks past an authored reach.'),
        Panel(
          El('p', {}, 'The default scene layers six generators over a 24 × 24 m ground plane: a swirling intake column, a prevailing wind quadrant, a gravity well, a pressure pulse, an eddy bed and a cross draft.'),
          El('button', { type: 'button', class: 'primary wide', onclick: () => state.Reset() }, IconEl('reset', 14), 'Reset the studio scene'),
        ),
      );
      return;
    }

    const law = LawById[source.Law];
    const domain = ForceDomains[law.Domain];
    scroll.append(El('div', { class: 'crumbs' }, `FORCES / ${domain.Label.toUpperCase()} / ${source.Law.toUpperCase()}`));

    // ---------------------------------------------------------------- identity
    const nameInput = El('input', { class: 'name-input', value: source.Name, 'aria-label': 'Source name' });
    nameInput.addEventListener('change', () => state.UpdateSource(source.Id, { Name: nameInput.value.trim() || law.Name }));
    const detailHead = El('div', { class: 'detail-head' },
      El('span', { class: 'detail-glyph', html: law.Glyph }),
      El('div', { class: 'detail-title' }, nameInput, El('div', { class: 'subtitle' }, `${law.Name} · ${law.Channel} channel · ${law.Unit}`)),
      El('div', { class: 'detail-actions' },
        IconButton('copy', 'Duplicate this source', () => {
          state.DuplicateSource(source.Id);
          Toast('Source duplicated');
        }),
        IconButton('trash', 'Delete this source', () => {
          state.RemoveSource(source.Id);
          Toast('Source removed');
        }),
      ),
    );
    scroll.append(detailHead);

    const chips = El('div', { class: 'chips' },
      Chip(law.Domain, 'ok'),
      Chip(law.Channel === 'velocity' ? 'advection' : law.Channel === 'acceleration' ? 'direct force' : 'damping'),
      Chip(`${source.Envelope.toLowerCase()} bound`),
      source.Muted ? Chip('muted', 'warn') : Chip(source.Enabled ? 'visible' : 'hidden', source.Enabled ? '' : 'dim'),
    );
    scroll.append(chips);

    // ---------------------------------------------------------------- generator choice
    scroll.append(Section('Generator'));
    const lawOptions = FORCE_LAWS.map((l) => ({ Id: l.Id, Label: `${l.Ordinal + 1}. ${l.Name} — ${ForceDomains[l.Domain].Label}` }));
    scroll.append(Panel(
      SelectField({ Label: 'Law', Options: lawOptions, Value: source.Law, OnChange: (value) => changeLaw(source, value) }),
      El('p', {}, law.Summary),
      El('div', { class: 'eq' }, law.Equation),
      (diagram = LawDiagram(source, law)).Node,
      Rows([
        ['Channel', law.Channel === 'velocity' ? 'advection velocity' : law.Channel === 'acceleration' ? 'direct acceleration' : 'velocity damping'],
        ['Unit', law.Unit],
        ['Finite bound', `${source.Envelope} · ${source.Envelope === 'Box' ? source.HalfExtents.map((v) => v.toFixed(1)).join(' × ') : source.Envelope === 'Column' ? `r ${source.Radius.toFixed(1)} × h ${source.Height.toFixed(1)}` : source.Envelope === 'Infinite' ? 'unbounded (explicit)' : `r ${source.Radius.toFixed(1)}`} m`],
        ['Attenuation', source.Attenuation],
        ['Duration', source.Duration],
      ]),
    ));

    // ---------------------------------------------------------------- law parameters
    scroll.append(Section('Governing parameters'));
    const paramFields = ParamSchema(source.Law).map((p) => {
      const value = Number(source.Params[p.Key] ?? p.Default);
      const step = p.Step || (p.Max - p.Min) / 100;
      return RangeField({
        Label: `${p.Label}${p.Unit ? ` [${p.Unit}]` : ''}`,
        Value: value,
        Min: p.Min,
        Max: p.Max,
        Step: step,
        Hint: p.Hint,
        Format: (v) => `${v.toFixed(Decimation(step))}${p.Unit ? ` ${p.Unit}` : ''}`,
        OnInput: (v) => {
          state.UpdateParam(source.Id, p.Key, v);
        },
      });
    });
    scroll.append(Panel(...paramFields));

    // ---------------------------------------------------------------- placement and direction
    scroll.append(Section('Placement'));
    scroll.append(Panel(
      VectorField({ Label: 'Anchor', Value: source.Position, Unit: 'm', Step: 0.25, OnCommit: (v) => state.UpdateSource(source.Id, { Position: v }) }),
      VectorField({ Label: 'Direction', Value: source.Direction, Unit: 'unit vector', Step: 0.05, OnCommit: (v) => state.UpdateSource(source.Id, { Direction: M.Normalize(v, [0, 0, 1]) }) }),
      Row3(
        MiniAction('Aim +X', () => state.UpdateSource(source.Id, { Direction: [1, 0, 0] })),
        MiniAction('Aim +Y', () => state.UpdateSource(source.Id, { Direction: [0, 1, 0] })),
        MiniAction('Aim +Z', () => state.UpdateSource(source.Id, { Direction: [0, 0, 1] })),
      ),
      Toggle({ Label: 'Enabled', Value: source.Enabled, Hint: 'Muted or hidden sources contribute nothing to the field.', OnChange: (v) => state.SetEnabled(source.Id, v) }),
      Toggle({ Label: 'Mute', Value: source.Muted, Hint: 'Keeps the source authored but out of the accumulation.', OnChange: (v) => state.UpdateSource(source.Id, { Muted: v }) }),
    ));

    // ---------------------------------------------------------------- envelope
    scroll.append(Section('Finite envelope'));
    const envelopeFields = [SelectField({ Label: 'Shape', Options: EnvelopeKinds, Value: source.Envelope, OnChange: (v) => state.UpdateSource(source.Id, { Envelope: v }) })];
    if (source.Envelope === 'Sphere' || source.Envelope === 'Column') {
      envelopeFields.push(RangeField({ Label: 'Radius [m]', Value: source.Radius, Min: 0.4, Max: 40, Step: 0.1, OnInput: (v) => state.UpdateSource(source.Id, { Radius: v }) }));
    }
    if (source.Envelope === 'Column') {
      envelopeFields.push(RangeField({ Label: 'Height [m]', Value: source.Height, Min: 0.4, Max: 60, Step: 0.1, OnInput: (v) => state.UpdateSource(source.Id, { Height: v }) }));
    }
    if (source.Envelope === 'Box') {
      envelopeFields.push(VectorField({ Label: 'Half extents (oriented)', Value: source.HalfExtents, Unit: 'm', Step: 0.25, OnCommit: (v) => state.UpdateSource(source.Id, { HalfExtents: v.map((c) => Math.max(0.2, Math.abs(c))) }) }));
    }
    envelopeFields.push(SelectField({ Label: 'Attenuation', Options: AttenuationKinds, Value: source.Attenuation, OnChange: (v) => state.UpdateSource(source.Id, { Attenuation: v }) }));
    envelopeFields.push(El('p', {}, 'Every attenuation curve reaches exactly zero at the envelope boundary, so a source can never spill past its reach.'));
    scroll.append(Panel(...envelopeFields));

    // ---------------------------------------------------------------- duration
    scroll.append(Section('Duration'));
    const durationFields = [SelectField({ Label: 'Mode', Options: DurationKinds, Value: source.Duration, OnChange: (v) => state.UpdateSource(source.Id, { Duration: v }) })];
    if (source.Duration === 'Modulated') {
      durationFields.push(
        RangeField({ Label: 'Period [s]', Value: source.ModulationPeriod, Min: 0.5, Max: 30, Step: 0.1, OnInput: (v) => state.UpdateSource(source.Id, { ModulationPeriod: v }) }),
        RangeField({ Label: 'Attack [s]', Value: source.ModulationAttack, Min: 0.05, Max: 10, Step: 0.05, OnInput: (v) => state.UpdateSource(source.Id, { ModulationAttack: v }) }),
        RangeField({ Label: 'Release [s]', Value: source.ModulationRelease, Min: 0.05, Max: 10, Step: 0.05, OnInput: (v) => state.UpdateSource(source.Id, { ModulationRelease: v }) }),
        RangeField({ Label: 'Phase', Value: source.ModulationPhase, Min: -1, Max: 1, Step: 0.01, OnInput: (v) => state.UpdateSource(source.Id, { ModulationPhase: v }) }),
      );
    }
    if (source.Duration === 'Impulse') {
      durationFields.push(
        RangeField({ Label: 'Interval [s]', Value: source.ImpulseInterval, Min: 0.5, Max: 20, Step: 0.1, OnInput: (v) => state.UpdateSource(source.Id, { ImpulseInterval: v }) }),
        RangeField({ Label: 'Wave speed [m/s]', Value: source.ImpulseSpeed, Min: 1, Max: 40, Step: 0.1, OnInput: (v) => state.UpdateSource(source.Id, { ImpulseSpeed: v }) }),
        RangeField({ Label: 'Shell width [m]', Value: source.ImpulseWidth, Min: 0.1, Max: 5, Step: 0.05, OnInput: (v) => state.UpdateSource(source.Id, { ImpulseWidth: v }) }),
        El('p', {}, 'The wavefront expands from the anchor each interval; the envelope radius still bounds how far it can reach.'),
        El('button', { type: 'button', class: 'primary wide', onclick: () => state.Retrigger() }, IconEl('bolt', 13), 'Fire an impulse now'),
      );
    }
    durationFields.push((liveReadout = El('div', { class: 'rows' })));
    scroll.append(Panel(...durationFields));

    // ---------------------------------------------------------------- motion
    scroll.append(Section('Motion'));
    const motionFields = [SelectField({ Label: 'Path', Options: MotionKinds, Value: source.Motion, OnChange: (v) => state.UpdateSource(source.Id, { Motion: v }) })];
    if (source.Motion !== 'Static') {
      motionFields.push(
        SelectField({ Label: 'Loop', Options: LoopPolicies, Value: source.LoopPolicy, OnChange: (v) => state.UpdateSource(source.Id, { LoopPolicy: v }) }),
        RangeField({ Label: 'Speed', Value: source.OrbitSpeed, Min: 0.02, Max: 4, Step: 0.01, Unit: '×', OnInput: (v) => state.UpdateSource(source.Id, { OrbitSpeed: v }) }),
        RangeField({ Label: 'Path length [m]', Value: source.PathLength, Min: 0.5, Max: 30, Step: 0.1, OnInput: (v) => state.UpdateSource(source.Id, { PathLength: v }) }),
        RangeField({ Label: 'Swirl radius [m]', Value: source.SwirlRadius, Min: 0, Max: 20, Step: 0.1, OnInput: (v) => state.UpdateSource(source.Id, { SwirlRadius: v }) }),
      );
    }
    motionFields.push(El('p', {}, 'Motion is evaluated as a pure function of the transport clock, so scrubbing backwards is exact and every source moves deterministically.'));
    scroll.append(Panel(...motionFields));

    // ---------------------------------------------------------------- formula workspace
    if (source.Law === 'CustomFormula') {
      scroll.append(Section('Formula workspace'));
      const presetRow = El('div', { class: 'preset-row' });
      for (const preset of FORMULA_PRESETS) {
        const button = El('button', { type: 'button', title: preset.Note }, preset.Label);
        button.addEventListener('click', () => {
          state.UpdateSource(source.Id, { Formula: preset.Code });
          Toast(`${preset.Label} loaded into the formula field`);
        });
        presetRow.append(button);
      }
      formulaArea = El('textarea', { spellcheck: 'false', 'aria-label': 'Formula source' });
      formulaArea.value = source.Formula;
      formulaArea.addEventListener('input', () => validateFormula());
      formulaArea.addEventListener('change', () => state.UpdateSource(source.Id, { Formula: formulaArea.value }, { Coalesce: `formula:${source.Id}` }));
      formulaStatus = El('div', { class: 'formula-status' });
      scroll.append(Panel(
        El('p', {}, 'One assignment per line: fx, fy, fz. Variables x, y, z, t, r, a; functions sin, cos, tan, asin, acos, atan, atan2, abs, sqrt, exp, log, sign, floor, ceil, round, fract, min, max, pow, mod, step, clamp, mix, smoothstep; constants pi, tau, e, phi.'),
        presetRow,
        El('div', { class: 'formula-editor' }, formulaArea),
        formulaStatus,
      ));
      validateFormula();
    }

    // ---------------------------------------------------------------- live readout
    scroll.append(Section('Live response'));
    scroll.append(Panel(livePanel(source, law)));
    refreshLive();
    scroll.append(El('div', { style: { height: '20px' } }));
  }

  function Row3(...children) {
    return El('div', { style: { display: 'grid', gridTemplateColumns: '1fr 1fr 1fr', gap: '5px' } }, ...children);
  }

  function MiniAction(label, onClick) {
    const button = El('button', { type: 'button', class: 'tool', style: { height: '26px', fontSize: '10px' } }, label);
    button.addEventListener('click', onClick);
    return button;
  }

  function changeLaw(source, lawId) {
    const law = LawById[lawId];
    if (!law) return;
    // Switching law keeps the placement, direction and name but reseeds params and bound defaults.
    state.Edit((draft) => {
      const target = draft.Sources.find((s) => s.Id === source.Id);
      if (!target) return draft;
      target.Law = law.Id;
      target.Params = Object.fromEntries(law.Params.map((p) => [p.Key, p.Default]));
      target.Envelope = law.Envelope || 'Sphere';
      target.Radius = law.Radius ?? target.Radius;
      target.Height = law.Height ?? target.Height;
      target.HalfExtents = (law.HalfExtents || target.HalfExtents).slice();
      target.Attenuation = law.Attenuation || target.Attenuation;
      target.Duration = law.Duration || 'Continuous';
      if (law.Formula) target.Formula = law.Formula.Default;
      return draft;
    });
    Toast(`${law.Name} selected`);
  }

  function livePanel(source, law) {
    const wrap = El('div', { class: 'rows' });
    const table = El('div', { class: 'rows' });
    wrap.append(table);
    wrap._refresh = () => {
      const runtime = state.Frame();
      const time = state.Time();
      const anchor = M.MotionAnchorAt(source, time);
      const gate = M.DurationGate(source, time, 0);
      const near = EvaluateAt(runtime, [anchor[0] + 0.6, anchor[1] + 0.4, anchor[2] + 0.5], time);
      const far = EvaluateAt(runtime, [anchor[0] + (source.Radius || 10) * 3.2 + 24, anchor[1], anchor[2]], time);
      const localSample = EvaluateAt(runtime, anchor, time);
      const outside = M.Length(far.Velocity) + M.Length(far.Acceleration);
      const inside = M.Length(near.Velocity) + M.Length(near.Acceleration);
      const quantity = law.Channel === 'velocity' ? 'velocity' : law.Channel === 'acceleration' ? 'acceleration' : 'damping';
      table.innerHTML = '';
      const bounded = outside < 1e-6;
      table.append(...[
        El('div', {}, El('span', {}, 'Gate now'), El('b', {}, gate.toFixed(3))),
        El('div', {}, El('span', {}, `Near ${quantity}`), El('b', {}, FormatNumber(law.Channel === 'velocity' ? M.Length(near.Velocity) : law.Channel === 'acceleration' ? M.Length(near.Acceleration) : near.Damping, 2))),
        El('div', {}, El('span', {}, 'At anchor'), El('b', {}, FormatNumber(M.Length(localSample.Velocity) + M.Length(localSample.Acceleration), 2))),
        El('div', {}, El('span', {}, 'Beyond bound'), El('b', {}, bounded ? '0.000 ✓' : `${outside.toFixed(3)} ⚠`)),
        El('div', {}, El('span', { class: `verdict ${bounded ? 'ok' : 'bad'}`, style: { marginTop: '4px' } },
          bounded
            ? `Bounded: ${(inside).toFixed(2)} inside the envelope, exactly zero past ${source.Envelope === 'Box' ? 'the box' : `${(source.Radius || 0).toFixed(1)} m`}.`
            : 'This source still contributes past its bound — check the attenuation and the envelope shape.')),
      ]);
    };
    return wrap;
  }

  // The live panel is rebuilt with the inspector on document edits, so one call here always hits the current node.
  function refreshLive() {
    if (liveReadout && liveReadout._refresh) liveReadout._refresh();
  }

  function validateFormula() {
    if (!formulaArea || !formulaStatus) return;
    const program = CompileFieldProgram(formulaArea.value);
    formulaArea.classList.toggle('bad', !program.Ok);
    formulaStatus.className = `formula-status ${program.Ok ? 'ok' : 'bad'}`;
    formulaStatus.textContent = program.Ok
      ? `Compiled ${program.ChannelCount} channel assignment${program.ChannelCount === 1 ? '' : 's'}; sampling is live in the viewport.`
      : program.Errors.map((e) => `line ${e.Line}: ${e.Message}`).join('  ·  ');
  }

  state.Subscribe((reason) => {
    if (reason === 'document' || reason === 'selection') {
      // Preserve focus inside the formula editor so typing is not interrupted by the rebuild.
      const active = document.activeElement;
      const inFormula = active && active.tagName === 'TEXTAREA' && scroll.contains(active);
      const inName = active && active.classList.contains('name-input');
      if (inFormula || inName) {
        if (inFormula) validateFormula();
        refreshLive();
        return;
      }
      rebuild();
    }
    if (reason === 'time') {
      refreshLive();
      if (diagram) diagram.Paint();
    }
  });

  return {
    Node: root,
    Mount: () => {
      rebuild();
      return root;
    },
    Rebuild: rebuild,
  };
}
