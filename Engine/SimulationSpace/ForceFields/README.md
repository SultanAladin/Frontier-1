# 🧩 `Engine/SimulationSpace/ForceFields/`

> **TL;DR**: VectorFieldLang lexer, AST parser, type validator, CPU bytecode generator, and .slang GPU shader expression emitter (15 source/header/shader files under `Engine/SimulationSpace/ForceFields/`).

---

## Files

| File | Purpose |
| :--- | :--- |
| `FieldFormulaCompiler.cpp` | VectorFieldLang lexer, AST parser, type validator, CPU bytecode generator, and .slang GPU shader expression emitter |
| `FieldFormulaCompiler.h` | VectorFieldLang lexer, AST parser, type validator, CPU bytecode generator, and .slang GPU shader expression emitter |
| `FieldLatticeCascade.cpp` | 4-tier cascaded 3D GPU/CPU velocity & vorticity lattice (2m, 16m, 256m, 100km weather bounds) with SSBO packing |
| `FieldLatticeCascade.h` | 4-tier cascaded 3D GPU/CPU velocity & vorticity lattice (2m, 16m, 256m, 100km weather bounds) with SSBO packing |
| `ForceFieldAccumulator.cpp` | Fixed-step ForceFieldAccumulator evaluating active ForceSources & curl/divergence telemetry (no rigid-body physics coupling yet) |
| `ForceFieldAccumulator.h` | Fixed-step ForceFieldAccumulator evaluating active ForceSources, curl/divergence telemetry & PhysicsSpace rigid-body coupling |
| `ForceFieldDescriptors.h` | Shared C++20 descriptors, 19 ForceModel laws, Right-Hand +Z Up math primitives, motion paths, and 16B-aligned GPU layouts |
| `ForceMotionPath.cpp` | Deterministic 3D LineAB, Catmull-Rom spline, and helical orbit trajectory evaluator for ForceSourceRecordEntry |
| `ForceMotionPath.h` | Deterministic 3D LineAB, Catmull-Rom spline, and helical orbit trajectory evaluator for ForceSourceRecordEntry |
| `StreamTransportField.cpp` | Directed FlowField ForceModel laws: FlowStream (Current, ThermalUpdraft, SlipstreamJet, NavigationField, VesselWake), SplineForce, Adhesion |
| `StreamTransportField.h` | Directed FlowField ForceModel laws: FlowStream (Current, ThermalUpdraft, SlipstreamJet, NavigationField, VesselWake), SplineForce, Adhesion |
| `TurbulenceField.cpp` | Divergence-free ∇×(w·ψ) CurlNoise, AtmosphericGust, VorticityConfinement, and deterministic BrownianNoise laws |
| `TurbulenceField.h` | Divergence-free ∇×(w·ψ) CurlNoise, AtmosphericGust, VorticityConfinement, and deterministic BrownianNoise laws |
| `VectorField.cpp` | Shared spatial reach envelope attenuation helpers and analytical VectorField ForceModel laws (Right-Hand +Z Up) |
| `VectorField.h` | Shared spatial reach envelope attenuation helpers and analytical VectorField ForceModel laws (Right-Hand +Z Up) |

## Namespaces & Primary Types

- **Namespaces**: `Frontier::SimulationSpace::ForceFields`
- **Primary Types**: `LexTokenCategory`, `LexToken`, `ParsedScalarOutput`, `RecursiveFormulaParser`, `StackSlot`, `FormulaExpressionCategory`, `FormulaOpcode`, `FormulaInstruction`, `FormulaBytecodeProgram`, `FormulaCompileOutcome`, `FormulaPresetDescriptor`, `FieldLatticeTierDescriptor`, `FieldLatticeCascade`, `ForceFieldAccumulator`, `ForceModel`, `StreamTransportProfile`, `ForceDurationMode`, `ForceAttenuationMode` (+16 more)

## Usage

```cpp
#include "Engine/SimulationSpace/ForceFields/FieldFormulaCompiler.h"
#include "Engine/SimulationSpace/ForceFields/FieldLatticeCascade.h"
#include "Engine/SimulationSpace/ForceFields/ForceFieldAccumulator.h"

using namespace Frontier::SimulationSpace::ForceFields;
LexTokenCategory Instance {};
```

## Build & Verification

- Built as part of `FrontierEngine` in root `CMakeLists.txt` (ISO C++20).
- Validated by the repository's CTest suites under `Exhibits/` (`ctest --test-dir build-editor --output-on-failure`).
