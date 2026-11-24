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
  OnTelemetry,
  View = 1,
}) {
  const HostRef = useRef(null);
  const CanvasRef = useRef(null);
  const EngineRef = useRef(null);
  const [ErrorMsg, SetErrorMsg] = useState("");
  const [HudState, SetHudState] = useState({
    Fps: 60,
    Phase: "Opening Reveal",
    IntroProgress: 0,
  });

  // Live mutable refs so the requestAnimationFrame loop never recreates WebGL2
  const PropsRef = useRef({ Rows, Values, Hidden, Selected, RunMode, Paused, LevelId });
  PropsRef.current = { Rows, Values, Hidden, Selected, RunMode, Paused, LevelId };

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

      const { Rows: CurRows, Values: CurValues, Hidden: CurHidden, Selected: CurSelected, RunMode: CurMode, Paused: IsPaused, LevelId: CurLevel } = PropsRef.current;
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

      Engine.RenderScene({
        Rows: CurRows,
        Values: CurValues,
        Hidden: CurHidden,
        Selected: CurSelected,
        Camera: Cam,
        LevelId: CurLevel,
        EditMode: CurMode !== "PLAY",
      });

      if (TickSamples === 0) {
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

  // Pointer Orbit / Pan / Dolly / Click-to-Select ray picking
  const DragRef = useRef(null);

  const OnPointerDown = (Event) => {
    if (Event.button !== 0 && Event.button !== 2) return;
    Event.currentTarget.setPointerCapture(Event.pointerId);
    DragRef.current = {
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
    if (!Drag) return;
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
      // Pan camera parallel to view plane
      const Sph = SphericalFromEyeTarget(Cam.Eye, Cam.Target);
      const Scale = Sph.Radius * 0.0014;
      const RightX = Math.cos(Sph.Azimuth);
      const RightZ = -Math.sin(Sph.Azimuth);
      Cam.Target[0] -= RightX * Dx * Scale;
      Cam.Target[2] -= RightZ * Dx * Scale;
      Cam.Target[1] = Math.max(0.15, Math.min(4.5, Cam.Target[1] + Dy * Scale));
      Cam.Eye[0] -= RightX * Dx * Scale;
      Cam.Eye[2] -= RightZ * Dx * Scale;
      Cam.Eye[1] = Math.max(0.25, Math.min(5.7, Cam.Eye[1] + Dy * Scale));
    } else {
      // Smooth studio turntable orbit
      const Sph = SphericalFromEyeTarget(Cam.Eye, Cam.Target);
      const NextAz = Sph.Azimuth - Dx * 0.0062;
      const NextEl = Math.max(0.03, Math.min(1.25, Sph.Elevation + Dy * 0.0052));
      Cam.Eye = EyeFromSpherical(Cam.Target, Sph.Radius, NextAz, NextEl);
    }
  };

  const OnPointerUp = (Event) => {
    const Drag = DragRef.current;
    DragRef.current = null;
    if (!Drag) return;
    if (!Drag.Moved && Drag.Button === 0 && EngineRef.current && CanvasRef.current) {
      const Rect = CanvasRef.current.getBoundingClientRect();
      const NdcX = ((Event.clientX - Rect.left) / Rect.width) * 2 - 1;
      const NdcY = 1 - ((Event.clientY - Rect.top) / Rect.height) * 2;
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
    const NextR = Math.max(1.8, Math.min(22.0, Sph.Radius * Factor));
    Cam.Eye = EyeFromSpherical(Cam.Target, NextR, Sph.Azimuth, Sph.Elevation);
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
