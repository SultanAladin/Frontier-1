import React, { useMemo, useState, useEffect, useRef } from "react";
import TransformPanel from "./TransformPanel.jsx";
import {
  FORCE_MODELS,
  FORCE_MODEL_SVG_PATHS,
  ForceDomains,
  ReachEnvelopes,
  AttenuationCurves,
  DurationModes,
  MotionTopologies,
  MotionLoopModes,
  EvaluatedQuantities,
  FieldColormaps,
  StreamColorModes,
  BokehPaletteModes,
  STREAM_COLOR_PRESETS,
  DEFAULT_FORCE_OVERLAY,
  FORMULA_PRESETS,
  GetForceModelMeta,
  ResolveForceValues,
  EnsureMotionWaypoints,
  EvaluateMotionPositionAtU,
  ComputeMotionPathMetrics,
  EvaluateSceneCompositeTelemetry,
  EvaluateRadialEnvelope,
  GetCompiledForceFormula,
  SerializeForceModelToml,
  SerializeSimulationSpaceToml,
} from "./ForceFieldCatalog.js";

export function ForceModelSvg({ ModelId, Size = 18, Color }) {
  const Meta = GetForceModelMeta(ModelId);
  const Stroke = Color || Meta.Color || "#4AA8FF";
  const Markup = FORCE_MODEL_SVG_PATHS[Meta.Id] || '<circle cx="8" cy="8" r="4"/>';
  return (
    <svg
      width={Size}
      height={Size}
      viewBox="0 0 16 16"
      fill="none"
      stroke={Stroke}
      strokeWidth="1.45"
      strokeLinecap="round"
      strokeLinejoin="round"
      aria-hidden="true"
      style={{ flexShrink: 0 }}
      dangerouslySetInnerHTML={{ __html: Markup }}
    />
  );
}

function ScalarRow({ Label, Value, Min, Max, Step = 0.05, Unit = "", Decimals = 2, OnChange }) {
  const Safe = Number.isFinite(+Value) ? +Value : Min;
  const Pct = Math.max(0, Math.min(100, ((Safe - Min) / Math.max(1e-6, Max - Min)) * 100));
  const Commit = (Raw) => {
    const N = Number(Raw);
    if (Number.isFinite(N)) OnChange(Math.max(Min, Math.min(Max, N)));
  };
  return (
    <label className="field compact" data-native-property={Label}>
      <span>{Label}</span>
      <div className="slider-pill">
        <div className="split-value">
          <input
            aria-label={Label + " value"}
            type="number"
            min={Min}
            max={Max}
            step={Step}
            value={Number(Safe.toFixed(Decimals))}
            onChange={(Ev) => Commit(Ev.target.value)}
          />
          {Unit ? <small>{Unit}</small> : null}
        </div>
        <input
          aria-label={Label}
          type="range"
          min={Min}
          max={Max}
          step={Step}
          value={Safe}
          style={{ "--fill": `${Pct}%` }}
          onChange={(Ev) => Commit(Ev.target.value)}
        />
      </div>
    </label>
  );
}

const SYMBOL_PALETTE = [
  { Label: "·", Insert: " · " },
  { Label: "×", Insert: " × " },
  { Label: "r_hat", Insert: "r̂" },
  { Label: "x_hat", Insert: "x̂" },
  { Label: "y_hat", Insert: "ŷ" },
  { Label: "z_hat", Insert: "ẑ" },
  { Label: "theta", Insert: "θ̂" },
  { Label: "ω_swirl", Insert: "ω_swirl" },
  { Label: "A(r)", Insert: "A(r)" },
  { Label: "S", Insert: "S" },
  { Label: "R", Insert: "R" },
  { Label: "curl(p)", Insert: "curlNoise(p, 1.5)" },
];

const CARD_STYLE = {
  padding: "11px 12px",
  borderRadius: 10,
  background: "rgba(22, 24, 28, 0.86)",
  border: "1px solid rgba(255,255,255,0.08)",
  display: "flex",
  flexDirection: "column",
  gap: 9,
};

const METRIC_CELL_STYLE = {
  padding: "6px 8px",
  borderRadius: 7,
  background: "rgba(255,255,255,0.035)",
  border: "1px solid rgba(255,255,255,0.06)",
  display: "flex",
  flexDirection: "column",
  gap: 2,
};

export default function ForceFieldPanel({
  Subject,
  Values = {},
  Change,
  AllRows = [],
  AllValues = {},
  AllHidden = {},
  OverlaySettings = DEFAULT_FORCE_OVERLAY,
  OnChangeOverlay,
  OnSavePreset,
  OnDeleteSubject,
}) {
  const [ActiveTab, SetActiveTab] = useState("Field");
  const [CopiedToml, SetCopiedToml] = useState(false);
  const [ProbeClock, SetProbeClock] = useState(0);
  const FormulaTextareaRef = useRef(null);

  const Force = useMemo(
    () => ResolveForceValues(Values.Force, Subject?.ForceModel || "LinearWind"),
    [Values.Force, Subject?.ForceModel],
  );
  const Meta = GetForceModelMeta(Force.Model);
  const DomainInfo = ForceDomains[Meta.Domain] || ForceDomains.VectorField;
  const Overlay = { ...DEFAULT_FORCE_OVERLAY, ...(OverlaySettings || {}) };

  const Position = Values.Position || Subject?.DefaultPosition || [0, 0, 0.65];
  const Rotation = Values.Rotation || Subject?.DefaultRotation || [0, 0, 0];

  // Collect all active scene ForceField nodes so Probe, Anemometer, Curl/Div & TOML reflect the true composite field
  const SceneForceNodes = useMemo(() => {
    const Out = [];
    if (Array.isArray(AllRows) && AllRows.length > 0) {
      for (const Row of AllRows) {
        if (!Row || Row.Panel !== "force") continue;
        if (AllHidden?.[Row.Id]) continue;
        const Rec = Row.Id === Subject?.Id ? Values : (AllValues?.[Row.Id] || {});
        if (Rec?.Powered === false) continue;
        Out.push({
          Id: Row.Id,
          Name: Row.Name,
          Position: Array.isArray(Rec?.Position) ? Rec.Position : [0, 0, 0.65],
          Rotation: Array.isArray(Rec?.Rotation) ? Rec.Rotation : [0, 0, 0],
          Force: ResolveForceValues(Rec?.Force, Row.ForceModel || "LinearWind"),
        });
      }
    }
    if (Out.length === 0) {
      Out.push({
        Id: Subject?.Id || "selected-force",
        Name: Subject?.Name || Meta.Name,
        Position,
        Rotation,
        Force,
      });
    }
    return Out;
  }, [AllRows, AllValues, AllHidden, Subject?.Id, Subject?.Name, Subject?.ForceModel, Values, Force, Position, Rotation, Meta.Name]);

  useEffect(() => {
    const Id = setInterval(() => SetProbeClock((T) => T + 0.14), 140);
    return () => clearInterval(Id);
  }, []);

  const UpdateForce = (Patch) => {
    const Next = ResolveForceValues({ ...Force, ...Patch }, Patch?.Model || Force.Model);
    Change("Force", Next);
  };

  const UpdateOverlay = (Patch) => {
    if (typeof OnChangeOverlay === "function") {
      const Next = { ...Overlay, ...Patch };
      if ("ShowHeatmap" in Patch) Next.SliceEnabled = Patch.ShowHeatmap;
      if ("Colormap" in Patch) Next.ColorRamp = Patch.Colormap;
      OnChangeOverlay(Next);
    }
  };

  const ProbePos = useMemo(() => {
    if (Array.isArray(Overlay.ProbePosition) && Overlay.ProbePosition.length >= 3) {
      return [
        Number(Overlay.ProbePosition[0]) || 0,
        Number(Overlay.ProbePosition[1]) || 0,
        Number(Overlay.ProbePosition[2]) || 0,
      ];
    }
    return [Position[0] + 0.45, Position[1] + 0.2, Position[2]];
  }, [Overlay.ProbePosition, Position]);

  const SetProbeCoord = (AxisIdx, RawVal) => {
    const Num = Number(RawVal);
    if (!Number.isFinite(Num)) return;
    const NextPos = [...ProbePos];
    NextPos[AxisIdx] = Num;
    UpdateOverlay({ ProbePosition: NextPos });
  };

  // Live composite Anemometer 60s history + Differential Tensor (Vorticity |∇×F| & Divergence ∇·F)
  const Telemetry = useMemo(() => {
    const Samples = [];
    for (let I = 0; I < 48; ++I) {
      const T = ProbeClock - (47 - I) * 0.12;
      const S = EvaluateSceneCompositeTelemetry(SceneForceNodes, ProbePos[0], ProbePos[1], ProbePos[2], T, 0.08);
      Samples.push(S.Speed);
    }
    const Now = EvaluateSceneCompositeTelemetry(SceneForceNodes, ProbePos[0], ProbePos[1], ProbePos[2], ProbeClock, 0.08);
    const Peak = Math.max(...Samples, Now.Speed);
    const Lull = Math.min(...Samples, Now.Speed);
    return { Samples, Now, Peak, Lull };
  }, [SceneForceNodes, ProbePos, ProbeClock]);

  const SparklineGeometry = useMemo(() => {
    const MaxS = Math.max(6.0, Telemetry.Peak * 1.18);
    const Pts = Telemetry.Samples.map((S, I) => {
      const X = 6 + (I / Math.max(1, Telemetry.Samples.length - 1)) * 236;
      const Y = 54 - (S / MaxS) * 42;
      return [X, Y];
    });
    const LineD = "M " + Pts.map((P) => `${P[0].toFixed(1)},${P[1].toFixed(1)}`).join(" L ");
    const AreaD = `${LineD} L 242,54 L 6,54 Z`;
    return { LineD, AreaD, MaxLabel: Math.round(MaxS) };
  }, [Telemetry]);

  // Dual-curve Radial Envelope f(r): Solid = Scaled Magnitude, Dashed = Normalized Attenuation A(r)
  const EnvelopeSvg = useMemo(() => {
    const MagPts = [];
    const AttPts = [];
    const MagScale = Math.min(1.0, Math.max(0.22, Math.abs(Force.Strength) / 30.0));
    for (let I = 0; I <= 36; ++I) {
      const U = I / 36;
      const Att = EvaluateRadialEnvelope(U, Force.Attenuation, Force.FalloffExponent);
      const X = (8 + U * 232).toFixed(1);
      AttPts.push(`${X},${(56 - Att * 44).toFixed(1)}`);
      MagPts.push(`${X},${(56 - Att * MagScale * 44).toFixed(1)}`);
    }
    const CoreX = 8 + Math.min(0.96, Force.CoreRadius / Math.max(0.2, Force.Radius)) * 232;
    return {
      MagPath: "M " + MagPts.join(" L "),
      AttPath: "M " + AttPts.join(" L "),
      CoreX: CoreX.toFixed(1),
    };
  }, [Force.Attenuation, Force.FalloffExponent, Force.Strength, Force.CoreRadius, Force.Radius]);

  const MotionMetrics = useMemo(
    () => ComputeMotionPathMetrics(Position, Force),
    [Position, Force],
  );

  const CompiledFormula = useMemo(
    () => GetCompiledForceFormula(Force.FormulaCode),
    [Force.FormulaCode],
  );

  const SingleSourceToml = useMemo(
    () => SerializeForceModelToml(Subject?.Name || Meta.Name, Force, { Position, Rotation }),
    [Subject?.Name, Meta.Name, Force, Position, Rotation],
  );

  const SimulationSpaceToml = useMemo(
    () => SerializeSimulationSpaceToml(SceneForceNodes, "VectorFieldWorkspace"),
    [SceneForceNodes],
  );

  const InsertFormulaSymbol = (Snippet) => {
    const El = FormulaTextareaRef.current;
    const Cur = Force.FormulaCode || "";
    if (!El) {
      UpdateForce({ FormulaEnabled: true, FormulaCode: Cur + Snippet });
      return;
    }
    const Start = El.selectionStart ?? Cur.length;
    const End = El.selectionEnd ?? Cur.length;
    const Next = Cur.slice(0, Start) + Snippet + Cur.slice(End);
    UpdateForce({ FormulaEnabled: true, FormulaCode: Next });
    requestAnimationFrame(() => {
      try {
        El.focus();
        El.setSelectionRange(Start + Snippet.length, Start + Snippet.length);
      } catch {}
    });
  };

  const DownloadForceModelFile = (MultiSource = false) => {
    const Ext = MultiSource || Force.FormulaEnabled ? ".VectorFieldSpace" : ".ForceModel";
    const SafeStem = String(MultiSource ? "VectorFieldWorkspace" : (Subject?.Name || Meta.Id))
      .trim()
      .replace(/[^a-zA-Z0-9_-]+/g, "_");
    const Content = MultiSource ? SimulationSpaceToml : SingleSourceToml;
    const BlobObj = new Blob([Content], { type: "text/plain;charset=utf-8" });
    const Url = URL.createObjectURL(BlobObj);
    const Link = document.createElement("a");
    Link.href = Url;
    Link.download = `${SafeStem || "ForcePreset"}${Ext}`;
    document.body.appendChild(Link);
    Link.click();
    Link.remove();
    URL.revokeObjectURL(Url);
  };

  const CopyToml = async () => {
    try {
      await navigator.clipboard?.writeText(SimulationSpaceToml);
      SetCopiedToml(true);
      setTimeout(() => SetCopiedToml(false), 1500);
    } catch {}
  };

  const ActivateMotionPreset = (Mode) => {
    if (Mode === "Static") {
      UpdateForce({ MotionTopology: "Static", MotionScrubU: null });
      return;
    }
    if (Mode === "CircularOrbit") {
      UpdateForce({
        MotionTopology: "CircularOrbit",
        MotionAmplitude: Math.max(0.45, Force.MotionAmplitude || 0.85),
        MotionScrubU: null,
      });
      return;
    }
    const Waypoints =
      Array.isArray(Force.MotionWaypoints) && Force.MotionWaypoints.length >= 2
        ? Force.MotionWaypoints
        : EnsureMotionWaypoints(Position, Force, Mode);
    UpdateForce({
      MotionTopology: Mode,
      MotionWaypoints: Waypoints,
      MotionScrubU: null,
    });
  };

  const AddMotionKnot = () => {
    const Wps =
      Array.isArray(Force.MotionWaypoints) && Force.MotionWaypoints.length >= 2
        ? [...Force.MotionWaypoints.map((W) => [...W])]
        : EnsureMotionWaypoints(Position, Force, "SplineCurve");
    const P0 = Wps[Wps.length - 2];
    const P1 = Wps[Wps.length - 1];
    const Mid = [
      Number(((P0[0] + P1[0]) * 0.5).toFixed(2)),
      Number(((P0[1] + P1[1]) * 0.5 + 0.25).toFixed(2)),
      Number((Math.max(P0[2], P1[2]) + 0.16).toFixed(2)),
    ];
    Wps.splice(Wps.length - 1, 0, Mid);
    UpdateForce({ MotionTopology: "SplineCurve", MotionWaypoints: Wps });
  };

  const UpdateWaypointAxis = (WpIdx, AxisIdx, RawVal) => {
    const Num = Number(RawVal);
    if (!Number.isFinite(Num)) return;
    const Wps = (Force.MotionWaypoints || []).map((W) => [...W]);
    if (!Wps[WpIdx]) return;
    Wps[WpIdx][AxisIdx] = Num;
    UpdateForce({ MotionWaypoints: Wps });
  };

  const DeleteWaypoint = (WpIdx) => {
    const Wps = (Force.MotionWaypoints || []).filter((_, I) => I !== WpIdx);
    if (Wps.length < 2) return;
    UpdateForce({ MotionWaypoints: Wps });
  };

  const SpeedFixed = Telemetry.Now.Speed.toFixed(1);
  const [SpeedInt, SpeedDec] = SpeedFixed.split(".");

  const TabButtonStyle = (Active) => ({
    flex: 1,
    padding: "6px 8px",
    borderRadius: 999,
    border: Active ? "1px solid rgba(255,255,255,0.22)" : "1px solid rgba(255,255,255,0.08)",
    background: Active ? "rgba(255,255,255,0.12)" : "rgba(255,255,255,0.025)",
    color: Active ? "#f7f9fc" : "rgba(255,255,255,0.62)",
    fontSize: 11.5,
    fontWeight: Active ? 600 : 500,
    cursor: "pointer",
  });

  const TogglePillRow = ({ Label, Checked, OnToggle }) => (
    <button
      type="button"
      onClick={() => OnToggle(!Checked)}
      style={{
        padding: "7px 9px",
        borderRadius: 8,
        border: Checked ? "1px solid rgba(82, 224, 152, 0.45)" : "1px solid rgba(255,255,255,0.08)",
        background: Checked ? "rgba(46, 196, 126, 0.13)" : "rgba(255,255,255,0.03)",
        color: "#eef2f6",
        display: "flex",
        alignItems: "center",
        justifyContent: "space-between",
        gap: 6,
        fontSize: 11,
        fontWeight: 500,
        cursor: "pointer",
      }}
    >
      <span style={{ textAlign: "left" }}>{Label}</span>
      <span
        style={{
          width: 28,
          height: 15,
          borderRadius: 999,
          background: Checked ? "#38d47a" : "rgba(255,255,255,0.16)",
          position: "relative",
          flexShrink: 0,
        }}
      >
        <span
          style={{
            position: "absolute",
            top: 2,
            left: Checked ? 15 : 2,
            width: 11,
            height: 11,
            borderRadius: "50%",
            background: "#fff",
            transition: "left 120ms ease",
          }}
        />
      </span>
    </button>
  );

  return (
    <div className="force-field-inspector" style={{ display: "flex", flexDirection: "column", gap: 10 }}>
      {/* Inspector Header + 4-Tab Bar (Field · Formula · Layers · Cache) */}
      <div style={{ ...CARD_STYLE, borderLeft: `3px solid ${Meta.Color}` }}>
        <div style={{ display: "flex", alignItems: "center", justifyContent: "space-between", gap: 8 }}>
          <div style={{ display: "flex", alignItems: "center", gap: 8, minWidth: 0 }}>
            <ForceModelSvg ModelId={Meta.Id} Size={19} Color={Meta.Color} />
            <div style={{ minWidth: 0 }}>
              <div style={{ fontSize: 13.5, fontWeight: 600, color: "#f3f5f8", whiteSpace: "nowrap", overflow: "hidden", textOverflow: "ellipsis" }}>
                {Subject?.Name || Meta.Name}
              </div>
              <div style={{ fontSize: 10.5, color: "rgba(255,255,255,0.56)" }}>
                {DomainInfo.Label} · {Meta.Channel} · Law #{Meta.Ordinal}
              </div>
            </div>
          </div>
          <div style={{ display: "flex", alignItems: "center", gap: 5 }}>
            <button
              type="button"
              onClick={() => UpdateForce({ Enabled: !Force.Enabled })}
              style={{
                padding: "4px 9px",
                borderRadius: 999,
                fontSize: 10.5,
                fontWeight: 600,
                border: `1px solid ${Force.Enabled ? "rgba(62,216,144,0.55)" : "rgba(255,255,255,0.14)"}`,
                background: Force.Enabled ? "rgba(58, 214, 156, 0.16)" : "rgba(255,255,255,0.04)",
                color: Force.Enabled ? "#7ef2c2" : "#9aa0a6",
                cursor: "pointer",
              }}
            >
              {Force.Enabled ? "Active" : "Muted"}
            </button>
            {typeof OnDeleteSubject === "function" && (
              <button
                type="button"
                onClick={OnDeleteSubject}
                title="Delete selected ForceField source"
                style={{
                  padding: "4px 9px",
                  borderRadius: 999,
                  fontSize: 10.5,
                  border: "1px solid rgba(255,255,255,0.12)",
                  background: "rgba(255,255,255,0.04)",
                  color: "rgba(255,255,255,0.75)",
                  cursor: "pointer",
                }}
              >
                Delete
              </button>
            )}
          </div>
        </div>

        <div role="tablist" aria-label="ForceField Inspector Tabs" style={{ display: "flex", gap: 5 }}>
          {["Field", "Formula", "Layers", "Cache"].map((Tab) => (
            <button
              key={Tab}
              type="button"
              role="tab"
              aria-selected={ActiveTab === Tab}
              style={TabButtonStyle(ActiveTab === Tab)}
              onClick={() => SetActiveTab(Tab)}
            >
              {Tab}
            </button>
          ))}
        </div>
      </div>

      {/* ============================================================================================================
          TAB 1: FIELD — (1) ANEMOMETER · PROBE WIND, (2) PROBE SAMPLE TENSOR, (3) SELECTED FORCE MODEL + MOTION SPLINE
         ============================================================================================================ */}
      {ActiveTab === "Field" && (
        <>
          {/* CARD 1: ANEMOMETER · PROBE WIND (1:1 with VectorFieldVisualizer) */}
          <div style={CARD_STYLE}>
            <div style={{ display: "flex", justifyContent: "space-between", alignItems: "center", gap: 6 }}>
              <span style={{ fontSize: 10, fontWeight: 600, letterSpacing: 0.7, textTransform: "uppercase", color: "rgba(255,255,255,0.58)" }}>
                ANEMOMETER · PROBE WIND
              </span>
              <span
                style={{
                  padding: "2px 8px",
                  borderRadius: 999,
                  fontSize: 10.5,
                  fontWeight: 600,
                  background: "rgba(62, 214, 152, 0.14)",
                  border: "1px solid rgba(62, 214, 152, 0.38)",
                  color: "#7ef2c2",
                }}
              >
                Bft {Telemetry.Now.Beaufort.Number} · {Telemetry.Now.Beaufort.Label}
              </span>
            </div>

            <div style={{ display: "flex", justifyContent: "space-between", alignItems: "baseline" }}>
              <div style={{ fontSize: 28, fontWeight: 300, letterSpacing: -0.5, color: "#f6f8fb", lineHeight: 1 }}>
                {SpeedInt}
                <span style={{ fontSize: 22, opacity: 0.55 }}>.{SpeedDec}</span>
                <span style={{ fontSize: 12, opacity: 0.58, marginLeft: 4 }}>m/s</span>
              </div>
              <span
                style={{
                  padding: "3px 9px",
                  borderRadius: 999,
                  fontSize: 11,
                  background: "rgba(255,255,255,0.06)",
                  border: "1px solid rgba(255,255,255,0.1)",
                  color: "#dbe4ee",
                }}
              >
                {Telemetry.Now.CompassLabel} · {Math.round(Telemetry.Now.CompassDeg)}°
              </span>
            </div>

            <div>
              <svg viewBox="0 0 248 60" width="100%" height="58" style={{ background: "rgba(0,0,0,0.28)", borderRadius: 7, display: "block" }}>
                <defs>
                  <linearGradient id="vfAnemFill" x1="0" y1="0" x2="0" y2="1">
                    <stop offset="0%" stopColor="#6be6b9" stopOpacity="0.34" />
                    <stop offset="100%" stopColor="#6be6b9" stopOpacity="0.02" />
                  </linearGradient>
                </defs>
                <text x="8" y="13" fill="rgba(255,255,255,0.35)" fontSize="8.5">{SparklineGeometry.MaxLabel}</text>
                <line x1="6" y1="54" x2="242" y2="54" stroke="rgba(255,255,255,0.1)" strokeWidth="1" />
                <path d={SparklineGeometry.AreaD} fill="url(#vfAnemFill)" />
                <path d={SparklineGeometry.LineD} fill="none" stroke="#78e8be" strokeWidth="1.75" />
              </svg>
              <div style={{ display: "flex", justifyContent: "space-between", fontSize: 9.5, color: "rgba(255,255,255,0.42)", marginTop: 3 }}>
                <span>-60 s</span>
                <span>-30 s</span>
                <span>now</span>
              </div>
            </div>

            <div style={{ fontSize: 11, color: "rgba(255,255,255,0.72)", lineHeight: 1.35 }}>
              <strong style={{ color: "#f2f5f8" }}>Beaufort {Telemetry.Now.Beaufort.Number}</strong> — {Telemetry.Now.Beaufort.Land}.
            </div>

            {/* 4-Cell Rail: Knots · km/h · Dyn q · Altitude */}
            <div style={{ display: "grid", gridTemplateColumns: "repeat(4, 1fr)", gap: 5 }}>
              {[
                ["KNOTS", `${(Telemetry.Now.Speed * 1.94384).toFixed(1)} kt`],
                ["KM/H", `${(Telemetry.Now.Speed * 3.6).toFixed(1)}`],
                ["DYN Q", `${Math.round(Telemetry.Now.DynamicPressurePa)} Pa`],
                ["ALTITUDE", `${ProbePos[2].toFixed(1)} m`],
              ].map(([K, V]) => (
                <div key={K} style={METRIC_CELL_STYLE}>
                  <small style={{ fontSize: 9, color: "rgba(255,255,255,0.45)", letterSpacing: 0.4 }}>{K}</small>
                  <b style={{ fontSize: 11.5, color: "#f0f4f8" }}>{V}</b>
                </div>
              ))}
            </div>

            {/* 2-Cell Gust / Lull Duo */}
            <div style={{ display: "grid", gridTemplateColumns: "1fr 1fr", gap: 6 }}>
              <div style={METRIC_CELL_STYLE}>
                <small style={{ fontSize: 9.5, color: "rgba(255,255,255,0.48)" }}>Gusting to (60s peak)</small>
                <b style={{ fontSize: 12.5, color: "#f6f8fb" }}>{Telemetry.Peak.toFixed(1)} m/s</b>
              </div>
              <div style={METRIC_CELL_STYLE}>
                <small style={{ fontSize: 9.5, color: "rgba(255,255,255,0.48)" }}>Lulling to (60s min)</small>
                <b style={{ fontSize: 12.5, color: "#f6f8fb" }}>{Telemetry.Lull.toFixed(1)} m/s</b>
              </div>
            </div>

            {/* Beaufort Color Scale Bar + Pin */}
            <div>
              <div
                style={{
                  position: "relative",
                  height: 7,
                  borderRadius: 999,
                  background: "linear-gradient(90deg, #4aa8ff 0%, #48d8a4 28%, #a6e048 54%, #f7b731 76%, #eb3b5a 100%)",
                }}
              >
                <span
                  style={{
                    position: "absolute",
                    top: -3,
                    left: `calc(${(Telemetry.Now.BeaufortShare * 100).toFixed(1)}% - 3px)`,
                    width: 6,
                    height: 13,
                    borderRadius: 3,
                    background: "#ffffff",
                    boxShadow: "0 0 6px rgba(0,0,0,0.7)",
                  }}
                />
              </div>
              <div style={{ display: "flex", justifyContent: "space-between", fontSize: 9.5, color: "rgba(255,255,255,0.45)", marginTop: 4 }}>
                <span>0 Calm</span>
                <span>5 Fresh</span>
                <span>8 Gale</span>
                <span>12 Hurricane</span>
              </div>
            </div>
          </div>

          {/* CARD 2: PROBE SAMPLE & DIFFERENTIAL TENSOR (|∇×F| Vorticity & ∇·F Divergence) */}
          <div style={CARD_STYLE}>
            <div style={{ display: "flex", justifyContent: "space-between", alignItems: "center" }}>
              <strong style={{ fontSize: 12, color: "#f2f5f8" }}>Probe Sample</strong>
              <div style={{ display: "flex", alignItems: "center", gap: 6 }}>
                <small style={{ fontSize: 10.5, color: "rgba(255,255,255,0.52)" }}>
                  {Telemetry.Now.ContributingCount} Contributing
                </small>
                <button
                  type="button"
                  onClick={() =>
                    UpdateOverlay({
                      ProbePosition: [
                        Number((Position[0] + 0.42).toFixed(2)),
                        Number((Position[1] + 0.18).toFixed(2)),
                        Number(Position[2].toFixed(2)),
                      ],
                    })
                  }
                  title="Snap 3D Probe near selected ForceField"
                  style={{
                    padding: "2px 7px",
                    borderRadius: 5,
                    fontSize: 10,
                    border: "1px solid rgba(255,255,255,0.12)",
                    background: "rgba(255,255,255,0.04)",
                    color: "#cfd8e3",
                    cursor: "pointer",
                  }}
                >
                  Snap
                </button>
              </div>
            </div>

            <div style={{ display: "grid", gridTemplateColumns: "repeat(3, 1fr)", gap: 6 }}>
              {[
                ["X", "#e27470", 0],
                ["Y", "#67c485", 1],
                ["Z", "#6ea7e5", 2],
              ].map(([Axis, Col, Idx]) => (
                <label
                  key={Axis}
                  style={{
                    display: "flex",
                    alignItems: "center",
                    gap: 5,
                    padding: "4px 7px",
                    borderRadius: 6,
                    background: "rgba(0,0,0,0.28)",
                    border: "1px solid rgba(255,255,255,0.08)",
                  }}
                >
                  <b style={{ fontSize: 10.5, color: Col }}>{Axis}</b>
                  <input
                    aria-label={`Probe ${Axis}`}
                    type="number"
                    step={0.1}
                    value={Number(ProbePos[Idx].toFixed(2))}
                    onChange={(Ev) => SetProbeCoord(Idx, Ev.target.value)}
                    style={{
                      width: "100%",
                      background: "transparent",
                      border: "none",
                      color: "#f0f4f8",
                      fontSize: 11.5,
                      textAlign: "right",
                      outline: "none",
                    }}
                  />
                </label>
              ))}
            </div>

            <div style={{ display: "grid", gridTemplateColumns: "1fr 1fr", gap: 6 }}>
              <div style={METRIC_CELL_STYLE}>
                <small style={{ fontSize: 9, color: "rgba(255,255,255,0.45)", letterSpacing: 0.4 }}>FLOW VELOCITY</small>
                <b style={{ fontSize: 13, color: "#f4f7fa" }}>{Telemetry.Now.Speed.toFixed(1)} m/s</b>
              </div>
              <div style={METRIC_CELL_STYLE}>
                <small style={{ fontSize: 9, color: "rgba(255,255,255,0.45)", letterSpacing: 0.4 }}>ACCELERATION</small>
                <b style={{ fontSize: 13, color: "#f4f7fa" }}>{Telemetry.Now.AccelMag.toFixed(1)} m/s²</b>
              </div>
              <div style={METRIC_CELL_STYLE}>
                <small style={{ fontSize: 9, color: "rgba(255,255,255,0.45)", letterSpacing: 0.4 }}>VORTICITY |∇×F|</small>
                <b style={{ fontSize: 13, color: "#7ef2c2" }}>{Telemetry.Now.CurlMag.toFixed(3)} /s</b>
              </div>
              <div style={METRIC_CELL_STYLE}>
                <small style={{ fontSize: 9, color: "rgba(255,255,255,0.45)", letterSpacing: 0.4 }}>DIVERGENCE ∇·F</small>
                <b style={{ fontSize: 13, color: "#9ecbff" }}>
                  {Telemetry.Now.Divergence >= 0 ? "+" : ""}
                  {Telemetry.Now.Divergence.toFixed(3)} /s
                </b>
              </div>
            </div>
          </div>

          {/* CARD 3: SELECTED 19-LAW FORCE MODEL + RADIAL ENVELOPE + WAYPOINT CURVE ANIMATION */}
          <div style={CARD_STYLE}>
            <div style={{ display: "flex", justifyContent: "space-between", alignItems: "center", gap: 6 }}>
              <div style={{ display: "flex", alignItems: "center", gap: 6, flex: 1, minWidth: 0 }}>
                <ForceModelSvg ModelId={Meta.Id} Size={16} Color={Meta.Color} />
                <select
                  aria-label="Force Law"
                  value={Force.Model}
                  onChange={(Ev) => {
                    const NextMeta = GetForceModelMeta(Ev.target.value);
                    UpdateForce({
                      Model: NextMeta.Id,
                      Reach: NextMeta.Defaults.Reach || Force.Reach,
                      SwirlRatio: NextMeta.Defaults.SwirlRatio ?? Force.SwirlRatio,
                    });
                  }}
                  style={{ flex: 1, minWidth: 0 }}
                >
                  {FORCE_MODELS.map((Law) => (
                    <option key={Law.Id} value={Law.Id}>
                      {String(Law.Ordinal).padStart(2, "0")} · {Law.Name} ({Law.Domain})
                    </option>
                  ))}
                </select>
              </div>
              <small style={{ fontSize: 10, color: Meta.Color, whiteSpace: "nowrap" }}>
                {Meta.Domain}
              </small>
            </div>

            {/* Equation Box + Jump to Symbolic Formula Tab */}
            <div
              style={{
                padding: "6px 8px",
                borderRadius: 7,
                background: "rgba(0,0,0,0.3)",
                border: "1px solid rgba(255,255,255,0.07)",
                display: "flex",
                alignItems: "center",
                justifyContent: "space-between",
                gap: 8,
              }}
            >
              <code
                style={{
                  fontFamily: "ui-monospace, SFMono-Regular, Menlo, monospace",
                  fontSize: 10.5,
                  color: Force.FormulaEnabled ? "#ffd480" : Meta.Color,
                  overflow: "hidden",
                  textOverflow: "ellipsis",
                  whiteSpace: "nowrap",
                }}
              >
                {Force.FormulaEnabled ? Force.FormulaCode : Meta.Equation}
              </code>
              <button
                type="button"
                onClick={() => SetActiveTab("Formula")}
                style={{
                  padding: "3px 8px",
                  borderRadius: 999,
                  fontSize: 10,
                  border: "1px solid rgba(255,255,255,0.14)",
                  background: "rgba(255,255,255,0.05)",
                  color: "#eef2f6",
                  cursor: "pointer",
                  flexShrink: 0,
                }}
              >
                {Force.FormulaEnabled ? "Edit fx" : "Formula"}
              </button>
            </div>

            <TransformPanel
              Values={{ ...Values, Position, Rotation, Scale: Values.Scale || [1, 1, 1] }}
              Change={Change}
              Compact
              Space="WORLD +Z UP"
            />

            {/* 2x2 Envelope & Duration Grid */}
            <div style={{ display: "grid", gridTemplateColumns: "1fr 1fr", gap: 6 }}>
              <label className="field compact" style={{ display: "flex", flexDirection: "column", gap: 3 }}>
                <span style={{ fontSize: 10.5, opacity: 0.72 }}>Reach Shape</span>
                <select value={Force.Reach} onChange={(Ev) => UpdateForce({ Reach: Ev.target.value })}>
                  {ReachEnvelopes.map((R) => (
                    <option key={R} value={R}>{R}</option>
                  ))}
                </select>
              </label>
              <label className="field compact" style={{ display: "flex", flexDirection: "column", gap: 3 }}>
                <span style={{ fontSize: 10.5, opacity: 0.72 }}>Attenuation</span>
                <select value={Force.Attenuation} onChange={(Ev) => UpdateForce({ Attenuation: Ev.target.value })}>
                  {AttenuationCurves.map((C) => (
                    <option key={C} value={C}>{C}</option>
                  ))}
                </select>
              </label>
              <label className="field compact" style={{ display: "flex", flexDirection: "column", gap: 3 }}>
                <span style={{ fontSize: 10.5, opacity: 0.72 }}>Effect Duration</span>
                <select value={Force.Duration} onChange={(Ev) => UpdateForce({ Duration: Ev.target.value })}>
                  {DurationModes.map((D) => (
                    <option key={D} value={D}>{D}</option>
                  ))}
                </select>
              </label>
              <label className="field compact" style={{ display: "flex", flexDirection: "column", gap: 3 }}>
                <span style={{ fontSize: 10.5, opacity: 0.72 }}>Core Ratio</span>
                <input
                  type="number"
                  step={0.05}
                  min={0.08}
                  max={Math.max(0.2, Force.Radius * 0.95)}
                  value={Number(Force.CoreRadius.toFixed(2))}
                  onChange={(Ev) => UpdateForce({ CoreRadius: Number(Ev.target.value) })}
                />
              </label>
            </div>

            <ScalarRow Label="Bounded Radius" Value={Force.Radius} Min={0.4} Max={12.0} Step={0.05} Unit="m" Decimals={2} OnChange={(V) => UpdateForce({ Radius: V })} />
            <ScalarRow Label="Strength" Value={Force.Strength} Min={-60} Max={60} Step={0.25} Unit={Meta.Unit} Decimals={2} OnChange={(V) => UpdateForce({ Strength: V })} />
            <ScalarRow Label="Core Radius" Value={Force.CoreRadius} Min={0.08} Max={Math.max(0.2, Force.Radius * 0.95)} Step={0.05} Unit="m" Decimals={2} OnChange={(V) => UpdateForce({ CoreRadius: V })} />
            <ScalarRow Label="Falloff Exp" Value={Force.FalloffExponent} Min={0.25} Max={4.0} Step={0.05} Decimals={2} OnChange={(V) => UpdateForce({ FalloffExponent: V })} />
            <ScalarRow Label="Swirl / Curl" Value={Force.SwirlRatio} Min={-2.5} Max={2.5} Step={0.05} Decimals={2} OnChange={(V) => UpdateForce({ SwirlRatio: V })} />
            <ScalarRow Label="Azimuth Yaw" Value={Force.YawDeg} Min={-180} Max={180} Step={1} Unit="°" Decimals={0} OnChange={(V) => UpdateForce({ YawDeg: V })} />
            {Meta.Domain === "TurbulenceField" && (
              <>
                <ScalarRow Label="Eddy Scale" Value={Force.TurbulenceScale} Min={0.2} Max={12.0} Step={0.05} Unit="m" Decimals={2} OnChange={(V) => UpdateForce({ TurbulenceScale: V })} />
                <ScalarRow Label="Octaves" Value={Force.Octaves} Min={1} Max={5} Step={1} Decimals={0} OnChange={(V) => UpdateForce({ Octaves: Math.round(V) })} />
              </>
            )}
            <ScalarRow Label="Pulse / Freq" Value={Force.Frequency} Min={0.05} Max={3.0} Step={0.02} Unit="Hz" Decimals={2} OnChange={(V) => UpdateForce({ Frequency: V })} />

            {/* Dual-Curve Radial Envelope f(r) */}
            <div>
              <div style={{ display: "flex", justifyContent: "space-between", fontSize: 10.5, color: "rgba(255,255,255,0.62)", marginBottom: 4 }}>
                <span>Radial Envelope f(r)</span>
                <small>Solid=Mag · Dashed=A(r)</small>
              </div>
              <svg viewBox="0 0 248 64" width="100%" height="58" style={{ background: "rgba(0,0,0,0.26)", borderRadius: 7 }}>
                <line x1="8" y1="56" x2="240" y2="56" stroke="rgba(255,255,255,0.12)" strokeWidth="1" />
                <line x1={EnvelopeSvg.CoreX} y1="10" x2={EnvelopeSvg.CoreX} y2="56" stroke="rgba(255,212,128,0.32)" strokeDasharray="2.5 2.5" strokeWidth="1" />
                <path d={EnvelopeSvg.AttPath} fill="none" stroke="rgba(255,255,255,0.38)" strokeDasharray="3.5 3" strokeWidth="1.4" />
                <path d={EnvelopeSvg.MagPath} fill="none" stroke={Meta.Color} strokeWidth="2.1" />
                <text x="12" y="14" fill="rgba(255,255,255,0.5)" fontSize="9">f(0)=1.0</text>
                <text x="206" y="52" fill="rgba(255,255,255,0.5)" fontSize="9">r = R</text>
              </svg>
            </div>

            {/* Curve & Path Animation Sub-Card (A -> B Line · 3D Spline Curve · Orbit · Editable Waypoints) */}
            <div
              style={{
                padding: "9px 10px",
                borderRadius: 8,
                background: "rgba(0,0,0,0.24)",
                border: "1px solid rgba(255,255,255,0.07)",
                display: "flex",
                flexDirection: "column",
                gap: 7,
              }}
            >
              <div style={{ display: "flex", justifyContent: "space-between", alignItems: "center" }}>
                <strong style={{ fontSize: 11, color: "#f0f4f8" }}>Curve & Path Animation</strong>
                <small style={{ fontSize: 10, color: Force.MotionTopology !== "Static" ? "#ffb454" : "rgba(255,255,255,0.48)" }}>
                  {Force.MotionTopology !== "Static"
                    ? `${MotionMetrics.LengthM.toFixed(1)} m · ${MotionMetrics.DurationSec.toFixed(1)} s`
                    : "Static"}
                </small>
              </div>

              <div style={{ display: "flex", flexWrap: "wrap", gap: 4 }}>
                {[
                  ["LinearAB", "A → B Line"],
                  ["SplineCurve", "3D Spline Curve"],
                  ["CircularOrbit", "Orbit"],
                  ...(Force.MotionTopology !== "Static" ? [["Static", "Clear"]] : []),
                ].map(([Mode, Label]) => (
                  <button
                    key={Mode}
                    type="button"
                    onClick={() => ActivateMotionPreset(Mode)}
                    style={{
                      padding: "4px 8px",
                      borderRadius: 6,
                      fontSize: 10.5,
                      border: Force.MotionTopology === Mode ? "1px solid #ffb454" : "1px solid rgba(255,255,255,0.12)",
                      background: Force.MotionTopology === Mode ? "rgba(255,180,84,0.16)" : "rgba(255,255,255,0.04)",
                      color: Force.MotionTopology === Mode ? "#ffd99a" : "#dbe4ee",
                      cursor: "pointer",
                    }}
                  >
                    {Label}
                  </button>
                ))}
              </div>

              <div style={{ display: "grid", gridTemplateColumns: "1fr 1fr", gap: 6 }}>
                <label className="field compact" style={{ display: "flex", flexDirection: "column", gap: 3 }}>
                  <span style={{ fontSize: 10, opacity: 0.7 }}>Path Mode</span>
                  <select value={Force.MotionTopology} onChange={(Ev) => ActivateMotionPreset(Ev.target.value)}>
                    {MotionTopologies.map((M) => (
                      <option key={M} value={M}>{M}</option>
                    ))}
                  </select>
                </label>
                <label className="field compact" style={{ display: "flex", flexDirection: "column", gap: 3 }}>
                  <span style={{ fontSize: 10, opacity: 0.7 }}>Loop Mode</span>
                  <select
                    value={Force.MotionLoopMode || "PingPong"}
                    disabled={Force.MotionTopology === "Static"}
                    onChange={(Ev) => UpdateForce({ MotionLoopMode: Ev.target.value })}
                  >
                    {MotionLoopModes.map((Lm) => (
                      <option key={Lm} value={Lm}>{Lm}</option>
                    ))}
                  </select>
                </label>
              </div>

              {Force.MotionTopology !== "Static" && (
                <>
                  <ScalarRow Label="Orbit / Rate" Value={Force.MotionSpeedHz} Min={0.04} Max={2.0} Step={0.02} Unit="Hz" Decimals={2} OnChange={(V) => UpdateForce({ MotionSpeedHz: V })} />
                  {(Force.MotionTopology === "CircularOrbit" || Force.MotionTopology === "Figure8") ? (
                    <ScalarRow Label="Orbit Radius" Value={Force.MotionAmplitude} Min={0.15} Max={5.0} Step={0.05} Unit="m" Decimals={2} OnChange={(V) => UpdateForce({ MotionAmplitude: V })} />
                  ) : (
                    <>
                      <div style={{ display: "flex", justifyContent: "space-between", alignItems: "center" }}>
                        <small style={{ fontSize: 10, color: "rgba(255,255,255,0.58)" }}>3D Trajectory Knots (A → B)</small>
                        <button
                          type="button"
                          onClick={AddMotionKnot}
                          style={{
                            padding: "2px 7px",
                            borderRadius: 5,
                            fontSize: 10,
                            border: "1px solid rgba(255,180,84,0.4)",
                            background: "rgba(255,180,84,0.12)",
                            color: "#ffd79a",
                            cursor: "pointer",
                          }}
                        >
                          + Knot
                        </button>
                      </div>
                      <div style={{ display: "flex", flexDirection: "column", gap: 4 }}>
                        {(Force.MotionWaypoints || []).map((Wp, WIdx, Arr) => {
                          const Tag = WIdx === 0 ? "A" : WIdx === Arr.length - 1 ? "B" : `P${WIdx}`;
                          return (
                            <div
                              key={WIdx}
                              style={{
                                display: "grid",
                                gridTemplateColumns: "26px 1fr 1fr 1fr 22px",
                                gap: 4,
                                alignItems: "center",
                              }}
                            >
                              <span
                                style={{
                                  textAlign: "center",
                                  padding: "2px 0",
                                  borderRadius: 4,
                                  fontSize: 10,
                                  fontWeight: 700,
                                  background: "rgba(255,180,84,0.18)",
                                  color: "#ffd480",
                                }}
                              >
                                {Tag}
                              </span>
                              {[0, 1, 2].map((Ax) => (
                                <input
                                  key={Ax}
                                  type="number"
                                  step={0.1}
                                  value={Number((Wp[Ax] || 0).toFixed(2))}
                                  onChange={(Ev) => UpdateWaypointAxis(WIdx, Ax, Ev.target.value)}
                                  style={{
                                    width: "100%",
                                    boxSizing: "border-box",
                                    padding: "3px 5px",
                                    borderRadius: 5,
                                    background: "rgba(0,0,0,0.32)",
                                    border: "1px solid rgba(255,255,255,0.1)",
                                    color: "#eef2f6",
                                    fontSize: 10.5,
                                  }}
                                />
                              ))}
                              <button
                                type="button"
                                disabled={Arr.length <= 2}
                                onClick={() => DeleteWaypoint(WIdx)}
                                style={{
                                  padding: "2px 0",
                                  borderRadius: 4,
                                  border: "1px solid rgba(255,255,255,0.1)",
                                  background: "rgba(255,255,255,0.04)",
                                  color: Arr.length <= 2 ? "rgba(255,255,255,0.22)" : "#ff9b9b",
                                  cursor: Arr.length <= 2 ? "default" : "pointer",
                                }}
                              >
                                ×
                              </button>
                            </div>
                          );
                        })}
                      </div>
                    </>
                  )}
                </>
              )}
            </div>

            {/* Preset Save / Export Actions */}
            <div style={{ display: "flex", gap: 7 }}>
              {typeof OnSavePreset === "function" && (
                <button
                  type="button"
                  style={{ flex: 1, padding: "7px 10px", borderRadius: 7, fontSize: 11, cursor: "pointer" }}
                  onClick={() => OnSavePreset(Subject?.Name || Meta.Name, Force, { Position, Rotation })}
                >
                  Save to Asset Browser
                </button>
              )}
              <button
                type="button"
                style={{ flex: 1, padding: "7px 10px", borderRadius: 7, fontSize: 11, cursor: "pointer" }}
                onClick={() => DownloadForceModelFile(false)}
              >
                Export .ForceModel
              </button>
            </div>
          </div>
        </>
      )}

      {/* ============================================================================================================
          TAB 2: FORMULA — SYMBOLIC GPU VECTOR FIELD LANGUAGE COMPILER + SYMBOL LIBRARY + PRESETS
         ============================================================================================================ */}
      {ActiveTab === "Formula" && (
        <div style={CARD_STYLE}>
          <div style={{ display: "flex", justifyContent: "space-between", alignItems: "center" }}>
            <strong style={{ fontSize: 12, color: "#f3f5f8" }}>Symbolic GPU Language</strong>
            <small style={{ fontSize: 10.5, color: "#ffb454" }}>{Subject?.Name || Meta.Name}</small>
          </div>

          <TogglePillRow
            Label="Evaluate Custom Formula on GPU"
            Checked={Boolean(Force.FormulaEnabled)}
            OnToggle={(Next) => UpdateForce({ FormulaEnabled: Next })}
          />

          <div>
            <div style={{ display: "flex", justifyContent: "space-between", fontSize: 10.5, color: "rgba(255,255,255,0.58)", marginBottom: 5 }}>
              <span>Presets</span>
              <small>Click to load</small>
            </div>
            <div style={{ display: "grid", gridTemplateColumns: "1fr 1fr", gap: 5 }}>
              {FORMULA_PRESETS.map((Preset) => (
                <button
                  key={Preset.Id}
                  type="button"
                  onClick={() => UpdateForce({ FormulaEnabled: true, FormulaCode: Preset.Code })}
                  style={{
                    padding: "6px 8px",
                    fontSize: 10.5,
                    borderRadius: 999,
                    border: Force.FormulaCode === Preset.Code ? "1px solid #ffb454" : "1px solid rgba(255,255,255,0.12)",
                    background: Force.FormulaCode === Preset.Code ? "rgba(255,180,84,0.14)" : "rgba(255,255,255,0.04)",
                    color: "#eef2f6",
                    cursor: "pointer",
                  }}
                >
                  {Preset.Name}
                </button>
              ))}
            </div>
          </div>

          <div>
            <div style={{ display: "flex", justifyContent: "space-between", fontSize: 10.5, color: "rgba(255,255,255,0.58)", marginBottom: 5 }}>
              <span>Symbol Library</span>
              <small>Insert symbol</small>
            </div>
            <div style={{ display: "grid", gridTemplateColumns: "repeat(4, 1fr)", gap: 4 }}>
              {SYMBOL_PALETTE.map((Sym) => (
                <button
                  key={Sym.Label}
                  type="button"
                  onClick={() => InsertFormulaSymbol(Sym.Insert)}
                  style={{
                    padding: "4px 6px",
                    borderRadius: 6,
                    fontSize: 10.5,
                    fontFamily: "ui-monospace, SFMono-Regular, Menlo, monospace",
                    border: "1px solid rgba(255,255,255,0.1)",
                    background: "rgba(255,255,255,0.04)",
                    color: "#dbe4ee",
                    cursor: "pointer",
                  }}
                >
                  {Sym.Label}
                </button>
              ))}
            </div>
          </div>

          <textarea
            ref={FormulaTextareaRef}
            aria-label="VectorFieldLang Formula Source"
            rows={5}
            spellCheck={false}
            value={Force.FormulaCode}
            onChange={(Ev) => UpdateForce({ FormulaEnabled: true, FormulaCode: Ev.target.value })}
            style={{
              width: "100%",
              boxSizing: "border-box",
              padding: 9,
              borderRadius: 7,
              background: "rgba(0,0,0,0.42)",
              color: "#e6edf3",
              border: CompiledFormula?.Ok ? "1px solid rgba(58,214,156,0.45)" : "1px solid rgba(242,121,121,0.65)",
              fontFamily: "ui-monospace, SFMono-Regular, Menlo, monospace",
              fontSize: 11.5,
              lineHeight: 1.45,
            }}
          />

          <div
            style={{
              padding: "6px 9px",
              borderRadius: 7,
              fontSize: 11,
              background: CompiledFormula?.Ok ? "rgba(58,214,156,0.13)" : "rgba(242,121,121,0.16)",
              border: CompiledFormula?.Ok ? "1px solid rgba(58,214,156,0.35)" : "1px solid rgba(242,121,121,0.45)",
              color: CompiledFormula?.Ok ? "#7ef2c2" : "#ff9b9b",
            }}
          >
            {CompiledFormula?.Ok
              ? `Compiled cleanly · Output ${CompiledFormula.OutputChannel || CompiledFormula.OutputKind || "Vector3"} · GPU WGSL/GLSL + CPU synchronized`
              : `Syntax/Type Error: ${CompiledFormula?.Diagnostics?.[0]?.Message || CompiledFormula?.Error || "Invalid expression"}`}
          </div>

          <ScalarRow Label="Strength (S)" Value={Force.Strength} Min={-60} Max={60} Step={0.5} Unit="m/s" Decimals={1} OnChange={(V) => UpdateForce({ Strength: V })} />
          <ScalarRow Label="Swirl (ω_swirl)" Value={Force.SwirlRatio} Min={-2.5} Max={2.5} Step={0.05} Decimals={2} OnChange={(V) => UpdateForce({ SwirlRatio: V })} />

          <div>
            <div style={{ fontSize: 10.5, color: "rgba(255,255,255,0.58)", marginBottom: 4 }}>
              Generated SPIR-V / Slang / GLSL GPU Expression
            </div>
            <pre
              style={{
                margin: 0,
                padding: 8,
                maxHeight: 130,
                overflow: "auto",
                borderRadius: 7,
                background: "rgba(0,0,0,0.36)",
                border: "1px solid rgba(255,255,255,0.07)",
                fontSize: 10.5,
                color: "#a8e6cf",
              }}
            >
              {CompiledFormula?.Ok
                ? CompiledFormula.GlslCode || CompiledFormula.GlslBody || "// Compiled Symbolic Kernel Ready"
                : "// Fix syntax error above to emit GPU kernel"}
            </pre>
          </div>
        </div>
      )}

      {/* ============================================================================================================
          TAB 3: LAYERS — (1) GPU SLICE & COLORIMETRY + (2) BOKEH & RK4 STREAMLINES (1:1 VectorFieldVisualizer)
         ============================================================================================================ */}
      {ActiveTab === "Layers" && (
        <>
          {/* CARD 1: GPU SLICE & COLORIMETRY */}
          <div style={CARD_STYLE}>
            <div style={{ display: "flex", justifyContent: "space-between", alignItems: "center" }}>
              <strong style={{ fontSize: 12, color: "#f3f5f8" }}>GPU Slice & Colorimetry</strong>
              <small style={{ fontSize: 10.5, color: "rgba(255,255,255,0.55)" }}>
                {Overlay.SliceAxis || "XY"} @ {(Overlay.SliceOffset ?? 0).toFixed(2)} m
              </small>
            </div>

            <div style={{ display: "grid", gridTemplateColumns: "1fr 1fr", gap: 6 }}>
              <label className="field compact" style={{ display: "flex", flexDirection: "column", gap: 3 }}>
                <span style={{ fontSize: 10.5, opacity: 0.72 }}>Evaluated Field</span>
                <select value={Overlay.Quantity || "Speed"} onChange={(Ev) => UpdateOverlay({ Quantity: Ev.target.value })}>
                  {EvaluatedQuantities.map((Q) => (
                    <option key={Q} value={Q}>{Q}</option>
                  ))}
                </select>
              </label>
              <label className="field compact" style={{ display: "flex", flexDirection: "column", gap: 3 }}>
                <span style={{ fontSize: 10.5, opacity: 0.72 }}>Color Ramp</span>
                <select
                  value={FieldColormaps.includes(Overlay.Colormap) ? Overlay.Colormap : "Engineering"}
                  onChange={(Ev) => UpdateOverlay({ Colormap: Ev.target.value })}
                >
                  {FieldColormaps.map((C) => (
                    <option key={C} value={C}>{C}</option>
                  ))}
                </select>
              </label>
            </div>

            {/* Slice Axis Chips: XY (Horiz) · XZ (Front) · YZ (Side) · All 3 */}
            <div style={{ display: "grid", gridTemplateColumns: "repeat(4, 1fr)", gap: 4 }}>
              {[
                ["XY", "XY Horiz"],
                ["XZ", "XZ Front"],
                ["YZ", "YZ Side"],
                ["ALL", "All 3"],
              ].map(([Ax, Label]) => {
                const Active = (Overlay.SliceAxis || "XY") === Ax;
                return (
                  <button
                    key={Ax}
                    type="button"
                    onClick={() => UpdateOverlay({ SliceAxis: Ax, ShowHeatmap: true })}
                    style={{
                      padding: "5px 6px",
                      borderRadius: 6,
                      fontSize: 10.5,
                      fontWeight: Active ? 600 : 500,
                      border: Active ? "1px solid #4aa8ff" : "1px solid rgba(255,255,255,0.1)",
                      background: Active ? "rgba(74, 168, 255, 0.18)" : "rgba(255,255,255,0.03)",
                      color: Active ? "#eef6ff" : "rgba(255,255,255,0.68)",
                      cursor: "pointer",
                    }}
                  >
                    {Label}
                  </button>
                );
              })}
            </div>

            <ScalarRow Label="Slice Plane Offset" Value={Overlay.SliceOffset ?? 0} Min={-1.5} Max={2.5} Step={0.05} Unit="m" Decimals={2} OnChange={(V) => UpdateOverlay({ SliceOffset: V })} />
            <ScalarRow Label="Fixed Scale Ceiling" Value={Overlay.FixedMax ?? 24} Min={4} Max={80} Step={1} Unit="max" Decimals={0} OnChange={(V) => UpdateOverlay({ FixedMax: V })} />
            <ScalarRow Label="Arrow Grid Density" Value={Overlay.ArrowDensity ?? 4} Min={2} Max={7} Step={1} Decimals={0} OnChange={(V) => UpdateOverlay({ ArrowDensity: Math.round(V) })} />
            <ScalarRow
              Label="Heatmap Opacity"
              Value={Math.round((Overlay.HeatmapOpacity ?? 0.62) * 100)}
              Min={10}
              Max={95}
              Step={2}
              Unit="%"
              Decimals={0}
              OnChange={(V) => UpdateOverlay({ HeatmapOpacity: V / 100 })}
            />
            <ScalarRow Label="3D Arrow Layers" Value={Overlay.VolumeLayers ?? 1} Min={1} Max={3} Step={1} Unit="lvl" Decimals={0} OnChange={(V) => UpdateOverlay({ VolumeLayers: Math.round(V) })} />

            {/* 2x3 Visual Layer Toggle Pills */}
            <div style={{ display: "grid", gridTemplateColumns: "1fr 1fr", gap: 6 }}>
              <TogglePillRow Label="GPU Heatmap" Checked={Overlay.ShowHeatmap !== false} OnToggle={(V) => UpdateOverlay({ ShowHeatmap: V })} />
              <TogglePillRow Label="3D GPU Arrows" Checked={Overlay.ShowVelocityArrows !== false} OnToggle={(V) => UpdateOverlay({ ShowVelocityArrows: V })} />
              <TogglePillRow Label="RK4 Streamlines" Checked={Overlay.ShowStreamlines !== false} OnToggle={(V) => UpdateOverlay({ ShowStreamlines: V })} />
              <TogglePillRow Label="Reach Bounds" Checked={Overlay.ShowEnvelopes !== false} OnToggle={(V) => UpdateOverlay({ ShowEnvelopes: V })} />
              <TogglePillRow Label="Bokeh Ribbons" Checked={Boolean(Overlay.ShowBokehRibbons)} OnToggle={(V) => UpdateOverlay({ ShowBokehRibbons: V })} />
              <TogglePillRow Label="Probe & HUD" Checked={Overlay.ShowProbeHud !== false} OnToggle={(V) => UpdateOverlay({ ShowProbeHud: V })} />
            </div>
          </div>

          {/* CARD 2: BOKEH & RK4 STREAMLINES */}
          <div style={CARD_STYLE}>
            <div style={{ display: "flex", justifyContent: "space-between", alignItems: "center" }}>
              <strong style={{ fontSize: 12, color: "#f3f5f8" }}>Bokeh & RK4 Streamlines</strong>
              <small style={{ fontSize: 10.5, color: "rgba(255,255,255,0.55)" }}>
                {Overlay.StreamLineCount ?? 96} lines · {Overlay.StreamResolution ?? 28} steps
              </small>
            </div>

            <label className="field compact" style={{ display: "flex", flexDirection: "column", gap: 3 }}>
              <span style={{ fontSize: 10.5, opacity: 0.72 }}>Color Visualization</span>
              <select
                value={Overlay.StreamColorMode || "CyanSingle"}
                onChange={(Ev) => UpdateOverlay({ StreamColorMode: Ev.target.value })}
              >
                <option value="CyanSingle">Single Color · Classic Cyan</option>
                <option value="FieldPalette">Speed Color Ramp ({Overlay.Colormap || "Engineering"})</option>
                <option value="CustomSingle">Single Color · Custom</option>
                <option value="Elevation">Altitude 3D Gradient</option>
              </select>
            </label>

            <div>
              <div style={{ display: "flex", justifyContent: "space-between", fontSize: 10.5, color: "rgba(255,255,255,0.6)", marginBottom: 5 }}>
                <span>Single Color / Tint</span>
                <small>{(Overlay.StreamSingleColor || "#68dcff").toUpperCase()}</small>
              </div>
              <div style={{ display: "flex", alignItems: "center", gap: 8 }}>
                <input
                  type="color"
                  value={Overlay.StreamSingleColor || "#68dcff"}
                  onChange={(Ev) => UpdateOverlay({ StreamSingleColor: Ev.target.value, StreamColorMode: "CustomSingle" })}
                  style={{ width: 34, height: 24, padding: 0, border: "none", borderRadius: 6, background: "transparent", cursor: "pointer" }}
                />
                <div style={{ display: "flex", gap: 6 }}>
                  {STREAM_COLOR_PRESETS.map((Sw) => {
                    const SelectedSw = (Overlay.StreamSingleColor || "#68dcff").toLowerCase() === Sw.Color.toLowerCase();
                    return (
                      <button
                        key={Sw.Color}
                        type="button"
                        title={Sw.Label}
                        onClick={() =>
                          UpdateOverlay({
                            StreamSingleColor: Sw.Color,
                            StreamColorMode: Sw.Color === "#68dcff" ? "CyanSingle" : "CustomSingle",
                          })
                        }
                        style={{
                          width: 19,
                          height: 19,
                          borderRadius: "50%",
                          background: Sw.Color,
                          border: SelectedSw ? "2px solid #ffffff" : "1px solid rgba(255,255,255,0.25)",
                          cursor: "pointer",
                        }}
                      />
                    );
                  })}
                </div>
              </div>
            </div>

            <ScalarRow Label="Thickness" Value={Overlay.StreamThickness ?? 0.78} Min={0.25} Max={2.0} Step={0.05} Unit="px" Decimals={2} OnChange={(V) => UpdateOverlay({ StreamThickness: V })} />
            <ScalarRow Label="Emissive Glow" Value={Overlay.StreamEmissive ?? 1.05} Min={0.25} Max={2.4} Step={0.05} Unit="×" Decimals={2} OnChange={(V) => UpdateOverlay({ StreamEmissive: V })} />
            <ScalarRow
              Label="Ribbon Opacity"
              Value={Math.round((Overlay.StreamOpacity ?? 0.92) * 100)}
              Min={15}
              Max={100}
              Step={3}
              Unit="%"
              Decimals={0}
              OnChange={(V) => UpdateOverlay({ StreamOpacity: V / 100 })}
            />
            <ScalarRow Label="Curve Resolution" Value={Overlay.StreamResolution ?? 28} Min={10} Max={64} Step={2} Unit="seg" Decimals={0} OnChange={(V) => UpdateOverlay({ StreamResolution: Math.round(V) })} />
            <ScalarRow Label="Number of Lines" Value={Overlay.StreamLineCount ?? 96} Min={24} Max={220} Step={8} Unit="ln" Decimals={0} OnChange={(V) => UpdateOverlay({ StreamLineCount: Math.round(V) })} />

            {/* Bokeh DOF Stream Ribbon Controls */}
            <div style={{ borderTop: "1px solid rgba(255,255,255,0.08)", paddingTop: 8, display: "flex", flexDirection: "column", gap: 7 }}>
              <label className="field compact" style={{ display: "flex", flexDirection: "column", gap: 3 }}>
                <span style={{ fontSize: 10.5, opacity: 0.72 }}>Bokeh Ribbon Palette</span>
                <select
                  value={Overlay.BokehPalette || "BokehTeal"}
                  onChange={(Ev) => UpdateOverlay({ BokehPalette: Ev.target.value })}
                >
                  {BokehPaletteModes.map((Bp) => (
                    <option key={Bp} value={Bp}>
                      {Bp === "BokehTeal" ? "Bokeh Mist Teal & White" : "Follow Streamline Palette"}
                    </option>
                  ))}
                </select>
              </label>
              <ScalarRow Label="Bokeh Flow Speed" Value={Overlay.BokehSpeed ?? 0.55} Min={0.1} Max={2.0} Step={0.05} Unit="×" Decimals={2} OnChange={(V) => UpdateOverlay({ BokehSpeed: V })} />
              <ScalarRow Label="Bokeh Ribbon Count" Value={Overlay.BokehCount ?? 120} Min={30} Max={360} Step={10} Unit="tr" Decimals={0} OnChange={(V) => UpdateOverlay({ BokehCount: Math.round(V) })} />
              <ScalarRow Label="Bokeh Width" Value={Overlay.BokehThickness ?? 1.0} Min={0.35} Max={2.2} Step={0.05} Unit="×" Decimals={2} OnChange={(V) => UpdateOverlay({ BokehThickness: V })} />
              <ScalarRow Label="Bokeh Glow" Value={Overlay.BokehEmissive ?? 1.05} Min={0.35} Max={2.2} Step={0.05} Unit="em" Decimals={2} OnChange={(V) => UpdateOverlay({ BokehEmissive: V })} />
            </div>
          </div>
        </>
      )}

      {/* ============================================================================================================
          TAB 4: CACHE — 4-TIER CASCADE CLIPMAP + MULTI-SOURCE SIMULATIONSPACE TOML
         ============================================================================================================ */}
      {ActiveTab === "Cache" && (
        <>
          <div style={CARD_STYLE}>
            <div style={{ display: "flex", justifyContent: "space-between", alignItems: "center" }}>
              <strong style={{ fontSize: 12, color: "#f3f5f8" }}>4-Tier Cascade Clipmap</strong>
              <small style={{ fontSize: 10.5, color: "rgba(255,255,255,0.58)" }}>0.25 MB Total</small>
            </div>

            <div style={{ display: "grid", gridTemplateColumns: "1fr 1fr", gap: 6 }}>
              {[
                ["CASCADE 0", "2.0m · 32m"],
                ["CASCADE 1", "16.0m · 256m"],
                ["CASCADE 2", "256m · 4km"],
                ["CASCADE 3", "6250m · 100km"],
              ].map(([Title, Spec]) => (
                <div key={Title} style={METRIC_CELL_STYLE}>
                  <small style={{ fontSize: 9, color: "rgba(255,255,255,0.45)", letterSpacing: 0.4 }}>{Title}</small>
                  <b style={{ fontSize: 12, color: "#f3f5f8" }}>{Spec}</b>
                </div>
              ))}
            </div>

            <div style={{ display: "flex", justifyContent: "space-between", alignItems: "center", marginTop: 4 }}>
              <strong style={{ fontSize: 11.5, color: "#f3f5f8" }}>SimulationSpace TOML</strong>
              <div style={{ display: "flex", gap: 6 }}>
                <button
                  type="button"
                  onClick={CopyToml}
                  style={{
                    padding: "3px 9px",
                    borderRadius: 999,
                    fontSize: 10.5,
                    border: "1px solid rgba(255,255,255,0.14)",
                    background: "rgba(255,255,255,0.05)",
                    color: "#eef2f6",
                    cursor: "pointer",
                  }}
                >
                  {CopiedToml ? "Copied!" : "Copy"}
                </button>
                <button
                  type="button"
                  onClick={() => DownloadForceModelFile(true)}
                  style={{
                    padding: "3px 9px",
                    borderRadius: 999,
                    fontSize: 10.5,
                    border: "1px solid rgba(255,255,255,0.14)",
                    background: "rgba(255,255,255,0.05)",
                    color: "#eef2f6",
                    cursor: "pointer",
                  }}
                >
                  Export
                </button>
              </div>
            </div>

            <pre
              style={{
                margin: 0,
                maxHeight: 240,
                overflow: "auto",
                padding: 9,
                borderRadius: 8,
                background: "rgba(0,0,0,0.42)",
                border: "1px solid rgba(255,255,255,0.08)",
                fontSize: 10.5,
                lineHeight: 1.45,
                color: "#dbe4ee",
              }}
            >
              {SimulationSpaceToml}
            </pre>
          </div>
        </>
      )}

      {/* Inspector Telemetry Footer matching C++ & VectorFieldVisualizer */}
      <div
        style={{
          display: "flex",
          justifyContent: "space-between",
          padding: "4px 6px",
          fontSize: 10.5,
          color: "rgba(255,255,255,0.48)",
        }}
      >
        <span>EvalCompositeGPU · {SceneForceNodes.length} / 64 slots</span>
        <span>SimulationSpace Ready</span>
      </div>
    </div>
  );
}
