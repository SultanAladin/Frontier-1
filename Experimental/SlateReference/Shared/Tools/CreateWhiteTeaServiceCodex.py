#!/usr/bin/env python3
"""Create the versioned White Tea Service workspace and shared pigment Codex documents."""
from pathlib import Path
import struct

ROOT = Path(__file__).resolve().parents[1]
CONTENT = ROOT / "EngineContent"
REVISION = 2

def u16(v): return struct.pack("<H", v)
def u32(v): return struct.pack("<I", v)
def u64(v): return struct.pack("<Q", v)
def f64(v): return struct.pack("<d", v)
def run(v):
    b = v.encode("utf-8")
    return u32(len(b)) + b

def digest(data):
    value = 14695981039346656037
    for byte in data:
        value = ((value ^ byte) * 1099511628211) & 0xffffffffffffffff
    return value

def codex(profile, identity, sections):
    stream = bytearray(64)
    indexed = []
    for code, content in sections:
        while len(stream) % 8:
            stream.append(0)
        position = len(stream)
        stream += content
        indexed.append((code, position, len(content), digest(content)))
    while len(stream) % 8:
        stream.append(0)
    index_at = len(stream)
    index_data = bytearray(u32(0x58444953) + u32(len(indexed)))
    for code, position, size, checksum in indexed:
        index_data += u32(code) + u16(1) + u16(0) + u64(REVISION) + u64(position) + u64(size) + u64(checksum)
    stream += index_data
    index_digest = digest(index_data)
    stream += u32(0x54464353) + u32(0) + u64(index_at) + u64(len(index_data)) + u64(index_digest)
    preamble = (u32(0x44434C53) + u16(1) + u16(0) + u32(profile) + u32(64) +
                u64(index_at) + u64(len(index_data)) + u64(identity) + u64(REVISION) + u64(index_digest) + u64(0))
    stream[:64] = preamble
    return bytes(stream)

def byte(v): return bytes([1 if v else 0])

def colour(red, green, blue, space=1):
    return f64(red) + f64(green) + f64(blue) + u32(space)

def textured():
    return u32(0) + u32(1) + u32(0)

def scalar_channel(value, default):
    return (u32(0) + u32(2) + u32(0) + f64(value) + colour(0., 0., 0., 0) +
            f64(default) + colour(0., 0., 0., 0) + f64(0.) + f64(1.) + byte(True))

def colour_channel(red, green, blue):
    c = colour(red, green, blue)
    return u32(0) + u32(0) + u32(0) + f64(0.) + c + f64(0.) + c + f64(0.) + f64(1.) + byte(True)

def absent_channel():
    return (u32(4) + u32(2) + u32(0) + f64(0.) + colour(0., 0., 0., 0) +
            f64(0.) + colour(0., 0., 0., 0) + f64(0.) + f64(1.) + byte(False))

def coverage():
    return u32(3) + u32(0) + textured() + f64(1.) + u32(0) + byte(False) + byte(False)

def material_layer():
    channel_mask = (1 << 0) | (1 << 1) | (1 << 2) | (1 << 3) | (1 << 5) | (1 << 6) | (1 << 7)
    return (u32(0) + u32(1) + u32(5) + u32(0) + u32(0) + u32(channel_mask) + u32(0) +
            coverage() + textured() + run("Base Material") + byte(True) + byte(True) + byte(False))

def material_record(reference):
    channels = []
    for index in range(20):
        if index == 0:
            channels.append(colour_channel(1., 1., 1.))
        elif index == 1:
            channels.append(scalar_channel(0., 0.))
        elif index == 2:
            channels.append(scalar_channel(0.5, 0.5))
        elif index == 3:
            channels.append(scalar_channel(0.04, 0.04))
        elif index == 5:
            channels.append(scalar_channel(1., 1.))
        elif index == 6:
            channels.append(colour_channel(0., 0., 0.))
        elif index == 7:
            channels.append(scalar_channel(1., 1.))
        else:
            channels.append(absent_channel())
    return (run(reference) + u32(0) + f64(0.5) + byte(False) +
            b"".join(channels) + u32(1) + material_layer())

def scene_entry(subject, name, geometry, material, position, rotation=(0.,0.,0.), scale=(1.,1.,1.)):
    return u32(subject) + run(name) + run(geometry) + run(material) + b"".join(f64(x) for x in position + rotation + scale)

def box_mesh(name, halfx, halfy, halfz):
    verts = [
        (-halfx,-halfy,-halfz),( halfx,-halfy,-halfz),( halfx,-halfy, halfz),(-halfx,-halfy, halfz),
        (-halfx, halfy,-halfz),( halfx, halfy,-halfz),( halfx, halfy, halfz),(-halfx, halfy, halfz),
    ]
    faces = [0,1,2, 0,2,3, 4,6,5, 4,7,6, 0,4,5, 0,5,1, 1,5,6, 1,6,2, 2,6,7, 2,7,3, 3,7,4, 3,4,0]
    out = bytearray(run(name) + u32(len(verts)))
    for v in verts:
        out += b"".join(f64(x) for x in v)
    out += u32(len(faces))
    for i in faces:
        out += u32(i)
    return bytes(out)

def mesh_record(name, verts, faces):
    out = bytearray(run(name) + u32(len(verts)))
    for v in verts:
        out += b"".join(f64(x) for x in v)
    flat = [i for tri in faces for i in tri]
    out += u32(len(flat))
    for i in flat:
        out += u32(i)
    return bytes(out)

def lathe_mesh(name, profile, segments=32):
    verts = []
    for i in range(segments):
        a = 2.0 * 3.141592653589793 * i / segments
        ca, sa = __import__('math').cos(a), __import__('math').sin(a)
        for r, y in profile:
            verts.append((r * ca, y, r * sa))
    faces = []
    rows = len(profile)
    for i in range(segments):
        ni = (i + 1) % segments
        for j in range(rows - 1):
            a = i * rows + j
            b = ni * rows + j
            c = ni * rows + j + 1
            d = i * rows + j + 1
            faces.append((a, b, c)); faces.append((a, c, d))
    return verts, faces

def translated_mesh(verts, faces, offset):
    base = 0
    return [(x + offset[0], y + offset[1], z + offset[2]) for x,y,z in verts], faces

def merge_mesh(name, parts):
    verts=[]; faces=[]
    for pv,pf in parts:
        base=len(verts); verts.extend(pv); faces.extend([(a+base,b+base,c+base) for a,b,c in pf])
    return mesh_record(name, verts, faces)

def box_part(halfx, halfy, halfz, offset=(0.,0.,0.)):
    ox,oy,oz=offset
    verts=[(ox+x,oy+y,oz+z) for x,y,z in [
        (-halfx,-halfy,-halfz),(halfx,-halfy,-halfz),(halfx,-halfy,halfz),(-halfx,-halfy,halfz),
        (-halfx,halfy,-halfz),(halfx,halfy,-halfz),(halfx,halfy,halfz),(-halfx,halfy,halfz)]]
    faces=[(0,1,2),(0,2,3),(4,6,5),(4,7,6),(0,4,5),(0,5,1),(1,5,6),(1,6,2),(2,6,7),(2,7,3),(3,7,4),(3,4,0)]
    return verts,faces

def tea_service_meshes():
    teapot_body = lathe_mesh("", [(0.00,-0.070),(0.105,-0.060),(0.150,-0.015),(0.132,0.045),(0.085,0.075),(0.035,0.088),(0.000,0.088)], 40)
    lid = lathe_mesh("", [(0.00,0.080),(0.070,0.080),(0.080,0.092),(0.035,0.108),(0.000,0.108)], 40)
    knob = lathe_mesh("", [(0.00,0.108),(0.025,0.112),(0.025,0.132),(0.000,0.136)], 24)
    spout = box_part(0.070,0.018,0.022,(0.190,0.022,0.000))
    handle = box_part(0.018,0.070,0.030,(-0.175,0.012,0.000))
    teapot = merge_mesh("Mesh/ServiceTeapot", [teapot_body,lid,knob,spout,handle])

    cup_outer = lathe_mesh("", [(0.038,-0.048),(0.058,-0.030),(0.064,0.040),(0.070,0.058)], 36)
    cup_inner = lathe_mesh("", [(0.048,-0.030),(0.055,0.035),(0.060,0.053)], 36)
    cup_handle = box_part(0.012,0.034,0.018,(-0.078,0.004,0.000))
    cup = merge_mesh("Mesh/Teacup", [cup_outer,cup_inner,cup_handle])

    saucer = mesh_record("Mesh/Saucer", *lathe_mesh("", [(0.020,-0.008),(0.090,-0.010),(0.105,0.000),(0.080,0.012),(0.030,0.014),(0.000,0.012)], 40))
    sugar = merge_mesh("Mesh/SugarBowl", [
        lathe_mesh("", [(0.00,-0.050),(0.070,-0.044),(0.082,0.010),(0.066,0.052),(0.030,0.064),(0.000,0.064)], 36),
        lathe_mesh("", [(0.00,0.058),(0.060,0.058),(0.068,0.070),(0.026,0.090),(0.000,0.094)], 36),
        box_part(0.026,0.010,0.010,(0.095,0.020,0.000)), box_part(0.026,0.010,0.010,(-0.095,0.020,0.000))])
    milk = merge_mesh("Mesh/MilkJug", [
        lathe_mesh("", [(0.00,-0.060),(0.055,-0.052),(0.070,0.020),(0.052,0.078),(0.030,0.090),(0.000,0.090)], 36),
        box_part(0.020,0.032,0.018,(-0.082,0.020,0.000)), box_part(0.050,0.015,0.018,(0.088,0.060,0.000))])
    floor = mesh_record("Mesh/Floor", *box_part(1.000,.001,1.000))
    return [teapot, cup, saucer, sugar, milk, floor]

def main():
    (CONTENT / "MaterialArchives").mkdir(parents=True, exist_ok=True)
    pigment = run("White Dielectric") + b"".join(f64(x) for x in (1.,1.,1.,0.32,1.5)) + b"\x01"
    pigment_path = CONTENT / "MaterialArchives" / "WhiteDielectric.pigment"
    pigment_path.write_bytes(codex(1, 0x574449454C454354, [(0x464E4950, pigment)]))

    naming = run("White Tea Service")
    environment = b"".join(f64(x) for x in (35., 120., 4.8, 5500., 1., 1., 1.))
    material = "EngineContent/MaterialArchives/WhiteDielectric.pigment"
    entries = [
        scene_entry(0, "Sun", "", "", (0.,0.,0.)),
        scene_entry(1, "Sky", "", "", (0.,0.,0.)),
        scene_entry(2, "Atmosphere", "", "", (0.,0.,0.)),
        scene_entry(3, "Service Teapot", "Mesh/ServiceTeapot", material, (0., 0.035, 0.)),
        scene_entry(3, "Teacup", "Mesh/Teacup", material, (0.31, 0.035, 0.05)),
        scene_entry(3, "Saucer", "Mesh/Saucer", material, (0.31, 0.0, 0.05)),
        scene_entry(3, "Sugar Bowl", "Mesh/SugarBowl", material, (-0.30, 0.0, 0.08)),
        scene_entry(3, "Milk Jug", "Mesh/MilkJug", material, (-0.16, 0.0, -0.28)),
        scene_entry(3, "Floor", "Mesh/Floor", material, (0., -0.002, 0.), scale=(2.,1.,2.)),
    ]
    scene = u32(len(entries)) + b"".join(entries)
    meshes = tea_service_meshes()
    mesh_section = u32(len(meshes)) + b"".join(meshes)
    materials = u32(1) + material_record(material)
    embedded = u32(0)
    (CONTENT / "WhiteTeaService.codex").write_bytes(codex(0, 0x5748544541534552,
        [(0x4D414E57, naming), (0x564E4557, environment), (0x454E4353, scene),
         (0x4853454D, mesh_section), (0x5354414D, materials), (0x44424D45, embedded)]))

if __name__ == "__main__":
    main()
