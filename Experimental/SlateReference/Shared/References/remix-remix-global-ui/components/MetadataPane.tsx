'use client';
import React from 'react';
import { useAppState, RecordEntry } from '@/lib/store';
import { ClassificationIcon, hueOf } from '@/lib/icons';
import { Eye, EyeOff, Copy, Trash, Type, BoxSelect, Target, Settings2, Plus, SlidersHorizontal } from 'lucide-react';

const CLASSIFICATION_LABEL: Record<string, string> = {
  scene: 'Part', folder: 'Body', solid: 'Solid', cylinder: 'Cylinder', sphere: 'Sphere',
  cone: 'Cone', sketch: 'Sketch', revolve: 'Revolve', loft: 'Loft'
};

export function MetadataPane({ onInspect }: { onInspect: () => void }) {
  const { records, selection, updateRecord } = useAppState();

  const selectedTokens = Array.from(selection);
  
  if (selectedTokens.length === 0) {
    return (
      <div className="flex-1 p-6 flex flex-col justify-center items-center text-center">
        <div className="w-[48px] h-[48px] bg-[var(--tile)] border border-[var(--hair)] rounded-xl flex items-center justify-center mb-4 text-[var(--muted)]">
           <Target size={24} />
        </div>
        <div className="text-[12.5px] text-[var(--ink)] font-semibold mb-1">Properties Overview</div>
        <div className="text-[11px] text-[var(--faint)] leading-relaxed mb-6 max-w-[200px]">
          Select a record in the directory to view its details.
        </div>
      </div>
    );
  }

  // Handle single selection
  const search = (entries: RecordEntry[]): RecordEntry | null => {
    for (const e of entries) {
      if (e.token === selectedTokens[0]) return e;
      if (e.nested) {
        const found = search(e.nested);
        if (found) return found;
      }
    }
    return null;
  };
  
  const entry = search(records);

  if (!entry || !entry.profile) return null;

  const hue = hueOf(entry.classification);
  const profile = entry.profile;

  const getStats = () => {
    const stats: {k: string, v: string | React.ReactNode}[] = [];
    stats.push({ k: 'Token', v: <span className="font-mono">{entry.token}</span> });
    stats.push({ k: 'Visible', v: entry.hidden ? 'hidden' : 'shown' });
    
    if (entry.nested) {
      stats.push({ k: 'Nested', v: `${entry.nested.length} records` });
    }
    if (profile.Position) {
      stats.push({ k: 'Position', v: `[${profile.Position.map(n => n.toFixed(1)).join(', ')}]` });
    }
    if (profile.Radius !== undefined) stats.push({ k: 'Radius', v: `${profile.Radius.toFixed(2)} mm` });
    if (profile.Height !== undefined) stats.push({ k: 'Height', v: `${profile.Height.toFixed(2)} mm` });
    if (profile.CurveTally !== undefined) stats.push({ k: 'Curves', v: profile.CurveTally });
    if (profile.FullyConstrained !== undefined) stats.push({ k: 'Status', v: profile.FullyConstrained ? 'Constrained' : 'Unconstrained' });
    if (profile.ExtrudeDepth !== undefined) stats.push({ k: 'Depth', v: `${profile.ExtrudeDepth.toFixed(2)} mm` });

    return stats;
  };

  const toggleVisible = () => {
    updateRecord(entry.token, { hidden: !entry.hidden });
  };

  return (
    <div className="flex-1 flex flex-col min-h-0">
      <div className="flex-1 min-h-0 overflow-y-auto p-3 flex flex-col gap-3">
        {/* Hero */}
        <div className="flex items-center gap-3 p-2.5 bg-[var(--tile)] border border-[var(--hair)] rounded-[var(--r-tile)]">
          <div className="w-[34px] h-[34px] bg-black rounded-lg flex items-center justify-center text-white flex-none">
            <ClassificationIcon cls={entry.classification} size={24} />
          </div>
          <div className="flex-1 min-w-0">
            <div className="text-[13px] font-semibold text-[var(--ink)] whitespace-nowrap overflow-hidden text-ellipsis">{entry.name}</div>
            <div className="text-[10.5px] font-medium mt-[2px]" style={{ color: hue }}>
              {CLASSIFICATION_LABEL[entry.classification]}
            </div>
          </div>
        </div>

        {/* Stats */}
        <div className="flex flex-col">
          {getStats().map((stat, i) => (
            <div key={i} className="flex items-center justify-between h-[28px] border-b border-[var(--hair)] last:border-b-0 text-[11.5px]">
              <span className="text-[var(--muted)]">{stat.k}</span>
              <span className="text-[var(--text-value)]">{stat.v}</span>
            </div>
          ))}
        </div>

        {/* Color preview if applicable */}
        {profile.Albedo && (
          <div className="flex items-center justify-between h-[28px] border-b border-[var(--hair)] text-[11.5px]">
            <span className="text-[var(--muted)]">Albedo</span>
            <div className="flex items-center gap-2">
              <span className="font-mono text-[var(--text-value)]">
                {profile.Albedo[0]}, {profile.Albedo[1]}, {profile.Albedo[2]}
              </span>
              <div 
                className="w-4 h-4 rounded-full border border-[var(--hair-strong)]" 
                style={{ background: `rgba(${profile.Albedo[0]}, ${profile.Albedo[1]}, ${profile.Albedo[2]}, ${profile.Albedo[3]/255})` }} 
              />
            </div>
          </div>
        )}

        {/* Advance CTA */}
        <button className="flex items-center justify-center gap-2 w-full h-[32px] mt-2 rounded-lg bg-[rgba(91,140,255,.13)] border border-[var(--accent)] text-[var(--ink)] font-semibold text-[11.5px] hover:bg-[rgba(91,140,255,.2)] transition-colors shadow-sm" onClick={onInspect}>
          <SlidersHorizontal size={14} />
          <span>Properties & History</span>
          <span className="text-[9.5px] font-normal text-[var(--muted)] bg-[var(--menu-2)] px-2 py-0.5 rounded-full">Tab</span>
        </button>

        {/* Inline Actions */}
        <div className="mt-3 flex flex-col">
          <div className="text-[9.5px] tracking-[1.1px] text-[var(--faint)] font-semibold uppercase px-1 pb-2">Actions</div>
          
          <div className="flex items-center gap-2 h-[29px] px-2 rounded-lg text-[11.5px] text-[var(--ink)] cursor-pointer hover:bg-[var(--tile-hi)]">
            <span className="w-[15px] h-[15px] flex items-center justify-center text-[var(--muted)]"><Plus size={14} /></span>
            <span>New record</span>
          </div>
          
          <div className="h-[1px] bg-[var(--hair)] my-1.5 mx-2" />
          
          <div className="flex items-center gap-2 h-[29px] px-2 rounded-lg text-[11.5px] text-[var(--ink)] cursor-pointer hover:bg-[var(--tile-hi)]">
            <span className="w-[15px] h-[15px] flex items-center justify-center text-[var(--muted)]"><Type size={14} /></span>
            <span>Rename</span>
            <span className="ml-auto text-[9.5px] text-[var(--faint)] font-mono">F2</span>
          </div>
          
          <div className="flex items-center gap-2 h-[29px] px-2 rounded-lg text-[11.5px] text-[var(--ink)] cursor-pointer hover:bg-[var(--tile-hi)]">
            <span className="w-[15px] h-[15px] flex items-center justify-center text-[var(--muted)]"><Copy size={14} /></span>
            <span>Duplicate</span>
            <span className="ml-auto text-[9.5px] text-[var(--faint)] font-mono">Ctrl D</span>
          </div>
          
          <div className="flex items-center gap-2 h-[29px] px-2 rounded-lg text-[11.5px] text-[var(--ink)] cursor-pointer hover:bg-[var(--tile-hi)]" onClick={toggleVisible}>
            <span className="w-[15px] h-[15px] flex items-center justify-center text-[var(--muted)]">
              {entry.hidden ? <EyeOff size={14} /> : <Eye size={14} />}
            </span>
            <span>{entry.hidden ? 'Show' : 'Hide'}</span>
            <span className="ml-auto text-[9.5px] text-[var(--faint)] font-mono">H</span>
          </div>

          <div className="flex items-center gap-2 h-[29px] px-2 rounded-lg text-[11.5px] text-[#ff6b6b] cursor-pointer hover:bg-[rgba(255,107,107,.12)]">
            <span className="w-[15px] h-[15px] flex items-center justify-center text-[#ff6b6b]"><Trash size={14} /></span>
            <span>Delete</span>
            <span className="ml-auto text-[9.5px] text-[var(--faint)] font-mono">Del</span>
          </div>
        </div>
      </div>
      
      {/* Footer */}
      <div className="flex-none flex items-center gap-[7px] h-[26px] px-[10px] bg-[var(--menu-2)] border-t border-[var(--hair)] text-[10px] text-[var(--muted)]">
        <span className="w-2 h-2 rounded-sm flex-none" style={{ background: hue }} />
        <span>{CLASSIFICATION_LABEL[entry.classification]}</span>
        <div className="flex-1" />
        <span className="font-mono">{entry.token}</span>
      </div>
    </div>
  );
}
