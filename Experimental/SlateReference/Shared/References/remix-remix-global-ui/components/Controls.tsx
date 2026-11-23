'use client';
import React, { useState, useEffect, useRef } from 'react';
import { useAppState } from '@/lib/store';

export function Slider({ value, onChange, min, max, unit = '·', fmt = 2 }: {
  value: number, onChange: (v: number) => void, min: number, max: number, unit?: string, fmt?: number
}) {
  const t = Math.max(0, Math.min(1, (value - min) / (max - min)));
  const trackRef = useRef<HTMLDivElement>(null);

  const handlePointerDown = (e: React.PointerEvent) => {
    if (!trackRef.current) return;
    const track = trackRef.current;
    track.setPointerCapture(e.pointerId);

    const updateFromX = (clientX: number) => {
      const rect = track.getBoundingClientRect();
      const pct = Math.max(0, Math.min(1, (clientX - rect.left) / rect.width));
      let val = min + pct * (max - min);
      if (fmt === 0) val = Math.round(val);
      onChange(val);
    };
    updateFromX(e.clientX);

    const onMove = (ev: PointerEvent) => updateFromX(ev.clientX);
    const onUp = () => {
      track.removeEventListener('pointermove', onMove);
      track.removeEventListener('pointerup', onUp);
    };
    track.addEventListener('pointermove', onMove);
    track.addEventListener('pointerup', onUp);
  };

  return (
    <div className="flex items-center gap-[7px] min-w-0">
      <div className="valuebox flex-[0_0_78px]">
        <div className="num">
          <input type="number" value={Number(value).toFixed(fmt)} onChange={e => {
            const v = parseFloat(e.target.value);
            if (!isNaN(v)) onChange(Math.max(min, Math.min(max, v)));
          }} />
        </div>
        <div className="unitseg">{unit}</div>
      </div>
      <div className="slider flex-1 min-w-0" ref={trackRef} onPointerDown={handlePointerDown}>
        <div className="fill" style={{ width: `${t * 100}%` }} />
        <div className="knob" style={{ left: `${t * 100}%` }} />
      </div>
    </div>
  );
}

export function ValueSlider({ value, onChange, min, max, label, unit = '·', fmt = 2 }: {
  value: number, onChange: (v: number) => void, min: number, max: number, label: string, unit?: string, fmt?: number
}) {
  return (
    <div className="grid grid-cols-[88px_minmax(0,1fr)] gap-2.5 items-center min-h-[var(--row-h)]">
      <div className="text-[13.5px] font-medium text-[var(--muted)] whitespace-nowrap overflow-hidden text-ellipsis">{label}</div>
      <Slider value={value} onChange={onChange} min={min} max={max} unit={unit} fmt={fmt} />
    </div>
  );
}

export function ScalarEntry({ value, onChange, step = 0.01, label, unit = '·', fmt = 2 }: {
  value: number, onChange: (v: number) => void, step?: number, label: string, unit?: string, fmt?: number
}) {
  const trackRef = useRef<HTMLDivElement>(null);

  const handlePointerDown = (e: React.PointerEvent) => {
    if (!trackRef.current) return;
    const track = trackRef.current;
    track.setPointerCapture(e.pointerId);
    let lastX = e.clientX;

    const onMove = (ev: PointerEvent) => {
      const dx = ev.clientX - lastX;
      lastX = ev.clientX;
      let val = value + dx * step;
      if (fmt === 0) val = Math.round(val);
      onChange(val);
    };
    const onUp = () => {
      track.removeEventListener('pointermove', onMove);
      track.removeEventListener('pointerup', onUp);
    };
    track.addEventListener('pointermove', onMove);
    track.addEventListener('pointerup', onUp);
  };

  return (
    <div className="grid grid-cols-[88px_minmax(0,1fr)] gap-2.5 items-center min-h-[var(--row-h)]">
      <div className="text-[13.5px] font-medium text-[var(--muted)] whitespace-nowrap overflow-hidden text-ellipsis">{label}</div>
      <div className="flex items-center gap-[7px] min-w-0">
        <div className="valuebox flex-[0_0_78px]">
          <div className="num">
            <input type="number" step={step} value={Number(value).toFixed(fmt)} onChange={e => {
              const v = parseFloat(e.target.value);
              if (!isNaN(v)) onChange(v);
            }} />
          </div>
          <div className="unitseg">{unit}</div>
        </div>
        <div className="slider flex-1 min-w-0" ref={trackRef} onPointerDown={handlePointerDown}>
          <div className="knob" style={{ left: '50%' }} />
        </div>
      </div>
    </div>
  );
}

export function VectorEntry({ value, onChange, step = 0.01, label, fmt = 2 }: {
  value: [number, number, number], onChange: (v: [number, number, number]) => void, step?: number, label: string, fmt?: number
}) {
  const update = (idx: number, v: number) => {
    const next: [number, number, number] = [...value];
    next[idx] = v;
    onChange(next);
  };

  return (
    <div className="grid grid-cols-[88px_minmax(0,1fr)] gap-2.5 items-center min-h-[var(--row-h)]">
      <div className="text-[13.5px] font-medium text-[var(--muted)] whitespace-nowrap overflow-hidden text-ellipsis">{label}</div>
      <div className="grid grid-cols-3 gap-1 min-w-0">
        {['X', 'Y', 'Z'].map((axis, i) => (
          <div key={axis} className="valuebox">
            <div className="axisseg">{axis}</div>
            <div className="num !px-2">
              <input type="number" step={step} value={Number(value[i]).toFixed(fmt)} onChange={e => {
                const v = parseFloat(e.target.value);
                if (!isNaN(v)) update(i, v);
              }} />
            </div>
          </div>
        ))}
      </div>
    </div>
  );
}

export function BooleanEntry({ value, onChange, label }: { value: boolean, onChange: (v: boolean) => void, label: string }) {
  return (
    <div className="grid grid-cols-[88px_minmax(0,1fr)] gap-2.5 items-center min-h-[var(--row-h)]">
      <div className="text-[13.5px] font-medium text-[var(--muted)] whitespace-nowrap overflow-hidden text-ellipsis">{label}</div>
      <div className={`switch ${value ? 'on' : ''}`} onClick={() => onChange(!value)}>
        <div className="nub" />
      </div>
    </div>
  );
}

export function SelectionEntry({ value, onChange, label, options }: { value: number, onChange: (v: number) => void, label: string, options: string[] }) {
  return (
    <div className="grid grid-cols-[88px_minmax(0,1fr)] gap-2.5 items-center min-h-[var(--row-h)]">
      <div className="text-[13.5px] font-medium text-[var(--muted)] whitespace-nowrap overflow-hidden text-ellipsis">{label}</div>
      <div className="segment">
        {options.map((opt, i) => (
          <div key={opt} className={`seg-opt ${value === i ? 'sel' : ''}`} onClick={() => onChange(i)}>
            {opt}
          </div>
        ))}
      </div>
    </div>
  );
}

export function Dropdown({ value, onChange, options }: { value: number, onChange: (v: number) => void, options: string[] }) {
  const [open, setOpen] = useState(false);
  const headRef = useRef<HTMLDivElement>(null);
  const [rect, setRect] = useState<{ left: number, top: number, width: number } | null>(null);

  useEffect(() => {
    const clickOut = () => setOpen(false);
    document.addEventListener('click', clickOut);
    return () => document.removeEventListener('click', clickOut);
  }, []);

  const toggle = (e: React.MouseEvent) => {
    e.stopPropagation();
    if (!open && headRef.current) {
      const r = headRef.current.getBoundingClientRect();
      setRect({ left: r.left, top: r.bottom + 4, width: r.width });
    }
    setOpen(!open);
  };

  return (
    <div className={`dropdown ${open ? 'open' : ''}`}>
      <div className="dd-head" ref={headRef} onClick={toggle}>
        <span className="cur">{options[value]}</span>
        <span className="caret">
          <span>
            <svg viewBox="0 0 24 24" width="11" height="11" fill="none" stroke="currentColor" strokeWidth="1.7" strokeLinecap="round" strokeLinejoin="round"><path d="M6 9l6 6 6-6"/></svg>
          </span>
        </span>
      </div>
      {open && rect && (
        <div className="dd-list !block" style={{ left: rect.left, top: rect.top, width: rect.width }}>
          {options.map((opt, i) => (
            <div key={opt} className={`dd-item ${value === i ? 'sel' : ''}`} onClick={() => { onChange(i); setOpen(false); }}>
              <span className="flex-1 overflow-hidden text-ellipsis whitespace-nowrap">{opt}</span>
              <span className="radio" />
            </div>
          ))}
        </div>
      )}
    </div>
  );
}

export function DropdownEntry({ value, onChange, label, options }: { value: number, onChange: (v: number) => void, label: string, options: string[] }) {
  return (
    <div className="grid grid-cols-[88px_minmax(0,1fr)] gap-2.5 items-center min-h-[var(--row-h)]">
      <div className="text-[13.5px] font-medium text-[var(--muted)] whitespace-nowrap overflow-hidden text-ellipsis">{label}</div>
      <Dropdown value={value} onChange={onChange} options={options} />
    </div>
  );
}

export function ColorEntry({ value, onChange, label }: { value: [number, number, number, number], onChange: (v: [number, number, number, number]) => void, label: string }) {
  const [open, setOpen] = useState(false);
  const [r, g, b, a] = value;
  const alphaVal = a / 255;
  const css = `rgba(${r},${g},${b},${alphaVal})`;

  return (
    <div className="grid grid-cols-[88px_minmax(0,1fr)] gap-2.5 items-start min-h-[var(--row-h)] py-[2px]">
      <div className="text-[13.5px] font-medium text-[var(--muted)] whitespace-nowrap overflow-hidden text-ellipsis leading-[var(--row-h)]">{label}</div>
      <div className="min-w-0">
        <div className={`colorbar ${open ? 'open' : ''}`} onClick={() => setOpen(!open)}>
          <div className="swatchseg">
            <div className="circle" style={{ background: css }} />
            <div className="cname">{r}, {g}, {b} <span className="dim">{alphaVal.toFixed(2)}</span></div>
          </div>
          <div className="caret"><span>▾</span></div>
        </div>
        <div className={`picker ${open ? 'open' : ''}`}>
          <div className="svbox" style={{ background: `linear-gradient(to top,#000,transparent),linear-gradient(to right,#fff,transparent),rgb(${r},${g},${b})` }}>
            <div className="svknob" style={{ left: '50%', top: '50%', background: css }} />
          </div>
          <div className="barstack">
            <div className="cbar huebar" style={{ background: 'linear-gradient(90deg,#f00,#ff0,#0f0,#0ff,#00f,#f0f,#f00)' }}>
              <div className="cknob" style={{ left: '50%' }} />
            </div>
            <div className="cbar alphabar" style={{ position: 'relative' }}>
              <div style={{ position: 'absolute', inset: 0, borderRadius: 999, background: 'repeating-conic-gradient(#808080 0 25%,#c0c0c0 0 50%) 0 0/12px 12px' }} />
              <div style={{ position: 'absolute', inset: 0, borderRadius: 999, background: `linear-gradient(90deg,transparent,rgb(${r},${g},${b}))` }} />
              <div className="cknob" style={{ left: `${alphaVal * 100}%` }} />
            </div>
          </div>
          <div className="flex items-center gap-2 mt-2">
            <input type="text" readOnly value={`#${r.toString(16).padStart(2,'0')}${g.toString(16).padStart(2,'0')}${b.toString(16).padStart(2,'0')}`.toUpperCase()} 
              className="flex-1 bg-[var(--value-focus)] border border-[var(--hair-strong)] rounded-md h-[24px] px-2 text-white text-[11px] font-mono outline-none" />
            <div className="text-[10px] text-[var(--muted)] font-mono">A {Math.round(alphaVal * 100)}%</div>
          </div>
        </div>
      </div>
    </div>
  );
}

export function PathEntry({ value, onChange, label }: { value: string, onChange: (v: string) => void, label: string }) {
  return (
    <div className="grid grid-cols-[88px_minmax(0,1fr)] gap-2.5 items-center min-h-[var(--row-h)]">
      <div className="text-[13.5px] font-medium text-[var(--muted)] whitespace-nowrap overflow-hidden text-ellipsis">{label}</div>
      <div className="pathfield">
        <div className="value">
          <input type="text" value={value || ''} onChange={e => onChange(e.target.value)} />
        </div>
        <button className="browse" onClick={() => {
          const picked = window.prompt('Document path', value);
          if (picked !== null) onChange(picked);
        }}>…</button>
      </div>
    </div>
  );
}

export function TextEntry({ value, onChange, label }: { value: string, onChange: (v: string) => void, label: string }) {
  return (
    <div className="grid grid-cols-[88px_minmax(0,1fr)] gap-2.5 items-center min-h-[var(--row-h)]">
      <div className="text-[13.5px] font-medium text-[var(--muted)] whitespace-nowrap overflow-hidden text-ellipsis">{label}</div>
      <div className="pathfield">
        <div className="value !rounded-[7px] w-full">
          <input type="text" value={value || ''} onChange={e => onChange(e.target.value)} />
        </div>
      </div>
    </div>
  );
}
