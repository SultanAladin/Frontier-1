import React from "react";
import {
  VehiclePaintPresets,
  RimFinishPresets,
  FixtureStyles,
  CinemaCameraStyles,
  IesProfiles,
  ComputeTripodMountHeight,
} from "./StudioLobbyScene.js";

function SliderRow({ Label, Value, Min, Max, Step = 0.01, Unit = "", OnChange }) {
  const Num = Number(Value ?? Min);
  return (
    <label className="studio-prop-row">
      <div className="studio-prop-label">
        <span>{Label}</span>
        <strong>
          {Number.isFinite(Num) ? Num.toFixed(Step >= 1 ? 0 : Step >= 0.1 ? 1 : 2) : "0"}
          {Unit}
        </strong>
      </div>
      <input
        type="range"
        min={Min}
        max={Max}
        step={Step}
        value={Num}
        onChange={(E) => OnChange(parseFloat(E.target.value))}
      />
    </label>
  );
}

function ToggleRow({ Label, Checked, OnChange }) {
  return (
    <label className="studio-prop-toggle">
      <span>{Label}</span>
      <input
        type="checkbox"
        checked={Boolean(Checked)}
        onChange={(E) => OnChange(E.target.checked)}
      />
    </label>
  );
}

function TransformSection({ Values, Change }) {
  const Pos = Values.Position || [0, 0, 0];
  const SetAxis = (Axis, Val) => {
    const Next = [...Pos];
    Next[Axis] = Val;
    Change("Position", Next);
  };
  return (
    <section className="studio-inspector-section">
      <header>Transform</header>
      <SliderRow Label="Position X" Value={Pos[0]} Min={-12} Max={12} Step={0.05} Unit=" m" OnChange={(V) => SetAxis(0, V)} />
      <SliderRow Label="Position Y" Value={Pos[1]} Min={0} Max={6} Step={0.05} Unit=" m" OnChange={(V) => SetAxis(1, V)} />
      <SliderRow Label="Position Z" Value={Pos[2]} Min={-10} Max={10} Step={0.05} Unit=" m" OnChange={(V) => SetAxis(2, V)} />
      <SliderRow Label="Yaw Rotation" Value={Values.YawDeg ?? 0} Min={-180} Max={180} Step={1} Unit="°" OnChange={(V) => Change("YawDeg", V)} />
    </section>
  );
}

export function StudioVehicleInspector({ Subject, Values, Change, OnFocus }) {
  return (
    <div className="studio-inspector">
      <div className="studio-inspector-banner vehicle">
        <div>
          <span className="studio-eyebrow">CONTROLLED INTEGRATOR · 25,068 TRIS</span>
          <h3>{Subject.Name}</h3>
          <p>{Subject.Description}</p>
        </div>
        {OnFocus && (
          <button type="button" className="studio-focus-btn" onClick={OnFocus}>
            Focus
          </button>
        )}
      </div>

      <section className="studio-inspector-section">
        <header>Automotive Paint & Clearcoat</header>
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
          <input
            type="color"
            aria-label="Custom vehicle paint colour"
            className="studio-color-input"
            value={Values.PaintHex || "#111319"}
            onChange={(E) => Change("PaintHex", E.target.value)}
          />
        </div>
        <SliderRow Label="Metallic Flake" Value={Values.Metallic ?? 0.84} Min={0} Max={1} Step={0.01} OnChange={(V) => Change("Metallic", V)} />
        <SliderRow Label="Base Roughness" Value={Values.Roughness ?? 0.14} Min={0.04} Max={0.65} Step={0.01} OnChange={(V) => Change("Roughness", V)} />
        <SliderRow Label="Clearcoat Gloss" Value={Values.Clearcoat ?? 1.0} Min={0} Max={1} Step={0.01} OnChange={(V) => Change("Clearcoat", V)} />
      </section>

      <section className="studio-inspector-section">
        <header>Forged Alloy Wheels</header>
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
        <SliderRow Label="Vehicle Scale" Value={Values.Scale ?? 0.76} Min={0.5} Max={1.15} Step={0.01} Unit="×" OnChange={(V) => Change("Scale", V)} />
      </section>

      <TransformSection Values={Values} Change={Change} />
    </div>
  );
}

export function StudioProceduralInspector({ Subject, Rows, AllValues, Values, Change, Select, OnFocus }) {
  const Kind = Values.ProceduralKind || "studio.tripod";
  const SharedCount = Rows.filter(
    (R) => R.Panel === "studio-procedural" && AllValues[R.Id]?.ProceduralKind === Kind,
  ).length;
  const Children = Rows.filter((R) => R.Parent === Subject.Id);

  return (
    <div className="studio-inspector">
      <div className="studio-inspector-banner procedural">
        <div>
          <span className="studio-eyebrow">
            PROCEDURAL COMPONENT · {Kind.toUpperCase()}
            {SharedCount > 1 ? ` · ${SharedCount} SHARED INSTANCES` : ""}
          </span>
          <h3>{Subject.Name}</h3>
          <p>{Subject.Description}</p>
        </div>
        {OnFocus && Kind !== "studio.ambient" && (
          <button type="button" className="studio-focus-btn" onClick={OnFocus}>
            Focus
          </button>
        )}
      </div>

      {Kind === "studio.tripod" && (
        <section className="studio-inspector-section">
          <header>
            Tripod Geometry · Mount Height {ComputeTripodMountHeight(Values).toFixed(2)} m
          </header>
          <SliderRow Label="Stand Height" Value={Values.StandHeight ?? 1.55} Min={0.5} Max={3.0} Step={0.02} Unit=" m" OnChange={(V) => Change("StandHeight", V)} />
          <SliderRow Label="Center Column Lift" Value={Values.ColumnLift ?? 0.28} Min={0} Max={0.85} Step={0.02} Unit=" m" OnChange={(V) => Change("ColumnLift", V)} />
          <SliderRow Label="Leg Spread Angle" Value={Values.LegSpreadDeg ?? 26} Min={14} Max={46} Step={1} Unit="°" OnChange={(V) => Change("LegSpreadDeg", V)} />
          <SliderRow Label="Tube Radius" Value={Values.TubeRadius ?? 0.018} Min={0.01} Max={0.032} Step={0.001} Unit=" m" OnChange={(V) => Change("TubeRadius", V)} />
          <SliderRow Label="Telescoping LegSegments" Value={Values.LegSegments ?? 3} Min={2} Max={4} Step={1} OnChange={(V) => Change("LegSegments", V)} />
          <SliderRow Label="Head Pitch Tilt" Value={Values.HeadTiltDeg ?? 0} Min={-45} Max={45} Step={1} Unit="°" OnChange={(V) => Change("HeadTiltDeg", V)} />
          <ToggleRow Label="Mid-Level Spreader" Checked={Values.Spreader !== false} OnChange={(V) => Change("Spreader", V)} />
          <ToggleRow Label="Rolling Studio Casters" Checked={Boolean(Values.Casters)} OnChange={(V) => Change("Casters", V)} />
          <ToggleRow Label="Matte Rubber Floor Mat" Checked={Boolean(Values.FloorMat)} OnChange={(V) => Change("FloorMat", V)} />
        </section>
      )}

      {Kind === "studio.light_fixture" && (
        <section className="studio-inspector-section">
          <header>Light Modifier Geometry</header>
          <label className="studio-prop-select">
            <span>Fixture Style</span>
            <select
              value={Values.FixtureStyle || "Overhead Softbox"}
              onChange={(E) => Change("FixtureStyle", E.target.value)}
            >
              {FixtureStyles.map((Style) => (
                <option key={Style} value={Style}>{Style}</option>
              ))}
            </select>
          </label>
          <SliderRow Label="Aperture Width" Value={Values.ApertureWidth ?? 1.5} Min={0.25} Max={9.0} Step={0.05} Unit=" m" OnChange={(V) => Change("ApertureWidth", V)} />
          <SliderRow Label="Aperture Height" Value={Values.ApertureHeight ?? 1.2} Min={0.25} Max={6.0} Step={0.05} Unit=" m" OnChange={(V) => Change("ApertureHeight", V)} />
          <SliderRow Label="Housing Depth" Value={Values.HousingDepth ?? 0.3} Min={0.08} Max={1.1} Step={0.02} Unit=" m" OnChange={(V) => Change("HousingDepth", V)} />
          <SliderRow Label="Yoke Pitch Tilt" Value={Values.YokeTiltDeg ?? 0} Min={-60} Max={45} Step={1} Unit="°" OnChange={(V) => Change("YokeTiltDeg", V)} />
          {Values.FixtureStyle === "Barn-Door Fresnel" && (
            <SliderRow Label="Barn-Door Flare" Value={Values.BarnDoorFlare ?? 34} Min={8} Max={75} Step={1} Unit="°" OnChange={(V) => Change("BarnDoorFlare", V)} />
          )}
          {Values.FixtureStyle === "Parabolic Umbrella" && (
            <SliderRow Label="Umbrella Ribs" Value={Values.UmbrellaRibs ?? 16} Min={8} Max={20} Step={2} OnChange={(V) => Change("UmbrellaRibs", V)} />
          )}
          {Values.FixtureStyle === "Overhead Softbox" && (
            <SliderRow Label="Ceiling Rigging Drop" Value={Values.SuspensionDrop ?? 2.3} Min={0} Max={4.0} Step={0.05} Unit=" m" OnChange={(V) => Change("SuspensionDrop", V)} />
          )}
        </section>
      )}

      {Kind === "studio.cinema_camera" && (
        <section className="studio-inspector-section">
          <header>Cinema Camera Rig</header>
          <label className="studio-prop-select">
            <span>Camera Body Style</span>
            <select
              value={Values.CameraStyle || "Cinema Matte Box"}
              onChange={(E) => Change("CameraStyle", E.target.value)}
            >
              {CinemaCameraStyles.map((Style) => (
                <option key={Style} value={Style}>{Style}</option>
              ))}
            </select>
          </label>
          <SliderRow Label="Lens Barrel Length" Value={Values.LensLength ?? 0.32} Min={0.12} Max={0.55} Step={0.01} Unit=" m" OnChange={(V) => Change("LensLength", V)} />
          <SliderRow Label="Film Magazine Radius" Value={Values.ReelRadius ?? 0.13} Min={0.05} Max={0.22} Step={0.01} Unit=" m" OnChange={(V) => Change("ReelRadius", V)} />
          <ToggleRow Label="Cinema Matte Box" Checked={Values.MatteBox !== false} OnChange={(V) => Change("MatteBox", V)} />
          <ToggleRow Label="Top Carry Handle" Checked={Values.TopHandle !== false} OnChange={(V) => Change("TopHandle", V)} />
        </section>
      )}

      {Kind === "studio.cyclorama" && (
        <section className="studio-inspector-section">
          <header>Cyclorama Infinity Cove</header>
          <SliderRow Label="Stage Width" Value={Values.CoveWidth ?? 11.4} Min={6.5} Max={18.0} Step={0.2} Unit=" m" OnChange={(V) => Change("CoveWidth", V)} />
          <SliderRow Label="Floor Depth" Value={Values.FloorDepth ?? 8.2} Min={5.0} Max={14.0} Step={0.2} Unit=" m" OnChange={(V) => Change("FloorDepth", V)} />
          <SliderRow Label="Wall Height" Value={Values.WallHeight ?? 5.6} Min={3.2} Max={8.0} Step={0.1} Unit=" m" OnChange={(V) => Change("WallHeight", V)} />
          <SliderRow Label="Cove Curve Radius" Value={Values.CoveRadius ?? 1.85} Min={0.6} Max={3.4} Step={0.05} Unit=" m" OnChange={(V) => Change("CoveRadius", V)} />
          <SliderRow Label="Plaster Micro-Grain" Value={Values.PlasterGrain ?? 0.38} Min={0} Max={1} Step={0.02} OnChange={(V) => Change("PlasterGrain", V)} />
          <ToggleRow Label="Swept Side Cove Wings" Checked={Values.SideWings !== false} OnChange={(V) => Change("SideWings", V)} />
          <label className="studio-prop-select">
            <span>Cove Surface Albedo</span>
            <input
              type="color"
              className="studio-color-input"
              value={Values.SurfaceTint || "#bfc5ce"}
              onChange={(E) => Change("SurfaceTint", E.target.value)}
            />
          </label>
        </section>
      )}

      {Kind === "studio.tiled_floor" && (
        <section className="studio-inspector-section">
          <header>Glossy Tiled Studio Floor</header>
          <SliderRow Label="Tile Pitch" Value={Values.TileSize ?? 1.2} Min={0.5} Max={2.4} Step={0.05} Unit=" m" OnChange={(V) => Change("TileSize", V)} />
          <SliderRow Label="Slate Reflection Gloss" Value={Values.FloorGloss ?? 0.68} Min={0.05} Max={1.0} Step={0.02} OnChange={(V) => Change("FloorGloss", V)} />
          <label className="studio-prop-select">
            <span>Slate Tile Tint</span>
            <input
              type="color"
              className="studio-color-input"
              value={Values.SurfaceTint || "#1c2027"}
              onChange={(E) => Change("SurfaceTint", E.target.value)}
            />
          </label>
        </section>
      )}

      {Kind === "studio.ceiling_truss" && (
        <section className="studio-inspector-section">
          <header>Ribbed Ceiling & Rigging Truss</header>
          <SliderRow Label="Ceiling Height" Value={Values.CeilingHeight ?? 5.95} Min={4.5} Max={8.5} Step={0.05} Unit=" m" OnChange={(V) => Change("CeilingHeight", V)} />
          <SliderRow Label="Roof Rib Count" Value={Values.RibCount ?? 12} Min={4} Max={22} Step={1} OnChange={(V) => Change("RibCount", V)} />
          <ToggleRow Label="Structural Box Truss Grid" Checked={Values.TrussGrid !== false} OnChange={(V) => Change("TrussGrid", V)} />
        </section>
      )}

      {Kind === "studio.workstation" && (
        <section className="studio-inspector-section">
          <header>Studio Crew Props</header>
          <ToggleRow Label="Hairpin Tether Desk" Checked={Values.HasDesk !== false} OnChange={(V) => Change("HasDesk", V)} />
          {Values.HasDesk !== false && (
            <>
              <SliderRow Label="Desk Width" Value={Values.DeskWidth ?? 1.48} Min={0.8} Max={2.4} Step={0.05} Unit=" m" OnChange={(V) => Change("DeskWidth", V)} />
              <SliderRow Label="Desk Height" Value={Values.DeskHeight ?? 0.78} Min={0.6} Max={1.1} Step={0.02} Unit=" m" OnChange={(V) => Change("DeskHeight", V)} />
            </>
          )}
          <ToggleRow Label="Pelican Flight Cases" Checked={Values.FlightCases !== false} OnChange={(V) => Change("FlightCases", V)} />
          <ToggleRow Label="Coiled Floor Power Cables" Checked={Values.CoiledCables !== false} OnChange={(V) => Change("CoiledCables", V)} />
        </section>
      )}

      {Kind === "studio.ambient" && (
        <section className="studio-inspector-section">
          <header>Photographic Darkroom Exposure</header>
          <SliderRow Label="ACES Exposure" Value={Values.Exposure ?? 1.16} Min={0.4} Max={2.4} Step={0.02} Unit=" EV" OnChange={(V) => Change("Exposure", V)} />
          <SliderRow Label="Studio Bounce Fill" Value={Values.AmbientFill ?? 0.14} Min={0.02} Max={0.45} Step={0.01} OnChange={(V) => Change("AmbientFill", V)} />
          <SliderRow Label="Contact Shadow Depth" Value={Values.ShadowDepth ?? 0.74} Min={0.0} Max={1.0} Step={0.02} OnChange={(V) => Change("ShadowDepth", V)} />
          <ToggleRow Label="Show Edit-Mode Light Rig Gizmos" Checked={Boolean(Values.ShowLightGizmos)} OnChange={(V) => Change("ShowLightGizmos", V)} />
        </section>
      )}

      {Children.length > 0 && (
        <section className="studio-inspector-section">
          <header>Mounted Child Entities ({Children.length})</header>
          <div className="studio-child-list">
            {Children.map((Child) => (
              <button
                key={Child.Id}
                type="button"
                className="studio-child-link"
                onClick={() => Select && Select(Child.Id)}
              >
                <span>{Child.Name}</span>
                <small>{Child.Panel}</small>
              </button>
            ))}
          </div>
        </section>
      )}

      {Kind !== "studio.ambient" && <TransformSection Values={Values} Change={Change} />}
    </div>
  );
}

export function StudioLightInspectorBlock({ Subject, Values, Change }) {
  const IsIes = Values.LightKind === "IES" || Values.LightType === 1 || Subject.Icon === "editor-spot-light";
  return (
    <div className="studio-inspector studio-light-override">
      <section className="studio-inspector-section">
        <header>
          Live WebGL2 Studio {IsIes ? "Photometric IES Light" : "Rectangular Area Light"}
        </header>
        <ToggleRow Label="Light Enabled" Checked={Values.Enabled !== false} OnChange={(V) => Change("Enabled", V)} />
        <SliderRow Label="Luminous Intensity" Value={Values.Intensity ?? (IsIes ? 16 : 5)} Min={0} Max={IsIes ? 45 : 16} Step={0.2} OnChange={(V) => Change("Intensity", V)} />
        <SliderRow Label="Color Temperature" Value={Values.Kelvin ?? 5600} Min={2400} Max={9500} Step={100} Unit=" K" OnChange={(V) => Change("Kelvin", V)} />
        {IsIes ? (
          <>
            <label className="studio-prop-select">
              <span>Photometric IES Profile</span>
              <select
                value={Values.IesProfile || "Barn-Door Fresnel"}
                onChange={(E) => Change("IesProfile", E.target.value)}
              >
                {IesProfiles.map((Prof) => (
                  <option key={Prof} value={Prof}>{Prof}</option>
                ))}
              </select>
            </label>
            <SliderRow Label="Beam Cone Angle" Value={Values.BeamDeg ?? 46} Min={15} Max={95} Step={1} Unit="°" OnChange={(V) => Change("BeamDeg", V)} />
            <SliderRow Label="Beam Edge Softness" Value={Values.Softness ?? 0.35} Min={0.05} Max={0.95} Step={0.02} OnChange={(V) => Change("Softness", V)} />
          </>
        ) : (
          <>
            <SliderRow Label="Emitter Width" Value={Values.Width ?? 2.0} Min={0.3} Max={9.0} Step={0.05} Unit=" m" OnChange={(V) => Change("Width", V)} />
            <SliderRow Label="Emitter Height" Value={Values.Height ?? 1.5} Min={0.3} Max={6.0} Step={0.05} Unit=" m" OnChange={(V) => Change("Height", V)} />
          </>
        )}
      </section>
    </div>
  );
}
