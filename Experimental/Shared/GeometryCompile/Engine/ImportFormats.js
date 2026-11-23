// Import readers for the Geometry Compile step "Source".
//
// Every reader returns raw triangles in the file's own units and axes:
//   { positions: Float32Array, indices: Uint32Array, families: Uint32Array | null, format, notes: string[] }
// Nothing is welded, centred or scaled here. Units, up axis and welding are compile settings (CompileSteps.js).
//
// Supported: OBJ (v / f, fan-triangulated), STL (binary and ASCII), PLY (ASCII and binary little-endian,
// vertex x y z + face lists), glTF 2.0 (.gltf with embedded data URIs) and GLB (node transforms applied),
// and Frontier FCT1 facet streams (positions, facets, CourseMaterial families).
// FBX is listed by the UI but not read: it is a closed binary format and has no reader in this build.

export const IMPORT_FORMATS = [
  { ext: 'obj', label: 'OBJ', reads: true },
  { ext: 'gltf', label: 'glTF', reads: true },
  { ext: 'glb', label: 'GLB', reads: true },
  { ext: 'stl', label: 'STL', reads: true },
  { ext: 'ply', label: 'PLY', reads: true },
  { ext: 'facet', label: 'FCT1', reads: true },
  { ext: 'fbx', label: 'FBX', reads: false },
];

export function formatOf(fileName) {
  const ext = String(fileName).toLowerCase().split('.').pop();
  return IMPORT_FORMATS.find((f) => f.ext === ext) || null;
}

// bytes: ArrayBuffer. Returns the raw triangle set described above.
export function readGeometry(fileName, bytes) {
  const format = formatOf(fileName);
  if (!format) throw new Error(`Unknown file type: ${fileName}`);
  if (!format.reads) throw new Error(`${format.label} has no reader in this build. Export OBJ, glTF/GLB, STL or PLY instead.`);
  switch (format.ext) {
    case 'obj': return readObj(new TextDecoder().decode(bytes));
    case 'stl': return readStl(bytes);
    case 'ply': return readPly(bytes);
    case 'gltf': return readGltf(JSON.parse(new TextDecoder().decode(bytes)), null);
    case 'glb': return readGlb(bytes);
    case 'facet': return readFacet(bytes);
  }
  throw new Error(`No reader for ${format.label}`);
}

// ---------------------------------------------------------------------------------------------------------- OBJ
export function readObj(text) {
  const raw = [], faces = [], families = [], notes = [];
  const groups = new Map();
  let family = 0, start = 0;
  const length = text.length;
  while (start < length) {
    let end = text.indexOf('\n', start);
    if (end < 0) end = length;
    const line = text.slice(start, end).trim();
    start = end + 1;
    if (!line || line[0] === '#') continue;
    const head = line.split(/\s+/, 1)[0];
    if (head === 'v') {
      const p = line.slice(2).trim().split(/\s+/);
      raw.push(+p[0], +p[1], +p[2]);
    } else if (head === 'f') {
      const count = raw.length / 3;
      const ids = line.slice(2).trim().split(/\s+/).map((p) => {
        const v = parseInt(p, 10);
        return v < 0 ? count + v : v - 1;
      });
      for (let i = 1; i + 1 < ids.length; i++) { faces.push(ids[0], ids[i], ids[i + 1]); families.push(family); }
    } else if (head === 'usemtl' || head === 'g' || head === 'o') {
      if (head !== 'usemtl') continue;
      const name = line.slice(head.length).trim() || 'default';
      if (!groups.has(name)) groups.set(name, groups.size);
      family = groups.get(name);
    }
  }
  if (!raw.length || !faces.length) throw new Error('OBJ has no triangles');
  if (groups.size > 1) notes.push(`${groups.size} material groups (usemtl) kept as material slots`);
  return {
    positions: new Float32Array(raw), indices: new Uint32Array(faces),
    families: groups.size > 1 ? new Uint32Array(families) : null,
    slotNames: groups.size > 1 ? [...groups.keys()] : null, format: 'OBJ', notes,
  };
}

// ---------------------------------------------------------------------------------------------------------- STL
export function readStl(bytes) {
  const view = new DataView(bytes);
  const isBinary = bytes.byteLength >= 84 && 84 + view.getUint32(80, true) * 50 === bytes.byteLength;
  if (isBinary) {
    const count = view.getUint32(80, true);
    const positions = new Float32Array(count * 9);
    for (let t = 0; t < count; t++) {
      const at = 84 + t * 50 + 12;
      for (let k = 0; k < 9; k++) positions[t * 9 + k] = view.getFloat32(at + k * 4, true);
    }
    return { positions, indices: sequence(count * 3), families: null, format: 'STL (binary)', notes: ['STL stores loose triangles: weld to share vertices'] };
  }
  const text = new TextDecoder().decode(bytes);
  const raw = [];
  for (const m of text.matchAll(/vertex\s+(\S+)\s+(\S+)\s+(\S+)/g)) raw.push(+m[1], +m[2], +m[3]);
  if (raw.length < 9) throw new Error('STL has no triangles');
  const usable = raw.length - (raw.length % 9);
  return { positions: new Float32Array(raw.slice(0, usable)), indices: sequence(usable / 3), families: null, format: 'STL (ASCII)', notes: ['STL stores loose triangles: weld to share vertices'] };
}

// ---------------------------------------------------------------------------------------------------------- PLY
export function readPly(bytes) {
  const u8 = new Uint8Array(bytes);
  const headerEnd = findSequence(u8, 'end_header');
  if (headerEnd < 0) throw new Error('PLY has no end_header');
  let body = headerEnd + 'end_header'.length;
  if (u8[body] === 13) body++;
  if (u8[body] === 10) body++;
  const header = new TextDecoder().decode(u8.subarray(0, headerEnd)).split(/\r?\n/);
  let format = 'ascii';
  const elements = [];
  for (const line of header) {
    const p = line.trim().split(/\s+/);
    if (p[0] === 'format') format = p[1];
    else if (p[0] === 'element') elements.push({ name: p[1], count: +p[2], props: [] });
    else if (p[0] === 'property' && elements.length) {
      const el = elements[elements.length - 1];
      if (p[1] === 'list') el.props.push({ list: true, countType: p[2], type: p[3], name: p[4] });
      else el.props.push({ type: p[1], name: p[2] });
    }
  }
  if (format !== 'ascii' && format !== 'binary_little_endian') throw new Error(`PLY ${format} is not read in this build`);
  const raw = [], faces = [];
  const size = { char: 1, uchar: 1, int8: 1, uint8: 1, short: 2, ushort: 2, int16: 2, uint16: 2, int: 4, uint: 4, int32: 4, uint32: 4, float: 4, float32: 4, double: 8, float64: 8 };
  if (format === 'ascii') {
    const tokens = new TextDecoder().decode(u8.subarray(body)).trim().split(/\s+/);
    let t = 0;
    for (const el of elements) for (let i = 0; i < el.count; i++) {
      const values = {};
      for (const pr of el.props) {
        if (pr.list) { const n = +tokens[t++]; values[pr.name] = tokens.slice(t, t + n).map(Number); t += n; }
        else values[pr.name] = +tokens[t++];
      }
      collectPly(el, values, raw, faces);
    }
  } else {
    const view = new DataView(bytes);
    let at = body;
    const read = (type) => {
      let v;
      switch (type) {
        case 'char': case 'int8': v = view.getInt8(at); break;
        case 'uchar': case 'uint8': v = view.getUint8(at); break;
        case 'short': case 'int16': v = view.getInt16(at, true); break;
        case 'ushort': case 'uint16': v = view.getUint16(at, true); break;
        case 'int': case 'int32': v = view.getInt32(at, true); break;
        case 'uint': case 'uint32': v = view.getUint32(at, true); break;
        case 'float': case 'float32': v = view.getFloat32(at, true); break;
        default: v = view.getFloat64(at, true);
      }
      at += size[type] || 4;
      return v;
    };
    for (const el of elements) for (let i = 0; i < el.count; i++) {
      const values = {};
      for (const pr of el.props) {
        if (pr.list) { const n = read(pr.countType); const list = new Array(n); for (let k = 0; k < n; k++) list[k] = read(pr.type); values[pr.name] = list; }
        else values[pr.name] = read(pr.type);
      }
      collectPly(el, values, raw, faces);
    }
  }
  if (!raw.length || !faces.length) throw new Error('PLY has no faces');
  return { positions: new Float32Array(raw), indices: new Uint32Array(faces), families: null, format: `PLY (${format === 'ascii' ? 'ASCII' : 'binary'})`, notes: [] };
}

function collectPly(el, values, raw, faces) {
  if (el.name === 'vertex') raw.push(values.x, values.y, values.z);
  else if (el.name === 'face') {
    const ids = values.vertex_indices || values.vertex_index || [];
    for (let i = 1; i + 1 < ids.length; i++) faces.push(ids[0], ids[i], ids[i + 1]);
  }
}

// ----------------------------------------------------------------------------------------------------- glTF / GLB
export function readGlb(bytes) {
  const view = new DataView(bytes);
  if (view.getUint32(0, true) !== 0x46546c67) throw new Error('Not a GLB file');
  let at = 12, json = null, bin = null;
  while (at < bytes.byteLength) {
    const length = view.getUint32(at, true), type = view.getUint32(at + 4, true);
    const chunk = bytes.slice(at + 8, at + 8 + length);
    if (type === 0x4e4f534a) json = JSON.parse(new TextDecoder().decode(chunk));
    else if (type === 0x004e4942) bin = chunk;
    at += 8 + length;
  }
  if (!json) throw new Error('GLB has no JSON chunk');
  const result = readGltf(json, bin);
  result.format = 'GLB';
  return result;
}

export function readGltf(json, glbBinary) {
  const buffers = (json.buffers || []).map((b, i) => {
    if (b.uri === undefined) return glbBinary;
    if (b.uri.startsWith('data:')) return decodeDataUri(b.uri);
    throw new Error(`glTF buffer ${i} is an external file (${b.uri}); import a .glb or embedded .gltf`);
  });
  const accessor = (index) => {
    const a = json.accessors[index];
    const bv = json.bufferViews[a.bufferView];
    const comps = { SCALAR: 1, VEC2: 2, VEC3: 3, VEC4: 4, MAT4: 16 }[a.type];
    const Typed = { 5120: Int8Array, 5121: Uint8Array, 5122: Int16Array, 5123: Uint16Array, 5125: Uint32Array, 5126: Float32Array }[a.componentType];
    const buffer = buffers[bv.buffer];
    const offset = (bv.byteOffset || 0) + (a.byteOffset || 0);
    const stride = bv.byteStride || comps * Typed.BYTES_PER_ELEMENT;
    const out = new Float64Array(a.count * comps);
    const view = new DataView(buffer);
    const get = { 5120: 'getInt8', 5121: 'getUint8', 5122: 'getInt16', 5123: 'getUint16', 5125: 'getUint32', 5126: 'getFloat32' }[a.componentType];
    for (let i = 0; i < a.count; i++) for (let k = 0; k < comps; k++) out[i * comps + k] = view[get](offset + i * stride + k * Typed.BYTES_PER_ELEMENT, true);
    return out;
  };
  const positions = [], indices = [], families = [], slotNames = [];
  const materialSlot = new Map();
  const visit = (nodeIndex, parent) => {
    const node = json.nodes[nodeIndex];
    const local = node.matrix ? node.matrix.slice() : composeTrs(node.translation, node.rotation, node.scale);
    const world = multiply4(parent, local);
    if (node.mesh !== undefined) {
      for (const prim of json.meshes[node.mesh].primitives) {
        if (prim.mode !== undefined && prim.mode !== 4) continue;
        const p = accessor(prim.attributes.POSITION);
        const base = positions.length / 3;
        for (let i = 0; i < p.length; i += 3) {
          const x = p[i], y = p[i + 1], z = p[i + 2];
          positions.push(world[0] * x + world[4] * y + world[8] * z + world[12], world[1] * x + world[5] * y + world[9] * z + world[13], world[2] * x + world[6] * y + world[10] * z + world[14]);
        }
        const ids = prim.indices !== undefined ? accessor(prim.indices) : sequence(p.length / 3);
        const key = prim.material ?? -1;
        if (!materialSlot.has(key)) { materialSlot.set(key, materialSlot.size); slotNames.push(key >= 0 ? (json.materials?.[key]?.name || `Material ${key}`) : 'Default'); }
        const fam = materialSlot.get(key);
        for (let i = 0; i + 2 < ids.length; i += 3) { indices.push(base + ids[i], base + ids[i + 1], base + ids[i + 2]); families.push(fam); }
      }
    }
    for (const child of node.children || []) visit(child, world);
  };
  const scene = json.scenes ? json.scenes[json.scene || 0] : { nodes: (json.nodes || []).map((_, i) => i) };
  for (const n of scene.nodes) visit(n, identity4());
  if (!indices.length) throw new Error('glTF has no triangle primitives');
  return {
    positions: new Float32Array(positions), indices: new Uint32Array(indices),
    families: slotNames.length > 1 ? new Uint32Array(families) : null, slotNames: slotNames.length > 1 ? slotNames : null,
    format: 'glTF', notes: slotNames.length > 1 ? [`${slotNames.length} materials kept as material slots`] : [],
  };
}

// --------------------------------------------------------------------------------------------------------- FCT1
// Frontier facet stream (Experimental/VehicleProvingGrounds/Archive/FacetArchiveCodec.py). Authored frame: +X forward,
// +Y left, +Z up, metres. Families are CourseMaterial identities.
export function readFacet(bytes) {
  const view = new DataView(bytes);
  const magic = String.fromCharCode(view.getUint8(0), view.getUint8(1), view.getUint8(2), view.getUint8(3));
  if (magic !== 'FCT1') throw new Error(`Not a facet stream (magic '${magic}')`);
  const vertexCount = view.getUint32(4, true), facetCount = view.getUint32(8, true), flags = view.getUint32(12, true);
  let at = 48;
  const positions = new Float32Array(bytes.slice(at, at + vertexCount * 12)); at += vertexCount * 12;
  const indices = new Uint32Array(bytes.slice(at, at + facetCount * 12)); at += facetCount * 12;
  const families = new Uint32Array(bytes.slice(at, at + facetCount * 4)); at += facetCount * 4;
  if (flags & 1) at += facetCount * 36;
  if (at !== bytes.byteLength) throw new Error(`Facet stream length ${bytes.byteLength} disagrees with its header (${at})`);
  return { positions, indices, families, format: 'FCT1 facet', notes: ['Authored Z-up, +X forward, metres', 'CourseMaterial families kept as material slots'] };
}

// --------------------------------------------------------------------------------------------------------- utils
function sequence(n) { const a = new Uint32Array(n); for (let i = 0; i < n; i++) a[i] = i; return a; }

function findSequence(u8, text) {
  const codes = [...text].map((c) => c.charCodeAt(0));
  outer: for (let i = 0; i + codes.length <= Math.min(u8.length, 65536); i++) {
    for (let k = 0; k < codes.length; k++) if (u8[i + k] !== codes[k]) continue outer;
    return i;
  }
  return -1;
}

function decodeDataUri(uri) {
  const comma = uri.indexOf(',');
  const binary = atob(uri.slice(comma + 1));
  const out = new Uint8Array(binary.length);
  for (let i = 0; i < binary.length; i++) out[i] = binary.charCodeAt(i);
  return out.buffer;
}

function identity4() { return [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1]; }

function multiply4(a, b) {
  const out = new Array(16);
  for (let c = 0; c < 4; c++) for (let r = 0; r < 4; r++) {
    out[c * 4 + r] = a[r] * b[c * 4] + a[4 + r] * b[c * 4 + 1] + a[8 + r] * b[c * 4 + 2] + a[12 + r] * b[c * 4 + 3];
  }
  return out;
}

function composeTrs(t = [0, 0, 0], q = [0, 0, 0, 1], s = [1, 1, 1]) {
  const [x, y, z, w] = q;
  const xx = x * x, yy = y * y, zz = z * z, xy = x * y, xz = x * z, yz = y * z, wx = w * x, wy = w * y, wz = w * z;
  return [
    (1 - 2 * (yy + zz)) * s[0], 2 * (xy + wz) * s[0], 2 * (xz - wy) * s[0], 0,
    2 * (xy - wz) * s[1], (1 - 2 * (xx + zz)) * s[1], 2 * (yz + wx) * s[1], 0,
    2 * (xz + wy) * s[2], 2 * (yz - wx) * s[2], (1 - 2 * (xx + yy)) * s[2], 0,
    t[0], t[1], t[2], 1,
  ];
}
