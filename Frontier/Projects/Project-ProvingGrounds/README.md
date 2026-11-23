# Project Proving Grounds — the first viewport scene

The spinning-cube test scene as a **project** (scenes live in projects, each with
its own code image; the engine keeps only shared code). The simulation lives here;
`Engine/Foundation/Math.h` and `Engine/Rendering` provide the shared maths,
ordering, aliasing and barrier machinery that will submit it.

| Entry | Role |
|---|---|
| `ProjectProvingGrounds.frontier` | descriptor (opening scene: ProvingGrounds) |
| `ProjectProvingGrounds.bat` | open script (forwards `--headless` etc.) |
| `Source/ProvingGrounds.h/.cpp` | procedural cube + ground plane, deterministic spin, render snapshots |
| `Source/ProvingGroundsInterchange.cpp` | v3 code image: host constructs/advances/retires the simulation |
| `Build.bat` | MSVC code-image build (mirrors Project Zero) |

Proof today: `Exhibits/RenderInfrastructure` drives this source directly
(validation + CPU capture in `Exhibits/ProgressEvidence.html`). On-screen Vulkan
rendering of the snapshot is the SDK-build remainder (plan Phase 3).
