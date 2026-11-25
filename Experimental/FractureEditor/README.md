# Frontier · Fracture Studio (`Experimental/FractureEditor`)

Redesigned Sun Inspector + Fluid Editor studio interface wrapping all 6 `eb353da2` fracture engines and celestial lighting modules:

- **Six Fracture Solvers**: Fast Runtime Boussinesq Flat-BSP (`runtime_boussinesq`), Engineering Navier–Cauchy FEM + Bouchaud `H=0.78` Interlocking Fractography (`engineering_fem`), Griffith Stress-Budgeted Clipping (`griffith`), LEFM Mott Wavefront (`wavefront`), Clustered 3D Voronoi + Strain Bonds (`voronoi`), and Dendritic Lightning & Spall (`dendritic`).
- **Piece SDF Voxel Atlas**: `32³ / 64³ / 128³` R16F signed-distance brick occupancy matrix, live 2D iso-slice canvas & interactive Y-slice plane.
- **Celestial & Volumetrics Suite**: Directional Sun (15 analytical properties + interactive 24h diurnal arc & observer map), Sky Atmosphere (18 optical fields + 256×512 RGBA16F LUT preview), and Volumetric Clouds (Global World-Z Slab & Local Box Cage).
- **Zero-External-Dependency Viewport**: Bundled `three@0.187.0` + `OrbitControls` WebGL2 ACES viewport with 5 shading lenses (`PBR`, `X-Ray`, `Telemetry`, `Bond Plexus`, `SDF Slice`) and automatic software 3D fallback.

## Run & Validate

```bash
node "Experimental/FractureEditor/FractureValidation.mjs"
python3 "Experimental/FractureEditor/Serve.py"   # http://localhost:8082
```
