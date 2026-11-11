import { LightNames, LightIcons } from "./LightSpecification.js";
import React, { useEffect, useRef, useState } from "react";

export const ReferenceLights = [
  ["reference-key-spot", "Key Spot", "spotlight"],
  ["reference-rim-point", "Rim Point", "pointlight"],
  ["reference-fill-point", "Fill Point", "pointlight"],
  ["reference-ece-low-beam", "ECE Low Beam", "ieslight"],
  ["reference-softbox", "Softbox", "arealight"],
  ["reference-studio-tube", "Studio Tube", "tubelight"],
  ["reference-led", "LED Emitter", "ledlight"],
  ["reference-led-strip", "LED Strip", "ledstrip"],
  ["reference-ies-downlight", "IES Downlight", "ieslight"],
].map(([Id, Name, ReferenceType]) => ({
  Id,
  Name,
  ReferenceType,
  ReferencePreset: Name,
  ReferenceOnly: true,
  Panel: "light",
  Icon: LightIcons[ReferenceType],
  Parent: "lighting",
  Description: "Light authoring · HTML preview",
}));

export function EnsureReferenceLights(Rows) {
  const Present = new Set(Rows.map((Row) => Row.Id));
  if (!Present.has("lighting"))
    Rows = [
      ...Rows,
      {
        Id: "lighting",
        Name: "Lighting",
        Panel: "group",
        Icon: "folder-generic",
        Parent: null,
        Description: "Light collection",
      },
    ];
  return [...Rows, ...ReferenceLights.filter((Row) => !Present.has(Row.Id))];
}

export function PrepareLightRows(Rows, Revision = 0) {
  let Result = Rows;
  if (Revision < 1) {
    const NewIds = new Set([
      "reference-led",
      "reference-led-strip",
      "reference-ies-downlight",
    ]);
    const Present = new Set(Rows.map((Row) => Row.Id));
    Result = EnsureReferenceLights(Rows).filter(
      (Row) =>
        Present.has(Row.Id) || NewIds.has(Row.Id) || Row.Id === "lighting",
    );
  }
  return Result.map((Row) =>
    Row.Panel === "light"
      ? {
          ...Row,
          Icon:
            LightIcons[Row.ReferenceType || "arealight"] || "editor-area-light",
          Description: Row.ReferenceOnly
            ? (LightNames[Row.ReferenceType] || "Light") + " · HTML authoring"
            : Row.Description,
        }
      : Row,
  );
}

function TypeOf(Row) {
  if (Row.Panel === "geometry") {
    const Primitive = Row.Icon?.replace("editor-", "");
    return ["sphere", "torus", "cylinder", "plane"].includes(Primitive)
      ? Primitive
      : "cube";
  }
  return (
    Row.ReferenceType ||
    {
      group: "folder",
      wind: "wind",
      moon: "moon",
      clouds: "clouds",
      "height-fog": "fog",
      sun: "sun",
      light: "arealight",
      geometry: "cube",
      atmosphere: "sky",
      sky: "sky",
      stars: "stars",
      camera: "camera",
      "aerial-fog": "fog",
      "local-fog": "fog",
      "local-cloud": "clouds",
      post: "post",
      flare: "sun",
      precipitation: "clouds",
      rainbow: "sky",
    }[Row.Panel] ||
    "cube"
  );
}
export function HasReferencePanel(Row) {
  if (Row.Panel === "group" || Row.Panel === "moon") return false;
  return (
    !!Row.ReferenceType ||
    ["wind", "clouds", "local-cloud", "height-fog", "sun", "light"].includes(
      Row.Panel,
    )
  );
}

export default function ReferencePanel({
  Subject,
  Rows,
  Values,
  Hidden,
  Collapsed,
  Apply,
  Slice = "all",
}) {
  const Frame = useRef(null),
    Current = useRef(null),
    Ready = useRef(false);
  const [Height, Size] = useState(400);
  Current.current = { Subject, Rows, Values, Hidden, Collapsed, Apply, Slice };
  const Transmit = () => {
    const State = Current.current;
    Frame.current?.contentWindow?.postMessage(
      {
        ReferenceHost: true,
        Slice: State.Slice,
        Selected: State.Subject.Id,
        Rows: State.Rows.map((Row) => ({
          Id: Row.Id,
          Name: Row.Name,
          Parent: Row.Parent,
          Type: TypeOf(Row),
          Panel: Row.Panel,
          Preset: Row.ReferencePreset,
        })),
        Values: State.Values,
        Hidden: State.Hidden,
        Collapsed: State.Collapsed,
      },
      "*",
    );
  };
  useEffect(() => {
    const Receive = (Event) => {
      if (
        Event.source !== Frame.current?.contentWindow ||
        !Event.data?.ReferenceInspector
      )
        return;
      if (Event.data.Type === "ready") {
        Ready.current = true;
        Transmit();
      }
      if (Event.data.Type === "height" && Number.isFinite(Event.data.Height))
        Size(Math.max(1, Math.min(500000, Event.data.Height)));
      if (Event.data.Type === "state")
        Current.current.Apply(Event.data.Records);
    };
    addEventListener("message", Receive);
    return () => removeEventListener("message", Receive);
  }, []);
  useEffect(() => {
    if (Ready.current) Transmit();
  }, [Rows, Values, Hidden, Collapsed, Slice]);
  if (!HasReferencePanel(Subject)) return null;
  return (
    <section
      className="reference-inspector-copy"
      data-reference-kind={TypeOf(Subject)}
      data-reference-slice={Slice}
    >
      <iframe
        ref={Frame}
        title={
          "Reference inspector · " +
          Subject.Name +
          (Slice === "all" ? "" : " · " + Slice)
        }
        sandbox="allow-scripts"
        srcDoc={window.NativeAssets.ReferenceInspector}
        onLoad={() => {
          Ready.current = true;
          Transmit();
        }}
        style={{
          display: "block",
          width: "100%",
          height: Height,
          border: 0,
          background: "#121212",
        }}
      />
    </section>
  );
}
