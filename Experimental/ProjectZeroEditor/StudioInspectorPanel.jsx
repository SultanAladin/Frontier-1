// Studio Inspector extensions reusing Project-Zero's existing native UI components
// (inspector-heading, generic-card, Control sliders/switches/selects/colours, TransformPanel).

import React from "react";
import { Control, Icon } from "./Inspectors.jsx";
import TransformPanel from "./TransformPanel.jsx";
import {
  VehiclePaintPresets,
  RimFinishPresets,
  FixtureStyles,
  CinemaCameraStyles,
  IesProfiles,
  ConfiguratorPresets,
  ComputeTripodMountHeight,
  ComputeAimAngles,
  HeroCarAimTarget,
} from "./StudioLobbyScene.js";

function NativeSlider(Label, Value, Min, Max, Unit, Decimals, OnChange) {
  return (
    <Control
      Field={{
        Label,
        Control: "Slider",
        Minimum: Min,
        Maximum: Max,
        Unit: Unit || "",
        Decimals: Decimals ?? 2,
        Default: Value,
      }}
      Value={Value}
      Change={OnChange}
    />
  );
}

function NativeSwitch(Label, Checked, OnChange) {
  return (
    <Control
      Field={{ Label, Control: "Switch", Default: Boolean(Checked) }}
      Value={Boolean(Checked)}
      Change={OnChange}
    />
  );
}

function NativeSelect(Label, Options, CurrentValue, OnChange) {
  const Idx = Math.max(0, Options.indexOf(CurrentValue));
  return (
    <Control
      Field={{ Label, Control: "Select", Options, Default: 0 }}
      Value={Idx}
      Change={(NextIdx) => OnChange(Options[NextIdx] || Options[0])}
    />
  );
}

function NativeColour(Label, Hex, OnChange) {
  return (
    <Control
      Field={{ Label, Control: "Colour", Default: Hex || "#ffffff" }}
      Value={Hex || "#ffffff"}
      Change={OnChange}
    />
  );
}

function NativeReadout(Label, Text) {
  return (
    <Control
      Field={{ Label, Control: "Readout", Default: Text }}
      Value={Text}
      Change={() => {}}
    />
  );
}

// Wrap Project-Zero's native TransformPanel so editing Rotation[1] updates YawDeg
// and Rotation[0] updates YokeTiltDeg / HeadTiltDeg / PitchDeg cleanly.
function StudioTransformCard({ Values, Change, ShowScale = false }) {
  const Yaw = Number(Values.Rotation?.[1] ?? Values.YawDeg ?? 0);
  const Pitch = Number(
    Values.YokeTiltDeg ?? Values.PitchDeg ?? Values.HeadTiltDeg ?? Values.Rotation?.[0] ?? 0,
  );
  const UniformScale = Number(Values.Scale ?? 1);
  const Normalized = {
    ...Values,
    Position: Array.isArray(Values.Position) ? Values.Position : [0, 0, 0],
    Rotation: [Pitch, Yaw, Number(Values.Rotation?.[2] ?? 0)],
    Scale: [UniformScale, UniformScale, UniformScale],
  };

  const HandleChange = (Key, Val) => {
    if (Key === "Position") {
      Change("Position", Val);
      return;
    }
    if (Key === "Rotation") {
      Change("Rotation", Val);
      Change("YawDeg", Val[1]);
      if (Values.ProceduralKind === "studio.light_fixture") Change("YokeTiltDeg", Val[0]);
      else if (Values.ProceduralKind === "studio.cinema_camera") Change("PitchDeg", Val[0]);
      else if (Values.ProceduralKind === "studio.tripod") Change("HeadTiltDeg", Val[0]);
      else if (!Values.ProceduralKind) {
        Change("InclineDeg", Val[0]);
        Change("BankDeg", Val[2]);
      }
      return;
    }
    if (Key === "Scale") {
      Change("Scale", Math.max(0.25, Math.min(2.0, Number(Val[0]) || 1)));
    }
  };

  const TransformRows = ShowScale
    ? [
        ["Position", "m", [0, 0, 0], -50, 50, 0.05],
        ["Rotation", "deg", [0, 0, 0], -360, 360, 1],
        ["Scale", "×", [0.76, 0.76, 0.76], 0.3, 2.0, 0.01],
      ]
    : [
        ["Position", "m", [0, 0, 0], -50, 50, 0.05],
        ["Rotation", "deg", [0, 0, 0], -360, 360, 1],
      ];

  return (
    <TransformPanel
      Values={Normalized}
      Change={HandleChange}
      Rows={TransformRows}
      Space="WORLD / STUDIO"
    />
  );
}

// Reuses Project-Zero native heading, TransformPanel, and Control cards for Studio Vehicles
export function StudioVehicleInspector({
  Subject,
  Values,
  Change,
  Hidden,
  ToggleHidden,
  OnFocus,
}) {
  return (
    <>
      <header className="inspector-heading">
        <div className="breadcrumbs">Inspector / Studio Vehicles</div>
        <h1>{Subject.Name}</h1>
        <p className="eyebrow">{Subject.Description}</p>
      </header>

      <StudioTransformCard Values={Values} Change={Change} ShowScale />

      <section className="generic-card entity-capabilities" data-card="Quick controls">
        <h4>QUICK CONTROLS</h4>
        <div className="studio-quick-bar">
          <button
            type="button"
            className={"studio-quick-btn" + (!Hidden ? " on" : "")}
            onClick={ToggleHidden}
          >
            {Hidden ? "Show Vehicle" : "Visible"}
          </button>
          {OnFocus && (
            <button type="button" className="studio-quick-btn" onClick={OnFocus}>
              Focus Camera
            </button>
          )}
        </div>
      </section>

      <details open className="generic-card">
        <summary>Terrain Stance & Incline</summary>
        {NativeSlider("Incline Pitch (Nose Up)", Values.InclineDeg ?? 0, -25, 25, "°", 1, (V) => {
          Change("InclineDeg", V);
          Change("Rotation", [V, Number(Values.YawDeg ?? 0), Number(Values.BankDeg ?? 0)]);
        })}
        {NativeSlider("Lateral Roll Bank", Values.BankDeg ?? 0, -20, 20, "°", 1, (V) => {
          Change("BankDeg", V);
          Change("Rotation", [Number(Values.InclineDeg ?? 0), Number(Values.YawDeg ?? 0), V]);
        })}
      </details>

      <details open className="generic-card">
        <summary>Automotive Paint & Clearcoat</summary>
        <div className="studio-swatch-row">
          {VehiclePaintPresets.map((Preset) => (
            <button
              key={Preset.Name}
              type="button"
              className={"studio-swatch" + (Values.PaintHex === Preset.Hex ? " active" : "")}
              title={Preset.Name}
              style={{ background: Preset.Hex }}
              onClick={() => {
                Change("PaintHex", Preset.Hex);
                Change("Metallic", Preset.Metallic);
                Change("Roughness", Preset.Roughness);
                Change("Clearcoat", Preset.Clearcoat);
              }}
            />
          ))}
        </div>
        {NativeColour("Base Paint", Values.PaintHex || "#111319", (V) => Change("PaintHex", V))}
        {NativeSlider("Metallic Flake", Values.Metallic ?? 0.84, 0, 1, "", 2, (V) => Change("Metallic", V))}
        {NativeSlider("Base Roughness", Values.Roughness ?? 0.14, 0.04, 0.65, "", 2, (V) => Change("Roughness", V))}
        {NativeSlider("Clearcoat Gloss", Values.Clearcoat ?? 1.0, 0, 1, "", 2, (V) => Change("Clearcoat", V))}
      </details>

      <details open className="generic-card">
        <summary>Forged Alloy Wheels</summary>
        <div className="studio-swatch-row">
          {RimFinishPresets.map((Rim) => (
            <button
              key={Rim.Name}
              type="button"
              className={"studio-swatch" + (Values.RimHex === Rim.Hex ? " active" : "")}
              title={Rim.Name}
              style={{ background: Rim.Hex }}
              onClick={() => Change("RimHex", Rim.Hex)}
            />
          ))}
        </div>
        {NativeColour("Rim Finish", Values.RimHex || "#1b1d22", (V) => Change("RimHex", V))}
      </details>
    </>
  );
}

// Reuses Project-Zero native heading, TransformPanel, and Control cards for Procedural Studio Rigs
export function StudioProceduralInspector({
  Subject,
  Rows,
  AllValues,
  Values,
  Change,
  Hidden,
  ToggleHidden,
  Select,
  OnFocus,
  OnAimAtHero,
}) {
  const Kind = Values.ProceduralKind || "studio.tripod";
  const SharedCount = Rows.filter(
    (R) => R.Panel === "studio-procedural" && AllValues[R.Id]?.ProceduralKind === Kind,
  ).length;
  const Children = Rows.filter((R) => R.Parent === Subject.Id);
  const CanAim =
    Kind === "studio.tripod" ||
    Kind === "studio.cinema_camera" ||
    Kind === "studio.studio_fan" ||
    (Kind === "studio.light_fixture" && Values.FixtureStyle !== "Overhead Softbox");

  return (
    <>
      <header className="inspector-heading">
        <div className="breadcrumbs">
          Inspector / Procedural · {Kind}
          {SharedCount > 1 ? ` · ${SharedCount} SHARED INSTANCES` : ""}
        </div>
        <h1>{Subject.Name}</h1>
        <p className="eyebrow">{Subject.Description}</p>
      </header>

      {Kind !== "studio.ambient" && <StudioTransformCard Values={Values} Change={Change} />}

      {Kind !== "studio.ambient" && (
        <section className="generic-card entity-capabilities" data-card="Quick controls">
          <h4>QUICK CONTROLS</h4>
          <div className="studio-quick-bar">
            <button
              type="button"
              className={"studio-quick-btn" + (!Hidden ? " on" : "")}
              onClick={ToggleHidden}
            >
              {Hidden ? "Show Rig" : "Visible"}
            </button>
            {OnFocus && (
              <button type="button" className="studio-quick-btn" onClick={OnFocus}>
                Focus Camera
              </button>
            )}
            {CanAim && OnAimAtHero && (
              <button
                type="button"
                className="studio-quick-btn"
                title="Compute exact Yaw & Pitch toward Hero Coupe"
                onClick={() => OnAimAtHero(Subject.Id)}
              >
                Aim at Hero Car
              </button>
            )}
          </div>
        </section>
      )}

      {Kind === "studio.tripod" && (
        <details open className="generic-card">
          <summary>Tripod Rig Geometry</summary>
          {NativeReadout("Mount Height", ComputeTripodMountHeight(Values).toFixed(2) + " m")}
          {NativeReadout("Shared Instances", String(SharedCount) + " rigs in scene")}
          {NativeSlider("Stand Height", Values.StandHeight ?? 1.55, 0.5, 3.0, "m", 2, (V) => Change("StandHeight", V))}
          {NativeSlider("Center Column Lift", Values.ColumnLift ?? 0.28, 0, 0.85, "m", 2, (V) => Change("ColumnLift", V))}
          {NativeSlider("Leg Spread", Values.LegSpreadDeg ?? 26, 14, 46, "°", 0, (V) => Change("LegSpreadDeg", V))}
          {NativeSlider("Tube Radius", Values.TubeRadius ?? 0.018, 0.01, 0.032, "m", 3, (V) => Change("TubeRadius", V))}
          {NativeSlider("Leg Segments", Values.LegSegments ?? 3, 2, 4, "", 0, (V) => Change("LegSegments", Math.round(V)))}
          {NativeSlider("Head Pitch Tilt", Values.HeadTiltDeg ?? 0, -45, 45, "°", 1, (V) => {
            Change("HeadTiltDeg", V);
            Change("Rotation", [V, Number(Values.Rotation?.[1] ?? Values.YawDeg ?? 0), 0]);
          })}
          {NativeSwitch("Mid-Level Spreader", Values.Spreader !== false, (V) => Change("Spreader", V))}
          {NativeSwitch("Rolling Studio Casters", Boolean(Values.Casters), (V) => Change("Casters", V))}
          {NativeSwitch("Matte Rubber Floor Mat", Boolean(Values.FloorMat), (V) => Change("FloorMat", V))}
        </details>
      )}

      {Kind === "studio.light_fixture" && (
        <details open className="generic-card">
          <summary>Light Modifier Geometry</summary>
          {NativeSelect("Fixture Style", FixtureStyles, Values.FixtureStyle || "Overhead Softbox", (V) => Change("FixtureStyle", V))}
          {NativeSlider("Aperture Width", Values.ApertureWidth ?? 1.5, 0.25, 9.0, "m", 2, (V) => Change("ApertureWidth", V))}
          {NativeSlider("Aperture Height", Values.ApertureHeight ?? 1.2, 0.25, 6.0, "m", 2, (V) => Change("ApertureHeight", V))}
          {NativeSlider("Housing Depth", Values.HousingDepth ?? 0.3, 0.08, 1.1, "m", 2, (V) => Change("HousingDepth", V))}
          {NativeSlider("Yoke Pitch Tilt", Values.YokeTiltDeg ?? 0, -45, 55, "°", 1, (V) => {
            Change("YokeTiltDeg", V);
            Change("Rotation", [V, Number(Values.Rotation?.[1] ?? Values.YawDeg ?? 0), 0]);
          })}
          {Values.FixtureStyle === "Barn-Door Fresnel" &&
            NativeSlider("Barn-Door Flare", Values.BarnDoorFlare ?? 34, 8, 75, "°", 0, (V) => Change("BarnDoorFlare", V))}
          {Values.FixtureStyle === "Parabolic Umbrella" &&
            NativeSlider("Umbrella Ribs", Values.UmbrellaRibs ?? 16, 8, 20, "", 0, (V) => Change("UmbrellaRibs", Math.round(V / 2) * 2))}
          {Values.FixtureStyle === "Overhead Softbox" &&
            NativeSlider("Ceiling Rigging Drop", Values.SuspensionDrop ?? 2.3, 0, 4.0, "m", 2, (V) => Change("SuspensionDrop", V))}
        </details>
      )}

      {Kind === "studio.cinema_camera" && (
        <details open className="generic-card">
          <summary>Cinema Camera Rig</summary>
          {NativeSelect("Camera Body Style", CinemaCameraStyles, Values.CameraStyle || "Cinema Matte Box", (V) => Change("CameraStyle", V))}
          {NativeSlider("Lens Barrel Length", Values.LensLength ?? 0.32, 0.12, 0.55, "m", 2, (V) => Change("LensLength", V))}
          {NativeSlider("Film Magazine Radius", Values.ReelRadius ?? 0.13, 0.05, 0.22, "m", 2, (V) => Change("ReelRadius", V))}
          {NativeSlider("Camera Pitch Tilt", Values.PitchDeg ?? 0, -35, 35, "°", 1, (V) => {
            Change("PitchDeg", V);
            Change("Rotation", [V, Number(Values.Rotation?.[1] ?? Values.YawDeg ?? 0), 0]);
          })}
          {NativeSwitch("Cinema Matte Box", Values.MatteBox !== false, (V) => Change("MatteBox", V))}
          {NativeSwitch("Top Carry Handle", Values.TopHandle !== false, (V) => Change("TopHandle", V))}
        </details>
      )}

      {Kind === "studio.cyclorama" && (
        <details open className="generic-card">
          <summary>Cyclorama Infinity Cove</summary>
          {NativeSlider("Cove Width", Values.CoveWidth ?? 11.4, 6.5, 18.0, "m", 1, (V) => Change("CoveWidth", V))}
          {NativeSlider("Floor Depth", Values.FloorDepth ?? 8.2, 5.0, 14.0, "m", 1, (V) => Change("FloorDepth", V))}
          {NativeSlider("Wall Height", Values.WallHeight ?? 5.6, 3.2, 8.0, "m", 1, (V) => Change("WallHeight", V))}
          {NativeSlider("Cove Curve Radius", Values.CoveRadius ?? 1.85, 0.6, 3.4, "m", 2, (V) => Change("CoveRadius", V))}
          {NativeSlider("Plaster Micro-Grain", Values.PlasterGrain ?? 0.38, 0, 1, "", 2, (V) => Change("PlasterGrain", V))}
          {NativeSwitch("Swept Side Cove Wings", Values.SideWings !== false, (V) => Change("SideWings", V))}
          {NativeColour("Cove Albedo", Values.SurfaceTint || "#bfc5ce", (V) => Change("SurfaceTint", V))}
        </details>
      )}

      {Kind === "studio.tiled_floor" && (
        <details open className="generic-card">
          <summary>Glossy Tiled Studio Floor</summary>
          {NativeSlider("Tile Pitch", Values.TileSize ?? 1.2, 0.5, 2.4, "m", 2, (V) => Change("TileSize", V))}
          {NativeSlider("Slate Reflection Gloss", Values.FloorGloss ?? 0.68, 0.05, 1.0, "", 2, (V) => Change("FloorGloss", V))}
          {NativeColour("Slate Tile Tint", Values.SurfaceTint || "#1c2027", (V) => Change("SurfaceTint", V))}
        </details>
      )}

      {Kind === "studio.ceiling_truss" && (
        <details open className="generic-card">
          <summary>Ribbed Ceiling & Rigging Truss</summary>
          {NativeSlider("Ceiling Height", Values.CeilingHeight ?? 5.95, 4.5, 8.5, "m", 2, (V) => Change("CeilingHeight", V))}
          {NativeSlider("Roof Rib Count", Values.RibCount ?? 12, 4, 22, "", 0, (V) => Change("RibCount", Math.round(V)))}
          {NativeSwitch("Structural Box Truss Grid", Values.TrussGrid !== false, (V) => Change("TrussGrid", V))}
        </details>
      )}

      {Kind === "studio.workstation" && (
        <details open className="generic-card">
          <summary>Studio Crew Props</summary>
          {NativeSwitch("Hairpin Tether Desk", Values.HasDesk !== false, (V) => Change("HasDesk", V))}
          {Values.HasDesk !== false && (
            <>
              {NativeSlider("Desk Width", Values.DeskWidth ?? 1.48, 0.8, 2.4, "m", 2, (V) => Change("DeskWidth", V))}
              {NativeSlider("Desk Height", Values.DeskHeight ?? 0.78, 0.6, 1.1, "m", 2, (V) => Change("DeskHeight", V))}
            </>
          )}
          {NativeSwitch("Pelican Flight Cases", Values.FlightCases !== false, (V) => Change("FlightCases", V))}
          {NativeSwitch("Coiled Floor Power Cables", Values.CoiledCables !== false, (V) => Change("CoiledCables", V))}
        </details>
      )}

      {Kind === "studio.studio_fan" && (
        <details open className="generic-card">
          <summary>Studio Wind Fan (Live Plant & Dust Blower)</summary>
          {NativeSlider("Wind Blow Speed", Values.WindSpeed ?? 0.82, 0, 2.0, "×", 2, (V) => Change("WindSpeed", V))}
          {NativeSlider("Gust Turbulence", Values.GustTurbulence ?? 0.55, 0, 1.5, "", 2, (V) => Change("GustTurbulence", V))}
          {NativeSlider("Fan Drum Radius", Values.FanRadius ?? 0.52, 0.3, 0.85, "m", 2, (V) => Change("FanRadius", V))}
          {NativeSlider("Stand Hub Height", Values.StandHeight ?? 1.28, 0.7, 2.4, "m", 2, (V) => Change("StandHeight", V))}
          {NativeSlider("Drum Pitch Tilt", Values.TiltDeg ?? -4, -30, 30, "°", 1, (V) => Change("TiltDeg", V))}
        </details>
      )}

      {Kind === "studio.dust_field" && (
        <details open className="generic-card">
          <summary>Sunbeam Dust Particles</summary>
          {NativeSlider("Particle Count", Values.ParticleCount ?? 280, 40, 480, "", 0, (V) => Change("ParticleCount", Math.round(V)))}
          {NativeSlider("Wind Drift Speed", Values.DriftSpeed ?? 0.72, 0.1, 2.0, "×", 2, (V) => Change("DriftSpeed", V))}
          {NativeSlider("Sunbeam Glow", Values.GlowIntensity ?? 1.05, 0.2, 2.0, "", 2, (V) => Change("GlowIntensity", V))}
          {NativeSlider("Mote Pixel Size", Values.MoteSize ?? 4.8, 2.0, 9.0, "px", 1, (V) => Change("MoteSize", V))}
        </details>
      )}

      {Kind === "studio.garage_shell" && (
        <details open className="generic-card">
          <summary>Racing Stadium Pit Garage</summary>
          {NativeSlider("Garage Width", Values.GarageWidth ?? 22.0, 16, 30, "m", 1, (V) => Change("GarageWidth", V))}
          {NativeSlider("Garage Depth", Values.GarageDepth ?? 18.0, 12, 26, "m", 1, (V) => Change("GarageDepth", V))}
          {NativeSlider("Ceiling Height", Values.GarageHeight ?? 7.15, 5.5, 9.5, "m", 2, (V) => Change("GarageHeight", V))}
          {NativeSlider("Stadium Sunlight", Values.BackdropBrightness ?? 1.32, 0.35, 2.4, "×", 2, (V) => Change("BackdropBrightness", V))}
          {NativeToggle("Two-Story Mezzanine Catwalks", Values.ShowMezzanine ?? true, (V) => Change("ShowMezzanine", V))}
          {NativeToggle("Overhead Linear Shop LEDs", Values.ShowLinearLeds ?? true, (V) => Change("ShowLinearLeds", V))}
        </details>
      )}

      {Kind === "studio.car_lift" && (
        <details open className="generic-card">
          <summary>Hydraulic 4-Post Car Lift</summary>
          {NativeSlider("Deck Lift Height", Values.LiftHeight ?? 2.12, 0.15, 2.6, "m", 2, (V) => Change("LiftHeight", V))}
          {NativeSlider("Steel Post Height", Values.PostHeight ?? 3.85, 2.8, 5.0, "m", 2, (V) => Change("PostHeight", V))}
        </details>
      )}

      {Kind === "studio.garage_props" && (
        <details open className="generic-card">
          <summary>Mechanics Workshop Equipment</summary>
          {NativeSelect(
            "Equipment Preset",
            Values.PropVariant || "tool-cart",
            [
              { Value: "tool-cart", Label: "Rolling Cherry-Red Tool Cart" },
              { Value: "tire-rack", Label: "Racing Slick Tire Rack & Jack" },
              { Value: "diag-cabinet", Label: "Diagnostic Telemetry Cabinet" },
            ],
            (V) => Change("PropVariant", V),
          )}
        </details>
      )}

      {Kind === "studio.garage_cables" && (
        <details open className="generic-card">
          <summary>Floor Air Hoses &amp; Cable Ramp</summary>
          {NativeSlider("Cable Run Span", Values.CableSpan ?? 11.5, 6.0, 18.0, "m", 1, (V) => Change("CableSpan", V))}
        </details>
      )}

      {Kind === "studio.rock_outcrop" && (
        <details open className="generic-card">
          <summary>Mossy Granite Rock Incline & Stream</summary>
          {NativeSlider("Rock Crest Height", Values.InclineHeight ?? 0.56, 0.2, 1.2, "m", 2, (V) => Change("InclineHeight", V))}
          {NativeSlider("Incline Slope", Values.InclineDeg ?? 8.5, -18, 20, "°", 1, (V) => Change("InclineDeg", V))}
          {NativeSlider("Cross Bank Angle", Values.BankDeg ?? 3.8, -14, 14, "°", 1, (V) => Change("BankDeg", V))}
          {NativeSlider("Boulder Scale", Values.RockScale ?? 1.0, 0.6, 1.5, "×", 2, (V) => Change("RockScale", V))}
          {NativeSwitch("Shallow Forest Stream & Pond", Values.HasStream !== false, (V) => Change("HasStream", V))}
        </details>
      )}

      {Kind === "studio.fallen_wood" && (
        <details open className="generic-card">
          <summary>Gnarled Fallen Tree Trunk</summary>
          {NativeSlider("Log & Root Scale", Values.LogScale ?? 1.05, 0.5, 1.8, "×", 2, (V) => Change("LogScale", V))}
        </details>
      )}

      {Kind === "studio.fern_cluster" && (
        <details open className="generic-card">
          <summary>Real 3D Forest Ferns (Wind-Reactive)</summary>
          {NativeSlider("Fern Rosettes", Values.FernCount ?? 15, 4, 18, "", 0, (V) => Change("FernCount", Math.round(V)))}
          {NativeSlider("Frond Scale", Values.FernScale ?? 1.05, 0.5, 1.7, "×", 2, (V) => Change("FernScale", V))}
        </details>
      )}

      {Kind === "studio.grass_plants" && (
        <details open className="generic-card">
          <summary>3D Grass Tufts & Woodland Shrubs</summary>
          {NativeSlider("Grass Tuft Density", Values.TuftCount ?? 56, 18, 72, "", 0, (V) => Change("TuftCount", Math.round(V)))}
          {NativeSlider("Plant Height Scale", Values.GrassScale ?? 1.05, 0.5, 1.7, "×", 2, (V) => Change("GrassScale", V))}
        </details>
      )}

      {Kind === "studio.forest_backdrop" && (
        <details open className="generic-card">
          <summary>Forest LED Volume Backdrop & Flags</summary>
          {NativeSlider("LED Backlight Brightness", Values.BackdropBrightness ?? 1.45, 0.3, 2.8, "×", 2, (V) => Change("BackdropBrightness", V))}
          {NativeSlider("LED Screen Width", Values.ScreenWidth ?? 12.4, 8.0, 17.0, "m", 1, (V) => Change("ScreenWidth", V))}
          {NativeSlider("LED Screen Height", Values.ScreenHeight ?? 6.2, 4.0, 7.2, "m", 1, (V) => Change("ScreenHeight", V))}
          {NativeSwitch("Flanking Negative-Fill Flags", Values.StudioFlags !== false, (V) => Change("StudioFlags", V))}
        </details>
      )}

      {Kind === "studio.ambient" && (
        <details open className="generic-card">
          <summary>Photographic Darkroom Exposure</summary>
          {NativeSlider("ACES Exposure", Values.Exposure ?? 1.16, 0.4, 2.4, "EV", 2, (V) => Change("Exposure", V))}
          {NativeSlider("Studio Bounce Fill", Values.AmbientFill ?? 0.14, 0.02, 0.45, "", 2, (V) => Change("AmbientFill", V))}
          {NativeSlider("Contact Shadow Depth", Values.ShadowDepth ?? 0.74, 0, 1, "", 2, (V) => Change("ShadowDepth", V))}
          {NativeSlider("Cinema Lens Vignette", Values.Vignette ?? 0.28, 0, 1, "", 2, (V) => Change("Vignette", V))}
          {NativeSwitch("Show Edit-Mode Light Rig Gizmos", Boolean(Values.ShowLightGizmos), (V) => Change("ShowLightGizmos", V))}
        </details>
      )}

      {Children.length > 0 && (
        <details open className="generic-card">
          <summary>Mounted Child Entities ({Children.length})</summary>
          <div className="studio-child-list">
            {Children.map((Child) => (
              <button
                key={Child.Id}
                type="button"
                className="studio-child-link"
                onClick={() => Select && Select(Child.Id)}
              >
                <span className="studio-child-title">
                  <Icon Name={Child.Icon} Size={18} />
                  <span>{Child.Name}</span>
                </span>
                <small>{Child.ReferenceType || Child.Panel}</small>
              </button>
            ))}
          </div>
        </details>
      )}
    </>
  );
}

// Add-on native card appended below Project-Zero's EXISTING Camera Inspector cards
export function StudioCameraExtensionCard({
  Subject,
  Values,
  Change,
  ActivePreset,
  SelectPreset,
  ReplayIntro,
}) {
  if (Subject.Id !== "cam-configurator") return null;
  return (
    <details open className="generic-card">
      <summary>Car Configurator Studio Rig</summary>
      {NativeSwitch("Showroom Orbit (Play Mode)", Values.ShowroomOrbit !== false, (V) => Change("ShowroomOrbit", V))}
      {NativeSlider("Orbit Speed", Values.OrbitSpeedDegPerSec ?? 7.5, 0, 30, "°/s", 1, (V) => Change("OrbitSpeedDegPerSec", V))}
      <div className="studio-quick-bar">
        {ConfiguratorPresets.map((Preset, Index) => (
          <button
            key={Preset.Id}
            type="button"
            className={"studio-quick-btn" + (ActivePreset === Index ? " on" : "")}
            onClick={() => SelectPreset && SelectPreset(Index)}
          >
            {Preset.Key} · {Preset.Label}
          </button>
        ))}
        <button type="button" className="studio-quick-btn" onClick={ReplayIntro}>
          ↺ Replay 3.8 s Intro
        </button>
      </div>
    </details>
  );
}

// Add-on native card appended below Project-Zero's EXISTING Light Inspector cards
export function StudioLightExtensionCard({ Subject, Rows, Values, Change, Select }) {
  const ParentRow = Subject.Parent ? Rows.find((R) => R.Id === Subject.Parent) : null;
  const IsIes = Subject.ReferenceType === "ieslight" || Values.LightKind === "IES";
  return (
    <details open className="generic-card">
      <summary>Studio Rig Coupling</summary>
      {NativeReadout("Direction Source", ParentRow ? "Locked to " + ParentRow.Name : "Local transform")}
      {IsIes && (
        <>
          {NativeSelect("Studio Polar Lobe", IesProfiles, Values.IesProfile || "Barn-Door Fresnel", (V) => Change("IesProfile", V))}
          {NativeSlider("Beam Edge Softness", Values.Softness ?? 0.35, 0.05, 0.95, "", 2, (V) => Change("Softness", V))}
        </>
      )}
      {ParentRow && (
        <div className="studio-child-list">
          <button
            type="button"
            className="studio-child-link"
            onClick={() => Select && Select(ParentRow.Id)}
          >
            <span className="studio-child-title">
              <Icon Name={ParentRow.Icon} Size={18} />
              <span>Select Parent · {ParentRow.Name}</span>
            </span>
            <small>Fixture</small>
          </button>
        </div>
      )}
    </details>
  );
}

export { ComputeAimAngles, HeroCarAimTarget };
