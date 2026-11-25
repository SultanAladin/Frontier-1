// Experimental/LoftedVehicleSdfWorkbench/LoftedVehicleSdfWorkbench.js
// 🧩 Interactive WebGPU (+ Automatic WebGL2 Fallback) Workbench comparing a Bi-Directional
//    Lofted Surface-Curve SDF (M Transverse Ribs x K Longitudinal Rails + 4 Revolved Wheel Lofts)
//    against Uniform 3D Voxel Grid SDFs (16^3, 32^3, 64^3) on the 25,068-triangle ControlVehicle.

import {
    QuadPatchResolutionPresets,
    LoftResolutionPresets,
    VoxelResolutionPresets,
    EncodeFloat16Bits,
    ParseFct1VehicleBinary,
    AssembleCompleteControlVehicle,
    BuildTriangleBvhAccelerator,
    CompileQuadPatchVehicleSdf,
    CompileLoftedVehicleSdf,
    CompileUniformVoxelVehicleSdf,
    BenchmarkQuadPatchVsVoxelSdf,
    BenchmarkLoftVsVoxelSdf
} from "./LoftedVehicleSdfCompiler.js";

import {
    ShowroomVertexBytes,
    CageLineVertexBytes,
    BuildShowroomSceneBuffers,
    BuildCutawayPlaneBuffers,
    BuildQuadPatchCageBuffers,
    BuildLoftCurveCageBuffers,
    BuildVoxelCageBuffers
} from "./VehicleShowroomBuilder.js";

import {
    ShowroomSurfaceWgsl,
    RayMarchIsoSurfaceWgsl,
    CageOverlayWgsl,
    ShowroomSurfaceGlslVert,
    ShowroomSurfaceGlslFrag,
    RayMarchIsoSurfaceGlslVert,
    RayMarchIsoSurfaceGlslFrag,
    CageOverlayGlslVert,
    CageOverlayGlslFrag
} from "./LoftedVehicleShaders.js";

const CameraBookmarks = Object.freeze({
    Hero:      { Target: [ 0.00, 0.66,  0.10], Yaw:  0.64, Pitch: 0.27, Dist: 5.35 },
    NoseQuads: { Target: [ 0.00, 0.72,  2.22], Yaw:  0.34, Pitch: 0.24, Dist: 2.65 },
    Fender:    { Target: [-0.82, 0.60,  1.58], Yaw: -0.94, Pitch: 0.22, Dist: 2.35 },
    Profile:   { Target: [ 0.00, 0.66,  0.05], Yaw: -1.52, Pitch: 0.14, Dist: 5.75 },
    Shadow:    { Target: [-0.85, 0.52, -0.55], Yaw:  0.78, Pitch: 0.46, Dist: 6.40 },
    TopRibs:   { Target: [ 0.00, 0.65,  0.00], Yaw:  0.32, Pitch: 1.22, Dist: 6.10 }
});

//============================================================================================================================================
//                                  1. PURE 4x4 COLUMN-MAJOR CAMERA TRANSFORM & INVERSE MATH
//============================================================================================================================================

function Clamp(X, Lo, Hi)
{
    return X < Lo ? Lo : (X > Hi ? Hi : X);
}

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

function InvertTransform4x4(M)
{
    const Inv = new Float32Array(16);
    Inv[0]  =  M[5]*M[10]*M[15] - M[5]*M[11]*M[14] - M[9]*M[6]*M[15] + M[9]*M[7]*M[14] + M[13]*M[6]*M[11] - M[13]*M[7]*M[10];
    Inv[4]  = -M[4]*M[10]*M[15] + M[4]*M[11]*M[14] + M[8]*M[6]*M[15] - M[8]*M[7]*M[14] - M[12]*M[6]*M[11] + M[12]*M[7]*M[10];
    Inv[8]  =  M[4]*M[9]*M[15]  - M[4]*M[11]*M[13] - M[8]*M[5]*M[15] + M[8]*M[7]*M[13] + M[12]*M[5]*M[11] - M[12]*M[7]*M[9];
    Inv[12] = -M[4]*M[9]*M[14]  + M[4]*M[10]*M[13] + M[8]*M[5]*M[14] - M[8]*M[6]*M[13] - M[12]*M[5]*M[10] + M[12]*M[6]*M[9];

    Inv[1]  = -M[1]*M[10]*M[15] + M[1]*M[11]*M[14] + M[9]*M[2]*M[15] - M[9]*M[3]*M[14] - M[13]*M[2]*M[11] + M[13]*M[3]*M[10];
    Inv[5]  =  M[0]*M[10]*M[15] - M[0]*M[11]*M[14] - M[8]*M[2]*M[15] + M[8]*M[3]*M[14] + M[12]*M[2]*M[11] - M[12]*M[3]*M[10];
    Inv[9]  = -M[0]*M[9]*M[15]  + M[0]*M[11]*M[13] + M[8]*M[1]*M[15] - M[8]*M[3]*M[13] - M[12]*M[1]*M[11] + M[12]*M[3]*M[9];
    Inv[13] =  M[0]*M[9]*M[14]  - M[0]*M[10]*M[13] - M[8]*M[1]*M[14] + M[8]*M[2]*M[13] + M[12]*M[1]*M[10] - M[12]*M[2]*M[9];

    Inv[2]  =  M[1]*M[6]*M[15] - M[1]*M[7]*M[14] - M[5]*M[2]*M[15] + M[5]*M[3]*M[14] + M[13]*M[2]*M[7] - M[13]*M[3]*M[6];
    Inv[6]  = -M[0]*M[6]*M[15] + M[0]*M[7]*M[14] + M[4]*M[2]*M[15] - M[4]*M[3]*M[14] - M[12]*M[2]*M[7] + M[12]*M[3]*M[6];
    Inv[10] =  M[0]*M[5]*M[15] - M[0]*M[7]*M[13] - M[4]*M[1]*M[15] + M[4]*M[3]*M[13] + M[12]*M[1]*M[7] - M[12]*M[3]*M[5];
    Inv[14] = -M[0]*M[5]*M[14] + M[0]*M[6]*M[13] + M[4]*M[1]*M[14] - M[4]*M[2]*M[13] - M[12]*M[1]*M[6] + M[12]*M[2]*M[5];

    Inv[3]  = -M[1]*M[6]*M[11] + M[1]*M[7]*M[10] + M[5]*M[2]*M[11] - M[5]*M[3]*M[10] - M[9]*M[2]*M[7] + M[9]*M[3]*M[6];
    Inv[7]  =  M[0]*M[6]*M[11] - M[0]*M[7]*M[10] - M[4]*M[2]*M[11] + M[4]*M[3]*M[10] + M[8]*M[2]*M[7] - M[8]*M[3]*M[6];
    Inv[11] = -M[0]*M[5]*M[11] + M[0]*M[7]*M[9]  + M[4]*M[1]*M[11] - M[4]*M[3]*M[9]  - M[8]*M[1]*M[7] + M[8]*M[3]*M[5];
    Inv[15] =  M[0]*M[5]*M[10] - M[0]*M[6]*M[9]  - M[4]*M[1]*M[10] + M[4]*M[2]*M[9]  + M[8]*M[1]*M[6] - M[8]*M[2]*M[5];

    let Det = M[0]*Inv[0] + M[1]*Inv[4] + M[2]*Inv[8] + M[3]*Inv[12];
    if (Math.abs(Det) < 1e-12) return Inv;
    Det = 1.0 / Det;
    for (let I = 0; I < 16; ++I) Inv[I] *= Det;
    return Inv;
}

// Packs M x K body Hermite knots + 1 row of WheelSamples profile into a RailsK x (RibsM + 1) rgba16float 2D image
function PackLoftRgba16Texture(LoftSdf)
{
    const TexW = LoftSdf.RailsK;
    const TexH = LoftSdf.RibsM + 1;
    const Out = new Uint16Array(TexW * TexH * 4);
    Out.set(LoftSdf.HermiteKnotsRgba16, 0);

    const WheelRowStart = LoftSdf.RibsM * TexW * 4;
    const Wf = LoftSdf.WheelGpuFloats;
    for (let S = 0; S < LoftSdf.WheelSamples; ++S)
    {
        const Dst = WheelRowStart + S * 4;
        Out[Dst + 0] = EncodeFloat16Bits(Wf[S * 4 + 0]);
        Out[Dst + 1] = EncodeFloat16Bits(Wf[S * 4 + 1]);
        Out[Dst + 2] = EncodeFloat16Bits(Wf[S * 4 + 2]);
        Out[Dst + 3] = 0;
    }
    return { Texels: Out, Width: TexW, Height: TexH };
}

// Packs 6 x (KnotSpan x KnotSpan) C² 4-corner patch knots + 1 wheel profile row into a 2D rgba16float image
function PackQuadPatchRgba16Texture(QuadPatchSdf)
{
    const KSpan = QuadPatchSdf.KnotSpan;
    const TexW  = Math.max(KSpan, QuadPatchSdf.WheelSamples);
    const TexH  = 6 * KSpan + 1;
    const Out   = new Uint16Array(TexW * TexH * 4);
    const Src   = QuadPatchSdf.HermiteKnotsRgba16;

    const TotalChartRows = 6 * KSpan;
    for (let Row = 0; Row < TotalChartRows; ++Row)
    {
        const SrcOff = Row * KSpan * 4;
        const DstOff = Row * TexW * 4;
        Out.set(Src.subarray(SrcOff, SrcOff + KSpan * 4), DstOff);
    }

    const WheelRowStart = TotalChartRows * TexW * 4;
    const Wf = QuadPatchSdf.WheelGpuFloats;
    for (let S = 0; S < QuadPatchSdf.WheelSamples; ++S)
    {
        const Dst = WheelRowStart + S * 4;
        Out[Dst + 0] = EncodeFloat16Bits(Wf[S * 4 + 0]);
        Out[Dst + 1] = EncodeFloat16Bits(Wf[S * 4 + 1]);
        Out[Dst + 2] = EncodeFloat16Bits(Wf[S * 4 + 2]);
        Out[Dst + 3] = 0;
    }
    return { Texels: Out, Width: TexW, Height: TexH };
}

//============================================================================================================================================
//                                            2. WEBGPU RENDERER IMPLEMENTATION
//============================================================================================================================================

async function TryInitializeWebGpuRenderer(Canvas, ShowroomBuffers)
{
    if (!navigator.gpu) return null;
    const Adapter = await navigator.gpu.requestAdapter();
    if (!Adapter) return null;
    const Device = await Adapter.requestDevice();
    const GpuCtx = Canvas.getContext("webgpu");
    if (!GpuCtx) return null;

    const Format = navigator.gpu.getPreferredCanvasFormat();
    GpuCtx.configure({ device: Device, format: Format, alphaMode: "opaque" });

    function Align16(ByteLen)
    {
        return Math.max(16, (ByteLen + 15) & ~15);
    }

    function UploadBuffer(TypedArr, UsageBits)
    {
        const Padded = Align16(TypedArr.byteLength);
        const Buf = Device.createBuffer({ size: Padded, usage: UsageBits | GPUBufferUsage.COPY_DST });
        if (Padded === TypedArr.byteLength)
        {
            Device.queue.writeBuffer(Buf, 0, TypedArr);
        }
        else
        {
            const Tmp = new Uint8Array(Padded);
            Tmp.set(new Uint8Array(TypedArr.buffer, TypedArr.byteOffset, TypedArr.byteLength));
            Device.queue.writeBuffer(Buf, 0, Tmp);
        }
        return Buf;
    }

    const UniformBuffer = Device.createBuffer({
        size: 272,
        usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST
    });

    const LinearSampler = Device.createSampler({
        magFilter: "linear",
        minFilter: "linear",
        addressModeU: "clamp-to-edge",
        addressModeV: "clamp-to-edge",
        addressModeW: "clamp-to-edge"
    });

    const SharedLayout = Device.createBindGroupLayout({
        entries: [
            { binding: 0, visibility: GPUShaderStage.VERTEX | GPUShaderStage.FRAGMENT, buffer: { type: "uniform" } },
            { binding: 1, visibility: GPUShaderStage.FRAGMENT, texture: { sampleType: "float", viewDimension: "2d" } },
            { binding: 2, visibility: GPUShaderStage.FRAGMENT, texture: { sampleType: "float", viewDimension: "3d" } },
            { binding: 3, visibility: GPUShaderStage.FRAGMENT, sampler: { type: "filtering" } },
            { binding: 4, visibility: GPUShaderStage.FRAGMENT, texture: { sampleType: "float", viewDimension: "2d" } }
        ]
    });

    const LineLayout = Device.createBindGroupLayout({
        entries: [
            { binding: 0, visibility: GPUShaderStage.VERTEX, buffer: { type: "uniform" } }
        ]
    });

    const ShowroomShader = Device.createShaderModule({ code: ShowroomSurfaceWgsl });
    const IsoShader      = Device.createShaderModule({ code: RayMarchIsoSurfaceWgsl });
    const CageShader     = Device.createShaderModule({ code: CageOverlayWgsl });

    for (const ShaderObj of [ShowroomShader, IsoShader, CageShader])
    {
        const Comp = await ShaderObj.getCompilationInfo();
        for (const Msg of Comp.messages)
        {
            if (Msg.type === "error")
            {
                throw new Error(`WGSL error (${Msg.lineNum}:${Msg.linePos}): ${Msg.message}`);
            }
        }
    }

    const VertexBufferAttributes = [
        { shaderLocation: 0, offset: 0,  format: "float32x3" },
        { shaderLocation: 1, offset: 12, format: "float32x3" },
        { shaderLocation: 2, offset: 24, format: "float32x3" },
        { shaderLocation: 3, offset: 36, format: "float32x2" }
    ];

    const SharedPipeLayout = Device.createPipelineLayout({ bindGroupLayouts: [SharedLayout] });
    const LinePipeLayout   = Device.createPipelineLayout({ bindGroupLayouts: [LineLayout] });

    const ShowroomOpaquePipe = Device.createRenderPipeline({
        layout: SharedPipeLayout,
        vertex: {
            module: ShowroomShader,
            entryPoint: "VsMain",
            buffers: [{ arrayStride: ShowroomVertexBytes, attributes: VertexBufferAttributes }]
        },
        fragment: {
            module: ShowroomShader,
            entryPoint: "FsMain",
            targets: [{ format: Format }]
        },
        primitive: { topology: "triangle-list", cullMode: "none" },
        depthStencil: { format: "depth24plus", depthWriteEnabled: true, depthCompare: "less-equal" }
    });

    const ShowroomAlphaPipe = Device.createRenderPipeline({
        layout: SharedPipeLayout,
        vertex: {
            module: ShowroomShader,
            entryPoint: "VsMain",
            buffers: [{ arrayStride: ShowroomVertexBytes, attributes: VertexBufferAttributes }]
        },
        fragment: {
            module: ShowroomShader,
            entryPoint: "FsMain",
            targets: [{
                format: Format,
                blend: {
                    color: { srcFactor: "src-alpha", dstFactor: "one-minus-src-alpha", operation: "add" },
                    alpha: { srcFactor: "one", dstFactor: "one-minus-src-alpha", operation: "add" }
                }
            }]
        },
        primitive: { topology: "triangle-list", cullMode: "none" },
        depthStencil: { format: "depth24plus", depthWriteEnabled: false, depthCompare: "less-equal" }
    });

    const IsoSurfacePipe = Device.createRenderPipeline({
        layout: SharedPipeLayout,
        vertex: { module: IsoShader, entryPoint: "VsMain" },
        fragment: {
            module: IsoShader,
            entryPoint: "FsMain",
            targets: [{ format: Format }]
        },
        primitive: { topology: "triangle-list", cullMode: "none" },
        depthStencil: { format: "depth24plus", depthWriteEnabled: true, depthCompare: "less-equal" }
    });

    const CageLinePipe = Device.createRenderPipeline({
        layout: LinePipeLayout,
        vertex: {
            module: CageShader,
            entryPoint: "VsMain",
            buffers: [{
                arrayStride: CageLineVertexBytes,
                attributes: [
                    { shaderLocation: 0, offset: 0,  format: "float32x3" },
                    { shaderLocation: 1, offset: 12, format: "float32x4" }
                ]
            }]
        },
        fragment: {
            module: CageShader,
            entryPoint: "FsMain",
            targets: [{
                format: Format,
                blend: {
                    color: { srcFactor: "src-alpha", dstFactor: "one-minus-src-alpha", operation: "add" },
                    alpha: { srcFactor: "one", dstFactor: "one-minus-src-alpha", operation: "add" }
                }
            }]
        },
        primitive: { topology: "line-list" },
        depthStencil: { format: "depth24plus", depthWriteEnabled: false, depthCompare: "less-equal" }
    });

    const LineGroup = Device.createBindGroup({
        layout: LineLayout,
        entries: [{ binding: 0, resource: { buffer: UniformBuffer } }]
    });

    const ShowroomVertBuf = UploadBuffer(ShowroomBuffers.Vertices, GPUBufferUsage.VERTEX);
    const ShowroomIdxBuf  = UploadBuffer(ShowroomBuffers.Indices,  GPUBufferUsage.INDEX);

    let CutPlaneVertBuf = null;
    let CutPlaneIdxBuf  = null;
    let CutPlaneIndexCount = 0;

    let LoftTexObj  = null;
    let VoxelTexObj = null;
    let QuadTexObj  = null;
    let SharedGroup = null;

    let QuadCageBuf = null;
    let QuadCageVertCount = 0;
    let LoftCageBuf = null;
    let LoftCageVertCount = 0;
    let VoxelCageBuf = null;
    let VoxelCageVertCount = 0;

    let DepthTex = null;
    let DepthW = 0, DepthH = 0;

    function RebuildSharedGroup()
    {
        if (!LoftTexObj || !VoxelTexObj || !QuadTexObj) return;
        SharedGroup = Device.createBindGroup({
            layout: SharedLayout,
            entries: [
                { binding: 0, resource: { buffer: UniformBuffer } },
                { binding: 1, resource: LoftTexObj.createView({ dimension: "2d" }) },
                { binding: 2, resource: VoxelTexObj.createView({ dimension: "3d" }) },
                { binding: 3, resource: LinearSampler },
                { binding: 4, resource: QuadTexObj.createView({ dimension: "2d" }) }
            ]
        });
    }

    return {
        RuntimeLabel: "WebGPU",
        ZeroToOneClip: true,

        SyncQuadPatchResources(QuadPatchSdf, CageBuffers)
        {
            if (QuadTexObj) QuadTexObj.destroy();
            const Packed = PackQuadPatchRgba16Texture(QuadPatchSdf);
            QuadTexObj = Device.createTexture({
                size: [Packed.Width, Packed.Height, 1],
                dimension: "2d",
                format: "rgba16float",
                usage: GPUTextureUsage.TEXTURE_BINDING | GPUTextureUsage.COPY_DST
            });
            Device.queue.writeTexture(
                { texture: QuadTexObj },
                Packed.Texels,
                { bytesPerRow: Packed.Width * 8, rowsPerImage: Packed.Height },
                [Packed.Width, Packed.Height, 1]
            );

            if (QuadCageBuf) QuadCageBuf.destroy();
            QuadCageBuf = UploadBuffer(CageBuffers.Vertices, GPUBufferUsage.VERTEX);
            QuadCageVertCount = CageBuffers.VertexCount;
            RebuildSharedGroup();
        },

        SyncLoftResources(LoftSdf, CageBuffers)
        {
            if (LoftTexObj) LoftTexObj.destroy();
            const Packed = PackLoftRgba16Texture(LoftSdf);
            LoftTexObj = Device.createTexture({
                size: [Packed.Width, Packed.Height, 1],
                dimension: "2d",
                format: "rgba16float",
                usage: GPUTextureUsage.TEXTURE_BINDING | GPUTextureUsage.COPY_DST
            });
            Device.queue.writeTexture(
                { texture: LoftTexObj },
                Packed.Texels,
                { bytesPerRow: Packed.Width * 8, rowsPerImage: Packed.Height },
                [Packed.Width, Packed.Height, 1]
            );

            if (LoftCageBuf) LoftCageBuf.destroy();
            LoftCageBuf = UploadBuffer(CageBuffers.Vertices, GPUBufferUsage.VERTEX);
            LoftCageVertCount = CageBuffers.VertexCount;
            RebuildSharedGroup();
        },

        SyncVoxelResources(VoxelSdf, CageBuffers)
        {
            if (VoxelTexObj) VoxelTexObj.destroy();
            const Res = VoxelSdf.Res;
            VoxelTexObj = Device.createTexture({
                size: [Res, Res, Res],
                dimension: "3d",
                format: "rgba16float",
                usage: GPUTextureUsage.TEXTURE_BINDING | GPUTextureUsage.COPY_DST
            });
            Device.queue.writeTexture(
                { texture: VoxelTexObj },
                VoxelSdf.Rgba16f,
                { bytesPerRow: Res * 8, rowsPerImage: Res },
                [Res, Res, Res]
            );

            if (VoxelCageBuf) VoxelCageBuf.destroy();
            VoxelCageBuf = UploadBuffer(CageBuffers.Vertices, GPUBufferUsage.VERTEX);
            VoxelCageVertCount = CageBuffers.VertexCount;
            RebuildSharedGroup();
        },

        SyncCutawayPlane(PlaneBuffers)
        {
            if (CutPlaneVertBuf) CutPlaneVertBuf.destroy();
            if (CutPlaneIdxBuf)  CutPlaneIdxBuf.destroy();
            CutPlaneVertBuf = UploadBuffer(PlaneBuffers.Vertices, GPUBufferUsage.VERTEX);
            CutPlaneIdxBuf  = UploadBuffer(PlaneBuffers.Indices,  GPUBufferUsage.INDEX);
            CutPlaneIndexCount = PlaneBuffers.IndexCount;
        },

        RenderTick(UniformFloats, Opts)
        {
            if (!SharedGroup) return;
            const W = Canvas.width;
            const H = Canvas.height;
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

            const Cmd = Device.createCommandEncoder();
            const Enc = Cmd.beginRenderPass({
                colorAttachments: [{
                    view: GpuCtx.getCurrentTexture().createView(),
                    clearValue: { r: 0.038, g: 0.056, b: 0.084, a: 1.0 },
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

            Enc.setPipeline(ShowroomOpaquePipe);
            Enc.setBindGroup(0, SharedGroup);
            Enc.setVertexBuffer(0, ShowroomVertBuf);
            Enc.setIndexBuffer(ShowroomIdxBuf, "uint32");

            if (Opts.DisplayMode === 1)
            {
                // Showroom floor & backdrop only, followed by sphere-traced d(x) = 0 SDF car!
                Enc.drawIndexed(ShowroomBuffers.EnvIndexCount, 1, ShowroomBuffers.EnvIndexStart, 0, 0);
                Enc.setPipeline(IsoSurfacePipe);
                Enc.setBindGroup(0, SharedGroup);
                Enc.draw(3, 1, 0, 0);
            }
            else
            {
                Enc.drawIndexed(ShowroomBuffers.EnvIndexCount + ShowroomBuffers.VehicleIndexCount, 1, 0, 0, 0);
            }

            if (Opts.ShowLoftCage || Opts.ShowVoxelCage)
            {
                Enc.setPipeline(CageLinePipe);
                Enc.setBindGroup(0, LineGroup);

                if (Opts.SplitCompare && W >= 4)
                {
                    const MidW = Math.max(1, Math.min(W - 1, Math.floor(W * 0.5)));
                    if (Opts.ShowLoftCage && QuadCageVertCount > 0)
                    {
                        Enc.setScissorRect(0, 0, MidW, H);
                        Enc.setVertexBuffer(0, QuadCageBuf);
                        Enc.draw(QuadCageVertCount, 1, 0, 0);
                    }
                    Enc.setScissorRect(MidW, 0, Math.max(1, W - MidW), H);
                    if (Opts.SplitRightMode === 1 && Opts.ShowLoftCage && LoftCageVertCount > 0)
                    {
                        Enc.setVertexBuffer(0, LoftCageBuf);
                        Enc.draw(LoftCageVertCount, 1, 0, 0);
                    }
                    if (Opts.ShowVoxelCage && VoxelCageVertCount > 0)
                    {
                        Enc.setVertexBuffer(0, VoxelCageBuf);
                        Enc.draw(VoxelCageVertCount, 1, 0, 0);
                    }
                    Enc.setScissorRect(0, 0, W, H);
                }
                else
                {
                    if (Opts.ShowLoftCage)
                    {
                        if (Opts.ActiveParadigm === 1 && LoftCageVertCount > 0)
                        {
                            Enc.setVertexBuffer(0, LoftCageBuf);
                            Enc.draw(LoftCageVertCount, 1, 0, 0);
                        }
                        else if (QuadCageVertCount > 0)
                        {
                            Enc.setVertexBuffer(0, QuadCageBuf);
                            Enc.draw(QuadCageVertCount, 1, 0, 0);
                        }
                    }
                    if (Opts.ShowVoxelCage && VoxelCageVertCount > 0)
                    {
                        Enc.setVertexBuffer(0, VoxelCageBuf);
                        Enc.draw(VoxelCageVertCount, 1, 0, 0);
                    }
                }
            }

            if (Opts.DisplayMode === 2 && CutPlaneVertBuf && CutPlaneIndexCount > 0)
            {
                Enc.setPipeline(ShowroomAlphaPipe);
                Enc.setBindGroup(0, SharedGroup);
                Enc.setVertexBuffer(0, CutPlaneVertBuf);
                Enc.setIndexBuffer(CutPlaneIdxBuf, "uint32");
                Enc.drawIndexed(CutPlaneIndexCount, 1, 0, 0, 0);
            }

            Enc.end();
            Device.queue.submit([Cmd.finish()]);
        }
    };
}

//============================================================================================================================================
//                                            3. WEBGL 2.0 FALLBACK RENDERER
//============================================================================================================================================

function InitializeWebGl2Renderer(Canvas, ShowroomBuffers)
{
    const Gl = Canvas.getContext("webgl2", { antialias: true, depth: true, alpha: false, preserveDrawingBuffer: true });
    if (!Gl)
    {
        throw new Error("Neither WebGPU nor WebGL2 is supported by this browser.");
    }

    function CompileGlProgram(VsSrc, FsSrc)
    {
        const Vs = Gl.createShader(Gl.VERTEX_SHADER);
        Gl.shaderSource(Vs, VsSrc);
        Gl.compileShader(Vs);
        if (!Gl.getShaderParameter(Vs, Gl.COMPILE_STATUS))
        {
            throw new Error(`GLSL VS compile error: ${Gl.getShaderInfoLog(Vs)}`);
        }

        const Fs = Gl.createShader(Gl.FRAGMENT_SHADER);
        Gl.shaderSource(Fs, FsSrc);
        Gl.compileShader(Fs);
        if (!Gl.getShaderParameter(Fs, Gl.COMPILE_STATUS))
        {
            throw new Error(`GLSL FS compile error: ${Gl.getShaderInfoLog(Fs)}`);
        }

        const Prog = Gl.createProgram();
        Gl.attachShader(Prog, Vs);
        Gl.attachShader(Prog, Fs);
        Gl.linkProgram(Prog);
        if (!Gl.getProgramParameter(Prog, Gl.LINK_STATUS))
        {
            throw new Error(`GLSL program link error: ${Gl.getProgramInfoLog(Prog)}`);
        }
        return Prog;
    }

    const ShowroomProg = CompileGlProgram(ShowroomSurfaceGlslVert, ShowroomSurfaceGlslFrag);
    const IsoProg      = CompileGlProgram(RayMarchIsoSurfaceGlslVert, RayMarchIsoSurfaceGlslFrag);
    const CageProg     = CompileGlProgram(CageOverlayGlslVert, CageOverlayGlslFrag);

    function LookupSharedUniforms(Prog)
    {
        return {
            ViewProj:    Gl.getUniformLocation(Prog, "uViewProj"),
            InvViewProj: Gl.getUniformLocation(Prog, "uInvViewProj"),
            Params:      Gl.getUniformLocation(Prog, "uParams[0]"),
            LoftTex:     Gl.getUniformLocation(Prog, "uLoftTex"),
            VoxelTex:    Gl.getUniformLocation(Prog, "uVoxelTex"),
            QuadTex:     Gl.getUniformLocation(Prog, "uQuadTex")
        };
    }

    const ShowroomUni = LookupSharedUniforms(ShowroomProg);
    const IsoUni      = LookupSharedUniforms(IsoProg);
    const CageViewProjLoc = Gl.getUniformLocation(CageProg, "uViewProj");

    function CreateSurfaceVao(Vertices, Indices)
    {
        const Vao = Gl.createVertexArray();
        Gl.bindVertexArray(Vao);

        const Vbo = Gl.createBuffer();
        Gl.bindBuffer(Gl.ARRAY_BUFFER, Vbo);
        Gl.bufferData(Gl.ARRAY_BUFFER, Vertices, Gl.STATIC_DRAW);

        const Ibo = Gl.createBuffer();
        Gl.bindBuffer(Gl.ELEMENT_ARRAY_BUFFER, Ibo);
        Gl.bufferData(Gl.ELEMENT_ARRAY_BUFFER, Indices, Gl.STATIC_DRAW);

        Gl.enableVertexAttribArray(0);
        Gl.vertexAttribPointer(0, 3, Gl.FLOAT, false, ShowroomVertexBytes, 0);
        Gl.enableVertexAttribArray(1);
        Gl.vertexAttribPointer(1, 3, Gl.FLOAT, false, ShowroomVertexBytes, 12);
        Gl.enableVertexAttribArray(2);
        Gl.vertexAttribPointer(2, 3, Gl.FLOAT, false, ShowroomVertexBytes, 24);
        Gl.enableVertexAttribArray(3);
        Gl.vertexAttribPointer(3, 2, Gl.FLOAT, false, ShowroomVertexBytes, 36);

        Gl.bindVertexArray(null);
        return { Vao, Vbo, Ibo };
    }

    function CreateLineVao(Vertices)
    {
        const Vao = Gl.createVertexArray();
        Gl.bindVertexArray(Vao);

        const Vbo = Gl.createBuffer();
        Gl.bindBuffer(Gl.ARRAY_BUFFER, Vbo);
        Gl.bufferData(Gl.ARRAY_BUFFER, Vertices, Gl.STATIC_DRAW);

        Gl.enableVertexAttribArray(0);
        Gl.vertexAttribPointer(0, 3, Gl.FLOAT, false, CageLineVertexBytes, 0);
        Gl.enableVertexAttribArray(1);
        Gl.vertexAttribPointer(1, 4, Gl.FLOAT, false, CageLineVertexBytes, 12);

        Gl.bindVertexArray(null);
        return { Vao, Vbo };
    }

    const ShowroomGpu = CreateSurfaceVao(ShowroomBuffers.Vertices, ShowroomBuffers.Indices);
    const EmptyIsoVao = Gl.createVertexArray();

    let CutPlaneGpu = null;
    let CutPlaneIndexCount = 0;

    let LoftTexObj  = null;
    let VoxelTexObj = null;
    let QuadTexObj  = null;

    let QuadCageGpu = null;
    let QuadCageVertCount = 0;
    let LoftCageGpu = null;
    let LoftCageVertCount = 0;
    let VoxelCageGpu = null;
    let VoxelCageVertCount = 0;

    Gl.pixelStorei(Gl.UNPACK_ALIGNMENT, 1);

    function ApplySharedUniforms(Prog, Locs, UniformFloats)
    {
        Gl.useProgram(Prog);
        Gl.uniformMatrix4fv(Locs.ViewProj, false, UniformFloats.subarray(0, 16));
        Gl.uniformMatrix4fv(Locs.InvViewProj, false, UniformFloats.subarray(16, 32));
        Gl.uniform4fv(Locs.Params, UniformFloats.subarray(32, 68));
        Gl.uniform1i(Locs.LoftTex, 0);
        Gl.uniform1i(Locs.VoxelTex, 1);
        Gl.uniform1i(Locs.QuadTex, 2);
    }

    return {
        RuntimeLabel: "WebGL2",
        ZeroToOneClip: false,

        SyncQuadPatchResources(QuadPatchSdf, CageBuffers)
        {
            if (QuadTexObj) Gl.deleteTexture(QuadTexObj);
            const Packed = PackQuadPatchRgba16Texture(QuadPatchSdf);
            QuadTexObj = Gl.createTexture();
            Gl.bindTexture(Gl.TEXTURE_2D, QuadTexObj);
            Gl.texParameteri(Gl.TEXTURE_2D, Gl.TEXTURE_MIN_FILTER, Gl.NEAREST);
            Gl.texParameteri(Gl.TEXTURE_2D, Gl.TEXTURE_MAG_FILTER, Gl.NEAREST);
            Gl.texParameteri(Gl.TEXTURE_2D, Gl.TEXTURE_WRAP_S, Gl.CLAMP_TO_EDGE);
            Gl.texParameteri(Gl.TEXTURE_2D, Gl.TEXTURE_WRAP_T, Gl.CLAMP_TO_EDGE);
            Gl.texImage2D(Gl.TEXTURE_2D, 0, Gl.RGBA16F, Packed.Width, Packed.Height, 0, Gl.RGBA, Gl.HALF_FLOAT, Packed.Texels);

            if (QuadCageGpu)
            {
                Gl.deleteBuffer(QuadCageGpu.Vbo);
                Gl.deleteVertexArray(QuadCageGpu.Vao);
            }
            QuadCageGpu = CreateLineVao(CageBuffers.Vertices);
            QuadCageVertCount = CageBuffers.VertexCount;
        },

        SyncLoftResources(LoftSdf, CageBuffers)
        {
            if (LoftTexObj) Gl.deleteTexture(LoftTexObj);
            const Packed = PackLoftRgba16Texture(LoftSdf);
            LoftTexObj = Gl.createTexture();
            Gl.bindTexture(Gl.TEXTURE_2D, LoftTexObj);
            Gl.texParameteri(Gl.TEXTURE_2D, Gl.TEXTURE_MIN_FILTER, Gl.NEAREST);
            Gl.texParameteri(Gl.TEXTURE_2D, Gl.TEXTURE_MAG_FILTER, Gl.NEAREST);
            Gl.texParameteri(Gl.TEXTURE_2D, Gl.TEXTURE_WRAP_S, Gl.CLAMP_TO_EDGE);
            Gl.texParameteri(Gl.TEXTURE_2D, Gl.TEXTURE_WRAP_T, Gl.CLAMP_TO_EDGE);
            Gl.texImage2D(Gl.TEXTURE_2D, 0, Gl.RGBA16F, Packed.Width, Packed.Height, 0, Gl.RGBA, Gl.HALF_FLOAT, Packed.Texels);

            if (LoftCageGpu)
            {
                Gl.deleteBuffer(LoftCageGpu.Vbo);
                Gl.deleteVertexArray(LoftCageGpu.Vao);
            }
            LoftCageGpu = CreateLineVao(CageBuffers.Vertices);
            LoftCageVertCount = CageBuffers.VertexCount;
        },

        SyncVoxelResources(VoxelSdf, CageBuffers)
        {
            if (VoxelTexObj) Gl.deleteTexture(VoxelTexObj);
            const Res = VoxelSdf.Res;
            VoxelTexObj = Gl.createTexture();
            Gl.bindTexture(Gl.TEXTURE_3D, VoxelTexObj);
            Gl.texParameteri(Gl.TEXTURE_3D, Gl.TEXTURE_MIN_FILTER, Gl.LINEAR);
            Gl.texParameteri(Gl.TEXTURE_3D, Gl.TEXTURE_MAG_FILTER, Gl.LINEAR);
            Gl.texParameteri(Gl.TEXTURE_3D, Gl.TEXTURE_WRAP_S, Gl.CLAMP_TO_EDGE);
            Gl.texParameteri(Gl.TEXTURE_3D, Gl.TEXTURE_WRAP_T, Gl.CLAMP_TO_EDGE);
            Gl.texParameteri(Gl.TEXTURE_3D, Gl.TEXTURE_WRAP_R, Gl.CLAMP_TO_EDGE);
            Gl.texImage3D(Gl.TEXTURE_3D, 0, Gl.RGBA16F, Res, Res, Res, 0, Gl.RGBA, Gl.HALF_FLOAT, VoxelSdf.Rgba16f);

            if (VoxelCageGpu)
            {
                Gl.deleteBuffer(VoxelCageGpu.Vbo);
                Gl.deleteVertexArray(VoxelCageGpu.Vao);
            }
            VoxelCageGpu = CreateLineVao(CageBuffers.Vertices);
            VoxelCageVertCount = CageBuffers.VertexCount;
        },

        SyncCutawayPlane(PlaneBuffers)
        {
            if (CutPlaneGpu)
            {
                Gl.deleteBuffer(CutPlaneGpu.Vbo);
                Gl.deleteBuffer(CutPlaneGpu.Ibo);
                Gl.deleteVertexArray(CutPlaneGpu.Vao);
            }
            CutPlaneGpu = CreateSurfaceVao(PlaneBuffers.Vertices, PlaneBuffers.Indices);
            CutPlaneIndexCount = PlaneBuffers.IndexCount;
        },

        RenderTick(UniformFloats, Opts)
        {
            if (!LoftTexObj || !VoxelTexObj || !QuadTexObj) return;
            const W = Canvas.width;
            const H = Canvas.height;
            Gl.viewport(0, 0, W, H);
            Gl.clearColor(0.038, 0.056, 0.084, 1.0);
            Gl.clearDepth(1.0);
            Gl.depthMask(true);
            Gl.clear(Gl.COLOR_BUFFER_BIT | Gl.DEPTH_BUFFER_BIT);

            Gl.enable(Gl.DEPTH_TEST);
            Gl.depthFunc(Gl.LEQUAL);
            Gl.disable(Gl.CULL_FACE);
            Gl.disable(Gl.BLEND);

            Gl.activeTexture(Gl.TEXTURE0);
            Gl.bindTexture(Gl.TEXTURE_2D, LoftTexObj);
            Gl.activeTexture(Gl.TEXTURE1);
            Gl.bindTexture(Gl.TEXTURE_3D, VoxelTexObj);
            Gl.activeTexture(Gl.TEXTURE2);
            Gl.bindTexture(Gl.TEXTURE_2D, QuadTexObj);

            ApplySharedUniforms(ShowroomProg, ShowroomUni, UniformFloats);
            Gl.bindVertexArray(ShowroomGpu.Vao);

            if (Opts.DisplayMode === 1)
            {
                Gl.drawElements(Gl.TRIANGLES, ShowroomBuffers.EnvIndexCount, Gl.UNSIGNED_INT, ShowroomBuffers.EnvIndexStart * 4);
                ApplySharedUniforms(IsoProg, IsoUni, UniformFloats);
                Gl.bindVertexArray(EmptyIsoVao);
                Gl.drawArrays(Gl.TRIANGLES, 0, 3);
            }
            else
            {
                Gl.drawElements(Gl.TRIANGLES, ShowroomBuffers.EnvIndexCount + ShowroomBuffers.VehicleIndexCount, Gl.UNSIGNED_INT, 0);
            }

            if (Opts.ShowLoftCage || Opts.ShowVoxelCage)
            {
                Gl.enable(Gl.BLEND);
                Gl.blendFunc(Gl.SRC_ALPHA, Gl.ONE_MINUS_SRC_ALPHA);
                Gl.depthMask(false);
                Gl.useProgram(CageProg);
                Gl.uniformMatrix4fv(CageViewProjLoc, false, UniformFloats.subarray(0, 16));

                if (Opts.SplitCompare && W >= 4)
                {
                    const MidW = Math.max(1, Math.min(W - 1, Math.floor(W * 0.5)));
                    Gl.enable(Gl.SCISSOR_TEST);
                    if (Opts.ShowLoftCage && QuadCageVertCount > 0)
                    {
                        Gl.scissor(0, 0, MidW, H);
                        Gl.bindVertexArray(QuadCageGpu.Vao);
                        Gl.drawArrays(Gl.LINES, 0, QuadCageVertCount);
                    }
                    Gl.scissor(MidW, 0, Math.max(1, W - MidW), H);
                    if (Opts.SplitRightMode === 1 && Opts.ShowLoftCage && LoftCageVertCount > 0)
                    {
                        Gl.bindVertexArray(LoftCageGpu.Vao);
                        Gl.drawArrays(Gl.LINES, 0, LoftCageVertCount);
                    }
                    if (Opts.ShowVoxelCage && VoxelCageVertCount > 0)
                    {
                        Gl.bindVertexArray(VoxelCageGpu.Vao);
                        Gl.drawArrays(Gl.LINES, 0, VoxelCageVertCount);
                    }
                    Gl.disable(Gl.SCISSOR_TEST);
                }
                else
                {
                    if (Opts.ShowLoftCage)
                    {
                        if (Opts.ActiveParadigm === 1 && LoftCageVertCount > 0)
                        {
                            Gl.bindVertexArray(LoftCageGpu.Vao);
                            Gl.drawArrays(Gl.LINES, 0, LoftCageVertCount);
                        }
                        else if (QuadCageVertCount > 0)
                        {
                            Gl.bindVertexArray(QuadCageGpu.Vao);
                            Gl.drawArrays(Gl.LINES, 0, QuadCageVertCount);
                        }
                    }
                    if (Opts.ShowVoxelCage && VoxelCageVertCount > 0)
                    {
                        Gl.bindVertexArray(VoxelCageGpu.Vao);
                        Gl.drawArrays(Gl.LINES, 0, VoxelCageVertCount);
                    }
                }
                Gl.depthMask(true);
                Gl.disable(Gl.BLEND);
            }

            if (Opts.DisplayMode === 2 && CutPlaneGpu && CutPlaneIndexCount > 0)
            {
                Gl.enable(Gl.BLEND);
                Gl.blendFunc(Gl.SRC_ALPHA, Gl.ONE_MINUS_SRC_ALPHA);
                Gl.depthMask(false);
                ApplySharedUniforms(ShowroomProg, ShowroomUni, UniformFloats);
                Gl.bindVertexArray(CutPlaneGpu.Vao);
                Gl.drawElements(Gl.TRIANGLES, CutPlaneIndexCount, Gl.UNSIGNED_INT, 0);
                Gl.depthMask(true);
                Gl.disable(Gl.BLEND);
            }

            Gl.bindVertexArray(null);
        }
    };
}

//============================================================================================================================================
//                                  4. MAIN WORKBENCH INITIALIZATION & UI SYNCHRONIZATION
//============================================================================================================================================

async function LaunchLoftedVehicleWorkbench()
{
    const Canvas = document.getElementById("ViewportCanvas");
    const RuntimeTag = document.getElementById("RuntimeTag");

    // 1. Load real ControlVehicle FCT1 binaries in parallel
    const [BodyRes, RimRes, TyreRes] = await Promise.all([
        fetch("./Content/ControlVehicleBody.bin"),
        fetch("./Content/ControlVehicleRim.bin"),
        fetch("./Content/ControlVehicleTyre.bin")
    ]);
    if (!BodyRes.ok || !RimRes.ok || !TyreRes.ok)
    {
        throw new Error("Failed to fetch ControlVehicle FCT1 geometry binaries.");
    }

    const [BodyBuf, RimBuf, TyreBuf] = await Promise.all([
        BodyRes.arrayBuffer(),
        RimRes.arrayBuffer(),
        TyreRes.arrayBuffer()
    ]);

    const BodyBin = ParseFct1VehicleBinary(BodyBuf);
    const RimBin  = ParseFct1VehicleBinary(RimBuf);
    const TyreBin = ParseFct1VehicleBinary(TyreBuf);
    const AssembledCar = AssembleCompleteControlVehicle(BodyBin, RimBin, TyreBin);
    const BodyBvh  = BuildTriangleBvhAccelerator(AssembledCar.BodyPart.Positions, AssembledCar.BodyPart.Indices, AssembledCar.BodyPart.FaceCount);
    const ExactBvh = BuildTriangleBvhAccelerator(AssembledCar.Positions, AssembledCar.Indices, AssembledCar.TotalFaces);
    const ShowroomBuffers = BuildShowroomSceneBuffers(AssembledCar);

    // 2. Compile all 4 Closed 4-Corner C² Quad-Patch presets (~24 ms total!) + all 4 Cylindrical Loft presets + Voxel 16³ & 32³ upfront
    const CompiledQuads  = Object.create(null);
    const QuadCages      = Object.create(null);
    const QuadFitTimesMs = Object.create(null);

    for (const Preset of QuadPatchResolutionPresets)
    {
        const T0 = performance.now();
        const QuadSdf = CompileQuadPatchVehicleSdf(AssembledCar, Preset.GridN, Preset.WheelSamples, BodyBvh);
        QuadFitTimesMs[Preset.PresetKey] = performance.now() - T0;
        CompiledQuads[Preset.PresetKey]  = QuadSdf;
        QuadCages[Preset.PresetKey]      = BuildQuadPatchCageBuffers(QuadSdf);
    }

    const CompiledLofts  = Object.create(null);
    const LoftCages      = Object.create(null);
    const LoftFitTimesMs = Object.create(null);

    for (const Preset of LoftResolutionPresets)
    {
        const T0 = performance.now();
        const LoftSdf = CompileLoftedVehicleSdf(AssembledCar, Preset.RibsM, Preset.RailsK, Preset.WheelSamples);
        LoftFitTimesMs[Preset.PresetKey] = performance.now() - T0;
        CompiledLofts[Preset.PresetKey]  = LoftSdf;
        LoftCages[Preset.PresetKey]      = BuildLoftCurveCageBuffers(LoftSdf);
    }

    const CompiledVoxels   = Object.create(null);
    const VoxelCages       = Object.create(null);
    const VoxelBakeTimesMs = Object.create(null);
    const SignRefHull = CompiledQuads["quad-16x16"];

    function EnsureVoxelPresetReady(PresetKey)
    {
        if (CompiledVoxels[PresetKey]) return CompiledVoxels[PresetKey];
        const Entry = VoxelResolutionPresets.find((P) => P.PresetKey === PresetKey);
        const T0 = performance.now();
        const Vox = CompileUniformVoxelVehicleSdf(AssembledCar, ExactBvh, Entry.Res, SignRefHull);
        VoxelBakeTimesMs[PresetKey] = performance.now() - T0;
        CompiledVoxels[PresetKey]   = Vox;
        VoxelCages[PresetKey]       = BuildVoxelCageBuffers(Vox);
        return Vox;
    }

    EnsureVoxelPresetReady("vox-16");
    EnsureVoxelPresetReady("vox-32");

    // 3. Initialize WebGPU (or automatic WebGL2 fallback)
    let GpuDriver = null;
    try
    {
        GpuDriver = await TryInitializeWebGpuRenderer(Canvas, ShowroomBuffers);
    }
    catch (Err)
    {
        console.warn("WebGPU initialization fell through to WebGL2:", Err);
    }
    if (!GpuDriver)
    {
        GpuDriver = InitializeWebGl2Renderer(Canvas, ShowroomBuffers);
    }

    RuntimeTag.textContent = `🟢 ${GpuDriver.RuntimeLabel} · ControlVehicle (${AssembledCar.TotalFaces.toLocaleString()} Tris)`;

    // 4. Active Workbench Controls & Camera Orbit
    const ActiveSettings = {
        DisplayMode:      0,             // 0 = Showroom + SDF, 1 = Pure d(x)=0 IsoSurface, 2 = 3D Cutaway
        ActiveParadigm:   0,             // 0 = Closed 4-Corner C² Quad-Patch, 1 = Cylindrical Polar-Fan Loft, 2 = Uniform Voxel Grid
        SplitCompare:     false,
        SplitRightMode:   2,             // 2 = Uniform 3D Voxel Grid, 1 = Cylindrical Polar-Fan Loft
        ZebraInspect:     false,
        ShowLoftCage:     true,
        ShowVoxelCage:    false,
        QuadKey:          "quad-16x16",
        LoftKey:          "loft-32x36",
        VoxelKey:         "vox-32",
        SunAzimuthDeg:    42.0,
        SunElevationDeg:  26.0,
        ShadowSoftness:   14.0,
        FloorReflectPct:  65.0,
        CutAxisMode:      0,             // 0 = Z transverse, 1 = X longitudinal
        CutCoord:         0.35,
        CutClipCar:       false
    };

    const Cam = {
        Target: [...CameraBookmarks.Hero.Target],
        Yaw:    CameraBookmarks.Hero.Yaw,
        Pitch:  CameraBookmarks.Hero.Pitch,
        Dist:   CameraBookmarks.Hero.Dist
    };

    function ApplyCameraBookmark(Key)
    {
        const Bm = CameraBookmarks[Key];
        if (!Bm) return;
        Cam.Target = [...Bm.Target];
        Cam.Yaw    = Bm.Yaw;
        Cam.Pitch  = Bm.Pitch;
        Cam.Dist   = Bm.Dist;
    }

    // 5. DOM References
    const TelLoftVram   = document.getElementById("TelLoftVram");
    const TelVoxVram    = document.getElementById("TelVoxVram");
    const TelVramRatio  = document.getElementById("TelVramRatio");
    const TelSkinErr    = document.getElementById("TelSkinErr");
    const TelFps        = document.getElementById("TelFps");
    const SplitBanner   = document.getElementById("SplitBanner");
    const SplitLeftLbl  = document.getElementById("SplitLeftLabel");
    const SplitRightLbl = document.getElementById("SplitRightLabel");

    const ModeBtnShowroom   = document.getElementById("ModeBtnShowroom");
    const ModeBtnIsoSurface = document.getElementById("ModeBtnIsoSurface");
    const ModeBtnCutaway    = document.getElementById("ModeBtnCutaway");

    const SplitCompareToggle    = document.getElementById("SplitCompareToggle");
    const SplitRightModeSelect  = document.getElementById("SplitRightModeSelect");
    const SplitRightModeReadout = document.getElementById("SplitRightModeReadout");
    const ZebraInspectToggle    = document.getElementById("ZebraInspectToggle");
    const LoftCageToggle        = document.getElementById("LoftCageToggle");
    const VoxelCageToggle       = document.getElementById("VoxelCageToggle");

    const ActiveParadigmSelect   = document.getElementById("ActiveParadigmSelect");
    const ActiveParadigmReadout  = document.getElementById("ActiveParadigmReadout");
    const QuadPatchPresetSelect  = document.getElementById("QuadPatchPresetSelect");
    const QuadPatchPresetReadout = document.getElementById("QuadPatchPresetReadout");
    const LoftPresetSelect       = document.getElementById("LoftPresetSelect");
    const LoftPresetReadout      = document.getElementById("LoftPresetReadout");
    const VoxelPresetSelect      = document.getElementById("VoxelPresetSelect");
    const VoxelPresetReadout     = document.getElementById("VoxelPresetReadout");

    const SunAzimuthSlider      = document.getElementById("SunAzimuthSlider");
    const SunAzimuthReadout     = document.getElementById("SunAzimuthReadout");
    const SunElevationSlider    = document.getElementById("SunElevationSlider");
    const SunElevationReadout   = document.getElementById("SunElevationReadout");
    const ShadowSoftnessSlider  = document.getElementById("ShadowSoftnessSlider");
    const ShadowSoftnessReadout = document.getElementById("ShadowSoftnessReadout");
    const FloorReflectSlider    = document.getElementById("FloorReflectSlider");
    const FloorReflectReadout   = document.getElementById("FloorReflectReadout");
    const CutAxisSelect         = document.getElementById("CutAxisSelect");
    const CutAxisReadout        = document.getElementById("CutAxisReadout");
    const CutCoordSlider        = document.getElementById("CutCoordSlider");
    const CutCoordReadout       = document.getElementById("CutCoordReadout");
    const CutClipCarToggle      = document.getElementById("CutClipCarToggle");

    const BenchHeadLoft       = document.getElementById("BenchHeadLoft");
    const BenchHeadVox        = document.getElementById("BenchHeadVox");
    const BenchLoftCompact    = document.getElementById("BenchLoftCompact");
    const BenchVoxFp16        = document.getElementById("BenchVoxFp16");
    const BenchLoftGpu        = document.getElementById("BenchLoftGpu");
    const BenchVoxRgba16      = document.getElementById("BenchVoxRgba16");
    const BenchLoftFitMs      = document.getElementById("BenchLoftFitMs");
    const BenchVoxBakeMs      = document.getElementById("BenchVoxBakeMs");
    const BenchLoftMedMm      = document.getElementById("BenchLoftMedMm");
    const BenchVoxMedMm       = document.getElementById("BenchVoxMedMm");
    const BenchLoftP90Mm      = document.getElementById("BenchLoftP90Mm");
    const BenchVoxP90Mm       = document.getElementById("BenchVoxP90Mm");
    const BenchLoftUs         = document.getElementById("BenchLoftUs");
    const BenchVoxUs          = document.getElementById("BenchVoxUs");
    const BenchLoftContinuity = document.getElementById("BenchLoftContinuity");
    const BenchVoxContinuity  = document.getElementById("BenchVoxContinuity");
    const LiveSummaryNote     = document.getElementById("LiveSummaryNote");

    const BenchmarkPairResults = Object.create(null);

    function RefreshQuantitativeTelemetry()
    {
        const QuadSdf  = CompiledQuads[ActiveSettings.QuadKey];
        const LoftSdf  = CompiledLofts[ActiveSettings.LoftKey];
        const VoxelSdf = EnsureVoxelPresetReady(ActiveSettings.VoxelKey);

        const InspectCylLoft = (!ActiveSettings.SplitCompare && ActiveSettings.ActiveParadigm === 1);
        const SurfKey = InspectCylLoft ? ActiveSettings.LoftKey : ActiveSettings.QuadKey;
        const PairKey = `${SurfKey}|${ActiveSettings.VoxelKey}`;

        let Bench = BenchmarkPairResults[PairKey];
        if (!Bench)
        {
            Bench = InspectCylLoft
                ? BenchmarkLoftVsVoxelSdf(AssembledCar, ExactBvh, LoftSdf, VoxelSdf)
                : BenchmarkQuadPatchVsVoxelSdf(AssembledCar, ExactBvh, QuadSdf, VoxelSdf);
            BenchmarkPairResults[PairKey] = Bench;
        }

        const CompactRatio = Bench.VoxFp16Kb / Math.max(Bench.LoftCompactKb, 0.01);
        const GpuRatio     = Bench.VoxRgba16Kb / Math.max(Bench.LoftGpuKb, 0.01);
        const SurfTitle    = InspectCylLoft ? `Loft ${LoftSdf.RibsM}×${LoftSdf.RailsK}` : `Quad 6×${QuadSdf.GridN}²`;
        const FitTimeMs    = InspectCylLoft ? LoftFitTimesMs[ActiveSettings.LoftKey] : QuadFitTimesMs[ActiveSettings.QuadKey];

        TelLoftVram.textContent  = `${Bench.LoftCompactKb.toFixed(2)} KB (${Bench.LoftGpuKb.toFixed(2)} KB GPU)`;
        TelVoxVram.textContent   = `${Bench.VoxFp16Kb.toFixed(1)} KB (${Bench.VoxRgba16Kb.toFixed(0)} KB GPU)`;
        TelVramRatio.textContent = `${CompactRatio.toFixed(1)}× smaller`;
        TelSkinErr.textContent   = `${SurfTitle}: ${Bench.LoftMedianMm.toFixed(1)} mm med (${Bench.LoftP90Mm.toFixed(1)} P90) · Vox ${Bench.VoxP90Mm.toFixed(1)} mm P90`;

        const QuadCompactKb = QuadSdf.CompactKnotBytes / 1024.0;
        const LoftCompactKb = LoftSdf.CompactKnotBytes / 1024.0;
        SplitLeftLbl.textContent = `LEFT · Closed 4-Corner C² Quad-Patch (6 × ${QuadSdf.GridN}² = ${QuadSdf.PatchCount} patches · ${QuadCompactKb.toFixed(2)} KB)`;
        if (ActiveSettings.SplitRightMode === 1)
        {
            SplitRightLbl.textContent = `RIGHT · Cylindrical Polar-Fan Loft (${LoftSdf.RibsM} × ${LoftSdf.RailsK} · ${LoftCompactKb.toFixed(2)} KB · 2 Pole Fans)`;
        }
        else
        {
            SplitRightLbl.textContent = `RIGHT · Uniform 3D Voxel SDF (${VoxelSdf.Res}³ · ${Bench.VoxFp16Kb.toFixed(1)} KB r16f · C⁰ Staircase)`;
        }

        QuadPatchPresetReadout.textContent = `6 × ${QuadSdf.GridN}²`;
        LoftPresetReadout.textContent      = `${LoftSdf.RibsM} × ${LoftSdf.RailsK}`;
        VoxelPresetReadout.textContent     = `${VoxelSdf.Res}³`;
        BenchHeadLoft.textContent          = SurfTitle;
        BenchHeadVox.textContent           = `Voxel ${VoxelSdf.Res}³`;

        BenchLoftCompact.textContent    = `${Bench.LoftCompactKb.toFixed(2)} KB`;
        BenchVoxFp16.textContent        = `${Bench.VoxFp16Kb.toFixed(1)} KB`;
        BenchLoftGpu.textContent        = `${Bench.LoftGpuKb.toFixed(2)} KB`;
        BenchVoxRgba16.textContent      = `${Bench.VoxRgba16Kb.toFixed(0)} KB`;
        BenchLoftFitMs.textContent      = `${FitTimeMs.toFixed(1)} ms`;
        BenchVoxBakeMs.textContent      = `${VoxelBakeTimesMs[ActiveSettings.VoxelKey].toFixed(0)} ms`;
        BenchLoftMedMm.textContent      = `${Bench.LoftMedianMm.toFixed(2)} mm`;
        BenchVoxMedMm.textContent       = `${Bench.VoxMedianMm.toFixed(2)} mm`;
        BenchLoftP90Mm.textContent      = `${Bench.LoftP90Mm.toFixed(2)} mm`;
        BenchVoxP90Mm.textContent       = `${Bench.VoxP90Mm.toFixed(2)} mm`;
        BenchLoftUs.textContent         = `${Bench.LoftUsPerEval.toFixed(2)} µs`;
        BenchVoxUs.textContent          = `${Bench.VoxUsPerEval.toFixed(3)} µs`;
        BenchLoftContinuity.textContent = InspectCylLoft ? "C¹ · 2 Pole Fans" : "C² · 0 Poles";
        BenchVoxContinuity.textContent  = "C⁰ Staircase";

        LiveSummaryNote.innerHTML =
            `💡 <strong>${SurfTitle}</strong> uses <strong>${CompactRatio.toFixed(1)}×</strong> less compact VRAM ` +
            `(<strong>${GpuRatio.toFixed(1)}×</strong> less GPU rgba16f VRAM) than <strong>Voxel ${VoxelSdf.Res}³</strong>, compiles in ` +
            `<strong>${FitTimeMs.toFixed(1)} ms</strong> vs. <strong>${VoxelBakeTimesMs[ActiveSettings.VoxelKey].toFixed(0)} ms</strong>, ` +
            `and wraps every panel in closed 4-corner C² patches with zero polar fan singularities.`;
    }

    function SyncDisplayModeButtons()
    {
        ModeBtnShowroom.classList.toggle("Active", ActiveSettings.DisplayMode === 0);
        ModeBtnIsoSurface.classList.toggle("Active", ActiveSettings.DisplayMode === 1);
        ModeBtnCutaway.classList.toggle("Active", ActiveSettings.DisplayMode === 2);
        SplitBanner.classList.toggle("Visible", ActiveSettings.SplitCompare);
    }

    function ActivateDisplayMode(NewMode)
    {
        ActiveSettings.DisplayMode = NewMode;
        if (NewMode === 2)
        {
            ActiveSettings.CutClipCar = true;
            CutClipCarToggle.checked = true;
        }
        SyncDisplayModeButtons();
    }

    // Initial resource upload
    GpuDriver.SyncQuadPatchResources(CompiledQuads[ActiveSettings.QuadKey], QuadCages[ActiveSettings.QuadKey]);
    GpuDriver.SyncLoftResources(CompiledLofts[ActiveSettings.LoftKey], LoftCages[ActiveSettings.LoftKey]);
    GpuDriver.SyncVoxelResources(CompiledVoxels[ActiveSettings.VoxelKey], VoxelCages[ActiveSettings.VoxelKey]);
    GpuDriver.SyncCutawayPlane(BuildCutawayPlaneBuffers(ActiveSettings.CutAxisMode, ActiveSettings.CutCoord));
    SyncDisplayModeButtons();
    RefreshQuantitativeTelemetry();

    // 6. Wire UI Controls
    ModeBtnShowroom.addEventListener("click", () => ActivateDisplayMode(0));
    ModeBtnIsoSurface.addEventListener("click", () => ActivateDisplayMode(1));
    ModeBtnCutaway.addEventListener("click", () => ActivateDisplayMode(2));

    SplitCompareToggle.addEventListener("change", () => {
        ActiveSettings.SplitCompare = SplitCompareToggle.checked;
        SyncDisplayModeButtons();
        RefreshQuantitativeTelemetry();
    });

    SplitRightModeSelect.addEventListener("change", () => {
        ActiveSettings.SplitRightMode = Number(SplitRightModeSelect.value);
        SplitRightModeReadout.textContent = ActiveSettings.SplitRightMode === 1 ? "Polar-Fan Loft" : "3D Voxel Grid";
        if (!ActiveSettings.SplitCompare)
        {
            ActiveSettings.SplitCompare = true;
            SplitCompareToggle.checked = true;
            SyncDisplayModeButtons();
        }
        RefreshQuantitativeTelemetry();
    });

    ZebraInspectToggle.addEventListener("change", () => {
        ActiveSettings.ZebraInspect = ZebraInspectToggle.checked;
        if (ActiveSettings.ZebraInspect && ActiveSettings.DisplayMode !== 1)
        {
            ActivateDisplayMode(1);
        }
    });

    LoftCageToggle.addEventListener("change", () => {
        ActiveSettings.ShowLoftCage = LoftCageToggle.checked;
    });

    VoxelCageToggle.addEventListener("change", () => {
        ActiveSettings.ShowVoxelCage = VoxelCageToggle.checked;
    });

    ActiveParadigmSelect.addEventListener("change", () => {
        ActiveSettings.ActiveParadigm = Number(ActiveParadigmSelect.value);
        const Labels = ["4-Corner Quad-Patch", "Polar-Fan Loft", "3D Voxel Grid"];
        ActiveParadigmReadout.textContent = Labels[ActiveSettings.ActiveParadigm] || "4-Corner Quad-Patch";
        RefreshQuantitativeTelemetry();
    });

    QuadPatchPresetSelect.addEventListener("change", () => {
        ActiveSettings.QuadKey = QuadPatchPresetSelect.value;
        GpuDriver.SyncQuadPatchResources(CompiledQuads[ActiveSettings.QuadKey], QuadCages[ActiveSettings.QuadKey]);
        RefreshQuantitativeTelemetry();
    });

    LoftPresetSelect.addEventListener("change", () => {
        ActiveSettings.LoftKey = LoftPresetSelect.value;
        GpuDriver.SyncLoftResources(CompiledLofts[ActiveSettings.LoftKey], LoftCages[ActiveSettings.LoftKey]);
        RefreshQuantitativeTelemetry();
    });

    VoxelPresetSelect.addEventListener("change", () => {
        ActiveSettings.VoxelKey = VoxelPresetSelect.value;
        const Vox = EnsureVoxelPresetReady(ActiveSettings.VoxelKey);
        GpuDriver.SyncVoxelResources(Vox, VoxelCages[ActiveSettings.VoxelKey]);
        RefreshQuantitativeTelemetry();
    });

    SunAzimuthSlider.addEventListener("input", () => {
        ActiveSettings.SunAzimuthDeg = Number(SunAzimuthSlider.value);
        SunAzimuthReadout.textContent = `${ActiveSettings.SunAzimuthDeg.toFixed(0)}°`;
    });

    SunElevationSlider.addEventListener("input", () => {
        ActiveSettings.SunElevationDeg = Number(SunElevationSlider.value);
        SunElevationReadout.textContent = `${ActiveSettings.SunElevationDeg.toFixed(0)}°`;
    });

    ShadowSoftnessSlider.addEventListener("input", () => {
        ActiveSettings.ShadowSoftness = Number(ShadowSoftnessSlider.value);
        ShadowSoftnessReadout.textContent = `${ActiveSettings.ShadowSoftness.toFixed(0)}`;
    });

    FloorReflectSlider.addEventListener("input", () => {
        ActiveSettings.FloorReflectPct = Number(FloorReflectSlider.value);
        FloorReflectReadout.textContent = `${ActiveSettings.FloorReflectPct.toFixed(0)}%`;
    });

    CutAxisSelect.addEventListener("change", () => {
        ActiveSettings.CutAxisMode = Number(CutAxisSelect.value);
        CutAxisReadout.textContent = ActiveSettings.CutAxisMode === 0 ? "Transverse Z" : "Longitudinal X";
        CutCoordSlider.min = ActiveSettings.CutAxisMode === 0 ? "-3.15" : "-1.35";
        CutCoordSlider.max = ActiveSettings.CutAxisMode === 0 ?  "3.15" :  "1.35";
        ActiveSettings.CutCoord = Clamp(ActiveSettings.CutCoord, Number(CutCoordSlider.min), Number(CutCoordSlider.max));
        CutCoordSlider.value = String(ActiveSettings.CutCoord);
        CutCoordReadout.textContent = `${ActiveSettings.CutCoord >= 0 ? "+" : ""}${ActiveSettings.CutCoord.toFixed(2)} m`;
        GpuDriver.SyncCutawayPlane(BuildCutawayPlaneBuffers(ActiveSettings.CutAxisMode, ActiveSettings.CutCoord));
    });

    CutCoordSlider.addEventListener("input", () => {
        ActiveSettings.CutCoord = Number(CutCoordSlider.value);
        CutCoordReadout.textContent = `${ActiveSettings.CutCoord >= 0 ? "+" : ""}${ActiveSettings.CutCoord.toFixed(2)} m`;
        GpuDriver.SyncCutawayPlane(BuildCutawayPlaneBuffers(ActiveSettings.CutAxisMode, ActiveSettings.CutCoord));
    });

    CutClipCarToggle.addEventListener("change", () => {
        ActiveSettings.CutClipCar = CutClipCarToggle.checked;
    });

    document.getElementById("CamHeroBtn").addEventListener("click",      () => ApplyCameraBookmark("Hero"));
    document.getElementById("CamNoseQuadsBtn").addEventListener("click", () => ApplyCameraBookmark("NoseQuads"));
    document.getElementById("CamFenderBtn").addEventListener("click",    () => ApplyCameraBookmark("Fender"));
    document.getElementById("CamProfileBtn").addEventListener("click",   () => ApplyCameraBookmark("Profile"));
    document.getElementById("CamShadowBtn").addEventListener("click",    () => ApplyCameraBookmark("Shadow"));
    document.getElementById("CamTopRibsBtn").addEventListener("click",   () => ApplyCameraBookmark("TopRibs"));

    // 7. Pointer Camera Orbit, Pan & Wheel Zoom
    let DragActive = false;
    let PanActive  = false;
    let LastPtrX = 0, LastPtrY = 0;

    Canvas.addEventListener("contextmenu", (Ev) => Ev.preventDefault());
    Canvas.addEventListener("pointerdown", (Ev) => {
        DragActive = true;
        PanActive  = Ev.button === 2 || Ev.shiftKey;
        LastPtrX   = Ev.clientX;
        LastPtrY   = Ev.clientY;
        Canvas.setPointerCapture(Ev.pointerId);
    });
    Canvas.addEventListener("pointermove", (Ev) => {
        if (!DragActive) return;
        const Dx = Ev.clientX - LastPtrX;
        const Dy = Ev.clientY - LastPtrY;
        LastPtrX = Ev.clientX;
        LastPtrY = Ev.clientY;

        if (PanActive)
        {
            const PanScale = Cam.Dist * 0.0014;
            const RightX = Math.cos(Cam.Yaw), RightZ = -Math.sin(Cam.Yaw);
            Cam.Target[0] -= RightX * Dx * PanScale;
            Cam.Target[2] -= RightZ * Dx * PanScale;
            Cam.Target[1] = Clamp(Cam.Target[1] + Dy * PanScale, 0.05, 2.20);
        }
        else
        {
            Cam.Yaw   -= Dx * 0.0065;
            Cam.Pitch  = Clamp(Cam.Pitch + Dy * 0.0065, 0.05, 1.48);
        }
    });
    Canvas.addEventListener("pointerup", () => { DragActive = false; });
    Canvas.addEventListener("wheel", (Ev) => {
        Ev.preventDefault();
        Cam.Dist = Clamp(Cam.Dist * Math.exp(Ev.deltaY * 0.0011), 1.45, 12.5);
    }, { passive: false });

    // 8. Continuous 60 FPS Render Loop
    const UniformFloats = new Float32Array(68);
    const StartMs = performance.now();
    let FpsAccumSec = 0.0;
    let FpsCount = 0;
    let LastTickMs = StartMs;

    function TickLoop(NowMs)
    {
        const DtSec = Math.max(0.0, (NowMs - LastTickMs) * 0.001);
        LastTickMs = NowMs;
        FpsAccumSec += DtSec;
        ++FpsCount;
        if (FpsAccumSec >= 0.45)
        {
            TelFps.textContent = `${Math.round(FpsCount / FpsAccumSec)} FPS`;
            FpsAccumSec = 0.0;
            FpsCount = 0;
        }

        const Dpr = Math.min(window.devicePixelRatio || 1, 2);
        const CssW = Math.max(1, Canvas.clientWidth);
        const CssH = Math.max(1, Canvas.clientHeight);
        const TargetW = Math.round(CssW * Dpr);
        const TargetH = Math.round(CssH * Dpr);
        if (Canvas.width !== TargetW || Canvas.height !== TargetH)
        {
            Canvas.width  = TargetW;
            Canvas.height = TargetH;
        }

        const CosP = Math.cos(Cam.Pitch);
        const Eye = [
            Cam.Target[0] + Math.sin(Cam.Yaw) * CosP * Cam.Dist,
            Cam.Target[1] + Math.sin(Cam.Pitch) * Cam.Dist,
            Cam.Target[2] + Math.cos(Cam.Yaw) * CosP * Cam.Dist
        ];

        const Aspect = TargetW / Math.max(1, TargetH);
        const FovRad = (42.0 * Math.PI) / 180.0;
        const View = BuildLookAtTransform(Eye, Cam.Target, [0.0, 1.0, 0.0]);
        const Proj = GpuDriver.ZeroToOneClip
            ? BuildPerspectiveZeroToOne(FovRad, Aspect, 0.12, 42.0)
            : BuildPerspectiveNegOneToOne(FovRad, Aspect, 0.12, 42.0);
        const ViewProj    = MultiplyTransforms4x4(Proj, View);
        const InvViewProj = InvertTransform4x4(ViewProj);

        const AzRad = (ActiveSettings.SunAzimuthDeg * Math.PI) / 180.0;
        const ElRad = (ActiveSettings.SunElevationDeg * Math.PI) / 180.0;
        const SunDir = Normalize3([
            Math.cos(ElRad) * Math.sin(AzRad),
            Math.sin(ElRad),
            Math.cos(ElRad) * Math.cos(AzRad)
        ]);

        const QuadSdf  = CompiledQuads[ActiveSettings.QuadKey];
        const LoftSdf  = CompiledLofts[ActiveSettings.LoftKey];
        const VoxelSdf = CompiledVoxels[ActiveSettings.VoxelKey];

        UniformFloats.set(ViewProj, 0);
        UniformFloats.set(InvViewProj, 16);

        // Params0..Params8
        UniformFloats[32] = Eye[0];
        UniformFloats[33] = Eye[1];
        UniformFloats[34] = Eye[2];
        UniformFloats[35] = TargetW;

        UniformFloats[36] = SunDir[0];
        UniformFloats[37] = SunDir[1];
        UniformFloats[38] = SunDir[2];
        UniformFloats[39] = TargetH;

        UniformFloats[40] = ActiveSettings.ActiveParadigm;
        UniformFloats[41] = ActiveSettings.SplitCompare ? 1.0 : 0.0;
        UniformFloats[42] = ActiveSettings.DisplayMode;
        UniformFloats[43] = ActiveSettings.ZebraInspect ? 1.0 : 0.0;

        UniformFloats[44] = LoftSdf.RibsM;
        UniformFloats[45] = LoftSdf.RailsK;
        UniformFloats[46] = LoftSdf.WheelSamples;
        UniformFloats[47] = LoftSdf.WheelHalfWidth;

        UniformFloats[48] = LoftSdf.ZMin;
        UniformFloats[49] = LoftSdf.ZMax;
        UniformFloats[50] = LoftSdf.ZSpan;
        UniformFloats[51] = ActiveSettings.CutCoord;

        UniformFloats[52] = VoxelSdf.BoundsMin[0];
        UniformFloats[53] = VoxelSdf.BoundsMin[1];
        UniformFloats[54] = VoxelSdf.BoundsMin[2];
        UniformFloats[55] = ActiveSettings.CutAxisMode;

        UniformFloats[56] = VoxelSdf.BoundsMax[0];
        UniformFloats[57] = VoxelSdf.BoundsMax[1];
        UniformFloats[58] = VoxelSdf.BoundsMax[2];
        UniformFloats[59] = VoxelSdf.Res;

        UniformFloats[60] = ActiveSettings.ShadowSoftness;
        UniformFloats[61] = ActiveSettings.FloorReflectPct * 0.01;
        UniformFloats[62] = ActiveSettings.CutClipCar ? 1.0 : 0.0;
        UniformFloats[63] = (NowMs - StartMs) * 0.001;

        UniformFloats[64] = QuadSdf.KnotSpan;
        UniformFloats[65] = QuadSdf.HaloLimit;
        UniformFloats[66] = ActiveSettings.SplitRightMode;
        UniformFloats[67] = QuadSdf.WheelSamples;

        GpuDriver.RenderTick(UniformFloats, {
            DisplayMode:    ActiveSettings.DisplayMode,
            ActiveParadigm: ActiveSettings.ActiveParadigm,
            SplitCompare:   ActiveSettings.SplitCompare,
            SplitRightMode: ActiveSettings.SplitRightMode,
            ShowLoftCage:   ActiveSettings.ShowLoftCage,
            ShowVoxelCage:  ActiveSettings.ShowVoxelCage
        });

        requestAnimationFrame(TickLoop);
    }

    requestAnimationFrame(TickLoop);
}

LaunchLoftedVehicleWorkbench().catch((Err) => {
    console.error(Err);
    const Tag = document.getElementById("RuntimeTag");
    if (Tag)
    {
        Tag.textContent = `🔴 Startup Error: ${Err.message}`;
    }
});
