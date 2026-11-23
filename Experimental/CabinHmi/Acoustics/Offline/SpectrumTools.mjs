// SPECTRUM TOOLS
// 🧩 Node-only evidence helpers for the vehicle sound renders: 16-bit WAV and MP3 writing, a radix-2 FFT, average
//    spectra and engine-order checks, and a dependency-free PNG writer that draws spectrogram and waveform plates.
// 📝 PNGs are encoded by hand (zlib deflate + CRC32) because the sandbox has no image library.

import { deflateSync } from 'node:zlib';
import { writeFileSync, readFileSync } from 'node:fs';
import { createRequire } from 'node:module';
import { createContext, runInContext } from 'node:vm';

//------------------------------------------------------------------------------------------------------------------------
//                                                    WAV
//------------------------------------------------------------------------------------------------------------------------

/** Writes a stereo 16-bit PCM WAV. Returns the peak and RMS (dBFS) of the written signal. */
export function WriteWav(Path, Left, Right, Rate)
{
    const Count = Left.length, Bytes = Count * 4;
    const Out = Buffer.alloc(44 + Bytes);
    Out.write('RIFF', 0); Out.writeUInt32LE(36 + Bytes, 4); Out.write('WAVE', 8);
    Out.write('fmt ', 12); Out.writeUInt32LE(16, 16); Out.writeUInt16LE(1, 20); Out.writeUInt16LE(2, 22);
    Out.writeUInt32LE(Rate, 24); Out.writeUInt32LE(Rate * 4, 28); Out.writeUInt16LE(4, 32); Out.writeUInt16LE(16, 34);
    Out.write('data', 36); Out.writeUInt32LE(Bytes, 40);
    let Peak = 0, Sum = 0;
    for (let Index = 0; Index < Count; Index++)
    {
        const L = Math.max(-1, Math.min(1, Left[Index])), R = Math.max(-1, Math.min(1, Right[Index]));
        Peak = Math.max(Peak, Math.abs(L), Math.abs(R)); Sum += (L * L + R * R) / 2;
        Out.writeInt16LE(Math.round(L * 32767), 44 + Index * 4);
        Out.writeInt16LE(Math.round(R * 32767), 46 + Index * 4);
    }
    writeFileSync(Path, Out);
    return { Peak: 20 * Math.log10(Peak + 1e-12), Rms: 10 * Math.log10(Sum / Count + 1e-12) };
}

/**
 * Writes a stereo MP3 through lamejs (npm install in this folder). lamejs's CommonJS entry is broken on current Node
 * (MPEGMode is not defined), so its single-file browser build is evaluated in a sandbox instead. Returns peak and RMS.
 */
let Lame = null;
export function WriteMp3(Path, Left, Right, Rate, Kbps = 192)
{
    if (!Lame)
    {
        const Script = createRequire(import.meta.url).resolve('lamejs/lame.all.js');
        const Context = createContext({ console, Math, Int8Array, Int16Array, Int32Array, Uint8Array, Float32Array, Float64Array, ArrayBuffer });
        runInContext(readFileSync(Script, 'utf8') + ';this.Found = lamejs;', Context);
        Lame = Context.Found;
    }
    const Encoder = new Lame.Mp3Encoder(2, Rate, Kbps), Chunks = [], Size = 1152;
    const SideL = new Int16Array(Size), SideR = new Int16Array(Size);
    let Peak = 0, Sum = 0;
    for (let Start = 0; Start < Left.length; Start += Size)
    {
        const Count = Math.min(Size, Left.length - Start);
        for (let Index = 0; Index < Count; Index++)
        {
            const L = Math.max(-1, Math.min(1, Left[Start + Index])), R = Math.max(-1, Math.min(1, Right[Start + Index]));
            Peak = Math.max(Peak, Math.abs(L), Math.abs(R)); Sum += (L * L + R * R) / 2;
            SideL[Index] = Math.round(L * 32767); SideR[Index] = Math.round(R * 32767);
        }
        const Encoded = Encoder.encodeBuffer(SideL.subarray(0, Count), SideR.subarray(0, Count));
        if (Encoded.length) Chunks.push(Buffer.from(Encoded));
    }
    const Tail = Encoder.flush();
    if (Tail.length) Chunks.push(Buffer.from(Tail));
    const Out = Buffer.concat(Chunks);
    writeFileSync(Path, Out);
    return { Peak: 20 * Math.log10(Peak + 1e-12), Rms: 10 * Math.log10(Sum / Left.length + 1e-12), Bytes: Out.length };
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    FFT
//------------------------------------------------------------------------------------------------------------------------

/** In-place iterative radix-2 FFT over separate real and imaginary arrays (length must be a power of two). */
export function Fft(Real, Imag)
{
    const Size = Real.length;
    for (let Index = 1, Swap = 0; Index < Size; Index++)
    {
        let Bit = Size >> 1;
        for (; Swap & Bit; Bit >>= 1) Swap ^= Bit;
        Swap ^= Bit;
        if (Index < Swap) { [Real[Index], Real[Swap]] = [Real[Swap], Real[Index]]; [Imag[Index], Imag[Swap]] = [Imag[Swap], Imag[Index]]; }
    }
    for (let Span = 2; Span <= Size; Span <<= 1)
    {
        const Angle = -2 * Math.PI / Span, StepR = Math.cos(Angle), StepI = Math.sin(Angle);
        for (let Start = 0; Start < Size; Start += Span)
        {
            let TwR = 1, TwI = 0;
            for (let Offset = 0; Offset < Span / 2; Offset++)
            {
                const A = Start + Offset, B = A + Span / 2;
                const BR = Real[B] * TwR - Imag[B] * TwI, BI = Real[B] * TwI + Imag[B] * TwR;
                Real[B] = Real[A] - BR; Imag[B] = Imag[A] - BI;
                Real[A] += BR; Imag[A] += BI;
                [TwR, TwI] = [TwR * StepR - TwI * StepI, TwR * StepI + TwI * StepR];
            }
        }
    }
}

/** Hann-windowed power spectrum (dB) of one window starting at Start; mono mix of both channels. */
export function SpectrumAt(Left, Right, Start, Size)
{
    const Real = new Float64Array(Size), Imag = new Float64Array(Size);
    for (let Index = 0; Index < Size; Index++)
    {
        const Sample = ((Left[Start + Index] ?? 0) + (Right[Start + Index] ?? 0)) / 2;
        Real[Index] = Sample * (0.5 - 0.5 * Math.cos(2 * Math.PI * Index / (Size - 1)));
    }
    Fft(Real, Imag);
    const Power = new Float64Array(Size / 2);
    for (let Bin = 0; Bin < Size / 2; Bin++) Power[Bin] = 10 * Math.log10((Real[Bin] ** 2 + Imag[Bin] ** 2) / (Size * Size) * 16 + 1e-14);
    return Power;
}

/** Average power spectrum (dB) between two times, overlapping windows. */
export function AverageSpectrum(Left, Right, Rate, From, To, Size = 8192)
{
    const Sum = new Float64Array(Size / 2);
    let Count = 0;
    for (let Start = Math.round(From * Rate); Start + Size <= Math.round(To * Rate); Start += Size / 4)
    {
        const Power = SpectrumAt(Left, Right, Start, Size);
        for (let Bin = 0; Bin < Size / 2; Bin++) Sum[Bin] += Math.pow(10, Power[Bin] / 10);
        Count++;
    }
    return Sum.map((Energy) => 10 * Math.log10(Energy / Math.max(Count, 1) + 1e-14));
}

/** Level (dB) of the strongest bin within ±Width Hz of a frequency. */
export function LevelNear(Spectrum, Rate, Hz, Width = 6)
{
    const BinHz = Rate / (Spectrum.length * 2);
    let Best = -200;
    for (let Bin = Math.max(1, Math.floor((Hz - Width) / BinHz)); Bin <= Math.ceil((Hz + Width) / BinHz); Bin++) Best = Math.max(Best, Spectrum[Bin] ?? -200);
    return Best;
}

/** Spectral centroid (Hz) of a dB spectrum between Low and High. */
export function Centroid(Spectrum, Rate, Low = 40, High = 12000)
{
    const BinHz = Rate / (Spectrum.length * 2);
    let Weighted = 0, Total = 0;
    for (let Bin = Math.ceil(Low / BinHz); Bin < Math.min(Spectrum.length, High / BinHz); Bin++)
    { const Energy = Math.pow(10, Spectrum[Bin] / 10); Weighted += Energy * Bin * BinHz; Total += Energy; }
    return Weighted / Total;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    PNG
//------------------------------------------------------------------------------------------------------------------------

const CrcTable = Array.from({ length: 256 }, (_, Number) => { let C = Number; for (let K = 0; K < 8; K++) C = C & 1 ? 0xedb88320 ^ (C >>> 1) : C >>> 1; return C >>> 0; });
function Crc(Bytes) { let C = 0xffffffff; for (const Byte of Bytes) C = CrcTable[(C ^ Byte) & 255] ^ (C >>> 8); return (C ^ 0xffffffff) >>> 0; }
function Chunk(Type, Body)
{
    const Head = Buffer.alloc(8); Head.writeUInt32BE(Body.length, 0); Head.write(Type, 4);
    const Tail = Buffer.alloc(4); Tail.writeUInt32BE(Crc(Buffer.concat([Buffer.from(Type), Body])), 0);
    return Buffer.concat([Head, Body, Tail]);
}

/** RGB canvas with a few drawing calls; Save() writes a PNG. */
export class Plate
{
    constructor(Width, Height, Fill = [12, 13, 16]) { this.Width = Width; this.Height = Height; this.Pixels = new Uint8Array(Width * Height * 3); this.Box(0, 0, Width, Height, Fill); }
    Dot(X, Y, Colour) { X |= 0; Y |= 0; if (X < 0 || Y < 0 || X >= this.Width || Y >= this.Height) return; const At = (Y * this.Width + X) * 3; this.Pixels[At] = Colour[0]; this.Pixels[At + 1] = Colour[1]; this.Pixels[At + 2] = Colour[2]; }
    Box(X, Y, W, H, Colour) { for (let Row = Y; Row < Y + H; Row++) for (let Col = X; Col < X + W; Col++) this.Dot(Col, Row, Colour); }
    Line(X0, Y0, X1, Y1, Colour) { const Steps = Math.max(Math.abs(X1 - X0), Math.abs(Y1 - Y0), 1); for (let S = 0; S <= Steps; S++) this.Dot(X0 + (X1 - X0) * S / Steps, Y0 + (Y1 - Y0) * S / Steps, Colour); }
    Text(X, Y, Words, Colour = [210, 214, 222], Scale = 1)
    {
        let Cursor = X;
        for (const Letter of String(Words).toUpperCase())
        {
            const Rows = Glyphs[Letter] ?? Glyphs['?'];
            for (let Row = 0; Row < 7; Row++) for (let Col = 0; Col < 5; Col++) if (Rows[Row] >> (4 - Col) & 1) this.Box(Cursor + Col * Scale, Y + Row * Scale, Scale, Scale, Colour);
            Cursor += 6 * Scale;
        }
    }
    Save(Path)
    {
        const Raw = Buffer.alloc((this.Width * 3 + 1) * this.Height);
        for (let Row = 0; Row < this.Height; Row++) { Raw[Row * (this.Width * 3 + 1)] = 0; Buffer.from(this.Pixels.buffer, Row * this.Width * 3, this.Width * 3).copy(Raw, Row * (this.Width * 3 + 1) + 1); }
        const Header = Buffer.alloc(13); Header.writeUInt32BE(this.Width, 0); Header.writeUInt32BE(this.Height, 4); Header[8] = 8; Header[9] = 2;
        writeFileSync(Path, Buffer.concat([Buffer.from([137, 80, 78, 71, 13, 10, 26, 10]), Chunk('IHDR', Header), Chunk('IDAT', deflateSync(Raw, { level: 9 })), Chunk('IEND', Buffer.alloc(0))]));
    }
}

// 5×7 bitmap font: digits, capitals and a little punctuation, enough for axis labels and titles.
const Glyphs = {
    '0': [14, 17, 19, 21, 25, 17, 14], '1': [4, 12, 4, 4, 4, 4, 14], '2': [14, 17, 1, 2, 4, 8, 31], '3': [31, 2, 4, 2, 1, 17, 14],
    '4': [2, 6, 10, 18, 31, 2, 2], '5': [31, 16, 30, 1, 1, 17, 14], '6': [6, 8, 16, 30, 17, 17, 14], '7': [31, 1, 2, 4, 8, 8, 8],
    '8': [14, 17, 17, 14, 17, 17, 14], '9': [14, 17, 17, 15, 1, 2, 12], 'A': [14, 17, 17, 31, 17, 17, 17], 'B': [30, 17, 17, 30, 17, 17, 30],
    'C': [14, 17, 16, 16, 16, 17, 14], 'D': [28, 18, 17, 17, 17, 18, 28], 'E': [31, 16, 16, 30, 16, 16, 31], 'F': [31, 16, 16, 30, 16, 16, 16],
    'G': [14, 17, 16, 23, 17, 17, 15], 'H': [17, 17, 17, 31, 17, 17, 17], 'I': [14, 4, 4, 4, 4, 4, 14], 'J': [7, 2, 2, 2, 2, 18, 12],
    'K': [17, 18, 20, 24, 20, 18, 17], 'L': [16, 16, 16, 16, 16, 16, 31], 'M': [17, 27, 21, 21, 17, 17, 17], 'N': [17, 17, 25, 21, 19, 17, 17],
    'O': [14, 17, 17, 17, 17, 17, 14], 'P': [30, 17, 17, 30, 16, 16, 16], 'Q': [14, 17, 17, 17, 21, 18, 13], 'R': [30, 17, 17, 30, 20, 18, 17],
    'S': [15, 16, 16, 14, 1, 1, 30], 'T': [31, 4, 4, 4, 4, 4, 4], 'U': [17, 17, 17, 17, 17, 17, 14], 'V': [17, 17, 17, 17, 17, 10, 4],
    'W': [17, 17, 17, 21, 21, 21, 10], 'X': [17, 17, 10, 4, 10, 17, 17], 'Y': [17, 17, 10, 4, 4, 4, 4], 'Z': [31, 1, 2, 4, 8, 16, 31],
    ' ': [0, 0, 0, 0, 0, 0, 0], '.': [0, 0, 0, 0, 0, 12, 12], ',': [0, 0, 0, 0, 12, 4, 8], ':': [0, 12, 12, 0, 12, 12, 0],
    '-': [0, 0, 0, 31, 0, 0, 0], '+': [0, 4, 4, 31, 4, 4, 0], '/': [1, 1, 2, 4, 8, 16, 16], '(': [2, 4, 8, 8, 8, 4, 2], ')': [8, 4, 2, 2, 2, 4, 8],
    '%': [24, 25, 2, 4, 8, 19, 3], '=': [0, 0, 31, 0, 31, 0, 0], '?': [14, 17, 1, 2, 4, 0, 4], '·': [0, 0, 0, 4, 0, 0, 0], "'": [4, 4, 8, 0, 0, 0, 0],
};

/** Magma-like ramp: black → purple → orange → pale yellow. */
function Heat(Amount)
{
    const Stops = [[0, 0, 4], [40, 11, 84], [101, 21, 110], [159, 42, 99], [212, 72, 66], [245, 125, 21], [250, 193, 39], [252, 255, 164]];
    const Position = Math.max(0, Math.min(1, Amount)) * (Stops.length - 1), Low = Math.floor(Position), Part = Position - Low;
    const A = Stops[Low], B = Stops[Math.min(Low + 1, Stops.length - 1)];
    return [A[0] + (B[0] - A[0]) * Part, A[1] + (B[1] - A[1]) * Part, A[2] + (B[2] - A[2]) * Part];
}

/**
 * Draws a titled plate: a waveform strip on top and a log-frequency spectrogram (30 Hz – 16 kHz) underneath, with time
 * and frequency labels and optional marker lines (e.g. scene beats). Overlay: optional [[time, Hz], …] trace drawn in
 * cyan (e.g. the predicted firing frequency) so the spectrogram can be checked against the physics.
 */
export function DrawSpectrogram(Path, Left, Right, Rate, { Title = '', Subtitle = '', Marks = [], Overlay = null, Floor = -96 } = {})
{
    const Width = 1400, Height = 640, Pad = 64, Top = 64, WaveHeight = 110, Gap = 18;
    const Plot = { X: Pad, Y: Top + WaveHeight + Gap, W: Width - Pad - 24, H: Height - Top - WaveHeight - Gap - 46 };
    const Canvas = new Plate(Width, Height);
    Canvas.Text(Pad, 18, Title, [240, 242, 246], 2);
    Canvas.Text(Pad, 42, Subtitle, [150, 156, 168], 1);
    const Seconds = Left.length / Rate;

    // Waveform (min/max per column, both channels averaged).
    const Mid = Top + WaveHeight / 2;
    Canvas.Box(Plot.X, Top, Plot.W, WaveHeight, [20, 22, 27]);
    for (let Col = 0; Col < Plot.W; Col++)
    {
        const From = Math.floor(Col / Plot.W * Left.length), To = Math.floor((Col + 1) / Plot.W * Left.length);
        let Low = 0, High = 0;
        for (let Index = From; Index < To; Index++) { const S = (Left[Index] + Right[Index]) / 2; if (S < Low) Low = S; if (S > High) High = S; }
        Canvas.Line(Plot.X + Col, Mid - High * WaveHeight / 2, Plot.X + Col, Mid - Low * WaveHeight / 2, [120, 200, 255]);
    }

    // Spectrogram.
    const Size = 4096, Low = 30, High = 16000, Span = Math.log(High / Low);
    for (let Col = 0; Col < Plot.W; Col++)
    {
        const Centre = Math.floor((Col + 0.5) / Plot.W * Left.length);
        const Power = SpectrumAt(Left, Right, Math.max(0, Centre - Size / 2), Size);
        for (let Row = 0; Row < Plot.H; Row++)
        {
            const HzLow = Low * Math.exp(Span * (Plot.H - Row - 1) / Plot.H), HzHigh = Low * Math.exp(Span * (Plot.H - Row) / Plot.H);
            let Best = -200;
            for (let Bin = Math.floor(HzLow / Rate * Size); Bin <= Math.ceil(HzHigh / Rate * Size); Bin++) Best = Math.max(Best, Power[Bin] ?? -200);
            Canvas.Dot(Plot.X + Col, Plot.Y + Row, Heat((Best - Floor) / -Floor));
        }
    }
    const HzToRow = (Hz) => Plot.Y + Plot.H - Math.log(Hz / Low) / Span * Plot.H;
    for (const Hz of [50, 100, 200, 500, 1000, 2000, 5000, 10000])
    {
        const Row = HzToRow(Hz);
        for (let Col = Plot.X; Col < Plot.X + Plot.W; Col += 6) Canvas.Dot(Col, Row, [90, 96, 110]);
        Canvas.Text(8, Row - 3, Hz >= 1000 ? `${Hz / 1000}K` : String(Hz), [150, 156, 168]);
    }
    const Tick = Seconds > 20 ? 5 : Seconds > 8 ? 2 : Seconds > 3 ? 1 : 0.5;
    for (let Time = 0; Time <= Seconds + 1e-6; Time += Tick)
    {
        const Col = Plot.X + Time / Seconds * Plot.W;
        Canvas.Line(Col, Plot.Y + Plot.H, Col, Plot.Y + Plot.H + 5, [150, 156, 168]);
        Canvas.Text(Col - 6, Plot.Y + Plot.H + 10, `${+Time.toFixed(1)}S`, [150, 156, 168]);
    }
    if (Overlay) for (const [Time, Hz] of Overlay) if (Hz > Low && Hz < High) Canvas.Box(Plot.X + Time / Seconds * Plot.W - 1, HzToRow(Hz) - 1, 2, 2, [80, 240, 255]);
    for (const [Time, Words] of Marks)
    {
        const Col = Plot.X + Time / Seconds * Plot.W;
        for (let Row = Top; Row < Plot.Y + Plot.H; Row += 3) Canvas.Dot(Col, Row, [255, 255, 255]);
        Canvas.Text(Col + 4, Plot.Y + 4, Words, [255, 255, 255]);
    }
    Canvas.Text(Pad, Height - 18, 'TOP: WAVEFORM   BOTTOM: SPECTROGRAM, LOG FREQUENCY 30 HZ - 16 KHZ, 4096-POINT HANN', [110, 116, 128]);
    Canvas.Save(Path);
}
