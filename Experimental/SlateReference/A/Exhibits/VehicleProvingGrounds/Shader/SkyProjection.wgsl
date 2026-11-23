//============================================================================================================================================
//                                                            SKYPROJECTION.WGSL
//============================================================================================================================================
// 🧩 The sky-view parametrisation: absolute azimuth on U, square-root elevation on V so the horizon receives the texels.

// 📐 u = φ/2π + ½,  v = ½ − ½·sign(e)·√(|e| / (π/2)); v = 0 is the zenith row, ½ the horizon, 1 the nadir.
fn ProjectSkyCoordinates(Direction: vec3f) -> vec2f
{
    let Azimuth   = atan2(Direction.y, Direction.x);
    let Elevation = asin(clamp(Direction.z, -1.0, 1.0));
    let U = Azimuth * (0.5 / 3.14159265358979) + 0.5;
    let V = 0.5 - 0.5 * sign(Elevation) * sqrt(abs(Elevation) / 1.5707963267949);
    return vec2f(U, V);
}

fn ReconstructSkyDirection(Coordinates: vec2f) -> vec3f
{
    let Azimuth = (Coordinates.x - 0.5) * 2.0 * 3.14159265358979;
    let Signed  = (0.5 - Coordinates.y) * 2.0;
    let Elevation = sign(Signed) * Signed * Signed * 1.5707963267949;
    return vec3f(cos(Elevation) * cos(Azimuth), cos(Elevation) * sin(Azimuth), sin(Elevation));
}
