// Source studies are analytical authoring diagrams, not native rendering or measured photometry.
const Clamp = (Number, Minimum, Maximum) =>
  Math.max(Minimum, Math.min(Maximum, Number));
function Polar(Profile, Angle, Cone, Section = 0) {
  Angle *= 60 / Math.max(5, Cone);
  if (Math.abs(Angle) > Math.PI / 2) return 0;
  const Axis = Math.max(0, Math.cos(Angle));
  if (Profile === "Batwing") return Math.abs(Math.sin(Angle * 2)) ** 1.4 * Axis;
  if (Profile === "Wall wash")
    return Math.max(0, Math.cos(Angle - 0.55)) ** (Section ? 3 : 5);
  let Power =
    Profile === "Fog Lamp"
      ? 0.65
      : Profile === "Parking Lamp"
        ? 0.4
        : (Profile === "High Beam" ? 240 : 130) / 60;
  let Lobe = Axis ** Math.max(0.4, Power * (Section ? 0.65 : 1));
  if (Profile.includes("Low Beam"))
    Lobe *= Math.sin(Angle) > 0 ? (Profile.startsWith("SAE") ? 0.5 : 0.65) : 1;
  return Lobe;
}
export function ProjectLight(
  Brush,
  Width,
  Height,
  { Style, Read, Properties, Tint, Flux },
) {
  Brush.clearRect(0, 0, Width, Height);
  Brush.save();
  const Scale = Math.min(Width / 380, Height / 260);
  Brush.translate((Width - 380 * Scale) / 2, (Height - 260 * Scale) / 2);
  Brush.scale(Scale, Scale);
  Brush.lineWidth = 1;
  const Text = (
    Caption,
    Horizontal,
    Vertical,
    Align = "left",
    Colour = "#89918d",
  ) => {
    Brush.font = "10px sans-serif";
    Brush.fillStyle = Colour;
    Brush.textAlign = Align;
    Brush.fillText(Caption, Horizontal, Vertical);
  };
  const Path = (Points, Stroke = "#ffffff30", Fill = null, Dash = false) => {
    Brush.beginPath();
    Points.forEach(([Horizontal, Vertical], Index) =>
      Index
        ? Brush.lineTo(Horizontal, Vertical)
        : Brush.moveTo(Horizontal, Vertical),
    );
    if (Fill) {
      Brush.closePath();
      Brush.fillStyle = Fill;
      Brush.fill();
    }
    Brush.strokeStyle = Stroke;
    Brush.setLineDash(Dash ? [3, 5] : []);
    Brush.stroke();
    Brush.setLineDash([]);
  };
  const Circle = (
    Horizontal,
    Vertical,
    Radius,
    Stroke = "#ffffff20",
    Fill = null,
  ) => {
    Brush.beginPath();
    Brush.arc(Horizontal, Vertical, Radius, 0, Math.PI * 2);
    if (Fill) {
      Brush.fillStyle = Fill;
      Brush.fill();
    }
    Brush.strokeStyle = Stroke;
    Brush.stroke();
  };
  const Dimension = (Left, Right, Vertical, Caption) => {
    Path([
      [Left, Vertical - 4],
      [Left, Vertical + 4],
    ]);
    Path([
      [Right, Vertical - 4],
      [Right, Vertical + 4],
    ]);
    Path(
      [
        [Left, Vertical],
        [Right, Vertical],
      ],
      "#ffffff55",
    );
    Text(Caption, (Left + Right) / 2, Vertical + 17, "center");
  };
  const Glow = (Horizontal, Vertical, Radius, Opacity) => {
    const Gradient = Brush.createRadialGradient(
      Horizontal,
      Vertical,
      0,
      Horizontal,
      Vertical,
      Radius,
    );
    Gradient.addColorStop(0, Tint(Opacity));
    Gradient.addColorStop(1, Tint(0));
    Brush.fillStyle = Gradient;
    Brush.fillRect(
      Horizontal - Radius,
      Vertical - Radius,
      Radius * 2,
      Radius * 2,
    );
  };
  if (Style === "arealight") {
    const Across = Read("width", 2),
      Tall = Read("height", 1),
      Scale = Math.min(230 / Across, 140 / Tall),
      HalfWidth = (Across * Scale) / 2,
      HalfHeight = (Tall * Scale) / 2,
      CenterY = 122;
    Text("APERTURE / FRONT ELEVATION", 22, 18);
    // Orthographic outline: no perspective plane, floor or frustum.
    Path(
      [
        [190, 36],
        [190, 207],
      ],
      "#ffffff18",
      null,
      true,
    );
    Path(
      [
        [40, CenterY],
        [340, CenterY],
      ],
      "#ffffff18",
      null,
      true,
    );
    Brush.save();
    Brush.beginPath();
    if (Properties.aperture === "Disk")
      Brush.ellipse(190, CenterY, HalfWidth, HalfHeight, 0, 0, Math.PI * 2);
    else
      Brush.rect(
        190 - HalfWidth,
        CenterY - HalfHeight,
        HalfWidth * 2,
        HalfHeight * 2,
      );
    Brush.fillStyle = Tint(0.07 + Math.min(0.12, Flux() / 80000));
    Brush.fill();
    Brush.strokeStyle = Tint(0.9);
    Brush.lineWidth = 1.5;
    Brush.stroke();
    Brush.clip();
    for (let X = 50; X <= 330; X += 12)
      for (let Y = 44; Y <= 206; Y += 12) {
        Brush.fillStyle = Tint(0.35);
        Brush.fillRect(X, Y, 1.5, 1.5);
      }
    Brush.restore();
    Dimension(190 - HalfWidth, 190 + HalfWidth, 218, Across.toFixed(1) + " m");
    const Edge = 190 + HalfWidth + 15;
    Path(
      [
        [Edge - 4, CenterY - HalfHeight],
        [Edge + 4, CenterY - HalfHeight],
      ],
      "#ffffff66",
    );
    Path(
      [
        [Edge, CenterY - HalfHeight],
        [Edge, CenterY + HalfHeight],
      ],
      "#ffffff44",
    );
    Path(
      [
        [Edge - 4, CenterY + HalfHeight],
        [Edge + 4, CenterY + HalfHeight],
      ],
      "#ffffff66",
    );
    Brush.save();
    Brush.translate(Edge + 13, CenterY);
    Brush.rotate(-Math.PI / 2);
    Text(Tall.toFixed(1) + " m", 0, 0, "center");
    Brush.restore();
    Text(Properties.twoSided ? "BOTH FACES" : "FRONT FACE", 22, 252);
    Text(Read("spread", 120) + "° SPREAD", 358, 252, "right");
  } else if (Style === "ieslight") {
    if (Properties.profile === "Custom .IES")
      Text("No measured IES samples loaded", 190, 130, "center");
    else {
      const Radius = 96,
        CenterY = 118,
        Profile = Properties.profile || "Downlight";
      for (const Fraction of [0.25, 0.5, 0.75, 1])
        Circle(190, CenterY, Radius * Fraction);
      for (let Index = 0; Index < 8; Index++) {
        const Angle = (Index * Math.PI) / 4;
        Path(
          [
            [190, CenterY],
            [
              190 + Math.sin(Angle) * Radius,
              CenterY + Math.cos(Angle) * Radius,
            ],
          ],
          "#ffffff14",
          null,
          true,
        );
      }
      for (const Section of [1, 0]) {
        const Points = [];
        for (let Index = 0; Index <= 240; Index++) {
          const Angle = (Index / 240) * Math.PI * 2,
            Pitch = (Read("cutoff", 0) * Math.PI) / 180,
            Lobe = Polar(Profile, Angle, Read("cone", 60), Section);
          Points.push([
            190 + Math.sin(Angle + Pitch) * Radius * Lobe,
            CenterY + Math.cos(Angle + Pitch) * Radius * Lobe,
          ]);
        }
        Path(
          Points,
          Section ? "#94b7ad" : Tint(0.95),
          Section
            ? "#94b7ad0b"
            : Tint(Math.min(0.16, (Flux() * Read("multiplier", 1)) / 18000)),
          !!Section,
        );
      }
      Text("C0", 25, 24, "left", Tint(0.9));
      Text("C90", 330, 24, "right", "#94b7ad");
      Text("90°", 69, 122);
      Text("90°", 312, 122, "right");
      Text("0°", 190, 235, "center");
      Text("NORMALIZED PRESET", 190, 254, "center");
    }
  } else if (Style === "ledlight") {
    const Size = Clamp(100 + Read("diameter", 40) * 0.5, 102, 158),
      Left = 190 - Size / 2,
      Top = 126 - Size / 2;
    Brush.fillStyle = "#303430";
    Brush.strokeStyle = "#777d72";
    Brush.beginPath();
    Brush.roundRect(Left - 14, Top - 14, Size + 28, Size + 28, 9);
    Brush.fill();
    Brush.stroke();
    for (const Sign of [-1, 1])
      for (let Index = 0; Index < 5; Index++) {
        Brush.fillStyle = "#857958";
        Brush.fillRect(
          190 + Sign * (Size / 2 + 20) - (Sign < 0 ? 8 : 0),
          Top + 9 + (Index * (Size - 18)) / 4,
          8,
          3,
        );
      }
    Glow(190, 126, 95, 0.22 * Read("dimmer", 1));
    Brush.fillStyle = Tint(0.15 + 0.35 * Read("dimmer", 1));
    Brush.fillRect(Left, Top, Size, Size);
    for (let Column = 0; Column < 4; Column++)
      for (let Row = 0; Row < 4; Row++) {
        const Pitch = Size / 4;
        Brush.fillStyle = Tint(0.15 + 0.65 * Read("dimmer", 1));
        Brush.fillRect(
          Left + Column * Pitch + 7,
          Top + Row * Pitch + 7,
          Pitch - 14,
          Pitch - 14,
        );
      }
    const Angle = (Read("angle", 120) * Math.PI) / 360;
    Brush.beginPath();
    Brush.arc(
      190,
      126,
      Size * 0.79,
      -Math.PI / 2 - Angle,
      -Math.PI / 2 + Angle,
    );
    Brush.strokeStyle = Tint(0.55);
    Brush.stroke();
    for (const Sign of [-1, 1])
      Circle(190 + Sign * (Size / 2 + 7), Top - 7, 2, "#999f92");
    Dimension(Left, Left + Size, 225, Read("diameter", 40) + " mm PACKAGE");
    Text(Read("angle", 120) + "° OPTIC", 190, 17, "center");
  } else if (Style === "ledstrip") {
    const Count = Math.round(Read("length", 2.4) * Read("ledsPerMetre", 60)),
      Samples = Math.min(64, Math.max(4, Count));
    const Points = Array.from({ length: Samples }, (_, Index) => {
      const Fraction = Index / (Samples - 1);
      return [40 + Fraction * 300, 123];
    });
    Brush.lineWidth = 18;
    Path(Points, "#424238");
    Brush.lineWidth = 12;
    Path(Points, "#262a23");
    Brush.lineWidth = 1;
    if (Properties.diffuser) {
      Brush.lineWidth = 10;
      Brush.shadowColor = Tint(0.6);
      Brush.shadowBlur = 14;
      Path(Points, Tint(0.12 + 0.45 * Read("dimmer", 1)));
      Brush.shadowBlur = 0;
      Brush.lineWidth = 1;
    } else
      Points.forEach(([Horizontal, Vertical], Index) => {
        Brush.fillStyle = Tint(0.12 + 0.7 * Read("dimmer", 1));
        Brush.fillRect(Horizontal - 2.5, Vertical - 2.5, 5, 5);
        if (Index % 8 === 0) {
          Brush.strokeStyle = "#bcaa6555";
          Brush.strokeRect(Horizontal - 4, Vertical - 5, 8, 10);
        }
      });
    Text("+", Points[0][0] - 17, Points[0][1] - 4, "center", "#c4ac79");
    Text("−", Points[0][0] - 17, Points[0][1] + 10, "center");
    Dimension(
      40,
      340,
      225,
      Read("length", 2.4).toFixed(1) + " m RUN · " + Count + " EMITTERS",
    );
    Text(Read("voltage", 24) + " V DC", 20, 18);
    Text(
      Properties.diffuser ? "OPAL DIFFUSER" : "EXPOSED PACKAGES",
      360,
      18,
      "right",
    );
  } else if (Style === "spotlight") {
    const Half = Clamp(
        Math.tan((Read("angle", 26) * Math.PI) / 360) * 220,
        12,
        145,
      ),
      Soft = Read("penumbra", 0.4);
    const Gradient = Brush.createLinearGradient(0, 50, 0, 215);
    Gradient.addColorStop(0, Tint(0.015));
    Gradient.addColorStop(1, Tint(0.06 + Math.min(0.2, Flux() / 1000)));
    Path(
      [
        [190, 49],
        [190 + Half, 211],
        [190 - Half, 211],
      ],
      Tint(0.45),
      Gradient,
    );
    Path(
      [
        [190 - Half * (1 - Soft), 211],
        [190, 49],
        [190 + Half * (1 - Soft), 211],
      ],
      Tint(0.65),
      null,
      true,
    );
    for (let Index = 1; Index <= 3; Index++) {
      const Fraction = Index / 3;
      Path(
        [
          [190 - Half * Fraction, 49 + 162 * Fraction],
          [190 + Half * Fraction, 49 + 162 * Fraction],
        ],
        "#ffffff18",
        null,
        true,
      );
    }
    Brush.fillStyle = "#454943";
    Brush.beginPath();
    Brush.roundRect(172, 22, 36, 24, 5);
    Brush.fill();
    Brush.strokeStyle = "#a5ab9e";
    Brush.stroke();
    Path(
      [
        [190, 49],
        [190, 211],
      ],
      "#ffffff30",
      null,
      true,
    );
    Dimension(190 - Half, 190 + Half, 225, Read("angle", 26) + "° FULL CONE");
    Text("PENUMBRA", 20, 16);
    Text(Math.round(Soft * 100) + " %", 360, 16, "right");
  } else if (Style === "pointlight") {
    const Reach = Read("distance", 30),
      Decay = Read("decay", 2);
    for (let Index = 4; Index > 0; Index--) {
      const Radius = Index * 24;
      Circle(190, 123, Radius, "#ffffff1c");
      Text(((Reach * Index) / 4).toFixed(0) + " m", 190 + Radius + 7, 123);
    }
    for (let Index = 0; Index < 12; Index++) {
      const Angle = (Index * Math.PI) / 6;
      Path(
        [
          [190 + Math.cos(Angle) * 25, 123 + Math.sin(Angle) * 25],
          [190 + Math.cos(Angle) * 96, 123 + Math.sin(Angle) * 96],
        ],
        Tint(0.15),
        null,
        true,
      );
    }
    Glow(190, 123, 80, Math.min(0.45, Flux() / 100));
    Circle(190, 123, 6, Tint(0.9), Tint(0.65));
    Text("OMNIDIRECTIONAL", 20, 18);
    Text("DECAY " + Decay.toFixed(1), 360, 18, "right");
    Text("Reach guides · free space", 190, 248, "center");
  } else {
    const Length = Clamp(160 + Read("length", 1.5) * 22, 165, 302),
      Radius = Clamp(7 + Read("radius", 0.04) * 85, 7, 28),
      Left = 190 - Length / 2;
    const Gradient = Brush.createLinearGradient(
      0,
      104 - Radius,
      0,
      104 + Radius,
    );
    Gradient.addColorStop(0, Tint(0.1));
    Gradient.addColorStop(0.45, Tint(0.2 + Math.min(0.7, Flux() / 5000)));
    Gradient.addColorStop(1, Tint(0.15));
    Brush.shadowColor = Tint(0.4);
    Brush.shadowBlur = 20;
    Brush.fillStyle = Gradient;
    Brush.beginPath();
    Brush.roundRect(Left, 104 - Radius, Length, Radius * 2, Radius);
    Brush.fill();
    Brush.shadowBlur = 0;
    Brush.strokeStyle = Tint(0.7);
    Brush.stroke();
    for (const Horizontal of [Left + 7, Left + Length - 7])
      Path(
        [
          [Horizontal, 104 - Radius],
          [Horizontal, 104 + Radius],
        ],
        "#9ba598",
      );
    for (let Index = 0; Index < 9; Index++) {
      const Horizontal = Left + (Length * Index) / 8;
      Path(
        [
          [Horizontal, 104 + Radius + 10],
          [Horizontal, 191],
        ],
        Tint(0.12),
        null,
        true,
      );
      Circle(Horizontal, 191, 1, Tint(0.25));
    }
    Dimension(
      Left,
      Left + Length,
      221,
      Read("length", 1.5).toFixed(1) + " m LENGTH",
    );
    Text((Read("radius", 0.04) * 1000).toFixed(0) + " mm RADIUS", 20, 18);
    Text("RADIAL EMISSION", 360, 18, "right");
  }
  Brush.restore();
}
export function ProjectResponse(
  Brush,
  Width,
  Height,
  { Style, Read, Properties, Tint, Flux },
) {
  Brush.clearRect(0, 0, Width, Height);
  Brush.lineWidth = 1;
  Brush.font = "9px sans-serif";
  if (Style === "ieslight" && Properties.profile === "Custom .IES") {
    Brush.fillStyle = "#929b8f";
    Brush.fillText("No measured samples loaded", 8, Height / 2);
    return;
  }
  if (Style === "pointlight") {
    const Left = 43,
      Right = Width - 15,
      Top = 28,
      Bottom = Height - 30,
      Maximum = Math.max(0.01, Flux()),
      ValueAt = (Distance) => Flux() / Distance ** Read("decay", 2),
      X = (Distance) => Left + ((Distance - 1) / 9) * (Right - Left),
      Y = (Value) => Bottom - (Value / Maximum) * (Bottom - Top),
      Label = (Value) =>
        Value >= 1000
          ? (Value / 1000).toFixed(1) + "k"
          : Value.toLocaleString("en-US", { maximumSignificantDigits: 3 });
    Brush.fillStyle = "#929b8f";
    Brush.textAlign = "left";
    Brush.fillText("ILLUMINANCE · lx", Left, 12);
    for (const Fraction of [0, 0.25, 0.5, 0.75, 1]) {
      const Vertical = Y(Maximum * Fraction);
      Brush.beginPath();
      Brush.moveTo(Left, Vertical);
      Brush.lineTo(Right, Vertical);
      Brush.strokeStyle = "#ffffff14";
      Brush.stroke();
      Brush.fillStyle = "#929b8f";
      Brush.textAlign = "right";
      Brush.fillText(Label(Maximum * Fraction), Left - 7, Vertical + 3);
    }
    Brush.beginPath();
    for (let Index = 0; Index <= 120; Index++) {
      const Distance = 1 + (9 * Index) / 120;
      Index
        ? Brush.lineTo(X(Distance), Y(ValueAt(Distance)))
        : Brush.moveTo(X(Distance), Y(ValueAt(Distance)));
    }
    Brush.strokeStyle = Tint(0.9);
    Brush.lineWidth = 1.5;
    Brush.stroke();
    Brush.lineTo(Right, Bottom);
    Brush.lineTo(Left, Bottom);
    Brush.closePath();
    Brush.fillStyle = Tint(0.06);
    Brush.fill();
    for (const Distance of [1, 2, 5, 10]) {
      Brush.beginPath();
      Brush.moveTo(X(Distance), Top);
      Brush.lineTo(X(Distance), Bottom);
      Brush.strokeStyle = Distance === 5 ? "#ffffff44" : "#ffffff14";
      Brush.setLineDash([2, 4]);
      Brush.stroke();
      Brush.setLineDash([]);
      Brush.beginPath();
      Brush.arc(X(Distance), Y(ValueAt(Distance)), 3, 0, Math.PI * 2);
      Brush.fillStyle = Distance === 5 ? "#eeeeea" : Tint(0.9);
      Brush.fill();
      Brush.fillStyle = "#a5aca1";
      Brush.textAlign = "center";
      Brush.fillText(Distance + " m", X(Distance), Bottom + 16);
    }
    return;
  }
  const Left = 8,
    Right = Width - 8,
    Top = 10,
    Bottom = Height - 18;
  for (let Index = 0; Index < 3; Index++) {
    const Vertical = Top + (Index * (Bottom - Top)) / 2;
    Brush.beginPath();
    Brush.setLineDash([2, 5]);
    Brush.moveTo(Left, Vertical);
    Brush.lineTo(Right, Vertical);
    Brush.strokeStyle = "#ffffff17";
    Brush.stroke();
  }
  Brush.setLineDash([]);
  const BeamSection = (Fraction) => {
    const Normalized =
        Math.abs((Fraction - 0.5) * 180) / (Math.max(2, Read("angle", 26)) / 2),
      Soft = Read("penumbra", 0.4);
    return Normalized > 1
      ? 0
      : Normalized <= 1 - Soft
        ? 1
        : Math.max(0, (1 - Normalized) / Math.max(0.001, Soft));
  };
  const At = (Fraction) =>
    Style === "pointlight"
      ? 1 / (1 + Fraction * 9) ** Read("decay", 2)
      : Style === "ieslight"
        ? Polar(
            Properties.profile || "Downlight",
            (Fraction - 0.5) * Math.PI,
            Read("cone", 60),
          )
        : Style === "spotlight"
          ? BeamSection(Fraction)
          : Style === "ledlight"
            ? Fraction * Read("dimmer", 1)
            : Style === "ledstrip"
              ? Fraction
              : Style === "arealight"
                ? Math.cos((Fraction - 0.5) * Math.PI)
                : 0.72;
  Brush.beginPath();
  for (let Index = 0; Index <= 80; Index++) {
    const Fraction = Index / 80,
      Horizontal = Left + Fraction * (Right - Left),
      Vertical = Bottom - At(Fraction) * (Bottom - Top);
    Index
      ? Brush.lineTo(Horizontal, Vertical)
      : Brush.moveTo(Horizontal, Vertical);
  }
  Brush.strokeStyle = Tint(0.8);
  Brush.stroke();
  Brush.fillStyle = "#828a82";
  Brush.textAlign = "left";
  Brush.fillText(
    Style === "pointlight"
      ? "1 m"
      : Style === "ieslight" || Style === "spotlight" || Style === "arealight"
        ? "−90°"
        : "0",
    Left,
    Height - 3,
  );
  Brush.textAlign = "right";
  Brush.fillText(
    Style === "pointlight"
      ? "10 m"
      : Style === "ieslight" || Style === "spotlight" || Style === "arealight"
        ? "90°"
        : Style === "ledstrip"
          ? Read("length", 2.4) + " m"
          : "100%",
    Right,
    Height - 3,
  );
}
