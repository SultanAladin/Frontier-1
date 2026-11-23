#!/usr/bin/env python3
#===========================================================================================================================================
#                                                     PLUMEPRESSUREVALIDATION.PY
#===========================================================================================================================================
# 🧩 Execute the retained plume pressure WGSL and compare it with independent CPU stencils.

import math
import pathlib
import struct
import numpy as np
import wgpu

Root = pathlib.Path(__file__).resolve().parents[3]
ShaderDirectory = Root / 'Experimental/VehicleHmiReference/Shader'
Width, Height, Depth = 32, 8, 8
Extent = (Width, Height, Depth)

#-----------------------------------------------------------------------------------------------------------------------
#                                                 CPU PRESSURE STENCILS
#-----------------------------------------------------------------------------------------------------------------------


def EvaluateCenteredDivergence(Velocity):
    Extended = np.pad(Velocity[:, :, :, :3].astype(np.float32), ((1, 1), (1, 1), (1, 1), (0, 0)), mode='edge')
    Flux = ((Extended[1:-1, 1:-1, 2:, 0] - Extended[1:-1, 1:-1, :-2, 0])
            + (Extended[1:-1, 2:, 1:-1, 1] - Extended[1:-1, :-2, 1:-1, 1])
            + (Extended[2:, 1:-1, 1:-1, 2] - Extended[:-2, 1:-1, 1:-1, 2]))
    return np.where(Velocity[:, :, :, 3] > .5, 0, Flux * .5).astype(np.float32)


def GatherPressureNeighbours(Pressure, Solid):
    Neighbour = [np.zeros_like(Pressure) for _ in range(6)]
    Covered = [np.zeros_like(Solid) for _ in range(6)]
    Neighbour[0][:, :, :-1] = Pressure[:, :, 1:]
    Neighbour[1][:, :, 1:] = Pressure[:, :, :-1]
    Neighbour[2][:, :-1, :] = Pressure[:, 1:, :]
    Neighbour[3][:, 1:, :] = Pressure[:, :-1, :]
    Neighbour[4][:-1, :, :] = Pressure[1:, :, :]
    Neighbour[5][1:, :, :] = Pressure[:-1, :, :]
    Neighbour[5][0, :, :] = Pressure[0, :, :]  # Neumann ground
    Covered[0][:, :, :-1] = Solid[:, :, 1:]
    Covered[1][:, :, 1:] = Solid[:, :, :-1]
    Covered[2][:, :-1, :] = Solid[:, 1:, :]
    Covered[3][:, 1:, :] = Solid[:, :-1, :]
    Covered[4][:-1, :, :] = Solid[1:, :, :]
    Covered[5][1:, :, :] = Solid[:-1, :, :]
    return [np.where(Mask, Pressure, Adjacent) for Adjacent, Mask in zip(Neighbour, Covered)]


def RelaxPressureCpu(Pressure, Solid, Flux, Iterations):
    for _ in range(Iterations):
        Adjacent = GatherPressureNeighbours(Pressure, Solid)
        Total = Adjacent[0] + Adjacent[1] + Adjacent[2] + Adjacent[3] + Adjacent[4] + Adjacent[5]
        Pressure = np.where(Solid, Pressure, (Total - Flux) / np.float32(6)).astype(np.float32)
    return Pressure


def ProjectVelocityCpu(Velocity, Pressure):
    Solid = Velocity[:, :, :, 3] > .5
    Adjacent = GatherPressureNeighbours(Pressure, Solid)
    Gradient = np.stack([(Adjacent[0]-Adjacent[1])*.5, (Adjacent[2]-Adjacent[3])*.5,
                         (Adjacent[4]-Adjacent[5])*.5], axis=3)
    SpeedVector = Velocity[:, :, :, :3].astype(np.float32) - Gradient
    Speed = np.linalg.norm(SpeedVector, axis=3, keepdims=True)
    SpeedVector *= np.minimum(1, 40 / np.maximum(Speed, 1e-20))
    Output = np.zeros_like(Velocity, dtype=np.float32)
    Output[:, :, :, :3] = SpeedVector
    Output[Solid] = Velocity[Solid]
    return Output.astype(np.float16)


#-----------------------------------------------------------------------------------------------------------------------
#                                                WEBGPU PRESSURE SUBMISSION
#-----------------------------------------------------------------------------------------------------------------------


class PlumePressureSubmission:
    def __init__(self):
        Adapter = wgpu.gpu.request_adapter_sync(power_preference='low-power')
        print('ADAPTER', dict(Adapter.info), flush=True)
        self.Device = Adapter.request_device_sync()
        Records = (ShaderDirectory / 'PlumeRecords.wgsl').read_text()
        Pressure = (ShaderDirectory / 'PlumePressure.wgsl').read_text()
        Include = '//! include "PlumeRecords.wgsl"'
        assert Pressure.count(Include) == 1
        Program = self.Device.create_shader_module(code=Pressure.replace(Include, Records))
        self.Pipelines = {}
        for Entry in ('ResolveDivergence', 'RelaxPressure', 'ProjectVelocity'):
            self.Pipelines[Entry] = self.Device.create_compute_pipeline(
                layout='auto', compute={'module': Program, 'entry_point': Entry})

    def CreateStorageBuffer(self, Array, Usage):
        return self.Device.create_buffer_with_data(data=np.ascontiguousarray(Array).tobytes(), usage=Usage)

    def CreateVelocityTexture(self, Array):
        Usage = (wgpu.TextureUsage.COPY_SRC | wgpu.TextureUsage.COPY_DST | wgpu.TextureUsage.TEXTURE_BINDING |
                 wgpu.TextureUsage.STORAGE_BINDING)
        Texture = self.Device.create_texture(size=Extent, dimension='3d', format='rgba16float', usage=Usage)
        self.Device.queue.write_texture({'texture': Texture}, np.ascontiguousarray(Array).tobytes(),
                                        {'bytes_per_row': Width * 8, 'rows_per_image': Height}, Extent)
        return Texture

    def CreatePressureInputMap(self, Entry, Resources):
        Entries = []
        for Index, Resource in Resources.items():
            if isinstance(Resource, wgpu.GPUBuffer):
                Resource = {'buffer': Resource}
            Entries.append({'binding': Index, 'resource': Resource})
        return self.Device.create_bind_group(layout=self.Pipelines[Entry].get_bind_group_layout(0), entries=Entries)

    def DispatchPressureEntry(self, Encoder, Entry, Group):
        Compute = Encoder.begin_compute_pass()
        Compute.set_pipeline(self.Pipelines[Entry])
        Compute.set_bind_group(0, Group)
        Compute.dispatch_workgroups((Width+3)//4, (Height+3)//4, (Depth+3)//4)
        Compute.end()

    def ValidatePressureFixture(self, Label, Velocity, InitialPressure, Origin, Prior, Iterations):
        Velocity = Velocity.astype(np.float16)
        TextureArray = np.roll(Velocity, (Origin[1], Origin[0]), axis=(1, 2))
        PressureArray = np.roll(InitialPressure.astype(np.float32), (Origin[1], Origin[0]), axis=(1, 2))
        Packed = bytearray(256)
        struct.pack_into('<4i', Packed, 0, *Origin, 0)
        struct.pack_into('<4i', Packed, 16, *Prior, 0)
        struct.pack_into('<4I', Packed, 32, Width, Height, Depth, 0)
        struct.pack_into('<4I', Packed, 48, Width*2, Height*2, Depth*2, 0)
        struct.pack_into('<4f', Packed, 64, .5, 1, 0, .1)
        Uniform = self.Device.create_buffer_with_data(data=Packed, usage=wgpu.BufferUsage.UNIFORM)
        Storage = wgpu.BufferUsage.STORAGE | wgpu.BufferUsage.COPY_SRC | wgpu.BufferUsage.COPY_DST
        InputTexture = self.CreateVelocityTexture(TextureArray)
        OutputTexture = self.CreateVelocityTexture(np.zeros_like(TextureArray))
        InputView = InputTexture.create_view(dimension='3d')
        OutputView = OutputTexture.create_view(dimension='3d')
        Flux = self.CreateStorageBuffer(np.zeros((Depth, Height, Width), np.float32), Storage)
        First = self.CreateStorageBuffer(PressureArray, Storage)
        Second = self.CreateStorageBuffer(PressureArray, Storage)
        DivGroup = self.CreatePressureInputMap('ResolveDivergence',
                                               {0: Uniform, 1: InputView, 2: Flux, 3: First, 4: Second})
        Encoder = self.Device.create_command_encoder()
        self.DispatchPressureEntry(Encoder, 'ResolveDivergence', DivGroup)
        self.Device.queue.submit([Encoder.finish()])

        def ReadStorage(Buffer):
            Bytes = self.Device.queue.read_buffer(Buffer)
            return np.frombuffer(Bytes, dtype=np.float32).copy().reshape((Depth, Height, Width))

        GpuFlux = ReadStorage(Flux)
        GpuInitial = np.roll(ReadStorage(First), (-Origin[1], -Origin[0]), axis=(1, 2))
        GpuOtherInitial = np.roll(ReadStorage(Second), (-Origin[1], -Origin[0]), axis=(1, 2))
        X = Origin[0] + np.arange(Width)
        Y = Origin[1] + np.arange(Height)
        Reset = ((X[None, None, :] < Prior[0]) | (X[None, None, :] >= Prior[0]+Width) |
                 (Y[None, :, None] < Prior[1]) | (Y[None, :, None] >= Prior[1]+Height))
        Reset = np.broadcast_to(Reset, (Depth, Height, Width))
        ExpectedInitial = np.where(Reset, 0, InitialPressure).astype(np.float32)
        CpuFlux = EvaluateCenteredDivergence(Velocity)
        assert np.max(np.abs(GpuFlux - CpuFlux)) < 1e-6, 'divergence differs'
        assert np.max(np.abs(GpuInitial - ExpectedInitial)) < 1e-6, 'moving-window first pressure clearing differs'
        assert np.max(np.abs(GpuOtherInitial - ExpectedInitial)) < 1e-6, 'second pressure clearing differs'
        Solid = Velocity[:, :, :, 3] > .5
        CpuPressure = RelaxPressureCpu(ExpectedInitial, Solid, CpuFlux, Iterations)
        CpuProjection = ProjectVelocityCpu(Velocity, CpuPressure)
        RelaxGroups = [self.CreatePressureInputMap('RelaxPressure',
                                                   {0: Uniform, 1: InputView, 2: Flux, 3: First, 4: Second}),
                       self.CreatePressureInputMap('RelaxPressure',
                                                   {0: Uniform, 1: InputView, 2: Flux, 3: Second, 4: First})]
        Encoder = self.Device.create_command_encoder()
        for I in range(Iterations):
            self.DispatchPressureEntry(Encoder, 'RelaxPressure', RelaxGroups[I % 2])
        Final = First if Iterations % 2 == 0 else Second
        Projection = self.CreatePressureInputMap('ProjectVelocity', {0: Uniform, 1: InputView, 3: Final, 5: OutputView})
        self.DispatchPressureEntry(Encoder, 'ProjectVelocity', Projection)
        FluxAfter = self.CreateStorageBuffer(np.zeros((Depth, Height, Width), np.float32), Storage)
        ScratchA = self.CreateStorageBuffer(np.zeros((Depth, Height, Width), np.float32), Storage)
        ScratchB = self.CreateStorageBuffer(np.zeros((Depth, Height, Width), np.float32), Storage)
        DivAfter = self.CreatePressureInputMap('ResolveDivergence',
                                               {0: Uniform, 1: OutputView, 2: FluxAfter, 3: ScratchA, 4: ScratchB})
        self.DispatchPressureEntry(Encoder, 'ResolveDivergence', DivAfter)
        self.Device.queue.submit([Encoder.finish()])
        GpuPressure = np.roll(ReadStorage(Final), (-Origin[1], -Origin[0]), axis=(1, 2))
        Raw = self.Device.queue.read_texture({'texture': OutputTexture},
                                             {'bytes_per_row': Width * 8, 'rows_per_image': Height}, Extent)
        GpuVelocity = np.roll(np.frombuffer(Raw, dtype=np.float16).reshape((Depth, Height, Width, 4)),
                              (-Origin[1], -Origin[0]), axis=(1, 2)).copy()
        GpuAfter = ReadStorage(FluxAfter)
        PressureError = np.max(np.abs(GpuPressure - CpuPressure))
        VelocityError = np.max(np.abs(GpuVelocity.astype(np.float32) - CpuProjection.astype(np.float32)))
        AfterError = np.max(np.abs(GpuAfter - EvaluateCenteredDivergence(GpuVelocity)))
        print(f'{Label}: iterations={Iterations} reset={np.count_nonzero(Reset)} pressure_error={PressureError:.9g}'
              f' velocity_error={VelocityError:.9g} after_divergence_error={AfterError:.9g}', flush=True)
        assert PressureError < 2e-5 and VelocityError < 1e-3 and AfterError < 1e-6
        assert np.array_equal(GpuVelocity[Solid], Velocity[Solid])
        Interior = (slice(2, -2), slice(2, -2), slice(4, -4))
        NormBefore = np.linalg.norm(GpuFlux[Interior].astype(np.float64))
        NormAfter = np.linalg.norm(GpuAfter[Interior].astype(np.float64))
        Adjacent = GatherPressureNeighbours(GpuPressure, Solid)
        Residual = sum(Adjacent) - 6 * GpuPressure - GpuFlux
        ResidualNorm = np.linalg.norm(Residual[Interior].astype(np.float64))
        Ratio = NormAfter / NormBefore if NormBefore else 0
        PressureResidual = ResidualNorm / NormBefore if NormBefore else 0
        return Ratio, PressureResidual, GpuPressure, GpuVelocity, np.count_nonzero(Reset)


#-----------------------------------------------------------------------------------------------------------------------
#                                                PRESSURE FIXTURES
#-----------------------------------------------------------------------------------------------------------------------


def Main():
    Submission = PlumePressureSubmission()
    Shape = (Depth, Height, Width)
    Origin = (0, 0, 0)

    # 📝 One sweep has simple independent boundary expectations for a constant pressure field.
    Velocity = np.zeros((*Shape, 4), dtype=np.float32)
    Velocity[3, 3, 10, 3] = 1
    _, _, BoundaryPressure, _, _ = Submission.ValidatePressureFixture(
        'BOUNDARY', Velocity, np.ones(Shape), Origin, Origin, 1)
    BoundaryProbes = [(0, 4, 8, 1), (7, 4, 8, 5/6), (4, 4, 0, 5/6),
                      (0, 0, 0, 4/6), (3, 3, 9, 1), (3, 3, 10, 1)]
    for Z, Y, X, Expected in BoundaryProbes:
        assert abs(BoundaryPressure[Z, Y, X] - Expected) < 1e-6, 'hand-computed boundary mismatch'
    print('BOUNDARY hand-computed ground/open/corner/solid probes=6', flush=True)

    Velocity = np.zeros((*Shape, 4), dtype=np.float32)
    Velocity[:, :, :, 0] = .7
    Velocity[:, :, :, 1] = -.2
    Velocity[:, :, :, 2] = .1
    Velocity[2:6, 2:6, 12:16, :] = 0
    Velocity[2:6, 2:6, 12:16, 3] = 1
    Ratio, _, _, _, _ = Submission.ValidatePressureFixture('SOLID', Velocity, np.zeros(Shape), Origin, Origin, 64)
    print(f'SOLID interior divergence ratio={Ratio:.9g}', flush=True)
    Pressure = np.broadcast_to(np.sin(np.arange(Width)[None, None, :] * .3) * .25, Shape).copy().astype(np.float32)
    Ratio, _, _, _, ResetCount = Submission.ValidatePressureFixture(
        'SHIFT', Velocity, Pressure, (-7, 3, 0), (-8, 3, 0), 48)
    assert ResetCount == Height * Depth
    print(f'SHIFT interior divergence ratio={Ratio:.9g}', flush=True)

    Ratios = []
    for Mode in (1, 15):
        Velocity = np.zeros((*Shape, 4), dtype=np.float32)
        Velocity[:, :, :, 0] = np.sin(2 * math.pi * Mode * (np.arange(Width) + .5) / Width)[None, None, :]
        Ratio, Residual, _, _, _ = Submission.ValidatePressureFixture(
            f'MODE_{Mode}', Velocity, np.zeros(Shape), Origin, Origin, 256)
        print(f'MODE_{Mode} interior divergence ratio={Ratio:.9g} pressure_residual_ratio={Residual:.9g}', flush=True)
        Ratios.append(Ratio)
    assert Ratios[0] < Ratios[1] and Ratios[1] > .8

    Velocity = np.zeros((*Shape, 4), dtype=np.float32)
    Velocity[:, :, :, 0] = 50
    _, _, _, ClampedVelocity, _ = Submission.ValidatePressureFixture(
        'SPEED', Velocity, np.zeros(Shape), Origin, Origin, 1)
    assert np.all(ClampedVelocity[:, :, :, 0] == 40)
    print('SPEED unoccluded 50 m/s field clamped to 40 m/s', flush=True)
    print('PASS unchanged demo pressure WGSL compared with independent CPU stencils', flush=True)

if __name__ == '__main__':
    Main()
