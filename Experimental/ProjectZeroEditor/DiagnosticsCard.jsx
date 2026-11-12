import React, { useState, useRef, useEffect } from "react";
import { useBrowserTiming } from "./BrowserTiming.js";
import "./WorkspaceCards.css";
const Storage = "Frontier.DiagnosticsCard.v1",
  Defaults = {
    Metric: "FPS",
    View: "both",
    Seconds: 30,
    X: 16,
    Y: 16,
    Width: 370,
    Height: 520,
  };
function Restore() {
  try {
    const S = JSON.parse(localStorage.getItem(Storage)) || {};
    return {
      ...Defaults,
      ...Object.fromEntries(
        ["X", "Y", "Width", "Height"]
          .filter((K) => Number.isFinite(S[K]))
          .map((K) => [K, S[K]]),
      ),
      Metric: ["FPS", "Frame", "GPU", "Scene"].includes(S.Metric)
        ? S.Metric
        : "FPS",
      View: ["both", "graph", "stats"].includes(S.View) ? S.View : "both",
      Seconds: [15, 30, 60].includes(S.Seconds) ? S.Seconds : 30,
    };
  } catch {
    return Defaults;
  }
}
const Clamp = (V, A, B) => Math.max(A, Math.min(B, V));
export default function DiagnosticsCard({
  Rows,
  VisibleCount,
  DebugName,
  HiZ,
  Alias,
  PatchError,
  Close,
}) {
  const Timing = useBrowserTiming(),
    [Frozen, Freeze] = useState(null),
    [Prefs, SetPrefs] = useState(Restore),
    [Room, Measure] = useState({ Width: 500, Height: 500 }),
    [Probe, Inspect] = useState(null),
    Root = useRef(null),
    Drag = useRef(null);
  const Set = (Key, Value) => {
    SetPrefs((P) => ({ ...P, [Key]: Value }));
    Inspect(null);
  };
  function Fit(P) {
    const Width = Clamp(
        P.Width,
        Math.min(280, Math.max(1, Room.Width - 16)),
        Math.max(1, Room.Width - 16),
      ),
      Height = Clamp(
        P.Height,
        Math.min(250, Math.max(1, Room.Height - 16)),
        Math.max(1, Room.Height - 16),
      );
    return {
      ...P,
      Width,
      Height,
      X: Clamp(P.X, 8, Math.max(8, Room.Width - Width - 8)),
      Y: Clamp(P.Y, 8, Math.max(8, Room.Height - Height - 8)),
    };
  }
  const Box = Fit(Prefs),
    Data = Frozen || Timing,
    Last = Data.Live,
    Samples = Data.Samples.filter(
      (S) => Last && Last.Time - S.Time <= Prefs.Seconds * 1000,
    ),
    Numeric = Prefs.Metric === "FPS" || Prefs.Metric === "Frame",
    Index =
      Probe === null ? Samples.length - 1 : Clamp(Probe, 0, Samples.length - 1),
    Sample = Samples[Index],
    Value = Numeric
      ? Sample?.[Prefs.Metric]
      : Prefs.Metric === "Scene"
        ? Rows.length
        : null,
    Unit =
      Prefs.Metric === "FPS"
        ? "fps"
        : Prefs.Metric === "Frame" || Prefs.Metric === "GPU"
          ? "ms"
          : "entries",
    Title = {
      FPS: "Browser frame rate",
      Frame: "Browser frame interval",
      GPU: "GPU timing",
      Scene: "Scene inventory",
    }[Prefs.Metric];
  useEffect(() => {
    const Parent = Root.current.parentElement,
      Observer = new ResizeObserver(() =>
        Measure({ Width: Parent.clientWidth, Height: Parent.clientHeight }),
      );
    Observer.observe(Parent);
    return () => Observer.disconnect();
  }, []);
  useEffect(() => {
    try {
      localStorage.setItem(Storage, JSON.stringify(Prefs));
    } catch {}
  }, [Prefs]);
  function Start(E, Kind) {
    if (E.button !== 0) return;
    E.preventDefault();
    E.currentTarget.focus();
    E.currentTarget.setPointerCapture(E.pointerId);
    Drag.current = { Kind, X: E.clientX, Y: E.clientY, Box };
  }
  function Move(E) {
    if (!Drag.current) return;
    const D = Drag.current,
      DX = E.clientX - D.X,
      DY = E.clientY - D.Y;
    SetPrefs(
      Fit({
        ...D.Box,
        ...(D.Kind === "move"
          ? { X: D.Box.X + DX, Y: D.Box.Y + DY }
          : { Width: D.Box.Width + DX, Height: D.Box.Height + DY }),
      }),
    );
  }
  function Keyboard(E, Kind) {
    if (!["ArrowLeft", "ArrowRight", "ArrowUp", "ArrowDown"].includes(E.key))
      return;
    E.preventDefault();
    E.stopPropagation();
    const Delta = E.shiftKey ? 30 : 10;
    SetPrefs(
      Fit({
        ...Box,
        ...(Kind === "move"
          ? {
              X:
                Box.X +
                (E.key === "ArrowRight"
                  ? Delta
                  : E.key === "ArrowLeft"
                    ? -Delta
                    : 0),
              Y:
                Box.Y +
                (E.key === "ArrowDown"
                  ? Delta
                  : E.key === "ArrowUp"
                    ? -Delta
                    : 0),
            }
          : {
              Width:
                Box.Width +
                (E.key === "ArrowRight"
                  ? Delta
                  : E.key === "ArrowLeft"
                    ? -Delta
                    : 0),
              Height:
                Box.Height +
                (E.key === "ArrowDown"
                  ? Delta
                  : E.key === "ArrowUp"
                    ? -Delta
                    : 0),
            }),
      }),
    );
  }
  const Values = Samples.map((S) => S[Prefs.Metric]),
    Maximum =
      Math.max(
        Prefs.Metric === "FPS" ? 75 : 25,
        ...Values.filter(Number.isFinite),
      ) * 1.05,
    Target = Prefs.Metric === "FPS" ? 60 : 1000 / 60,
    PX = (S) =>
      40 +
      ((S.Time - (Last.Time - Prefs.Seconds * 1000)) / (Prefs.Seconds * 1000)) *
        420,
    PY = (N) => 160 - (140 * N) / Maximum,
    Path = Samples.map(
      (S, I) => `${I ? "L" : "M"}${PX(S)},${PY(S[Prefs.Metric])}`,
    ).join(" ");
  const Stats = Numeric
    ? [
        [
          "Mean",
          Values.length
            ? (Values.reduce((A, B) => A + B, 0) / Values.length).toFixed(1) +
              " " +
              Unit
            : "—",
        ],
        [
          "Low / High",
          Values.length
            ? Math.min(...Values).toFixed(1) +
              " / " +
              Math.max(...Values).toFixed(1)
            : "—",
        ],
        ["Latest p95 interval", Last ? Last.P95.toFixed(1) + " ms" : "—"],
      ]
    : Prefs.Metric === "Scene"
      ? [
          ["Visible records", VisibleCount],
          ["Folders", Rows.filter((R) => R.Panel === "group").length],
          ["Constructed markers", Rows.filter((R) => R.Preview).length],
        ]
      : [
          ["Cull / Raster", "— / —"],
          ["Resolve / Lighting", "— / —"],
          ["Native renderer", "Not connected"],
        ];
  return (
    <section
      ref={Root}
      className="diagnostic-overlay debug-card workspace-card"
      aria-label="Viewport performance diagnostics"
      style={{ left: Box.X, top: Box.Y, width: Box.Width, height: Box.Height }}
      data-metric={Prefs.Metric}
      data-paused={!!Frozen}
      onPointerMove={Move}
      onPointerUp={() => (Drag.current = null)}
      onPointerCancel={() => (Drag.current = null)}
      onLostPointerCapture={() => (Drag.current = null)}
      onKeyDown={(E) => {
        if (E.key === "Escape") {
          E.preventDefault();
          E.stopPropagation();
          Close();
        }
      }}
    >
      <header className="debug-card-header">
        <button
          className="debug-drag"
          aria-label="Move diagnostics card"
          title="Drag to move · arrow keys move 10 px"
          onPointerDown={(E) => Start(E, "move")}
          onKeyDown={(E) => Keyboard(E, "move")}
        >
          <span className="status-dot" />
          Performance <small>{Frozen ? "PAUSED" : "LIVE"}</small>
        </button>
        <button
          aria-label="Reset diagnostics size and position"
          title="Reset layout"
          onClick={() =>
            SetPrefs((P) => ({ ...P, X: 16, Y: 16, Width: 370, Height: 520 }))
          }
        >
          ↗
        </button>
        <button aria-label="Close diagnostics" onClick={Close}>
          ×
        </button>
      </header>
      <div className="debug-card-body">
        <div className="debug-card-selectors">
          <select
            aria-label="Diagnostics metric"
            value={Prefs.Metric}
            onChange={(E) => Set("Metric", E.target.value)}
          >
            <option value="FPS">Frame rate / FPS</option>
            <option value="Frame">Frame interval / ms</option>
            <option value="GPU">GPU timing</option>
            <option value="Scene">Scene counts</option>
          </select>
          <select
            aria-label="Diagnostics presentation"
            value={Prefs.View}
            onChange={(E) => Set("View", E.target.value)}
          >
            <option value="both">Graph + stats</option>
            <option value="graph">Graph</option>
            <option value="stats">Stats</option>
          </select>
        </div>
        <h2>
          <i className={Prefs.Metric === "Frame" ? "warm" : ""} />
          {Title}
        </h2>
        <div className="workspace-number debug-number">
          <span>
            {Value == null
              ? "—"
              : Math.floor(Numeric ? +Value.toFixed(1) : Value).toLocaleString(
                  "en",
                )}
          </span>
          {Value != null && Numeric && (
            <span className="workspace-fraction">
              .{Value.toFixed(1).split(".")[1]}
            </span>
          )}
          <small>{Unit}</small>
        </div>
        <p className="debug-source">
          {Numeric
            ? "Measured requestAnimationFrame rate · not engine FPS or GPU execution time"
            : Prefs.Metric === "GPU"
              ? "Unavailable · HTML preview has no GPU telemetry or debug rendering."
              : "Current scene records · not draw calls, triangles or memory usage."}
        </p>
        {Prefs.View !== "stats" &&
          (Numeric ? (
            <div className="debug-history">
              <svg
                viewBox="0 0 500 200"
                preserveAspectRatio="none"
                role="slider"
                tabIndex={0}
                aria-label="Inspect diagnostics history"
                aria-valuemin={0}
                aria-valuemax={Math.max(0, Samples.length - 1)}
                aria-valuenow={Math.max(0, Index)}
                aria-valuetext={
                  Sample
                    ? `${((Last.Time - Sample.Time) / 1000).toFixed(1)} seconds ago, ${Sample[Prefs.Metric].toFixed(1)} ${Unit}`
                    : "Collecting browser samples"
                }
                onPointerMove={(E) => {
                  if (!Samples.length) return;
                  const B = E.currentTarget.getBoundingClientRect(),
                    Time =
                      Last.Time -
                      Prefs.Seconds * 1000 +
                      Clamp(
                        (((E.clientX - B.left) / B.width) * 500 - 40) / 420,
                        0,
                        1,
                      ) *
                        Prefs.Seconds *
                        1000;
                  let Best = 0;
                  Samples.forEach((S, I) => {
                    if (
                      Math.abs(S.Time - Time) <
                      Math.abs(Samples[Best].Time - Time)
                    )
                      Best = I;
                  });
                  Inspect(Best);
                }}
                onPointerLeave={() => Inspect(null)}
                onKeyDown={(E) => {
                  if (
                    ["ArrowLeft", "ArrowRight", "Home", "End"].includes(E.key)
                  ) {
                    E.preventDefault();
                    Inspect(
                      E.key === "Home"
                        ? 0
                        : E.key === "End"
                          ? Samples.length - 1
                          : Clamp(
                              Index + (E.key === "ArrowLeft" ? -1 : 1),
                              0,
                              Samples.length - 1,
                            ),
                    );
                  }
                }}
              >
                {[0, 0.5, 1].map((T) => (
                  <g key={T}>
                    <path
                      d={`M40 ${PY(Maximum * T)}H460`}
                      stroke="#ffffff14"
                      strokeDasharray="4 7"
                    />
                    <text x="475" y={PY(Maximum * T) + 4}>
                      {Math.round(Maximum * T)}
                    </text>
                  </g>
                ))}
                <path
                  d={`M40 ${PY(Target)}H460`}
                  stroke="#a3ac9260"
                  strokeDasharray="7 8"
                />
                <text x="40" y="191">
                  −{Prefs.Seconds}s
                </text>
                <text x="430" y="191">
                  now
                </text>
                {Samples.length > 1 && (
                  <>
                    <path
                      d={`${Path}L${PX(Samples.at(-1))},160L${PX(Samples[0])},160Z`}
                      fill="#a3ac920c"
                    />
                    <path
                      d={Path}
                      stroke="#dbded6"
                      strokeWidth="1.4"
                      vectorEffect="non-scaling-stroke"
                      fill="none"
                    />
                  </>
                )}
                {Sample && (
                  <>
                    <path d={`M${PX(Sample)} 20V160`} stroke="#b7c8a947" />
                    <circle
                      cx={PX(Sample)}
                      cy={PY(Sample[Prefs.Metric])}
                      r="4"
                      fill="#b8c9a8"
                    />
                  </>
                )}
              </svg>
              <div className="debug-graph-caption">
                <span>
                  {Samples.length
                    ? `${Samples.length} samples · ${Probe === null ? "latest" : ((Last.Time - Sample.Time) / 1000).toFixed(1) + "s ago"}`
                    : "Collecting live samples…"}
                </span>
                <span>
                  Reference · {Prefs.Metric === "FPS" ? "60 fps" : "16.7 ms"}
                </span>
              </div>
            </div>
          ) : (
            <div className="debug-unavailable">
              {Prefs.Metric === "GPU" ? (
                <>
                  <span>—</span>No connected GPU timing source.
                  <small>No synthetic timing graph is shown.</small>
                </>
              ) : (
                <div className="debug-scene-bars">
                  {[
                    ["Visible", VisibleCount],
                    ["Hidden", Rows.length - VisibleCount],
                  ].map(([Name, N]) => (
                    <div key={Name}>
                      <span>{Name}</span>
                      <meter min="0" max={Math.max(1, Rows.length)} value={N} />
                      <b>{N}</b>
                    </div>
                  ))}
                </div>
              )}
            </div>
          ))}
        {Prefs.View !== "graph" && (
          <dl className="debug-stats">
            {Stats.map(([Name, N]) => (
              <div key={Name}>
                <dt>{Name}</dt>
                <dd>{N}</dd>
              </div>
            ))}
          </dl>
        )}
        <details className="debug-native">
          <summary>Debug View · {DebugName}</summary>
          <p>Native buffer selection only · not connected</p>
          <p>
            HiZ {HiZ ? "on" : "off"} · Alias {Alias ? "on" : "off"} · Patch
            error {PatchError} px
          </p>
          <p>F3 next · Shift+F3 previous · F4 HiZ · F5 alias · F6 error</p>
        </details>
      </div>
      <div className="debug-controls">
        <button
          aria-pressed={!!Frozen}
          onClick={() => {
            Freeze(Frozen ? null : Timing);
            Inspect(null);
          }}
        >
          {Frozen ? "Resume samples" : "Pause samples"}
        </button>
        <select
          aria-label="Diagnostics history duration"
          value={Prefs.Seconds}
          onChange={(E) => Set("Seconds", +E.target.value)}
        >
          {[15, 30, 60].map((N) => (
            <option key={N} value={N}>
              {N}s history
            </option>
          ))}
        </select>
      </div>
      <footer className="debug-card-footer">
        <span>
          {Math.round(Box.Width)} × {Math.round(Box.Height)} · resize ↘
        </span>
        <button
          className="debug-resize"
          aria-label="Resize diagnostics card"
          title="Drag to resize · arrow keys resize 10 px"
          onPointerDown={(E) => Start(E, "resize")}
          onKeyDown={(E) => Keyboard(E, "resize")}
        >
          ⌟
        </button>
      </footer>
    </section>
  );
}
