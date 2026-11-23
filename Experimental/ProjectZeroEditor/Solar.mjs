// Browser translation of the solar branch in Engine/DisplayPresentation/CelestialSolver.cpp.
// No atmospheric refraction, identical to the native geometric horizon calculation.
export function Solar({
  Year = 2026,
  Month = 9,
  Day = 10,
  LocalHours = 12,
  UtcOffset = 2,
  Latitude = -26.19,
  Longitude = 28.32,
}) {
  const Radians = (Degrees) => (Degrees * Math.PI) / 180,
    Degrees = (Radians) => (Radians * 180) / Math.PI,
    Wrap = (Angle) => ((Angle % 360) + 360) % 360,
    Clamp = (Value) => Math.max(-1, Math.min(1, Value));
  let Y = Year,
    M = Month;
  if (M <= 2) {
    Y--;
    M += 12;
  }
  const A = Math.trunc(Y / 100),
    B = 2 - A + Math.trunc(A / 4),
    UtcHours = LocalHours - UtcOffset;
  const Jd =
      Math.floor(365.25 * (Y + 4716)) +
      Math.floor(30.6001 * (M + 1)) +
      Day +
      B -
      1524.5 +
      UtcHours / 24,
    T = (Jd - 2451545) / 36525;
  const MeanLongitude = Wrap(280.46646 + T * (36000.76983 + T * 0.0003032)),
    MeanAnomaly = 357.52911 + T * (35999.05029 - 0.0001537 * T),
    Eccentricity = 0.016708634 - T * (0.000042037 + 0.0000001267 * T);
  const Mr = Radians(MeanAnomaly),
    Centre =
      Math.sin(Mr) * (1.914602 - T * (0.004817 + 0.000014 * T)) +
      Math.sin(2 * Mr) * (0.019993 - 0.000101 * T) +
      Math.sin(3 * Mr) * 0.000289;
  const Omega = 125.04 - 1934.136 * T,
    ApparentLongitude =
      MeanLongitude + Centre - 0.00569 - 0.00478 * Math.sin(Radians(Omega));
  const MeanObliquity =
      23 +
      (26 + (21.448 - T * (46.815 + T * (0.00059 - T * 0.001813))) / 60) / 60,
    Obliquity = MeanObliquity + 0.00256 * Math.cos(Radians(Omega));
  const Declination = Degrees(
    Math.asin(
      Math.sin(Radians(Obliquity)) * Math.sin(Radians(ApparentLongitude)),
    ),
  );
  const Y2 = Math.tan(Radians(Obliquity / 2)) ** 2,
    L0r = Radians(MeanLongitude);
  const EquationOfTime =
    4 *
    Degrees(
      Y2 * Math.sin(2 * L0r) -
        2 * Eccentricity * Math.sin(Mr) +
        4 * Eccentricity * Y2 * Math.sin(Mr) * Math.cos(2 * L0r) -
        0.5 * Y2 * Y2 * Math.sin(4 * L0r) -
        1.25 * Eccentricity * Eccentricity * Math.sin(2 * Mr),
    );
  const TrueSolarMinutes =
      (UtcHours * 60 + EquationOfTime + 4 * Longitude) % 1440,
    HourAngle = TrueSolarMinutes / 4 - 180,
    LatR = Radians(Latitude),
    DecR = Radians(Declination),
    HaR = Radians(HourAngle);
  const Zenith = Math.acos(
      Clamp(
        Math.sin(LatR) * Math.sin(DecR) +
          Math.cos(LatR) * Math.cos(DecR) * Math.cos(HaR),
      ),
    ),
    Elevation = 90 - Degrees(Zenith),
    SinZenith = Math.sin(Zenith);
  let Azimuth = 0;
  if (Math.abs(SinZenith) > 1e-9) {
    Azimuth = Degrees(
      Math.acos(
        Clamp(
          (Math.sin(LatR) * Math.cos(Zenith) - Math.sin(DecR)) /
            (Math.cos(LatR) * SinZenith),
        ),
      ),
    );
    Azimuth = HourAngle > 0 ? Wrap(Azimuth + 180) : Wrap(540 - Azimuth);
  }
  return { Elevation, Azimuth, Declination, EquationOfTime };
}
