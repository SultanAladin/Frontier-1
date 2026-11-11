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
const { build } = Require("esbuild"),
  { createCanvas, loadImage } = Require("@napi-rs/canvas");
const Assets = {
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
for (const Name of ["luna", "ember", "glacier", "sulfur", "shroud", "shard"]) {
  const Image = await loadImage(
    path.join(Frontier, "EngineContent/CelestialTextures", Name + "_2k.jpg"),
  );
  const Canvas = createCanvas(512, 256);
  Canvas.getContext("2d").drawImage(Image, 0, 0, 512, 256);
  Assets.Moons.push(Canvas.toDataURL("image/jpeg", 0.9));
}
const Fonts = ["Light", "Regular"]
  .map(
    (Weight, Index) =>
      `@font-face{font-family:'DM Sans';font-style:normal;font-weight:${Index ? 400 : 300};font-display:swap;src:url(data:font/ttf;base64,${fs.readFileSync(path.join(Frontier, "EngineContent/Fonts/SunReference", "DMSans-" + Weight + ".ttf")).toString("base64")}) format('truetype')}`,
  )
  .join("\n");
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
const Output = await build({
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
  `<!doctype html>\n<html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><meta name="description" content="Project Zero native editor UI translated to HTML. Native icons and fonts; browser-local controls, no engine connection."><title>Project-Zero — Editor</title><style>${Fonts}\n${Style}</style></head><body><div id="Editor"></div><script>window.NativeAssets=${JSON.stringify(Assets).replaceAll("<", "\\u003c")};</script><script>${Script.replaceAll("</script", "<\\/script")}</script></body></html>\n`,
);
console.log(
  `Built standalone index.html (${(fs.statSync(path.join(Folder, "index.html")).size / 1024 / 1024).toFixed(2)} MiB), ${Object.keys(Assets.Icons).length} shipped icons, ${Object.keys(Assets.Glyphs).length} native vector glyphs, six moon textures, and the repository shader-ball mesh.`,
);
