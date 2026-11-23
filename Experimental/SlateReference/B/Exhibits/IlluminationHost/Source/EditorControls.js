//============================================================================================================================================
//                                                             EDITORCONTROLS.JS
//============================================================================================================================================
// 🧩 The Frontier Editor's inspector vocabulary as DOM builders: cards, metrics, sliders with fields, switches, segments, stats.

// 📝 Every control binds one key of one target object (the exhibit settings, the gas parameters, a fracture recipe…).
//    Edits write the target and call OnChange(Key, Target); Synchronise() pulls every control back from its target
//    after something else changed it (a key, a preset). Live callbacks refresh readouts a few times a second.
//    Builders only use DOM calls the headless harness stubs, so the inspector also runs under Dawn's null backend.

import { ConstructGlyph } from "./EditorIcons.js";

export function ClearElement(Element)
{
    if (Element.replaceChildren)
        Element.replaceChildren();
    else
        Element.children = [];
}

export function AssignStyle(Element, Name, Value)
{
    if (Element.style.setProperty)
        Element.style.setProperty(Name, Value);
    else
        Element.style[Name] = Value;
}

function Construct(Tag, ClassName = "", Text = "")
{
    const Element = document.createElement(Tag);
    if (ClassName)
        Element.className = ClassName;
    if (Text)
        Element.textContent = Text;
    return Element;
}

function FormatScalar(Scalar, Digits)
{
    if (!Number.isFinite(Scalar))
        return "–";
    return Number(Scalar).toLocaleString("en-US", { minimumFractionDigits: Digits, maximumFractionDigits: Digits });
}

export class EditorControls
{
    constructor(OnChange)
    {
        this.OnChange = OnChange;
        this.Bindings = [];
        this.Live = [];
    }

    Reset()
    {
        this.Bindings = [];
        this.Live = [];
    }

    Synchronise()
    {
        for (const Binding of this.Bindings)
            Binding();
    }

    RefreshLive()
    {
        for (const Callback of this.Live)
            Callback();
    }

    Notify(Key, Target)
    {
        this.OnChange(Key, Target);
    }

    //--------------------------------------------------------------------------------------------------------------------
    //                                                 STRUCTURE
    //--------------------------------------------------------------------------------------------------------------------

    // 📝 A card: heading with its glyph and an index, an optional uppercase subtitle, and a body.
    Card(Parent, Title, Glyph, Options = {})
    {
        const Card = Construct("section", "card");
        if (Options.Accent)
            AssignStyle(Card, "--accent", Options.Accent);
        const Heading = Construct("div", "card-heading");
        const Label = Construct("span");
        if (Glyph)
            Label.appendChild(ConstructGlyph(Glyph, 16, 1.5));
        Label.appendChild(Construct("span", "", Title));
        Heading.appendChild(Label);
        if (Options.Index !== undefined)
            Heading.appendChild(Construct("span", "card-index", String(Options.Index).padStart(2, "0")));
        Card.appendChild(Heading);
        if (Options.Subtitle)
            Card.appendChild(Construct("div", "card-subtitle", Options.Subtitle));
        Parent.appendChild(Card);
        return Card;
    }

    Muted(Parent, Text)
    {
        const Element = Construct("p", "muted", Text);
        Parent.appendChild(Element);
        return Element;
    }

    // 📝 A large figure with its unit; Read() supplies the live value (refreshed with the readouts).
    Metric(Parent, Read, Unit = "", Digits = 0, Pill = null)
    {
        const Row = Construct("div", "metric-row");
        const Metric = Construct("div", "metric");
        const Value = Construct("span");
        const Small = Construct("small", "", Unit);
        Metric.append(Value, Small);
        Row.appendChild(Metric);
        let PillElement = null;
        if (Pill)
        {
            PillElement = Construct("span", "small-pill");
            Row.appendChild(PillElement);
        }
        Parent.appendChild(Row);
        const Refresh = () =>
        {
            const Scalar = Read();
            Value.textContent = typeof Scalar === "string" ? Scalar : FormatScalar(Scalar, Digits);
            if (PillElement)
            {
                const Text = Pill();
                const State = Array.isArray(Text) ? Text[1] : "";
                PillElement.textContent = Array.isArray(Text) ? Text[0] : Text;
                PillElement.className = `small-pill ${State}`;
            }
        };
        Refresh();
        this.Live.push(Refresh);
        return Refresh;
    }

    Pills(Parent, Texts)
    {
        const Row = Construct("div", "pill-row");
        for (const Text of Texts)
            Row.appendChild(Construct("span", "small-pill", Text));
        Parent.appendChild(Row);
        return Row;
    }

    // 📝 A grid of small figures: Items [[Label, Read() → value or string, Unit, Digits]].
    Stats(Parent, Items, Columns = 3)
    {
        const Grid = Construct("div", `stats${Columns === 2 ? " two" : Columns === 4 ? " four" : ""}`);
        const Refreshers = Items.map(([Label, Read, Unit = "", Digits = 0]) =>
        {
            const Cell = Construct("div");
            const Strong = Construct("strong");
            Cell.append(Construct("span", "", Label), Strong);
            Grid.appendChild(Cell);
            return () =>
            {
                const Scalar = Read();
                Strong.textContent = typeof Scalar === "string" ? Scalar : FormatScalar(Scalar, Digits);
                if (Unit)
                {
                    const Small = Construct("small", "", ` ${Unit}`);
                    Strong.appendChild(Small);
                }
            };
        });
        Parent.appendChild(Grid);
        const Refresh = () => Refreshers.forEach((Refresher) => Refresher());
        Refresh();
        this.Live.push(Refresh);
        return Refresh;
    }

    Receipt(Parent, Items)
    {
        const List = Construct("div", "receipt-list");
        const Refreshers = Items.map(([Label, Read]) =>
        {
            const Row = Construct("div");
            const Strong = Construct("strong");
            Row.append(Construct("span", "", Label), Strong);
            List.appendChild(Row);
            return () => { Strong.textContent = String(Read()); };
        });
        Parent.appendChild(List);
        const Refresh = () => Refreshers.forEach((Refresher) => Refresher());
        Refresh();
        this.Live.push(Refresh);
        return Refresh;
    }

    // 📝 A monospaced block refreshed from Read() → lines.
    Readout(Parent, Read)
    {
        const Element = Construct("pre", "readout");
        Parent.appendChild(Element);
        const Refresh = () => { Element.textContent = Read().join("\n"); };
        Refresh();
        this.Live.push(Refresh);
        return Refresh;
    }

    //--------------------------------------------------------------------------------------------------------------------
    //                                                  CONTROLS
    //--------------------------------------------------------------------------------------------------------------------

    // 📝 Label and numeric field on one line, the slider beneath, optional range captions. Options: Label, Minimum,
    //    Maximum, Step, Unit, Digits, Labels [left, right], Scale (display multiplier), OnInput(Value).
    Slider(Parent, Target, Key, Options)
    {
        const { Minimum, Maximum, Step = 0.01, Unit = "", Digits = 2, Scale = 1.0 } = Options;
        const Block = Construct("div", "control");
        const Line = Construct("div", "control-line");
        Line.appendChild(Construct("span", "", Options.Label));
        const Field = Construct("label", "number-field");
        const Number_ = document.createElement("input");
        Number_.type = "text";
        Number_.inputMode = "decimal";
        Field.appendChild(Number_);
        if (Unit)
            Field.appendChild(Construct("small", "", Unit));
        Line.appendChild(Field);
        Block.appendChild(Line);
        const Range = document.createElement("input");
        Range.type = "range";
        Range.min = String(Minimum);
        Range.max = String(Maximum);
        Range.step = String(Step);
        Block.appendChild(Range);
        if (Options.Labels)
        {
            const Labels = Construct("div", "range-labels");
            Labels.append(Construct("span", "", Options.Labels[0]), Construct("span", "", Options.Labels[1]));
            Block.appendChild(Labels);
        }
        Parent.appendChild(Block);
        const Show = () =>
        {
            const Scalar = Number(Target[Key]);
            Range.value = String(Scalar);
            Number_.value = FormatScalar(Scalar * Scale, Digits).replace(/,/g, "");
            AssignStyle(Range, "--progress", `${Math.max(0, Math.min(100, (Scalar - Minimum) / (Maximum - Minimum) * 100))}%`);
        };
        const Commit = (Scalar) =>
        {
            const Clamped = Math.max(Minimum, Math.min(Maximum, Scalar));
            Target[Key] = Clamped;
            Show();
            if (Options.OnInput)
                Options.OnInput(Clamped);
            this.Notify(Key, Target);
        };
        Range.addEventListener("input", () => Commit(Number(Range.value)));
        Number_.addEventListener("change", () =>
        {
            const Scalar = Number(String(Number_.value).replace(/[^0-9eE+\-.]/g, "")) / Scale;
            if (Number.isFinite(Scalar))
                Commit(Step >= 1 ? Math.round(Scalar / Step) * Step : Scalar);
            else
                Show();
        });
        Show();
        this.Bindings.push(Show);
        return Show;
    }

    // 📝 A labelled switch row (the native green toggle). Options: Label, Detail, Invert, OnChange(Value).
    Switch(Parent, Target, Key, Options)
    {
        const Row = Construct("div", "switch-row");
        const Text = Construct("div");
        Text.appendChild(Construct("span", "", Options.Label));
        if (Options.Detail)
            Text.appendChild(Construct("small", "", Options.Detail));
        const Toggle = Construct("button", "toggle");
        Toggle.setAttribute("role", "switch");
        Toggle.setAttribute("aria-label", Options.Label);
        Row.append(Text, Toggle);
        Parent.appendChild(Row);
        const Read = () => (Options.Invert ? !Target[Key] : !!Target[Key]);
        const Show = () =>
        {
            Toggle.classList.toggle("on", Read());
            Toggle.setAttribute("aria-checked", Read() ? "true" : "false");
        };
        Toggle.addEventListener("click", () =>
        {
            const Next = !Read();
            Target[Key] = Options.Invert ? !Next : Next;
            Show();
            if (Options.OnChange)
                Options.OnChange(Target[Key]);
            this.Notify(Key, Target);
        });
        Show();
        this.Bindings.push(Show);
        return Show;
    }

    // 📝 A segmented choice: Choices [[Caption, Value]]. Options: Label, OnChange(Value).
    Segmented(Parent, Target, Key, Choices, Options = {})
    {
        const Block = Construct("div", "control");
        if (Options.Label)
        {
            const Line = Construct("div", "control-line");
            Line.appendChild(Construct("span", "", Options.Label));
            Block.appendChild(Line);
        }
        const Group = Construct("div", "segmented");
        const Buttons = Choices.map(([Caption, Value]) =>
        {
            const Button = Construct("button", "", Caption);
            Button.addEventListener("click", () =>
            {
                Target[Key] = Value;
                Show();
                if (Options.OnChange)
                    Options.OnChange(Value);
                this.Notify(Key, Target);
            });
            Group.appendChild(Button);
            return [Button, Value];
        });
        Block.appendChild(Group);
        Parent.appendChild(Block);
        const Show = () => Buttons.forEach(([Button, Value]) => Button.classList.toggle("active", Target[Key] === Value));
        Show();
        this.Bindings.push(Show);
        return Show;
    }

    // 📝 A colour swatch: Target[Key] is "#rrggbb" (sRGB). Options: Label, OnChange(Value).
    Colour(Parent, Target, Key, Options = {})
    {
        const Block = Construct("div", "control control-colour");
        const Line = Construct("div", "control-line");
        Line.appendChild(Construct("span", "", Options.Label || Key));
        const Pair = Construct("span", "colour-pair");
        const Code = Construct("span", "colour-code", Target[Key]);
        const Input = document.createElement("input");
        Input.type = "color";
        Input.className = "colour-input";
        Input.value = Target[Key];
        Input.addEventListener("input", () =>
        {
            Target[Key] = Input.value;
            Code.textContent = Input.value;
            if (Options.OnChange)
                Options.OnChange(Input.value);
            this.Notify(Key, Target);
        });
        Pair.append(Code, Input);
        Line.appendChild(Pair);
        Block.appendChild(Line);
        Parent.appendChild(Block);
        const Show = () =>
        {
            Input.value = Target[Key];
            Code.textContent = Target[Key];
        };
        this.Bindings.push(Show);
        return Show;
    }

    // 📝 A dropdown: Choices [[Caption, Value]] with numeric or string values. Options: Label, OnChange(Value).
    Select(Parent, Target, Key, Choices, Options = {})
    {
        const Block = Construct("div", "control");
        if (Options.Label)
        {
            const Line = Construct("div", "control-line");
            Line.appendChild(Construct("span", "", Options.Label));
            Block.appendChild(Line);
        }
        const Field = document.createElement("select");
        Field.className = "select-field";
        for (const [Caption, Value] of Choices)
        {
            const Choice = document.createElement("option");
            Choice.value = String(Value);
            Choice.textContent = Caption;
            Field.appendChild(Choice);
        }
        Block.appendChild(Field);
        Parent.appendChild(Block);
        const Numeric = Choices.every(([, Value]) => typeof Value === "number");
        const Show = () => { Field.value = String(Target[Key]); };
        Field.addEventListener("change", () =>
        {
            Target[Key] = Numeric ? Number(Field.value) : Field.value;
            if (Options.OnChange)
                Options.OnChange(Target[Key]);
            this.Notify(Key, Target);
        });
        Show();
        this.Bindings.push(Show);
        return Show;
    }

    ActionRow(Parent)
    {
        const Row = Construct("div", "action-row");
        Parent.appendChild(Row);
        return Row;
    }

    Button(Parent, Caption, Glyph, Action, Variant = "")
    {
        const Button = Construct("button", `action-button ${Variant}`.trim());
        if (Glyph)
            Button.appendChild(ConstructGlyph(Glyph, 13, 1.7));
        Button.appendChild(Construct("span", "", Caption));
        Button.addEventListener("click", Action);
        Parent.appendChild(Button);
        return Button;
    }

    LinkButton(Parent, Caption, Action)
    {
        const Button = Construct("button", "link-button");
        Button.append(Construct("span", "", Caption), ConstructGlyph("ChevronRight", 14, 1.6));
        Button.addEventListener("click", Action);
        Parent.appendChild(Button);
        return Button;
    }

    // 📝 The inspector's ON/OFF tiles: Items [{ Name, Glyph, Read(), Write(Value) }].
    PropertySwitches(Parent, Items)
    {
        const Row = Construct("div", "property-switches");
        const Shows = Items.map((Item) =>
        {
            const Tile = Construct("button", "property-switch");
            const Icon = Construct("span", "switch-icon");
            Icon.appendChild(ConstructGlyph(Item.Glyph, 17, 1.7));
            const State = Construct("span", "switch-state");
            Tile.append(Icon, Construct("span", "switch-name", Item.Name), State);
            const Show = () =>
            {
                const On = !!Item.Read();
                Tile.classList.toggle("is-on", On);
                Tile.classList.toggle("is-off", !On);
                State.textContent = On ? "ON" : "OFF";
            };
            Tile.addEventListener("click", () =>
            {
                Item.Write(!Item.Read());
                Show();
                this.Notify(Item.Name, null);
            });
            Row.appendChild(Tile);
            Show();
            return Show;
        });
        Parent.appendChild(Row);
        const Refresh = () => Shows.forEach((Show) => Show());
        this.Bindings.push(Refresh);
        this.Live.push(Refresh);
        return Refresh;
    }

    // 📝 A canvas instrument drawn by Draw(Context, Width, Height); redrawn with the readouts when Live is set.
    Instrument(Parent, Height, Draw, Live = false)
    {
        const Canvas = document.createElement("canvas");
        Canvas.className = "instrument";
        const Width = 640;
        Canvas.width = Width;
        Canvas.height = Height * 2;
        AssignStyle(Canvas, "height", `${Height}px`);
        Parent.appendChild(Canvas);
        const Context = Canvas.getContext("2d");
        const Redraw = () =>
        {
            // 📝 The headless harness's canvas has no path API; there is nothing to draw on it.
            if (!Context || !Context.beginPath)
                return;
            Context.clearRect(0, 0, Width, Height * 2);
            Draw(Context, Width, Height * 2);
        };
        Redraw();
        this.Bindings.push(Redraw);
        if (Live)
            this.Live.push(Redraw);
        return Redraw;
    }
}
