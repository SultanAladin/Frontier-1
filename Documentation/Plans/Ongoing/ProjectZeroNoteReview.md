# Project Zero editor — 📝 settings review, one panel at a time

**Status: 🚧 waiting for your decisions.** Decision 4 was Option A: we go through one panel per round. Nothing is removed until
you say so. In the editor, every row below shows 📝 next to its label (headless screenshots can't draw the emoji and show a box instead).

Answer per row with **Keep** or **Remove**. You can also say "remove all on this panel".

## Round 1 — §1 Sun (next up)

| # | Field | Group | Why it is marked | Suggestion |
|---|-------|-------|------------------|------------|
| 1 | Elevation | Solved direction | Readout only. The specification lists manual azimuth / elevation, not the solved values | Keep: it shows where the sun ended up |
| 2 | Azimuth | Solved direction | Same as above | Keep |
| 3 | Declination | Solved direction | Solar declination readout, not in the specification | Remove |
| 4 | Equation of Time | Solved direction | Equation-of-time readout, not in the specification | Remove |

## Later rounds (queued in this order)

| Round | Panel | 📝 fields |
|-------|-------|-----------|
| 2 | §3 Stars | Sidereal Time (readout) |
| 3 | §4 Clouds | Owned wind component; Light Taps (tier readout) |
| 4 | §5 Local cloud | Owned wind component |
| 5 | §6 Atmosphere | White Line; Line at civil only; Air Mass; Tier Samples; Fetch Baked Dome; Dome Path; Sheet |
| 6 | §8 Lens flare | Halo Radius; Chromatic; Aperture Blades; Streak gain; Anamorphic / Streaks / Starburst switches; Ray pairs; Halo width; Preview X / Y |
| 7 | §10 Render | Quality tiers *Minimal* and *Reference* (the specification lists Low to Ultra); viewport HiZ overlay, Alias overlay, Patch error budget; debug views Visibility ID, Motion Vectors, HiZ (level 3), Reservoir M / W / Age, Patch Tiles, Tiles + Wireframe |
| 8 | §14 Project Browser (separate HTML prototype) | *Open from disk* button; CAD and Geometry filters |

Source of truth for the marks: `Experimental/ProjectZeroEditor/ProjectZeroSpecification.js` (`Notes` per panel), `DebugViews.js`,
`RenderSettings.js`, `Notch.jsx`, `Editor.jsx`, and `Experimental/Project browser/index.html`.
