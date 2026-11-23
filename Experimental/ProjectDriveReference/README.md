# Project Drive source reference

These unmodified selected files come from `SultanAladin/Frontier-`, commit
`3f14808a6dcc909ae64070d62639a7019f5de794`, original path `Engine/PhysicalDynamics/Vehicle/`.
Their Git blob identities are recorded in `SourceManifest.txt`. The portable vehicle, drivetrain,
Pacejka and contact cores are separately copied to `Frontier/Engine/Physics/ProjectDrive/` for use
as the original hook-based solver; it is not the same solver as the HTML-demo `Frontier::VehicleSolver`.

Source validation harnesses are retained as references, not as real-world measured data. In this
revision `VehicleSceneValidation.cpp` does not compile because its WheelMount aggregate predates
the `SuspensionMount` field. `VehicleDrivingChecks.cpp` and `AbsSuperchargerChecks.cpp` build with
warnings in their source; their in-process box/ground mocks are not a Jolt integration.
