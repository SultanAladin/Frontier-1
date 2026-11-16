import { WebGL2PyroEngine } from "./engine-webgl2.js";
import { OrbitCamera } from "./camera.js";
import {
  ComputeColliderPosition,
  SourceRevision,
} from "./SceneSpecification.js";

const Select = (Selector) => document.querySelector(Selector);
const YieldFrame = () =>
  new Promise((Resolve) => requestAnimationFrame(Resolve));
export function SpecifyFlipbook(Count, Size, Rate, Warmup) {
  if (
    ![4, 16, 32, 64].includes(Count) ||
    ![64, 128, 256, 512].includes(Size) ||
    ![12, 24, 30, 60].includes(Rate) ||
    !Number.isFinite(Warmup) ||
    Warmup < 0 ||
    Warmup > 3
  )
    throw new Error(
      "Choose a supported frame count, tile size, frame rate and 0–3 second warm-up.",
    );
  const Columns = Math.ceil(Math.sqrt(Count));
  return {
    Count,
    Size,
    Rate,
    Warmup,
    Columns,
    Rows: Math.ceil(Count / Columns),
    Width: Columns * Size,
    Height: Math.ceil(Count / Columns) * Size,
  };
}
export function UnpackFrame(Bytes, Size, Transparent) {
  const Pixels = new Uint8ClampedArray(Bytes.length);
  for (let Row = 0; Row < Size; Row++)
    for (let Column = 0; Column < Size; Column++) {
      const Source = ((Size - 1 - Row) * Size + Column) * 4;
      const Target = (Row * Size + Column) * 4;
      const Alpha = Transparent
        ? Math.max(
            Bytes[Source + 3],
            Bytes[Source],
            Bytes[Source + 1],
            Bytes[Source + 2],
          )
        : 255;
      for (let Channel = 0; Channel < 3; Channel++)
        Pixels[Target + Channel] = Alpha
          ? Math.round((Bytes[Source + Channel] * 255) / Alpha)
          : 0;
      Pixels[Target + 3] = Alpha;
    }
  return Pixels;
}
export function DownloadBlob(BlobValue, Name) {
  const Url = URL.createObjectURL(BlobValue);
  const Link = document.createElement("a");
  Link.href = Url;
  Link.download = Name;
  Link.click();
  setTimeout(() => URL.revokeObjectURL(Url), 1500);
}

export class FlipbookSequence {
  constructor(Editor) {
    this.Editor = Editor;
    this.Result = null;
    this.Cancelled = false;
    this.PreviewFrame = 0;
    Select("#flipbook-button").addEventListener("click", () => {
      if (Editor.Switching) return;
      Select("#bake-burst").checked = !Editor.Parameters.emitterEnabled;
      Select("#bake-scene-name").textContent =
        `${Select("#document-name").value}.fluid`;
      Select("#flipbook-dialog").showModal();
      this.PreviewTime = performance.now();
      this.PreviewLoop();
    });
    Select("#bake-start").addEventListener("click", () => this.Bake());
    Select("#bake-cancel").addEventListener("click", () => {
      this.Cancelled = true;
      Select("#bake-progress-label").textContent = "Cancelling…";
    });
    Select("#close-flipbook").addEventListener("click", () => this.Close());
    Select("#flipbook-dialog").addEventListener("cancel", (Event) => {
      if (Editor.Baking) {
        Event.preventDefault();
        this.Cancelled = true;
      }
    });
    Select("#bake-frame").addEventListener("input", (Event) => {
      Select("#bake-play").checked = false;
      this.PreviewFrame = Number(Event.target.value);
      this.RenderPreview();
    });
    Select("#bake-png").addEventListener("click", () => {
      if (this.Result)
        DownloadBlob(this.Result.Blob, `${this.Result.Name}.flipbook.png`);
    });
    Select("#bake-json").addEventListener("click", () => {
      if (this.Result)
        DownloadBlob(
          new Blob([JSON.stringify(this.Result.Metadata, null, 2)], {
            type: "application/json",
          }),
          `${this.Result.Name}.flipbook.json`,
        );
    });
  }

  Close() {
    if (this.Editor.Baking) {
      this.Cancelled = true;
      return;
    }
    Select("#flipbook-dialog").close();
  }

  async Bake() {
    if (this.Editor.Baking || this.Editor.Switching || !this.Editor.Engine)
      return;
    let Layout;
    try {
      Layout = SpecifyFlipbook(
        Number(Select("#bake-count").value),
        Number(Select("#bake-size").value),
        Number(Select("#bake-rate").value),
        Number(Select("#bake-warmup").value),
      );
    } catch (ErrorValue) {
      Select("#bake-progress-label").textContent = ErrorValue.message;
      return;
    }
    this.Editor.Baking = true;
    this.Cancelled = false;
    Select("#bake-settings").disabled = true;
    Select("#bake-start").disabled = true;
    Select("#bake-cancel").hidden = false;
    Select("#bake-result").hidden = true;
    Select("#bake-progress").value = 0;
    Select("#bake-progress-label").textContent =
      "Allocating an isolated WebGL2 bake…";
    const Canvas = document.createElement("canvas");
    Canvas.width = Layout.Size;
    Canvas.height = Layout.Size;
    const Transparent = Select("#bake-transparent").checked;
    const StartBurst = Select("#bake-burst").checked;
    const Authored = { ...this.Editor.Parameters };
    const Parameters = {
      ...Authored,
      paused: false,
      autoTurntable: false,
      autoGpuGovernor: false,
      showBoundingBox: false,
      showVoxelGridLines: false,
      showActiveVoxelCells: false,
      showAtlasMinimap: false,
      showFloorGrid: !Transparent && Authored.showFloorGrid,
      renderChannel: 0,
    };
    const Name =
      Select("#document-name")
        .value.replace(/[^a-z0-9 _-]/gi, "")
        .trim() || "scene";
    const Camera = new OrbitCamera();
    Object.assign(Camera, structuredClone(this.Editor.Camera));
    Camera.aspect = 1;
    Camera.targetTheta = Camera.theta;
    Camera.targetPhi = Camera.phi;
    Camera.targetDistance = Camera.distance;
    Camera.targetCenter = [...Camera.target];
    Camera.update(1);
    let Engine;
    try {
      await YieldFrame();
      if (this.Cancelled)
        throw new Error("Bake cancelled. Live scene preserved.");
      Engine = new WebGL2PyroEngine(Canvas, Parameters, {
        transparent: Transparent,
      });
      const Sheet = document.createElement("canvas");
      Sheet.width = Layout.Width;
      Sheet.height = Layout.Height;
      const Context = Sheet.getContext("2d");
      if (!Context)
        throw new Error(
          "Cannot allocate the flipbook sheet. Try fewer or smaller frames.",
        );
      const Readback = new Uint8Array(Layout.Size * Layout.Size * 4);
      const Coordinates = [
        Parameters.obstacleX,
        Parameters.obstacleY,
        Parameters.obstacleZ,
      ];
      const Advance = async (Seconds) => {
        const Steps = Math.max(1, Math.ceil(Seconds * 60));
        for (let Index = 0; Index < Steps; Index++) {
          if (this.Cancelled)
            throw new Error("Bake cancelled. Live scene preserved.");
          [Parameters.obstacleX, Parameters.obstacleY, Parameters.obstacleZ] =
            ComputeColliderPosition(
              {
                ...Parameters,
                obstacleX: Coordinates[0],
                obstacleY: Coordinates[1],
                obstacleZ: Coordinates[2],
              },
              Engine.time,
            );
          Engine.stepSimulation(Seconds / Steps);
          if (Index % 2 === 0) await YieldFrame();
        }
      };
      if (StartBurst) Engine.triggerExplosion();
      if (Layout.Warmup > 0) {
        Select("#bake-progress-label").textContent = "Warming up simulation…";
        await Advance(Layout.Warmup);
      }
      const Frames = [];
      for (let Index = 0; Index < Layout.Count; Index++) {
        await Advance(1 / Layout.Rate);
        Engine.render(Camera);
        const Gl = Engine.gl;
        Gl.readPixels(
          0,
          0,
          Layout.Size,
          Layout.Size,
          Gl.RGBA,
          Gl.UNSIGNED_BYTE,
          Readback,
        );
        if (Gl.isContextLost() || Gl.getError() !== Gl.NO_ERROR)
          throw new Error(
            "GPU bake interrupted. Reduce voxel resolution or tile size and retry.",
          );
        const X = (Index % Layout.Columns) * Layout.Size,
          Y = Math.floor(Index / Layout.Columns) * Layout.Size;
        Context.putImageData(
          new ImageData(
            UnpackFrame(Readback, Layout.Size, Transparent),
            Layout.Size,
            Layout.Size,
          ),
          X,
          Y,
        );
        Frames.push({
          index: Index,
          x: X,
          y: Y,
          width: Layout.Size,
          height: Layout.Size,
          simulationTime: Engine.time,
          uv: [
            X / Layout.Width,
            Y / Layout.Height,
            Layout.Size / Layout.Width,
            Layout.Size / Layout.Height,
          ],
        });
        Select("#bake-progress").value = (Index + 1) / Layout.Count;
        Select("#bake-progress-label").textContent =
          `Baking frame ${Index + 1} / ${Layout.Count}`;
        await YieldFrame();
      }
      if (this.Cancelled)
        throw new Error("Bake cancelled. Live scene preserved.");
      const BlobValue = await new Promise((Resolve) =>
        Sheet.toBlob(Resolve, "image/png"),
      );
      if (!BlobValue)
        throw new Error("PNG encoding failed. Try a smaller sheet.");
      if (this.Cancelled)
        throw new Error("Bake cancelled. Live scene preserved.");
      const Metadata = {
        format: "frontier-fluid-flipbook",
        version: 1,
        name: Name,
        sourceRevision: SourceRevision,
        image: `${Name}.flipbook.png`,
        width: Layout.Width,
        height: Layout.Height,
        columns: Layout.Columns,
        rows: Layout.Rows,
        frameCount: Layout.Count,
        framesPerSecond: Layout.Rate,
        playbackDuration: Layout.Count / Layout.Rate,
        warmup: Layout.Warmup,
        startBurst: StartBurst,
        origin: "top-left",
        uvEncoding: "normalized x, y, width, height",
        simulationTimeUnits: "seconds",
        frameOrder: "left-to-right, top-to-bottom",
        colourSpace: "srgb-tonemapped",
        alphaMode: Transparent
          ? "straight-opacity-and-emission-coverage"
          : "opaque",
        initialization:
          "empty-grid; stochastic turbulence, bursts and embers are not a deterministic replay",
        camera: {
          position: Camera.position,
          forward: Camera.forward,
          up: Camera.up,
          aspect: 1,
          fovY: Camera.fovY,
        },
        sceneParameters: Authored,
        frames: Frames,
      };
      this.Result = { Sheet, Blob: BlobValue, Name, Metadata };
      this.PreviewFrame = 0;
      Select("#bake-frame").max = Layout.Count - 1;
      Select("#bake-frame").value = 0;
      Select("#bake-result-label").textContent =
        `${Name} · ${Layout.Width} × ${Layout.Height} · ${Layout.Count} frames`;
      Select("#bake-progress-label").textContent =
        "Bake complete. Download the PNG sheet and its timing / UV metadata.";
      Select("#bake-result").hidden = false;
      this.RenderPreview();
    } catch (ErrorValue) {
      Select("#bake-progress-label").textContent = ErrorValue.message;
    } finally {
      Engine?.destroy();
      (Engine?.gl || Canvas.getContext("webgl2"))
        ?.getExtension("WEBGL_lose_context")
        ?.loseContext();
      this.Editor.Baking = false;
      this.Editor.LastTime = performance.now();
      Select("#bake-settings").disabled = false;
      Select("#bake-start").disabled = false;
      Select("#bake-cancel").hidden = true;
      Select("#bake-result").hidden = !this.Result;
    }
  }

  RenderPreview() {
    if (!this.Result) return;
    const Frame = this.Result.Metadata.frames[this.PreviewFrame];
    const Canvas = Select("#bake-preview");
    const Context = Canvas.getContext("2d");
    Context.clearRect(0, 0, Canvas.width, Canvas.height);
    Context.drawImage(
      this.Result.Sheet,
      Frame.x,
      Frame.y,
      Frame.width,
      Frame.height,
      0,
      0,
      Canvas.width,
      Canvas.height,
    );
    Select("#bake-frame").value = this.PreviewFrame;
    Select("#bake-frame-label").textContent =
      `${this.PreviewFrame + 1} / ${this.Result.Metadata.frameCount}`;
  }

  PreviewLoop() {
    cancelAnimationFrame(this.PreviewRequest);
    if (!Select("#flipbook-dialog").open) return;
    if (
      this.Result &&
      !this.Editor.Baking &&
      Select("#bake-play").checked &&
      performance.now() - this.PreviewTime >=
        1000 / this.Result.Metadata.framesPerSecond
    ) {
      this.PreviewFrame =
        (this.PreviewFrame + 1) % this.Result.Metadata.frameCount;
      this.PreviewTime = performance.now();
      this.RenderPreview();
    }
    this.PreviewRequest = requestAnimationFrame(() => this.PreviewLoop());
  }
}
