/*====================================================================================================================================
                                                            ORBITCAMERA.H
====================================================================================================================================*/
// 🧩 Shared Z-up orbit camera: orbit / pan / dolly, view presets, fit-to-bounds, perspective or orthographic Vulkan clip
//    matrices, pixel rays. Applications add their own navigation on top (Sketcher: Plasticity mouse and keymap).

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                    INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include <array>
#include <cstdint>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    TYPES
//------------------------------------------------------------------------------------------------------------------------

using CameraMatrix = std::array<double, 16>;                                                 // Column-major 4 × 4

// 📝 View table: Top / Bottom look along ∓z (XY), Front / Back along ±y (XZ), Right / Left along ∓x (YZ); Isometric
//    looks from (+x, −y, +z).
enum class OrbitViewPreset : uint8_t
{
    Isometric = 0,
    Top       = 1,
    Bottom    = 2,
    Front     = 3,
    Back      = 4,
    Right     = 5,
    Left      = 6,
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 Azimuth / Elevation / Distance / Target follow BrowserPreviewCamera's names; this camera is Z-up (CAD convention).
struct OrbitCamera
{
    std::array<double, 3> Target              = { 0.0, 0.0, 0.0 };                           // [len] - Orbit centre
    double                Azimuth             = -0.7853981633974483;                         // [rad] - Eye azimuth from +x (CCW about +z)
    double                Elevation           = 0.6154797086703873;                          // [rad] - Eye elevation above the XY plane
    double                Distance            = 8.0;                                         // [len] - Eye → target
    double                FieldOfView         = 0.7;                                         // [rad] - Vertical, perspective
    bool                  OrthographicEnabled = false;                                       // [-]   - Parallel projection
    double                SceneRadius         = 2.0;                                         // [len] - Bounds radius (near / far planes)
};

// 📝 Everything a frame needs, derived once per frame from the camera and the viewport size.
struct OrbitCameraFrame
{
    CameraMatrix          ClipFromWorld       = {};                                          // [-]   - Projection × view (Vulkan clip: y down, z ∈ [0, 1])
    CameraMatrix          WorldFromClip       = {};                                          // [-]   - Inverse
    std::array<double, 3> Eye                 = {};                                          // [len] - Eye point
    std::array<double, 3> Right               = {};                                          // [-]   - Camera right
    std::array<double, 3> Up                  = {};                                          // [-]   - Camera up
    std::array<double, 3> Forward             = {};                                          // [-]   - Eye → target
    double                WorldPerPixel       = 0.01;                                        // [len] - At the target depth
    double                Width               = 1.0;                                         // [px]  - Viewport width
    double                Height              = 1.0;                                         // [px]  - Viewport height
    bool                  OrthographicEnabled = false;                                       // [-]   - Parallel projection
};

struct OrbitRay
{
    std::array<double, 3> Origin    = {};                                                    // [len] - Start point
    std::array<double, 3> Direction = { 0.0, 0.0, -1.0 };                                    // [-]   - Unit direction
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

[[nodiscard]] OrbitCameraFrame FrameOrbitCamera(const OrbitCamera& Camera, double Width, double Height);

// Pixel (x right, y down, from the top-left corner; centres at +0.5) → world ray through it.
[[nodiscard]] OrbitRay CastOrbitRay(const OrbitCameraFrame& Frame, double PixelX, double PixelY);

void RevolveOrbitCamera(OrbitCamera& Camera, double DeltaX, double DeltaY);                               // [px]
void PanOrbitCamera(OrbitCamera& Camera, const OrbitCameraFrame& Frame, double DeltaX, double DeltaY);    // [px]
void DollyOrbitCamera(OrbitCamera& Camera, double WheelSteps);                                            // [-] + = closer
void PresentOrbitView(OrbitCamera& Camera, OrbitViewPreset Preset);

// Centres the target on the box Low … High and backs off until its bounding sphere fills the vertical field of view.
void FitOrbitCamera(OrbitCamera& Camera, const std::array<double, 3>& Low, const std::array<double, 3>& High);

[[nodiscard]] const char*  OrbitViewLabel(OrbitViewPreset Preset) noexcept;
[[nodiscard]] CameraMatrix InvertCameraMatrix(const CameraMatrix& Matrix);

} // namespace Frontier
