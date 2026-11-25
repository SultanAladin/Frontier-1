/**
 * 3D Structural Geology Tectonic Field:
 * - Strike & Dip orientation frames
 * - Conjugate Andersonian joint families (J0 bedding/foliation, J1 cross-joint, J2/J3 shear joints)
 * - Discrete geological slip faults with hanging-wall throw & fault-damage crush zones
 */

import { DEG2RAD, buildStrikeDipFrame, rotateAroundAxis, vnorm, smoothstep } from '../core/vec3.js';
import { scalarField3D } from '../core/spatial-noise.js';

export function createTectonicField(params, rng) {
  const scale = params.scale;
  const strikeRad = (params.strikeAngle ?? 0) * DEG2RAD + rng.signed() * 0.08;
  const dipRad = (params.dipAngle ?? 15) * DEG2RAD + rng.signed() * 0.06;

  const baseFrame = buildStrikeDipFrame(strikeRad, dipRad);

  // Conjugate shear joint normals (intersecting around ~58 deg Andersonian shear angle)
  const shearAngle = (54 + rng.range(-6, 8)) * DEG2RAD;
  const jointJ1 = baseFrame.dipVec;
  const jointJ2 = vnorm(rotateAroundAxis(baseFrame.dipVec, baseFrame.normal, shearAngle));
  const jointJ3 = vnorm(rotateAroundAxis(baseFrame.dipVec, baseFrame.normal, -shearAngle));

  // Build 1-2 discrete tectonic fault planes
  const faultIntensity = params.faultIntensity ?? 0.5;
  const numFaults = faultIntensity > 0.12 ? (faultIntensity > 0.52 ? 2 : 1) : 0;
  const faults = [];

  for (let i = 0; i < numFaults; i++) {
    const fStrike = strikeRad + (i === 0 ? rng.range(0.65, 1.35) : rng.range(-1.35, -0.65));
    const fDip = rng.range(56, 78) * DEG2RAD;
    const fFrame = buildStrikeDipFrame(fStrike, fDip);

    const origin = [
      rng.range(-0.26, 0.26) * scale,
      rng.range(0.25, 0.55) * scale * 0.5,
      rng.range(-0.15, 0.15) * scale,
    ];

    // Normal-fault down-dip slip + minor strike-slip component
    const throwMag = scale * (0.04 + 0.10 * faultIntensity) * rng.range(0.75, 1.25) * (rng.chance(0.5) ? 1 : -1);
    const slip = [
      fFrame.dipVec[0] * throwMag * 0.45 + fFrame.strikeVec[0] * throwMag * 0.2,
      -Math.abs(throwMag),
      fFrame.dipVec[2] * throwMag * 0.45 + fFrame.strikeVec[2] * throwMag * 0.2,
    ];

    faults.push({
      normal: fFrame.normal,
      origin,
      slip,
      zoneWidth: scale * rng.range(0.045, 0.085) * (0.6 + 0.7 * faultIntensity),
      salt: rng.int(100, 99999),
    });
  }

  const foldFreq = 1 / (scale * 0.85);
  const foldSalt = rng.int(1, 99999);

  function signedFaultDist(fault, x, y, z) {
    // Gentle non-planar fault surface undulation
    const warp = scalarField3D(x * foldFreq * 1.4, y * foldFreq * 1.4, z * foldFreq * 1.4, fault.salt) * scale * 0.035;
    return (
      fault.normal[0] * (x - fault.origin[0]) +
      fault.normal[1] * (y - fault.origin[1]) +
      fault.normal[2] * (z - fault.origin[2]) +
      warp
    );
  }

  function evalTectonicShift(x, y, z) {
    let dx = 0, dy = 0, dz = 0;
    // Gentle regional fold arch
    const fold = scalarField3D(x * foldFreq, y * foldFreq * 0.5, z * foldFreq, foldSalt) * scale * 0.04;
    dy += fold;

    for (let i = 0; i < faults.length; i++) {
      const f = faults[i];
      const sd = signedFaultDist(f, x, y, z);
      const t = smoothstep(-f.zoneWidth * 0.5, f.zoneWidth * 0.5, sd);
      dx += f.slip[0] * t;
      dy += f.slip[1] * t;
      dz += f.slip[2] * t;
    }
    return [dx, dy, dz];
  }

  function evalFaultDamage(x, y, z) {
    let dmg = 0;
    for (let i = 0; i < faults.length; i++) {
      const f = faults[i];
      const sd = Math.abs(signedFaultDist(f, x, y, z));
      if (sd < f.zoneWidth * 1.6) {
        const d = 1 - sd / (f.zoneWidth * 1.6);
        if (d > dmg) dmg = d;
      }
    }
    return dmg;
  }

  function localFrameAt(x, y, z) {
    const tiltJitter = scalarField3D(x * foldFreq * 1.2, y * foldFreq, z * foldFreq * 1.2, foldSalt + 77) * 0.10;
    return buildStrikeDipFrame(strikeRad + tiltJitter * 0.5, dipRad + tiltJitter);
  }

  return {
    strikeRad,
    dipRad,
    baseFrame,
    jointNormals: [baseFrame.normal, jointJ1, jointJ2, jointJ3],
    faults,
    evalTectonicShift,
    evalFaultDamage,
    localFrameAt,
  };
}
