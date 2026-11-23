# 🚗 Frontier JoltPhysics Vehicle Dynamics & ControlVehicle Validation Report

> **Status:** 🟢 Complete & Verified — All 4 Physics Validation Suites Passing (Zero Warnings, Zero Torn Readings)
> **Branch:** `arena/6e2e878f-frontier`
> **Physics Runtime:** JoltPhysics v5.6.0 (`PhysicsExtension` + Dedicated `PhysicsThread`)

---

## 🎯 Objectives Completed

1. **Vocabulary Governance & Wait-Free Publication Cleanup (`2a39c26`)**
   - Added `snapshot` to `Documentation/BannedWordsVocabulary.md` (§1.1, §2, §4, §5).
   - Replaced all cross-thread exchange and physics readout identifiers with Frontier-native `Reading` terminology (`PublishedReadingExchange`, `ReadingLease`, `ReadingEpochHeader`, `ConstraintListReading`, `RigidBodyReading`).
   - Eliminated external-engine vocabulary across headers, implementation files, exhibits, and plans.

2. **Full Vehicle Dynamics Solver in ISO C++20 on JoltPhysics v5.6.0**
   - Implemented zero-allocation, physics-thread-optimized vehicle specifications in `Engine/SimulationSpace/PhysicsSpace/VehicleSpecifications.h` and the 10-stage coupled Jolt solver `VehiclePhysicsRig` in `Engine/SimulationSpace/PhysicsSpace/VehicleDynamicsSolver.{h,cpp}`.
   - Extended `PhysicsExtension` (`PhysicsExtension.{h,cpp}`) with `ApplyForceAtPoint`, live `AssignBodyMassKg`, and self-ignoring `ExecuteRayCastIgnoringBody` for swept wheel-ground queries.
   - Completed all physics domains — including the previously unfinished **Supercharger**, **Turbocharger**, **Fuel Delivery**, and **6 Suspension Architectures** — and published `VehicleReading` via `ThreadBoundaryExchange` for wait-free consumption by Game, Render, Audio, and Editor threads.

3. **`ControlVehicle` Integration & 8-Suite Physics Verification**
   - Authored `Exhibits/ControlVehiclePhysicsValidation.cpp` driven directly by `ControlVehicleClassBuilder::ConstructControlVehicle` (`Experimental/Shared/Models/ControlVehicleBody.facet`, tyres, rim, springs, steering hinges, RearWing `Dock`, 6-speed gearbox, 18-point torque curve).

---

## ⚙️ Architecture & Completed Physics Domains

| Domain | Implementation | Key Physics & Performance Highlights |
| :--- | :--- | :--- |
| **Suspension Architectures (6)** | `SuspensionCornerSpecification`, `SolveSuspensionKinematics` | `Telescopic`, `DoubleWishbone`, `MacPhersonStrut`, `MultiLink`, `SolidAxle`, `TrailingArm` (+ `Manual`). Dynamic camber gain fed into Pacejka MF6.1, bump-toe, coupled solid-axle tilt, cubic progressive/digressive/dual-rate springs, asymmetric slow/fast bump/rebound dampers, rubber bump stops, and unit-consistent `N/m` anti-roll bars. |
| **Turbocharger** | `TurbochargerSpecification`, `ComputeForcedInduction` | Calibrated ball-bearing inertia (`0.00004 kg*m^2`) and realistic bearing windage (`~480 W @ 120k rpm`), Serrano internal heat transfer, octane-governed proportional wastegate, Blow-Off Valve surge relief on throttle lift, intercooler cooling, and physically exact absolute MAP (`101.325 + BoostBar * 100 kPa`, plus closed-throttle vacuum). |
| **Supercharger & Twincharged** | `SuperchargerSpecification`, `InductionCategory` | Full `Roots` (instant low-RPM positive displacement), `TwinScrew` (internal compression), and `Centrifugal` (`RPM^2` impeller) models with bypass valve modulation, crank parasitic drag torque, and charge heating. `Twincharged` mode seamlessly hands boost from supercharger to turbocharger via progressive bypass opening. |
| **Fuel Delivery & Live Mass** | `FuelDeliverySpecification`, `SolveFuelThermalBalance` | Tank capacity (`62 L`), fuel density (`0.745 kg/L`), lower heating value (`43.5 MJ/kg`), BSFC burn with boost AFR enrichment, idle flow, and deceleration fuel cut-off (DFCO). Dynamically updates Jolt rigid-body mass via `AssignBodyMassKg`, enforces octane boost ceiling, cuts combustion on starvation, and supports `RefuelTank` directives. |
| **Tire & Traction** | `PacejkaTireSpecification`, `SolveTireTraction` | Implicit Newton-Raphson combined-slip Pacejka MF6.1 (`Gx_alpha`, `Gy_kappa`), camber thrust, load sensitivity, 3-regime Karnopp stiction (`Kinetic`, `StaticLateral`, `StaticFull`) for rock-steady slope holding, and two-node tread/carcass thermal & wear grip scaling. |
| **Powertrain, Brakes & Assists** | `SolveDrivetrainTorque`, `SolveBrakeHydraulics` | 18-point torque curve, 6-speed automatic/DCT shift schedule with corner-hold lateral-G lock, limited-slip differential, hydraulic brakes with rotor thermal balance, ABS wheel-lock modulation, TCS, and ESC yaw stabilization. |
| **Body-Axis Aerodynamics** | `ComputeBodyAxisAerodynamics` | Velocity decomposed strictly in chassis body axes (`+X` forward, `+Y` up, `+Z` right), true underbody clearance ground-effect curve, and live RearWing `Dock` detachment reducing rear downforce. |
| **Cross-Thread Publication** | `ThreadBoundaryExchange<VehicleReading, VehicleControlDirective>` | Lock-free directive ingestion from caller threads and wait-free immutable `VehicleReading` leases on every Jolt physics step. |

---

## 📊 Verification Results

All four validation binaries built cleanly with zero compiler warnings in the new vehicle files and passed 100% across consecutive runs:

| Exhibit Executable | Status | Measured Metrics |
| :--- | :---: | :--- |
| `ControlVehiclePhysicsValidation` (8 suites, 28 checks, 3× consecutive runs) | 🟢 PASS | **Equilibrium:** `Y = 0.4171 m`, `SumFz = 12748.7 N` vs `12748.6 N` weight (`0.0008%` error), `IdleMAP = 32.0 kPa`, **`Step = 2.3 us`**. <br>**WOT 7 s:** `171.5 km/h`, Gear 4, `1.15 g`, Turbo `129,338 rpm` (`1.36 bar`, `MAP = 237.2 kPa`), BOV `1.00` (`1.36 -> 0.00 bar`), Aero `1482 N` (RearWing detach `1342 -> 126 N`). <br>**ABS Stop:** `-2.07 g`, rotor `293.1 -> 400.0 K`, final speed `0.032 m/s`, `StaticFull`. <br>**Superchargers:** Roots `0.77/0.96 bar` (`67.9 Nm` drag), TwinScrew `0.30/0.95 bar`, Centrifugal `0.05/0.58 bar`, Twincharged `1.16/1.36 bar` (`SC_Bypass = 1.00`). <br>**Fuel:** `0.35 -> 0.000 L`, live Jolt mass `1249.60 -> 1249.34 -> 1255.28 kg`, starved `-79.0 Nm` -> refueled `+652.6 Nm`. <br>**6 Suspensions over Curb:** DoubleWishbone `dCam = 0.0109 rad`, MultiLink `bumpToe = 0.0029 rad`, SolidAxle `tilt = 0.0526 rad`; all 6 architectures stable. <br>**Cornering & 10° Slope:** `1.04 g` lateral, `1.04 rad/s` yaw, Ackermann verified, tread `339.1 -> 384.0 K`; **10° slope creep `0.0015 m` over `2.5 s`** (`0.0000 m/s`). <br>**Live `PhysicsThread`:** `373–410` steps, `53.6–61.4 km/h`, **`AvgStep = 7.7–8.2 us`**, `>300k` concurrent Game/Render/Audio reads, **`0` violations**. |
| `CrossThreadConstraintValidation` (25 checks) | 🟢 PASS | 8 ProjectZero-parity constraint kinds + live `PhysicsThread` (`1306` steps, `0` races/torn reads). |
| `ComponentClassPhysicsValidation` (23 checks) | 🟢 PASS | `ControlVehicle` FCT1 load, `.ComponentSpace` round-trip, constraints, 1D powertrain parity. |
| `PhysicsSpaceValidation` | 🟢 PASS | JoltPhysics v5.6.0 broadphase, impulse, wind, bond rupture, proxy export. |

---

## 📁 Modified & Created Files

- `Engine/SimulationSpace/PhysicsSpace/VehicleSpecifications.h` — Zero-allocation ISO C++20 specifications & `VehicleFastMath`.
- `Engine/SimulationSpace/PhysicsSpace/VehicleDynamicsSolver.h` — `VehiclePhysicsRig`, `VehicleReading`, `WheelCornerReading`, `VehicleControlDirective` + preserved 1D powertrain evaluators.
- `Engine/SimulationSpace/PhysicsSpace/VehicleDynamicsSolver.cpp` — Full 10-stage Jolt vehicle solver & `ControlVehicle` specification builder.
- `Engine/SimulationSpace/PhysicsSpace/PhysicsExtension.{h,cpp}` — Added `ApplyForceAtPoint`, `AssignBodyMassKg`, `ExecuteRayCastIgnoringBody`.
- `Exhibits/ControlVehiclePhysicsValidation.cpp` & `CMakeLists.txt` — Registered 8-suite `ControlVehicle` + `PhysicsThread` validation target.
- `Documentation/Plans/VehiclePhysicsJoltMigrationPlan.md` & `Documentation/Plans/MarkdownStudioViewer.html` — Documentation & rebuilt studio viewer.
