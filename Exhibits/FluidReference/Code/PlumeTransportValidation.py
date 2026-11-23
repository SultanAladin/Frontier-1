#!/usr/bin/env python3
#==========================================================================================================================================
#                                              PLUMETRANSPORTVALIDATION.PY
#==========================================================================================================================================
# 🧩 Execute retained plume advection, vorticity and light WGSL against bounded analytic fixtures.

import hashlib
import struct
import subprocess
import sys
from pathlib import Path

import numpy as np
import wgpu

Root = Path(__file__).resolve().parents[3]
ShaderRoot = Root / 'Experimental/VehicleHmiReference/Shader'
Coarse = (8, 8, 8)
Fine = (16, 16, 16)

def ConstructPrograms(Device):
    Files = {
        'PlumeRecords.wgsl': 'be2f043db792f5dd45692c65d761b2e58892be4f',
        'PlumeAdvection.wgsl': '4c0b128800bf63f3e9c048f48ad089c534316cb5',
        'PlumeVorticity.wgsl': '3fa7354077e5b8f239099289dd2d4702253cafd7',
        'PlumePressure.wgsl': '3ac6350519526ec2584e1f4fceadfafa19eac573',
        'SceneRecords.wgsl': 'e49e8f269ab8c46ea6ef35136e1092edbc0dc6da',
        'DistanceRecords.wgsl': '0bee46f9ba11725592d21521533891aa6b8e36fd',
        'DistanceTraversal.wgsl': 'fdda8728de49a2c7ffc445f337f351a2338d18c0',
    }
    Source = {}
    for Name, Blob in Files.items():
        PathName = ShaderRoot / Name
        Actual = subprocess.check_output(['git', 'hash-object', str(PathName)], cwd=Root, text=True).strip()
        if Actual != Blob:
            raise RuntimeError(f'{Name} differs from the pinned reference')
        Source[Name] = PathName.read_text()
        print(f'REFERENCE {Name} sha256={hashlib.sha256(PathName.read_bytes()).hexdigest()}', flush=True)
    Advection = Source['PlumeAdvection.wgsl']
    Vorticity = Source['PlumeVorticity.wgsl']
    for Token in ('//! include "PlumeRecords.wgsl"', '//! include "SceneRecords.wgsl"',
                  '//! include "DistanceTraversal.wgsl"'):
        if Advection.count(Token) != 1:
            raise RuntimeError(f'Unexpected advection include {Token}')
    if Vorticity.count('//! include "PlumeRecords.wgsl"') != 1 or \
       Source['PlumePressure.wgsl'].count('//! include "PlumeRecords.wgsl"') != 1:
        raise RuntimeError('Unexpected vorticity or pressure include')
    Traversal = Source['DistanceTraversal.wgsl']
    if Traversal.count('//! include "DistanceRecords.wgsl"') != 1:
        raise RuntimeError('Unexpected distance traversal include')
    Traversal = Traversal.replace('//! include "DistanceRecords.wgsl"', Source['DistanceRecords.wgsl'])
    Advection = Advection.replace('//! include "PlumeRecords.wgsl"', Source['PlumeRecords.wgsl'])
    Advection = Advection.replace('//! include "SceneRecords.wgsl"', Source['SceneRecords.wgsl'])
    Advection = Advection.replace('//! include "DistanceTraversal.wgsl"', Traversal)
    # 📝 The included traversal requires this compilation constant; plume advection calls
    #    SampleSceneDistance, not either ray traversal routine that consumes it.
    Advection = 'const MaximumTraversalSteps: u32 = 256u;\n' + Advection
    Vorticity = Vorticity.replace('//! include "PlumeRecords.wgsl"', Source['PlumeRecords.wgsl'])
    Pressure = Source['PlumePressure.wgsl'].replace('//! include "PlumeRecords.wgsl"', Source['PlumeRecords.wgsl'])
    Modules = [Device.create_shader_module(code=Advection), Device.create_shader_module(code=Vorticity),
               Device.create_shader_module(code=Pressure)]
    Entries = [('AdvectVelocity', 0), ('AdvectThermo', 0), ('ResolveSunTransmittance', 0),
               ('ResolveCurl', 1), ('ConfineVorticity', 1), ('ResolveDivergence', 2),
               ('RelaxPressure', 2), ('ProjectVelocity', 2)]
    return {Name: Device.create_compute_pipeline(layout='auto', compute={'module': Modules[Index], 'entry_point': Name})
            for Name, Index in Entries}


def MakeUniform(Seconds=1/60, Buoyancy=0.0, Vorticity=0.0, Wind=(0, 0, 0), Extinction=0.5):
    Packed = bytearray(256)
    struct.pack_into('<4i', Packed, 0, 0, 0, 0, 0)
    struct.pack_into('<4i', Packed, 16, 0, 0, 0, 0)
    struct.pack_into('<4I', Packed, 32, *Coarse, 0)
    struct.pack_into('<4I', Packed, 48, *Fine, 0)
    struct.pack_into('<4f', Packed, 64, 0.5, 1.0, 0.0, Seconds)
    struct.pack_into('<4f', Packed, 80, Buoyancy, 0.0, Vorticity, 0.0)
    struct.pack_into('<4f', Packed, 96, 1.0, 1.0, 1.0, 1.0)
    struct.pack_into('<4f', Packed, 112, *Wind, 0.0)
    struct.pack_into('<4f', Packed, 128, 0.0, 0.0, 0.0, 0.1)
    struct.pack_into('<4f', Packed, 160, Extinction, 0.9, 0.0, 8.0)
    struct.pack_into('<4f', Packed, 176, 0.0, 0.0, 1.0, 0.1)
    struct.pack_into('<4f', Packed, 192, 0.0, 0.0, 1.0, 0.0)
    return Packed


class PlumeSubmission:
    def __init__(self, Probe):
        self.Probe = Path(Probe).resolve()
        self.ValidatedLattices = 0
        self.ValidatedSources = 0
        self.ReadbackDigest = hashlib.sha256()
        self.Adapter = wgpu.gpu.request_adapter_sync(power_preference='low-power')
        print('ADAPTER', dict(self.Adapter.info), flush=True)
        self.Device = self.Adapter.request_device_sync()
        self.Pipelines = ConstructPrograms(self.Device)
        self.Sampler = self.Device.create_sampler(mag_filter='linear', min_filter='linear',
                                                  address_mode_u='repeat', address_mode_v='repeat',
                                                  address_mode_w='clamp-to-edge')
        self.Sources = self.Buffer(np.zeros(12, dtype=np.float32), wgpu.BufferUsage.STORAGE)
        self.Occupants = self.Buffer(np.zeros(84, dtype=np.uint32), wgpu.BufferUsage.STORAGE)
        self.OpenScene = self.DistanceScene(0)

    def Buffer(self, Array, Usage):
        return self.Device.create_buffer_with_data(data=np.ascontiguousarray(Array).tobytes(),
                                                   usage=Usage | wgpu.BufferUsage.COPY_SRC | wgpu.BufferUsage.COPY_DST)

    def Uniform(self, Packed):
        if len(Packed) == 256:
            Validated = subprocess.check_output([str(self.Probe), '--lattice'], input=bytes(Packed))
            if Validated != bytes(Packed):
                raise RuntimeError('Engine plume lattice ABI changed input bytes')
            Packed = Validated
            self.ValidatedLattices += 1
        return self.Device.create_buffer_with_data(data=Packed, usage=wgpu.BufferUsage.UNIFORM)

    def SourceBuffer(self, Source):
        Packed = np.ascontiguousarray(Source, dtype=np.float32).tobytes()
        Validated = subprocess.check_output([str(self.Probe), '--sources'], input=Packed)
        if Validated != Packed:
            raise RuntimeError('Engine plume source ABI changed input bytes')
        self.ValidatedSources += len(Packed) // 48
        return self.Buffer(np.frombuffer(Validated, dtype=np.uint8), wgpu.BufferUsage.STORAGE)

    def Texture(self, Array):
        Array = np.ascontiguousarray(Array, dtype=np.float16)
        Depth, Height, Width, Channels = Array.shape
        if Channels != 4:
            raise RuntimeError('Expected rgba16float texture')
        Usage = (wgpu.TextureUsage.COPY_SRC | wgpu.TextureUsage.COPY_DST |
                 wgpu.TextureUsage.TEXTURE_BINDING | wgpu.TextureUsage.STORAGE_BINDING)
        Texture = self.Device.create_texture(size=(Width, Height, Depth), dimension='3d', format='rgba16float', usage=Usage)
        self.Device.queue.write_texture({'texture': Texture}, Array.tobytes(),
                                        {'bytes_per_row': Width * 8, 'rows_per_image': Height}, (Width, Height, Depth))
        return Texture

    def View(self, Texture):
        return Texture.create_view(dimension='3d')

    def DistanceScene(self, ClipCount):
        Packed = bytearray(160)
        # 📝 One constant negative-distance cascade starts at voxel (2,2,2); count zero uses the analytic plane.
        struct.pack_into('<3iI4f', Packed, 0, 2, 2, 2, 16, .5, 4.0, 1.0 / 8.0, 0.0)
        struct.pack_into('<IIffIIII', Packed, 128, ClipCount, 1, -100.0, .5, 0, 0, 2, 0)
        Field = np.zeros((16, 16, 16, 4), dtype=np.float16)
        Field[..., 0] = -1
        Field[..., 1] = 0  # occupant identity
        Clip = self.Texture(Field)
        return {0: self.Uniform(Packed), 1: self.View(Clip), 2: self.View(Clip), 3: self.View(Clip),
                4: self.View(Clip), 5: self.Sampler}

    def ReadTexture(self, Texture, Extent):
        Width, Height, Depth = Extent
        Raw = self.Device.queue.read_texture({'texture': Texture},
                                             {'bytes_per_row': Width * 8, 'rows_per_image': Height}, Extent)
        Result = np.frombuffer(Raw, dtype=np.float16).copy().reshape(Depth, Height, Width, 4)
        self.ReadbackDigest.update(Result.tobytes())
        return Result

    def Group(self, Entry, Resources, Index=0):
        Entries = []
        for Binding, Resource in Resources.items():
            if isinstance(Resource, wgpu.GPUBuffer):
                Resource = {'buffer': Resource}
            Entries.append({'binding': Binding, 'resource': Resource})
        return self.Device.create_bind_group(layout=self.Pipelines[Entry].get_bind_group_layout(Index), entries=Entries)

    def Encode(self, Encoder, Entry, Resources, Extent, Scene=None):
        Group = self.Group(Entry, Resources)
        SceneGroup = None
        if Entry in ('AdvectVelocity', 'AdvectThermo'):
            SceneGroup = self.Group(Entry, self.OpenScene if Scene is None else Scene, 1)
        Compute = Encoder.begin_compute_pass()
        Compute.set_pipeline(self.Pipelines[Entry])
        Compute.set_bind_group(0, Group)
        if SceneGroup is not None:
            Compute.set_bind_group(1, SceneGroup)
        Compute.dispatch_workgroups(*[(Value + 3) // 4 for Value in Extent])
        Compute.end()

    def Submit(self, Entry, Resources, Extent, Scene=None):
        Encoder = self.Device.create_command_encoder()
        self.Encode(Encoder, Entry, Resources, Extent, Scene)
        self.Device.queue.submit([Encoder.finish()])

    def Coverage(self):
        return self.Buffer(np.array([2147483647, 2147483647, 2147483647, 0,
                                     -2147483647, -2147483647, -2147483647, 0], dtype=np.int32),
                           wgpu.BufferUsage.STORAGE)


def ValidateJavaScriptFootprints(Submission):
    Data = subprocess.check_output(['node', 'Exhibits/FluidReference/Code/PlumeLatticeReference.mjs'], cwd=Root)
    if len(Data) != 4 * 256:
        raise RuntimeError('Unexpected retained JavaScript plume footprint count')
    Extents = [(64, 64, 20), (96, 96, 24), (128, 128, 32), (160, 160, 48)]
    Edges = [.4, .3, .25, .2]
    for Index in range(4):
        Footprint = Data[Index * 256:(Index + 1) * 256]
        Validated = subprocess.check_output([str(Submission.Probe), '--lattice'], input=Footprint)
        if Validated != Footprint:
            raise RuntimeError('Engine footprint does not preserve JavaScript bytes')
        Coarse = struct.unpack_from('<3I', Footprint, 32)
        Fine = struct.unpack_from('<3I', Footprint, 48)
        FineEdge, CoarseEdge, Floor, Seconds = struct.unpack_from('<4f', Footprint, 64)
        if Fine != Extents[Index] or Coarse != tuple(Value // 2 for Value in Fine) or \
           abs(FineEdge - Edges[Index]) > 1e-6 or abs(CoarseEdge - 2 * Edges[Index]) > 1e-6 or \
           abs(Floor + .5) > 1e-6 or abs(Seconds - 1/60) > 1e-6:
            raise RuntimeError('JavaScript footprint field offsets or grade values disagree')
    print(f'PLUME_FOOTPRINT_JS grades=4 bytes={len(Data)} sha256={hashlib.sha256(Data).hexdigest()} exact_roundtrip=1', flush=True)


def ValidateBuoyancy(Submission):
    Velocity = np.zeros((*Coarse[::-1], 4), dtype=np.float16)
    Thermo = np.zeros((*Fine[::-1], 4), dtype=np.float16)
    Thermo[..., 1] = 40
    VelocityTexture, ThermoTexture = Submission.Texture(Velocity), Submission.Texture(Thermo)
    Output = Submission.Texture(Velocity)
    Uniform = Submission.Uniform(MakeUniform(Seconds=1/30, Buoyancy=2.4))
    Submission.Submit('AdvectVelocity', {0: Uniform, 1: Submission.View(VelocityTexture), 2: Submission.View(ThermoTexture),
                                          3: Submission.Sampler, 4: Submission.View(Output), 5: Submission.Sources,
                                          7: Submission.Occupants}, Coarse)
    Actual = Submission.ReadTexture(Output, Coarse).astype(np.float32)
    Expected = float(np.float16(2.4 * 40 / (1 + .125 * 40) / 30))
    Error = float(np.max(np.abs(Actual[..., 2] - Expected)))
    if Error > 0.002 or np.any(Actual[..., :2] != 0) or np.any(Actual[..., 3] != 0):
        raise RuntimeError(f'Buoyancy differs from analytic uniform field: {Error}')
    print(f'VELOCITY_BUOYANCY cells={np.prod(Coarse)} expected_z={Expected:.9g} max_error={Error:.9g}', flush=True)


def ValidateMovingSolid(Submission):
    Velocity = np.zeros((*Coarse[::-1], 4), dtype=np.float16)
    Thermo = np.zeros((*Fine[::-1], 4), dtype=np.float16)
    Thermo[..., 0] = 1
    VelA, VelB = Submission.Texture(Velocity), Submission.Texture(Velocity)
    HeatA, HeatB = Submission.Texture(Thermo), Submission.Texture(np.zeros_like(Thermo))
    Occupant = bytearray(336)
    Identity = [1.0 if I % 5 == 0 else 0.0 for I in range(16)]
    for Offset in (0, 64, 128, 192):
        struct.pack_into('<16f', Occupant, Offset, *Identity)
    struct.pack_into('<f', Occupant, 128 + 12 * 4, -.2)
    struct.pack_into('<I', Occupant, 268, 8)  # RoleDynamic
    OccupantBuffer = Submission.Buffer(np.frombuffer(Occupant, dtype=np.uint8), wgpu.BufferUsage.STORAGE)
    Uniform = Submission.Uniform(MakeUniform(Seconds=1/30))
    Scene = Submission.DistanceScene(1)
    Coverage = Submission.Coverage()
    Encoder = Submission.Device.create_command_encoder()
    Submission.Encode(Encoder, 'AdvectVelocity', {0: Uniform, 1: Submission.View(VelA), 2: Submission.View(HeatA),
                                                    3: Submission.Sampler, 4: Submission.View(VelB), 5: Submission.Sources,
                                                    7: OccupantBuffer}, Coarse, Scene)
    Submission.Encode(Encoder, 'AdvectThermo', {0: Uniform, 1: Submission.View(VelA), 2: Submission.View(HeatA),
                                                 3: Submission.Sampler, 4: Submission.View(HeatB), 5: Submission.Sources,
                                                 6: Coverage}, Fine, Scene)
    Submission.Device.queue.submit([Encoder.finish()])
    ActualVelocity = Submission.ReadTexture(VelB, Coarse).astype(np.float32)
    ActualThermo = Submission.ReadTexture(HeatB, Fine).astype(np.float32)
    Inside = ActualVelocity[2, 2, 2]
    Outside = ActualVelocity[2, 2, 0]
    if np.max(np.abs(Inside - [2, 0, 0, 1])) > .002 or np.any(Outside != 0):
        raise RuntimeError(f'Moving-solid point velocity mismatch: inside={Inside} outside={Outside}')
    if ActualThermo[6, 6, 6, 0] != 0 or ActualThermo[6, 6, 0, 0] != 1:
        raise RuntimeError('Distance-field solid did not exclude smoke while preserving the exterior')
    print(f'MOVING_DISTANCE_SOLID velocity_inside={Inside.tolist()} velocity_outside={Outside.tolist()} '
          'thermo_inside=0 thermo_outside=1 sampled_cascade=1', flush=True)


def ValidateSourcesAndCombustion(Submission):
    Velocity = np.zeros((*Coarse[::-1], 4), dtype=np.float16)
    Empty = np.zeros((*Fine[::-1], 4), dtype=np.float16)
    VelA, VelB = Submission.Texture(Velocity), Submission.Texture(Velocity)
    HeatA, HeatB = Submission.Texture(Empty), Submission.Texture(Empty)
    Source = np.array([3.25, 3.25, 2.975, .5, 4, 0, 0, 0, 2, 60, 3, 4], dtype=np.float32)
    Sources = Submission.SourceBuffer(Source)
    Packed = MakeUniform(Seconds=1/30)
    struct.pack_into('<i', Packed, 28, 1)
    struct.pack_into('<3f', Packed, 128, 0, 0, 0)
    struct.pack_into('<3f', Packed, 144, 8, 8, 8)
    Uniform = Submission.Uniform(Packed)
    Encoder = Submission.Device.create_command_encoder()
    Submission.Encode(Encoder, 'AdvectVelocity', {0: Uniform, 1: Submission.View(VelA), 2: Submission.View(HeatA),
                                                    3: Submission.Sampler, 4: Submission.View(VelB), 5: Sources,
                                                    7: Submission.Occupants}, Coarse)
    Submission.Encode(Encoder, 'AdvectThermo', {0: Uniform, 1: Submission.View(VelA), 2: Submission.View(HeatA),
                                                 3: Submission.Sampler, 4: Submission.View(HeatB), 5: Sources,
                                                 6: Submission.Coverage()}, Fine)
    Submission.Device.queue.submit([Encoder.finish()])
    ActualVelocity = Submission.ReadTexture(VelB, Coarse).astype(np.float32)
    ActualThermo = Submission.ReadTexture(HeatB, Fine).astype(np.float32)
    Expected = np.asarray(np.array([2, 60, 3, 4], dtype=np.float32) / 30, dtype=np.float16).astype(np.float32)
    CentreError = float(np.max(np.abs(ActualThermo[6, 6, 6] - Expected)))
    NeighbourExpected = float(np.float16((2 / 30) * np.exp(-1.7)))
    NeighbourError = abs(float(ActualThermo[6, 6, 7, 0]) - NeighbourExpected)
    Offset = np.array([3.5, 3.5, 3.5], dtype=np.float64) - Source[:3].astype(np.float64) - [0, 0, .55]
    Weight = np.exp(-1.7 * np.dot(Offset, Offset) / (.75 ** 2))
    Push = 1 - np.exp(-Weight / 30 * (4 + .15 * (2 + 4) + .05 * 3))
    ExpectedVelocity = float(np.float16(4 * Push))
    VelocityError = abs(float(ActualVelocity[3, 3, 3, 0]) - ExpectedVelocity)
    if CentreError > .002 or NeighbourError > .001 or VelocityError > .002:
        raise RuntimeError(f'Gaussian source mismatch {CentreError} {NeighbourError} {VelocityError}')

    Thermo = np.zeros_like(Empty)
    Thermo[...] = [1, 2, .5, .1]
    HeatA, HeatB = Submission.Texture(Thermo), Submission.Texture(Empty)
    Packed = MakeUniform(Seconds=1/30)
    struct.pack_into('<4f', Packed, 96, .7, .8, .6, 1)
    struct.pack_into('<4f', Packed, 192, 2, 30, .9, 0)
    Uniform = Submission.Uniform(Packed)
    Submission.Submit('AdvectThermo', {0: Uniform, 1: Submission.View(VelA), 2: Submission.View(HeatA),
                                       3: Submission.Sampler, 4: Submission.View(HeatB), 5: Submission.Sources,
                                       6: Submission.Coverage()}, Fine)
    Burned = Submission.ReadTexture(HeatB, Fine).astype(np.float32)[6, 6, 6]
    Initial = Thermo[6, 6, 6].astype(np.float32)
    Burning = Initial[2] / 30
    ExpectedBurned = np.asarray([Initial[0] * .7, (Initial[1] + Burning * 30) * .8,
                                 Initial[2] * .6, (Initial[3] + Burning * 2) * .9], dtype=np.float16).astype(np.float32)
    BurnError = float(np.max(np.abs(Burned - ExpectedBurned)))
    if BurnError > .002:
        raise RuntimeError(f'Combustion and retention mismatch: {BurnError}')
    print(f'PLUME_SOURCES centre_error={CentreError:.9g} neighbour_error={NeighbourError:.9g} '
          f'momentum_error={VelocityError:.9g} combustion_retention_error={BurnError:.9g}', flush=True)


def ValidateTransportChain(Submission):
    Velocity = np.zeros((*Coarse[::-1], 4), dtype=np.float16)
    Velocity[..., 0] = 15
    Thermo = np.zeros((*Fine[::-1], 4), dtype=np.float16)
    Thermo[7, 6, 5, 0] = 2
    Thermo[7, 6, 15, 0] = 3
    ZeroVelocity = np.zeros_like(Velocity)
    ZeroThermo = np.zeros_like(Thermo)
    VelA, VelB = Submission.Texture(Velocity), Submission.Texture(ZeroVelocity)
    VelC, VelD = Submission.Texture(ZeroVelocity), Submission.Texture(ZeroVelocity)
    HeatA, HeatB = Submission.Texture(Thermo), Submission.Texture(ZeroThermo)
    Light = Submission.Texture(ZeroVelocity)
    Curl = Submission.Buffer(np.zeros((*Coarse[::-1], 4), dtype=np.float32), wgpu.BufferUsage.STORAGE)
    ScalarZeros = np.zeros(Coarse[::-1], dtype=np.float32)
    Divergence = Submission.Buffer(ScalarZeros, wgpu.BufferUsage.STORAGE)
    PressureA, PressureB = Submission.Buffer(ScalarZeros, wgpu.BufferUsage.STORAGE), Submission.Buffer(ScalarZeros, wgpu.BufferUsage.STORAGE)
    Coverage = Submission.Coverage()
    Uniform = Submission.Uniform(MakeUniform(Seconds=1/30, Vorticity=3.0, Wind=(15, 0, 0)))
    Encoder = Submission.Device.create_command_encoder()
    Submission.Encode(Encoder, 'AdvectVelocity', {0: Uniform, 1: Submission.View(VelA), 2: Submission.View(HeatA),
                                                    3: Submission.Sampler, 4: Submission.View(VelB), 5: Submission.Sources,
                                                    7: Submission.Occupants}, Coarse)
    Submission.Encode(Encoder, 'ResolveCurl', {0: Uniform, 1: Submission.View(VelB), 2: Curl}, Coarse)
    Submission.Encode(Encoder, 'ConfineVorticity', {0: Uniform, 1: Submission.View(VelB), 2: Curl,
                                                    3: Submission.View(VelC)}, Coarse)
    Submission.Encode(Encoder, 'ResolveDivergence', {0: Uniform, 1: Submission.View(VelC), 2: Divergence,
                                                      3: PressureA, 4: PressureB}, Coarse)
    for Sweep in range(8):
        Current, Next = (PressureA, PressureB) if Sweep % 2 == 0 else (PressureB, PressureA)
        Submission.Encode(Encoder, 'RelaxPressure', {0: Uniform, 1: Submission.View(VelC), 2: Divergence,
                                                     3: Current, 4: Next}, Coarse)
    Submission.Encode(Encoder, 'ProjectVelocity', {0: Uniform, 1: Submission.View(VelC), 3: PressureA,
                                                    5: Submission.View(VelD)}, Coarse)
    Submission.Encode(Encoder, 'AdvectThermo', {0: Uniform, 1: Submission.View(VelD), 2: Submission.View(HeatA),
                                                 3: Submission.Sampler, 4: Submission.View(HeatB), 5: Submission.Sources,
                                                 6: Coverage}, Fine)
    Submission.Encode(Encoder, 'ResolveSunTransmittance', {0: Uniform, 2: Submission.View(HeatB),
                                                           3: Submission.Sampler, 4: Submission.View(Light)}, Coarse)
    Submission.Device.queue.submit([Encoder.finish()])
    ActualVelocity = Submission.ReadTexture(VelD, Coarse)
    ActualThermo = Submission.ReadTexture(HeatB, Fine)
    ActualLight = Submission.ReadTexture(Light, Coarse).astype(np.float32)
    ActualCoverage = np.frombuffer(Submission.Device.queue.read_buffer(Coverage), dtype=np.int32)
    ActualDivergence = np.frombuffer(Submission.Device.queue.read_buffer(Divergence), dtype=np.float32)
    ActualPressure = np.frombuffer(Submission.Device.queue.read_buffer(PressureA), dtype=np.float32)
    Submission.ReadbackDigest.update(ActualCoverage.tobytes())
    Submission.ReadbackDigest.update(ActualDivergence.tobytes())
    Submission.ReadbackDigest.update(ActualPressure.tobytes())
    ExpectedThermo = np.zeros_like(Thermo)
    ExpectedThermo[7, 6, 6, 0] = 2
    Error = float(np.max(np.abs(ActualThermo.astype(np.float32) - ExpectedThermo.astype(np.float32))))
    if Error > 0.001 or not np.array_equal(ActualVelocity, Velocity) or np.any(ActualDivergence != 0) or np.any(ActualPressure != 0):
        raise RuntimeError(f'Uniform advection/zero-pressure mismatch: {Error}')
    if not np.array_equal(ActualCoverage[[0, 1, 2, 4, 5, 6]], [6, 6, 7, 6, 6, 7]):
        raise RuntimeError(f'Unexpected coverage {ActualCoverage}')
    if not np.isfinite(ActualLight).all() or np.min(ActualLight[..., :2]) < 0 or np.max(ActualLight[..., :2]) > 1:
        raise RuntimeError('Plume light transmission outside [0, 1]')
    print(f'TRANSPORT_CHAIN cells={np.prod(Fine)} dispatches=15 pressure_sweeps=8 same_submission=1 impulse_error={Error:.9g} '
          f'coverage={ActualCoverage[[0, 1, 2, 4, 5, 6]].tolist()} boundary_exit_no_wrap=1 '
          f'light_range=[{np.min(ActualLight[..., :2]):.9g},{np.max(ActualLight[..., :2]):.9g}]', flush=True)


def ValidateLight(Submission):
    Velocity = np.zeros((*Coarse[::-1], 4), dtype=np.float16)
    Thermo = np.zeros((*Fine[::-1], 4), dtype=np.float16)
    Thermo[..., 0] = 2.0
    Heat, Light = Submission.Texture(Thermo), Submission.Texture(Velocity)
    Uniform = Submission.Uniform(MakeUniform(Extinction=0.5))
    Submission.Submit('ResolveSunTransmittance', {0: Uniform, 2: Submission.View(Heat), 3: Submission.Sampler,
                                                  4: Submission.View(Light)}, Coarse)
    Actual = Submission.ReadTexture(Light, Coarse).astype(np.float32)
    Expected = np.zeros((Coarse[2], 2), dtype=np.float64)
    for Z in range(Coarse[2]):
        Position, Travel, Depth = Z + 0.5, 0.25, 0.0
        for Step in range(8):
            Length = .5 * (1 + .6 * Step)
            Sample = Position + Travel + .5 * Length
            if Sample < .25 or Sample > 7.75:
                break
            Depth += Length  # smoke density 2 times extinction 0.5
            Travel += Length
        Expected[Z] = [np.exp(-.65 * Depth), np.exp(-.5 * Depth)]
    Error = float(np.max(np.abs(Actual[..., :2] - Expected[:, None, None, :])))
    if Error > .002:
        raise RuntimeError(f'Uniform smoke transmittance mismatch: {Error}')
    print(f'LIGHT_UNIFORM cells={np.prod(Coarse)} analytic_max_error={Error:.9g}', flush=True)


def ValidateVorticity(Submission):
    Velocity = np.zeros((*Coarse[::-1], 4), dtype=np.float16)
    X = np.arange(Coarse[0], dtype=np.float32) + .5
    Velocity[..., 1] = np.float16(.05 * X * X)[None, None, :]
    Velocity[3, 3, 4, 3] = 1
    Input, Output = Submission.Texture(Velocity), Submission.Texture(np.zeros_like(Velocity))
    Curl = Submission.Buffer(np.zeros((*Coarse[::-1], 4), dtype=np.float32), wgpu.BufferUsage.STORAGE)
    Uniform = Submission.Uniform(MakeUniform(Seconds=1/30, Vorticity=3.0))
    Encoder = Submission.Device.create_command_encoder()
    Submission.Encode(Encoder, 'ResolveCurl', {0: Uniform, 1: Submission.View(Input), 2: Curl}, Coarse)
    Submission.Encode(Encoder, 'ConfineVorticity', {0: Uniform, 1: Submission.View(Input), 2: Curl,
                                                     3: Submission.View(Output)}, Coarse)
    Submission.Device.queue.submit([Encoder.finish()])
    ActualCurl = np.frombuffer(Submission.Device.queue.read_buffer(Curl), dtype=np.float32).copy().reshape(*Coarse[::-1], 4)
    Submission.ReadbackDigest.update(ActualCurl.tobytes())
    Actual = Submission.ReadTexture(Output, Coarse).astype(np.float32)
    Values = Velocity[2, 2, :, 1].astype(np.float32)
    ExactCurl = .5 * (Values[2:] - Values[:-2])
    CurlError = float(np.max(np.abs(ActualCurl[2, 2, 1:-1, 2] - ExactCurl)))
    ExpectedY = float(np.float16(Values[4] - 3.0 / 30 * ExactCurl[3]))
    ConfinementError = abs(float(Actual[2, 2, 4, 1]) - ExpectedY)
    if CurlError > 1e-6 or ConfinementError > .002 or not np.array_equal(Actual[3, 3, 4], Velocity[3, 3, 4]):
        raise RuntimeError(f'Analytic curl/confinement mismatch {CurlError} {ConfinementError}')
    print(f'VORTICITY interior_curl_max_error={CurlError:.9g} confinement_expected_y={ExpectedY:.9g} '
          f'confinement_error={ConfinementError:.9g} solid_cell_unchanged=1', flush=True)


def Main():
    if len(sys.argv) != 2:
        raise RuntimeError('Expected Engine footprint validation executable')
    Submission = PlumeSubmission(sys.argv[1])
    ValidateJavaScriptFootprints(Submission)
    ValidateBuoyancy(Submission)
    ValidateMovingSolid(Submission)
    ValidateSourcesAndCombustion(Submission)
    ValidateTransportChain(Submission)
    ValidateLight(Submission)
    ValidateVorticity(Submission)
    print(f'PLUME_FOOTPRINT_GPU validated_lattices={Submission.ValidatedLattices} validated_sources={Submission.ValidatedSources}', flush=True)
    print(f'PLUME_READBACK_SHA256 {Submission.ReadbackDigest.hexdigest()}', flush=True)
    print('PLUME_TRANSPORT_SUMMARY PASS reference_wgsl=1 actual_distance_sampling=1 pressure_in_chain=1', flush=True)


if __name__ == '__main__':
    Main()
