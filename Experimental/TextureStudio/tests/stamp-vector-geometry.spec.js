import { test, expect } from "@playwright/test";
import Paper from "paper";
import { patternLayer } from "../src/patternDocument.js";
import { booleanPatternLayers, layerWorldContours } from "../src/vectorEditorGeometry.js";

function Shape(Category, Translation = {})
{
    return patternLayer(Category, { x: 256, y: 256, width: 300, height: 300, paint: true, fillEnabled: true, visible: true, ...Translation });
}
function Inspect(Geometry, Predicate)
{
    const Scope = new Paper.PaperScope();
    Scope.setup(new Scope.Size(512, 512));
    try
    {
        const Paths = layerWorldContours(Geometry).map(Contour => new Scope.Path({
            segments: Contour.nodes.map(Point => new Scope.Segment([Point.x, Point.y], [(Point.in?.x ?? Point.x) - Point.x, (Point.in?.y ?? Point.y) - Point.y], [(Point.out?.x ?? Point.x) - Point.x, (Point.out?.y ?? Point.y) - Point.y])),
            closed: Contour.closed,
            insert: false,
        }));
        const Contours = new Scope.CompoundPath({ children: Paths, insert: false, fillRule: Geometry.fillRule || "nonzero" });
        Predicate(Contours);
    }
    finally
    {
        Scope.project.remove();
    }
}
for (const Operation of ["difference", "exclude"])
{
    test(`Tangent rectangle/ellipse ${Operation} retains corners and removes the centre`, () =>
    {
        const Geometry = booleanPatternLayers([Shape("rect"), Shape("ellipse")], Operation);
        Inspect(Geometry, Contours =>
        {
            expect(Math.abs(Contours.area)).toBeCloseTo(19294.37, 0);
            expect(Contours.contains([256, 256])).toBe(false);
            expect(Contours.contains([115, 115])).toBe(true);
            expect(Contours.contains([90, 90])).toBe(false);
        });
    });
}
test("Boolean holes survive a second operation, transforms and serialization", () =>
{
    const Ring = booleanPatternLayers([Shape("rect"), Shape("ellipse", { width: 200, height: 200 })], "difference");
    const Combined = booleanPatternLayers([Ring, Shape("rect", { x: 450, width: 40, height: 40, rotation: 30 })], "union");
    Inspect(JSON.parse(JSON.stringify(Combined)), Contours =>
    {
        expect(Contours.contains([256, 256])).toBe(false);
        expect(Contours.contains([115, 115])).toBe(true);
        expect(Contours.contains([450, 256])).toBe(true);
    });
});
