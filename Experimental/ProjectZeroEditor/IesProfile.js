//============================================================================================================================================
//                                                              IESPROFILE.JS
//============================================================================================================================================
// 💡 Custom IES photometry: a candela curve editor (vertical angle γ 0–180° against relative intensity) for the C0 and C90 planes, preset
// sampling, monotone-cubic or linear interpolation, beam / field / peak readouts, a local profile library and LM-63-2002 import / export.
// Browser authoring only; nothing here reaches the native renderer.

import { el } from "./InspectorDepot/kit.js";

const Clamp = (Value, Minimum, Maximum) => Math.max(Minimum, Math.min(Maximum, Value));
export const Presets = ["Downlight", "Wall wash", "Batwing", "ECE Low Beam", "SAE Low Beam", "High Beam", "Fog Lamp", "Parking Lamp"];

// Analytical preset lobes (angle in radians from the downward axis, signed; Section 1 = C90 plane).
export function PresetLobe(Profile, Angle, Cone, Section = 0) {
  Angle *= 60 / Math.max(5, Cone);
  if (Math.abs(Angle) > Math.PI / 2) return 0;
  const Axis = Math.max(0, Math.cos(Angle));
  if (Profile === "Batwing") return Math.abs(Math.sin(Angle * 2)) ** 1.4 * Axis;
  if (Profile === "Wall wash") return Math.max(0, Math.cos(Angle - 0.55)) ** (Section ? 3 : 5);
  const Power = Profile === "Fog Lamp" ? 0.65 : Profile === "Parking Lamp" ? 0.4 : (Profile === "High Beam" ? 240 : 130) / 60;
  let Lobe = Axis ** Math.max(0.4, Power * (Section ? 0.65 : 1));
  if (Profile.includes("Low Beam")) Lobe *= Math.sin(Angle) > 0 ? (Profile.startsWith("SAE") ? 0.5 : 0.65) : 1;
  return Lobe;
}

// Sample a preset into editable control points (one plane), normalised to a peak of 1.
export function SamplePreset(Profile, Cone, Section = 0) {
  const Angles = [0, 10, 20, 30, 40, 50, 60, 75, 90, 120, 180];
  const Raw = Angles.map((Gamma) => [Gamma, PresetLobe(Profile, (Gamma * Math.PI) / 180, Cone, Section)]);
  const Peak = Math.max(1e-6, ...Raw.map((Point) => Point[1]));
  return Raw.map(([Gamma, Value]) => [Gamma, +(Value / Peak).toFixed(4)]);
}

// Monotone cubic (Fritsch–Carlson) or linear interpolation through sorted control points.
export function CurveAt(Points, Gamma, Smooth = true) {
  if (!Points?.length) return 0;
  if (Gamma <= Points[0][0]) return Points[0][1];
  const Last = Points[Points.length - 1];
  if (Gamma >= Last[0]) return Last[1];
  let Index = 0;
  while (Points[Index + 1][0] < Gamma) Index++;
  const [X0, Y0] = Points[Index], [X1, Y1] = Points[Index + 1];
  const Span = X1 - X0 || 1, T = (Gamma - X0) / Span;
  if (!Smooth) return Y0 + (Y1 - Y0) * T;
  const Slope = (A) => {
    if (A <= 0 || A >= Points.length - 1) return A <= 0 ? (Points[1][1] - Points[0][1]) / (Points[1][0] - Points[0][0] || 1) : (Last[1] - Points[A - 1][1]) / (Last[0] - Points[A - 1][0] || 1);
    const Before = (Points[A][1] - Points[A - 1][1]) / (Points[A][0] - Points[A - 1][0] || 1);
    const After = (Points[A + 1][1] - Points[A][1]) / (Points[A + 1][0] - Points[A][0] || 1);
    return Before * After <= 0 ? 0 : (2 * Before * After) / (Before + After);
  };
  const M0 = Slope(Index) * Span, M1 = Slope(Index + 1) * Span;
  const T2 = T * T, T3 = T2 * T;
  return Clamp((2 * T3 - 3 * T2 + 1) * Y0 + (T3 - 2 * T2 + T) * M0 + (-2 * T3 + 3 * T2) * Y1 + (T3 - T2) * M1, 0, 1.5);
}

// The curves a light uses: custom points, or the sampled preset.
export function Curves(Properties, Read) {
  if (Properties.profile === "Custom" && Array.isArray(Properties.iesC0) && Properties.iesC0.length > 1) {
    const C0 = Properties.iesC0;
    return { C0, C90: Array.isArray(Properties.iesC90) && Properties.iesC90.length > 1 ? Properties.iesC90 : C0, Smooth: Properties.iesSmooth !== false, Custom: true };
  }
  const Profile = Properties.profile && Presets.includes(Properties.profile) ? Properties.profile : "Downlight";
  return { C0: SamplePreset(Profile, Read("cone", 60), 0), C90: SamplePreset(Profile, Read("cone", 60), 1), Smooth: true, Custom: false, Profile };
}

// Lobe used by the polar and response drawings: presets stay analytical, custom profiles read the curve.
export function ProfileLobe(Properties, Read, Angle, Section = 0) {
  if (Properties.profile === "Custom" && Array.isArray(Properties.iesC0)) {
    const Pair = Curves(Properties, Read);
    const Gamma = Math.min(180, (Math.abs(Angle) * 180) / Math.PI);
    return CurveAt(Section ? Pair.C90 : Pair.C0, Gamma, Pair.Smooth);
  }
  return PresetLobe(Properties.profile && Presets.includes(Properties.profile) ? Properties.profile : "Downlight", Angle, Read("cone", 60), Section);
}

// Photometric figures from the curves: flux integral, peak candela for the authored lumens, beam (50 %) and field (10 %) angles.
export function Figures(Pair, Lumens) {
  const Steps = 360;
  let Total = 0, Upward = 0;
  for (let Step = 0; Step < Steps; Step++) {
    const Gamma = ((Step + 0.5) / Steps) * 180, Radians = (Gamma * Math.PI) / 180;
    const Relative = (CurveAt(Pair.C0, Gamma, Pair.Smooth) + CurveAt(Pair.C90, Gamma, Pair.Smooth)) / 2;
    const Part = Relative * 2 * Math.PI * Math.sin(Radians) * (Math.PI / Steps);
    Total += Part;
    if (Gamma > 90) Upward += Part;
  }
  const Peak = Math.max(1e-6, ...Array.from({ length: 181 }, (_, Gamma) => Math.max(CurveAt(Pair.C0, Gamma, Pair.Smooth), CurveAt(Pair.C90, Gamma, Pair.Smooth))));
  const Crossing = (Fraction) => {
    for (let Gamma = 0; Gamma <= 180; Gamma += 0.25) if (CurveAt(Pair.C0, Gamma, Pair.Smooth) < Peak * Fraction && Gamma > 0) return Gamma;
    return 180;
  };
  return {
    PeakCandela: Total > 0 ? (Lumens * Peak) / Total : 0,
    Beam: 2 * Crossing(0.5),
    Field: 2 * Crossing(0.1),
    Upward: Total > 0 ? Upward / Total : 0,
  };
}

// ── LM-63-2002 export / import ──────────────────────────────────────────────────────────────────────────────────────────────────────────
export function ExportIes(Pair, Lumens, Name) {
  const Vertical = Array.from({ length: 73 }, (_, Index) => Index * 2.5);
  const { PeakCandela } = Figures(Pair, Lumens);
  const Peak = Math.max(1e-6, ...Vertical.map((Gamma) => Math.max(CurveAt(Pair.C0, Gamma, Pair.Smooth), CurveAt(Pair.C90, Gamma, Pair.Smooth))));
  const Symmetric = JSON.stringify(Pair.C0) === JSON.stringify(Pair.C90);
  const Horizontal = Symmetric ? [0] : [0, 90, 180, 270, 360];
  const Plane = (H) => (H % 180 === 0 ? Pair.C0 : Pair.C90);
  const Wrap = (Values) => {
    const Lines = [];
    for (let Index = 0; Index < Values.length; Index += 10) Lines.push(Values.slice(Index, Index + 10).join(" "));
    return Lines.join("\n");
  };
  const Candela = Horizontal.map((H) => Wrap(Vertical.map((Gamma) => ((CurveAt(Plane(H), Gamma, Pair.Smooth) / Peak) * PeakCandela).toFixed(2))));
  return [
    "IESNA:LM-63-2002",
    "[TEST] Frontier custom profile (browser authoring)",
    "[MANUFAC] Frontier",
    `[LUMCAT] ${Name}`,
    `[LUMINAIRE] ${Name}`,
    `[ISSUEDATE] ${new Date().toISOString().slice(0, 10)}`,
    "TILT=NONE",
    `1 ${Lumens.toFixed(1)} 1 ${Vertical.length} ${Horizontal.length} 1 2 0 0 0`,
    "1 1 0",
    Wrap(Vertical.map((Gamma) => Gamma.toFixed(1))),
    Wrap(Horizontal.map((H) => H.toFixed(1))),
    ...Candela,
    "",
  ].join("\n");
}

export function ImportIes(Text) {
  const Lines = Text.replace(/\r/g, "").split("\n");
  const TiltLine = Lines.findIndex((Line) => /^\s*TILT\s*=/i.test(Line));
  if (TiltLine < 0) throw new Error("Not an LM-63 file: no TILT line.");
  const Label = (Lines.find((Line) => /^\[(LUMINAIRE|LUMCAT)\]/i.test(Line)) || "").replace(/^\[[^\]]+\]\s*/, "").trim();
  let Numbers = Lines.slice(TiltLine + 1).join(" ").trim().split(/[\s,]+/).filter(Boolean).map(Number);
  if (/INCLUDE/i.test(Lines[TiltLine])) {
    const Pairs = Numbers[1];
    Numbers = Numbers.slice(2 + Pairs * 2);
  }
  const [Lamps, LumensPerLamp, Multiplier, VerticalCount, HorizontalCount] = Numbers;
  if (!(VerticalCount > 1) || !(HorizontalCount >= 1)) throw new Error("Unreadable angle counts.");
  let Cursor = 13;
  const Vertical = Numbers.slice(Cursor, (Cursor += VerticalCount));
  const Horizontal = Numbers.slice(Cursor, (Cursor += HorizontalCount));
  const Planes = Horizontal.map(() => Numbers.slice(Cursor, (Cursor += VerticalCount)).map((Value) => Value * (Multiplier || 1)));
  if (Planes.some((Plane) => Plane.length < VerticalCount || Plane.some((Value) => !Number.isFinite(Value)))) throw new Error("Candela table is incomplete.");
  const Nearest = (Target) => Planes[Horizontal.reduce((Best, H, Index) => (Math.abs(H - Target) < Math.abs(Horizontal[Best] - Target) ? Index : Best), 0)];
  const Peak = Math.max(1e-6, ...Planes.flat());
  // Vertical angles may run 90–180 (uplight-only files) or −90–90; fold them into γ 0–180 and keep at most 25 points.
  const Points = (Plane) => {
    const All = Vertical.map((Gamma, Index) => [Clamp(Math.abs(Gamma), 0, 180), +(Plane[Index] / Peak).toFixed(4)]).sort((A, B) => A[0] - B[0]);
    const Unique = All.filter((Point, Index) => !Index || Point[0] !== All[Index - 1][0]);
    if (Unique.length <= 25) return Unique;
    return Array.from({ length: 25 }, (_, Index) => Unique[Math.round((Index / 24) * (Unique.length - 1))]);
  };
  const C0 = Points(Nearest(0)), C90 = HorizontalCount > 1 ? Points(Nearest(90)) : null;
  return { C0, C90, Lumens: LumensPerLamp > 0 ? LumensPerLamp * (Lamps || 1) : null, Name: Label || "Imported profile" };
}

// The inspector frame is sandboxed without storage: the editor owns the library and sends it down with each update.
export const ReadLibrary = () => (Array.isArray(globalThis.__IES_LIBRARY__) ? globalThis.__IES_LIBRARY__ : []);
const WriteLibrary = (Entries) => {
  globalThis.__IES_LIBRARY__ = Entries;
  parent.postMessage({ ReferenceInspector: true, Type: "ies-library", Entries }, "*");
};

// ── The editor ──────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────
const Svg = (Name, Attributes = {}) => {
  const Drawn = document.createElementNS("http://www.w3.org/2000/svg", Name);
  for (const [Key, Value] of Object.entries(Attributes)) Drawn.setAttribute(Key, Value);
  return Drawn;
};

export function MountIesEditor(Section, { Properties, Read, Write, Locked, Flux, Tint, Bindings }) {
  const Box = el("div", "ies-editor lp-response");
  Section.append(Box);
  const W = 300, H = 170, Left = 30, Right = 292, Top = 14, Bottom = 142;
  const X = (Gamma) => Left + (Gamma / 180) * (Right - Left);
  const Y = (Value) => Bottom - Value * (Bottom - Top);
  const Plot = Svg("svg", { viewBox: `0 0 ${W} ${H}`, class: "ies-plot", role: "application", "aria-label": "IES candela curve editor", tabindex: "0" });
  Box.append(Plot);

  // Plane selector and tools.
  const Tools = el("div", "ies-tools");
  const Plane = { Active: "C0" };
  const PlaneButtons = ["C0", "C90"].map((Name) => {
    const Button = el("button", "", Name + " plane");
    Button.type = "button";
    Button.onclick = () => {
      Plane.Active = Name;
      Paint();
    };
    Tools.append(Button);
    return [Name, Button];
  });
  const Linked = el("button", "", "Linked");
  Linked.type = "button";
  Linked.title = "C90 follows C0 (rotationally symmetric)";
  const SmoothButton = el("button", "", "Smooth");
  SmoothButton.type = "button";
  Tools.append(Linked, SmoothButton);
  Box.append(Tools);

  const Readings = el("div", "ies-readings");
  Box.append(Readings);
  const Hint = el("p", "lp-note ies-hint", "Drag points · double-click to add or remove · arrow keys nudge the selected point.");
  Box.append(Hint);

  const Naming = el("label", "ies-name");
  const NameInput = el("input");
  NameInput.type = "text";
  NameInput.maxLength = 48;
  NameInput.placeholder = "Profile name";
  NameInput.setAttribute("aria-label", "Profile name");
  NameInput.onchange = () => {
    if (Locked()) return;
    Begin();
    Write("iesName", NameInput.value.trim() || "Custom profile");
  };
  Naming.append(el("span", "", "Name"), NameInput);
  Box.append(Naming);
  const Actions = el("div", "ies-actions");
  const Action = (Label, Handler, Title) => {
    const Button = el("button", "", Label);
    Button.type = "button";
    Button.title = Title || Label;
    Button.onclick = Handler;
    Actions.append(Button);
    return Button;
  };
  const File = el("input");
  File.type = "file";
  File.accept = ".ies,.IES,text/plain";
  File.hidden = true;
  Box.append(Actions, File);

  let Selected = null;
  const Current = () => Curves(Properties, Read);
  // Editing a preset turns it into a custom profile seeded from the preset's samples.
  const Begin = () => {
    const Pair = Current();
    if (!Pair.Custom) {
      Write("iesName", (Pair.Profile || "Downlight") + " (edited)");
      Write("iesC0", Pair.C0.map((Point) => Point.slice()));
      Write("iesC90", JSON.stringify(Pair.C0) === JSON.stringify(Pair.C90) ? null : Pair.C90.map((Point) => Point.slice()));
      Write("iesSmooth", true);
      Write("profile", "Custom");
    }
  };
  const Edit = (Mutate) => {
    if (Locked()) return;
    Begin();
    const LinkedNow = !Array.isArray(Properties.iesC90);
    const Key = Plane.Active === "C90" && !LinkedNow ? "iesC90" : "iesC0";
    const Points = Properties[Key].map((Point) => Point.slice());
    Mutate(Points);
    Points.sort((A, B) => A[0] - B[0]);
    Write(Key, Points);
  };
  Linked.onclick = () => {
    if (Locked()) return;
    Begin();
    Write("iesC90", Array.isArray(Properties.iesC90) ? null : Properties.iesC0.map((Point) => Point.slice()));
    if (!Array.isArray(Properties.iesC90)) Plane.Active = "C0";
  };
  SmoothButton.onclick = () => {
    if (Locked()) return;
    Begin();
    Write("iesSmooth", Properties.iesSmooth === false);
  };

  const Pointer = (Event) => {
    const Rect = Plot.getBoundingClientRect();
    const Px = ((Event.clientX - Rect.left) / Rect.width) * W, Py = ((Event.clientY - Rect.top) / Rect.height) * H;
    return [Clamp(((Px - Left) / (Right - Left)) * 180, 0, 180), Clamp((Bottom - Py) / (Bottom - Top), 0, 1)];
  };
  const ActivePoints = () => {
    const Pair = Current();
    return Plane.Active === "C90" ? Pair.C90 : Pair.C0;
  };
  const Hit = (Event) => {
    const Rect = Plot.getBoundingClientRect();
    const Px = ((Event.clientX - Rect.left) / Rect.width) * W, Py = ((Event.clientY - Rect.top) / Rect.height) * H;
    let Best = null, Distance = 10;
    ActivePoints().forEach(([Gamma, Value], Index) => {
      const Gap = Math.hypot(X(Gamma) - Px, Y(Value) - Py);
      if (Gap < Distance) (Distance = Gap), (Best = Index);
    });
    return Best;
  };
  let Dragging = null;
  Plot.addEventListener("pointerdown", (Event) => {
    const Index = Hit(Event);
    if (Index === null) return;
    Selected = Index;
    Dragging = Index;
    Plot.setPointerCapture(Event.pointerId);
    Paint();
  });
  Plot.addEventListener("pointermove", (Event) => {
    if (Dragging === null) return;
    const [Gamma, Value] = Pointer(Event);
    Edit((Points) => {
      const Count = Points.length;
      const Lower = Dragging === 0 ? 0 : Points[Dragging - 1][0] + 0.5;
      const Upper = Dragging === Count - 1 ? 180 : Points[Dragging + 1][0] - 0.5;
      // The first and last points stay pinned to 0° and 180°.
      Points[Dragging] = [Dragging === 0 ? 0 : Dragging === Count - 1 ? 180 : +Clamp(Gamma, Lower, Upper).toFixed(1), +Value.toFixed(3)];
    });
  });
  const Release = () => (Dragging = null);
  Plot.addEventListener("pointerup", Release);
  Plot.addEventListener("pointercancel", Release);
  Plot.addEventListener("dblclick", (Event) => {
    const Index = Hit(Event);
    if (Index !== null) {
      if (Index === 0 || Index === ActivePoints().length - 1) return;
      Edit((Points) => Points.splice(Index, 1));
      Selected = null;
    } else {
      const [Gamma, Value] = Pointer(Event);
      Edit((Points) => Points.push([+Gamma.toFixed(1), +Value.toFixed(3)]));
    }
  });
  Plot.addEventListener("keydown", (Event) => {
    if (Selected === null) return;
    const Step = Event.shiftKey ? 0.1 : 1;
    const Move = { ArrowUp: [0, 0.02], ArrowDown: [0, -0.02], ArrowLeft: [-1, 0], ArrowRight: [1, 0] }[Event.key];
    if (Event.key === "Delete" || Event.key === "Backspace") {
      if (Selected === 0 || Selected === ActivePoints().length - 1) return;
      Event.preventDefault();
      Edit((Points) => Points.splice(Selected, 1));
      Selected = null;
      return;
    }
    if (!Move) return;
    Event.preventDefault();
    Edit((Points) => {
      const Point = Points[Selected];
      const Edge = Selected === 0 || Selected === Points.length - 1;
      Points[Selected] = [Edge ? Point[0] : +Clamp(Point[0] + Move[0] * Step, 0.5, 179.5).toFixed(1), +Clamp(Point[1] + Move[1] * Step, 0, 1).toFixed(3)];
    });
  });

  Action("Save to library", () => {
    const Pair = Current();
    const Name = NameInput.value.trim() || Properties.iesName || (Pair.Profile || "Custom") + " profile";
    const Entries = ReadLibrary().filter((Entry) => Entry.Name !== Name);
    Entries.push({ Name, C0: Pair.C0, C90: JSON.stringify(Pair.C0) === JSON.stringify(Pair.C90) ? null : Pair.C90, Smooth: Pair.Smooth });
    WriteLibrary(Entries.slice(-64));
    Status.textContent = `Saved “${Name}” to the profile library.`;
    if (!Locked()) {
      Begin();
      Write("iesName", Name);
    }
  }, "Keep this curve in the local profile library");
  Action("Export .ies", () => {
    const Pair = Current();
    const Name = Properties.iesName || Pair.Profile || "Custom profile";
    const Download = new Blob([ExportIes(Pair, Flux(), Name)], { type: "text/plain" });
    const Link = document.createElement("a");
    Link.href = URL.createObjectURL(Download);
    Link.download = Name.replace(/[^a-z0-9]+/gi, "_") + ".ies";
    Link.click();
    setTimeout(() => URL.revokeObjectURL(Link.href), 2000);
  }, "Download an LM-63-2002 file");
  Action("Import .ies", () => File.click(), "Load an LM-63 Type C file");
  Action("Reset to preset", () => {
    if (Locked()) return;
    Write("profile", Presets.includes(Properties.iesBase) ? Properties.iesBase : "Downlight");
  }, "Discard the custom curve");
  const Status = el("p", "lp-note ies-status");
  Box.append(Status);
  File.onchange = async () => {
    const Chosen = File.files?.[0];
    File.value = "";
    if (!Chosen || Locked()) return;
    try {
      const Imported = ImportIes(await Chosen.text());
      Write("iesC0", Imported.C0);
      Write("iesC90", Imported.C90);
      Write("iesSmooth", false);
      Write("iesName", Imported.Name);
      Write("profile", "Custom");
      if (Imported.Lumens) Write("lumens", Math.round(Imported.Lumens));
      Status.textContent = `Imported ${Chosen.name}: ${Imported.C0.length} points${Imported.C90 ? " per plane" : ", rotationally symmetric"}.`;
    } catch (Failure) {
      Status.textContent = "Import failed: " + Failure.message;
    }
  };

  function Paint() {
    const Pair = Current();
    const LinkedNow = JSON.stringify(Pair.C0) === JSON.stringify(Pair.C90) || (Pair.Custom && !Array.isArray(Properties.iesC90));
    if (LinkedNow) Plane.Active = "C0";
    Plot.replaceChildren();
    Plot.append(Svg("rect", { x: X(90), y: Top, width: Right - X(90), height: Bottom - Top, fill: "#ffffff05" }));
    for (const Value of [0.25, 0.5, 0.75, 1]) Plot.append(Svg("line", { x1: Left, x2: Right, y1: Y(Value), y2: Y(Value), stroke: "#ffffff12", "stroke-dasharray": "2 4" }));
    for (const Gamma of [0, 30, 60, 90, 120, 150, 180]) {
      Plot.append(Svg("line", { x1: X(Gamma), x2: X(Gamma), y1: Bottom, y2: Bottom + 4, stroke: "#5a5a5a" }));
      const Label = Svg("text", { x: X(Gamma), y: Bottom + 14, "text-anchor": "middle" });
      Label.textContent = Gamma + "°";
      Plot.append(Label);
    }
    for (const [Value, Text] of [[0, "0"], [0.5, "50%"], [1, "100%"]]) {
      const Label = Svg("text", { x: Left - 4, y: Y(Value) + 3, "text-anchor": "end" });
      Label.textContent = Text;
      Plot.append(Label);
    }
    const Caption = Svg("text", { x: X(135), y: Top + 10, "text-anchor": "middle" });
    Caption.textContent = "UPWARD";
    Plot.append(Caption);
    const Figure = Figures(Pair, Flux());
    // Beam (50 %) and field (10 %) markers on the C0 curve.
    for (const [Angle, Text] of [[Figure.Beam / 2, "beam"], [Figure.Field / 2, "field"]]) {
      if (Angle >= 180) continue;
      Plot.append(Svg("line", { x1: X(Angle), x2: X(Angle), y1: Top, y2: Bottom, stroke: "#ffffff30", "stroke-dasharray": "3 3" }));
      const Label = Svg("text", { x: X(Angle) + 3, y: Top + 10 });
      Label.textContent = Text;
      Plot.append(Label);
    }
    const Trace = (Points, Colour, Dashed) => {
      const D = Array.from({ length: 181 }, (_, Gamma) => (Gamma ? "L" : "M") + X(Gamma).toFixed(1) + " " + Y(CurveAt(Points, Gamma, Pair.Smooth)).toFixed(1)).join("");
      Plot.append(Svg("path", { d: D + `L${X(180)} ${Bottom}L${X(0)} ${Bottom}Z`, fill: Colour, "fill-opacity": Dashed ? 0.03 : 0.1, stroke: "none" }));
      Plot.append(Svg("path", { d: D, fill: "none", stroke: Colour, "stroke-width": 1.6, "stroke-dasharray": Dashed ? "4 3" : "none", "stroke-linecap": "round" }));
    };
    const Warm = Tint(1);
    if (!LinkedNow) Trace(Plane.Active === "C0" ? Pair.C90 : Pair.C0, Plane.Active === "C0" ? "#94b7ad" : Warm, true);
    Trace(Plane.Active === "C0" ? Pair.C0 : Pair.C90, Plane.Active === "C0" ? Warm : "#94b7ad", false);
    (Plane.Active === "C0" ? Pair.C0 : Pair.C90).forEach(([Gamma, Value], Index) => {
      Plot.append(Svg("circle", { cx: X(Gamma), cy: Y(Value), r: Index === Selected ? 5 : 3.6, fill: Index === Selected ? "#ffffff" : "#1a1a1a", stroke: "#f2f2f2", "stroke-width": 1.4, class: "ies-point" }));
    });
    for (const [Name, Button] of PlaneButtons) {
      Button.setAttribute("aria-pressed", Plane.Active === Name);
      Button.disabled = Name === "C90" && LinkedNow;
    }
    Linked.setAttribute("aria-pressed", LinkedNow);
    SmoothButton.setAttribute("aria-pressed", Pair.Smooth);
    SmoothButton.textContent = Pair.Smooth ? "Smooth" : "Linear";
    Readings.innerHTML = [
      ["Peak", Math.round(Figure.PeakCandela).toLocaleString("en-US") + " cd"],
      ["Beam", Figure.Beam >= 360 ? "—" : Figure.Beam.toFixed(0) + "°"],
      ["Field", Figure.Field >= 360 ? "—" : Figure.Field.toFixed(0) + "°"],
      ["Upward", (Figure.Upward * 100).toFixed(1) + " %"],
    ]
      .map(([Key, Value]) => `<div class="ies-reading"><b>${Value}</b><span>${Key}</span></div>`)
      .join("");
    if (document.activeElement !== NameInput) NameInput.value = Pair.Custom ? Properties.iesName || "" : "";
    NameInput.disabled = Locked();
    Hint.textContent = Pair.Custom
      ? `${Properties.iesName || "Custom profile"} · drag points, double-click to add or remove, arrow keys nudge.`
      : `${Pair.Profile} preset · drag a point to start a custom profile from it.`;
  }
  Bindings.push(Paint);
  return { Paint };
}
