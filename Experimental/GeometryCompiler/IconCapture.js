// Icon camera: a square camera box over the viewport, a default three-quarter angle, and a transparent capture.
// The box is the exact crop of the saved PNG: its vertical field of view is the viewport's, scaled by the box size.

const LIBRARY_KEY = 'frontier.clusterLodStudio.icons';
const DEFAULT_YAW = 0.7, DEFAULT_PITCH = 0.38;
// The bounding sphere of the geometry fills this fraction of the box's half-height by default.
const FILL = 0.82;

const $ = (id) => document.getElementById(id);

function readLibrary() {
  try { return JSON.parse(localStorage.getItem(LIBRARY_KEY)) || {}; } catch { return {}; }
}

export function createIconCapture({ viewport, camera, settings, updateEye, toast, currentSource, onLibraryChange }) {
  const well = $('CanvasWell'), canvas = viewport.canvas, box = $('CaptureBox');
  const state = { active: false, look: 'shaded', current: null };
  let library = readLibrary();

  const side = () => Math.max(120, Math.round(Math.min(well.clientWidth, well.clientHeight) * 0.8));
  const crop = () => side() / Math.max(1, canvas.clientHeight);
  const captureFov = () => 2 * Math.atan(Math.tan(camera.fov / 2) * crop());

  function layoutBox() {
    box.style.setProperty('--side', side() + 'px');
    $('CaptureLabel').textContent = `ICON · ${$('IconSize').value} × ${$('IconSize').value} · transparent`;
  }

  // Puts the camera on the default angle, far enough that the geometry's bounding sphere fills FILL of the box.
  function resetAngle() {
    const geometry = viewport.geometry;
    if (!geometry) return;
    const instance = viewport.instances[viewport.centreInstance()];
    camera.target = viewport.toWorld(instance, geometry.centre);
    camera.yaw = DEFAULT_YAW;
    camera.pitch = DEFAULT_PITCH;
    const half = Math.atan(FILL * Math.tan(captureFov() / 2));
    camera.distance = Math.min(4000, geometry.fitRadius / Math.sin(half));
    updateEye();
  }

  function setActive(on) {
    state.active = on;
    box.hidden = !on;
    const tool = $('ToolIcon');
    tool.classList.toggle('active', on);
    tool.setAttribute('aria-pressed', on);
    $('IconCameraTile').classList.toggle('on', on);
    $('IconCameraTile').setAttribute('aria-pressed', on);
    $('IconCameraState').textContent = on ? 'ON' : 'OFF';
    if (on) { layoutBox(); resetAngle(); }
  }

  function capture() {
    const geometry = viewport.geometry;
    const source = currentSource();
    if (!geometry || !source) { toast('Wait for the geometry to finish building'); return; }
    const size = +$('IconSize').value;
    updateEye();
    const look = state.look === 'live' ? settings.viewMode : 'shaded';
    const pixels = viewport.captureIcon({ ...camera, fov: captureFov() }, { ...settings, viewMode: look }, size);
    let filled = 0;
    for (let i = 3; i < pixels.length; i += 4) if (pixels[i]) filled++;
    if (!filled) { toast('Nothing inside the box — move the camera closer'); return; }
    const out = document.createElement('canvas');
    out.width = size; out.height = size;
    out.getContext('2d').putImageData(new ImageData(pixels, size, size), 0, 0);
    state.current = {
      key: source.key, name: source.name, size, look: state.look,
      coverage: filled / (size * size),
      canvas: out, url: out.toDataURL('image/png'),
    };
    showPreview();
    toast(`Captured ${size} × ${size} icon`);
  }

  function showPreview() {
    const current = state.current;
    const image = $('IconImage'), empty = $('IconEmpty');
    if (!current) { image.hidden = true; empty.hidden = false; $('IconMeta').textContent = '—'; return; }
    image.src = current.url;
    image.hidden = false;
    empty.hidden = true;
    $('IconMeta').textContent = `${current.size} × ${current.size} PNG · ${current.look === 'live' ? 'live view' : 'shaded'} · ${Math.round(current.coverage * 100)} % covered`;
    const saved = library[current.key];
    $('IconSaveButton').textContent = saved ? 'Replace saved icon' : 'Save as icon';
  }

  function download() {
    const current = state.current;
    if (!current) return toast('Capture an icon first');
    const link = document.createElement('a');
    link.href = current.url;
    link.download = `${current.key}-icon-${current.size}.png`;
    link.click();
  }

  async function copy() {
    const current = state.current;
    if (!current) return toast('Capture an icon first');
    try {
      const blob = await new Promise((resolve) => current.canvas.toBlob(resolve, 'image/png'));
      await navigator.clipboard.write([new ClipboardItem({ 'image/png': blob })]);
      toast('Copied PNG to clipboard');
    } catch {
      toast('Clipboard blocked — use Download PNG');
    }
  }

  // The saved copy is 256 px so the library stays small in localStorage; Download keeps the full size.
  function saveAsIcon() {
    const current = state.current;
    if (!current) return toast('Capture an icon first');
    const thumb = document.createElement('canvas');
    thumb.width = thumb.height = 256;
    const g = thumb.getContext('2d');
    g.imageSmoothingQuality = 'high';
    g.drawImage(current.canvas, 0, 0, 256, 256);
    library = { ...library, [current.key]: { url: thumb.toDataURL('image/png'), name: current.name, savedAt: Date.now() } };
    try { localStorage.setItem(LIBRARY_KEY, JSON.stringify(library)); }
    catch { toast('Browser storage is full — remove an icon or download it instead'); return; }
    renderLibrary();
    onLibraryChange();
    showPreview();
    toast(`Saved icon for ${current.name}`);
  }

  function removeIcon(key) {
    const { [key]: _removed, ...rest } = library;
    library = rest;
    localStorage.setItem(LIBRARY_KEY, JSON.stringify(library));
    renderLibrary();
    onLibraryChange();
  }

  function renderLibrary() {
    const host = $('IconLibrary');
    const keys = Object.keys(library);
    host.innerHTML = '';
    if (!keys.length) { host.innerHTML = '<small>Saved icons appear here and in the Outliner.</small>'; return; }
    for (const key of keys) {
      const entry = library[key];
      const row = document.createElement('div');
      row.className = 'library-row';
      row.innerHTML = `<img alt="" src="${entry.url}"><div><b></b><small>${key}</small></div><button class="tool round" aria-label="Remove saved icon"><span>×</span></button>`;
      row.querySelector('b').textContent = entry.name;
      row.querySelector('img').addEventListener('click', () => { state.current = null; $('IconImage').src = entry.url; $('IconImage').hidden = false; $('IconEmpty').hidden = true; $('IconMeta').textContent = 'Saved icon · ' + entry.name; });
      row.querySelector('button').addEventListener('click', () => removeIcon(key));
      host.append(row);
    }
  }

  // --- Wiring ----------------------------------------------------------------------------------------------
  $('IconSize').addEventListener('change', () => { layoutBox(); if (state.current) showPreview(); });
  $('IconCapture').addEventListener('click', capture);
  $('CaptureShoot').addEventListener('click', capture);
  $('IconDownload').addEventListener('click', download);
  $('IconCopy').addEventListener('click', copy);
  $('IconSaveButton').addEventListener('click', saveAsIcon);
  $('IconCameraTile').addEventListener('click', () => setActive(!state.active));
  $('ToolIcon').addEventListener('click', () => setActive(!state.active));
  $('IconResetAngle').addEventListener('click', () => { resetAngle(); toast('Default camera angle'); });
  for (const button of document.querySelectorAll('[data-look]')) {
    button.addEventListener('click', () => {
      state.look = button.dataset.look;
      for (const other of document.querySelectorAll('[data-look]')) other.classList.toggle('active', other === button);
    });
  }
  window.addEventListener('resize', () => { if (state.active) layoutBox(); });
  renderLibrary();
  showPreview();

  return {
    get active() { return state.active; },
    setActive,
    capture,
    layoutBox,
    // URL of the saved 256 px icon for a source key, or null. Used by the Outliner rows.
    iconFor(key) { return library[key] ? library[key].url : null; },
    notifyLibrary() { onLibraryChange(); },
    get library() { return library; },
    showPreview,
  };
}
