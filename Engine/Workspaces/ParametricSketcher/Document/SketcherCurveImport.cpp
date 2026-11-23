/*====================================================================================================================================
                                                        VEHICLECURVEIMPORT.CPP
====================================================================================================================================*/
// 🧩 Vehicle plan V1: VehicleCurves.toml decode / encode with toml++ (all-or-nothing, units scaled to metres)

//------------------------------------------------------------------------------------------------------------------------
//                                                    INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Workspaces/ParametricSketcher/Document/SketcherCurveImport.h"
#include "Engine/GeometrySpace/Parametric/Exchange/ExchangePath.h"

#include "Engine/Workspaces/ParametricSketcher/Document/SketcherImagePlaneToml.h"

#include <toml++/toml.hpp>

#include <cmath>
#include <fstream>
#include <sstream>

namespace Frontier::Workspaces::Sketcher
{

using namespace Frontier::GeometrySpace::Parametric;

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

VehicleCurveFile DecodeVehicleCurves(const std::string& Text)
{
    VehicleCurveFile File;
    toml::table Root;
    try
    {
        Root = toml::parse(Text);
    }
    catch (const toml::parse_error& Failure)
    {
        File.Refusal = std::string("not TOML: ") + std::string(Failure.description());
        return File;
    }
    if (Root["Format"].value_or(std::string{}) != "VehicleCurves") { File.Refusal = "Format is not \"VehicleCurves\""; return File; }
    const std::string Units = Root["Units"].value_or(std::string{ "m" });
    double Scale = 1.0;                                                                      // [len/unit] - File unit → metres
    if (Units == "mm") Scale = 1.0e-3;
    else if (Units != "m") { File.Refusal = "unknown Units \"" + Units + "\" (m or mm)"; return File; }
    File.Source = Root["Source"].value_or(std::string{});
    const toml::array* Curves = Root["Curves"].as_array();
    if (!Curves || Curves->empty()) { File.Refusal = "no [[Curves]]"; return File; }
    for (size_t Index = 0; Index < Curves->size(); ++Index)
    {
        const toml::node_view<const toml::node> View{ (*Curves)[Index] };
        VehicleCurve Curve;
        Curve.Name            = View["Name"].value_or(std::string{});
        Curve.ClosedCondition = View["Closed"].value_or(false);
        Curve.PlanarCondition = View["Planar"].value_or(false);
        const std::string Where = "curve " + std::to_string(Index + 1) + (Curve.Name.empty() ? "" : " (" + Curve.Name + ")");
        if (Curve.Name.empty()) { File.Refusal = Where + " has no Name"; return File; }
        const toml::array* Points = View["Points"].as_array();
        if (!Points || Points->size() < 2) { File.Refusal = Where + " needs at least 2 points"; return File; }
        for (size_t Point = 0; Point < Points->size(); ++Point)
        {
            const toml::array* Triple = (*Points)[Point].as_array();
            std::optional<double> Coordinates[3];
            if (Triple && Triple->size() == 3) for (int Axis = 0; Axis < 3; ++Axis) Coordinates[Axis] = (*Triple)[Axis].value<double>();
            if (!Coordinates[0] || !Coordinates[1] || !Coordinates[2] || !std::isfinite(*Coordinates[0]) || !std::isfinite(*Coordinates[1]) || !std::isfinite(*Coordinates[2]))
            {
                File.Refusal = Where + ": point " + std::to_string(Point) + " is not [x, y, z]";
                return File;
            }
            Curve.Points.push_back(Vector3{ *Coordinates[0], *Coordinates[1], *Coordinates[2] } * Scale);
        }
        File.Curves.push_back(std::move(Curve));
    }
    if (std::string Refusal = DecodeSketcherImagePlanes(Root["ImagePlanes"].as_array(), Scale, File.ImagePlanes); !Refusal.empty())
    {
        File.Curves.clear();
        File.Refusal = Refusal;
    }
    return File;
}

VehicleCurveFile ReadVehicleCurves(const std::string& Path)
{
    std::ifstream Stream(GeometrySpace::Parametric::ResolveUtf8Path(Path), std::ios::binary);
    if (!Stream) { VehicleCurveFile File; File.Refusal = "cannot open " + Path; return File; }
    std::ostringstream Text;
    Text << Stream.rdbuf();
    VehicleCurveFile File = DecodeVehicleCurves(Text.str());
    for (SketcherImagePlane& Plane : File.ImagePlanes) Plane.Path = ResolveSketcherImagePath(Plane.Path, Path);
    return File;
}

std::string EncodeVehicleCurves(const VehicleCurveFile& File)
{
    toml::array Curves;
    for (const VehicleCurve& Curve : File.Curves)
    {
        toml::array Points;
        for (const Vector3& Point : Curve.Points) Points.push_back(toml::array{ Point.X, Point.Y, Point.Z });
        toml::table Entry{ { "Name", Curve.Name }, { "Closed", Curve.ClosedCondition }, { "Points", std::move(Points) } };
        if (Curve.PlanarCondition) Entry.insert("Planar", true);
        Curves.push_back(std::move(Entry));
    }
    toml::table Root{ { "Format", "VehicleCurves" }, { "Units", "m" }, { "Curves", std::move(Curves) } };
    if (!File.Source.empty()) Root.insert("Source", File.Source);
    if (!File.ImagePlanes.empty()) Root.insert("ImagePlanes", EncodeSketcherImagePlanes(File.ImagePlanes));
    std::ostringstream Stream;
    Stream << Root << "\n";
    return Stream.str();
}

}
