#=============================================================================================================================================
#                                                            FACETARCHIVECODEC.PY
#=============================================================================================================================================
# 🧩 Translates the authored ControlVehicle geometry archives (Frontier .inl) into the binary facet streams the exhibit loads.
#
# 📝 Sources, read verbatim from a Frontier checkout (branch arena/4f07c82a-frontier):
#    ① EngineContent/GeometryArchives/ControlVehicleStructure.inl         body, 2924 facets, per-facet CourseMaterial family
#    ② Projects/Project-Drive/Source/ControlVehicleTyreMesh.inl          authored RubberFL, hub-local, corner normals
#    ③ EngineContent/GeometryArchives/ControlWheelStructure.inl          authored RimFL, centred on its own axle
#    ④ Projects/Project-Drive/Source/ControlVehicleRimMesh.inl           the same RimFL topology with corner normals,
#                                                                         but offset +0.04543 m in Z about the axle socket
#
# 📝 The rim stream takes its positions from ③ (centred, the archive DriveSceneAuthor emits) and its corner normals from
#    ④. The two share topology index for index, which this codec verifies before it writes anything.
#
# 📝 Stream layout, little-endian, every field 4-byte aligned:
#    offset  0  char[4]   'FCT1'
#    offset  4  u32       VertexCount
#    offset  8  u32       FacetCount
#    offset 12  u32       Flags                      bit 0 = corner normals present
#    offset 16  f32[3]    BoundsMinimum              [m]
#    offset 28  f32[3]    BoundsMaximum              [m]
#    offset 40  u32[2]    Reserved
#    offset 48  f32       Positions[VertexCount * 3] [m]   authored frame: +X forward, +Y left, +Z up
#               u32       Facets[FacetCount * 3]     [-]   vertex indices, counter-clockwise seen from outside
#               u32       Families[FacetCount]       [-]   CourseMaterial identity (DriveCourse.h)
#               f32       CornerNormals[FacetCount * 9]    present only when Flags bit 0 is set
#
# Usage:  python3 FacetArchiveCodec.py <FrontierCheckout>

import os
import re
import struct
import sys

ArchiveFolder = os.path.dirname(os.path.abspath(__file__))
StreamMagic   = b"FCT1"
CornerNormalsPresent = 1


#-------------------------------------------------------------------------------------------------------------------------
#                                                    INL ARRAY DECODE
#-------------------------------------------------------------------------------------------------------------------------

def DecodeArchiveSpans(ArchivePath):
    with open(ArchivePath, "r", encoding="utf-8") as Reader:
        Text = Reader.read()
    Spans = {}
    for Match in re.finditer(r"inline constexpr (float|uint32_t) (\w+)\[\]\s*=\s*\{(.*?)\};", Text, re.S):
        Scalar, Name, Body = Match.groups()
        Tokens = [Token.strip() for Token in Body.replace("\n", " ").split(",") if Token.strip()]
        if Scalar == "float":
            Spans[Name] = [float(Token.rstrip("f")) for Token in Tokens]
        else:
            Spans[Name] = [int(Token.rstrip("u")) for Token in Tokens]
    return Spans


#-------------------------------------------------------------------------------------------------------------------------
#                                                     STREAM ENCODE
#-------------------------------------------------------------------------------------------------------------------------

def EncodeFacetStream(TargetPath, Positions, Facets, Families, CornerNormals):
    VertexCount = len(Positions) // 3
    FacetCount  = len(Facets) // 3
    if len(Families) != FacetCount:
        raise ValueError(f"{TargetPath}: {len(Families)} families for {FacetCount} facets")
    if CornerNormals is not None and len(CornerNormals) != FacetCount * 9:
        raise ValueError(f"{TargetPath}: {len(CornerNormals)} corner normal scalars for {FacetCount} facets")
    if max(Facets) >= VertexCount:
        raise ValueError(f"{TargetPath}: facet references vertex {max(Facets)} of {VertexCount}")

    Minimum = [min(Positions[Axis::3]) for Axis in range(3)]
    Maximum = [max(Positions[Axis::3]) for Axis in range(3)]
    Flags   = CornerNormalsPresent if CornerNormals is not None else 0

    with open(TargetPath, "wb") as Writer:
        Writer.write(StreamMagic)
        Writer.write(struct.pack("<3I", VertexCount, FacetCount, Flags))
        Writer.write(struct.pack("<3f", *Minimum))
        Writer.write(struct.pack("<3f", *Maximum))
        Writer.write(struct.pack("<2I", 0, 0))
        Writer.write(struct.pack(f"<{len(Positions)}f", *Positions))
        Writer.write(struct.pack(f"<{len(Facets)}I", *Facets))
        Writer.write(struct.pack(f"<{len(Families)}I", *Families))
        if CornerNormals is not None:
            Writer.write(struct.pack(f"<{len(CornerNormals)}f", *CornerNormals))

    Extent = os.path.getsize(TargetPath)
    print(f"[FacetArchive] {os.path.basename(TargetPath)}: {VertexCount} vertices, {FacetCount} facets, "
          f"{Extent} bytes, bounds {['%.4f' % Scalar for Scalar in Minimum]} .. {['%.4f' % Scalar for Scalar in Maximum]}")


#-------------------------------------------------------------------------------------------------------------------------
#                                                      TRANSLATION
#-------------------------------------------------------------------------------------------------------------------------

def TranslateFrontierArchives(FrontierRoot):
    Body  = DecodeArchiveSpans(os.path.join(FrontierRoot, "EngineContent/GeometryArchives/ControlVehicleStructure.inl"))
    Tyre  = DecodeArchiveSpans(os.path.join(FrontierRoot, "Projects/Project-Drive/Source/ControlVehicleTyreMesh.inl"))
    Wheel = DecodeArchiveSpans(os.path.join(FrontierRoot, "EngineContent/GeometryArchives/ControlWheelStructure.inl"))
    Rim   = DecodeArchiveSpans(os.path.join(FrontierRoot, "Projects/Project-Drive/Source/ControlVehicleRimMesh.inl"))

    # 📝 ① body — positions, facets and the authored family stream; flat facet normals are derived by the loader,
    #    exactly as DriveSceneAuthor::AppendFace derives them.
    EncodeFacetStream(os.path.join(ArchiveFolder, "ControlVehicleBody.facet"),
                      Body["kPositions"], Body["kTriangles"], Body["kTriangleFamily"], None)

    # 📝 ② tyre — authored RubberFL with its corner normals, already centred on the axle.
    EncodeFacetStream(os.path.join(ArchiveFolder, "ControlVehicleTyre.facet"),
                      Tyre["kPositions"], Tyre["kTriangles"], Tyre["kTriangleMaterial"], Tyre["kCornerNormals"])

    # 🔴 ③ + ④ rim — the topology must agree index for index, otherwise the normals would land on the wrong corners.
    if Wheel["kTriangles"] != Rim["kTriangles"] or len(Wheel["kPositions"]) != len(Rim["kPositions"]):
        raise ValueError("ControlWheelStructure and ControlVehicleRimMesh no longer share topology")
    if Wheel["kTriangleFamily"] != Rim["kTriangleMaterial"]:
        raise ValueError("ControlWheelStructure and ControlVehicleRimMesh disagree on families")
    EncodeFacetStream(os.path.join(ArchiveFolder, "ControlVehicleRim.facet"),
                      Wheel["kPositions"], Wheel["kTriangles"], Wheel["kTriangleFamily"], Rim["kCornerNormals"])


if __name__ == "__main__":
    if len(sys.argv) != 2:
        print("usage: python3 FacetArchiveCodec.py <FrontierCheckout>")
        sys.exit(2)
    TranslateFrontierArchives(sys.argv[1])
