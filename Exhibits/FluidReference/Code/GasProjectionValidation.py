#!/usr/bin/env python3
#==========================================================================================================================================
#                                               GASPROJECTIONVALIDATION.PY
#==========================================================================================================================================
# 🧩 Exercise Engine Slang face-flux pressure projection on Vulkan with analytical and independent matrix oracles.
import hashlib
import math
import struct
import subprocess
import sys
from pathlib import Path

import numpy as np
import wgpu


def CpuDivergence(Velocity, Spacing):
    Solid = Velocity[..., 3] > 0.5
    Result = np.zeros(Solid.shape, dtype=np.float64)
    for Component, Axis in enumerate((2, 1, 0)):
        Lower = [slice(None)] * 3
        Upper = [slice(None)] * 3
        Lower[Axis], Upper[Axis] = slice(None, -1), slice(1, None)
        Lower, Upper = tuple(Lower), tuple(Upper)
        Open = ~(Solid[Lower] | Solid[Upper])
        Flux = np.where(Open, Velocity[Lower + (Component,)], 0).astype(np.float64)
        Result[Lower] += Flux / Spacing
        Result[Upper] -= Flux / Spacing
    return Result


def CpuGradient(Pressure, Solid, Spacing):
    Result = np.zeros((*Solid.shape, 3), dtype=np.float64)
    for Component, Axis in enumerate((2, 1, 0)):
        Lower = [slice(None)] * 3
        Upper = [slice(None)] * 3
        Lower[Axis], Upper[Axis] = slice(None, -1), slice(1, None)
        Lower, Upper = tuple(Lower), tuple(Upper)
        Open = ~(Solid[Lower] | Solid[Upper])
        Result[Lower + (Component,)] = np.where(Open, (Pressure[Upper] - Pressure[Lower]) / Spacing, 0)
    return Result


def ConstructPressureMatrix(Solid, Spacing):
    Cells = list(zip(*np.nonzero(~Solid)))
    Number = {Cell: Index for Index, Cell in enumerate(Cells)}
    Matrix = np.zeros((len(Cells), len(Cells)), dtype=np.float64)
    for Cell, Row in Number.items():
        for Axis in range(3):
            for Direction in (-1, 1):
                Adjacent = list(Cell)
                Adjacent[Axis] += Direction
                Adjacent = tuple(Adjacent)
                if Adjacent in Number:
                    Matrix[Row, Number[Adjacent]] += 1 / (Spacing * Spacing)
                    Matrix[Row, Row] -= 1 / (Spacing * Spacing)
    return Cells, Matrix


class GasProjectionSubmission:
    def __init__(self, Validator, Directory):
        self.Validator = Path(Validator).resolve()
        Adapter = wgpu.gpu.request_adapter_sync(power_preference='low-power')
        self.Device = Adapter.request_device_sync()
        self.Pipelines = {}
        print(f'GAS_ADAPTER {Adapter.info}', flush=True)
        for Entry in ('MeasureGasDivergence', 'RelaxGasPressure', 'ProjectGasVelocity'):
            # 📝 wgpu imports Slang-generated SPIR-V via Naga; this is not a raw-SPIR-V Vulkan host.
            Module = self.Device.create_shader_module(code=(Path(Directory) / (Entry + '.spv')).read_bytes())
            self.Pipelines[Entry] = self.Device.create_compute_pipeline(
                layout='auto', compute={'module': Module, 'entry_point': 'main'})

    def Buffer(self, Data, Uniform=False):
        Usage = (wgpu.BufferUsage.UNIFORM if Uniform else wgpu.BufferUsage.STORAGE) | \
                wgpu.BufferUsage.COPY_SRC | wgpu.BufferUsage.COPY_DST
        return self.Device.create_buffer_with_data(data=np.ascontiguousarray(Data), usage=Usage)

    def Footprint(self, Shape, Spacing, Weight=2/3, Checked=True):
        Z, Y, X = Shape
        Packed = struct.pack('<4I4f', X, Y, Z, 0, Spacing, Weight, 0, 0)
        if Checked:
            Echo = subprocess.run([str(self.Validator), '--footprint'], input=Packed,
                                  capture_output=True, check=True).stdout
            if Echo != Packed:
                raise RuntimeError('Native footprint validation changed input bytes')
        return self.Buffer(np.frombuffer(Packed, dtype=np.uint8), Uniform=True)

    def Group(self, Entry, Bindings):
        return self.Device.create_bind_group(
            layout=self.Pipelines[Entry].get_bind_group_layout(0),
            entries=[{'binding': Index, 'resource': {'buffer': Buffer}} for Index, Buffer in Bindings.items()])

    def Dispatch(self, Encoder, Entry, Group, Shape):
        Compute = Encoder.begin_compute_pass()
        Compute.set_pipeline(self.Pipelines[Entry])
        Compute.set_bind_group(0, Group)
        Z, Y, X = Shape
        Compute.dispatch_workgroups((X+3)//4, (Y+3)//4, (Z+3)//4)
        Compute.end()

    def Read(self, Buffer, Shape):
        return np.frombuffer(self.Device.queue.read_buffer(Buffer), dtype=np.float32).copy().reshape(Shape)

    def Execute(self, Velocity, Spacing, Sweeps, Weight=2/3):
        Velocity = np.asarray(Velocity, dtype=np.float32)
        Shape = Velocity.shape[:3]
        Parameters = self.Footprint(Shape, Spacing, Weight)
        Initial = self.Buffer(Velocity)
        Projected = self.Buffer(np.zeros_like(Velocity))
        InitialDivergence = self.Buffer(np.zeros(Shape, dtype=np.float32))
        FinalDivergence = self.Buffer(np.zeros(Shape, dtype=np.float32))
        PressureA = self.Buffer(np.zeros(Shape, dtype=np.float32))
        PressureB = self.Buffer(np.zeros(Shape, dtype=np.float32))
        Measure = self.Group('MeasureGasDivergence', {0: Parameters, 1: Initial, 2: InitialDivergence})
        Relax = [self.Group('RelaxGasPressure', {0: Parameters, 1: Initial, 2: InitialDivergence,
                                                3: PressureA, 4: PressureB}),
                 self.Group('RelaxGasPressure', {0: Parameters, 1: Initial, 2: InitialDivergence,
                                                3: PressureB, 4: PressureA})]
        FinalPressure = PressureA if Sweeps % 2 == 0 else PressureB
        Project = self.Group('ProjectGasVelocity', {0: Parameters, 1: Initial, 3: FinalPressure, 5: Projected})
        MeasureAfter = self.Group('MeasureGasDivergence', {0: Parameters, 1: Projected, 2: FinalDivergence})
        Encoder = self.Device.create_command_encoder()
        self.Dispatch(Encoder, 'MeasureGasDivergence', Measure, Shape)
        for Sweep in range(Sweeps):
            self.Dispatch(Encoder, 'RelaxGasPressure', Relax[Sweep % 2], Shape)
        self.Dispatch(Encoder, 'ProjectGasVelocity', Project, Shape)
        self.Dispatch(Encoder, 'MeasureGasDivergence', MeasureAfter, Shape)
        self.Device.queue.submit([Encoder.finish()])
        Before = self.Read(InitialDivergence, Shape)
        After = self.Read(FinalDivergence, Shape)
        VelocityAfter = self.Read(Projected, (*Shape, 4))
        PressureAfter = self.Read(FinalPressure, Shape)
        CpuBefore = CpuDivergence(Velocity, Spacing)
        CpuAfter = CpuDivergence(VelocityAfter, Spacing)
        BeforeError = np.max(np.abs(Before - CpuBefore))
        AfterError = np.max(np.abs(After - CpuAfter))
        if BeforeError > 3e-6 or AfterError > 3e-6:
            raise RuntimeError(f'Independent face divergence differs {BeforeError=} {AfterError=}')
        if not np.isfinite(Before).all() or not np.isfinite(After).all() or not np.isfinite(VelocityAfter).all():
            raise RuntimeError('Nonfinite gas projection')
        return Before, After, VelocityAfter, PressureAfter

    def GuardChecks(self):
        Shape = (2, 3, 5)  # Rounded-up dispatch plus non-workgroup-aligned dimensions.
        Count = math.prod(Shape)
        Parameters = self.Footprint(Shape, 1.0)
        Velocity = self.Buffer(np.zeros((Count, 4), dtype=np.float32))
        LongTarget = self.Buffer(np.full(Count + 7, 19, dtype=np.float32))
        Group = self.Group('MeasureGasDivergence', {0: Parameters, 1: Velocity, 2: LongTarget})
        Encoder = self.Device.create_command_encoder()
        self.Dispatch(Encoder, 'MeasureGasDivergence', Group, Shape)
        self.Device.queue.submit([Encoder.finish()])
        Value = self.Read(LongTarget, (Count + 7,))
        if not (np.all(Value[:Count] == 0) and np.all(Value[Count:] == 19)):
            raise RuntimeError('Rounded dispatch touched storage tail')
        ShortVelocity = self.Buffer(np.zeros((Count - 1, 4), dtype=np.float32))
        Target = self.Buffer(np.full(Count, 23, dtype=np.float32))
        Group = self.Group('MeasureGasDivergence', {0: Parameters, 1: ShortVelocity, 2: Target})
        Encoder = self.Device.create_command_encoder()
        self.Dispatch(Encoder, 'MeasureGasDivergence', Group, Shape)
        self.Device.queue.submit([Encoder.finish()])
        if not np.all(self.Read(Target, (Count,)) == 23):
            raise RuntimeError('Short velocity buffer touched divergence output')
        Invalid = self.Footprint(Shape, 1.0, Weight=1.0, Checked=False)
        Pressure = self.Buffer(np.zeros(Count, dtype=np.float32))
        SourceDivergence = self.Buffer(np.zeros(Count, dtype=np.float32))
        PressureTarget = self.Buffer(np.full(Count, 29, dtype=np.float32))
        Group = self.Group('RelaxGasPressure', {0: Invalid, 1: Velocity, 2: SourceDivergence,
                                                3: Pressure, 4: PressureTarget})
        Encoder = self.Device.create_command_encoder()
        self.Dispatch(Encoder, 'RelaxGasPressure', Group, Shape)
        self.Device.queue.submit([Encoder.finish()])
        if not np.all(self.Read(PressureTarget, (Count,)) == 29):
            raise RuntimeError('Invalid relaxation touched pressure output')
        ShortPressure = self.Buffer(np.zeros(Count - 1, dtype=np.float32))
        Projected = self.Buffer(np.full((Count, 4), 31, dtype=np.float32))
        Group = self.Group('ProjectGasVelocity', {0: Parameters, 1: Velocity, 3: ShortPressure, 5: Projected})
        Encoder = self.Device.create_command_encoder()
        self.Dispatch(Encoder, 'ProjectGasVelocity', Group, Shape)
        self.Device.queue.submit([Encoder.finish()])
        if not np.all(self.Read(Projected, (Count, 4)) == 31):
            raise RuntimeError('Short pressure buffer touched projected velocity')
        print('GAS_PROJECTION_GUARDS rounded_tail=1 short_velocity=1 invalid_weight=1 short_pressure=1', flush=True)


def TestEigenmode(Submission, Width, Mode, Sweeps, Digest):
    Shape, Spacing, Weight = (4, 4, Width), 1.0, 2/3
    Position = np.arange(Width, dtype=np.float64)
    Potential = np.cos(math.pi * Mode * (Position + .5) / Width)
    Velocity = np.zeros((*Shape, 4), dtype=np.float32)
    Velocity[..., :-1, 0] = np.diff(Potential).astype(np.float32)[None, None, :]
    Before, After, Projected, Pressure = Submission.Execute(Velocity, Spacing, Sweeps, Weight)
    Ratio = float(np.linalg.norm(After.astype(np.float64)) / np.linalg.norm(Before.astype(np.float64)))
    Decay = 1.0 - Weight * 4.0 * math.sin(math.pi * Mode / (2.0 * Width))**2 / 6.0
    Expected = Decay**Sweeps
    if abs(Ratio - Expected) > 3e-5:
        raise RuntimeError(f'Analytic eigenmode attenuation mismatch {Ratio=} {Expected=}')
    if np.linalg.norm(Projected[..., :3].astype(np.float64)) > np.linalg.norm(Velocity[..., :3].astype(np.float64)) + 1e-5:
        raise RuntimeError('Projection increased manufactured-flow kinetic norm')
    Digest.update(Before.tobytes()); Digest.update(After.tobytes()); Digest.update(Projected.tobytes()); Digest.update(Pressure.tobytes())
    print(f'GAS_MODE width={Width} mode={Mode} sweeps={Sweeps} observed_divergence_ratio={Ratio:.9g} '
          f'analytic_ratio={Expected:.9g}', flush=True)


def TestMatrix(Submission, Digest):
    Shape, Spacing = (4, 4, 4), .75
    Random = np.random.default_rng(43031)
    Velocity = np.zeros((*Shape, 4), dtype=np.float32)
    Velocity[..., :3] = Random.normal(0, .5, (*Shape, 3))
    Velocity[1:3, 1:3, 1:3, 3] = 1  # One static, enclosed solid block.
    Solid = Velocity[..., 3] > .5
    Before, After, Projected, Pressure = Submission.Execute(Velocity, Spacing, 1024)
    Cells, Matrix = ConstructPressureMatrix(Solid, Spacing)
    Right = np.array([CpuDivergence(Velocity, Spacing)[Cell] for Cell in Cells])
    Exact, _, Rank, _ = np.linalg.lstsq(Matrix, Right, rcond=None)
    if Rank != len(Cells)-1 or abs(float(np.sum(Right))) > 1e-5:
        raise RuntimeError('Closed matrix should have one constant-pressure nullspace and balanced flux')
    ExactPressure = np.zeros(Shape, dtype=np.float64)
    for Cell, Value in zip(Cells, Exact): ExactPressure[Cell] = Value
    Expected = np.zeros((*Shape, 3), dtype=np.float64)
    for Component, Axis in enumerate((2, 1, 0)):
        Lower = [slice(None)] * 3; Upper = [slice(None)] * 3
        Lower[Axis], Upper[Axis] = slice(None, -1), slice(1, None)
        Lower, Upper = tuple(Lower), tuple(Upper)
        Open = ~(Solid[Lower] | Solid[Upper])
        Expected[Lower + (Component,)] = np.where(Open, Velocity[Lower + (Component,)], 0)
    Expected -= CpuGradient(ExactPressure, Solid, Spacing)
    Error = float(np.max(np.abs(Projected[..., :3][~Solid] - Expected[~Solid])))
    Ratio = float(np.linalg.norm(After.astype(np.float64)) / np.linalg.norm(Before.astype(np.float64)))
    if Error > 1e-5 or Ratio > 1e-5 or not np.array_equal(Projected[Solid], Velocity[Solid]):
        raise RuntimeError(f'Independent least-squares projection failed {Error=} {Ratio=}')
    Digest.update(Before.tobytes()); Digest.update(After.tobytes()); Digest.update(Projected.tobytes()); Digest.update(Pressure.tobytes())
    print(f'GAS_MATRIX cells={len(Cells)} solid={int(np.sum(Solid))} matrix_rank={Rank} '
          f'pressure_gauge_dimension=1 divergence_ratio={Ratio:.9g} velocity_max_error={Error:.9g}', flush=True)


def TestDisconnected(Submission, Digest):
    Shape = (3, 3, 5)
    Velocity = np.zeros((*Shape, 4), dtype=np.float32)
    Velocity[..., 3] = 1
    for X in (0, 1, 3, 4): Velocity[1, 1, X, 3] = 0
    Velocity[1, 1, 0, 0], Velocity[1, 1, 3, 0] = .75, -.40
    Before, After, Projected, Pressure = Submission.Execute(Velocity, 1.0, 128)
    Cells, Matrix = ConstructPressureMatrix(Velocity[..., 3] > .5, 1.0)
    Rank = int(np.linalg.matrix_rank(Matrix))
    Ratio = float(np.linalg.norm(After.astype(np.float64)) / np.linalg.norm(Before.astype(np.float64)))
    if len(Cells) != 4 or Rank != 2 or Ratio > 1e-5 or np.max(np.abs(Projected[..., :3])) > 1e-5:
        raise RuntimeError(f'Disconnected closed regions failed {Rank=} {Ratio=}')
    Digest.update(Before.tobytes()); Digest.update(After.tobytes()); Digest.update(Projected.tobytes()); Digest.update(Pressure.tobytes())
    print(f'GAS_DISCONNECTED fluid_cells=4 components=2 matrix_rank={Rank} divergence_ratio={Ratio:.9g}', flush=True)

    SolidOnly = np.zeros((2, 2, 2, 4), dtype=np.float32)
    SolidOnly[..., 0], SolidOnly[..., 3] = .3, 1
    Before, After, Projected, Pressure = Submission.Execute(SolidOnly, 1.0, 2)
    if np.any(Before) or np.any(After) or np.any(Pressure) or not np.array_equal(Projected, SolidOnly):
        raise RuntimeError('All-solid closed field was not preserved')
    Digest.update(Before.tobytes()); Digest.update(After.tobytes()); Digest.update(Projected.tobytes()); Digest.update(Pressure.tobytes())
    print('GAS_SOLID_ONLY cells=8 pressure_zero=1 velocity_preserved=1', flush=True)


def Main():
    if len(sys.argv) != 3:
        raise RuntimeError('Expected native footprint validator and compiled SPIR-V directory')
    Submission = GasProjectionSubmission(sys.argv[1], sys.argv[2])
    Submission.GuardChecks()
    Digest = hashlib.sha256()
    TestEigenmode(Submission, 16, 1, 1024, Digest)
    TestEigenmode(Submission, 32, 31, 128, Digest)
    TestMatrix(Submission, Digest)
    TestDisconnected(Submission, Digest)
    print(f'GAS_PROJECTION_READBACK_SHA256 {Digest.hexdigest()}', flush=True)
    print('GAS_PROJECTION_SUMMARY PASS closed_static_solids=1 cell_centered_advection_integrated=0', flush=True)


if __name__ == '__main__':
    Main()
