// Browser-only adapter around the pinned, unmodified reference inspector modules.
import {
  LightDefaults,
  LightPresetDefaults,
  LightNames,
} from "./LightSpecification.js";
import { CloudReference } from "./CloudSpecification.js";
import { HeightFogReference } from "./HeightFogSpecification.js";
import { LightPanel } from "./LightPanel.js";
import { CUSTOM_PANELS } from "./InspectorDepot/panels/index.js";

for (const Type of [
  "pointlight",
  "spotlight",
  "ieslight",
  "arealight",
  "tubelight",
  "ledlight",
  "ledstrip",
])
  CUSTOM_PANELS[Type] = { ...CUSTOM_PANELS[Type], build: LightPanel };

import { buildSheet } from "./InspectorDepot/inspector.js";
import { makeNode, flat, TYPES } from "./InspectorDepot/world.js";
import { bus } from "./InspectorDepot/bus.js";
import { repaintSliders } from "./InspectorDepot/kit.js";

for (const Type of ["ledlight", "ledstrip"])
  TYPES[Type] = {
    label: LightNames[Type],
    icon: "sun",
    color: "#cab88c",
    groups: [],
  };

const Mount = document.getElementById("ReferenceMount");
const Nodes = new Map();
const Baseline = new Map();
const Presets = new Map(
  flat.map((Node) => [Node.name, structuredClone(Node.props)]),
);
let Sheet,
  Active,
  Kind,
  Slice = "all",
  LastHeight = 0,
  Applying = false,
  StructureKey = "";
const Send = (Type, Data = {}) =>
  parent.postMessage({ ReferenceInspector: true, Type, ...Data }, "*");
const Escape = (Value) =>
  String(Value).replace(
    /[&<>"']/g,
    (Character) =>
      ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;", "'": "&#39;" })[
        Character
      ],
  );

function Measure() {
  if (!Sheet?.isConnected) return;
  const Height = Math.ceil(Mount.getBoundingClientRect().height);
  if (Height !== LastHeight) {
    LastHeight = Height;
    Send("height", { Height });
  }
}
function Trim() {
  const Custom = Sheet.querySelector(".mpanel");
  // Filter only the imported additions; the host's existing card list is independent.
  const LightCards =
    Slice === "summary"
      ? [".lp-summary-column", ".lp-status-column"]
      : Slice === "details"
        ? [".lp-source-column", ".lp-control-column"]
        : [
            ".lp-summary-column",
            ".lp-status-column",
            ".lp-source-column",
            ".lp-control-column",
            ".lp-preview",
            ".mp-rail",
            ".lp-output",
            ".lp-shape",
            ".lp-transform",
            ".lp-participation",
            ".lp-response",
          ];
  const Allowed = {
    ledlight: LightCards,
    ledstrip: LightCards,
    sun: [".mp-hero", ".mp-rail", ".mp-duo"],
    wind: [".wf-trace", ".mp-rail", ".mp-duo"],
    clouds:
      Slice === "coverage"
        ? [".cl-cover"]
        : [".cl-hero", ".mp-rail", ".mp-duo"],
    fog:
      Slice === "summary"
        ? [".fg-hero", ".mp-rail", ".mp-duo"]
        : Slice === "details"
          ? [".fg-vis"]
          : Slice === "beam"
            ? [".fg-scatter"]
            : [".fg-hero", ".mp-rail", ".mp-duo", ".fg-vis", ".fg-scatter"],
    pointlight: LightCards,
    spotlight: LightCards,
    ieslight: LightCards,
    arealight: LightCards,
    tubelight: LightCards,
  }[Kind];
  if (Allowed) {
    [...Custom.children].forEach((Child) => {
      if (!Child.matches(Allowed.join(","))) Child.remove();
    });
    [...Sheet.children].forEach((Child) => {
      if (Child !== Custom && !Child.classList.contains("ident"))
        Child.remove();
    });
  }
  if (Kind === "clouds" && Active.HostPanel === "local-cloud") {
    const Hero = Custom.querySelector(".cl-hero");
    if (Hero) {
      const Label = document.createElement("span");
      Label.textContent = "LOCAL VOLUME · SCHEMATIC";
      Label.className = "cl-local-domain";
      Hero.append(Label);
    }
  }
  Mount.dataset.referenceKind = Kind;
  if (
    [
      "wind",
      "fog",
      "sun",
      "clouds",
      "pointlight",
      "spotlight",
      "arealight",
      "ieslight",
      "tubelight",
      "ledlight",
      "ledstrip",
    ].includes(Kind)
  )
    Sheet.querySelector(".ident")?.remove();
  if (Kind === "wind") {
    Custom.prepend(Custom.querySelector(".wf-trace"));
    Custom.querySelector(".wf-specs")?.remove();
  }
  if (Kind === "fog") {
    const Transport = Custom.querySelector(".fg-scatter"),
      Chamber = Transport?.querySelector(".fg-chamber");
    if (Transport && Chamber) Transport.replaceChildren(Chamber);
  }
}
function Record(Node) {
  return {
    Id: Node.HostId,
    Name: Node.name,
    Visible: Node.vis,
    Open: Node.open,
    Locked: Node.locked,
    Dynamic: Node.dynamic,
    Notes: Node.notes,
    Properties: Node.props,
  };
}
function Publish() {
  if (Applying) return;
  const Records = [...Nodes.values()]
    .map(Record)
    .filter((Value) => JSON.stringify(Value) !== Baseline.get(Value.Id));
  if (Records.length) Send("state", { Records });
}
// Use text/value DOM properties for names while retaining the source identity styling.
function Identity() {
  const Name = Sheet.querySelector(".name-edit");
  if (!Name) return;
  Name.textContent = Active.name;
  const Caption = Sheet.querySelector(".fd-hero .mp-cap b");
  if (Caption) Caption.textContent = Active.name + " collection";
  const Notes = Sheet.querySelector("textarea");
  if (Notes) Notes.placeholder = "Notes for " + Active.name + "…";
  Name.ondblclick = () => {
    if (Name.querySelector("input")) return;
    const Node = Active,
      Before = Node.name,
      Input = document.createElement("input");
    Input.value = Before;
    Name.replaceChildren(Input);
    Input.focus();
    Input.select();
    let Done = false;
    const Finish = (Accept) => {
      if (Done) return;
      Done = true;
      Node.name = Accept && Input.value.trim() ? Input.value.trim() : Before;
      Name.textContent = Node.name;
      bus.emit("treechange");
    };
    Input.onkeydown = (Event) => {
      Event.stopPropagation();
      if (Event.key === "Enter") Finish(true);
      if (Event.key === "Escape") Finish(false);
    };
    Input.onblur = () => Finish(true);
    Input.onclick = Input.ondblclick = (Event) => Event.stopPropagation();
  };
}
bus.on("propchange", Publish);
bus.on("treechange", Publish);
bus.on("settod", (Hour) => {
  if (Active?.type === "sun") {
    Active.props.elevation = 62 * Math.sin(((Hour - 6) / 12) * Math.PI);
    Active.props.azimuth = (((90 + (Hour - 6) * 15) % 360) + 360) % 360;
    bus.emit("propchange", { node: Active, src: "reference-clock" });
  }
});
Mount.addEventListener("input", (Event) => {
  if (Event.target.matches("textarea")) Publish();
});

function Synchronize(Data) {
  Applying = true;
  Slice = Data.Slice || "all";
  for (const [Index, Row] of Data.Rows.entries()) {
    let Node = Nodes.get(Row.Id);
    if (!Node) {
      Node = makeNode(Escape(Row.Name), TYPES[Row.Type] ? Row.Type : "cube");
      Node.id = Index + 1;
      Node.HostId = Row.Id;
      if (LightDefaults[Row.Type])
        Object.assign(Node.props, structuredClone(LightDefaults[Row.Type]));
      if (LightPresetDefaults[Row.Preset])
        Object.assign(
          Node.props,
          structuredClone(LightPresetDefaults[Row.Preset]),
        );
      if (Presets.has(Row.Preset))
        Object.assign(Node.props, structuredClone(Presets.get(Row.Preset)));
      Nodes.set(Row.Id, Node);
    }
    Node.HostPanel = Row.Panel;
    Node.name = Row.Name;
    Node.vis = !Data.Hidden[Row.Id];
    Node.open = !Data.Collapsed[Row.Id];
    const Stored = Data.Values[Row.Id]?.ReferenceInspector;
    if (Stored) {
      Object.assign(Node.props, Stored.Properties || {});
      Node.locked = !!Stored.Locked;
      Node.dynamic = !!Stored.Dynamic;
      Node.notes = Stored.Notes || "";
    }
    if (Row.Type === "clouds")
      Object.assign(
        Node.props,
        CloudReference(Data.Values[Row.Id] || {}, Row.Panel),
      );
    if (Row.Panel === "height-fog")
      Object.assign(
        Node.props,
        HeightFogReference(Data.Values[Row.Id] || {}),
      );
    Node.kids = [];
  }
  const Present = new Set(Data.Rows.map((Row) => Row.Id));
  for (const Id of Nodes.keys()) if (!Present.has(Id)) Nodes.delete(Id);
  const Parents = new Map(Data.Rows.map((Row) => [Row.Id, Row.Parent]));
  for (const Row of Data.Rows) {
    const Seen = new Set([Row.Id]);
    let Ancestor = Row.Parent,
      Depth = 0,
      Cyclic = false;
    while (Nodes.has(Ancestor)) {
      if (Seen.has(Ancestor)) {
        Cyclic = true;
        break;
      }
      Seen.add(Ancestor);
      Depth++;
      Ancestor = Parents.get(Ancestor);
    }
    Nodes.get(Row.Id).depth = Cyclic ? 0 : Depth;
    const Parent = Nodes.get(Row.Parent);
    if (Parent && !Cyclic) Parent.kids.push(Nodes.get(Row.Id));
  }
  const Next = Nodes.get(Data.Selected);
  if (!Next) {
    Applying = false;
    return;
  }
  const NextKey = JSON.stringify(
    Data.Rows.map((Row) => [
      Row.Id,
      Row.Name,
      Row.Parent,
      !!Data.Hidden[Row.Id],
      !!Data.Collapsed[Row.Id],
      !!Data.Values[Row.Id]?.ReferenceInspector?.Locked,
    ]),
  );
  if (Active !== Next || StructureKey !== NextKey) {
    StructureKey = NextKey;
    Sheet?._dispose?.();
    Mount.replaceChildren();
    Active = Next;
    Kind = Next.type;
    const RawName = Active.name;
    Active.name = Escape(RawName);
    try {
      Sheet = buildSheet(Active, { onDirty: Publish });
    } finally {
      Active.name = RawName;
    }
    Identity();
    Trim();
    Mount.append(Sheet);
    bus.emit("propchange", { node: Active, src: "host" });
  } else bus.emit("propchange", { node: Active, src: "host" });
  Baseline.clear();
  for (const Node of Nodes.values())
    Baseline.set(Node.HostId, JSON.stringify(Record(Node)));
  Applying = false;
  // Appended frames can start offscreen, where browsers throttle rAF/observers.
  // Measure the populated sheet synchronously so the full card list is scrollable.
  repaintSliders(Mount);
  Measure();
  requestAnimationFrame(() => {
    repaintSliders(Mount);
    Measure();
  });
}
addEventListener("message", (Event) => {
  if (Event.source !== parent || !Event.data?.ReferenceHost) return;
  Synchronize(Event.data);
});
const Observer = new ResizeObserver(Measure);
Observer.observe(Mount);
addEventListener("pagehide", () => {
  Observer.disconnect();
  Sheet?._dispose?.();
});
Send("ready");
