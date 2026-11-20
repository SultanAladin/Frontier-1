/*============================================================================================================================================
                                                              VIEWPORT.JS
============================================================================================================================================*/
// Canvas-2D world viewport for the Frontier ForceField Editor: slice-plane heatmap, envelope outlines, lattice arrows,
// bidirectional animated streamlines, advected particle transport, generator markers, grid, and cursor readout anchoring.
// +Z up world space; view planes Top XY / Front XZ / Side YZ with a movable slice depth.

"use strict";

const Viewport = (function ConstructViewport() {
  let CanvasHost = null;
  let Ctx = null;
  let WidthPx = 0;
  let HeightPx = 0;
  let DeviceScale = 1;

  // Shared studio status reference (assigned by Studio.js).
  let StatusRef = null;

  // Heatmap recompute throttle: cache keyed on the inputs that change the slice image.
  let HeatmapKey = "";
  let HeatmapCanvas = null;
  let HeatmapSampleCount = 0;

  // Streamline cache.
  let StreamKey = "";
  let StreamPolylines = [];
  let StreamBudgetTick = 0;

  // Advected particle transport buffers.
  let ParticlePosX = null, ParticlePosY = null, ParticlePosZ = null;
  let ParticleVelX = null, ParticleVelY = null, ParticleVelZ = null;
  let ParticleAge = null, ParticleLife = null;
  let ParticleCapacity = 0;
  const TrailLength = 7;
  let TrailBuffer = null; // Float32Array capacity * TrailLength * 3
  let TrailCursor = 0;

  // Cursor anchor in screen pixels (CSS px), null when the pointer is outside the canvas.
  let CursorScreen = null;

  const BoundsHalf = 3.6;

  //----------------------------------------------------------------------------------------------------------------------
  //                                                 COORDINATE MAPPING
  //----------------------------------------------------------------------------------------------------------------------

  function PlaneAxes(Mode) {
    // Returns {A, B, N, FixedDepthIndex} where A/B are in-plane world axis indices, N the plane-normal axis index.
    if (Mode === "Top XY") return { A: 0, B: 1, N: 2, ScreenSign: [1, -1] };
    if (Mode === "Front XZ") return { A: 0, B: 2, N: 1, ScreenSign: [1, -1] };
    return { A: 1, B: 2, N: 0, ScreenSign: [1, -1] };
  }

  function PixelsPerMetre() {
    return Math.max(24, StatusRef.View.Zoom);
  }

  function WorldToScreen(X, Y, Z) {
    const Mode = StatusRef.View.Mode;
    const Axes = PlaneAxes(Mode);
    const Ppm = PixelsPerMetre();
    const WorldPt = [X, Y, Z];
    const Centre = StatusRef.View.Centre;
    const Cx = WidthPx * 0.5;
    const Cy = HeightPx * 0.5;
    const ScreenX = Cx + (WorldPt[Axes.A] - Centre[Axes.A]) * Ppm * Axes.ScreenSign[0];
    const ScreenY = Cy + (WorldPt[Axes.B] - Centre[Axes.B]) * Ppm * Axes.ScreenSign[1];
    return [ScreenX, ScreenY];
  }

  function ScreenToWorldPlane(ScreenX, ScreenY) {
    const Mode = StatusRef.View.Mode;
    const Axes = PlaneAxes(Mode);
    const Ppm = PixelsPerMetre();
    const Centre = StatusRef.View.Centre;
    const Cx = WidthPx * 0.5;
    const Cy = HeightPx * 0.5;
    const WorldPt = [Centre[0], Centre[1], Centre[2]];
    WorldPt[Axes.A] = Centre[Axes.A] + ((ScreenX - Cx) / Ppm) * Axes.ScreenSign[0];
    WorldPt[Axes.B] = Centre[Axes.B] + ((ScreenY - Cy) / Ppm) * Axes.ScreenSign[1];
    WorldPt[Axes.N] = StatusRef.Overlay.SliceDepth;
    return WorldPt;
  }

  function ScreenDeltaToWorld(DxScreen, DyScreen) {
    const Axes = PlaneAxes(StatusRef.View.Mode);
    const Ppm = PixelsPerMetre();
    const Out = [0, 0, 0];
    Out[Axes.A] = (DxScreen / Ppm) * Axes.ScreenSign[0];
    Out[Axes.B] = (DyScreen / Ppm) * Axes.ScreenSign[1];
    return Out;
  }

  //----------------------------------------------------------------------------------------------------------------------
  //                                                    PARTICLES
//----------------------------------------------------------------------------------------------------------------------

  function RespawnParticle(I, Uniform) {
    const Half = BoundsHalf * 0.92;
    ParticlePosX[I] = (Math.random() * 2 - 1) * Half;
    ParticlePosY[I] = (Math.random() * 2 - 1) * Half;
    ParticlePosZ[I] = Math.max(0.02, (Math.random() * 2 - 1) * Half * 0.7 + 0.55);
    ParticleVelX[I] = 0;
    ParticleVelY[I] = 0;
    ParticleVelZ[I] = 0;
    ParticleAge[I] = -Math.random() * 0.8; // staggered intake delay
    ParticleLife[I] = 7 + Math.random() * 7;
    if (Uniform) {
      ParticleAge[I] = Math.random() * ParticleLife[I];
    }
    const Slot = TrailLength - 1;
    const Base = (I * TrailLength + Slot) * 3;
    TrailBuffer[Base] = ParticlePosX[I];
    TrailBuffer[Base + 1] = ParticlePosY[I];
    TrailBuffer[Base + 2] = ParticlePosZ[I];
  }

  function EnsureParticleCapacity(Count) {
    if (ParticleCapacity >= Count && ParticlePosX) return;
    const NewCapacity = Math.max(Count, 512);
    const NewX = new Float32Array(NewCapacity);
    const NewY = new Float32Array(NewCapacity);
    const NewZ = new Float32Array(NewCapacity);
    const NewVx = new Float32Array(NewCapacity);
    const NewVy = new Float32Array(NewCapacity);
    const NewVz = new Float32Array(NewCapacity);
    const NewAge = new Float32Array(NewCapacity);
    const NewLife = new Float32Array(NewCapacity);
    const NewTrail = new Float32Array(NewCapacity * TrailLength * 3);
    for (let I = 0; I < NewCapacity; ++I) {
      const Copy = I < ParticleCapacity;
      NewX[I] = Copy ? ParticlePosX[I] : 0;
      NewY[I] = Copy ? ParticlePosY[I] : 0;
      NewZ[I] = Copy ? ParticlePosZ[I] : 0;
      NewVx[I] = Copy ? ParticleVelX[I] : 0;
      NewVy[I] = Copy ? ParticleVelY[I] : 0;
      NewVz[I] = Copy ? ParticleVelZ[I] : 0;
      NewAge[I] = Copy ? ParticleAge[I] : 0;
      NewLife[I] = Copy ? ParticleLife[I] : 0;
      if (Copy) {
        for (let Slot = 0; Slot < TrailLength; ++Slot) {
          const Base = (I * TrailLength + Slot) * 3;
          NewTrail[Base] = ParticlePosX[I];
          NewTrail[Base + 1] = ParticlePosY[I];
          NewTrail[Base + 2] = ParticlePosZ[I];
        }
      }
    }
    ParticlePosX = NewX; ParticlePosY = NewY; ParticlePosZ = NewZ;
    ParticleVelX = NewVx; ParticleVelY = NewVy; ParticleVelZ = NewVz;
    ParticleAge = NewAge; ParticleLife = NewLife;
    TrailBuffer = NewTrail;
    const FirstInit = ParticleCapacity === 0;
    ParticleCapacity = NewCapacity;
    for (let I = 0; I < NewCapacity; ++I) {
      RespawnParticle(I, FirstInit);
    }
  }

  function UpdateParticles(DtSec, TimeSec) {
    const Overlay = StatusRef.Overlay;
    const Count = Math.min(Overlay.ParticleCount, ParticleCapacity);
    const Generators = StatusRef.Generators;
    if (!Generators.length) {
      // Free drift with no forces: gently recycle.
      for (let I = 0; I < Count; ++I) {
        ParticlePosX[I] += ParticleVelX[I] * DtSec;
        ParticlePosY[I] += ParticleVelY[I] * DtSec;
        ParticlePosZ[I] += ParticleVelZ[I] * DtSec;
        ParticleAge[I] += DtSec;
        if (ParticleAge[I] > ParticleLife[I]) RespawnParticle(I, false);
      }
      return;
    }
    const Half = BoundsHalf * 1.35;
    const Damping = Math.pow(0.35, DtSec); // mild global drag to keep speeds bounded
    const MaxSpeed = 34.0;
    for (let I = 0; I < Count; ++I) {
      ParticleAge[I] += DtSec;
      if (ParticleAge[I] < 0) continue; // staggered intake delay
      const F = EvaluateCompositeForceAt(Generators, ParticlePosX[I], ParticlePosY[I], ParticlePosZ[I], TimeSec);
      ParticleVelX[I] = (ParticleVelX[I] + F[0] * DtSec) * Damping;
      ParticleVelY[I] = (ParticleVelY[I] + F[1] * DtSec) * Damping;
      ParticleVelZ[I] = (ParticleVelZ[I] + F[2] * DtSec) * Damping;
      const Speed = Math.hypot(ParticleVelX[I], ParticleVelY[I], ParticleVelZ[I]);
      if (Speed > MaxSpeed) {
        const K = MaxSpeed / Speed;
        ParticleVelX[I] *= K; ParticleVelY[I] *= K; ParticleVelZ[I] *= K;
      }
      ParticlePosX[I] += ParticleVelX[I] * DtSec;
      ParticlePosY[I] += ParticleVelY[I] * DtSec;
      ParticlePosZ[I] += ParticleVelZ[I] * DtSec;
      const Outside =
        Math.abs(ParticlePosX[I]) > Half || Math.abs(ParticlePosY[I]) > Half ||
        ParticlePosZ[I] < -0.05 || ParticlePosZ[I] > Half || ParticleAge[I] > ParticleLife[I];
      if (Outside) RespawnParticle(I, false);
    }
  }

  function AppendParticleTrails(Count) {
    TrailCursor = (TrailCursor + 1) % TrailLength;
    for (let I = 0; I < Count; ++I) {
      const Base = (I * TrailLength + TrailCursor) * 3;
      TrailBuffer[Base] = ParticlePosX[I];
      TrailBuffer[Base + 1] = ParticlePosY[I];
      TrailBuffer[Base + 2] = ParticlePosZ[I];
    }
  }

  //----------------------------------------------------------------------------------------------------------------------
  //                                                  RENDER PIPELINE
  //----------------------------------------------------------------------------------------------------------------------

  function ResizeIfNeeded() {
    const Rect = CanvasHost.getBoundingClientRect();
    DeviceScale = Math.min(2, window.devicePixelRatio || 1);
    const W = Math.max(2, Math.round(Rect.width * DeviceScale));
    const H = Math.max(2, Math.round(Rect.height * DeviceScale));
    if (CanvasHost.width !== W || CanvasHost.height !== H) {
      CanvasHost.width = W;
      CanvasHost.height = H;
      HeatmapKey = ""; // force slice rebuild on resize
    }
    WidthPx = W;
    HeightPx = H;
    Ctx.setTransform(DeviceScale, 0, 0, DeviceScale, 0, 0);
  }

  function VisibleWorldRect() {
    const Ppm = PixelsPerMetre();
    const HalfW = WidthPx * 0.5 / Ppm;
    const HalfH = HeightPx * 0.5 / Ppm;
    const Centre = StatusRef.View.Centre;
    const Axes = PlaneAxes(StatusRef.View.Mode);
    return {
      MinA: Centre[Axes.A] - HalfW, MaxA: Centre[Axes.A] + HalfW,
      MinB: Centre[Axes.B] - HalfH, MaxB: Centre[Axes.B] + HalfH,
      HalfW, HalfH,
    };
  }

  function RenderBackdrop() {
    Ctx.fillStyle = "#0b0c10";
    Ctx.fillRect(0, 0, WidthPx / DeviceScale, HeightPx / DeviceScale);
  }

  function RenderGrid() {
    const Overlay = StatusRef.Overlay;
    if (!Overlay.GridEnabled) return;
    const Ppm = PixelsPerMetre();
    const Rect = VisibleWorldRect();
    const Axes = PlaneAxes(StatusRef.View.Mode);
    const Centre = StatusRef.View.Centre;

    // Adaptive minor spacing: keep grid cells between 28 and 90 px.
    let Spacing = 0.25;
    while (Spacing * Ppm < 28) Spacing *= 2;
    while (Spacing * Ppm > 96) Spacing *= 2;

    const Cx = WidthPx * 0.5 / DeviceScale;
    const Cy = HeightPx * 0.5 / DeviceScale;

    Ctx.save();
    Ctx.lineWidth = 1;
    // Minor grid
    Ctx.strokeStyle = "rgba(255,255,255,0.045)";
    Ctx.beginPath();
    for (let A = Math.ceil(Rect.MinA / Spacing) * Spacing; A <= Rect.MaxA; A += Spacing) {
      const Sx = Cx + (A - Centre[Axes.A]) * Ppm;
      Ctx.moveTo(Sx, 0); Ctx.lineTo(Sx, HeightPx / DeviceScale);
    }
    for (let B = Math.ceil(Rect.MinB / Spacing) * Spacing; B <= Rect.MaxB; B += Spacing) {
      const Sy = Cy + (B - Centre[Axes.B]) * Ppm;
      Ctx.moveTo(0, Sy); Ctx.lineTo(WidthPx / DeviceScale, Sy);
    }
    Ctx.stroke();

    // Major grid at 4x spacing
    const Major = Spacing * 4;
    Ctx.strokeStyle = "rgba(255,255,255,0.10)";
    Ctx.beginPath();
    for (let A = Math.ceil(Rect.MinA / Major) * Major; A <= Rect.MaxA; A += Major) {
      const Sx = Cx + (A - Centre[Axes.A]) * Ppm;
      Ctx.moveTo(Sx, 0); Ctx.lineTo(Sx, HeightPx / DeviceScale);
    }
    for (let B = Math.ceil(Rect.MinB / Major) * Major; B <= Rect.MaxB; B += Major) {
      const Sy = Cy + (B - Centre[Axes.B]) * Ppm;
      Ctx.moveTo(0, Sy); Ctx.lineTo(WidthPx / DeviceScale, Sy);
    }
    Ctx.stroke();

    // World origin axes
    const OriginScreen = WorldToScreen(0, 0, 0);
    Ctx.strokeStyle = "rgba(255,120,120,0.55)";
    Ctx.beginPath();
    Ctx.moveTo(OriginScreen[0], 0); Ctx.lineTo(OriginScreen[0], HeightPx / DeviceScale);
    Ctx.stroke();
    Ctx.strokeStyle = "rgba(140,255,160,0.55)";
    Ctx.beginPath();
    Ctx.moveTo(0, OriginScreen[1]); Ctx.lineTo(WidthPx / DeviceScale, OriginScreen[1]);
    Ctx.stroke();

    // Metre labels along major lines
    Ctx.fillStyle = "rgba(240,242,245,0.34)";
    Ctx.font = "10px 'DM Sans', system-ui, sans-serif";
    for (let A = Math.ceil(Rect.MinA / Major) * Major; A <= Rect.MaxA; A += Major) {
      if (Math.abs(A) < 1e-6) continue;
      const Sx = Cx + (A - Centre[Axes.A]) * Ppm;
      Ctx.fillText(A.toFixed(1), Sx + 3, OriginScreen[1] - 4);
    }
    for (let B = Math.ceil(Rect.MinB / Major) * Major; B <= Rect.MaxB; B += Major) {
      if (Math.abs(B) < 1e-6) continue;
      const Sy = Cy + (B - Centre[Axes.B]) * Ppm;
      Ctx.fillText(B.toFixed(1), OriginScreen[0] + 4, Sy - 3);
    }

    // World bounds outline
    Ctx.strokeStyle = "rgba(255,255,255,0.14)";
    Ctx.setLineDash([6, 6]);
    const MinCorner = WorldToScreen(Rect.MinA - (Rect.MinA % 1), Rect.MinB - (Rect.MinB % 1), 0);
    void MinCorner;
    const BoundsMin = WorldToScreen(-BoundsHalf, -BoundsHalf, 0);
    const BoundsMax = WorldToScreen(BoundsHalf, BoundsHalf, 0);
    Ctx.strokeRect(
      Math.min(BoundsMin[0], BoundsMax[0]), Math.min(BoundsMin[1], BoundsMax[1]),
      Math.abs(BoundsMax[0] - BoundsMin[0]), Math.abs(BoundsMax[1] - BoundsMin[1]),
    );
    Ctx.setLineDash([]);
    Ctx.restore();
  }

  function RenderHeatmap(TimeSec) {
    const Overlay = StatusRef.Overlay;
    if (!Overlay.HeatmapEnabled || !StatusRef.Generators.length) return;
    const Key = [
      StatusRef.View.Mode, Overlay.SliceDepth.toFixed(3), Overlay.Quantity, Overlay.Ramp,
      Overlay.FixedMax.toFixed(2), Overlay.HeatmapResolution, StatusRef.GeneratorsSignature, TimeSec.toFixed(3),
    ].join("|");
    const Res = Overlay.HeatmapResolution;
    const Cw = WidthPx / DeviceScale;
    const Ch = HeightPx / DeviceScale;
    const Aspect = Ch / Math.max(1, Cw);
    const SampleW = Res;
    const SampleH = Math.max(8, Math.round(Res * Aspect));

    if (!HeatmapCanvas) {
      HeatmapCanvas = document.createElement("canvas");
    }
    if (HeatmapKey !== Key || HeatmapCanvas.width !== SampleW || HeatmapCanvas.height !== SampleH) {
      HeatmapCanvas.width = SampleW;
      HeatmapCanvas.height = SampleH;
      const HCtx = HeatmapCanvas.getContext("2d");
      const Image = HCtx.createImageData(SampleW, SampleH);
      const Data = Image.data;
      const Rect = VisibleWorldRect();
      const Axes = PlaneAxes(StatusRef.View.Mode);
      const PlanePt = [0, 0, 0];
      PlanePt[Axes.N] = Overlay.SliceDepth;
      const Generators = StatusRef.Generators;
      const FixedMax = Math.max(0.5, Overlay.FixedMax);
      const Signed = Overlay.Quantity === "Divergence" || Overlay.Quantity === "Curl";
      let Idx = 0;
      for (let Row = 0; Row < SampleH; ++Row) {
        const B = Rect.MaxB - ((Row + 0.5) / SampleH) * (Rect.MaxB - Rect.MinB);
        PlanePt[Axes.B] = B;
        for (let Col = 0; Col < SampleW; ++Col) {
          const A = Rect.MinA + ((Col + 0.5) / SampleW) * (Rect.MaxA - Rect.MinA);
          PlanePt[Axes.A] = A;
          const Raw = EvaluateSliceQuantityAt(
            Generators, Overlay.Quantity, PlanePt[0], PlanePt[1], PlanePt[2], TimeSec, StatusRef.View.Mode,
          );
          let Share = Raw / FixedMax;
          if (Signed) Share = Share * 0.5 + 0.5;
          Share = Math.max(0, Math.min(1, Share));
          const Rgb = SampleColourRamp(Overlay.Ramp, Share, StatusRef.DomainTint);
          Data[Idx++] = Math.round(Rgb[0] * 255);
          Data[Idx++] = Math.round(Rgb[1] * 255);
          Data[Idx++] = Math.round(Rgb[2] * 255);
          Data[Idx++] = Math.round(255 * Overlay.HeatmapOpacity);
        }
      }
      HCtx.putImageData(Image, 0, 0);
      HeatmapKey = Key;
      HeatmapSampleCount = SampleW * SampleH;
    }
    Ctx.save();
    Ctx.imageSmoothingEnabled = true;
    Ctx.imageSmoothingQuality = "high";
    Ctx.globalAlpha = 1.0;
    Ctx.drawImage(HeatmapCanvas, 0, 0, Cw, Ch);
    Ctx.restore();
  }

  function EnvelopeOutlineScreen(Gen) {
    const F = Gen.Force;
    const C = Gen.Position;
    const Mode = StatusRef.View.Mode;
    const Centre = WorldToScreen(C[0], C[1], C[2]);
    const Ppm = PixelsPerMetre();
    const R = Math.max(0.1, F.Radius);
    if (F.Reach === "Sphere") {
      return { Kind: "Circle", Centre, RadiusPx: R * Ppm };
    }
    if (F.Reach === "Column") {
      if (Mode === "Top XY") return { Kind: "Circle", Centre, RadiusPx: R * Ppm };
      return { Kind: "Rect", Centre, HalfWpx: R * Ppm, HalfHpx: R * 1.15 * Ppm };
    }
    if (F.Reach === "Box") {
      return { Kind: "Rect", Centre, HalfWpx: R * Ppm, HalfHpx: R * Ppm };
    }
    return { Kind: "Infinite", Centre, RadiusPx: Math.max(30, R * Ppm) };
  }

  function RenderEnvelopes(TimeSec) {
    const Overlay = StatusRef.Overlay;
    if (!Overlay.EnvelopesEnabled) return;
    const DepthAxis = PlaneAxes(StatusRef.View.Mode).N;
    for (const Gen of StatusRef.Generators) {
      if (!Gen.Force.Enabled) continue;
      const Tint = Gen.Force.Tint || GetForceLawMeta(Gen.Force.Model).Tint;
      const DepthOff = Math.abs(Gen.Position[DepthAxis] - Overlay.SliceDepth);
      const Ghost = DepthOff > Gen.Force.Radius;
      const Outline = EnvelopeOutlineScreen(Gen);
      const Selected = Gen.Id === StatusRef.SelectedId;
      Ctx.save();
      Ctx.globalAlpha = Ghost ? 0.28 : (Selected ? 1.0 : 0.62);
      Ctx.lineWidth = Selected ? 2 : 1.3;
      Ctx.strokeStyle = Tint;
      if (Outline.Kind === "Circle") {
        Ctx.beginPath();
        Ctx.arc(Outline.Centre[0], Outline.Centre[1], Outline.RadiusPx, 0, Math.PI * 2);
        Ctx.stroke();
        Ctx.globalAlpha *= 0.10;
        Ctx.fillStyle = Tint;
        Ctx.fill();
      } else if (Outline.Kind === "Rect") {
        Ctx.strokeRect(Outline.Centre[0] - Outline.HalfWpx, Outline.Centre[1] - Outline.HalfHpx, Outline.HalfWpx * 2, Outline.HalfHpx * 2);
        Ctx.globalAlpha *= 0.10;
        Ctx.fillStyle = Tint;
        Ctx.fillRect(Outline.Centre[0] - Outline.HalfWpx, Outline.Centre[1] - Outline.HalfHpx, Outline.HalfWpx * 2, Outline.HalfHpx * 2);
      } else {
        Ctx.setLineDash([5, 7]);
        Ctx.beginPath();
        Ctx.arc(Outline.Centre[0], Outline.Centre[1], Outline.RadiusPx, 0, Math.PI * 2);
        Ctx.stroke();
        Ctx.setLineDash([]);
      }
      if (Selected) {
        Ctx.globalAlpha = 0.9;
        Ctx.setLineDash([4, 5]);
        Ctx.lineDashOffset = -TimeSec * 22;
        Ctx.strokeStyle = "#ffffff";
        Ctx.lineWidth = 1;
        if (Outline.Kind === "Rect") {
          Ctx.strokeRect(Outline.Centre[0] - Outline.HalfWpx - 4, Outline.Centre[1] - Outline.HalfHpx - 4, Outline.HalfWpx * 2 + 8, Outline.HalfHpx * 2 + 8);
        } else {
          Ctx.beginPath();
          Ctx.arc(Outline.Centre[0], Outline.Centre[1], Outline.RadiusPx + 4, 0, Math.PI * 2);
          Ctx.stroke();
        }
        Ctx.setLineDash([]);
      }
      Ctx.restore();
    }
  }

  function RenderLatticeArrows(TimeSec) {
    const Overlay = StatusRef.Overlay;
    if (!Overlay.ArrowsEnabled) return;
    const Rect = VisibleWorldRect();
    const Axes = PlaneAxes(StatusRef.View.Mode);
    const PlanePt = [0, 0, 0];
    PlanePt[Axes.N] = Overlay.SliceDepth;
    const Spacing = Math.max(0.14, Overlay.ArrowSpacing);
    const FixedMax = Math.max(0.5, Overlay.FixedMax);
    const Generators = StatusRef.Generators;
    const SignedQuantity = false;

    Ctx.save();
    Ctx.lineCap = "round";
    for (let A = Math.ceil(Rect.MinA / Spacing) * Spacing; A <= Rect.MaxA; A += Spacing) {
      for (let B = Math.ceil(Rect.MinB / Spacing) * Spacing; B <= Rect.MaxB; B += Spacing) {
        PlanePt[Axes.A] = A;
        PlanePt[Axes.B] = B;
        const F = EvaluateCompositeForceAt(Generators, PlanePt[0], PlanePt[1], PlanePt[2], TimeSec);
        const InPlaneX = F[Axes.A];
        const InPlaneY = F[Axes.B];
        const Mag = Math.hypot(InPlaneX, InPlaneY);
        if (Mag < 0.05) continue;
        const Screen = WorldToScreen(PlanePt[0], PlanePt[1], PlanePt[2]);
        const Share = Math.min(1, Mag / FixedMax);
        void SignedQuantity;
        const Rgb = SampleColourRamp(Overlay.Ramp, Share, StatusRef.DomainTint);
        const LenPx = Math.min(Spacing * PixelsPerMetre() * 0.92, 6 + 34 * Share * Overlay.VectorScale);
        const Ux = (InPlaneX / Mag) * LenPx;
        const Uy = -(InPlaneY / Mag) * LenPx;
        const Ex = Screen[0] + Ux * 0.5;
        const Ey = Screen[1] + Uy * 0.5;
        const Sx = Screen[0] - Ux * 0.5;
        const Sy = Screen[1] - Uy * 0.5;
        Ctx.strokeStyle = RampCss(Overlay.Ramp, Rgb);
        Ctx.globalAlpha = 0.34 + 0.5 * Share;
        Ctx.lineWidth = 1.1 + 1.3 * Share;
        Ctx.beginPath();
        Ctx.moveTo(Sx, Sy);
        Ctx.lineTo(Ex, Ey);
        Ctx.stroke();
        // Arrowhead
        const HeadLen = 3.2 + 3.4 * Share;
        const Ang = Math.atan2(Uy, Ux);
        Ctx.beginPath();
        Ctx.moveTo(Ex, Ey);
        Ctx.lineTo(Ex - HeadLen * Math.cos(Ang - 0.42), Ey - HeadLen * Math.sin(Ang - 0.42));
        Ctx.moveTo(Ex, Ey);
        Ctx.lineTo(Ex - HeadLen * Math.cos(Ang + 0.42), Ey - HeadLen * Math.sin(Ang + 0.42));
        Ctx.stroke();
      }
    }
    Ctx.restore();
  }

  function RenderStreamlines(TimeSec, ForceRebuild) {
    const Overlay = StatusRef.Overlay;
    if (!Overlay.StreamlinesEnabled) { StreamKey = ""; return; }
    const Key = [
      StatusRef.View.Mode, Overlay.SliceDepth.toFixed(3), Overlay.StreamlineCount,
      StatusRef.GeneratorsSignature, ForceRebuild ? "R" : "C",
    ].join("|");

    if (ForceRebuild || StreamKey !== Key || !StreamPolylines.length) {
      StreamPolylines = [];
      const Rect = VisibleWorldRect();
      const Axes = PlaneAxes(StatusRef.View.Mode);
      const Count = Math.max(4, Overlay.StreamlineCount);
      const GridN = Math.max(3, Math.round(Math.sqrt(Count)));
      const PlanePt = [0, 0, 0];
      PlanePt[Axes.N] = Overlay.SliceDepth;
      for (let Row = 0; Row < GridN; ++Row) {
        for (let Col = 0; Col < GridN; ++Col) {
          const A = Rect.MinA + ((Col + 0.5) / GridN) * (Rect.MaxA - Rect.MinA);
          const B = Rect.MinB + ((Row + 0.5) / GridN) * (Rect.MaxB - Rect.MinB);
          PlanePt[Axes.A] = A;
          PlanePt[Axes.B] = B;
          StreamPolylines.push(TraceSliceStreamline(
            StatusRef.Generators, [PlanePt[0], PlanePt[1], PlanePt[2]], StatusRef.View.Mode,
            TimeSec, 130, 0.034, BoundsHalf,
          ));
        }
      }
      StreamKey = Key;
      StreamBudgetTick = 0;
    } else if (StatusRef.Clock.Playing) {
      // Time-dependent laws shift the field; rebuild round-robin in small batches.
      StreamBudgetTick += 1;
      if (StreamBudgetTick >= 10) {
        StreamBudgetTick = 0;
        const Axes = PlaneAxes(StatusRef.View.Mode);
        const PlanePt = [0, 0, 0];
        PlanePt[Axes.N] = Overlay.SliceDepth;
        for (let Slot = 0; Slot < 4 && Slot < StreamPolylines.length; ++Slot) {
          const Pick = Math.floor(Math.random() * StreamPolylines.length);
          const Old = StreamPolylines[Pick];
          if (!Old || !Old.length) continue;
          PlanePt[Axes.A] = Old[0][0];
          PlanePt[Axes.B] = Old[0][1];
          StreamPolylines[Pick] = TraceSliceStreamline(
            StatusRef.Generators, [PlanePt[0], PlanePt[1], PlanePt[2]], StatusRef.View.Mode,
            TimeSec, 130, 0.034, BoundsHalf,
          );
        }
      }
    }

    const FixedMax = Math.max(0.5, Overlay.FixedMax);
    const Drift = Overlay.StreamDrift;
    const DashPhase = -((TimeSec * Drift * 60) % 26);
    Ctx.save();
    Ctx.lineWidth = Overlay.StreamlineThickness;
    Ctx.lineCap = "round";
    Ctx.setLineDash([9, 17]);
    for (const Polyline of StreamPolylines) {
      if (Polyline.length < 2) continue;
      for (let P = 1; P < Polyline.length; ++P) {
        const P0 = Polyline[P - 1];
        const P1 = Polyline[P];
        const S0 = WorldToScreen(P0[0], P0[1], P0[2]);
        const S1 = WorldToScreen(P1[0], P1[1], P1[2]);
        const Share = Math.min(1, P1[3] / FixedMax);
        if (Overlay.StreamColourMode === "Single") {
          Ctx.strokeStyle = Overlay.StreamColour;
          Ctx.globalAlpha = 0.5 + 0.4 * Share;
        } else {
          const Rgb = SampleColourRamp(Overlay.Ramp, Share, StatusRef.DomainTint);
          Ctx.strokeStyle = RampCss(Overlay.Ramp, Rgb);
          Ctx.globalAlpha = 0.55 + 0.35 * Share;
        }
        Ctx.beginPath();
        Ctx.lineDashOffset = DashPhase;
        Ctx.moveTo(S0[0], S0[1]);
        Ctx.lineTo(S1[0], S1[1]);
        Ctx.stroke();
      }
    }
    Ctx.restore();
  }

  function RenderParticles() {
    const Overlay = StatusRef.Overlay;
    if (!Overlay.ParticlesEnabled) return;
    const Count = Math.min(Overlay.ParticleCount, ParticleCapacity);
    const Generators = StatusRef.Generators;
    const Axes = PlaneAxes(StatusRef.View.Mode);
    const DepthAxis = Axes.N;
    const SliceDepth = Overlay.SliceDepth;
    const FixedMax = Math.max(0.5, Overlay.FixedMax);
    const SizePx = Overlay.ParticleSize;
    const Half = BoundsHalf * 1.35;

    Ctx.save();
    for (let I = 0; I < Count; ++I) {
      if (ParticleAge[I] < 0) continue;
      const WorldPt = [ParticlePosX[I], ParticlePosY[I], ParticlePosZ[I]];
      // Trail first (oldest to newest)
      const TrailSegments = Math.min(TrailLength, Overlay.TrailLength);
      if (TrailSegments >= 2) {
        for (let T = 1; T < TrailSegments; ++T) {
          const Slot0 = (TrailCursor + TrailLength + 1 - TrailSegments + T - 1) % TrailLength;
          const Slot1 = (TrailCursor + TrailLength + 1 - TrailSegments + T) % TrailLength;
          const B0 = (I * TrailLength + Slot0) * 3;
          const B1 = (I * TrailLength + Slot1) * 3;
          const WP0 = [TrailBuffer[B0], TrailBuffer[B0 + 1], TrailBuffer[B0 + 2]];
          const WP1 = [TrailBuffer[B1], TrailBuffer[B1 + 1], TrailBuffer[B1 + 2]];
          if (Math.abs(WP1[0]) > Half || Math.abs(WP1[1]) > Half || WP1[2] < -0.05) continue;
          const DepthFade = Math.exp(-Math.pow((WP1[DepthAxis] - SliceDepth) / 1.05, 2));
          if (DepthFade < 0.03) continue;
          const SpeedShare = Math.min(1, Math.hypot(ParticleVelX[I], ParticleVelY[I], ParticleVelZ[I]) / FixedMax);
          const Rgb = SampleColourRamp(Overlay.ParticleRamp, SpeedShare, StatusRef.DomainTint);
          const S0 = WorldToScreen(WP0[0], WP0[1], WP0[2]);
          const S1 = WorldToScreen(WP1[0], WP1[1], WP1[2]);
          Ctx.strokeStyle = RampCss(Overlay.ParticleRamp, Rgb);
          Ctx.globalAlpha = 0.05 + 0.16 * DepthFade * (T / TrailSegments);
          Ctx.lineWidth = 1;
          Ctx.beginPath();
          Ctx.moveTo(S0[0], S0[1]);
          Ctx.lineTo(S1[0], S1[1]);
          Ctx.stroke();
        }
      }
      const DepthFade = Math.exp(-Math.pow((WorldPt[DepthAxis] - SliceDepth) / 1.05, 2));
      if (DepthFade < 0.02) continue;
      const Speed = Math.hypot(ParticleVelX[I], ParticleVelY[I], ParticleVelZ[I]);
      const Share = Math.min(1, Speed / FixedMax);
      const Rgb = SampleColourRamp(Overlay.ParticleRamp, Share, StatusRef.DomainTint);
      const Screen = WorldToScreen(WorldPt[0], WorldPt[1], WorldPt[2]);
      Ctx.fillStyle = RampCss(Overlay.ParticleRamp, Rgb);
      Ctx.globalAlpha = 0.22 + 0.72 * DepthFade;
      Ctx.beginPath();
      Ctx.arc(Screen[0], Screen[1], SizePx * (0.6 + 0.7 * Share), 0, Math.PI * 2);
      Ctx.fill();
    }
    Ctx.restore();
  }

  function RenderGeneratorMarkers() {
    const DepthAxis = PlaneAxes(StatusRef.View.Mode).N;
    Ctx.save();
    Ctx.font = "11px 'DM Sans', system-ui, sans-serif";
    for (const Gen of StatusRef.Generators) {
      if (!Gen.Force.Enabled) continue;
      const Meta = GetForceLawMeta(Gen.Force.Model);
      const Tint = Gen.Force.Tint || Meta.Tint;
      const Screen = WorldToScreen(Gen.Position[0], Gen.Position[1], Gen.Position[2]);
      const Ghost = Math.abs(Gen.Position[DepthAxis] - StatusRef.Overlay.SliceDepth) > Gen.Force.Radius;
      const Selected = Gen.Id === StatusRef.SelectedId;
      Ctx.globalAlpha = Ghost ? 0.4 : 1;
      // Centre marker
      Ctx.fillStyle = Tint;
      Ctx.beginPath();
      Ctx.arc(Screen[0], Screen[1], Selected ? 5.5 : 4, 0, Math.PI * 2);
      Ctx.fill();
      Ctx.strokeStyle = "rgba(0,0,0,0.55)";
      Ctx.lineWidth = 1.5;
      Ctx.stroke();
      if (Selected) {
        Ctx.strokeStyle = "#ffffff";
        Ctx.lineWidth = 1.4;
        Ctx.beginPath();
        Ctx.arc(Screen[0], Screen[1], 9.5, 0, Math.PI * 2);
        Ctx.stroke();
      }
      // Name tag
      const Label = Gen.Name;
      const TextW = Ctx.measureText(Label).width;
      Ctx.globalAlpha = Ghost ? 0.35 : 0.92;
      Ctx.fillStyle = "rgba(8,9,12,0.72)";
      const TagX = Screen[0] + 10;
      const TagY = Screen[1] - 20;
      Ctx.fillRect(TagX - 5, TagY - 11, TextW + 10, 16);
      Ctx.fillStyle = Selected ? "#ffffff" : Tint;
      Ctx.fillText(Label, TagX, TagY);
      Ctx.globalAlpha = 1;
    }
    Ctx.restore();
  }

  function RenderCursorCrosshair() {
    if (!CursorScreen) return;
    Ctx.save();
    Ctx.strokeStyle = "rgba(255,255,255,0.28)";
    Ctx.lineWidth = 1;
    const R = 7;
    Ctx.beginPath();
    Ctx.moveTo(CursorScreen[0] - R, CursorScreen[1]);
    Ctx.lineTo(CursorScreen[0] - 2, CursorScreen[1]);
    Ctx.moveTo(CursorScreen[0] + 2, CursorScreen[1]);
    Ctx.lineTo(CursorScreen[0] + R, CursorScreen[1]);
    Ctx.moveTo(CursorScreen[0], CursorScreen[1] - R);
    Ctx.lineTo(CursorScreen[0], CursorScreen[1] - 2);
    Ctx.moveTo(CursorScreen[0], CursorScreen[1] + 2);
    Ctx.lineTo(CursorScreen[0], CursorScreen[1] + R);
    Ctx.stroke();
    Ctx.restore();
  }

  function RenderTick(TimeSec, DtSec) {
    if (!Ctx || !StatusRef) return { HeatmapSamples: 0 };
    const ForceStreamRebuild = StatusRef.ViewportDirty === true;
    StatusRef.ViewportDirty = false;
    ResizeIfNeeded();
    RenderBackdrop();
    RenderGrid();
    RenderHeatmap(TimeSec);
    RenderEnvelopes(TimeSec);
    RenderLatticeArrows(TimeSec);
    const ForceStreamRebuildConsumed = ForceStreamRebuild;
    void ForceStreamRebuildConsumed;
    RenderStreamlines(TimeSec, ForceStreamRebuild);
    if (StatusRef.Clock.Playing) {
      UpdateParticles(DtSec, TimeSec);
      AppendParticleTrails(Math.min(StatusRef.Overlay.ParticleCount, ParticleCapacity));
    }
    RenderParticles();
    RenderGeneratorMarkers();
    RenderCursorCrosshair();
    return { HeatmapSamples: HeatmapSampleCount };
  }

  //----------------------------------------------------------------------------------------------------------------------
  //                                                 PUBLIC INTERFACE
  //----------------------------------------------------------------------------------------------------------------------

  return {
    AssignCanvas(CanvasEl, Status) {
      CanvasHost = CanvasEl;
      try {
        Ctx = CanvasEl.getContext("2d");
      } catch (Err) {
        Ctx = null; // headless/dom-only profiles: logic paths stay testable
      }
      StatusRef = Status;
    },
    RenderTick,
    WorldToScreen,
    ScreenToWorldPlane,
    ScreenDeltaToWorld,
    PlaneAxes,
    PickGenerator(ScreenX, ScreenY) {
      if (!StatusRef) return null;
      const DepthAxis = PlaneAxes(StatusRef.View.Mode).N;
      let Best = null;
      let BestDist = 18;
      for (const Gen of StatusRef.Generators) {
        if (!Gen.Force.Enabled) continue;
        const Screen = WorldToScreen(Gen.Position[0], Gen.Position[1], Gen.Position[2]);
        const D = Math.hypot(Screen[0] - ScreenX, Screen[1] - ScreenY);
        const Ghost = Math.abs(Gen.Position[DepthAxis] - StatusRef.Overlay.SliceDepth) > Gen.Force.Radius;
        const Limit = Ghost ? 10 : BestDist;
        if (D < Limit) { Best = Gen; BestDist = D; }
      }
      return Best;
    },
    NotifyPointerMove(ScreenX, ScreenY) {
      CursorScreen = ScreenX === null ? null : [ScreenX, ScreenY];
    },
    NotifyViewChanged() {
      HeatmapKey = "";
      StreamKey = "";
    },
    NotifySceneChanged() {
      HeatmapKey = "";
      StreamKey = "";
      StreamPolylines = [];
    },
    ResetParticles() {
      for (let I = 0; ParticleCapacity && I < ParticleCapacity; ++I) {
        RespawnParticle(I, true);
      }
    },
  };
})();
