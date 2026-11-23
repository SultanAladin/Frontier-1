//============================================================================================================================================
//                                                         VEHICLEASSETINSPECTOR.JSX
//============================================================================================================================================
// 🚗 The level's vehicles (Outliner folder Vehicles): the Controlled Vehicle (the player's car, which holds the Vehicle Movement, Body,
// socket, collision and light components), an Off-road Vehicle and a Pickup Truck. Each is one of VehicleAssets.js's models, parked in
// the 3D viewport. Its inspector has three lettered pages behind the Orbit menu (InspectorTabs.jsx, the "garage" look):
//   A · Model   the vehicle rendered large in its paint, and the paint swatches (the viewport and this picture follow).
//   B · Size    length, then width, height, wheelbase, track, ground clearance and mass.
//   C · Drive   who drives it; the Controlled Vehicle lists its components (each opens its row).

import React, { useMemo } from "react";
import { InspectorTabs } from "./InspectorTabs.jsx";
import { ModelIcon } from "./EditorAssets.js";
import { VehicleAsset, VehicleModelOf, VehiclePaints, VehicleFacts } from "./VehicleAssets.js";
import { ComponentIcon } from "./VehicleComponents.jsx";
import "./VehicleAssetInspector.css";

export const VehicleAssetPanel = "vehicle-asset";
export const VehicleAssetSheet = [];
export const PaintOf = (Row, Values = {}) => (/^#[\da-f]{6}$/i.test(Values.Paint || "") ? Values.Paint : VehicleModelOf(Row.Model).Paint);
export function VehicleAssetSummary(Row, Values = {}) {
  const Model = VehicleModelOf(Row.Model);
  const Paint = VehiclePaints.find(([, Colour]) => Colour === PaintOf(Row, Values))?.[0] || "Custom paint";
  return (Model.Id === "controlled" ? "Player controlled" : "Parked") + " · " + Paint;
}

export function VehicleAssetInspector({ Subject, Values, Change, Kit, Scene = null }) {
  const { Card, Metric, Icon } = Kit;
  const Model = VehicleModelOf(Subject.Model);
  const Paint = PaintOf(Subject, Values);
  const Facts = VehicleFacts[Model.Id];
  // The picture is the viewport's own model, rendered in the studio the outliner icons come from.
  const Picture = useMemo(() => {
    try {
      return ModelIcon(VehicleAsset(Model.Id, Paint), 560, { Height: 300, Yaw: -128, Pitch: 13 });
    } catch {
      return null;
    }
  }, [Model.Id, Paint]);
  const Parts = (Scene?.Rows || []).filter((Row) => Row.Parent === Subject.Id);
  const Figure = (Label, Amount, Unit) => (
    <div className="va-fact" key={Label}>
      <small>{Label}</small>
      <b>
        {Amount}
        {Unit && <em> {Unit}</em>}
      </b>
    </div>
  );
  return (
    <InspectorTabs
      Owner={Subject.Id}
      Look="garage"
      Pages={[
        {
          Id: "model",
          Label: "Model",
          Content: (
            <Card Title="Vehicle model" Class="va-model-card">
              <div className="va-hero" data-vehicle-shown={Model.Id} data-vehicle-paint-shown={Paint} style={{ "--va-paint": Paint }}>
                {Picture ? <img src={Picture} alt={Model.Name} draggable={false} /> : <Icon Name={Model.Icon} Size={168} />}
                <div>
                  <b>{Model.Name}</b>
                  <small>{Model.Caption}</small>
                </div>
              </div>
              <div className="va-paints" role="radiogroup" aria-label="Paint">
                {VehiclePaints.map(([Name, Colour]) => (
                  <button
                    key={Colour}
                    type="button"
                    role="radio"
                    aria-checked={Colour === Paint}
                    aria-label={Name}
                    title={Name}
                    data-vehicle-paint={Colour}
                    className={Colour === Paint ? "on" : ""}
                    style={{ "--swatch": Colour }}
                    onClick={() => Change("Paint", Colour)}
                  />
                ))}
                <span>{VehiclePaints.find(([, Colour]) => Colour === Paint)?.[0] || "Custom"}</span>
              </div>
              {Parts.length > 0 && Scene?.OpenComponentViewer && (
                <button type="button" className="va-viewer" data-open-component-viewer onClick={() => Scene.OpenComponentViewer()}>
                  <span className="va-viewer-icons" aria-hidden="true">
                    {Parts.slice(0, 4).map((Row) => (
                      <Icon key={Row.Id} Name={ComponentIcon(Row, Scene.Values?.[Row.Id] || {})} Size={18} />
                    ))}
                  </span>
                  <span className="va-viewer-text">
                    <b>Open Component Viewer</b>
                    <small>{Parts.length} components · outliner, view and inspector</small>
                  </span>
                  <svg viewBox="0 0 16 16" width="14" height="14" aria-hidden="true"><path d="M6 3.5 10.5 8 6 12.5" fill="none" stroke="currentColor" strokeWidth="1.6" strokeLinecap="round" strokeLinejoin="round" /></svg>
                </button>
              )}
            </Card>
          ),
        },
        {
          Id: "size",
          Label: "Size",
          Content: (
            <Card Title="Dimensions" Class="va-size-card">
              <Metric Value={Facts.Length.toFixed(2)} Unit="m" Caption="Overall length" />
              <svg className="va-plan" viewBox="0 0 200 64" aria-hidden="true">
                <rect x="14" y="14" width="172" height="36" rx="9" />
                {[0.2, 0.8].map((At) => (
                  <g key={At}>
                    <rect className="va-plan-wheel" x={14 + 172 * At - 9} y="8" width="18" height="8" rx="2" />
                    <rect className="va-plan-wheel" x={14 + 172 * At - 9} y="48" width="18" height="8" rx="2" />
                  </g>
                ))}
                <line x1={14 + 172 * 0.2} y1="60" x2={14 + 172 * 0.8} y2="60" />
                <text x="100" y="35">
                  {Facts.Wheelbase.toFixed(2)} m wheelbase
                </text>
              </svg>
              <div className="va-facts">
                {Figure("Width", Facts.Width.toFixed(2), "m")}
                {Figure("Height", Facts.Height.toFixed(2), "m")}
                {Figure("Track", Facts.Track.toFixed(2), "m")}
                {Figure("Clearance", Facts.Clearance.toFixed(2), "m")}
                {Figure("Mass", Facts.Mass.toLocaleString("en-US"), "kg")}
                {Figure("Seats", Facts.Seats)}
              </div>
            </Card>
          ),
        },
        {
          Id: "drive",
          Label: "Drive",
          Content: (
            <Card Title="Drive" Class="va-drive-card">
              <div className={"va-driver" + (Model.Id === "controlled" ? " on" : "")}>
                <i />
                <span>
                  <b>{Model.Id === "controlled" ? "Player controlled" : "Parked in the level"}</b>
                  <small>{Facts.Drive}</small>
                </span>
              </div>
              {Parts.length > 0 && (
                <div className="va-parts" aria-label="Components">
                  {Parts.map((Row) => (
                    <button key={Row.Id} type="button" className="va-part" data-vehicle-part={Row.Id} onClick={() => (Scene?.OpenComponentViewer ? Scene.OpenComponentViewer(Row.Id) : Scene?.SelectRow?.(Row.Id))}>
                      <Icon Name={ComponentIcon(Row, Scene?.Values?.[Row.Id] || {})} Size={18} />
                      <span>
                        <b>{Row.Name}</b>
                        <small>{Row.Description}</small>
                      </span>
                    </button>
                  ))}
                </div>
              )}
            </Card>
          ),
        },
      ]}
    />
  );
}
