import React, { useEffect, useRef, useState } from "react";
import { StudioRenderEngine } from "./StudioRenderEngine.js";
import {
  ConfiguratorPresets,
  EvaluateOpeningReveal,
  OpeningRevealDurationSec,
  ResolveStudioHierarchy,
} from "./StudioLobbyScene.js";

function Lerp(A, B, T) {
  return A + (B - A) * T;
}

function SphericalFromEyeTarget(Eye, Target) {
  const Dx = Eye[0] - Target[0];
  const Dy = Eye[1] - Target[1];
  const Dz = Eye[2] - Target[2];
  const Radius = Math.max(1.2, Math.hypot(Dx, Dy, Dz));
  const Azimuth = Math.atan2(Dx, Dz);
  const Elevation = Math.asin(Math.max(-0.95, Math.min(0.95, Dy / Radius)));
  return { Radius, Azimuth, Elevation };
}

function EyeFromSpherical(Target, Radius, Azimuth, Elevation) {
  const CosEl = Math.cos(Elevation);
  return [
    Target[0] + Math.sin(Azimuth) * CosEl * Radius,
    Target[1] + Math.sin(Elevation) * Radius,
    Target[2] + Math.cos(Azimuth) * CosEl * Radius,
  ];
}

function IntersectHorizontalPlane(Ray, PlaneY) {
  if (!Ray || Math.abs(Ray.Dir[1]) < 1e-5) return null;
  const T = (PlaneY - Ray.Origin[1]) / Ray.Dir[1];
  if (T <= 0.05 || T > 120) return null;
  return [
    Ray.Origin[0] + Ray.Dir[0] * T,
    PlaneY,
    Ray.Origin[2] + Ray.Dir[2] * T,
  ];
}

// Closest parameter t along axis line (Origin + Axis * t) to camera ray
function ClosestAxisParam(Ray, AxisOrigin, AxisDir) {
  if (!Ray) return 0;
  const Wx = Ray.Origin[0] - AxisOrigin[0];
  const Wy = Ray.Origin[1] - AxisOrigin[1];
  const Wz = Ray.Origin[2] - AxisOrigin[2];
  const A = Ray.Dir[0] * Ray.Dir[0] + Ray.Dir[1] * Ray.Dir[1] + Ray.Dir[2] * Ray.Dir[2];
  const B = Ray.Dir[0] * AxisDir[0] + Ray.Dir[1] * AxisDir[1] + Ray.Dir[2] * AxisDir[2];
  const C = AxisDir[0] * AxisDir[0] + AxisDir[1] * AxisDir[1] + AxisDir[2] * AxisDir[2];
  const D = Ray.Dir[0] * Wx + Ray.Dir[1] * Wy + Ray.Dir[2] * Wz;
  const E = AxisDir[0] * Wx + AxisDir[1] * Wy + AxisDir[2] * Wz;
  const Denom = A * C - B * B;
  if (Math.abs(Denom) < 1e-6) return 0;
  return (A * E - B * D) / Denom;
}

function ClampCameraInsideLobby(Eye, Values, LevelId) {
  if (LevelId !== "studio-lobby") return Eye;
  const Cyc = Values["studio-cyclorama"] || {};
  const Ceil = Values["studio-ceiling"] || {};
  const HalfW = Math.max(6.5, Number(Cyc.CoveWidth ?? 18.0) * 0.5 - 0.85);
  const CycZ = Number(Cyc.Position?.[2] ?? -0.8);
  const FloorD = Number(Cyc.FloorDepth ?? 13.5);
  const BackZ = CycZ - FloorD * 0.58 + 1.15;
  const MaxY = Math.max(4.2, Number(Ceil.CeilingHeight ?? 7.25) - 0.78);
  return [
    Math.max(-HalfW, Math.min(HalfW, Eye[0])),
    Math.max(0.32, Math.min(MaxY, Eye[1])),
    Math.max(BackZ, Math.min(14.5, Eye[2])),
  ];
}

export default function StudioViewport({
  Rows,
  Values,
  Hidden,
  Selected,
  Select,
  RunMode,
  Paused,
  TogglePaused,
  SetRunMode,
  LevelId,
  ActivePreset,
  SelectPreset,
  IntroNonce,
  ReplayIntro,
  FocusNonce,
  GizmoMode = "translate",
  SetGizmoMode,
  OnTransformEntity,
  OnThumbnails,
  OnTelemetry,
  View = 1,
}) {
  const HostRef = useRef(null);
  const CanvasRef = useRef(null);
  const EngineRef = useRef(null);
  const HoveredAxisRef = useRef(null);
  const [HoveredAxis, SetHoveredAxis] = useState(null);
  const [ErrorMsg, SetErrorMsg] = useState("");
  const [HudState, SetHudState] = useState({
    Fps: 60,
    Phase: "Opening Reveal",
    IntroProgress: 0,
  });

  // Live mutable refs so the requestAnimationFrame loop never recreates WebGL2
  const PropsRef = useRef({ Rows, Values, Hidden, Selected, RunMode, Paused, LevelId, GizmoMode, OnTransformEntity, OnThumbnails });
  PropsRef.current = { Rows, Values, Hidden, Selected, RunMode, Paused, LevelId, GizmoMode, OnTransformEntity, OnThumbnails };

  const CamRef = useRef({
    Eye: [...(View === 2 ? ConfiguratorPresets[4].Eye : ConfiguratorPresets[0].Eye)],
    Target: [...(View === 2 ? ConfiguratorPresets[4].Target : ConfiguratorPresets[0].Target)],
    FovDeg: View === 2 ? ConfiguratorPresets[4].FovDeg : ConfiguratorPresets[0].FovDeg,
    GoalEye: [...(View === 2 ? ConfiguratorPresets[4].Eye : ConfiguratorPresets[0].Eye)],
    GoalTarget: [...(View === 2 ? ConfiguratorPresets[4].Target : ConfiguratorPresets[0].Target)],
    GoalFovDeg: View === 2 ? ConfiguratorPresets[4].FovDeg : ConfiguratorPresets[0].FovDeg,
    IntroElapsed: View === 2 ? OpeningRevealDurationSec : 0,
    OrbitAngleOffset: 0,
    ManualControl: View === 2,
    PresetGlide: false,
  });

  // Replay opening cinematic reveal when IntroNonce increments or entering Play mode
  useEffect(() => {
    if (View === 2) return;
    CamRef.current.IntroElapsed = 0;
    CamRef.current.OrbitAngleOffset = 0;
    CamRef.current.ManualControl = false;
    CamRef.current.PresetGlide = false;
  }, [IntroNonce, View]);

  // Smooth glide when user selects a Car Configurator preset (1..6)
  useEffect(() => {
    if (View === 2 || ActivePreset == null) return;
    const Preset = ConfiguratorPresets[ActivePreset] || ConfiguratorPresets[0];
    CamRef.current.GoalEye = [...Preset.Eye];
    CamRef.current.GoalTarget = [...Preset.Target];
    CamRef.current.GoalFovDeg = Preset.FovDeg;
    CamRef.current.IntroElapsed = OpeningRevealDurationSec;
    CamRef.current.OrbitAngleOffset = 0;
    CamRef.current.PresetGlide = true;
    CamRef.current.ManualControl = false;
  }, [ActivePreset, View]);

  // Frame currently selected entity when FocusNonce increments
  useEffect(() => {
    if (!FocusNonce || !Selected) return;
    const { Rows: R, Values: V, Hidden: H } = PropsRef.current;
    const Hier = ResolveStudioHierarchy(R, V, H);
    const Xf = Hier[Selected];
    if (!Xf) return;
    const Target = [Xf.Position[0], Math.max(0.7, Xf.Position[1] + 0.65), Xf.Position[2]];
    CamRef.current.GoalTarget = Target;
    CamRef.current.GoalEye = [Target[0] + 3.2, Target[1] + 1.15, Target[2] + 3.6];
    CamRef.current.GoalFovDeg = 36;
    CamRef.current.PresetGlide = true;
    CamRef.current.ManualControl = true;
  }, [FocusNonce, Selected]);

  // Mount WebGL2 renderer ONCE and run continuous render loop
  useEffect(() => {
    const Canvas = CanvasRef.current;
    const Host = HostRef.current;
    if (!Canvas || !Host) return;

    let Engine;
    try {
      Engine = new StudioRenderEngine(Canvas);
      EngineRef.current = Engine;
    } catch (Err) {
      SetErrorMsg(String(Err?.message || Err));
      return;
    }

    const Resize = () => {
      const Rect = Host.getBoundingClientRect();
      Engine.Resize(Rect.width, Rect.height);
    };
    Resize();
    const Observer = new ResizeObserver(Resize);
    Observer.observe(Host);

    let RafId = 0;
    let LastTime = performance.now();
    let TickSamples = 0;
    let FpsStamp = LastTime;
    let CurrentFps = 60;

    const Tick = (Now) => {
      const Dt = Math.min(0.05, Math.max(0.001, (Now - LastTime) / 1000));
      LastTime = Now;
      TickSamples += 1;
      if (Now - FpsStamp >= 400) {
        CurrentFps = Math.round((TickSamples * 1000) / (Now - FpsStamp));
        TickSamples = 0;
        FpsStamp = Now;
      }

      const { Rows: CurRows, Values: CurValues, Hidden: CurHidden, Selected: CurSelected, RunMode: CurMode, Paused: IsPaused, LevelId: CurLevel, GizmoMode: CurGizmo, OnThumbnails: PushThumbs } = PropsRef.current;
      const Cam = CamRef.current;
      const ConfigCam = CurValues["cam-configurator"] || {};
      const OrbitDegSec = Number(ConfigCam.OrbitSpeedDegPerSec ?? 7.5);
      const ShowroomOrbit = ConfigCam.ShowroomOrbit !== false;
      // Live optical FOV from Project-Zero's native Camera Inspector Focal Length & Sensor Width!
      const NativeFocal = Number(ConfigCam["Focal Length"] ?? 36);
      const NativeSensor = Number(ConfigCam["Sensor Width"] ?? 36);
      const NativeVertFov =
        (2 * Math.atan(NativeSensor / (16 / 9) / (2 * Math.max(10, NativeFocal))) * 180) / Math.PI;
      if (View === 1 && (Cam.ManualControl || (!Cam.PresetGlide && Cam.IntroElapsed >= OpeningRevealDurationSec))) {
        Cam.FovDeg = Lerp(Cam.FovDeg, NativeVertFov, 1 - Math.exp(-Dt * 10));
      }

      let Phase = CurMode === "PLAY" ? (IsPaused ? "Paused" : "Showroom Orbit") : "Free Orbit";

      if (View === 1 && CurMode === "PLAY" && !Cam.ManualControl) {
        if (!Cam.PresetGlide && Cam.IntroElapsed < OpeningRevealDurationSec) {
          if (!IsPaused) {
            Cam.IntroElapsed = Math.min(OpeningRevealDurationSec, Cam.IntroElapsed + Dt);
          }
          const Progress = Cam.IntroElapsed / OpeningRevealDurationSec;
          const Sample = EvaluateOpeningReveal(Progress);
          Cam.Eye = Sample.Eye;
          Cam.Target = Sample.Target;
          Cam.FovDeg = Sample.FovDeg;
          Cam.GoalEye = [...Sample.Eye];
          Cam.GoalTarget = [...Sample.Target];
          Cam.GoalFovDeg = Sample.FovDeg;
          Phase = IsPaused ? "Reveal Paused" : "Opening Reveal";
        } else {
          const Glide = 1 - Math.exp(-Dt * 5.2);
          if (Cam.PresetGlide) {
            for (let A = 0; A < 3; A++) {
              Cam.Eye[A] = Lerp(Cam.Eye[A], Cam.GoalEye[A], Glide);
              Cam.Target[A] = Lerp(Cam.Target[A], Cam.GoalTarget[A], Glide);
            }
            Cam.FovDeg = Lerp(Cam.FovDeg, Cam.GoalFovDeg, Glide);
            const Dist = Math.hypot(
              Cam.Eye[0] - Cam.GoalEye[0],
              Cam.Eye[1] - Cam.GoalEye[1],
              Cam.Eye[2] - Cam.GoalEye[2],
            );
            if (Dist < 0.03) Cam.PresetGlide = false;
          } else if (ShowroomOrbit && !IsPaused) {
            const Sph = SphericalFromEyeTarget(Cam.Eye, Cam.Target);
            const Delta = ((OrbitDegSec * Math.PI) / 180) * Dt;
            Cam.OrbitAngleOffset += Delta;
            Cam.Eye = EyeFromSpherical(Cam.Target, Sph.Radius, Sph.Azimuth + Delta, Sph.Elevation);
          }
        }
      } else if (Cam.PresetGlide) {
        const Glide = 1 - Math.exp(-Dt * 6.5);
        for (let A = 0; A < 3; A++) {
          Cam.Eye[A] = Lerp(Cam.Eye[A], Cam.GoalEye[A], Glide);
          Cam.Target[A] = Lerp(Cam.Target[A], Cam.GoalTarget[A], Glide);
        }
        Cam.FovDeg = Lerp(Cam.FovDeg, Cam.GoalFovDeg, Glide);
        const Dist = Math.hypot(
          Cam.Eye[0] - Cam.GoalEye[0],
          Cam.Eye[1] - Cam.GoalEye[1],
          Cam.Eye[2] - Cam.GoalEye[2],
        );
        if (Dist < 0.025) Cam.PresetGlide = false;
      }

      // Prevent camera from ever clipping through cyclorama walls or ceiling
      Cam.Eye = ClampCameraInsideLobby(Cam.Eye, CurValues, CurLevel);

      Engine.RenderScene({
        Rows: CurRows,
        Values: CurValues,
        Hidden: CurHidden,
        Selected: CurSelected,
        Camera: Cam,
        LevelId: CurLevel,
        EditMode: CurMode !== "PLAY",
        GizmoMode: CurGizmo,
        HoveredAxis: HoveredAxisRef.current,
      });

      if (TickSamples === 0) {
        if (View === 1 && PushThumbs) {
          const Captured = Engine.CaptureAllEntityThumbnails(CurRows, CurValues);
          if (Object.keys(Captured).length > 0) PushThumbs(Captured);
        }
        const Progress = Math.min(1, Cam.IntroElapsed / OpeningRevealDurationSec);
        SetHudState({ Fps: CurrentFps, Phase, IntroProgress: Progress });
        if (View === 1 && OnTelemetry) {
          OnTelemetry({
            Fps: CurrentFps,
            DrawCalls: Engine.LastStats.DrawCalls,
            Triangles: Engine.LastStats.Triangles,
            CachedSurfaces: Engine.LastStats.CachedSurfaces,
            ShaderCompiles: Engine.LastStats.ShaderCompiles,
            ShaderRecompiles: Engine.ShaderRecompiles,
            ShaderCompileMs: Engine.ShaderCompileMs,
            VehicleSource: Engine.LastStats.VehicleSource,
            CameraPos: Cam.Eye.map((V) => Math.round(V * 10) / 10),
          });
        }
      }

      RafId = requestAnimationFrame(Tick);
    };

    RafId = requestAnimationFrame(Tick);
    return () => {
      cancelAnimationFrame(RafId);
      Observer.disconnect();
      Engine.Dispose();
      EngineRef.current = null;
    };
  }, [View]);

  // Pointer Orbit / Pan / Dolly / 3D Gizmo Drag / Click-to-Select ray picking
  const DragRef = useRef(null);

  const PointerNdc = (Event) => {
    const Rect = CanvasRef.current.getBoundingClientRect();
    return [
      ((Event.clientX - Rect.left) / Math.max(1, Rect.width)) * 2 - 1,
      1 - ((Event.clientY - Rect.top) / Math.max(1, Rect.height)) * 2,
    ];
  };

  const OnPointerDown = (Event) => {
    if (Event.button !== 0 && Event.button !== 2) return;
    Event.currentTarget.setPointerCapture(Event.pointerId);

    // Check 3D Transform Gizmo hit first in Edit/Sim mode on left click
    if (Event.button === 0 && EngineRef.current && CanvasRef.current) {
      const [NdcX, NdcY] = PointerNdc(Event);
      const Axis = EngineRef.current.HitTestGizmo(NdcX, NdcY);
      const GState = EngineRef.current.ActiveGizmoState;
      if (Axis && GState) {
        const Ray = EngineRef.current.ComputeWorldRay(NdcX, NdcY);
        const CurVal = PropsRef.current.Values[GState.Id] || {};
        const StartPos = Array.isArray(CurVal.Position) ? [...CurVal.Position] : [0, 0, 0];
        const StartYaw = Number(CurVal.Rotation?.[1] ?? CurVal.YawDeg ?? 0);
        let StartParam = 0;
        let StartPlaneHit = null;
        if (Axis === "xz" || Axis === "ry") {
          StartPlaneHit = IntersectHorizontalPlane(Ray, GState.Origin[1]);
        } else {
          const AxisVec = Axis === "x" ? [1, 0, 0] : Axis === "y" ? [0, 1, 0] : [0, 0, 1];
          StartParam = ClosestAxisParam(Ray, GState.Origin, AxisVec);
        }
        DragRef.current = {
          Kind: "gizmo",
          Axis,
          EntityId: GState.Id,
          GizmoOrigin: [...GState.Origin],
          StartPos,
          StartYaw,
          StartParam,
          StartPlaneHit,
          StartX: Event.clientX,
          StartY: Event.clientY,
          Moved: true,
        };
        return;
      }
    }

    DragRef.current = {
      Kind: "camera",
      Button: Event.button,
      Shift: Event.shiftKey,
      StartX: Event.clientX,
      StartY: Event.clientY,
      LastX: Event.clientX,
      LastY: Event.clientY,
      Moved: false,
    };
  };

  const OnPointerMove = (Event) => {
    const Drag = DragRef.current;
    if (!Drag) {
      // Live gizmo axis hover highlight
      if (EngineRef.current && CanvasRef.current && PropsRef.current.RunMode !== "PLAY") {
        const [NdcX, NdcY] = PointerNdc(Event);
        const Hit = EngineRef.current.HitTestGizmo(NdcX, NdcY);
        if (Hit !== HoveredAxisRef.current) {
          HoveredAxisRef.current = Hit;
          SetHoveredAxis(Hit);
        }
      }
      return;
    }

    if (Drag.Kind === "gizmo" && EngineRef.current && CanvasRef.current) {
      const [NdcX, NdcY] = PointerNdc(Event);
      const Ray = EngineRef.current.ComputeWorldRay(NdcX, NdcY);
      const OnXf = PropsRef.current.OnTransformEntity;
      if (!Ray || !OnXf) return;

      if (Drag.Axis === "xz" && Drag.StartPlaneHit) {
        const Hit = IntersectHorizontalPlane(Ray, Drag.GizmoOrigin[1]);
        if (Hit) {
          const NextPos = [
            +(Drag.StartPos[0] + (Hit[0] - Drag.StartPlaneHit[0])).toFixed(3),
            Drag.StartPos[1],
            +(Drag.StartPos[2] + (Hit[2] - Drag.StartPlaneHit[2])).toFixed(3),
          ];
          OnXf(Drag.EntityId, { Position: NextPos });
        }
      } else if (Drag.Axis === "ry" && Drag.StartPlaneHit) {
        const Hit = IntersectHorizontalPlane(Ray, Drag.GizmoOrigin[1]);
        if (Hit) {
          const A0 = Math.atan2(Drag.StartPlaneHit[0] - Drag.GizmoOrigin[0], Drag.StartPlaneHit[2] - Drag.GizmoOrigin[2]);
          const A1 = Math.atan2(Hit[0] - Drag.GizmoOrigin[0], Hit[2] - Drag.GizmoOrigin[2]);
          const DeltaDeg = ((A1 - A0) * 180) / Math.PI;
          const NextYaw = +(Drag.StartYaw + DeltaDeg).toFixed(1);
          OnXf(Drag.EntityId, { YawDeg: NextYaw });
        }
      } else {
        const AxisVec = Drag.Axis === "x" ? [1, 0, 0] : Drag.Axis === "y" ? [0, 1, 0] : [0, 0, 1];
        const NextParam = ClosestAxisParam(Ray, Drag.GizmoOrigin, AxisVec);
        const Delta = NextParam - Drag.StartParam;
        const NextPos = [...Drag.StartPos];
        if (Drag.Axis === "x") NextPos[0] = +(Drag.StartPos[0] + Delta).toFixed(3);
        if (Drag.Axis === "y") NextPos[1] = +Math.max(0, Drag.StartPos[1] + Delta).toFixed(3);
        if (Drag.Axis === "z") NextPos[2] = +(Drag.StartPos[2] + Delta).toFixed(3);
        OnXf(Drag.EntityId, { Position: NextPos });
      }
      return;
    }

    const Dx = Event.clientX - Drag.LastX;
    const Dy = Event.clientY - Drag.LastY;
    Drag.LastX = Event.clientX;
    Drag.LastY = Event.clientY;
    if (Math.hypot(Event.clientX - Drag.StartX, Event.clientY - Drag.StartY) > 4) {
      Drag.Moved = true;
    }
    if (!Drag.Moved) return;

    const Cam = CamRef.current;
    Cam.ManualControl = true;
    Cam.PresetGlide = false;
    Cam.IntroElapsed = OpeningRevealDurationSec;

    if (Drag.Button === 2 || Drag.Shift) {
      const Sph = SphericalFromEyeTarget(Cam.Eye, Cam.Target);
      const Scale = Sph.Radius * 0.0014;
      const RightX = Math.cos(Sph.Azimuth);
      const RightZ = -Math.sin(Sph.Azimuth);
      Cam.Target[0] -= RightX * Dx * Scale;
      Cam.Target[2] -= RightZ * Dx * Scale;
      Cam.Target[1] = Math.max(0.15, Math.min(4.8, Cam.Target[1] + Dy * Scale));
      Cam.Eye[0] -= RightX * Dx * Scale;
      Cam.Eye[2] -= RightZ * Dx * Scale;
      Cam.Eye[1] = Math.max(0.32, Math.min(6.4, Cam.Eye[1] + Dy * Scale));
    } else {
      const Sph = SphericalFromEyeTarget(Cam.Eye, Cam.Target);
      const NextAz = Sph.Azimuth - Dx * 0.0062;
      const NextEl = Math.max(0.03, Math.min(1.22, Sph.Elevation + Dy * 0.0052));
      Cam.Eye = EyeFromSpherical(Cam.Target, Sph.Radius, NextAz, NextEl);
    }
    Cam.Eye = ClampCameraInsideLobby(Cam.Eye, PropsRef.current.Values, PropsRef.current.LevelId);
  };

  const OnPointerUp = (Event) => {
    const Drag = DragRef.current;
    DragRef.current = null;
    if (!Drag || Drag.Kind === "gizmo") return;
    if (!Drag.Moved && Drag.Button === 0 && EngineRef.current && CanvasRef.current) {
      const [NdcX, NdcY] = PointerNdc(Event);
      const HitId = EngineRef.current.PickAtNormalizedCoords(NdcX, NdcY);
      if (HitId && Select) {
        Select(HitId);
      }
    }
  };

  const OnWheel = (Event) => {
    Event.preventDefault();
    const Cam = CamRef.current;
    Cam.ManualControl = true;
    Cam.PresetGlide = false;
    Cam.IntroElapsed = OpeningRevealDurationSec;
    const Sph = SphericalFromEyeTarget(Cam.Eye, Cam.Target);
    const Factor = Math.exp(Event.deltaY * 0.0011);
    const NextR = Math.max(1.8, Math.min(15.5, Sph.Radius * Factor));
    Cam.Eye = ClampCameraInsideLobby(
      EyeFromSpherical(Cam.Target, NextR, Sph.Azimuth, Sph.Elevation),
      PropsRef.current.Values,
      PropsRef.current.LevelId,
    );
  };

  if (ErrorMsg) {
    return (
      <div className="studio-viewport-fallback">
        <strong>WebGL2 Studio Viewport unavailable</strong>
        <span>{ErrorMsg}</span>
      </div>
    );
  }

  return (
    <div
      ref={HostRef}
      className="studio-viewport-host"
      data-viewport-index={View}
      onContextMenu={(Event) => Event.preventDefault()}
    >
      <canvas
        ref={CanvasRef}
        className="studio-viewport-canvas"
        style={HoveredAxis ? { cursor: "move" } : undefined}
        onPointerDown={OnPointerDown}
        onPointerMove={OnPointerMove}
        onPointerUp={OnPointerUp}
        onWheel={OnWheel}
      />

      {View === 1 && (
        <>
          <div className="studio-viewport-top-hud">
            <span className="studio-hud-chip">
              <i className={RunMode === "PLAY" && !Paused ? "dot live" : "dot"} />
              <b>{RunMode === "PLAY" ? "PLAY · CONFIGURATOR" : RunMode + " · STUDIO"}</b>
              <span>{HudState.Phase}</span>
            </span>
            {RunMode !== "PLAY" && SetGizmoMode && (
              <div className="studio-gizmo-pill" role="group" aria-label="Transform Gizmo">
                {[
                  ["translate", "Move", "W"],
                  ["rotate", "Rotate", "E"],
                  ["off", "Off", "Q"],
                ].map(([ModeKey, Label, Key]) => (
                  <button
                    key={ModeKey}
                    type="button"
                    className={GizmoMode === ModeKey ? "active" : ""}
                    title={`${Label} gizmo (${Key})`}
                    onClick={() => SetGizmoMode(ModeKey)}
                  >
                    <span>{Label}</span>
                    <kbd>{Key}</kbd>
                  </button>
                ))}
              </div>
            )}
            <span className="studio-hud-chip subtle">
              <b>{HudState.Fps} FPS</b>
              <span>WebGL2 PBR · 0 shader recompiles</span>
            </span>
          </div>

          <div className="studio-configurator-bar" role="toolbar" aria-label="Car Configurator Camera">
            <div className="studio-configurator-transport">
              <button
                type="button"
                className={RunMode === "PLAY" && !Paused ? "active" : ""}
                title={RunMode === "PLAY" && !Paused ? "Pause configurator motion" : "Play configurator camera"}
                onClick={() => {
                  if (RunMode !== "PLAY") {
                    SetRunMode("PLAY");
                    if (Paused) TogglePaused();
                  } else {
                    TogglePaused();
                  }
                }}
              >
                {RunMode === "PLAY" && !Paused ? "❚❚ Pause" : "▶ Play"}
              </button>
              <button
                type="button"
                title="Replay 3.8 s opening cinematic reveal (0)"
                onClick={() => {
                  if (RunMode !== "PLAY") SetRunMode("PLAY");
                  if (Paused) TogglePaused();
                  ReplayIntro();
                }}
              >
                ↺ Replay Intro
              </button>
            </div>
            <div className="studio-configurator-presets">
              {ConfiguratorPresets.map((Preset, Index) => (
                <button
                  key={Preset.Id}
                  type="button"
                  className={ActivePreset === Index ? "active" : ""}
                  title={`${Preset.Label} (Shortcut ${Preset.Key})`}
                  onClick={() => SelectPreset(Index)}
                >
                  <kbd>{Preset.Key}</kbd>
                  <span>{Preset.Label}</span>
                </button>
              ))}
            </div>
          </div>
        </>
      )}
    </div>
  );
}
