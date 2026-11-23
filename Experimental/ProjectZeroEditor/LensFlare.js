//============================================================================================================================================
//                                                              LENSFLARE.JS
//============================================================================================================================================
// 🧩 The Lens Flare object drawn in screen space from a baked atlas, for the 3D viewport and the Flare composite card.
//   • The atlas (PaintFlareAtlas) holds eight 256 px sprites on black: the glow, the starburst (one spike per aperture blade, two
//     per blade when the count is odd, as diffraction gives), the halo ring (split red, green and blue by Chromatic), the streak, and
//     the ghost shape (round, hexagon or octagon) in four tints. Everything is multiplied by the sun's colour after the air.
//   • PaintFlare adds the sprites onto a 2D canvas: glow and starburst on the sun, ray pairs and the anamorphic streak through it, the
//     halo around it, and ghosts on the line from the sun through the centre of the frame (and past it), as reflections between lens
//     elements fall. The Type weights the layers; the Anamorphic, Streaks and Starburst switches turn theirs off.
// A screen-space illustration of the authored settings, not a lens simulation and not the native composite.

export const FlareCell = 256;
export const FlareCells = ["Glow", "Starburst", "Halo", "Streak", "Ghost amber", "Ghost green", "Ghost blue", "Ghost violet"];
const GhostTints = [
  [1, 0.72, 0.38],
  [0.55, 1, 0.62],
  [0.45, 0.62, 1],
  [0.86, 0.52, 1],
];
// How much each Type leans on each layer (Cinematic, Anamorphic, Starburst, Halo, Custom layers).
const TypeWeights = [
  { Glow: 1, Burst: 1, Rays: 1, Streak: 0.6, Halo: 1, Ghosts: 1 },
  { Glow: 0.9, Burst: 0.6, Rays: 0.4, Streak: 1.7, Halo: 0.5, Ghosts: 0.8 },
  { Glow: 1, Burst: 1.5, Rays: 1.4, Streak: 0.3, Halo: 0.4, Ghosts: 0.6 },
  { Glow: 0.9, Burst: 0.7, Rays: 0.3, Streak: 0.2, Halo: 1.7, Ghosts: 0.6 },
  { Glow: 1, Burst: 1, Rays: 1, Streak: 1, Halo: 1, Ghosts: 1 },
];

// The Lens Flare object's settings, read through `Read(Name)` (with the sheet's defaults behind it).
export function FlareSettings(Read) {
  const N = (Name, Fallback) => (Number.isFinite(+Read(Name)) && Read(Name) !== null && Read(Name) !== "" ? +Read(Name) : Fallback);
  const B = (Name, Fallback) => (Read(Name) == null ? Fallback : !!Read(Name));
  return {
    Enabled: B("Enabled", true),
    Type: Math.max(0, Math.min(4, Math.round(N("Type", 0)))),
    Intensity: N("Intensity", 1),
    Ghosts: Math.max(0, Math.min(24, Math.round(N("Ghosts", 6)))),
    HaloRadius: N("Halo Radius", 0.28),
    Chromatic: Math.max(0, Math.min(1, N("Chromatic", 0.6))),
    Blades: Math.max(3, Math.min(12, Math.round(N("Aperture Blades", 6)))),
    StreakGain: N("Streak gain", 1),
    Anamorphic: B("Anamorphic", true),
    Streaks: B("Streaks", true),
    Starburst: B("Starburst", true),
    Spread: N("Spread", 1),
    Rotation: N("Rotation", 0),
    RayPairs: Math.max(2, Math.min(12, Math.round(N("Ray pairs", 6)))),
    GhostGain: N("Ghost brightness", 1),
    GhostSpacing: N("Ghost spacing", 1),
    HaloGain: N("Halo brightness", 1),
    HaloWidth: N("Halo width", 0.035),
    GhostShape: Math.max(0, Math.min(2, Math.round(N("Ghost shape", 0)))),
  };
}

const Rgb = (Colour, Alpha = 1) => `rgba(${Colour.map((C) => Math.round(Math.max(0, Math.min(1, C)) * 255)).join(",")},${Alpha})`;
const Times = (A, B) => A.map((C, Index) => C * B[Index]);
const Mix = (A, B, T) => A.map((C, Index) => C + (B[Index] - C) * T);

// Draws the atlas (4 × 2 cells) onto `Canvas`. Tint is the sun's colour (brightest channel 1).
export function PaintFlareAtlas(Canvas, Settings, Tint = [1, 1, 1]) {
  const S = FlareCell, Half = S / 2;
  Canvas.width = S * 4;
  Canvas.height = S * 2;
  const Brush = Canvas.getContext("2d");
  Brush.setTransform(1, 0, 0, 1, 0, 0);
  Brush.globalCompositeOperation = "source-over";
  Brush.fillStyle = "#000";
  Brush.fillRect(0, 0, Canvas.width, Canvas.height);
  Brush.globalCompositeOperation = "lighter";
  const Warm = Times(Tint, [1, 0.93, 0.82]);
  const At = (Index) => [(Index % 4) * S + Half, Math.floor(Index / 4) * S + Half];
  const Clip = (Index, Draw) => {
    const [X, Y] = At(Index);
    Brush.save();
    Brush.beginPath();
    Brush.rect(X - Half, Y - Half, S, S);
    Brush.clip();
    Brush.translate(X, Y);
    Draw();
    Brush.restore();
  };

  // Glow: a hot centre and a soft glow.
  Clip(0, () => {
    const Glow = Brush.createRadialGradient(0, 0, 0, 0, 0, Half);
    Glow.addColorStop(0, Rgb([1, 1, 1]));
    Glow.addColorStop(0.06, Rgb(Mix([1, 1, 1], Warm, 0.4)));
    Glow.addColorStop(0.2, Rgb(Warm, 0.45));
    Glow.addColorStop(0.55, Rgb(Warm, 0.1));
    Glow.addColorStop(1, Rgb(Warm, 0));
    Brush.fillStyle = Glow;
    Brush.fillRect(-Half, -Half, S, S);
  });

  // Starburst: thin tapered spikes, one per blade (two per blade when the count is odd).
  Clip(1, () => {
    const Spikes = Settings.Blades % 2 ? Settings.Blades * 2 : Settings.Blades;
    for (let Spike = 0; Spike < Spikes; Spike++) {
      Brush.save();
      Brush.rotate((Spike / Spikes) * Math.PI * 2);
      const Fade = Brush.createLinearGradient(0, 0, Half, 0);
      Fade.addColorStop(0, Rgb([1, 1, 1], 0.95));
      Fade.addColorStop(0.25, Rgb(Warm, 0.5));
      Fade.addColorStop(1, Rgb(Warm, 0));
      Brush.fillStyle = Fade;
      Brush.beginPath();
      Brush.moveTo(0, -3.2);
      Brush.lineTo(Half, -0.3);
      Brush.lineTo(Half, 0.3);
      Brush.lineTo(0, 3.2);
      Brush.closePath();
      Brush.fill();
      Brush.restore();
    }
  });

  // Halo: a ring, its red, green and blue parts spread apart by Chromatic.
  Clip(2, () => {
    const Radius = Half * 0.8, Width = Math.max(1.5, Settings.HaloWidth * S * 1.6);
    const Spread = Settings.Chromatic * Width * 0.9;
    [[1, 0.25, 0.2], [0.3, 1, 0.35], [0.25, 0.4, 1]].forEach((Channel, Index) => {
      const R = Radius + (Index - 1) * Spread;
      const Ring = Brush.createRadialGradient(0, 0, Math.max(0, R - Width), 0, 0, R + Width);
      const Colour = Times(Mix([1, 1, 1], Channel, 0.35 + 0.65 * Settings.Chromatic), Tint);
      Ring.addColorStop(0, Rgb(Colour, 0));
      Ring.addColorStop(0.5, Rgb(Colour, 0.55));
      Ring.addColorStop(1, Rgb(Colour, 0));
      Brush.fillStyle = Ring;
      Brush.fillRect(-Half, -Half, S, S);
    });
  });

  // Streak: a long horizontal smear, cooler at its ends (drawn stretched, so it is round here).
  Clip(3, () => {
    Brush.scale(1, 0.12);
    const Smear = Brush.createRadialGradient(0, 0, 0, 0, 0, Half);
    Smear.addColorStop(0, Rgb([1, 1, 1], 0.95));
    Smear.addColorStop(0.15, Rgb(Mix(Tint, [0.6, 0.78, 1], 0.55), 0.7));
    Smear.addColorStop(0.6, Rgb([0.45, 0.62, 1], 0.22));
    Smear.addColorStop(1, Rgb([0.4, 0.55, 1], 0));
    Brush.fillStyle = Smear;
    Brush.fillRect(-Half, -Half / 0.12, S, S / 0.12);
  });

  // Ghosts: the aperture's shape, a faint body and a brighter rim with a coloured fringe.
  const Sides = [0, 6, 8][Settings.GhostShape];
  const Outline = (Radius) => {
    Brush.beginPath();
    if (!Sides) Brush.arc(0, 0, Radius, 0, Math.PI * 2);
    else
      for (let Corner = 0; Corner <= Sides; Corner++) {
        const Angle = (Corner / Sides) * Math.PI * 2 + Math.PI / Sides;
        Brush[Corner ? "lineTo" : "moveTo"](Math.cos(Angle) * Radius, Math.sin(Angle) * Radius);
      }
    Brush.closePath();
  };
  GhostTints.forEach((Hue, Index) => {
    Clip(4 + Index, () => {
      const Colour = Times(Mix([1, 1, 1], Hue, 0.25 + 0.75 * Settings.Chromatic), Tint);
      const Radius = Half * 0.86;
      const Body = Brush.createRadialGradient(0, 0, 0, 0, 0, Radius);
      Body.addColorStop(0, Rgb(Colour, 0.22));
      Body.addColorStop(0.75, Rgb(Colour, 0.32));
      Body.addColorStop(0.93, Rgb(Colour, 0.75));
      Body.addColorStop(1, Rgb(Colour, 0.1));
      Outline(Radius);
      Brush.fillStyle = Body;
      Brush.fill();
      // Fringe: the complementary edge just outside.
      Outline(Radius * 1.03);
      Brush.lineWidth = 3;
      Brush.strokeStyle = Rgb(Times(Mix([1, 1, 1], [Hue[2], Hue[0], Hue[1]], Settings.Chromatic), Tint), 0.35 * Settings.Chromatic);
      Brush.stroke();
    });
  });
  Brush.globalCompositeOperation = "source-over";
  return { Cells: FlareCells.length };
}

// A fixed sequence, so each ghost keeps its size and tint from frame to frame.
const Sequence = (Index, Salt) => {
  const X = Math.sin(Index * 12.9898 + Salt * 78.233) * 43758.5453;
  return X - Math.floor(X);
};

// Adds the flare onto `Brush` (a 2D context the size of the view). Pose: the sun's position in pixels (X, Y), the view's Width and
// Height, and Strength (0 hides it). Returns how many sprites were drawn.
export function PaintFlare(Brush, Atlas, Settings, Pose) {
  const { X, Y, Width, Height } = Pose;
  const Strength = Pose.Strength * Settings.Intensity;
  if (!Settings.Enabled || !(Strength > 0.002) || !Atlas) return 0;
  const W = TypeWeights[Settings.Type] || TypeWeights[0];
  const Short = Math.min(Width, Height), S = FlareCell;
  let Drawn = 0;
  const Sprite = (Cell, Cx, Cy, SizeX, SizeY, Angle, Alpha) => {
    if (!(Alpha > 0.002) || SizeX < 1 || SizeY < 1) return;
    Brush.save();
    Brush.globalAlpha = Math.min(1, Alpha);
    Brush.translate(Cx, Cy);
    if (Angle) Brush.rotate(Angle);
    Brush.drawImage(Atlas, (Cell % 4) * S, Math.floor(Cell / 4) * S, S, S, -SizeX / 2, -SizeY / 2, SizeX, SizeY);
    Brush.restore();
    Drawn++;
  };
  const Turn = (Settings.Rotation * Math.PI) / 180;
  Brush.save();
  Brush.globalCompositeOperation = "lighter";
  // Veiling glare, then the glow.
  Sprite(0, X, Y, Short * 1.5 * Settings.Spread, Short * 1.5 * Settings.Spread, 0, Strength * 0.18 * W.Glow);
  Sprite(0, X, Y, Short * 0.34 * Settings.Spread, Short * 0.34 * Settings.Spread, 0, Strength * W.Glow);
  if (Settings.Starburst) Sprite(1, X, Y, Short * 0.62 * Settings.Spread, Short * 0.62 * Settings.Spread, Turn, Strength * 0.9 * W.Burst);
  if (Settings.Streaks)
    for (let Pair = 0; Pair < Settings.RayPairs; Pair++)
      Sprite(3, X, Y, Short * 0.95 * Settings.Spread, Short * 0.035, Turn + 0.12 + (Pair / Settings.RayPairs) * Math.PI, Strength * 0.45 * Settings.StreakGain * W.Rays);
  if (Settings.Anamorphic) Sprite(3, X, Y, Width * 1.5 * Settings.Spread, Short * 0.07, 0, Strength * 0.85 * Settings.StreakGain * W.Streak);
  const Ring = Short * Settings.HaloRadius * 2.5 * Settings.Spread;
  Sprite(2, X, Y, Ring, Ring, 0, Strength * 0.32 * Settings.HaloGain * W.Halo);
  // Ghosts along the line from the sun through the centre, the far ones past it.
  const Cx = Width / 2, Cy = Height / 2;
  for (let Ghost = 0; Ghost < Settings.Ghosts; Ghost++) {
    const Along = 0.32 + (Settings.Ghosts > 1 ? Ghost / (Settings.Ghosts - 1) : 0.5) * 1.75 * Settings.GhostSpacing;
    const Px = X + (Cx - X) * Along, Py = Y + (Cy - Y) * Along;
    const Size = Short * (0.03 + 0.13 * Sequence(Ghost, 1)) * Settings.Spread;
    Sprite(4 + (Ghost % 4), Px, Py, Size, Size, Turn, Strength * Settings.GhostGain * W.Ghosts * (0.16 + 0.24 * Sequence(Ghost, 2)));
  }
  Brush.restore();
  return Drawn;
}
