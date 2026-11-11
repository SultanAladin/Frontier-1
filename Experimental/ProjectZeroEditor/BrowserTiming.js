import { useSyncExternalStore, createElement } from "react";
const Listeners = new Set();
let Snapshot = { Samples: [], Live: null },
  Frame = 0,
  Previous = 0,
  Start = 0,
  Intervals = [];
function VisibilityChanged() {
  Previous = 0;
  Start = 0;
  Intervals = [];
  Snapshot = { Samples: Snapshot.Samples, Live: null };
  for (const Listener of Listeners) Listener();
}
function Tick(Time) {
  if (document.hidden) {
    Previous = 0;
    Start = 0;
    Intervals = [];
  } else {
    if (Previous && Time > Previous) Intervals.push(Time - Previous);
    Previous = Time;
    if (!Start) Start = Time;
    if (Time - Start >= 500 && Intervals.length) {
      const Sorted = [...Intervals].sort((A, B) => A - B),
        Mean = Intervals.reduce((A, B) => A + B, 0) / Intervals.length,
        Sample = {
          Time,
          FPS: 1000 / Mean,
          Frame: Mean,
          P95: Sorted[Math.ceil(Sorted.length * 0.95) - 1],
          Worst: Sorted.at(-1),
        };
      Snapshot = {
        Live: Sample,
        Samples: [
          ...Snapshot.Samples.filter((S) => Time - S.Time <= 60000),
          Sample,
        ],
      };
      Intervals = [];
      Start = Time;
      for (const Listener of Listeners) Listener();
    }
  }
  Frame = requestAnimationFrame(Tick);
}
function Subscribe(Listener) {
  Listeners.add(Listener);
  if (Listeners.size === 1) {
    Snapshot = { Samples: [], Live: null };
    Previous = 0;
    Start = 0;
    Intervals = [];
    document.addEventListener("visibilitychange", VisibilityChanged);
    Frame = requestAnimationFrame(Tick);
  }
  return () => {
    Listeners.delete(Listener);
    if (!Listeners.size) {
      cancelAnimationFrame(Frame);
      Frame = 0;
      document.removeEventListener("visibilitychange", VisibilityChanged);
    }
  };
}
export function useBrowserTiming() {
  return useSyncExternalStore(Subscribe, () => Snapshot);
}
export function BrowserFPS() {
  const { Live } = useBrowserTiming();
  return createElement(
    "span",
    null,
    `Browser ${Live ? Live.FPS.toFixed(1) : "—"} fps · rAF`,
  );
}
