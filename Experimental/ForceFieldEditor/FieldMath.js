/*============================================================================================================================================
                                                              FIELDMATH.JS
============================================================================================================================================*/
// Analytic +Z-up composite vector field evaluation, finite-difference quantities, colour ramps, and streamline tracing
// for the Frontier ForceField Editor HTML reference studio. Pure functions; no engine connection, no rigid-body physics.

"use strict";

//------------------------------------------------------------------------------------------------------------------------
//                                        NOISE POTENTIAL & CURL SAMPLING
//------------------------------------------------------------------------------------------------------------------------

function Hash3(X, Y, Z) {
  const S = Math.sin(X * 127.1 + Y * 311.7 + Z * 74.7) * 43758.5453123;
  return S - Math.floor(S);
}

function ValueNoise3(X, Y, Z) {
  const Ix = Math.floor(X), Iy = Math.floor(Y), Iz = Math.floor(Z);
  const Fx = X - Ix, Fy = Y - Iy, Fz = Z - Iz;
  const Ux = Fx * Fx * Fx * (Fx * (Fx * 6 - 15) + 10);
  const Uy = Fy * Fy * Fy * (Fy * (Fy * 6 - 15) + 10);
  const Uz = Fz * Fz * Fz * (Fz * (Fz * 6 - 15) + 10);
  const Lerp = (A, B, T) => A + (B - A) * T;
  const N000 = Hash3(Ix, Iy, Iz), N100 = Hash3(Ix + 1, Iy, Iz);
  const N010 = Hash3(Ix, Iy + 1, Iz), N110 = Hash3(Ix + 1, Iy + 1, Iz);
  const N001 = Hash3(Ix, Iy, Iz + 1), N101 = Hash3(Ix + 1, Iy, Iz + 1);
  const N011 = Hash3(Ix, Iy + 1, Iz + 1), N111 = Hash3(Ix + 1, Iy + 1, Iz + 1);
  return (
    Lerp(
      Lerp(Lerp(N000, N100, Ux), Lerp(N010, N110, Ux), Uy),
      Lerp(Lerp(N001, N101, Ux), Lerp(N011, N111, Ux), Uy),
      Uz,
    ) * 2.0 - 1.0
  );
}

function PotentialFbmVec3(X, Y, Z, Octaves = 3) {
  let Ax = 0, Ay = 0, Az = 0, Amp = 1.0, Freq = 1.0, Norm = 0;
  for (let O = 0; O < Octaves; ++O) {
    Ax += Amp * ValueNoise3(X * Freq + 13.7, Y * Freq - 5.3, Z * Freq + 2.1);
    Ay += Amp * ValueNoise3(X * Freq - 9.2, Y * Freq + 17.4, Z * Freq - 6.8);
    Az += Amp * ValueNoise3(X * Freq + 4.9, Y * Freq - 11.1, Z * Freq + 21.3);
    Norm += Amp;
    Amp *= 0.5;
    Freq *= 2.02;
  }
  const Inv = Norm > 1e-6 ? 1.0 / Norm : 1.0;
  return [Ax * Inv, Ay * Inv, Az * Inv];
}

// Divergence-free 3D curl(potential) via central finite differences (div(curl Psi) = 0 identically).
function SampleCurlNoise3D(Px, Py, Pz, ScaleMeters = 1.3, Octaves = 3, TimeSec = 0) {
  const InvS = 1.0 / Math.max(0.15, ScaleMeters);
  const Qx = Px * InvS + TimeSec * 0.24;
  const Qy = Py * InvS - TimeSec * 0.19;
  const Qz = Pz * InvS + TimeSec * 0.15;
  const Eps = 1e-3;
  const Pxp = PotentialFbmVec3(Qx + Eps, Qy, Qz, Octaves);
  const Pxm = PotentialFbmVec3(Qx - Eps, Qy, Qz, Octaves);
  const Pyp = PotentialFbmVec3(Qx, Qy + Eps, Qz, Octaves);
  const Pym = PotentialFbmVec3(Qx, Qy - Eps, Qz, Octaves);
  const Pzp = PotentialFbmVec3(Qx, Qy, Qz + Eps, Octaves);
  const Pzm = PotentialFbmVec3(Qx, Qy, Qz - Eps, Octaves);
  const Inv2E = 0.5 / Eps;
  const dPz_dy = (Pyp[2] - Pym[2]) * Inv2E;
  const dPy_dz = (Pzp[1] - Pzm[1]) * Inv2E;
  const dPx_dz = (Pzp[0] - Pzm[0]) * Inv2E;
  const dPz_dx = (Pxp[2] - Pxm[2]) * Inv2E;
  const dPy_dx = (Pxp[1] - Pxm[1]) * Inv2E;
  const dPx_dy = (Pyp[0] - Pym[0]) * Inv2E;
  return [dPz_dy - dPy_dz, dPx_dz - dPz_dx, dPy_dx - dPx_dy];
}

//------------------------------------------------------------------------------------------------------------------------
//                                       ENVELOPES, ATTENUATION & MOTION
//------------------------------------------------------------------------------------------------------------------------

function EvaluateReachNormalizedDistance(Dx, Dy, Dz, Radius, Reach) {
  const SafeR = Math.max(0.1, Radius);
  if (Reach === "Infinite") return 0.0;
  if (Reach === "Column") {
    const RadialXY = Math.hypot(Dx, Dy) / SafeR;
    const AxialZ = Math.abs(Dz) / (SafeR * 1.15);
    return Math.max(RadialXY, AxialZ);
  }
  if (Reach === "Box") {
    return Math.max(Math.abs(Dx), Math.abs(Dy), Math.abs(Dz)) / SafeR;
  }
  return Math.hypot(Dx, Dy, Dz) / SafeR;
}

function EvaluateRadialEnvelope(NormU, Curve = "SmoothStep", Exponent = 1.35) {
  if (NormU >= 1.0) return 0.0;
  if (Curve === "Constant") return 1.0;
  const U = Math.max(0.0, Math.min(1.0, NormU));
  if (Curve === "Linear") return Math.pow(1.0 - U, Exponent);
  if (Curve === "InverseSquare") {
    const Tail = 1.0 - U * U * (3.0 - 2.0 * U);
    return (1.0 / (1.0 + 6.0 * U * U)) * Tail;
  }
  const S = 1.0 - U * U;
  return Math.pow(Math.max(0.0, S * S), Exponent * 0.75);
}

function EvalCatmullRom3(Pts, U) {
  const N = Pts.length;
  if (N === 0) return [0, 0, 0];
  if (N === 1) return [Pts[0][0], Pts[0][1], Pts[0][2]];
  const Scaled = Math.max(0, Math.min(1, U)) * (N - 1);
  const Seg = Math.min(N - 2, Math.floor(Scaled));
  const T = Scaled - Seg;
  const T2 = T * T;
  const T3 = T2 * T;
  const P0 = Pts[Math.max(0, Seg - 1)];
  const P1 = Pts[Seg];
  const P2 = Pts[Seg + 1];
  const P3 = Pts[Math.min(N - 1, Seg + 2)];
  const Interp = (K) =>
    0.5 *
    (2 * P1[K] +
      (-P0[K] + P2[K]) * T +
      (2 * P0[K] - 5 * P1[K] + 4 * P2[K] - P3[K]) * T2 +
      (-P0[K] + 3 * P1[K] - 3 * P2[K] + P3[K]) * T3);
  return [Interp(0), Interp(1), Interp(2)];
}

function EvaluateMotionCenter(BaseCenter, Force, TimeSec = 0) {
  const Cx = BaseCenter?.[0] || 0;
  const Cy = BaseCenter?.[1] || 0;
  const Cz = BaseCenter?.[2] || 0;
  const Pattern = Force.MotionPattern || "Static";
  if (Pattern === "Static") return [Cx, Cy, Cz];

  const RawPhase = TimeSec * (Force.MotionSpeedHz || 0.22) + (Force.MotionPhase || 0);
  const Loop = Force.MotionLoopMode || "PingPong";
  let U = 0;
  if (Loop === "Once") {
    U = Math.max(0, Math.min(1, RawPhase));
  } else if (Loop === "Loop" || Pattern === "CircularOrbit" || Pattern === "Figure8") {
    U = ((RawPhase % 1) + 1) % 1;
  } else {
    const Cyc = ((RawPhase % 2) + 2) % 2;
    U = Cyc <= 1 ? Cyc : 2 - Cyc;
  }

  if ((Pattern === "SplineCurve" || Pattern === "LinearAB") && Array.isArray(Force.MotionWaypoints) && Force.MotionWaypoints.length >= 2) {
    return EvalCatmullRom3(Force.MotionWaypoints, U);
  }
  const Amp = Force.MotionAmplitude || 0;
  if (Amp <= 1e-4) return [Cx, Cy, Cz];
  const PhaseRad = TimeSec * TWO_PI * (Force.MotionSpeedHz || 0.22);
  if (Pattern === "LinearAB") return [Cx + Math.sin(PhaseRad) * Amp, Cy, Cz];
  if (Pattern === "CircularOrbit") return [Cx + Math.cos(PhaseRad) * Amp, Cy + Math.sin(PhaseRad) * Amp, Cz];
  if (Pattern === "Figure8") return [Cx + Math.sin(PhaseRad) * Amp, Cy + Math.sin(PhaseRad * 2.0) * Amp * 0.55, Cz];
  return [Cx, Cy, Cz];
}

//------------------------------------------------------------------------------------------------------------------------
//                                               PER-LAW FORCE EVALUATION
//------------------------------------------------------------------------------------------------------------------------

// Evaluate one generator's force contribution (Newtons per unit mass, i.e. acceleration) at a world point.
function EvaluateForceSourceAt(ForceConfig, BaseCenter, RotationDeg, Px, Py, Pz, TimeSec = 0) {
  const F = ForceConfig;
  if (!F.Enabled || F.Muted) return [0, 0, 0];

  const Center = EvaluateMotionCenter(BaseCenter || [0, 0, 1.4], F, TimeSec);
  const Dx = Px - Center[0];
  const Dy = Py - Center[1];
  const Dz = Pz - Center[2];

  const NormU = EvaluateReachNormalizedDistance(Dx, Dy, Dz, F.Radius, F.Reach);
  if (NormU >= 1.0 && F.Reach !== "Infinite") return [0, 0, 0];

  let Gate = 1.0;
  if (F.Duration === "Modulated") {
    Gate = 0.62 + 0.38 * Math.sin(TimeSec * TWO_PI * F.Frequency);
  } else if (F.Duration === "Impulse") {
    const Cycle = (TimeSec * F.Frequency) % 1.0;
    const T = Cycle < 0 ? Cycle + 1.0 : Cycle;
    Gate = Math.exp(-T * 4.5) * Math.sin(Math.min(1.0, T * 6.0) * Math.PI * 0.5);
  }

  const Env = EvaluateRadialEnvelope(NormU, F.Attenuation, F.FalloffExponent) * Gate;
  if (Math.abs(Env) <= 1e-6) return [0, 0, 0];

  const RotYaw = (((RotationDeg?.[2] || 0) + F.YawDeg) * DEG_TO_RAD);
  const RotPitch = (((RotationDeg?.[1] || 0) + F.PitchDeg) * DEG_TO_RAD);
  const DirX = Math.cos(RotPitch) * Math.cos(RotYaw);
  const DirY = Math.cos(RotPitch) * Math.sin(RotYaw);
  const DirZ = Math.sin(RotPitch);

  const R3 = Math.hypot(Dx, Dy, Dz);
  const SafeR3 = Math.max(F.CoreRadius, R3, 0.08);
  const Rx = R3 > 1e-5 ? Dx / R3 : 1.0;
  const Ry = R3 > 1e-5 ? Dy / R3 : 0.0;
  const Rz = R3 > 1e-5 ? Dz / R3 : 0.0;

  const Rxy = Math.hypot(Dx, Dy);
  const Tx = Rxy > 1e-5 ? -Dy / Rxy : 0.0;
  const Ty = Rxy > 1e-5 ? Dx / Rxy : 1.0;

  const S = F.Strength * Env;

  switch (F.Model) {
    case "LinearWind": {
      const Shear = 1.0 + 0.14 * Math.max(-1.0, Dz / Math.max(0.5, F.Radius));
      return [DirX * S * Shear, DirY * S * Shear, (DirZ + 0.05 * F.SwirlRatio) * S];
    }
    case "Buoyancy":
      return [-Dx * 0.12 * S, -Dy * 0.12 * S, S];
    case "RadialBurst":
    case "Repulsor":
      return [Rx * S, Ry * S, Rz * S];
    case "Attractor": {
      const InnerGain = Math.min(1.8, (F.Radius * 0.55) / SafeR3);
      return [
        (-Rx * InnerGain + Tx * F.SwirlRatio * 0.45) * S,
        (-Ry * InnerGain + Ty * F.SwirlRatio * 0.45) * S,
        -Rz * InnerGain * S,
      ];
    }
    case "AxisAttractor": {
      const RadIn = Rxy > 1e-5 ? -1.0 / Rxy : 0.0;
      return [
        (Dx * RadIn * 0.75 + Tx * F.SwirlRatio) * S,
        (Dy * RadIn * 0.75 + Ty * F.SwirlRatio) * S,
        0.18 * S,
      ];
    }
    case "Containment": {
      if (R3 <= F.CoreRadius) return [Tx * S * 0.12 * F.SwirlRatio, Ty * S * 0.12 * F.SwirlRatio, 0];
      const Wall = Math.min(1.5, (R3 - F.CoreRadius) / Math.max(0.15, F.Radius - F.CoreRadius));
      return [-Rx * S * Wall, -Ry * S * Wall, -Rz * S * Wall];
    }
    case "RotationalColumn": {
      const Rankine =
        Rxy < F.CoreRadius
          ? Rxy / Math.max(0.08, F.CoreRadius)
          : Math.max(0.08, F.CoreRadius) / Math.max(F.CoreRadius, Rxy);
      const Inflow = -0.22 * (Rxy > 1e-5 ? 1.0 : 0.0);
      return [
        (Tx * Rankine * F.SwirlRatio + (Rxy > 1e-5 ? (Dx / Rxy) * Inflow : 0)) * S,
        (Ty * Rankine * F.SwirlRatio + (Rxy > 1e-5 ? (Dy / Rxy) * Inflow : 0)) * S,
        0.28 * Rankine * S,
      ];
    }
    case "Torque":
      return [Tx * S * F.SwirlRatio, Ty * S * F.SwirlRatio, 0.0];
    case "DragBaffle":
      return [-DirX * S * 0.55, -DirY * S * 0.55, -Rz * S * 0.35];
    case "CurlNoise": {
      const C = SampleCurlNoise3D(Dx, Dy, Dz, F.TurbulenceScale, F.Octaves, TimeSec);
      return [C[0] * S * 1.4, C[1] * S * 1.4, C[2] * S * 1.4];
    }
    case "AtmosphericGust": {
      const Wave = 0.65 + 0.35 * Math.sin((Dx * DirX + Dy * DirY) * 2.2 - TimeSec * TWO_PI * F.Frequency);
      const C = SampleCurlNoise3D(Dx, Dy, Dz, F.TurbulenceScale, F.Octaves, TimeSec);
      return [
        (DirX * Wave + C[0] * 0.42) * S,
        (DirY * Wave + C[1] * 0.42) * S,
        (DirZ * Wave + C[2] * 0.35) * S,
      ];
    }
    case "VorticityConfinement": {
      const C = SampleCurlNoise3D(Dx, Dy, Dz, F.TurbulenceScale, F.Octaves, TimeSec);
      return [(Tx * 0.65 + C[0]) * S, (Ty * 0.65 + C[1]) * S, C[2] * S];
    }
    case "BrownianNoise": {
      const J = PotentialFbmVec3(Dx / F.TurbulenceScale + TimeSec * 1.7, Dy / F.TurbulenceScale, Dz / F.TurbulenceScale - TimeSec * 1.3, F.Octaves);
      return [J[0] * S, J[1] * S, J[2] * S];
    }
    case "LorentzField":
      return [
        (DirX * 0.35 + Tx * F.SwirlRatio) * S,
        (DirY * 0.35 + Ty * F.SwirlRatio) * S,
        0.42 * S,
      ];
    case "MagneticDipole": {
      const MdotR = Rz;
      const Bx = 3.0 * MdotR * Rx;
      const By = 3.0 * MdotR * Ry;
      const Bz = 3.0 * MdotR * Rz - 1.0;
      const BNorm = Math.hypot(Bx, By, Bz) || 1.0;
      return [(Bx / BNorm) * S, (By / BNorm) * S, (Bz / BNorm) * S];
    }
    case "SlipstreamJet": {
      const Proj = Dx * DirX + Dy * DirY + Dz * DirZ;
      const LatX = Dx - Proj * DirX;
      const LatY = Dy - Proj * DirY;
      const LatZ = Dz - Proj * DirZ;
      return [
        (DirX - LatX * 0.32) * S,
        (DirY - LatY * 0.32) * S,
        (DirZ - LatZ * 0.32) * S,
      ];
    }
    case "SplineForce": {
      const CurveY = 0.55 * Math.sin((Dx / Math.max(0.5, F.Radius)) * Math.PI);
      const Slope = 0.55 * (Math.PI / Math.max(0.5, F.Radius)) * Math.cos((Dx / Math.max(0.5, F.Radius)) * Math.PI);
      const NormS = Math.hypot(1.0, Slope);
      return [
        (1.0 / NormS) * S,
        ((Slope / NormS) - (Dy - CurveY) * 0.45) * S,
        -Dz * 0.35 * S,
      ];
    }
    case "BoundaryAdhesion": {
      const HeightAboveFloor = Math.max(0.0, Pz);
      const Skin = Math.exp(-HeightAboveFloor / Math.max(0.25, F.CoreRadius * 1.4));
      return [DirX * S * (0.45 + 0.75 * Skin), DirY * S * (0.45 + 0.75 * Skin), -0.45 * S * Skin];
    }
    default:
      return [DirX * S, DirY * S, DirZ * S];
  }
}

// Sum every enabled generator contribution at a world point.
function EvaluateCompositeForceAt(Generators, Px, Py, Pz, TimeSec = 0) {
  let Fx = 0, Fy = 0, Fz = 0;
  for (let G = 0; G < Generators.length; ++G) {
    const Gen = Generators[G];
    if (!Gen.Force.Enabled || Gen.Force.Muted) continue;
    const C = EvaluateForceSourceAt(Gen.Force, Gen.Position, Gen.Rotation, Px, Py, Pz, TimeSec);
    Fx += C[0];
    Fy += C[1];
    Fz += C[2];
  }
  return [Fx, Fy, Fz];
}

//------------------------------------------------------------------------------------------------------------------------
//                                   FINITE-DIFFERENCE FIELD QUANTITIES (SLICE PLANE)
//------------------------------------------------------------------------------------------------------------------------

// Evaluate a scalar quantity of the composite field on a slice plane.
// PlaneMode: "Top XY" (Z fixed), "Front XZ" (Y fixed), "Side YZ" (X fixed).
// Quantities: Speed |F|, Acceleration |F| (force per unit mass), Curl |curl F|,
// Divergence (signed div F), Pressure (stagnation -|F|^2 / 2).
function EvaluateSliceQuantityAt(Generators, Quantity, Px, Py, Pz, TimeSec, PlaneMode, StepH = 0.09) {
  const F0 = EvaluateCompositeForceAt(Generators, Px, Py, Pz, TimeSec);
  if (Quantity === "Speed" || Quantity === "Acceleration") return Math.hypot(F0[0], F0[1], F0[2]);
  if (Quantity === "Pressure") return -0.5 * (F0[0] * F0[0] + F0[1] * F0[1] + F0[2] * F0[2]);

  const Top = PlaneMode === "Top XY";
  const Front = PlaneMode === "Front XZ";

  if (Quantity === "Divergence") {
    let Div = 0;
    if (Top) {
      const FxP = EvaluateCompositeForceAt(Generators, Px + StepH, Py, Pz, TimeSec);
      const FxM = EvaluateCompositeForceAt(Generators, Px - StepH, Py, Pz, TimeSec);
      const FyP = EvaluateCompositeForceAt(Generators, Px, Py + StepH, Pz, TimeSec);
      const FyM = EvaluateCompositeForceAt(Generators, Px, Py - StepH, Pz, TimeSec);
      const FzP = EvaluateCompositeForceAt(Generators, Px, Py, Pz + StepH, TimeSec);
      const FzM = EvaluateCompositeForceAt(Generators, Px, Py, Pz - StepH, TimeSec);
      Div = (FxP[0] - FxM[0] + FyP[1] - FyM[1] + FzP[2] - FzM[2]) / (2 * StepH);
    } else if (Front) {
      const FxP = EvaluateCompositeForceAt(Generators, Px + StepH, Py, Pz, TimeSec);
      const FxM = EvaluateCompositeForceAt(Generators, Px - StepH, Py, Pz, TimeSec);
      const FzP = EvaluateCompositeForceAt(Generators, Px, Py, Pz + StepH, TimeSec);
      const FzM = EvaluateCompositeForceAt(Generators, Px, Py, Pz - StepH, TimeSec);
      const FyP = EvaluateCompositeForceAt(Generators, Px, Py + StepH, Pz, TimeSec);
      const FyM = EvaluateCompositeForceAt(Generators, Px, Py - StepH, Pz, TimeSec);
      Div = (FxP[0] - FxM[0] + FzP[2] - FzM[2] + FyP[1] - FyM[1]) / (2 * StepH);
    } else {
      const FyP = EvaluateCompositeForceAt(Generators, Px, Py + StepH, Pz, TimeSec);
      const FyM = EvaluateCompositeForceAt(Generators, Px, Py - StepH, Pz, TimeSec);
      const FzP = EvaluateCompositeForceAt(Generators, Px, Py, Pz + StepH, TimeSec);
      const FzM = EvaluateCompositeForceAt(Generators, Px, Py, Pz - StepH, TimeSec);
      const FxP = EvaluateCompositeForceAt(Generators, Px + StepH, Py, Pz, TimeSec);
      const FxM = EvaluateCompositeForceAt(Generators, Px - StepH, Py, Pz, TimeSec);
      Div = (FyP[1] - FyM[1] + FzP[2] - FzM[2] + FxP[0] - FxM[0]) / (2 * StepH);
    }
    return Div;
  }

  // Curl magnitude via central differences of the full composite.
  const Eps = StepH;
  const FxP = EvaluateCompositeForceAt(Generators, Px + Eps, Py, Pz, TimeSec);
  const FxM = EvaluateCompositeForceAt(Generators, Px - Eps, Py, Pz, TimeSec);
  const FyP = EvaluateCompositeForceAt(Generators, Px, Py + Eps, Pz, TimeSec);
  const FyM = EvaluateCompositeForceAt(Generators, Px, Py - Eps, Pz, TimeSec);
  const FzP = EvaluateCompositeForceAt(Generators, Px, Py, Pz + Eps, TimeSec);
  const FzM = EvaluateCompositeForceAt(Generators, Px, Py, Pz - Eps, TimeSec);
  const Inv2E = 0.5 / Eps;
  const Cx = (FyP[2] - FyM[2] - (FzP[1] - FzM[1])) * Inv2E;
  const Cy = (FzP[0] - FzM[0] - (FxP[2] - FxM[2])) * Inv2E;
  const Cz = (FxP[1] - FxM[1] - (FyP[0] - FyM[0])) * Inv2E;
  return Math.hypot(Cx, Cy, Cz);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    COLOUR RAMPS
//------------------------------------------------------------------------------------------------------------------------

function HexToRgb01(Hex) {
  const N = parseInt(Hex.slice(1), 16);
  return [((N >> 16) & 255) / 255, ((N >> 8) & 255) / 255, (N & 255) / 255];
}

const RampStopTable = Object.freeze({
  Engineering: ["#102a56", "#1c7ed6", "#22b8cf", "#82c91e", "#fab005", "#f03e3e"],
  Turbo: ["#30123b", "#4685fa", "#1ae4b6", "#faba39", "#7a0403"],
  Plasma: ["#0d0887", "#7e03a8", "#cc4778", "#f89540", "#f0f921"],
  Viridis: ["#440154", "#31688e", "#35b779", "#fde725"],
  Diverging: ["#2b6cb0", "#63b3ed", "#1e2430", "#fc8181", "#e53e3e"],
});

function MixRgb(A, B, T) {
  return [A[0] + (B[0] - A[0]) * T, A[1] + (B[1] - A[1]) * T, A[2] + (B[2] - A[2]) * T];
}

// Map share01 in [0,1] to an RGB triplet under the chosen colour ramp.
function SampleColourRamp(RampMode = "Engineering", Share01 = 0.0, DomainTint = "#4AA8FF") {
  const T = Math.max(0.0, Math.min(1.0, Number(Share01) || 0));
  if (RampMode === "DomainTint") {
    const Stops = [HexToRgb01("#132b47"), HexToRgb01(DomainTint || "#4AA8FF"), HexToRgb01("#cfe8ff")];
    const Seg = T < 0.55 ? [Stops[0], Stops[1], T / 0.55] : [Stops[1], Stops[2], (T - 0.55) / 0.45];
    return MixRgb(Seg[0], Seg[1], Math.max(0, Math.min(1, Seg[2])));
  }
  const Stops = (RampStopTable[RampMode] || RampStopTable.Engineering).map(HexToRgb01);
  const SegT = T * (Stops.length - 1);
  const Seg = Math.min(Stops.length - 2, Math.floor(SegT));
  return MixRgb(Stops[Seg], Stops[Seg + 1], SegT - Seg);
}

function RampCss(RampMode, Rgb) {
  const R = Math.round(Math.max(0, Math.min(1, Rgb[0])) * 255);
  const G = Math.round(Math.max(0, Math.min(1, Rgb[1])) * 255);
  const B = Math.round(Math.max(0, Math.min(1, Rgb[2])) * 255);
  return `rgb(${R},${G},${B})`;
}

//------------------------------------------------------------------------------------------------------------------------
//                                             BEAUFORT & COMPASS READOUT
//------------------------------------------------------------------------------------------------------------------------

function ClassifyBeaufortScale(SpeedMs) {
  const Ladder = [
    [0.3, "0 \u00B7 Calm"], [1.6, "1 \u00B7 Light Air"], [3.4, "2 \u00B7 Light Breeze"], [5.5, "3 \u00B7 Gentle Breeze"],
    [8.0, "4 \u00B7 Moderate Breeze"], [10.8, "5 \u00B7 Fresh Breeze"], [13.9, "6 \u00B7 Strong Breeze"],
    [17.2, "7 \u00B7 Near Gale"], [20.8, "8 \u00B7 Gale"], [24.5, "9 \u00B7 Strong Gale"],
    [28.5, "10 \u00B7 Storm"], [32.7, "11 \u00B7 Violent Storm"],
  ];
  for (const [Limit, Label] of Ladder) {
    if (SpeedMs < Limit) return Label;
  }
  return "12 \u00B7 Hurricane";
}

function CompassFromBearingDeg(Deg) {
  const Sectors = ["N", "NNE", "NE", "ENE", "E", "ESE", "SE", "SSE", "S", "SSW", "SW", "WSW", "W", "WNW", "NW", "NNW"];
  const Norm = ((Deg % 360) + 360) % 360;
  return Sectors[Math.round(Norm / 22.5) % 16];
}

//------------------------------------------------------------------------------------------------------------------------
//                                                BIDIRECTIONAL STREAMLINES
//------------------------------------------------------------------------------------------------------------------------

// Trace an in-plane streamline through the composite field on the current slice plane.
// Returns an array of world points (in-plane coordinates only; the plane-normal component is projected out).
// PlaneMode as in EvaluateSliceQuantityAt; PlaneA/PlaneB are the two in-plane world axes; PlaneNormalIndex is the fixed axis.
function TraceSliceStreamline(Generators, SeedPoint, PlaneMode, TimeSec, StepsPerDir = 120, StepLen = 0.035, BoundsHalf = 3.6) {
  const NormalIndex = PlaneMode === "Top XY" ? 2 : PlaneMode === "Front XZ" ? 1 : 0;
  const AxisA = PlaneMode === "Top XY" ? 0 : PlaneMode === "Side YZ" ? 1 : 0;
  const AxisB = PlaneMode === "Top XY" ? 1 : 2;

  const PlanePoint = [SeedPoint[0], SeedPoint[1], SeedPoint[2]];

  const SampleInPlane = (Pt) => {
    const F = EvaluateCompositeForceAt(Generators, Pt[0], Pt[1], Pt[2], TimeSec);
    const Out = [0, 0];
    Out[0] = F[AxisA];
    Out[1] = F[AxisB];
    return Out;
  };

  const March = (DirSign) => {
    const Points = [];
    const Pt = [PlanePoint[0], PlanePoint[1], PlanePoint[2]];
    for (let Step = 0; Step < StepsPerDir; ++Step) {
      const V0 = SampleInPlane(Pt);
      const Speed0 = Math.hypot(V0[0], V0[1]);
      if (Speed0 < 1e-4) break;
      const Mid = [Pt[0], Pt[1], Pt[2]];
      Mid[AxisA] += (V0[0] / Speed0) * StepLen * 0.5 * DirSign;
      Mid[AxisB] += (V0[1] / Speed0) * StepLen * 0.5 * DirSign;
      const V1 = SampleInPlane(Mid);
      const Speed1 = Math.hypot(V1[0], V1[1]);
      if (Speed1 < 1e-4) break;
      Pt[AxisA] += (V1[0] / Speed1) * StepLen * DirSign;
      Pt[AxisB] += (V1[1] / Speed1) * StepLen * DirSign;
      const FixedCoord = Pt[NormalIndex];
      if (Math.abs(Pt[0]) > BoundsHalf || Math.abs(Pt[1]) > BoundsHalf || Math.abs(Pt[2]) > BoundsHalf) break;
      Points.push([Pt[0], Pt[1], Pt[2], Speed1, FixedCoord]);
    }
    return Points;
  };

  const Forward = March(1.0);
  const Backward = March(-1.0).reverse();
  return Backward.concat([[PlanePoint[0], PlanePoint[1], PlanePoint[2], 0, PlanePoint[NormalIndex]]], Forward);
}
