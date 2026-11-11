import React, {
  useEffect,
  useRef,
  useState,
  useMemo,
  useLayoutEffect,
} from "react";
import ShaderballPanel from "./ShaderballPanel.jsx";
import {
  MaterialChannels,
  MaterialTypes,
  SourceNames,
  ResolveMaterial,
  ChangeChannel,
  ChooseMaterialType,
  Clamp,
  GradientCss,
  GradientValue,
} from "./MaterialSpecification.js";
import "./MaterialPanel.css";

function NumericInput(Props) {
  const [Draft, EditDraft] = useState(String(Props.value));
  useLayoutEffect(() => EditDraft(String(Props.value)), [Props.value]);
  return (
    <input
      {...Props}
      value={Draft}
      onChange={(Event) => {
        EditDraft(Event.target.value);
        if (Event.target.value !== "") Props.onChange(Event);
      }}
      onBlur={() => EditDraft(String(Props.value))}
    />
  );
}

function ValueInput({ Field, Value, Change, Label }) {
  const [Draft, EditDraft] = useState(String(Value));
  useEffect(() => EditDraft(String(Value)), [Value]);
  if (Field.Kind === "Colour")
    return (
      <div className="colour-input">
        <input
          aria-label={Label}
          type="color"
          value={Value}
          onChange={(Event) => Change(Event.target.value)}
        />
        <input
          aria-label={Label + " hex"}
          value={Draft}
          maxLength={7}
          onChange={(Event) => EditDraft(Event.target.value)}
          onBlur={() => {
            if (/^#[0-9a-f]{6}$/i.test(Draft)) Change(Draft);
            else EditDraft(String(Value));
          }}
          onKeyDown={(Event) => {
            if (Event.key === "Enter") Event.currentTarget.blur();
          }}
          onFocus={(Event) => Event.target.select()}
        />
        <span>sRGB</span>
      </div>
    );
  return (
    <div className="material-number">
      <NumericInput
        type="number"
        aria-label={Label}
        min={Field.Minimum}
        max={Field.Maximum}
        step={Field.Id === "displacement" ? 0.001 : 0.01}
        value={Value}
        onChange={(Event) =>
          Change(Clamp(Event.target.value, Field.Minimum, Field.Maximum))
        }
      />
      <span>{Field.Unit || "value"}</span>
      <input
        aria-label={Label + " slider"}
        type="range"
        min={Field.Minimum}
        max={Field.Maximum}
        step={Field.Id === "displacement" ? 0.001 : 0.01}
        value={Value}
        onChange={(Event) => Change(+Event.target.value)}
      />
    </div>
  );
}
function GradientEditor({ Field, Channel, Change }) {
  const [Selected, Select] = useState(Channel.Stops[0].Id),
    Track = useRef(null);
  const Current =
    Channel.Stops.find((Stop) => Stop.Id === Selected) || Channel.Stops[0];
  const Assign = (Id, Patch) =>
    Change({
      Stops: Channel.Stops.map((Stop) =>
        Stop.Id === Id ? { ...Stop, ...Patch } : Stop,
      ),
    });
  const Add = () => {
    const Sorted = [...Channel.Stops].sort((A, B) => A.Position - B.Position);
    let Left = Sorted[0],
      Right = Sorted.at(-1);
    for (let Index = 1; Index < Sorted.length; Index++)
      if (
        Sorted[Index].Position - Sorted[Index - 1].Position >=
          Right.Position - Left.Position ||
        (Sorted.length > 2 && Index === 1)
      ) {
        Left = Sorted[Index - 1];
        Right = Sorted[Index];
      }
    const Position = (Left.Position + Right.Position) / 2,
      Id = crypto.randomUUID();
    Change({
      Stops: [
        ...Channel.Stops,
        {
          Id,
          Position,
          Value: GradientValue(Channel, Position, Field.Kind === "Colour"),
        },
      ],
    });
    Select(Id);
  };
  return (
    <div className="gradient-editor">
      <div className="material-section-title">
        <h3>Gradient stops</h3>
        <select
          aria-label="Gradient axis"
          value={Channel.Axis}
          onChange={(Event) => Change({ Axis: Event.target.value })}
        >
          <option value="U">U · horizontal</option>
          <option value="V">V · vertical</option>
        </select>
      </div>
      <div
        className="gradient-track"
        ref={Track}
        style={{ background: GradientCss(Channel, Field) }}
      >
        {Channel.Stops.map((Stop, Index) => (
          <button
            key={Stop.Id}
            className={Current.Id === Stop.Id ? "selected" : ""}
            style={{ left: Stop.Position * 100 + "%" }}
            aria-label={"Gradient stop " + (Index + 1)}
            aria-pressed={Current.Id === Stop.Id}
            onClick={() => Select(Stop.Id)}
            onPointerDown={(Event) => {
              Select(Stop.Id);
              Event.currentTarget.setPointerCapture(Event.pointerId);
            }}
            onPointerMove={(Event) => {
              if (!Event.currentTarget.hasPointerCapture(Event.pointerId))
                return;
              const Rect = Track.current.getBoundingClientRect();
              Assign(Stop.Id, {
                Position: +Clamp(
                  (Event.clientX - Rect.left) / Rect.width,
                ).toFixed(3),
              });
            }}
            onPointerUp={(Event) =>
              Event.currentTarget.releasePointerCapture(Event.pointerId)
            }
            onKeyDown={(Event) => {
              if (["ArrowLeft", "ArrowRight"].includes(Event.key)) {
                Event.preventDefault();
                Assign(Stop.Id, {
                  Position: Clamp(
                    Stop.Position + (Event.key === "ArrowRight" ? 0.01 : -0.01),
                  ),
                });
              }
            }}
          >
            ◇
          </button>
        ))}
      </div>
      <div className="gradient-scale">
        <span>0%</span>
        <span>50%</span>
        <span>100%</span>
      </div>
      <div className="stop-editor">
        <label>
          Stop position{" "}
          <NumericInput
            type="number"
            aria-label="Stop position"
            min="0"
            max="100"
            step="1"
            value={+(Current.Position * 100).toFixed(1)}
            onChange={(Event) =>
              Assign(Current.Id, { Position: Clamp(Event.target.value / 100) })
            }
          />
          <span>%</span>
        </label>
        <ValueInput
          Field={Field}
          Value={Current.Value}
          Label="Stop value"
          Change={(Value) => Assign(Current.Id, { Value })}
        />
      </div>
      <div className="gradient-actions">
        <button disabled={Channel.Stops.length >= 8} onClick={Add}>
          + Add stop
        </button>
        <button
          disabled={Channel.Stops.length <= 2}
          onClick={() => {
            Change({
              Stops: Channel.Stops.filter((Stop) => Stop.Id !== Current.Id),
            });
            Select(Channel.Stops.find((Stop) => Stop.Id !== Current.Id).Id);
          }}
        >
          Remove stop
        </button>
        <span>{Channel.Stops.length} / 8 stops</span>
      </div>
      <p>
        Linear interpolation in UV space. Drag a stop or use arrow keys. Scalar
        channels interpolate values, not colour.
      </p>
    </div>
  );
}
function SourceEditor({ Field, Channel, Change }) {
  const [Message, Report] = useState(""),
    Request = useRef(0),
    Mounted = useRef(true);
  useEffect(
    () => () => {
      Mounted.current = false;
    },
    [],
  );
  const Import = async (Event) => {
    const File = Event.target.files?.[0];
    Event.target.value = "";
    if (!File) return;
    const Ticket = ++Request.current;
    if (
      !["image/png", "image/jpeg", "image/webp"].includes(File.type) ||
      File.size > 8 * 1024 * 1024
    ) {
      Report("Choose a PNG, JPEG or WebP under 8 MB.");
      return;
    }
    const Url = URL.createObjectURL(File);
    try {
      const Image = new window.Image();
      Image.src = Url;
      await Image.decode();
      const Canvas = document.createElement("canvas"),
        Ratio = Math.min(1, 160 / Math.max(Image.width, Image.height));
      Canvas.width = Math.max(1, Math.round(Image.width * Ratio));
      Canvas.height = Math.max(1, Math.round(Image.height * Ratio));
      Canvas.getContext("2d").drawImage(
        Image,
        0,
        0,
        Canvas.width,
        Canvas.height,
      );
      if (Mounted.current && Ticket === Request.current) {
        Change({
          Texture: {
            Name: File.name,
            Data: Canvas.toDataURL("image/png"),
            Width: Image.width,
            Height: Image.height,
          },
        });
        Report("Embedded preview saved. Original texture stays on disk.");
      }
    } catch {
      if (Mounted.current)
        Report(
          "Image could not be decoded. The previous texture is unchanged.",
        );
    } finally {
      URL.revokeObjectURL(Url);
    }
  };
  return (
    <section className="channel-editor">
      <div className="material-section-title">
        <div>
          <span className="material-eyebrow">
            {Field.Group} / CHANNEL{" "}
            {String(MaterialChannels.indexOf(Field) + 1).padStart(2, "0")}
          </span>
          <h2>{Field.Label}</h2>
        </div>
        <span className={"source-indicator " + Channel.Source.toLowerCase()}>
          {Channel.Source === "Code"
            ? "Unresolved"
            : Channel.Source === "Texture" && !Channel.Texture
              ? "No image"
              : "Authored"}
        </span>
      </div>
      <div
        className="source-tabs"
        role="group"
        aria-label={Field.Label + " source"}
      >
        {Object.entries(SourceNames).map(([Key, Name]) => (
          <button
            key={Key}
            aria-pressed={Channel.Source === Key}
            onClick={() => Change({ Source: Key })}
          >
            {Name}
          </button>
        ))}
      </div>
      <p className="source-explanation">
        One active source per channel. Other source drafts are kept when
        switching.
      </p>
      {Channel.Source === "Fill" && (
        <div className="fill-editor">
          <h3>{Field.Kind === "Colour" ? "Fill colour" : "Constant value"}</h3>
          {Field.Kind === "Colour" && (
            <div
              className="large-colour"
              style={{ background: Channel.Fill }}
            />
          )}
          <ValueInput
            Field={Field}
            Value={Channel.Fill}
            Label={Field.Label + " fill"}
            Change={(Fill) => Change({ Fill })}
          />
        </div>
      )}
      {Channel.Source === "Gradient" && (
        <GradientEditor Field={Field} Channel={Channel} Change={Change} />
      )}
      {Channel.Source === "Texture" && (
        <div className="texture-editor">
          <div className="texture-drop">
            {Channel.Texture ? (
              <img src={Channel.Texture.Data} alt={Channel.Texture.Name} />
            ) : (
              <span className="texture-empty">
                ▧<small>No texture assigned</small>
              </span>
            )}
            <label className="texture-upload">
              {Channel.Texture ? "Replace texture" : "+ Choose texture"}
              <input
                aria-label="Upload channel texture"
                type="file"
                accept="image/png,image/jpeg,image/webp"
                onChange={Import}
              />
            </label>
          </div>
          {Channel.Texture && (
            <div className="texture-file">
              <span>
                {Channel.Texture.Name}
                <small>
                  {Channel.Texture.Width} × {Channel.Texture.Height} · embedded
                  preview ≤160 px
                </small>
              </span>
              <button
                aria-label="Remove texture"
                onClick={() => {
                  Request.current++;
                  Change({ Texture: null });
                }}
              >
                ×
              </button>
            </div>
          )}
          <div className="texture-options">
            <label>
              Sample
              <select
                aria-label="Texture component"
                value={Channel.Component}
                onChange={(Event) => Change({ Component: Event.target.value })}
              >
                {(Field.Kind === "Colour"
                  ? ["RGB", "R", "G", "B", "A"]
                  : ["R", "G", "B", "A"]
                ).map((Name) => (
                  <option key={Name}>{Name}</option>
                ))}
              </select>
            </label>
            <label>
              UV repeat
              <NumericInput
                type="number"
                aria-label="Texture repeat"
                min=".1"
                max="16"
                step=".1"
                value={Channel.Repeat}
                onChange={(Event) =>
                  Change({ Repeat: Clamp(Event.target.value, 0.1, 16) })
                }
              />
            </label>
          </div>
          <p>
            PNG / JPEG / WebP · up to 8 MB. Local preview only; no upload or
            native texture descriptor.
          </p>
        </div>
      )}
      {Channel.Source === "Code" && (
        <div className="code-editor">
          <div className="code-warning">
            <b>Native link pending</b>
            <p>
              This channel is driven by a code descriptor. Nothing is compiled
              or executed in this HTML editor.
            </p>
          </div>
          <label>
            Provider
            <select
              aria-label="Code provider"
              value={Channel.Code.Language}
              onChange={(Event) =>
                Change({
                  Code: { ...Channel.Code, Language: Event.target.value },
                })
              }
            >
              <option>C++</option>
              <option>Slang / HLSL</option>
              <option>GLSL</option>
            </select>
          </label>
          <label>
            Symbol / entry point
            <input
              aria-label="Code symbol"
              placeholder="Project.Materials.UpdateColour"
              value={Channel.Code.Symbol}
              onChange={(Event) =>
                Change({
                  Code: {
                    ...Channel.Code,
                    Symbol: Event.target.value.slice(0, 256),
                  },
                })
              }
            />
          </label>
          <label>
            Source / integration notes
            <textarea
              aria-label="Channel code"
              spellCheck="false"
              placeholder="Describe the native producer or paste shader source for the later C++ integration."
              maxLength={12000}
              value={Channel.Code.Text}
              onChange={(Event) =>
                Change({ Code: { ...Channel.Code, Text: Event.target.value } })
              }
            />
          </label>
          <h3>Preview fallback</h3>
          <ValueInput
            Field={Field}
            Value={Channel.Fill}
            Label="Code fallback"
            Change={(Fill) => Change({ Fill })}
          />
        </div>
      )}
      {Message && (
        <p role="status" className="material-message">
          {Message}
        </p>
      )}
      {["ior", "reflectance"].includes(Field.Id) && (
        <p className="channel-note">
          Reflectance and refraction share one IOR carrier in the native
          material. Both rows stay synchronized.
        </p>
      )}
      {[
        "normal",
        "coatNormal",
        "anisotropy",
        "direction",
        "subsurface",
        "thickness",
        "displacement",
      ].includes(Field.Id) && (
        <p className="channel-note">
          Authoring supported. This channel is not evaluated by the browser
          lighting preview.
        </p>
      )}
    </section>
  );
}
export default function MaterialPanel({
  Subject,
  Values,
  Change,
  Expand,
  Compact = false,
}) {
  const Material = useMemo(() => ResolveMaterial(Values), [Values]),
    Latest = useRef(Material);
  Latest.current = Material;
  const [Selected, Select] = useState("colour"),
    [Query, Search] = useState("");
  const Field = MaterialChannels.find((Item) => Item.Id === Selected),
    Channel = Material.Channels[Selected];
  const Apply = (Next) => Change("Material", Next);
  const Assign = (Id, Patch) => Apply(ChangeChannel(Latest.current, Id, Patch));
  const Authored = Object.values(Material.Channels).filter(
    (Item) => Item.Source !== "Fill",
  ).length;
  return (
    <div
      className={
        "material-panel " + (Compact ? "material-compact" : "material-expanded")
      }
      data-material-owner={Subject.Id}
    >
      <header className="material-heading">
        <div>
          <span className="material-eyebrow">MATERIAL / SURFACE SLOT 0</span>
          <h2>{Compact ? "Material channels" : "ShaderEditor"}</h2>
          <p>
            Bound to <b>{Subject.Name}</b> · #{Subject.Id}
          </p>
        </div>
        {Expand && (
          <button
            className="material-expand"
            aria-label="Expand ShaderEditor"
            title="Open dockable ShaderEditor"
            onClick={Expand}
          >
            ↗
          </button>
        )}
      </header>
      <div className="material-properties">
        <label>
          Material name
          <input
            aria-label="Material name"
            value={Material.Name}
            maxLength={80}
            onChange={(Event) =>
              Apply({ ...Material, Name: Event.target.value })
            }
          />
        </label>
        <label>
          Material type
          <select
            aria-label="Material type"
            value={Material.Type}
            onChange={(Event) =>
              Apply(ChooseMaterialType(Material, Event.target.value))
            }
          >
            {MaterialTypes.map((Name) => (
              <option key={Name}>{Name}</option>
            ))}
          </select>
        </label>
      </div>
      <p className="material-type-note">
        {Material.Type === "SSR"
          ? "SSR is a reflection route, not a BSDF. Reflective preset here; screen-space tracing needs the native renderer."
          : "Type presets adjust fill defaults without removing gradient, texture or code drafts."}
      </p>
      <div className="material-stats">
        <div>
          <span>
            <i className="material-dot" />
            Surface channels
          </span>
          <strong>20</strong>
        </div>
        <div>
          <span>
            <i className="material-dot linked" />
            Non-fill sources
          </span>
          <strong>{Authored}</strong>
        </div>
      </div>
      <div className="material-workbench">
        <aside className="channel-library">
          <div className="material-section-title">
            <h3>Channels</h3>
            <small>ALL 20</small>
          </div>
          <input
            className="channel-search"
            aria-label="Search material channels"
            placeholder="Search channels…"
            value={Query}
            onChange={(Event) => Search(Event.target.value)}
          />
          <div className="channel-list">
            {MaterialChannels.filter((Item) =>
              (Item.Label + Item.Group)
                .toLowerCase()
                .includes(Query.toLowerCase()),
            ).map((Item) => {
              const Data = Material.Channels[Item.Id];
              return (
                <button
                  className={Selected === Item.Id ? "active" : ""}
                  key={Item.Id}
                  data-material-channel={Item.Id}
                  aria-pressed={Selected === Item.Id}
                  onClick={() => Select(Item.Id)}
                >
                  <span
                    className="channel-swatch"
                    style={{
                      background:
                        Data.Source === "Gradient"
                          ? GradientCss(Data, Item)
                          : Item.Kind === "Colour"
                            ? Data.Fill
                            : undefined,
                    }}
                  >
                    {Item.Kind !== "Colour" && Data.Source !== "Gradient"
                      ? "◈"
                      : ""}
                  </span>
                  <span className="channel-name">
                    {Item.Label}
                    <small>{Item.Group}</small>
                  </span>
                  <span className="channel-value">
                    {Data.Source === "Fill"
                      ? Item.Kind === "Colour"
                        ? Data.Fill
                        : (+Data.Fill).toFixed(2)
                      : SourceNames[Data.Source]}
                    <small
                      className={Data.Source === "Code" ? "unresolved" : ""}
                    >
                      {Data.Source === "Code"
                        ? "unresolved"
                        : Data.Source === "Texture"
                          ? Data.Texture
                            ? "embedded"
                            : "unassigned"
                          : Data.Source === "Gradient"
                            ? Data.Stops.length + " stops"
                            : "constant"}
                    </small>
                  </span>
                </button>
              );
            })}
          </div>
          {!MaterialChannels.some((Item) =>
            (Item.Label + Item.Group)
              .toLowerCase()
              .includes(Query.toLowerCase()),
          ) && <p>No matching channels.</p>}
        </aside>
        <SourceEditor
          key={Subject.Id + Selected}
          Field={Field}
          Channel={Channel}
          Change={(Patch) => Assign(Selected, Patch)}
        />
        <ShaderballPanel Material={Material} Compact={Compact} />
      </div>
    </div>
  );
}
