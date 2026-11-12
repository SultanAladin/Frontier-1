import React from "react";
import { createRoot } from "react-dom/client";
import TransformPanel from "./TransformPanel.jsx";

// Legacy target-authored lights retain their orientation when moving to rotation controls.
export function LightRotation(Properties) {
  if (Array.isArray(Properties.rot)) return Properties.rot;
  if (!Array.isArray(Properties.target)) return [0, 0, 0];
  const Position = Properties.pos || [0, 0, 0];
  const Direction = (Properties.target || [0, 0, -1]).map(
    (Value, Axis) => Value - Position[Axis],
  );
  const Length = Math.hypot(...Direction);
  return Length < 1e-8
    ? [0, 0, 0]
    : [
        (Math.asin(Direction[1] / Length) * 180) / Math.PI,
        (Math.atan2(-Direction[0], -Direction[2]) * 180) / Math.PI,
        0,
      ].map((Value) => +Value.toFixed(4));
}
export function MountLightTransform(Element, Subject, Context) {
  const Root = createRoot(Element);
  const Write = (Key, Value) => {
    if (Subject.locked) return;
    const Properties = Subject.props,
      Position = Properties.pos || [0, 0, 0];
    const Target = Properties.target;
    if (Key === "Position" && Target) {
      Context.setProp(
        Subject,
        "target",
        Target.map(
          (Coordinate, Axis) => Coordinate + Value[Axis] - Position[Axis],
        ),
      );
    }
    if (Key === "Rotation" && Target) {
      const Length =
        Math.hypot(
          ...Target.map((Coordinate, Axis) => Coordinate - Position[Axis]),
        ) || 1;
      const Pitch = (Value[0] * Math.PI) / 180,
        Yaw = (Value[1] * Math.PI) / 180;
      const Forward = [
        -Math.sin(Yaw) * Math.cos(Pitch),
        Math.sin(Pitch),
        -Math.cos(Yaw) * Math.cos(Pitch),
      ];
      Context.setProp(
        Subject,
        "target",
        Position.map((Coordinate, Axis) => Coordinate + Forward[Axis] * Length),
      );
    }
    Context.setProp(
      Subject,
      { Position: "pos", Rotation: "rot", Scale: "scale" }[Key],
      Value,
    );
    Refresh();
  };
  function Refresh() {
    Root.render(
      <TransformPanel
        Space="WORLD SPACE"
        Values={{
          Position: Subject.props.pos || [0, 0, 0],
          Rotation: LightRotation(Subject.props),
          Scale: Subject.props.scale || [1, 1, 1],
          LOCKED: Subject.locked,
        }}
        Change={Write}
      />,
    );
  }
  Refresh();
  return { Refresh, Dispose: () => Root.unmount() };
}
