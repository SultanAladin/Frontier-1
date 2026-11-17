/*====================================================================================================================================
                                                        STLEXCHANGE.H
====================================================================================================================================*/
// 🧩 STL exchange: bodies → watertight PolygonComplex → binary / ASCII STL in millimetres, and the reader that welds a file
//    back into shared vertices and measures its soundness (open / non-manifold / misoriented sides, Euler χ, volume)

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                    INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometrySpace/Parametric/Tessellation/BodyTessellation.h"

#include <array>
#include <string>
#include <vector>

namespace Frontier::GeometrySpace::Parametric
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 STL carries no units; millimetres are the convention every slicer and CAD reader assumes, so model coordinates
//    are multiplied by ModelUnitMillimetres. Sag 0 takes Tessellation.SagRatio × each body's own diagonal.
struct StlRequest
{
    bool                 BinaryEnabled        = true;                                        // [-]  - Binary (else ASCII)
    double               ModelUnitMillimetres = 1.0;                                         // [mm] - Length of one model unit
    TessellationSettings Tessellation         = {};                                          // [-]  - Chordal bound and limits
    std::string          Name                 = "Frontier";                                  // [-]  - Header / solid name (ASCII)
};

struct StlExport
{
    std::string Bytes        = {};                                                           // [-]   - File content
    int         Triangles    = 0;                                                            // [-]   - Facets written
    int         Bodies       = 0;                                                            // [-]   - Bodies tessellated
    double      Sag          = 0.0;                                                          // [len] - Largest chordal bound used (model units)
    double      Milliseconds = 0.0;                                                          // [ms]
    std::string Refusal      = {};                                                           // [-]   - Empty on success
};

// 💡 A file read back: corners welded where their 32-bit coordinates are bit-identical (the writer emits each shared
//    complex vertex as the same floats, so a watertight complex welds watertight again).
struct StlReading
{
    std::vector<Vector3>            Positions       = {};                                    // [mm] - Welded vertices
    std::vector<std::array<int, 3>> Triangles       = {};                                    // [-]  - Counter-clockwise from outside
    std::vector<Vector3>            Normals         = {};                                    // [-]  - Facet normals as stored
    std::string                     Name            = {};                                    // [-]  - Header / solid name
    bool                            BinaryCondition = false;                                 // [-]  - Binary layout detected
    std::string                     Refusal         = {};                                    // [-]  - Empty on success
};

// 📝 Soundness of a triangle set: every side shared by exactly two triangles running it in opposite directions makes
//    it closed and consistently oriented; then χ = V − E + F = 2 · (components − genus) and the divergence volume is
//    the enclosed volume.
struct StlSoundness
{
    int    OpenSides           = 0;                                                          // [-]   - Sides used once
    int    NonManifoldSides    = 0;                                                          // [-]   - Sides used three times or more
    int    MisorientedSides    = 0;                                                          // [-]   - Two uses in the same direction
    int    DegenerateTriangles = 0;                                                          // [-]   - Repeated corner or zero area
    int    Components          = 0;                                                          // [-]   - Connected through sides
    int    Sides               = 0;                                                          // [-]   - Distinct sides E
    int    EulerCharacteristic = 0;                                                          // [-]   - V − E + F
    double Volume              = 0.0;                                                        // [mm³] - Σ (a · (b × c)) / 6
    double Area                = 0.0;                                                        // [mm²] - Σ triangle areas
    bool   WatertightCondition = false;                                                      // [-]   - No open / non-manifold / misoriented side
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// 📝 Tessellates every body (one shared sag) and writes them as one STL. Binary: 80-byte header (Name), little-endian
//    uint32 count, 50 bytes per facet (normal, three corners, zero attribute). ASCII: "solid Name" … "endsolid Name"
//    with 9 significant digits, enough to reproduce every 32-bit float exactly, so both forms read back identically.
[[nodiscard]] StlExport WriteStl(const ModelPartition& Partition, const std::vector<BodyToken>& Bodies, const StlRequest& Request);

// Reads binary or ASCII STL (binary when 84 + 50·count equals the size, else ASCII starting with "solid").
[[nodiscard]] StlReading ReadStl(const std::string& Bytes);

[[nodiscard]] StlSoundness MeasureStlSoundness(const StlReading& Reading);

// Writes the export to Path; returns an empty string or the refusal.
[[nodiscard]] std::string ExportStlFile(const StlExport& Export, const std::string& Path);

} // namespace Frontier::GeometrySpace::Parametric
