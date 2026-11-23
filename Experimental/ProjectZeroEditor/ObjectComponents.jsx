//============================================================================================================================================
//                                                          OBJECTCOMPONENTS.JSX
//============================================================================================================================================
// 🧩 Components. Showcase objects carry a component stack (Mass, Rigid Body, Collision) on the object's own Components page, the way
// a modifier stack sits on a mesh: add, remove, switch off and reorder, and each entry opens its own page with its cards. Cameras keep
// one Render Engine component as an outliner row (RenderComponents.jsx). In both cases the values live on the owner (Values[Owner]), so
// the owner's viewport, checks and quick controls keep reading the same keys. Browser authoring only.

import React, { useEffect, useState } from "react";
import "./ObjectComponents.css";
import { ShapeOf, Measure, MassOf, MassText, CollisionProxy, ObjectComponentCards, Switch } from "./PrimitiveInspector.jsx";
import { RenderComponentTypes, RetiredRenderComponents, RenderSummary } from "./RenderComponents.jsx";

export const ComponentPanel = "object-component";
export const ComponentSheet = [];

// Component, baked icon, description, per owner kind.
export const ComponentTypes = {
  object: [
    ["Mass", "editor-mass", "Mass from a material or set directly, in any unit"],
    ["Rigid Body", "editor-physics", "Body type, contact and damping"],
    ["Collision", "editor-box-collider", "Collision shape and filtering"],
  ],
  camera: RenderComponentTypes.map(([Name, Icon, Description]) => [Name, Icon, Description]),
};
// The revision each kind's components arrived in (saved scenes below it gain them once).
const Arrival = { object: 1, camera: 2 };
// Components retired at a revision (saved scenes below it lose them once): Follow-up 41 split camera render settings over five rows;
// Follow-up 42 folds them back into Render Engine.
const Retired = { 3: RetiredRenderComponents };
// Follow-up 46: object components leave the outliner for the object's component stack (Row.Components).
const StackArrival = 4;
export const ComponentRevision = 4;

const Slug = (Component) => Component.toLowerCase().replace(/\s+/g, "-");
export const OwnerKind = (Row) => (Row?.Panel === "geometry" && Row.Parent === "showcase" ? "object" : Row?.Panel === "camera" ? "camera" : null);
const TypesOf = (Row) => ComponentTypes[OwnerKind(Row)] || [];
export const TypeOf = (Component) => ComponentTypes.object.find(([Name]) => Name === Component) || ComponentTypes.object[0];

// The object's component stack, top to bottom. An object that never edited its stack has all three, switched on.
export const StackOf = (Owner) =>
  OwnerKind(Owner) !== "object"
    ? []
    : Array.isArray(Owner.Components)
      ? Owner.Components.filter((Entry) => ComponentTypes.object.some(([Name]) => Name === Entry?.Component))
      : ComponentTypes.object.map(([Component]) => ({ Component, Enabled: true }));
export const ComponentOn = (Owner, Component) => StackOf(Owner).some((Entry) => Entry.Component === Component && Entry.Enabled !== false);

// Older addresses (`cube-mass`, saved selections and ?inspect= links) open the owner's Components page at that entry.
export function ComponentAddress(Id, Rows = []) {
  if (typeof Id !== "string" || Rows.some((Row) => Row.Id === Id)) return null;
  for (const [Component] of ComponentTypes.object) {
    const Suffix = "-" + Slug(Component);
    if (!Id.endsWith(Suffix)) continue;
    const Owner = Rows.find((Row) => Row.Id === Id.slice(0, -Suffix.length));
    if (OwnerKind(Owner) === "object") return { Owner: Owner.Id, Component };
  }
  return null;
}

export function ComponentRow(Owner, Component) {
  const Types = TypesOf(Owner);
  const [, Icon, Description] = Types.find(([Name]) => Name === Component) || Types[0];
  return { Id: `${Owner.Id}-${Slug(Component)}`, Name: Component, Panel: ComponentPanel, Icon, Parent: Owner.Id, Description, Component };
}
// Outliner rows a new owner gets: only cameras; an object's stack lives on its row.
export const ComponentRowsFor = (Owner) => (OwnerKind(Owner) === "camera" ? TypesOf(Owner).map(([Component]) => ComponentRow(Owner, Component)) : []);

// Inserts each camera's components right after it (for scenes saved before cameras had them) and folds object component rows into
// their owner's stack (scenes saved before revision 4 keep the order and any removals they had).
export function WithObjectComponents(Rows, Revision = 0) {
  const Owners = new Map(Rows.map((Row) => [Row.Id, Row]));
  const Result = [];
  for (const Row of Rows) {
    if (Row.Panel === ComponentPanel && OwnerKind(Owners.get(Row.Parent)) === "object") continue;
    const Kind = OwnerKind(Row);
    if (Kind === "object") {
      const Stacked = Revision >= Arrival.object && Revision < StackArrival && !Array.isArray(Row.Components);
      const Attached = Rows.filter((Each) => Each.Parent === Row.Id && Each.Panel === ComponentPanel && ComponentTypes.object.some(([Name]) => Name === Each.Component));
      Result.push(Stacked ? { ...Row, Components: Attached.map((Each) => ({ Component: Each.Component, Enabled: true })) } : Row);
      continue;
    }
    Result.push(Row);
    if (Kind !== "camera" || Arrival.camera <= Revision) continue;
    const Present = new Set(Rows.filter((Each) => Each.Parent === Row.Id && Each.Panel === ComponentPanel).map((Each) => Each.Component));
    Result.push(...ComponentRowsFor(Row).filter((Each) => !Present.has(Each.Component)));
  }
  return Result;
}

// Saved scenes gain each kind's components once (ComponentRevision), so a component removed later stays removed.
export function EnsureObjectComponents(Rows = [], Revision) {
  const Seen = +Revision || 0;
  const Gone = new Set(Object.entries(Retired).filter(([At]) => +At > Seen).flatMap(([, Names]) => Names));
  const Owners = new Map(Rows.map((Row) => [Row.Id, Row]));
  const Kept = Rows.filter((Row) => !(Row.Panel === ComponentPanel && Gone.has(Row.Component) && OwnerKind(Owners.get(Row.Parent)) === "camera"));
  return WithObjectComponents(Kept, Seen);
}

const Read = (Values) => (Key, Initial) => (Values?.[Key] === undefined ? Initial : Values[Key]);
const SizeOf = (Owner, Values) => Measure(ShapeOf(Owner), Read(Values), Array.isArray(Values?.Scale) ? Values.Scale : [1, 1, 1]);
const Proxies = { box: "Box", sphere: "Sphere", capsule: "Capsule", convex: "Convex hull", sdf: "SDF", triangles: "Triangles", custom: "Custom" };

// Outliner value line, read from the owner's values.
export function ComponentSummary(Row, Owner, Values = {}) {
  if (!Owner) return "Owner removed";
  if (OwnerKind(Owner) === "camera") return RenderSummary(Row.Component, Values);
  return StackSummary(Row.Component, Owner, Values);
}

// Value line for one stack entry, read from the owner's values.
export function StackSummary(Component, Owner, Values = {}) {
  const R = Read(Values);
  if (Component === "Mass") {
    const { Mass, Manual, Density, Unit: Shown } = MassOf(R, SizeOf(Owner, Values));
    return `${MassText(Mass, Shown)} · ${Manual ? "set directly" : Density}`;
  }
  if (Component === "Rigid Body") {
    const Body = !Values.DYNAMIC ? "Static" : R("Physics Body", "dynamic").replace(/^./, (Letter) => Letter.toUpperCase());
    return `${Body} · ${Values.PHYSICS ? "simulated" : "not simulated"}`;
  }
  const Proxy = Proxies[CollisionProxy(ShapeOf(Owner), R)] || "Convex hull";
  return `${Proxy} · ${R("Physics Collides", true) ? "collides" : "ignored"}`;
}

const Chevron = (
  <svg viewBox="0 0 16 16" width="14" height="14" aria-hidden="true">
    <path d="M6 3.5L10.5 8 6 12.5" fill="none" stroke="currentColor" strokeWidth="1.6" strokeLinecap="round" strokeLinejoin="round" />
  </svg>
);

// Overview card listing the owner's components: each is a link that opens it (an object's stack entry, or a camera's outliner row);
// missing ones can be added back.
export function ComponentsCard({ Owner, Values, Scene, Kit, Title = "Components" }) {
  const { Icon } = Kit;
  const Rows = Scene?.Rows || [];
  const Stacked = OwnerKind(Owner) === "object";
  const Present = Stacked
    ? StackOf(Owner).map((Entry) => {
        const [Component, Glyph] = TypeOf(Entry.Component);
        return { Key: Component, Component, Icon: Glyph, Name: Component, Enabled: Entry.Enabled !== false, Open: () => Scene?.OpenComponent?.(Owner.Id, Component) };
      })
    : Rows.filter((Each) => Each.Parent === Owner.Id && Each.Panel === ComponentPanel).map((Row) => ({ Key: Row.Id, Component: Row.Component, Icon: Row.Icon, Name: Row.Name, Enabled: true, Open: () => Scene?.SelectRow?.(Row.Id) }));
  const Missing = TypesOf(Owner).filter(([Component]) => !Present.some((Each) => Each.Component === Component));
  const Add = (Component) =>
    Stacked
      ? Scene?.PatchRow?.(Owner.Id, { Components: [...StackOf(Owner), { Component, Enabled: true }] })
      : Scene?.AddRows?.(Owner.Id, [ComponentRow(Owner, Component)]);
  return (
    <section className="pi-card pi-components-card" data-card="Components">
      <header className="pi-card-head">
        <h3>{Title}</h3>
        <span className="pi-card-aside">{Present.length} on this {Stacked ? "object" : "camera"}</span>
      </header>
      <div className="oc-list">
        {Present.map((Each) => (
          <button key={Each.Key} type="button" className={"oc-link" + (Each.Enabled ? "" : " is-off")} data-component={Each.Component} onClick={Each.Open} title={`Open ${Each.Name}`}>
            {Icon && <Icon Name={Each.Icon} Size={22} />}
            <span>
              <b>{Each.Name}</b>
              <small>{Each.Enabled ? (Stacked ? StackSummary(Each.Component, Owner, Values) : ComponentSummary({ Component: Each.Component }, Owner, Values)) : "Switched off"}</small>
            </span>
            {Chevron}
          </button>
        ))}
      </div>
      {Missing.length > 0 && (Stacked ? Scene?.PatchRow : Scene?.AddRows) && (
        <div className="oc-add">
          {Missing.map(([Component]) => (
            <button key={Component} type="button" onClick={() => Add(Component)}>
              + {Component}
            </button>
          ))}
        </div>
      )}
    </section>
  );
}

const Grip = (
  <svg viewBox="0 0 12 16" width="10" height="14" aria-hidden="true">
    {[3, 8, 13].flatMap((Y) => [<circle key={"a" + Y} cx="3.5" cy={Y} r="1.3" />, <circle key={"b" + Y} cx="8.5" cy={Y} r="1.3" />])}
  </svg>
);
const Arrow = (Up) => (
  <svg viewBox="0 0 16 16" width="12" height="12" aria-hidden="true">
    <path d={Up ? "M4 10l4-4 4 4" : "M4 6l4 4 4-4"} fill="none" stroke="currentColor" strokeWidth="1.7" strokeLinecap="round" strokeLinejoin="round" />
  </svg>
);
const Cross = (
  <svg viewBox="0 0 16 16" width="12" height="12" aria-hidden="true">
    <path d="M4.5 4.5l7 7M11.5 4.5l-7 7" fill="none" stroke="currentColor" strokeWidth="1.7" strokeLinecap="round" />
  </svg>
);

// The object's Components page: the stack, top to bottom, like a modifier stack. Each entry can be switched off, moved, removed or
// opened; an open entry shows that component's cards (the same cards the component inspectors had) with a way back to the stack.
// Focus ({ Owner, Component, Stamp }) opens an entry from elsewhere (the Overview card, an older `cube-mass` link).
export function ComponentStack({ Owner, Values, Change, Scene, Kit, Focus = null, Notes = null }) {
  const { Icon } = Kit;
  const Stack = StackOf(Owner);
  const Focused = Focus?.Owner === Owner.Id ? Focus.Component : null;
  const [Opened, Open] = useState(Focused);
  const [Dragged, Drag] = useState(null);
  useEffect(() => {
    if (Focused) Open(Focused);
  }, [Focus?.Stamp]);
  const Store = (Next) => Scene?.PatchRow?.(Owner.Id, { Components: Next });
  const Move = (From, To) => {
    if (To < 0 || To >= Stack.length || From === To) return;
    const Next = [...Stack];
    const [Entry] = Next.splice(From, 1);
    Next.splice(To, 0, Entry);
    Store(Next);
  };
  const Toggle = (Component) => Store(Stack.map((Entry) => (Entry.Component === Component ? { ...Entry, Enabled: Entry.Enabled === false } : Entry)));
  const Remove = (Component) => {
    Store(Stack.filter((Entry) => Entry.Component !== Component));
    if (Opened === Component) Open(null);
  };
  const Missing = ComponentTypes.object.filter(([Component]) => !Stack.some((Entry) => Entry.Component === Component));
  const Current = Stack.find((Entry) => Entry.Component === Opened);

  if (Current) {
    const [Component, Glyph, Description] = TypeOf(Current.Component);
    const Enabled = Current.Enabled !== false;
    return (
      <div className="oc-entry-page" data-entry={Component}>
        <nav className="oc-crumbs" aria-label="Component path">
          <button type="button" className="oc-back" onClick={() => Open(null)} title="Back to the component stack">
            <span className="oc-back-arrow">{Chevron}</span>
            Components
          </button>
          <span aria-hidden="true">/</span>
          <b>{Component}</b>
        </nav>
        <section className={"pi-card oc-entry-head" + (Enabled ? "" : " is-off")}>
          <header className="pi-card-head">
            {Icon && <Icon Name={Glyph} Size={30} />}
            <h3>
              {Component}
              <small>{Description}</small>
            </h3>
            <span className="pi-card-aside">
              {Stack.indexOf(Current) + 1} of {Stack.length}
            </span>
          </header>
          <Switch Label="Enabled" Detail={Enabled ? StackSummary(Component, Owner, Values) : `Switched off · the object acts as if it had no ${Component.toLowerCase()}`} On={Enabled} Assign={() => Toggle(Component)} />
          {Notes && Notes(Component)}
        </section>
        <div className={Enabled ? "" : "oc-dimmed"}>
          <ObjectComponentCards Component={Component} Owner={Owner} Values={Values} Change={Change} Kit={Kit} />
        </div>
      </div>
    );
  }

  return (
    <div className="oc-stack-page">
      <section className="pi-card oc-stack-card" data-card="Component stack">
        <header className="pi-card-head">
          <h3>Component stack</h3>
          <span className="pi-card-aside">
            {Stack.filter((Entry) => Entry.Enabled !== false).length} of {Stack.length} on
          </span>
        </header>
        {Stack.length === 0 && <p className="oc-empty">No components. Add one below; the values the object had are kept.</p>}
        <ol className="oc-stack" aria-label="Component stack">
          {Stack.map((Entry, Index) => {
            const [Component, Glyph] = TypeOf(Entry.Component);
            const Enabled = Entry.Enabled !== false;
            return (
              <li
                key={Component}
                className={"oc-entry" + (Enabled ? "" : " is-off") + (Dragged === Index ? " is-dragged" : "")}
                data-component={Component}
                onDragOver={(Event) => {
                  if (Dragged === null) return;
                  Event.preventDefault();
                }}
                onDrop={(Event) => {
                  Event.preventDefault();
                  if (Dragged !== null) Move(Dragged, Index);
                  Drag(null);
                }}
              >
                <span
                  className="oc-grip"
                  draggable
                  title="Drag to reorder"
                  onDragStart={(Event) => {
                    Drag(Index);
                    Event.dataTransfer.effectAllowed = "move";
                    Event.dataTransfer.setData("text/plain", Component);
                  }}
                  onDragEnd={() => Drag(null)}
                >
                  {Grip}
                </span>
                <button type="button" className="oc-open" onClick={() => Open(Component)} title={`Open ${Component}`}>
                  {Icon && <Icon Name={Glyph} Size={24} />}
                  <span>
                    <b>{Component}</b>
                    <small>{Enabled ? StackSummary(Component, Owner, Values) : "Switched off"}</small>
                  </span>
                </button>
                <span className="oc-tools">
                  <button type="button" role="switch" aria-checked={Enabled} aria-label={`${Enabled ? "Switch off" : "Switch on"} ${Component}`} title={Enabled ? "Switch off" : "Switch on"} className={"oc-toggle" + (Enabled ? " on" : "")} onClick={() => Toggle(Component)}>
                    <span />
                  </button>
                  <button type="button" aria-label={`Move ${Component} up`} title="Move up" disabled={Index === 0} onClick={() => Move(Index, Index - 1)}>
                    {Arrow(true)}
                  </button>
                  <button type="button" aria-label={`Move ${Component} down`} title="Move down" disabled={Index === Stack.length - 1} onClick={() => Move(Index, Index + 1)}>
                    {Arrow(false)}
                  </button>
                  <button type="button" aria-label={`Remove ${Component}`} title="Remove (values stay on the object)" onClick={() => Remove(Component)}>
                    {Cross}
                  </button>
                </span>
              </li>
            );
          })}
        </ol>
        {Missing.length > 0 && (
          <div className="oc-add">
            {Missing.map(([Component]) => (
              <button key={Component} type="button" onClick={() => Store([...Stack, { Component, Enabled: true }])}>
                + {Component}
              </button>
            ))}
          </div>
        )}
      </section>
      <p className="pi-footnote oc-footnote">Values stay on the object, so a removed component comes back with its settings. Order is kept with the scene.</p>
    </div>
  );
}
