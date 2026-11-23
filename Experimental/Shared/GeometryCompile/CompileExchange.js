// Hand-off between the two workspaces. The Geometry Compiler is its own page (Experimental/GeometryCompiler/
// Compile.html); the Component Viewer hosts it in a full-window frame and the two talk with structured-clone messages,
// so typed arrays (positions, SDF volume, cluster levels) travel without conversion. This mirrors the C++ plan, where
// FrontierGeometryCompiler and FrontierComponentViewer are separate executables joined by CompiledGeometryExchange.
//
//   compiler → host   { channel, type: 'ready' }                                  page booted, can take a session
//   host → compiler   { channel, type: 'open', asset, recompile }                 start a session on this asset
//   compiler → host   { channel, type: 'finish', asset, cancelled, open, existing } session ended (asset is a copy)

export const CHANNEL = 'frontier-geometry-compile';
export const COMPILER_PAGE = new URL('../../GeometryCompiler/Compile.html?host=component-viewer', import.meta.url);

// Host side (Component Viewer). `frame` is the <iframe>; it loads on the first open.
export function hostCompiler(frame, { onFinish, onError = () => {} }) {
  let ready = false, pending = null;
  const post = (message) => {
    try { frame.contentWindow.postMessage({ channel: CHANNEL, ...message }, '*'); }
    catch (error) { onError(error); }
  };
  addEventListener('message', (e) => {
    const m = e.data;
    if (!m || m.channel !== CHANNEL || e.source !== frame.contentWindow) return;
    if (m.type === 'ready') {
      ready = true;
      if (pending) { post(pending); pending = null; }
    } else if (m.type === 'finish') {
      frame.hidden = true;
      onFinish(m.asset, { cancelled: !!m.cancelled, open: !!m.open, existing: !!m.existing });
    }
  });
  function open(asset, { recompile = false } = {}) {
    const message = { type: 'open', asset, recompile };
    frame.hidden = false;
    if (!frame.getAttribute('src')) frame.setAttribute('src', COMPILER_PAGE.href);
    if (ready) post(message); else pending = message;
    try { frame.contentWindow && frame.contentWindow.focus(); } catch { /* cross-origin or not loaded yet */ }
  }
  return { open, get ready() { return ready; } };
}

// Compiler side (Geometry Compiler). Embedded when a host page frames it; standalone otherwise.
export function compilerGuest({ onOpen }) {
  const host = window.parent !== window ? window.parent : null;
  if (host) {
    addEventListener('message', (e) => {
      const m = e.data;
      if (!m || m.channel !== CHANNEL || e.source !== host || m.type !== 'open') return;
      onOpen(m.asset, { recompile: !!m.recompile });
    });
  }
  return {
    embedded: !!host,
    announce() { if (host) host.postMessage({ channel: CHANNEL, type: 'ready' }, '*'); },
    finish(asset, info) { if (host) host.postMessage({ channel: CHANNEL, type: 'finish', asset, ...info }, '*'); },
  };
}
