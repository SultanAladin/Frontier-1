// Hosts the original Frontier World Mirror MaterialPanel in the Component Viewer inspector.
import React, { useState } from 'react';
import { createRoot } from 'react-dom/client';
import MaterialPanel from './MaterialPanel.jsx';
import { ResolveMaterial } from './MaterialSpecification.js';

let meshPromise;
async function loadShaderBall() {
  if (!meshPromise) meshPromise = fetch(new URL('../../FrontierWorldMirror/ShaderBall.mesh', import.meta.url)).then(async (response) => {
    if (!response.ok) throw new Error('Could not load the World Mirror shader ball');
    const data = new Uint8Array(await response.arrayBuffer());
    let binary = '';
    for (let i = 0; i < data.length; i += 0x8000) binary += String.fromCharCode(...data.subarray(i, i + 0x8000));
    window.NativeAssets ||= {};
    window.NativeAssets.ShaderBall = window.btoa(binary);
  }).catch((error) => { meshPromise = null; throw error; });
  return meshPromise;
}

function MaterialCard({ component, asset, slots, change }) {
  const [family, setFamily] = useState(slots[0].family);
  const [expanded, setExpanded] = useState(false);
  const [, repaint] = useState(0);
  const slot = slots.find((s) => s.family === family) || slots[0];
  component.props.materialEdits ||= {};
  const material = component.props.materialEdits[slot.family] || ResolveMaterial({
    Albedo: component.props.slots[slot.family] || slot.color, Roughness: slot.roughness,
    Material: { Name: slot.name, Channels: { metallic: { Fill: slot.metalness } } },
  });
  function apply(key, value) {
    if (key !== 'Material') return;
    component.props.materialEdits[slot.family] = value;
    if (value.Channels.colour.Source === 'Fill') component.props.slots[slot.family] = value.Channels.colour.Fill;
    change(value, slot.family);
    repaint((n) => n + 1);
  }
  return <section className="world-material-host">
    {slots.length > 1 && <label className="material-slot-picker">Surface slot <select aria-label="Surface slot" value={family} onChange={(e) => setFamily(+e.target.value)}>{slots.map((s) => <option key={s.family} value={s.family}>{s.name}</option>)}</select></label>}
    <MaterialPanel Subject={{ Id: component.id, Name: asset.name }} Values={{ Material: material }} Change={apply} Compact={!expanded} Expand={() => setExpanded((v) => !v)} />
    {expanded && <button className="tool" onClick={() => setExpanded(false)}>Close ShaderEditor</button>}
    <p className="material-preview-note">The World Mirror card previews all channels on its shader ball. The component viewport currently renders fill colour, roughness and metallic only; other sources and channels are retained as authoring data.</p>
  </section>;
}
export async function mountWorldMaterial(host, props) {
  await loadShaderBall();
  if (!host.isConnected) return null;
  const root = createRoot(host);
  root.render(<MaterialCard {...props} />);
  return () => root.unmount();
}
