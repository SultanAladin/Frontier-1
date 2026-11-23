# InputProvider — Naming Decision

🧩 Records why the folder is **`InputProvider/`** (not `InputBackend/`) and why the component inside it is
named **`InputEncoder`**. Preserves the alternate component name (`PlatformInput`) that was also on the table.

---

## Folder: `InputProvider/` (was `InputBackend/`)

FolderStructure.md names this folder `InputBackend/`, but **`Backend` is a banned word** (per the migration
direction — it reads as a generic-programmer term like Manager/Handler). The folder was renamed to
**`InputProvider/`**: it is the platform-tier home for the input seam that provides normalized input to the
engine. When FolderStructure.md is next revised, its `InputBackend/` references (lines 121, 343) should follow
this rename.

> ⚠️ **`Provider` is itself on the SKILL-Naming §1.6 job-title blacklist.** It is retained here by an explicit
> user-granted exemption for this one folder — see `SYSTEM_NOMENCLATURE.md` §1 (row E1) at the project root,
> which records the authorization. This is the *only* sanctioned use of `Provider` in the engine; do not adopt
> it elsewhere.

## Component: `InputEncoder`

The folder was an empty scaffold (`.gitkeep`, 0 bytes) — no prior implementation to port, so the component was
written fresh and named for its actual responsibility.

### The two candidate names, and the rule that decided it

| Candidate | Applies when the component… | Verdict |
|---|---|---|
| `InputEncoder` | translates / serializes raw OS window events into normalized engine action streams | ✅ chosen |
| `PlatformInput` / `InputProvider` | directly interfaces with hardware devices or pumps the OS event loop | kept as alternate (and reused as the folder name) |

### Why `InputEncoder` and not `PlatformInput`

Raw OS event pumping **already exists** in the windowing seam:

- `Windowing/PlatformWindowWin32.cpp` handles `WM_INPUT` (raw mouse), `WM_KEYDOWN`/`WM_KEYUP`, buttons, and
  wheel, folding them into an `InputPacket` each `PollPlatformEvents`.
- The X11 / Wayland / Stub backends do the same behind the same seam.

A component that pumped the OS loop here would **duplicate** the window backend — a regression the migration
rules forbid. The genuine gap was the **downstream** half: turning the per-integration `InputPacket` (level state)
into the normalized signals game / UI code consumes — pressed / released **edges** and held duration.
That is exactly the "translates raw events into normalized engine action streams" branch → **`InputEncoder`**.

## Data flow

```
OS events ──▶ PlatformWindow (per-OS backend)  ──▶ InputPacket ──▶ InputEncoder ──▶ normalized actions
             pumps WM_INPUT / WM_KEY* etc.        (level state)    (edges + held)   (pressed/released/held)
             — the "provider" role, already done                     — the "encoder" role, added here
```

`InputPacket.h` itself lives one pillar over at `EngineContext/Input/InputPacket.h` (its home per
FolderStructure.md line 72); Platform reaches it through the `/I …/Internal` include root, so the include reads
`#include "EngineContext/Input/InputPacket.h"`.

## Contract summary

- `InitializeInputEncoder` — clean neutral integration.
- `IntegrateInputPacket` — advance one integration; seeds history on the first call so no spurious startup edge.
- Queries: `KeyPressed` / `KeyReleased` / `KeyHeldNow` / `KeyHeldDurationCount` and the `Button…` equivalents.
  (Names avoid the banned `Is` prefix, the past-tense `Was`, and the banned word `Frame` — the edge queries read
  as plain `Pressed` / `Released`; held duration is counted in integrations via `…HeldDurationCount`.)
- Zero per-integration allocation (fixed-size arrays mirroring `InputPacket`); no OS types; never pumps the OS loop.
