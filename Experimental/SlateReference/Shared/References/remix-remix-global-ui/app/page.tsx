'use client';

import React, { useState, useEffect } from 'react';
import { AppProvider, useAppState, RecordEntry } from '@/lib/store';
import { DirectoryTree } from '@/components/DirectoryPane';
import { PropertiesPane } from '@/components/Inspector';
import { MetadataPane } from '@/components/MetadataPane';
import { LayersPane, LayerInspectorPane } from '@/components/TexturePaint';
import { GameEngineOutliner, GamePropertiesPane } from '@/components/GameOutliner';
import { ClassificationIcon, hueOf } from '@/lib/icons';

function WorkspaceLayout() {
  const { selection, records } = useAppState();
  const [menuOpen, setMenuOpen] = useState(false);
  const [showInspector, setShowInspector] = useState(false);
  const [isDocked, setIsDocked] = useState(false);
  const [workspaceMode, setWorkspaceMode] = useState<'drafting' | 'texture' | 'game'>('game');

  const selectedTokens = Array.from(selection);
  let activeEntry: RecordEntry | null = null;
  if (selectedTokens.length === 1) {
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
    activeEntry = search(records);
  }

  useEffect(() => {
    const handleKeyDown = (e: KeyboardEvent) => {
      if (document.activeElement?.tagName === 'INPUT') return;
      if (e.key === 'Tab') {
        e.preventDefault();
        if (isDocked) {
          setShowInspector(prev => !prev);
        } else {
          if (!menuOpen) {
            setMenuOpen(true);
          } else {
            setShowInspector(prev => !prev);
          }
        }
      }
      if (e.key === 'Escape' && menuOpen && !isDocked) {
        if (showInspector) {
          setShowInspector(false);
        } else {
          setMenuOpen(false);
        }
      }
    };
    window.addEventListener('keydown', handleKeyDown);
    return () => window.removeEventListener('keydown', handleKeyDown);
  }, [menuOpen, showInspector, isDocked]);

  const [activeLayerId, setActiveLayerId] = useState<number>(1);
  const [activeTarget, setActiveTarget] = useState<'layer'|'mask'>('layer');
  const [activeGameId, setActiveGameId] = useState<string | null>('g_03');

  const handleInspectLayer = (id: number, target: 'layer'|'mask') => {
    setActiveLayerId(id);
    setActiveTarget(target);
    setShowInspector(true);
  };

  const handleInspectGame = (id: string) => {
    setActiveGameId(id);
    setShowInspector(true);
  };

  const inspectorContent = (
    <div className="h-full overflow-hidden w-full relative">
      <div className={`flex w-[200%] h-full transition-transform duration-300 ease-[cubic-bezier(.5,.05,.2,1)] ${showInspector ? '-translate-x-1/2' : ''}`}>
        
        {/* Slide 1: Directory / Layers / Game */}
        <div className="w-1/2 h-full">
          {workspaceMode === 'drafting' ? (
            <div className="h-full grid grid-cols-[350px_minmax(0,1fr)]">
              <DirectoryTree onInspect={() => setShowInspector(true)} />
              <div className="flex flex-col bg-[var(--menu-2)]">
                <div className="flex-none flex items-center h-[46px] px-[10px] bg-[var(--menu-2)] border-b border-[var(--hair)] cursor-pointer hover:bg-[#292930] transition-colors" onClick={() => setShowInspector(true)}>
                  <span className="w-6 h-6 flex-none bg-black rounded-md flex items-center justify-center text-white mr-2">
                    <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="1.6" className="w-[17px] h-[17px]"><path d="M12 2.5v3M12 18.5v3M2.5 12h3M18.5 12h3"/><circle cx="12" cy="12" r="7.5"/></svg>
                  </span>
                  <span className="flex-1 text-[12.5px] font-semibold">Properties & Actions</span>
                  <span className="w-[22px] h-[22px] flex items-center justify-center text-[var(--muted)] rounded-md hover:text-[var(--ink)] hover:bg-[var(--tile-hi)] transition-colors">
                    <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="1.8" className="w-[13px] h-[13px]"><path d="M8 4l8 8-8 8"/></svg>
                  </span>
                </div>
                <MetadataPane onInspect={() => setShowInspector(true)} />
              </div>
            </div>
          ) : workspaceMode === 'texture' ? (
            <LayersPane 
              activeLayerId={activeLayerId} 
              activeTarget={activeTarget}
              onSelect={(id, target) => { setActiveLayerId(id); setActiveTarget(target); }}
              onInspect={(id, target) => handleInspectLayer(id, target)} 
            />
          ) : (
            <div className="h-full grid grid-cols-[350px_minmax(0,1fr)]">
              <GameEngineOutliner
                activeId={activeGameId}
                onSelect={(id) => setActiveGameId(id)}
                onInspect={() => setShowInspector(true)}
              />
              <div className="flex flex-col bg-[var(--menu-2)] items-center justify-center">
                <div className="p-6 text-center text-[var(--faint)] text-[11.5px] max-w-[250px]">
                  Select an entity in the Outliner and press Tab or double-click to view its properties in the Inspector slide.
                </div>
              </div>
            </div>
          )}
        </div>

        {/* Slide 2: Properties / Texture Channels / Game Props */}
        <div className="w-1/2 h-full flex flex-col bg-[var(--menu-2)]">
          {workspaceMode === 'drafting' ? (
            <PropertiesPane onBack={() => setShowInspector(false)} />
          ) : workspaceMode === 'texture' ? (
            <LayerInspectorPane 
              layerId={activeLayerId} 
              target={activeTarget} 
              onTargetChange={setActiveTarget}
              onBack={() => setShowInspector(false)} 
            />
          ) : (
            <GamePropertiesPane
              activeId={activeGameId}
              onBack={() => setShowInspector(false)}
            />
          )}
        </div>
      </div>
    </div>
  );

  return (
    <div className="h-full flex flex-col overflow-hidden bg-[var(--desk)] text-[var(--ink)] font-sans">
      {/* Top Bar */}
      <div className="flex-none h-[36px] flex items-center gap-3 px-[14px] bg-[var(--menu-2)] border-b border-[var(--hair)] z-20 relative">
        <div className="w-[18px] h-[18px] text-[var(--accent)] flex items-center justify-center">
          <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="1.6" strokeLinecap="round" strokeLinejoin="round">
            <path d="M12 3l8 4.5v9L12 21l-8-4.5v-9z"/><path d="M4 7.5l8 4.5 8-4.5M12 12v9" strokeWidth="1.3"/>
          </svg>
        </div>
        <span className="text-[12.5px] font-semibold tracking-wide">{workspaceMode === 'drafting' ? 'DraftingWorkspace' : workspaceMode === 'texture' ? 'Texture Paint' : 'World Editor'}</span>
        <span className="text-[11px] text-[var(--faint)]">{workspaceMode === 'game' ? 'Level_01_City.map' : 'Bracket_Rev4.wsdoc'}</span>
        <div className="flex-1" />
        <span className="text-[10.5px] text-[var(--muted)] bg-[var(--menu)] border border-[var(--hair)] rounded-full px-2.5 py-1">
          <b className="text-[var(--ink)] font-semibold">Tab</b> &nbsp;summon inspector
        </span>
      </div>

      {/* Main Viewport + Left/Right Sidebars */}
      <div className="flex-1 min-h-0 flex relative bg-[var(--desk)] overflow-hidden">
        
        {/* Options Menu (Left) */}
        <div className="w-[220px] flex-none bg-[var(--menu-2)] border-r border-[var(--hair-strong)] flex flex-col shadow-[1px_0_10px_rgba(0,0,0,0.2)] z-10">
          <div className="h-[46px] flex-none flex items-center px-4 border-b border-[var(--hair)] text-[12.5px] font-semibold tracking-wide">
            Options
          </div>
          <div className="p-4 flex flex-col gap-6">
            <div>
              <div className="flex items-center justify-between mb-2">
                <span className="text-[12.5px] text-[var(--muted)]">Dock Inspector</span>
                <div className={`switch ${isDocked ? 'on' : ''}`} onClick={() => setIsDocked(!isDocked)}>
                  <div className="nub" />
                </div>
              </div>
              <div className="text-[11px] text-[var(--faint)] leading-relaxed">
                {isDocked ? "Inspector is docked to the right side of the screen." : "Inspector is hidden. Press Tab to summon it."}
              </div>
            </div>
            
            <div>
              <div className="flex items-center justify-between mb-2">
                <span className="text-[12.5px] text-[var(--muted)]">Workspace Mode</span>
              </div>
              <div className="flex flex-col gap-2 mt-3">
                <button 
                  className={`h-8 rounded-md text-[11px] font-medium border transition-colors ${workspaceMode === 'drafting' ? 'bg-[var(--accent-soft)] border-[var(--accent)] text-[var(--ink)]' : 'bg-[var(--tile)] border-[var(--hair)] text-[var(--muted)] hover:border-[#444]'}`}
                  onClick={() => setWorkspaceMode('drafting')}
                >Drafting</button>
                <button 
                  className={`h-8 rounded-md text-[11px] font-medium border transition-colors ${workspaceMode === 'texture' ? 'bg-[var(--accent-soft)] border-[var(--accent)] text-[var(--ink)]' : 'bg-[var(--tile)] border-[var(--hair)] text-[var(--muted)] hover:border-[#444]'}`}
                  onClick={() => setWorkspaceMode('texture')}
                >Texture Paint</button>
                <button 
                  className={`h-8 rounded-md text-[11px] font-medium border transition-colors ${workspaceMode === 'game' ? 'bg-[var(--accent-soft)] border-[var(--accent)] text-[var(--ink)]' : 'bg-[var(--tile)] border-[var(--hair)] text-[var(--muted)] hover:border-[#444]'}`}
                  onClick={() => setWorkspaceMode('game')}
                >Game Editor</button>
              </div>
            </div>
          </div>
        </div>

        {/* Viewport Area */}
        <div className="flex-1 relative overflow-hidden flex flex-col">
          {/* Weave grid background */}
          <div className="absolute inset-0 pointer-events-none" style={{
            backgroundImage: `
              linear-gradient(rgba(255,255,255,.028) 1px, transparent 1px),
              linear-gradient(90deg, rgba(255,255,255,.028) 1px, transparent 1px),
              linear-gradient(rgba(255,255,255,.055) 1px, transparent 1px),
              linear-gradient(90deg, rgba(255,255,255,.055) 1px, transparent 1px)`,
            backgroundSize: '28px 28px, 28px 28px, 140px 140px, 140px 140px'
          }} />
          <div className="absolute inset-0 pointer-events-none" style={{
            background: 'radial-gradient(ellipse at 50% 45%, transparent 40%, rgba(0,0,0,.55) 100%)'
          }} />

          <div className="absolute left-1/2 top-1/2 -translate-x-1/2 -translate-y-1/2 text-center text-[var(--faint)] text-[12px] leading-relaxed pointer-events-none z-0">
            press <kbd className="inline-block bg-[var(--menu)] text-[var(--ink)] border border-[var(--hair-strong)] border-b-2 rounded-md px-2 py-0.5 font-semibold text-[11px] mx-0.5">Tab</kbd> to {isDocked ? (workspaceMode === 'drafting' ? 'slide through properties' : workspaceMode === 'texture' ? 'slide through channels' : 'slide through components') : (workspaceMode === 'drafting' ? 'summon the scene directory' : workspaceMode === 'texture' ? 'summon layers' : 'summon the outliner')}<br/>
            {!isDocked && <><kbd className="inline-block bg-[var(--menu)] text-[var(--ink)] border border-[var(--hair-strong)] border-b-2 rounded-md px-2 py-0.5 font-semibold text-[11px] mx-0.5">Tab</kbd> again slides through to {workspaceMode === 'drafting' ? 'properties' : workspaceMode === 'texture' ? 'channels' : 'components'}</>}
          </div>

          <div className="absolute left-0 right-0 bottom-0 h-[28px] flex items-center gap-[9px] px-[13px] text-[10.5px] text-[var(--faint)] pointer-events-none z-0" style={{ background: 'linear-gradient(transparent, rgba(0,0,0,.5))' }}>
            <span>Orbit LMB</span><span className="text-[var(--value-unit)]">·</span>
            <span>Pan MMB</span><span className="text-[var(--value-unit)]">·</span>
            <span>Zoom Wheel</span><span className="text-[var(--value-unit)]">·</span>
            <span>Inspector Tab</span>
          </div>
        </div>

        {/* Docked Inspector (Right) */}
        {isDocked && (
          <div className="w-[700px] flex-none bg-[var(--menu)] border-l border-[var(--hair-strong)] shadow-[-1px_0_10px_rgba(0,0,0,0.2)] z-10 relative">
            {inspectorContent}
          </div>
        )}
      </div>

      {/* Summon Menu Veil */}
      {!isDocked && menuOpen && (
        <div className="fixed inset-0 z-[880] bg-black/30 animate-[veilIn_0.14s_ease_both]" onClick={() => setMenuOpen(false)} />
      )}

      {/* Summon Menu (Modal) */}
      {!isDocked && menuOpen && (
        <div 
          className="fixed z-[900] w-[700px] h-[400px] bg-[var(--menu)] border border-[var(--hair-strong)] rounded-[var(--r-menu)] shadow-[0_30px_80px_rgba(0,0,0,.66),0_1px_0_rgba(255,255,255,.04)_inset] overflow-hidden origin-top-left animate-[pop_0.14s_cubic-bezier(0.16,1,0.3,1)]"
          style={{ left: '50%', top: '50%', transform: 'translate(-50%, -50%)' }}
        >
          {inspectorContent}
        </div>
      )}
    </div>
  );
}

export default function WorkspacePage() {
  return (
    <AppProvider>
      <WorkspaceLayout />
    </AppProvider>
  );
}
