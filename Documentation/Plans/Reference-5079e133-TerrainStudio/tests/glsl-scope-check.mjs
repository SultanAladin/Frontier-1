/**
 * Lightweight GLSL ES 3.00 declaration-before-use scope checker.
 *
 * The Node test suite runs against a mock WebGL2 context, so real GLSL
 * compile errors (undeclared identifiers, variables used outside the block
 * that declared them) would otherwise only surface as a black screen in the
 * browser. This tokenizer tracks brace scopes, function definitions and
 * parameters, and variable declarations, then flags any identifier used
 * without a visible declaration. It is intentionally conservative: it only
 * models declaration visibility, not types or overloads.
 */

const TYPES = new Set([
  'void', 'float', 'int', 'uint', 'bool',
  'vec2', 'vec3', 'vec4', 'ivec2', 'ivec3', 'ivec4', 'uvec2', 'uvec3', 'uvec4',
  'bvec2', 'bvec3', 'bvec4',
  'mat2', 'mat3', 'mat4', 'mat2x2', 'mat2x3', 'mat2x4', 'mat3x2', 'mat3x3', 'mat3x4', 'mat4x2', 'mat4x3', 'mat4x4',
  'sampler2D', 'sampler3D', 'samplerCube', 'sampler2DArray', 'samplerCubeArray',
  'isampler2D', 'isampler3D', 'usampler2D', 'usampler3D',
  'sampler2DShadow', 'samplerCubeShadow', 'sampler2DMS', 'image2D',
]);

const QUALIFIERS = new Set([
  'uniform', 'in', 'out', 'inout', 'const', 'highp', 'mediump', 'lowp',
  'invariant', 'precise', 'buffer', 'shared', 'coherent', 'volatile',
  'restrict', 'readonly', 'writeonly', 'attribute', 'varying',
  'flat', 'smooth', 'centroid', 'sample', 'patch', 'noperspective',
]);

const KEYWORDS = new Set([
  'if', 'else', 'for', 'while', 'do', 'switch', 'case', 'default',
  'break', 'continue', 'return', 'discard', 'struct', 'precision',
  'layout', 'true', 'false',
]);

const BUILTIN_FUNCS = new Set([
  'radians', 'degrees', 'sin', 'cos', 'tan', 'asin', 'acos', 'atan',
  'sinh', 'cosh', 'tanh', 'asinh', 'acosh', 'atanh',
  'pow', 'exp', 'log', 'exp2', 'log2', 'sqrt', 'inversesqrt',
  'abs', 'sign', 'floor', 'trunc', 'round', 'roundEven', 'ceil', 'fract',
  'mod', 'modf', 'min', 'max', 'clamp', 'mix', 'step', 'smoothstep',
  'isnan', 'isinf', 'floatBitsToInt', 'floatBitsToUint', 'intBitsToFloat', 'uintBitsToFloat',
  'fma', 'frexp', 'ldexp',
  'packSnorm4x8', 'packUnorm4x8', 'packSnorm2x16', 'packUnorm2x16', 'packHalf2x16',
  'unpackSnorm4x8', 'unpackUnorm4x8', 'unpackSnorm2x16', 'unpackUnorm2x16', 'unpackHalf2x16',
  'length', 'distance', 'dot', 'cross', 'normalize', 'faceforward', 'reflect', 'refract',
  'matrixCompMult', 'outerProduct', 'transpose', 'determinant', 'inverse',
  'lessThan', 'lessThanEqual', 'greaterThan', 'greaterThanEqual', 'equal', 'notEqual',
  'any', 'all', 'not', 'bitCount', 'findLSB', 'findMSB',
  'textureSize', 'texture', 'textureProj', 'textureLod', 'textureOffset',
  'texelFetch', 'texelFetchOffset', 'textureProjOffset', 'textureLodOffset',
  'textureProjLod', 'textureProjLodOffset', 'textureGrad', 'textureGradOffset',
  'textureProjGrad', 'textureProjGradOffset', 'textureGather', 'textureGatherOffset',
  'dFdx', 'dFdy', 'fwidth', 'dFdxCoarse', 'dFdyCoarse', 'fwidthCoarse',
  'dFdxFine', 'dFdyFine', 'fwidthFine',
  'interpolateAtCentroid', 'interpolateAtSample', 'interpolateAtOffset',
  'EmitVertex', 'EndPrimitive', 'memoryBarrier', 'barrier',
  'atomicAdd', 'atomicMin', 'atomicMax', 'atomicAnd', 'atomicOr', 'atomicXor', 'atomicExchange', 'atomicCompSwap',
  'imageSize', 'imageLoad', 'imageStore',
]);

function stripNonCode(src) {
  // Blank out comments and preprocessor lines while preserving newlines.
  return src
    .replace(/\/\*[\s\S]*?\*\//g, (m) => m.replace(/[^\n]/g, ' '))
    .replace(/\/\/[^\n]*/g, (m) => ' '.repeat(m.length))
    .replace(/^[ \t]*#[^\n]*/gm, (m) => ' '.repeat(m.length));
}

function tokenize(src) {
  const tokens = [];
  const re = /([A-Za-z_][A-Za-z0-9_]*)|(\d+\.?\d*(?:[eE][+-]?\d+)?[uU]?|\.\d+(?:[eE][+-]?\d+)?)|(\s+)|(\S)/g;
  let line = 1;
  let m;
  while ((m = re.exec(src)) !== null) {
    if (m[3] !== undefined) {
      line += (m[3].match(/\n/g) || []).length;
    } else if (m[1] !== undefined) {
      tokens.push({ t: m[1], kind: 'id', line });
    } else if (m[2] !== undefined) {
      tokens.push({ t: m[2], kind: 'num', line });
    } else {
      tokens.push({ t: m[4], kind: 'punct', line });
    }
  }
  return tokens;
}

function parseParamNames(tokens, openIdx, closeIdx) {
  const names = [];
  let segStart = openIdx + 1;
  let depth = 0;
  const flush = (end) => {
    for (let k = segStart; k < end; k++) {
      if (tokens[k].kind === 'id' && TYPES.has(tokens[k].t)) {
        for (let n = k + 1; n < end; n++) {
          if (tokens[n].kind === 'id' && !TYPES.has(tokens[n].t) && !QUALIFIERS.has(tokens[n].t)) {
            names.push(tokens[n].t);
            return;
          }
        }
        return;
      }
    }
  };
  for (let k = openIdx + 1; k < closeIdx; k++) {
    if (tokens[k].t === '(' || tokens[k].t === '[') depth++;
    else if (tokens[k].t === ')' || tokens[k].t === ']') depth--;
    else if (tokens[k].t === ',' && depth === 0) {
      flush(k);
      segStart = k + 1;
    }
  }
  flush(closeIdx);
  return names;
}

export function checkGlslScopes(source, label = 'shader') {
  const tokens = tokenize(stripNonCode(source));
  const errors = [];
  const reported = new Set();
  const scopes = [new Set()];
  const functions = new Set();
  const declaratorIdx = new Set();
  let pendingParams = null;
  let parenDepth = 0;
  let declState = null;

  const isVisible = (name) => scopes.some((s) => s.has(name));
  const report = (name, line, what) => {
    if (reported.has(name)) return;
    reported.add(name);
    errors.push(`${label}: line ${line}: ${what} '${name}' has no visible declaration`);
  };

  for (let i = 0; i < tokens.length; i++) {
    const tok = tokens[i];

    if (tok.t === '{') {
      scopes.push(new Set(pendingParams || []));
      pendingParams = null;
      declState = null;
      continue;
    }
    if (tok.t === '}') {
      if (scopes.length > 1) scopes.pop();
      declState = null;
      continue;
    }
    if (tok.t === '(') { parenDepth++; continue; }
    if (tok.t === ')') { parenDepth--; continue; }
    if (tok.t === ';') { declState = null; continue; }

    // Consume layout(...) qualifier groups wholesale.
    if (tok.kind === 'id' && tok.t === 'layout' && tokens[i + 1]?.t === '(') {
      let d = 0;
      let j = i + 1;
      for (; j < tokens.length; j++) {
        if (tokens[j].t === '(') d++;
        else if (tokens[j].t === ')') { d--; if (d === 0) break; }
      }
      i = j;
      continue;
    }

    // Additional declarator after a comma in a declaration statement.
    if (tok.t === ',' && declState && parenDepth === declState.parenDepth) {
      const nameTok = tokens[i + 1];
      const after = tokens[i + 2]?.t;
      if (nameTok?.kind === 'id' && (after === '=' || after === ';' || after === ',' || after === '[')) {
        scopes[scopes.length - 1].add(nameTok.t);
        declaratorIdx.add(i + 1);
      }
      continue;
    }

    if (tok.kind !== 'id') continue;

    // Declaration / function-definition detection: [qualifiers]* TYPE NAME ...
    if ((QUALIFIERS.has(tok.t) || TYPES.has(tok.t)) && tokens[i - 1]?.t !== '.') {
      let j = i;
      while (tokens[j] && QUALIFIERS.has(tokens[j].t)) j++;
      if (tokens[j] && TYPES.has(tokens[j].t)) {
        const nameTok = tokens[j + 1];
        const after = tokens[j + 2]?.t;
        if (nameTok?.kind === 'id' && after === '(' && scopes.length === 1) {
          // Function definition or prototype.
          functions.add(nameTok.t);
          declaratorIdx.add(j + 1);
          let d = 0;
          let close = -1;
          for (let k = j + 2; k < tokens.length; k++) {
            if (tokens[k].t === '(') d++;
            else if (tokens[k].t === ')') { d--; if (d === 0) { close = k; break; } }
          }
          if (close > -1) {
            if (tokens[close + 1]?.t === '{') {
              pendingParams = parseParamNames(tokens, j + 2, close);
            }
            i = close; // loop resumes at '{' or ';'
            continue;
          }
        } else if (nameTok?.kind === 'id' && (after === '=' || after === ';' || after === ',' || after === '[')) {
          scopes[scopes.length - 1].add(nameTok.t);
          declaratorIdx.add(j + 1);
          declState = { parenDepth };
          // Fall through: the type token itself is never a "use", and the
          // declarator token is skipped via declaratorIdx when reached.
          continue;
        }
      }
    }

    // Use checking.
    if (declaratorIdx.has(i)) continue;
    if (TYPES.has(tok.t) || QUALIFIERS.has(tok.t) || KEYWORDS.has(tok.t)) continue;
    if (tokens[i - 1]?.t === '.') continue; // field / swizzle
    if (tok.t.startsWith('gl_')) continue; // built-in variable/function
    if (tokens[i + 1]?.t === '(') {
      if (!functions.has(tok.t) && !BUILTIN_FUNCS.has(tok.t)) {
        report(tok.t, tok.line, 'call to undeclared function');
      }
    } else if (!isVisible(tok.t) && !functions.has(tok.t)) {
      report(tok.t, tok.line, 'use of undeclared identifier');
    }
  }

  return errors;
}
