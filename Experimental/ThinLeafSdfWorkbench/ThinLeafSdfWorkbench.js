// Experimental/ThinLeafSdfWorkbench/ThinLeafSdfWorkbench.js
// 🧩 Interactive WebGPU (+ Automatic WebGL2 Fallback) Thin-Leaf Foliage SDF Shadow Workbench:
//    Demonstrates unconditional 1.2 mm botanical leaf shadows across 5 instanced leaf templates via
//    Two-Level 16^3 Canopy Bricks + 64x64 2D Signed-Sheet Extrusion + Continuous Cone-Width Optical-Depth LOD.

import {
    LeafArchetypeCount,
    LeafSdf2dRes,
    MacroVoxelRes,
    CanopyBrickRes,
    LeafHalfThicknessM,
    DefaultBarkNoiseAmp,
    DefaultBarkNoiseFreq,
    DefaultBranchGnarl,
    SdfEvaluationMode,
    CompileFiveLeafArchetypeSdfs,
    ConstructFoliagePreset,
    CompileFoliageSdfVolumes,
    BenchmarkFoliageShadowAccuracy
} from "./ThinLeafSdfCompiler.js";

import {
    SurfaceVertexBytes,
    DebugLineVertexBytes,
    BuildFoliageSceneGeometry,
    BuildDebugWireframeGeometry
} from "./FoliageGeometryBuilder.js";

import {
    FoliageSurfaceWgsl,
    DebugWireframeWgsl,
    FoliageSurfaceGlslVert,
    FoliageSurfaceGlslFrag,
    DebugWireframeGlslVert,
    DebugWireframeGlslFrag
} from "./ThinLeafWgslShaders.js";

const ModeLabels = [
    "Standard 32³ Signed Voxel SDF",
    "Dilated Two-Sided 32³ Voxel SDF",
    "Non-Instanced 32³ Opacity UDF",
    "Two-Level 2.5D Thin-Sheet SDF"
];

// Camera bookmarks tailored to each preset & view angle
const CameraBookmarks = {
    "bough-closeup": {
        Shadow: { Target: [-0.25, 0.25, -0.55], Yaw: 0.42,  Pitch: 0.62, Dist: 2.65 },
        Canopy: { Target: [-0.10, 0.92, -0.25], Yaw: 0.48,  Pitch: 0.34, Dist: 3.55 },
        Wall:   { Target: [-0.30, 1.05, -1.65], Yaw: 0.22,  Pitch: 0.22, Dist: 3.85 },
        Avenue: { Target: [-0.10, 0.85, -0.35], Yaw: -0.52, Pitch: 0.38, Dist: 4.40 }
    },
    "full-arbor": {
        Shadow: { Target: [-1.05, 0.32, -0.95], Yaw: 0.54,  Pitch: 0.58, Dist: 3.60 },
        Canopy: { Target: [-0.35, 1.55, -0.45], Yaw: 0.52,  Pitch: 0.30, Dist: 6.30 },
        Wall:   { Target: [-0.65, 1.65, -2.35], Yaw: 0.18,  Pitch: 0.20, Dist: 5.90 },
        Avenue: { Target: [-0.20, 1.50, -0.40], Yaw: -0.62, Pitch: 0.34, Dist: 7.60 }
    },
    "avenue-lod": {
        Shadow: { Target: [-1.10, 0.35, 0.35],  Yaw: 0.62,  Pitch: 0.52, Dist: 4.10 },
        Canopy: { Target: [-0.80, 1.75, -3.20], Yaw: 0.58,  Pitch: 0.26, Dist: 9.20 },
        Wall:   { Target: [-2.80, 1.85, -5.20], Yaw: 0.78,  Pitch: 0.20, Dist: 8.80 },
        Avenue: { Target: [-0.60, 1.85, -5.80], Yaw: 0.34,  Pitch: 0.22, Dist: 12.80 }
    }
};

// ============================================================================
// Minimal Pure Column-Major 4x4 Transform Math (Zero External Dependencies)
// ============================================================================

function Normalize3(V)
{
    const L = Math.hypot(V[0], V[1], V[2]) || 1.0;
    return [V[0] / L, V[1] / L, V[2] / L];
}

function Subtract3(A, B)
{
    return [A[0] - B[0], A[1] - B[1], A[2] - B[2]];
}

function Cross3(A, B)
{
    return [
        A[1] * B[2] - A[2] * B[1],
        A[2] * B[0] - A[0] * B[2],
        A[0] * B[1] - A[1] * B[0]
    ];
}

function Dot3(A, B)
{
    return A[0] * B[0] + A[1] * B[1] + A[2] * B[2];
}

function BuildLookAtTransform(Eye, Target, Up)
{
    const F = Normalize3(Subtract3(Target, Eye));
    const S = Normalize3(Cross3(F, Up));
    const U = Cross3(S, F);

    const Out = new Float32Array(16);
    Out[0] = S[0];  Out[4] = S[1];  Out[8]  = S[2];  Out[12] = -Dot3(S, Eye);
    Out[1] = U[0];  Out[5] = U[1];  Out[9]  = U[2];  Out[13] = -Dot3(U, Eye);
    Out[2] = -F[0]; Out[6] = -F[1]; Out[10] = -F[2]; Out[14] =  Dot3(F, Eye);
    Out[3] = 0.0;   Out[7] = 0.0;   Out[11] = 0.0;   Out[15] =  1.0;
    return Out;
}

// WebGPU [0, 1] NDC depth perspective transform
function BuildPerspectiveZeroToOne(FovYRad, Aspect, Near, Far)
{
    const F = 1.0 / Math.tan(0.5 * FovYRad);
    const Out = new Float32Array(16);
    Out[0]  = F / Math.max(Aspect, 1e-4);
    Out[5]  = F;
    Out[10] = Far / (Near - Far);
    Out[11] = -1.0;
    Out[14] = (Far * Near) / (Near - Far);
    return Out;
}

// WebGL2 [-1, +1] NDC depth perspective transform
function BuildPerspectiveNegOneToOne(FovYRad, Aspect, Near, Far)
{
    const F = 1.0 / Math.tan(0.5 * FovYRad);
    const Nf = 1.0 / (Near - Far);
    const Out = new Float32Array(16);
    Out[0]  = F / Math.max(Aspect, 1e-4);
    Out[5]  = F;
    Out[10] = (Far + Near) * Nf;
    Out[11] = -1.0;
    Out[14] = (2.0 * Far * Near) * Nf;
    return Out;
}

function MultiplyTransforms4x4(A, B)
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

function ComputeSolarDirection(AzimuthDeg, ElevationDeg)
{
    const Az = (AzimuthDeg * Math.PI) / 180.0;
    const El = (ElevationDeg * Math.PI) / 180.0;
    const CosEl = Math.cos(El);
    return Normalize3([
        CosEl * Math.sin(Az),
        Math.sin(El),
        CosEl * Math.cos(Az)
    ]);
}

function PackBrickHeaderFloats(CompiledVol)
{
    const BrickCount = CanopyBrickRes * CanopyBrickRes * CanopyBrickRes;
    const Packed = new Float32Array(BrickCount * 8);
    for (let B = 0; B < BrickCount; ++B)
    {
        Packed[B * 8 + 0] = CompiledVol.BrickOffsetsU32[B * 4 + 0];
        Packed[B * 8 + 1] = CompiledVol.BrickOffsetsU32[B * 4 + 1];
        Packed[B * 8 + 2] = 0.0;
        Packed[B * 8 + 3] = 0.0;
        Packed[B * 8 + 4] = CompiledVol.BrickOpticalF32[B * 4 + 0];
        Packed[B * 8 + 5] = CompiledVol.BrickOpticalF32[B * 4 + 1];
        Packed[B * 8 + 6] = CompiledVol.BrickOpticalF32[B * 4 + 2];
        Packed[B * 8 + 7] = CompiledVol.BrickOpticalF32[B * 4 + 3];
    }
    return Packed;
}

// ============================================================================
// 2D Leaf Template SDF Contour Inspector Renderer (#LeafArchetypeCanvas)
// ============================================================================

function PaintLeafArchetypeInspector(CanvasEl, CompiledArchetypes, ArchIdx)
{
    const Ctx2d = CanvasEl.getContext("2d");
    const W = CanvasEl.width;
    const H = CanvasEl.height;
    const Img = Ctx2d.createImageData(W, H);
    const Pix = Img.data;
    const Grid = CompiledArchetypes.FloatGrids[ArchIdx];

    for (let Py = 0; Py < H; ++Py)
    {
        // Flip Y visually so leaf tip (+W = +1) points UPWARD in the 2D inspector!
        const NormW = 1.0 - (Py + 0.5) / H * 2.0;
        const Gy = Math.max(0.0, Math.min(LeafSdf2dRes - 1.001, (NormW * 0.5 + 0.5) * LeafSdf2dRes - 0.5));
        const Y0 = Math.floor(Gy), Y1 = Math.min(LeafSdf2dRes - 1, Y0 + 1);
        const Ty = Gy - Y0;

        for (let Px = 0; Px < W; ++Px)
        {
            const NormU = (Px + 0.5) / W * 2.0 - 1.0;
            const Gx = Math.max(0.0, Math.min(LeafSdf2dRes - 1.001, (NormU * 0.5 + 0.5) * LeafSdf2dRes - 0.5));
            const X0 = Math.floor(Gx), X1 = Math.min(LeafSdf2dRes - 1, X0 + 1);
            const Tx = Gx - X0;

            const D00 = Grid[Y0 * LeafSdf2dRes + X0];
            const D10 = Grid[Y0 * LeafSdf2dRes + X1];
            const D01 = Grid[Y1 * LeafSdf2dRes + X0];
            const D11 = Grid[Y1 * LeafSdf2dRes + X1];
            const D = (D00 * (1.0 - Tx) + D10 * Tx) * (1.0 - Ty) + (D01 * (1.0 - Tx) + D11 * Tx) * Ty;

            // Periodic Signed Distance Isolines + Crisp White Zero-Crossing Contour!
            const Wave = 0.5 + 0.5 * Math.cos(D * 78.0);
            const IsoRing = Math.pow(Wave, 10.0);
            const ZeroEdge = Math.exp(-Math.abs(D) * 145.0);

            let R = 0.0, G = 0.0, B = 0.0;
            if (D <= 0.0)
            {
                const Depth = Math.min(1.0, -D * 3.4);
                R = 26 + 38 * Depth + 46 * IsoRing;
                G = 118 + 92 * Depth + 50 * IsoRing;
                B = 64 + 48 * Depth + 38 * IsoRing;
            }
            else
            {
                const DistFade = Math.exp(-D * 2.1);
                R = 12 + 24 * DistFade + 34 * IsoRing * DistFade;
                G = 24 + 58 * DistFade + 62 * IsoRing * DistFade;
                B = 38 + 94 * DistFade + 82 * IsoRing * DistFade;
            }

            R = R * (1.0 - ZeroEdge) + 248.0 * ZeroEdge;
            G = G * (1.0 - ZeroEdge) + 252.0 * ZeroEdge;
            B = B * (1.0 - ZeroEdge) + 245.0 * ZeroEdge;

            const Out = (Py * W + Px) * 4;
            Pix[Out + 0] = Math.min(255, Math.max(0, Math.round(R)));
            Pix[Out + 1] = Math.min(255, Math.max(0, Math.round(G)));
            Pix[Out + 2] = Math.min(255, Math.max(0, Math.round(B)));
            Pix[Out + 3] = 255;
        }
    }
    Ctx2d.putImageData(Img, 0, 0);
}

// ============================================================================
// Primary Native WebGPU Renderer (WGSL + rgba16float 3D & 2D-Array + Storage)
// ============================================================================

async function InitializeWebGpuRenderer(CanvasEl, CompiledArchetypes)
{
    if (typeof navigator === "undefined" || !navigator.gpu)
    {
        return null;
    }

    const Adapter = await navigator.gpu.requestAdapter({ powerPreference: "high-performance" });
    if (!Adapter)
    {
        return null;
    }

    const Device = await Adapter.requestDevice();
    const GpuCanvas = CanvasEl.getContext("webgpu");
    if (!GpuCanvas)
    {
        return null;
    }

    const CanvasFormat = navigator.gpu.getPreferredCanvasFormat();
    GpuCanvas.configure({
        device: Device,
        format: CanvasFormat,
        alphaMode: "opaque"
    });

    const SurfaceShader = Device.createShaderModule({ code: FoliageSurfaceWgsl });
    const WireShader    = Device.createShaderModule({ code: DebugWireframeWgsl });

    if (SurfaceShader.getCompilationInfo)
    {
        const Comp = await SurfaceShader.getCompilationInfo();
        for (const Msg of Comp.messages)
        {
            if (Msg.type === "error")
            {
                console.error("WGSL compilation error:", Msg.message, "line", Msg.lineNum);
                return null;
            }
        }
    }

    const UniformBuffer = Device.createBuffer({
        size: 192,
        usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST
    });

    const LinearClampSampler = Device.createSampler({
        magFilter: "linear",
        minFilter: "linear",
        mipmapFilter: "nearest",
        addressModeU: "clamp-to-edge",
        addressModeV: "clamp-to-edge",
        addressModeW: "clamp-to-edge"
    });

    // Upload 5 x 64x64 rgba16float 2D-Array Leaf Archetype SDF Texture (20 KB)
    const LeafArchetype2dTex = Device.createTexture({
        size: [LeafSdf2dRes, LeafSdf2dRes, LeafArchetypeCount],
        dimension: "2d",
        format: "rgba16float",
        usage: GPUTextureUsage.TEXTURE_BINDING | GPUTextureUsage.COPY_DST
    });
    Device.queue.writeTexture(
        { texture: LeafArchetype2dTex },
        CompiledArchetypes.Rgba16fTexels,
        { bytesPerRow: LeafSdf2dRes * 8, rowsPerImage: LeafSdf2dRes },
        [LeafSdf2dRes, LeafSdf2dRes, LeafArchetypeCount]
    );

    // Explicit BindGroupLayout so group(0) layout is deterministic and clean
    const SurfaceGroupLayout = Device.createBindGroupLayout({
        entries: [
            { binding: 0, visibility: GPUShaderStage.VERTEX | GPUShaderStage.FRAGMENT, buffer: { type: "uniform" } },
            { binding: 1, visibility: GPUShaderStage.FRAGMENT, sampler: { type: "filtering" } },
            { binding: 2, visibility: GPUShaderStage.FRAGMENT, texture: { sampleType: "float", viewDimension: "2d-array" } },
            { binding: 3, visibility: GPUShaderStage.FRAGMENT, texture: { sampleType: "float", viewDimension: "3d" } },
            { binding: 4, visibility: GPUShaderStage.FRAGMENT, texture: { sampleType: "float", viewDimension: "3d" } },
            { binding: 5, visibility: GPUShaderStage.FRAGMENT, buffer: { type: "read-only-storage" } },
            { binding: 6, visibility: GPUShaderStage.FRAGMENT, buffer: { type: "read-only-storage" } },
            { binding: 7, visibility: GPUShaderStage.FRAGMENT, buffer: { type: "read-only-storage" } },
            { binding: 8, visibility: GPUShaderStage.FRAGMENT, buffer: { type: "read-only-storage" } }
        ]
    });

    const WireGroupLayout = Device.createBindGroupLayout({
        entries: [
            { binding: 0, visibility: GPUShaderStage.VERTEX | GPUShaderStage.FRAGMENT, buffer: { type: "uniform" } }
        ]
    });

    const SurfaceProg = Device.createRenderPipeline({
        layout: Device.createPipelineLayout({ bindGroupLayouts: [SurfaceGroupLayout] }),
        vertex: {
            module: SurfaceShader,
            entryPoint: "VsMain",
            buffers: [{
                arrayStride: SurfaceVertexBytes,
                attributes: [
                    { shaderLocation: 0, offset: 0,  format: "float32x4" },
                    { shaderLocation: 1, offset: 16, format: "float32x4" },
                    { shaderLocation: 2, offset: 32, format: "float32x4" },
                    { shaderLocation: 3, offset: 48, format: "float32x4" }
                ]
            }]
        },
        fragment: {
            module: SurfaceShader,
            entryPoint: "FsMain",
            targets: [{ format: CanvasFormat }]
        },
        primitive: {
            topology: "triangle-list",
            cullMode: "none"
        },
        depthStencil: {
            format: "depth24plus",
            depthWriteEnabled: true,
            depthCompare: "less"
        }
    });

    const WireProg = Device.createRenderPipeline({
        layout: Device.createPipelineLayout({ bindGroupLayouts: [WireGroupLayout] }),
        vertex: {
            module: WireShader,
            entryPoint: "VsWire",
            buffers: [{
                arrayStride: DebugLineVertexBytes,
                attributes: [
                    { shaderLocation: 0, offset: 0,  format: "float32x4" },
                    { shaderLocation: 1, offset: 16, format: "float32x4" }
                ]
            }]
        },
        fragment: {
            module: WireShader,
            entryPoint: "FsWire",
            targets: [{
                format: CanvasFormat,
                blend: {
                    color: { srcFactor: "src-alpha", dstFactor: "one-minus-src-alpha", operation: "add" },
                    alpha: { srcFactor: "one",       dstFactor: "one-minus-src-alpha", operation: "add" }
                }
            }]
        },
        primitive: {
            topology: "line-list"
        },
        depthStencil: {
            format: "depth24plus",
            depthWriteEnabled: false,
            depthCompare: "less-equal"
        }
    });

    const WireUniformGroup = Device.createBindGroup({
        layout: WireGroupLayout,
        entries: [{ binding: 0, resource: { buffer: UniformBuffer } }]
    });

    let DepthTex = null;
    let DepthW = 0, DepthH = 0;
    let ActiveGpuScene = null;
    let ActiveWireBuf = null;
    let ActiveWireVertCount = 0;

    function Align16(ByteLen)
    {
        return Math.max(16, (ByteLen + 15) & ~15);
    }

    function CreateUploadedBuffer(TypedArr, UsageBits)
    {
        const PaddedBytes = Align16(TypedArr.byteLength);
        const Buf = Device.createBuffer({ size: PaddedBytes, usage: UsageBits | GPUBufferUsage.COPY_DST });
        if (TypedArr.byteLength === PaddedBytes)
        {
            Device.queue.writeBuffer(Buf, 0, TypedArr);
        }
        else
        {
            const Padded = new Uint8Array(PaddedBytes);
            Padded.set(new Uint8Array(TypedArr.buffer, TypedArr.byteOffset, TypedArr.byteLength));
            Device.queue.writeBuffer(Buf, 0, Padded);
        }
        return Buf;
    }

    function UploadSceneResources(CompiledVol, SurfaceGeom)
    {
        if (ActiveGpuScene)
        {
            ActiveGpuScene.MacroTex.destroy();
            ActiveGpuScene.CompTex.destroy();
            ActiveGpuScene.BrickHdrBuf.destroy();
            ActiveGpuScene.BrickRefBuf.destroy();
            ActiveGpuScene.LeafBuf.destroy();
            ActiveGpuScene.CapBuf.destroy();
            ActiveGpuScene.VertBuf.destroy();
            ActiveGpuScene.IdxBuf.destroy();
        }

        const MacroTex = Device.createTexture({
            size: [MacroVoxelRes, MacroVoxelRes, MacroVoxelRes],
            dimension: "3d",
            format: "rgba16float",
            usage: GPUTextureUsage.TEXTURE_BINDING | GPUTextureUsage.COPY_DST
        });
        Device.queue.writeTexture(
            { texture: MacroTex },
            CompiledVol.MacroRgba16f,
            { bytesPerRow: MacroVoxelRes * 8, rowsPerImage: MacroVoxelRes },
            [MacroVoxelRes, MacroVoxelRes, MacroVoxelRes]
        );

        const CompTex = Device.createTexture({
            size: [MacroVoxelRes, MacroVoxelRes, MacroVoxelRes],
            dimension: "3d",
            format: "rgba16float",
            usage: GPUTextureUsage.TEXTURE_BINDING | GPUTextureUsage.COPY_DST
        });
        Device.queue.writeTexture(
            { texture: CompTex },
            CompiledVol.ComparisonRgba16f,
            { bytesPerRow: MacroVoxelRes * 8, rowsPerImage: MacroVoxelRes },
            [MacroVoxelRes, MacroVoxelRes, MacroVoxelRes]
        );

        const BrickHdrFloats = PackBrickHeaderFloats(CompiledVol);
        const BrickHdrBuf = CreateUploadedBuffer(BrickHdrFloats, GPUBufferUsage.STORAGE);
        const BrickRefBuf = CreateUploadedBuffer(CompiledVol.BrickLeafRefsU32, GPUBufferUsage.STORAGE);
        const LeafBuf     = CreateUploadedBuffer(CompiledVol.LeafGpuFloats, GPUBufferUsage.STORAGE);
        const CapBuf      = CreateUploadedBuffer(CompiledVol.CapsuleGpuFloats, GPUBufferUsage.STORAGE);

        const VertBuf = CreateUploadedBuffer(SurfaceGeom.VerticesF32, GPUBufferUsage.VERTEX);
        const IdxBuf  = CreateUploadedBuffer(SurfaceGeom.IndicesU32, GPUBufferUsage.INDEX);

        const SurfaceGroup = Device.createBindGroup({
            layout: SurfaceGroupLayout,
            entries: [
                { binding: 0, resource: { buffer: UniformBuffer } },
                { binding: 1, resource: LinearClampSampler },
                { binding: 2, resource: LeafArchetype2dTex.createView({ dimension: "2d-array" }) },
                { binding: 3, resource: MacroTex.createView({ dimension: "3d" }) },
                { binding: 4, resource: CompTex.createView({ dimension: "3d" }) },
                { binding: 5, resource: { buffer: BrickHdrBuf } },
                { binding: 6, resource: { buffer: BrickRefBuf } },
                { binding: 7, resource: { buffer: LeafBuf } },
                { binding: 8, resource: { buffer: CapBuf } }
            ]
        });

        ActiveGpuScene = {
            MacroTex,
            CompTex,
            BrickHdrBuf,
            BrickRefBuf,
            LeafBuf,
            CapBuf,
            VertBuf,
            IdxBuf,
            IndexCount: SurfaceGeom.IndexCount,
            SurfaceGroup
        };
    }

    function UploadDebugWireframes(WireGeom)
    {
        if (ActiveWireBuf)
        {
            ActiveWireBuf.destroy();
            ActiveWireBuf = null;
        }
        ActiveWireVertCount = WireGeom.VertexCount;
        if (ActiveWireVertCount > 0)
        {
            ActiveWireBuf = CreateUploadedBuffer(WireGeom.VerticesF32, GPUBufferUsage.VERTEX);
        }
    }

    function RenderViewport(UniformFloats, DebugMask)
    {
        if (!ActiveGpuScene) return;
        const W = CanvasEl.width;
        const H = CanvasEl.height;
        if (W <= 0 || H <= 0) return;

        if (!DepthTex || DepthW !== W || DepthH !== H)
        {
            if (DepthTex) DepthTex.destroy();
            DepthTex = Device.createTexture({
                size: [W, H, 1],
                format: "depth24plus",
                usage: GPUTextureUsage.RENDER_ATTACHMENT
            });
            DepthW = W;
            DepthH = H;
        }

        Device.queue.writeBuffer(UniformBuffer, 0, UniformFloats);

        const CmdEncoder = Device.createCommandEncoder();
        const ColorView = GpuCanvas.getCurrentTexture().createView();
        const RenderOp = CmdEncoder.beginRenderPass({
            colorAttachments: [{
                view: ColorView,
                clearValue: { r: 0.055, g: 0.082, b: 0.110, a: 1.0 },
                loadOp: "clear",
                storeOp: "store"
            }],
            depthStencilAttachment: {
                view: DepthTex.createView(),
                depthClearValue: 1.0,
                depthLoadOp: "clear",
                depthStoreOp: "store"
            }
        });

        RenderOp.setPipeline(SurfaceProg);
        RenderOp.setBindGroup(0, ActiveGpuScene.SurfaceGroup);
        RenderOp.setVertexBuffer(0, ActiveGpuScene.VertBuf);
        RenderOp.setIndexBuffer(ActiveGpuScene.IdxBuf, "uint32");
        RenderOp.drawIndexed(ActiveGpuScene.IndexCount);

        if (DebugMask > 0 && ActiveWireBuf && ActiveWireVertCount > 0)
        {
            RenderOp.setPipeline(WireProg);
            RenderOp.setBindGroup(0, WireUniformGroup);
            RenderOp.setVertexBuffer(0, ActiveWireBuf);
            RenderOp.draw(ActiveWireVertCount);
        }

        RenderOp.end();
        Device.queue.submit([CmdEncoder.finish()]);
    }

    const AdapterLabel = (Adapter.info && (Adapter.info.description || Adapter.info.vendor))
        ? `🟢 WebGPU · WGSL (${Adapter.info.description || Adapter.info.vendor})`
        : "🟢 WebGPU · WGSL Hardware";

    return {
        GpuFlavor: "webgpu",
        RuntimeLabel: AdapterLabel,
        UploadSceneResources,
        UploadDebugWireframes,
        RenderViewport
    };
}

// ============================================================================
// Automatic WebGL2 Fallback Renderer (when navigator.gpu is absent)
// ============================================================================

function InitializeWebGl2FallbackRenderer(CanvasEl, CompiledArchetypes)
{
    const Gl = CanvasEl.getContext("webgl2", { antialias: true, depth: true });
    if (!Gl) return null;

    function CompileProg(VsSrc, FsSrc)
    {
        const Vs = Gl.createShader(Gl.VERTEX_SHADER);
        Gl.shaderSource(Vs, VsSrc);
        Gl.compileShader(Vs);
        if (!Gl.getShaderParameter(Vs, Gl.COMPILE_STATUS))
        {
            console.error("GLSL VS error:", Gl.getShaderInfoLog(Vs));
            return null;
        }
        const Fs = Gl.createShader(Gl.FRAGMENT_SHADER);
        Gl.shaderSource(Fs, FsSrc);
        Gl.compileShader(Fs);
        if (!Gl.getShaderParameter(Fs, Gl.COMPILE_STATUS))
        {
            console.error("GLSL FS error:", Gl.getShaderInfoLog(Fs));
            return null;
        }
        const Prog = Gl.createProgram();
        Gl.attachShader(Prog, Vs);
        Gl.attachShader(Prog, Fs);
        Gl.linkProgram(Prog);
        if (!Gl.getProgramParameter(Prog, Gl.LINK_STATUS))
        {
            console.error("GLSL Link error:", Gl.getProgramInfoLog(Prog));
            return null;
        }
        return Prog;
    }

    const SurfaceProg = CompileProg(FoliageSurfaceGlslVert, FoliageSurfaceGlslFrag);
    const WireProg    = CompileProg(DebugWireframeGlslVert, DebugWireframeGlslFrag);
    if (!SurfaceProg || !WireProg) return null;

    // Upload 64x64x5 RGBA16F 2D-Array Texture
    const LeafTex = Gl.createTexture();
    Gl.bindTexture(Gl.TEXTURE_2D_ARRAY, LeafTex);
    Gl.texParameteri(Gl.TEXTURE_2D_ARRAY, Gl.TEXTURE_MIN_FILTER, Gl.LINEAR);
    Gl.texParameteri(Gl.TEXTURE_2D_ARRAY, Gl.TEXTURE_MAG_FILTER, Gl.LINEAR);
    Gl.texParameteri(Gl.TEXTURE_2D_ARRAY, Gl.TEXTURE_WRAP_S, Gl.CLAMP_TO_EDGE);
    Gl.texParameteri(Gl.TEXTURE_2D_ARRAY, Gl.TEXTURE_WRAP_T, Gl.CLAMP_TO_EDGE);
    Gl.texImage3D(
        Gl.TEXTURE_2D_ARRAY, 0, Gl.RGBA16F,
        LeafSdf2dRes, LeafSdf2dRes, LeafArchetypeCount, 0,
        Gl.RGBA, Gl.HALF_FLOAT, CompiledArchetypes.Rgba16fTexels
    );

    let MacroTex = null, CompTex = null, PackedStorageTex = null;
    let SurfaceVao = null, VertBuf = null, IdxBuf = null, IndexCount = 0;
    let WireVao = null, WireBuf = null, WireVertCount = 0;
    let PackedOffsets = [0, 0, 0, 256];

    function Create3dHalfTex(HalfTexels)
    {
        const T = Gl.createTexture();
        Gl.bindTexture(Gl.TEXTURE_3D, T);
        Gl.texParameteri(Gl.TEXTURE_3D, Gl.TEXTURE_MIN_FILTER, Gl.LINEAR);
        Gl.texParameteri(Gl.TEXTURE_3D, Gl.TEXTURE_MAG_FILTER, Gl.LINEAR);
        Gl.texParameteri(Gl.TEXTURE_3D, Gl.TEXTURE_WRAP_S, Gl.CLAMP_TO_EDGE);
        Gl.texParameteri(Gl.TEXTURE_3D, Gl.TEXTURE_WRAP_T, Gl.CLAMP_TO_EDGE);
        Gl.texParameteri(Gl.TEXTURE_3D, Gl.TEXTURE_WRAP_R, Gl.CLAMP_TO_EDGE);
        Gl.texImage3D(
            Gl.TEXTURE_3D, 0, Gl.RGBA16F,
            MacroVoxelRes, MacroVoxelRes, MacroVoxelRes, 0,
            Gl.RGBA, Gl.HALF_FLOAT, HalfTexels
        );
        return T;
    }

    function UploadSceneResources(CompiledVol, SurfaceGeom)
    {
        if (MacroTex) Gl.deleteTexture(MacroTex);
        if (CompTex) Gl.deleteTexture(CompTex);
        if (PackedStorageTex) Gl.deleteTexture(PackedStorageTex);
        if (SurfaceVao) Gl.deleteVertexArray(SurfaceVao);
        if (VertBuf) Gl.deleteBuffer(VertBuf);
        if (IdxBuf) Gl.deleteBuffer(IdxBuf);

        MacroTex = Create3dHalfTex(CompiledVol.MacroRgba16f);
        CompTex  = Create3dHalfTex(CompiledVol.ComparisonRgba16f);

        // Pack BrickHeaders + BrickLeafRefs + LeafInstances + WoodCapsules into a 256-wide RGBA32F 2D texture!
        const HdrF32 = PackBrickHeaderFloats(CompiledVol);
        const HdrVec4Count  = HdrF32.length / 4;
        const RefVec4Count  = Math.ceil(CompiledVol.BrickLeafRefsU32.length / 4);
        const LeafVec4Count = CompiledVol.LeafGpuFloats.length / 4;
        const CapVec4Count  = CompiledVol.CapsuleGpuFloats.length / 4;

        const RefStart  = HdrVec4Count;
        const LeafStart = RefStart + RefVec4Count;
        const CapStart  = LeafStart + LeafVec4Count;
        const TotalVec4 = CapStart + CapVec4Count;
        const TexW = 256;
        const TexH = Math.ceil(TotalVec4 / TexW);
        const PackedF32 = new Float32Array(TexW * TexH * 4);

        PackedF32.set(HdrF32, 0);
        for (let R = 0; R < CompiledVol.BrickLeafRefsU32.length; ++R)
        {
            PackedF32[RefStart * 4 + R] = CompiledVol.BrickLeafRefsU32[R];
        }
        PackedF32.set(CompiledVol.LeafGpuFloats, LeafStart * 4);
        PackedF32.set(CompiledVol.CapsuleGpuFloats, CapStart * 4);
        PackedOffsets = [RefStart, LeafStart, CapStart, TexW];

        PackedStorageTex = Gl.createTexture();
        Gl.bindTexture(Gl.TEXTURE_2D, PackedStorageTex);
        Gl.texParameteri(Gl.TEXTURE_2D, Gl.TEXTURE_MIN_FILTER, Gl.NEAREST);
        Gl.texParameteri(Gl.TEXTURE_2D, Gl.TEXTURE_MAG_FILTER, Gl.NEAREST);
        Gl.texParameteri(Gl.TEXTURE_2D, Gl.TEXTURE_WRAP_S, Gl.CLAMP_TO_EDGE);
        Gl.texParameteri(Gl.TEXTURE_2D, Gl.TEXTURE_WRAP_T, Gl.CLAMP_TO_EDGE);
        Gl.texImage2D(Gl.TEXTURE_2D, 0, Gl.RGBA32F, TexW, TexH, 0, Gl.RGBA, Gl.FLOAT, PackedF32);

        SurfaceVao = Gl.createVertexArray();
        Gl.bindVertexArray(SurfaceVao);
        VertBuf = Gl.createBuffer();
        Gl.bindBuffer(Gl.ARRAY_BUFFER, VertBuf);
        Gl.bufferData(Gl.ARRAY_BUFFER, SurfaceGeom.VerticesF32, Gl.STATIC_DRAW);

        for (let Loc = 0; Loc < 4; ++Loc)
        {
            Gl.enableVertexAttribArray(Loc);
            Gl.vertexAttribPointer(Loc, 4, Gl.FLOAT, false, SurfaceVertexBytes, Loc * 16);
        }

        IdxBuf = Gl.createBuffer();
        Gl.bindBuffer(Gl.ELEMENT_ARRAY_BUFFER, IdxBuf);
        Gl.bufferData(Gl.ELEMENT_ARRAY_BUFFER, SurfaceGeom.IndicesU32, Gl.STATIC_DRAW);
        Gl.bindVertexArray(null);
        IndexCount = SurfaceGeom.IndexCount;
    }

    function UploadDebugWireframes(WireGeom)
    {
        if (WireVao) Gl.deleteVertexArray(WireVao);
        if (WireBuf) Gl.deleteBuffer(WireBuf);
        WireVertCount = WireGeom.VertexCount;
        if (WireVertCount <= 0) return;

        WireVao = Gl.createVertexArray();
        Gl.bindVertexArray(WireVao);
        WireBuf = Gl.createBuffer();
        Gl.bindBuffer(Gl.ARRAY_BUFFER, WireBuf);
        Gl.bufferData(Gl.ARRAY_BUFFER, WireGeom.VerticesF32, Gl.DYNAMIC_DRAW);
        Gl.enableVertexAttribArray(0);
        Gl.vertexAttribPointer(0, 4, Gl.FLOAT, false, DebugLineVertexBytes, 0);
        Gl.enableVertexAttribArray(1);
        Gl.vertexAttribPointer(1, 4, Gl.FLOAT, false, DebugLineVertexBytes, 16);
        Gl.bindVertexArray(null);
    }

    function RenderViewport(UniformFloats, DebugMask)
    {
        if (!SurfaceVao) return;
        Gl.viewport(0, 0, CanvasEl.width, CanvasEl.height);
        Gl.clearColor(0.055, 0.082, 0.110, 1.0);
        Gl.clear(Gl.COLOR_BUFFER_BIT | Gl.DEPTH_BUFFER_BIT);
        Gl.enable(Gl.DEPTH_TEST);
        Gl.disable(Gl.CULL_FACE);

        Gl.useProgram(SurfaceProg);
        Gl.uniformMatrix4fv(Gl.getUniformLocation(SurfaceProg, "uViewProjTransform"), false, UniformFloats.subarray(0, 16));
        Gl.uniform4fv(Gl.getUniformLocation(SurfaceProg, "uCameraEyeCone"),   UniformFloats.subarray(16, 20));
        Gl.uniform4fv(Gl.getUniformLocation(SurfaceProg, "uSunDirMode"),      UniformFloats.subarray(20, 24));
        Gl.uniform4fv(Gl.getUniformLocation(SurfaceProg, "uBoundsMinThick"),  UniformFloats.subarray(24, 28));
        Gl.uniform4fv(Gl.getUniformLocation(SurfaceProg, "uBoundsMaxDiag"),   UniformFloats.subarray(28, 32));
        Gl.uniform4fv(Gl.getUniformLocation(SurfaceProg, "uViewportVis"),     UniformFloats.subarray(32, 36));
        Gl.uniform4fv(Gl.getUniformLocation(SurfaceProg, "uSceneCountsMask"), UniformFloats.subarray(36, 40));
        Gl.uniform4fv(Gl.getUniformLocation(SurfaceProg, "uBarkParams"),      UniformFloats.subarray(40, 44));
        Gl.uniform4iv(Gl.getUniformLocation(SurfaceProg, "uPackedOffsets"),   PackedOffsets);

        Gl.activeTexture(Gl.TEXTURE0);
        Gl.bindTexture(Gl.TEXTURE_2D_ARRAY, LeafTex);
        Gl.uniform1i(Gl.getUniformLocation(SurfaceProg, "uLeafArchetype2dTex"), 0);

        Gl.activeTexture(Gl.TEXTURE1);
        Gl.bindTexture(Gl.TEXTURE_3D, MacroTex);
        Gl.uniform1i(Gl.getUniformLocation(SurfaceProg, "uMacroSdf3dTex"), 1);

        Gl.activeTexture(Gl.TEXTURE2);
        Gl.bindTexture(Gl.TEXTURE_3D, CompTex);
        Gl.uniform1i(Gl.getUniformLocation(SurfaceProg, "uComparisonVoxel3dTex"), 2);

        Gl.activeTexture(Gl.TEXTURE3);
        Gl.bindTexture(Gl.TEXTURE_2D, PackedStorageTex);
        Gl.uniform1i(Gl.getUniformLocation(SurfaceProg, "uPackedStorageTex"), 3);

        Gl.bindVertexArray(SurfaceVao);
        Gl.drawElements(Gl.TRIANGLES, IndexCount, Gl.UNSIGNED_INT, 0);

        if (DebugMask > 0 && WireVao && WireVertCount > 0)
        {
            Gl.enable(Gl.BLEND);
            Gl.blendFunc(Gl.SRC_ALPHA, Gl.ONE_MINUS_SRC_ALPHA);
            Gl.depthMask(false);
            Gl.useProgram(WireProg);
            Gl.uniformMatrix4fv(Gl.getUniformLocation(WireProg, "uViewProjTransform"), false, UniformFloats.subarray(0, 16));
            Gl.uniform1i(Gl.getUniformLocation(WireProg, "uDebugMask"), DebugMask);
            Gl.bindVertexArray(WireVao);
            Gl.drawArrays(Gl.LINES, 0, WireVertCount);
            Gl.depthMask(true);
            Gl.disable(Gl.BLEND);
        }
    }

    return {
        GpuFlavor: "webgl2",
        RuntimeLabel: "🏷️ WebGL2 Fallback (navigator.gpu off)",
        UploadSceneResources,
        UploadDebugWireframes,
        RenderViewport
    };
}

// ============================================================================
// Main Interactive Workbench Bootstrap
// ============================================================================

async function LaunchThinLeafSdfWorkbench()
{
    const CanvasEl = document.getElementById("ViewportCanvas");
    const ArchetypeCanvasEl = document.getElementById("LeafArchetypeCanvas");

    // 1. Compile the 5 exact 64x64 2D Leaf Archetype SDFs once at startup (~110 ms)
    const TArch0 = performance.now();
    const CompiledArchetypes = CompileFiveLeafArchetypeSdfs();
    const ArchMs = performance.now() - TArch0;

    // 2. Initialize Native WebGPU (with automatic WebGL2 fallback only if navigator.gpu is absent)
    let Renderer = await InitializeWebGpuRenderer(CanvasEl, CompiledArchetypes);
    if (!Renderer)
    {
        Renderer = InitializeWebGl2FallbackRenderer(CanvasEl, CompiledArchetypes);
    }
    const RuntimeTagEl = document.getElementById("GpuRuntimeTag");
    if (!Renderer)
    {
        RuntimeTagEl.textContent = "🔴 No WebGPU / WebGL2 GPU API";
        return;
    }
    RuntimeTagEl.textContent = Renderer.RuntimeLabel;

    // Interactive Workbench Settings
    const Settings = {
        PresetId: "full-arbor",
        SdfMode: SdfEvaluationMode.TwoLevelThinSheet,
        DisplayMode: 0,
        SunAzimuthDeg: 42.0,
        SunElevationDeg: 55.0,
        SunConeDeg: 0.46,
        LodScale: 1.00,
        BarkNoiseAmp: DefaultBarkNoiseAmp,
        BarkNoiseFreq: DefaultBarkNoiseFreq,
        BranchGnarl: DefaultBranchGnarl,
        DebugCapsules: false,
        DebugBricks: false,
        DebugLeaves: false,
        DebugRays: true,
        OrbitSunActive: false,
        SelectedArchetype: 0,
        RayOriginGround: [-0.95, 0.015, -0.85],
        CamTarget: [-0.35, 1.55, -0.45],
        CamYaw: 0.52,
        CamPitch: 0.30,
        CamDist: 6.30
    };

    let ActiveVol = null;
    let LatestBenchmark = null;
    let BenchDebounceTimer = null;
    let BarkRebuildTimer = null;

    function GetCameraEye()
    {
        const CosP = Math.cos(Settings.CamPitch);
        return [
            Settings.CamTarget[0] + Settings.CamDist * CosP * Math.sin(Settings.CamYaw),
            Settings.CamTarget[1] + Settings.CamDist * Math.sin(Settings.CamPitch),
            Settings.CamTarget[2] + Settings.CamDist * CosP * Math.cos(Settings.CamYaw)
        ];
    }

    function ApplyCameraBookmark(BookmarkName)
    {
        const Group = CameraBookmarks[Settings.PresetId] || CameraBookmarks["full-arbor"];
        const Bm = Group[BookmarkName] || Group.Canopy;
        Settings.CamTarget = [...Bm.Target];
        Settings.CamYaw    = Bm.Yaw;
        Settings.CamPitch  = Bm.Pitch;
        Settings.CamDist   = Bm.Dist;

        const BookmarkButtons = { Shadow: "CamShadowBtn", Canopy: "CamCanopyBtn", Wall: "CamWallBtn", Avenue: "CamFarLodBtn" };
        for (const [Key, BtnId] of Object.entries(BookmarkButtons))
        {
            document.getElementById(BtnId).classList.toggle("Active", Key === BookmarkName);
        }
    }

    function RefreshHudMetrics()
    {
        if (!LatestBenchmark || !ActiveVol) return;
        const Ordered = [
            LatestBenchmark.StandardVoxel,
            LatestBenchmark.DilatedVoxel,
            LatestBenchmark.OpacityUdf,
            LatestBenchmark.TwoLevelSheet
        ];

        for (let M = 0; M < 4; ++M)
        {
            const S = Ordered[M];
            document.getElementById(`BenchSurv${M}`).textContent  = `${S.LeafSurvivalPercent.toFixed(M === 3 ? 1 : 0)}%`;
            document.getElementById(`BenchBloat${M}`).textContent = `${S.BlobBloatPercent.toFixed(M === 3 ? 1 : 0)}%`;
            document.getElementById(`BenchRmse${M}`).textContent  = `${S.RmsePercent.toFixed(M === 3 ? 2 : 1)}%`;
        }

        const Cur = Ordered[Settings.SdfMode];
        document.getElementById("HudSurvivalStat").textContent = `${Cur.LeafSurvivalPercent.toFixed(1)}%`;
        document.getElementById("HudBloatStat").textContent    = `${Cur.BlobBloatPercent.toFixed(1)}%`;
        document.getElementById("HudRmseStat").textContent     = `${Cur.RmsePercent.toFixed(2)}%`;
        document.getElementById("HudStepsStat").textContent    = `${Cur.AvgSteps.toFixed(1)}`;
        document.getElementById("SplitCenterLabel").textContent = `② Active SDF: ${ModeLabels[Settings.SdfMode]}`;
    }

    function RefreshDebugRaysWithStats()
    {
        if (!ActiveVol) return;
        const SunDir = ComputeSolarDirection(Settings.SunAzimuthDeg, Settings.SunElevationDeg);
        const TanCone = Math.tan((Settings.SunConeDeg * Math.PI) / 180.0);

        const WireGeom = BuildDebugWireframeGeometry(ActiveVol, CompiledArchetypes, SunDir, Settings.RayOriginGround);
        Renderer.UploadDebugWireframes(WireGeom);

        LatestBenchmark = BenchmarkFoliageShadowAccuracy(ActiveVol, CompiledArchetypes, SunDir, TanCone);
        RefreshHudMetrics();
    }

    function RebuildActivePreset(PresetId, FirstBoot = false, KeepCamera = false)
    {
        Settings.PresetId = PresetId;
        const T0 = performance.now();
        const Spec = ConstructFoliagePreset(PresetId, {
            BarkNoiseAmp: Settings.BarkNoiseAmp,
            BarkNoiseFreq: Settings.BarkNoiseFreq,
            BranchGnarl: Settings.BranchGnarl
        });
        ActiveVol  = CompileFoliageSdfVolumes(Spec, CompiledArchetypes);
        const Geom = BuildFoliageSceneGeometry(Spec);
        Renderer.UploadSceneResources(ActiveVol, Geom);
        const TotalMs = performance.now() - T0 + (FirstBoot ? ArchMs : 0.0);

        if (!KeepCamera)
        {
            // Position default ground diagnostic ray right inside the arbor's cast shadow
            const SunDir = ComputeSolarDirection(Settings.SunAzimuthDeg, Settings.SunElevationDeg);
            const MidY = 0.55 * (ActiveVol.BoundsMin[1] + ActiveVol.BoundsMax[1]);
            Settings.RayOriginGround = [
                -SunDir[0] * (MidY / Math.max(SunDir[1], 0.3)),
                0.015,
                -SunDir[2] * (MidY / Math.max(SunDir[1], 0.3))
            ];
        }

        document.getElementById("CompileTimingLabel").textContent = `${TotalMs.toFixed(0)} ms`;
        document.getElementById("HudCapsuleCount").textContent = `${Spec.Capsules.length}`;
        document.getElementById("HudLeafCount").textContent    = `${Spec.LeafInstances.length}`;
        document.getElementById("HudBrickCount").textContent   = `${ActiveVol.OccupiedBrickBounds.length} / 4096`;
        document.getElementById("HudBladeThick").textContent   = `${(LeafHalfThicknessM * 2000.0).toFixed(1)} mm`;

        if (!KeepCamera)
        {
            ApplyCameraBookmark(PresetId === "avenue-lod" ? "Avenue" : "Canopy");
        }
        RefreshDebugRaysWithStats();
    }

    function SelectLeafArchetypeInspector(ArchIdx)
    {
        Settings.SelectedArchetype = ArchIdx;
        PaintLeafArchetypeInspector(ArchetypeCanvasEl, CompiledArchetypes, ArchIdx);
        const Meta = CompiledArchetypes.ArchetypeStats[ArchIdx];
        document.getElementById("ArchNameLabel").textContent = Meta.Label;
        document.getElementById("ArchAreaLabel").textContent = `${(Meta.AreaFraction * 100.0).toFixed(1)}%`;
        for (let A = 0; A < LeafArchetypeCount; ++A)
        {
            document.getElementById(`ArchTabBtn${A}`).classList.toggle("Active", A === ArchIdx);
        }
    }

    // Bind UI Controls
    document.getElementById("ScenePresetSelect").addEventListener("change", (Ev) => {
        RebuildActivePreset(Ev.target.value, false);
    });

    document.getElementById("CamShadowBtn").addEventListener("click", () => ApplyCameraBookmark("Shadow"));
    document.getElementById("CamCanopyBtn").addEventListener("click", () => ApplyCameraBookmark("Canopy"));
    document.getElementById("CamWallBtn").addEventListener("click",   () => ApplyCameraBookmark("Wall"));
    document.getElementById("CamFarLodBtn").addEventListener("click", () => ApplyCameraBookmark("Avenue"));

    for (let M = 0; M < 4; ++M)
    {
        document.getElementById(`SdfModeBtn${M}`).addEventListener("click", () => {
            Settings.SdfMode = M;
            for (let J = 0; J < 4; ++J)
            {
                document.getElementById(`SdfModeBtn${J}`).classList.toggle("Active", J === M);
            }
            RefreshHudMetrics();
        });
    }

    document.getElementById("DisplayModeSelect").addEventListener("change", (Ev) => {
        Settings.DisplayMode = Number(Ev.target.value);
        document.getElementById("SplitColumnBanner").classList.toggle("Visible", Settings.DisplayMode === 2);
    });

    document.getElementById("DebugCapsulesCheck").addEventListener("change", (Ev) => { Settings.DebugCapsules = Ev.target.checked; });
    document.getElementById("DebugBricksCheck").addEventListener("change",   (Ev) => { Settings.DebugBricks   = Ev.target.checked; });
    document.getElementById("DebugLeavesCheck").addEventListener("change",   (Ev) => { Settings.DebugLeaves   = Ev.target.checked; });
    document.getElementById("DebugRaysCheck").addEventListener("change",     (Ev) => { Settings.DebugRays     = Ev.target.checked; });

    function ScheduleBarkGeometryRebuild()
    {
        if (BarkRebuildTimer) clearTimeout(BarkRebuildTimer);
        BarkRebuildTimer = setTimeout(() => {
            RebuildActivePreset(Settings.PresetId, false, true);
        }, 90);
    }

    document.getElementById("BarkAmpSlider").addEventListener("input", (Ev) => {
        Settings.BarkNoiseAmp = Number(Ev.target.value);
        document.getElementById("BarkAmpReadout").textContent = Settings.BarkNoiseAmp.toFixed(2);
        ScheduleBarkGeometryRebuild();
    });

    document.getElementById("BarkFreqSlider").addEventListener("input", (Ev) => {
        Settings.BarkNoiseFreq = Number(Ev.target.value);
        document.getElementById("BarkFreqReadout").textContent = `${Settings.BarkNoiseFreq.toFixed(1)} m⁻¹`;
        ScheduleBarkGeometryRebuild();
    });

    document.getElementById("BranchGnarlSlider").addEventListener("input", (Ev) => {
        Settings.BranchGnarl = Number(Ev.target.value);
        document.getElementById("BranchGnarlReadout").textContent = Settings.BranchGnarl.toFixed(2);
        ScheduleBarkGeometryRebuild();
    });

    function ScheduleSolarBenchmarkRefresh()
    {
        if (BenchDebounceTimer) clearTimeout(BenchDebounceTimer);
        BenchDebounceTimer = setTimeout(() => {
            RefreshDebugRaysWithStats();
        }, 110);
    }

    document.getElementById("SunAzimuthSlider").addEventListener("input", (Ev) => {
        Settings.SunAzimuthDeg = Number(Ev.target.value);
        document.getElementById("SunAzimuthReadout").textContent = `${Settings.SunAzimuthDeg.toFixed(0)}°`;
        ScheduleSolarBenchmarkRefresh();
    });

    document.getElementById("SunElevationSlider").addEventListener("input", (Ev) => {
        Settings.SunElevationDeg = Number(Ev.target.value);
        document.getElementById("SunElevationReadout").textContent = `${Settings.SunElevationDeg.toFixed(0)}°`;
        ScheduleSolarBenchmarkRefresh();
    });

    document.getElementById("SunConeSlider").addEventListener("input", (Ev) => {
        Settings.SunConeDeg = Number(Ev.target.value);
        document.getElementById("SunConeReadout").textContent = `${Settings.SunConeDeg.toFixed(2)}°`;
        ScheduleSolarBenchmarkRefresh();
    });

    document.getElementById("LodScaleSlider").addEventListener("input", (Ev) => {
        Settings.LodScale = Number(Ev.target.value);
        document.getElementById("LodScaleReadout").textContent = `${Settings.LodScale.toFixed(2)}×`;
    });

    document.getElementById("AnimateSunBtn").addEventListener("click", (Ev) => {
        Settings.OrbitSunActive = !Settings.OrbitSunActive;
        Ev.currentTarget.classList.toggle("Active", Settings.OrbitSunActive);
    });

    for (let A = 0; A < LeafArchetypeCount; ++A)
    {
        document.getElementById(`ArchTabBtn${A}`).addEventListener("click", () => {
            SelectLeafArchetypeInspector(A);
        });
    }

    // Interactive Viewport Orbit / Pan / Zoom + Alt+Click 3D Shadow-Ray Placement
    let DragMode = 0; // 0 = none, 1 = orbit, 2 = pan
    let LastPtrX = 0, LastPtrY = 0;

    CanvasEl.addEventListener("contextmenu", (Ev) => Ev.preventDefault());

    CanvasEl.addEventListener("pointerdown", (Ev) => {
        if (Ev.altKey && Ev.button === 0)
        {
            // Intersect camera ray with courtyard plane y = 0 to place 3D diagnostic shadow ray!
            const Rect = CanvasEl.getBoundingClientRect();
            const NdcX = ((Ev.clientX - Rect.left) / Math.max(Rect.width, 1)) * 2.0 - 1.0;
            const NdcY = 1.0 - ((Ev.clientY - Rect.top) / Math.max(Rect.height, 1)) * 2.0;

            const Eye = GetCameraEye();
            const Fwd = Normalize3(Subtract3(Settings.CamTarget, Eye));
            const Right = Normalize3(Cross3(Fwd, [0, 1, 0]));
            const Up = Cross3(Right, Fwd);
            const TanHalfFov = Math.tan((46.0 * Math.PI / 180.0) * 0.5);
            const Aspect = Rect.width / Math.max(Rect.height, 1);

            const Rd = Normalize3([
                Fwd[0] + Right[0] * NdcX * Aspect * TanHalfFov + Up[0] * NdcY * TanHalfFov,
                Fwd[1] + Right[1] * NdcX * Aspect * TanHalfFov + Up[1] * NdcY * TanHalfFov,
                Fwd[2] + Right[2] * NdcX * Aspect * TanHalfFov + Up[2] * TanHalfFov * NdcY
            ]);
            if (Math.abs(Rd[1]) > 1e-4)
            {
                const TFloor = (0.015 - Eye[1]) / Rd[1];
                if (TFloor > 0.1 && TFloor < 40.0)
                {
                    Settings.RayOriginGround = [Eye[0] + Rd[0] * TFloor, 0.015, Eye[2] + Rd[2] * TFloor];
                    Settings.DebugRays = true;
                    document.getElementById("DebugRaysCheck").checked = true;
                    RefreshDebugRaysWithStats();
                }
            }
            return;
        }

        CanvasEl.setPointerCapture(Ev.pointerId);
        DragMode = (Ev.button === 2 || Ev.shiftKey) ? 2 : 1;
        LastPtrX = Ev.clientX;
        LastPtrY = Ev.clientY;
    });

    CanvasEl.addEventListener("pointermove", (Ev) => {
        if (DragMode === 0) return;
        const Dx = Ev.clientX - LastPtrX;
        const Dy = Ev.clientY - LastPtrY;
        LastPtrX = Ev.clientX;
        LastPtrY = Ev.clientY;

        if (DragMode === 1)
        {
            Settings.CamYaw   -= Dx * 0.0065;
            Settings.CamPitch  = Math.max(0.05, Math.min(1.46, Settings.CamPitch + Dy * 0.0065));
        }
        else if (DragMode === 2)
        {
            const Eye = GetCameraEye();
            const Fwd = Normalize3(Subtract3(Settings.CamTarget, Eye));
            const Right = Normalize3(Cross3(Fwd, [0, 1, 0]));
            const Up = Cross3(Right, Fwd);
            const PanSpeed = Settings.CamDist * 0.0014;
            Settings.CamTarget[0] += (-Right[0] * Dx + Up[0] * Dy) * PanSpeed;
            Settings.CamTarget[1] = Math.max(0.05, Settings.CamTarget[1] + (-Right[1] * Dx + Up[1] * Dy) * PanSpeed);
            Settings.CamTarget[2] += (-Right[2] * Dx + Up[2] * Dy) * PanSpeed;
        }
    });

    const EndDrag = () => { DragMode = 0; };
    CanvasEl.addEventListener("pointerup", EndDrag);
    CanvasEl.addEventListener("pointercancel", EndDrag);

    CanvasEl.addEventListener("wheel", (Ev) => {
        Ev.preventDefault();
        const ZoomFactor = Math.exp(Ev.deltaY * 0.0011);
        Settings.CamDist = Math.max(1.05, Math.min(26.0, Settings.CamDist * ZoomFactor));
    }, { passive: false });

    // Initial Scene + 2D Archetype Inspector Boot
    SelectLeafArchetypeInspector(0);
    RebuildActivePreset("full-arbor", true);

    const UniformFloats = new Float32Array(48);
    let PrevTimeMs = performance.now();

    function AnimationTick(NowMs)
    {
        const DtSec = Math.min(0.1, (NowMs - PrevTimeMs) * 0.001);
        PrevTimeMs = NowMs;

        if (Settings.OrbitSunActive)
        {
            Settings.SunAzimuthDeg += DtSec * 22.0;
            if (Settings.SunAzimuthDeg > 180.0) Settings.SunAzimuthDeg -= 360.0;
            document.getElementById("SunAzimuthSlider").value = Settings.SunAzimuthDeg.toFixed(0);
            document.getElementById("SunAzimuthReadout").textContent = `${Settings.SunAzimuthDeg.toFixed(0)}°`;
            ScheduleSolarBenchmarkRefresh();
        }

        const Dpr = Math.min(window.devicePixelRatio || 1, 2.0);
        const CssW = Math.max(1, CanvasEl.clientWidth);
        const CssH = Math.max(1, CanvasEl.clientHeight);
        const PixW = Math.round(CssW * Dpr);
        const PixH = Math.round(CssH * Dpr);
        if (CanvasEl.width !== PixW || CanvasEl.height !== PixH)
        {
            CanvasEl.width  = PixW;
            CanvasEl.height = PixH;
        }

        if (ActiveVol)
        {
            const Eye = GetCameraEye();
            const View = BuildLookAtTransform(Eye, Settings.CamTarget, [0.0, 1.0, 0.0]);
            const FovRad = (46.0 * Math.PI) / 180.0;
            const Aspect = PixW / Math.max(PixH, 1);
            const Proj = Renderer.GpuFlavor === "webgpu"
                ? BuildPerspectiveZeroToOne(FovRad, Aspect, 0.08, 65.0)
                : BuildPerspectiveNegOneToOne(FovRad, Aspect, 0.08, 65.0);
            const ViewProj = MultiplyTransforms4x4(Proj, View);

            const SunDir  = ComputeSolarDirection(Settings.SunAzimuthDeg, Settings.SunElevationDeg);
            const TanCone = Math.tan((Settings.SunConeDeg * Math.PI) / 180.0);
            const DebugMask =
                (Settings.DebugBricks   ? 1 : 0) |
                (Settings.DebugLeaves   ? 2 : 0) |
                (Settings.DebugRays     ? 4 : 0) |
                (Settings.DebugCapsules ? 8 : 0);

            UniformFloats.set(ViewProj, 0);
            UniformFloats[16] = Eye[0];
            UniformFloats[17] = Eye[1];
            UniformFloats[18] = Eye[2];
            UniformFloats[19] = TanCone;

            UniformFloats[20] = SunDir[0];
            UniformFloats[21] = SunDir[1];
            UniformFloats[22] = SunDir[2];
            UniformFloats[23] = Settings.SdfMode;

            UniformFloats[24] = ActiveVol.BoundsMin[0];
            UniformFloats[25] = ActiveVol.BoundsMin[1];
            UniformFloats[26] = ActiveVol.BoundsMin[2];
            UniformFloats[27] = LeafHalfThicknessM;

            UniformFloats[28] = ActiveVol.BoundsMax[0];
            UniformFloats[29] = ActiveVol.BoundsMax[1];
            UniformFloats[30] = ActiveVol.BoundsMax[2];
            UniformFloats[31] = ActiveVol.VoxelDiag;

            UniformFloats[32] = PixW;
            UniformFloats[33] = PixH;
            UniformFloats[34] = Settings.DisplayMode;
            UniformFloats[35] = 0.5;

            UniformFloats[36] = ActiveVol.SceneSpec.Capsules.length;
            UniformFloats[37] = ActiveVol.SceneSpec.LeafInstances.length;
            UniformFloats[38] = Settings.LodScale;
            UniformFloats[39] = DebugMask;

            UniformFloats[40] = Settings.BarkNoiseAmp;
            UniformFloats[41] = Settings.BarkNoiseFreq;
            UniformFloats[42] = Settings.BranchGnarl;
            UniformFloats[43] = 0.0;

            Renderer.RenderViewport(UniformFloats, DebugMask);
        }

        requestAnimationFrame(AnimationTick);
    }

    requestAnimationFrame(AnimationTick);
}

LaunchThinLeafSdfWorkbench();
