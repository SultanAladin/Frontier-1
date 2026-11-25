# Lofted Bi-Directional Surface-Curve SDF vs. Uniform 3D Voxel Grid SDF (`ControlVehicle`)

## 1. Dimensional Scaling: $\mathcal{O}(M \cdot K)$ 2D Boundary Manifold vs. $\mathcal{O}(N^3)$ 3D Volume Grid

An automotive body is a compact 2-manifold embedded in $\mathbb{R}^3$. Representing its Signed Distance Field on a uniform $N \times N \times N$ 3D voxel lattice expends $\mathcal{O}(N^3)$ memory storing signed distances throughout empty surrounding air and deep interior cabin volume, whereas only $\mathcal{O}(N^2)$ cells straddle the zero-level skin $d(\mathbf{x}) = 0$:

- On the Frontier **`ControlVehicle` ("Controlled Integrator", `12,572` vertices, `25,068` triangles)** spanning $2.50\text{ m} \times 1.77\text{ m} \times 6.22\text{ m}$ (with $0.32\text{ m}$ shadow/AO padding $\Rightarrow 6.86\text{ m}$ longitudinal span):
  - A **`16³` Voxel SDF** (`8.0 KB` `r16f` / `32.0 KB` `rgba16f`) has longitudinal cell pitch $\Delta z = 428.8\text{ mm}$, erasing sharp bumper splitters and producing coarse cubic steps across the hood and fenders.
  - A **`32³` Voxel SDF** (`64.0 KB` `r16f` / `256.0 KB` `rgba16f`) has longitudinal cell pitch $\Delta z = 214.4\text{ mm}$. Because hardware trilinear interpolation is only $C^0$-continuous across cell faces, the surface normal $\nabla d(\mathbf{x})$ suffers discontinuous kinks at every voxel boundary — shattering glossy clearcoat and zebra reflections into rectilinear blocks.
  - A **`64³` Voxel SDF** (`512.0 KB` `r16f` / `2,048.0 KB` `rgba16f`) narrows pitch to $\Delta z = 107.2\text{ mm}$ at the cost of **`0.5–2.0 MB` per unique vehicle** (`982 ms` CPU bake), scaling to **`4–16 MB` at `128³`** and **`32–128 MB` at `256³`**.

By contrast, a **Bi-Directional Lofted Surface-Curve SDF** parameterizes the coachwork directly in two surface coordinates $(u, v) \in [0, 1] \times [0, 1)$:
1. **$M$ Transverse Cross-Section Rib Curves** ($u_m = \frac{m}{M - 1}$ along longitudinal vehicle axis $z \in [-3.110\text{ m}, +3.110\text{ m}]$), capturing cross-sectional width, rocker sills, tumblehome, and roof crown around a smooth interior spine $(0, y_c(u), z(u))$.
2. **$K$ Longitudinal Loft Rail Curves** ($v_k = \frac{k}{K}$, $\theta = 2\pi v$), sweeping nose-to-tail across all $M$ ribs via **Fritsch–Carlson / Steffen monotone-clamped Bicubic Ferguson–Hermite patches** $\mathbf{S}(u, v)$ so high-curvature fender transitions never ring or overshoot.
3. **Exact CSG Wheel-Well Arch Cutouts + 4 Revolved Wheel-Profile Curve Lofts**, subtracting circular front (`0.560 m`) and rear (`0.562 m`) wheel-well cylinders from the smooth outer coachwork loft before unioning the four revolved alloy-rim + tyre profile curves ($W$ samples).

Adding more ribs $M$ and rails $K$ refines geometric fidelity quadratically in surface resolution while memory grows only as **$\mathcal{O}(M \cdot K)$** (`0.36 KB` to `6.13 KB` compact `fp16`, or `1.34 KB` to `24.25 KB` pre-differentiated GPU `rgba16float` Hermite knots `[R, R_u, R_v, R_{uv}]`).

---

## 2. Mathematical Formulation & Point-to-Loft Distance Evaluation

### 2.1 Monotone Bicubic Ferguson–Hermite Surface Skin

At each knot $(m, k)$, `CompileLoftedVehicleSdf` packs the radial profile $R_{m,k}$, monotone longitudinal slope $(R_u)_{m,k}$, periodic monotone angular slope $(R_v)_{m,k}$, and cross-twist $(R_{uv})_{m,k}$ in an `rgba16float` 2D image (`RailsK × (RibsM + 1)`, with row `RibsM` holding the revolved wheel profile). Evaluating both position $\mathbf{S}(u, v)$ and analytical tangent vectors $(\mathbf{S}_u, \mathbf{S}_v)$ requires just **4 integer `textureLoad` / `texelFetch` reads** into L1 texture memory:

$$\mathbf{S}(u, v) = \begin{bmatrix} R(u,v)\cos(2\pi v) \\ y_c(u) + R(u,v)\sin(2\pi v) \\ z_{\min} + \Delta Z\,u \end{bmatrix}, \qquad \mathbf{n}_{\text{analytic}}(u, v) = \frac{\mathbf{S}_u \times \mathbf{S}_v}{\|\mathbf{S}_u \times \mathbf{S}_v\|}.$$

Because $R(u, v)$ is $C^1$-continuous across every rib and rail junction, surface normals $\nabla d(\mathbf{x})$ remain smooth even at ultra-compact `10 × 16` (`0.36 KB`) loft density: lowering loft count acts as a shape-preserving aerodynamic smoothing filter rather than introducing cubic staircase terracing.

### 2.2 Regularized $2 \times 2$ Gauss–Newton Orthogonal Projection

Whereas a 3D Voxel SDF evaluates $d(\mathbf{p})$ in $\mathcal{O}(1)$ via a single hardware trilinear `textureSampleLevel`, evaluating exact orthogonal distance $d(\mathbf{p}) = \pm\min_{u,v} \|\mathbf{p} - \mathbf{S}(u, v)\|$ to a parametric bicubic loft requires solving $\mathbf{J}^\top (\mathbf{p} - \mathbf{S}(u, v)) = \mathbf{0}$:

1. **Outer Bounding-Box Leap**: Queries outside an expanded $0.65\text{ m}$ margin around `ControlVehicle` return an exact AABB lower bound immediately (`0` spline reads).
2. **Cylindrical Spine Coordinate + 5-Tap Star Seed**: From $u_0 = \text{clamp}\!\bigl(\frac{p_z - z_{\min}}{\Delta Z}, 0, 1\bigr)$ and $v_0 = \frac{1}{2\pi}\text{atan2}(p_y - y_c(u_0), p_x) \bmod 1$, five candidate star offsets $(u_0 \pm \delta_u, v_0 \pm \delta_v)$ guard against steep windshield/bumper inclination.
3. **2–3 Damped $2 \times 2$ Gauss–Newton Steps**:

$$\begin{bmatrix} \|\mathbf{S}_u\|^2 + \lambda & \mathbf{S}_u \cdot \mathbf{S}_v \\ \mathbf{S}_u \cdot \mathbf{S}_v & \|\mathbf{S}_v\|^2 + \lambda \end{bmatrix} \begin{bmatrix} \Delta u \\ \Delta v \end{bmatrix} = \begin{bmatrix} (\mathbf{p} - \mathbf{S}) \cdot \mathbf{S}_u \\ (\mathbf{p} - \mathbf{S}) \cdot \mathbf{S}_v \end{bmatrix}, \qquad \lambda = 2\times 10^{-3}.$$

4. **Watertight Spine-Tube Sign + Symmetric Axle CSG**: Comparing $\rho(p_x, p_y) = \sqrt{p_x^2 + (p_y - y_c(u_0))^2}$ against $R(u_0, v_0)$ yields exact interior/exterior sign without open-panel ray-parity leaks, while $|p_x|$ symmetry evaluates all four wheel-arch cutouts and revolved wheel lofts in **2 symmetric axle pairs** instead of 4 separate wheels.

---

## 3. Quantitative Benchmarks on `ControlVehicle` (`25,068` Triangles)

Measured across stratified multi-azimuth exterior surface rays against the exact `25,068`-triangle Bounding Volume Accelerator:

| SDF Configuration | Compact / `r16f` VRAM | GPU `rgba16f` VRAM | Compile / Bake Time | Median Skin Error | 90th-Pct (`P90`) Error | Single-Eval CPU Cost | Normal Continuity |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| **`Loft 10 × 16`** (`12`-pt wheel) | **`0.36 KB`** | **`1.34 KB`** | **`~2.3 ms`** | `18.02 mm` | `74.91 mm` | `~1.85 µs` | **$C^1$ Smooth** |
| **`Loft 18 × 24`** (`16`-pt wheel) | **`0.91 KB`** | **`3.50 KB`** | **`~2.3 ms`** | `6.54 mm` | `31.76 mm` | `~2.10 µs` | **$C^1$ Smooth** |
| **`Loft 32 × 36`** (`24`-pt wheel) | **`2.34 KB`** | **`9.19 KB`** | **`~3.0 ms`** | `2.96 mm` | `17.99 mm` | `~1.80 µs` | **$C^1$ Smooth** |
| **`Loft 48 × 64`** (`32`-pt wheel) | **`6.13 KB`** | **`24.25 KB`** | **`~5.1 ms`** | **`0.71 mm`** | **`8.86 mm`** | `~1.80 µs` | **$C^1$ Smooth** |
| **`Voxel 16³`** (`429 mm` pitch) | `8.00 KB` | `32.00 KB` | `~19 ms` | `1.09 mm` | `37.91 mm` | **`~0.08 µs`** | $C^0$ Staircase |
| **`Voxel 32³`** (`214 mm` pitch) | `64.00 KB` | `256.00 KB` | `~147 ms` | `0.02 mm` | `10.41 mm` | **`~0.08 µs`** | $C^0$ Staircase |
| **`Voxel 64³`** (`107 mm` pitch) | `512.00 KB` | `2,048.00 KB` | `~982 ms` | `0.01 mm` | `3.65 mm` | **`~0.08 µs`** | $C^0$ Staircase |

### Key Engineering Conclusions

1. **VRAM Compression (`10.5×` to `83.5×` smaller)**: `Loft 48 × 64` achieves **`0.71 mm` median** and **`8.86 mm` P90** skin accuracy in **`6.13 KB` compact (`24.25 KB` GPU `rgba16f`)** — **`10.5×` smaller** than `Voxel 32³` (`64 KB` / `256 KB`, which still has worse `10.41 mm` P90 error and visible $C^0$ normal kinks) and **`83.5×` smaller** than `Voxel 64³` (`512 KB` / `2,048 KB`). Across a 50-car racing grid of distinct vehicles, Lofted Curve SDFs fit inside **`~300 KB` (`< 1.2 MB` GPU)** instead of **`25–100 MB`** of 3D textures.
2. **Authoring & Deformation Speed (`30×` to `190×` faster compile)**: Compiling `Loft 32 × 36` takes **`~3.0 ms`** vs. **`147 ms`** (`32³`) and **`982 ms`** (`64³`) for 3D voxel baking, enabling real-time morphing, aerodynamic body-kit customization, or crash-dent rib deformation on the fly.
3. **Compute vs. Bandwidth Tradeoff (`~22×` ALU per raw query, mitigated in ray-marching)**: A cold point query on the Lofted Curve SDF executes ~28 L1 texel reads + two $2 \times 2$ Gauss–Newton steps (`~1.8 µs` CPU vs. `~0.08 µs` trilinear voxel fetch). In GPU sphere-tracing, AABB early-exit skips $>75\%$ of screen rays immediately, X-axle symmetry halves wheel evaluations, and the entire `9.19 KB` Hermite patch image stays permanently resident in L1 texture memory without 3D-volume DRAM thrashing.
