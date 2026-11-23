import React, { useEffect, useRef, useState } from "react";
import TransformPanel from "./TransformPanel.jsx";
import PrimitiveGlyph from "./PrimitiveIcons.jsx";
import { convexHull, inflateHull } from "./ConvexHull.js";
import { PrimitiveSurface } from "./WorkspaceGeometry.js";
import { CustomKey, CustomModeKey, CustomModes, ColliderAccept, importCollider } from "./ColliderImport.js";
import { OrbitMenu } from "./OrbitMenu.jsx";
import "./PrimitiveInspector.css";

// 🧩 Inspector for the Showcase primitives (Cube, Sphere, Cylinder, Torus, Cone): one category carousel —
//    Overview (info cards, then quick controls, then notes with the purpose) · Physics · Geometry (Transform first) · Material · Fracture.
// 📝 Every edit is a browser draft stored through Change(Key, Next) like the rest of the HTML editor; nothing reaches
//    the native scene. Quick controls and carousel pages share the same keys (PHYSICS, DYNAMIC, "Cast shadows", GI,
//    LOCKED), so a toggle in one place shows in the other.

const Shapes = {
  cube: {
    Name: "Cube",
    Collider: "Box",
    Purpose: "Reference cube for judging scale, flat-face lighting and hard shadow edges.",
    Reason: "Its six flat faces show how the key light, GI bounce and shadow softness read on planar surfaces.",
    Tags: ["reference", "lighting"],
    Parameters: [
      ["Size X", "Geometry Size X", 1, 0.01, 10, 0.01, "m"],
      ["Size Y", "Geometry Size Y", 1, 0.01, 10, 0.01, "m"],
      ["Size Z", "Geometry Size Z", 1, 0.01, 10, 0.01, "m"],
      ["Bevel", "Geometry Bevel", 0, 0, 0.25, 0.005, "m"],
      ["Bevel segments", "Geometry Bevel Segments", 1, 1, 8, 1, ""],
    ],
  },
  sphere: {
    Name: "Sphere",
    Collider: "Sphere",
    Purpose: "Smooth reference sphere for checking specular highlights, roughness and the terminator line.",
    Reason: "A curved surface shows the full range of light angles at once, so material and exposure changes are easy to judge.",
    Tags: ["reference", "material"],
    Parameters: [
      ["Radius", "Geometry Radius", 0.5, 0.01, 5, 0.01, "m"],
      ["Segments", "Geometry Segments", 32, 3, 128, 1, ""],
      ["Rings", "Geometry Rings", 16, 2, 64, 1, ""],
    ],
  },
  cylinder: {
    Name: "Cylinder",
    Collider: "Capsule",
    Purpose: "Upright cylinder for checking curved-to-flat transitions and contact shadows on the ground.",
    Reason: "Its caps and wall together test smoothing groups, ambient occlusion at the base and rolling physics.",
    Tags: ["reference", "physics"],
    Parameters: [
      ["Radius", "Geometry Radius", 0.5, 0.01, 5, 0.01, "m"],
      ["Height", "Geometry Height", 1, 0.01, 10, 0.01, "m"],
      ["Segments", "Geometry Segments", 32, 3, 128, 1, ""],
    ],
    Caps: true,
  },
  torus: {
    Name: "Torus",
    Collider: "Mesh",
    Purpose: "Ring that tests self-shadowing, the inner cavity's GI and concave collision.",
    Reason: "Light has to bounce into the hole, so it shows whether indirect light and occlusion behave on concave shapes.",
    Tags: ["reference", "GI"],
    Parameters: [
      ["Major radius", "Geometry Major Radius", 0.5, 0.05, 5, 0.01, "m"],
      ["Minor radius", "Geometry Minor Radius", 0.15, 0.01, 2, 0.005, "m"],
      ["Segments", "Geometry Segments", 48, 3, 192, 1, ""],
      ["Sides", "Geometry Sides", 16, 3, 64, 1, ""],
    ],
  },
  cone: {
    Name: "Cone",
    Collider: "Convex",
    Purpose: "Cone for checking a sharp apex, tapering silhouettes and pointed shadow tips.",
    Reason: "The narrowing surface shows normal interpolation and shadow-bias problems near the tip.",
    Tags: ["reference", "silhouette"],
    Parameters: [
      ["Radius", "Geometry Radius", 0.5, 0.01, 5, 0.01, "m"],
      ["Height", "Geometry Height", 1, 0.01, 10, 0.01, "m"],
      ["Segments", "Geometry Segments", 32, 3, 128, 1, ""],
    ],
    Caps: true,
  },
};

export const Densities = [
  ["Plastic", 950],
  ["Wood", 700],
  ["Rubber", 1100],
  ["Concrete", 2400],
  ["Glass", 2500],
  ["Aluminium", 2700],
  ["Steel", 7850],
];

// Mass, rigid body and collision sit in the object's component stack (Components page); each edit workspace has its own page.
const Categories = [
  ["overview", "Overview", "overview", "#7cb7ff"],
  ["components", "Components", "components", "#b59cff"],
  ["geometry", "Geometry", "geometry", "#5fe0a0"],
  ["material", "Material", "material", "#ff7eb6"],
  ["uv", "UV", "uv", "#7ab8ff"],
  ["fracture", "Fracture", "fracture", "#ff6b6b"],
  ["sdf", "SDF", "sdf", "#95c3ad"],
  ["lod", "LOD", "lod", "#edc181"],
];

//------------------------------------------------------------------------------------------------------------------------
//                                                   MEASUREMENT
//------------------------------------------------------------------------------------------------------------------------

export function ShapeOf(Subject) {
  const Key = String(Subject.Icon || "").replace(/^editor-/, "");
  return Shapes[Key] ? Key : Shapes[Subject.Id] ? Subject.Id : "cube";
}

const Fixed = (Amount, Digits = 2) =>
  Number.isFinite(Amount) ? Amount.toFixed(Digits) : "–";

// Bounds, volume, area and a closed-mesh vertex count (Euler: V = F / 2 + 2, or F / 2 for the genus-1 torus).
export function Measure(Shape, Read, Scale) {
  const P = (Key, Initial) => +Read(Key, Initial);
  const [Sx, Sy, Sz] = Scale.map((Axis) => Math.abs(+Axis || 0));
  let Bounds, Volume, Area, Triangles, Closed = true;
  if (Shape === "cube") {
    const X = P("Geometry Size X", 1), Y = P("Geometry Size Y", 1), Z = P("Geometry Size Z", 1);
    const Bevel = P("Geometry Bevel", 0), Steps = P("Geometry Bevel Segments", 1);
    Bounds = [X, Y, Z];
    Volume = X * Y * Z;
    Area = 2 * (X * Y + Y * Z + X * Z);
    Triangles = 12 + (Bevel > 0 ? 24 * Steps + 8 * Steps * Steps : 0);
  } else if (Shape === "sphere") {
    const R = P("Geometry Radius", 0.5), S = P("Geometry Segments", 32), Rings = P("Geometry Rings", 16);
    Bounds = [2 * R, 2 * R, 2 * R];
    Volume = (4 / 3) * Math.PI * R ** 3;
    Area = 4 * Math.PI * R * R;
    Triangles = 2 * S * (Rings - 1);
  } else if (Shape === "cylinder") {
    const R = P("Geometry Radius", 0.5), H = P("Geometry Height", 1), S = P("Geometry Segments", 32);
    const Caps = Read("Geometry Caps", true) !== false;
    Bounds = [2 * R, H, 2 * R];
    Volume = Math.PI * R * R * H;
    Area = 2 * Math.PI * R * H + (Caps ? 2 * Math.PI * R * R : 0);
    Triangles = 2 * S + (Caps ? 2 * (S - 2) : 0);
    Closed = Caps;
  } else if (Shape === "torus") {
    const Major = P("Geometry Major Radius", 0.5), Minor = P("Geometry Minor Radius", 0.15);
    const S = P("Geometry Segments", 48), Sides = P("Geometry Sides", 16);
    Bounds = [2 * (Major + Minor), 2 * Minor, 2 * (Major + Minor)];
    Volume = 2 * Math.PI ** 2 * Major * Minor * Minor;
    Area = 4 * Math.PI ** 2 * Major * Minor;
    Triangles = 2 * S * Sides;
  } else {
    const R = P("Geometry Radius", 0.5), H = P("Geometry Height", 1), S = P("Geometry Segments", 32);
    const Caps = Read("Geometry Caps", true) !== false;
    Bounds = [2 * R, H, 2 * R];
    Volume = (Math.PI * R * R * H) / 3;
    Area = Math.PI * R * Math.hypot(R, H) + (Caps ? Math.PI * R * R : 0);
    Triangles = S + (Caps ? S - 2 : 0);
    Closed = Caps;
  }
  const Vertices =
    Shape === "torus" ? Triangles / 2 : Shape === "cone" && !Closed ? P("Geometry Segments", 32) + 1
      : Shape === "cylinder" && !Closed ? 2 * P("Geometry Segments", 32) : Triangles / 2 + 2;
  return {
    Bounds: [Bounds[0] * Sx, Bounds[1] * Sy, Bounds[2] * Sz],
    Volume: Volume * Sx * Sy * Sz,
    Area: Area * Math.cbrt(Sx * Sy * Sz) ** 2,
    Triangles: Math.round(Triangles),
    Vertices: Math.round(Vertices),
    Closed,
  };
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    CONTROLS
//------------------------------------------------------------------------------------------------------------------------

function Slider({ Label, Reading, Assign, Minimum, Maximum, Step = 0.01, Unit = "", Digits = 2, Disabled, Tone }) {
  const Shown = Number.isFinite(+Reading) ? +Reading : Minimum;
  const Fill = ((Shown - Minimum) / (Maximum - Minimum)) * 100;
  return (
    <label className={"pi-slider" + (Disabled ? " is-disabled" : "")}>
      <span className="pi-slider-line">
        <span>{Label}</span>
        <span className="pi-number">
          <input
            type="number"
            min={Minimum}
            max={Maximum}
            step={Step}
            disabled={Disabled}
            aria-label={Label}
            value={+Shown.toFixed(Digits)}
            onChange={(Event) => Event.target.value !== "" && Assign(Math.min(Maximum, Math.max(Minimum, +Event.target.value)))}
          />
          {Unit && <small>{Unit}</small>}
        </span>
      </span>
      <input
        type="range"
        min={Minimum}
        max={Maximum}
        step={Step}
        disabled={Disabled}
        aria-label={Label + " slider"}
        value={Shown}
        style={{ "--pi-fill": Math.max(0, Math.min(100, Fill)) + "%", ...(Tone ? { "--pi-tone": Tone } : {}) }}
        onChange={(Event) => Assign(+Event.target.value)}
      />
    </label>
  );
}

export function Switch({ Label, Detail, On, Assign, Disabled, Tone }) {
  return (
    <button
      type="button"
      className={"pi-switch" + (On ? " on" : "")}
      role="switch"
      aria-checked={!!On}
      disabled={Disabled}
      onClick={() => Assign(!On)}
      style={Tone ? { "--pi-tone": Tone } : undefined}
    >
      <span className="pi-switch-text">
        <span>{Label}</span>
        {Detail && <small>{Detail}</small>}
      </span>
      <span className="pi-switch-track">
        <span />
      </span>
    </button>
  );
}

function Segmented({ Label, Choices, Current, Assign, Disabled }) {
  return (
    <div className={"pi-segmented-block" + (Disabled ? " is-disabled" : "")}>
      {Label && <span className="pi-caption">{Label}</span>}
      <div className="pi-segmented" role="radiogroup" aria-label={Label}>
        {Choices.map(([Caption, Key]) => (
          <button
            key={Key}
            type="button"
            role="radio"
            aria-checked={Current === Key}
            className={Current === Key ? "active" : ""}
            disabled={Disabled}
            onClick={() => Assign(Key)}
          >
            {Caption}
          </button>
        ))}
      </div>
    </div>
  );
}

function Card({ Title, Glyph, Tone, Aside, children, className = "" }) {
  return (
    <section className={"pi-card " + className} data-card={Title}>
      {(Title || Aside) && (
        <header className="pi-card-head">
          {Glyph && (
            <span className="pi-card-glyph" style={Tone ? { color: Tone } : undefined}>
              <PrimitiveGlyph Name={Glyph} Size={16} />
            </span>
          )}
          <h3>{Title}</h3>
          {Aside && <span className="pi-card-aside">{Aside}</span>}
        </header>
      )}
      {children}
    </section>
  );
}

function BigFigure({ Whole, Unit, Caption }) {
  const [Integer, Fraction] = String(Whole).split(".");
  return (
    <div className="pi-figure">
      <span className="pi-figure-number">
        {Integer}
        {Fraction !== undefined && <span className="pi-figure-fraction">.{Fraction}</span>}
      </span>
      {Unit && <small>{Unit}</small>}
      {Caption && <p>{Caption}</p>}
    </div>
  );
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  QUICK CONTROLS
//------------------------------------------------------------------------------------------------------------------------

const QuickSet = [
  ["Visible", "visible"],
  ["Dynamic", "dynamic"],
  ["Shadows", "shadow"],
  ["GI", "gi"],
  ["Physics", "physics"],
  ["Locked", "lock"],
];

function QuickGrid({ Values, Change, Hidden, ToggleHidden }) {
  const Switches = {
    Visible: [!Hidden, ToggleHidden],
    Dynamic: [!!Values.DYNAMIC, () => { const Next = !Values.DYNAMIC; Change("DYNAMIC", Next); if (!Next) Change("PHYSICS", false); }],
    Shadows: [Values["Cast shadows"] ?? true, () => Change("Cast shadows", !(Values["Cast shadows"] ?? true))],
    GI: [Values.GI ?? true, () => Change("GI", !(Values.GI ?? true))],
    Physics: [!!Values.PHYSICS, () => { const Next = !Values.PHYSICS; Change("PHYSICS", Next); if (Next) Change("DYNAMIC", true); }],
    Locked: [!!Values.LOCKED, () => Change("LOCKED", !Values.LOCKED)],
  };
  const Count = Object.values(Switches).filter(([On]) => On).length;
  return (
    <section className="pi-card pi-quick" data-card="Quick controls">
      <header className="pi-card-head">
        <h3>Quick controls</h3>
        <span className="pi-card-aside">
          <b>{Count}</b> / 6 on
        </span>
      </header>
      <div className="pi-quick-grid">
        {QuickSet.map(([Label, Glyph]) => {
          const [On, Toggle] = Switches[Label];
          return (
            <button
              key={Label}
              type="button"
              className={"pi-quick-tile" + (On ? " on" : "")}
              aria-pressed={!!On}
              aria-label={Label}
              data-quick-action={Label}
              onClick={Toggle}
            >
              <span className={"pi-status-icon " + (On ? "ok" : "idle")}>
                <PrimitiveGlyph Name={Glyph} Size={17} />
              </span>
              <span className="pi-quick-label">{Label}</span>
            </button>
          );
        })}
      </div>
      <p className="pi-footnote">
        {Values.DYNAMIC ? "Dynamic" : "Static"} · {Values.PHYSICS ? "physics on" : "physics off"}
        {Values.LOCKED ? " · transform locked" : ""}. Authored flags; native execution is pending.
      </p>
    </section>
  );
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     PAGES
//------------------------------------------------------------------------------------------------------------------------

function Checks({ Values, Hidden, Size }) {
  const Scale = Array.isArray(Values.Scale) ? Values.Scale : [1, 1, 1];
  const Rows = [];
  const Uniform = Scale.every((Axis) => Math.abs(Axis - Scale[0]) < 1e-6);
  Rows.push(Uniform ? ["check", "Uniform scale", "Collision and UVs stay true"] : ["warning", "Non-uniform scale", Scale.map((Axis) => Fixed(+Axis, 2)).join(" × ")]);
  if (Values.PHYSICS) {
    const Mass = +Values["Physics Mass Manual"] || 0;
    Rows.push(Values["Physics Mass Mode"] === "manual" && Mass < 0.05 ? ["warning", "Very light body", Fixed(Mass, 3) + " kg may jitter"] : ["check", "Physics ready", "Mass and collider set"]);
  }
  if (Hidden && (Values["Cast shadows"] ?? true)) Rows.push(["warning", "Hidden but casting shadows", "Turn Shadows off to remove it fully"]);
  if (!Size.Closed) Rows.push(["warning", "Open mesh", "No caps: volume and mass are estimates"]);
  if (Values.LOCKED) Rows.push(["check", "Transform locked", "Protected from accidental edits"]);
  if (Size.Triangles > 20000) Rows.push(["warning", "Dense mesh", Size.Triangles.toLocaleString("en-US") + " triangles"]);
  return Rows;
}

function OverviewPage({ Shape, Read, Change, Values, Hidden, ToggleHidden, Size, Components = null }) {
  const Profile = Shapes[Shape];
  const Tags = Array.isArray(Values.Tags) ? Values.Tags : Profile.Tags;
  const [Draft, EditDraft] = useState("");
  const Rows = Checks({ Values, Hidden, Size });
  const Passed = Rows.filter(([Verdict]) => Verdict === "check").length;
  return (
    <>
      <div className="pi-status-pair">
        <div className="pi-status">
          <span className="pi-status-icon ok"><PrimitiveGlyph Name="check" Size={12} /></span>
          <span className="pi-status-label">Checks passed</span>
          <span className="pi-status-number">{Passed}</span>
        </div>
        <div className="pi-status">
          <span className="pi-status-icon warn"><PrimitiveGlyph Name="warning" Size={12} /></span>
          <span className="pi-status-label">Warnings</span>
          <span className="pi-status-number">{Rows.length - Passed}</span>
        </div>
      </div>

      <Card Title="Footprint" Glyph="ruler" Tone="#5fe0a0" Aside="World size">
        <BigFigure Whole={Fixed(Size.Volume, 3)} Unit="m³" Caption="Volume" />
        <div className="pi-stat-grid">
          <div><span>Width</span><b>{Fixed(Size.Bounds[0])}<small> m</small></b></div>
          <div><span>Height</span><b>{Fixed(Size.Bounds[1])}<small> m</small></b></div>
          <div><span>Depth</span><b>{Fixed(Size.Bounds[2])}<small> m</small></b></div>
        </div>
        <p className="pi-footnote">Surface ≈ {Fixed(Size.Area)} m² · bounds include scale.</p>
      </Card>

      {Components}

      <Card Title="Checks" Glyph="check" Tone="#2fd27a" Aside={`${Passed} / ${Rows.length}`}>
        <ul className="pi-checks">
          {Rows.map(([Verdict, Title, Detail]) => (
            <li key={Title}>
              <span className={"pi-status-icon " + (Verdict === "check" ? "ok" : "warn")}>
                <PrimitiveGlyph Name={Verdict} Size={11} />
              </span>
              <span>
                <b>{Title}</b>
                <small>{Detail}</small>
              </span>
            </li>
          ))}
        </ul>
      </Card>

      <Card Title="Tags" Glyph="tag" Tone="#7cb7ff">
        <div className="pi-tags">
          {Tags.map((Tag) => (
            <span className="pi-tag" key={Tag}>
              {Tag}
              <button type="button" aria-label={"Remove tag " + Tag} onClick={() => Change("Tags", Tags.filter((Each) => Each !== Tag))}>
                <PrimitiveGlyph Name="close" Size={10} />
              </button>
            </span>
          ))}
          <form
            className="pi-tag-add"
            onSubmit={(Event) => {
              Event.preventDefault();
              const Next = Draft.trim();
              if (Next && !Tags.includes(Next)) Change("Tags", [...Tags, Next]);
              EditDraft("");
            }}
          >
            <input aria-label="New tag" placeholder="Add tag" value={Draft} onChange={(Event) => EditDraft(Event.target.value)} />
          </form>
        </div>
      </Card>

      <QuickGrid Values={Values} Change={Change} Hidden={Hidden} ToggleHidden={ToggleHidden} />
      <Card Title="Notes" Glyph="notes" Tone="#c9c9c9" Aside="Shared with the header" className="pi-notes-card">
        <div className="pi-purpose" data-card="Purpose">
          <span className="pi-notes-heading">
            <PrimitiveGlyph Name="purpose" Size={13} /> Purpose · {Profile.Name}
          </span>
          <label className="pi-text-block">
            <span>What it does</span>
            <textarea
              rows={3}
              aria-label="What it does"
              value={Read("Purpose", Profile.Purpose)}
              onChange={(Event) => Change("Purpose", Event.target.value)}
            />
          </label>
          <label className="pi-text-block">
            <span>Why it's here</span>
            <textarea
              rows={3}
              aria-label="Why it's here"
              value={Read("Reason", Profile.Reason)}
              onChange={(Event) => Change("Reason", Event.target.value)}
            />
          </label>
        </div>
        <label className="pi-text-block">
          <span>Notes</span>
          <textarea
            className="pi-notes"
            rows={3}
            aria-label="Notes"
            placeholder="Ownership, review notes, links…"
            value={Values.Notes || ""}
            onChange={(Event) => Change("Notes", Event.target.value)}
          />
        </label>
      </Card>
    </>
  );
}

function ShapeMark({ Shape, X, Y, Size, ...Rest }) {
  const H = Size / 2;
  if (Shape === "cube") return <rect x={X - H} y={Y - Size} width={Size} height={Size} rx="1.5" {...Rest} />;
  if (Shape === "cone") return <path d={`M${X - H} ${Y}L${X} ${Y - Size}L${X + H} ${Y}Z`} {...Rest} />;
  if (Shape === "cylinder") return <rect x={X - H * 0.8} y={Y - Size} width={Size * 0.8} height={Size} rx={H * 0.4} {...Rest} />;
  if (Shape === "torus")
    return <ellipse cx={X} cy={Y - H * 0.6} rx={H} ry={H * 0.6} fill="none" strokeWidth={H * 0.45} {...Rest} stroke={Rest.fill} />;
  return <circle cx={X} cy={Y - H} r={H} {...Rest} />;
}

// Everyday objects the balance compares against.
const Everyday = [
  ["Phone", 0.2],
  ["Brick", 2.5],
  ["Bowling ball", 7],
  ["Person", 75],
  ["Piano", 300],
  ["Car", 1500],
];


// Balance study: the primitive on one pan, the nearest everyday object (as many as it takes) on the other. The beam
// tips by the remaining difference, so the mass reads as "about four bricks" rather than a bare number.
const Silhouette = {
  cube: (X, Y, S) => <rect x={X - S / 2} y={Y - S} width={S} height={S} rx="2" />,
  sphere: (X, Y, S) => <circle cx={X} cy={Y - S / 2} r={S / 2} />,
  cylinder: (X, Y, S) => <rect x={X - S * 0.4} y={Y - S} width={S * 0.8} height={S} rx={S * 0.12} />,
  torus: (X, Y, S) => <ellipse cx={X} cy={Y - S * 0.28} rx={S * 0.55} ry={S * 0.28} fillRule="evenodd" />,
  cone: (X, Y, S) => <path d={`M${X - S / 2} ${Y}L${X} ${Y - S}L${X + S / 2} ${Y}Z`} />,
};
function BalanceStudy({ Mass, Shape, Unit = "kg" }) {
  // The reference and count (1–6) that come closest to balancing.
  const [Label, Kg, Count] = Everyday.flatMap(([Name, Each]) => [1, 2, 3, 4, 5, 6].map((Many) => [Name, Each, Many])).reduce((Best, Option) =>
    Math.abs(Math.log(Mass / (Option[1] * Option[2]))) < Math.abs(Math.log(Mass / (Best[1] * Best[2]))) ? Option : Best,
  );
  const Offset = Math.log10(Mass / (Count * Kg));
  const Tilt = Math.max(-14, Math.min(14, Offset * 60));
  const Pivot = [150, 46], Arm = 92;
  const Turn = (Tilt * Math.PI) / 180;
  const LeftEnd = [Pivot[0] - Arm * Math.cos(Turn), Pivot[1] + Arm * Math.sin(Turn)];
  const RightEnd = [Pivot[0] + Arm * Math.cos(Turn), Pivot[1] - Arm * Math.sin(Turn)];
  const Pan = ([X, Y], Content) => (
    <g>
      <line x1={X} x2={X - 22} y1={Y} y2={Y + 40} stroke="#6a6a6a" strokeWidth=".8" />
      <line x1={X} x2={X + 22} y1={Y} y2={Y + 40} stroke="#6a6a6a" strokeWidth=".8" />
      <path d={`M${X - 28} ${Y + 40}h56a28 6 0 0 1-56 0Z`} fill="#3a3a3a" />
      {Content(X, Y + 39)}
    </g>
  );
  const Shown = Count;
  return (
    <svg className="pi-balance" viewBox="0 0 300 140" role="img" aria-label="Mass balance" data-study="mass">
      <path d={`M${Pivot[0]} ${Pivot[1]}L${Pivot[0] - 12} 124H${Pivot[0] + 12}Z`} fill="#2a2a2a" />
      <rect x={Pivot[0] - 34} y="124" width="68" height="5" rx="2.5" fill="#2a2a2a" />
      <line x1={LeftEnd[0]} y1={LeftEnd[1]} x2={RightEnd[0]} y2={RightEnd[1]} stroke="#cfcfcf" strokeWidth="2.4" strokeLinecap="round" />
      <circle cx={Pivot[0]} cy={Pivot[1]} r="4" fill="#cfcfcf" />
      {Pan(LeftEnd, (X, Y) => (
        <g fill="#a9bfdc">{(Silhouette[Shape] || Silhouette.cube)(X, Y, 26)}</g>
      ))}
      {Pan(RightEnd, (X, Y) => (
        <g fill="#8a8a8a">
          {Array.from({ length: Shown }, (_, Index) => {
            const Column = Index % 3, Row = Math.floor(Index / 3);
            return <rect key={Index} x={X - 23 + Column * 16} y={Y - 13 - Row * 14} width="14" height="12" rx="3.5" />;
          })}
        </g>
      ))}
      <text x={LeftEnd[0]} y={LeftEnd[1] - 6} textAnchor="middle" className="pi-balance-strong">
        {MassText(Mass, Unit)}
      </text>
      <text x={RightEnd[0]} y={RightEnd[1] - 6} textAnchor="middle">
        {Count} × {Label.toLowerCase()}
      </text>
      <text x="150" y="139" textAnchor="middle">
        {Math.abs(Offset) < 0.03 ? "balanced" : Offset > 0 ? "a little heavier than the reference" : "a little lighter than the reference"}
      </text>
    </svg>
  );
}

// Spin resistance about the vertical axis for the solid shape (kg·m²).
function Inertia(Shape, Mass, Bounds, Read, Scale) {
  const R = Bounds[0] / 2;
  if (Shape === "cube") return (Mass * (Bounds[0] ** 2 + Bounds[2] ** 2)) / 12;
  if (Shape === "sphere") return 0.4 * Mass * R * R;
  if (Shape === "cylinder") return 0.5 * Mass * R * R;
  if (Shape === "cone") return 0.3 * Mass * R * R;
  const Major = +Read("Geometry Major Radius", 0.5) * Scale, Minor = +Read("Geometry Minor Radius", 0.15) * Scale;
  return Mass * (Major * Major + 0.75 * Minor * Minor);
}

// Mass units: id, kilograms per unit, decimals shown.
export const MassUnits = [
  ["g", 0.001],
  ["kg", 1],
  ["t", 1000],
  ["lb", 0.45359237],
  ["oz", 0.028349523125],
];
const UnitScale = (Unit) => (MassUnits.find(([Id]) => Id === Unit) || MassUnits[1])[1];
const MassDigits = (Amount) => (Amount >= 100 ? 0 : Amount >= 1 ? 2 : 3);
export const MassIn = (Kilograms, Unit) => Kilograms / UnitScale(Unit);
export const MassText = (Kilograms, Unit = "kg") => {
  const Amount = MassIn(Kilograms, Unit);
  return `${Fixed(Amount, MassDigits(Amount))} ${Unit}`;
};

// Mass entry: a number in the chosen unit, stored in kilograms.
function MassEntry({ Mass, Unit, Assign, Disabled }) {
  const Amount = MassIn(Mass, Unit);
  return (
    <label className={"pi-mass-entry" + (Disabled ? " is-disabled" : "")}>
      <span>Mass</span>
      <input
        type="number"
        min="0"
        step={10 ** -MassDigits(Amount)}
        aria-label="Mass amount"
        disabled={Disabled}
        value={+Amount.toFixed(MassDigits(Amount) + 1)}
        onChange={(Event) => Event.target.value !== "" && +Event.target.value > 0 && Assign(+Event.target.value * UnitScale(Unit))}
      />
      <b>{Unit}</b>
    </label>
  );
}

function MassCard({ Shape, Read, Change, Values, Size, Kit, Mass, Manual, Density, DensityKg, Gravity, Unit }) {
  const { Card: PropertyCard, Control, Metric } = Kit;
  const Scale = Array.isArray(Values.Scale) ? Values.Scale[0] : 1;
  const Spin = Inertia(Shape, Mass, Size.Bounds, Read, Scale);
  const Readout = (Label, Text) => <Control key={Label} Field={{ Label, Control: "Readout" }} Value={Text} />;
  const Amount = MassIn(Mass, Unit);
  return (
    <PropertyCard Title="Mass" Class="pi-mass-card">
      <Metric
        Value={Fixed(Amount, MassDigits(Amount))}
        Unit={Unit}
        Caption={Manual ? `Set directly · ${Fixed(Mass / Math.max(1e-9, Size.Volume), 0)} kg/m³ over ${Fixed(Size.Volume, 3)} m³` : `${Fixed(Size.Volume, 3)} m³ of ${Density.toLowerCase()} at ${DensityKg} kg/m³`}
      />
      <div className="graphic pi-balance-wrap">
        <BalanceStudy Mass={Mass} Shape={Shape} Unit={Unit} />
      </div>
      <Segmented
        Label="Mass from"
        Current={Manual ? "manual" : "auto"}
        Choices={[["Material", "auto"], ["Set directly", "manual"]]}
        Assign={(Next) => {
          if (Next === "manual") Change("Physics Mass Manual", +Mass.toFixed(4));
          Change("Physics Mass Mode", Next);
        }}
      />
      {Manual ? (
        <MassEntry Mass={Mass} Unit={Unit} Assign={(Next) => Change("Physics Mass Manual", Next)} />
      ) : (
        <Control
          Field={{ Label: "Material", Control: "Select", Options: Densities.map(([Name, Kg]) => `${Name} · ${Kg} kg/m³`) }}
          Value={Math.max(0, Densities.findIndex(([Name]) => Name === Density))}
          Change={(Next) => Change("Physics Density", Densities[Next][0])}
        />
      )}
      <div className="pi-unit-pills" role="radiogroup" aria-label="Mass unit">
        {MassUnits.map(([Id]) => (
          <button key={Id} type="button" role="radio" aria-checked={Id === Unit} onClick={() => Change("Physics Mass Unit", Id)}>
            {Id}
          </button>
        ))}
      </div>
      {Readout("Volume", Fixed(Size.Volume, 3) + " m³")}
      {Readout("Weight", Fixed(Mass * 9.81 * Gravity, 1) + " N")}
      {Readout("Spin resistance", (Spin < 0.01 ? Spin.toExponential(2) : Fixed(Spin, 3)) + " kg·m²")}
    </PropertyCard>
  );
}

// Mass from the density table and the measured volume, unless it is set directly. Stored in kilograms; Unit is how it is shown.
export function MassOf(Read, Size) {
  const Density = Read("Physics Density", "Plastic");
  const DensityKg = (Densities.find(([Name]) => Name === Density) || Densities[0])[1];
  const Manual = Read("Physics Mass Mode", "auto") === "manual";
  const Mass = Manual ? +Read("Physics Mass Manual", 1) : DensityKg * Size.Volume;
  const Unit = MassUnits.some(([Id]) => Id === Read("Physics Mass Unit", "kg")) ? Read("Physics Mass Unit", "kg") : "kg";
  return { Density, DensityKg, Manual, Mass, Unit };
}

// Body types as tiles: what the physics world does with the object. Static is also what an unsimulated object is.
const BodyTypes = [
  ["static", "Static", "Never moves. Others collide with it."],
  ["dynamic", "Dynamic", "Gravity, contacts and forces move it."],
  ["kinematic", "Kinematic", "Animation moves it. It pushes others."],
];
function BodyGlyph({ Type }) {
  return (
    <svg viewBox="0 0 48 36" aria-hidden="true" className="pi-body-glyph">
      <path d="M4 31H44" stroke="currentColor" strokeOpacity=".45" strokeWidth="1.4" strokeLinecap="round" />
      {Type === "static" && (
        <>
          <rect x="15" y="15" width="18" height="16" rx="2.5" fill="currentColor" fillOpacity=".85" />
          <path d="M8 35l4-4M15 35l4-4M22 35l4-4M29 35l4-4M36 35l4-4" stroke="currentColor" strokeOpacity=".4" strokeWidth="1.2" strokeLinecap="round" />
        </>
      )}
      {Type === "dynamic" && (
        <>
          <rect x="15" y="4" width="18" height="16" rx="2.5" fill="currentColor" fillOpacity=".85" transform="rotate(12 24 12)" />
          <path d="M24 23v6M21 26.5l3 3 3-3" stroke="currentColor" strokeWidth="1.6" fill="none" strokeLinecap="round" strokeLinejoin="round" />
          <path d="M11 6v8M37 8v8" stroke="currentColor" strokeOpacity=".4" strokeWidth="1.2" strokeLinecap="round" />
        </>
      )}
      {Type === "kinematic" && (
        <>
          <path d="M6 24C14 8 30 8 42 18" stroke="currentColor" strokeOpacity=".5" strokeWidth="1.3" strokeDasharray="2.5 3" fill="none" />
          <rect x="28" y="6" width="14" height="12" rx="2" fill="currentColor" fillOpacity=".85" />
          <circle cx="6" cy="24" r="2" fill="currentColor" fillOpacity=".6" />
        </>
      )}
    </svg>
  );
}

// Friction as the steepest slope the body rests on: it starts sliding above atan(friction).
function SlopeStudy({ Friction, Shape }) {
  const Angle = Math.atan(Math.max(0, Friction)), Degrees = (Angle * 180) / Math.PI;
  const Foot = [16, 84], Run = 118, Rise = Math.min(70, Math.tan(Angle) * Run);
  const Top = [Foot[0] + Run, Foot[1] - Rise], Slope = Math.atan2(Rise, Run);
  const Mid = [Foot[0] + Run * 0.55, Foot[1] - Rise * 0.55];
  const Arc = 26;
  return (
    <svg className="pi-mini-study" viewBox="0 0 150 106" role="img" aria-label="Friction slope study" data-study="slope">
      <path d={`M${Foot[0]} ${Foot[1]}L${Top[0]} ${Top[1]}L${Top[0]} ${Foot[1]}Z`} fill="#232323" stroke="#4a4a4a" />
      <path d={`M${Foot[0] + Arc} ${Foot[1]}A${Arc} ${Arc} 0 0 0 ${Foot[0] + Arc * Math.cos(Slope)} ${Foot[1] - Arc * Math.sin(Slope)}`} stroke="#e6cf72" fill="none" strokeWidth="1.4" />
      <g transform={`translate(${Mid[0]} ${Mid[1]}) rotate(${(-Slope * 180) / Math.PI})`}>
        <g fill="#cfcfcf">{(Silhouette[Shape] || Silhouette.cube)(0, -1, 16)}</g>
      </g>
      <text x={Foot[0] + Arc + 4} y={Foot[1] - 4} className="pi-mini-strong">{Degrees.toFixed(0)}°</text>
      <text x="8" y="102">slides above {Degrees.toFixed(0)}°</text>
    </svg>
  );
}

// Bounciness as a ladder of rebound heights from a 1 m drop: each rebound is restitution² of the one before.
function BounceLadder({ Restitution }) {
  const E2 = Math.max(0, Math.min(1, Restitution)) ** 2;
  const Heights = Array.from({ length: 6 }, (_, Index) => E2 ** (Index + 1));
  const Rests = Heights.findIndex((Height) => Height < 0.01);
  return (
    <svg className="pi-mini-study" viewBox="0 0 150 106" role="img" aria-label="Bounce ladder" data-study="bounce">
      <path d="M8 84H142" stroke="#4a4a4a" />
      <rect x="10" y="14" width="14" height="70" rx="3" fill="none" stroke="#a9bfdc" strokeOpacity=".5" strokeDasharray="3 3" />
      {Heights.map((Height, Index) => (
        <rect key={Index} x={30 + Index * 19} y={84 - Math.max(1.5, Height * 70)} width="14" height={Math.max(1.5, Height * 70)} rx="3" fill="#a9bfdc" fillOpacity={0.9 - Index * 0.1} />
      ))}
      <text x="10" y="10">drop 1 m</text>
      <text x="8" y="102">{E2 < 0.0001 ? "no rebound" : `rebounds ${Fixed(Heights[0] * 100, 0)} cm`}</text>
    </svg>
  );
}

// Damping as speed decay with no contacts: linear speed and spin each fall by e^(-damping · t).
function DecayStudy({ Linear, Angular }) {
  const Seconds = 10, Left = 30, Right = 290, Top = 12, Bottom = 92;
  const Px = (Time) => Left + (Time / Seconds) * (Right - Left), Py = (Ratio) => Bottom - Ratio * (Bottom - Top);
  const Curve = (Rate) => Array.from({ length: 61 }, (_, Index) => { const T = (Index / 60) * Seconds; return `${Px(T).toFixed(1)},${Py(Math.exp(-Rate * T)).toFixed(1)}`; }).join(" ");
  const Half = (Rate) => (Rate > 0 ? Math.LN2 / Rate : Infinity);
  return (
    <svg className="pi-study-svg" viewBox="0 0 300 112" role="img" aria-label="Damping decay" data-study="decay">
      <path d={`M${Left} ${Py(0.5)}H${Right}`} stroke="#ffffff22" strokeDasharray="3 4" />
      <path d={`M${Left} ${Top}V${Bottom}H${Right}`} stroke="#4a4a4a" fill="none" />
      <text x={Left - 4} y={Top + 4} textAnchor="end">100</text>
      <text x={Left - 4} y={Py(0.5) + 4} textAnchor="end">50</text>
      <text x={Left - 4} y={Bottom + 4} textAnchor="end">0 %</text>
      <text x={Right} y={Bottom + 14} textAnchor="end">{Seconds} s</text>
      {[[Linear, "#f2f2f2", "speed"], [Angular, "#ea6b45", "spin"]].map(([Rate, Tone, Label]) => (
        <g key={Label}>
          <polyline points={Curve(Rate)} fill="none" stroke={Tone} strokeWidth="1.6" />
          {Half(Rate) <= Seconds && <circle cx={Px(Half(Rate))} cy={Py(0.5)} r="3" fill={Tone} />}
        </g>
      ))}
      <g transform={`translate(${Left + 8} ${Bottom + 14})`}>
        <circle cx="0" cy="-3.5" r="3" fill="#f2f2f2" />
        <text x="7" y="0">speed</text>
        <circle cx="52" cy="-3.5" r="3" fill="#ea6b45" />
        <text x="59" y="0">spin</text>
      </g>
    </svg>
  );
}
const HalfLife = (Rate) => (Rate > 0 ? Fixed(Math.LN2 / Rate, Math.LN2 / Rate < 10 ? 1 : 0) + " s" : "never");

function RigidBodyCards({ Shape, Read, Change, Values }) {
  const Simulate = !!Values.PHYSICS;
  const Body = !Simulate || !Values.DYNAMIC ? "static" : Read("Physics Body", "dynamic");
  const Moving = Body === "dynamic";
  const Gravity = +Read("Physics Gravity Scale", 1);
  const Linear = +Read("Physics Linear Damping", 0.05);
  const Angular = +Read("Physics Angular Damping", 0.1);
  const Friction = +Read("Physics Friction", 0.6);
  const Restitution = +Read("Physics Bounciness", 0.2);
  const G = 9.81 * Gravity;
  const Choose = (Next) => {
    if (Next === "static") { Change("PHYSICS", false); Change("DYNAMIC", false); return; }
    Change("PHYSICS", true);
    Change("DYNAMIC", true);
    Change("Physics Body", Next);
  };
  return (
    <>
      <Card Title="Body" Glyph="physics-tab" Tone="#ff9f5a" Aside={Simulate ? "Simulated" : "Not simulated"} className="pi-body-card">
        <div className="pi-body-tiles" role="radiogroup" aria-label="Body type">
          {BodyTypes.map(([Id, Label, Line]) => (
            <button key={Id} type="button" role="radio" aria-checked={Body === Id} data-body={Id} onClick={() => Choose(Id)}>
              <BodyGlyph Type={Id} />
              <b>{Label}</b>
              <small>{Line}</small>
            </button>
          ))}
        </div>
        {Moving ? (
          <>
            <div className="pi-body-facts">
              <div><span>Gravity</span><b>{Fixed(G, 1)}<small> m/s²</small></b></div>
              <div><span>Falls 1 m in</span><b>{G > 0 ? Fixed(Math.sqrt(2 / G), 2) : "–"}<small> s</small></b></div>
              <div><span>Top speed</span><b>{G > 0 && Linear > 0 ? Fixed(G / Linear, 0) : "–"}<small> m/s</small></b></div>
            </div>
            <Slider Label="Gravity scale" Reading={Gravity} Minimum={-2} Maximum={4} Step={0.05} Unit="×" Tone="#ff9f5a" Assign={(Next) => Change("Physics Gravity Scale", Next)} />
            <Switch Label="Start asleep" Detail="Wakes on first contact" On={Read("Physics Asleep", false)} Assign={(Next) => Change("Physics Asleep", Next)} />
          </>
        ) : (
          <p className="pi-study-note">{Body === "static" ? "Static bodies cost nothing to simulate; contact and collision still apply." : "Kinematic bodies follow animation or script exactly; gravity and contacts never move them."}</p>
        )}
      </Card>

      <Card Title="Contact" Glyph="physics" Tone="#ffc93d" Aside="When it touches" className="pi-contact-card">
        <div className="pi-contact-grid">
          <div className="pi-contact-panel" data-contact="friction">
            <SlopeStudy Friction={Friction} Shape={Shape} />
            <Slider Label="Friction" Reading={Friction} Minimum={0} Maximum={1.5} Step={0.01} Tone="#e6cf72" Assign={(Next) => Change("Physics Friction", Next)} />
          </div>
          <div className="pi-contact-panel" data-contact="bounce">
            <BounceLadder Restitution={Restitution} />
            <Slider Label="Bounce" Reading={Restitution} Minimum={0} Maximum={1} Step={0.01} Tone="#a9bfdc" Assign={(Next) => Change("Physics Bounciness", Next)} />
          </div>
        </div>
      </Card>

      <Card Title="Damping" Glyph="dynamic" Tone="#ea6b45" Aside={Moving ? "No contacts" : "Dynamic only"} className={"pi-damping-card" + (Moving ? "" : " is-muted")}>
        <BigFigure Whole={HalfLife(Linear).replace(" s", "")} Unit={Linear > 0 ? "s" : ""} Caption={`Speed halves after ${HalfLife(Linear)} · spin halves after ${HalfLife(Angular)}`} />
        <div className="pi-study">
          <DecayStudy Linear={Linear} Angular={Angular} />
        </div>
        <Slider Label="Linear damping" Reading={Linear} Minimum={0} Maximum={1} Step={0.01} Disabled={!Moving} Tone="#f2f2f2" Assign={(Next) => Change("Physics Linear Damping", Next)} />
        <Slider Label="Angular damping" Reading={Angular} Minimum={0} Maximum={1} Step={0.01} Disabled={!Moving} Tone="#ea6b45" Assign={(Next) => Change("Physics Angular Damping", Next)} />
      </Card>
    </>
  );
}

// Collision proxy: what the physics world tests against. Automatic picks the cheapest proxy that hugs the shape;
// SDF uses the object's distance-field bake.
export const CollisionShapes = [
  ["auto", "Automatic"],
  ["box", "Box"],
  ["sphere", "Sphere"],
  ["capsule", "Capsule"],
  ["convex", "Convex hull"],
  ["triangles", "Triangles (exact surface)"],
  ["sdf", "Distance field (SDF)"],
  ["custom", "Custom (imported)"],
];
// Concave shapes the physics can only use on bodies that don't move; a dynamic body falls back to the convex hull.
export const StaticOnlyProxies = ["triangles", "sdf"];
const AutomaticProxy = { cube: "box", sphere: "sphere", cylinder: "convex", cone: "convex", torus: "sdf" };
export function CollisionProxy(Shape, Read) {
  const Chosen = Read("Collision Shape", "auto");
  return Chosen === "auto" ? AutomaticProxy[Shape] || "convex" : Chosen;
}
// Exact holds measured volumes where they exist: Convex (hull of the surface, grown by the margin) and Custom (the import).
function ProxyVolume(Proxy, Shape, Size, Read, Exact = {}) {
  const [X, Y, Z] = Size.Bounds, Margin = +Read("Collision Margin", 0.01);
  const [Gx, Gy, Gz] = [X + 2 * Margin, Y + 2 * Margin, Z + 2 * Margin];
  if (Proxy === "box") return Gx * Gy * Gz;
  if (Proxy === "sphere") return (4 / 3) * Math.PI * (Math.max(Gx, Gy, Gz) / 2) ** 3;
  if (Proxy === "capsule") {
    const R = Math.max(Gx, Gz) / 2, Straight = Math.max(0, Gy - 2 * R);
    return Math.PI * R * R * Straight + (4 / 3) * Math.PI * R ** 3;
  }
  if (Proxy === "convex" && Exact.Convex) return Exact.Convex;
  if (Proxy === "triangles") return Size.Volume;
  if (Proxy === "custom") return Exact.Custom || Size.Volume;
  if (Proxy === "convex") return Shape === "torus" ? Math.PI * (Gx / 2) ** 2 * Gy : Size.Volume * (Gx * Gy * Gz) / Math.max(1e-9, X * Y * Z);
  return Size.Volume * (Gx * Gy * Gz) / Math.max(1e-9, X * Y * Z);
}
// Silhouette of a point set seen from the front (x right, y up): its 2D convex hull, for the custom collider drawing.
function FrontOutline(Positions) {
  const Points = [];
  for (let Index = 0; Index < Positions.length; Index += 3) Points.push([Positions[Index], Positions[Index + 1]]);
  Points.sort((A, B) => A[0] - B[0] || A[1] - B[1]);
  const Turn = (O, A, B) => (A[0] - O[0]) * (B[1] - O[1]) - (A[1] - O[1]) * (B[0] - O[0]);
  const Lower = [], Upper = [];
  for (const Point of Points) {
    while (Lower.length >= 2 && Turn(Lower[Lower.length - 2], Lower[Lower.length - 1], Point) <= 0) Lower.pop();
    Lower.push(Point);
  }
  for (const Point of [...Points].reverse()) {
    while (Upper.length >= 2 && Turn(Upper[Upper.length - 2], Upper[Upper.length - 1], Point) <= 0) Upper.pop();
    Upper.push(Point);
  }
  return [...Lower.slice(0, -1), ...Upper.slice(0, -1)];
}
function ProxyStudy({ Shape, Proxy, Size, Custom = null }) {
  const [X, Y, Z] = Size.Bounds, Span = Math.max(X, Y, Z, 1e-6), Scale = 96 / Span;
  const W = Math.max(X, Z) * Scale, H = Y * Scale, Cx = 150, Base = 128;
  const Outline = { fill: "none", stroke: "#7cb7ff", strokeWidth: 1.6, strokeDasharray: "5 4" };
  const R = Math.max(W, H) / 2;
  return (
    <svg className="pi-study-svg" viewBox="0 0 300 150" role="img" aria-label={`${Proxy} collision proxy`} data-study="collision">
      <line x1="30" x2="270" y1={Base} y2={Base} stroke="#3a3a3a" />
      <ShapeMark Shape={Shape} X={Cx} Y={Base} Size={Math.max(W, H)} fill="#3a3a3a" stroke="#8a8a8a" />
      {Proxy === "box" && <rect x={Cx - W / 2 - 4} y={Base - H - 4} width={W + 8} height={H + 8} rx="3" {...Outline} />}
      {Proxy === "sphere" && <circle cx={Cx} cy={Base - H / 2} r={R + 4} {...Outline} />}
      {Proxy === "capsule" && <rect x={Cx - W / 2 - 4} y={Base - H - 4} width={W + 8} height={H + 8} rx={W / 2 + 4} {...Outline} />}
      {Proxy === "convex" && (Shape === "cone" ? <path d={`M${Cx - W / 2 - 5} ${Base + 2}L${Cx} ${Base - H - 6}L${Cx + W / 2 + 5} ${Base + 2}Z`} {...Outline} /> : <rect x={Cx - W / 2 - 4} y={Base - H - 4} width={W + 8} height={H + 8} rx={Math.min(W, H) * 0.3} {...Outline} />)}
      {Proxy === "triangles" && <ShapeMark Shape={Shape} X={Cx} Y={Base} Size={Math.max(W, H)} fill="none" stroke="#7cb7ff" strokeWidth={1.6} />}
      {Proxy === "custom" && Custom?.Positions?.length > 8 && (
        <polygon points={FrontOutline(Custom.Positions).map(([Px, Py]) => `${(Cx + Px * Scale).toFixed(1)},${(Base - H / 2 - Py * Scale).toFixed(1)}`).join(" ")} {...Outline} strokeDasharray="2 3" data-custom-outline="" />
      )}
      {Proxy === "sdf" && [6, 12, 18].map((Grow, Index) => <ShapeMark key={Grow} Shape={Shape} X={Cx} Y={Base + Grow / 2} Size={Math.max(W, H) + Grow} fill="none" stroke="#7cb7ff" strokeOpacity={0.7 - Index * 0.22} strokeDasharray="3 3" />)}
      <text x="30" y="20">{CollisionShapes.find(([Id]) => Id === Proxy)?.[1]}</text>
    </svg>
  );
}

function CollisionCards({ Shape, Read, Change, Values, Size, Kit }) {
  const { Control, Metric } = Kit;
  const Proxy = CollisionProxy(Shape, Read);
  const Margin = +Read("Collision Margin", 0.01);
  const Scale = Array.isArray(Values.Scale) ? Values.Scale.map((Each) => Math.abs(+Each) || 1) : [1, 1, 1];
  const SurfaceKey = JSON.stringify([Shape, Scale, Margin, ["Geometry Size X", "Geometry Size Y", "Geometry Size Z", "Geometry Radius", "Geometry Height", "Geometry Segments", "Geometry Rings", "Geometry Major Radius", "Geometry Minor Radius", "Geometry Sides"].map((Key) => Values[Key])]);
  const Hull = React.useMemo(() => (Proxy === "convex" ? inflateHull(convexHull(PrimitiveSurface(Shape, Read, Scale).Positions), Margin) : null), [Proxy === "convex" && SurfaceKey]);
  const Custom = Values[CustomKey] || null;
  const CustomMode = Math.max(0, Math.min(1, +Read(CustomModeKey, 0) || 0));
  const [Importing, SetImporting] = useState("");
  const Picker = useRef(null);
  const Volume = ProxyVolume(Proxy, Shape, Size, Read, { Convex: Hull?.Volume, Custom: Custom ? (CustomMode === 1 ? Custom.HullVolume : Custom.Volume) : 0 });
  const Load = async (File_) => {
    if (!File_) return;
    SetImporting(`Reading ${File_.name}…`);
    try {
      const Record = await importCollider(File_);
      Change(CustomKey, Record);
      Change("Collision Shape", "custom");
      SetImporting(`Imported ${Record.Name}: ${Record.Triangles.toLocaleString("en-US")} triangles${Record.Closed ? ", closed" : ", open (uses its hull volume)"}.`);
    } catch (Problem) {
      SetImporting(String(Problem?.message || Problem));
    }
  };
  const Fit = Size.Volume / Math.max(1e-9, Volume);
  const Sdf = Values["SDF Bake"]?.Baked;
  const Simulated = !!Values.PHYSICS;
  const Layers = ["Default", "Static world", "Dynamic props", "Characters", "Triggers only"];
  return (
    <>
      <Card Title="Collision shape" Glyph="physics" Tone="#7cb7ff" Aside={Read("Physics Collides", true) ? "Collides" : "Ignored"} className="pi-study-card pi-collision-card">
        <BigFigure Whole={Fixed(Fit * 100, 0)} Unit="% fit" Caption={`Object volume over proxy volume · ${Fixed(Volume, 3)} m³ proxy`} />
        <div className="pi-study">
          <ProxyStudy Shape={Shape} Proxy={Proxy} Size={Size} Custom={Custom} />
        </div>
        <Control
          Field={{ Label: "Shape", Control: "Select", Options: CollisionShapes.map(([, Label]) => Label) }}
          Value={Math.max(0, CollisionShapes.findIndex(([Id]) => Id === Read("Collision Shape", "auto")))}
          Change={(Index) => Change("Collision Shape", CollisionShapes[Index][0])}
        />
        <Control Field={{ Label: "Margin", Minimum: 0, Maximum: 0.1, Decimals: 3, Unit: "m", Default: 0.01 }} Value={+Read("Collision Margin", 0.01)} Change={(Next) => Change("Collision Margin", Next)} />
        {Proxy === "sdf" && <p className="pi-footnote">{Sdf ? `Uses the SDF bake (${Sdf.Resolution}³, ${Sdf.Bricks} bricks).` : "Needs an SDF bake: open the object's SDF page and bake it."}</p>}
        {Shape === "torus" && Proxy === "convex" && <p className="pi-footnote">A convex hull fills the torus hole; use triangles or the distance field to keep it open.</p>}
        {Proxy === "convex" && Hull && <p className="pi-footnote" data-hull="">Hull of the surface: {Hull.Points} points, {Hull.Faces} faces.</p>}
        {StaticOnlyProxies.includes(Proxy) && Simulated && Values.DYNAMIC && Read("Physics Body", "dynamic") === "dynamic" && (
          <p className="pi-footnote pi-warning" data-static-only="">⚠️ Concave colliders only work on bodies that don't move. While the body is dynamic, physics uses its convex hull.</p>
        )}
        {Proxy === "custom" && (
          <div className="pi-custom-collider" data-custom={Custom ? "loaded" : "empty"}>
            <input ref={Picker} type="file" accept={ColliderAccept} hidden aria-label="Collider file" onChange={(Event) => Load(Event.target.files?.[0])} />
            {Custom ? (
              <div className="pi-custom-record">
                <b>{Custom.Name}</b>
                <small>
                  {Custom.Format} · {Custom.Triangles.toLocaleString("en-US")} triangles · {Custom.Closed ? "closed" : "open"} · {Fixed(CustomMode === 1 ? Custom.HullVolume : Custom.Volume, 3)} m³
                </small>
              </div>
            ) : (
              <p className="pi-footnote">No collider imported yet. OBJ, STL, PLY, glTF / GLB or .facet, in the object's own space and units (metres).</p>
            )}
            {Custom && <Control Field={{ Label: "Use", Control: "Select", Options: CustomModes }} Value={CustomMode} Change={(Index) => Change(CustomModeKey, Index)} />}
            <div className="pi-custom-actions">
              <button type="button" onClick={() => Picker.current?.click()}>{Custom ? "Replace collider…" : "Import collider…"}</button>
              {Custom && <button type="button" onClick={() => Change(CustomKey, null)}>Remove</button>}
            </div>
            {Importing && <p className="pi-footnote" role="status">{Importing}</p>}
          </div>
        )}
      </Card>
      <Card Title="Filtering" Glyph="lock" Tone="#c9c9c9" className="pi-filter-card">
        <Switch Label="Collides" Detail="Other bodies bump into it" On={Read("Physics Collides", true)} Assign={(Next) => Change("Physics Collides", Next)} />
        <Switch Label="Continuous collision" Detail="Stops fast bodies tunnelling through thin walls" On={Read("Physics CCD", false)} Disabled={!Simulated} Assign={(Next) => Change("Physics CCD", Next)} />
        <Switch Label="Overlap events" Detail="Reports when something enters it" On={Read("Collision Overlaps", false)} Assign={(Next) => Change("Collision Overlaps", Next)} />
        <Control Field={{ Label: "Layer", Control: "Select", Options: Layers }} Value={Math.max(0, Layers.indexOf(Read("Collision Layer", "Default")))} Change={(Index) => Change("Collision Layer", Layers[Index])} />
      </Card>
    </>
  );
}

// Cards for one of the object's components (Mass, Rigid Body, Collision). Owner is the object row; values are the owner's.
export function ObjectComponentCards({ Component, Owner, Values, Change, Kit }) {
  const Shape = ShapeOf(Owner);
  const Read = (Key, Initial) => (Values[Key] === undefined ? Initial : Values[Key]);
  const Size = Measure(Shape, Read, Array.isArray(Values.Scale) ? Values.Scale : [1, 1, 1]);
  const Shared = { Shape, Read, Change, Values, Size, Kit };
  const Gravity = +Read("Physics Gravity Scale", 1);
  return (
    <div className="primitive-inspector pi-component" data-component={Component}>
      {Component === "Mass" ? <MassCard {...Shared} {...MassOf(Read, Size)} Gravity={Gravity} /> : Component === "Rigid Body" ? <RigidBodyCards {...Shared} /> : <CollisionCards {...Shared} />}
    </div>
  );
}

// Shading study. The primitive's cross-section (segments around the axis, or the torus tube sides, or the bevelled
// cube square) is drawn twice: from above with its normals, and as a lit low-poly band. Flat gives one normal per
// face, Smooth averages normals at every vertex, Auto averages only where the corner is below the smoothing angle.
function CrossSection(Shape, Read) {
  if (Shape === "cube") {
    const Bevel = Math.min(0.24, +Read("Geometry Bevel", 0)), Steps = Bevel > 0 ? Math.round(+Read("Geometry Bevel Segments", 1)) : 0;
    const Points = [], Half = 0.5, Round = Bevel * 2;
    for (const [Cx, Cy, Start] of [[1, 1, 0], [-1, 1, 90], [-1, -1, 180], [1, -1, 270]]) {
      if (!Steps) { Points.push([Cx * Half, Cy * Half]); continue; }
      for (let Index = 0; Index <= Steps; Index++) {
        const A = ((Start + (90 * Index) / Steps) * Math.PI) / 180;
        Points.push([Cx * (Half - Round / 2) + (Round / 2) * Math.cos(A), Cy * (Half - Round / 2) + (Round / 2) * Math.sin(A)]);
      }
    }
    const Turn = Math.PI / 6;
    return Points.map(([X, Y]) => [X * Math.cos(Turn) - Y * Math.sin(Turn), X * Math.sin(Turn) + Y * Math.cos(Turn)]).map(([X, Y]) => [X * 1.3, Y * 1.3]);
  }
  const Count = Math.max(3, Math.min(64, Math.round(+Read(Shape === "torus" ? "Geometry Sides" : "Geometry Segments", Shape === "torus" ? 16 : Shape === "sphere" ? 32 : 32))));
  return Array.from({ length: Count }, (_, Index) => {
    const A = (Index / Count) * Math.PI * 2 + Math.PI / Count + Math.PI / 2;
    return [0.62 * Math.cos(A), 0.62 * Math.sin(A)];
  });
}

const Normalise = ([X, Y]) => { const L = Math.hypot(X, Y) || 1; return [X / L, Y / L]; };
const LightDirection = Normalise([-0.65, -0.75]);

function ShadeProfile(Points, Mode, Threshold, Flip) {
  const Count = Points.length;
  const Faces = Points.map((A, Index) => {
    const B = Points[(Index + 1) % Count];
    const N = Normalise([B[1] - A[1], -(B[0] - A[0])]);
    const Mid = [(A[0] + B[0]) / 2, (A[1] + B[1]) / 2];
    const Outward = N[0] * Mid[0] + N[1] * Mid[1] >= 0 ? N : [-N[0], -N[1]];
    return { A, B, Normal: Flip ? [-Outward[0], -Outward[1]] : Outward };
  });
  // Vertex Index joins face Index − 1 and face Index.
  const Corners = Points.map((Point, Index) => {
    const Before = Faces[(Index - 1 + Count) % Count].Normal, After = Faces[Index].Normal;
    const Angle = (Math.acos(Math.max(-1, Math.min(1, Before[0] * After[0] + Before[1] * After[1]))) * 180) / Math.PI;
    const Hard = Mode === "flat" || (Mode === "auto" && Angle > Threshold);
    const Shared = Normalise([Before[0] + After[0], Before[1] + After[1]]);
    return { Point, Angle, Hard, Before, After, Shared };
  });
  const Ends = Faces.map((Face, Index) => {
    const Start = Corners[Index], Finish = Corners[(Index + 1) % Count];
    return [Start.Hard ? Face.Normal : Start.Shared, Finish.Hard ? Face.Normal : Finish.Shared];
  });
  return { Faces, Corners, Ends, HardCount: Corners.filter((Corner) => Corner.Hard).length };
}
const Lambert = ([X, Y]) => 0.1 + 0.9 * Math.max(0, X * LightDirection[0] + Y * LightDirection[1]);
const Grey = (Amount) => { const Level = Math.round(28 + Amount * 212); return `rgb(${Level},${Level},${Level})`; };

function ShadingStudy({ Points, Mode, Threshold, Flip }) {
  const { Faces, Corners, Ends } = ShadeProfile(Points, Mode, Threshold, Flip);
  const Ox = 72, Oy = 76, Scale = 62;
  const At = ([X, Y]) => [Ox + X * Scale, Oy - Y * Scale];
  // Lit band: viewer looks along −Y (from the bottom of the top view); the light comes from upper left.
  const Bx = 222, Top = 30, Bottom = 124, Depth = 20, Wide = 70;
  const Band = ([X, Y], Level) => [Bx + X * Wide, Level - Y * Depth * 0.5];
  const Front = Faces.map((Face, Index) => ({ Face, Index })).filter(({ Face }) => (Face.A[1] + Face.B[1]) / 2 < 0.02);
  return (
    <svg className="pi-study-svg" viewBox="0 0 300 150" role="img" aria-label="Shading study" data-study="shading">
      <defs>
        {Front.map(({ Index, Face }) => {
          const [Start, Finish] = Ends[Index];
          return (
            <linearGradient key={Index} id={"pi-shade-" + Index} gradientUnits="userSpaceOnUse" x1={Band(Face.A, 0)[0]} x2={Band(Face.B, 0)[0]} y1="0" y2="0">
              <stop offset="0" stopColor={Grey(Lambert(Start))} />
              <stop offset="1" stopColor={Grey(Lambert(Finish))} />
            </linearGradient>
          );
        })}
      </defs>
      <text x="8" y="12">TOP VIEW · NORMALS</text>
      <text x="292" y="12" textAnchor="end">LIT BAND</text>
      <polygon points={Points.map((Point) => At(Point).join(",")).join(" ")} fill="#151515" stroke="#6a6a6a" strokeWidth="1" />
      {Mode === "flat"
        ? Faces.map((Face, Index) => {
            const Mid = At([(Face.A[0] + Face.B[0]) / 2, (Face.A[1] + Face.B[1]) / 2]);
            return <line key={Index} x1={Mid[0]} y1={Mid[1]} x2={Mid[0] + Face.Normal[0] * 12} y2={Mid[1] - Face.Normal[1] * 12} stroke="#5fe0a0" strokeWidth="1.2" strokeLinecap="round" />;
          })
        : Corners.map((Corner, Index) => {
            const P = At(Corner.Point);
            const Arrows = Corner.Hard ? [Corner.Before, Corner.After] : [Corner.Shared];
            return (
              <g key={Index}>
                {Arrows.map((N, Each) => (
                  <line key={Each} x1={P[0]} y1={P[1]} x2={P[0] + N[0] * 12} y2={P[1] - N[1] * 12} stroke={Corner.Hard ? "#ffc93d" : "#5fe0a0"} strokeWidth="1.2" strokeLinecap="round" />
                ))}
                <circle cx={P[0]} cy={P[1]} r={Corner.Hard ? 2.2 : 1.4} fill={Corner.Hard ? "#ffc93d" : "#bdbdbd"} />
              </g>
            );
          })}
      <path d={`M${Ox - 66} ${Oy + 58}L${Ox - 50} ${Oy + 42}M${Ox - 50} ${Oy + 42}l-7 1M${Ox - 50} ${Oy + 42}l-1 7`} stroke="#e6cf72" strokeWidth="1.3" fill="none" strokeLinecap="round" />
      <text x={Ox - 66} y={Oy + 36} className="pi-study-accent">light</text>
      <polygon points={Points.map((Point) => Band(Point, Top).join(",")).join(" ")} fill="#5a5a5a" stroke="#2a2a2a" strokeWidth=".6" />
      {Front.map(({ Face, Index }) => {
        const Corner = [Band(Face.A, Top), Band(Face.B, Top), Band(Face.B, Bottom), Band(Face.A, Bottom)];
        return <polygon key={Index} points={Corner.map((Point) => Point.join(",")).join(" ")} fill={`url(#pi-shade-${Index})`} stroke={Mode === "smooth" ? "none" : "#00000030"} strokeWidth=".5" />;
      })}
      <text x={Bx} y="146" textAnchor="middle">{Mode === "flat" ? "faceted" : Mode === "smooth" ? "continuous" : "hard where corners exceed " + Math.round(Threshold) + "°"}</text>
      <text x={Ox} y="146" textAnchor="middle">{Points.length} faces in profile</text>
    </svg>
  );
}

// Wireframe of the authored primitive: the real segment / ring / side counts and bevel, projected at a three-quarter
// view. Edges facing away from the viewer are drawn fainter, so the density of the mesh reads at a glance.
function WireLines(Shape, Read, Scale) {
  const P = (Key, Initial) => +Read(Key, Initial);
  const Circle = (Count, At) => Array.from({ length: Count + 1 }, (_, Index) => At((2 * Math.PI * (Index % Count)) / Count));
  const Lines = [];
  if (Shape === "cube") {
    const Half = [P("Geometry Size X", 1) / 2, P("Geometry Size Y", 1) / 2, P("Geometry Size Z", 1) / 2];
    const Bevel = Math.min(P("Geometry Bevel", 0), ...Half.map((Each) => Each * 0.98));
    const Steps = Bevel > 0 ? Math.max(1, Math.round(P("Geometry Bevel Segments", 1))) : 0;
    for (let Axis = 0; Axis < 3; Axis++) {
      const [A, B] = [0, 1, 2].filter((Each) => Each !== Axis);
      for (const SignA of [-1, 1])
        for (const SignB of [-1, 1])
          for (let Step = 0; Step <= Steps; Step++) {
            const Turn = Steps ? (Step / Steps) * (Math.PI / 2) : 0;
            const Point = (Along) => {
              const Spot = [0, 0, 0];
              Spot[Axis] = Along * (Half[Axis] - Bevel);
              Spot[A] = SignA * (Half[A] - Bevel + Bevel * Math.cos(Turn));
              Spot[B] = SignB * (Half[B] - Bevel + Bevel * Math.sin(Turn));
              return Spot;
            };
            Lines.push([Point(-1), Point(1)]);
          }
    }
  } else if (Shape === "sphere") {
    const R = P("Geometry Radius", 0.5), Segments = Math.round(P("Geometry Segments", 32)), Rings = Math.round(P("Geometry Rings", 16));
    for (let Ring = 1; Ring < Rings; Ring++) {
      const Phi = (Math.PI * Ring) / Rings;
      Lines.push(Circle(Segments, (Theta) => [R * Math.sin(Phi) * Math.cos(Theta), R * Math.cos(Phi), R * Math.sin(Phi) * Math.sin(Theta)]));
    }
    for (let Segment = 0; Segment < Segments; Segment++) {
      const Theta = (2 * Math.PI * Segment) / Segments;
      Lines.push(Array.from({ length: Rings + 1 }, (_, Ring) => {
        const Phi = (Math.PI * Ring) / Rings;
        return [R * Math.sin(Phi) * Math.cos(Theta), R * Math.cos(Phi), R * Math.sin(Phi) * Math.sin(Theta)];
      }));
    }
  } else if (Shape === "cylinder" || Shape === "cone") {
    const R = P("Geometry Radius", 0.5), H = P("Geometry Height", 1), Segments = Math.round(P("Geometry Segments", 32));
    const Base = Circle(Segments, (Theta) => [R * Math.cos(Theta), -H / 2, R * Math.sin(Theta)]);
    Lines.push(Base);
    if (Shape === "cylinder") {
      Lines.push(Circle(Segments, (Theta) => [R * Math.cos(Theta), H / 2, R * Math.sin(Theta)]));
      for (let Segment = 0; Segment < Segments; Segment++) {
        const Theta = (2 * Math.PI * Segment) / Segments;
        Lines.push([[R * Math.cos(Theta), -H / 2, R * Math.sin(Theta)], [R * Math.cos(Theta), H / 2, R * Math.sin(Theta)]]);
      }
    } else
      for (let Segment = 0; Segment < Segments; Segment++) Lines.push([Base[Segment], [0, H / 2, 0]]);
  } else {
    const Major = P("Geometry Major Radius", 0.5), Minor = P("Geometry Minor Radius", 0.15);
    const Segments = Math.round(P("Geometry Segments", 48)), Sides = Math.round(P("Geometry Sides", 16));
    for (let Segment = 0; Segment < Segments; Segment++) {
      const Theta = (2 * Math.PI * Segment) / Segments;
      Lines.push(Circle(Sides, (Phi) => [(Major + Minor * Math.cos(Phi)) * Math.cos(Theta), Minor * Math.sin(Phi), (Major + Minor * Math.cos(Phi)) * Math.sin(Theta)]));
    }
    for (let Side = 0; Side < Sides; Side++) {
      const Phi = (2 * Math.PI * Side) / Sides;
      Lines.push(Circle(Segments, (Theta) => [(Major + Minor * Math.cos(Phi)) * Math.cos(Theta), Minor * Math.sin(Phi), (Major + Minor * Math.cos(Phi)) * Math.sin(Theta)]));
    }
  }
  return Lines.map((Line) => Line.map(([X, Y, Z]) => [X * Scale[0], Y * Scale[1], Z * Scale[2]]));
}

const Yaw = (-35 * Math.PI) / 180, Pitch = (24 * Math.PI) / 180;
function Project([X, Y, Z]) {
  const Across = X * Math.cos(Yaw) + Z * Math.sin(Yaw);
  const Deep = -X * Math.sin(Yaw) + Z * Math.cos(Yaw);
  return [Across, Y * Math.cos(Pitch) - Deep * Math.sin(Pitch), Y * Math.sin(Pitch) + Deep * Math.cos(Pitch)];
}

function WireStudy({ Shape, Read, Scale, Size }) {
  const Lines = WireLines(Shape, Read, Scale).map((Line) => Line.map(Project));
  const All = Lines.flat();
  const Span = (Index) => [Math.min(...All.map((Point) => Point[Index])), Math.max(...All.map((Point) => Point[Index]))];
  const [Xl, Xh] = Span(0), [Yl, Yh] = Span(1), [Dl, Dh] = Span(2);
  const Fit = Math.min(176 / Math.max(1e-6, Xh - Xl), 118 / Math.max(1e-6, Yh - Yl));
  const Cx = 136 - ((Xl + Xh) / 2) * Fit, Cy = 78 + ((Yl + Yh) / 2) * Fit;
  const Screen = ([X, Y]) => [Cx + X * Fit, Cy - Y * Fit];
  // Front edges (towards the viewer, larger depth) bright; back edges faint.
  const Strokes = [];
  Lines.forEach((Line, Index) => {
    for (let Start = 0; Start < Line.length - 1; Start += 6) {
      const Chunk = Line.slice(Start, Math.min(Line.length, Start + 7));
      const Depth = Chunk.reduce((Sum, Point) => Sum + Point[2], 0) / Chunk.length;
      const Near = (Depth - Dl) / Math.max(1e-6, Dh - Dl);
      Strokes.push(
        <path
          key={Index + "-" + Start}
          d={Chunk.map((Point, Each) => (Each ? "L" : "M") + Screen(Point).map((Value) => Value.toFixed(1)).join(" ")).join("")}
          stroke="#d8d8d8"
          strokeOpacity={(0.1 + 0.75 * Near ** 1.4).toFixed(2)}
          strokeWidth=".8"
          fill="none"
        />,
      );
    }
  });
  const Left = Cx + Xl * Fit, Right = Cx + Xh * Fit, Top = Cy - Yh * Fit, Bottom = Cy - Yl * Fit;
  return (
    <svg className="pi-wire-svg" viewBox="0 0 300 170" role="img" aria-label="Primitive wireframe" data-study="wireframe">
      <ellipse cx={(Left + Right) / 2} cy={Bottom + 6} rx={(Right - Left) * 0.42} ry="5" fill="#000" opacity=".45" />
      {Strokes}
      <path d={`M${Left} ${Bottom + 16}H${Right}M${Left} ${Bottom + 12}v8M${Right} ${Bottom + 12}v8`} stroke="#9a9a9a" strokeWidth=".8" />
      <text x={(Left + Right) / 2} y={Bottom + 27} textAnchor="middle">W {Fixed(Size.Bounds[0])} m · D {Fixed(Size.Bounds[2])} m</text>
      <path d={`M${Right + 14} ${Top}V${Bottom}M${Right + 10} ${Top}h8M${Right + 10} ${Bottom}h8`} stroke="#9a9a9a" strokeWidth=".8" />
      <text x={Right + 20} y={(Top + Bottom) / 2 + 3}>H {Fixed(Size.Bounds[1])} m</text>
    </svg>
  );
}

// Geometry page from the editor's own building blocks: property cards (Kit.Card), the split-value pill slider and
// switches (Kit.Control) and the large metric (Kit.Metric), so it matches every other inspector.
function GeometryPage({ Shape, Read, Change, Values, Size, Kit }) {
  const { Card: PropertyCard, Control, Metric } = Kit;
  const Primitive = Shapes[Shape];
  const Locked = !!Values.LOCKED;
  const Scale = Array.isArray(Values.Scale) ? Values.Scale : [1, 1, 1];
  const Shading = Read("Geometry Shading", "smooth");
  const Threshold = +Read("Geometry Smooth Angle", 40);
  const Flip = !!Read("Geometry Flip Normals", false);
  const Points = CrossSection(Shape, Read);
  const Result = ShadeProfile(Points, Shading, Threshold, Flip);
  const CornerAngle = Result.Corners.reduce((Largest, Corner) => Math.max(Largest, Corner.Angle), 0);
  const Groups = Shading === "smooth" ? 1 : Shading === "flat" ? Points.length : Math.max(1, Result.HardCount);
  const Parameter = ([Label, Key, Initial, Minimum, Maximum, Step, Suffix]) => (
    <Control
      key={Key}
      Field={{ Label, Control: "Slider", Minimum, Maximum, Default: Initial, Unit: Suffix, Decimals: Step >= 1 ? 0 : Step >= 0.01 ? 2 : 3 }}
      Value={Read(Key, Initial)}
      Disabled={Locked}
      Change={(Next) => Change(Key, Step >= 1 ? Math.round(Next) : Next)}
    />
  );
  const Toggle = (Label, Key, Initial) => (
    <Control key={Key} Field={{ Label, Control: "Switch" }} Value={!!Read(Key, Initial)} Disabled={Locked} Change={(Next) => Change(Key, Next)} />
  );
  const Readout = (Label, Text) => <Control key={Label} Field={{ Label, Control: "Readout" }} Value={Text} />;
  const Dimensions = Primitive.Parameters.filter((Entry) => Entry[6] === "m");
  const Resolution = Primitive.Parameters.filter((Entry) => Entry[6] !== "m");
  const [Lead] = Dimensions;
  const Modes = ["smooth", "auto", "flat"];
  return (
    <>
      <TransformPanel Values={Values} Change={Change} Gizmo />
      <PropertyCard Title="Shape" Class="pi-shape-card">
        <Metric
          Value={Fixed(+Read(Lead[1], Lead[2]))}
          Unit={"m · " + Lead[0]}
          Caption={`${Primitive.Name} · ${Fixed(Size.Bounds[0])} × ${Fixed(Size.Bounds[1])} × ${Fixed(Size.Bounds[2])} m with scale`}
        />
        <div className="graphic pi-wire">
          <WireStudy Shape={Shape} Read={Read} Scale={Scale} Size={Size} />
        </div>
        {Dimensions.map(Parameter)}
        {Locked && <p>Locked · unlock in Quick controls to edit the shape.</p>}
      </PropertyCard>
      <PropertyCard Title="Resolution" Class="pi-resolution-card">
        <Metric Value={Size.Triangles.toLocaleString("en-US")} Unit="triangles" Caption={`${Size.Vertices.toLocaleString("en-US")} vertices · ${Size.Closed ? "closed" : "open"} surface`} />
        {Resolution.map(Parameter)}
        {Primitive.Caps && Toggle("Caps", "Geometry Caps", true)}
        {Readout("Surface area", Fixed(Size.Area) + " m²")}
        {Readout("Volume", Fixed(Size.Volume, 3) + " m³")}
      </PropertyCard>
      <PropertyCard Title="Shading" Class="pi-shading-card">
        <Metric
          Value={Shading === "auto" ? Fixed(Threshold, 0) : Shading === "flat" ? String(Points.length) : "1"}
          Unit={Shading === "auto" ? "° smoothing angle" : Shading === "flat" ? "flat faces" : "smooth surface"}
          Caption={`Profile corners turn ${Fixed(CornerAngle, 1)}° · ${Result.HardCount} of ${Points.length} hard`}
        />
        <div className="graphic pi-study">
          <ShadingStudy Points={Points} Mode={Shading} Threshold={Threshold} Flip={Flip} />
        </div>
        <Control
          Field={{ Label: "Shading", Control: "Select", Options: ["Smooth", "Auto smooth", "Flat"] }}
          Value={Math.max(0, Modes.indexOf(Shading))}
          Disabled={Locked}
          Change={(Next) => Change("Geometry Shading", Modes[Next] || "smooth")}
        />
        <Control
          Field={{ Label: "Smoothing angle", Control: "Slider", Minimum: 0, Maximum: 180, Default: 40, Unit: "°", Decimals: 0 }}
          Value={Threshold}
          Disabled={Locked || Shading !== "auto"}
          Change={(Next) => Change("Geometry Smooth Angle", Math.round(Next))}
        />
        <p>Green: shared normals (soft). Amber: split normals (hard edge).</p>
      </PropertyCard>
      <PropertyCard Title="Normals" Class="pi-normals-card">
        {Readout("Smoothing groups", String(Groups))}
        {Readout("Hard edges", String(Result.HardCount))}
        {Readout("Largest corner", Fixed(CornerAngle, 0) + "°")}
        {Toggle("Weighted normals", "Geometry Weighted Normals", true)}
        {Toggle("Double-sided", "Geometry Double Sided", false)}
        {Toggle("Flip normals", "Geometry Flip Normals", false)}
      </PropertyCard>
    </>
  );
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    CAROUSEL
//------------------------------------------------------------------------------------------------------------------------

// The categories are picked from the Orbit menu (OrbitMenu.jsx): a round button pinned bottom right, ringed by the category colours,
// that opens the categories in a circle around the object's summary. The cards always come first. ← and → (or a sideways swipe on the
// cards) move between categories.
function Carousel({ Pages, Focus = null, Summary }) {
  const [Current, Choose] = useState(Focus ? "components" : "overview");
  const [Direction, Turn] = useState("none");
  const Index = Math.max(0, Categories.findIndex(([Key]) => Key === Current));
  const Go = (Next) => {
    const Target = (Next + Categories.length) % Categories.length;
    Turn(Target >= Index ? "next" : "previous");
    Choose(Categories[Target][0]);
  };
  // Opening a component from elsewhere turns to the Components page.
  useEffect(() => {
    if (Focus && Current !== "components") { Turn("next"); Choose("components"); }
  }, [Focus?.Stamp]);
  const Swipe = useRef(null);
  const [Key, Label, Glyph, Tone] = Categories[Index];
  return (
    <section className="pi-carousel" aria-label="Categories" data-navigation="orbit" style={{ "--pi-current": Tone }}>
      <div
        key={Key}
        className={"pi-page pi-enter-" + Direction}
        role="tabpanel"
        aria-label={Label}
        data-page={Key}
        onPointerDown={(Event) => {
          // A press on a control (or a drag grip, whose pointerup never arrives) is not a swipe; drop any stale start too.
          Swipe.current = Event.target.closest("input, textarea, button, select, [role=slider], label, [draggable=true]") ? null : { X: Event.clientX, Y: Event.clientY };
        }}
        onDragStart={() => (Swipe.current = null)}
        onPointerUp={(Event) => {
          const Start = Swipe.current;
          Swipe.current = null;
          if (!Start) return;
          const Dx = Event.clientX - Start.X, Dy = Event.clientY - Start.Y;
          if (Math.abs(Dx) > 60 && Math.abs(Dx) > Math.abs(Dy) * 1.5) Go(Index + (Dx < 0 ? 1 : -1));
        }}
      >
        {Pages[Key]}
      </div>
      <OrbitMenu
        Owner="Category"
        Look="categories"
        Index={Index}
        Pick={Go}
        Face={<PrimitiveGlyph Name={Glyph} Size={18} />}
        Pages={Categories.map(([Each, Caption, EachGlyph, EachTone]) => ({ Id: Each, Label: Caption, Tone: EachTone, ClassName: "pi-pill" + (Each === Key ? " active" : ""), Inside: <PrimitiveGlyph Name={EachGlyph} Size={16} /> }))}
        Middle={
          <>
            <b>{Summary.Triangles}</b>
            <small>tris</small>
            <span>
              {Summary.Vertices} verts · {Summary.Materials} mat
            </span>
            <span>{Summary.Motion}</span>
            <strong>{Label}</strong>
          </>
        }
      />
    </section>
  );
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    INSPECTOR
//------------------------------------------------------------------------------------------------------------------------

export default function PrimitiveInspector({ Subject, Values, Change, Hidden, ToggleHidden, MaterialCards, FractureCards, UvCards = null, SdfCards = null, LodCards = null, Components = null, ComponentPage = null, Focus = null, Kit }) {
  const Shape = ShapeOf(Subject);
  const Read = (Key, Initial) => (Values[Key] === undefined ? Initial : Values[Key]);
  const Size = Measure(Shape, Read, Array.isArray(Values.Scale) ? Values.Scale : [1, 1, 1]);
  const Shared = { Shape, Read, Change, Values, Hidden, ToggleHidden, Size, Kit };
  return (
    <div className="primitive-inspector">
      <Carousel
        key={Subject.Id}
        Focus={Focus?.Owner === Subject.Id ? Focus : null}
        Summary={{
          Triangles: Size.Triangles.toLocaleString("en-US"),
          Vertices: Size.Vertices.toLocaleString("en-US"),
          Materials: 1,
          Motion: Values.DYNAMIC ? "Dynamic" : "Static",
        }}
        Pages={{
          overview: <OverviewPage {...Shared} Components={Components} />,
          components: ComponentPage,
          geometry: <GeometryPage {...Shared} />,
          material: MaterialCards,
          uv: UvCards,
          fracture: FractureCards,
          sdf: SdfCards,
          lod: LodCards,
        }}
      />
    </div>
  );
}
