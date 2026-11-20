import React, { useEffect, useRef, useState } from "react";
import ReferencePanel from "./ReferencePanel.jsx";
import { Glyph, Icon, Inspector } from "./Inspectors.jsx";
import {
  BeginWorldActorDrag,
  BuildActorPayloadFromConstructItem,
  EndWorldActorDrag,
} from "./WorldDragDrop.js";
import { AnalyticalShapeThumbnail } from "./AnalyticalShapeThumbnail.jsx";

export const ConstructGroups = [
  "All",
  "Forces",
  "Environment",
  "Weather",
  "Cameras",
  "Fans",
  "Geometry",
  "Lighting",
];
export function ConstructGroup(Subject) {
  if (Subject.Panel === "force") return "Forces";
  if (
    [
      "wind",
      "precipitation",
      "clouds",
      "local-cloud",
      "height-fog",
      "aerial-fog",
      "local-fog",
    ].includes(Subject.Panel)
  )
    return "Weather";
  if (Subject.Panel === "camera") return "Cameras";
  if (Subject.Category === "Fans & Wind" || String(Subject.PropId || "").includes("_fan"))
    return "Fans";
  if (Subject.Panel === "geometry") return "Geometry";
  if (Subject.Panel === "light") return "Lighting";
  return "Environment";
}

// Same dimensions, category rail and entity matrix as NativeConstructPanel.
// Placement is intentionally browser-local; native construction is deferred.
export default function ConstructPanel({
  Catalogue,
  Add,
  Close,
  ResolveSpawnPosition,
}) {
  const [Query, Search] = useState("");
  const [Group, ChooseGroup] = useState("All");
  const [Picked, Choose] = useState(null);
  const [Name, Rename] = useState("");
  const [Properties, AssignProperties] = useState({});
  const [Visible, Show] = useState(true);
  const Dialog = useRef(null),
    SearchField = useRef(null),
    BackButton = useRef(null);
  const Opener = useRef(document.activeElement);
  const LastChoice = useRef(null);
  const Adding = useRef(false);
  const Candidates = Catalogue.filter(
    (Subject) =>
      (Group === "All" || ConstructGroup(Subject) === Group) &&
      Subject.Name.toLowerCase().includes(Query.trim().toLowerCase()),
  );

  useEffect(() => {
    SearchField.current?.focus();
    return () => {
      if (Opener.current?.isConnected) Opener.current.focus();
    };
  }, []);
  useEffect(() => {
    if (Picked) BackButton.current?.focus();
    else if (LastChoice.current)
      Dialog.current
        ?.querySelector(`[data-construct-id="${LastChoice.current}"]`)
        ?.focus();
  }, [Picked]);

  const BuildPickedState = (Subject) => {
    const PresetPayload = BuildActorPayloadFromConstructItem(Subject);
    const HalfHeightZ = PresetPayload?.HalfHeightZ ?? 0.39;
    const SpawnPos = ResolveSpawnPosition
      ? ResolveSpawnPosition(HalfHeightZ)
      : [0, 0, +(-0.64 + HalfHeightZ).toFixed(3)];
    const MergedSubject = PresetPayload?.Template
      ? { ...Subject, ...PresetPayload.Template, Id: Subject.Id, Name: Subject.Name }
      : Subject;
    const MergedProps = PresetPayload?.Properties
      ? {
          ...PresetPayload.Properties,
          Position: SpawnPos,
          ...(PresetPayload.Properties.ReferenceInspector
            ? {
                ReferenceInspector: {
                  ...PresetPayload.Properties.ReferenceInspector,
                  Properties: {
                    ...(PresetPayload.Properties.ReferenceInspector.Properties || {}),
                    pos: SpawnPos,
                  },
                },
              }
            : {}),
        }
      : {};
    return { MergedSubject, MergedProps };
  };

  const Pick = (Subject) => {
    LastChoice.current = Subject.Id;
    const { MergedSubject, MergedProps } = BuildPickedState(Subject);
    Choose(MergedSubject);
    Rename(Subject.Name);
    AssignProperties(MergedProps);
    Show(true);
  };
  const Keyboard = (Event) => {
    // The modal owns Escape, even when its search or a property has text focus.
    Event.stopPropagation();
    if (Event.key === "Escape") {
      Event.preventDefault();
      if (Picked) Choose(null);
      else Close();
    }
    if (Event.key === "Tab") {
      const Focusable = [
        ...Dialog.current.querySelectorAll(
          'button:not([disabled]), input:not([disabled]), select:not([disabled]), textarea:not([disabled]), [tabindex="0"]',
        ),
      ].filter((Control) => Control.getClientRects().length);
      const First = Focusable[0],
        Last = Focusable.at(-1);
      if (Event.shiftKey && document.activeElement === First) {
        Event.preventDefault();
        Last?.focus();
      } else if (!Event.shiftKey && document.activeElement === Last) {
        Event.preventDefault();
        First?.focus();
      }
    }
  };

  return (
    <div
      className="modal-scrim construct-scrim"
      onClick={(Event) => {
        if (Event.target === Event.currentTarget) Close();
      }}
    >
      <section
        className="construct-dialog"
        role="dialog"
        aria-modal="true"
        aria-label="Construct"
        ref={Dialog}
        onKeyDown={Keyboard}
      >
        <header className="construct-titlebar">
          <h2>Construct</h2>
          <button aria-label="Close construct" onClick={Close}>
            <Glyph Name="close" Size={14} />
          </button>
        </header>
        <div className="construct-path">
          <strong>CONSTRUCT / FRONTIER</strong>
          <span className={!Picked ? "active" : ""}>01 Entities</span>
          <Glyph Name="arrow" Size={12} />
          <span className={Picked ? "active" : ""}>02 Properties</span>
          <kbd>Ctrl+A</kbd>
        </div>
        {!Picked ? (
          <>
            <label className="construct-search">
              <Glyph Name="search" Size={16} />
              <input
                ref={SearchField}
                placeholder="Search existing engine entities…"
                aria-label="Search construct"
                value={Query}
                onChange={(Event) => Search(Event.target.value)}
              />
              {Query && (
                <button
                  aria-label="Clear construct search"
                  onClick={() => {
                    Search("");
                    SearchField.current?.focus();
                  }}
                >
                  <Glyph Name="close" Size={13} />
                </button>
              )}
            </label>
            <div className="construct-catalogue">
              <nav
                className="construct-categories"
                aria-label="Construct categories"
              >
                {ConstructGroups.map((Label) => (
                  <button
                    key={Label}
                    className={Group === Label ? "active" : ""}
                    aria-pressed={Group === Label}
                    onClick={() => ChooseGroup(Label)}
                  >
                    {Label}
                  </button>
                ))}
              </nav>
              <div className="construct-results">
                <div
                  className="construct-matrix"
                  aria-label="Construct entities"
                >
                  {Candidates.map((Subject) => {
                    const DragPayload = BuildActorPayloadFromConstructItem(Subject);
                    return (
                      <button
                        key={Subject.Id}
                        data-construct-id={Subject.Id}
                        className="construct-entity"
                        aria-label={Subject.Name}
                        title={DragPayload ? `${Subject.Name} · Click to configure or drag directly into the 3D scene` : Subject.Name}
                        draggable={Boolean(DragPayload)}
                        onDragStart={
                          DragPayload
                            ? (Event) => BeginWorldActorDrag(Event, DragPayload)
                            : undefined
                        }
                        onDragEnd={DragPayload ? EndWorldActorDrag : undefined}
                        onClick={() => Pick(Subject)}
                        onDoubleClick={() => {
                          if (Adding.current) return;
                          Adding.current = true;
                          const { MergedSubject, MergedProps } = BuildPickedState(Subject);
                          Add(MergedSubject, MergedProps, true);
                        }}
                      >
                        {DragPayload?.Template?.PropId ||
                        (Subject.Panel === "geometry" && DragPayload?.Template?.PrimitiveShape) ? (
                          <AnalyticalShapeThumbnail
                            PropId={DragPayload?.Template?.PropId || null}
                            Shape={DragPayload?.Template?.PrimitiveShape || "box"}
                            Material={DragPayload?.Properties?.Material}
                            Size={56}
                          />
                        ) : (
                          <Icon Name={Subject.Icon} Size={52} />
                        )}
                        <span>{Subject.Name}</span>
                      </button>
                    );
                  })}
                </div>
                {!Candidates.length && (
                  <p className="construct-empty" role="status">
                    No matching engine entities.
                  </p>
                )}
                <p className="construct-caption">
                  Select an entity to edit its properties, then add it to the
                  HTML preview.
                </p>
              </div>
            </div>
          </>
        ) : (
          <>
            <div className="construct-selection">
              <button ref={BackButton} onClick={() => Choose(null)}>
                <Glyph Name="back" Size={14} />
                Entities
              </button>
              <Icon Name={Picked.Icon} Size={24} />
              <input
                aria-label="Construct entity name"
                maxLength={64}
                value={Name}
                onChange={(Event) => Rename(Event.target.value)}
              />
            </div>
            <div className="construct-properties">
              <Inspector
                Subject={{ ...Picked, Name: Name.trim() || Picked.Name }}
                ReferenceCards={
                  Picked.ReferenceOnly && Picked.Panel === "light" ? (
                    <ReferencePanel
                      Subject={{ ...Picked, Name: Name.trim() || Picked.Name }}
                      Rows={[{ ...Picked, Name: Name.trim() || Picked.Name }]}
                      Values={{ [Picked.Id]: Properties }}
                      Hidden={{ [Picked.Id]: !Visible }}
                      Collapsed={{}}
                      Apply={(Records) => {
                        const Authored = Records.find(
                          (Record) => Record.Id === Picked.Id,
                        );
                        if (Authored)
                          AssignProperties((Previous) => ({
                            ...Previous,
                            ReferenceInspector: {
                              Properties: Authored.Properties,
                              Locked: Authored.Locked,
                              Dynamic: Authored.Dynamic,
                              Notes: Authored.Notes,
                            },
                          }));
                      }}
                    />
                  ) : null
                }
                Values={Properties}
                Change={(Key, Value) =>
                  AssignProperties((Previous) => ({
                    ...Previous,
                    [Key]: Value,
                  }))
                }
                Hidden={!Visible}
                ToggleHidden={() => Show((Previous) => !Previous)}
              />
            </div>
          </>
        )}
        <footer>
          <small>
            {Picked
              ? "Configure properties or add directly to the active level"
              : `${Candidates.length} entities · Click to inspect or drag into 3D viewport`}
          </small>
          <button onClick={Close}>Cancel</button>
          {Picked && (
            <button
              className="construct-add"
              onClick={() => {
                if (Adding.current) return;
                Adding.current = true;
                Add(
                  { ...Picked, Name: Name.trim() || Picked.Name },
                  Properties,
                  Visible,
                );
              }}
            >
              Add to scene
              <Glyph Name="plus" Size={14} />
            </button>
          )}
        </footer>
      </section>
    </div>
  );
}
