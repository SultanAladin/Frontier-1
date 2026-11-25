/*============================================================================================================================================
                                                    BAKEDLIGHTGRIDWORKBENCH.JS
============================================================================================================================================*/
// 🧩 Interactive WebGL2 Workbench proving Multi-Resolution 8x4x4 Spatial-Directional Baked Light Grids with Nanite Distance-LOD,
//    Zero Center-Averaging of separated lamps, Live |GroundTruth - Grid| Error Heatmaps, and Iterative Residual-Error Optimization.

import {
    GridDimX,
    GridDimY,
    GridDimZ,
    TileResLod0,
    GridBakingMode,
    ConstructSceneLightClusters,
    BakeInitialLightClusterGrid,
    ComputeClusterNaniteLod,
    GenerateCalibrationReceivers,
    MeasureClusterGridError,
    StepResidualErrorOptimization,
    PackGpuLightGridTextures
} from "./LightGridBaker.js";

import {
    SurfaceVertexFloats,
    DebugVertexFloats,
    DecodeFct1Binary,
    BuildPresetSceneSurfaces,
    BuildDebugOverlayGeometry
} from "./SceneGeometryBuilder.js";

import {
    SurfaceVertexShaderSource,
    SurfaceFragmentShaderSource,
    DebugOverlayVertexShaderSource,
    DebugOverlayFragmentShaderSource
} from "./GridWorkbenchShaders.js";

//============================================================================================================================================
//                                                  3D CAMERA & PERSPECTIVE TRANSFORMS
//============================================================================================================================================

function BuildPerspectiveXf(FovYRad, Aspect, NearZ, FarZ)
{
    const F = 1.0 / Math.tan(0.5 * FovYRad);
    const Nf = 1.0 / (NearZ - FarZ);
    const Out = new Float32Array(16);
    Out[0]  = F / Math.max(Aspect, 1e-4);
    Out[5]  = F;
    Out[10] = (FarZ + NearZ) * Nf;
    Out[11] = -1.0;
    Out[14] = 2.0 * FarZ * NearZ * Nf;
    return Out;
}

function BuildLookAtXf(Eye, Target, UpVec)
{
    let Zx = Eye[0] - Target[0], Zy = Eye[1] - Target[1], Zz = Eye[2] - Target[2];
    const ZLen = Math.hypot(Zx, Zy, Zz) || 1.0;
    Zx /= ZLen; Zy /= ZLen; Zz /= ZLen;

    let Xx = UpVec[1] * Zz - UpVec[2] * Zy;
    let Xy = UpVec[2] * Zx - UpVec[0] * Zz;
    let Xz = UpVec[0] * Zy - UpVec[1] * Zx;
    const XLen = Math.hypot(Xx, Xy, Xz) || 1.0;
    Xx /= XLen; Xy /= XLen; Xz /= XLen;

    const Yx = Zy * Xz - Zz * Xy;
    const Yy = Zz * Xx - Zx * Xz;
    const Yz = Zx * Xy - Zy * Xx;

    const Out = new Float32Array(16);
    Out[0] = Xx; Out[4] = Xy; Out[8]  = Xz; Out[12] = -(Xx * Eye[0] + Xy * Eye[1] + Xz * Eye[2]);
    Out[1] = Yx; Out[5] = Yy; Out[9]  = Yz; Out[13] = -(Yx * Eye[0] + Yy * Eye[1] + Yz * Eye[2]);
    Out[2] = Zx; Out[6] = Zy; Out[10] = Zz; Out[14] = -(Zx * Eye[0] + Zy * Eye[1] + Zz * Eye[2]);
    Out[3] = 0;  Out[7] = 0;  Out[11] = 0;  Out[15] = 1.0;
    return Out;
}

function MultiplyXf4(A, B)
{
    const Out = new Float32Array(16);
    for (let Col = 0; Col < 4; ++Col)
    {
        const B0 = B[Col * 4 + 0], B1 = B[Col * 4 + 1], B2 = B[Col * 4 + 2], B3 = B[Col * 4 + 3];
        Out[Col * 4 + 0] = A[0] * B0 + A[4] * B1 + A[8]  * B2 + A[12] * B3;
        Out[Col * 4 + 1] = A[1] * B0 + A[5] * B1 + A[9]  * B2 + A[13] * B3;
        Out[Col * 4 + 2] = A[2] * B0 + A[6] * B1 + A[10] * B2 + A[14] * B3;
        Out[Col * 4 + 3] = A[3] * B0 + A[7] * B1 + A[11] * B2 + A[15] * B3;
    }
    return Out;
}

//============================================================================================================================================
//                                                        WEBGL2 COMPILER & TEXTURES
//============================================================================================================================================

function CompileGlShader(Gl, ShaderSort, SourceCode)
{
    const Sh = Gl.createShader(ShaderSort);
    Gl.shaderSource(Sh, SourceCode);
    Gl.compileShader(Sh);
    if (!Gl.getShaderParameter(Sh, Gl.COMPILE_STATUS))
    {
        const Log = Gl.getShaderInfoLog(Sh);
        Gl.deleteShader(Sh);
        throw new Error("GLSL compile failure: " + Log);
    }
    return Sh;
}

function LinkGlProgram(Gl, VertSrc, FragSrc)
{
    const Vs = CompileGlShader(Gl, Gl.VERTEX_SHADER, VertSrc);
    const Fs = CompileGlShader(Gl, Gl.FRAGMENT_SHADER, FragSrc);
    const Prog = Gl.createProgram();
    Gl.attachShader(Prog, Vs);
    Gl.attachShader(Prog, Fs);
    Gl.linkProgram(Prog);
    Gl.deleteShader(Vs);
    Gl.deleteShader(Fs);
    if (!Gl.getProgramParameter(Prog, Gl.LINK_STATUS))
    {
        throw new Error("GLSL link failure: " + Gl.getProgramInfoLog(Prog));
    }
    return Prog;
}

function UploadRgba32fTexture(Gl, TexHandle, Width, Height, FloatArray)
{
    Gl.bindTexture(Gl.TEXTURE_2D, TexHandle);
    Gl.texParameteri(Gl.TEXTURE_2D, Gl.TEXTURE_MIN_FILTER, Gl.NEAREST);
    Gl.texParameteri(Gl.TEXTURE_2D, Gl.TEXTURE_MAG_FILTER, Gl.NEAREST);
    Gl.texParameteri(Gl.TEXTURE_2D, Gl.TEXTURE_WRAP_S, Gl.CLAMP_TO_EDGE);
    Gl.texParameteri(Gl.TEXTURE_2D, Gl.TEXTURE_WRAP_T, Gl.CLAMP_TO_EDGE);
    Gl.pixelStorei(Gl.UNPACK_ALIGNMENT, 1);
    Gl.texImage2D(Gl.TEXTURE_2D, 0, Gl.RGBA32F, Width, Height, 0, Gl.RGBA, Gl.FLOAT, FloatArray);
}

//============================================================================================================================================
//                                                    WORKBENCH APPLICATION RUNTIME
//============================================================================================================================================

async function FetchOptionalBinary(RelPath)
{
    try
    {
        const Resp = await fetch(new URL(RelPath, import.meta.url));
        if (!Resp.ok) return null;
        const Buf = await Resp.arrayBuffer();
        return DecodeFct1Binary(Buf);
    }
    catch (_Err)
    {
        return null;
    }
}

async function BootLightGridWorkbench()
{
    const GlCanvas = document.getElementById("glViewportCanvas");
    const Gl = GlCanvas.getContext("webgl2", { antialias: true, alpha: false, depth: true });
    if (!Gl)
    {
        document.getElementById("statusBannerText").textContent = "⚠️ WebGL2 is unavailable in this browser.";
        return;
    }

    const SurfaceProg = LinkGlProgram(Gl, SurfaceVertexShaderSource, SurfaceFragmentShaderSource);
    const DebugProg   = LinkGlProgram(Gl, DebugOverlayVertexShaderSource, DebugOverlayFragmentShaderSource);

    const SurfUniforms = {
        ViewProjXf:     Gl.getUniformLocation(SurfaceProg, "uViewProjXf"),
        CameraPos:      Gl.getUniformLocation(SurfaceProg, "uCameraPos"),
        RenderViewMode: Gl.getUniformLocation(SurfaceProg, "uRenderViewMode"),
        BakingMode:     Gl.getUniformLocation(SurfaceProg, "uBakingMode"),
        EnableDistLod:  Gl.getUniformLocation(SurfaceProg, "uEnableDistLod"),
        LodBias:        Gl.getUniformLocation(SurfaceProg, "uLodBias"),
        Exposure:       Gl.getUniformLocation(SurfaceProg, "uExposure"),
        ErrorGain:      Gl.getUniformLocation(SurfaceProg, "uErrorGain"),
        RawSourceCount: Gl.getUniformLocation(SurfaceProg, "uRawSourceCount"),
        RawSourcesTex:  Gl.getUniformLocation(SurfaceProg, "uRawSourcesTex"),
        ClusterCount:   Gl.getUniformLocation(SurfaceProg, "uClusterCount"),
        ClusterMetaTex: Gl.getUniformLocation(SurfaceProg, "uClusterMetaTex"),
        CellAnchorTex:  Gl.getUniformLocation(SurfaceProg, "uCellAnchorTex"),
        OctSheetTex:    Gl.getUniformLocation(SurfaceProg, "uOctSheetTex")
    };

    const DbgUniforms = {
        ViewProjXf: Gl.getUniformLocation(DebugProg, "uViewProjXf"),
        AlphaScale: Gl.getUniformLocation(DebugProg, "uAlphaScale")
    };

    // GPU Geometry Buffers & VAOs
    const SurfaceVao = Gl.createVertexArray();
    const SurfaceVbo = Gl.createBuffer();
    const SurfaceEbo = Gl.createBuffer();

    Gl.bindVertexArray(SurfaceVao);
    Gl.bindBuffer(Gl.ARRAY_BUFFER, SurfaceVbo);
    const SurfStrideBytes = SurfaceVertexFloats * 4;
    Gl.enableVertexAttribArray(0);
    Gl.vertexAttribPointer(0, 3, Gl.FLOAT, false, SurfStrideBytes, 0);
    Gl.enableVertexAttribArray(1);
    Gl.vertexAttribPointer(1, 3, Gl.FLOAT, false, SurfStrideBytes, 12);
    Gl.enableVertexAttribArray(2);
    Gl.vertexAttribPointer(2, 3, Gl.FLOAT, false, SurfStrideBytes, 24);
    Gl.enableVertexAttribArray(3);
    Gl.vertexAttribPointer(3, 1, Gl.FLOAT, false, SurfStrideBytes, 36);
    Gl.bindBuffer(Gl.ELEMENT_ARRAY_BUFFER, SurfaceEbo);
    Gl.bindVertexArray(null);

    const DebugLineVao = Gl.createVertexArray();
    const DebugLineVbo = Gl.createBuffer();
    const DbgStrideBytes = DebugVertexFloats * 4;
    Gl.bindVertexArray(DebugLineVao);
    Gl.bindBuffer(Gl.ARRAY_BUFFER, DebugLineVbo);
    Gl.enableVertexAttribArray(0);
    Gl.vertexAttribPointer(0, 3, Gl.FLOAT, false, DbgStrideBytes, 0);
    Gl.enableVertexAttribArray(1);
    Gl.vertexAttribPointer(1, 4, Gl.FLOAT, false, DbgStrideBytes, 12);

    const DebugSolidVao = Gl.createVertexArray();
    const DebugSolidVbo = Gl.createBuffer();
    Gl.bindVertexArray(DebugSolidVao);
    Gl.bindBuffer(Gl.ARRAY_BUFFER, DebugSolidVbo);
    Gl.enableVertexAttribArray(0);
    Gl.vertexAttribPointer(0, 3, Gl.FLOAT, false, DbgStrideBytes, 0);
    Gl.enableVertexAttribArray(1);
    Gl.vertexAttribPointer(1, 4, Gl.FLOAT, false, DbgStrideBytes, 12);
    Gl.bindVertexArray(null);

    // 4 RGBA32F GPU Light Textures
    const RawSourcesTex  = Gl.createTexture();
    const ClusterMetaTex = Gl.createTexture();
    const CellAnchorTex  = Gl.createTexture();
    const OctSheetTex    = Gl.createTexture();

    // Load ControlVehicle FCT1 parts
    const [CarBody, CarRim, CarTyre] = await Promise.all([
        FetchOptionalBinary("./Content/ControlVehicleBody.bin"),
        FetchOptionalBinary("./Content/ControlVehicleRim.bin"),
        FetchOptionalBinary("./Content/ControlVehicleTyre.bin")
    ]);
    const VehicleParts = { Body: CarBody, Rim: CarRim, Tyre: CarTyre };

    // Live Workbench Session Parameters
    const Session = {
        PresetId: "dual-headlamps",
        SplitLayout: "tri-split", // "tri-split" | "dual-split" | "grid-only" | "gt-only" | "lod-diag"
        BakingMode: GridBakingMode.ErrorOptimizedMip,
        EnableDistLod: true,
        LodBias: 0.0,
        Exposure: 1.35,
        ErrorGain: 6.0,
        DebugToggles: {
            Show3dGrid: true,
            ShowRawLights: true,
            ShowLodRings: true
        },
        Camera: {
            Yaw: 0.62,
            Pitch: 0.34,
            Distance: 7.4,
            Target: [0.0, 0.65, 3.2]
        },
        CompiledClusters: [],
        PrimaryReceivers: [],
        MetricsNaive: null,
        MetricsUnopt: null,
        MetricsOpt: null,
        SelectedCellSlot: 0,
        SurfaceIndexCount: 0,
        DebugLineCount: 0,
        DebugSolidCount: 0,
        AnimatedOptStepsRemaining: 0
    };

    function ComputeCameraEye()
    {
        const C = Session.Camera;
        const CosP = Math.cos(C.Pitch);
        return [
            C.Target[0] + Math.sin(C.Yaw) * CosP * C.Distance,
            Math.max(0.18, C.Target[1] + Math.sin(C.Pitch) * C.Distance),
            C.Target[2] + Math.cos(C.Yaw) * CosP * C.Distance
        ];
    }

    // Clones optimized SubOffset, ApertureRadius & OptTile16 from archetype clusters onto matching hall clusters
    function PropagateArchetypeOptimizationToHall()
    {
        if (Session.CompiledClusters.length <= 1) return;
        const CarRef = Session.CompiledClusters[0];
        const PanelRef = Session.CompiledClusters[3];
        for (let Idx = 1; Idx < Session.CompiledClusters.length; ++Idx)
        {
            if (Idx === 3) continue;
            const Dst = Session.CompiledClusters[Idx];
            const Src = Idx < 3 ? CarRef : PanelRef;
            const ScaleRatio = Dst.Cluster.Sources[0].IntensityCd / Math.max(Src.Cluster.Sources[0].IntensityCd, 1e-4);
            for (let K = 0; K < Math.min(Dst.ActiveCellsLod0.length, Src.ActiveCellsLod0.length); ++K)
            {
                Dst.ActiveCellsLod0[K].SubOffset = [
                    Src.ActiveCellsLod0[K].SubOffset[0],
                    Src.ActiveCellsLod0[K].SubOffset[1],
                    Src.ActiveCellsLod0[K].SubOffset[2]
                ];
                Dst.ActiveCellsLod0[K].ApertureRadius = Src.ActiveCellsLod0[K].ApertureRadius;
                for (let T = 0; T < Src.ActiveCellsLod0[K].OptTile16.length; ++T)
                {
                    Dst.ActiveCellsLod0[K].OptTile16[T] = Src.ActiveCellsLod0[K].OptTile16[T] * ScaleRatio;
                }
            }
            for (let T = 0; T < Src.UnifiedLod2.Tile12.length; ++T)
            {
                Dst.UnifiedLod2.Tile12[T] = Src.UnifiedLod2.Tile12[T] * ScaleRatio;
            }
            Dst.OptimizationSteps = Src.OptimizationSteps;
        }
    }

    function SyncGpuGridWithDebug()
    {
        const Packed = PackGpuLightGridTextures(Session.CompiledClusters, Session.BakingMode);
        UploadRgba32fTexture(Gl, RawSourcesTex,  Packed.RawTexWidth,        Packed.RawTexHeight,        Packed.RawTexels);
        UploadRgba32fTexture(Gl, ClusterMetaTex, Packed.ClusterMetaWidth,   Packed.ClusterMetaHeight,   Packed.ClusterMetaTexels);
        UploadRgba32fTexture(Gl, CellAnchorTex,  Packed.CellAnchorTexWidth, Packed.CellAnchorTexHeight, Packed.CellAnchorTexels);
        UploadRgba32fTexture(Gl, OctSheetTex,    Packed.SheetDim,           Packed.SheetDim,            Packed.SheetPixels);

        Session.RawSourceCount = Packed.RawSourceCount;
        Session.ClusterCount   = Packed.ClusterCount;

        const Dbg = BuildDebugOverlayGeometry(Session.CompiledClusters, Session.BakingMode, Session.DebugToggles);
        Gl.bindBuffer(Gl.ARRAY_BUFFER, DebugLineVbo);
        Gl.bufferData(Gl.ARRAY_BUFFER, Dbg.LineVertices, Gl.DYNAMIC_DRAW);
        Session.DebugLineCount = Dbg.LineVertexCount;

        Gl.bindBuffer(Gl.ARRAY_BUFFER, DebugSolidVbo);
        Gl.bufferData(Gl.ARRAY_BUFFER, Dbg.SolidVertices, Gl.DYNAMIC_DRAW);
        Session.DebugSolidCount = Dbg.SolidVertexCount;
    }

    function RefreshInspectorReadouts()
    {
        const Primary = Session.CompiledClusters[0];
        Session.MetricsNaive = MeasureClusterGridError(Primary, Session.PrimaryReceivers, GridBakingMode.NaiveCenterAveraged, false);
        Session.MetricsUnopt = MeasureClusterGridError(Primary, Session.PrimaryReceivers, GridBakingMode.SpatialUnoptimized, false);
        Session.MetricsOpt   = MeasureClusterGridError(Primary, Session.PrimaryReceivers, GridBakingMode.ErrorOptimizedMip, false);

        const ActiveMetrics = Session.BakingMode === GridBakingMode.NaiveCenterAveraged
            ? Session.MetricsNaive
            : (Session.BakingMode === GridBakingMode.SpatialUnoptimized ? Session.MetricsUnopt : Session.MetricsOpt);

        document.getElementById("tagNaiveErr").textContent = Session.MetricsNaive.RelRmsePercent.toFixed(1) + "% err";
        document.getElementById("tagUnoptErr").textContent = Session.MetricsUnopt.RelRmsePercent.toFixed(1) + "% err";
        document.getElementById("tagOptErr").textContent   = Session.MetricsOpt.RelRmsePercent.toFixed(1) + "% err";

        document.getElementById("metricRelRmse").textContent = ActiveMetrics.RelRmsePercent.toFixed(2) + "%";
        document.getElementById("metricPsnr").textContent    = ActiveMetrics.PsnrDb.toFixed(1) + " dB";
        document.getElementById("metricMaxErr").textContent  = ActiveMetrics.MaxErr.toFixed(3);

        document.getElementById("rowNaiveRmse").textContent = Session.MetricsNaive.RelRmsePercent.toFixed(1) + "%";
        document.getElementById("rowNaivePsnr").textContent = Session.MetricsNaive.PsnrDb.toFixed(1) + " dB";
        document.getElementById("rowUnoptRmse").textContent = Session.MetricsUnopt.RelRmsePercent.toFixed(1) + "%";
        document.getElementById("rowUnoptPsnr").textContent = Session.MetricsUnopt.PsnrDb.toFixed(1) + " dB";
        document.getElementById("rowUnoptTaps").textContent = String(Primary.ActiveCellsLod0.length);
        document.getElementById("rowOptRmse").textContent   = Session.MetricsOpt.RelRmsePercent.toFixed(1) + "%";
        document.getElementById("rowOptPsnr").textContent   = Session.MetricsOpt.PsnrDb.toFixed(1) + " dB";
        document.getElementById("rowOptTaps").textContent   = String(Primary.ActiveCellsLod0.length);

        document.getElementById("optStepCounter").textContent = "Step " + Primary.OptimizationSteps;

        const TotalRaw = Session.CompiledClusters.reduce((Acc, C) => Acc + C.Cluster.Sources.length, 0);
        const ActiveVoxels = Primary.ActiveCellsLod0.length;
        document.getElementById("activeLookupsLabel").textContent =
            Session.BakingMode === GridBakingMode.NaiveCenterAveraged
                ? "1 center origin (merged!)"
                : `${ActiveVoxels} / 128 cells active`;

        const NoteEl = document.getElementById("modeExplanationNote");
        if (Session.BakingMode === GridBakingMode.NaiveCenterAveraged)
        {
            NoteEl.textContent = "🔴 BROKEN MERGE: Averages all lights into BoxCenter! Merges left/right headlamps into 1 center beam and causes huge near-field error (" + Session.MetricsNaive.RelRmsePercent.toFixed(1) + "% RelRMSE).";
        }
        else if (Session.BakingMode === GridBakingMode.SpatialUnoptimized)
        {
            NoteEl.textContent = "🚩 NON-AVERAGED RAW 8×4×4: Keeps left/right spatial voxels separate (center cells stay empty!), but quantized voxel centers & raw filter blur leave " + Session.MetricsUnopt.RelRmsePercent.toFixed(1) + "% RelRMSE.";
        }
        else
        {
            NoteEl.textContent = "🟢 RESIDUAL-ERROR OPTIMIZED: Sub-voxel optical anchors + aperture radii + deconvolved 16×16 octahedral tiles slash error to " + Session.MetricsOpt.RelRmsePercent.toFixed(2) + "% (" + Session.MetricsOpt.PsnrDb.toFixed(1) + " dB PSNR)!";
        }

        document.getElementById("statusBannerText").textContent =
            `${Primary.Cluster.Label} · ${TotalRaw} raw lights → ${Session.CompiledClusters.length} Baked Grid(s) · RelRMSE ${ActiveMetrics.RelRmsePercent.toFixed(2)}%`;

        RenderSpatialPlanCanvas();
        RenderSelectedOctahedralTileCanvas();
        RenderConvergencePlotCanvas();
        UpdatePaneCaptionTags();
    }

    //========================================================================================================================================
    //                                             2D DIAGNOSTIC INSPECTOR CANVASES
    //========================================================================================================================================

    function RenderSpatialPlanCanvas()
    {
        const Canvas = document.getElementById("spatialPlanCanvas");
        const Ctx = Canvas.getContext("2d");
        const W = Canvas.width, H = Canvas.height;
        Ctx.clearRect(0, 0, W, H);
        Ctx.fillStyle = "#080b12";
        Ctx.fillRect(0, 0, W, H);

        const Primary = Session.CompiledClusters[0];
        if (!Primary) return;

        const PadX = 10, PadY = 10;
        const GridW = W - PadX * 2;
        const GridH = H - PadY * 2;
        const CellW = GridW / GridDimX;
        const CellH = GridH / GridDimZ;

        // Collapse Y occupancy into top-down (Ix, Iz) view
        for (let Iz = 0; Iz < GridDimZ; ++Iz)
        {
            for (let Ix = 0; Ix < GridDimX; ++Ix)
            {
                const CellPx = PadX + Ix * CellW;
                const CellPy = PadY + (GridDimZ - 1 - Iz) * CellH;

                let MatchSlot = -1;
                for (let CIdx = 0; CIdx < Primary.ActiveCellsLod0.length; ++CIdx)
                {
                    const G = Primary.ActiveCellsLod0[CIdx].GridIndex;
                    if (G[0] === Ix && G[2] === Iz) { MatchSlot = CIdx; break; }
                }

                if (Session.BakingMode === GridBakingMode.NaiveCenterAveraged)
                {
                    Ctx.fillStyle = "rgba(255, 77, 94, 0.06)";
                }
                else if (MatchSlot >= 0)
                {
                    Ctx.fillStyle = MatchSlot === Session.SelectedCellSlot
                        ? "rgba(36, 229, 138, 0.34)"
                        : "rgba(54, 185, 255, 0.20)";
                }
                else
                {
                    Ctx.fillStyle = "rgba(255, 255, 255, 0.015)";
                }
                Ctx.fillRect(CellPx + 0.5, CellPy + 0.5, CellW - 1, CellH - 1);

                Ctx.strokeStyle = MatchSlot === Session.SelectedCellSlot
                    ? "#24e58a"
                    : "rgba(130, 165, 220, 0.22)";
                Ctx.lineWidth = MatchSlot === Session.SelectedCellSlot ? 1.5 : 1.0;
                Ctx.strokeRect(CellPx + 0.5, CellPy + 0.5, CellW - 1, CellH - 1);

                if (MatchSlot < 0 && Iz === 2 && Ix >= 2 && Ix <= 5 && Session.PresetId === "dual-headlamps")
                {
                    Ctx.fillStyle = "rgba(155, 176, 204, 0.36)";
                    Ctx.font = "9px DM Sans, sans-serif";
                    Ctx.textAlign = "center";
                    Ctx.fillText("0", CellPx + CellW * 0.5, CellPy + CellH * 0.58);
                }
            }
        }

        // Project world (X, Z) inside BoundsMin..BoundsMax onto canvas pixels
        const BMin = Primary.Cluster.BoundsMin;
        const BMax = Primary.Cluster.BoundsMax;
        const ProjectWorldToCanvas = (Wx, Wz) => [
            PadX + ((Wx - BMin[0]) / (BMax[0] - BMin[0])) * GridW,
            PadY + (1.0 - (Wz - BMin[2]) / (BMax[2] - BMin[2])) * GridH
        ];

        // Plot raw physical sources as amber rings
        for (const S of Primary.Cluster.Sources)
        {
            const [Px, Py] = ProjectWorldToCanvas(S.Position[0], S.Position[2]);
            Ctx.strokeStyle = "#ffb838";
            Ctx.lineWidth = 1.4;
            Ctx.beginPath();
            Ctx.arc(Px, Py, 3.8, 0, Math.PI * 2);
            Ctx.stroke();
        }

        if (Session.BakingMode === GridBakingMode.NaiveCenterAveraged)
        {
            const [Cx, Cy] = ProjectWorldToCanvas(Primary.BoxCenter[0], Primary.BoxCenter[2]);
            Ctx.strokeStyle = "rgba(255, 77, 94, 0.7)";
            for (const S of Primary.Cluster.Sources)
            {
                const [Px, Py] = ProjectWorldToCanvas(S.Position[0], S.Position[2]);
                Ctx.beginPath(); Ctx.moveTo(Px, Py); Ctx.lineTo(Cx, Cy); Ctx.stroke();
            }
            Ctx.fillStyle = "#ff4d5e";
            Ctx.beginPath(); Ctx.arc(Cx, Cy, 5.0, 0, Math.PI * 2); Ctx.fill();
        }
        else
        {
            const UseOpt = Session.BakingMode === GridBakingMode.ErrorOptimizedMip;
            for (let CIdx = 0; CIdx < Primary.ActiveCellsLod0.length; ++CIdx)
            {
                const Cell = Primary.ActiveCellsLod0[CIdx];
                const [Vx, Vy] = ProjectWorldToCanvas(Cell.VoxelCenter[0], Cell.VoxelCenter[2]);
                const AxWorld = UseOpt ? Cell.VoxelCenter[0] + Cell.SubOffset[0] : Cell.VoxelCenter[0];
                const AzWorld = UseOpt ? Cell.VoxelCenter[2] + Cell.SubOffset[2] : Cell.VoxelCenter[2];
                const [Ax, Ay] = ProjectWorldToCanvas(AxWorld, AzWorld);

                if (UseOpt)
                {
                    Ctx.strokeStyle = "#ffe552";
                    Ctx.lineWidth = 1.2;
                    Ctx.beginPath(); Ctx.moveTo(Vx, Vy); Ctx.lineTo(Ax, Ay); Ctx.stroke();
                }
                Ctx.fillStyle = UseOpt ? "#24e58a" : "#36b9ff";
                Ctx.beginPath(); Ctx.arc(Ax, Ay, 2.8, 0, Math.PI * 2); Ctx.fill();
            }
        }
    }

    function RenderSelectedOctahedralTileCanvas()
    {
        const Canvas = document.getElementById("octTileCanvas");
        const Ctx = Canvas.getContext("2d");
        const W = Canvas.width, H = Canvas.height;
        Ctx.fillStyle = "#080b12";
        Ctx.fillRect(0, 0, W, H);

        const Primary = Session.CompiledClusters[0];
        if (!Primary) return;

        let TileFloats = Primary.NaiveTile16;
        let LabelText = "Naive Center 16×16";
        if (Session.BakingMode !== GridBakingMode.NaiveCenterAveraged && Primary.ActiveCellsLod0.length > 0)
        {
            const Slot = Math.min(Session.SelectedCellSlot, Primary.ActiveCellsLod0.length - 1);
            const Cell = Primary.ActiveCellsLod0[Slot];
            TileFloats = Session.BakingMode === GridBakingMode.ErrorOptimizedMip ? Cell.OptTile16 : Cell.RawTile16;
            LabelText = `Cell [${Cell.GridIndex.join(",")}]`;
        }
        document.getElementById("selectedCellReadout").textContent = LabelText;

        const Res = TileResLod0;
        let MaxVal = 1e-4;
        for (let I = 0; I < Res * Res * 3; ++I)
        {
            if (TileFloats[I] > MaxVal) MaxVal = TileFloats[I];
        }

        const Size = Math.min(W, H) - 12;
        const Ox = Math.floor((W - Size) * 0.5);
        const Oy = Math.floor((H - Size) * 0.5);
        const Pix = Size / Res;

        for (let Y = 0; Y < Res; ++Y)
        {
            for (let X = 0; X < Res; ++X)
            {
                const Idx = (Y * Res + X) * 3;
                const R = Math.pow(Math.min(1.0, TileFloats[Idx + 0] / MaxVal), 0.45);
                const G = Math.pow(Math.min(1.0, TileFloats[Idx + 1] / MaxVal), 0.45);
                const B = Math.pow(Math.min(1.0, TileFloats[Idx + 2] / MaxVal), 0.45);
                Ctx.fillStyle = `rgb(${Math.round(R * 255)}, ${Math.round(G * 255)}, ${Math.round(B * 255)})`;
                Ctx.fillRect(Ox + X * Pix, Oy + Y * Pix, Math.ceil(Pix), Math.ceil(Pix));
            }
        }

        // Octahedral diamond fold guide
        Ctx.strokeStyle = "rgba(180, 210, 255, 0.28)";
        Ctx.lineWidth = 1.0;
        Ctx.strokeRect(Ox + 0.5, Oy + 0.5, Size - 1, Size - 1);
        Ctx.beginPath();
        Ctx.moveTo(Ox + Size * 0.5, Oy);
        Ctx.lineTo(Ox + Size, Oy + Size * 0.5);
        Ctx.lineTo(Ox + Size * 0.5, Oy + Size);
        Ctx.lineTo(Ox, Oy + Size * 0.5);
        Ctx.closePath();
        Ctx.stroke();
    }

    function RenderConvergencePlotCanvas()
    {
        const Canvas = document.getElementById("convergencePlotCanvas");
        const Ctx = Canvas.getContext("2d");
        const W = Canvas.width, H = Canvas.height;
        Ctx.fillStyle = "#080b12";
        Ctx.fillRect(0, 0, W, H);

        const Primary = Session.CompiledClusters[0];
        if (!Primary || !Session.MetricsNaive || !Session.MetricsUnopt) return;

        const PadL = 34, PadR = 10, PadT = 10, PadB = 16;
        const PlotW = W - PadL - PadR;
        const PlotH = H - PadT - PadB;

        const MaxY = Math.max(45.0, Session.MetricsUnopt.RelRmsePercent * 1.25);
        const PlotX = (Step) => PadL + (Math.min(Step, 25) / 25.0) * PlotW;
        const PlotY = (Pct)  => PadT + (1.0 - Math.min(Pct, MaxY) / MaxY) * PlotH;

        // Horizontal grid & axis labels
        Ctx.strokeStyle = "rgba(140, 170, 220, 0.14)";
        Ctx.lineWidth = 1.0;
        for (const Frac of [0.0, 0.5, 1.0])
        {
            const Y = PadT + Frac * PlotH;
            Ctx.beginPath(); Ctx.moveTo(PadL, Y); Ctx.lineTo(W - PadR, Y); Ctx.stroke();
        }

        // Dashed Amber reference line for Unoptimized 8x4x4 Spatial Grid
        const UnoptY = PlotY(Session.MetricsUnopt.RelRmsePercent);
        Ctx.setLineDash([4, 3]);
        Ctx.strokeStyle = "rgba(255, 184, 56, 0.65)";
        Ctx.beginPath(); Ctx.moveTo(PadL, UnoptY); Ctx.lineTo(W - PadR, UnoptY); Ctx.stroke();
        Ctx.setLineDash([]);

        // Emerald optimization trajectory from Step 0 -> Step N
        const Points = [{ Step: 0, RelRmsePercent: Session.MetricsUnopt.RelRmsePercent }, ...Primary.ErrorHistory];
        Ctx.strokeStyle = "#24e58a";
        Ctx.lineWidth = 2.0;
        Ctx.beginPath();
        for (let I = 0; I < Points.length; ++I)
        {
            const X = PlotX(Points[I].Step);
            const Y = PlotY(Points[I].RelRmsePercent);
            if (I === 0) Ctx.moveTo(X, Y);
            else Ctx.lineTo(X, Y);
        }
        Ctx.stroke();

        const LastPt = Points[Points.length - 1];
        Ctx.fillStyle = "#24e58a";
        Ctx.beginPath();
        Ctx.arc(PlotX(LastPt.Step), PlotY(LastPt.RelRmsePercent), 3.2, 0, Math.PI * 2);
        Ctx.fill();

        Ctx.fillStyle = "#8ea2c4";
        Ctx.font = "10px DM Sans, sans-serif";
        Ctx.textAlign = "right";
        Ctx.fillText(MaxY.toFixed(0) + "%", PadL - 4, PadT + 8);
        Ctx.fillText("0%", PadL - 4, PadT + PlotH + 3);
        Ctx.textAlign = "left";
        Ctx.fillText("Step 0", PadL, H - 3);
        Ctx.textAlign = "right";
        Ctx.fillStyle = "#ff7b88";
        Ctx.fillText(`Naive Merged: ${Session.MetricsNaive.RelRmsePercent.toFixed(0)}% (off-scale!)`, W - PadR, PadT + 9);
        Ctx.fillStyle = "#8ea2c4";
        Ctx.fillText("Step 25", W - PadR, H - 3);
    }

    function UpdatePaneCaptionTags()
    {
        const Row = document.getElementById("paneCaptionsRow");
        const DivL = document.getElementById("dividerLeft");
        const DivR = document.getElementById("dividerRight");

        const ModeTitle = Session.BakingMode === GridBakingMode.NaiveCenterAveraged
            ? "🔴 Naive Center-Averaged Grid (1 Origin)"
            : (Session.BakingMode === GridBakingMode.SpatialUnoptimized
                ? "🚩 Unoptimized 8×4×4 Spatial Grid"
                : "🟢 Residual-Error Optimized 8×4×4 Mip-Grid");

        const ActiveRmse = Session.BakingMode === GridBakingMode.NaiveCenterAveraged
            ? Session.MetricsNaive.RelRmsePercent
            : (Session.BakingMode === GridBakingMode.SpatialUnoptimized
                ? Session.MetricsUnopt.RelRmsePercent
                : Session.MetricsOpt.RelRmsePercent);

        if (Session.SplitLayout === "tri-split")
        {
            DivL.style.display = "block"; DivL.style.left = "36%";
            DivR.style.display = "block"; DivR.style.left = "72%";
            Row.innerHTML = `
                <div class="pane-tag" style="width:calc(36% - 8px);">
                    <div class="pane-tag-heading">① EXACT GROUND TRUTH (${Session.RawSourceCount} Unmerged Lights)</div>
                    <div class="pane-tag-sub">Direct per-source physical evaluation + Raw Light Cones</div>
                </div>
                <div class="pane-tag" style="width:calc(36% - 8px);">
                    <div class="pane-tag-heading">② ${ModeTitle}</div>
                    <div class="pane-tag-sub">3D Spatial Voxels + Sub-Voxel Anchors + Nanite Distance-LOD</div>
                </div>
                <div class="pane-tag" style="width:calc(28% - 8px);">
                    <div class="pane-tag-heading">③ LIVE ERROR |GT − GRID| × ${Session.ErrorGain.toFixed(1)}×</div>
                    <div class="pane-tag-sub">RelRMSE = ${ActiveRmse.toFixed(2)}% (Dark Indigo = Zero Error)</div>
                </div>
            `;
        }
        else if (Session.SplitLayout === "dual-split")
        {
            DivL.style.display = "block"; DivL.style.left = "50%";
            DivR.style.display = "none";
            Row.innerHTML = `
                <div class="pane-tag" style="width:calc(50% - 6px);">
                    <div class="pane-tag-heading">① EXACT GROUND TRUTH (${Session.RawSourceCount} Unmerged Lights)</div>
                    <div class="pane-tag-sub">True individual lamp positions &amp; aiming cones</div>
                </div>
                <div class="pane-tag" style="width:calc(50% - 6px);">
                    <div class="pane-tag-heading">② ${ModeTitle}</div>
                    <div class="pane-tag-sub">RelRMSE = ${ActiveRmse.toFixed(2)}% · PSNR = ${(Session.BakingMode === 2 ? Session.MetricsOpt.PsnrDb : Session.MetricsUnopt.PsnrDb).toFixed(1)} dB</div>
                </div>
            `;
        }
        else
        {
            DivL.style.display = "none";
            DivR.style.display = "none";
            const SingleDesc = Session.SplitLayout === "gt-only"
                ? `EXACT GROUND TRUTH (${Session.RawSourceCount} Unmerged Physical Lights)`
                : (Session.SplitLayout === "lod-diag"
                    ? "NANITE LIGHT DISTANCE-LOD DIAGNOSTIC (Emerald LOD 0 → Cyan LOD 1 → Amber LOD 2 [1-Sample] → Violet LOD 3)"
                    : `${ModeTitle} · RelRMSE = ${ActiveRmse.toFixed(2)}%`);
            Row.innerHTML = `
                <div class="pane-tag">
                    <div class="pane-tag-heading">${SingleDesc}</div>
                    <div class="pane-tag-sub">Drag to orbit · Right-drag to pan · Scroll to zoom across Nanite LOD zones</div>
                </div>
            `;
        }
    }

    //========================================================================================================================================
    //                                                    PRESET & CAMERA LOADING
    //========================================================================================================================================

    function ApplyPresetCameraDefault(PresetId)
    {
        if (PresetId === "dual-headlamps")
        {
            Session.Camera.Yaw = 0.56;
            Session.Camera.Pitch = 0.34;
            Session.Camera.Distance = 7.4;
            Session.Camera.Target = [0.0, 0.55, 3.1];
        }
        else if (PresetId === "five-emissives")
        {
            Session.Camera.Yaw = 0.72;
            Session.Camera.Pitch = 0.42;
            Session.Camera.Distance = 8.4;
            Session.Camera.Target = [0.0, 0.90, 0.0];
        }
        else
        {
            Session.Camera.Yaw = 0.42;
            Session.Camera.Pitch = 0.32;
            Session.Camera.Distance = 14.5;
            Session.Camera.Target = [0.0, 0.85, -7.5];
        }
    }

    function LoadPresetScene(PresetId, RunInitialOptimization = true)
    {
        Session.PresetId = PresetId;
        Session.SelectedCellSlot = 0;
        Session.AnimatedOptStepsRemaining = 0;

        // 1. Upload 3D Receiver Surfaces
        const Surf = BuildPresetSceneSurfaces(PresetId, VehicleParts);
        Gl.bindVertexArray(SurfaceVao);
        Gl.bindBuffer(Gl.ARRAY_BUFFER, SurfaceVbo);
        Gl.bufferData(Gl.ARRAY_BUFFER, Surf.InterleavedVertices, Gl.STATIC_DRAW);
        Gl.bindBuffer(Gl.ELEMENT_ARRAY_BUFFER, SurfaceEbo);
        Gl.bufferData(Gl.ELEMENT_ARRAY_BUFFER, Surf.Indices, Gl.STATIC_DRAW);
        Gl.bindVertexArray(null);
        Session.SurfaceIndexCount = Surf.Indices.length;

        // 2. Compile Multi-Resolution 8x4x4 Spatial Light Grids
        const RawClusters = ConstructSceneLightClusters(PresetId);
        Session.CompiledClusters = RawClusters.map((C) => BakeInitialLightClusterGrid(C));
        Session.PrimaryReceivers = GenerateCalibrationReceivers(Session.CompiledClusters[0].Cluster);

        if (RunInitialOptimization)
        {
            StepResidualErrorOptimization(Session.CompiledClusters[0], Session.PrimaryReceivers, 25);
            if (PresetId === "nanite-hall" && Session.CompiledClusters.length > 3)
            {
                const PanelReceivers = GenerateCalibrationReceivers(Session.CompiledClusters[3].Cluster);
                StepResidualErrorOptimization(Session.CompiledClusters[3], PanelReceivers, 20);
                PropagateArchetypeOptimizationToHall();
            }
        }

        ApplyPresetCameraDefault(PresetId);
        SyncGpuGridWithDebug();
        RefreshInspectorReadouts();
    }

    //========================================================================================================================================
    //                                                    SUB-VIEWPORT RENDERING
    //========================================================================================================================================

    function RenderSubPane(VpX, VpY, VpW, VpH, RenderModeInt, RenderDebugOverlay)
    {
        if (VpW <= 2 || VpH <= 2) return;
        Gl.viewport(VpX, VpY, VpW, VpH);
        Gl.scissor(VpX, VpY, VpW, VpH);
        Gl.clearColor(0.025, 0.032, 0.052, 1.0);
        Gl.clear(Gl.COLOR_BUFFER_BIT | Gl.DEPTH_BUFFER_BIT);

        const Eye = ComputeCameraEye();
        const ProjXf = BuildPerspectiveXf((46.0 * Math.PI) / 180.0, VpW / VpH, 0.12, 120.0);
        const ViewXf = BuildLookAtXf(Eye, Session.Camera.Target, [0.0, 1.0, 0.0]);
        const ViewProjXf = MultiplyXf4(ProjXf, ViewXf);

        Gl.enable(Gl.DEPTH_TEST);
        Gl.depthFunc(Gl.LEQUAL);
        Gl.disable(Gl.BLEND);

        Gl.useProgram(SurfaceProg);
        Gl.uniformMatrix4fv(SurfUniforms.ViewProjXf, false, ViewProjXf);
        Gl.uniform3f(SurfUniforms.CameraPos, Eye[0], Eye[1], Eye[2]);
        Gl.uniform1i(SurfUniforms.RenderViewMode, RenderModeInt);
        Gl.uniform1i(SurfUniforms.BakingMode, Session.BakingMode);
        Gl.uniform1i(SurfUniforms.EnableDistLod, Session.EnableDistLod ? 1 : 0);
        Gl.uniform1f(SurfUniforms.LodBias, Session.LodBias);
        Gl.uniform1f(SurfUniforms.Exposure, Session.Exposure);
        Gl.uniform1f(SurfUniforms.ErrorGain, Session.ErrorGain);
        Gl.uniform1i(SurfUniforms.RawSourceCount, Session.RawSourceCount);
        Gl.uniform1i(SurfUniforms.ClusterCount, Session.ClusterCount);

        Gl.activeTexture(Gl.TEXTURE0); Gl.bindTexture(Gl.TEXTURE_2D, RawSourcesTex);  Gl.uniform1i(SurfUniforms.RawSourcesTex, 0);
        Gl.activeTexture(Gl.TEXTURE1); Gl.bindTexture(Gl.TEXTURE_2D, ClusterMetaTex); Gl.uniform1i(SurfUniforms.ClusterMetaTex, 1);
        Gl.activeTexture(Gl.TEXTURE2); Gl.bindTexture(Gl.TEXTURE_2D, CellAnchorTex);  Gl.uniform1i(SurfUniforms.CellAnchorTex, 2);
        Gl.activeTexture(Gl.TEXTURE3); Gl.bindTexture(Gl.TEXTURE_2D, OctSheetTex);    Gl.uniform1i(SurfUniforms.OctSheetTex, 3);

        Gl.bindVertexArray(SurfaceVao);
        Gl.drawElements(Gl.TRIANGLES, Session.SurfaceIndexCount, Gl.UNSIGNED_INT, 0);

        if (RenderDebugOverlay && (Session.DebugLineCount > 0 || Session.DebugSolidCount > 0))
        {
            Gl.useProgram(DebugProg);
            Gl.uniformMatrix4fv(DbgUniforms.ViewProjXf, false, ViewProjXf);
            Gl.enable(Gl.BLEND);
            Gl.blendFunc(Gl.SRC_ALPHA, Gl.ONE_MINUS_SRC_ALPHA);

            // Depth-tested crisp foreground layer
            Gl.uniform1f(DbgUniforms.AlphaScale, 1.0);
            if (Session.DebugSolidCount > 0)
            {
                Gl.bindVertexArray(DebugSolidVao);
                Gl.drawArrays(Gl.TRIANGLES, 0, Session.DebugSolidCount);
            }
            if (Session.DebugLineCount > 0)
            {
                Gl.bindVertexArray(DebugLineVao);
                Gl.drawArrays(Gl.LINES, 0, Session.DebugLineCount);
            }

            // Faint X-ray layer behind car bodywork so true lamp positions inside housings stay visible!
            Gl.depthFunc(Gl.GREATER);
            Gl.depthMask(false);
            Gl.uniform1f(DbgUniforms.AlphaScale, 0.24);
            if (Session.DebugLineCount > 0)
            {
                Gl.bindVertexArray(DebugLineVao);
                Gl.drawArrays(Gl.LINES, 0, Session.DebugLineCount);
            }
            Gl.depthMask(true);
            Gl.depthFunc(Gl.LEQUAL);
            Gl.disable(Gl.BLEND);
        }

        Gl.bindVertexArray(null);
    }

    function RenderWorkbenchTick()
    {
        // Step live animated Residual-Error Optimization if active!
        if (Session.AnimatedOptStepsRemaining > 0)
        {
            StepResidualErrorOptimization(Session.CompiledClusters[0], Session.PrimaryReceivers, 1);
            if (Session.PresetId === "nanite-hall" && Session.CompiledClusters.length > 3)
            {
                PropagateArchetypeOptimizationToHall();
            }
            --Session.AnimatedOptStepsRemaining;
            SyncGpuGridWithDebug();
            RefreshInspectorReadouts();
        }

        const Dpr = Math.min(window.devicePixelRatio || 1, 2);
        const CssW = GlCanvas.clientWidth;
        const CssH = GlCanvas.clientHeight;
        const TargetW = Math.max(1, Math.floor(CssW * Dpr));
        const TargetH = Math.max(1, Math.floor(CssH * Dpr));
        if (GlCanvas.width !== TargetW || GlCanvas.height !== TargetH)
        {
            GlCanvas.width = TargetW;
            GlCanvas.height = TargetH;
        }

        // Update live camera distance & active Nanite LOD readout for Cluster 0
        if (Session.CompiledClusters.length > 0)
        {
            const Eye = ComputeCameraEye();
            const LodStat = ComputeClusterNaniteLod(Session.CompiledClusters[0], Eye, Session.LodBias);
            const TierTxt = Session.EnableDistLod ? `LOD ${LodStat.LodContinuous.toFixed(2)}` : "LOD 0 (Locked)";
            document.getElementById("camDistReadout").textContent = `d = ${LodStat.Distance.toFixed(1)} m · ${TierTxt}`;
        }

        Gl.enable(Gl.SCISSOR_TEST);
        if (Session.SplitLayout === "tri-split")
        {
            const W0 = Math.floor(TargetW * 0.36);
            const W1 = Math.floor(TargetW * 0.36);
            const W2 = TargetW - W0 - W1;
            RenderSubPane(0,       0, W0, TargetH, 0, Session.DebugToggles.ShowRawLights);
            RenderSubPane(W0,      0, W1, TargetH, 1, true);
            RenderSubPane(W0 + W1, 0, W2, TargetH, 2, false);
        }
        else if (Session.SplitLayout === "dual-split")
        {
            const HalfW = Math.floor(TargetW * 0.5);
            RenderSubPane(0,     0, HalfW,           TargetH, 0, Session.DebugToggles.ShowRawLights);
            RenderSubPane(HalfW, 0, TargetW - HalfW, TargetH, 1, true);
        }
        else if (Session.SplitLayout === "gt-only")
        {
            RenderSubPane(0, 0, TargetW, TargetH, 0, true);
        }
        else if (Session.SplitLayout === "lod-diag")
        {
            RenderSubPane(0, 0, TargetW, TargetH, 3, true);
        }
        else
        {
            RenderSubPane(0, 0, TargetW, TargetH, 1, true);
        }
        Gl.disable(Gl.SCISSOR_TEST);

        requestAnimationFrame(RenderWorkbenchTick);
    }

    //========================================================================================================================================
    //                                                    INTERACTIVE UI CONTROLS
    //========================================================================================================================================

    function SetExclusiveButtonGroup(ButtonIds, ActiveId, SelectedClass)
    {
        for (const Id of ButtonIds)
        {
            const Btn = document.getElementById(Id);
            Btn.classList.remove("selected-cyan", "selected-emerald", "selected-amber", "selected-crimson");
            if (Id === ActiveId) Btn.classList.add(SelectedClass);
        }
    }

    // Preset Buttons
    const PresetBtnIds = ["btnPresetHeadlamps", "btnPresetFivePanels", "btnPresetNaniteHall"];
    document.getElementById("btnPresetHeadlamps").addEventListener("click", () => {
        SetExclusiveButtonGroup(PresetBtnIds, "btnPresetHeadlamps", "selected-cyan");
        LoadPresetScene("dual-headlamps", true);
    });
    document.getElementById("btnPresetFivePanels").addEventListener("click", () => {
        SetExclusiveButtonGroup(PresetBtnIds, "btnPresetFivePanels", "selected-cyan");
        LoadPresetScene("five-emissives", true);
    });
    document.getElementById("btnPresetNaniteHall").addEventListener("click", () => {
        SetExclusiveButtonGroup(PresetBtnIds, "btnPresetNaniteHall", "selected-cyan");
        LoadPresetScene("nanite-hall", true);
    });

    // Split View Buttons
    const ViewBtnIds = ["btnViewTriSplit", "btnViewDualSplit", "btnViewGridOnly", "btnViewGtOnly", "btnViewLodDiag"];
    const BindViewButton = (BtnId, LayoutKey) => {
        document.getElementById(BtnId).addEventListener("click", () => {
            Session.SplitLayout = LayoutKey;
            SetExclusiveButtonGroup(ViewBtnIds, BtnId, "selected-emerald");
            UpdatePaneCaptionTags();
        });
    };
    BindViewButton("btnViewTriSplit",  "tri-split");
    BindViewButton("btnViewDualSplit", "dual-split");
    BindViewButton("btnViewGridOnly",  "grid-only");
    BindViewButton("btnViewGtOnly",    "gt-only");
    BindViewButton("btnViewLodDiag",   "lod-diag");

    // Baking Mode Buttons
    const ModeBtnIds = ["btnModeNaive", "btnModeUnopt", "btnModeOpt"];
    document.getElementById("btnModeNaive").addEventListener("click", () => {
        Session.BakingMode = GridBakingMode.NaiveCenterAveraged;
        SetExclusiveButtonGroup(ModeBtnIds, "btnModeNaive", "selected-crimson");
        SyncGpuGridWithDebug();
        RefreshInspectorReadouts();
    });
    document.getElementById("btnModeUnopt").addEventListener("click", () => {
        Session.BakingMode = GridBakingMode.SpatialUnoptimized;
        SetExclusiveButtonGroup(ModeBtnIds, "btnModeUnopt", "selected-amber");
        SyncGpuGridWithDebug();
        RefreshInspectorReadouts();
    });
    document.getElementById("btnModeOpt").addEventListener("click", () => {
        Session.BakingMode = GridBakingMode.ErrorOptimizedMip;
        SetExclusiveButtonGroup(ModeBtnIds, "btnModeOpt", "selected-emerald");
        SyncGpuGridWithDebug();
        RefreshInspectorReadouts();
    });

    // Residual-Error Optimizer Controls
    document.getElementById("btnResetOpt").addEventListener("click", () => {
        Session.AnimatedOptStepsRemaining = 0;
        const RawClusters = ConstructSceneLightClusters(Session.PresetId);
        Session.CompiledClusters = RawClusters.map((C) => BakeInitialLightClusterGrid(C));
        Session.BakingMode = GridBakingMode.ErrorOptimizedMip;
        SetExclusiveButtonGroup(ModeBtnIds, "btnModeOpt", "selected-emerald");
        SyncGpuGridWithDebug();
        RefreshInspectorReadouts();
    });

    document.getElementById("btnStepOnce").addEventListener("click", () => {
        Session.AnimatedOptStepsRemaining = 0;
        Session.BakingMode = GridBakingMode.ErrorOptimizedMip;
        SetExclusiveButtonGroup(ModeBtnIds, "btnModeOpt", "selected-emerald");
        StepResidualErrorOptimization(Session.CompiledClusters[0], Session.PrimaryReceivers, 1);
        if (Session.PresetId === "nanite-hall" && Session.CompiledClusters.length > 3)
        {
            PropagateArchetypeOptimizationToHall();
        }
        SyncGpuGridWithDebug();
        RefreshInspectorReadouts();
    });

    document.getElementById("btnAnimateOpt").addEventListener("click", () => {
        const RawClusters = ConstructSceneLightClusters(Session.PresetId);
        Session.CompiledClusters = RawClusters.map((C) => BakeInitialLightClusterGrid(C));
        if (Session.PresetId === "nanite-hall" && Session.CompiledClusters.length > 3)
        {
            const PanelReceivers = GenerateCalibrationReceivers(Session.CompiledClusters[3].Cluster);
            StepResidualErrorOptimization(Session.CompiledClusters[3], PanelReceivers, 20);
        }
        Session.BakingMode = GridBakingMode.ErrorOptimizedMip;
        SetExclusiveButtonGroup(ModeBtnIds, "btnModeOpt", "selected-emerald");
        Session.AnimatedOptStepsRemaining = 25;
        SyncGpuGridWithDebug();
        RefreshInspectorReadouts();
    });

    // 3D Debug & Nanite Distance-LOD Toggles
    const BindToggle = (BtnId, InitialOn, LabelText, ColorClass, ApplyFn) => {
        let Active = InitialOn;
        const Btn = document.getElementById(BtnId);
        Btn.addEventListener("click", () => {
            Active = !Active;
            Btn.textContent = (Active ? "✔️ " : "🏳️ ") + LabelText;
            Btn.classList.toggle(ColorClass, Active);
            ApplyFn(Active);
        });
    };

    BindToggle("btnToggle3dGrid", true, "3D 8×4×4 Grid", "selected-cyan", (On) => {
        Session.DebugToggles.Show3dGrid = On;
        SyncGpuGridWithDebug();
    });
    BindToggle("btnToggleRawLights", true, "Raw Light Cones", "selected-amber", (On) => {
        Session.DebugToggles.ShowRawLights = On;
        SyncGpuGridWithDebug();
    });
    BindToggle("btnToggleLodRings", true, "3D LOD Rings", "selected-cyan", (On) => {
        Session.DebugToggles.ShowLodRings = On;
        SyncGpuGridWithDebug();
    });
    BindToggle("btnToggleDistLod", true, "Nanite Dist-LOD", "selected-emerald", (On) => {
        Session.EnableDistLod = On;
    });

    // Sliders
    document.getElementById("sliderErrorGain").addEventListener("input", (Ev) => {
        Session.ErrorGain = parseFloat(Ev.target.value);
        document.getElementById("readoutErrorGain").textContent = Session.ErrorGain.toFixed(1) + "×";
        UpdatePaneCaptionTags();
    });
    document.getElementById("sliderLodBias").addEventListener("input", (Ev) => {
        Session.LodBias = parseFloat(Ev.target.value);
        const Sign = Session.LodBias >= 0 ? "+" : "";
        document.getElementById("readoutLodBias").textContent = Sign + Session.LodBias.toFixed(2);
    });
    document.getElementById("sliderExposure").addEventListener("input", (Ev) => {
        Session.Exposure = parseFloat(Ev.target.value);
        document.getElementById("readoutExposure").textContent = Session.Exposure.toFixed(2);
    });

    // Camera Bookmarks
    document.getElementById("btnCamHero").addEventListener("click", () => ApplyPresetCameraDefault(Session.PresetId));
    document.getElementById("btnCamBumper").addEventListener("click", () => {
        const C = Session.CompiledClusters[0].BoxCenter;
        Session.Camera.Yaw = 0.28;
        Session.Camera.Pitch = 0.24;
        Session.Camera.Distance = 3.25;
        Session.Camera.Target = [C[0], C[1], C[2] + 0.35];
    });
    document.getElementById("btnCamTopGrid").addEventListener("click", () => {
        const C = Session.CompiledClusters[0].BoxCenter;
        Session.Camera.Yaw = 0.02;
        Session.Camera.Pitch = 1.18;
        Session.Camera.Distance = 6.8;
        Session.Camera.Target = [C[0], 0.2, C[2] + 1.2];
    });
    document.getElementById("btnCamFarHall").addEventListener("click", () => {
        Session.Camera.Yaw = 0.30;
        Session.Camera.Pitch = 0.28;
        Session.Camera.Distance = 21.0;
        Session.Camera.Target = [0.0, 0.6, Session.PresetId === "nanite-hall" ? -11.0 : 2.0];
    });

    // Clickable 2D Spatial Plan Canvas to inspect any active cell's 16x16 octahedral tile
    document.getElementById("spatialPlanCanvas").addEventListener("click", (Ev) => {
        const Canvas = Ev.currentTarget;
        const Rect = Canvas.getBoundingClientRect();
        const U = ((Ev.clientX - Rect.left) / Rect.width) * Canvas.width;
        const V = ((Ev.clientY - Rect.top) / Rect.height) * Canvas.height;
        const PadX = 10, PadY = 10;
        const Ix = Math.floor(((U - PadX) / (Canvas.width - PadX * 2)) * GridDimX);
        const Iz = GridDimZ - 1 - Math.floor(((V - PadY) / (Canvas.height - PadY * 2)) * GridDimZ);

        const Primary = Session.CompiledClusters[0];
        if (!Primary) return;
        for (let CIdx = 0; CIdx < Primary.ActiveCellsLod0.length; ++CIdx)
        {
            const G = Primary.ActiveCellsLod0[CIdx].GridIndex;
            if (G[0] === Ix && G[2] === Iz)
            {
                Session.SelectedCellSlot = CIdx;
                RenderSpatialPlanCanvas();
                RenderSelectedOctahedralTileCanvas();
                break;
            }
        }
    });

    // Mouse Orbit, Pan & Wheel Zoom on WebGL2 Canvas
    let DragMode = 0; // 0 = idle, 1 = orbit, 2 = pan
    let PrevMouseX = 0, PrevMouseY = 0;

    GlCanvas.addEventListener("contextmenu", (Ev) => Ev.preventDefault());
    GlCanvas.addEventListener("pointerdown", (Ev) => {
        GlCanvas.setPointerCapture(Ev.pointerId);
        DragMode = (Ev.button === 2 || Ev.shiftKey) ? 2 : 1;
        PrevMouseX = Ev.clientX;
        PrevMouseY = Ev.clientY;
    });
    GlCanvas.addEventListener("pointermove", (Ev) => {
        if (DragMode === 0) return;
        const Dx = Ev.clientX - PrevMouseX;
        const Dy = Ev.clientY - PrevMouseY;
        PrevMouseX = Ev.clientX;
        PrevMouseY = Ev.clientY;

        if (DragMode === 1)
        {
            Session.Camera.Yaw   -= Dx * 0.0065;
            Session.Camera.Pitch  = Math.max(0.05, Math.min(1.46, Session.Camera.Pitch + Dy * 0.0065));
        }
        else
        {
            const PanScale = Session.Camera.Distance * 0.0016;
            const CosY = Math.cos(Session.Camera.Yaw), SinY = Math.sin(Session.Camera.Yaw);
            Session.Camera.Target[0] -= CosY * Dx * PanScale;
            Session.Camera.Target[2] += SinY * Dx * PanScale;
            Session.Camera.Target[1]  = Math.max(0.0, Session.Camera.Target[1] + Dy * PanScale);
        }
    });
    const EndDrag = () => { DragMode = 0; };
    GlCanvas.addEventListener("pointerup", EndDrag);
    GlCanvas.addEventListener("pointercancel", EndDrag);

    GlCanvas.addEventListener("wheel", (Ev) => {
        Ev.preventDefault();
        const ZoomFactor = Math.exp(Ev.deltaY * 0.0011);
        Session.Camera.Distance = Math.max(1.4, Math.min(48.0, Session.Camera.Distance * ZoomFactor));
    }, { passive: false });

    // Initial Boot: Load Dual-Headlamps Isolated Rig, Run 25 Residual-Error Optimization Steps, and Start Render Loop!
    LoadPresetScene("dual-headlamps", true);
    requestAnimationFrame(RenderWorkbenchTick);
}

BootLightGridWorkbench();
