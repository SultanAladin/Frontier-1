# Project Drive vehicle core

This is the hook-based vehicle core from `SultanAladin/Frontier-` commit
`3f14808a6dcc909ae64070d62639a7019f5de794` (`Engine/PhysicalDynamics/Vehicle/`). It lives in
`Frontier::Vehicle`, distinct from the HTML-demo `Frontier::VehicleSolver`. The original core includes
the tyre contact fan, suspension, Project Drive's Pacejka-style force and aligning moment, transient
slip, engine/turbo/supercharger, clutch, gearbox, differential, braking/ABS, aerodynamics and thermal
state. The original unmodified files and hashes are retained in `Experimental/ProjectDriveReference/`.
Only explanatory comments were corrected in the imported files; behaviour has not been deliberately changed.

A host creates `Frontier::Vehicle::VehicleSolver`, supplies chassis-state, force, torque and ground-query
hooks, configures it (for the original vehicle use `ApplyGeometry` and `DrivingScheme::PacejkaDrivetrain`),
and calls `Step` once per fixed physics step before advancing its chassis. This module does not create a
Jolt body, connect itself to the existing demo vehicle, or make the editor/game use it automatically.
`TyreVisualBridge` optionally converts Project Drive wheel telemetry into the current VehicleHMI demo's
projection or solid **visual** shapes. The host supplies each wheel's gauge pressure; no visual solver
reaction is fed into Project Drive's physics. Call `Reset` after building/respawning and `Advance` after
physics updates. The spring deformation path can be evaluated separately from wheel telemetry.

Build it as an opt-in CMake static library (`cmake -S Frontier/Engine/Physics/ProjectDrive -B <build>`),
or run `bash Exhibits/VehiclePhysics/RunProjectDriveValidation.sh` for source comparisons, original
invariants and mock-chassis scenarios. None is a calibrated real-world tyre comparison or a complete
Jolt/editor acceptance test. Project Drive's Newton slip mode is a reconstruction of GRIT's algorithm,
not an independently verified bit-faithful GRIT port. The original project also has host/editor and
other systems not included in this portable core.
