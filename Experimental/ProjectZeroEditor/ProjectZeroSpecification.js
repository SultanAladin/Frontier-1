//============================================================================================================================================
//                                                         PROJECTZEROSPECIFICATION.JS
//============================================================================================================================================
// 🧩 Follow-up 48 Project Zero specification overlay on the native inspector sheets (NativePanels.json stays the untouched native
// export). Per panel: range and option corrections, controls the specification adds, and the editor fields the specification does
// not list. Those are kept and carry a 📝 note so the user can decide whether to remove them. Browser authoring only.

// 📝 Marks a field that exists in the editor but not in the Project Zero specification. Shown next to the label; never removed here.
export const SpecificationNoteMark = "📝";

const Slider = (Label, Minimum, Maximum, Default, Unit = "", Decimals = 2, Group = "Specification") => ({
  Label, Control: "Slider", Minimum, Maximum, Default, Unit, Decimals, Group, Added: true,
});
const Switch = (Label, Default, Group = "Specification") => ({ Label, Control: "Switch", Default, Group, Added: true });
const Choice = (Label, Options, Default, Group = "Specification") => ({ Label, Control: "Select", Options, Default, Group, Added: true });
const Colour = (Label, Default, Group = "Specification") => ({ Label, Control: "Colour", Default, Group, Added: true });
const Readout = (Label, Group = "Specification") => ({ Label, Control: "Readout", Default: "—", Group, Added: true });

export const WindSources = ["Global wind", "Cloud wind", "Local cloud wind", "Local fog wind", "Height fog wind", "Aerial fog wind"];

// The twelve specification cloud types. The first six are the native list, in the native order, so stored indices keep their meaning.
export const CloudTypes = [
  "Stratus", "Stratocumulus", "Cumulus", "Cumulonimbus", "Altostratus", "Cirrus",
  "Lenticular", "Mammatus", "Kelvin-Helmholtz", "Asperitas", "Nacreous", "Pyrocumulus",
];

// Sensor gates (horizontal gate width, mm). Choosing one writes Sensor Width.
export const SensorGates = [
  ["Custom", 0],
  ["Micro Four Thirds", 17.3],
  ["APS-C", 23.6],
  ["Super 35", 24.89],
  ["Full frame 36 mm", 36],
  ["Vista 35 mm", 37.72],
  ["65 mm", 54.12],
  ["IMAX 70 mm", 70.41],
];

export const MoonSlotCount = 6;
export const MoonPresets = ["Luna", "Ember", "Glacier", "Sulfur", "Shroud", "Shard"];
// Angular size per preset (degrees), also the default size of each slot.
export const MoonPresetSizes = [0.52, 1.6, 1.2, 1.1, 2.4, 0.6];
export const MaximumLocalClouds = 8;

// Panel → { Ranges: corrections by label, Notes: label → why it is flagged, Added: new fields }.
export const PanelSpecification = {
  sun: {
    Section: "§1 Sun",
    Ranges: {
      "Colour source": { Options: ["RGB tint", "Temperature (Kelvin)"] },
      Latitude: { Default: -26.19 },
      Longitude: { Default: 28.32 },
    },
    Notes: {
      Elevation: "Solved-direction readout; the specification lists manual azimuth / elevation only.",
      Azimuth: "Solved-direction readout; the specification lists manual azimuth / elevation only.",
      Declination: "Solar declination readout not listed in the specification.",
      "Equation of Time": "Equation-of-time readout not listed in the specification.",
    },
    Added: [
      Slider("Flare / glare", 0, 2, 1, "×", 2, "Sun Disk"),
      Choice("Appearance", ["Muted disc", "Radiant glare veil"], 0, "Sun Disk"),
      Switch("Manual direction", false, "Manual direction"),
      Slider("Manual azimuth", 0, 360, 300, "deg", 1, "Manual direction"),
      Slider("Manual elevation", -90, 90, 25, "deg", 1, "Manual direction"),
      Slider("Year", 1900, 2100, 2026, "", 0, "Observer / date"),
      Slider("UTC offset", -12, 14, 2, "h", 1, "Observer / date"),
      Switch("Cast shadows", true, "Sunlight gain"),
    ],
  },
  moon: {
    Section: "§2 Moon",
    Ranges: {},
    Notes: {},
    Added: [
      Switch("Has atmosphere", false, "Moon atmosphere"),
      Slider("Atmosphere intensity", 0, 3, 1, "×", 2, "Moon atmosphere"),
      Slider("Thickness scale", 0.1, 4, 1, "×", 2, "Moon atmosphere"),
      Colour("Atmosphere tint", "#9fc4ff", "Moon atmosphere"),
    ],
  },
  stars: {
    Section: "§3 Stars",
    Ranges: {},
    Notes: { "Sidereal Time": "Readout not listed in the specification." },
    Added: [
      Switch("Bright star catalogue", true, "Sky features"),
      Switch("Milky Way", true, "Sky features"),
      Switch("Zodiacal light", false, "Sky features"),
      Slider("Zodiacal radiance", 0, 4, 1, "×", 2, "Sky features"),
    ],
  },
  clouds: {
    Section: "§4 Clouds",
    Ranges: {
      Type: { Options: CloudTypes },
      "Shadow Type": { Options: CloudTypes },
      Coverage: { DisplayScale: 100, Unit: "%", Decimals: 0 },
      Base: { Minimum: 100, Maximum: 15000 },
      Thickness: { Minimum: 100, Maximum: 6500 },
      Ceiling: { Minimum: 4000, Maximum: 22000 },
    },
    Notes: {
      "Owned wind component": "The specification lists Follow wind and Wind source only.",
      "Light Taps": "Tier readout not listed in the specification.",
    },
    Added: [
      Slider("March steps", 16, 128, 64, "", 0, "Quality"),
      Slider("Render scale", 0.25, 1, 0.5, "×", 2, "Quality"),
      Slider("Wind speed", 0, 40, 8, "m/s", 1, "Cloud wind"),
      Slider("Wind bearing", 0, 360, 270, "deg", 0, "Cloud wind"),
      Slider("Wind shear", 0, 20, 2, "m/s per km", 1, "Cloud wind"),
      Slider("Wind veer", -60, 60, 0, "deg/km", 0, "Cloud wind"),
      Slider("Turbulence", 0, 1, 0.3, "", 2, "Cloud wind"),
    ],
  },
  "local-cloud": {
    Section: "§5 Local cloud",
    Ranges: {},
    Notes: { "Owned wind component": "The specification lists Follow wind and Wind source only." },
    Added: [
      Choice("Shape", ["Box", "Prism"], 0, "Shape"),
      Slider("Prism sides", 3, 12, 6, "", 0, "Shape"),
      Slider("Yaw", 0, 360, 0, "deg", 0, "Shape"),
      Slider("Edge softness", 0, 1, 0.25, "", 2, "Shape"),
    ],
  },
  atmosphere: {
    Section: "§6 Atmosphere",
    Ranges: {},
    Notes: {
      "White Line": "Twilight line gain is not listed in the specification.",
      "Line at civil only": "Twilight line switch is not listed in the specification.",
      "Air Mass": "Live readout not listed in the specification.",
      "Tier Samples": "Live readout not listed in the specification.",
      "Fetch Baked Dome": "Baked dome fetch is not listed in the specification.",
      "Dome Path": "Baked dome readout is not listed in the specification.",
      Sheet: "Baked dome readout is not listed in the specification.",
    },
    Added: [
      Slider("Planet radius", 1000, 20000, 6371, "km", 0, "Planet"),
      Switch("Orbital view", false, "Observer"),
      Slider("Observer altitude", 0, 400000, 2, "m", 0, "Observer"),
      Choice("Wind source", WindSources, 0, "Wind binding"),
    ],
  },
  "god-rays": {
    Section: "§7 Fog and God Rays",
    Ranges: {},
    Notes: {},
    Added: [],
  },
  flare: {
    Section: "§8 Lens flare",
    Ranges: {
      Type: { Note: "Specification styles are Anamorphic, Streaks and Halos; Cinematic, Starburst and Custom layers are editor extras." },
      Spread: { Minimum: 0.2, Maximum: 2 },
      Ghosts: { Minimum: 0, Maximum: 16, Decimals: 0 },
      "Ghost brightness": { Minimum: 0, Maximum: 2.5 },
      "Ghost spacing": { Minimum: 0.2, Maximum: 1.8 },
      "Halo brightness": { Minimum: 0, Maximum: 2.5 },
      "Ghost shape": { Options: ["Round (0 sides)", "Hexagon (6)", "Octagon (8)"] },
    },
    Notes: {
      "Halo Radius": "Not listed in the specification.",
      Chromatic: "Not listed in the specification.",
      "Aperture Blades": "Not listed in the specification (ghost sides are 0 / 6 / 8).",
      "Streak gain": "Not listed in the specification.",
      Anamorphic: "Layer switch; the specification chooses one style instead.",
      Streaks: "Layer switch; the specification chooses one style instead.",
      Starburst: "Layer switch; the specification chooses one style instead.",
      "Ray pairs": "Not listed in the specification.",
      "Halo width": "Not listed in the specification.",
      "Preview X": "Editor preview pose, not listed in the specification.",
      "Preview Y": "Editor preview pose, not listed in the specification.",
    },
    Added: [Choice("Style", ["Anamorphic", "Streaks", "Halos"], 0, "Lens")],
  },
  camera: {
    Section: "§9 Camera",
    Ranges: {
      "Focal Length": { Minimum: 1, Maximum: 500 },
      "Sensor Width": { Minimum: 8, Maximum: 72 },
      Aperture: { Minimum: 0.95, Maximum: 32 },
      "Subject Distance": { Minimum: 0.1, Maximum: 1000 },
    },
    Notes: {},
    Added: [
      Choice("Camera mode", ["Orbit", "Fly", "FirstPerson", "Follow", "Spline", "Planar"], 0, "Mode"),
      Choice("Projection", ["Perspective", "Orthographic"], 0, "Mode"),
      Choice("Projection preset", ["Free", "Top", "Bottom", "Front", "Back", "Left", "Right"], 0, "Mode"),
      Slider("Ortho width", 0.1, 1000, 20, "m", 1, "Mode"),
      Choice("Sensor gate", SensorGates.map(([Name, Width]) => (Width ? `${Name} · ${Width} mm` : Name)), 4, "Optics"),
      Readout("Vertical FOV", "Optics"),
      Slider("Near clip", 0.01, 10, 0.1, "m", 2, "Optics"),
      Slider("Far clip", 100, 100000, 20000, "m", 0, "Optics"),
      Slider("Fly speed", 0.1, 16, 1, "×", 1, "Navigation"),
      Slider("Sprint multiplier", 1, 8, 3, "×", 1, "Navigation"),
      Slider("Pitch limit", 0, 87, 87, "deg", 0, "Navigation"),
      Slider("Move smoothing", 0, 30, 10, "1/s", 1, "Navigation"),
      Slider("Look smoothing", 0, 30, 14, "1/s", 1, "Navigation"),
      Slider("Follow spring", 0, 50, 8, "1/s", 1, "Navigation"),
      Switch("Location lag", false, "Camera lag"),
      Switch("Rotation lag", false, "Camera lag"),
      Slider("Location lag speed", 0, 50, 10, "1/s", 1, "Camera lag"),
      Slider("Rotation lag speed", 0, 50, 10, "1/s", 1, "Camera lag"),
      Slider("Maximum lag distance", 0, 10, 2, "m", 2, "Camera lag"),
      // Camera effect sub-cards (§9). Motion blur, heat shimmer, glitch and chromatic aberration are their own effect rows already.
      Switch("Depth of field", false, "Depth of field"),
      Slider("Maximum blur radius", 0, 64, 16, "px", 0, "Depth of field"),
      Slider("Bokeh blades", 0, 12, 0, "", 0, "Depth of field"),
      Switch("Vignette", false, "Vignette"),
      Slider("Vignette intensity", 0, 1, 0.35, "", 2, "Vignette"),
      Slider("Vignette smoothness", 0.01, 1, 0.5, "", 2, "Vignette"),
      Slider("Vignette roundness", 0, 1, 1, "", 2, "Vignette"),
    ],
  },
};

// God rays (§7): shafts through clouds and fog, driven by the sun. Follow-up 46 makes it a component of the Sun (an outliner child
// with its own icon) instead of a world row after Local Fog.
export const GodRaysPanel = "god-rays";
export const GodRaysSheet = [
  { Label: "Enabled", Control: "Switch", Default: true, Group: "Shafts" },
  { Label: "Intensity", Control: "Slider", Minimum: 0, Maximum: 4, Default: 1, Unit: "×", Decimals: 2, Group: "Shafts" },
  { Label: "Anisotropy", Control: "Slider", Minimum: 0, Maximum: 0.95, Default: 0.76, Unit: "g", Decimals: 2, Group: "Shafts" },
  { Label: "Decay", Control: "Slider", Minimum: 0, Maximum: 1, Default: 0.95, Decimals: 2, Group: "Shafts" },
  { Label: "Density", Control: "Slider", Minimum: 0, Maximum: 3, Default: 1, Unit: "×", Decimals: 2, Group: "Shafts" },
  { Label: "Maximum distance", Control: "Slider", Minimum: 10, Maximum: 2000, Default: 400, Unit: "m", Decimals: 0, Group: "March" },
  { Label: "Steps", Control: "Slider", Minimum: 8, Maximum: 48, Default: 24, Decimals: 0, Group: "March" },
  { Label: "Cloud interaction", Control: "Switch", Default: true, Group: "Interaction" },
  { Label: "Fog interaction", Control: "Switch", Default: true, Group: "Interaction" },
  { Label: "Shaft tint", Control: "Colour", Default: "#fff1d6", Group: "Interaction" },
];
export const GodRaysRow = {
  Id: "god-rays",
  Name: "God Rays",
  Panel: GodRaysPanel,
  Icon: "god-rays",
  Parent: "sun",
  Description: "Sun component · volumetric light shafts that clouds and fog occlude",
};

// Applies the overlay to one native sheet. Native fields keep their order; added fields follow.
export function SpecifySheet(Panel, Sheet) {
  const Overlay = PanelSpecification[Panel];
  if (!Overlay || !Array.isArray(Sheet)) return Sheet;
  const Known = new Set(Sheet.map((Field) => Field.Label));
  const Corrected = Sheet.map((Field) => {
    const Range = Overlay.Ranges[Field.Label] || {};
    const Note = Overlay.Notes[Field.Label];
    const Next = { ...Field, ...Range, Section: Overlay.Section };
    if (Note) Next.SpecificationNote = Note;
    return Next;
  });
  return [...Corrected, ...Overlay.Added.filter((Field) => !Known.has(Field.Label)).map((Field) => ({ ...Field, Section: Overlay.Section }))];
}

export function SpecifyPanels(NativeSheets) {
  const Result = { ...NativeSheets, [GodRaysPanel]: GodRaysSheet.map((Field) => ({ ...Field, Section: "§7 Fog and God Rays" })) };
  for (const Panel of Object.keys(PanelSpecification)) if (Result[Panel] && Panel !== GodRaysPanel) Result[Panel] = SpecifySheet(Panel, Result[Panel]);
  return Result;
}

// Keeps God Rays as the Sun's component: scenes saved before Follow-up 48 gain the row, and scenes saved before Follow-up 46 (a world
// row after Local Fog, sun icon) have it moved under the Sun. Its values stay keyed by its Id, so settings carry over.
export function EnsureGodRays(Rows) {
  if (!Array.isArray(Rows)) return Rows;
  const Sun = Rows.find((Row) => Row.Id === "sun");
  const Existing = Rows.find((Row) => Row.Id === GodRaysRow.Id);
  if (!Sun) return Existing || !Rows.some((Row) => Row.Id === "world") ? Rows : [...Rows, { ...GodRaysRow, Parent: "world" }];
  if (Existing && Existing.Parent === Sun.Id && Existing.Icon === GodRaysRow.Icon) return Rows;
  const Moved = { ...GodRaysRow, ...(Existing || {}), Parent: Sun.Id, Icon: GodRaysRow.Icon, Description: GodRaysRow.Description };
  const Next = Rows.filter((Row) => Row.Id !== GodRaysRow.Id);
  let At = Next.findIndex((Row) => Row.Id === Sun.Id);
  while (At + 1 < Next.length && Next[At + 1].Parent === Sun.Id) At++;
  Next.splice(At + 1, 0, Moved);
  return Next;
}
