/* Tiny synchronous event bus — the one channel every panel talks over.
   Events: select, propchange, treechange, focus, popup, toast, settod, physicschange */
const map = new Map();
export const bus = {
  on(evt, fn) { (map.get(evt) || map.set(evt, []).get(evt)).push(fn); return () => bus.off(evt, fn); },
  off(evt, fn) { const l = map.get(evt); if (l) l.splice(l.indexOf(fn) >>> 0, 1); },
  emit(evt, payload) { (map.get(evt) || []).forEach(fn => fn(payload)); },
};
