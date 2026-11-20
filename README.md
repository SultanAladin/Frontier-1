# Frontier

General Purpose Editor.

## Editor prototypes

This checkout contains exactly two browser editor prototypes, copied from
`SultanAladin/Frontier` at branch `arena/8391e994-frontier`
(commit `63db1201d0cea199c7276c2126e62812b04821b9`):

- `Experimental/ProjectZeroEditor` — canonical HTML/JS reference specification
  for the native editor UI (trapezoidal docks, Notch drawer, outliner, inspector).
- `Experimental/FrontierEditor` — Vite/React scene inspector, CAD workspace,
  icon galleries and companion design studies.

### Run locally

```sh
# Project Zero Editor (plain HTTP, no dependencies)
python3 Experimental/ProjectZeroEditor/Serve.py 8092

# Frontier Editor (Vite/React)
cd Experimental/FrontierEditor
npm ci
npm run dev -- --port 5173
```

Both servers bind to `0.0.0.0` and accept all hosts, so they can also be opened
through an Arena live preview. Frontier Editor serves `/`, `/cad.html`,
`/icons.html`, `/collection-icon-options.html`, and `/cluster-lod.html`.
