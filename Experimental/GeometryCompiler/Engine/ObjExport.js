// Text exporters for compiled geometry. Pure functions: they return strings and never touch the DOM.

// Wavefront OBJ with one `vt` per triangle corner, so UV seams survive: vertices stay welded, texture
// coordinates are split. Round-trips through parseObj (which ignores the `/vt` part of each face corner).
export function formatObj(positions, indices, uvs, name = 'geometry') {
  const lines = ['# Frontier Cluster LOD Studio — geometry compiled with UV charts', `o ${name.replace(/\s+/g, '_')}`];
  for (let i = 0; i < positions.length; i += 3) lines.push(`v ${positions[i].toFixed(6)} ${positions[i + 1].toFixed(6)} ${positions[i + 2].toFixed(6)}`);
  for (let s = 0; s < uvs.length; s += 2) lines.push(`vt ${uvs[s].toFixed(6)} ${uvs[s + 1].toFixed(6)}`);
  for (let t = 0; t < indices.length; t += 3) {
    const corner = (k) => `${indices[t + k] + 1}/${t + k + 1}`;
    lines.push(`f ${corner(0)} ${corner(1)} ${corner(2)}`);
  }
  return lines.join('\n') + '\n';
}

// UV layout sheet: unit square, chart borders in grey, seams in amber, mesh borders in white.
export function formatUvSvg(layout, uvs, name = 'geometry') {
  const px = (u) => (u * 1024).toFixed(2), py = (v) => ((1 - v) * 1024).toFixed(2);
  const segments = (slots) => {
    let d = '';
    for (const slot of slots) {
      const t = (slot / 3) | 0, k = slot % 3;
      const a = (t * 3 + k) * 2, b = (t * 3 + ((k + 1) % 3)) * 2;
      d += `M${px(uvs[a])} ${py(uvs[a + 1])}L${px(uvs[b])} ${py(uvs[b + 1])}`;
    }
    return d;
  };
  return [
    '<svg xmlns="http://www.w3.org/2000/svg" viewBox="-16 -16 1056 1056" width="1024" height="1024">',
    `<title>${name} UV layout · ${layout.charts.length} charts</title>`,
    '<rect x="-16" y="-16" width="1056" height="1056" fill="#101010"/>',
    '<rect x="0" y="0" width="1024" height="1024" fill="none" stroke="#3a3a3a" stroke-width="2"/>',
    `<path d="${segments(layout.borders)}" stroke="#f0f0f0" stroke-width="2" fill="none"/>`,
    `<path d="${segments(layout.seams)}" stroke="#ffb238" stroke-width="1.5" fill="none"/>`,
    '</svg>',
    '',
  ].join('\n');
}
