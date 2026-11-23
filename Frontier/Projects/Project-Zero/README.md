# Project Zero — the reference project

A blank, known-good Frontier project: the window it opens is the shared
workspace (notch with the project name + dockable panels) carrying exactly
one panel today, the **Log Stream**. New projects are measured against this
one; when something works here it works everywhere.

## Open it three ways

| Route | How |
|---|---|
| Project Browser | run `Frontier.exe` with no arguments, pick **ProjectZero** — the browser window closes and the project window opens |
| Script | `ProjectZero.bat` (forwards extra arguments, e.g. `--headless`) |
| Command line | `Frontier.exe --project <path>\ProjectZero.frontier [--headless]` |

## What is in the folder

| Entry | Role |
|---|---|
| `ProjectZero.frontier` | descriptor (TOML): name, content location, code image, declared log categories |
| `ProjectZero.bat` | the open script every project carries |
| `Content/` | content this project adds; shared engine content is referenced, not copied (see `Content/README.md`) |
| `ContentManifest.toml` | engine content this project uses — staged beside the exe at build time, nothing more |
| `Source/ProjectZeroInterchange.cpp` | the project code image (v3 host C ABI): emits reference diagnostics, including the tree shedding leaves into its pink `Foliage` category |
| `Build.bat` | MSVC build of the code image into `Build/Output/` + manifest staging |
| `Saved/` (runtime, ignored) | session log + workspace layout written by the host |

## Honest status

The portable engine + Log Stream panel logic in this repository compile and
pass headless validation today (`Exhibits/LogStream/`). The windowed host
needs the interface SDK from the upstream checkout named in `Upstream.json`
(Engine/Interface, vendored ImGui/GLFW) plus MSVC and the Vulkan SDK;
without them the same executable runs the truthful headless session instead
of pretending to open a window.
