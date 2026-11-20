/*============================================================================================================================================
                                                     VECTOR FIELD LANG
============================================================================================================================================*/
// 🧩 Compiler for the custom Formula Field: tokenises `fx = …` channel expressions, parses them with a precedence-climbing parser and lowers
// each node to a plain closure. No `eval`, no `Function` constructor and no dynamic identifiers reach the browser: only the whitelisted
// functions below are ever callable, so a mis-typed formula reports a position-tagged error instead of executing anything unexpected.

//------------------------------------------------------------------------------------------------------------------------
//                                                  CONSTANTS & BUILTINS
//------------------------------------------------------------------------------------------------------------------------

const CONSTANTS = {
  pi: Math.PI,
  tau: Math.PI * 2,
  e: Math.E,
  phi: (1 + Math.sqrt(5)) / 2,
};

const BUILTINS = {
  sin: { Arity: 1, Fn: Math.sin },
  cos: { Arity: 1, Fn: Math.cos },
  tan: { Arity: 1, Fn: Math.tan },
  asin: { Arity: 1, Fn: Math.asin },
  acos: { Arity: 1, Fn: Math.acos },
  atan: { Arity: 1, Fn: Math.atan },
  atan2: { Arity: 2, Fn: Math.atan2 },
  abs: { Arity: 1, Fn: Math.abs },
  sqrt: { Arity: 1, Fn: Math.sqrt },
  exp: { Arity: 1, Fn: Math.exp },
  log: { Arity: 1, Fn: Math.log },
  sign: { Arity: 1, Fn: Math.sign },
  floor: { Arity: 1, Fn: Math.floor },
  ceil: { Arity: 1, Fn: Math.ceil },
  round: { Arity: 1, Fn: Math.round },
  fract: { Arity: 1, Fn: (a) => a - Math.floor(a) },
  min: { Arity: 2, Fn: Math.min },
  max: { Arity: 2, Fn: Math.max },
  pow: { Arity: 2, Fn: Math.pow },
  mod: { Arity: 2, Fn: (a, b) => (b === 0 ? 0 : a - b * Math.floor(a / b)) },
  step: { Arity: 2, Fn: (edge, a) => (a < edge ? 0 : 1) },
  clamp: { Arity: 3, Fn: (a, lo, hi) => (a < lo ? lo : a > hi ? hi : a) },
  mix: { Arity: 3, Fn: (a, b, u) => a + (b - a) * u },
  smoothstep: {
    Arity: 3,
    Fn: (e0, e1, a) => {
      if (e0 === e1) return a < e0 ? 0 : 1;
      const u = Math.min(1, Math.max(0, (a - e0) / (e1 - e0)));
      return u * u * (3 - 2 * u);
    },
  },
};

export const FORMULA_VARIABLES = ['x', 'y', 'z', 't', 'r', 'a'];
export const FORMULA_FUNCTIONS = Object.keys(BUILTINS).sort();

//------------------------------------------------------------------------------------------------------------------------
//                                                  TOKENISER
//------------------------------------------------------------------------------------------------------------------------

const isDigit = (c) => c >= '0' && c <= '9';
const isIdentStart = (c) => /[A-Za-z_]/.test(c);
const isIdentPart = (c) => /[A-Za-z0-9_]/.test(c);

function Tokenize(text) {
  const tokens = [];
  let i = 0;
  while (i < text.length) {
    const c = text[i];
    if (c === '\n' || c === ' ' || c === '\t' || c === '\r') {
      i++;
      continue;
    }
    if (isDigit(c) || (c === '.' && isDigit(text[i + 1]))) {
      const start = i;
      while (i < text.length && (isDigit(text[i]) || text[i] === '.')) i++;
      if (text[i] === 'e' || text[i] === 'E') {
        const save = i;
        i++;
        if (text[i] === '+' || text[i] === '-') i++;
        if (isDigit(text[i])) {
          while (i < text.length && isDigit(text[i])) i++;
        } else i = save;
      }
      const raw = text.slice(start, i);
      const value = Number(raw);
      if (!Number.isFinite(value)) throw new Error(`Malformed number "${raw}" at column ${start + 1}`);
      tokens.push({ Kind: 'number', Value: value, At: start });
      continue;
    }
    if (isIdentStart(c)) {
      const start = i;
      while (i < text.length && isIdentPart(text[i])) i++;
      tokens.push({ Kind: 'ident', Value: text.slice(start, i), At: start });
      continue;
    }
    if ('+-*/^%(),'.includes(c)) {
      tokens.push({ Kind: 'punct', Value: c, At: i });
      i++;
      continue;
    }
    throw new Error(`Unexpected character "${c}" at column ${i + 1}`);
  }
  tokens.push({ Kind: 'end', Value: '', At: text.length });
  return tokens;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  EXPRESSION PARSER → CLOSURES
//------------------------------------------------------------------------------------------------------------------------

const BinaryPrecedence = { '+': 1, '-': 1, '*': 2, '/': 2, '%': 2, '^': 3 };
const RightAssociative = new Set(['^']);

function ParseExpression(tokens, state, minPrecedence) {
  let left = ParseUnary(tokens, state);
  while (true) {
    const token = tokens[state.i];
    if (token.Kind !== 'punct' || !(token.Value in BinaryPrecedence)) break;
    const precedence = BinaryPrecedence[token.Value];
    if (precedence < minPrecedence) break;
    state.i++;
    const right = ParseExpression(tokens, state, RightAssociative.has(token.Value) ? precedence : precedence + 1);
    left = ApplyBinary(token.Value, left, right, token.At);
  }
  return left;
}

function ApplyBinary(op, a, b, at) {
  switch (op) {
    case '+': return (x, y, z, t) => a(x, y, z, t) + b(x, y, z, t);
    case '-': return (x, y, z, t) => a(x, y, z, t) - b(x, y, z, t);
    case '*': return (x, y, z, t) => a(x, y, z, t) * b(x, y, z, t);
    case '/': return (x, y, z, t) => {
      const d = b(x, y, z, t);
      return d === 0 ? 0 : a(x, y, z, t) / d;
    };
    case '%': return (x, y, z, t) => {
      const d = b(x, y, z, t);
      const n = a(x, y, z, t);
      return d === 0 ? 0 : n - d * Math.floor(n / d);
    };
    case '^': return (x, y, z, t) => Math.pow(a(x, y, z, t), b(x, y, z, t));
    default: throw new Error(`Unsupported operator "${op}" at column ${at + 1}`);
  }
}

function ParseUnary(tokens, state) {
  const token = tokens[state.i];
  if (token.Kind === 'punct' && (token.Value === '-' || token.Value === '+')) {
    state.i++;
    // Scientific calculators bind exponentiation tighter than a leading sign: -2 ^ 2 reads as -(2 ^ 2).
    const operand = token.Value === '-' ? ParseExpression(tokens, state, 3) : ParseUnary(tokens, state);
    return token.Value === '-' ? (x, y, z, t) => -operand(x, y, z, t) : operand;
  }
  return ParsePrimary(tokens, state);
}

function ParsePrimary(tokens, state) {
  const token = tokens[state.i];
  if (token.Kind === 'number') {
    state.i++;
    const value = token.Value;
    return () => value;
  }
  if (token.Kind === 'ident') {
    const name = token.Value.toLowerCase();
    state.i++;
    if (tokens[state.i].Kind === 'punct' && tokens[state.i].Value === '(') {
      const builtin = BUILTINS[name];
      if (!builtin) throw new Error(`Unknown function "${token.Value}" at column ${token.At + 1}`);
      state.i++;
      const args = ParseArguments(tokens, state, token);
      if (args.length !== builtin.Arity) {
        throw new Error(`"${token.Value}" takes ${builtin.Arity} argument${builtin.Arity === 1 ? '' : 's'}, ${args.length} given (column ${token.At + 1})`);
      }
      const fn = builtin.Fn;
      if (args.length === 1) { const a = args[0]; return (x, y, z, t) => fn(a(x, y, z, t)); }
      if (args.length === 2) { const [a, b] = args; return (x, y, z, t) => fn(a(x, y, z, t), b(x, y, z, t)); }
      const [a, b, c] = args;
      return (x, y, z, t) => fn(a(x, y, z, t), b(x, y, z, t), c(x, y, z, t));
    }
    if (name === 'x') return (x) => x;
    if (name === 'y') return (_x, y) => y;
    if (name === 'z') return (_x, _y, z) => z;
    if (name === 't') return (_x, _y, _z, t) => t;
    if (name === 'r') return (x, y, z) => Math.hypot(x, y, z);
    if (name === 'rho') return (x, y) => Math.hypot(x, y);
    if (name === 'a') return (x, y) => Math.atan2(y, x);
    if (Object.prototype.hasOwnProperty.call(CONSTANTS, name)) {
      const value = CONSTANTS[name];
      return () => value;
    }
    throw new Error(`Unknown identifier "${token.Value}" at column ${token.At + 1} — use x, y, z, t, r, a or a listed function`);
  }
  if (token.Kind === 'punct' && token.Value === '(') {
    state.i++;
    const inner = ParseExpression(tokens, state, 0);
    if (tokens[state.i].Kind !== 'punct' || tokens[state.i].Value !== ')') {
      throw new Error(`Missing ")" at column ${tokens[state.i].At + 1}`);
    }
    state.i++;
    return inner;
  }
  throw new Error(`Unexpected "${token.Value || 'end of line'}" at column ${token.At + 1}`);
}

function ParseArguments(tokens, state, openToken) {
  const args = [];
  if (tokens[state.i].Kind === 'punct' && tokens[state.i].Value === ')') {
    state.i++;
    return args;
  }
  while (true) {
    args.push(ParseExpression(tokens, state, 0));
    const token = tokens[state.i];
    if (token.Kind === 'punct' && token.Value === ',') {
      state.i++;
      continue;
    }
    if (token.Kind === 'punct' && token.Value === ')') {
      state.i++;
      return args;
    }
    throw new Error(`Expected "," or ")" in call to "${openToken.Value}" at column ${token.At + 1}`);
  }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  CHANNEL PROGRAM COMPILER
//------------------------------------------------------------------------------------------------------------------------

const DEFAULT_PROGRAM = 'fx = sin(y * 0.9 + t * 0.6)\nfy = cos(x * 0.9 - t * 0.5)\nfz = 0.35 * sin((x + y) * 0.45 + t)';

const clampResult = (v) => (Number.isFinite(v) ? Math.max(-1e4, Math.min(1e4, v)) : 0);

// Splits the source into assignments, tolerating comments (# or //), blank lines and `fx = …;` terminators.
function SplitAssignments(code) {
  const rows = String(code).split('\n');
  const assignments = new Map();
  const errors = [];
  rows.forEach((row, index) => {
    const line = row.replace(/\/\/.*$/, '').replace(/#.*$/, '').trim();
    if (!line) return;
    const match = line.match(/^(f?[xyz]|v[xyz])\s*=\s*(.+?);?$/i);
    if (!match) {
      errors.push({ Line: index + 1, Message: 'Each line must assign one channel, e.g. fx = sin(y + t)' });
      return;
    }
    const channel = `f${match[1].slice(-1).toLowerCase()}`;
    assignments.set(channel, { Source: match[2], Line: index + 1, At: row.indexOf('=') + 1 });
  });
  return { assignments, errors };
}

export function CompileFieldProgram(code) {
  const source = String(code ?? '').trim() ? String(code) : DEFAULT_PROGRAM;
  const { assignments, errors } = SplitAssignments(source);
  const channels = {};
  for (const axis of ['x', 'y', 'z']) {
    const channel = `f${axis}`;
    const entry = assignments.get(channel);
    if (!entry) {
      channels[channel] = () => 0;
      continue;
    }
    try {
      const tokens = Tokenize(entry.Source);
      const state = { i: 0 };
      const node = ParseExpression(tokens, state, 0);
      if (tokens[state.i].Kind !== 'end') {
        throw new Error(`Unexpected "${tokens[state.i].Value}" at column ${tokens[state.i].At + 1}`);
      }
      const raw = node;
      channels[channel] = (x, y, z, t) => clampResult(raw(x, y, z, t));
    } catch (error) {
      channels[channel] = () => 0;
      errors.push({ Line: entry.Line, Column: entry.At, Message: `${channel}: ${error.message}` });
    }
  }
  return {
    Ok: errors.length === 0,
    Errors: errors,
    Sample: (x, y, z, t) => [channels.fx(x, y, z, t), channels.fy(x, y, z, t), channels.fz(x, y, z, t)],
    ChannelCount: assignments.size,
    Source: source,
  };
}

export const DEFAULT_FORMULA_CODE = DEFAULT_PROGRAM;

export const FORMULA_PRESETS = Object.freeze([
  {
    Id: 'Breather',
    Label: 'Breather',
    Note: 'A radial pulse that inhales and exhales around the origin.',
    Code: 'fx = x * 0.22 * sin(t * 0.9 - r * 0.6)\nfy = y * 0.22 * sin(t * 0.9 - r * 0.6)\nfz = 0.5 * cos(r * 0.8 - t * 1.1)',
  },
  {
    Id: 'ShearWave',
    Label: 'Shear wave',
    Note: 'Horizontal layers sliding past one another with a travelling wave.',
    Code: 'fx = sin(y * 0.8 + t * 0.7) * cos(x * 0.18)\nfy = 0.18 * sin(x * 0.5 - t * 0.4)\nfz = 0.1 * cos((x + y) * 0.35 + t)',
  },
  {
    Id: 'Rose',
    Label: 'Rose',
    Note: 'Petal-shaped circulation whose count you can count on the slice.',
    Code: 'fx = 0.8 * cos(5 * a) * cos(a) - 0.55 * sin(a)\nfy = 0.8 * cos(5 * a) * sin(a) + 0.55 * cos(a)\nfz = 0.4 * sin(r * 0.9 - t * 1.4)',
  },
  {
    Id: 'Lattice',
    Label: 'Lattice',
    Note: 'A three-axis standing wave; useful as an even test pattern for the colormaps.',
    Code: 'fx = sin(x * 1.4) * cos(z * 1.1 + t)\nfy = sin(y * 1.4) * cos(x * 1.1 - t)\nfz = sin(z * 1.4) * cos(y * 1.1 + t)',
  },
  {
    Id: 'Helix',
    Label: 'Helix',
    Note: 'Upward helix flow around the vertical axis.',
    Code: 'fx = -sin(a) * r * 0.55\nfy = cos(a) * r * 0.55\nfz = 1.1 - r * 0.05',
  },
]);
