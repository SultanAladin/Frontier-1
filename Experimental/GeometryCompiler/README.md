# Geometry Compiler (HTML, Workspace 2 of 2)

Ground truth for `FrontierGeometryCompiler` ([plan](../../Documentation/Plans/GeometryCompilerPortPlan.md)). This folder
has two pages for now. **G0** merges them into one step shell with the studio tools inside the steps.

| Page | Origin | What it is |
| :--- | :--- | :--- |
| `Compile.html` | Split out of the Component Viewer (**M0**, 2026-11-15) | The 8-step **Geometry Compile** shell (Source & Units → Metadata). Standalone it opens on a start screen (import, samples, assets compiled this session). Framed by the Component Viewer it takes one session per `open` message and posts the compiled asset back. |
| `index.html` | Reference studio (`unassignedinbox/Frontier` @ `7eb76ce`) | Geometry Editor · UV · SDF · Icon studio, with `GeometryCompilerValidation.mjs` (6 suites). |

```
python3 Experimental/ComponentViewer/Serve.py                          # 8095: /Experimental/GeometryCompiler/Compile.html
python3 Experimental/GeometryCompiler/Serve.py                         # 8094: reference studio (index.html)
JSDOM_DIR=/tmp/smoke node Experimental/GeometryCompiler/Scripts/SmokeGeometryCompile.mjs
node Experimental/GeometryCompiler/GeometryCompilerValidation.mjs
```

## Compile.html files

| File | Role |
| :--- | :--- |
| `CompileMain.js` | Boot: compile context, start screen, standalone session list, framed hand-off |
| `GeometryCompile.js` | Import inspector UI (steps, preview modes, settings, exports) |
| `GeometryCompiler.css` | Step shell + start screen; the shared look is `../Shared/Studio/Studio.css` |
| `../Shared/GeometryCompile/` | `CompileSteps.js` (pipeline, no DOM), `CompileContext.js`, `CompileSources.js`, `CompileExchange.js`, `Engine/` |

## Hand-off (`CompileExchange.js`)

```text
 Component Viewer (host)                         Geometry Compiler (Compile.html in a frame)
   import / sample / Recompile ──► frame loads ─────► { type: 'ready' }
   { type: 'open', asset, recompile } ─────────────► session runs
   frame hides, asset stored / opened ◄──────────── { type: 'finish', asset, cancelled, open, existing }
```

Assets travel as structured clones, so typed arrays (positions, SDF volume, cluster levels) cross unchanged. In C++ this
becomes `CompiledGeometryExchange` between two executables.
