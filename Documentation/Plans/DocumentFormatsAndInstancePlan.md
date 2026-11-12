# Document Containers, Sub-Formats & Instance Concepts

> **Purpose:** Conceptual blueprint for how Frontier persists assets on disk, encodes/decodes `.Space` containers, exchanges external formats, records resolution-independent edit history within fixed memory budgets, and organizes world/workspace instances without polymorphic bloat.
> *(Purely conceptual reference — folder layouts live in `FolderStructure.md` and `DocumentSpacesAndRevisionPlan.md`.)*

---

## 1. Foundational Concept: One Global File Extension + Embedded Sub-Formats

Every native Frontier asset on disk uses a **Two-Tier Hybrid Container** under **one Global Extension** (following **Option 1A** natural domain mix and **Option 2A** case-insensitive recognition on load with canonical **PascalCase** on write):

- **Primary `.Space` Containers:** `.ProjectSpace` · `.FontSpace` · `.ColourSpace` · `.GeometrySpace` · `.UVSpace` · `.TextureSpace` · `.MaterialSpace` · `.ImageSpace` · `.VectorSpace` · `.SimulationSpace` · `.VolumetricSpace` · `.ParticleSpace`
- **Complementary Domain Containers (Option 1A):** `.Pigment` · `.Sketch` · `.Assembly` · `.Motion` · `.Render` · `.Photometry` · `.Terrain` · `.Acoustic`

```text
+======================================================================================+
| TIER 1 — HUMAN-READABLE UTF-8 TOML MANIFEST (Top of File)                            |
|   • Document identity, schema version, authoring units, and colour primaries         |
|   • Analytic domain sub-formats (e.g. .ColourSpace, .BSDF, .Microfacet, .UVTile)     |
|   • Binary Section Directory (TTF/SFNT-style offset, byte length, 64-bit checksum)   |
+======================================================================================+
| 64-BYTE ALIGNMENT BOUNDARY (Explicit ASCII delimiter + zero-padding to 64 B)         |
+======================================================================================+
| TIER 2 — PACKED 64-BYTE-ALIGNED BINARY SECTIONS (Bottom of File)                     |
|   • Parametric baseline payload, tiled numeric streams, or mip/tile blocks           |
|   • Embedded '.Revision' parametric operation stream (<= 215 MiB per document)       |
+======================================================================================+
```

- **Case-Insensitive Load, Canonical PascalCase Write (Option 2A):** Opening `.projectspace` or `.ProjectSpace` (or `.imagespace` / `.ImageSpace`) works identically across Windows, Linux, and Android; newly saved containers write canonical PascalCase extensions.
- **Inspectable & Zero-Parse Fast:** Plain-text editors and Git diffs read the TOML manifest at the top, while heavy binary sections sit after a `64-byte` boundary with exact byte offsets and 64-bit checksums for instant random-access GPU/CPU reads.

---

## 2. Foundational Concept: Codecs (`Encoder` + `Decoder`) vs. `Interchange` vs. `Exchange`

Frontier separates file and runtime payload movement into three clear conceptual roles:

| Concept | Role | Direction & Examples |
| :--- | :--- | :--- |
| **Codec (`Encoder` + `Decoder`)** | Reads (`Decoder`) and writes (`Encoder`) **native Frontier `.Space` containers** and `.Revision` binary sections with zero loss. | Project, Font, Colour, Geometry, UV, Texture, Material, Image, Vector, Simulation, Volumetric, and Particle codecs. |
| **Interchange** | Translates **external third-party formats or hardware API payloads** into and out of native Frontier domains. | External geometry (OBJ, glTF, FBX, STEP), images (EXR, PNG, HDR, KTX2), materials (MaterialX), vector (SVG), fonts (TTF/OTF), and Vulkan GPU hardware interchange. |
| **Exchange** | Performs **live bi-directional runtime transfer** across OS boundaries or between active in-memory workspaces without disk round-trips. | OS peripheral/cursor/haptic input exchange, structured clipboard exchange, and live UV ↔ Texture ↔ Material workspace synchronization. |

---

## 3. Foundational Concept: Resolution-Independent Revision & Memory Caps

Documents never retain baked pixel dumps or fixed-density polygon snapshots per undo step. Every workspace records **parametric operations** — compact mathematical descriptions of *what changed*:

- **Paint & Texture (`.Pigment` / `.TextureSpace`):** Normalized `[0, 1]` spline control points, stylus pressure/tilt curves, brush formulation, UDIM target, and deterministic PRNG seed.
- **UV & Image (`.UVSpace` / `.ImageSpace`):** Seam cuts, pinned `.UVTile` island coordinates, relaxation parameters, or non-destructive exposure/chromaticity adjustments.
- **Solid & Geometry (`.GeometrySpace` / `.Sketch` / `.Assembly`):** Analytic primitive parameters (box, cylinder, cone, frustum, sphere), curve constraints, watertight sewing tolerances, and parametric bevel/extrude readings.

| Budget Scope | Default Limit | Behavior & Guarantees |
| :--- | :---: | :--- |
| **Per-Document Revision Limit** | **`215 MiB`** | Dedicated parametric operation budget per open document — holds `100,000+` compact operations. |
| **Global Application Undo Limit** | **`750 MiB`** | Shared ceiling across all open documents in the session, ordered by a monotonic cross-document sequence stamp. |
| **Overflow Compaction** | **Redo Purge → Prefix Fold** | When either ceiling is reached: ① inactive redo branches are purged first; ② oldest prefix operations fold into the parametric baseline so **arbitrary-resolution reconstruction never breaks**. |

---

## 4. Foundational Concept: Lean Domain Instances (Replacing `UObject` Bloat)

Instead of burdening every instance with virtual dispatch tables, garbage-collection headers, and scattered heap allocations, Frontier groups **Instances** contiguously by category:

1. **Compact Identity (`~32 B`, Zero Virtual Overhead):** Generation-checked token, domain category tag, visibility/lock directives, non-kinship index links (enclosing, first-nested, next-peer), and transform slot index.
2. **Contiguous Category Arrays:** Geometry, Material, Texture, Image, Photometric, Terrain, Volumetric, Particle, Player, Agent, Trigger, Audio, and Projectile instances live in cache-contiguous storage.
3. **Selective Lifecycle Ticks:** Passive asset instances pay zero per-tick cost; only active simulation and interactive categories register in lifecycle tick sequences.

---

## 5. Master Global Extensions & Embedded Sub-Format Summary

| Domain | Canonical Global Extension | Embedded Sub-Format Sections | Conceptual Payload |
| :--- | :--- | :--- | :--- |
| **Project Configuration** | **`.ProjectSpace`** *(reads `.projectspace`)* | `.VisualAppearance` · `.FontSpace` · `.WorkspaceLayout` · `.InputSettings` · `.NotificationSettings` | Theme swatches, UI scale, font selections, workspace splits, shortcut chords, alert preferences |
| **Colour Science** | **`.ColourSpace`** | `.Primaries` · `.WhitePoint` · `.TransferCurve` · `.Gamut` · `.Spectral` | CIE XYZ, ACEScg, Rec.2020, Oklab primaries, chromatic adaptation, and electro-optical curves |
| **Typography** | **`.FontSpace`** | `.TypefaceGroup` · `.WeightList` · `.Metrics` | Scalable vector typefaces and optical weight mappings |
| **Geometry** | **`.GeometrySpace`** | `.Primitives` · `.Points` · `.Corners` · `.Facets` · `.Edges` · `.Normals` · `.Tangents` · `.UV0..UV7` · `.Creases` · `.UDIM` · `.UVTile` · `.Revision` | Analytic solid primitives, watertight sewing, multi-domain geometric attributes, seam/crease directives, and parametric edit log |
| **UV Parameterization** | **`.UVSpace`** | `.UDIM` · `.UVTile` · `.TexelDensity` · `.Seams` · `.Revision` | Integer `[TileU, TileV]` UVTile grids, `1001`-series UDIM addressing, seam cuts, conformal/relaxed island coordinates |
| **Textures** | **`.TextureSpace`** | `.ColourSpace` · `.UDIM` · `.UVTile` · `.MipChain` · `.ChannelPack` · `.BlockEncoding` · `.Revision` | Multi-UDIM channel-packed GPU texture sets, texel density targets, and procedural/baked channel layers |
| **Materials & Shading** | **`.MaterialSpace`** | `.ColourSpace` · `.BSDF` · `.BSSRDF` · `.Microfacet` · `.Fresnel` · `.Anisotropy` · `.ThinFilm` · `.Clearcoat` · `.Sheen` · `.Transmission` · `.Dispersion` · `.Absorption` · `.Subsurface` · `.ClosureNetwork` · `.LayerStack` · *(embedded `.TextureSpace` / `.ImageSpace`)* · `.Revision` | Physical reflection/transmission/subsurface math, closure networks, and embedded texture channels |
| **Images** | **`.ImageSpace`** | `.ColourSpace` · `.Exposure` · `.Tone` · `.HDRI` · `.LayerStack` · `.Tiles` · `.Revision` | Colour-managed SDR/HDR multi-channel tiled images and non-destructive radiometric adjustments |
| **Vector Graphics** | **`.VectorSpace`** | `.Curve` · `.Stencil` · `.LayerStack` · `.ColourSpace` · `.Revision` | Exact Bézier/polynomial contours, boolean winding, and variable-width strokes |
| **Simulation** | **`.SimulationSpace`** | `.RigidDynamics` · `.Constraints` · `.Collision` · `.Fluid` · `.ForceField` · `.Motion` · `.Revision` | Deterministic rigid, constraint, multiphase fluid, and spatial force-field solver setups |
| **Volumetrics** | **`.VolumetricSpace`** | `.SignedDistance` · `.DensityGrid` · `.Absorption` · `.Scatter` · `.Phase` · `.Cloud` · `.Revision` | Sparse signed-distance volumes, heterogeneous density grids, and participating-media parameters |
| **Particles** | **`.ParticleSpace`** | `.SpawnSource` · `.Field` · `.Integration` · `.Fibre` · `.Trail` · `.Revision` | GPU compute particulate sources, curl/rotational vector fields, trails, and solver parameters |
| **Natural Media Paint** | **`.Pigment`** | `.Stroke` · `.Brush` · `.Bristle` · `.Dab` · `.PigmentRamp` · `.PaintLayer` · `.Stylus` · `.ColourSpace` · `.Revision` | Natural media bristle/dab dynamics, stylus response curves, and resolution-free stroke splines |
| **CAD Solid & Sketch** | **`.Assembly`** · **`.Sketch`** | `.Part` · `.Solid` · `.BRep` · `.Feature` · `.Sketch` · `.Section` · `.Curve` · `.Revision` | Exact analytic 2D/3D curves, dimensional constraints, watertight solids, booleans, bevels, mates |
| **Motion · Render · World · Audio** | **`.Motion`** · **`.Render`** · **`.Photometry`** · **`.Terrain`** · **`.Acoustic`** | Domain-specific analytic sections + `.Revision` | Keyframe FCurves, physical camera/integrator queues, `.IES` luminaires, terrain ecosystems, spatial acoustics |
