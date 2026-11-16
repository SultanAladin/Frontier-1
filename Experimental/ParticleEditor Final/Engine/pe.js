// The namespace every module of the Particle Editor hangs its exports on.
//
// 📝 WHY A SHARED OBJECT AND NOT NAMED EXPORTS. The page was written as three IIFEs over a `window.PE`
//    global, and roughly four thousand lines reach through `PE.` to find each other. Converting those
//    references to named imports is a four-thousand-line edit with no test behind it; converting the
//    *loading* of the files is an eight-line edit that removes the actual defect — hand-maintained
//    `?v=` cache-busting query strings, which serve a stale script the first time somebody forgets to
//    bump one. So the namespace stays and the script tags go. Narrowing `PE` into real named exports is
//    a later, file-by-file job, best done alongside the identifier renaming at the C++ port.
export const PE = {};

// sRGB hex to linear RGBA, the colour space every system parameter uses.
PE.hexLinear = function hexLinear(hex) {
  const v = [1, 3, 5].map((i) => parseInt(hex.slice(i, i + 2), 16) / 255);
  const lin = v.map((c) => (c <= 0.04045 ? c / 12.92 : Math.pow((c + 0.055) / 1.055, 2.4)));
  return [lin[0], lin[1], lin[2], 1];
};

// Kept reachable from the console, which is how this page has always been debugged. Nothing in the
//    source reads it from `window` any more — the modules import it.
if (typeof window !== "undefined") window.PE = PE;
