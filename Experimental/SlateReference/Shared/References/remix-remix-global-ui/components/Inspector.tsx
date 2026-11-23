'use client';
import React, { useState } from 'react';
import { useAppState, establishProfile, RecordProfile, RecordEntry, Revision } from '@/lib/store';
import { ClassificationIcon, hueOf } from '@/lib/icons';
import { ChevronRight, ChevronDown } from 'lucide-react';
import { ValueSlider, ScalarEntry, VectorEntry, BooleanEntry, SelectionEntry, DropdownEntry, ColorEntry, PathEntry, TextEntry } from '@/components/Controls';

const CLASSIFICATION_LABEL: Record<string, string> = {
  scene: 'Part', folder: 'Body', solid: 'Solid', cylinder: 'Cylinder', sphere: 'Sphere',
  cone: 'Cone', sketch: 'Sketch', revolve: 'Revolve', loft: 'Loft'
};

export function PropertiesPane({ onBack }: { onBack?: () => void }) {
  const { records, selection, updateProfile, updateRecord, revisions, updateRevision } = useAppState();
  const [carouselMode, setCarouselMode] = useState<'properties' | 'history'>('properties');
  const [collapsedCards, setCollapsedCards] = useState<Record<string, boolean>>({});
  const [collapsedHistory, setCollapsedHistory] = useState<Record<string, boolean>>({});
  const [expandedRevisions, setExpandedRevisions] = useState<Record<string, boolean>>({});

  const toggleCard = (title: string) => {
    setCollapsedCards(prev => ({ ...prev, [title]: !prev[title] }));
  };

  const toggleHistoryCard = (token: string) => {
    setCollapsedHistory(prev => ({ ...prev, [token]: !prev[token] }));
  };

  const toggleRevisionCard = (revId: string) => {
    setExpandedRevisions(prev => ({ ...prev, [revId]: !prev[revId] }));
  };

  const searchToken = (token: string, entries: RecordEntry[]): RecordEntry | null => {
    for (const e of entries) {
      if (e.token === token) return e;
      if (e.nested) {
        const found = searchToken(token, e.nested);
        if (found) return found;
      }
    }
    return null;
  };

  const selectedTokens = Array.from(selection);
  const entry = selectedTokens.length === 1 ? searchToken(selectedTokens[0], records) : null;

  if (selectedTokens.length === 0) {
    return (
      <div className="flex flex-col h-full overflow-hidden bg-[var(--menu-2)]">
        <div className="flex-none flex items-center h-[46px] px-[10px] bg-[var(--menu-2)] border-b border-[var(--hair)]">
          <span className="w-6 h-6 flex-none bg-black rounded-md flex items-center justify-center text-white mr-2">
            <ClassificationIcon cls="scene" size={14} />
          </span>
          <span className="flex-1 text-[12.5px] font-semibold">Nothing selected</span>
          {onBack && (
            <button className="flex-none flex items-center gap-1.5 px-2 h-7 rounded-md text-[11px] font-medium text-[var(--muted)] hover:text-[var(--ink)] hover:bg-[var(--tile-hi)] transition-colors ml-2" onClick={onBack}>
              <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" className="w-3.5 h-3.5"><path d="M15 18l-6-6 6-6"/></svg>
              Back to scene directory
            </button>
          )}
        </div>
        <div className="flex-1 p-6 text-center text-[var(--faint)] text-[11.5px]">Select a record to inspect its properties.</div>
      </div>
    );
  }

  if (selectedTokens.length > 1) {
    return (
      <div className="flex flex-col h-full overflow-hidden bg-[var(--menu-2)]">
         <div className="flex-none flex items-center h-[46px] px-[10px] bg-[var(--menu-2)] border-b border-[var(--hair)]">
          <span className="w-6 h-6 flex-none bg-black rounded-md flex items-center justify-center text-white mr-2">
            <ClassificationIcon cls="scene" size={14} />
          </span>
          <span className="flex-1 text-[12.5px] font-semibold">{selectedTokens.length} records</span>
          {onBack && (
            <button className="flex-none flex items-center gap-1.5 px-2 h-7 rounded-md text-[11px] font-medium text-[var(--muted)] hover:text-[var(--ink)] hover:bg-[var(--tile-hi)] transition-colors ml-2" onClick={onBack}>
              <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" className="w-3.5 h-3.5"><path d="M15 18l-6-6 6-6"/></svg>
              Back to scene directory
            </button>
          )}
        </div>
        <div className="flex-1 p-6 text-center text-[var(--faint)] text-[11.5px]">Multiple selection properties not supported in prototype.</div>
      </div>
    );
  }

  if (!entry || !entry.profile) return null;

  const hue = hueOf(entry.classification);
  const profile = entry.profile;

  const changeProfile = (key: string, val: any) => {
    updateProfile(entry.token, { [key]: val });
  };

  const renderFields = (fields: any[]) => {
    return fields.map((f, i) => {
      const key = f.key as string;
      const val = profile[key];
      switch (f.ctl) {
        case 'text': return <TextEntry key={i} label={f.label} value={val || ''} onChange={v => {
          if (key === 'Name') updateRecord(entry.token, { name: v });
          changeProfile(key, v);
        }} />;
        case 'vector': return <VectorEntry key={i} label={f.label} value={val} step={f.step} fmt={f.fmt} onChange={v => changeProfile(key, v)} />;
        case 'slider': return <ValueSlider key={i} label={f.label} value={val} min={f.min} max={f.max} fmt={f.fmt} unit={f.unit} onChange={v => changeProfile(key, v)} />;
        case 'scalar': return <ScalarEntry key={i} label={f.label} value={val} step={f.step} fmt={f.fmt} unit={f.unit} onChange={v => changeProfile(key, v)} />;
        case 'boolean': return <BooleanEntry key={i} label={f.label} value={val} onChange={v => {
          if (key === 'Visible') updateRecord(entry.token, { hidden: !v });
          changeProfile(key, v);
        }} />;
        case 'selection': return <SelectionEntry key={i} label={f.label} value={val} options={f.options} onChange={v => changeProfile(key, v)} />;
        case 'dropdown': return <DropdownEntry key={i} label={f.label} value={val} options={f.options} onChange={v => changeProfile(key, v)} />;
        case 'color': return <ColorEntry key={i} label={f.label} value={val} onChange={v => changeProfile(key, v)} />;
        case 'path': return <PathEntry key={i} label={f.label} value={val} onChange={v => changeProfile(key, v)} />;
        default: return null;
      }
    });
  };

  const getCards = () => {
    const cards: any[] = [];
    cards.push({ title: 'Record', fields: [
      { ctl: 'text', key: 'Name', label: 'Name' },
      { ctl: 'boolean', key: 'Visible', label: 'Visible' }
    ]});

    const TRANSFORM = { title: 'Transform', fields: [
      { ctl: 'vector', key: 'Position', label: 'Position', step: 0.1, fmt: 2 },
      { ctl: 'vector', key: 'Rotation', label: 'Rotation', step: 0.5, fmt: 1 },
      { ctl: 'vector', key: 'Scale', label: 'Scale', step: 0.01, fmt: 3 }
    ]};
    
    const APPEARANCE = { title: 'Appearance', fields: [
      { ctl: 'color', key: 'Albedo', label: 'Albedo' },
      { ctl: 'slider', key: 'Roughness', label: 'Roughness', min: 0, max: 1, fmt: 2 },
      { ctl: 'slider', key: 'Metalness', label: 'Metalness', min: 0, max: 1, fmt: 2 },
      { ctl: 'dropdown', key: 'ShadingMode', label: 'Shading', options: ['Smooth', 'Faceted', 'Flat'] }
    ]};

    switch (entry.classification) {
      case 'scene':
        cards[0].fields.push({ ctl: 'dropdown', key: 'Units', label: 'Units', options: ['Inches', 'Millimetres', 'Centimetres', 'Metres'] });
        cards.push({ title: 'Tolerance', fields: [
          { ctl: 'scalar', key: 'ToleranceLinear', label: 'Linear', step: 0.001, fmt: 3, unit: 'mm' },
          { ctl: 'scalar', key: 'ToleranceAngular', label: 'Angular', step: 0.1, fmt: 1, unit: '°' },
          { ctl: 'path', key: 'DocumentPath', label: 'Document' }
        ]});
        break;
      case 'folder':
        cards.push({ title: 'Group', fields: [
          { ctl: 'selection', key: 'BooleanMode', label: 'Boolean', options: ['Union', 'Subtract', 'Intersect'] },
          { ctl: 'boolean', key: 'Suppressed', label: 'Suppress' },
          { ctl: 'scalar', key: 'NestedTally', label: 'Records', step: 1, fmt: 0, unit: 'ct' }
        ]});
        cards.push(TRANSFORM);
        break;
      case 'solid':
        cards.push({ title: 'Extrusion', fields: [
          { ctl: 'scalar', key: 'ExtrudeDepth', label: 'Depth', step: 0.1, fmt: 2, unit: 'mm' },
          { ctl: 'slider', key: 'DraftAngle', label: 'Draft', min: -30, max: 30, fmt: 1, unit: '°' },
          { ctl: 'scalar', key: 'WallThickness', label: 'Wall', step: 0.1, fmt: 2, unit: 'mm' },
          { ctl: 'boolean', key: 'CappedEnds', label: 'Cap ends' }
        ]});
        cards.push(TRANSFORM, APPEARANCE);
        break;
      case 'cylinder':
        cards.push({ title: 'Cylinder', fields: [
          { ctl: 'scalar', key: 'Radius', label: 'Radius', step: 0.05, fmt: 2, unit: 'mm' },
          { ctl: 'scalar', key: 'Height', label: 'Height', step: 0.1, fmt: 2, unit: 'mm' },
          { ctl: 'slider', key: 'SegmentTally', label: 'Segments', min: 6, max: 128, fmt: 0, unit: 'ct' },
          { ctl: 'boolean', key: 'CappedEnds', label: 'Cap ends' }
        ]});
        cards.push(TRANSFORM, APPEARANCE);
        break;
      case 'sphere':
        cards.push({ title: 'Sphere', fields: [
          { ctl: 'scalar', key: 'Radius', label: 'Radius', step: 0.05, fmt: 2, unit: 'mm' },
          { ctl: 'slider', key: 'SegmentTally', label: 'Segments', min: 6, max: 128, fmt: 0, unit: 'ct' },
          { ctl: 'slider', key: 'RingTally', label: 'Rings', min: 3, max: 64, fmt: 0, unit: 'ct' }
        ]});
        cards.push(TRANSFORM, APPEARANCE);
        break;
      // Provide basic fallbacks for others
      default:
        cards.push(TRANSFORM, APPEARANCE);
    }
    return cards;
  };

  const REVISION_CLASS: Record<string, { glyph: string, label: string, tone: string }> = {
    start:     { glyph: 'solid',      label: 'Start',     tone: '' },
    feature:   { glyph: 'solid',      label: 'Feature',   tone: 'text-[#fbbf24]' },
    param:     { glyph: 'cylinder',   label: 'Params',    tone: 'text-[#fbbf24]' },
    sketch:    { glyph: 'sketch',     label: 'Sketch',    tone: 'text-[#c084fc]' },
    transform: { glyph: 'loft',       label: 'Relocate',  tone: 'text-[#c084fc]' },
    body:      { glyph: 'folder',     label: 'Group',     tone: 'text-[#7dd3fc]' },
    add:       { glyph: 'scene',      label: 'Create',    tone: 'text-[#c084fc]' },
    edit:      { glyph: 'revolve',    label: 'Edit',      tone: 'text-[#7dd3fc]' },
    drop:      { glyph: 'cone',       label: 'Drop',      tone: 'text-[#fbbf24]' }
  };

  const REVISION_HUE: Record<string, string> = {
    start: '#7ec8ff', feature: '#ffb24d', param: '#4fd18b', sketch: '#37d6d6', transform: '#5b8cff',
    body: '#b98bff', add: '#7ec8ff', edit: '#c99b6a', drop: '#ff6b6b'
  };

  const CLASSIFICATION_ABBR: Record<string, string> = {
    scene: 'PT', folder: 'GR', solid: 'SO', cylinder: 'CY', sphere: 'SP',
    cone: 'CO', sketch: 'SK', revolve: 'RV', loft: 'LO'
  };

  const renderHistory = () => {
    if (!entry) return null;
    
    // Get all descendant tokens recursively to collect all nested histories
    const getDescendantTokens = (ent: RecordEntry): string[] => {
      let tokens = [ent.token];
      if (ent.nested) {
        ent.nested.forEach(child => {
          tokens = tokens.concat(getDescendantTokens(child));
        });
      }
      return tokens;
    };
    
    const relevantTokens = getDescendantTokens(entry);
    
    const hasAnyRevision = revisions.some(r => relevantTokens.includes(r.token));
    if (!hasAnyRevision) {
      return <div className="p-4 text-center text-[var(--faint)] text-[11.5px]">No history events found for this selection or its children.</div>;
    }

    return (
      <div className="flex flex-col px-0 py-2 gap-4">
        {relevantTokens.map(token => {
          const tokenRevisions = revisions.filter(r => r.token === token).sort((a, b) => a.date.getTime() - b.date.getTime());
          if (tokenRevisions.length === 0) return null;
          
          const record = searchToken(token, records);
          if (!record) return null;

          const hue = hueOf(record.classification);
          const isCollapsed = collapsedHistory[token];
          const abbr = CLASSIFICATION_ABBR[record.classification] || 'OB';

          return (
            <div key={token} className="flex flex-col">
              {/* Header */}
              <div 
                className="flex items-center gap-3 h-[32px] px-[12px] mb-[4px] cursor-pointer group"
                onClick={() => toggleHistoryCard(token)}
              >
                <div 
                  className="w-[20px] h-[20px] rounded-[5px] flex items-center justify-center text-[var(--desk)] shadow-sm flex-none"
                  style={{ backgroundColor: hue }}
                >
                  <ClassificationIcon cls={record.classification} size={12} customHue="white" />
                </div>
                <span className="text-[12.5px] font-semibold text-[var(--ink)] group-hover:text-white transition-colors truncate">{record.name}</span>
                <span className="text-[9.5px] font-bold tracking-wide uppercase text-[var(--muted)] px-1.5 py-0.5 rounded bg-[var(--menu)] border border-[var(--hair)]">{CLASSIFICATION_LABEL[record.classification]}</span>
                <span className="flex-1" />
                <span className="text-[10px] font-medium text-[var(--muted)]">{tokenRevisions.length} ops</span>
                <span className="text-[var(--faint)] transition-transform duration-200" style={{ transform: isCollapsed ? 'rotate(-90deg)' : 'none' }}>
                  <ChevronDown size={14} />
                </span>
              </div>

              {/* Revisions Stack */}
              <div className="grid transition-[grid-template-rows] duration-300 ease-in-out" style={{ gridTemplateRows: isCollapsed ? '0fr' : '1fr' }}>
                <div className="overflow-hidden min-h-0">
                  <div className="flex flex-col">
                    {tokenRevisions.map((rev, i) => {
                      const isFirst = i === 0;
                      const isLast = i === tokenRevisions.length - 1;
                      const spec = REVISION_CLASS[rev.category] || REVISION_CLASS.edit;
                      const isRevExpanded = expandedRevisions[rev.id];

                      return (
                        <div key={rev.id} className="flex relative group">
                          {/* bubble */}
                          <div className="w-[32px] flex-none pt-[7px] flex justify-center">
                            <div className="w-[25px] h-[25px] rounded-full flex items-center justify-center text-[10px] font-bold tracking-[-0.02em] transition-all group-hover:scale-110" style={{ background: hue, color: "#fff" }}>
                              {i.toString().padStart(2, '0')}
                            </div>
                          </div>
                          
                          {/* spine */}
                          <div className="w-[15px] flex-none flex justify-center relative">
                            <div className="absolute w-[6px] top-0 bottom-0 transition-colors" style={{ background: hue, top: isFirst ? '19px' : 0, borderTopLeftRadius: isFirst ? '4px' : 0, borderTopRightRadius: isFirst ? '4px' : 0, bottom: isLast ? 'auto' : 0, height: isLast ? '19px' : 'auto', borderBottomLeftRadius: isLast ? '4px' : 0, borderBottomRightRadius: isLast ? '4px' : 0 }}></div>
                            <div className="absolute top-[19px] w-[7px] h-[7px] rounded-full bg-white shadow-[0_0_0_3px_var(--menu-2)] -translate-y-1/2 z-10"></div>
                          </div>

                          {/* card col */}
                          <div className="flex-1 min-w-0 pb-[4px] pl-[8px] pr-[8px]">
                              
                              <div 
                                className={`flex items-center gap-[7px] px-[8px] cursor-pointer relative transition-colors border bg-[var(--tile)] ${isRevExpanded ? 'rounded-t-[8px] border-[var(--accent)] bg-[var(--accent-soft)] border-b-transparent' : 'rounded-[8px] border-[var(--hair)] hover:bg-[var(--tile-hi)]'}`}
                                style={{ minHeight: '44px' }}
                                onClick={() => toggleRevisionCard(rev.id)}
                              >
                                <div className="flex-1 min-w-0 py-2">
                                  <div className="flex items-baseline gap-2">
                                    <span className="text-[12.5px] text-[var(--text-value)] font-semibold truncate leading-[1.2] block">{rev.title}</span>
                                  </div>
                                  {rev.subtitle && (
                                    <div className="text-[10px] text-[var(--muted)] mt-[2px] leading-[1.3] whitespace-nowrap overflow-hidden text-ellipsis flex items-center gap-1.5">
                                      <ClassificationIcon cls={spec.glyph} size={11} customHue="currentColor" />
                                      <span className="truncate">{rev.subtitle}</span>
                                    </div>
                                  )}
                                </div>
                                <span className="text-[10px] text-[var(--faint)] font-mono flex-none flex items-center gap-1.5">
                                  {rev.date.toLocaleTimeString([], { hour: '2-digit', minute: '2-digit' })}
                                </span>
                                <span 
                                  className={`w-[18px] h-[18px] flex-none rounded-[6px] flex items-center justify-center cursor-pointer text-[#8a8a8a] hover:bg-[#1a1a1a] hover:text-[#ededed] transition-all ${isRevExpanded ? 'rotate-180' : ''}`}
                                >
                                  <ChevronDown size={14} />
                                </span>
                              </div>

                              {/* fold */}
                              <div className={`grid transition-[grid-template-rows] duration-[270ms] ease-[cubic-bezier(.4,0,.2,1)] -mt-[1px]`} style={{gridTemplateRows: isRevExpanded ? '1fr' : '0fr'}}>
                                <div className="overflow-hidden min-h-0">
                                  <div className="p-[7px_8px_8px] border border-t-0 border-[var(--accent)] bg-[var(--accent-soft)] rounded-b-[8px]">
                                    
                                    <div className="flex items-center justify-between text-[10px] text-[var(--muted)] mb-2">
                                      <span>By <strong className="font-medium text-[var(--text-value)]">{rev.author || 'System'}</strong></span>
                                      <span>{rev.date.toLocaleDateString()}</span>
                                    </div>
                                    
                                    <div className="bg-[var(--menu-2)] p-2 rounded-[8px] border border-[var(--hair)] text-[11.5px] text-[var(--ink)] leading-relaxed flex flex-col gap-1 mb-2">
                                      <div className="text-[9px] uppercase font-bold text-[var(--faint)] tracking-wider">Comment</div>
                                      <textarea
                                        className="w-full bg-transparent border-none outline-none resize-none placeholder-[var(--faint)] text-[var(--text-value)]"
                                        placeholder="Add a comment..."
                                        defaultValue={rev.comment || ''}
                                        onBlur={(e) => updateRevision(rev.id, { comment: e.target.value })}
                                        rows={rev.comment ? Math.max(2, rev.comment.split('\n').length) : 1}
                                      />
                                    </div>
                                    
                                    {rev.editValue !== undefined && (
                                      <div className="flex items-center gap-3 bg-[var(--menu-2)] p-2 rounded-[8px] border border-[var(--hair)]">
                                        <span className="text-[10px] font-medium text-[var(--muted)] w-[40px]">Value</span>
                                        <input 
                                          type="text" 
                                          defaultValue={rev.editValue} 
                                          onBlur={(e) => updateRevision(rev.id, { editValue: e.target.value })}
                                          className="flex-1 bg-[var(--menu)] border border-[var(--hair)] rounded px-2 py-1 text-[11px] text-[var(--text-value)] focus:outline-none focus:border-[var(--accent)] transition-colors"
                                        />
                                      </div>
                                    )}

                                  </div>
                                </div>
                              </div>
                          </div>
                        </div>
                      );
                    })}
                  </div>
                </div>
              </div>
            </div>
          );
        })}
      </div>
    );
  };

  const cards = getCards();

  return (
    <div className="flex flex-col h-full overflow-hidden bg-[var(--menu-2)]">
      {/* Header */}
      <div className="flex-none flex items-center h-[46px] px-[10px] bg-[var(--menu-2)] border-b border-[var(--hair)]">
        <span className="w-[24px] h-[24px] flex-none bg-black rounded-md flex items-center justify-center text-white mr-2">
          <ClassificationIcon cls={entry.classification} size={16} />
        </span>
        <div className="flex-1 min-w-0 text-[12.5px] font-semibold leading-tight">
          <div className="whitespace-nowrap overflow-hidden text-ellipsis">{entry.name}</div>
          <div className="text-[10px] font-normal tracking-wide mt-[2px]" style={{ color: hue }}>{CLASSIFICATION_LABEL[entry.classification]}</div>
        </div>
        {onBack && (
          <button className="flex-none flex items-center gap-1.5 px-2 h-7 rounded-md text-[11px] font-medium text-[var(--muted)] hover:text-[var(--ink)] hover:bg-[var(--tile-hi)] transition-colors ml-2" onClick={onBack}>
            <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" className="w-3.5 h-3.5"><path d="M15 18l-6-6 6-6"/></svg>
            Back to scene directory
          </button>
        )}
      </div>

      {/* Carousel Tabs */}
      <div className="flex-none flex items-stretch h-[31px] px-[6px] bg-[var(--menu-2)] border-b border-[var(--hair)]">
        <button className={`flex-1 font-medium text-[11.5px] border-b-2 transition-colors ${carouselMode === 'properties' ? 'text-[var(--ink)] border-[var(--accent)]' : 'text-[var(--muted)] border-transparent'}`} onClick={() => setCarouselMode('properties')}>Properties</button>
        <button className={`flex-1 font-medium text-[11.5px] border-b-2 transition-colors ${carouselMode === 'history' ? 'text-[var(--ink)] border-[var(--accent)]' : 'text-[var(--muted)] border-transparent'}`} onClick={() => setCarouselMode('history')}>History</button>
      </div>

      {/* Body */}
      <div className="flex-1 min-h-0 overflow-hidden relative">
        <div 
          className="absolute inset-0 flex transition-transform duration-300 ease-[cubic-bezier(.5,.05,.2,1)]"
          style={{ transform: carouselMode === 'properties' ? 'translateX(0)' : 'translateX(-100%)' }}
        >
          {/* Properties Panel */}
          <div className="w-full h-full flex-none overflow-y-auto p-[7px] pb-3 pr-[4px]">
            {cards.map(c => {
              const isCollapsed = collapsedCards[c.title];
              return (
                <div key={c.title} className="bg-[#0a0a0b] border border-[var(--hair)] rounded-[var(--r-tile)] mb-[6px] overflow-hidden">
                  <div className="flex items-center gap-2 h-[31px] px-[10px] text-[10.5px] font-semibold uppercase tracking-wide text-[var(--muted)] cursor-pointer bg-[var(--menu-2)] border-b border-transparent hover:text-[var(--ink)] transition-colors" onClick={() => toggleCard(c.title)} style={!isCollapsed ? { borderBottomColor: 'var(--hair)' } : {}}>
                    <span className="flex-none text-[var(--faint)] transition-transform duration-200" style={{ transform: isCollapsed ? 'rotate(-90deg)' : 'none' }}>
                      <ChevronDown size={14} />
                    </span>
                    {c.title}
                    <span className="ml-auto text-[9.5px] text-[var(--faint)] tracking-normal">{c.fields.length}</span>
                  </div>
                  <div className={`grid transition-[grid-template-rows] duration-200 ease-in-out`} style={{ gridTemplateRows: isCollapsed ? '0fr' : '1fr' }}>
                    <div className="overflow-hidden min-h-0">
                      <div className="p-2.5 flex flex-col gap-2">
                        {renderFields(c.fields)}
                      </div>
                    </div>
                  </div>
                </div>
              );
            })}
          </div>

          {/* History Panel */}
          <div className="w-full h-full flex-none overflow-y-auto p-[7px] pb-3 pr-[4px]">
            {renderHistory()}
          </div>
        </div>
      </div>

      {/* Footer */}
      <div className="flex-none flex items-center gap-[7px] h-[26px] px-[10px] bg-[var(--menu-2)] border-t border-[var(--hair)] text-[10px] text-[var(--muted)]">
        <span className="w-2 h-2 rounded-sm" style={{ background: hue }} />
        <span className="text-[var(--ink)] font-semibold">{cards.reduce((acc, c) => acc + c.fields.length, 0)}</span> fields
      </div>
    </div>
  );
}
