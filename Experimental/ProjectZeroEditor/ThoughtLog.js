//============================================================================================================================================
//                                                               THOUGHTLOG.JS
//============================================================================================================================================
// 🧩 Browser counterpart of the Thought Log (Project Zero specification §15, Follow-up 48). Scope timings go into a fixed ring in RAM;
// an aggregator folds the ring into per-scope totals four times a second. Nothing is written while the editor runs. On close
// (pagehide) the report is written once, under the key named after Logs/FrontierThoughtLog.txt, and the Profiler tab can save the
// same text as a file. 📝 The native C++ ring (lock-free, aggregator thread, FRONTIER_THOUGHT_SCOPE) is not in this repository.

// Specification scopes, in report order. Scopes recorded under any other name are appended as they appear.
export const ThoughtScopes = [
  "Host.Frame",
  "Host.EditorTick",
  "Editor.Tick",
  "Editor.ViewportRaster",
  "Editor.ViewportInteraction",
  "Editor.GizmoOverlayRefresh",
  "Editor.ImGuiPresent",
  "Render.WorldViewportRgba8",
  "Render.WorldViewportWithEngine",
];
// Scopes the HTML editor has no equivalent for (ImGui, native world renderer).
export const NativeOnlyScopes = new Set(["Editor.ImGuiPresent", "Render.WorldViewportRgba8", "Render.WorldViewportWithEngine"]);
export const ThoughtLogPath = "Logs/FrontierThoughtLog.txt";
export const ThoughtLogStorageKey = "Frontier.ThoughtLog:" + ThoughtLogPath;

import { useLayoutEffect } from "react";

const RingCapacity = 1 << 16;
const PercentileWindow = 8192;
const RingScope = new Uint16Array(RingCapacity);
const RingDuration = new Float32Array(RingCapacity);
let WriteCursor = 0; // total samples ever written; the ring slot is WriteCursor % RingCapacity
let ReadCursor = 0;
let Dropped = 0;
const ScopeNames = [];
const ScopeIndex = new Map();
const Totals = [];
const Started = typeof performance !== "undefined" ? performance.now() : 0;

function IndexOf(Name) {
  let Index = ScopeIndex.get(Name);
  if (Index === undefined) {
    Index = ScopeNames.length;
    ScopeNames.push(Name);
    ScopeIndex.set(Name, Index);
    Totals.push({ Calls: 0, Total: 0, Minimum: Infinity, Maximum: 0, Window: new Float32Array(PercentileWindow), Filled: 0 });
  }
  return Index;
}
ThoughtScopes.forEach(IndexOf);

// One sample: a scope name and its duration in milliseconds. Constant time, no allocation after the scope is first seen.
export function RecordThought(Name, Milliseconds) {
  if (!Number.isFinite(Milliseconds) || Milliseconds < 0) return;
  const Slot = WriteCursor % RingCapacity;
  RingScope[Slot] = IndexOf(Name);
  RingDuration[Slot] = Milliseconds;
  WriteCursor += 1;
}

// Starts a scope; call the returned function to close it.
export function OpenThought(Name) {
  const Start = performance.now();
  return () => RecordThought(Name, performance.now() - Start);
}

// Component scope: from the component's render to its layout effect (render plus commit of its subtree). React.Profiler is not used
// because the production build disables it.
export function useThoughtScope(Name) {
  const Start = performance.now();
  useLayoutEffect(() => RecordThought(Name, performance.now() - Start));
}

// Folds the unread part of the ring into the per-scope totals. Samples overwritten before folding are counted as dropped.
export function AggregateThoughts() {
  if (WriteCursor - ReadCursor > RingCapacity) {
    Dropped += WriteCursor - ReadCursor - RingCapacity;
    ReadCursor = WriteCursor - RingCapacity;
  }
  for (; ReadCursor < WriteCursor; ReadCursor += 1) {
    const Slot = ReadCursor % RingCapacity;
    const Entry = Totals[RingScope[Slot]];
    const Duration = RingDuration[Slot];
    Entry.Calls += 1;
    Entry.Total += Duration;
    if (Duration < Entry.Minimum) Entry.Minimum = Duration;
    if (Duration > Entry.Maximum) Entry.Maximum = Duration;
    Entry.Window[Entry.Filled % PercentileWindow] = Duration;
    Entry.Filled += 1;
  }
}

function Percentile(Sorted, Fraction) {
  if (!Sorted.length) return 0;
  return Sorted[Math.min(Sorted.length - 1, Math.max(0, Math.ceil(Fraction * Sorted.length) - 1))];
}

// Per-scope rows: calls, total, average, minimum, maximum, P50 and P95 (percentiles over the last 8192 samples of each scope).
export function ThoughtReport() {
  AggregateThoughts();
  return ScopeNames.map((Name, Index) => {
    const Entry = Totals[Index];
    const Sorted = Array.from(Entry.Window.subarray(0, Math.min(Entry.Filled, PercentileWindow))).sort((A, B) => A - B);
    return {
      Name,
      Calls: Entry.Calls,
      Total: Entry.Total,
      Average: Entry.Calls ? Entry.Total / Entry.Calls : 0,
      Minimum: Entry.Calls ? Entry.Minimum : 0,
      Maximum: Entry.Maximum,
      P50: Percentile(Sorted, 0.5),
      P95: Percentile(Sorted, 0.95),
      NativeOnly: NativeOnlyScopes.has(Name),
    };
  });
}

export const ThoughtStatistics = () => ({ Written: WriteCursor, Dropped, Capacity: RingCapacity, Seconds: (performance.now() - Started) / 1000 });

export function ThoughtLogText(When = new Date()) {
  const Rows = ThoughtReport();
  const Stats = ThoughtStatistics();
  const Columns = ["Scope", "Calls", "Total ms", "Avg ms", "Min ms", "Max ms", "P50 ms", "P95 ms"];
  const Lines = Rows.map((Row) => [
    Row.Name + (Row.NativeOnly && !Row.Calls ? " (native only)" : ""),
    String(Row.Calls),
    ...[Row.Total, Row.Average, Row.Minimum, Row.Maximum, Row.P50, Row.P95].map((Number) => Number.toFixed(3)),
  ]);
  const Widths = Columns.map((Column, Index) => Math.max(Column.length, ...Lines.map((Line) => Line[Index].length)));
  const Format = (Cells) => Cells.map((Cell, Index) => (Index ? Cell.padStart(Widths[Index]) : Cell.padEnd(Widths[Index]))).join("  ");
  return [
    "Frontier Thought Log · Project Zero HTML editor",
    `Written ${When.toISOString()} · session ${Stats.Seconds.toFixed(1)} s · ${Stats.Written} samples · ${Stats.Dropped} dropped · ring ${Stats.Capacity}`,
    "Recorded in RAM only; this report is the single write, made when the editor closes.",
    "",
    Format(Columns),
    Format(Widths.map((Width) => "-".repeat(Width))),
    ...Lines.map(Format),
    "",
  ].join("\n");
}

// Host.Frame: the interval between animation frames. Other scopes are recorded where the editor does that work (React profilers,
// viewport handlers); a scope with no HTML equivalent stays at zero calls and is reported as such.
let Running = false;
export function StartThoughtLog() {
  if (Running || typeof window === "undefined") return;
  Running = true;
  let Previous = performance.now();
  const Tick = (Now) => {
    RecordThought("Host.Frame", Now - Previous);
    Previous = Now;
    requestAnimationFrame(Tick);
  };
  requestAnimationFrame(Tick);
  setInterval(AggregateThoughts, 250);
  // ⚠️ The only write: one localStorage entry at close. No storage or network I/O happens while the editor runs.
  window.addEventListener("pagehide", () => {
    try {
      localStorage.setItem(ThoughtLogStorageKey, ThoughtLogText());
    } catch {}
  });
}

export function SaveThoughtLog() {
  const Blob_ = new Blob([ThoughtLogText()], { type: "text/plain" });
  const Link = document.createElement("a");
  Link.href = URL.createObjectURL(Blob_);
  Link.download = "FrontierThoughtLog.txt";
  Link.click();
  setTimeout(() => URL.revokeObjectURL(Link.href), 1000);
}
