/*====================================================================================================================================
                                                        STEPEXCHANGE.H
====================================================================================================================================*/
// 🧩 STEP exchange (ISO 10303-21 file, AP214 automotive_design schema): solid bodies → MANIFOLD_SOLID_BREP / BREP_WITH_VOIDS
//    with exact analytic carriers (PLANE, CYLINDRICAL / CONICAL / SPHERICAL / TOROIDAL_SURFACE, LINE, CIRCLE, ELLIPSE), exact
//    B-splines for NURBS carriers, and certified cubic B-spline stand-ins for procedural carriers (rolling-ball and parallel
//    surfaces, traced intersection and contact curves), in millimetres; and the reverse — any AP203 / AP214 / AP242 boundary
//    representation (MANIFOLD_SOLID_BREP, BREP_WITH_VOIDS, SHELL_BASED_SURFACE_MODEL) read into blueprints and assembled

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                    INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometrySpace/Parametric/Topology/ModelPartition.h"

#include <string>
#include <vector>

namespace Frontier::GeometrySpace::Parametric
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 STEP carries its unit: the file declares millimetres and model coordinates are multiplied by ModelUnitMillimetres.
//    Timestamp goes into FILE_NAME verbatim (empty: a fixed epoch, so the same model always writes the same bytes).
struct StepRequest
{
    double      ModelUnitMillimetres   = 1.0;                                                // [mm]  - Length of one model unit
    double      ApproximationTolerance = 1.0e-6;                                             // [len] - Stand-in deviation bound (model units)
    std::string Name                   = "Frontier";                                         // [-]   - PRODUCT name and file name
    std::string Timestamp              = {};                                                 // [-]   - ISO 8601, FILE_NAME time_stamp
};

struct StepExport
{
    std::string Bytes                = {};                                                   // [-]   - File content
    int         Instances            = 0;                                                    // [-]   - #n written
    int         Solids               = 0;                                                    // [-]   - MANIFOLD_SOLID_BREP / BREP_WITH_VOIDS
    int         Voids                = 0;                                                    // [-]   - Cavity shells
    int         Faces                = 0;                                                    // [-]   - ADVANCED_FACE
    int         Edges                = 0;                                                    // [-]   - EDGE_CURVE
    int         Vertices             = 0;                                                    // [-]   - VERTEX_POINT
    int         ExactSurfaces        = 0;                                                    // [-]   - Analytic or NURBS, written exactly
    int         ApproximatedSurfaces = 0;                                                    // [-]   - Cubic stand-ins
    int         ExactCurves          = 0;                                                    // [-]   - Line / circle / ellipse / NURBS
    int         ApproximatedCurves   = 0;                                                    // [-]   - Cubic stand-ins
    double      Deviation            = 0.0;                                                  // [len] - Largest certified stand-in deviation (model units)
    double      Milliseconds         = 0.0;                                                  // [ms]
    std::string Refusal              = {};                                                   // [-]   - Empty on success
};

// 📝 Import: the file's own length unit (SI prefixes, CONVERSION_BASED_UNIT such as INCH) is converted into model units.
//    The whole file is refused when any part of it cannot be read or a body fails assembly or audit: nothing is added to
//    the partition then (every body is assembled and audited in a scratch partition first).
struct StepImportRequest
{
    double      ModelUnitMillimetres = 1.0;                                                  // [mm] - Length of one model unit
    std::string Label                = "STEP";                                               // [-]  - Refusal prefix
};

struct StepImport
{
    std::vector<BodyToken>   Bodies              = {};                                       // [-]   - One per solid (or surface model)
    int                      Instances           = 0;                                        // [-]   - #n read
    int                      Solids              = 0;                                        // [-]   - MANIFOLD_SOLID_BREP / BREP_WITH_VOIDS
    int                      Sheets              = 0;                                        // [-]   - SHELL_BASED_SURFACE_MODEL
    int                      Voids               = 0;                                        // [-]   - Cavity shells
    int                      Faces               = 0;                                        // [-]
    int                      Edges               = 0;                                        // [-]   - Edges assembled
    int                      Vertices            = 0;                                        // [-]   - Vertices assembled
    int                      SynthesizedSeams    = 0;                                        // [-]   - Seams added to edge-less closed faces
    int                      DegenerateEdges     = 0;                                        // [-]   - Zero-length pole / apex edges skipped
    int                      TolerantEdges       = 0;                                        // [-]   - Edges given a local precision
    int                      TolerantVertices    = 0;                                        // [-]   - Vertices given a local precision
    int                      ConvertedSurfaces   = 0;                                        // [-]   - Revolutions / extrusions → NURBS
    int                      ReorientedFaces     = 0;                                        // [-]   - Faces whose loops ran against their mates
    double                   FileUnitMillimetres = 1.0;                                      // [mm]  - Length unit declared by the file
    double                   LargestGap          = 0.0;                                      // [len] - Worst vertex / edge mismatch found (model units)
    std::vector<std::string> Notes               = {};                                       // [-]   - Non-fatal observations
    double                   Milliseconds        = 0.0;                                      // [ms]
    std::string              Refusal             = {};                                       // [-]   - Empty on success
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// Every body must be a solid; each solid region becomes one MANIFOLD_SOLID_BREP (BREP_WITH_VOIDS when it has cavities).
[[nodiscard]] StepExport WriteStep(const ModelPartition& Partition, const std::vector<BodyToken>& Bodies, const StepRequest& Request);

// Writes Export.Bytes to Path; returns a refusal (empty on success).
[[nodiscard]] std::string ExportStepFile(const StepExport& Export, const std::string& Path);

// 💡 Part 21 parse → entities → blueprint per solid: vertices and edges shared through their instance numbers, carriers
//    converted exactly (analytic, B-spline; surfaces of revolution and linear extrusion become exact NURBS), SP-curves
//    fitted per face (FitFaceSurfaceCurves), local precisions where the file's geometry misses its own vertices, then
//    AssembleBody and AuditBody. Loops whose direction contradicts every neighbour (a writer's slip that OpenCascade's
//    own reader repairs with ShapeFix) are reversed: each shared edge demands its two faces agree or differ, the faces
//    are 2-coloured per shell and the smaller colour class is turned. Unsupported carriers are named in the refusal.
[[nodiscard]] StepImport ReadStep(ModelPartition& Partition, const std::string& Bytes, const StepImportRequest& Request);

[[nodiscard]] StepImport ImportStepFile(ModelPartition& Partition, const std::string& Path, const StepImportRequest& Request);

} // namespace Frontier::GeometrySpace::Parametric
