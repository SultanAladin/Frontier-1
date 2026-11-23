// Thin-lens depth-of-field approximation for a 36 × 24 mm full-frame sensor.
// Distances are meters externally, millimeters internally; CoC = 0.03 mm.
export function cameraOptics(focal, aperture, focus) {
  const sensorWidth = 36, sensorHeight = 24, coc = 0.03;
  const s = focus * 1000;
  const hyperfocalMm = focal * focal / (aperture * coc) + focal;
  const near = hyperfocalMm * s / (hyperfocalMm + s - focal) / 1000;
  const farDenominator = hyperfocalMm - (s - focal);
  const far = farDenominator <= 0 ? Infinity : hyperfocalMm * s / farDenominator / 1000;
  return {
    horizontalFov: 2 * Math.atan(sensorWidth / (2 * focal)) * 180 / Math.PI,
    verticalFov: 2 * Math.atan(sensorHeight / (2 * focal)) * 180 / Math.PI,
    pupil: focal / aperture,
    near, far, depth: far - near,
    hyperfocal: hyperfocalMm / 1000,
    frameWidth: focus * sensorWidth / focal,
    frameHeight: focus * sensorHeight / focal,
  };
}
export const formatDistance = value => Number.isFinite(value) ? `${value.toFixed(1)} m` : '∞';
