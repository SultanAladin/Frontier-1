# Project browser — HTML redesign

Interactive prototype requested before native startup-browser integration. Open `index.html` through a local web server.
All HTML, CSS, JavaScript, fonts and illustrative preview assets live together in this directory.

## Run

```bash
python3 -m http.server 8770 --bind 0.0.0.0 --directory 'Experimental/Project browser'
```

## Implemented interactions

- Responsive file catalogue with format filters, search, sorting and browser-local stars.
- Three fixed shared columns for both catalogue and reader: 204 px left, flexible centre, 302 px right on desktop.
- Each column owns an aligned 64 px header; the global Frontier/design-preview branding header is removed.
- Left/right headers collapse their panels independently to 44 px rails and reopen them without losing view selection.
- Collapse choices carry across catalogue/reader transitions; narrow screens use compact collapsed panels.
- The working version selector has a pill-shaped trigger.
- Overview Markdown uses a complete 210:297 A4 sheet, laid out at 794 × 1123 px and fitted to available width/height.
- Document zoom/fit and scrolling preserve the complete sheet instead of clipping it to the renderer rectangle.
- Long imported Markdown remains scrollable; automatic multi-page pagination is not implemented.
- Project levels; CAD assemblies and numbered sketches; geometry, UV, texture and metadata views.
- Markdown document preview, local notes and selectable sample version history/header version selector.
- Reference WebGL2 EON/GGX lighting, four surface channels and orthographic drafting/hidden-edge contours.
- Small rendered studio scene, reference actuator CAD specimen and extracted housing geometry.
- Catalogue, preview-strip, detail and history thumbnails come from renderer PNG captures, never illustrative SVGs.
- Capture thumbnail stores the current orbit/zoom/channel frame in the matching file/view/revision cache.
- OLED default, Light and Dark palettes based on `Engine/Interface/ThemeStructure.cpp`.
- Theme selection applies to the catalogue and reader; retained under browser-local `frontier.appearance.theme`.
- Real file and directory inputs, local image previews and a safe, bounded Markdown text subset.
- Preview selection, zoom/fit, arrow-key navigation, Escape to library and `/` to search.

## Explicit boundaries

- Sample locations, revisions and comments are labelled examples, not real disk projects or collaboration.
- WebGL-rendered fixtures are actual browser rasterization, not native Vulkan output or decoded project files.
- Reference renderer mathematics/shaders/actuator geometry are reused from the pinned source HTML. A small studio
  enclosure is added using its vertex layout/shaders. CAD uses its original contour extraction and depth testing.
- Sample revision selection changes actual geometry: copper/shaft omission for older assembly/studio samples and
  smaller housing proportions for geometry samples. These are fixtures, not an on-disk version archive.
- Level/assembly buttons currently provide sample camera/orthographic views, not authored native level documents.
- UV inspection uses generated cylindrical coordinates from actual vertices; it is not an authored UV archive.
- No-WebGL cases show an explicit unavailable message; no fake image replaces the renderer.
- Native project/CAD/geometry codecs are not decoded here. Imported native files show an explicit placeholder.
- Native editor launch is a hand-off preview dialog, not a simulated successful launch.
- Browsers hide absolute file paths. Native startup integration must provide OS file/folder dialogs.
- `.project` replaces `.scene` in the proposed browser vocabulary only; no existing files/codecs are renamed.
- `.geometry` is a prototype suffix pending final format decisions. Existing `.projectspace` is accepted separately.
- Notes and stars are browser-local. No project file, native preferences or version archive is rewritten.
- Markdown raw HTML and embedded image/graph syntax are displayed safely as text; full embedded-document support remains
  future work. Sample documents include a known local illustrative image, not arbitrary remote resources.
- Native Frontier.exe startup routing, project creation, native version comparison/restore, CAD/geometry codecs and
  shared native theme persistence remain implementation work after design approval.

## References and checks

Inspected previous prototype at `SultanAladin/Frontier` commit `658fac7378f2b38309966fab72c007e98075e538`,
`Documentation/Prototypes/ProjectBrowser.html`, blob `c91a3804a56754ad92e51001bd9237a0d17d7e3e`.
Supplied visual reference images are retained under `Exhibits/ProjectBrowser/References/`.
Inter is reused from the existing font archive; its original licence is in `Assets/Inter-LICENSE.txt`.

Real Chromium/SwiftShader WebGL checks and screenshots are retained in `Exhibits/ProjectBrowser/`:
renderer-produced PNG thumbnails, nonblack studio pixels, exact current-frame/thumbnail byte matching, revision
pixel changes and repeatable Current restore, CAD/sketch contours, generated UV, albedo and actual metadata,
Light theme and 390 px layouts without horizontal overflow. This is real browser/software-renderer execution,
not native Vulkan, physical GPU or codec validation. Earlier jsdom checks are historical only.

## Aligned-panel correction checks

`Exhibits/ProjectBrowser/Code/PanelLayoutValidation.cjs` verifies identical library/reader column boundaries and header
alignment, A4 proportions/no clipping/full-sheet fit, accessible document bottom, pill version selection, independent
collapse/expand, cross-view collapse preservation and 390 px layout bounds in real Chromium.
Results: `Exhibits/ProjectBrowser/Results/PanelLayoutChecks.json`; captures include `A4Overview.png`, `A4Narrow.png`
and `AlignedLibrary.png`. The rendering/version regression suite also passes after these corrections.

## Preview cache consistency

HTML asset URLs include the first 12 characters of each stylesheet/script SHA-256. Update those query revisions when
changing the corresponding asset, so embedded previews cannot combine new markup with an older cached script.
`CatalogueLoadingValidation.cjs` checks those revisions, visible file cards at 890 × 800, all six open/back routes,
filtering and reload. Evidence: `Results/CatalogueLoadingChecks.json` and `Images/CatalogueEmbedded.png`.
A loading message remains visible if script startup fails instead of leaving an unexplained empty catalogue.


## Renderer validation moved out

The Project Browser application reference no longer contains a renderer-validation catalogue entry, material editor,
Vulkan controls or Vulkan scripts/styles. Its six original file samples and WebGL reference rendering remain.

Native renderer validation now lives at `Exhibits/Renderer/index.html`, linked from the native/ImGui evidence pages
`Exhibits/SpaceFormats/index.html` and `Exhibits/ProjectBrowser/index.html`. Run
`Exhibits/Renderer/Code/ServeRenderer.py` using the explicit arguments in `Exhibits/Renderer/README.md`.
That server's root is the evidence review; `/project-browser/` serves this application reference separately.

Texture Studio's standalone Material Library plan is `Documentation/Plans/Ongoing/TextureStudio.md`.
Native material authoring remains `.material` TOML; no Project Browser format migration was introduced.
