import test from "node:test";
import assert from "node:assert/strict";

import { NearestDepthSpace } from "../Source/NearestDepthSpace.js";

// 📝 Node has no WebGPU namespace; the under-test code only reads these constants, so plain stand-ins do.
globalThis.GPUShaderStage ??= { COMPUTE: 1, VERTEX: 2, FRAGMENT: 4 };
globalThis.GPUTextureUsage ??= { STORAGE_BINDING: 8, TEXTURE_BINDING: 4 };

// 📝 The nearest-depth pyramid behind the screen-trace toggle, exercised without a GPU: a mock exchange records the
//    layouts, textures and groups the real code constructs, and a mock encoder counts the reduction dispatches. These
//    check the level chain, the parity convention the shaders depend on, and the dispatch count — not the walked result.

function ConstructMockExchange()
{
    let Next = 1;
    const Exchange = {
        Layouts: [], Textures: [], Groups: [], Programs: [],
        ConstructDescriptorLayout: (Kinds, Stage, Label) =>
        {
            const Layout = { Id: Next++, Kinds, Label };
            Exchange.Layouts.push(Layout);
            return Layout;
        },
        ConstructTexture: (Descriptor) =>
        {
            const Texture = { Id: Next++, Descriptor, destroy: () => { Texture.Destroyed = true; },
                              createView: (View = {}) => ({ Texture: Texture.Id, ...View }) };
            Exchange.Textures.push(Texture);
            return Texture;
        },
        ConstructDescriptorGroup: (Layout, Resources, Label) =>
        {
            const Group = { Id: Next++, Layout, Resources, Label };
            Exchange.Groups.push(Group);
            return Group;
        }
    };
    return Exchange;
}

function ConstructMockSurface()
{
    return { Views: { Depth: { Mock: "Depth" }, Attributes: { Mock: "Attributes" } } };
}

function ConstructMockShaders(Exchange)
{
    let Next = 1000;
    return {
        ConstructComputeProgram: async (File, Entry, Layouts) =>
        {
            const Program = { Id: Next++, File, Entry, Layouts };
            Exchange.Programs.push(Program);
            return Program;
        }
    };
}

function ConstructSpace(Exchange)
{
    return new NearestDepthSpace(Exchange, ConstructMockShaders(Exchange));
}

function ConstructMockEncoder()
{
    const Pass = { Pipelines: [], Groups: [], Dispatches: [],
                   setPipeline: (Program) => Pass.Pipelines.push(Program),
                   setBindGroup: (Slot, Group) => Pass.Groups.push([Slot, Group]),
                   dispatchWorkgroups: (X, Y) => Pass.Dispatches.push([X, Y]), end: () => { Pass.Ended = true; } };
    return { Pass, beginComputePass: () => Pass };
}

test("ConstructTargets chains halved levels down to one texel", async () =>
{
    const Exchange = ConstructMockExchange();
    const Space = ConstructSpace(Exchange);
    await Space.ConstructPrograms();
    Space.ConstructTargets([1920, 1080], ConstructMockSurface());
    assert.equal(Space.LevelCount, 11);
    assert.deepEqual(Space.LevelExtents[0], [1920, 1080]);
    assert.deepEqual(Space.LevelExtents[1], [960, 540]);
    assert.deepEqual(Space.LevelExtents[10], [1, 1]);
    for (let Level = 1; Level < Space.LevelCount; ++Level)
    {
        assert.deepEqual(Space.LevelExtents[Level],
                         [Math.max(1, 1920 >> Level), Math.max(1, 1080 >> Level)]);
    }
    assert.equal(Space.ReductionGroups[0].length, Space.LevelCount - 1);
    assert.equal(Space.ReductionGroups[1].length, Space.LevelCount - 1);
});

test("ConstructTargets floors odd extents without dropping a pixel", async () =>
{
    const Exchange = ConstructMockExchange();
    const Space = ConstructSpace(Exchange);
    await Space.ConstructPrograms();
    Space.ConstructTargets([100, 75], ConstructMockSurface());
    assert.deepEqual(Space.LevelExtents, [[100, 75], [50, 37], [25, 18], [12, 9], [6, 4], [3, 2], [1, 1]]);
});

test("ConstructTraversalGroups binds current, prior, attributes and exitance per parity", async () =>
{
    const Exchange = ConstructMockExchange();
    const Space = ConstructSpace(Exchange);
    await Space.ConstructPrograms();
    const Surface = ConstructMockSurface();
    const Display = { Views: { Exitance: { Mock: "Exitance" } } };
    Space.ConstructTargets([64, 64], Surface);
    Space.ConstructTraversalGroups(Surface, Display);
    assert.equal(Space.TraversalGroups.length, 2);
    // The traversal layout carries both pyramids before the attributes and the exitance.
    const TraversalLayout = Exchange.Layouts.find((Layout) => Layout.Label === "Screen traversal");
    assert.deepEqual(TraversalLayout.Kinds, ["Texture2DUnfiltered", "Texture2DUnfiltered", "Texture2D", "Texture2D"]);
    for (const Parity of [0, 1])
    {
        const Resources = Space.TraversalGroups[Parity].Resources;
        assert.equal(Resources[0], Space.Views[Parity]);
        assert.equal(Resources[1], Space.Views[1 - Parity]);
        assert.equal(Resources[2], Surface.Views.Attributes);
        assert.equal(Resources[3], Display.Views.Exitance);
    }
});

test("RecordReduction dispatches the transfer plus one reduction per level", async () =>
{
    const Exchange = ConstructMockExchange();
    const Space = ConstructSpace(Exchange);
    await Space.ConstructPrograms();
    Space.ConstructTargets([100, 75], ConstructMockSurface());
    const Encoder = ConstructMockEncoder();
    Space.RecordReduction(Encoder, 0, undefined);
    assert.equal(Encoder.Pass.Ended, true);
    assert.equal(Encoder.Pass.Dispatches.length, Space.LevelCount);
    assert.deepEqual(Encoder.Pass.Dispatches[0], [Math.ceil(100 / 8), Math.ceil(75 / 8)]);
    const Last = Space.LevelExtents[Space.LevelCount - 1];
    assert.deepEqual(Encoder.Pass.Dispatches[Space.LevelCount - 1], [Math.ceil(Last[0] / 8), Math.ceil(Last[1] / 8)]);
});
