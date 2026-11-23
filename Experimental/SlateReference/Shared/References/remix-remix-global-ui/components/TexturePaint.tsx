import React, { useState } from 'react';
import { Eye, EyeOff, PaintBucket, Plus, ChevronRight, Image as ImageIcon, Box, Search, Trash2, X } from 'lucide-react';
import { ValueSlider, Slider, Dropdown, ColorEntry } from '@/components/Controls';

const KINDS = [
  {Label:"Paint",     Tint:"#f97316", Summary:"Accepts brush strokes"},
  {Label:"Material",  Tint:"#8b5cf6", Summary:"Fills entire UV"},
  {Label:"Generator", Tint:"#10b981", Summary:"Procedural over the atlas"}
];

const BLEND_MODES = ["Normal", "Multiply", "Screen", "Overlay", "Add", "Darken", "Linear Dodge"];

// Move state out to module level so both panes can share it (mocking a store)
let mockLayers = [
  {
    id: 1, name: "Edge Wear", kind: 0, blend: "Multiply", opacity: 78, shown: true,
    paint: "#f97316", tag: "#eab308", channels: ["Base Color", "Roughness", "Metallic"],
    mask: { enabled: true, source: "Generator", tone: "#fff", strength: 92, invert: false, shown: true }
  },
  {
    id: 2, name: "Dirt Pass", kind: 1, blend: "Overlay", opacity: 45, shown: true,
    paint: "#8b5cf6", tag: "#ec4899", channels: ["Base Color", "Roughness"],
    mask: { enabled: true, source: "Paint", tone: "#fff", strength: 100, invert: true, shown: true }
  },
  {
    id: 3, name: "Scratches", kind: 0, blend: "Screen", opacity: 60, shown: false,
    paint: "#f97316", tag: "#06b6d4", channels: ["Base Color", "Bump"],
    mask: { enabled: false, source: "", tone: "#fff", strength: 100, invert: false, shown: true }
  },
  {
    id: 4, name: "Base Metal", kind: 1, blend: "Normal", opacity: 100, shown: true,
    paint: "#8b5cf6", tag: "#3b82f6", channels: ["Base Color", "Roughness", "Metallic", "Bump"],
    mask: { enabled: false, source: "", tone: "#fff", strength: 100, invert: false, shown: true }
  }
];

export function LayersPane({ 
  activeLayerId, 
  activeTarget, 
  onSelect, 
  onInspect 
}: { 
  activeLayerId: number, 
  activeTarget: 'layer' | 'mask',
  onSelect: (id: number, target: 'layer'|'mask') => void,
  onInspect: (id: number, target: 'layer'|'mask') => void
}) {
  const [layers, setLayers] = useState(mockLayers);
  const [expandedIds, setExpandedIds] = useState<number[]>([activeLayerId]);
  const [draggedId, setDraggedId] = useState<number | null>(null);
  const [dragOverId, setDragOverId] = useState<number | null>(null);

  // Sync back to module variable for the other pane
  React.useEffect(() => { mockLayers = layers; }, [layers]);

  const shownCount = layers.filter(l => l.shown).length;

  return (
    <div className="h-full flex flex-col bg-[#0b0b0b]">
      {/* Header */}
      <div className="flex-none flex items-center justify-between h-[46px] px-[10px] bg-[#121214] border-b border-[#1c1c1c]">
        <div className="flex items-center">
          <span className="w-6 h-6 flex-none bg-black border border-[#1c1c1c] rounded-md flex items-center justify-center text-[#8a8a8a] mr-2 shadow-[0_2px_4px_rgba(0,0,0,0.4)]">
            <LayersIcon size={14} />
          </span>
          <div>
            <div className="text-[12.5px] font-semibold leading-tight text-[var(--ink)]">Layer Stack</div>
            <div className="text-[10px] text-[#8a8a8a] leading-tight">Suzanne · one material + paint</div>
          </div>
        </div>
        <div className="flex items-center gap-[6px]">
          <span className="h-[20px] px-[6px] rounded-full bg-[#1b1b1b] border border-[#2a2a2a] text-[10px] font-semibold text-[#8a8a8a] flex items-center justify-center font-mono">{layers.length}</span>
        </div>
      </div>

      {/* Toolbar */}
      <div className="flex-none p-[7px]">
        <div className="flex gap-[5px]">
          <button 
            className="flex-1 h-[28px] rounded-[7px] border border-[#242424] bg-[#141414] flex items-center justify-center gap-[6px] text-[#8a8a8a] text-[11px] font-medium hover:border-[#3a3a3a] hover:text-[#ededed] transition-colors shadow-[0_1px_2px_rgba(0,0,0,0.3)]"
            onClick={() => {
              const NEW_LAYER_COLORS = ["#4a90e2", "#f59e0b", "#10b981", "#8b5cf6", "#ec4899", "#06b6d4", "#eab308"];
              const c = NEW_LAYER_COLORS[layers.length % NEW_LAYER_COLORS.length];
              const newLayer = {
                id: Date.now(), name: "New Layer", kind: 0, blend: "Normal", opacity: 100, shown: true,
                paint: c, tag: c, channels: ["Base Color"],
                mask: { enabled: false, source: "", tone: "#fff", strength: 100, invert: false, shown: true }
              };
              setLayers([newLayer, ...layers]);
            }}
          >
            <Plus size={13} /> Add layer
          </button>
        </div>
        <div className="relative mt-[5px]">
          <div className="absolute left-[8px] top-0 bottom-0 flex items-center text-[#5a5a5a] pointer-events-none"><Search size={12} /></div>
          <input type="text" placeholder="Filter layers..." className="w-full h-[26px] bg-transparent border border-transparent rounded-[6px] pl-[26px] pr-[8px] text-[11px] text-[#ededed] placeholder:text-[#4a4a4a] hover:bg-[#121214] hover:border-[#242424] focus:bg-[#121214] focus:border-[#4a90e2] focus:outline-none transition-all" />
        </div>
      </div>

      {/* Layers List */}
      <div className="flex-1 overflow-y-auto px-[3px]">
        {layers.map((l, idx) => {
          const kind = KINDS[l.kind];
          const isFirst = idx === 0;
          const isLast = idx === layers.length - 1;

          return (
            <div 
              key={l.id} 
              className={`flex relative group pb-[5px] transition-opacity ${draggedId === l.id ? 'opacity-40' : ''}`}
              draggable
              onDragStart={(e) => {
                setDraggedId(l.id);
                e.dataTransfer.effectAllowed = 'move';
                e.dataTransfer.setData('text/plain', l.id.toString());
              }}
              onDragOver={(e) => {
                e.preventDefault();
                if (draggedId !== l.id) setDragOverId(l.id);
              }}
              onDragLeave={() => {
                if (dragOverId === l.id) setDragOverId(null);
              }}
              onDrop={(e) => {
                e.preventDefault();
                if (draggedId !== null && draggedId !== l.id) {
                  setLayers(prev => {
                    const oldIdx = prev.findIndex(x => x.id === draggedId);
                    const newIdx = prev.findIndex(x => x.id === l.id);
                    const clone = [...prev];
                    const [item] = clone.splice(oldIdx, 1);
                    clone.splice(newIdx, 0, item);
                    return clone;
                  });
                }
                setDraggedId(null);
                setDragOverId(null);
              }}
              onDragEnd={() => {
                setDraggedId(null);
                setDragOverId(null);
              }}
            >
              {/* Spine */}
              <div className="w-[30px] flex-none flex justify-center relative ml-[2px]">
                <div className="absolute w-[3px] top-0 bottom-0 transition-colors" style={{ background: l.shown ? l.tag : "#1b1b1b", top: isFirst ? '22px' : 0, borderTopLeftRadius: isFirst ? '2px' : 0, borderTopRightRadius: isFirst ? '2px' : 0, bottom: isLast ? 'auto' : 0, height: isLast ? '22px' : 'auto', borderBottomLeftRadius: isLast ? '2px' : 0, borderBottomRightRadius: isLast ? '2px' : 0 }}></div>
                <div 
                  className="absolute top-[22px] w-[20px] h-[20px] rounded-full flex items-center justify-center text-white text-[9.5px] font-bold shadow-[0_0_0_3px_#0b0b0b] -translate-y-1/2 z-10 font-mono transition-colors"
                  style={{ backgroundColor: l.shown ? l.tag : "#2a2a2a", opacity: l.shown ? 1 : 0.4 }}
                >
                  {String(layers.length - idx).padStart(2, '0')}
                </div>
              </div>

              {/* Row Container */}
              <div className={`flex-1 min-w-0 pl-[4px] pr-[10px] flex flex-col transition-all ${dragOverId === l.id ? 'shadow-[0_-2px_0_var(--accent)] rounded-t-[8px]' : ''}`}>
                <div className={`flex flex-col rounded-[8px] border transition-colors ${activeLayerId === l.id ? 'border-[var(--accent)] bg-[var(--accent-soft)]' : 'border-[var(--hair)] bg-[var(--tile)] hover:border-[#3a3a3a]'} ${!l.shown ? 'opacity-40' : ''}`}>
                  
                  {/* Top Header Row (50/50 split) */}
                  <div className="flex items-stretch h-[44px]">
                    {/* LEFT: Layer */}
                    <div 
                      className={`flex-1 min-w-0 flex items-center gap-[6px] px-[6px] rounded-tl-[7px] ${expandedIds.includes(l.id) ? 'rounded-bl-[0px]' : 'rounded-bl-[7px]'} cursor-pointer hover:bg-[rgba(255,255,255,0.04)] ${activeLayerId === l.id && activeTarget === 'layer' ? 'bg-[rgba(255,255,255,0.06)] shadow-[inset_0_-2px_0_var(--accent)]' : ''}`}
                      onClick={() => onSelect(l.id, 'layer')}
                      onDoubleClick={() => onInspect(l.id, 'layer')}
                    >
                      <button 
                        className="w-[20px] h-[20px] flex-none flex items-center justify-center text-[#8a8a8a] hover:text-[#ededed] transition-colors rounded-[4px] hover:bg-black/20"
                        onClick={(e) => { 
                          e.stopPropagation(); 
                          setExpandedIds(prev => prev.includes(l.id) ? prev.filter(x => x !== l.id) : [...prev, l.id]);
                        }}
                      >
                        <ChevronRight size={14} className={`transition-transform ${expandedIds.includes(l.id) ? 'rotate-90' : ''}`} />
                      </button>
                      <button 
                        className="w-[20px] h-[20px] flex-none flex items-center justify-center text-[#8a8a8a] hover:text-[#ededed] transition-colors rounded-[4px] hover:bg-black/20"
                        onClick={(e) => { e.stopPropagation(); setLayers(ls => ls.map((x, i) => i === idx ? {...x, shown: !x.shown} : x)); }}
                      >
                        {l.shown ? <Eye size={13} /> : <EyeOff size={13} />}
                      </button>
                      <span className="w-[26px] h-[26px] flex-none rounded-[6px] bg-black border border-[#1c1c1c] relative overflow-hidden flex items-center justify-center">
                        <span className="absolute inset-0 m-auto w-[14px] h-[14px] rounded-[3px] transition-colors" style={{background: l.paint}}></span>
                        <span className="absolute right-[2px] top-[2px] w-[4px] h-[4px] rounded-full" style={{background: kind.Tint}}></span>
                      </span>
                      <div className="flex-1 min-w-0 pr-1">
                        <b className="block font-medium text-[11.5px] leading-[1.2] whitespace-nowrap overflow-hidden text-ellipsis text-[#ededed]">{l.name}</b>
                        <span className="block text-[9.5px] text-[#6a6a6a] leading-[1.3] whitespace-nowrap overflow-hidden text-ellipsis">{l.blend} · {l.opacity}%</span>
                      </div>
                    </div>

                    {/* Divider */}
                    <div className="w-[1px] bg-[var(--hair)] my-[6px]"></div>

                    {/* RIGHT: Mask */}
                    <div 
                      className={`flex-1 min-w-0 flex items-center gap-[6px] px-[6px] rounded-tr-[7px] ${expandedIds.includes(l.id) ? 'rounded-br-[0px]' : 'rounded-br-[7px]'} cursor-pointer hover:bg-[rgba(255,255,255,0.04)] ${activeLayerId === l.id && activeTarget === 'mask' ? 'bg-[rgba(255,255,255,0.06)] shadow-[inset_0_-2px_0_var(--accent)]' : ''}`}
                      onClick={() => onSelect(l.id, 'mask')}
                      onDoubleClick={() => onInspect(l.id, 'mask')}
                    >
                      {!l.mask.enabled ? (
                        <>
                          <div className="w-[26px] h-[26px] flex-none rounded-[5px] border border-dashed border-[#8a8a8a] flex items-center justify-center opacity-40"><Plus size={10} className="text-[#8a8a8a]"/></div>
                          <div className="flex-1 min-w-0 opacity-40">
                            <span className="block text-[10.5px] text-[#8a8a8a] italic">No Mask</span>
                          </div>
                        </>
                      ) : (
                        <>
                          <button 
                            className="w-[20px] h-[20px] flex-none flex items-center justify-center text-[#8a8a8a] hover:text-[#ededed] transition-colors rounded-[4px] hover:bg-black/20"
                            onClick={(e) => { e.stopPropagation(); setLayers(ls => ls.map(x => x.id === l.id ? {...x, mask: {...x.mask, shown: x.mask.shown === false ? true : false}} : x)); }}
                            title={l.mask.shown !== false ? "Hide Mask" : "Show Mask"}
                          >
                            {l.mask.shown !== false ? <Eye size={13} /> : <EyeOff size={13} />}
                          </button>
                          <span className="w-[26px] h-[26px] flex-none rounded-[6px] bg-black border border-[#1c1c1c] p-[2px] flex items-center justify-center">
                            <span className="block w-full h-full rounded-[3px] bg-white transition-opacity" style={{opacity: l.mask.shown !== false ? l.mask.strength/100 : 0.1}}></span>
                          </span>
                          <div className={`flex-1 min-w-0 pr-1 ${l.mask.shown === false ? 'opacity-40' : ''}`}>
                            <b className="block font-medium text-[11.5px] leading-[1.2] whitespace-nowrap overflow-hidden text-ellipsis text-[#ededed]">Mask</b>
                            <span className="block text-[9.5px] text-[#6a6a6a] leading-[1.3] whitespace-nowrap overflow-hidden text-ellipsis">{l.mask.strength}%</span>
                          </div>
                          <button 
                            className="w-[20px] h-[20px] flex-none flex items-center justify-center text-[#8a8a8a] hover:text-[#ef4444] transition-colors rounded-[4px] hover:bg-[rgba(239,68,68,0.15)]"
                            onClick={(e) => { 
                              e.stopPropagation(); 
                              setLayers(ls => ls.map(x => x.id === l.id ? {...x, mask: {...x.mask, enabled: false}} : x));
                            }}
                            title="Delete Mask"
                          >
                            <X size={13} />
                          </button>
                        </>
                      )}
                      <button 
                        className="w-[20px] h-[20px] flex-none flex items-center justify-center text-[#8a8a8a] hover:text-[#ef4444] transition-colors rounded-[4px] hover:bg-[rgba(239,68,68,0.15)] ml-auto"
                        onClick={(e) => { 
                          e.stopPropagation(); 
                          setLayers(ls => ls.filter(x => x.id !== l.id));
                          if (activeLayerId === l.id && layers.length > 1) {
                            const next = layers.find(x => x.id !== l.id);
                            if (next) onSelect(next.id, 'layer');
                          }
                        }}
                        title="Delete Layer"
                      >
                        <Trash2 size={13} />
                      </button>
                    </div>
                  </div>

                  {/* Folded Properties */}
                  {expandedIds.includes(l.id) && (
                    <div className="flex border-t border-[var(--hair)] bg-[rgba(0,0,0,0.15)] rounded-b-[7px]">
                      {/* Material Props */}
                      <div className="flex-1 min-w-0 p-[8px] flex flex-col gap-[6px]">
                        <div className="grid grid-cols-[50px_minmax(0,1fr)] gap-x-[8px] items-center min-h-[26px]">
                          <div className="text-[10px] text-[#8a8a8a]">Blend</div>
                          <Dropdown options={BLEND_MODES} value={Math.max(0, BLEND_MODES.indexOf(l.blend))} onChange={(v) => setLayers(ls => ls.map(x => x.id === l.id ? {...x, blend: BLEND_MODES[v]} : x))} />
                        </div>
                        <div className="grid grid-cols-[50px_minmax(0,1fr)] gap-x-[8px] items-center min-h-[26px]">
                          <div className="text-[10px] text-[#8a8a8a]">Opac</div>
                          <Slider value={l.opacity} min={0} max={100} unit="%" fmt={0} onChange={(v) => setLayers(ls => ls.map(x => x.id === l.id ? {...x, opacity: v} : x))} />
                        </div>
                        <div className="text-[10px] font-semibold text-[#6a6a6a] uppercase mt-[2px]">Channels</div>
                        <div className="flex gap-1 flex-wrap mb-1">
                          {l.channels.map(c => (
                            <span key={c} className="h-[20px] px-2 rounded-[4px] bg-[#1c1c1c] text-[#8a8a8a] text-[10px] flex items-center border border-[#2a2a2a]">{c}</span>
                          ))}
                        </div>
                        
                        <div className="mt-auto pt-2">
                          <button 
                            className="h-[26px] w-full rounded-[6px] bg-[var(--tile)] hover:bg-[#3a3a3a] border border-[var(--hair)] text-[10px] font-medium text-[#ededed] transition-colors flex items-center justify-center gap-1"
                            onClick={() => onInspect(l.id, 'layer')}
                          >
                            Full Properties <ChevronRight size={12} />
                          </button>
                        </div>
                      </div>

                      <div className="w-[1px] bg-[var(--hair)] my-[8px]"></div>

                      {/* Mask Props */}
                      <div className="flex-1 min-w-0 p-[8px] flex flex-col gap-[6px]">
                        {!l.mask.enabled ? (
                          <div className="flex-1 flex flex-col items-center justify-center text-center">
                            <button className="h-[26px] px-3 rounded-[6px] bg-[var(--accent)] text-white text-[10px] font-medium hover:opacity-90 transition-all" onClick={() => setLayers(ls => ls.map(x => x.id === l.id ? {...x, mask: {...x.mask, enabled: true}} : x))}>Add Mask</button>
                          </div>
                        ) : (
                          <>
                            <div className="grid grid-cols-[50px_minmax(0,1fr)] gap-x-[8px] items-center min-h-[26px]">
                              <div className="text-[10px] text-[#8a8a8a]">Str</div>
                              <Slider value={l.mask.strength} min={0} max={100} unit="%" fmt={0} onChange={(v) => setLayers(ls => ls.map(x => x.id === l.id ? {...x, mask: {...x.mask, strength: v}} : x))} />
                            </div>
                            <div className="grid grid-cols-[50px_minmax(0,1fr)] gap-x-[8px] items-center min-h-[26px]">
                              <div className="text-[10px] text-[#8a8a8a]">Invert</div>
                              <div className={`w-[26px] h-[14px] rounded-full border cursor-pointer relative transition-colors ${l.mask.invert ? 'bg-[var(--accent)] border-[var(--accent)]' : 'bg-[#2a2a2a] border-[#3a3a3a]'}`} onClick={() => setLayers(ls => ls.map(x => x.id === l.id ? {...x, mask: {...x.mask, invert: !x.mask.invert}} : x))}>
                                <div className={`absolute top-[1px] w-[10px] h-[10px] rounded-full bg-[#ededed] transition-all shadow-sm ${l.mask.invert ? 'left-[13px]' : 'left-[1px]'}`}></div>
                              </div>
                            </div>
                            <div className="text-[10px] font-semibold text-[#6a6a6a] uppercase mt-[2px]">Sources</div>
                            <div className="flex items-center gap-[6px] h-[24px] px-[6px] bg-[var(--menu-2)] border border-[var(--hair)] rounded-[6px] mb-1">
                              <span className="w-3 h-3 rounded-sm bg-[#10b981] flex items-center justify-center text-white"><ImageIcon size={8} /></span>
                              <span className="text-[10px] text-[var(--ink)] font-medium">{l.mask.source || "Generated"}</span>
                            </div>
                            
                            <div className="mt-auto pt-2">
                              <button 
                                className="h-[26px] w-full rounded-[6px] bg-[var(--tile)] hover:bg-[#3a3a3a] border border-[var(--hair)] text-[10px] font-medium text-[#ededed] transition-colors flex items-center justify-center gap-1"
                                onClick={() => onInspect(l.id, 'mask')}
                              >
                                Full Mask <ChevronRight size={12} />
                              </button>
                            </div>
                          </>
                        )}
                      </div>
                    </div>
                  )}
                </div>
              </div>
            </div>
          );
        })}
      </div>

      {/* Footer */}
      <div className="flex-none h-[26px] flex items-center gap-[7px] px-[11px] bg-[#121214] border-t border-[#1c1c1c] text-[10px] text-[#8a8a8a]">
        <span className="w-[7px] h-[7px] rounded-[2px] flex-none bg-[#94a3b8]"></span>
        <span><b className="text-[#ededed] font-semibold font-mono">{shownCount}</b> shown</span>
        <span className="text-[#2c2c2c]">·</span>
        <span><b className="text-[#ededed] font-semibold font-mono">{layers.length - shownCount}</b> hidden</span>
        <span className="flex-1"></span>
        <span>drag to reorder</span>
      </div>
    </div>
  );
}

// -------------------------------------------------------------------------------------------------
// PORTED CHANNEL PROPERTY PANEL (From provided HTML)
// -------------------------------------------------------------------------------------------------

const CHANNEL_SLOTS: Record<string, any> = {
  baseColour:       { Group: "Surface",     Hue: "#b87333", Label: "Base Colour",       Edit: "colour",                                    Placement: "Colour atlas · RGB"    },
  metallic:         { Group: "Surface",     Hue: "#8b5cf6", Label: "Metallic",          Edit: "scalar",  Min: 0, Max: 1,   Step: 0.01,      Placement: "Material atlas · R"    },
  roughness:        { Group: "Surface",     Hue: "#3b82f6", Label: "Roughness",         Edit: "scalar",  Min: 0, Max: 1,   Step: 0.01,      Placement: "Material atlas · G"    },
  height:           { Group: "Surface",     Hue: "#8a8a8a", Label: "Height",            Edit: "scalar",  Min: 0, Max: 1,   Step: 0.01,      Placement: "Material atlas · B"    },
  normal:           { Group: "Surface",     Hue: "#10b981", Label: "Normal",            Edit: "derived", Source: "height",                  Placement: "No storage · derived"  },
  opacity:          { Group: "Surface",     Hue: "#94a3b8", Label: "Opacity",           Edit: "scalar",  Min: 0, Max: 1,   Step: 0.01,      Placement: "Material atlas · A"    },

  emission:         { Group: "Radiance",    Hue: "#f59e0b", Label: "Emissive",          Edit: "colour",                                     Placement: "Emissive atlas · RGB"  },
  ambientOcclusion: { Group: "Radiance",    Hue: "#6b7280", Label: "Ambient Occlusion", Edit: "scalar",  Min: 0, Max: 1,   Step: 0.01,      Placement: "Emissive atlas · A"    },

  anisotropy:       { Group: "Reflectance", Hue: "#22d3ee", Label: "Anisotropy",        Edit: "scalar",  Min: 0, Max: 1,   Step: 0.01,      Placement: "Reflect atlas · R"     },
  anisotropyAngle:  { Group: "Reflectance", Hue: "#0ea5e9", Label: "Anisotropy Angle",  Edit: "scalar",  Min: 0, Max: 360, Step: 1, Unit: "°", Placement: "Reflect atlas · G"  },
  clearcoat:        { Group: "Reflectance", Hue: "#e2e8f0", Label: "Clearcoat",         Edit: "scalar",  Min: 0, Max: 1,   Step: 0.01,      Placement: "Reflect atlas · B"     },
  refractionIndex:  { Group: "Reflectance", Hue: "#a78bfa", Label: "Refraction Index",  Edit: "scalar",  Min: 1, Max: 3,   Step: 0.01,      Placement: "Reflect atlas · A"     },

  sheen:            { Group: "Scattering",  Hue: "#f472b6", Label: "Sheen",             Edit: "colour",                                     Placement: "Scatter atlas · RGB"   },
  subsurface:       { Group: "Scattering",  Hue: "#fb7185", Label: "Subsurface",        Edit: "colour",                                     Placement: "Sheen atlas · RGB"     }
};

const CHANNEL_ORDER = Object.keys(CHANNEL_SLOTS);
const CHANNEL_GROUPS = ["Surface", "Radiance", "Reflectance", "Scattering"];
const ATLAS_TOTAL = 5;

const GENERATOR_CATALOGUE = [
  { Group: "Mask" },
  { Key: "CurvatureEdges", Label: "Curvature",       Note: "Convex edge wear",
    Parameters: [{ Key: "Balance", Label: "Balance", Min: 0, Max: 1, Default: 0.5 }, { Key: "Contrast", Label: "Contrast", Min: 0, Max: 1, Default: 0.7 }, { Key: "Radius", Label: "Radius", Min: 0, Max: 1, Default: 0.25 }] },
  { Key: "AmbientOcclusion", Label: "Ambient Occlusion", Note: "Cavity dirt",
    Parameters: [{ Key: "Spread", Label: "Spread",  Min: 0, Max: 1, Default: 0.4 }, { Key: "Contrast", Label: "Contrast", Min: 0, Max: 1, Default: 0.6 }] },
  { Key: "Thickness", Label: "Thickness",            Note: "Translucent falloff",
    Parameters: [{ Key: "Depth", Label: "Depth",    Min: 0, Max: 1, Default: 0.5 }, { Key: "Contrast", Label: "Contrast", Min: 0, Max: 1, Default: 0.5 }] },
  { Key: "PositionGradient", Label: "Position Gradient", Note: "World-axis ramp",
    Parameters: [{ Key: "Origin", Label: "Origin",  Min: 0, Max: 1, Default: 0.5 }, { Key: "Falloff", Label: "Falloff", Min: 0, Max: 1, Default: 0.35 }] },

  { Group: "Wear" },
  { Key: "MetalEdgeWear", Label: "Metal Edge Wear",  Note: "Curvature + grunge",
    Parameters: [{ Key: "Intensity", Label: "Intensity", Min: 0, Max: 1, Default: 0.6 }, { Key: "Softness", Label: "Softness", Min: 0, Max: 1, Default: 0.3 }, { Key: "Roughness", Label: "Grain", Min: 0, Max: 1, Default: 0.45 }] },
  { Key: "DirtAccumulation", Label: "Dirt",          Note: "Occlusion-driven",
    Parameters: [{ Key: "Amount", Label: "Amount",  Min: 0, Max: 1, Default: 0.5 }, { Key: "Scale", Label: "Scale",    Min: 0, Max: 1, Default: 0.3 }] },
  { Key: "WaterRunoff", Label: "Water Runoff",       Note: "Gravity streaks",
    Parameters: [{ Key: "Length", Label: "Length",  Min: 0, Max: 1, Default: 0.55 }, { Key: "Density", Label: "Density", Min: 0, Max: 1, Default: 0.4 }, { Key: "Gravity", Label: "Gravity", Min: 0, Max: 1, Default: 0.8 }] },

  { Group: "Procedural" },
  { Key: "PerlinNoise", Label: "Perlin Noise",       Note: "Fractal value noise",
    Parameters: [{ Key: "Scale", Label: "Scale",    Min: 0, Max: 1, Default: 0.4 }, { Key: "Octaves", Label: "Octaves", Min: 0, Max: 1, Default: 0.5 }, { Key: "Contrast", Label: "Contrast", Min: 0, Max: 1, Default: 0.5 }] },
  { Key: "VoronoiCells", Label: "Voronoi",           Note: "Cellular partition",
    Parameters: [{ Key: "Density", Label: "Density", Min: 0, Max: 1, Default: 0.35 }, { Key: "Jitter", Label: "Jitter",  Min: 0, Max: 1, Default: 0.7 }] },
  { Key: "BrushedAnisotropy", Label: "Brushed Metal", Note: "Anisotropic streaks",
    Parameters: [{ Key: "Direction", Label: "Angle", Min: 0, Max: 1, Default: 0.0 }, { Key: "Grain", Label: "Grain",    Min: 0, Max: 1, Default: 0.6 }] }
];

const SOURCE_CATALOGUE = [
  { Key: "Value",     Label: "Value",     Note: "Flat authored"   },
  { Key: "Texture",   Label: "Texture",   Note: "Painted by hand" },
  { Key: "Generator", Label: "Generator", Note: "Procedural"      }
];

const C_GLYPH = {
  ChevronDown: (size: number) => `<svg width="${size}" height="${size}" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="3" stroke-linecap="round" stroke-linejoin="round"><polyline points="6 9 12 15 18 9"/></svg>`,
  Plus: `<svg viewBox="0 0 16 16" fill="none"><path d="M8 3v10M3 8h10" stroke="currentColor" stroke-width="1.4" stroke-linecap="round"/></svg>`,
  Cross: `<svg viewBox="0 0 16 16" fill="none"><path d="M4 4l8 8M12 4l-8 8" stroke="currentColor" stroke-width="1.6" stroke-linecap="round"/></svg>`,
  Check: `<svg viewBox="0 0 16 16" fill="none"><path d="M3 8.5l3.2 3.2L13 5" stroke="currentColor" stroke-width="1.6" stroke-linecap="round" stroke-linejoin="round"/></svg>`,
  Stack: `<svg viewBox="0 0 16 16" fill="none"><path d="M2 4.5L8 2l6 2.5L8 7 2 4.5zM2 8l6 2.5L14 8M2 11.5L8 14l6-2.5" stroke="currentColor" stroke-width="1.3" stroke-linejoin="round"/></svg>`,
  Image: `<svg viewBox="0 0 16 16" fill="none"><rect x="2" y="3" width="12" height="10" rx="1.5" stroke="currentColor" stroke-width="1.3"/><circle cx="6" cy="6.5" r="1.1" fill="currentColor"/><path d="M3 11l3-2.5 2.5 2L11 8l2 2" stroke="currentColor" stroke-width="1.3" stroke-linejoin="round"/></svg>`,
  Import: `<svg viewBox="0 0 16 16" fill="none"><path d="M8 2v7m0 0L5.2 6.2M8 9l2.8-2.8M3 11.5v1A1.5 1.5 0 0 0 4.5 14h7a1.5 1.5 0 0 0 1.5-1.5v-1" stroke="currentColor" stroke-width="1.3" stroke-linecap="round" stroke-linejoin="round"/></svg>`,
  Trash: `<svg viewBox="0 0 16 16" fill="none"><path d="M3 5h10M6.5 5V3.5h3V5M4.5 5l.6 8.5h5.8L11.5 5M6.8 7.5v4M9.2 7.5v4" stroke="currentColor" stroke-width="1.3" stroke-linecap="round" stroke-linejoin="round"/></svg>`,
  Reload: `<svg viewBox="0 0 16 16" fill="none"><path d="M13 8a5 5 0 1 1-1.6-3.7M13 3v2.6h-2.6" stroke="currentColor" stroke-width="1.3" stroke-linecap="round" stroke-linejoin="round"/></svg>`,
  Brush: `<svg viewBox="0 0 16 16" fill="none"><path d="M11.5 2.5l2 2-6 6-2.6.6.6-2.6 6-6z" stroke="currentColor" stroke-width="1.3" stroke-linejoin="round"/><path d="M4.5 11.5c-.8.8-1 2-1 2s1.2-.2 2-1" stroke="currentColor" stroke-width="1.3" stroke-linecap="round"/></svg>`,
  Cog: `<svg viewBox="0 0 16 16" fill="none"><circle cx="8" cy="8" r="2.2" stroke="currentColor" stroke-width="1.3"/><path d="M8 1.6v1.8M8 12.6v1.8M1.6 8h1.8M12.6 8h1.8M3.5 3.5l1.3 1.3M11.2 11.2l1.3 1.3M12.5 3.5l-1.3 1.3M4.8 11.2L3.5 12.5" stroke="currentColor" stroke-width="1.3" stroke-linecap="round"/></svg>`,
  Circle: `<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.8" stroke-linecap="round"><circle cx="12" cy="12" r="9"/><path d="M12 3a9 9 0 0 0 0 18"/></svg>`
};

const IconGlyph = ({ svg, className }: { svg: string, className?: string }) => (
  <span className={className} dangerouslySetInnerHTML={{ __html: svg }} />
);

export function ChannelPropertyPanel({ layerName, onBack }: { layerName?: string, onBack?: () => void }) {
  const [layer, setLayer] = useState({
    Name: layerName || "Brushed Copper",
    Classification: "Material",
    Blend: "Normal",
    Enabled: ["baseColour", "metallic", "roughness", "height", "normal", "ambientOcclusion", "anisotropy", "anisotropyAngle", "clearcoat"],
    Modes: { baseColour: "Texture", metallic: "Value", roughness: "Generator", height: "Value", opacity: "Value", ambientOcclusion: "Generator", anisotropy: "Value", anisotropyAngle: "Value", clearcoat: "Value", refractionIndex: "Value", emission: "Value", sheen: "Value", subsurface: "Value" } as Record<string, string>,
    Amounts: { metallic: 0.94, roughness: 0.38, height: 0.50, opacity: 1.0, ambientOcclusion: 1.0, anisotropy: 0.72, anisotropyAngle: 35, clearcoat: 0.15, refractionIndex: 1.45 } as Record<string, number>,
    Colours: { baseColour: "#b87333", emission: "#000000", sheen: "#3a3a3a", subsurface: "#d98a72" } as Record<string, string>,
    Textures: { baseColour: { Name: "copper_basecolour.png", Width: 2048, Height: 2048, Format: "sRGB 8" } } as Record<string, any>,
    Strokes: { baseColour: 148, height: 62 } as Record<string, number>,
    Generators: { roughness: { Key: "MetalEdgeWear", Parameters: { Intensity: 0.6, Softness: 0.3, Roughness: 0.45 } }, ambientOcclusion: { Key: "AmbientOcclusion", Parameters: { Spread: 0.4, Contrast: 0.6 } } } as Record<string, any>
  });

  const [collapsed, setCollapsed] = useState<Set<string>>(new Set(["height", "normal", "ambientOcclusion", "anisotropy", "anisotropyAngle", "clearcoat"]));
  const [openPicker, setOpenPicker] = useState<string | null>(null);

  const toggleFold = (key: string) => {
    setCollapsed(prev => {
      const next = new Set(prev);
      if (next.has(key)) next.delete(key); else next.add(key);
      return next;
    });
  };

  const FormatAmount = (amt: number, unit?: string) => unit === "°" ? String(Math.round(amt)) : amt.toFixed(2);

  const CustomSlider = ({ channelKey, value, min, max, unit, isGen, paramKey }: any) => {
    const fraction = (value - min) / (max - min);
    
    const handleDown = (e: React.PointerEvent) => {
      const track = e.currentTarget as HTMLElement;
      track.setPointerCapture(e.pointerId);
      const update = (clientX: number) => {
        const rect = track.getBoundingClientRect();
        const frac = Math.min(1, Math.max(0, (clientX - rect.left) / rect.width));
        const val = min + frac * (max - min);
        if (isGen) {
          setLayer(l => ({ ...l, Generators: { ...l.Generators, [channelKey]: { ...l.Generators[channelKey], Parameters: { ...l.Generators[channelKey].Parameters, [paramKey]: val } } } }));
        } else {
          setLayer(l => ({ ...l, Amounts: { ...l.Amounts, [channelKey]: val } }));
        }
      };
      update(e.clientX);
      track.onpointermove = (me: any) => { if (me.buttons) update(me.clientX); };
      track.onpointerup = () => { track.onpointermove = null; };
    };

    return (
      <div className="sliderrow">
        <div className="valuebox">
          <div className="num">
            <input value={FormatAmount(value, unit)} readOnly />
          </div>
          <div className="unitseg">{unit || "–"}</div>
        </div>
        <div className="slider" onPointerDown={handleDown}>
          <div className="fill" style={{ right: `${(1 - fraction) * 100}%` }}></div>
          <div className="knob" style={{ left: `${fraction * 100}%` }}></div>
        </div>
      </div>
    );
  };

  const toggleChannel = (key: string) => {
    if (key === "baseColour") return;
    setLayer(l => ({ ...l, Enabled: l.Enabled.includes(key) ? l.Enabled.filter(k => k !== key) : [...l.Enabled, key] }));
  };

  const setMode = (key: string, mode: string) => {
    setLayer(l => ({ ...l, Modes: { ...l.Modes, [key]: mode } }));
    setOpenPicker(null);
  };

  const getDefParams = (def: any) => {
    const bag: any = {};
    def.Parameters.forEach((p: any) => bag[p.Key] = p.Default);
    return bag;
  };

  return (
    <div className="cpp-container h-full flex flex-col relative" onClick={() => setOpenPicker(null)}>
      <style>{`
        .cpp-container {
          --desk: #000000; --menu: #0e0e0e; --menu-2: #0e0e0e; --hair: #1c1c1c; --hair-strong: #2a2a30;
          --tile: #141414; --tile-hi: #1a1a1a; --tile-active: #1f1f1f; --accent: #e8e8e8; --marker: #4a90e2;
          --ink: #ededed; --muted: #8a8a8a; --faint: #6a6a6a; --on-accent: #111111;
          --value-black: #000000; --value-unit: #141414; --track-bg: #141414; --track-fill: #5a5a5a;
          --knob: #ffffff; --text-value: #ffffff; --danger: #e05a5a; --row-hover: rgba(255,255,255,.045);
          --r-tile: 12px; --card-gap: 6px; --row-h: 32px;
          --hue-material: #8b5cf6; --hue-generator: #10b981; --hue-brushwork: #f97316; --hue-flood: #3b82f6;
          background: var(--desk); font: 12px/1.4 "Segoe UI", system-ui, sans-serif; color: var(--ink);
        }
        .slide { background: var(--menu); display: flex; flex-direction: column; height: 100%; border-left: 1px solid var(--hair); }
        .slide-head { display: flex; align-items: center; gap: 9px; height: 46px; padding: 0 10px; background: var(--menu-2); border-bottom: 1px solid var(--hair); }
        .head-icon { flex: 0 0 24px; height: 24px; border-radius: 6px; background: var(--desk); border: 1px solid var(--hair); display: flex; align-items: center; justify-content: center; color: var(--muted); }
        .head-text { min-width: 0; flex: 1; }
        .head-name { font-size: 12.5px; font-weight: 600; white-space: nowrap; overflow: hidden; text-overflow: ellipsis; line-height:1.2; }
        .head-sub { font-size: 10px; color: var(--faint); white-space: nowrap; overflow: hidden; text-overflow: ellipsis; margin-top: 1px; }
        .slide-body { padding: 8px; flex: 1; overflow-y: auto; }
        .slide-foot { display: flex; align-items: center; height: 28px; padding: 0 11px; font-size: 10px; color: var(--faint); background: var(--menu-2); border-top: 1px solid var(--hair); }
        .pf-strong { color: var(--muted); font-weight: 600; }
        .pf-spacer { flex: 1; }
        .chips-region { padding: 9px 10px; border: 1px solid var(--hair); border-radius: var(--r-tile); background: var(--menu-2); margin-bottom: var(--card-gap); }
        .chips-region.menu-open { position: relative; z-index: 70; }
        .chips-head { display: flex; align-items: center; gap: 7px; font-size: 10px; letter-spacing: .9px; text-transform: uppercase; color: var(--faint); font-weight: 600; margin-bottom: 8px; }
        .chips-head svg { width: 12px; height: 12px; }
        .ccount { background: var(--accent); color: var(--on-accent); border-radius: 9px; font-size: 9.5px; padding: 1px 6px; letter-spacing: 0; font-weight: 600; opacity: 0; transform: scale(.6); transition: .18s; }
        .ccount.show { opacity: 1; transform: scale(1); }
        .clear-all { cursor: pointer; font-size: 10px; letter-spacing: .3px; color: var(--faint); text-transform: none; font-weight: 500; transition: color .15s; display: none; }
        .clear-all:hover { color: var(--danger); }
        .clear-all.show { display: inline; }
        .chips { display: flex; flex-wrap: wrap; gap: 6px; align-items: center; }
        .chip { display: inline-flex; align-items: center; gap: 6px; background: var(--tile); border: 1px solid var(--hair); border-radius: 999px; padding: 4px 5px 4px 10px; font-size: 11.5px; color: var(--ink); transition: border-color .15s, background .15s; animation: chip-in .18s cubic-bezier(.34,1.4,.64,1) both; }
        @keyframes chip-in { from { opacity: 0; transform: scale(.82); } to { opacity: 1; transform: scale(1); } }
        .chip:hover { border-color: var(--hair-strong); }
        .chip .cswatch { width: 9px; height: 9px; border-radius: 50%; flex-shrink: 0; }
        .chip .cx { width: 17px; height: 17px; border-radius: 50%; border: none; cursor: pointer; background: var(--tile-active); color: var(--muted); display: flex; align-items: center; justify-content: center; flex-shrink: 0; transition: background .15s, color .15s, transform .15s; }
        .chip .cx svg { width: 9px; height: 9px; }
        .chip .cx:hover { background: var(--danger); color: #fff; transform: rotate(90deg); }
        .chip.locked .cx { opacity: .3; cursor: default; }
        .chip.locked .cx:hover { background: var(--tile-active); color: var(--muted); transform: none; }
        .add-wrap { position: relative; display: inline-flex; }
        .add-btn { display: inline-flex; align-items: center; justify-content: center; width: 27px; height: 27px; background: transparent; border: 1px dashed var(--hair); border-radius: 50%; cursor: pointer; color: var(--muted); transition: border-color .15s, color .15s, background .15s, transform .15s; }
        .add-btn:hover { border-color: var(--accent); color: var(--ink); background: rgba(255,255,255,.05); }
        .add-btn svg { width: 13px; height: 13px; transition: transform .2s cubic-bezier(.4,0,.2,1); }
        .add-btn.open svg { transform: rotate(135deg); }
        .add-menu { position: absolute; left: 0; top: 33px; z-index: 60; background: #141416; border: 1px solid var(--hair-strong); border-radius: 9px; padding: 6px; min-width: 196px; box-shadow: 0 14px 34px rgba(0,0,0,.7); transform-origin: top left; opacity: 0; transform: scale(.94) translateY(-4px); pointer-events: none; transition: opacity .16s, transform .16s; max-height: 330px; overflow-y: auto; }
        .add-menu.open { opacity: 1; transform: none; pointer-events: auto; }
        .add-menu::-webkit-scrollbar { width: 8px; }
        .add-menu::-webkit-scrollbar-thumb { background: #26262c; border-radius: 99px; }
        .add-menu::-webkit-scrollbar-track { background: transparent; }
        .afm-head { font-size: 9.5px; color: var(--faint); text-transform: uppercase; letter-spacing: .6px; padding: 5px 7px; font-weight: 600; }
        .afm-item { display: flex; align-items: center; gap: 9px; padding: 6px 8px; border-radius: 6px; cursor: pointer; font-size: 12px; color: var(--ink); transition: background .12s, padding-left .12s; }
        .afm-item:hover { background: var(--row-hover); padding-left: 11px; }
        .afm-item.disabled { opacity: .34; pointer-events: none; }
        .afm-item .swatch { width: 11px; height: 11px; border-radius: 50%; flex-shrink: 0; }
        .afm-item .afm-sub { margin-left: auto; font-size: 9.5px; color: var(--faint); letter-spacing: .3px; }
        .afm-item .tick { margin-left: auto; color: var(--accent); display: none; align-items: center; }
        .afm-item.on .tick { display: inline-flex; }
        .afm-item .tick svg { width: 13px; height: 13px; }
        .afm-sep { height: 1px; background: var(--hair); margin: 5px 3px; }
        .add-menu.open .afm-item, .add-menu.open .afm-head { animation: afm-rise .2s cubic-bezier(.4,0,.2,1) both; }
        @keyframes afm-rise { from { opacity: 0; transform: translateY(-5px); } to { opacity: 1; transform: none; } }
        .chan-panel { background: var(--menu); border: 1px solid var(--hair); border-radius: 10px; margin-bottom: 5px; overflow: hidden; }
        .chan-panel.menu-open { overflow: visible; position: relative; z-index: 70; }
        .chan-panel.menu-open .chan-shell, .chan-panel.menu-open .chan-clip { overflow: visible; }
        .chan-head { display: flex; align-items: center; gap: 8px; height: 29px; padding: 0 9px; background: var(--menu-2); border-bottom: 1px solid var(--hair); cursor: pointer; user-select: none; transition: background .13s; }
        .chan-panel.collapsed .chan-head { border-bottom-color: transparent; }
        .chan-head:hover { background: var(--tile); }
        .ch-tw { display: flex; align-items: center; justify-content: center; width: 9px; height: 9px; color: var(--faint); transition: transform .26s cubic-bezier(.34,1.5,.64,1), color .15s; }
        .chan-panel.collapsed .ch-tw { transform: rotate(-90deg); }
        .chan-head:hover .ch-tw { color: var(--ink); }
        .ch-dot { flex: 0 0 6px; width: 6px; height: 6px; border-radius: 50%; transition: transform .22s cubic-bezier(.34,1.5,.64,1), opacity .2s; }
        .chan-panel.collapsed .ch-dot { transform: scale(.72); opacity: .5; }
        .ch-title { flex: 1; font-size: 11px; }
        .ch-src { font-size: 9.5px; letter-spacing: .4px; text-transform: uppercase; color: var(--faint); transition: opacity .2s ease, transform .24s cubic-bezier(.4,0,.2,1); }
        .chan-panel:not(.collapsed) .ch-src { opacity: .45; transform: translateX(3px); }
        .chan-shell { display: grid; grid-template-rows: 1fr; transition: grid-template-rows .28s cubic-bezier(.4,0,.2,1); }
        .chan-panel.collapsed .chan-shell { grid-template-rows: 0fr; transition: grid-template-rows .22s cubic-bezier(.4,0,.2,1); }
        .chan-clip { overflow: hidden; min-height: 0; }
        .chan-body { padding: 6px 9px 8px; transition: opacity .24s ease .04s, transform .28s cubic-bezier(.4,0,.2,1); }
        .chan-panel.collapsed .chan-body { opacity: 0; transform: translateY(-6px); transition: opacity .14s ease, transform .2s ease; }
        .chan-note { font-size: 10.5px; line-height: 1.6; color: var(--faint); padding: 2px 1px; }
        .prow { display: grid; grid-template-columns: 88px minmax(0,1fr); column-gap: 10px; align-items: center; min-height: var(--row-h); }
        .plabel { font-size: 11.5px; color: var(--muted); }
        .valuebox { display: flex; align-items: center; overflow: hidden; height: 26px; background: var(--value-black); border-radius: 999px; }
        .valuebox .num { flex: 1; min-width: 0; display: flex; align-items: center; justify-content: center; }
        .valuebox input { width: 100%; border: 0; background: none; outline: none; text-align: center; color: var(--text-value); font: 600 12.6px/1 "Segoe UI", system-ui, sans-serif; font-variant-numeric: tabular-nums; }
        .unitseg { flex: 0 0 30px; display: flex; align-items: center; justify-content: center; align-self: stretch; font-size: 10.5px; color: var(--muted); background: var(--value-unit); }
        .sliderrow { display: flex; align-items: center; gap: 9px; }
        .sliderrow .valuebox { flex: 0 0 92px; }
        .slider { position: relative; flex: 1; height: 19px; background: var(--track-bg); border-radius: 999px; cursor: pointer; }
        .fill { position: absolute; inset: 0 auto 0 0; background: var(--track-fill); border-radius: 999px; }
        .knob { position: absolute; top: 50%; width: 21px; height: 21px; margin-left: -10.5px; transform: translateY(-50%); border-radius: 50%; background: var(--knob); box-shadow: 0 0 0 1px rgba(0,0,0,.35); transition: box-shadow .13s; }
        .slider:hover .knob { box-shadow: 0 0 0 1px rgba(0,0,0,.35), 0 0 0 4px rgba(255,255,255,.07); }
        .segrow { display: flex; height: 26px; background: var(--tile); border: 1px solid var(--hair); border-radius: 999px; overflow: hidden; }
        .seg { flex: 1; display: flex; align-items: center; justify-content: center; font-size: 10.5px; color: var(--muted); cursor: pointer; user-select: none; transition: background .13s, color .13s; }
        .seg:hover { background: var(--tile-hi); color: var(--ink); }
        .seg.on { background: var(--accent); color: var(--on-accent); font-weight: 600; }
        .pickbar { display: flex; align-items: center; gap: 8px; height: 26px; padding: 0 4px 0 10px; background: var(--tile); border: 1px solid var(--hair); border-radius: 999px; cursor: pointer; transition: border-color .15s, background .15s; }
        .pickbar:hover { border-color: var(--hair-strong); background: var(--tile-hi); }
        .pickbar .pname { flex: 1; font-size: 11px; color: var(--ink); white-space: nowrap; overflow: hidden; text-overflow: ellipsis; }
        .pickbar .caret { flex: 0 0 16px; color: var(--faint); display: flex; align-items: center; justify-content: center; transition: transform .2s cubic-bezier(.4,0,.2,1); }
        .pickbar.open .caret { transform: rotate(180deg); }
        .pickwrap { position: relative; }
        .pickwrap .add-menu { top: 30px; min-width: 100%; }
        .colorbar { display: flex; align-items: center; gap: 8px; height: 26px; padding: 0 4px; background: var(--tile); border: 1px solid var(--hair); border-radius: 999px; cursor: pointer; transition: border-color .15s; }
        .colorbar:hover { border-color: var(--hair-strong); }
        .chip-round { flex: 0 0 18px; height: 18px; border-radius: 50%; box-shadow: inset 0 0 0 1px rgba(255,255,255,.14); }
        .cname { flex: 1; font-size: 11px; color: var(--text-value); font-variant-numeric: tabular-nums; text-transform: uppercase; letter-spacing: .4px; }
        .caret { flex: 0 0 16px; color: var(--faint); display: flex; align-items: center; justify-content: center; }
        .slot { display: flex; align-items: center; gap: 9px; padding: 6px; margin-top: 5px; background: var(--tile); border: 1px solid var(--hair); border-radius: 9px; }
        .slot-thumb { flex: 0 0 38px; height: 38px; border-radius: 6px; border: 1px solid var(--hair); background-image: linear-gradient(45deg,#1a1a1a 25%,transparent 25%,transparent 75%,#1a1a1a 75%), linear-gradient(45deg,#1a1a1a 25%,transparent 25%,transparent 75%,#1a1a1a 75%); background-size: 8px 8px; background-position: 0 0,4px 4px; background-color: #0a0a0a; display: flex; align-items: center; justify-content: center; color: var(--faint); }
        .slot-text { flex: 1; min-width: 0; }
        .slot-name { font-size: 11px; white-space: nowrap; overflow: hidden; text-overflow: ellipsis; }
        .slot-meta { font-size: 9.5px; color: var(--faint); margin-top: 2px; font-variant-numeric: tabular-nums; }
        .slot-acts { display: flex; gap: 4px; flex-shrink: 0; }
        .iconbtn { width: 24px; height: 24px; border-radius: 6px; border: 1px solid var(--hair); background: var(--menu-2); color: var(--muted); cursor: pointer; display: flex; align-items: center; justify-content: center; transition: background .14s, color .14s, border-color .14s, transform .14s; }
        .iconbtn svg { width: 13px; height: 13px; }
        .iconbtn:hover { background: var(--tile-hi); color: var(--ink); border-color: var(--hair-strong); }
        .iconbtn:active { transform: scale(.9); }
        .iconbtn.danger:hover { background: rgba(224,90,90,.16); color: var(--danger); border-color: rgba(224,90,90,.4); }
        .iconbtn:disabled { opacity: .3; cursor: default; }
        .iconbtn:disabled:hover { background: var(--menu-2); color: var(--muted); border-color: var(--hair); }
        .slot-thumb.painted { color: rgba(0,0,0,.62); background-image: none; }
        .slot-label { font-size: 9px; letter-spacing: .7px; text-transform: uppercase; color: var(--faint); font-weight: 600; padding: 8px 1px 1px; }
        .slot-empty { display: flex; align-items: center; justify-content: center; gap: 7px; height: 44px; margin-top: 5px; background: transparent; border: 1px dashed var(--hair); border-radius: 9px; color: var(--muted); font-size: 11px; cursor: pointer; transition: border-color .15s, color .15s, background .15s; }
        .slot-empty:hover { border-color: var(--accent); color: var(--ink); background: rgba(255,255,255,.035); }
        .slot-empty svg { width: 13px; height: 13px; }
        .gen-params { margin-top: 4px; padding-top: 4px; border-top: 1px solid var(--hair); animation: afm-rise .22s cubic-bezier(.4,0,.2,1) both; }
        .chan-prev { display: flex; align-items: center; gap: 8px; margin-top: 5px; padding: 5px 1px 1px; }
        .prev-tile { flex: 0 0 34px; height: 34px; border-radius: 7px; border: 1px solid var(--hair); background-image: linear-gradient(45deg,#1a1a1a 25%,transparent 25%,transparent 75%,#1a1a1a 75%), linear-gradient(45deg,#1a1a1a 25%,transparent 25%,transparent 75%,#1a1a1a 75%); background-size: 8px 8px; background-position: 0 0,4px 4px; background-color: #0a0a0a; }
        .prev-text { min-width: 0; }
        .prev-line1 { font-size: 10.5px; color: var(--muted); }
        .prev-line2 { font-size: 9.5px; color: var(--faint); margin-top: 2px; font-variant-numeric: tabular-nums; }
      `}</style>
      <div className="slide">
        <div className="slide-head">
          <div className="head-icon"><IconGlyph svg={C_GLYPH.Circle} /></div>
          <div className="head-text">
            <div className="head-name">{layer.Name}</div>
            <div className="head-sub">{layer.Classification} · {layer.Blend}</div>
          </div>
          {onBack && (
            <button className="flex-none flex items-center gap-1.5 px-2 h-7 rounded-md text-[11px] font-medium text-white hover:bg-[var(--tile-hi)] transition-colors ml-auto border border-[var(--hair)] bg-[var(--tile)]" onClick={onBack}>
              <IconGlyph svg={C_GLYPH.ChevronDown(13)} className="rotate-90" />
              Back
            </button>
          )}
        </div>
        <div className="slide-body">
          {/* Chips Region */}
          <div className={`chips-region ${openPicker === 'chips' ? 'menu-open' : ''}`}>
            <div className="chips-head">
              <IconGlyph svg={C_GLYPH.Stack} />
              <span>Channels</span>
              <span className={`ccount ${layer.Enabled.length ? 'show' : ''}`}>{layer.Enabled.length}</span>
              <span style={{ flex: 1 }}></span>
              <span className={`clear-all ${layer.Enabled.length > 1 ? 'show' : ''}`} onClick={() => setLayer(l => ({ ...l, Enabled: ["baseColour"] }))}>Clear all</span>
            </div>
            <div className="chips">
              {CHANNEL_ORDER.filter(k => layer.Enabled.includes(k)).map(k => {
                const locked = k === "baseColour";
                return (
                  <div key={k} className={`chip ${locked ? 'locked' : ''}`}>
                    <span className="cswatch" style={{ background: CHANNEL_SLOTS[k].Hue }}></span>
                    <span>{CHANNEL_SLOTS[k].Label}</span>
                    <button className="cx" title="Remove channel" onClick={(e) => { e.stopPropagation(); toggleChannel(k); }}><IconGlyph svg={C_GLYPH.Cross} /></button>
                  </div>
                );
              })}
              <div className="add-wrap">
                <button 
                  className={`add-btn ${openPicker === 'chips' ? 'open' : ''}`} 
                  title="Add channel"
                  onClick={(e) => { e.stopPropagation(); setOpenPicker(p => p === 'chips' ? null : 'chips'); }}
                >
                  <IconGlyph svg={C_GLYPH.Plus} />
                </button>
                <div className={`add-menu ${openPicker === 'chips' ? 'open' : ''}`}>
                  {CHANNEL_GROUPS.map((group, gIdx) => (
                    <React.Fragment key={group}>
                      {gIdx > 0 && <div className="afm-sep"></div>}
                      <div className="afm-head" style={{"--afm-i": gIdx * 4} as any}>{group}</div>
                      {CHANNEL_ORDER.filter(k => CHANNEL_SLOTS[k].Group === group).map((k, idx) => {
                        const active = layer.Enabled.includes(k);
                        const locked = k === "baseColour";
                        return (
                          <div key={k} className={`afm-item ${active ? 'on' : ''} ${locked ? 'disabled' : ''}`} onClick={(e) => { e.stopPropagation(); toggleChannel(k); }} style={{"--afm-i": (gIdx * 4) + idx + 1} as any}>
                            <span className="swatch" style={{ background: CHANNEL_SLOTS[k].Hue }}></span>
                            <span>{CHANNEL_SLOTS[k].Label}</span>
                            <span className="tick"><IconGlyph svg={C_GLYPH.Check} /></span>
                          </div>
                        );
                      })}
                    </React.Fragment>
                  ))}
                </div>
              </div>
            </div>
          </div>

          {/* Channel Panels */}
          {CHANNEL_ORDER.filter(k => layer.Enabled.includes(k)).map(k => {
            const folded = collapsed.has(k);
            const slot = CHANNEL_SLOTS[k];
            const mode = slot.Edit === "derived" ? "Derived" : (layer.Modes[k] || "Value");
            const pickerOpen = openPicker === `${k}:generator`;
            
            return (
              <div key={k} className={`chan-panel ${folded ? 'collapsed' : ''} ${pickerOpen ? 'menu-open' : ''}`}>
                <div className="chan-head" onClick={() => toggleFold(k)}>
                  <span className="ch-tw"><IconGlyph svg={C_GLYPH.ChevronDown(9)} /></span>
                  <span className="ch-dot" style={{ background: "var(--hue-material)" }}></span>
                  <span className="ch-title">{slot.Label}</span>
                  <span className="ch-src">{mode}</span>
                </div>
                <div className="chan-shell">
                  <div className="chan-clip">
                    <div className="chan-body">
                      {slot.Edit === "derived" ? (
                        <>
                          <div className="chan-note">Derived from the painted height. No value to author.</div>
                          <div className="chan-prev">
                            <div className="prev-tile"></div>
                            <div className="prev-text"><div className="prev-line1">Derived preview</div><div className="prev-line2">{slot.Placement}</div></div>
                          </div>
                        </>
                      ) : (
                        <>
                          <div className="prow">
                            <div className="plabel">Source</div>
                            <div>
                              <div className="segrow">
                                {SOURCE_CATALOGUE.map(entry => (
                                  <div key={entry.Key} className={`seg ${entry.Key === mode ? 'on' : ''}`} onClick={() => setMode(k, entry.Key)}>{entry.Label}</div>
                                ))}
                              </div>
                            </div>
                          </div>
                          
                          {mode === "Texture" && (
                            <>
                              <div className="slot">
                                <div className={`slot-thumb ${layer.Strokes[k] ? 'painted' : ''}`} style={{ background: layer.Strokes[k] ? slot.Hue : '' }}>
                                  <IconGlyph svg={layer.Strokes[k] ? C_GLYPH.Brush : C_GLYPH.Image} />
                                </div>
                                <div className="slot-text">
                                  <div className="slot-name">{layer.Strokes[k] ? "Painted strokes" : "No strokes yet"}</div>
                                  <div className="slot-meta">{layer.Strokes[k] ? `${layer.Strokes[k]} strokes · 2048 × 2048 atlas` : "Atlas allocates on the first stroke"}</div>
                                </div>
                                <div className="slot-acts">
                                  <button className="iconbtn danger" disabled={!layer.Strokes[k]} onClick={() => setLayer(l => { const ns = {...l.Strokes}; delete ns[k]; return {...l, Strokes: ns}; })}><IconGlyph svg={C_GLYPH.Trash} /></button>
                                </div>
                              </div>
                              <div className="slot-label">Imported base {layer.Textures[k] ? "" : "— optional"}</div>
                              {layer.Textures[k] ? (
                                <div className="slot">
                                  <div className="slot-thumb"><IconGlyph svg={C_GLYPH.Image} /></div>
                                  <div className="slot-text">
                                    <div className="slot-name">{layer.Textures[k].Name}</div>
                                    <div className="slot-meta">{layer.Textures[k].Width} × {layer.Textures[k].Height} · {layer.Textures[k].Format}</div>
                                  </div>
                                  <div className="slot-acts">
                                    <button className="iconbtn" onClick={() => setLayer(l => ({ ...l, Textures: { ...l.Textures, [k]: { ...l.Textures[k], Name: `${k.toLowerCase()}_replaced.png`, Format: "Linear 16" } } }))}><IconGlyph svg={C_GLYPH.Reload} /></button>
                                    <button className="iconbtn danger" onClick={() => setLayer(l => { const nt = {...l.Textures}; delete nt[k]; return {...l, Textures: nt}; })}><IconGlyph svg={C_GLYPH.Trash} /></button>
                                  </div>
                                </div>
                              ) : (
                                <div className="slot-empty" onClick={() => setLayer(l => ({ ...l, Textures: { ...l.Textures, [k]: { Name: `${k.toLowerCase()}_import.png`, Width: 2048, Height: 2048, Format: "Linear 8" } } }))}>
                                  <IconGlyph svg={C_GLYPH.Import} /><span>Import base texture</span>
                                </div>
                              )}
                            </>
                          )}

                          {mode === "Generator" && (
                            <div className="prow">
                              <div className="plabel">Generator</div>
                              <div>
                                <div className="pickwrap">
                                  <div className={`pickbar ${pickerOpen ? 'open' : ''}`} onClick={(e) => { e.stopPropagation(); setOpenPicker(p => p === `${k}:generator` ? null : `${k}:generator`); }}>
                                    <span className="pname">{layer.Generators[k] ? GENERATOR_CATALOGUE.find(x => x.Key === layer.Generators[k].Key)?.Label : "Choose generator"}</span>
                                    <span className="caret"><IconGlyph svg={C_GLYPH.ChevronDown(9)} /></span>
                                  </div>
                                  <div className={`add-menu ${pickerOpen ? 'open' : ''}`}>
                                    {GENERATOR_CATALOGUE.map((entry, idx) => {
                                      if (entry.Group) return <div key={idx} className="afm-head" style={{"--afm-i": idx} as any}>{entry.Group}</div>;
                                      const on = layer.Generators[k]?.Key === entry.Key;
                                      return (
                                        <div key={idx} className={`afm-item ${on ? 'on' : ''}`} style={{"--afm-i": idx} as any} onClick={(e) => { e.stopPropagation(); setLayer(l => ({ ...l, Generators: { ...l.Generators, [k]: { Key: entry.Key, Parameters: getDefParams(entry) } } })); setOpenPicker(null); }}>
                                          <span>{entry.Label}</span>
                                          <span className={on ? "tick" : "afm-sub"}>{on ? <IconGlyph svg={C_GLYPH.Check} /> : entry.Note}</span>
                                        </div>
                                      );
                                    })}
                                  </div>
                                </div>
                              </div>
                            </div>
                          )}
                          {mode === "Generator" && layer.Generators[k] && (() => {
                            const def = GENERATOR_CATALOGUE.find(x => x.Key === layer.Generators[k].Key);
                            return def && (
                              <div className="gen-params">
                                <div style={{ display: 'flex', alignItems: 'center', gap: '6px', padding: '1px 1px 3px' }}>
                                  <span style={{ fontSize: '9.5px', letterSpacing: '.6px', textTransform: 'uppercase', color: 'var(--faint)', fontWeight: 600, flex: 1 }}>{def.Note}</span>
                                  <button className="iconbtn" onClick={() => setLayer(l => ({ ...l, Generators: { ...l.Generators, [k]: { ...l.Generators[k], Parameters: getDefParams(def) } } }))}><IconGlyph svg={C_GLYPH.Cog} /></button>
                                  <button className="iconbtn danger" onClick={() => setLayer(l => { const ng = {...l.Generators}; delete ng[k]; return {...l, Generators: ng}; })}><IconGlyph svg={C_GLYPH.Trash} /></button>
                                </div>
                                {def.Parameters?.map((p: any) => (
                                  <div key={p.Key} className="prow">
                                    <div className="plabel">{p.Label}</div>
                                    <div><CustomSlider channelKey={k} paramKey={p.Key} isGen value={layer.Generators[k].Parameters[p.Key] ?? p.Default} min={p.Min} max={p.Max} unit={p.Unit} /></div>
                                  </div>
                                ))}
                              </div>
                            );
                          })()}

                          {mode === "Value" && slot.Edit === "colour" && (
                            <div className="prow">
                              <div className="plabel">Colour</div>
                              <div>
                                <div className="colorbar">
                                  <span className="chip-round" style={{ background: layer.Colours[k] || "#000" }}></span>
                                  <span className="cname">{layer.Colours[k] || "#000"}</span>
                                  <span className="caret"><IconGlyph svg={C_GLYPH.ChevronDown(9)} /></span>
                                </div>
                              </div>
                            </div>
                          )}

                          {mode === "Value" && slot.Edit === "scalar" && (
                            <div className="prow">
                              <div className="plabel">Amount</div>
                              <div><CustomSlider channelKey={k} isGen={false} value={layer.Amounts[k] ?? slot.Min} min={slot.Min} max={slot.Max} unit={slot.Unit} /></div>
                            </div>
                          )}

                          <div className="chan-prev">
                            <div className="prev-tile" style={mode === "Value" && slot.Edit === "colour" ? { backgroundImage: 'none', backgroundColor: layer.Colours[k] || '#000' } : mode === "Value" && slot.Edit === "scalar" ? { backgroundImage: 'none', backgroundColor: `rgb(${Math.round(((layer.Amounts[k] ?? slot.Min) - slot.Min) / (slot.Max - slot.Min) * 255)},${Math.round(((layer.Amounts[k] ?? slot.Min) - slot.Min) / (slot.Max - slot.Min) * 255)},${Math.round(((layer.Amounts[k] ?? slot.Min) - slot.Min) / (slot.Max - slot.Min) * 255)})` } : {}}></div>
                            <div className="prev-text">
                              <div className="prev-line1">{mode} preview</div>
                              <div className="prev-line2">{slot.Placement}</div>
                            </div>
                          </div>
                        </>
                      )}
                    </div>
                  </div>
                </div>
              </div>
            );
          })}
        </div>
        <div className="slide-foot">
          <span><span className="pf-strong">{layer.Enabled.length}</span> channels</span>
          <span className="pf-spacer"></span>
          <span><span className="pf-strong">{ATLAS_TOTAL}</span> atlases</span>
        </div>
      </div>
    </div>
  );
}

const PropertyRow = ({ label, children }: { label: string, children: React.ReactNode }) => (
  <div className="MaskRow">
    <div className="MaskRowLabel">{label}</div>
    <div className="MaskRowField">{children}</div>
  </div>
);

const Segments = ({ options, current, onChange }: { options: string[], current: string, onChange: (v: string) => void }) => (
  <div className="MaskSegments">
    {options.map(opt => (
      <div key={opt} className={`MaskSegment ${opt === current ? 'On' : ''}`} onClick={() => onChange(opt)}>{opt}</div>
    ))}
  </div>
);

const MaskSlider = ({ value, min, max, unit, decimals = 2, onChange }: { value: number, min: number, max: number, unit?: string, decimals?: number, onChange: (v: number) => void }) => {
  const fraction = Math.min(1, Math.max(0, (value - min) / (max - min)));

  const handleDown = (e: React.PointerEvent) => {
    const track = e.currentTarget as HTMLElement;
    track.setPointerCapture(e.pointerId);
    
    const update = (clientX: number) => {
      const rect = track.getBoundingClientRect();
      const frac = Math.min(1, Math.max(0, (clientX - rect.left) / rect.width));
      onChange(min + frac * (max - min));
    };
    
    update(e.clientX);
    
    track.onpointermove = (me: any) => { if (me.buttons) update(me.clientX); };
    track.onpointerup = () => { track.onpointermove = null; };
  };

  return (
    <div className="MaskSlider">
      <div className="MaskCapsule">
        <div className="MaskCapsuleField">
          <input type="number" step={Math.pow(10, -decimals)} value={value.toFixed(decimals)} onChange={(e) => {
            const typed = parseFloat(e.target.value);
            if (!isNaN(typed)) onChange(Math.min(max, Math.max(min, typed)));
          }} />
        </div>
        <div className="MaskCapsuleUnit">{unit || "-"}</div>
      </div>
      <div className="MaskTrack" onPointerDown={handleDown}>
        <div className="MaskTrackFill" style={{ right: `${(1 - fraction) * 100}%` }}></div>
        <div className="MaskKnob" style={{ left: `${fraction * 100}%` }}></div>
      </div>
    </div>
  );
};

const MaskVerb = ({ svg, title, danger, disabled, onClick }: { svg: string, title: string, danger?: boolean, disabled?: boolean, onClick: () => void }) => (
  <button title={title} className={`MaskVerb ${danger ? 'Danger' : ''} ${disabled ? 'Off' : ''}`} disabled={disabled} onClick={disabled ? undefined : onClick} dangerouslySetInnerHTML={{ __html: svg }} />
);

export function MaskPropertyPanel({ layerName, mask, onBack }: { layerName?: string, mask?: any, onBack?: () => void }) {
  const [collapsed, setCollapsed] = useState(false);
  const [baseMask, setBaseMask] = useState(mask?.Fill?.toLowerCase() === 'black' ? 'Black' : 'White');
  const [invert, setInvert] = useState(!!mask?.invert);
  const [strength, setStrength] = useState(Math.min(1, Math.max(0, (mask?.strength === undefined ? 100 : mask.strength) / 100)));
  const [sourceMode, setSourceMode] = useState("Texture");
  const [strokes, setStrokes] = useState(mask?.StrokeCount || 0);
  const [textureName, setTextureName] = useState<string | null>(null);
  const [generatorKey, setGeneratorKey] = useState<string | null>("MetalEdgeWear");
  const [parameters, setParameters] = useState<Record<string, number>>({ Intensity: 0.6, Softness: 0.3, Grain: 0.45 });
  const [menuOpen, setMenuOpen] = useState(false);
  const maskPresent = mask?.enabled !== false;

  const MASK_GLYPH = {
    ChevronDown: (size: number, weight: number = 2) => `<svg width="${size}" height="${size}" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="${weight}" stroke-linecap="round" stroke-linejoin="round"><path d="m6 9 6 6 6-6"/></svg>`,
    Check: (size: number, weight: number = 2) => `<svg width="${size}" height="${size}" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="${weight}" stroke-linecap="round" stroke-linejoin="round"><path d="M20 6 9 17l-5-5"/></svg>`,
    Trash: (size: number) => `<svg width="${size}" height="${size}" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M3 6h18"/><path d="M19 6v14a2 2 0 0 1-2 2H7a2 2 0 0 1-2-2V6"/><path d="M8 6V4a2 2 0 0 1 2-2h4a2 2 0 0 1 2 2v2"/></svg>`,
    Settings: (size: number) => `<svg width="${size}" height="${size}" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><circle cx="12" cy="12" r="3"/><path d="M19.4 15a1.65 1.65 0 0 0 .33 1.82l.06.06a2 2 0 0 1-2.83 2.83l-.06-.06a1.65 1.65 0 0 0-1.82-.33 1.65 1.65 0 0 0-1 1.51V21a2 2 0 0 1-4 0v-.09a1.65 1.65 0 0 0-1-1.51 1.65 1.65 0 0 0-1.82.33l-.06.06a2 2 0 0 1-2.83-2.83l.06.06a1.65 1.65 0 0 0 .33-1.82 1.65 1.65 0 0 0-1.51-1H3a2 2 0 0 1 0-4h.09a1.65 1.65 0 0 0 1.51-1 1.65 1.65 0 0 0-.33-1.82l-.06-.06a2 2 0 0 1 2.83-2.83l.06.06a1.65 1.65 0 0 0 1.82.33H9a1.65 1.65 0 0 0 1-1.51V3a2 2 0 0 1 4 0v.09a1.65 1.65 0 0 0 1 1.51 1.65 1.65 0 0 0 1.82-.33l.06-.06a2 2 0 0 1 2.83 2.83l-.06.06a1.65 1.65 0 0 0-.33 1.82V9a1.65 1.65 0 0 0 1.51 1H21a2 2 0 0 1 0 4h-.09a1.65 1.65 0 0 0-1.51 1z"/></svg>`,
    Image: (size: number) => `<svg width="${size}" height="${size}" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><rect width="18" height="18" x="3" y="3" rx="2" ry="2"/><circle cx="9" cy="9" r="2"/><path d="m21 15-3.086-3.086a2 2 0 0 0-2.828 0L6 21"/></svg>`,
    Brush: (size: number) => `<svg width="${size}" height="${size}" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="m9.06 11.9 8.07-8.06a2.85 2.85 0 1 1 4.03 4.03l-8.06 8.08"/><path d="M7.07 14.94c-1.66 0-3 1.35-3 3.02 0 1.33-2.5 1.52-2 2.02 1.08 1.1 2.49 2.02 4 2.02 2.2 0 4-1.8 4-4.04a3.01 3.01 0 0 0-3-3.02z"/></svg>`,
    RefreshCw: (size: number) => `<svg width="${size}" height="${size}" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M3 12a9 9 0 0 1 9-9 9.75 9.75 0 0 1 6.74 2.74L21 8"/><path d="M21 3v5h-5"/><path d="M21 12a9 9 0 0 1-9 9 9.75 9.75 0 0 1-6.74-2.74L3 16"/><path d="M8 16H3v5"/></svg>`,
    Plus: (size: number) => `<svg width="${size}" height="${size}" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M5 12h14"/><path d="M12 5v14"/></svg>`,
    Circle: (size: number) => `<svg width="${size}" height="${size}" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><circle cx="12" cy="12" r="10"/></svg>`
  };

  const MASK_GENERATORS = [
    { key:"CurvatureEdges", group:"Mask", label:"Curvature", note:"Convex edge wear", parameters:[
        { key:"Balance",  label:"Balance",  min:0, max:1, default:0.5  },
        { key:"Contrast", label:"Contrast", min:0, max:1, default:0.7  },
        { key:"Radius",   label:"Radius",   min:0, max:1, default:0.25 },
    ]},
    { key:"AmbientOcclusion", group:"Mask", label:"Ambient Occlusion", note:"Cavity dirt", parameters:[
        { key:"Spread",   label:"Spread",   min:0, max:1, default:0.4 },
        { key:"Contrast", label:"Contrast", min:0, max:1, default:0.6 },
    ]},
    { key:"MetalEdgeWear", group:"Wear", label:"Metal Edge Wear", note:"Curvature + grunge", parameters:[
        { key:"Intensity", label:"Intensity", min:0, max:1, default:0.6  },
        { key:"Softness",  label:"Softness",  min:0, max:1, default:0.3  },
        { key:"Grain",     label:"Grain",     min:0, max:1, default:0.45 },
    ]},
    { key:"DirtAccumulation", group:"Wear", label:"Dirt", note:"Occlusion-driven", parameters:[
        { key:"Amount", label:"Amount", min:0, max:1, default:0.5 },
        { key:"Scale",  label:"Scale",  min:0, max:1, default:0.3 },
    ]},
    { key:"PerlinNoise", group:"Procedural", label:"Perlin Noise", note:"Fractal value noise", parameters:[
        { key:"Scale",    label:"Scale",    min:0, max:1, default:0.4 },
        { key:"Octaves",  label:"Octaves",  min:0, max:1, default:0.5 },
        { key:"Contrast", label:"Contrast", min:0, max:1, default:0.5 },
    ]},
  ];
  const MASK_GROUPS = ["Mask", "Wear", "Procedural"];

  const applyGenerator = (key: string) => {
    setGeneratorKey(key);
    const chosen = MASK_GENERATORS.find(g => g.key === key);
    if (chosen) {
      const seeded: Record<string, number> = {};
      chosen.parameters.forEach(p => seeded[p.key] = p.default);
      setParameters(seeded);
    }
  };

  const activeGen = MASK_GENERATORS.find(g => g.key === generatorKey);

  return (
    <div className="MaskMount" onClick={() => { if (menuOpen) setMenuOpen(false); }}>
      <style>{`
        .MaskMount {
          --color-desk:#000000; --color-menu:#0e0e0e; --color-menu-2:#0e0e0e;
          --color-hair:#1c1c1c; --color-hair-strong:#2a2a30;
          --color-tile:#141414; --color-tile-hi:#1a1a1a; --color-tile-active:#1f1f1f;
          --color-accent:#e8e8e8; --color-on-accent:#111111;
          --color-ink:#ededed; --color-muted:#8a8a8a; --color-faint:#6a6a6a;
          --color-value-black:#000000; --color-value-unit:#141414;
          --color-track-bg:#141414; --color-track-fill:#5a5a5a; --color-knob:#ffffff;
          --color-text-value:#ffffff; --color-danger:#e05a5a;
          --color-row-hover:rgba(255,255,255,0.045);
          color:var(--color-ink);
          font-family:"Segoe UI", system-ui, sans-serif;
          font-size:12px;
          -webkit-font-smoothing:antialiased;
          display: flex; flex-direction: column; height: 100%;
          background: var(--color-menu);
          border-left: 1px solid var(--color-hair);
        }
        .MaskMount *{ box-sizing:border-box; margin:0; padding:0; }
        .MaskMount input[type="number"]::-webkit-inner-spin-button,
        .MaskMount input[type="number"]::-webkit-outer-spin-button{ -webkit-appearance:none; margin:0; }
        .MaskMount input[type="number"]{ -moz-appearance:textfield; }
        .MaskMount .MaskScroll::-webkit-scrollbar{ width:8px; }
        .MaskMount .MaskScroll::-webkit-scrollbar-thumb{ background:#26262c; border-radius:99px; }
        .MaskMount .MaskScroll::-webkit-scrollbar-track{ background:transparent; }
        .MaskMount .MaskCardBody::-webkit-scrollbar{ width:8px; }
        .MaskMount .MaskCardBody::-webkit-scrollbar-thumb{ background:#26262c; border-radius:99px; }
        .MaskMount .MaskCardBody::-webkit-scrollbar-track{ background:transparent; }

        .MaskCard{ display:flex; flex-direction:column; height: 100%; flex: 1; min-height: 0; }
        .MaskCardHead{
            display:flex; align-items:center; gap:9px; height:46px; padding:0 11px; flex:0 0 auto;
            background:var(--color-menu-2); border-bottom:1px solid var(--color-hair);
        }
        .MaskCardGlyph{
            flex-shrink:0; width:24px; height:24px; border-radius:6px;
            background:var(--color-desk); border:1px solid var(--color-hair);
            display:flex; align-items:center; justify-content:center; color:var(--color-muted);
            cursor: pointer; transition: background .1s, color .1s;
        }
        .MaskCardGlyph:hover { background: var(--color-tile); color: var(--color-ink); }
        .MaskCardTitle{
            font-size:12.5px; font-weight:600; white-space:nowrap; overflow:hidden; text-overflow:ellipsis;
        }
        .MaskCardSub{
            font-size:10px; color:var(--color-faint); margin-top:1px;
            white-space:nowrap; overflow:hidden; text-overflow:ellipsis;
        }
        .MaskCardBody{ padding:8px 8px 12px; flex:1; min-height:0; overflow-y:auto; }
        .MaskSection{
            background:var(--color-menu); border:1px solid var(--color-hair);
            border-radius:10px; margin-bottom:5px; transition:all .2s; overflow:visible;
        }
        .MaskSection.Collapsed{ overflow:hidden; }
        .MaskSectionHead{
            display:flex; align-items:center; gap:8px; height:29px; padding:0 9px;
            background:var(--color-menu-2); border-bottom:1px solid var(--color-hair);
            cursor:pointer; user-select:none; transition:background-color .15s;
        }
        .MaskSectionHead:hover{ background:var(--color-tile); }
        .MaskSection.Collapsed .MaskSectionHead{ border-bottom-color:transparent; }
        .MaskChevron{
            width:9px; height:9px; color:var(--color-faint);
            display:flex; align-items:center; justify-content:center;
            transition:transform .2s;
        }
        .MaskSection.Collapsed .MaskChevron{ transform:rotate(-90deg); }
        .MaskDot{
            flex-shrink:0; width:6px; height:6px; border-radius:9999px;
            background:var(--color-accent); transition:all .2s;
        }
        .MaskSection.Collapsed .MaskDot{ transform:scale(.75); opacity:.5; }
        .MaskSectionName{ flex:1; font-size:11px; font-weight:500; color:var(--color-ink); }
        .MaskSectionTail{
            font-size:9.5px; letter-spacing:.4px; text-transform:uppercase;
            color:var(--color-faint); opacity:.45; transition:all .2s;
        }
        .MaskSection.Collapsed .MaskSectionTail{ opacity:0; transform:translateX(8px); }

        .MaskFold{ display:grid; grid-template-rows:1fr; transition:all .3s ease-out; }
        .MaskSection.Collapsed .MaskFold{ grid-template-rows:0fr; }
        .MaskFoldClip{ overflow:visible; min-height:0; }
        .MaskSection.Collapsed .MaskFoldClip{ overflow:hidden; }
        .MaskFoldBody{
            padding:6px 9px 8px; opacity:1; transform:translateY(0);
            transition:all .2s; transition-delay:75ms;
        }
        .MaskSection.Collapsed .MaskFoldBody{
            opacity:0; transform:translateY(-6px); transition-delay:0ms;
        }
        .MaskRule{ height:1px; background:var(--color-hair); margin:12px 4px; }

        .MaskRow{
            display:grid; grid-template-columns:88px minmax(0,1fr); column-gap:10px;
            align-items:center; min-height:32px;
        }
        .MaskRowLabel{ font-size:11.5px; color:var(--color-muted); }
        .MaskRowField{ min-width:0; }

        .MaskSegments{
            display:flex; height:26px; background:var(--color-tile);
            border:1px solid var(--color-hair); border-radius:9999px; overflow:hidden;
        }
        .MaskSegment{
            flex:1; display:flex; align-items:center; justify-content:center;
            font-size:10.5px; cursor:pointer; user-select:none;
            color:var(--color-muted); transition:background-color .15s, color .15s;
        }
        .MaskSegment:hover{ background:var(--color-tile-hi); color:var(--color-ink); }
        .MaskSegment.On{
            background:var(--color-accent); color:var(--color-on-accent); font-weight:600;
        }
        .MaskSegment.On:hover{ background:var(--color-accent); color:var(--color-on-accent); }

        .MaskSlider{ display:flex; align-items:center; gap:9px; }
        .MaskCapsule{
            display:flex; align-items:center; overflow:hidden; height:26px;
            background:var(--color-value-black); border-radius:9999px; flex-shrink:0; width:92px;
        }
        .MaskCapsuleField{ flex:1; min-width:0; display:flex; align-items:center; justify-content:center; }
        .MaskCapsuleField input{
            width:100%; border:0; background:transparent; outline:none; text-align:center;
            color:var(--color-text-value); font-weight:600; font-size:12.6px;
            font-variant-numeric:tabular-nums; font-family:inherit;
        }
        .MaskCapsuleUnit{
            flex-shrink:0; width:30px; align-self:stretch;
            display:flex; align-items:center; justify-content:center;
            font-size:10.5px; color:var(--color-muted);
            background:var(--color-value-unit); border-left:1px solid var(--color-hair);
        }
        .MaskTrack{
            position:relative; flex:1; height:19px; background:var(--color-track-bg);
            border-radius:9999px; cursor:pointer;
        }
        .MaskTrackFill{
            position:absolute; top:0; bottom:0; left:0;
            background:var(--color-track-fill); border-radius:9999px; pointer-events:none;
        }
        .MaskKnob{
            position:absolute; top:50%; width:21px; height:21px; margin-left:-10.5px;
            transform:translateY(-50%); border-radius:9999px; background:var(--color-knob);
            box-shadow:0 0 0 1px rgba(0,0,0,0.35); transition:box-shadow .15s; pointer-events:none;
        }
        .MaskTrack:hover .MaskKnob{
            box-shadow:0 0 0 1px rgba(0,0,0,0.35), 0 0 0 4px rgba(255,255,255,0.07);
        }

        .MaskSlot{
            display:flex; align-items:center; gap:9px; padding:6px;
            background:var(--color-tile); border:1px solid var(--color-hair); border-radius:9px;
        }
        .MaskSlotPreview{
            flex-shrink:0; width:38px; height:38px; border-radius:6px;
            border:1px solid var(--color-hair);
            display:flex; align-items:center; justify-content:center;
            color:var(--color-faint); background:#0a0a0a;
            background-image:
                linear-gradient(45deg,#1a1a1a 25%,transparent 25%,transparent 75%,#1a1a1a 75%),
                linear-gradient(45deg,#1a1a1a 25%,transparent 25%,transparent 75%,#1a1a1a 75%);
            background-size:8px 8px; background-position:0 0, 4px 4px;
        }
        .MaskSlotPreview.Painted{
            color:rgba(0,0,0,.6); background:#e8e8e8; background-image:none;
        }
        .MaskSlotText{ flex:1; min-width:0; }
        .MaskSlotName{
            font-size:11px; white-space:nowrap; overflow:hidden; text-overflow:ellipsis;
        }
        .MaskSlotNote{
            font-size:9.5px; color:var(--color-faint); margin-top:2px; font-variant-numeric:tabular-nums;
        }
        .MaskSlotVerbs{ display:flex; gap:4px; flex-shrink:0; }
        .MaskVerb{
            width:24px; height:24px; border-radius:6px; border:1px solid var(--color-hair);
            background:var(--color-menu); color:var(--color-muted);
            display:flex; align-items:center; justify-content:center;
            transition:all .15s; cursor:pointer;
        }
        .MaskVerb:hover{
            background:var(--color-tile-hi); color:var(--color-ink); border-color:var(--color-hair-strong);
        }
        .MaskVerb:active{ transform:scale(.9); }
        .MaskVerb.Danger:hover{
            background:rgba(224,90,90,0.16); color:var(--color-danger); border-color:rgba(224,90,90,0.4);
        }
        .MaskVerb.Off{
            color:rgba(138,138,138,.3); cursor:default; background:var(--color-menu);
        }
        .MaskVerb.Off:hover{
            background:var(--color-menu); color:rgba(138,138,138,.3); border-color:var(--color-hair);
        }
        .MaskSlotCaption{
            font-size:9px; letter-spacing:.7px; text-transform:uppercase;
            color:var(--color-faint); font-weight:600; padding:8px 1px 2px;
        }
        .MaskImport{
            width:100%; display:flex; align-items:center; justify-content:center; gap:7px;
            height:44px; margin-top:4px; background:transparent;
            border:1px dashed var(--color-hair); border-radius:9px;
            color:var(--color-muted); font-size:11px; font-family:inherit;
            transition:border-color .15s, color .15s, background-color .15s; cursor:pointer;
        }
        .MaskImport:hover{
            border-color:var(--color-accent); color:var(--color-ink); background:rgba(255,255,255,.05);
        }
        .MaskSlotTop{ margin-top:4px; }

        .MaskCombo{ position:relative; }
        .MaskComboHead{
            display:flex; align-items:center; gap:8px; height:26px; padding:0 4px 0 10px;
            background:var(--color-tile); border:1px solid var(--color-hair); border-radius:9999px;
            cursor:pointer; transition:border-color .15s, background-color .15s;
        }
        .MaskComboHead:hover,
        .MaskCombo.Open .MaskComboHead{
            border-color:var(--color-hair-strong); background:var(--color-tile-hi);
        }
        .MaskComboName{
            flex:1; font-size:11px; color:var(--color-ink);
            white-space:nowrap; overflow:hidden; text-overflow:ellipsis;
        }
        .MaskComboCaret{
            flex-shrink:0; width:16px; color:var(--color-faint);
            display:flex; align-items:center; justify-content:center; transition:transform .15s;
        }
        .MaskCombo.Open .MaskComboCaret{ transform:rotate(180deg); }
        .MaskMenu{
            position:absolute; left:0; top:30px; z-index:50; width:100%; min-width:196px;
            background:#141416; border:1px solid var(--color-hair-strong); border-radius:9px;
            padding:6px; box-shadow:0 14px 34px rgba(0,0,0,0.7);
            max-height:330px; overflow-y:auto;
        }
        .MaskMenuGroup{
            font-size:9.5px; color:var(--color-faint); text-transform:uppercase;
            letter-spacing:.6px; padding:5px 7px; font-weight:600;
        }
        .MaskMenuItem{
            display:flex; align-items:center; gap:8px; padding:6px 8px; border-radius:6px;
            cursor:pointer; font-size:12px; color:var(--color-ink); transition:all .15s;
        }
        .MaskMenuItem:hover{ background:var(--color-row-hover); padding-left:11px; }
        .MaskMenuNote{
            margin-left:auto; font-size:9.5px; color:var(--color-faint); letter-spacing:.3px;
        }
        .MaskMenuTick{ margin-left:auto; color:var(--color-accent); display:flex; align-items:center; }
        .MaskMenuRule{ height:1px; background:var(--color-hair); margin:5px 3px; }
        .MaskMenuRule:last-child{ display:none; }

        .MaskParams{
            margin-top:4px; padding-top:4px; border-top:1px solid var(--color-hair);
            animation:MaskParamsIn .2s ease-out;
        }
        @keyframes MaskParamsIn{ from{ opacity:0; transform:translateY(-4px); } to{ opacity:1; transform:none; } }
        .MaskParamsHead{ display:flex; align-items:center; gap:6px; padding:2px 1px 4px; }
        .MaskParamsNote{
            font-size:9.5px; letter-spacing:.6px; text-transform:uppercase;
            color:var(--color-faint); font-weight:600; flex:1;
        }
        .MaskPreviewRow{ display:flex; align-items:center; gap:8px; margin-top:8px; padding:6px 1px 0; }
        .MaskPreviewTile{
            flex-shrink:0; width:34px; height:34px; border-radius:7px;
            border:1px solid var(--color-hair); background:#0a0a0a;
            background-image:
                linear-gradient(45deg,#1a1a1a 25%,transparent 25%,transparent 75%,#1a1a1a 75%),
                linear-gradient(45deg,#1a1a1a 25%,transparent 25%,transparent 75%,#1a1a1a 75%);
            background-size:8px 8px; background-position:0 0, 4px 4px;
        }
        .MaskPreviewShade{ width:100%; height:100%; border-radius:6px; }
        .MaskPreviewName{ font-size:10.5px; color:var(--color-muted); }
        .MaskPreviewMeta{
            font-size:9.5px; color:var(--color-faint); margin-top:2px; font-variant-numeric:tabular-nums;
        }
      `}</style>
      
      <div className="MaskCard">
        <div className="MaskCardHead">
          <div className="MaskCardGlyph" onClick={onBack} title="Back" dangerouslySetInnerHTML={{ __html: MASK_GLYPH.ChevronDown(14, 2.5).replace('<svg', '<svg style="transform:rotate(90deg)"') }}></div>
          <div style={{flex: 1, minWidth: 0}}>
            <div className="MaskCardTitle">Mask Property</div>
            <div className="MaskCardSub">{layerName || "Layer Mask"} · Grayscale</div>
          </div>
        </div>
        <div className="MaskCardBody">
          {!maskPresent ? (
            <div className="MaskSlotCaption" style={{letterSpacing: '.4px', textTransform: 'none', fontWeight: 400, padding: '14px 4px', color: 'var(--color-faint)', fontSize: '11px', lineHeight: 1.6}}>
              {layerName || "Layer"} carries no mask. Add one in the layer stack.
            </div>
          ) : (
            <div className={`MaskSection ${collapsed ? 'Collapsed' : ''}`}>
              <div className="MaskSectionHead" onClick={() => { setCollapsed(!collapsed); setMenuOpen(false); }}>
                <span className="MaskChevron" dangerouslySetInnerHTML={{ __html: MASK_GLYPH.ChevronDown(12, 2.5) }}></span>
                <span className="MaskDot"></span>
                <span className="MaskSectionName">Mask</span>
                <span className="MaskSectionTail">{sourceMode}</span>
              </div>
              <div className="MaskFold">
                <div className="MaskFoldClip">
                  <div className="MaskFoldBody">
                    <PropertyRow label="Base Mask">
                      <Segments options={["White", "Black"]} current={baseMask} onChange={setBaseMask} />
                    </PropertyRow>
                    <PropertyRow label="Invert">
                      <Segments options={["Off", "On"]} current={invert ? "On" : "Off"} onChange={(v) => setInvert(v === "On")} />
                    </PropertyRow>
                    <PropertyRow label="Strength">
                      <MaskSlider value={strength} min={0} max={1} onChange={setStrength} />
                    </PropertyRow>
                    
                    <div className="MaskRule"></div>
                    
                    <PropertyRow label="Source">
                      <Segments options={["Texture", "Generator"]} current={sourceMode} onChange={(v) => { setSourceMode(v); setMenuOpen(false); }} />
                    </PropertyRow>

                    {sourceMode === "Texture" ? (
                      <div className="MaskSlotTop">
                        <div className="MaskSlot">
                          <div className={`MaskSlotPreview ${strokes > 0 ? 'Painted' : ''}`} dangerouslySetInnerHTML={{ __html: strokes > 0 ? MASK_GLYPH.Brush(18) : MASK_GLYPH.Image(18) }}></div>
                          <div className="MaskSlotText">
                            <div className="MaskSlotName">{strokes > 0 ? "Painted mask" : "No mask strokes yet"}</div>
                            <div className="MaskSlotNote">{strokes > 0 ? `${strokes} strokes · 2048 × 2048 atlas` : "Atlas allocates on first stroke"}</div>
                          </div>
                          <div className="MaskSlotVerbs">
                            {!strokes && <MaskVerb svg={MASK_GLYPH.Brush(13)} title="Simulate paint stroke" onClick={() => setStrokes(1)} />}
                            <MaskVerb svg={MASK_GLYPH.Trash(13)} title="Erase painted strokes" danger disabled={!strokes} onClick={() => setStrokes(0)} />
                          </div>
                        </div>
                        <div className="MaskSlotCaption">Imported base {textureName ? "" : "— optional"}</div>
                        {textureName ? (
                          <div className="MaskSlot">
                            <div className="MaskSlotPreview" dangerouslySetInnerHTML={{ __html: MASK_GLYPH.Image(18) }}></div>
                            <div className="MaskSlotText">
                              <div className="MaskSlotName">{textureName}</div>
                              <div className="MaskSlotNote">2048 × 2048 · Linear 8</div>
                            </div>
                            <div className="MaskSlotVerbs">
                              <MaskVerb svg={MASK_GLYPH.RefreshCw(13)} title="Replace imported base" onClick={() => setTextureName("mask_import_02_replaced.png")} />
                              <MaskVerb svg={MASK_GLYPH.Trash(13)} title="Delete imported base" danger onClick={() => setTextureName(null)} />
                            </div>
                          </div>
                        ) : (
                          <button className="MaskImport" onClick={() => setTextureName("mask_import_01.png")}>
                            <span dangerouslySetInnerHTML={{ __html: MASK_GLYPH.Plus(13) }}></span>
                            <span>Import base mask</span>
                          </button>
                        )}
                      </div>
                    ) : (
                      <div>
                        <PropertyRow label="Generator">
                          <div className={`MaskCombo ${menuOpen ? 'Open' : ''}`}>
                            <div className="MaskComboHead" onClick={(e) => { e.stopPropagation(); setMenuOpen(!menuOpen); }}>
                              <span className="MaskComboName">{activeGen ? activeGen.label : "Choose generator"}</span>
                              <span className="MaskComboCaret" dangerouslySetInnerHTML={{ __html: MASK_GLYPH.ChevronDown(12) }}></span>
                            </div>
                            {menuOpen && (
                              <div className="MaskMenu MaskScroll">
                                {MASK_GROUPS.map(group => {
                                  const members = MASK_GENERATORS.filter(g => g.group === group);
                                  if (!members.length) return null;
                                  return (
                                    <React.Fragment key={group}>
                                      <div className="MaskMenuGroup">{group}</div>
                                      {members.map(g => (
                                        <div key={g.key} className="MaskMenuItem" onClick={(e) => { e.stopPropagation(); applyGenerator(g.key); setMenuOpen(false); }}>
                                          <span>{g.label}</span>
                                          {g.key === generatorKey ? (
                                            <span className="MaskMenuTick" dangerouslySetInnerHTML={{ __html: MASK_GLYPH.Check(13, 2.5) }}></span>
                                          ) : (
                                            <span className="MaskMenuNote">{g.note}</span>
                                          )}
                                        </div>
                                      ))}
                                      <div className="MaskMenuRule"></div>
                                    </React.Fragment>
                                  );
                                })}
                              </div>
                            )}
                          </div>
                        </PropertyRow>
                        {activeGen && (
                          <div className="MaskParams">
                            <div className="MaskParamsHead">
                              <span className="MaskParamsNote">{activeGen.note}</span>
                              <MaskVerb svg={MASK_GLYPH.Settings(13)} title="Reset parameters" onClick={() => applyGenerator(generatorKey!)} />
                              <MaskVerb svg={MASK_GLYPH.Trash(13)} title="Remove generator" danger onClick={() => setGeneratorKey(null)} />
                            </div>
                            {activeGen.parameters.map(p => {
                              const val = parameters[p.key] !== undefined ? parameters[p.key] : p.default;
                              return (
                                <PropertyRow key={p.key} label={p.label}>
                                  <MaskSlider value={val} min={p.min} max={p.max} onChange={(v) => setParameters({...parameters, [p.key]: v})} />
                                </PropertyRow>
                              );
                            })}
                          </div>
                        )}
                      </div>
                    )}
                    
                    <div className="MaskPreviewRow">
                      <div className="MaskPreviewTile">
                        <div className="MaskPreviewShade" style={{
                          backgroundColor: baseMask === "White" ? `rgba(255,255,255,${strength})` : `rgba(0,0,0,${strength})`,
                          filter: invert ? "invert(1)" : "none"
                        }}></div>
                      </div>
                      <div style={{minWidth: 0}}>
                        <div className="MaskPreviewName">Mask preview</div>
                        <div className="MaskPreviewMeta">Material atlas · A</div>
                      </div>
                    </div>
                  </div>
                </div>
              </div>
            </div>
          )}
        </div>
      </div>
    </div>
  );
}

export function LayerInspectorPane({ layerId, target, onTargetChange, onBack }: { 
  layerId: number, target: 'layer' | 'mask', onTargetChange: (t: 'layer'|'mask') => void, onBack?: () => void 
}) {
  const [layers, setLayers] = useState(mockLayers);
  const layer = layers.find(x => x.id === layerId) || layers[0];

  if (target === 'mask') {
    return <MaskPropertyPanel layerName={layer.name} mask={layer.mask} onBack={onBack} />;
  }

  return <ChannelPropertyPanel layerName={layer.name} onBack={onBack} />;
}



function LayersIcon({ size=16 }: { size?: number }) {
  return (
    <svg width={size} height={size} viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round">
      <polygon points="12 2 2 7 12 12 22 7 12 2"></polygon>
      <polyline points="2 12 12 17 22 12"></polyline>
      <polyline points="2 17 12 22 22 17"></polyline>
    </svg>
  );
}
