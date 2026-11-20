import { RecolourInstrument } from "./InstrumentSpecification.js";
import { LightIcons } from "./LightSpecification.js";
import fs from "node:fs";
import { createHash } from "node:crypto";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { createRequire } from "node:module";
const Folder = path.dirname(fileURLToPath(import.meta.url)),
  Frontier = path.resolve(Folder, "../..");
const Require = createRequire(
  path.resolve(Folder, "../FrontierEditor/package.json"),
);
const { build } = Require("esbuild");

let Assets;
let Fonts;

if (fs.existsSync(path.join(Frontier, "EngineContent/Icons"))) {
  const { createCanvas, loadImage } = Require("@napi-rs/canvas");
  Assets = {
    Icons: {},
    Glyphs: {},
    Moons: [],
    Fonts: Object.fromEntries(
      ["Light", "Regular"].map((Weight) => [
        Weight,
        "data:font/ttf;base64," +
          fs
            .readFileSync(
              path.join(
                Frontier,
                "EngineContent/Fonts/SunReference",
                "DMSans-" + Weight + ".ttf",
              ),
            )
            .toString("base64"),
      ]),
    ),
    ShaderBall: fs
      .readFileSync(
        path.join(Frontier, "Exhibits/DistanceFieldGI/ShaderBall.mesh"),
      )
      .toString("base64"),
    Notices: {
      Fonts: fs.readFileSync(
        path.join(Frontier, "EngineContent/Fonts/SunReference/OFL.txt"),
        "utf8",
      ),
      Icons: fs.readFileSync(
        path.join(Frontier, "EngineContent/Icons/Slate-NOTICE.md"),
        "utf8",
      ),
    },
  };
  for (const File of fs
    .readdirSync(path.join(Frontier, "EngineContent/Icons"))
    .filter((Name) => Name.endsWith(".svg"))) {
    Assets.Icons[File.slice(0, -4)] =
      "data:image/svg+xml;base64," +
      fs
        .readFileSync(path.join(Frontier, "EngineContent/Icons", File))
        .toString("base64");
  }
  const Vectors = fs.readFileSync(
    path.join(Frontier, "Engine/DisplayPresentation/VectorCodec.cpp"),
    "utf8",
  );
  for (const Match of Vectors.matchAll(
    /VectorGlyphRecord\s*\{\s*"([^"]+)"\s*,\s*"([^"]+)"/g,
  ))
    Assets.Glyphs[Match[1]] = Match[2];
  for (const Name of [
    "luna",
    "ember",
    "glacier",
    "sulfur",
    "shroud",
    "shard",
  ]) {
    const Image = await loadImage(
      path.join(Frontier, "EngineContent/CelestialTextures", Name + "_2k.jpg"),
    );
    const Canvas = createCanvas(512, 256);
    Canvas.getContext("2d").drawImage(Image, 0, 0, 512, 256);
    Assets.Moons.push(Canvas.toDataURL("image/jpeg", 0.9));
  }
  Fonts = ["Light", "Regular"]
    .map(
      (Weight, Index) =>
        `@font-face{font-family:'DM Sans';font-style:normal;font-weight:${Index ? 400 : 300};font-display:swap;src:url(data:font/ttf;base64,${fs.readFileSync(path.join(Frontier, "EngineContent/Fonts/SunReference", "DMSans-" + Weight + ".ttf")).toString("base64")}) format('truetype')}`,
    )
    .join("\n");
} else {
  const ExistingHtml = fs.readFileSync(path.join(Folder, "index.html"), "utf8");
  const StyleStart = ExistingHtml.indexOf("<style>") + "<style>".length;
  const SecondNewline = ExistingHtml.indexOf(
    "\n",
    ExistingHtml.indexOf("\n", StyleStart) + 1,
  );
  Fonts = ExistingHtml.slice(StyleStart, SecondNewline);
  const Marker = "<script>window.NativeAssets=";
  const JsonStart = ExistingHtml.indexOf(Marker) + Marker.length;
  const JsonEnd = ExistingHtml.indexOf(";</script><script>", JsonStart);
  const Embedded = JSON.parse(ExistingHtml.slice(JsonStart, JsonEnd));
  Assets = {
    Icons: Embedded.Icons,
    Glyphs: Embedded.Glyphs,
    Moons: Embedded.Moons,
    Fonts: Embedded.Fonts,
    ShaderBall: Embedded.ShaderBall,
    Notices: Embedded.Notices,
  };
}

// Preserve the reference's document-wide CSS and control behaviour without restyling the current editor.
const ReferenceOutput = await build({
  plugins: [
    {
      name: "instrument-presentation",
      setup(Build) {
        Build.onLoad(
          { filter: /InspectorDepot[\\/]panels[\\/](wind|fog)\.js$/ },
          (Args) => ({
            contents: RecolourInstrument(
              path.basename(Args.path),
              fs.readFileSync(Args.path, "utf8"),
            ),
            loader: "js",
          }),
        );
      },
    },
  ],
  entryPoints: [path.join(Folder, "InspectorHost.js")],
  nodePaths: [path.join(Folder, "../FrontierEditor/node_modules")],
  define: {
    "process.env.NODE_ENV": '"production"',
    __LIGHT_ICONS__: JSON.stringify(
      Object.fromEntries(
        Object.values(LightIcons).map((Name) => [Name, Assets.Icons[Name]]),
      ),
    ),
  },
  bundle: true,
  write: false,
  minify: true,
  format: "iife",
  target: ["chrome110", "firefox115", "safari16"],
});
const ReferenceStyle = fs.readFileSync(
  path.join(Folder, "InspectorDepot/styles.css"),
  "utf8",
);
const ReferenceScript = ReferenceOutput.outputFiles[0].text.replaceAll(
  "</script",
  "<\\/script",
);
const ReferenceHash = createHash("sha256")
  .update(ReferenceScript)
  .digest("base64");
const MainControlStyle = fs.readFileSync(
  path.join(Folder, "Editor.css"),
  "utf8",
);
const LightControlSelectors = new Set([
  'input[type="range"]',
  'input[type="range"]::-webkit-slider-thumb',
  'input[type="range"]::-moz-range-thumb',
  'input[type="range"]:disabled',
  ".field",
  ".field > span",
  ".field > select",
  ".slider-pill",
  ".split-value",
  ".split-value input",
  ".split-value small",
  ".slider-pill > input",
]);
const LightControls = [...MainControlStyle.matchAll(/([^{}]+)\{([^{}]*)\}/g)]
  .filter((Match) => LightControlSelectors.has(Match[1].trim()))
  .map((Match) => `.lighting-panel ${Match[1].trim()} {${Match[2]}}`)
  .join("\n");
Assets.ReferenceInspector = `<!doctype html><html><head><meta charset="utf-8">
<meta http-equiv="Content-Security-Policy" content="default-src 'none'; script-src 'sha256-${ReferenceHash}'; style-src 'unsafe-inline'; font-src https://cdn.fontshare.com; img-src data:; base-uri 'none'">
<style>${fs.readFileSync(path.join(Folder, "InspectorDepot/Fontshare.css"), "utf8")}
${ReferenceStyle}
${LightControls}
${fs.readFileSync(path.join(Folder, "MaterialPanel.css"), "utf8").split(".material-panel {")[0]}
${fs.readFileSync(path.join(Folder, "LightPanel.css"), "utf8")}
#ReferenceMount:is([data-reference-kind="wind"],[data-reference-kind="sun"],[data-reference-kind="fog"],[data-reference-kind="clouds"],[data-reference-kind$="light"],[data-reference-kind="ledstrip"]){padding:0;}
@media(max-width:320px){#ReferenceMount[data-reference-kind="wind"] .mp-rail{grid-template-columns:repeat(2,minmax(0,1fr));}}
html,body{height:auto;overflow:hidden;background:var(--panel)}
#ReferenceMount{display:block;overflow:hidden;flex:none}
</style></head><body><div id="ReferenceMount" class="props"></div><script>${ReferenceScript}</script></body></html>`;
const FractureBridgePlugin = {
  name: "fracture-bridge-fallback",
  setup(Build) {
    Build.onResolve(
      { filter: /\.\.\/FractureEditor\/Fracture(Specification|Projection)\.js$/ },
      (Args) => {
        const Candidate = path.resolve(Args.resolveDir, Args.path);
        if (fs.existsSync(Candidate)) return { path: Candidate };
        return {
          path: Args.path,
          namespace: "fracture-fallback",
        };
      },
    );
    Build.onLoad({ filter: /.*/, namespace: "fracture-fallback" }, (Args) => {
      if (Args.path.endsWith("FractureSpecification.js")) {
        return {
          loader: "js",
          contents: `
export const Prefix = "Frontier.Fracture.v2:";
const Defaults = { Enabled: false, Mode: "dynamic", PieceSdf: false, SdfResolution: 64, Material: "concrete", Energy: 2500, Seed: 42, Ceiling: 48, MinimumSize: 0.045, X: 0, Y: 0, Z: 0 };
const Materials = { concrete: "Concrete", rock: "Stone", wood: "Wood", glass: "Glass", tempered: "Tempered glass", plastic: "ABS plastic" };
const Bounds = { Energy: [0, 50000], Seed: [1, 999999], Ceiling: [2, 160], MinimumSize: [0.002, 0.3], X: [-1000, 1000], Y: [-1000, 1000], Z: [-1000, 1000] };
export function Normalize(Input = {}) {
  if (!Input || typeof Input !== "object") Input = {};
  const Next = {
    ...Defaults,
    EditedAt: Number.isFinite(Input.EditedAt) ? Input.EditedAt : 0,
    Enabled: Input.Enabled === true,
    PieceSdf: Input.PieceSdf === true,
    SdfResolution: [32, 64, 128].includes(Number(Input.SdfResolution)) ? Number(Input.SdfResolution) : 64,
    Mode: Input.Mode === "baked" ? "baked" : "dynamic",
    Material: Object.hasOwn(Materials, Input.Material) ? Input.Material : Defaults.Material,
  };
  for (const [Key, [Min, Max]] of Object.entries(Bounds)) {
    const Val = Number(Input[Key] ?? Defaults[Key]);
    Next[Key] = Number.isFinite(Val) ? Math.min(Max, Math.max(Min, Val)) : Defaults[Key];
    if (["Ceiling", "Seed"].includes(Key)) Next[Key] = Math.round(Next[Key]);
  }
  if (Input.Baked && typeof Input.Baked.Signature === "string") Next.Baked = Input.Baked;
  return Next;
}
export function Describe(Row, Values = {}) {
  return {
    Id: Row.Id,
    Name: Row.Name,
    Primitive: Row.Icon?.replace(/^editor-/, ""),
    Scale: [0, 1, 2].map((Idx) => {
      const Val = Number(Values.Scale?.[Idx] ?? 1);
      return Number.isFinite(Val) ? Math.min(1000, Math.max(0.001, Val)) : 1;
    }),
  };
}
export function Signature(Owner, Settings) {
  const { Enabled, Mode, Baked, EditedAt, PieceSdf, SdfResolution, ...Recipe } = Normalize(Settings);
  return JSON.stringify({ Revision: 2, Primitive: Owner.Primitive, Scale: Owner.Scale, Recipe });
}
export function ReadRecord(Id) {
  try { return JSON.parse(localStorage.getItem(Prefix + Id)); } catch { return null; }
}
export function Publish(Owner, Settings) {
  const Record = { Owner, Settings: Normalize(Settings) };
  const Json = JSON.stringify(Record);
  const Key = Prefix + Owner.Id;
  if (localStorage.getItem(Key) !== Json) localStorage.setItem(Key, Json);
  return Record;
}
export function OpenFracture(Row, Values) {
  Publish(Describe(Row, Values), Values.Fracture);
  window.open("../FractureEditor/index.html?object=" + encodeURIComponent(Row.Id), "_blank", "noopener");
}
`,
        };
      }
      return {
        loader: "js",
        contents: `
const FormatPoints = (Pts) => Pts.map((Pt) => Pt.map((N) => N.toFixed(2)).join(",")).join(" ");
function ClipHalfPlane(Poly, Normal, Offset) {
  const Out = [];
  Poly.forEach((Cur, Idx) => {
    const Next = Poly[(Idx + 1) % Poly.length];
    const D1 = Cur[0] * Normal[0] + Cur[1] * Normal[1] - Offset;
    const D2 = Next[0] * Normal[0] + Next[1] * Normal[1] - Offset;
    if (D1 <= 0) Out.push(Cur);
    if ((D1 < 0) !== (D2 < 0)) {
      const T = D1 / (D1 - D2);
      Out.push(Cur.map((V, Axis) => V + (Next[Axis] - V) * T));
    }
  });
  return Out;
}
function VoronoiCells(Seeds, Boundary) {
  return Seeds.map((Seed, Idx) => {
    let Cell = Boundary;
    Seeds.forEach((Other, OtherIdx) => {
      if (OtherIdx === Idx) return;
      const Normal = Other.map((V, Axis) => V - Seed[Axis]);
      const Offset = (Other[0] ** 2 + Other[1] ** 2 - Seed[0] ** 2 - Seed[1] ** 2) / 2;
      Cell = ClipHalfPlane(Cell, Normal, Offset);
    });
    return Cell;
  });
}
function ShadeTone(Level, Tinted = false) {
  const N = Math.round(Math.max(35, Math.min(210, Level)));
  return Tinted ? \`rgb(\${N - 12},\${N + 3},\${N - 4})\` : \`rgb(\${N},\${N + 2},\${N + 1})\`;
}
function RenderCell(Cell, Idx, Project) {
  if (Cell.length < 3) return "";
  const Center = Cell.reduce((Acc, Pt) => Acc.map((V, Axis) => V + Pt[Axis] / Cell.length), [0, 0]);
  const Ring = Cell.map((Pt) => Project(Pt, Center));
  const Mid = Project(Center, Center);
  const Base = 133 - Center[0] * 0.36 - Center[1] * 0.35;
  let Svg = "";
  Ring.forEach((Cur, Edge) => {
    const Next = Ring[(Edge + 1) % Ring.length];
    if (Next[0] < Cur[0]) Svg += \`<polygon points="\${FormatPoints([Cur, Next, [Next[0] + 1, Next[1] + 7], [Cur[0] + 1, Cur[1] + 7]])}" fill="\${ShadeTone(Base * 0.42)}"/>\`;
  });
  Ring.forEach((Cur, Edge) => {
    const Next = Ring[(Edge + 1) % Ring.length];
    Svg += \`<polygon points="\${FormatPoints([Cur, Next, Mid])}" fill="\${ShadeTone(Base + Math.sin(Edge * 2.3 + Idx) * 15, Idx % 7 === 0)}"/>\`;
  });
  return Svg + \`<polygon points="\${FormatPoints(Ring)}" fill="none" stroke="#d4dfd4" stroke-opacity=".19" stroke-width=".6" stroke-linejoin="round"/>\`;
}
export function FractureGlyph() {
  const Seeds = [[-9, -6], [19, -22], [-32, -29], [41, 5], [-34, 8], [8, 28], [-19, 37], [39, 39], [2, -46]];
  const Boundary = Array.from({ length: 36 }, (_, Idx) => {
    const Angle = (Idx * Math.PI) / 18;
    return [Math.cos(Angle) * 58, Math.sin(Angle) * 58];
  });
  return \`<svg viewBox="0 0 280 134" role="img" aria-label="Shaded fractured solid illustration"><ellipse cx="140" cy="113" rx="71" ry="9" fill="#000" opacity=".19"/><g>\${VoronoiCells(Seeds, Boundary).sort((A, B) => A[0][1] - B[0][1]).map((Cell, Idx) => RenderCell(Cell, Idx, (Pt, Ctr) => [140 + Pt[0] * 1.05 + Ctr[0] * 0.18, 61 + Pt[1] * 0.77 + Ctr[1] * 0.16 - Math.sqrt(Math.max(0, 58 ** 2 - Ctr[0] ** 2 - Ctr[1] ** 2)) * 0.12])).join("")}</g></svg>\`;
}
`,
      };
    });
  },
};

const Output = await build({
  plugins: [FractureBridgePlugin],
  entryPoints: [path.join(Folder, "Editor.jsx")],
  bundle: true,
  write: false,
  minify: true,
  format: "iife",
  outfile: "Editor.js",
  nodePaths: [path.join(Folder, "../FrontierEditor/node_modules")],
  define: { "process.env.NODE_ENV": '"production"' },
  target: ["chrome110", "firefox115", "safari16"],
});
const Script = Output.outputFiles.find((File) =>
    File.path.endsWith(".js"),
  ).text,
  Style = Output.outputFiles.find((File) => File.path.endsWith(".css")).text;
fs.writeFileSync(
  path.join(Folder, "index.html"),
  `<!doctype html>\n<html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><meta name="description" content="Frontier World Mirror — Calibrated Tier 1 GTX WebGL2 mirror of the native C++20 Cornell-Studio Gallery scene, EditorCamera, Outliner, and Inspector."><title>Frontier World Mirror — Tier 1 GTX (C++ ↔ HTML)</title><style>${Fonts}\n${Style}</style></head><body><div id="Editor"></div><script>window.NativeAssets=${JSON.stringify(Assets).replaceAll("<", "\\u003c")};</script><script>${Script.replaceAll("</script", "<\\/script")}</script></body></html>\n`,
);
console.log(
  `Built standalone index.html (${(fs.statSync(path.join(Folder, "index.html")).size / 1024 / 1024).toFixed(2)} MiB), ${Object.keys(Assets.Icons).length} shipped icons, ${Object.keys(Assets.Glyphs).length} native vector glyphs, six moon textures, and the repository shader-ball mesh.`,
);
