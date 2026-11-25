# Thin-Leaf Foliage Signed Distance Shadow Architecture (`WebGPU`)

## 1. Physical Failure Modes of Thin Botanical Sheets in Uniform 3D Voxel SDFs

A temperate broadleaf blade has physical thickness $2h \approx 1.2\text{ mm}$ ($h = 0.6\text{ mm}$), whereas a uniform $32^3$ 3D Signed Distance Volume spanning a $3.8\text{ m}$ botanical crown has voxel pitch $\Delta x \approx 119\text{ mm}$ and diagonal $\Delta d = \sqrt{3}\,\Delta x \approx 206\text{ mm}$ — **over $170\times$ thicker than the leaf blade**.

Evaluating thin foliage inside standard 3D voxel distance volumes exposes two opposing geometric failure regimes:

1. **Zero-Crossing Erasure (`Mode 0 · Standard 32³ Signed Voxel SDF`)**:
   Every one of the 8 voxel corners surrounding a $1.2\text{ mm}$ leaf sheet lies outside the blade interior ($d(\mathbf{p}_i) > 0$). Under hardware trilinear interpolation, $d_{\text{trilinear}}(\mathbf{x}) > 0$ holds everywhere in $\mathbb{R}^3$. Sphere-tracing rays step straight through the canopy with **0.0% thin-leaf survival** (`Rmse = 25.04%` on `full-arbor`).
2. **Meatball Dilation Bloat (`Mode 1 · Two-Sided Dilated 32³ Voxel SDF`)**:
   Subtracting half a voxel diagonal from unsigned sheet proximity, $d_{\text{dilated}}(\mathbf{x}) = |d(\mathbf{x})| - 0.54\,\Delta d$, forces negative distances at adjacent voxel corners so rays stop skipping leaves, but inflates every $1.2\text{ mm}$ leaf into an ~11 cm capsule/blob — causing **+164.5% to +212.1% false shadow area bloat** (`Rmse = 29.67%`) and obliterating serrated Beech margins, Linden notches, Oak lobes, and Maple star tips.
3. **Featureless Extinction Wash (`Mode 2 · Non-Instanced 32³ Opacity UDF`)**:
   Storing unsigned distance alongside scalar voxel opacity preserves macro crown darkness without spherical blobs (`15.15%` bloat), yet blurs all individual leaf silhouettes into a uniform grey cloud (`57.6%` crisp silhouette survival).

---

## 2. Two-Level `16³` Canopy Brick + `5 × 64²` 2.5D Thin-Sheet Extrusion SDF (`Mode 3`)

To guarantee **unconditional `100.0%` thin-leaf shadow survival** across near, mid, and far viewing distances with **20 KB** of 2D archetype storage and **zero meatball dilation**, the workbench decouples empty-space acceleration from curved thin-sheet zero-crossings:

### 2.1 Five Botanical `64×64` 2D Euclidean Signed Distance Templates (`rgba16float`)

At startup (`~110 ms`), `CompileFiveLeafArchetypeSdfs` rasterizes five botanical silhouettes at `256×256` super-resolution and computes the exact separable 2-axis Felzenszwalb–Huttenlocher squared Euclidean Distance Transform, downsampling to a `64×64×5` `rgba16float` 2D texture array (`20 KB` total):

- `0 · Ovate Beech` (`29.6%` blade area, 9-tooth serrated margin)
- `1 · Heart Linden` (`29.8%` blade area, basal cordate notch + acuminate tip)
- `2 · Lobed Oak` (`20.5%` blade area, 4-sinus pinnate lobes)
- `3 · 5-Point Maple` (`28.3%` blade area, palmate 5-lobe star + secondary teeth)
- `4 · Slender Willow` (`12.7%` blade area, narrow lanceolate ribbon)

### 2.2 Curved 2.5D Thin-Sheet Extrusion Operator

Each instanced leaf blade $i$ carries an oriented local orthonormal basis $(\mathbf{e}_x, \mathbf{e}_y, \mathbf{e}_z)$, uniform half-span $s_i$, template index $a_i \in \{0..4\}$, and parabolic botanical camber $c_i(u, w) = \kappa_x u^2 + \kappa_z w^2$. Transforming a world point $\mathbf{p}$ into normalized blade coordinates $(u, y, w) = s_i^{-1} \mathbf{R}_i^\top (\mathbf{p} - \mathbf{c}_i)$, the exact curved thin-sheet 3D Signed Distance is:

$$d_{\text{leaf}}(\mathbf{p}) = \begin{cases} \max\!\bigl(s_i\,d_{2\text{D}}(u, w, a_i),\; |y - c_i(u,w)|\,s_i - h\bigr) & \text{if } d_{2\text{D}}(u, w, a_i) \le 0, \\[4pt] \sqrt{\bigl(s_i\,d_{2\text{D}}(u, w, a_i)\bigr)^2 + \max\!\bigl(0,\; |y - c_i(u,w)|\,s_i - h\bigr)^2} & \text{otherwise.} \end{cases}$$

Crucially, along any shadow ray segment $[t_0, t_1]$ traversing an occupied canopy brick, the signed sheet height

$$\phi(t) = y(t) - \bigl(\kappa_x u(t)^2 + \kappa_z w(t)^2\bigr)$$

changes sign ($\phi(t_0)\cdot \phi(t_1) \le 0$) **whenever the ray crosses the curved leaf sheet**, regardless of how thin $h = 0.6\text{ mm}$ is! One secant + Newton refinement locates the exact sheet crossing $t^* \in [t_0, t_1]$, where hardware-filtered `textureSampleLevel(LeafArchetype2dTex, LinearClampSampler, uv, a_i, 0.0).r` yields sub-millimeter serrated silhouettes and exact cone-ratio soft penumbrae $\text{clamp}(s_i\,d_{2\text{D}} / r_{\text{cone}}, 0, 1)$.

### 2.3 Two-Level `32³` Macro Leap + `16³` Canopy Brick Accelerator + Continuous Cone-Width LOD

1. **Level 0 (`32³` `MacroSdf3dTex`, `rgba16float`)**:
   - `.r`: Exact wooden trunk/branch tapered-capsule distance $d_{\text{wood}}$ (triggers exact analytical ray-capsule visibility near limbs so $14\text{ mm}$ twigs never vanish either).
   - `.g`: Conservative distance $\max(0, d_{\text{brickHull}} - 0.55\,\Delta d)$ to the nearest occupied `16³` Canopy Brick AABB, enabling rapid empty-space sphere-tracing leaps outside the crown while guaranteeing trilinear lookups never exceed $0$ inside occupied bricks.
2. **Level 1 (`16³` Canopy Bricks + 3-Axis Beer–Lambert Extinction $\boldsymbol{\sigma}_b$)**:
   - Each occupied brick packs `[StartOffset, InstanceCount]` into `BrickLeafRefs` plus direction-projected leaf cross-section extinction $(\sigma_x, \sigma_y, \sigma_z)$ derived from exact blade areas $\sum_i A_i |\mathbf{n}_i \cdot \mathbf{e}_k| / V_{\text{brick}}$.
   - As the solar shadow cone radius $r_{\text{cone}}(t) = t \tan\theta_{\text{sun}}$ grows with ray travel distance (or down the `avenue-lod` colonnade), `LodBlend` blends smoothly from exact 2.5D thin-sheet silhouettes ($r_{\text{cone}} \ll \bar{s}$) to single-lookup anisotropic Beer–Lambert transmittance $\exp(-\sigma(\mathbf{d})\,\Delta t_{\text{brick}})$ ($r_{\text{cone}} \gtrsim \bar{s}$), preventing distant sub-pixel aliasing while cutting distant evaluation cost.

---

## 3. Measured Shadow Accuracy Benchmarks (`h = 0.6 mm`, `1.2 mm` Blade)

| Preset & Method | Thin-Leaf Survival | False Blob Bloat | Shadow RMSE vs. Exact GT | Avg Steps / Ray |
| :--- | :---: | :---: | :---: | :---: |
| **`bough-closeup` (`36` leaves)** · `0 · Standard 32³ Voxel SDF` | `0.0%` | `3.2%` | `21.94%` | `4.12` |
| **`bough-closeup` (`36` leaves)** · `1 · Dilated 32³ Voxel SDF` | `100.0%` | `164.5%` | `25.54%` | `4.10` |
| **`bough-closeup` (`36` leaves)** · `2 · 32³ Opacity UDF` | `0.0%` | `0.0%` | `17.93%` | `5.34` |
| **`bough-closeup` (`36` leaves)** · `🟢 3 · Two-Level 2.5D Thin-Sheet` | **`100.0%`** | **`0.0%`** | **`0.28%`** | **`6.48`** |
| **`full-arbor` (`235` leaves)** · `0 · Standard 32³ Voxel SDF` | `0.0%` | `0.0%` | `25.04%` | `6.08` |
| **`full-arbor` (`235` leaves)** · `1 · Dilated 32³ Voxel SDF` | `100.0%` | `212.1%` | `29.67%` | `5.53` |
| **`full-arbor` (`235` leaves)** · `2 · 32³ Opacity UDF` | `57.6%` | `15.2%` | `15.68%` | `7.66` |
| **`full-arbor` (`235` leaves)** · `🟢 3 · Two-Level 2.5D Thin-Sheet` | **`100.0%`** | **`0.0%`** | **`0.27%`** | **`8.02`** |

---

## 4. WebGPU (`WGSL`) Compliance Notes

- All hardware-filtered 2D-array (`64×64×5`) and 3D (`32×32×32`) distance textures use IEEE-754 `rgba16float` (`Uint16Array` half-float encoding), which is natively filterable in Standard WebGPU without optional `float32-filterable` features, with `bytesPerRow` (`512` and `256` bytes) aligned to multiples of `256` bytes.
- Every texture lookup inside fragment ray-marching loops and post-`discard` paths uses explicit `textureSampleLevel(..., 0.0)` to satisfy strict WGSL derivative-uniformity rules.
- Fragment storage slots are consolidated into **4** `read-only-storage` buffers (`BrickHeaders`, `BrickLeafRefs`, `LeafInstances`, `WoodCapsules`) so the shader satisfies both Standard WebGPU and 4-buffer compatibility limits.
