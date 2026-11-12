# Vehicle Lab

A standalone vehicle inspection studio using the same dark, rounded-panel, Outliner / Stage / Inspector design language as the Particle and Photometric studios. This is the reference implementation for new remastered UI components; see the [shared UI Component Guidelines](../UI_COMPONENT_GUIDELINES.md). The centerpiece is a real-time 3D render of the Vehicle HMI's authored **ControlVehicle**; the right inspector exposes its engineering parameters, and a compact response dock below the Stage edits the engine, turbo and tyre curves.

![Vehicle Lab interface and ControlVehicle inspection stage](../screenshots/vehicle-lab.png)

## Launch

```bash
python3 "Experimental/VehicleLab/Serve.py"
# http://localhost:8086/ redirects into Vehicle Lab
```

The preview server binds to `0.0.0.0`. Vehicle Lab is a separate standalone app; the Experimental Remastered hub link is only a launcher.

## ControlVehicle asset

The WebGL viewport loads the actual ControlVehicle body, tyre and rim FCT1 streams from `Experimental/VehicleProvingGrounds/Archive/`—the same authored mesh assets used by Vehicle HMI. The inspector decodes their facet/material-family data and renders the assembled car; there is no drawn vehicle illustration or substitute mesh. Orbit with left-drag, pan with right-drag, scroll to zoom, or use the Side and Top camera presets. Select a component from the Outliner or directly in the viewport.

## Inspector features

- Chassis, powertrain, suspension, tyres, thermal and MF6.1 Pacejka values in editable pill sliders.
- Draggable engine-torque, turbo-multiplier and lateral Pacejka response curves with per-curve Linear, Smooth spline, Ease in/out or Step/hold interpolation between control points.
- Ten selectable Stage inspection channels: tyre and brake temperatures, contact patches, slip vectors, suspension travel, surface normals, wheel loads, ride height, aero load and drivetrain torque. Tyre, slip, spring, brake and load graphics sit outside the body at each wheel station so they remain readable.
- A drive-simulation preview uses the editable engine torque curve and interpolation, vehicle mass, final drive and throttle / brake inputs; it animates wheel spin and reports speed, RPM, acceleration and torque.
- Add Sensor, Camera, Ballast, Aero Surface and Light child components; choose a parent and mount point, tune their offsets, toggle visibility, and remove them without changing the shared source mesh.
- Font Awesome Free solid icons are self-hosted for offline use; see `Fonts/LICENSE-Font-Awesome.txt` for attribution and license terms.
- Local autosave and JSON setup / CSV curve export.

Run verification:

```bash
node "Experimental/VehicleLab/AssetInspectorValidation.mjs"
```
