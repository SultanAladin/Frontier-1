import React, { useEffect, useRef, useState } from "react";
import { StudioRenderEngine } from "./StudioRenderEngine.js";
import {
  ClampGameCameraInsideLobby,
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
  const Radius = Math.max(0.35, Math.hypot(Dx, Dy, Dz));
  const Azimuth = Math.atan2(Dx, Dz);
  const Elevation = Math.asin(Math.max(-0.998, Math.min(0.998, Dy / Radius)));
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
  if (T <= 0.05 || T > 240) return null;
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
  const PropsRef = useRef({
    Rows,
    Values,
    Hidden,
    Selected,
    RunMode,
    Paused,
    LevelId,
    GizmoMode,
    OnTransformEntity,
    OnThumbnails,
  });
  PropsRef.current = {
    Rows,
    Values,
    Hidden,
    Selected,
    RunMode,
    Paused,
    LevelId,
    GizmoMode,
    OnTransformEntity,
    OnThumbnails,
  };

  // 1) UNRESTRICTED Editor Camera (EDIT & SIM modes, and Split View 2) — NEVER clamped to studio bounds
  const EditorCamRef = useRef({
    Eye: View === 2 ? [...ConfiguratorPresets[4].Eye] : [7.6, 3.85, 10.4],
    Target: View === 2 ? [...ConfiguratorPresets[4].Target] : [0.0, 0.85, 0.2],
    FovDeg: View === 2 ? ConfiguratorPresets[4].FovDeg : 42,
    GoalEye: View === 2 ? [...ConfiguratorPresets[4].Eye] : [7.6, 3.85, 10.4],
    GoalTarget: View === 2 ? [...ConfiguratorPresets[4].Target] : [0.0, 0.85, 0.2],
    GoalFovDeg: View === 2 ? ConfiguratorPresets[4].FovDeg : 42,
    FocusGlide: false,
  });

  // 2) MOVING Game / Car Configurator Camera (PLAY mode + live frustum in EDIT/SIM) — bounded inside studio set
  const GameCamRef = useRef({
    Eye: [...ConfiguratorPresets[0].Eye],
    Target: [...ConfiguratorPresets[0].Target],
    FovDeg: ConfiguratorPresets[0].FovDeg,
    GoalEye: [...ConfiguratorPresets[0].Eye],
    GoalTarget: [...ConfiguratorPresets[0].Target],
    GoalFovDeg: ConfiguratorPresets[0].FovDeg,
    IntroElapsed: 0,
    OrbitAngleOffset: 0,
    PresetGlide: false,
    ResumeOrbitAt: 0,
  });

  // Replay opening cinematic reveal on the GAME CAMERA (never hijacks Editor Camera)
  useEffect(() => {
    if (View === 2) return;
    const GCam = GameCamRef.current;
    GCam.IntroElapsed = 0;
    GCam.OrbitAngleOffset = 0;
    GCam.PresetGlide = false;
    GCam.ResumeOrbitAt = 0;
  }, [IntroNonce, View]);

  // Smooth glide on the GAME CAMERA when user selects a Car Configurator preset (1..6)
  useEffect(() => {
    if (View === 2 || ActivePreset == null) return;
    const Preset = ConfiguratorPresets[ActivePreset] || ConfiguratorPresets[0];
    const GCam = GameCamRef.current;
    GCam.GoalEye = [...Preset.Eye];
    GCam.GoalTarget = [...Preset.Target];
    GCam.GoalFovDeg = Preset.FovDeg;
    GCam.IntroElapsed = OpeningRevealDurationSec;
    GCam.OrbitAngleOffset = 0;
    GCam.PresetGlide = true;
    GCam.ResumeOrbitAt = 0;
  }, [ActivePreset, View]);

  // Frame currently selected entity with the UNRESTRICTED Editor Camera when FocusNonce increments
  useEffect(() => {
    if (!FocusNonce || !Selected) return;
    const { Rows: R, Values: V, Hidden: H } = PropsRef.current;
    const Hier = ResolveStudioHierarchy(R, V, H);
    const Xf = Hier[Selected];
    if (!Xf) return;
    const Target = [Xf.Position[0], Math.max(0.7, Xf.Position[1] + 0.65), Xf.Position[2]];
    const ECam = EditorCamRef.current;
    ECam.GoalTarget = Target;
    ECam.GoalEye = [Target[0] + 4.2, Target[1] + 1.85, Target[2] + 4.8];
    ECam.GoalFovDeg = 42;
    ECam.FocusGlide = true;
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

      const {
        Rows: CurRows,
        Values: CurValues,
        Hidden: CurHidden,
        Selected: CurSelected,
        RunMode: CurMode,
        Paused: IsPaused,
        LevelId: CurLevel,
        GizmoMode: CurGizmo,
        OnThumbnails: PushThumbs,
      } = PropsRef.current;

      const ECam = EditorCamRef.current;
      const GCam = GameCamRef.current;
      const IsPlayView = View === 1 && CurMode === "PLAY";

      // --- 1) Step the MOVING GAME CAMERA (active in PLAY and live-simulated in SIM) ---
      const ConfigCam = CurValues["cam-configurator"] || {};
      const OrbitDegSec = Number(ConfigCam.OrbitSpeedDegPerSec ?? 7.5);
      const ShowroomOrbit = ConfigCam.ShowroomOrbit !== false;
      const NativeFocal = Number(ConfigCam["Focal Length"] ?? 36);
      const NativeSensor = Number(ConfigCam["Sensor Width"] ?? 36);
      const NativeVertFov =
        (2 * Math.atan(NativeSensor / (16 / 9) / (2 * Math.max(10, NativeFocal))) * 180) / Math.PI;

      const GameShouldStep = View === 1 && (CurMode === "PLAY" || CurMode === "SIM");
      let GamePhase = IsPaused ? "Paused" : "Showroom Orbit";

      if (GameShouldStep) {
        if (!GCam.PresetGlide && GCam.IntroElapsed < OpeningRevealDurationSec) {
          if (!IsPaused) {
            GCam.IntroElapsed = Math.min(OpeningRevealDurationSec, GCam.IntroElapsed + Dt);
          }
          const Progress = GCam.IntroElapsed / OpeningRevealDurationSec;
          const Sample = EvaluateOpeningReveal(Progress);
          GCam.Eye = Sample.Eye;
          GCam.Target = Sample.Target;
          GCam.FovDeg = Sample.FovDeg;
          GCam.GoalEye = [...Sample.Eye];
          GCam.GoalTarget = [...Sample.Target];
          GCam.GoalFovDeg = Sample.FovDeg;
          GamePhase = IsPaused ? "Reveal Paused" : "Opening Reveal";
        } else if (GCam.PresetGlide) {
          const Glide = 1 - Math.exp(-Dt * 5.2);
          for (let A = 0; A < 3; A++) {
            GCam.Eye[A] = Lerp(GCam.Eye[A], GCam.GoalEye[A], Glide);
            GCam.Target[A] = Lerp(GCam.Target[A], GCam.GoalTarget[A], Glide);
          }
          GCam.FovDeg = Lerp(GCam.FovDeg, GCam.GoalFovDeg, Glide);
          const Dist = Math.hypot(
            GCam.Eye[0] - GCam.GoalEye[0],
            GCam.Eye[1] - GCam.GoalEye[1],
            GCam.Eye[2] - GCam.GoalEye[2],
          );
          GamePhase = "Preset Glide";
          if (Dist < 0.03) GCam.PresetGlide = false;
        } else {
          GCam.FovDeg = Lerp(GCam.FovDeg, NativeVertFov, 1 - Math.exp(-Dt * 10));
          const IsDraggingGameCam = DragRef.current?.Kind === "game-camera";
          if (ShowroomOrbit && !IsPaused && !IsDraggingGameCam && Now >= GCam.ResumeOrbitAt) {
            const Sph = SphericalFromEyeTarget(GCam.Eye, GCam.Target);
            const Delta = ((OrbitDegSec * Math.PI) / 180) * Dt;
            GCam.OrbitAngleOffset += Delta;
            GCam.Eye = EyeFromSpherical(GCam.Target, Sph.Radius, Sph.Azimuth + Delta, Sph.Elevation);
          }
          GamePhase = IsPaused
            ? "Paused"
            : IsDraggingGameCam
              ? "Game Orbit"
              : ShowroomOrbit
                ? "Showroom Orbit"
                : "Game Static";
        }
        // Only the GAME CAMERA is bounded inside the studio set so it never clips walls/ceiling
        GCam.Eye = ClampGameCameraInsideLobby(GCam.Eye, CurValues, CurLevel);
      }

      // --- 2) Step the UNRESTRICTED EDITOR CAMERA (focus glide only — NEVER clamped!) ---
      if (!IsPlayView && ECam.FocusGlide) {
        const Glide = 1 - Math.exp(-Dt * 7.0);
        for (let A = 0; A < 3; A++) {
          ECam.Eye[A] = Lerp(ECam.Eye[A], ECam.GoalEye[A], Glide);
          ECam.Target[A] = Lerp(ECam.Target[A], ECam.GoalTarget[A], Glide);
        }
        ECam.FovDeg = Lerp(ECam.FovDeg, ECam.GoalFovDeg, Glide);
        const Dist = Math.hypot(
          ECam.Eye[0] - ECam.GoalEye[0],
          ECam.Eye[1] - ECam.GoalEye[1],
          ECam.Eye[2] - ECam.GoalEye[2],
        );
        if (Dist < 0.025) ECam.FocusGlide = false;
      }

      const ActiveCamera = IsPlayView ? GCam : ECam;
      const Phase = IsPlayView
        ? GamePhase
        : CurMode === "SIM"
          ? `Unrestricted · Game Cam (${GamePhase})`
          : "Unrestricted Free Orbit";

      Engine.RenderScene({
        Rows: CurRows,
        Values: CurValues,
        Hidden: CurHidden,
        Selected: CurSelected,
        Camera: ActiveCamera,
        GameCamera: !IsPlayView && View === 1 ? GCam : null,
        LevelId: CurLevel,
        EditMode: !IsPlayView,
        GizmoMode: CurGizmo,
        HoveredAxis: HoveredAxisRef.current,
      });

      if (TickSamples === 0) {
        if (View === 1 && PushThumbs) {
          const Captured = Engine.CaptureAllEntityThumbnails(CurRows, CurValues);
          if (Object.keys(Captured).length > 0) PushThumbs(Captured);
        }
        const Progress = Math.min(1, GCam.IntroElapsed / OpeningRevealDurationSec);
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
            CameraPos: ActiveCamera.Eye.map((V) => Math.round(V * 10) / 10),
            GameCameraPos: GCam.Eye.map((V) => Math.round(V * 10) / 10),
            EditorCameraPos: ECam.Eye.map((V) => Math.round(V * 10) / 10),
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
    if (Event.button !== 0 && Event.button !== 1 && Event.button !== 2) return;
    Event.currentTarget.setPointerCapture(Event.pointerId);

    const IsPlayView = View === 1 && PropsRef.current.RunMode === "PLAY";

    // Check 3D Transform Gizmo hit first in Edit/Sim mode on left click
    if (!IsPlayView && Event.button === 0 && EngineRef.current && CanvasRef.current) {
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
      Kind: IsPlayView ? "game-camera" : "editor-camera",
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

    if (Drag.Kind === "editor-camera") {
      // UNRESTRICTED Editor Camera — NO wall/ceiling/floor clamp, full pitch & unlimited pan!
      const ECam = EditorCamRef.current;
      ECam.FocusGlide = false;
      const Sph = SphericalFromEyeTarget(ECam.Eye, ECam.Target);
      if (Drag.Button === 2 || Drag.Button === 1 || Drag.Shift) {
        const Scale = Math.max(0.002, Sph.Radius * 0.0015);
        const RightX = Math.cos(Sph.Azimuth);
        const RightZ = -Math.sin(Sph.Azimuth);
        ECam.Target[0] -= RightX * Dx * Scale;
        ECam.Target[2] -= RightZ * Dx * Scale;
        ECam.Target[1] += Dy * Scale;
        ECam.Eye[0] -= RightX * Dx * Scale;
        ECam.Eye[2] -= RightZ * Dx * Scale;
        ECam.Eye[1] += Dy * Scale;
      } else {
        const NextAz = Sph.Azimuth - Dx * 0.0062;
        const NextEl = Math.max(-1.54, Math.min(1.54, Sph.Elevation + Dy * 0.0052));
        ECam.Eye = EyeFromSpherical(ECam.Target, Sph.Radius, NextAz, NextEl);
      }
      return;
    }

    // GAME CAMERA interaction (PLAY mode) — bounded inside studio cyclorama & resumes smooth orbit after release
    const GCam = GameCamRef.current;
    GCam.PresetGlide = false;
    GCam.IntroElapsed = OpeningRevealDurationSec;
    GCam.ResumeOrbitAt = performance.now() + 1800;

    if (Drag.Button === 2 || Drag.Button === 1 || Drag.Shift) {
      const Sph = SphericalFromEyeTarget(GCam.Eye, GCam.Target);
      const Scale = Sph.Radius * 0.0012;
      const RightX = Math.cos(Sph.Azimuth);
      const RightZ = -Math.sin(Sph.Azimuth);
      GCam.Target[0] = Math.max(-4.5, Math.min(4.5, GCam.Target[0] - RightX * Dx * Scale));
      GCam.Target[2] = Math.max(-4.5, Math.min(4.5, GCam.Target[2] - RightZ * Dx * Scale));
      GCam.Target[1] = Math.max(0.25, Math.min(2.8, GCam.Target[1] + Dy * Scale));
      GCam.Eye[0] -= RightX * Dx * Scale;
      GCam.Eye[2] -= RightZ * Dx * Scale;
      GCam.Eye[1] += Dy * Scale;
    } else {
      const Sph = SphericalFromEyeTarget(GCam.Eye, GCam.Target);
      const NextAz = Sph.Azimuth - Dx * 0.0062;
      const NextEl = Math.max(0.05, Math.min(1.15, Sph.Elevation + Dy * 0.0052));
      GCam.Eye = EyeFromSpherical(GCam.Target, Sph.Radius, NextAz, NextEl);
    }
    GCam.Eye = ClampGameCameraInsideLobby(GCam.Eye, PropsRef.current.Values, PropsRef.current.LevelId);
  };

  const OnPointerUp = (Event) => {
    const Drag = DragRef.current;
    DragRef.current = null;
    if (!Drag || Drag.Kind === "gizmo") return;
    if (Drag.Kind === "game-camera" && Drag.Moved) {
      GameCamRef.current.ResumeOrbitAt = performance.now() + 1800;
      return;
    }
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
    const IsPlayView = View === 1 && PropsRef.current.RunMode === "PLAY";
    const Factor = Math.exp(Event.deltaY * 0.0011);

    if (!IsPlayView) {
      // UNRESTRICTED Editor Camera zoom — NO room clamp, wide 0.35 m .. 240 m range
      const ECam = EditorCamRef.current;
      ECam.FocusGlide = false;
      const Sph = SphericalFromEyeTarget(ECam.Eye, ECam.Target);
      const NextR = Math.max(0.35, Math.min(240, Sph.Radius * Factor));
      ECam.Eye = EyeFromSpherical(ECam.Target, NextR, Sph.Azimuth, Sph.Elevation);
      return;
    }

    // GAME CAMERA zoom (PLAY mode) — bounded cleanly inside the studio set
    const GCam = GameCamRef.current;
    GCam.PresetGlide = false;
    GCam.IntroElapsed = OpeningRevealDurationSec;
    GCam.ResumeOrbitAt = performance.now() + 1800;
    const Sph = SphericalFromEyeTarget(GCam.Eye, GCam.Target);
    const NextR = Math.max(2.2, Math.min(12.5, Sph.Radius * Factor));
    GCam.Eye = ClampGameCameraInsideLobby(
      EyeFromSpherical(GCam.Target, NextR, Sph.Azimuth, Sph.Elevation),
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
      data-active-camera={View === 1 && RunMode === "PLAY" ? "game" : "editor"}
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
              <b>{RunMode === "PLAY" ? "PLAY · GAME CAMERA" : RunMode + " · EDITOR CAMERA"}</b>
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
                title={RunMode === "PLAY" && !Paused ? "Pause game camera motion" : "Play game camera"}
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
                title="Replay 3.8 s Game Camera opening reveal (0)"
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
                  title={`Game Camera · ${Preset.Label} (Shortcut ${Preset.Key})`}
                  onClick={() => {
                    if (RunMode === "EDIT") SetRunMode("PLAY");
                    SelectPreset(Index);
                  }}
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
