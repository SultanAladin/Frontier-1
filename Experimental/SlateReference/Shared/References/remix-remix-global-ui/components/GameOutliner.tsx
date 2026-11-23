'use client';
import React, { useState } from 'react';
import { ChevronDown, Eye, EyeOff, Search, Box, Camera, Lightbulb, Volume2, Sparkles, Crosshair, Folder, Code, Settings } from 'lucide-react';
import { ValueSlider, Slider, Dropdown, DropdownEntry, ColorEntry, BooleanEntry, TextEntry, VectorEntry } from '@/components/Controls';

export type GameNodeType = 'level' | 'folder' | 'actor' | 'camera' | 'light' | 'audio' | 'particle' | 'trigger' | 'script';

export interface GameNode {
  id: string;
  name: string;
  type: GameNodeType;
  expanded?: boolean;
  hidden?: boolean;
  children?: GameNode[];
  props?: any;
}

const initialGameGraph: GameNode[] = [
  { id: 'g_01', name: 'Level_01_City', type: 'level', expanded: true, children: [
    { id: 'g_02', name: 'Lighting', type: 'folder', expanded: true, children: [
      { id: 'g_03', name: 'Directional Light (Sun)', type: 'light', props: { intensity: 100000, color: [255, 240, 220], castShadows: true } },
      { id: 'g_04', name: 'Sky Atmosphere', type: 'light', props: { intensity: 10000, color: [180, 200, 255], castShadows: false } },
    ]},
    { id: 'g_05', name: 'Player_Start', type: 'trigger', props: { radius: 50, tag: 'Spawn' } },
    { id: 'g_06', name: 'Main Camera', type: 'camera', props: { fov: 90, nearClip: 0.1, farClip: 10000, projection: 0 } },
    { id: 'g_07', name: 'Environment', type: 'folder', expanded: true, children: [
      { id: 'g_08', name: 'Building_A_Prefab', type: 'actor', props: { position: [0, 0, 0], rotation: [0, 0, 0], scale: [1, 1, 1], static: true } },
      { id: 'g_09', name: 'Building_B_Prefab', type: 'actor', props: { position: [500, 0, 200], rotation: [0, 90, 0], scale: [1, 1, 1], static: true } },
      { id: 'g_10', name: 'Street_Prop_FireHydrant', type: 'actor', props: { position: [120, 0, 45], rotation: [0, 0, 0], scale: [1, 1, 1], static: true } },
    ]},
    { id: 'g_11', name: 'Systems', type: 'folder', expanded: true, children: [
      { id: 'g_12', name: 'GameManager', type: 'script', props: { state: 'Playing', difficulty: 1 } },
      { id: 'g_13', name: 'Ambient_City_Noise', type: 'audio', props: { volume: 0.8, loop: true, spatial: false } },
      { id: 'g_14', name: 'Dust_Motes_VFX', type: 'particle', props: { emitRate: 50, lifeTime: 5.0, looping: true } },
    ]}
  ]}
];

const ICONS: Record<string, React.ReactNode> = {
  level: <Box size={14} />,
  folder: <Folder size={14} />,
  actor: <Box size={14} />,
  camera: <Camera size={14} />,
  light: <Lightbulb size={14} />,
  audio: <Volume2 size={14} />,
  particle: <Sparkles size={14} />,
  trigger: <Crosshair size={14} />,
  script: <Code size={14} />
};

const COLORS: Record<string, string> = {
  level: '#eab308',
  folder: '#8a8a8a',
  actor: '#3b82f6',
  camera: '#ec4899',
  light: '#f59e0b',
  audio: '#8b5cf6',
  particle: '#10b981',
  trigger: '#ef4444',
  script: '#06b6d4'
};

export function GameEngineOutliner({ activeId, onSelect, onInspect }: { activeId: string | null, onSelect: (id: string) => void, onInspect: () => void }) {
  const [graph, setGraph] = useState(initialGameGraph);
  const [filterText, setFilterText] = useState('');

  const toggleExpand = (id: string, e: React.MouseEvent) => {
    e.stopPropagation();
    setGraph(prev => {
      const next = JSON.parse(JSON.stringify(prev));
      const toggle = (nodes: GameNode[]) => {
        for (const n of nodes) {
          if (n.id === id) { n.expanded = !n.expanded; return true; }
          if (n.children && toggle(n.children)) return true;
        }
        return false;
      };
      toggle(next);
      return next;
    });
  };

  const toggleVisibility = (id: string, e: React.MouseEvent, currentHidden: boolean) => {
    e.stopPropagation();
    setGraph(prev => {
      const next = JSON.parse(JSON.stringify(prev));
      const setHidden = (nodes: GameNode[]) => {
        for (const n of nodes) {
          if (n.id === id) {
            const applyHidden = (node: GameNode) => {
              node.hidden = !currentHidden;
              if (node.children) node.children.forEach(applyHidden);
            };
            applyHidden(n);
            return true;
          }
          if (n.children && setHidden(n.children)) return true;
        }
        return false;
      };
      setHidden(next);
      return next;
    });
  };

  const isRetained = (node: GameNode): boolean => {
    if (!filterText) return true;
    if (node.name.toLowerCase().includes(filterText.toLowerCase())) return true;
    if (node.children) return node.children.some(isRetained);
    return false;
  };

  const countNodes = (nodes: GameNode[]): number => {
    let sum = nodes.length;
    nodes.forEach(n => sum += (n.children ? countNodes(n.children) : 0));
    return sum;
  };

  const renderRow = (node: GameNode, depth: number) => {
    if (filterText && !isRetained(node)) return null;
    const isBranch = !!node.children && node.children.length > 0;
    const isExpanded = node.expanded || !!filterText;
    const isSelected = activeId === node.id;
    
    return (
      <React.Fragment key={node.id}>
        <div 
          className={`relative flex items-center gap-2 h-[32px] pr-[7px] rounded-md text-[12.5px] cursor-pointer transition-colors
            ${isSelected ? 'bg-[#1e40af33] text-[var(--ink)]' : 'text-[var(--muted)] hover:bg-[var(--row-hover)] hover:text-[var(--ink)]'}
            ${node.hidden ? 'opacity-50' : ''}`}
          style={{ paddingLeft: `${8 + depth * 15}px` }}
          onClick={() => onSelect(node.id)}
          onDoubleClick={onInspect}
        >
          {isSelected && (
            <div className="absolute left-[-7px] top-1/2 -translate-y-1/2 w-[3px] h-[15px] rounded-r-sm bg-[#3b82f6]" />
          )}
          
          <div className="w-[15px] h-[15px] flex-none flex items-center justify-center text-[var(--faint)] hover:text-[var(--ink)] transition-colors" onClick={(e) => isBranch && toggleExpand(node.id, e)}>
            {isBranch ? (
              <ChevronDown size={14} className={`transition-transform ${!isExpanded ? '-rotate-90' : ''}`} />
            ) : <span className="w-[14px]" />}
          </div>

          <div className="w-[18px] h-[18px] flex-none flex items-center justify-center" style={{ color: COLORS[node.type] || '#8a8a8a' }}>
            {ICONS[node.type] || <Box size={14} />}
          </div>

          <div className="flex-1 min-w-0 whitespace-nowrap overflow-hidden text-ellipsis">{node.name}</div>
          
          {isBranch && (
            <div className="text-[10px] text-[var(--faint)] font-mono flex-none">{node.children!.length}</div>
          )}

          <div className={`w-5 h-5 flex-none flex items-center justify-center rounded-sm text-[var(--faint)] hover:bg-[var(--tile-hi)] hover:text-[var(--ink)] transition-all ${node.hidden ? 'opacity-100' : 'opacity-0 group-hover:opacity-100'}`} onClick={(e) => toggleVisibility(node.id, e, !!node.hidden)}>
            {node.hidden ? <EyeOff size={14} /> : <Eye size={14} />}
          </div>
        </div>
        {isBranch && isExpanded && node.children!.map(child => renderRow(child, depth + 1))}
      </React.Fragment>
    );
  };

  return (
    <div className="flex flex-col h-full bg-[var(--menu)] border-r border-[var(--hair)]">
      {/* Header */}
      <div className="flex-none flex items-center gap-[10px] h-[46px] px-[10px] bg-[var(--menu)] border-b border-[var(--hair)]">
        <span className="w-6 h-6 flex-none bg-[#3b82f6] rounded-md flex items-center justify-center text-white">
          <Settings size={14} />
        </span>
        <div className="flex-1 min-w-0 text-[12.5px] font-semibold leading-tight">
          <div className="whitespace-nowrap overflow-hidden text-ellipsis">World Outliner</div>
          <div className="text-[10px] font-normal text-[var(--faint)] mt-[2px]">Level_01_City</div>
        </div>
      </div>

      {/* Search */}
      <div className="flex-none p-2 pb-1">
        <label className="flex items-center gap-2 h-[30px] px-2.5 bg-[var(--menu-2)] border border-[var(--hair)] rounded-md focus-within:border-[var(--outline)] transition-colors">
          <Search size={14} className="text-[var(--faint)]" />
          <input 
            type="text" 
            placeholder="Filter Entities…" 
            className="flex-1 bg-transparent border-none outline-none text-[12px] text-[var(--ink)] placeholder:text-[var(--faint)]"
            value={filterText}
            onChange={e => setFilterText(e.target.value)}
            spellCheck={false}
          />
        </label>
      </div>

      {/* Tree Body */}
      <div className="flex-1 min-h-0 overflow-y-auto px-2 pb-2 group">
        {graph.map(entry => renderRow(entry, 0))}
      </div>

      {/* Footer */}
      <div className="flex-none flex items-center gap-[7px] h-[26px] px-[10px] bg-[var(--menu-2)] border-t border-[var(--hair)] text-[10px] text-[var(--muted)]">
        <span className="font-mono text-[var(--ink)]">{countNodes(graph)}</span> entities
      </div>
    </div>
  );
}

export function GamePropertiesPane({ activeId, onBack }: { activeId: string | null, onBack?: () => void }) {
  const [graph, setGraph] = useState(initialGameGraph);

  const searchNode = (nodes: GameNode[], id: string): GameNode | null => {
    for (const n of nodes) {
      if (n.id === id) return n;
      if (n.children) {
        const found = searchNode(n.children, id);
        if (found) return found;
      }
    }
    return null;
  };

  const node = activeId ? searchNode(graph, activeId) : null;

  if (!node) {
    return (
      <div className="flex flex-col h-full overflow-hidden bg-[var(--menu-2)]">
        <div className="flex-none flex items-center h-[46px] px-[10px] bg-[var(--menu-2)] border-b border-[var(--hair)]">
          <span className="w-6 h-6 flex-none bg-[#111] rounded-md flex items-center justify-center text-[var(--faint)] mr-2">
            <Box size={14} />
          </span>
          <span className="flex-1 text-[12.5px] font-semibold">Nothing selected</span>
          {onBack && (
            <button className="flex-none flex items-center gap-1.5 px-2 h-7 rounded-md text-[11px] font-medium text-[var(--muted)] hover:text-[var(--ink)] hover:bg-[var(--tile-hi)] transition-colors ml-2" onClick={onBack}>
              <ChevronDown className="w-3.5 h-3.5 rotate-90"/> Back
            </button>
          )}
        </div>
        <div className="flex-1 p-6 text-center text-[var(--faint)] text-[11.5px]">Select an entity to inspect its components.</div>
      </div>
    );
  }

  const props = node.props || {};

  return (
    <div className="flex flex-col h-full overflow-hidden bg-[var(--menu-2)]">
      {/* Header */}
      <div className="flex-none flex items-center h-[46px] px-[10px] bg-[var(--menu-2)] border-b border-[var(--hair)]">
        <span className="w-[24px] h-[24px] flex-none bg-[#111] border border-[#222] rounded-md flex items-center justify-center mr-2 shadow-sm" style={{ color: COLORS[node.type] || '#8a8a8a' }}>
          {ICONS[node.type] || <Box size={14} />}
        </span>
        <div className="flex-1 min-w-0 text-[12.5px] font-semibold leading-tight">
          <div className="whitespace-nowrap overflow-hidden text-ellipsis">{node.name}</div>
          <div className="text-[10px] font-normal tracking-wide mt-[2px] capitalize" style={{ color: COLORS[node.type] || '#8a8a8a' }}>{node.type} Entity</div>
        </div>
        {onBack && (
          <button className="flex-none flex items-center gap-1.5 px-2 h-7 rounded-md text-[11px] font-medium text-[var(--muted)] hover:text-[var(--ink)] hover:bg-[var(--tile-hi)] transition-colors ml-2" onClick={onBack}>
            <ChevronDown className="w-3.5 h-3.5 rotate-90"/> Back
          </button>
        )}
      </div>

      <div className="flex-1 min-h-0 overflow-y-auto p-[7px] pb-3 pr-[4px]">
        {/* Transform Component (Common) */}
        {node.type !== 'level' && node.type !== 'folder' && node.type !== 'script' && (
          <div className="bg-[#0a0a0b] border border-[var(--hair)] rounded-[var(--r-tile)] mb-[6px] overflow-hidden">
            <div className="flex items-center gap-2 h-[31px] px-[10px] text-[10.5px] font-semibold uppercase tracking-wide text-[var(--muted)] bg-[var(--menu-2)] border-b border-[var(--hair)]">
              Transform
            </div>
            <div className="p-2.5 flex flex-col gap-2">
              <VectorEntry label="Position" value={props.position || [0,0,0]} step={1} fmt={2} onChange={()=>{}} />
              <VectorEntry label="Rotation" value={props.rotation || [0,0,0]} step={1} fmt={2} onChange={()=>{}} />
              <VectorEntry label="Scale" value={props.scale || [1,1,1]} step={0.1} fmt={2} onChange={()=>{}} />
            </div>
          </div>
        )}

        {/* Specific Components */}
        <div className="bg-[#0a0a0b] border border-[var(--hair)] rounded-[var(--r-tile)] mb-[6px] overflow-hidden">
          <div className="flex items-center gap-2 h-[31px] px-[10px] text-[10.5px] font-semibold uppercase tracking-wide text-[var(--muted)] bg-[var(--menu-2)] border-b border-[var(--hair)]">
            <span className="capitalize">{node.type}</span> Component
          </div>
          <div className="p-2.5 flex flex-col gap-2">
            {node.type === 'light' && (
              <>
                <ValueSlider label="Intensity" value={props.intensity || 0} min={0} max={150000} fmt={0} unit="lm" onChange={()=>{}} />
                <BooleanEntry label="Cast Shadows" value={props.castShadows ?? true} onChange={()=>{}} />
                <div className="grid grid-cols-[80px_minmax(0,1fr)] gap-x-[10px] items-center min-h-[32px]">
                  <div className="text-[11.5px] text-[var(--muted)]">Light Color</div>
                  <div className="h-[24px] rounded-[6px] border border-[var(--hair)]" style={{ background: `rgb(${(props.color||[255,255,255]).join(',')})`}} />
                </div>
              </>
            )}
            {node.type === 'camera' && (
              <>
                <DropdownEntry label="Projection" value={props.projection || 0} options={['Perspective', 'Orthographic']} onChange={()=>{}} />
                <ValueSlider label="Field of View" value={props.fov || 90} min={10} max={170} fmt={1} unit="°" onChange={()=>{}} />
                <ValueSlider label="Near Clip" value={props.nearClip || 0.1} min={0.01} max={10} fmt={2} unit="m" onChange={()=>{}} />
                <ValueSlider label="Far Clip" value={props.farClip || 10000} min={100} max={50000} fmt={0} unit="m" onChange={()=>{}} />
              </>
            )}
            {node.type === 'audio' && (
              <>
                <ValueSlider label="Volume" value={props.volume || 1} min={0} max={2} fmt={2} onChange={()=>{}} />
                <BooleanEntry label="Looping" value={props.loop ?? true} onChange={()=>{}} />
                <BooleanEntry label="Spatial 3D" value={props.spatial ?? true} onChange={()=>{}} />
              </>
            )}
            {node.type === 'particle' && (
              <>
                <ValueSlider label="Emit Rate" value={props.emitRate || 10} min={1} max={1000} fmt={0} unit="/s" onChange={()=>{}} />
                <ValueSlider label="Life Time" value={props.lifeTime || 1} min={0.1} max={20} fmt={2} unit="s" onChange={()=>{}} />
                <BooleanEntry label="Looping" value={props.looping ?? true} onChange={()=>{}} />
              </>
            )}
            {node.type === 'trigger' && (
              <>
                <TextEntry label="Event Tag" value={props.tag || ''} onChange={()=>{}} />
                <ValueSlider label="Radius" value={props.radius || 10} min={1} max={500} fmt={1} unit="m" onChange={()=>{}} />
              </>
            )}
            {node.type === 'script' && (
              <>
                <DropdownEntry label="State" value={props.state === 'Playing' ? 0 : 1} options={['Playing', 'Paused', 'Stopped']} onChange={()=>{}} />
                <ValueSlider label="Difficulty" value={props.difficulty || 1} min={1} max={5} fmt={0} onChange={()=>{}} />
              </>
            )}
            {node.type === 'actor' && (
              <>
                <BooleanEntry label="Static Mesh" value={props.static ?? true} onChange={()=>{}} />
                <BooleanEntry label="Simulate Physics" value={false} onChange={()=>{}} />
                <BooleanEntry label="Generate Overlaps" value={true} onChange={()=>{}} />
              </>
            )}
            {node.type === 'level' && (
              <>
                <TextEntry label="Level Name" value={node.name} onChange={()=>{}} />
                <BooleanEntry label="World Partition" value={true} onChange={()=>{}} />
              </>
            )}
            {node.type === 'folder' && (
              <>
                <TextEntry label="Folder Name" value={node.name} onChange={()=>{}} />
                <BooleanEntry label="Is Editor Only" value={false} onChange={()=>{}} />
              </>
            )}
          </div>
        </div>
      </div>
    </div>
  );
}
