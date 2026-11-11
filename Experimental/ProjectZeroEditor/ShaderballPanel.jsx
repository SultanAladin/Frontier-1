import React, { useEffect, useRef, useState } from "react";
import {
  Clamp,
  GradientValue,
  Rgb,
  MaterialChannels,
} from "./MaterialSpecification.js";

let Mesh;
function ReadMesh() {
  if (Mesh) return Mesh;
  const Raw = Uint8Array.from(atob(window.NativeAssets.ShaderBall), (Byte) =>
    Byte.charCodeAt(0),
  );
  const Header = new DataView(Raw.buffer);
  const Count = Header.getUint32(4, true),
    Indices = Header.getUint32(8, true);
  Mesh = {
    Vertices: new Float32Array(Raw.buffer, 16, Count * 8),
    Indices: new Uint32Array(Raw.buffer, 16 + Count * 32, Indices),
    Count,
  };
  return Mesh;
}
function Rasterize(Size, Angle) {
  const { Vertices, Indices, Count } = ReadMesh();
  const Projected = new Float32Array(Count * 8),
    Depth = new Float32Array(Size * Size).fill(-Infinity),
    Surface = new Float32Array(Size * Size * 6);
  const C = Math.cos(Angle),
    S = Math.sin(Angle),
    Pitch = 0.22,
    CP = Math.cos(Pitch),
    SP = Math.sin(Pitch),
    Scale = Size * 0.72;
  for (let Index = 0; Index < Count; Index++) {
    const I = Index * 8,
      X = Vertices[I] * C - Vertices[I + 1] * S,
      D = Vertices[I] * S + Vertices[I + 1] * C,
      Z = Vertices[I + 2] - 0.56;
    Projected[I] = Size / 2 + X * Scale;
    Projected[I + 1] = Size * 0.51 - (Z * CP - D * SP) * Scale;
    Projected[I + 2] = D * CP + Z * SP;
    const NX = Vertices[I + 3] * C - Vertices[I + 4] * S,
      ND = Vertices[I + 3] * S + Vertices[I + 4] * C,
      NZ = Vertices[I + 5];
    Projected[I + 3] = NX;
    Projected[I + 4] = NZ * CP - ND * SP;
    Projected[I + 5] = ND * CP + NZ * SP;
    Projected[I + 6] = Vertices[I + 6];
    Projected[I + 7] = Vertices[I + 7];
  }
  for (let Triangle = 0; Triangle < Indices.length; Triangle += 3) {
    const A = Indices[Triangle] * 8,
      B = Indices[Triangle + 1] * 8,
      C = Indices[Triangle + 2] * 8;
    const AX = Projected[A],
      AY = Projected[A + 1],
      BX = Projected[B],
      BY = Projected[B + 1],
      CX = Projected[C],
      CY = Projected[C + 1];
    const Area = (BY - CY) * (AX - CX) + (CX - BX) * (AY - CY);
    if (Math.abs(Area) < 0.0001) continue;
    const MinX = Math.max(0, Math.floor(Math.min(AX, BX, CX))),
      MaxX = Math.min(Size - 1, Math.ceil(Math.max(AX, BX, CX))),
      MinY = Math.max(0, Math.floor(Math.min(AY, BY, CY))),
      MaxY = Math.min(Size - 1, Math.ceil(Math.max(AY, BY, CY)));
    for (let Y = MinY; Y <= MaxY; Y++)
      for (let X = MinX; X <= MaxX; X++) {
        const WA =
            ((BY - CY) * (X + 0.5 - CX) + (CX - BX) * (Y + 0.5 - CY)) / Area,
          WB = ((CY - AY) * (X + 0.5 - CX) + (AX - CX) * (Y + 0.5 - CY)) / Area,
          WC = 1 - WA - WB;
        if (WA < 0 || WB < 0 || WC < 0) continue;
        const Z =
            WA * Projected[A + 2] +
            WB * Projected[B + 2] +
            WC * Projected[C + 2],
          Pixel = Y * Size + X;
        if (Z <= Depth[Pixel]) continue;
        Depth[Pixel] = Z;
        for (let K = 0; K < 5; K++)
          Surface[Pixel * 6 + K] =
            WA * Projected[A + 3 + K] +
            WB * Projected[B + 3 + K] +
            WC * Projected[C + 3 + K];
        Surface[Pixel * 6 + 5] = 1;
      }
  }
  return Surface;
}
async function ReadTextures(Material) {
  const Textures = {};
  await Promise.all(
    Object.entries(Material.Channels).map(async ([Id, Channel]) => {
      if (Channel.Source !== "Texture" || !Channel.Texture?.Data) return;
      const Image = new window.Image();
      Image.src = Channel.Texture.Data;
      try {
        await Image.decode();
        const Canvas = document.createElement("canvas");
        Canvas.width = Canvas.height = 128;
        const Context = Canvas.getContext("2d");
        Context.drawImage(Image, 0, 0, 128, 128);
        Textures[Id] = Context.getImageData(0, 0, 128, 128).data;
      } catch {}
    }),
  );
  return Textures;
}
function Sample(Material, Textures, Id, U, V) {
  const Channel = Material.Channels[Id],
    Field = MaterialChannels.find((Item) => Item.Id === Id),
    Colour = Field.Kind === "Colour";
  let Value = Channel.Fill;
  if (Channel.Source === "Gradient")
    Value = GradientValue(Channel, Channel.Axis === "V" ? V : U, Colour);
  if (Channel.Source === "Texture" && Textures[Id]) {
    const X = Math.floor(((((U * Channel.Repeat) % 1) + 1) % 1) * 127),
      Y = Math.floor((1 - ((((V * Channel.Repeat) % 1) + 1) % 1)) * 127),
      P = (Y * 128 + X) * 4,
      Data = Textures[Id];
    const Component = { R: 0, G: 1, B: 2, A: 3 }[Channel.Component];
    if (Colour)
      return Component === undefined
        ? [Data[P] / 255, Data[P + 1] / 255, Data[P + 2] / 255]
        : Array(3).fill(Data[P + Component] / 255);
    return (
      Field.Minimum +
      (Data[P + (Component ?? 0)] / 255) * (Field.Maximum - Field.Minimum)
    );
  }
  return Colour ? Rgb(Value) : +Value;
}
export default function ShaderballPanel({ Material, Compact = false }) {
  const Canvas = useRef(null),
    Cache = useRef(null),
    [Angle, Rotate] = useState(0.6),
    [Status, Report] = useState("Preparing mesh"),
    [Studio, ChooseStudio] = useState("Softbox");
  useEffect(() => {
    let Cancelled = false;
    const Timer = setTimeout(async () => {
      try {
        const Size = Compact ? 240 : 360;
        if (
          !Cache.current ||
          Cache.current.Angle !== Angle ||
          Cache.current.Size !== Size
        )
          Cache.current = { Angle, Size, Surface: Rasterize(Size, Angle) };
        const Textures = await ReadTextures(Material);
        if (Cancelled || !Canvas.current) return;
        const Context = Canvas.current.getContext("2d"),
          Image = Context.createImageData(Size, Size),
          Data = Image.data,
          Surface = Cache.current.Surface;
        for (let Pixel = 0; Pixel < Size * Size; Pixel++) {
          const I = Pixel * 6;
          if (!Surface[I + 5]) continue;
          const Length =
              Math.hypot(Surface[I], Surface[I + 1], Surface[I + 2]) || 1,
            NX = Surface[I] / Length,
            NY = Surface[I + 1] / Length,
            NZ = Surface[I + 2] / Length,
            U = Surface[I + 3],
            V = Surface[I + 4];
          const SampleAt = (Id) => Sample(Material, Textures, Id, U, V);
          const Colour = SampleAt("colour"),
            Rough = Clamp(SampleAt("roughness"), 0.03, 1),
            Metal = Clamp(SampleAt("metallic")),
            Coat = SampleAt("coat"),
            Transmission = Clamp(SampleAt("transmission")),
            Ior = SampleAt("ior"),
            F0 = ((Ior - 1) / (Ior + 1)) ** 2;
          const Light = Math.max(0, -NX * 0.48 + NY * 0.57 + NZ * 0.66),
            Rim = (1 - Math.max(0, NZ)) ** 3;
          const Spec = Math.pow(
            Math.max(0, -NX * 0.25 + NY * 0.3 + NZ * 0.92),
            5 + 180 * (1 - Rough) ** 2,
          );
          const Strip = Math.pow(
            Math.max(0, NX * 0.65 + NY * 0.15 + NZ * 0.74),
            20 + 110 * (1 - Rough) ** 2,
          );
          const Sheen = SampleAt("sheen"),
            Emission = SampleAt("emission"),
            AO = SampleAt("occlusion");
          for (let K = 0; K < 3; K++) {
            const Tint =
              Studio === "Warm"
                ? [1.13, 0.99, 0.83][K]
                : Studio === "Cool"
                  ? [0.8, 0.96, 1.2][K]
                  : 1;
            let Result =
              Colour[K] * (0.2 + Light * 0.72) * (1 - Metal * 0.45) * AO +
              Metal * Colour[K] * (0.08 + 0.16 * Math.max(0, NY)) +
              (Spec + Strip * 0.55) *
                (F0 * 3 * (1 - Metal) + Metal * Colour[K] * 1.5) +
              Coat * Spec * 0.5 +
              Sheen[K] * Rim * 0.35 +
              Colour[K] * Emission * 0.3;
            if (Transmission > 0.01)
              Result =
                Result * (1 - Transmission * 0.6) +
                (Colour[K] * 0.24 + Spec * 0.65 + Rim * 0.28) * Transmission;
            if (Material.Type === "Unlit") Result = Colour[K];
            Data[Pixel * 4 + K] = Math.round(
              255 *
                (Material.Type === "Unlit"
                  ? Clamp(Colour[K])
                  : Math.pow(Clamp(Result * Tint), 1 / 2.2)),
            );
          }
          Data[Pixel * 4 + 3] = Math.round(
            255 * Clamp(SampleAt("opacity"), 0, 1),
          );
        }
        Canvas.current.width = Canvas.current.height = Size;
        Context.putImageData(Image, 0, 0);
        Report("Preview ready");
      } catch {
        if (!Cancelled) Report("Preview unavailable");
      }
    }, 50);
    return () => {
      Cancelled = true;
      clearTimeout(Timer);
    };
  }, [Material, Angle, Studio, Compact]);
  return (
    <section className={"shaderball-panel " + (Compact ? "compact" : "")}>
      <header>
        <span>SHADER BALL</span>
        <span className="preview-live">{Status}</span>
      </header>
      <div className="shaderball-stage">
        <div className="shaderball-shadow" />
        <canvas
          ref={Canvas}
          aria-label="Material preview on repository shader ball"
          data-preview-status={Status}
        />
      </div>
      <div className="preview-controls">
        <label>
          Orbit
          <input
            aria-label="Shader ball orbit"
            type="range"
            min="-3.14"
            max="3.14"
            step=".05"
            value={Angle}
            onChange={(Event) => Rotate(+Event.target.value)}
          />
        </label>
        <select
          aria-label="Preview studio"
          value={Studio}
          onChange={(Event) => ChooseStudio(Event.target.value)}
        >
          {["Softbox", "Warm", "Cool"].map((Name) => (
            <option key={Name}>{Name}</option>
          ))}
        </select>
      </div>
      <p>
        Original shader-ball mesh · browser lighting study.
        <br />
        Not the native BSDF. Code links use the stored fill fallback.
      </p>
      {!Compact && (
        <details className="preview-limits">
          <summary>Preview coverage & source</summary>
          <p>
            Live colour, texture UVs, gradients, roughness, metallic, IOR,
            occlusion, emission, opacity, coat, sheen and approximate
            transmission. Orientation, anisotropy, subsurface and displacement
            are authored but not evaluated here. No SSR ray tracing or shader
            compilation.
          </p>
          <p>
            Mesh: existing ShaderBall.mesh (Pseudopode / UnityShaderBall, CC0,
            as identified by the native exhibit).
          </p>
        </details>
      )}
    </section>
  );
}
