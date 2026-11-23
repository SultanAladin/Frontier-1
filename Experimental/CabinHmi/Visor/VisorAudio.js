// VISOR AUDIO
// 🧩 Every sound in the Visor demo, synthesised live with WebAudio (no audio files): the engine (firing note, sub growl
//    and intake hiss that follow RPM and throttle), road and wind roar, the rumble strip, the fuel pump, and the HUD cues
//    (info chime, warning beeps, urgent BRAKE tones, ping blips per ping kind, team chime, countdown ticks, accept and
//    dismiss, ping-wheel hover), plus the whoosh of passing traffic and the crash thump.
// 📝 Browsers only start audio after a user gesture: Wake() runs on the first click or key press. M mutes.

//------------------------------------------------------------------------------------------------------------------------
//                                                    SOUND DESK
//------------------------------------------------------------------------------------------------------------------------

export function SoundDesk()
{
    let Context = null, Master = null, Muted = false;
    const Live = {};                                              // continuous voices, filled in Build()
    let NoiseBuffer = null;

    function Build()
    {
        Context = new (window.AudioContext || window.webkitAudioContext)();
        Master = Context.createGain();
        Master.gain.value = 0.7;
        const Limiter = Context.createDynamicsCompressor();
        Limiter.threshold.value = -10;
        Limiter.ratio.value = 6;
        Master.connect(Limiter).connect(Context.destination);

        NoiseBuffer = Context.createBuffer(1, Context.sampleRate * 2, Context.sampleRate);
        const Samples = NoiseBuffer.getChannelData(0);
        for (let Index = 0; Index < Samples.length; Index++) Samples[Index] = Math.random() * 2 - 1;

        // Engine: firing note (saw), half-order growl (square), sub (sine) → low-pass that opens with throttle.
        const EngineBus = Context.createGain();
        EngineBus.gain.value = 0;
        const EngineTone = Context.createBiquadFilter();
        EngineTone.type = 'lowpass';
        EngineTone.Q.value = 3;
        EngineTone.frequency.value = 400;
        EngineTone.connect(EngineBus).connect(Master);
        const Voices = [['sawtooth', 1, 0.5], ['square', 0.5, 0.22], ['sine', 0.25, 0.7], ['sawtooth', 2.01, 0.12]].map(([Type, Ratio, Level]) =>
        {
            const Voice = Context.createOscillator();
            Voice.type = Type;
            const Gain = Context.createGain();
            Gain.gain.value = Level;
            Voice.connect(Gain).connect(EngineTone);
            Voice.start();
            return { Voice, Ratio };
        });
        Object.assign(Live, { EngineBus, EngineTone, Voices });

        // Road roar (band-passed noise), wind (high-passed noise) and rumble strip (low noise gated at 22 Hz).
        const Noise = (Type, Frequency, Q) =>
        {
            const Source = Context.createBufferSource();
            Source.buffer = NoiseBuffer;
            Source.loop = true;
            const Filter = Context.createBiquadFilter();
            Filter.type = Type;
            Filter.frequency.value = Frequency;
            Filter.Q.value = Q;
            const Gain = Context.createGain();
            Gain.gain.value = 0;
            Source.connect(Filter).connect(Gain).connect(Master);
            Source.start(0, Math.random() * 2);
            return { Filter, Gain };
        };
        Live.Road = Noise('bandpass', 380, 0.6);
        Live.Wind = Noise('highpass', 1400, 0.4);
        Live.Rumble = Noise('lowpass', 140, 1.2);
        const Gate = Context.createOscillator();
        Gate.type = 'square';
        Gate.frequency.value = 22;
        const GateDepth = Context.createGain();
        GateDepth.gain.value = 0;
        Gate.connect(GateDepth).connect(Live.Rumble.Gain.gain);
        Gate.start();
        Live.RumbleDepth = GateDepth;
        Live.Pump = Noise('bandpass', 900, 3);
        const Hum = Context.createOscillator();
        Hum.frequency.value = 118;
        const HumGain = Context.createGain();
        HumGain.gain.value = 0;
        Hum.connect(HumGain).connect(Master);
        Hum.start();
        Live.Hum = HumGain.gain;                                    // the AudioParam, not the node
    }

    const Ease = (Param, Value, Time = 0.08) => { if (Number.isFinite(Value)) Param.setTargetAtTime(Value, Context.currentTime, Time); };
    // A sound fault must never stop the game: report it once and carry on silently for that call.
    let Reported = false;
    const Guard = (Work) => { try { Work(); } catch (Fault) { if (!Reported) { Reported = true; console.warn('Visor sound:', Fault); } } };
    let LastDrive = -1;

    // One shaped tone: optional glide, attack and exponential release, optional stereo pan.
    function Tone(Frequency, { At = 0, Length = 0.12, Type = 'sine', Level = 0.18, Glide = null, Pan = 0, Attack = 0.006 } = {})
    {
        if (!Context || Muted) return;
        Level = Math.max(0.0002, Number.isFinite(Level) ? Level : 0.1);   // exponential ramps need a positive target
        const Start = Context.currentTime + At;
        const Voice = Context.createOscillator();
        Voice.type = Type;
        Voice.frequency.setValueAtTime(Frequency, Start);
        if (Glide) Voice.frequency.exponentialRampToValueAtTime(Glide, Start + Length);
        const Gain = Context.createGain();
        Gain.gain.setValueAtTime(0.0001, Start);
        Gain.gain.exponentialRampToValueAtTime(Level, Start + Attack);
        Gain.gain.exponentialRampToValueAtTime(0.0001, Start + Length);
        const Panner = Context.createStereoPanner();
        Panner.pan.value = Pan;
        Voice.connect(Gain).connect(Panner).connect(Master);
        Voice.start(Start);
        Voice.stop(Start + Length + 0.05);
    }
    // Filtered noise burst with a sweep (whoosh, crash).
    function Burst({ At = 0, Length = 1, From = 300, Peak = 1400, Level = 0.3, Pan = 0, Type = 'bandpass', Q = 0.9 } = {})
    {
        if (!Context || Muted) return;
        Level = Math.max(0.0002, Number.isFinite(Level) ? Level : 0.1);
        Pan = Number.isFinite(Pan) ? Math.max(-1, Math.min(1, Pan)) : 0;
        const Start = Context.currentTime + At;
        const Source = Context.createBufferSource();
        Source.buffer = NoiseBuffer;
        const Filter = Context.createBiquadFilter();
        Filter.type = Type;
        Filter.Q.value = Q;
        Filter.frequency.setValueAtTime(From, Start);
        Filter.frequency.exponentialRampToValueAtTime(Peak, Start + Length * 0.45);
        Filter.frequency.exponentialRampToValueAtTime(From, Start + Length);
        const Gain = Context.createGain();
        Gain.gain.setValueAtTime(0.0001, Start);
        Gain.gain.exponentialRampToValueAtTime(Level, Start + Length * 0.45);
        Gain.gain.exponentialRampToValueAtTime(0.0001, Start + Length);
        const Panner = Context.createStereoPanner();
        Panner.pan.setValueAtTime(Pan, Start);
        Panner.pan.linearRampToValueAtTime(-Pan * 0.6, Start + Length);
        Source.connect(Filter).connect(Gain).connect(Panner).connect(Master);
        Source.start(Start, Math.random());
        Source.stop(Start + Length + 0.05);
    }

    const Cues = {
        Notice:    () => { Tone(988, { Length: 0.16, Level: 0.12 }); Tone(1319, { At: 0.09, Length: 0.24, Level: 0.12 }); },
        Warn:    () => { for (let Beep = 0; Beep < 3; Beep++) Tone(740, { At: Beep * 0.15, Length: 0.1, Type: 'triangle', Level: 0.2 }); },
        Danger:  () => { Tone(1046, { Length: 0.1, Type: 'square', Level: 0.09 }); Tone(784, { At: 0.12, Length: 0.1, Type: 'square', Level: 0.09 }); },
        Team:    () => { [523, 784, 1046].forEach((Note, Index) => Tone(Note, { At: Index * 0.07, Length: 0.3, Type: 'triangle', Level: 0.11 })); },
        Accept:  () => { Tone(660, { Length: 0.18, Glide: 1320, Level: 0.13 }); },
        Dismiss: () => { Tone(620, { Length: 0.12, Glide: 380, Level: 0.1 }); },
        Tick:    (Urgent) => { Tone(Urgent ? 2400 : 1800, { Length: 0.04, Type: 'square', Level: Urgent ? 0.07 : 0.04 }); },
        Hover:   () => { Tone(2200, { Length: 0.03, Level: 0.05 }); },
        Open:    () => { Burst({ Length: 0.22, From: 500, Peak: 2400, Level: 0.05, Type: 'highpass', Q: 0.5 }); },
        Crash:   () => { Burst({ Length: 0.9, From: 120, Peak: 900, Level: 0.6, Type: 'lowpass', Q: 0.4 }); Tone(55, { Length: 0.6, Level: 0.5, Glide: 30 }); },
        Whoosh:    (Pan, Level = 0.32) => { Burst({ Length: 1.1, From: 260, Peak: 1300, Level, Pan }); },
        // Ping blips: each kind has its own voice, like a squad game. Enemy is sharp and double, location is a bright drop.
        Ping:    (Call) =>
        {
            const Voices = {
                Going:   () => { Tone(1568, { Length: 0.09, Level: 0.14 }); Tone(2093, { At: 0.06, Length: 0.25, Level: 0.12 }); },
                Enemy:   () => { Tone(1760, { Length: 0.08, Type: 'sawtooth', Level: 0.08 }); Tone(1175, { At: 0.1, Length: 0.08, Type: 'sawtooth', Level: 0.08 }); Tone(1760, { At: 0.2, Length: 0.12, Type: 'sawtooth', Level: 0.08 }); },
                Danger:  () => { Tone(880, { Length: 0.14, Type: 'triangle', Level: 0.16 }); Tone(660, { At: 0.12, Length: 0.22, Type: 'triangle', Level: 0.16 }); },
                Supply:  () => { Tone(1046, { Length: 0.1, Level: 0.13 }); Tone(1568, { At: 0.08, Length: 0.22, Level: 0.12 }); },
                Fuel:    () => { Tone(784, { Length: 0.12, Type: 'triangle', Level: 0.14 }); Tone(1046, { At: 0.1, Length: 0.2, Type: 'triangle', Level: 0.13 }); },
                Watch:   () => { Tone(1318, { Length: 0.3, Glide: 1760, Level: 0.1 }); },
                Hold:    () => { Tone(587, { Length: 0.2, Type: 'triangle', Level: 0.15 }); Tone(587, { At: 0.18, Length: 0.2, Type: 'triangle', Level: 0.12 }); },
                Regroup: () => { [784, 988, 1175].forEach((Note, Index) => Tone(Note, { At: Index * 0.06, Length: 0.2, Level: 0.11 })); },
            };
            (Voices[Call] ?? Voices.Going)();
        },
    };

    return {
        Wake()
        {
            Guard(() =>
            {
                if (!Context) Build();
                if (Context.state === 'suspended') Context.resume();
            });
        },
        get Awake() { return !!Context && Context.state === 'running'; },
        get Muted() { return Muted; },
        ToggleMute()
        {
            Muted = !Muted;
            if (Context) Ease(Master.gain, Muted ? 0 : 0.7, 0.05);
            return Muted;
        },
        Play(Name, ...Options) { if (Context && !Muted && Cues[Name]) Guard(() => Cues[Name](...Options)); },
        // Called every frame with the car's state; all moves are smoothed so frame hitches never click.
        Drive({ Rpm, Throttle, Speed, Rumble, Fuelling, Running = true })
        {
            if (!Context || Context.state !== 'running') return;
            // About 30 updates a second is plenty for smoothed parameters and keeps the automation timeline short.
            if (Context.currentTime - LastDrive < 0.033) return;
            LastDrive = Context.currentTime;
            Guard(() =>
            {
            const Firing = (Rpm / 60) * 2;                                 // four-cylinder: two firings per turn
            for (const { Voice, Ratio } of Live.Voices) Ease(Voice.frequency, Firing * Ratio, 0.05);
            Ease(Live.EngineTone.frequency, 260 + Throttle * 1500 + Rpm * 0.12, 0.08);
            Ease(Live.EngineBus.gain, Running ? 0.07 + Throttle * 0.1 + Math.min(1, Rpm / 6000) * 0.05 : 0, 0.1);
            const Pace = Math.min(1, Math.abs(Speed) / 36);
            Ease(Live.Road.Gain.gain, Pace * 0.18, 0.2);
            Ease(Live.Road.Filter.frequency, 260 + Pace * 400, 0.2);
            Ease(Live.Wind.Gain.gain, Pace * Pace * 0.09, 0.3);
            Ease(Live.Rumble.Gain.gain, Rumble * Math.min(1, Math.abs(Speed) / 8) * 0.25, 0.04);
            Ease(Live.RumbleDepth.gain, Rumble * Math.min(1, Math.abs(Speed) / 8) * 0.25, 0.04);
            Live.Rumble.Filter.frequency.value = 90 + Math.abs(Speed) * 3;
            Ease(Live.Pump.Gain.gain, Fuelling ? 0.05 : 0, 0.2);
            Ease(Live.Hum, Fuelling ? 0.04 : 0, 0.2);
            });
        },
    };
}
