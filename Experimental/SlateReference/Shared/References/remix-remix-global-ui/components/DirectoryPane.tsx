'use client';
import React, { useState } from 'react';
import { useAppState, RecordEntry } from '@/lib/store';
import { ClassificationIcon, hueOf } from '@/lib/icons';
import { ChevronDown, Eye, EyeOff, Search } from 'lucide-react';

export function DirectoryTree({ onInspect }: { onInspect?: () => void }) {
  const { records, selection, filterText, setSelection, setFilterText, updateRecord } = useAppState();

  const toggleExpand = (token: string, e: React.MouseEvent) => {
    e.stopPropagation();
    const findAndToggle = (entries: RecordEntry[]) => {
      for (const entry of entries) {
        if (entry.token === token) {
          updateRecord(token, { expanded: !entry.expanded });
          return true;
        }
        if (entry.nested && findAndToggle(entry.nested)) return true;
      }
      return false;
    };
    findAndToggle(records);
  };

  const toggleVisibility = (entry: RecordEntry, e: React.MouseEvent) => {
    e.stopPropagation();
    const nextHidden = !entry.hidden;
    const hideAll = (ent: RecordEntry) => {
      updateRecord(ent.token, { hidden: nextHidden });
      if (ent.nested) ent.nested.forEach(hideAll);
    };
    hideAll(entry);
  };

  const handleRowClick = (token: string, e: React.MouseEvent) => {
    if (e.ctrlKey || e.metaKey) {
      const newSel = new Set(selection);
      if (newSel.has(token)) newSel.delete(token);
      else newSel.add(token);
      setSelection(newSel);
    } else {
      setSelection(new Set([token]));
    }
  };

  const isRetained = (entry: RecordEntry): boolean => {
    if (!filterText) return true;
    if (entry.name.toLowerCase().includes(filterText.toLowerCase())) return true;
    if (entry.nested) return entry.nested.some(isRetained);
    return false;
  };

  const renderRow = (entry: RecordEntry, depth: number) => {
    if (filterText && !isRetained(entry)) return null;
    const isBranch = !!entry.nested && entry.nested.length > 0;
    const isExpanded = entry.expanded || !!filterText;
    const isSelected = selection.has(entry.token);
    
    return (
      <React.Fragment key={entry.token}>
        <div 
          className={`relative flex items-center gap-2 h-[32px] pr-[7px] rounded-md text-[12.5px] cursor-pointer transition-colors
            ${isSelected ? 'bg-[var(--row-sel)] text-[var(--ink)]' : 'text-[var(--muted)] hover:bg-[var(--row-hover)] hover:text-[var(--ink)]'}
            ${entry.hidden ? 'opacity-50' : ''}`}
          style={{ paddingLeft: `${8 + depth * 15}px` }}
          onClick={(e) => handleRowClick(entry.token, e)}
          onDoubleClick={() => onInspect?.()}
        >
          {isSelected && (
            <div className="absolute left-[-7px] top-1/2 -translate-y-1/2 w-[3px] h-[15px] rounded-r-sm bg-[var(--accent)]" />
          )}
          
          <div className="w-[15px] h-[15px] flex-none flex items-center justify-center text-[var(--faint)] hover:text-[var(--ink)] transition-colors" onClick={(e) => isBranch && toggleExpand(entry.token, e)}>
            {isBranch ? (
              <ChevronDown size={14} className={`transition-transform ${!isExpanded ? '-rotate-90' : ''}`} />
            ) : <span className="w-[14px]" />}
          </div>

          <div className="w-[18px] h-[18px] flex-none flex items-center justify-center">
            <ClassificationIcon cls={entry.classification} size={18} />
          </div>

          <div className="flex-1 min-w-0 whitespace-nowrap overflow-hidden text-ellipsis">{entry.name}</div>
          
          {isBranch && (
            <div className="text-[10px] text-[var(--faint)] font-mono flex-none">{entry.nested!.length}</div>
          )}

          <div className={`w-5 h-5 flex-none flex items-center justify-center rounded-sm text-[var(--faint)] hover:bg-[var(--tile-hi)] hover:text-[var(--ink)] transition-all ${entry.hidden ? 'opacity-100' : 'opacity-0 group-hover:opacity-100'}`} onClick={(e) => toggleVisibility(entry, e)}>
            {entry.hidden ? <EyeOff size={14} /> : <Eye size={14} />}
          </div>
        </div>
        {isBranch && isExpanded && entry.nested!.map(child => renderRow(child, depth + 1))}
      </React.Fragment>
    );
  };

  return (
    <div className="flex flex-col h-full bg-[var(--menu)] border-r border-[var(--hair)]">
      {/* Header */}
      <div className="flex-none flex items-center gap-[10px] h-[46px] px-[10px] bg-[var(--menu)] border-b border-[var(--hair)]">
        <span className="w-6 h-6 flex-none bg-black rounded-md flex items-center justify-center text-white">
          <ClassificationIcon cls="scene" size={16} />
        </span>
        <div className="flex-1 min-w-0 text-[12.5px] font-semibold leading-tight">
          <div className="whitespace-nowrap overflow-hidden text-ellipsis">Directory</div>
          <div className="text-[10px] font-normal text-[var(--faint)] mt-[2px]">Bracket_Rev4</div>
        </div>
        <span className="flex-none text-[10px] text-[var(--muted)] bg-[var(--menu-2)] px-2.5 py-1 rounded-full font-mono">{selection.size > 1 ? `${selection.size} sel` : '0'}</span>
      </div>

      {/* Search */}
      <div className="flex-none p-2 pb-1">
        <label className="flex items-center gap-2 h-[30px] px-2.5 bg-[var(--menu-2)] border border-[var(--hair)] rounded-md focus-within:border-[var(--outline)] transition-colors">
          <Search size={14} className="text-[var(--faint)]" />
          <input 
            type="text" 
            placeholder="Filter…" 
            className="flex-1 bg-transparent border-none outline-none text-[12px] text-[var(--ink)] placeholder:text-[var(--faint)]"
            value={filterText}
            onChange={e => setFilterText(e.target.value)}
            spellCheck={false}
          />
        </label>
      </div>

      {/* Tree Body */}
      <div className="flex-1 min-h-0 overflow-y-auto px-2 pb-2 group">
        {records.map(entry => renderRow(entry, 0))}
      </div>

      {/* Footer */}
      <div className="flex-none flex items-center gap-[7px] h-[26px] px-[10px] bg-[var(--menu-2)] border-t border-[var(--hair)] text-[10px] text-[var(--muted)]">
        <span className="font-mono text-[var(--ink)]">1</span> groups <span className="text-[var(--value-unit)]">·</span> <span className="font-mono text-[var(--ink)]">4</span> bodies
      </div>
    </div>
  );
}
