//============================================================================================================================================
//                                                           HEADLESSHARNESS.MJS
//============================================================================================================================================
// 🧩 Runs the real exhibit — bring-up and frame loop — under Node with Dawn, to validate it or to render it without a browser.
//
// 📝 Dawn's null backend validates every WGSL module (Tint), every layout, pipeline and command exactly as Chrome
//    would, without executing anything; a SwiftShader Vulkan ICD executes it on the CPU and the read-back canvas of
//    ?capture=1 is written out as a PNG. DOM, fetch and requestAnimationFrame are stubbed just enough for the host.
//
//    npm install                          (installs the `webgpu` package: Dawn for Node)
//    node HeadlessHarness.mjs .. 12 "?capture=1"                         validate bring-up and 12 frames (null backend)
//    BACKEND=vulkan SIZE=480x270 OUT=frame.png node HeadlessHarness.mjs .. 24 "?capture=1&quality=low"
//                                         render through Vulkan (point VK_ICD_FILENAMES at a SwiftShader ICD)
//    HOOKS=hooks.mjs ...                  a module whose default export has BeforeFrame(Host, Frame) / Report(Host)
//    HARNESS_TIMEOUT=600000               milliseconds before giving up (default 240000)
//
//    Exit status is 1 when any WebGPU or WGSL error was reported.

import { create, globals } from "webgpu";
import fs from "node:fs";
import path from "node:path";
import { pathToFileURL } from "node:url";
import zlib from "node:zlib";

const Root = path.resolve(process.argv[2] || ".");
const FrameLimit = Number(process.argv[3] || 12);
const Query = process.argv[4] || "?capture=1";
Object.assign(globalThis, globals);

const Backend = process.env.BACKEND || "null";
const Gpu = create([`backend=${Backend}`]);
const [ViewWidth, ViewHeight] = (process.env.SIZE || "1280x720").split("x").map(Number);
const Captures = [];
function EncodePng(Rgba, Width, Height)
{
    const Raw = Buffer.alloc((Width * 4 + 1) * Height);
    for (let Row = 0; Row < Height; ++Row)
    {
        Raw[Row * (Width * 4 + 1)] = 0;
        Buffer.from(Rgba.buffer, Rgba.byteOffset + Row * Width * 4, Width * 4).copy(Raw, Row * (Width * 4 + 1) + 1);
    }
    const Table = new Int32Array(256).map((_, N) => { let C = N; for (let K = 0; K < 8; ++K) C = C & 1 ? 0xEDB88320 ^ (C >>> 1) : C >>> 1; return C; });
    const Crc = (Bytes) => { let C = -1; for (const B of Bytes) C = Table[(C ^ B) & 255] ^ (C >>> 8); return (C ^ -1) >>> 0; };
    const Chunk = (Type, Data) => { const Length = Buffer.alloc(4); Length.writeUInt32BE(Data.length); const Body = Buffer.concat([Buffer.from(Type), Data]); const Sum = Buffer.alloc(4); Sum.writeUInt32BE(Crc(Body)); return Buffer.concat([Length, Body, Sum]); };
    const Header = Buffer.alloc(13); Header.writeUInt32BE(Width, 0); Header.writeUInt32BE(Height, 4); Header[8] = 8; Header[9] = 6; Header[10] = 0; Header[11] = 0; Header[12] = 0;
    return Buffer.concat([Buffer.from([137, 80, 78, 71, 13, 10, 26, 10]), Chunk("IHDR", Header), Chunk("IDAT", zlib.deflateSync(Raw)), Chunk("IEND", Buffer.alloc(0))]);
}
globalThis.SaveCapture = (Name) => { const Last = Captures[Captures.length - 1]; if (Last) { fs.writeFileSync(Name, EncodePng(Last.data, Last.width, Last.height)); console.log("saved", Name, Last.width + "x" + Last.height); } };
Object.defineProperty(globalThis, "navigator", { value: { gpu: Gpu, userAgent: "node" }, configurable: true, writable: true });

const Errors = [];
const Warnings = [];
const OriginalError = console.error.bind(console);
const OriginalWarn = console.warn.bind(console);
console.error = (...Args) => { Errors.push(Args.map(String).join(" ")); OriginalError(...Args); };
console.warn = (...Args) => { Warnings.push(Args.map(String).join(" ")); OriginalWarn(...Args); };
console.info = () => {};

class FakeClassList
{
    constructor() { this.Set = new Set(); }
    add(...Names) { Names.forEach((Name) => this.Set.add(Name)); }
    remove(...Names) { Names.forEach((Name) => this.Set.delete(Name)); }
    toggle(Name, Force) { const On = Force === undefined ? !this.Set.has(Name) : Force; if (On) this.Set.add(Name); else this.Set.delete(Name); return On; }
    contains(Name) { return this.Set.has(Name); }
}
class FakeElement
{
    constructor(Tag = "div", Id = "")
    {
        this.tagName = Tag.toUpperCase(); this.id = Id; this.children = []; this.style = {}; this.dataset = {};
        this.classList = new FakeClassList(); this.textContent = ""; this.innerHTML = ""; this.value = ""; this.checked = false;
        this.clientWidth = ViewWidth; this.clientHeight = ViewHeight; this.width = 0; this.height = 0; this.Listeners = {};
        this.parentNode = null; this.title = ""; this.type = ""; this.disabled = false;
    }
    appendChild(Child) { this.children.push(Child); Child.parentNode = this; return Child; }
    append(...Children) { Children.forEach((Child) => this.appendChild(typeof Child === "string" ? new FakeElement("span") : Child)); }
    prepend(Child) { this.children.unshift(Child); Child.parentNode = this; }
    insertBefore(Child) { return this.appendChild(Child); }
    removeChild(Child) { this.children = this.children.filter((Item) => Item !== Child); }
    remove() { if (this.parentNode) this.parentNode.removeChild(this); }
    querySelector() { return new FakeElement(); }
    querySelectorAll() { return []; }
    addEventListener(Name, Handler) { (this.Listeners[Name] = this.Listeners[Name] || []).push(Handler); }
    removeEventListener() {}
    dispatch(Name, Event = {}) { (this.Listeners[Name] || []).forEach((Handler) => Handler({ preventDefault() {}, stopPropagation() {}, target: this, ...Event })); }
    setAttribute(Name, Value) { this[Name] = Value; }
    getAttribute(Name) { return this[Name]; }
    hasAttribute(Name) { return Name in this; }
    setPointerCapture() {}
    releasePointerCapture() {}
    focus() {}
    blur() {}
    click() { this.dispatch("click"); }
    getBoundingClientRect() { return { left: 0, top: 0, width: this.clientWidth, height: this.clientHeight, right: this.clientWidth, bottom: this.clientHeight }; }
    getContext(Kind) { return Kind === "2d" ? { putImageData(Image) { Captures.push(Image); if (Captures.length > 4) Captures.shift(); }, drawImage() {} } : null; }
}
globalThis.HTMLElement = FakeElement;
globalThis.HTMLInputElement = class extends FakeElement {};
globalThis.HTMLSelectElement = class extends FakeElement {};
globalThis.HTMLButtonElement = class extends FakeElement {};
const Elements = new Map();
globalThis.document = {
    getElementById(Id) { if (!Elements.has(Id)) Elements.set(Id, new FakeElement("div", Id)); return Elements.get(Id); },
    createElement(Tag)
    {
        if (Tag === "input") return new globalThis.HTMLInputElement(Tag);
        if (Tag === "select") return new globalThis.HTMLSelectElement(Tag);
        if (Tag === "button") return new globalThis.HTMLButtonElement(Tag);
        return new FakeElement(Tag);
    },
    querySelector() { return new FakeElement(); },
    querySelectorAll() { return []; },
    addEventListener() {},
    body: new FakeElement("body"),
    documentElement: new FakeElement("html"),
    activeElement: null,
    visibilityState: "visible"
};
globalThis.window = globalThis;
globalThis.devicePixelRatio = 1;
const WindowListeners = {};
globalThis.addEventListener = (Name, Handler) => { (WindowListeners[Name] = WindowListeners[Name] || []).push(Handler); };
globalThis.removeEventListener = () => {};
globalThis.DispatchWindow = (Name, Event) => (WindowListeners[Name] || []).forEach((Handler) => Handler({ preventDefault() {}, target: document.body, repeat: false, ...Event }));
globalThis.location = { search: Query, href: "http://localhost/" + Query };
globalThis.localStorage = { Store: {}, getItem(Key) { return this.Store[Key] ?? null; }, setItem(Key, Value) { this.Store[Key] = String(Value); }, removeItem(Key) { delete this.Store[Key]; } };
globalThis.ResizeObserver = class { observe() {} disconnect() {} };
globalThis.ImageData = class { constructor(Data, Width, Height) { this.data = Data; this.width = Width; this.height = Height; } };
let FramesRun = 0;
let FrameCallbacks = [];
globalThis.requestAnimationFrame = (Callback) => { FrameCallbacks.push(Callback); return FrameCallbacks.length; };
globalThis.cancelAnimationFrame = () => {};
globalThis.fetch = async (Location) =>
{
    const File = path.join(Root, String(Location).split("?")[0]);
    if (!fs.existsSync(File))
        return { ok: false, status: 404, text: async () => "", arrayBuffer: async () => new ArrayBuffer(0) };
    const Bytes = fs.readFileSync(File);
    return { ok: true, status: 200, text: async () => Bytes.toString("utf8"),
             arrayBuffer: async () => Bytes.buffer.slice(Bytes.byteOffset, Bytes.byteOffset + Bytes.byteLength), json: async () => JSON.parse(Bytes.toString("utf8")) };
};

const Started = performance.now();
if (process.env.HOOKS)
    globalThis.HarnessHooks = (await import(pathToFileURL(path.resolve(process.env.HOOKS)).href)).default;
await import(pathToFileURL(path.join(Root, "Source/IlluminationHost.js")).href);

// Drive the frame loop until FrameLimit frames have run, then report.
const Hooks = globalThis.HarnessHooks || {};
let Simulated = 0;
while (FramesRun < FrameLimit)
{
    await new Promise((Resolve) => setTimeout(Resolve, 5));
    if ((performance.now() - Started) > Number(process.env.HARNESS_TIMEOUT || 240000))
    {
        Errors.push("harness timeout");
        break;
    }
    const Pending = FrameCallbacks;
    FrameCallbacks = [];
    for (const Callback of Pending)
    {
        Simulated += 16.6667;
        const Host = globalThis.IlluminationExhibit;
        if (Host && Host.Ready && Hooks.BeforeFrame)
            Hooks.BeforeFrame(Host, FramesRun);
        Callback(Started + Simulated);
        if (globalThis.IlluminationExhibit && globalThis.IlluminationExhibit.Ready)
            FramesRun = globalThis.IlluminationExhibit.FrameIndex;
    }
    const Host = globalThis.IlluminationExhibit;
    if ((Host && Host.Failed) || document.getElementById("failure").children.length > 0)
        break;
}
const Host = globalThis.IlluminationExhibit;
if (Host && Host.Exchange && Host.Exchange.Device)
    await Host.Exchange.Device.queue.onSubmittedWorkDone();
await new Promise((Resolve) => setTimeout(Resolve, 50));
const Failure = document.getElementById("failure");
console.log(`\n=== harness: ${FramesRun} frames, ${Errors.length} errors, ${Warnings.length} warnings, ${((performance.now() - Started) / 1000).toFixed(1)} s`);
if (Hooks.Report && Host)
    await Hooks.Report(Host);
if (Failure.children.length)
    console.log("failure panel:", Failure.children.map((Child) => Child.textContent).join(" | "));
if (process.env.OUT)
{
    await new Promise((Resolve) => setTimeout(Resolve, 500));
    globalThis.SaveCapture(process.env.OUT);
}
process.exit(Errors.length ? 1 : 0);
