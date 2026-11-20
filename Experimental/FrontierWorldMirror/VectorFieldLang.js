/*============================================================================================================================================
                                                        VECTORFIELDLANG.JS
============================================================================================================================================*/
// 🧩 Symbolic Vector Field Language, Lexer, Recursive-Descent Parser, Type Checker, Syntax Highlighter & Dual Compiler (JS + GLSL ES 3.00)
//    Supports physics notation natively:
//      a = -S · A(r) · r̂ + ω_swirl · (ŷ × r̂)
//      let radial = -S * A(r) * r_hat
//      v = 0.8 * S * A(r) * curlNoise(p, 48.0)

const HAT_ALIASES = {
    'r\u0302': 'r_hat',
    'r_hat': 'r_hat',
    'd\u0302': 'r_hat',
    'd_hat': 'r_hat',
    'x\u0302': 'x_hat',
    'x_hat': 'x_hat',
    '\u0177': 'y_hat',
    'y\u0302': 'y_hat',
    'y_hat': 'y_hat',
    '\u1e91': 'z_hat',
    'z\u0302': 'z_hat',
    'z_hat': 'z_hat',
    '\u03b8\u0302': 'theta_hat',
    'theta\u0302': 'theta_hat',
    'theta_hat': 'theta_hat'
};

const BUILTIN_SYMS = {
    p: 'vec3',
    c: 'vec3',
    d: 'vec3',
    r: 'scalar',
    rho: 'scalar',
    t: 'scalar',
    x: 'scalar',
    y: 'scalar',
    z: 'scalar',
    S: 'scalar',
    R: 'scalar',
    '\u03c9': 'scalar',
    '\u03c9_swirl': 'scalar',
    omega: 'scalar',
    omega_swirl: 'scalar',
    '\u03c0': 'scalar',
    pi: 'scalar',
    '\u03c4': 'scalar',
    tau: 'scalar',
    e: 'scalar'
};

const BUILTIN_FNS = {
    A: { MinArgs: 1, MaxArgs: 2, Args: ['scalar', 'scalar'], Ret: 'scalar' },
    attenuate: { MinArgs: 1, MaxArgs: 2, Args: ['scalar', 'scalar'], Ret: 'scalar' },
    G: { MinArgs: 0, MaxArgs: 0, Args: [], Ret: 'scalar' },
    gate: { MinArgs: 0, MaxArgs: 0, Args: [], Ret: 'scalar' },
    sin: { MinArgs: 1, MaxArgs: 1, Args: ['scalar'], Ret: 'scalar' },
    cos: { MinArgs: 1, MaxArgs: 1, Args: ['scalar'], Ret: 'scalar' },
    tan: { MinArgs: 1, MaxArgs: 1, Args: ['scalar'], Ret: 'scalar' },
    tanh: { MinArgs: 1, MaxArgs: 1, Args: ['scalar'], Ret: 'scalar' },
    exp: { MinArgs: 1, MaxArgs: 1, Args: ['scalar'], Ret: 'scalar' },
    sqrt: { MinArgs: 1, MaxArgs: 1, Args: ['scalar'], Ret: 'scalar' },
    abs: { MinArgs: 1, MaxArgs: 1, Args: ['scalar'], Ret: 'scalar' },
    pow: { MinArgs: 2, MaxArgs: 2, Args: ['scalar', 'scalar'], Ret: 'scalar' },
    min: { MinArgs: 2, MaxArgs: 2, Args: ['scalar', 'scalar'], Ret: 'scalar' },
    max: { MinArgs: 2, MaxArgs: 2, Args: ['scalar', 'scalar'], Ret: 'scalar' },
    clamp: { MinArgs: 3, MaxArgs: 3, Args: ['scalar', 'scalar', 'scalar'], Ret: 'scalar' },
    smoothstep: { MinArgs: 3, MaxArgs: 3, Args: ['scalar', 'scalar', 'scalar'], Ret: 'scalar' },
    mix: { MinArgs: 3, MaxArgs: 3, Args: ['any', 'any', 'scalar'], Ret: 'same0' },
    dot: { MinArgs: 2, MaxArgs: 2, Args: ['vec3', 'vec3'], Ret: 'scalar' },
    cross: { MinArgs: 2, MaxArgs: 2, Args: ['vec3', 'vec3'], Ret: 'vec3' },
    length: { MinArgs: 1, MaxArgs: 1, Args: ['vec3'], Ret: 'scalar' },
    norm: { MinArgs: 1, MaxArgs: 1, Args: ['vec3'], Ret: 'vec3' },
    normalize: { MinArgs: 1, MaxArgs: 1, Args: ['vec3'], Ret: 'vec3' },
    curlNoise: { MinArgs: 1, MaxArgs: 2, Args: ['vec3', 'scalar'], Ret: 'vec3' },
    noise3: { MinArgs: 1, MaxArgs: 2, Args: ['vec3', 'scalar'], Ret: 'vec3' },
    vec3: { MinArgs: 1, MaxArgs: 3, Args: ['scalar', 'scalar', 'scalar'], Ret: 'vec3' },
    vec2: { MinArgs: 2, MaxArgs: 2, Args: ['scalar', 'scalar'], Ret: 'vec2' }
};

export const FORMULA_PRESETS = [
    {
        Id: 'SwirlAttractor',
        Name: 'Swirl Vortex + Radial Pull',
        Code: 'a = -S · A(r) · r̂ + ω_swirl · (ŷ × r̂)'
    },
    {
        Id: 'HelmholtzRing',
        Name: 'Helmholtz Vortex + Updraft',
        Code: 'let vortex = ω_swirl · A(r) · (ŷ × r̂)\nlet updraft = 0.45 · S · exp(-rho / 48.0) · ŷ\nv = vortex + updraft - 0.25 · S · A(r) · r̂'
    },
    {
        Id: 'StandingWave',
        Name: 'Pulsed Standing Wave Shell',
        Code: 'let wave = cos(r · 0.075 - t · 3.4)\nlet swirl = 0.65 · ω_swirl · A(r) · (ŷ × r̂)\na = S · A(r) · wave · r̂ + swirl'
    },
    {
        Id: 'CurlSpiral',
        Name: 'Curl Turbulence + Cyclone',
        Code: 'let turb = 0.85 · curlNoise(p, 46.0)\nlet spin = ω_swirl · (ŷ × r̂)\nv = A(r) · (S · turb + spin)'
    },
    {
        Id: 'DipoleLobes',
        Name: 'Magnetic Dipole Flux',
        Code: 'let lobe = 3.0 · dot(ŷ, r̂) · r̂ - ŷ\na = S · A(r) · norm(lobe) + 0.3 · ω_swirl · (ŷ × r̂)'
    }
];

export const DEFAULT_FORMULA_CODE = FORMULA_PRESETS[0].Code;

//------------------------------------------------------------------------------------------------------------------------
//                                             1. UNICODE-AWARE LEXER
//------------------------------------------------------------------------------------------------------------------------

function IsIdentStart(Ch)
{
    if (!Ch) return false;
    const Cp = Ch.codePointAt(0);
    return (Cp >= 65 && Cp <= 90) || (Cp >= 97 && Cp <= 122) || Cp === 95 || Cp === 0x03b8 || Cp === 0x03c9 || Cp === 0x03c0 || Cp === 0x03c4 || Cp === 0x0177 || Cp === 0x1e91;
}

function IsIdentCont(Ch)
{
    if (!Ch) return false;
    const Cp = Ch.codePointAt(0);
    return IsIdentStart(Ch) || (Cp >= 48 && Cp <= 57);
}

export function TokenizeFieldLang(Source)
{
    const Tokens = [];
    let I = 0;
    let Line = 1;
    let Col = 1;
    const N = Source.length;

    while (I < N)
    {
        const Ch = Source[I];
        const Start = I;
        const StartLine = Line;
        const StartCol = Col;

        if (Ch === '\r') { I++; continue; }
        if (Ch === '\n')
        {
            Tokens.push({ Kind: 'newline', Text: '\n', Start, End: I + 1, Line: StartLine, Col: StartCol });
            I++; Line++; Col = 1;
            continue;
        }
        if (Ch === ' ' || Ch === '\t')
        {
            while (I < N && (Source[I] === ' ' || Source[I] === '\t')) I++;
            Tokens.push({ Kind: 'ws', Text: Source.slice(Start, I), Start, End: I, Line: StartLine, Col: StartCol });
            Col += (I - Start);
            continue;
        }

        // Line comments: # ... or // ...
        if (Ch === '#' || (Ch === '/' && Source[I + 1] === '/'))
        {
            while (I < N && Source[I] !== '\n') I++;
            Tokens.push({ Kind: 'comment', Text: Source.slice(Start, I), Start, End: I, Line: StartLine, Col: StartCol });
            Col += (I - Start);
            continue;
        }

        // Numbers (including scientific notation)
        if ((Ch >= '0' && Ch <= '9') || (Ch === '.' && Source[I + 1] >= '0' && Source[I + 1] <= '9'))
        {
            while (I < N && Source[I] >= '0' && Source[I] <= '9') I++;
            if (Source[I] === '.')
            {
                I++;
                while (I < N && Source[I] >= '0' && Source[I] <= '9') I++;
            }
            if ((Source[I] === 'e' || Source[I] === 'E') && (Source[I + 1] === '+' || Source[I + 1] === '-' || (Source[I + 1] >= '0' && Source[I + 1] <= '9')))
            {
                I++;
                if (Source[I] === '+' || Source[I] === '-') I++;
                while (I < N && Source[I] >= '0' && Source[I] <= '9') I++;
            }
            const Text = Source.slice(Start, I);
            Tokens.push({ Kind: 'number', Text, Value: Number(Text), Start, End: I, Line: StartLine, Col: StartCol });
            Col += (I - Start);
            continue;
        }

        // Identifiers, keywords, built-in symbols & combining-hat unit vectors
        if (IsIdentStart(Ch))
        {
            I++;
            while (I < N && IsIdentCont(Source[I])) I++;
            if (Source[I] === '\u0302') I++; // combining circumflex (e.g. r̂, x̂, ŷ, ẑ, θ̂)
            const Text = Source.slice(Start, I);
            let Kind = 'ident';
            if (Text === 'let') Kind = 'keyword';
            else if (HAT_ALIASES[Text]) Kind = 'hatvec';
            else if (BUILTIN_FNS[Text]) Kind = 'builtin_fn';
            else if (BUILTIN_SYMS[Text]) Kind = 'builtin_sym';
            Tokens.push({ Kind, Text, Start, End: I, Line: StartLine, Col: StartCol });
            Col += (I - Start);
            continue;
        }

        // Operators (ASCII + Unicode math operators)
        if (Ch === '+' || Ch === '-' || Ch === '\u2212' || Ch === '*' || Ch === '\u00b7' || Ch === '\u22c5' || Ch === '\u00d7' || Ch === '/' || Ch === '^' || Ch === '=')
        {
            I++;
            const Text = Source.slice(Start, I);
            const Norm = Ch === '\u2212' ? '-' : (Ch === '\u00b7' || Ch === '\u22c5') ? 'dot_mul' : Ch === '\u00d7' ? 'cross_op' : Text;
            Tokens.push({ Kind: 'op', Text, Op: Norm, Start, End: I, Line: StartLine, Col: StartCol });
            Col++;
            continue;
        }

        if (Ch === '(' || Ch === ')' || Ch === ',' || Ch === '.')
        {
            I++;
            Tokens.push({ Kind: 'punct', Text: Ch, Start, End: I, Line: StartLine, Col: StartCol });
            Col++;
            continue;
        }

        I++;
        Tokens.push({ Kind: 'error', Text: Ch, Start, End: I, Line: StartLine, Col: StartCol });
        Col++;
    }

    return Tokens;
}

//------------------------------------------------------------------------------------------------------------------------
//                                      2. RECURSIVE-DESCENT PARSER & TYPE CHECKER
//------------------------------------------------------------------------------------------------------------------------

export function ParseAndTypeCheck(Source)
{
    const AllTokens = TokenizeFieldLang(Source);
    const Stream = AllTokens.filter((T) => T.Kind !== 'ws' && T.Kind !== 'comment');
    const Diagnostics = [];
    let Pos = 0;

    const Peek = (Offset = 0) => Stream[Pos + Offset] || null;
    const Next = () => Stream[Pos++] || null;
    const SkipNewlines = () => { while (Peek()?.Kind === 'newline') Pos++; };

    const Fail = (Tok, Msg) => {
        const T = Tok || Stream[Math.max(0, Pos - 1)] || { Start: Source.length, End: Source.length, Line: 1, Col: 1 };
        const Err = new SyntaxError(Msg);
        Err.Diagnostic = { Message: Msg, Start: T.Start, End: Math.max(T.End, T.Start + 1), Line: T.Line, Col: T.Col };
        throw Err;
    };

    const ParseExpr = () => ParseAddSub();

    const ParseAddSub = () => {
        let Left = ParseMulDiv();
        while (Peek()?.Kind === 'op' && (Peek().Op === '+' || Peek().Op === '-'))
        {
            const OpTok = Next();
            const Right = ParseMulDiv();
            Left = { Type: 'Binary', Op: OpTok.Op, Left, Right, Start: Left.Start, End: Right.End, Tok: OpTok };
        }
        return Left;
    };

    const ParseMulDiv = () => {
        let Left = ParseUnary();
        while (Peek()?.Kind === 'op' && (Peek().Op === '*' || Peek().Op === 'dot_mul' || Peek().Op === 'cross_op' || Peek().Op === '/'))
        {
            const OpTok = Next();
            const Right = ParseUnary();
            Left = { Type: 'Binary', Op: OpTok.Op, Left, Right, Start: Left.Start, End: Right.End, Tok: OpTok };
        }
        return Left;
    };

    const ParseUnary = () => {
        if (Peek()?.Kind === 'op' && (Peek().Op === '+' || Peek().Op === '-'))
        {
            const OpTok = Next();
            const Operand = ParseUnary();
            return { Type: 'Unary', Op: OpTok.Op, Operand, Start: OpTok.Start, End: Operand.End, Tok: OpTok };
        }
        return ParsePower();
    };

    const ParsePower = () => {
        const Base = ParsePostfix();
        if (Peek()?.Kind === 'op' && Peek().Op === '^')
        {
            const OpTok = Next();
            const Exp = ParseUnary();
            return { Type: 'Binary', Op: '^', Left: Base, Right: Exp, Start: Base.Start, End: Exp.End, Tok: OpTok };
        }
        return Base;
    };

    const ParsePostfix = () => {
        let Node = ParsePrimary();
        while (Peek()?.Kind === 'punct' && Peek().Text === '.')
        {
            const DotTok = Next();
            const Field = Next();
            if (!Field || !['ident', 'builtin_sym'].includes(Field.Kind)) Fail(DotTok, 'Expected component (.x, .y, .z, .xz) after "."');
            Node = { Type: 'Swizzle', Target: Node, Field: Field.Text, Start: Node.Start, End: Field.End, Tok: Field };
        }
        return Node;
    };

    const ParsePrimary = () => {
        const T = Peek();
        if (!T || T.Kind === 'newline') Fail(T, 'Expected expression, variable, or number');
        if (T.Kind === 'error') Fail(T, `Unexpected character "${T.Text}"`);

        if (T.Kind === 'number')
        {
            Next();
            return { Type: 'Number', Value: T.Value, Start: T.Start, End: T.End, Tok: T };
        }

        if (T.Kind === 'punct' && T.Text === '(')
        {
            Next();
            const Inner = ParseExpr();
            const Close = Peek();
            if (!Close || Close.Text !== ')') Fail(Close || T, 'Missing closing ")"');
            Next();
            return { ...Inner, Start: T.Start, End: Close.End };
        }

        if (T.Kind === 'hatvec' || T.Kind === 'builtin_sym' || T.Kind === 'ident' || T.Kind === 'builtin_fn')
        {
            Next();
            if (Peek()?.Kind === 'punct' && Peek().Text === '(')
            {
                Next();
                const Args = [];
                if (Peek()?.Text !== ')')
                {
                    while (true)
                    {
                        Args.push(ParseExpr());
                        if (Peek()?.Text === ',') { Next(); continue; }
                        break;
                    }
                }
                const Close = Peek();
                if (!Close || Close.Text !== ')') Fail(Close || T, `Expected ")" closing call to ${T.Text}(...)`);
                Next();
                return { Type: 'Call', Name: T.Text, Args, Start: T.Start, End: Close.End, Tok: T };
            }
            return { Type: 'Symbol', Name: T.Text, Kind: T.Kind, Start: T.Start, End: T.End, Tok: T };
        }

        Fail(T, `Unexpected token "${T.Text}"`);
    };

    // Parse statements
    const Statements = [];
    let OutputChannel = 'Auto';
    let HasOutput = false;

    try
    {
        SkipNewlines();
        while (Pos < Stream.length)
        {
            const Cur = Peek();
            if (Cur.Kind === 'keyword' && Cur.Text === 'let')
            {
                Next();
                const NameTok = Next();
                if (!NameTok || !['ident', 'builtin_sym', 'hatvec'].includes(NameTok.Kind))
                {
                    Fail(NameTok || Cur, 'Expected variable name after "let"');
                }
                const Eq = Next();
                if (!Eq || Eq.Op !== '=') Fail(Eq || NameTok, 'Expected "=" in let binding');
                const Expr = ParseExpr();
                Statements.push({ Type: 'Let', Name: NameTok.Text, Expr, Start: Cur.Start, End: Expr.End, Tok: NameTok });
            }
            else if ((Cur.Kind === 'ident' || Cur.Kind === 'builtin_sym') && Peek(1)?.Op === '=')
            {
                const Lhs = Next();
                Next(); // '='
                const Expr = ParseExpr();
                if (Lhs.Text === 'a' || Lhs.Text === 'v' || Lhs.Text === 'f')
                {
                    OutputChannel = Lhs.Text === 'v' ? 'Velocity' : 'Accelerate';
                    HasOutput = true;
                    Statements.push({ Type: 'Output', Channel: Lhs.Text, Expr, Start: Lhs.Start, End: Expr.End, Tok: Lhs });
                }
                else
                {
                    Statements.push({ Type: 'Let', Name: Lhs.Text, Expr, Start: Lhs.Start, End: Expr.End, Tok: Lhs });
                }
            }
            else
            {
                const Expr = ParseExpr();
                HasOutput = true;
                Statements.push({ Type: 'Output', Channel: 'f', Expr, Start: Expr.Start, End: Expr.End, Tok: Expr.Tok });
            }

            if (Pos < Stream.length && Peek()?.Kind !== 'newline')
            {
                Fail(Peek(), `Unexpected token "${Peek().Text}" — expected end of line or operator`);
            }
            SkipNewlines();
        }

        if (!HasOutput) Fail(null, 'Formula must produce a vector output, e.g.: a = -S · A(r) · r̂ + ω_swirl · (ŷ × r̂)');

        // Static Type Inference Pass
        const Env = new Map();
        const Infer = (Node) => {
            if (Node.Type === 'Number') { Node.ValType = 'scalar'; return 'scalar'; }
            if (Node.Type === 'Symbol')
            {
                if (HAT_ALIASES[Node.Name]) { Node.Canonical = HAT_ALIASES[Node.Name]; Node.ValType = 'vec3'; return 'vec3'; }
                if (Env.has(Node.Name)) { Node.ValType = Env.get(Node.Name); return Node.ValType; }
                if (BUILTIN_SYMS[Node.Name]) { Node.ValType = BUILTIN_SYMS[Node.Name]; return Node.ValType; }
                Fail(Node.Tok, `Unknown symbol "${Node.Name}". Try p, c, d, r, rho, S, R, ω_swirl, r̂, x̂, ŷ, ẑ`);
            }
            if (Node.Type === 'Swizzle')
            {
                const TT = Infer(Node.Target);
                if (TT !== 'vec3' && TT !== 'vec2') Fail(Node.Tok, `Cannot access ".${Node.Field}" on ${TT}`);
                if (!/^[xyz]{1,3}$/.test(Node.Field)) Fail(Node.Tok, `Invalid vector component ".${Node.Field}"`);
                if (TT === 'vec2' && Node.Field.includes('z')) Fail(Node.Tok, `vec2 has no .z component`);
                Node.ValType = Node.Field.length === 1 ? 'scalar' : Node.Field.length === 2 ? 'vec2' : 'vec3';
                return Node.ValType;
            }
            if (Node.Type === 'Unary')
            {
                Node.ValType = Infer(Node.Operand);
                return Node.ValType;
            }
            if (Node.Type === 'Call')
            {
                const Spec = BUILTIN_FNS[Node.Name];
                if (!Spec) Fail(Node.Tok, `Unknown function "${Node.Name}(...)"`);
                if (Node.Args.length < Spec.MinArgs || Node.Args.length > Spec.MaxArgs)
                {
                    Fail(Node.Tok, `"${Node.Name}" expects ${Spec.MinArgs}${Spec.MaxArgs > Spec.MinArgs ? `..${Spec.MaxArgs}` : ''} argument(s), got ${Node.Args.length}`);
                }
                const ArgTypes = Node.Args.map((A) => Infer(A));
                if (Node.Name === 'vec3' && ArgTypes.length !== 1 && ArgTypes.length !== 3)
                {
                    Fail(Node.Tok, 'vec3 expects either 1 or 3 scalar arguments');
                }
                for (let K = 0; K < ArgTypes.length; ++K)
                {
                    const Expected = Spec.Args[K];
                    if (Expected && Expected !== 'any' && ArgTypes[K] !== Expected)
                    {
                        Fail(Node.Args[K].Tok || Node.Tok, `"${Node.Name}" arg ${K + 1} expects ${Expected}, got ${ArgTypes[K]}`);
                    }
                }
                Node.ValType = Spec.Ret === 'same0' ? ArgTypes[0] : Spec.Ret;
                return Node.ValType;
            }
            if (Node.Type === 'Binary')
            {
                const L = Infer(Node.Left), R = Infer(Node.Right);
                if (Node.Op === 'cross_op')
                {
                    if (L !== 'vec3' || R !== 'vec3') Fail(Node.Tok, `Cross product "×" requires vec3 × vec3 (got ${L} × ${R})`);
                    Node.ValType = 'vec3';
                    return 'vec3';
                }
                if (Node.Op === '^')
                {
                    if (L !== 'scalar' || R !== 'scalar') Fail(Node.Tok, `Exponent "^" requires scalar ^ scalar (got ${L} ^ ${R})`);
                    Node.ValType = 'scalar';
                    return 'scalar';
                }
                if (Node.Op === '/')
                {
                    if (R !== 'scalar') Fail(Node.Tok, `Denominator of "/" must be scalar (got ${R})`);
                    Node.ValType = L;
                    return L;
                }
                if (Node.Op === 'dot_mul' || Node.Op === '*')
                {
                    if (L === 'scalar' && R === 'scalar') { Node.ValType = 'scalar'; return 'scalar'; }
                    if (L === 'scalar') { Node.ValType = R; return R; }
                    if (R === 'scalar') { Node.ValType = L; return L; }
                    if (L === R)
                    {
                        if (Node.Op === 'dot_mul') { Node.ValType = 'scalar'; Node.IsVectorDot = true; return 'scalar'; }
                        Node.ValType = L; return L;
                    }
                    Fail(Node.Tok, `Incompatible types ${L} and ${R} for "${Node.Op === 'dot_mul' ? '·' : '*'}"`);
                }
                if (Node.Op === '+' || Node.Op === '-')
                {
                    if (L !== R) Fail(Node.Tok, `Cannot ${Node.Op === '+' ? 'add' : 'subtract'} ${L} and ${R}`);
                    Node.ValType = L;
                    return L;
                }
            }
            Fail(Node.Tok, 'Unsupported AST node');
        };

        for (const Stmt of Statements)
        {
            const T = Infer(Stmt.Expr);
            if (Stmt.Type === 'Let')
            {
                Env.set(Stmt.Name, T);
            }
            else if (Stmt.Type === 'Output' && T !== 'vec3')
            {
                Fail(Stmt.Expr.Tok || Stmt.Tok, `Field output "${Stmt.Channel} = ..." must evaluate to vec3 (currently evaluates to ${T})`);
            }
        }

        return { Ok: true, Tokens: AllTokens, Statements, OutputChannel, Diagnostics: [] };
    }
    catch (Err)
    {
        if (Err.Diagnostic) Diagnostics.push(Err.Diagnostic);
        else Diagnostics.push({ Message: String(Err.message || Err), Start: 0, End: Math.max(1, Source.length), Line: 1, Col: 1 });
        return { Ok: false, Tokens: AllTokens, Statements: [], OutputChannel: 'Accelerate', Diagnostics };
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                     3. SYNTAX HIGHLIGHTER (EXACT TOKEN OFFSET LAYER)
//------------------------------------------------------------------------------------------------------------------------

function EscapeHtml(Str)
{
    return Str.replaceAll('&', '&amp;').replaceAll('<', '&lt;').replaceAll('>', '&gt;');
}

export function RenderHighlightedFormulaHtml(Source, Parsed)
{
    const Tokens = Parsed?.Tokens || TokenizeFieldLang(Source);
    const Diags = Parsed?.Diagnostics || [];
    const ErrRange = Diags[0] || null;

    let Out = '';
    for (const Tok of Tokens)
    {
        const Raw = EscapeHtml(Tok.Text);
        if (Tok.Kind === 'ws' || Tok.Kind === 'newline')
        {
            Out += Raw;
            continue;
        }
        const IsErr = Tok.Kind === 'error' || (ErrRange && Tok.Start < ErrRange.End && Tok.End > ErrRange.Start);
        let Cls = `vfl-tok-${Tok.Kind}`;
        if (Tok.Kind === 'builtin_sym' && ['S', 'R', '\u03c9', '\u03c9_swirl', 'omega', 'omega_swirl', '\u03c0', 'pi', '\u03c4', 'tau', 'e'].includes(Tok.Text))
        {
            Cls = 'vfl-tok-param';
        }
        if ((Tok.Kind === 'ident' || Tok.Kind === 'builtin_sym') && ['a', 'v', 'f'].includes(Tok.Text))
        {
            Cls = 'vfl-tok-channel';
        }
        if (IsErr) Cls += ' vfl-tok-err';
        Out += `<span class="${Cls}">${Raw}</span>`;
    }
    if (Source.endsWith('\n') || Source.length === 0) Out += ' ';
    return Out;
}

//------------------------------------------------------------------------------------------------------------------------
//                                           4A. JAVASCRIPT RUNTIME COMPILER
//------------------------------------------------------------------------------------------------------------------------

function SafeJsVar(Name)
{
    return 'v_' + Name.replace(/[^\w]/g, (C) => '_' + C.codePointAt(0).toString(16));
}

export function CompileProgramToJS(Parsed)
{
    if (!Parsed?.Ok) return null;

    const Emit = (Node) => {
        if (Node.Type === 'Number') return `{ k: 's', v: ${Number.isFinite(Node.Value) ? Node.Value : 0} }`;
        if (Node.Type === 'Symbol')
        {
            if (Node.Canonical)
            {
                if (Node.Canonical === 'x_hat') return `{ k: 'v3', x: 1, y: 0, z: 0 }`;
                if (Node.Canonical === 'y_hat') return `{ k: 'v3', x: 0, y: 1, z: 0 }`;
                if (Node.Canonical === 'z_hat') return `{ k: 'v3', x: 0, y: 0, z: 1 }`;
                if (Node.Canonical === 'r_hat') return `{ k: 'v3', x: rHatX, y: rHatY, z: rHatZ }`;
                if (Node.Canonical === 'theta_hat') return `{ k: 'v3', x: tHatX, y: 0, z: tHatZ }`;
            }
            if (Node.Name === 'p') return `{ k: 'v3', x: px, y: py, z: pz }`;
            if (Node.Name === 'c') return `{ k: 'v3', x: cx, y: cy, z: cz }`;
            if (Node.Name === 'd') return `{ k: 'v3', x: dx, y: dy, z: dz }`;
            if (Node.Name === 'r') return `{ k: 's', v: r }`;
            if (Node.Name === 'rho') return `{ k: 's', v: rho }`;
            if (Node.Name === 't') return `{ k: 's', v: time }`;
            if (Node.Name === 'x') return `{ k: 's', v: dx }`;
            if (Node.Name === 'y') return `{ k: 's', v: dy }`;
            if (Node.Name === 'z') return `{ k: 's', v: dz }`;
            if (Node.Name === 'S') return `{ k: 's', v: S }`;
            if (Node.Name === 'R') return `{ k: 's', v: R }`;
            if (['\u03c9', '\u03c9_swirl', 'omega', 'omega_swirl'].includes(Node.Name)) return `{ k: 's', v: swirl }`;
            if (Node.Name === '\u03c0' || Node.Name === 'pi') return `{ k: 's', v: Math.PI }`;
            if (Node.Name === '\u03c4' || Node.Name === 'tau') return `{ k: 's', v: Math.PI * 2 }`;
            if (Node.Name === 'e') return `{ k: 's', v: Math.E }`;
            return SafeJsVar(Node.Name);
        }
        if (Node.Type === 'Swizzle')
        {
            const Src = Emit(Node.Target);
            if (Node.Field.length === 1) return `{ k: 's', v: (${Src}).${Node.Field} }`;
            if (Node.Field.length === 2) return `{ k: 'v2', x: (${Src}).${Node.Field[0]}, y: (${Src}).${Node.Field[1]} }`;
            return `{ k: 'v3', x: (${Src}).${Node.Field[0]}, y: (${Src}).${Node.Field[1]}, z: (${Src}).${Node.Field[2]} }`;
        }
        if (Node.Type === 'Unary')
        {
            const A = Emit(Node.Operand);
            if (Node.Op === '+') return A;
            if (Node.ValType === 'scalar') return `{ k: 's', v: -(${A}).v }`;
            if (Node.ValType === 'vec2') return `{ k: 'v2', x: -(${A}).x, y: -(${A}).y }`;
            return `{ k: 'v3', x: -(${A}).x, y: -(${A}).y, z: -(${A}).z }`;
        }
        if (Node.Type === 'Binary')
        {
            const L = Emit(Node.Left), R = Emit(Node.Right);
            if (Node.Op === 'cross_op') return `vfCross(${L}, ${R})`;
            if (Node.IsVectorDot) return `{ k: 's', v: vfDot(${L}, ${R}) }`;
            if (Node.Op === '^') return `{ k: 's', v: Math.pow(Math.max(1e-6, (${L}).v), (${R}).v) }`;
            return `vfBin('${Node.Op}', ${L}, ${R})`;
        }
        if (Node.Type === 'Call')
        {
            const Args = Node.Args.map(Emit).join(', ');
            return `vfCall('${Node.Name}', [${Args}], ctx)`;
        }
        return `{ k: 's', v: 0 }`;
    };

    const Lines = [
        'const dx = px - cx, dy = py - cy, dz = pz - cz;',
        'const r = Math.sqrt(dx * dx + dy * dy + dz * dz);',
        'const rho = Math.hypot(dx, dz);',
        'const rHatX = r > 1e-4 ? dx / r : 0, rHatY = r > 1e-4 ? dy / r : 0, rHatZ = r > 1e-4 ? dz / r : 0;',
        'const tHatLen = Math.hypot(rHatZ, rHatX);',
        'const tHatX = tHatLen > 1e-4 ? rHatZ / tHatLen : 0, tHatZ = tHatLen > 1e-4 ? -rHatX / tHatLen : 0;',
        'let outX = 0, outY = 0, outZ = 0;'
    ];
    for (const Stmt of Parsed.Statements)
    {
        if (Stmt.Type === 'Let')
        {
            Lines.push(`const ${SafeJsVar(Stmt.Name)} = ${Emit(Stmt.Expr)};`);
        }
        else if (Stmt.Type === 'Output')
        {
            Lines.push(`const _res = ${Emit(Stmt.Expr)}; outX = _res.x; outY = _res.y; outZ = _res.z;`);
        }
    }
    Lines.push('return [outX, outY, outZ];');

    const RawFn = new Function(
        'px', 'py', 'pz', 'cx', 'cy', 'cz', 'R', 'S', 'swirl', 'time', 'ctx',
        'vfCross', 'vfDot', 'vfBin', 'vfCall',
        Lines.join('\n')
    );

    const VfCross = (a, b) => ({ k: 'v3', x: a.y * b.z - a.z * b.y, y: a.z * b.x - a.x * b.z, z: a.x * b.y - a.y * b.x });
    const VfDot = (a, b) => a.x * b.x + a.y * b.y + a.z * b.z;
    const VfBin = (op, a, b) => {
        if (a.k === 's' && b.k === 's')
        {
            if (op === '+') return { k: 's', v: a.v + b.v };
            if (op === '-') return { k: 's', v: a.v - b.v };
            if (op === '*' || op === 'dot_mul') return { k: 's', v: a.v * b.v };
            return { k: 's', v: Math.abs(b.v) > 1e-6 ? a.v / b.v : 0 };
        }
        if (a.k === 's' && b.k === 'v3') return { k: 'v3', x: a.v * b.x, y: a.v * b.y, z: a.v * b.z };
        if (a.k === 'v3' && b.k === 's')
        {
            const s = op === '/' ? (Math.abs(b.v) > 1e-6 ? 1 / b.v : 0) : b.v;
            return { k: 'v3', x: a.x * s, y: a.y * s, z: a.z * s };
        }
        if (a.k === 's' && b.k === 'v2') return { k: 'v2', x: a.v * b.x, y: a.v * b.y };
        if (a.k === 'v2' && b.k === 's')
        {
            const s = op === '/' ? (Math.abs(b.v) > 1e-6 ? 1 / b.v : 0) : b.v;
            return { k: 'v2', x: a.x * s, y: a.y * s };
        }
        if (a.k === 'v3' && b.k === 'v3')
        {
            if (op === '+') return { k: 'v3', x: a.x + b.x, y: a.y + b.y, z: a.z + b.z };
            if (op === '-') return { k: 'v3', x: a.x - b.x, y: a.y - b.y, z: a.z - b.z };
            return { k: 'v3', x: a.x * b.x, y: a.y * b.y, z: a.z * b.z };
        }
        if (a.k === 'v2' && b.k === 'v2')
        {
            if (op === '+') return { k: 'v2', x: a.x + b.x, y: a.y + b.y };
            if (op === '-') return { k: 'v2', x: a.x - b.x, y: a.y - b.y };
            return { k: 'v2', x: a.x * b.x, y: a.y * b.y };
        }
        return a;
    };
    const VfCall = (name, args, ctx) => {
        const s0 = args[0]?.v ?? 0, s1 = args[1]?.v ?? 0, s2 = args[2]?.v ?? 0;
        if (name === 'A' || name === 'attenuate') return { k: 's', v: ctx.Attenuate(s0, args.length > 1 ? s1 : ctx.R) };
        if (name === 'G' || name === 'gate') return { k: 's', v: ctx.Gate };
        if (name === 'sin') return { k: 's', v: Math.sin(s0) };
        if (name === 'cos') return { k: 's', v: Math.cos(s0) };
        if (name === 'tan') return { k: 's', v: Math.tan(s0) };
        if (name === 'tanh') return { k: 's', v: Math.tanh(s0) };
        if (name === 'exp') return { k: 's', v: Math.exp(Math.max(-20, Math.min(20, s0))) };
        if (name === 'sqrt') return { k: 's', v: Math.sqrt(Math.max(0, s0)) };
        if (name === 'abs') return { k: 's', v: Math.abs(s0) };
        if (name === 'pow') return { k: 's', v: Math.pow(Math.max(1e-6, s0), s1) };
        if (name === 'min') return { k: 's', v: Math.min(s0, s1) };
        if (name === 'max') return { k: 's', v: Math.max(s0, s1) };
        if (name === 'clamp') return { k: 's', v: Math.max(s1, Math.min(s2, s0)) };
        if (name === 'smoothstep')
        {
            const u = Math.max(0, Math.min(1, (s2 - s0) / Math.max(1e-5, s1 - s0)));
            return { k: 's', v: u * u * (3 - 2 * u) };
        }
        if (name === 'mix')
        {
            if (args[0].k === 'v3') return { k: 'v3', x: args[0].x + (args[1].x - args[0].x) * s2, y: args[0].y + (args[1].y - args[0].y) * s2, z: args[0].z + (args[1].z - args[0].z) * s2 };
            return { k: 's', v: s0 + (s1 - s0) * s2 };
        }
        if (name === 'dot') return { k: 's', v: VfDot(args[0], args[1]) };
        if (name === 'cross') return VfCross(args[0], args[1]);
        if (name === 'length') return { k: 's', v: Math.hypot(args[0].x, args[0].y, args[0].z) };
        if (name === 'norm' || name === 'normalize')
        {
            const v = args[0], len = Math.hypot(v.x, v.y, v.z);
            return len > 1e-4 ? { k: 'v3', x: v.x / len, y: v.y / len, z: v.z / len } : { k: 'v3', x: 0, y: 0, z: 0 };
        }
        if (name === 'vec2') return { k: 'v2', x: s0, y: s1 };
        if (name === 'vec3') return args.length === 1 ? { k: 'v3', x: s0, y: s0, z: s0 } : { k: 'v3', x: s0, y: s1, z: s2 };
        if (name === 'curlNoise')
        {
            const v = args[0], sc = args.length > 1 ? s1 : 48;
            const res = ctx.CurlNoise(v.x, v.y, v.z, sc);
            return { k: 'v3', x: res[0], y: res[1], z: res[2] };
        }
        if (name === 'noise3')
        {
            const v = args[0], sc = args.length > 1 ? s1 : 48;
            const res = ctx.Noise3(v.x / Math.max(4, sc), v.y / Math.max(4, sc), v.z / Math.max(4, sc));
            return { k: 'v3', x: res[0], y: res[1], z: res[2] };
        }
        return { k: 's', v: 0 };
    };

    return (px, py, pz, cx, cy, cz, R, S, swirl, time, ctx) =>
        RawFn(px, py, pz, cx, cy, cz, R, S, swirl, time, ctx, VfCross, VfDot, VfBin, VfCall);
}

//------------------------------------------------------------------------------------------------------------------------
//                                      4B. GLSL ES 3.00 LIVE GPU SHADER COMPILER
//------------------------------------------------------------------------------------------------------------------------

function GlslVar(Name)
{
    return 'u_' + Name.replace(/[^\w]/g, (C) => '_' + C.codePointAt(0).toString(16));
}

function GlslType(T)
{
    return T === 'vec3' ? 'vec3' : T === 'vec2' ? 'vec2' : 'float';
}

export function CompileProgramToGLSL(Parsed, SlotIndex = 0)
{
    if (!Parsed?.Ok) return null;

    const Emit = (Node) => {
        if (Node.Type === 'Number')
        {
            const S = String(Number(Node.Value));
            return S.includes('.') || S.includes('e') || S.includes('E') ? S : `${S}.0`;
        }
        if (Node.Type === 'Symbol')
        {
            if (Node.Canonical)
            {
                if (Node.Canonical === 'x_hat') return 'vec3(1.0, 0.0, 0.0)';
                if (Node.Canonical === 'y_hat') return 'vec3(0.0, 1.0, 0.0)';
                if (Node.Canonical === 'z_hat') return 'vec3(0.0, 0.0, 1.0)';
                if (Node.Canonical === 'r_hat') return 'rHat';
                if (Node.Canonical === 'theta_hat') return 'thetaHat';
            }
            if (Node.Name === 'p') return 'p';
            if (Node.Name === 'c') return 'c';
            if (Node.Name === 'd') return 'd';
            if (Node.Name === 'r') return 'r';
            if (Node.Name === 'rho') return 'rho';
            if (Node.Name === 't') return 'time';
            if (Node.Name === 'x') return 'd.x';
            if (Node.Name === 'y') return 'd.y';
            if (Node.Name === 'z') return 'd.z';
            if (Node.Name === 'S') return 'strength';
            if (Node.Name === 'R') return 'rMax';
            if (['\u03c9', '\u03c9_swirl', 'omega', 'omega_swirl'].includes(Node.Name)) return 'swirl';
            if (Node.Name === '\u03c0' || Node.Name === 'pi') return '3.14159265';
            if (Node.Name === '\u03c4' || Node.Name === 'tau') return '6.28318530';
            if (Node.Name === 'e') return '2.71828182';
            return GlslVar(Node.Name);
        }
        if (Node.Type === 'Swizzle') return `(${Emit(Node.Target)}).${Node.Field}`;
        if (Node.Type === 'Unary') return Node.Op === '+' ? Emit(Node.Operand) : `(-(${Emit(Node.Operand)}))`;
        if (Node.Type === 'Binary')
        {
            const L = Emit(Node.Left), R = Emit(Node.Right);
            if (Node.Op === 'cross_op') return `cross(${L}, ${R})`;
            if (Node.IsVectorDot) return `dot(${L}, ${R})`;
            if (Node.Op === '^') return `pow(max(1e-5, ${L}), ${R})`;
            if (Node.Op === 'dot_mul') return `(${L} * ${R})`;
            if (Node.Op === '/') return `(${L} / ((${R}) >= 0.0 ? max(${R}, 1e-5) : min(${R}, -1e-5)))`;
            return `(${L} ${Node.Op} ${R})`;
        }
        if (Node.Type === 'Call')
        {
            const Args = Node.Args.map(Emit);
            if (Node.Name === 'A' || Node.Name === 'attenuate')
            {
                return `vfAttenuate(attenCode, ${Args[0]}, ${Args[1] || 'rMax'})`;
            }
            if (Node.Name === 'G' || Node.Name === 'gate') return 'gate';
            if (Node.Name === 'exp') return `exp(clamp(${Args[0]}, -20.0, 20.0))`;
            if (Node.Name === 'sqrt') return `sqrt(max(0.0, ${Args[0]}))`;
            if (Node.Name === 'pow') return `pow(max(1e-5, ${Args[0]}), ${Args[1]})`;
            if (Node.Name === 'norm' || Node.Name === 'normalize')
            {
                return `vfSafeNorm(${Args[0]})`;
            }
            if (Node.Name === 'curlNoise') return `vfCurlNoise(${Args[0]} + vec3(time * 4.2, 0.0, 0.0), ${Args[1] || '48.0'}, 7331.0)`;
            if (Node.Name === 'noise3') return `vfFastNoise3(${Args[0]} / max(4.0, ${Args[1] || '48.0'}), floor(time * 2.0), 7.0)`;
            return `${Node.Name}(${Args.join(', ')})`;
        }
        return '0.0';
    };

    const BodyLines = [
        `vec3 vfEvalCustomFormula${SlotIndex}(vec3 p, vec3 c, vec3 d, float rMax, float strength, float swirl, float time, int attenCode, float gate)`,
        '{',
        '    float r = length(d);',
        '    float rho = length(d.xz);',
        '    vec3 rHat = r > 1e-4 ? d / r : vec3(0.0);',
        '    vec3 thetaHat = vfSafeNorm(cross(vec3(0.0, 1.0, 0.0), rHat));',
        '    vec3 fOut = vec3(0.0);'
    ];

    for (const Stmt of Parsed.Statements)
    {
        if (Stmt.Type === 'Let')
        {
            BodyLines.push(`    ${GlslType(Stmt.Expr.ValType)} ${GlslVar(Stmt.Name)} = ${Emit(Stmt.Expr)};`);
        }
        else if (Stmt.Type === 'Output')
        {
            BodyLines.push(`    fOut = ${Emit(Stmt.Expr)};`);
        }
    }
    BodyLines.push('    return fOut;', '}');
    return BodyLines.join('\n');
}

export function CompileFieldProgram(Source, SlotIndex = 0)
{
    const Parsed = ParseAndTypeCheck(Source);
    const HighlightHtml = RenderHighlightedFormulaHtml(Source, Parsed);
    if (!Parsed.Ok)
    {
        return {
            Ok: false,
            Source,
            Parsed,
            HighlightHtml,
            Diagnostics: Parsed.Diagnostics,
            OutputChannel: Parsed.OutputChannel,
            JsFn: null,
            GlslCode: ''
        };
    }

    const JsFn = CompileProgramToJS(Parsed);
    const GlslCode = CompileProgramToGLSL(Parsed, SlotIndex);
    return {
        Ok: true,
        Source,
        Parsed,
        HighlightHtml,
        Diagnostics: [],
        OutputChannel: Parsed.OutputChannel,
        JsFn,
        GlslCode
    };
}
