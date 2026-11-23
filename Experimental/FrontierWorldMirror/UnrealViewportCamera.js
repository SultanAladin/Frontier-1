//==========================================================================================================================================
// FILE: UnrealViewportCamera.js
// PURPOSE: Authentic Unreal Engine 5 Editor Viewport Camera math (Right-Handed Z-Up World: +X Right, +Y Forward, +Z Up).
//          Supports simultaneous RMB Free-Look + WASDQE 6-DOF Fly + Shift Sprint + RMB+Wheel Speed Scalar + Orbit + Pan + Smooth Dolly.
//==========================================================================================================================================

export const UNREAL_PITCH_LIMIT = 1.4835; // ±85°

export function BuildUnrealCameraBasis(Yaw, Pitch) {
  const ClampedPitch = Math.max(-UNREAL_PITCH_LIMIT, Math.min(UNREAL_PITCH_LIMIT, Pitch));
  const CosP = Math.cos(ClampedPitch);
  const SinP = Math.sin(ClampedPitch);
  const CosY = Math.cos(Yaw);
  const SinY = Math.sin(Yaw);
  // Forward = [cos(P)*cos(Y), cos(P)*sin(Y), sin(P)]
  // Right   = normalize(cross(Forward, [0, 0, 1])) = [sin(Y), -cos(Y), 0]
  // Up      = cross(Right, Forward) = [-sin(P)*cos(Y), -sin(P)*sin(Y), cos(P)]
  return {
    Forward: [CosP * CosY, CosP * SinY, SinP],
    Right: [SinY, -CosY, 0],
    Up: [-SinP * CosY, -SinP * SinY, CosP],
  };
}

export function DeriveUnrealCameraState(Origin, Target, SpeedScalar = 1.0) {
  const Dx = Target[0] - Origin[0];
  const Dy = Target[1] - Origin[1];
  const Dz = Target[2] - Origin[2];
  const FocusDist = Math.max(0.35, Math.sqrt(Dx * Dx + Dy * Dy + Dz * Dz));
  const Fx = Dx / FocusDist;
  const Fy = Dy / FocusDist;
  const Fz = Dz / FocusDist;
  const Yaw = Math.atan2(Fy, Fx);
  const Pitch = Math.max(
    -UNREAL_PITCH_LIMIT,
    Math.min(UNREAL_PITCH_LIMIT, Math.asin(Math.max(-1, Math.min(1, Fz)))),
  );
  return {
    Position: [Origin[0], Origin[1], Origin[2]],
    Target: [Target[0], Target[1], Target[2]],
    Yaw,
    Pitch,
    FocusDist,
    SpeedScalar,
  };
}

export function ApplyUnrealMouseLook(Cam, DeltaX, DeltaY, Sensitivity = 0.0038) {
  const Yaw = Cam.Yaw - DeltaX * Sensitivity;
  const Pitch = Math.max(
    -UNREAL_PITCH_LIMIT,
    Math.min(UNREAL_PITCH_LIMIT, Cam.Pitch - DeltaY * Sensitivity),
  );
  const { Forward } = BuildUnrealCameraBasis(Yaw, Pitch);
  const Pos = Cam.Position;
  return {
    ...Cam,
    Yaw,
    Pitch,
    Target: [
      Pos[0] + Forward[0] * Cam.FocusDist,
      Pos[1] + Forward[1] * Cam.FocusDist,
      Pos[2] + Forward[2] * Cam.FocusDist,
    ],
  };
}

export function ApplyUnrealOrbit(Cam, Pivot, DeltaX, DeltaY, Sensitivity = 0.0052) {
  const Yaw = Cam.Yaw - DeltaX * Sensitivity;
  const Pitch = Math.max(
    -1.46,
    Math.min(1.46, Cam.Pitch - DeltaY * Sensitivity * 0.92),
  );
  const { Forward } = BuildUnrealCameraBasis(Yaw, Pitch);
  const Dist = Math.max(0.35, Cam.FocusDist);
  return {
    ...Cam,
    Yaw,
    Pitch,
    Position: [
      Pivot[0] - Forward[0] * Dist,
      Pivot[1] - Forward[1] * Dist,
      Pivot[2] - Forward[2] * Dist,
    ],
    Target: [Pivot[0], Pivot[1], Pivot[2]],
  };
}

export function ApplyUnrealPan(Cam, DeltaX, DeltaY, ScaleFactor = 0.0015) {
  const { Right, Up } = BuildUnrealCameraBasis(Cam.Yaw, Cam.Pitch);
  const Scale = Math.max(0.35, Cam.FocusDist) * ScaleFactor;
  const Sx = -Right[0] * DeltaX * Scale + Up[0] * DeltaY * Scale;
  const Sy = -Right[1] * DeltaX * Scale + Up[1] * DeltaY * Scale;
  const Sz = -Right[2] * DeltaX * Scale + Up[2] * DeltaY * Scale;
  return {
    ...Cam,
    Position: [Cam.Position[0] + Sx, Cam.Position[1] + Sy, Cam.Position[2] + Sz],
    Target: [Cam.Target[0] + Sx, Cam.Target[1] + Sy, Cam.Target[2] + Sz],
  };
}

export function ApplyUnrealDolly(Cam, Steps, StepGain = 0.105) {
  const { Forward } = BuildUnrealCameraBasis(Cam.Yaw, Cam.Pitch);
  const NextDist = Math.max(0.35, Math.min(42.0, Cam.FocusDist * Math.exp(Steps * StepGain)));
  return {
    ...Cam,
    FocusDist: NextDist,
    Position: [
      Cam.Target[0] - Forward[0] * NextDist,
      Cam.Target[1] - Forward[1] * NextDist,
      Cam.Target[2] - Forward[2] * NextDist,
    ],
  };
}

export function StepUnrealWasdCamera(Cam, KeysSet, Dt, BaseSpeed = 3.4) {
  let InFwd = 0;
  let InRight = 0;
  let InUp = 0;
  if (KeysSet.has("KeyW") || KeysSet.has("ArrowUp") || KeysSet.has("w")) InFwd += 1;
  if (KeysSet.has("KeyS") || KeysSet.has("ArrowDown") || KeysSet.has("s")) InFwd -= 1;
  if (KeysSet.has("KeyD") || KeysSet.has("ArrowRight") || KeysSet.has("d")) InRight += 1;
  if (KeysSet.has("KeyA") || KeysSet.has("ArrowLeft") || KeysSet.has("a")) InRight -= 1;
  if (KeysSet.has("KeyE") || KeysSet.has("PageUp") || KeysSet.has("e")) InUp += 1;
  if (KeysSet.has("KeyQ") || KeysSet.has("PageDown") || KeysSet.has("q")) InUp -= 1;

  if (InFwd === 0 && InRight === 0 && InUp === 0) return null;

  const { Forward, Right } = BuildUnrealCameraBasis(Cam.Yaw, Cam.Pitch);
  let DirX = Forward[0] * InFwd + Right[0] * InRight;
  let DirY = Forward[1] * InFwd + Right[1] * InRight;
  let DirZ = Forward[2] * InFwd + Right[2] * InRight + InUp;
  const DirLen = Math.sqrt(DirX * DirX + DirY * DirY + DirZ * DirZ);
  if (DirLen < 1e-6) return null;
  DirX /= DirLen;
  DirY /= DirLen;
  DirZ /= DirLen;

  const Sprint =
    KeysSet.has("ShiftLeft") || KeysSet.has("ShiftRight") || KeysSet.has("shift")
      ? 2.5
      : 1.0;
  const StepDist = BaseSpeed * (Cam.SpeedScalar || 1.0) * Sprint * Dt;
  const NextPos = [
    Cam.Position[0] + DirX * StepDist,
    Cam.Position[1] + DirY * StepDist,
    Cam.Position[2] + DirZ * StepDist,
  ];
  return {
    ...Cam,
    Position: NextPos,
    Target: [
      NextPos[0] + Forward[0] * Cam.FocusDist,
      NextPos[1] + Forward[1] * Cam.FocusDist,
      NextPos[2] + Forward[2] * Cam.FocusDist,
    ],
  };
}
