# ShadowBox — top-down 2D level editor (`Experimental/ShadowBoxEditor`)

A concept tool for drawing **top-down worlds / levels for vehicles** — a 2D
"shadow-box" (blockout) stage — with props, splines, labels and design notes.
The chrome follows the **Project-Zero editor** in
`Experimental/ProjectZeroEditor` (trapezoidal dock tabs, top notch, bottom
asset drawer, outliner / viewport / inspector workspace).

![UI family] Same dark workspace: left **Outliner**, centre **Viewport**,
right **Inspector**, top-centre **notch** (file actions), bottom-centre
**Prop Library** drawer.

## Run

```bash
python3 Experimental/ShadowBoxEditor/Serve.py 8098
```

Opens the editor at `/` and exposes the level API:

| Route | Purpose |
| --- | --- |
| `GET /api/levels` | list saved `levels/*.json` |
| `GET /api/levels/<file>` | load one level |
| `POST /api/save` | `{ "name": "my-level", "doc": {...} }` → writes `levels/<slug>.json` |

Saved JSON lands in **`Experimental/ShadowBoxEditor/levels/`** inside the
repository, so a normal `git add … && git push` publishes it and the level can
be read straight from GitHub.

## Tools

| Tool | Key | What it does |
| --- | --- | --- |
| Select | `V` | pick / move / edit; drag spline points, `Alt`-click a point to remove it, double-click a path to insert a point |
| Terrain | `B` | freehand ground strokes (grass, dirt, sand, stone, mud, asphalt) painted under everything |
| Spline | `S` | **the important one** — click to lay control points for roads, rivers, trails and vehicle routes; `Enter`/double-click finishes, `Backspace` removes the last point |
| Prop | `P` | place rocks, trees, plants, logs, cones… from the Prop Library drawer (`Shift` = no scatter jitter) |
| Label | `L` | text callouts on the map |
| Note | `N` | design notes — what something does, how it should be built |

Other keys: `Ctrl+Z` / `Ctrl+Shift+Z` undo-redo · `Ctrl+S` save to repo ·
`Del` delete · `F` fit view · `G` grid · `Esc` cancel · wheel zoom ·
middle-drag or `Space`-drag pan.

## Level JSON

```jsonc
{
  "format": "shadowbox.level",
  "version": 1,
  "name": "River Crossing",
  "settings": { "grid": 32, "snap": true, "sunAngle": 50, "shadowLength": 0.7 },
  "layers": {
    "terrain":  [ { "id", "material", "points": [[x, y], …], "width" } ],
    "splines":  [ { "id", "name", "kind": "road|river|path|route", "points": [{x, y}], "width", "closed", "tension" } ],
    "props":    [ { "id", "kind", "x", "y", "rotation", "scale" } ],
    "labels":   [ { "id", "text", "x", "y", "size", "colour" } ],
    "notes":    [ { "id", "title", "body", "x", "y" } ]
  }
}
```

Props render as **animated top-down SVG** (wind sway on foliage) with
**directional shadows** driven by `settings.sunAngle` / `shadowLength` —
always laid flat, never billboarded, because the view is top-down.

`levels/example-crossroads.json` is a worked example (river, road, trail,
race route, bridge, foliage, labels + notes).
