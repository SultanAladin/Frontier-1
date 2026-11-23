// jsdom harness shared by the Component Viewer and Geometry Compiler smokes: mocked WebGL / 2D canvas / layout, Workers
// that run the SDF and cluster-LOD bakes in-process, fetch from the repo, image onload. Rendering itself is not checked
// (no GPU); the smokes catch wiring and runtime errors.
//   npm install --prefix /tmp/smoke jsdom@25
import { readFileSync } from 'node:fs';
import { createRequire } from 'node:module';
import { pathToFileURL, fileURLToPath } from 'node:url';
import { dirname, resolve } from 'node:path';

const { JSDOM } = createRequire(pathToFileURL(resolve(process.env.JSDOM_DIR || process.cwd()) + '/'))('jsdom');
export const REPO = resolve(dirname(fileURLToPath(import.meta.url)), '../../../..');
const ENGINE = REPO + '/Experimental/Shared/GeometryCompile/Engine/';

// htmlPath: page to load (scripts stripped; the smoke imports the entry module itself). url: the page's address.
export function bootStudio({ htmlPath, url }) {
  const html = readFileSync(htmlPath, 'utf8').replace(/<script[\s\S]*?<\/script>/g, '');
  const dom = new JSDOM(html, { url, pretendToBeVisual: true });
  const w = dom.window;
  const errors = [];
  process.on('unhandledRejection', (e) => { errors.push('unhandledRejection: ' + (e && e.stack || e)); });
  w.addEventListener('error', (e) => errors.push('window error: ' + (e.error && e.error.stack || e.message)));
  const origError = console.error;
  console.error = (...a) => { if (/Not implemented: window\.focus/.test(String(a[0] && a[0].message || a[0]))) return; /* jsdom has no frame focus */ errors.push('console.error: ' + a.map((x) => (x && x.stack) || String(x)).join(' ')); };

  // ---------------------------------------------------------------- WebGL mock
  const GL = { VERSION: 7938, SHADING_LANGUAGE_VERSION: 35724, FRAMEBUFFER_COMPLETE: 36053, ACTIVE_UNIFORMS: 35718, ACTIVE_ATTRIBUTES: 35721, MAX_TEXTURE_SIZE: 3379, MAX_SAMPLES: 36183 };
  let constCounter = 50000;
  const constMap = new Map();
  function glMock(canvas) {
    const target = {
      canvas, drawingBufferWidth: 300, drawingBufferHeight: 150,
      getParameter(p) {
        if (p === GL.VERSION) return 'WebGL 2.0';
        if (p === GL.SHADING_LANGUAGE_VERSION) return 'WebGL GLSL ES 3.00';
        if (p === 2978 || p === 3088) return new Int32Array([0, 0, 300, 150]);
        return 16;
      },
      getShaderPrecisionFormat() { return { precision: 23, rangeMin: 127, rangeMax: 127 }; },
      getExtension(name) { return name === 'EXT_color_buffer_float' || name === 'WEBGL_lose_context' ? { loseContext() {} } : null; },
      getSupportedExtensions() { return []; },
      getContextAttributes() { return { alpha: true, antialias: true, depth: true, stencil: false, premultipliedAlpha: true, preserveDrawingBuffer: true, powerPreference: 'default' }; },
      getShaderParameter() { return true; }, getProgramParameter(p, n) { return (n === GL.ACTIVE_UNIFORMS || n === GL.ACTIVE_ATTRIBUTES) ? 0 : true; },
      getShaderInfoLog() { return ''; }, getProgramInfoLog() { return ''; }, getShaderSource() { return ''; },
      checkFramebufferStatus() { return GL.FRAMEBUFFER_COMPLETE; },
      isContextLost() { return false; },
      getUniformLocation() { return {}; }, getAttribLocation() { return 0; }, getError() { return 0; },
      readPixels(x, y, wd, h, f, t, buf) { if (buf) buf.fill(0); },
      fenceSync() { return {}; }, clientWaitSync() { return 37149; },
    };
    return new Proxy(target, {
      get(t, k) {
        if (k in t) return t[k];
        if (typeof k === 'string' && /^[A-Z0-9_]+$/.test(k)) { if (GL[k]) return GL[k]; if (!constMap.has(k)) constMap.set(k, constCounter++); return constMap.get(k); }
        if (typeof k === 'string' && k.startsWith('create')) return () => ({});
        return () => undefined;
      },
    });
  }
  function ctx2dMock(canvas) {
    const target = {
      canvas, measureText: (s) => ({ width: String(s).length * 6 }),
      createImageData: (a, b) => ({ width: a, height: b, data: new Uint8ClampedArray(a * b * 4) }),
      getImageData: (x, y, a, b) => ({ width: a, height: b, data: new Uint8ClampedArray(a * b * 4) }),
      createLinearGradient: () => ({ addColorStop() {} }), createRadialGradient: () => ({ addColorStop() {} }), createPattern: () => ({}),
      getTransform: () => ({ a: 1, d: 1 }),
    };
    return new Proxy(target, { get(t, k) { if (k in t) return t[k]; return () => undefined; }, set(t, k, v) { t[k] = v; return true; } });
  }
  w.HTMLCanvasElement.prototype.getContext = function (type) {
    if (type === '2d') return (this._c2 ||= ctx2dMock(this));
    return (this._gl ||= glMock(this));
  };
  w.HTMLCanvasElement.prototype.toDataURL = () => 'data:image/png;base64,AAAA';
  w.HTMLCanvasElement.prototype.toBlob = (cb) => cb(new Blob(['x']));
  w.HTMLCanvasElement.prototype.setPointerCapture = function () {};
  w.HTMLCanvasElement.prototype.releasePointerCapture = function () {};
  w.Element.prototype.setPointerCapture ||= function () {};
  w.Element.prototype.releasePointerCapture ||= function () {};
  w.Element.prototype.hasPointerCapture ||= function () { return false; };
  w.Element.prototype.scrollIntoView ||= function () {};
  // Layout: give every element a plausible size.
  Object.defineProperty(w.HTMLElement.prototype, 'offsetParent', { get() { return this.closest('[hidden]') ? null : w.document.body; } });
  Object.defineProperty(w.HTMLElement.prototype, 'clientWidth', { get() { return 800; } });
  Object.defineProperty(w.HTMLElement.prototype, 'clientHeight', { get() { return 600; } });
  w.Element.prototype.getBoundingClientRect = function () { return { x: 0, y: 0, left: 0, top: 0, width: 800, height: 600, right: 800, bottom: 600 }; };
  w.SVGElement.prototype.getBBox = function () { return { x: 0, y: 0, width: 100, height: 20 }; };
  w.URL.createObjectURL = () => 'blob:x'; w.URL.revokeObjectURL = () => {};
  w.HTMLAnchorElement.prototype.click = function () { downloads.push(this.download); };
  const downloads = [];

  // ---------------------------------------------------------------- Worker mock (in-process)
  class FakeWorker {
    constructor(url) { this.url = String(url); }
    postMessage(data) {
      setTimeout(async () => {
        try {
          let result;
          if (this.url.includes('/GeometryCompiler/Engine/SdfWorker')) {
            const { compileSparseSdfVolume } = await import(pathToFileURL(REPO + '/Experimental/GeometryCompiler/Engine/SparseSdfBaker.js'));
            const r = data.request;
            const volume = compileSparseSdfVolume({ positions: r.positions, indices: r.indices, closed: r.closed }, { gridDim: r.gridDim, paddingFraction: r.paddingFraction, narrowBandMult: r.narrowBandMult });
            this.onmessage({ data: { ticket: data.ticket, type: 'done', volume } });
            return;
          }
          if (this.url.includes('SdfWorker')) { const { bakeSdf } = await import(pathToFileURL(ENGINE + 'SdfBake.js')); result = bakeSdf(data.request.positions, data.request.indices, { resolution: data.request.resolution, band: data.request.band }); }
          else { const { buildClusterHierarchy } = await import(pathToFileURL(ENGINE + 'ClusterHierarchy.js')); result = buildClusterHierarchy(data.request.positions, data.request.indices, { ...data.request.build, families: data.request.families || null }); }
          this.onmessage({ data: { ticket: data.ticket, type: 'done', result } });
        } catch (e) { this.onmessage({ data: { ticket: data.ticket, type: 'error', message: e.message } }); }
      }, 0);
    }
    terminate() {}
  }

  // ---------------------------------------------------------------- globals
  const keys = ['window', 'document', 'navigator', 'HTMLElement', 'HTMLCanvasElement', 'HTMLInputElement', 'HTMLImageElement', 'Element', 'Node', 'Event', 'KeyboardEvent', 'MouseEvent', 'WheelEvent', 'DragEvent', 'CustomEvent',
    'DOMParser', 'getComputedStyle', 'requestAnimationFrame', 'cancelAnimationFrame', 'SVGElement', 'Image', 'FileReader', 'DocumentFragment', 'localStorage', 'sessionStorage', 'matchMedia', 'innerWidth', 'innerHeight', 'File', 'DataTransfer', 'addEventListener', 'removeEventListener', 'devicePixelRatio', 'self', 'location', 'history'];
  for (const k of keys) { try { if (w[k] !== undefined) Object.defineProperty(globalThis, k, { value: k === 'window' || k === 'self' ? w : (typeof w[k] === 'function' && !/^[A-Z]/.test(k) ? w[k].bind(w) : w[k]), configurable: true, writable: true }); } catch (e) { /* ignore */ } }
  globalThis.Path2D = w.Path2D = class { moveTo() {} lineTo() {} closePath() {} };
  globalThis.PointerEvent = w.PointerEvent || class extends w.MouseEvent { constructor(t, o = {}) { super(t, o); this.pointerId = o.pointerId || 1; this.pointerType = 'mouse'; } };
  w.PointerEvent = globalThis.PointerEvent;
  globalThis.ResizeObserver = w.ResizeObserver = class { constructor(cb) { this.cb = cb; } observe() { setTimeout(() => this.cb([]), 0); } disconnect() {} unobserve() {} };
  globalThis.Worker = w.Worker = FakeWorker;
  w.matchMedia ||= () => ({ matches: false, addEventListener() {}, removeEventListener() {} });
  globalThis.matchMedia = w.matchMedia;
  globalThis.fetch = w.fetch = async (u) => {
    const url = new URL(u, w.location.href);
    const path = (url.protocol === 'file:' ? '' : REPO) + decodeURIComponent(url.pathname);   // module-relative URLs resolve to file: in node
    const bytes = readFileSync(path);
    return { ok: true, status: 200, arrayBuffer: async () => bytes.buffer.slice(bytes.byteOffset, bytes.byteOffset + bytes.byteLength), text: async () => bytes.toString('utf8'), json: async () => JSON.parse(bytes) };
  };
  globalThis.ImageData = w.ImageData = class { constructor(d, a, b) { if (typeof d === 'number') { this.width = d; this.height = a; this.data = new Uint8ClampedArray(d * a * 4); } else { this.data = d; this.width = a; this.height = b ?? d.length / 4 / a; } } };
  globalThis.createImageBitmap = async () => ({ width: 1, height: 1, close() {} });
  w.HTMLImageElement.prototype.decode = async function () {};
  // Fire image onload for data URLs.
  const imgSrc = Object.getOwnPropertyDescriptor(w.HTMLImageElement.prototype, 'src');
  Object.defineProperty(w.HTMLImageElement.prototype, 'src', { get() { return imgSrc.get.call(this); }, set(v) { imgSrc.set.call(this, v); setTimeout(() => { this.onload && this.onload(); this.dispatchEvent(new w.Event('load')); }, 0); } });

  const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
  const $ = (id) => w.document.getElementById(id);
  const click = (el) => { if (!el) throw new Error('click target missing'); el.dispatchEvent(new w.MouseEvent('click', { bubbles: true })); };
  const key = (k, opts = {}) => w.document.body.dispatchEvent(new w.KeyboardEvent('keydown', { key: k, bubbles: true, ...opts }));
  const step = async (name, fn) => { const before = errors.length; try { await fn(); } catch (e) { errors.push(`[${name}] threw: ${e.stack}`); } console.log(`${errors.length > before ? 'FAIL' : ' ok '} ${name}`); };

  const finish = async () => {
    await sleep(200);
    console.error = origError;
    console.log(`\n${errors.length} errors · downloads ${downloads.length}`);
    const seen = new Set();
    for (const e of errors) { const k = e.replace(/console.error: Error: Uncaught \[/, 'window error: ').split('\n').slice(0, 3).join(' | '); if (k.startsWith('console.error: Error: Uncaught')) continue; if (seen.has(k)) continue; seen.add(k); console.log('-', e.split('\n').slice(0, 4).join('\n    ').slice(0, 700)); }
    process.exit(errors.length ? 1 : 0);
  };
  return { w, $, click, key, sleep, step, errors, downloads, finish };
}
