/** A single IndexedDB document keeps masks, bounds, height bakes and terrain
 * recipes/sculpt/height snapshots without forcing megabytes into localStorage. */
function database() {
  return new Promise((resolve, reject) => {
    const request = indexedDB.open('frontier-world-layout', 1);
    request.onupgradeneeded = () => request.result.createObjectStore('worlds');
    request.onsuccess = () => resolve(request.result);
    request.onerror = () => reject(request.error);
  });
}
export async function loadWorld() {
  const db = await database();
  try {
    return await new Promise((resolve, reject) => {
      const request = db.transaction('worlds', 'readonly').objectStore('worlds').get('current');
      request.onsuccess = () => resolve(request.result || null);
      request.onerror = () => reject(request.error);
    });
  } finally { db.close(); }
}
export async function saveWorld(world) {
  const db = await database();
  try {
    return await new Promise((resolve, reject) => {
      const transaction = db.transaction('worlds', 'readwrite');
      transaction.objectStore('worlds').put(world, 'current');
      transaction.oncomplete = () => resolve();
      transaction.onerror = () => reject(transaction.error);
      transaction.onabort = () => reject(transaction.error || new Error('World save aborted'));
    });
  } finally { db.close(); }
}
