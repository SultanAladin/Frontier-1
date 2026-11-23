//============================================================================================================================================
//                                                          GPUCONTEXT.JS
//                 WebGL2 Helper Layer — Program Compilation, Half-Float Render Targets, Ping-Pong Pairs,
//                            Fullscreen Quad Dispatch, and Tiled 3D Volume Texture Layout
//============================================================================================================================================

export class GpuContext
{
    constructor(canvas)
    {
        this.canvas = canvas;
        const gl = canvas.getContext('webgl2', { antialias: false, alpha: false, premultipliedAlpha: false, preserveDrawingBuffer: false, powerPreference: 'high-performance' });
        if (!gl) throw new Error('WebGL2 is unavailable in this browser.');
        this.gl = gl;
        this.colourBufferFloat = gl.getExtension('EXT_color_buffer_float');
        this.colourBufferHalfFloat = gl.getExtension('EXT_color_buffer_half_float');
        if (!this.colourBufferFloat && !this.colourBufferHalfFloat)
            throw new Error('Float render targets (EXT_color_buffer_float) are required for the fluid solver.');
        gl.getExtension('OES_texture_float_linear');
        this.programCache = new Map();
        this.quadVao = this.createQuadVao();
    }

    //------------------------------------------------------------------------------------------------------------------------
    //                                                  PROGRAM COMPILATION
    //------------------------------------------------------------------------------------------------------------------------

    compileShader(type, source, label)
    {
        const gl = this.gl;
        const shader = gl.createShader(type);
        gl.shaderSource(shader, source);
        gl.compileShader(shader);
        if (!gl.getShaderParameter(shader, gl.COMPILE_STATUS))
        {
            const log = gl.getShaderInfoLog(shader);
            const numbered = source.split('\n').map((line, index) => `${String(index + 1).padStart(4)} | ${line}`).join('\n');
            console.error(`[GpuContext] Shader "${label}" failed to compile:\n${log}\n${numbered}`);
            throw new Error(`Shader compile failure (${label}): ${log}`);
        }
        return shader;
    }

    createProgram(label, vertexSource, fragmentSource, uniformNames = [])
    {
        const gl = this.gl;
        const vertex = this.compileShader(gl.VERTEX_SHADER, vertexSource, label + '.vert');
        const fragment = this.compileShader(gl.FRAGMENT_SHADER, fragmentSource, label + '.frag');
        const program = gl.createProgram();
        gl.attachShader(program, vertex);
        gl.attachShader(program, fragment);
        gl.linkProgram(program);
        if (!gl.getProgramParameter(program, gl.LINK_STATUS))
        {
            const log = gl.getProgramInfoLog(program);
            console.error(`[GpuContext] Program "${label}" failed to link:\n${log}`);
            throw new Error(`Program link failure (${label}): ${log}`);
        }
        gl.deleteShader(vertex);
        gl.deleteShader(fragment);
        const uniforms = {};
        const count = gl.getProgramParameter(program, gl.ACTIVE_UNIFORMS);
        for (let index = 0; index < count; index += 1)
        {
            const info = gl.getActiveUniform(program, index);
            const name = info.name.replace(/\[0\]$/, '');
            uniforms[name] = gl.getUniformLocation(program, info.name);
        }
        for (const name of uniformNames) if (!(name in uniforms)) uniforms[name] = gl.getUniformLocation(program, name);
        return { program, uniforms, label };
    }

    use(programRecord)
    {
        this.gl.useProgram(programRecord.program);
        this.activeProgram = programRecord;
        this.textureUnit = 0;
        return programRecord.uniforms;
    }

    bindTexture(uniformName, texture)
    {
        const gl = this.gl;
        const location = this.activeProgram.uniforms[uniformName];
        if (location === undefined || location === null) return;
        gl.activeTexture(gl.TEXTURE0 + this.textureUnit);
        gl.bindTexture(gl.TEXTURE_2D, texture);
        gl.uniform1i(location, this.textureUnit);
        this.textureUnit += 1;
    }

    //------------------------------------------------------------------------------------------------------------------------
    //                                                  RENDER TARGETS
    //------------------------------------------------------------------------------------------------------------------------

    createTarget(width, height, options = {})
    {
        const gl = this.gl;
        const filter = options.nearest ? gl.NEAREST : gl.LINEAR;
        const internalFormat = options.rgba32f ? gl.RGBA32F : gl.RGBA16F;
        const texture = gl.createTexture();
        gl.bindTexture(gl.TEXTURE_2D, texture);
        gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, options.rgba32f ? gl.NEAREST : filter);
        gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, options.rgba32f ? gl.NEAREST : filter);
        gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
        gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
        gl.texStorage2D(gl.TEXTURE_2D, 1, internalFormat, width, height);
        const framebuffer = gl.createFramebuffer();
        gl.bindFramebuffer(gl.FRAMEBUFFER, framebuffer);
        gl.framebufferTexture2D(gl.FRAMEBUFFER, gl.COLOR_ATTACHMENT0, gl.TEXTURE_2D, texture, 0);
        const status = gl.checkFramebufferStatus(gl.FRAMEBUFFER);
        if (status !== gl.FRAMEBUFFER_COMPLETE) throw new Error('Framebuffer incomplete: 0x' + status.toString(16));
        gl.bindFramebuffer(gl.FRAMEBUFFER, null);
        return { texture, framebuffer, width, height, rgba32f: Boolean(options.rgba32f), nearest: Boolean(options.nearest) };
    }

    /** Exact texel copy between two targets of the same size and format (timeline checkpoints). */
    copyTarget(source, destination)
    {
        const gl = this.gl;
        gl.bindFramebuffer(gl.READ_FRAMEBUFFER, source.framebuffer);
        gl.bindFramebuffer(gl.DRAW_FRAMEBUFFER, destination.framebuffer);
        gl.blitFramebuffer(0, 0, source.width, source.height, 0, 0, destination.width, destination.height, gl.COLOR_BUFFER_BIT, gl.NEAREST);
        gl.bindFramebuffer(gl.READ_FRAMEBUFFER, null);
        gl.bindFramebuffer(gl.DRAW_FRAMEBUFFER, null);
    }

    /** Integer lookup texture (R16I or RGBA32I), nearest-filtered, uploaded with uploadIntegerTexture(). */
    createIntegerTexture(width, height, channels = 1)
    {
        const gl = this.gl;
        const texture = gl.createTexture();
        gl.bindTexture(gl.TEXTURE_2D, texture);
        gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.NEAREST);
        gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.NEAREST);
        gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
        gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
        gl.texStorage2D(gl.TEXTURE_2D, 1, channels === 4 ? gl.RGBA32I : gl.R16I, width, height);
        return { texture, width, height, channels };
    }

    uploadIntegerTexture(record, data)
    {
        const gl = this.gl;
        gl.bindTexture(gl.TEXTURE_2D, record.texture);
        gl.pixelStorei(gl.UNPACK_ALIGNMENT, 1);
        if (record.channels === 4) gl.texSubImage2D(gl.TEXTURE_2D, 0, 0, 0, record.width, record.height, gl.RGBA_INTEGER, gl.INT, data);
        else gl.texSubImage2D(gl.TEXTURE_2D, 0, 0, 0, record.width, record.height, gl.RED_INTEGER, gl.SHORT, data);
    }

    /** Float RGBA texture uploaded once (object texels, spawn anchors). Nearest-filtered, no mips. */
    createDataTexture(width, height, data)
    {
        const gl = this.gl;
        const texture = gl.createTexture();
        gl.bindTexture(gl.TEXTURE_2D, texture);
        gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.NEAREST);
        gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.NEAREST);
        gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
        gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
        gl.texStorage2D(gl.TEXTURE_2D, 1, gl.RGBA32F, width, height);
        if (data) gl.texSubImage2D(gl.TEXTURE_2D, 0, 0, 0, width, height, gl.RGBA, gl.FLOAT, data);
        return { texture, width, height };
    }

    destroyDataTexture(record)
    {
        if (record) this.gl.deleteTexture(record.texture);
    }

    /** Clears a rectangle of a render target (scissored). */
    clearRegion(target, x, y, width, height)
    {
        const gl = this.gl;
        gl.bindFramebuffer(gl.FRAMEBUFFER, target.framebuffer);
        gl.viewport(0, 0, target.width, target.height);
        gl.enable(gl.SCISSOR_TEST);
        gl.scissor(x, y, width, height);
        gl.clearColor(0, 0, 0, 0);
        gl.clear(gl.COLOR_BUFFER_BIT);
        gl.disable(gl.SCISSOR_TEST);
    }

    /** Synchronous float readback of a render target. */
    readTarget(target, buffer)
    {
        const gl = this.gl;
        gl.bindFramebuffer(gl.FRAMEBUFFER, target.framebuffer);
        gl.readPixels(0, 0, target.width, target.height, gl.RGBA, gl.FLOAT, buffer);
        return buffer;
    }

    createPingPong(width, height, options = {})
    {
        const pair = { read: this.createTarget(width, height, options), write: this.createTarget(width, height, options), width, height };
        pair.swap = () => { const read = pair.read; pair.read = pair.write; pair.write = read; };
        return pair;
    }

    destroyTarget(target)
    {
        const gl = this.gl;
        gl.deleteFramebuffer(target.framebuffer);
        gl.deleteTexture(target.texture);
    }

    destroyPingPong(pair)
    {
        this.destroyTarget(pair.read);
        this.destroyTarget(pair.write);
    }

    clearTarget(target, r = 0, g = 0, b = 0, a = 0)
    {
        const gl = this.gl;
        gl.bindFramebuffer(gl.FRAMEBUFFER, target.framebuffer);
        gl.viewport(0, 0, target.width, target.height);
        gl.clearColor(r, g, b, a);
        gl.clear(gl.COLOR_BUFFER_BIT);
    }

    //------------------------------------------------------------------------------------------------------------------------
    //                                                  FULLSCREEN DISPATCH
    //------------------------------------------------------------------------------------------------------------------------

    createQuadVao()
    {
        const gl = this.gl;
        const vao = gl.createVertexArray();
        gl.bindVertexArray(vao);
        const buffer = gl.createBuffer();
        gl.bindBuffer(gl.ARRAY_BUFFER, buffer);
        gl.bufferData(gl.ARRAY_BUFFER, new Float32Array([-1, -1, 3, -1, -1, 3]), gl.STATIC_DRAW);
        gl.enableVertexAttribArray(0);
        gl.vertexAttribPointer(0, 2, gl.FLOAT, false, 0, 0);
        gl.bindVertexArray(null);
        return vao;
    }

    dispatch(target)
    {
        const gl = this.gl;
        gl.bindFramebuffer(gl.FRAMEBUFFER, target ? target.framebuffer : null);
        gl.viewport(0, 0, target ? target.width : this.canvas.width, target ? target.height : this.canvas.height);
        gl.bindVertexArray(this.quadVao);
        gl.drawArrays(gl.TRIANGLES, 0, 3);
        gl.bindVertexArray(null);
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  TILED VOLUME LAYOUT
//------------------------------------------------------------------------------------------------------------------------

export function computeVolumeLayout(width, height, depth)
{
    const tilesX = Math.ceil(Math.sqrt(depth));
    const tilesY = Math.ceil(depth / tilesX);
    return { width, height, depth, tilesX, tilesY, textureWidth: width * tilesX, textureHeight: height * tilesY, cellCount: width * height * depth };
}
