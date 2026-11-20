import React, { useRef, useState, useLayoutEffect } from "react";
import { Clamp } from "./MaterialSpecification.js";

function TransformValue(Props) {
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

export default function TransformPanel({
  Values,
  Change,
  Rows: CustomRows,
  Compact = false,
  Space = "LOCAL SPACE",
}) {
  const Drag = useRef(null);
  const Locked = !!Values.LOCKED;
  const Rows = CustomRows || [
    ["Position", "m", [0, 0, 0], -100000, 100000, 0.01],
    ["Rotation", "deg", [0, 0, 0], -36000, 36000, 0.1],
    ["Scale", "×", [1, 1, 1], 0.001, 1000, 0.01],
  ];
  const Read = (Key, Initial) =>
    Array.isArray(Values[Key]) ? Values[Key] : Initial;
  const Assign = (Key, Initial, Axis, Next, Minimum, Maximum) => {
    const Result = [...Read(Key, Initial)];
    Result[Axis] = +Clamp(Next, Minimum, Maximum).toFixed(4);
    Change(Key, Result);
  };
  return (
    <section className={"transform-card" + (Compact ? "" : " generic-card")}>
      <header>
        <h3>Transform</h3>
        <span>{Space}</span>
      </header>
      <table className="transform-table">
        <thead>
          <tr>
            <th scope="col">Component</th>
            {["X", "Y", "Z"].map((Axis) => (
              <th scope="col" key={Axis} className={"axis-" + Axis}>
                {Axis}
              </th>
            ))}
          </tr>
        </thead>
        <tbody>
          {Rows.map(([Key, Unit, Initial, Minimum, Maximum, Step]) => (
            <tr key={Key}>
              <th scope="row">
                {Key}
                <small>{Unit}</small>
              </th>
              {[0, 1, 2].map((Axis) => (
                <td key={Axis}>
                  <TransformValue
                    type="number"
                    aria-label={Key + " " + ["X", "Y", "Z"][Axis]}
                    min={Minimum}
                    max={Maximum}
                    step={Step}
                    disabled={Locked}
                    value={Read(Key, Initial)[Axis]}
                    onChange={(Event) =>
                      Assign(
                        Key,
                        Initial,
                        Axis,
                        Event.target.value,
                        Minimum,
                        Maximum,
                      )
                    }
                    onPointerDown={(Event) => {
                      if (Event.button !== 0) return;
                      Drag.current = {
                        X: Event.clientX,
                        Value: +Read(Key, Initial)[Axis],
                        Moved: false,
                      };
                      Event.currentTarget.setPointerCapture(Event.pointerId);
                    }}
                    onPointerMove={(Event) => {
                      const Start = Drag.current;
                      if (!Start) return;
                      const Distance = Event.clientX - Start.X;
                      if (Math.abs(Distance) > 4) Start.Moved = true;
                      if (Start.Moved)
                        Assign(
                          Key,
                          Initial,
                          Axis,
                          Start.Value +
                            Distance * Step * (Event.shiftKey ? 0.1 : 1),
                          Minimum,
                          Maximum,
                        );
                    }}
                    onPointerUp={() => (Drag.current = null)}
                    onPointerCancel={() => (Drag.current = null)}
                  />
                </td>
              ))}
            </tr>
          ))}
        </tbody>
      </table>
      <div className="transform-resets">
        {Rows.map(([Key, , Initial]) => (
          <button
            key={Key}
            disabled={Locked}
            aria-label={"Reset " + Key}
            onClick={() => Change(Key, Initial)}
          >
            ↺ {Key}
          </button>
        ))}
      </div>
      <p>
        {Locked
          ? "Object locked. Unlock in Instance to edit."
          : "Type or drag values · Shift for precision."}
        <br />
        Browser transform draft; native scene unchanged.
      </p>
    </section>
  );
}
