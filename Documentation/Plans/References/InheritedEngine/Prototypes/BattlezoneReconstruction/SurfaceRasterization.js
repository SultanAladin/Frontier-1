/*====================================================================================================================================
                                                    SURFACERASTERIZATION.JS
====================================================================================================================================*/
// 🧩 WebGPU device provisioning, instance-buffer accumulation and the single indexed draw unit for the combat field

import { VertexFloatStride } from "./ChassisGeometry.js";
import { FieldSpecification } from "./CombatSpecification.js";
import { Mat4Assemble, Mat4Multiply, Mat4NormalTransform, Mat4Perspective, Mat4LookDirection } from "./LinearAlgebra.js";

//------------------------------------------------------------------------------------------------------------------------
//                                                       CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

const InstanceFloatStride = 36;      // [-] - 16 model + 16 normal + 4 tone floats per instance record
const InstanceCeiling     = 4096;    // [-] - Storage-buffer capacity in instance records
const ViewUniformBytes    = 128;     // [B] - mat4x4 (64) + three vec4 rows (48) padded to a 128-byte block
const DepthAttachmentFormat = "depth24plus";

//------------------------------------------------------------------------------------------------------------------------
//                                                  RASTERIZATION ASSEMBLY
//------------------------------------------------------------------------------------------------------------------------

export class SurfaceRasterization
{
    constructor(TargetSurface)
    {
        this.TargetSurface   = TargetSurface;
        this.InstanceScalars = new Float32Array(InstanceCeiling * InstanceFloatStride);
        this.InstanceTally   = 0;
        this.DrawWindows     = [];      // { IndexOffset, IndexCount, FirstInstance, InstanceCount }
    }

    // 🔴 Adapter and device acquisition can both fail on unsupported hardware; the caller surfaces the reason to the page.
    async InitializeDevice(GeometryAtlas)
    {
        if (!navigator.gpu) throw new Error("WebGPU is unavailable — this build requires a WebGPU-capable browser.");

        this.GraphicsAdapter = await navigator.gpu.requestAdapter({ powerPreference: "high-performance" });
        if (!this.GraphicsAdapter) throw new Error("No WebGPU adapter could be acquired.");

        this.GraphicsDevice  = await this.GraphicsAdapter.requestDevice();
        this.SurfaceContext  = this.TargetSurface.getContext("webgpu");
        this.SurfaceFormat   = navigator.gpu.getPreferredCanvasFormat();
        this.SurfaceContext.configure({
            device:    this.GraphicsDevice,
            format:    this.SurfaceFormat,
            alphaMode: "opaque"
        });

        this.PartRegions = GeometryAtlas.PartRegions;
        this.ProvisionGeometryBuffers(GeometryAtlas);
        await this.ProvisionPipeline();
        this.ProvisionDepthAttachment();
        return this;
    }

    ProvisionGeometryBuffers(GeometryAtlas)
    {
        this.VertexBuffer = this.GraphicsDevice.createBuffer({
            size:  GeometryAtlas.VertexScalars.byteLength,
            usage: GPUBufferUsage.VERTEX | GPUBufferUsage.COPY_DST
        });
        this.GraphicsDevice.queue.writeBuffer(this.VertexBuffer, 0, GeometryAtlas.VertexScalars);

        this.IndexBuffer = this.GraphicsDevice.createBuffer({
            size:  GeometryAtlas.IndexScalars.byteLength,
            usage: GPUBufferUsage.INDEX | GPUBufferUsage.COPY_DST
        });
        this.GraphicsDevice.queue.writeBuffer(this.IndexBuffer, 0, GeometryAtlas.IndexScalars);

        this.ViewUniformBuffer = this.GraphicsDevice.createBuffer({
            size:  ViewUniformBytes,
            usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST
        });

        this.InstanceBuffer = this.GraphicsDevice.createBuffer({
            size:  InstanceCeiling * InstanceFloatStride * 4,
            usage: GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_DST
        });
    }

    async ProvisionPipeline()
    {
        const ShaderSource = await (await fetch("./CombatSurface.wgsl")).text();
        const ShaderModule = this.GraphicsDevice.createShaderModule({ code: ShaderSource });

        this.ResourceLayout = this.GraphicsDevice.createBindGroupLayout({
            entries: [
                { binding: 0, visibility: GPUShaderStage.VERTEX | GPUShaderStage.FRAGMENT, buffer: { type: "uniform" } },
                { binding: 1, visibility: GPUShaderStage.VERTEX, buffer: { type: "read-only-storage" } }
            ]
        });

        this.ResourceBinding = this.GraphicsDevice.createBindGroup({
            layout:  this.ResourceLayout,
            entries: [
                { binding: 0, resource: { buffer: this.ViewUniformBuffer } },
                { binding: 1, resource: { buffer: this.InstanceBuffer } }
            ]
        });

        this.SurfacePipeline = this.GraphicsDevice.createRenderPipeline({
            layout: this.GraphicsDevice.createPipelineLayout({ bindGroupLayouts: [this.ResourceLayout] }),
            vertex: {
                module:     ShaderModule,
                entryPoint: "VertexSurface",
                buffers: [{
                    arrayStride: VertexFloatStride * 4,
                    attributes: [
                        { shaderLocation: 0, offset:  0, format: "float32x3" },
                        { shaderLocation: 1, offset: 12, format: "float32x3" }
                    ]
                }]
            },
            fragment: {
                module:     ShaderModule,
                entryPoint: "FragmentSurface",
                targets:    [{ format: this.SurfaceFormat }]
            },
            primitive: { topology: "triangle-list", cullMode: "none" },
            depthStencil: { format: DepthAttachmentFormat, depthWriteEnabled: true, depthCompare: "less" }
        });
    }

    // 📝 The depth attachment is rebuilt whenever the backing store changes size; a stale attachment is a validation error.
    ProvisionDepthAttachment()
    {
        if (this.DepthAttachment) this.DepthAttachment.destroy();
        this.DepthAttachment = this.GraphicsDevice.createTexture({
            size:   [this.TargetSurface.width, this.TargetSurface.height],
            format: DepthAttachmentFormat,
            usage:  GPUTextureUsage.RENDER_ATTACHMENT
        });
        this.AttachmentWidth  = this.TargetSurface.width;
        this.AttachmentHeight = this.TargetSurface.height;
    }

    //--------------------------------------------------------------------------------------------------------------------
    //                                               INSTANCE ACCUMULATION
    //--------------------------------------------------------------------------------------------------------------------

    ResetInstanceAccumulator()
    {
        this.InstanceTally = 0;
        this.DrawWindows   = [];
    }

    // Append one instance of a named part. Consecutive appends of the same part coalesce into one instanced draw.
    AccumulateInstance(RegionLabel, ModelTransform, SurfaceTone, EmissiveLift)
    {
        if (this.InstanceTally >= InstanceCeiling) return;
        const PartRegion = this.PartRegions[RegionLabel];
        if (!PartRegion) return;

        const NormalTransform = Mat4NormalTransform(ModelTransform);
        const ScalarOffset    = this.InstanceTally * InstanceFloatStride;
        this.InstanceScalars.set(ModelTransform,  ScalarOffset);
        this.InstanceScalars.set(NormalTransform, ScalarOffset + 16);
        this.InstanceScalars[ScalarOffset + 32] = SurfaceTone[0];
        this.InstanceScalars[ScalarOffset + 33] = SurfaceTone[1];
        this.InstanceScalars[ScalarOffset + 34] = SurfaceTone[2];
        this.InstanceScalars[ScalarOffset + 35] = EmissiveLift;

        const TrailingWindow = this.DrawWindows[this.DrawWindows.length - 1];
        if (TrailingWindow && TrailingWindow.IndexOffset === PartRegion.IndexOffset &&
            TrailingWindow.FirstInstance + TrailingWindow.InstanceCount === this.InstanceTally)
        {
            TrailingWindow.InstanceCount += 1;
        }
        else
        {
            this.DrawWindows.push({
                IndexOffset:   PartRegion.IndexOffset,
                IndexCount:    PartRegion.IndexCount,
                FirstInstance: this.InstanceTally,
                InstanceCount: 1
            });
        }
        this.InstanceTally += 1;
    }

    // Convenience wrapper so simulation code never assembles matrices itself.
    AccumulatePart(RegionLabel, Translation, YawAngle, PitchAngle, Scale, SurfaceTone, EmissiveLift = 0.0)
    {
        this.AccumulateInstance(RegionLabel, Mat4Assemble(Translation, YawAngle, PitchAngle, Scale),
                                SurfaceTone, EmissiveLift);
    }

    //--------------------------------------------------------------------------------------------------------------------
    //                                                  FRAME SUBMISSION
    //--------------------------------------------------------------------------------------------------------------------

    UploadViewUniform(SightPosition, SightTarget, ElapsedSeconds, FogDensity)
    {
        const AspectRatio  = this.TargetSurface.width / Math.max(this.TargetSurface.height, 1);
        const Projection   = Mat4Perspective(Math.PI * 0.32, AspectRatio, 0.35, 900.0);
        const ViewTransform = Mat4LookDirection(SightPosition, SightTarget, [0, 1, 0]);

        const UniformScalars = new Float32Array(ViewUniformBytes / 4);
        UniformScalars.set(Mat4Multiply(Projection, ViewTransform), 0);
        UniformScalars.set([SightPosition[0], SightPosition[1], SightPosition[2], ElapsedSeconds], 16);
        UniformScalars.set([FieldSpecification.SunDirection[0], FieldSpecification.SunDirection[1],
                            FieldSpecification.SunDirection[2], FogDensity], 20);
        UniformScalars.set([FieldSpecification.HorizonColour[0], FieldSpecification.HorizonColour[1],
                            FieldSpecification.HorizonColour[2], 1.0], 24);
        this.GraphicsDevice.queue.writeBuffer(this.ViewUniformBuffer, 0, UniformScalars);
    }

    SubmitFrame()
    {
        if (this.AttachmentWidth  !== this.TargetSurface.width ||
            this.AttachmentHeight !== this.TargetSurface.height) this.ProvisionDepthAttachment();

        // Upload only the populated span of the accumulator rather than the whole 4096-record ceiling.
        this.GraphicsDevice.queue.writeBuffer(this.InstanceBuffer, 0, this.InstanceScalars, 0,
                                              Math.max(this.InstanceTally, 1) * InstanceFloatStride);

        const CommandRecorder = this.GraphicsDevice.createCommandEncoder();
        const SurfaceRecorder = CommandRecorder.beginRenderPass({
            colorAttachments: [{
                view:       this.SurfaceContext.getCurrentTexture().createView(),
                clearValue: { r: FieldSpecification.HorizonColour[0], g: FieldSpecification.HorizonColour[1],
                              b: FieldSpecification.HorizonColour[2], a: 1.0 },
                loadOp:     "clear",
                storeOp:    "store"
            }],
            depthStencilAttachment: {
                view:            this.DepthAttachment.createView(),
                depthClearValue: 1.0,
                depthLoadOp:     "clear",
                depthStoreOp:    "store"
            }
        });

        SurfaceRecorder.setPipeline(this.SurfacePipeline);
        SurfaceRecorder.setBindGroup(0, this.ResourceBinding);
        SurfaceRecorder.setVertexBuffer(0, this.VertexBuffer);
        SurfaceRecorder.setIndexBuffer(this.IndexBuffer, "uint32");

        for (const DrawWindow of this.DrawWindows)
        {
            SurfaceRecorder.drawIndexed(DrawWindow.IndexCount, DrawWindow.InstanceCount,
                                        DrawWindow.IndexOffset, 0, DrawWindow.FirstInstance);
        }

        SurfaceRecorder.end();
        this.GraphicsDevice.queue.submit([CommandRecorder.finish()]);
    }
}
